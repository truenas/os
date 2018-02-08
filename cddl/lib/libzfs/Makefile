# $FreeBSD$

.PATH: ${SRCTOP}/cddl/compat/opensolaris/misc
.PATH: ${SRCTOP}/sys/cddl/contrib/opensolaris/common/zfs
.PATH: ${SRCTOP}/sys/cddl/contrib/opensolaris/uts/common/fs/zfs
.PATH: ${SRCTOP}/cddl/contrib/opensolaris/lib/libzfs/common
.PATH: ${SRCTOP}/cddl/contrib/opensolaris/lib/libcmdutils/common
.PATH: ${SRCTOP}/
LIB=	zfs
LIBADD=	md pthread umem util uutil m avl bsdxml geom nvpair z zfs_core crypto
SRCS=	deviceid.c \
	fsshare.c \
	mkdirp.c \
	mnttab.c \
	thread_pool.c \
	zmount.c \
	zone.c

SRCS+=	nicenum.c

SRCS+=	libzfs_changelist.c \
	libzfs_compat.c \
	libzfs_config.c \
	libzfs_crypto.c \
	libzfs_dataset.c \
	libzfs_diff.c \
	libzfs_import.c \
	libzfs_iter.c \
	libzfs_mount.c \
	libzfs_pool.c \
	libzfs_sendrecv.c \
	libzfs_status.c \
	libzfs_util.c \
	zfeature_common.c \
	zfs_comutil.c \
	zfs_deleg.c \
	zfs_fletcher.c \
	zfs_namecheck.c \
	zfs_prop.c \
	zpool_prop.c \
	zprop_common.c \

WARNS?=	0
CSTD=	c99
CFLAGS+= -DZFS_NO_ACL
CFLAGS+= -I${SRCTOP}/sbin/mount
CFLAGS+= -I${SRCTOP}/sys/cddl/compat/opensolaris
CFLAGS+= -I${SRCTOP}/cddl/compat/opensolaris/include
CFLAGS+= -I${SRCTOP}/cddl/compat/opensolaris/lib/libumem
CFLAGS+= -I${SRCTOP}/cddl/contrib/opensolaris/lib/libzpool/common
CFLAGS+= -I${SRCTOP}/sys/cddl/contrib/opensolaris/common/zfs
CFLAGS+= -I${SRCTOP}/sys/cddl/contrib/opensolaris/uts/common/fs/zfs
CFLAGS+= -I${SRCTOP}/sys/cddl/contrib/opensolaris/uts/common/sys
CFLAGS+= -I${SRCTOP}/cddl/contrib/opensolaris/head
CFLAGS+= -I${SRCTOP}/sys/cddl/contrib/opensolaris/uts/common
CFLAGS+= -I${SRCTOP}/cddl/contrib/opensolaris/lib/libnvpair
CFLAGS+= -I${SRCTOP}/cddl/contrib/opensolaris/lib/libuutil/common
CFLAGS+= -I${SRCTOP}/cddl/contrib/opensolaris/lib/libzfs/common
CFLAGS+= -I${SRCTOP}/cddl/contrib/opensolaris/lib/libzfs_core/common
CFLAGS+= -I${SRCTOP}/cddl/contrib/opensolaris/lib/libcmdutils

# OpenZFS crypto code
ICP=	${SRCTOP}/sys/cddl/contrib/opensolaris/icp
CFLAGS+=	-I${ICP}/include

.PATH:	${ICP}/api
SRCS+=	kcf_cipher.c \
	kcf_digest.c \
	kcf_mac.c \
	kcf_miscapi.c \
	kcf_ctxops.c
#.PATH:	${ICP}/core
#SRCS+=	kcf_callprov.c \
#	kcf_prov_tabs.c \
#	kcf_sched.c \
#	kcf_mech_tabs.c \
#	kcf_prov_lib.c
.PATH:	${ICP}/spi
SRCS+=	kcf_spi.c
.PATH:	${ICP}/io
SRCS+=	aes.c \
	edonr_mod.c \
	sha1_mod.c \
	sha2_mod.c \
	skein_mod.c
.PATH:	${ICP}/os
SRCS+=	modhash.c
.PATH:	${ICP}/algs/modes
SRCS+=	cbc.c \
	ccm.c \
	ctr.c \
	gcm.c \
	modes.c
.PATH:	${ICP}/algs
SRCS+=	aes/aes_impl.c \
	aes/aes_modes.c \
	edonr/edonr.c \
	sha1/sha1.c \
	sha2/sha2.c \
	skein/skein.c \
	skein/skein_block.c \
	skein/skein_iv.c

.include <bsd.lib.mk>
