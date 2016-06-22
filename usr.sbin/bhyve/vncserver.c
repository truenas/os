/*-
 * Copyright (c) 2015 Tycho Nightingale <tycho.nightingale@pluribusnetworks.com>
 * Copyright (c) 2015 Leon Dang
 * Copyright (c) 2016 Jakub Klama <jakub@ixsystems.com>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/event.h>
#include <sys/param.h>
#include <sys/un.h>
#include <netinet/in.h>

#include <assert.h>
#include <pthread.h>
#include <pthread_np.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <cpuid.h>
#include <rfb/rfb.h>

#include "bhyvegc.h"
#include "console.h"

static int vncserver_debug = 0;
#define	DPRINTF(params) if (vncserver_debug) printf params
#define	WPRINTF(params) printf params

struct vncserver_softc {
	struct bhyvegc_image *	vs_gc;
	pthread_t		vs_tid;
	rfbScreenInfoPtr	vs_screen;
	int			vs_width;
	int			vs_height;
	int			vs_conn_wait;
	int			vs_sending;
	pthread_mutex_t 	vs_mtx;
	pthread_cond_t 		vs_cond;

	int			vs_hw_crc;
	uint32_t *		vs_crc;	/* WxH crc cells */
	uint32_t *		vs_crc_tmp; /* buffer to store single crc row */
	int			vs_crc_width;
	int			vs_crc_height;
};

#define RFB_MAX_WIDTH			2000
#define RFB_MAX_HEIGHT			1200
#define	RFB_ZLIB_BUFSZ			RFB_MAX_WIDTH*RFB_MAX_HEIGHT*4

#define PIX_PER_CELL	32
#define	PIXCELL_SHIFT	5
#define PIXCELL_MASK	0x1F

/* percentage changes to screen before sending the entire screen */
#define RFB_SEND_ALL_THRESH             25

/*
 * Calculate CRC32 using SSE4.2; Intel or AMD Bulldozer+ CPUs only
 */
static __inline uint32_t
fast_crc32(void *buf, int len, uint32_t crcval)
{
	uint32_t q = len / sizeof(uint32_t);
	uint32_t *p = (uint32_t *)buf;

	while (q--) {
		asm volatile (
			".byte 0xf2, 0xf, 0x38, 0xf1, 0xf1;"
			:"=S" (crcval)
			:"0" (crcval), "c" (*p)
		);
		p++;
	}

	return (crcval);
}

static int
vncserver_send_screen(struct vncserver_softc *sc, int all)
{
	struct bhyvegc_image *gc_image;
	int x, y;
	int celly, cellwidth;
	int xcells, ycells;
	int w, h;
	uint32_t *p;
	int rem_x, rem_y;   /* remainder for resolutions not x32 pixels ratio */
	int retval;
	uint32_t *crc_p, *orig_crc;
	int changes;

	console_refresh();

	pthread_mutex_lock(&sc->vs_mtx);
	if (sc->vs_sending) {
		pthread_mutex_unlock(&sc->vs_mtx);
		return (1);
	}
	sc->vs_sending = 1;
	pthread_mutex_unlock(&sc->vs_mtx);

	retval = 0;

	if (all) {
		rfbMarkRectAsModified(sc->vs_screen, 0, 0, sc->vs_width,
		    sc->vs_height);
		goto done;
	}

	/*
	 * Calculate the checksum for each 32x32 cell. Send each that
	 * has changed since the last scan.
	 */

	/* Resolution changed */

	gc_image = sc->vs_gc;
	sc->vs_crc_width = gc_image->width;
	sc->vs_crc_height = gc_image->height;

	w = sc->vs_crc_width;
	h = sc->vs_crc_height;
	xcells = howmany(sc->vs_crc_width, PIX_PER_CELL);
	ycells = howmany(sc->vs_crc_height, PIX_PER_CELL);

	rem_x = w & PIXCELL_MASK;

	rem_y = h & PIXCELL_MASK;
	if (!rem_y)
		rem_y = PIX_PER_CELL;

	p = gc_image->data;

	/*
	 * Go through all cells and calculate crc. If significant number
	 * of changes, then send entire screen.
	 * crc_tmp is dual purpose: to store the new crc and to flag as
	 * a cell that has changed.
	 */
	crc_p = sc->vs_crc_tmp - xcells;
	orig_crc = sc->vs_crc - xcells;
	changes = 0;
	memset(sc->vs_crc_tmp, 0, sizeof(uint32_t) * xcells * ycells);
	for (y = 0; y < h; y++) {
		if ((y & PIXCELL_MASK) == 0) {
			crc_p += xcells;
			orig_crc += xcells;
		}

		for (x = 0; x < xcells; x++) {
			if (sc->vs_hw_crc)
				crc_p[x] = fast_crc32(p,
				             PIX_PER_CELL * sizeof(uint32_t),
				             crc_p[x]);
			else
				crc_p[x] = (uint32_t)crc32(crc_p[x],
				             (Bytef *)p,
				             PIX_PER_CELL * sizeof(uint32_t));

			p += PIX_PER_CELL;

			/* check for crc delta if last row in cell */
			if ((y & PIXCELL_MASK) == PIXCELL_MASK || y == (h-1)) {
				if (orig_crc[x] != crc_p[x]) {
					orig_crc[x] = crc_p[x];
					crc_p[x] = 1;
					changes++;
				} else {
					crc_p[x] = 0;
				}
			}
		}

		if (rem_x) {
			if (sc->vs_hw_crc)
				crc_p[x] = fast_crc32(p,
				                    rem_x * sizeof(uint32_t),
				                    crc_p[x]);
			else
				crc_p[x] = (uint32_t)crc32(crc_p[x],
				                    (Bytef *)p,
				                    rem_x * sizeof(uint32_t));
			p += rem_x;

			if ((y & PIXCELL_MASK) == PIXCELL_MASK || y == (h-1)) {
				if (orig_crc[x] != crc_p[x]) {
					orig_crc[x] = crc_p[x];
					crc_p[x] = 1;
					changes++;
				} else {
					crc_p[x] = 0;
				}
			}
		}
	}

	/* If number of changes is > THRESH percent, send the whole screen */
	if (((changes * 100) / (xcells * ycells)) >= RFB_SEND_ALL_THRESH) {
		rfbMarkRectAsModified(sc->vs_screen, 0, 0, sc->vs_width,
		    sc->vs_height);
		goto done;
	}
	
	/* Go through all cells, and send only changed ones */
	crc_p = sc->vs_crc_tmp;
	for (y = 0; y < h; y += PIX_PER_CELL) {
		/* previous cell's row */
		celly = (y >> PIXCELL_SHIFT);

		/* Delta check crc to previous set */
		for (x = 0; x < xcells; x++) {
			if (*crc_p++ == 0)
				continue;

			if (x == (xcells - 1) && rem_x > 0)
				cellwidth = rem_x;
			else
				cellwidth = PIX_PER_CELL;

			rfbMarkRectAsModified(sc->vs_screen, x * PIX_PER_CELL,
				celly * PIX_PER_CELL,
				x * PIX_PER_CELL + cellwidth,
				(celly * PIX_PER_CELL) + (y + PIX_PER_CELL >= h ? rem_y : PIX_PER_CELL));
		}
	}
	retval = 1;

done:
	pthread_mutex_lock(&sc->vs_mtx);
	sc->vs_sending = 0;
	pthread_mutex_unlock(&sc->vs_mtx);
	
	return (retval);
}

static void
vncserver_handle_key(rfbBool down, rfbKeySym key, rfbClientPtr cl)
{

	console_key_event(down, key);
}

static void
vncserver_handle_ptr(int buttonMask, int x, int y, rfbClientPtr cl)
{

	console_ptr_event(buttonMask, x, y);
}

static enum rfbNewClientAction
vncserver_handle_client(rfbClientPtr cl)
{
	struct vncserver_softc *sc;

	sc = cl->screen->screenData;
	pthread_cond_signal(&sc->vs_cond);

	return (RFB_CLIENT_ACCEPT);
}

static int64_t
timeval_delta(struct timeval *prev, struct timeval *now)
{
	int64_t n1, n2;
	n1 = now->tv_sec * 1000000 + now->tv_usec;
	n2 = prev->tv_sec * 1000000 + prev->tv_usec;
	return (n1 - n2);
}

static void *
vncserver_thr(void *arg)
{
	struct vncserver_softc *sc;
	struct kevent kev, ret;
	int kq, err;

	sc = arg;
	kq = kqueue();

	EV_SET(&kev, 1, EVFILT_TIMER, EV_ADD | EV_ENABLE, 0, 40, NULL);

	err = kevent(kq, &kev, 1, NULL, 0, NULL);
	if (err < 0)
		goto out;

	for (;;) {
		err  = kevent(kq, NULL, 0, &ret, 1, NULL);
		if (err < 0)
			goto out;

		if (ret.filter == EVFILT_TIMER) {
			if (vncserver_send_screen(sc, 0) < 0)
				goto out;
		}
	}

out:
	close(kq);
	return (NULL);
}

static int
sse42_supported()
{
	unsigned int eax, ebx, ecx, edx;
 
	__get_cpuid(1, &eax, &ebx, &ecx, &edx);
 
	return ((ecx & bit_SSE42) != 0);
}

int
vncserver_init(struct sockaddr *sa, size_t sa_len, int wait)
{
	struct vncserver_softc *sc;

	sc = calloc(1, sizeof(struct vncserver_softc));

	sc->vs_crc = calloc(howmany(RFB_MAX_WIDTH * RFB_MAX_HEIGHT, 32),
	                 sizeof(uint32_t));
	sc->vs_crc_tmp = calloc(howmany(RFB_MAX_WIDTH * RFB_MAX_HEIGHT, 32),
	                     sizeof(uint32_t));
	sc->vs_crc_width = RFB_MAX_WIDTH;
	sc->vs_crc_height = RFB_MAX_HEIGHT;

	sc->vs_hw_crc = sse42_supported();

	sc->vs_conn_wait = wait;
	if (wait) {
		pthread_mutex_init(&sc->vs_mtx, NULL);
		pthread_cond_init(&sc->vs_cond, NULL);
	}

	sc->vs_gc = console_get_image();
	sc->vs_height = sc->vs_gc->height;
	sc->vs_width = sc->vs_gc->width;
	sc->vs_screen = rfbGetScreen(NULL, NULL, sc->vs_width, sc->vs_height, 8, 3, 4);
	sc->vs_screen->desktopName = "bhyve";
	sc->vs_screen->alwaysShared = true;
	sc->vs_screen->serverFormat.redShift = 16;
	sc->vs_screen->serverFormat.greenShift = 8;
	sc->vs_screen->serverFormat.blueShift = 0;
	sc->vs_screen->frameBuffer = (char *)sc->vs_gc->data;
	sc->vs_screen->ptrAddEvent = vncserver_handle_ptr;
	sc->vs_screen->kbdAddEvent = vncserver_handle_key;
	sc->vs_screen->newClientHook = vncserver_handle_client;
	sc->vs_screen->screenData = sc;

	if (sa->sa_family == AF_INET) {
		struct sockaddr_in *sin = (struct sockaddr_in *)sa;
		sc->vs_screen->port = ntohs(sin->sin_port);
	}

	if (sa->sa_family == AF_UNIX) {
		struct sockaddr_un *sun = (struct sockaddr_un *)sa;
		sc->vs_screen->unixSockPath = sun->sun_path;
	}

	rfbInitServer(sc->vs_screen);
	rfbRunEventLoop(sc->vs_screen, 40000, true);

	pthread_create(&sc->vs_tid, NULL, vncserver_thr, sc);
	pthread_set_name_np(sc->vs_tid, "vncserver");

	if (wait) {
		DPRINTF(("Waiting for vnc client...\n"));
		pthread_mutex_lock(&sc->vs_mtx);
		pthread_cond_wait(&sc->vs_cond, &sc->vs_mtx);
		pthread_mutex_unlock(&sc->vs_mtx);
	}

	return (0);
}
