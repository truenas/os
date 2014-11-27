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

CFLAGS+= -I${.CURDIR}/../../include -I. -I./${MACHINE_CPUARCH} -I${.CURDIR}
CFLAGS+= -I${SYSDIR} -I${.CURDIR}/../../apsl/include
CFLAGS+= -fPIC -DPRIVATE -D__MigTypeCheck -DLIBSYSCALL_INTERFACE

mach_port.h mach_portUser.c: machine mach_port.defs
	mig ${CFLAGS} -server /dev/null mach_port.defs
task.h taskUser.c: machine task.defs
	mig ${CFLAGS} -server /dev/null task.defs
host_priv.h host_privUser.c: machine host_priv.defs
	mig ${CFLAGS} -server /dev/null host_priv.defs
mach_host.h mach_hostUser.c: machine mach_host.defs
	mig ${CFLAGS} -server /dev/null mach_host.defs
vm_map.h vm_mapUser.c: machine vm_map.defs
	mig ${CFLAGS} -server /dev/null vm_map.defs

SRCS=					\
	mach_portUser.c		\
	taskUser.c			\
	host_privUser.c		\
	mach_hostUser.c		\
	vm_mapUser.c		\
	error_codes.c		\
	mach_error_string.c \
	mach_init.c 		\
	mach_msg.c			\
	mach_misc.c			\
	mig_allocate.c 		\
	mig_deallocate.c 	\
	mig_reply_setup.c	\
	mig_strncpy.c 		\
	mig_support.c


MACH_OBJS = 	syscalls.o	

SRCS+= ${MACH_OBJS:C/.o$/.S/}





CLEANFILES+= machine *~ mach_port.h task.h host_priv.h mach_host.h
CLEANFILES+= cscope.* vm_map.h *User.c

.include <bsd.lib.mk>
