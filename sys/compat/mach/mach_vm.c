/*-
 * Copyright (c) 2002-2003, 2008 The NetBSD Foundation, Inc.
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
#include <sys/mount.h>
#include <sys/proc.h>
#include <sys/mman.h>
#include <sys/malloc.h>
#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/exec.h>
#include <sys/sysproto.h>
#include <sys/uio.h>
#include <sys/vnode.h>

#include <sys/ktrace.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_extern.h>

#include <compat/mach/mach_types.h>
#include <compat/mach/mach_message.h>
#include <compat/mach/mach_clock.h>
#include <compat/mach/mach_vm.h>
#include <compat/mach/mach_errno.h>
#include <compat/mach/mach_port.h>
#include <compat/mach/mach_services.h>
#include <compat/mach/mach_proto.h>

/*
 * Like copyin(), but operates on an arbitrary process.
 */

int
copyin_proc(struct proc *p, const void *uaddr, void *kaddr, size_t len)
{
	struct proc *mycp;
	struct vmspace *myvm, *tmpvm;
	struct thread *td = curthread;
	int error;

	/* XXX need to suspend other threads in curproc if we're in a user
	 * context
	 */

	/*
	 * Local copies of curproc (cp) and vmspace (myvm)
	 */
	mycp = td->td_proc;
	myvm = mycp->p_vmspace;

	/*
	 * Connect to process address space for user program.
	 */
	if (p != mycp) {
		/*
		 * Save the current address space that we are
		 * connected to.
		 */
		tmpvm = mycp->p_vmspace;

		/*
		 * Point to the new user address space, and
		 * refer to it.
		 */
		mycp->p_vmspace = p->p_vmspace;
		atomic_add_int(&mycp->p_vmspace->vm_refcnt, 1);

		/* Activate the new mapping. */
		pmap_activate(FIRST_THREAD_IN_PROC(mycp));
	}
	error = copyin(uaddr, kaddr, len);
	if (p != mycp) {
		mycp->p_vmspace = myvm;
		vmspace_free(tmpvm);
		/* Activate the old mapping. */
		pmap_activate(FIRST_THREAD_IN_PROC(mycp));
	}
	return (error);
}

/*
 * Like copyout(), but operates on an arbitrary process.
 */
int
copyout_proc(struct proc *p, const void *kaddr, void *uaddr, size_t len)
{
	struct proc *mycp;
	struct vmspace *myvm, *tmpvm;
	struct thread *td = curthread;
	int error;

	/* XXX need to suspend other threads in curproc if we're in a user
	 * context
	 */

	/*
	 * Local copies of curproc (cp) and vmspace (myvm)
	 */
	mycp = td->td_proc;
	myvm = mycp->p_vmspace;

	/*
	 * Connect to process address space for user program.
	 */
	if (p != mycp) {
		/*
		 * Save the current address space that we are
		 * connected to.
		 */
		tmpvm = mycp->p_vmspace;

		/*
		 * Point to the new user address space, and
		 * refer to it.
		 */
		mycp->p_vmspace = p->p_vmspace;
		atomic_add_int(&mycp->p_vmspace->vm_refcnt, 1);

		/* Activate the new mapping. */
		pmap_activate(FIRST_THREAD_IN_PROC(mycp));
	}
	error = copyout(kaddr, uaddr, len);
	if (p != mycp) {
		mycp->p_vmspace = myvm;
		vmspace_free(tmpvm);
		/* Activate the old mapping. */
		pmap_activate(FIRST_THREAD_IN_PROC(mycp));
	}
	return (error);
}

int
mach_vm_map(struct mach_trap_args *args)
{
	mach_vm_map_request_t *req = args->smsg;
	mach_vm_map_reply_t *rep = args->rmsg;
	size_t *msglen = args->rsize;
	struct thread *ttd = args->ttd;
	struct proc *tp = ttd->td_proc;
	vm_prot_t prot, protmax;
	vm_offset_t addr;
	int flags, docow = 0;
	size_t size;
	vm_map_t map = &tp->p_vmspace->vm_map;

#ifdef DEBUG_MACH_VM
	printf("mach_vm_map(addr = %p, size = 0x%08lx, obj = 0x%x, "
	    "mask = 0x%08lx, flags = 0x%x, offset = 0x%08llx, "
	    "copy = %d, cur_prot = 0x%x, max_prot = 0x%x, inh = 0x%x);\n",
	    (void *)req->req_address, (long)req->req_size, req->req_object.name,
	    (long)req->req_mask, req->req_flags, (off_t)req->req_offset,
	    req->req_copy, req->req_cur_protection, req->req_max_protection,
	    req->req_inheritance);
#endif

	/* XXX Darwin fails on mapping a page at address 0 */
	if (req->req_address == 0)
		return (mach_msg_error(args, ENOMEM));

	req->req_size = round_page(req->req_size);

	/* Where Mach uses 0x00ff, we use 0x0100 */
	if ((req->req_mask & (req->req_mask + 1)) || (req->req_mask == 0))
		req->req_mask = 0;
	else
		req->req_mask += 1;

	if (req->req_flags & MACH_VM_FLAGS_ANYWHERE) {
		flags = MAP_ANON;
	} else {
		flags = MAP_ANON | MAP_FIXED;
	}

	prot = req->req_cur_protection;
	protmax = req->req_max_protection;
	size = round_page(req->req_size);
#if 0
	/* only doing anonymous mappings for now */
	cup.pos = req->req_offset;
#endif
	vm_map_lock(map);
	if (vm_map_findspace(map,
						 trunc_page(req->req_address),
						 size, &addr)) {
		vm_map_unlock(map);
		return (mach_msg_error(args, ENOMEM));
	}
	switch(req->req_inheritance) {
	case MACH_VM_INHERIT_SHARE:
		flags |= MAP_INHERIT_SHARE;
		break;
	case MACH_VM_INHERIT_COPY:
		flags |= MAP_COPY_ON_WRITE;
		docow = 1;
		break;
	case MACH_VM_INHERIT_NONE:
		break;
	case MACH_VM_INHERIT_DONATE_COPY:
	default:
		uprintf("mach_vm_map: unsupported inheritance flag %d\n",
		    req->req_inheritance);
		break;
	}

	if (vm_map_insert(map, NULL, 0, addr, addr + size, prot, protmax, docow)) {
		vm_map_unlock(map);
		return (EFAULT);
	}

	rep->rep_retval = addr;
	*msglen = sizeof(*rep);
	mach_set_header(rep, req, *msglen);
	mach_set_trailer(rep, *msglen);

	return (0);
}

int
mach_vm_allocate(vm_map_t map, vm_offset_t *addr, size_t _size, int flags)
{
	size_t size = round_page(_size);
	vm_offset_t daddr;
	vm_prot_t prot, protmax;

	prot = VM_PROT_READ|VM_PROT_WRITE;
	protmax = VM_PROT_ALL;
	KASSERT(addr != NULL, ("invalid addr passed"));

	daddr = trunc_page(*addr);
	vm_map_lock(map);
	if ((flags & MACH_VM_FLAGS_ANYWHERE) &&
		(vm_map_findspace(map, 0, size, &daddr))) {
		vm_map_unlock(map);
		return (ENOMEM);
	} else {
		/* Address range must be all in user VM space. */
		if (daddr < vm_map_min(map) ||
		    daddr + size > vm_map_max(map))
			return (EINVAL);
		if (daddr + size < daddr)
			return (EINVAL);

	}
	if (vm_map_insert(map, NULL, 0, daddr, daddr + size, prot, protmax, 0)) {
		vm_map_unlock(map);
		return (EFAULT);
	}
	vm_map_unlock(map);
	*addr = daddr;
	return (0);
}

int
mach_vm_allocate_ipc(struct mach_trap_args *args)
{
	mach_vm_allocate_request_t *req = args->smsg;
	mach_vm_allocate_reply_t *rep = args->rmsg;
	size_t *msglen = args->rsize;
	struct proc *tp = args->ttd->td_proc;
	vm_offset_t addr;
	size_t size;
	int error, flags;;

	addr = req->req_address;
	size = req->req_size;
	flags = req->req_flags;

#ifdef DEBUG_MACH_VM
	printf("mach_vm_allocate(addr = %p, size = 0x%08x);\n",
	    (void *)addr, size);
#endif

	if ((error = mach_vm_allocate(&tp->p_vmspace->vm_map, &addr, size, flags)))
		return (mach_msg_error(args, error));
	rep->rep_address = addr;
#ifdef DEBUG_MACH_VM
	printf("vm_allocate: success at %p\n", (void *)rep->rep_address);
#endif

	*msglen = sizeof(*rep);
	mach_set_header(rep, req, *msglen);

	rep->rep_retval = 0;

	mach_set_trailer(rep, *msglen);

	return (0);
}

int
mach_vm_deallocate(struct mach_trap_args *args)
{
	mach_vm_deallocate_request_t *req = args->smsg;
	mach_vm_deallocate_reply_t *rep = args->rmsg;
	size_t *msglen = args->rsize;
	struct thread *ttd = args->ttd;
	struct munmap_args cup;
	int error;

#ifdef DEBUG_MACH_VM
	printf("mach_vm_deallocate(addr = %p, size = 0x%08lx);\n",
	    (void *)req->req_address, (long)req->req_size);
#endif

	cup.addr = (void *)req->req_address;
	cup.len = req->req_size;

	if ((error = sys_munmap(ttd, &cup)) != 0)
		return (mach_msg_error(args, error));

	rep->rep_retval = ttd->td_retval[0];
	*msglen = sizeof(*rep);
	mach_set_header(rep, req, *msglen);
	mach_set_trailer(rep, *msglen);

	return (0);
}

/*
 * XXX This server message Id clashes with bootstrap_look_up.
 * Is there a way to resolve this easily?
 */
#if 0
int
mach_vm_wire(struct mach_trap_args *args)
{
	mach_vm_wire_request_t *req = args->smsg;
	mach_vm_wire_reply_t *rep = args->rmsg;
	size_t *msglen = args->rsize;
	struct thread *ttd = args->ttd;
	int error;

#ifdef DEBUG_MACH_VM
	printf("mach_vm_wire(addr = %p, size = 0x%08x, prot = 0x%x);\n",
	    (void *)req->req_address, req->req_size, req->req_access);
#endif

	memset(&rep, 0, sizeof(*rep));

	if ((req->req_access & ~VM_PROT_ALL) != 0)
		return (mach_msg_error(args, EINVAL));

	/*
	 * Mach maintains a count of how many times a page is wired
	 * and unwire it once the count is zero. We cannot do that yet.
	 */
	if (req->req_access == 0) {
		struct munlock_args cup;

		cup.addr = (void *)req->req_address;
		cup.len = req->req_size;
		error = sys_munlock(ttd, &cup);
	} else {
		struct mlock_args cup;

		cup.addr = (void *)req->req_address;
		cup.len = req->req_size;
		error = sys_mlock(ttd, &cup);
	}
	if (error != 0)
		return (mach_msg_error(args, error));

	if ((error = vm_map_protect(&ttd->td_proc->p_vmspace->vm_map,
	    req->req_address, req->req_address + req->req_size,
	    req->req_access, 0)) != 0)
		return (mach_msg_error(args, error));

	*msglen = sizeof(*rep);
	mach_set_header(rep, req, *msglen);
	mach_set_trailer(rep, *msglen);

	return (0);
}
#endif

int
mach_vm_protect(struct mach_trap_args *args)
{
	mach_vm_protect_request_t *req = args->smsg;
	mach_vm_protect_reply_t *rep = args->rmsg;
	size_t *msglen = args->rsize;
	struct thread *ttd = args->ttd;
	struct mprotect_args cup;
	int error;

	cup.addr = (void *)req->req_addr;
	cup.len = req->req_size;
	cup.prot = req->req_prot;

	if ((error = sys_mprotect(ttd, &cup)) != 0)
		return (mach_msg_error(args, error));

	*msglen = sizeof(*rep);
	mach_set_header(rep, req, *msglen);
	mach_set_trailer(rep, *msglen);

	return (0);
}

#ifdef USE_OBSOLETE
int
sys_mach_map_fd(struct thread *td, struct mach_map_fd_args *uap)
{
	/* {
		syscallarg(int) fd;
		syscallarg(mach_vm_offset_t) offset;
		syscallarg(mach_vm_offset_t *) va;
		syscallarg(mach_boolean_t) findspace;
		syscallarg(mach_vm_size_t) size;
	} */

	struct munmap_args cup;
	struct mmap_args mmap_cup;
	void *va;
	int error;


	if ((error = copyin(uap->va, (void *)&va, sizeof(va))) != 0)
		return (error);

	mmap_cup.addr = va;
	mmap_cup.len = uap->size;
	mmap_cup.prot = VM_PROT_ALL;
	mmap_cup.flags = MAP_SHARED;
	mmap_cup.fd = uap->fd;
	mmap_cup.pos = uap->offset;
	if (uap->findspace == 0) {
		/* Make some free space XXX probably not The Right Way */
		cup.addr = va;
		cup.len = uap->size;
		(void)sys_munmap(td, &cup);
		mmap_cup.flags |= MAP_FIXED;
	}

	if ((error = sys_mmap(td, &mmap_cup)) != 0)
		return (error);

	if ((error = copyout((void *)&td->td_retval[0], uap->va, sizeof(va))) != 0)
		return (error);

	return (0);
}
#endif

int
mach_vm_inherit(struct mach_trap_args *args)
{
	mach_vm_inherit_request_t *req = args->smsg;
	mach_vm_inherit_reply_t *rep = args->rmsg;
	size_t *msglen = args->rsize;
	struct thread *ttd = args->ttd;
	struct minherit_args cup;
	int error;

	cup.addr = (void *)req->req_addr;
	cup.len = req->req_size;
	/* Flags map well between Mach and NetBSD */
	cup.inherit = req->req_inh;

	if ((error = sys_minherit(ttd, &cup)) != 0)
		return (mach_msg_error(args, error));

	*msglen = sizeof(*rep);
	mach_set_header(rep, req, *msglen);
	mach_set_trailer(rep, *msglen);

	return (0);
}

int
mach_make_memory_entry_64(struct mach_trap_args *args)
{
	mach_make_memory_entry_64_request_t *req = args->smsg;
	mach_make_memory_entry_64_reply_t *rep = args->rmsg;
	size_t *msglen = args->rsize;
	struct thread *td = args->td;
	struct mach_port *mp;
	struct mach_right *mr;
	struct mach_memory_entry *mme;

	printf("mach_make_memory_entry_64, offset 0x%lx, size 0x%lx\n",
	    (u_long)req->req_offset, (u_long)req->req_size);

	mp = mach_port_get();
	mp->mp_flags |= (MACH_MP_INKERNEL | MACH_MP_DATA_ALLOCATED);
	mp->mp_datatype = MACH_MP_MEMORY_ENTRY;

	mme = malloc(sizeof(*mme), M_MACH, M_WAITOK);
	mme->mme_proc = td->td_proc;
	mme->mme_offset = req->req_offset;
	mme->mme_size = req->req_size;
	mp->mp_data = mme;

	mr = mach_right_get(mp, td, MACH_PORT_TYPE_SEND, 0);

	*msglen = sizeof(*rep);
	mach_set_header(rep, req, *msglen);
	mach_add_port_desc(rep, mr->mr_name);

	rep->rep_size = req->req_size;

	mach_set_trailer(rep, *msglen);

	return (0);
}

int
mach_vm_region(struct mach_trap_args *args)
{
	mach_vm_region_request_t *req = args->smsg;
	mach_vm_region_reply_t *rep = args->rmsg;
	size_t *msglen = args->rsize;
	struct thread *ttd = args->ttd;
	struct mach_vm_region_basic_info *rbi;
	struct vm_map *map;
	struct vm_map_entry *vme;
	int error;

	/* Sanity check req_count */
	if (req->req_count > 9)
		return (mach_msg_error(args, EINVAL));

	/*
	 * MACH_VM_REGION_BASIC_INFO is the only
	 * supported flavor in Darwin.
	 */
	if (req->req_flavor != MACH_VM_REGION_BASIC_INFO)
		return (mach_msg_error(args, EINVAL));
	if (req->req_count != (sizeof(*rbi) / sizeof(int))) /* This is 8 */
		return (mach_msg_error(args, EINVAL));
	*msglen = sizeof(*rep) + ((req->req_count - 9) * sizeof(int));

	map = &ttd->td_proc->p_vmspace->vm_map;

	vm_map_lock(map);
	error = vm_map_lookup_entry(map, req->req_addr, &vme);
	vm_map_unlock(map);

	if (error == 0)
		return (mach_msg_error(args, ENOMEM));

	mach_set_header(rep, req, *msglen);
	mach_add_port_desc(rep, 0); /* XXX Why this null name */

	rep->rep_addr = vme->start;
	rep->rep_size = vme->end - vme->start;
	rep->rep_count = req->req_count;

	rbi = (struct mach_vm_region_basic_info *)&rep->rep_info[0];
	rbi->protection = vme->protection;
	rbi->inheritance = 1; /* vme->inheritance */
	rbi->shared = 0; /* XXX how can we know? */
	rbi->offset = vme->offset;
	rbi->behavior = MACH_VM_BEHAVIOR_DEFAULT; /* XXX What is it? */
	rbi->user_wired_count = vme->wired_count;

	/* XXX Why this? */
	*(short *)((u_long)&rbi->user_wired_count + sizeof(short)) = 1;

	mach_set_trailer(rep, *msglen);

	return (0);
}

int
mach_vm_region_64(struct mach_trap_args *args)
{
	mach_vm_region_64_request_t *req = args->smsg;
	mach_vm_region_64_reply_t *rep = args->rmsg;
	size_t *msglen = args->rsize;
	struct thread *ttd = args->ttd;
	struct mach_vm_region_basic_info_64 *rbi;
	struct vm_map *map;
	struct vm_map_entry *vme;
	int error;

	/* Sanity check req_count */
	if (req->req_count > 10)
		return (mach_msg_error(args, EINVAL));

	/*
	 * MACH_VM_REGION_BASIC_INFO is the only
	 * supported flavor in Darwin.
	 */
	if (req->req_flavor != MACH_VM_REGION_BASIC_INFO)
		return (mach_msg_error(args, EINVAL));
	if (req->req_count != (sizeof(*rbi) / sizeof(int))) /* This is 8 */
		return (mach_msg_error(args, EINVAL));
	*msglen = sizeof(*rep) + ((req->req_count - 9) * sizeof(int));

	map = &ttd->td_proc->p_vmspace->vm_map;

	vm_map_lock(map);
	error = vm_map_lookup_entry(map, req->req_addr, &vme);
	vm_map_unlock(map);

	if (error == 0)
		return (mach_msg_error(args, ENOMEM));

	mach_set_header(rep, req, *msglen);
	mach_add_port_desc(rep, 0);	/* XXX null port ? */

	rep->rep_size = PAGE_SIZE; /* XXX Why? */
	rep->rep_count = req->req_count;

	rbi = (struct mach_vm_region_basic_info_64 *)&rep->rep_info[0];
	rbi->protection = vme->protection;
	rbi->inheritance = 1; /* vme->inheritance */
	rbi->shared = 0; /* XXX how can we know? */
	rbi->offset = vme->offset;
	rbi->behavior = MACH_VM_BEHAVIOR_DEFAULT; /* XXX What is it? */
	rbi->user_wired_count = vme->wired_count;

	/* XXX Why this? */
	*(short *)((u_long)&rbi->user_wired_count + sizeof(short)) = 1;

	mach_set_trailer(rep, *msglen);

	return (0);
}

int
mach_vm_msync(struct mach_trap_args *args)
{
	mach_vm_msync_request_t *req = args->smsg;
	mach_vm_msync_reply_t *rep = args->rmsg;
	size_t *msglen = args->rsize;
	struct thread *ttd = args->ttd;
	struct msync_args cup;
	int error;

	cup.addr = (void *)req->req_addr;
	cup.len = req->req_size;
	cup.flags = 0;
	if (req->req_flags & MACH_VM_SYNC_ASYNCHRONOUS)
		cup.flags |= MS_ASYNC;
	if (req->req_flags & MACH_VM_SYNC_SYNCHRONOUS)
		cup.flags |= MS_SYNC;
	if (req->req_flags & MACH_VM_SYNC_INVALIDATE)
		cup.flags |= MS_INVALIDATE;

	error = sys_msync(ttd, &cup);

	*msglen = sizeof(*rep);
	mach_set_header(rep, req, *msglen);

	rep->rep_retval = native_to_mach_errno[error];

	mach_set_trailer(rep, *msglen);

	return (0);
}

/* XXX Do it for remote task */
int
mach_vm_copy(struct mach_trap_args *args)
{
	mach_vm_copy_request_t *req = args->smsg;
	mach_vm_copy_reply_t *rep = args->rmsg;
	size_t *msglen = args->rsize;
	char *tmpbuf;
	int error;
	char *src, *dst;
	size_t size;

#ifdef DEBUG_MACH_VM
	printf("mach_vm_copy: src = 0x%08lx, size = 0x%08lx, addr = 0x%08lx\n",
	    (long)req->req_src, (long)req->req_size, (long)req->req_addr);
#endif
	if ((req->req_src & (PAGE_SIZE - 1)) ||
	    (req->req_addr & (PAGE_SIZE - 1)) ||
	    (req->req_size & (PAGE_SIZE - 1)))
		return (mach_msg_error(args, EINVAL));

	src = (void *)req->req_src;
	dst = (void *)req->req_addr;
	size = (size_t)req->req_size;

	tmpbuf = malloc(PAGE_SIZE, M_TEMP, M_WAITOK);

	/* Is there an easy way of dealing with that efficiently? */
	do {
		if ((error = copyin(src, tmpbuf, PAGE_SIZE)) != 0)
			goto out;

		if ((error = copyout(tmpbuf, dst, PAGE_SIZE)) != 0)
			goto out;

		src += PAGE_SIZE;
		dst += PAGE_SIZE;
		size -= PAGE_SIZE;
	} while (size > 0);

	*msglen = sizeof(*rep);
	mach_set_header(rep, req, *msglen);

	rep->rep_retval = 0;

	mach_set_trailer(rep, *msglen);

	free(tmpbuf, M_TEMP);
	return (0);

out:
	free(tmpbuf, M_TEMP);
	return (mach_msg_error(args, error));
}

int
mach_vm_read(struct mach_trap_args *args)
{
	mach_vm_read_request_t *req = args->smsg;
	mach_vm_read_reply_t *rep = args->rmsg;
	size_t *msglen = args->rsize;
	struct thread *td = args->td;
	vm_map_t map = &curproc->p_vmspace->vm_map;
	caddr_t tbuf;
	vm_offset_t srcaddr, dstaddr;
	vm_prot_t prot, protmax;
	size_t size;
	int error;

	size = round_page(req->req_size);
	prot = VM_PROT_READ|VM_PROT_WRITE;
	protmax = VM_PROT_ALL;

	if ((error = mach_vm_allocate(map, &dstaddr, size, 0)))
		return (mach_msg_error(args, ENOMEM));
	/*
	 * Copy the data from the target process to the current process
	 * This is reasonable for small chunk of data, but we should
	 * remap COW for areas bigger than a page.
	 */
	tbuf = malloc(size, M_MACH, M_WAITOK);

	srcaddr = req->req_addr;
	if ((error = copyin_proc(td->td_proc, (caddr_t)srcaddr, tbuf, size)) != 0) {
		printf("copyin_proc error = %d, addr = %lx, size = %zx\n", error, srcaddr, size);
		free(tbuf, M_MACH);
		return (mach_msg_error(args, EFAULT));
	}

	if ((error = copyout(tbuf, (void *)dstaddr, size)) != 0) {
		printf("copyout error = %d\n", error);
		free(tbuf, M_MACH);
		return (mach_msg_error(args, EFAULT));
	}

	if (error == 0)
		ktrmool(tbuf, size, (void *)va);

	free(tbuf, M_MACH);

	*msglen = sizeof(*rep);
	mach_set_header(rep, req, *msglen);
	mach_add_ool_desc(rep, (void *)dstaddr, size);

	rep->rep_count = size;
	mach_set_trailer(rep, *msglen);
	return (0);
}

int
mach_vm_write(struct mach_trap_args *args)
{
	mach_vm_write_request_t *req = args->smsg;
	mach_vm_write_reply_t *rep = args->rmsg;
	size_t *msglen = args->rsize;
	struct thread *ttd = args->ttd;
	size_t size;
	void *addr;
	char *tbuf;
	int error;

#ifdef DEBUG_MACH
	if (req->req_body.msgh_descriptor_count != 1)
		printf("mach_vm_write: OOL descriptor count is not 1\n");
#endif

	/*
	 * Copy the data from the current process to the target process
	 * This is reasonable for small chunk of data, but we should
	 * remap COW for areas bigger than a page.
	 */
	size = req->req_data.size;
	tbuf = malloc(size, M_MACH, M_WAITOK);

	if ((error = copyin(req->req_data.address, tbuf, size)) != 0) {
		printf("copyin error = %d\n", error);
		free(tbuf, M_MACH);
		return (mach_msg_error(args, EFAULT));
	}

	addr = (void *)req->req_addr;
	if ((error = copyout_proc(ttd->td_proc, tbuf, addr, size)) != 0) {
		printf("copyout_proc error = %d\n", error);
		free(tbuf, M_MACH);
		return (mach_msg_error(args, EFAULT));
	}

	if (error == 0)
		ktrmool(tbuf, size, (void *)addr);

	free(tbuf, M_MACH);

	*msglen = sizeof(*rep);
	mach_set_header(rep, req, *msglen);

	rep->rep_retval = 0;

	mach_set_trailer(rep, *msglen);

	return (0);
}

int
mach_vm_machine_attribute(struct mach_trap_args *args)
{
	mach_vm_machine_attribute_request_t *req = args->smsg;
	mach_vm_machine_attribute_reply_t *rep = args->rmsg;
	size_t *msglen = args->rsize;
	struct thread *ttd = args->ttd;
	int error = 0;
	int attribute, value;

	attribute = req->req_attribute;
	value = req->req_value;

	switch (attribute) {
	case MACH_MATTR_CACHE:
		switch(value) {
		case MACH_MATTR_VAL_CACHE_FLUSH:
		case MACH_MATTR_VAL_DCACHE_FLUSH:
		case MACH_MATTR_VAL_ICACHE_FLUSH:
		case MACH_MATTR_VAL_CACHE_SYNC:
			error = cpu_mach_vm_machine_attribute(ttd,
			    req->req_addr, req->req_size, &value);
			break;
		default:
#ifdef DEBUG_MACH
			printf("unimplemented value %d\n", req->req_value);
#endif
			error = EINVAL;
			break;
		}
		break;

	case MACH_MATTR_MIGRATE:
	case MACH_MATTR_REPLICATE:
	default:
#ifdef DEBUG_MACH
		printf("unimplemented attribute %d\n", req->req_attribute);
#endif
		error = EINVAL;
		break;
	}

	*msglen = sizeof(*rep);
	mach_set_header(rep, req, *msglen);

	rep->rep_retval = native_to_mach_errno[error];
	if (error != 0)
		rep->rep_value = value;

	mach_set_trailer(rep, *msglen);

	return (0);
}
