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
/* CMU_HIST */
/*
 * Revision 2.20.3.3  92/03/28  10:04:52  jeffreyh
 * 	Reduced net_queue_free_min to 10 in the NORMA case.
 * 	[92/03/17            jeffreyh]
 *
 * Revision 2.20.3.2  92/03/03  16:14:04  jeffreyh
 * 	Raised net_queue_free_min on NORMA_IPC. Starting with the Norma branch
 * 	lost packets have been seen. This should be looked at to see if it
 * 	is machine dependent, NORMA dependent, or just a bug.
 * 	[92/02/27            jeffreyh]
 * 	Pick up changes from MK68
 * 	[92/02/26  11:03:30  jeffreyh]
 *
 * Revision 2.21  92/01/03  20:03:57  dbg
 * 	Add: NETF_PUSHHDR, NETF_PUSHSTK, NETF_PUSHIND, NETF_PUSHHDRIND.
 * 	[91/12/23            dbg]
 *
 * Revision 2.20.3.1  92/01/21  21:49:55  jsb
 * 	Changed parameters to netipc_net_packet.
 * 	[92/01/17  18:32:12  jsb]
 *
 * Revision 2.20  91/08/28  11:11:28  jsb
 * 	Panic if network write attempted with continuation.
 * 	[91/08/12  17:29:53  dlb]
 *
 * Revision 2.19  91/08/24  11:55:55  af
 * 	Missing include for Spls definitions.
 * 	[91/08/02  02:45:16  af]
 *
 * Revision 2.18  91/08/03  18:17:43  jsb
 * 	Added NORMA_ETHER support.
 * 	[91/07/24  22:54:41  jsb]
 *
 * Revision 2.17  91/05/14  15:59:34  mrt
 * 	Correcting copyright
 *
 * Revision 2.16  91/05/10  11:48:47  dbg
 * 	Don't forget to copy the packet size when duplicating a packet
 * 	for multiple filters in net_filter().
 * 	[91/05/09            dpj]
 *
 * Revision 2.15  91/03/16  14:43:14  rpd
 * 	Added net_thread, net_thread_continue.
 * 	[91/02/13            rpd]
 * 	Split net_rcv_msg_queue into high and low priority queues.
 * 	Cap the total number of buffers allocated.
 * 	[91/01/14            rpd]
 *
 * 	Added net_rcv_msg_queue_size, net_rcv_msg_queue_max.
 * 	[91/01/12            rpd]
 *
 * Revision 2.14  91/02/14  14:37:07  mrt
 * 	Added garbage collection of dead filters.
 * 	[91/02/12  12:11:10  af]
 *
 * Revision 2.13  91/02/05  17:09:54  mrt
 * 	Changed to new Mach copyright
 * 	[91/01/31  17:30:04  mrt]
 *
 * Revision 2.12  91/01/08  15:09:48  rpd
 * 	Replaced NET_KMSG_GET, NET_KMSG_FREE
 * 	with net_kmsg_get, net_kmsg_put, net_kmsg_collect.
 * 	Increased net_kmsg_ilist_min to 4.
 * 	[91/01/05            rpd]
 * 	Fixed net_rcv_msg_thread to round message sizes up to an int multiple.
 * 	[90/12/07            rpd]
 *
 * 	Fixed net_rcv_msg_thread to not set vm_privilege.
 * 	[90/11/29            rpd]
 *
 * Revision 2.11  90/09/09  23:20:00  rpd
 * 	Zero the mapped_size stats for non mappable interfaces.
 * 	[90/08/30  17:41:00  af]
 *
 * Revision 2.10  90/08/27  21:55:18  dbg
 * 	If multiple filters receive a packet, copy the header as well as
 * 	the body.  Fix from Dan Julin.
 * 	[90/08/27            dbg]
 *
 * 	Fix filter check to account for literal word.
 * 	[90/07/17            dbg]
 *
 * Revision 2.9  90/08/06  15:06:57  rwd
 * 	Fixed a bug in parse_net_filter(), that was reading the
 * 	litteral from NETF_PUSHLIT as an instruction.
 * 	[90/07/18  21:56:20  dpj]
 *
 * Revision 2.8  90/06/02  14:48:14  rpd
 * 	Converted to new IPC.
 * 	[90/03/26  21:57:43  rpd]
 *
 * Revision 2.7  90/02/22  20:02:21  dbg
 * 	Track changes to kmsg structure.
 * 	[90/01/31            dbg]
 *
 * Revision 2.6  90/01/11  11:42:20  dbg
 * 	Make run in parallel.
 * 	[89/12/15            dbg]
 *
 * Revision 2.5  89/12/08  19:52:22  rwd
 * 	Picked up changes from rfr to minimize wired down memory
 * 	[89/11/21            rwd]
 *
 * Revision 2.4  89/09/08  11:24:35  dbg
 * 	Convert to run in kernel task.  Removed some lint.
 * 	[89/07/26            dbg]
 *
 * Revision 2.3  89/08/11  17:55:18  rwd
 * 	Picked up change from rfr which made zone collectable and
 * 	decreased min net_kmesg to 2.
 * 	[89/08/10            rwd]
 *
 * Revision 2.2  89/08/05  16:06:58  rwd
 * 	Changed device_map to device_task_map
 * 	[89/08/04            rwd]
 *
 * 13-Mar-89  David Golub (dbg) at Carnegie-Mellon University
 *	Created.
 *
 */
/* CMU_ENDHIST */
/*
 * Mach Operating System
 * Copyright (c) 1991,1990,1989 Carnegie Mellon University
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie Mellon
 * the rights to redistribute these changes.
 */
/*
 */
/*
 *	Author: David B. Golub, Carnegie Mellon University
 *	Date: 	3/98
 *
 *	Packet filter code taken from vaxif/enet.c written
 *		CMU and Stanford.
 */

#include <mach_kdb.h>
#include <norma_ether.h>
#include <dipc.h>
#include <mach_ldebug.h>
#include <cpus.h>

#include <device/net_status.h>
#include <machine/machparam.h>		/* spl definitions */
#include <device/net_io.h>
#include <device/if_hdr.h>
#include <device/io_req.h>
#include <device/ds_routines.h>
#include <device/device_port.h>

#include <mach/boolean.h>
#include <mach/vm_param.h>
#include <mach/mach_types.h>
#include <mach/mach_host_server.h>

#include <ipc/ipc_port.h>
#include <ipc/ipc_kmsg.h>
#include <ipc/ipc_mqueue.h>
#include <ipc/ipc_print.h>
#include <ipc/ipc_table.h>

#include <kern/counters.h>
#include <kern/kalloc.h>
#include <kern/lock.h>
#include <kern/processor.h>
#include <kern/queue.h>
#include <kern/sched_prim.h>
#include <kern/thread.h>
#include <kern/thread_swap.h>
#include <kern/spl.h>
#include <kern/misc_protos.h>

#include <vm/vm_map.h>
#include <vm/vm_kern.h>
#include <vm/vm_user.h>
#include <vm/pmap.h>

#include <machine/endian.h>

#if	NORMA_ETHER
#include <norma/ipc_ether.h>
#endif	/*NORMA_ETHER*/
#include <fddi.h>

/*
 * Network IO.
 */

/*
 * Locking:
 *
 * Each net_pool structure has a set of locks covering some elements in that
 * structure. This has been inherited from the original code.
 *
 * net_pool_lock - protects the net_pool?* fields. Held at spllo().
 *  This lock is currently not necessary since we have no manipulation
 *  of data it protects after pool creation, but is included for future
 *  interface expansion.
 *
 * net_queue_lock - protects the net_queue?* fields. Held at splimp().
 * net_queue_free_lock - protects the net_queue_free?* fields. Held at splimp().
 * net_kmsg_total_lock - protects the net_kmsg variables. Held at spllo().
 *
 * There are two global locks.
 *
 * The net_thread_lock is used to exclude the net_thread and to check/set the
 * wakeup state for that thread.  Held at splimp().
 *
 * The net_pool_lock protects additions to the net_pool_list (list of all pools).
 * Held at spllo().
 *
 */

/*
 * Each interface has a write port and a set of read ports.
 * Each read port has one or more filters to determine what packets
 * should go to that port.
 */

/*
 * Receive port for net, with packet filter.
 */
struct net_rcv_port {
	queue_chain_t	chain;		/* list of open_descriptors */
	net_pool_t	pool;		/* pool attached */
	ipc_port_t	rcv_port;	/* port to send packet to */
	mach_device_t	device;		/* device attached */
	mach_port_msgcount_t rcv_qlimit;/* port's qlimit */
	mach_port_msgcount_t rcv_count;	/* number of packets received */
	int		priority;	/* priority for filter */
	filter_t	*filter_end;	/* pointer to end of filter */
	filter_t	filter[NET_MAX_FILTER];
					/* filter operations */
	filter_fct_t	fpstart;	/* machine-dependent filter */
	unsigned int	fplen;		/* machine-dependent filter length */
};
typedef struct net_rcv_port *net_rcv_port_t;

/*
 * List of all interfaces owning packet filters.
 */
queue_head_t 	net_rcv_list;
decl_mutex_data(,net_rcv_list_lock)

int		net_thread_awaken = 0;		/* for debugging */
int		net_ast_taken = 0;		/* for debugging */

/*
 * List of net kmsgs queued to be sent to users.
 * Messages can be high priority or low priority.
 * The network thread processes high priority messages first.
 */
decl_simple_lock_data(,net_thread_lock)
boolean_t	net_thread_awake = FALSE;
boolean_t	net_thread_running = FALSE;

/*
 * Pool list.
 */
decl_simple_lock_data(,net_pool_lock)
net_pool_t 	net_pool_list;

/*
 * Statically defined pool for compatibility with old interface.
 */
net_pool_t inline_pagepool;

/*
 * This value is critical to network performance.
 * At least this many buffers should be sitting in net_queue_free.
 * If this is set too small, we will drop network packets.
 * Even a low drop rate (<1%) can cause severe network throughput problems.
 * We add one to net_queue_free_min for every filter.
 */
#if	SYMMETRY
 /*
 * Sequent needs at least 17 of them. The code shouild be rearranged to
 * remove machine dependencies from here !
 */
int		net_inline_free_min = 40;
#elif NFDDI > 0
int		net_inline_free_min = 16;
#else 	/* SYMMETRY */
int		net_inline_free_min = 3;
#endif 	/* SYMMETRY */

vm_size_t	net_kmsg_size;
zone_t		net_rcv_zone;

/* Forward */

boolean_t	net_deliver(
			net_pool_t		p,
			boolean_t		nonblocking);
int		net_multicast_hash(
			unsigned char		*addr,
			unsigned		len);
void		reorder_queue(
			queue_t			first,
			queue_t			last);
boolean_t	net_do_filter(
			net_rcv_port_t		infp,
			char			*data,
			mach_msg_type_number_t	dcount,
			char 			*hdr);
boolean_t	parse_net_filter(
			filter_t		*filter,
			mach_msg_type_number_t	count);

void		net_filter_inline(
			net_pool_t		p,
			ipc_kmsg_t		kmsg,
			ipc_kmsg_queue_t	send_list);

void		net_package_inline(
			ipc_kmsg_t		kmsg);

void		net_recv_port_print(
			struct ifnet		*ifp);

void		net_pool_print(void);

/*
 *	We want more buffers when there aren't enough in the free queue
 *	and the low priority queue.  However, we don't want to allocate
 *	more than net_kmsg_max.
 */

#define net_kmsg_want_more(p)		\
	((((p)->net_queue_free_size + (p)->net_queue_low_size) \
	  < (p)->net_queue_free_min) && \
	 ((p)->net_kmsg_total < (p)->net_kmsg_max))

/*
 * Collect free queue pages from all net pools.
 */
void
net_kmsg_collect(void)
{
	register net_pool_t listp, p;

	/*
	 * Get current list start.
	 */

	simple_lock(&net_pool_lock);
	listp = net_pool_list;
	simple_unlock(&net_pool_lock);

	for (p = listp; p != NET_POOL_NULL; p = p->net_pool_next) {
		net_kmsg_collect_pool(p);
	}
}

void
net_kmsg_more(
	net_pool_t	p)
{
	register ipc_kmsg_t kmsg;
	spl_t s;
	net_callback_t callback;

	/*
	 * Replenish net kmsg pool if low.  We don't have the locks
	 * necessary to look at these variables, but that's OK because
	 * misread values aren't critical.  The danger in this code is
	 * that while we allocate buffers, interrupts are happening
	 * which take buffers out of the free list.  If we are not
	 * careful, we will sit in the loop and allocate a zillion
	 * buffers while a burst of packets arrives.  So we count
	 * buffers in the low priority queue as available, because
	 * net_kmsg_get will make use of them, and we cap the total
	 * number of buffers we are willing to allocate.
	 */

	for (;;) {
	    if (net_kmsg_want_more(p) &&
		(kmsg = net_kmsg_alloc_buf(p)) != IKM_NULL)
		net_kmsg_put_buf(p, kmsg);
	    else {
		s = splimp();
		simple_lock(&p->net_queue_free_lock);
		if (queue_empty(&p->net_queue_free_callback) ||
		    ipc_kmsg_queue_first(&p->net_queue_free) == IKM_NULL) {
		    simple_unlock(&p->net_queue_free_lock);
		    splx(s);
		    break;
		}
		queue_remove_first(&p->net_queue_free_callback,
				   callback, net_callback_t, ncb_chain);
		simple_unlock(&p->net_queue_free_lock);
		splx(s);
		(*callback->ncb_fct)(callback->ncb_arg);
	    }
	}
}

/*
 *	ethernet_priority:
 *
 *	This function properly belongs in the ethernet interfaces;
 *	it should not be called by this module.  (We get packet
 *	priorities as an argument to net_filter.)  It is here
 *	to avoid massive code duplication.
 *
 *	Returns TRUE for high-priority packets.
 */

boolean_t
ethernet_priority(
	ipc_kmsg_t kmsg)
{
	register unsigned char *addr =
		(unsigned char *) net_kmsg(kmsg)->header;

	/*
	 *	A simplistic check for broadcast packets.
	 */

	if ((addr[0] == 0xff) && (addr[1] == 0xff) &&
	    (addr[2] == 0xff) && (addr[3] == 0xff) &&
	    (addr[4] == 0xff) && (addr[5] == 0xff))
	    return FALSE;
	else
	    return TRUE;
}

boolean_t
net_do_filter(
	net_rcv_port_t		infp,
	char *			data,
	mach_msg_type_number_t	data_count,
	char *			header)
{
	int		stack[NET_FILTER_STACK_DEPTH+1];
	register int	*sp;
	register filter_t	*fp, *fpe;
	register unsigned int	op, arg;

	/*
	 * The filter accesses the header and data
	 * as unsigned short words.
	 */
	data_count /= sizeof(unsigned short);

#define	data_word	((unsigned short *)data)
#define	header_word	((unsigned short *)header)

	sp = &stack[NET_FILTER_STACK_DEPTH];
	fp = &infp->filter[0];
	fpe = infp->filter_end;

	*sp = TRUE;

	while (fp < fpe) {
	    arg = *fp++;
	    op = NETF_OP(arg);
	    arg = NETF_ARG(arg);

	    switch (arg) {
		case NETF_NOPUSH:
		    arg = *sp++;
		    break;
		case NETF_PUSHZERO:
		    arg = 0;
		    break;
		case NETF_PUSHLIT:
		    arg = *fp++;
		    break;
		case NETF_PUSHIND:
		    arg = *sp++;
		    if (arg >= data_count)
			return FALSE;
		    arg = data_word[arg];
		    break;
		case NETF_PUSHHDRIND:
		    arg = *sp++;
		    if (arg >= NET_HDW_HDR_MAX/sizeof(unsigned short))
			return FALSE;
		    arg = header_word[arg];
		    break;
		default:
		    if (arg >= NETF_PUSHSTK) {
			arg = sp[arg - NETF_PUSHSTK];
		    }
		    else if (arg >= NETF_PUSHHDR) {
			arg = header_word[arg - NETF_PUSHHDR];
		    }
		    else {
			arg -= NETF_PUSHWORD;
			if (arg >= data_count)
			    return FALSE;
			arg = data_word[arg];
		    }
		    break;

	    }
	    switch (op) {
		case NETF_OP(NETF_NOP):
		    *--sp = arg;
		    break;
		case NETF_OP(NETF_AND):
		    *sp &= arg;
		    break;
		case NETF_OP(NETF_OR):
		    *sp |= arg;
		    break;
		case NETF_OP(NETF_XOR):
		    *sp ^= arg;
		    break;
		case NETF_OP(NETF_EQ):
		    *sp = (*sp == arg);
		    break;
		case NETF_OP(NETF_NEQ):
		    *sp = (*sp != arg);
		    break;
		case NETF_OP(NETF_LT):
		    *sp = (*sp < arg);
		    break;
		case NETF_OP(NETF_LE):
		    *sp = (*sp <= arg);
		    break;
		case NETF_OP(NETF_GT):
		    *sp = (*sp > arg);
		    break;
		case NETF_OP(NETF_GE):
		    *sp = (*sp >= arg);
		    break;
		case NETF_OP(NETF_COR):
		    if (*sp++ == arg)
			return (TRUE);
		    break;
		case NETF_OP(NETF_CAND):
		    if (*sp++ != arg)
			return (FALSE);
		    break;
		case NETF_OP(NETF_CNOR):
		    if (*sp++ == arg)
			return (FALSE);
		    break;
		case NETF_OP(NETF_CNAND):
		    if (*sp++ != arg)
			return (TRUE);
		    break;
		case NETF_OP(NETF_LSH):
		    *sp = (*sp << arg) & 0xFFFF;
		    break;
		case NETF_OP(NETF_RSH):
		    *sp >>= arg;
		    break;
		case NETF_OP(NETF_ADD):
		    *sp = (*sp + arg) & 0xFFFF;
		    break;
		case NETF_OP(NETF_SUB):
		    *sp = (*sp - arg) & 0xFFFF;
		    break;
	    }
	}
	return ((*sp) ? TRUE : FALSE);

#undef	data_word
#undef	header_word

}

void
reorder_queue(
	register queue_t	first,
	register queue_t	last)
{
	register queue_entry_t	prev, next;

	prev = first->prev;
	next = last->next;

	prev->next = last;
	next->prev = first;

	last->prev = prev;
	last->next = first;

	first->next = next;
	first->prev = last;
}

/*
 * Run a packet through the filters, returning a list of messages.
 * We are *not* called at interrupt level.
 */
void
net_filter_inline(
	register net_pool_t	p,
	register ipc_kmsg_t	kmsg,
	ipc_kmsg_queue_t	send_list)
{
	register struct ifnet	*ifp;
	register net_rcv_port_t	infp, nextfp;
	register ipc_kmsg_t	new_kmsg;
	register mach_device_t	device;
	mach_msg_type_number_t count = net_kmsg(kmsg)->net_rcv_msg_packet_count;

	ifp = (struct ifnet *) kmsg->ikm_header.msgh_remote_port;
	device = (mach_device_t) kmsg->ikm_header.msgh_local_port;
	ipc_kmsg_queue_init(send_list);

	/*
	 * Unfortunately we can't allocate or deallocate memory
	 * while holding this lock.  And we can't drop the lock
	 * while examining the filter list.
	 */
	mutex_lock(&ifp->if_rcv_port_list_lock);
	for (infp = (net_rcv_port_t) queue_first(&ifp->if_rcv_port_list);
	     !queue_end(&ifp->if_rcv_port_list, (queue_entry_t)infp);
	     infp = nextfp) {
	    nextfp = (net_rcv_port_t) queue_next(&infp->chain);

	    if ((kmsg->ikm_header.msgh_id != NET_RCV_MSG_ID ||
		 (infp->device->flag & D_CLONED) == 0 ||
		 infp->device != device) &&
		(infp->filter == infp->filter_end ||
		 (infp->fpstart ?
		  (*infp->fpstart)(
			&net_kmsg(kmsg)->packet[sizeof(struct packet_header)],
			count, net_kmsg(kmsg)->header) :
		  net_do_filter(infp,
			&net_kmsg(kmsg)->packet[sizeof(struct packet_header)],
			count, net_kmsg(kmsg)->header)))) {
		register ipc_port_t dest;

		/*
		 * Make a send right for the destination.
		 */

		dest = ipc_port_copy_send(infp->rcv_port);
		if (!IP_VALID(dest)) {
		    /*
		     * This filter is dead. This filter *can't* be
		     * deallocated here because net_unset_filter()
		     * (which destroys properly packet-filters) will
		     * be called later without any port lock held.
		     */
		    continue;
		}

		/*
		 * Deliver copy of packet to this channel.
		 */
		if (ipc_kmsg_queue_empty(send_list)) {
		    /*
		     * Only receiver, so far
		     */
		    new_kmsg = kmsg;
		    new_kmsg->ikm_header.msgh_local_port = MACH_PORT_NULL;
		} else {
		    /*
		     * Other receivers - must allocate message and copy.
		     */
		    new_kmsg = net_kmsg_get_buf(p);
		    if (new_kmsg == IKM_NULL) {
			ipc_port_release_send(dest);
			break;
		    }

		    bcopy(
			net_kmsg(kmsg)->packet,
			net_kmsg(new_kmsg)->packet,
			count);
		    bcopy(
			net_kmsg(kmsg)->header,
			net_kmsg(new_kmsg)->header,
			NET_HDW_HDR_MAX);
		    net_kmsg(new_kmsg)->net_rcv_msg_packet_count = count;
		    new_kmsg->ikm_header.msgh_id = kmsg->ikm_header.msgh_id;
		}

		new_kmsg->ikm_header.msgh_remote_port = (mach_port_t) dest;
		if (kmsg->ikm_header.msgh_id == PROMISC_RCV_MSG_ID &&
		    infp->device != device)
		    new_kmsg->ikm_header.msgh_id = NET_RCV_MSG_ID;
		ipc_kmsg_enqueue(send_list, new_kmsg);

		{
		register net_rcv_port_t prevfp;
		mach_port_msgcount_t rcount = ++infp->rcv_count;

		/*
		 * See if ordering of filters is wrong
		 */
		 if (infp->priority >= NET_HI_PRI) {
		    prevfp = (net_rcv_port_t) queue_prev(&infp->chain);
		    /*
		     * If infp is not the first element on the queue,
		     * and the previous element is at equal priority
		     * but has a lower count, then promote infp to
		     * be in front of prevfp.
		     */
		    if ((queue_t)prevfp != &ifp->if_rcv_port_list &&
			infp->priority == prevfp->priority) {
			/*
			 * Threshold difference to prevent thrashing
			 */
			if (100 + prevfp->rcv_count < rcount)
			    reorder_queue(&prevfp->chain, &infp->chain);
		    }
		    /*
		     * High-priority filter -> no more deliveries
		     */
		    break;
		 }
	       }
	    }
	}
	mutex_unlock(&ifp->if_rcv_port_list_lock);

	if (ipc_kmsg_queue_empty(send_list)) {
	    /* Not sent - recycle */
	    net_kmsg_put_buf(p, kmsg);
	}

	if (device) {
	    mach_device_deallocate(device);
	}
}

void
net_package_inline(
	ipc_kmsg_t kmsg)
{
	int count;
	mach_msg_format_0_trailer_t *trailer;

	/*
	 * Fill in the rest of the kmsg.
	 */
	count = net_kmsg(kmsg)->net_rcv_msg_packet_count;

	ikm_init_special(kmsg, IKM_SIZE_NETWORK);

	kmsg->ikm_header.msgh_bits =
		MACH_MSGH_BITS(MACH_MSG_TYPE_PORT_SEND, 0);
	/* remember message sizes must be rounded up */
	kmsg->ikm_header.msgh_size = round_msg(sizeof(struct net_rcv_msg)
					- sizeof(mach_msg_format_0_trailer_t)
					- NET_RCV_MAX + count);
	kmsg->ikm_header.msgh_local_port = MACH_PORT_NULL;

	net_kmsg(kmsg)->NDR = NDR_record;
	trailer = (mach_msg_format_0_trailer_t *)((char *)&kmsg->ikm_header +
				     kmsg->ikm_header.msgh_size);
	trailer->msgh_sender = KERNEL_SECURITY_TOKEN;
	trailer->msgh_trailer_type = MACH_MSG_TRAILER_FORMAT_0;
	trailer->msgh_trailer_size = MACH_MSG_TRAILER_MINIMUM_SIZE;
	net_kmsg(kmsg)->net_rcv_msg_packet_count = count;
	return;
}

/*
 *	net_deliver:
 *
 *	Called and returns holding pool net_queue_lock, at splimp.
 *	Dequeues a message and delivers it at spllo.
 *	Returns FALSE if no messages.
 */
boolean_t
net_deliver(
	net_pool_t	p,
	boolean_t	nonblocking)
{
	register ipc_kmsg_t kmsg;
	boolean_t high_priority;
	struct ipc_kmsg_queue send_list;
	register vm_offset_t addr;

	/*
	 * Pick up a pending network message and deliver it.
	 * Deliver high priority messages before low priority.
	 */

	if ((kmsg = ipc_kmsg_dequeue(&p->net_queue_high)) != IKM_NULL) {
	    p->net_queue_high_size--;
	    high_priority = TRUE;
	} else if ((kmsg = ipc_kmsg_dequeue(&p->net_queue_low)) != IKM_NULL) {
	    p->net_queue_low_size--;
	    high_priority = FALSE;
	} else
	    return FALSE;
	simple_unlock(&p->net_queue_lock);
	(void) spllo();

	/*
	 * Run the packet through the filters,
	 * getting back a queue of packets to send.
	 */
	switch (p->net_pool_type) {
	case NET_POOL_INLINE:
		net_filter_inline(p, kmsg, &send_list);
		break;
	case NET_POOL_SG:
		panic("net_deliver: NET_POOL_SG type");
		break;
	}

	if (!nonblocking) {
	    /*
	     * There is a danger of running out of available buffers
	     * because they all get moved into the high priority queue
	     * or a port queue.  In particular, we might need to
	     * allocate more buffers as we pull (previously available)
	     * buffers out of the low priority queue.  But we can only
	     * allocate if we are allowed to block.
	     */
	    net_kmsg_more(p);
	}

	while ((kmsg = ipc_kmsg_dequeue(&send_list)) != IKM_NULL) {

		/*
		 * Package message
		 */

		switch (p->net_pool_type) {
		case NET_POOL_INLINE:
			net_package_inline(kmsg);
			break;
		case NET_POOL_SG:
			panic("net_deliver: NET_POOL_SG type");
			break;
		}

		/*
		 * Send the packet to the destination port.  Drop it
		 * if the destination port is over its backlog.
		 */

		if (ipc_mqueue_send(kmsg, MACH_SEND_TIMEOUT, 0) ==
						    MACH_MSG_SUCCESS) {
			if (high_priority)
				p->net_kmsg_send_high_hits++;
			else
				p->net_kmsg_send_low_hits++;
			/* the receiver is responsible for the message now */
		} else {
			if (high_priority)
				p->net_kmsg_send_high_misses++;
			else
				p->net_kmsg_send_low_misses++;
			net_kmsg_put_buf(p, kmsg);
		}
	}

	(void) splimp();
	simple_lock(&p->net_queue_lock);
	return TRUE;
}


/*
 * Incoming packet.  Header has already been moved to proper place.
 * We are already at splimp.
 */
void
net_packet_pool(
	net_pool_t		p,
	register struct ifnet	*ifp,
	register ipc_kmsg_t	kmsg,
	unsigned int		count,
	boolean_t		priority,
	io_req_t		ior)
{
	assert(kmsg->ikm_header.msgh_id == NET_RCV_MSG_ID ||
	       kmsg->ikm_header.msgh_id == PROMISC_RCV_MSG_ID ||
	       kmsg->ikm_header.msgh_id == NORMA_RCV_MSG_ID);
	kmsg->ikm_header.msgh_remote_port = (mach_port_t) ifp;
	if (ior != (io_req_t)0 &&
	    ior->io_device != MACH_DEVICE_NULL &&
	    (ior->io_device->flag & D_CLONED)) {
	    kmsg->ikm_header.msgh_local_port = (mach_port_t)ior->io_device;
	    ior->io_op |= IO_WCLONED;
	} else
	    kmsg->ikm_header.msgh_local_port = MACH_PORT_NULL;

	switch (p->net_pool_type) {
	case NET_POOL_INLINE:
		net_kmsg(kmsg)->net_rcv_msg_packet_count = count;
		break;
	case NET_POOL_SG:
		panic("net_packet_pool: NET_POOL_SG type");
		break;
	}

	simple_lock(&p->net_queue_lock);
	if (priority) {
	    ipc_kmsg_enqueue(&p->net_queue_high, kmsg);
	    if (++p->net_queue_high_size > p->net_queue_high_max)
		p->net_queue_high_max = p->net_queue_high_size;
	} else {
	    ipc_kmsg_enqueue(&p->net_queue_low, kmsg);
	    if (++p->net_queue_low_size > p->net_queue_low_max)
		p->net_queue_low_max = p->net_queue_low_size;
	}
	simple_unlock(&p->net_queue_lock);

	/*
	 *	If the network thread is awake, then we don't
	 *	need to take an AST, because the thread will
	 *	deliver the packet.
	 */
	simple_lock(&net_thread_lock);
	if (!net_thread_awake) {
#if DIPC || NCPUS > 1 || MACH_LDEBUG
	    boolean_t running = net_thread_running;
	    net_thread_awake = TRUE;
	    simple_unlock(&net_thread_lock);
	    if (!running)
		thread_wakeup((event_t) &net_thread_awake);
#else /* DIPC || NCPUS > 1 || MACH_LDEBUG */
	    if (!net_thread_running) {
		spl_t s;
		simple_unlock(&net_thread_lock);
		s = splsched();
		mp_disable_preemption();
		ast_on(cpu_number(), AST_NETWORK);
		mp_enable_preemption();
		splx(s);
	    } else {
		net_thread_awake = TRUE;
		simple_unlock(&net_thread_lock);
	    }
#endif /* DIPC || NCPUS > 1 || MACH_LDEBUG */
	} else
	    simple_unlock(&net_thread_lock);
}

/*
 * Check filter for invalid operations or stack over/under-flow.
 */
boolean_t
parse_net_filter(
	register filter_t	*filter,
	mach_msg_type_number_t	count)
{
	register int	sp;
	register filter_t	*fpe = &filter[count];
	register filter_t	op, arg;

	sp = NET_FILTER_STACK_DEPTH;

	for (; filter < fpe; filter++) {
	    op = NETF_OP(*filter);
	    arg = NETF_ARG(*filter);

	    switch (arg) {
		case NETF_NOPUSH:
		    break;
		case NETF_PUSHZERO:
		    sp--;
		    break;
		case NETF_PUSHLIT:
		    filter++;
		    if (filter >= fpe)
			return (FALSE);	/* literal value not in filter */
		    sp--;
		    break;
		case NETF_PUSHIND:
		case NETF_PUSHHDRIND:
		    if (sp >= NET_FILTER_STACK_DEPTH)
			return (FALSE);
		    break;
		default:
		    if (arg >= NETF_PUSHSTK) {
			if (arg - NETF_PUSHSTK + sp > NET_FILTER_STACK_DEPTH)
			    return FALSE;
		    }
		    else if (arg >= NETF_PUSHHDR) {
			if (arg - NETF_PUSHHDR >=
				NET_HDW_HDR_MAX/sizeof(unsigned short))
			    return FALSE;
		    }
		    /* else... cannot check for packet bounds
				without packet */
		    sp--;
		    break;
	    }
	    if (sp < 2) {
		return (FALSE);	/* stack overflow */
	    }
	    if (op == NETF_OP(NETF_NOP))
		continue;

	    /*
	     * all non-NOP operators are binary.
	     */
	    if (sp > NET_FILTER_STACK_DEPTH-2)
		return (FALSE);

	    sp++;
	    switch (op) {
		case NETF_OP(NETF_AND):
		case NETF_OP(NETF_OR):
		case NETF_OP(NETF_XOR):
		case NETF_OP(NETF_EQ):
		case NETF_OP(NETF_NEQ):
		case NETF_OP(NETF_LT):
		case NETF_OP(NETF_LE):
		case NETF_OP(NETF_GT):
		case NETF_OP(NETF_GE):
		case NETF_OP(NETF_COR):
		case NETF_OP(NETF_CAND):
		case NETF_OP(NETF_CNOR):
		case NETF_OP(NETF_CNAND):
		case NETF_OP(NETF_LSH):
		case NETF_OP(NETF_RSH):
		case NETF_OP(NETF_ADD):
		case NETF_OP(NETF_SUB):
		    break;
		default:
		    return (FALSE);
	    }
	}
	return (TRUE);
}

/*
 * Destroy notification for a port that holds a filter.
 */
boolean_t
net_unset_filter(
	ipc_port_t	rcv_port)
{
	net_rcv_port_t	infp;
	struct ifnet	*ifp;
	spl_t		s;

	mutex_lock(&net_rcv_list_lock);
	queue_iterate(&net_rcv_list, ifp, struct ifnet *, if_chain) {
	    mutex_lock(&ifp->if_rcv_port_list_lock);
	    queue_iterate(&ifp->if_rcv_port_list, infp, net_rcv_port_t, chain) {
		if (infp->rcv_port == rcv_port)
		    break;
	    }
	    if (!queue_end(&ifp->if_rcv_port_list, (queue_entry_t)infp))
		break;
	    mutex_unlock(&ifp->if_rcv_port_list_lock);
	}
	if (queue_end(&net_rcv_list, (queue_entry_t)ifp)) {
	    mutex_unlock(&net_rcv_list_lock);
	    return FALSE;
	}
	mutex_unlock(&net_rcv_list_lock);

	queue_remove(&ifp->if_rcv_port_list, infp, net_rcv_port_t, chain);

	if (!(ifp->if_flags & IFF_SG)) {
	    net_pool_t p = infp->pool;
	    s = splimp();
	    simple_lock(&p->net_queue_free_lock);
	    p->net_queue_free_min--;
	    simple_unlock(&p->net_queue_free_lock);
	    splx(s);
	    simple_lock(&p->net_kmsg_total_lock);
	    p->net_kmsg_max -= infp->rcv_qlimit + 1;
	    simple_unlock(&p->net_kmsg_total_lock);
	}
	mutex_unlock(&ifp->if_rcv_port_list_lock);

	ipc_port_release_send(rcv_port);
	if (infp->fpstart)
	    net_filter_free(infp->fpstart, infp->fplen);
	mach_device_deallocate(infp->device);
	zfree(net_rcv_zone, (vm_offset_t) infp);

	return TRUE;
}

/*
 * Set a filter for a network interface.
 *
 * We are given a naked send right for the rcv_port.
 * If we are successful, we must consume that right.
 */
io_return_t
net_set_filter_pool(
	net_pool_t		p,
	struct ifnet		*ifp,
	ipc_port_t		rcv_port,
	int			priority,
	filter_t		*filter,
	mach_msg_type_number_t	filter_count,
	mach_device_t	device)
{
	struct ifnet	*ifpp;
	boolean_t	found;
	boolean_t	dead;
	register net_rcv_port_t	infp, new_infp, next;
	ipc_port_t      notify;
	kern_return_t   kr;
	ipc_port_request_index_t index;
	int		i;
	filter_fct_t	fpstart;
	unsigned int	fplen;
	spl_t		s;

	/*
	 * Check the filter syntax.
	 */
	if (!parse_net_filter(filter, filter_count))
	    return (D_INVALID_OPERATION);

	/*
	 * Check for SG use in interface.
	 */
	if ((ifp->if_flags & IFF_SG) && (p->net_pool_type != NET_POOL_SG))
	    return (D_INVALID_OPERATION);

	/*
	 * Check that the interface is on the list, and insert it if needed.
	 */
	mutex_lock(&net_rcv_list_lock);
	queue_iterate(&net_rcv_list, ifpp, struct ifnet *, if_chain) {
	    if (ifpp == ifp)
		break;
	}
	if (queue_end(&net_rcv_list, (queue_entry_t)ifpp))
	    queue_enter(&net_rcv_list, ifp, struct ifnet *, if_chain);
	mutex_unlock(&net_rcv_list_lock);

	/*
	 * Compute machine-dependent net_filter program.
	 */
	fpstart = (filter_count ?
		   net_filter_alloc(filter, filter_count, &fplen) :
		   (filter_fct_t)0);

	/*
	 * Look for an existing filter on the same reply port.
	 * If there is one, replace it.
	 */
	assert(IP_VALID(rcv_port));
	new_infp = 0;
	notify = IP_NULL;

	mutex_lock(&ifp->if_rcv_port_list_lock);
	ip_lock(rcv_port);
	while (TRUE) {
	    found = FALSE;
	    queue_iterate(&ifp->if_rcv_port_list, infp, net_rcv_port_t, chain) {
		register int i,exists;
		/*
		 * For SG type interfaces check for an existing
		 * filter since there is no support for multiple
		 * receivers (ool messages). Will only find
		 * exact matches.
		 */
		if ((ifp->if_flags & IFF_SG)) {
		     /* check for same filter size */
		     if (filter_count == (infp->filter_end - &infp->filter[0])) {
			  /* check for active port */
			  if (io_active(&(infp->rcv_port)->ip_object)) {
			       /* check each filter element  */
			       exists = TRUE;
			       for (i = 0; i < filter_count; i++) {
				    if (infp->filter[i] != filter[i])
					 exists = FALSE;
			       }
			       if (exists) {
				    if (notify != IP_NULL)
					notify = ipc_port_dncancel(rcv_port,
						(mach_port_t)rcv_port, index);
				    ip_unlock(rcv_port);
				    mutex_unlock(&ifp->if_rcv_port_list_lock);
				    if (notify != IP_NULL)
					ipc_port_release_sonce(notify);
				    if (fpstart)
					 net_filter_free(fpstart, fplen);
				    if (new_infp != (net_rcv_port_t)0)
					zfree(net_rcv_zone,
					      (vm_offset_t) new_infp);
				    return (D_INVALID_OPERATION);
			       }
			  }
		     }
		}

		if (infp->rcv_port == rcv_port) {
		    /*
		     * Keep the old filter from list, and re-use its
		     * data structure while handling the interface lock.
		     */
		    found = TRUE;
		    break;
		}
	    }
	    if (found) {
		break;
	    }

	    if (new_infp) {
		/*
		 * No existing filter - use the new structure.
		 */
		infp = new_infp;
		break;
	    }

	    /*
	     * Must unlock to allocate a new filter and to manage dnrequest.
	     */
	    ip_unlock(rcv_port);
	    mutex_unlock(&ifp->if_rcv_port_list_lock);

	    /*
	     * If someone else has added a filter on the same port after we
	     * have allocated, release the new structure and indicate an error.
	     */
	    new_infp = (net_rcv_port_t)zalloc(net_rcv_zone);
	    new_infp->pool = p;
	    new_infp->rcv_port = rcv_port;
	    new_infp->rcv_count = (mach_port_msgcount_t)0;

	    /*
	     * Support for remote receivers. Request a dead name notification
	     * for all receive ports added to filter list. Send the dead name
	     * notification to the master_device_port.
	     */
	    notify = ipc_port_make_sonce(master_device_port);
	    for (;;) {
		mutex_lock(&ifp->if_rcv_port_list_lock);
		ip_lock(rcv_port);
		if (!ip_active(rcv_port) ||
		    ipc_port_dnrequest(rcv_port, (mach_port_t)rcv_port,
				       notify, &index) == KERN_SUCCESS)
			break;

		mutex_unlock(&ifp->if_rcv_port_list_lock);
		if (ipc_port_dngrow(rcv_port, ITS_SIZE_NONE) != KERN_SUCCESS)
		    panic("net_set_filter_pool: ipc_port_dngrow failure");
	    }

	    /*
	     * If port is no longer valid, then immediately exit the loop.
	     */
	    if (!ip_active(rcv_port))
		break;
	}

	/*
	 * Deal with invalid port.
	 */
	if (!ip_active(rcv_port)) {
	    ip_unlock(rcv_port);
	    mutex_unlock(&ifp->if_rcv_port_list_lock);
	    if (notify != IP_NULL)
		ipc_port_release_sonce(notify);
	    /*
	     * The packet filter is being (or will be) destroyed in
	     * net_unset_filter. So, just free the allocated structure if any.
	     */
	    if (fpstart)
		net_filter_free(fpstart, fplen);
	    if (!found || new_infp)
		zfree(net_rcv_zone, (vm_offset_t) new_infp);
	    return (D_INVALID_OPERATION);
	}

	/*
	 * If a second dead name notification has been added, remove it.
	 */
	if (found && notify != IP_NULL)
	    notify = ipc_port_dncancel(rcv_port, (mach_port_t)rcv_port, index);
	ip_unlock(rcv_port);

	/*
	 * Install new filter program.
	 */
	infp->priority = priority;
	for (i = 0; i < filter_count; i++)
	    infp->filter[i] = filter[i];
	infp->filter_end = &infp->filter[i];

	/*
	 * Install machine-dependent filter.
	 */
	if (found && infp->fpstart)
	    net_filter_free(infp->fpstart, infp->fplen);
	infp->fpstart = fpstart;
	infp->fplen = fplen;

	if (found) {
	    assert(infp->pool == p);
	    if (device != infp->device) {
		    mach_device_deallocate(infp->device);
		    infp->device = device;
		    mach_device_reference(infp->device);
	    }
	    mutex_unlock(&ifp->if_rcv_port_list_lock);

	    /*
	     * We found the receive port, so we already have a reference.
	     * Deallocate the extra reference.
	     */
	    ipc_port_release_send(rcv_port);
	    if (notify != IP_NULL)
		ipc_port_release_sonce(notify);
	    if (new_infp)
		zfree(net_rcv_zone, (vm_offset_t) new_infp);

	} else {
	    /*
	     * Install limit values.
	     */
	    infp->rcv_qlimit = rcv_port->ip_qlimit;
	    if (!(ifp->if_flags & IFF_SG)) {
		/*
		 * Since a new not-SG type filter has been allocated,
		 * increase net_queue_free_min and net_kmsg_max to
		 * allow for more queued messages.
		 */
		simple_lock(&p->net_kmsg_total_lock);
		p->net_kmsg_max += infp->rcv_qlimit + 1;
		simple_unlock(&p->net_kmsg_total_lock);
		s = splimp();
		simple_lock(&p->net_queue_free_lock);
		p->net_queue_free_min++;
		simple_unlock(&p->net_queue_free_lock);
		splx(s);
	    }

	    /*
	     * Insert according to priority.
	     */
	    queue_iterate(&ifp->if_rcv_port_list, next, net_rcv_port_t, chain) {
		if (infp->priority > next->priority)
		    break;
	    }
	    queue_insert_before(&ifp->if_rcv_port_list,
				infp, next, net_rcv_port_t, chain);
	    infp->device = device;
	    mach_device_reference(device);
	    mutex_unlock(&ifp->if_rcv_port_list_lock);
	}

	return (D_SUCCESS);
}

/*
 * Compute a hash value for boosting multicast addresses value comparison.
 */
int
net_multicast_hash(
	unsigned char	*addr,
	unsigned	len)
{
	int		ret;

	ret = 0;
	while (len-- > 0)
	   ret += *addr++;
	return (ret);
}

/*
 * Look for a matching multicast address range in a HASH-ordered multicast list.
 * MP notes: MP protection has been setup before being called.
 */
net_multicast_t *
net_multicast_match(
	queue_t		queue,
	unsigned char	*addr,
	unsigned	len)
{
	net_multicast_t	*cur;
	int		hash;
	unsigned char	*p1;
	unsigned char	*p2;
	unsigned	i;

	if (queue_empty(queue))
	    return (NULL);

	hash = net_multicast_hash(addr, len);
	queue_iterate(queue, cur, net_multicast_t *, chain) {
	    if (cur->len != len || (cur->hash >= 0 && cur->hash < hash))
		continue;

	    if (cur->hash > hash)
		return (NULL);

	    p1 = (unsigned char *)cur + sizeof (net_multicast_t);
	    p2 = addr;
	    for (i = 0; i < len; i++) {
		if (*p1 != *p2)
		    break;
		p1++;
		p2++;
	    }

	    if (cur->hash >= 0) {
		if (i == len)
		    return (cur);
	    } else if (i == len || *p1 < *p2) {
		p1 = addr;
		p2 = (unsigned char *)cur + sizeof (net_multicast_t) + len;
		for (i = 0; i < len; i++) {
		    if (*p1 != *p2)
			break;
		    p1++;
		    p2++;
		}
		if (i == len || *p1 < *p2)
		    return (cur);
	    }
	}
	return (NULL);
}

/*
 * Create a new multicast address range in a HASH-ordered multicast list.
 */
void
net_multicast_create(
	net_multicast_t	*new,
	unsigned char	*from,
	unsigned char	*to,
	unsigned	len)
{
	unsigned char	*p1;
	unsigned char	*p2;
	unsigned	i;

	new->len = len;

	p1 = from;
	p2 = to;
	for (i = 0; i < len; i++) {
	    if (*p1 != *p2)
		break;
	    p1++;
	    p2++;
	}

	if (i == len) {
	    new->hash = net_multicast_hash(from, len);
	    bcopy((char *)from,
		  (char *)NET_MULTICAST_FROM(new), len);
	    bcopy((char *)to,
		  (char *)NET_MULTICAST_TO(new), len);
	} else {
	    new->hash = -1;
	    if (*p1 < *p2) {
		bcopy((char *)from,
		      (char *)NET_MULTICAST_FROM(new), len);
		bcopy((char *)to,
		      (char *)NET_MULTICAST_TO(new), len);
	    } else {
		bcopy((char *)to,
		      (char *)NET_MULTICAST_FROM(new), len);
		bcopy((char *)from,
		      (char *)NET_MULTICAST_TO(new), len);
	    }
	}

	if (new->size > NET_MULTICAST_LENGTH(new->len))
		bzero((char *)NET_MULTICAST_MISC(new),
		      new->size - NET_MULTICAST_LENGTH(new->len));
}

/*
 * Remove a multicast address range in a HASH-ordered multicast list.
 * MP notes: MP protection has been setup before being called.
 */
net_multicast_t *
net_multicast_remove(
	queue_t		queue,
	unsigned char	*from,
	unsigned char	*to,
	unsigned	len)
{
	net_multicast_t	*cur;
	int		hash;
	unsigned char	*p1;
	unsigned char	*p2;
	unsigned	i;

	if (queue_empty(queue))
	    return (NULL);

	p1 = from;
	p2 = to;
	for (i = 0; i < len; i++) {
	    if (*p1 != *p2)
		break;
	    p1++;
	    p2++;
	}

	if (i == len)
	    hash = net_multicast_hash(from, len);
	else {
	    hash = -1;
	    if (*p1 > *p2) {
		p1 = from;
		from = to;
		to = p1;
	    }
	}

	queue_iterate(queue, cur, net_multicast_t *, chain) {
	    if (cur->len != len || cur->hash < hash)
		continue;

	    if (cur->hash > hash)
		return (NULL);

	    p1 = NET_MULTICAST_FROM(cur);
	    p2 = from;
	    for (i = 0; i < len; i++) {
		if (*p1 != *p2)
		    break;
		p1++;
		p2++;
	    }

	    if (i == len) {
		p1 = NET_MULTICAST_TO(cur);
		p2 = to;
		for (i = 0; i < len; i++) {
		    if (*p1 != *p2)
			break;
		    p1++;
		    p2++;
		}
	    }

	    if (i == len) {
		queue_remove(queue, cur, net_multicast_t *, chain);
		return (cur);
	    }
	}
	return (NULL);
}

/*
 * Other network operations
 */
io_return_t
net_getstat(
	struct ifnet		*ifp,
	dev_flavor_t		flavor,
	dev_status_t		status,		/* pointer to OUT array */
	mach_msg_type_number_t	*count)		/* IN/OUT */
{

	switch (flavor) {
	    case NET_STATUS:
	    {
		struct net_status ns;

		ns.min_packet_size = ifp->if_header_size;
		ns.max_packet_size = ifp->if_header_size + ifp->if_mtu;
		ns.header_format   = ifp->if_header_format;
		ns.header_size	   = ifp->if_header_size;
		ns.address_size    = ifp->if_address_size;
		ns.flags	   = ifp->if_flags;
		ns.mapped_size	   = 0;
		ns.max_queue_size  = ifp->if_snd.ifq_maxlen;

		if (*count > NET_STATUS_COUNT)
		    *count = NET_STATUS_COUNT;
		bcopy((char *)&ns, (char *)status, *count * sizeof(int));
		break;
	    }

	    case NET_ADDRESS:
	    {
		register int	addr_byte_count;
		register int	addr_int_count;
		register int	i;

		addr_byte_count = ifp->if_address_size;
		addr_int_count = (addr_byte_count + (sizeof(long)-1))
					 / sizeof(int);
		if (*count < addr_int_count) {
		    *count = 0;
		    break;
		}

		bcopy((char *)ifp->if_address,
		      (char *)status,
		      (unsigned) addr_byte_count);
		if (addr_byte_count < addr_int_count * sizeof(int))
		    bzero((char *)status + addr_byte_count,
			  (unsigned) (addr_int_count * sizeof(int)
				      - addr_byte_count));

		for (i = 0; i < addr_int_count; i++) {
		    register int word;

		    word = status[i];
		    status[i] = htonl(word);
		}
		*count = addr_int_count;
		break;
	    }

	    default:
		return (D_INVALID_OPERATION);
	}
	return (D_SUCCESS);
}

io_return_t
net_write(
	struct ifnet	*ifp,
	void		(*start)(int unit),
	io_req_t	ior)
{
	spl_t		s;
	kern_return_t	rc;
	boolean_t	wait;
	boolean_t	ior_sync = ((ior->io_op & IO_SYNC) != 0);

	/*
	 * Reject the write if the interface is down.
	 */
	if ((ifp->if_flags & (IFF_UP|IFF_RUNNING)) != (IFF_UP|IFF_RUNNING))
	    return (D_DEVICE_DOWN);

	/*
	 * Reject the write if the packet is too large or too small.
	 */
	if (ior->io_count < ifp->if_header_size ||
	    ior->io_count > ifp->if_header_size + ifp->if_mtu)
	    return (D_INVALID_SIZE);

	/*
	 * Wire down the memory.
	 */

	rc = device_write_get(ior, &wait);
	if (rc != KERN_SUCCESS)
	    return (rc);

	/*
	 *	Network interfaces can't cope with VM continuations.
	 *	If wait is set, just panic.
	*/
	if (wait) {
		panic("net_write: VM continuation");
	}

	/*
	 * Queue the packet on the output queue, waiting if it is an
	 * asynchronous write in a full output queue, and start the device.
	 */
	s = splimp();
	IF_ENQUEUE_WAIT(&ifp->if_snd, ior);
	(*start)(ifp->if_unit);
	splx(s);

	/*
	 * Wait for fully synchronous operations
	 */
	if (ior_sync) {
		iowait(ior);
		return(D_SUCCESS);
	}

	return (D_IO_QUEUED);
}

/*
 * Create Pool - Create a buffer pool
 *
 * Possible errors
 *	KERN_RESOURCE_SHORTAGE - unable to allocate pool resources.
 */
kern_return_t
net_kmsg_create_pool(
	net_pool_type_t ptype,	/* Pool type */
	vm_size_t bufsize,	/* Total buffer size (in bytes) */
	int minbufs,		/* # of buffers in pool (to start with) */
	boolean_t sharable,	/* Does caller want to share this pool */
	char *name,		/* Name - Must be static! */
	net_pool_t *pool_result) /* New Pool Handle (OUT arg) */
{
	register net_pool_t p = NET_POOL_NULL;
	vm_size_t kmsg_size;
	spl_t s;

	switch (ptype) {
	case NET_POOL_INLINE:
		kmsg_size = round_page(ikm_plus_overhead(bufsize));
		break;
	case NET_POOL_SG:
		panic("net_kmsg_create_pool: NET_POOL_SG type");
		break;
	default:
		*pool_result = NET_POOL_NULL;
		return(KERN_INVALID_ARGUMENT);
	}

	for (;;) {
		if (sharable) {
			net_pool_t pp;
			simple_lock(&net_pool_lock);
			pp = net_pool_list;

			while (pp != NET_POOL_NULL) {
				if (pp->net_pool_sharable &&
				    pp->net_pool_type == ptype &&
				    pp->net_kmsg_size == kmsg_size)
				    break;
				pp = pp->net_pool_next;
			}


			if (pp != NET_POOL_NULL) {
				pp->net_pool_refcount++;
				simple_unlock(&net_pool_lock);

				simple_lock(&pp->net_kmsg_total_lock);
				pp->net_kmsg_max += minbufs;
				simple_unlock(&pp->net_kmsg_total_lock);

				s = splimp();
				simple_lock(&pp->net_queue_free_lock);
				pp->net_queue_free_min += minbufs;
				simple_unlock(&pp->net_queue_free_lock);
				splx(s);

				if (p != NET_POOL_NULL)
					kfree((vm_offset_t)p, sizeof (*p));
				*pool_result = pp;
				return(KERN_SUCCESS);
			}
			else if (p != NET_POOL_NULL)
				break;
			else
				simple_unlock(&net_pool_lock);

		} else if (p != NET_POOL_NULL)
			break;

		p = (net_pool_t) kalloc(sizeof(*p));
		if (p == NET_POOL_NULL) {
			*pool_result = NET_POOL_NULL;
			return(KERN_RESOURCE_SHORTAGE);
		}

		bzero((char *)p, sizeof(*p));

		/*
		 * Fill in all the good stuff
		 */
		p->net_pool_type = ptype;
		p->net_pool_name = name;
		p->net_pool_sharable = sharable;
		p->net_queue_free_min = minbufs;
		p->net_kmsg_max = minbufs;
		p->net_kmsg_size = kmsg_size;
		p->net_pool_refcount = 1;

		simple_lock_init(&p->net_queue_free_lock, ETAP_NET_QFREE);
		simple_lock_init(&p->net_kmsg_total_lock, ETAP_NET_KMSG);
		simple_lock_init(&p->net_queue_lock, ETAP_NET_Q);

		queue_init(&p->net_queue_free_callback);
		ipc_kmsg_queue_init(&p->net_queue_free);
		ipc_kmsg_queue_init(&p->net_queue_high);
		ipc_kmsg_queue_init(&p->net_queue_low);
	}

	/*
	 * Add to the list of pools.
	 * We only add to the front of the list so
	 * we can be deliver packets at the same time
	 * as pool additions.
	 */
	if (!sharable)
		simple_lock(&net_pool_lock);
	p->net_pool_next = net_pool_list;
	net_pool_list = p;
	simple_unlock(&net_pool_lock);

	*pool_result = p;
	return(KERN_SUCCESS);
}

/*
 * Grow Pool - Add buffers to pool free list
 */
kern_return_t
net_kmsg_grow_pool(
	net_pool_t p,			/* Pool Handle */
	int nbufs,			/* # of buffers to add to pool */
	int *newmax)			/* # of buffers in pool (OUT arg) */
{
	spl_t s;

	simple_lock(&p->net_kmsg_total_lock);
	p->net_kmsg_max += nbufs;
	*newmax = p->net_kmsg_max;
	simple_unlock(&p->net_kmsg_total_lock);

	s = splimp();
	simple_lock(&p->net_queue_free_lock);
	p->net_queue_free_min += nbufs;
	simple_unlock(&p->net_queue_free_lock);
	splx(s);

	return(KERN_SUCCESS);
}

/*
 * Shrink Pool - Removed buffers from pool free list
 *
 * Possible errors
 *	KERN_INVALID_ARGUMENT - removal exceeds pool total
 */
kern_return_t
net_kmsg_shrink_pool(
	net_pool_t p,			/* Pool Handle */
	int nbufs,			/* # of buffers to remove from pool */
	int *newmax)			/* # of buffers in pool (OUT arg) */
{
	spl_t s = splimp();
	simple_lock(&p->net_queue_free_lock);
	if (p->net_queue_free_min < nbufs) {
		simple_unlock(&p->net_queue_free_lock);
		splx(s);
		return(KERN_INVALID_ARGUMENT);
	}
	p->net_queue_free_min -= nbufs;
	simple_unlock(&p->net_queue_free_lock);
	splx(s);

	simple_lock(&p->net_kmsg_total_lock);
	assert (p->net_kmsg_max >= nbufs);
	p->net_kmsg_max -= nbufs;
	*newmax = p->net_kmsg_max;
	simple_unlock(&p->net_kmsg_total_lock);

	net_kmsg_collect_pool(p);
	return(KERN_SUCCESS);
}

/*
 * Collect Pool - Collect free queue pages from a net pool.
 */

void
net_kmsg_collect_pool(
	net_pool_t	p)		/* Pool Handle */
{
	register ipc_kmsg_t kmsg;
	spl_t s;

	s = splimp();
	simple_lock(&p->net_queue_free_lock);
	while (p->net_queue_free_size > p->net_queue_free_min) {
		kmsg = ipc_kmsg_dequeue(&p->net_queue_free);
		p->net_queue_free_size--;
		simple_unlock(&p->net_queue_free_lock);
		splx(s);
		net_kmsg_free_buf(p, kmsg);
		s = splimp();
		simple_lock(&p->net_queue_free_lock);
	}
	simple_unlock(&p->net_queue_free_lock);
	splx(s);
}

/*
 * Allocate/Free (possibly blocking)
 */

/*
 * Allocate Buffer - Allocate buffer for a particular pool.
 *
 * Possible errors
 *	IKM_NULL - no more resources
 */
ipc_kmsg_t
net_kmsg_alloc_buf(
	net_pool_t	p)		/* Pool Handle */
{
	register struct io_sglist *sglist;
	register ipc_kmsg_t kmsg;
	vm_map_copy_t copy;
	kern_return_t ret;
	vm_size_t size;
	register int ii;
	int nentries;

	/*
	 * Allocate and return a new one if we are allowed to.
	 */
	if (p->net_kmsg_total >= p->net_kmsg_max) {
		return(IKM_NULL);
	}

	/*
	 * Ok to allocate.
	 */
	switch (p->net_pool_type) {
	case NET_POOL_INLINE:
		kmsg = (ipc_kmsg_t) kalloc(p->net_kmsg_size);
		if (kmsg == IKM_NULL)
			return(IKM_NULL);
		break;

	case NET_POOL_SG:
		panic("net_kmsg_create_pool: NET_POOL_SG type");
		break;
	}

	/* increment the total */
	simple_lock(&p->net_kmsg_total_lock);
	p->net_kmsg_total++;
	simple_unlock(&p->net_kmsg_total_lock);
	return(kmsg);
}

/*
 * Free Buffer - Free buffer from a particular pool.
 */
kern_return_t
net_kmsg_free_buf(
	net_pool_t p,			/* Pool Handle */
	ipc_kmsg_t kmsg)
{
	struct io_sglist *sglist;

	if (kmsg == IKM_NULL) {
		panic("net_kmsg_free_buf: NULL kmsg");
		return(KERN_FAILURE);
	}

	switch (p->net_pool_type) {
	case NET_POOL_INLINE:
		kfree((vm_offset_t) (kmsg), p->net_kmsg_size);
		break;

	case NET_POOL_SG:
		panic("net_kmsg_free_buf: NET_POOL_SG type");
		break;
	}

	/* decrement the total */
	simple_lock(&p->net_kmsg_total_lock);
	p->net_kmsg_total--;
	simple_unlock(&p->net_kmsg_total_lock);

	return(KERN_SUCCESS);
}


/*
 * Get/Put (non-blocking)
 */

/*
 * Get Buffer - Get free buffer from pool free list
 *
 * Possible errors
 *	IKM_NULL - no available buffers
 */
ipc_kmsg_t
net_kmsg_get_buf(
	net_pool_t	p)		/* Pool Handle */
{
	register ipc_kmsg_t kmsg;
	spl_t s;

	/*
	 *	First check the list of free buffers.
	 */
	s = splimp();
	simple_lock(&p->net_queue_free_lock);
	kmsg = ipc_kmsg_queue_first(&p->net_queue_free);
	if (kmsg != IKM_NULL) {
	    ipc_kmsg_rmqueue_first_macro(&p->net_queue_free, kmsg);
	    p->net_queue_free_size--;
	    p->net_queue_free_hits++;
	}
	simple_unlock(&p->net_queue_free_lock);

	if (kmsg == IKM_NULL) {
	    /*
	     *	Try to steal from the low priority queue.
	     */
	    simple_lock(&p->net_queue_lock);
	    kmsg = ipc_kmsg_queue_first(&p->net_queue_low);
	    if (kmsg != IKM_NULL) {
		ipc_kmsg_rmqueue_first_macro(&p->net_queue_low, kmsg);
		p->net_queue_low_size--;
		p->net_queue_free_steals++;
	    }
	    simple_unlock(&p->net_queue_lock);
	}

	if (kmsg == IKM_NULL)
	    p->net_queue_free_misses++;
	else
	    kmsg->ikm_private = (vm_offset_t)p;
	splx(s);

	if (net_kmsg_want_more(p) || (kmsg == IKM_NULL)) {
	    boolean_t awake;

	    s = splimp();
	    simple_lock(&net_thread_lock);
	    awake = net_thread_awake || net_thread_running;
	    if (!net_thread_awake)
		 net_thread_awake = TRUE;
	    simple_unlock(&net_thread_lock);
	    splx(s);

	    if (!awake)
		thread_wakeup((event_t) &net_thread_awake);
	}

	return kmsg;
}

/*
 * Put Buffer - Put buffer back on pool free list
 */
void
net_kmsg_put_buf(
	net_pool_t p,		/* Pool Handle */
	ipc_kmsg_t kmsg)
{
	spl_t s;

	if (kmsg == IKM_NULL)
		return;

	s = splimp();
	simple_lock(&p->net_queue_free_lock);
	ipc_kmsg_enqueue_macro(&p->net_queue_free, kmsg);
	if (++p->net_queue_free_size > p->net_queue_free_max)
	    p->net_queue_free_max = p->net_queue_free_size;
	if (!queue_empty(&p->net_queue_free_callback)){
	    /*
	     * Wakeup net_thread if there is any callback that may have not
	     * been satisfied by last net_kmsg_more() call.
	     */
	    boolean_t awake;
	    simple_unlock(&p->net_queue_free_lock);
	    simple_lock(&net_thread_lock);
	    awake = net_thread_awake || net_thread_running;
	    if (!net_thread_awake)
		net_thread_awake = TRUE;
	    simple_unlock(&net_thread_lock);
	    splx(s);
	    if (!awake)
		thread_wakeup((event_t) &net_thread_awake);
	} else {
	    simple_unlock(&p->net_queue_free_lock);
	    splx(s);
	}
	return;
}

/*
 * COMPATIBILITY code.
 */
ipc_kmsg_t
net_kmsg_get(void)
{
	return(net_kmsg_get_buf(inline_pagepool));
}

void
net_kmsg_put(
	ipc_kmsg_t	kmsg)
{
	net_kmsg_put_buf((net_pool_t)kmsg->ikm_private, kmsg);
}

ipc_kmsg_t
net_kmsg_alloc(void)
{
	return(net_kmsg_alloc_buf(inline_pagepool));
}

void
net_kmsg_free(
	ipc_kmsg_t kmsg)
{
	net_kmsg_free_buf(inline_pagepool, kmsg);
}

/*
 * Set a filter for a network interface.
 *
 * We are given a naked send right for the rcv_port.
 * If we are successful, we must consume that right.
 */
io_return_t
net_set_filter(
	struct ifnet		*ifp,
	ipc_port_t		rcv_port,
	int			priority,
	filter_t		*filter,
	mach_msg_type_number_t	filter_count,
	mach_device_t		device)
{
	return(net_set_filter_pool(inline_pagepool, ifp, rcv_port,
			priority, filter, filter_count, device));
}

/*
 * Incoming packet.  Header has already been moved to proper place.
 * We are already at splimp.
 */
void
net_packet(
	register struct ifnet	*ifp,
	register ipc_kmsg_t	kmsg,
	unsigned int		count,
	boolean_t		priority,
	io_req_t		ior)
{
#if	NORMA_ETHER
	if (netipc_net_packet(kmsg, count)) {
		if (!(ifp->if_flags & IFF_PROMISC) || node_self_addr == (struct node_addr *) 0) {
			net_kmsg_put(kmsg);
			return;
		}
		kmsg->ikm_header.msgh_id = NORMA_RCV_MSG_ID;
	} else
#endif	/* NORMA_ETHER */
		kmsg->ikm_header.msgh_id =
			(ior != (io_req_t)0 && (ifp->if_flags & IFF_PROMISC)) ?
				PROMISC_RCV_MSG_ID : NET_RCV_MSG_ID;
	net_packet_pool(inline_pagepool, ifp, kmsg, count, priority, ior);
}

/*
 *	We want to deliver packets using ASTs, so we can avoid the
 *	thread_wakeup/thread_block needed to get to the network
 *	thread.  However, we can't allocate memory in the AST handler,
 *	because memory allocation might block.  Hence we have the
 *	network thread to allocate memory.  The network thread also
 *	delivers packets, so it can be allocating and delivering for a
 *	burst.  net_thread_awake is protected by net_queue_lock
 *	(instead of net_queue_free_lock) so that net_packet and
 *	net_ast can safely determine if the network thread is running.
 *	This prevents a race that might leave a packet sitting without
 *	being delivered.  It is possible for net_kmsg_get to think
 *	the network thread is awake, and so avoid a wakeup, and then
 *	have the network thread sleep without allocating.  The next
 *	net_kmsg_get will do a wakeup.
 */

void net_ast(void)
{
	int s;
	register net_pool_t listp, p;
	boolean_t awake;

#if DIPC || NCPUS > 1 || MACH_LDEBUG
	panic("There should be no network AST with DIPC\n");
#else /* DIPC || NCPUS > 1 || MACH_LDEBUG */
	net_ast_taken++;

#if	MACH_RT
	if (current_thread()->sched_pri >= DEPRESSPRI)
#else	/* MACH_RT */
	if (current_thread()==current_processor()->idle_thread)
#endif	/* MACH_RT */
	{
	    /* 
	     * We cannot call net_deliver from the idle thread
	     * since net_deliver can block in ipc_mqueue_send.
	     * We also cannot allow depressed priority threads
	     * to do so in preemptible systems, as this can cause
	     * a priority inversion.
	     */
	    s = splimp();
	    simple_lock(&net_thread_lock);
	    awake = net_thread_awake || net_thread_running;
	    if (!net_thread_awake)
		net_thread_awake = TRUE;
	    simple_unlock(&net_thread_lock);

	} else {
	    /*
	     *	If the network thread is awake, then we would
	     *	rather deliver messages from it, because
	     *	it can also allocate memory.
	     */
	    simple_lock(&net_pool_lock);
	    listp = net_pool_list;
	    simple_unlock(&net_pool_lock);

	    awake = FALSE;
	    s = splimp();
	    for (p = listp;
		 p != NET_POOL_NULL && !awake; p = p->net_pool_next) {
		/*
		 * Start delivering.
		 * If we find the network thread awake,
		 * bail out and let it deliver the packets
		 */
		simple_lock(&p->net_queue_lock);
		do {
		    simple_lock(&net_thread_lock);
		    if (net_thread_awake || net_thread_running) {
			simple_unlock(&net_thread_lock);
			awake = TRUE;
			break;
		    }
		    simple_unlock(&net_thread_lock);
		} while (net_deliver(p, TRUE));
		simple_unlock(&p->net_queue_lock);
	    }
	    if (!awake) {
		splx(s);
		return;
	    }
	}

	/*
	 *	Prevent an unnecessary AST, because the network
	 *	thread will deliver the messages
	 */
	(void) splsched();
	mp_disable_preemption();
	ast_off(cpu_number(), AST_NETWORK);
	mp_enable_preemption();
	splx(s);
	if (!awake)
	    thread_wakeup((event_t) &net_thread_awake);
#endif /* DIPC || NCPUS > 1 || MACH_LDEBUG */
}

void
net_kmsg_callback(
	net_pool_t p,
	net_callback_t callback)
{
	boolean_t awake;
	spl_t s;

	s = splimp();
	simple_lock(&p->net_queue_free_lock);
	queue_enter(&p->net_queue_free_callback,
		    callback, net_callback_t, ncb_chain);
	simple_unlock(&p->net_queue_free_lock);

	simple_lock(&net_thread_lock);
	awake = net_thread_awake || net_thread_running;
	if (!net_thread_awake)
	    net_thread_awake = TRUE;
	simple_unlock(&net_thread_lock);
	splx(s);

	if (!awake)
	    thread_wakeup((event_t) &net_thread_awake);
}

void
net_thread(void)
{
	spl_t s;
	boolean_t sleep_first = TRUE;

#if	NORMA_ETHER
	sleep_first = FALSE;
#endif	/* !NORMA_ETHER */
#ifdef	AT386
#include <sce.h>
#if	NSCE > 0
	sleep_first = FALSE;
#endif	/* NSCE > 0 */
#endif	/* AT386 */
	thread_swappable(current_act(), FALSE);

	/*
	 *	We should be very high priority.
	 */

	thread_set_own_priority(BASEPRI_KERNEL+2);

	/*
	 *	We sleep initially, so that we don't allocate any buffers
	 *	unless the network is really in use and they are needed.
	 */
	s = splimp();
	simple_lock(&net_thread_lock);
	if (sleep_first) {
		net_thread_running = net_thread_awake = FALSE;
		counter(c_net_thread_block++);
		thread_sleep_simple_lock((event_t)&net_thread_awake,
					 &net_thread_lock, FALSE);
		net_thread_awaken++;
		simple_lock(&net_thread_lock);
	}
	net_thread_running = TRUE;

	for (;;) {
		register net_pool_t listp, p;

		/*
		 * Update thread status
		 */
		net_thread_awake = FALSE;
		simple_unlock(&net_thread_lock);
		splx(s);

		/*
		 * Get current list start.
		 */
		simple_lock(&net_pool_lock);
		listp = net_pool_list;
		simple_unlock(&net_pool_lock);

		for (p = listp; p != NET_POOL_NULL; p = p->net_pool_next) {
			/*
			 *	First get more buffers.
			 */
			net_kmsg_more(p);
		}

		s = splimp();
		for (p = listp; p != NET_POOL_NULL; p = p->net_pool_next) {
			simple_lock(&p->net_queue_lock);
			while (net_deliver(p, FALSE))
				continue;
			simple_unlock(&p->net_queue_lock);
		}

		/*
		 * Update thread status
		 */
		simple_lock(&net_thread_lock);
		if (!net_thread_awake) {
		    
		    net_thread_running = FALSE;
		    counter(c_net_thread_block++);
		    thread_sleep_simple_lock((event_t)&net_thread_awake,
					     &net_thread_lock, FALSE);
		    net_thread_awaken++;
		    simple_lock(&net_thread_lock);
		    net_thread_running = TRUE;
		}
	}
}

/*
 * Initialize the whole package.
 */
void
net_io_init(void)
{
	register vm_size_t	size;

	size = sizeof(struct net_rcv_port);
	net_rcv_zone = zinit(size,
			     size * 1000,
			     PAGE_SIZE,
			     "net_rcv_port");

	simple_lock_init(&net_pool_lock,	ETAP_NET_POOL);
	simple_lock_init(&net_thread_lock,	ETAP_NET_THREAD);

	queue_init(&net_rcv_list);
	mutex_init(&net_rcv_list_lock, ETAP_NET_RCV);

	/*
	 * Create inline pool for existing drivers not using
	 * new interface.
	 */
	if (net_kmsg_create_pool(
		 NET_POOL_INLINE,
		 sizeof(struct net_rcv_msg),
		 net_inline_free_min, FALSE, "inline-pagepool",
		 &inline_pagepool) != KERN_SUCCESS) {
		panic("net_io_init: unable to create inline pool");
	}
	net_kmsg_size =
	    round_page(ikm_plus_overhead(sizeof (struct net_rcv_msg)));
}

#if	MACH_KDB
#include <ddb/db_output.h>
#define printf        db_printf

/* Forward */

void  db_net_filter_print(
		filter_t        *fp,
		filter_t        *fpe);

/*
 * Print all net_recv_port for a device
 */
void
net_recv_port_print(
	struct ifnet 	*ifp)
{
	register net_rcv_port_t infp,nextfp;
	int i;

	printf("net_rcv_port for network device, ifp =  0x%x\n",ifp);
	for (infp = (net_rcv_port_t) queue_first(&ifp->if_rcv_port_list);
	     !queue_end(&ifp->if_rcv_port_list, (queue_entry_t)infp);
	     infp = nextfp) {
		nextfp = (net_rcv_port_t) queue_next(&infp->chain);
		printf("infp->rcv_port   0x%x\n",infp->rcv_port);
		ipc_port_print(infp->rcv_port, FALSE, 0, "");
		printf("infp->pool	 0x%x\n",infp->pool);
		printf("infp->rcv_qlimit   %d\n",infp->rcv_qlimit);
		printf("infp->rcv_count    %d\n",infp->rcv_count);
		printf("infp->priority     %d\n",infp->priority);
		printf("infp->fpstart    0x%x\n",infp->fpstart);
		printf("infp->fplen        %d\n",infp->fplen);
		printf("infp->filter_end 0x%x\n",infp->filter_end);
		printf("infp->filter[]   0x%x\n",&infp->filter[0]);
		db_net_filter_print(infp->filter, infp->filter_end);
		printf("\n");
	}
}

/*
 * Print statistics of all net_pools
 */
void
net_pool_print(void)
{
	register net_pool_t p;

	for (p = net_pool_list; p != NET_POOL_NULL; p = p->net_pool_next) {
		printf("NET_POOL STATISTICS:\n");
		printf("net_pool_name = %s\n",p->net_pool_name);
		printf("Pool Buffer total and limits:\n");
		printf(" net_kmsg_size=%d net_kmsg_total=%d net_msg_max=%d\n",
			p->net_kmsg_size,p->net_kmsg_total,p->net_kmsg_max);
		printf("Kmsgs queued to be sent to user:\n");
		printf(" net_queue_high_size=%d net_queue_high_max=%d\n",
			p->net_queue_high_size,p->net_queue_high_max);
		printf(" net_queue_low_size=%d  net_queue_low_max=%d\n",
			p->net_queue_low_size,p->net_queue_low_max);
		printf("Receive port backlog:\n");
		printf(" net_kmsg_send_high_hits=%d net_kmsg_send_high_misses=%d\n",
			p->net_kmsg_send_high_hits,p->net_kmsg_send_high_misses);
		printf(" net_kmsg_send_low_hits=%d net_kmsg_send_low_misses=%d\n",
			p->net_kmsg_send_low_hits,p->net_kmsg_send_low_misses);
		printf("Kmsg free list Statistics:\n");
		printf(" net_queue_free_size=%d \n",p->net_queue_free_size);
		printf(" net_queue_free_min=%d net_queue_free_max=%d\n",
			p->net_queue_free_min,p->net_queue_free_max);
		printf(" net_queue_free_hits=%d net_queue_free_misses=%d\n",
			p->net_queue_free_hits,p->net_queue_free_misses);
		printf(" net_queue_free_steals=%d\n",p->net_queue_free_steals);
	}
}

void
db_net_filter_print(
	filter_t	*fp,
	filter_t	*fpe)
{
	unsigned	op;
	unsigned	arg;
	unsigned	value;

	value = 0;
	while (fp < fpe) {
		arg = *fp++;
		printf("\t0x%04x : ", arg);

		if (value) {
			value = 0;
			printf("<== value");
		} else {
			op = NETF_OP(arg);
			arg = NETF_ARG(arg);

			switch (arg) {
			case NETF_NOPUSH:
				break;
			case NETF_PUSHZERO:
				printf("NETF_PUSHZERO");
				break;
			case NETF_PUSHLIT:
				printf("NETF_PUSHLIT");
				value = 1;
				break;
			case NETF_PUSHIND:
				printf("NETF_PUSHIND");
				break;
			case NETF_PUSHHDRIND:
				printf("NETF_PUSHHDRIND");
				break;
			default:
				if (arg >= NETF_PUSHSTK)
					printf("NETF_PUSHSTK+%d",
					       arg - NETF_PUSHSTK);
				else if (arg >= NETF_PUSHHDR)
					printf("NETF_PUSHHDR+%d",
					       arg - NETF_PUSHHDR);
				else
					printf("NETF_PUSHWORD+%d",
					       arg - NETF_PUSHWORD);
				break;
			}

			if (op == NETF_OP(NETF_NOP)) {
				if (arg == NETF_NOPUSH)
					printf("NETF_NOP");
			} else {
				if (arg != NETF_NOPUSH)
					printf(" | ");

				switch (op) {
				case NETF_OP(NETF_AND):
					printf("NETF_AND");
					break;
				case NETF_OP(NETF_OR):
					printf("NETF_OR");
					break;
				case NETF_OP(NETF_XOR):
					printf("NETF_XOR");
					break;
				case NETF_OP(NETF_EQ):
					printf("NETF_EQ");
					break;
				case NETF_OP(NETF_NEQ):
					printf("NETF_NEQ");
					break;
				case NETF_OP(NETF_LT):
					printf("NETF_LT");
					break;
				case NETF_OP(NETF_LE):
					printf("NETF_LE");
					break;
				case NETF_OP(NETF_GT):
					printf("NETF_GT");
					break;
				case NETF_OP(NETF_GE):
					printf("NETF_GE");
					break;
				case NETF_OP(NETF_COR):
					printf("NETF_COR");
					break;
				case NETF_OP(NETF_CAND):
					printf("NETF_CAND");
					break;
				case NETF_OP(NETF_CNOR):
					printf("NETF_CNOR");
					break;
				case NETF_OP(NETF_CNAND):
					printf("NETF_CNAND");
					break;
				case NETF_OP(NETF_LSH):
					printf("NETF_LSH");
					break;
				case NETF_OP(NETF_RSH):
					printf("NETF_RSH");
					break;
				case NETF_OP(NETF_ADD):
					printf("NETF_ADD");
					break;
				case NETF_OP(NETF_SUB):
					printf("NETF_SUB");
					break;
				default:
					printf("<unknown>");
					break;
				}
			}
		}
		printf("\n");
	}
}
#endif	/* MACH_KDB */
