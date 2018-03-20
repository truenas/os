/*
 * Copyright 2018 iXSystems Inc.
 */

/*
 * FreeBSD, via opencrypto, has its own implementations of
 * the SHA functions.  Here, we simply provide wrappers for
 * them.
 */

#include <sys/types.h>
#include <sys/systm.h>

/* With FreeBSD, this gets us the opencrypto files included as well */
#include <sys/sha2.h>

void
SHA2Init(uint64_t mech, SHA2_CTX *ctx)
{
	switch (mech) {
	case SHA256: case SHA256_HMAC: case SHA256_HMAC_GEN:
		SHA256_Init(&ctx->sha.sha256); break;
	case SHA384: case SHA384_HMAC: case SHA384_HMAC_GEN:
		SHA384_Init(&ctx->sha.sha384); break;
	case SHA512: case SHA512_HMAC: case SHA512_HMAC_GEN:
		SHA512_Init(&ctx->sha.sha512); break;
	default:	panic("Unknown SHA 2 type %llu", (unsigned long long)mech);
		break;
	}
	ctx->algotype = mech;
}

void
SHA2Update(SHA2_CTX *ctx, const void *buf, size_t len)
{
	void *sha = &ctx->sha;
	switch (ctx->algotype) {
	case SHA256: case SHA256_HMAC: case SHA256_HMAC_GEN:
		SHA256_Update(sha, buf, len); break;
	case SHA384: case SHA384_HMAC: case SHA384_HMAC_GEN:
		SHA384_Update(sha, buf, len); break;
	case SHA512: case SHA512_HMAC: case SHA512_HMAC_GEN:
		SHA512_Update(sha, buf, len); break;
	default:	panic("Unknown SHA2 type %u", ctx->algotype);
	}
	return;
}

void
SHA2Final(void *buf, SHA2_CTX *ctx)
{
	void *sha = &ctx->sha;
	switch (ctx->algotype) {
	case SHA256: case SHA256_HMAC: case SHA256_HMAC_GEN:
		SHA256_Final(buf, sha); break;
	case SHA384: case SHA384_HMAC: case SHA384_HMAC_GEN:
		SHA384_Final(buf, sha); break;
	case SHA512: case SHA512_HMAC: case SHA512_HMAC_GEN:
		SHA512_Final(buf, sha); break;
	default:	panic("Unknown SHA2 type %u", ctx->algotype);
		break;
	}
	return;
}

		
