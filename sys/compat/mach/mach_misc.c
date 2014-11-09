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
#include <sys/proc.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/filedesc.h>
#include <sys/ioctl.h>
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


#include <compat/mach/mach_types.h>
#include <compat/mach/mach_message.h>
#include <compat/mach/mach_clock.h>
#include <compat/mach/mach_proto.h>


int
sys_mach_semaphore_timedwait_trap(struct thread *td, struct mach_semaphore_timedwait_trap_args *uap)
{

	DPRINTF(("%s(0x%x, %d, %d);\n",
			 __FUNCTION__, uap->wait_name, uap->sec, uap->nsec));
	return 0;
}


int
sys_mach_semaphore_timedwait_signal_trap(struct thread *td, struct mach_semaphore_timedwait_signal_trap_args *uap)
{

	DPRINTF(("%s(0x%x, 0x%x, %d, %d);\n", __FUNCTION__,
	    uap->wait_name, uap->signal_name, uap->sec,
	    uap->nsec));
	return 0;
}


int
sys_mach_init_process(struct thread *td, const void *v)
{
	DPRINTF(("sys_mach_init_process();\n"));
	return 0;
}


int
sys_mach_pid_for_task(struct thread *td, struct mach_pid_for_task_args *uap)
{

	DPRINTF(("sys_mach_pid_for_task(0x%x, %p);\n",
	    uap->t, uap->x));
	return 0;
}


int
sys_mach_macx_swapon(struct thread *td, struct mach_macx_swapon_args *uap)
{

	DPRINTF(("sys_mach_macx_swapon(%p, %d, %d, %d);\n",
	    uap->name, uap->flags, uap->size,
			 uap->priority));
	return 0;
}

int
sys_mach_macx_swapoff(struct thread *td, struct mach_macx_swapoff_args *uap)
{

	DPRINTF(("sys_mach_macx_swapoff(%p, %d);\n",
			 uap->name, uap->flags));
	return 0;
}

int
sys_mach_macx_triggers(struct thread *td, struct mach_macx_triggers_args *uap)
{

	DPRINTF(("sys_mach_macx_triggers(%d, %d, %d, 0x%x);\n",
	    uap->hi_water, uap->low_water, uap->flags,
			 uap->alert_port));
	return 0;
}


int
sys_mach_wait_until(struct thread *td, struct mach_wait_until_args *uap)
{

	DPRINTF(("sys_mach_wait_until(%lld);\n", uap->deadline));
	return 0;
}


int
sys_mach_timer_create(struct thread *td, const void *v)
{
	DPRINTF(("sys_mach_timer_create();\n"));
	return 0;
}


int
sys_mach_timer_destroy(struct thread *td, struct mach_timer_destroy_args *uap)
{

	DPRINTF(("sys_mach_timer_destroy(0x%x);\n", uap->name));
	return 0;
}


int
sys_mach_timer_arm(struct thread *td, struct mach_timer_arm_args *uap)
{

	DPRINTF(("sys_mach_timer_arm(0x%x, %d);\n",
	    uap->name, uap->expire_time));
	return 0;
}


int
sys_mach_timer_cancel(struct thread *td, struct mach_timer_cancel_args *uap)
{

	DPRINTF(("sys_mach_timer_cancel(0x%x, %p);\n",
	    uap->name, uap->result_time));
	return 0;
}


int
sys_mach_get_time_base_info(struct thread *td, const void *v)
{
	DPRINTF(("sys_mach_get_time_base_info();\n"));
	return 0;
}
