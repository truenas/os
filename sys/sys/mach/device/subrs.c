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
 * Revision 2.11.2.1  92/09/15  17:14:43  jeffreyh
 * 	Added definitions for strcat, strchr, itoa and atoi and atoi_term
 * 	[92/09/10            stans@ssd.intel.com]
 * 
 * Revision 2.11  91/09/12  16:37:39  bohman
 * 	Added strlen() (BSD version).
 * 	[91/09/11  17:07:25  bohman]
 * 
 * Revision 2.10  91/08/24  11:56:04  af
 * 	Use BSD string functions now, which of course come with a
 * 	copyright.  Also, undef them in case some smarty..
 * 	[91/08/22            af]
 * 
 * Revision 2.9  91/07/31  17:34:27  dbg
 * 	Add strcpy.
 * 	[91/07/30  16:47:37  dbg]
 * 
 * Revision 2.8  91/05/14  16:01:30  mrt
 * 	Correcting copyright
 * 
 * Revision 2.7  91/03/16  14:43:34  rpd
 * 	Updated for new kmem_alloc interface.
 * 	[91/03/03            rpd]
 * 
 * Revision 2.6  91/02/05  17:10:17  mrt
 * 	Changed to new Mach copyright
 * 	[91/01/31  17:30:39  mrt]
 * 
 * Revision 2.5  91/01/08  15:10:02  rpd
 * 	Added continuation argument to thread_block.
 * 	[90/12/08            rpd]
 * 
 * Revision 2.4  90/05/03  15:19:27  dbg
 * 	Add compatibility routines for BSD-compatible device drivers:
 * 	sleep, wakeup, geteblk, brelse.
 * 	[90/03/14            dbg]
 * 
 * Revision 2.3  90/01/11  11:42:31  dbg
 * 	De-linted.
 * 	[89/12/15            dbg]
 * 
 * 	Add lock initialization in if_init_queues.
 * 	[89/11/30            dbg]
 * 
 * Revision 2.2  89/11/29  14:08:58  af
 * 	RCS-ed, added strncmp (needed for scsi label comparisons).
 * 	[89/10/28            af]
 * 
 */
/* CMU_ENDHIST */

/*
 *(C)UNIX System Laboratories, Inc. all or some portions of this file are
 *derived from material licensed to the University of California by
 *American Telephone and Telegraph Co. or UNIX System Laboratories,
 *Inc. and are reproduced herein with the permission of UNIX System
 *Laboratories, Inc.
 */

/* 
 * Mach Operating System
 * Copyright (c) 1993,1991,1990,1989,1988 Carnegie Mellon University
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
 * Copyright (c) 1988 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*
 * Random device subroutines and stubs.
 */

#include <vm/vm_kern.h>
#include <device/buf.h>
#include <device/if_hdr.h>
#include <device/if_ether.h>
#include <device/net_io.h>
#include <device/subrs.h>
#include <device/misc_protos.h>
#include <kern/misc_protos.h>
#include <mach/mach_server.h>

void		harderr(
			struct buf		* bp,
			char			* cp);

/*
 * Print out disk name and block number for hard disk errors.
 */
void
harderr(
	struct buf	*bp,
	char		* cp)
{
	printf("%s%d%c: hard error sn%d ",
	       cp,
	       minor(bp->b_dev) >> 3,
	       'a' + (minor(bp->b_dev) & 0x7),
	       bp->b_blkno);
}

/* String routines, from CMU */
#ifdef	strcpy
#undef strcmp
#undef strncmp
#undef strcpy
#undef strncpy
#undef strlen
#endif

/*
 * Abstract:
 *      strcmp (s1, s2) compares the strings "s1" and "s2".
 *      It returns 0 if the strings are identical. It returns
 *      > 0 if the first character that differs in the two strings
 *      is larger in s1 than in s2 or if s1 is longer than s2 and
 *      the contents are identical up to the length of s2.
 *      It returns < 0 if the first differing character is smaller
 *      in s1 than in s2 or if s1 is shorter than s2 and the
 *      contents are identical upto the length of s1.
 */

int
strcmp(
	register const char *s1,
	register const char *s2)
{
	register unsigned int a, b;

	do {
		a = *s1++;
		b = *s2++;
		if (a != b)
			return a-b;     /* includes case when
					   'a' is zero and 'b' is not zero
					   or vice versa */
	} while (a != '\0');

	return 0;       /* both are zero */
}

/*
 * Abstract:
 *      strncmp (s1, s2, n) compares the strings "s1" and "s2"
 *      in exactly the same way as strcmp does.  Except the
 *      comparison runs for at most "n" characters.
 */

int
strncmp(
	register const char *s1,
	register const char *s2,
	size_t n)
{
	register unsigned int a, b;

	while (n != 0) {
		a = *s1++;
		b = *s2++;
		if (a != b)
			return a-b;     /* includes case when
					   'a' is zero and 'b' is not zero
					   or vice versa */
		if (a == '\0')
			return 0;       /* both are zero */
		n--;
	}

	return 0;
}

/*
 * Abstract:
 *      strcpy copies the contents of the string "from" including
 *      the null terminator to the string "to". A pointer to "to"
 *      is returned.
 */

char *
strcpy(
	register char *to,
	register const char *from)
{
	register char *ret = to;

	while ((*to++ = *from++) != '\0')
		continue;

	return ret;
}


/*
 * Abstract:
 *      strncpy copies "count" characters from the "from" string to
 *      the "to" string. If "from" contains less than "count" characters
 *      "to" will be padded with null characters until exactly "count"
 *      characters have been written. The return value is a pointer
 *      to the "to" string.
 */

char *
strncpy(
	char *s1, 
	const char *s2,
	size_t n)
{
	char *os1 = s1;
	unsigned long i;

	for (i = 0; i < n;)
		if ((*s1++ = *s2++) == '\0')
			for (i++; i < n; i++)
				*s1++ = '\0';
		else
			i++;
	return (os1);
}


#if !defined(__alpha)

/*
 * Abstract:
 *      strlen returns the number of characters in "string" preceeding
 *      the terminating null character.
 */

size_t
strlen(
	register const char *string)
{
	register const char *ret = string;

	while (*string++ != '\0')
		continue;
	return string - 1 - ret;
}
#endif /* !defined(__alpha) */

/*
 * atoi:
 *
 *      This function converts an ascii string into an integer.
 *
 * input        : string
 * output       : a number
 */

int
atoi(
	u_char  *cp)
{
	int     number;

	for (number = 0; ('0' <= *cp) && (*cp <= '9'); cp++)
		number = (number * 10) + (*cp - '0');

	return( number );
}

/*
 * convert an ASCII string (decimal radix) to an integer
 * inputs:
 *	p	string pointer.
 *	t	char **, return a pointer to the cahr which terminates the
 *		numeric string.
 * returns:
 *	integer value of the numeric string.
 * side effect:
 *	pointer to terminating char.
 */

int
atoi_term(
	char	*p,	/* IN */
	char	**t)	/* OUT */
{
	register int n;
	register int f;

	n = 0;
	f = 0;
	for(;;p++) {
		switch(*p) {
		case ' ':
		case '\t':
			continue;
		case '-':
			f++;
		case '+':
			p++;
		}
		break;
	}
	while(*p >= '0' && *p <= '9')
		n = n*10 + *p++ - '0';

	/* return pointer to terminating character */
	if ( t )
		*t = p;

	return(f? -n: n);
}

/*
 * convert an integer to an ASCII string.
 * inputs:
 *	num	integer to be converted
 *	str	string pointer.
 *
 * outputs:
 *	pointer to string start.
 */

char *
itoa(
	int	num,
	char	*str)
{
	char    digits[11];
	register char *dp;
	register char *cp = str;

	if (num == 0) {
	    *cp++ = '0';
	}
	else {
	    dp = digits;
	    while (num) {
		*dp++ = '0' + num % 10;
		num /= 10;
	    }
	    while (dp != digits) {
		*cp++ = *--dp;
	    }
	}
	*cp++ = '\0';

	return str;
}



/*
 * Ethernet support routines.
 */
u_char	etherbroadcastaddr[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

/*
 * Convert Ethernet address to printable (loggable) representation.
 */
char *
ether_sprintf(
	u_char		*ap)
{
	register int i;
	static char etherbuf[18];
	register char *cp = etherbuf;
	static char digits[] = "0123456789abcdef";

	for (i = 0; i < 6; i++) {
		*cp++ = digits[*ap >> 4];
		*cp++ = digits[*ap++ & 0xf];
		*cp++ = ':';
	}
	*--cp = 0;
	return (etherbuf);
}

/*
 * Initialize send and receive queues on an interface.
 */
void
if_init_queues(
	struct ifnet *ifp)
{
	IFQ_INIT(&ifp->if_snd);
	queue_init(&ifp->if_rcv_port_list);
	mutex_init(&ifp->if_rcv_port_list_lock, ETAP_NET_RCV_PLIST);
}


/*
 * Compatibility with BSD device drivers.
 */
/*ARGSUSED*/
void
sleep(
	event_t		channel,
	int		priority)
{
	assert_wait(channel, FALSE);	/* not interruptible XXX */
	thread_block((void (*)(void)) 0);
}

void
wakeup(
	event_t		channel)
{
	thread_wakeup(channel);
}

struct io_req *
geteblk(
	io_buf_len_t	size)
{
	register io_req_t	ior;

	io_req_alloc(ior);
	ior->io_device = (mach_device_t)0;
	ior->io_unit = 0;
	ior->io_op = 0;
	ior->io_mode = 0;
	ior->io_recnum = 0;
	ior->io_count = size;
	ior->io_residual = 0;
	ior->io_error = 0;
	ior->io_copy	= VM_MAP_COPY_NULL;

	size = round_page(size);
	ior->io_alloc_size = size;
	if (kmem_alloc(kernel_map, (vm_offset_t *)&ior->io_data, (vm_size_t)size) != KERN_SUCCESS)
	    panic("geteblk");

	return (ior);
}

void
brelse(
	struct buf *bp)
{
	register io_req_t	ior = bp;

	(void) kmem_free(kernel_map,
			(vm_offset_t) ior->io_data,
			(vm_size_t) ior->io_alloc_size);
	io_req_free(ior);
}

char *
strcat(
	register char *dest,
	register const char *src)
{
	char *old = dest;

	while (*dest)
		++dest;
	while (*dest++ = *src++)
		;
	return (old);
}


/************************************************************************/
/*									*/
/* drvr_register_shutdown						*/
/*		       register calback routine to be called when	*/
/*			system performs shutdown.			*/
/*									*/
/* SYNOPSIS                                                             */
/*	void drvr_register_shutdown(callback, param, flags)		*/
/*									*/
/* PARAMETERS                                                           */
/*	callback Driver routine to be called at system shutdown		*/
/*	param    Parameter passed to driver shutdown routine		*/
/*	flags	 flags field						*/
/*									*/
/* DESCRIPTION                                                          */
/*	This function registers or de-registers a shutdown routine	*/
/*	for the calling driver. This routine is called just prior	*/
/*	to system halt. This allows drivers to gracefully quiesce	*/
/*	its hardware.							*/
/*									*/
/* RETURN VALUES                                                        */
/*	None								*/
/*									*/
/************************************************************************/

struct drvr_shut *drvr_shut_head = 0;

void
drvr_register_shutdown(void (*callback)(caddr_t), caddr_t param, int flags)
{
	struct drvr_shut *drvr_shut, *cur_dvr, *prev_dvr;

/*
 *	Driver shutdown registration
 *
 *	Allocate data structure and put onto linked list.
 *	This list is processed at shutdown time.
 */
	if (flags & DRVR_REGISTER) {
		drvr_shut = (struct drvr_shut *)kalloc(sizeof(struct drvr_shut));
		if (drvr_shut == NULL)
			panic("Unable to register driver shutdown routine");
		drvr_shut->callback = callback;
		drvr_shut->param = param;
		drvr_shut->next = drvr_shut_head;
		drvr_shut_head = drvr_shut;
	}
/*
 *	Driver shutdown unregister
 *
 *	Remove entry from linked list that matches the paramater and routine
 *	in this call.
 */
	else if (flags & DRVR_UNREGISTER) {
		prev_dvr = 0;
		cur_dvr = drvr_shut_head;
		while (cur_dvr) {
			if ((cur_dvr->callback == callback) &&
			    (cur_dvr->param == param)) {
				if (prev_dvr == 0)
					drvr_shut_head = cur_dvr->next;
				else
					prev_dvr->next = cur_dvr->next;
				kfree((vm_offset_t)cur_dvr, sizeof(struct drvr_shut));
				break;
			}
			prev_dvr = cur_dvr;
			cur_dvr = cur_dvr->next;
		}
	}
	else {
		panic("Invalid flag parameter for drvr_register_shutdown");
	}
}



/************************************************************************/
/*									*/
/* drvr_shutdown							*/
/*		       call driver shutdown routines			*/
/*									*/
/* SYNOPSIS                                                             */
/*	drvr_shutdown()							*/
/*									*/
/* PARAMETERS                                                           */
/*									*/
/* DESCRIPTION                                                          */
/*	This routine calls the driver shutdown routines that have been	*/
/*	registered by calls to drvr_register_shutdown. The global	*/
/*	drvr_shutdown_head points to a linked list of driver routines	*/
/*	to call.							*/
/*									*/
/* RETURN VALUES                                                        */
/*									*/
/************************************************************************/

void drvr_shutdown(void)
{
	struct drvr_shut *cur_dvr, *prev_dvr;

	cur_dvr = drvr_shut_head;
	while (cur_dvr) {
		(*cur_dvr->callback)(cur_dvr->param);
		prev_dvr = cur_dvr;
		cur_dvr = cur_dvr->next;
		kfree((vm_offset_t)prev_dvr, sizeof(struct drvr_shut));
	}
	drvr_shut_head = 0;
}
