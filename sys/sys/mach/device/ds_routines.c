/*
 * Ported to OSF Mach by Elgin Lee (ehl@funghi.com), 1998
 */
/* 
 * Mach device server routines (i386at version).
 *
 * Copyright (c) 1996 The University of Utah and
 * the Computer Systems Laboratory at the University of Utah (CSL).
 * All rights reserved.
 *
 * Permission to use, copy, modify and distribute this software is hereby
 * granted provided that (1) source code retains these copyright, permission,
 * and disclaimer notices, and (2) redistributions including binaries
 * reproduce the notices in supporting documentation, and (3) all advertising
 * materials mentioning features or use of this software display the following
 * acknowledgement: ``This product includes software developed by the
 * Computer Systems Laboratory at the University of Utah.''
 *
 * THE UNIVERSITY OF UTAH AND CSL ALLOW FREE USE OF THIS SOFTWARE IN ITS "AS
 * IS" CONDITION.  THE UNIVERSITY OF UTAH AND CSL DISCLAIM ANY LIABILITY OF
 * ANY KIND FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * CSL requests users of this software to return to csl-dist@cs.utah.edu any
 * improvements that they make and grant CSL redistribution rights.
 *
 *	Author: Shantanu Goel, University of Utah CSL
 */

#include <linux_dev.h>
#if	LINUX_DEV
#include <linux/device-drivers.h> /* file name is consistent with gnumach */
#endif

#include <mach/boolean.h>
#include <mach/kern_return.h>
#include <mach/mig_errors.h>
#include <mach/port.h>
#include <mach/notify.h>

#include <device/device_types.h>
#include <device/dev_hdr.h>
#include <device/device_emul.h>
#include <device/dev_hdr.h>
#include <device/ds_routines.h>
#include <device/net_io.h>
#include <device/device_port.h>
#include <device/device_reply.h>
#include <device/device_server.h>

extern struct device_emulation_ops mach_device_emulation_ops;
#if	LINUX_DEV
extern struct device_emulation_ops linux_block_emulation_ops;
#ifdef	CONFIG_INET
extern struct device_emulation_ops linux_net_emulation_ops;
#endif
#endif

extern void		ds_no_senders(
				mach_no_senders_notification_t
						* notification);

/* List of emulations.	*/
static struct device_emulation_ops *emulation_list[] =
{
#if	LINUX_DEV
  &linux_block_emulation_ops,
#ifdef	CONFIG_INET
  &linux_net_emulation_ops,
#endif
#endif
  &mach_device_emulation_ops,
};

#define NUM_EMULATION (sizeof (emulation_list) / sizeof (emulation_list[0]))

io_return_t
ds_device_open (ipc_port_t open_port, ipc_port_t reply_port,
		mach_msg_type_name_t reply_port_type, ledger_t ledger,
		dev_mode_t mode, security_token_t sec_token,
		char *name, device_t *devp)
{
  int i;
  device_t dev;
  io_return_t err;

  /* Open must be called on the master device port.  */
  if (open_port != master_device_port)
    return D_INVALID_OPERATION;

  /* There must be a reply port.  */
  if (! IP_VALID (reply_port))
    {
      printf ("ds_* invalid reply port\n");
      Debugger ("ds_* reply_port");
      return MIG_NO_REPLY;
    }

  /* Call each emulation's open routine to find the device.  */
  for (i = 0; i < NUM_EMULATION; i++)
    {
      err = (*emulation_list[i]->open) (reply_port, reply_port_type, ledger,
					mode, sec_token, name, devp);
      if (err != D_NO_SUCH_DEVICE)
	break;
    }

  return err;
}

io_return_t
ds_device_close (device_t dev)
{
  if (dev == DEVICE_NULL)
    return D_NO_SUCH_DEVICE;
  return (dev->emul_ops->close
	  ? (*dev->emul_ops->close) (dev->emul_data)
	  : D_SUCCESS);
}

io_return_t
ds_device_write (device_t dev, ipc_port_t reply_port,
		 mach_msg_type_name_t reply_port_type, dev_mode_t mode,
		 recnum_t recnum, io_buf_ptr_t data,
		 mach_msg_type_number_t count, io_buf_len_t *bytes_written)
{
  if (dev == DEVICE_NULL)
    return D_NO_SUCH_DEVICE;
  if (! data)
    return D_INVALID_SIZE;
  if (! dev->emul_ops->write)
    return D_INVALID_OPERATION;
  return (*dev->emul_ops->write) (dev->emul_data, reply_port,
				  reply_port_type, mode, recnum,
				  data, count, bytes_written);
}

io_return_t
ds_device_write_inband (device_t dev, ipc_port_t reply_port,
			mach_msg_type_name_t reply_port_type,
			dev_mode_t mode, recnum_t recnum,
			io_buf_ptr_inband_t data,
			mach_msg_type_number_t count,
			io_buf_len_t *bytes_written)
{
  if (dev == DEVICE_NULL)
    return D_NO_SUCH_DEVICE;
  if (! data)
    return D_INVALID_SIZE;
  if (! dev->emul_ops->write_inband)
    return D_INVALID_OPERATION;
  return (*dev->emul_ops->write_inband) (dev->emul_data, reply_port,
					 reply_port_type, mode, recnum,
					 data, count, bytes_written);
}

io_return_t
ds_device_write_common (device_t dev, ipc_port_t reply_port,
		 mach_msg_type_name_t reply_port_type, dev_mode_t mode,
		 recnum_t recnum, io_buf_ptr_t data,
		 mach_msg_type_number_t count, int op,
		 io_buf_len_t *bytes_written)
{
  if (dev == DEVICE_NULL)
    return D_NO_SUCH_DEVICE;
  if (! data)
    return D_INVALID_SIZE;
  if (! dev->emul_ops->write_common)
    return D_INVALID_OPERATION;
  return (*dev->emul_ops->write_common) (dev->emul_data, reply_port,
				  reply_port_type, mode, recnum,
				  data, count, op, bytes_written);
}

io_return_t
ds_device_read (device_t dev, ipc_port_t reply_port,
		mach_msg_type_name_t reply_port_type, dev_mode_t mode,
		recnum_t recnum, io_buf_len_t count, io_buf_ptr_t *data,
		mach_msg_type_number_t *bytes_read)
{
  if (dev == DEVICE_NULL)
    return D_NO_SUCH_DEVICE;
  if (! dev->emul_ops->read)
    return D_INVALID_OPERATION;
  return (*dev->emul_ops->read) (dev->emul_data, reply_port,
				 reply_port_type, mode, recnum,
				 count, data, bytes_read);
}

io_return_t
ds_device_read_inband (device_t dev, ipc_port_t reply_port,
		       mach_msg_type_name_t reply_port_type, dev_mode_t mode,
		       recnum_t recnum, io_buf_len_t count,
		       io_buf_ptr_inband_t data,
		       mach_msg_type_number_t *bytes_read)
{
  if (dev == DEVICE_NULL)
    return D_NO_SUCH_DEVICE;
  if (! dev->emul_ops->read_inband)
    return D_INVALID_OPERATION;
  return (*dev->emul_ops->read_inband) (dev->emul_data, reply_port,
					reply_port_type, mode, recnum,
					count, data, bytes_read);
}

io_return_t
ds_device_read_common (device_t dev, ipc_port_t reply_port,
		       mach_msg_type_name_t reply_port_type, dev_mode_t mode,
		       recnum_t recnum, io_buf_len_t count, int op,
		       io_buf_ptr_t *data, mach_msg_type_number_t *bytes_read)
{
  if (dev == DEVICE_NULL)
    return D_NO_SUCH_DEVICE;
  if (! dev->emul_ops->read_common)
    return D_INVALID_OPERATION;
  return (*dev->emul_ops->read_common) (dev->emul_data, reply_port,
					reply_port_type, mode, recnum,
					count, op, data, bytes_read);
}

io_return_t
ds_device_set_status (device_t dev, dev_flavor_t flavor,
		      dev_status_t status, mach_msg_type_number_t status_count)
{
  if (dev == DEVICE_NULL)
    return D_NO_SUCH_DEVICE;
  if (! dev->emul_ops->set_status)
    return D_INVALID_OPERATION;

  return (*dev->emul_ops->set_status) (dev->emul_data, flavor, status,
				       status_count);
}

io_return_t
ds_device_get_status (device_t dev, dev_flavor_t flavor, dev_status_t status,
		      mach_msg_type_number_t *status_count)
{
  if (dev == DEVICE_NULL)
    return D_NO_SUCH_DEVICE;
  if (! dev->emul_ops->get_status)
    return D_INVALID_OPERATION;

  return (*dev->emul_ops->get_status) (dev->emul_data, flavor, status,
				       status_count);
}

io_return_t
ds_device_set_filter (device_t dev, ipc_port_t receive_port, int priority,
		      filter_t *filter, mach_msg_type_number_t filter_count)
{
  if (dev == DEVICE_NULL)
    return D_NO_SUCH_DEVICE;
  if (! dev->emul_ops->set_filter)
    return D_INVALID_OPERATION;
  return (*dev->emul_ops->set_filter) (dev->emul_data, receive_port,
				       priority, filter, filter_count);
}

io_return_t
ds_device_map (device_t dev, vm_prot_t prot, vm_offset_t offset,
	       vm_size_t size, ipc_port_t *pager, boolean_t unmap)
{
  if (dev == DEVICE_NULL)
    return D_NO_SUCH_DEVICE;
  if (! dev->emul_ops->map)
    return D_INVALID_OPERATION;
  return (*dev->emul_ops->map) (dev->emul_data, prot,
				offset, size, pager, unmap);
}

boolean_t
ds_notify (mach_msg_header_t *msg)
{
  if (msg->msgh_id == MACH_NOTIFY_NO_SENDERS)
    {
      device_t dev;
      mach_no_senders_notification_t *ns;

      ns = (mach_no_senders_notification_t *) msg;
      dev = (device_t) ns->not_header.msgh_remote_port;
      if (dev->emul_ops->no_senders)
	(*dev->emul_ops->no_senders) (ns);
      return TRUE;
    }

  printf ("ds_notify: strange notification %d\n", msg->msgh_id);
  return FALSE;
}

void
device_reference (device_t dev)
{
  if (dev->emul_ops->reference)
    (*dev->emul_ops->reference) (dev->emul_data);
}

void
device_deallocate (device_t dev)
{
  if (dev->emul_ops->dealloc)
    (*dev->emul_ops->dealloc) (dev->emul_data);
}

/*
 * Get the port for a device.
 * Consumes a device reference; produces a naked send right.
 */
ipc_port_t
convert_device_to_port(
	device_t	device)
{
	if (device == DEVICE_NULL)
	    return IP_NULL;

	return (*device->emul_ops->dev_to_port) (device->emul_data);
}

/*
 * Handle the No-More_Senders notification generated from a device port destroy.
 * Since there are no longer any tasks which hold a send right to this device
 * port a NMS notification has been generated. We check the device state and
 * if it's closing then this becomes a no-op, otherwise the task holding the
 * send right has died without performing a device_close() call. We clean house
 * here by calling ds_device_close() on the task's behalf.
 */
void
ds_no_senders(
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

		if (device->emul_ops->no_senders) {
			(*device->emul_ops->no_senders)(notification);
			return;
		}

		/*
		 * release reference created by previous dev_port_lookup()
		 */
		if (device->emul_ops->dealloc)
			(*device->emul_ops->dealloc)(device->emul_data);

		assert( convert_device_to_port(device) == dev_port );

		/*
		 * The task which owned the last send right has died without
		 * doing a device_close(), do it now.
		 */
		if ( (rc=ds_device_close(device)) != D_SUCCESS )
			printf("ds_no_senders() ds_device_close(%x) rc %d\n",
				device,rc);
	}
}

boolean_t
ds_master_notify(
	mach_msg_header_t *msg)
{
	extern ipc_port_t master_device_port;

	assert(msg->msgh_remote_port == (mach_port_t)master_device_port);

	switch (msg->msgh_id) {
	case MACH_NOTIFY_DEAD_NAME: {
		mach_dead_name_notification_t *m;

		m = (mach_dead_name_notification_t *)msg;
		if (net_unset_filter((ipc_port_t)m->not_port))
			return TRUE;
		break;
	}
	default:
		break;
	}

	printf("ds_master_notify: strange notification %d\n", msg->msgh_id);
	return FALSE;
}
