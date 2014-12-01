.include <bsd.init.mk>

LIB=	xpc



BASE_INCLUDE= -I${.CURDIR}/../../apsl/include
BASE_INCLUDE= -I${.CURDIR}/../libnv
BASE_INCLUDE+= -I${.CURDIR} -I${.CURDIR}/../../sbin/launchd -I.
BASE_INCLUDE+= -I${.CURDIR}/../../include -I${.CURDIR}/../../sys
CFLAGS+= ${BASE_INCLUDE} -fblocks

SRCS = xpc_dictionary.c xpc_misc.c xpc_type.c xpc_array.c


.include <bsd.lib.mk>
