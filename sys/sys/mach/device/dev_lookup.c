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
/* CMU_HIST */
/*
 * Revision 2.9	 91/06/25  10:26:31  rpd
 *	Changed the convert_foo_to_bar functions
 *	to use ipc_port_t instead of mach_port_t.
 *	[91/05/29	     rpd]
 * 
 * Revision 2.8	 91/05/18  14:29:28  rpd
 *	Fixed device_deallocate to always unlock the reference count.
 *	[91/04/03	     rpd]
 * 
 * Revision 2.7	 91/05/14  15:40:44  mrt
 *	Correcting copyright
 * 
 * Revision 2.6	 91/02/05  17:08:25  mrt
 *	Changed to new Mach copyright
 *	[91/01/31  17:27:17  mrt]
 * 
 * Revision 2.5	 90/09/09  14:31:13  rpd
 *	Use decl_simple_lock_data.
 *	[90/08/30	     rpd]
 * 
 * Revision 2.4	 90/08/27  21:55:02  dbg
 *	Remove obsolete type names and macros.
 *	[90/07/16	     dbg]
 * 
 * Revision 2.3	 90/06/02  14:47:14  rpd
 *	Converted to new IPC.
 *	Fixed device leak in convert_device_to_port.
 *	[90/03/26  21:45:09  rpd]
 * 
 * Revision 2.2	 89/09/08  11:23:12  dbg
 *	Modified to run in kernel context.  Moved name search routines
 *	to dev_name.c.	Reorganized remaining routines.	 Added
 *	correct locking.
 *	[89/08/01	     dbg]
 * 
 *  5-Jun-89  Randall Dean (rwd) at Carnegie-Mellon University
 *	Added dev_change_indirect for use by sun autoconf (ms,kbd,fb)
 *
 * 12-May-89  David Golub (dbg) at Carnegie-Mellon University
 *	Added search through indirection table for certain devices.
 *
 * 12-Apr-89  David Golub (dbg) at Carnegie-Mellon University
 *	Added routine to call a function on each device.
 *
 *  3-Mar-89  David Golub (dbg) at Carnegie-Mellon University
 *	Created.
 *
 */
/* CMU_ENDHIST */
/* 
 * Mach Operating System
 * Copyright (c) 1991,1990,1989,1988 Carnegie Mellon University
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

#include <mach/port.h>
#include <mach/vm_param.h>

#include <kern/queue.h>
#include <kern/zalloc.h>

#include <device/device_types.h>
#include <device/dev_hdr.h>
#include <device/device_emul.h>
#include <device/conf.h>
#include <device/ds_routines.h>

#include <ipc/ipc_port.h>
#include <kern/ipc_kobject.h>

/*
 * Device structure routines: reference counting, port->device.
 */

/*
 * Lookup/enter by device number.
 */
#define	NDEVHASH	8
#define	DEV_NUMBER_HASH(dev)	((dev) & (NDEVHASH-1))
queue_head_t	dev_number_hash_table[NDEVHASH];

/*
 * Lock for device-number to device lookup.
 * Must be held before device-ref_count lock.
 */

decl_mutex_data(,dev_number_lock)

zone_t		dev_hdr_zone;

/* Forward */

extern void		dev_number_enter(
				mach_device_t		device);
extern void		dev_number_remove(
				mach_device_t		device);
extern mach_device_t		dev_number_lookup(
				dev_ops_t		ops,
				int			devnum);

/*
 * Enter device in the number lookup table.
 * The number table lock must be held.
 */
void
dev_number_enter(
	mach_device_t	device)
{
	register queue_t	q;

	q = &dev_number_hash_table[DEV_NUMBER_HASH(device->dev_number)];
	queue_enter(q, device, mach_device_t, number_chain);
}

/*
 * Remove device from the device-number lookup table.
 * The device-number table lock must be held.
 */
void
dev_number_remove(
	mach_device_t	device)
{
	register queue_t	q;

	q = &dev_number_hash_table[DEV_NUMBER_HASH(device->dev_number)];
	queue_remove(q, device, mach_device_t, number_chain);
}

/*
 * Lookup a device by device operations and minor number.
 * The number table lock must be held.
 */
mach_device_t
dev_number_lookup(
	dev_ops_t	ops,
	int		devnum)
{
	register queue_t	q;
	register mach_device_t	device;

	q = &dev_number_hash_table[DEV_NUMBER_HASH(devnum)];
	queue_iterate(q, device, mach_device_t, number_chain) {
	    if (device->dev_ops == ops &&
		device->dev_number == devnum &&
		(device->flag & D_CLONED) == 0) {
		return (device);
	    }
	}
	return (MACH_DEVICE_NULL);
}



/*
 *	This value can be over-ridden at boot-time.
 */
unsigned char	io_in_progress_default = IO_IN_PROGRESS_DEFAULT;

/*
 * Look up a device by name, and create the device structure
 * if it does not exist.  Enter it in the dev_number lookup
 * table.
 */
kern_return_t
device_lookup_mode(
	char *		name,
	dev_mode_t	mode,
	mach_device_t	*dev)
{
	dev_ops_t	dev_ops;
	int		dev_minor;
	register mach_device_t	device;
	register mach_device_t	new_device;
	boolean_t clone;

	/*
	 * Get the device and unit number from the name.
	 */
	if (!dev_name_lookup(name, &dev_ops, &dev_minor))
	    return (D_NO_SUCH_DEVICE);

	/*
	 * Get the cloneability of the request.
	 */
	if ((mode & D_CLONE) == 0)
	    clone = FALSE;
	else if ((*dev_ops->d_dev_info)(
			dev_minor,
			D_INFO_CLONE_OPEN,
			(char *)&clone) == D_INVALID_OPERATION || !clone)
	    return (D_NOT_CLONED);

	/*
	 * Look up the device in the hash table.  If it is
	 * not there, enter it.
	 */

	if (!clone) {
	    mutex_lock(&dev_number_lock);
	    device = dev_number_lookup(dev_ops, dev_minor);
	    if (device == MACH_DEVICE_NULL)
		mutex_unlock(&dev_number_lock);
	}

	if (clone || device == MACH_DEVICE_NULL) {
	    new_device = (mach_device_t) zalloc(dev_hdr_zone);
	    mutex_init(&new_device->ref_lock, ETAP_IO_DEV_REF);

	    new_device->ref_count = 1;

	    mutex_init(&new_device->iop_lock, ETAP_IO_IOP_LOCK);
	    mutex_init(&new_device->lock, ETAP_IO_DEV);

	    new_device->state = DEV_STATE_INIT;
	    new_device->flag = (clone ? D_CLONED : 0);
	    new_device->open_count = 0;
	    new_device->io_in_progress = 0;
	    new_device->io_in_progress_limit = io_in_progress_default;
	    new_device->io_wait = FALSE;
	    new_device->iop_wait = FALSE;
	    new_device->port = IP_NULL;
	    new_device->dev_ops = dev_ops;
	    new_device->dev_number = dev_minor;
	    mutex_lock(&dev_number_lock);
	} else
	    new_device = MACH_DEVICE_NULL;

	if (!clone && device == MACH_DEVICE_NULL)
	    device = dev_number_lookup(dev_ops, dev_minor);

	if (!clone && device != MACH_DEVICE_NULL) {
	    /*
	     * Have existing device.
	     */
	    mach_device_reference(device);
	    mutex_unlock(&dev_number_lock);

	    if (new_device != MACH_DEVICE_NULL)
		zfree(dev_hdr_zone, (vm_offset_t)new_device);
	    *dev = device;
	} else {
	    /*
	     * No existing device structure.  Insert the
	     * new one.
	     */
	    assert(new_device != MACH_DEVICE_NULL);
	    *dev = new_device;

	    dev_number_enter(new_device);
	    mutex_unlock(&dev_number_lock);
	}
	return (D_SUCCESS);
}

/*
 * Keep old device_lookup routine for compatibility.
 */
mach_device_t
device_lookup(
	char *		name)
{
	mach_device_t	device;

	if (device_lookup_mode(name, 0, &device) != D_SUCCESS)
	    return (MACH_DEVICE_NULL);
	return (device);
}

/*
 * Add a reference to the device.
 */
void
mach_device_reference(
	void	*dev_p)
{
	register mach_device_t	device = (mach_device_t) dev_p;

	mutex_lock(&device->ref_lock);
	device->ref_count++;
	mutex_unlock(&device->ref_lock);
}

/*
 * Remove a reference to the device, and deallocate the
 * structure if no references are left.
 */
void
mach_device_deallocate(
	void	*dev_p)
{
	register mach_device_t	device = (mach_device_t) dev_p;

	if (device == MACH_DEVICE_NULL)
	    return;

	mutex_lock(&device->ref_lock);
	if (--device->ref_count > 0) {
	    mutex_unlock(&device->ref_lock);
	    return;
	}
	device->ref_count = 1;
	mutex_unlock(&device->ref_lock);

	mutex_lock(&dev_number_lock);
	mutex_lock(&device->ref_lock);
	if (--device->ref_count > 0) {
	    mutex_unlock(&device->ref_lock);
	    mutex_unlock(&dev_number_lock);
	    return;
	}

	dev_number_remove(device);
	mutex_unlock(&device->ref_lock);
	mutex_unlock(&dev_number_lock);

	zfree(dev_hdr_zone, (vm_offset_t)device);
}

/*
 * port-to-device lookup routines.
 */
decl_mutex_data(,dev_port_lock)

/*
 * Enter a port-to-device mapping.
 */
void
dev_port_enter(
	mach_device_t	device)
{
	extern struct device_emulation_ops mach_device_emulation_ops;

	mach_device_reference(device);
	ipc_kobject_set(device->port,
			(ipc_kobject_t) &device->dev, IKOT_DEVICE);
	device->dev.emul_data = device;
	device->dev.emul_ops = &mach_device_emulation_ops;
}

/*
 * Remove a port-to-device mapping.
 */
void
dev_port_remove(
	mach_device_t	device)
{
	ipc_kobject_set(device->port, IKO_NULL, IKOT_NONE);
	mach_device_deallocate(device);
}

/*
 * Lookup a device by its port.
 * Doesn't consume the naked send right; produces a device reference.
 */
device_t
dev_port_lookup(
	ipc_port_t	port)
{
	register device_t	device;

	if (!IP_VALID(port))
	    return (DEVICE_NULL);

	ip_lock(port);
	if (ip_active(port) && (ip_kotype(port) == IKOT_DEVICE)) {
	    device = (device_t) port->ip_kobject;
	    if (device->emul_ops->reference)
		(*device->emul_ops->reference)(device->emul_data);
	}
	else
	    device = DEVICE_NULL;

	ip_unlock(port);
	return (device);
}

/*
 * Get the port for a device.
 * Consumes a device reference; produces a naked send right.
 */
ipc_port_t
mach_convert_device_to_port(
	void	*dev_p)
{
	register ipc_port_t	port;
	register mach_device_t	device = (mach_device_t) dev_p;

	if (device == MACH_DEVICE_NULL)
	    return IP_NULL;

	device_lock(device);
	if (device->state == DEV_STATE_OPEN)
	    port = ipc_port_make_send(device->port);
	else
	    port = IP_NULL;
	device_unlock(device);

	mach_device_deallocate(device);
	return port;
}

/*
 * Call a supplied routine on each device, passing it
 * the port as an argument.  If the routine returns TRUE,
 * stop the search and return TRUE.  If none returns TRUE,
 * return FALSE.
 */
boolean_t
dev_map(
	boolean_t	(*routine)(mach_device_t dev, ipc_port_t port),
	ipc_port_t	port)
{
	register int		i;
	register queue_t	q;
	register mach_device_t	dev, prev_dev;

	for (i = 0, q = &dev_number_hash_table[0];
	     i < NDEVHASH;
	     i++, q++) {
	    prev_dev = MACH_DEVICE_NULL;
	    mutex_lock(&dev_number_lock);
	    queue_iterate(q, dev, mach_device_t, number_chain) {
		mach_device_reference(dev);
		mutex_unlock(&dev_number_lock);
		if (prev_dev != MACH_DEVICE_NULL)
		    mach_device_deallocate(prev_dev);

		if ((*routine)(dev, port)) {
		    /*
		     * Done
		     */
		    mach_device_deallocate(dev);
		    return (TRUE);
		}

		mutex_lock(&dev_number_lock);
		prev_dev = dev;
	    }
	    mutex_unlock(&dev_number_lock);
	    if (prev_dev != MACH_DEVICE_NULL)
		mach_device_deallocate(prev_dev);
	}
	return (FALSE);
}

/*
 * Initialization
 */
#define	NDEVICES	256

void
dev_lookup_init(void)
{
	register int	i;

	mutex_init(&dev_number_lock, ETAP_IO_DEV_NUM);

	for (i = 0; i < NDEVHASH; i++)
	    queue_init(&dev_number_hash_table[i]);

	mutex_init(&dev_port_lock, ETAP_IO_DEV_PORT);

	dev_hdr_zone = zinit(sizeof(struct mach_device),
			     sizeof(struct mach_device) * NDEVICES,
			     PAGE_SIZE, "open device entry");
	dev_name_init();
}

/*
 * Convert a port to an I/O completion queue kernel object.
 */
io_done_queue_t
io_done_queue_port_lookup(
	ipc_port_t	port)
{
	io_done_queue_t		queue;

	if (!IP_VALID(port))
	    return (IO_DONE_QUEUE_NULL);

	ip_lock(port);
	if (ip_active(port) && (ip_kotype(port) == IKOT_IO_DONE_QUEUE)) {
	    queue = (io_done_queue_t) port->ip_kobject;
	    io_done_queue_reference(queue);
	}
	else
	    queue = IO_DONE_QUEUE_NULL;

	ip_unlock(port);
	return (queue);
}

/*
 * Convert an I/O completion queue kernel object to a port.
 */
ipc_port_t
convert_io_done_queue_to_port(
	io_done_queue_t		queue)
{
	ipc_port_t	port;
	ipc_port_t	qport;
	spl_t		s;

	if (queue == IO_DONE_QUEUE_NULL)
	    return IP_NULL;

	/* NB: the io_done_queue_lock must be a simple_lock, since it is
	   acquired at raised SPL (and is acquired on interrupt side).
	   For other reasons, the port lock must be a mutex, so the
	   usual trick of holding the io_done_queue lock while acquiring
	   the send right on the port won't work.  Since the queue
	   can't go away while there are references, acquire a fake
	   reference during the port operation */

	s = splsched();

	io_done_queue_lock(queue);

	qport = queue->port;

	io_done_queue_reference(queue);

	io_done_queue_unlock(queue);

	splx(s);

	if (qport != IP_NULL)
	    port = ipc_port_make_send(qport);
	else
	    port = IP_NULL;

	io_done_queue_deallocate(queue);

	return(port);
}

/*
 * Add a reference to an I/O completion queue
 */
void
io_done_queue_reference(
	io_done_queue_t		queue)
{
	simple_lock(&queue->ref_lock);
	queue->ref_count++;
	simple_unlock(&queue->ref_lock);
}

