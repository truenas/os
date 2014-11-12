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

#ifndef	_DEVICE_NET_DEVICE_H_
#define _DEVICE_NET_DEVICE_H_

#include <xkmachkernel.h>

#include <mach/boolean.h>
#include <mach/vm_types.h>
#include <ipc/ipc_kmsg.h>
#include <kern/lock.h>
#include <kern/queue.h>
#include <kern/zalloc.h>
#include <device/device_types.h>
#include <device/net_status.h>
#include <device/if_hdr.h>
#include <device/net_io.h>	/* for net_multicast_t */
#include <busses/iobus.h>

#if XKMACHKERNEL
#include <xkern/include/xtype.h>
#include <xkern/include/msg_s.h>
#include <uk_xkern/include/eth_support.h>
#endif /* XKMACHKERNEL */


#define	NET_DEVICE_NAME	"net"
#define	NET_DEVICE_POOL	"net-pagepool"
#define	NET_DEVICE_ZONE	"net-zone"

/*
 * Compute CRC32 of a network address
 */

#define	NET_COMPUTE_CRC32(addr, size, crc32)		\
MACRO_BEGIN						\
    unsigned int _i, _j;				\
    unsigned char _c;					\
    (crc32) = 0xFFFFFFFF;				\
    for (_i = 0; _i < (size); _i++) {			\
	_c = (addr)[i];					\
	for (_j = 0; _j < 8; _j++, _c >>= 1)		\
	    if ((_c & 0x01) ^ ((crc32) & 0x01))		\
		(crc32) = ((crc32) >> 1) ^ 0xEDB88320;	\
	    else					\
		(crc32) >>= 1;				\
    }							\
MACRO_END

/*
 * Definition of address types.
 */
#define	NET_PACKET_UNICAST	0x01	/* Unicast */
#define	NET_PACKET_MULTICAST	0x02	/* Multicast */
#define	NET_PACKET_BROADCAST	0x04	/* Broadcast */
#define	NET_PACKET_PERFECT	0x08	/* Perfect filtering */
#define	NET_PACKET_MASK		0x7	/* Address packet type mask */
#define	NET_PACKET_SHIFT	3	/* Address packet type shift */

/*
 * Definition of module sender/receiver
 */
#define	NET_MODULE_ANYSERVER	-4	/* Any MACH server module */
#define	NET_MODULE_KGDB		-3	/* KGDB module */
#define	NET_MODULE_XKERNEL	-2	/* X-Kernel module */
#define	NET_MODULE_UNKNOWN	-1	/* Unknown module */
#define	NET_MODULE_DEVICE	 0	/* MACH device module */

/*
 * typedefs
 */
typedef struct net_done *net_done_t;
typedef struct net_queue *net_queue_t;
typedef struct net_device *net_device_t;

/*
 * Output done queue element
 */
#if XKMACHKERNEL
struct net_xk_output;
#endif /* XKMACHKERNEL */

struct net_done {
    queue_chain_t	  netd_queue;		/* Next done element */
    void		(*netd_done)(		/* External done function */
	net_device_t,
	net_done_t);
    void		 *netd_external;	/* External area address */
    union {
#if XKMACHKERNEL
	struct net_xk_output	*netdu_xk;	/* Current output x-kernel */
#endif /* XKMACHKERNEL */
	io_req_t		 netdu_ior;	/* Current output io_req */
    }			  netd_union;
    int			  netd_module;		/* Packet module */
    unsigned int	  netd_ptype;		/* Packet address type */
    unsigned int	  netd_length;		/* Packet total length */
    unsigned int	  netd_outlen;		/* Packet output length */
    vm_offset_t		  netd_addr;		/* Packet output address */
};

#define	netd_ior	netd_union.netdu_ior
#if XKMACHKERNEL
#define	netd_xk		netd_union.netdu_xk

struct net_xk_output {
    Msg_s		xk_msg;		/* x-kernel buffer header */
    struct net_done	xk_done;	/* x-kernel if_done structure */
};
#endif /* XKMACHKERNEL */

/*
 * Output queue
 */
struct net_queue {
    queue_head_t	netq_head;	/* head of the queue */
    unsigned		netq_len;	/* length of queue */
    unsigned		netq_maxlen;	/* maximum length of queue */
    queue_entry_t	netq_wait;	/* first entry for which
					   a thread is waiting */
    decl_simple_lock_data(,netq_lock)	/* lock for queue and length */
};

#define	NET_QFULL(netq)	((netq)->netq_maxlen > 0 &&			\
			 (netq)->netq_len >= (netq)->netq_maxlen)

#define	NET_ENQUEUE(netq, netd, wait)					\
MACRO_BEGIN								\
    simple_lock(&(netq)->netq_lock); 					\
    enqueue_tail(&(netq)->netq_head, &(netd)->netd_queue); 		\
    (netq)->netq_len++; 						\
    if (wait) 								\
	while (NET_QFULL(netq)) { 					\
	    if ((netq)->netq_wait == (queue_entry_t)0)	 		\
		(netq)->netq_wait = &(netd)->netd_queue; 		\
	    thread_sleep_simple_lock((event_t)((netq)->netq_wait),	\
		   simple_lock_addr((netq)->netq_lock), FALSE); 	\
	    simple_lock(&(netq)->netq_lock); 				\
	} 								\
    simple_unlock(&(netq)->netq_lock); 					\
MACRO_END

#define	NET_PREPEND(netq, netd)						\
MACRO_BEGIN								\
    simple_lock(&(netq)->netq_lock);					\
    enqueue_head(&(netq)->netq_head, &(netd)->netd_queue);		\
    (netq)->netq_len++;							\
    simple_unlock(&(netq)->netq_lock);					\
MACRO_END

#define	NET_DEQUEUE(netq, netd)						\
MACRO_BEGIN								\
    simple_lock(&(netq)->netq_lock); 					\
    (netd) = (net_done_t)dequeue_head(&(netq)->netq_head);		\
    if ((netd) != (net_done_t)0) {					\
	(netq)->netq_len--;						\
	if ((netq)->netq_wait != (queue_entry_t)0) { 			\
	    assert((netq)->netq_maxlen > 0); 				\
	    if ((netq)->netq_len < (netq)->netq_maxlen) { 		\
		thread_wakeup((event_t)(netq)->netq_wait); 		\
		(netq)->netq_wait = queue_next((netq)->netq_wait);	\
		if (queue_end(&(netq)->netq_head, (netq)->netq_wait))	\
		    (netq)->netq_wait = (queue_entry_t)0; 		\
	    } 								\
	} 								\
    }									\
    simple_unlock(&(netq)->netq_lock); 					\
MACRO_END

#define	NETQ_INIT(netq, maxlen)						\
MACRO_BEGIN								\
    queue_init(&(netq)->netq_head);					\
    simple_lock_init(&(netq)->netq_lock, ETAP_NET_IFQ);			\
    (netq)->netq_len = 0;						\
    (netq)->netq_maxlen = (maxlen);					\
    (netq)->netq_wait = (queue_entry_t)0; 				\
MACRO_END

/*
 * BIC (Bus Interface Controller) operations description
 */
typedef enum net_bicin {	/* Returned code from bic_copyin */
    NET_BICIN_ERROR,		/* Copyin is in error condition */
    NET_BICIN_COMPLETED,	/* Copyin is completed */
    NET_BICIN_IN_PROGRESS	/* Copyin is in progress */
} net_bicin_t;

typedef enum net_bicout {	/* Returned code from bic_copyout */
    NET_BICOUT_ERROR,		/* Copyout in error condition */
    NET_BICOUT_COMPLETED,	/* Copyout is completed */
    NET_BICOUT_IN_PROGRESS	/* Copyout is in progress */
} net_bicout_t;

typedef enum net_bicintr {	/* Returned code from bic_intr */
    NET_BICINTR_ERROR,		/* BIC is in error condition */
    NET_BICINTR_EVENT,		/* BIC reported an event */
    NET_BICINTR_NOTHING		/* BIC reported no event */
} net_bicintr_t;

struct net_bic_ops {
    boolean_t	(*bic_reset)(		/* reset */
	net_device_t
	);
    boolean_t	(*bic_init)(		/* initialization */
	net_device_t
	);
    net_bicintr_t (*bic_intr)(		/* interrupt handler */
	net_device_t,
	boolean_t *,
	boolean_t *,
	boolean_t *
	);
    net_bicout_t (*bic_copyout)(	/* copyout */
	net_device_t,
	net_done_t,
	vm_offset_t,
	vm_size_t,
	unsigned int
	);
    net_bicin_t	(*bic_copyin)(		/* copyin */
	net_device_t,
	int,
	vm_offset_t,
	vm_offset_t,
	vm_size_t,
	boolean_t
	);
    io_return_t	(*bic_dinfo)(		/* dev_info */
	net_device_t,
	dev_flavor_t,
	char *
	);
};

#define	NET_BICOUT_FLAGS_VIRTUAL 0x01	/* Packet "from" address is virtual */
#define	NET_BICOUT_FLAGS_FIRST	 0x02	/* Packet first "from" address */
#define	NET_BICOUT_FLAGS_LAST	 0x04	/* Packet last "from" address */
#define	NET_BICOUT_FLAGS_PADDING 0x08	/* Packet padding */

/*
 * NIC (Network Interface Controller) operations description
 */
typedef enum net_nicin {	/* Returned code from nic_copyin */
    NET_NICIN_ERROR,		/* Copyin is in error condition */
    NET_NICIN_SUCCESS,		/* There is a packet to copyin */
    NET_NICIN_COMPLETED,	/* Copyin completed */
    NET_NICIN_EMPTY		/* No packet to copyin */
} net_nicin_t;

typedef enum net_nicout {	/* Returned code from nic_copyout */
    NET_NICOUT_ERROR,		/* Copyout in error condition */
    NET_NICOUT_SUCCESS,		/* Copyout accepted */
    NET_NICOUT_COMPLETED,	/* Copyout completed */
    NET_NICOUT_DELAYED		/* Delay copyout */
} net_nicout_t;

typedef enum net_nicintr {	/* Returned code from nic_intr */
    NET_NICINTR_ERROR,		/* NIC is in error condition */
    NET_NICINTR_EVENT,		/* NIC reported an event */
    NET_NICINTR_NOTHING		/* NIC reported no event */
} net_nicintr_t;

struct net_nic_ops {
    boolean_t	(*nic_reset)(		/* reset */
	net_device_t
	);
    boolean_t	(*nic_init)(		/* initialization */
	net_device_t
	);
    int		(*nic_start)(		/* start output */
	net_device_t,
	net_done_t
	);
    net_nicintr_t (*nic_intr)(		/* interrupt handler */
	net_device_t
	);
    int		(*nic_timeout)(		/* timeout */
	net_device_t
	);
    net_nicin_t	(*nic_copyin)(		/* where to copyin data from board */
	net_device_t,
	int *,
	vm_offset_t *,
	vm_size_t *,
	unsigned int *
	);
    net_nicout_t (*nic_copyout)(	/* where to copyout data to board */
	net_device_t,
	net_done_t
	);
    void	(*nic_multicast)(	/* Update multicast */
	net_device_t,
	net_multicast_t *
	);
    io_return_t	(*nic_dinfo)(		/* dev_info */
	net_device_t,
	dev_flavor_t,
	char *
	);
    void	(*nic_prepout)(		/* copyout prepare */
	net_device_t,
	net_done_t,
	vm_offset_t,
	vm_size_t,
	unsigned int
	);
    boolean_t	(*nic_prepin)(		/* copyin prepare */
	net_device_t,
	int,
	vm_offset_t,
	vm_offset_t,
	vm_offset_t
	);
};

/*
 * Network generic device management.
 * The net_if field is only used for packet filter management.
 */
struct net_device {
    struct ifnet	  net_if;	/* Old generic network interface */
    char		 *net_name;	/* Per-interface unit name */
    int			  net_unit;	/* Per-interface unit number */
    unsigned int	  net_generic;	/* Generic unit number */
    struct net_status	  net_status;	/* Interface status */
    void		 *net_address;	/* Pointer to hardware address */
    void		 *net_bic;	/* BIC structure */
    void		 *net_nic;	/* NIC structure */
    struct net_bic_ops	  net_bic_ops;	/* BIC operations */
    struct net_nic_ops	  net_nic_ops;	/* NIC operations */
    queue_head_t	  net_mqueue;	/* Multicast address list */
    unsigned		  net_msize[2];	/* Multicast private size */
    struct net_statistics net_genstat;	/* Generic statistics */
    unsigned int	  net_spfcount;	/* Specific statistics count */
    void		 *net_spfstat;	/* Specific statistics */
    mach_device_t	  net_device;	/* Associated device */
    struct net_pool	 *net_pool;	/* Input kmsg pool */
    unsigned int	  net_pool_min;	/* Minimum # of receive buffers */
    struct net_callback	  net_callback;	/* Free kmsg callback structure */
    mpqueue_head_t	  net_done;	/* Done queue for callback */
    struct net_queue	  net_head;	/* Mserver output queue */
    unsigned int	  net_ilen;	/* Input packet length */
    unsigned int	  net_ptype;	/* Input packet protocol type */
    unsigned int	  net_itype;	/* Input packet address type */
    net_done_t		  net_opacket;	/* Current output packet */
    unsigned int	  net_osize;	/* Extra output packet size */
    ipc_kmsg_t		  net_kmsg;	/* Current input kmsg */
    zone_t		  net_zone;	/* Zone for callback elements */
#if XKMACHKERNEL
    XObj		  net_xk_self;	/* x-kernel object associated */
    InputBuffer		 *net_xk_input;	/* x-kernel current input buf */
    InputPool		 *net_xk_pool;	/* x-kernel input buffer pool */
    unsigned short	  net_xk_low;	/* x-kernel lowest pkt type */
    unsigned short	  net_xk_high;	/* x-kernel highest pkt type */
    Allocator		  net_xk_mda;	/* x-kernel data allocator */
    unsigned int	  net_xk_olen;	/* x-kernel output len */
    struct net_queue	  net_xk_head;	/* x-kernel output queue */
#endif /* XKMACHKERNEL */
    IOBUS_LOCK_DECL(,net_dev_lock)	/* device ops mutex lock */
};

/*
 * Generic network board interface
 */
extern struct dev_ops	net_devops;

extern net_device_t	net_driver_allocate(char *,
					    int,
					    unsigned int
    );

extern void		net_driver_unprepare(net_device_t,
					     int,
					     vm_offset_t
    );

extern void		net_driver_alias(net_device_t
    );

extern void		net_driver_free(net_device_t,
					net_done_t
    );

extern void		net_driver_intr(int
    );

extern void		net_driver_timeout(int
    );

#if XKMACHKERNEL
extern xmsg_handle_t	xknet_push(XObj,
				   Msg
    );
#endif /* XKMACHKERNEL */

#endif /* _DEVICE_NET_DEVICE_H_ */
