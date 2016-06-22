

#include <sys/types.h>
#include <machine/vmm.h>
#include <sys/ioctl.h>

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

#include <vmmapi.h>
#include <net/ethernet.h>

#include "pci_emul.h"
#include "inout.h"
#include "mevent.h"
#include "if_edreg.h"

/*
 * NE2000 Debug Log
 */
#define	DEBUG_NE2000				1
#if DEBUG_NE2000 == 1
static FILE *dbg;
#define DPRINTF(fmt, arg...)						\
do {fprintf(dbg, "%s-%d: " fmt "\n", __func__, __LINE__, ##arg);	\
fflush(dbg); } while (0)
#else
#define DPRINTF(fmt, arg...)
#endif

/*
 * NE2000 defines
 */
#define NE2000_P0	0
#define NE2000_P1	1
#define NE2000_P2	2
#define NE2000_P3	3
#define NE2000_P0_RO	4

#define NE2000_MEM_SIZE		32768
#define NE2000_PAGE_SIZE	0x10
#define NE2000_PAGE_COUNT	5

#define NE2000_BAR_NIC		0
#define NE2000_BAR_ASIC		1

#define LPC_NE2000_NUM		2

#define ED_RTL80X9_CONFIG2	0x05
#define ED_RTL80X9_CF2_10_T		0x40
#define ED_RTL80X9_CONFIG3	0x06
#define ED_RTL80X9_CF3_FUDUP		0x40
#define ED_RTL80X9_80X9ID0	0x0a
#define ED_RTL80X9_ID0			0x50
#define ED_RTL80X9_80X9ID1	0x0b
#define ED_RTL8029_ID1			0x43

#define MAX_INPUT_LEN		32

#define ETHER_MAX_FRAME_LEN	(ETHER_MAX_LEN - ETHER_CRC_LEN)
#define ETHER_MIN_FRAME_LEN	(ETHER_MIN_LEN - ETHER_CRC_LEN)

typedef void (*ne2000_intr_func_t)(void *arg);

/*
 * NE2000 data structures
 */
struct ne2000_softc {
	/* NIC registers */
	uint8_t nic_regs[NE2000_PAGE_COUNT][NE2000_PAGE_SIZE];

	/* ASIC registers */
	uint8_t reset;

	/* State Variables */
	int tapfd;
	uint8_t lintr;
	uint8_t page;
	uint8_t remote_read;
	uint8_t remote_write;

	/* NIC memory is 32k */
	uint8_t ram[NE2000_MEM_SIZE];
	uint8_t rcv_buf[ETHER_MAX_FRAME_LEN];

	/*
	 * one single mutex used to lock the reception flow with
	 * the read and write register flows
	 */
	pthread_mutex_t mtx;

	/* Interrupts callbacks (PCI or LPC) */
	ne2000_intr_func_t intr_assert;
	ne2000_intr_func_t intr_deassert;

	/* The argument used by the interrupts callbacks */
	void *intr_arg;
};

/*
 * NE2000 module function declarations
 */
static void
ne2000_set_reg_by_offset(struct ne2000_softc *sc, uint8_t page,
		uint8_t offset, uint8_t value);
static uint8_t
ne2000_get_reg_by_offset(struct ne2000_softc *sc, uint8_t page,
		uint8_t offset);
static void
ne2000_set_field_by_offset(struct ne2000_softc *sc, uint8_t page,
		uint8_t offset, uint8_t mask, uint8_t value);

static struct ne2000_softc *
ne2000_init(ne2000_intr_func_t intr_assert, ne2000_intr_func_t intr_deassert,
		void *intr_arg, const char *opts);

static void
ne2000_update_intr(struct ne2000_softc *sc);

static uint16_t
ne2000_read(struct ne2000_softc *sc, uint8_t offset, int size);
static int
ne2000_write(struct ne2000_softc *sc, uint8_t offset, uint16_t value, int size);

static uint8_t
ne2000_read_nic_locked(struct ne2000_softc *sc, uint8_t offset);
static uint16_t
ne2000_read_asic_locked(struct ne2000_softc *sc, uint8_t offset);

static int
ne2000_write_nic_locked(struct ne2000_softc *sc, uint8_t offset, uint8_t value);
static int
ne2000_write_asic_locked(struct ne2000_softc *sc, uint8_t offset, uint16_t value);

static int
ne2000_emul_reg_cr(struct ne2000_softc *sc, uint8_t value);
static int
ne2000_emul_reg_page0(struct ne2000_softc *sc, uint8_t offset,
		uint8_t value);
static int
ne2000_emul_reg_page1(struct ne2000_softc *sc, uint8_t offset,
		uint8_t value);

static int ne2000_reset_board(void);
static int ne2000_software_reset(struct ne2000_softc *sc);

static int
ne2000_tap_init(struct ne2000_softc *sc, char *tap_name);
static int
ne2000_tap_tx(struct ne2000_softc *sc, uint8_t tpsr, uint16_t tbcr);
static int
ne2000_tap_rx(struct ne2000_softc *sc);
static void
ne2000_tap_callback(int fd, enum ev_type type, void *param);

static int
ne2000_receive_ring_is_valid(struct ne2000_softc *sc);
static int
ne2000_receive_ring_is_full(struct ne2000_softc *sc);

static int
ne2000_ether_frame_is_valid(struct ne2000_softc *sc);

static uint32_t
ne2000_ether_crc32_be(const uint8_t *buf, size_t len);

static int
ne2000_parse_input(const char *opts, char *tap_name, uint8_t *mac);

/*
 * PCI NE2000 function declarations
 */
static int
pci_ne2000_init(struct vmctx *ctx, struct pci_devinst *pi, char *opts);
static void
pci_ne2000_write(struct vmctx *ctx, int vcpu, struct pci_devinst *pi,
		int baridx, uint64_t offset, int size, uint64_t value);
static uint64_t
pci_ne2000_read(struct vmctx *ctx, int vcpu, struct pci_devinst *pi,
		int baridx, uint64_t offset, int size);

static void
pci_ne2000_intr_assert(void *arg);
static void
pci_ne2000_intr_deassert(void *arg);

/*
 * LPC NE2000 function declarations
 */
int
lpc_ne2000_init(struct vmctx *lpc_ctx, uint8_t unit, const char *opts);
static int
lpc_ne2000_io_handler(struct vmctx *ctx, int vcpu, int in, int port, int bytes,
		uint32_t *eax, void *arg);

static void
lpc_ne2000_intr_assert(void *arg);
static void
lpc_ne2000_intr_deassert(void *arg);

/*
 * NE2000 global data
 */
static struct lpc_ne2000_softc {
	struct ne2000_softc *ne2000_sc;
	struct vmctx *lpc_ctx;
	int base_addr;
	int irq;
	const char *name;
} lpc_ne2000_sc[LPC_NE2000_NUM] = {
	{NULL, NULL, 0x310, 10, "ne2k0"},
	{NULL, NULL, 0x330, 11, "ne2k1"}
};

struct pci_devemu pci_de_ne2000_net = {
	.pe_emu         = "ne2k",
	.pe_init        = pci_ne2000_init,
	.pe_barwrite    = pci_ne2000_write,
	.pe_barread     = pci_ne2000_read
};
PCI_EMUL_SET(pci_de_ne2000_net);

/*
 * NE2000 module function definitions
 */
static void
ne2000_set_reg_by_offset(struct ne2000_softc *sc, uint8_t page,
		uint8_t offset, uint8_t value)
{
	assert(page < NE2000_PAGE_COUNT);
	assert(offset < NE2000_PAGE_SIZE);

	sc->nic_regs[page][offset] = value;
}

static uint8_t
ne2000_get_reg_by_offset(struct ne2000_softc *sc, uint8_t page,
		uint8_t offset)
{
	assert(page < NE2000_PAGE_COUNT);
	assert(offset < NE2000_PAGE_SIZE);

	return sc->nic_regs[page][offset];
}

static void
ne2000_set_field_by_offset(struct ne2000_softc *sc, uint8_t page,
		uint8_t offset, uint8_t mask, uint8_t value)
{
	uint8_t reg_value = 0;

	reg_value = ne2000_get_reg_by_offset(sc, page, offset);

	reg_value &= ~mask;
	reg_value |= value;

	ne2000_set_reg_by_offset(sc, page, offset, reg_value);
}

static int
ne2000_tap_init(struct ne2000_softc *sc, char *tap_name)
{
	int err;
	int opt = 1;
	struct mevent *evf_read = NULL;

	assert(tap_name != NULL);

	sc->tapfd = open(tap_name, O_RDWR);
	assert(sc->tapfd != -1);

	err = ioctl(sc->tapfd, FIONBIO, &opt);
	assert(err >= 0);

	evf_read = mevent_add(sc->tapfd, EVF_READ, ne2000_tap_callback, sc);
	assert(evf_read != NULL);

	DPRINTF("Tap interface: fd: %d, opt: %d", sc->tapfd, opt);

	return 0;
}

static int
ne2000_tap_tx(struct ne2000_softc *sc, uint8_t tpsr, uint16_t tbcr)
{
	ssize_t write_len;

	write_len = write(sc->tapfd, sc->ram + tpsr * ED_PAGE_SIZE, tbcr);
	assert(write_len > 0 && write_len == tbcr);

	DPRINTF("Transmit Packet: from %d address of %d bytes",
			tpsr * ED_PAGE_SIZE, tbcr);

	ne2000_set_field_by_offset(sc, NE2000_P0, ED_P0_ISR,
			ED_ISR_PTX, ED_ISR_PTX);
	ne2000_update_intr(sc);

	return 0;
}

static int
ne2000_tap_rx(struct ne2000_softc *sc)
{
	uint8_t pstart = 0;
	uint8_t pstop = 0;
	uint8_t curr = 0;
	uint8_t next_curr = 0;
	uint8_t psize = 0;

	uint32_t size = 0;
	uint32_t tmp_size = 0;
	uint32_t index = 0;
	uint32_t start = 0;
	uint32_t stop = 0;

	ssize_t read_len = 0;

	struct ed_ring *ed_hdr = NULL;

	memset(sc->rcv_buf, 0, ETHER_MAX_FRAME_LEN);
	read_len = read(sc->tapfd, sc->rcv_buf, ETHER_MAX_FRAME_LEN);
	assert(read_len > 0);

	DPRINTF("Receive Packet: from tap interface of %zd bytes", read_len);

	/* clear the Receiver Status Register */
	ne2000_set_reg_by_offset(sc, NE2000_P0_RO, ED_P0_RSR, 0x00);

	if (!ne2000_receive_ring_is_valid(sc)) {
		DPRINTF("Drop the packet: the ring is not valid");
		return 0;
	}

	if (!ne2000_ether_frame_is_valid(sc)) {
		DPRINTF("Drop the packet: the ether frame did not match");
		return 0;
	}

	if (ne2000_receive_ring_is_full(sc)) {
		DPRINTF("Drop the packet: the ring is full");
		return 0;
	}

	ne2000_set_field_by_offset(sc, NE2000_P0_RO, ED_P0_RSR,
			ED_RSR_PRX, ED_RSR_PRX);

	size = read_len < ETHER_MIN_FRAME_LEN ? ETHER_MIN_FRAME_LEN : read_len;

	/* psize is the number of pages used by the frame and ne2000 header */
	psize = (size + sizeof(struct ed_ring) + (ED_PAGE_SIZE - 1)) /
		ED_PAGE_SIZE;
	assert(psize <= 6);

	pstart = ne2000_get_reg_by_offset(sc, NE2000_P0, ED_P0_PSTART);
	pstop = ne2000_get_reg_by_offset(sc, NE2000_P0, ED_P0_PSTOP);
	curr = ne2000_get_reg_by_offset(sc, NE2000_P1, ED_P1_CURR);

	start = pstart * ED_PAGE_SIZE;
	stop = pstop * ED_PAGE_SIZE;
	index = curr * ED_PAGE_SIZE;

	next_curr = curr + psize;
	if (next_curr >= pstop)
		next_curr -= (pstop - pstart);

	DPRINTF("Receive Packet: size: %d psize: %d next_curr: %d index: %d",
			size, psize, next_curr, index);

	ed_hdr = (struct ed_ring *)(sc->ram + index);
	ed_hdr->rsr = ne2000_get_reg_by_offset(sc, NE2000_P0_RO, ED_P0_RSR);
	ed_hdr->next_packet = next_curr;
	ed_hdr->count = size + sizeof(struct ed_ring);

	index += sizeof(struct ed_ring);

	if (index + size >= stop) {
		tmp_size = stop - index;
		memcpy(sc->ram + index, sc->rcv_buf, tmp_size);
		index = start;
		size -= tmp_size;
	}
	memcpy(sc->ram + index, sc->rcv_buf + tmp_size, size);

	ne2000_set_reg_by_offset(sc, NE2000_P1, ED_P1_CURR, next_curr);
	ne2000_set_field_by_offset(sc, NE2000_P0, ED_P0_ISR,
			ED_ISR_PRX, ED_ISR_PRX);

	ne2000_update_intr(sc);

	return 0;
}

static void
ne2000_tap_callback(int fd, enum ev_type type, void *param)
{
	int err;
	struct ne2000_softc *sc = (struct ne2000_softc *)param;
	assert(sc != NULL);

	err = pthread_mutex_lock(&sc->mtx);
	assert(err == 0);

	err = ne2000_tap_rx(sc);
	assert(err == 0);

	err = pthread_mutex_unlock(&sc->mtx);
	assert(err == 0);

	return;
}

static int
ne2000_receive_ring_is_valid(struct ne2000_softc *sc)
{
	uint8_t cr = 0;

	uint8_t pstart = 0;
	uint8_t pstop = 0;

	uint8_t curr = 0;
	uint8_t bnry = 0;

	cr = ne2000_get_reg_by_offset(sc, NE2000_P0, ED_P0_CR);
	if (cr & ED_CR_STP) {
		DPRINTF("Ring is not valid: the NIC is Stopped");
		return 0;
	}

	pstart = ne2000_get_reg_by_offset(sc, NE2000_P0, ED_P0_PSTART);
	pstop = ne2000_get_reg_by_offset(sc, NE2000_P0, ED_P0_PSTOP);

	curr = ne2000_get_reg_by_offset(sc, NE2000_P1, ED_P1_CURR);
	bnry = ne2000_get_reg_by_offset(sc, NE2000_P0, ED_P0_BNRY);

	if (pstart == 0 || pstop == 0)
		return 0;
	if (curr < pstart || curr >= pstop)
		return 0;
	if (bnry < pstart || bnry >= pstop)
		return 0;

	return 1;
}

static int
ne2000_receive_ring_is_full(struct ne2000_softc *sc)
{
	uint32_t avail = 0;
	uint32_t start = 0;
	uint32_t stop = 0;
	uint32_t index = 0;
	uint32_t boundary = 0;

	uint8_t pstart = 0;
	uint8_t pstop = 0;
	uint8_t curr = 0;
	uint8_t bnry = 0;

	assert(ne2000_receive_ring_is_valid(sc));

	pstart = ne2000_get_reg_by_offset(sc, NE2000_P0, ED_P0_PSTART);
	pstop = ne2000_get_reg_by_offset(sc, NE2000_P0, ED_P0_PSTOP);
	curr = ne2000_get_reg_by_offset(sc, NE2000_P1, ED_P1_CURR);
	bnry = ne2000_get_reg_by_offset(sc, NE2000_P0, ED_P0_BNRY);

	index = curr * ED_PAGE_SIZE;
	boundary = bnry * ED_PAGE_SIZE;
	start = pstart * ED_PAGE_SIZE;
	stop = pstop * ED_PAGE_SIZE;

	if (index < boundary)
		avail = boundary - index;
	else
		avail = (stop - start) - (index - boundary);

	if (avail < (ETHER_MAX_FRAME_LEN + sizeof(struct ed_ring)))
		return 1;

	return 0;
}

static int
ne2000_ether_frame_is_valid(struct ne2000_softc *sc)
{
	uint8_t key = 0;
	uint8_t mar_offset = 0;
	uint8_t mar_reg = 0;
	uint8_t rcr = 0;
	uint8_t broadcast_addr[] = {[0 ... (ETHER_ADDR_LEN - 1)] = 0xff};

	rcr = ne2000_get_reg_by_offset(sc, NE2000_P0, ED_P0_RCR);

	if (rcr & ED_RCR_MON) {
		DPRINTF("The NIC card is in Monitor Mode");
		return 0;
	}

	/* is valid if the destination MAC matches the NIC's address */
	if (sc->rcv_buf[0] == sc->ram[0] &&
	    sc->rcv_buf[1] == sc->ram[2] &&
	    sc->rcv_buf[2] == sc->ram[4] &&
	    sc->rcv_buf[3] == sc->ram[6] &&
	    sc->rcv_buf[4] == sc->ram[8] &&
	    sc->rcv_buf[5] == sc->ram[10])
		return 1;

	/* is valid if the destination MAC is the broadcast address */
	if (rcr & ED_RCR_AB) {
		if (memcmp(sc->rcv_buf, broadcast_addr, ETHER_ADDR_LEN) == 0) {
			ne2000_set_field_by_offset(sc, NE2000_P0_RO, ED_P0_RSR,
					ED_RSR_PHY, ED_RSR_PHY);
			return 1;
		}
	}

	/* is valid if the destination MAC represents a multicast address group */
	if ((rcr & ED_RCR_AM) && (sc->rcv_buf[0] & 0x01)) {
		key = ne2000_ether_crc32_be(sc->rcv_buf, ETHER_ADDR_LEN) >> 26;

		mar_offset = ED_P1_MAR0 + (key >> 3);
		mar_reg = ne2000_get_reg_by_offset(sc, NE2000_P1, mar_offset);

		if (mar_reg & (1 << (key & 7))) {
			ne2000_set_field_by_offset(sc, NE2000_P0_RO, ED_P0_RSR,
					ED_RSR_PHY, ED_RSR_PHY);
			return 1;
		}
	}

	/*
	 * if the physical destination address does not match the station's
	 * address, accept the frame only in the Promiscuous Physical mode
	 */
	if (rcr & ED_RCR_PRO)
		return 1;

	return 0;
}

/*
 * This function is a copy of the ether_crc32_be function from the net kernel
 * module in the FreeBSD tree sources
 */
static uint32_t
ne2000_ether_crc32_be(const uint8_t *buf, size_t len)
{
	size_t i;
	uint32_t crc, carry;
	int bit;
	uint8_t data;

	crc = 0xffffffff;	/* initial value */

	for (i = 0; i < len; i++) {
		for (data = *buf++, bit = 0; bit < 8; bit++, data >>= 1) {
			carry = ((crc & 0x80000000) ? 1 : 0) ^ (data & 0x01);
			crc <<= 1;
			if (carry)
				crc = (crc ^ ETHER_CRC_POLY_BE) | carry;
		}
	}

	return (crc);
}

static int
ne2000_parse_input(const char *opts, char *tap_name, uint8_t *mac)
{
	uint8_t len = 0;
	char cp_opts[MAX_INPUT_LEN];
	char *delim = NULL;
	char *p_mac = NULL;
	struct ether_addr *addr = NULL;

	if (opts == NULL)
		return 1;

	len = strlen(opts);
	if (len >= MAX_INPUT_LEN) {
		DPRINTF("The input len should be less than %d", MAX_INPUT_LEN);
		return 1;
	}

	strncpy(cp_opts, opts, MAX_INPUT_LEN);

	/* search for mac address in the input string */
	delim = strchr(cp_opts, ',');

	if (delim != NULL) {
		/* mark the end of the tap name */
		*delim = 0;

		/* point to the start of the mac address */
		p_mac = delim + 1;

		/* parse the mac addres */
		addr = ether_aton(p_mac);

		/* if the mac address is valid overwrite the default one */
		if (addr != NULL)
			memcpy(mac, addr, ETHER_ADDR_LEN);
		else
			DPRINTF("Invalid mac");
	}

	/* copy the tap name */
	strcpy(tap_name, cp_opts);

	return 0;
}

static int
ne2000_write_nic_locked(struct ne2000_softc *sc, uint8_t offset, uint8_t value)
{
	int err;

	/* the CR register is located always at offset = 0 */
	if (offset == ED_P0_CR || offset == ED_P1_CR || offset == ED_P2_CR)
		return ne2000_emul_reg_cr(sc, value);

	if (!(sc->page == NE2000_P0 && offset == ED_P0_ISR))
		ne2000_set_reg_by_offset(sc, sc->page, offset, value);

	if (sc->page == NE2000_P0) {
		err = ne2000_emul_reg_page0(sc, offset, value);
		assert(err == 0);
	} else if (sc->page == NE2000_P1) {
		err = ne2000_emul_reg_page1(sc, offset, value);
		assert(err == 0);
	} else if (sc->page == NE2000_P3)
		DPRINTF("The ED driver wrote a register from PAGE3");
	else
		assert(0);

	return 0;
}

static int
ne2000_write_asic_locked(struct ne2000_softc *sc, uint8_t offset, uint16_t value)
{
	uint8_t dcr = 0;
	uint8_t rbcr0 = 0;
	uint8_t rbcr1 = 0;
	uint8_t rsar0 = 0;
	uint8_t rsar1 = 0;

	uint16_t rbcr = 0;
	uint16_t rsar = 0;

	uint8_t pstart = 0;
	uint8_t pstop = 0;

	switch (offset) {
	case ED_NOVELL_RESET:
		sc->reset = value;
		break;
	case ED_NOVELL_DATA:
		/* Write the value word into the NIC's RAM using the Remote DMA
		 * protocol
		 */
		dcr = ne2000_get_reg_by_offset(sc, NE2000_P0, ED_P0_DCR);
		if ((dcr & ED_DCR_WTS) != ED_DCR_WTS) {
			DPRINTF("The NE2000 card is working only in Word mode");
			sc->remote_write = 0;
			break;
		}

		assert(sc->remote_write);

		rbcr0 = ne2000_get_reg_by_offset(sc, NE2000_P0, ED_P0_RBCR0);
		rbcr1 = ne2000_get_reg_by_offset(sc, NE2000_P0, ED_P0_RBCR1);
		rbcr = rbcr0 | (rbcr1 << 8);

		rsar0 = ne2000_get_reg_by_offset(sc, NE2000_P0, ED_P0_RSAR0);
		rsar1 = ne2000_get_reg_by_offset(sc, NE2000_P0, ED_P0_RSAR1);
		rsar = rsar0 | (rsar1 << 8);

		assert(rsar < NE2000_MEM_SIZE);

		if (ne2000_receive_ring_is_valid(sc)) {
			pstart = ne2000_get_reg_by_offset(sc, NE2000_P0,
					ED_P0_PSTART);
			pstop = ne2000_get_reg_by_offset(sc, NE2000_P0,
					ED_P0_PSTOP);
			assert(rsar + 1 < pstart * ED_PAGE_SIZE ||
					rsar >= pstop * ED_PAGE_SIZE);
		}

		/* copy the value in LOW - HIGH order */
		sc->ram[rsar]     = value;
		sc->ram[rsar + 1] = value >> 8;

		rsar += 2;
		rbcr -= 2;

		if (rbcr == 0) {
			sc->remote_write = 0;
			ne2000_set_field_by_offset(sc, NE2000_P0, ED_P0_ISR,
					ED_ISR_RDC, ED_ISR_RDC);
		}

		ne2000_set_reg_by_offset(sc, NE2000_P0, ED_P0_RSAR0, rsar);
		ne2000_set_reg_by_offset(sc, NE2000_P0, ED_P0_RSAR1, rsar >> 8);

		ne2000_set_reg_by_offset(sc, NE2000_P0, ED_P0_RBCR0, rbcr);
		ne2000_set_reg_by_offset(sc, NE2000_P0, ED_P0_RBCR1, rbcr >> 8);

		break;
	default:
		assert(0);
	}

	return 0;
}

static int
ne2000_emul_reg_cr(struct ne2000_softc *sc, uint8_t value)
{
	int err;
	uint8_t rbcr0 = 0;
	uint8_t rbcr1 = 0;
	uint8_t rsar0 = 0;
	uint8_t rsar1 = 0;

	uint16_t rbcr = 0;
	uint16_t rsar = 0;

	uint8_t tbcr0 = 0;
	uint8_t tbcr1 = 0;

	uint16_t tbcr = 0;
	uint8_t tpsr = 0;

	uint8_t old_cr = 0;

	/* check is not selected a new page */
	switch (value & (ED_CR_PS0 | ED_CR_PS1)) {
	case ED_CR_PAGE_0:
		sc->page = NE2000_P0;
		break;
	case ED_CR_PAGE_1:
		sc->page = NE2000_P1;
		break;
	case ED_CR_PAGE_2:
		DPRINTF("The ED driver seleted PAGE2");
		assert(0);
		break;
	case ED_CR_PAGE_3:
		sc->page = NE2000_P3;
		break;
	}

	old_cr = ne2000_get_reg_by_offset(sc, NE2000_P0, ED_P0_CR);

	/* emulate any command specified in the CR register */
	if (value & ED_CR_STA) {
		if ((old_cr & ED_CR_STA) == 0) {
			ne2000_set_field_by_offset(sc, NE2000_P0, ED_P0_ISR,
					ED_ISR_RST, 0);
		}
	}
	if (value & ED_CR_RD2)
		assert(!(sc->remote_read || sc->remote_write));
	if (value & (ED_CR_RD0 | ED_CR_RD1)) {
		assert(value & ED_CR_STA);

		if (value & ED_CR_RD0)
			sc->remote_read = 1;
		else
			sc->remote_write = 1;

		rbcr0 = ne2000_get_reg_by_offset(sc, NE2000_P0,
				ED_P0_RBCR0);
		rbcr1 = ne2000_get_reg_by_offset(sc, NE2000_P0,
				ED_P0_RBCR1);
		rbcr = rbcr0 | (rbcr1 << 8);

		rsar0 = ne2000_get_reg_by_offset(sc, NE2000_P0,
				ED_P0_RSAR0);
		rsar1 = ne2000_get_reg_by_offset(sc, NE2000_P0,
				ED_P0_RSAR1);
		rsar = rsar0 | (rsar1 << 8);

		DPRINTF("Remote DMA %s: from %d address of %d bytes",
			sc->remote_read ? "read" : "write", rsar, rbcr);
	}
	if (value & ED_CR_TXP) {
		assert(!(sc->remote_read || sc->remote_write));
		assert(value & ED_CR_STA);

		tpsr = ne2000_get_reg_by_offset(sc, NE2000_P0,
				ED_P0_TPSR);
		tbcr0 = ne2000_get_reg_by_offset(sc, NE2000_P0,
				ED_P0_TBCR0);
		tbcr1 = ne2000_get_reg_by_offset(sc, NE2000_P0,
				ED_P0_TBCR1);
		tbcr = tbcr0 | (tbcr1 << 8);

		err = ne2000_tap_tx(sc, tpsr, tbcr);
		assert(err == 0);
	}

	/* store the value in the CR register located in the Page0 */
	ne2000_set_reg_by_offset(sc, NE2000_P0, ED_P0_CR, value);

	return 0;
}

static int
ne2000_emul_reg_page0(struct ne2000_softc *sc, uint8_t offset,
		uint8_t value)
{
	uint8_t pstart = 0;
	uint8_t pstop = 0;

	assert(offset != ED_P0_CR);

	switch (offset) {
	case ED_P0_PSTART:
		DPRINTF("Page Start Register: %d", value);
		assert(value > 0 && value * ED_PAGE_SIZE < NE2000_MEM_SIZE);
		break;
	case ED_P0_PSTOP:
		DPRINTF("Page Stop Register: %d", value);
		assert(value > 0 && value * ED_PAGE_SIZE <= NE2000_MEM_SIZE);
		break;
	case ED_P0_BNRY:
		DPRINTF("Boundary Register: %d", value);
		pstart = ne2000_get_reg_by_offset(sc, NE2000_P0, ED_P0_PSTART);
		pstop = ne2000_get_reg_by_offset(sc, NE2000_P0, ED_P0_PSTOP);
		assert(value >= pstart && value < pstop);
		break;
	case ED_P0_ISR:
		DPRINTF("ISR Register: %d", value);
		ne2000_set_field_by_offset(sc, NE2000_P0, ED_P0_ISR, value, 0);
		ne2000_update_intr(sc);
		break;
	case ED_P0_RCR:
		DPRINTF("RCR Register: %d", value);
		break;
	}

	return 0;
}

static int
ne2000_emul_reg_page1(struct ne2000_softc *sc, uint8_t offset,
		uint8_t value)
{
	uint8_t pstart = 0;
	uint8_t pstop = 0;

	assert(offset != ED_P1_CR);

	switch (offset) {
	case ED_P1_PAR0 ... ED_P1_PAR5:
		DPRINTF("PAR[%d]: 0x%x", offset - ED_P1_PAR0, value);
		break;
	case ED_P1_CURR:
		DPRINTF("Current Page Register: %d", value);
		pstart = ne2000_get_reg_by_offset(sc, NE2000_P0, ED_P0_PSTART);
		pstop = ne2000_get_reg_by_offset(sc, NE2000_P0, ED_P0_PSTOP);
		assert(value >= pstart && value < pstop);
		break;
	case ED_P1_MAR0 ... ED_P1_MAR7:
		DPRINTF("MAR[%d]: 0x%x", offset - ED_P1_MAR0, value);
		break;
	default:
		assert(0);
	}

	return 0;
}

static int
ne2000_software_reset(struct ne2000_softc *sc)
{
	DPRINTF("The NIC is in Software Reset State");

	/* reset the Receive Ring Registers */
	ne2000_set_reg_by_offset(sc, NE2000_P0, ED_P0_PSTART, 0);
	ne2000_set_reg_by_offset(sc, NE2000_P0, ED_P0_PSTOP, 0);
	ne2000_set_reg_by_offset(sc, NE2000_P1, ED_P1_CURR, 0);
	ne2000_set_reg_by_offset(sc, NE2000_P0, ED_P0_BNRY, 0);

	/* disable the interrupts */
	ne2000_set_reg_by_offset(sc, NE2000_P0, ED_P0_IMR, 0);

	/* the NIC enters the reset state */
	ne2000_set_field_by_offset(sc, NE2000_P0, ED_P0_ISR,
			ED_ISR_RST, ED_ISR_RST);

	return 0;
}

static struct ne2000_softc *
ne2000_init(ne2000_intr_func_t intr_assert, ne2000_intr_func_t intr_deassert,
		void *intr_arg, const char *opts)
{
	struct ne2000_softc *sc = NULL;

	/* the default mac address is 00:a0:98:4a:0e:ee */
	uint8_t mac[ETHER_ADDR_LEN] = {0x00, 0xa0, 0x98, 0x4a, 0x0e, 0xee};
	char tap_name[MAX_INPUT_LEN];
	int err;

	assert(intr_assert);
	assert(intr_deassert);
	assert(intr_arg);

#if DEBUG_NE2000 == 1
	dbg = fopen("/tmp/bhyve_ne2000.log", "w+");
#endif

	sc = calloc(1, sizeof(struct ne2000_softc));

	sc->intr_assert = intr_assert;
	sc->intr_deassert = intr_deassert;
	sc->intr_arg = intr_arg;

	err = ne2000_parse_input(opts, tap_name, mac);
	if (err != 0) {
		printf("Use input param like: -s x:y,ne2000-net,tap_name[,mac address]");
		free(sc);
		return NULL;
	}

	err = pthread_mutex_init(&sc->mtx, NULL);
	assert(err == 0);

	err = ne2000_tap_init(sc, tap_name);
	assert(err == 0);

	/* set network medium type as 10BaseT and full-duplex */
	ne2000_set_reg_by_offset(sc, NE2000_P3,
			ED_RTL80X9_CONFIG2, ED_RTL80X9_CF2_10_T);
	ne2000_set_reg_by_offset(sc, NE2000_P3,
			ED_RTL80X9_CONFIG3, ED_RTL80X9_CF3_FUDUP);

	/* the NE2000 card has his MAC address located in the first 6 words of the RAM memory */
	sc->ram[0] = mac[0];
	sc->ram[2] = mac[1];
	sc->ram[4] = mac[2];
	sc->ram[6] = mac[3];
	sc->ram[8] = mac[4];
	sc->ram[10] = mac[5];

	return sc;
}

static void
ne2000_update_intr(struct ne2000_softc *sc)
{
	uint8_t isr = 0;
	uint8_t imr = 0;

	isr = ne2000_get_reg_by_offset(sc, NE2000_P0, ED_P0_ISR);
	imr = ne2000_get_reg_by_offset(sc, NE2000_P0, ED_P0_IMR);

	if (imr & isr) {
		if (!sc->lintr) {
			sc->intr_assert(sc->intr_arg);
			sc->lintr = 1;
		}
	} else {
		if (sc->lintr) {
			sc->intr_deassert(sc->intr_arg);
			sc->lintr = 0;
		}
	}
}

static uint16_t
ne2000_read(struct ne2000_softc *sc, uint8_t offset, int size)
{
	int err;
	uint16_t value = 0;

	assert(offset < ED_NOVELL_IO_PORTS);

	err = pthread_mutex_lock(&sc->mtx);
	assert(err == 0);

	if (offset < ED_NOVELL_ASIC_OFFSET) {
		assert(size == 1);
		value = ne2000_read_nic_locked(sc, offset);
	}
	else {
		assert(size <= 2);
		value = ne2000_read_asic_locked(sc, offset - ED_NOVELL_ASIC_OFFSET);
	}

	err = pthread_mutex_unlock(&sc->mtx);
	assert(err == 0);

	return value;
}

static int
ne2000_write(struct ne2000_softc *sc, uint8_t offset, uint16_t value, int size)
{
	int err;

	assert(offset < ED_NOVELL_IO_PORTS);

	err = pthread_mutex_lock(&sc->mtx);
	assert(err == 0);

	if (offset < ED_NOVELL_ASIC_OFFSET) {
		assert(size == 1);
		err = ne2000_write_nic_locked(sc, offset, value);
	}
	else {
		assert(size <= 2);
		err = ne2000_write_asic_locked(sc, offset - ED_NOVELL_ASIC_OFFSET, value);
	}

	assert(err == 0);

	err = pthread_mutex_unlock(&sc->mtx);
	assert(err == 0);

	return 0;
}

static uint8_t
ne2000_read_nic_locked(struct ne2000_softc *sc, uint8_t offset)
{
	int err;
	uint8_t value = 0;
	uint8_t cr = 0;

	/*
	 * check is either a RTL8029 Register Defined in Page0
	 * or is a read-only Register Defined in Page0
	 */
	if (sc->page == NE2000_P0) {
		switch (offset) {
		case ED_RTL80X9_80X9ID0:
			value = ED_RTL80X9_ID0;
			break;
		case ED_RTL80X9_80X9ID1:
			value = ED_RTL8029_ID1;
			break;
		case ED_P0_CLDA0:
		case ED_P0_CLDA1:
		case ED_P0_TSR:
		case ED_P0_NCR:
		case ED_P0_FIFO:
		case ED_P0_CRDA0:
		case ED_P0_CRDA1:
		case ED_P0_RSR:
		case ED_P0_CNTR0:
		case ED_P0_CNTR1:
		case ED_P0_CNTR2:
			/* read a read-only register from page 0 */
			value = ne2000_get_reg_by_offset(sc, NE2000_P0_RO, offset);
			break;
		case ED_P0_ISR:
			/*
			 * Software Reset Workaround: the NIC enters the reset state
			 * in about 5us after the STP bit was set. Because of this
			 * we don't reset it every time the STP bit is set, but only
			 * when the ED driver polls the ISR register looking for
			 * the RST bit.
			 */
			cr = ne2000_get_reg_by_offset(sc, NE2000_P0, ED_P0_CR);
			if (cr & ED_CR_STP) {
				err = ne2000_software_reset(sc);
				assert(err == 0);
			}
			value = ne2000_get_reg_by_offset(sc, NE2000_P0, ED_P0_ISR);
			break;
		default:
			value = ne2000_get_reg_by_offset(sc, NE2000_P0, offset);
			break;
		}
	} else {
		/* read a general NE2000 register */
		value = ne2000_get_reg_by_offset(sc, sc->page, offset);
	}

	return value;
}

static uint16_t
ne2000_read_asic_locked(struct ne2000_softc *sc, uint8_t offset)
{
	int err;
	uint8_t dcr = 0;
	uint8_t rbcr0 = 0;
	uint8_t rbcr1 = 0;
	uint8_t rsar0 = 0;
	uint8_t rsar1 = 0;

	uint16_t rbcr = 0;
	uint16_t rsar = 0;
	uint16_t read_value = 0;

	switch (offset) {
	case ED_NOVELL_RESET:
		read_value = sc->reset;
		err = ne2000_reset_board();
		assert(err == 0);
		break;
	case ED_NOVELL_DATA:
		/* Read one word from the NIC's RAM using the Remote DMA
		 * protocol
		 */
		dcr = ne2000_get_reg_by_offset(sc, NE2000_P0, ED_P0_DCR);
		if ((dcr & ED_DCR_WTS) != ED_DCR_WTS) {
			DPRINTF("The NE2000 card is working only in Word mode");
			sc->remote_read = 0;
			break;
		}

		assert(sc->remote_read);

		rbcr0 = ne2000_get_reg_by_offset(sc, NE2000_P0, ED_P0_RBCR0);
		rbcr1 = ne2000_get_reg_by_offset(sc, NE2000_P0, ED_P0_RBCR1);
		rbcr = rbcr0 | (rbcr1 << 8);

		rsar0 = ne2000_get_reg_by_offset(sc, NE2000_P0, ED_P0_RSAR0);
		rsar1 = ne2000_get_reg_by_offset(sc, NE2000_P0, ED_P0_RSAR1);
		rsar = rsar0 | (rsar1 << 8);

		assert(rsar < NE2000_MEM_SIZE);

		/* copy the value in LOW - HIGH order */
		read_value = sc->ram[rsar] | (sc->ram[rsar + 1] << 8);

		rsar += 2;
		rbcr -= 2;

		if (rbcr == 0)
			sc->remote_read = 0;

		ne2000_set_reg_by_offset(sc, NE2000_P0, ED_P0_RSAR0, rsar);
		ne2000_set_reg_by_offset(sc, NE2000_P0, ED_P0_RSAR1, rsar >> 8);

		ne2000_set_reg_by_offset(sc, NE2000_P0, ED_P0_RBCR0, rbcr);
		ne2000_set_reg_by_offset(sc, NE2000_P0, ED_P0_RBCR1, rbcr >> 8);

		break;
	default:
		assert(0);
	}

	return read_value;
}

static int ne2000_reset_board(void)
{
	DPRINTF("The driver resets the board");

	return 0;
}

/*
 * PCI NE2000 function definitions
 */
static int
pci_ne2000_init(struct vmctx *ctx, struct pci_devinst *pi, char *opts)
{
	struct ne2000_softc *sc = NULL;

	assert(ctx != NULL);
	assert(pi != NULL);

	/* probe a RTL8029 PCI card as a generic NE2000 device */
	pci_set_cfgdata16(pi, PCIR_DEVICE, 0x8029);
	pci_set_cfgdata16(pi, PCIR_VENDOR, 0x10ec);
	pci_set_cfgdata8(pi, PCIR_CLASS, PCIC_NETWORK);

	/* allocate one BAR register for both NIC and ASIC I/O bus address offsets */
	pci_emul_alloc_bar(pi, 0, PCIBAR_IO, ED_NOVELL_IO_PORTS);

	/* allocate an IRQ pin for our slot */
	pci_lintr_request(pi);

	/* initialize the ne2000 data structure */
	sc = ne2000_init(pci_ne2000_intr_assert, pci_ne2000_intr_deassert, pi, opts);
	if (sc == NULL)
		return 1;

	pi->pi_arg = sc;

	DPRINTF("PCI NE2000 initialized: ne2k,%s", opts);

	return 0;
}

static void
pci_ne2000_write(struct vmctx *ctx, int vcpu, struct pci_devinst *pi,
		int baridx, uint64_t offset, int size, uint64_t value)
{
	struct ne2000_softc *sc = pi->pi_arg;
	int err;

	assert(sc != NULL);

	err = ne2000_write(sc, offset, value, size);
	assert(err == 0);

	return;
}

static uint64_t
pci_ne2000_read(struct vmctx *ctx, int vcpu, struct pci_devinst *pi,
		int baridx, uint64_t offset, int size)
{
	struct ne2000_softc *sc = pi->pi_arg;
	uint64_t value = 0;

	assert(sc != NULL);

	value = ne2000_read(sc, offset, size);

	return value;
}

static void
pci_ne2000_intr_assert(void *arg)
{
	struct pci_devinst *pi = (struct pci_devinst *)arg;

	pci_lintr_assert(pi);

	return;
}

static void
pci_ne2000_intr_deassert(void *arg)
{
	struct pci_devinst *pi = (struct pci_devinst *)arg;

	pci_lintr_deassert(pi);

	return;
}

/*
 * LPC NE2000 function definitions
 */
int
lpc_ne2000_init(struct vmctx *lpc_ctx, uint8_t unit, const char *opts)
{
	struct ne2000_softc *sc = NULL;
	struct inout_port iop;
	int err;

	assert(lpc_ctx != NULL);
	assert(unit < LPC_NE2000_NUM);

	if (lpc_ne2000_sc[unit].ne2000_sc != NULL) {
		fprintf(stderr, "The LPC ne2k%d device is initialized\n", unit);
		return 0;
	}

	/*
	 * Allocate one io port for both NIC and ASIC I/O bus address offsets.
	 * The NE2000 devices on the LPC bus generate edge triggered interrupts
	 * so nothing more to do here
	 */
	bzero(&iop, sizeof(struct inout_port));
	iop.name = lpc_ne2000_sc[unit].name;
	iop.port = lpc_ne2000_sc[unit].base_addr;
	iop.size = ED_NOVELL_IO_PORTS;
	iop.flags = IOPORT_F_INOUT;
	iop.handler = lpc_ne2000_io_handler;
	iop.arg = &lpc_ne2000_sc[unit];

	err = register_inout(&iop);
	assert(err == 0);

	/* initialize the ne2000 data structure */
	sc = ne2000_init(lpc_ne2000_intr_assert, lpc_ne2000_intr_deassert,
			&lpc_ne2000_sc[unit], opts);
	if (sc == NULL)
		return -1;

	lpc_ne2000_sc[unit].ne2000_sc = sc;
	lpc_ne2000_sc[unit].lpc_ctx = lpc_ctx;

	DPRINTF("LPC NE2000 initialized: %s,%s", lpc_ne2000_sc[unit].name, opts);

	return 0;
}

static int
lpc_ne2000_io_handler(struct vmctx *ctx, int vcpu, int in, int port, int bytes,
		uint32_t *eax, void *arg)
{
	struct lpc_ne2000_softc *sc_lpc_ne2000 = (struct lpc_ne2000_softc *)arg;
	struct ne2000_softc *sc = NULL;

	uint8_t offset = 0;
	int err;

	assert(sc_lpc_ne2000 != NULL);

	sc = sc_lpc_ne2000->ne2000_sc;
	assert(sc != NULL);

	offset = port - sc_lpc_ne2000->base_addr;

	if (in) {
		*eax = ne2000_read(sc, offset, bytes);
	}
	else {
		err = ne2000_write(sc, offset, *eax, bytes);
		assert(err == 0);
	}

	return 0;
}

static void
lpc_ne2000_intr_assert(void *arg)
{
	struct lpc_ne2000_softc *sc_lpc_ne2000 = (struct lpc_ne2000_softc *)arg;
	struct vmctx *lpc_ctx = sc_lpc_ne2000->lpc_ctx;

	vm_isa_pulse_irq(lpc_ctx, sc_lpc_ne2000->irq, sc_lpc_ne2000->irq);

	return;
}

static void
lpc_ne2000_intr_deassert(void *arg)
{
	/*
	 * The NE2000 devices on the LPC bus generate edge triggered interrupts
	 * so nothing more to do here
	 */

	return;
}

