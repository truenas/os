/*-
 * Copyright (c) 2003 Poul-Henning Kamp
 * Copyright (c) 2015 iXsystems, Inc.
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
 * 3. The names of the authors may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
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
 * $FreeBSD$
 */

#include <sys/types.h>
#include <sys/sysctl.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "libgeom.h"

#include <syslog.h>

char *
geom_getxml(void)
{
	char *p;
	size_t len = 0;
	int mib[3];
	size_t sizep;

	sizep = sizeof(mib) / sizeof(*mib);
	if (sysctlnametomib("kern.geom.confxml", mib, &sizep) != 0)
		return (NULL);

	/*
	 * Get an initial estimate for the size of the buffer.
	 * If we're lucky, and things aren't in flux, that'll be
	 * the size we want.
	 */
	if (sysctl(mib, sizep, NULL, &len, NULL, 0) != 0)
		return (NULL);

	do {
		p = malloc(len);
		if (p == NULL)
			return (NULL);

		if (sysctl(mib, sizep, p, &len, NULL, 0) == 0) {
			/* truncate buffer to the ASCIIZ string */
			return (reallocf(p, len));
		} else if (errno == ENOMEM) {
			/*
			 * This is extremely annoying:  if the buffer wasn't large
			 * enough, sysctl returns -1, with errno set to ENOMEM, but
			 * doesn't tell us the size it thought it should be.
			 * So I hate doing this, but let's add another 64k to it.
			 * (We could double it, but we'd start using serious memory
			 * pretty quickly.  What we're trying to handle here is the
			 * case where the available partitions is causing the geom xml
			 * to change in size rapidly.  We could also call sysctl with
			 * NULL for the buffer again, instead.)
			 */
			len += 64 * 1024;
			free(p);
		} else {
			free(p);
			p = NULL;
			break;
		}
	} while (1);
	
	syslog(LOG_ALERT | LOG_CONSOLE, "%s: len = %zd: %m", __FUNCTION__, len);

	return (NULL);
}
