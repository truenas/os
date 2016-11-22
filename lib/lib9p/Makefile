# $FreeBSD$

.PATH: ${.CURDIR}/../../contrib/lib9p
CFLAGS+=-I${.CURDIR}/../../contrib/lib9p
CFLAGS+=-DL9P_DEBUG=L9P_DEBUG

LIB=		9p
SHLIB_MAJOR=	1
SRCS=		pack.c \
		connection.c \
		request.c \
		log.c \
		hashtable.c \
		rfuncs.c \
		utils.c \
		transport/socket.c \
		backend/fs.c

INCSDIR=	${INCLUDEDIR}/lib9p
INCS=		lib9p.h fid.h backend/fs.h

DPADD=		${LIBSBUF}
LDADD=		-lsbuf

.include <bsd.lib.mk>
