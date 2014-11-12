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
 * XKERNEL TODO list
 *
 * 1) get rid of ior_xkernel_qhead in uk_xkern/framework/eth_support.[hc]
 * 2) Copy xk buffer to kmsg (if needed) inside an XK thread and not under
 *	IT.
 * 3) Rework net_driver_clone() to duplicate input packets to
 *	both x-kernel and device
 */

#include <xkmachkernel.h>
#include <xk_debug.h>
#include <mach_assert.h>

#include <device/conf.h>
#include <device/device_types.h>
#include <device/ds_routines.h>
#include <device/io_req.h>
#include <device/net_io.h>
#include <device/net_device.h>
#include <device/if_ether.h>
#include <device/if_myrinet.h>
#include <kern/assert.h>
#include <kern/kalloc.h>
#include <kern/macro_help.h>
#include <kern/zalloc.h>
#include <machine/endian.h>
#include <busses/iobus.h>
#include <types.h>

#if NET_XKMACHKERNEL
#include <xkern/protocols/eth/eth_i.h>
#include <xkern/include/xk_path.h>
#include <xkern/include/xk_debug.h>
#include <xkern/include/trace.h>
#include <uk_xkern/protocols.h>
#endif /* NET_XKMACHKERNEL */

/*
 * Management of all network devices
 */
net_device_t *net_board_addr;	/* Array of pointers to all network boards */
unsigned net_board_size;	/* Size of previous array */
unsigned net_board_last;	/* Last allocated index of previous array */

/*
 * validate netp or reject the write if the interface does not exist
 */
#define NET_VALIDATE_RET_NETP(netp, unit)		\
MACRO_BEGIN						\
    if ((unit) < 0 ||					\
	(unit) >= net_board_size ||			\
	net_board_addr[unit] == (net_device_t) 0)	\
	return (D_NO_SUCH_DEVICE);			\
    (netp) = net_board_addr[unit];			\
    assert(netp->net_status.index == unit);		\
MACRO_END

#if MACH_ASSERT
#define NET_VALIDATE_VOID_NETP(netp, unit, func)	\
MACRO_BEGIN						\
    assert((unit) >= 0 &&				\
	   (unit) < net_board_size &&			\
	   net_board_addr[unit]);			\
    netp = net_board_addr[unit];			\
    assert(netp->net_status.index == unit);		\
MACRO_END
#else /* MACH_ASSERT */
#define NET_VALIDATE_VOID_NETP(netp, unit, func)	\
MACRO_BEGIN						\
    if ((unit) < 0 ||					\
	(unit) >= net_board_size ||			\
	net_board_addr[unit] == (net_device_t)0)	\
	panic("%s: Incorrect unit number (%d)\n", func);\
    netp = net_board_addr[unit];			\
    assert(netp->net_status.index == unit);		\
MACRO_END
#endif /* MACH_ASSERT */

#if NET_XKMACHKERNEL
#if MACH_ASSERT
#define NET_VALIDATE_XK_NETP(netp, self, func)				\
MACRO_BEGIN								\
    assert((unsigned)self->state >= 0 &&				\
	   (unsigned)self->state < net_board_size &&			\
	   net_board_addr[(unsigned)self->state]);			\
    netp = net_board_addr[(unsigned)self->state];			\
    assert(netp->net_status.index == (unsigned)self->state);		\
MACRO_END
#else /* MACH_ASSERT */
#define NET_VALIDATE_XK_NETP(netp, self, func)				\
MACRO_BEGIN								\
    if ((unsigned)self->state < 0 ||					\
	(unsigned)self->state >= net_board_size ||			\
	net_board_addr[(unsigned)self->state] == (net_device_t *)0)	\
	panic("%s: Incorrect unit number (%d)\n", func);		\
    netp = net_board_addr[(unsigned)self->state];			\
    assert(netp->net_status.index == (unsigned)self->state);		\
MACRO_END
#endif /* MACH_ASSERT */

/*
 * Input x-kernel buffer
 *
 * No conditional locking here: if we execute this code,
 * it means that eth_resources succeded. Hence, no one
 * can else could sneak in and grab this lock.
 */
#define NET_INPUT_XKERNEL(pool, buf, len)	\
MACRO_BEGIN					\
    (buf)->len = (len);				\
    simple_lock(&(pool)->iplock);		\
    queue_enter(&(pool)->xkIncomingData, (buf),	\
		InputBuffer *, link);		\
    if (more_input_thread(pool))		\
	thread_wakeup_one((event_t)(pool));	\
    simple_unlock(&(pool)->iplock);		\
MACRO_END

/*
 * When used with promiscous traffic,
 * x-kernel traffic is identified by
 * the following interval.
 */
#define XK_LOWEST_TYPE  0x3000
#define XK_HIGHEST_TYPE 0x3FFF

#if     XK_DEBUG
int     tracexknetp;
#endif /* XK_DEBUG */

#endif /* NET_XKMACHKERNEL */

/*
 * Alias device names management
 */
static unsigned net_name_alias_lan[] = {	/* "lan" alias (for HP/UX) */
    NET_MEDIA_ETHERNET,
    NET_MEDIA_FDDI,
    NET_MEDIA_NONE
};

static unsigned net_name_alias_eth[] = {	/* "eth" alias (for Linux) */
    NET_MEDIA_ETHERNET,
    NET_MEDIA_NONE
};

struct net_name_alias {
    char		*nna_name;		/* indirect device name */
    unsigned int	*nna_media;		/* media types associated */
    unsigned int	 nna_unit;		/* last unit number */
} net_name_alias[] = {
    { "lan",	net_name_alias_lan	},	/* "lan" alias (for HP/UX) */
    { "eth",	net_name_alias_eth	},	/* "eth" alias (for Linux) */
    { (char *)0 }
};

/*
 * Forward declaration
 */
boolean_t
net_driver_prepare(net_device_t,
		   int
    );

void
net_driver_setup(net_device_t
    );

void
net_driver_callback(int
    );

io_return_t
net_driver_reset(net_device_t
    );

io_return_t
net_driver_init(net_device_t
    );

unsigned int
net_driver_addrtype(net_device_t,
		    char *,
		    unsigned int
    );

void
net_driver_input(net_device_t,
		 net_done_t
    );

void
net_driver_clone(net_device_t
    );

void
net_driver_loopback(net_device_t,
		    net_done_t
    );

boolean_t
net_driver_copyout(net_device_t,
		   net_done_t
    );

boolean_t
net_driver_copyin(net_device_t,
		  vm_offset_t
    );

boolean_t
net_driver_copy(net_device_t,
		boolean_t *
    );

void
net_driver_event(net_device_t
    );

void
net_driver_start(int
    );

io_return_t
net_devops_open(dev_t,
		dev_mode_t,
		io_req_t
    );

void
net_devops_close(dev_t
    );

void
net_devops_reset(dev_t
    );

io_return_t
net_devops_write(dev_t,
		 io_req_t
    );

io_return_t
net_devops_getstat(dev_t,
		   dev_flavor_t,
		   dev_status_t,
		   mach_msg_type_number_t *
    );

io_return_t
net_devops_setstat(dev_t,
		   dev_flavor_t,
		   dev_status_t,
		   mach_msg_type_number_t
    );

io_return_t
net_devops_async_in(dev_t,
		    ipc_port_t,
		    int,
		    filter_t *,
		    mach_msg_type_number_t,
		    mach_device_t
    );

io_return_t
net_devops_dinfo(dev_t,
		 dev_flavor_t,
		 char *
    );

#if NET_XKMACHKERNEL
InputBuffer *
net_driver_xkalloc(net_device_t,
		   InputPool **
    );

xmsg_handle_t
xknet_push(XObj,
	   Msg
    );

int
xknet_control(XObj,
	      int,
	      char *,
	      int
    );

xkern_return_t
xknet_openenable(XObj, 
		 XObj, 
		 XObj,
		 Part
    );

boolean_t
xknet_copyout(char *,
	      long,
	      void *
    );

boolean_t
xknet_usrcopyout(char *,
		 long,
		 void *
    );

boolean_t
xknet_copyin(char *,
	     long,
	     void *
    );

boolean_t
xknet_usrcopyin(char *,
		long,
		void *
    );

boolean_t
xknet_prepout(char *,
	      long,
	      void *
    );

boolean_t
xknet_usrprepout(char *,
		 long,
		 void *
    );
#endif /* NET_XKMACHKERNEL */

/*
 * Network generic device operations
 */
struct dev_ops net_devops = {
    NET_DEVICE_NAME,		/* name for major device */
    net_devops_open,		/* open */
    net_devops_close,		/* close */
    NULL_READ,			/* read */
    net_devops_write,		/* write */
    net_devops_getstat,		/* get status/control */
    net_devops_setstat,		/* set status/control */
    NULL_MMAP,			/* map memory */
    net_devops_async_in,	/* asynchronous input setup */
    net_devops_reset,		/* reset */
    NULL_DEATH,			/* clean up reply ports */
    0,				/* sub-devices per unit */
    net_devops_dinfo		/* driver info for kernel */
};

/*
 * net_driver_allocate
 *
 * Purpose : Allocate a net_device structure
 * Returns : net_device structure allocated
 *
 * IN_arg  : name    ==> interface name
 *	     in_unit ==> local driver unit number (-1: look for a valid unit)
 *	     maxlen  ==> maximum length of the output queues
 */
net_device_t
net_driver_allocate(
    char *name,
    int in_unit,
    unsigned int maxlen)
{
    net_device_t *root;
    net_device_t netp;
    unsigned i;
    char *p;
    char *q;
    unsigned int out_unit;

    if (in_unit >= 0) {
	/*
	 * Look for another entry of the same name in the interface array
	 */
	for (i = 0; i < net_board_size; i++) {
	    p = net_board_addr[i]->net_name;
	    q = name;
	    while (*p == *q) {
		if (*p == '\0' && net_board_addr[i]->net_unit == in_unit)
		    return ((net_device_t)0);
		p++;
		q++;
	    }
	}
    } else {
	/*
	 * Look for the next unit for the same name
	 */
	in_unit = 0;
	for (;;) {
	    for (i = 0; i < net_board_size; i++) {
		for (p = net_board_addr[i]->net_name, q = name;
		     *p != '\0' && *p == *q; p++, q++)
		    continue;
		if (*p == *q && net_board_addr[i]->net_unit == in_unit)
		    break;
	    }
	    if (i == net_board_size)
		break;
	    in_unit++;
	}
    }

    /*
     * Allocate an entry in the board array
     */
    root = (net_device_t *)kalloc(sizeof (net_device_t) * (net_board_size + 1));
    for (i = 0; i < net_board_size; i++)
	root[i] = net_board_addr[i];
    if (net_board_addr != (net_device_t *)0)
	kfree((vm_offset_t)net_board_addr, net_board_size);
    out_unit = (int)net_board_size++;
    net_board_addr = root;

    /*
     * Allocate a net_device structure
     */
    netp = net_board_addr[out_unit] =
	(net_device_t)kalloc(sizeof (struct net_device));
    bzero((char *)netp, sizeof (*netp));

    /*
     * Initialize the net_device structure
     */
    netp->net_name = name;
    netp->net_unit = in_unit;
    netp->net_generic = out_unit;
    NETQ_INIT(&netp->net_head, maxlen);
    queue_init(&netp->net_mqueue);
    mpqueue_init(&netp->net_done);
    IOBUS_LOCK_INIT(&netp->net_dev_lock, out_unit, net_driver_start,
		    net_driver_intr, net_driver_timeout, net_driver_callback);
    netp->net_callback.ncb_fct = (void (*)(int))net_driver_callback;
    netp->net_callback.ncb_arg = out_unit;

    /*
     * Initialize the ifnet structure
     */
    queue_init(&netp->net_if.if_rcv_port_list);
    mutex_init(&netp->net_if.if_rcv_port_list_lock, ETAP_NET_RCV_PLIST);

    return (netp);
}

/*
 * net_driver_alias
 *
 * Purpose : Allocate alias of an net_device structure
 * Returns : Nothing
 *
 * IN_arg  : netp ==> network generic structure
 */
void
net_driver_alias(
    net_device_t netp)
{
    char *name;
    int unit;
    unsigned i;
    unsigned l;
    char *pp;
    char *p;
    char *q;
    struct net_name_alias *np;
    unsigned *ni;

    /*
     * Print and set up indirect links for this network device
     */
    printf(" (aliased to ");
    name = netp->net_name;
    unit = netp->net_unit;

    np = net_name_alias;
    for (;;) {
	l = 2;
	p = name;
	while (*p++)
	    l++;
	for (i = unit; i / 10; i /= 10)
	    l++;
	p = pp = (char *)kalloc(l);
	q = name;
	while (*p++ = *q++)
	    continue;
	p = &pp[l - 1];
	*p-- = '\0';
	for (i = unit; ; i /= 10) {
	    *p-- = (i % 10) + '0';
	    if (i == 0)
		break;
	}
	printf(pp);
	dev_set_indirection(pp, &net_devops, netp->net_generic);
	kfree((vm_offset_t)pp, l);

	while (np->nna_name != (char *)0) {
	    for (ni = np->nna_media; *ni != NET_MEDIA_NONE; ni++)
		if (*ni == netp->net_status.media_type)
		    break;
	    if (ni != NET_MEDIA_NONE) {
		for (p = np->nna_name, q = netp->net_name;
		     *p != '\0' && *p == *q; p++, q++)
		    continue;
		if (*p != *q) {
		    name = np->nna_name;
		    unit = np->nna_unit++;
		    printf(", ");
		    break;
		}
	    }
	    np++;
	}
	if (np->nna_name == (char *)0)
	    break;
	np++;
    }
    printf(")");
}

/*
 * net_driver_free
 *
 * purpose : Output packet free
 * Returns : Nothing
 *
 * In_arg  : netd ==> Output packet to free
 */
void
net_driver_free(
    net_device_t netp,
    net_done_t netd)
{
    if (netd->netd_done != (void (*)(net_device_t, net_done_t))0)
	(*netd->netd_done)(netp, netd);

    switch (netd->netd_module) {
    default:
	assert(netd->netd_module == NET_MODULE_KGDB);
	break;
#if NET_XKMACHKERNEL
    case NET_MODULE_XKERNEL:
	pathFree(netd->netd_xk);
	break;
#endif /* NET_XKMACHKERNEL */
    case NET_MODULE_DEVICE:
	iodone(netd->netd_ior);
	zfree(netp->net_zone, (vm_offset_t)netd);
	break;
    }
}

/*
 * net_driver_unprepare
 *
 * Purpose : Free already prepared DMA buffers
 * Returns : Nothing
 *
 * IN_args : netp   ==> Network generic structure
 * 	     module ==> Module the DMA buffer was prepared for
 *	     addr   ==> Prepared DMA buffer
 */
void
net_driver_unprepare(
    net_device_t netp,
    int module,
    vm_offset_t addr)
{
    assert((netp->net_status.flags & NET_STATUS_SG) == 0);
    net_kmsg_put_buf(netp->net_pool, (ipc_kmsg_t)addr);
}

/*
 * net_driver_prepare
 *
 * Purpose : Add new copyin DMA buffer
 * Returns : TRUE/FALSE whether there are still available buffers in the pool
 *
 * IN_args : netp   ==> Network generic structure
 *	     module ==> Module to prepare DMA buffer for
 */
boolean_t
net_driver_prepare(
    net_device_t netp,
    int module)
{
    ipc_kmsg_t kmsg;

    if (netp->net_status.flags & NET_STATUS_SG)
	panic("%s: Scatter/Gather interface not yet supported\n",
	      "net_driver_prepare");

#if NET_XKMACHKERNEL
    if (module == NET_MODULE_XKERNEL)
	return (TRUE);
#endif /* NET_XKMACHKERNEL */

    for (;;) {
	kmsg = net_kmsg_get_buf(netp->net_pool);
	if (kmsg == IKM_NULL)
	    break;
	if (!(*netp->net_nic_ops.nic_prepin)
	    (netp, module, (vm_offset_t)kmsg,
	     (vm_offset_t)net_kmsg(kmsg)->header,
	     (vm_offset_t)net_kmsg(kmsg)->packet +
	     sizeof (struct packet_header))) {
	    net_kmsg_put_buf(netp->net_pool, kmsg);
	    return (TRUE);
	}
    }
    return (FALSE);
}

/*
 * net_driver_setup
 *
 * Purpose : Setup more input DMA buffers for all modules
 * Returns : Nothing
 *
 * IN_arg  : netp ==> network generic structure
 */
void
net_driver_setup(
    net_device_t netp)
{
    if (
#if NET_XKMACHKERNEL
	net_driver_prepare(netp, NET_MODULE_XKERNEL) &&
#endif /* NET_XKMACHKERNEL */
	net_driver_prepare(netp, NET_MODULE_DEVICE)) {
	if (netp->net_status.flags & NET_STATUS_CALLBACK)
	    netp->net_status.flags &= ~NET_STATUS_CALLBACK;
    } else {
	if ((netp->net_status.flags & NET_STATUS_CALLBACK) == 0)
	    netp->net_status.flags |= NET_STATUS_CALLBACK;
	net_kmsg_callback(netp->net_pool, &netp->net_callback);
    }
}

/*
 * net_driver_callback
 *
 * Purpose : Callback routine called when more free buffers are available
 * Returns : Nothing
 *
 * IN_arg  : unit ==> network generic unit number
 */
void
net_driver_callback(
    int unit)
{
    net_device_t netp;
    spl_t spl;
    IOBUS_LOCK_STATE();

    NET_VALIDATE_VOID_NETP(netp, unit, "net_driver_callback");

    spl = splimp();
    if (IOBUS_LOCK(&netp->net_dev_lock, IOBUS_LOCK_CALLB)) {
	net_driver_setup(netp);
	IOBUS_UNLOCK(&netp->net_dev_lock);
    }
    splx(spl);
}

/*
 * net_driver_reset
 *
 * Purpose : network generic driver reset
 * Returns : Device return value
 *
 * IN_arg  : netp ==> network generic structure
 */
io_return_t
net_driver_reset(
    net_device_t netp)
{
    net_done_t netd;
    ipc_kmsg_t kmsg;

    /*
     * Reset BIC
     */
    netp->net_status.flags &= ~NET_STATUS_RUNNING;
    if (netp->net_bic_ops.bic_reset && !(*netp->net_bic_ops.bic_reset)(netp)) {
	printf("%s: Cannot reset BIC on board %s%d\n",
	       "net_driver_reset", netp->net_name, netp->net_unit);
	return (D_IO_ERROR);
    }

    /*
     * Reset NIC
     */
    if (netp->net_nic_ops.nic_reset && !(*netp->net_nic_ops.nic_reset)(netp)) {
	printf("%s: Cannot reset NIC on board %s%d\n",
	       "net_driver_reset", netp->net_name, netp->net_unit);
	return (D_IO_ERROR);
    }

    /*
     * Reset generic network interface
     */
    if (netp->net_kmsg != (ipc_kmsg_t)0) {
	net_kmsg_put_buf(netp->net_pool, netp->net_kmsg);
	netp->net_kmsg = (ipc_kmsg_t)0;
    }

    /*
     * Free current output packet callback
     */
    if ((netd = netp->net_opacket) != (net_done_t)0) {
	net_driver_free(netp, netd);
	netp->net_opacket = (net_done_t)0;
    }

#if NET_XKMACHKERNEL
    if (netp->net_xk_input != (InputBuffer *)0) {
	/*
	 * No conditional locking here: if we execute this code,
	 * it means that we already got the lock. Hence, no one
	 * can else could sneak in and grab this lock.
	 */
	simple_lock(&netp->net_xk_pool->iplock);
	queue_enter(&netp->net_xk_pool->xkBufferPool,
		    netp->net_xk_input, InputBuffer *, link);
	simple_unlock(&netp->net_xk_pool->iplock);
	netp->net_xk_input = (InputBuffer *)0;
    }
#endif /* NET_XKMACHKERNEL */

    return (D_SUCCESS);
}

/*
 * net_driver_init
 *
 * Purpose : network generic driver initialization
 * Returns : Device return value
 *
 * IN_arg  : netp ==> network generic structure
 */
io_return_t
net_driver_init(
    net_device_t netp)
{
    /*
     * Initialize BIC
     */
    if (netp->net_bic_ops.bic_init && !(*netp->net_bic_ops.bic_init)(netp)) {
	printf("%s: Cannot init BIC on board %s%d\n",
	       "net_driver_init", netp->net_name, netp->net_unit);
	return (D_IO_ERROR);
    }

    /*
     * Initialize NIC
     */
    if (netp->net_nic_ops.nic_init && !(*netp->net_nic_ops.nic_init)(netp)) {
	printf("%s: Cannot init NIC on board %s%d\n",
	       "net_driver_init", netp->net_name, netp->net_unit);
	return (D_IO_ERROR);
    }

    /*
     * Populate copyin DMA buffers
     */
    if (netp->net_nic_ops.nic_prepin &&
	(netp->net_status.flags & (NET_STATUS_CALLBACK |
				   NET_STATUS_RUNNING)) == NET_STATUS_RUNNING)
	net_driver_setup(netp);
    return (D_SUCCESS);
}

/*
 * net_driver_addrtype
 *
 * Purpose : network generic driver address type define
 * Returns : packet type
 *
 * IN_arg  : netp ==> network generic structure
 *	     addr ==> packet to test
 *	     type ==> initial packet type
 */
unsigned int
net_driver_addrtype(
    net_device_t netp,
    char *addr,
    unsigned int type)
{
    unsigned i;

    if (type & NET_PACKET_PERFECT)
	switch (type & NET_PACKET_MASK) {
	case NET_PACKET_UNICAST:
	case NET_PACKET_MULTICAST:
	case NET_PACKET_BROADCAST:
	    return (type & NET_PACKET_MASK);
	}

    switch (netp->net_status.header_format) {
    case HDR_ETHERNET:
    {
	struct ether_header *eth;

	switch (netp->net_status.media_type) {
	case NET_MEDIA_OTHERS:
	    if (netp->net_status.header_size !=
		sizeof (struct if_myrinet_eep))
		return (0);
	    eth = (struct ether_header *)(addr + netp->net_status.header_size -
					  sizeof (struct ether_header));
	    break;

	case NET_MEDIA_ETHERNET:
	    eth = (struct ether_header *)addr;
	    break;

	default:
	    return (0);
	}

	if ((eth->ether_dhost[0] & 1) == 0)
	    return (NET_PACKET_UNICAST);

	for (i = 0; i < netp->net_status.address_size; i++)
	    if (eth->ether_dhost[i] != 0xFF)
		return (NET_PACKET_MULTICAST);
	return (NET_PACKET_BROADCAST);
    }
    default:
	return (0);
    }
}
 
/*
 * net_driver_input
 *
 * Purpose : network generic driver input packet
 * Returns : Nothing
 *
 * IN_arg  : netp ==> network generic structure
 *	     netd ==> output I/O request to loopback
 */
void
net_driver_input(
    net_device_t netp,
    net_done_t netd)
{
    boolean_t priority;
    io_req_t ior;

    switch (netp->net_itype) {
    case NET_PACKET_BROADCAST:
	    priority = FALSE;
	    netp->net_genstat.in_pkt_broadcast++;
	    break;

    case NET_PACKET_MULTICAST:
	    priority = FALSE;
	    netp->net_genstat.in_pkt_multicast++;
	    break;

    case NET_PACKET_UNICAST:
	    priority = TRUE;
	    netp->net_genstat.in_pkt_unicast++;
	    break;

    default:
	panic("%s: Unsupported packet type (0x%x)\n",
	      "net_driver_input", netp->net_itype);
    }
    netp->net_genstat.in_bytes += netp->net_ilen;

#if NET_XKMACHKERNEL
    if (netp->net_xk_input != (InputBuffer *)0) {
	if (netp->net_kmsg != (ipc_kmsg_t)0) {
	    /*
	     * It is assumed here that the header has already been copied
	     */
	    bcopy(netp->net_xk_input->data,
		  (char *)(net_kmsg(netp->net_kmsg)->packet +
			   sizeof (struct packet_header)),
		  netp->net_ilen);
	}

	/*
	 * No conditional locking here: if we execute this code,
	 * it means that we already got the lock. Hence, no one
	 * can else could sneak in and grab this lock.
	 */
	netp->net_xk_input->len = netp->net_ilen;
	simple_lock(&netp->net_xk_pool->iplock);
	queue_enter(&netp->net_xk_pool->xkIncomingData,
		    netp->net_xk_input, InputBuffer *, link);
	if (more_input_thread(netp->net_xk_pool))
	    thread_wakeup_one((event_t)(netp->net_xk_pool));
	simple_unlock(&netp->net_xk_pool->iplock);
	netp->net_xk_input = (InputBuffer *)0;
    }
#endif /* NET_XKMACHKERNEL */

    if (netp->net_kmsg != (ipc_kmsg_t)0) {
	struct packet_header *pkt =
	    (struct packet_header *)net_kmsg(netp->net_kmsg)->packet;
	pkt->type = netp->net_ptype;
	pkt->length = netp->net_ilen + sizeof (struct packet_header);

	if (netd == (net_done_t)0) {
	    netp->net_kmsg->ikm_header.msgh_id = NET_RCV_MSG_ID;
	    ior = (io_req_t)0;
	} else {
	    ior = (netd->netd_module == NET_MODULE_DEVICE ? 
		   netd->netd_ior : (io_req_t)0);
	    if (netp->net_status.flags & NET_STATUS_PROMISC)
		netp->net_kmsg->ikm_header.msgh_id = PROMISC_RCV_MSG_ID;
	    else
		switch (netd->netd_module) {
		default:
		    assert (netd->netd_module == NET_MODULE_DEVICE);
		    netp->net_kmsg->ikm_header.msgh_id = NET_RCV_MSG_ID;
		    break;
#if NET_XKMACHKERNEL
		case NET_MODULE_XKERNEL:
		    netp->net_kmsg->ikm_header.msgh_id = NORMA_RCV_MSG_ID;
		    break;
#endif /* NET_XKMACHKERNEL */
		case NET_MODULE_KGDB:
		    netp->net_kmsg->ikm_header.msgh_id = NET_RCV_MSG_ID;
		    break;
		}
	}
	net_packet_pool(netp->net_pool, &netp->net_if,
			netp->net_kmsg, pkt->length, priority, ior);
	netp->net_kmsg = (ipc_kmsg_t)0;
    }
}

/*
 * net_driver_clone
 *
 * Purpose : Allocate x-kernel buffer and/or ipc kernel message
 * Returns : Nothing
 *
 * In_arg  : netp ==> network generic structure
 */
void
net_driver_clone(
    net_device_t netp)
{
    assert(netp->net_kmsg == (ipc_kmsg_t)0);
#if NET_XKMACHKERNEL
    assert (netp->net_xk_input == (InputBuffer *)0);

    if (netp->net_ptype >= netp->net_xk_low &&
	netp->net_ptype < netp->net_xk_high) {
	/*
	 * Allocate an x-kernel input buffer
	 */
	netp->net_xk_input = net_driver_xkalloc(netp, &netp->net_xk_pool);
	if ((netp->net_status.flags & NET_STATUS_PROMISC) == 0)
	    return;
    } else {
	switch (netp->net_ptype) {
	case ETHERTYPE_ARP:
	case ETHERTYPE_RARP:
	    /*
	     * Allocate an x-kernel input buffer
	     */
	    netp->net_xk_input = net_driver_xkalloc(netp, &netp->net_xk_pool);
	    break;
	default:
	    break;
	}
    }
#endif /* NET_XKMACHKERNEL */

    /*
     * Allocate an ipc kernel input message
     */
    netp->net_kmsg = net_kmsg_get_buf(netp->net_pool);
}

/*
 * net_driver_loopback
 *
 * Purpose : network generic driver output packet loop back
 * Returns : Nothing
 *
 * IN_arg  : netp ==> network generic structure
 *	     netd ==> output packet to loopback
 *
 * Comment : Loopback Network packet if :
 *		1) The interface is in promiscuous mode
 *		2) It's a broadcast address
 *		3) The device has been opened more than once (multi-server)
 *			and the physical destination address is mine
 *		4) It's a multicast address and the destination address is
 *			in the local set of the accepted multicast addresses.
 */
void
net_driver_loopback(
    net_device_t netp,
    net_done_t netd)
{
    unsigned short type;
    unsigned int i;
    char *addr;

    switch (netp->net_status.media_type) {
    case NET_MEDIA_OTHERS:
	if (netp->net_status.header_size != sizeof (struct if_myrinet_eep))
	    panic("%s: Unsupported media type (header length = %d)\n",
		  "net_driver_loopback", netp->net_status.header_size);
	/*FALL THRU*/

    case NET_MEDIA_ETHERNET:
    {
	struct ether_header *eth;
	switch (netd->netd_module) {
	default:
	    assert(netd->netd_module == NET_MODULE_KGDB);
	    return;
#if NET_XKMACHKERNEL
	case NET_MODULE_XKERNEL:
	    addr = (char *)msgGetAttr(&netd->netd_xk->xk_msg, 0);
	    break;
#endif /* NET_XKMACHKERNEL */
	case NET_MODULE_DEVICE:
	    addr = (char *)netd->netd_ior->io_data;
	    break;
	}

	/*
	 * Ethernet header may be extended (like for MYRINET)
	 */
	eth = (struct ether_header *)(addr + netp->net_status.header_size -
				      sizeof (struct ether_header));

	/*
	 * Set up input packet type
	 */
	if (netp->net_status.header_format == HDR_ETHERNET)
	    type = ntohs(eth->ether_type);
	else
	    panic("%s: Unsupported header format %d for media 0x%x\n",
		  "net_driver_loopback",
		  netp->net_status.header_format, NET_MEDIA_ETHERNET);

	/*
	 * Test case 1
	 */
	if (netp->net_status.flags & NET_STATUS_PROMISC)
	    break;

	/*
	 * Test case 2
	 */
	for (i = 0; i < netp->net_status.address_size; i++)
	    if (eth->ether_dhost[i] != 0xFF)
		break;
	if (i == netp->net_status.address_size)
	    break;

	/*
	 * Test case 3
	 */
	if (netp->net_device != (mach_device_t)0 &&
#if NET_XKMACHKERNEL
	    (netp->net_status.flags & NET_STATUS_XK) ? 1 : 0 +
#endif /* NET_XKMACHKERNEL */
	    netp->net_device->open_count > 1) {
	    for (i = 0; i < netp->net_status.address_size; i++)
		if (eth->ether_dhost[i] !=
		    ((unsigned char *)netp->net_address)[i])
		    break;
	    if (i == netp->net_status.address_size)
		break;
	}

	/*
	 * Test case 4
	 */
	if ((eth->ether_dhost[0] & 1) &&
	    net_multicast_match(&netp->net_mqueue,
				eth->ether_dhost, netp->net_status.media_type))
	    break;

	/*
	 * No loopback is needed for this packet
	 */
	return;
    }

    default:
	panic("%s: Unsupported media type %d\n",
	      "net_driver_loopback", netp->net_status.media_type);
    }

    /*
     * Allocate needed x-kernel buffer and/or ipc kernel message
     */
    netp->net_ptype = type;
    net_driver_clone(netp);

#if NET_XKMACHKERNEL
    if (netp->net_xk_input != (InputBuffer *)0) {
	/*
	 * Copy header and data into the x-kernel buffer and send it
	 */
	switch (netd->netd_module) {
	case NET_MODULE_XKERNEL:
	    /*
	     * Copy header and data of an output x-kernel message
	     */
	    bcopy(msgGetAttr(&netd->netd_xk->xk_msg, 0),
		  netp->net_xk_input->hdr, netp->net_status.header_size);
	    if (netp->net_kmsg != (ipc_kmsg_t)0) {
		/*
		 * If an input kmsg exists, copy header while in the cache
		 * Data copy into kmsg is delayed until net_driver_input call
		 */
		bcopy(netp->net_xk_input->hdr,
		      net_kmsg(netp->net_kmsg)->header,
		      netp->net_status.header_size);
	    }
	    msgForEachAlternate(&netd->netd_xk->xk_msg,
				xknet_copyin, xknet_usrcopyin, netp);
	    netp->net_ilen = msgLen(&netd->netd_xk->xk_msg);
	    if (netp->net_ilen < (netp->net_status.min_packet_size -
				  netp->net_status.header_size)) {
		bzero(&netp->net_xk_input->data[netp->net_ilen],
		      (netp->net_status.min_packet_size -
		       netp->net_status.header_size - netp->net_ilen));
		netp->net_ilen = netp->net_status.min_packet_size;
	    } else
		netp->net_ilen += netp->net_status.header_size;
	    break;

	case NET_MODULE_DEVICE:
	    /*
	     * Copy header and data of an output device I/O request
	     */
	    bcopy(netd->netd_ior->io_data,
		  netp->net_xk_input->hdr, netp->net_status.header_size);
	    if (netp->net_kmsg != (ipc_kmsg_t)0) {
		/*
		 * If an input kmsg exists, copy header while in the cache
		 * Data copy into kmsg is delayed until net_driver_input call
		 */
		bcopy(netp->net_xk_input->hdr,
		      net_kmsg(netp->net_kmsg)->header,
		      netp->net_status.header_size);
	    }
	    bcopy(netd->netd_ior->io_data + netp->net_status.header_size,
		  netp->net_xk_input->data,
		  netd->netd_ior->io_count - netp->net_status.header_size);
	    if (netd->netd_ior->io_count < netp->net_status.min_packet_size) {
		bzero(netp->net_xk_input->data +
		      netd->netd_ior->io_count - netp->net_status.header_size,
		      netp->net_status.min_packet_size -
		      netd->netd_ior->io_count);
		netp->net_ilen = (netp->net_status.min_packet_size -
				  netp->net_status.header_size);
	    } else
		netp->net_ilen =
		    netd->netd_ior->io_count - netp->net_status.header_size;
	    break;

	case NET_MODULE_KGDB:
	    break;
	}

	netp->net_itype = netd->netd_ptype;
	net_driver_input(netp, netd);
	return;
    }
#endif /* NET_XKMACHKERNEL */

    if (netp->net_kmsg == (ipc_kmsg_t)0)
	return;

    /*
     * Copy header and data into the ipc kernel message and send it
     */
    switch (netd->netd_module) {
    case NET_MODULE_KGDB:
	break;

#if NET_XKMACHKERNEL
    case NET_MODULE_XKERNEL:
	/*
	 * Copy header and data of an output x-kernel message
	 */
	bcopy(msgGetAttr(&netd->netd_xk->xk_msg, 0),
	      net_kmsg(netp->net_kmsg)->header, netp->net_status.header_size);
	netp->net_ilen = 0;
	msgForEachAlternate(&netd->netd_xk->xk_msg,
			    xknet_copyin, xknet_usrcopyin, netp);
	if (netp->net_ilen < (netp->net_status.min_packet_size -
			      netp->net_status.header_size)) {
	    bzero(net_kmsg(netp->net_kmsg)->packet +
		  sizeof (struct packet_header) + netp->net_ilen,
		  netp->net_status.min_packet_size -
		  netp->net_status.header_size - netp->net_ilen);
	    netp->net_ilen = netp->net_status.min_packet_size;
	} else
	    netp->net_ilen += netp->net_status.header_size;
	break;

#endif /* NET_XKMACHKERNEL */
    case NET_MODULE_DEVICE:
	/*
	 * Copy header and data of an output device I/O request
	 */
	bcopy(netd->netd_ior->io_data,
	      net_kmsg(netp->net_kmsg)->header, netp->net_status.header_size);
	bcopy(netd->netd_ior->io_data + netp->net_status.header_size,
	      net_kmsg(netp->net_kmsg)->packet + sizeof (struct packet_header),
	      netd->netd_ior->io_count - netp->net_status.header_size);
	if (netd->netd_ior->io_count < netp->net_status.min_packet_size) {
	    bzero(net_kmsg(netp->net_kmsg)->packet +
		  sizeof (struct packet_header) +
		  (netd->netd_ior->io_count - netp->net_status.header_size),
		  netp->net_status.min_packet_size -
		  netd->netd_ior->io_count);
	    netp->net_ilen =
		netp->net_status.min_packet_size - netp->net_status.header_size;
	} else
	    netp->net_ilen =
		netd->netd_ior->io_count - netp->net_status.header_size;
	break;
    }

    netp->net_itype = netd->netd_ptype;
    net_driver_input(netp, netd);
    return;
}

/*
 * net_driver_copyout
 *
 * Purpose : network generic driver copyout termination management
 * Returns : TRUE/FALSE if copyout has been completed
 *
 * IN_arg  : netp ==> network generic structure
 *	     netd ==> output packet to copyout
 */
boolean_t
net_driver_copyout(
    net_device_t netp,
    net_done_t netd)
{
    if (netd->netd_outlen < netp->net_status.min_packet_size) {
	/*
	 * Achieve minimum transmission length
	 */
	switch((*netp->net_bic_ops.bic_copyout)(netp, netd, (vm_offset_t)0,
						(netp->net_status.
						 min_packet_size -
						 netd->netd_outlen),
						(NET_BICOUT_FLAGS_LAST |
						 NET_BICOUT_FLAGS_PADDING))) {
	case -1:	/* error */
	    if (net_driver_reset(netp) == D_SUCCESS)
		(void)net_driver_init(netp);
	    return (FALSE);

	case 0:		/* copyout in progress */
	    netd->netd_outlen = netp->net_status.min_packet_size;
	    return (FALSE);

	default:	/* copyout completed */
	    netd->netd_outlen = netp->net_status.min_packet_size;
	    break;
	}
    }
    return (TRUE);
}

/*
 * net_driver_copyin
 *
 * Purpose : network generic driver copyin continuation management
 * Returns : TRUE/FALSE if some work has been completed
 *
 * IN_arg  : netp ==> network generic structure
 *	     to   ==> address where to copyin to
 */
boolean_t
net_driver_copyin(
    net_device_t netp,
    vm_offset_t to)
{
    vm_offset_t from;
    unsigned int atype;
    unsigned int len;
    int receiver;

    /*
     * Copyin the rest of the frame
     */
    for (;;) {
	switch ((*netp->net_nic_ops.nic_copyin)(netp, &receiver,
						&from, &len, &atype)) {
	case NET_NICIN_ERROR:		/* error */
	    if (net_driver_reset(netp) == D_SUCCESS)
		(void)net_driver_init(netp);
	    return (FALSE);

	case NET_NICIN_EMPTY:		/* no packet */
	    panic("%s: No packet to continue packet copyin\n",
		  "net_driver_copyin");

	case NET_NICIN_COMPLETED:	/* copyin completed */
	    panic("%s: Copyin completed in continuation\n",
		  "net_driver_copyin");

	case NET_NICIN_SUCCESS:		/* packet input */
	    if (len == 0)
		break;

	    netp->net_ilen += len;
	    switch ((*netp->net_bic_ops.bic_copyin)(netp, receiver, from,
						    to, len, FALSE)) {
	    case NET_BICIN_ERROR:	/* error */
		if (net_driver_reset(netp) == D_SUCCESS)
		    (void)net_driver_init(netp);
		break;

	    case NET_BICIN_IN_PROGRESS:	/* copyin in progress */
		break;

	    case NET_BICIN_COMPLETED:	/* copyin has been done */
		to += len;
		continue;
	    }
	    return (FALSE);
	}
	break;
    }
    return (TRUE);
}

/*
 * net_driver_copy
 *
 * Purpose : network generic driver copyin/copyout management
 * Returns : TRUE/FALSE if some work has been started and completed
 *
 * IN_arg  : netp   ==> network generic structure
 * OUT_arg : copyin ==> TRUE/FALSE if copyin has been started
 */
boolean_t
net_driver_copy(
    net_device_t netp,
    boolean_t *copyin)
{
    vm_offset_t to;
    vm_offset_t from;
    unsigned char *addr;
    unsigned int atype;
    vm_size_t len;
    int receiver;
    unsigned short type;
    unsigned int header_type;
    int ret;
    net_done_t netd;
    ipc_kmsg_t kmsg;
    char *header;
    char header_array[NET_HDW_HDR_MAX];

    /*
     * BIC chip must not be busy on output
     */
    if ((netd = netp->net_opacket) != (net_done_t)0 && netd->netd_outlen != 0)
	switch (netd->netd_module) {
	default:
	    assert(netd->netd_module == NET_MODULE_KGDB);
	    break;
#if NET_XKMACHKERNEL
	case NET_MODULE_XKERNEL:
	    if (netd->netd_outlen < msgLen(&netd->netd_xk->xk_msg)) {
		/*
		 * Continue copying out x-kernel message
		 */
		netp->net_xk_olen = netp->net_status.header_size;
		msgForEachAlternate(&netd->netd_xk->xk_msg,
				    xknet_copyout, xknet_usrcopyout, netp);
		if (netd->netd_outlen > netp->net_xk_olen)
		    return (FALSE);
	    }
	    if (net_driver_copyout(netp, netd)) {
		*copyin = FALSE;
		return (TRUE);
	    }
	    return (FALSE);
#endif /* NET_XKMACHKERNEL */
	case NET_MODULE_DEVICE:
	    if (net_driver_copyout(netp, netd)) {
		*copyin = FALSE;
		return (TRUE);
	    }
	    return (FALSE);
	}

    /*
     * BIC chip must not be busy on input
     */
#if NET_XKMACHKERNEL
    if (netp->net_xk_input != (InputBuffer *)0) {
	if (net_driver_copyin(netp, (vm_offset_t)(netp->net_xk_input->data +
						 netp->net_ilen))) {
	    *copyin = TRUE;
	    return (TRUE);
	}
	return (FALSE);
    }
#endif /* NET_XKMACHKERNEL */

    if (netp->net_kmsg != (ipc_kmsg_t)0) {
	if (net_driver_copyin(netp,
			      (vm_offset_t)(net_kmsg(netp->net_kmsg)->packet +
					    sizeof (struct packet_header) +
					    netp->net_ilen))) {
	    *copyin = TRUE;
	    return (TRUE);
	}
	return (FALSE);
    }

    /*
     * First, try to copyout
     */
    for (;;) {
#if NET_XKMACHKERNEL
	NET_DEQUEUE(&netp->net_xk_head, netd);
	if (netd == (net_done_t)0)
#endif /* NET_XKMACHKERNEL */
	    NET_DEQUEUE(&netp->net_head, netd);
	if (netd == (net_done_t)0)
	    break;

	/*
	 * Reject the write if the interface is down.
	 */
	if ((netp->net_status.flags & (NET_STATUS_UP |
				       NET_STATUS_RUNNING)) !=
	    (NET_STATUS_UP | NET_STATUS_RUNNING)) {
	    net_driver_free(netp, netd);
	    continue;
	}

	netp->net_opacket = netd;
	switch (netd->netd_module) {
	default:
	    assert(netd->netd_module == NET_MODULE_KGDB);
	    continue;

#if NET_XKMACHKERNEL
	case NET_MODULE_XKERNEL:
	    assert(netd->netd_outlen == 0);

	    switch ((*netp->net_nic_ops.nic_copyout)(netp, netd)) {
	    case NET_NICOUT_ERROR:	/* error */
		if (net_driver_reset(netp) == D_SUCCESS)
		    (void)net_driver_init(netp);
		return (FALSE);

	    case NET_NICOUT_DELAYED:	/* no available space */
		netp->net_opacket = (net_done_t)0;
		NET_PREPEND(&netp->net_head, netd);
		break;

	    case NET_NICOUT_COMPLETED:	/* packet copyout is completed */
		*copyin = FALSE;
		return (TRUE);

	    case NET_NICOUT_SUCCESS:	/* packet copyout is accepted */
		netd->netd_outlen += netp->net_status.header_size;
		switch ((*netp->net_bic_ops.bic_copyout)
			(netp, netd,
			 (vm_offset_t)msgGetAttr(&netd->netd_xk->xk_msg, 0),
			 netp->net_status.header_size,
			 NET_BICOUT_FLAGS_VIRTUAL | NET_BICOUT_FLAGS_FIRST |
			 (netd->netd_outlen < netd->netd_length ?
			  0 : NET_BICOUT_FLAGS_LAST))) {
		case NET_BICOUT_ERROR:		/* error */
		    if (net_driver_reset(netp) == D_SUCCESS)
			(void)net_driver_init(netp);
		    break;

		case NET_BICOUT_IN_PROGRESS:	/* copyout in progress */
		    break;

		case NET_BICOUT_COMPLETED:	/* copyout completed */
		    netp->net_xk_olen = netp->net_status.header_size;
		    msgForEachAlternate(&netd->netd_xk->xk_msg,
					xknet_copyout, xknet_usrcopyout, netp);
		    if (netd->netd_outlen > netp->net_xk_olen)
			break;

		    /*
		     * X-kernel message has been completely copied out
		     */
		    if (net_driver_copyout(netp, netd)) {
			*copyin = FALSE;
			return (TRUE);
		    }
		    break;
		}
		return (FALSE);
	    }
	    break;

#endif /* NET_XKMACHKERNEL */

	case NET_MODULE_DEVICE:
	    assert(netd->netd_outlen == 0);

	    /*
	     * Start copying out packet
	     */
	    switch ((*netp->net_nic_ops.nic_copyout)(netp, netd)) {
	    case NET_NICOUT_ERROR:	/* error */
		if (net_driver_reset(netp) == D_SUCCESS)
		    (void)net_driver_init(netp);
		return (FALSE);

	    case NET_NICOUT_DELAYED:	/* no available space */
		netp->net_opacket = (net_done_t)0;
		NET_PREPEND(&netp->net_head, netd);
		break;

	    case NET_NICOUT_COMPLETED:	/* packet copyout is completed */
		*copyin = FALSE;
		return (TRUE);

	    case NET_NICOUT_SUCCESS:	/* packet copyout is accepted */
		netd->netd_outlen += len;
		switch ((*netp->net_bic_ops.bic_copyout)
			(netp, netd, (vm_offset_t)netd->netd_ior->io_data, len,
			 NET_BICOUT_FLAGS_VIRTUAL | NET_BICOUT_FLAGS_FIRST |
			 (len < netd->netd_length ?
			  0 : NET_BICOUT_FLAGS_LAST))) {
		case NET_BICOUT_ERROR:	/* error */
		    if (net_driver_reset(netp) == D_SUCCESS)
			(void)net_driver_init(netp);
		    break;

		case NET_BICOUT_IN_PROGRESS:	/* copyout in progress */
		    break;

		case NET_BICOUT_COMPLETED:	/* copyout completed */
		    if (net_driver_copyout(netp, netd)) {
			*copyin = FALSE;
			return (TRUE);
		    }
		    break;
		}
		return (FALSE);
	    }
	    break;
	}
    }

    /*
     * Then, try to copyin
     */
    for (;;) {
	for (;;) {
	    switch ((*netp->net_nic_ops.nic_copyin)(netp, &receiver,
						    &from, &len, &atype)) {
	    case NET_NICIN_ERROR:	/* error */
		if (net_driver_reset(netp) == D_SUCCESS)
		    (void)net_driver_init(netp);
		return (FALSE);

	    case NET_NICIN_EMPTY:	/* no packet */
		return (FALSE);

	    case NET_NICIN_COMPLETED:	/* Packet completed */
		kmsg = (ipc_kmsg_t)from;

		if (netp->net_status.flags & NET_STATUS_SG)
		    panic("%s: Scatter/Gather interface not yet implemented\n",
			  "net_driver_copy");
		else
		    header = net_kmsg(kmsg)->header;
		break;

	    case NET_NICIN_SUCCESS:	/* packet to be copied in */
		/*
		 * Copyin header from the NIC
		 */
		assert(netp->net_status.header_size < NET_HDW_HDR_MAX);

		switch ((*netp->net_bic_ops.bic_copyin)
			(netp, receiver, from, (vm_offset_t)header,
			 netp->net_status.header_size, TRUE)) {
		case NET_BICIN_ERROR:		/* error */
		    if (net_driver_reset(netp) == D_SUCCESS)
			(void)net_driver_init(netp);
		    return (FALSE);

		case NET_BICIN_IN_PROGRESS:	/* copyin in progress */
		    panic("%s: synchronous copyin returned asynchronous\n",
			  "net_driver_copy");

		case NET_BICIN_COMPLETED:	/* copyin has been done */
		    break;
		}

		header = header_array;
		kmsg = IKM_NULL;
		from += netp->net_status.header_size;
		break;
	    }
	    len -= netp->net_status.header_size;

	    /*
	     * Validate header format
	     */
	    switch (netp->net_status.header_format) {
	    case HDR_ETHERNET:
	    {
		unsigned int offset;

		switch (netp->net_status.media_type) {
		case NET_MEDIA_OTHERS:
		    if (netp->net_status.header_size !=
			sizeof (struct if_myrinet_eep))
			panic("%s: Unsupported media type (header length = %d)",
			      "net_driver_copy", netp->net_status.header_size);
		    offset = (netp->net_status.header_size -
			      sizeof (struct ether_header));
		    break;

		case NET_MEDIA_ETHERNET:
		    offset = 0;
		    break;

		default:
		    panic("%s: Unsupported media type %d\n",
			  "net_driver_copy", netp->net_status.media_type);
		}

		addr = ((struct ether_header *)(header + offset))->ether_dhost;
		netp->net_ptype = ntohs(((struct ether_header *)
					 (header + offset))->ether_type);
		break;
	    }
	    default:
		panic("%s: Unsupported header format %d\n",
		      "net_driver_copy", netp->net_status.header_format);
	    }

	    /*
	     * Validate received packet
	     */
	    netp->net_itype = net_driver_addrtype(netp, header, atype);
	    if ((atype & NET_PACKET_PERFECT) ||
		netp->net_itype == NET_PACKET_BROADCAST)
		break;

	    if (netp->net_itype == NET_PACKET_UNICAST)
		panic("%s: Unsupported imperfect unicast address\n",
		      "net_driver_copy");

	    assert(netp->net_itype == NET_PACKET_MULTICAST);
	    if (net_multicast_match(&netp->net_mqueue, addr,
				    netp->net_status.media_type))
		break;

	    /*
	     * Discard packet (it was not for us).
	     */
	    len += netp->net_status.header_size;
	    if (kmsg != IKM_NULL) {
		/*
		 * Free prepared input buffer
		 */
		netp->net_genstat.in_pkt_discarded++;
		netp->net_genstat.in_bytes += len;
		net_kmsg_put_buf(netp->net_pool, kmsg);
		continue;
	    }

	    for (;;) {
		if (len == 0) {
		    netp->net_genstat.in_pkt_discarded++;
		    break;
		}
		netp->net_genstat.in_bytes += len;

		switch ((*netp->net_nic_ops.nic_copyin)(netp, &receiver,
							&from, &len, &atype)) {
		case NET_NICIN_ERROR:		/* error */
		    if (net_driver_reset(netp) == D_SUCCESS)
			(void)net_driver_init(netp);
		    return (FALSE);
		case NET_NICIN_SUCCESS:		/* packet to be copied in */
		    break;

		case NET_NICIN_EMPTY:		/* no packet */
		    panic("%s: impossible NET_NICIN_EMPTY nic_copyin\n",
			  "net_driver_copy"); 

		case NET_NICIN_COMPLETED:	/* packet copied in on place */
		    panic("%s: impossible NET_NICIN_COMPLETED nic_copyin\n",
			  "net_driver_copy"); 
		}
	    }
	}

	if (kmsg != IKM_NULL) {
	    /*
	     * Prepared input buffer
	     *
	     * XXX XKERNEL not yet implemented
	     */
	    assert(netp->net_kmsg == (ipc_kmsg_t)0);
	    netp->net_ilen = len;
	    netp->net_kmsg = kmsg;
	    *copyin = TRUE;
	    return (TRUE);
	}

	/*
	 * Allocate needed x-kernel buffer and/or ipc kernel message
	 */
	net_driver_clone(netp);

#if NET_XKMACHKERNEL
	/*
	 * Start by copying it into an x-kernel buffer
	 */
	if (netp->net_xk_input != (InputBuffer *)0) {
	    /*
	     * First, copy header
	     */
	    bcopy(header,
		  netp->net_xk_input->hdr, netp->net_status.header_size);
	    if (netp->net_kmsg != (ipc_kmsg_t)0) {
		/*
		 * If an input kmsg exists, copy header while in the cache
		 * Data copy into kmsg is delayed until net_driver_input call
		 */
		bcopy(header,
		      net_kmsg(netp->net_kmsg)->header,
		      netp->net_status.header_size);
	    }
	    to = (vm_offset_t)netp->net_xk_input->data;

	    /*
	     * Then, copy data
	     */
	    netp->net_ilen = len;
	    switch ((*netp->net_bic_ops.bic_copyin)(netp, receiver,
						    from, to, len, FALSE)) {
	    case NET_BICIN_ERROR:	/* error */
		if (net_driver_reset(netp) == D_SUCCESS)
		    (void)net_driver_init(netp);
		break;

	    case NET_BICIN_IN_PROGRESS:	/* copyin in progress */
		break;

	    case NET_BICIN_COMPLETED:	/* copyin has been done */
		if (net_driver_copyin(netp, to + len)) {
		    *copyin = TRUE;
		    return (TRUE);
		}
		break;
	    }
	    return (FALSE);
	}
#endif /* NET_XKMACHKERNEL */

	/*
	 * Data is not copied into an x-kernel message, so copy it into a kmsg
	 */
	if (netp->net_kmsg != (ipc_kmsg_t)0) {
	    /*
	     * First, copy header
	     */
	    bcopy(header,
		  net_kmsg(netp->net_kmsg)->header,
		  netp->net_status.header_size);

	    /*
	     * Then, copy data
	     */
	    netp->net_ilen = len;
	    to = (vm_offset_t)net_kmsg(netp->net_kmsg)->packet +
		sizeof (struct packet_header);
	    switch ((*netp->net_bic_ops.bic_copyin)(netp, receiver,
						    from, to, len, FALSE)) {
	    case NET_BICIN_ERROR:	/* error */
		if (net_driver_reset(netp) == D_SUCCESS)
		    (void)net_driver_init(netp);
		break;

	    case NET_BICIN_IN_PROGRESS:	/* copyin in progress */
		break;

	    case NET_BICIN_COMPLETED:	/* copyin has been done */
		if (net_driver_copyin(netp, to + len)) {
		    *copyin = TRUE;
		    return (TRUE);
		}
		break;
	    }
	    return (FALSE);
	}

	/*
	 * Nothing has been started: clean up NIC received packet
	 */
	len += netp->net_status.header_size;
	do {
	    netp->net_genstat.in_bytes += len;
	    switch ((*netp->net_nic_ops.nic_copyin)(netp, &receiver,
						    &from, &len, &atype)) {
	    case NET_NICIN_ERROR:	/* error */
		if (net_driver_reset(netp) == D_SUCCESS)
		    (void)net_driver_init(netp);
		return (FALSE);

	    case NET_NICIN_EMPTY:	/* no packet */
		panic("net_driver_copy: packet without end marker\n");

	    case NET_NICIN_SUCCESS:	/* packet to be copied in */
		break;

	    case NET_NICIN_COMPLETED:	/* packet copied in on place */
		panic("%s: impossible NET_NICIN_COMPLETED nic_copyin\n",
		      "net_driver_copy"); 
	    }
	} while (len != 0);
	netp->net_genstat.in_pkt_discarded++;
    }
}

/*
 * net_driver_event
 *
 * Purpose : network generic driver event management
 * Returns : Nothing
 *
 * IN_arg  : netp ==> network generic structure
 */
void
net_driver_event(
    net_device_t netp)
{
    boolean_t busy;
    boolean_t copyin;
    boolean_t event;
    boolean_t nic;
    unsigned count;
    net_done_t netd;

    /*
     * Manage everything that has been completely copied in or out
     */
    nic = FALSE;
    for (;;) {
	switch ((*netp->net_bic_ops.bic_intr)(netp, &copyin, &busy, &event)) {
	case NET_BICINTR_ERROR:
	    /*
	     * Error on the BIC
	     */
	    if (net_driver_reset(netp) == D_SUCCESS)
		(void)net_driver_init(netp);
	    break;

	case NET_BICINTR_NOTHING:
	    /*
	     * No event to report on the BIC
	     */
	    if (busy || !net_driver_copy(netp, &copyin)) {
		if (event || nic)
		    /*
		     * Lately acknowledge any NIC event
		     */
		    switch ((*netp->net_nic_ops.nic_intr)(netp)) {
		    case NET_NICINTR_ERROR:
			/*
			 * Error on the NIC ==> Reset the adapter
			 */
			if (net_driver_reset(netp) == D_SUCCESS)
			    (void)net_driver_init(netp);
			break;

		    case NET_NICINTR_NOTHING:
			/*
			 * Nothing happened on the NIC
			 */
			break;

		    case NET_NICINTR_EVENT:
			/*
			 * Something happened on the NIC
			 */
			if (!nic)
			    nic = TRUE;
			continue;
		    }
		break;
	    }
	    /*FALL THRU*/

	case NET_BICINTR_EVENT:
	    /*
	     * BIC reported an event or
	     * BIC is not busy and copy in/out has been completed
	     */
	    if (event && !nic)
		nic = TRUE;

	    if (copyin) {
		/*
		 * End of BIC copyin must give input packet to higher
		 * level input queues.
		 */
		net_driver_input(netp, (net_done_t)0);

	    } else {
		/*
		 * End of BIC copyout must start NIC transmission
		 */
		netd = netp->net_opacket;
		netp->net_opacket = (net_done_t)0;
		switch ((*netp->net_nic_ops.nic_start)(netp, netd)) {
		case -1:
		    /*
		     * Error on the NIC ==> Reset the adapter
		     */
		    if (net_driver_reset(netp) == D_SUCCESS)
			(void)net_driver_init(netp);
		    break;

		default:
		    /*
		     * Start successful on the NIC
		     */
		    continue;
		}
		break;
	    }
	    continue;
	}

	/*
	 * Clean up done queue
	 */
	mpdequeue_head(&netp->net_done, (queue_entry_t *)&netd);
	if (netd == (net_done_t)0)
	    break;

	do {
	    net_driver_loopback(netp, netd);
	    net_driver_free(netp, netd);
	    mpdequeue_head(&netp->net_done, (queue_entry_t *)&netd);
	} while (netd != (net_done_t)0);

	if (nic)
	    nic = FALSE;
    }

    /*
     * Try to fill up the DMA copyin buffers
     */
    if (netp->net_nic_ops.nic_prepin &&
	(netp->net_status.flags & (NET_STATUS_CALLBACK |
				   NET_STATUS_RUNNING)) == NET_STATUS_RUNNING)
	net_driver_setup(netp);
}

/*
 * net_driver_start
 *
 * Purpose : network generic start routine
 * Returns : Nothing
 *
 * IN_arg  : unit ==> network generic unit number
 *
 * Comment : This procedure is only used by MP systems to continue
 *		INTR/START event management
 */
void
net_driver_start(
    int	unit)
{
    net_device_t netp;

    NET_VALIDATE_VOID_NETP(netp, unit, "net_driver_start");

    net_driver_event(netp);
}

/*
 * net_driver_intr
 *
 * Purpose : network generic IT handler
 * Returns : Nothing
 *
 * IN_arg  : unit ==> network generic unit number
 */
void
net_driver_intr(
    int	unit)
{
    net_device_t netp;
    IOBUS_LOCK_STATE();

    NET_VALIDATE_VOID_NETP(netp, unit, "net_driver_intr");

    if (IOBUS_LOCK(&netp->net_dev_lock, IOBUS_LOCK_INTR)) {
	net_driver_event(netp);
	IOBUS_UNLOCK(&netp->net_dev_lock);
    }
}

/*
 * net_driver_timeout
 *
 * Purpose : network generic timeout routine
 * Returns : Nothing
 *
 * IN_arg  : unit ==> network generic unit number
 */
void
net_driver_timeout(
    int	unit)
{
    spl_t spl;
    net_device_t netp;
    IOBUS_LOCK_STATE();

    NET_VALIDATE_VOID_NETP(netp, unit, "net_driver_timeout");

    spl = splimp();
    if (IOBUS_LOCK(&netp->net_dev_lock, IOBUS_LOCK_TIMEO)) {
	if ((*netp->net_nic_ops.nic_timeout)(netp) == -1) {
	    if (net_driver_reset(netp) == D_SUCCESS)
		(void)net_driver_init(netp);
	}
	IOBUS_UNLOCK(&netp->net_dev_lock);
    }
    splx(spl);
}

/*
 * net_devops_open
 *
 * Purpose : Network generic open operation
 * Returns : Device return value
 *
 * IN_arg  : dev  ==> device to open
 *	     mode ==> open mode
 *	     ior  ==> open I/O request
 */
io_return_t
net_devops_open(
    dev_t dev,
    dev_mode_t mode,
    io_req_t ior)
{
    int unit = minor(dev);
    net_device_t netp;
    spl_t spl;
    io_return_t ret;
    int max;
    IOBUS_LOCK_STATE();

    NET_VALIDATE_RET_NETP(netp, unit);

    if (netp->net_pool == NET_POOL_NULL) {
	ret = net_kmsg_create_pool(NET_POOL_INLINE,
				   (sizeof(struct net_rcv_msg) +
				    netp->net_status.max_packet_size -
				    netp->net_status.header_size -
				    NET_RCV_MAX),
				   netp->net_pool_min,
				   FALSE,
				   NET_DEVICE_POOL,
				   &netp->net_pool);
	if (ret != KERN_SUCCESS)
	    return (ret);

    } else {
	ret = net_kmsg_grow_pool(netp->net_pool, netp->net_pool_min, &max);
	if (ret != KERN_SUCCESS)
	    return (ret);
    }
    net_kmsg_more(netp->net_pool);

    if (netp->net_zone == ZONE_NULL) {
	netp->net_zone = zinit(sizeof (struct net_done) + netp->net_osize,
			       PAGE_SIZE, PAGE_SIZE, NET_DEVICE_ZONE);
	zone_enable_spl(netp->net_zone, splimp);
	zfill(netp->net_zone, 1);
    } else {
	/*
	 * Force an element to be in the free area
	 */
	zfree(netp->net_zone, zalloc(netp->net_zone));
    }

    spl = splimp();
    IOBUS_LOCK(&netp->net_dev_lock, IOBUS_LOCK_WAIT);

    /* XXX Cloning is not yet implemented */
    netp->net_device = ior->io_device;

    IOBUS_UNLOCK(&netp->net_dev_lock);
    splx(spl);
    return (D_SUCCESS);
}

/*
 * net_devops_close
 *
 * Purpose : Network generic close operation
 * Returns : Nothing
 *
 * IN_arg  : dev  ==> device to close
 */
void
net_devops_close(
    dev_t dev)
{
    int unit = minor(dev);
    net_device_t netp;
    spl_t spl;
    unsigned int running;
    int max;
    IOBUS_LOCK_STATE();

    NET_VALIDATE_VOID_NETP(netp, unit, "net_devops_close");

    /*
     * Don't close device if not already open
     */
    spl = splimp();
    IOBUS_LOCK(&netp->net_dev_lock, IOBUS_LOCK_WAIT);
    if (running = (netp->net_status.flags & NET_STATUS_RUNNING))
	(void)net_driver_reset(netp);
    IOBUS_UNLOCK(&netp->net_dev_lock);
    splx(spl);

    if (running) {
	/*
	 * Shrink pool
	 */
	net_kmsg_shrink_pool(netp->net_pool, netp->net_pool_min, &max);
	netp->net_pool = NET_POOL_NULL;
    }
}

/*
 * net_devops_reset
 *
 * Purpose : Network generic reset operation
 * Returns : Device return value
 *
 * IN_arg  : dev  ==> device to reset
 */
void
net_devops_reset(
    dev_t dev)
{
    int unit = minor(dev);
    net_device_t netp;
    spl_t spl;
    IOBUS_LOCK_STATE();

    NET_VALIDATE_VOID_NETP(netp, unit, "net_devops_reset");

    spl = splimp();
    IOBUS_LOCK(&netp->net_dev_lock, IOBUS_LOCK_WAIT);
    if ((netp->net_status.flags & NET_STATUS_RUNNING) == 0 ||
	net_driver_reset(netp) == D_SUCCESS)
	(void)net_driver_init(netp);
    IOBUS_UNLOCK(&netp->net_dev_lock);
    splx(spl);
}

/*
 * net_devops_write
 *
 * Purpose : Network generic write operation
 * Returns : Device return value
 *
 * IN_arg  : dev  ==> device to write to
 *	     ior  ==> write I/O request
 *
 * Comment : It is assumed that the device has already been opened
 */
io_return_t
net_devops_write(
    dev_t dev,
    io_req_t ior)
{
    int unit = minor(dev);
    spl_t spl;
    net_device_t netp;
    io_return_t	ret;
    net_done_t netd;
    boolean_t wait;
    IOBUS_LOCK_STATE();

    NET_VALIDATE_RET_NETP(netp, unit);

    /*
     * Reject the write if the packet is too large or too small.
     */
    if (ior->io_count < netp->net_status.header_size ||
	ior->io_count > netp->net_status.max_packet_size)
	return (D_INVALID_SIZE);

    /*
     * Wire down the memory.
     */
    ret = device_write_get(ior, &wait);
    if (ret != KERN_SUCCESS)
	return (ret);

    /*
     *	Network interfaces can't cope with VM continuations.
     *	If wait is set, the packet sent is too long, then return an error
     */
    if (wait) {
	ior->io_residual = ior->io_count;
	return (D_INVALID_SIZE);
    }

    /*
     * Allocate net_done element and initialize it
     */
    if (ior->io_mode & D_NOWAIT) {
	netd = (net_done_t)zget(netp->net_zone);
	if (netd == (net_done_t)0) {
	    ior->io_residual = ior->io_count;
	    return (D_WOULD_BLOCK);
	}
    } else
	netd = (net_done_t)zalloc(netp->net_zone);

    netd->netd_ior = ior;
    netd->netd_module = NET_MODULE_DEVICE;
    netd->netd_done = (void (*)(net_device_t, net_done_t))0;
    netd->netd_external = netd + 1;
    netd->netd_outlen = 0;
    netd->netd_ptype = net_driver_addrtype(netp, ior->io_data, 0);

    /*
     * Prepare netdone element to be copied out
     */
    if (ior->io_count < netp->net_status.min_packet_size) {
	netd->netd_length = netp->net_status.min_packet_size;
	if (netp->net_nic_ops.nic_prepout) {
	    (*netp->net_nic_ops.nic_prepout)
		(netp, netd, (vm_offset_t)ior->io_data, ior->io_count,
		 NET_BICOUT_FLAGS_VIRTUAL | NET_BICOUT_FLAGS_FIRST);
	    netd->netd_outlen = ior->io_count;
	    (*netp->net_nic_ops.nic_prepout)
		(netp, netd, 0, netd->netd_length - ior->io_count,
		 NET_BICOUT_FLAGS_LAST | NET_BICOUT_FLAGS_PADDING);
	    netd->netd_outlen = 0;
	}
    } else {
	netd->netd_length = ior->io_count;
	if (netp->net_nic_ops.nic_prepout)
	    (*netp->net_nic_ops.nic_prepout)
		(netp, netd, (vm_offset_t)ior->io_data, ior->io_count,
		 (NET_BICOUT_FLAGS_VIRTUAL |
		  NET_BICOUT_FLAGS_FIRST |
		  NET_BICOUT_FLAGS_LAST));
    }

    /*
     * Queue the packet on the output queue, waiting if it is an
     * asynchronous write in a full output queue
     */
    spl = splimp();
    NET_ENQUEUE(&netp->net_head, netd, TRUE);

    /*
     * Start the device
     */
    if (IOBUS_LOCK(&netp->net_dev_lock, IOBUS_LOCK_START)) {
	net_driver_event(netp);
	IOBUS_UNLOCK(&netp->net_dev_lock);
    }
    splx(spl);

    /*
     * Wait for fully synchronous operations
     */
    if (ior->io_op & IO_SYNC) {
	iowait(ior);
	return(D_SUCCESS);
    }
    return (D_IO_QUEUED);
}

/*
 * net_devops_getstat
 *
 * Purpose : Network generic getstat operation
 * Returns : Device return value
 *
 * IN_arg  : dev    ==> device to get status from
 *	     flavor ==> type of status
 *	     data   ==> area to put status
 *	     count  ==> area size
 * OUT_arg : count  ==> size of returned data
 *
 * Comment : It is assumed that the device has alredy been opened
 */
io_return_t
net_devops_getstat(
    dev_t dev,
    dev_flavor_t flavor,
    dev_status_t data,
    mach_msg_type_number_t *count)
{
    int unit = minor(dev);
    net_device_t netp;
    spl_t oldpri;
    io_return_t	ret = D_SUCCESS;
    IOBUS_LOCK_STATE();

    NET_VALIDATE_RET_NETP(netp, unit);

    /*
     * Insure that pointers are stable
     */
    oldpri = splimp();
    IOBUS_LOCK(&netp->net_dev_lock, IOBUS_LOCK_WAIT);

    switch (flavor) {
    case NET_STATUS:
	if (*count > NET_STATUS_COUNT)
	    *count = NET_STATUS_COUNT;
	bcopy((char *)&netp->net_status, (char *)data, *count * sizeof(int));
	break;

    case NET_ADDRESS:
    {
	int addr_byte_count;
	int addr_int_count;
	int i;
	int word;

	addr_byte_count = netp->net_status.address_size;
	addr_int_count = (addr_byte_count + (sizeof (int) - 1)) / sizeof (int);
	if (*count < addr_int_count) {
	    *count = 0;
	    break;
	}

	bcopy((char *)netp->net_address,
	      (char *)data, (unsigned) addr_byte_count);
	if (addr_byte_count < addr_int_count * sizeof (int))
	    bzero((char *)data + addr_byte_count,
		  (unsigned) (addr_int_count * sizeof (int) - addr_byte_count));

	for (i = 0; i < addr_int_count; i++) {
	    word = data[i];
	    data[i] = htonl(word);
	}
	*count = addr_int_count;
	break;
    }

    case NET_GENERIC_STATS:
	if (*count > NET_GENERIC_STATS_COUNT)
	    *count = NET_GENERIC_STATS_COUNT;
	bcopy((char *)&netp->net_genstat, (char *)data, *count * sizeof (int));
	break;

    case NET_SPECIFIC_STATS:
	if (*count > netp->net_spfcount)
	    *count = netp->net_spfcount;
	bcopy((char *)&netp->net_spfstat, (char *)data, *count * sizeof (int));
	break;

    default:
	ret = D_INVALID_OPERATION;
	break;
    }

    IOBUS_UNLOCK(&netp->net_dev_lock);
    splx(oldpri);
    return (ret);
}

/*
 * net_devops_setstat
 *
 * Purpose : Network generic setstat operation
 * Returns : Device return value
 *
 * IN_arg  : dev    ==> device to set status to
 *	     flavor ==> type of status
 *	     data   ==> status area
 *	     count  ==> area size
 *
 * Comment : It is assumed that the device has alredy been opened
 */
io_return_t
net_devops_setstat(
    dev_t dev,
    dev_flavor_t flavor,
    dev_status_t data,
    mach_msg_type_number_t count)
{
    int unit = minor(dev);
    io_return_t ret = D_SUCCESS;
    net_device_t netp;
    spl_t oldpri;
    IOBUS_LOCK_STATE();

    NET_VALIDATE_RET_NETP(netp, unit);

    switch (flavor) {
    case NET_STATUS:
    {
	struct net_status *ns;
	boolean_t setup_mcast;

	if (count < NET_STATUS_COUNT)
	    return (D_INVALID_SIZE);
	/*
	 * All we can change are flags, and not many of those.
	 */
	ns = (struct net_status *)data;

	/*
	 * Force a complete reset if the receive mode changes
	 * so that these take effect immediately.
	 */
	oldpri = splimp();
	IOBUS_LOCK(&netp->net_dev_lock, IOBUS_LOCK_WAIT);
	if ((netp->net_status.flags & NET_STATUS_CANTCHANGE) !=
	    (ns->flags & ~NET_STATUS_CANTCHANGE)) {
	    /*
	     * Manage ALLMULTI flag setup
	     */
	    setup_mcast = ((ns->flags & NET_STATUS_ALLMULTI) !=
			   (netp->net_status.flags & NET_STATUS_ALLMULTI));
	    if (setup_mcast &&
		(netp->net_status.flags & NET_STATUS_MULTICAST) == 0)
		ret = D_INVALID_OPERATION;
	    else {
		netp->net_status.flags &= NET_STATUS_CANTCHANGE;
		netp->net_status.flags |= ns->flags & ~NET_STATUS_CANTCHANGE;

		if (setup_mcast)
		    (*netp->net_nic_ops.nic_multicast)(netp,
						       (net_multicast_t *)0);
	    }

	    if ((ns->flags & NET_STATUS_UP) == 0 &&
		(netp->net_status.flags & NET_STATUS_RUNNING)) {
		/*
		 * Reset BIC/NIC
		 */
		if (ret == D_SUCCESS)
		    ret = net_driver_reset(netp);
	    }

	    if ((ns->flags & NET_STATUS_UP) &&
		(netp->net_status.flags & NET_STATUS_RUNNING) == 0) {
		/*
		 * Init BIC/NIC
		 */
		if (ret == D_SUCCESS)
		    ret = net_driver_init(netp);
	    }
	}
	IOBUS_UNLOCK(&netp->net_dev_lock);
	splx(oldpri);
	break;
    }

    case NET_ADDMULTI:
    {
	net_multicast_t *new;
	unsigned int temp;

	/*
	 * Validate multicast attribute and addresses
	 */
	if ((netp->net_status.flags & NET_STATUS_MULTICAST) == 0)
	    return (D_INVALID_OPERATION);

	if (count != netp->net_status.address_size * 2 ||
	    (((unsigned char *)data)[0] & 1) == 0 ||
	    (((unsigned char *)data)[netp->net_status.address_size] & 1) == 0)
	    return (KERN_INVALID_VALUE);

	for (temp = 0; temp < netp->net_status.address_size; temp++)
	    if (((unsigned char *)data)[temp] !=
		((unsigned char *)data)[netp->net_status.address_size + temp])
		break;
	temp = NET_MULTICAST_LENGTH(netp->net_status.address_size) +
	    netp->net_msize[temp == netp->net_status.address_size ? 0 : 1];
	new = (net_multicast_t *)kalloc(temp);
	new->size = temp;
	net_multicast_create(new, (unsigned char *)data,
			     &((unsigned char *)
			       data)[netp->net_status.address_size],
			     netp->net_status.address_size);

	oldpri = splimp();
	IOBUS_LOCK(&netp->net_dev_lock, IOBUS_LOCK_WAIT);
	net_multicast_insert(&netp->net_mqueue, new);
	(*netp->net_nic_ops.nic_multicast)(netp, new);
	IOBUS_UNLOCK(&netp->net_dev_lock);
	splx(oldpri);
	break;
    }

    case NET_DELMULTI:
    {
	net_multicast_t *cur;

	/*
	 * Validate multicast attribute and addresses
	 */
	if ((netp->net_status.flags & NET_STATUS_MULTICAST) == 0)
	    return (D_INVALID_OPERATION);

	if (count != netp->net_status.address_size * 2 ||
	    (((unsigned char *)data)[0] & 1) == 0 ||
	    (((unsigned char *)data)[netp->net_status.address_size] & 1) == 0)
	    return (KERN_INVALID_VALUE);

	oldpri = splimp();
	IOBUS_LOCK(&netp->net_dev_lock, IOBUS_LOCK_WAIT);
	cur = net_multicast_remove(&netp->net_mqueue,
				   (unsigned char *)data,
				   &((unsigned char *)data)
				   [netp->net_status.address_size],
				   netp->net_status.address_size);
	if (cur == (net_multicast_t *)0)
	    ret = KERN_INVALID_VALUE;
	else
	    (*netp->net_nic_ops.nic_multicast)(netp, (net_multicast_t *)0);
	IOBUS_UNLOCK(&netp->net_dev_lock);
	splx(oldpri);

	kfree((vm_offset_t)cur, cur->size);
	break;
    }

    default:
	ret = D_INVALID_OPERATION;
	break;
    }
    return (ret);
}

/*
 * net_devops_async_in
 *
 * Purpose : Network generic async_in operation
 * Returns : Device return value
 *
 * IN_arg  : dev      ==> device to set status to
 *	     port     ==> port to receive filtered packets on
 *	     priority ==> filter priority
 *	     filter   ==> filter associated to the received port
 *	     count    ==> filter size
 *
 * Comment : It is assumed that the device has alredy been opened
 */
io_return_t
net_devops_async_in(
    dev_t dev,
    ipc_port_t port,
    int priority,
    filter_t *filter,
    mach_msg_type_number_t count,
    mach_device_t device)
{
    int unit = minor(dev);
    net_device_t netp;

    NET_VALIDATE_RET_NETP(netp, unit);
    return (net_set_filter_pool(netp->net_pool, &netp->net_if,
				port, priority, filter, count, device));
}

/*
 * net_devops_dinfo
 *
 * Purpose : Network generic device info operation
 * Returns : Device return value
 *
 * IN_arg  : dev    ==> device to set status to
 *	     flavor ==> type of information
 *	     info   ==> information associated to the flavor
 *
 * Comment : It is assumed that the device has alredy been opened
 */
io_return_t
net_devops_dinfo(
    dev_t dev,
    dev_flavor_t flavor,
    char *info)
{
    int unit = minor(dev);
    io_return_t ret;
    spl_t spl;
    net_device_t netp;
    IOBUS_LOCK_STATE();

    NET_VALIDATE_RET_NETP(netp, unit);

    if (netp->net_nic_ops.nic_dinfo || netp->net_bic_ops.bic_dinfo) {
	spl = splimp();
	IOBUS_LOCK(&netp->net_dev_lock, IOBUS_LOCK_WAIT);
	ret = (*netp->net_nic_ops.nic_dinfo)(netp, flavor, info);
	if (ret == D_INVALID_OPERATION)
	    ret = (*netp->net_bic_ops.bic_dinfo)(netp, flavor, info);
	IOBUS_UNLOCK(&netp->net_dev_lock);
	splx(spl);
	return (ret);
    }
    return (D_INVALID_OPERATION);
}

#if NET_XKMACHKERNEL
/*
 * net_driver_xkalloc
 *
 * Purpose : Allocate an x-kernel buffer
 * Returns : x-kernel buffer
 *
 * IN_arg  : netp ==> network generic structure
 */
InputBuffer *
net_driver_xkalloc(
    net_device_t netp,
    InputPool **ret)
{
    InputBuffer *buf = (InputBuffer *)0;

    switch (netp->net_ptype) {
    case VCI_ETH_TYPE:	 /* VCI-zed x-kernel traffic */
    {
	VCIhdr *vciheader = (VCIhdr *)(eh + 1);
	VciType vci = vciheader->vci;
	InputPool *pool;
		    
	/* lastVci is in network byte order */
	if (vci != lastVci) {
	    vci = ntohs(vciheader->vci);
	    if (mapResolve(poolMap, &vci, &pool) == XK_FAILURE) { 
		/* 
		 * We don't have any resource 
		 * associated to this vci.
		 */
		break;
	    }
	    xAssert(pool);
	    lastVci = vciheader->vci;
	    lastPool = pool;
	}
	/* find resources */
	if (simple_lock_try(&lastPool->iplock)) {
	    if (!queue_empty(&lastPool->xkBufferPool)) {
		queue_remove_first(&lastPool->xkBufferPool, buf, 
				       InputBuffer *, link);
		*ret = lastPool;
		buf->driverProtl = netp->net_xk_self;
	    }
	    simple_unlock(&lastPool->iplock);
	} 
    }

    default:		/* regular x-kernel traffic */
	/* find resources */
	if (defaultPool) 
	    if (simple_lock_try(&defaultPool->iplock)) {
		if (!queue_empty(&defaultPool->xkBufferPool)) {
		    queue_remove_first(&defaultPool->xkBufferPool,
				       buf, InputBuffer *, link);
		    *ret = defaultPool;
		    buf->driverProtl = netp->net_xk_self;
		}
		simple_unlock(&defaultPool->iplock);
	    }
	break;
    }
    return (buf);
}

/*
 * xknet_init
 *
 * Purpose : X-kernel network generic initialization
 * Returns : X-kernel generic return value
 *
 * IN_arg  : self ==> X-kernel object
 */
xkern_return_t
xknet_init(
    XObj self)
{
    int unit;
    io_return_t ret;
    char *p;
    net_device_t netp;
    spl_t spl;

    xTrace0(xknetp, TR_GROSS_EVENTS, "xknet_init entered");

    for (p = self->instName; *p != '/' && *p != '\0'; p++)
	continue;
    if (*p == '\0') {
	printf("%s: Incorrect x-kernel driver name (unit missing) '%s'\n",
	       "xknet_init", self->instName);
	return (XK_FAILURE);
    }

    unit = 0;
    p++;
    while (*p >= '0' && *p <= '9')
	unit = (unit * 10) + (*p++ - '0');
    if (*p != '\0') {
	printf("%s: Incorrect x-kernel driver unit string '%s'\n",
	       "xknet_init", self->instName);
	return (XK_FAILURE);
    }

    if (unit > net_board_size || net_board_addr[unit] == (net_device_t) 0) {
	printf("%s: Incorrect x-kernel driver unit '%s'\n",
	       "xknet_init", self->instName);
	return (XK_FAILURE);
    }

    netp = net_board_addr[unit];
    if (netp->net_status.flags & NET_STATUS_XK) {
	printf("%s: x-kernel driver unit '%s' already initialized\n",
	       "xknet_init", self->instName);
	return (XK_FAILURE);
    }

    /* 
     * We kludge the unit number in the 'state' pointer field of the XObj.
     */
    self->state = (char *)unit;
    netp->net_xk_self = self;

    /* 
     * If the interface is in use, we respect the boundaries assigned.
     * Otherwise, we can take all of the traffic space (0, 0xFFFF).
     * This is particluarly usefull to run bootp and arp at boot time.
     */
    if (netp->net_status.flags & NET_STATUS_RUNNING) {
	netp->net_xk_low = XK_LOWEST_TYPE;
	netp->net_xk_high = XK_HIGHEST_TYPE;
    } else {
	netp->net_xk_low = 0;
	netp->net_xk_high = 0xFFFF;
    }

    spl = splimp();
    IOBUS_LOCK(&netp->net_dev_lock, IOBUS_LOCK_WAIT);
    ret = net_driver_init(netp);
    IOBUS_UNLOCK(&netp->net_dev_lock);
    splx(spl);

    if (ret != D_SUCCESS) {
	printf("%s: Unsuccessful x-kernel driver init '%s'\n",
	       "xknet_init", self->instName);
	return (XK_FAILURE);
    }

    netp->net_status.flags |= NET_STATUS_XK;

    if (allocateResources(self) != XK_SUCCESS) {
	printf("%s: can't allocate x-kernel resource for '%s'\n",
	      "xknet_init", self->instName);
	return (XK_FAILURE);
    }
    NETQ_INIT(&netp->net_xk_head, 0);
    netp->net_xk_mda = pathGetAlloc(self->path, MD_ALLOC);

    /*
     * Set up protocol operations
     */
    self->push = xknet_push;
    self->control = xknet_control;
    self->openenable = xknet_openenable;

    xTrace0(xknetp, TR_GROSS_EVENTS, "xknet_init returns");
    return (XK_SUCCESS);
}

/*
 * xknet_push
 *
 * Purpose : X-kernel network generic write
 * Returns : X-kernel generic handle return value
 *
 * IN_arg  : self    ==> X-kernel object
 *	     downmsg ==> Msg to write on the network
 */
xmsg_handle_t
xknet_push(
    XObj self,
    Msg downMsg)
{
    spl_t spl;
    net_device_t netp;
    net_done_t netd;
    struct net_xk_output *outp;
    vm_size_t len;
    IOBUS_LOCK_STATE();

    NET_VALIDATE_XK_NETP(netp, self, "xknet_push");

    switch (netp->net_status.media_type) {
    case NET_MEDIA_OTHERS:
	if (netp->net_status.header_size != sizeof (struct if_myrinet_eep)) {
	    xTraceP0(self, TR_SOFT_ERRORS,
		     "xknet_push: unsupported media type");
	    return (XMSG_ERR_HANDLE);
	}
	/*FALL THRU */
	    
    case NET_MEDIA_ETHERNET:
    {
	ETHhost *dest = &((ETHhdr *)msgGetAttr(downMsg, 0))->dst;
	xTrace3(xknetp, TR_EVENTS, "xknet_push: send to %x.%x.%x",
		dest->high, dest->mid, dest->low);
	if (netp->net_status.header_size == sizeof (ETHhdr))
	    break;
	xTraceP0(self, TR_SOFT_ERRORS,
		 "xknet_push: Ethernet header lengths differ (%d/%d)");
	return (XMSG_ERR_HANDLE);
    }

    default:
	xTraceP0(self, TR_SOFT_ERRORS, "xknet_push: unsupported media type");
	return (XMSG_ERR_HANDLE);
    }

    /*
     * Reject the write if the packet is too large or too small.
     */
    len = msgLen(downMsg) + netp->net_status.header_size;
    if (len > netp->net_status.max_packet_size) {
	printf("%s: incorrect output packet size (%d bytes) on %s%d\n",
	       "xknet_push", len, netp->net_name, netp->net_unit);
	return (XMSG_ERR_HANDLE);
    }

    /*
     * Construct packet header
     */
    spl = splimp();
    outp = pathAlloc(self->path,
		     sizeof (*outp) + netp->net_osize +
		     netp->net_status.header_size);
    splx(spl);
    if (outp = (struct net_xk_output *)0) {
	xTraceP0(self, TR_SOFT_ERRORS, "xknet_push: allocation error");
	return (XMSG_ERR_HANDLE);
    }

    msgConstructCopy(&outp->xk_msg, downMsg);
    msgSetAttr(&outp->xk_msg, 0,
	       ((char *)(outp + 1)) + netp->net_osize,
		netp->net_status.header_size);
    memcpy((char *)msgGetAttr(&outp->xk_msg, 0),
	   (const char *)msgGetAttr(downMsg, 0), netp->net_status.header_size);

    /*
     * Initialize net_done element
     */
    netd = &outp->xk_done;
    netd->netd_xk = outp;
    netd->netd_module = NET_MODULE_XKERNEL;
    netd->netd_done = (void (*)(net_device_t, net_done_t))0;
    netd->netd_external = &outp + 1;
    netd->netd_outlen = 0;
    netd->netd_ptype = net_driver_addrtype(netp,
					   (char *)msgGetAttr(downMsg, 0), 0);
    if (len < netp->net_status.min_packet_size)
	netd->netd_length = netp->net_status.min_packet_size;
    else
	netd->netd_length = len;

    /*
     * Validate packet to be copied out
     */
    if (netp->net_nic_ops.nic_prepout) {
	msgForEachAlternate(&netd->netd_xk->xk_msg,
			    xknet_prepout, xknet_usrprepout, netp);
	if (netd->netd_outlen < netd->netd_length)
	    (*netp->net_nic_ops.nic_prepout)
		(netp, netd, 0, netd->netd_length - len,
		 NET_BICOUT_FLAGS_LAST | NET_BICOUT_FLAGS_PADDING);
	netd->netd_outlen = 0;
    }

    /*
     * Enqueue message and start the device
     */
    spl = splimp();
    NET_ENQUEUE(&netp->net_xk_head, netd, TRUE);

    if (IOBUS_LOCK(&netp->net_dev_lock, IOBUS_LOCK_START)) {
	net_driver_event(netp);
	IOBUS_UNLOCK(&netp->net_dev_lock);
    }
    splx(spl);

    return (XMSG_NULL_HANDLE);
}

/*
 * xknet_control
 *
 * Purpose : X-kernel network generic control operation
 * Returns : length written to buf
 *
 * IN_arg  : self ==> X-kernel object
 *	     op   ==> X-kernel operation
 *	     buf  ==> Output buffer
 *	     len  ==> Length of output buffer
 */
int
xknet_control(
    XObj  self,
    int   op,
    char *buf,
    int   len)
{  
    net_device_t netp;

    NET_VALIDATE_XK_NETP(netp, self, "xknet_control");
    xTrace1(xknetp, TR_FULL_TRACE, "xknet_control: operation 0x%x", op);

    switch (op) {
    case GETMYHOST:
	if (len < netp->net_status.address_size)
	    break;
	bcopy((char *)netp->net_address, buf, netp->net_status.address_size);
	return (netp->net_status.address_size);

    default:
	break;
    }

    return (-1);
}

/*
 * xknet_openenable
 *
 * Purpose : X-kernel network generic x-kernel operations
 * Returns : X-kernel generic return value
 *
 * IN_arg  : self    ==> X-kernel object
 *	     hlp     ==> upper X-kernel object
 *	     hlptype ==> lower X-kernel object
 *	     part    ==> list of participant (to demultiplex)
 *
 * Comment : The openenable interface allows the driver get the address
 *		of the higher level protocol to which it interfaces
 */
xkern_return_t
xknet_openenable(
    XObj self, 
    XObj hlp, 
    XObj hlptype,
    Part part)
{ 
    net_device_t netp;

    NET_VALIDATE_XK_NETP(netp, self, "xknet_openenable");

    xSetUp(self, hlp);
    return (XK_SUCCESS);
}

/*
 * xknet_copyout
 *
 * Purpose : X-kernel buffer copyout
 * Returns : boolean if the copyout can continue
 *
 * IN_arg  : addr ==> output virtual address
 *	     len  ==> output length
 *	     ptr  ==> pointer to the netp structure
 */
boolean_t
xknet_copyout(
    char *from,
    long len,
    void *ptr)
{
    net_device_t netp = (net_device_t)ptr;
    net_done_t netd = netp->net_opacket;

    if (len == 0)
	return (TRUE);

    if (netp->net_xk_olen < netd->netd_outlen) {
	netp->net_xk_olen += len;
	return (TRUE);
    }

    if (netp->net_xk_olen != netd->netd_outlen) {
	printf("xknet_copyout: synchronization failed (%d/%d)",
	      netd->netd_outlen, netp->net_xk_olen);
	if (net_driver_reset(netp) == D_SUCCESS)
	    (void)net_driver_init(netp);
	return (FALSE);
    }

    /*
     * Restart copyout
     */
    netd->netd_outlen += len;
    switch ((*netp->net_bic_ops.bic_copyout)
	    (netp, netd, (vm_offset_t)from, len,
	     NET_BICOUT_FLAGS_VIRTUAL |
	     (netd->netd_outlen < netd->netd_length ?
	      0 : NET_BICOUT_FLAGS_LAST))) {
    case NET_BICOUT_ERROR:		/* error */
	if (net_driver_reset(netp) == D_SUCCESS)
	    (void)net_driver_init(netp);
	break;

    case NET_BICOUT_IN_PROGRESS:	/* copyout in progress */
	break;

    case NET_BICOUT_COMPLETED:		/* copyout completed */
	netp->net_xk_olen += len;
	return (TRUE);
    }
    return (FALSE);
}

/*
 * xknet_usrcopyout
 *
 * Purpose : X-kernel Inplace buffer copyout
 * Returns : boolean if the copyout can continue
 *
 * IN_arg  : addr ==> output virtual address
 *	     len  ==> output length
 *	     ptr  ==> pointer to the netp structure
 */
boolean_t
xknet_usrcopyout(
    char *from,
    long len,
    void *ptr)
{
    /* 
     * Validate source address (in case of messages constructed in place,
     * users may have withdrawn the bits)
     */
    if (pmap_extract(kernel_pmap, (vm_offset_t)from) == (vm_offset_t)0)
	return (FALSE);
    return (xknet_copyout(from, len, ptr));
}

/*
 * xknet_copyin
 *
 * Purpose : X-kernel buffer copyin
 * Returns : boolean if the copyin can continue
 *
 * IN_arg  : addr ==> input virtual address
 *	     len  ==> input length
 *	     ptr  ==> pointer to the netp structure
 */
boolean_t
xknet_copyin(
    char *from,
    long len,
    void *ptr)
{
    net_device_t netp = (net_device_t)ptr;

    if (len == 0)
	return (TRUE);

    if (netp->net_xk_input != (InputBuffer *)0)
	bcopy(from, &netp->net_xk_input->data[netp->net_ilen], len);
    else {
	assert(netp->net_kmsg != (ipc_kmsg_t)0);
	bcopy(from, (net_kmsg(netp->net_kmsg)->packet +
		     sizeof (struct packet_header) + netp->net_ilen), len);
    }
    netp->net_ilen += len;
    return (TRUE);
}

/*
 * xknet_usrcopyin
 *
 * Purpose : X-kernel Inplace buffer copyin
 * Returns : boolean if the copyin can continue
 *
 * IN_arg  : addr ==> input virtual address
 *	     len  ==> input length
 *	     ptr  ==> pointer to the netp structure
 */
boolean_t
xknet_usrcopyin(
    char *from,
    long len,
    void *ptr)
{
    /* 
     * Validate source address (in case of messages constructed in place,
     * users may have withdrawn the bits)
     */
    if (pmap_extract(kernel_pmap, (vm_offset_t)from) == (vm_offset_t)0)
	return (FALSE);
    return (xknet_copyin(from, len, ptr));
}

/*
 * xknet_prepout
 *
 * Purpose : X-kernel preparation of buffer copyout
 * Returns : TRUE
 *
 * IN_arg  : addr ==> input virtual address
 *	     len  ==> input length
 *	     ptr  ==> pointer to the netp structure
 */
boolean_t
xknet_prepout(
    char *from,
    long len,
    void *ptr)
{
    net_device_t netp = (net_device_t)ptr;
    net_done_t netd = netp->net_opacket;

    if (len > 0) {
	netd->netd_outlen += len;
	(*netp->net_nic_ops.nic_prepout)
	    (netp, netd, (vm_offset_t)from, len,
	     NET_BICOUT_FLAGS_VIRTUAL |
	     (netd->netd_outlen < netd ->netd_length ?
	      0 : NET_BICOUT_FLAGS_LAST));
    }
    return (TRUE);
}

/*
 * xknet_usrprepout
 *
 * Purpose : X-kernel preparation of Inplace buffer copyout
 * Returns : TRUE
 *
 * IN_arg  : addr ==> input virtual address
 *	     len  ==> input length
 *	     ptr  ==> pointer to the netp structure
 */
boolean_t
xknet_usrprepout(
    char *from,
    long len,
    void *ptr)
{
    return (xknet_prepout(from, len, ptr));
}
#endif /* NET_XKMACHKERNEL */
