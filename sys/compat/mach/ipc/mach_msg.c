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
 * Revision 2.16.3.2  92/03/03  16:19:04  jeffreyh
 * 	Changes from TRUNK
 * 	[92/02/26  11:52:59  jeffreyh]
 * 
 * Revision 2.17  92/01/03  20:13:19  dbg
 * 	Add quick dispatch to Mach Kernel messages.
 * 	[91/12/18            dbg]
 * 
 * Revision 2.16  91/12/14  14:28:41  jsb
 * 	Removed ipc_fields.h hack.
 * 
 * Revision 2.15  91/11/14  16:58:17  rpd
 * 	Added ipc_fields.h hack.
 *	Use IP_NORMA_IS_PROXY macro instead of ipc_space_remote.
 * 	[91/11/00            jsb] 
 * 
 * Revision 2.14  91/10/09  16:11:23  af
 * 	Added <ipc/ipc_notify.h>.  Fixed type-mismatch in msg_rpc_trap.
 * 	[91/09/02            rpd]
 * 
 * Revision 2.13  91/08/28  11:13:53  jsb
 * 	Changed MACH_RCV_TOO_LARGE and MACH_RCV_INVALID_NOTIFY to work
 * 	like MACH_RCV_HEADER_ERROR, using ipc_kmsg_copyout_dest.
 * 	[91/08/12            rpd]
 * 
 * 	Added seqno argument to ipc_mqueue_receive.
 * 	Updated mach_msg_trap fast path for seqno processing.
 * 	[91/08/10            rpd]
 * 	Fixed mach_msg_interrupt to check for MACH_RCV_IN_PROGRESS.
 * 	[91/08/03            rpd]
 * 	Renamed clport things to norma_ipc things.
 * 	[91/08/15  08:24:12  jsb]
 * 
 * Revision 2.12  91/07/31  17:43:41  dbg
 * 	Add mach_msg_interrupt to force a thread waiting in mach_msg_continue
 * 	or mach_msg_receive_continue into a stable state.
 * 	[91/07/30  17:02:11  dbg]
 * 
 * Revision 2.11  91/06/25  10:27:47  rpd
 * 	Fixed ikm_cache critical sections to avoid blocking operations.
 * 	[91/05/23            rpd]
 * 
 * Revision 2.10  91/06/17  15:46:33  jsb
 * 	Renamed NORMA conditionals.
 * 	[91/06/17  10:46:35  jsb]
 * 
 * Revision 2.9  91/06/06  17:06:12  jsb
 * 	A little more NORMA_IPC support.
 * 	[91/05/13  17:22:08  jsb]
 * 
 * Revision 2.8  91/05/14  16:38:44  mrt
 * 	Correcting copyright
 * 
 * Revision 2.7  91/03/16  14:49:09  rpd
 * 	Replaced ipc_thread_switch with thread_handoff.
 * 	Replaced ith_saved with ikm_cache.
 * 	[91/02/16            rpd]
 * 	Made null mach_msg_trap measurement easier.
 * 	[91/01/29            rpd]
 * 
 * Revision 2.6  91/02/05  17:24:37  mrt
 * 	Changed to new Mach copyright
 * 	[91/02/01  15:53:02  mrt]
 * 
 * Revision 2.5  91/01/08  15:15:03  rpd
 * 	Added KEEP_STACKS support.
 * 	[91/01/07            rpd]
 * 	Changed to use thread_syscall_return.
 * 	Added msg_receive_continue.
 * 	[90/12/18            rpd]
 * 	Added mach_msg_continue, mach_msg_receive_continue.
 * 	Changes to support kernel stack discarding/hand-off.
 * 	[90/12/09  17:29:04  rpd]
 * 
 * 	Removed MACH_IPC_GENNOS.
 * 	[90/11/08            rpd]
 * 
 * Revision 2.4  90/12/14  11:01:36  jsb
 * 	Added NORMA_IPC support: always ipc_mqueue_send() to a remote port.
 * 	[90/12/13  21:25:47  jsb]
 * 
 * Revision 2.3  90/11/05  14:30:29  rpd
 * 	Removed ipc_object_release_macro.
 * 	Changed ip_reference to ipc_port_reference.
 * 	Changed ip_release to ipc_port_release.
 * 	Changed io_release to ipc_object_release.
 * 	Use new io_reference and io_release.
 * 	Use new ip_reference and ip_release.
 * 	[90/10/29            rpd]
 * 
 * Revision 2.2  90/06/02  14:52:22  rpd
 * 	Created for new IPC.
 * 	[90/03/26  21:05:49  rpd]
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
 *	File:	ipc/mach_msg.c
 *	Author:	Rich Draves
 *	Date:	1989
 *
 *	Exported message traps.  See mach/message.h.
 */

#if 0
#include <cpus.h>
#include <dipc.h>
#include <mach_rt.h>
#endif

#include <sys/mach/kern_return.h>
#include <sys/mach/port.h>
#include <sys/mach/message.h>
#include <sys/mach/mig_errors.h>
#if 0
#include <kern/assert.h>
#include <kern/counters.h>
#include <kern/cpu_number.h>
#include <kern/lock.h>
#endif
#if 0
#include <kern/ipc_sched.h>
#include <kern/exception.h>
#include <kern/misc_protos.h>
#include <vm/vm_map.h>
#endif
#include <sys/mach/ipc/ipc_kmsg.h>
#include <sys/mach/ipc/ipc_mqueue.h>
#include <sys/mach/ipc/ipc_object.h>
#include <sys/mach/ipc/ipc_notify.h>
#include <sys/mach/ipc/ipc_port.h>
#include <sys/mach/ipc/ipc_pset.h>
#include <sys/mach/ipc/ipc_space.h>
#include <sys/mach/ipc/ipc_thread.h>
#include <sys/mach/ipc/ipc_entry.h>
#include <sys/mach/ipc/mach_msg.h>
#include <sys/mach/thread.h>
#include <sys/mach/sched_prim.h>
#include <sys/mach/ipc_kobject.h>

#if 0
#include <kern/kalloc.h>
#include <kern/thread_swap.h>
#endif
#if	MACH_RT
#include <kern/rtalloc.h>
#endif	/* MACH_RT */

#if	DIPC
#include <dipc/dipc_funcs.h>
#include <dipc/dipc_port.h>
#endif	/* DIPC */

#define THREAD_SWAPPER 0

/*
 * Forward declarations
 */

mach_msg_return_t mach_msg_send(
	mach_msg_header_t	*msg,
	mach_msg_option_t	option,
	mach_msg_size_t		send_size,
	mach_msg_timeout_t	timeout,
	mach_port_name_t		notify);

mach_msg_return_t mach_msg_receive(
	mach_msg_header_t	*msg,
	mach_msg_option_t	option,
	mach_msg_size_t		rcv_size,
	mach_port_name_t		rcv_name,
	mach_msg_timeout_t	timeout,
	mach_port_name_t		notify,
	mach_msg_size_t		slist_size);

mach_msg_return_t msg_receive_error(
	ipc_kmsg_t		kmsg,
	mach_msg_header_t	*msg,
	mach_msg_option_t	option,
	mach_port_seqno_t	seqno,
	ipc_space_t		space);

/* the size of each trailer has to be listed here for copyout purposes */
mach_msg_trailer_size_t trailer_size[] = {
          sizeof(mach_msg_trailer_t), 
	  sizeof(mach_msg_seqno_trailer_t),
	  sizeof(mach_msg_security_trailer_t) };

security_token_t KERNEL_SECURITY_TOKEN = KERNEL_SECURITY_TOKEN_VALUE;

mach_msg_format_0_trailer_t trailer_template = {
	/* mach_msg_trailer_type_t */ MACH_MSG_TRAILER_FORMAT_0,
	/* mach_msg_trailer_size_t */ MACH_MSG_TRAILER_MINIMUM_SIZE,
        /* mach_port_seqno_t */       0,
	/* security_token_t */        KERNEL_SECURITY_TOKEN_VALUE
};

/*
 *	Routine:	mach_msg_send
 *	Purpose:
 *		Send a message.
 *	Conditions:
 *		Nothing locked.
 *	Returns:
 *		MACH_MSG_SUCCESS	Sent the message.
 *		MACH_SEND_MSG_TOO_SMALL	Message smaller than a header.
 *		MACH_SEND_NO_BUFFER	Couldn't allocate buffer.
 *		MACH_SEND_INVALID_DATA	Couldn't copy message data.
 *		MACH_SEND_INVALID_HEADER
 *			Illegal value in the message header bits.
 *		MACH_SEND_INVALID_DEST	The space is dead.
 *		MACH_SEND_INVALID_NOTIFY	Bad notify port.
 *		MACH_SEND_INVALID_DEST	Can't copyin destination port.
 *		MACH_SEND_INVALID_REPLY	Can't copyin reply port.
 *		MACH_SEND_TIMED_OUT	Timeout expired without delivery.
 *		MACH_SEND_INTERRUPTED	Delivery interrupted.
 *		MACH_SEND_NO_NOTIFY	Can't allocate a msg-accepted request.
 *		MACH_SEND_WILL_NOTIFY	Msg-accepted notif. requested.
 *		MACH_SEND_NOTIFY_IN_PROGRESS
 *			This space has already forced a message to this port.
 */

mach_msg_return_t
mach_msg_send(
	mach_msg_header_t	*msg,
	mach_msg_option_t	option,
	mach_msg_size_t		send_size,
	mach_msg_timeout_t	timeout,
	mach_port_name_t	notify)
{
	ipc_space_t space = current_space();
	vm_map_t map = current_map();
	ipc_kmsg_t kmsg;
	mach_msg_return_t mr;

	mr = ipc_kmsg_get(msg, send_size, &kmsg, space);

	if (mr != MACH_MSG_SUCCESS)
		return mr;
#if 0
	if (option & MACH_SEND_CANCEL) {
		if (notify == MACH_PORT_NULL)
			mr = MACH_SEND_INVALID_NOTIFY;
		else
			mr = ipc_kmsg_copyin(kmsg, space, map, notify);
	} else
#endif		
		mr = ipc_kmsg_copyin(kmsg, space, map, MACH_PORT_NAME_NULL);
	if (mr != MACH_MSG_SUCCESS) {
		ikm_free(kmsg);
		return mr;
	}

	mr = ipc_mqueue_send(kmsg, option & MACH_SEND_TIMEOUT, timeout);

	if (mr != MACH_MSG_SUCCESS) {
	    mr |= ipc_kmsg_copyout_pseudo(kmsg, space, map, MACH_MSG_BODY_NULL);
	    (void) ipc_kmsg_put(msg, kmsg, kmsg->ikm_header.msgh_size);
	}

	return mr;
}

#define FREE_SCATTER_LIST(s, l, rt) 			\
MACRO_BEGIN						\
	if((s) != MACH_MSG_BODY_NULL) { 		\
		KFREE(((vm_offset_t)(s)), (l), rt);	\
	}						\
MACRO_END

/*
 *	Routine:	mach_msg_receive
 *	Purpose:
 *		Receive a message.
 *	Conditions:
 *		Nothing locked.
 *	Returns:
 *		MACH_MSG_SUCCESS	Received a message.
 *		MACH_RCV_INVALID_NAME	The name doesn't denote a right,
 *			or the denoted right is not receive or port set.
 *		MACH_RCV_IN_SET		Receive right is a member of a set.
 *		MACH_RCV_TOO_LARGE	Message wouldn't fit into buffer.
 *		MACH_RCV_TIMED_OUT	Timeout expired without a message.
 *		MACH_RCV_INTERRUPTED	Reception interrupted.
 *		MACH_RCV_PORT_DIED	Port/set died while receiving.
 *		MACH_RCV_PORT_CHANGED	Port moved into set while receiving.
 *		MACH_RCV_INVALID_DATA	Couldn't copy to user buffer.
 *		MACH_RCV_INVALID_NOTIFY	Bad notify port.
 *		MACH_RCV_HEADER_ERROR
 */

mach_msg_return_t
mach_msg_receive(
	mach_msg_header_t	*msg,
	mach_msg_option_t	option,
	mach_msg_size_t		rcv_size,
	mach_port_name_t		rcv_name,
	mach_msg_timeout_t	timeout,
	mach_port_name_t		notify,
	mach_msg_size_t		slist_size)
{
	ipc_thread_t self = current_thread();
	ipc_space_t space = current_space();
	vm_map_t map = current_map();
	ipc_object_t object;
	ipc_mqueue_t mqueue;
	ipc_kmsg_t kmsg;
	mach_port_seqno_t seqno;
	mach_msg_return_t mr;
	mach_msg_body_t *slist;
	mach_msg_format_0_trailer_t *trailer;
#if	MACH_RT
	boolean_t slist_rt;
#endif	/* MACH_RT */

	mr = ipc_mqueue_copyin(space, rcv_name, &mqueue, &object);
	if (mr != MACH_MSG_SUCCESS) {
		return mr;
	}
	/* hold ref for object; mqueue is locked */

#if	MACH_RT
	/* NOTE: This only works for true ports, and not port sets */
	slist_rt = ipc_object_is_rt(object);
#endif	/* MACH_RT */

	/*
 	 * If MACH_RCV_OVERWRITE was specified, both receive_msg (msg)
	 * and receive_msg_size (slist_size) need to be non NULL.
	 */
	if (option & MACH_RCV_OVERWRITE) {
		if (slist_size < sizeof(mach_msg_base_t)) {
			imq_unlock(mqueue);
			ipc_object_release(object);
			return MACH_RCV_SCATTER_SMALL;
		} else {
			slist_size -= sizeof(mach_msg_header_t);
			imq_unlock(mqueue);
			slist = (mach_msg_body_t *)KALLOC(slist_size, slist_rt);
			if (slist == MACH_MSG_BODY_NULL ||
			    copyin((char *) (msg + 1), (char *)slist,
					slist_size)) {
				ipc_object_release(object);
				return MACH_RCV_INVALID_DATA;
			}
			if ((slist->msgh_descriptor_count*
			     sizeof(mach_msg_descriptor_t)
			     + sizeof(mach_msg_size_t)) > slist_size) {
				FREE_SCATTER_LIST(slist, slist_size, slist_rt);
				ipc_object_release(object);
				return MACH_RCV_INVALID_TYPE;
			}
			imq_lock(mqueue);
		}
	} else {
		slist = MACH_MSG_BODY_NULL;
	}
	
	self->ith_option = option;
	self->ith_scatter_list = slist;
	self->ith_scatter_list_size = slist_size;

	mr = ipc_mqueue_receive(mqueue, option & MACH_RCV_TIMEOUT, rcv_size,
				timeout, FALSE,
				(void(*)(void))SAFE_EXTERNAL_RECEIVE,
				&kmsg, &seqno);
	
	/* mqueue is unlocked */
	ipc_object_release(object);

	if (mr != MACH_MSG_SUCCESS) {
		if (mr == MACH_RCV_TOO_LARGE || mr == MACH_RCV_SCATTER_SMALL
#if	DIPC
		    || mr == MACH_RCV_TRANSPORT_ERROR
#endif	/* DIPC */
		    ) {
			if (msg_receive_error(kmsg, msg, option, seqno, space)
			    == MACH_RCV_INVALID_DATA)
				mr = MACH_RCV_INVALID_DATA;
		}
		FREE_SCATTER_LIST(slist, slist_size, slist_rt);
		return mr;
	}
	trailer = (mach_msg_format_0_trailer_t *)
			((vm_offset_t)&kmsg->ikm_header +
			round_msg(kmsg->ikm_header.msgh_size));
	if (option & MACH_RCV_TRAILER_MASK) {
		trailer->msgh_seqno = seqno;
		trailer->msgh_trailer_size = REQUESTED_TRAILER_SIZE(option);
	}

	if (option & MACH_RCV_NOTIFY) {
		if (notify == MACH_PORT_NAME_NULL)
			mr = MACH_RCV_INVALID_NOTIFY;
		else
			mr = ipc_kmsg_copyout(kmsg, space, map, notify, slist);
	} else {
		mr = ipc_kmsg_copyout(kmsg, space, map, MACH_PORT_NAME_NULL, slist);
	}
	if (mr != MACH_MSG_SUCCESS) {
		if ((mr &~ MACH_MSG_MASK) == MACH_RCV_BODY_ERROR
#if	DIPC
			|| mr == MACH_RCV_TRANSPORT_ERROR
#endif	/* DIPC */
		    ) {
			if (ipc_kmsg_put(msg, kmsg, kmsg->ikm_header.msgh_size +
			   trailer->msgh_trailer_size) == MACH_RCV_INVALID_DATA)
				mr = MACH_RCV_INVALID_DATA;
		} 
		else {
			if (msg_receive_error(kmsg, msg, option, seqno, space) 
						== MACH_RCV_INVALID_DATA)
				mr = MACH_RCV_INVALID_DATA;
		}

		FREE_SCATTER_LIST(slist, slist_size, slist_rt);
		return mr;
	}
	mr = ipc_kmsg_put(msg, kmsg, 
		kmsg->ikm_header.msgh_size + trailer->msgh_trailer_size);
	FREE_SCATTER_LIST(slist, slist_size, slist_rt);

	return mr;
}


/*
 * Toggle this to compile the hotpath in/out
 * If compiled in, the run-time toggle "enable_hotpath" below
 * eases testing & debugging
 */
#if	NCPUS == 1 && !MACH_RT
#define ENABLE_HOTPATH 1
#else	/* NCPUS == 1 && !MACH_RT */
#define ENABLE_HOTPATH 0
#endif	/* NCPUS == 1 && !MACH_RT */

#if	ENABLE_HOTPATH
/*
 * These counters allow tracing of hotpath behavior under test loads.
 * A couple key counters are unconditional (see below).
 */
#define	HOTPATH_DEBUG	0	/* Toggle to include lots of counters	*/
#if	HOTPATH_DEBUG
#define HOT(expr)	expr

unsigned int c_mmot_FIRST = 0;			/* Unused First Counter	*/
unsigned int c_mmot_combined_S_R = 0;		/* hotpath candidates	*/
unsigned int c_mach_msg_trap_switch_fast = 0;	/* hotpath successes	*/
unsigned int c_mmot_ikm_cache_miss = 0;		/* Reasons to Fall Off:	*/
unsigned int c_mmot_kernel_send = 0;		/*    kernel server	*/
unsigned int c_mmot_cold_000 = 0;		/*    see below ...	*/
unsigned int c_mmot_smallsendsize = 0;
unsigned int c_mmot_oddsendsize = 0;
unsigned int c_mmot_bigsendsize = 0;
unsigned int c_mmot_copyinmsg_fail = 0;
unsigned int c_mmot_g_slow_copyin3 = 0;
unsigned int c_mmot_cold_006 = 0;
unsigned int c_mmot_cold_007 = 0;
unsigned int c_mmot_cold_008 = 0;
unsigned int c_mmot_cold_009 = 0;
unsigned int c_mmot_cold_010 = 0;
unsigned int c_mmot_cold_012 = 0;
unsigned int c_mmot_cold_013 = 0;
unsigned int c_mmot_cold_014 = 0;
unsigned int c_mmot_cold_016 = 0;
unsigned int c_mmot_cold_018 = 0;
unsigned int c_mmot_cold_019 = 0;
unsigned int c_mmot_cold_020 = 0;
unsigned int c_mmot_cold_021 = 0;
unsigned int c_mmot_cold_022 = 0;
unsigned int c_mmot_cold_023 = 0;
unsigned int c_mmot_cold_024 = 0;
unsigned int c_mmot_cold_025 = 0;
unsigned int c_mmot_cold_026 = 0;
unsigned int c_mmot_cold_027 = 0;
unsigned int c_mmot_hot_fSR_ok = 0;
unsigned int c_mmot_cold_029 = 0;
unsigned int c_mmot_cold_030 = 0;
unsigned int c_mmot_cold_031 = 0;
unsigned int c_mmot_cold_032 = 0;
unsigned int c_mmot_cold_033 = 0;
unsigned int c_mmot_bad_rcvr = 0;
unsigned int c_mmot_rcvr_swapped = 0;
unsigned int c_mmot_rcvr_locked = 0;
unsigned int c_mmot_rcvr_tswapped = 0;
unsigned int c_mmot_rcvr_freed = 0;
unsigned int c_mmot_g_slow_copyout6 = 0;
unsigned int c_mmot_g_slow_copyout5 = 0;
unsigned int c_mmot_cold_037 = 0;
unsigned int c_mmot_cold_038 = 0;
unsigned int c_mmot_cold_039 = 0;
unsigned int c_mmot_g_slow_copyout4 = 0;
unsigned int c_mmot_g_slow_copyout3 = 0;
unsigned int c_mmot_hot_ok1 = 0;
unsigned int c_mmot_hot_ok2 = 0;
unsigned int c_mmot_hot_ok3 = 0;
unsigned int c_mmot_g_slow_copyout1 = 0;
unsigned int c_mmot_g_slow_copyout2 = 0;
unsigned int c_mmot_getback_fast_copyin = 0;
unsigned int c_mmot_cold_048 = 0;
unsigned int c_mmot_getback_FastSR = 0;
unsigned int c_mmot_cold_050 = 0;
unsigned int c_mmot_cold_051 = 0;
unsigned int c_mmot_cold_052 = 0;
unsigned int c_mmot_cold_053 = 0;
unsigned int c_mmot_fastkernelreply = 0;
unsigned int c_mmot_cold_055 = 0;
unsigned int c_mmot_getback_fast_put = 0;
unsigned int c_mmot_LAST = 0;			/* End Marker - Unused */

void db_mmot_zero_counters(void);		/* forward; */
void db_mmot_show_counters(void);		/* forward; */

void			/* Call from the debugger to clear all counters	*/
db_mmot_zero_counters(void)
{
	register unsigned int *ip = &c_mmot_FIRST;
	while (ip <= &c_mmot_LAST)
		*ip++ = 0;
}

void			/* Call from the debugger to show all counters */
db_mmot_show_counters(void)
{
#define	xx(str)	printf("%s: %d\n", # str, str);

	xx(c_mmot_combined_S_R);
	xx(c_mach_msg_trap_switch_fast);
	xx(c_mmot_ikm_cache_miss);	
	xx(c_mmot_kernel_send);
	xx(c_mmot_cold_000);
	xx(c_mmot_smallsendsize);
	xx(c_mmot_oddsendsize);
	xx(c_mmot_bigsendsize);
	xx(c_mmot_copyinmsg_fail);
	xx(c_mmot_g_slow_copyin3);
	xx(c_mmot_cold_006);
	xx(c_mmot_cold_007);
	xx(c_mmot_cold_008);
	xx(c_mmot_cold_009);
	xx(c_mmot_cold_010);
	xx(c_mmot_cold_012);
	xx(c_mmot_cold_013);
	xx(c_mmot_cold_014);
	xx(c_mmot_cold_016);
	xx(c_mmot_cold_018);
	xx(c_mmot_cold_019);
	xx(c_mmot_cold_020);
	xx(c_mmot_cold_021);
	xx(c_mmot_cold_022);
	xx(c_mmot_cold_023);
	xx(c_mmot_cold_024);
	xx(c_mmot_cold_025);
	xx(c_mmot_cold_026);
	xx(c_mmot_cold_027);
	xx(c_mmot_hot_fSR_ok);
	xx(c_mmot_cold_029);
	xx(c_mmot_cold_030);
	xx(c_mmot_cold_031);
	xx(c_mmot_cold_032);
	xx(c_mmot_cold_033);
	xx(c_mmot_bad_rcvr);
	xx(c_mmot_rcvr_swapped);
	xx(c_mmot_rcvr_locked);
	xx(c_mmot_rcvr_tswapped);
	xx(c_mmot_rcvr_freed);
	xx(c_mmot_g_slow_copyout6);
	xx(c_mmot_g_slow_copyout5);
	xx(c_mmot_cold_037);
	xx(c_mmot_cold_038);
	xx(c_mmot_cold_039);
	xx(c_mmot_g_slow_copyout4);
	xx(c_mmot_g_slow_copyout3);
	xx(c_mmot_g_slow_copyout1);
	xx(c_mmot_hot_ok3);
	xx(c_mmot_hot_ok2);
	xx(c_mmot_hot_ok1);
	xx(c_mmot_g_slow_copyout2);
	xx(c_mmot_getback_fast_copyin);
	xx(c_mmot_cold_048);
	xx(c_mmot_getback_FastSR);
	xx(c_mmot_cold_050);
	xx(c_mmot_cold_051);
	xx(c_mmot_cold_052);
	xx(c_mmot_cold_053);
	xx(c_mmot_fastkernelreply);
	xx(c_mmot_cold_055);
	xx(c_mmot_getback_fast_put);

#undef	xx
}

#else	/* !HOTPATH_DEBUG */

/*
 * Duplicate just these few so we can always do a quick sanity check
 */
unsigned int c_mmot_combined_S_R = 0;		/* hotpath candidates	*/
unsigned int c_mach_msg_trap_switch_fast = 0;	/* hotpath successes	*/
unsigned int c_mmot_kernel_send = 0;		/* kernel server calls	*/
#define HOT(expr)				/* no optional counters	*/

#endif	/* !HOTPATH_DEBUG */

boolean_t enable_hotpath = TRUE;	/* Patchable, just in case ...	*/
#endif	/* HOTPATH_ENABLE */

/*
 *	Routine:	mach_msg_overwrite_trap [mach trap]
 *	Purpose:
 *		Possibly send a message; possibly receive a message.
 *	Conditions:
 *		Nothing locked.
 *	Returns:
 *		All of mach_msg_send and mach_msg_receive error codes.
 */

mach_msg_return_t
mach_msg_overwrite_trap(
	mach_msg_header_t	*msg,
	mach_msg_option_t	option,
	mach_msg_size_t		send_size,
	mach_msg_size_t		rcv_size,
	mach_port_name_t	rcv_name,
	mach_msg_timeout_t	timeout,
	mach_port_name_t	notify,
	mach_msg_header_t	*rcv_msg,
    mach_msg_size_t		scatter_list_size)
{
	register mach_msg_header_t *hdr;		 

	mach_msg_return_t  mr = MACH_MSG_SUCCESS;
	/* mask out some of the options before entering the hot path */
	mach_msg_option_t  masked_option = 
		option & ~(MACH_SEND_TRAILER|MACH_RCV_TRAILER_MASK);

#if	ENABLE_HOTPATH
	/* BEGINNING OF HOT PATH */
	if (masked_option == (MACH_SEND_MSG|MACH_RCV_MSG) && enable_hotpath) {
		register ipc_thread_t self = current_thread();
		register mach_msg_format_0_trailer_t *trailer;

		ipc_space_t space = current_act()->task->itk_space;
		ipc_kmsg_t kmsg;
		register ipc_port_t dest_port;
		ipc_object_t rcv_object;
		register ipc_mqueue_t rcv_mqueue;
		mach_msg_size_t reply_size;

		c_mmot_combined_S_R++;
		self->ith_scatter_list = MACH_MSG_BODY_NULL;

		/*
		 *	This case is divided into ten sections, each
		 *	with a label.  There are five optimized
		 *	sections and six unoptimized sections, which
		 *	do the same thing but handle all possible
		 *	cases and are slower.
		 *
		 *	The five sections for an RPC are
		 *	    1) Get request message into a buffer.
		 *		(fast_get or slow_get)
		 *	    2) Copyin request message and rcv_name.
		 *		(fast_copyin or slow_copyin)
		 *	    3) Enqueue request and dequeue reply.
		 *		(fast_send_receive or
		 *		 slow_send and slow_receive)
		 *	    4) Copyout reply message.
		 *		(fast_copyout or slow_copyout)
		 *	    5) Put reply message to user's buffer.
		 *		(fast_put or slow_put)
		 *
		 *	Keep the locking hierarchy firmly in mind.
		 *	(First spaces, then ports, then port sets,
		 *	then message queues.)  Only a non-blocking
		 *	attempt can be made to acquire locks out of
		 *	order, or acquire two locks on the same level.
		 *	Acquiring two locks on the same level will
		 *	fail if the objects are really the same,
		 *	unless simple locking is disabled.  This is OK,
		 *	because then the extra unlock does nothing.
		 *
		 *	There are two major reasons these RPCs can't use
		 *	ipc_thread_switch, and use slow_send/slow_receive:
		 *		1) Kernel RPCs.
		 *		2) Servers fall behind clients, so
		 *		client doesn't find a blocked server thread and
		 *		server finds waiting messages and can't block.
		 */
#if 0
	fast_get:
#endif		
		/*
		 *	optimized ipc_kmsg_get
		 *
		 *	No locks, references, or messages held.
		 *	We must clear ikm_cache before copyinmsg.
		 */

		if ((send_size < sizeof(mach_msg_header_t)) ||
		    (send_size & 3) ||
		    (send_size > (IKM_SAVED_MSG_SIZE - MAX_TRAILER_SIZE)) ||
		    !ikm_cache_get(&kmsg)) {
#if	HOTPATH_DEBUG
			if (send_size < sizeof(mach_msg_header_t)) {
			    HOT(c_mmot_smallsendsize++);
			} else if (send_size & 3) {
			    HOT(c_mmot_oddsendsize++);
			} else if (send_size >
				(IKM_SAVED_MSG_SIZE - MAX_TRAILER_SIZE)) {
			    HOT(c_mmot_bigsendsize++);
			} else if (!kmsg) {
			    HOT(c_mmot_ikm_cache_miss++);
			}
#endif
			goto slow_get;
		}

		ikm_check_initialized(kmsg, IKM_SAVED_KMSG_SIZE);

		hdr = &kmsg->ikm_header;
		if (copyinmsg((char *) msg, (char *) hdr, send_size)) {
			if (kmsg->ikm_size != IKM_SAVED_KMSG_SIZE ||
			    KMSG_IS_RT(kmsg) || !ikm_cache_put(kmsg))
				ikm_free(kmsg);
			HOT(c_mmot_copyinmsg_fail++);
			goto slow_get;
		}

		hdr->msgh_size = send_size;

		/* naturally align the message before tacking on the trailer */
		trailer = (mach_msg_format_0_trailer_t *) ((vm_offset_t) hdr +
                                                  send_size);
		bcopy(  (char *)&trailer_template, 
			(char *)trailer, 
			sizeof(trailer_template));

	    fast_copyin:
		/*
		 *	optimized ipc_kmsg_copyin/ipc_mqueue_copyin
		 *
		 *	We have the request message data in kmsg.
		 *	Must still do copyin, send, receive, etc.
		 *
		 *	If the message isn't simple, we can't combine
		 *	ipc_kmsg_copyin_header and ipc_mqueue_copyin,
		 *	because copyin of the message body might
		 *	affect rcv_name.
		 */

		switch (hdr->msgh_bits) {
		    case MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND,
					MACH_MSG_TYPE_MAKE_SEND_ONCE): {
			register ipc_entry_t table;
			register ipc_entry_num_t size;
			register ipc_port_t reply_port;

			/* sending a request message */

		    {
			register mach_port_index_t index;
			register mach_port_gen_t gen;

		    {
				mach_port_name_t reply_name = CAST_MACH_PORT_TO_NAME(hdr->msgh_local_port);

			if (reply_name != rcv_name) {
				HOT(c_mmot_g_slow_copyin3++);
				goto slow_copyin;
			}

			/* optimized ipc_entry_lookup of reply_name */

			index = MACH_PORT_INDEX(reply_name);
			gen = MACH_PORT_GEN(reply_name);

			is_read_lock(space);
			assert(space->is_active);

			size = space->is_table_size;
			table = space->is_table;

		    {
			register ipc_entry_t entry;
			register ipc_entry_bits_t bits;

			if (index < size) {
				entry = &table[index];
				bits = entry->ie_bits;
				if (IE_BITS_GEN(bits) != gen ||
				    (bits & IE_BITS_COLLISION)) {
					entry = IE_NULL;
				}
			} else {
				entry = IE_NULL;
			}
			if (entry == IE_NULL) {
				entry = ipc_entry_lookup(space, reply_name);
				if (entry == IE_NULL) {
					HOT(c_mmot_cold_006++);
					goto abort_request_copyin;
				}
				bits = entry->ie_bits;
			}

			/* check type bit */

			if (! (bits & MACH_PORT_TYPE_RECEIVE)) {
				HOT(c_mmot_cold_007++);
				goto abort_request_copyin;
			}

			reply_port = (ipc_port_t) entry->ie_object;
			assert(reply_port != IP_NULL);
		    }
		    }
		    }

			/* optimized ipc_entry_lookup of dest_name */

		    {
			register mach_port_index_t index;
			register mach_port_gen_t gen;

		    {
			mach_port_name_t dest_name =
				CAST_MACH_PORT_TO_NAME(hdr->msgh_remote_port);

			index = MACH_PORT_INDEX(dest_name);
			gen = MACH_PORT_GEN(dest_name);

		    {
			register ipc_entry_t entry;
			register ipc_entry_bits_t bits;

			if (index < size) {
				entry = &table[index];
				bits = entry->ie_bits;
				if (IE_BITS_GEN(bits) != gen ||
				    (bits & IE_BITS_COLLISION)) {
					entry = IE_NULL;
				}
			} else {
				entry = IE_NULL;
			}
			if (entry == IE_NULL) {
				entry = ipc_entry_lookup(space, dest_name);
				if (entry == IE_NULL) {
					HOT(c_mmot_cold_008++);
					goto abort_request_copyin;
				}
				bits = entry->ie_bits;
			}

			/* check type bit */

			if (! (bits & MACH_PORT_TYPE_SEND)) {
				HOT(c_mmot_cold_009++);
				goto abort_request_copyin;
			}

			assert(IE_BITS_UREFS(bits) > 0);

			dest_port = (ipc_port_t) entry->ie_object;
			assert(dest_port != IP_NULL);
		    }
		    }
		    }

			/*
			 *	To do an atomic copyin, need simultaneous
			 *	locks on both ports and the space.  If
			 *	dest_port == reply_port, and simple locking is
			 *	enabled, then we will abort.  Otherwise it's
			 *	OK to unlock twice.
			 */

			ip_lock(dest_port);
			if (!ip_active(dest_port) ||
			    !ip_lock_try(reply_port)) {
				ip_unlock(dest_port);
				HOT(c_mmot_cold_010++);
				goto abort_request_copyin;
			}
			is_read_unlock(space);

			assert(dest_port->ip_srights > 0);
			dest_port->ip_srights++;
			ip_reference(dest_port);

			assert(ip_active(reply_port));
			assert(reply_port->ip_receiver_name == CAST_MACH_PORT_TO_NAME(hdr->msgh_local_port));
			assert(reply_port->ip_receiver == space);

			reply_port->ip_sorights++;
			ip_reference(reply_port);

			hdr->msgh_bits =
				MACH_MSGH_BITS(MACH_MSG_TYPE_PORT_SEND,
					       MACH_MSG_TYPE_PORT_SEND_ONCE);
			hdr->msgh_remote_port =
					(mach_port_t) dest_port;
			hdr->msgh_local_port =
					(mach_port_t) reply_port;

			/* make sure we can queue to the destination */

			if (dest_port->ip_receiver == ipc_space_kernel) {
				/*
				 * The kernel server has a reference to
				 * the reply port, which it hands back
				 * to us in the reply message.  We do
				 * not need to keep another reference to
				 * it.
				 */
				ip_unlock(reply_port);

				assert(ip_active(dest_port));
				ip_unlock(dest_port);
				goto kernel_send;
			}

#if	DIPC
			if (IP_IS_REMOTE(dest_port)) {
				ip_unlock(reply_port);
				ip_unlock(dest_port);
				HOT(c_mmot_cold_012++);
				goto slow_send;
		        }
#endif	/* DIPC */

			if (dest_port->ip_msgcount >= dest_port->ip_qlimit) {
				HOT(c_mmot_cold_013++);
				goto abort_request_send_receive;
			}

			/* optimized ipc_mqueue_copyin */

			if (reply_port->ip_pset != IPS_NULL) {
				HOT(c_mmot_cold_014++);
				goto abort_request_send_receive;
			}

			rcv_object = (ipc_object_t) reply_port;
			io_reference(rcv_object);
			rcv_mqueue = &reply_port->ip_messages;
			imq_lock(rcv_mqueue);
			io_unlock(rcv_object);
			HOT(c_mmot_hot_fSR_ok++);
			goto fast_send_receive;

		    abort_request_copyin:
			is_read_unlock(space);
			goto slow_copyin;

		    abort_request_send_receive:
			ip_unlock(dest_port);
			ip_unlock(reply_port);
			goto slow_send;
		    }

		    case MACH_MSGH_BITS(MACH_MSG_TYPE_MOVE_SEND_ONCE, 0): {
			register ipc_entry_num_t size;
			register ipc_entry_t table;

			/* sending a reply message */

		    {
			register mach_port_name_t reply_name =
				CAST_MACH_PORT_TO_NAME(hdr->msgh_local_port);

			if (reply_name != MACH_PORT_NAME_NULL) {
				HOT(c_mmot_cold_018++);
				goto slow_copyin;
			}
		    }

			is_write_lock(space);
			assert(space->is_active);

			/* optimized ipc_entry_lookup */

			size = space->is_table_size;
			table = space->is_table;

		    {
			register ipc_entry_t entry;
			register mach_port_gen_t gen;
			register mach_port_index_t index;

		    {
			register mach_port_name_t dest_name =
				CAST_MACH_PORT_TO_NAME(hdr->msgh_remote_port);

			index = MACH_PORT_INDEX(dest_name);
			gen = MACH_PORT_GEN(dest_name);
		    }

			if (index >= size) {
				HOT(c_mmot_cold_019++);
				goto abort_reply_dest_copyin;
			}

			entry = &table[index];

			/* check generation, collision bit, and type bit */

			if ((entry->ie_bits & (IE_BITS_GEN_MASK|
					       IE_BITS_COLLISION|
					       MACH_PORT_TYPE_SEND_ONCE)) !=
			    (gen | MACH_PORT_TYPE_SEND_ONCE)) {
				HOT(c_mmot_cold_020++);
				goto abort_reply_dest_copyin;
			}

			/* optimized ipc_right_copyin */

			assert(IE_BITS_TYPE(entry->ie_bits) ==
						MACH_PORT_TYPE_SEND_ONCE);
			assert(IE_BITS_UREFS(entry->ie_bits) == 1);

			if (entry->ie_request != 0) {
				HOT(c_mmot_cold_021++);
				goto abort_reply_dest_copyin;
			}

			dest_port = (ipc_port_t) entry->ie_object;
			assert(dest_port != IP_NULL);

			ip_lock(dest_port);
			if (!ip_active(dest_port)) {
				ip_unlock(dest_port);
				HOT(c_mmot_cold_022++);
				goto abort_reply_dest_copyin;
			}

			assert(dest_port->ip_sorights > 0);

			/* optimized ipc_entry_dealloc */

			entry->ie_next = table->ie_next;
			table->ie_next = index;
			entry->ie_bits = gen;
			entry->ie_object = IO_NULL;
		    }

			hdr->msgh_bits =
				MACH_MSGH_BITS(MACH_MSG_TYPE_PORT_SEND_ONCE,
					       0);
			hdr->msgh_remote_port = (mach_port_t) dest_port;

			/* make sure we can queue to the destination */

			assert(dest_port->ip_receiver != ipc_space_kernel);
#if	DIPC
			if (IP_IS_REMOTE(dest_port)) {
				ip_unlock(dest_port);
				is_write_unlock(space);
				HOT(c_mmot_cold_023++);
				goto slow_send;
			}
#endif	/* DIPC */

			/* optimized ipc_entry_lookup/ipc_mqueue_copyin */

		    {
			register ipc_entry_t entry;
			register ipc_entry_bits_t bits;

		    {
			register mach_port_index_t index;
			register mach_port_gen_t gen;

			index = MACH_PORT_INDEX(rcv_name);
			gen = MACH_PORT_GEN(rcv_name);

			if (index < size) {
				entry = &table[index];
				bits = entry->ie_bits;
				if (IE_BITS_GEN(bits) != gen ||
				    (bits & IE_BITS_COLLISION)) {
					entry = IE_NULL;
				}
			} else {
				entry = IE_NULL;
			}
			if (entry == IE_NULL) {
				entry = ipc_entry_lookup(space, rcv_name);
				if (entry == IE_NULL) {
					HOT(c_mmot_cold_024++);
					goto abort_reply_rcv_copyin;
				}
				bits = entry->ie_bits;
			}

		    }

			/* check type bits; looking for receive or set */

			if (bits & MACH_PORT_TYPE_PORT_SET) {
				register ipc_pset_t rcv_pset;

				rcv_pset = (ipc_pset_t) entry->ie_object;
				assert(rcv_pset != IPS_NULL);

				ips_lock(rcv_pset);
				assert(ips_active(rcv_pset));

				rcv_object = (ipc_object_t) rcv_pset;
				rcv_mqueue = &rcv_pset->ips_messages;
			} else if (bits & MACH_PORT_TYPE_RECEIVE) {
				register ipc_port_t rcv_port;

				rcv_port = (ipc_port_t) entry->ie_object;
				assert(rcv_port != IP_NULL);

				if (!ip_lock_try(rcv_port)) {
					HOT(c_mmot_cold_025++);
					goto abort_reply_rcv_copyin;
				}
				assert(ip_active(rcv_port));

				if (rcv_port->ip_pset != IPS_NULL) {
					ip_unlock(rcv_port);
					HOT(c_mmot_cold_026++);
					goto abort_reply_rcv_copyin;
				}

				rcv_object = (ipc_object_t) rcv_port;
				rcv_mqueue = &rcv_port->ip_messages;
			} else {
				HOT(c_mmot_cold_027++);
				goto abort_reply_rcv_copyin;
			}
		    }

			is_write_unlock(space);
			io_reference(rcv_object);
			imq_lock(rcv_mqueue);
			io_unlock(rcv_object);
			HOT(c_mmot_hot_fSR_ok++);
			goto fast_send_receive;

		    abort_reply_dest_copyin:
			is_write_unlock(space);
			HOT(c_mmot_cold_029++);
			goto slow_copyin;

		    abort_reply_rcv_copyin:
			ip_unlock(dest_port);
			is_write_unlock(space);
			HOT(c_mmot_cold_030++);
			goto slow_send;
		    }

		    default:
			HOT(c_mmot_cold_031++);
			goto slow_copyin;
		}
		/*NOTREACHED*/

	    fast_send_receive:
		/*
		 *	optimized ipc_mqueue_send/ipc_mqueue_receive
		 *
		 *	Finished get/copyin of kmsg and copyin of rcv_name.
		 *	space is unlocked, dest_port is locked,
		 *	we can queue kmsg to dest_port,
		 *	rcv_mqueue is locked, rcv_object holds a ref,
		 *	if rcv_object is a port it isn't in a port set
		 *
		 *	Note that if simple locking is turned off,
		 *	then we could have dest_mqueue == rcv_mqueue
		 *	and not abort when we try to lock dest_mqueue.
		 */

		assert(ip_active(dest_port));
		assert(dest_port->ip_receiver != ipc_space_kernel);
#if	DIPC
		assert(! IP_IS_REMOTE(dest_port));
#endif	/* DIPC */
		assert((dest_port->ip_msgcount < dest_port->ip_qlimit) ||
		       (MACH_MSGH_BITS_REMOTE(hdr->msgh_bits) ==
						MACH_MSG_TYPE_PORT_SEND_ONCE));
		assert((hdr->msgh_bits & MACH_MSGH_BITS_CIRCULAR) == 0);

	    {
		register ipc_mqueue_t dest_mqueue;
		register ipc_thread_t receiver;
#if	THREAD_SWAPPER
		thread_act_t rcv_act;
#endif

	    {
		register ipc_pset_t dest_pset;

		dest_pset = dest_port->ip_pset;
		if (dest_pset == IPS_NULL)
			dest_mqueue = &dest_port->ip_messages;
		else
			dest_mqueue = &dest_pset->ips_messages;
	    }

		if (!imq_lock_try(dest_mqueue)) {
		    abort_send_receive:
			ip_unlock(dest_port);
			imq_unlock(rcv_mqueue);
			ipc_object_release(rcv_object);
			HOT(c_mmot_cold_032++);
			goto slow_send;
		}

		receiver = ipc_thread_queue_first(&dest_mqueue->imq_threads);
		if ((receiver == ITH_NULL) ||
		    (ipc_kmsg_queue_first(&rcv_mqueue->imq_messages)
								!= IKM_NULL)) {
			imq_unlock(dest_mqueue);
			HOT(c_mmot_cold_033++);
			goto abort_send_receive;
		}

#if 0
		/*
		 * Check that the receiver can stay on the hot path.
		 */
		if (!(((receiver->at_safe_point == SAFE_EXTERNAL_RECEIVE) ||
		    (receiver->at_safe_point == SAFE_INTERNAL_RECEIVE)) &&
		    (send_size + REQUESTED_TRAILER_SIZE(receiver->ith_option)
			<= receiver->ith_msize) &&
		    ((receiver->ith_option & MACH_RCV_NOTIFY) == 0) &&
		    (receiver->ith_scatter_list == MACH_MSG_BODY_NULL))) {
			/*
			 *	The receiver can't accept the message,
			 *	or we can't switch to the receiver.
			 */

			/* Almost always at_safe_point wrongly set */
			HOT(c_mmot_bad_rcvr++);
		fall_off:
			imq_unlock(dest_mqueue);
			goto abort_send_receive;
		}
#endif
		
#if	THREAD_SWAPPER
		/*
		 * Receiver looks okay -- is it swapped in?
		 */
		rpc_lock(receiver);
		rcv_act = receiver->top_act;
		if (rcv_act->swap_state != TH_SW_IN &&
			rcv_act->swap_state != TH_SW_UNSWAPPABLE) {
			rpc_unlock(receiver);
			HOT(c_mmot_rcvr_swapped++);
			goto fall_off;
		}

		/*
		 * Make sure receiver stays swapped in (if we can).
		 */
		if (!act_lock_try(rcv_act)) {	/* out of order! */
			rpc_unlock(receiver);
			HOT(c_mmot_rcvr_locked++);
			goto fall_off;
		}
		
		/*
		 * Check for task swapping in progress affecting
		 * receiver.  Since rcv_act is attached to a shuttle,
		 * its swap_state is covered by shuttle's thread_lock()
		 * (sigh).
		 */
		s = splsched();
		thread_lock(receiver);
		/*
		 * Re-check swap_state now that we hold thread_lock().
		 */
		if ((rcv_act->swap_state != TH_SW_IN &&
			rcv_act->swap_state != TH_SW_UNSWAPPABLE) ||
		 	rcv_act->ast & AST_SWAPOUT) {
			thread_unlock(receiver);
			splx(s);
			act_unlock(rcv_act);
			rpc_unlock(receiver);
			HOT(c_mmot_rcvr_tswapped++);
			goto fall_off;
		}

		/*
		 * We don't need to make receiver unswappable here -- holding
		 * act_lock() of rcv_act is sufficient to prevent either thread
		 * or task swapping from changing its state (see swapout_scan(),
		 * task_swapout()).  Don't release lock till receiver's state
		 * is consistent.  Its task may then be marked for swapout,
		 * but that's life.
		 */

		thread_unlock(receiver);
		splx(s);

		rpc_unlock(receiver);
		/*
		 * NB:  act_lock(rcv_act) still held
		 */
#endif	/* THREAD_SWAPPER */

		/* At this point we are committed to do the "handoff". */
		c_mach_msg_trap_switch_fast++;

		/*
		 *	Safe to unlock dest_port now that we are
		 *	committed to this path, because we hold
		 *	dest_mqueue locked.
		 */

		dest_port->ip_msgcount++;
		ip_unlock(dest_port);

		/*
		 *	Put sender on reply port's queue.
		 *	Also save state that the sender of
		 *	our reply message needs to determine if it
		 *	can hand off directly back to us.
		 */

		ipc_thread_enqueue_macro(&rcv_mqueue->imq_threads, self);
		self->ith_state = MACH_RCV_IN_PROGRESS;
		self->ith_msize = MACH_MSG_SIZE_MAX;

		self->ith_option = option;
		self->ith_scatter_list = MACH_MSG_BODY_NULL;
		self->ith_scatter_list_size = scatter_list_size;
		imq_unlock(rcv_mqueue);

		/*
		 *	Extract receiver from dest_mqueue, and store the
		 *	kmsg and seqno where the receiver can pick it up.
		 */

		ipc_thread_rmqueue_first_macro(
			&dest_mqueue->imq_threads, receiver);
		receiver->ith_state = MACH_MSG_SUCCESS;
		receiver->ith_kmsg = kmsg;
		receiver->ith_seqno = dest_port->ip_seqno++;
		imq_unlock(dest_mqueue);

		/* inline ipc_object_release */
		io_lock(rcv_object);
		io_release(rcv_object);
		io_check_unlock(rcv_object);

#ifdef notyet
		/*
		 * Switch directly to receiving thread, and block
		 * this thread as though it had called ipc_mqueue_receive.
		 */
		{
			extern unsigned sched_tick; /* kern/sched_prim.c */
	
			check_simple_locks();

			s = splsched();

			mp_disable_preemption();
			/* from thread_block_reason() */
			ast_off(cpu_number(),
				(AST_QUANTUM|AST_BLOCK|AST_URGENT));
			mp_enable_preemption();

			/*
			 * This receiver is no longer eligible to be
			 * awakened and handed a message.
			 */
			thread_lock(receiver);

			assert((receiver->state & (TH_RUN|TH_WAIT)) == TH_WAIT);
			assert(receiver != self);

			receiver->state &= ~(TH_WAIT|TH_UNINT);

			receiver->state |= TH_RUN;
			receiver->at_safe_point = NOT_AT_SAFE_POINT;
#if	NCPUS > 1	/* from thread_invoke inline */
			mp_disable_preemption();
			receiver->last_processor = current_processor();
			mp_enable_preemption();
#endif	/* NCPUS > 1 */
			thread_unlock(receiver);
			assert( receiver != self );
#if	THREAD_SWAPPER
			act_unlock(rcv_act);
#endif	/* THREAD_SWAPPER */
	
			/*
			 * Prepare self (the sender) to block.
			 */
			thread_lock(self);
			assert(!(self->state & TH_ABORT));
			assert(self->wait_result = -1); /* for assertions */
			assert(self->state & TH_RUN);
			self->state |= TH_WAIT;
			self->at_safe_point = SAFE_EXTERNAL_RECEIVE;
			self->reason = 0;	/* inline thread_invoke */
			thread_unlock(self);

			/*
			 * Switch to the receiver now.
			 * Inlined: thread_invoke(self, receiver, 0);
			 */
			{   thread_t old_thread;
			    mp_disable_preemption();
			    ast_context(receiver->top_act, cpu_number());
			    mp_enable_preemption();
			    timer_switch(&receiver->system_timer);
			    disable_preemption();
			    old_thread = switch_context(self, 0, receiver);
			    assert(old_thread != self);
			    thread_dispatch(old_thread);
			    enable_preemption();
			}

			splx(s);
		}

#endif
		if (self->ith_state != MACH_MSG_SUCCESS) {
			/* why did we wake up? */
			kern_return_t	save_wait_result;

			/* save wait_result in case we block trying to lock */
			save_wait_result = self->wait_result;
			imq_lock(rcv_mqueue);

			switch (self->ith_state) {
			    case MACH_RCV_PORT_DIED:
			    case MACH_RCV_PORT_CHANGED:
				/* something bad happened to the port/set */

				imq_unlock(rcv_mqueue);
				return self->ith_state;

			    case MACH_RCV_IN_PROGRESS:
				/*
				 *	Awakened for other than IPC completion.
				 *	Remove ourselves from the waiting queue,
				 *	then check the wakeup cause.
				 */

				ipc_thread_rmqueue(&rcv_mqueue->imq_threads,
									self);

				switch (save_wait_result) {
				    case THREAD_INTERRUPTED:
					/* receive was interrupted - give up */

					imq_unlock(rcv_mqueue);
					return MACH_RCV_INTERRUPTED;

				    default:
					panic("mmot_hotpath: bad wait result");
				}
				break;

			    default:
				panic("mmot_hotpath: bad ith_state");
			}
		}

		/*
		 * Restore state saved in sending thread's send path.
		 */
		kmsg = self->ith_kmsg;
		assert(kmsg != IKM_NULL);
		dest_port = (ipc_port_t)kmsg->ikm_header.msgh_remote_port;

#if	DIPC
		if (KMSG_IN_DIPC(kmsg)) {
			mr = ipc_mqueue_finish_receive(&kmsg, dest_port, option,
							MACH_MSG_SIZE_MAX);

			if (mr == MACH_RCV_TRANSPORT_ERROR) {
				if (msg_receive_error(kmsg,
				    (rcv_msg != MACH_MSG_NULL) ? rcv_msg : msg,
					option, self->ith_seqno, space)
				    == MACH_RCV_INVALID_DATA) {
					mr = MACH_RCV_INVALID_DATA;
				}
			}

			if (mr != MACH_MSG_SUCCESS)
				return(mr);
		} else
#endif	/* DIPC */
			{
			ip_lock(dest_port);
			assert(dest_port->ip_msgcount > 0);
			dest_port->ip_msgcount--;
			ip_unlock(dest_port);
		}

		hdr = &kmsg->ikm_header;
		send_size = hdr->msgh_size;
		trailer = (mach_msg_format_0_trailer_t *) ((vm_offset_t) hdr +
			round_msg(send_size));

		if (option & MACH_RCV_TRAILER_MASK) {
			trailer->msgh_seqno = self->ith_seqno;	
			trailer->msgh_trailer_size =
					REQUESTED_TRAILER_SIZE(option);
		}
	    }

	    fast_copyout:
		/*
		 *	Nothing locked and no references held, except
		 *	we have kmsg with msgh_seqno filled in.  Must
		 *	still check against rcv_size and do
		 *	ipc_kmsg_copyout/ipc_kmsg_put.
		 */

		reply_size = send_size + trailer->msgh_trailer_size;
		if ((rcv_size < reply_size)
#if	DIPC
		    || KMSG_IS_DIPC_FORMAT(kmsg)
#endif	/* DIPC */
						) {
			HOT(c_mmot_g_slow_copyout6++);
			goto slow_copyout;
		}

		/* optimized ipc_kmsg_copyout/ipc_kmsg_copyout_header */

		switch (hdr->msgh_bits) {
		    case MACH_MSGH_BITS(MACH_MSG_TYPE_PORT_SEND,
					MACH_MSG_TYPE_PORT_SEND_ONCE): {
			ipc_port_t reply_port =
				(ipc_port_t) hdr->msgh_local_port;
			mach_port_name_t dest_name, reply_name;

			/* receiving a request message */

			if (!IP_VALID(reply_port)) {
				HOT(c_mmot_g_slow_copyout5++);
				goto slow_copyout;
			}

			is_write_lock(space);
			assert(space->is_active);

			/*
			 *	To do an atomic copyout, need simultaneous
			 *	locks on both ports and the space.  If
			 *	dest_port == reply_port, and simple locking is
			 *	enabled, then we will abort.  Otherwise it's
			 *	OK to unlock twice.
			 */

			ip_lock(dest_port);
			if (!ip_active(dest_port) ||
			    !ip_lock_try(reply_port)) {
				HOT(c_mmot_cold_037++);
				goto abort_request_copyout;
			}

			if (!ip_active(reply_port)) {
				ip_unlock(reply_port);
				HOT(c_mmot_cold_038++);
				goto abort_request_copyout;
			}

			assert(reply_port->ip_sorights > 0);
			ip_unlock(reply_port);

		    {
			register ipc_entry_t table;
			register ipc_entry_t entry;
			register mach_port_index_t index;

			/* optimized ipc_entry_get */

			table = space->is_table;
			index = table->ie_next;

			if (index == 0) {
				HOT(c_mmot_cold_039++);
				goto abort_request_copyout;
			}

			entry = &table[index];
			table->ie_next = entry->ie_next;
			entry->ie_request = 0;

		    {
			register mach_port_gen_t gen;

			assert((entry->ie_bits &~ IE_BITS_GEN_MASK) == 0);
			gen = IE_BITS_NEW_GEN(entry->ie_bits);

			reply_name = MACH_PORT_MAKE(index, gen);

			/* optimized ipc_right_copyout */

			entry->ie_bits = gen | (MACH_PORT_TYPE_SEND_ONCE | 1);
		    }

			assert(MACH_PORT_NAME_VALID(reply_name));
			entry->ie_object = (ipc_object_t) reply_port;
			is_write_unlock(space);
		    }

			/* optimized ipc_object_copyout_dest */

			assert(dest_port->ip_srights > 0);
			ip_release(dest_port);

			if (dest_port->ip_receiver == space)
				dest_name = dest_port->ip_receiver_name;
			else
				dest_name = MACH_PORT_NAME_NULL;

			if ((--dest_port->ip_srights == 0) &&
#if	DIPC
			    (!DIPC_IS_DIPC_PORT(dest_port) || 
			     (dest_port->ip_transit == 0)) &&
#endif	/* DIPC */
			    (dest_port->ip_nsrequest != IP_NULL)) {
				ipc_port_t nsrequest;
				mach_port_mscount_t mscount;

				/* a rather rare case */

				nsrequest = dest_port->ip_nsrequest;
				mscount = dest_port->ip_mscount;
				dest_port->ip_nsrequest = IP_NULL;
				ip_unlock(dest_port);
				ipc_notify_no_senders(nsrequest, mscount);
			} else
				ip_unlock(dest_port);

			hdr->msgh_bits =
				MACH_MSGH_BITS(MACH_MSG_TYPE_PORT_SEND_ONCE,
					       MACH_MSG_TYPE_PORT_SEND);
			hdr->msgh_remote_port = CAST_MACH_NAME_TO_PORT(reply_name);
			hdr->msgh_local_port = CAST_MACH_NAME_TO_PORT(dest_name);
			HOT(c_mmot_hot_ok1++);
			goto fast_put;

		    abort_request_copyout:
			ip_unlock(dest_port);
			is_write_unlock(space);
			HOT(c_mmot_g_slow_copyout4++);
			goto slow_copyout;
		    }

		    case MACH_MSGH_BITS(MACH_MSG_TYPE_PORT_SEND_ONCE, 0): {
			mach_port_name_t dest_name;

			/* receiving a reply message */

			ip_lock(dest_port);
			if (!ip_active(dest_port)) {
				ip_unlock(dest_port);
				HOT(c_mmot_g_slow_copyout3++);
				goto slow_copyout;
			}

			/* optimized ipc_object_copyout_dest */

			assert(dest_port->ip_sorights > 0);

			if (dest_port->ip_receiver == space) {
				ip_release(dest_port);
				dest_port->ip_sorights--;
				dest_name = dest_port->ip_receiver_name;
				ip_unlock(dest_port);
			} else {
				ip_unlock(dest_port);

				ipc_notify_send_once(dest_port);
				dest_name = MACH_PORT_NAME_NULL;
			}

			hdr->msgh_bits = MACH_MSGH_BITS(0,
					       MACH_MSG_TYPE_PORT_SEND_ONCE);
			hdr->msgh_remote_port = MACH_PORT_NULL;
			hdr->msgh_local_port = CAST_MACH_NAME_TO_PORT(dest_name);
			HOT(c_mmot_hot_ok2++);
			goto fast_put;
		    }

		    case MACH_MSGH_BITS_COMPLEX|
			 MACH_MSGH_BITS(MACH_MSG_TYPE_PORT_SEND_ONCE, 0): {
			register mach_port_name_t dest_name;

			/* receiving a complex reply message */

			ip_lock(dest_port);
			if (!ip_active(dest_port)) {
				ip_unlock(dest_port);
				HOT(c_mmot_g_slow_copyout1++);
				goto slow_copyout;
			}

			/* optimized ipc_object_copyout_dest */

			assert(dest_port->ip_sorights > 0);

			if (dest_port->ip_receiver == space) {
				ip_release(dest_port);
				dest_port->ip_sorights--;
				dest_name = dest_port->ip_receiver_name;
				ip_unlock(dest_port);
			} else {
				ip_unlock(dest_port);

				ipc_notify_send_once(dest_port);
				dest_name = MACH_PORT_NAME_NULL;
			}

			hdr->msgh_bits =
				MACH_MSGH_BITS_COMPLEX |
				MACH_MSGH_BITS(0, MACH_MSG_TYPE_PORT_SEND_ONCE);
			hdr->msgh_remote_port = MACH_PORT_NULL;
			hdr->msgh_local_port = CAST_MACH_NAME_TO_PORT(dest_name);

			mr = ipc_kmsg_copyout_body(kmsg, space,
						   current_map(), 
						   MACH_MSG_BODY_NULL);
			if (mr != MACH_MSG_SUCCESS) {
				if (ipc_kmsg_put(msg, kmsg, hdr->msgh_size +
					       trailer->msgh_trailer_size) == 
							MACH_RCV_INVALID_DATA)
					return MACH_RCV_INVALID_DATA;
				else
					return mr | MACH_RCV_BODY_ERROR;
			}
			HOT(c_mmot_hot_ok3++);
			goto fast_put;
		    }

		    default:
			HOT(c_mmot_g_slow_copyout2++);
			goto slow_copyout;
		}
		/*NOTREACHED*/ panic("happy birthday rwd");

	    fast_put:
		/*
		 * We have the reply message data in kmsg,
		 * and the reply message size (plus trailer size)
		 * in reply_size.  Just need to copy it out to the
		 * user and free kmsg.  Must check ikm_cache after
		 * copyoutmsg.  Inlined ipc_kmsg_put() here.
		 */
#if	DIPC
		assert(!KMSG_IN_DIPC(kmsg));
#endif	/* DIPC */

		mr = MACH_MSG_SUCCESS;
		if (copyoutmsg((char *) &kmsg->ikm_header,
				(char *)(rcv_msg ? rcv_msg : msg),
				hdr->msgh_size + trailer->msgh_trailer_size))
			mr = MACH_RCV_INVALID_DATA;

		if ((kmsg->ikm_size != IKM_SAVED_KMSG_SIZE) ||
		    KMSG_IS_RT(kmsg) || !ikm_cache_put(kmsg)) {
			ikm_free(kmsg);
		}
		return(mr);


		/* BEGINNING OF WARM PATH */

		/*
		 *	The slow path has a few non-register temporary
		 *	variables used only for call-by-reference.
		 */

	    {
		ipc_kmsg_t temp_kmsg;
		mach_port_seqno_t temp_seqno;
		ipc_object_t temp_rcv_object;
		ipc_mqueue_t temp_rcv_mqueue;

	    slow_get:
		/*
		 *	No locks, references, or messages held.
		 *	Still have to get the request, send it,
		 *	receive reply, etc.
		 */

		mr = ipc_kmsg_get(msg, send_size, &temp_kmsg, current_space());
		if (mr != MACH_MSG_SUCCESS) {
			return(mr);
			/*NOTREACHED*/
		}
		kmsg = temp_kmsg;
		hdr = &kmsg->ikm_header;	      
		/* naturally align the message before tacking on the trailer */
		trailer = (mach_msg_format_0_trailer_t *) ((vm_offset_t) hdr +
                                                  send_size);
		/* try to get back on optimized path */
		HOT(c_mmot_getback_fast_copyin++);
		goto fast_copyin;

	    slow_copyin:
		/*
		 *	We have the message data in kmsg, but
		 *	we still need to copyin, send it,
		 *	receive a reply, and do copyout.
		 */

		mr = ipc_kmsg_copyin(kmsg, space, current_map(),
				     MACH_PORT_NAME_NULL);
		if (mr != MACH_MSG_SUCCESS) {
			if (kmsg->ikm_size != IKM_SAVED_KMSG_SIZE ||
			    KMSG_IS_RT(kmsg) || !ikm_cache_put(kmsg))
				ikm_free(kmsg);
			return(mr);
		}

		/* try to get back on optimized path */

		if (hdr->msgh_bits & MACH_MSGH_BITS_CIRCULAR) {
			HOT(c_mmot_cold_048++);
			goto slow_send;
		}

		dest_port = (ipc_port_t) hdr->msgh_remote_port;
		assert(IP_VALID(dest_port));

		ip_lock(dest_port);
		if (dest_port->ip_receiver == ipc_space_kernel) {
			assert(ip_active(dest_port));
			ip_unlock(dest_port);
			goto kernel_send;
		}

		if (ip_active(dest_port) &&
#if	DIPC
		    (! IP_IS_REMOTE(dest_port)) &&
#endif	/* DIPC */
		    ((dest_port->ip_msgcount < dest_port->ip_qlimit) ||
		     (MACH_MSGH_BITS_REMOTE(hdr->msgh_bits) ==
					MACH_MSG_TYPE_PORT_SEND_ONCE)))
		{
		    /*
		     *	Try an optimized ipc_mqueue_copyin.
		     *	It will work if this is a request message.
		     */

		    register ipc_port_t reply_port;

		    reply_port = (ipc_port_t) hdr->msgh_local_port;
		    if (IP_VALID(reply_port)) {
			if (ip_lock_try(reply_port)) {
			    if (ip_active(reply_port) &&
				reply_port->ip_receiver == space &&
				reply_port->ip_receiver_name == rcv_name &&
				reply_port->ip_pset == IPS_NULL)
			    {
				/* Grab a reference to the reply port. */
				rcv_object = (ipc_object_t) reply_port;
				io_reference(rcv_object);
				rcv_mqueue = &reply_port->ip_messages;
				imq_lock(rcv_mqueue);
				io_unlock(rcv_object);
				HOT(c_mmot_getback_FastSR++);
				goto fast_send_receive;
			    }
			    ip_unlock(reply_port);
			}
		    }
		}

		ip_unlock(dest_port);
		HOT(c_mmot_cold_050++);
		goto slow_send;

	    kernel_send:
		/*
		 *	Special case: send message to kernel services.
		 *	The request message has been copied into the
		 *	kmsg.  Nothing is locked.
		 */

	    {
		register ipc_port_t	reply_port;
		mach_port_seqno_t	local_seqno;

		/*
		 * Perform the kernel function.
		 */
		c_mmot_kernel_send++;
		kmsg = ipc_kobject_server(kmsg);
		if (kmsg == IKM_NULL) {
			/*
			 * No reply.  Take the
			 * slow receive path.
			 */
			HOT(c_mmot_cold_051++);
			goto slow_get_rcv_port;
		}

		/*
		 * Check that:
		 *	the reply port is alive
		 *	we hold the receive right
		 *	the name has not changed.
		 *	the port is not in a set
		 * If any of these are not true,
		 * we cannot directly receive the reply
		 * message.
		 */
		hdr = &kmsg->ikm_header;
		send_size = hdr->msgh_size;
		trailer = (mach_msg_format_0_trailer_t *) ((vm_offset_t) hdr +
			round_msg(send_size));
		reply_port = (ipc_port_t) hdr->msgh_remote_port;
		ip_lock(reply_port);

		if ((!ip_active(reply_port)) ||
		    (reply_port->ip_receiver != space) ||
		    (reply_port->ip_receiver_name != rcv_name) ||
		    (reply_port->ip_pset != IPS_NULL))
		{
			ip_unlock(reply_port);
			ipc_mqueue_send_always(kmsg);
			HOT(c_mmot_cold_052++);
			goto slow_get_rcv_port;
		}

		rcv_mqueue = &reply_port->ip_messages;
		imq_lock(rcv_mqueue);
		/* keep port locked, and don`t change ref count yet */

		/*
		 * If there are messages on the port
		 * or other threads waiting for a message,
		 * we cannot directly receive the reply.
		 */
		if ((ipc_thread_queue_first(&rcv_mqueue->imq_threads)
			!= ITH_NULL) ||
		    (ipc_kmsg_queue_first(&rcv_mqueue->imq_messages)
			!= IKM_NULL))
		{
			imq_unlock(rcv_mqueue);
			ip_unlock(reply_port);
			ipc_mqueue_send_always(kmsg);
			HOT(c_mmot_cold_053++);
			goto slow_get_rcv_port;
		}

		/*
		 * We can directly receive this reply.
		 * Since there were no messages queued
		 * on the reply port, there should be
		 * no threads blocked waiting to send.
		 */

		assert(ipc_thread_queue_first(&reply_port->ip_blocked)
				== ITH_NULL);

		dest_port = reply_port;
		local_seqno = dest_port->ip_seqno++;
		imq_unlock(rcv_mqueue);

		/*
		 * inline ipc_object_release.
		 * Port is still locked.
		 * Reference count was not incremented.
		 */
		ip_check_unlock(reply_port);

		if (option & MACH_RCV_TRAILER_MASK) {
			trailer->msgh_seqno = local_seqno;	
			trailer->msgh_trailer_size = REQUESTED_TRAILER_SIZE(option);
		}
		/* copy out the kernel reply */
		HOT(c_mmot_fastkernelreply++);
		goto fast_copyout;
	    }

	    slow_send:
		/*
		 *	Nothing is locked.  We have acquired kmsg, but
		 *	we still need to send it and receive a reply.
		 */

		mr = ipc_mqueue_send(kmsg, MACH_MSG_OPTION_NONE,
				     MACH_MSG_TIMEOUT_NONE);
		if (mr != MACH_MSG_SUCCESS) {
			mr |= ipc_kmsg_copyout_pseudo(kmsg, space,
						      current_map(),
						      MACH_MSG_BODY_NULL);

			(void) ipc_kmsg_put(msg, kmsg, hdr->msgh_size);
			return(mr);
		}

	    slow_get_rcv_port:
		/*
		 * We have sent the message.  Copy in the receive port.
		 */
		mr = ipc_mqueue_copyin(space, rcv_name,
				       &temp_rcv_mqueue, &temp_rcv_object);
		if (mr != MACH_MSG_SUCCESS) {
			return(mr);
		}
		rcv_mqueue = temp_rcv_mqueue;
		rcv_object = temp_rcv_object;
		/* hold ref for rcv_object; rcv_mqueue is locked */
#if 0
	    slow_receive:
#endif		
		/*
		 *	Now we have sent the request and copied in rcv_name,
		 *	so rcv_mqueue is locked and hold ref for rcv_object.
		 *	Just receive a reply and try to get back to fast path.
		 */

		self->ith_option = option;
		self->ith_scatter_list = MACH_MSG_BODY_NULL;
		self->ith_scatter_list_size = scatter_list_size;

		mr = ipc_mqueue_receive(rcv_mqueue,
					MACH_MSG_OPTION_NONE,
					MACH_MSG_SIZE_MAX,
					MACH_MSG_TIMEOUT_NONE,
					FALSE,
					(void(*)(void))SAFE_EXTERNAL_RECEIVE,
					&temp_kmsg, &temp_seqno); 
		/* rcv_mqueue is unlocked */
		ipc_object_release(rcv_object);

#if	DIPC
		if (mr == MACH_RCV_TRANSPORT_ERROR) {
			if (msg_receive_error(temp_kmsg,
				(rcv_msg != MACH_MSG_NULL) ? rcv_msg : msg,
				option, temp_seqno, space)
			    == MACH_RCV_INVALID_DATA) {
				mr = MACH_RCV_INVALID_DATA;
			}
		}
#endif	/* DIPC */

		if (mr != MACH_MSG_SUCCESS) {
			return(mr);
		}

		kmsg = temp_kmsg;
		hdr = &kmsg->ikm_header;
		send_size = hdr->msgh_size;
		trailer = (mach_msg_format_0_trailer_t *) ((vm_offset_t) hdr +
                                                  round_msg(send_size));
		if (option & MACH_RCV_TRAILER_MASK) {
			trailer->msgh_seqno = temp_seqno;	
			trailer->msgh_trailer_size = REQUESTED_TRAILER_SIZE(option);
		}
		dest_port = (ipc_port_t) hdr->msgh_remote_port;
		HOT(c_mmot_cold_055++);
		goto fast_copyout;

	    slow_copyout:
		/*
		 *	Nothing locked and no references held, except
		 *	we have kmsg with msgh_seqno filled in.  Must
		 *	still check against rcv_size and do
		 *	ipc_kmsg_copyout/ipc_kmsg_put.
		 */

		reply_size = send_size + trailer->msgh_trailer_size;
		if (rcv_size < reply_size) {
			if (msg_receive_error(kmsg, msg, option, temp_seqno,
				        space) == MACH_RCV_INVALID_DATA) {
				mr = MACH_RCV_INVALID_DATA;
				return(mr);
			}
			else {
				mr = MACH_RCV_TOO_LARGE;
				return(mr);
			}
		}

		mr = ipc_kmsg_copyout(kmsg, space, current_map(),
				      MACH_PORT_NAME_NULL, MACH_MSG_BODY_NULL);
		if (mr != MACH_MSG_SUCCESS) {
			if ((mr &~ MACH_MSG_MASK) == MACH_RCV_BODY_ERROR
#if	DIPC
				|| mr == MACH_RCV_TRANSPORT_ERROR
#endif	/* DIPC */
			    ) {
				if (ipc_kmsg_put(msg, kmsg, reply_size) == 
							MACH_RCV_INVALID_DATA)
				    	mr = MACH_RCV_INVALID_DATA;
			} 
			else {
				if (msg_receive_error(kmsg, msg, option,
				    temp_seqno, space) == MACH_RCV_INVALID_DATA)
					mr = MACH_RCV_INVALID_DATA;
			}

			return(mr);
		}

		/* try to get back on optimized path */
		HOT(c_mmot_getback_fast_put++);
		goto fast_put;
#if 0
	    slow_put:
#endif		
		mr = ipc_kmsg_put((rcv_msg != MACH_MSG_NULL)?rcv_msg:msg,
				  kmsg, 
				  hdr->msgh_size + trailer->msgh_trailer_size);
		return(mr);
		/*NOTREACHED*/
	    }
	} /* END OF HOT PATH */
#endif	/* ENABLE_HOTPATH */

	if (option & MACH_SEND_MSG) {
		mr = mach_msg_send(msg, option, send_size,
				   timeout, notify);
		if (mr != MACH_MSG_SUCCESS) {
			return mr;
		}
	}

	if (option & MACH_RCV_MSG) {
		mach_msg_header_t *rcv;

		/*
		 * 1. MACH_RCV_OVERWRITE is on, and rcv_msg is our scatter list
		 *    and receive buffer
		 * 2. MACH_RCV_OVERWRITE is off, and rcv_msg might be the
		 *    alternate receive buffer (separate send and receive buffers).
		 */
		if (option & MACH_RCV_OVERWRITE) 
		    rcv = rcv_msg;
		else if (rcv_msg != MACH_MSG_NULL)
		    rcv = rcv_msg;
		else
		    rcv = msg;
		mr = mach_msg_receive(rcv, option, rcv_size, rcv_name, 
				      timeout, notify, scatter_list_size);
		if (mr != MACH_MSG_SUCCESS) {
			return mr;
		}
	}

	return MACH_MSG_SUCCESS;
}

/*
 *	Routine:	msg_receive_error	[internal]
 *	Purpose:
 *		Builds a minimal header/trailer and copies it to
 *		the user message buffer.  Invoked when in the case of a
 *		MACH_RCV_TOO_LARGE or MACH_RCV_BODY_ERROR error.
 *	Conditions:
 *		Nothing locked.
 *	Returns:
 *		MACH_MSG_SUCCESS	minimal header/trailer copied
 *		MACH_RCV_INVALID_DATA	copyout to user buffer failed
 */
	
mach_msg_return_t
msg_receive_error(
	ipc_kmsg_t		kmsg,
	mach_msg_header_t	*msg,
	mach_msg_option_t	option,
	mach_port_seqno_t	seqno,
	ipc_space_t		space)
{
	mach_msg_format_0_trailer_t *trailer;

	/*
	 * Copy out the destination port in the message.
 	 * Destroy all other rights and memory in the message.
	 */
	ipc_kmsg_copyout_dest(kmsg, space);

	/*
	 * Build a minimal message with the requested trailer.
	 */
	trailer = (mach_msg_format_0_trailer_t *) 
			((vm_offset_t)&kmsg->ikm_header +
			round_msg(sizeof(mach_msg_header_t)));
	kmsg->ikm_header.msgh_size = sizeof(mach_msg_header_t);
	bcopy(  (char *)&trailer_template, 
		(char *)trailer, 
		sizeof(trailer_template));
	if (option & MACH_RCV_TRAILER_MASK) {
		trailer->msgh_seqno = seqno;
		trailer->msgh_trailer_size = REQUESTED_TRAILER_SIZE(option);
	}

	/*
	 * Copy the message to user space
	 */
	if (ipc_kmsg_put(msg, kmsg, kmsg->ikm_header.msgh_size +
			trailer->msgh_trailer_size) == MACH_RCV_INVALID_DATA)
		return(MACH_RCV_INVALID_DATA);
	else 
		return(MACH_MSG_SUCCESS);
}
