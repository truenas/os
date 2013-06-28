/*-
 * Copyright (c) 2009 Gleb Kurtsou <gleb@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/dirent.h>
#include <sys/endian.h>
#include <sys/lock.h>
#include <sys/libkern.h>
#include <sys/limits.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/refcount.h>
#include <sys/queue.h>
#include <sys/vnode.h>
#include <vm/uma.h>

#include <opencrypto/cryptodev.h>

#include <fs/pefs/pefs.h>
#include <fs/pefs/pefs_crypto.h>

#define	PEFS_NAME_KEY_BITS	128

CTASSERT(PEFS_KEY_SIZE <= HMAC_DIGEST_LENGTH_MAX);
CTASSERT(PEFS_TWEAK_SIZE == 64/8);
CTASSERT(PEFS_NAME_CSUM_SIZE <= sizeof(uint64_t));
CTASSERT(MAXNAMLEN >= PEFS_NAME_PTON_SIZE(MAXNAMLEN) + PEFS_NAME_BLOCK_SIZE);

static algop_keysetup_t pefs_aes_keysetup;
static algop_crypt_t pefs_aes_encrypt;
static algop_crypt_t pefs_aes_decrypt;

static algop_keysetup_t pefs_camellia_keysetup;
static algop_crypt_t pefs_camellia_encrypt;
static algop_crypt_t pefs_camellia_decrypt;

static uma_zone_t		pefs_ctx_zone;
static uma_zone_t		pefs_key_zone;

static const char		magic_keyinfo_v1[] = "PEFSKEY-V1";

static struct pefs_alg pefs_alg_aes = {
	.pa_id =		PEFS_ALG_AES_XTS,
#ifdef PEFS_AESNI
	.pa_init =		pefs_aesni_init,
#endif
	.pa_keysetup =		pefs_aes_keysetup,
	.pa_encrypt =		pefs_aes_encrypt,
	.pa_decrypt =		pefs_aes_decrypt,
};

static struct pefs_alg pefs_alg_camellia = {
	.pa_id =		PEFS_ALG_CAMELLIA_XTS,
	.pa_keysetup =		pefs_camellia_keysetup,
	.pa_encrypt =		pefs_camellia_encrypt,
	.pa_decrypt =		pefs_camellia_decrypt,
};

static __inline void
pefs_alg_init(struct pefs_alg *pa)
{
	if (pa->pa_init != NULL)
		pa->pa_init(pa);
}

static __inline void
pefs_alg_uninit(struct pefs_alg *pa)
{
	if (pa->pa_uninit != NULL)
		pa->pa_uninit(pa);
}

void
pefs_crypto_init(void)
{
	pefs_ctx_zone = uma_zcreate("pefs_ctx", sizeof(struct pefs_ctx),
	    NULL, NULL, NULL, (uma_fini)bzero, UMA_ALIGN_CACHE, 0);
	pefs_key_zone = uma_zcreate("pefs_key", sizeof(struct pefs_key),
	    NULL, NULL, NULL, (uma_fini)bzero, UMA_ALIGN_PTR, 0);
	pefs_alg_init(&pefs_alg_aes);
	pefs_alg_init(&pefs_alg_camellia);
}

void
pefs_crypto_uninit(void)
{
	pefs_alg_uninit(&pefs_alg_aes);
	pefs_alg_uninit(&pefs_alg_camellia);
	uma_zdestroy(pefs_ctx_zone);
	uma_zdestroy(pefs_key_zone);
}

struct pefs_ctx *
pefs_ctx_get(void)
{
	struct pefs_ctx *ctx;

	ctx = uma_zalloc(pefs_ctx_zone, M_WAITOK);
	return (ctx);
}

void
pefs_ctx_free(struct pefs_ctx *ctx)
{
	uma_zfree(pefs_ctx_zone, ctx);
}

static __inline void
pefs_ctx_cpy(struct pefs_ctx *dst, struct pefs_ctx *src)
{
	*dst = *src;
}

static __inline void
pefs_session_enter(const struct pefs_alg *alg, struct pefs_session *ses)
{
	if (alg->pa_enter != NULL)
		alg->pa_enter(ses);
}

static __inline void
pefs_session_leave(const struct pefs_alg *alg, struct pefs_session *ses)
{
	if (alg->pa_leave != NULL)
		alg->pa_leave(ses);
}

/*
 * Use HKDF-Expand() defined in RFC5869 to derive keys,
 * masterkey parameter should be cryptographically strong.
 */
static void
pefs_hkdf_expand(struct pefs_ctx *ctx, const uint8_t *masterkey, uint8_t *key,
    int idx, const uint8_t *magic, size_t magicsize)
{
	uint8_t byte_idx = idx;

	hmac_init(&ctx->o.pctx_hmac, CRYPTO_SHA2_512_HMAC,
	    masterkey, PEFS_KEY_SIZE);
	hmac_update(&ctx->o.pctx_hmac, key, PEFS_KEY_SIZE);
	hmac_update(&ctx->o.pctx_hmac, magic, magicsize);
	hmac_update(&ctx->o.pctx_hmac, &byte_idx, sizeof(byte_idx));
	hmac_final(&ctx->o.pctx_hmac, key, PEFS_KEY_SIZE);
}

static void
pefs_key_wipe(struct pefs_key *pk)
{
	bzero(pk->pk_name_ctx, sizeof(struct pefs_ctx));
	bzero(pk->pk_name_csum_ctx, sizeof(struct pefs_ctx));
	bzero(pk->pk_data_ctx, sizeof(struct pefs_ctx));
	bzero(pk->pk_tweak_ctx, sizeof(struct pefs_ctx));
}

static int
pefs_key_generate(struct pefs_key *pk, const uint8_t *masterkey,
    const uint8_t *magic, size_t magicsize)
{
	struct pefs_session ses;
	struct pefs_ctx *ctx;
	uint8_t key[PEFS_KEY_SIZE];
	int error;

	/* Properly initialize contexts as they are used to compare keys. */
	pefs_key_wipe(pk);

	ctx = pefs_ctx_get();
	pefs_session_enter(pk->pk_alg, &ses);

	bzero(key, PEFS_KEY_SIZE);
	pefs_hkdf_expand(ctx, masterkey, key, 1, magic, magicsize);
	error = pk->pk_alg->pa_keysetup(&ses, pk->pk_data_ctx, key,
	    pk->pk_keybits);
	if (error != 0) {
		pefs_session_leave(pk->pk_alg, &ses);
		goto out;
	}

	pefs_hkdf_expand(ctx, masterkey, key, 2, magic, magicsize);
	error = pk->pk_alg->pa_keysetup(&ses, pk->pk_tweak_ctx, key,
	    pk->pk_keybits);
	if (error != 0) {
		pefs_session_leave(pk->pk_alg, &ses);
		goto out;
	}

	if (pk->pk_alg != &pefs_alg_aes) {
		pefs_session_leave(pk->pk_alg, &ses);
		pefs_session_enter(&pefs_alg_aes, &ses);
	}

	pefs_hkdf_expand(ctx, masterkey, key, 3, magic, magicsize);
	error = pefs_alg_aes.pa_keysetup(&ses, pk->pk_name_ctx, key,
	    PEFS_NAME_KEY_BITS);

	pefs_session_leave(&pefs_alg_aes, &ses);

	if (error != 0)
		goto out;

	pefs_hkdf_expand(ctx, masterkey, key, 4, magic, magicsize);
	vmac_set_key(key, &pk->pk_name_csum_ctx->o.pctx_vmac);

out:
	if (error != 0)
		pefs_key_wipe(pk);
	bzero(key, PEFS_KEY_SIZE);
	pefs_ctx_free(ctx);

	return (error);
}

struct pefs_key *
pefs_key_get(int alg, int keybits, const char *key, const char *keyid)
{
	struct pefs_key *pk;

	pk = uma_zalloc(pefs_key_zone, M_WAITOK | M_ZERO);

	switch (alg) {
	case PEFS_ALG_AES_XTS:
		pk->pk_alg = &pefs_alg_aes;
		if (keybits == 128 || keybits == 192 || keybits == 256)
			pk->pk_keybits = keybits;
		break;
	case PEFS_ALG_CAMELLIA_XTS:
		pk->pk_alg = &pefs_alg_camellia;
		if (keybits == 128 || keybits == 192 || keybits == 256)
			pk->pk_keybits = keybits;
		break;
	default:
		uma_zfree(pefs_key_zone, pk);
		printf("pefs: unknown algorithm %d\n", alg);
		return (NULL);
	}
	if (pk->pk_keybits == 0) {
		uma_zfree(pefs_key_zone, pk);
		printf("pefs: invalid key size %d for algorithm %d\n",
		    keybits, alg);
		return (NULL);
	}
	pk->pk_algid = alg;

	refcount_init(&pk->pk_refcnt, 1);
	memcpy(pk->pk_keyid, keyid, PEFS_KEYID_SIZE);

	pk->pk_name_ctx = pefs_ctx_get();
	pk->pk_name_csum_ctx = pefs_ctx_get();
	pk->pk_data_ctx = pefs_ctx_get();
	pk->pk_tweak_ctx = pefs_ctx_get();

	pefs_key_generate(pk, key, magic_keyinfo_v1, sizeof(magic_keyinfo_v1));

	return (pk);
}

struct pefs_key *
pefs_key_ref(struct pefs_key *pk)
{
	refcount_acquire(&pk->pk_refcnt);
	return (pk);
}

void
pefs_key_release(struct pefs_key *pk)
{
	if (pk == NULL)
		return;
	if (refcount_release(&pk->pk_refcnt)) {
		PEFSDEBUG("pefs_key_release: free pk=%p\n", pk);
		pefs_ctx_free(pk->pk_name_ctx);
		pefs_ctx_free(pk->pk_name_csum_ctx);
		pefs_ctx_free(pk->pk_data_ctx);
		pefs_ctx_free(pk->pk_tweak_ctx);
		uma_zfree(pefs_key_zone, pk);
	}
}

struct pefs_key *
pefs_key_lookup(struct pefs_mount *pm, char *keyid)
{
	struct pefs_key *pk;

	mtx_assert(&pm->pm_keys_lock, MA_OWNED);
	TAILQ_FOREACH(pk, &pm->pm_keys, pk_entry) {
		if (memcmp(pk->pk_keyid, keyid, PEFS_KEYID_SIZE) == 0)
			return (pk);
	}

	return (NULL);
}

int
pefs_key_add(struct pefs_mount *pm, int index, struct pefs_key *pk)
{
	struct pefs_key *i, *pk_pos;
	int pos;

	mtx_lock(&pm->pm_keys_lock);
	if (index == 0 && !TAILQ_EMPTY(&pm->pm_keys)) {
		mtx_unlock(&pm->pm_keys_lock);
		return (EEXIST);
	}
	pk_pos = NULL;
	pos = 0;
	TAILQ_FOREACH(i, &pm->pm_keys, pk_entry) {
		if (memcmp(pk->pk_keyid, i->pk_keyid, PEFS_KEYID_SIZE) == 0 ||
		    memcmp(pk->pk_data_ctx, i->pk_data_ctx,
		    sizeof(struct pefs_ctx)) == 0) {
			mtx_unlock(&pm->pm_keys_lock);
			return (EEXIST);
		}
		if (index == pos + 1)
			pk_pos = i;
	}
	pk->pk_entry_lock = &pm->pm_keys_lock;
	if (TAILQ_EMPTY(&pm->pm_keys)) {
		TAILQ_INSERT_HEAD(&pm->pm_keys, pk, pk_entry);
		PEFSDEBUG("pefs_key_add: root key added: %p\n", pk);
	} else if (pk_pos == NULL) {
		TAILQ_INSERT_TAIL(&pm->pm_keys, pk, pk_entry);
		PEFSDEBUG("pefs_key_add: tail key added: %p\n", pk);
	} else {
		TAILQ_INSERT_AFTER(&pm->pm_keys, pk_pos, pk, pk_entry);
		PEFSDEBUG("pefs_key_add: key added at pos=%d: %p\n", pos, pk);
	}
	mtx_unlock(&pm->pm_keys_lock);

	return (0);
}

void
pefs_key_remove(struct pefs_mount *pm, struct pefs_key *pk)
{
	mtx_assert(&pm->pm_keys_lock, MA_OWNED);
	MPASS(pk->pk_entry_lock != NULL);
	TAILQ_REMOVE(&pm->pm_keys, pk, pk_entry);
	pk->pk_entry_lock = NULL;
	PEFSDEBUG("pefs_key_remove: pk=%p\n", pk);
	pefs_key_release(pk);
}

int
pefs_key_remove_all(struct pefs_mount *pm)
{
	int n = 0;

	mtx_lock(&pm->pm_keys_lock);
	while (!TAILQ_EMPTY(&pm->pm_keys)) {
		pefs_key_remove(pm, TAILQ_FIRST(&pm->pm_keys));
		n++;
	}
	mtx_unlock(&pm->pm_keys_lock);

	return (n);
}

void
pefs_data_encrypt(struct pefs_tkey *ptk, off_t offset, struct pefs_chunk *pc)
{
	struct pefs_session ses;
	char *buf;
	ssize_t resid, block;

	MPASS(ptk->ptk_key != NULL);
	MPASS((offset & PEFS_SECTOR_MASK) == 0);

	pefs_session_enter(ptk->ptk_key->pk_alg, &ses);
	buf = pc->pc_base;
	resid = pc->pc_size;
	while (resid > 0) {
		block = qmin(resid, PEFS_SECTOR_SIZE);
		pefs_xts_block_encrypt(ptk->ptk_key->pk_alg, &ses,
		    ptk->ptk_key->pk_tweak_ctx, ptk->ptk_key->pk_data_ctx,
		    offset, ptk->ptk_tweak, block,
		    buf, buf);
		offset += block;
		buf += block;
		resid -= block;
	}
	pefs_session_leave(ptk->ptk_key->pk_alg, &ses);
}

void
pefs_data_decrypt(struct pefs_tkey *ptk, off_t offset, struct pefs_chunk *pc)
{
	struct pefs_session ses;
	ssize_t resid;
	long *p;
	char *buf, *end;

	MPASS(ptk->ptk_key != NULL);
	MPASS((offset & PEFS_SECTOR_MASK) == 0);

	pefs_session_enter(ptk->ptk_key->pk_alg, &ses);
	buf = (char *)pc->pc_base;
	end = buf + pc->pc_size;
	while (buf < end) {
		if ((end - buf) >= PEFS_SECTOR_SIZE) {
			p = (long *)buf;
			resid = PEFS_SECTOR_SIZE / sizeof(long);
			for (; resid > 0; resid--)
				if (*(p++) != 0)
					break;
			if (resid == 0) {
				bzero(buf, PEFS_SECTOR_SIZE);
				offset += PEFS_SECTOR_SIZE;
				buf += PEFS_SECTOR_SIZE;
				continue;
			}
			resid = PEFS_SECTOR_SIZE;
		} else
			resid = end - buf;
		pefs_xts_block_decrypt(ptk->ptk_key->pk_alg, &ses,
		    ptk->ptk_key->pk_tweak_ctx, ptk->ptk_key->pk_data_ctx,
		    offset, ptk->ptk_tweak, resid, buf, buf);
		buf += resid;
		offset += resid;
	}
	pefs_session_leave(ptk->ptk_key->pk_alg, &ses);
}

/*
 * File name layout: [checksum] [tweak] [name]
 * File name is padded with zeros to 16 byte boundary
 */
static __inline size_t
pefs_name_padsize(size_t size)
{
	size_t psize;

	psize = size - PEFS_NAME_CSUM_SIZE;
	psize = PEFS_NAME_CSUM_SIZE +
	    roundup2(psize, PEFS_NAME_BLOCK_SIZE);

	return (psize);
}

static __inline size_t
pefs_name_pad(char *name, size_t size, size_t maxsize)
{
	size_t psize;

	MPASS(size > PEFS_NAME_CSUM_SIZE && size <= MAXNAMLEN);
	psize = pefs_name_padsize(size);
	MPASS(psize <= MAXNAMLEN);
	if (psize != size) {
		if (maxsize < psize)
			panic("pefs_name_pad: buffer is too small");
		bzero(name + size, psize - size);
	}

	return (psize);
}

static void
pefs_name_checksum(struct pefs_ctx *ctx, struct pefs_key *pk, char *csum,
    char *name, size_t size)
{
	uint64_t buf[howmany(MAXNAMLEN + 1, sizeof(uint64_t))];
	uint64_t nonce[2];
	uint64_t csum_int;
	char *data;

	MPASS(size >= PEFS_NAME_CSUM_SIZE + (PEFS_TWEAK_SIZE * 2) &&
	    size <= MAXNAMLEN &&
	    (size - PEFS_NAME_CSUM_SIZE) % PEFS_NAME_BLOCK_SIZE == 0);

	/*
	 * First block of encrypted name contains 64bit random tweak.
	 * Considering AES strong cipher reuse it as a nonce. It's rather far
	 * from what VMAC specification suggests, but storing additional random
	 * data in file name is too expensive and decrypting before running vmac
	 * degrades performance dramatically.
	 * Use separate key for name checksum.
	 */
	memcpy(nonce, name + PEFS_NAME_CSUM_SIZE, PEFS_TWEAK_SIZE * 2);
	((char *)nonce)[15] &= 0xfe; /* VMAC requirement */

	size -= PEFS_NAME_CSUM_SIZE;
	data = name + PEFS_NAME_CSUM_SIZE;
	if (((uintptr_t)data & (__alignof__(uint64_t) - 1)) != 0) {
		memcpy(buf, data, size);
		data = (char *)buf;
	}

	pefs_ctx_cpy(ctx, pk->pk_name_csum_ctx);
	csum_int = vmac(data, size, (char *)nonce, NULL, &ctx->o.pctx_vmac);
	memcpy(csum, &csum_int, PEFS_NAME_CSUM_SIZE);
}

static __inline int
pefs_name_checksum_eq(const uint8_t *mac1, const uint8_t *mac2)
{
	int i, result;

	result = 0;
	for (i = 0; i < PEFS_NAME_CSUM_SIZE; i++)
		result |= *(mac1++) ^ *(mac2++);

	return (result == 0);
}

static void
pefs_name_enccbc(struct pefs_key *pk, u_char *data, ssize_t size)
{
	struct pefs_session ses;
	u_char *prev;
	int i;

	size -= PEFS_NAME_CSUM_SIZE;
	data += PEFS_NAME_CSUM_SIZE;
	MPASS(size > 0 && size % PEFS_NAME_BLOCK_SIZE == 0);

	pefs_session_enter(&pefs_alg_aes, &ses);
	/* Start with zero iv */
	while (1) {
		pefs_alg_aes.pa_encrypt(&ses, pk->pk_name_ctx, data, data);
		prev = data;
		data += PEFS_NAME_BLOCK_SIZE;
		size -= PEFS_NAME_BLOCK_SIZE;
		if (size == 0)
			break;
		for (i = 0; i < PEFS_NAME_BLOCK_SIZE; i++)
			data[i] ^= prev[i];
	}
	pefs_session_leave(&pefs_alg_aes, &ses);
}

static void
pefs_name_deccbc(struct pefs_key *pk, u_char *data, ssize_t size)
{
	struct pefs_session ses;
	u_char tmp[PEFS_NAME_BLOCK_SIZE], iv[PEFS_NAME_BLOCK_SIZE];
	int i;

	size -= PEFS_NAME_CSUM_SIZE;
	data += PEFS_NAME_CSUM_SIZE;
	MPASS(size > 0 && size % PEFS_NAME_BLOCK_SIZE == 0);

	pefs_session_enter(&pefs_alg_aes, &ses);
	bzero(iv, PEFS_NAME_BLOCK_SIZE);
	while (size > 0) {
		memcpy(tmp, data, PEFS_NAME_BLOCK_SIZE);
		pefs_alg_aes.pa_decrypt(&ses, pk->pk_name_ctx, data, data);
		for (i = 0; i < PEFS_NAME_BLOCK_SIZE; i++)
			data[i] ^= iv[i];
		memcpy(iv, tmp, PEFS_NAME_BLOCK_SIZE);
		data += PEFS_NAME_BLOCK_SIZE;
		size -= PEFS_NAME_BLOCK_SIZE;
	}
	pefs_session_leave(&pefs_alg_aes, &ses);
}

int
pefs_name_encrypt(struct pefs_ctx *ctx, struct pefs_tkey *ptk,
    const char *plain, size_t plain_len, char *enc, size_t enc_size)
{
	char buf[MAXNAMLEN + 1];
	size_t size;
	int free_ctx = 0;
	int r;

	KASSERT(ptk != NULL && ptk->ptk_key != NULL,
	    ("pefs_name_encrypt: key is null"));

	size = PEFS_NAME_CSUM_SIZE + PEFS_TWEAK_SIZE + plain_len;
	/* Resulting name size, count '.' prepended to name */
	r = PEFS_NAME_NTOP_SIZE(pefs_name_padsize(size)) + 1;
	if (r > MAXNAMLEN) {
		return (-ENAMETOOLONG);
	}
	if (enc_size < r) {
		printf("pefs: name encryption buffer is too small: "
		    "length %zd, required %d\n", enc_size, r);
		return (-EOVERFLOW);
	}

	if (ctx == NULL) {
		ctx = pefs_ctx_get();
		free_ctx = 1;
	}

	memcpy(buf + PEFS_NAME_CSUM_SIZE, ptk->ptk_tweak, PEFS_TWEAK_SIZE);
	memcpy(buf + PEFS_NAME_CSUM_SIZE + PEFS_TWEAK_SIZE, plain, plain_len);

	size = pefs_name_pad(buf, size, sizeof(buf));
	pefs_name_enccbc(ptk->ptk_key, buf, size);
	pefs_name_checksum(ctx, ptk->ptk_key, buf, buf, size);

	if (free_ctx != 0)
		pefs_ctx_free(ctx);

	enc[0] = '.';
	r = pefs_name_ntop(buf, size, enc + 1, enc_size - 1);
	if (r <= 0)
		return (r);
	r++;

	return (r);
}

int
pefs_name_decrypt(struct pefs_ctx *ctx, struct pefs_key *pk,
    struct pefs_tkey *ptk, const char *enc, size_t enc_len,
    char *plain, size_t plain_size)
{
	struct pefs_key *ki;
	char csum[PEFS_NAME_CSUM_SIZE];
	int free_ctx = 0;
	int r, ki_rev;

	KASSERT(enc != plain, ("pefs_name_decrypt: "
	    "ciphertext and plaintext buffers should differ"));
	MPASS(enc_len > 0 && enc_len <= MAXNAMLEN);

	if (enc[0] != '.' || enc_len <= 1)
		return (-EINVAL);
	enc++;
	enc_len--;

	r = PEFS_NAME_PTON_SIZE(enc_len);
	if (r <= PEFS_TWEAK_SIZE + PEFS_NAME_CSUM_SIZE ||
	    (r - PEFS_NAME_CSUM_SIZE) % PEFS_NAME_BLOCK_SIZE != 0)
		return (-EINVAL);
	if (plain_size < r) {
		printf("pefs: name decryption buffer is too small: "
		    "length %zd, required %d\n", plain_size, r);
		return (-EOVERFLOW);
	}

	r = pefs_name_pton(enc, enc_len, plain, plain_size);
	if (r <= 0) {
		PEFSDEBUG("pefs_name_decrypt: error: r=%d\n", r);
		return (-EINVAL);
	}

	if (ctx == NULL) {
		ctx = pefs_ctx_get();
		free_ctx = 1;
	}

	ki = pk;
	ki_rev = 0;
	do {
		pefs_name_checksum(ctx, ki, csum, plain, r);
		if (pefs_name_checksum_eq(csum, plain))
			break;

		if (ki_rev == 0) {
			ki = TAILQ_NEXT(ki, pk_entry);
			if (ki == NULL) {
				ki_rev = 1;
				ki = pk;
			}
		}
		if (ki_rev != 0)
			ki = TAILQ_PREV(ki, pefs_key_head, pk_entry);
	} while (ki != NULL);

	if (free_ctx != 0)
		pefs_ctx_free(ctx);

	if (ki == NULL)
		return (-EINVAL);

	pefs_name_deccbc(ki, plain, r);

	if (ptk != NULL) {
		ptk->ptk_key = ki;
		memcpy(ptk->ptk_tweak, plain + PEFS_NAME_CSUM_SIZE,
		    PEFS_TWEAK_SIZE);
	}

	r -= PEFS_TWEAK_SIZE + PEFS_NAME_CSUM_SIZE;
	memcpy(plain, plain + PEFS_NAME_CSUM_SIZE + PEFS_TWEAK_SIZE, r);
	plain[r] = '\0';
	/* Remove encryption zero padding */
	while (r > 0 && plain[r - 1] == '\0')
		r--;
	MPASS(r > 0 && strlen(plain) == r);

	return (r);
}

static int
pefs_aes_keysetup(const struct pefs_session *sess __unused,
    struct pefs_ctx *ctx, const uint8_t *key, uint32_t keybits)
{
	rijndael_set_key(&ctx->o.pctx_aes, key, keybits);
	return (0);
}

static void
pefs_aes_encrypt(const struct pefs_session *sess __unused,
	    const struct pefs_ctx *ctx, const uint8_t *in, uint8_t *out)
{
	rijndael_encrypt(&ctx->o.pctx_aes, in, out);
}

static void
pefs_aes_decrypt(const struct pefs_session *sess __unused,
	    const struct pefs_ctx *ctx, const uint8_t *in, uint8_t *out)
{
	rijndael_decrypt(&ctx->o.pctx_aes, in, out);
}

static int
pefs_camellia_keysetup(const struct pefs_session *sess __unused,
    struct pefs_ctx *ctx, const uint8_t *key, uint32_t keybits)
{
	camellia_set_key(&ctx->o.pctx_camellia, key, keybits);
	return (0);
}

static void
pefs_camellia_encrypt(const struct pefs_session *sess __unused,
	    const struct pefs_ctx *ctx, const uint8_t *in, uint8_t *out)
{
	camellia_encrypt(&ctx->o.pctx_camellia, in, out);
}

static void
pefs_camellia_decrypt(const struct pefs_session *sess __unused,
	    const struct pefs_ctx *ctx, const uint8_t *in, uint8_t *out)
{
	camellia_decrypt(&ctx->o.pctx_camellia, in, out);
}
