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
 * Revision 2.9  91/08/28  11:11:05  jsb
 * 	From rpd: documented what (*max_count)() does in block_io.
 * 	[91/08/22  15:30:18  jsb]
 * 
 * 	Unlimited size is for write only.
 * 	[91/08/12  17:44:31  dlb]
 * 
 * 	Support synchronous waits for writes with vm continuations.
 * 	Remove limit on size of operations.
 * 	[91/08/12  17:22:59  dlb]
 * 
 * Revision 2.8  91/07/30  15:45:38  rvb
 * 	From rpd: temp(!!) jack up MAXPHYS to 256K to make newfs
 * 	work.  rpd is working on having blkio loop, doing NBYTES
 * 	per interation, rather than having a fixed max size.
 * 	[91/07/30            rvb]
 * 
 * Revision 2.7  91/05/14  15:38:36  mrt
 * 	Correcting copyright
 * 
 * Revision 2.6  91/05/13  06:02:15  af
 * 	Do not trim io_count to DEV_BSIZE, we know how to cope
 * 	and this is used for tapes also [which might be block
 * 	devices or not, depends].
 * 	[91/05/12  15:48:48  af]
 * 
 * Revision 2.5  91/02/05  17:07:46  mrt
 * 	Changed to new Mach copyright
 * 	[91/01/31  17:26:03  mrt]
 * 
 * Revision 2.4  90/08/27  21:54:15  dbg
 * 	Add disk sort routine.
 * 	[90/06/20            dbg]
 * 
 * Revision 2.3  89/09/08  11:22:56  dbg
 * 	Converted to run in kernel task.
 * 	[89/07/27            dbg]
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
 *	Author: David B. Golub, Carnegie Mellon University
 *	Date: 	7/89
 *
 * 	Block IO driven from generic kernel IO interface.
 */
#include <platforms.h>
#include <chained_ios.h>
#include <mach/kern_return.h>

#include <device/param.h>
#include <device/device_types.h>
#include <device/io_req.h>
#include <device/ds_routines.h>
#include <device/buf.h>

int higher_priority(
	io_req_t	ior,
	io_req_t	prev,
	io_req_t	next);

int sawtooth = 1;	/* bootmagic settable DISKSORT_SAWTOOTH */
int optimize_read = 0;	/* bootmagic settable DISKSORT_OPTIMIZE_READ */

io_return_t
block_io(
	void			(*strat)(io_req_t	ior),
	void			(*max_count)(io_req_t	ior),
	io_req_t		ior)
{
	register kern_return_t	rc;
	boolean_t		wait = FALSE;

	/*
	 * Make sure the size is not too large by letting max_count
	 * change io_count.  If we are doing a write, then io_alloc_size
	 * preserves the original io_count.
	 */
	(*max_count)(ior);

	/*
	 * If reading, allocate memory.  If writing, wire
	 * down the incoming memory.
	 */
	if (ior->io_op & IO_READ)
	    rc = device_read_alloc(ior, (vm_size_t)ior->io_count);
	else
	    rc = device_write_get(ior, &wait);

	if (rc != KERN_SUCCESS)
	    return (rc);

	if (!wait && (ior->io_op & IO_SYNC))
	    wait = TRUE;

	/*
	 * Queue the operation for the device.
	 */
	(*strat)(ior);

	/*
	 * The io is now queued.  Wait for it if needed.
	 */
	if (wait) {
		iowait(ior);
		return(D_SUCCESS);
	}

	return (D_IO_QUEUED);
}

/*
 * 'standard' max_count routine.  VM continuations mean that this
 * code can cope with arbitrarily-sized write operations (they won't be
 * atomic, but any caller that cares will do the op synchronously).
 */
#define MAX_PHYS        (256 * 1024)

void
minphys(
	io_req_t		ior)
{
	if ((ior->io_op & (IO_WRITE | IO_READ | IO_OPEN)) == IO_WRITE)
	    return;

        if (ior->io_count > MAX_PHYS)
            ior->io_count = MAX_PHYS;
}

/*
 * Dummy routine placed in device switch entries to indicate that
 * block device may be mapped.
 */

vm_offset_t
block_io_mmap(dev_t dev, vm_offset_t addr, vm_prot_t prot)
{
	return (0);
}


/*
 * Disk sort routine.
 *
 * We order the disk request chain so that the disk head will sweep
 * back and forth across the disk.  The chain is divided into two
 * pieces, with requests ordered in opposite directions.  Assume that
 * the first part of the chain holds increasing cylinder numbers.
 * If a new request has a higher cylinder number than the head of
 * the chain, the disk head has not yet reached it; the new request
 * can go in the first part of the chain.  If the new request has
 * a lower cylinder number, the disk head has already passed it and
 * must catch it on the way back; so the new request goes in the
 * second (descending) part of the chain.
 * When all of the requests in the ascending portion are filled,
 * the descending chain becomes the first chain, and requests above
 * the first now go in the second part of the chain (ascending).
 */

#define	io_cylinder	io_residual
				/* Disk drivers put cylinder or block here */
#define	h_head		io_next
#define	h_tail		io_prev
				/* IORs are chained here */

#define ABS(X)	((X) < 0 ? -(X) : (X))

int
higher_priority(
	io_req_t	ior,
	io_req_t	prev,
	io_req_t	next)
{
	if (!optimize_read)
		return(ABS(prev->io_cylinder - ior->io_cylinder) <
			ABS(prev->io_cylinder - next->io_cylinder));
	else {
		/* optimize read ahead of write */
		if (ior->io_op & IO_READ)
			return ((next->io_op & IO_WRITE) ||
				(ABS(prev->io_cylinder - ior->io_cylinder) <
					ABS(prev->io_cylinder - next->io_cylinder)));
		else return ((next->io_op & IO_WRITE) &&
				(ABS(prev->io_cylinder - ior->io_cylinder) <
					ABS(prev->io_cylinder - next->io_cylinder)));
	}
}

void
disksort(
	io_req_t	head,	/* (sort of) */
	io_req_t	ior)
{
	register integer_t	cylinder = ior->io_cylinder;
	register io_req_t	next, prev;

	next = head->h_head;
	if (next == 0) {
	    head->h_head = ior;
	    head->h_tail = ior;
	    ior->io_next = 0;
	    ior->io_prev = 0;
	    return;
	}

	do {
	    prev = next;
	    next = prev->io_next;
	} while (next != 0 && prev->io_cylinder == next->io_cylinder);

	if (next == 0) {
	    prev->io_next = ior;
	    head->h_tail = ior;
	    ior->io_next = 0;
	    ior->io_prev = prev;
	    return;
	}


       if (sawtooth) {
	if (prev->io_cylinder < next->io_cylinder) {
	    /*
	     * Ascending list first.
	     */
	    if (prev->io_cylinder <= cylinder) {
		/*
		 * Insert in ascending list.
		 */
		while (next != 0 &&
			next->io_cylinder <= cylinder &&
			prev->io_cylinder <= next->io_cylinder)
		{
		    prev = next;
		    next = prev->io_next;
		}
	    }
	    else {
		/*
		 * Insert in descending list
		 */
		do {
		    prev = next;
		    next = prev->io_next;
		} while (next != 0 &&
			prev->io_cylinder <= next->io_cylinder);

		while (next != 0 &&
			next->io_cylinder >= cylinder)
		{
		    prev = next;
		    next = prev->io_next;
		}
	    }
	}
	else {
	    /*
	     * Descending first.
	     */
	    if (prev->io_cylinder >= cylinder) {
		/*
		 * Insert in descending list.
		 */
		while (next != 0 &&
			next->io_cylinder >= cylinder &&
			prev->io_cylinder >= next->io_cylinder)
		{
		    prev = next;
		    next = prev->io_next;
		}
	    }
	    else {
		/*
		 * Insert in ascending list
		 */
		do {
		    prev = next;
		    next = prev->io_next;
		} while (next != 0 &&
			prev->io_cylinder >= next->io_cylinder);
		while (next != 0 &&
			next->io_cylinder <= cylinder)
		{
		    prev = next;
		    next = prev->io_next;
		}
	    }
	}
       } else {
	do {
	    if (higher_priority(ior, prev, next))
		/* schedule new request ahead of next */
		break;
	    else {
		prev = next;
		next = next->io_next;
	    }
	} while (next);
       }   

    /*
     * Insert between prev and next.
     */
    prev->io_next = ior;
    ior->io_next = next;
    ior->io_prev = prev;
    if (next == 0) {
      	/* At tail of list. */
      	head->h_tail = ior;
    } else 
      	next->io_prev = ior;
}

#if CHAINED_IOS

#ifdef	i386
/* there's still a bug somewhere */
boolean_t	chained_ios_enabled = 0;
#else	/* i386 */
boolean_t	chained_ios_enabled = 1;
#endif	/* i386 */


#if	MACH_KDB
int		chained_reads;
int		chained_writes;
#endif	/* MACH_KDB */

/*
 * chain_io_reqs(ior1, ior2, head)
 *
 * Merge two IO requets for which records are consecutive
 * into a single IO request. Mostly used for disk IO.
 *	
 * Use the io_link/io_rlink for this purpose.
 *	io_link links the IO requests into ascending order
 *	io_rlink for the head points to last IO in the chain. 
 *	io_rlink is null for other elements of the chain.
 * Only the head stays in the driver queue.
 * Counts are updated to reflect total counts of chained IO requests 
 *	(self + right hand linked I/Os)
 */

void 
chain_io_reqs(
	io_req_t	ior1,
	io_req_t	ior2, 
	io_req_t	queue_head)
{
  	io_req_t 	head, next;

	if (!chained_ios_enabled)
		return;

#if	MACH_KDB
	if ((ior1->io_op & (IO_READ|IO_WRITE)) == IO_READ)
		chained_reads++;
	else
	  	chained_writes++;
#endif	/* MACH_KDB */

	if (ior1->io_recnum < ior2->io_recnum) {
		head = ior1;
		next = ior2;
	} else {
		head = ior2;
		next = ior1;
	}
	
	if (head->io_op & IO_CHAINED) {
		io_req_t ior;

		assert(head->io_link);

		ior = head;
		/* Already existing chained IOs at head.
		 * insert at tail, adjust io_count & io_seg_count 
		 */
		do {
			assert(ior->io_op & IO_CHAINED);
			ior->io_count += next->io_count;
			ior->io_seg_count +=  next->io_seg_count;
		} while (ior = ior->io_link);

		head->io_rlink->io_link = next;
		if (next->io_op & IO_CHAINED) {
			/*
			 * Chaining two chains.
			 */
		  	head->io_rlink = next->io_rlink;
			next->io_rlink = 0;
		} else {
			next->io_op |= IO_CHAINED;
			head->io_rlink = next;
			next->io_link = 0;
		}
	} else if (next->io_op & IO_CHAINED) {
		assert(next->io_link);

		/* Already existing chained IOs for next elt.
		 * insert at head, adjust io io_count & io_seg_count
		 */
		
		head->io_op |= IO_CHAINED;
		head->io_count += next->io_count;
		head->io_seg_count += next->io_seg_count;
		head->io_link = next;
		head->io_rlink = next->io_rlink;
		next->io_rlink = 0;
	} else {
		/* No existing chained IOs.
		 * chain, adjust io_count & io_seg_count
		 */
		head->io_op |= IO_CHAINED;
		next->io_op |= IO_CHAINED;
		head->io_count += next->io_count;
		head->io_seg_count += next->io_seg_count;
		head->io_link = next;
		head->io_rlink = next;
		next->io_link = 0;
	}
		
	/* unlink from sorted q */
	if (next->io_next)
		next->io_next->io_prev = next->io_prev;
	if (next->io_prev == 0)
		queue_head->io_next = next->io_next;
	else
	        next->io_prev->io_next = next->io_next;
	next->io_next = 0;
}

/*
 * chained_iodone(io_req_t ior)
 * io_done routine for chained io requests.
 * Walk down the chain to perform io_done on individual requets.
 */

void
chained_iodone(
        io_req_t        ior)
{
  	io_req_t	next;
  	do {
		assert(ior->io_op & IO_CHAINED);
		if (next = ior->io_link) {
			ior->io_count -= next->io_count;
			ior->io_seg_count = 0;
			next->io_error = ior->io_error;
			next->io_op |= (ior->io_op & IO_ERROR);
			if (next->io_count >= ior->io_residual) {
				next->io_residual = ior->io_residual;
				ior->io_residual = 0;
			} else {
				next->io_residual = next->io_count;
				ior->io_residual -= next->io_residual;
			}
		}
		iodone(ior);
	} while (ior = next);
}

/*
 * Break I/O chain: Remove first request from chain.
 * Leave both in ioqueue. Used for IO error processing.
 */

void
split_io_reqs(
	io_req_t	ior)
{
  	io_req_t 	next = ior->io_link;

	assert(ior->io_op & IO_CHAINED);
	assert(next && (next->io_op & IO_CHAINED));

	/* break first req from chain, leave it in io queue */

	ior->io_count -= next->io_count;
	ior->io_seg_count = 0;
	ior->io_op &= ~IO_CHAINED;
	next->io_rlink = ior->io_rlink;
	ior->io_link = 0;
	ior->io_rlink = 0;

	/* Insert rest of chain in ioqueue. Don't need to sort */

	next->io_next = ior->io_next;
	if (next->io_next)
		next->io_next->io_prev = next;
	next->io_prev = ior;
	ior->io_next = next;

	if (!next->io_link) {
   		next->io_op &= ~IO_CHAINED;
		next->io_rlink = 0;
	}
	
}

#endif	/* CHAINED_IOS */
