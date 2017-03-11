/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2017 Hayashi Naoyuki
 */

#include <sys/promif.h>
#include <sys/miiregs.h>
#include <sys/ethernet.h>
#include <sys/byteorder.h>
#include <sys/controlregs.h>
#include <sys/debug.h>
#include <sys/platmod.h>
#include "enet.h"

#include <sys/salib.h>
#include <sys/boot.h>
#include "prom_dev.h"

#define MENET_PATH	"/soc/ethernet@17020000"

static void
enet_csr_write(struct enet_sc *sc, uint32_t offset, uint32_t val)
{
	void *addr = sc->reg[ENET_CSR].addr + offset;
	*(volatile uint32_t *)(addr) = val;
}

static uint32_t
enet_csr_read(struct enet_sc *sc, uint32_t offset)
{
	void *addr = sc->reg[ENET_CSR].addr + offset;
	return *(volatile uint32_t *)(addr);
}

static void
enet_ring_csr_write(struct enet_sc *sc, uint32_t offset, uint32_t val)
{
	void *addr = sc->reg[RING_CSR].addr + offset;
	*(volatile uint32_t *)(addr) = val;
}

static uint32_t
enet_ring_csr_read(struct enet_sc *sc, uint32_t offset)
{
	void *addr = sc->reg[RING_CSR].addr + offset;
	return *(volatile uint32_t *)(addr);
}

static void
enet_cmd_write(struct enet_sc *sc, struct enet_desc_ring *ring, uint32_t offset, uint32_t val)
{
	void *addr = ring->cmd_base + offset;
	*(volatile uint32_t *)(addr) = val;
}

static void
enet_usecwait(int usec)
{
	uint64_t cnt = (read_cntpct() / (read_cntfrq() / 1000000)) + usec + 2;
	for (;;) {
		if ((read_cntpct() / (read_cntfrq() / 1000000)) > cnt)
			break;
	}

}

static pnode_t
enet_get_node(struct enet_sc *sc)
{
	return prom_finddevice(MENET_PATH);
}

static uint32_t
enet_mac_read(struct enet_sc *sc, uint32_t addr)
{
	enet_csr_write(sc, ENET_CSR_MAC_ADDR, addr);
	enet_csr_write(sc, ENET_CSR_MAC_COMMAND, ENET_CSR_MAC_COMMAND_READ);

	while (!enet_csr_read(sc, ENET_CSR_MAC_COMMAND_DONE)) {
		enet_usecwait(1);
	}

	uint32_t val = enet_csr_read(sc, ENET_CSR_MAC_READ);
	enet_csr_write(sc, ENET_CSR_MAC_COMMAND, 0);

	return val;
}

static void
enet_mac_write(struct enet_sc *sc, uint32_t addr, uint32_t val)
{
	enet_csr_write(sc, ENET_CSR_MAC_ADDR, addr);
	enet_csr_write(sc, ENET_CSR_MAC_WRITE, val);
	enet_csr_write(sc, ENET_CSR_MAC_COMMAND, ENET_CSR_MAC_COMMAND_WRITE);

	while (!enet_csr_read(sc, ENET_CSR_MAC_COMMAND_DONE)) {
		enet_usecwait(1);
	}

	enet_csr_write(sc, ENET_CSR_MAC_COMMAND, 0);
}

static void
enet_write_ring_state(struct enet_sc *sc, struct enet_desc_ring *ring)
{
	enet_ring_csr_write(sc, ENET_RING_CSR_CONFIG, ring->num);
	enet_ring_csr_write(sc, ENET_RING_CSR_WR_BASE + (4 * 0), ring->cfg.cfg0);
	enet_ring_csr_write(sc, ENET_RING_CSR_WR_BASE + (4 * 1), ring->cfg.cfg1);
	enet_ring_csr_write(sc, ENET_RING_CSR_WR_BASE + (4 * 2), ring->cfg.cfg2);
	enet_ring_csr_write(sc, ENET_RING_CSR_WR_BASE + (4 * 3), ring->cfg.cfg3);
	enet_ring_csr_write(sc, ENET_RING_CSR_WR_BASE + (4 * 4), ring->cfg.cfg4);
}

static void
enet_clr_ring_state(struct enet_sc *sc, struct enet_desc_ring *ring)
{
	memset(&ring->cfg, 0, sizeof(ring->cfg));
	enet_write_ring_state(sc, ring);
}

static void
enet_set_ring_state(struct enet_sc *sc, struct enet_desc_ring *ring)
{
	if (ring->is_bufpool) {
		ring->cfg.mode = 3;
		ring->cfg.type = 2;
	} else {
		ring->cfg.type = 1;
	}

	if (ring->id.owner != ENET_RING_OWNER_CPU) {
		ring->cfg.recombbuf = 1;
		ring->cfg.recomtimeout_l = 0xF;
		ring->cfg.recomtimeout_h = 0x7;
	}

	uint64_t addr = ring->desc.dmac_addr;

	ring->cfg.selthrsh = 1;
	ring->cfg.acceptlerr = 1;
	ring->cfg.qcoherent = 1;
	ring->cfg.addr_lo = (addr >> 8);
	ring->cfg.addr_hi = (addr >> (8 + 27));
	ring->cfg.size = ring->cfgsize;

	enet_write_ring_state(sc, ring);
}

static void
enet_set_ring_id(struct enet_sc *sc, struct enet_desc_ring *ring)
{
	union {
		uint32_t val;
		struct {
			uint32_t id		: 10;
			uint32_t 		: 21;
			uint32_t overwrite	: 1;
		};
	} ring_id = {
		.id = ring->id.val,
		.overwrite = 1,
	};

	union {
		uint32_t val;
		struct {
			uint32_t 		: 9;
			uint32_t num		: 10;
			uint32_t 		: 1;
			uint32_t bufpool	: 1;
			uint32_t prefetch	: 1;
		};
	} ring_id_buf = {
		.num = ring->num,
		.bufpool = ring->is_bufpool? 1: 0,
		.prefetch = 1,
	};

	enet_ring_csr_write(sc, ENET_RING_CSR_ID, ring_id.val);
	enet_ring_csr_write(sc, ENET_RING_CSR_ID_BUF, ring_id_buf.val);
}

static void
enet_setup_ring(struct enet_sc *sc, struct enet_desc_ring *ring)
{
	enet_clr_ring_state(sc, ring);
	enet_set_ring_state(sc, ring);
	enet_set_ring_id(sc, ring);

	if (ring->is_bufpool || ring->id.owner != ENET_RING_OWNER_CPU)
		return;

	for (int i = 0; i < ring->slots; i++) {
		struct enet_desc *desc_ptr = ((struct enet_desc *)ring->desc.addr + i);
		desc_ptr->m1 = ENET_DESC_M1_EMPTY;
	}
	dsb();

	uint32_t value = enet_ring_csr_read(sc, ENET_RING_CSR_NE_INT_MODE);
	value |= (1u << (31 - ring->id.bufnum));
	enet_ring_csr_write(sc, ENET_RING_CSR_NE_INT_MODE, value);
}

static void
enet_gmac_set_mac_addr(struct enet_sc *sc)
{
	union {
		uint32_t val[2];
		struct {
			uint32_t addr0:	8;
			uint32_t addr1:	8;
			uint32_t addr2:	8;
			uint32_t addr3:	8;
			uint32_t :	16;
			uint32_t addr4:	8;
			uint32_t addr5:	8;
		};
	} mac_address = {
		.addr0 = sc->dev_addr[0],
		.addr1 = sc->dev_addr[1],
		.addr2 = sc->dev_addr[2],
		.addr3 = sc->dev_addr[3],
		.addr4 = sc->dev_addr[4],
		.addr5 = sc->dev_addr[5]
	};

	enet_mac_write(sc, ENET_MAC_STATION_ADDR0, mac_address.val[0]);
	enet_mac_write(sc, ENET_MAC_STATION_ADDR1, mac_address.val[1]);
}

static void
enet_gmac_reset(struct enet_sc *sc)
{
	union {
		struct {
			uint32_t tx_en		: 1;
			uint32_t		: 1;
			uint32_t rx_en		: 1;
			uint32_t 		: 28;
			uint32_t soft_reset	: 1;
		};
		uint32_t val;
	} mac1 = {0};

	mac1.soft_reset = 1;
	enet_mac_write(sc, ENET_MAC_CONFIG_1, mac1.val);
	mac1.soft_reset = 0;
	enet_mac_write(sc, ENET_MAC_CONFIG_1, mac1.val);
}

static void
enet_gmac_init(struct enet_sc *sc)
{
	enet_gmac_reset(sc);

	union {
		struct {
			uint32_t full_duplex	: 1;
			uint32_t 		: 7;
			uint32_t mode		: 2;
			uint32_t		: 22;
		};
		uint32_t val;
	} mc2;

	union {
		struct {
			uint32_t		: 18;
			uint32_t mode		: 2;
			uint32_t		: 12;
		};
		uint32_t val;
	} icm0;

	union {
		struct {
			uint32_t wait_sync	: 16;
			uint32_t		: 16;
		};
		uint32_t val;
	} icm2;

	union {
		struct {
			uint32_t		: 24;
			uint32_t speed1250	: 1;
			uint32_t		: 4;
			uint32_t txclk_muxsel0	: 3;
		};
		uint32_t val;
	} rgmii;

	union {
		struct {
			uint32_t		: 25;
			uint32_t lhd_mode	: 1;
			uint32_t ghd_mode	: 1;
			uint32_t		: 5;
		};
		uint32_t val;
	} intf_ctl;

	icm0.val = enet_csr_read(sc, ENET_CSR_ICM_CONFIG0);
	icm2.val = enet_csr_read(sc, ENET_CSR_ICM_CONFIG2);
	mc2.val = enet_mac_read(sc, ENET_MAC_CONFIG_2);
	intf_ctl.val = enet_mac_read(sc, ENET_MAC_INTERFACE_CONTROL);
	rgmii.val = enet_csr_read(sc, ENET_CSR_RGMII);

	switch (sc->phy_speed) {
	case 10:
		mc2.mode = 1;
		icm0.mode = 0;
		icm2.wait_sync = 500;
		rgmii.speed1250 = 0;
		break;
	case 100:
		mc2.mode = 1;
		intf_ctl.lhd_mode = 1;
		icm0.mode = 1;
		icm2.wait_sync = 80;
		rgmii.speed1250 = 0;
		break;
	default:
		{
			mc2.mode = 2;
			intf_ctl.ghd_mode = 1;
			rgmii.txclk_muxsel0 = 4;

			union {
				struct {
					uint32_t			: 1;
					uint32_t bypass_unisec_rx	: 1;
					uint32_t bypass_unisec_tx	: 1;
					uint32_t			: 29;
				};
				uint32_t val;
			} debug_reg;

			debug_reg.val = enet_csr_read(sc, ENET_CSR_DEBUG);
			debug_reg.bypass_unisec_tx = 1;
			debug_reg.bypass_unisec_rx = 1;
			enet_csr_write(sc, ENET_CSR_DEBUG, debug_reg.val);
		}
		break;
	}

	mc2.full_duplex = 1;

	enet_mac_write(sc, ENET_MAC_CONFIG_2, mc2.val);
	enet_mac_write(sc, ENET_MAC_INTERFACE_CONTROL, intf_ctl.val);

	enet_gmac_set_mac_addr(sc);

	union {
		struct {
			uint32_t clk_sel	: 3;
			uint32_t		: 2;
			uint32_t scan_auto_incr	: 1;
			uint32_t		: 26;
		};
		uint32_t val;
	} mii_mgmt;

	mii_mgmt.val = enet_mac_read(sc, ENET_MAC_MII_MGMT_CONFIG);
	mii_mgmt.clk_sel = 7;
	enet_mac_write(sc, ENET_MAC_MII_MGMT_CONFIG, mii_mgmt.val);

	union {
		struct {
			uint32_t			: 31;
			uint32_t fpbuff_timeout_en	: 1;
		};
		uint32_t val;
	} rsif;

	rsif.val = enet_csr_read(sc, ENET_CSR_RSIF_CONFIG);
	rsif.fpbuff_timeout_en = 1;
	enet_csr_write(sc, ENET_CSR_RSIF_CONFIG, rsif.val);

	enet_csr_write(sc, ENET_CSR_RSIF_RAM_DBG, 0);
	enet_csr_write(sc, ENET_CSR_RGMII, rgmii.val);

	union {
		struct {
			uint32_t tx_port0	: 1;
			uint32_t		: 31;
		};
		uint32_t val;
	} link_aggr_resume = {0};

	link_aggr_resume.tx_port0 = 1;
	enet_csr_write(sc, ENET_CSR_CFG_LINK_AGGR_RESUME, link_aggr_resume.val);

	enet_csr_write(sc, ENET_CSR_ICM_CONFIG0, icm0.val);
	enet_csr_write(sc, ENET_CSR_ICM_CONFIG2, icm2.val);

	union {
		struct {
			uint32_t resume_rx0	: 1;
			uint32_t rx_dv_gate_en0	: 1;
			uint32_t tx_dv_gate_en0	: 1;
			uint32_t		: 29;
		};
		uint32_t val;
	} rx_dv_gate;

	rx_dv_gate.val = enet_csr_read(sc, ENET_CSR_RX_DV_GATE);
	rx_dv_gate.tx_dv_gate_en0 = 0;
	rx_dv_gate.rx_dv_gate_en0 = 0;
	rx_dv_gate.resume_rx0 = 1;
	enet_csr_write(sc, ENET_CSR_RX_DV_GATE, rx_dv_gate.val);

	union {
		struct {
			uint32_t resume_tx	: 1;
			uint32_t		: 31;
		};
		uint32_t val;
	} bypass = {0};

	bypass.resume_tx = 1;
	enet_csr_write(sc, ENET_CSR_CFG_BYPASS, bypass.val);
}

static void
enet_gmac_enable(struct enet_sc *sc)
{
	union {
		struct {
			uint32_t tx_en		: 1;
			uint32_t		: 1;
			uint32_t rx_en		: 1;
			uint32_t 		: 28;
			uint32_t soft_reset	: 1;
		};
		uint32_t val;
	} mac1;

	mac1.val = enet_mac_read(sc, ENET_MAC_CONFIG_1);
	mac1.tx_en = 1;
	mac1.rx_en = 1;
	enet_mac_write(sc, ENET_MAC_CONFIG_1, mac1.val);
}

static void
enet_gmac_disable(struct enet_sc *sc)
{
	union {
		struct {
			uint32_t tx_en		: 1;
			uint32_t		: 1;
			uint32_t rx_en		: 1;
			uint32_t 		: 29;
		};
		uint32_t val;
	} mac1;

	mac1.val = enet_mac_read(sc, ENET_MAC_CONFIG_1);
	mac1.tx_en = 0;
	mac1.rx_en = 0;
	enet_mac_write(sc, ENET_MAC_CONFIG_1, mac1.val);
}

static bool
enet_reset(struct enet_sc *sc)
{
	if (!enet_ring_csr_read(sc, ENET_RING_CSR_CLKEN))
		return false;

	if (enet_ring_csr_read(sc, ENET_RING_CSR_SRST))
		return false;

	uint32_t val;

	enet_csr_write(sc, ENET_CSR_CFG_MEM_RAM_SHUTDOWN, 0x0);
	for (int i = 0; i < 10; i++) {
		enet_usecwait(200);
		val = enet_csr_read(sc, ENET_CSR_BLOCK_MEM_RDY);
		if (val == 0xffffffff)
			break;
	}

	if (val != 0xffffffff) {
		return false;
	}

	enet_csr_write(sc, ENET_CSR_CFGSSQMIWQASSOC, 0xffffffff);
	enet_csr_write(sc, ENET_CSR_CFGSSQMIFPQASSOC, 0xffffffff);
	enet_csr_write(sc, ENET_CSR_CFGSSQMIQMLITEFPQASSOC, 0xffffffff);
	enet_csr_write(sc, ENET_CSR_CFGSSQMIQMLITEWQASSOC, 0xffffffff);

	union {
		struct {
			uint32_t clk_sel	: 3;
			uint32_t		: 2;
			uint32_t scan_auto_incr	: 1;
			uint32_t		: 26;
		};
		uint32_t val;
	} mii_mgmt;

	mii_mgmt.val = enet_mac_read(sc, ENET_MAC_MII_MGMT_CONFIG);
	mii_mgmt.scan_auto_incr = 1;
	mii_mgmt.clk_sel = 1;
	enet_mac_write(sc, ENET_MAC_MII_MGMT_CONFIG, mii_mgmt.val);

	return true;
}

static caddr_t
enet_ring_cmd_base(struct enet_sc *sc, struct enet_desc_ring *ring)
{
	return (caddr_t)sc->reg[RING_CMD].addr + (ring->num << 6);
}

static void
menet_send_buf(struct enet_sc *sc, uint64_t addr, size_t len)
{
	int index = sc->tx_ring.index;

	struct enet_desc desc = {
		.addr = addr,
		.len = len,
		.ic = 1,
		.coherent = 1,
		.type = 1,	// ethernet
		.henqnum = sc->tx_ring.dst_ring.val
	};

	struct enet_desc *desc_ptr = (struct enet_desc *)sc->tx_ring.desc.addr + index;
	desc_ptr->m0 = desc.m0;
	desc_ptr->m1 = desc.m1;
	desc_ptr->m2 = desc.m2;
	desc_ptr->m3 = desc.m3;
	dsb();

	enet_cmd_write(sc, &sc->tx_ring, ENET_RING_CMD_INC_DEC, 1);

	sc->tx_ring.index = (index + 1) % sc->tx_ring.slots;
}

static bool
menet_recv_available(struct enet_sc *sc)
{
	int rxc_index = sc->rxc_ring.index;
	struct enet_desc *desc_ptr = (struct enet_desc *)(sc->rxc_ring.desc.addr) + rxc_index;
	if (desc_ptr->m1 == ENET_DESC_M1_EMPTY)
		return false;
	return true;
}

static void
menet_recv(struct enet_sc *sc, void *buf, size_t len)
{
	int rx_index = sc->rx_ring.index;
	int rxc_index = sc->rxc_ring.index;

	struct enet_desc *desc_ptr = (struct enet_desc *)(sc->rxc_ring.desc.addr) + rxc_index;

	if (buf && len) {
		ASSERT(desc_ptr->info == rx_index);
		size_t buf_size = 2048;

		void *ptr = (void *)(sc->rx_ring.buf.addr + buf_size * rx_index);
		memcpy(buf, ptr, len);
	}

	desc_ptr->m1 = ENET_DESC_M1_EMPTY;
	dsb();

	enet_cmd_write(sc, &sc->rxc_ring, ENET_RING_CMD_INC_DEC, -1);
	enet_cmd_write(sc, &sc->rx_ring, ENET_RING_CMD_INC_DEC, 1);

	sc->rx_ring.index = (rx_index + 1) % sc->rx_ring.slots;
	sc->rxc_ring.index = (rxc_index + 1) % sc->rxc_ring.slots;
}

static int
menet_txc(struct enet_sc *sc)
{
	int ret = 0;
	for (;;) {
		int index = sc->txc_ring.index;
		struct enet_desc *desc_ptr = (struct enet_desc *)(sc->txc_ring.desc.addr) + index;
		if (desc_ptr->m1 == ENET_DESC_M1_EMPTY)
			break;

		desc_ptr->m1 = ENET_DESC_M1_EMPTY;
		sc->txc_ring.index = (index + 1) % sc->txc_ring.slots;
		ret++;
	}

	if (ret) {
		dsb();
		enet_cmd_write(sc, &sc->txc_ring, ENET_RING_CMD_INC_DEC, -ret);
	}

	return ret;
}

static bool
enet_alloc_desc_ring(struct enet_sc *sc, struct enet_desc_ring *desc_ring, size_t size)
{
	caddr_t ptr = (caddr_t)kmem_alloc(size, 0);
	if (!ptr)
		return false;

	desc_ring->desc.addr = ptr;
	desc_ring->desc.dmac_addr = (uintptr_t)ptr;

	return true;
}

static bool
enet_alloc_desc_ring_buf(struct enet_sc *sc, struct enet_desc_ring *desc_ring, size_t size, size_t align)
{
	caddr_t ptr = (caddr_t)kmem_alloc(size + align, 0);
	if (!ptr)
		return false;

	ptr = (caddr_t)((uintptr_t)(ptr + align - 1) & ~(align - 1));
	desc_ring->buf.addr = ptr;
	desc_ring->buf.dmac_addr = (uintptr_t)ptr;

	return true;
}

static bool
enet_open(struct enet_sc *sc)
{
	sc->rm = ENET_RM3;

	plat_clk_disable("menetclk");
	enet_usecwait(10000);
	plat_clk_enable("menetclk");
	enet_usecwait(10000);

	if (!enet_reset(sc)) {
		return false;
	}

	enet_gmac_disable(sc);

	uint8_t cpu_bufnum = ENET_CPU_BUFNUM_START(sc->port);
	uint8_t eth_bufnum = ENET_ETH_BUFNUM_START(sc->port);
	uint8_t bufpool_bufnum = ENET_BUFPOOL_BUFNUM_START(sc->port);
	uint16_t ring_num = ENET_RING_NUM_START(sc->port);

	union enet_ring_id ring_id = {0};

	sc->rxc_ring.is_bufpool = false;
	sc->rxc_ring.num = ring_num++;
	ring_id.owner = ENET_RING_OWNER_CPU;
	ring_id.bufnum = cpu_bufnum++;
	sc->rxc_ring.id = ring_id;
	sc->rxc_ring.cmd_base = enet_ring_cmd_base(sc, &sc->rxc_ring);
	sc->rxc_ring.cfgsize = ENET_RING_CFGSIZE_512KB;
	sc->rxc_ring.slots = 524288 / sizeof(struct enet_desc);
	if (!enet_alloc_desc_ring(sc, &sc->rxc_ring, 524288))
		return false;
	enet_setup_ring(sc, &sc->rxc_ring);

	sc->rx_ring.is_bufpool = true;
	sc->rx_ring.num = ring_num++;
	ring_id.owner = (sc->port == 0? ENET_RING_OWNER_ETH0: ENET_RING_OWNER_ETH1);
	ring_id.bufnum = bufpool_bufnum++;
	sc->rx_ring.id = ring_id;
	sc->rx_ring.cmd_base = enet_ring_cmd_base(sc, &sc->rx_ring);
	sc->rx_ring.cfgsize = ENET_RING_CFGSIZE_64KB;
	sc->rx_ring.slots = 65536 / sizeof(struct enet_desc16);
	if (!enet_alloc_desc_ring(sc, &sc->rx_ring, 65536))
		return false;
	enet_setup_ring(sc, &sc->rx_ring);

	sc->tx_ring.is_bufpool = false;
	sc->tx_ring.num = ring_num++;
	ring_id.owner = (sc->port == 0? ENET_RING_OWNER_ETH0: ENET_RING_OWNER_ETH1);
	ring_id.bufnum = eth_bufnum++;
	sc->tx_ring.id = ring_id;
	sc->tx_ring.cmd_base = enet_ring_cmd_base(sc, &sc->tx_ring);
	sc->tx_ring.cfgsize = ENET_RING_CFGSIZE_64KB;
	sc->tx_ring.slots = 65536 / sizeof(struct enet_desc);
	if (!enet_alloc_desc_ring(sc, &sc->tx_ring, 65536))
		return false;
	enet_setup_ring(sc, &sc->tx_ring);

	sc->txc_ring.is_bufpool = false;
	sc->txc_ring.num = ring_num++;
	ring_id.owner = ENET_RING_OWNER_CPU;
	ring_id.bufnum = cpu_bufnum++;
	sc->txc_ring.id = ring_id;
	sc->txc_ring.cmd_base = enet_ring_cmd_base(sc, &sc->txc_ring);
	sc->txc_ring.cfgsize = ENET_RING_CFGSIZE_64KB;
	sc->txc_ring.slots = 65536 / sizeof(struct enet_desc);
	if (!enet_alloc_desc_ring(sc, &sc->txc_ring, 65536))
		return false;
	enet_setup_ring(sc, &sc->txc_ring);

	union enet_dst_ring dst_ring = { .rm = sc->rm };

	dst_ring.num = sc->rx_ring.num;
	sc->rx_ring.dst_ring = dst_ring;

	dst_ring.num = sc->txc_ring.num;
	sc->tx_ring.dst_ring = dst_ring;

	{
		size_t buf_size = 2048;
		if (!enet_alloc_desc_ring_buf(sc, &sc->rx_ring, buf_size * sc->rx_ring.slots, buf_size))
			return false;

		for (int i = 0; i < sc->rx_ring.slots; i++) {
			struct enet_desc16 desc = {
				.info = i,
				.fpqnum = sc->rx_ring.dst_ring.val,
				.stash = 3,
				.addr = sc->rx_ring.buf.dmac_addr + buf_size * i,
				.len = buf_size,
				.coherent = 1,
			};

			struct enet_desc16 *desc_ptr = (struct enet_desc16 *)sc->rx_ring.desc.addr + i;

			desc_ptr->m0 = desc.m0;
			desc_ptr->m1 = desc.m1;
			dsb();
			enet_cmd_write(sc, &sc->rx_ring, ENET_RING_CMD_INC_DEC, 1);
		}
	}
	{
		size_t buf_size = 2048;
		if (!enet_alloc_desc_ring_buf(sc, &sc->tx_ring, buf_size * sc->tx_ring.slots, buf_size))
			return false;
	}

	union {
		struct {
			uint32_t		: 16;
			uint32_t ip_protocol0	: 2;
			uint32_t 		: 13;
			uint32_t en0		: 1;
		};
		uint32_t val;
	} cle_bypass0;

	cle_bypass0.val = enet_csr_read(sc, ENET_CSR_CLE_BYPASS0);
	cle_bypass0.en0 = 1;
	cle_bypass0.ip_protocol0 = 3;
	enet_csr_write(sc, ENET_CSR_CLE_BYPASS0, cle_bypass0.val);

	union {
		struct {
			uint32_t dstqid0	: 12;
			uint32_t 		: 4;
			uint32_t fpsel0		: 4;
			uint32_t 		: 12;
		};
		uint32_t val;
	} cle_bypass1;

	cle_bypass1.val = enet_csr_read(sc, ENET_CSR_CLE_BYPASS1);
	dst_ring.num = sc->rxc_ring.num;
	cle_bypass1.dstqid0 = dst_ring.val;
	cle_bypass1.fpsel0 = sc->rx_ring.id.bufnum - 0x20;
	enet_csr_write(sc, ENET_CSR_CLE_BYPASS1, cle_bypass1.val);

	enet_gmac_init(sc);

	return true;
}

static bool
enet_get_macaddress(struct enet_sc *sc)
{
	int len;
	pnode_t node = enet_get_node(sc);

	if (node < 0)
		return false;

	len = prom_getproplen(node, "local-mac-address");
	if (len != 6) {
		return false;
	}
	prom_getprop(node, "local-mac-address", (caddr_t)sc->dev_addr);

	return true;
}

static bool
enet_get_phy(struct enet_sc *sc)
{
	int len;
	int phy_handle;

	pnode_t node = enet_get_node(sc);
	if (node < 0)
		return false;

	len = prom_getproplen(node, "phy-handle");
	if (len != sizeof(int)) {
		return false;
	}
	prom_getprop(node, "phy-handle", (caddr_t)&phy_handle);

	pnode_t phy_node = prom_findnode_by_phandle(ntohl(phy_handle));
	if (phy_node <= 0) {
		return false;
	}

	len = prom_getproplen(phy_node, "reg");
	if (len != sizeof(int)) {
		return false;
	}
	int phy_id;
	prom_getprop(phy_node, "reg", (caddr_t)&phy_id);
	sc->phy_id = ntohl(phy_id);

	return true;
}

static void
enet_get_portid(struct enet_sc *sc)
{
	sc->port = 0;
	pnode_t node = enet_get_node(sc);

	if (node > 0) {
		int portid;
		int len = prom_getproplen(node, "port-id");
		if (len == sizeof(int)) {
			prom_getprop(node, "port-id", (caddr_t)&portid);
			sc->port = ntohl(portid);
		}
	}
}

static void
enet_mii_write(void *arg, uint8_t phy, uint8_t reg, uint16_t value)
{
	struct enet_sc *sc = arg;
	uint32_t done;
	uint8_t wait = 10;

	union {
		struct {
			uint32_t reg	: 8;
			uint32_t phy	: 8;
			uint32_t 	: 16;
		};
		uint32_t val;
	} addr = {
		.reg = reg,
		.phy = phy,
	};

	enet_mac_write(sc, ENET_MAC_MII_MGMT_ADDRESS, addr.val);
	enet_mac_write(sc, ENET_MAC_MII_MGMT_COMMAND, 0);
	enet_mac_write(sc, ENET_MAC_MII_MGMT_CONTROL, value);
	do {
		enet_usecwait(10);
		done = enet_mac_read(sc, ENET_MAC_MII_MGMT_INDICATORS);
	} while ((done & ENET_MAC_MII_MGMT_INDICATORS_BUSY) && wait--);

	if (done & ENET_MAC_MII_MGMT_INDICATORS_BUSY) {
		prom_printf("MII_MGMT write failed\n");
	}
}

static uint16_t
enet_mii_read(void *arg, uint8_t phy, uint8_t reg)
{
	struct enet_sc *sc = arg;

	uint32_t data, done;
	uint8_t wait = 10;

	union {
		struct {
			uint32_t reg	: 8;
			uint32_t phy	: 8;
			uint32_t 	: 16;
		};
		uint32_t val;
	} addr = {
		.reg = reg,
		.phy = phy,
	};

	enet_mac_write(sc, ENET_MAC_MII_MGMT_ADDRESS, addr.val);
	enet_mac_write(sc, ENET_MAC_MII_MGMT_COMMAND, ENET_MAC_MII_MGMT_COMMAND_READ);
	do {
		enet_usecwait(10);
		done = enet_mac_read(sc, ENET_MAC_MII_MGMT_INDICATORS);
	} while ((done & ENET_MAC_MII_MGMT_INDICATORS_BUSY) && wait--);

	if (done & ENET_MAC_MII_MGMT_INDICATORS_BUSY) {
		prom_printf("MII_MGMT read failed\n");
		return 0xffff;
	}

	data = enet_mac_read(sc, ENET_MAC_MII_MGMT_STATUS);
	enet_mac_write(sc, ENET_MAC_MII_MGMT_COMMAND, 0);

	return data;
}

static struct enet_sc enet;

static bool
enet_map_regs(struct enet_sc *sc)
{
	pnode_t node = enet_get_node(sc);
	if (node < 0)
		return false;

	uint64_t base;
	if (prom_get_reg(node, ENET_CSR, &base))
		return false;
	sc->reg[ENET_CSR].addr = (void *)base;

	if (prom_get_reg(node, RING_CSR, &base))
		return false;
	sc->reg[RING_CSR].addr = (void *)base;

	if (prom_get_reg(node, RING_CMD, &base))
		return false;
	sc->reg[RING_CMD].addr = (void *)base;

	return true;
}

static int
ethernet_open(const char *unused)
{
	struct enet_sc *sc = &enet;
	int i;

	if (!enet_get_macaddress(sc)) {
		return -1;
	}
	if (!enet_get_phy(sc)) {
		return -1;
	}
	enet_get_portid(sc);

	if (!enet_map_regs(sc))
		return -1;

	if (!enet_open(sc))
		return -1;

	enet_mii_write(sc, sc->phy_id, MII_CONTROL, MII_CONTROL_RESET);
	enet_usecwait(1000);
	for (i = 0; i < 100000; i++) {
		enet_usecwait(1000);
		int reg = enet_mii_read(sc, sc->phy_id, MII_CONTROL);
		if (reg != 0xffff && (reg & MII_CONTROL_RESET) == 0)
			break;
	}
	if (i == 100000)
		return -1;

	uint16_t advert = MII_AN_SELECTOR_8023;
	advert |= MII_ABILITY_100BASE_T4;
	advert |= MII_ABILITY_100BASE_TX_FD;
	advert |= MII_ABILITY_100BASE_TX;
	advert |= MII_ABILITY_10BASE_T_FD;
	advert |= MII_ABILITY_10BASE_T;

	uint16_t gigctrl =  MII_MSCONTROL_1000T_FD | MII_MSCONTROL_1000T;
	uint16_t bmcr = MII_CONTROL_ANE | MII_CONTROL_RSAN | MII_CONTROL_1GB | MII_CONTROL_FDUPLEX;

	enet_mii_write(sc, sc->phy_id, MII_AN_ADVERT, advert);
	enet_mii_write(sc, sc->phy_id, MII_MSCONTROL, gigctrl);
	enet_mii_write(sc, sc->phy_id, MII_CONTROL, bmcr);

	for (;;) {
		enet_usecwait(1000);
		uint16_t bmsr;
		int reg = enet_mii_read(sc, sc->phy_id, MII_STATUS);
		if (reg == 0xffff)
			continue;
		bmsr = reg;
		if (bmsr & MII_STATUS_LINKUP)
			break;
	}
	uint16_t lpar = enet_mii_read(sc, sc->phy_id, MII_AN_LPABLE);
	uint16_t msstat = enet_mii_read(sc, sc->phy_id, MII_MSSTATUS);
	if (msstat & MII_MSSTATUS_LP1000T_FD) {
		sc->phy_speed = 1000;
	} else if (lpar & MII_ABILITY_100BASE_TX_FD) {
		sc->phy_speed = 100;
	} else if (lpar & MII_ABILITY_100BASE_TX) {
		sc->phy_speed = 100;
	} else {
		sc->phy_speed = 10;
	}

	enet_gmac_init(sc);
	enet_gmac_enable(sc);

	phandle_t chosen = prom_chosennode();
	char *str;
	str = "bootp";
	prom_setprop(chosen, "net-config-strategy", (caddr_t)str, strlen(str) + 1);
	str = "ethernet,100,rj45,full";
	prom_setprop(chosen, "network-interface-type", (caddr_t)str, strlen(str) + 1);

	return 0;
}

static ssize_t
ethernet_send(int dev, caddr_t data, size_t packet_length, uint_t startblk)
{
	struct enet_sc *sc = &enet;

	int index = sc->tx_ring.index;

	size_t buf_size = 2048;
	void *buf = sc->tx_ring.buf.addr + buf_size * index;

	memcpy(buf, data, packet_length);
	menet_send_buf(sc, sc->tx_ring.buf.dmac_addr + buf_size * index, packet_length);

	while (menet_txc(sc) == 0) {}

	return packet_length;
}

static ssize_t
ethernet_recv(int dev, caddr_t buf, size_t buf_len, uint_t startblk)
{
	struct enet_sc *sc = &enet;
	int rxc_index = sc->rxc_ring.index;

	if (!menet_recv_available(sc))
		return 0;

	struct enet_desc *desc_ptr = (struct enet_desc *)(sc->rxc_ring.desc.addr) + rxc_index;

	size_t len = 0;

	if (desc_ptr->lerr <= 2) {
		len = desc_ptr->len - 4;
	}

	menet_recv(sc, buf, len);
	return len;
}

static int
ethernet_getmacaddr(ihandle_t fd, caddr_t ea)
{
	memcpy(ea, enet.dev_addr, 6);
	return 0;
}

static int
enet_match(const char *path)
{
	if (strcmp(path, MENET_PATH) == 0)
		return 1;
	return 0;
}

static struct prom_dev enet_dev =
{
	.match = enet_match,
	.open = ethernet_open,
	.write = ethernet_send,
	.read = ethernet_recv,
	.getmacaddr = ethernet_getmacaddr,
};

void init_menet(void)
{
	prom_register(&enet_dev);
}

