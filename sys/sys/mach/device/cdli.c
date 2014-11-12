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
 *   (C) COPYRIGHT International Business Machines Corp. 1993
 *   All Rights Reserved
 *   Licensed Materials - Property of IBM
 *   US Government Users Restricted Rights - Use, duplication or
 *   disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
 */

#include <device/errno.h>
#include <sys/types.h>
#include <kern/lock.h>
#include <string.h>

/* Needed for ifnet routines */
#include <device/if_hdr.h>
#include <device/net_io.h>
#include <device/io_req.h>
#include <device/device_typedefs.h>
#include <device/ds_routines.h>
#include <device/cdli.h>
#include <machine/endian.h>

struct ndd		*ndd = NULL;
decl_simple_lock_data(,ndd_lock_data)

struct ns_demuxer	*demuxers = NULL;
decl_simple_lock_data(,ns_demuxer_lock_data)
decl_simple_lock_data(,if_en_lock_data)

/***************************************************************************
*
*	Notes:	
*		Lock hierarchy	
*			ndd_lock
*			ns_demuxer_lock 
*			per ndd lock
*	
***************************************************************************/

/***************************************************************************
*
*	ns_init() -  CDLI initialization
*	
***************************************************************************/
int
ns_init(void)
{
	struct cdli_inits *cdli_ip;
	int	ret;

	CDLI_LOCK_INIT(&ndd_lock_data);
	CDLI_LOCK_INIT(&ns_demuxer_lock_data);
	CDLI_LOCK_INIT(&if_en_lock_data);

	cdli_ip = cdli_inits;

	while (cdli_ip->init_rtn)
	{
		if (ret = (cdli_ip->init_rtn)())
			return(ret);
		cdli_ip++;
	}

	return(KERN_SUCCESS);
}

/***************************************************************************
*
*	ns_get_demuxer() -  Search the demuxer chain for a demuxer of the
*			    given type. Returns a pointer to that demuxer.
*	
***************************************************************************/
struct ns_demuxer *
ns_get_demuxer(u_short type)
{
	struct ns_demuxer *nd;

	assert(CDLI_IS_LOCKED(ns_demuxer_lock_data));

	for (nd = demuxers; nd; nd = nd->nd_next) {
		if (nd->nd_type == type)
			break;
	}
	return(nd);
}

/***************************************************************************
*
*	ns_add_demux() - Add a demuxer to the list of available demuxers.
*	
***************************************************************************/
int
ns_add_demux(u_long ndd_type,
	     struct ns_demuxer *demuxer)
{
	struct ns_demuxer 	**p = &demuxers;

	CDLI_LOCK(&ns_demuxer_lock_data);
	if (ns_get_demuxer(ndd_type)) {
		CDLI_UNLOCK(&ns_demuxer_lock_data);
		return(CDLI_ERR_DEMUX_EXISTS);
	}

	while (*p)
		p = &((*p)->nd_next);
	*p = demuxer;
	demuxer->nd_next = 0;
	demuxer->nd_type = ndd_type;
	CDLI_UNLOCK(&ns_demuxer_lock_data);
	return(KERN_SUCCESS);	
}

/***************************************************************************
*
*	ns_del_demux() - Remove a demuxer from the list of available demuxers.
*	
***************************************************************************/
int
ns_del_demux(u_int ndd_type)
{
	struct ns_demuxer 	**p = &demuxers;
	int			error = KERN_SUCCESS;

	CDLI_LOCK(&ns_demuxer_lock_data);
	while (*p && (*p)->nd_type != ndd_type) 
		p = &((*p)->nd_next);

	if (*p == (struct ns_demuxer *) NULL) 
		error = CDLI_ERR_DEMUX_DOESNT_EXIST;
	else {
		if ((*p)->nd_inuse)
			error = CDLI_ERR_DEMUX_IN_USE;
		else
			*p = (*p)->nd_next;
	}

	CDLI_UNLOCK(&ns_demuxer_lock_data);
	return(error);	
}

/***************************************************************************
*
*	ns_add_filter() -  Pass "add filter" request on to demuxer
*	
***************************************************************************/
int
ns_add_filter(struct ndd *nddp,
	      caddr_t filter,
	      int len,
	      struct ns_user *ns_user)
{
	return((*(nddp->ndd_demuxer->nd_add_filter))
		(nddp, filter, len, ns_user));
}

/***************************************************************************
*
*	ns_del_filter() -  Pass "delete filter" request to demuxer
*	
***************************************************************************/
int
ns_del_filter(struct ndd *nddp,
	      caddr_t filter,
	      int len)
{
	return((*(nddp->ndd_demuxer->nd_del_filter))(nddp, filter, len));
}

/***************************************************************************
*
*	ns_add_status() -  Pass add status filter request to demuxer. 
*	
***************************************************************************/
int
ns_add_status(struct ndd *nddp,
	      caddr_t filter,
	      int len,
	      struct ns_statuser *ns_statuser)
{
	return((*(nddp->ndd_demuxer->nd_add_status))
		(nddp, filter, len, ns_statuser));
}

/***************************************************************************
*
*	ns_del_status() -  Pass delete status filter request to demuxer.
*	
***************************************************************************/
int
ns_del_status(struct ndd *nddp,
	      caddr_t filter,
	      int len)
{
	return((*(nddp->ndd_demuxer->nd_del_status))(nddp, filter, len));
}

/***************************************************************************
*
*	ns_attach() -  Attach a NDD to the list of available drivers.
*	
***************************************************************************/
int
ns_attach(struct ndd *nddp)
{
	register struct ndd **p = &ndd;

	assert(nddp != 0);

	CDLI_LOCK(&ndd_lock_data);
	if (ns_locate(nddp->ndd_name)) {
		CDLI_UNLOCK(&ndd_lock_data);
		return(CDLI_ERR_NS_EXISTS);
	}

	while (*p)
		p = &((*p)->ndd_next);
	*p = nddp;
	nddp->ndd_next = 0;
	nddp->ndd_refcnt = 0;
	CDLI_LOCK_INIT(&nddp->ndd_lock_data);
	CDLI_UNLOCK(&ndd_lock_data);
	return(KERN_SUCCESS);
}

/***************************************************************************
*
*	ns_detach() -  Detach a NDD from the list of available drivers.
*	
***************************************************************************/
int
ns_detach(register struct ndd *nddp)
{
	register struct ndd 	**p = &ndd;
	int 			error = KERN_SUCCESS;

	CDLI_LOCK(&ndd_lock_data);
	CDLI_LOCK(&nddp->ndd_lock_data);

	if (nddp->ndd_refcnt != 0) {
		CDLI_UNLOCK(&ndd_lock_data);
		CDLI_UNLOCK(&nddp->ndd_lock_data);
		return(EBUSY);
	}	

	while (*p && *p != nddp) 
		p = &((*p)->ndd_next);

	if (*p == (struct ndd *) NULL) 
		error = CDLI_ERR_NS_DOESNT_EXIST;
	else 
		*p = (*p)->ndd_next;

	CDLI_UNLOCK(&nddp->ndd_lock_data);
	CDLI_UNLOCK(&ndd_lock_data);
	return(error);
}

struct ndd *
ns_locate_wo_lock(register char *name)
{
	struct ndd *nddp;

	CDLI_LOCK(&ndd_lock_data);
	nddp = ns_locate(name);
	CDLI_UNLOCK(&ndd_lock_data);
	return(nddp);
}

/***************************************************************************
*
* 	ns_locate() - Locates NDD based on NDD name. Returns NDD
*		      pointer. Must be called with ndd_lock.
*
***************************************************************************/
struct ndd *
ns_locate(register char *name)
{
	register struct ndd *nddp;

	assert(CDLI_IS_LOCKED(ndd_lock_data));

	for (nddp = ndd; nddp; nddp = nddp->ndd_next) {
		if (!strncmp(nddp->ndd_name, name, NDD_MAXNAMELEN))
			break;
	}
	return (nddp);
}

/***************************************************************************
*
*	ns_alloc() -  Allocate use of a NDD.
*		      This function takes a NDD name as input and returns
*		      a struct ndd pointer if the NDD is succesfully
*		      allocated. If this is the first alloc for a particular
*		      NDD, an associated demuxer must be located and the NDD
*		      must be opened.
*	
***************************************************************************/
int
ns_alloc(register char *nddname,
	 struct ndd **nddpp)
{
	register struct ndd 	*nddp;
	int			error = KERN_SUCCESS;
	struct ns_demuxer	*demuxer;

	/*
	 * First see if we can find the requested NDD.
	 */
	CDLI_LOCK(&ndd_lock_data);
	nddp = ns_locate(nddname);
	if (nddp == (struct ndd *) NULL)
	{
		error = CDLI_ERR_NDD_DOESNT_EXIST;
		goto ndd_unlock;
	}

	CDLI_LOCK(&ns_demuxer_lock_data);
	/* 
	 * Locate demuxer for the NDD. If there is already an associated
	 * demuxer, a demuxer pointer will be in the ndd struct. If not
	 * look for a demuxer based on NDD type.
	 */
	demuxer = nddp->ndd_demuxer;
	if (demuxer == (struct ns_demuxer *) NULL) {
		demuxer = ns_get_demuxer(nddp->ndd_type);
		if (demuxer == NULL)
		{
			error = CDLI_ERR_DEMUX_DOESNT_EXIST;
			goto demux_unlock;
		}
	}

	/* 
	 * If this is the first alloc for a NDD the reference count
	 * will be zero and the NDD must be opened.
	 */
	CDLI_LOCK(&nddp->ndd_lock_data);
	if (nddp->ndd_refcnt == 0) {
		if (nddp->ndd_demuxer != (struct ns_demuxer *) NULL)
			nddp->ndd_demuxsource = 1;
		else {
			nddp->ndd_demuxer = demuxer;
			nddp->ndd_demuxsource = 0;
		}
		/*
		 * Initialize demuxer if it's using common services.
		 */
		if (demuxer->nd_use_nsdmx)
			error = dmx_init(nddp);

		if (!error) {
			nddp->nd_receive = (void (*)(struct ndd *, caddr_t))demuxer->nd_receive;
			nddp->nd_status = demuxer->nd_status;
			error = (*(nddp->ndd_open))(nddp);
		}
	}
	if (error == KERN_SUCCESS) {
		nddp->ndd_refcnt++;
		*nddpp = nddp;
	} else {
		dmx_term(nddp);
	}
	CDLI_UNLOCK(&nddp->ndd_lock_data);
demux_unlock:
	CDLI_UNLOCK(&ns_demuxer_lock_data);
ndd_unlock:
	CDLI_UNLOCK(&ndd_lock_data);
	return(error);
}

/***************************************************************************
*
*	ns_free() -  Relinquish use of a NDD.
*	
***************************************************************************/
void
ns_free(struct ndd *nddp)
{
	CDLI_LOCK(&nddp->ndd_lock_data);
	nddp->ndd_refcnt--;
	assert(nddp->ndd_refcnt >= 0);

	/* 
	 * If reference count drops to zero, close NDD.
	 */
	if (nddp->ndd_refcnt == 0) {
		(*(nddp->ndd_close))(nddp);
		dmx_term(nddp);
		/* 
		 * If the demuxer was not provided by the NDD need to NULL 
		 * it out in case it changes before the next alloc.
		 */
		if (nddp->ndd_demuxsource = 1)
			nddp->ndd_demuxsource = 0;
		else
			nddp->ndd_demuxer = (struct ns_demuxer *) NULL;
	}
	CDLI_UNLOCK(&nddp->ndd_lock_data);
}

void
ether_input(struct ifnet *ifp,
	    register struct ether_header *eh,
	    struct mbuf *m)
{
	ipc_kmsg_t	new_kmsg;
	register int	length = m->m_pkthdr.len;
	struct ether_header *ehp;
	struct packet_header *pkt;

	new_kmsg = net_kmsg_get();

	if (new_kmsg == IKM_NULL)
	{
		/* Drop the packet */
		ifp->if_rcvdrops++;
		m_freem(m);
		return;
	}

	ehp = (struct ether_header *) (&net_kmsg(new_kmsg)->header[0]);
	pkt = (struct packet_header *) (&net_kmsg(new_kmsg)->packet[0]);

	/* Copy in header */

	bcopy((caddr_t)eh, (caddr_t)ehp, sizeof(*eh));

	/* Copy in data */

	m_copydata(m, 0, length, (caddr_t)(pkt + 1));
	m_freem(m);

	pkt->type = htons(ehp->ether_type);
	pkt->length = length + sizeof(struct packet_header);

	net_packet(ifp, new_kmsg, length + sizeof(struct packet_header),
		   ethernet_priority(new_kmsg), (io_req_t)0);
}

void
mbuf_free_ior(caddr_t buf, u_long size, caddr_t arg)
{
	iodone((io_req_t)arg);
}

void
if_dequeue_m(struct ifqueue *ifq, struct mbuf **m)
{
	register io_req_t ior;
	register struct mbuf *mb;
	
	IF_DEQUEUE(ifq, ior);

	if (!ior)
	{
		*m = 0;
		return;
	}

	MGETHDR(mb, M_WAIT, MT_HEADER);

	if (!mb)
	{
		iodone(ior);
		return;
	}

	*m = mb;

	mb->m_flags |= M_EXT;
	mb->m_ext.ext_size = ior->io_count;
	mb->m_ext.ext_arg = (caddr_t)ior;
	mb->m_ext.ext_free = mbuf_free_ior;
	mb->m_ext.ext_ref.forw = mb->m_ext.ext_ref.back = &mb->m_ext.ext_ref;
	mb->m_data = mb->m_ext.ext_buf = ior->io_data;
	mb->m_pkthdr.len = mb->m_len = ior->io_count;
	mb->m_pkthdr.rcvif = 0;
}
