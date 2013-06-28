/*-
 * Copyright (c) 1992, 1993 The Regents of the University of California.
 * Copyright (c) 2009 Gleb Kurtsou <gleb@FreeBSD.org>
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * John Heidemann of the UCLA Ficus project.
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
#include <sys/conf.h>
#include <sys/ioccom.h>
#include <sys/fcntl.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/mutex.h>
#include <sys/namei.h>
#include <sys/priv.h>
#include <sys/rwlock.h>
#include <sys/sf_buf.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/sx.h>
#include <sys/vnode.h>
#include <sys/dirent.h>
#include <sys/limits.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/unistd.h>
#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pager.h>
#include <vm/vnode_pager.h>

#include <fs/pefs/pefs.h>
#include <fs/pefs/pefs_dircache.h>

#define	DIRENT_MINSIZE		(sizeof(struct dirent) - (MAXNAMLEN + 1))
#define	DIRENT_MAXSIZE		(sizeof(struct dirent))

CTASSERT(PEFS_SECTOR_SIZE == PAGE_SIZE);

struct pefs_enccn {
	struct componentname	pec_cn;
	void			*pec_buf;
	struct pefs_tkey	pec_tkey;
};

static int	pefs_read_int(struct vnode *vp, struct uio *uio, int ioflag,
		    struct ucred *cred, u_quad_t fsize);
static int	pefs_write_int(struct vnode *vp, struct uio *uio, int ioflag,
		    struct ucred *cred, u_quad_t nsize);

static __inline u_long
pefs_getgen(struct vnode *vp, struct ucred *cred)
{
	struct vattr va;
	int error;

	error = VOP_GETATTR(PEFS_LOWERVP(vp), &va, cred);
	if (error != 0)
		return (0);

	return (va.va_gen);
}

static __inline int
pefs_tkey_cmp(struct pefs_tkey *a, struct pefs_tkey *b)
{
	int r;

	r = (intptr_t)a->ptk_key - (intptr_t)b->ptk_key;
	if (r == 0)
		r = memcmp(a->ptk_tweak, b->ptk_tweak, PEFS_TWEAK_SIZE);

	return (r);
}

static __inline int
pefs_cache_active(struct vnode *vp)
{
	struct pefs_mount *pm = VFS_TO_PEFS(vp->v_mount);

	return (pefs_dircache_enable &&
	    (pm->pm_flags & PM_DIRCACHE) != 0);
}

static struct pefs_dircache_entry *
pefs_cache_dirent(struct pefs_dircache *pd, struct dirent *de,
    struct pefs_ctx *ctx, struct pefs_key *pk)
{
	struct pefs_dircache_entry *cache;
	struct pefs_tkey ptk;
	char buf[MAXNAMLEN + 1];
	int name_len;

	cache = pefs_dircache_enclookup(pd, de->d_name, de->d_namlen);
	if (cache != NULL)
		pefs_dircache_update(cache);
	else {
		name_len = pefs_name_decrypt(ctx, pk, &ptk,
		    de->d_name, de->d_namlen, buf, sizeof(buf));
		if (name_len <= 0)
			return (NULL);
		cache = pefs_dircache_insert(pd, &ptk,
		    buf, name_len, de->d_name, de->d_namlen);
	}

	return (cache);
}

static __inline int
pefs_name_skip(char *name, int namelen)
{
	if (name[0] == '.' &&
	    (namelen == 1 || (namelen == 2 && name[1] == '.')))
		return (1);
	return (0);
}

static __inline void
pefs_enccn_init(struct pefs_enccn *pec)
{
	pec->pec_buf = NULL;
	pec->pec_cn.cn_flags = 0;
}

static __inline void
pefs_enccn_alloc(struct pefs_enccn *pec, struct componentname *cnp)
{
	KASSERT(pec->pec_buf == NULL, ("pefs_enccn_alloc: buffer reuse\n"));
	pec->pec_buf = uma_zalloc(namei_zone, M_WAITOK);
	pec->pec_cn = *cnp;
	pec->pec_cn.cn_flags |= HASBUF;
	pec->pec_cn.cn_pnbuf = pec->pec_buf;
	pec->pec_cn.cn_nameptr = pec->pec_buf;
	pec->pec_cn.cn_consume = 0;
	pec->pec_cn.cn_namelen = 0;
}

static __inline void
pefs_enccn_free(struct pefs_enccn *pec)
{
	if (pec->pec_buf != NULL) {
		KASSERT(pec->pec_buf == pec->pec_cn.cn_pnbuf &&
		    pec->pec_buf == pec->pec_cn.cn_nameptr,
		    ("pefs_enccn_free: invalid buffer\n"));
		KASSERT(pec->pec_cn.cn_flags & HASBUF,
		    ("pefs_enccn_free: buffer is already freed\n"));
		uma_zfree(namei_zone, pec->pec_buf);
		pec->pec_buf = NULL;
		pec->pec_cn.cn_flags = 0;
		pec->pec_cn.cn_pnbuf = NULL;
		pec->pec_cn.cn_nameptr = NULL;
	}
}

static void
pefs_enccn_set(struct pefs_enccn *pec, struct pefs_tkey *ptk, char *encname,
    int encname_len, struct componentname *cnp)
{
	MPASS(pec != NULL && cnp != NULL);

	if (encname_len >= MAXPATHLEN)
		panic("pefs_enccn_set: invalid encrypted name length: %d",
		    encname_len);

	pefs_enccn_alloc(pec, cnp);
	if (ptk != NULL && ptk->ptk_key != NULL)
		pec->pec_tkey = *ptk;
	else
		pec->pec_tkey.ptk_key = NULL;
	memcpy(pec->pec_buf, encname, encname_len);
	((char *) pec->pec_buf)[encname_len] = '\0';
	pec->pec_cn.cn_namelen = encname_len;
}

static int
pefs_enccn_create(struct pefs_enccn *pec, struct pefs_key *pk, char *tweak,
    struct componentname *cnp)
{
	int r;

	MPASS(pec != NULL && cnp != NULL && pk != NULL);
	MPASS((cnp->cn_flags & ISDOTDOT) == 0);
	MPASS(pefs_name_skip(cnp->cn_nameptr, cnp->cn_namelen) == 0);

	pefs_enccn_alloc(pec, cnp);
	if (tweak == NULL)
		arc4rand(pec->pec_tkey.ptk_tweak, PEFS_TWEAK_SIZE, 0);
	else
		memcpy(pec->pec_tkey.ptk_tweak, tweak, PEFS_TWEAK_SIZE);
	pec->pec_tkey.ptk_key = pk;
	r = pefs_name_encrypt(NULL, &pec->pec_tkey, cnp->cn_nameptr,
	    cnp->cn_namelen, pec->pec_buf, MAXPATHLEN);
	if (r <= 0) {
		pefs_enccn_free(pec);
		if (r == 0)
			return (EINVAL);
		return (-r);
	}
	pec->pec_cn.cn_namelen = r;

	return (0);
}

static int
pefs_enccn_create_node(struct pefs_enccn *pec, struct vnode *dvp,
    struct componentname *cnp)
{
	struct pefs_key *dpk;
	int error;

	dpk = pefs_node_key(VP_TO_PN(dvp));
	error = pefs_enccn_create(pec, dpk, NULL, cnp);
	pefs_key_release(dpk);

	return (error);
}

static void
pefs_enccn_parsedir(struct pefs_dircache *pd, struct pefs_ctx *ctx,
    struct pefs_key *pk, void *mem, size_t sz, char *name, size_t name_len,
    struct pefs_dircache_entry **retval)
{
	struct pefs_dircache_entry *cache;
	struct dirent *de;

	PEFSDEBUG("pefs_enccn_parsedir: lookup %.*s\n", (int)name_len, name);
	cache = NULL;
	for (de = (struct dirent*) mem; sz > DIRENT_MINSIZE;
			sz -= de->d_reclen,
			de = (struct dirent *)(((caddr_t)de) + de->d_reclen)) {
		MPASS(de->d_reclen <= sz);
		if (de->d_reclen == 0)
			break;
		if (de->d_type == DT_WHT || de->d_fileno == 0)
			continue;
		if (pefs_name_skip(de->d_name, de->d_namlen))
			continue;

		cache = pefs_cache_dirent(pd, de, ctx, pk);
		if (cache != NULL && *retval == NULL &&
		    cache->pde_namelen == name_len &&
		    memcmp(name, cache->pde_name, name_len) == 0) {
			*retval = cache;
		}
	}
}

static int
pefs_enccn_lookup(struct pefs_enccn *pec, struct vnode *dvp,
    struct componentname *cnp)
{
	struct uio *uio;
	struct vnode *ldvp = PEFS_LOWERVP(dvp);
	struct pefs_node *dpn = VP_TO_PN(dvp);
	struct pefs_chunk pc;
	struct pefs_ctx *ctx;
	struct pefs_dircache_entry *cache;
	struct pefs_key *dpn_key;
	off_t offset;
	u_long dgen;
	int eofflag, error;

	MPASS(pec != NULL && dvp != NULL && cnp != NULL);

	if ((cnp->cn_flags & ISDOTDOT) ||
	    pefs_name_skip(cnp->cn_nameptr, cnp->cn_namelen)) {
		pefs_enccn_set(pec, NULL, cnp->cn_nameptr, cnp->cn_namelen,
		    cnp);
		return (0);
	}

	PEFSDEBUG("pefs_enccn_lookup: name=%.*s op=%d\n",
	    (int)cnp->cn_namelen, cnp->cn_nameptr, (int) cnp->cn_nameiop);

	error = 0;
	dgen = pefs_getgen(dvp, cnp->cn_cred);
	pefs_dircache_lock(dpn->pn_dircache);
	if (pefs_cache_active(dvp) &&
	    pefs_dircache_valid(dpn->pn_dircache, dgen)) {
		cache = pefs_dircache_lookup(dpn->pn_dircache,
		    cnp->cn_nameptr, cnp->cn_namelen);
		goto out;
	}

	offset = 0;
	eofflag = 0;
	cache = NULL;
	ctx = pefs_ctx_get();
	pefs_chunk_create(&pc, dpn, PEFS_SECTOR_SIZE);
	dpn_key = pefs_node_key(dpn);
	pefs_dircache_beginupdate(dpn->pn_dircache, dgen);
	while (!eofflag) {
		uio = pefs_chunk_uio(&pc, offset, UIO_READ);
		error = VOP_READDIR(ldvp, uio, cnp->cn_cred, &eofflag,
		    NULL, NULL);
		if (error != 0)
			break;
		offset = uio->uio_offset;

		if (pc.pc_size == uio->uio_resid)
			break;
		pefs_chunk_setsize(&pc, pc.pc_size - uio->uio_resid);
		pefs_enccn_parsedir(dpn->pn_dircache, ctx, dpn_key,
		    pc.pc_base, pc.pc_size, cnp->cn_nameptr, cnp->cn_namelen,
		    &cache);
		pefs_chunk_restore(&pc);
	}
	pefs_dircache_endupdate(dpn->pn_dircache);

	pefs_ctx_free(ctx);
	pefs_key_release(dpn_key);
	pefs_chunk_free(&pc, dpn);
out:
	if (cache != NULL && error == 0)
		pefs_enccn_set(pec, &cache->pde_tkey,
		    cache->pde_encname, cache->pde_encnamelen, cnp);
	else if (cache == NULL && error == 0)
		error = ENOENT;

	pefs_dircache_unlock(dpn->pn_dircache);

	return (error);
}

static int
pefs_enccn_get(struct pefs_enccn *pec, struct vnode *dvp, struct vnode *vp,
    struct componentname *cnp)
{
	struct pefs_node *dpn = VP_TO_PN(dvp);
	struct pefs_node *pn = VP_TO_PN(vp);
	struct pefs_dircache_entry *cache;
	int error;

	if ((pn->pn_flags & PN_HASKEY) == 0) {
		pefs_enccn_set(pec, NULL, cnp->cn_nameptr, cnp->cn_namelen,
		    cnp);
		return (0);
	}

	if (pefs_cache_active(dvp)) {
		pefs_dircache_lock(dpn->pn_dircache);
		/* Do not check if cache valid check keys are equal instead */
		cache = pefs_dircache_lookup(dpn->pn_dircache,
		    cnp->cn_nameptr, cnp->cn_namelen);
		if (cache != NULL &&
		    pefs_tkey_cmp(&cache->pde_tkey, &pn->pn_tkey) == 0) {
			pefs_enccn_set(pec, &pn->pn_tkey,
			    cache->pde_encname, cache->pde_encnamelen, cnp);
			pefs_dircache_unlock(dpn->pn_dircache);
			return (0);
		}
		pefs_dircache_unlock(dpn->pn_dircache);
	}

	error = pefs_enccn_create(pec, pn->pn_tkey.ptk_key,
	    pn->pn_tkey.ptk_tweak, cnp);
	PEFSDEBUG("pefs_enccn_get: create: %s -> %s\n",
	    cnp->cn_nameptr, pec->pec_cn.cn_nameptr);
	return (error);
}

#ifdef PEFS_DEBUG_EXTRA
static void
__pefs_enccn_assert_noent(struct vnode *dvp, struct componentname *cnp,
    const char *file, int line)
{
	struct pefs_enccn enccn;
	int error;

	pefs_enccn_init(&enccn);
	error = pefs_enccn_lookup(&enccn, dvp, cnp);

	if (error != ENOENT)
		panic("pefs_enccn assertion failed: %s:%d: "
		    "unexpected error %d: %*s\n",
		    file, line, error, (int)cnp->cn_namelen, cnp->cn_nameptr);
	pefs_enccn_free(&enccn);
}

#define	PEFS_ENCCN_ASSERT_NOENT(dvp, cnp)	\
		__pefs_enccn_assert_noent((dvp), (cnp), __FILE__, __LINE__);
#else
#define	PEFS_ENCCN_ASSERT_NOENT(dvp, cnp)	\
		do { } while(0)
#endif /* PEFS_DEBUG_EXTRA */

#define	PEFS_FLUSHKEY_ALL		1

/*
 * Recycle vnodes with key pk.
 *
 * pk == NULL => recycle vnodes without key
 * flags & PEFS_FLUSHKEY_ALL => recycle all vnodes with key
 */
static int
pefs_flushkey(struct mount *mp, struct thread *td, int flags,
    struct pefs_key *pk)
{
	struct vnode *vp, *rootvp, *mvp;
	struct pefs_node *pn;
	int error;

	vflush(mp, 0, 0, td);
	rootvp = VFS_TO_PEFS(mp)->pm_rootvp;
#if __FreeBSD_version >= 1000025
loop:
	MNT_VNODE_FOREACH_ACTIVE(vp, mp, mvp) {
		if ((vp->v_type != VREG && vp->v_type != VDIR) ||
		    vp == rootvp) {
			VI_UNLOCK(vp);
			continue;
		}
		error = vget(vp, LK_EXCLUSIVE | LK_INTERLOCK, td);
		if (error != 0) {
			if (error == ENOENT) {
				MNT_VNODE_FOREACH_ACTIVE_ABORT(mp, mvp);
				goto loop;
			}
			continue;
		}
		pn = VP_TO_PN(vp);
		if (((pn->pn_flags & PN_HASKEY) != 0 &&
		    ((flags & PEFS_FLUSHKEY_ALL) != 0 ||
		    pn->pn_tkey.ptk_key == pk)) ||
		    ((pn->pn_flags & PN_HASKEY) == 0 && pk == NULL)) {
			vgone(vp);
		}
		vput(vp);
	}
#else
	MNT_ILOCK(mp);
loop:
	MNT_VNODE_FOREACH(vp, mp, mvp) {
		if ((vp->v_type != VREG && vp->v_type != VDIR) || vp == rootvp)
			continue;
		VI_LOCK(vp);
		pn = VP_TO_PN(vp);
		if (((pn->pn_flags & PN_HASKEY) &&
		    ((flags & PEFS_FLUSHKEY_ALL) ||
		    pn->pn_tkey.ptk_key == pk)) ||
		    ((pn->pn_flags & PN_HASKEY) == 0 && pk == NULL)) {
			vholdl(vp);
			MNT_IUNLOCK(mp);
			error = vn_lock(vp, LK_INTERLOCK | LK_EXCLUSIVE);
			if (error != 0) {
				vdrop(vp);
				MNT_ILOCK(mp);
				MNT_VNODE_FOREACH_ABORT_ILOCKED(mp, mvp);
				goto loop;
			}
			PEFSDEBUG("pefs_flushkey: pk=%p, vp=%p\n", pk, vp);
			vgone(vp);
			VOP_UNLOCK(vp, 0);
			vdrop(vp);
			MNT_ILOCK(mp);
		} else
			VI_UNLOCK(vp);
	}
	MNT_IUNLOCK(mp);
#endif

	cache_purgevfs(mp);
	return (0);
}

static int
pefs_lookup(struct vop_cachedlookup_args *ap)
{
	struct componentname *cnp = ap->a_cnp;
	struct vnode *vp = NULL;
	struct vnode *lvp = NULL;
	struct vnode *dvp = ap->a_dvp;
	struct vnode *ldvp = PEFS_LOWERVP(dvp);
	struct pefs_enccn enccn;
	struct pefs_node *dpn = VP_TO_PN(dvp);
	uint64_t flags = cnp->cn_flags;
	int nokey_lookup, skip_lookup;
	int error;

	PEFSDEBUG("pefs_lookup: op=%lx, name=%.*s\n",
	    cnp->cn_nameiop, (int)cnp->cn_namelen, cnp->cn_nameptr);

	pefs_enccn_init(&enccn);

	if ((flags & ISLASTCN) &&
	    ((dvp->v_mount->mnt_flag & MNT_RDONLY) || pefs_no_keys(dvp)) &&
	    (cnp->cn_nameiop != LOOKUP))
		return (EROFS);

	nokey_lookup = 0;
	skip_lookup = (flags & ISDOTDOT) ||
	    pefs_name_skip(cnp->cn_nameptr, cnp->cn_namelen);
	if (((dpn->pn_flags & PN_HASKEY) == 0 || skip_lookup)) {
		error = VOP_LOOKUP(ldvp, &lvp, cnp);
		if (skip_lookup || error == 0 ||
		    pefs_no_keys(dvp))
			nokey_lookup = 1;
	}

	if (!nokey_lookup) {
		error = pefs_enccn_lookup(&enccn, dvp, cnp);
		if (error == ENOENT && (cnp->cn_flags & ISLASTCN) &&
		    (cnp->cn_nameiop == CREATE || cnp->cn_nameiop == RENAME ||
		    (cnp->cn_nameiop == DELETE &&
		    (cnp->cn_flags & DOWHITEOUT) &&
		    (cnp->cn_flags & ISWHITEOUT)))) {
			/*
			 * Some file systems (like ufs) update internal inode
			 * fields during VOP_LOOKUP which are later used by
			 * VOP_CREATE, VOP_MKDIR, etc. That's why we can't
			 * return EJUSTRETURN here and have to perform
			 * VOP_LOOKUP(ldvp).
			 * Attention should also be paid not to unlock dvp.
			 *
			 * XXX We also need to have a valid encrypted cnp. Real
			 * encrypted cnp will be created anyway, encrypted name
			 * length should just be the same here.
			 */
			error = pefs_enccn_create_node(&enccn, dvp, cnp);
		}

		if (error == 0) {
			error = VOP_LOOKUP(ldvp, &lvp, &enccn.pec_cn);
			if (error == 0 || error == EJUSTRETURN)
				cnp->cn_flags = (cnp->cn_flags & HASBUF) |
				    (enccn.pec_cn.cn_flags & ~HASBUF);
			if (error != 0)
				PEFSDEBUG("pefs_lookup: lower error = %d\n",
				    error);
		} else
			PEFSDEBUG("pefs_lookup: pefs_enccn_lookup error = %d\n",
			    error);
	}

	if ((error == 0 || error == EJUSTRETURN) && (flags & ISLASTCN) &&
	    cnp->cn_nameiop != LOOKUP)
		cnp->cn_flags |= SAVENAME;
	if (error == ENOENT && (cnp->cn_flags & MAKEENTRY) &&
	    cnp->cn_nameiop != CREATE)
		cache_enter(dvp, NULLVP, cnp);
	else if ((error == 0 || error == EJUSTRETURN) && lvp != NULL) {
		if (ldvp == lvp) {
			*ap->a_vpp = dvp;
			VREF(dvp);
			vrele(lvp);
		} else {
			if (nokey_lookup)
				error = pefs_node_get_nokey(dvp->v_mount, lvp,
				    &vp);
			else
				error = pefs_node_get_haskey(dvp->v_mount, lvp,
				    &vp, &enccn.pec_tkey);
			if (error != 0) {
				vput(lvp);
			} else {
				*ap->a_vpp = vp;
				if ((cnp->cn_flags & MAKEENTRY) &&
				    cnp->cn_nameiop != CREATE)
					cache_enter(dvp, vp, cnp);
			}
		}
	} else
		*ap->a_vpp = NULL;

	if (!nokey_lookup)
		pefs_enccn_free(&enccn);

	PEFSDEBUG("pefs_lookup: op=%lx, name=%.*s error=%d vp=%p\n",
	    cnp->cn_nameiop, (int)cnp->cn_namelen, cnp->cn_nameptr, error,
	    *ap->a_vpp);

	return (error);
}

static int
pefs_open(struct vop_open_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct vnode *lvp = PEFS_LOWERVP(vp);
	struct pefs_node *pn = VP_TO_PN(vp);
	int error;

	if (pefs_no_keys(vp) && (ap->a_mode & (FWRITE | O_APPEND)))
		return (EROFS);

	error = VOP_OPEN(lvp, ap->a_mode, ap->a_cred, ap->a_td, ap->a_fp);
	if (error == 0) {
		if ((pn->pn_flags & PN_HASKEY) == 0)
			vp->v_object = lvp->v_object;
		else
			vnode_create_vobject(vp, 0, ap->a_td);
	}
	return (error);
}

static int
pefs_truncate(struct vnode *vp, u_quad_t nsize, struct ucred *cred)
{
	struct vnode *lvp = PEFS_LOWERVP(vp);
	struct vattr va;
	struct uio *puio;
	struct pefs_node *pn = VP_TO_PN(vp);
	struct pefs_chunk pc;
	u_quad_t osize, diff;
	size_t oskip, nskip;
	int error;

	MPASS(vp->v_type == VREG);
	MPASS(pn->pn_flags & PN_HASKEY);

	error = VOP_GETATTR(lvp, &va, cred);
	if (error != 0)
		return (error);
	osize = va.va_size;

	if (nsize == osize)
		return (0);

	if ((va.va_flags & (IMMUTABLE | APPEND)) != 0)
		return (EPERM);

	if (VOP_ISLOCKED(vp) != LK_EXCLUSIVE) {
		vn_lock(vp, LK_UPGRADE | LK_RETRY);
		error = VOP_GETATTR(lvp, &va, cred);
		if (error != 0)
			return (error);
		osize = va.va_size;
		if (nsize == osize)
			return (0);
	}

	PEFSDEBUG("pefs_truncate: old size 0x%jx, new size 0x%jx\n",
	    osize, nsize);

	oskip = osize & PEFS_SECTOR_MASK;
	nskip = nsize & PEFS_SECTOR_MASK;
	pefs_chunk_create(&pc, pn, PEFS_SECTOR_SIZE);

	if (nsize < osize && nskip != 0) {
		pefs_chunk_setsize(&pc, nskip);
		puio = pefs_chunk_uio(&pc, nsize - nskip, UIO_READ);
		PEFSDEBUG("pefs_truncate: shrinking file: "
		    "offset=0x%jx, resid=0x%jx\n",
		    puio->uio_offset, puio->uio_resid);
		error = pefs_read_int(vp, puio, IO_UNIT, cred, osize);
		if (error != 0)
			goto out;
		MPASS(puio->uio_resid == 0);
		puio = pefs_chunk_uio(&pc, nsize - nskip, UIO_WRITE);
		error = pefs_write_int(vp, puio, IO_UNIT, cred, nsize - nskip);
		if (error != 0)
			goto out;
	}

	VATTR_NULL(&va);
	va.va_size = nsize;
	error = VOP_SETATTR(lvp, &va, cred);
	if (error != 0)
		goto out;
	vnode_pager_setsize(vp, nsize);

	if (nsize < osize)
		goto out;

	diff = nsize - osize;
	if (oskip != 0 || diff < PEFS_SECTOR_SIZE) {
		pefs_chunk_setsize(&pc, qmin(PEFS_SECTOR_SIZE - oskip, diff));
		pefs_chunk_zero(&pc);
		puio = pefs_chunk_uio(&pc, osize, UIO_WRITE);
		PEFSDEBUG("pefs_truncate: extending file: "
		    "offset=0x%jx, resid=0x%jx\n",
		    puio->uio_offset, puio->uio_resid);
		error = pefs_write_int(vp, puio, IO_UNIT, cred, osize);
		if (error != 0 || pc.pc_size == diff)
			goto out;
	}

	if (nskip != 0) {
		pefs_chunk_setsize(&pc, nskip);
		pefs_chunk_zero(&pc);
		puio = pefs_chunk_uio(&pc, nsize - nskip, UIO_WRITE);
		PEFSDEBUG("pefs_truncate: extending file: "
		    "offset=0x%jx, resid=0x%jx\n",
		    puio->uio_offset, puio->uio_resid);
		error = pefs_write_int(vp, puio, IO_UNIT, cred, nsize - nskip);
	}

out:
	pefs_chunk_free(&pc, pn);

	return (error);
}

/*
 * Setattr call. Disallow write attempts if the layer is mounted read-only.
 */
static int
pefs_setattr(struct vop_setattr_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct ucred *cred = ap->a_cred;
	struct vattr *vap = ap->a_vap;
	int error;

	if ((vap->va_flags != VNOVAL || vap->va_uid != (uid_t)VNOVAL ||
	    vap->va_gid != (gid_t)VNOVAL || vap->va_atime.tv_sec != VNOVAL ||
	    vap->va_mtime.tv_sec != VNOVAL || vap->va_mode != (mode_t)VNOVAL) &&
	    (vp->v_mount->mnt_flag & MNT_RDONLY || pefs_no_keys(vp)))
		return (EROFS);

	if (vap->va_size != VNOVAL) {
		switch (vp->v_type) {
		case VDIR:
			return (EISDIR);
		case VCHR:
		case VBLK:
		case VSOCK:
		case VFIFO:
			if (vap->va_flags != VNOVAL)
				return (EOPNOTSUPP);
			return (0);
		case VREG:
		case VLNK:
			/*
			 * Disallow write attempts if the file system is
			 * mounted read-only.
			 */
			if ((vp->v_mount->mnt_flag & MNT_RDONLY) ||
			    pefs_no_keys(vp))
				return (EROFS);
			/* Bypass size change for node without key */
			if ((VP_TO_PN(vp)->pn_flags & PN_HASKEY) == 0)
				break;
			if (vp->v_type == VREG)
				error = pefs_truncate(vp, vap->va_size, cred);
			else
				error = EOPNOTSUPP; /* TODO */
			if (error != 0)
				return (error);
			vnode_pager_setsize(vp, vap->va_size);
			break;
		default:
			return (EOPNOTSUPP);
		}
	}

	return (VOP_SETATTR(PEFS_LOWERVP(vp), vap, cred));
}

/*
 *  We handle getattr only to change the fsid.
 */
static int
pefs_getattr(struct vop_getattr_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct vattr *vap = ap->a_vap;
	int error;

	error = VOP_GETATTR(PEFS_LOWERVP(vp), vap, ap->a_cred);
	if (error != 0)
		return (error);

	vap->va_fsid = vp->v_mount->mnt_stat.f_fsid.val[0];
	if (vap->va_type == VLNK)
		vap->va_size = PEFS_NAME_PTON_SIZE(vap->va_size);
	return (0);
}

/*
 * Handle to disallow write access if mounted read-only.
 */
static int
pefs_access_checkmode(struct vnode *vp, accmode_t accmode)
{
	/*
	 * Disallow write attempts on read-only layers;
	 * unless the file is a socket, fifo, or a block or
	 * character device resident on the file system.
	 */
	if (accmode & VWRITE) {
		switch (vp->v_type) {
		case VDIR:
		case VLNK:
		case VREG:
			if ((vp->v_mount->mnt_flag & MNT_RDONLY) != 0 ||
			    pefs_no_keys(vp))
				return (EROFS);
			break;
		default:
			break;
		}
	}

	return (0);
}

static int
pefs_access(struct vop_access_args *ap)
{
	struct vnode *vp = ap->a_vp;
	accmode_t accmode = ap->a_accmode;
	int error;

	error = pefs_access_checkmode(vp, accmode);
	if (error != 0)
		return (error);
	error = VOP_ACCESS(PEFS_LOWERVP(vp), accmode, ap->a_cred, ap->a_td);
	return (error);
}

static int
pefs_accessx(struct vop_accessx_args *ap)
{
	struct vnode *vp = ap->a_vp;
	accmode_t accmode = ap->a_accmode;
	int error;

	error = pefs_access_checkmode(vp, accmode);
	if (error != 0)
		return (error);
	error = VOP_ACCESSX(PEFS_LOWERVP(vp), accmode, ap->a_cred, ap->a_td);
	return (error);
}

static int
pefs_getacl(struct vop_getacl_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct vnode *lvp = PEFS_LOWERVP(vp);
	int error;

	ap->a_vp = lvp;
	error = VOP_GETACL_AP(ap);
	ap->a_vp = vp;
	return (error);
}

static int
pefs_close(struct vop_close_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct vnode *lvp = PEFS_LOWERVP(vp);
	int error;

	ap->a_vp = lvp;
	error = VOP_CLOSE_AP(ap);
	ap->a_vp = vp;
	return (error);
}

static int
pefs_isemptydir_parsedir(void *mem, size_t sz)
{
	struct dirent *de;

	for (de = (struct dirent*) mem; sz > DIRENT_MINSIZE;
			sz -= de->d_reclen,
			de = (struct dirent *)(((caddr_t)de) + de->d_reclen)) {
		MPASS(de->d_reclen <= sz);
		if (de->d_reclen == 0)
			break;
		if (de->d_type == DT_WHT || de->d_fileno == 0)
			continue;
		if (de->d_name[0] == '.' && (de->d_namlen == 1 ||
		    (de->d_namlen == 2 && de->d_name[1] == '.')))
			continue;
		return (ENOTEMPTY);
	}
	return (0);
}

static int
pefs_isemptydir(struct vnode *dvp, struct ucred *cred)
{
	struct vnode *ldvp = PEFS_LOWERVP(dvp);
	struct uio *uio;
	struct pefs_node *dpn = VP_TO_PN(dvp);
	struct pefs_chunk pc;
	off_t offset;
	int eofflag, error;

	MPASS(dvp->v_type == VDIR);

	offset = 0;
	eofflag = 0;
	error = 0;
	ldvp = PEFS_LOWERVP(dvp);
	pefs_chunk_create(&pc, dpn, PEFS_SECTOR_SIZE);
	while (!eofflag) {
		uio = pefs_chunk_uio(&pc, offset, UIO_READ);
		error = VOP_READDIR(ldvp, uio, cred, &eofflag, NULL, NULL);
		if (error != 0)
			break;
		offset = uio->uio_offset;
		if (pc.pc_size == uio->uio_resid)
			break;
		pefs_chunk_setsize(&pc, pc.pc_size - uio->uio_resid);
		error = pefs_isemptydir_parsedir(pc.pc_base, pc.pc_size);
		if (error != 0)
			break;
		pefs_chunk_restore(&pc);
	}
	pefs_chunk_free(&pc, dpn);
	return (error);
}

static int
pefs_rename(struct vop_rename_args *ap)
{
	struct vnode *fdvp = ap->a_fdvp;
	struct vnode *lfdvp = PEFS_LOWERVP(fdvp);
	struct vnode *fvp = ap->a_fvp;
	struct vnode *lfvp = PEFS_LOWERVP(fvp);
	struct vnode *tdvp = ap->a_tdvp;
	struct vnode *ltdvp = PEFS_LOWERVP(tdvp);
	struct vnode *tvp = ap->a_tvp;
	struct vnode *ltvp = (tvp == NULL ? NULL : PEFS_LOWERVP(tvp));
	struct componentname *fcnp = ap->a_fcnp;
	struct componentname *tcnp = ap->a_tcnp;
	struct pefs_enccn fenccn;
	struct pefs_enccn tenccn;
	struct pefs_enccn txenccn;
	int error;

	KASSERT(tcnp->cn_flags & (SAVENAME | SAVESTART),
	    ("pefs_rename: no name"));
	KASSERT(fcnp->cn_flags & (SAVENAME | SAVESTART),
	    ("pefs_rename: no name"));

	pefs_enccn_init(&fenccn);
	pefs_enccn_init(&tenccn);
	pefs_enccn_init(&txenccn);

	/* Check for cross-device rename. */
	if ((fvp->v_mount != tdvp->v_mount) ||
	    (tvp && (fvp->v_mount != tvp->v_mount))) {
		error = EXDEV;
		goto out_locked;
	}

	/* Handle '.' and '..' rename attempt */
	if ((fcnp->cn_namelen == 1 && fcnp->cn_nameptr[0] == '.') ||
	    (fcnp->cn_flags & ISDOTDOT) || (tcnp->cn_flags & ISDOTDOT) ||
	    fdvp == fvp) {
		error = EINVAL;
		goto out_locked;
	}

	if (pefs_no_keys(tdvp)) {
		error = EROFS;
		goto out_locked;
	}

	if ((VP_TO_PN(fvp)->pn_flags & PN_HASKEY) == 0) {
		PEFSDEBUG("pefs_rename: source !HASKEY: %s\n",
		    fcnp->cn_nameptr);
		if ((VP_TO_PN(tdvp)->pn_flags & PN_HASKEY) == 0) {
			PEFSDEBUG("pefs_rename: target dir !HASKEY: %s\n",
			    tcnp->cn_nameptr);
			/* Allow unencrypted to unencrypted rename. */
			vref(lfdvp);
			vref(lfvp);
			vref(ltdvp);
			if (ltvp != NULL)
				vref(ltvp);
			error = VOP_RENAME(lfdvp, lfvp, fcnp, ltdvp, ltvp,
			    tcnp);
			goto out_unlocked;
		}
		/* Target directory is encrypted. Files should be recreated. */
		error = EXDEV;
		goto out_locked;
	}

	error = pefs_enccn_get(&fenccn, fdvp, fvp, fcnp);
	if (error != 0)
		goto out_locked;
	error = pefs_enccn_create(&tenccn, fenccn.pec_tkey.ptk_key,
	    fenccn.pec_tkey.ptk_tweak, tcnp);
	if (error != 0)
		goto out_locked;

	if (tvp != NULL) {
		if (fvp->v_type == VDIR && tvp->v_type != VDIR) {
			error = ENOTDIR;
			goto out_locked;
		} else if (fvp->v_type != VDIR && tvp->v_type == VDIR) {
			error = EISDIR;
			goto out_locked;
		} else {
			/*
			 * We end up having 2 files with same name but
			 * different tweaks/keys. If target is directory verify
			 * it's empty. Set ltvp to zero here because we rename
			 * to new name and then remove old one.
			 */
			if (tvp->v_type == VDIR) {
				error = pefs_isemptydir(tvp, tcnp->cn_cred);
				if (error != 0)
					goto out_locked;
			}

			error = pefs_enccn_get(&txenccn, tdvp, tvp, tcnp);
			if (error != 0)
				goto out_locked;

			ltvp = NULL;
			VP_TO_PN(tvp)->pn_flags |= PN_WANTRECYCLE;
			cache_purge(tvp);
			VOP_UNLOCK(tvp, 0);

			error = VOP_LOOKUP(ltdvp, &ltvp, &tenccn.pec_cn);
			if (error == 0)
				panic("pefs_rename: "
				    "dummy rename target exists");
			error = 0;
		}
	}

	MPASS(ltvp == NULL);
	MPASS(error == 0);
	ASSERT_VOP_LOCKED(ltdvp, "pefs_rename");

	vref(lfdvp);
	vref(lfvp);
	vref(ltdvp);

	error = VOP_RENAME(lfdvp, lfvp, &fenccn.pec_cn, ltdvp, NULL,
	    &tenccn.pec_cn);

	if (error == 0) {
		if (tvp != NULL) {
			/*
			 * Remove dummy target file.
			 */
			ASSERT_VOP_UNLOCKED(tdvp, "pefs_rename");
			ASSERT_VOP_UNLOCKED(tvp, "pefs_rename");
			vn_lock(tdvp, LK_EXCLUSIVE | LK_RETRY);
			txenccn.pec_cn.cn_nameiop = DELETE;
			error = VOP_LOOKUP(ltdvp, &ltvp, &txenccn.pec_cn);
			if (error == 0) {
				if (tvp->v_type != VDIR)
					error = VOP_REMOVE(ltdvp, ltvp,
					    &txenccn.pec_cn);
				else
					error = VOP_RMDIR(ltdvp, ltvp,
					    &txenccn.pec_cn);
				PEFSDEBUG("pefs_rename: remove old: %s\n",
				    txenccn.pec_cn.cn_nameptr);
				vput(ltvp);
			}
			VOP_UNLOCK(tdvp, 0);
		}
		cache_purge(fdvp);
		cache_purge(fvp);
	}

out_unlocked:
	ASSERT_VOP_UNLOCKED(tdvp, "pefs_rename");
	vrele(fdvp);
	vrele(fvp);
	vrele(tdvp);
	if (tvp != NULL)
		vrele(tvp);
	goto out;

out_locked:
	if (tdvp == tvp)
		vrele(tdvp);
	else
		vput(tdvp);
	if (tvp != NULL)
		vput(tvp);
	vrele(fdvp);
	vrele(fvp);

out:
	pefs_enccn_free(&tenccn);
	pefs_enccn_free(&txenccn);
	pefs_enccn_free(&fenccn);

	return (error);
}

/*
 * We need to process our own vnode lock and then clear the
 * interlock flag as it applies only to our vnode, not the
 * vnodes below us on the stack.
 */
static int
pefs_lock(struct vop_lock1_args *ap)
{
	struct vnode *vp = ap->a_vp;
	int flags = ap->a_flags;
	struct pefs_mount *pm;
	struct pefs_node *pn;
	struct vnode *lvp;
	int error;


	if ((flags & LK_INTERLOCK) == 0) {
		VI_LOCK(vp);
		ap->a_flags = flags |= LK_INTERLOCK;
	}
	pn = (struct pefs_node *)vp->v_data;
	/*
	 * If we're still active we must ask the lower layer to
	 * lock as ffs has special lock considerations in it's
	 * vop lock.
	 */
	if (pn != NULL && (lvp = pn->pn_lowervp) != NULL) {
		if (vp->v_mount != NULL) {
			pm = VFS_TO_PEFS(vp->v_mount);
			if (pm != NULL &&
			    (pm->pm_flags & PM_ROOT_CANRECURSE) &&
			    pm->pm_rootvp == vp)
				ap->a_flags = flags |= LK_CANRECURSE;
		}
		VI_LOCK_FLAGS(lvp, MTX_DUPOK);
		VI_UNLOCK(vp);
		/*
		 * We have to hold the vnode here to solve a potential
		 * reclaim race.  If we're forcibly vgone'd while we
		 * still have refs, a thread could be sleeping inside
		 * the lowervp's vop_lock routine.  When we vgone we will
		 * drop our last ref to the lowervp, which would allow it
		 * to be reclaimed.  The lowervp could then be recycled,
		 * in which case it is not legal to be sleeping in it's VOP.
		 * We prevent it from being recycled by holding the vnode
		 * here.
		 */
		vholdl(lvp);
		error = VOP_LOCK(lvp, flags);

		/*
		 * We might have slept to get the lock and someone might have
		 * cleaned our vnode already, switching vnode lock from one in
		 * lowervp to v_lock in our own vnode structure.  Handle this
		 * case by reacquiring correct lock in requested mode.
		 */
		if (vp->v_data == NULL && error == 0) {
			ap->a_flags &= ~(LK_TYPE_MASK | LK_INTERLOCK);
			switch (flags & LK_TYPE_MASK) {
			case LK_SHARED:
				ap->a_flags |= LK_SHARED;
				break;
			case LK_UPGRADE:
			case LK_EXCLUSIVE:
				ap->a_flags |= LK_EXCLUSIVE;
				break;
			default:
				panic("pefs_lock: "
				    "unsupported lock request %d\n",
				    ap->a_flags);
			}
			VOP_UNLOCK(lvp, 0);
			error = vop_stdlock(ap);
		}
		vdrop(lvp);
	} else
		error = vop_stdlock(ap);

	return (error);
}

/*
 * We need to process our own vnode unlock and then clear the
 * interlock flag as it applies only to our vnode, not the
 * vnodes below us on the stack.
 */
static int
pefs_unlock(struct vop_unlock_args *ap)
{
	struct vnode *vp = ap->a_vp;
	int flags = ap->a_flags;
	int mtxlkflag = 0;
	struct pefs_node *pn;
	struct vnode *lvp;
	int error;

	if ((flags & LK_INTERLOCK) != 0)
		mtxlkflag = 1;
	else if (mtx_owned(VI_MTX(vp)) == 0) {
		VI_LOCK(vp);
		mtxlkflag = 2;
	}
	pn = (struct pefs_node *)vp->v_data;
	if (pn != NULL && (lvp = pn->pn_lowervp) != NULL) {
		VI_LOCK_FLAGS(lvp, MTX_DUPOK);
		flags |= LK_INTERLOCK;
		vholdl(lvp);
		VI_UNLOCK(vp);
		error = VOP_UNLOCK(lvp, flags);
		vdrop(lvp);
		if (mtxlkflag == 0)
			VI_LOCK(vp);
	} else {
		if (mtxlkflag == 2)
			VI_UNLOCK(vp);
		error = vop_stdunlock(ap);
	}

	return (error);
}

static int
pefs_inactive(struct vop_inactive_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct pefs_node *pn = VP_TO_PN(vp);

	/*
	 * Buffers can be still be in use because they are protected only by
	 * vnode interlock.
	 * Free remaining buffers in pefs_reclaim.
	 */
	pefs_node_buf_free(pn);

	if ((pn->pn_flags & PN_HASKEY) && vp->v_object != NULL) {
		if (vp->v_object->resident_page_count > 0)
			PEFSDEBUG("pefs_inactive: vobject has dirty pages: "
			    "vp=%p count=%d\n",
			    vp, vp->v_object->resident_page_count);
#if __FreeBSD_version >= 1000030
		VM_OBJECT_WLOCK(vp->v_object);
		vm_object_page_clean(vp->v_object, 0, 0, OBJPC_SYNC);
		VM_OBJECT_WUNLOCK(vp->v_object);
#else
		VM_OBJECT_LOCK(vp->v_object);
		vm_object_page_clean(vp->v_object, 0, 0, OBJPC_SYNC);
		VM_OBJECT_UNLOCK(vp->v_object);
#endif
	}

	if ((pn->pn_flags & PN_WANTRECYCLE) || (pn->pn_flags & PN_HASKEY) == 0)
#if __FreeBSD_version >= 1000011
		vrecycle(vp);
#else
		vrecycle(vp, ap->a_td);
#endif

	return (0);
}

static int
pefs_reclaim(struct vop_reclaim_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct pefs_node *pn = VP_TO_PN(vp);
	struct vnode *lowervp = pn->pn_lowervp;

	PEFSDEBUG("pefs_reclaim: vp=%p\n", vp);

	if (pn->pn_flags & PN_HASKEY)
		vnode_destroy_vobject(vp);
	else
		vp->v_object = NULL;
	cache_purge(vp);

	/*
	 * Use the interlock to protect the clearing of v_data to
	 * prevent faults in pefs_lock().
	 */

	pefs_node_buf_free(pn);
	VI_LOCK(vp);
#ifdef INVARIANTS
	if ((pn->pn_flags & (PN_LOCKBUF_SMALL | PN_LOCKBUF_LARGE)) != 0)
		printf("pefs_reclaim: node buffer leaked: vp: %p\n", vp);
#endif
	vp->v_data = NULL;
	vp->v_vnlock = &vp->v_lock;
	pn->pn_lowervp = NULL;
	pn->pn_lowervp_dead = lowervp;
	lockmgr(vp->v_vnlock, LK_EXCLUSIVE | LK_INTERLOCK, VI_MTX(vp));
	if (lowervp == NULL)
		panic("pefs_reclaim: reclaiming a node with no lowervp");
	VOP_UNLOCK(lowervp, 0);

	/* Asynchronously release lower vnode and free pefs node. */
	pefs_node_asyncfree(pn);

	return (0);
}

static int
pefs_print(struct vop_print_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct pefs_node *pn = VP_TO_PN(vp);

	printf("\tvp=%p, lowervp=%p, flags=%04d\n", vp,
	    pn->pn_lowervp, pn->pn_flags);
	return (0);
}

static int
pefs_getwritemount(struct vop_getwritemount_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct vnode *lowervp;
	struct pefs_node *pn;

	VI_LOCK(vp);
	pn = VP_TO_PN(vp);
	if (pn != NULL && (lowervp = pn->pn_lowervp) != NULL) {
		VI_LOCK_FLAGS(lowervp, MTX_DUPOK);
		VI_UNLOCK(vp);
		vholdl(lowervp);
		VI_UNLOCK(lowervp);
		VOP_GETWRITEMOUNT(lowervp, ap->a_mpp);
		vdrop(lowervp);
	} else {
		VI_UNLOCK(vp);
		*(ap->a_mpp) = NULL;
	}
	return (0);
}

static int
pefs_vptofh(struct vop_vptofh_args *ap)
{
	struct vnode *lvp = PEFS_LOWERVP(ap->a_vp);

	return (VOP_VPTOFH(lvp, ap->a_fhp));
}

static void
pefs_readdir_decrypt(struct pefs_dircache *pd, struct pefs_ctx *ctx,
    struct pefs_key *pk, int dflags, void *mem, size_t *psize)
{
	struct pefs_dircache_entry *cache;
	struct dirent *de, *de_next;
	size_t sz;

	for (de = (struct dirent*) mem, sz = *psize; sz > DIRENT_MINSIZE;
	    de = de_next) {
		MPASS(de->d_reclen <= sz);
		if (de->d_reclen == 0)
			break;
		sz -= de->d_reclen;
		de_next = (struct dirent *)(((caddr_t)de) + de->d_reclen);
		if (de->d_type == DT_WHT || de->d_fileno == 0)
			continue;
		if (pefs_name_skip(de->d_name, de->d_namlen))
			continue;
		cache = pefs_cache_dirent(pd, de, ctx, pk);
		if (cache != NULL) {
			/* Do not change d_reclen */
			MPASS(cache->pde_namelen + 1 <= de->d_namlen);
			memcpy(de->d_name, cache->pde_name,
			    cache->pde_namelen + 1);
			de->d_namlen = cache->pde_namelen;
		} else if (dflags & PN_HASKEY) {
			*psize -= de->d_reclen;
			memcpy(de, de_next, sz);
			de_next = de;
		}
	}
}

static int
pefs_readdir(struct vop_readdir_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct vnode *lvp = PEFS_LOWERVP(vp);
	struct uio *uio = ap->a_uio;
	struct ucred *cred = ap->a_cred;
	int *eofflag = ap->a_eofflag;
	struct uio *puio;
	struct pefs_node *pn = VP_TO_PN(vp);
	struct pefs_key *pn_key;
	struct pefs_chunk pc;
	struct pefs_ctx *ctx;
	size_t mem_size;
	u_long gen;
	int error;
	int r_ncookies = 0, r_ncookies_max = 0, ncookies = 0;
	u_long *r_cookies = NULL, *cookies = NULL;
	int *a_ncookies;
	u_long **a_cookies;

	if (pefs_no_keys(vp)) {
		error = VOP_READDIR(lvp, uio, cred, eofflag, ap->a_ncookies,
		    ap->a_cookies);
		return (error);
	}

	if (ap->a_ncookies == NULL || ap->a_cookies == NULL) {
		a_ncookies = NULL;
		a_cookies = NULL;
	} else {
		a_ncookies = &ncookies;
		a_cookies = &cookies;
	}

	gen = pefs_getgen(vp, cred);
	ctx = pefs_ctx_get();
	pefs_chunk_create(&pc, pn, qmin(uio->uio_resid, DFLTPHYS));
	pn_key = pefs_node_key(pn);
	pefs_dircache_lock(pn->pn_dircache);
	if (!pefs_dircache_valid(pn->pn_dircache, gen) && uio->uio_offset != 0)
		gen = 0;
	pefs_dircache_beginupdate(pn->pn_dircache, gen);
	while (1) {
		if (uio->uio_resid < pc.pc_size)
			pefs_chunk_setsize(&pc, uio->uio_resid);
		puio = pefs_chunk_uio(&pc, uio->uio_offset, uio->uio_rw);
		error = VOP_READDIR(lvp, puio, cred, eofflag,
		    a_ncookies, a_cookies);
		if (error != 0)
			break;

		if (pc.pc_size == puio->uio_resid)
			break;
		pefs_chunk_setsize(&pc, pc.pc_size - puio->uio_resid);
		mem_size = pc.pc_size;
		if (*eofflag == 0)
			pefs_dircache_abortupdate(pn->pn_dircache);
		pefs_readdir_decrypt(pn->pn_dircache, ctx, pn_key, pn->pn_flags,
		    pc.pc_base, &mem_size);
		pefs_chunk_setsize(&pc, mem_size);
		error = pefs_chunk_copy(&pc, 0, uio);
		if (error != 0)
			break;
		uio->uio_offset = puio->uio_offset;

		/* Finish if there is no need to merge cookies */
		if ((*eofflag != 0 || uio->uio_resid < DIRENT_MAXSIZE) &&
		    (a_cookies == NULL || r_cookies == NULL))
			break;

		if (a_cookies != NULL && ncookies != 0) {
			KASSERT(cookies != NULL, ("cookies == NULL"));
			if (r_cookies == NULL) {
				/* Allocate buffer of maximum possible size */
				r_ncookies_max = uio->uio_resid /
				    DIRENT_MINSIZE;
				r_ncookies_max += ncookies;
				r_cookies = malloc(r_ncookies_max *
				    sizeof(u_long),
				    M_TEMP, M_WAITOK);
			}
			PEFSDEBUG("pefs_readdir: merge cookies %d + %d\n",
			    r_ncookies, ncookies);
			KASSERT(r_ncookies + ncookies <= r_ncookies_max,
			    ("cookies buffer is too small"));
			memcpy(r_cookies + r_ncookies, cookies,
			    ncookies * sizeof(u_long));
			r_ncookies += ncookies;
			ncookies = 0;
			free(cookies, M_TEMP);
			cookies = NULL;
		}

		if (*eofflag != 0 || uio->uio_resid < DIRENT_MAXSIZE)
			break;

		pefs_chunk_restore(&pc);
	}
	if (*eofflag != 0 && error == 0)
		pefs_dircache_endupdate(pn->pn_dircache);
	else
		pefs_dircache_abortupdate(pn->pn_dircache);

	if (error == 0 && a_cookies != NULL) {
		if (r_cookies != NULL) {
			*ap->a_cookies = r_cookies;
			*ap->a_ncookies = r_ncookies;
		} else {
			*ap->a_cookies = cookies;
			*ap->a_ncookies = ncookies;
		}
	}

	pefs_dircache_unlock(pn->pn_dircache);
	pefs_ctx_free(ctx);
	pefs_key_release(pn_key);
	pefs_chunk_free(&pc, pn);

	return (error);
}

static int
pefs_mkdir(struct vop_mkdir_args *ap)
{
	struct vnode *dvp = ap->a_dvp;
	struct vnode *lvp;
	struct componentname *cnp = ap->a_cnp;
	struct pefs_enccn enccn;
	int error;

	KASSERT(cnp->cn_flags & SAVENAME, ("pefs_mkdir: no name"));
	if (pefs_no_keys(dvp))
		return (EROFS);
	pefs_enccn_init(&enccn);
	PEFS_ENCCN_ASSERT_NOENT(dvp, cnp);
	error = pefs_enccn_create_node(&enccn, dvp, cnp);
	if (error != 0)
		return (error);

	error = VOP_MKDIR(PEFS_LOWERVP(dvp), &lvp, &enccn.pec_cn, ap->a_vap);
	if (error == 0 && lvp != NULL) {
		error = pefs_node_get_haskey(dvp->v_mount, lvp, ap->a_vpp,
		    &enccn.pec_tkey);
		if (error != 0)
			vput(lvp);
	}

	pefs_enccn_free(&enccn);

	return (error);
}

static int
pefs_rmdir(struct vop_rmdir_args *ap)
{
	struct vnode *dvp = ap->a_dvp;
	struct vnode *vp = ap->a_vp;
	struct componentname *cnp = ap->a_cnp;
	struct pefs_enccn enccn;
	int error;

	KASSERT(cnp->cn_flags & SAVENAME, ("pefs_rmdir: no name"));
	if (pefs_no_keys(vp))
		return (EROFS);
	pefs_enccn_init(&enccn);
	error = pefs_enccn_get(&enccn, dvp, vp, cnp);
	if (error != 0)
		return (error);

	error = VOP_RMDIR(PEFS_LOWERVP(dvp), PEFS_LOWERVP(vp), &enccn.pec_cn);
	VP_TO_PN(vp)->pn_flags |= PN_WANTRECYCLE;

	pefs_enccn_free(&enccn);

	if (error == 0) {
		cache_purge(dvp);
		cache_purge(vp);
	}

	return (error);
}

static int
pefs_create(struct vop_create_args *ap)
{
	struct vnode *dvp = ap->a_dvp;
	struct vnode *lvp;
	struct componentname *cnp = ap->a_cnp;
	struct pefs_enccn enccn;
	int error;

	KASSERT(cnp->cn_flags & SAVENAME, ("pefs_create: no name"));
	if (pefs_no_keys(dvp))
		return (EROFS);
	pefs_enccn_init(&enccn);
	PEFS_ENCCN_ASSERT_NOENT(dvp, cnp);
	error = pefs_enccn_create_node(&enccn, dvp, cnp);
	if (error != 0)
		return (error);

	error = VOP_CREATE(PEFS_LOWERVP(dvp), &lvp, &enccn.pec_cn, ap->a_vap);
	if (error == 0 && lvp != NULL) {
		error = pefs_node_get_haskey(dvp->v_mount, lvp, ap->a_vpp,
		    &enccn.pec_tkey);
		if (error != 0)
			vput(lvp);
	}

	pefs_enccn_free(&enccn);

	return (error);
}

static int
pefs_remove(struct vop_remove_args *ap)
{
	struct vnode *dvp = ap->a_dvp;
	struct vnode *vp = ap->a_vp;
	struct componentname *cnp = ap->a_cnp;
	struct pefs_enccn enccn;
	int error;

	KASSERT(cnp->cn_flags & SAVENAME, ("pefs_remove: no name"));
	if (pefs_no_keys(dvp))
		return (EROFS);
	pefs_enccn_init(&enccn);
	error = pefs_enccn_get(&enccn, dvp, vp, cnp);
	if (error != 0)
		return (error);

	error = VOP_REMOVE(PEFS_LOWERVP(dvp), PEFS_LOWERVP(vp), &enccn.pec_cn);
	VP_TO_PN(vp)->pn_flags |= PN_WANTRECYCLE;

	pefs_enccn_free(&enccn);

	if (error == 0)
		cache_purge(vp);

	return (error);
}

static int
pefs_link(struct vop_link_args *ap)
{
	struct vnode *dvp = ap->a_tdvp;
	struct vnode *vp = ap->a_vp;
	struct componentname *cnp = ap->a_cnp;
	struct pefs_node *pn = VP_TO_PN(vp);
	struct pefs_enccn enccn;
	int error;

	KASSERT(cnp->cn_flags & SAVENAME, ("pefs_link: no name"));
	if (dvp->v_mount != vp->v_mount)
		return (EXDEV);
	if ((pn->pn_flags & PN_HASKEY) == 0 || pefs_no_keys(vp))
		return (EROFS);
	pefs_enccn_init(&enccn);
	PEFS_ENCCN_ASSERT_NOENT(ap->a_tdvp, cnp);
	error = pefs_enccn_create(&enccn, pn->pn_tkey.ptk_key,
	    pn->pn_tkey.ptk_tweak, cnp);
	if (error != 0)
		return (error);

	error = VOP_LINK(PEFS_LOWERVP(dvp), PEFS_LOWERVP(vp), &enccn.pec_cn);

	pefs_enccn_free(&enccn);

	return (error);
}

static int
pefs_symlink(struct vop_symlink_args *ap)
{
	struct vnode *dvp = ap->a_dvp;
	struct vnode *ldvp = PEFS_LOWERVP(dvp);
	struct vnode *lvp;
	struct componentname *cnp = ap->a_cnp;
	struct pefs_node *dpn = VP_TO_PN(dvp);
	struct pefs_enccn enccn;
	struct pefs_chunk pc;
	char *target = ap->a_target;
	char *enc_target, *penc_target;
	size_t penc_target_len;
	size_t target_len;
	int error;

	KASSERT(cnp->cn_flags & SAVENAME, ("pefs_symlink: no name"));
	if (pefs_no_keys(dvp))
		return (EROFS);

	target_len = strlen(ap->a_target);
	penc_target_len = PEFS_NAME_NTOP_SIZE(target_len) + 1;
	if (penc_target_len > MAXPATHLEN)
		return (ENAMETOOLONG);

	pefs_enccn_init(&enccn);
	PEFS_ENCCN_ASSERT_NOENT(dvp, cnp);
	error = pefs_enccn_create_node(&enccn, dvp, cnp);
	if (error != 0)
		return (error);

	pefs_chunk_create(&pc, dpn, target_len);
	enc_target = pc.pc_base;
	penc_target = malloc(penc_target_len, M_PEFSBUF, M_WAITOK);

	memcpy(enc_target, target, target_len);
	pefs_data_encrypt(&enccn.pec_tkey, 0, &pc);
	error = pefs_name_ntop(enc_target, target_len, penc_target,
	    penc_target_len);
	if (error <= 0) {
		PEFSDEBUG("pefs_symlink: pefs_name_ntop error=%d\n", error);
		error = EIO;
		goto out;
	}

	pefs_chunk_free(&pc, dpn);
	enc_target = NULL;

	error = VOP_SYMLINK(ldvp, &lvp, &enccn.pec_cn, ap->a_vap, penc_target);
	if (error == 0) {
		error = pefs_node_get_haskey(dvp->v_mount, lvp, ap->a_vpp,
		    &enccn.pec_tkey);
		if (error != 0)
			vput(lvp);
	}

out:
	pefs_enccn_free(&enccn);
	free(penc_target, M_PEFSBUF);

	return (error);
}

static int
pefs_readlink(struct vop_readlink_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct vnode *lvp = PEFS_LOWERVP(vp);
	struct uio *uio = ap->a_uio;
	struct uio *puio;
	struct pefs_chunk pc;
	struct pefs_node *pn = VP_TO_PN(vp);
	ssize_t target_len;
	int error;

	if ((pn->pn_flags & PN_HASKEY) == 0)
		return (VOP_READLINK(lvp, uio, ap->a_cred));

	MPASS(uio->uio_offset == 0);
	pefs_chunk_create(&pc, pn, MAXPATHLEN);
	puio = pefs_chunk_uio(&pc, 0, uio->uio_rw);
	error = VOP_READLINK(lvp, puio, ap->a_cred);
	if (error == 0) {
		target_len = pc.pc_size - puio->uio_resid;
		target_len = pefs_name_pton(pc.pc_base, target_len, pc.pc_base,
		    target_len);
		if (target_len >= 0) {
			pefs_chunk_setsize(&pc, target_len);
			pefs_data_decrypt(&pn->pn_tkey, 0, &pc);
			uiomove(pc.pc_base, target_len, uio);
		} else
			error = EIO;
	}
	pefs_chunk_free(&pc, pn);

	return (error);
}

static int
pefs_mknod(struct vop_mknod_args *ap)
{
	struct vnode *dvp = ap->a_dvp;
	struct vnode *lvp;
	struct componentname *cnp = ap->a_cnp;
	struct pefs_enccn enccn;
	int error;

	if (pefs_no_keys(dvp))
		return (EROFS);
	pefs_enccn_init(&enccn);
	PEFS_ENCCN_ASSERT_NOENT(dvp, cnp);
	error = pefs_enccn_create_node(&enccn, dvp, cnp);
	if (error != 0)
		return (error);

	error = VOP_MKNOD(PEFS_LOWERVP(dvp), &lvp, &enccn.pec_cn, ap->a_vap);
	if (error == 0 && lvp != NULL) {
		error = pefs_node_get_haskey(dvp->v_mount, lvp, ap->a_vpp,
		    &enccn.pec_tkey);
		if (error != 0)
			vput(lvp);
	}
	pefs_enccn_free(&enccn);

	return (error);
}

static __inline int
pefs_getsize(struct vnode *vp, u_quad_t *sizep, struct ucred *cred)
{
	struct vattr va;
	int error;

	error = VOP_GETATTR(PEFS_LOWERVP(vp), &va, cred);
	if (error == 0)
		*sizep = va.va_size;

	return (error);
}

static __inline int
pefs_ismapped(struct vnode *vp)
{
	vm_object_t object = vp->v_object;

	if (object == NULL)
		return (0);

	if (object->resident_page_count > 0 ||
#if __FreeBSD_version >= 1000030
	    !vm_object_cache_is_empty(object)
#else
	    object->cache != NULL
#endif
	    )
		return (1);
	return (0);
}

static int
pefs_readmapped(struct vnode *vp, struct uio *uio, ssize_t bsize,
    vm_page_t *mp)
{
	vm_page_t m;
	vm_offset_t moffset;
	ssize_t msize;
	int error;

	MPASS(bsize <= PEFS_SECTOR_SIZE);
	*mp = NULL;
	moffset = uio->uio_offset & PEFS_SECTOR_MASK;
	msize = bsize - moffset;

#if __FreeBSD_version >= 1000030
	VM_OBJECT_WLOCK(vp->v_object);
#else
	VM_OBJECT_LOCK(vp->v_object);
#endif
lookupvpg:
	m = vm_page_lookup(vp->v_object,
	    OFF_TO_IDX(uio->uio_offset));
	if (m != NULL && vm_page_is_valid(m, moffset, msize)) {
#if __FreeBSD_version >= 900038
		if ((m->oflags & VPO_BUSY) != 0) {
			/*
			 * Reference the page before unlocking and
			 * sleeping so that the page daemon is less
			 * likely to reclaim it.
			 */
#if __FreeBSD_version >= 900044
			vm_page_aflag_set(m, PGA_REFERENCED);
#else
			vm_page_lock_queues();
			vm_page_flag_set(m, PG_REFERENCED);
#endif
			vm_page_sleep(m, "pefsmr");
			goto lookupvpg;
		}
#else
		if (vm_page_sleep_if_busy(m, FALSE, "pefsmr"))
			goto lookupvpg;
#endif
		vm_page_busy(m);
#if __FreeBSD_version >= 1000030
		VM_OBJECT_WUNLOCK(vp->v_object);
#else
		VM_OBJECT_UNLOCK(vp->v_object);
#endif
		PEFSDEBUG("pefs_read: mapped: "
		    "offset=0x%jx moffset=0x%jx msize=0x%jx\n",
		    uio->uio_offset, (intmax_t)moffset, (intmax_t)msize);
		error = uiomove_fromphys(&m, moffset, msize, uio);
#if __FreeBSD_version >= 1000030
		VM_OBJECT_WLOCK(vp->v_object);
		vm_page_wakeup(m);
		VM_OBJECT_WUNLOCK(vp->v_object);
#else
		VM_OBJECT_LOCK(vp->v_object);
		vm_page_wakeup(m);
		VM_OBJECT_UNLOCK(vp->v_object);
#endif
		if (error != 0) {
			MPASS(error != EJUSTRETURN);
			return (error);
		}
		return (EJUSTRETURN);
	}
	if (m != NULL && uio->uio_segflg == UIO_NOCOPY) {
#if __FreeBSD_version >= 900036
		if ((m->oflags & VPO_BUSY) != 0) {
			/*
			 * Reference the page before unlocking and
			 * sleeping so that the page daemon is less
			 * likely to reclaim it.
			 */
#if __FreeBSD_version >= 900044
			vm_page_aflag_set(m, PGA_REFERENCED);
#else
			vm_page_lock_queues();
			vm_page_flag_set(m, PG_REFERENCED);
#endif
			vm_page_sleep(m, "pefsmr");
			goto lookupvpg;
		}
#else
		if (vm_page_sleep_if_busy(m, FALSE, "pefsmr"))
			goto lookupvpg;
#endif /* __FreeBSD_version */
		vm_page_busy(m);
		*mp = m;
	}
#if __FreeBSD_version >= 1000030
	VM_OBJECT_WUNLOCK(vp->v_object);
#else
	VM_OBJECT_UNLOCK(vp->v_object);
#endif
	return (0);
}

static __inline ssize_t
pefs_bufsize(struct uio *uio, ssize_t maxsize)
{
	return (qmin(roundup2(uio->uio_resid, PEFS_SECTOR_SIZE), maxsize));
}

static int
pefs_read(struct vop_read_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct vnode *lvp = PEFS_LOWERVP(vp);
	struct uio *uio = ap->a_uio;
	struct ucred *cred = ap->a_cred;
	struct pefs_node *pn = VP_TO_PN(vp);
	u_quad_t fsize;
	int ioflag = ap->a_ioflag;
	int error;

	if (vp->v_type == VDIR)
		return (EISDIR);
	if ((pn->pn_flags & PN_HASKEY) == 0 || vp->v_type == VFIFO)
		return (VOP_READ(lvp, uio, ioflag, cred));
	if (vp->v_type != VREG)
		return (EOPNOTSUPP);
	if (uio->uio_resid == 0)
		return (0);
	if (uio->uio_offset < 0)
		return (EINVAL);

	error = pefs_getsize(vp, &fsize, cred);
	if (error != 0)
		return (error);

	error = pefs_read_int(vp, uio, ioflag, cred, fsize);
	return (error);
}

static int
pefs_read_int(struct vnode *vp, struct uio *uio, int ioflag, struct ucred *cred,
    u_quad_t fsize)
{
	struct vnode *lvp = PEFS_LOWERVP(vp);
	struct uio *puio;
	struct pefs_node *pn = VP_TO_PN(vp);
	struct pefs_chunk pc;
	struct sf_buf *sf;
	vm_page_t m;
	char *ma;
	ssize_t bsize, bskip, done;
	off_t poffset;
	int error = 0, mapped, nocopy;

	MPASS(vp->v_type == VREG);
	MPASS(uio->uio_resid != 0);
	MPASS(uio->uio_offset >= 0);

	mapped = pefs_ismapped(vp);
	if (mapped != 0)
		bsize = PEFS_SECTOR_SIZE;
	else
		bsize = pefs_bufsize(uio, DFLTPHYS);

	pefs_chunk_create(&pc, pn, bsize);
	m = NULL;
	nocopy = 0;
	while (uio->uio_resid > 0 && uio->uio_offset < fsize) {
		MPASS(nocopy == 0);
		bskip = uio->uio_offset & PEFS_SECTOR_MASK;
		poffset = uio->uio_offset - bskip;
		bsize = pefs_bufsize(uio, bsize);
		bsize = qmin(fsize - poffset, bsize);

		if (mapped != 0) {
			error = pefs_readmapped(vp, uio, bsize, &m);
			if (error == EJUSTRETURN) {
				error = 0;
				continue;
			} else if (error != 0)
				break;
			if (m != NULL && uio->uio_segflg == UIO_NOCOPY)
				nocopy = 1;
			else
				MPASS(m == NULL);
		}
		pefs_chunk_setsize(&pc, bsize);

		PEFSDEBUG("pefs_read: mapped=%d m=%d offset=0x%jx size=0x%zx\n",
		    mapped, m != NULL, uio->uio_offset, bsize - bskip);
		puio = pefs_chunk_uio(&pc, poffset, uio->uio_rw);
		error = VOP_READ(lvp, puio, ioflag, cred);
		if (error != 0)
			break;

		done = pc.pc_size - puio->uio_resid;
		if (done <= bskip)
			break;

		/* XXX assert full buffer is read */
		pefs_chunk_setsize(&pc, done);
		pefs_data_decrypt(&pn->pn_tkey, poffset, &pc);
		if (nocopy == 0) {
			error = pefs_chunk_copy(&pc, bskip, uio);
			if (error != 0)
				break;
		} else {
			nocopy = 0;
			sched_pin();
			sf = sf_buf_alloc(m, SFB_CPUPRIVATE);
			ma = (char *)sf_buf_kva(sf);
			done -= bskip;
			memcpy(ma + bskip, (char *)pc.pc_base + bskip, done);
			uio->uio_offset += done;
			uio->uio_resid -= done;
			sf_buf_free(sf);
			sched_unpin();
#if __FreeBSD_version >= 1000030
			VM_OBJECT_WLOCK(vp->v_object);
			vm_page_wakeup(m);
			VM_OBJECT_WUNLOCK(vp->v_object);
#else
			VM_OBJECT_LOCK(vp->v_object);
			vm_page_wakeup(m);
			VM_OBJECT_UNLOCK(vp->v_object);
#endif
		}
	}
	if (nocopy != 0) {
#if __FreeBSD_version >= 1000030
		VM_OBJECT_WLOCK(vp->v_object);
		vm_page_wakeup(m);
		VM_OBJECT_WUNLOCK(vp->v_object);
#else
		VM_OBJECT_LOCK(vp->v_object);
		vm_page_wakeup(m);
		VM_OBJECT_UNLOCK(vp->v_object);
#endif
	}
	pefs_chunk_free(&pc, pn);

	return (error);
}

static int
pefs_writemapped(struct vnode *vp, struct uio *uio,
    ssize_t bsize, char *pagebuf)
{
	struct sf_buf *sf;
	char *ma;
	vm_page_t m;
	vm_offset_t moffset;
	vm_pindex_t idx;
	int error;

	MPASS(bsize <= PEFS_SECTOR_SIZE);
	moffset = uio->uio_offset & PEFS_SECTOR_MASK;

#if __FreeBSD_version >= 1000030
	VM_OBJECT_WLOCK(vp->v_object);
#else
	VM_OBJECT_LOCK(vp->v_object);
#endif
lookupvpg:
	idx = OFF_TO_IDX(uio->uio_offset);
	m = vm_page_lookup(vp->v_object, idx);
	if (m != NULL && vm_page_is_valid(m, 0, bsize)) {
#if __FreeBSD_version >= 900038
		if ((m->oflags & VPO_BUSY) != 0) {
			/*
			 * Reference the page before unlocking and
			 * sleeping so that the page daemon is less
			 * likely to reclaim it.
			 */
#if __FreeBSD_version >= 900044
			vm_page_aflag_set(m, PGA_REFERENCED);
#else
			vm_page_lock_queues();
			vm_page_flag_set(m, PG_REFERENCED);
#endif
			vm_page_sleep(m, "pefsmw");
			goto lookupvpg;
		}
		vm_page_busy(m);
		vm_page_undirty(m);
#else
		if (vm_page_sleep_if_busy(m, FALSE, "pefsmw"))
			goto lookupvpg;
		vm_page_busy(m);
		vm_page_lock_queues();
		vm_page_undirty(m);
		vm_page_unlock_queues();
#endif
#if __FreeBSD_version >= 1000030
		VM_OBJECT_WUNLOCK(vp->v_object);
#else
		VM_OBJECT_UNLOCK(vp->v_object);
#endif
		PEFSDEBUG("pefs_write: mapped: "
		    "offset=0x%jx moffset=0x%jx bsize=0x%zx\n",
		    uio->uio_offset, (intmax_t)moffset, bsize);
		sched_pin();
		sf = sf_buf_alloc(m, SFB_CPUPRIVATE);
		ma = (char *)sf_buf_kva(sf);
		error = uiomove(ma + moffset, bsize - moffset, uio);
		memcpy(pagebuf, ma, bsize);
		sf_buf_free(sf);
		sched_unpin();
#if __FreeBSD_version >= 1000030
		VM_OBJECT_WLOCK(vp->v_object);
		vm_page_wakeup(m);
		VM_OBJECT_WUNLOCK(vp->v_object);
#else
		VM_OBJECT_LOCK(vp->v_object);
		vm_page_wakeup(m);
		VM_OBJECT_UNLOCK(vp->v_object);
#endif
		if (error != 0) {
			MPASS(error != EJUSTRETURN);
			return (error);
		}
		return (EJUSTRETURN);
	}
#if __FreeBSD_version >= 1000030
	if (vm_page_is_cached(vp->v_object, idx))
#else
	if (__predict_false(vp->v_object->cache != NULL))
#endif
	{
		PEFSDEBUG("pefs_write: free cache: 0x%jx\n",
		    uio->uio_offset - moffset);
		vm_page_cache_free(vp->v_object, idx, idx + 1);
	}
	MPASS(m == NULL ||
	    !vm_page_is_valid(m, moffset, bsize - moffset));
#if __FreeBSD_version >= 1000030
	VM_OBJECT_WUNLOCK(vp->v_object);
#else
	VM_OBJECT_UNLOCK(vp->v_object);
#endif
	return (0);
}

static int
pefs_write(struct vop_write_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct vnode *lvp = PEFS_LOWERVP(vp);
	struct ucred *cred = ap->a_cred;
	struct uio *uio = ap->a_uio;
	struct pefs_node *pn = VP_TO_PN(vp);
	u_quad_t fsize;
	int ioflag = ap->a_ioflag;
	int error;

	if (vp->v_type == VDIR)
		return (EISDIR);
	if (vp->v_type == VFIFO)
		return (VOP_WRITE(lvp, uio, ioflag, cred));
	if (vp->v_type != VREG)
		return (EOPNOTSUPP);
	if (uio->uio_resid == 0)
		return (0);
	if (uio->uio_offset < 0)
		return (EINVAL);

	if ((pn->pn_flags & PN_HASKEY) == 0) {
		if (pefs_no_keys(vp))
			return (EROFS);
		return (VOP_WRITE(lvp, uio, ioflag, cred));
	}

	error = pefs_getsize(vp, &fsize, cred);
	if (error != 0)
		return (error);

	if (ioflag & IO_APPEND) {
		uio->uio_offset = fsize;
		ioflag &= ~IO_APPEND;
	}

	if (uio->uio_offset > fsize) {
		error = pefs_truncate(vp, uio->uio_offset, cred);
		if (error != 0)
			return (error);
		fsize = uio->uio_offset;
	}

	error = pefs_write_int(vp, uio, ioflag, cred, fsize);

	return (error);
}

static int
pefs_write_int(struct vnode *vp, struct uio *uio, int ioflag,
    struct ucred *cred, u_quad_t fsize)
{
	struct vnode *lvp = PEFS_LOWERVP(vp);
	struct uio *puio;
	struct pefs_node *pn = VP_TO_PN(vp);
	struct pefs_chunk pc;
	u_quad_t nsize;
	off_t poffset;
	ssize_t bmaxsize, bsize, bskip;
	int error = 0, mapped;

	MPASS(vp->v_type == VREG);
	MPASS(uio->uio_resid != 0);
	MPASS(uio->uio_offset >= 0);

	mapped = pefs_ismapped(vp);
	if (mapped != 0)
		bmaxsize = PEFS_SECTOR_SIZE;
	else
		bmaxsize = pefs_bufsize(uio, DFLTPHYS);
	bsize = bmaxsize;

	nsize = fsize;
	MPASS(uio->uio_offset <= fsize);
	if (uio->uio_offset + uio->uio_resid > nsize) {
		PEFSDEBUG("pefs_write: extend: 0x%jx (old size: 0x%jx)\n",
		    uio->uio_offset + uio->uio_resid, nsize);
		nsize = uio->uio_offset + uio->uio_resid;
		vnode_pager_setsize(vp, nsize);
	}

	pefs_chunk_create(&pc, pn, bsize);
	while (uio->uio_resid > 0) {
		bskip = uio->uio_offset & PEFS_SECTOR_MASK;
		poffset = uio->uio_offset - bskip;
		if (bskip != 0)
			bsize = PEFS_SECTOR_SIZE;
		else {
			/* Exclude last incomplete page from the chunk */
			bsize = rounddown(uio->uio_resid, PEFS_SECTOR_SIZE);
			if (bsize == 0)
				bsize = PEFS_SECTOR_SIZE;
			bsize = qmin(bsize, bmaxsize);
		}
		bsize = qmin(nsize - poffset, bsize);
		pefs_chunk_setsize(&pc, bsize);

		if (mapped != 0) {
			error = pefs_writemapped(vp, uio, bsize, pc.pc_base);
			if (error == EJUSTRETURN) {
				error = 0;
				goto lower_update;
			} else if (error != 0) {
				PEFSDEBUG("pefs_write: mapped write error: "
				    "offset=0x%jx resid=%0jx\n",
				    uio->uio_offset, uio->uio_resid);
				break;
			}
		}
		if (bskip != 0 || (bskip + uio->uio_resid) < PEFS_SECTOR_SIZE) {
			MPASS(pc.pc_size <= PEFS_SECTOR_SIZE);
			puio = pefs_chunk_uio(&pc, poffset, UIO_READ);
			if (poffset < fsize) {
				error = pefs_read_int(vp, puio, IO_UNIT, cred,
				    qmax(fsize, uio->uio_offset));
				if (error != 0)
					break;
			}
			if (puio->uio_resid != 0) {
				PEFSDEBUG("pefs_write: clear buffer: "
				    "resid=0x%jx fsize=0x%jx nsize=0x%jx "
				    "poffset=0x%jx offset=0x%jx bsize=0x%jx\n",
				    puio->uio_resid, fsize, nsize, poffset,
				    uio->uio_offset, pc.pc_size);
				MPASS(puio->uio_offset == fsize ||
				    puio->uio_offset == poffset);
				bzero(puio->uio_iov[0].iov_base,
				    puio->uio_resid);
			}
		}
		error = pefs_chunk_copy(&pc, bskip, uio);
		if (error != 0) {
			PEFSDEBUG("pefs_write: chunk copy error: "
			    "offset=0x%jx resid=%0jx\n",
			    uio->uio_offset, uio->uio_resid);
			break;
		}
lower_update:
		PEFSDEBUG("pefs_write: mapped=%d offset=0x%jx size=0x%jx\n",
		    mapped, poffset + bskip, (intmax_t)bsize - bskip);
		pefs_data_encrypt(&pn->pn_tkey, poffset, &pc);
		puio = pefs_chunk_uio(&pc, poffset, uio->uio_rw);

		/* IO_APPEND handled above to prevent offset change races. */
		error = VOP_WRITE(lvp, puio, ioflag, cred);
		if (error != 0) {
			/*
			 * XXX Original uio is not preserved thus can't be
			 * restored. Cloning entire uio structure is too
			 * inefficient. uio shouldn't be reused after error, but
			 * uid_resid and uio_offset should remain valid.
			 * Partially destroy uio, so that using it further
			 * would result in panic.
			 *
			 * Correct solution is to implement uiocopy that
			 * doesn't change uio and iovec.
			 */
			PEFSDEBUG("pefs_write: lower level write error: "
			    "offset=0x%jx resid=%0jx\n",
			    uio->uio_offset, uio->uio_resid);
			uio->uio_iov = NULL;
			uio->uio_iovcnt = 0;
			uio->uio_rw = -1;
			uio->uio_resid += uio->uio_offset - (poffset + bsize);
			uio->uio_offset = poffset + bsize;
			break;
		}
		MPASS(puio->uio_resid == 0);
	}
	pefs_chunk_free(&pc, pn);

	return (error);
}

static int
pefs_setkey(struct vnode *vp, struct pefs_key *pk, struct ucred *cred,
    struct thread *td)
{
	struct vnode *dvp, *ldvp, *lvp;
	struct componentname cn;
	struct pefs_node *pn = VP_TO_PN(vp);
	struct pefs_enccn fenccn, tenccn;
	char *namebuf;
	int error, namelen;

	pefs_enccn_init(&fenccn);
	pefs_enccn_init(&tenccn);
	if ((pn->pn_flags & PN_HASKEY) == 0 || vp->v_type != VDIR ||
	    pn->pn_tkey.ptk_key == pk) {
		PEFSDEBUG("pefs_setkey failed: haskey=%d; type=%d; pk=%d\n",
		    (pn->pn_flags & PN_HASKEY) == 0, vp->v_type != VDIR,
		    pn->pn_tkey.ptk_key == pk);
		return (EINVAL);
	}

	namebuf = malloc(MAXNAMLEN, M_PEFSBUF, M_WAITOK | M_ZERO);
	namelen = MAXNAMLEN - 1;
	dvp = vp;
	vref(vp);
	error = vn_vptocnp(&dvp, cred, namebuf, &namelen);
	if (error != 0) {
		PEFSDEBUG("pefs_setkey: vn_vptocnp failed: error=%d; vp=%p\n",
		    error, vp);
		free(namebuf, M_PEFSBUF);
		return (error);
	}
	bzero(&cn, sizeof(cn));
	cn.cn_thread = td;
	cn.cn_cred = cred;
	cn.cn_flags = LOCKPARENT | LOCKLEAF | ISLASTCN | SAVENAME;
	cn.cn_lkflags = LK_EXCLUSIVE;
	cn.cn_pnbuf = cn.cn_nameptr = namebuf + namelen;
	cn.cn_namelen = MAXNAMLEN - 1 - namelen;

	MPASS(dvp->v_mount == vp->v_mount);
	vn_lock(dvp, LK_EXCLUSIVE | LK_RETRY);
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);

	error = VOP_ACCESS(vp, VWRITE, cred, td);
	if (error == 0) {
		cn.cn_nameiop = DELETE;
		error = pefs_enccn_get(&fenccn, dvp, vp, &cn);
	}
	if (error != 0) {
		VOP_UNLOCK(vp, 0);
#if __FreeBSD_version >= 900501
		vput(dvp); /* vref by vn_vptocnp */
#else
		VOP_UNLOCK(dvp, 0);
		vdrop(dvp); /* vrele by vn_vptocnp */
#endif
		PEFSDEBUG("pefs_setkey: pefs_enccn_get failed: %d\n", error);
		goto out;
	}
	cn.cn_nameiop = RENAME;
	error = pefs_enccn_create(&tenccn, pk, NULL, &cn);
	if (error != 0) {
		VOP_UNLOCK(vp, 0);
#if __FreeBSD_version >= 900501
		vput(dvp); /* vref by vn_vptocnp */
#else
		VOP_UNLOCK(dvp, 0);
		vdrop(dvp); /* vrele by vn_vptocnp */
#endif
		pefs_enccn_free(&fenccn);
		goto out;
	}
	PEFSDEBUG("pefs_setkey: fromname=%s; key=%p\n",
	    fenccn.pec_cn.cn_nameptr, fenccn.pec_tkey.ptk_key);
	PEFSDEBUG("pefs_setkey: toname=%s; key=%p\n",
	    tenccn.pec_cn.cn_nameptr, tenccn.pec_tkey.ptk_key);
	ldvp = PEFS_LOWERVP(dvp);
	VOP_UNLOCK(PEFS_LOWERVP(vp), 0);
	error = VOP_LOOKUP(ldvp, &lvp, &fenccn.pec_cn);
	if (error != 0) {
		PEFSDEBUG("pefs_setkey: lookup faild: %s\n",
		    fenccn.pec_cn.cn_nameptr);
#if __FreeBSD_version >= 900501
		vput(dvp); /* vref by vn_vptocnp */
#else
		VOP_UNLOCK(dvp, 0);
		vdrop(dvp); /* vrele by vn_vptocnp */
#endif
		goto out_enccn;
	}
	MPASS(lvp == PEFS_LOWERVP(vp));
	VOP_UNLOCK(PEFS_LOWERVP(vp), 0);
	error = VOP_LOOKUP(ldvp, &lvp, &tenccn.pec_cn);
	if (error != EJUSTRETURN) {
		PEFSDEBUG("pefs_setkey: lookup faild: %s\n",
		    tenccn.pec_cn.cn_nameptr);
#if __FreeBSD_version >= 900501
		vput(dvp); /* vref by vn_vptocnp */
#else
		VOP_UNLOCK(dvp, 0);
		vdrop(dvp); /* vrele by vn_vptocnp */
#endif
		error = EBUSY;
		goto out_enccn;
	}
	vref(ldvp);
	vref(ldvp);
	error = VOP_RENAME(ldvp, PEFS_LOWERVP(vp), &fenccn.pec_cn, ldvp, NULL,
	    &tenccn.pec_cn);
#if __FreeBSD_version >= 900501
	vrele(dvp); /* vref by vn_vptocnp */
#else
	vdrop(dvp); /* vrele by vn_vptocnp */
#endif
	if (error == 0) {
		vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
		vgone(vp);
		VOP_UNLOCK(vp, 0);
	}

out_enccn:
	pefs_enccn_free(&fenccn);
	pefs_enccn_free(&tenccn);

out:
	free(namebuf, M_PEFSBUF);

	return (error);
}
static int
pefs_ioctl(struct vop_ioctl_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct pefs_xkey *xk = ap->a_data;
	struct ucred *cred = ap->a_cred;
	struct thread *td = ap->a_td;
	struct mount *mp = vp->v_mount;
	struct pefs_mount *pm = VFS_TO_PEFS(mp);
	struct pefs_node *pn;
	struct pefs_key *pk;
	int error = 0, i;

	if (mp->mnt_cred->cr_uid != cred->cr_uid) {
		error = priv_check_cred(cred, PRIV_VFS_ADMIN, 0);
		if (error != 0 && (mp->mnt_flag & MNT_RDONLY) == 0) {
			vn_lock(pm->pm_rootvp, LK_SHARED | LK_RETRY);
			error = VOP_ACCESS(mp->mnt_vnodecovered, VWRITE,
			    cred, td);
			VOP_UNLOCK(pm->pm_rootvp, 0);
		}
		if (error != 0)
			return (error);
	}

	/*
	 * Recycle all unused vnodes after adding/deleting keys to cleanup
	 * caches.
	 */
	switch (ap->a_command) {
	case PEFS_GETKEY:
		PEFSDEBUG("pefs_ioctl: get key: pm=%p, pxk_index=%d\n",
		    pm, xk->pxk_index);
		mtx_lock(&pm->pm_keys_lock);
		i = 0;
		TAILQ_FOREACH(pk, &pm->pm_keys, pk_entry) {
			if (i++ == xk->pxk_index) {
				memcpy(xk->pxk_keyid, pk->pk_keyid,
				    PEFS_KEYID_SIZE);
				xk->pxk_alg = pk->pk_algid;
				xk->pxk_keybits = pk->pk_keybits;
				break;
			}
		}
		mtx_unlock(&pm->pm_keys_lock);
		if (pk == NULL)
			error = ENOENT;
		break;
	case PEFS_GETNODEKEY:
		PEFSDEBUG("pefs_ioctl: get key: %8D\n", xk->pxk_keyid, "");
		pn = VP_TO_PN(vp);
		if ((pn->pn_flags & PN_HASKEY) != 0) {
			mtx_lock(&pm->pm_keys_lock);
			pk = pn->pn_tkey.ptk_key;
			memcpy(xk->pxk_keyid, pk->pk_keyid, PEFS_KEYID_SIZE);
			xk->pxk_alg = pk->pk_algid;
			xk->pxk_keybits = pk->pk_keybits;
			mtx_unlock(&pm->pm_keys_lock);
		} else {
			PEFSDEBUG("pefs_ioctl: key not found\n");
			error = ENOENT;
		}
		break;
	case PEFS_SETKEY:
		PEFSDEBUG("pefs_ioctl: set key: %8D\n", xk->pxk_keyid, "");
		mtx_lock(&pm->pm_keys_lock);
		pk = pefs_key_lookup(pm, xk->pxk_keyid);
		if (pk != NULL)
			pefs_key_ref(pk);
		mtx_unlock(&pm->pm_keys_lock);
		if (pk != NULL) {
			error = pefs_setkey(vp, pk, cred, td);
			pefs_key_release(pk);
		} else {
			PEFSDEBUG("pefs_ioctl: key not found\n");
			error = ENOENT;
		}
		break;
	case PEFS_ADDKEY:
		PEFSDEBUG("pefs_ioctl: add key: %8D\n", xk->pxk_keyid, "");
		pk = pefs_key_get(xk->pxk_alg, xk->pxk_keybits, xk->pxk_key,
		    xk->pxk_keyid);
		if (pk == NULL) {
			PEFSDEBUG("pefs_key_get: error\n");
			error = ENOENT;
			break;
		}
		error = pefs_key_add(pm, xk->pxk_index, pk);
		if (error == 0)
			pefs_flushkey(mp, td, 0, NULL);
		else
			pefs_key_release(pk);
		break;
	case PEFS_DELKEY:
		PEFSDEBUG("pefs_ioctl: del key\n");
		mtx_lock(&pm->pm_keys_lock);
		pk = pefs_key_lookup(pm, xk->pxk_keyid);
		if (pk != NULL) {
			pefs_key_ref(pk);
			pefs_key_remove(pm, pk);
			mtx_unlock(&pm->pm_keys_lock);
			pefs_flushkey(mp, td, 0, pk);
			pefs_key_release(pk);
		} else {
			mtx_unlock(&pm->pm_keys_lock);
			error = ENOENT;
		}
		break;
	case PEFS_FLUSHKEYS:
		PEFSDEBUG("pefs_ioctl: flush keys\n");
		if (pefs_key_remove_all(pm))
			pefs_flushkey(mp, td, PEFS_FLUSHKEY_ALL, NULL);
		break;
	default:
		error = ENOTTY;
		break;
	};

	return (error);
}

static int
pefs_pathconf(struct vop_pathconf_args *ap)
{
	struct vnode *vp = ap->a_vp;
	int error, v;

	switch (ap->a_name) {
	case _PC_MIN_HOLE_SIZE:
		return (EINVAL);
	}

	error = VOP_PATHCONF(PEFS_LOWERVP(vp), ap->a_name, ap->a_retval);
	if (error != 0)
		return (error);

	switch (ap->a_name) {
	case _PC_NAME_MAX:
		/*
		 * ntop(csum + roundup(tweak + name, bs) = maxname
		 * roundup(tweak + name, bs) = pton(maxname) - csum
		 * name = rounddown(pton(maxname) - csum, bs) - tweak
		 */
		v = PEFS_NAME_PTON_SIZE(*ap->a_retval);
		v = rounddown(v - PEFS_NAME_CSUM_SIZE, PEFS_NAME_BLOCK_SIZE);
		v = v - PEFS_TWEAK_SIZE;
		*ap->a_retval = v;
		break;
	case _PC_SYMLINK_MAX:
		v = PEFS_NAME_PTON_SIZE(*ap->a_retval - 1);
		*ap->a_retval = v;
		break;
	}

	return (0);
}

/*
 * Global vfs data structures
 */
struct vop_vector pefs_vnodeops = {
	.vop_default =		&default_vnodeops,
	.vop_access =		pefs_access,
	.vop_accessx =		pefs_accessx,
	.vop_getacl =		pefs_getacl,
	.vop_close =		pefs_close,
	.vop_getattr =		pefs_getattr,
	.vop_getwritemount =	pefs_getwritemount,
	.vop_inactive =		pefs_inactive,
	.vop_islocked =		vop_stdislocked,
	.vop_lock1 =		pefs_lock,
	.vop_open =		pefs_open,
	.vop_print =		pefs_print,
	.vop_reclaim =		pefs_reclaim,
	.vop_setattr =		pefs_setattr,
	.vop_unlock =		pefs_unlock,
	.vop_vptocnp =		vop_stdvptocnp,
	.vop_vptofh =		pefs_vptofh,
	.vop_cachedlookup =	pefs_lookup,
	.vop_lookup =		vfs_cache_lookup,
	.vop_mkdir =		pefs_mkdir,
	.vop_rmdir =		pefs_rmdir,
	.vop_create =		pefs_create,
	.vop_remove =		pefs_remove,
	.vop_rename =		pefs_rename,
	.vop_link =		pefs_link,
	.vop_mknod =		pefs_mknod,
	.vop_readdir =		pefs_readdir,
	.vop_symlink =		pefs_symlink,
	.vop_readlink =		pefs_readlink,
	.vop_read =		pefs_read,
	.vop_write =		pefs_write,
	.vop_strategy =		VOP_PANIC,
	.vop_bmap =		VOP_EOPNOTSUPP,
	.vop_getpages =		vop_stdgetpages,
	.vop_putpages =		vop_stdputpages,
	.vop_fsync =		vop_stdfsync,
	.vop_ioctl =		pefs_ioctl,
	.vop_pathconf =		pefs_pathconf,
};
