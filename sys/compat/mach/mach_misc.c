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

#include <compat/mach/mach_types.h>
#include <compat/mach/mach_message.h>
#include <compat/mach/mach_clock.h>
#include <compat/mach/mach_port.h>
#include <compat/mach/mach_proto.h>
#include <compat/mach/mach_semaphore.h>
#include <compat/mach/mach_notify.h>
#include <compat/mach/mach_exec.h>
#include <compat/mach/mach_vm.h>

MALLOC_DEFINE(M_MACH, "mach", "mach compatibility subsystem");


static int mach_cold = 1; /* Have we initialized COMPAT_MACH structures? */

static void
mach_init(void)
{
	mach_semaphore_init();
	mach_message_init();
	mach_port_init();

	mach_cold = 0;
}

void
mach_e_proc_init(struct proc *p)
{
	struct mach_emuldata *med;
	struct mach_right *mr;

	/*
	 * Initialize various things if needed.
	 * XXX Not the best place for this.
	 */
	if (mach_cold == 1)
		mach_init();

	/*
	 * For Darwin binaries, p->p_emuldata is always allocated:
	 * from the previous program if it had the same emulation,
	 * or from darwin_e_proc_exec(). In the latter situation,
	 * everything has been set to zero.
	 */
	if (!p->p_emuldata) {
#ifdef DIAGNOSTIC
		if (p->p_emul != &emul_mach)
			printf("mach_emuldata allocated for non Mach binary\n");
#endif
		med = malloc(sizeof(struct mach_emuldata),
		    M_MACH, M_WAITOK | M_ZERO);
	} else
		med = (struct mach_emuldata *)p->p_emuldata;

	/*
	 * p->p_emudata has med_inited set if we inherited it from
	 * the program that called exec(). In that situation, we
	 * must free anything that will not be used anymore.
	 */
	if (med->med_inited != 0) {
		rw_wlock(&med->med_rightlock);
		while ((mr = LIST_FIRST(&med->med_right)) != NULL)
			mach_right_put_exclocked(mr, MACH_PORT_TYPE_ALL_RIGHTS);
		rw_wunlock(&med->med_rightlock);

		/*
		 * Do not touch special ports. Some other process (eg: gdb)
		 * might have grabbed them to control the process, and the
		 * controller intend to keep in control even after exec().
		 */
	} else {
		/*
		 * p->p_emuldata is uninitialized. Go ahead and initialize it.
		 */
		LIST_INIT(&med->med_right);
		rw_init(&med->med_rightlock, "rights lock");
		rw_init(&med->med_exclock, "excl lock");

		/*
		 * For debugging purpose, it's convenient to have each process
		 * using distinct port names, so we prefix the first port name
		 * by the PID. Darwin does not do that, but we can remove it
		 * when we want, it will not hurt.
		 */
		med->med_nextright = p->p_pid << 16;

		/*
		 * Initialize special ports. Bootstrap port is shared
		 * among all Mach processes in our implementation.
		 */
		med->med_kernel = mach_port_get();
		med->med_host = mach_port_get();

		med->med_kernel->mp_flags |= MACH_MP_INKERNEL;
		med->med_host->mp_flags |= MACH_MP_INKERNEL;

		med->med_kernel->mp_data = (void *)p;
		med->med_host->mp_data = (void *)p;

		med->med_kernel->mp_datatype = MACH_MP_PROC;
		med->med_host->mp_datatype = MACH_MP_PROC;

		MACH_PORT_REF(med->med_kernel);
		MACH_PORT_REF(med->med_host);

		med->med_bootstrap = mach_bootstrap_port;
		MACH_PORT_REF(med->med_bootstrap);
	}

	/*
	 * Exception ports are inherited accross exec() calls.
	 * If the structure is initialized, the ports are just
	 * here, so leave them untouched. If the structure is
	 * uninitalized, the ports are all set to zero, which
	 * is the default, so do not touch them either.
	 */

	med->med_dirty_thid = 1;
	med->med_suspend = 0;
	med->med_inited = 1;

	PROC_LOCK(p);
	if (p->p_emuldata != med)
		p->p_emuldata = med;
	PROC_UNLOCK(p);
}

void
mach_e_thread_init(struct thread *td)
{
	struct mach_thread_emuldata *mle;

	KASSERT(td == curthread, ("mach_e_thread_init should only be called on curthread"));
	if (curproc->p_emuldata == NULL)
		mach_e_proc_init(curproc);

	mle = malloc(sizeof(*mle), M_MACH, M_WAITOK);
	mle->mle_kernel = mach_port_get();
	MACH_PORT_REF(mle->mle_kernel);

	mle->mle_kernel->mp_flags |= MACH_MP_INKERNEL;
	mle->mle_kernel->mp_datatype = MACH_MP_LWP;
	mle->mle_kernel->mp_data = (void *)td;

	td->td_emuldata = mle;
	td->td_pflags |= TDP_MACHINITED;
}


int
sys_mach_semaphore_timedwait_trap(struct thread *td, struct mach_semaphore_timedwait_trap_args *uap)
{

	DPRINTF(("%s(0x%x, %d, %d);\n",
			 __FUNCTION__, uap->wait_name, uap->sec, uap->nsec));
	return (ENOSYS);
}


int
sys_mach_semaphore_timedwait_signal_trap(struct thread *td, struct mach_semaphore_timedwait_signal_trap_args *uap)
{

	DPRINTF(("%s(0x%x, 0x%x, %d, %d);\n", __FUNCTION__,
	    uap->wait_name, uap->signal_name, uap->sec,
	    uap->nsec));
	return (ENOSYS);
}


int
sys_mach_pid_for_task(struct thread *td, struct mach_pid_for_task_args *uap)
{

	DPRINTF(("sys_mach_pid_for_task(0x%x, %p);\n",
	    uap->t, uap->x));
	return (ENOSYS);
}


int
sys_mach_macx_swapon(struct thread *td, struct mach_macx_swapon_args *uap)
{

	DPRINTF(("sys_mach_macx_swapon(%p, %d, %d, %d);\n",
	    uap->name, uap->flags, uap->size,
			 uap->priority));
	return (ENOSYS);
}

int
sys_mach_macx_swapoff(struct thread *td, struct mach_macx_swapoff_args *uap)
{

	DPRINTF(("sys_mach_macx_swapoff(%p, %d);\n",
			 uap->name, uap->flags));
	return (ENOSYS);
}

int
sys_mach_macx_triggers(struct thread *td, struct mach_macx_triggers_args *uap)
{

	DPRINTF(("sys_mach_macx_triggers(%d, %d, %d, 0x%x);\n",
	    uap->hi_water, uap->low_water, uap->flags,
			 uap->alert_port));
	return (ENOSYS);
}


int
sys_mach_wait_until(struct thread *td, struct mach_wait_until_args *uap)
{

	DPRINTF(("sys_mach_wait_until(%lld);\n", uap->deadline));
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
sys__kernelrpc_mach_vm_allocate_trap(struct thread *td, struct _kernelrpc_mach_vm_allocate_trap_args *uap)
{
	/* mach_port_name_t target = uap->target; current task only */
	mach_vm_offset_t *address = uap->address;
	mach_vm_offset_t uaddr;
	mach_vm_size_t size = uap->size;
	int flags = uap->flags;
	int error;

	if ((error = copyin(address, &uaddr, sizeof(mach_vm_offset_t))))
		return (error);

	if ((error = mach_vm_allocate(&td->td_proc->p_vmspace->vm_map,
								  &uaddr, size, flags)))
		return (error);
	if ((error = copyout(&uaddr, address, sizeof(mach_vm_offset_t))))
		return (error);
	return (0);
}

int
sys__kernelrpc_mach_vm_deallocate_trap(struct thread *td, struct _kernelrpc_mach_vm_deallocate_trap_args *uap)
{
	/* mach_port_name_t target = uap->target; current task only */
	mach_vm_offset_t address = uap->address;
	mach_vm_size_t size = uap->size;
	struct munmap_args cup;

	cup.addr = (void *)address;
	cup.len = size;

	return (sys_munmap(td, &cup));
}

int
sys__kernelrpc_mach_vm_protect_trap(struct thread *td, struct _kernelrpc_mach_vm_protect_trap_args *uap)
{
	return (ENOSYS);
}

int
sys__kernelrpc_mach_vm_map_trap(struct thread *td, struct _kernelrpc_mach_vm_map_trap_args *uap)
{

	return (ENOSYS);
}

int
sys__kernelrpc_mach_port_allocate_trap(struct thread *td, struct _kernelrpc_mach_port_allocate_trap_args *uap)
{
	/* mach_port_name_t target = uap->target; */
	mach_port_right_t right = uap->right;
	struct mach_right *mr;
	struct mach_port *mp;

	switch (right) {
	case MACH_PORT_RIGHT_RECEIVE:
		mp = mach_port_get();
		mr = mach_right_get(mp, td, MACH_PORT_TYPE_RECEIVE, 0);
		break;

	case MACH_PORT_RIGHT_DEAD_NAME:
		mr = mach_right_get(NULL, td, MACH_PORT_TYPE_DEAD_NAME, 0);
		break;

	case MACH_PORT_RIGHT_PORT_SET:
		mr = mach_right_get(NULL, td, MACH_PORT_TYPE_PORT_SET, 0);
		break;

	default:
		uprintf("mach_port_allocate: unknown right %x\n",
				right);
		return (EINVAL);
		break;
	}
	return (copyout(&mr->mr_name, uap->name, sizeof(*uap->name)));
}

int
sys__kernelrpc_mach_port_destroy_trap(struct thread *td, struct _kernelrpc_mach_port_destroy_trap_args *uap)
{
	/* mach_port_name_t target = uap->target; */
	mach_port_name_t name = uap->name;
	struct mach_right *mr;

	if ((mr =
		 mach_right_check(name, td, MACH_PORT_TYPE_ALL_RIGHTS)) == NULL)
		return (ENOENT);

	MACH_PORT_UNREF(mr->mr_port);
	mr->mr_port = NULL;
	mach_right_put(mr, MACH_PORT_TYPE_ALL_RIGHTS);
	return (0);
}

int
sys__kernelrpc_mach_port_deallocate_trap(struct thread *td, struct _kernelrpc_mach_port_deallocate_trap_args *uap)
{
	/* mach_port_name_t target = uap->target; */
	mach_port_name_t name = uap->name;
	struct mach_right *mr;

	if ((mr = mach_right_check(name, td, MACH_PORT_TYPE_REF_RIGHTS)) == NULL)
		return (ENOENT);
	mach_right_put(mr, MACH_PORT_TYPE_REF_RIGHTS);
	return (0);
}

int
sys__kernelrpc_mach_port_mod_refs_trap(struct thread *td, struct _kernelrpc_mach_port_mod_refs_trap_args *uap)
{
	/*
	  mach_port_name_t target = uap->target;
	  mach_port_name_t *name = uap->name;
	  mach_port_right_t right = uap->right;
	  mach_port_delta_t delta = uap->delta;
	*/
	return (ENOSYS);
}

int
sys__kernelrpc_mach_port_move_member_trap(struct thread *td, struct _kernelrpc_mach_port_move_member_trap_args *uap)
{
	/* mach_port_name_t target = uap->target; */
	mach_port_name_t member = uap->member;
	mach_port_name_t after = uap->after;
	struct mach_emuldata *med = td->td_proc->p_emuldata;
	struct mach_right *mrr;
	struct mach_right *mrs;

	mrr = mach_right_check(member, td, MACH_PORT_TYPE_RECEIVE);
	if (mrr == NULL)
		return (EPERM);

	mrs = mach_right_check(after, td, MACH_PORT_TYPE_PORT_SET);
	if (mrs == NULL)
		return (EPERM);

	rw_wlock(&med->med_rightlock);

	/* Remove it from an existing port set */
	if (mrr->mr_sethead != mrr)
		LIST_REMOVE(mrr, mr_setlist);

	/* Insert it into the new port set */
	LIST_INSERT_HEAD(&mrs->mr_set, mrr, mr_setlist);
	mrr->mr_sethead = mrs;

	rw_wunlock(&med->med_rightlock);
	return (0);
}

int
sys__kernelrpc_mach_port_insert_right_trap(struct thread *td, struct _kernelrpc_mach_port_insert_right_trap_args *uap)
{
	return (ENOSYS);
}

int
sys__kernelrpc_mach_port_insert_member_trap(struct thread *td, struct _kernelrpc_mach_port_insert_member_trap_args *uap)
{
	return (ENOSYS);
}

int
sys__kernelrpc_mach_port_extract_member_trap(struct thread *td, struct _kernelrpc_mach_port_extract_member_trap_args *uap)
{
	return (ENOSYS);
}

int
sys__kernelrpc_mach_port_construct_trap(struct thread *td, struct _kernelrpc_mach_port_construct_trap_args *uap)
{
	return (ENOSYS);
}

int
sys__kernelrpc_mach_port_destruct_trap(struct thread *td, struct _kernelrpc_mach_port_destruct_trap_args *uap)
{
	return (ENOSYS);
}

int
sys__kernelrpc_mach_port_guard_trap(struct thread *td, struct _kernelrpc_mach_port_guard_trap_args *uap)
{
	return (ENOSYS);
}

int
sys__kernelrpc_mach_port_unguard_trap(struct thread *td, struct _kernelrpc_mach_port_unguard_trap_args *uap)
{
	return (ENOSYS);
}

int
sys_mach_task_name_for_pid(struct thread *td, struct mach_task_name_for_pid_args *uap)
{
	return (ENOSYS);
}

int
sys_mach_macx_backing_store_suspend(struct thread *td, struct mach_macx_backing_store_suspend_args *uap)
{
	return (ENOSYS);
}

int
sys_mach_macx_backing_store_recovery(struct thread *td, struct mach_macx_backing_store_recovery_args *uap)
{
	return (ENOSYS);
}



