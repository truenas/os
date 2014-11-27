.include <bsd.init.mk>

LIB=	mach

.PATH: mach
.PATH: ${MACHINE_CPUARCH}
.PATH: ${MACHINE_CPUARCH}/sys


machine:
	ln -s ${.CURDIR}/../../sys/${MACHINE_CPUARCH}/include machine

.for _dir in ${.CURDIR}/../.. ${.CURDIR}/../../sys ${.CURDIR}/../../.. /sys /usr/src/sys
.if !defined(SYSDIR) && exists(${_dir}/kern/)
SYSDIR=	${_dir}
.endif
.endfor
.if !defined(SYSDIR) || !exists(${SYSDIR}/kern/)
.error "can't find kernel source tree"
.endif

SRCS=			\
	mach_error_string.c \
	mach_init.c \
	mach_msg.c	\
	mach_misc.c	\
	mig_allocate.c \
	mig_deallocate.c \
	mig_reply_setup.c	\
	mig_strncpy.c \
	mig_support.c


MACH_OBJS = 	syscalls.o	

SRCS+= ${MACH_OBJS:C/.o$/.S/}

CFLAGS+= -I${.CURDIR}/../../include -I. -I./${MACHINE_CPUARCH} -I${.CURDIR}
CFLAGS+= -I${SYSDIR} -I${.CURDIR}/../../apsl/include
CFLAGS+= -fPIC -DPRIVATE

.include <bsd.lib.mk>
