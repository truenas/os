/*
 * Copyright 1991-1998 by Open Software Foundation, Inc. 
 *              All Rights Reserved 
 *  
 * Permission to use, copy, modify, and distribute this software and 
 * its documentation for any purpose and without fee is hereby granted, 
 * provided that the above copyright notice appears in all copies and 
 * that both the copyright notice and this permission notice appear in 
 * supporting documentation. 
 *  
 * OSF DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE 
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS 
 * FOR A PARTICULAR PURPOSE. 
 *  
 * IN NO EVENT SHALL OSF BE LIABLE FOR ANY SPECIAL, INDIRECT, OR 
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM 
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN ACTION OF CONTRACT, 
 * NEGLIGENCE, OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION 
 * WITH THE USE OR PERFORMANCE OF THIS SOFTWARE. 
 */
/*
 * MkLinux
 */
/*
 * Copyright (C) 1988,1989 Encore Computer Corporation.  All Rights Reserved
 *
 * Property of Encore Computer Corporation.
 * This software is made available solely pursuant to the terms of
 * a software license agreement which governs its use. Unauthorized
 * duplication, distribution or sale are strictly prohibited.
 *
 */
/*
 * Copyright (c) 1982, 1986, 1988 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted provided
 * that: (1) source distributions retain this entire copyright notice and
 * comment, and (2) distributions including binaries display the following
 * acknowledgement:  ``This product includes software developed by the
 * University of California, Berkeley and its contributors'' in the
 * documentation or other materials provided with the distribution and in
 * all advertising materials mentioning features or use of this software.
 * Neither the name of the University nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 *	Base:	uipc_mbuf.c	7.12 (Berkeley) 9/26/89
 *	Merged: uipc_mbuf.c	7.16 (Berkeley) 6/28/90
 */

#include <sys/types.h>
#include <kern/spl.h>
#include <kern/misc_protos.h>
#include <kern/kalloc.h>
#include <kern/queue.h>
#include <device/mbuf.h>

struct	mbstat mbstat;		/* statistics */
int	mclbytes = MCLBYTES;
int	max_linkhdr = 16;
int	max_protohdr = 40;
decl_simple_lock_data(,mbuf_slock)

void
mbinit()
{
	MBUF_LOCKINIT();
}

caddr_t
m_clalloc(register struct mbuf *m,
	  register int siz,
	  int canwait)
{
	caddr_t mcl;

	if (siz != MCLBYTES) {
		if (siz >= mclbytes)
			siz = mclbytes;
	}
	if (canwait == M_DONTWAIT)
		mcl = (caddr_t)kget(siz);
	else
		mcl = (caddr_t)kalloc(siz);
	
	if (m && mcl) {
		m->m_data = m->m_ext.ext_buf = mcl;
		m->m_flags |= M_EXT;
		m->m_ext.ext_size = siz;
		m->m_ext.ext_free = 0;
		m->m_ext.ext_arg  = 0;
		m->m_ext.ext_ref.forw = m->m_ext.ext_ref.back =
			&m->m_ext.ext_ref;
	}
	MBSTAT(mcl ? mbstat.m_clusters++ : mbstat.m_drops++);
	return mcl;
}

/*
 * Space allocation routines.
 * These are also available as macros
 * for critical paths.
 */
struct mbuf *
m_get(int canwait, int type)
{
	struct mbuf *m;

	MGET(m, canwait, type);
	return (m);
}

struct mbuf *
m_gethdr(int canwait, int type)
{
	struct mbuf *m;

	MGETHDR(m, canwait, type);
	return (m);
}

struct mbuf *
m_getclr(int canwait, int type)
{
	struct mbuf *m;

	MGET(m, canwait, type);
	if (m)
		bzero(mtod(m, caddr_t), MLEN);
	return (m);
}

/*
 * Free an allocated mbuf, freeing associated cluster if present.
 * If cluster requires special action, place whole mbuf on mfreelater
 * and schedule later freeing (so as not to free from interrupt level).
 */
struct mbuf *
m_free(struct mbuf *m)
{
	struct mbuf *n = m->m_next;
	spl_t s;

	if (m->m_type == MT_FREE) {
		panic("freeing free mbuf");
		return 0;
	}
	s = splimp();
	MBUF_LOCK();
	if (m->m_flags & M_EXT && MCLREFERENCED(m))		/* Unlink with lock held */
	{
		remque((queue_entry_t)&m->m_ext.ext_ref);
		m->m_flags &= ~M_EXT;
	}

	mbstat.m_mbufs--;
	mbstat.m_mtypes[m->m_type]--;
	if (m->m_flags & M_EXT && m->m_ext.ext_free == NULL)
		mbstat.m_clusters--;
	MBUF_UNLOCK();
	splx(s);
	m->m_type = MT_FREE;
	if (m->m_flags & M_EXT)
	{
		if (m->m_ext.ext_free == NULL)
			kfree((vm_offset_t)m->m_ext.ext_buf, m->m_ext.ext_size); /* Free cluster */
		else {
			(*(m->m_ext.ext_free))(m->m_ext.ext_buf,
					       m->m_ext.ext_size, m->m_ext.ext_arg);
		}
	}
	kfree((vm_offset_t)m, MSIZE); /* Free mbuf */
	return (n);
}

void
m_freem(register struct mbuf *m)
{
	while (m)
		m = m_free(m);
}

/*
 * Mbuffer utility routines.
 */

/*
 * Compute the amount of space available
 * before the current start of data in an mbuf.
 */
int
m_leadingspace(register struct mbuf *m)
{
	if (m->m_flags & M_EXT) {
		if (MCLREFERENCED(m))
			return 0;
		return (m->m_data - m->m_ext.ext_buf);
	}
	if (m->m_flags & M_PKTHDR)
		return (m->m_data - m->m_pktdat);
	return (m->m_data - m->m_dat);
}

/*
 * Compute the amount of space available
 * after the end of data in an mbuf.
 */
int
m_trailingspace(register struct mbuf *m)
{
	if (m->m_flags & M_EXT) {
		if (MCLREFERENCED(m))
			return 0;
		return (m->m_ext.ext_buf + m->m_ext.ext_size -
			(m->m_data + m->m_len));
	}
	return (&m->m_dat[MLEN] - (m->m_data + m->m_len));
}

/*
 * Lesser-used path for M_PREPEND:
 * allocate new mbuf to prepend to chain,
 * copy junk along.
 */
struct mbuf *
m_prepend(register struct mbuf *m,
	  int len,
	  int how)
{
	struct mbuf *mn;

	MGET(mn, how, m->m_type);
	if (mn == (struct mbuf *)NULL) {
		m_freem(m);
		return ((struct mbuf *)NULL);
	}
	if (m->m_flags & M_PKTHDR) {
		M_COPY_PKTHDR(mn, m);
		m->m_flags &= ~M_PKTHDR;
	}
	mn->m_next = m;
	m = mn;
	if (len < MHLEN)
		MH_ALIGN(m, len);
	m->m_len = len;
	return (m);
}

/*
 * Make a copy of an mbuf chain starting "off0" bytes from the beginning,
 * continuing for "len" bytes.  If len is M_COPYALL, copy to end of mbuf.
 * The wait parameter is a choice of M_WAIT/M_DONTWAIT from caller.
 */
struct mbuf *
m_copym(register struct mbuf *m,
	int off0,
	register int len,
	int wait)
{
	struct mbuf *n;
	register struct mbuf **np;
	register int off = off0;
	struct mbuf *top;
	int copyhdr = 0;

	if (off < 0 || len < 0) {
		panic("m_copym sanity");
		return 0;
	}
	if (off == 0 && m->m_flags & M_PKTHDR)
		copyhdr = 1;
	while (off > 0) {
		if (m == 0) {
			panic("m_copym offset");
			return 0;
		}
		if (off < m->m_len)
			break;
		off -= m->m_len;
		m = m->m_next;
	}
	np = &top;
	top = 0;
	while (len > 0) {
		if (m == 0) {
			if (len != M_COPYALL) {
				panic("m_copym length");
				goto nospace;
			}
			break;
		}
		MGET(n, wait, m->m_type);
		*np = n;
		if (n == 0)
			goto nospace;
		if (copyhdr) {
			M_COPY_PKTHDR(n, m);
			if (len == M_COPYALL)
				n->m_pkthdr.len -= off0;
			else
				n->m_pkthdr.len = len;
			copyhdr = 0;
		}
		n->m_len = MIN(len, m->m_len - off);
		if (m->m_flags & M_EXT) {
			spl_t s = splimp();
			MBUF_LOCK();
			n->m_ext = m->m_ext;
			insque((queue_entry_t)&n->m_ext.ext_ref,
			       (queue_entry_t)&m->m_ext.ext_ref);
			MBUF_UNLOCK();
			splx(s);
			n->m_data = m->m_data + off;
			n->m_flags |= M_EXT;
		} else {
			/*
			 * Try to compress sequential, identical, small
			 * mbufs into a cluster. Do this carefully, but
			 * since we didn't copy flags before, we'll play
			 * loose for now. Note that MT_DATA is effectively
			 * identical to MT_HEADER, which are 1 and 2.
			 */
			register int extra = 0;
			if (len > n->m_len) {
				register struct mbuf *peek = m->m_next;
				while (peek && !(peek->m_flags & M_EXT) &&
				    (m->m_type == peek->m_type ||
				     m->m_type + peek->m_type ==
						MT_DATA + MT_HEADER)) {
					extra += peek->m_len;
					peek = peek->m_next;
				}
				if (n->m_len + extra > len)
					extra = len - n->m_len;
				if (extra > M_TRAILINGSPACE(n) + MLEN)
					MCLGET(n, wait);
			}
			bcopy(mtod(m, caddr_t)+off, mtod(n, caddr_t),
			    (unsigned)n->m_len);
			if (extra > 0 && extra <= M_TRAILINGSPACE(n)) {
				off = n->m_len;
				n->m_len += extra;
				do {
					m = m->m_next;
					bcopy(mtod(m, caddr_t),
						mtod(n, caddr_t)+off,
						(unsigned)MIN(extra, m->m_len));
					off += m->m_len;
				} while ((extra -= m->m_len) > 0);
			}
		}
		if (len != M_COPYALL)
			len -= n->m_len;
		off = 0;
		m = m->m_next;
		np = &n->m_next;
	}
	return (top);
nospace:
	m_freem(top);
	return (0);
}

/*
 * Copy data from an mbuf chain starting "off" bytes from the beginning,
 * continuing for "len" bytes, into the indicated buffer.
 */
void
m_copydata(register struct mbuf *m,
	   register int off,
	   register int len,
	   caddr_t cp)
{
	register unsigned count;

	if (off < 0 || len < 0) {
		panic("m_copydata sanity");
		return;
	}
	while (off > 0) {
		if (m == 0) {
			panic("m_copydata offset");
			return;
		}
		if (off < m->m_len)
			break;
		off -= m->m_len;
		m = m->m_next;
	}
	while (len > 0) {
		if (m == 0) {
			panic("m_copydata length");
			return;
		}
		count = MIN(m->m_len - off, len);
		bcopy(mtod(m, caddr_t) + off, cp, count);
		len -= count;
		cp += count;
		off = 0;
		m = m->m_next;
	}
}

/*
 * Concatenate mbuf chain n to m.
 * Both chains must be of the same type (e.g. MT_DATA).
 * Any m_pkthdr is not updated.
 */
void
m_cat(register struct mbuf *m,
      register struct mbuf *n)
{
	while (m->m_next)
		m = m->m_next;
	while (n) {
		if (n->m_len > MINCLSIZE || n->m_len > m_trailingspace(m)) {
			/* just join the two chains */
			m->m_next = n;
			return;
		}
		/* splat the data from one into the other */
		bcopy(mtod(n, caddr_t), mtod(m, caddr_t) + m->m_len,
		    (u_int)n->m_len);
		m->m_len += n->m_len;
		n = m_free(n);
	}
}

void
m_adj(struct mbuf *mp, int req_len)
{
	register int len = req_len;
	register struct mbuf *m;
	register count;

	if ((m = mp) == NULL)
		return;
	if (len >= 0) {
		/*
		 * Trim from head.
		 */
		while (m != NULL && len > 0) {
			if (m->m_len <= len) {
				len -= m->m_len;
				m->m_len = 0;
				m = m->m_next;
			} else {
				m->m_len -= len;
				m->m_data += len;
				len = 0;
			}
		}
		if ((m = mp)->m_flags & M_PKTHDR)
			m->m_pkthdr.len -= (req_len - len);
	} else {
		/*
		 * Trim from tail.  Scan the mbuf chain,
		 * calculating its length and finding the last mbuf.
		 * If the adjustment only affects this mbuf, then just
		 * adjust and return.  Otherwise, rescan and truncate
		 * after the remaining size.
		 */
		len = -len;
		count = 0;
		for (;;) {
			count += m->m_len;
			if (m->m_next == (struct mbuf *)0)
				break;
			m = m->m_next;
		}
		if (m->m_len >= len) {
			m->m_len -= len;
			if ((m = mp)->m_flags & M_PKTHDR)
				m->m_pkthdr.len -= len;
			return;
		}
		count -= len;
		if (count < 0)
			count = 0;
		/*
		 * Correct length for chain is "count".
		 * Find the mbuf with last data, adjust its length,
		 * and toss data from remaining mbufs on chain.
		 */
		if ((m = mp)->m_flags & M_PKTHDR)
			m->m_pkthdr.len = count;
		for (; m; m = m->m_next) {
			if (m->m_len >= count) {
				m->m_len = count;
				break;
			}
			count -= m->m_len;
		}
		while (m = m->m_next)
			m->m_len = 0;
	}
}

/*
 * Rearange an mbuf chain so that len bytes are contiguous
 * and in the data area of an mbuf (so that mtod and dtom
 * will work for a structure of size len).  Returns the resulting
 * mbuf chain on success, frees it and returns null on failure.
 * If there is room, it will add up to max_protohdr-len extra bytes to the
 * contiguous region in an attempt to avoid being called next time.
 */
struct mbuf *
m_pullup(register struct mbuf *n,
	 int len)
{
	struct mbuf *m;
	register int count;
	int space;

	/*
	 * If first mbuf has no cluster, and has room for len bytes
	 * without shifting current data, pullup into it,
	 * otherwise allocate a new mbuf to prepend to the chain.
	 */
	if ((n->m_flags & M_EXT) == 0 &&
	    n->m_data + len < &n->m_dat[MLEN] && n->m_next) {
		if (n->m_len >= len)
			return (n);
		m = n;
		n = n->m_next;
		len -= m->m_len;
	} else {
		if (len > MHLEN)
			goto bad;
		MGET(m, M_DONTWAIT, n->m_type);
		if (m == 0)
			goto bad;
		m->m_len = 0;
		if (n->m_flags & M_PKTHDR) {
			M_COPY_PKTHDR(m, n);
			n->m_flags &= ~M_PKTHDR;
		}
	}
	space = &m->m_dat[MLEN] - (m->m_data + m->m_len);
	do {
		count = MIN(MIN(MAX(len, max_protohdr), space), n->m_len);
		bcopy(mtod(n, caddr_t), mtod(m, caddr_t) + m->m_len,
		  (unsigned)count);
		len -= count;
		m->m_len += count;
		n->m_len -= count;
		space -= count;
		if (n->m_len)
			n->m_data += count;
		else
			n = m_free(n);
	} while (len > 0 && n);
	if (len > 0) {
		(void) m_free(m);
		goto bad;
	}
	m->m_next = n;
	return (m);
bad:
	m_freem(n);
	return (0);
}
