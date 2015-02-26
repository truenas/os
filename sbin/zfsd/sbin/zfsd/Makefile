# $FreeBSD$

PROG_CXX=	zfsd
SRCS=		callout.cc		\
		case_file.cc		\
		dev_ctl_event.cc	\
		vdev.cc			\
		vdev_iterator.cc	\
		zfsd.cc			\
		zfsd_exception.cc	\
		zpool_list.cc

NO_MAN=		YES

WARNS?=		0

FREEBSD_SRCTOP?=/usr/src

INCFLAGS+= -I${FREEBSD_SRCTOP}/cddl/contrib/opensolaris/lib/libzpool/common
INCFLAGS+= -I${FREEBSD_SRCTOP}/cddl/compat/opensolaris/include
INCFLAGS+= -I${FREEBSD_SRCTOP}/cddl/compat/opensolaris/lib/libumem
INCFLAGS+= -I${FREEBSD_SRCTOP}/sys/cddl/compat/opensolaris
INCFLAGS+= -I${FREEBSD_SRCTOP}/cddl/contrib/opensolaris/lib/libzfs_core/common
INCFLAGS+= -I${FREEBSD_SRCTOP}/cddl/contrib/opensolaris/head
INCFLAGS+= -I${FREEBSD_SRCTOP}/cddl/contrib/opensolaris/lib/libuutil/common
INCFLAGS+= -I${FREEBSD_SRCTOP}/cddl/contrib/opensolaris/lib/libumem/common
INCFLAGS+= -I${FREEBSD_SRCTOP}/cddl/contrib/opensolaris/lib/libzfs/common
INCFLAGS+= -I${FREEBSD_SRCTOP}/cddl/contrib/opensolaris/lib/libnvpair
INCFLAGS+= -I${FREEBSD_SRCTOP}/sys/cddl/contrib/opensolaris/common/zfs
INCFLAGS+= -I${FREEBSD_SRCTOP}/sys/cddl/contrib/opensolaris/uts/common
INCFLAGS+= -I${FREEBSD_SRCTOP}/sys/cddl/contrib/opensolaris/uts/common/fs/zfs
INCFLAGS+= -I${FREEBSD_SRCTOP}/sys/cddl/contrib/opensolaris/uts/common/sys

CFLAGS+= -g -DNEED_SOLARIS_BOOLEAN ${INCFLAGS}
CXXFLAGS+= -g -DNEED_SOLARIS_BOOLEAN ${INCFLAGS}

BINDIR=${LOCALBASE}/sbin

DPADD=  ${LIBZFS} ${LIBUTIL} ${LIBGEOM} ${LIBBSDXML} ${LIBSBUF} \
	${LIBNVPAIR} ${LIBUUTIL}
LDADD=  -lzfs -lutil -lgeom -lbsdxml -lsbuf -lnvpair -luutil

cscope:
	find ${.CURDIR} -type f -a \( -name "*.[ch]" -o -name "*.cc" \) \
	     > ${.CURDIR}/cscope.files
	cd ${.CURDIR} && cscope -buq ${INCFLAGS}

.include <bsd.prog.mk>
