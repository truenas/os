# nvdimm Loadable Kernel Module
#
# $FreeBSD$

.PATH:  ${.CURDIR}/../../dev/nvdimm

KMOD=	nvdimm
SRCS=	nvdimm.c nvdimm_copy.S
SRCS+=	device_if.h bus_if.h ntb_if.h opt_ddb.h

# XXX: for assym.s
SRCS+=  opt_kstack_pages.h opt_nfs.h opt_hwpmc_hooks.h opt_compat.h
.if ${MACHINE_CPUARCH} == "i386"
SRCS+=	opt_apic.h
.endif

SRCS+=	assym.s

# XXX: for assym.s
SRCS+=	opt_kstack_pages.h opt_nfs.h opt_apic.h opt_hwpmc_hooks.h opt_compat.h
SRCS+=	assym.s

CFLAGS += -I${.CURDIR}/../../dev/ntb

MFILES=	kern/bus_if.m kern/device_if.m dev/ntb/ntb_if.m

.include <bsd.kmod.mk>
