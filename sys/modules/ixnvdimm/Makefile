# ixnvdimm Loadable Kernel Module
#
# $FreeBSD$

.PATH:  ${.CURDIR}/../../dev/ixnvdimm

KMOD=	ixnvdimm
SRCS=	ixnvdimm.c ixnvdimm_copy.S
SRCS+=	opt_acpi.h acpi_if.h device_if.h bus_if.h ntb_if.h opt_ddb.h

# XXX: for assym.inc
SRCS+=  opt_kstack_pages.h opt_nfs.h opt_hwpmc_hooks.h opt_compat.h

DPSRCS+=	assym.inc

CFLAGS += -I${.CURDIR}/../../dev/ntb

MFILES=	kern/bus_if.m kern/device_if.m dev/ntb/ntb_if.m

.include <bsd.kmod.mk>
