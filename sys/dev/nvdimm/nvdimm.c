/*-
 * Copyright (c) 2016 Alexander Motin <mav@FreeBSD.org>
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

#include "../ntb/ntb.h"

#define	ELF_KERN_STR	("elf"__XSTRING(__ELF_WORD_SIZE)" kernel")

enum {
	STATE_INCORRECT = 0,
	STATE_NONE,
	STATE_IDLE,
	STATE_WAITING,
	STATE_READY,
};

/* NVDIMM label */
struct nvdimm_label {
	uint64_t	sign;	/* NTBN_SIGN_LONG signature */
	uint64_t	array;	/* Unique array ID */
	uint32_t	empty;	/* NVDIMM is empty and was never written */
	uint32_t	dirty;	/* NVDIMM was written without NTB connection */
	uint32_t	opened;	/* NVDIMM device is open now */
	uint32_t	state;	/* Synchronization state */
};

/* NVDIMM device */
struct nvdimm_disk {
	struct disk		*disk;
	int			 rid;
	struct resource		*res;
	vm_paddr_t		 paddr;		/* Local NVDIMM phys address */
	vm_paddr_t		 size;		/* Local NVDIMM size */
	uint8_t			*vaddr;		/* Local NVDIMM KVA address */
	struct nvdimm_label	*label;		/* Local NVDIMM label */
	uint8_t			*rvaddr;	/* Remote NVDIMM KVA address */
	struct nvdimm_label	*rlabel;	/* Remote NVDIMM label */
};

/* NTB NVDIMM device */
struct ntb_nvdimm {
	device_t		 nvd_dev;	/* NVDIMM device. */
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

SYSCTL_NODE(_hw, OID_AUTO, nvdimm, CTLFLAG_RW, 0, "NVDIMM stuff");

static u_int ntb_nvdimm_start_timeout = 120;
SYSCTL_UINT(_hw_nvdimm, OID_AUTO, start_timeout, CTLFLAG_RWTUN,
    &ntb_nvdimm_start_timeout, 0, "Time to wait for NTB connection");

static int
nvdimm_open(struct disk *dp)
{
	struct nvdimm_disk *sc = dp->d_drv1;

	sc->label->opened = 1;
	return (0);
}

static int
nvdimm_close(struct disk *dp)
{
	struct nvdimm_disk *sc = dp->d_drv1;

	sc->label->opened = 0;
	return (0);
}

void ntb_copy1(void *dst, void *srv, size_t len);
void ntb_copy2(void *dst1, void *dst2, void *srv, size_t len);

static void
nvdimm_strategy(struct bio *bp)
{
	struct nvdimm_disk *sc = bp->bio_disk->d_drv1;
	uint8_t *nvaddr;

	if (bp->bio_cmd == BIO_READ) {
		memcpy(bp->bio_data, sc->vaddr + bp->bio_offset,
		    bp->bio_length);
	} else if (bp->bio_cmd == BIO_WRITE) {
		sc->label->empty = 0;
		if ((nvaddr = sc->rvaddr) != NULL) {
			ntb_copy2(sc->vaddr + bp->bio_offset,
			    nvaddr + bp->bio_offset,
			    bp->bio_data, bp->bio_length);
		} else {
			ntb_copy1(sc->vaddr + bp->bio_offset,
			    bp->bio_data, bp->bio_length);
			sc->label->dirty = 1;
		}
	} else {
		bp->bio_flags |= BIO_ERROR;
		bp->bio_error = EOPNOTSUPP;
	}
	biodone(bp);
}

static void
nvdimm_identify(driver_t *driver, device_t parent)
{
	device_t dev;
	struct bios_smap *smapbase, *smap, *smapend;
	caddr_t kmdp;
	uint32_t smapsize;
	int error, n;

	if (resource_disabled("nvdimm", 0))
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

		if (device_find_child(parent, "nvdimm", n) != NULL)
			continue;

		dev = BUS_ADD_CHILD(parent, 0, "nvdimm", n);
		if (dev) {
			error = bus_set_resource(dev, SYS_RES_MEMORY, 0,
			    smap->base, smap->length);
			if (error) {
				device_printf(dev, "Setting resource failed");
				device_delete_child(parent, dev);
			}
		} else {
			device_printf(parent, "Adding nvdimm child failed");
		}
		n++;
	}
}

static int
nvdimm_probe(device_t dev)
{

	device_set_desc(dev, "NVDIMM");
	return (0);
}

static int
nvdimm_attach(device_t dev)
{
	struct nvdimm_disk *sc = device_get_softc(dev);
	struct disk *disk;

	sc->rid = 0;
	sc->res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &sc->rid, 0);
	if (sc->res == NULL)
		return (ENXIO);
	sc->paddr = rman_get_start(sc->res);
	sc->size = rman_get_size(sc->res);
	sc->vaddr = pmap_mapdev_attr(sc->paddr, sc->size, VM_MEMATTR_DEFAULT);

	sc->label = (struct nvdimm_label *)(sc->vaddr + sc->size - PAGE_SIZE);
	sc->rlabel = NULL;

	if (sc->label->sign != NTBN_SIGN_LONG) {
		device_printf(dev, "NVDIMM not labeled, new or data loss!\n");
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
	disk->d_open = nvdimm_open;
	disk->d_close = nvdimm_close;
	disk->d_strategy = nvdimm_strategy;
	disk->d_name = "nvdimm";
	disk->d_unit = device_get_unit(dev);
	disk->d_drv1 = sc;
	disk->d_maxsize = MAXPHYS;
	disk->d_sectorsize = DEV_BSIZE;
	disk->d_mediasize = sc->size - PAGE_SIZE;
	disk->d_flags = DISKFLAG_DIRECT_COMPLETION;
	disk_create(disk, DISK_VERSION);
	return (0);
}

static int
nvdimm_detach(device_t dev)
{
	struct nvdimm_disk *sc = device_get_softc(dev);

	disk_destroy(sc->disk);
	pmap_unmapdev((vm_offset_t)sc->vaddr, sc->size);
	bus_release_resource(dev, SYS_RES_MEMORY, sc->rid, sc->res);
	return (0);
}

static device_method_t nvdimm_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,	nvdimm_identify),
	DEVMETHOD(device_probe,		nvdimm_probe),
	DEVMETHOD(device_attach,	nvdimm_attach),
	DEVMETHOD(device_detach,	nvdimm_detach),
	{ 0, 0 }
};

static driver_t nvdimm_driver = {
	"nvdimm",
	nvdimm_methods,
	sizeof(struct nvdimm_disk),
};

static devclass_t nvdimm_devclass;

DRIVER_MODULE(nvdimm, nexus, nvdimm_driver, nvdimm_devclass, 0, 0);
MODULE_VERSION(mvdimm, 1);


static void
ntb_nvdimm_start(void *data)
{
	device_t dev = data;
	struct ntb_nvdimm *sc = device_get_softc(dev);

	device_printf(dev, "Releasing root mount\n");
	root_mount_rel(sc->ntb_rootmount);
	sc->ntb_rootmount = NULL;
}

static void
ntb_nvdimm_sync(void *data)
{
	device_t dev = data;
	struct ntb_nvdimm *scn = device_get_softc(dev);
	struct nvdimm_disk *sc = device_get_softc(scn->nvd_dev);
	struct nvdimm_label *ll = sc->label;
	struct nvdimm_label *rl = sc->rlabel;
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
		    sc->size * hz / 1024 / 1024 / (ticks - b));
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
		disk_media_changed(sc->disk, M_NOWAIT);
	} else {
		device_printf(dev, "No need to copy.\n");
		atomic_store_rel_32(&ll->state, STATE_READY);
	}
	device_printf(dev, "Sync is done.\n");
}

static void
ntb_nvdimm_link_work(void *data)
{
	device_t dev = data;
	struct ntb_nvdimm *sc = device_get_softc(dev);
	struct nvdimm_disk *scd = device_get_softc(sc->nvd_dev);
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
		device_printf(dev, "NVDIMM sizes don't match (%u != %u)\n",
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
	scd->rlabel = (struct nvdimm_label *)(scd->rvaddr + scd->size -
	    PAGE_SIZE);
	ntb_nvdimm_sync(dev);
	ntb_nvdimm_start(dev);
	return;
out:
	if (ntb_link_is_up(dev, NULL, NULL))
		callout_reset(&sc->ntb_link_work, hz/10, ntb_nvdimm_link_work, dev);
}

static void
ntb_nvdimm_link_event(void *data)
{
	device_t dev = data;
	struct ntb_nvdimm *sc = device_get_softc(dev);
	struct nvdimm_disk *scd = device_get_softc(sc->nvd_dev);

	if (ntb_link_is_up(dev, NULL, NULL)) {
		ntb_nvdimm_link_work(dev);
	} else {
		device_printf(dev, "Connection lost\n");
		callout_stop(&sc->ntb_link_work);
		scd->rvaddr = NULL;
		scd->rlabel = NULL;
		scd->label->state = MIN(scd->label->state, STATE_IDLE);
	}
}

static const struct ntb_ctx_ops ntb_nvdimm_ops = {
	.link_event = ntb_nvdimm_link_event,
	.db_event = NULL,
};

static int
ntb_nvdimm_probe(device_t dev)
{

	device_set_desc(dev, "NTB NVDIMM syncer");
	return (0);
}

static int
ntb_nvdimm_attach(device_t dev)
{
	struct ntb_nvdimm *sc = device_get_softc(dev);
	struct nvdimm_disk *scd;
	int error;

	sc->nvd_dev = devclass_get_device(nvdimm_devclass, 0);
	if (sc->nvd_dev == NULL) {
		device_printf(dev, "Can not find nvdimm0 device\n");
		return (ENXIO);
	}
	scd = device_get_softc(sc->nvd_dev);

	callout_init(&sc->ntb_link_work, 1);
	callout_init(&sc->ntb_start, 1);

	/* Delay boot if this NVDIMM ever saw NTB. */
	if (scd->label->state >= STATE_IDLE) {
		device_printf(dev, "NVDIMM saw NTB, delaying root mount.\n");
		callout_reset(&sc->ntb_start, ntb_nvdimm_start_timeout * hz,
		    ntb_nvdimm_start, dev);
		sc->ntb_rootmount = root_mount_hold("ntb_nvdimm");
	}

	/* Get memory window parameters. */
	error = ntb_mw_get_range(dev, 0, &sc->ntb_paddr, &sc->ntb_caddr,
	    &sc->ntb_size, &sc->ntb_xalign, NULL, NULL);
	if (error != 0) {
		device_printf(dev, "ntb_mw_get_range() error %d\n", error);
		return (ENXIO);
	}
	error = ntb_mw_set_wc(dev, 0, VM_MEMATTR_WRITE_COMBINING);
	if (error != 0)
		device_printf(dev, "ntb_mw_set_wc() error %d\n", error);

	/* Setup XLAT. */
	sc->ntb_xpaddr = scd->paddr & ~(sc->ntb_xalign - 1);
	sc->ntb_xsize = scd->paddr - sc->ntb_xpaddr + scd->size;
	error = ntb_mw_set_trans(dev, 0, sc->ntb_xpaddr, sc->ntb_xsize);
	if (error != 0) {
		device_printf(dev, "ntb_mw_set_trans() error %d\n", error);
		return (ENXIO);
	}

	/* Bring up the link. */
	error = ntb_set_ctx(dev, dev, &ntb_nvdimm_ops);
	if (error != 0)
		device_printf(dev, "ntb_set_ctx() error %d\n", error);
	error = ntb_link_enable(dev, NTB_SPEED_AUTO, NTB_WIDTH_AUTO);
	if (error != 0)
		device_printf(dev, "ntb_link_enable() error %d\n", error);

	return (0);
}

static int
ntb_nvdimm_detach(device_t dev)
{
	struct ntb_nvdimm *sc = device_get_softc(dev);
	struct nvdimm_disk *scd = device_get_softc(sc->nvd_dev);
	int error;

	callout_drain(&sc->ntb_start);
	root_mount_rel(sc->ntb_rootmount);
	sc->ntb_rootmount = NULL;

	scd->rvaddr = NULL;
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

static device_method_t ntb_nvdimm_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		ntb_nvdimm_probe),
	DEVMETHOD(device_attach,	ntb_nvdimm_attach),
	DEVMETHOD(device_detach,	ntb_nvdimm_detach),
	{ 0, 0 }
};

static driver_t ntb_nvdimm_driver = {
	"ntb_nvdimm",
	ntb_nvdimm_methods,
	sizeof(struct ntb_nvdimm),
};

static devclass_t ntb_nvdimm_devclass;

DRIVER_MODULE(ntb_nvdimm, ntb_hw, ntb_nvdimm_driver, ntb_nvdimm_devclass, 0, 0);
MODULE_DEPEND(ntb_nvdimm, ntb, 1, 1, 1);
MODULE_VERSION(ntb_mvdimm, 1);
