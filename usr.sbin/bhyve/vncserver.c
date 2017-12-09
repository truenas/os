/*-
 * Copyright (c) 2017 Marcelo Araujo <araujo@FreeBSD.org>
 * Copyright (c) 2016 Jakub Klama <jakub@ixsystems.com>
 * Copyright (c) 2015 Tycho Nightingale <tycho.nightingale@pluribusnetworks.com>
 * Copyright (c) 2015 Leon Dang
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
#include <dlfcn.h>
#include <err.h>
#include <errno.h>
#include <pthread.h>
#include <pthread_np.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include <cpuid.h>
#include <zlib.h>

#include "bhyvegc.h"
#include "console.h"

static int vncserver_debug = 1;
#define	DPRINTF(params) if (vncserver_debug) printf params
#define	WPRINTF(params) printf params

struct vncserver_softc {
	struct bhyvegc_image	*vs_gc;
	pthread_t		vs_tid;
	int			vs_width;
	int			vs_height;
	int			vs_conn_wait;
	int			vs_sending;
	pthread_mutex_t		vs_mtx;
	pthread_cond_t		vs_cond;

	int			vs_hw_crc;
	uint32_t *		vs_crc;	/* WxH crc cells */
	uint32_t *		vs_crc_tmp; /* buffer to store single crc row */
	int			vs_crc_width;
	int			vs_crc_height;

	char			*desktopName;
	bool			alwaysShared;
        bool                    vncweb;
	int			redShift;
	int			greenShift;
	int			blueShift;
	char			*frameBuffer;
	int			bitsPerSample;
	int			samplesPerPixel;
	int			bytesPerPixel;
	int			bind_port;
	void (*kdb_handler)(int down, uint32_t keysym);
	void (*ptr_handler)(uint8_t button, int x, int y);
};

#define RFB_MAX_WIDTH			2000
#define RFB_MAX_HEIGHT			1200
#define	RFB_ZLIB_BUFSZ			RFB_MAX_WIDTH*RFB_MAX_HEIGHT*4

#define PIX_PER_CELL	32
#define	PIXCELL_SHIFT	5
#define PIXCELL_MASK	0x1F

/* percentage changes to screen before sending the entire screen */
#define RFB_SEND_ALL_THRESH		25

/* path of libhyve-remote */
#define LIB_HYVE_REMOTE "/usr/local/lib/libhyverem.so"

/* prototype functions from shared library libhyve-remote */
int (*vnc_init_server)(struct vncserver_softc *sc, char *hostname);
int (*vnc_event_loop)(int time, bool b);
int (*vnc_enable_http)(char *webdir, bool enable);
int (*vnc_enable_password)(char *vnc_password);
void (*vnc_mark_rect_modified)(struct vncserver_softc *sc, int x1, int y1,
	int x2, int y2);

/* Try to load libhyve-remote */
static int
load_shared_lib(void)
{
    char *loader = NULL;
    void *shlib;

    if (loader == NULL) {
	loader = strdup(LIB_HYVE_REMOTE);
	if (loader == NULL)
	    err(ENOMEM, "malloc error");
    }

    shlib = dlopen(loader, RTLD_GLOBAL);
    if (!shlib) {
	dlclose(shlib);
	free(loader);
	return (1);
    } else {
	vnc_init_server = dlsym(shlib, "vnc_init_server");
	vnc_event_loop = dlsym(shlib, "vnc_event_loop");
	vnc_enable_http = dlsym(shlib, "vnc_enable_http");
	vnc_mark_rect_modified = dlsym(shlib, "vnc_mark_rect_modified");
	vnc_enable_password = dlsym(shlib, "vnc_enable_password");
    }
    return (0);
}

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

	pthread_mutex_unlock(&sc->vs_mtx);

	pthread_mutex_lock(&sc->vs_mtx);
	if (sc->vs_sending) {
		pthread_mutex_unlock(&sc->vs_mtx);
		return (1);
	}
	sc->vs_sending = 1;
	pthread_mutex_unlock(&sc->vs_mtx);

	retval = 0;

	if (all) {
		vnc_mark_rect_modified(sc, 0, 0, sc->vs_width, sc->vs_height);
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
		vnc_mark_rect_modified(sc, 0, 0, sc->vs_width, sc->vs_height);
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

			vnc_mark_rect_modified(sc, x * PIX_PER_CELL,
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
handle_keyboard(int down, uint32_t keysym) {
    console_key_event(down, keysym);
}

static void
handle_ptr(uint8_t button, int x, int y) {
    console_ptr_event(button, x, y);
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
vncserver_init(char *hostname, int port, int wait, char *password, int webserver)
{
	struct vncserver_softc *sc;

	if(load_shared_lib() == 1)
	    errx(EX_SOFTWARE, "cannot load shared library libhyve-remote");
	
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

	sc->desktopName = "bhyve";
	if (password) {
	    vnc_enable_password(password);
	}
	sc->alwaysShared = true;
	sc->redShift = 16;
	sc->greenShift = 8;
	sc->blueShift = 0;
	sc->bind_port = port ? port : 5900;
	sc->frameBuffer = (char *)sc->vs_gc->data;

	sc->kdb_handler = handle_keyboard;
	sc->ptr_handler = handle_ptr;

        sc->vncweb = 1;

        /*
        if (webserver)
        {
            DPRINTF(("===> HTTP ENABLED\n"));
            vnc_enable_http("/z/github/libvncserver/webclients", true);
        }
        */

	pthread_create(&sc->vs_tid, NULL, vncserver_thr, sc);
	pthread_set_name_np(sc->vs_tid, "vncserver");

	vnc_init_server(sc, hostname);
	vnc_event_loop(-1, true);

	if (wait) {
		DPRINTF(("Waiting for vnc client...\n"));
		pthread_mutex_lock(&sc->vs_mtx);
		pthread_cond_wait(&sc->vs_cond, &sc->vs_mtx);
		pthread_mutex_unlock(&sc->vs_mtx);
	}

	free(sc);
	return (0);
}
