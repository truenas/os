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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/ioccom.h>
#include <sys/module.h>
#include <sys/mount.h>

#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <libgen.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <libutil.h>
#include <readpassphrase.h>

#include <fs/pefs/pefs.h>

#include "pefs_ctl.h"
#include "pefs_keychain.h"

#define	PATH_MOUNT		"/sbin/mount"
#define	PATH_UMOUNT		"/sbin/umount"
#define	PATH_DEVRANDOM		"/dev/random"

#define	PEFS_KEY_PROMPT_DEFAULT			"passphrase"

static void	pefs_usage(void);
static void	pefs_usage_alg(void);
static int	pefs_mount(int argc, char *argv[]);
static int	pefs_unmount(int argc, char *argv[]);
static int	pefs_addkey(int argc, char *argv[]);
static int	pefs_setkey(int argc, char *argv[]);
static int	pefs_delkey(int argc, char *argv[]);
static int	pefs_flushkeys(int argc, char *argv[]);
static int	pefs_addchain(int argc, char *argv[]);
static int	pefs_delchain(int argc, char *argv[]);
static int	pefs_randomchain(int argc, char *argv[]);
static int	pefs_showkeys(int argc, char *argv[]);
static int	pefs_getkey(int argc, char *argv[]);
static int	pefs_showchains(int argc, char *argv[]);
static int	pefs_showalgs(int argc, char *argv[]);

typedef int (*command_func_t)(int argc, char **argv);
typedef int (*keyop_func_t)(struct pefs_keychain_head *kch, int fd,
    int verbose);

struct command {
	const char	*name;
	command_func_t	func;
};

static struct command cmds[] = {
	{ "mount",	pefs_mount },
	{ "unmount",	pefs_unmount },
	{ "umount",	pefs_unmount },
	{ "addkey",	pefs_addkey },
	{ "setkey",	pefs_setkey },
	{ "delkey",	pefs_delkey },
	{ "flushkeys",	pefs_flushkeys },
	{ "showkeys",	pefs_showkeys },
	{ "getkey",	pefs_getkey },
	{ "status",	pefs_showkeys },
	{ "randomchain", pefs_randomchain },
	{ "addchain",	pefs_addchain },
	{ "delchain",	pefs_delchain },
	{ "showchains",	pefs_showchains },
	{ "showalgs",	pefs_showalgs },
	{ NULL, NULL },
};

void
pefs_warn(const char *fmt, ...)
{
        va_list ap;

        va_start(ap, fmt);
        vwarnx(fmt, ap);
        va_end(ap);
}

static int
checkargs_fs(int argc, char **argv __unused)
{
	if (argc != 1) {
		if (argc == 0)
			warnx("missing filesystem argument");
		else
			warnx("too many arguments");
		return (0);
	}

	return (1);
}

static void
initfsroot(int argc, char **argv, int flags, char *fsroot, size_t size)
{
	if (!checkargs_fs(argc, argv))
		pefs_usage();

	if (pefs_getfsroot(argv[0], flags, fsroot, size) != 0)
		exit(PEFS_ERR_INVALID);
}

static int
openx_rdonly(const char *path)
{
	int fd;

	fd = open(path, O_RDONLY);
	if (fd == -1)
		warn("cannot open %s", path);

	return (fd);

}

struct pefs_readpassfile_ctx {
	size_t	passphrase_pos;
	size_t	passphrase_sz;
	char	*passphrase;
};

static int
pefs_readpassfile_handler(void *a, uint8_t *buf, size_t len, const char *file)
{
	struct pefs_readpassfile_ctx *ctx = a;
	char *s;

	if (strlen(buf) != len) {
		pefs_warn("invalid passfile content: %s.", file);
		return (PEFS_ERR_INVALID);
	}

	s = strchr(buf, '\n');
	if (s != NULL)
		*s = '\0';
	if (strlcat(ctx->passphrase, buf, ctx->passphrase_sz) >=
	    ctx->passphrase_sz) {
		pefs_warn("passphrase in %s too long.", file);
		bzero(ctx->passphrase, ctx->passphrase_sz);
		return (PEFS_ERR_INVALID);
	}

	return (0);
}

static int
pefs_readpassfile(char *passphrase, int passphrase_sz, const char **files,
    int file_count)
{
	struct pefs_readpassfile_ctx ctx;
	int error;

	ctx.passphrase_pos = 0;
	ctx.passphrase_sz = passphrase_sz;
	ctx.passphrase = passphrase;

	bzero(ctx.passphrase, ctx.passphrase_sz);

	error = pefs_readfiles(files, file_count, &ctx,
	    pefs_readpassfile_handler);
	if (error != 0)
		bzero(ctx.passphrase, ctx.passphrase_sz);
	return (error);
}

static int
pefs_readpassphrase(char *passphrase, int passphrase_sz, const char *prompt,
    int verify)
{
	char promptbuf[64], buf[BUFSIZ], buf2[BUFSIZ], *p;
	int i;

	if (verify)
		verify = 1;
	if (prompt == NULL)
		prompt = PEFS_KEY_PROMPT_DEFAULT;
	for (i = 0; i <= verify; i++) {
		snprintf(promptbuf, sizeof(promptbuf), "%s %s:",
		    !i ? "Enter" : "Reenter", prompt);
		p = readpassphrase(promptbuf, !i ? buf : buf2, BUFSIZ,
		    RPP_ECHO_OFF | RPP_REQUIRE_TTY);
		if (p == NULL || p[0] == '\0') {
			bzero(buf, sizeof(buf));
			bzero(buf2, sizeof(buf2));
			warnx("unable to read passphrase");
			return (PEFS_ERR_INVALID);
		}
	}
	if (verify && strcmp(buf, buf2) != 0) {
		bzero(buf, sizeof(buf));
		bzero(buf2, sizeof(buf2));
		warnx("passphrases didn't match");
		return (PEFS_ERR_INVALID);
	}
	strlcpy(passphrase, buf, passphrase_sz);
	bzero(buf2, sizeof(buf2));
	bzero(buf, sizeof(buf));

	return (0);
}

static int
pefs_key_get(struct pefs_xkey *xk, const char *prompt, int verify,
    struct pefs_keyparam *kp)
{
	char buf[BUFSIZ];
	int error;

	if (kp->kp_passfile_count != 0 && kp->kp_nopassphrase != 0) {
		pefs_warn("options no-passphrase (-p) and passphrase-file (-j) "
		    "are mutually exclusive.");
		return (PEFS_ERR_USAGE);
	}

	buf[0] = '\0';
	if (kp->kp_passfile_count != 0) {
		error = pefs_readpassfile(buf, sizeof(buf), kp->kp_passfile,
		    kp->kp_passfile_count);
		if (error != 0)
			return (error);
	} else if (kp->kp_nopassphrase == 0) {
		error = pefs_readpassphrase(buf, sizeof(buf), prompt, verify);
		if (error != 0)
			return (error);
	}

	error = pefs_key_generate(xk, buf, kp);
	bzero(buf, sizeof(buf));

	return (error);
}

static inline void
pefs_key_showind(struct pefs_xkey *xk, int ind)
{
	printf("\t%-4d %016jx %s\n", ind, pefs_keyid_as_int(xk->pxk_keyid),
	    pefs_alg_name(xk));
}

static inline void
pefs_key_shownode(struct pefs_xkey *xk, const char *path)
{
	const char *basepath;

	basepath = basename(path);
	if (xk == NULL)
		printf("Key(%s): <not specified>\n", basepath);
	else
		printf("Key(%s): %016jx %s\n", basepath,
		    pefs_keyid_as_int(xk->pxk_keyid), pefs_alg_name(xk));
}

static int
pefs_keychain_lookup(struct pefs_keychain_head *kch, const char *fsroot,
    int chain_flags, struct pefs_keyparam *kp)
{
	struct pefs_xkey k;
	int error;

	error = pefs_keyparam_init(kp, fsroot);
	if (error != 0)
		return (error);
	error = pefs_key_get(&k, NULL, 0, kp);
	if (error != 0)
		return (error);

	error = pefs_keychain_get(kch, fsroot, chain_flags, &k);
	bzero(&k, sizeof(k));
	if (error)
		return (PEFS_ERR_INVALID);

	return (0);
}

static int
pefs_keyop(keyop_func_t func, int argc, char *argv[])
{
	struct pefs_keychain_head kch;
	struct pefs_keyparam kp;
	char fsroot[MAXPATHLEN];
	int error, fd, i;
	int chain = PEFS_KEYCHAIN_IGNORE_MISSING;
	int verbose = 0;

	pefs_keyparam_create(&kp);
	while ((i = getopt(argc, argv, "cCpva:i:j:k:")) != -1)
		switch(i) {
		case 'a':
			if (pefs_keyparam_setalg(&kp, optarg) != 0)
				pefs_usage_alg();
			break;
		case 'c':
			chain = PEFS_KEYCHAIN_USE;
			break;
		case 'C':
			chain = 0;
			break;
		case 'p':
			kp.kp_nopassphrase = 1;
			break;
		case 'i':
			if (pefs_keyparam_setiterations(&kp, optarg) != 0)
				pefs_usage();
			break;
		case 'j':
			if (pefs_keyparam_setfile(&kp, kp.kp_passfile,
			    optarg) != 0)
				pefs_usage();
			break;
		case 'k':
			if (pefs_keyparam_setfile(&kp, kp.kp_keyfile,
			    optarg) != 0)
				pefs_usage();
			break;
		case 'v':
			verbose = 1;
			break;
		default:
			pefs_usage();
		}
	argc -= optind;
	argv += optind;

	initfsroot(argc, argv, 0, fsroot, sizeof(fsroot));

	error = pefs_keychain_lookup(&kch, fsroot, chain, &kp);
	if (error != 0)
		return (error);

	fd = openx_rdonly(fsroot);
	if (fd == -1) {
		pefs_keychain_free(&kch);
		return (PEFS_ERR_IO);
	}

	error = func(&kch, fd, verbose);

	pefs_keychain_free(&kch);

	close(fd);

	return (error);
}

static int
pefs_addkey_op(struct pefs_keychain_head *kch, int fd, int verbose)
{
	struct pefs_keychain *kc;

	TAILQ_FOREACH(kc, kch, kc_entry) {
		if (ioctl(fd, PEFS_ADDKEY, &kc->kc_key) == -1) {
			warn("cannot add key");
			return (-1);
		} else if (verbose)
			printf("Key added: %016jx\n",
			    pefs_keyid_as_int(kc->kc_key.pxk_keyid));
	}

	return (0);
}

static int
pefs_delkey_op(struct pefs_keychain_head *kch, int fd, int verbose)
{
	struct pefs_keychain *kc;

	TAILQ_FOREACH(kc, kch, kc_entry) {
		if (ioctl(fd, PEFS_DELKEY, &kc->kc_key) == -1) {
			warn("cannot delete key");
		} else if (verbose)
			printf("Key deleted: %016jx\n",
			    pefs_keyid_as_int(kc->kc_key.pxk_keyid));
	}

	return (0);
}

static int
pefs_addkey(int argc, char *argv[])
{
	return (pefs_keyop(pefs_addkey_op, argc, argv));
}

static int
pefs_delkey(int argc, char *argv[])
{
	return (pefs_keyop(pefs_delkey_op, argc, argv));
}

static int
pefs_setkey(int argc, char *argv[])
{
	struct pefs_keychain_head kch;
	struct pefs_keychain *kc;
	struct pefs_keyparam kp;
	char fsroot[MAXPATHLEN];
	int error, fd, i;
	int verbose = 0;
	int addkey = 0;
	int chain = PEFS_KEYCHAIN_IGNORE_MISSING;

	pefs_keyparam_create(&kp);
	while ((i = getopt(argc, argv, "cCpvxa:i:j:k:")) != -1)
		switch(i) {
		case 'a':
			if (pefs_keyparam_setalg(&kp, optarg) != 0)
				pefs_usage_alg();
			break;
		case 'c':
			chain = PEFS_KEYCHAIN_USE;
			break;
		case 'C':
			chain = 0;
			break;
		case 'p':
			kp.kp_nopassphrase = 1;
			break;
		case 'i':
			if (pefs_keyparam_setiterations(&kp, optarg) != 0)
				pefs_usage();
			break;
		case 'j':
			if (pefs_keyparam_setfile(&kp, kp.kp_passfile,
			    optarg) != 0)
				pefs_usage();
			break;
		case 'k':
			if (pefs_keyparam_setfile(&kp, kp.kp_keyfile,
			    optarg) != 0)
				pefs_usage();
			break;
		case 'v':
			verbose = 1;
			break;
		case 'x':
			addkey = 1;
			break;
		default:
			pefs_usage();
		}
	argc -= optind;
	argv += optind;

	if (chain == PEFS_KEYCHAIN_USE && addkey) {
		warnx("invalid argument combination: -x -c");
		return (PEFS_ERR_USAGE);
	}

	if (argc != 1) {
		if (argc == 0)
			warnx("missing directory argument");
		else
			warnx("too many arguments");
		pefs_usage();
	}

	initfsroot(argc, argv, 0, fsroot, sizeof(fsroot));

	error = pefs_keychain_lookup(&kch, fsroot, chain, &kp);
	if (error != 0)
		return (error);

	fd = openx_rdonly(argv[0]);
	if (fd == -1) {
		pefs_keychain_free(&kch);
		return (PEFS_ERR_IO);
	}

	if (addkey) {
		TAILQ_FOREACH(kc, &kch, kc_entry) {
			if (ioctl(fd, PEFS_ADDKEY, &kc->kc_key) == 0 && verbose)
				printf("Key added: %016jx\n",
				    pefs_keyid_as_int(kc->kc_key.pxk_keyid));
		}
	}
	kc = TAILQ_FIRST(&kch);
	if (ioctl(fd, PEFS_SETKEY, &kc->kc_key) == -1) {
		warn("cannot set key");
		error = PEFS_ERR_SYS;
	} else if (verbose)
		pefs_key_shownode(&kc->kc_key, argv[0]);

	pefs_keychain_free(&kch);

	close(fd);

	return (error);
}

static int
pefs_flushkeys(int argc, char *argv[])
{
	char fsroot[MAXPATHLEN];
	int fd;

	initfsroot(argc, argv, 0, fsroot, sizeof(fsroot));

	fd = openx_rdonly(fsroot);
	if (fd == -1)
		return (PEFS_ERR_IO);
	if (ioctl(fd, PEFS_FLUSHKEYS) == -1) {
		warn("cannot flush keys");
		return (PEFS_ERR_IO);
	}
	close(fd);

	return (0);
}

static int
pefs_getkey(int argc, char *argv[])
{
	struct pefs_xkey k;
	int testonly = 0;
	int error = 0;
	int fd, i;

	while ((i = getopt(argc, argv, "t")) != -1)
		switch(i) {
		case 't':
			testonly = 1;
			break;
		case '?':
		default:
			pefs_usage();
		}
	argc -= optind;
	argv += optind;

	if (argc != 1) {
		if (argc == 0)
			warnx("missing file argument");
		else
			warnx("too many arguments");
		pefs_usage();
	}

	/* only check file system type */
	if (pefs_getfsroot(argv[0], 0, NULL, 0) != 0)
		return (PEFS_ERR_INVALID);

	fd = openx_rdonly(argv[0]);
	if (fd == -1)
		return (PEFS_ERR_IO);

	bzero(&k, sizeof(k));
	if (ioctl(fd, PEFS_GETNODEKEY, &k) == -1) {
		if (errno == ENOENT) {
			if (testonly == 0)
				pefs_key_shownode(NULL, argv[0]);
			else
				error = PEFS_ERR_GENERIC;
		} else {
			warn("cannot get key");
			error = PEFS_ERR_SYS;
		}
	} else if (testonly == 0)
		pefs_key_shownode(&k, argv[0]);

	close(fd);

	return (error);
}

static int
pefs_showkeys(int argc, char *argv[])
{
	char fsroot[MAXPATHLEN];
	struct pefs_xkey k;
	int testonly = 0;
	int fd, i;

	while ((i = getopt(argc, argv, "t")) != -1)
		switch(i) {
		case 't':
			testonly = 1;
			break;
		case '?':
		default:
			pefs_usage();
		}
	argc -= optind;
	argv += optind;

	initfsroot(argc, argv, 0, fsroot, sizeof(fsroot));

	fd = openx_rdonly(fsroot);
	if (fd == -1)
		return (PEFS_ERR_IO);

	bzero(&k, sizeof(k));
	if (ioctl(fd, PEFS_GETKEY, &k) == -1) {
		if (testonly) {
			close(fd);
			return (PEFS_ERR_INVALID);
		}
		if (errno == ENOENT)
			printf("No keys specified\n");
		else {
			warn("cannot list keys");
			close(fd);
			return (PEFS_ERR_IO);
		}
	} else {
		if (testonly) {
			close(fd);
			return (0);
		}
		printf("Keys:\n");
		while (1) {
			pefs_key_showind(&k, k.pxk_index);
			k.pxk_index++;
			if (ioctl(fd, PEFS_GETKEY, &k) == -1)
				break;
		}
	}
	close(fd);

	return (0);
}

static int
pefs_mount(int argc, char *argv[])
{
	char **nargv;
	int nargc, topt, i, shift;

	topt = 0;
	opterr = 0;
	while ((i = getopt(argc, argv, "t:")) != -1)
		switch(i) {
		case 't':
			if (strcmp(optarg, PEFS_FSTYPE) != 0) {
				warnx("invalid file system type: %s",
				    optarg);
				return (PEFS_ERR_USAGE);
			}
			topt = 1;
			break;
		default:
			break;
		}

	shift = (topt == 0 ? 2 : 0);
	nargc = argc + shift + 2;
	nargv = malloc(nargc * sizeof(*nargv));
	nargv[0] = __DECONST(char *, "pefs mount");
	if (topt == 0) {
		nargv[1] = __DECONST(char *, "-t");
		nargv[2] = __DECONST(char *, PEFS_FSTYPE);
	}
	for (i = 0; i < argc; i++)
		nargv[i + shift + 1] = argv[i];
	nargv[nargc - 1] = NULL;

	execv(PATH_MOUNT, nargv);
	warnx("exec %s", PATH_MOUNT);

	return (PEFS_ERR_SYS);
}

static int
pefs_unmount(int argc, char *argv[])
{
	char **nargv;
	int i;

	while ((i = getopt(argc, argv, "fv")) != -1)
		switch(i) {
		case 'f':
		case 'v':
			break;
		case '?':
		default:
			pefs_usage();
		}
	argc -= optind;
	argv += optind;

	if (!checkargs_fs(argc, argv))
		pefs_usage();

	nargv = malloc((argc + 2) * sizeof(*nargv));
	for (i = 0; i < argc; i++)
		nargv[i + 1] = argv[i];
	nargv[0] = __DECONST(char *, "pefs unmount");
	nargv[argc + 1] = NULL;

	execv(PATH_UMOUNT, nargv);
	warnx("exec %s", PATH_UMOUNT);

	return (PEFS_ERR_SYS);
}

static int
pefs_addchain(int argc, char *argv[])
{
	struct pefs_keychain *kc;
	struct pefs_keychain_head kch;
	struct {
		struct pefs_xkey k;
		struct pefs_keyparam kp;
	} p[2];
	struct pefs_xkey *k1 = &p[0].k, *k2 = &p[1].k;
	struct pefs_keyparam *kpi;
	char fsroot[MAXPATHLEN];
	int fsflags = 0, verbose = 0;
	int zerochainedkey = 0, optchainedkey = 0;
	int error, i, fd;

	pefs_keyparam_create(&p[0].kp);
	pefs_keyparam_create(&p[1].kp);
	while ((i = getopt(argc, argv, "a:A:i:I:j:J:k:K:fpPvZ")) != -1)
		switch(i) {
		case 'v':
			verbose = 1;
			break;
		case 'f':
			fsflags |= PEFS_FS_IGNORE_TYPE;
			break;
		case 'Z':
			zerochainedkey = 1;
			break;
		case 'a':
		case 'A':
			if (isupper(i))
				optchainedkey = i;
			kpi = &p[isupper(i) ? 1 : 0].kp;
			if (pefs_keyparam_setalg(kpi, optarg) != 0)
				pefs_usage_alg();
			break;
		case 'p':
		case 'P':
			if (isupper(i))
				optchainedkey = i;
			kpi = &p[isupper(i) ? 1 : 0].kp;
			kpi->kp_nopassphrase = 1;
			break;
		case 'i':
		case 'I':
			if (isupper(i))
				optchainedkey = i;
			kpi = &p[isupper(i) ? 1 : 0].kp;
			if (pefs_keyparam_setiterations(kpi, optarg) != 0)
				pefs_usage();
			break;
		case 'j':
		case 'J':
			if (isupper(i))
				optchainedkey = i;
			kpi = &p[isupper(i) ? 1 : 0].kp;
			if (pefs_keyparam_setfile(kpi, kpi->kp_passfile,
			    optarg) != 0)
				pefs_usage();
			break;
		case 'k':
		case 'K':
			if (isupper(i))
				optchainedkey = i;
			kpi = &p[isupper(i) ? 1 : 0].kp;
			if (pefs_keyparam_setfile(kpi, kpi->kp_keyfile,
			    optarg) != 0)
				pefs_usage();
			break;
		default:
			pefs_usage();
		}
	argc -= optind;
	argv += optind;

	if (optchainedkey && zerochainedkey) {
		warnx("invalid argument combination: -Z -%c",
		    optchainedkey);
		return (PEFS_ERR_USAGE);
	}

	initfsroot(argc, argv, fsflags, fsroot, sizeof(fsroot));

	error = pefs_keyparam_init(&p[0].kp, fsroot);
	if (error != 0)
		return (error);
	error = pefs_keyparam_init(&p[1].kp, fsroot);
	if (error != 0)
		return (error);

	error = pefs_key_get(k1, "parent key passphrase", 1, &p[0].kp);
	if (error != 0) {
		bzero(p, sizeof(p));
		return (error);
	}

	if (zerochainedkey) {
		fd = openx_rdonly(PATH_DEVRANDOM);
		if (fd == -1) {
			bzero(p, sizeof(p));
			return (PEFS_ERR_IO);
		}
		read(fd, k2, sizeof(struct pefs_keychain));
		close(fd);
		k2->pxk_alg = PEFS_ALG_INVALID;
		error = pefs_keychain_set(fsroot, k1, k2);
		bzero(p, sizeof(p));
		if (error)
			return (PEFS_ERR_INVALID);
		return (0);
	}

	error = pefs_key_get(k2, "chained key passphrase", 1, &p[1].kp);
	if (error != 0) {
		bzero(p, sizeof(p));
		return (error);
	}

	pefs_keychain_get(&kch, fsroot, PEFS_KEYCHAIN_IGNORE_MISSING, k1);
	TAILQ_FOREACH(kc, &kch, kc_entry) {
		if (memcmp(k2->pxk_keyid, kc->kc_key.pxk_keyid,
		    PEFS_KEYID_SIZE) == 0) {
			pefs_keychain_free(&kch);
			bzero(k1->pxk_key, PEFS_KEY_SIZE);
			bzero(k2->pxk_key, PEFS_KEY_SIZE);
			warnx("key chain is already set: %016jx -> %016jx",
			    pefs_keyid_as_int(k1->pxk_keyid),
			    pefs_keyid_as_int(k2->pxk_keyid));
			pefs_keychain_free(&kch);
			return (PEFS_ERR_INVALID);
		}
	}
	kc = TAILQ_FIRST(&kch);
	if (TAILQ_NEXT(kc, kc_entry) != NULL) {
		bzero(k1->pxk_key, PEFS_KEY_SIZE);
		bzero(k2->pxk_key, PEFS_KEY_SIZE);
		warnx("key chain for parent key is already set: "
		    "%016jx -> %016jx",
		    pefs_keyid_as_int(kc->kc_key.pxk_keyid),
		    pefs_keyid_as_int(
			TAILQ_NEXT(kc, kc_entry)->kc_key.pxk_keyid));
		pefs_keychain_free(&kch);
		return (PEFS_ERR_INVALID);
	}
	pefs_keychain_free(&kch);

	error = pefs_keychain_set(fsroot, k1, k2);
	bzero(k1->pxk_key, PEFS_KEY_SIZE);
	bzero(k2->pxk_key, PEFS_KEY_SIZE);
	if (error)
		return (PEFS_ERR_INVALID);
	if (verbose) {
		if (zerochainedkey)
			printf("Key chain set: %016jx\n",
			    pefs_keyid_as_int(k1->pxk_keyid));
		else
			printf("Key chain set: %016jx -> %016jx\n",
			    pefs_keyid_as_int(k1->pxk_keyid),
			    pefs_keyid_as_int(k2->pxk_keyid));
	}

	return (0);
}

static int
pefs_delchain(int argc, char *argv[])
{
	struct pefs_keyparam kp;
	struct pefs_keychain *kc, *kc_next;
	struct pefs_keychain_head kch;
	char fsroot[MAXPATHLEN];
	int deleteall = 0, fsflags = 0, verbose = 0;
	int error, i;

	pefs_keyparam_create(&kp);
	while ((i = getopt(argc, argv, "fFvpi:j:k:")) != -1)
		switch(i) {
		case 'f':
			fsflags |= PEFS_FS_IGNORE_TYPE;
			break;
		case 'F':
			deleteall = 1;
			break;
		case 'v':
			verbose = 1;
			break;
		case 'p':
			kp.kp_nopassphrase = 1;
			break;
		case 'i':
			if (pefs_keyparam_setiterations(&kp, optarg) != 0)
				pefs_usage();
			break;
		case 'j':
			if (pefs_keyparam_setfile(&kp, kp.kp_passfile,
			    optarg) != 0)
				pefs_usage();
			break;
		case 'k':
			if (pefs_keyparam_setfile(&kp, kp.kp_keyfile,
			    optarg) != 0)
				pefs_usage();
		default:
			pefs_usage();
		}
	argc -= optind;
	argv += optind;

	initfsroot(argc, argv, fsflags, fsroot, sizeof(fsroot));

	error = pefs_keychain_lookup(&kch, fsroot, PEFS_KEYCHAIN_USE, &kp);
	if (error != 0)
		return (error);

	TAILQ_FOREACH(kc, &kch, kc_entry) {
		kc_next = TAILQ_NEXT(kc, kc_entry);
		error = pefs_keychain_del(fsroot,
		    kc_next == NULL ? PEFS_KEYCHAIN_IGNORE_MISSING : 0,
		    &kc->kc_key);
		if (error != 0)
			break;
		if (verbose) {
			if (kc_next == NULL)
				printf("Key chain deleted: %016jx\n",
				    pefs_keyid_as_int(kc->kc_key.pxk_keyid));
			else
				printf("Key chain deleted: %016jx -> %016jx\n",
				    pefs_keyid_as_int(kc->kc_key.pxk_keyid),
				    pefs_keyid_as_int(
					kc_next->kc_key.pxk_keyid));
		}
		if (!deleteall)
			break;
	}
	pefs_keychain_free(&kch);

	return (0);
}

static int
pefs_showchains(int argc, char *argv[])
{
	struct pefs_keyparam kp;
	struct pefs_keychain *kc;
	struct pefs_keychain_head kch;
	char fsroot[MAXPATHLEN];
	int fsflags = 0;
	int error, i;

	pefs_keyparam_create(&kp);
	while ((i = getopt(argc, argv, "fpi:j:k:")) != -1)
		switch(i) {
		case 'f':
			fsflags |= PEFS_FS_IGNORE_TYPE;
			break;
		case 'p':
			kp.kp_nopassphrase = 1;
			break;
		case 'i':
			if (pefs_keyparam_setiterations(&kp, optarg) != 0)
				pefs_usage();
			break;
		case 'j':
			if (pefs_keyparam_setfile(&kp, kp.kp_passfile,
			    optarg) != 0)
				pefs_usage();
		case 'k':
			if (pefs_keyparam_setfile(&kp, kp.kp_keyfile,
			    optarg) != 0)
				pefs_usage();
			break;
		default:
			pefs_usage();
		}
	argc -= optind;
	argv += optind;

	initfsroot(argc, argv, fsflags, fsroot, sizeof(fsroot));

	error = pefs_keychain_lookup(&kch, fsroot, PEFS_KEYCHAIN_USE, &kp);
	if (error)
		return (PEFS_ERR_INVALID);

	printf("Key chain:\n");
	i = 1;
	TAILQ_FOREACH(kc, &kch, kc_entry) {
		pefs_key_showind(&kc->kc_key, i++);
	}
	pefs_keychain_free(&kch);

	return (0);
}

static int
pefs_randomchain(int argc, char *argv[])
{
	struct pefs_xkey k[2];
	char fsroot[MAXPATHLEN];
	int nmin = PEFS_RANDOMCHAIN_MIN, nmax = PEFS_RANDOMCHAIN_MAX;
	int fsflags = 0, verbose = 0;
	int i, n, fd;

	k[0].pxk_index = k[1].pxk_index = -1;
	while ((i = getopt(argc, argv, "vfn:N:")) != -1)
		switch(i) {
		case 'v':
			verbose = 1;
			break;
		case 'f':
			fsflags |= PEFS_FS_IGNORE_TYPE;
			break;
		case 'n':
			if ((nmin = atoi(optarg)) <= 0) {
				warnx("invalid lower bound argument: %s",
				    optarg);
				pefs_usage();
			}
			break;
		case 'N':
			if ((nmax = atoi(optarg)) <= 0) {
				warnx("invalid lower bound argument: %s",
				    optarg);
				pefs_usage();
			}
			break;
		default:
			pefs_usage();
		}
	argc -= optind;
	argv += optind;

	if (nmin >= nmax) {
		warnx("invalid arguments: "
		    "lower bound (%d) >= upper bound (%d)", nmin, nmax);
		return (PEFS_ERR_USAGE);
	}

	initfsroot(argc, argv, fsflags, fsroot, sizeof(fsroot));

	n = arc4random_uniform(nmax - nmin) + nmin;
	n /= 2;

	fd = openx_rdonly(PATH_DEVRANDOM);
	if (fd == -1)
		return (PEFS_ERR_IO);

	for (i = 1; i <= n; i++) {
		read(fd, k, sizeof(k));
		k[0].pxk_alg = PEFS_ALG_INVALID;
		k[1].pxk_alg = PEFS_ALG_INVALID;
		pefs_keychain_set(fsroot, &k[0], &k[1]);
		if (verbose)
			printf("Key chain set: %016jx -> %016jx\n",
			    pefs_keyid_as_int(k[0].pxk_keyid),
			    pefs_keyid_as_int(k[1].pxk_keyid));
	}

	close(fd);

	return (0);
}

static int
pefs_showalgs(int argc, char *argv[] __unused)
{
	if (argc != 0)
		pefs_usage();

	pefs_alg_list(stdout);

	return (0);
}

static void
pefs_usage_alg(void)
{
	pefs_alg_list(stderr);
	exit(PEFS_ERR_USAGE);
}

static void
pefs_usage(void)
{
	fprintf(stderr,
"usage:	pefs mount [-o options] [from filesystem]\n"
"	pefs unmount [-fv] filesystem\n"
"	pefs addkey [-cCpv] [-a alg] [-i iterations] [-j passfile] [-k keyfile] filesystem\n"
"	pefs delkey [-cCpv] [-i iterations] [-j passfile] [-k keyfile] filesystem\n"
"	pefs flushkeys filesystem\n"
"	pefs getkey [-t] file\n"
"	pefs setkey [-cCpvx] [-a alg] [-i iterations] [-j passfile] [-k keyfile] directory\n"
"	pefs showkeys [-t] filesystem\n"
"	pefs addchain [-fpPvZ] [-a alg] [-i iterations] [-j passfile] [-k keyfile]\n"
"		[-A alg] [-I iterations] [-J passfile] [-K keyfile] filesystem\n"
"	pefs delchain [-fFpv] [-i iterations] [-j passfile] [-k keyfile] filesystem\n"
"	pefs randomchain [-fv] [-n min] [-N max] filesystem\n"
"	pefs showchains [-fp] [-i iterations] [-j passfile] [-k keyfile] filesystem\n"
"	pefs showalgs\n"
);
	exit(PEFS_ERR_USAGE);
}

static void
pefs_kld_load(void)
{
	if (modfind(PEFS_KLD) < 0)
		if (kld_load(PEFS_KLD) < 0 || modfind(PEFS_KLD) < 0)
			errx(PEFS_ERR_SYS,
			    "cannot find or load \"%s\" kernel module",
			    PEFS_KLD);
}

int
main(int argc, char *argv[])
{
	struct command *cmd = NULL;
	char *prog;

	prog = strrchr(argv[0], '/');
	if (prog == NULL)
		prog = argv[0];

	if (argc <= 1)
		pefs_usage();

	for (cmd = cmds; cmd->name; cmd++) {
		if (strcmp(cmd->name, argv[1]) == 0) {
			argc -= 2;
			argv += 2;
			optind = 0;
			optreset = 1;
			pefs_kld_load();
			return (cmd->func(argc, argv));
		}
	}

	warnx("unknown command: %s", argv[1]);
	pefs_usage();

	return (1);
}
