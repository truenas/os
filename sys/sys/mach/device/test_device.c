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

#include <test_device.h>

#include <types.h>
#include <device/buf.h>
#include <device/conf.h>
#include <device/errno.h>
#include <device/misc_protos.h>
#include <device/ds_routines.h>
#include <kern/misc_protos.h>
#include <device/test_device_entries.h>
#include <device/test_device_status.h>
#include <vm/vm_kern.h>
#include <kern/task.h>
#include <kern/thread.h>
#include <kern/thread_act.h>
#include <device/net_io.h>
#include <device/if_hdr.h>
#include <mach/mach_host_server.h>

vm_address_t	testdev_mem = (vm_address_t)0;
boolean_t	testdev_debug = FALSE;
struct ifnet	testdev_if;

void
testdev_generate_packets(void)
{
	thread_act_t			self, caller;
	test_device_status_t		*status;
	ipc_kmsg_t			kmsg;
	int				i;
	policy_fifo_base_data_t		fifo_base;
	policy_fifo_limit_data_t	fifo_limit;
	kern_return_t			kr;

	self = current_act();
	status = (test_device_status_t *) self->thread->ith_other;

	/*
	 * Set caller's priority higher than the net thread,
	 * or we might lose some packets.
	 */
	caller = (thread_act_t) status->reply_thread;
	fifo_base.base_priority = BASEPRI_KERNEL+1;
	fifo_limit.max_priority = BASEPRI_KERNEL+1;
	kr = thread_set_policy(caller, caller->thread->processor_set, POLICY_FIFO,
			       (policy_base_t) &fifo_base,
			       POLICY_FIFO_BASE_COUNT,
			       (policy_limit_t) &fifo_limit,
			       POLICY_FIFO_LIMIT_COUNT);
	if (kr != KERN_SUCCESS) {
		printf("testdev_generate_packets: couldn't raise caller's priority\n");
	}

	/*
	 * Set our priority lower than the net thread,
	 * or we might lose some packets.
	 */
	fifo_base.base_priority = BASEPRI_KERNEL+3;
	fifo_limit.max_priority = BASEPRI_KERNEL+3;
	kr = thread_set_policy(self, self->thread->processor_set, POLICY_FIFO,
			       (policy_base_t) &fifo_base,
			       POLICY_FIFO_BASE_COUNT,
			       (policy_limit_t) &fifo_limit,
			       POLICY_FIFO_LIMIT_COUNT);
	if (kr != KERN_SUCCESS) {
		printf("testdev_generate_packets: couldn't lower our priority\n");
	}

	if (testdev_debug) {
		printf("testdev_generate_packets[%x]: port=0x%x type=%d count=%d size=%d\n",
		       self, status->reply_port, status->reply_type,
		       status->reply_count, status->reply_size);
	}

	for (i = 0; i < status->reply_count; i++) {
		kmsg = net_kmsg_get();
		if (kmsg == IKM_NULL) {
			if (testdev_debug || status->reply_synchronous) {
				printf("testdev_generate_packets: net_kmsg_get() failed\n");
			}
			if (status->reply_synchronous) {
				return;
			}
			thread_block((void (*)(void)) 0);
			i--;
			continue;
		}
		net_packet(&testdev_if, kmsg,
			status->reply_size, TRUE, (io_req_t)0);
		if (! status->reply_synchronous) {
			thread_block((void (*)(void)) 0);
		}
	}

	if (testdev_debug) {
		printf("testdev_generate_packets[%x]: done i=%d count=%d\n", self, i, status->reply_count);
	}

	return;
}

void
testdev_generate_replies(void)
{
	thread_act_t		self;
	test_device_status_t	*status;
	int			i;
	io_req_t		ior;
	mach_device_t		device;
	kern_return_t		kr;

	self = current_act();
	status = (test_device_status_t *) self->thread->ith_other;

	if (status->reply_type == DEVICE_TEST_FILTERED_PACKET) {
		testdev_generate_packets();
		goto done;
	}

	if (testdev_debug) {
		printf("testdev_generate_replies[%x]: port=0x%x type=%d count=%d size=%d\n",
		       self, status->reply_port, status->reply_type,
		       status->reply_count, status->reply_size);
	}

	device = device_lookup("test_device");
	mutex_lock(&device->ref_lock);
	device->ref_count += status->reply_count - 1;
	mutex_unlock(&device->ref_lock);

	for (i = 0; i < status->reply_count; i++) {
		io_req_alloc(ior);
		ior->io_device = device;
		ior->io_unit = device->dev_number;
		ior->io_mode = D_NOWAIT;
		ior->io_recnum = 0;
		ior->io_uaddr = status->reply_uaddr;
		ior->io_map = ((thread_act_t)status->reply_thread)->task->map;
		ior->io_count = status->reply_size;
		ior->io_alloc_size = 0;
		ior->io_residual = 0;
		ior->io_error = 0;
		ior->io_reply_port = (struct ipc_port *) status->reply_port;
		ior->io_reply_port_type = MACH_MSG_TYPE_MAKE_SEND_ONCE;
		ior->io_copy = VM_MAP_COPY_NULL;
		ior->io_total = ior->io_count;

		switch(status->reply_type) {
		    case DEVICE_TEST_READ_REPLY:
			ior->io_op = IO_READ | IO_CALL;
			ior->io_done = ds_read_done;
			kr = device_read_alloc(ior, ior->io_count);
			if (kr != KERN_SUCCESS) {
				if (testdev_debug ||
				    status->reply_synchronous) {
					printf("testdev_generate_reply: device_read_alloc returned 0x%x\n", kr);
				}
				if (status->reply_synchronous) {
					goto done;
				}
				thread_block((void (*)(void)) 0);
				i--;
				continue;
			}
			break;
		    case DEVICE_TEST_READ_INBAND_REPLY:
			ior->io_op = IO_READ | IO_CALL | IO_INBAND;
			ior->io_done = ds_read_done;
			kr = device_read_alloc(ior, ior->io_count);
			if (kr != KERN_SUCCESS) {
				if (testdev_debug ||
				    status->reply_synchronous) {
					printf("testdev_generate_reply: device_read_alloc returned 0x%x\n", kr);
				}
				if (status->reply_synchronous) {
					goto done;
				}
				thread_block((void (*)(void)) 0);
				i--;
				continue;
			}
			break;
		    case DEVICE_TEST_READ_OVERWRITE_REPLY:
			ior->io_op = IO_READ | IO_CALL | IO_OVERWRITE;
			ior->io_done = ds_read_done;
			kr = device_read_alloc(ior, ior->io_count);
			if (kr != KERN_SUCCESS) {
				if (testdev_debug ||
				    status->reply_synchronous) {
					printf("testdev_generate_reply: device_read_alloc returned 0x%x\n", kr);
				}
				if (status->reply_synchronous) {
					goto done;
				}
				thread_block((void (*)(void)) 0);
				i--;
				continue;
			}
			break;
		    case DEVICE_TEST_WRITE_REPLY:
			ior->io_op = IO_WRITE | IO_CALL;
			ior->io_data = (io_buf_ptr_t) status->reply_uaddr;
			ior->io_done = ds_write_done;
			break;
		    case DEVICE_TEST_WRITE_INBAND_REPLY:
			ior->io_op = IO_WRITE | IO_CALL | IO_INBAND;
			ior->io_data = (io_buf_ptr_t) status->reply_uaddr;
			ior->io_done = ds_write_done;
			break;
		}
		if (testdev_debug)
			printf("*");
		iodone(ior);
		if (! status->reply_synchronous) {
			thread_block((void (*)(void)) 0);
		}
	}

	if (testdev_debug) {
		printf("testdev_generate_replies[%x]: done i=%d count=%d\n", self, i, status->reply_count);
	}

    done:
	if (status->reply_synchronous) {
		/* wakeup the caller thread */
		status->reply_synchronous = FALSE;
	} else {
		kfree((vm_offset_t) status, sizeof (*status));
	}

	thread_terminate_self();
}

io_return_t
testdev_open(
	dev_t		dev,
	dev_mode_t	flag,
	io_req_t	ior)
{
	kern_return_t	kr;

	if (testdev_mem == (vm_address_t)0) {
		kr = kmem_alloc(kernel_map, &testdev_mem, PAGE_SIZE);
		if (kr != KERN_SUCCESS) {
			testdev_mem = (vm_address_t)0;
			return(kr);
		}
	}

	testdev_if.if_flags = IFF_UP | IFF_RUNNING;
	if_init_queues(&testdev_if);

	return D_SUCCESS;
}

void
testdev_close(
	dev_t		dev)
{
	
	return;
}

io_return_t
testdev_read(
	dev_t		dev,
	io_req_t 	ior)
{
	kern_return_t	rc;
	vm_offset_t	offset;

	rc = device_read_alloc(ior, (vm_size_t) ior->io_count);
	if (rc != KERN_SUCCESS)
		return rc;

	/* workaround for kernel bug XXX */
	for (offset = 0; offset < ior->io_count; offset += PAGE_SIZE) {
		*(((char *) ior->io_data) + offset) = 'x';
	}
	ior->io_residual = 0;
	iodone(ior);

	if (ior->io_op & IO_SYNC) {
		iowait(ior);
		return D_SUCCESS;
	}

	return D_IO_QUEUED;
}

io_return_t
testdev_write(
	dev_t		dev,
	io_req_t	ior)
{
	kern_return_t	rc;
	boolean_t	wait = FALSE;

	rc = device_write_get(ior, &wait);
	if (rc != KERN_SUCCESS)
		return rc;

	ior->io_residual = 0;
	iodone(ior);

	if (wait || (ior->io_op & IO_SYNC)) {
		iowait(ior);
		return D_SUCCESS;
	}

	return D_IO_QUEUED;
}

io_return_t
testdev_getstat(
	dev_t		dev,
	dev_flavor_t	flavor,
	dev_status_t	data,	/* pointer to OUT array */
	mach_msg_type_number_t	*count)	/* OUT */
{
	test_device_status_t	*status;

	status = (test_device_status_t *) data;

	switch (flavor) {
		/* Mandatory flavors */
	    case DEV_GET_SIZE:
		break;

		/* Extra flavors */
	    case TEST_DEVICE_STATUS:
		status->reply_count = -1;
		break;

	    default:
		return D_INVALID_OPERATION;
	}
	return D_SUCCESS;
}

io_return_t
testdev_setstat(
	dev_t		dev,
	dev_flavor_t	flavor,
	dev_status_t	data,
	mach_msg_type_number_t	count)
{
	int			i;
	test_device_status_t	*status;
	thread_t		th;
	kern_return_t		kr;
	boolean_t		synchronous;

	switch (flavor) {
	    case TEST_DEVICE_STATUS:
		break;
	    case TEST_DEVICE_GENERATE_REPLIES:
		status = (test_device_status_t *) kalloc(sizeof (*status));
		if (status == NULL) {
			printf("testdev_setstat: couldn't allocate status\n");
			return KERN_FAILURE;
		}
		*status = * (test_device_status_t *) data;
		synchronous = status->reply_synchronous;

		/* get the thread dependant data here */
		if (status->reply_port != MACH_PORT_NULL) {
			kr = ipc_object_copyin(current_space(),
					       status->reply_port,
					       MACH_MSG_TYPE_MAKE_SEND,
					       (ipc_object_t *)
					       &(status->reply_port));
			if (kr != KERN_SUCCESS) {
				printf("testdev_setstat: couldn't copyin reply_port\n");
				break;
			}
		}
		status->reply_thread = (mach_port_t) current_act();

		/* launch a new thread to generate the replies */
		th = kernel_thread(kernel_task,
				   testdev_generate_replies,
				   (char *) status);	
		if (th == THREAD_NULL) {
			printf("testdev_setstat: couldn't launch testdev_generate_replies thread\n");
		}

		if (synchronous) {
			/* wait until the replies have been generated */
			while (status->reply_synchronous) {
				thread_block((void (*)(void)) 0);
			}
			kfree((vm_offset_t) status, sizeof (*status));
		}

		break;
	    default:
		return D_INVALID_OPERATION;
	}
	return D_SUCCESS;
}

vm_offset_t
testdev_mmap(
	dev_t		dev,
	vm_offset_t	off,
	vm_prot_t	prot)
{
	if (testdev_mem == (vm_address_t)0)
		return -1;
	else
		return kvtophys(testdev_mem);
}

io_return_t
testdev_async_in(
	dev_t		dev,
	ipc_port_t	rcv_port,
	int		pri,
	filter_t	*filter,
	mach_msg_type_number_t	fcount,
	mach_device_t	device)
{
	io_return_t	rc;

	rc = net_set_filter(&testdev_if, rcv_port, pri, filter, fcount, device);
	return rc;
}

void
testdev_reset(
	dev_t		dev)
{
}

boolean_t
testdev_port_death(
	dev_t		dev,
	ipc_port_t	port)
{
	return FALSE;
}

io_return_t
testdev_dev_info(
	dev_t		dev,
	dev_flavor_t	flavor,
	char		*info)
{
	register int	result;

	result = D_SUCCESS;

	switch (flavor) {
	    default:
		result = D_INVALID_OPERATION;
	}

	return result;
}
