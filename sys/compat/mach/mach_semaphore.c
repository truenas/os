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
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/rwlock.h>
#include <sys/signal.h>

#include <vm/uma.h>

#include <compat/mach/mach_types.h>
#include <compat/mach/mach_message.h>
#include <compat/mach/mach_semaphore.h>
#include <compat/mach/mach_clock.h>
#include <compat/mach/mach_errno.h>
#include <compat/mach/mach_port.h>
#include <compat/mach/mach_services.h>
#include <compat/mach/mach_proto.h>

/* Semaphore list, lock, zones */
static LIST_HEAD(mach_semaphore_list, mach_semaphore) mach_semaphore_list;
static struct rwlock mach_semaphore_list_lock;
static uma_zone_t mach_semaphore_list_zone;
static uma_zone_t mach_waiting_lwp_zone;

/* Function to manipulate them */
static struct mach_semaphore *mach_semaphore_get(int, int);
static void mach_semaphore_put(struct mach_semaphore *);
static struct mach_waiting_lwp *mach_waiting_lwp_get
    (struct lwp *, struct mach_semaphore *);
static void mach_waiting_lwp_put
    (struct mach_waiting_lwp *, struct mach_semaphore *, int);

int
mach_sys_semaphore_wait_trap(struct thread *td, struct mach_sys_semaphore_wait_trap_args *uap)
{
	/* {
		syscallarg(mach_port_name_t) wait_name;
	} */
	struct mach_semaphore *ms;
	struct mach_waiting_lwp *mwl;
	struct mach_right *mr;
	mach_port_t mn;
	int blocked = 0;

	mn = uap->wait_name);
	if ((mr = mach_right_check(mn, td, MACH_PORT_TYPE_ALL_RIGHTS)) == 0)
		return EPERM;

	if (mr->mr_port->mp_datatype != MACH_MP_SEMAPHORE)
		return EINVAL;

	ms = (struct mach_semaphore *)mr->mr_port->mp_data;

	rw_enter(&ms->ms_lock, RW_WRITER);
	ms->ms_value--;
	if (ms->ms_value < 0)
		blocked = 1;
	rw_exit(&ms->ms_lock);

	if (blocked != 0) {
		mwl = mach_waiting_lwp_get(l, ms);
		while (ms->ms_value < 0)
			tsleep(mwl, PZERO|PCATCH, "sem_wait", 0);
		mach_waiting_lwp_put(mwl, ms, 0);
	}
	return 0;
}

int
mach_sys_semaphore_signal_trap(struct thread *td, struct mach_sys_semaphore_signal_trap_args *uap)
{
	/* {
		syscallarg(mach_port_name_t) signal_name;
	} */
	struct mach_semaphore *ms;
	struct mach_waiting_lwp *mwl;
	struct mach_right *mr;
	mach_port_t mn;
	int unblocked = 0;

	mn = uap->signal_name);
	if ((mr = mach_right_check(mn, td, MACH_PORT_TYPE_ALL_RIGHTS)) == 0)
		return EPERM;

	if (mr->mr_port->mp_datatype != MACH_MP_SEMAPHORE)
		return EINVAL;

	ms = (struct mach_semaphore *)mr->mr_port->mp_data;

	rw_enter(&ms->ms_lock, RW_WRITER);
	ms->ms_value++;
	if (ms->ms_value >= 0)
		unblocked = 1;
	rw_exit(&ms->ms_lock);

	if (unblocked != 0) {
		rw_enter(&ms->ms_lock, RW_READER);
		mwl = TAILQ_FIRST(&ms->ms_waiting);
		wakeup(mwl);
		rw_exit(&ms->ms_lock);
	}
	return 0;
}

int
mach_semaphore_create(struct mach_trap_args *args)
{
	mach_semaphore_create_request_t *req = args->smsg;
	mach_semaphore_create_reply_t *rep = args->rmsg;
	size_t *msglen = args->rsize;
	struct thread *td = args->td;
	struct mach_semaphore *ms;
	struct mach_port *mp;
	struct mach_right *mr;

	ms = mach_semaphore_get(req->req_value, req->req_policy);

	mp = mach_port_get();
	mp->mp_datatype = MACH_MP_SEMAPHORE;
	mp->mp_data = (void *)ms;

	mr = mach_right_get(mp, td, MACH_PORT_TYPE_SEND, 0);

	*msglen = sizeof(*rep);
	mach_set_header(rep, req, *msglen);
	mach_add_port_desc(rep, mr->mr_name);
	mach_set_trailer(rep, *msglen);

	return 0;
}

int
mach_semaphore_destroy(struct mach_trap_args *args)
{
	mach_semaphore_destroy_request_t *req = args->smsg;
	mach_semaphore_destroy_reply_t *rep = args->rmsg;
	struct thread *td = args->td;
	size_t *msglen = args->rsize;
	struct mach_semaphore *ms;
	struct mach_right *mr;
	mach_port_t mn;

	mn = req->req_sem.name;
	if ((mr = mach_right_check(mn, td, MACH_PORT_TYPE_ALL_RIGHTS)) == 0)
		return mach_msg_error(args, EPERM);

	if (mr->mr_port->mp_datatype != MACH_MP_SEMAPHORE)
		return mach_msg_error(args, EINVAL);

	ms = (struct mach_semaphore *)mr->mr_port->mp_data;
	mach_semaphore_put(ms);
	mach_right_put(mr, MACH_PORT_TYPE_REF_RIGHTS);

	*msglen = sizeof(*rep);
	mach_set_header(rep, req, *msglen);

	rep->rep_retval = 0;

	mach_set_trailer(rep, *msglen);

	return 0;
}

void
mach_semaphore_init(void)
{
	LIST_INIT(&mach_semaphore_list);
	rw_init(&mach_semaphore_list_lock);
	mach_semaphore_list_zone =
		uma_zcreate("mach_sem_zone", sizeof (struct mach_semaphore),
					NULL, NULL, NULL, 0/* align*/, 0/*flags*/);
	mach_waiting_lwp_zone =
		uma_zcreate("mach_waitp_zone", sizeof (struct mach_waiting_lwp),
					NULL, NULL, NULL, 0/* align*/, 0/*flags*/);

	return;
}

static struct mach_semaphore *
mach_semaphore_get(int value, int policy)
{
	struct mach_semaphore *ms;

	ms = uma_zalloc(mach_semaphore_list_zone, M_WAITOK);
	ms->ms_value = value;
	ms->ms_policy = policy;
	TAILQ_INIT(&ms->ms_waiting);
	rw_init(&ms->ms_lock);

	rw_enter(&mach_semaphore_list_lock, RW_WRITER);
	LIST_INSERT_HEAD(&mach_semaphore_list, ms, ms_list);
	rw_exit(&mach_semaphore_list_lock);

	return ms;
}

static void
mach_semaphore_put(struct mach_semaphore *ms)
{
	struct mach_waiting_lwp *mwl;

	rw_enter(&ms->ms_lock, RW_WRITER);
	while ((mwl = TAILQ_FIRST(&ms->ms_waiting)) != NULL)
		mach_waiting_lwp_put(mwl, ms, 0);
	rw_exit(&ms->ms_lock);
	rw_destroy(&ms->ms_lock);

	rw_enter(&mach_semaphore_list_lock, RW_WRITER);
	LIST_REMOVE(ms, ms_list);
	rw_exit(&mach_semaphore_list_lock);

	uma_zfree(mach_semaphore_list_zone, ms);

	return;
}

static struct mach_waiting_lwp *
mach_waiting_lwp_get(struct thread *td, struct mach_semaphore *ms)
{
	struct mach_waiting_lwp *mwl;

	mwl = uma_zalloc(mach_waiting_lwp_zone, M_WAITOK);
	mwl->mwl_l = l;

	rw_enter(&ms->ms_lock, RW_WRITER);
	TAILQ_INSERT_TAIL(&ms->ms_waiting, mwl, mwl_list);
	rw_exit(&ms->ms_lock);

	return mwl;
}

static void
mach_waiting_lwp_put(struct mach_waiting_lwp *mwl, struct mach_semaphore *ms, int locked)
{
	if (!locked)
		rw_enter(&ms->ms_lock, RW_WRITER);
	TAILQ_REMOVE(&ms->ms_waiting, mwl, mwl_list);
	if (!locked)
		rw_exit(&ms->ms_lock);
	uma_zfree(mach_waiting_lwp_zone, mwl);

	return;
}

/*
 * Cleanup after process exit. Need improvements, there
 * can be some memory leaks here.
 */
void
mach_semaphore_cleanup(struct thread *td)
{
	struct mach_semaphore *ms;
	struct mach_waiting_lwp *mwl;

	rw_enter(&mach_semaphore_list_lock, RW_READER);
	LIST_FOREACH(ms, &mach_semaphore_list, ms_list) {
		rw_enter(&ms->ms_lock, RW_WRITER);
		TAILQ_FOREACH(mwl, &ms->ms_waiting, mwl_list)
			if (mwl->mwl_l == l) {
				mach_waiting_lwp_put(mwl, ms, 0);
				ms->ms_value++;
				if (ms->ms_value >= 0)
					wakeup(TAILQ_FIRST(&ms->ms_waiting));
			}
		rw_exit(&ms->ms_lock);
	}
	rw_exit(&mach_semaphore_list_lock);

	return;
}

int
mach_sys_semaphore_wait_signal_trap(struct thread *td, struct mach_sys_semaphore_wait_signal_trap_args *uap)
{
	/* {
		syscallarg(mach_port_name_t) wait_name;
		syscallarg(mach_port_name_t) signal_name;
	} */
	struct mach_sys_semaphore_wait_trap_args cupwait;
	struct mach_sys_semaphore_signal_trap_args cupsig;
	int error;

	&cupsig->signal_name = uap->signal_name;
	if ((error = mach_sys_semaphore_signal_trap(td, &cupsig)) != 0)
		return error;

	&cupwait->wait_name = uap->wait_name;
	if ((error = mach_sys_semaphore_wait_trap(td, &cupwait)) != 0)
		return error;

	return 0;
}


int
mach_sys_semaphore_signal_thread_trap(struct thread *td, struct mach_sys_semaphore_signal_thread_trap_args *uap)
{
	/* {
		syscallarg(mach_port_name_t) signal_name;
		syscallarg(mach_port_name_t) thread;
	} */
	struct mach_right *mr;
	struct mach_semaphore *ms;
	mach_port_t mn;
	struct mach_waiting_lwp *mwl;
	int unblocked = 0;

	/*
	 * Get the semaphore
	 */
	mn = uap->signal_name);
	if ((mr = mach_right_check(mn, td, MACH_PORT_TYPE_ALL_RIGHTS)) == NULL)
		return EINVAL;

	if (mr->mr_port->mp_datatype != MACH_MP_SEMAPHORE)
		return EINVAL;

	ms = (struct mach_semaphore *)mr->mr_port->mp_data;

	/*
	 * Get the thread, and check that it is waiting for our semaphore
	 * If no thread was supplied, pick up the first one.
	 */
	mn = uap->thread);
	if (mn != 0) {
		if ((mr = mach_right_check(mn, td,
		    MACH_PORT_TYPE_ALL_RIGHTS)) == NULL)
			return EINVAL;

		if (mr->mr_port->mp_datatype != MACH_MP_LWP)
			return EINVAL;

		rw_enter(&ms->ms_lock, RW_WRITER);
		TAILQ_FOREACH(mwl, &ms->ms_waiting, mwl_list)
			if (mwl->mwl_l == (struct lwp *)mr->mr_port->mp_data)
				break;
	} else {
		rw_enter(&ms->ms_lock, RW_WRITER);
		mwl = TAILQ_FIRST(&ms->ms_waiting);
	}

	/*
	 * No thread was awaiting for this semaphore,
	 * exit without touching the semaphore.
	 */
	if (mwl == NULL) {
		rw_exit(&ms->ms_lock);
		return 0; /* Should be KERN_NOT_WAITING */
	}

	ms->ms_value++;
	if (ms->ms_value >= 0)
		unblocked = 1;
	rw_exit(&ms->ms_lock);

	if (unblocked != 0)
		wakeup(mwl);

	return 0;
}


int
mach_sys_semaphore_signal_all_trap(struct thread *td, struct mach_sys_semaphore_signal_all_trap_args *uap)
{
	/* {
		syscallarg(mach_port_name_t) signal_name;
	} */
	struct mach_right *mr;
	struct mach_semaphore *ms;
	mach_port_t mn;
	struct mach_waiting_lwp *mwl;
	int unblocked = 0;

	/*
	 * Get the semaphore
	 */
	mn = uap->signal_name;
	if ((mr = mach_right_check(mn, td, MACH_PORT_TYPE_ALL_RIGHTS)) == NULL)
		return EINVAL;

	if (mr->mr_port->mp_datatype != MACH_MP_SEMAPHORE)
		return EINVAL;

	ms = (struct mach_semaphore *)mr->mr_port->mp_data;

	rw_enter(&ms->ms_lock, RW_WRITER);
	ms->ms_value++;
	if (ms->ms_value >= 0)
		unblocked = 1;

	/*
	 * Wakeup all threads sleeping on it.
	 */
	if (unblocked != 0)
		TAILQ_FOREACH(mwl, &ms->ms_waiting, mwl_list)
			wakeup(mwl);

	rw_exit(&ms->ms_lock);

	return 0;
}


