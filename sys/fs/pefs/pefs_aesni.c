/*-
 * Copyright (c) 2010 Konstantin Belousov <kib@FreeBSD.org>
 * Copyright (c) 2010 Pawel Jakub Dawidek <pjd@FreeBSD.org>
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/libkern.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/pcpu.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/smp.h>
#include <sys/systm.h>

#include <fs/pefs/pefs_crypto.h>

#define	AESNI_ENABLE_ENV	"vfs.pefs.aesni_enable"

DPCPU_DEFINE(struct fpu_kern_ctx *, pefs_aesni_fpu);

#if __FreeBSD_version < 900503
static struct fpu_kern_ctx *
fpu_kern_alloc_ctx(int flags __unused)
{
	struct fpu_kern_ctx *ctx;

	ctx = malloc(sizeof(*ctx), M_TEMP, M_WAITOK | M_ZERO);
	return (ctx);
}

static void
fpu_kern_free_ctx(struct fpu_kern_ctx *ctx)
{
	free(ctx, M_TEMP);
}
#endif

static int
pefs_aesni_keysetup(const struct pefs_session *xses,
    struct pefs_ctx *xctx, const uint8_t *key, uint32_t keybits)
{
	const struct pefs_aesni_ses *ses = &xses->o.ps_aesni;
	struct pefs_aesni_ctx *ctx = &xctx->o.pctx_aesni;
	struct fpu_kern_ctx *tmpctx = NULL;
	int error;

	switch (keybits) {
	case 128:
		ctx->rounds = AES128_ROUNDS;
		break;
	case 192:
		ctx->rounds = AES192_ROUNDS;
		break;
	case 256:
		ctx->rounds = AES256_ROUNDS;
		break;
	default:
		printf("pefs: AESNI: invalid key length: %d", keybits);
		return (EINVAL);
	}

	if (ses->fpu_saved < 0) {
		tmpctx = fpu_kern_alloc_ctx(FPU_KERN_NORMAL);
		if (tmpctx == NULL)
			return (ENOMEM);
		error = fpu_kern_enter(curthread, tmpctx, FPU_KERN_NORMAL);
		if (error != 0) {
			fpu_kern_free_ctx(tmpctx);
			return (error);
		}
	}

	aesni_set_enckey(key, ctx->enc_schedule, ctx->rounds);
	aesni_set_deckey(ctx->enc_schedule, ctx->dec_schedule, ctx->rounds);
	rijndael_set_key(&ctx->sw, key, keybits);

	if (tmpctx != NULL) {
		fpu_kern_leave(curthread, tmpctx);
		fpu_kern_free_ctx(tmpctx);
	}

	return (0);
}

static void
pefs_aesni_encrypt(const struct pefs_session *xses,
    const struct pefs_ctx *xctx, const uint8_t *in, uint8_t *out)
{
	const struct pefs_aesni_ses *ses = &xses->o.ps_aesni;
	const struct pefs_aesni_ctx *ctx = &xctx->o.pctx_aesni;

	if (ses->fpu_saved >= 0)
		aesni_enc(ctx->rounds - 1, ctx->enc_schedule, in, out, NULL);
	else
		rijndael_encrypt(&ctx->sw, in, out);
}

static void
pefs_aesni_decrypt(const struct pefs_session *xses,
    const struct pefs_ctx *xctx, const uint8_t *in, uint8_t *out)
{
	const struct pefs_aesni_ses *ses = &xses->o.ps_aesni;
	const struct pefs_aesni_ctx *ctx = &xctx->o.pctx_aesni;

	if (ses->fpu_saved >= 0)
		aesni_dec(ctx->rounds - 1, ctx->dec_schedule, in, out, NULL);
	else
		rijndael_decrypt(&ctx->sw, in, out);
}

static void
pefs_aesni_enter(struct pefs_session *xses)
{
	struct pefs_aesni_ses *ses = &xses->o.ps_aesni;
	int error;

	if (is_fpu_kern_thread(0)) {
		ses->fpu_saved = 0;
		return;
	}

	sched_pin();
	ses->fpu_ctx = DPCPU_GET(pefs_aesni_fpu);
	if (ses->fpu_ctx != NULL) {
		ses->td = curthread;
		ses->fpu_cpuid = curcpu;
		DPCPU_ID_SET(ses->fpu_cpuid, pefs_aesni_fpu, NULL);
		sched_unpin();
		error = fpu_kern_enter(ses->td, ses->fpu_ctx, FPU_KERN_NORMAL);
		if (error == 0) {
			ses->fpu_saved = 1;
			return;
		}
		DPCPU_ID_SET(ses->fpu_cpuid, pefs_aesni_fpu, ses->fpu_ctx);
	} else
		sched_unpin();
	ses->fpu_saved = -1;
}

static void
pefs_aesni_leave(struct pefs_session *xses)
{
	struct pefs_aesni_ses *ses = &xses->o.ps_aesni;

	if (ses->fpu_saved <= 0)
		return;

	fpu_kern_leave(ses->td, ses->fpu_ctx);
	DPCPU_ID_SET(ses->fpu_cpuid, pefs_aesni_fpu, ses->fpu_ctx);
}

static void
pefs_aesni_uninit(struct pefs_alg *pa)
{
	struct fpu_kern_ctx *fpu_ctx;
	u_int cpuid;

	CPU_FOREACH(cpuid) {
		fpu_ctx = DPCPU_ID_GET(cpuid, pefs_aesni_fpu);
		if (fpu_ctx != NULL) {
			fpu_kern_free_ctx(fpu_ctx);
			DPCPU_ID_SET(cpuid, pefs_aesni_fpu, NULL);
		}
	}
}

void
pefs_aesni_init(struct pefs_alg *pa)
{
	struct fpu_kern_ctx *fpu_ctx;
	u_long enable = 1;
	u_int cpuid;

	TUNABLE_ULONG_FETCH(AESNI_ENABLE_ENV, &enable);

	if (enable != 0 && (cpu_feature2 & CPUID2_AESNI) != 0) {
		printf("pefs: AESNI hardware acceleration enabled\n");
		pa->pa_uninit = pefs_aesni_uninit;
		pa->pa_enter = pefs_aesni_enter;
		pa->pa_leave = pefs_aesni_leave;
		pa->pa_keysetup = pefs_aesni_keysetup;
		pa->pa_encrypt = pefs_aesni_encrypt;
		pa->pa_decrypt = pefs_aesni_decrypt;
		CPU_FOREACH(cpuid) {
			fpu_ctx = fpu_kern_alloc_ctx(FPU_KERN_NORMAL);
			DPCPU_ID_SET(cpuid, pefs_aesni_fpu, fpu_ctx);
		}
	} else
#ifndef PEFS_DEBUG
	if (bootverbose)
#endif
		printf("pefs: AESNI hardware acceleration disabled\n");
}
