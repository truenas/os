.include <src.opts.mk>
PROG= launchd

.PATH:
.PATH: support
BASE_INCLUDE= -I${.CURDIR}/../../include -I${.CURDIR}/../../sys -I${.CURDIR} 
DEFINES= -DTARGET_CPU_X86=1 -D__APPLE__ -fblocks -DLIBC_NO_LIBCRASHREPORTERCLIENT -DPRIVATE
MIG_FLAGS= ${BASE_INCLUDE} 	${DEFINES}
CFLAGS+= ${MIG_FLAGS}  -D__MigTypeCheck
CFLAGS+= -I${.CURDIR}/../../apsl/include -I${.CURDIR}/../../contrib/openbsm
CFLAGS+= -I${.CURDIR}/../../lib/liblaunch
LDFLAGS+= -L${.CURDIR}/../../lib/liblaunch -llaunch
LDFLAGS+= -L${.CURDIR}/../../lib/libdispatch -ldispatch
LDFLAGS+= -L/usr/local/lib -lobjc -lbsm -lauditd -L../../lib/libmach -lmach
LDFLAGS+= -pthread

jobServer.h jobServer.c job.h: job.defs
	mig ${MIG_FLAGS} -sheader jobServer.h job.defs
helper.h launchd_helperServer.c: helper.defs
	mig ${MIG_FLAGS} -header helper.h -sheader launchd_helperServer.h helper.defs
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



SRCS= jobServer.c launchd_helperServer.c mach_excServer.c internalServer.c
SRCS+= job_forwardUser.c  job_replyUser.c job_replyServer.c notifyServer.c
SRCS+= internalUser.c
SRCS+= launchd.c core.c kill2.c ktrace.c ipc.c log.c runtime.c
SRCS+= assumes.c


CLEANFILES+= *Server.c *User.c job.h helper.h job_reply.h job_forward.h notify.h \
	internal.h mach_exc.h *Server.h *~ cscope.*
.include <bsd.prog.mk>
