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

#include <mach/boolean.h>
#include <kern/assert.h>
#include <kern/lock.h>
#include <kern/spl.h>
#include <device/device_types.h>
#include <device/ds_routines.h>
#include <device/io_req.h>
#include <device/data_device.h>
#include <device/conf.h>

/*
 * Generic opaque data loading inside the kernel via the bootstrap task
 */
static io_req_t		datadev_ior;	/* current data request read ior */
static queue_head_t	datadev_curr;	/* current data requests */
static queue_head_t	datadev_wait;	/* waiting data requests */
decl_mutex_data(static ,datadev_lock)	/* lock for data request lists */

/*
 * Initialization of the datadev module
 */
void
datadev_init()
{
    mutex_init(&datadev_lock, ETAP_IO_DATADEV);
    queue_init(&datadev_curr);
    queue_init(&datadev_wait);
}

/*
 * Manage next request event
 */
void
datadev_request(
    datadev_t ddp)
{
    kern_return_t rc;
    io_req_t ior;
    spl_t s;

    s = splsched();
    mutex_lock(&datadev_lock);
    if (ddp != (datadev_t)0) {
	/*
	 * Queue current request
	 */
	queue_enter(&datadev_wait, ddp, datadev_t, dd_chain);
    }

    /*
     * Try to start next request
     */
    if (queue_empty(&datadev_wait) || datadev_ior == (io_req_t)0) {
	/*
	 * No request or no pending read
	 */
	mutex_unlock(&datadev_lock);
	splx(s);
	return;
    }

    /*
     * Extract first waiting request
     */
    ddp = (datadev_t)queue_first(&datadev_wait);

    /*
     * Extract pending I/O request
     */
    ior = datadev_ior;
    datadev_ior = (io_req_t)0;

    /*
     * Allocate read memory
     */
    if (ior->io_count < ddp->dd_size) {
	/*
	 * Return size error for this request
	 */
	mutex_unlock(&datadev_lock);
	splx(s);
	ior->io_error = D_INVALID_SIZE;

    } else {
	/*
	 * Move waiting request from the waiting queue to the active one.
	 */
	queue_remove(&datadev_wait, ddp, datadev_t, dd_chain);
	queue_enter(&datadev_curr, ddp, datadev_t, dd_chain);
	mutex_unlock(&datadev_lock);
	splx(s);

	/*
	 * Activate the request
	 */
	bcopy(ddp->dd_name, ior->io_data, ddp->dd_size);
	ddp->dd_dev = ior->io_unit;
	ior->io_residual = ior->io_count - ddp->dd_size;
	ior->io_error = D_SUCCESS;
    }

    io_completed(ior, FALSE);
}

/*
 * Devops open routine
 */
io_return_t
datadev_open(
    dev_t dev,
    dev_mode_t mode,
    io_req_t req)
{
    /*
     * Always force exclusive open
     */
    req->io_device->flag |= D_EXCL_OPEN;
    return (D_SUCCESS);
}

/*
 * Devops close routine
 */
void
datadev_close(
    dev_t dev)
{
    void (*close)(void *);
    void *arg;
    spl_t s;
    datadev_t ddp;

    assert(datadev_ior == (io_req_t)0);

    s = splsched();
    mutex_lock(&datadev_lock);
    queue_iterate(&datadev_curr, ddp, datadev_t, dd_chain)
	if (ddp->dd_dev == dev)
	    break;
    assert(ddp != (datadev_t)0);

    close = ddp->dd_close;
    arg = ddp->dd_arg;
    queue_remove(&datadev_curr, ddp, datadev_t, dd_chain);
    mutex_unlock(&datadev_lock);
    splx(s);

    (*close)(arg);
}

/*
 * Devops read routine
 */
io_return_t
datadev_read(
    dev_t dev,
    io_req_t ior)
{
    kern_return_t rc;
    spl_t s;
    datadev_t ddp;

    rc = device_read_alloc(ior, (vm_size_t)ior->io_count);
    if (rc != KERN_SUCCESS)
	return (rc);

    s = splsched();
    mutex_lock(&datadev_lock);
    queue_iterate(&datadev_curr, ddp, datadev_t, dd_chain)
	if (ddp->dd_dev == dev)
	    break;
    if (!queue_end(&datadev_curr, (queue_entry_t)ddp) ||
	datadev_ior != (io_req_t)0) {
	mutex_unlock(&datadev_lock);
	splx(s);
	return (D_INVALID_OPERATION);
    }
    datadev_ior = ior;
    mutex_unlock(&datadev_lock);
    splx(s);

    datadev_request((datadev_t)0);

    if ((ior->io_op & IO_SYNC) == 0)
	return (D_IO_QUEUED);

    iowait(ior);
    return (D_SUCCESS);
}

/*
 * Devops write routine
 */
io_return_t
datadev_write(
    dev_t dev,
    io_req_t ior)
{
    void (*write)(void *, io_req_t);
    kern_return_t rc;
    void *arg;
    spl_t s;
    boolean_t wait;
    datadev_t ddp;

    rc = device_write_get(ior, &wait);
    if (rc != KERN_SUCCESS)
	return (rc);

    s = splsched();
    mutex_lock(&datadev_lock);
    queue_iterate(&datadev_curr, ddp, datadev_t, dd_chain)
	if (ddp->dd_dev == dev)
	    break;
    assert(ddp != (datadev_t)0);

    write = ddp->dd_write;
    arg = ddp->dd_arg;
    mutex_unlock(&datadev_lock);
    splx(s);

    (*write)(arg, ior);

    if (wait || (ior->io_op & IO_SYNC)) {
	iowait(ior);
	return (D_SUCCESS);
    }

    return (D_IO_QUEUED);
}

/*
 * Devops dev_info routine
 */
io_return_t
datadev_dinfo(
    dev_t dev,
    dev_flavor_t flavor,
    char *info)
{
    switch (flavor) {
    case D_INFO_BLOCK_SIZE:
	*(int *)info = 1;		/* io_recnum is byte-oriented */
	break;

    default:
        return (D_INVALID_OPERATION);
    }
    return (D_SUCCESS);
}
