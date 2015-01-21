/*-
 * Copyright (c) 2001 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Christos Zoulas.
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

/*
 * MACH compatibility module.
 *
 * We actually don't implement anything here yet!
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/namei.h>
#include <sys/dirent.h>
#include <sys/eventhandler.h>
#include <sys/proc.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/filedesc.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/resource.h>
#include <sys/resourcevar.h>
#include <sys/socket.h>
#include <sys/vnode.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <sys/utsname.h>
#include <sys/unistd.h>
#include <sys/times.h>
#include <sys/sem.h>
#include <sys/msg.h>
#include <sys/ptrace.h>
#include <sys/signalvar.h>

#include <netinet/in.h>
#include <sys/sysproto.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_extern.h>

#include <sys/mach/mach_types.h>

#undef DPRINTF
#ifdef MACH_DEBUG
#define DPRINTF(x) printf x
#else
#define DPRINTF(x)
#endif

MALLOC_DEFINE(M_MACH, "mach", "mach compatibility subsystem");

int
sys_semaphore_timedwait_trap(struct thread *td, struct semaphore_timedwait_trap_args *uap)
{

	DPRINTF(("%s(0x%x, %d, %d);\n",
			 __FUNCTION__, uap->wait_name, uap->sec, uap->nsec));
	return (ENOSYS);
}


int
sys_semaphore_timedwait_signal_trap(struct thread *td, struct semaphore_timedwait_signal_trap_args *uap)
{

	DPRINTF(("%s(0x%x, 0x%x, %d, %d);\n", __FUNCTION__,
	    uap->wait_name, uap->signal_name, uap->sec,
	    uap->nsec));
	return (ENOSYS);
}


int
sys_pid_for_task(struct thread *td, struct pid_for_task_args *uap)
{

	DPRINTF(("sys_mach_pid_for_task(0x%x, %p);\n",
	    uap->t, uap->pid));
	return (ENOSYS);
}


int
sys_macx_swapon(struct thread *td, struct macx_swapon_args *uap)
{

	DPRINTF(("sys_mach_macx_swapon(%p, %d, %d, %d);\n",
	    uap->name, uap->flags, uap->size,
			 uap->priority));
	return (ENOSYS);
}

int
sys_macx_swapoff(struct thread *td, struct macx_swapoff_args *uap)
{

	DPRINTF(("sys_mach_macx_swapoff(%p, %d);\n",
			 uap->name, uap->flags));
	return (ENOSYS);
}

int
sys_macx_triggers(struct thread *td, struct macx_triggers_args *uap)
{

	DPRINTF(("sys_mach_macx_triggers(%d, %d, %d, 0x%x);\n",
	    uap->hi_water, uap->low_water, uap->flags,
			 uap->alert_port));
	return (ENOSYS);
}


int
sys_mach_wait_until(struct thread *td, struct mach_wait_until_args *uap)
{

	DPRINTF(("sys_mach_wait_until(%lu);\n", uap->deadline));
	return (ENOSYS);
}


int
sys_mk_timer_create(struct thread *td, struct mk_timer_create_args *uap)
{
	DPRINTF(("sys_mach_timer_create();\n"));
	return (ENOSYS);
}


int
sys_mk_timer_destroy(struct thread *td, struct mk_timer_destroy_args *uap)
{

	DPRINTF(("sys_mach_timer_destroy(0x%x);\n", uap->name));
	return (ENOSYS);
}


int
sys_mk_timer_arm(struct thread *td, struct mk_timer_arm_args *uap)
{

	DPRINTF(("sys_mach_timer_arm(0x%x, %d);\n",
	    uap->name, uap->expire_time));
	return (ENOSYS);
}

int
sys_mk_timer_cancel(struct thread *td, struct mk_timer_cancel_args *uap)
{

	DPRINTF(("sys_mach_timer_cancel(0x%x, %p);\n",
	    uap->name, uap->result_time));
	return (ENOSYS);
}

int
sys_task_name_for_pid(struct thread *td, struct task_name_for_pid_args *uap)
{
	return (ENOSYS);
}

int
sys_macx_backing_store_suspend(struct thread *td, struct macx_backing_store_suspend_args *uap)
{
	return (ENOSYS);
}

int
sys_macx_backing_store_recovery(struct thread *td, struct macx_backing_store_recovery_args *uap)
{
	return (ENOSYS);
}



