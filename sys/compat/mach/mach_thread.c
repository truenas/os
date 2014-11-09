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

int
sys_mach_syscall_thread_switch(struct thread *td, struct mach_syscall_thread_switch_args *uap)
{
	/* {
		syscallarg(mach_port_name_t) thread_name;
		syscallarg(int) option;
		syscallarg(mach_msg_timeout_t) option_time;
	} */
	int timeout;
	struct mach_emuldata *med;

	med = (struct mach_emuldata *)td->td_proc->p_emuldata;
	timeout = uap->option_time * hz / 1000;

	/*
	 * The day we will be able to find out the struct proc from
	 * the port number, try to use preempt() to call the right thread.
	 * [- but preempt() is for _involuntary_ context switches.]
	 */
	switch(uap->option) {
	case MACH_SWITCH_OPTION_NONE:
		sched_yield();
		break;

	case MACH_SWITCH_OPTION_WAIT:
		med->med_thpri = 1;
		while (med->med_thpri != 0)
			(void)tsleep(&med->med_thpri, PZERO|PCATCH,
			    "thread_switch", timeout);
		break;

	case MACH_SWITCH_OPTION_DEPRESS:
	case MACH_SWITCH_OPTION_IDLE:
		/* Use a callout to restore the priority after depression? */
		med->med_thpri = td->td_priority;
		td->td_priority = PRI_MAX_TIMESHARE;
		break;

	default:
		uprintf("sys_mach_syscall_thread_switch(): unknown option %d\n",		    uap->option);
		break;
	}
	return 0;
}

int
sys_mach_swtch_pri(struct thread *td, struct mach_swtch_pri_args *uap)
{
	/* {
		syscallarg(int) pri;
	} */

	/*
	 * Copied from preempt(9). We cannot just call preempt
	 * because we want to return mi_switch(9) return value.
	 * XXX no return value on FreeBSD
	 */
	thread_lock(td);
	if (td->td_state == TDS_RUNNING)
		td->td_proc->p_stats->p_ru.ru_nivcsw++;	/* XXXSMP */
	mi_switch(SW_VOL, NULL);
	thread_unlock(td);

	return 0;
}

int
sys_mach_swtch(struct thread *td, struct mach_swtch_args *v)
{
	struct mach_swtch_pri_args cup;

	cup.pri = 0;

	return sys_mach_swtch_pri(td, &cup);
}


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
		return mach_msg_error(args, EINVAL);

	uprintf("Unimplemented mach_thread_policy\n");

	*msglen = sizeof(*rep);
	mach_set_header(rep, req, *msglen);

	rep->rep_retval = 0;

	mach_set_trailer(rep, *msglen);

	return 0;
}

/* XXX it might be possible to use this on another task */
int
mach_thread_create_running(struct mach_trap_args *args)
{
	mach_thread_create_running_request_t *req = args->smsg;
	mach_thread_create_running_reply_t *rep = args->rmsg;
	size_t *msglen = args->rsize;
	struct thread *td = args->td;
	struct proc *p = td->td_proc;
	struct mach_create_thread_child_args mctc;
	struct mach_right *child_mr;
	struct mach_thread_emuldata *mle;
	vm_offset_t uaddr;
	int flags;
	int error;
	int end_offset;

	/* Sanity check req_count */
	end_offset = req->req_count;
	if (MACH_REQMSG_OVERFLOW(args, req->req_state[end_offset]))
		return mach_msg_error(args, EINVAL);

	/*
	 * Prepare the data we want to transmit to the child.
	 */
	mctc.mctc_flavor = req->req_flavor;
	mctc.mctc_oldtd = td;
	mctc.mctc_child_done = 0;
	mctc.mctc_state = req->req_state;

	uaddr = uvm_uarea_alloc();
	if (__predict_false(uaddr == 0))
		return ENOMEM;

	flags = 0;
	if ((error = lwp_create(l, p, uaddr, flags, NULL, 0,
	    mach_create_thread_child, (void *)&mctc, &mctc.mctc_lwp,
	    SCHED_OTHER)) != 0)
	{
		uvm_uarea_free(uaddr);
		return mach_msg_error(args, error);
	}

	/*
	 * Make the child runnable.
	 */
	PROC_LOCK(p);
	thread_lock(mctc.mctc_td);
	mctc.mctc_td->td_private = 0;
	mctc.mctc_td->td_state = LSRUN;
	sched_enqueue(mctc.mctc_td, false);
	thread_unlock(mctc.mctc_td);
	PROC_UNLOCK(p);

	/*
	 * Get the child's kernel port
	 */
	mle = mctc.mctc_lwp->l_emuldata;
	child_mr = mach_right_get(mle->mle_kernel, td, MACH_PORT_TYPE_SEND, 0);

	/*
	 * The child relies on some values in mctc, so we should not
	 * exit until it is finished with it. We catch signals so that
	 * the process can be killed with kill -9, but we loop to avoid
	 * spurious wakeups due to other signals.
	 */
	while(mctc.mctc_child_done == 0)
		(void)tsleep(&mctc.mctc_child_done,
		    PZERO|PCATCH, "mach_thread", 0);

	*msglen = sizeof(*rep);
	mach_set_header(rep, req, *msglen);
	mach_add_port_desc(rep, child_mr->mr_name);
	mach_set_trailer(rep, *msglen);

	return 0;
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
	if (req->req_count > 12)
		return mach_msg_error(args, EINVAL);

	rep->rep_count = req->req_count;

	*msglen = sizeof(*rep) + ((req->req_count - 12) * sizeof(int));
	mach_set_header(rep, req, *msglen);

	switch (req->req_flavor) {
	case MACH_THREAD_BASIC_INFO: {
		struct mach_thread_basic_info *tbi;

		if (req->req_count != (sizeof(*tbi) / sizeof(int))) /* 10 */
			return mach_msg_error(args, EINVAL);

		tbi = (struct mach_thread_basic_info *)rep->rep_out;
		tbi->user_time.seconds = tp->p_uticks * hz / 1000000;
		tbi->user_time.microseconds =
		    (tp->p_uticks) * hz - tbi->user_time.seconds;
		tbi->system_time.seconds = tp->p_sticks * hz / 1000000;
		tbi->system_time.microseconds =
		    (tp->p_sticks) * hz - tbi->system_time.seconds;
		tbi->cpu_usage = tp->p_pctcpu;
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
		tbi->sleep_time = tl->l_slptime;
		break;
	}

	case MACH_THREAD_SCHED_TIMESHARE_INFO: {
		struct mach_policy_timeshare_info *pti;

		if (req->req_count != (sizeof(*pti) / sizeof(int))) /* 5 */
			return mach_msg_error(args, EINVAL);

		pti = (struct mach_policy_timeshare_info *)rep->rep_out;

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

	mach_set_trailer(rep, *msglen);
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
	if (req->req_count > 144)
		return mach_msg_error(args, EINVAL);

	if ((error = mach_thread_get_state_machdep(tl,
	    req->req_flavor, &rep->rep_state, &size)) != 0)
		return mach_msg_error(args, error);

	rep->rep_count = size / sizeof(int);
	*msglen = sizeof(*rep) + ((req->req_count - 144) * sizeof(int));
	mach_set_header(rep, req, *msglen);
	mach_set_trailer(rep, *msglen);

	return 0;
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
	end_offset = req->req_count;
	if (MACH_REQMSG_OVERFLOW(args, req->req_state[end_offset]))
		return mach_msg_error(args, EINVAL);

	if ((error = mach_thread_set_state_machdep(tl,
	    req->req_flavor, &req->req_state)) != 0)
		return mach_msg_error(args, error);

	*msglen = sizeof(*rep);
	mach_set_header(rep, req, *msglen);

	rep->rep_retval = 0;

	mach_set_trailer(rep, *msglen);

	return 0;
}

int
mach_thread_suspend(struct mach_trap_args *args)
{
	mach_thread_suspend_request_t *req = args->smsg;
	mach_thread_suspend_reply_t *rep = args->rmsg;
	size_t *msglen = args->rsize;
	struct thread *td = args->td;
	struct thread *ttd = args->ttd;
	struct proc *p = ttd->td_proc;
	int error;

	PROC_LOCK(p);
	PROC_SLOCK(p);
	error = thread_suspend_switch(ttd);
	PROC_SUNLOCK(p);
	PROC_UNLOCK(p);

	*msglen = sizeof(*rep);
	mach_set_header(rep, req, *msglen);
	rep->rep_retval = native_to_mach_errno[error];
	mach_set_trailer(rep, *msglen);

	return 0;
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
	rep->rep_retval = 0;
	mach_set_trailer(rep, *msglen);

	return 0;
}

int
mach_thread_abort(struct mach_trap_args *args)
{
	mach_thread_abort_request_t *req = args->smsg;
	mach_thread_abort_reply_t *rep = args->rmsg;
	size_t *msglen = args->rsize;
	struct thread *ttd = args->ttd;

	lwp_exit(tl);

	*msglen = sizeof(*rep);
	mach_set_header(rep, req, *msglen);
	rep->rep_retval = 0;
	mach_set_trailer(rep, *msglen);

	return 0;
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

	limit_count_offset = req->req_base_count;
	if (MACH_REQMSG_OVERFLOW(args, req->req_base[limit_count_offset]))
		return mach_msg_error(args, EINVAL);

	limit_count = req->req_base[limit_count_offset];
	limit_offset = limit_count_offset +
	    (sizeof(req->req_limit_count) / sizeof(req->req_base[0]));
	limit = &req->req_base[limit_offset];
	if (MACH_REQMSG_OVERFLOW(args, limit[limit_count]))
		return mach_msg_error(args, EINVAL);

	mn = req->req_pset.name;
	if ((mr = mach_right_check(mn, tl, MACH_PORT_TYPE_ALL_RIGHTS)) == NULL)
		return mach_msg_error(args, EINVAL);

	*msglen = sizeof(*rep);
	mach_set_header(rep, req, *msglen);
	rep->rep_retval = 0;
	mach_set_trailer(rep, *msglen);

	return 0;
}

