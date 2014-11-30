.include <src.opts.mk>
PROG= launchd

.PATH: .
.PATH: support
.PATH: uuid

BASE_INCLUDE= -I${.CURDIR}/../../apsl/include
BASE_INCLUDE+=  -I${.CURDIR}/../../include -I${.CURDIR}/../../sys -I${.CURDIR}
DEFINES= -DTARGET_CPU_X86=1 -D__APPLE__ -fblocks -DLIBC_NO_LIBCRASHREPORTERCLIENT -DPRIVATE
MIG_FLAGS= ${BASE_INCLUDE} 	${DEFINES}
CFLAGS+= ${MIG_FLAGS}  -D__MigTypeCheck
CFLAGS+= -I${.CURDIR}/../../contrib/openbsm -I${.CURDIR}/../../lib/liblaunch
CFLAGS+= -DHAVE_INTTYPES_H

LDFLAGS+= -L${.CURDIR}/../../lib/liblaunch -llaunch
LDFLAGS+= -L${.CURDIR}/../../lib/libdispatch -ldispatch
LDFLAGS+= -L${.CURDIR}/../../lib/libosxsupport -losxsupport
LDFLAGS+= -L/usr/local/lib -lobjc -lbsm -lauditd -L../../lib/libmach -lmach -lutil
LDFLAGS+= -pthread

jobServer.h jobServer.c job.h: job.defs helper.h
	mig ${MIG_FLAGS} -sheader jobServer.h job.defs
helper.h launchd_helperServer.h: helper.defs
	mig ${MIG_FLAGS} -user /dev/null -server /dev/null -header helper.h \
	-sheader launchd_helperServer.h helper.defs
job_reply.h job_replyUser.c job_replyServer.c job_replyServer.h: job_reply.defs
	mig ${MIG_FLAGS} -sheader job_replyServer.h job_reply.defs
job_forward.h job_forwardUser.c job_forwardServer.h: job_forward.defs
	mig ${MIG_FLAGS} -sheader job_forwardServer.h job_forward.defs
notify.h notifyServer.c notifyServer.h: notify.defs
	mig ${MIG_FLAGS} -sheader notifyServer.h notify.defs
mach_excServer.h mach_excServer.c: mach_exc.defs
	mig ${MIG_FLAGS} -sheader mach_excServer.h mach_exc.defs
internal.h internalServer.h internalServer.c internalUser.c:
	mig ${MIG_FLAGS} -sheader internalServer.h internal.defs



SRCS= jobServer.c mach_excServer.c internalServer.c
SRCS+= job_forwardUser.c  job_replyUser.c job_replyServer.c notifyServer.c
SRCS+= internalUser.c
SRCS+= launchd.c core.c kill2.c ktrace.c ipc.c log.c runtime.c




CLEANFILES+= *Server.c *User.c job.h helper.h job_reply.h job_forward.h notify.h \
	internal.h mach_exc.h *Server.h *~ cscope.*
.include <bsd.prog.mk>
