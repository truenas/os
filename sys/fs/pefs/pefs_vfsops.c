/*-
 * Copyright (c) 1992, 1993, 1995
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 2009 Gleb Kurtsou <gleb@FreeBSD.org>
 *
 * This code is derived from software donated to Berkeley by
 * Jan-Simon Pendry.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
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
#include <sys/systm.h>
#include <sys/fcntl.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/proc.h>
#include <sys/vnode.h>

#include <fs/pefs/pefs.h>

struct pefs_opt_descr {
	char	*fs;
	int	initial;
	int	forbid;
};

static const struct pefs_opt_descr pefs_opt_support[] = {
	{
		.fs = "zfs",
#if __FreeBSD_version >= 1000025
		.initial = PM_DIRCACHE,
#else
		.initial = PM_DIRCACHE | PM_ASYNCRECLAIM,
#endif
		.forbid = 0
	},
	{	/* default flags */
		.fs = NULL,
		.initial = 0,
		.forbid = PM_DIRCACHE,
	}
};

static const char		*pefs_opts[] = {
	"from",
	"export",
	"dircache",
	"nodircache",
	"asyncreclaim",
	NULL
};

static MALLOC_DEFINE(M_PEFSMNT, "pefs_mount", "PEFS mount structure");

static void
pefs_opt_set(struct mount *mp, int opt, struct pefs_mount *pm,
    int flag, const char *flagname)
{
	const struct pefs_opt_descr *descr;
	char *lowerfs;

	lowerfs = mp->mnt_vnodecovered->v_mount->mnt_vfc->vfc_name;
	for (descr = pefs_opt_support; descr->fs != NULL; descr++)
		if (strcmp(lowerfs, descr->fs) == 0)
			break;
	if (opt < 0)
		opt = descr->initial & flag;
	else if (opt > 0 && (descr->forbid & flag) != 0) {
		printf("pefs: %s is not supported by file system: %s\n",
		    flagname, lowerfs);
		opt = 0;
	}

	if (opt == 0)
		pm->pm_flags &= ~flag;
	else
		pm->pm_flags |= flag;
	PEFSDEBUG("pefs_mount: %s %s\n",
	    flagname, (opt ? "enabled" : "disabed"));
}

static int
subdir(const char *p, const char *dir)
{
	int l;

	l = strlen(dir);
	if (l <= 1)
		return (1);

	if ((strncmp(p, dir, l) == 0) && (p[l] == '/' || p[l] == '\0'))
		return (1);

	return (0);
}

/*
 * Mount null layer
 */
static int
pefs_mount(struct mount *mp)
{
	struct vnode *lowerrootvp, *vp;
	struct vnode *pm_rootvp;
	struct nameidata nd, *ndp = &nd;
	struct pefs_mount *pm;
	char *from, *from_free;
	int isvnunlocked = 0, len;
	int opt_dircache, opt_asyncreclaim;
	int error = 0;

	PEFSDEBUG("pefs_mount(mp = %p)\n", (void *)mp);

	if (mp->mnt_flag & MNT_ROOTFS)
		return (EOPNOTSUPP);

	if (vfs_filteropt(mp->mnt_optnew, pefs_opts))
		return (EINVAL);

	opt_dircache = -1;
	if (vfs_flagopt(mp->mnt_optnew, "dircache", NULL, 0)) {
		vfs_deleteopt(mp->mnt_optnew, "dircache");
		opt_dircache = 1;
	} else if (vfs_flagopt(mp->mnt_optnew, "nodircache", NULL, 0)) {
		vfs_deleteopt(mp->mnt_optnew, "nodircache");
		opt_dircache = 0;
	}
	opt_asyncreclaim = -1;
	if (vfs_flagopt(mp->mnt_optnew, "asyncreclaim", NULL, 0)) {
		vfs_deleteopt(mp->mnt_optnew, "asyncreclaim");
		opt_asyncreclaim = 1;
	}

	if (mp->mnt_flag & MNT_UPDATE) {
		error = EOPNOTSUPP;
		if (vfs_flagopt(mp->mnt_optnew, "export", NULL, 0))
			error = 0;
		if (opt_dircache >= 0) {
			pefs_opt_set(mp, opt_dircache, mp->mnt_data,
			    PM_DIRCACHE, "dircache");
			error = 0;
		}
		if (opt_asyncreclaim >= 0) {
			pefs_opt_set(mp, opt_dircache, mp->mnt_data,
			    PM_ASYNCRECLAIM, "asyncreclaim");
			error = 0;
		}
		return (error);
	}

	/*
	 * Get argument
	 */
	error = vfs_getopt(mp->mnt_optnew, "from", (void **)&from, &len);
	if (error || from[len - 1] != '\0')
		return (EINVAL);
	vfs_mountedfrom(mp, from);

	/*
	 * Unlock lower node to avoid deadlock.
	 * (XXX) VOP_ISLOCKED is needed?
	 */
	if ((mp->mnt_vnodecovered->v_op == &pefs_vnodeops) &&
		VOP_ISLOCKED(mp->mnt_vnodecovered)) {
		VOP_UNLOCK(mp->mnt_vnodecovered, 0);
		isvnunlocked = 1;
	}
	/*
	 * Find lower node
	 */
	NDINIT(ndp, LOOKUP, FOLLOW, UIO_SYSSPACE, from, curthread);
	error = namei(ndp);

	if (error == 0) {
		from_free = NULL;
		error = vn_fullpath(curthread, ndp->ni_vp, &from,
		    &from_free);
		if (error == 0)
			vfs_mountedfrom(mp, from);
		else
			error = 0;
		free(from_free, M_TEMP);
	}
	/*
	 * Re-lock vnode.
	 */
	if (isvnunlocked && !VOP_ISLOCKED(mp->mnt_vnodecovered))
		vn_lock(mp->mnt_vnodecovered, LK_EXCLUSIVE | LK_RETRY);

	if (error != 0)
		return (error);
	NDFREE(ndp, NDF_ONLY_PNBUF);

	/*
	 * Sanity check on lower vnode
	 */
	lowerrootvp = ndp->ni_vp;
	vn_lock(lowerrootvp, LK_EXCLUSIVE | LK_RETRY);
	if ((lowerrootvp->v_iflag & VI_DOOMED) != 0) {
		PEFSDEBUG("pefs_mount: target vnode disappeared\n");
		vput(lowerrootvp);
		return (ENOENT);
	}

	/*
	 * Check multi pefs mount to avoid `lock against myself' panic.
	 */
	if (lowerrootvp->v_mount->mnt_vfc == mp->mnt_vfc) {
		PEFSDEBUG("pefs_mount: multi pefs mount\n");
		vput(lowerrootvp);
		return (EDEADLK);
	}

	/*
	 * Check paths are not nested
	 */
	if ((lowerrootvp != mp->mnt_vnodecovered) &&
	    (subdir(mp->mnt_stat.f_mntfromname, mp->mnt_stat.f_mntonname) ||
	    subdir(mp->mnt_stat.f_mntonname, mp->mnt_stat.f_mntfromname))) {
		PEFSDEBUG("pefs_mount: %s and %s are nested paths\n",
		    mp->mnt_stat.f_mntfromname, mp->mnt_stat.f_mntonname);
		vput(lowerrootvp);
		return (EDEADLK);
	}

	pm = (struct pefs_mount *)malloc(sizeof(struct pefs_mount), M_PEFSMNT,
	    M_WAITOK | M_ZERO);

	mtx_init(&pm->pm_keys_lock, "pefs_mount lock", NULL, MTX_DEF);
	TAILQ_INIT(&pm->pm_keys);

	/*
	 * Save reference to underlying FS
	 */
	pm->pm_lowervfs = lowerrootvp->v_mount;
	if (lowerrootvp == mp->mnt_vnodecovered)
		pm->pm_flags |= PM_ROOT_CANRECURSE;
	pefs_opt_set(mp, opt_dircache, pm, PM_DIRCACHE, "dircache");
	pefs_opt_set(mp, opt_asyncreclaim, pm, PM_ASYNCRECLAIM, "asyncreclaim");

	/*
	 * Save reference.  Each mount also holds
	 * a reference on the root vnode.
	 */
	vp = NULL;
	error = pefs_node_get_nokey(mp, lowerrootvp, &vp);
	/*
	 * Make sure the node alias worked
	 */
	if (error != 0) {
		vput(lowerrootvp);
		free(pm, M_PEFSMNT);
		return (error);
	}

	/*
	 * Keep a held reference to the root vnode.
	 * It is vrele'd in pefs_unmount.
	 */
	pm_rootvp = vp;
	pm_rootvp->v_vflag |= VV_ROOT;
	pm->pm_rootvp = pm_rootvp;

	/*
	 * Unlock the node (either the lower or the alias)
	 */
	VOP_UNLOCK(vp, 0);

	if (PEFS_LOWERVP(pm_rootvp)->v_mount->mnt_flag & MNT_LOCAL) {
		MNT_ILOCK(mp);
		mp->mnt_flag |= MNT_LOCAL;
		MNT_IUNLOCK(mp);
	}
#if __FreeBSD_version < 1000021
	MNT_ILOCK(mp);
	mp->mnt_kern_flag |= lowerrootvp->v_mount->mnt_kern_flag & MNTK_MPSAFE;
	MNT_IUNLOCK(mp);
#endif
	mp->mnt_data =  pm;
	vfs_getnewfsid(mp);

	PEFSDEBUG("pefs_mount: lower %s, alias at %s\n",
		mp->mnt_stat.f_mntfromname, mp->mnt_stat.f_mntonname);
	return (0);
}

/*
 * Free reference to null layer
 */
static int
pefs_unmount(struct mount *mp, int mntflags)
{
	struct pefs_mount *pm;
	int error;
	int flags = 0;

	PEFSDEBUG("pefs_unmount: mp = %p\n", (void *)mp);

	if (mntflags & MNT_FORCE)
		flags |= FORCECLOSE;

	/* There is 1 extra root vnode reference (pm_rootvp). */
	error = vflush(mp, 1, flags, curthread);
	if (error != 0)
		return (error);

	/*
	 * Finally, throw away the pefs_mount structure
	 */
	pm = VFS_TO_PEFS(mp);
	mp->mnt_data = 0;
	pefs_key_remove_all(pm);
	mtx_destroy(&pm->pm_keys_lock);
	free(pm, M_PEFSMNT);
	return (0);
}

static int
pefs_root(struct mount *mp, int flags, struct vnode **vpp)
{
	struct vnode *vp;

	/*
	 * Return locked reference to root.
	 */
	vp = VFS_TO_PEFS(mp)->pm_rootvp;
	VREF(vp);

	vn_lock(vp, flags | LK_RETRY);
	*vpp = vp;
	return (0);
}

static int
pefs_quotactl(struct mount *mp, int cmd, uid_t uid, void *arg)
{
	return (VFS_QUOTACTL(VFS_TO_PEFS(mp)->pm_lowervfs, cmd, uid, arg));
}

static int
pefs_statfs(struct mount *mp, struct statfs *sbp)
{
	int error;
	struct statfs mstat;

	bzero(&mstat, sizeof(mstat));

	error = VFS_STATFS(VFS_TO_PEFS(mp)->pm_lowervfs, &mstat);
	if (error != 0)
		return (error);

	/* now copy across the "interesting" information and fake the rest */
	sbp->f_type = mstat.f_type;
	sbp->f_flags = mstat.f_flags;
	sbp->f_bsize = mstat.f_bsize;
	sbp->f_iosize = mstat.f_iosize;
	sbp->f_blocks = mstat.f_blocks;
	sbp->f_bfree = mstat.f_bfree;
	sbp->f_bavail = mstat.f_bavail;
	sbp->f_files = mstat.f_files;
	sbp->f_ffree = mstat.f_ffree;
	return (0);
}

static int
pefs_sync(struct mount *mp, int waitfor)
{
	/*
	 * XXX - Assumes no data cached at null layer.
	 */
	return (0);
}

static int
pefs_vget(struct mount *mp, ino_t ino, int flags, struct vnode **vpp)
{
	int error;
	error = VFS_VGET(VFS_TO_PEFS(mp)->pm_lowervfs, ino, flags, vpp);
	if (error != 0)
		return (error);

	return (pefs_node_get_lookupkey(mp, *vpp, vpp, curthread->td_ucred));
}

static int
#if __FreeBSD_version >= 900038
pefs_fhtovp(struct mount *mp, struct fid *fidp, int flags, struct vnode **vpp)
#else
pefs_fhtovp(struct mount *mp, struct fid *fidp, struct vnode **vpp)
#endif
{
	int error;

#if __FreeBSD_version >= 900038
	error = VFS_FHTOVP(VFS_TO_PEFS(mp)->pm_lowervfs, fidp, flags, vpp);
#else
	error = VFS_FHTOVP(VFS_TO_PEFS(mp)->pm_lowervfs, fidp, vpp);
#endif /* __FreeBSD_version */
	if (error != 0)
		return (error);

	error = pefs_node_get_lookupkey(mp, *vpp, vpp, curthread->td_ucred);
	if (error != 0)
		return (error);
	vnode_create_vobject(*vpp, 0, curthread);
	return (error);
}

static int
pefs_extattrctl(struct mount *mp, int cmd, struct vnode *filename_vp,
    int namespace, const char *attrname)
{
	return (VFS_EXTATTRCTL(VFS_TO_PEFS(mp)->pm_lowervfs, cmd, filename_vp,
	    namespace, attrname));
}


static struct vfsops pefs_vfsops = {
	.vfs_extattrctl =	pefs_extattrctl,
	.vfs_fhtovp =		pefs_fhtovp,
	.vfs_init =		pefs_init,
	.vfs_mount =		pefs_mount,
	.vfs_quotactl =		pefs_quotactl,
	.vfs_root =		pefs_root,
	.vfs_statfs =		pefs_statfs,
	.vfs_sync =		pefs_sync,
	.vfs_uninit =		pefs_uninit,
	.vfs_unmount =		pefs_unmount,
	.vfs_vget =		pefs_vget,
};

VFS_SET(pefs_vfsops, pefs, VFCF_LOOPBACK);
MODULE_DEPEND(pefs, crypto, 1, 1, 1);
#ifdef PEFS_AESNI
MODULE_DEPEND(pefs, aesni, 1, 1, 1);
#endif
