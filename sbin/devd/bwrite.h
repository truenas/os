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

/*
 * Buffered data writer.
 *
 * The buffer that we're writing may consist either of streaming
 * data (sent with write or send-with-no-flags) or record-oriented
 * data (sent with send(fd, data, len, MSG_EOR).
 *
 * If the buffer contains record-oriented data, we have a length
 * per record and a maximum number of records.  In any case, we
 * have an overall buffer size as well.
 *
 * Writes could be done non-blocking and using select()/kqueue, or via
 * POSIX threads, or some combination thereof.
 *
 * The current implementation always uses threads.
 */

#include <sys/cdefs.h>
#include <pthread.h>

/*
 * This enumerates writer state and "check" results.
 * (Not all of these actually go into the bw_state field.)
 */
enum bw_state {
	BW_ERROR = -1,		/* error (bw_init only), sets errno */
	BW_OPEN,		/* seems to be normal */
	BW_INTR,		/* polling interrupted */
	BW_BLOCKED,		/* polling says write is blocked */
	BW_WFAIL,		/* write failed: receiver is gone */
	BW_RHUP,		/* poll indicated socket closed */
	BW_RREADY,		/* poll indicated read data is ready */
	BW_DONE,		/* write stopped via bw_fini() */
};

enum bw_put_result {
	BW_PUT_OK,		/* put succeeded */
	BW_PUT_RFULL,		/* put failed, record buffers full */
	BW_PUT_DFULL,		/* put failed, data buffers full */
	BW_PUT_ERROR,		/* put failed, other error */
};

struct bwrite {
	int	bw_fd;		/* file descriptor */
	pthread_t bw_thread;	/* the thread that is sending data */
	pthread_mutex_t bw_lock; /* lock for cv, state, etc below */
	pthread_cond_t bw_cv;	/* condition var for waiting */
	enum bw_state bw_state;	/* current thread state */
	int	bw_error;	/* saved errno for BW_ERROR state */
	int	bw_flags;	/* record vs stream */
	int	bw_iflags;	/* internal flags */
	size_t	bw_bufsize;	/* total buffer size (>= 1) */
	size_t	bw_maxrec;	/* max number of records (>= 1) */
	size_t	bw_dput;	/* data "put" index */
	size_t	bw_dget;	/* data "get" index */
	size_t	bw_rput;	/* record "put" index */
	size_t	bw_rget;	/* record "get" index */
	char	*bw_dbuf;	/* the data buffer itself */
	size_t	*bw_reclens;	/* lengths for individual records */
};

__BEGIN_DECLS

/*
 * Initialize a buffered writer.
 * Returns BW_OPEN on success, BW_ERROR (with errno set) on failure.
 *
 * If streaming, nrec can be 0, otherwise it must be at least 1.
 */
int	bw_init(struct bwrite *bw, int fd,
		char *dbuf, size_t dsize,	/* data buffer + size */
		size_t *reclens, size_t nrecs,	/* record lengths + n */
		int flags);
#define	BW_STREAMING	0x00	/* streaming data mode */
#define	BW_RECORDS	0x01	/* record oriented */
#define	BW_NONBLOCK	0x02	/* set non-blocking mode (else clear) */
#define	BW_ALL		0x03	/* all user flags */
/* not yet: BW_PERSISTENT - user can supply new fd after write fail */

/*
 * Test the state of a buffered writer.  Can do a poll() as
 * well, if flags is nonzero.
 *
 * Returns BW_OPEN if operating normally, BW_WFAIL indicates that a
 * write failed and the output is now dead.  BW_RHUP and BW_RREADY
 * can occur if you ask to check READ and/or HUP.
 */
enum bw_state bw_check(struct bwrite *bw, int flags);
#define	BW_CHECK_READ	0x01	/* check for read ready */
#define	BW_CHECK_HUP	0x02	/* check for "hangup" (socket closed) */

#ifdef notyet
/*
 * Test the state of many (n) writers.  If they're in polling mode,
 * this will let all of those that can write, write.
 */
void	bw_check_multi(struct bwrite **bwp, struct bw_state *statep, size_t n);
#endif

/*
 * Add another record, or more data to streaming output.
 * Returns one of the bw_put_result values.
 */
enum bw_put_result bw_put(struct bwrite *bw, void *data, size_t dsize);

/*
 * Terminate a buffered writer.  Collects any thread.
 * Does not close the file descriptor (this is left to
 * the caller, including after a failed bw_init).
 */
void	bw_fini(struct bwrite *bw);

__END_DECLS
