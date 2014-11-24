.include <bsd.init.mk>

LIB=	launch

.PATH: .
.PATH: ../../sbin/launchd



BASE_INCLUDE= -I${.CURDIR}/../../include -I${.CURDIR}/../../sys -I${.CURDIR} 
DEFINES= -DTARGET_CPU_X86=1 -D__APPLE__ -fblocks -DLIBC_NO_LIBCRASHREPORTERCLIENT -DPRIVATE
MIG_FLAGS= ${BASE_INCLUDE} 	${DEFINES}
CFLAGS+= ${MIG_FLAGS}
CFLAGS+= -I${.CURDIR}/../../apsl/include -I${.CURDIR}/../../contrib/openbsm
CFLAGS+= -I${.CURDIR}/../../sbin/launchd

helper.h launchd_helperServer.c helperServer.h: helper.defs
	mig ${MIG_FLAGS} -header helper.h -sheader helperServer.h helper.defs
job.h jobServer.c jobServer.h: job.defs
	mig ${MIG_FLAGS} -sheader jobServer.h job.defs

SRCS=  jobServer.c launchd_helperServer.c
SRCS+= libbootstrap.c liblaunch.c libvproc.c


clean:
	rm -f *.o *.so* *.So *.a *.po *User.c *Server.c job.h helper.h *Server.h launchd_helper.h *~

.include <bsd.lib.mk>
