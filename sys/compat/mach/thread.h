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
 * Revision 2.13.5.2  92/09/15  17:23:18  jeffreyh
 * 	Add argument defines for thread_halt.
 * 	[92/08/06            dlb]
 * 
 * 	Cleanup for profiling, dont include profil.h
 * 	[92/07/24            bernadat]
 * 
 * Revision 2.13.5.1  92/02/18  19:12:48  jeffreyh
 * 	Added an optional field for measuring number
 * 	of simple locks held when MACH_LOCK_MON is on.
 * 	[91/12/06            bernadat]
 * 
 * 	New fields for profiling
 * 	(Bernard Tabib & Andrei Danes @ gr.osf.org)
 * 	[91/09/16            bernadat]
 * 
 * Revision 2.13  91/08/28  11:14:53  jsb
 * 	Added ith_seqno.
 * 	[91/08/10            rpd]
 * 
 * Revision 2.12  91/07/31  17:49:44  dbg
 * 	Consolidated interruptible, swap_state, halted into state field.
 * 	Revised state machine.
 * 	[91/07/30  17:06:37  dbg]
 * 
 * Revision 2.11  91/05/18  14:34:28  rpd
 * 	Added depress_timer.
 * 	[91/03/31            rpd]
 * 
 * 	Replaced swap_privilege with stack_privilege.
 * 	[91/03/30            rpd]
 * 
 * Revision 2.10  91/05/14  16:48:57  mrt
 * 	Correcting copyright
 * 
 * Revision 2.9  91/03/16  14:52:58  rpd
 * 	Removed ith_saved.
 * 	[91/02/16            rpd]
 * 	Added save-state fields for page faults.
 * 	[91/02/05            rpd]
 * 	Added NCPUS to active_threads declaration.
 * 	Added active_stacks.
 * 	[91/01/28            rpd]
 * 	Added swap_privilege.
 * 	[91/01/18            rpd]
 * 
 * Revision 2.8  91/02/05  17:30:34  mrt
 * 	Changed to new Mach copyright
 * 	[91/02/01  16:19:58  mrt]
 * 
 * Revision 2.7  91/01/08  15:18:11  rpd
 * 	Added saved-state fields for exceptions.
 * 	[90/12/23            rpd]
 * 	Added swap_func.
 * 	[90/11/20            rpd]
 * 
 * Revision 2.6  90/08/27  22:04:23  dbg
 * 	Remove import of thread_modes.h (unneeded).
 * 	[90/07/17            dbg]
 * 
 * Revision 2.5  90/08/07  17:59:15  rpd
 * 	Put last_processor field under NCPUS > 1.
 * 	Removed tmp_address and tmp_object fields.
 * 	[90/08/07            rpd]
 * 
 * Revision 2.4  90/06/02  14:57:07  rpd
 * 	Converted to new IPC.
 * 	[90/03/26  22:25:34  rpd]
 * 
 * Revision 2.3  90/02/22  20:04:18  dbg
 * 	Add per-thread global VM variables (tmp_address, tmp_object).
 * 		[89/04/29	mwyoung]
 * 
 * Revision 2.2  89/09/08  11:26:59  dbg
 * 	Added simple_rpc_kmsg [rfr].  Set its size to size of
 * 	small Kmsg.  Moved all IPC data structures to end of
 * 	thread structure.
 * 	[89/08/16            dbg]
 * 
 * Revision 2.6  88/10/11  10:26:00  rpd
 * 	Added ipc_data to the thread structure.
 * 	[88/10/10  08:00:16  rpd]
 * 	
 * Revision 2.5  88/08/24  02:47:53  mwyoung
 * 	Adjusted include file references.
 * 	[88/08/17  02:24:57  mwyoung]
 * 
 *  1-Sep-88  David Black (dlb) at Carnegie-Mellon University
 *	Change all usage and delta fields to unsigned to prevent
 *	negative priorities.
 *
 * 11-Aug-88  David Black (dlb) at Carnegie-Mellon University
 *	Split exit_code field into ast and halted fields.
 *	Changed thread_should_halt() macro.
 *
 *  9-Aug-88  David Black (dlb) at Carnegie-Mellon University
 *	Replaced preempt_pri field with first_quantum.
 *
 * 19-Aug-88  David Golub (dbg) at Carnegie-Mellon University
 *	Changed 'struct pcb *' to 'pcb_t' to pry this file loose from
 *	old data structures.  Removed include of 'machine/pcb.h' - the
 *	structure definition should be moved to 'machine/thread.h'.
 *
 * Revision 2.4  88/08/06  19:22:05  rpd
 * Declare external variables as "extern".
 * Added macros ipc_thread_lock(), ipc_thread_unlock().
 * 
 * Revision 2.3  88/07/17  18:55:10  mwyoung
 * .
 * 
 * Revision 2.2.1.1  88/06/28  20:53:40  mwyoung
 * Reorganized.  Added thread_t->vm_privilege.
 * 
 * Added current_task() declaration.
 * 
 *
 *  6-Jul-88  David Golub (dbg) at Carnegie-Mellon University
 *	Remove compatibility data structures.  Add per-thread timeout
 *	element.
 *
 * 21-Jun-88  Michael Young (mwyoung) at Carnegie-Mellon University
 *	Reorganized.  Added thread_t->vm_privilege.
 *
 *  4-May-88  David Golub (dbg) at Carnegie-Mellon University
 *	Remove vax-specific field (pcb physical address).
 *
 * 19-Apr-88  Michael Young (mwyoung) at Carnegie-Mellon University
 *	Added current_task() declaration.
 *
 *  7-Apr-88  David Black (dlb) at Carnegie-Mellon University
 *	MACH_TIME_NEW is now standard.
 *
 *  4-Mar-88  David Black (dlb) at Carnegie-Mellon University
 *	Added usage_save and preempt_pri fields.
 *
 * 19-Feb-88  David Black (dlb) at Carnegie-Mellon University
 *	Deleted unused ticks field.  Rearranged and added MACH_TIME_NEW
 *	fields for scheduler interface.  user_ticks and system_ticks are
 *	not needed under MACH_TIME_NEW.  Change wait_time to sched_stamp.
 *
 * 21-Jan-88  David Golub (dbg) at Carnegie-Mellon University
 *	Replaced swappable boolean with swap_state field.  Swap states
 *	are defined in sys/thread_swap.h.
 *
 * 17-Jan-88  David Golub (dbg) at Carnegie-Mellon University
 *	Added declarations of new routines.
 *
 * 29-Dec-87  David Golub (dbg) at Carnegie-Mellon University
 *	Added declarations of thread_halt() and thread_halt_self().
 *
 * 21-Dec-87  David Golub (dbg) at Carnegie-Mellon University
 *	Added thread_should_halt macro.
 *
 *  9-Dec-87  David Golub (dbg) at Carnegie-Mellon University
 *	Added exit_code for thread termination and interrupt.
 *	Removed ipc_message_waiting and ipc_timer_set.
 *
 *  3-Dec-87  David Black (dlb) at Carnegie-Mellon University
 *	Added ipc_kernel field to indicate when message buffer is in
 *	kernel address space.  Added exception_clear_port to cache
 *	reply port for reuse in exc rpc.
 *
 *  2-Dec-87  David Black (dlb) at Carnegie-Mellon University
 *	Removed conditionals, purged history.
 */
/* CMU_ENDHIST */
/* 
 * Mach Operating System
 * Copyright (c) 1991,1990,1989,1988,1987 Carnegie Mellon University
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
 *	File:	thread.h
 *	Author:	Avadis Tevanian, Jr.
 *
 *	This file contains the structure definitions for threads.
 *
 */
/*
 * Copyright (c) 1993 The University of Utah and
 * the Computer Systems Laboratory (CSL).  All rights reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * THE UNIVERSITY OF UTAH AND CSL ALLOW FREE USE OF THIS SOFTWARE IN ITS "AS
 * IS" CONDITION.  THE UNIVERSITY OF UTAH AND CSL DISCLAIM ANY LIABILITY OF
 * ANY KIND FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * CSL requests users of this software to return to csl-dist@cs.utah.edu any
 * improvements that they make and grant CSL redistribution rights.
 *
 */

#ifndef	_KERN_THREAD_H_
#define _KERN_THREAD_H_
#if 0
#include <cpus.h>
#include <hw_footprint.h>
#include <mach_host.h>
#include <mach_prof.h>
#include <dipc.h>
#include <xkmachkernel.h>
#include <mach_lock_mon.h>
#include <mach_ldebug.h>
#include <linux_dev.h>
#endif

#include <sys/mach/port.h>
#include <sys/mach/message.h>
#include <machine/mach/vm_types.h>
#include <sys/mach/vm_prot.h>
#include <sys/mach/exception.h>
#include <sys/mach/thread_info.h>
#if 0
#include <kern/ast.h>
#include <kern/kern_types.h>
#include <kern/cpu_number.h>
#include <kern/queue.h>
#include <kern/processor.h>
#include <kern/task.h>
#include <kern/cpu_data.h>
#include <kern/time_out.h>
#include <kern/timer.h>
#include <kern/lock.h>
#include <kern/sched.h>
#endif
#if 0
#include <vm/vm_map.h>		/* for vm_map_version_t */
#include <machine/mach/thread.h>
#include <sys/mach/thread_status.h>

#include <kern/thread_pool.h>
#include <kern/thread_act.h>
#include <ipc/ipc_kmsg.h>
#endif

/*
 * Logically, a thread of control consists of two parts:
 *	a thread_shuttle, which may migrate during an RPC, and
 *	a thread_activation, which remains attached to a task.
 * The thread_shuttle is the larger portion of the two-part thread,
 * and contains scheduling info, messaging support, accounting info,
 * and links to the thread_activation within which the shuttle is
 * currently operating.
 *
 * It might make sense to have the thread_shuttle be a proper sub-structure
 * of the thread, with the thread containing links to both the shuttle and
 * activation.  In order to reduce the scope and complexity of source
 * changes and the overhead of maintaining these linkages, we have subsumed
 * the shuttle into the thread, calling it a thread_shuttle.
 *
 * User accesses to threads always come in via the user's thread port,
 * which gets translated to a pointer to the target thread_activation.
 * Kernel accesses intended to effect the entire thread, typically use
 * a pointer to the thread_shuttle (current_thread()) as the target of
 * their operations.  This makes sense given that we have subsumed the
 * shuttle into the thread_shuttle, eliminating one set of linkages.
 * Operations effecting only the shuttle may use a thread_shuttle_t
 * to indicate this.
 *
 * The current_act() macro returns a pointer to the current thread_act, while
 * the current_thread() macro returns a pointer to the currently active
 * thread_shuttle (representing the thread in its entirety).
 */

#define	NO_EVENT	((event_t)0)
#define	WAKING_EVENT	((event_t)~0)

typedef struct thread_shuttle {
	/*
	 * Beginning of thread_shuttle proper
	 */
	queue_chain_t	links;		/* current run queue links */
	run_queue_t	runq;		/* run queue p is on SEE BELOW */
	int		whichq;		/* which queue level p is on */
/*
 *	NOTE:	The runq field in the thread structure has an unusual
 *	locking protocol.  If its value is RUN_QUEUE_NULL, then it is
 *	locked by the thread_lock, but if its value is something else
 *	(i.e. a run_queue) then it is locked by that run_queue's lock.
 */

	/* Thread bookkeeping */
	queue_chain_t	pset_threads;	/* list of all shuttles in proc set */

	/* Self-preservation */
	decl_simple_lock_data(,lock)	/* scheduling lock (thread_lock()) */
	decl_simple_lock_data(,w_lock)  /* covers wake_active (wake_lock())*/
	decl_mutex_data(,rpc_lock)	/* RPC lock (rpc_lock()) */
	int		ref_count;	/* number of references to me */
	
	vm_offset_t     kernel_stack;   /* accurate only if the thread is 
					   not swapped and not executing */

	vm_offset_t	stack_privilege;/* reserved kernel stack */

	/* Blocking information */
	int		reason;		/* why we blocked */
	event_t		wait_event;	/* event we are waiting on */
	kern_return_t	wait_result;	/* outcome of wait -
					   may be examined by this thread
					   WITHOUT locking */
	queue_chain_t	wait_link;	/* event's wait queue link */
	boolean_t	wake_active;	/* Someone is waiting for this
					   thread to become suspended */
	int		state;		/* Thread state: */

/*
 *	Thread states [bits or'ed]
 */
#define TH_WAIT			0x01	/* thread is queued for waiting */
#define TH_SUSP			0x02	/* thread has been asked to stop */
#define TH_RUN			0x04	/* thread is running or on runq */
#define TH_UNINT		0x08	/* thread is waiting uninteruptibly */
#define	TH_HALTED		0x10	/* thread is halted at clean point ? */

#define TH_ABORT		0x20	/* abort interruptible waits */
#define TH_SWAPPED_OUT		0x40	/* thread is swapped out */

#define TH_IDLE			0x80	/* thread is an idle thread */

#define	TH_SCHED_STATE	(TH_WAIT|TH_SUSP|TH_RUN|TH_UNINT)

/* These two flags will never be seen and might well be removed */
#define	TH_STACK_HANDOFF	0x0100	/* thread has no kernel stack */
#define	TH_STACK_COMING_IN	0x0200	/* thread is waiting for kernel stack */
#define	TH_STACK_STATE	(TH_STACK_HANDOFF | TH_STACK_COMING_IN)

	int		preempt;	/* Thread preemption status */
#define	TH_PREEMPTABLE		0	/* Thread is preemptable */
#define	TH_NOT_PREEMPTABLE	1	/* Thread is not preemptable */
#define	TH_PREEMPTED		2	/* Thread has been preempted */

#if	ETAP_EVENT_MONITOR
	int		etap_reason;	/* real reason why we blocked */
	boolean_t	etap_trace;	/* ETAP trace status */
#endif	/* ETAP_EVENT_MONITOR */


	/* Stack handoff information */
	void		(*continuation)(/* start here next time runnable */
				void);

	/* Scheduling information */
	int		priority;	/* thread's priority */
	int		max_priority;	/* maximum priority */
	int		sched_pri;	/* scheduled (computed) priority */
	int		sched_data;	/* for use by policy */
	int		policy;		/* scheduling policy */
	int		depress_priority; /* depressed from this priority */
	unsigned int	cpu_usage;	/* exp. decaying cpu usage [%cpu] */
	unsigned int	sched_usage;	/* load-weighted cpu usage [sched] */
	unsigned int	sched_stamp;	/* last time priority was updated */
	unsigned int	sleep_stamp;	/* last time in TH_WAIT state */
	unsigned int	sched_change_stamp;
					/* last time priority or policy was
					   explicitly changed (not the same
					   units as sched_stamp!) */
	int		unconsumed_quantum;	/* leftover quantum (RR/FIFO) */

	/* VM global variables */
	boolean_t	vm_privilege;	/* can use reserved memory? */
	vm_offset_t	recover;	/* page fault recovery (copyin/out) */

	/* IPC data structures */
	struct thread_shuttle *ith_next, *ith_prev;
	mach_msg_return_t ith_state;
	mach_msg_size_t ith_msize;		/* max size for recvd msg */
	struct ipc_kmsg *ith_kmsg;		/* received message */
	mach_port_seqno_t ith_seqno;		/* seqno of recvd message */

	struct ipc_kmsg_queue ith_messages;

	decl_mutex_data(,ith_lock_data)
	mach_port_t	ith_mig_reply;	/* reply port for mig */
	struct ipc_port	*ith_rpc_reply;	/* reply port for kernel RPCs */

	/* Various bits of stashed state */
	union {
		struct {
			mach_msg_option_t option;
			mach_msg_body_t *scatter_list;
			mach_msg_size_t scatter_list_size;
		} receive;
		char *other;		/* catch-all for other state */
	} saved;

	/* Timing data structures */
	timer_data_t	user_timer;	/* user mode timer */
	timer_data_t	system_timer;	/* system mode timer */
	timer_data_t	depressed_timer;/* depressed priority timer */
	timer_save_data_t user_timer_save;  /* saved user timer value */
	timer_save_data_t system_timer_save;  /* saved sys timer val. */
	unsigned int	cpu_delta;	/* cpu usage since last update */
	unsigned int	sched_delta;	/* weighted cpu usage since update */

	/* Time-outs */
	timer_elt_data_t timer;		/* timer for thread */
	timer_elt_data_t depress_timer;	/* timer for priority depression */

	/* Ast/Halt data structures */
	boolean_t	active;		/* how alive is the thread */

	/* Processor data structures */
	processor_set_t	processor_set;	/* assigned processor set */
#if	NCPUS > 1
	processor_t	bound_processor;	/* bound to processor ?*/
#endif	/* NCPUS > 1 */
#if	MACH_HOST
	boolean_t	may_assign;	/* may assignment change? */
	boolean_t	assign_active;	/* someone waiting for may_assign */
#endif	/* MACH_HOST */

#if	XKMACHKERNEL
	int		xk_type;
#endif	/* XKMACHKERNEL */

#if	NCPUS > 1
	processor_t	last_processor; /* processor this last ran on */
#if	MACH_LOCK_MON
	unsigned	lock_stack;	/* number of locks held */
#endif  /* MACH_LOCK_MON */
#endif	/* NCPUS > 1 */

	int		at_safe_point;	/* thread_abort_safely allowed */

#if	MACH_LDEBUG
	/*
	 *	Debugging:  track acquired mutexes and locks.
	 *	Because a thread can block while holding such
	 *	synchronizers, we think of the thread as
	 *	"owning" them.
	 */
#define	MUTEX_STACK_DEPTH	20
#define	LOCK_STACK_DEPTH	20
	mutex_t		*mutex_stack[MUTEX_STACK_DEPTH];
	lock_t		*lock_stack[LOCK_STACK_DEPTH];
	unsigned int	mutex_stack_index;
	unsigned int	lock_stack_index;
	unsigned	mutex_count;	/* XXX to be deleted XXX */
	boolean_t	kthread;	/* thread is a kernel thread */
#endif	/* MACH_LDEBUG */

#if	LINUX_DEV
	/*
	 * State for Linux device drivers
	 */
	void            *linux_data;    /* used by linuxdev/mach/glue/block.c */
#endif	/* LINUX_DEV */

	/*
	 * End of thread_shuttle proper
	 */

	/*
	 * Migration and thread_activation linkage information
	 */
	struct thread_activation *top_act; /* "current" thr_act */

} Thread_Shuttle;


/* typedef of thread_t is in kern/kern_types.h */
typedef struct thread_shuttle	*thread_shuttle_t;
#define THREAD_NULL		((thread_t) 0)
#define THREAD_SHUTTLE_NULL	((thread_shuttle_t)0)

#define	ith_wait_result		wait_result

#define	ith_option		saved.receive.option
#define ith_scatter_list	saved.receive.scatter_list
#define ith_scatter_list_size	saved.receive.scatter_list_size

#define ith_other		saved.other

/*
 * thread_t->at_safe_point values
 */
#define NOT_AT_SAFE_POINT		 0
#define SAFE_EXCEPTION_RETURN		-1
#define SAFE_BOOTSTRAP_RETURN		-2
#define SAFE_EXTERNAL_RECEIVE		-3
#define SAFE_THR_DEPRESS		-4
#define SAFE_SUSPENDED			-5
#define SAFE_MISCELLANEOUS		-6
#define SAFE_INTERNAL_RECEIVE		-7

extern thread_act_t active_kloaded[NCPUS];	/* "" kernel-loaded acts */
extern vm_offset_t active_stacks[NCPUS];	/* active kernel stacks */
extern vm_offset_t kernel_stack[NCPUS];

#ifndef MACHINE_STACK_STASH
/*
 * MD Macro to fill up global stack state,
 * keeping the MD structure sizes + games private
 */
#define MACHINE_STACK_STASH(stack)					\
MACRO_BEGIN								\
	mp_disable_preemption();					\
	active_stacks[cpu_number()] = (stack),				\
	    kernel_stack[cpu_number()] = (stack) + KERNEL_STACK_SIZE;	\
	mp_enable_preemption();						\
MACRO_END
#endif	/* MACHINE_STACK_STASH */

/*
 *	Kernel-only routines
 */

/* Initialize thread module */
extern void		thread_init(void);

/* Take reference on thread (make sure it doesn't go away) */
extern void		thread_reference(
				thread_t	th);

/* Release reference on thread */
extern void		thread_deallocate(
				thread_t	th);

/* Set priority of calling thread */
extern void		thread_set_own_priority(
				int		priority);
/* Reset thread's priority */
extern kern_return_t thread_priority(
				thread_act_t	thr_act,
				int		priority,
				boolean_t	set_max);

/* Reset thread's max priority */
extern kern_return_t	thread_max_priority(
				thread_act_t	thr_act,
				processor_set_t	pset,
				int		max_priority);

/* Reset thread's max priority while holding RPC locks */
extern kern_return_t	thread_max_priority_locked(
				thread_t	thread,
				processor_set_t	pset,
				int		max_priority);

/* Set a thread's priority while holding RPC locks */
extern kern_return_t	thread_priority_locked(
				thread_t	thread,
				int		priority,
				boolean_t	set_max);

/* Start a thread at specified routine */
#define thread_start(thread, start) (thread)->continuation = (start)

/* Create Kernel mode thread */
extern thread_t		kernel_thread(
				task_t	task,
				void	(*start)(void),
				void	*arg);

/* Reaps threads waiting to be destroyed */
extern void		reaper_thread(void);

#if	MACH_HOST
/* Preclude thread processor set assignement */
extern void		thread_freeze(
				thread_t	th);

/* Assign thread to a processor set */
extern void		thread_doassign(
				thread_t		thread,
				processor_set_t		new_pset,
				boolean_t		release_freeze);

/* Allow thread processor set assignement */
extern void		thread_unfreeze(
				thread_t	th);

#endif	/* MACH_HOST */

/* Insure thread always has a kernel stack */
extern void		stack_privilege(
				thread_t	th);

extern void		consider_thread_collect(void);

/*
 *	Arguments to specify aggressiveness to thread halt.
 *	Can't have MUST_HALT and SAFELY at the same time.
 */
#define	THREAD_HALT_NORMAL	0
#define	THREAD_HALT_MUST_HALT	1	/* no deadlock checks */
#define	THREAD_HALT_SAFELY	2	/* result must be restartable */

/*
 *	Macro-defined routines
 */

#define	thread_lock_init(th)	simple_lock_init(&(th)->lock, ETAP_THREAD_LOCK)
#define thread_lock(th)		simple_lock(&(th)->lock)
#define thread_unlock(th)	simple_unlock(&(th)->lock)

#define thread_should_halt(thread)	\
	(!(thread)->top_act || \
	!(thread)->top_act->active || \
	(thread)->top_act->ast & (AST_HALT|AST_TERMINATE))

/*
 * We consider a thread not preemptable if it is marked as either
 * suspended, waiting or halted.
 * XXX - when scheduling framework and such is done, the
 * thread state check can be eliminated
*/
#define thread_not_preemptable(thread)		\
	(((thread)->state & (TH_WAIT|TH_SUSP)) != 0)

#define	thread_lock_pair(ta,tb)					\
{								\
	if (ta < tb) {						\
		thread_lock (ta);				\
		thread_lock (tb);				\
	} else {						\
		thread_lock (tb);				\
		thread_lock (ta);				\
	}							\
}

#define	thread_unlock_pair(ta,tb)				\
{								\
	if (ta < tb) {						\
		thread_unlock (tb);				\
		thread_unlock (ta);				\
	} else {						\
		thread_unlock (ta);				\
		thread_unlock (tb);				\
	}							\
}

#define rpc_lock_init(th)	mutex_init(&(th)->rpc_lock, ETAP_THREAD_RPC)
#define rpc_lock(th)		mutex_lock(&(th)->rpc_lock)
#define rpc_lock_try(th)	mutex_try(&(th)->rpc_lock)
#define rpc_unlock(th)		mutex_unlock(&(th)->rpc_lock)

/*
 * Lock to cover wake_active only; like thread_lock(), is taken
 * at splsched().  Used to avoid calling into scheduler with a
 * thread_lock() held.  Precedes thread_lock() (and other scheduling-
 * related locks) in the system lock ordering.
 */
#define wake_lock_init(th)	simple_lock_init(&(th)->w_lock,ETAP_THREAD_WAKE)
#define wake_lock(th)		simple_lock(&(th)->w_lock)
#define wake_unlock(th)		simple_unlock(&(th)->w_lock)

#define current_act()						\
	(current_thread() ? current_thread()->top_act : THR_ACT_NULL)
#define current_act_fast()	(current_thread()->top_act)

static __inline__ vm_offset_t current_stack(void);
static __inline__ vm_offset_t
current_stack(void)
{
	vm_offset_t	ret;

	mp_disable_preemption();
	ret = active_stacks[cpu_number()];
	mp_enable_preemption();
	return ret;
}

/* These should be safe if only called in running task context */
#define	current_task()		(current_act_fast()->task)
#define	current_map()		(current_act_fast()->map)
#define	current_space()		(current_task()->itk_space)

extern void		pcb_module_init(void);
extern void		pcb_init(
				thread_act_t		thr_act);
extern void		pcb_terminate(
				thread_act_t		thr_act);
extern void		pcb_collect(
				thread_act_t		thr_act);
extern void		pcb_user_to_kernel(
				thread_act_t		thr_act);
extern kern_return_t	thread_setstatus(
				thread_act_t		thr_act,
				int			flavor,
				thread_state_t		tstate,
				mach_msg_type_number_t	count);
extern kern_return_t	thread_getstatus(
				thread_act_t		thr_act,
				int			flavor,
				thread_state_t		tstate,
				mach_msg_type_number_t	*count);
extern void		thread_user_to_kernel(
				thread_t		thread);

/* This routine now used only internally */
extern kern_return_t	Thread_info(
				thread_t		thread,
				thread_act_t		thr_act,
				thread_flavor_t		flavor,
				thread_info_t		thread_info_out,
				mach_msg_type_number_t	*thread_info_count);

extern kern_return_t	act_create_running(
				task_t			parent_task,
				int                     flavor,
				thread_state_t          new_state,
				unsigned int            new_state_count,
				thread_act_t			*child_act);
extern kern_return_t	thread_create_at(
				task_t			parent_task,
				thread_t		*child_thread,
				void			(*start_pos)(void));
extern void		thread_terminate_self(void);

/* Machine-dependent routines */
extern void thread_machine_init(void);
extern void thread_machine_set_current(
				thread_t		thread );
extern kern_return_t	thread_machine_create(
				thread_t thread,
				thread_act_t thr_act,
				void (*start_pos)(void));
extern void		thread_set_syscall_return(
				thread_t		thread,
				kern_return_t		retval);
extern void		thread_machine_destroy(
				thread_t thread );
extern void		thread_machine_flush(
				thread_act_t thr_act);

#endif	/* _KERN_THREAD_H_ */
