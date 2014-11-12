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

#include <device/cdli.h>
#include <kern/lock.h>
#include <net/netisr.h>
#include <vm/vm_kern.h>
#include <device/eth_demux.h>

struct ns_demuxer	demuxer;
struct ns_demuxer	demuxer8023;
int			eth_users;
static u_char  		ebcastaddr[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

int
nd_eth_config(char *name, int type)
{
	struct nd_config nd;
	struct ndd *nddp;
	int	err;

	nd.ndd_type = NDD_ISO88023;

	if (err = eth_config(CFG_INIT, nd))
		return(err);

	if (!(nddp = (struct ndd *)kget(sizeof(struct ndd))))
	{
		printf("nd_eth_config: no memory\n");
		return(KERN_RESOURCE_SHORTAGE);
	}

	bzero((char *)nddp, sizeof(*nddp));
	nddp->ndd_name = name;
	nddp->ndd_type = type;
	return(KERN_SUCCESS);
}

int
eth_config(int cmd, struct nd_config nd_config)
{
	int				error;

	if ((cmd != CFG_INIT) && (cmd != CFG_TERM))
		return(CDLI_ERR_BAD_ETH_CONFIG_CMD);

	if (cmd == CFG_INIT) {
			error = eth_add_demuxer(nd_config.ndd_type);
		if (error) {
			return(error);
		}
	} else {
		/* don't unconfig if in use */
		if (eth_users > 0)
			return(CDLI_ERR_DEMUX_IN_USE);

		ns_del_demux(nd_config.ndd_type);
	}
	return(KERN_SUCCESS);
}

int
eth_add_filter(struct ndd *nddp,
	       struct ns_8022 *filter,
	       int len,
	       struct ns_user *ns_user)
{
	int 	error = KERN_SUCCESS;

	if (ns_user->isr == NULL && ns_user->netisr == 0)
		return(CDLI_ERR_ISR_MISSING);
	if (ns_user->protoq != NULL && ns_user->netisr == 0)
		return(CDLI_ERR_PROTOQ_MISSING);
	if (len != sizeof(struct ns_8022))
		return(CDLI_ERR_BAD_LEN);

	if (nddp->ndd_specdemux == (caddr_t) NULL) {
		if (error = eth_dmx_init(nddp))
			return(error);
	}

	switch (filter->filtertype) {
		case NS_8022_LLC_DSAP:
		case NS_8022_LLC_DSAP_SNAP:
			error = dmx_8022_add_filter(nddp, filter, ns_user);
			break;
		case NS_ETHERTYPE:
			error = eth_add_ethertype(nddp, filter, ns_user);
			break;
		case NS_TAP:
			error = eth_add_tap(nddp, ns_user);
			break;
		default:
			error = CDLI_ERR_BAD_FILTERTYPE;
			break;
	}

	if (!error)
		((*(nddp->ndd_ctl)) (nddp, NDD_ADD_FILTER, (caddr_t)filter, len));

	if (!error)
		eth_users++;
	return(error);
}

int
eth_del_filter(struct ndd *nddp,
	       struct ns_8022 *filter,
	       int len)
{
	int 			error = KERN_SUCCESS;
	struct eth_dmx_ctl	*eth_dmx_ctl;

	if (len != sizeof(struct ns_8022))
		return(CDLI_ERR_BAD_LEN);

	if (nddp->ndd_specdemux == (caddr_t) NULL)
		return(CDLI_ERR_DEMUX_DOESNT_EXIST);

	switch (filter->filtertype) {
		case NS_8022_LLC_DSAP:
		case NS_8022_LLC_DSAP_SNAP:
			error = dmx_8022_del_filter(nddp, filter);
			break;
		case NS_ETHERTYPE:
			error = eth_del_ethertype(nddp, filter);
			break;
		case NS_TAP:
			error = eth_del_tap(nddp);
			break;
		default:
			error = CDLI_ERR_BAD_FILTERTYPE;
			break;
	}

	if (!error)
		((*(nddp->ndd_ctl)) (nddp, NDD_DEL_FILTER, (caddr_t)filter, len));

	if (!error)
		eth_users--;
	return(error);
}

int
eth_add_status(struct ndd *nddp,
	       struct ns_com_status *filter,
	       int len,
	       struct ns_statuser *ns_statuser)
{
	int	error;

	error = dmx_add_status(nddp, filter, ns_statuser);
	if (!error)
		((*(nddp->ndd_ctl)) (nddp, NDD_ADD_STATUS, (caddr_t)filter, len));

	if (!error)
		eth_users++;
	return(error);
}

int
eth_del_status(struct ndd *nddp,
	       struct ns_com_status *filter,
	       int len)
{
	int	error;

	error = dmx_del_status(nddp, filter);

	if (!error)
		((*(nddp->ndd_ctl)) (nddp, NDD_DEL_STATUS, (caddr_t)filter, len));

	if (!error)
		eth_users--;
	return(error);
}

struct eth_user	*
eth_get_user(u_short etype,
	     struct ndd *nddp)
{
	struct eth_user		*filter_head;
	struct eth_user		*fup;
	struct eth_dmx_ctl	*eth_dmx_ctl;

	eth_dmx_ctl = (struct eth_dmx_ctl *) nddp->ndd_specdemux;
	filter_head = &eth_dmx_ctl->hash_heads[etype % ETHDMX_HASH];
	for ( fup = filter_head->next; fup != filter_head; fup = fup->next) {
		if (fup->filter.ethertype == etype)
			return(fup);
	}
	return(KERN_SUCCESS);
}

int
eth_add_ethertype(struct ndd *nddp,
		  struct ns_8022 *filter,
		  struct ns_user *ns_user)
{
	int			x;
	struct eth_user		*new_user;
	struct eth_dmx_ctl	*eth_dmx_ctl;

	if (!(new_user = (struct eth_user *)kget(sizeof(struct eth_user))))
		return(CDLI_ERR_NOMEM);
	
	bzero((char *)new_user, sizeof(struct eth_user));
	new_user->user = *ns_user;
	new_user->filter = *filter;

	x = splimp();
	if (eth_get_user(filter->ethertype, nddp)) {
		splx(x);
		kfree((vm_offset_t)new_user, sizeof(struct eth_user));
		return(CDLI_ERR_FILTER_EXISTS);
	}
	eth_dmx_ctl = (struct eth_dmx_ctl *) nddp->ndd_specdemux;
	insque((queue_entry_t)new_user, 
	       (queue_entry_t)eth_dmx_ctl->hash_heads[filter->ethertype % ETHDMX_HASH].prev);
	splx(x);

#ifdef CDLI_DEBUG
	printf("added ethernet entry type %x for interface %s\n", 
		filter->ethertype, nddp->ndd_name);
#endif

	return(KERN_SUCCESS);
}

int
eth_del_ethertype(struct ndd *nddp,
		  struct ns_8022 *filter)
{
	int			x;
	struct eth_user		*fup;

	x = splimp();
	fup = eth_get_user(filter->ethertype, nddp);
	if (fup == NULL) {
		splx(x);
		return(CDLI_ERR_FILTER_DOESNT_EXIST);
	}
	remque((queue_entry_t)fup);
	splx(x);
	kfree((vm_offset_t)fup, (vm_size_t)sizeof(struct eth_user));

#ifdef CDLI_DEBUG
	printf("deleted entry type %x for interface %s\n", 
		filter->ethertype, nddp->ndd_name);
#endif
	return(KERN_SUCCESS);
}

int
eth_add_tap(struct ndd *nddp,
	    struct ns_user *ns_user)
{
	struct eth_dmx_ctl	*eth_dmx_ctl;

	eth_dmx_ctl = (struct eth_dmx_ctl *) nddp->ndd_specdemux;
	if (eth_dmx_ctl->tap_user.isr != NULL)
		return(CDLI_ERR_ISR_EXISTS);

	eth_dmx_ctl->tap_user = *ns_user;	
	return(KERN_SUCCESS);
}

int
eth_del_tap(struct ndd *nddp)
{
	struct eth_dmx_ctl	*eth_dmx_ctl;

	eth_dmx_ctl = (struct eth_dmx_ctl *) nddp->ndd_specdemux;
	if (eth_dmx_ctl->tap_user.isr == NULL)
		return(CDLI_ERR_ISR_DOESNT_EXIST);

	bzero((char *)&eth_dmx_ctl->tap_user, sizeof(eth_dmx_ctl->tap_user));
	return(KERN_SUCCESS);
}

void
eth_receive(struct ndd *nddp, caddr_t m)
{
/* Currently the CDLI ifnet driver goes directly to and from the
   hardware without running through these filters.  If there isn't
   more than one driver, there isn't much of a need to run data
   through the filters.  This code is not tied in to the main
   CDLI code, nor the existing 386 enet ifnet driver. */
#if 0 /* FIXME - whose filter to use? */
	struct mbuf		*mnextpkt;
	struct ether_header	*eh;
	struct eth_dmx_ctl	*eth_dmx_ctl;

	if (nddp->ndd_specdemux == (caddr_t)NULL) {
		while ( m != (struct mbuf *) NULL ) {
			mnextpkt = m->m_nextpkt;
			m->m_nextpkt = (struct mbuf *) NULL;
			m_freem(m);
			m = mnextpkt;
		}	
		return;	
	}
	eth_dmx_ctl = (struct eth_dmx_ctl *)nddp->ndd_specdemux;
	while ( m != (struct mbuf *) NULL ) {
		mnextpkt = m->m_nextpkt;
		m->m_nextpkt = (struct mbuf *) NULL;
		eh = mtod(m, struct ether_header *);
		if (eh->ether_dhost[0] & 1) {
			if (bcmp((caddr_t)ebcastaddr, (caddr_t)eh->ether_dhost,
			     sizeof(ebcastaddr)) == 0) {
				m->m_flags |= M_BCAST;
				nddp->ndd_demuxer->nd_dmxstats.nd_bcast++;
			} else {
				nddp->ndd_demuxer->nd_dmxstats.nd_mcast++;
				m->m_flags |= M_MCAST;
			}
		}
		if (eth_dmx_ctl->tap_user.isr != NULL)
			(*(eth_dmx_ctl->tap_user.isr))
				  (nddp, m, eh, eth_dmx_ctl->tap_user.isr_data);

		if (nddp->ndd_flags & NDD_PROMISC) {
		     if  ((m->m_flags & (M_BCAST|M_MCAST)) == 0) {
			if (bcmp((caddr_t)nddp->ndd_physaddr, 
			      (caddr_t)eh->ether_dhost, nddp->ndd_addrlen)) {
				m_freem(m);
				m = mnextpkt;
				continue;
			}
		     }
		}
		if (eh->ether_type > ETHERMTU)
			eth_std_receive(nddp, m, eh);
		else
		{
			printf("unknown ethernet packet type %d\n", eh->ether_type);
			m_freem(m);
/*			dmx_8022_receive(nddp, m, sizeof(struct ie3_mac_hdr));*/
		}

		m = mnextpkt;
	}
	return;
#endif /* #if 0 */
	return;
}

int
eth_std_receive(struct ndd *nddp,
		caddr_t m,
		struct ether_header *eh)
{
#if 0	/* FIXME -- whose receive to use */
	struct eth_user		*fup;
	struct eth_user		*filter_head;
	struct eth_dmx_ctl	*eth_dmx_ctl;

	eth_dmx_ctl = (struct eth_dmx_ctl *) nddp->ndd_specdemux;
	filter_head = &eth_dmx_ctl->hash_heads[eh->ether_type % ETHDMX_HASH];
	
	for ( fup = filter_head->next; fup != filter_head; fup = fup->next) {
		if (fup->filter.ethertype == eh->ether_type) {
			if (fup->user.pkt_format != NS_INCLUDE_MAC) {
				m->m_pkthdr.len -= sizeof(struct ether_header);
				m->m_len -= sizeof(struct ether_header);
				m->m_data += sizeof(struct ether_header);
			}
			nddp->nd_receive(m, eh); /* FIXME -- Right arguments */
/*			IFSTUFF_AND_DELIVER(fup, nddp, m, eh);*/
			return;
		}
	}
	
	nddp->ndd_demuxer->nd_dmxstats.nd_nofilter++;
	m_freem(m);
	return;
#endif
	return(KERN_SUCCESS);
}

void
eth_response(struct ndd *nddp,
	     caddr_t m,
	     int llcoffset)
{
#if 0 /* FIXME Whose response code to use */
	struct ie3_mac_hdr		*ie3macp;
	char				tmp[6];

	ie3macp = mtod(m, struct ie3_mac_hdr *);
	bcopy(ie3macp->ie3_mac_dst, tmp, 6);
	bcopy(ie3macp->ie3_mac_src, ie3macp->ie3_mac_dst, 6);
	bcopy(tmp, ie3macp->ie3_mac_src, 6);
#endif
	(*(nddp->ndd_output))(nddp, m);
	return;
}

int
eth_dmx_init(struct ndd *nddp) 
{
	struct eth_dmx_ctl	*eth_dmx_ctl;
	int			i;

	if (!(eth_dmx_ctl = (struct eth_dmx_ctl *)kget((vm_size_t)sizeof(struct eth_dmx_ctl))))
		return(CDLI_ERR_NOMEM);
	nddp->ndd_specdemux = (caddr_t) eth_dmx_ctl;
	for (i = 0 ; i < ETHDMX_HASH ; i++) {
		eth_dmx_ctl->hash_heads[i].next = 
			&eth_dmx_ctl->hash_heads[i];
		eth_dmx_ctl->hash_heads[i].prev = 
			&eth_dmx_ctl->hash_heads[i];
	}
	return(KERN_SUCCESS);
}

int
eth_add_demuxer(int ndd_type)
{
	demuxer.nd_add_filter = (int (*)(struct ndd *, caddr_t, int, struct ns_user *))eth_add_filter;
	demuxer.nd_del_filter = (int (*)(struct ndd *, caddr_t, int))eth_del_filter;
	demuxer.nd_add_status = (int (*)(struct ndd *, caddr_t, int, struct ns_statuser *))eth_add_status;
	demuxer.nd_del_status = (int (*)(struct ndd *, caddr_t, int))eth_del_status;
	demuxer.nd_receive = (void (*)(struct ndd *, struct mbuf *))eth_receive;
	demuxer.nd_status = 0;
	demuxer.nd_response = (void (*)(struct ndd *, struct mbuf *, int))eth_response;
	demuxer.nd_use_nsdmx = 1;
	demuxer8023 = demuxer;
	ns_add_demux(NDD_ISO88023, &demuxer8023);
	return(ns_add_demux(NDD_ETHER, &demuxer));
}
