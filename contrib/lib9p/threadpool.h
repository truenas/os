/*
 * Copyright 2016 Jakub Klama <jceel@FreeBSD.org>
 * All rights reserved
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted providing that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef	LIB9P_THREADPOOL_H
#define	LIB9P_THREADPOOL_H

#include <stdbool.h>
#include <pthread.h>
#include <sys/queue.h>
#include "lib9p.h"

STAILQ_HEAD(l9p_request_queue, l9p_request);

struct l9p_threadpool {
    struct l9p_connection *	ltp_conn;
    struct l9p_request_queue	ltp_queue;
    int 			ltp_size;
    pthread_mutex_t		ltp_mtx;
    pthread_cond_t		ltp_work_cv;
    pthread_cond_t		ltp_flush_cv;
    LIST_HEAD(, l9p_worker)	ltp_workers;
};

struct l9p_worker {
    struct l9p_threadpool *	ltw_tp;
    pthread_t			ltw_thread;
    bool			ltw_exiting;
    LIST_ENTRY(l9p_worker)	ltw_link;
};

void	l9p_threadpool_flushee_done(struct l9p_request *);
int	l9p_threadpool_init(struct l9p_threadpool *, int);
void	l9p_threadpool_run(struct l9p_threadpool *, struct l9p_request *);
int	l9p_threadpool_shutdown(struct l9p_threadpool *);
int	l9p_threadpool_tflush(struct l9p_request *);

#endif	/* LIB9P_THREADPOOL_H  */
