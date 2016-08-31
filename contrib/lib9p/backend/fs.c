/*
 * Copyright 2016 Jakub Klama <jceel@FreeBSD.org>
 * All rights reserved
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted providing that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

/*
 * Based on libixp code: ©2007-2010 Kris Maglione <maglione.k at Gmail>
 */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/param.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <dirent.h>
#include <pwd.h>
#include <grp.h>
#include <libgen.h>
#include "../lib9p.h"
#include "../lib9p_impl.h"
#include "../fid.h"
#include "../log.h"
#include "../rfuncs.h"
#include "backend.h"
#include "fs.h"

#if defined(__FreeBSD__)
  #include <sys/param.h>
  #if __FreeBSD_version >= 1000000
    #define	HAVE_BINDAT
  #endif
#endif

#if defined(__FreeBSD__)
  #define	HAVE_BIRTHTIME
#endif

#if defined(__FreeBSD__)
  /* should probably check version but fstatat has been in for ages */
  #define HAVE_FSTATAT
#endif

#if defined(__APPLE__)
  #include "Availability.h"
  #if __MAC_OS_X_VERSION_MIN_REQUIRED > 1090
    #define HAVE_FSTATAT
  #endif
#endif

struct fs_softc {
	const char *fs_rootpath;
	bool fs_readonly;
};

struct fs_fid {
	DIR	*ff_dir;
	int	ff_fd;
	char	*ff_name;
	struct fs_authinfo *ff_ai;
};

/*
 * Our authinfo consists of:
 *
 *  - a reference count
 *  - a uid
 *  - a gid-set
 *
 * The "default" gid is the first gid in the git-set, provided the
 * set size is at least 1.  The set-size may be zero, though.
 *
 * Note that some ops (Linux style ones) pass an effective gid for
 * the op, in which case, that gid may override.  To achieve this
 * effect, permissions testing functions also take an extra gid.
 * If this gid is (gid_t)-1 it is not used and only the remaining
 * gids take part.
 *
 * The uid may also be (uid_t)-1, meaning "no uid was available
 * at all at attach time".  In this case, new files inherit parent
 * directory uids.
 *
 * The refcount is simply the number of "openfile"s using this
 * authinfo (so that when the last ref goes away, we can free it).
 */
struct fs_authinfo {
	uint32_t ai_refcnt;
	uid_t	ai_uid;
	int	ai_ngids;
	gid_t	ai_gids[];	/* NB: flexible array member */
};

/*
 * Internal functions (except inline functions).
 */
static int fs_buildname(struct l9p_fid *, char *, char *, size_t);
static int fs_dpf(char *, char *, size_t);
static int fs_oflags_dotu(int, int *);
static int fs_oflags_dotl(uint32_t, int *, enum l9p_omode *);
static int fs_nde(struct fs_softc *, struct l9p_fid *, gid_t,
    struct stat *, uid_t *, gid_t *);
static int fs_rde(struct fs_softc *, struct l9p_fid *, struct stat *);
static int fs_rdf(struct fs_softc *, struct l9p_fid *,
    char *, size_t, struct stat *);
static struct fs_fid *open_fid(const char *, struct fs_authinfo *);
static void dostat(struct l9p_stat *, char *, struct stat *, bool dotu);
static void dostatfs(struct l9p_statfs *, struct statfs *, long);
static bool check_access(struct stat *, struct fs_authinfo *, gid_t,
    enum l9p_omode);
static void generate_qid(struct stat *, struct l9p_qid *);

static int fs_icreate(void *, struct l9p_fid *, char *, int,
    bool, mode_t, gid_t, struct stat *);
static int fs_iopen(void *, struct l9p_fid *, int, enum l9p_omode,
    gid_t, struct stat *);
static int fs_imkdir(void *, struct l9p_fid *, char *,
    bool, mode_t, gid_t, struct stat *);
static int fs_imkfifo(void *, struct l9p_fid *, char *,
    bool, mode_t, gid_t, struct stat *);
static int fs_imknod(void *, struct l9p_fid *, char *,
    bool, mode_t, dev_t, gid_t, struct stat *);
static int fs_imksocket(void *, struct l9p_fid *, char *,
    bool, mode_t, gid_t, struct stat *);
static int fs_isymlink(void *, struct l9p_fid *, char *, char *,
    gid_t, struct stat *);

/*
 * Internal functions implementing backend.
 */
static int fs_attach(void *, struct l9p_request *);
static int fs_clunk(void *, struct l9p_fid *);
static int fs_create(void *, struct l9p_request *);
static int fs_open(void *, struct l9p_request *);
static int fs_read(void *, struct l9p_request *);
static int fs_remove(void *, struct l9p_fid *);
static int fs_stat(void *, struct l9p_request *);
static int fs_walk(void *, struct l9p_request *);
static int fs_write(void *, struct l9p_request *);
static int fs_wstat(void *, struct l9p_request *);
static int fs_statfs(void *, struct l9p_request *);
static int fs_lopen(void *, struct l9p_request *);
static int fs_lcreate(void *, struct l9p_request *);
static int fs_symlink(void *, struct l9p_request *);
static int fs_mknod(void *, struct l9p_request *);
static int fs_rename(void *, struct l9p_request *);
static int fs_readlink(void *, struct l9p_request *);
static int fs_getattr(void *, struct l9p_request *);
static int fs_setattr(void *, struct l9p_request *);
static int fs_xattrwalk(void *, struct l9p_request *);
static int fs_xattrcreate(void *, struct l9p_request *);
static int fs_readdir(void *, struct l9p_request *);
static int fs_fsync(void *, struct l9p_request *);
static int fs_lock(void *, struct l9p_request *);
static int fs_getlock(void *, struct l9p_request *);
static int fs_link(void *, struct l9p_request *);
static int fs_renameat(void *softc, struct l9p_request *req);
static int fs_unlinkat(void *softc, struct l9p_request *req);
static void fs_freefid(void *softc, struct l9p_fid *f);

/*
 * Convert from 9p2000 open/create mode to Unix-style O_* flags.
 * This includes 9p2000.u extensions, but not 9p2000.L protocol,
 * which has entirely different open, create, etc., flag bits.
 *
 * The <mode> given here is the one-byte (uint8_t) "mode"
 * argument to Tcreate or Topen, so it can have at most 8 bits.
 *
 * https://swtch.com/plan9port/man/man9/open.html and
 * http://plan9.bell-labs.com/magic/man2html/5/open
 * both say:
 *
 *   The [low two bits of the] mode field determines the
 *   type of I/O ... [I]f mode has the OTRUNC (0x10) bit
 *   set, the file is to be truncated, which requires write
 *   permission ...; if the mode has the ORCLOSE (0x40) bit
 *   set, the file is to be removed when the fid is clunked,
 *   which requires permission to remove the file from its
 *   directory.  All other bits in mode should be zero.  It
 *   is illegal to write a directory, truncate it, or
 *   attempt to remove it on close.
 *
 * 9P2000.u may add ODIRECT (0x80); this is not completely clear.
 * The fcall.h header defines OCEXEC (0x20) as well, but it makes
 * no sense to send this to a server.  There seem to be no bits
 * 0x04 and 0x08.
 *
 * We always turn on O_NOCTTY since as a server, we never want
 * to gain a controlling terminal.  We always turn on O_NOFOLLOW
 * for reasons described elsewhere.
 */
static int
fs_oflags_dotu(int mode, int *aflags)
{
	int flags;
#define	CONVERT(theirs, ours) \
	do { \
		if (mode & (theirs)) { \
			mode &= ~(theirs); \
			flags |= ours; \
		} \
	} while (0)

	switch (mode & L9P_OACCMODE) {

	case L9P_OREAD:
	default:
		flags = O_RDONLY;
		break;

	case L9P_OWRITE:
		flags = O_WRONLY;
		break;

	case L9P_ORDWR:
		flags = O_RDWR;
		break;

	case L9P_OEXEC:
		if (mode & L9P_OTRUNC)
			return (EINVAL);
		flags = O_RDONLY;
		break;
	}

	flags |= O_NOCTTY | O_NOFOLLOW;

	CONVERT(L9P_OTRUNC, O_TRUNC);

	/*
	 * Now take away some flags locally:
	 *   the access mode (already translated)
	 *   ORCLOSE - caller only
	 *   OCEXEC - makes no sense in server
	 *   ODIRECT - not applicable here
	 * If there are any flag bits left after this,
	 * we were unable to translate them.  For now, let's
	 * treat this as EINVAL so that we can catch problems.
	 */
	mode &= ~(L9P_OACCMODE | L9P_ORCLOSE | L9P_OCEXEC | L9P_ODIRECT);
	if (mode != 0) {
		L9P_LOG(L9P_INFO,
		    "fs_oflags_dotu: untranslated bits: %#x",
		    (unsigned)mode);
		return (EINVAL);
	}

	*aflags = flags;
	return (0);
#undef CONVERT
}

/*
 * Convert from 9P2000.L (Linux) open mode bits to O_* flags.
 * See fs_oflags_dotu above.
 *
 * Linux currently does not have open-for-exec, but there is a
 * proposal for it using O_PATH|O_NOFOLLOW, now handled here.
 *
 * We may eventually also set L9P_ORCLOSE for L_O_TMPFILE.
 */
static int
fs_oflags_dotl(uint32_t l_mode, int *aflags, enum l9p_omode *ap9)
{
	int flags;
	enum l9p_omode p9;
#define	CLEAR(theirs)	l_mode &= ~(uint32_t)(theirs)
#define	CONVERT(theirs, ours) \
	do { \
		if (l_mode & (theirs)) { \
			CLEAR(theirs); \
			flags |= ours; \
		} \
	} while (0)

	/*
	 * Linux O_RDONLY, O_WRONLY, O_RDWR (0,1,2) match BSD/MacOS.
	 */
	flags = l_mode & O_ACCMODE;
	if (flags == 3)
		return (EINVAL);
	CLEAR(O_ACCMODE);

	if ((l_mode & (L9P_L_O_PATH | L9P_L_O_NOFOLLOW)) ==
		    (L9P_L_O_PATH | L9P_L_O_NOFOLLOW)) {
		CLEAR(L9P_L_O_PATH | L9P_L_O_NOFOLLOW);
		p9 = L9P_OEXEC;
	} else {
		/*
		 * Slightly dirty, but same dirt, really, as
		 * setting flags from l_mode & O_ACCMODE.
		 */
		p9 = (enum l9p_omode)flags;	/* slightly dirty */
	}

	/* turn L_O_TMPFILE into L9P_ORCLOSE in *p9? */
	if (l_mode & L9P_L_O_TRUNC)
		p9 |= L9P_OTRUNC;	/* but don't CLEAR yet */

	flags |= O_NOCTTY | O_NOFOLLOW;

	/*
	 * L_O_CREAT seems to be noise, since we get separate open
	 * and create.  But it is actually set sometimes.  We just
	 * throw it out here; create ops must set it themselves and
	 * open ops have no permissions bits and hence cannot create.
	 *
	 * L_O_EXCL does make sense on create ops, i.e., we can
	 * take a create op with or without L_O_EXCL.  We pass that
	 * through.
	 */
	CLEAR(L9P_L_O_CREAT);
	CONVERT(L9P_L_O_EXCL, O_EXCL);
	CONVERT(L9P_L_O_TRUNC, O_TRUNC);
	CONVERT(L9P_L_O_DIRECTORY, O_DIRECTORY);
	CONVERT(L9P_L_O_APPEND, O_APPEND);
	CONVERT(L9P_L_O_NONBLOCK, O_NONBLOCK);

	/*
	 * Discard these as useless noise at our (server) end.
	 * (NOATIME might be useful but we can only set it on a
	 * per-mount basis.)
	 */
	CLEAR(L9P_L_O_CLOEXEC);
	CLEAR(L9P_L_O_DIRECT);
	CLEAR(L9P_L_O_DSYNC);
	CLEAR(L9P_L_O_FASYNC);
	CLEAR(L9P_L_O_LARGEFILE);
	CLEAR(L9P_L_O_NOATIME);
	CLEAR(L9P_L_O_NOCTTY);
	CLEAR(L9P_L_O_NOFOLLOW);
	CLEAR(L9P_L_O_SYNC);

	if (l_mode != 0) {
		L9P_LOG(L9P_INFO,
		    "fs_oflags_dotl: untranslated bits: %#x",
		    (unsigned)l_mode);
		return (EINVAL);
	}

	*aflags = flags;
	*ap9 = p9;
	return (0);
#undef CLEAR
#undef CONVERT
}

/*
 * Build full name of file by appending given name to directory name.
 */
static int
fs_buildname(struct l9p_fid *dir, char *name, char *buf, size_t size)
{
	struct fs_fid *dirf = dir->lo_aux;
	size_t dlen, nlen1;

	assert(dirf != NULL);
	dlen = strlen(dirf->ff_name);
	nlen1 = strlen(name) + 1;	/* +1 for '\0' */
	if (dlen + 1 + nlen1 > size)
		return (ENAMETOOLONG);
	memcpy(buf, dirf->ff_name, dlen);
	buf[dlen] = '/';
	memcpy(buf + dlen + 1, name, nlen1);
	return (0);
}

/*
 * Like fs_buildname() but for adding a file name to a buffer
 * already holding a directory name.  Essentially does
 *     strcat(dbuf, "/");
 *     strcat(dbuf, fname);
 * but with size checking and an ENAMETOOLONG error as needed.
 *
 * (Think of the function name as "directory plus-equals file".)
 */
static int
fs_dpf(char *dbuf, char *fname, size_t size)
{
	size_t dlen, nlen1;

	dlen = strlen(dbuf);
	nlen1 = strlen(fname) + 1;
	if (dlen + 1 + nlen1 > size)
		return (ENAMETOOLONG);
	dbuf[dlen] = '/';
	memcpy(dbuf + dlen + 1, fname, nlen1);
	return (0);
}

/*
 * Prepare to create a new directory entry (open with O_CREAT,
 * mkdir, etc -- any operation that creates a new inode),
 * operating in parent data <dir>, based on authinfo <ai> and
 * effective gid <egid>.
 *
 * The new entity should be owned by user/group <*nuid, *ngid>,
 * if it's really a new entity.
 *
 * Returns an error number if the entry should not be created
 * (e.g., read-only file system or no permission to write in
 * parent directory).  Always sets *nuid and *ngid on success:
 * in the worst case, when there is no available ID, this will
 * use the parent directory's IDs.  Fills in <*st> on success.
 */
static int
fs_nde(struct fs_softc *sc, struct l9p_fid *dir, gid_t egid,
    struct stat *st, uid_t *nuid, gid_t *ngid)
{
	struct fs_fid *dirf;
	struct fs_authinfo *ai;

	if (sc->fs_readonly)
		return (EROFS);
	dirf = dir->lo_aux;
	assert(dirf != NULL);
	if (lstat(dirf->ff_name, st) != 0)
		return (errno);
	if (!S_ISDIR(st->st_mode))
		return (ENOTDIR);
	dirf = dir->lo_aux;
	ai = dirf->ff_ai;
	if (!check_access(st, ai, egid, L9P_OWRITE))
		return (EPERM);

	*nuid = ai->ai_uid != (uid_t)-1 ? ai->ai_uid : st->st_uid;
	*ngid = egid != (gid_t)-1 ? egid :
	    ai->ai_ngids > 0 ?  ai->ai_gids[0] : st->st_gid;
	return (0);
}

/*
 * Check for permission to remove or rename a directory entry.
 * (In other words, check for write permission on the directory.)
 *
 * For full two-fid rename requests, we effectively remove from one
 * directory and add to another.  Used twice, this function suffices
 * for both directories.
 *
 * This function also works for making links, where we will make
 * a new directory entry (rather than renaming one) but are not
 * actually making a new inode and hence do not need uid+gid.
 */
static int
fs_rde(struct fs_softc *sc, struct l9p_fid *dir, struct stat *st)
{
	struct fs_fid *ff;
	struct fs_authinfo *ai;

	if (sc->fs_readonly)
		return (EROFS);

	ff = dir->lo_aux;
	assert(ff != NULL);

	if (lstat(ff->ff_name, st) != 0)
		return (errno);

	if (!S_ISDIR(st->st_mode))
		return (ENOTDIR);

	ai = ff->ff_ai;
	if (!check_access(st, ai, (gid_t)-1, L9P_OWRITE))
		return (EPERM);

	return (0);
}

/*
 * Much like fs_rde but for a file fid.  We need to find the file's
 * containing directory.
 *
 * The file itself might be the file system root, in which case it
 * effectively has no containing directory and cannot be removed or
 * renamed.
 */
static int
fs_rdf(struct fs_softc *sc, struct l9p_fid *file, char *pathbuf,
    size_t bufsize, struct stat *st)
{
	struct fs_fid *ff;
	struct fs_authinfo *ai;
	char *path;

	if (sc->fs_readonly)
		return (EROFS);

	ff = file->lo_aux;
	assert(ff != NULL);
	path = ff->ff_name;
	if (strcmp(path, sc->fs_rootpath) == 0)
		return (EINVAL);
	path = r_dirname(path, pathbuf, bufsize);
	if (path == NULL)
		return (ENAMETOOLONG);

	if (lstat(path, st) != 0)
		return (errno);

	if (!S_ISDIR(st->st_mode))
		return (ENOTDIR);

	ai = ff->ff_ai;
	if (!check_access(st, ai, (gid_t)-1, L9P_OWRITE))
		return (EPERM);

	return (0);
}

/*
 * Allocate new open-file data structure to attach to a fid.
 *
 * The new file's authinfo is the same as the old one's, and
 * we gain a reference.
 */
static struct fs_fid *
open_fid(const char *path, struct fs_authinfo *ai)
{
	struct fs_fid *ret;

	ret = l9p_calloc(1, sizeof(*ret));
	ret->ff_fd = -1;
	ret->ff_name = strdup(path);
	if (ret->ff_name == NULL) {
		free(ret);
		return (NULL);
	}
	ai->ai_refcnt++;
	ret->ff_ai = ai;
	return (ret);
}

static void
dostat(struct l9p_stat *s, char *name, struct stat *buf, bool dotu)
{
	struct passwd *user;
	struct group *group;

	memset(s, 0, sizeof(struct l9p_stat));

	generate_qid(buf, &s->qid);

	s->type = 0;
	s->dev = 0;
	s->mode = buf->st_mode & 0777;

	if (S_ISDIR(buf->st_mode))
		s->mode |= L9P_DMDIR;

	if (S_ISLNK(buf->st_mode) && dotu)
		s->mode |= L9P_DMSYMLINK;

	if (S_ISCHR(buf->st_mode) || S_ISBLK(buf->st_mode))
		s->mode |= L9P_DMDEVICE;

	if (S_ISSOCK(buf->st_mode))
		s->mode |= L9P_DMSOCKET;

	if (S_ISFIFO(buf->st_mode))
		s->mode |= L9P_DMNAMEDPIPE;

	s->atime = (uint32_t)buf->st_atime;
	s->mtime = (uint32_t)buf->st_mtime;
	s->length = (uint64_t)buf->st_size;

	s->name = r_basename(name, NULL, 0);

	if (!dotu) {
		struct r_pgdata udata, gdata;

		user = r_getpwuid(buf->st_uid, &udata);
		group = r_getgrgid(buf->st_gid, &gdata);
		s->uid = user != NULL ? strdup(user->pw_name) : NULL;
		s->gid = group != NULL ? strdup(group->gr_name) : NULL;
		s->muid = user != NULL ? strdup(user->pw_name) : NULL;
		r_pgfree(&udata);
		r_pgfree(&gdata);
	} else {
		/*
		 * When using 9P2000.u, we don't need to bother about
		 * providing user and group names in textual form.
		 *
		 * NB: if the asprintf()s fail, s->extension should
		 * be unset so we can ignore these.
		 */
		s->n_uid = buf->st_uid;
		s->n_gid = buf->st_gid;
		s->n_muid = buf->st_uid;

		if (S_ISLNK(buf->st_mode)) {
			char target[MAXPATHLEN];
			ssize_t ret = readlink(name, target, MAXPATHLEN);

			if (ret < 0) {
				s->extension = NULL;
				return;
			}

			s->extension = strndup(target, (size_t)ret);
		}

		if (S_ISBLK(buf->st_mode)) {
			asprintf(&s->extension, "b %d %d", major(buf->st_rdev),
			    minor(buf->st_rdev));
		}

		if (S_ISCHR(buf->st_mode)) {
			asprintf(&s->extension, "c %d %d", major(buf->st_rdev),
			    minor(buf->st_rdev));
		}
	}
}

static void dostatfs(struct l9p_statfs *out, struct statfs *in, long namelen)
{

	out->type = L9P_FSTYPE;
	out->bsize = in->f_bsize;
	out->blocks = in->f_blocks;
	out->bfree = in->f_bfree;
	out->bavail = in->f_bavail;
	out->files = in->f_files;
	out->ffree = in->f_ffree;
	out->fsid = ((uint64_t)in->f_fsid.val[0] << 32) | (uint64_t)in->f_fsid.val[1];
	out->namelen = (uint32_t)namelen;
}

static void
generate_qid(struct stat *buf, struct l9p_qid *qid)
{
	qid->path = buf->st_ino;
	qid->version = 0;

	if (S_ISREG(buf->st_mode))
		qid->type |= L9P_QTFILE;

	if (S_ISDIR(buf->st_mode))
		qid->type |= L9P_QTDIR;

	if (S_ISLNK(buf->st_mode))
		qid->type |= L9P_QTSYMLINK;
}

/*
 * Verify that the user whose authinfo is in <ai> and effective
 * group ID is <egid> ((gid_t)-1 means no egid supplied) has
 * permission to do something to a file, based on stat st_mode
 * data and 9p-style requested open-mode (omode).
 */
static bool
check_access(struct stat *st, struct fs_authinfo *ai, gid_t egid,
    enum l9p_omode omode)
{
	int i, mask;

	if (ai->ai_uid == 0)
		return (true);

	/*
	 * This is a bit dirty (using the well known mode bits instead
	 * of the S_I[RWX]{USR,GRP,OTH} macros), but lets us be very
	 * efficient about it.
	 *
	 * Note that L9P_OTRUNC requires write access to the file.
	 * (L9P_ORCLOSE requires write access to the parent directory,
	 * but the caller must do that check.)
	 */
	switch (omode & L9P_OACCMODE) {
	case L9P_ORDWR:
		mask = 0600;
		break;
	case L9P_OREAD:
	default:
		mask = 0400;
		break;
	case L9P_OWRITE:
		mask = 0200;
		break;
	case L9P_OEXEC:
		mask = 0100;
		break;
	}
	if (omode & L9P_OTRUNC)
		mask |= 0400;

	/*
	 * Normal Unix semantics are: apply user permissions first
	 * and if these fail, reject the request entirely.  Current
	 * lib9p semantics go on to allow group or other as well.
	 *
	 * Also, we check "other" before "group" because group check
	 * is more expensive.
	 */
	if (st->st_uid == ai->ai_uid) {
		if ((st->st_mode & mask) == mask)
			return (true);
	}

	/* Check for "other" access */
	mask >>= 6;
	if ((st->st_mode & mask) == mask)
		return (true);

	/*
	 * Check for group access.  If we don't have it we're done,
	 * if we do have it, see if we're listed in the group,
	 * including the extra gid if any.
	 *
	 * If the passed-in egid is (gid_t)-1 it should not
	 * match st->st_gid, which should never be (gid_t)-1,
	 * so we should not need any special test here.
	 * (We use this with ai->ai_uid as well, if the attach
	 * was done with NONUNAME and no valid user name.)
	 */
	mask <<= 3;
	if ((st->st_mode & mask) == 0)
		return (false);

	if (st->st_gid == egid)
		return (true);
	for (i = 0; i < ai->ai_ngids; i++)
		if (st->st_gid == ai->ai_gids[i])
			return (true);

	return (false);
}

static int
fs_attach(void *softc, struct l9p_request *req)
{
	struct fs_authinfo *ai;
	struct fs_softc *sc = (struct fs_softc *)softc;
	struct fs_fid *file;
	struct passwd *pwd;
	struct stat st;
	uint32_t n_uname;
	gid_t *gids;
	uid_t uid;
	int error;
	int ngroups;

	assert(req->lr_fid != NULL);

	n_uname = req->lr_req.tattach.n_uname;
	if (n_uname != L9P_NONUNAME) {
		uid = (uid_t)n_uname;
		pwd = getpwuid(uid);
		if (pwd == NULL)
			L9P_LOG(L9P_DEBUG,
			    "Tattach: uid %lu: no such user",
			    (u_long)uid);
	} else {
		uid = (uid_t)-1;
		pwd = getpwnam(req->lr_req.tattach.uname);
		if (pwd == NULL)
			L9P_LOG(L9P_DEBUG,
			    "Tattach: %s: no such user",
			    req->lr_req.tattach.uname);
	}

	if (pwd == NULL && req->lr_conn->lc_version != L9P_2000L)
		return (EPERM);

	error = 0;
	if (lstat(sc->fs_rootpath, &st) != 0)
		error = errno;
	else if (!S_ISDIR(st.st_mode))
		error = ENOTDIR;
	if (error) {
		L9P_LOG(L9P_DEBUG,
		    "Tattach: denying access to \"%s\": %s",
		    sc->fs_rootpath, strerror(error));
		/*
		 * Pass ENOENT and ENOTDIR through for diagnosis;
		 * others become EPERM.  This should not leak too
		 * much security.
		 */
		return (error == ENOENT || error == ENOTDIR ? error : EPERM);
	}

	if (pwd != NULL) {
		gids = l9p_getgrlist(pwd->pw_name, pwd->pw_gid, &ngroups);
		if (gids == NULL)
			return (ENOMEM);
	} else {
		gids = NULL;
		ngroups = 0;
	}
	ai = malloc(sizeof(*ai) + (size_t)ngroups * sizeof(gid_t));
	if (ai == NULL) {
		free(gids);
		return (ENOMEM);
	}
	ai->ai_refcnt = 0;
	ai->ai_uid = pwd != NULL ? pwd->pw_uid : uid;
	memcpy(ai->ai_gids, gids, (size_t)ngroups * sizeof(gid_t));
	free(gids);

	file = open_fid(sc->fs_rootpath, ai);
	if (file == NULL) {
		free(ai);
		return (ENOMEM);
	}

	req->lr_fid->lo_aux = file;
	generate_qid(&st, &req->lr_resp.rattach.qid);
	return (0);
}

static int
fs_clunk(void *softc __unused, struct l9p_fid *fid)
{
	struct fs_fid *file;

	file = fid->lo_aux;
	assert(file != NULL);

	if (file->ff_dir) {
		closedir(file->ff_dir);
		file->ff_dir = NULL;
	} else if (file->ff_fd != -1) {
		close(file->ff_fd);
		file->ff_fd = -1;
	}

	return (0);
}

/*
 * Create ops.
 *
 * We are to create a new file under some existing path,
 * where the new file's name is in the Tcreate request and the
 * existing path is due to a fid-based file (req->lr_fid).
 *
 * One op (create regular file) sets file->fd, the rest do not.
 */
static int
fs_create(void *softc, struct l9p_request *req)
{
	struct l9p_fid *dir;
	struct stat st;
	uint32_t dmperm;
	mode_t perm;
	char *name;
	int error;

	dir = req->lr_fid;
	name = req->lr_req.tcreate.name;
	dmperm = req->lr_req.tcreate.perm;
	perm = (mode_t)(dmperm & 0777);

	if (dmperm & L9P_DMDIR)
		error = fs_imkdir(softc, dir, name, true,
		    perm, (gid_t)-1, &st);
	else if (dmperm & L9P_DMSYMLINK)
		error = fs_isymlink(softc, dir, name,
		    req->lr_req.tcreate.extension, (gid_t)-1, &st);
	else if (dmperm & L9P_DMNAMEDPIPE)
		error = fs_imkfifo(softc, dir, name, true,
		    perm, (gid_t)-1, &st);
	else if (dmperm & L9P_DMSOCKET)
		error = fs_imksocket(softc, dir, name, true,
		    perm, (gid_t)-1, &st);
	else if (dmperm & L9P_DMDEVICE) {
		unsigned int major, minor;
		char type;
		dev_t dev;

		/*
		 * ??? Should this be testing < 3?  For now, allow a single
		 * integer mode with minor==0 implied.
		 */
		minor = 0;
		if (sscanf(req->lr_req.tcreate.extension, "%c %u %u",
		    &type, &major, &minor) < 2) {
			return (EINVAL);
		}

		switch (type) {
		case 'b':
			perm |= S_IFBLK;
			break;
		case 'c':
			perm |= S_IFCHR;
			break;
		default:
			return (EINVAL);
		}
		dev = makedev(major, minor);
		error = fs_imknod(softc, dir, name, true, perm, dev,
		    (gid_t)-1, &st);
	} else {
		enum l9p_omode p9;
		int flags;

		p9 = req->lr_req.tcreate.mode;
		error = fs_oflags_dotu(p9, &flags);
		if (error)
			return (error);
		error = fs_icreate(softc, dir, name, flags,
		    true, perm, (gid_t)-1, &st);
		req->lr_resp.rcreate.iounit = req->lr_conn->lc_max_io_size;
	}

	if (error == 0)
		generate_qid(&st, &req->lr_resp.rcreate.qid);
	return (error);
}

/*
 * https://swtch.com/plan9port/man/man9/open.html and
 * http://plan9.bell-labs.com/magic/man2html/5/open
 * say that permissions are actually
 *     perm & (~0666 | (dir.perm & 0666))
 * for files, and
 *     perm & (~0777 | (dir.perm & 0777))
 * for directories.  That is, the parent directory may
 * take away permissions granted by the operation.
 *
 * This seems a bit restrictive; probably
 * there should be a control knob for this.
 */
static inline mode_t
fs_p9perm(mode_t perm, mode_t dir_perm, bool isdir)
{

	if (isdir)
		perm &= ~0777 | (dir_perm & 0777);
	else
		perm &= ~0666 | (dir_perm & 0666);
	return (perm);
}

/*
 * Internal form of create (plain file).
 *
 * Our caller takes care of splitting off all the special
 * types of create (mknod, etc), so this is purely for files.
 * We receive the fs_softc <softc>, the directory fid <dir>
 * in which the new file is to be created, the name of the
 * new file, a flag <isp9> indicating whether to do plan9 style
 * permissions or Linux style permissions, the permissions <perm>,
 * an effective group id <egid>, and a pointer to a stat structure
 * <st> to fill in describing the final result on success.
 *
 * On successful create, the fid switches to the newly created
 * file, which is now open; its associated file-name changes too.
 *
 * Note that the original (dir) fid is never currently open,
 * so there is nothing to close.
 */
static int
fs_icreate(void *softc, struct l9p_fid *dir, char *name, int flags,
    bool isp9, mode_t perm, gid_t egid, struct stat *st)
{
	struct fs_fid *file;
	gid_t gid;
	uid_t uid;
	char newname[MAXPATHLEN];
	int error, fd;

	/*
	 * Build full path name from directory + file name.  We'll
	 * check permissions on the parent directory, then race to
	 * create the file before anything bad happens like symlinks.
	 *
	 * (To close this race we need to use openat(), which is
	 * left for a later version of this code.)
	 */
	error = fs_buildname(dir, name, newname, sizeof(newname));
	if (error)
		return (error);

	/* In case of success, we will need a new file->ff_name. */
	name = strdup(newname);
	if (name == NULL)
		return (ENOMEM);

	/* Check create permission and compute new file ownership. */
	error = fs_nde(softc, dir, egid, st, &uid, &gid);
	if (error) {
		free(name);
		return (error);
	}

	/* Adjust new-file permissions for Plan9 protocol. */
	if (isp9)
		perm = fs_p9perm(perm, st->st_mode, false);

	/* Create is always exclusive so O_TRUNC is irrelevant. */
	fd = open(newname, flags | O_CREAT | O_EXCL, perm);
	if (fd < 0) {
		error = errno;
		free(name);
		return (error);
	}

	/* Fix permissions and owner. */
	if (fchmod(fd, perm) != 0 ||
	    fchown(fd, uid, gid) != 0 ||
	    fstat(fd, st) != 0) {
		error = errno;
		(void) close(fd);
		/* unlink(newname); ? */
		free(name);
		return (error);
	}

	/* It *was* a directory; now it's a file, and it's open. */
	file = dir->lo_aux;
	free(file->ff_name);
	file->ff_name = name;
	file->ff_fd = fd;
	return (0);
}

/*
 * Internal form of open: stat file and verify permissions (from p9
 * argument), then open the file-or-directory, leaving the internal
 * fs_fid fields set up.  If we cannot open the file, return a
 * suitable error number, and leave everything unchanged.
 *
 * To mitigate the race between permissions testing and the actual
 * open, we can stat the file twice (once with lstat() before open,
 * then with fstat() after).  We assume O_NOFOLLOW is set in flags,
 * so if some other race-winner substitutes in a symlink we won't
 * open it here.  (However, embedded symlinks, if they occur, are
 * still an issue.  Ideally we would like to have an O_NEVERFOLLOW
 * that fails on embedded symlinks, and a way to pass this to
 * lstat() as well.)
 *
 * When we use opendir() we cannot pass O_NOFOLLOW, so we must rely
 * on substitution-detection via fstat().  To simplify the code we
 * just always re-check.
 *
 * (For a proper fix in the future, we can require openat(), keep
 * each parent directory open during walk etc, and allow only final
 * name components with O_NOFOLLOW.)
 *
 * On successful return, st has been filled in.
 */
static int
fs_iopen(void *softc, struct l9p_fid *fid, int flags, enum l9p_omode p9,
    gid_t egid, struct stat *st)
{
	struct fs_softc *sc = softc;
	struct fs_fid *file;
	struct stat first;
	char *name;
	int fd;
	DIR *dirp;

	/* Forbid write ops on read-only file system. */
	if (sc->fs_readonly) {
		if ((flags & O_TRUNC) != 0)
			return (EROFS);
		if ((flags & O_ACCMODE) != O_RDONLY)
			return (EROFS);
		if (p9 & L9P_ORCLOSE)
			return (EROFS);
	}

	file = fid->lo_aux;
	assert(file != NULL);
	name = file->ff_name;

	if (lstat(name, &first) != 0)
		return (errno);
	if (S_ISLNK(first.st_mode))
		return (EPERM);
	if (!check_access(&first, file->ff_ai, egid, p9))
		return (EPERM);

	if (S_ISDIR(first.st_mode)) {
		/* Forbid write or truncate on directory. */
		if ((flags & O_ACCMODE) != O_RDONLY || (flags & O_TRUNC))
			return (EPERM);
		dirp = opendir(name);
		if (dirp == NULL)
			return (EPERM);
		fd = dirfd(dirp);
	} else {
		dirp = NULL;
		fd = open(name, flags);
		if (fd < 0)
			return (EPERM);
	}

	/*
	 * We have a valid fd, and maybe non-null dirp.  Re-check
	 * the file, and fail if st_dev or st_ino changed.
	 */
	if (fstat(fd, st) != 0 ||
	    first.st_dev != st->st_dev ||
	    first.st_ino != st->st_ino) {
		if (dirp != NULL)
			(void) closedir(dirp);
		else
			(void) close(fd);
		return (EPERM);
	}
	if (dirp != NULL)
		file->ff_dir = dirp;
	else
		file->ff_fd = fd;
	return (0);
}

/*
 * Internal form of mkdir (common code for all forms).
 * We receive the fs_softc <softc>, the directory fid <dir>
 * in which the new entry is to be created, the name of the
 * new entry, a flag <isp9> indicating whether to do plan9 style
 * permissions or Linux style permissions, the permissions <perm>,
 * an effective group id <egid>, and a pointer to a stat structure
 * <st> to fill in describing the final result on success.
 *
 * See also fs_icreate() above.
 */
static int
fs_imkdir(void *softc, struct l9p_fid *dir, char *name,
    bool isp9, mode_t perm, gid_t egid, struct stat *st)
{
	gid_t gid;
	uid_t uid;
	char newname[MAXPATHLEN];
	int error, fd;

	error = fs_buildname(dir, name, newname, sizeof(newname));
	if (error)
		return (error);

	error = fs_nde(softc, dir, egid, st, &uid, &gid);
	if (error)
		return (error);

	if (isp9)
		perm = fs_p9perm(perm, st->st_mode, true);

	if (mkdir(newname, perm) != 0)
		return (errno);

	fd = open(newname, O_DIRECTORY | O_RDONLY);
	if (fd < 0 ||
	    fchown(fd, uid, gid) != 0 ||
	    fchmod(fd, perm) != 0 ||
	    fstat(fd, st) != 0) {
		error = errno;
		/* rmdir(newname) ? */
	}
	if (fd >= 0)
		(void) close(fd);

	return (error);
}

/*
 * Internal form of mknod (special device).
 *
 * The device type (S_IFBLK, S_IFCHR) is included in the <mode> parameter.
 */
static int
fs_imknod(void *softc, struct l9p_fid *dir, char *name,
    bool isp9, mode_t mode, dev_t dev, gid_t egid, struct stat *st)
{
	mode_t perm;
	gid_t gid;
	uid_t uid;
	char newname[MAXPATHLEN];
	int error;

	error = fs_buildname(dir, name, newname, sizeof(newname));
	if (error)
		return (error);

	error = fs_nde(softc, dir, egid, st, &uid, &gid);
	if (error)
		return (error);

	if (isp9) {
		perm = fs_p9perm(mode & 0777, st->st_mode, false);
		mode = (mode & ~0777) | perm;
	} else {
		perm = mode & 0777;
	}

	if (mknod(newname, mode, dev) != 0)
		return (errno);

	/* We cannot open the new name; race to use l* syscalls. */
	if (lchown(newname, uid, gid) != 0 ||
	    lchmod(newname, perm) != 0 ||
	    lstat(newname, st) != 0)
		error = errno;
	else if ((st->st_mode & S_IFMT) != (mode & S_IFMT))
		error = EPERM;		/* ??? lost a race anyway */

	/* if (error) unlink(newname) ? */

	return (error);
}

/*
 * Internal form of mkfifo.
 */
static int
fs_imkfifo(void *softc, struct l9p_fid *dir, char *name,
    bool isp9, mode_t perm, gid_t egid, struct stat *st)
{
	gid_t gid;
	uid_t uid;
	char newname[MAXPATHLEN];
	int error;

	error = fs_buildname(dir, name, newname, sizeof(newname));
	if (error)
		return (error);

	error = fs_nde(softc, dir, egid, st, &uid, &gid);
	if (error)
		return (error);

	if (isp9)
		perm = fs_p9perm(perm, st->st_mode, false);

	if (mkfifo(newname, perm) != 0)
		return (errno);

	/* We cannot open the new name; race to use l* syscalls. */
	if (lchown(newname, uid, gid) != 0 ||
	    lchmod(newname, perm) != 0 ||
	    lstat(newname, st) != 0)
		error = errno;
	else if (!S_ISFIFO(st->st_mode))
		error = EPERM;		/* ??? lost a race anyway */

	/* if (error) unlink(newname) ? */

	return (error);
}

/*
 * Internal form of mksocket.
 *
 * This is a bit different because of the horrible socket naming
 * system (bind() with sockaddr_un sun_path).
 */
static int
fs_imksocket(void *softc, struct l9p_fid *dir, char *name,
    bool isp9, mode_t perm, gid_t egid, struct stat *st)
{
	struct sockaddr_un sun;
	char *path;
	char newname[MAXPATHLEN];
	gid_t gid;
	uid_t uid;
	int error = 0, s, fd;

	error = fs_buildname(dir, name, newname, sizeof(newname));
	if (error)
		return (error);

	error = fs_nde(softc, dir, egid, st, &uid, &gid);
	if (error)
		return (error);

	if (isp9)
		perm = fs_p9perm(perm, st->st_mode, false);

	s = socket(AF_UNIX, SOCK_STREAM, 0);
	if (s < 0)
		return (errno);

	path = newname;
	fd = -1;
#ifdef HAVE_BINDAT
	/* Try bindat() if needed. */
	if (strlen(path) >= sizeof(sun.sun_path)) {
		struct fs_fid *file = dir->lo_aux;

		fd = open(file->ff_name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
		if (fd >= 0)
			path = name;
	}
#endif

	/*
	 * Can only create the socket if the path will fit.
	 * Even if we are using bindat() there are limits
	 * (the API for AF_UNIX sockets is ... not good).
	 *
	 * Note: in theory we can fill sun_path to the end
	 * (omitting a terminating '\0') but in at least one
	 * Unix-like system, this was known to behave oddly,
	 * so we test for ">=" rather than just ">".
	 */
	if (strlen(path) >= sizeof(sun.sun_path)) {
		error = ENAMETOOLONG;
		goto out;
	}
	sun.sun_family = AF_UNIX;
	sun.sun_len = sizeof(struct sockaddr_un);
	strncpy(sun.sun_path, path, sizeof(sun.sun_path));

#ifdef HAVE_BINDAT
	if (fd >= 0) {
		if (bindat(fd, s, (struct sockaddr *)&sun, sun.sun_len) < 0)
			error = errno;
		goto out;	/* done now, for good or ill */
	}
#endif

	if (bind(s, (struct sockaddr *)&sun, sun.sun_len) < 0)
		error = errno;
out:

	if (error == 0) {
		/*
		 * We believe we created the socket-inode.  Fix
		 * permissions etc.  Note that we cannot use
		 * fstat() on the socket descriptor: it succeeds,
		 * but we get bogus data!
		 */
		if (lchown(newname, uid, gid) != 0 ||
		    lchmod(newname, perm) != 0 ||
		    lstat(newname, st) != 0)
			error = errno;
		else if (!S_ISSOCK(st->st_mode))
			error = EPERM;		/* ??? lost a race anyway */

		/* if (error) unlink(newname) ? */
	}

	/*
	 * It's not clear which error should override, although
	 * ideally we should never see either close() call fail.
	 * In any case we do want to try to close both fd and s,
	 * always.  Let's set error only if it is not already set,
	 * so that all exit paths can use the same code.
	 */
	if (fd >= 0 && close(fd) != 0)
		if (error == 0)
			error = errno;
	if (close(s) != 0)
		if (error == 0)
			error = errno;

	return (error);
}

/*
 * Internal form of symlink.
 *
 * Note that symlinks are presumed to carry no permission bits.
 * They do have owners, however (who may be charged for quotas).
 */
static int
fs_isymlink(void *softc, struct l9p_fid *dir, char *name,
    char *symtgt, gid_t egid, struct stat *st)
{
	gid_t gid;
	uid_t uid;
	char newname[MAXPATHLEN];
	int error;

	error = fs_buildname(dir, name, newname, sizeof(newname));
	if (error)
		return (error);

	error = fs_nde(softc, dir, egid, st, &uid, &gid);
	if (error)
		return (error);

	if (symlink(symtgt, newname) != 0)
		return (errno);

	/* We cannot open the new name; race to use l* syscalls. */
	if (lchown(newname, uid, gid) != 0 ||
	    lstat(newname, st) != 0)
		error = errno;
	else if (!S_ISLNK(st->st_mode))
		error = EPERM;		/* ??? lost a race anyway */

	/* if (error) unlink(newname) ? */

	return (error);
}

static int
fs_open(void *softc, struct l9p_request *req)
{
	struct l9p_fid *fid = req->lr_fid;
	struct stat st;
	enum l9p_omode p9;
	int error, flags;

	p9 = req->lr_req.topen.mode;
	error = fs_oflags_dotu(p9, &flags);
	if (error)
		return (error);

	error = fs_iopen(softc, fid, flags, p9, (gid_t)-1, &st);
	if (error)
		return (error);

	generate_qid(&st, &req->lr_resp.ropen.qid);
	req->lr_resp.ropen.iounit = req->lr_conn->lc_max_io_size;
	return (0);
}

/*
 * Helper for directory read.  We want to run an lstat on each
 * file name within the directory.  This is a lot faster if we
 * have lstatat (or fstatat with AT_SYMLINK_NOFOLLOW), but not
 * all systems do, so hide the ifdef-ed code in an inline function.
 */
static inline int
fs_lstatat(struct fs_fid *file, char *name, struct stat *st)
{
#ifdef HAVE_FSTATAT
	return (fstatat(dirfd(file->ff_dir), name, st, AT_SYMLINK_NOFOLLOW));
#else
	char buf[MAXPATHLEN];

	if (strlcpy(buf, file->ff_name, sizeof(buf)) >= sizeof(buf) ||
	    strlcat(buf, name, sizeof(buf)) >= sizeof(buf))
		return (-1);
	return (lstat(name, st));
#endif
}

static int
fs_read(void *softc __unused, struct l9p_request *req)
{
	struct l9p_stat l9stat;
	struct fs_fid *file;
	bool dotu = req->lr_conn->lc_version >= L9P_2000U;
	ssize_t ret;

	file = req->lr_fid->lo_aux;
	assert(file != NULL);

	if (file->ff_dir != NULL) {
		struct dirent *d;
		struct stat st;
		struct l9p_message msg;
		long o;

		/*
		 * Must use telldir before readdir since seekdir
		 * takes cookie values.  Unfortunately this wastes
		 * a lot of time (and memory) building unneeded
		 * cookies that can only be flushed by closing
		 * the directory.
		 *
		 * NB: FreeBSD libc seekdir has SINGLEUSE defined,
		 * so in fact, we can discard the cookies by
		 * calling seekdir on them.  This clears up wasted
		 * memory at the cost of even more wasted time...
		 *
		 * XXX: readdir/telldir/seekdir not thread safe
		 */
		l9p_init_msg(&msg, req, L9P_PACK);
		for (;;) {
			o = telldir(file->ff_dir);
			d = readdir(file->ff_dir);
			if (d == NULL)
				break;
			if (fs_lstatat(file, d->d_name, &st))
				continue;
			dostat(&l9stat, d->d_name, &st, dotu);
			if (l9p_pack_stat(&msg, req, &l9stat) != 0) {
				seekdir(file->ff_dir, o);
				break;
			}
#if defined(__FreeBSD__)
			seekdir(file->ff_dir, o);
			(void) readdir(file->ff_dir);
#endif
		}
	} else {
		size_t niov = l9p_truncate_iov(req->lr_data_iov,
                    req->lr_data_niov, req->lr_req.io.count);

#if defined(__FreeBSD__)
		ret = preadv(file->ff_fd, req->lr_data_iov, niov,
		    req->lr_req.io.offset);
#else
		/* XXX: not thread safe, should really use aio_listio. */
		if (lseek(file->ff_fd, (off_t)req->lr_req.io.offset, SEEK_SET) < 0)
			return (errno);

		ret = (uint32_t)readv(file->ff_fd, req->lr_data_iov, (int)niov);
#endif

		if (ret < 0)
			return (errno);

		req->lr_resp.io.count = (uint32_t)ret;
	}

	return (0);
}

static int
fs_remove(void *softc, struct l9p_fid *fid)
{
	struct fs_fid *file;
	struct stat st;
	char namebuf[MAXPATHLEN];
	int error;

	error = fs_rdf(softc, fid, namebuf, sizeof(namebuf), &st);
	if (error)
		return (error);

	file = fid->lo_aux;
	if (S_ISDIR(st.st_mode)) {
		if (rmdir(file->ff_name) != 0)
			error = errno;
	} else {
		if (unlink(file->ff_name) != 0)
			error = errno;
	}

	return (error);
}

static int
fs_stat(void *softc __unused, struct l9p_request *req)
{
	struct fs_fid *file;
	struct stat st;
	bool dotu = req->lr_conn->lc_version >= L9P_2000U;

	file = req->lr_fid->lo_aux;
	assert(file);

	lstat(file->ff_name, &st);
	dostat(&req->lr_resp.rstat.stat, file->ff_name, &st, dotu);

	return (0);
}

static int
fs_walk(void *softc, struct l9p_request *req)
{
	struct fs_authinfo *ai;
	struct fs_softc *sc = softc;
	struct fs_fid *file = req->lr_fid->lo_aux;
	struct fs_fid *newfile;
	struct stat st;
	size_t clen, namelen, need;
	char *comp, *succ, *next, *swtmp;
	bool atroot;
	bool dotdot;
	int i, nwname;
	int error = 0;
	char namebufs[2][MAXPATHLEN];

	/*
	 * https://swtch.com/plan9port/man/man9/walk.html:
	 *
	 *    It is legal for nwname to be zero, in which case newfid
	 *    will represent the same file as fid and the walk will
	 *    usually succeed; this is equivalent to walking to dot.
	 * [Aside: it's not clear if we should test S_ISDIR here.]
	 *    ...
	 *    The name ".." ... represents the parent directory.
	 *    The name "." ... is not used in the protocol.
	 *    ... A walk of the name ".." in the root directory
	 *    of the server is equivalent to a walk with no name
	 *    elements.
	 *
	 * Note that req.twalk.nwname never exceeds L9P_MAX_WELEM,
	 * so it is safe to convert to plain int.
	 *
	 * We are to return an error only if the first walk fails,
	 * else stop at the end of the names or on the first error.
	 * The final fid is based on the last name successfully
	 * walked.
	 *
	 * Note that we *do* get Twalk requests with nwname==0 on files.
	 *
	 * Set up "successful name" buffer pointer with base fid name,
	 * initially.  We'll swap each new success into it as we go.
	 *
	 * Invariant: atroot and stat data correspond to current
	 * (succ) path.
	 */
	succ = namebufs[0];
	next = namebufs[1];
	namelen = strlcpy(succ, file->ff_name, MAXPATHLEN);
	if (namelen >= MAXPATHLEN)
		return (ENAMETOOLONG);
	if (lstat(succ, &st) < 0)
		return (errno);
	ai = file->ff_ai;
	atroot = strcmp(succ, sc->fs_rootpath) == 0;

	nwname = (int)req->lr_req.twalk.nwname;

	for (i = 0; i < nwname; i++) {
		/*
		 * Must have execute permission to search a directory.
		 * Then, look up each component in its directory-so-far.
		 * Check for ".." along the way, handlng specially
		 * as needed.  Forbid "/" in name components.
		 *
		 */
		if (!S_ISDIR(st.st_mode)) {
			error = ENOTDIR;
			goto out;
		}
		if (!check_access(&st, ai, (gid_t)-1, L9P_OEXEC)) {
			L9P_LOG(L9P_DEBUG,
			    "Twalk: denying dir-walk on \"%s\" for uid %u",
			    succ, (unsigned)ai->ai_uid);
			error = EPERM;
			goto out;
		}
		comp = req->lr_req.twalk.wname[i];
		if (strchr(comp, '/') != NULL) {
			error = EINVAL;
			break;
		}

		clen = strlen(comp);
		dotdot = false;

		/*
		 * Build next pathname (into "next").  If "..",
		 * just strip one name component off the success
		 * name so far.  Since we know this name fits, the
		 * stripped down version also fits.  Otherwise,
		 * the name is the base name plus '/' plus the
		 * component name plus terminating '\0'; this may
		 * or may not fit.
		 */
		if (comp[0] == '.') {
			if (clen == 1) {
				error = EINVAL;
				break;
			}
			if (comp[1] == '.' && clen == 2)
				dotdot = true;
		}
		if (dotdot) {
			/*
			 * It's not clear how ".." at root should
			 * be handled when i > 0.  Obeying the man
			 * page exactly, we reset i to 0 and stop,
			 * declaring terminal success.
			 *
			 * Otherwise, we just climbed up one level
			 * so adjust "atroot".
			 */
			if (atroot) {
				i = 0;
				break;
			}
			(void) r_dirname(succ, next, MAXPATHLEN);
			namelen = strlen(next);
			atroot = strcmp(next, sc->fs_rootpath) == 0;
		} else {
			need = namelen + 1 + clen + 1;
			if (need > MAXPATHLEN) {
				error = ENAMETOOLONG;
				break;
			}
			memcpy(next, succ, namelen);
			next[namelen++] = '/';
			memcpy(&next[namelen], comp, clen + 1);
			namelen += clen;
			/*
			 * Since name is never ".", we are necessarily
			 * descending below the root now.
			 */
			atroot = false;
		}

		if (lstat(next, &st) < 0) {
			error = ENOENT;
			break;
		}

		/*
		 * Success: generate qid and swap this
		 * successful name into place.
		 */
		generate_qid(&st, &req->lr_resp.rwalk.wqid[i]);
		swtmp = succ;
		succ = next;
		next = swtmp;
	}

	/*
	 * Fail only if we failed on the first name.
	 * Otherwise we succeeded on something, and "succ"
	 * points to the last successful name in namebufs[].
	 */
	if (error) {
		if (i == 0)
			goto out;
		error = 0;
	}

	newfile = open_fid(succ, ai);
	if (newfile == NULL) {
		error = ENOMEM;
		goto out;
	}
	if (req->lr_newfid == req->lr_fid) {
		/*
		 * Before overwriting fid->lo_aux, free the old value.
		 * Note that this doesn't free the l9p_fid data,
		 * just the fs_fid data.
		 */
		fs_freefid(softc, req->lr_fid);
	}
	req->lr_newfid->lo_aux = newfile;
	req->lr_resp.rwalk.nwqid = (uint16_t)i;
out:
	return (error);
}

static int
fs_write(void *softc, struct l9p_request *req)
{
	struct fs_softc *sc = softc;
	struct fs_fid *file;
	ssize_t ret;

	file = req->lr_fid->lo_aux;
	assert(file != NULL);

	if (sc->fs_readonly)
		return (EROFS);

	size_t niov = l9p_truncate_iov(req->lr_data_iov,
            req->lr_data_niov, req->lr_req.io.count);

#if defined(__FreeBSD__)
	ret = pwritev(file->ff_fd, req->lr_data_iov, niov,
	    req->lr_req.io.offset);
#else
	/* XXX: not thread safe, should really use aio_listio. */
	if (lseek(file->ff_fd, (off_t)req->lr_req.io.offset, SEEK_SET) < 0)
		return (errno);

	ret = writev(file->ff_fd, req->lr_data_iov,
	    (int)niov);
#endif

	if (ret < 0)
		return (errno);

	req->lr_resp.io.count = (uint32_t)ret;
	return (0);
}

static int
fs_wstat(void *softc, struct l9p_request *req)
{
	struct fs_softc *sc = softc;
	struct l9p_stat *l9stat = &req->lr_req.twstat.stat;
	struct l9p_fid *fid;
	struct fs_fid *file;
	int error = 0;

	fid = req->lr_fid;
	file = fid->lo_aux;
	assert(file != NULL);

	/*
	 * XXX:
	 *
	 * stat(9P) sez:
	 *
	 * Either all the changes in wstat request happen, or none of them
	 * does: if the request succeeds, all changes were made; if it fails,
	 * none were.
	 *
	 * Atomicity is clearly missing in current implementation.
	 */

	if (sc->fs_readonly)
		return (EROFS);

	if (l9stat->atime != (uint32_t)~0) {
		/* XXX: not implemented, ignore */
	}

	if (l9stat->mtime != (uint32_t)~0) {
		/* XXX: not implemented, ignore */
	}

	if (l9stat->dev != (uint32_t)~0) {
		error = EPERM;
		goto out;
	}

	if (l9stat->length != (uint64_t)~0) {
		if (file->ff_dir != NULL) {
			error = EINVAL;
			goto out;
		}

		if (truncate(file->ff_name, (off_t)l9stat->length) != 0) {
			error = errno;
			goto out;
		}
	}

	if (req->lr_conn->lc_version >= L9P_2000U) {
		if (lchown(file->ff_name, l9stat->n_uid, l9stat->n_gid) != 0) {
			error = errno;
			goto out;
		}
	}

	if (l9stat->mode != (uint32_t)~0) {
		if (chmod(file->ff_name, l9stat->mode & 0777) != 0) {
			error = errno;
			goto out;
		}
	}

	if (strlen(l9stat->name) > 0) {
		struct stat st;
		char *tmp;
		char newname[MAXPATHLEN];

		error = fs_rdf(sc, fid, newname, sizeof(newname), &st);
		if (error == 0)
			error = fs_dpf(newname, l9stat->name, sizeof(newname));
		if (error)
			goto out;
		tmp = strdup(newname);
		if (tmp == NULL) {
			error = ENOMEM;
			goto out;
		}
		if (rename(file->ff_name, tmp) != 0) {
			error = errno;
			free(tmp);
			goto out;
		}
		/* Successful rename, update file->ff_name. */
		free(file->ff_name);
		file->ff_name = tmp;
	}
out:
	return (error);
}

static int
fs_statfs(void *softc __unused, struct l9p_request *req)
{
	struct fs_fid *file;
	struct stat st;
	struct statfs f;
	long name_max;

	file = req->lr_fid->lo_aux;
	assert(file);

	if (lstat(file->ff_name, &st) != 0)
		return (errno);

	if (!check_access(&st, file->ff_ai, (gid_t)-1, L9P_OREAD))
		return (EPERM);

	if (statfs(file->ff_name, &f) != 0)
		return (errno);

	name_max = pathconf(file->ff_name, _PC_NAME_MAX);
	if (name_max == -1)
		return (errno);

	dostatfs(&req->lr_resp.rstatfs.statfs, &f, name_max);

	return (0);
}

static int
fs_lopen(void *softc, struct l9p_request *req)
{
	struct l9p_fid *fid = req->lr_fid;
	struct stat st;
	enum l9p_omode p9;
	gid_t gid;
	int error, flags;

	error = fs_oflags_dotl(req->lr_req.tlopen.flags, &flags, &p9);
	if (error)
		return (error);

	gid = req->lr_req.tlopen.gid;
	error = fs_iopen(softc, fid, flags, p9, gid, &st);
	if (error)
		return (error);

	generate_qid(&st, &req->lr_resp.rlopen.qid);
	req->lr_resp.rlopen.iounit = req->lr_conn->lc_max_io_size;
	return (0);
}

static int
fs_lcreate(void *softc, struct l9p_request *req)
{
	struct l9p_fid *dir;
	struct stat st;
	enum l9p_omode p9;
	char *name;
	mode_t perm;
	gid_t gid;
	int error, flags;

	dir = req->lr_fid;
	name = req->lr_req.tlcreate.name;

	error = fs_oflags_dotl(req->lr_req.tlcreate.flags, &flags, &p9);
	if (error)
		return (error);

	perm = (mode_t)req->lr_req.tlcreate.mode & 0777; /* ? set-id bits? */
	gid = req->lr_req.tlcreate.gid;
	error = fs_icreate(softc, dir, name, flags, false, perm, gid, &st);
	if (error == 0)
		generate_qid(&st, &req->lr_resp.rlcreate.qid);
	req->lr_resp.rlcreate.iounit = req->lr_conn->lc_max_io_size;
	return (error);
}

static int
fs_symlink(void *softc, struct l9p_request *req)
{
	struct l9p_fid *dir;
	struct stat st;
	gid_t gid;
	char *name, *symtgt;
	int error;

	dir = req->lr_fid;
	name = req->lr_req.tsymlink.name;
	symtgt = req->lr_req.tsymlink.symtgt;
	gid = req->lr_req.tsymlink.gid;
	error = fs_isymlink(softc, dir, name, symtgt, gid, &st);
	if (error == 0)
		generate_qid(&st, &req->lr_resp.rsymlink.qid);
	return (error);
}

static int
fs_mknod(void *softc, struct l9p_request *req)
{
	struct l9p_fid *dir;
	struct stat st;
	uint32_t mode, major, minor;
	dev_t dev;
	gid_t gid;
	char *name;
	int error;

	dir = req->lr_fid;
	name = req->lr_req.tmknod.name;
	mode = req->lr_req.tmknod.mode;
	gid = req->lr_req.tmknod.gid;

	switch (mode & S_IFMT) {
	case S_IFBLK:
	case S_IFCHR:
		mode = (mode & S_IFMT) | (mode & 0777);	/* ??? */
		major = req->lr_req.tmknod.major;
		minor = req->lr_req.tmknod.major;
		dev = makedev(major, minor);
		error = fs_imknod(softc, dir, name, false,
		    (mode_t)mode, dev, gid, &st);
		break;

	case S_IFIFO:
		error = fs_imkfifo(softc, dir, name, false,
		    (mode_t)(mode & 0777), gid, &st);
		break;

	case S_IFSOCK:
		error = fs_imksocket(softc, dir, name, false,
		    (mode_t)(mode & 0777), gid, &st);
		break;

	default:
		error = EINVAL;
		break;
	}
	if (error == 0)
		generate_qid(&st, &req->lr_resp.rmknod.qid);
	return (error);
}

static int
fs_rename(void *softc, struct l9p_request *req)
{
	struct l9p_fid *fid, *f2;
	struct fs_fid *file, *f2ff;
	struct stat st;
	char *tmp;
	char newname[MAXPATHLEN];
	int error;

	/*
	 * Note: lr_fid represents the file that is to be renamed,
	 * so we must locate its parent directory and verify that
	 * both this parent directory and the new directory f2 are
	 * writable.
	 */
	fid = req->lr_fid;
	f2 = req->lr_fid2;
	f2ff = f2->lo_aux;
	assert(f2ff != NULL);

	error = fs_rdf(softc, fid, newname, sizeof(newname), &st);
	if (error)
		return (error);

	/*
	 * Only need to check f2 if it's not the same path as the
	 * directory we already discovered.
	 */
	if (strcmp(newname, f2ff->ff_name) != 0) {
		error = fs_rde(softc, f2, &st);
		if (error)
			return (error);
	}

	/* Directories OK, file systems not R/O, etc; build final name. */
	error = fs_dpf(newname, req->lr_req.trename.name, sizeof(newname));
	if (error)
		return (error);
	tmp = strdup(newname);
	if (tmp == NULL)
		return (ENOMEM);

	file = fid->lo_aux;
	if (rename(file->ff_name, tmp) != 0) {
		error = errno;
		free(tmp);
		return (error);
	}
	/* file has been renamed but old fid is not clunked */
	free(file->ff_name);
	file->ff_name = tmp;
	return (0);
}

static int
fs_readlink(void *softc __unused, struct l9p_request *req)
{
	struct fs_fid *file;
	ssize_t linklen;
	char buf[MAXPATHLEN];
	int error = 0;

	file = req->lr_fid->lo_aux;
	assert(file);

	linklen = readlink(file->ff_name, buf, sizeof(buf));
	if (linklen < 0)
		error = errno;
	else if ((size_t)linklen >= sizeof(buf))
		error = ENOMEM; /* todo: allocate dynamically */
	else if ((req->lr_resp.rreadlink.target = strndup(buf,
	    (size_t)linklen)) == NULL)
		error = ENOMEM;
	return (error);
}

static int
fs_getattr(void *softc __unused, struct l9p_request *req)
{
	uint64_t mask, valid;
	struct fs_fid *file;
	struct stat st;
	int error = 0;

	file = req->lr_fid->lo_aux;
	assert(file);

	valid = 0;
	if (lstat(file->ff_name, &st)) {
		error = errno;
		goto out;
	}
	/* ?? Can we provide items not-requested? If so, can skip tests. */
	mask = req->lr_req.tgetattr.request_mask;
	if (mask & L9PL_GETATTR_MODE) {
		/* It is not clear if we need any translations. */
		req->lr_resp.rgetattr.mode = st.st_mode;
		valid |= L9PL_GETATTR_MODE;
	}
	if (mask & L9PL_GETATTR_NLINK) {
		req->lr_resp.rgetattr.nlink = st.st_nlink;
		valid |= L9PL_GETATTR_NLINK;
	}
	if (mask & L9PL_GETATTR_UID) {
		/* provide st_uid, or file->ff_uid? */
		req->lr_resp.rgetattr.uid = st.st_uid;
		valid |= L9PL_GETATTR_UID;
	}
	if (mask & L9PL_GETATTR_GID) {
		/* provide st_gid, or file->ff_gid? */
		req->lr_resp.rgetattr.gid = st.st_gid;
		valid |= L9PL_GETATTR_GID;
	}
	if (mask & L9PL_GETATTR_RDEV) {
		/* It is not clear if we need any translations. */
		req->lr_resp.rgetattr.rdev = (uint64_t)st.st_rdev;
		valid |= L9PL_GETATTR_RDEV;
	}
	if (mask & L9PL_GETATTR_ATIME) {
		req->lr_resp.rgetattr.atime_sec =
		    (uint64_t)st.st_atimespec.tv_sec;
		req->lr_resp.rgetattr.atime_nsec =
		    (uint64_t)st.st_atimespec.tv_nsec;
		valid |= L9PL_GETATTR_ATIME;
	}
	if (mask & L9PL_GETATTR_MTIME) {
		req->lr_resp.rgetattr.mtime_sec =
		    (uint64_t)st.st_mtimespec.tv_sec;
		req->lr_resp.rgetattr.mtime_nsec =
		    (uint64_t)st.st_mtimespec.tv_nsec;
		valid |= L9PL_GETATTR_MTIME;
	}
	if (mask & L9PL_GETATTR_CTIME) {
		req->lr_resp.rgetattr.ctime_sec =
		    (uint64_t)st.st_ctimespec.tv_sec;
		req->lr_resp.rgetattr.ctime_nsec =
		    (uint64_t)st.st_ctimespec.tv_nsec;
		valid |= L9PL_GETATTR_CTIME;
	}
	if (mask & L9PL_GETATTR_BTIME) {
#if defined(HAVE_BIRTHTIME)
		req->lr_resp.rgetattr.btime_sec =
		    (uint64_t)st.st_birthtim.tv_sec;
		req->lr_resp.rgetattr.btime_nsec =
		    (uint64_t)st.st_birthtim.tv_nsec;
#else
		req->lr_resp.rgetattr.btime_sec = 0;
		req->lr_resp.rgetattr.btime_nsec = 0;
#endif
		valid |= L9PL_GETATTR_BTIME;
	}
	if (mask & L9PL_GETATTR_INO)
		valid |= L9PL_GETATTR_INO;
	if (mask & L9PL_GETATTR_SIZE) {
		req->lr_resp.rgetattr.size = (uint64_t)st.st_size;
		valid |= L9PL_GETATTR_SIZE;
	}
	if (mask & L9PL_GETATTR_BLOCKS) {
		req->lr_resp.rgetattr.blksize = (uint64_t)st.st_blksize;
		req->lr_resp.rgetattr.blocks = (uint64_t)st.st_blocks;
		valid |= L9PL_GETATTR_BLOCKS;
	}
	if (mask & L9PL_GETATTR_GEN) {
		req->lr_resp.rgetattr.gen = st.st_gen;
		valid |= L9PL_GETATTR_GEN;
	}
	/* don't know what to do with data version yet */

	generate_qid(&st, &req->lr_resp.rgetattr.qid);
out:
	req->lr_resp.rgetattr.valid = valid;
	return (error);
}

/*
 * Should combine some of this with wstat code.
 */
static int
fs_setattr(void *softc, struct l9p_request *req)
{
	uint64_t mask;
	struct fs_softc *sc = softc;
	struct timeval tv[2];
	struct fs_fid *file;
	struct stat st;
	int error = 0;
	uid_t uid, gid;

	file = req->lr_fid->lo_aux;
	assert(file);

	if (sc->fs_readonly)
		return (EROFS);

	/*
	 * As with WSTAT we have atomicity issues.
	 */
	mask = req->lr_req.tsetattr.valid;

	if (lstat(file->ff_name, &st)) {
		error = errno;
		goto out;
	}

	if ((mask & L9PL_SETATTR_SIZE) && S_ISDIR(st.st_mode)) {
		error = EISDIR;
		goto out;
	}

	if (mask & L9PL_SETATTR_MODE) {
		if (lchmod(file->ff_name, req->lr_req.tsetattr.mode & 0777)) {
			error = errno;
			goto out;
		}
	}

	if (mask & (L9PL_SETATTR_UID | L9PL_SETATTR_GID)) {
		uid = mask & L9PL_SETATTR_UID ? req->lr_req.tsetattr.uid :
		    (uid_t)-1;
		gid = mask & L9PL_SETATTR_GID ? req->lr_req.tsetattr.gid :
		    (gid_t)-1;
		if (lchown(file->ff_name, uid, gid)) {
			error = errno;
			goto out;
		}
	}

	if (mask & L9PL_SETATTR_SIZE) {
		/* Truncate follows symlinks, is this OK? */
		if (truncate(file->ff_name, (off_t)req->lr_req.tsetattr.size)) {
			error = errno;
			goto out;
		}
	}

	if (mask & (L9PL_SETATTR_ATIME | L9PL_SETATTR_CTIME)) {
		tv[0].tv_sec = st.st_atimespec.tv_sec;
		tv[0].tv_usec = (int)st.st_atimespec.tv_nsec / 1000;
		tv[1].tv_sec = st.st_mtimespec.tv_sec;
		tv[1].tv_usec = (int)st.st_mtimespec.tv_nsec / 1000;

		if (mask & L9PL_SETATTR_ATIME) {
			if (mask & L9PL_SETATTR_ATIME_SET) {
				tv[0].tv_sec =
				    (long)req->lr_req.tsetattr.atime_sec;
				tv[0].tv_usec =
				    (int)req->lr_req.tsetattr.atime_nsec / 1000;
			} else {
				if (gettimeofday(&tv[0], NULL)) {
					error = errno;
					goto out;
				}
			}
		}
		if (mask & L9PL_SETATTR_MTIME) {
			if (mask & L9PL_SETATTR_MTIME_SET) {
				tv[1].tv_sec =
				    (long)req->lr_req.tsetattr.mtime_sec;
				tv[1].tv_usec =
				    (int)req->lr_req.tsetattr.mtime_nsec / 1000;
			} else {
				if (gettimeofday(&tv[1], NULL)) {
					error = errno;
					goto out;
				}
			}
		}
		if (lutimes(file->ff_name, tv)) {
			error = errno;
			goto out;
		}
	}
out:
	return (error);
}

static int
fs_xattrwalk(void *softc __unused, struct l9p_request *req __unused)
{
	return (EOPNOTSUPP);
}

static int
fs_xattrcreate(void *softc __unused, struct l9p_request *req __unused)
{
	return (EOPNOTSUPP);
}

static int
fs_readdir(void *softc __unused, struct l9p_request *req)
{
	struct l9p_message msg;
	struct l9p_dirent de;
	struct fs_fid *file;
	struct dirent *dp;
	struct stat st;
	int error = 0;

	file = req->lr_fid->lo_aux;
	assert(file);

	if (file->ff_dir == NULL)
		return (ENOTDIR);

	/*
	 * There is no getdirentries variant that accepts an
	 * offset, so once we are multithreaded, this will need
	 * a lock (which will cover the dirent structures as well).
	 *
	 * It's not clear whether we can use the same trick for
	 * discarding offsets here as we do in fs_read.  It
	 * probably should work, we'll have to see if some
	 * client(s) use the zero-offset thing to rescan without
	 * clunking the directory first.
	 */
	if (req->lr_req.io.offset == 0)
		rewinddir(file->ff_dir);
	else
		seekdir(file->ff_dir, (long)req->lr_req.io.offset);

	l9p_init_msg(&msg, req, L9P_PACK);
	while ((dp = readdir(file->ff_dir)) != NULL) {
		/*
		 * Although "." is forbidden in naming and ".." is
		 * special cased, testing shows that we must transmit
		 * them through readdir.  (For ".." at root, we
		 * should perhaps alter the inode number, but not
		 * yet.)
		 */
#ifdef wrong
		if (dp->d_name[0] == '.' &&
		    (dp->d_namlen == 1 || strcmp(dp->d_name, "..") == 0))
			continue;
#endif

		/*
		 * TODO: we do a full lstat here; could use dp->d_*
		 * to construct the qid more efficiently, as long
		 * as dp->d_type != DT_UNKNOWN.
		 */
		if (fs_lstatat(file, dp->d_name, &st))
			continue;

		de.qid.type = 0;
		generate_qid(&st, &de.qid);
		de.offset = (uint64_t)telldir(file->ff_dir);
		de.type = dp->d_type;
		de.name = dp->d_name;

		if (l9p_pudirent(&msg, &de) < 0)
			break;
	}

	req->lr_resp.io.count = (uint32_t)msg.lm_size;
	return (error);
}

static int
fs_fsync(void *softc __unused, struct l9p_request *req)
{
	struct fs_fid *file;
	int error = 0;

	file = req->lr_fid->lo_aux;
	assert(file);
	if (fsync(file->ff_fd))
		error = errno;
	return (error);
}

static int
fs_lock(void *softc, struct l9p_request *req)
{
	struct fs_softc *sc = softc;
	struct fs_fid *file;
	struct flock fl;

	if (sc->fs_readonly)
		return (EROFS);

	file = req->lr_fid->lo_aux;
	fl.l_start = (off_t)req->lr_req.tlock.start;
	fl.l_len = (off_t)req->lr_req.tlock.length;
	fl.l_pid = (pid_t)req->lr_req.tlock.proc_id;
	fl.l_type = req->lr_req.tlock.type;
	fl.l_whence = SEEK_SET;

	if (fcntl(file->ff_fd, F_SETLK, (void *)&fl) != 0) {
		if (errno == EAGAIN) {
			req->lr_resp.rlock.status = L9PL_LOCK_BLOCKED;
			return (0);
		}

		req->lr_resp.rlock.status = L9PL_LOCK_ERROR;
		return (0);
	}

	req->lr_resp.rlock.status = L9PL_LOCK_SUCCESS;
	return (0);
}

static int
fs_getlock(void *softc __unused, struct l9p_request *req)
{
	struct fs_fid *file;
	struct flock fl;

	file = req->lr_fid->lo_aux;

	if (fcntl(file->ff_fd, F_GETLK, (void *)&fl) != 0)
		return (errno);

	req->lr_resp.getlock.proc_id = (uint32_t)fl.l_pid;
	req->lr_resp.getlock.start = (uint64_t)fl.l_start;
	req->lr_resp.getlock.length = (uint64_t)fl.l_len;
	req->lr_resp.getlock.type = (uint8_t)fl.l_type;
	req->lr_resp.getlock.client_id = (char *)"";  /* XXX what should go here? */
	return (0);
}

static int
fs_link(void *softc, struct l9p_request *req)
{
	struct l9p_fid *dir;
	struct fs_fid *file;
	struct fs_fid *dirf;
	struct stat st;
	char *name;
	char newname[MAXPATHLEN];
	int error;

	/* N.B.: lr_fid is the file to link, lr_fid2 is the target dir */
	dir = req->lr_fid2;
	name = req->lr_req.tlink.name;
	error = fs_buildname(dir, name, newname, sizeof(newname));
	if (error)
		return (error);

	/* Require write access to target directory. */
	error = fs_rde(softc, dir, &st);
	if (error)
		return (error);

	dirf = dir->lo_aux;

	file = req->lr_fid->lo_aux;
	assert(file != NULL);
	if (link(file->ff_name, newname) != 0)
		error = errno;
	return (error);
}

static int
fs_mkdir(void *softc, struct l9p_request *req)
{
	struct l9p_fid *dir;
	struct stat st;
	mode_t perm;
	gid_t gid;
	char *name;
	int error;

	dir = req->lr_fid;
	name = req->lr_req.tmkdir.name;
	perm = (mode_t)req->lr_req.tmkdir.mode;
	gid = req->lr_req.tmkdir.gid;

	error = fs_imkdir(softc, dir, name, false, perm, gid, &st);
	if (error == 0)
		generate_qid(&st, &req->lr_resp.rmkdir.qid);
	return (error);
}

static int
fs_renameat(void *softc, struct l9p_request *req)
{
	struct l9p_fid *olddir, *newdir;
	struct stat st;
	char *onp, *nnp;
	char onb[MAXPATHLEN], nnb[MAXPATHLEN];
	int error;

	olddir = req->lr_fid;
	newdir = req->lr_fid2;
	assert(olddir != NULL && newdir != NULL);

	/*
	 * We don't support path rewriting in open fids yet, so let's just
	 * fail here for now.
	 */
	return (EOPNOTSUPP);

	error = fs_rde(softc, olddir, &st);
	if (error)
		return (error);

	if (olddir != newdir) {
		/*
		 * The dir fids may differ but have the same path.
		 * Is this strcmp worth doing?
		 *
		 * struct ff_fid *off, *nff;
		 * off = olddir->lo_aux;
		 * nff = newdir->lo_aux;
		 * assert(nff != NULL);
		 * if (strcmp(off->ff_name, nff->ff_name) != 0) {
		 *     error = fs_rde(...);
		 *     ...
		 * }
		 */
		error = fs_rde(softc, newdir, &st);
		if (error)
			return (error);
	}

	onp = req->lr_req.trenameat.oldname;
	nnp = req->lr_req.trenameat.newname;

	error = fs_buildname(olddir, onp, onb, sizeof(onb));
	if (error)
		return (error);
	error = fs_buildname(newdir, nnp, nnb, sizeof(nnb));
	if (error)
		return (error);

	if (rename(onb, nnb))
		error = errno;

	return (error);
}

/*
 * Unlink file in given directory, or remove directory in given
 * directory, based on flags.
 */
static int
fs_unlinkat(void *softc, struct l9p_request *req)
{
	struct l9p_fid *dir;
	struct stat st;
	char *name;
	char newname[MAXPATHLEN];
	int error;

	dir = req->lr_fid;
	name = req->lr_req.tunlinkat.name;
	error = fs_buildname(dir, name, newname, sizeof(newname));
	if (error)
		return (error);

	/* Require write access to directory. */
	error = fs_rde(softc, dir, &st);
	if (error)
		return (error);

	if (req->lr_req.tunlinkat.flags & L9PL_AT_REMOVEDIR) {
		if (rmdir(newname) != 0)
			error = errno;
	} else {
		if (unlink(newname) != 0)
			error = errno;
	}
	return (error);
}

static void
fs_freefid(void *softc __unused, struct l9p_fid *fid)
{
	struct fs_fid *f = fid->lo_aux;
	struct fs_authinfo *ai;

	if (f == NULL) {
		/* Nothing to do here */
		return;
	}

	if (f->ff_fd != -1)
		close(f->ff_fd);

	if (f->ff_dir)
		closedir(f->ff_dir);

	free(f->ff_name);
	ai = f->ff_ai;
	free(f);
	if (--ai->ai_refcnt == 0)
		free(ai);
}

int
l9p_backend_fs_init(struct l9p_backend **backendp, const char *root)
{
	struct l9p_backend *backend;
	struct fs_softc *sc;
	const char *rroot;

	rroot = realpath(root, NULL);
	if (rroot == NULL)
		return (-1);
	backend = l9p_malloc(sizeof(*backend));
	backend->attach = fs_attach;
	backend->clunk = fs_clunk;
	backend->create = fs_create;
	backend->open = fs_open;
	backend->read = fs_read;
	backend->remove = fs_remove;
	backend->stat = fs_stat;
	backend->walk = fs_walk;
	backend->write = fs_write;
	backend->wstat = fs_wstat;
	backend->statfs = fs_statfs;
	backend->lopen = fs_lopen;
	backend->lcreate = fs_lcreate;
	backend->symlink = fs_symlink;
	backend->mknod = fs_mknod;
	backend->rename = fs_rename;
	backend->readlink = fs_readlink;
	backend->getattr = fs_getattr;
	backend->setattr = fs_setattr;
	backend->xattrwalk = fs_xattrwalk;
	backend->xattrcreate = fs_xattrcreate;
	backend->readdir = fs_readdir;
	backend->fsync = fs_fsync;
	backend->lock = fs_lock;
	backend->getlock = fs_getlock;
	backend->link = fs_link;
	backend->mkdir = fs_mkdir;
	backend->renameat = fs_renameat;
	backend->unlinkat = fs_unlinkat;
	backend->freefid = fs_freefid;

	sc = l9p_malloc(sizeof(*sc));
	sc->fs_rootpath = rroot;
	sc->fs_readonly = false;
	backend->softc = sc;

	setpassent(1);

	*backendp = backend;
	return (0);
}
