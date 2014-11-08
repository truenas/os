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
#include <sys/vnode.h>
#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/ktrace.h>
#include <sys/exec.h>
#include <sys/sysproto.h>

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

int
mach_vm_map(struct mach_trap_args *args)
{
	mach_vm_map_request_t *req = args->smsg;
	mach_vm_map_reply_t *rep = args->rmsg;
	size_t *msglen = args->rsize;
	struct thread *ttd = args->ttd;
	struct proc *tp = ttd->td_proc;
	struct mmap_args cup;
	vm_offset_t addr;
	int error, flags;
	void *ret;

#ifdef DEBUG_MACH_VM
	printf("mach_vm_map(addr = %p, size = 0x%08lx, obj = 0x%x, "
	    "mask = 0x%08lx, flags = 0x%x, offset = 0x%08llx, "
	    "copy = %d, cur_prot = 0x%x, max_prot = 0x%x, inh = 0x%x);\n",
	    (void *)req->req_address, (long)req->req_size, req->req_object.name,
	    (long)req->req_mask, req->req_flags, (off_t)req->req_offset,
	    req->req_copy, req->req_cur_protection, req->req_max_protection,
	    req->req_inherance);
#endif

	/* XXX Darwin fails on mapping a page at address 0 */
	if (req->req_address == 0)
		return mach_msg_error(args, ENOMEM);

	req->req_size = round_page(req->req_size);

	/* Where Mach uses 0x00ff, we use 0x0100 */
	if ((req->req_mask & (req->req_mask + 1)) || (req->req_mask == 0))
		req->req_mask = 0;
	else
		req->req_mask += 1;

	if (req->req_flags & MACH_VM_FLAGS_ANYWHERE) {
		cup.flags = MAP_ANON;
		flags = 0;
	} else {
		cup.flags = MAP_ANON | MAP_FIXED;
		flags = MAP_FIXED;
	}

	/*
	 * Use uvm_map_findspace to find a place which conforms to the
	 * requested alignement.
	 */
	vm_map_lock(&tp->p_vmspace->vm_map);
	ret = vm_map_findspace(&tp->p_vmspace->vm_map,
	    trunc_page(req->req_address), req->req_size, &addr,
	    NULL, 0, req->req_mask, flags);
	vm_map_unlock(&tp->p_vmspace->vm_map);

	if (ret == NULL)
		return mach_msg_error(args, ENOMEM);

	switch(req->req_inherance) {
	case MACH_VM_INHERIT_SHARE:
		cup.flags |= MAP_INHERIT_SHARE;
		break;
	case MACH_VM_INHERIT_COPY:
		cup.flags |= MAP_COPY_ON_WRITE;
		break;
	case MACH_VM_INHERIT_NONE:
		break;
	case MACH_VM_INHERIT_DONATE_COPY:
	default:
		uprintf("mach_vm_map: unsupported inheritance flag %d\n",
		    req->req_inherance);
		break;
	}

	cup.addr = (void *)addr;
	cup.len = req->req_size;
	cup.prot = req->req_cur_protection;
	cup.fd = -1;		/* XXX For now, no object mapping */
	cup.pos = req->req_offset;

	if ((error = sys_mmap(ttd, &cup)) != 0)
		return mach_msg_error(args, error);

	rep->rep_retval = ttd->td_retval[0];
	*msglen = sizeof(*rep);
	mach_set_header(rep, req, *msglen);
	mach_set_trailer(rep, *msglen);

	return 0;
}

int
mach_vm_allocate(struct mach_trap_args *args)
{
	mach_vm_allocate_request_t *req = args->smsg;
	mach_vm_allocate_reply_t *rep = args->rmsg;
	size_t *msglen = args->rsize;
	struct thread *ttd = args->ttd;
	struct proc *tp = ttd->td_proc;
	struct mmap_args cup;
	vm_offset_t addr;
	size_t size;
	int error;

	addr = req->req_address;
	size = req->req_size;

#ifdef DEBUG_MACH_VM
	printf("mach_vm_allocate(addr = %p, size = 0x%08x);\n",
	    (void *)addr, size);
#endif

	/*
	 * Avoid mappings at address zero: it should
	 * be a "red zone" with nothing mapped on it.
	 */
	if (addr == 0) {
		if (req->req_flags & MACH_VM_FLAGS_ANYWHERE)
			addr = 0x1000;
		else
			return mach_msg_error(args, EINVAL);
#ifdef DEBUG_MACH_VM
		printf("mach_vm_allocate: trying addr = %p\n", (void *)addr);
#endif
	}

	size = round_page(size);
	if (req->req_flags & MACH_VM_FLAGS_ANYWHERE)
		addr = vm_map_min(&tp->p_vmspace->vm_map);
	else
		addr = trunc_page(addr);

	if (((addr + size) > vm_map_max(&tp->p_vmspace->vm_map)) ||
	    ((addr + size) <= addr))
		addr = vm_map_min(&tp->p_vmspace->vm_map);

	if (size == 0)
		goto out;

	cup.addr = (void *)addr;
	cup.len = size;
	cup.prot = PROT_READ | PROT_WRITE;
	cup.flags = MAP_ANON;
	if ((req->req_flags & MACH_VM_FLAGS_ANYWHERE) == 0)
		cup.flags |= MAP_FIXED;
	cup.fd = -1;
	cup.pos = 0;

	if ((error = sys_mmap(ttd, &cup)) != 0)
		return mach_msg_error(args, error);
	rep->rep_address = ttd->td_retval[0];
#ifdef DEBUG_MACH_VM
	printf("vm_allocate: success at %p\n", (void *)rep->rep_address);
#endif

out:
	*msglen = sizeof(*rep);
	mach_set_header(rep, req, *msglen);

	rep->rep_retval = 0;

	mach_set_trailer(rep, *msglen);

	return 0;
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
		return mach_msg_error(args, error);

	rep->rep_retval = ttd->td_retval[0];
	*msglen = sizeof(*rep);
	mach_set_header(rep, req, *msglen);
	mach_set_trailer(rep, *msglen);

	return 0;
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
		return mach_msg_error(args, EINVAL);

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
		return mach_msg_error(args, error);

	if ((error = vm_map_protect(&ttd->td_proc->p_vmspace->vm_map,
	    req->req_address, req->req_address + req->req_size,
	    req->req_access, 0)) != 0)
		return mach_msg_error(args, error);

	*msglen = sizeof(*rep);
	mach_set_header(rep, req, *msglen);
	mach_set_trailer(rep, *msglen);

	return 0;
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
		return mach_msg_error(args, error);

	*msglen = sizeof(*rep);
	mach_set_header(rep, req, *msglen);
	mach_set_trailer(rep, *msglen);

	return 0;
}

int
mach_sys_map_fd(struct thread *td, const struct mach_map_fd_args *uap)
{
	/* {
		syscallarg(int) fd;
		syscallarg(mach_vm_offset_t) offset;
		syscallarg(mach_vm_offset_t *) va;
		syscallarg(mach_boolean_t) findspace;
		syscallarg(mach_vm_size_t) size;
	} */
	file_t *fp;
	struct vnode *vp;
	struct exec_vmcmd evc;
	struct vm_map_entry *ret;
	struct proc *p = td->td_proc;
	struct munmap_args cup;
	void *va;
	int error;

	if ((error = copyin(uap->va, (void *)&va, sizeof(va))) != 0)
		return error;

	if (uap->findspace == 0) {
		/* Make some free space XXX probably not The Right Way */
		cup.addr = va;
		cup.len = uap->size;
		(void)sys_munmap(td, &cup);
	}

	fp = fd_getfile(uap->fd);
	if (fp == NULL)
		return EBADF;

	vp = fp->f_data;
	vref(vp);

#ifdef DEBUG_MACH_VM
	printf("vm_map_fd: addr = %p len = 0x%08lx\n",
		   va, (long)uap->size);
#endif
	memset(&evc, 0, sizeof(evc));
	evc.ev_addr = (u_long)va;
	evc.ev_len = uap->size;
	evc.ev_prot = VM_PROT_ALL;
	evc.ev_flags = uap->findspace ? 0 : VMCMD_FIXED;
	evc.ev_proc = vmcmd_map_readvn;
	evc.ev_offset = uap->offset;
	evc.ev_vp = vp;

	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	if ((error = (*evc.ev_proc)(td, &evc)) != 0) {
		VOP_UNLOCK(vp);

#ifdef DEBUG_MACH_VM
		printf("mach_map_fd: mapping at %p failed\n", va);
#endif

		if (uap->findspace == 0)
			goto bad2;

		vm_map_lock(&p->p_vmspace->vm_map);
		if ((ret = vm_map_findspace(&p->p_vmspace->vm_map,
		    vm_map_min(&p->p_vmspace->vm_map), evc.ev_len,
		    (vm_offset_t *)&evc.ev_addr, NULL, 0, PAGE_SIZE, 0)) == NULL) {
			vm_map_unlock(&p->p_vmspace->vm_map);
			goto bad2;
		}
		vm_map_unlock(&p->p_vmspace->vm_map);

		va = (void *)evc.ev_addr;

		memset(&evc, 0, sizeof(evc));
		evc.ev_addr = (u_long)va;
		evc.ev_len = uap->size;
		evc.ev_prot = VM_PROT_ALL;
		evc.ev_flags = 0;
		evc.ev_proc = vmcmd_map_readvn;
		evc.ev_offset = uap->offset;
		evc.ev_vp = vp;

#ifdef DEBUG_MACH_VM
		printf("mach_map_fd: trying at %p\n", va);
#endif
		vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
		if ((error = (*evc.ev_proc)(td, &evc)) != 0)
			goto bad1;
	}

	vput(vp);
	fd_putfile(uap->fd);
#ifdef DEBUG_MACH_VM
	printf("mach_map_fd: mapping at %p\n", (void *)evc.ev_addr);
#endif

	va = (mach_vm_offset_t *)evc.ev_addr;

	if ((error = copyout((void *)&va, uap->va, sizeof(va))) != 0)
		return error;

	return 0;

bad1:
	VOP_UNLOCK(vp);
bad2:
	vrele(vp);
	fd_putfile(uap->fd);
#ifdef DEBUG_MACH_VM
	printf("mach_map_fd: mapping at %p failed, error = %d\n",
	    (void *)evc.ev_addr, error);
#endif
	return error;
}

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
		return mach_msg_error(args, error);

	*msglen = sizeof(*rep);
	mach_set_header(rep, req, *msglen);
	mach_set_trailer(rep, *msglen);

	return 0;
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

	return 0;
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
		return mach_msg_error(args, EINVAL);

	/*
	 * MACH_VM_REGION_BASIC_INFO is the only
	 * supported flavor in Darwin.
	 */
	if (req->req_flavor != MACH_VM_REGION_BASIC_INFO)
		return mach_msg_error(args, EINVAL);
	if (req->req_count != (sizeof(*rbi) / sizeof(int))) /* This is 8 */
		return mach_msg_error(args, EINVAL);
	*msglen = sizeof(*rep) + ((req->req_count - 9) * sizeof(int));

	map = &ttd->td_proc->p_vmspace->vm_map;

	vm_map_lock(map);
	error = vm_map_lookup_entry(map, req->req_addr, &vme);
	vm_map_unlock(map);

	if (error == 0)
		return mach_msg_error(args, ENOMEM);

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

	return 0;
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
		return mach_msg_error(args, EINVAL);

	/*
	 * MACH_VM_REGION_BASIC_INFO is the only
	 * supported flavor in Darwin.
	 */
	if (req->req_flavor != MACH_VM_REGION_BASIC_INFO)
		return mach_msg_error(args, EINVAL);
	if (req->req_count != (sizeof(*rbi) / sizeof(int))) /* This is 8 */
		return mach_msg_error(args, EINVAL);
	*msglen = sizeof(*rep) + ((req->req_count - 9) * sizeof(int));

	map = &td->td_proc->p_vmspace->vm_map;

	vm_map_lock(map);
	error = vm_map_lookup_entry(map, req->req_addr, &vme);
	vm_map_unlock(map);

	if (error == 0)
		return mach_msg_error(args, ENOMEM);

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

	return 0;
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

	return 0;
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
		return mach_msg_error(args, EINVAL);

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
	return 0;

out:
	free(tmpbuf, M_TEMP);
	return mach_msg_error(args, error);
}

int
mach_vm_read(struct mach_trap_args *args)
{
	mach_vm_read_request_t *req = args->smsg;
	mach_vm_read_reply_t *rep = args->rmsg;
	size_t *msglen = args->rsize;
	struct thread *td = args->td;
	struct thread *ttd = args->ttd;
	char *tbuf;
	void *addr;
	vm_offset_t va;
	size_t size;
	int error;

	size = req->req_size;
	va = vm_map_min(&td->td_proc->p_vmspace->vm_map);
	if ((error = vm_map(&td->td_proc->p_vmspace->vm_map, &va,
	    round_page(size), NULL, UVM_UNKNOWN_OFFSET, 0,
	    UVM_MAPFLAG(UVM_PROT_RW, UVM_PROT_ALL,
	    UVM_INH_COPY, UVM_ADV_NORMAL, UVM_FLAG_COPYONW))) != 0) {
		printf("uvm_map error = %d\n", error);
		return mach_msg_error(args, EFAULT);
	}

	/*
	 * Copy the data from the target process to the current process
	 * This is reasonable for small chunk of data, but we should
	 * remap COW for areas bigger than a page.
	 */
	tbuf = malloc(size, M_MACH, M_WAITOK);

	addr = (void *)req->req_addr;
	if ((error = copyin_proc(td->td_proc, addr, tbuf, size)) != 0) {
		printf("copyin_proc error = %d, addr = %p, size = %x\n", error, addr, size);
		free(tbuf, M_WAITOK);
		return mach_msg_error(args, EFAULT);
	}

	if ((error = copyout(tbuf, (void *)va, size)) != 0) {
		printf("copyout error = %d\n", error);
		free(tbuf, M_WAITOK);
		return mach_msg_error(args, EFAULT);
	}

	if (error == 0)
		ktrmool(tbuf, size, (void *)va);

	free(tbuf, M_WAITOK);

	*msglen = sizeof(*rep);
	mach_set_header(rep, req, *msglen);
	mach_add_ool_desc(rep, (void *)va, size);

	rep->rep_count = size;

	mach_set_trailer(rep, *msglen);

	return 0;
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
		free(tbuf, M_WAITOK);
		return mach_msg_error(args, EFAULT);
	}

	addr = (void *)req->req_addr;
	if ((error = copyout_proc(td->td_proc, tbuf, addr, size)) != 0) {
		printf("copyout_proc error = %d\n", error);
		free(tbuf, M_WAITOK);
		return mach_msg_error(args, EFAULT);
	}

	if (error == 0)
		ktrmool(tbuf, size, (void *)addr);

	free(tbuf, M_WAITOK);

	*msglen = sizeof(*rep);
	mach_set_header(rep, req, *msglen);

	rep->rep_retval = 0;

	mach_set_trailer(rep, *msglen);

	return 0;
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
			error = mach_vm_machine_attribute_machdep(ttd,
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

	return 0;
}
