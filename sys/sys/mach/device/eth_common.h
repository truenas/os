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
 * Utilities to be used by non-DMA Ethernet drivers.
 * These utilities are fully machine-independent.
 * Besides, they encapsulate the mechanisms which
 * depend upon the networking architecture: Mach IPC 
 * interface versus x-kernel programming model. The
 * utilities are meant to make the driver adaptation
 * simpler and more effective.
 *
 * Support for non-Ethernet media to be verified.
 * Check the CDLI interface work.
 */ 

#ifndef eth_common_h
#define eth_common_h

#include        <xkmachkernel.h>
#include        <xk_debug.h>

#if XKMACHKERNEL
#include	<uk_xkern/include/eth_support.h>

VciType lastVci = -1;   /* static ! */
InputPool *lastPool;    /* static ! */

#endif /* XKMACHKERNEL */


typedef char header_t[sizeof(struct ether_header) + 6];  

static __inline__ kern_return_t eth_resources(
        boolean_t               is_xk_type,
	header_t		*eh,
	unsigned short 		type,
	unsigned short 		len,
	char			**base,
	char			**header,
	char			**body);

static __inline__ void eth_dispatch(
        boolean_t               is_xk_type,
	struct ifnet		*ifp,
	unsigned short 		type,
	unsigned short 		len,
	int			unit,
	char			*base,
	io_req_t		ior);

static __inline__ boolean_t eth_tobecloned(
        boolean_t               is_xk_type,
        unsigned short		type);


static __inline__ kern_return_t
eth_resources(
        boolean_t               is_xk_type,
	header_t		*hs,
	unsigned short 		type,
	unsigned short 		len,
	char			**base,
	char			**header,
	char			**body)
{
        struct ether_header *eh = (struct ether_header *)hs;
	kern_return_t ret = KERN_RESOURCE_SHORTAGE;

	/* type is already ntohs-converted */
#if     XKMACHKERNEL
	if (is_xk_type)  {
            InputBuffer     *buf;

	    switch (type) {
	    default:		/* regular x-kernel traffic */
		{
                    /* find resources */
                    if (defaultPool) 
			if (simple_lock_try(&defaultPool->iplock)) {
			    if (!queue_empty(&defaultPool->xkBufferPool)) {
				queue_remove_first(&defaultPool->xkBufferPool, buf, 
						   InputBuffer *, link);
				*base = (char *)buf;
				*header = buf->hdr;
				*body = buf->data;
				ret = KERN_SUCCESS;
			    }
			    simple_unlock(&defaultPool->iplock);
			}
		} 
		break;
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
			    *base = (char *)buf;
			    *header = buf->hdr;
			    *body = buf->data;
			    ret = KERN_SUCCESS;
			}
			simple_unlock(&lastPool->iplock);
                    } 
		} 
		break;
	    }
        } else
#endif   /* XKMACHKERNEL */
	{
	    ipc_kmsg_t	new_kmsg;
	    struct packet_header *pkt;

	    new_kmsg = net_kmsg_get();
	    if (new_kmsg != IKM_NULL) {
		*base = (char *) new_kmsg;
		*header = (char *) (&net_kmsg(new_kmsg)->header[0]);
		pkt = (struct packet_header *) 
		    (&net_kmsg(new_kmsg)->packet[0]);
		pkt->type = eh->ether_type; /* network format */
		pkt->length = len + sizeof(struct packet_header);
		pkt++;
		*body = (char *)pkt;
		ret = KERN_SUCCESS;
	    }
	}
	/*
	 * Note: KERN_RESOURCE_SHORTAGE can be returned as the result
	 * of a conflict on the InputPool's simple lock. Should we
         * detect that conflicts are frequent, we will invest on a
	 * lock free queueing mechanism.
	 */
	return ret;
}

#if XK_DEBUG
int xk_input_wakeup;
int xk_input_queueing;
#endif


/*
 * Observe that this procedure gets called iff eth_resources() 
 * has returned KERN_SUCCESS.
 */
static __inline__ void
eth_dispatch(
        boolean_t               is_xk_type,
	struct ifnet		*ifp,
	unsigned short 		type,
	unsigned short 		len,
	int			unit,
	char			*base,
	io_req_t		ior)
{
	/* type is already ntohs-converted */
#if     XKMACHKERNEL
	if (is_xk_type)  {

	    XObj     self = self_protocol[unit];
	    InputBuffer *buf = (InputBuffer *)base;
	    InputPool *pool = 
		(type == VCI_ETH_TYPE)? lastPool : defaultPool;

	    buf->driverProtl = self;
	    buf->len = len;

	    /*
	     * No conditional locking here: if we execute this code,
	     * it means that eth_resources succeded. Hence, no one
	     * can else could sneak in and grab this lock.
	     */
	    simple_lock(&pool->iplock);
	    queue_enter(&pool->xkIncomingData, buf, InputBuffer *, link);
	    if (more_input_thread(pool)) 
		thread_wakeup_one((event_t) pool);
	    simple_unlock(&pool->iplock);
	} else
#endif   /* XKMACHKERNEL */
	{
	    net_packet(ifp, 
		       (ipc_kmsg_t) base, 
		       len + sizeof(struct packet_header),
                       ethernet_priority((ipc_kmsg_t) base),
			ior);
	} 
}

static __inline__ boolean_t
eth_tobecloned(
	       boolean_t                is_xk_type,
	       unsigned short		type)
{
#if     XKMACHKERNEL
    if (!is_xk_type) {
	switch (type) {
	case 0x0806:            /* ARP,  to become a define */
	case 0x8035:            /* RARP, to become a define */
	    return (1);
	}
    }
#endif  /* XKMACHKERNEL */
    return (0);
}

#endif /* eth_common_h */



