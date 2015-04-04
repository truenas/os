.include <src.opts.mk>

PROG= launchctl

BASE_INCLUDE= -I${.CURDIR}/../../apsl/include
BASE_INCLUDE+=  -I${.CURDIR}/../../include -I${.CURDIR}/../../sys -I${.CURDIR}

DEBUG_FLAGS=-g -O0

CFLAGS+= -I${.CURDIR}/../../lib/liblaunch
CFLAGS+= -I${.CURDIR}/../../include/mach
LDFLAGS+= -L${.CURDIR}/../../lib/liblaunch -llaunch
LDFLAGS+= -L${.CURDIR}/../../lib/libmach -lmach
LDFLAGS+= -L${.CURDIR}/../../lib/libdispatch -ldispatch
LDFLAGS+= -L${.CURDIR}/../../lib/libosxsupport -losxsupport
LDFLAGS+= -L/usr/local/lib -lBlocksRuntime
SRCS= launchctl.c

.include <bsd.prog.mk>
