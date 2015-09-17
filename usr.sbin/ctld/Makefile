# $FreeBSD$

CFLAGS+=-I${.CURDIR}/../../contrib/libucl/include
.PATH:  ${.CURDIR}/../../contrib/libucl/include

PROG=		ctld
SRCS=		chap.c ctld.c discovery.c isns.c kernel.c keys.c log.c
SRCS+=		login.c parse.y pdu.c token.l y.tab.h parser.c
CFLAGS+=	-I${.CURDIR} -g -O0
CFLAGS+=	-I${.CURDIR}/../../sys
CFLAGS+=	-I${.CURDIR}/../../sys/cam/ctl
CFLAGS+=	-I${.CURDIR}/../../sys/dev/iscsi
#CFLAGS+=	-DICL_KERNEL_PROXY
MAN=		ctld.8 ctl.conf.5

DPADD=		${LIBBSDXML} ${LIBL} ${LIBMD} ${LIBSBUF} ${LIBUTIL} ${LIBUCL} ${LIBM}
LDADD=		-lbsdxml -ll -lmd -lsbuf -lutil -lucl -lm
USEPRIVATELIB=	ucl

YFLAGS+=	-v
CLEANFILES=	y.tab.c y.tab.h y.output

WARNS=		6
NO_WMISSING_VARIABLE_DECLARATIONS=

.include <bsd.prog.mk>
