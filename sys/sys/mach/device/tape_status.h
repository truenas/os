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
 * Revision 2.7  91/05/14  16:01:44  mrt
 * 	Correcting copyright
 * 
 * Revision 2.6  91/05/13  06:02:28  af
 * 	Modified tape_status structure to hold more info
 * 	either way.  Added flags field to hold special
 * 	behavioural properties such as rewind-on-close.
 * 	[91/05/12            af]
 * 
 * Revision 2.5  91/03/16  14:43:42  rpd
 * 	Fixed ioctl definitions for ANSI C.
 * 	[91/02/20            rpd]
 * 
 * Revision 2.4  91/02/05  17:10:21  mrt
 * 	Changed to new Mach copyright
 * 	[91/01/31  17:30:46  mrt]
 * 
 * Revision 2.3  90/12/05  23:28:29  af
 * 	Created.
 * 
 * Revision 2.2  90/12/05  20:42:10  af
 * 	Created, from BSD 4.3Reno mtio.h.
 * 	[90/11/11            af]
 * 
 */
/*
 * 
 * $Copyright
 * Copyright 1991 Intel Corporation
 * INTEL CONFIDENTIAL
 * The technical data and computer software contained herein are subject
 * to the copyright notices; trademarks; and use and disclosure
 * restrictions identified in the file located in /etc/copyright on
 * this system.
 * Copyright$
 * 
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
 * HISTORY
 * tape_status.h,v
 * Revision 1.11  1994/06/10  17:14:46  richardg
 *  Reviewer: Jerrie Coffman
 *  Risk: low
 *  Benefit or PTS #: included sys/types.h instead of declaring daddr_t locally
 *  Testing: built the mk.
 *  Module(s):
 *
 * Revision 1.10  1994/05/23  21:47:52  richardg
 *  Reviewer: Jerrie Coffman
 *  Risk: low
 *  Benefit or PTS #: merged in sys/mtio.h so there is only one file to maintain.
 *  Testing: tape EAT's
 *  Module(s):
 *
 * Revision 1.9  1994/04/20  22:16:17  richardg
 *  Reviewer: Jerrie Coffman
 *  Risk: low
 *  Benefit or PTS #: added for 3480 Tape Library support - LOAD DISPLAY.
 *  Testing: tape EAT's and costom code to exercise the ioctl
 *  Module(s):
 *
 * Revision 1.8  1994/03/08  17:36:46  richardg
 *  Reviewer: Jerrie Coffman
 *  Risk: low
 *  Benefit or PTS #: adding 3480 tape library support
 *  Testing: tape EAT's, a short program that uses the MTLOCATE ioctl
 *  Module(s): added the define for MTLOCATE.
 *
 * Revision 1.7  1994/03/03  23:18:51  richardg
 *  Reviewer: Jerrie Coffman
 *  Risk: low
 *  Benefit or PTS #: adding support for 3480 tape library.
 *  Testing: tape EAT's, test program that uses the MTIOPOS ioctl.
 *  Module(s): this defines the ioctl and the data structure that holds the
 * 		value(s) returned by a SCSI READ POSITION.  this file
 * 		is the origin of sys/mtio.h
 *
 * Revision 1.6  1993/09/20  17:15:06  jerrie
 * Removed the #ifdef PARAGON860's from around the TAPE_FLG_WRITE_COMPRESSION
 * tape_status flag.
 *
 * Revision 1.5  1993/09/14  21:57:15  jerrie
 * Added TAPE_FLG_WRITE_COMPRESSION to tape_status flags to allow
 * enable and disable of tape data compression on writes.
 *
 * Revision 1.4  1993/06/30  22:22:41  dleslie
 * Adding copyright notices required by legal folks
 *
 * Revision 1.3  1993/04/27  20:21:03  dleslie
 * Copy of R1.0 sources onto main trunk
 *
 * Revision 1.1.10.2  1993/04/22  18:20:54  dleslie
 * First R1_0 release
 */
/*
 * Copyright (c) 1982, 1986 The Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that the above copyright notice and this paragraph are
 * duplicated in all such forms and that any documentation,
 * advertising materials, and other materials related to such
 * distribution and use acknowledge that the software was developed
 * by the University of California, Berkeley.  The name of the
 * University may not be used to endorse or promote products derived
 * from this software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 *	@(#)mtio.h	7.4 (Berkeley) 8/31/88
 */

#ifndef	_TAPE_STATUS_H_
#define	_TAPE_STATUS_H_

#include <device/ds_status.h>

/*
 * Tape status
 */

struct tape_status {
	unsigned int	mt_type;
	unsigned int	speed;
	unsigned int	density;
	unsigned int	flags;
#	define TAPE_FLG_REWIND			0x1
#	define TAPE_FLG_WP			0x2
#	define TAPE_FLG_WRITE_COMPRESSION	0x4
};
#define	TAPE_STATUS_COUNT	(sizeof(struct tape_status)/sizeof(int))
#define	TAPE_STATUS		(('m'<<16) + 1)

/*
 * Constants for mt_type.  These are the same
 * for controllers compatible with the types listed.
 */
#define	MACH_MT_ISTS	0x01		/* TS-11 */
#define	MACH_MT_ISHT	0x02		/* TM03 Massbus: TE16, TU45, TU77 */
#define	MACH_MT_ISTM	0x03		/* TM11/TE10 Unibus */
#define	MACH_MT_ISMT	0x04		/* TM78/TU78 Massbus */
#define	MACH_MT_ISUT	0x05		/* SI TU-45 emulation on Unibus */
#define	MACH_MT_ISCPC	0x06		/* SUN */
#define	MACH_MT_ISAR	0x07		/* SUN */
#define	MACH_MT_ISTMSCP	0x08		/* DEC TMSCP protocol (TU81, TK50) */
#define	MACH_MT_ISCY	0x09		/* CCI Cipher */
#define	MACH_MT_ISSCSI	0x0a		/* SCSI tape (all brands) */


/*
 * Set status parameters
 */

struct tape_params {
	unsigned short	mt_operation;
	unsigned int	mt_repeat_count;
};

/* operations */
#define MACH_MTWEOF	0	/* write an end-of-file record */
#define MACH_MTFSF	1	/* forward space file */
#define MACH_MTBSF	2	/* backward space file */
#define MACH_MTFSR	3	/* forward space record */
#define MACH_MTBSR	4	/* backward space record */
#define MACH_MTREW	5	/* rewind */
#define MACH_MTOFFL	6	/* rewind and put the drive offline */
#define MACH_MTNOP	7	/* no operation, sets status only */
#define MACH_MTCACHE	8	/* enable controller cache */
#define MACH_MTNOCACHE	9	/* disable controller cache */
#define MACH_MTLOCATE	10	/* position tape to requested block number */

/*
 * U*x compatibility
 */

/* structure for MTIOCGET - mag tape get status command */

struct mach_mtget {
	short	mt_type;	/* type of magtape device */
/* the following two registers are grossly device dependent */
	short	mt_dsreg;	/* ``drive status'' register */
	short	mt_erreg;	/* ``error'' register */
/* end device-dependent registers */
	short	mt_resid;	/* residual count */
/* the following two are not yet implemented */
	unsigned long	mt_fileno;	/* file number of current position */
	unsigned long	mt_blkno;	/* block number of current position */
/* end not yet implemented */
};

typedef struct position position_t;

struct position {
	char		flags;
	char  		partition;
	char  		reserved_1;
	char  		reserved_2;
	unsigned long 	first_block;
};

typedef struct load_display load_display_t;

struct load_display{
	char		message[17];
};

/* mag tape io control commands */
#define	MACH_MTIOCTOP _MACH_IOW('m', 1, struct tape_params)/* do a mag tape op*/
#define	MACH_MTIOCGET _MACH_IOR('m', 2, struct mach_mtget) /* get tape status */
#define MACH_MTIOCIEOT _MACH_IO('m', 3)		/* ignore EOT error */
#define MACH_MTIOCEEOT _MACH_IO('m', 4)		/* enable EOT error */
#define	MACH_MTIOPOS _MACH_IOR('m', 5, struct position)/* get tape position */
#define	MACH_MTIODISPLY	_MACH_IOW('m', 6, struct load_display)/*3480 load disp*/

#endif	/* _TAPE_STATUS_H_ */
