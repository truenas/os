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

#include <inttypes.h>

#define	PEFS_FSTYPE			"pefs"
#define	PEFS_KLD			PEFS_FSTYPE

#define	PEFS_ALG_DEFAULT		PEFS_ALG_AES_XTS
#define	PEFS_ALG_DEFAULT_KEYBITS	128

#define	PEFS_KDF_ITERATIONS		50000

#define	PEFS_FILE_KEYCHAIN		".pefs.db"
#define	PEFS_FILE_KEYCONF		".pefs.conf"

#define	PEFS_KEYCONF_ALG_IND		0
#define	PEFS_KEYCONF_ITERATIONS_IND	1

#define	PEFS_RANDOMCHAIN_MIN		16
#define	PEFS_RANDOMCHAIN_MAX		64

#define	PEFS_KEYENC_MAC_SIZE		(PEFS_KEY_SIZE / 2)

#define PEFS_KEYPARAM_FILES_MAX		16

#define	PEFS_ERR_GENERIC		1
#define	PEFS_ERR_USAGE			2
#define	PEFS_ERR_IO			3
#define	PEFS_ERR_SYS			4
#define	PEFS_ERR_NOENT			5
#define	PEFS_ERR_EXIST			6
#define	PEFS_ERR_INVALID		7

#define	PEFS_FS_IGNORE_TYPE		0x0001

struct pefs_xkeyenc {
	struct {
		struct pefs_xkey	ke_next;
		uint32_t		ke_alg;
		uint32_t		ke_keybits;
	} a;
	u_char		ke_mac[PEFS_KEYENC_MAC_SIZE];
};

struct pefs_keyparam {
	int		kp_alg;
	int		kp_keybits;
	int		kp_nopassphrase;
	int		kp_iterations;
	int		kp_keyfile_count;
	int		kp_passfile_count;
	const char	*kp_keyfile[PEFS_KEYPARAM_FILES_MAX];
	const char	*kp_passfile[PEFS_KEYPARAM_FILES_MAX];
};

void	pefs_warn(const char *, ...) __printf0like(1, 2);

int	pefs_getfsroot(const char *path, int flags, char *fsroot, size_t size);
int	pefs_readfiles(const char **files, size_t count, void *ctx,
	    int (*handler)(void *, uint8_t *, size_t, const char *));

int	pefs_key_generate(struct pefs_xkey *xk, const char *passphrase,
	    struct pefs_keyparam *kp);
int	pefs_key_encrypt(struct pefs_xkeyenc *xe,
	    const struct pefs_xkey *xk_parent);
int	pefs_key_decrypt(struct pefs_xkeyenc *xe,
	    const struct pefs_xkey *xk_parent);
uintmax_t	pefs_keyid_as_int(char *keyid);

const char *	pefs_alg_name(struct pefs_xkey *xk);
void	pefs_alg_list(FILE *stream);

int	pefs_keyparam_init(struct pefs_keyparam *kp, const char *fsroot);
int	pefs_keyparam_setalg(struct pefs_keyparam *kp, const char *algname);
int	pefs_keyparam_setiterations(struct pefs_keyparam *kp, const char *arg);
int	pefs_keyparam_setfile(struct pefs_keyparam *kp, const char **files,
    const char *arg);

static inline void
pefs_keyparam_create(struct pefs_keyparam *kp)
{
	kp->kp_nopassphrase = 0;
	kp->kp_iterations = -1;
	kp->kp_keyfile_count = 0;
	kp->kp_passfile_count = 0;
	kp->kp_alg = 0;
	kp->kp_keybits = 0;
}
