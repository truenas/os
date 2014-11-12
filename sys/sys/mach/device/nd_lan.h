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

#ifndef _NET_ND_LAN_H_
#define _NET_ND_LAN_H_

#ifndef _802_2_LLC
#define	_802_2_LLC
/*
 * 802.2 LLC header
 */
struct ie2_llc_hdr {
	unsigned char	dsap;		/* DSAP				*/
	unsigned char	ssap;		/* SSAP				*/
	unsigned char	ctrl;		/* control field		*/
};

struct ie2_llc_snaphdr {
	unsigned char	dsap;		/* DSAP				*/
	unsigned char	ssap;		/* SSAP				*/
	unsigned char	ctrl;		/* control field		*/
	unsigned char	prot_id[3];	/* protocol id			*/
	unsigned short	type;		/* type field			*/
};

struct ie2_xid_ifield {
	unsigned char	dsap;		/* DSAP				*/
	unsigned char	ssap;		/* SSAP				*/
	unsigned char	ctrl;		/* control field		*/
	unsigned char	xid_info[3];	/* XID information field	*/
};
#endif

/* MOST OF THE FOLLOWING IS UNUSED IN OSF 1.3 */

struct config_proto {
	int			(*loop)(void);	/* protocol loopback function	*/
        struct ifqueue 		*loopq;		/* input queue, may be NULL	*/
	u_short			netisr;		/* isr number for schednetisr	*/
	int			(*resolve)(void); /* address resolution 	*/
	int			(*ioctl)(void);	/* address resolution ioctl */
	int			(*whohas)(void); /* address resolution whohas */
};

#define	DSAP_INET	0xaa		/* SNAP SSAP			*/
#define	SSAP_INET	0xaa		/* SNAP DSAP			*/
#define	SSAP_RESP	0x01		/* SSAP response bit		*/
#define DSAP_NETWARE_802_5 0xe0 	/* NETWARE APPLETALK */
#define SSAP_NETWARE_802_5 0xe0		

#define	CTRL_UI		0x03		/* unnumbered info		*/
#define CTRL_XID	0xaf		/* eXchange IDentifier		*/
#define	CTRL_TEST	0xe3		/* test frame			*/

#define	NS_MAX_SAPS	256

struct ns_8022_user {
	struct ns_8022_user	*next;
	struct ns_8022_user	*prev;
	struct ns_user 		user;
	struct ns_8022 		filter;
};

struct ns_8022_user_head {
	struct ns_8022_user	*next;
	struct ns_8022_user	*prev;
};

struct com_status_user {
	struct com_status_user	*next;
	struct com_status_user	*prev;
	struct ns_statuser 	user;
	struct ns_com_status	filter;
};

struct ns_dmx_ctl {
	struct ns_8022_user_head	dsap_heads[NS_MAX_SAPS];
	struct com_status_user		com_status_head;	
};


int nd_bcmp(register unsigned char *, register unsigned char *, register int);

#define	DMX_8022_GET_USER(retfup, sap, etype, org, nsdemux) { 		\
	struct ns_8022_user	*fup;					\
	struct ns_8022_user	*filter_head;				\
									\
	filter_head = (struct ns_8022_user *) &((nsdemux)->dsap_heads[sap]);\
	fup = filter_head->next;					\
	if (fup->filter.filtertype != NS_8022_LLC_DSAP) { 		\
		for ( ; fup != filter_head; fup = fup->next) {		\
			if ( (fup->filter.ethertype == etype) &&	\
			     (!nd_bcmp(fup->filter.orgcode, org, sizeof(org))) )\
				break;					\
		}							\
	} 								\
	if (fup == filter_head)						\
		retfup = NULL;						\
	else								\
		retfup = fup;						\
}


#define IFSTUFF_AND_DELIVER(nsuser, nddp, m, macp) {			\
	int	x;							\
									\
	if (nsuser->user.ifp) {						\
		if ((nsuser->user.ifp->if_flags & IFF_UP) == 0) {	\
			m_freem(m);					\
			nsuser->user.ifp->if_rcvdrops++;			\
			goto out;					\
		} else {						\
			nsuser->user.ifp->if_ipackets++;		\
			/*nsuser->user.ifp->if_ibytes += m->m_pkthdr.len;*/	\
			m->m_pkthdr.rcvif = nsuser->user.ifp;		\
		}							\
	}								\
	if (nsuser->user.protoq == NULL)				\
		(*(nsuser->user.isr))(nddp, m, macp, nsuser->user.isr_data);\
	else {								\
		x = splimp();						\
		/* if not full, enqueue and schednetisr	*/		\
		if (IF_QFULL(nsuser->user.protoq)) {			\
			IF_DROP(nsuser->user.protoq);			\
			splx(x);					\
			m_freem(m);					\
		} else {						\
			IF_ENQUEUE(nsuser->user.protoq, m);		\
			splx(x);					\
			schednetisr(nsuser->user.netisr);		\
		}							\
	}								\
out:	;								\
}

#define DELIVER_PACKET(nsuser, nddp, m, macp) {				\
	int	x;							\
									\
	if (nsuser->user.protoq == NULL)				\
		(*(nsuser->user.isr))(nddp, m, macp, nsuser->user.isr_data);\
	else {								\
		x = splimp();						\
		/* if not full, enqueue and schednetisr	*/		\
		if (IF_QFULL(nsuser->user.protoq)) {			\
			IF_DROP(nsuser->user.protoq);			\
			splx(x);					\
			m_freem(m);					\
		} else {						\
			IF_ENQUEUE(nsuser->user.protoq, m);		\
			splx(x);					\
			schednetisr(nsuser->user.netisr);		\
		}							\
	}								\
}

struct af_ent {
	struct config_proto config;
	u_short sap;
	u_short type;
};
extern struct af_ent	af_table[];

#endif
