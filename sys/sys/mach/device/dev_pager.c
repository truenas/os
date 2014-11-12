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
 * Revision 2.21.3.3  92/03/03  16:13:39  jeffreyh
 * 	Define device_pager_server_routine for new kernel stub calling
 * 	sequence.
 * 	[92/02/26            dbg]
 * 
 * Revision 2.21.3.2  92/02/21  11:23:09  jsb
 * 	NORMA_VM: changes for vm_object_lookup using pager, not pager_request.
 * 	[92/02/10  09:40:47  jsb]
 * 
 * 	Removed NORMA_VM hacks, leaving only necessary NORMA_VM conditionals.
 * 	In particular, NORMA_VM now uses vm_object_page_map.
 * 	Remaining conditionals necessary because vm_object_lookup is used for
 * 	two different things, one being obtaining a real vm object structure,
 * 	and the other being returning the type of thing that the kernel
 * 	memory_object routines want to see. In a NORMA_VM system, the
 * 	memory_object routines are implemented by the xmm system, and thus
 * 	the things in question are xmm_obj's, not vm objects.
 * 	[92/02/09  12:44:50  jsb]
 * 
 * Revision 2.21.3.1  92/01/03  16:34:21  jsb
 * 	Added check for pager_name != ds->pager_name.
 * 	[91/12/24  13:51:35  jsb]
 * 
 * Revision 2.21  91/09/12  16:37:07  bohman
 * 	For device_map objects, don't setup the actual resident page mappings
 * 	at initialization time.  Instead, do it in response to actual requests
 * 	for the data.  I left in the old code for NORMA_VM.
 * 	[91/09/11  17:05:20  bohman]
 * 
 * Revision 2.20  91/08/28  11:26:19  jsb
 * 	For now, use older dev_pager code for NORMA_VM.
 * 	I'll fix this soon.
 * 	[91/08/28  11:24:45  jsb]
 * 
 * Revision 2.19  91/08/03  18:17:26  jsb
 * 	Corrected declaration of xmm_vm_object_lookup.
 * 	[91/07/17  13:54:47  jsb]
 * 
 * Revision 2.18  91/07/31  17:33:24  dbg
 * 	Use vm_object_page_map to allocate pages for a device-map object.
 * 	Its vm_page structures are allocated and freed by the VM system.
 * 	[91/07/30  16:46:43  dbg]
 * 
 * Revision 2.17  91/06/25  11:06:33  rpd
 * 	Fixed includes to avoid norma files unless they are really needed.
 * 	[91/06/25            rpd]
 * 
 * Revision 2.16  91/06/18  20:49:55  jsb
 * 	Removed extra include of norma_vm.h.
 * 	[91/06/18  18:47:29  jsb]
 * 
 * Revision 2.15  91/06/17  15:43:50  jsb
 * 	NORMA_VM: use xmm_vm_object_lookup, since we really need a vm_object_t;
 * 	use xmm_add_exception to mark device pagers as non-interposable.
 * 	[91/06/17  13:22:58  jsb]
 * 
 * Revision 2.14  91/05/18  14:29:38  rpd
 * 	Added proper locking for vm_page_insert.
 * 	[91/04/21            rpd]
 * 	Changed vm_page_init.
 * 	[91/03/24            rpd]
 * 
 * Revision 2.13  91/05/14  15:41:35  mrt
 * 	Correcting copyright
 * 
 * Revision 2.12  91/02/05  17:08:40  mrt
 * 	Changed to new Mach copyright
 * 	[91/01/31  17:27:40  mrt]
 * 
 * Revision 2.11  90/11/05  14:26:46  rpd
 * 	Fixed memory_object_terminate to return KERN_SUCCESS.
 * 	[90/10/29            rpd]
 * 
 * Revision 2.10  90/09/09  14:31:20  rpd
 * 	Use decl_simple_lock_data.
 * 	[90/08/30            rpd]
 * 
 * Revision 2.9  90/06/02  14:47:19  rpd
 * 	Converted to new IPC.
 * 	Renamed functions to mainline conventions.
 * 	Fixed private/fictitious bug in memory_object_init.
 * 	Fixed port leak in memory_object_terminate.
 * 	[90/03/26  21:49:57  rpd]
 * 
 * Revision 2.8  90/05/29  18:36:37  rwd
 * 	From rpd: set private to specify that the page structure is
 * 	ours, not fictitious.
 * 	[90/05/14            rwd]
 * 	From rpd: set private to specify that the page structure is
 * 	ours, not fictitious.
 * 	[90/05/14            rwd]
 * 
 * Revision 2.7  90/05/21  13:26:21  dbg
 * 	 	From rpd: set private to specify that the page structure is
 * 	 	ours, not fictitious.
 * 	 	[90/05/14            rwd]
 * 	 
 * 
 * Revision 2.6  90/02/22  20:02:05  dbg
 * 	Change PAGE_WAKEUP to PAGE_WAKEUP_DONE to reflect the fact that
 * 	it clears the busy flag.
 * 	[90/01/29            dbg]
 * 
 * Revision 2.5  90/01/11  11:41:53  dbg
 * 	De-lint.
 * 	[89/12/06            dbg]
 * 
 * Revision 2.4  89/09/08  11:23:24  dbg
 * 	Rewrite to run in kernel task (off user thread or
 * 	vm_pageout!)
 * 	[89/08/24            dbg]
 * 
 * Revision 2.3  89/08/09  14:33:03  rwd
 * 	Call round_page on incoming size to get to mach page.
 * 	[89/08/09            rwd]
 * 
 * Revision 2.2  89/08/05  16:04:51  rwd
 * 	Added char_pager code for frame buffer.
 * 	[89/07/26            rwd]
 * 
 * 26-May-89  Randall Dean (rwd) at Carnegie-Mellon University
 *	If no error, zero pad residual and set to 0
 *
 *  3-Mar-89  David Golub (dbg) at Carnegie-Mellon University
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
 *	Date: 	3/89
 *
 * 	Device pager.
 */
#include <norma_vm.h>

#include <mach/boolean.h>
#include <mach/port.h>
#include <mach/message.h>
#include <mach/std_types.h>
#include <mach/mach_types.h>
#include <mach/mach_server.h>

#include <ipc/ipc_port.h>
#include <ipc/ipc_space.h>

#include <kern/misc_protos.h>
#include <kern/queue.h>
#include <kern/zalloc.h>
#include <kern/kalloc.h>

#include <vm/vm_page.h>
#include <vm/vm_kern.h>

#include <device/device_types.h>
#include <device/ds_routines.h>
#include <device/dev_hdr.h>
#include <device/io_req.h>
#include <device/conf.h>

#if	NORMA_VM
#include <xmm/xmm_obj.h>
#endif	/* NORMA_VM */

typedef struct dev_pager *dev_pager_t;

/* Forward */

#if	NORMA_VM
static vm_object_t	vm_object_lookup_by_pager(
				ipc_port_t		pager);
#endif	/* NORMA_VM */

extern void		dev_pager_reference(
				dev_pager_t		ds);
extern void		dev_pager_deallocate(
				dev_pager_t		ds);
extern void		dev_pager_hash_init(void);
extern void		dev_pager_hash_insert(
				ipc_port_t		port,
				dev_pager_t		rec);
extern void		dev_pager_hash_delete(
				ipc_port_t		port);
extern dev_pager_t	dev_pager_hash_lookup(
				ipc_port_t		port);
extern void		device_pager_release(
				memory_object_t		object);
extern boolean_t	device_pager_data_write_done(
				io_req_t		ior);
extern vm_offset_t	device_map_page(
				void			* dsp,
				vm_offset_t		offset);

#if	NORMA_VM
/*
 * Use this when we really want the vm_object structure.
 * Declaring a function bypasses the macro following.
 */
static vm_object_t
vm_object_lookup_by_pager(
	ipc_port_t	pager)
{
	return vm_object_lookup(pager);
}

/*
 * Use this when we want to perform the port to object conversion that
 * mig would normally do automatically. In a NORMA_VM system, that
 * object is an xmm obj, not a vm object.
 */
#define	vm_object_lookup(pager_request)		xmm_kobj_lookup(pager_request)
#endif	/* NORMA_VM */

/*
 *	The device pager routines are called directly from the message
 *	system (via mach_msg), and thus run in the kernel-internal
 *	environment.  All ports are in internal form (ipc_port_t),
 *	and must be correctly reference-counted in order to be saved
 *	in other data structures.  Kernel routines may be called
 *	directly.  Kernel types are used for data objects (tasks,
 *	memory objects, ports).  The only IPC routines that may be
 *	called are ones that masquerade as the kernel task (via
 *	msg_send_from_kernel).
 *
 *	Port rights and references are maintained as follows:
 *		Memory object port:
 *			The device_pager task has all rights.
 *		Memory object control port:
 *			The device_pager task has only send rights.
 *		Memory object name port:
 *			The device_pager task has only send rights.
 *			The name port is not even recorded.
 *	Regardless how the object is created, the control and name
 *	ports are created by the kernel and passed through the memory
 *	management interface.
 *
 *	The device_pager assumes that access to its memory objects
 *	will not be propagated to more that one host, and therefore
 *	provides no consistency guarantees beyond those made by the
 *	kernel.
 *
 *	In the event that more than one host attempts to use a device
 *	memory object, the device_pager will only record the last set
 *	of port names.  [This can happen with only one host if a new
 *	mapping is being established while termination of all previous
 *	mappings is taking place.]  Currently, the device_pager assumes
 *	that its clients adhere to the initialization and termination
 *	protocols in the memory management interface; otherwise, port
 *	rights or out-of-line memory from erroneous messages may be
 *	allowed to accumulate.
 *
 *	[The phrase "currently" has been used above to denote aspects of
 *	the implementation that could be altered without changing the rest
 *	of the basic documentation.]
 */

/*
 * Basic device pager structure.
 */
struct dev_pager {
	decl_mutex_data(,lock)		/* lock for reference count */
	int		ref_count;	/* reference count */
	int		client_count;	/* How many memory_object_create
					 * calls have we received */
	ipc_port_t	pager;		/* pager port */
	ipc_port_t	pager_request;	/* Known request port */
	mach_device_t	device;		/* Device handle */
	int		type;		/* to distinguish */
#define DEV_PAGER_TYPE	0
#define CHAR_PAGER_TYPE	1
	/* char pager specifics */
	int		prot;
	vm_size_t	size;
};

zone_t		dev_pager_zone;

#define	DEV_PAGER_NULL	((dev_pager_t)0)

void
dev_pager_reference(
	dev_pager_t		ds)
{
	mutex_lock(&ds->lock);
	ds->ref_count++;
	mutex_unlock(&ds->lock);
}

void
dev_pager_deallocate(
	dev_pager_t		ds)
{
	mutex_lock(&ds->lock);
	if (--ds->ref_count > 0) {
	    mutex_unlock(&ds->lock);
	    return;
	}

	mutex_unlock(&ds->lock);
	zfree(dev_pager_zone, (vm_offset_t)ds);
}

/*
 * A hash table of ports for device_pager backed objects.
 */

#define	DEV_PAGER_HASH_COUNT		127

struct dev_pager_entry {
	queue_chain_t	links;
	ipc_port_t	name;
	dev_pager_t	pager_rec;
};
typedef struct dev_pager_entry *dev_pager_entry_t;

queue_head_t	dev_pager_hashtable[DEV_PAGER_HASH_COUNT];
zone_t		dev_pager_hash_zone;
decl_mutex_data(,dev_pager_hash_lock)

#define	dev_pager_hash(name_port) \
		(((natural_t)(name_port) & 0xffffff) % DEV_PAGER_HASH_COUNT)

void
dev_pager_hash_init(void)
{
	register int	i;
	register vm_size_t	size;

	size = sizeof(struct dev_pager_entry);
	dev_pager_hash_zone = zinit(
				size,
				size * 1000,
				PAGE_SIZE,
				"dev_pager port hash");
	for (i = 0; i < DEV_PAGER_HASH_COUNT; i++)
	    queue_init(&dev_pager_hashtable[i]);
	mutex_init(&dev_pager_hash_lock, ETAP_IO_DEV_PAGEH);
}

void
dev_pager_hash_insert(
	ipc_port_t	name_port,
	dev_pager_t	rec)
{
	register dev_pager_entry_t new_entry;

	new_entry = (dev_pager_entry_t) zalloc(dev_pager_hash_zone);
	new_entry->name = name_port;
	new_entry->pager_rec = rec;

	mutex_lock(&dev_pager_hash_lock);
	queue_enter(&dev_pager_hashtable[dev_pager_hash(name_port)],
			new_entry, dev_pager_entry_t, links);
	mutex_unlock(&dev_pager_hash_lock);
}

void
dev_pager_hash_delete(
	ipc_port_t	name_port)
{
	register queue_t	bucket;
	register dev_pager_entry_t	entry;

	bucket = &dev_pager_hashtable[dev_pager_hash(name_port)];

	mutex_lock(&dev_pager_hash_lock);
	for (entry = (dev_pager_entry_t)queue_first(bucket);
	     !queue_end(bucket, &entry->links);
	     entry = (dev_pager_entry_t)queue_next(&entry->links)) {
	    if (entry->name == name_port) {
		queue_remove(bucket, entry, dev_pager_entry_t, links);
		break;
	    }
	}
	mutex_unlock(&dev_pager_hash_lock);
	if (entry)
	    zfree(dev_pager_hash_zone, (vm_offset_t)entry);
}

dev_pager_t
dev_pager_hash_lookup(
	ipc_port_t	name_port)
{
	register queue_t	bucket;
	register dev_pager_entry_t	entry;
	register dev_pager_t	pager;

	bucket = &dev_pager_hashtable[dev_pager_hash(name_port)];

	mutex_lock(&dev_pager_hash_lock);
	for (entry = (dev_pager_entry_t)queue_first(bucket);
	     !queue_end(bucket, &entry->links);
	     entry = (dev_pager_entry_t)queue_next(&entry->links)) {
	    if (entry->name == name_port) {
		pager = entry->pager_rec;
		dev_pager_reference(pager);
		mutex_unlock(&dev_pager_hash_lock);
		return (pager);
	    }
	}
	mutex_unlock(&dev_pager_hash_lock);
	return (DEV_PAGER_NULL);
}

/*ARGSUSED*/
kern_return_t
device_pager_setup(
	mach_device_t	device,
	int		prot,
	vm_offset_t	offset,
	vm_size_t	size,
	ipc_port_t	*pager)
{
	register dev_pager_t	d;

	/*
	 *	Allocate a structure to hold the arguments
	 *	and port to represent this object.
	 */

	d = dev_pager_hash_lookup((ipc_port_t)device);	/* HACK */
	if (d != DEV_PAGER_NULL) {
		*pager = (ipc_port_t) ipc_port_make_send(d->pager);
		dev_pager_deallocate(d);
		return (D_SUCCESS);
	}

	d = (dev_pager_t) zalloc(dev_pager_zone);
	if (d == DEV_PAGER_NULL)
		return (KERN_RESOURCE_SHORTAGE);

	mutex_init(&d->lock, ETAP_IO_DEV_PAGER);
	d->ref_count = 1;

	/*
	 * Allocate the pager port.
	 */
	d->pager = ipc_port_alloc_kernel();
	if (d->pager == IP_NULL) {
		dev_pager_deallocate(d);
		return (KERN_RESOURCE_SHORTAGE);
	}

	d->client_count = 0;
	d->pager_request = IP_NULL;
	d->device = device;
	mach_device_reference(device);
	d->prot = prot;
	d->size = round_page(size);
	if (device->dev_ops->d_mmap == block_io_mmap) {
		d->type = DEV_PAGER_TYPE;
	} else {
		d->type = CHAR_PAGER_TYPE;
	}

	dev_pager_hash_insert(d->pager, d);
	dev_pager_hash_insert((ipc_port_t)device, d);	/* HACK */

	*pager = (ipc_port_t) ipc_port_make_send(d->pager);
	return (KERN_SUCCESS);
}

/*
 *	Routine:	device_pager_release
 *	Purpose:
 *		Relinquish any references or rights that were
 *		associated with the result of a call to
 *		device_pager_setup.
 */
void
device_pager_release(
	memory_object_t		object)
{
	if (MACH_PORT_VALID(object))
		ipc_port_release_send((ipc_port_t) object);
}

/*
 * Rename all of the functions in the pager interface, to avoid
 * confusing them with the kernel interface.
 */

#define	memory_object_init		device_pager_init_pager
#define	memory_object_terminate		device_pager_terminate
#define	memory_object_data_request	device_pager_data_request
#define	memory_object_data_unlock	device_pager_data_unlock
#define	memory_object_lock_completed	device_pager_lock_completed
#define	memory_object_supply_completed	device_pager_supply_completed
#define	memory_object_synchronize	device_pager_synchronize
#define	memory_object_data_return	device_pager_data_return
#define	memory_object_change_completed	device_pager_change_completed

/*
 * Include memory_object_server in this file to avoid name
 * conflicts with other possible pagers.
 */
#define	memory_object_server		device_pager_server
#define	memory_object_server_routine	device_pager_server_routine

#include <device/device_pager_server.h>
#include <device/device_pager_server.c>

boolean_t	device_pager_debug = FALSE;

kern_return_t
memory_object_data_request(
	ipc_port_t	pager,
	ipc_port_t	pager_request,
	vm_offset_t	offset,
	vm_size_t	length,
	vm_prot_t	protection_required)
{
	register dev_pager_t	ds;

#ifdef lint
	protection_required++;
#endif /* lint */

	if (device_pager_debug)
		printf("(device_pager)data_request: pager=%ld, offset=0x%lx, length=0x%lx\n",
			pager, offset, length);

	ds = dev_pager_hash_lookup((ipc_port_t)pager);
	if (ds == DEV_PAGER_NULL)
		panic("(device_pager)data_request: lookup failed");

	if (ds->pager_request != pager_request)
		panic("(device_pager)data_request: bad pager_request");

	if (ds->type == CHAR_PAGER_TYPE) {
	    register vm_object_t	object;
#if	NORMA_VM
	    object = vm_object_lookup_by_pager(pager);
	    if (object == VM_OBJECT_NULL) {
		    (void) memory_object_data_error(
					vm_object_lookup(pager_request),
					offset, length, KERN_FAILURE);
		    return KERN_FAILURE;
	    }
#else	/* NORMA_VM */
	    object = vm_object_lookup(pager_request);
	    assert(object != VM_OBJECT_NULL);
#endif	/* NORMA_VM */
	    vm_object_lock(object);
	    object->private = TRUE;
	    vm_object_unlock(object);

	    vm_object_page_map(object, offset, length,
			       device_map_page, (void *)ds);

	    vm_object_deallocate(object);
	}
	else {
	    panic("(device_pager)data_request: dev pager");
	}

	dev_pager_deallocate(ds);

	return (KERN_SUCCESS);
}

boolean_t
device_pager_data_write_done(
	io_req_t		ior)
{
	device_write_dealloc(ior);
	mach_device_deallocate(ior->io_device);

	return (TRUE);
}

/*ARGSUSED*/
kern_return_t
memory_object_synchronize(
	ipc_port_t		pager,
	ipc_port_t		pager_request,
	vm_offset_t		offset,
	vm_offset_t		length,
	vm_sync_t		sync_flags)
{
	memory_object_synchronize_completed(vm_object_lookup(pager_request),
					offset, length);
	return (KERN_SUCCESS);
}/* memory_object_synchronize */

/*ARGSUSED*/
kern_return_t
memory_object_supply_completed(
	ipc_port_t		pager,
	ipc_port_t		pager_request,
	vm_offset_t		offset,
	vm_size_t		length,
	kern_return_t		result,
	vm_offset_t		error_offset)
{
	panic("(device_pager)supply_completed: called");
	return (KERN_FAILURE);
}

/*ARGSUSED*/
kern_return_t
memory_object_data_return(
	ipc_port_t		pager,
	ipc_port_t		pager_request,
	vm_offset_t		offset,
	pointer_t		addr,
	mach_msg_type_number_t	data_count,
	boolean_t		dirty,
	boolean_t		kernel_copy)
{
	panic("(device_pager)data_return: called");
	return (KERN_FAILURE);
}

/*ARGSUSED*/
kern_return_t
memory_object_change_completed(
	ipc_port_t			pager,
	ipc_port_t			pager_request,
	memory_object_flavor_t		flavor)
{
	panic("(device_pager)change_completed: called");
	return (KERN_FAILURE);
}

/*
 *	The mapping function takes a byte offset, but returns
 *	a machine-dependent page frame number.  We convert
 *	that into something that the pmap module will
 *	accept later.
 */
vm_offset_t
device_map_page(
	void		* dsp,
	vm_offset_t	offset)
{
	register dev_pager_t	ds = (dev_pager_t) dsp;

	return pmap_phys_address(
		   (*(ds->device->dev_ops->d_mmap))
			(ds->device->dev_number, (vm_offset_t)offset,
			ds->prot));
}

/*ARGSUSED*/
kern_return_t
memory_object_init(
	ipc_port_t	pager,
	ipc_port_t	pager_request,
	vm_size_t	pager_page_size)
{
	register dev_pager_t		ds;
	security_token_t		secid = { {0, 0} };
	memory_object_attr_info_data_t 	info_data;
	memory_object_attr_info_t info 	= &info_data;
	vm_object_t			object;

	if (device_pager_debug)
		printf("(device_pager)init: pager=%ld, request=%ld\n",
		       pager, pager_request);

	assert(pager_page_size == PAGE_SIZE);
	assert(IP_VALID(pager_request));

	ds = dev_pager_hash_lookup(pager);
	assert(ds != DEV_PAGER_NULL);

	assert(ds->client_count == 0);
	assert(ds->pager_request == IP_NULL);

	ds->client_count = 1;

	/*
	 * We save the send rights for the request port.
	 */

	ds->pager_request = pager_request;

	if (ds->type == CHAR_PAGER_TYPE) {
	    info->may_cache_object = FALSE;
	    info->copy_strategy = MEMORY_OBJECT_COPY_NONE;
	} else {
	    info->may_cache_object = TRUE;
	    info->copy_strategy = MEMORY_OBJECT_COPY_DELAY;
	}

	/*
	 * Reply that the object is ready
	 */
	info->temporary = TRUE;
	info->cluster_size = pager_page_size;
	(void) memory_object_change_attributes(
					vm_object_lookup(pager_request),
					MEMORY_OBJECT_ATTRIBUTE_INFO,
					(memory_object_info_t)info,
					MEMORY_OBJECT_ATTR_INFO_COUNT,
					MACH_PORT_NULL,
					MACH_MSG_TYPE_COPY_SEND);

	dev_pager_deallocate(ds);
	return (KERN_SUCCESS);
}

kern_return_t
memory_object_terminate(
	ipc_port_t	pager,
	ipc_port_t	pager_request)
{
	register dev_pager_t	ds;

	assert(IP_VALID(pager_request));

	ds = dev_pager_hash_lookup(pager);
	assert(ds != DEV_PAGER_NULL);

	assert(ds->client_count == 1);
	assert(ds->pager_request == pager_request);

	dev_pager_hash_delete(ds->pager);
	dev_pager_hash_delete((ipc_port_t)ds->device);	/* HACK */
	mach_device_deallocate(ds->device);

	/* release the send rights we have saved from the init call */

	ipc_port_release_send(pager_request);

	/* release the naked receive rights we just acquired */

	ipc_port_release_receive(pager_request);

	/* release the kernel's receive right for the pager port */

	ipc_port_dealloc_kernel(pager);

	/* once for ref from lookup, once to make it go away */
	dev_pager_deallocate(ds);
	dev_pager_deallocate(ds);

	return (KERN_SUCCESS);
}

/*ARGSUSED*/
kern_return_t
memory_object_data_unlock(
	ipc_port_t		pager,
	ipc_port_t		memory_control_port,
	vm_offset_t		offset,
	vm_size_t		length,
	vm_prot_t		desired_access)
{
	panic("(device_pager)data_unlock: called");
	return (KERN_FAILURE);
}

/*ARGSUSED*/
kern_return_t
memory_object_lock_completed(
	ipc_port_t		pager,
	ipc_port_t		pager_request_port,
	vm_offset_t		offset,
	vm_size_t		length)
{
	panic("(device_pager)lock_completed: called");
	return (KERN_FAILURE);
}


void
device_pager_init(void)
{
	register vm_size_t	size;

	/*
	 * Initialize zone of paging structures.
	 */
	size = sizeof(struct dev_pager);
	dev_pager_zone = zinit(size,
				(vm_size_t) size * 1000,
				PAGE_SIZE,
				"device pager structures");

	/*
	 *	Initialize the name port hashing stuff.
	 */
	dev_pager_hash_init();
}
