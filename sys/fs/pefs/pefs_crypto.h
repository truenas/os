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
 *
 * $FreeBSD$
 */

#include <crypto/camellia/camellia.h>
#include <crypto/hmac/hmac.h>
#include <crypto/rijndael/rijndael.h>

#include <fs/pefs/pefs_aesni.h>
#include <fs/pefs/vmac.h>

struct pefs_alg;
struct pefs_ctx;
struct pefs_session;

typedef void	algop_init_t(struct pefs_alg *alg);
typedef void	algop_uninit_t(struct pefs_alg *alg);
typedef void	algop_session_t(struct pefs_session *ses);
typedef int	algop_keysetup_t(const struct pefs_session *sess,
	    struct pefs_ctx *ctx, const uint8_t *key, uint32_t keybits);
typedef void	algop_crypt_t(const struct pefs_session *sess,
	    const struct pefs_ctx *ctx, const uint8_t *in, uint8_t *out);

struct pefs_alg {
	algop_session_t		*pa_enter;
	algop_session_t		*pa_leave;
	algop_crypt_t		*pa_encrypt;
	algop_crypt_t		*pa_decrypt;
	algop_keysetup_t	*pa_keysetup;
	algop_init_t		*pa_init;
	algop_uninit_t		*pa_uninit;
	int			pa_id;
};

struct pefs_ctx {
	union {
		camellia_ctx	pctx_camellia;
		rijndael_ctx	pctx_aes;
		struct hmac_ctx	pctx_hmac;
		vmac_ctx_t	pctx_vmac;
#ifdef PEFS_AESNI
		struct pefs_aesni_ctx pctx_aesni;
#endif
	} o;
} __aligned(CACHE_LINE_SIZE);

struct pefs_session {
	union {
		int dummy;
#ifdef PEFS_AESNI
		struct pefs_aesni_ses ps_aesni;
#endif
	} o;
};

algop_init_t	pefs_aesni_init;

void	pefs_xts_block_encrypt(const struct pefs_alg *alg,
	    const struct pefs_session *ses,
	    const struct pefs_ctx *tweak_ctx, const struct pefs_ctx *data_ctx,
	    uint64_t sector, const uint8_t *xtweak, int len,
	    const uint8_t *src, uint8_t *dst);

void	pefs_xts_block_decrypt(const struct pefs_alg *alg,
	    const struct pefs_session *ses,
	    const struct pefs_ctx *tweak_ctx, const struct pefs_ctx *data_ctx,
	    uint64_t sector, const uint8_t *xtweak, int len,
	    const uint8_t *src, uint8_t *dst);
