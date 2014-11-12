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
 * Revision 2.8  91/05/14  15:59:55  mrt
 * 	Correcting copyright
 * 
 * Revision 2.7  91/03/16  14:43:26  rpd
 * 	Added net_packet.
 * 	[91/01/14            rpd]
 * 
 * Revision 2.6  91/02/05  17:10:03  mrt
 * 	Changed to new Mach copyright
 * 	[91/01/31  17:30:15  mrt]
 * 
 * Revision 2.5  91/01/08  15:09:56  rpd
 * 	Replaced NET_KMSG_GET, NET_KMSG_FREE
 * 	with net_kmsg_get, net_kmsg_put, net_kmsg_collect.
 * 	[91/01/05            rpd]
 * 
 * Revision 2.4  90/06/02  14:48:20  rpd
 * 	Converted to new IPC.
 * 	[90/03/26  21:58:23  rpd]
 * 
 * Revision 2.3  90/02/22  20:02:26  dbg
 * 	kmsg->queue_head becomds kmsg->chain.
 * 	[90/01/25            dbg]
 * 
 * Revision 2.2  90/01/11  11:42:26  dbg
 * 	Make run in parallel.
 * 	[89/11/27            dbg]
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
 *	Date: 	ll/89
 */

#ifndef	_DEVICE_NET_IO_H_
#define	_DEVICE_NET_IO_H_

/*
 * Utilities for playing with network messages.
 */

#include <norma_ether.h>

#include <mach/machine/vm_types.h>
#include <mach/boolean.h>
#include <ipc/ipc_kmsg.h>

#include <kern/macro_help.h>
#include <kern/lock.h>
#include <kern/kalloc.h>

#include <device/net_status.h>
#include <device/if_hdr.h>

/*
 * A network packet is wrapped in a kernel message while in
 * the kernel.
 */

#define	net_kmsg(kmsg)		((net_rcv_msg_t)&(kmsg)->ikm_header)
#define	net_kmsg_sg(kmsg)	((net_rcv_msg_sg_t)&(kmsg)->ikm_header)

/*
 * typedef of machine dependent filter function.
 */
typedef int (*filter_fct_t)(char *, unsigned int, char *);

/* Buffer pool types */

typedef enum {
	NET_POOL_INLINE,	/* Inline packet data */
	NET_POOL_SG		/* Scatter/Gather list packet data */
} net_pool_type_t;

/*
 * Private Pool descriptor
 */
struct net_pool {
	struct net_pool	*net_pool_next;		/* next pool on list */
	int		net_pool_refcount;
	char		*net_pool_name;
	net_pool_type_t	net_pool_type;
	boolean_t	net_pool_sharable;	/* Attach possible? */
	vm_offset_t	net_pool_va;		/* VA for SG packet filter */
	vm_size_t	net_kmsg_size;		/* packet size */

	/*
	 * List of net kmsgs queued to be sent to users.
	 * Messages can be high priority or low priority.
	 * The network thread processes high priority messages first.
	 */
	decl_simple_lock_data(,net_queue_lock)
	struct ipc_kmsg_queue	net_queue_high;
	int		net_queue_high_size;
	int		net_queue_high_max;		/* for debugging */
	struct ipc_kmsg_queue	net_queue_low;
	int		net_queue_low_size;
	int		net_queue_low_max;		/* for debugging */

	/*
	 * List of net kmsgs that can be touched at interrupt level.
	 * If it is empty, we will also steal low priority messages.
	 */
	decl_simple_lock_data(,net_queue_free_lock)
	struct ipc_kmsg_queue	net_queue_free;
	int		net_queue_free_size;		/* on free list */
	int		net_queue_free_min;
	queue_head_t	net_queue_free_callback;	/* callback list */

	/*
	 * Pool Buffer total & limit
	 */
	decl_simple_lock_data(,net_kmsg_total_lock)
	int		net_kmsg_total;			/* total allocated */
	int		net_kmsg_max;			/* max possible */

	/*
	 * Statistics
	 */
	int		net_queue_free_max;		/* for debugging */
	int		net_queue_free_hits;		/* for debugging */
	int		net_queue_free_steals;		/* for debugging */
	int		net_queue_free_misses;		/* for debugging */

	int		net_kmsg_send_high_hits;	/* for debugging */
	int		net_kmsg_send_low_hits;		/* for debugging */
	int		net_kmsg_send_high_misses;	/* for debugging */
	int		net_kmsg_send_low_misses;	/* for debugging */
};

typedef struct net_pool *net_pool_t;	/* Handle to active buffer pool */

#define NET_POOL_NULL	((net_pool_t)0)

/*
 * Callback element. A callback element ows a porocedure and an argument
 * to be called by the net_thread. Usually, this is used when a pool is
 * getting out of available buffers, to refill a DMA controller with input
 * buffers.
 */
typedef struct net_callback {
	queue_chain_t	  ncb_chain;		/* callback chain */
	void		(*ncb_fct)(int);	/* callback function */
	int		  ncb_arg;		/* callback function argument */
} *net_callback_t;

/*
 * Multicast address element. Each owns 2 multicast addresses selecting all
 * multicast addresses between them. List is ordered by growing hash value.
 * Hash is positive for non-range elements and -1 otherwise.
 */
typedef struct net_multicast {
	queue_chain_t		chain;	/* list of hardware multicast addr */
	int			hash;	/* quick compare value */
	unsigned short		size;	/* element total size */
	unsigned short		len;	/* address length */
	/* unsigned char	from[];	   min address (variable length) */
	/* unsigned char	to[];	   max address (variable length) */
	/* unsigned char	misc[];	   additional data (variable length) */
} net_multicast_t;

#define	NET_MULTICAST_FROM(cur)		\
	((unsigned char *)(cur) + sizeof (net_multicast_t))

#define	NET_MULTICAST_TO(cur)		\
	((unsigned char *)(cur) + sizeof (net_multicast_t) + (cur)->len)

#define	NET_MULTICAST_MISC(cur)		\
	((unsigned char *)(cur) + sizeof (net_multicast_t) + 2 * (cur)->len)

#define	NET_MULTICAST_LENGTH(len)	\
	(sizeof (net_multicast_t) + 2 * (len))

/*
 * Interrupt routines may allocate and free net_kmsgs with these
 * functions.  net_kmsg_get may return IKM_NULL.
 */

extern ipc_kmsg_t	net_kmsg_get(void);

extern void		net_kmsg_put(
				ipc_kmsg_t	kmsg);

extern void		net_kmsg_collect(void);

extern void		net_kmsg_more(
				net_pool_t	pool);

/*
 * Network utility routines.
 */

extern void		net_io_init(void);

extern void		net_thread(void);

extern void		net_packet(
				struct ifnet	*ifp,
				ipc_kmsg_t	kmsg,
				unsigned int	count,
				boolean_t	pri,
				io_req_t	ior);

extern io_return_t	net_getstat(
				struct ifnet		* ifp,
				dev_flavor_t		flavor,
				dev_status_t		status,
				mach_msg_type_number_t	* count);

extern io_return_t	net_write(
				struct ifnet	*ifp,
				void		(*start)(int unit),
				io_req_t	ior);

extern io_return_t	net_set_filter(
				struct ifnet		*ifp,
				ipc_port_t		rcv,
				int			pri,
				filter_t		*filter,
				mach_msg_type_number_t	count,
				mach_device_t		device);

extern boolean_t	net_unset_filter(
				ipc_port_t	rcv_port);

extern boolean_t	ethernet_priority(
				ipc_kmsg_t	kmsg);

extern void		if_init_queues(
				struct ifnet	*ifp);

extern void		net_ast(void);

extern ipc_kmsg_t 	net_kmsg_alloc(void);

extern void		net_kmsg_free(
				 ipc_kmsg_t	kmsg);

#if	NORMA_ETHER
extern boolean_t	netipc_net_packet(
				ipc_kmsg_t	kmsg,
				unsigned long	count);
#endif	/* NORMA_ETHER */

extern filter_fct_t	net_filter_alloc(
				filter_t	*fpstart,
				unsigned int	fplen,
				unsigned int	*len);

extern void		net_filter_free(
				filter_fct_t	fp,
				unsigned int	len);

extern net_multicast_t	*net_multicast_match(
				queue_t		queue,
				unsigned char	*addr,
				unsigned	len);

extern void		net_multicast_create(
				net_multicast_t	*new,
				unsigned char	*from,
				unsigned char	*to,
				unsigned	len);

extern net_multicast_t	*net_multicast_remove(
				queue_t		queue,
				unsigned char	*from,
				unsigned char	*to,
				unsigned	len);

#define	net_multicast_insert(head, new)					\
MACRO_BEGIN								\
	net_multicast_t	*_cur;						\
	queue_iterate((head), _cur, net_multicast_t *, chain) {		\
		if (_cur->hash >= (new)->hash)				\
			break;						\
	}								\
	queue_insert_before((head), (new), _cur,			\
			    net_multicast_t *, chain);			\
MACRO_END

/*
 * Non-interrupt code may allocate and free net_kmsgs with these functions.
 */

extern vm_size_t net_kmsg_size;

/*
 * Create Pool - Create a buffer pool
 *
 * Possible errors
 *	KERN_RESOURCE_SHORTAGE - unable to allocate pool resources.
 */
kern_return_t
net_kmsg_create_pool(
	net_pool_type_t ptype,	       /* Pool type */
	vm_size_t	bufsize,       /* Total buffer size (in bytes) */
	int 		minbufs,       /* # of buffers in pool
					  (to start with) */
	boolean_t       sharable,      /* Does caller want to share
					  this pool */
	char 		*name,	       /* Name - Must be static! */
	net_pool_t      *pool_result); /* New Pool Handle (OUT arg) */

/*
 * Grow Pool - Add buffers to pool free list
 */
kern_return_t
net_kmsg_grow_pool(
	net_pool_t	pool,		/* Pool Handle */
	int		nbufs,		/* # of buffers to add to pool */
	int 		*newmax);	/* # of buffers in pool (OUT arg) */

/*
 * Shrink Pool - Removed buffers from pool free list
 *
 * Possible errors
 *	KERN_INVALID_ARGUMENT - removal exceeds pool total
 */
kern_return_t
net_kmsg_shrink_pool(
	net_pool_t 	pool,		/* Pool Handle */
	int 		nbufs,		/* # of buffers to remove from pool */
	int 		*newmax);	/* # of buffers in pool (OUT arg) */

/*
 * Collect Pool - Collect free queue pages from a net pool.
 */
void
net_kmsg_collect_pool(net_pool_t pool);	/* Pool Handle */

/*
 * Allocate/Free (possibly blocking)
 */

/*
 * Allocate Buffer - Allocate buffer from pool
 *
 * Possible errors
 *	IKM_NULL - pool free list empty and no more resources
 */
ipc_kmsg_t
net_kmsg_alloc_buf(
	net_pool_t pool);		/* Pool Handle */

/*
 * Free Buffer - Free buffer to pool
 */
kern_return_t
net_kmsg_free_buf(
	net_pool_t pool,		/* Pool Handle */
	ipc_kmsg_t kmsg);

/*
 * Add a free list callback function to a pool
 */
void
net_kmsg_callback(
	net_pool_t	pool,		/* Poll Handle */
	net_callback_t	callback);

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
	net_pool_t pool);		/* Pool Handle */

/*
 * Put Buffer - Put buffer back on pool free list
 */
void
net_kmsg_put_buf(
	net_pool_t pool,		/* Pool Handle */
	ipc_kmsg_t kmsg);

/*
 * Incoming packet.  Header has already been moved to proper place.
 * We are already at splimp.
 */
void
net_packet_pool(
	net_pool_t		pool,
	register struct ifnet	*ifp,
	register ipc_kmsg_t	kmsg,
	unsigned int		count,
	boolean_t		priority,
	io_req_t		ior);

/*
 * Set a filter for a network interface.
 *
 * We are given a naked send right for the rcv_port.
 * If we are successful, we must consume that right.
 */
io_return_t
net_set_filter_pool(
	net_pool_t	pool,
	struct ifnet	*ifp,
	ipc_port_t	rcv_port,
	int		priority,
	filter_t	*filter,
	mach_msg_type_number_t	filter_count,
	mach_device_t	device);

#endif	/* _DEVICE_NET_IO_H_ */
