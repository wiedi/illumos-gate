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
#include <sys/conf.h>
#include <sys/modctl.h>
#include <sys/vlan.h>
#include <sys/mac.h>
#include <sys/mac_ether.h>
#include <sys/strsun.h>
#include <sys/miiregs.h>
#include <sys/sunndi.h>
#include <sys/ndi_impldefs.h>
#include <sys/ddi_impldefs.h>
#include "enet.h"

static const size_t pkt_size = 2048;

static void
enet_csr_write(struct enet_sc *sc, uint32_t offset, uint32_t val)
{
	void *addr = sc->reg[ENET_CSR].addr + offset;
	ddi_put32(sc->reg[ENET_CSR].handle, addr, val);
}

static uint32_t
enet_csr_read(struct enet_sc *sc, uint32_t offset)
{
	void *addr = sc->reg[ENET_CSR].addr + offset;
	return ddi_get32(sc->reg[ENET_CSR].handle, addr);
}

static void
enet_ring_csr_write(struct enet_sc *sc, uint32_t offset, uint32_t val)
{
	void *addr = sc->reg[RING_CSR].addr + offset;
	ddi_put32(sc->reg[RING_CSR].handle, addr, val);
}

static uint32_t
enet_ring_csr_read(struct enet_sc *sc, uint32_t offset)
{
	void *addr = sc->reg[RING_CSR].addr + offset;
	return ddi_get32(sc->reg[RING_CSR].handle, addr);
}

static void
enet_cmd_write(struct enet_sc *sc, struct enet_desc_ring *ring, uint32_t offset, uint32_t val)
{
	void *addr = ring->cmd_base + offset;
	ddi_put32(sc->reg[RING_CMD].handle, addr, val);
}

static uint32_t
enet_cmd_read(struct enet_sc *sc, struct enet_desc_ring *ring, uint32_t offset)
{
	void *addr = ring->cmd_base + offset;
	return ddi_get32(sc->reg[RING_CMD].handle, addr);
}

static void
enet_usecwait(int usec)
{
	drv_usecwait(usec);
}

static pnode_t
enet_get_node(struct enet_sc *sc)
{
	return ddi_get_nodeid(sc->dip);
}
static void
enet_mutex_enter(struct enet_sc *sc)
{
	mutex_enter(&sc->intrlock);
}
static void
enet_mutex_exit(struct enet_sc *sc)
{
	mutex_exit(&sc->intrlock);
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

static uint32_t
enet_ring_len(struct enet_sc *sc, struct enet_desc_ring *ring)
{
	union {
		struct {
			uint32_t		: 1;
			uint32_t nummsginq	: 16;
			uint32_t		: 15;
		};
		uint32_t val;
	} ring_state;

	ring_state.val = enet_cmd_read(sc, ring, 4);

	return ring_state.nummsginq;
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

	mc2.full_duplex = ((sc->phy_duplex == LINK_DUPLEX_HALF)? 0: 1);

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
	int index = sc->tx_ring.ring.index;

	struct enet_desc desc = {
		.addr = addr,
		.len = len,
		.ic = 1,
		.coherent = 1,
		.type = 1,	// ethernet
		.henqnum = sc->tx_ring.ring.dst_ring.val
	};

	struct enet_desc *desc_ptr = (struct enet_desc *)sc->tx_ring.ring.desc.addr + index;
	desc_ptr->m0 = desc.m0;
	desc_ptr->m1 = desc.m1;
	desc_ptr->m2 = desc.m2;
	desc_ptr->m3 = desc.m3;
	dsb();

	enet_cmd_write(sc, &sc->tx_ring.ring, ENET_RING_CMD_INC_DEC, 1);

	sc->tx_ring.ring.index = (index + 1) % sc->tx_ring.ring.slots;
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

static ddi_device_acc_attr_t mem_acc_attr = {
	DDI_DEVICE_ATTR_V0,
	DDI_NEVERSWAP_ACC,
	DDI_STRICTORDER_ACC,
};

static ddi_device_acc_attr_t reg_acc_attr = {
	DDI_DEVICE_ATTR_V0,
	DDI_NEVERSWAP_ACC,
	DDI_STRICTORDER_ACC,
};

static ddi_dma_attr_t dma_attr = {
	DMA_ATTR_V0,			/* dma_attr_version	*/
	0x0000000000000000ull,		/* dma_attr_addr_lo	*/
	0xFFFFFFFFFFFFFFFFull,		/* dma_attr_addr_hi	*/
	0x00000000FFFFFFFFull,		/* dma_attr_count_max	*/
	0x0000000000000001ull,		/* dma_attr_align	*/
	0x00000FFF,			/* dma_attr_burstsizes	*/
	0x00000001,			/* dma_attr_minxfer	*/
	0xFFFFFFFFFFFFFFFFull,		/* dma_attr_maxxfer	*/
	0xFFFFFFFFFFFFFFFFull,		/* dma_attr_seg		*/
	1,				/* dma_attr_sgllen	*/
	0x00000001,			/* dma_attr_granular	*/
	DDI_DMA_FLAGERR			/* dma_attr_flags	*/
};

static void
enet_free_rx_packet(struct enet_rx_packet *pkt)
{
	struct enet_sc *sc = pkt->sc;
	if (sc->rx_pkt_num < RX_PKT_NUM_MAX) {
		pkt->mp = desballoc((unsigned char *)pkt->dma.addr, pkt_size, BPRI_MED, &pkt->free_rtn);
	} else {
		pkt->mp = NULL;
	}
	if (pkt->mp == NULL) {
		ddi_dma_unbind_handle(pkt->dma.dma_handle);
		ddi_dma_mem_free(&pkt->dma.mem_handle);
		ddi_dma_free_handle(&pkt->dma.dma_handle);
		kmem_free(pkt, sizeof(struct enet_rx_packet));
	} else {
		mutex_enter(&sc->rx_pkt_lock);
		pkt->next = sc->rx_pkt_free;
		sc->rx_pkt_free = pkt;
		sc->rx_pkt_num++;
		mutex_exit(&sc->rx_pkt_lock);
	}
}

static struct enet_rx_packet *
enet_alloc_rx_packet(struct enet_sc *sc)
{
	struct enet_rx_packet *pkt;
	ddi_dma_attr_t desc_dma_attr = dma_attr;
	desc_dma_attr.dma_attr_align = pkt_size;

	mutex_enter(&sc->rx_pkt_lock);
	pkt = sc->rx_pkt_free;
	if (pkt) {
		sc->rx_pkt_free = pkt->next;
		sc->rx_pkt_num--;
	}
	mutex_exit(&sc->rx_pkt_lock);

	if (pkt == NULL) {
		pkt = (struct enet_rx_packet *)kmem_zalloc(sizeof(struct enet_rx_packet), KM_NOSLEEP);
		if (pkt) {
			if (ddi_dma_alloc_handle(sc->dip, &desc_dma_attr, DDI_DMA_SLEEP, 0, &pkt->dma.dma_handle) != DDI_SUCCESS) {
				kmem_free(pkt, sizeof(struct enet_rx_packet));
				pkt= NULL;
			}
		}

		if (pkt) {
			if (ddi_dma_mem_alloc(pkt->dma.dma_handle, pkt_size, &mem_acc_attr, DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, 0,
				    &pkt->dma.addr, &pkt->dma.size, &pkt->dma.mem_handle)) {
				ddi_dma_free_handle(&pkt->dma.dma_handle);
				kmem_free(pkt, sizeof(struct enet_rx_packet));
				pkt= NULL;
			} else {
				ASSERT(pkt->dma.size >= pkt_size);
			}
		}

		if (pkt) {
			ddi_dma_cookie_t cookie;
			uint_t ccount;
			int result = ddi_dma_addr_bind_handle(pkt->dma.dma_handle, NULL, pkt->dma.addr, pkt->dma.size, DDI_DMA_RDWR | DDI_DMA_CONSISTENT,
			    DDI_DMA_SLEEP, NULL, &cookie, &ccount);
			if (result == DDI_DMA_MAPPED) {
				ASSERT(ccount == 1);
				pkt->dma.dmac_addr = cookie.dmac_laddress;
				pkt->sc = sc;
				pkt->free_rtn.free_func = enet_free_rx_packet;
				pkt->free_rtn.free_arg = (char *)pkt;

				pkt->mp = desballoc((unsigned char *)pkt->dma.addr, pkt_size, BPRI_MED, &pkt->free_rtn);
				if (pkt->mp == NULL) {
					ddi_dma_unbind_handle(pkt->dma.dma_handle);
					ddi_dma_mem_free(&pkt->dma.mem_handle);
					ddi_dma_free_handle(&pkt->dma.dma_handle);
					kmem_free(pkt, sizeof(struct enet_rx_packet));
					pkt= NULL;
				}
			} else {
				ddi_dma_mem_free(&pkt->dma.mem_handle);
				ddi_dma_free_handle(&pkt->dma.dma_handle);
				kmem_free(pkt, sizeof(struct enet_rx_packet));
				pkt= NULL;
			}
		}
	}

	return pkt;
}

static bool
enet_alloc_desc_ring(struct enet_sc *sc, struct enet_desc_ring *desc_ring, size_t size)
{
	ddi_dma_attr_t desc_dma_attr = dma_attr;
	desc_dma_attr.dma_attr_align = size;

	if (ddi_dma_alloc_handle(sc->dip, &desc_dma_attr, DDI_DMA_SLEEP, 0, &desc_ring->desc.dma_handle) != DDI_SUCCESS) {
		return false;
	}

	size_t real_length;
	if (ddi_dma_mem_alloc(desc_ring->desc.dma_handle, size, &mem_acc_attr, DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, 0,
		    &desc_ring->desc.addr, &real_length, &desc_ring->desc.mem_handle)) {
		return false;
	}

	ddi_dma_cookie_t cookie;
	uint_t ccount;
	int result = ddi_dma_addr_bind_handle(
	    desc_ring->desc.dma_handle, NULL, desc_ring->desc.addr, real_length, DDI_DMA_RDWR | DDI_DMA_CONSISTENT,
	    DDI_DMA_SLEEP, NULL, &cookie, &ccount);
	if (result == DDI_DMA_MAPPED) {
		ASSERT(ccount == 1);
	} else {
		return false;
	}
	ASSERT(real_length >= size);
	desc_ring->desc.dmac_addr = cookie.dmac_laddress;

	return true;
}

static bool
enet_open(struct enet_sc *sc)
{
	int len;

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

	sc->rx_ring.ring.is_bufpool = true;
	sc->rx_ring.ring.num = ring_num++;
	ring_id.owner = (sc->port == 0? ENET_RING_OWNER_ETH0: ENET_RING_OWNER_ETH1);
	ring_id.bufnum = bufpool_bufnum++;
	sc->rx_ring.ring.id = ring_id;
	sc->rx_ring.ring.cmd_base = enet_ring_cmd_base(sc, &sc->rx_ring.ring);
	sc->rx_ring.ring.cfgsize = ENET_RING_CFGSIZE_64KB;
	sc->rx_ring.ring.slots = 65536 / sizeof(struct enet_desc16);
	if (!enet_alloc_desc_ring(sc, &sc->rx_ring.ring, 65536))
		return false;
	enet_setup_ring(sc, &sc->rx_ring.ring);

	sc->tx_ring.ring.is_bufpool = false;
	sc->tx_ring.ring.num = ring_num++;
	ring_id.owner = (sc->port == 0? ENET_RING_OWNER_ETH0: ENET_RING_OWNER_ETH1);
	ring_id.bufnum = eth_bufnum++;
	sc->tx_ring.ring.id = ring_id;
	sc->tx_ring.ring.cmd_base = enet_ring_cmd_base(sc, &sc->tx_ring.ring);
	sc->tx_ring.ring.cfgsize = ENET_RING_CFGSIZE_64KB;
	sc->tx_ring.ring.slots = 65536 / sizeof(struct enet_desc);
	if (!enet_alloc_desc_ring(sc, &sc->tx_ring.ring, 65536))
		return false;
	enet_setup_ring(sc, &sc->tx_ring.ring);

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

	dst_ring.num = sc->rx_ring.ring.num;
	sc->rx_ring.ring.dst_ring = dst_ring;

	dst_ring.num = sc->txc_ring.num;
	sc->tx_ring.ring.dst_ring = dst_ring;

	{
		sc->rx_ring.pkt = (struct enet_rx_packet **)kmem_zalloc(sizeof(struct enet_rx_packet *) * sc->rx_ring.ring.slots, KM_SLEEP);
		for (int i = 0; i < sc->rx_ring.ring.slots; i++) {
			sc->rx_ring.pkt[i] = enet_alloc_rx_packet(sc);
			struct enet_desc16 desc = {
				.info = i,
				.fpqnum = sc->rx_ring.ring.dst_ring.val,
				.stash = 3,
				.addr = sc->rx_ring.pkt[i]->dma.dmac_addr,
				.len = sc->rx_ring.pkt[i]->dma.size,
				.coherent = 1,
			};

			struct enet_desc16 *desc_ptr = (struct enet_desc16 *)sc->rx_ring.ring.desc.addr + i;

			desc_ptr->m0 = desc.m0;
			desc_ptr->m1 = desc.m1;
			dsb();
			enet_cmd_write(sc, &sc->rx_ring.ring, ENET_RING_CMD_INC_DEC, 1);
		}
	}
	{
		ddi_dma_attr_t desc_dma_attr = dma_attr;
		desc_dma_attr.dma_attr_align = pkt_size;

		sc->tx_ring.mblk = (mblk_t **)kmem_zalloc(sizeof(mblk_t *) * sc->tx_ring.ring.slots, KM_SLEEP);
		sc->tx_ring.dma = (struct enet_dma *)kmem_zalloc(sizeof(struct enet_dma) * sc->tx_ring.ring.slots, KM_SLEEP);
		for (int i = 0; i < sc->tx_ring.ring.slots; i++) {
			ddi_dma_cookie_t cookie;
			uint_t ccount;
			if (ddi_dma_alloc_handle(sc->dip, &desc_dma_attr, DDI_DMA_SLEEP, 0, &sc->tx_ring.dma[i].dma_handle) != DDI_SUCCESS) {
				return false;
			}
			if (ddi_dma_mem_alloc(sc->tx_ring.dma[i].dma_handle, pkt_size, &mem_acc_attr, DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, 0,
				    &sc->tx_ring.dma[i].addr, &sc->tx_ring.dma[i].size, &sc->tx_ring.dma[i].mem_handle)) {
				return false;
			}
			if (ddi_dma_addr_bind_handle(sc->tx_ring.dma[i].dma_handle, NULL, sc->tx_ring.dma[i].addr, sc->tx_ring.dma[i].size, DDI_DMA_RDWR | DDI_DMA_CONSISTENT,
				    DDI_DMA_SLEEP, NULL, &cookie, &ccount) != DDI_DMA_MAPPED) {
				return false;
			}
			ASSERT(ccount == 1);
			sc->tx_ring.dma[i].dmac_addr = cookie.dmac_laddress;
		}
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
	cle_bypass1.fpsel0 = sc->rx_ring.ring.id.bufnum - 0x20;
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

	ndi_prop_update_int(DDI_DEV_T_NONE, sc->dip, "phy-addr", phy_id);

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
	uint32_t wr_data = 0;
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

	enet_mutex_enter(sc);

	enet_mac_write(sc, ENET_MAC_MII_MGMT_ADDRESS, addr.val);
	enet_mac_write(sc, ENET_MAC_MII_MGMT_COMMAND, 0);
	enet_mac_write(sc, ENET_MAC_MII_MGMT_CONTROL, value);
	do {
		enet_usecwait(10);
		done = enet_mac_read(sc, ENET_MAC_MII_MGMT_INDICATORS);
	} while ((done & ENET_MAC_MII_MGMT_INDICATORS_BUSY) && wait--);

	if (done & ENET_MAC_MII_MGMT_INDICATORS_BUSY) {
		cmn_err(CE_WARN, "MII_MGMT write failed");
	}
	enet_mutex_exit(sc);
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

	enet_mutex_enter(sc);

	enet_mac_write(sc, ENET_MAC_MII_MGMT_ADDRESS, addr.val);
	enet_mac_write(sc, ENET_MAC_MII_MGMT_COMMAND, ENET_MAC_MII_MGMT_COMMAND_READ);
	do {
		enet_usecwait(10);
		done = enet_mac_read(sc, ENET_MAC_MII_MGMT_INDICATORS);
	} while ((done & ENET_MAC_MII_MGMT_INDICATORS_BUSY) && wait--);

	if (done & ENET_MAC_MII_MGMT_INDICATORS_BUSY) {
		enet_mutex_exit(sc);
		cmn_err(CE_WARN, "MII_MGMT read failed");
		return 0xffff;
	}

	data = enet_mac_read(sc, ENET_MAC_MII_MGMT_STATUS);
	enet_mac_write(sc, ENET_MAC_MII_MGMT_COMMAND, 0);

	enet_mutex_exit(sc);

	return data;
}

static int
menet_probe(dev_info_t *dip)
{
	int len;
	char buf[80];
	pnode_t node = ddi_get_nodeid(dip);
	if (node < 0)
		return (DDI_PROBE_FAILURE);


	len = prom_getproplen(node, "phy-connection-type");
	if (len <= 0 || len >= sizeof(buf))
		return (DDI_PROBE_FAILURE);

	prom_getprop(node, "phy-connection-type", buf);

	buf[len] = '\0';
	if (strcmp(buf, "rgmii")) {
		return (DDI_PROBE_FAILURE);
	}

	len = prom_getproplen(node, "status");
	if (len <= 0 || len >= sizeof(buf))
		return (DDI_PROBE_FAILURE);

	prom_getprop(node, "status", (caddr_t)buf);
	if (strcmp(buf, "ok") != 0)
		return (DDI_PROBE_FAILURE);

	return (DDI_PROBE_SUCCESS);
}

static int
menet_map_regs(struct enet_sc *sc)
{
	for (int i = 0; i < NUM_ENET_REGS; i++) {
		if (ddi_regs_map_setup(sc->dip, i, &sc->reg[i].addr, 0, 0, &reg_acc_attr, &sc->reg[i].handle) != DDI_SUCCESS) {
			for (int j = 0; j < i; j++)
				ddi_regs_map_free(&sc->reg[j].handle);
			return (DDI_FAILURE);
		}
	}
	return DDI_SUCCESS;
}

static void
enet_mii_notify(void *arg, link_state_t link)
{
	struct enet_sc *sc = arg;
	uint32_t gmac;
	uint32_t gpcr;
	link_flowctrl_t fc;
	link_duplex_t duplex;
	int speed;

	fc = mii_get_flowctrl(sc->mii_handle);
	duplex = mii_get_duplex(sc->mii_handle);
	speed = mii_get_speed(sc->mii_handle);

	enet_mutex_enter(sc);

	if (link == LINK_STATE_UP) {
		sc->phy_speed = speed;
		sc->phy_duplex = duplex;
		enet_gmac_init(sc);
		enet_gmac_enable(sc);
	} else {
		sc->phy_speed = -1;
		sc->phy_duplex = LINK_DUPLEX_UNKNOWN;
	}

	enet_mutex_exit(sc);

	mac_link_update(sc->mac_handle, link);
}

static mii_ops_t menet_mii_ops = {
	MII_OPS_VERSION,
	enet_mii_read,
	enet_mii_write,
	enet_mii_notify,
	NULL	/* reset */
};

static int
menet_phy_install(struct enet_sc *sc)
{
	sc->mii_handle = mii_alloc(sc, sc->dip, &menet_mii_ops);
	if (sc->mii_handle == NULL) {
		return (DDI_FAILURE);
	}
	//mii_set_pauseable(sc->mii_handle, B_FALSE, B_FALSE);

	return DDI_SUCCESS;
}

static mblk_t *
menet_send(struct enet_sc *sc, mblk_t *mp)
{
	uint32_t tx_len = enet_ring_len(sc, &sc->tx_ring.ring);
	uint32_t txc_len = enet_ring_len(sc, &sc->txc_ring);

	if (sc->txc_ring.slots <= txc_len + tx_len)
		return mp;

	int index = sc->tx_ring.ring.index;
	size_t mblen = 0;
	size_t frags = 0;

	for (mblk_t *bp = mp; bp != NULL; bp = bp->b_cont) {
		size_t frag_len = MBLKL(bp);
		if (frag_len == 0)
			continue;
		frags++;
		mblen += frag_len;
	}

	mcopymsg(mp, sc->tx_ring.dma[index].addr);

	menet_send_buf(sc, sc->tx_ring.dma[index].dmac_addr, mblen);

	return (NULL);
}

static mblk_t *
menet_m_tx(void *arg, mblk_t *mp)
{
	struct enet_sc *sc = arg;
	mblk_t *nmp;

	enet_mutex_enter(sc);

	while (mp != NULL) {
		nmp = mp->b_next;
		mp->b_next = NULL;
		if ((mp = menet_send(sc, mp)) != NULL) {
			mp->b_next = nmp;
			break;
		}
		mp = nmp;
	}

	enet_mutex_exit(sc);

	return (mp);
}


static mblk_t *
menet_rx(struct enet_sc *sc)
{
	int rx_index = sc->rx_ring.ring.index;
	int rxc_index = sc->rxc_ring.index;

	if (!menet_recv_available(sc))
		return NULL;

	struct enet_desc *desc_ptr = (struct enet_desc *)(sc->rxc_ring.desc.addr) + rxc_index;
	size_t len = 0;
	static mblk_t *mp = NULL;

	if (desc_ptr->lerr <= 2) {
		len = desc_ptr->len - 4;
	}

	if (len > 0) {
		struct enet_rx_packet *pkt = enet_alloc_rx_packet(sc);
		if (pkt) {
			mp = sc->rx_ring.pkt[rx_index]->mp;
			mp->b_wptr += len;
			sc->rx_ring.pkt[rx_index] = pkt;
		}
	}

	{
		struct enet_rx_packet *pkt = sc->rx_ring.pkt[rx_index];
		struct enet_desc16 desc = {
			.info = rx_index,
			.fpqnum = sc->rx_ring.ring.dst_ring.val,
			.stash = 3,
			.addr = pkt->dma.dmac_addr,
			.len = pkt->dma.size,
			.coherent = 1,
		};

		struct enet_desc16 *desc16_ptr = (struct enet_desc16 *)sc->rx_ring.ring.desc.addr + rx_index;
		desc16_ptr->m0 = desc.m0;
		desc16_ptr->m1 = desc.m1;
		dsb();
	}

	{
		desc_ptr->m1 = ENET_DESC_M1_EMPTY;
		dsb();
	}

	enet_cmd_write(sc, &sc->rxc_ring, ENET_RING_CMD_INC_DEC, -1);
	enet_cmd_write(sc, &sc->rx_ring.ring, ENET_RING_CMD_INC_DEC, 1);

	sc->rx_ring.ring.index = (rx_index + 1) % sc->rx_ring.ring.slots;
	sc->rxc_ring.index = (rxc_index + 1) % sc->rxc_ring.slots;

	return mp;
}

static paddr_t
enet_va_to_pa(void *va)
{
	uint64_t daif = read_daif();
	set_daif(0xF);
	write_s1e1w((uint64_t)va);
	uint64_t pa = read_par_el1();
	write_daif(daif);
	if (pa & 1)
		return (paddr_t)(-1ul);

	return ((pa & PAR_PA_MASK) | ((uint64_t)va & MMU_PAGEOFFSET));
}


static uint_t
menet_intr(caddr_t arg, caddr_t unused)
{
	struct enet_sc *sc = (struct enet_sc *)arg;

	for (;;) {
		mblk_t *mp = NULL;
		enet_mutex_enter(sc);
		mp = menet_rx(sc);
		enet_mutex_exit(sc);

		if (mp)
			mac_rx(sc->mac_handle, NULL, mp);
		else
			break;
	}

	{
		int txc = 0;

		enet_mutex_enter(sc);
		txc = menet_txc(sc);
		enet_mutex_exit(sc);

		if (txc)
			mac_tx_update(sc->mac_handle);
	}

	return (DDI_INTR_CLAIMED);
}


static int menet_attach(dev_info_t *dip, ddi_attach_cmd_t cmd);
static int
menet_detach(dev_info_t *a, ddi_detach_cmd_t b)
{
	return DDI_FAILURE;
}

static int
menet_quiesce(dev_info_t *a)
{
	return DDI_FAILURE;
}

static int
menet_m_setpromisc(void *a, boolean_t b)
{
	return 0;
}

static int
menet_m_multicst(void *a, boolean_t b, const uint8_t *c)
{
	return 0;
}

static int
menet_m_unicst(void *arg, const uint8_t *dev_addr)
{
	struct enet_sc *sc = arg;

	enet_mutex_enter(sc);

	memcpy(sc->dev_addr, dev_addr, 6);
	uint32_t old = enet_mac_read(sc, ENET_MAC_CONFIG_1);

	enet_gmac_disable(sc);
	enet_gmac_set_mac_addr(sc);
	enet_gmac_enable(sc);

	enet_mac_write(sc, ENET_MAC_CONFIG_1, old);

	enet_mutex_exit(sc);

	return 0;
}

static int
menet_m_start(void *arg)
{
	struct enet_sc *sc = arg;

	enet_mutex_enter(sc);

	sc->running = 1;
	enet_gmac_enable(sc);

	if (ddi_intr_enable(sc->intr_handle) != DDI_SUCCESS) {
		enet_gmac_disable(sc);
		sc->running = 0;
		enet_mutex_exit(sc);
		return EIO;
	}

	enet_mutex_exit(sc);

	mii_start(sc->mii_handle);

	return 0;
}

static void
menet_m_stop(void *arg)
{
	struct enet_sc *sc = arg;

	mii_stop(sc->mii_handle);

	enet_mutex_enter(sc);

	ddi_intr_disable(sc->intr_handle);

	sc->running = 0;
	enet_gmac_disable(sc);

	enet_mutex_exit(sc);
}

static int
menet_m_getstat(void *arg, uint_t stat, uint64_t *val)
{
	struct enet_sc *sc = arg;
	return mii_m_getstat(sc->mii_handle, stat, val);
}

static int
menet_m_setprop(void *arg, const char *name, mac_prop_id_t num, uint_t sz, const void *val)
{
	struct enet_sc *sc = arg;
	return mii_m_setprop(sc->mii_handle, name, num, sz, val);
}

static int
menet_m_getprop(void *arg, const char *name, mac_prop_id_t num, uint_t sz, void *val)
{
	struct enet_sc *sc = arg;
	return mii_m_getprop(sc->mii_handle, name, num, sz, val);
}

static void
menet_m_propinfo(void *arg, const char *name, mac_prop_id_t num, mac_prop_info_handle_t prh)
{
	struct enet_sc *sc = arg;
	mii_m_propinfo(sc->mii_handle, name, num, prh);
}

static void
menet_m_ioctl(void *arg, queue_t *wq, mblk_t *mp)
{
	struct enet_sc *sc = arg;
	if (mii_m_loop_ioctl(sc->mii_handle, wq, mp))
		return;

	miocnak(wq, mp, 0, EINVAL);
}

extern struct mod_ops mod_driverops;

DDI_DEFINE_STREAM_OPS(menet_devops, nulldev, menet_probe, menet_attach,
    menet_detach, nodev, NULL, D_MP, NULL, menet_quiesce);

static struct modldrv menet_modldrv = {
	&mod_driverops,		/* Type of module.  This one is a driver */
	"X-Gene menet",		/* short description */
	&menet_devops		/* driver specific ops */
};

static struct modlinkage menet_modlinkage = {
	MODREV_1,		/* ml_rev */
	{ &menet_modldrv, NULL }	/* ml_linkage */
};

static mac_callbacks_t menet_m_callbacks = {
	0,	/* mc_callbacks */
	menet_m_getstat,	/* mc_getstat */
	menet_m_start,		/* mc_start */
	menet_m_stop,		/* mc_stop */
	menet_m_setpromisc,	/* mc_setpromisc */
	menet_m_multicst,	/* mc_multicst */
	menet_m_unicst,		/* mc_unicst */
	menet_m_tx,		/* mc_tx */
	NULL,
	menet_m_ioctl,		/* mc_ioctl */
	NULL,			/* mc_getcapab */
	NULL,			/* mc_open */
	NULL,			/* mc_close */
	menet_m_setprop,
	menet_m_getprop,
	menet_m_propinfo
};

static int
menet_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	switch (cmd) {
	case DDI_ATTACH:
		break;
	default:
		return (DDI_FAILURE);
	}

	struct enet_sc *sc = kmem_zalloc(sizeof(struct enet_sc), KM_SLEEP);
	ddi_set_driver_private(dip, sc);
	sc->dip = dip;

	mutex_init(&sc->intrlock, NULL, MUTEX_DRIVER, NULL);
	mutex_init(&sc->rx_pkt_lock, NULL, MUTEX_DRIVER, NULL);

	if (!enet_get_macaddress(sc)) {
		return DDI_FAILURE;
	}
	if (menet_map_regs(sc) != DDI_SUCCESS) {
		return DDI_FAILURE;
	}
	if (!enet_get_phy(sc)) {
		return DDI_FAILURE;
	}

	enet_get_portid(sc);

	enet_mutex_enter(sc);
	if (!enet_open(sc)) {
		enet_mutex_exit(sc);
		return DDI_FAILURE;
	}
	enet_mutex_exit(sc);

	mac_register_t *macp;
	if ((macp = mac_alloc(MAC_VERSION)) == NULL) {
		kmem_free(sc, sizeof(struct enet_sc));
		return (DDI_FAILURE);
	}

	macp->m_type_ident = MAC_PLUGIN_IDENT_ETHER;
	macp->m_driver = sc;
	macp->m_dip = dip;
	macp->m_src_addr = sc->dev_addr;
	macp->m_callbacks = &menet_m_callbacks;
	macp->m_min_sdu = 0;
	macp->m_max_sdu = ETHERMTU;
	macp->m_margin = VLAN_TAGSZ;

	if (mac_register(macp, &sc->mac_handle) != 0) {
		return (DDI_FAILURE);
	}

	if (menet_phy_install(sc) != DDI_SUCCESS) {
		return DDI_FAILURE;
	}

	int actual;
	if (ddi_intr_alloc(dip, &sc->intr_handle, DDI_INTR_TYPE_FIXED, 0, 1, &actual, DDI_INTR_ALLOC_STRICT) != DDI_SUCCESS) {
		return (DDI_FAILURE);
	}

	if (ddi_intr_add_handler(sc->intr_handle, menet_intr, sc, NULL) != DDI_SUCCESS) {
		return (DDI_FAILURE);
	}

	return DDI_SUCCESS;
}

int
_init(void)
{
	int i;

	mac_init_ops(&menet_devops, "platmac");

	if ((i = mod_install(&menet_modlinkage)) != 0) {
		mac_fini_ops(&menet_devops);
	}
	return (i);
}

int
_fini(void)
{
	int i;

	if ((i = mod_remove(&menet_modlinkage)) == 0) {
		mac_fini_ops(&menet_devops);
	}
	return (i);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&menet_modlinkage, modinfop));
}
