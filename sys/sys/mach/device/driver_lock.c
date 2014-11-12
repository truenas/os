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

#include <cpus.h>
#include <mach_kdb.h>
#include <mach_kgdb.h>

#include <mach_ldebug.h>
#include <mach/boolean.h>
#include <kern/assert.h>
#include <kern/thread.h>
#include <kern/cpu_data.h>
#include <kern/etap_options.h>
#include <kern/lock.h>
#include <kern/sched_prim.h>
#include <device/driver_lock.h>
#if	MACH_KDB
#include <machine/db_machdep.h>
#if	NCPUS > 1
extern int kdb_active[NCPUS];
#endif	/* NCPUS > 1 */
#endif	/* MACH_KDB */

#if NCPUS > 1 || MACH_LDEBUG || ETAP_LOCK_TRACE

/*
 * driver_lock_init
 *
 * Purpose : Initialize a driver lock
 * Returns : Nothing
 *
 * IN_arg  : dl    ==> Driver lock
 *	     unit  ==> Driver unit
 *	     start ==> Function to call for asynchronous start operation
 *	     intr  ==> Function to call for asynchronous interrupt operation
 *	     timeo ==> Function to call for asynchronous timeout operation
 */
void
driver_lock_init(
    driver_lock_t	*dl, 
    int			unit,
    void		(*start)(int),
    void		(*intr)(int),
    void		(*timeo)(int),
    void		(*callb)(int))
{
    simple_lock_init(&dl->dl_lock, ETAP_IO_DEVINS);
    dl->dl_unit = unit;
    dl->dl_count = 0;
    dl->dl_pending = 0;
    dl->dl_thread = current_thread();
    dl->dl_op[DRIVER_OP_START] = start;
    dl->dl_op[DRIVER_OP_INTR] = intr;
    dl->dl_op[DRIVER_OP_TIMEO] = timeo;
    dl->dl_op[DRIVER_OP_CALLB] = callb;
}

/*
 * driver_lock
 *
 * Purpose : Try to acquire a driver lock for a driver operation, and
 *		conditionally mark the driver operation as pending
 * Returns : TRUE/FALSE whether the lock is available or busy
 *
 * IN_arg  : dl  ==> Driver lock
 *	     op  ==> Driver operation
 *	     try ==> Don't mark this operation as pending if driver lock busy
 */
boolean_t
driver_lock(
    driver_lock_t	*dl, 
    unsigned int 	op,
    boolean_t		try)
{
    thread_t self = current_thread();

    assert(op <= DRIVER_OP_LAST);
    simple_lock(&dl->dl_lock);
    if (dl->dl_count == 0)
	dl->dl_thread = self;

    else if (dl->dl_thread != self) {
	if (try) {
	    simple_unlock(&dl->dl_lock);
	    return (FALSE);
	}
	if (op != DRIVER_OP_WAIT) {
	    dl->dl_pending |= (1 << op);
	    simple_unlock(&dl->dl_lock);
	    return (FALSE);
	}
	/*
	 * Wait while the lock is held
	 */
	while (dl->dl_count > 0) {
	    dl->dl_pending |= (1 << DRIVER_OP_WAIT);
	    assert_wait((event_t)&dl->dl_count, FALSE);
	    simple_unlock(&dl->dl_lock);
	    thread_block((void (*)(void))0);
	    simple_lock(&dl->dl_lock);
	}
	assert(dl->dl_pending == 0);
	dl->dl_thread = self;

    } else
	assert(op != DRIVER_OP_WAIT);

    dl->dl_count++;
    simple_unlock(&dl->dl_lock);
    return (TRUE);
}

/*
 * driver_unlock
 *
 * Purpose : Release a driver lock, starting pending driver operations
 * Returns : Nothing
 *
 * IN_arg  : dl ==> Driver lock
 */
void
driver_unlock(
    driver_lock_t *dl)
{
    unsigned int i;

    simple_lock(&dl->dl_lock);
    if (dl->dl_count == 1) {
	/*
	 * Scan for asynchronous operation to do only during last unlock
	 */
	while (dl->dl_pending & ~(1 << DRIVER_OP_WAIT))
	    for (i = 0; i < DRIVER_OP_LAST; i++)
		if (dl->dl_pending & (1 << i)) {
		    dl->dl_pending &= ~(1 << i);
		    simple_unlock(&dl->dl_lock);
		    (*dl->dl_op[i])(dl->dl_unit);
		    simple_lock(&dl->dl_lock);
		    assert(dl->dl_count == 1);
		}
	/*
	 * No more asynchronous operation to do, so wakeup waiting threads
	 */
	if (dl->dl_pending & (1 << DRIVER_OP_WAIT)) {
	    dl->dl_pending &= ~(1 << DRIVER_OP_WAIT);
	    thread_wakeup((event_t)&dl->dl_count);
	}
    } else
	assert(dl->dl_count != 0);
    dl->dl_count--;
    simple_unlock(&dl->dl_lock);
}


#if	defined(POWERMAC) && MACH_KDB
extern struct funnel scc_funnel;
#endif	/* POWERMAC && MACH_KDB */

#if	MACH_LDEBUG
int funnel_loop_max = 20000000;
#endif	/* MACH_LDEBUG */

void
funnel_init(
	struct funnel	*f,
	processor_t	bound_processor)
{
	simple_lock_init(&f->funnel_lock, ETAP_MISC_MP_IO);
#if 1
	f->funnel_bound_processor = bound_processor;
#else
	f->funnel_bound_processor = PROCESSOR_NULL;
#endif
	f->funnel_count = 0;
	f->funnel_cpu = -1;
#if	MACH_LDEBUG
	simple_lock_init(&f->funnel_debug_lock, ETAP_MISC_MP_IO);
#endif	/* MACH_LDEBUG */
}

void
funnel_enter(
	struct funnel	*f,
	processor_t	*saved_processor)
{
	int		i;
	spl_t		s;
	processor_t	bound_processor;
	thread_t	thread;

	mp_disable_preemption();

#if	MACH_KDB
#ifdef	POWERMAC
	if (f == &scc_funnel) {
		mp_enable_preemption();
		return;
	}
#endif	/* POWERMAC */

	if (db_active
#if	NCPUS > 1
	    || kdb_active[cpu_number()]
#endif	/* NCPUS > 1 */
	    ) {
		mp_enable_preemption();
		return;
	}
#endif	/* MACH_KDB */

	*saved_processor = PROCESSOR_NULL;
	bound_processor = f->funnel_bound_processor;
	thread = current_thread();
	if (bound_processor != PROCESSOR_NULL) {
		if (thread == THREAD_NULL ||
		    thread == current_processor()->idle_thread) {
			mp_enable_preemption();
			return;
		}
#if	NCPUS > 1
		*saved_processor = thread->bound_processor;
		thread_bind(thread, bound_processor);
		if (current_processor() != bound_processor) {
			/* block until we get the CPU we want */
			mp_enable_preemption();
			thread_block((void (*)(void)) 0);
		} else {
			mp_enable_preemption();
		}
#else	/* NCPUS > 1 */
		mp_enable_preemption();
#endif	/* NCPUS > 1 */
		return ;
	}

	if (thread == THREAD_NULL && cpu_number() == master_cpu) {
		mp_enable_preemption();
		return;
	}

    restart:
	while (f->funnel_count &&
	       f->funnel_cpu != cpu_number()) {
#if	MACH_LDEBUG
		if (i++ == funnel_loop_max) {
			i = 0;
			printf("Looping trying to enter funnel 0x%x\n", f);
#if	MACH_KDB || MACH_KGDB
			Debugger("funnel_enter: deadlock ?");
#endif	/* MACH_KDB || MACH_KGDB */
		}
#endif	/* MACH_LDEBUG */
	}
	s = splhigh();
	simple_lock(&f->funnel_lock);
	if (f->funnel_count && f->funnel_cpu != cpu_number()) {
		/* we were beaten by another cpu */
		simple_unlock(&f->funnel_lock);
		splx(s);
		goto restart;
	}
	if (f->funnel_count == 0) {
		mp_disable_preemption();/* unpreemptible while in funnel */
#if	MACH_LDEBUG
		simple_lock(&f->funnel_debug_lock);
#endif	/* MACH_LDEBUG */
		f->funnel_cpu = cpu_number();
	}
	f->funnel_count++;
	simple_unlock(&f->funnel_lock);
	splx(s);
	mp_enable_preemption();
}

void
funnel_exit(
	struct funnel	*f,
	processor_t	saved_processor)
{
	spl_t		s;
	thread_t	thread;

	mp_disable_preemption();

#if	MACH_KDB
#ifdef	POWERMAC
	if (f == &scc_funnel) {
		mp_enable_preemption();
		return;
	}
#endif	/* POWERMAC */

	if (db_active
#if	NCPUS > 1
	    || kdb_active[cpu_number()]
#endif	/* NCPUS > 1 */
	    ) {
		mp_enable_preemption();
		return;
	}
#endif	/* MACH_KDB */

	thread = current_thread();
	if (f->funnel_bound_processor != PROCESSOR_NULL) {
		if (thread == THREAD_NULL ||
		    thread == current_processor()->idle_thread) {
			mp_enable_preemption();
			return;
		}
#if	NCPUS > 1
		thread_bind(thread, saved_processor);
		if (saved_processor != PROCESSOR_NULL &&
		    saved_processor != current_processor()) {
			mp_enable_preemption();
			thread_block((void (*)(void)) 0);
		} else {
			mp_enable_preemption();
		}
#else	/* NCPUS > 1 */
		mp_enable_preemption();
#endif	/* NCPUS > 1 */
		return;
	}

	if (thread == THREAD_NULL && cpu_number() == master_cpu) {
		mp_enable_preemption();
		return;
	}

	s = splhigh();
	simple_lock(&f->funnel_lock);
	assert(funnel_in_use(f));
	f->funnel_count--;
	if (f->funnel_count == 0) {
		f->funnel_cpu = -1;
#if	MACH_LDEBUG
		simple_unlock(&f->funnel_debug_lock);
#endif	/* MACH_LDEBUG */
		mp_enable_preemption();	/* no longer unpreemptible */
	}
	simple_unlock(&f->funnel_lock);
	splx(s);
	mp_enable_preemption();
}

int
funnel_escape(
	struct funnel	*f)
{
	spl_t		s;
	int		retval;
	thread_t	thread;

	mp_disable_preemption();

#if	MACH_KDB
#ifdef	POWERMAC
	if (f == &scc_funnel) {
		mp_enable_preemption();
		return 1;
	}
#endif	/* POWERMAC */

	if (db_active
#if	NCPUS > 1
	    || kdb_active[cpu_number()]
#endif	/* NCPUS > 1 */
	    ) {
		mp_enable_preemption();
		return 1;
	}
#endif	/* MACH_KDB */

	thread = current_thread();
	if (f->funnel_bound_processor != PROCESSOR_NULL) {
		mp_enable_preemption();
		return 1;
	}

	if (thread == THREAD_NULL && cpu_number() == master_cpu) {
		mp_enable_preemption();
		return 1;
	}

	s = splhigh();
	simple_lock(&f->funnel_lock);
	assert(funnel_in_use(f));
	retval = f->funnel_count;
	f->funnel_count = 0;
	f->funnel_cpu = -1;
#if	MACH_LDEBUG
	simple_unlock(&f->funnel_debug_lock);
#endif	/* MACH_LDEBUG */
	mp_enable_preemption();	/* no longer unpreemptible */
	simple_unlock(&f->funnel_lock);
	splx(s);
	mp_enable_preemption();
	return retval;
}

void
funnel_reenter(
	struct funnel	*f,
	int		count)
{
	spl_t		s;
	processor_t	saved_processor;
	thread_t	thread;

	mp_disable_preemption();

#if	MACH_KDB
#ifdef	POWERMAC
	if (f == &scc_funnel) {
		mp_enable_preemption();
		return;
	}
#endif	/* POWERMAC */

	if (db_active
#if	NCPUS > 1
	    || kdb_active[cpu_number()]
#endif	/* NCPUS > 1 */
	    ) {
		mp_enable_preemption();
		return;
	}
#endif	/* MACH_KDB */

	thread = current_thread();
	if (f->funnel_bound_processor != PROCESSOR_NULL) {
		mp_enable_preemption();
		return;
	}

	if (thread == THREAD_NULL && cpu_number() == master_cpu) {
		mp_enable_preemption();
		return;
	}

	s = splhigh();
	assert(!funnel_in_use(f));
	funnel_enter(f, &saved_processor);
	simple_lock(&f->funnel_lock);
	assert(f->funnel_count == 1);
	assert(f->funnel_cpu == cpu_number());
	f->funnel_count = count;
	simple_unlock(&f->funnel_lock);
	splx(s);
	mp_enable_preemption();
}

boolean_t
funnel_in_use(
	struct funnel	*f)
{
	thread_t	thread;
	boolean_t	retval;

	mp_disable_preemption();

#if	MACH_KDB
#ifdef	POWERMAC
	if (f == &scc_funnel) {
		mp_enable_preemption();
		return TRUE;
	}
#endif	/* POWERMAC */

	if (db_active
#if	NCPUS > 1
	    || kdb_active[cpu_number()]
#endif	/* NCPUS > 1 */
	    ) {
		mp_enable_preemption();
		return TRUE;	/* XXX */
	}
#endif	/* MACH_KDB */

	thread = current_thread();
	if (f->funnel_bound_processor != PROCESSOR_NULL) {
#if	NCPUS > 1
		retval = (current_processor() == f->funnel_bound_processor &&
			  (thread == THREAD_NULL ||
			   thread == current_processor()->idle_thread ||
			   thread->bound_processor ==
			   f->funnel_bound_processor));
#else	/* NCPUS > 1 */
		retval = (current_processor() == f->funnel_bound_processor);
#endif	/* NCPUS > 1 */
		mp_enable_preemption();
		return retval;
	}

	if (thread == THREAD_NULL && cpu_number() == master_cpu) {
		mp_enable_preemption();
		return TRUE;	/* XXX */
	}

	/* no need to lock: that's just a debugging hint... */
	retval = (f->funnel_count && f->funnel_cpu == cpu_number());
	mp_enable_preemption();
	return retval;
}
#endif /* NCPUS > 1 || MACH_LDEBUG || ETAP_LOCK_TRACE */
