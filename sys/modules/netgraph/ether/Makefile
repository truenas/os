# $FreeBSD$

KMOD=	ng_ether
SRCS= 	ng_ether.c

.include <bsd.kmod.mk>

.if !defined(KERNBUILDDIR)
.if ${MK_VIMAGE} != "no"
CFLAGS+=	-DVIMAGE
.endif
.endif

