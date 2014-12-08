/*-
 * Copyright (c) 2002-2003 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Emmanuel Dreyfus
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/queue.h>
#include <sys/resource.h>
#include <sys/resourcevar.h>
#include <sys/rwlock.h>
#include <sys/sched.h>
#include <sys/sleepqueue.h>
#include <sys/signal.h>

#include <compat/mach/mach_types.h>
#include <compat/mach/mach_message.h>
#include <compat/mach/mach_exec.h>
#include <compat/mach/mach_clock.h>
#include <compat/mach/mach_port.h>
#include <compat/mach/mach_task.h>
#include <compat/mach/mach_thread.h>
#include <compat/mach/mach_errno.h>
#include <compat/mach/mach_services.h>
#include <compat/mach/mach_proto.h>

#include <sys/mach/ipc/ipc_kmsg.h>
#include <sys/mach/thread.h>

#define MT_SETRUNNABLE 0x1

#pragma clang diagnostic ignored "-Wunused-parameter"
#ifdef notyet
/*
 * Am assuming that Mach lacks the concept of uninterruptible
 * sleep - this may need to be changed back to what is in pci_pass.c
 */
static int
_intr_tdsigwakeup(struct thread *td, int intrval)
{
	struct proc *p = td->td_proc;
	int rc = 0;

	PROC_SLOCK(p);
	thread_lock(td);
	if (TD_ON_SLEEPQ(td)) {
		/*
		 * Give low priority threads a better chance to run.
		 */
		if (td->td_priority > PUSER)
			sched_prio(td, PUSER);

		sleepq_abort(td, intrval);
		rc = 1;
	}
	PROC_SUNLOCK(p);
	thread_unlock(td);
	return (rc);
}
#endif
#if 0
static int
_create_thread(struct thread *td, int (*func)(void *), void *args, struct thread **rettd, int flags)
{
	struct thread *newtd;
	struct proc *p;
	int error;

	p = td->td_proc;

	/* Have race condition but it is cheap. */
	if (p->p_numthreads >= max_threads_per_proc) {
		return (EPROCLIM);
	}

#ifdef RACCT
	PROC_LOCK(td->td_proc);
	error = racct_add(p, RACCT_NTHR, 1);
	PROC_UNLOCK(td->td_proc);
	if (error != 0)
		return (EPROCLIM);
#endif

	/* Initialize our td */
	newtd = thread_alloc(0);
	if (newtd == NULL) {
		error = ENOMEM;
		goto fail;
	}

	cpu_set_upcall(newtd, td);

	bzero(&newtd->td_startzero,
	    __rangeof(struct thread, td_startzero, td_endzero));
	bcopy(&td->td_startcopy, &newtd->td_startcopy,
	    __rangeof(struct thread, td_startcopy, td_endcopy));
	newtd->td_proc = td->td_proc;
	newtd->td_ucred = crhold(td->td_ucred);

#if 0
	/* Set up our machine context. */
	stack.ss_sp = stack_base;
	stack.ss_size = stack_size;
	/* Set upcall address to user thread entry function. */
	cpu_set_upcall_kse(newtd, start_func, arg, &stack);
	/* Setup user TLS address and TLS pointer register. */
	error = cpu_set_user_tls(newtd, tls_base);
	if (error != 0) {
		thread_free(newtd);
		crfree(td->td_ucred);
		goto fail;
	}
#endif
	PROC_LOCK(td->td_proc);
	td->td_proc->p_flag |= P_HADTHREADS;
	thread_link(newtd, p);
	bcopy(p->p_comm, newtd->td_name, sizeof(newtd->td_name));
	error = (func)(args);

	thread_lock(td);
	/* let the scheduler know about these things. */
	sched_fork_thread(td, newtd);
	thread_unlock(td);
	if (P_SHOULDSTOP(p))
		newtd->td_flags |= TDF_ASTPENDING | TDF_NEEDSUSPCHK;
	PROC_UNLOCK(p);

	tidhash_add(newtd);

	if (flags & MT_SETRUNNABLE) {
		thread_lock(newtd);
		TD_SET_CAN_RUN(newtd);
		sched_add(newtd, SRQ_BORING);
		thread_unlock(newtd);
	}
	*rettd = newtd;
	return (0);

fail:
#ifdef RACCT
	PROC_LOCK(p);
	racct_sub(p, RACCT_NTHR, 1);
	PROC_UNLOCK(p);
#endif
	return (error);
}
#endif

int
mach_thread_switch(struct thread *td, mach_port_name_t thread_name, int option, mach_msg_timeout_t option_time)
{
	int timeout;
	struct mach_emuldata *med;

	med = (struct mach_emuldata *)td->td_proc->p_emuldata;
	timeout = option_time * hz / 1000;

	/*
	 * The day we will be able to find out the struct proc from
	 * the port number, try to use preempt() to call the right thread.
	 * [- but preempt() is for _involuntary_ context switches.]
	 */
	switch(option) {
	case MACH_SWITCH_OPTION_NONE:
		sched_relinquish(curthread);
		break;

	case MACH_SWITCH_OPTION_WAIT:
		med->med_thpri = 1;
		while (med->med_thpri != 0) {
			rw_wlock(&med->med_rightlock);
			(void)msleep(&med->med_thpri, &med->med_rightlock, PZERO|PCATCH,
						 "thread_switch", timeout);
			rw_wunlock(&med->med_rightlock);
		}
		break;

	case MACH_SWITCH_OPTION_DEPRESS:
	case MACH_SWITCH_OPTION_IDLE:
		/* Use a callout to restore the priority after depression? */
		med->med_thpri = td->td_priority;
		td->td_priority = PRI_MAX_TIMESHARE;
		break;

	default:
		uprintf("sys_mach_syscall_thread_switch(): unknown option %d\n", option);
		break;
	}
	return (0);
}

int
mach_swtch_pri(struct thread *td, int pri)
{

	thread_lock(td);
	if (td->td_state == TDS_RUNNING)
		td->td_proc->p_stats->p_cru.ru_nivcsw++;	/* XXXSMP */
	mi_switch(SW_VOL, NULL);
	thread_unlock(td);

	return (0);
}

#if 0
int
mach_thread_policy(struct mach_trap_args *args)
{
	mach_thread_policy_request_t *req = args->smsg;
	mach_thread_policy_reply_t *rep = args->rmsg;
	size_t *msglen = args->rsize;
	int end_offset;

	/* Sanity check req_count */
	end_offset = req->req_count +
		     (sizeof(req->req_setlimit) / sizeof(req->req_base[0]));
	if (MACH_REQMSG_OVERFLOW(args, req->req_base[end_offset]))
		return (mach_msg_error(args, EINVAL));

	uprintf("Unimplemented mach_thread_policy\n");

	*msglen = sizeof(*rep);
	mach_set_header(rep, req, *msglen);

	rep->rep_retCode = 0;
	return (0);
}

int
mach_thread_create(struct mach_trap_args *args)
{

	return (ENOSYS);
}

/* XXX it might be possible to use this on another task */
int
mach_thread_create_running(struct mach_trap_args *args)
{
	mach_thread_create_running_request_t *req = args->smsg;
	mach_thread_create_running_reply_t *rep = args->rmsg;
	size_t *msglen = args->rsize;
	struct thread *td = args->td;
	struct mach_create_thread_child_args mctc;
	struct mach_right *child_mr;
	struct mach_thread_emuldata *mle;
	int error;
	int end_offset;

	/* Sanity check req_count */
	end_offset = req->req_new_stateCnt;
	if (MACH_REQMSG_OVERFLOW(args, req->req_new_state[end_offset]))
		return (mach_msg_error(args, EINVAL));

	/*
	 * Prepare the data we want to transmit to the child.
	 */
	mctc.mctc_flavor = req->req_flavor;
	mctc.mctc_oldtd = td;
	mctc.mctc_child_done = 0;
	mctc.mctc_state = req->req_new_state;
	if ((error = _create_thread(td, cpu_mach_create_thread, &mctc, &mctc.mctc_td, MT_SETRUNNABLE))) {
		return (mach_msg_error(args, error));
	}

	/*
	 * Get the child's kernel port
	 */
	mle = mctc.mctc_td->td_emuldata;
	child_mr = mach_right_get(mle->mle_kernel, td, MACH_PORT_TYPE_SEND, 0);

	/*
	 * The child relies on some values in mctc, so we should not
	 * exit until it is finished with it. We catch signals so that
	 * the process can be killed with kill -9, but we loop to avoid
	 * spurious wakeups due to other signals.
	 */
	while(mctc.mctc_child_done == 0) {
		mtx_lock(&child_mr->mr_lock);
		(void)msleep(&mctc.mctc_child_done,
					 &child_mr->mr_lock,
		    PZERO|PCATCH, "mach_thread", 0);
		mtx_unlock(&child_mr->mr_lock);
	}
	*msglen = sizeof(*rep);
	mach_set_header(rep, req, *msglen);
	mach_add_port_desc(rep, child_mr->mr_name);
	return (0);
}

int
mach_thread_info(struct mach_trap_args *args)
{
	mach_thread_info_request_t *req = args->smsg;
	mach_thread_info_reply_t *rep = args->rmsg;
	size_t *msglen = args->rsize;
	struct thread *td = args->td;
	struct thread *ttd = args->ttd;
	struct proc *tp = ttd->td_proc;

	/* Sanity check req->req_count */
	if (req->req_thread_info_outCnt > 12)
		return (mach_msg_error(args, EINVAL));

	rep->rep_thread_info_outCnt = req->req_thread_info_outCnt;

	*msglen = sizeof(*rep) + ((req->req_thread_info_outCnt - 12) * sizeof(int));
	mach_set_header(rep, req, *msglen);

	switch (req->req_flavor) {
	case MACH_THREAD_BASIC_INFO: {
		struct mach_thread_basic_info *tbi;

		if (req->req_thread_info_outCnt != (sizeof(*tbi) / sizeof(int))) /* 10 */
			return (mach_msg_error(args, EINVAL));

		tbi = (struct mach_thread_basic_info *)&rep->rep_thread_info_out;
		/* may not actually be up to date ... */
		tbi->user_time.seconds = tp->p_ru.ru_utime.tv_sec;
		tbi->user_time.microseconds = tp->p_ru.ru_utime.tv_usec;
		tbi->system_time.seconds = tp->p_prev_runtime * hz / 1000000;
		tbi->system_time.microseconds =
		    (tp->p_prev_runtime) * hz - tbi->system_time.seconds;
#ifdef notyet		
		tbi->cpu_usage = tp->p_pctcpu;
#endif
		tbi->policy = MACH_THREAD_STANDARD_POLICY;

		/* XXX this is not very accurate */
		tbi->run_state = MACH_TH_STATE_RUNNING;
		tbi->flags = 0;
		switch (td->td_state) {
		case TDS_RUNNING:
			tbi->run_state = MACH_TH_STATE_RUNNING;
			break;
		case TDS_INACTIVE:
			tbi->run_state = MACH_TH_STATE_STOPPED;
			break;
		case TDS_INHIBITED:
			tbi->run_state = MACH_TH_STATE_WAITING;
			break;
		case TDS_CAN_RUN:
			tbi->run_state = MACH_TH_STATE_RUNNING;
			tbi->flags = MACH_TH_FLAGS_IDLE;
			break;
		default:
			break;
		}

		tbi->suspend_count = 0;
		tbi->sleep_time = td->td_slptick;
		break;
	}

	case MACH_THREAD_SCHED_TIMESHARE_INFO: {
		struct mach_policy_timeshare_info *pti;

		if (req->req_thread_info_outCnt != (sizeof(*pti) / sizeof(int))) /* 5 */
			return (mach_msg_error(args, EINVAL));

		pti = (struct mach_policy_timeshare_info *)&rep->rep_thread_info_out;

		pti->max_priority = ttd->td_priority;
		pti->base_priority = ttd->td_priority;
		pti->cur_priority = ttd->td_priority;
		pti->depressed = 0;
		pti->depress_priority = ttd->td_priority;
		break;
	}

	case MACH_THREAD_SCHED_RR_INFO:
	case MACH_THREAD_SCHED_FIFO_INFO:
		uprintf("Unimplemented thread_info flavor %d\n",
		    req->req_flavor);
	default:
		return (mach_msg_error(args, EINVAL));
		break;
	}
	return (0);
}

int
mach_thread_get_state(struct mach_trap_args *args)
{
	mach_thread_get_state_request_t *req = args->smsg;
	mach_thread_get_state_reply_t *rep = args->rmsg;
	size_t *msglen = args->rsize;
	struct thread *ttd = args->ttd;
	int error;
	int size;

	/* Sanity check req->req_count */
	if (req->req_old_stateCnt > 224)
		return (mach_msg_error(args, EINVAL));

	if ((error = cpu_mach_thread_get_state(ttd,
	    req->req_flavor, &rep->rep_old_state, &size)) != 0)
		return (mach_msg_error(args, error));

	rep->rep_old_stateCnt = size / sizeof(int);
	*msglen = sizeof(*rep) + ((req->req_old_stateCnt - 224) * sizeof(int));
	mach_set_header(rep, req, *msglen);
	return (0);
}

int
mach_thread_set_state(struct mach_trap_args *args)
{
	mach_thread_set_state_request_t *req = args->smsg;
	mach_thread_set_state_reply_t *rep = args->rmsg;
	size_t *msglen = args->rsize;
	struct thread *ttd = args->ttd;
	int error;
	int end_offset;

	/* Sanity check req_count */
	end_offset = req->req_new_stateCnt;
	if (MACH_REQMSG_OVERFLOW(args, req->req_new_state[end_offset]))
		return (mach_msg_error(args, EINVAL));

	if ((error = cpu_mach_thread_set_state(ttd,
	    req->req_flavor, &req->req_new_state)) != 0)
		return (mach_msg_error(args, error));

	*msglen = sizeof(*rep);
	mach_set_header(rep, req, *msglen);

	rep->rep_retCode = 0;
	return (0);
}

int
mach_thread_suspend(struct mach_trap_args *args)
{
	mach_thread_suspend_request_t *req = args->smsg;
	mach_thread_suspend_reply_t *rep = args->rmsg;
	size_t *msglen = args->rsize;
	struct thread *ttd = args->ttd;
	struct proc *p = ttd->td_proc;

	PROC_LOCK(p);
	PROC_SLOCK(p);
	thread_suspend_switch(ttd);
	PROC_SUNLOCK(p);
	PROC_UNLOCK(p);

	*msglen = sizeof(*rep);
	mach_set_header(rep, req, *msglen);
	rep->rep_retCode = native_to_mach_errno[0];
	return (0);
}

int
mach_thread_resume(struct mach_trap_args *args)
{
	mach_thread_resume_request_t *req = args->smsg;
	mach_thread_resume_reply_t *rep = args->rmsg;
	size_t *msglen = args->rsize;
	struct thread *ttd = args->ttd;
	struct proc *p = ttd->td_proc;

	PROC_SLOCK(p);
	thread_lock(ttd);
	thread_unsuspend_one(ttd);
	thread_unlock(ttd);
	PROC_SUNLOCK(p);

	*msglen = sizeof(*rep);
	mach_set_header(rep, req, *msglen);
	rep->rep_retCode = 0;
	return (0);
}



int
mach_thread_abort(struct mach_trap_args *args)
{
	mach_thread_abort_request_t *req = args->smsg;
	mach_thread_abort_reply_t *rep = args->rmsg;
	size_t *msglen = args->rsize;
	struct thread *ttd = args->ttd;

	_intr_tdsigwakeup(ttd, EINTR);
	*msglen = sizeof(*rep);
	mach_set_header(rep, req, *msglen);
	rep->rep_retCode = 0;
	return (0);
}

int
mach_thread_set_policy(struct mach_trap_args *args)
{
	mach_thread_set_policy_request_t *req = args->smsg;
	mach_thread_set_policy_reply_t *rep = args->rmsg;
	size_t *msglen = args->rsize;
	struct thread *ttd = args->ttd;
	mach_port_t mn;
	struct mach_right *mr;
	int limit_count_offset, limit_offset;
	int limit_count;
	int *limit;

	limit_count_offset = req->req_baseCnt;
	if (MACH_REQMSG_OVERFLOW(args, req->req_base[limit_count_offset]))
		return (mach_msg_error(args, EINVAL));

	limit_count = req->req_base[limit_count_offset];
	limit_offset = limit_count_offset +
	    (sizeof(req->req_limitCnt) / sizeof(req->req_base[0]));
	limit = &req->req_base[limit_offset];
	if (MACH_REQMSG_OVERFLOW(args, limit[limit_count]))
		return (mach_msg_error(args, EINVAL));

	mn = req->req_pset.name;
	if ((mr = mach_right_check(mn, ttd, MACH_PORT_TYPE_ALL_RIGHTS)) == NULL)
		return (mach_msg_error(args, EINVAL));

	*msglen = sizeof(*rep);
	mach_set_header(rep, req, *msglen);
	rep->rep_retCode = 0;
	return (0);
}
#endif

void
thread_go(thread_t thread)
{
	/* XXX */
}

void
thread_block(void (*continuation)(void))
{
	/* XXX */
}

void
thread_will_wait_with_timeout(thread_t thread, int timeout)
{


}


void
thread_will_wait(thread_t thread)
{
	/* XXX */
}
