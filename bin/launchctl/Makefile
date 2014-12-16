.include <src.opts.mk>

PROG= launchctl

BASE_INCLUDE= -I${.CURDIR}/../../apsl/include
BASE_INCLUDE+=  -I${.CURDIR}/../../include -I${.CURDIR}/../../sys -I${.CURDIR}
DEFINES=  -D__APPLE__ -fblocks
DEFINES+= -DLIBC_NO_LIBCRASHREPORTERCLIENT -DPRIVATE
MIG_FLAGS= ${BASE_INCLUDE} 	${DEFINES}

CFLAGS+= ${MIG_FLAGS}  -D__MigTypeCheck -D__APPLE_CC__=1 
CFLAGS+= -I${.CURDIR}/../../lib/liblaunch -I${.CURDIR}/../../contrib/openbsm
CFLAGS+= -I${.CURDIR}/../../include/mach
LDFLAGS+= -L${.CURDIR}/../../lib/liblaunch -llaunch
LDFLAGS+= -L${.CURDIR}/../../lib/libmach -lmach
LDFLAGS+= -L${.CURDIR}/../../lib/libdispatch -ldispatch
LDFLAGS+= -L${.CURDIR}/../../lib/libosxsupport -losxsupport
LDFLAGS+= -L${.CURDIR}/../../lib/libcfxlite -lcfxlite
LDFLAGS+= -L/usr/local/lib -lBlocksRuntime -lbsm -lauditd
LDFLAGS+= -lreadline -lutil -licui18n -pthread
SRCS= launchctl.c

.include <bsd.prog.mk>
