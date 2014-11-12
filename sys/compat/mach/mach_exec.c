/*-
 * Copyright (c) 2001-2003 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Christos Zoulas and Emmanuel Dreyfus.
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/exec.h>
#include <sys/queue.h>
#include <sys/malloc.h>

#include <sys/sysent.h>
#include <sys/syscall.h>

#include <compat/mach/mach_types.h>
#include <compat/mach/mach_message.h>
#include <compat/mach/mach_port.h>



static void mach_init(void);

extern struct sysent sysent[];
extern const char * const mach_syscallnames[];
#ifndef __HAVE_SYSCALL_INTERN
void syscall(void);
#else
void mach_syscall_intern(struct proc *);
#endif

#ifdef COMPAT_16
extern char sigcode[], esigcode[];
struct uvm_object *emul_mach_object;
#endif

/*
 * Copy arguments onto the stack in the normal way, but add some
 * extra information in case of dynamic binding.
 * XXX This needs a cleanup: it is not used anymore by the Darwin
 * emulation, and it probably contains Darwin specific bits.
 */
int
exec_mach_copyargs(struct thread *td, struct exec_package *pack, struct ps_strings *arginfo, char **stackp, void *argp)
{
	struct exec_macho_emul_arg *emea;
	struct exec_macho_object_header *macho_hdr;
	size_t len;
	size_t zero = 0;
	int error;

	*stackp = (char *)(((unsigned long)*stackp - 1) & ~0xfUL);

	emea = (struct exec_macho_emul_arg *)pack->ep_emul_arg;
	macho_hdr = (struct exec_macho_object_header *)emea->macho_hdr;
	if ((error = copyout(&macho_hdr, *stackp, sizeof(macho_hdr))) != 0)
		return error;

	*stackp += sizeof(macho_hdr);

	if ((error = copyargs(td, pack, arginfo, stackp, argp)) != 0) {
		DPRINTF(("mach: copyargs failed\n"));
		return error;
	}

	if ((error = copyout(&zero, *stackp, sizeof(zero))) != 0)
		return error;
	*stackp += sizeof(zero);

	if ((error = copyoutstr(emea->filename,
	    *stackp, MAXPATHLEN, &len)) != 0) {
		DPRINTF(("mach: copyout path failed\n"));
		return error;
	}
	*stackp += len + 1;

	/* We don't need this anymore */
	free(pack->ep_emul_arg, M_TEMP);
	pack->ep_emul_arg = NULL;

	len = len % sizeof(zero);
	if (len) {
		if ((error = copyout(&zero, *stackp, len)) != 0)
			return error;
		*stackp += len;
	}

	if ((error = copyout(&zero, *stackp, sizeof(zero))) != 0)
		return error;
	*stackp += sizeof(zero);

	return 0;
}

int
exec_mach_probe(const char **path)
{
	*path = emul_mach.e_path;
	return 0;
}

void
mach_e_proc_exec(struct proc *p, struct exec_package *epp)
{
	mach_e_proc_init(p);

	if (p->p_emul != epp->ep_esch->es_emul) {
		struct thread *td = TAILQ_FIRST(&p->p_threads);
		KASSERT(td != NULL, ("no threads in proc"));
		mach_e_lwp_fork(NULL, td);
	}
}

void
mach_e_proc_fork(struct proc *p2, struct thread *td1, int forkflags)
{
	mach_e_proc_fork1(p2, td1, 1);
}

void
mach_e_proc_fork1(struct proc *p2, struct thread *td1, int allocate)
{
	struct mach_emuldata *med1;
	struct mach_emuldata *med2;
	int i;

	/*
	 * For Darwin binaries, p2->p_emuldata has already been
	 * allocated, no need to throw it away and allocate it again.
	 */
	if (allocate)
		p2->p_emuldata = NULL;

	mach_e_proc_init(p2);

	med1 = p2->p_emuldata;
	med2 = td1->td_proc->p_emuldata;

	/*
	 * Exception ports are inherited between forks,
	 * but we need to double their reference counts,
	 * since the ports are referenced by rights in the
	 * parent and in the child.
	 *
	 * XXX we need to convert all the parent's rights
	 * to the child namespace. This will make the
	 * following fixup obsolete.
	 */
	for (i = 0; i <= MACH_EXC_MAX; i++) {
		med1->med_exc[i] = med2->med_exc[i];
		if (med1->med_exc[i] !=  NULL)
			med1->med_exc[i]->mp_refcount *= 2;
	}
}

void
mach_e_proc_exit(struct proc *p)
{
	struct mach_emuldata *med;
	struct mach_right *mr;
	struct thread *td;
	int i;

	/* There is only one lwp remaining... */
	td = TAILQ_FIRST(&p->p_threads);
	KASSERT(td != NULL, ("no threads in proc"));
	mach_e_lwp_exit(td);

	med = (struct mach_emuldata *)p->p_emuldata;

	rw_wlock(&med->med_rightlock);
	while ((mr = LIST_FIRST(&med->med_right)) != NULL)
		mach_right_put_exclocked(mr, MACH_PORT_TYPE_ALL_RIGHTS);
	rw_wunlock(&med->med_rightlock);

	MACH_PORT_UNREF(med->med_bootstrap);

	/*
	 * If the lock on this task exception handler is held,
	 * release it now as it will never be released by the
	 * exception handler.
	 */
	if (rw_wowned(&med->med_exclock))
		wakeup(&med->med_exclock);

	/*
	 * If the kernel and host port are still referenced, remove
	 * the pointer to this process' struct proc, as it will
	 * become invalid once the process will exit.
	 */
	med->med_kernel->mp_datatype = MACH_MP_NONE;
	med->med_kernel->mp_data = NULL;
	MACH_PORT_UNREF(med->med_kernel);

	med->med_host->mp_datatype = MACH_MP_NONE;
	med->med_host->mp_data = NULL;
	MACH_PORT_UNREF(med->med_host);

	for (i = 0; i <= MACH_EXC_MAX; i++)
		if (med->med_exc[i] != NULL)
			MACH_PORT_UNREF(med->med_exc[i]);

	rw_destroy(&med->med_exclock);
	rw_destroy(&med->med_rightlock);
	free(med, M_MACH);
	p->p_emuldata = NULL;
}

void
mach_e_lwp_exit(struct thread *td)
{
	struct mach_thread_emuldata *mle;

	mach_semaphore_cleanup(td);

#ifdef DIAGNOSTIC
	if (td->td_emuldata == NULL) {
		printf("lwp_emuldata already freed\n");
		return;
	}
#endif
	mle = td->td_emuldata;

	mle->mle_kernel->mp_data = NULL;
	mle->mle_kernel->mp_datatype = MACH_MP_NONE;
	MACH_PORT_UNREF(mle->mle_kernel);

	free(mle, M_MACH);
	td->td_emuldata = NULL;
}

