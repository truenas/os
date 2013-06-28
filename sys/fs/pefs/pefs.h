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

#define	PEFS_SECTOR_SIZE		4096
#define	PEFS_SECTOR_MASK		(PEFS_SECTOR_SIZE - 1)

#define	PEFS_ALG_INVALID		0
#define	PEFS_ALG_AES_XTS		4
#define	PEFS_ALG_CAMELLIA_XTS		5

#define	PEFS_TWEAK_SIZE			8
#define	PEFS_KEY_BITS			512
#define	PEFS_KEY_SIZE			(PEFS_KEY_BITS / 8)
#define	PEFS_KEYID_SIZE			8
#define	PEFS_NAME_CSUM_SIZE		8
#define	PEFS_NAME_BLOCK_SIZE		16

struct pefs_xkey {
	uint32_t		pxk_index;
	uint32_t		pxk_alg;
	uint32_t		pxk_keybits;
	char			pxk_keyid[PEFS_KEYID_SIZE];
	char			pxk_key[PEFS_KEY_SIZE];
};

#ifdef _IO
#define	PEFS_GETKEY			_IOWR('p', 0, struct pefs_xkey)
#define	PEFS_ADDKEY			_IOWR('p', 1, struct pefs_xkey)
#define	PEFS_SETKEY			_IOWR('p', 2, struct pefs_xkey)
#define	PEFS_DELKEY			_IOWR('p', 3, struct pefs_xkey)
#define	PEFS_FLUSHKEYS			_IO('p', 4)
#define	PEFS_GETNODEKEY			_IOWR('p', 5, struct pefs_xkey)
#endif

#ifdef _KERNEL

#define	PEFS_NAME_NTOP_SIZE(a)		(((a) * 4 + 2)/3)
#define	PEFS_NAME_PTON_SIZE(a)		(((a) * 3)/4)

#ifdef PEFS_DEBUG
#define	PEFSDEBUG(format, args...)	printf(format ,## args)
#else
#define	PEFSDEBUG(format, args...)	do { } while (0)
#endif /* PEFS_DEBUG */

struct pefs_alg;
struct pefs_ctx;
struct pefs_dircache;
struct vfsconf;

TAILQ_HEAD(pefs_key_head, pefs_key);

struct pefs_key {
	TAILQ_ENTRY(pefs_key)	pk_entry;
	volatile u_int		pk_refcnt;
	const struct		pefs_alg *pk_alg;
	struct pefs_ctx		*pk_name_csum_ctx;
	struct pefs_ctx		*pk_name_ctx;
	struct pefs_ctx		*pk_tweak_ctx;
	struct pefs_ctx		*pk_data_ctx;
	struct mtx		*pk_entry_lock;
	int			pk_algid;
	int			pk_keybits;
	char			pk_keyid[PEFS_KEYID_SIZE];
};

struct pefs_tkey {
	struct pefs_key		*ptk_key;
	char			ptk_tweak[PEFS_TWEAK_SIZE];
};

#define	PN_HASKEY			0x000001
#define	PN_WANTRECYCLE			0x000100
#define	PN_LOCKBUF_SMALL		0x001000
#define	PN_LOCKBUF_LARGE		0x002000

struct pefs_node {
	LIST_ENTRY(pefs_node)	pn_listentry;
	struct vnode		*pn_lowervp;
	struct vnode		*pn_lowervp_dead;
	struct vnode		*pn_vnode;
	struct pefs_dircache	*pn_dircache;
	void			*pn_buf_small;
	void			*pn_buf_large;
	int			pn_flags;
	struct pefs_tkey	pn_tkey;
};

#define	PM_ROOT_CANRECURSE		0x01
#define	PM_DIRCACHE			0x02
#define	PM_ASYNCRECLAIM			0x04

struct pefs_mount {
	struct mount		*pm_lowervfs;
	struct vnode		*pm_rootvp;
	struct mtx		pm_keys_lock;
	struct pefs_key_head	pm_keys;
	int			pm_flags;
};

struct pefs_chunk {
	size_t			pc_size;
	size_t			pc_capacity;
	void			*pc_base;
	int			pc_nodebuf;
	struct iovec		pc_iov;
	struct uio		pc_uio;
};

int	pefs_init(struct vfsconf *vfsp);
int	pefs_uninit(struct vfsconf *vfsp);
void	pefs_crypto_init(void);
void	pefs_crypto_uninit(void);

int	pefs_node_get_nokey(struct mount *mp, struct vnode *lvp,
	    struct vnode **vpp);
int	pefs_node_get_haskey(struct mount *mp, struct vnode *lvp,
	    struct vnode **vpp, struct pefs_tkey *ptk);
int	pefs_node_get_lookupkey(struct mount *mp, struct vnode *lvp,
	    struct vnode **vpp, struct ucred *cred);
void	pefs_node_asyncfree(struct pefs_node *xp);
struct pefs_key	*pefs_node_key(struct pefs_node *pn);
void	pefs_node_buf_free(struct pefs_node *pn);

struct pefs_ctx	*pefs_ctx_get(void);
void	pefs_ctx_free(struct pefs_ctx *ctx);

struct pefs_key	*pefs_key_get(int alg, int keybits, const char *key,
		    const char *keyid);
struct pefs_key	*pefs_key_ref(struct pefs_key *pk);
void	pefs_key_release(struct pefs_key *pk);

struct pefs_key	*pefs_key_lookup(struct pefs_mount *pm, char *keyid);
int	pefs_key_add(struct pefs_mount *pm, int index, struct pefs_key *pk);
void	pefs_key_remove(struct pefs_mount *pm, struct pefs_key *pk);
int	pefs_key_remove_all(struct pefs_mount *pm);

void	pefs_data_encrypt(struct pefs_tkey *ptk, off_t offset,
	    struct pefs_chunk *pc);
void	pefs_data_decrypt(struct pefs_tkey *ptk, off_t offset,
	    struct pefs_chunk *pc);

int	pefs_name_encrypt(struct pefs_ctx *ctx, struct pefs_tkey *ptk,
	    const char *plain, size_t plain_len, char *enc, size_t enc_size);
int	pefs_name_decrypt(struct pefs_ctx *ctx, struct pefs_key *pk,
	    struct pefs_tkey *ptk, const char *enc, size_t enc_len, char *plain,
	    size_t plain_size);

int	pefs_name_ntop(u_char const *src, size_t srclength, char *target,
	    size_t targsize);
int	pefs_name_pton(char const *src, size_t srclen, u_char *target,
	    size_t targsize);

void	pefs_chunk_create(struct pefs_chunk *pc, struct pefs_node *pn,
	    size_t size);
void	pefs_chunk_restore(struct pefs_chunk* pc);
void	pefs_chunk_free(struct pefs_chunk* pc, struct pefs_node *pn);
void	pefs_chunk_zero(struct pefs_chunk *pc);
int	pefs_chunk_copy(struct pefs_chunk *pc, size_t skip, struct uio *uio);
void	pefs_chunk_setsize(struct pefs_chunk *pc, size_t size);
struct uio	*pefs_chunk_uio(struct pefs_chunk *pc, off_t uio_offset,
	    enum uio_rw uio_rw);

#ifdef DIAGNOSTIC
struct vnode	*pefs_checkvp(struct vnode *vp, char *fil, int lno);
#endif

#ifdef SYSCTL_DECL
SYSCTL_DECL(_vfs_pefs);
#endif

#ifdef MALLOC_DECLARE
MALLOC_DECLARE(M_PEFSBUF);
MALLOC_DECLARE(M_PEFSHASH);
#endif

extern struct vop_vector	pefs_vnodeops;

static __inline struct pefs_mount *
VFS_TO_PEFS(struct mount *mp)
{
	MPASS(mp != NULL && mp->mnt_data != NULL);
	return ((struct pefs_mount *)(mp->mnt_data));
}

static __inline struct pefs_node *
VP_TO_PN(struct vnode *vp)
{
	MPASS(vp != NULL && vp->v_data != NULL);
	return ((struct pefs_node *)vp->v_data);
}

static __inline struct vnode *
PN_TO_VP(struct pefs_node *pn)
{
	MPASS(pn != NULL && pn->pn_vnode != NULL);
	return (pn->pn_vnode);
}

static __inline struct vnode *
PEFS_LOWERVP(struct vnode *vp)
{
	struct vnode *lvp;

	MPASS(vp != NULL);
#ifdef DIAGNOSTIC
	pefs_checkvp((vp), __FILE__, __LINE__);
#endif
	lvp = VP_TO_PN(vp)->pn_lowervp;
	MPASS(lvp != NULL);
	return (lvp);
}

static __inline void **
pefs_node_buf(struct pefs_node *pn, int flag)
{
	MPASS(flag == PN_LOCKBUF_SMALL || flag == PN_LOCKBUF_LARGE);
	if (flag == PN_LOCKBUF_SMALL)
		return (&pn->pn_buf_small);
	else
		return (&pn->pn_buf_large);
}

static __inline struct pefs_key *
pefs_rootkey(struct pefs_mount *pm)
{
	struct pefs_key *pk;

	mtx_lock(&pm->pm_keys_lock);
	pk = TAILQ_FIRST(&pm->pm_keys);
	mtx_unlock(&pm->pm_keys_lock);

	return (pk);
}

static __inline int
pefs_no_keys(struct vnode *vp)
{
	return (!(VP_TO_PN(vp)->pn_flags & PN_HASKEY) &&
	    pefs_rootkey(VFS_TO_PEFS(vp->v_mount)) == NULL);
}

static __inline uint32_t
pefs_hash_mixptr(void *ptr)
{
	uintptr_t h = (uintptr_t)ptr;

	h = (~h) + (h << 18);
	h = h ^ (h >> 31);
	h = h * 21;
	h = h ^ (h >> 11);
	h = h + (h << 6);
	h = h ^ (h >> 22);
	return (h);
}

#endif /* _KERNEL */
