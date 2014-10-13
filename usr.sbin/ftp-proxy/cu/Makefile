# $OpenBSD: Makefile,v 1.3 2012/07/10 11:42:02 nicm Exp $

PROG=	cu
SRCS=	cu.c command.c error.c input.c xmodem.c

CFLAGS+= -I${.CURDIR}/../../../contrib/pf/libevent

LDADD+= ${LIBEVENT}
DPADD+= ${LIBEVENT}
BINDIR=	/usr/bin

WARNS?=	3

.include <bsd.prog.mk>
