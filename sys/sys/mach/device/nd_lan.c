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
 *   (C) COPYRIGHT International Business Machines Corp. 1988,1993
 *   All Rights Reserved
 *   Licensed Materials - Property of IBM
 *   US Government Users Restricted Rights - Use, duplication or
 *   disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
 */

#include <device/errno.h>
#include <kern/lock.h>
#include <device/cdli.h>
#include <device/if_hdr.h>
#include <vm/vm_kern.h>

#define AF_MAX 20
#define THEMAX (2 * AF_MAX)
struct af_ent	af_table[THEMAX];

/* TODO: Augment spl()s with correct locking techniques. */

/* Very inefficient - need a real bcmp  FIXME*/
int
nd_bcmp(register unsigned char *a, register unsigned char *b, register int len)
{
	register int r;

	while (len--)
		if (!(r = *a++ - *b++))
			return(r);
	return(0);
}

/***************************************************************************
*
*	Process incoming 802.2 packets. Look for user and distribute packets.
*
***************************************************************************/
void
dmx_8022_receive(struct ndd *nddp,
		 struct mbuf *m,
		 int llcoffset)
{
	struct ns_8022_user	*ns_8022_user;
	caddr_t 		macp;		/* pointer to mac header */
	struct ie2_llc_snaphdr  *llcp;
	int 			headerlen = 0;	/* length of header the
						   isr doesn't care about */

	assert(nddp->ndd_nsdemux != (struct ns_dmx_ctl *) NULL);

	/*
	 * Make sure enough of the header is contiguous
	 */
	macp = mtod(m, caddr_t);
	llcp = (struct ie2_llc_snaphdr *) (macp + llcoffset);
	if (m->m_len < (llcoffset + sizeof(struct ie2_llc_snaphdr)) ) {
		m = m_pullup(m, llcoffset + sizeof(struct ie2_llc_snaphdr));
		if (m == (struct mbuf *) NULL) {
			nddp->ndd_demuxer->nd_dmxstats.nd_nobufs++;
			return;
		}
	}

	/*
	 *	Locate user that's done an ns_add_filter for this packet.
	 */
	DMX_8022_GET_USER(ns_8022_user, llcp->dsap, llcp->type,
		llcp->prot_id, nddp->ndd_nsdemux);
	if (ns_8022_user == NULL) {
		nddp->ndd_demuxer->nd_dmxstats.nd_nofilter++;
		m_freem(m);
		return;
	}

	/*
	 *	If it's not a UI packet, see what user wants done.
	 */
	if ( (llcp->ctrl != CTRL_UI) &&
	     (ns_8022_user->user.pkt_format & NS_HANDLE_NON_UI) ) {
		dmx_non_ui(nddp, m, llcoffset);
		return;
	}

	/*
	 *	Fix up headers as requested.
	 */
	switch (ns_8022_user->user.pkt_format & ~NS_HANDLE_NON_UI) {
		case NS_PROTO:
			headerlen = llcoffset + sizeof(struct ie2_llc_hdr);
			break;
		case NS_PROTO_SNAP:
			headerlen = llcoffset + sizeof(struct ie2_llc_snaphdr);
			break;
		case NS_INCLUDE_LLC:
			headerlen = llcoffset;
			break;
		case NS_INCLUDE_MAC:
		default:
			break;
	}

	if (headerlen) {
		m->m_pkthdr.len -= headerlen;
		m->m_len -= headerlen;
		m->m_data += headerlen;
	}

	/*
	 *	All done. Give it to user.
	 */
/*	IFSTUFF_AND_DELIVER(ns_8022_user, nddp, m, macp);*/
{
	int	x;

	if (ns_8022_user->user.ifp) {
		if ((ns_8022_user->user.ifp->if_flags & IFF_UP) == 0) {
			m_freem(m);
			ns_8022_user->user.ifp->if_rcvdrops++;
			goto out;
		} else {
			ns_8022_user->user.ifp->if_ipackets++;
/*			ns_8022_user->user.ifp->if_ibytes += m->m_pkthdr.len;*/
			m->m_pkthdr.rcvif = ns_8022_user->user.ifp;
		}
	}
	if (ns_8022_user->user.protoq == NULL)
		(*(ns_8022_user->user.isr))(nddp, m, macp, ns_8022_user->user.isr_data);
	else {
		x = splimp();
		/* if not full, enqueue and schednetisr	*/
		if (IF_QFULL(ns_8022_user->user.protoq)) {
			IF_DROP(ns_8022_user->user.protoq);
			splx(x);
			m_freem(m);
		} else {
			IF_ENQUEUE(ns_8022_user->user.protoq, m);
			splx(x);
			panic("schednetisr(ns_8022_user->user.netisr);");
		}
	}
out:	;
}
	return;
}

/***************************************************************************
*
*	Respond to non-ui (ie XID and TEST) requests.
* 	DA/SA swapping is done in demuxer's nd_response()
*
***************************************************************************/
void
dmx_non_ui(struct ndd *nddp,
	   struct mbuf *m,
	   int llcoffset)
{
	caddr_t 			macp;
	struct ie2_xid_ifield		*llcp;
	unsigned char			t;

	macp = mtod(m, caddr_t);
	llcp = (struct ie2_xid_ifield *) (macp + llcoffset);

	if (llcp->ssap & SSAP_RESP) {		/* rcv Response	*/
		m_freem(m);
		return;
	}

	/*
	 * swap DSAP/SSAP
	 */
	t = llcp->dsap;
	llcp->dsap = llcp->ssap;
	llcp->ssap = t | SSAP_RESP;			/* set response bit  */

	if ((llcp->ctrl & 0xef) == CTRL_XID ) {
		llcp->xid_info[0] = 0x81; /* IEEE 802.2 format */
		llcp->xid_info[1] = 0x01; /* connectionless */
		llcp->xid_info[2] = 0x00; /* window size */
		m->m_len = llcoffset + sizeof (struct ie2_xid_ifield);
	} else {
		if ((llcp->ctrl & 0xe3) != CTRL_TEST ) {
			m_freem(m);
			return;
		}
	}

	if (nddp->ndd_demuxer->nd_response)
		(*(nddp->ndd_demuxer->nd_response)) (nddp, m, llcoffset);
	else
		m_freem(m);
	return;
}

/***************************************************************************
*
*	dmx_8022_add_filter() -  Add 802.2 filter to the demuxer
*
*	RETURNS :	0      - no errors
*			EEXIST - sap/type already in table
*			ENOMEM - couldn't allocate entry
***************************************************************************/
int
dmx_8022_add_filter(struct ndd *nddp,
		    struct ns_8022 *filter,
		    struct ns_user *ns_user)
{
	int			x;
	int			error;
	struct ns_8022_user	*new_user;

	if (!(new_user = (struct ns_8022_user *)kalloc((vm_size_t)sizeof(struct ns_8022_user))))
		return(CDLI_ERR_NOMEM);

	bzero((char *)new_user, sizeof(struct ns_8022_user));
	new_user->user = *ns_user;
	new_user->filter = *filter;

	if (dmx_8022_get_user(filter, nddp)) {
		kfree((vm_offset_t)new_user, (vm_size_t)sizeof(struct ns_8022_user));
		return(CDLI_ERR_FILTER_EXISTS);
	}
	x = splimp();
	insque((queue_entry_t)new_user,
	       (queue_entry_t)nddp->ndd_nsdemux->dsap_heads[filter->dsap].prev);
	splx(x);
	return(0);
}


/***************************************************************************
*	dmx_8022_del_filter() - Deletes a 802.2 filter
*
*	RETURNS :	0      - no errors
*			ENOENT - the type was not found
***************************************************************************/
int
dmx_8022_del_filter(struct ndd *nddp,
		    struct ns_8022 *filter)
{
	int			x;
	struct ns_8022_user	*fup;

	fup = dmx_8022_get_user(filter, nddp);
	if (fup == NULL)
		return(CDLI_ERR_FILTER_DOESNT_EXIST);

	x = splimp();
	remque((queue_entry_t)fup);
	splx(x);
	kfree((vm_offset_t)fup, (vm_size_t)sizeof(struct ns_8022_user));
	return(0);
}

/***************************************************************************
*
*	dmx_add_status() -  Add status filter to the demuxer
*
***************************************************************************/
int
dmx_add_status(struct ndd *nddp,
	       struct ns_com_status *filter,
	       struct ns_statuser *ns_statuser)
{
	struct com_status_user	*new_status_user;
	struct com_status_user	*status_head;
	int			x;

	if (filter->filtertype != NS_STATUS_MASK)
		return(EINVAL);

	status_head = &(nddp->ndd_nsdemux->com_status_head);

	if (!(new_status_user =
	      (struct com_status_user *)kalloc((vm_size_t)sizeof(struct com_status_user))))
		return(CDLI_ERR_NOMEM);
	bzero((char *)new_status_user, sizeof(*new_status_user));
	filter->sid = new_status_user;
	new_status_user->user = *ns_statuser;
	new_status_user->filter = *filter;
	x = splimp();
	insque((queue_entry_t)new_status_user,
	       (queue_entry_t)status_head->prev);
	splx(x);
	return(0);
}

/***************************************************************************
*
*	dmx_del_status() -  Delete a status filter from the demuxer
*
***************************************************************************/
int
dmx_del_status(struct ndd *nddp,
	       struct ns_com_status *filter)
{
	struct com_status_user	*sup;
	struct com_status_user	*status_head;
	int			x;

	if (filter->filtertype != NS_STATUS_MASK)
		return(CDLI_ERR_FILTER_DOESNT_EXIST);

	status_head = &(nddp->ndd_nsdemux->com_status_head);
	for (sup = status_head->next; sup != status_head; sup = sup->next) {
		if (sup->filter.sid == filter->sid)
			break;
	}

	x = splimp();
	if (sup != status_head) {
		remque((queue_entry_t)sup);
		splx(x);
		kfree((vm_offset_t)sup, (vm_size_t)sizeof(struct ns_com_status));
		return(0);
	}

	splx(x);
	return(CDLI_ERR_FILTER_DOESNT_EXIST);
}

/***************************************************************************
*
*	Deliver status to all registered users that match the status.
*
***************************************************************************/
#if 0  /* FIXME - Not Implemented */
void
dmx_status(struct ndd *nddp,
	   struct ndd_statblk *status)
{
	struct com_status_user	*sup;
	struct com_status_user	*status_head;

	if (nddp->ndd_nsdemux == (struct ns_dmx_ctl *) NULL)
		return;

	status_head = &(nddp->ndd_nsdemux->com_status_head);

	for (sup = status_head->next; sup != status_head; sup = sup->next) {
		if ( (sup->filter.mask & status->code) != 0 )
			(*(sup->user.isr)) (nddp, status, sup->user.isr_data, 0);
	}
	return;
}
#endif

/***************************************************************************
*
*	dmx_8022_get_user() - retrieves a user based upon filter
*
***************************************************************************/
struct ns_8022_user *
dmx_8022_get_user(struct ns_8022 *filter,
		  struct ndd *nddp)
{
	struct ns_8022_user	*ns_8022_user;

	assert(nddp->ndd_nsdemux != (struct ns_dmx_ctl *) NULL);

	DMX_8022_GET_USER(ns_8022_user, filter->dsap, filter->ethertype,
		filter->orgcode, nddp->ndd_nsdemux);
	return(ns_8022_user);
}


/***************************************************************************
*
*	Initialize common demuxer services for a NDD.
*
***************************************************************************/
int
dmx_init(struct ndd *nddp)
{
	struct com_status_user		*status_head;
	struct ns_8022_user_head	*dsap_heads;
	int				i;

	if (nddp->ndd_nsdemux != NULL)
		return(0);

	if (!(nddp->ndd_nsdemux =
	      (struct ns_dmx_ctl *)kalloc((vm_size_t)sizeof(struct ns_dmx_ctl))))
		return(CDLI_ERR_NOMEM);
	bzero((char *)nddp->ndd_nsdemux, sizeof(nddp->ndd_nsdemux));

	status_head = &(nddp->ndd_nsdemux->com_status_head);
	status_head->next = status_head->prev = status_head;
	dsap_heads = nddp->ndd_nsdemux->dsap_heads;
	for (i = 0 ; i < NS_MAX_SAPS ; i++)
		dsap_heads[i].next = dsap_heads[i].prev =
			(struct ns_8022_user *)&dsap_heads[i];
	return(0);
}

/***************************************************************************
*
*	Terminate common demuxer services for a NDD.
*
***************************************************************************/
void
dmx_term(struct ndd *nddp)
{
	if (nddp->ndd_nsdemux == NULL)
		return;

	kfree((vm_offset_t)nddp->ndd_nsdemux, (vm_size_t)sizeof(struct ns_dmx_ctl));
	nddp->ndd_nsdemux = NULL;
	return;
}


/***************************************************************************
*
*	RETURNS :
*			EEXIST - af already in table
* 			EINVAL - af out of range
*			0      - no errors
*
***************************************************************************/
int
nd_config_proto(u_short af,
		struct config_proto *config_proto)
{
	int		x;

	if (af >= THEMAX)
		return(CDLI_ERR_AF_OUT_OF_RANGE);

	if (af_table[af].sap)
		return(CDLI_ERR_PROTO_EXISTS);

	x = splnet();
	af_table[af].config = *config_proto;
	af_table[af].sap = 1;
	splx(x);
	return(0);
}

/***************************************************************************
*	Path for DLPI protocols to register.
*
*	RETURNS :
*			EEXIST - sap/type already in table
*			ENOMEM - table overflow.
*			0      - no errors
*
***************************************************************************/
int
nd_config_sap(u_short sap,
	      u_short type,
	      struct config_proto *config_proto,
	      u_short *af)
{
	int		i, x;

	for (i=(THEMAX/2); i < THEMAX; i++) {
		if (af_table[i].sap) {
			if (af_table[i].sap == sap && af_table[i].type == type)
				return (CDLI_ERR_PROTO_EXISTS);
			continue;
		} else
			break;
	}

	if (i >= THEMAX)
		return (CDLI_ERR_AF_OUT_OF_RANGE);

	if (x = nd_config_proto(i,config_proto))
		return(x);

	x = splnet();
	af_table[i].sap = sap;
	af_table[i].type = type;
	splx(x);
	*af = i;
	return(0);
}

/***************************************************************************
*	Routine for DLPI to find registered address families
*
*	RETURNS :
*			af
*			0      - not found
*
***************************************************************************/
int
nd_find_af(u_short sap,
	   u_short type)
{
	int		i;

	for (i=(THEMAX/2); i < THEMAX; i++) {
		if (af_table[i].sap) {
			if (af_table[i].sap == sap && af_table[i].type == type)
				return (i);
			continue;
		} else
			return(0);
	}
	return(0);
}
