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
	struct l9p_threadpool *tp;
	struct l9p_worker *worker = arg;
	struct l9p_request *req;
	int error;

	tp = worker->ltw_tp;
	for (;;) {
		pthread_mutex_lock(&tp->ltp_mtx);

		while (STAILQ_EMPTY(&tp->ltp_queue) && !worker->ltw_exiting)
			pthread_cond_wait(&tp->ltp_work_cv, &tp->ltp_mtx);

		if (worker->ltw_exiting)
			break;

		req = STAILQ_FIRST(&tp->ltp_queue);
		STAILQ_REMOVE_HEAD(&tp->ltp_queue, lr_worklink);

		pthread_mutex_unlock(&tp->ltp_mtx);
		error = l9p_dispatch_request(req);
		l9p_respond(req, error, false);
	}

	pthread_mutex_unlock(&tp->ltp_mtx);
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
	error = pthread_cond_init(&tp->ltp_work_cv, NULL);
	if (error) {
		pthread_mutex_destroy(&tp->ltp_mtx);
		return (error);
	}
	error = pthread_cond_init(&tp->ltp_flush_cv, NULL);
	if (error) {
		pthread_cond_destroy(&tp->ltp_work_cv);
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
	if (error) {
		pthread_cond_destroy(&tp->ltp_flush_cv);
		pthread_cond_destroy(&tp->ltp_work_cv);
		pthread_mutex_destroy(&tp->ltp_mtx);
	}

	return (error);
}

/*
 * Run a request, usually by queueing it.
 */
void
l9p_threadpool_run(struct l9p_threadpool *tp, struct l9p_request *req)
{

	/*
	 * Flush requests must be handled specially, since they
	 * can cancel / kill off regular requests.  (But we can
	 * run them through the regular dispatch mechanism.)
	 */
	if (req->lr_req.hdr.type == L9P_TFLUSH) {
		(void) l9p_dispatch_request(req);
	} else {
		pthread_mutex_lock(&tp->ltp_mtx);
		STAILQ_INSERT_TAIL(&tp->ltp_queue, req, lr_worklink);
		pthread_cond_signal(&tp->ltp_work_cv);
		pthread_mutex_unlock(&tp->ltp_mtx);
	}
}

/*
 * Run a Tflush request.  Called via l9p_dispatch_request() since
 * it has some debug code in it, but not called from worker thread.
 * This means we have to do the l9p_respond in line here, unless we
 * can put this request into a worker thread.
 */
int
l9p_threadpool_tflush(struct l9p_request *req)
{
	struct l9p_connection *conn;
	struct l9p_threadpool *tp = NULL;
	struct l9p_request *flushee;
	uint16_t oldtag;
	bool first_flush, we_took_flushee = false;

	/*
	 * Find what we're supposed to flush (the flushee, as it were).
	 */
	conn = req->lr_conn;
	oldtag = req->lr_req.tflush.oldtag;
	ht_wrlock(&conn->lc_requests);
	flushee = ht_find(&conn->lc_requests, oldtag);
	if (flushee == NULL) {
		/*
		 * Nothing to flush!  The old request must have
		 * been done and gone already.  Just take this
		 * Tflush out and say "OK".
		 */
		ht_remove_locked(&conn->lc_requests, req->lr_req.hdr.tag);
		ht_unlock(&conn->lc_requests);
		l9p_respond(req, 0, false);
		return (0);
	}

	/*
	 * OK, there is something to flush.  It might already be
	 * being worked-on, in which case we may need to boot the
	 * worker in the head to wake it from some blocking call.
	 * Or, it might not be in a work thread at all yet.
	 *
	 * Meanwhile it might already have some flush operations
	 * attached to it, or not.
	 *
	 * In any case, add us to the list of Tflush-es that
	 * are waiting for this request.
	 *
	 * Note that we're still holding the hash table tag lock
	 * while we take the threadpool mutex.
	 */
	if (flushee->lr_flushstate == L9P_FLUSH_NONE) {
		/*
		 * Never been flush-requested yet.  Set up its
		 * flushq and mark it as "has flush request(s)".
		 * This is the first time someone said to flush
		 * the flushee.
		 */
		STAILQ_INIT(&flushee->lr_flushq);
		flushee->lr_flushstate = L9P_FLUSH_NOT_RUN;
		first_flush = true;
	} else
		first_flush = false;
	STAILQ_INSERT_TAIL(&flushee->lr_flushq, req, lr_flushlink);

	tp = &conn->lc_tp;
	pthread_mutex_lock(&tp->ltp_mtx);
	if (first_flush && flushee->lr_worker == NULL) {
		/*
		 * There is no work thread.  Pull the original
		 * request off the threadpool queue: we'll abort
		 * it ourselves.
		 */
		STAILQ_REMOVE(&tp->ltp_queue, flushee,
		    l9p_request, lr_worklink);
		we_took_flushee = true;
	} else {
		/*
		 * There's a work thread.  Kick it to make
		 * make it get out of blocking system calls.
		 */
		/* pthread_kill(...) -- not yet */
	}
	pthread_mutex_unlock(&tp->ltp_mtx);
	ht_unlock(&conn->lc_requests);

	if (we_took_flushee) {
		/*
		 * We took the flushee out of the work queue.
		 * There's no one else left to send a response
		 * so now we have to do that.
		 */
		ht_remove_locked(&conn->lc_requests, flushee->lr_req.hdr.tag);
		l9p_respond(flushee, EINTR, false);
	} else {
		/*
		 * We *didn't* take the flushee out of the work
		 * queue.  Instead, the worker thread has to
		 * finish the flushee somehow (whether success
		 * or failure) and then report back to us
		 * when we can reply to this flush request.
		 */
		pthread_mutex_lock(&tp->ltp_mtx);
		while (!req->lr_flushee_done)
			pthread_cond_wait(&tp->ltp_flush_cv, &tp->ltp_mtx);
		pthread_mutex_unlock(&tp->ltp_mtx);
	}

	return (0);
}

/*
 * Called from l9p_response after sending the response to a
 * request that has been marked as being-flushed (a flushee).
 * Wake anyone waiting for this request.  The flushee will be
 * free() when we return, so we had better get all of them now.
 */
void
l9p_threadpool_flushee_done(struct l9p_request *req)
{
	struct l9p_threadpool *tp = &req->lr_conn->lc_tp;
	struct l9p_request *flusher;

	pthread_mutex_lock(&tp->ltp_mtx);
	STAILQ_FOREACH(flusher, &req->lr_flushq, lr_flushlink) {
		flusher->lr_flushee_done = true;
	}
	pthread_mutex_unlock(&tp->ltp_mtx);
	pthread_cond_broadcast(&tp->ltp_flush_cv);
}

int
l9p_threadpool_shutdown(struct l9p_threadpool *tp)
{
	struct l9p_worker *worker, *tmp;

	LIST_FOREACH_SAFE(worker, &tp->ltp_workers, ltw_link, tmp) {
		pthread_mutex_lock(&tp->ltp_mtx);
		worker->ltw_exiting = true;
		pthread_cond_broadcast(&tp->ltp_work_cv);
		pthread_mutex_unlock(&tp->ltp_mtx);
		pthread_join(worker->ltw_thread, NULL);
		LIST_REMOVE(worker, ltw_link);
		free(worker);
	}
	pthread_cond_destroy(&tp->ltp_flush_cv);
	pthread_cond_destroy(&tp->ltp_work_cv);
	pthread_mutex_destroy(&tp->ltp_mtx);

	return (0);
}
