.include <bsd.init.mk>

LIB=	mach

.PATH: mach
CFLAGS+= -I${.CURDIR}/../../include -I. 

.if ${MACHINE_CPUARCH} == "amd64"
.PATH: amd64
.PATH: amd64/sys
CFLAGS+=	-I./amd64

.else
# unsupported
.endif

.for _dir in ${.CURDIR}/../.. ${.CURDIR}/../../sys ${.CURDIR}/../../.. /sys /usr/src/sys
.if !defined(SYSDIR) && exists(${_dir}/kern/)
SYSDIR=	${_dir}
.endif
.endfor
.if !defined(SYSDIR) || !exists(${SYSDIR}/kern/)
.error "can't find kernel source tree"
.endif

SRCS=			\
	mach_msg.c	\
	mach_init.c \
	mach_misc.c

MACH_OBJS = 	syscalls.o	

SRCS+= ${MACH_OBJS:C/.o$/.S/}


CFLAGS+= -I${SYSDIR}
CFLAGS+= -fPIC

.include <bsd.lib.mk>
