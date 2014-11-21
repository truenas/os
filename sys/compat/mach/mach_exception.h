/*-
 * Copyright (c) 2003 The NetBSD Foundation, Inc.
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
 *
 *	$FreeBSD$
 */

#ifndef _MACH_EXCEPTION_H_
#define _MACH_EXCEPTION_H_
#include <sys/mach/exception_types.h>

/* really should be in mach_signal.h or siginfo.h */
#define MACH_BUS_ADRALN         1

#define MACH_SEGV_MAPERR        1
#define MACH_SEGV_ACCERR        2

#define MACH_TRAP_BRKPT         1

#define MACH_ILL_ILLOPC         1
#define MACH_ILL_ILLTRP         2
#define MACH_ILL_PRVOPC         3

/*
 * Exception codes. Values < 0x10000 are machine dependent, and
 * are defined in sys/<arch>/include/mach_machdep.h.
 */
#define MACH_EXC_UNIX_BAD_SYSCALL	0x10000	/* unused ? */
#define MACH_EXC_UNIX_BAD_PIPE		0x10001	/* unused ? */
#define MACH_EXC_UNIX_ABORT		0x10002 /* unused ? */
#define MACH_SOFT_SIGNAL		0x10003

/* Exception behaviors and associated messages Id */

#define MACH_EXC_RAISE_MSGID		2401
#define MACH_EXC_RAISE_STATE_MSGID	2402
#define MACH_EXC_RAISE_STATE_IDENTITY_MSGID	2403

/* exception_raise. The kernel is the client, not the server */

typedef struct {
	mach_msg_header_t req_msgh;
	mach_msg_body_t req_body;
	mach_msg_port_descriptor_t req_thread;
	mach_msg_port_descriptor_t req_task;
	mach_ndr_record_t req_ndr;
	mach_exception_type_t req_exc;
	mach_msg_type_number_t req_codecount;
	mach_integer_t req_code[2];
	mach_msg_trailer_t req_trailer;
} mach_exception_raise_request_t;

typedef struct {
	mach_msg_header_t rep_msgh;
	mach_ndr_record_t rep_ndr;
	mach_kern_return_t rep_retval;
} mach_exception_raise_reply_t;

/* exception_raise_state. The kernel is the client, not the server */

typedef struct {
	mach_msg_header_t req_msgh;
	mach_ndr_record_t req_ndr;
	mach_exception_type_t req_exc;
	mach_msg_type_number_t req_codecount;
	mach_integer_t req_code[2];
	int req_flavor;
	mach_msg_type_number_t req_statecount;
	mach_natural_t req_state[144];
	mach_msg_trailer_t req_trailer;
} mach_exception_raise_state_request_t;

typedef struct {
	mach_msg_header_t rep_msgh;
	mach_ndr_record_t rep_ndr;
	mach_kern_return_t rep_retval;
} mach_exception_raise_state_reply_t;

/* exception_raise_state_identity. The kernel is the client, not the server */

typedef struct {
	mach_msg_header_t req_msgh;
	mach_msg_body_t req_body;
	mach_msg_port_descriptor_t req_thread;
	mach_msg_port_descriptor_t req_task;
	mach_ndr_record_t req_ndr;
	mach_exception_type_t req_exc;
	mach_msg_type_number_t req_codecount;
	mach_integer_t req_code[2];
	int req_flavor;
	mach_msg_type_number_t req_statecount;
	mach_natural_t req_state[144];
	mach_msg_trailer_t req_trailer;
} mach_exception_raise_state_identity_request_t;

typedef struct {
	mach_msg_header_t rep_msgh;
	mach_ndr_record_t rep_ndr;
	mach_kern_return_t rep_retval;
} mach_exception_raise_state_identity_reply_t;

struct mach_exc_info {
	int mei_flavor;
	int mei_behavior;
};

#include <sys/signalvar.h>
void mach_trapsignal(struct thread *, struct ksiginfo *);
int mach_trapsignal1(struct thread *, struct ksiginfo *);
int mach_exception(struct thread *, int, int *);

#endif /* _MACH_EXCEPTION_H_ */

