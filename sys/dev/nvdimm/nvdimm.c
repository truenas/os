/*-
 * Copyright (c) 2016-2017 Alexander Motin <mav@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_acpi.h"

#include <sys/param.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bio.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/malloc.h>
#include <sys/linker.h>
#include <sys/rman.h>
#include <sys/sysctl.h>
#include <sys/uio.h>
#include <sys/vmmeter.h>

#include <vm/vm.h>      /* vtophys */
#include <vm/pmap.h>    /* vtophys */
#include <vm/vm_param.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_phys.h>

#include <geom/geom_disk.h>

#include <machine/bus.h>
#include <machine/metadata.h>
#include <machine/pc/bios.h>
#include <machine/cpu.h>

#include <contrib/dev/acpica/include/acpi.h>
#include <contrib/dev/acpica/include/accommon.h>

#include <dev/acpica/acpivar.h>

#include "../ntb/ntb.h"

#define	ELF_KERN_STR	("elf"__XSTRING(__ELF_WORD_SIZE)" kernel")

enum {
	STATE_INCORRECT = 0,
	STATE_NONE,
	STATE_IDLE,
	STATE_WAITING,
	STATE_READY,
};

/* PMEM label */
struct pmem_label {
	uint64_t	sign;	/* NTBN_SIGN_LONG signature */
	uint64_t	array;	/* Unique array ID */
	uint32_t	empty;	/* PMEM is empty and was never written */
	uint32_t	dirty;	/* PMEM was written without NTB connection */
	uint32_t	opened;	/* PMEM device is open now */
	uint32_t	state;	/* Synchronization state */
};

/* PMEM device */
struct pmem_disk {
	struct disk		*disk;
	int			 rid;
	struct resource		*res;
	vm_paddr_t		 paddr;		/* Local PMEM phys address */
	vm_paddr_t		 size;		/* Local PMEM size */
	uint8_t			*vaddr;		/* Local PMEM KVA address */
	struct pmem_label	*label;		/* Local PMEM label */
	uint8_t			*rvaddr;	/* Remote PMEM KVA address */
	struct pmem_label	*rlabel;	/* Remote PMEM label */
};

/* NTB PMEM device */
struct ntb_pmem {
	device_t		 nvd_dev;	/* PMEM device. */
	struct callout		 ntb_link_work;
	struct callout		 ntb_start;
	vm_paddr_t		 ntb_paddr;	/* MW physical address */
	vm_paddr_t		 ntb_size;	/* MW size */
	caddr_t			 ntb_caddr;	/* MW KVA address */
	vm_paddr_t		 ntb_xalign;	/* XLAT address allignment */
	vm_paddr_t		 ntb_xpaddr;	/* XLAT physical address */
	vm_paddr_t		 ntb_xsize;	/* XLAT size */
	struct root_hold_token	*ntb_rootmount; /* Root mount delay token. */
};

enum {
	NTBN_SIGN = 0,
	NTBN_SIZE_HIGH,
	NTBN_OFF_HIGH,
	NTBN_OFF_LOW,
};

#define NTBN_SIGN_SHORT	0x4e564430
#define NTBN_SIGN_LONG	0x4e5644494d4d3030

SYSCTL_NODE(_hw, OID_AUTO, pmem, CTLFLAG_RW, 0, "PMEM stuff");

static u_int ntb_pmem_start_timeout = 120;
SYSCTL_UINT(_hw_pmem, OID_AUTO, start_timeout, CTLFLAG_RWTUN,
    &ntb_pmem_start_timeout, 0, "Time to wait for NTB connection");

static int
pmem_open(struct disk *dp)
{
	struct pmem_disk *sc = dp->d_drv1;

	sc->label->opened = 1;
	return (0);
}

static int
pmem_close(struct disk *dp)
{
	struct pmem_disk *sc = dp->d_drv1;

	sc->label->opened = 0;
	return (0);
}

void ntb_copy1(void *dst, void *src, size_t len);
void ntb_copy2(void *dst1, void *dst2, void *src, size_t len);

static void
pmem_strategy(struct bio *bp)
{
	struct pmem_disk *sc = bp->bio_disk->d_drv1;
	uint8_t *addr, *nvaddr;
	vm_offset_t page;
	off_t done, off, moff, size;

	if (bp->bio_cmd != BIO_READ && bp->bio_cmd != BIO_WRITE) {
		bp->bio_flags |= BIO_ERROR;
		bp->bio_error = EOPNOTSUPP;
		biodone(bp);
		return;
	}

	for (done = 0; done < bp->bio_length; done += size) {
		if (bp->bio_flags & BIO_UNMAPPED) {
			moff = bp->bio_ma_offset + done;
			page = pmap_quick_enter_page(bp->bio_ma[moff / PAGE_SIZE]);
			moff %= PAGE_SIZE;
			addr = (void *)(page + moff);
			size = MIN(bp->bio_length - done, PAGE_SIZE - moff);
		} else {
			addr = bp->bio_data;
			size = bp->bio_length;
		}
		off = bp->bio_offset + done;

		if (bp->bio_cmd == BIO_READ) {
			memcpy(addr, sc->vaddr + off, size);
		} else if (bp->bio_cmd == BIO_WRITE) {
			sc->label->empty = 0;
			if ((nvaddr = sc->rvaddr) != NULL) {
				ntb_copy2(sc->vaddr + off, nvaddr + off,
				    addr, size);
			} else {
				ntb_copy1(sc->vaddr + off, addr, size);
				sc->label->dirty = 1;
			}
		}

		if (bp->bio_flags & BIO_UNMAPPED)
			pmap_quick_remove_page(page);
	}

	biodone(bp);
}

static void
pmem_identify(driver_t *driver, device_t parent)
{
	device_t dev;
	struct bios_smap *smapbase, *smap, *smapend;
	caddr_t kmdp;
	uint32_t smapsize;
	int error, n;

	if (resource_disabled("pmem", 0))
		return;

	/* Retrieve the system memory map from the loader. */
	kmdp = preload_search_by_type("elf kernel");
	if (kmdp == NULL)
		kmdp = preload_search_by_type(ELF_KERN_STR);
	smapbase = (struct bios_smap *)preload_search_info(kmdp,
	    MODINFO_METADATA | MODINFOMD_SMAP);
	if (smapbase == NULL)
		return;
	smapsize = *((u_int32_t *)smapbase - 1);
	smapend = (struct bios_smap *)((uintptr_t)smapbase + smapsize);

	n = 0;
	for (smap = smapbase; smap < smapend; smap++) {
		if (smap->type != 12 || smap->length == 0)
			continue;

		if (device_find_child(parent, "pmem", n) != NULL)
			continue;

		dev = BUS_ADD_CHILD(parent, 0, "pmem", n);
		if (dev) {
			error = bus_set_resource(dev, SYS_RES_MEMORY, 0,
			    smap->base, smap->length);
			if (error) {
				device_printf(dev, "Setting resource failed");
				device_delete_child(parent, dev);
			}
		} else {
			device_printf(parent, "Adding pmem child failed");
		}
		n++;
	}
}

static int
pmem_probe(device_t dev)
{
	struct resource *res;
	int rid;
	uintmax_t size;
	char buf[32];

	rid = 0;
	res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, 0);
	if (res == NULL)
		return (ENXIO);
	size = rman_get_size(res);
	bus_release_resource(dev, SYS_RES_MEMORY, rid, res);

	snprintf(buf, sizeof(buf), "PMEM region %juGB",
	    (uintmax_t)(size + 536870912) / 1073741824);
	device_set_desc_copy(dev, buf);
	return (0);
}

static int
pmem_attach(device_t dev)
{
	struct pmem_disk *sc = device_get_softc(dev);
	struct disk *disk;

	sc->rid = 0;
	sc->res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &sc->rid, 0);
	if (sc->res == NULL)
		return (ENXIO);
	sc->paddr = rman_get_start(sc->res);
	sc->size = rman_get_size(sc->res);
	sc->vaddr = pmap_mapdev_attr(sc->paddr, sc->size, VM_MEMATTR_DEFAULT);
	sc->rvaddr = NULL;

	sc->label = (struct pmem_label *)(sc->vaddr + sc->size - PAGE_SIZE);
	sc->rlabel = NULL;

	if (sc->label->sign != NTBN_SIGN_LONG) {
		device_printf(dev, "PMEM not labeled, new or data loss!\n");
		memset(sc->label, 0, PAGE_SIZE);
		sc->label->sign = NTBN_SIGN_LONG;
		arc4rand(&sc->label->array, sizeof(sc->label->array), 0);
		sc->label->empty = 1;
		sc->label->dirty = 0;
		sc->label->state = STATE_NONE;
	}
	sc->label->opened = 0;
	sc->label->state = MIN(sc->label->state, STATE_IDLE);

	disk = sc->disk = disk_alloc();
	disk->d_open = pmem_open;
	disk->d_close = pmem_close;
	disk->d_strategy = pmem_strategy;
	disk->d_name = "pmem";
	disk->d_unit = device_get_unit(dev);
	disk->d_drv1 = sc;
	disk->d_maxsize = MAXPHYS;
	disk->d_sectorsize = DEV_BSIZE;
	disk->d_mediasize = sc->size - PAGE_SIZE;
	disk->d_flags = DISKFLAG_UNMAPPED_BIO | DISKFLAG_DIRECT_COMPLETION;
	snprintf(disk->d_ident, sizeof(disk->d_ident),
	    "%016jX", (uintmax_t)sc->label->array);
	snprintf(disk->d_descr, sizeof(disk->d_descr),
	    "PMEM region %juGB", (uintmax_t)(sc->size + 536870912) /
	    1073741824);
	disk->d_rotation_rate = DISK_RR_NON_ROTATING;
	disk_create(disk, DISK_VERSION);
	return (0);
}

static int
pmem_detach(device_t dev)
{
	struct pmem_disk *sc = device_get_softc(dev);

	disk_destroy(sc->disk);
	pmap_unmapdev((vm_offset_t)sc->vaddr, sc->size);
	bus_release_resource(dev, SYS_RES_MEMORY, sc->rid, sc->res);
	return (0);
}

static device_method_t pmem_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,	pmem_identify),
	DEVMETHOD(device_probe,		pmem_probe),
	DEVMETHOD(device_attach,	pmem_attach),
	DEVMETHOD(device_detach,	pmem_detach),
	{ 0, 0 }
};

static driver_t pmem_driver = {
	"pmem",
	pmem_methods,
	sizeof(struct pmem_disk),
};

static devclass_t pmem_devclass;
static devclass_t nvdimm_root_devclass;

DRIVER_MODULE(pmem, nexus, pmem_driver, pmem_devclass, 0, 0);
DRIVER_MODULE(pmem, nvdimm_root, pmem_driver, pmem_devclass, 0, 0);
MODULE_VERSION(nvdimm, 1);

/* Hooks for the ACPI CA debugging infrastructure */
#define _COMPONENT	ACPI_BUS
ACPI_MODULE_NAME("NVDIMM")

struct nvdimm_root {
};

struct nvdimm_child {
	struct resource_list resources;
	ACPI_HANDLE handle;
	int adr;
	int domain;
	int interleave;
	uint64_t size;
};

static int
nvdimm_probe(device_t dev)
{
	struct nvdimm_child *ivar;
	char buf[32];

	ivar = (struct nvdimm_child *)device_get_ivars(dev);
	ACPI_FUNCTION_TRACE((char *)(uintptr_t) __func__);

	if (acpi_disabled("nvdimm"))
		return (ENXIO);

	snprintf(buf, sizeof(buf), "NVDIMM region %juGB interleave %d",
	    (uintmax_t)(ivar->size + 536870912) / 1073741824, ivar->interleave);
	device_set_desc_copy(dev, buf);
	return (0);
}

static int
nvdimm_attach(device_t dev)
{

	return (0);
}

static int
nvdimm_detach(device_t dev)
{

	return (0);
}

static device_method_t nvdimm_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		nvdimm_probe),
	DEVMETHOD(device_attach,	nvdimm_attach),
	DEVMETHOD(device_detach,	nvdimm_detach),
	{ 0, 0 }
};

static driver_t nvdimm_driver = {
	"nvdimm",
	nvdimm_methods,
	0,
};

static devclass_t nvdimm_devclass;
DRIVER_MODULE(nvdimm, nvdimm_root, nvdimm_driver, nvdimm_devclass, 0, 0);

static char *nvdimm_root_ids[] = { "ACPI0012", NULL };
static uint8_t pmem_uuid[ACPI_UUID_LENGTH] = {
	0x79, 0xd3, 0xf0, 0x66, 0xf3, 0xb4, 0x74, 0x40,
	0xac, 0x43, 0x0d, 0x33, 0x18, 0xb7, 0x8c, 0xdb
};

static int
nvdimm_root_probe(device_t dev)
{
	ACPI_FUNCTION_TRACE((char *)(uintptr_t) __func__);

	if (acpi_disabled("nvdimm"))
		return (ENXIO);
	if (acpi_get_handle(dev) != NULL &&
	    ACPI_ID_PROBE(device_get_parent(dev), dev, nvdimm_root_ids) == NULL)
		return (ENXIO);

	device_set_desc(dev, "NVDIMM root");
	return (0);
}

struct nvdimm_root_walk_ctx {
	device_t	dev;
	ACPI_NFIT_SYSTEM_ADDRESS **sas;
	ACPI_NFIT_MEMORY_MAP **mms;
	u_int		san, mmn;
};

static ACPI_STATUS
nvdimm_root_walk_dev(ACPI_HANDLE handle, UINT32 level, void *ctx, void **st)
{
	struct nvdimm_root_walk_ctx *wctx = ctx;
	device_t dev = wctx->dev;
	ACPI_STATUS status;
	ACPI_NFIT_SYSTEM_ADDRESS **sas = wctx->sas;
	ACPI_NFIT_MEMORY_MAP *mm;
	struct nvdimm_child	*ivar;
	device_t	child;
	uint64_t	size;
	u_int adr, sai, mmi;

	status = acpi_GetInteger(handle, "_ADR", &adr);
	if (ACPI_FAILURE(status))
		return_ACPI_STATUS(status);

	for (mmi = 0; mmi < wctx->mmn; mmi++) {
		if (wctx->mms[mmi]->DeviceHandle == adr)
			break;
	}
	if (mmi >= wctx->mmn)
		return_ACPI_STATUS(AE_OK);

	mm = wctx->mms[mmi];
	if (mm->RangeIndex == 0)
		return_ACPI_STATUS(AE_OK);
	for (sai = 0; sai < wctx->san; sai++) {
		if (sas[sai]->RangeIndex == mm->RangeIndex)
			break;
	}
	if (sai >= wctx->san) {
		device_printf(dev, "Unknown SPA Range Structure Index: %u\n",
		    mm->RangeIndex);
		return_ACPI_STATUS(AE_OK);
	}

	if (device_find_child(dev, "nvdimm", mm->RegionId) != NULL)
		return_ACPI_STATUS(AE_OK);

	ivar = malloc(sizeof(struct nvdimm_child), M_DEVBUF,
		M_WAITOK | M_ZERO);
	resource_list_init(&ivar->resources);
	size = mm->RegionSize * MAX(1, mm->InterleaveWays);
	resource_list_add(&ivar->resources, SYS_RES_MEMORY,
	    0, sas[sai]->Address + mm->RegionOffset,
	    sas[sai]->Address + mm->RegionOffset + size - 1, size);
	ivar->handle = handle;
	ivar->adr = adr;
	if (sas[sai]->Flags & ACPI_NFIT_PROXIMITY_VALID)
		ivar->domain = sas[sai]->ProximityDomain;
	else
		ivar->domain = -1;
	ivar->size = mm->RegionSize;
	ivar->interleave = mm->InterleaveWays;

	child = device_add_child(dev, "nvdimm", mm->RegionId);
	if (child) {
		device_set_ivars(child, ivar);
		if (mm->Flags & ~ACPI_NFIT_MEM_HEALTH_ENABLED) {
			device_printf(child, "Flags: 0x%b\n",
			    mm->Flags, "\020"
			    "\001SAVE_FAILED\002RESTORE_FAILED"
			    "\003FLUSH_FAILED\004NOT_ARMED"
			    "\005HEALTH_OBSERVED\006HEALTH_ENABLED"
			    "\007MAP_FAILED");
		}
	} else {
		device_printf(dev, "Adding nvdimm%u failed\n",
		    mm->RegionId);
	}

	return_ACPI_STATUS(AE_OK);
}

static int
nvdimm_root_attach(device_t dev)
{
	ACPI_TABLE_NFIT	*nfit;
	ACPI_STATUS	status;
	ACPI_NFIT_HEADER	*subtable;
	uint8_t		*end;
	ACPI_NFIT_SYSTEM_ADDRESS *sas[64], *sa;
	ACPI_NFIT_MEMORY_MAP *mms[64];
	struct nvdimm_root_walk_ctx wctx;
	struct nvdimm_child	*ivar;
	device_t	child;
	int		error, sai, san = 0, mmn = 0;

	/* Search for NFIT table. */
	status = AcpiGetTable("NFIT", 0, (ACPI_TABLE_HEADER **)&nfit);
	if (ACPI_FAILURE(status))
		return (ENXIO);

	subtable = (ACPI_NFIT_HEADER *)(nfit + 1);
	end = (uint8_t *)nfit + nfit->Header.Length;
	while ((uint8_t *)subtable < end) {
		if (subtable->Length < sizeof(ACPI_NFIT_HEADER)) {
			device_printf(dev,
			    "Invalid subtable length: %u", subtable->Length);
			return (EIO);
		}
		if (subtable->Type == ACPI_NFIT_TYPE_SYSTEM_ADDRESS)
			sas[san++] = (ACPI_NFIT_SYSTEM_ADDRESS *)subtable;
		if (subtable->Type == ACPI_NFIT_TYPE_MEMORY_MAP)
			mms[mmn++] = (ACPI_NFIT_MEMORY_MAP *)subtable;
		subtable = (ACPI_NFIT_HEADER *)((char *)subtable +
		    subtable->Length);
	}

	wctx.dev = dev;
	wctx.sas = sas;
	wctx.mms = mms;
	wctx.san = san;
	wctx.mmn = mmn;
	status = AcpiWalkNamespace(ACPI_TYPE_DEVICE, acpi_get_handle(dev), 100,
	    nvdimm_root_walk_dev, NULL, &wctx, NULL);

	for (sai = 0; sai < san; sai++) {
		sa = sas[sai];
		if (memcmp(sa->RangeGuid, pmem_uuid, sizeof(pmem_uuid)) != 0)
			continue;
		if (device_find_child(dev, "pmem", sa->RangeIndex - 1) != NULL)
			return_ACPI_STATUS(AE_OK);

		ivar = malloc(sizeof(struct nvdimm_child), M_DEVBUF,
		    M_WAITOK | M_ZERO);
		resource_list_init(&ivar->resources);
		resource_list_add(&ivar->resources, SYS_RES_MEMORY,
		    0, sa->Address, sa->Address + sa->Length, sa->Length);
		if (sa->Flags & ACPI_NFIT_PROXIMITY_VALID)
			ivar->domain = sa->ProximityDomain;
		else
			ivar->domain = -1;
		ivar->size = sa->Length;

		child = device_add_child(dev, "pmem", sa->RangeIndex - 1);
		if (child) {
			device_set_ivars(child, ivar);
		} else {
			device_printf(dev, "Adding pmem%u failed\n",
			    sa->RangeIndex - 1);
		}
	}

	AcpiPutTable((ACPI_TABLE_HEADER *)nfit);

	error = bus_generic_attach(dev);
	if (error != 0)
		device_printf(dev, "bus_generic_attach failed\n");

	return (0);
}

static int
nvdimm_root_detach(device_t dev)
{

	device_delete_children(dev);
	return (0);
}

static int
nvdimm_root_get_domain(device_t dev, device_t child, int *domain)
{
	struct nvdimm_child *ivar;

	ivar = (struct nvdimm_child *)device_get_ivars(child);
	if (ivar->domain >= 0) {
		*domain = ivar->domain;
		return (0);
	}
	return (bus_generic_get_domain(dev, child, domain));
}

static struct resource_list *
nvdimm_root_get_resource_list(device_t dev, device_t child)
{
	struct nvdimm_child *ivar;

	ivar = (struct nvdimm_child *)device_get_ivars(child);
	return &ivar->resources;
}

static int
nvdimm_root_print_child(device_t dev, device_t child)
{
	struct nvdimm_child *ivar;
	struct resource_list *rl;
	int	retval = 0;

	ivar = (struct nvdimm_child *)device_get_ivars(child);
	rl = &ivar->resources;

	retval += bus_print_child_header(dev, child);
	if (STAILQ_FIRST(rl))
		retval += printf(" at");
	retval += resource_list_print_type(rl, "iomem", SYS_RES_MEMORY, "%#jx");
	retval += bus_print_child_domain(dev, child);
	retval += bus_print_child_footer(dev, child);

	return (retval);
}

static int
nvdimm_root_child_location_str(device_t dev, device_t child, char *buf,
    size_t buflen)
{
	struct nvdimm_child *ivar;

	ivar = (struct nvdimm_child *)device_get_ivars(child);
	if (ivar->handle != NULL) {
		snprintf(buf, buflen, "handle=%s _ADR=%u",
		    acpi_name(ivar->handle), ivar->adr);
	}
	return (0);
}

static device_method_t nvdimm_root_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		nvdimm_root_probe),
	DEVMETHOD(device_attach,	nvdimm_root_attach),
	DEVMETHOD(device_detach,	nvdimm_root_detach),

	DEVMETHOD(bus_print_child,	nvdimm_root_print_child),
	DEVMETHOD(bus_child_location_str, nvdimm_root_child_location_str),
	DEVMETHOD(bus_get_domain,	nvdimm_root_get_domain),
	DEVMETHOD(bus_get_resource_list,nvdimm_root_get_resource_list),
	DEVMETHOD(bus_alloc_resource,	bus_generic_rl_alloc_resource),
	DEVMETHOD(bus_release_resource,	bus_generic_rl_release_resource),
	DEVMETHOD(bus_activate_resource, bus_generic_activate_resource),
	DEVMETHOD(bus_deactivate_resource, bus_generic_deactivate_resource),
	DEVMETHOD(bus_get_resource,	bus_generic_rl_get_resource),
	DEVMETHOD(bus_set_resource,	bus_generic_rl_set_resource),
	DEVMETHOD(bus_delete_resource,	bus_generic_rl_delete_resource),
	{ 0, 0 }
};

static driver_t nvdimm_root_driver = {
	"nvdimm_root",
	nvdimm_root_methods,
	sizeof(struct nvdimm_root),
};

DRIVER_MODULE(nvdimm_root, acpi, nvdimm_root_driver, nvdimm_root_devclass, 0, 0);
MODULE_DEPEND(nvdimm_root, acpi, 1, 1, 1);

static void
ntb_pmem_start(void *data)
{
	device_t dev = data;
	struct ntb_pmem *sc = device_get_softc(dev);

	if (sc->ntb_rootmount != NULL) {
		device_printf(dev, "Releasing root mount\n");
		root_mount_rel(sc->ntb_rootmount);
		sc->ntb_rootmount = NULL;
	}
}

static void
ntb_pmem_sync(void *data)
{
	device_t dev = data;
	struct ntb_pmem *scn = device_get_softc(dev);
	struct pmem_disk *sc = device_get_softc(scn->nvd_dev);
	struct pmem_label *ll = sc->label;
	struct pmem_label *rl = sc->rlabel;
	uint32_t state;
	int b, dir;

	if (rl == NULL || rl->sign != NTBN_SIGN_LONG) {
		device_printf(dev, "Can't see label on other side.\n");
		return;
	}

	dir = 0;
	if (rl->empty && !ll->empty) {
		device_printf(dev, "Other side is empty.\n");
		dir = 1;
	} else if (ll->empty && !rl->empty) {
		device_printf(dev, "Our side is empty.\n");
		dir = -1;
	} else if (rl->array != ll->array) {
		if (ll->empty && rl->empty)
			device_printf(dev, "Both sides are empty.\n");
		else
			device_printf(dev, "Two different arrays!\n");
		if (ll->array > rl->array)
			dir = 1;	/* Forcefully sync l->r. */
		else
			dir = -1;	/* Forcefully sync r->l. */
	} else if (!ll->dirty && !rl->dirty) {
		device_printf(dev, "Both sides are clean.\n");
	} else if (ll->opened && rl->opened) {
		device_printf(dev, "Both sides are opened!\n");
	} else if (ll->opened) {
		device_printf(dev, "Local side is opened.\n");
		dir = 1;
	} else if (rl->opened) {
		device_printf(dev, "Remote side is opened.\n");
		dir = -1;
	} else if (ll->dirty) {
		device_printf(dev, "Local side is dirty.\n");
		dir = 1;
	} else if (rl->dirty) {
		device_printf(dev, "Remote side is dirty.\n");
		dir = -1;
	}

	atomic_store_rel_32(&ll->state, STATE_WAITING);
	while (((state = atomic_load_acq_32(&rl->state)) == STATE_NONE ||
	    state == STATE_IDLE) && sc->rlabel != NULL)
		cpu_spinwait();

	if (dir > 0) {
#if 0
		b = ticks;
		memset(sc->vaddr, 0, 1024*1024*1024);
		device_printf(dev, "memset() local %dMB/s\n", 1024*hz/(ticks - b));
		b = ticks;
		memset(sc->rvaddr, 0, 1024*1024*1024);
		device_printf(dev, "memset() remote %dMB/s\n", 1024*hz/(ticks - b));
		b = ticks;
		memcpy(sc->rvaddr, sc->vaddr, 1024*1024*1024);
		device_printf(dev, "local to remote %dMB/s\n", 1024*hz/(ticks - b));
		b = ticks;
		memcpy(sc->rvaddr, sc->vaddr, 1024*1024*1024);
		device_printf(dev, "local to remote %dMB/s\n", 1024*hz/(ticks - b));
		b = ticks;
		memcpy(sc->vaddr, sc->rvaddr, 1024*1024*1024);
		device_printf(dev, "remote to local %dMB/s\n", 1024*hz/(ticks - b));
		b = ticks;
		memcpy(sc->vaddr, sc->rvaddr, 1024*1024*1024);
		device_printf(dev, "remote to local %dMB/s\n", 1024*hz/(ticks - b));
#endif
		device_printf(dev, "Copying local to remote.\n");
		b = ticks;
		memcpy(sc->rvaddr, sc->vaddr, sc->size - PAGE_SIZE);
		device_printf(dev, "Copied %juMB at %juMB/s\n",
		    sc->size / 1024 / 1024,
		    sc->size * hz / 1024 / 1024 / imax(ticks - b, 1));
		rl->array = ll->array;
		rl->empty = 0;
		rl->dirty = 0;
		ll->empty = 0;
		ll->dirty = 0;
		pmap_invalidate_cache();
		atomic_store_rel_32(&ll->state, STATE_READY);
		atomic_store_rel_32(&rl->state, STATE_READY);
	} else if (dir < 0) {
		device_printf(dev, "Waiting for remote to local copy.\n");
		while (atomic_load_acq_32(&rl->state) == STATE_WAITING &&
		    sc->rlabel != NULL)
			cpu_spinwait();
		snprintf(sc->disk->d_ident, sizeof(sc->disk->d_ident),
		    "%016jX", (uintmax_t)ll->array);
		disk_media_changed(sc->disk, M_NOWAIT);
	} else {
		device_printf(dev, "No need to copy.\n");
		atomic_store_rel_32(&ll->state, STATE_READY);
	}
	device_printf(dev, "Sync is done.\n");
}

static void
ntb_pmem_link_work(void *data)
{
	device_t dev = data;
	struct ntb_pmem *sc = device_get_softc(dev);
	struct pmem_disk *scd = device_get_softc(sc->nvd_dev);
	vm_paddr_t off;
	uint32_t val;

	off = scd->paddr - sc->ntb_xpaddr;
	ntb_peer_spad_write(dev, NTBN_OFF_LOW, off & 0xffffffff);
	ntb_peer_spad_write(dev, NTBN_OFF_HIGH, off >> 32);
	ntb_peer_spad_write(dev, NTBN_SIZE_HIGH, scd->size >> 32);
	ntb_peer_spad_write(dev, NTBN_SIGN, NTBN_SIGN_SHORT);

	ntb_spad_read(dev, NTBN_SIGN, &val);
	if (val != NTBN_SIGN_SHORT)
		goto out;

	ntb_spad_read(dev, NTBN_SIZE_HIGH, &val);
	if (val != (scd->size >> 32)) {
		device_printf(dev, "PMEM sizes don't match (%u != %u)\n",
		    val << 2, (uint32_t)(scd->size >> 30));
		return;
	}

	ntb_spad_read(dev, NTBN_OFF_HIGH, &val);
	off = (vm_paddr_t)val << 32;
	ntb_spad_read(dev, NTBN_OFF_LOW, &val);
	off |= val;

	callout_stop(&sc->ntb_start);
	device_printf(dev, "Connection established\n");
	scd->rvaddr = sc->ntb_caddr + off;
	scd->rlabel = (struct pmem_label *)(scd->rvaddr + scd->size -
	    PAGE_SIZE);
	ntb_pmem_sync(dev);
	ntb_pmem_start(dev);
	return;
out:
	if (ntb_link_is_up(dev, NULL, NULL))
		callout_reset(&sc->ntb_link_work, hz/10, ntb_pmem_link_work, dev);
}

static void
ntb_pmem_link_event(void *data)
{
	device_t dev = data;
	struct ntb_pmem *sc = device_get_softc(dev);
	struct pmem_disk *scd = device_get_softc(sc->nvd_dev);

	if (ntb_link_is_up(dev, NULL, NULL)) {
		ntb_pmem_link_work(dev);
	} else {
		device_printf(dev, "Connection is down\n");
		callout_stop(&sc->ntb_link_work);

		/*
		 * The scratchpad registers keep the values if the remote side
		 * goes down, blast them now to give them a sane value the next
		 * time they are accessed.
		 */
		ntb_spad_clear(dev);

		scd->rvaddr = NULL;
		scd->rlabel = NULL;
		scd->label->state = MIN(scd->label->state, STATE_IDLE);
	}
}

static const struct ntb_ctx_ops ntb_pmem_ops = {
	.link_event = ntb_pmem_link_event,
	.db_event = NULL,
};

static int
ntb_pmem_probe(device_t dev)
{

	device_set_desc(dev, "NTB PMEM syncer");

	/*
	 * Use lower priority if we can't find pmemX device with equal
	 * unit number.  User may not want us, just not specified exactly.
	 */
	if (devclass_get_device(pmem_devclass, device_get_unit(dev)) == NULL)
		return (BUS_PROBE_LOW_PRIORITY);
	return (BUS_PROBE_DEFAULT);
}

static int
ntb_pmem_attach(device_t dev)
{
	struct ntb_pmem *sc = device_get_softc(dev);
	struct pmem_disk *scd;
	int error;

	/* Find pmemX device with equal unit number. */
	sc->nvd_dev = devclass_get_device(pmem_devclass, device_get_unit(dev));
	if (sc->nvd_dev == NULL) {
		device_printf(dev, "Can not find pmem%u device\n",
		    device_get_unit(dev));
		return (ENXIO);
	}
	scd = device_get_softc(sc->nvd_dev);

	/* Make sure we have enough NTB resources. */
	if (ntb_mw_count(dev) < 1) {
		device_printf(dev, "At least 1 memory window required.\n");
		return (ENXIO);
	}
	if (ntb_spad_count(dev) < 4) {
		device_printf(dev, "At least 4 scratchpads required.\n");
		return (ENXIO);
	}
	error = ntb_mw_get_range(dev, 0, &sc->ntb_paddr, &sc->ntb_caddr,
	    &sc->ntb_size, &sc->ntb_xalign, NULL, NULL);
	if (error != 0) {
		device_printf(dev, "ntb_mw_get_range() error %d\n", error);
		return (ENXIO);
	}
	sc->ntb_xpaddr = scd->paddr & ~(sc->ntb_xalign - 1);
	sc->ntb_xsize = scd->paddr - sc->ntb_xpaddr + scd->size;
	if (sc->ntb_size < sc->ntb_xsize) {
		device_printf(dev, "Memory window is too small (%ju < %ju).\n",
		    sc->ntb_size, sc->ntb_xsize);
		return (ENXIO);
	} else if (sc->ntb_size < 2 * scd->size) {
		device_printf(dev, "Memory window may be too small (%ju < %ju).\n",
		    sc->ntb_size, 2 * scd->size);
	}

	callout_init(&sc->ntb_link_work, 1);
	callout_init(&sc->ntb_start, 1);

	/* Delay boot if this PMEM ever saw NTB. */
	if (scd->label->state >= STATE_IDLE) {
		device_printf(dev, "PMEM saw NTB, delaying root mount.\n");
		sc->ntb_rootmount = root_mount_hold("ntb_pmem");
		callout_reset(&sc->ntb_start, ntb_pmem_start_timeout * hz,
		    ntb_pmem_start, dev);
	} else
		sc->ntb_rootmount = NULL;

	/* Allow write combining for the memory window. */
	error = ntb_mw_set_wc(dev, 0, VM_MEMATTR_WRITE_COMBINING);
	if (error != 0)
		device_printf(dev, "ntb_mw_set_wc() error %d\n", error);

	/* Setup address translation. */
	error = ntb_mw_set_trans(dev, 0, sc->ntb_xpaddr, sc->ntb_xsize);
	if (error != 0) {
		device_printf(dev, "ntb_mw_set_trans() error %d\n", error);
		return (ENXIO);
	}

	/* Bring up the link. */
	error = ntb_set_ctx(dev, dev, &ntb_pmem_ops);
	if (error != 0)
		device_printf(dev, "ntb_set_ctx() error %d\n", error);
	error = ntb_link_enable(dev, NTB_SPEED_AUTO, NTB_WIDTH_AUTO);
	if (error != 0)
		device_printf(dev, "ntb_link_enable() error %d\n", error);

	return (0);
}

static int
ntb_pmem_detach(device_t dev)
{
	struct ntb_pmem *sc = device_get_softc(dev);
	struct pmem_disk *scd = device_get_softc(sc->nvd_dev);
	int error;

	callout_drain(&sc->ntb_start);
	root_mount_rel(sc->ntb_rootmount);
	sc->ntb_rootmount = NULL;

	scd->rvaddr = NULL;
	scd->rlabel = NULL;
	error = ntb_link_disable(dev);
	if (error != 0)
		device_printf(dev, "ntb_link_disable() error %d\n", error);
	ntb_clear_ctx(dev);
	error = ntb_mw_clear_trans(dev, 0);
	if (error != 0)
		device_printf(dev, "ntb_mw_clear_trans() error %d\n", error);
	callout_drain(&sc->ntb_link_work);
	return (0);
}

static device_method_t ntb_pmem_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		ntb_pmem_probe),
	DEVMETHOD(device_attach,	ntb_pmem_attach),
	DEVMETHOD(device_detach,	ntb_pmem_detach),
	{ 0, 0 }
};

static driver_t ntb_pmem_driver = {
	"ntb_pmem",
	ntb_pmem_methods,
	sizeof(struct ntb_pmem),
};

static devclass_t ntb_pmem_devclass;

DRIVER_MODULE(ntb_pmem, ntb_hw, ntb_pmem_driver, ntb_pmem_devclass, 0, 0);
MODULE_DEPEND(ntb_pmem, ntb, 1, 1, 1);
MODULE_VERSION(ntb_pmem, 1);
