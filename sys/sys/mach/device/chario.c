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
 * Revision 2.13.3.2  92/04/30  11:49:10  bernadat
 * 	30-Apr-92  emcmanus at gr.osf.org
 * 	Disabled out-of-band notification of inpuflush completion
 * 	due to server problems
 * 	16-Apr-92  emcmanus at gr.osf.org 
 * 	Increase tty_inq_size to 512.  Clear input and output queues on open,
 * 	in case stuff left over from last session.  Send out-of-band
 * 	notification when an input flush has completed.  Added conditional
 * 	debugging code to show tty structures.
 * 
 * 	17-Mar-92  emcmanus at gr.of.org
 * 	Removed incorrect attempt to restart stopped output in ttydrain()
 * 	[92/04/22  09:59:50  bernadat]
 * 
 * Revision 2.13.3.1  92/03/28  10:04:20  jeffreyh
 * 	Increase tty_inq_size from 64 to 256, reducing lost chars at
 * 	higher speeds.  Change Carrier Detect logic on read so that
 * 	status is only checked when there are no chars already buffered.
 * 	Wait for any ongoing output drain to finish before stopping in
 * 	TTY_STOP.
 * 	[92/03/20            emcmanus]
 * 	04-Mar-92  emcmanus at gr.osf.org
 * 		Move common code from char_read and char_read_done into new function
 * 		do_char_read.
 * 		Support for out-of-band events (BREAK and wrong parity):
 * 		quoting in tty input queues, new TF_OUT_OF_BAND bit to enable
 * 		D_OUT_OF_BAND return from device_read, new TTY_OUT_OF_BAND
 * 		device_get_status request.
 * 		Allow TF_HUPCLS to be cleared when it has been set: previously it
 * 		stuck as long as the device was open.
 * 		Output queues can be drained using new TTY_DRAIN device_set_status,
 * 		or as called from device driver.
 * 	[92/03/16  09:42:16  jeffreyh]
 * 
 * Revision 2.13  91/09/12  16:36:51  bohman
 * 	Added missing clear of TS_TTSTOP in char_write().
 * 	TS_INIT belongs in t_state, not t_flags.
 * 	[91/09/11  17:04:18  bohman]
 * 
 * Revision 2.12  91/08/28  11:11:08  jsb
 * 	Fixed char_write to check vm_map_copyout's return code.
 * 	[91/08/03            rpd]
 * 
 * Revision 2.11  91/08/24  11:55:34  af
 * 	Spl definitions.
 * 	[91/08/02  02:44:21  af]
 * 
 * Revision 2.10  91/05/14  15:39:09  mrt
 * 	Correcting copyright
 * 
 * Revision 2.9  91/02/05  17:07:55  mrt
 * 	Changed to new Mach copyright
 * 	[91/01/31  17:26:20  mrt]
 * 
 * Revision 2.8  90/08/27  21:54:21  dbg
 * 	Fixed type declaration for char_open.
 * 	[90/07/16            dbg]
 * 
 * 	Added call to cb_alloc in ttychars..
 * 	[90/07/09            dbg]
 * 
 * Revision 2.7  90/06/02  14:47:02  rpd
 * 	Updated for new IPC.  Purged MACH_XP_FPD.
 * 	[90/03/26  21:42:42  rpd]
 * 
 * Revision 2.6  90/01/11  11:41:39  dbg
 * 	Fix test on 'i' (clist exhausted) in char_write.
 * 	Document what operations need locking, and which must be
 * 	serialized if the device driver only runs on one CPU.
 * 	[89/11/30            dbg]
 * 
 * Revision 2.5  89/11/29  14:08:50  af
 * 	char_write wasn't calling b_to_q() with the right char count.
 * 	[89/11/11            af]
 * 	Marked tty as initialized in ttychars.
 * 	[89/11/03  16:57:39  af]
 * 
 * Revision 2.4  89/09/08  11:23:01  dbg
 * 	Add in-band write check to char_write.
 * 	[89/08/30            dbg]
 * 
 * 	Make char_write copy data directly from user instead of wiring
 * 	down data buffer first.
 * 	[89/08/24            dbg]
 * 
 * 	Convert to run in kernel task.
 * 	[89/07/27            dbg]
 * 
 * Revision 2.3  89/08/31  16:17:20  rwd
 * 	Don't assume adequate spl when inserting/deleting ior's from
 * 	delay queues.
 * 	[89/08/31            rwd]
 * 
 * Revision 2.2  89/08/05  16:04:35  rwd
 * 	Added tty_queueempty for sun console input polling.
 * 	Added ttyoutput for sundev/kbd.c.  Allow inband data.
 * 	[89/06/02            rwd]
 * 
 * 18-May-89  David Golub (dbg) at Carnegie-Mellon University
 *	Check for uninitialized TTY queues in close/port_death.
 *
 * 12-Apr-89  David Golub (dbg) at Carnegie-Mellon University
 *	Added port_death routines.
 *
 * 24-Aug-88  David Golub (dbg) at Carnegie-Mellon University
 *	Created.
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
 *	Date: 	8/88
 */
/*
 * 	TTY io.
 * 	Compatibility with old TTY device drivers.
 */

#include <mach/mig_errors.h>
#include <mach/vm_param.h>
#include <mach/mach_server.h>

#include <machine/machparam.h>		/* spl definitions */

#include <ipc/ipc_port.h>

#include <kern/lock.h>
#include <kern/queue.h>
#include <kern/time_out.h>
#include <kern/spl.h>
#include <kern/misc_protos.h>

#include <vm/vm_map.h>
#include <vm/vm_kern.h>

#include <device/device_types.h>
#include <device/io_req.h>
#include <device/ds_routines.h>
#include <device/device_reply.h>

#include <device/tty.h>
#include <machine/machparam.h>	/* for spl */

#define NSPEEDS_COMPAT	16

/* Forward */

extern void		queue_delayed_reply(
				queue_t		qh,
				io_req_t	ior,
				boolean_t	(*io_done)(io_req_t ior));
extern boolean_t	char_open_done(
				io_req_t	ior);
extern boolean_t	tty_close_open_reply(
				io_req_t	ior);
extern boolean_t	char_write_done(
				io_req_t	ior);
extern boolean_t	tty_close_write_reply(
				io_req_t	ior);
extern kern_return_t	do_char_read(
				struct tty	* tp,
				io_req_t	ior);
extern int		char_read_quoted(
				struct tty	* tp,
				char		* data,
				int		count);
extern boolean_t	char_read_done(
				io_req_t	ior);
extern boolean_t	tty_close_read_reply(
				io_req_t	ior);
extern boolean_t	tty_queue_clean(
				queue_t		q,
				ipc_port_t	port,
				boolean_t	(*routine)(io_req_t ior));
extern void		ttyquoted(
				int		qc,
				int		qarg,
				struct tty	* tp);
extern void		ttypush(
				struct tty	* tp);
extern void		tty_queue_filled_reads(
				struct tty	*tp);
extern int		TTHIWAT(
				struct tty	* tp);

int compat_to_baud_rate[NSPEEDS_COMPAT] = {
	0,
	50,
	75,
	110,
	134,
	150,
	200,
	300,
	600,
	1200,
	1800,
	2400,
	4800,
	9600,
	19200,
	38400
};

struct baud_rate_info baud_rate_to_compat[] = {
	{	0,	0	},
	{	50,	1	},
	{	75,	2	},
	{	110,	3	},
	{	134,	4	},
	{	150,	5	},
	{	200,	6	},
	{	300,	7	},
	{	600,	8	},
	{	1200,	9	},
	{	1800,	10	},
	{	2400,	11	},
	{	4800,	12	},
	{	9600,	13	},
	{	19200,	14	},
	{	38400,	15	},
	{	-1,	-1	}
};

#define trunc_at(v, bound)	(((v) / (bound)) * (bound))
#define fraction_of(v, num, den)	(((v) * (num)) / (den))

/*
 * TTOUTQSIZE should be larger than TTMAXHIWAT (though it needn't
 * be much larger).  The rationale for these particular values
 * has not come down to us...
 */
#define TTTRUNCAT	50
#define hiwattrunc(v)	trunc_at((v), TTTRUNCAT)

#ifdef HPUXCOMPAT
#define TTINQSIZE	2048	/* XXX */
#else
#define TTINQSIZE	256	/* Should be at least MTU for PPP (1506) */
                                /* the smaller the buffer, the less time we block */
#endif /* HPUXCOMPAT */
#define TTOUTQSIZE	2048	/* XXX */

#define TTMAXHIWAT	hiwattrunc(TTOUTQSIZE)
#define hiwatfrac(num, den) \
	hiwattrunc(fraction_of(TTMAXHIWAT, num, den))

struct baud_rate_info tthiwat[] = {
	{	0,	hiwatfrac(1, 20)	},
	{	50,	hiwatfrac(1, 20)	},
	{	75,	hiwatfrac(1, 20)	},
	{	110,	hiwatfrac(1, 20)	},
	{	134,	hiwatfrac(1, 20)	},
	{	150,	hiwatfrac(1, 20)	},
	{	200,	hiwatfrac(1, 20)	},
	{	300,	hiwatfrac(1, 10)	},
	{	600,	hiwatfrac(1, 10)	},
	{	1200,	hiwatfrac(1, 5)	},
	{	1800,	hiwatfrac(1, 5)	},
	{	2400,	hiwatfrac(1, 5)	},
	{	4800,	hiwatfrac(1, 3)	},
	{	9600,	hiwatfrac(1, 3)	},
	{	14400,	hiwatfrac(1, 3)	},
	{	19200,	hiwatfrac(2, 3)	},
	{	38400,	hiwatfrac(1, 1)	},
	{	57600,	hiwatfrac(1, 1)	},
	{	115200,	hiwatfrac(1, 1)	},
	{	230400,	hiwatfrac(1, 1)	},
	{	-1,	-1 }
};

struct baud_rate_info ttlowat[] = {
	{	0,	30	},
	{	50,	30	},
	{	75,	30	},
	{	110,	30	},
	{	134,	30	},
	{	150,	30	},
	{	200,	30	},
	{	300,	50	},
	{	600,	50	},
	{	1200,	120	},
	{	1800,	120	},
	{	2400,	120	},
	{	4800,	125	},
	{	9600,	125	},
	{	14400,	125	},
	{	19200,	125	},
	{	38400,	125	},
	{	57600,	125	},
	{	115200,	125	},
	{	230400,	125	},
	{	-1,	-1}
};

/*
 * Fake 'line discipline' switch for the benefit of old code
 * that wants to call through it.
 */
struct ldisc_switch	linesw[] = {
	{
	    char_read,
	    char_write,
	    ttyinput,
	    ttymodem,
	    0	/* only called if t_line != 0 */
	}
};

/*
 * Sizes for input and output circular buffers.
 */
int	tty_inq_size = TTINQSIZE;
int	tty_outq_size = TTOUTQSIZE;	/* Should be larger than max hi wat */

int
baud_rate_get_info(int baud_rate, baud_rate_info_t info)
{
	int i;

	for (i = 0; info[i].br_rate >= 0; i++)
		if (info[i].br_rate == baud_rate)
			return(info[i].br_info);
	return(-1);
}

int
TTLOWAT(struct tty *tp)
{
	int ospeed = tp->t_ospeed;

	if (ospeed < 0)
		return -1;
	if (ospeed < NSPEEDS_COMPAT)
		ospeed = compat_to_baud_rate[ospeed];
	return(baud_rate_get_info(ospeed, ttlowat));
}

int
TTHIWAT(struct tty *tp)
{
	int ospeed = tp->t_ospeed;

	if (ospeed < 0)
		return -1;
	if (ospeed < NSPEEDS_COMPAT)
		ospeed = compat_to_baud_rate[ospeed];
	return(baud_rate_get_info(ospeed, tthiwat));
}

/*
 * Open TTY, waiting for CARR_ON.
 * No locks may be held.
 * May run on any CPU.
 */

io_return_t
char_open(
	dev_t		dev,
	struct tty	*tp,
	dev_mode_t	mode,
	io_req_t	ior)
{
	spl_t		s;

	s = spltty();
	simple_lock(&tp->t_lock);

	tp->t_dev = dev;
	tp->t_hiwater = TTHIWAT(tp);
	tp->t_lowater = TTLOWAT(tp);

	if (tp->t_mctl)
		(*tp->t_mctl)(tp, TM_DTR, DMSET);

	while (getc(&tp->t_inq) >= 0)
	    continue;
	while (getc(&tp->t_outq) >= 0)
	    continue;

	if (mode & D_NODELAY)
		tp->t_state |= TS_ONDELAY;

	if ((tp->t_state & TS_CARR_ON) == 0) {
		tp->t_state |= TS_WOPEN;

		ior->io_dev_ptr = (char *)tp;

		queue_delayed_reply(&tp->t_delayed_open, ior, char_open_done);
		simple_unlock(&tp->t_lock);
		splx(s);
		return (D_IO_QUEUED);
	}
	tp->t_state |= TS_ISOPEN;
	if (tp->t_mctl)
		(*tp->t_mctl)(tp, TM_RTS, DMBIS);

	simple_unlock(&tp->t_lock);
	splx(s);
	return (D_SUCCESS);
}

/*
 * Retry wait for CARR_ON for open.
 * No locks may be held.
 * May run on any CPU.
 */
boolean_t
char_open_done(
	io_req_t	ior)
{
	struct tty *tp = (struct tty *)ior->io_dev_ptr;
	spl_t		s = spltty();

	simple_lock(&tp->t_lock);
	if ((tp->t_state & TS_CARR_ON) == 0) {
	    queue_delayed_reply(&tp->t_delayed_open, ior, char_open_done);
	    simple_unlock(&tp->t_lock);
	    splx(s);
	    return (FALSE);
	}

	tp->t_state &= ~TS_WOPEN;
	tp->t_state |= TS_ISOPEN;
	if (tp->t_mctl)
		(*tp->t_mctl)(tp, TM_RTS, DMBIS);

	simple_unlock(&tp->t_lock);
	splx(s);

	ior->io_error = D_SUCCESS;
	(void) ds_open_done(ior);
	return (TRUE);
}

boolean_t
tty_close_open_reply(
	io_req_t	ior)
{
	ior->io_error = D_DEVICE_DOWN;
	(void) ds_open_done(ior);
	return (TRUE);
}


/*
 * Close the tty.
 * Tty must be locked (at spltty).
 * May run on any CPU.
 */
void
ttyclose(
	struct tty		*tp)
{
	io_req_t		ior;

	tp->t_outofband = TOOB_NO_EVENT;
	tp->t_outofbandarg = 0;
	tp->t_nquoted = 0;
	tp->t_breakc = 0;
	tp->t_flags = 0;
	tp->t_ispeed = 0;
	tp->t_ospeed = 0;

	/*
	 * Flush the read and write queues.  Signal
	 * the open queue so that those waiting for open
	 * to complete will see that the tty is closed.
	 */
	while ((ior = (io_req_t)dequeue_head(&tp->t_delayed_read)) != 0) {
	    ior->io_done = tty_close_read_reply;
	    iodone(ior);
	}
	while ((ior = (io_req_t)dequeue_head(&tp->t_delayed_write)) != 0) {
	    ior->io_done = tty_close_write_reply;
	    iodone(ior);
	}
	while ((ior = (io_req_t)dequeue_head(&tp->t_delayed_open)) != 0) {
	    ior->io_done = tty_close_open_reply;
	    iodone(ior);
	}

	/* Close down modem */
	if (tp->t_mctl) {
		(*tp->t_mctl)(tp, TM_BRK|TM_RTS, DMBIC);
		if ((tp->t_state & (TS_HUPCLS|TS_WOPEN)) ||
		((tp->t_state & TS_ISOPEN) == 0))
		    (*tp->t_mctl)(tp, TM_HUP, DMSET);
	}
	tp->t_state = tp->t_state & (TS_INIT|TS_CARR_ON|TS_HUPCLS);
}

/*
 * Port-death routine to clean up reply messages.
 */
boolean_t
tty_queue_clean(
	queue_t		q,
	ipc_port_t	port,
	boolean_t	(*routine)(io_req_t ior))
{
	io_req_t	ior;

	ior = (io_req_t)queue_first(q);
	while (!queue_end(q, (queue_entry_t)ior)) {
	    if (ior->io_reply_port == port) {
		remqueue(q, (queue_entry_t)ior);
		ior->io_done = routine;
		iodone(ior);
		return (TRUE);
	    }
	    ior = ior->io_next;
	}
	return (FALSE);
}

/*
 * Handle port-death (dead reply port) for tty.
 * No locks may be held.
 * May run on any CPU.
 */
boolean_t
tty_portdeath(
	struct tty	*tp,
	ipc_port_t	port)
{
	spl_t		spl = spltty();
	boolean_t	result;

	simple_lock(&tp->t_lock);

	/*
	 * The queues may never have been initialized
	 */
	if (tp->t_delayed_read.next == 0) {
	    result = FALSE;
	}
	else {
	    result =
		tty_queue_clean(&tp->t_delayed_read,  port,
				tty_close_read_reply)
	     || tty_queue_clean(&tp->t_delayed_write, port,
				tty_close_write_reply)
	     || tty_queue_clean(&tp->t_delayed_open,  port,
				tty_close_open_reply);
	}
	simple_unlock(&tp->t_lock);
	splx(spl);

	return (result);
}

/*
 * Read from TTY.
 * No locks may be held.
 * May run on any CPU - does not talk to device driver.
 */
io_return_t
char_read(
	struct tty	*tp,
	io_req_t	ior)
{
	kern_return_t	rc;
	spl_t		s;

	/*
	 * Allocate memory for read buffer.
	 */
	rc = device_read_alloc(ior, (vm_size_t)ior->io_count);
	if (rc != KERN_SUCCESS)
	    return (rc);

	s = spltty();
	simple_lock(&tp->t_lock);
	rc = do_char_read(tp, ior);
	simple_unlock(&tp->t_lock);
	splx(s);
	return rc;
}

kern_return_t
do_char_read(
	struct tty	*tp,
	io_req_t	ior)
{
	int nread;

	if (tp->t_outofband != TOOB_NO_EVENT)
	    return D_OUT_OF_BAND;

	if (tp->t_inq.c_cc <= 0) {
	    if (!(tp->t_state & TS_CARR_ON)) {
		if (!(tp->t_state & TS_ONDELAY)) {
		    /*
		     * No delayed reads - tell caller that device is down
		     */
		    return D_IO_ERROR;
		} else if (ior->io_mode & D_NOWAIT) {
		    return D_WOULD_BLOCK;
		}
	    }
	    ior->io_dev_ptr = (char *)tp;
	    queue_delayed_reply(&tp->t_delayed_read, ior, char_read_done);
	    return D_IO_QUEUED;
	}
	
	if (tp->t_nquoted > 0) {
	    nread = char_read_quoted(tp, (char *)ior->io_data, ior->io_count);
	    if (tp->t_outofband != TOOB_NO_EVENT) {
		if (nread == 0)
		    return D_OUT_OF_BAND;
	    } else {
		nread += q_to_b(&tp->t_inq,
				ior->io_data + nread,
				ior->io_count - nread);
	    }
	} else {
	    /* Degenerate but typical case: we could call char_read_quoted
	     * always, but we avoid the overhead.
	     */
	    nread = q_to_b(&tp->t_inq,
			   ior->io_data,
			   ior->io_count);
	}

	ior->io_residual = ior->io_count - nread;
	return D_SUCCESS;
}


/*
 * Read from TTY when we know there are some quoted characters in there.
 * Return number of characters read; stop when there are no more quoted
 * characters so the efficient q_to_b can be used.
 */
int
char_read_quoted(
	struct tty	*tp,
	char		*data,
	int		count)
{
	int nread = 0;
	int c;
	while (nread < count) {
	    if ((c = getc(&tp->t_inq)) < 0) {
		printf("char_read_quoted: t_nquoted lied\n");
		tp->t_nquoted = 0;
		break;
	    }
	    if ((char)c == TTY_QUOTEC) {
		switch (getc(&tp->t_inq)) {
		case -1:
		    printf("char_read_quoted: quote without quoted\n");
		    break;
		case TQ_QUOTEC:
		    *data++ = TTY_QUOTEC;
		    nread++;
		    break;
		case TQ_BREAK:
		    tp->t_outofband = TOOB_BREAK;
		    goto getarg;
		case TQ_BAD_PARITY:
		    tp->t_outofband = TOOB_BAD_PARITY;
getarg:
		    if ((c = getc(&tp->t_inq)) < 0) {
			printf("char_read_quoted: missing event arg\n");
			tp->t_outofbandarg = 0;
		    } else tp->t_outofbandarg = c;
		    break;
		case TQ_FLUSHED:
		    tp->t_outofband = TOOB_FLUSHED;
		    tp->t_outofbandarg = 0;
		    break;
		default:
		    printf("char_read_quoted: bad quoted value\n");
		    break;
		}
		tp->t_nquoted--;
		if (tp->t_outofband != TOOB_NO_EVENT || tp->t_nquoted == 0)
		    break;
	    } else {
		*data++ = c;
		nread++;
	    }
	}
	return nread;
}

/*
 * Retry wait for characters, for read.
 * No locks may be held.
 * May run on any CPU - does not talk to device driver.
 */
boolean_t
char_read_done(
	io_req_t	ior)
{
	register struct tty *tp = (struct tty *)ior->io_dev_ptr;
	kern_return_t rc;
	spl_t		s = spltty();

	simple_lock(&tp->t_lock);

	rc = do_char_read(tp, ior);

	simple_unlock(&tp->t_lock);
	splx(s);

	if (rc == D_IO_QUEUED)
	    return (FALSE);

	ior->io_error = rc;

	(void) ds_read_done(ior);
	return (TRUE);
}

boolean_t
tty_close_read_reply(
	io_req_t	ior)
{
	ior->io_residual = ior->io_count;
	ior->io_error = D_DEVICE_DOWN;
	(void) ds_read_done(ior);
	return (TRUE);
}

/* Drain output queue of tp.  Call at spltty with tty locked. */

void
ttydrain(
	struct tty	*tp)
{
	struct io_req iorq;
	simple_lock_init(&iorq.io_req_lock, ETAP_IO_REQ);
	while (tp->t_outq.c_cc) {
		iorq.io_op = 0;
		queue_delayed_reply(&tp->t_delayed_write, &iorq,
				    (boolean_t (*)(struct io_req *)) 0);
		tp->t_state |= TS_ASLEEP;
		while(!(iorq.io_op & IO_DONE)) {
		  	thread_t self;
			thread_sleep_simple_lock((event_t) &iorq,
						 simple_lock_addr(tp->t_lock), TRUE);
			if ((self = current_thread())->wait_result == THREAD_INTERRUPTED &&
			    self->top_act &&
			    self->top_act->handlers) {
				act_execute_returnhandlers();
			}
			simple_lock(&tp->t_lock);
		}

		tp->t_state &= ~TS_ASLEEP;
	}
}

/*
 * Write to TTY.
 * No locks may be held.
 * Calls device start routine; must already be on master if
 * device needs to run on master.
 */
io_return_t
char_write(
	struct tty *	tp,
	io_req_t	ior)
{
	spl_t		s;
	register int	i, c;
	register io_buf_len_t	count;
	register io_buf_ptr_t	data;
	char		obuf[100];
	vm_offset_t	addr;

	data  = ior->io_data;
	count = ior->io_count;
	if (count == 0)
	    return (D_SUCCESS);

	if (!(ior->io_op & IO_INBAND)) {
	    vm_map_copy_t copy = (vm_map_copy_t) data;
	    kern_return_t kr;

	    kr = vm_map_copyout(device_io_map, &addr, copy);
	    if (kr != KERN_SUCCESS)
		return (kr);
	    data = (io_buf_ptr_t) addr;
	}

	/*
	 * Loop is entered at spl0 and tty unlocked.
	 * All exits from the loop leave spltty and tty locked.
	 */
	while (TRUE) {
	    /*
	     * Copy a block of characters from the user.
	     */
	    c = count;
	    if (c > sizeof(obuf))
		c = sizeof(obuf);

	    /*
	     * Will fault on data.
	     */
	    bcopy(data, obuf, c);

	    /*
	     * Check for tty operating.
	     */
	    s = spltty();
	    simple_lock(&tp->t_lock);

	    if ((tp->t_state & TS_CARR_ON) == 0) {

		if ((tp->t_state & TS_ONDELAY) == 0) {
		    /*
		     * No delayed writes - tell caller that device is down
		     */
		    simple_unlock(&tp->t_lock);
		    splx(s);
		    if (!(ior->io_op & IO_INBAND))
		    (void) vm_deallocate(device_io_map, addr, ior->io_count);
		    return (D_IO_ERROR);
		}

		if (ior->io_mode & D_NOWAIT) {
		    simple_unlock(&tp->t_lock);
		    splx(s);
		    if (!(ior->io_op & IO_INBAND))
		    (void) vm_deallocate(device_io_map, addr, ior->io_count);
		    return (D_WOULD_BLOCK);
		}

	    }

	    /*
	     * If output clist is full, stop now.
	     */
	    if (tp->t_outq.c_cc > tp->t_hiwater)
		break;

	    /*
	     * Copy data into the output clist.
	     */
	    i = b_to_q(obuf, c, &tp->t_outq);
	    c -= i;

	    data  += c;
	    count -= c;

	    /*
	     * Stop copying if high-water mark exceeded
	     * or clist is full
	     * or data is exhausted.
	     */
	    if (tp->t_outq.c_cc > tp->t_hiwater || i != 0 || count == 0)
		break;

	    /*
	     * Unlock tty for the next copy from user.
	     */
	    simple_unlock(&tp->t_lock);
	    splx(s);
	}

	/*
	 * Report the amount not copied, and start hardware output.
	 */
	ior->io_residual = count;
/**	tp->t_state &= ~TS_TTSTOP; **/
	ttstart(tp);

	if (tp->t_outq.c_cc > tp->t_hiwater ||
	    (tp->t_state & (TS_CARR_ON | TS_ONDELAY)) == 0) {

	    /*
	     * Do not send reply until some characters have been sent.
	     */
	    ior->io_dev_ptr = (char *)tp;
	    queue_delayed_reply(&tp->t_delayed_write, ior, char_write_done);

	    simple_unlock(&tp->t_lock);
	    splx(s);

	    /*
	     * Incoming data has already been copied - delete it.
	     */
	    if (!(ior->io_op & IO_INBAND))
	    (void) vm_deallocate(device_io_map, addr, ior->io_count);
	    return (D_IO_QUEUED);
	}
	simple_unlock(&tp->t_lock);
	splx(s);

	if (!(ior->io_op & IO_INBAND))
	(void) vm_deallocate(device_io_map, addr, ior->io_count);
	return (D_SUCCESS);
}

/*
 * Retry wait for output queue emptied, for write.
 * No locks may be held.
 * May run on any CPU.
 */
boolean_t
char_write_done(
	io_req_t	ior)
{
	struct tty *tp = (struct tty *)ior->io_dev_ptr;
	spl_t s = spltty();

	simple_lock(&tp->t_lock);
	if (tp->t_outq.c_cc > tp->t_hiwater ||
	    (tp->t_state & TS_CARR_ON) == 0) {

	    queue_delayed_reply(&tp->t_delayed_write, ior, char_write_done);
	    simple_unlock(&tp->t_lock);
	    splx(s);
	    return (FALSE);
	}
	simple_unlock(&tp->t_lock);
	splx(s);

	return (ds_write_done(ior));
}

boolean_t
tty_close_write_reply(
	io_req_t	ior)
{
	ior->io_residual = ior->io_count;
	ior->io_error = D_DEVICE_DOWN;
	(void) ds_write_done(ior);
	return (TRUE);
}

/*
 * Set TTY status.
 * No locks may be held.
 * Calls device start or stop routines; must already be on master if
 * device needs to run on master.
 */
io_return_t
tty_set_status(
	struct tty		*tp,
	dev_flavor_t		flavor,
	dev_status_t		data,
	mach_msg_type_number_t	count)
{
	spl_t		s;

	switch (flavor) {
	    case TTY_FLUSH:
	    {
		register int	flags;
		if (count < TTY_FLUSH_COUNT)
		    return (D_INVALID_OPERATION);

		flags = *data;
		if (flags == 0)
		    flags = D_READ | D_WRITE;
		else
		    flags &= (D_READ | D_WRITE);
		ttyflush(tp, flags);
		break;
	    }
	    case TTY_STOP:
		/* stop output */
		s = spltty();
		simple_lock(&tp->t_lock);
		if ((tp->t_state & TS_TTSTOP) == 0) {
		    /* If we're draining output, stopping now could lead
		       to deadlock.  Wait for the drain to finish. XXX */
		    while (tp->t_state & TS_ASLEEP) {
			ttydrain(tp);
			ttstart(tp);
		    }
		    tp->t_state |= TS_TTSTOP;
		    (*tp->t_stop)(tp, 0);
		}
		simple_unlock(&tp->t_lock);
		splx(s);
		break;

	    case TTY_START:
		/* start output */
		s = spltty();
		simple_lock(&tp->t_lock);
		if (tp->t_state & TS_TTSTOP) {
		    tp->t_state &= ~TS_TTSTOP;
		    ttstart(tp);
		}
		simple_unlock(&tp->t_lock);
		splx(s);
		break;

	    case TTY_STATUS_NEW:
	    case TTY_STATUS_COMPAT:
		/* set special characters and speed */
	    {
		register struct tty_status *tsp;

		if (count < TTY_STATUS_COUNT)
		    return (D_INVALID_OPERATION);

		tsp = (struct tty_status *)data;

		s = spltty();
		simple_lock(&tp->t_lock);

		if (flavor == TTY_STATUS_COMPAT) {
			if (tsp->tt_ispeed < 0)
				tp->t_ispeed = 0;
			else if (tsp->tt_ispeed < NSPEEDS_COMPAT)
				tp->t_ispeed =
					compat_to_baud_rate[tsp->tt_ispeed];
			else
				tp->t_ispeed = 0;
			if (tsp->tt_ospeed < 0)
				tp->t_ospeed = 0;
			else if (tsp->tt_ospeed < NSPEEDS_COMPAT)
				tp->t_ospeed =
					compat_to_baud_rate[tsp->tt_ospeed];
			else
				tp->t_ospeed = 0;
		} else {
			tp->t_ispeed = tsp->tt_ispeed;
			tp->t_ospeed = tsp->tt_ospeed;
		}
		tp->t_breakc = tsp->tt_breakc;
		tp->t_flags  = tsp->tt_flags & ~TF_HUPCLS;
		if (tsp->tt_flags & TF_HUPCLS)
		    tp->t_state |= TS_HUPCLS;
		else tp->t_state &= ~TS_HUPCLS;
		tp->t_hiwater = TTHIWAT(tp);
		tp->t_lowater = TTLOWAT(tp);

		simple_unlock(&tp->t_lock);
		splx(s);
		break;
	    }
	    case TTY_DRAIN:
		s = spltty();
		simple_lock(&tp->t_lock);
		ttydrain(tp);
		simple_unlock(&tp->t_lock);
		splx(s);
		break;
	    default:
		return (D_INVALID_OPERATION);
	}
	return (D_SUCCESS);
}

/*
 * Get TTY status.
 * No locks may be held.
 * May run on any CPU.
 */
io_return_t
tty_get_status(
	struct tty		*tp,
	dev_flavor_t		flavor,
	dev_status_t		data,		/* pointer to OUT array */
	mach_msg_type_number_t	*count)		/* out */
{
	spl_t		s;

	switch (flavor) {
	    case TTY_STATUS_NEW:
	    case TTY_STATUS_COMPAT:
	    {
		register struct tty_status *tsp =
			(struct tty_status *) data;

		s = spltty();
		simple_lock(&tp->t_lock);

		tsp->tt_ispeed = tp->t_ispeed;
		tsp->tt_ospeed = tp->t_ospeed;
		tsp->tt_breakc = tp->t_breakc;
		tsp->tt_flags  = tp->t_flags;
		if (tp->t_state & TS_HUPCLS)
		    tsp->tt_flags |= TF_HUPCLS;

		simple_unlock(&tp->t_lock);
		splx(s);

		if (flavor == TTY_STATUS_COMPAT) {
			tsp->tt_ispeed =
				baud_rate_get_info(tsp->tt_ispeed,
						   baud_rate_to_compat);
			if (tsp->tt_ispeed == -1)
				tsp->tt_ispeed = NSPEEDS_COMPAT - 1;
			tsp->tt_ospeed =
				baud_rate_get_info(tsp->tt_ospeed,
						   baud_rate_to_compat);
			if (tsp->tt_ospeed == -1)
				tsp->tt_ospeed = NSPEEDS_COMPAT - 1;
		}

		*count = TTY_STATUS_COUNT;
		break;

	    }
	    case TTY_OUT_OF_BAND:
	    {
		register struct tty_out_of_band *toobp =
			(struct tty_out_of_band *) data;

		s = spltty();
		simple_lock(&tp->t_lock);

		toobp->toob_event = tp->t_outofband;
		toobp->toob_arg = tp->t_outofbandarg;
		tp->t_outofband = TOOB_NO_EVENT;

		simple_unlock(&tp->t_lock);
		splx(s);

		*count = TTY_OUT_OF_BAND_COUNT;
		break;

	    }
	    default:
		return (D_INVALID_OPERATION);
	}
	return (D_SUCCESS);
}

/*
 * Retry delayed IO operations for TTY.
 * TTY containing queue must be locked (at spltty).
 */
void
tty_queue_completion(
	queue_t		qh)
{
	io_req_t	ior;

	while ((ior = (io_req_t)dequeue_head(qh)) != 0) {
	    iodone(ior);
	}
}


/*
 * The inq contains characters: push them into iors (if any);
 * but put no "empty" iors on the completion queue.
 *
 * TTY containing queue must be locked (at spltty).
 */
extern void
tty_queue_filled_reads(
	struct tty	*tp)
{
	queue_t	qh = &tp->t_delayed_read;

	while (tp->t_inq.c_cc > 0) {
	    io_req_t	ior = (io_req_t)dequeue_head(qh);
	    int		nread;

	    if (!ior)
		break;
	    /*
	     * We'd like to assert that any queued ior's were created
	     * via an io_done queue interface, but can't, so long as
	     * old servers can use the old "request" interfaces.
	    assert( ior->io_op & IO_QUEUE );
	     */
	    if (tp->t_nquoted > 0) {
		nread = char_read_quoted(tp,(char *)ior->io_data,ior->io_count);
		/* XXX No OOB mechanism here! */
		nread += q_to_b(&tp->t_inq,
				    ior->io_data + nread,
				    ior->io_count - nread);
	    } else {
		/* Degenerate but typical case: we could call char_read_quoted
		 * always, but we avoid the overhead.
		 */
		nread = q_to_b(&tp->t_inq,
			       ior->io_data,
			       ior->io_count);
	    }
	    ior->io_residual = ior->io_count - nread;
	    ior->io_done = ds_read_done;
	    iodone(ior);
	}
}


/*
 * [internal]
 * Queue IOR on reply queue, to wait for TTY operation.
 * TTY must be locked (at spltty).
 */
void
queue_delayed_reply(
	queue_t		qh,
	io_req_t	ior,
	boolean_t	(*io_done)(io_req_t))
{
	ior->io_done = io_done;
	enqueue_tail(qh, (queue_entry_t)ior);
}

/*
 * Flush all TTY queues.
 * No locks may be held.
 * Calls device STOP routine; must already be on master if
 * device needs to run on master.
 */

void
ttyflush(
	struct tty	*tp,
	int		rw)
{
	spl_t		s = spltty();

	simple_lock(&tp->t_lock);

	if (rw & D_READ) {
	    while (getc(&tp->t_inq) >= 0)
		continue;
	    tty_queue_completion(&tp->t_delayed_read);
	}
	if (rw & D_WRITE) {
	    tp->t_state &= ~TS_TTSTOP;
	    (*tp->t_stop)(tp, rw);
	    while (getc(&tp->t_outq) >= 0)
		continue;
	    tty_queue_completion(&tp->t_delayed_write);
	}
	if (rw & D_READ) {
	    while (getc(&tp->t_inq) >= 0)
		continue;
	    tp->t_nquoted = 0;
#if 0
	    if (tp->t_flags & TF_OUT_OF_BAND)
		ttyquoted(TQ_FLUSHED, -1, tp);
#endif/*  0 */
	}
	simple_unlock(&tp->t_lock);
	splx(s);
}

/*
 * Set the default special characters.
 * Since this routine is called whenever a tty has never been opened,
 * we can initialize the queues here.
 *
 * N.B.  This routine is not really correct on an SMP.  A race
 * opening a tty could result in multiple execution of the
 * queue, buffer and lock initialization code, below.  This
 * race used to be serialized by the caller taking the tty lock,
 * but someone decided to restructure the code, leaving us with
 * the race.  XXX
 */

void
ttychars(
	struct tty	*tp)
{
	if ((tp->t_state & TS_INIT) == 0) {
	    /*
	     * Initialize queues
	     */
	    queue_init(&tp->t_delayed_open);
	    queue_init(&tp->t_delayed_read);
	    queue_init(&tp->t_delayed_write);

	    /*
	     * Initialize character buffers
	     */
	    cb_alloc(&tp->t_inq,  tty_inq_size);

	    /* if we might do modem flow control */
	    if (tp->t_mctl && (tp->t_inq.c_hog > 30))
		    tp->t_inq.c_hog -= 30;

	    cb_alloc(&tp->t_outq, tty_outq_size);

	    tp->t_nquoted = 0;
	    tp->t_outofband = TOOB_NO_EVENT;

	    /*
	     * Initialize the lock.
	     */
	    simple_lock_init(&tp->t_lock, ETAP_IO_TTY);

	    /*
	     * Mark initialized
	     */
	    tp->t_state |= TS_INIT;
	}

	tp->t_breakc = 0;
}


/*
 * Start output on the TTY. It is used from the top half
 * after some characters have been put on the output queue,
 * from the interrupt routine to transmit the next
 * character, and after a timeout has finished.
 *
 * Called at spltty, tty already locked.
 * Must be on master CPU if device runs on master.
 */

void
ttstart(
	struct tty	*tp)
{
	if ((tp->t_state & (TS_BUSY|TS_TTSTOP|TS_TIMEOUT)) == 0) {
	    /*
	     * Start up the hardware again
	     */
	    (*tp->t_start)(tp);

	    /*
	     * Wake up those waiting for write completion.
	     */
	    if (tp->t_outq.c_cc <= tp->t_lowater)
		tty_queue_completion(&tp->t_delayed_write);
	}
}

/*
 * Restart tty output following a timeout.  This is
 * called off of the kernel timeout queue and hence will
 * be called at interrupt/ast level.  It must be on the
 * master processor because it calls the device's start
 * routine (via ttstart).
 *
 * XXX What if device only accessible from a CPU other
 * XXX than the one that is running the timeout queue??
 */

void
ttrstrt(
	struct tty	*tp)
{
	spl_t		s;

	if (tp == 0)
		panic("ttrstrt");

	s = spltty();
	simple_lock(&tp->t_lock);

	tp->t_state &= ~TS_TIMEOUT;
	if ((tp->t_state & (TS_TTSTOP | TS_BUSY)) == 0) {
	    /*
	     * Not busy - start output.
	     */
	    (*tp->t_start)(tp);

	    /*
	     * If output buffer has been drained,
	     * wake up writers.
	     */
	    if (tp->t_outq.c_cc <= tp->t_lowater)
		tty_queue_completion(&tp->t_delayed_write);
	}

	simple_unlock(&tp->t_lock);
        splx(s);
}

/*
 *  TTY output routine.  Put one character on output queue and
 *  start the TTY (if needed, ttstart makes that decision).
 */

void
ttyoutput(
	unsigned	c,
	struct tty	*tp)
{
	spl_t		s;

	s = spltty();
	simple_lock(&tp->t_lock);
	(void) putc(c, &tp->t_outq);
	ttstart(tp);
	simple_unlock(&tp->t_lock);
	splx(s);
}

/*
 * Add quoted character of type qc to input queue.  Tty is locked.
 */

void
ttyquoted(
	int		qc,
	int		qarg,
	struct tty	*tp)
{
	int spaceneeded = (qarg >= 0) ? 3 : 2;
	if (cb_space(&tp->t_inq) >= spaceneeded) {
	    if (putc((char)TTY_QUOTEC, &tp->t_inq) || putc(qc, &tp->t_inq) ||
		(qarg >= 0 && putc(qarg, &tp->t_inq)))
		panic("ttyquoted: cb_space lied");
	    tp->t_nquoted++;
	    tty_queue_filled_reads(tp);
	}
}

/*
 * Put input character on input queue.
 *
 * Called at spltty, tty already locked.
 */

void
ttyinput(
	unsigned int	c,
	struct tty	*tp)
{
	if (tp->t_inq.c_cc >= tp->t_inq.c_hog) {
		/*
		 * Do not want to overflow input queue 
		 */
		if (tp->t_mctl) {
			(*tp->t_mctl)(tp, TM_RTS, DMBIC);
			tp->t_state |= TS_RTS_DOWN;
		}
		tty_queue_filled_reads(tp);
		return;
	} else if (tp->t_state & TS_RTS_DOWN) {
		(*tp->t_mctl)(tp, TM_RTS, DMBIS);
		tp->t_state &= ~TS_RTS_DOWN;
	}

	c &= 0xff;

	if (putc(c, &tp->t_inq) >= 0) {
		register boolean_t	awakeit;

		awakeit = ((tp->t_state & TS_MIN) == 0) ||
			  (tp->t_inq.c_cc > TTMINBUF);


		if (!awakeit) {
			if ((tp->t_state & TS_MIN_TO) == 0) {
				tp->t_state |= TS_MIN_TO;
				timeout((timeout_fcn_t)ttypush, tp, hz/4);
			}
		} else {
			/*
			 * Grab request from input queue, queue it to
			 * char_io_complete thread. 
			 */
			if (tp->t_state & TS_MIN_TO) {
				tp->t_state &= ~TS_MIN_TO;
				untimeout((timeout_fcn_t)ttypush, tp);
			}
			tty_queue_filled_reads(tp);
		}
	}
}

/*
 * Check if anything is on the input queue.
 * Called at spltty, tty already locked.
 *
 * Needed by sun console driver with prom monitor
 */

boolean_t
tty_queueempty(
	struct tty	*tp,
	int		queue)
{
	if (queue & D_READ) return (tp->t_inq.c_cc <= 0);
	if (queue & D_WRITE) return (tp->t_outq.c_cc <= 0);
	return (TRUE);
}

/*
 * Send any buffered recvd chars up to user
 */

void ttypush(
	struct tty	*tp)
{
	spl_t		s = spltty();

	simple_lock(&tp->t_lock);

	if ((tp->t_state & TS_MIN_TO) && tp->t_inq.c_cc)
		tty_queue_filled_reads(tp);
	tp->t_state &= ~TS_MIN_TO;

	simple_unlock(&tp->t_lock);
	splx(s);
}

/*
 * Record a received break or framing error, the incorrectly framed
 * character being c.  Called at spltty, tty already locked.
 */

void
ttyinputbadparity(
	int		c,
	struct tty	*tp)
{
#if 0
	/*
	 * Pushing characters with incorrect parity though the OOB
	 * mechanism can *never* work.  They need to stay synchronized
	 * with the input stream; hence they need to be encoded a la
	 * Posix as they move from tp->t_inq into ior-s from
	 * tp->t_delayed_read and get put on the reply port's
	 * io_done_list.  Otherwise, the OOB char with weird parity will
	 * jump in front of characters that are already on the io_done_list.
	 *
	 * Disable the parity mechanism as a temporary workaround.
	 */
	if (tp->t_flags & TF_INPCK) {
	    if (tp->t_flags & TF_OUT_OF_BAND)
		ttyquoted(TQ_BAD_PARITY, c, tp);
	    /* else drop the character. */
	} else
#endif
		ttyinput(c, tp);
}

void
ttybreak(
	int		c,
	struct tty	*tp)
{
	if (tp->t_flags & TF_OUT_OF_BAND)
	    ttyquoted(TQ_BREAK, c, tp);
	else ttyinput(tp->t_breakc, tp);
}

/*
 * Handle modem control transition on a tty.
 * carrier_up flag indicates new state of carrier.
 * Returns 0 if the line should be turned off, otherwise 1.
 *
 * Called at spltty, tty already locked.
 */
boolean_t
ttymodem(
	struct tty	*tp,
	int		carrier_up)
{

	if ((tp->t_state&TS_WOPEN) == 0 && (tp->t_flags & MDMBUF)) {
		/*
		 * MDMBUF: do flow control according to carrier_up flag
		 */
		if (carrier_up) {
			tp->t_state &= ~TS_TTSTOP;
			ttstart(tp);
		} else if ((tp->t_state&TS_TTSTOP) == 0) {
			/* If we're draining output, stopping now could lead
			   to deadlock.  Wait for the drain to finish. XXX */
			while (tp->t_state & TS_ASLEEP)
				ttydrain(tp);
			tp->t_state |= TS_TTSTOP;
			(*tp->t_stop)(tp, 0);
		}
	} else if (carrier_up == 0) {
		/*
		 * Lost carrier.
		 */
		tp->t_state &= ~TS_CARR_ON;
		if (tp->t_state & TS_ISOPEN) {
		    if (tp->t_state & TS_ONDELAY) {
			/* notify server of change in carrier given ONDELAY */
			tp->t_outofband = TOOB_CARRIER;
			tp->t_outofbandarg = 0;
		    }
		    /* complete any pending read with an error */
		    tty_queue_completion(&tp->t_delayed_read);
		    if ((tp->t_flags & NOHANG) == 0) {
			/* This is just ttyflush(R|W) without the locking. */
			while (getc(&tp->t_inq) >= 0)
			    continue;
			tty_queue_completion(&tp->t_delayed_read);
			tp->t_nquoted = 0;
			tp->t_state &= ~TS_TTSTOP;
			(*tp->t_stop)(tp, D_READ|D_WRITE);
			while (getc(&tp->t_outq) >= 0)
			    continue;
			tty_queue_completion(&tp->t_delayed_write);
#if 0
			if (tp->t_flags & TF_OUT_OF_BAND)
			    ttyquoted(TQ_FLUSHED, -1, tp);
#endif
			return (FALSE);
		    }
		}
	} else {
		/*
		 * Carrier now on.
		 */
		tp->t_state |= TS_CARR_ON;
		tt_open_wakeup(tp);
	}
	return (TRUE);
}

/*
 * Tty must be locked and on master.
 */
void tty_cts(
        struct tty *    tp,
        boolean_t       cts_up)
{
        if ((tp->t_state & TS_ISOPEN) && (tp->t_flags & CRTSCTS)) {
                if (cts_up) {
                        tp->t_state &= ~(TS_TTSTOP|TS_BUSY);
                        (*tp->t_start)(tp);
                } else {
                        tp->t_state |= (TS_TTSTOP|TS_BUSY);
                        (*tp->t_stop)(tp, D_WRITE);
                }
        }
}

#ifdef TTY_DEBUG	/* showtty() function. */

struct flagnames {
    char *name;
    int bit;
};

struct flagnames tty_state_names[] = {
    "INIT", TS_INIT, "TIMEOUT", TS_TIMEOUT, "WOPEN", TS_WOPEN,
    "ISOPEN", TS_ISOPEN, "FLUSH", TS_FLUSH, "CARR_ON", TS_CARR_ON,
    "BUSY", TS_BUSY, "ASLEEP", TS_ASLEEP, "TTSTOP", TS_TTSTOP,
    "HUPCLS", TS_HUPCLS, "TBLOCK", TS_TBLOCK, "NBIO", TS_NBIO,
    "ONDELAY", TS_ONDELAY, "OUT", TS_OUT, "TRANSLATE", TS_TRANSLATE,
    "KDB", TS_KDB, 0, 0,
};

struct flagnames tty_flag_names[] = {
    "TANDEM", TF_TANDEM, "ODDP", TF_ODDP, "EVENP", TF_EVENP,
    "LITOUT", TF_LITOUT, "MDMBUF", TF_MDMBUF, "NOHANG", TF_NOHANG,
    "HUPCLS", TF_HUPCLS, "ECHO", TF_ECHO, "CRMOD", TF_CRMOD,
    "XTABS", TF_XTABS, "OUT_OF_BAND", TF_OUT_OF_BAND, "INPCK", TF_INPCK,
    "CRTSCTS", TF_CRTSCTS,
    0, 0,
};

extern void		showflags(
				char			*name,
				int			bits,
				struct flagnames	*names);
extern void		showtty(
				struct tty		* tp);
extern void		showqueue(
				char			* name,
				queue_head_t		* q);
extern void		showcirbuf(
				char			* name,
				struct cirbuf		* q);

void
showtty(
	struct tty	*tp)
{
    printf("tty %x: ", tp);
    showcirbuf("input", &tp->t_inq);
    showcirbuf("output", &tp->t_outq);
    printf("addr %x; dev %x; start %x; stop %x; ispeed %d; ospeed %d; breakc %x; ",
	   tp->t_addr, tp->t_dev, tp->t_start, tp->t_stop, tp->t_ispeed,
	   tp->t_ospeed, tp->t_breakc);
    showflags("flags", tp->t_flags, tty_flag_names);
    showflags("state", tp->t_state, tty_state_names);
    printf("line %d; outofband %d; outofbandarg %x; nquoted %d; ",
	   tp->t_line, tp->t_outofband, tp->t_outofbandarg, tp->t_nquoted);
    showqueue("read q", &tp->t_delayed_read);
    showqueue("write q", &tp->t_delayed_write);
    showqueue("open q", &tp->t_delayed_open);
    printf("getstat %x; setstat %x; tops %x; ", tp->t_getstat, tp->t_setstat,
	   tp->t_tops);
}

void
showcirbuf(
	char		*name,
	struct cirbuf	*q)
{
    printf("%s: %d chars; ", name, q->c_cc);	/* Could do better. */
}

void
showflags(
	char			*name,
	int			bits,
	struct flagnames	*names)
{
    char *sep = "";
    printf("%s: %x <", name, bits);
    for ( ; names->name; names++) {
	if (bits & names->bit) {
	    printf("%s%s", sep, names->name);
	    sep = "|";
	    bits &= ~names->bit;
	}
    }
    if (bits)
	printf("%s%x", sep, bits);
    printf(">; ");
}

void
showqueue(
	char		*name,
	queue_head_t	*q)
{
    queue_chain_t *p;
    int n;
    printf("%s: head %x, queue:", name, q);
    if (q->next == q)
	printf(" empty");
    else {
	for (p = q->next, n = 0; p != q && n < 10; p = p->next, n++)
	    printf(" %x", p);
	if (p != q)
	    printf("...");	/* Probably a loop in a bogus tty struct. */
    }
    printf("; ");
}
#endif /* TTY_DEBUG */
