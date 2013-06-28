/*
 * Copyright (c) 2007, 2008 University of Tsukuba
 * Copyright (c) 2010 Gleb Kurtsou <gleb@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of the University of Tsukuba nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/endian.h>

#include <fs/pefs/pefs_crypto.h>

#ifdef _KERNEL
#include <sys/libkern.h>
#else
#include <string.h>
#endif

#define	XTS_BLK_BYTES		16
#define	XTS_BLK_MASK		(XTS_BLK_BYTES - 1)

static __inline void
xor128(void *dst, const void *src1, const void *src2)
{
	const uint64_t *s1 = (const uint64_t *)src1;
	const uint64_t *s2 = (const uint64_t *)src2;
	uint64_t *d = (uint64_t *)dst;

	d[0] = s1[0] ^ s2[0];
	d[1] = s1[1] ^ s2[1];
}

static __inline int
shl128(uint64_t *d, const uint64_t *s)
{
	int c0, c1;

	c0 = s[0] & (1ULL << 63) ? 1 : 0;
	c1 = s[1] & (1ULL << 63) ? 1 : 0;
	d[0] = s[0] << 1;
	d[1] = s[1] << 1 | c0;

	return (c1);
}

static __inline void
gf_mul128(uint64_t *dst, const uint64_t *src)
{
	static const uint8_t gf_128_fdbk = 0x87;
	int carry;

	carry = shl128(dst, src);
	if (carry != 0)
		((uint8_t *)dst)[0] ^= gf_128_fdbk;
}

static __inline void
xts_fullblock(algop_crypt_t *data_crypt, const struct pefs_session *ses,
    const struct pefs_ctx *data_ctx,
    uint64_t *tweak, const uint8_t *src, uint8_t *dst)
{
	xor128(dst, src, tweak);
	data_crypt(ses, data_ctx, dst, dst);
	xor128(dst, dst, tweak);
	gf_mul128(tweak, tweak);
}

static __inline void
xts_lastblock(algop_crypt_t *data_crypt, const struct pefs_session *ses,
    const struct pefs_ctx *data_ctx,
    uint64_t *tweak, const uint8_t *src, uint8_t *dst, int len)
{
	uint8_t b[XTS_BLK_BYTES];

	dst -= XTS_BLK_BYTES;			/* m - 1 */
	memcpy(b, dst, XTS_BLK_BYTES);
	memcpy(b, src, len);
	memcpy(dst + XTS_BLK_BYTES, dst, len);

	xor128(dst, b, tweak);
	data_crypt(ses, data_ctx, dst, dst);
	xor128(dst, dst, tweak);
}

static __inline void
xts_smallblock(const struct pefs_alg *alg, const struct pefs_session *ses,
    const struct pefs_ctx *data_ctx,
    uint64_t *tweak, const uint8_t *src, uint8_t *dst, int len)
{
	uint8_t buf[XTS_BLK_BYTES], *p;

	/*
	 * Encryption/decryption of sectors smaller then 128 bits is not defined
	 * by IEEE P1619 standard.
	 * To work around it encrypt such sector in CTR mode.
	 * CTR tweak (counter) value is XTS-tweak xor'ed with block length, i.e.
	 * entire small block has to be reencrypted after length change.
	 */
	memset(buf, len, XTS_BLK_BYTES);
	xor128(buf, buf, tweak);
	alg->pa_encrypt(ses, data_ctx, buf, buf);
	for (p = buf; len > 0; len--)
		*(dst++) = *(src++) ^ *(p++);
}

static __inline void
xts_start(const struct pefs_alg *alg, const struct pefs_session *ses,
    const struct pefs_ctx *tweak_ctx,
    uint64_t *tweak, uint64_t sector, const uint8_t *xtweak)
{
	tweak[0] = htole64(sector);
	tweak[1] = *((const uint64_t *)xtweak);

	/* encrypt the tweak */
	alg->pa_encrypt(ses, tweak_ctx, (uint8_t *)tweak, (uint8_t *)tweak);
}

void
pefs_xts_block_encrypt(const struct pefs_alg *alg,
    const struct pefs_session *ses,
    const struct pefs_ctx *tweak_ctx, const struct pefs_ctx *data_ctx,
    uint64_t sector, const uint8_t *xtweak, int len,
    const uint8_t *src, uint8_t *dst)
{
	uint64_t tweak[XTS_BLK_BYTES / 8];

	xts_start(alg, ses, tweak_ctx, tweak, sector, xtweak);

	if (len < XTS_BLK_BYTES) {
		xts_smallblock(alg, ses, data_ctx, tweak, src, dst, len);
		return;
	}

	while (len >= XTS_BLK_BYTES) {
		xts_fullblock(alg->pa_encrypt, ses, data_ctx, tweak, src, dst);
		dst += XTS_BLK_BYTES;
		src += XTS_BLK_BYTES;
		len -= XTS_BLK_BYTES;
	}

	if (len != 0)
		xts_lastblock(alg->pa_encrypt, ses, data_ctx, tweak,
		    src, dst, len);
}

void
pefs_xts_block_decrypt(const struct pefs_alg *alg,
    const struct pefs_session *ses,
    const struct pefs_ctx *tweak_ctx, const struct pefs_ctx *data_ctx,
    uint64_t sector, const uint8_t *xtweak, int len,
    const uint8_t *src, uint8_t *dst)
{
	uint64_t tweak[XTS_BLK_BYTES / 8];
	uint64_t prevtweak[XTS_BLK_BYTES / 8];

	xts_start(alg, ses, tweak_ctx, tweak, sector, xtweak);

	if (len < XTS_BLK_BYTES) {
		xts_smallblock(alg, ses, data_ctx, tweak, src, dst, len);
		return;
	}

	if ((len & XTS_BLK_MASK) != 0)
		len -= XTS_BLK_BYTES;

	while (len >= XTS_BLK_BYTES) {
		xts_fullblock(alg->pa_decrypt, ses, data_ctx, tweak, src, dst);
		dst += XTS_BLK_BYTES;
		src += XTS_BLK_BYTES;
		len -= XTS_BLK_BYTES;
	}

	if (len != 0) {
		len += XTS_BLK_BYTES;
		prevtweak[0] = tweak[0];
		prevtweak[1] = tweak[1];
		gf_mul128(tweak, tweak);
		xts_fullblock(alg->pa_decrypt, ses, data_ctx, tweak, src, dst);
		dst += XTS_BLK_BYTES;
		src += XTS_BLK_BYTES;
		len -= XTS_BLK_BYTES;
		xts_lastblock(alg->pa_decrypt, ses, data_ctx, prevtweak,
		    src, dst, len);
	}
}
