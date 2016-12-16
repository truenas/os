# $FreeBSD$

.PATH: ${.CURDIR}/../../contrib/libvncserver/libvncserver
.PATH: ${.CURDIR}/../../contrib/libvncserver/common
.PATH: ${.CURDIR}/../../contrib/libvncserver/rfb

CFLAGS+=-I${.CURDIR}/../../contrib/libvncserver
CFLAGS+=-I${.CURDIR}/../../contrib/libvncserver/common
CFLAGS+=-I${.CURDIR}/../../contrib/libvncserver/libvncserver
CFLAGS+=-I${.CURDIR}

LIB=		vncserver
PACKAGE=	lib${LIB}
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
		md5.c \
		sha1.c \
		rfbssl_none.c \
		rfbcrypto_included.c \
		zrleoutstream.c \
		zrlepalettehelper.c \
		zywrletemplate.c

INCSDIR=	${INCLUDEDIR}/rfb
INCS=		rfb.h rfbproto.h rfb/rfbconfig.h

.include <bsd.lib.mk>
