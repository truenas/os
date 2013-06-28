/*-
 * Copyright (c) 1992, 1993 The Regents of the University of California.
 * Copyright (c) 2009 Gleb Kurtsou <gleb@FreeBSD.org>
 * All rights reserved.
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
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/mutex.h>
#include <sys/namei.h>
#include <sys/dirent.h>
#include <sys/queue.h>
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <sys/sx.h>
#include <sys/uio.h>
#include <sys/taskqueue.h>
#include <sys/vnode.h>

#include <fs/pefs/pefs.h>
#include <fs/pefs/pefs_dircache.h>

typedef int	pefs_node_init_fn(struct mount *mp, struct pefs_node *pn,
	    void *context);
LIST_HEAD(pefs_node_listhead, pefs_node);

static struct taskqueue		*pefs_taskq;
static struct task		pefs_task_freenode;

static struct mtx		pefs_node_listmtx;

static struct pefs_node_listhead pefs_node_freelist;
static struct pefs_node_listhead *pefs_nodehash_tbl;
static u_long			pefs_nodehash_mask;

static uma_zone_t		pefs_node_zone;

MALLOC_DEFINE(M_PEFSHASH, "pefs_hash", "PEFS hash table");
MALLOC_DEFINE(M_PEFSBUF, "pefs_buf", "PEFS buffers");

SYSCTL_NODE(_vfs, OID_AUTO, pefs, CTLFLAG_RW, 0, "PEFS file system");

static u_long	pefs_nodes;
SYSCTL_ULONG(_vfs_pefs, OID_AUTO, nodes, CTLFLAG_RD, &pefs_nodes, 0,
    "Allocated nodes");

static void	pefs_node_free_proc(void *, int);

/*
 * Initialise cache headers
 */
int
pefs_init(struct vfsconf *vfsp)
{
	PEFSDEBUG("pefs_init\n");

	LIST_INIT(&pefs_node_freelist);

	TASK_INIT(&pefs_task_freenode, 0, pefs_node_free_proc, NULL);
	pefs_taskq = taskqueue_create("pefs_taskq", M_WAITOK,
	    taskqueue_thread_enqueue, &pefs_taskq);
	taskqueue_start_threads(&pefs_taskq, 1, PVFS, "pefs taskq");

	pefs_node_zone = uma_zcreate("pefs_node", sizeof(struct pefs_node),
	    NULL, NULL, NULL, (uma_fini) bzero, UMA_ALIGN_PTR, 0);

	pefs_nodehash_tbl = hashinit(desiredvnodes / 8, M_PEFSHASH,
	    &pefs_nodehash_mask);
	pefs_nodes = 0;
	mtx_init(&pefs_node_listmtx, "pefs_node_list", NULL, MTX_DEF);

	pefs_dircache_init();
	pefs_crypto_init();

	return (0);
}

int
pefs_uninit(struct vfsconf *vfsp)
{
	taskqueue_enqueue(pefs_taskq, &pefs_task_freenode);
	taskqueue_drain(pefs_taskq, &pefs_task_freenode);
	taskqueue_free(pefs_taskq);
	pefs_dircache_uninit();
	pefs_crypto_uninit();
	mtx_destroy(&pefs_node_listmtx);
	free(pefs_nodehash_tbl, M_PEFSHASH);
	uma_zdestroy(pefs_node_zone);
	return (0);
}

static __inline struct pefs_node_listhead *
pefs_nodehash_gethead(struct vnode *vp)
{
	uint32_t v;

	v = pefs_hash_mixptr(vp);
	return (&pefs_nodehash_tbl[v & pefs_nodehash_mask]);
}

/*
 * Return a VREF'ed alias for lower vnode if already exists, else 0.
 * Lower vnode should be locked on entry and will be left locked on exit.
 */
static struct vnode *
pefs_nodehash_get(struct mount *mp, struct vnode *lowervp)
{
	struct pefs_node_listhead *hd;
	struct pefs_node *a;
	struct vnode *vp;

	ASSERT_VOP_LOCKED(lowervp, "pefs_nodehash_get");

	/*
	 * Find hash base, and then search the (two-way) linked
	 * list looking for a pefs_node structure which is referencing
	 * the lower vnode.  If found, the increment the pefs_node
	 * reference count (but NOT the lower vnode's VREF counter).
	 */
	hd = pefs_nodehash_gethead(lowervp);
	mtx_lock(&pefs_node_listmtx);
	LIST_FOREACH(a, hd, pn_listentry) {
		if (a->pn_lowervp == lowervp && PN_TO_VP(a)->v_mount == mp) {
			/*
			 * Since we have the lower node locked the pefs
			 * node can not be in the process of recycling.  If
			 * it had been recycled before we grabed the lower
			 * lock it would not have been found on the hash.
			 */
			vp = PN_TO_VP(a);
			vref(vp);
			mtx_unlock(&pefs_node_listmtx);
			return (vp);
		}
	}
	mtx_unlock(&pefs_node_listmtx);
	return (NULLVP);
}

/*
 * Act like pefs_nodehash_get, but add passed pefs_node to hash if no existing
 * node found.
 */
static struct vnode *
pefs_nodehash_insert(struct mount *mp, struct pefs_node *pn)
{
	struct pefs_node_listhead *hd;
	struct pefs_node *oxp;
	struct vnode *ovp;

	hd = pefs_nodehash_gethead(pn->pn_lowervp);
	mtx_lock(&pefs_node_listmtx);
	LIST_FOREACH(oxp, hd, pn_listentry) {
		if (oxp->pn_lowervp == pn->pn_lowervp &&
		    PN_TO_VP(oxp)->v_mount == mp) {
			/*
			 * See pefs_nodehash_get for a description of this
			 * operation.
			 */
			ovp = PN_TO_VP(oxp);
			vref(ovp);
			mtx_unlock(&pefs_node_listmtx);
			return (ovp);
		}
	}
	pefs_nodes++;
	LIST_INSERT_HEAD(hd, pn, pn_listentry);
	mtx_unlock(&pefs_node_listmtx);
	return (NULLVP);
}

static void
pefs_insmntque_dtr(struct vnode *vp, void *_pn)
{
	struct pefs_node *pn = _pn;

	PEFSDEBUG("pefs_insmntque_dtr: free node %p\n", pn);
	vp->v_data = NULL;
	vp->v_vnlock = &vp->v_lock;
	pefs_key_release(pn->pn_tkey.ptk_key);
	uma_zfree(pefs_node_zone, pn);
	vp->v_op = &dead_vnodeops;
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	vgone(vp);
	vput(vp);
}

static int
pefs_node_lookup_name(struct vnode *lvp, struct vnode *ldvp, struct ucred *cred,
    char *encname, int *encname_len)
{
	struct vnode *nldvp;
	int error, locked, dlocked;
	int buflen = *encname_len;

	ASSERT_VOP_LOCKED(lvp, "pefs_node_lookup_name");
	locked = VOP_ISLOCKED(lvp);
	if (ldvp) {
		dlocked = VOP_ISLOCKED(ldvp);
		if (dlocked)
			VOP_UNLOCK(ldvp, 0);
	} else
		dlocked = 0;

	vref(lvp);
	VOP_UNLOCK(lvp, 0);
	nldvp = lvp;
	error = vn_vptocnp(&nldvp, cred, encname, encname_len);
	if (error == 0) {
#if __FreeBSD_version >= 900501
		vrele(nldvp);
#else
		vdrop(nldvp);
#endif
	}
	vrele(lvp);
	if (ldvp && dlocked)
		vn_lock(ldvp, dlocked | LK_RETRY);
	vn_lock(lvp, locked | LK_RETRY);
	if (error != 0)
		return (ENOENT);

	memcpy(encname, encname + *encname_len, buflen - *encname_len);
	*encname_len = buflen - *encname_len;
	if (*encname_len < buflen)
		encname[*encname_len] = '\0';

	return (0);
}

static int
pefs_node_lookup_key(struct pefs_mount *pm, struct vnode *lvp,
    struct vnode *ldvp, struct ucred *cred, struct pefs_tkey *ptk)
{
	char *namebuf;
	char *encname;
	int error, encname_len, name_len;

	namebuf = malloc((MAXNAMLEN + 1)*2, M_PEFSBUF, M_WAITOK | M_ZERO);
	encname = namebuf + MAXNAMLEN + 1;
	encname_len = MAXNAMLEN + 1;

	error = pefs_node_lookup_name(lvp, ldvp, cred, encname, &encname_len);
	if (error != 0) {
		free(namebuf, M_PEFSBUF);
		return (error);
	}

	PEFSDEBUG("pefs_node_lookup_key: encname=%.*s\n", encname_len, encname);

	name_len = pefs_name_decrypt(NULL, pefs_rootkey(pm), ptk,
	    encname, encname_len, namebuf, MAXNAMLEN + 1);

	if (name_len > 0)
		pefs_key_ref(ptk->ptk_key);
	else
		PEFSDEBUG("pefs_node_lookup_key: not found: %.*s\n",
		    encname_len, encname);

	free(namebuf, M_PEFSBUF);

	return (error);
}

static int
pefs_node_init_knownkey(struct mount *mp, struct pefs_node *pn,
    void *context)
{
	struct pefs_tkey *ptk = context;

	MPASS((pn->pn_flags & PN_HASKEY) == 0);

	if (ptk != NULL && ptk->ptk_key != NULL) {
		pn->pn_tkey = *ptk;
		pefs_key_ref(pn->pn_tkey.ptk_key);
		pn->pn_flags |= PN_HASKEY;
	}

	return (0);
}

static int
pefs_node_init_lookupkey(struct mount *mp, struct pefs_node *pn,
    void *context)
{
	struct ucred *cred = context;
	int error;

	KASSERT(mp->mnt_data != NULL,
	    ("pefs_node_get_lookupkey called for uninitialized mount point"));

	if (pefs_rootkey(VFS_TO_PEFS(mp)) == NULL)
		return (0);

	error = pefs_node_lookup_key(VFS_TO_PEFS(mp), pn->pn_lowervp, NULL,
	    cred, &pn->pn_tkey);

	if (pn->pn_tkey.ptk_key != NULL) {
		MPASS(error == 0);
		pn->pn_flags |= PN_HASKEY;
	}

	return (error);

}

/*
 * Make a new or get existing pefs node.
 * vp is the alias vnode
 * lvp is the lower vnode
 * ldvp is the lower directory vnode, used if no key specified
 *
 * The lvp assumed to be locked and having "spare" reference. This routine
 * vrele lvp if pefs node was taken from hash. Otherwise it "transfers" the
 * caller's "spare" reference to created pefs vnode.
 */
static int
pefs_node_get(struct mount *mp, struct vnode *lvp, struct vnode **vpp,
    pefs_node_init_fn *init_fn, void *context)
{
	struct pefs_node *pn;
	struct vnode *vp;
	int error;

	ASSERT_VOP_LOCKED(lvp, "pefs_node_get");
	/* Lookup the hash firstly */
	*vpp = pefs_nodehash_get(mp, lvp);
	if (*vpp != NULL) {
		vrele(lvp);
		return (0);
	}

	/*
	 * We do not serialize vnode creation, instead we will check for
	 * duplicates later, when adding new vnode to hash.
	 *
	 * Note that duplicate can only appear in hash if the lvp is
	 * locked LK_SHARED.
	 */

	/*
	 * Do the MALLOC before the getnewvnode since doing so afterward
	 * might cause a bogus v_data pointer to get dereferenced
	 * elsewhere if MALLOC should block.
	 */
	pn = uma_zalloc(pefs_node_zone, M_WAITOK | M_ZERO);
	pn->pn_lowervp = lvp;

	/* pn->pn_lowervp should be initialized before calling init_fn. */
	error = init_fn(mp, pn, context);
	MPASS(!(((pn->pn_flags & PN_HASKEY) == 0) ^
	    (pn->pn_tkey.ptk_key == NULL)));
	if (error != 0) {
		uma_zfree(pefs_node_zone, pn);
		return (error);
	}

	error = getnewvnode("pefs", mp, &pefs_vnodeops, &vp);
	if (error != 0) {
		pefs_key_release(pn->pn_tkey.ptk_key);
		uma_zfree(pefs_node_zone, pn);
		return (error);
	}

	if (pn->pn_tkey.ptk_key == NULL)
		PEFSDEBUG("pefs_node_get: creating node without key: %p\n", pn);

	pn->pn_vnode = vp;
	vp->v_type = lvp->v_type;
	vp->v_data = pn;
	vp->v_vnlock = lvp->v_vnlock;
	if (vp->v_vnlock == NULL)
		panic("pefs_node_get: Passed a NULL vnlock.\n");
	error = insmntque1(vp, mp, pefs_insmntque_dtr, pn);
	if (error != 0)
		return (error);
	/*
	 * Atomically insert our new node into the hash or vget existing
	 * if someone else has beaten us to it.
	 */
	*vpp = pefs_nodehash_insert(mp, pn);
	if (*vpp != NULL) {
		vrele(lvp);
		vp->v_vnlock = &vp->v_lock;
		pn->pn_lowervp = NULL;
		vrele(vp);
		MPASS(PEFS_LOWERVP(*vpp) == lvp);
		ASSERT_VOP_LOCKED(*vpp, "pefs_node_get: duplicate");
		return (0);
	}
	if (vp->v_type == VDIR)
		pn->pn_dircache = pefs_dircache_get();
	*vpp = vp;
	MPASS(PEFS_LOWERVP(*vpp) == lvp);
	ASSERT_VOP_LOCKED(*vpp, "pefs_node_get");

	return (0);
}

int
pefs_node_get_nokey(struct mount *mp, struct vnode *lvp, struct vnode **vpp)
{
	return (pefs_node_get(mp, lvp, vpp, pefs_node_init_knownkey, NULL));
}

int
pefs_node_get_haskey(struct mount *mp, struct vnode *lvp, struct vnode **vpp,
    struct pefs_tkey *ptk)
{
	MPASS(ptk != NULL && ptk->ptk_key != NULL);
	return (pefs_node_get(mp, lvp, vpp, pefs_node_init_knownkey, ptk));
}

/*
 * Lookup vnode key using VOP_VPTOCNP.
 * Directory vnode (ldvp) of lvp should not be locked.
 * XXX will fail if ldvp is not active ???
 */
int
pefs_node_get_lookupkey(struct mount *mp, struct vnode *lvp, struct vnode **vpp,
    struct ucred *cred)
{
	MPASS(cred != NULL);
	return (pefs_node_get(mp, lvp, vpp, pefs_node_init_lookupkey, cred));
}

static __inline void
pefs_node_free(struct pefs_node *pn)
{
	struct vnode *lowervp;

	lowervp = pn->pn_lowervp_dead;
	uma_zfree(pefs_node_zone, pn);
	if (lowervp != NULL) {
#if __FreeBSD_version < 1000021
		int vfslocked;
		vfslocked = VFS_LOCK_GIANT(lowervp->v_mount);
#endif
		vrele(lowervp);
#if __FreeBSD_version < 1000021
		VFS_UNLOCK_GIANT(vfslocked);
#endif
	}
}

static void
pefs_node_free_proc(void *context __unused, int pending __unused)
{
	struct pefs_node *pn;

	while (1) {
		mtx_lock(&pefs_node_listmtx);
		pn = LIST_FIRST(&pefs_node_freelist);
		if (pn == NULL) {
			mtx_unlock(&pefs_node_listmtx);
			break;
		}
		LIST_REMOVE(pn, pn_listentry);
		mtx_unlock(&pefs_node_listmtx);
		pefs_node_free(pn);
	}
}

/*
 * Remove node from hash and free it.
 */
void
pefs_node_asyncfree(struct pefs_node *pn)
{
	int flags;

	PEFSDEBUG("pefs_node_asyncfree: free node %p\n", pn);
	pefs_key_release(pn->pn_tkey.ptk_key);
	pefs_dircache_free(pn->pn_dircache);
	mtx_lock(&pefs_node_listmtx);
	pefs_nodes--;
	LIST_REMOVE(pn, pn_listentry);
	flags = VFS_TO_PEFS(PN_TO_VP(pn)->v_mount)->pm_flags;
	/* XXX Find a better way to check for safe context */
	if ((flags & PM_ASYNCRECLAIM) == 0 ||
	    memcmp(curthread->td_name, "vnlru", 6) == 0) {
		mtx_unlock(&pefs_node_listmtx);
		pefs_node_free(pn);
		return;
	} else {
		LIST_INSERT_HEAD(&pefs_node_freelist, pn, pn_listentry);
		mtx_unlock(&pefs_node_listmtx);
		taskqueue_enqueue(pefs_taskq, &pefs_task_freenode);
	}
}

void
pefs_node_buf_free(struct pefs_node *pn)
{
	void *bufs[2] = { NULL, NULL };
	int ind = 0;

	ASSERT_VI_UNLOCKED(pn->pn_vnode, "pefs_node_buf_free");
	VI_LOCK(pn->pn_vnode);
	if (pn->pn_buf_small != NULL &&
	    (pn->pn_flags & PN_LOCKBUF_SMALL) == 0) {
		bufs[ind++] = pn->pn_buf_small;
		pn->pn_buf_small = NULL;
	}
	if (pn->pn_buf_large != NULL &&
	    (pn->pn_flags & PN_LOCKBUF_LARGE) == 0) {
		bufs[ind++] = pn->pn_buf_large;
		pn->pn_buf_large = NULL;
	}
	VI_UNLOCK(pn->pn_vnode);
	free(bufs[0], M_PEFSBUF);
	free(bufs[1], M_PEFSBUF);
}

struct pefs_key*
pefs_node_key(struct pefs_node *pn)
{
	struct pefs_key *pk;

	if (pn->pn_flags & PN_HASKEY) {
		MPASS(pn->pn_tkey.ptk_key != NULL);
		pk = pn->pn_tkey.ptk_key;
	} else {
		MPASS(pn->pn_tkey.ptk_key == NULL);
		pk = pefs_rootkey(VFS_TO_PEFS(pn->pn_vnode->v_mount));
	}
	MPASS(pk != NULL);
	return (pefs_key_ref(pk));
}

void
pefs_chunk_create(struct pefs_chunk *pc, struct pefs_node *pn, size_t size)
{
	size_t wantbufsize;
	int nodebuf;
	void **nodebuf_ptr;

	if (size > DFLTPHYS)
		panic("pefs_chunk_create: requested buffer is too large %zd",
		    size);

	nodebuf = 0;
	wantbufsize = (size <= PEFS_SECTOR_SIZE ? PEFS_SECTOR_SIZE : DFLTPHYS);
	if (pn != NULL) {
		nodebuf = (size <= PEFS_SECTOR_SIZE ? PN_LOCKBUF_SMALL :
		    PN_LOCKBUF_LARGE);
		VI_LOCK(pn->pn_vnode);
		if ((pn->pn_flags & nodebuf) == 0) {
			pn->pn_flags |= nodebuf;
			nodebuf_ptr = pefs_node_buf(pn, nodebuf);
		} else if (nodebuf == PN_LOCKBUF_SMALL &&
		    (pn->pn_flags & PN_LOCKBUF_LARGE) == 0)  {
			nodebuf = PN_LOCKBUF_LARGE;
			wantbufsize = DFLTPHYS;
			pn->pn_flags |= nodebuf;
			nodebuf_ptr = &pn->pn_buf_large;
		} else
			nodebuf = 0;
		VI_UNLOCK(pn->pn_vnode);
	}
	if (nodebuf != 0) {
		if (*nodebuf_ptr == NULL)
			*nodebuf_ptr = malloc(wantbufsize, M_PEFSBUF, M_WAITOK);
		pc->pc_nodebuf = nodebuf;
		pc->pc_base = *nodebuf_ptr;
	} else {
		pc->pc_nodebuf = 0;
		pc->pc_base = malloc(wantbufsize, M_PEFSBUF, M_WAITOK);
	}
	pc->pc_size = size;
	pc->pc_capacity = pc->pc_size;
	pc->pc_uio.uio_iovcnt = 1;
	pc->pc_uio.uio_iov = &pc->pc_iov;
}

void
pefs_chunk_restore(struct pefs_chunk* pc)
{
	pc->pc_size = pc->pc_capacity;
	pc->pc_iov.iov_base = pc->pc_base;
	pc->pc_iov.iov_len = pc->pc_size;
}

void
pefs_chunk_free(struct pefs_chunk* pc, struct pefs_node *pn)
{
	if (pc->pc_nodebuf != 0) {
		MPASS(pn != NULL);
		MPASS(pc->pc_base == *pefs_node_buf(pn, pc->pc_nodebuf));
		VI_LOCK(pn->pn_vnode);
		pn->pn_flags &= ~(pc->pc_nodebuf);
		VI_UNLOCK(pn->pn_vnode);
	} else
		free(pc->pc_base, M_PEFSBUF);
	pc->pc_nodebuf = 0;
	pc->pc_base = NULL;
}

struct uio*
pefs_chunk_uio(struct pefs_chunk *pc, off_t uio_offset, enum uio_rw uio_rw)
{
	pc->pc_iov.iov_base = pc->pc_base;
	pc->pc_iov.iov_len = pc->pc_size;

	pc->pc_uio.uio_offset = uio_offset;
	pc->pc_uio.uio_resid = pc->pc_size;
	pc->pc_uio.uio_rw = uio_rw;
	pc->pc_uio.uio_segflg = UIO_SYSSPACE;
	pc->pc_uio.uio_td = curthread;

	return (&pc->pc_uio);
}

void
pefs_chunk_zero(struct pefs_chunk *pc)
{
	bzero(pc->pc_base, pc->pc_size);
}

int
pefs_chunk_copy(struct pefs_chunk *pc, size_t skip, struct uio *uio)
{
	int error;

	MPASS(skip < pc->pc_size);
	error = uiomove((char *)pc->pc_base + skip,
	    qmin(pc->pc_size - skip, uio->uio_resid), uio);

	return (error);
}

void
pefs_chunk_setsize(struct pefs_chunk *pc, size_t size)
{
	MPASS(size <= pc->pc_capacity);
	pc->pc_size = size;
}

#ifdef DIAGNOSTIC
struct vnode *
pefs_checkvp(struct vnode *vp, char *fil, int lno)
{
	int interlock = 0;
	struct pefs_node *a = VP_TO_PN(vp);
	if (a->pn_lowervp == NULLVP) {
		/* Should never happen */
		int i; u_long *p;
		printf("vp = %p, ZERO ptr\n", (void *)vp);
		for (p = (u_long *) a, i = 0; i < 8; i++)
			printf(" %lx", p[i]);
		printf("\n");
		panic("pefs_checkvp");
	}
	if (mtx_owned(VI_MTX(vp)) != 0) {
		VI_UNLOCK(vp);
		interlock = 1;
	}
	if (vrefcnt(a->pn_lowervp) < 1) {
		int i; u_long *p;
		printf("vp = %p, unref'ed lowervp\n", (void *)vp);
		for (p = (u_long *) a, i = 0; i < 8; i++)
			printf(" %lx", p[i]);
		printf("\n");
		panic ("null with unref'ed lowervp");
	};
	if (interlock != 0)
		VI_LOCK(vp);
	return (a->pn_lowervp);
}
#endif /* DIAGNOSTIC */
