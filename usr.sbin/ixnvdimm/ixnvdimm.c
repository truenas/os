/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2020 Alexander Motin <mav@FreeBSD.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <assert.h>
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>
#include <uuid.h>
#include <sys/endian.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <dev/ixnvdimm/ixnvdimm.h>

/* Page 0 */
#define	NVDIMM_MGT_CMD0		0x40
#define	NVDIMM_MGT_CMD1		0x41
#define	FIRMWARE_OPS_CMD	0x4a
#define	NVDIMM_CMD_STATUS0	0x61
#define	NVDIMM_CMD_STATUS1	0x62
#define	FIRMWARE_OPS_STATUS	0x71

/* Page 3 */
#define	TYPED_BLOCK_DATA	0x04
#define	FW_REGION_CRC0		0x40
#define	FW_REGION_CRC1		0x41
#define	FW_SLOT_INFO		0x42

#define	BLOCK_SIZE		32

#define MGUID "1EE68B36-D4BD-4a1a-9A16-4F8E53D46E05"
static struct uuid mguid;

static int
Crc16(char *ptr, int count)
{
	int crc, i;

	crc = 0;
	while (--count >= 0) {
		crc = crc ^ (int)*ptr++ << 8;
		for (i = 0; i < 8; ++i) {
			if (crc & 0x8000) {
				crc = crc << 1 ^ 0x1021;
			} else {
				crc = crc << 1;
			}
		}
	}
	return (crc & 0xFFFF);
}

static uint8_t
i2c_read(int fd, uint8_t page, uint8_t off)
{
	struct ixnvdimm_dsm d;
	uint8_t in_buf[2], out_buf[5];
	uint32_t status;

	bzero(&d, sizeof(d));
	d.guid = mguid;
	d.rev = 1;
	d.func = 27;
	in_buf[0] = page;
	in_buf[1] = off;
	d.in_size = sizeof(in_buf);
	d.in_buf = in_buf;
	d.out_size = sizeof(out_buf);
	d.out_buf = out_buf;
	if (ioctl(fd, IXNVDIMM_DSM, &d) || d.out_size < 5)
		err(EX_IOERR, "Can't read i2c (%0x, %0x)", page, off);
	status = le32dec(&out_buf[0]);
	if (status != 0)
		errx(EX_IOERR, "i2c read error (%0x, %0x): %x", page, off, status);
	return (out_buf[4]);
}

static void
i2c_write(int fd, uint8_t page, uint8_t off, uint8_t val)
{
	struct ixnvdimm_dsm d;
	uint8_t in_buf[3], out_buf[4];
	uint32_t status;

	bzero(&d, sizeof(d));
	d.guid = mguid;
	d.rev = 1;
	d.func = 28;
	in_buf[0] = page;
	in_buf[1] = off;
	in_buf[2] = val;
	d.in_size = sizeof(in_buf);
	d.in_buf = in_buf;
	d.out_size = sizeof(out_buf);
	d.out_buf = out_buf;
	if (ioctl(fd, IXNVDIMM_DSM, &d) || d.out_size < 4)
		err(EX_IOERR, "Can't write i2c (%0x, %0x)", page, off);
	status = le32dec(&out_buf[0]);
	if (status != 0)
		errx(EX_IOERR, "i2c write error (%0x, %0x): %x", page, off, status);
}

static uint16_t
i2c_read16(int fd, uint8_t page, uint8_t off)
{

	return (i2c_read(fd, page, off) |
	    ((uint16_t)i2c_read(fd, page, off + 1) << 8));
}

static void
i2c_write16(int fd, uint8_t page, uint8_t off, uint16_t val)
{

	i2c_write(fd, page, off, val);
	i2c_write(fd, page, off + 1, val >> 8);
}

static void
factory_default(int fd)
{
	struct ixnvdimm_dsm dsm;
	uint8_t status[4];

	/* Factory default via DSM. */
	i2c_write(fd, 0, NVDIMM_MGT_CMD1, 1 << 1);
	bzero(&dsm, sizeof(dsm));
	dsm.guid = mguid;
	dsm.rev = 1;
	dsm.func = 21;	/* Reset to Factory Defaults */
	memset(&status, 0xff, sizeof(status));
	dsm.out_size = sizeof(status);
	dsm.out_buf = status;
	if (ioctl(fd, IXNVDIMM_DSM, &dsm) || dsm.out_size != 4)
		err(EX_NOINPUT, "Can't call Reset to Factory Defaults DSM");
	if (le32dec(&status[0]) != 0)
		errx(EX_NOINPUT, "Reset to Factory Defaults DSM failed: %x",
		    le32dec(&status[0]));
	printf("Reset to Factory Defaults succeeded\n");
	fflush(stdout);
}

static void
firmware_update(int fd, struct ixnvdimm_info *info, const char *f)
{
	struct ixnvdimm_dsm dsm;
	int b, error = 0, ffd, r, rbs, regions, t, timeout, atimeout, try;
	struct stat sb;
	off_t size;
	uint8_t *image, status[4], buf[7 + BLOCK_SIZE];

	/* Firmware commands timeout in milliseconds. */
	timeout = i2c_read16(fd, 0x00, 0x22);
	if (timeout & 0x8000)
		timeout = (timeout & 0x7fff) * 1000;

	/* Abort commands timeout in milliseconds. */
	atimeout = i2c_read(fd, 0x00, 0x24);
	if (i2c_read(fd, 0x00, 0x11) & 0x02)
		atimeout = atimeout * 1000;

	/* Region size in blocks. */
	rbs = i2c_read(fd, 0x00, 0x32);

	if ((ffd = open(f, O_RDONLY)) < 0)
		err(EX_NOINPUT, "Can't open firmware file '%s'", f);
	if (fstat(ffd, &sb) < 0)
		err(EX_NOINPUT, "Can't stat firmware file '%s'", f);
	size = roundup(sb.st_size, rbs * BLOCK_SIZE);
	regions = size / (rbs * BLOCK_SIZE);
	if (size < BLOCK_SIZE)
		errx(EX_NOINPUT, "Firmware file '%s' is too small", f);
	image = calloc(1, size);
	if (image == NULL)
		err(EX_OSERR, "Can't allocate firmware memory");
	if (read(ffd, image, sb.st_size) != sb.st_size)
		err(EX_OSERR, "Can't read firmware file '%s'", f);
	close(ffd);

	/* Validate firmware image header. */
	if (image[31] == 0)
		t = 0x03;
	else if (image[31] == 1)
		t = image[19];
	else
		errx(EX_NOINPUT, "Unknown Firmware Image Header Format %u", image[31]);
	if ((t & 0x01) && le16dec(&image[0]) != info->VendorId)
		errx(EX_NOINPUT, "Module Manufacturer ID Code does not match");
	if ((t & 0x02) && le16dec(&image[2]) != info->DeviceId)
		errx(EX_NOINPUT, "Module Product Identifier does not match");
	if ((t & 0x04) && le16dec(&image[4]) != info->SubsystemVendorId)
		errx(EX_NOINPUT, "Non-Volatile Memory Subsystem Controller Module Manufacturer ID Code does not match");
	if ((t & 0x08) && le16dec(&image[6]) != info->SubsystemDeviceId)
		errx(EX_NOINPUT, "Non-Volatile Memory Subsystem Controller Module Product Identifier does not match");
	if ((t & 0x10) && image[8] != info->RevisionId)
		errx(EX_NOINPUT, "Module Revision Code does not match");
	if ((t & 0x20) && image[9] != info->SubsystemRevisionId)
		errx(EX_NOINPUT, "Non-Volatile Memory Subsystem Controller Revision Code does not match");
	if (be32dec(&image[20]) != sb.st_size)
		errx(EX_NOINPUT, "Size in firmware header does not match");
	if (be16dec(&image[24]) !=
	    Crc16((char *)image + BLOCK_SIZE, sb.st_size - BLOCK_SIZE))
		errx(EX_NOINPUT, "Checksum in firmware header does not match");

	/* Wait for the Operation In Progress bit in the NVDIMM_CMD_STATUS0 register to be clear. */
	if (i2c_read(fd, 0, NVDIMM_CMD_STATUS0) & 1) {
		printf("Waiting for Operation In Progress to clear\n");
		for (t = 0; (i2c_read(fd, 0, NVDIMM_CMD_STATUS0) & 1) && t < timeout; t++)
			usleep(1000);
		if (t >= timeout) {
			errx(EX_PROTOCOL, "NVDIMM has operation in progress: %02x",
			    i2c_read16(fd, 0, NVDIMM_CMD_STATUS0));
		}
	}

	/* Start firmware update via DSM. */
	i2c_write(fd, 0, NVDIMM_MGT_CMD1, 1 << 1);
	bzero(&dsm, sizeof(dsm));
	dsm.guid = mguid;
	dsm.rev = 1;
	dsm.func = 22;	/* Start Firmware Update */
	buf[0] = 1;	/* Firmware Slot */
	dsm.in_size = 1;
	dsm.in_buf = buf;
	memset(&status, 0xff, sizeof(status));
	dsm.out_size = sizeof(status);
	dsm.out_buf = status;
	if (ioctl(fd, IXNVDIMM_DSM, &dsm) || dsm.out_size != 4)
		err(EX_NOINPUT, "Can't call Start Firmware Update DSM");
	if (le32dec(&status[0]) != 0)
		errx(EX_NOINPUT, "Start Firmware Update DSM failed: %x",
		    le32dec(&status[0]));
	printf("Start Firmware Update succeeded\n");
	fflush(stdout);

	/* Check ACPI enabled firmware update mode. */
	if ((i2c_read(fd, 0, FIRMWARE_OPS_STATUS) & 0x04) == 0) {
		warnx("Firmware update mode is not enabled: %02x",
		    i2c_read(fd, 0, FIRMWARE_OPS_STATUS));
		goto error;
	}

	/* Clear firmware region. */
	i2c_write(fd, 0, NVDIMM_MGT_CMD1, 1 << 1);
	i2c_write(fd, 0, FIRMWARE_OPS_CMD, 0x02);
	for (t = 0; (i2c_read(fd, 0, NVDIMM_CMD_STATUS0) & 1) && t < timeout; t++)
		usleep(1000);
	if (t >= timeout) {
		warnx("Clear Firmware timeout: %04x",
		    i2c_read16(fd, 0, NVDIMM_CMD_STATUS0));
		goto timeout;
	}
	if (i2c_read(fd, 0, FIRMWARE_OPS_STATUS) & 0x02) {
		warnx("Clear Firmware failed: %02x",
		    i2c_read(fd, 0, FIRMWARE_OPS_STATUS));
		goto error2;
	}

	/* Send firmware one region at a time */
	i2c_write(fd, 3, TYPED_BLOCK_DATA, 0x01);
	for (r = 0; r < regions; r++) {
		printf("\r%d/%d ", r, regions);
		fflush(stdout);
		try = 0;

retry:
		/* Send region blocks via Send Firmware Update Data DSM. */
		for (b = 0; b < rbs; b++) {
			bzero(&dsm, sizeof(dsm));
			dsm.guid = mguid;
			dsm.rev = 1;
			dsm.func = 23;	/* Send Firmware Update Data */
			le32enc(&buf[0], BLOCK_SIZE);
			le16enc(&buf[4], r);
			buf[6] = b;
			memcpy(&buf[7], &image[(r * rbs + b) * BLOCK_SIZE], BLOCK_SIZE);
			dsm.in_size = sizeof(buf);
			dsm.in_buf = buf;
			memset(&status, 0xff, sizeof(status));
			dsm.out_size = sizeof(status);
			dsm.out_buf = status;
			if (ioctl(fd, IXNVDIMM_DSM, &dsm) || dsm.out_size != 4) {
				warn("Can't call Send Firmware Update Data DSM");
				goto error2;
			}
			if (le32dec(&status[0]) != 0) {
				warnx("Send Firmware Update Data DSM failed: %x",
				    le32dec(&status[0]));
				goto error2;
			}
		}

		/* Verify region checksum. */
		i2c_write(fd, 0, NVDIMM_MGT_CMD1, 1 << 1);
		i2c_write(fd, 0, FIRMWARE_OPS_CMD, 0x04);
		for (t = 0; (i2c_read(fd, 0, NVDIMM_CMD_STATUS0) & 1) && t < timeout; t++)
			usleep(1000);
		if (t >= timeout) {
			warnx("Generate Firmware Checksum timeout: %04x",
			    i2c_read16(fd, 0, NVDIMM_CMD_STATUS0));
			goto timeout;
		}
		if (i2c_read(fd, 0, FIRMWARE_OPS_STATUS) & 0x02) {
			warnx("Generate Firmware Checksum failed: %02x",
			    i2c_read(fd, 0, FIRMWARE_OPS_STATUS));
			goto error2;
		}
		if (i2c_read16(fd, 3, FW_REGION_CRC0) !=
		    Crc16((char *)image + r * rbs * BLOCK_SIZE, rbs * BLOCK_SIZE)) {
			if (++try < 3) {
				printf("Region checksum mismatch, retrying\n");
				goto retry;
			} else {
				warnx("Region checksum mismatch");
				goto error2;
			}
		}

		/*
		 * After sending the first region with the header in it ask
		 * the module to check that the new image really applies.
		 */
		if (r == 0) {
			i2c_write(fd, 0, NVDIMM_MGT_CMD1, 1 << 1);
			i2c_write(fd, 0, FIRMWARE_OPS_CMD, 0x10);
			for (t = 0; (i2c_read(fd, 0, NVDIMM_CMD_STATUS0) & 1) && t < timeout; t++)
				usleep(1000);
			if (t >= timeout) {
				warnx("Validate Firmware Header timeout: %04x",
				    i2c_read16(fd, 0, NVDIMM_CMD_STATUS0));
				goto timeout;
			}
			if (i2c_read(fd, 0, FIRMWARE_OPS_STATUS) & 0x02) {
				warnx("Validate Firmware Header failed: %02x",
				    i2c_read(fd, 0, FIRMWARE_OPS_STATUS));
				goto error2;
			}
			printf("\rValidate Firmware Header succeeded\n");
		}

		/* The region seems to be correct, commit it. */
		i2c_write(fd, 0, NVDIMM_MGT_CMD1, 1 << 1);
		i2c_write(fd, 0, FIRMWARE_OPS_CMD, 0x08);
		for (t = 0; (i2c_read(fd, 0, NVDIMM_CMD_STATUS0) & 1) && t < timeout; t++)
			usleep(1000);
		if (t >= timeout) {
			warnx("Commit Firmware timeout: %04x",
			    i2c_read16(fd, 0, NVDIMM_CMD_STATUS0));
			goto timeout;
		}
		if (i2c_read(fd, 0, FIRMWARE_OPS_STATUS) & 0x02) {
			warnx("Commit Firmware failed: %02x",
			    i2c_read(fd, 0, FIRMWARE_OPS_STATUS));
			goto error2;
		}
	}
	i2c_write(fd, 3, TYPED_BLOCK_DATA, 0x00);
	printf("\rSend Firmware Update Data succeeded\n");
	fflush(stdout);

	/* Validate firmware image. */
	printf("Validating firmware image... ");
	fflush(stdout);
	i2c_write(fd, 0, NVDIMM_MGT_CMD1, 1 << 1);
	i2c_write(fd, 0, FIRMWARE_OPS_CMD, 0x20);
	for (t = 0; (i2c_read(fd, 0, NVDIMM_CMD_STATUS0) & 1) && t < timeout; t++)
		usleep(1000);
	if (t >= timeout) {
		warnx("Validate Firmware Image timeout: %02x",
		    i2c_read16(fd, 0, NVDIMM_CMD_STATUS0));
timeout:
		/* Send Abort command. */
		i2c_write(fd, 0, 0x43, 0x10);
		for (t = 0; (i2c_read(fd, 0, NVDIMM_CMD_STATUS0) & 1) && t < atimeout; t++)
			usleep(1000);
		if (t >= atimeout)
			warnx("Abort command timeout: %04x",
			    i2c_read16(fd, 0, NVDIMM_CMD_STATUS0));
		else if ((i2c_read(fd, 0, FIRMWARE_OPS_STATUS) & 0x10) == 0)
			warnx("Abort command haven't succeeded: %02x",
			    i2c_read(fd, 0, FIRMWARE_OPS_STATUS));
		goto error2;
	}
	if (i2c_read(fd, 0, FIRMWARE_OPS_STATUS) & 0x02) {
		warnx("Validate Firmware Image failed: %02x",
		    i2c_read(fd, 0, FIRMWARE_OPS_STATUS));
error2:
		/* Disable firmware update mode. */
		i2c_write(fd, 0, NVDIMM_MGT_CMD1, 1 << 1);
		i2c_write(fd, 0, FIRMWARE_OPS_CMD, 0);
		for (t = 0; (i2c_read(fd, 0, NVDIMM_CMD_STATUS0) & 1) && t < timeout; t++)
			usleep(1000);
		if (t >= timeout)
			warnx("Disable firmware update mode timeout: %04x",
			    i2c_read16(fd, 0, NVDIMM_CMD_STATUS0));
		else if (i2c_read(fd, 0, FIRMWARE_OPS_STATUS) & 0x04)
			warnx("Firmware update mode disable error: %02x",
			    i2c_read(fd, 0, FIRMWARE_OPS_STATUS));
error:
		error = EX_PROTOCOL;
		goto done;
	}
	printf("\rValidate Firmware Image succeeded\n");
	fflush(stdout);

	/* Finish firmware update via DSM. */
	i2c_write(fd, 0, NVDIMM_MGT_CMD1, 1 << 1);
	bzero(&dsm, sizeof(dsm));
	dsm.guid = mguid;
	dsm.rev = 1;
	dsm.func = 24;	/* Finish Firmware Update */
	memset(&status, 0xff, sizeof(status));
	dsm.out_size = sizeof(status);
	dsm.out_buf = status;
	if (ioctl(fd, IXNVDIMM_DSM, &dsm) || dsm.out_size != 4)
		err(EX_NOINPUT, "Can't call Finish Firmware Update DSM");
	if (le32dec(&status[0]) != 0)
		errx(EX_NOINPUT, "Finish Firmware Update DSM status: %x",
		    le32dec(&status[0]));
	printf("Finish Firmware Update succeeded\n");
	fflush(stdout);

	/* Select firmware image slot 1 */
	i2c_write(fd, 0, NVDIMM_MGT_CMD1, 1 << 1);
	bzero(&dsm, sizeof(dsm));
	dsm.guid = mguid;
	dsm.rev = 1;
	dsm.func = 25;
	buf[0] = 1;	/* Firmware Slot 1 */
	dsm.in_size = 1;
	dsm.in_buf = buf;
	memset(&status, 0xff, sizeof(status));
	dsm.out_size = sizeof(status);
	dsm.out_buf = status;
	if (ioctl(fd, IXNVDIMM_DSM, &dsm) || dsm.out_size != 4)
		err(EX_NOINPUT, "Can't call Select Firmware Image Slot DSM");
	if (le32dec(&status[0]) != 0)
		errx(EX_NOINPUT, "Select Firmware Image Slot DSM failed: %x",
		    le32dec(&status[0]));
	printf("Select Firmware Image Slot 1 succeeded\n");
	fflush(stdout);

done:
	free(image);
}

static void
health(int fd, struct ixnvdimm_info *info)
{
	struct ixnvdimm_dsm dsm;
	int val;
	char buf[16];

	printf("Module:\n");
	printf("vendor: %04x device: %04x revision: %02x\n",
	    info->VendorId, info->DeviceId, info->RevisionId);
	printf("subvendor: %04x subdevice: %04x subrevision: %02x\n",
	    info->SubsystemVendorId, info->SubsystemDeviceId,
	    info->SubsystemRevisionId);
	printf("serial: %08X\n", be32toh(info->SerialNumber));

	printf("\nFirmware:\n");
	printf("slot0: %04x sub: %02x es: %04x\n", i2c_read16(fd, 0, 0x07),
	    i2c_read(fd, 0, 0x0b), i2c_read16(fd, 1, 0x08));
	printf("slot1: %04x sub: %02x es: %04x\n", i2c_read16(fd, 0, 0x09),
	    i2c_read(fd, 0, 0x0c), i2c_read16(fd, 1, 0x0a));
	val = i2c_read(fd, 0x03, FW_SLOT_INFO);
	printf("selected: %u running: %u\n", val & 0x0f, val >> 4);

	bzero(&dsm, sizeof(dsm));
	dsm.guid = mguid;
	dsm.rev = 1;
	dsm.func = 10;
	bzero(&buf, sizeof(buf));
	dsm.out_size = sizeof(buf);
	dsm.out_buf = buf;
	if (ioctl(fd, IXNVDIMM_DSM, &dsm) == 0 && dsm.out_size >= 5) {
		val = buf[4];
		printf("\nCritical Health Info: 0x%02x <%s%s%s%s%s%s>\n",
		    val,
		    val & 0x01 ? "PERSISTENCY_LOST_ERROR,":"",
		    val & 0x02 ? "WARNING_THRESHOLD_EXCEEDED,":"",
		    val & 0x04 ? "PERSISTENCY_RESTORED,":"",
		    val & 0x08 ? "BELOW_WARNING_THRESHOLD,":"",
		    val & 0x10 ? "PERMANENT_HARDWARE_FAILURE,":"",
		    val & 0x20 ? "EVENT_N_LOW":"");
	}

	bzero(&dsm, sizeof(dsm));
	dsm.guid = mguid;
	dsm.rev = 1;
	dsm.func = 11;
	bzero(&buf, sizeof(buf));
	dsm.out_size = sizeof(buf);
	dsm.out_buf = buf;
	if (ioctl(fd, IXNVDIMM_DSM, &dsm) == 0 && dsm.out_size >= 13) {
		val = le16dec(&buf[4]);
		printf("\nModule Health: 0x%04x <%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s>\n",
		    val,
		    val & 0x0001 ? "VOLTAGE_REGULATOR_FAILED,":"",
		    val & 0x0002 ? "VDD_LOST,":"",
		    val & 0x0004 ? "VPP_LOST,":"",
		    val & 0x0008 ? "VTT_LOST,":"",
		    val & 0x0010 ? "DRAM_NOT_SELF_REFRESH,":"",
		    val & 0x0020 ? "CONTROLLER_HARDWARE_ERROR,":"",
		    val & 0x0040 ? "NVM_CONTROLLER_ERROR,":"",
		    val & 0x0080 ? "NVM_LIFETIME_ERROR,":"",
		    val & 0x0100 ? "NOT_ENOUGH_ENERGY_FOR_CSAVE,":"",
		    val & 0x0200 ? "INVALID_FIRMWARE_ERROR,":"",
		    val & 0x0400 ? "CONFIG_DATA_ERROR,":"",
		    val & 0x0800 ? "NO_ES_PRESENT,":"",
		    val & 0x1000 ? "ES_POLICY_NOT_SET,":"",
		    val & 0x2000 ? "ES_HARDWARE_FAILURE,":"",
		    val & 0x4000 ? "ES_HEALTH_ASSESSMENT_ERROR,":"");
		val = le16dec(&buf[6]);
		printf("Module Current Temperature: %d C\n", val);
		val = buf[8];
		printf("Error Threshold Status: 0x%02x <%s%s%s>\n",
		    val,
		    val & 0x01 ? "NVM_LIFETIME_ERROR,":"",
		    val & 0x02 ? "ES_LIFETIME_ERROR,":"",
		    val & 0x04 ? "ES_TEMP_ERROR,":"");
		val = buf[9];
		printf("Warning Threshold Status: 0x%02x <%s%s%s>\n",
		    val,
		    val & 0x01 ? "NVM_LIFETIME_WARNING,":"",
		    val & 0x01 ? "ES_LIFETIME_WARNING,":"",
		    val & 0x01 ? "ES_TEMP_WARNING,":"");
		printf("NVM Lifetime: %d%%\n",  buf[10]);
		printf("Count of DRAM Uncorrectable ECC Errors: %d\n", buf[11]);
		printf("Count of DRAM Correctable ECC Error Above Threshold Events: %d\n",
		    buf[12]);
		val = i2c_read(fd, 0, 0x64);
		printf("Last Catastrophic Save: 0x%02x <%s%s%s%s%s>\n", val,
		    val & 0x01 ? "SUCCESS,":"",
		    val & 0x02 ? "ERROR,":"",
		    val & 0x04 ? "REJECT,":"",
		    val & 0x10 ? "ABORT_SUCCESS,":"",
		    val & 0x20 ? "ABORT_ERROR":"");
		val = i2c_read(fd, 0, 0x80);
		printf("Last Catastrophic Save Info: 0x%02x <%s%s%s%s>\n", val,
		    val & 0x01 ? "NVM_Data_Valid,":"",
		    val & 0x02 ? "START_CSAVE,":"",
		    val & 0x04 ? "SAVE_n,":"",
		    val & 0x08 ? "RESET_n":"");
		val = i2c_read16(fd, 0, 0x84);
		printf("Last Catastrophic Save Fail Info: 0x%04x <%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s>\n",
		    val,
		    val & 0x0001 ? "VOLTAGE_REGULATOR_FAILED,":"",
		    val & 0x0002 ? "VDD_LOST,":"",
		    val & 0x0004 ? "VPP_LOST,":"",
		    val & 0x0008 ? "VTT_LOST,":"",
		    val & 0x0010 ? "DRAM_NOT_SELF_REFRESH,":"",
		    val & 0x0020 ? "CONTROLLER_HARDWARE_ERROR,":"",
		    val & 0x0040 ? "NVM_CONTROLLER_ERROR,":"",
		    val & 0x0080 ? "NVM_MEDIA_ERROR,":"",
		    val & 0x0100 ? "NOT_ENOUGH_ENERGY_FOR_CSAVE,":"",
		    val & 0x0200 ? "PARTIAL_DATA_SAVED,":"",
		    val & 0x0400 ? "SAVE_ABORT,":"",
		    val & 0x0800 ? "NO_SAVE_N,":"",
		    val & 0x1000 ? "INSUFFICIENT_SAVE_N,":"",
		    val & 0x2000 ? "NO_RESET_N,":"",
		    val & 0x4000 ? "SECURITY_ERROR":"");
		val = i2c_read(fd, 0, 0x66);
		printf("Last Restore: 0x%02x <%s%s%s%s>\n", val,
		    val & 0x01 ? "SUCCESS,":"",
		    val & 0x02 ? "ERROR,":"",
		    val & 0x10 ? "ABORT_SUCCESS,":"",
		    val & 0x20 ? "ABORT_ERROR":"");
		val = i2c_read(fd, 0, 0x88);
		printf("Last Restore Fail Info: 0x%02x <%s%s%s%s%s%s>\n", val,
		    val & 0x01 ? "INVALID_IMAGE,":"",
		    val & 0x02 ? "SECURITY_ERROR,":"",
		    val & 0x10 ? "DRAM_NOT_SELF_REFRESH,":"",
		    val & 0x20 ? "CONTROLLER_HARDWARE_ERROR,":"",
		    val & 0x40 ? "NVM_CONTROLLER_ERROR,":"",
		    val & 0x80 ? "NVM_MEDIA_ERROR":"");
		val = i2c_read(fd, 0, 0x68);
		printf("Last Erase: 0x%02x <%s%s%s%s>\n", val,
		    val & 0x01 ? "SUCCESS,":"",
		    val & 0x02 ? "ERROR,":"",
		    val & 0x10 ? "ABORT_SUCCESS,":"",
		    val & 0x20 ? "ABORT_ERROR":"");
		val = i2c_read(fd, 0, 0x6a);
		printf("Last Arm: 0x%02x <%s%s%s%s%s%s>\n", val,
		    val & 0x01 ? "SUCCESS,":"",
		    val & 0x02 ? "ERROR,":"",
		    val & 0x04 ? "SAVE_N_ARMED,":"",
		    val & 0x04 ? "RESET_N_ARMED,":"",
		    val & 0x10 ? "ABORT_SUCCESS,":"",
		    val & 0x20 ? "ABORT_ERROR":"");
	}

	bzero(&dsm, sizeof(dsm));
	dsm.guid = mguid;
	dsm.rev = 1;
	dsm.func = 12;
	bzero(&buf, sizeof(buf));
	dsm.out_size = sizeof(buf);
	dsm.out_buf = buf;
	if (ioctl(fd, IXNVDIMM_DSM, &dsm) == 0 && dsm.out_size >= 11) {
		printf("\nES Lifetime Percentage: %d%%\n", buf[4]);
		val = le16dec(&buf[5]);
		/* Workaround wrong units reported by Micron NVDIMMs. */
		if ((val & 0x1000) != 0)
			val = - (val & 0x0fff) / 16;
		else if (val >= 128)
			val = (val & 0x0fff) / 16;
		printf("ES Current Temperature: %d C\n", val);
		printf("Total Runtime: %d\n", le32dec(&buf[7]));
	}
}

static struct regs {
	int	page;
	int	off;
	int	size;
	const char *name;
} regs[] = {
	{ 0,	0x01,	1,	"STD_NUM_PAGES" },
	{ 0,	0x02,	1,	"VENDOR_START_PAGES" },
	{ 0,	0x03,	1,	"VENDOR_NUM_PAGES" },
	{ 0,	0x04,	1,	"HWREV" },
	{ 0,	0x06,	1,	"SPECREV" },
	{ 0,	0x07,	2,	"SLOT0_FWREV" },
	{ 0,	0x09,	2,	"SLOT1_FWREV" },
	{ 0,	0x0b,	1,	"SLOT0_SUBFWREV" },
	{ 0,	0x0c,	1,	"SLOT1_SUBFWREV" },

	{ 0,	0x10,	2,	"CAPABILITIES" },
	{ 0,	0x14,	1,	"ENERGY_SOURCE_POLICY" },
	{ 0,	0x15,	1,	"HOST_MAX_OPERATION_RETRY" },
	{ 0,	0x16,	1,	"CSAVE_TRIGGER_SUPPORT" },
	{ 0,	0x17,	1,	"EVENT_NOTIFICATION_SUPPORT" },
	{ 0,	0x18,	2,	"CSAVE_TIMEOUT" },
	{ 0,	0x1a,	2,	"PAGE_SWITCH_LATENCY" },
	{ 0,	0x1c,	2,	"RESTORE_TIMEOUT" },
	{ 0,	0x1e,	2,	"ERASE_TIMEOUT" },
	{ 0,	0x20,	2,	"ARM_TIMEOUT" },
	{ 0,	0x22,	2,	"FIRMWARE_OPS_TIMEOUT" },
	{ 0,	0x24,	1,	"ABORT_CMD_TIMEOUT" },
	{ 0,	0x27,	2,	"MAX_RUNTIME_POWER" },
	{ 0,	0x29,	2,	"CSAVE_POWER_REQ" },
	{ 0,	0x2b,	2,	"CSAVE_IDLE_POWER_REQ" },
	{ 0,	0x2d,	2,	"CSAVE_MIN_VOLT_REQ" },
	{ 0,	0x2f,	2,	"CSAVE_MAX_VOLT_REQ" },
	{ 0,	0x31,	1,	"VENDOR_LOG_PAGE_SIZE" },
	{ 0,	0x32,	1,	"REGION_BLOCK_SIZE" },
	{ 0,	0x33,	2,	"OPERATIONAL_UNIT_OPS_TIMEOUT" },
	{ 0,	0x35,	2,	"FACTORY_DEFAULT_TIMEOUT" },
	{ 0,	0x38,	2,	"MIN_OPERATING_TEMP" },
	{ 0,	0x3a,	2,	"MAX_OPERATING_TEMP" },

	{ 0,	0x40,	2,	"NVDIMM_MGT_CMD" },
	{ 0,	0x43,	1,	"NVDIMM_FUNC_CMD" },
	{ 0,	0x45,	1,	"ARM_CMD" },
	{ 0,	0x47,	1,	"SET_EVENT_NOTIFICATION_CMD" },
	{ 0,	0x49,	1,	"SET_ES_POLICY_CMD" },
	{ 0,	0x4a,	1,	"FIRMWARE_OPS_CMD" },
	{ 0,	0x4b,	1,	"OPERATIONAL_UNIT_OPS_CMD" },
	{ 0,	0x4c,	2,	"SECURITY_PROTOCOL_SPECIFIC" },
	{ 0,	0x4e,	1,	"SECURITY_PROTOCOL_TYPE" },

	{ 0,	0x60,	1,	"NVDIMM_READY" },
	{ 0,	0x61,	2,	"NVDIMM_CMD_STATUS" },
	{ 0,	0x64,	1,	"CSAVE_STATUS" },
	{ 0,	0x66,	1,	"RESTORE_STATUS" },
	{ 0,	0x68,	1,	"ERASE_STATUS" },
	{ 0,	0x6a,	1,	"ARM_STATUS" },
	{ 0,	0x6c,	1,	"FACTORY_DEFAULT_STATUS" },
	{ 0,	0x6e,	1,	"SET_EVENT_NOTIFICATION_STATUS" },
	{ 0,	0x70,	1,	"SET_ES_POLICY_STATUS" },
	{ 0,	0x71,	1,	"FIRMWARE_OPS_STATUS" },
	{ 0,	0x72,	1,	"OPERATIONAL_UNIT_OPS_STATUS" },
	{ 0,	0x73,	1,	"ERASE_FAIL_INFO" },
	{ 0,	0x74,	1,	"FACTORY_DEFAULT_STATUS_FAIL_INFO" },
	{ 0,	0x75,	1,	"FIRMWARE_OPS_FAIL_INFO" },
	{ 0,	0x76,	1,	"ARM_FAIL_INFO" },

	{ 0,	0x80,	1,	"CSAVE_INFO" },
	{ 0,	0x84,	2,	"CSAVE_FAIL_INFO" },
	{ 0,	0x88,	1,	"RESTORE_FAIL_INFO" },
	{ 0,	0x8f,	1,	"OPERATIONAL_UNIT_OPS_FAIL_INFO" },
	{ 0,	0x90,	1,	"NVM_LIFETIME_ERROR_THRESHOLD" },
	{ 0,	0x91,	1,	"ES_LIFETIME_ERROR_THRESHOLD" },
	{ 0,	0x94,	2,	"ES_TEMP_ERROR_HIGH_THRESHOLD" },
	{ 0,	0x96,	2,	"ES_TEMP_ERROR_LOW_THRESHOLD" },
	{ 0,	0x98,	1,	"NVM_LIFETIME_WARNING_THRESHOLD" },
	{ 0,	0x99,	1,	"ES_LIFETIME_WARNING_THRESHOLD" },
	{ 0,	0x9c,	2,	"ES_TEMP_WARNING_HIGH_THRESHOLD" },
	{ 0,	0x9e,	2,	"ES_TEMP_WARNING_LOW_THRESHOLD" },
	{ 0,	0xa0,	1,	"MODULE_HEALTH" },
	{ 0,	0xa1,	2,	"MODULE_HEALTH_STATUS" },
	{ 0,	0xa5,	1,	"ERROR_THRESHOLD_STATUS" },
	{ 0,	0xa7,	1,	"WARNING_THRESHOLD_STATUS" },
	{ 0,	0xa9,	1,	"AUTO_ES_HEALTH_FREQUENCY" },
	{ 0,	0xaa,	1,	"MODULE_OPS_CONFIG" },
	{ 0,	0xc0,	1,	"NVM_LIFETIME" },

	{ 1,	0x04,	1,	"ES_HWREV" },
	{ 1,	0x06,	2,	"ES_FWREV" },
	{ 1,	0x08,	2,	"SLOT0_ES_FWREV" },
	{ 1,	0x0a,	2,	"SLOT1_ES_FWREV" },
	{ 1,	0x10,	2,	"ES_CHARGE_TIMEOUT" },
	{ 1,	0x14,	1,	"ES_ATTRIBUTES" },
	{ 1,	0x15,	1,	"ES_TECH" },
	{ 1,	0x16,	2,	"MIN_ES_OPERATING_TEMP" },
	{ 1,	0x18,	2,	"MAX_ES_OPERATING_TEMP" },
	{ 1,	0x30,	1,	"ES_FUNC_CMD" },
	{ 1,	0x50,	1,	"ES_CMD_STATUS" },
	{ 1,	0x70,	1,	"ES_LIFETIME" },
	{ 1,	0x71,	2,	"ES_TEMP" },
	{ 1,	0x73,	2,	"ES_RUNTIME" },

	{ 2,	0x04,	2,	"LAST_CSAVE_DURATION" },
	{ 2,	0x06,	2,	"LAST_RESTORE_DURATION" },
	{ 2,	0x08,	2,	"LAST_ERASE_DURATION" },
	{ 2,	0x0a,	2,	"CSAVE_SUCCESS_COUNT" },
	{ 2,	0x0c,	2,	"RESTORE_SUCCESS_COUNT" },
	{ 2,	0x0e,	2,	"ERASE_SUCCESS_COUNT" },
	{ 2,	0x10,	2,	"POWER_CYCLE_COUNT" },
	{ 2,	0x12,	2,	"CSAVE_FAILURE_COUNT" },
	{ 2,	0x14,	2,	"RESTORE_FAILURE_COUNT" },
	{ 2,	0x16,	2,	"ERASE_FAILURE_COUNT" },
	{ 2,	0x18,	2,	"LAST_ARM_DURATION" },
	{ 2,	0x1a,	2,	"LAST_FACTORY_DEFAULT_DURATION" },
	{ 2,	0x1c,	2,	"LAST_FIRMWARE_OPS_DURATION" },
	{ 2,	0x1e,	2,	"LAST_OPERATIONAL_UNIT_OPS_DURATION" },

	{ 2,	0x20,	2,	"ARM_SUCCESS_COUNT" },
	{ 2,	0x22,	2,	"FACTORY_DEFAULT_SUCCESS_COUNT" },
	{ 2,	0x24,	2,	"FIRMWARE_SUCCESS_COUNT" },
	{ 2,	0x26,	2,	"OPERATIONAL_UNIT_SUCCESS_COUNT" },
	{ 2,	0x28,	2,	"ARM_FAILURE_COUNT" },
	{ 2,	0x2a,	2,	"FACTORY_DEFAULT_FAILURE_COUNT" },
	{ 2,	0x2c,	2,	"FIRMWARE_FAILURE_COUNT" },
	{ 2,	0x2e,	2,	"OPERATIONAL_UNIT_FAILURE_COUNT" },

	{ 2,	0x60,	2,	"INJECT_OPS_FAILURES" },
	{ 2,	0x64,	1,	"INJECT_ES_FAILURES" },
	{ 2,	0x65,	1,	"INJECT_FW_FAILURES" },
	{ 2,	0x67,	1,	"INJECT_BAD_BLOCK_CAP" },
	{ 2,	0x78,	1,	"INJECT_ERROR_TYPE" },
	{ 2,	0x80,	1,	"DRAM_ECC_ERROR_COUNT" },
	{ 2,	0x81,	1,	"DRAM_THRESHOLD_ECC_COUNT" },
	{ 2,	0x82,	1,	"HOST_MANAGED_ES_ATTRIBUTES" },
	{ 2,	0x83,	1,	"HOST_CSAVE_FAIL" },
	{ 2,	0x84,	2,	"HOST_CSAVE_WORKFLOW_FAILURE_COUNT" },

	{ 3,	0x04,	1,	"TYPED_BLOCK_DATA" },
	{ 3,	0x05,	2,	"REGION_ID" },
	{ 3,	0x07,	1,	"BLOCK_ID" },
	{ 3,	0x08,	3,	"TYPED_BLOCK_DATA_SIZE" },
	{ 3,	0x0c,	2,	"OPERATIONAL_UNIT_ID" },
	{ 3,	0x10,	3,	"OPERATIONAL_UNIT_SIZE" },
	{ 3,	0x14,	2,	"OPERATIONAL_UNIT_CRC" },
	{ 3,	0x40,	2,	"FW_REGION_CRC" },
	{ 3,	0x42,	1,	"FW_SLOT_INFO" },

	{ 3,	0x80,	32,	"TYPED_BLOCK_DATA_BYTE" },

	{ 0,	0,	0,	NULL },
};

static int
reg_parse(const char *name, int *page, int *off, int *size)
{
	int i, l, o = -1;

	for (i = 0; regs[i].name != NULL; i++) {
		l = strlen(regs[i].name);
		if (strncmp(name, regs[i].name, l) != 0)
			continue;
		if (name[l] == 0)
			break;
		if (name[l] - '0' < regs[i].size) {
			o = name[l] - '0';
			break;
		}
	}
	if (regs[i].name == NULL)
		return (0);
	*page = regs[i].page;
	if (o >= 0) {
		*off = regs[i].off + o;
		*size = 1;
	} else {
		*off = regs[i].off;
		*size = regs[i].size;
	}
	return (1);
}

static void
usage(const char *progname)
{

	fprintf(stderr, "usage: %s {nvdimm}\n", progname);
	fprintf(stderr, "       %s -F {nvdimm}\n", progname);
	fprintf(stderr, "       %s -d {nvdimm}\n", progname);
	fprintf(stderr, "       %s -f {firmware} {nvdimm}\n", progname);
	fprintf(stderr, "       %s -r [-h] {nvdimm} [{page} [{off}] | {reg}]\n", progname);
	fprintf(stderr, "       %s -w [-h] {nvdimm} ({page} {off} | {reg}) {val}\n", progname);
	exit(1);
}

int
main(int argc, char *argv[])
{
	struct ixnvdimm_info info;
	const char *progname, *devname, *f = NULL;
	char *e;
	struct ixnvdimm_dsm dsm;
	int F = 0, b, c, d = 0, i, fd, h = 0, r = 0, w = 0;
	int page = -1, off = -1, size = 1;
	uint32_t status, funcs, val;
	uint8_t	buf[4];

	progname = argv[0];
	uuid_from_string(MGUID, &mguid, &status);
	assert(status == uuid_s_ok);

	while ((c = getopt(argc, argv, "Fdf:hrw")) != -1) {
		switch (c) {
		case 'd':
			d = 1;
			break;
		case 'F':
			F = 1;
			break;
		case 'f':
			f = optarg;
			break;
		case 'r':
			r = 1;
			break;
		case 'w':
			w = 1;
			break;
		case 'h':
			h = 1;
			size = 2;
			break;
		default:
			usage(progname);
			/* NOTREACHED */
		}
	}
	argc -= optind;
	argv += optind;

	if (argc < 1 || (d + r + w + (f != NULL) > 1) || (h && !r && !w))
		usage(progname);

	devname = argv[0];
	argc--;
	argv++;
	if ((fd = open(devname, O_RDONLY)) < 0)
		err(EX_NOINPUT, "Can't open %s", devname);

	if (ioctl(fd, IXNVDIMM_INFO, &info))
		err(EX_NOINPUT, "Can't get info from %s", devname);

	bzero(&dsm, sizeof(dsm));
	dsm.guid = mguid;
	dsm.rev = 1;
	dsm.func = 0;
	bzero(&buf, sizeof(buf));
	dsm.out_size = sizeof(buf);
	dsm.out_buf = buf;
	if (ioctl(fd, IXNVDIMM_DSM, &dsm) || dsm.out_size != 4)
		err(EX_NOINPUT, "Can't call DSM on %s", devname);
	funcs = le32dec(&buf[0]);
	if (~funcs & 0x18000001) {
		errx(EX_UNAVAILABLE, "Required Microsoft DSM functions "
		    "are not supported: 0x%08x\n", funcs);
	}

	if (d) {
		int snp, vsp, vnp;

		snp = i2c_read(fd, 0, 1);
		vsp = i2c_read(fd, 0, 2);
		vnp = i2c_read(fd, 0, 3);
		for (page = 0; page < snp; page++) {
			printf("Page 0x%02x:\n", page);
			for (off = 0; off < 256; off++) {
				printf(" %02x", i2c_read(fd, page, off));
				if ((off % 16) == 15)
					printf("\n");
			}
		}
		for (page = vsp; page < vsp+vnp; page++) {
			printf("Page 0x%02x:\n", page);
			for (off = 0; off < 256; off++) {
				printf(" %02x", i2c_read(fd, page, off));
				if ((off % 16) == 15)
					printf("\n");
			}
		}
		goto done;
	}
	if (F) {
		if (~funcs & 0x08200001) {
			errx(EX_UNAVAILABLE, "Required Microsoft DSM functions "
			    "are not supported: 0x%08x\n", funcs);
		}
		factory_default(fd);
		goto done;
	}
	if (f) {
		if (~funcs & 0x1bc00001) {
			errx(EX_UNAVAILABLE, "Required Microsoft DSM functions "
			    "are not supported: 0x%08x\n", funcs);
		}
		firmware_update(fd, &info, f);
		goto done;
	}
	if (r || w) {
		if (argc < 1 && w) {
			usage(progname);
		} else if (argc < 1) {
			;
		} else if (reg_parse(argv[0], &page, &off, &size)) {
			argc--;
			argv++;
		} else {
			if (argc < 1)
				usage(progname);
			page = strtol(argv[0], &e, 0);
			if (e[0] != 0)
				usage(progname);
			argc--;
			argv++;
			if (argc >= 1) {
				off = strtol(argv[0], &e, 0);
				if (e[0] != 0)
					usage(progname);
				argc--;
				argv++;
			}
		}
	}
	if (r) {
		if (off >= 0) {
			for (b = 0; b < size; b++)
				printf("%02x", i2c_read(fd, page, off + size - b - 1));
			printf("\n");
			goto done;
		}
		for (i = 0; regs[i].name != NULL; i++) {
			if (page >= 0 && regs[i].page != page)
				continue;
			printf("%-36s ", regs[i].name);
			for (b = 0; b < regs[i].size; b++)
				printf("%02x", i2c_read(fd, regs[i].page, regs[i].off + regs[i].size - b - 1));
			printf("\n");
		}
		goto done;
	}
	if (w) {
		if (argc < 1 || size > 3 || page < 0 || off < 0)
			usage(progname);
		val = strtol(argv[0], &e, 0);
		if (e[0] != 0 || val > (size == 3 ? 16777216 : (size == 2 ? 65535 : 255)))
			usage(progname);
		if (size == 3) {
			i2c_write16(fd, page, off, val);
			i2c_write(fd, page, off + 2, val >> 16);
		} else if (size == 2)
			i2c_write16(fd, page, off, val);
		else
			i2c_write(fd, page, off, val);
		goto done;
	}

	health(fd, &info);
done:
	close(fd);
	exit(0);
}
