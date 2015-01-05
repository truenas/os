.include <src.opts.mk>
PROG= launchd

.PATH: .
.PATH: ${.CURDIR}
.PATH: ${.CURDIR}/support
.PATH: ${.CURDIR}/uuid
.PATH: ${.CURDIR}/init


MK_MAN= no

BASE_INCLUDE=  -I. -I${.CURDIR} -I${.CURDIR}/init -I${.CURDIR}/../../apsl/include
BASE_INCLUDE+= -I${.CURDIR}/../../include -I${.CURDIR}/../../sys
DEFINES= -DTARGET_CPU_X86=1 -D__APPLE__ -fblocks -DLIBC_NO_LIBCRASHREPORTERCLIENT -DPRIVATE
MIG_FLAGS= ${BASE_INCLUDE} 	${DEFINES}
CFLAGS+= ${MIG_FLAGS}  -D__MigTypeCheck
CFLAGS+= -I${.CURDIR}/../../contrib/openbsm -I${.CURDIR}/../../lib/liblaunch
CFLAGS+= -DHAVE_INTTYPES_H

LDFLAGS+= -static 
LDFLAGS+= -lBlocksRuntime -lbsm -lauditd
LDFLAGS+= -pthread

helper.h launchd_helperServer.h: ${.CURDIR}/helper.defs
	mig ${MIG_FLAGS} -user /dev/null -server /dev/null -header ${.CURDIR}/helper.h \
	-sheader ${.CURDIR}/launchd_helperServer.h ${.CURDIR}/helper.defs

jobServer.h jobServer.c job.h: ${.CURDIR}/job.defs ${.CURDIR}/helper.h
	mig ${MIG_FLAGS} -sheader ${.CURDIR}/jobServer.h ${.CURDIR}/job.defs
job_reply.h job_replyUser.c job_replyServer.c job_replyServer.h: ${.CURDIR}/job_reply.defs
	mig ${MIG_FLAGS} -header ${.CURDIR}/job_reply.h -sheader ${.CURDIR}/job_replyServer.h ${.CURDIR}/job_reply.defs
${.CURDIR}/job_forward.h job_forwardUser.c job_forwardServer.h: ${.CURDIR}/job_forward.defs
	mig ${MIG_FLAGS} -header ${.CURDIR}/job_forward.h -sheader ${.CURDIR}/job_forwardServer.h ${.CURDIR}/job_forward.defs
notify.h notifyServer.c notifyServer.h: ${.CURDIR}/notify.defs
	mig ${MIG_FLAGS} -header ${.CURDIR}/notify.h -sheader ${.CURDIR}/notifyServer.h ${.CURDIR}/notify.defs
mach_excServer.h mach_excServer.c: ${.CURDIR}/mach_exc.defs
	mig ${MIG_FLAGS} -sheader ${.CURDIR}/mach_excServer.h ${.CURDIR}/mach_exc.defs
internal.h internalServer.h internalServer.c internalUser.c: ${.CURDIR}/internal.defs
	mig ${MIG_FLAGS} -header ${.CURDIR}/internal.h -sheader ${.CURDIR}/internalServer.h ${.CURDIR}/internal.defs



SRCS= jobServer.c mach_excServer.c internalServer.c
SRCS+= job_forwardUser.c  job_replyUser.c job_replyServer.c notifyServer.c
SRCS+= internalUser.c
SRCS+= launchd.c core.c kill2.c ktrace.c ipc.c log.c runtime.c
SRCS+= init.c
LDADD+= -lutil -lauditd -lbsm -ldispatch -lmach -lBlocksRuntime -llaunch -losxsupport -lxpc -lnv 



CLEANFILES+= *Server.c *User.c job.h helper.h job_reply.h job_forward.h notify.h \
	internal.h mach_exc.h *Server.h *~ cscope.*
.include <bsd.prog.mk>
