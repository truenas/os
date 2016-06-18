# $FreeBSD$

.PATH: ${.CURDIR}/../../contrib/libvncserver/libvncserver
.PATH: ${.CURDIR}/../../contrib/libvncserver/common
.PATH: ${.CURDIR}/../../contrib/libvncserver/rfb
CFLAGS+=-I${.CURDIR}/../../contrib/libvncserver -I${.CURDIR}/../../contrib/libvncserver/common -I${.CURDIR}
CFLAGS+=-I${.CURDIR}/../../contrib/libvncserver/libvncserver

LIB=		vncserver
SHLIB_MAJOR=	1
WARNS=		1
SRCS=		main.c \
		rfbserver.c \
		rfbregion.c \
		auth.c \
		sockets.c \
		websockets.c \
		stats.c \
		corre.c \
		hextile.c \
		rre.c \
		translate.c \
		cutpaste.c \
		httpd.c \
		cursor.c \
		font.c \
		draw.c \
		selbox.c \
		d3des.c \
		vncauth.c \
		cargs.c \
		minilzo.c \
		ultra.c \
		scale.c \
		zlib.c \
		zrle.c \
		zrleoutstream.c \
		zrlepalettehelper.c \
		zywrletemplate.c

INCS=		rfb.h

DPADD=		${LIBSBUF}
LDADD=		-lsbuf

.include <bsd.lib.mk>
