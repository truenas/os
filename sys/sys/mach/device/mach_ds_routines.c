/*
 * Adapted from OSF Mach ds_routines.c by Elgin Lee (1998) */
/*
 * Copyright 1991-1998 by Open Software Foundation, Inc. 
 *		All Rights Reserved 
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
 *(C)UNIX System Laboratories, Inc. all or some portions of this file are
 *derived from material licensed to the University of California by
 *American Telephone and Telegraph Co. or UNIX System Laboratories,
 *Inc. and are reproduced herein with the permission of UNIX System
 *Laboratories, Inc.
 */

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
 *	Date:	3/89
 */

#include <mach_kdb.h>
#include <norma_device.h>
#include <zone_debug.h>
#include <mach_kdb.h>
#if	NORMA_DEVICE
#include <dipc.h>
#endif
#include <linux_dev.h>
#if	LINUX_DEV
#include <linux/device-drivers.h> /* file name is consistent with gnumach */
#endif

#include <mach/boolean.h>
#include <mach/kern_return.h>
#include <mach/mig_errors.h>
#include <mach/port.h>
#include <mach/vm_param.h>
#include <mach/notify.h>
#include <mach/mach_server.h>
#include <mach/mach_host_server.h>

#include <machine/machparam.h>		/* spl definitions */

#include <ipc/ipc_port.h>
#include <ipc/ipc_space.h>

#include <kern/spl.h>
#include <kern/ast.h>
#include <kern/counters.h>
#include <kern/queue.h>
#include <kern/zalloc.h>
#include <kern/thread.h>
#include <kern/thread_swap.h>
#include <kern/task.h>
#include <kern/sched_prim.h>
#include <kern/misc_protos.h>

#include <vm/memory_object.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_kern.h>

#include <device/device_types.h>
#include <device/dev_hdr.h>
#include <device/conf.h>
#include <device/io_req.h>
#include <device/ds_routines.h>
#include <device/net_status.h>
#include <device/net_io.h>
#include <device/device_port.h>
#include <device/device_reply.h>
#include <device/device_server.h>
#include <device/device_emul.h>

#include <sys/ioctl.h>
#include <device/ds_status.h>

#include <machine/machparam.h>

#if	NORMA_DEVICE && DIPC
#include <dipc/special_ports.h>
#endif

#if	PARAGON860 || iPSC386 || iPSC860
int	ndf_noise=0;
#endif

/* Forward */

#if	DEBUG
extern void		log_thread_action (char *, long, long, long);
#endif

extern void		io_done_queue_add(io_done_queue_t queue, io_req_t ior);
extern void		io_done_thread_continue(void);

#if	MACH_KDB
unsigned int db_count_io_done_list(io_done_queue_t	queue);
unsigned int db_count_io_done_list_locked(io_done_queue_t	queue);
void db_device(void);
#endif	/* MACH_KDB */

#define DEVICE_LIMITS 0

#define	device_iop_lock(device)		mutex_lock(&(device)->iop_lock)
#define	device_iop_unlock(device)	mutex_unlock(&(device)->iop_lock)

void	device_inc_ioip(
			mach_device_t	device,
			io_req_t	ior);
void	device_dec_ioip(
			mach_device_t	device,
			io_req_t	ior);

extern	struct device_emulation_ops mach_device_emulation_ops;

void
device_inc_ioip(
	mach_device_t	device,
	io_req_t	ior)
{
#if	DEVICE_LIMITS
#if	MACH_KDB
	assert(!ior->io_ioip);
	ior->io_ioip = (int)device;
#endif
	device_iop_lock(device);
	counter(++c_dev_io_tries);
	while (device->io_in_progress >= device->io_in_progress_limit) {
		counter(++c_dev_io_blocks);
		device->iop_wait = TRUE;
		assert_wait((event_t)&device->iop_wait, FALSE);
		device_iop_unlock(device);
		thread_block(0);
		device_iop_lock(device);
	}
	device->io_in_progress++;
	device_iop_unlock(device);
#endif	/* DEVICE_LIMITS */
}

void
device_dec_ioip(
	mach_device_t	device,
	io_req_t	ior)
{
#if	DEVICE_LIMITS
#if	MACH_KDB
	assert(ior->io_ioip);
	ior->io_ioip = 0;
#endif
	device_iop_lock(device);
	device->io_in_progress--;
	if (device->iop_wait) {
		device->iop_wait = FALSE;
		thread_wakeup((event_t)&device->iop_wait);
	}
	device_iop_unlock(device);
#endif	/* DEVICE_LIMITS */
}

#if	MACH_KDB
#include <ddb/db_output.h>
extern int indent;

void
db_show_ior(io_req_t ior)
{
	iprintf("IOR: 0x%x\n", ior);
	indent += 3;
	iprintf("next 0x%x prev 0x%x dev 0x%x dev_prt 0x%x\n",
		ior->io_next, ior->io_prev, ior->io_device, ior->io_dev_ptr);
	iprintf("unit 0x%x op 0x%x ioip 0x%x mode 0x%x recnum 0x%x\n",
		ior->io_unit, ior->io_op, ior->io_ioip, ior->io_mode,
		ior->io_recnum);
	iprintf("data 0x%x sgp 0x%x uaddr 0x%x map 0x%x count 0x%x\n",
		ior->io_data, ior->io_sgp, ior->io_uaddr, ior->io_map, 
		ior->io_count);
	iprintf("alloc_size 0x%x residual 0x%x error 0x%x done 0x%x\n",
		ior->io_alloc_size, ior->io_residual, ior->io_error,
		ior->io_done);
	iprintf("rport 0x%x link 0x%x rlink 0x%x copy 0x%x total 0x%x\n",
		ior->io_reply_port, ior->io_link, ior->io_rlink, 
		ior->io_copy, ior->io_total);
	indent -= 3;
}


#if	DEVICE_LIMITS
#if	ZONE_DEBUG
void	ds_find_ioip(mach_device_t device);
void
ds_find_ioip(mach_device_t device)
{
	io_req_t ior;
	ior = (io_req_t)first_element(io_req_zone);
	while(ior != (io_req_t)0) {
		if (ior->io_ioip == (int)device)
		    db_printf("ior 0x%x\n", ior);
		ior = (io_req_t)next_element(io_req_zone, (vm_offset_t)ior);
	}
}
#endif
#endif
#endif

#if	DEBUG
#define	log_io_map(m,ior)					\
	log_thread_action (m, (long)ior, *(long *)&ior->io_data,\
		(long)ior->io_op)
#else
#define log_io_map(m,ior)
#endif

#define	splio	splsched	/* XXX must block ALL io devices */

#define io_done_queue_request(q)				\
	MACRO_BEGIN						\
	if ((io_done_queue_t) (q) != IO_DONE_QUEUE_NULL) {	\
		spl_t		s = splio();			\
		io_done_queue_lock((io_done_queue_t) (q));	\
		((io_done_queue_t) (q))->io_in_progress++;	\
		io_done_queue_unlock((io_done_queue_t) (q));	\
		splx(s);					\
	}							\
	MACRO_END

security_token_t NULL_SECURITY_TOKEN = { {0, 0} };

/*ARGSUSED*/
static io_return_t
mach_device_open(
	ipc_port_t		reply_port,
	mach_msg_type_name_t	reply_port_type,
	ledger_t		ledger,
	dev_mode_t		mode,
	security_token_t	sec_token,
	char *			name,
	device_t		*device_p)	/* out */
{
	mach_device_t		device;
	register io_return_t	result;
	register io_req_t	ior;
	char			namebuf[64];
	ipc_port_t		notify;
	static int		mynode= -1; /* whoami ? */

	/* caller has checked that reply_port is valid */

#if	NORMA_DEVICE
	if ( mynode == -1 )
		mynode = node_self();
	/*
	 * Translate device name if needed:
	 * With the server sending to the correct MK, device forwarding becomes
	 * an issue only when booting; trying to read pagingfile & server
	 * from the boot file system.
	 */
	/*
	 * Map global device name to <node> + local device name.
	 */
	if (name[0] != '<')
		name = dev_forward_name(name, namebuf, sizeof(namebuf));

	/*
	 * Look for explicit node specifier, e.g., <2>sd0a.
	 * If found, then forward request to correct device server.
	 * If not found, then remove '<n>' and process locally.
	 *
	 * XXX should handle send-right reply_port as well as send-once XXX
	 */
	if (name[0] == '<') {
		char *n;
		int node = 0;

		for (n = &name[1]; *n != '>'; n++) {
			if (*n >= '0' && *n <= '9') {
				node = 10 * node + (*n - '0');
			} else {
				return (D_NO_SUCH_DEVICE);
			}
		}
		if (node == mynode) {
			name = &n[1];	/* skip trailing '>' */
		} else {
#if	DIPC
			forward_device_open_send(
					 dipc_device_port((node_name) node),
					 reply_port, mode, name);
#else	/* DIPC */
			panic("mach_device_open");
#endif	/* DIPC */
			return (MIG_NO_REPLY);
		}
	}
#endif	/* NORMA_DEVICE */

#if	PARAGON860 || iPSC386 || iPSC860
if (ndf_noise) printf("local: %s@%d\n",name,mynode); /* XXX i860 */
#endif	/* PARAGON860 || iPSC386 || iPSC860 */

	/*
	 * Find the local device.
	 */
	result = device_lookup_mode(name, mode, &device);
	if (result != D_SUCCESS) {
#if	PARAGON860 || iPSC386 || iPSC860
if (ndf_noise) printf("device_lookup_mode(%s) failed\n",name);
#endif	/* PARAGON860 || iPSC386 || iPSC860 */
	    return (result);
	}

	/*
	 * If the device is being opened or closed,
	 * wait for that operation to finish.
	 */
	device_lock(device);
	while (device->state == DEV_STATE_OPENING ||
		device->state == DEV_STATE_CLOSING) {
	    device->io_wait = TRUE;
	    thread_sleep_mutex((event_t)device, &device->lock, TRUE);
	    device_lock(device);
	}

	/*
	 * If the device is already open, increment the open count
	 * and return.
	 */
	if (device->state == DEV_STATE_OPEN) {

	    if (device->flag & D_EXCL_OPEN) {
		/*
		 * Cannot open a second time.
		 */
		device_unlock(device);
		mach_device_deallocate(device);
		return (D_ALREADY_OPEN);
	    }

	    assert((device->flag & D_CLONED) == 0);
	    device->open_count++;
	    device_unlock(device);
	    *device_p = &device->dev;
	    return (D_SUCCESS);
	    /*
	     * Return deallocates device reference while acquiring
	     * port.
	     */
	}

	/*
	 * Allocate the device port and register the device before
	 * opening it.
	 */
	device->state = DEV_STATE_OPENING;
	device_unlock(device);

	/*
	 * Allocate port, keeping a reference for it.
	 */
	device->port = ipc_port_alloc_kernel();
	if (device->port == IP_NULL) {
	    device_lock(device);
	    device->state = DEV_STATE_INIT;
	    device->port = IP_NULL;
	    if (device->io_wait) {
		device->io_wait = FALSE;
		thread_wakeup((event_t)device);
	    }
	    device_unlock(device);
	    mach_device_deallocate(device);
	    return (KERN_RESOURCE_SHORTAGE);
	}

	dev_port_enter(device);

	/*
	 * Request no-senders notifications on device port.
	 */
	notify = ipc_port_make_sonce(device->port);
	ip_lock(device->port);
	ipc_port_nsrequest(device->port, 1, notify, &notify);
	assert(notify == IP_NULL);

	/*
	 * Open the device.
	 */
	io_req_alloc(ior);

	ior->io_device	= device;
	ior->io_unit	= device->dev_number;
	ior->io_op	= IO_OPEN | IO_CALL;
	ior->io_mode	= mode;
	ior->io_error	= 0;
	ior->io_done	= ds_open_done;
	ior->io_reply_port = reply_port;
	ior->io_reply_port_type = reply_port_type;
	ior->io_copy	= VM_MAP_COPY_NULL;

	result = (*device->dev_ops->d_open)(device->dev_number, (int)mode, ior);
	if (result == D_IO_QUEUED) {
	    return (MIG_NO_REPLY);
	}

	/*
	 * Return result via ds_open_done.
	 */
	ior->io_error = result;
	(void) ds_open_done(ior);

	io_req_free(ior);

	return (MIG_NO_REPLY);	/* reply already sent */
}

boolean_t
ds_open_done(
	register io_req_t	ior)
{
	kern_return_t		result;
	register mach_device_t	device;
	ipc_port_t		port;

	device = ior->io_device;
	result = ior->io_error;

	if (result != D_SUCCESS) {
	    /*
	     * Open failed.  Deallocate port and device.
	     */
	    dev_port_remove(device);
	    ipc_port_dealloc_kernel(device->port);
	    device->port = IP_NULL;

	    device_lock(device);
	    device->state = DEV_STATE_INIT;
	    if (device->io_wait) {
		device->io_wait = FALSE;
		thread_wakeup((event_t)device);
	    }
	    device_unlock(device);

	    if (IP_VALID(ior->io_reply_port))
		(void) ds_device_open_reply(ior->io_reply_port,
					    ior->io_reply_port_type,
					    result, IP_NULL);
	}
	else {
	    /*
	     * Open succeeded.
	     */
	    device_lock(device);
	    device->state = DEV_STATE_OPEN;
	    device->open_count = 1;
	    if (device->io_wait) {
		device->io_wait = FALSE;
		thread_wakeup((event_t)device);
	    }

	    if (IP_VALID(ior->io_reply_port)) {
		port = device->port;
		ipc_port_reference(port);
		device_unlock(device);
		(void) ds_device_open_reply(ior->io_reply_port,
					    ior->io_reply_port_type,
					    result, port);
		ipc_port_release(port);
	    } else {
		device_unlock(device);
	    }
	    mach_device_deallocate(device);
	}

	return (TRUE);
}

static io_return_t
mach_device_close(
	void	*dev_p)
{
	register mach_device_t	device = (mach_device_t) dev_p;

	/* caller has checked that device is valid */

	device_lock(device);

	/*
	 * If device will remain open, do nothing.
	 */
	if (--device->open_count > 0) {
	    device_unlock(device);
	    return (D_SUCCESS);
	}

	/*
	 * If device is being closed, do nothing.
	 */
	if (device->state == DEV_STATE_CLOSING) {
	    device_unlock(device);
	    return (D_SUCCESS);
	}

	/*
	 * Mark device as closing, to prevent new IO.
	 * Outstanding IO will still be in progress.
	 */
	device->state = DEV_STATE_CLOSING;
	device_unlock(device);

	/*
	 * ? wait for IO to end ?
	 *   only if device wants to
	 */

	/*
	 * Remove the device-port association.
	 */
	dev_port_remove(device);
	ipc_port_dealloc_kernel(device->port);

	/*
	 * Close the device
	 */
	(*device->dev_ops->d_close)(device->dev_number);

	/*
	 * Finally mark it closed.  If someone else is trying
	 * to open it, the open can now proceed.
	 */
	device_lock(device);
	device->state = DEV_STATE_INIT;
	if (device->io_wait) {
	    device->io_wait = FALSE;
	    thread_wakeup((event_t)device);
	}
	device_unlock(device);

	return (D_SUCCESS);
}

/*
 * Write to a device.  This handles synchronous, request/reply, and
 * asynchronous interfaces, with data inband or out-of-line.
 */
static io_return_t
mach_device_write_common(
	void			*dev_p,
	ipc_port_t		reply_port,
	mach_msg_type_name_t	reply_port_type,
	dev_mode_t		mode,
	recnum_t		recnum,
	io_buf_ptr_t		data,
	mach_msg_type_number_t	data_count,
	int			op,
	io_buf_len_t		*bytes_written) /* out */
{
	register io_req_t	ior;
	register io_return_t	result;
	register mach_device_t	device = (mach_device_t) dev_p;
	boolean_t		called_io_completed;

	/*
	 * Refuse if device is dead or not completely open.
	 */
	/* caller has checked that device is valid */

	if (device->state != DEV_STATE_OPEN)
	    return (D_NO_SUCH_DEVICE);

	if (op & IO_QUEUE) {
	    assert(!(op & IO_SYNC));
	    /*
	     * Indicate that an I/O is in progress that will later be
	     * added to the queue.  If the I/O completes in this function,
	     * we will drop this indication in io_completed() below.
	     */
	    io_done_queue_request(reply_port);
	}

	/*
	 * Package the write request for the device driver.
	 */
	io_req_alloc(ior);

	/* XXX note that a CLOSE may proceed at any point */

	device_inc_ioip(device, ior);

	ior->io_device		= device;
	ior->io_unit		= device->dev_number;
	ior->io_op		= op;
	ior->io_mode		= mode;
	ior->io_recnum		= recnum;
	ior->io_data		= data;
	ior->io_count		= data_count;
	ior->io_total		= data_count;
	ior->io_alloc_size	= 0;
	ior->io_residual	= 0;
	ior->io_error		= 0;
	ior->io_done		= ds_write_done;
	ior->io_reply_port	= reply_port;
	ior->io_reply_port_type = reply_port_type;
	ior->io_copy		= VM_MAP_COPY_NULL;

	/*
	 * The ior keeps an extra reference for the device.
	 */
	mach_device_reference(device);

	assert((ior->io_op & IO_WCLONED) == 0);
	result = (*device->dev_ops->d_write)(device->dev_number, ior);

	/*
	 * If the IO was queued, delay reply until it is finished.
	 * D_IO_QUEUED is a promise that io_completed() will be called later;
	 * conversely, if the driver does not return D_IO_QUEUED, it should
	 * not call io_completed() either.
	 * If the operation was synchronous, we wait for it to finish here.
	 * We remember that io_completed() has been called to avoid calling
	 * it again later.  Many drivers do their own wait for synchronous
	 * completion, but some (notably tty drivers) don't.
	 * Synchronous inband operations do not have IO_SYNC set because
	 * they can have a reply message sent by the (*io_done)() function.
	 */
	if (result == D_IO_QUEUED) {
	    if (op & IO_SYNC) {
		iowait(ior);
		called_io_completed = TRUE;
		result = ior->io_error;
	    } else
		return (MIG_NO_REPLY);
	} else
	    called_io_completed = FALSE;

	if (result != D_SUCCESS)
	    ior->io_error = result;
	if (!(op & IO_SYNC))
	    result = MIG_NO_REPLY;
	else if (ior->io_error) /* check if there was an error set rag */
	    result = ior->io_error;

	/*
	 * Return the number of bytes actually written.
	 */
	*bytes_written = ior->io_total - ior->io_residual;

	if (!called_io_completed) {
	    /*
	     * Call io_completed(), which will call ds_write_done() and, for
	     * asynchronous requests, send the reply appropriately.  We need
	     * to supply IO_CALL explicitly to get the (*ior->io_done)()
	     * function called; for synchronous requests it will have been
	     * clear.
	     */
	    assert(ior->io_done == ds_write_done);
	    ior->io_op |= IO_CALL;
	    io_completed(ior, TRUE);
	}

	if (ior->io_op & IO_FREE)
	    io_req_free(ior);

	return (result);
}

/*
 * Wire down incoming memory to give to device in scatter/gather list form.
 */
kern_return_t
device_write_get_sg(
	register io_req_t	ior,
	boolean_t		*wait)
{
	vm_map_copy_t		io_copy;
	struct io_sglist	*sgp;
	register kern_return_t	result;
	int			bsize;
	vm_size_t		min_size;
	kern_return_t		kr;

	/*
	 * By default, caller does not have to wait.
	 */
	*wait = FALSE;


	/*
	 * Nothing to do if no data.
	 */
	if (ior->io_count == (io_buf_len_t)0)
	    return (KERN_SUCCESS);

	/*
	 * Loaned iors already have valid data.
	 */
	if (ior->io_op & IO_LOANED)
	    return (KERN_SUCCESS);

	/*
	 *	Figure out how much data to move this time.  If the device
	 *	won't return a block size, then we have to do the whole
	 *	request in one shot (ditto if this is a block fragment),
	 *	otherwise, move at least one block's worth.
	 */
	result = (*ior->io_device->dev_ops->d_dev_info)(
					ior->io_device->dev_number,
					D_INFO_BLOCK_SIZE,
					(char *)&bsize);

	if (result != KERN_SUCCESS)
		min_size  = (vm_size_t) ior->io_count;
	else if (ior->io_count % (vm_size_t) bsize) { 
		min_size  = (vm_size_t) ior->io_count;
		min_size += (vm_size_t) (bsize - ior->io_count % bsize);
	} 
	else 
		min_size  = (vm_size_t) ior->io_count;

	/*
	 *	Collect the pages from the page list into a scatter/gather list.
	 *	io_data will point to the scatter/gather list.
	 *	io_alloc_size of the total length of the transfer.
	 *	ARD - make sure message is VM_MAP_COPY_PAGE_LIST type.
	 */
	io_copy = (vm_map_copy_t) ior->io_data;
#if	DIPC
	if (io_copy->type != VM_MAP_COPY_PAGE_LIST) {
		assert((io_copy->type == VM_MAP_COPY_OBJECT) ||
			(io_copy->type == VM_MAP_COPY_ENTRY_LIST));
		kr = vm_map_convert_to_page_list(&io_copy);
		assert(kr == KERN_SUCCESS);
		ior->io_data = (io_buf_ptr_t) io_copy;
	}
#endif	/* DIPC */
	assert(io_copy->type == VM_MAP_COPY_PAGE_LIST);
	result = kmem_io_page_list_to_sglist(io_copy, &sgp,
					     (vm_size_t *)&ior->io_alloc_size,
					     min_size);
	if (result != KERN_SUCCESS) {
		return (result);
	}

	ior->io_sgp = sgp;

	if (ior->io_count > sgp->iosg_hdr.length) {

		/*
		 *	Operation has to be split.  Reset io_count for how
		 *	much we can do this time.
		 */
		assert(vm_map_copy_has_cont(io_copy));
		assert(ior->io_count == io_copy->size);
		ior->io_count = sgp->iosg_hdr.length;

		/*
		 *	Caller must wait synchronously.
		 */
		ior->io_op &= ~IO_CALL;
		*wait = TRUE;		
	}

	ior->io_copy = io_copy;			/* vm_map_copy to discard */
	return (KERN_SUCCESS);
}

/*
 * Write to a device.
 */
static io_return_t
mach_device_write(
	void			*dev_p,
	ipc_port_t		reply_port,
	mach_msg_type_name_t	reply_port_type,
	dev_mode_t		mode,
	recnum_t		recnum,
	io_buf_ptr_t		data,
	mach_msg_type_number_t	data_count,
	io_buf_len_t		*bytes_written)	/* out */
{
	return(mach_device_write_common(dev_p, reply_port, reply_port_type,
			       mode, recnum, data, data_count,
			       (IO_WRITE | IO_CALL), bytes_written));
}

/*
 * Write to a device, but memory is in message.
 */
static io_return_t
mach_device_write_inband(
	void			*dev_p,
	ipc_port_t		reply_port,
	mach_msg_type_name_t	reply_port_type,
	dev_mode_t		mode,
	recnum_t		recnum,
	io_buf_ptr_t		data,
	mach_msg_type_number_t	data_count,
	io_buf_len_t		*bytes_written)	/* out */
{
	return(mach_device_write_common(dev_p, reply_port,
	       reply_port_type, mode, recnum, data, data_count,
	       (IO_WRITE | IO_CALL | IO_INBAND), bytes_written));
}

/*
 * Wire down incoming memory to give to device.
 */
kern_return_t
device_write_get(
	io_req_t		ior,
	boolean_t		*wait)
{
	vm_map_copy_t		io_copy;
	vm_offset_t		new_addr;
	register kern_return_t	result;
	kern_return_t		kr;
	int			bsize;
	vm_size_t		min_size;

	/*
	 * By default, caller does not have to wait.
	 */
	*wait = FALSE;

	/*
	 * Nothing to do if no data.
	 */
	if (ior->io_count == 0)
	    return (KERN_SUCCESS);

	/*
	 * Loaned iors already have valid data.
	 */
	if (ior->io_op & IO_LOANED)
	    return (KERN_SUCCESS);

	/*
	 * Inband case.
	 */
	if (ior->io_op & IO_INBAND) {
	    assert(ior->io_count <= sizeof (io_buf_ptr_inband_t));
	    new_addr = zalloc(io_inband_zone);
	    bcopy(ior->io_data, (char *)new_addr, ior->io_count);
	    ior->io_data = (io_buf_ptr_t)new_addr;
	    ior->io_alloc_size = sizeof (io_buf_ptr_inband_t);

	    return (KERN_SUCCESS);
	}
	else {
		boolean_t sgio = FALSE;
		/*
		 *	Figure out if this device can to scatter/gather I/O.
		 */
		result = (*ior->io_device->dev_ops->d_dev_info)(
					ior->io_device->dev_number,
					D_INFO_SGLIST_IO,
					(char *)&sgio);

		if ((result == KERN_SUCCESS) && sgio) {
			/*
			 * XXX	scatter/gather devices can't do kernel buf.
			 */
			if (ior->io_op & IO_KERNEL_BUF)
	panic("Kernel Buffer not supported for scatter/gather devices yet.");

			/*
			 * Yes, note it and do the setup.
			 */
			ior->io_op |= IO_SGLIST;
			return (device_write_get_sg(ior, wait));
		}
	}

	/*
	 *	Kernel buffer case (kernel loaded server).
	 *	vm_map_copyin_page_list (w/ pmap_enter opt)
	 *	must be called because buffer may not be wired.
	 *
	 * XXX	current_map() assumes that driver entry point
	 * XXX	didn't do any request queueing.
	 */
	if (ior->io_op & IO_KERNEL_BUF) {
		result = vm_map_copyin_page_list(current_map(),
				(vm_offset_t) ior->io_data, ior->io_count,
				(VM_PROT_READ | VM_MAP_COPYIN_OPT_PMAP_ENTER),
				&ior->io_copy, FALSE);

		ior->io_alloc_size = ior->io_count;

		/*
		 * XXX	Can't handle continuations!!  Might be able to
		 * XXX	recover by clearing IO_KERNEL_BUF and using code
		 * XXX	below (???).
		 */
		if (vm_map_copy_has_cont(ior->io_copy))
			panic("device_write_get: write too large!!");

		return (result);
	}

	/*
	 *	Figure out how much data to move this time.  If the device
	 *	won't return a block size, then we have to do the whole
	 *	request in one shot (ditto if this is a block fragment),
	 *	otherwise, move at least one block's worth.
	 */
	result = (*ior->io_device->dev_ops->d_dev_info)(
					ior->io_device->dev_number,
					D_INFO_BLOCK_SIZE,
					(char *)&bsize);

	if (result != KERN_SUCCESS || ior->io_count < (vm_size_t) bsize)
		min_size = (vm_size_t) ior->io_count;
	else
		min_size = (vm_size_t) bsize;

	/*
	 *	Map the pages from this page list into memory.
	 *	io_data records location of data.
	 *	io_alloc_size is the vm size of the region to deallocate.
	 */
	io_copy = (vm_map_copy_t) ior->io_data;
#if	DIPC
	if (io_copy->type != VM_MAP_COPY_PAGE_LIST) {
		assert((io_copy->type == VM_MAP_COPY_OBJECT) ||
			(io_copy->type == VM_MAP_COPY_ENTRY_LIST));
		kr = vm_map_convert_to_page_list(&io_copy);
		assert(kr == KERN_SUCCESS);
		ior->io_data = (io_buf_ptr_t) io_copy;
	}
#endif	/* DIPC */
	assert(io_copy->type == VM_MAP_COPY_PAGE_LIST);
	result = kmem_io_map_copyout(device_io_map,
				     (vm_offset_t *)&ior->io_data,
				     (vm_size_t *)&ior->io_alloc_size,
				     io_copy, min_size, VM_PROT_READ);
	if (result != KERN_SUCCESS)
	    return (result);

log_io_map ("device_write_get", ior);

	if ((ior->io_data + ior->io_count) > 
	    ((io_buf_ptr_t)(trunc_page(ior->io_data) + ior->io_alloc_size))) {

		/*
		 *	Operation has to be split.  Reset io_count for how
		 *	much we can do this time.
		 */
		assert(vm_map_copy_has_cont(io_copy));
		assert(ior->io_count == io_copy->size);
		ior->io_count = ior->io_alloc_size -
		    (ior->io_data - (io_buf_ptr_t)(trunc_page(ior->io_data)));

		/*
		 *	Caller must wait synchronously. Remember whether
		 *	it was IO_CALL or IO_QUEUE.
		 */
		ior->io_save = ior->io_op;
		ior->io_op &= ~(IO_CALL|IO_QUEUE);
		*wait = TRUE;		
	}

	ior->io_copy = io_copy;			/* vm_map_copy to discard */
	return (KERN_SUCCESS);
}

/*
 * Clean up memory allocated for IO.
 */
boolean_t
device_write_dealloc(
	io_req_t	ior)
{
	vm_map_copy_t	new_copy = VM_MAP_COPY_NULL;
	register
	vm_map_copy_t	io_copy;
	kern_return_t	result;
	vm_offset_t	size_to_do;
	int		bsize;	

	if (ior->io_alloc_size == (io_buf_len_t)0)
	    return (TRUE);

	/*
	 * Inband case.
	 */
	if (ior->io_op & IO_INBAND) {
	    zfree(io_inband_zone, (vm_offset_t)ior->io_data);

	    return (TRUE);
	}
	
	/*
	 * Kernel buffer case.	Free page list that is keeping pages wired.
	 */
	if (ior->io_op & IO_KERNEL_BUF) {
	    vm_map_copy_discard(ior->io_copy);

	    return (TRUE);
	}

	if ((io_copy = ior->io_copy) == VM_MAP_COPY_NULL)
	    return (TRUE);
	assert(io_copy->type == VM_MAP_COPY_PAGE_LIST);

	if (ior->io_op & IO_SGLIST) {
		io_sglist_free(ior->io_sgp);
	} else {
		/*
		 *	To prevent a possible deadlock with the default pager,
		 *	we have to release space in the device_io_map before
		 *	we allocate any memory.	 (Which vm_map_copy_invoke_cont
		 *	might do.)  See the discussion in ds_init.
		 */

log_io_map ("device_write_dealloc", ior);
		kmem_io_map_deallocate(device_io_map,
				       trunc_page(ior->io_data),
				       (vm_size_t) ior->io_alloc_size);
	}

	if (vm_map_copy_has_cont(io_copy)) {

		/*
		 *	Remember how much is left, then 
		 *	invoke or abort the continuation.
		 */
		size_to_do = io_copy->size - ior->io_count;
		if (ior->io_error == 0) {
			vm_map_copy_invoke_cont(io_copy, &new_copy, &result);
		}
		else {
			vm_map_copy_abort_cont(io_copy);
			result = KERN_FAILURE;
		}

		if (result == KERN_SUCCESS && new_copy != VM_MAP_COPY_NULL) {
			register int	res;

			/*
			 *	We have a new continuation, reset the ior to
			 *	represent the remainder of the request.	 Must
			 *	adjust the recnum because drivers assume
			 *	that the residual is zero.
			 */
			ior->io_op &= ~(IO_DONE|IO_CALL|IO_QUEUE);
			ior->io_op |= (ior->io_save & (IO_CALL|IO_QUEUE));

			assert((ior->io_op & IO_WCLONED) == 0);
			res = (*ior->io_device->dev_ops->d_dev_info)(
					ior->io_device->dev_number,
					D_INFO_BLOCK_SIZE,
					(char *)&bsize);

			if (res != D_SUCCESS)
				panic("device_write_dealloc: No block size");
			
			ior->io_recnum += ior->io_count/bsize;
			ior->io_count = new_copy->size;
		}
		else {

			/*
			 *	No continuation.  Add amount we didn't get
			 *	to into residual.
			 */
			ior->io_residual += size_to_do;
		}
	}

	/*
	 *	Clean up the state for the IO that just completed.
	 */
	vm_map_copy_discard(ior->io_copy);
	ior->io_copy = VM_MAP_COPY_NULL;
	ior->io_data = (char *) new_copy;

	/*
	 *	Return FALSE if there's more IO to do.
	 */

	return(new_copy == VM_MAP_COPY_NULL);
}

/*
 * Send write completion message to client, and discard the data.
 */
boolean_t
ds_write_done(
	io_req_t	ior)
{
	/*
	 *	device_write_dealloc discards the data that has been
	 *	written, but may decide that there is more to write.
	 */
	while (!device_write_dealloc(ior)) {
		register io_return_t	result;
		register mach_device_t	device;

		/*
		 *     More IO to do -- invoke it.
		 */
		assert((ior->io_op & IO_WCLONED) == 0);
		device = ior->io_device;
		result = (*device->dev_ops->d_write)(device->dev_number, ior);

		/*
		 * If the IO was queued, return FALSE -- not done yet.
		 */
		if (result == D_IO_QUEUED) {
			if (ior->io_op & IO_QUEUE)
				io_done_queue_request(ior->io_reply_port);
			return (FALSE);
		}
	}

	/*
	 *	Now the write is really complete.  Send reply.
	 */

	if (!(ior->io_op & (IO_SYNC|IO_QUEUE)) && IP_VALID(ior->io_reply_port))
	{
	    (void) (*((ior->io_op & IO_INBAND) ?
		      ds_device_write_reply_inband :
		      ds_device_write_reply))(ior->io_reply_port,
					      ior->io_reply_port_type,
					      ior->io_error,
					      (ior->io_total -
						     ior->io_residual));
	}
	device_dec_ioip(ior->io_device, ior);

	/*
	 * Remove the extra reference if write has not been cloned.
	 */
	if ((ior->io_op & IO_WCLONED) == 0)
		mach_device_deallocate(ior->io_device);

	return (TRUE);
}

/*
 * Read from a device.
 */
static io_return_t
mach_device_read_common(
	void			*dev_p,
	ipc_port_t		reply_port,
	mach_msg_type_name_t	reply_port_type,
	dev_mode_t		mode,
	recnum_t		recnum,
	io_buf_len_t		bytes_wanted,
	int			op,
	io_buf_ptr_t		*data,		/* out */
	mach_msg_type_number_t	*data_count)	/* out */
{
	register io_req_t	ior;
	register io_return_t	result;
	register mach_device_t	device = (mach_device_t) dev_p;
	boolean_t		called_io_completed;

	/*
	 * Refuse if device is dead or not completely open.
	 */
	/* caller has checked that device is valid */

	if (device->state != DEV_STATE_OPEN)
	    return (D_NO_SUCH_DEVICE);

	/* XXX note that a CLOSE may proceed at any point */

	if (op & IO_QUEUE) {
	    assert(!(op & IO_SYNC));
	    /*
	     * Indicate that an I/O is in progress that will later be
	     * added to the queue.  If the I/O completes in this function,
	     * we will drop this indication in io_done_queue_add() below.
	     */
	    io_done_queue_request(reply_port);
	}

	if ((op & IO_INBAND) && bytes_wanted > sizeof (io_buf_ptr_inband_t)) {
	    assert(!(op & IO_SYNC));	/* Reply sent from ds_read_done().  */
	    bytes_wanted = sizeof (io_buf_ptr_inband_t);
	}

	/*
	 * Package the read request for the device driver
	 */
	io_req_alloc(ior);

	device_inc_ioip(device, ior);

	ior->io_device		= device;
	ior->io_unit		= device->dev_number;
	ior->io_op		= op;
	ior->io_mode		= mode;
	ior->io_recnum		= recnum;
	if (op & IO_OVERWRITE) {
	    ior->io_uaddr	= (vm_address_t) *data;
	} else {
	    ior->io_data	= 0;		/* driver must allocate data */
	}
	ior->io_map		= current_act()->map;
	ior->io_count		= bytes_wanted;
	ior->io_alloc_size	= 0;		/* no data allocated yet */
	ior->io_residual	= 0;
	ior->io_error		= 0;
	ior->io_done		= ds_read_done;
	ior->io_reply_port	= reply_port;
	ior->io_reply_port_type	= reply_port_type;
	ior->io_copy		= VM_MAP_COPY_NULL;

	/*
	 * The ior keeps an extra reference for the device.
	 */
	mach_device_reference(device);

	/*
	 * And do the read.
	 */
	result = (*device->dev_ops->d_read)(device->dev_number, ior);

	/*
	 * If the IO was queued, delay reply until it is finished.
	 * D_IO_QUEUED is a promise that io_completed() will be called later;
	 * conversely, if the driver does not return D_IO_QUEUED, it should
	 * not call io_completed() either.
	 * If the operation was synchronous, we wait for it to finish here.
	 * We remember that io_completed() has been called to avoid calling
	 * it again later.  Many drivers do their own wait for synchronous
	 * completion, but some (notably tty drivers) don't.
	 * Synchronous inband operations do not have IO_SYNC set because
	 * they can have a reply message sent by the (*io_done)() function.
	 */
	if (result == D_IO_QUEUED) {
	    if (op & IO_SYNC) {
		iowait(ior);
		called_io_completed = TRUE;
		result = ior->io_error;
	    } else
		return (MIG_NO_REPLY);
	} else
	    called_io_completed = FALSE;

	/*
	 * Return result via ds_read_done.
	 * If there was a problem queueing the IO, report it.
	 * Otherwise, return status of IO.
	 */
	if (result != D_SUCCESS)
	    ior->io_error = result;
	if (!(op & IO_SYNC))
	    result = MIG_NO_REPLY;
	else
	    result = ior->io_error;

	if (!called_io_completed) {
	    /*
	     * Call io_completed(), which will call ds_read_done() and, for
	     * asynchronous requests, send the reply appropriately.  We need
	     * to supply IO_CALL explicitly to get the (*ior->io_done)()
	     * function called; for synchronous requests it will have been
	     * clear.
	     */
	    assert(ior->io_done == ds_read_done);
	    ior->io_op |= IO_CALL;
	    io_completed(ior, TRUE);
	}

	*data = ior->io_data;
	*data_count = ior->io_total;

	if (ior->io_op & IO_FREE)
	    io_req_free(ior);

	return (result);
}

/*
 * Allocate wired-down memory for device read with scatter/gather lists.
 */
kern_return_t
device_read_alloc_sg(
	io_req_t		ior,
	vm_size_t		size)
{
	vm_offset_t		addr;
	kern_return_t		kr;
	int			nentries, ii;
	struct io_sglist	*sgp;
	vm_size_t		xfer_length;

	/*
	 * If io_device field is not present we assume the allocate size
	 * is correct.	Otherwise, call d_dev_info to check the device
	 * block size and possibly adjust the allocate size.
	 */
	if (ior->io_device) {
		int		bsize;
		vm_size_t	min_size;

		/*
		 * Figure out how much data to move this time.	If the device
		 * won't return a block size, then we have to do the whole
		 * request in one shot (ditto if this is a block fragment),
		 * otherwise, move at least one block's worth.
		 */
		kr = (*ior->io_device->dev_ops->d_dev_info)(
						ior->io_device->dev_number,
						D_INFO_BLOCK_SIZE,
						(char *)&bsize);

		if (kr != KERN_SUCCESS)
			min_size  = (vm_size_t) ior->io_count;
		else if (ior->io_count % (vm_size_t) bsize) { 
			min_size  = (vm_size_t) ior->io_count;
			min_size += (vm_size_t) (bsize - ior->io_count % bsize);
		} 
		else 
			min_size  = (vm_size_t) ior->io_count;

		min_size = round_page(min_size);
		if (size < min_size)
			size = min_size;
	}

	xfer_length = size;
	size = round_page(size);
	nentries = size/PAGE_SIZE;
	io_sglist_alloc(sgp, nentries);

	kr = kmem_alloc(kernel_map, &addr, size);
	if (kr != KERN_SUCCESS) {
		io_sglist_free(sgp);
		return (kr);
	}

	ior->io_data = (io_buf_ptr_t) addr;
	ior->io_alloc_size = size;

	/*
	 * Fill in the scatter/gather list.
	 */
	for (ii = 0; ii < nentries; ii++) {
		sgp->iosg_list[ii].iosge_length = PAGE_SIZE;
		sgp->iosg_list[ii].iosge_phys =
			pmap_extract(vm_map_pmap(kernel_map), addr);
		addr += PAGE_SIZE;
	}
	sgp->iosg_list[nentries-1].iosge_length -= (size - xfer_length);
	sgp->iosg_hdr.length = xfer_length;
	ior->io_sgp = sgp;
	ior->io_op |= IO_SGLIST;
	return(KERN_SUCCESS);
}

/*
 * Read from a device.
 */
static io_return_t
mach_device_read(
	void			*dev_p,
	ipc_port_t		reply_port,
	mach_msg_type_name_t	reply_port_type,
	dev_mode_t		mode,
	recnum_t		recnum,
	io_buf_len_t		bytes_wanted,
	io_buf_ptr_t		*data,		/* out */
	mach_msg_type_number_t	*data_count)	/* out */
{
	return(mach_device_read_common(dev_p, reply_port, reply_port_type,
			      mode, recnum, bytes_wanted,
			      (IO_READ | IO_CALL), data, data_count));
}

/*
 * Read from a device, but return the data 'inband.'
 */
static io_return_t
mach_device_read_inband(
	void			*dev_p,
	ipc_port_t		reply_port,
	mach_msg_type_name_t	reply_port_type,
	dev_mode_t		mode,
	recnum_t		recnum,
	io_buf_len_t		bytes_wanted,
	io_buf_ptr_inband_t	data,		/* pointer to OUT array */
	mach_msg_type_number_t	*data_count)	/* out */
{
	io_buf_ptr_t		data_junk;

	return(mach_device_read_common(dev_p, reply_port,
		reply_port_type, mode, recnum, bytes_wanted,
		(IO_READ | IO_CALL | IO_INBAND), &data_junk, data_count));
}

/*
 * Allocate wired-down memory for device read.
 */
kern_return_t
device_read_alloc(
	io_req_t	ior,
	vm_size_t	size)
{
	vm_offset_t		addr;
	kern_return_t		kr;
	boolean_t		sgio = FALSE;

	/*
	 * Nothing to do if no data.
	 */
	if (ior->io_count == (io_buf_len_t)0) {
	    ior->io_data = NULL;
	    return (KERN_SUCCESS);
	}

	if (ior->io_op & IO_INBAND) {
		ior->io_data = (io_buf_ptr_t) zalloc(io_inband_zone);
		ior->io_alloc_size = sizeof(io_buf_ptr_inband_t);
		return (KERN_SUCCESS);
	}

	/*
	 *	Figure out if this device can do scatter/gather I/O.
	 */
	kr = (*ior->io_device->dev_ops->d_dev_info)(
				ior->io_device->dev_number,
				D_INFO_SGLIST_IO,
				(char *)&sgio);
	if (kr != KERN_SUCCESS)
		sgio = FALSE;
	
	if (ior->io_op & IO_OVERWRITE) {
		/*
		 * XXX	Just punt on scatter/gather for now.  This
		 * XXX	is probably a simple matter of extracting
		 * XXX	the guts of device_read_alloc_sg into a
		 * XXX	routine that converts a page list to an sglist
		 * XXX	and calling it in the appropriate places below.
		 */
		if (sgio)
	panic("Overwrite not supported for scatter/gather devices yet");

		if (ior->io_op & IO_KERNEL_BUF) {

			/*
			 *	Set up kernel buffer for device.
			 */
			kr = vm_map_copyin_page_list(ior->io_map,
				ior->io_uaddr,
				ior->io_count,
				(VM_PROT_READ | VM_PROT_WRITE |
				 VM_MAP_COPYIN_OPT_PMAP_ENTER |
				 VM_MAP_COPYIN_OPT_NO_ZERO_FILL),
				&ior->io_copy,
				FALSE);

			if (kr != KERN_SUCCESS)
				return (kr);

			/*
			 * XXX	Can't handle continuations!!  Might be able to
			 * XXX	recover by clearing IO_KERNEL_BUF and using
			 * XXX	code below (???).
			 */
			if (vm_map_copy_has_cont(ior->io_copy))
		panic("device_read_alloc: overwrite read too large!!");

			ior->io_data = (io_buf_ptr_t) ior->io_uaddr;
			ior->io_alloc_size = ior->io_count;
		}
		else {

			/*
			 *	Map user pages directly for device.  Pages in a
			 *	page list are wired.  This code does not do any
			 *	splitting of large operations.
			 */
			kr = vm_map_copyin_page_list(ior->io_map,
				ior->io_uaddr,
				ior->io_count,
				VM_PROT_READ|VM_PROT_WRITE,
				&ior->io_copy,
				FALSE);

			if (kr != KERN_SUCCESS)
				return (kr);

			kr = kmem_io_map_copyout(device_io_map,
				(vm_offset_t *)&ior->io_data,
				(vm_size_t *)&ior->io_alloc_size,
				ior->io_copy,
				ior->io_count,
				VM_PROT_READ|VM_PROT_WRITE);

			if (kr != KERN_SUCCESS) {
				vm_map_copy_discard(ior->io_copy);
				return (kr);
			}

log_io_map ("device_read_alloc", ior);

		}
	}

	/*
	 *	This is the !IO_OVERWRITE case.
	 */
	else {
		if (sgio) {
			kr = device_read_alloc_sg(ior, size);
			if (kr != KERN_SUCCESS)
			    return (kr);
		}
		else {
			size = round_page(size);
			kr = kmem_alloc(kernel_map, &addr, size);
			if (kr != KERN_SUCCESS)
				return (kr);
			ior->io_data = (io_buf_ptr_t) addr;
			ior->io_alloc_size = size;
		}
	}

	return (KERN_SUCCESS);
}

boolean_t
ds_read_done(
	io_req_t		ior)
{
	vm_offset_t		start_data, end_data;
	vm_offset_t		start_sent, end_sent;
	register io_buf_len_t	size_read;

	if (ior->io_error)
	    size_read = 0;
	else
	    size_read = ior->io_count - ior->io_residual;

	start_data  = (vm_offset_t)ior->io_data;
	end_data    = start_data + size_read;

	start_sent  = (ior->io_op & IO_INBAND) ? start_data :
						trunc_page(start_data);
	end_sent    = (ior->io_op & IO_INBAND) ? 
		start_data + ior->io_alloc_size : round_page(end_data);

	/*
	 * Zero memory that the device did not fill.  Overwrite case
	 * doesn't need to do this because device wrote directly to
	 * target buffer.  Amount of data read is returned to client,
	 * so short read is handled there, not here.
	 */
	if ((ior->io_op & IO_OVERWRITE) == 0) {
		if (start_sent < start_data)
		    bzero((char *)start_sent, start_data - start_sent);
		if (end_sent > end_data)
		    bzero((char *)end_data, end_sent - end_data);
	}

	/*
	 * Mark the data dirty (if the pages were filled by DMA, the
	 * pmap module may think that they are clean).
	 */
	pmap_modify_pages(kernel_pmap, start_sent, end_sent);

	/*
	 * Send the data to the reply port - this
	 * unwires and deallocates it.
	 */
	if (ior->io_op & IO_INBAND && ior->io_op & IO_QUEUE)
		goto out;
	else if (ior->io_op & IO_INBAND) {
		assert((ior->io_op & IO_SYNC) == 0);
		(void)ds_device_read_reply_inband(ior->io_reply_port,
					      ior->io_reply_port_type,
					      ior->io_error,
					      (char *) start_data,
					      size_read);
	}
	else if (ior->io_op & IO_OVERWRITE) {

		/*
		 *	The clever code in device_read_alloc has arranged
		 *	for the bytes to land exactly where they were wanted.
		 *	In the kernel buf case, no kernel mapping was needed.
		 *	Just have a little clean up left to do here.
		 */

		if ((ior->io_op & IO_KERNEL_BUF) == 0) {

log_io_map ("ds_read_done", ior);

			kmem_io_map_deallocate(device_io_map,
					trunc_page(ior->io_data),
					(vm_size_t) ior->io_alloc_size);
		}

		vm_map_copy_discard(ior->io_copy);

		ior->io_copy = VM_MAP_COPY_NULL;
		ior->io_data = (io_buf_ptr_t) 0;

		if (ior->io_op & (IO_SYNC | IO_QUEUE))
			ior->io_total = size_read;
		else 
			(void)ds_device_read_reply_overwrite(
					ior->io_reply_port,
					ior->io_reply_port_type,
					ior->io_error,
					size_read);

	}
	else {
		vm_map_copy_t copy;
		kern_return_t kr;

		kr = vm_map_copyin(kernel_map, start_data, size_read,
					   TRUE, &copy);

		if (kr != KERN_SUCCESS) {
			ior->io_error = kr;
		}
		else if (ior->io_op & (IO_SYNC | IO_QUEUE)) {
			if (copy != VM_MAP_COPY_NULL) {
				vm_map_t map;
				assert(ior->io_map != VM_MAP_NULL); /* XXX */
				if (ior->io_map == VM_MAP_NULL) {
					/* should never happen */
					map = current_map();
				} else {
					map = ior->io_map;
				}
				kr = vm_map_copyout(ior->io_map,
					(vm_offset_t *) (&ior->io_data), copy);
			}
			if (kr != KERN_SUCCESS) {
				ior->io_error = kr;
				ior->io_total = 0;
				vm_map_copy_discard(copy);
			}
			else
				ior->io_total = size_read;
		}
		else {
			(void)ds_device_read_reply(ior->io_reply_port,
						   ior->io_reply_port_type,
						   ior->io_error,
						   (char *) copy,
						   size_read);
		}
	}

	/*
	 * Free any memory that was allocated but not sent.
	 */
	if (ior->io_count != (io_buf_len_t)0) {
		if (ior->io_op & IO_INBAND) {
			if (ior->io_alloc_size > (io_buf_len_t)0)
				zfree(io_inband_zone,
				      (vm_offset_t)ior->io_data);
		} else {
			register vm_offset_t	end_alloc;

			if ((ior->io_op & IO_OVERWRITE) == 0) {
				end_alloc = start_sent + round_page(ior->io_alloc_size);
				if (end_alloc > end_sent) {
					kmem_free(kernel_map, end_sent,
						  end_alloc - end_sent);
				}
			}
			if (ior->io_op & IO_SGLIST)
				io_sglist_free(ior->io_sgp);
		}

	}

out:
	device_dec_ioip(ior->io_device, ior);

	/*
	 * Remove the extra reference if write has not been cloned.
	 */
	if ((ior->io_op & IO_WCLONED) == 0)
		mach_device_deallocate(ior->io_device);

	return (TRUE);
}

static io_return_t
mach_device_set_status(
	void			*dev_p,
	dev_flavor_t		flavor,
	dev_status_t		status,
	mach_msg_type_number_t	status_count)
{
	register mach_device_t	device = (mach_device_t) dev_p;

	/*
	 * Refuse if device is dead or not completely open.
	 */
	/* caller has checked that device is valid */

	if (device->state != DEV_STATE_OPEN)
	    return (D_NO_SUCH_DEVICE);

	/* XXX note that a CLOSE may proceed at any point */

	switch (flavor) {
	      case MDIOCSETL:
		if (status[0] < 0 || status[0] > IO_IN_PROGRESS_MAX)
		    return(D_INVALID_OPERATION);
		device->io_in_progress_limit = status[0];
		return (D_SUCCESS);
		break;
	      default:
		if (!device->dev_ops->d_setstat)
			return (D_INVALID_OPERATION);
		return ((*device->dev_ops->d_setstat)(device->dev_number,
						      flavor,
						      status,
						      status_count));
	}
}

static io_return_t
mach_device_get_status(
	void			*dev_p,
	dev_flavor_t		flavor,
	dev_status_t		status,		/* pointer to OUT array */
	mach_msg_type_number_t	*status_count)	/* in/out */
{
	register mach_device_t	device = (mach_device_t) dev_p;

	/*
	 * Refuse if device is dead or not completely open.
	 */
	/* caller has checked that device is valid */

	if (device->state != DEV_STATE_OPEN)
	    return (D_NO_SUCH_DEVICE);

	/* XXX note that a CLOSE may proceed at any point */

	switch (flavor) {
	      case MDIOCGETL:
		status[0] = device->io_in_progress_limit;
		*status_count = 1;
		return (D_SUCCESS);
		break;
	      default:
		if (!*device->dev_ops->d_getstat)
			return (D_INVALID_OPERATION);
		return ((*device->dev_ops->d_getstat)(device->dev_number,
						      flavor,
						      status,
						      status_count));
	}
}

static io_return_t
mach_device_set_filter(
	void			*dev_p,
	ipc_port_t		receive_port,
	int			priority,
	filter_t		filter[],	/* pointer to IN array */
	mach_msg_type_number_t	filter_count)
{
	register mach_device_t	device = (mach_device_t) dev_p;

	/*
	 * Refuse if device is dead or not completely open.
	 */
	/* caller has checked that device is valid */

	if (device->state != DEV_STATE_OPEN)
	    return (D_NO_SUCH_DEVICE);

	/* XXX note that a CLOSE may proceed at any point */

	/*
	 * Request is absurd if no receive port is specified.
	 */
	if (!IP_VALID(receive_port))
	    return (D_INVALID_OPERATION);

	return ((*device->dev_ops->d_async_in)(device->dev_number,
					       receive_port,
					       priority,
					       filter,
					       filter_count,
					       device));
}

static io_return_t
mach_device_map(
	void			*dev_p,
	vm_prot_t		protection,
	vm_offset_t		offset,
	vm_size_t		size,
	ipc_port_t		*pager,	/* out */
	boolean_t		unmap)	/* ? */
{
	register mach_device_t	device = (mach_device_t) dev_p;

#ifdef	lint
	unmap = unmap;
#endif	/* lint */
	if (protection & ~VM_PROT_ALL)
		return (KERN_INVALID_ARGUMENT);
	/*
	 * Refuse if device is dead or not completely open.
	 */
	/* caller has checked that device is valid */

	if (device->state != DEV_STATE_OPEN)
	    return (D_NO_SUCH_DEVICE);

	/* XXX note that a CLOSE may proceed at any point */

	return (device_pager_setup(device, protection, offset, size,
				    pager));
}

/*
 * Handle the No-More_Senders notification generated from a device port destroy.
 * Since there are no longer any tasks which hold a send right to this device
 * port a NMS notification has been generated. We check the device state and
 * if it's closing then this becomes a no-op, otherwise the task holding the
 * send right has died without performing a device_close() call. We clean house
 * here by calling ds_device_close() on the task's behalf.
 */
static void
mach_no_senders(
	mach_no_senders_notification_t	*notification)
{
	device_t		device;
	ipc_port_t		dev_port;
	extern device_t		dev_port_lookup(ipc_port_t);

	dev_port = (ipc_port_t) notification->not_header.msgh_remote_port;

	/*
	 * convert a port to its device structure.
	 */
	if ( (device = dev_port_lookup( dev_port )) != DEVICE_NULL ) {
		io_return_t	rc;
		short		state;
		mach_device_t	mach_device;

		// assert( device->emul_ops == mach_device_emulation_ops );
		mach_device = (mach_device_t) device->emul_data;

		/*
		 * release reference created by previous dev_port_lookup()
		 */
		mach_device_deallocate(mach_device);

		assert( mach_device->port == dev_port );

		/*
		 * If the device is already being closed (NMS generated by
		 * calling mach_device_close()), then do nothing as this
		 * NMS notification is extra baggage at this juncture.
		 */
		device_lock(mach_device);
		state = mach_device->state;
		if ( state == DEV_STATE_CLOSING ) {
			device_unlock(mach_device);
			return;
		}

		/*
		 * When a task unexpectedly terminates, the reference
		 * count on the device port is decremented, but not
		 * the reference count on the device.  As a result, we
		 * may be about to call device close with a reference
		 * count above 1.  This won't do any good at all.
		 * Based on the assumption that the device port is the
		 * *only* thing that holds a reference to the device
		 * structure, we may safely decrease the device open
		 * count to 1 before calling device_close.
		 *
		 * There are two possible problems with this: a) the
		 * assumption may be wrong (something besides the
		 * device port may have a reference to the device),
		 * and b) conceptually it's wrong, in that we are
		 * allowing the device reference count to be left
		 * incorrect and then manually correcting it.  In some
		 * sense, if we are going to ignore the data
		 * structure, why have it at all?
		 */

		mach_device->open_count = 1;
		device_unlock(mach_device);


		/*
		 * The task which owned the last send right has died without
		 * doing a mach_device_close(), do it now.
		 */
		if ( (rc=mach_device_close(mach_device)) != D_SUCCESS )
			printf("mach_no_senders() mach_device_close(%x) rc %d\n",
				mach_device,rc);
	}
}

struct io_done_queue io_done_thread_queue;

void
io_completed(
	io_req_t	ior,
	boolean_t	can_block)
{
	spl_t		s;
	io_done_queue_t	queue;

	/*
	 * If this ior was loaned to us, return it directly.
	 */
	if (ior->io_op & IO_LOANED) {
		(*ior->io_done)(ior);
		return;
	}
	s = splio();
	if (!(ior->io_op & (IO_QUEUE | IO_CALL))) {
	    /* Someone is doing (or will do) io_wait(). */
	    ior_lock(ior);
	    ior->io_op |= IO_DONE;
	    ior->io_op &= ~IO_WANTED;
	    ior_unlock(ior);
	    thread_wakeup((event_t)ior);
	} else {
	    /*
	     * Mark the ior as done and freeable.  We will cancel the free
	     * bit below if we end up putting the ior on a queue; otherwise
	     * it is no longer accessible and the caller should do io_req_free
	     * after extracting any needed fields.
	     */
	    ior->io_op |= IO_DONE | IO_FREE;

	    /*
	     * If this was not a *_async request, or if there was no reply
	     * queue specified, it should be processed by the kernel's
	     * io_done_thread.
	     */
	    if (!(ior->io_op & IO_QUEUE)
		|| (queue = (io_done_queue_t) ior->io_reply_port) ==
		   IO_DONE_QUEUE_NULL)
		queue = &io_done_thread_queue;

	    /*
	     * If we're called from a context where we can block
	     * (io_done_thread, synchronous I/O, or asynchronous I/O that
	     * completed within mach_device_read/write_common()), call the
	     * completion function now.
	     */
	    if (can_block && ior->io_done != 0) {
		splx(s);
		if (!(*ior->io_done)(ior)) {
		    ior->io_op &= ~(IO_DONE|IO_FREE);
		    return;
		}
		s = splio();
		ior->io_done = 0;
	    }

	    /*
	     * If our only reason for queueing to the io_done_thread was to
	     * get the (*io_done)() function executed and we were able to do
	     * it here, nothing further need be done.
	     */
	    if (queue == &io_done_thread_queue) {
		assert(queue->handoffs == 0);
		if (ior->io_done == 0)
		    goto out;
	    } else {
		/*
		 * If we're about to queue to a user io_done_queue_wait()
		 * queue, we may need to hand the ior to the io_done_thread
		 * instead.  This happens if we have an (*io_done)() to
		 * execute and nobody is doing io_done_queue_wait() right now,
		 * since otherwise we might wait indefinitely for operations
		 * such as unbusying pages which should be punctual; or if
		 * other iors have already been queued to the io_done_thread
		 * and not yet processed, since otherwise the replies would
		 * get out of order.
		 */
		io_done_queue_lock(queue);
		if ((queue->waiters <= 0 && ior->io_done != 0)
		    || (queue->handoffs > 0 && !(ior->io_op & IO_HANDOFF))) {
		    assert(queue->handoffs >= 0);
		    queue->handoffs++;
		    io_done_queue_unlock(queue);
		    queue = &io_done_thread_queue;
		    ior->io_op |= IO_HANDOFF;
		} else {
		    if (ior->io_op & IO_HANDOFF) {
			queue->handoffs--;
			assert(queue->handoffs >= 0);
		    }
		    io_done_queue_unlock(queue);
		}
	    }
	    ior->io_op &= ~IO_FREE;
	    io_done_queue_add(queue, ior);
	}
out:
	splx(s);
}


int	iodone_thread_fixpri = -1;

void
io_done_thread(void)
{
    {
	thread_t			thread;
	processor_set_t			pset;
	kern_return_t			ret;
	policy_base_t			base;
	policy_limit_t			limit;
	policy_fifo_base_data_t		fifo_base;
	policy_fifo_limit_data_t	fifo_limit;
	extern void vm_page_free_reserve(int pages);

	/*
	 * Set thread privileges.
	 */
	thread = current_thread();
	current_thread()->vm_privilege = TRUE;
	vm_page_free_reserve(5);	/* XXX */
	stack_privilege(current_thread());
	thread_swappable(current_act(), FALSE);

	/*
	 * Set thread priority and scheduling policy.
	 */
	pset = thread->processor_set;
	base = (policy_base_t) &fifo_base; 
	limit = (policy_limit_t) &fifo_limit;
	if (iodone_thread_fixpri == -1) {
		fifo_base.base_priority = BASEPRI_KERNEL+2; 
		fifo_limit.max_priority = BASEPRI_KERNEL+2;
	} else {
		fifo_base.base_priority = iodone_thread_fixpri;
		fifo_limit.max_priority = iodone_thread_fixpri;
	}
	ret = thread_set_policy(thread->top_act, pset, POLICY_FIFO, 
				base, POLICY_FIFO_BASE_COUNT,
				limit, POLICY_FIFO_LIMIT_COUNT);
	if (ret != KERN_SUCCESS)
		printf("WARNING: io_done_thread is being TIMESHARED!\n");
    }

    for (;;) {
	    spl_t		s;
	    register io_req_t	ior;
	    io_done_queue_t	queue = &io_done_thread_queue;

#if	LINUX_DEV && defined (CONFIG_INET)
	    extern void free_skbuffs();

	    free_skbuffs ();
#endif
	    s = splio();
	    io_done_queue_lock(queue);
	    while ((ior = (io_req_t)dequeue_head(&queue->io_done_list))
							!= (io_req_t)0){
		io_done_queue_unlock(queue);
		splx(s);
		io_completed(ior, TRUE);
		if (ior->io_op & IO_FREE)
		    io_req_free(ior);
		s = splio();
		io_done_queue_lock(queue);
	    }

	    assert_wait((event_t) queue, FALSE);
	    io_done_queue_unlock(queue);
	    splx(s);
	    counter(c_io_done_thread_block++);
	    thread_block((void (*)(void)) 0);
    }
    /*NOTREACHED*/
}

#define	DEVICE_IO_MAP_SIZE	(2 * 1024 * 1024)

zone_t	io_req_zone = (zone_t)0;

void
ds_init(void)
{
	kern_return_t	retval;
	vm_offset_t	device_io_min, device_io_max;

	queue_init(&io_done_thread_queue.io_done_list);
	simple_lock_init(&io_done_thread_queue.lock, ETAP_IO_DONE_LIST);

	retval = kmem_suballoc(kernel_map,
			       &device_io_min,
			       DEVICE_IO_MAP_SIZE,
			       FALSE,
			       TRUE,
			       &device_io_map);
	if (retval != KERN_SUCCESS)
		panic("ds_init: kmem_suballoc failed");
	device_io_max = device_io_min + round_page(DEVICE_IO_MAP_SIZE);

	/*
	 *	If the kernel receives many device_write requests, the
	 *	device_io_map might run out of space.  To prevent
	 *	device_write_get from failing in this case, we enable
	 *	wait_for_space on the map.  This causes kmem_io_map_copyout
	 *	to block until there is sufficient space.
	 *	(XXX Large writes may be starved by small writes.)
	 *
	 *	There is a potential deadlock problem with this solution,
	 *	if a device_write from the default pager has to wait
	 *	for the completion of a device_write which needs to wait
	 *	for memory allocation.	Hence, once device_write_get
	 *	allocates space in device_io_map, no blocking memory
	 *	allocations should happen until device_write_dealloc
	 *	frees the space.  (XXX A large write might starve
	 *	a small write from the default pager.)
	 */
	device_io_map->wait_for_space = TRUE;

	io_inband_zone = zinit(sizeof(io_buf_ptr_inband_t),
			    1000 * sizeof(io_buf_ptr_inband_t),
			    10 * sizeof(io_buf_ptr_inband_t),
			    "io inband read buffers");

	io_req_zone = zinit(sizeof(struct io_req),
			    1000 * sizeof(struct io_req),
			    10	 * sizeof(struct io_req),
			    "io request structures");
}

void
iowait(
	io_req_t	ior)
{
    spl_t s;

    s = splio();
    ior_lock(ior);
    while ((ior->io_op&IO_DONE)==0) {
	assert_wait((event_t)ior, FALSE);
	ior_unlock(ior);
	thread_block((void (*)(void)) 0);
	ior_lock(ior);
    }
    ior_unlock(ior);
    splx(s);
}

/*
 * Allocate an I/O completion queue kernel object.
 */
io_return_t
ds_io_done_queue_create(
	host_t		host,
	io_done_queue_t	*io_done_queue)
{
	io_done_queue_t		queue;

	if (host == HOST_NULL)
		return(KERN_INVALID_ARGUMENT);

	/*
	 * Allocate the I/O completion queue header
	 */
	queue = (io_done_queue_t) kalloc(sizeof(struct io_done_queue));
	if (queue == IO_DONE_QUEUE_NULL)
		return(KERN_RESOURCE_SHORTAGE);

	/*
	 * Allocate the associated kernel port and register the object
	 */
	queue->port = ipc_port_alloc_kernel();
	if (queue->port == IP_NULL) {
		kfree((vm_offset_t) queue, sizeof(struct io_done_queue));
		return(KERN_RESOURCE_SHORTAGE);
	}
	ipc_kobject_set(queue->port, (ipc_kobject_t) queue, IKOT_IO_DONE_QUEUE);

	/*
	 * Initialize the remaining header elements
	 */
	simple_lock_init(&queue->ref_lock, ETAP_IO_DONE_REF);
	simple_lock_init(&queue->lock, ETAP_IO_DONE_Q);
	queue_init(&queue->io_done_list);
	queue->ref_count = 1;
	queue->io_in_progress = 0;
	queue->waiters = 0;
	queue->handoffs = 0;

	*io_done_queue = queue;

	return(KERN_SUCCESS);
}

/*
 * Add a completed ior to an I/O completion queue.
 */
void
io_done_queue_add(
	io_done_queue_t	queue,
	io_req_t	ior)
{
	spl_t		s;

	s = splio();
	io_done_queue_lock(queue);
	ior->io_op |= IO_DONE;
	enqueue_tail(&queue->io_done_list, (queue_entry_t) ior);
	if (queue != &io_done_thread_queue) {
	    assert(queue->io_in_progress > 0);
	    queue->io_in_progress--;
	    queue->waiters--;	/* Use up an io_done_queue_wait(). */
	}
	io_done_queue_unlock(queue);
	thread_wakeup_one((event_t) queue);
	splx(s);
}

/*
 * Deallocate an I/O completion queue
 */
io_return_t
ds_io_done_queue_terminate(
	io_done_queue_t queue)
{
	spl_t		s;
	io_req_t	ior;

	/*
	 * Remove port-to-queue mapping, preventing any additional
	 * asynchronous requests on this queue.
	 */
	ipc_kobject_set(queue->port, IKO_NULL, IKOT_NONE);
	ipc_port_dealloc_kernel(queue->port);
	io_done_queue_deallocate(queue);

	/*
	 * Wakeup any waiters
	 */
	s = splio();
	thread_wakeup((event_t) queue);
	splx(s);

	/*
	 * Clean up any completed requests and wait for any pending ones.
	 * At this point queue->ref_count cannot be incremented since
	 * the port-to-queue mapping is gone.
	 */
	for (;;) {
		s = splio();
		io_done_queue_lock(queue);
		while ((ior = (io_req_t) dequeue_head(&queue->io_done_list)) !=
		       (io_req_t) 0) {
			io_done_queue_unlock(queue);	
			splx(s);

			assert((ior->io_op & IO_QUEUE) != 0);

			if (ior->io_done == 0 || (*ior->io_done)(ior)) {

				/*	
				 * Free the ior and drop the reference on the 
				 * queue held by the pending ior
				 */
				io_req_free(ior);
				io_done_queue_deallocate(queue);
			}

			s = splio();
			io_done_queue_lock(queue);
		}

		/*
		 * Wait for pending ior's
		 */
		if (queue->io_in_progress > 0) {
#if			DEBUG
			log_thread_action("ds_io_done_queue_terminate",
					(long)queue, 0, 0);
#endif
			assert_wait((event_t) queue, TRUE);
			io_done_queue_unlock(queue);	
			splx(s);
			thread_block((void (*)(void)) 0);
		}
		else {
			assert(queue->ref_count == 1);
			break;
		}
	}

	/*
	 * Drop the refererence held by the caller
	 */
	io_done_queue_deallocate(queue);	

	return(KERN_SUCCESS);
}

/*
 * Remove a reference to an I/O completion queue, and deallocate the
 * structure if no references are left.
 */
void
io_done_queue_deallocate(
	io_done_queue_t		queue)
{

	if (queue == IO_DONE_QUEUE_NULL)
		return;

	simple_lock(&queue->ref_lock);
	if (--queue->ref_count > 0) {
		simple_unlock(&queue->ref_lock);
		return;
	}

	/*
	 * Free the I/O completion queue
	 */
	kfree((vm_offset_t) queue, sizeof(struct io_done_queue));
}

/*
 * Wait on an I/O completion queue.  Completions are processed serially.
 */
io_return_t
ds_io_done_queue_wait(
	io_done_queue_t		queue,
	io_done_result_t	*result,
	io_done_result_t	*ures)
{
	spl_t			s;
	io_req_t		ior;

	s = splio();
	io_done_queue_lock(queue);

	for (;;) {

		/*
		 * Check that the queue still exists.  The object may
		 * have been terminated.
		 */
		if (queue->port == IP_NULL) {
			io_done_queue_unlock(queue);	
			splx(s);
			return(KERN_TERMINATED);
		}

		queue->waiters++;

		/*
		 * Check queue for completed I/O requests.  If queue->handoffs
		 * > 0, the io_done_thread has been given an ior on our behalf,
		 * which we wait for in order to guarantee seriality.
		 */
		while (queue->handoffs > 0
		       || (ior = (io_req_t) dequeue_head(&queue->io_done_list))
			  == (io_req_t) 0) {
#if			DEBUG
			log_thread_action("ds_io_done_queue_wait",
					(long)queue, 0, 0);
#endif
			assert_wait((event_t) queue, TRUE);
			io_done_queue_unlock(queue);	
			splx(s);
			thread_block((void (*)(void)) SAFE_MISCELLANEOUS);

			/*
			 * Check that the queue still exists.  The wakeup
			 * may be because the object has been terminated.
			 */
			if (queue->port == IP_NULL)
				return(KERN_TERMINATED);

			if (current_act()->handlers) {
				act_execute_returnhandlers();
			}

			s = splio();
			io_done_queue_lock(queue);
		}

		io_done_queue_unlock(queue);	
		splx(s);

		assert(ior && (ior->io_op & IO_QUEUE) != 0);

		/*
		 * We have a completion.  Invoke the completion
		 * handler.  If ior->io_done == 0, the handler has already
		 * been executed in another context.  If (*ior->io_done)()
		 * returns FALSE, the request has been queued again and
		 * will complete later.
		 */
		if (ior->io_done == 0 || (*ior->io_done)(ior)) {
			kern_return_t ret = KERN_SUCCESS;

			/*
			 * Marshall the results for the user
			 */	
			result->qd_reqid = (mach_port_t)ior->io_reply_port_type;
			result->qd_code = ior->io_error;

			if (ior->io_op & IO_READ) {		/* read */
				int	err;

				result->qd_type = IO_DONE_READ;
				if (ior->io_op & IO_INBAND) {
					result->qd_count = ior->io_count - 
							ior->io_residual;
					ret = copyout(ior->io_data, 
						   (char *)ures->qd_inline,
						   result->qd_count);
					if (ior->io_alloc_size> (io_buf_len_t)0)
					    zfree(io_inband_zone, 
						  (vm_offset_t)ior->io_data);
					if (ret)
					    ret = KERN_INVALID_ADDRESS;
					result->qd_data = 
					    (io_buf_ptr_t )ures->qd_inline;
				}
				else if (ior->io_op & IO_OVERWRITE) {
					result->qd_count = ior->io_total;
					result->qd_type = IO_DONE_OVERWRITE;
				}
				else {
					result->qd_count = ior->io_total;
					result->qd_type = IO_DONE_READ;
					result->qd_data = ior->io_data;
				}
			}
			else {					/* write */
				result->qd_type = IO_DONE_WRITE;
				result->qd_count = ior->io_total - 
							ior->io_residual;
			}

			/*	
			 * Free the ior and drop the reference on the 
			 * queue held by the pending ior
			 */
			io_req_free(ior);
			io_done_queue_deallocate(queue);

			return(ret);
		}

		s = splio();
		io_done_queue_lock(queue);

		/* We only reach here if the iodone function returned FALSE,
		 * indicating that this I/O is not finished and that the ior
		 * will be requeued to this io_done queue later.  Thus we
		 * need to increment the count of I/O's in progress, which
		 * was incorrectly decremented when io_done_queue_add put
		 * this ior on our queue.  There should be a better way to
		 * do this.
		 */
		queue->io_in_progress++;
	}
}


#if	MACH_KDB

unsigned int
db_count_io_done_list(io_done_queue_t	queue)
{
	io_req_t	ior;
	unsigned int	count;

	count = 0;
	for (ior = (io_req_t) queue_first(&queue->io_done_list);
	     !(queue_end(&queue->io_done_list, (queue_head_t *) ior));
	     ior = ior->io_next)
		++count;

	return count;
}

unsigned int
db_count_io_done_list_locked(io_done_queue_t	queue)
{
	unsigned int	count;
	int		s;

	s = splio();
	io_done_queue_lock(queue);

	count = db_count_io_done_list(queue);

	io_done_queue_unlock(queue);
	splx(s);

	return count;
}


#define	pct(a,b)	((100 * (a)) / (b))

void
db_device(void)
{
	iprintf("Device Subsystem:\n");
	indent += 2;
	iprintf("Pending I/O Dones:\t%d\n",
		db_count_io_done_list_locked(&io_done_thread_queue));
#if	MACH_COUNTERS && DEVICE_LIMITS
	iprintf("Device tries %d blocks %d (%d%%)\n",
		c_dev_io_tries, c_dev_io_blocks,
		pct(c_dev_io_blocks, c_dev_io_tries));
#endif	/* MACH_COUNTERS && DEVICE_LIMITS */
	indent -= 2;
}

#define printf	db_printf

/* Forward */

void db_io_sglist_hdr_print(
	struct io_sglist_hdr	*iogh);

void db_io_sg_entry_print(
	struct io_sg_entry	*ioge);

void db_io_sglist_print(
	io_sglist_t	iog);

void db_io_req_print(
	io_req_t	ior);

/*
 * Print a device IO scatter/gather header.
 */
void
db_io_sglist_hdr_print(
	struct io_sglist_hdr	*iogh)
{
	iprintf("length = %d, nentries = %d", iogh->length, iogh->nentries); 
}

/*
 * Print a device IO scatter/gather entry.
 */
void
db_io_sg_entry_print(
	struct io_sg_entry	*ioge)
{
	iprintf("phy_addr = 0x%x, length = %d",
		ioge->iosge_phys, ioge->iosge_length); 
}

/*
 * Print a device IO scatter/gather list.
 */
void db_io_sglist_print(
	io_sglist_t	iog)
{
	unsigned i;

	printf("io_sglist 0x%x\n", iog);
	indent += 2;
	db_io_sglist_hdr_print(&iog->iosg_hdr);
	indent += 2;
	for (i = 0; i < iog->iosg_hdr.nentries; i++) {
		if (i % 3) {
			printf("\n");
			iprintf("%d = (", i);
		} else
			printf(" %d = (", i);
		db_io_sg_entry_print(&iog->iosg_list[i]);
		printf(")");
	}
	indent -= 4;
}

/*
 * Print a device IO request.
 */
void
db_io_req_print(
	io_req_t	ior)
{
	char		*s;

	printf("ioreq 0x%x\n", ior);
	indent += 2;
	
	iprintf("next = 0x%x, prev = 0x%x, device = 0x%x, dev_ptr= 0x%x\n",
	       ior->io_next, ior->io_prev, ior->io_device, ior->io_dev_ptr);

	iprintf("unit = %d, recnum = %d, mode = 0x%x",
	       ior->io_unit, ior->io_recnum, ior->io_mode);
	if (ior->io_mode & D_READ) {
		printf(" <READ");
		s = ",";
	} else
		s = " <";
	if (ior->io_mode & D_WRITE) {
		printf("%sWRITE", s);
		s = ",";
	}
	if (ior->io_mode & D_NODELAY) {
		printf("%sNODELAY", s);
		s = ",";
	}
	if (ior->io_mode & D_NOWAIT)
		printf("%sNOWAIT>", s);
	else if (*s == ',')
		printf(">");
	printf("\n");

	iprintf("op = 0x%x <", ior->io_op);
	switch (ior->io_op & (IO_READ|IO_OPEN)) {
	case IO_WRITE:
		printf("WRITE");
		break;
	case IO_READ:
		printf("READ");
		break;
	case IO_OPEN:
		printf("OPEN");
		break;
	}
	if (ior->io_op & IO_DONE)
		printf(",DONE");
	if (ior->io_op & IO_ERROR)
		printf(",ERROR");
	if (ior->io_op & IO_BUSY)
		printf(",BUSY");
	if (ior->io_op & IO_WANTED)
		printf(",WANTED");
	if (ior->io_op & IO_BAD)
		printf(",BAD");
	if (ior->io_op & IO_CALL)
		printf(",CALL");
	if (ior->io_op & IO_INBAND)
		printf(",INBAND");
	if (ior->io_op & IO_INTERNAL)
		printf(",INTERNAL");
	if (ior->io_op & IO_LOANED)
		printf(",LOANED");
	if (ior->io_op & IO_SGLIST)
		printf(",SGLIST");
	if (ior->io_op & IO_SYNC)
		printf(",SYNC");
	if (ior->io_op & IO_OVERWRITE)
		printf(",OVERWRITE");
	if (ior->io_op & IO_WCLONED)
		printf(",WCLONED");
	printf(">\n");

	iprintf("data = 0x%x, sglist = 0x%x, uaddr = 0x%x, map = 0x%x\n",
	       ior->io_data, ior->io_sgp, ior->io_uaddr, ior->io_map);

	iprintf("count = %d, alloc_size = %d, residual = %d, error = 0x%x\n",
	       ior->io_count, ior->io_alloc_size,
	       ior->io_residual, ior->io_error);

	iprintf("io_done = 0x%x, reply_port = 0x%x, reply_port_type = 0x%x\n",
	       ior->io_done, ior->io_reply_port, ior->io_reply_port_type);

	iprintf("link = 0x%x, rlink = 0x%x, io_copy = 0x%x, io_total = %d\n",
	       ior->io_link, ior->io_rlink, ior->io_copy, ior->io_total);
	indent -= 2;
}
#endif	/* MACH_KDB */

struct device_emulation_ops mach_device_emulation_ops =
{
  mach_device_reference,
  mach_device_deallocate,
  mach_convert_device_to_port,
  mach_device_open,
  mach_device_close,
  mach_device_write,
  mach_device_write_inband,
  mach_device_write_common,
  mach_device_read,
  mach_device_read_inband,
  mach_device_read_common,
  mach_device_set_status,
  mach_device_get_status,
  mach_device_set_filter,
  mach_device_map,
  mach_no_senders
};

