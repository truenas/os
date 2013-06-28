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
#include <sys/hash.h>
#include <sys/queue.h>
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <sys/sx.h>
#include <sys/uio.h>
#include <sys/taskqueue.h>
#include <sys/vnode.h>

#include <fs/pefs/pefs.h>
#include <fs/pefs/pefs_dircache.h>

#define	DIRCACHE_SIZE_ENV	"vfs.pefs.dircache.buckets"
#define	DIRCACHE_SIZE_MIN	512
#define	DIRCACHE_SIZE_DEFAULT	(desiredvnodes / 8)

#define	DIRCACHE_HEADOFF(pd)	(((pd)->pd_flags & PD_SWAPEDHEADS) ? 1 : 0)
#define	DIRCACHE_ACTIVEHEAD(pd)	(&(pd)->pd_heads[DIRCACHE_HEADOFF(pd) ^ 0])
#define	DIRCACHE_STALEHEAD(pd)	(&(pd)->pd_heads[DIRCACHE_HEADOFF(pd) ^ 1])

#define	DIRCACHE_ASSERT(pd)	MPASS(LIST_EMPTY(&(pd)->pd_heads[0]) || \
		LIST_EMPTY(&(pd)->pd_heads[1]))

static struct mtx		dircache_mtx;

static struct pefs_dircache_listhead *dircache_tbl;
static struct pefs_dircache_listhead *dircache_enctbl;
static u_long			pefs_dircache_hashmask;

static uma_zone_t		dircache_zone;
static uma_zone_t		dircache_entry_zone;

SYSCTL_NODE(_vfs_pefs, OID_AUTO, dircache, CTLFLAG_RW, 0,
    "PEFS directory cache");

int		pefs_dircache_enable = 1;
SYSCTL_INT(_vfs_pefs_dircache, OID_AUTO, enable, CTLFLAG_RW,
    &pefs_dircache_enable, 0, "Enable dircache");

static u_long	dircache_buckets = 0;
SYSCTL_ULONG(_vfs_pefs_dircache, OID_AUTO, buckets, CTLFLAG_RD,
    &dircache_buckets, 0, "Number of dircache hash table buckets");

static u_long	dircache_entries = 0;
SYSCTL_ULONG(_vfs_pefs_dircache, OID_AUTO, entries, CTLFLAG_RD,
    &dircache_entries, 0, "Entries in dircache");

static void	dircache_entry_free(struct pefs_dircache_entry *pde);

void
pefs_dircache_init(void)
{
	TUNABLE_ULONG_FETCH(DIRCACHE_SIZE_ENV, &dircache_buckets);

	if (dircache_buckets < DIRCACHE_SIZE_MIN)
		dircache_buckets = DIRCACHE_SIZE_DEFAULT;

	dircache_zone = uma_zcreate("pefs_dircache",
	    sizeof(struct pefs_dircache), NULL, NULL, NULL, NULL,
	    UMA_ALIGN_PTR, 0);
	dircache_entry_zone = uma_zcreate("pefs_dircache_entry",
	    sizeof(struct pefs_dircache_entry), NULL, NULL, NULL,
	    (uma_fini) bzero, UMA_ALIGN_PTR, 0);

	dircache_tbl = hashinit(dircache_buckets, M_PEFSHASH,
	    &pefs_dircache_hashmask);
	dircache_enctbl = hashinit(dircache_buckets, M_PEFSHASH,
	    &pefs_dircache_hashmask);
	mtx_init(&dircache_mtx, "pefs_dircache_mtx", NULL, MTX_DEF);
}

void
pefs_dircache_uninit(void)
{
	mtx_destroy(&dircache_mtx);
	free(dircache_tbl, M_PEFSHASH);
	free(dircache_enctbl, M_PEFSHASH);
	uma_zdestroy(dircache_zone);
	uma_zdestroy(dircache_entry_zone);
}

struct pefs_dircache *
pefs_dircache_get(void)
{
	struct pefs_dircache *pd;

	pd = uma_zalloc(dircache_zone, M_WAITOK | M_ZERO);
	sx_init(&pd->pd_lock, "pefs_dircache_sx");
	LIST_INIT(&pd->pd_heads[0]);
	LIST_INIT(&pd->pd_heads[1]);

	return (pd);
}

void
pefs_dircache_free(struct pefs_dircache *pd)
{
	struct pefs_dircache_entry *pde;

	if (pd == NULL)
		return;

	while (!LIST_EMPTY(DIRCACHE_STALEHEAD(pd))) {
		pde = LIST_FIRST(DIRCACHE_STALEHEAD(pd));
		dircache_entry_free(pde);
	}
	while (!LIST_EMPTY(DIRCACHE_ACTIVEHEAD(pd))) {
		pde = LIST_FIRST(DIRCACHE_ACTIVEHEAD(pd));
		dircache_entry_free(pde);
	}
	sx_destroy(&pd->pd_lock);
	uma_zfree(dircache_zone, pd);
}

static __inline uint32_t
dircache_hashname(struct pefs_dircache *pd, char const *buf, size_t len)
{
	uint32_t h;

	h = pefs_hash_mixptr(pd);
	h ^= hash32_buf(buf, len, HASHINIT * len);
	return (h);
}

static void
dircache_entry_free(struct pefs_dircache_entry *pde)
{
	MPASS(pde != NULL);

	PEFSDEBUG("dircache_entry_free: %s -> %s\n",
	    pde->pde_name, pde->pde_encname);
	pefs_key_release(pde->pde_tkey.ptk_key);
	LIST_REMOVE(pde, pde_dir_entry);
	mtx_lock(&dircache_mtx);
	LIST_REMOVE(pde, pde_hash_entry);
	LIST_REMOVE(pde, pde_enchash_entry);
	dircache_entries--;
	mtx_unlock(&dircache_mtx);
	uma_zfree(dircache_entry_zone, pde);
}

static void
dircache_expire(struct pefs_dircache *pd)
{
	struct pefs_dircache_entry *pde;

	pd->pd_gen = 0;
	if (LIST_EMPTY(DIRCACHE_STALEHEAD(pd))) {
		pd->pd_flags ^= PD_SWAPEDHEADS;
	} else while (!LIST_EMPTY(DIRCACHE_ACTIVEHEAD(pd))) {
		pde = LIST_FIRST(DIRCACHE_ACTIVEHEAD(pd));
		pde->pde_gen = 0;
		LIST_REMOVE(pde, pde_dir_entry);
		LIST_INSERT_HEAD(DIRCACHE_STALEHEAD(pd), pde, pde_dir_entry);
		PEFSDEBUG("dircache_expire: active entry: %p\n", pde);
	}
	MPASS(LIST_EMPTY(DIRCACHE_ACTIVEHEAD(pd)));
}

static void
dircache_update(struct pefs_dircache_entry *pde, int onlist)
{
	struct pefs_dircache *pd = pde->pde_dircache;

	sx_assert(&pd->pd_lock, SA_XLOCKED);

	if ((pd->pd_flags & PD_UPDATING) != 0) {
		PEFSDEBUG("pefs_dircache_update: %s -> %s\n",
		    pde->pde_name, pde->pde_encname);
		pde->pde_gen = pd->pd_gen;
		if (onlist != 0)
			LIST_REMOVE(pde, pde_dir_entry);
		LIST_INSERT_HEAD(DIRCACHE_ACTIVEHEAD(pd), pde, pde_dir_entry);
	} else if (pd->pd_gen == 0 || pd->pd_gen != pde->pde_gen) {
		PEFSDEBUG("pefs_dircache: inconsistent cache: "
		    "gen=%ld old_gen=%ld name=%s\n",
		    pd->pd_gen, pde->pde_gen, pde->pde_name);
		dircache_expire(pd);
		pde->pde_gen = 0;
		if (onlist == 0)
			LIST_INSERT_HEAD(DIRCACHE_STALEHEAD(pd), pde,
					pde_dir_entry);
	}
}

struct pefs_dircache_entry *
pefs_dircache_insert(struct pefs_dircache *pd, struct pefs_tkey *ptk,
    char const *name, size_t name_len,
    char const *encname, size_t encname_len)
{
	struct pefs_dircache_listhead *head;
	struct pefs_dircache_entry *pde;

	MPASS(ptk->ptk_key != NULL);
	sx_assert(&pd->pd_lock, SA_XLOCKED);

	if (name_len == 0 || name_len >= sizeof(pde->pde_name) ||
	    encname_len == 0 || encname_len >= sizeof(pde->pde_encname))
		panic("pefs: invalid file name length: %zd/%zd",
		    name_len, encname_len);

	pde = uma_zalloc(dircache_entry_zone, M_WAITOK | M_ZERO);
	pde->pde_dircache = pd;

	pde->pde_tkey = *ptk;
	pefs_key_ref(pde->pde_tkey.ptk_key);

	pde->pde_namelen = name_len;
	memcpy(pde->pde_name, name, name_len);
	pde->pde_name[name_len] = '\0';
	pde->pde_namehash = dircache_hashname(pd, pde->pde_name,
	    pde->pde_namelen);

	pde->pde_encnamelen = encname_len;
	memcpy(pde->pde_encname, encname, encname_len);
	pde->pde_encname[encname_len] = '\0';
	pde->pde_encnamehash = dircache_hashname(pd, pde->pde_encname,
	    pde->pde_encnamelen);

	/* Insert into list and set pge_gen */
	dircache_update(pde, 0);

	mtx_lock(&dircache_mtx);
	head = &dircache_tbl[pde->pde_namehash & pefs_dircache_hashmask];
	LIST_INSERT_HEAD(head, pde, pde_hash_entry);
	head = &dircache_enctbl[pde->pde_encnamehash & pefs_dircache_hashmask];
	LIST_INSERT_HEAD(head, pde, pde_enchash_entry);
	dircache_entries++;
	mtx_unlock(&dircache_mtx);

	PEFSDEBUG("pefs_dircache_insert: hash=%x enchash=%x: %s -> %s\n",
	    pde->pde_namehash, pde->pde_encnamehash,
	    pde->pde_name, pde->pde_encname);

	return (pde);
}

struct pefs_dircache_entry *
pefs_dircache_lookup(struct pefs_dircache *pd, char const *name,
    size_t name_len)
{
	struct pefs_dircache_entry *pde;
	struct pefs_dircache_listhead *head;
	uint32_t h;

	MPASS(pd != NULL);
	MPASS((pd->pd_flags & PD_UPDATING) == 0);
	MPASS(LIST_EMPTY(DIRCACHE_STALEHEAD(pd)));

	h = dircache_hashname(pd, name, name_len);
	head = &dircache_tbl[h & pefs_dircache_hashmask];
	mtx_lock(&dircache_mtx);
	LIST_FOREACH(pde, head, pde_hash_entry) {
		if (pde->pde_namehash == h &&
		    pde->pde_dircache == pd &&
		    pde->pde_gen == pd->pd_gen &&
		    pde->pde_namelen == name_len &&
		    memcmp(pde->pde_name, name, name_len) == 0) {
			mtx_unlock(&dircache_mtx);
			PEFSDEBUG("pefs_dircache_lookup: found %s -> %s\n",
			    pde->pde_name, pde->pde_encname);
			return (pde);
		}
	}
	mtx_unlock(&dircache_mtx);
	PEFSDEBUG("pefs_dircache_lookup: not found %s\n", name);
	return (NULL);
}

struct pefs_dircache_entry *
pefs_dircache_enclookup(struct pefs_dircache *pd, char const *encname,
    size_t encname_len)
{
	struct pefs_dircache_entry *pde;
	struct pefs_dircache_listhead *head;
	uint32_t h;

	h = dircache_hashname(pd, encname, encname_len);
	head = &dircache_enctbl[h & pefs_dircache_hashmask];
	mtx_lock(&dircache_mtx);
	LIST_FOREACH(pde, head, pde_enchash_entry) {
		if (pde->pde_encnamehash == h &&
		    pde->pde_dircache == pd &&
		    pde->pde_encnamelen == encname_len &&
		    memcmp(pde->pde_encname, encname, encname_len) == 0) {
			mtx_unlock(&dircache_mtx);
			PEFSDEBUG("pefs_dircache_enclookup: found %s -> %s\n",
			    pde->pde_name, pde->pde_encname);
			return (pde);
		}
	}
	mtx_unlock(&dircache_mtx);
	PEFSDEBUG("pefs_dircache_enclookup: not found %s\n", encname);
	return (NULL);
}

void
pefs_dircache_update(struct pefs_dircache_entry *pde)
{
	dircache_update(pde, 1);
}

void
pefs_dircache_beginupdate(struct pefs_dircache *pd, u_long gen)
{
	if (sx_try_upgrade(&pd->pd_lock) == 0) {
		/* vnode should be locked to avoid races */
		sx_unlock(&pd->pd_lock);
		sx_xlock(&pd->pd_lock);
	}
	if (gen != 0 && pd->pd_gen != gen) {
		PEFSDEBUG("pefs_dircache_beginupdate: update: gen=%lu %p\n",
		    gen, pd);
		if (!LIST_EMPTY(DIRCACHE_ACTIVEHEAD(pd))) {
			/* Assert consistent state */
			MPASS(LIST_EMPTY(DIRCACHE_STALEHEAD(pd)));
			dircache_expire(pd);
		}
		pd->pd_gen = gen;
		pd->pd_flags |= PD_UPDATING;
		MPASS(LIST_EMPTY(DIRCACHE_ACTIVEHEAD(pd)));
	}
}

void
pefs_dircache_abortupdate(struct pefs_dircache *pd)
{
	sx_assert(&pd->pd_lock, SA_XLOCKED);

	if ((pd->pd_flags & PD_UPDATING) != 0) {
		PEFSDEBUG("pefs_dircache_abortupdate: gen=%lu %p\n",
		    pd->pd_gen, pd);
		dircache_expire(pd);
		pd->pd_flags &= ~PD_UPDATING;
	}
	DIRCACHE_ASSERT(pd);
}

void
pefs_dircache_endupdate(struct pefs_dircache *pd)
{
	struct pefs_dircache_entry *pde;

	sx_assert(&pd->pd_lock, SA_XLOCKED);

	if ((pd->pd_flags & PD_UPDATING) == 0) {
		DIRCACHE_ASSERT(pd);
		return;
	}

	while (!LIST_EMPTY(DIRCACHE_STALEHEAD(pd))) {
		pde = LIST_FIRST(DIRCACHE_STALEHEAD(pd));
		dircache_entry_free(pde);
	}
	pd->pd_flags &= ~PD_UPDATING;
}
