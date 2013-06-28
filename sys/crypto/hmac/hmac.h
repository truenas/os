/*-
 * Copyright (c) 2005-2011 Pawel Jakub Dawidek <pjd@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _SYS_CRYPTO_HMAC_H
#define _SYS_CRYPTO_HMAC_H

#include <crypto/sha2/sha2.h>

#define HMAC_BLOCK_LENGTH_MAX		SHA512_BLOCK_LENGTH
#define HMAC_DIGEST_LENGTH_MAX		SHA512_DIGEST_LENGTH

struct hmac_hash;

struct hmac_ctx {
	const struct hmac_hash	*hash;
	union hmac_hash_ctx {
		SHA256_CTX	sha256_ctx;
		SHA384_CTX	sha384_ctx;
		SHA512_CTX	sha512_ctx;
	}		hash_ctx;
	u_char		k_opad[HMAC_BLOCK_LENGTH_MAX];
};

void hmac_init(struct hmac_ctx *ctx, int algo, const uint8_t *hkey,
    size_t hkeylen);
void hmac_update(struct hmac_ctx *ctx, const uint8_t *data,
    size_t datasize);
void hmac_final(struct hmac_ctx *ctx, uint8_t *md, size_t mdsize);
void hmac(int algo, const uint8_t *hkey, size_t hkeysize,
    const uint8_t *data, size_t datasize, uint8_t *md, size_t mdsize);

#endif /* _SYS_CRYPTO_HMAC_H */
