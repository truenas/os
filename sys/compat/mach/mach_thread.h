/*	$FreeBSD$ */

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

#ifndef	_MACH_THREAD_H_
#define	_MACH_THREAD_H_

#include <sys/types.h>
#include <sys/param.h>
#include <sys/signal.h>

#include <sys/mach/mach_types.h>

/* For mach_sys_syscall_thread_switch() */
#define MACH_SWITCH_OPTION_NONE	0
#define MACH_SWITCH_OPTION_DEPRESS	1
#define MACH_SWITCH_OPTION_WAIT	2
#define MACH_SWITCH_OPTION_IDLE	3

/* For mach_thread_info */
#define MACH_THREAD_BASIC_INFO 3

#define MACH_TH_STATE_RUNNING		1
#define MACH_TH_STATE_STOPPED		2
#define MACH_TH_STATE_WAITING		3
#define MACH_TH_STATE_UNINTERRUPTIBLE	4
#define MACH_TH_STATE_HALTED		5

#define MACH_TH_FLAGS_SWAPPED	1
#define MACH_TH_FLAGS_IDLE	2

#define MACH_THREAD_SCHED_TIMESHARE_INFO 10

#define MACH_THREAD_SCHED_RR_INFO	11
#define MACH_THREAD_SCHED_FIFO_INFO	12

/* For mach_policy_t */
#define MACH_THREAD_STANDARD_POLICY	1
#define MACH_THREAD_TIME_CONSTRAINT_POLICY 2
#define MACH_THREAD_PRECEDENCE_POLICY	3
#include <compat/mach/mach_vm.h>

/* These are machine dependent functions */
int cpu_mach_thread_get_state(struct thread *, int, void *, int *);
int cpu_mach_thread_set_state(struct thread *, int, void *);
int cpu_mach_create_thread(void *);
int mach_swtch_pri(struct thread *td, int pri);

#endif /* _MACH_THREAD_H_ */
