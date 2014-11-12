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
 * Revision 2.11  91/08/03  18:17:30  jsb
 * 	Removed NORMA hooks.
 * 	[91/07/17  23:06:14  jsb]
 * 
 * Revision 2.10  91/06/17  15:43:55  jsb
 * 	Renamed NORMA conditionals.
 * 	[91/06/17  09:58:32  jsb]
 * 
 * Revision 2.9  91/06/06  17:03:56  jsb
 * 	NORMA_BOOT: master_device_port initialization change.
 * 	[91/05/13  16:51:18  jsb]
 * 
 * Revision 2.8  91/05/14  15:42:08  mrt
 * 	Correcting copyright
 * 
 * Revision 2.7  91/03/16  14:42:48  rpd
 * 	Changed net_rcv_msg_thread to net_thread.
 * 	[91/02/13            rpd]
 * 
 * Revision 2.6  91/02/05  17:08:54  mrt
 * 	Changed to new Mach copyright
 * 	[91/01/31  17:27:56  mrt]
 * 
 * Revision 2.5  90/12/14  10:59:35  jsb
 * 	Added NORMA_BOOT support.
 * 	[90/12/13  21:06:19  jsb]
 * 
 * Revision 2.4  90/08/27  21:55:11  dbg
 * 	Removed call to cinit.
 * 	[90/07/09            dbg]
 * 
 * Revision 2.3  90/06/02  14:47:29  rpd
 * 	Converted to new IPC.
 * 	[90/03/26  21:51:02  rpd]
 * 
 * Revision 2.2  89/09/08  11:23:33  dbg
 * 	Created.
 * 	[89/08/02            dbg]
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
 *	Date: 	8/89
 *
 * 	Initialize device service as part of kernel task.
 */
#include <xkmachkernel.h>
#include <dipc_xkern.h>
#include <ipc/ipc_port.h>
#include <ipc/ipc_space.h>
#include <kern/task.h>
#include <kern/misc_protos.h>
#include <device/device_types.h>
#include <device/device_port.h>
#include <device/net_io.h>
#include <device/ds_routines.h>
#include <device/dev_hdr.h>
#include <device/data_device.h>
#
#if XKMACHKERNEL && !DIPC_XKERN
#include <uk_xkern/include/utils.h>
#endif	/* XKMACHKERNEL && !DIPC_XKERN */

ipc_port_t	master_device_port;

void
device_service_create(void)
{
	master_device_port = ipc_port_alloc_kernel();
	if (master_device_port == IP_NULL)
	    panic("can't allocate master device port");

	ipc_kobject_set(master_device_port, 1, IKOT_MASTER_DEVICE);
	ds_init();
	net_io_init();
	device_pager_init();
	datadev_init();

	(void) kernel_thread(kernel_task, io_done_thread, (char *)0);
	(void) kernel_thread(kernel_task, net_thread, (char *)0);
#if	XKMACHKERNEL && !DIPC_XKERN
	/*
	 * Initialize the x-kernel
	 */
	(void) kernel_thread(kernel_task, xkInit, (char *)0);
#endif	/* XKMACHKERNEL && !DIPC_XKERN */
}
