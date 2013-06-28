# $FreeBSD$

.PATH:	${.CURDIR}/../../fs/pefs

KMOD=	pefs
SRCS=	vnode_if.h \
	pefs_subr.c pefs_vfsops.c pefs_vnops.c pefs_xbase64.c pefs_crypto.c \
	pefs_dircache.c \
	pefs_xts.c vmac.c

.if ${MACHINE_CPUARCH} == "i386" || ${MACHINE_CPUARCH} == "amd64"
SRCS+=	pefs_aesni.c
CFLAGS+= -DPEFS_AESNI
.endif

CFLAGS+= -I${.CURDIR}/../../

# Temporally build crypto/hmac into pefs module
.PATH:	${.CURDIR}/../../crypto/hmac

SRCS+=	hmac.c

.include <bsd.kmod.mk>
