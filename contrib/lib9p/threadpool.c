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

#include <errno.h>
#include <stdlib.h>
#include <pthread.h>
#if defined(__FreeBSD__)
#include <pthread_np.h>
#endif
#include <sys/queue.h>
#include "lib9p.h"
#include "threadpool.h"

static void *
l9p_worker(void *arg)
{
	struct l9p_worker *worker = (struct l9p_worker *)arg;
	struct l9p_request *req;

	for (;;) {
		pthread_mutex_lock(&worker->ltw_tp->ltp_mtx);

		while (STAILQ_EMPTY(&worker->ltw_tp->ltp_queue)
		    && !worker->ltw_exiting)
			pthread_cond_wait(&worker->ltw_tp->ltp_cv,
			    &worker->ltw_tp->ltp_mtx);

		if (worker->ltw_exiting)
			break;

		req = STAILQ_FIRST(&worker->ltw_tp->ltp_queue);
		STAILQ_REMOVE_HEAD(&worker->ltw_tp->ltp_queue, lr_link);

		pthread_mutex_unlock(&worker->ltw_tp->ltp_mtx);
		l9p_dispatch_request(req);
	}

	pthread_mutex_unlock(&worker->ltw_tp->ltp_mtx);
	return (NULL);
}

int
l9p_threadpool_init(struct l9p_threadpool *tp, int size)
{
	struct l9p_worker *worker;
#if defined(__FreeBSD__)
	char threadname[16];
#endif
	int error;
	int i;

	if (size <= 0)
		return (EINVAL);
	error = pthread_mutex_init(&tp->ltp_mtx, NULL);
	if (error)
		return (error);
	error = pthread_cond_init(&tp->ltp_cv, NULL);
	if (error) {
		pthread_mutex_destroy(&tp->ltp_mtx);
		return (error);
	}
	STAILQ_INIT(&tp->ltp_queue);
	LIST_INIT(&tp->ltp_workers);

	for (i = 0; i < size; i++) {
		worker = calloc(1, sizeof(struct l9p_worker));
		worker->ltw_tp = tp;
		error = pthread_create(&worker->ltw_thread, NULL, &l9p_worker,
		    (void *)worker);
		if (error) {
			free(worker);
			break;
		}

#if defined(__FreeBSD__)
		sprintf(threadname, "9p-worker:%d", i);
		pthread_set_name_np(worker->ltw_thread, threadname);
#endif

		LIST_INSERT_HEAD(&tp->ltp_workers, worker, ltw_link);
	}
	/* if we made any workers, that's sufficient */
	if (i > 0)
		error = 0;

	return (error);
}

int
l9p_threadpool_enqueue(struct l9p_threadpool *tp, struct l9p_request *req)
{

	pthread_mutex_lock(&tp->ltp_mtx);
	STAILQ_INSERT_TAIL(&tp->ltp_queue, req, lr_link);
	pthread_cond_signal(&tp->ltp_cv);
	pthread_mutex_unlock(&tp->ltp_mtx);

	return (0);
}

int
l9p_threadpool_shutdown(struct l9p_threadpool *tp)
{
	struct l9p_worker *worker, *tmp;

	LIST_FOREACH_SAFE(worker, &tp->ltp_workers, ltw_link, tmp) {
		pthread_mutex_lock(&tp->ltp_mtx);
		worker->ltw_exiting = true;
		pthread_cond_broadcast(&tp->ltp_cv);
		pthread_mutex_unlock(&tp->ltp_mtx);
		pthread_join(worker->ltw_thread, NULL);
		LIST_REMOVE(worker, ltw_link);
		free(worker);
	}
	pthread_cond_destroy(&tp->ltp_cv);
	pthread_mutex_destroy(&tp->ltp_mtx);

	return (0);
}
