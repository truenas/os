/*
 * Copyright (c) 2016 iXsystems, Inc.
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/types.h>
#include <sys/socket.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "bwrite.h"

/*
 * Internal flags (bw_iflags field).
 */
#define	BI_THREAD	0x01	/* using a thread to write (else polling) */
#define	BI_THREAD_GONE	0x02	/* thread has exited */
#define	BI_DFULL	0x04	/* data buffer is totally full */
#define	BI_RFULL	0x08	/* records buffer is totally full */

/*
 * How much room is there for data?
 *
 * (bw must be locked if threading.)
 *
 * There are basically two cases, either the valid data wrap
 * across the edge of the buffer (dget > dput), or the valid
 * data are somewhere in the middle of the buffer, i.e.:
 *
 *     buffer: D DATA...junk...VALI
 *                   ^         ^
 *                 dput      dget
 * or:
 *     buffer: ...junk....VALID DATA...junk...
 *                        ^         ^
 *                      dget      dput
 *
 * One tricky edge case occurs, when dget==dput: the buffer
 * is either completely empty, or completely full.  We use the
 * iflags to tell which.
 */
static inline size_t
_bw_data_spaceavail(struct bwrite *bw)
{
	size_t space;

	if (bw->bw_dget > bw->bw_dput)
		return bw->bw_dget - bw->bw_dput;

	if (bw->bw_dget < bw->bw_dput) {
		space = bw->bw_bufsize - bw->bw_dput;
		space += bw->bw_dget;
		return (space);
	}

	/*
	 * The get and put indices are equal, so we're either
	 * completely empty or completely full, and we have to
	 * tell this apart by the DFULL flag.
	 */
	return (bw->bw_iflags & BI_DFULL) ? 0 : bw->bw_bufsize;
}

/*
 * How much room is there for records?
 * Can only be used if this is record-oriented.
 *
 * (bw must be locked if threading.)
 */
static inline int
_bw_recs_spaceavail(struct bwrite *bw)
{

	/* Same as for data_spaceavail, really. */
	if (bw->bw_rget > bw->bw_rput)
		return bw->bw_rget - bw->bw_rput;
	if (bw->bw_rget < bw->bw_rput)
		return (bw->bw_maxrec - bw->bw_rput) + bw->bw_rget;
	return (bw->bw_iflags & BI_RFULL) ? 0 : bw->bw_maxrec;
}

/*
 * Return true (1) if there is some data to send, 0 if not.
 */
static inline int
_bw_has_data_to_send(struct bwrite *bw)
{

	/*
	 * If record oriented, go by rget vs rput.  (We could
	 * have a bunch of zero-length records to write.)
	 */
	if (bw->bw_flags & BW_RECORDS)
		return bw->bw_rget != bw->bw_rput ||
		    (bw->bw_iflags & BI_RFULL);

	/* Otherwise it's dget vs dput. */
	return bw->bw_dget != bw->bw_dput || (bw->bw_iflags & BI_DFULL);
}

/*
 * How much record-style data is there to write right now?  It
 * may be in two sections, due to wrapping.  Fill in one or two
 * iov's for the next send/sendmsg.  (The actual length is of
 * course stored in bw->bw_reclens[bw->bw_rget].)
 *
 * Applies only for record style output; bw must be locked if
 * threading; and there must be a record to send.
 */
static inline int
_bw_record_data_iovs(struct bwrite *bw, struct iovec *iov, size_t *alen)
{
	size_t to_end, reclen;

	iov[0].iov_base = &bw->bw_dbuf[bw->bw_dget];
	reclen = bw->bw_reclens[bw->bw_rget];
	*alen = reclen;
	to_end = bw->bw_bufsize - bw->bw_dget;
	if (reclen <= to_end) {
		iov[0].iov_len = reclen;
		return 1;
	}

	/*
	 * Data are in two segments, one from rget to end, and
	 * one from beginning to cover reclen.
	 */
	iov[0].iov_len = to_end;
	iov[1].iov_base = &bw->bw_dbuf[0];
	iov[1].iov_len = reclen - to_end;
	return 2;
}

/*
 * How much stream-style data is there to write right now?
 * Same idea as for records, but there are no record lengths
 * and record boundaries, just one big blob of data-stream.
 *
 * Applies only for stream style output; bw must be locked if
 * threading; and there must be some data.
 */
static inline int
_bw_stream_data_iovs(struct bwrite *bw, struct iovec *iov, size_t *alen)
{
	size_t len;

	iov[0].iov_base = &bw->bw_dbuf[bw->bw_dget];
	if (bw->bw_dget < bw->bw_dput) {
		len = bw->bw_dput - bw->bw_dget;
		iov[0].iov_len = len;
		*alen = len;
		return 1;
	}
	len = bw->bw_bufsize - bw->bw_dget;
	iov[0].iov_len = len;
	iov[1].iov_base = &bw->bw_dbuf[0];
	iov[1].iov_len = bw->bw_dput;
	len += bw->bw_dput;
	*alen = len;
	return 2;
}

/*
 * Add some data to buffer (increment dput by dsize, and rput by 1 if
 * we're using records; caller must already have verified that there
 * is space for both).
 *
 * It's OK for datalen to be zero here (for empty records).
 *
 * Assumes bw is locked if needed.
 */
static inline void
_bw_add_data(struct bwrite *bw, void *data, size_t datalen)
{
	size_t to_end;

	if (bw->bw_flags & BW_RECORDS) {
		/*
		 * Save record length in next slot.  If this
		 * takes up the last slot, mark the record
		 * table as full.
		 */
		bw->bw_reclens[bw->bw_rput] = datalen;
		if (++bw->bw_rput >= bw->bw_maxrec)
			bw->bw_rput = 0;
		if (bw->bw_rput == bw->bw_rget)
			bw->bw_iflags |= BI_RFULL;
	}
	to_end = bw->bw_bufsize - bw->bw_dput;
	if (datalen > to_end) {
		/*
		 * These data fill the buffer past the end.
		 * Write the first part, then wrap and shrink
		 * data to remaining (nonempty) part.
		 */
		memcpy(&bw->bw_dbuf[bw->bw_dput], data, to_end);
		bw->bw_dput = 0;
		data = (char *)data + to_end;
		datalen -= to_end;
	}
	if (datalen > 0) {
		/*
		 * Copy remaining (maybe all) data and adjust dput
		 * index, wrapping to zero if we reach the end
		 * of the buffer.
		 *
		 * If this makes dput catch up to dget, the
		 * data buffer is now 100% full.
		 */
		memcpy(&bw->bw_dbuf[bw->bw_dput], data, datalen);
		bw->bw_dput += datalen;
		if (bw->bw_dput >= bw->bw_bufsize)
			bw->bw_dput = 0;
		if (bw->bw_dput == bw->bw_dget)
			bw->bw_flags |= BI_DFULL;
	}
}

/*
 * Discard one record (increment rget).
 *
 * Assumes bw is locked if needed, and that discarding a record
 * makes sense.
 */
static inline void
_bw_remove_rec(struct bwrite *bw)
{
	if (++bw->bw_rget >= bw->bw_maxrec)
		bw->bw_rget = 0;
	bw->bw_iflags &= ~BI_RFULL;
}

/*
 * Discard some data (increment dget).  Note, it's OK for datalen
 * to be zero here (for empty records).
 *
 * Assumes bw is locked if needed.
 */
static inline void
_bw_remove_data(struct bwrite *bw, size_t datalen)
{
	size_t to_end;

	if (datalen > bw->bw_bufsize) {
		printf("internal error: datalen %zu exceeds bufsize %zu "
		    "(fd %d)\n", datalen, bw->bw_bufsize, bw->bw_fd);
		bw->bw_state = BW_ERROR;
		bw->bw_error = EDOOFUS;
		return;
	}
	to_end = bw->bw_bufsize - bw->bw_dget;
	if (datalen > to_end) {
		/*
		 * The data being discarded go to the end and
		 * wrap through the beginning.  "Use up" the
		 * span to the end, resetting dget to 0.
		 * Note that datalen remains positive even
		 * after subtraction here.
		 */
		datalen -= to_end;
		bw->bw_dget = 0;
	}

	/*
	 * Discard datalen byes from the front (dget).  If this gets
	 * us all the way to the end of the buffer, wrap to zero.
	 * In any case, if we discarded data, we're no longer full.
	 * (If not, we are in the same fullness state as before.)
	 */
	bw->bw_dget += datalen;
	if (bw->bw_dget >= bw->bw_bufsize)
		bw->bw_dget = 0;
	if (datalen > 0)
		bw->bw_iflags &= ~BI_DFULL;
}

/*
 * Attempt to send data (from thread or from poll code).
 *
 * This may only be called when there is something to send.
 */
static int
_bw_trysend(struct bwrite *bw)
{
	struct msghdr msghdr;
	struct iovec iov[2];
	ssize_t nsent;
	size_t dsize;
	int error, flags, threaded, niov;

	if (bw->bw_flags & BW_RECORDS) {
		niov = _bw_record_data_iovs(bw, iov, &dsize);
		flags = MSG_EOR;
	} else {
		niov = _bw_stream_data_iovs(bw, iov, &dsize);
		if (dsize == 0)
			return 0;	/* nothing to send */
		flags = 0;
	}

	threaded = bw->bw_iflags & BI_THREAD;
	if (threaded) {
		/* Release lock so that writers can add data. */
		(void) pthread_mutex_unlock(&bw->bw_lock);
	}
	flags |= MSG_NOSIGNAL;	/* no SIGPIPEs, please */
	if (niov > 1) {
		memset(&msghdr, '\0', sizeof(msghdr));
		msghdr.msg_iov = iov;
		msghdr.msg_iovlen = niov;
		nsent = sendmsg(bw->bw_fd, &msghdr, flags);
	} else
		nsent = send(bw->bw_fd, iov[0].iov_base, dsize, flags);
	error = nsent < 0 ? errno : 0;
	if (threaded)
		(void) pthread_mutex_lock(&bw->bw_lock);
	if (nsent >= 0) {
		/*
		 * Success: sent some data, maybe as a record.
		 */
		if (bw->bw_flags & BW_RECORDS) {
			/*
			 * If nsent < dsize, it's not really clear what
			 * to do here.  For now, we'll just assume that
			 * the the whole record got sent.
			 */
			_bw_remove_rec(bw);
			_bw_remove_data(bw, dsize);
		} else {
			/*
			 * It's easy to handle arbitrary nsent here.
			 */
			_bw_remove_data(bw, (size_t)nsent);
		}
	}
	return error;
}

/*
 * Writer thread, spun off by bw_init.
 */
static void *
_bw_writer_thread(void *bw0)
{
	struct bwrite *bw = bw0;
	int error;

	/* if this lock call fails we're doomed, so just don't check */
	(void) pthread_mutex_lock(&bw->bw_lock);
	for (;;) {
		/*
		 * If we don't seen to have anything to do, wait for
		 * a condition signal (more data to write, or time to
		 * end).
		 */
		if (bw->bw_state == BW_OPEN) {
			error = pthread_cond_wait(&bw->bw_cv, &bw->bw_lock);
			if (error) {
				bw->bw_error = error;
				bw->bw_state = BW_ERROR;
			}
		}

		/*
		 * If we're done, stop.  This includes write-error state.
		 */
		if (bw->bw_state == BW_DONE || bw->bw_state == BW_ERROR)
			break;

		/*
		 * When we get here, we should generally have dput
		 * and/or rput different from dget and/or rget.
		 * This means we have data to write.
		 *
		 * If they're all equal, though, just set to OPEN
		 * and go back around the loop.
		 */
		if (bw->bw_state != BW_OPEN && bw->bw_state != BW_BLOCKED) {
			printf("internal error: bw_state %d on fd %d\n",
			    (int)bw->bw_state, bw->bw_fd);
			bw->bw_error = EDOOFUS;
			bw->bw_state = BW_ERROR;
		}

		if (!_bw_has_data_to_send(bw)) {
			bw->bw_state = BW_OPEN;
			continue;
		}

		/*
		 * Set state to "write blocked" and try to send data.
		 * On EINTR, just keep going.  On success we have
		 * dput and/or rput updated.  On other errors, record
		 * failure.
		 */
		bw->bw_state = BW_BLOCKED;
		error = _bw_trysend(bw);
		if (error != 0 && error != EINTR) {
			bw->bw_error = error;
			bw->bw_state = BW_ERROR;
		}
	}
	bw->bw_iflags |= BI_THREAD_GONE;
#ifdef unneeded
	pthread_cond_signal(&bw->bw_cv);
#endif
	(void) pthread_mutex_unlock(&bw->bw_lock);
	return NULL;
}

int
bw_init(struct bwrite *bw, int fd,
	char *dbuf, size_t dsize,
	size_t *reclens, size_t nrecs,
	int flags)
{
	size_t minrecs;
	int desired_nonblock;
	int error, fdflags, iflags;

	/*
	 * Verify arguments.
	 */
	minrecs = flags & BW_RECORDS ? 1 : 0;
	if ((flags & ~BW_ALL) || dsize < 1 || nrecs < minrecs) {
		errno = EINVAL;
		return BW_ERROR;
	}
	if (flags & BW_NONBLOCK) {
		/*
		 * Non-blocking mode: we'll poll this fd.
		 */
		iflags = 0;
		desired_nonblock = O_NONBLOCK;
	} else {
		/*
		 * Blocking mode: we'll use a thread.
		 */
		iflags = BI_THREAD;
		desired_nonblock = 0;
	}
	fdflags = fcntl(fd, F_GETFL);
	if (fdflags < 0)
		return BW_ERROR;
	if ((fdflags & O_NONBLOCK) != desired_nonblock) {
		fdflags &= ~O_NONBLOCK;
		fdflags |= desired_nonblock;
		if (fcntl(fd, F_SETFL, fdflags) < 0)
			return BW_ERROR;
	}
	if (iflags & BI_THREAD) {
		error = pthread_mutex_init(&bw->bw_lock, NULL);
		if (error) {
			errno = error;
			return BW_ERROR;
		}
		error = pthread_cond_init(&bw->bw_cv, NULL);
		if (error) {
			errno = error;
			return BW_ERROR;
		}
	}
	bw->bw_fd = fd;
	bw->bw_state = BW_OPEN;
	bw->bw_flags = flags;
	bw->bw_iflags = iflags;
	bw->bw_bufsize = dsize;
	bw->bw_maxrec = nrecs;
	bw->bw_dput = bw->bw_dget = 0;
	bw->bw_rput = bw->bw_rget = 0;
	bw->bw_dbuf = dbuf;
	bw->bw_reclens = reclens;

	if (iflags & BI_THREAD) {
		/*
		 * Spin off the thread that does the writing.
		 */
		error = pthread_create(&bw->bw_thread, NULL,
		    _bw_writer_thread, bw);
		if (error) {
			errno = error;
			return BW_ERROR;
		}
	}
	return BW_OPEN;
}

/*
 * Test the state of a buffered writer.
 *
 * Callers can also ask whether it's possible to *read* from the
 * file descriptor.
 */
enum bw_state
bw_check(struct bwrite *bw, int flags)
{
	struct pollfd fds[1];
	enum bw_state retval;
	int pollin, pollout, do_poll;
	int nready, error, threaded;

	retval = BW_OPEN;

	/*
	 * If using polling mode or instructed to
	 * check for readability / EOF, do a poll.
	 *
	 * Note that we must not use any of the write info
	 * fields here if we're using threads, since we are
	 * not holding a mutex now, so make sure pollout==0
	 * if we're threaded.
	 */
	threaded = bw->bw_iflags & BI_THREAD;
	do_poll = 0;
	if (!threaded && _bw_has_data_to_send(bw)) {
		pollout = POLLOUT;
		do_poll = 1;
	} else
		pollout = 0;
	if (flags & BW_CHECK_READ) {
		do_poll = 1;
		pollin = POLLIN;
	} else
		pollin = 0;
	if (flags & BW_CHECK_HUP)
		do_poll = 1;
	while (do_poll && retval == BW_OPEN) {
		/*
		 * Documentation says we get POLLERR, POLLHUP, and
		 * POLLNVAL regardless of flags set, but let's set
		 * them explicitly anyway.
		 */
		fds[0].fd = bw->bw_fd;
		fds[0].events = pollin | pollout | POLLERR | POLLHUP | POLLNVAL;
		nready = poll(fds, 1, 0);
		if (nready < 0) {
			/* Some kind of failure. */
			if (errno == EINTR)
				return BW_INTR;
			return BW_WFAIL;	/* probably EBADF */
		}

		if (nready == 0) {
			/* Nothing to report or do: end the loop. */
			break;
		}

		/* We have something to do and/or report. */
		if (fds[0].revents & POLLHUP)
			retval = BW_RHUP;	/* report HUP */
		else if (fds[0].revents & POLLIN)
			retval = BW_RREADY;	/* report input */
		if (fds[0].revents & pollout) {
			/* Try doing output. */

			if (!_bw_has_data_to_send(bw)) {
				/* Yay, we sent it all! */
				break;
			}

			/*
			 * Try sending data.  On failure replace
			 * normal (BW_OPEN) return with error but leave
			 * RHUP/RREADY return values alone.  On EAGAIN
			 * error, we tried to write too much, and/or
			 * poll lied; stop here.
			 *
			 * ?? need special check for EINTR?
			 */
			error = _bw_trysend(bw);
			if (error == EAGAIN)
				break;
			if (error && retval == BW_OPEN)
				retval = BW_WFAIL;
		}

		/*
		 * If we are threaded, we only want to go through
		 * this loop once (to check for input and/or EOF
		 * conditions).  Otherwise repeat until we have
		 * something interesting to report, or have written
		 * everything and thus done a "break" earlier.
		 */
		if (threaded)
			break;
	}
	if (!threaded || retval != BW_OPEN) {
		if (retval == BW_OPEN && _bw_has_data_to_send(bw))
			retval = BW_BLOCKED;
		return retval;
	}

	/*
	 * We're using threads to write.  This means we must
	 * take the lock to examine the state.
	 */
	error = pthread_mutex_lock(&bw->bw_lock);
	if (error)
		return BW_INTR;	/* ??? */
	retval = bw->bw_state;
	if (retval == BW_ERROR)
		errno = bw->bw_error;
	pthread_mutex_unlock(&bw->bw_lock);
	return (retval);
}

int
bw_get_qdepth(struct bwrite *bw, size_t *ddepth, size_t *dsize,
    size_t *rdepth, size_t *nrecs)
{
	int error, threaded;

	*dsize = bw->bw_bufsize;
	*nrecs = bw->bw_maxrec;
	threaded = bw->bw_iflags & BI_THREAD;
	if (threaded) {
		error = pthread_mutex_lock(&bw->bw_lock);
		if (error) {
			errno = error;
			*ddepth = *rdepth = 0;
			return -1;
		}
	}
	*ddepth = bw->bw_bufsize - _bw_data_spaceavail(bw);
	*rdepth = bw->bw_maxrec - _bw_recs_spaceavail(bw);
	if (threaded)
		(void) pthread_mutex_unlock(&bw->bw_lock);
	return 0;
}

enum bw_put_result
bw_put(struct bwrite *bw, void *data, size_t dsize)
{
	int error, threaded;
	enum bw_put_result retval;

	error = 0;
	threaded = bw->bw_iflags & BI_THREAD;
	if (threaded) {
		error = pthread_mutex_lock(&bw->bw_lock);
		if (error) {
			/* didn't get a lock, so return early */
			errno = error;
			return BW_PUT_ERROR;
		}

		/*
		 * See if writer thread recorded failure and/or died.
		 * It should be in OPEN or BLOCKED state until we
		 * tell it to finish.
		 */
		if (bw->bw_iflags & BI_THREAD_GONE)
			error = EIO;	/* or EBADF? */
		if (bw->bw_state != BW_OPEN && bw->bw_state != BW_BLOCKED)
			error = bw->bw_error;
	}

	if (error == 0) {
		/*
		 * Writer thread is still good.  Enqueue data
		 * if there's room, or say we're full if not.
		 */
		if ((bw->bw_flags & BW_RECORDS) && _bw_recs_spaceavail(bw) < 1)
		    	retval = BW_PUT_RFULL;
		else if (_bw_data_spaceavail(bw) < dsize)
			retval = BW_PUT_DFULL;
		else {
			_bw_add_data(bw, data, dsize);
			retval = BW_PUT_OK;
		}
	} else {
		errno = error;
	}

	if (threaded) {
		(void) pthread_mutex_unlock(&bw->bw_lock);
		if (error == 0) {
			error = pthread_cond_signal(&bw->bw_cv);
			if (error)
				errno = error;
		}
	}

	return (error ? BW_PUT_ERROR : retval);
}

/*
 * Release buffered writer.  If it has a thread, prod the
 * thread and wait for it to finish (by joining it).
 */
void
bw_fini(struct bwrite *bw)
{
	int error, threaded;

	threaded = bw->bw_iflags & BI_THREAD;
	if (threaded) {
		error = pthread_mutex_lock(&bw->bw_lock);
		if (error)
			return;	/* nothing else we can do */
		bw->bw_state = BW_DONE;
		pthread_cond_signal(&bw->bw_cv);
#ifdef unneeded
		while ((bw->bw_iflags & BI_THREAD_GONE) == 0) {
			error = pthread_cond_wait(&bw->bw_cv, &bw->bw_lock);
			if (error)
				break;
		}
#endif
		(void) pthread_mutex_unlock(&bw->bw_lock);
		(void) pthread_join(bw->bw_thread, NULL);
		(void) pthread_cond_destroy(&bw->bw_cv);
		(void) pthread_mutex_destroy(&bw->bw_lock);
	}
}
