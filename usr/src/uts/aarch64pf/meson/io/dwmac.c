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

#include <stddef.h>
#include <sys/promif.h>
#include <sys/miiregs.h>
#include <sys/ethernet.h>
#include <sys/byteorder.h>
#include <sys/controlregs.h>
#include <sys/debug.h>
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
#include <sys/gxbb_smc.h>
#include <sys/crc32.h>
#include <sys/sysmacros.h>
#include "dwmac.h"
#include "dwmacreg.h"

#define PKT_SIZE	1536
#define PKT_BUFFER_SIZE	2048

static void dwmac_destroy(struct dwmac_sc *sc);
static void dwmac_m_stop(void *arg);

static void
dcache_flush(void *addr, size_t size)
{
	uintptr_t end = roundup((uintptr_t)addr + size, DCACHE_LINE);
	while ((uintptr_t)addr < end) {
		flush_data_cache((uintptr_t)addr);
		addr = (void *)((uintptr_t)addr + DCACHE_LINE);
	}
	dsb();
}

static void
dwmac_reg_write(struct dwmac_sc *sc, uint32_t offset, uint32_t val)
{
	void *addr = sc->reg.addr + offset;
	ddi_put32(sc->reg.handle, addr, val);
}

static uint32_t
dwmac_reg_read(struct dwmac_sc *sc, uint32_t offset)
{
	void *addr = sc->reg.addr + offset;
	return ddi_get32(sc->reg.handle, addr);
}

static void
dwmac_usecwait(int usec)
{
	drv_usecwait(usec);
}

static pnode_t
dwmac_get_node(struct dwmac_sc *sc)
{
	return ddi_get_nodeid(sc->dip);
}
static void
dwmac_mutex_enter(struct dwmac_sc *sc)
{
	mutex_enter(&sc->intrlock);
}
static void
dwmac_mutex_exit(struct dwmac_sc *sc)
{
	mutex_exit(&sc->intrlock);
}

static void
dwmac_gmac_reset(struct dwmac_sc *sc)
{
	pnode_t node = ddi_get_nodeid(sc->dip);

	struct prom_hwreset hwreset;
	if (prom_get_reset(node, 0, &hwreset) == 0) {
		uint64_t base;
		if (prom_get_reg(hwreset.node, 0, &base) == 0) {
			*(volatile uint32_t *)(SEGKPM_BASE + base + ((0x50 + hwreset.id / 32) << 2)) |= (1u << (hwreset.id % 32));
		}
	}

	uint64_t preg_eth0;
	if (prom_get_reg(node, 1, &preg_eth0) == 0) {
		if (prom_getproplen(node, "mc_val") == sizeof(uint32_t)) {
			uint32_t mc_val;
			prom_getprop(node, "mc_val", (caddr_t)&mc_val);
			mc_val = ntohl(mc_val);
			*(volatile uint32_t *)(preg_eth0 + SEGKPM_BASE) = mc_val;
		}
	}

	dwmac_reg_write(sc, DWMAC_MAC_CONF, 0);

	// stop
	dwmac_reg_write(sc, DWMAC_DMA_OPMODE, 0);
	dwmac_reg_write(sc, DWMAC_DMA_OPMODE, dwmac_reg_read(sc, DWMAC_DMA_OPMODE) | GMAC_DMA_OP_FLUSHTX);
	dwmac_reg_write(sc, DWMAC_DMA_OPMODE, 0);

	// reset
	dwmac_reg_write(sc, DWMAC_DMA_BUSMODE, GMAC_BUSMODE_RESET);
	dsb();
	for (int i = 0; i < 1000; i++) {
		dwmac_usecwait(100);
		if (!(dwmac_reg_read(sc, DWMAC_DMA_BUSMODE) & GMAC_BUSMODE_RESET))
			break;
	}
}

static void
dwmac_gmac_init(struct dwmac_sc *sc)
{
	uint32_t mac_conf = dwmac_reg_read(sc, DWMAC_MAC_CONF);
	mac_conf &= ~(DWMAC_MAC_CONF_FES | DWMAC_MAC_CONF_PS | DWMAC_MAC_CONF_DM);
	mac_conf &= ~(DWMAC_MAC_CONF_TE | DWMAC_MAC_CONF_RE);
	mac_conf |= DWMAC_MAC_CONF_JD;
	mac_conf |= DWMAC_MAC_CONF_BE;
	mac_conf |= DWMAC_MAC_CONF_ACS;
	mac_conf |= DWMAC_MAC_CONF_DCRS;
	dwmac_reg_write(sc, DWMAC_MAC_CONF, mac_conf);

	dwmac_reg_write(sc, DWMAC_MAC_FFILT, DWMAC_MAC_FFILT_HMC);
	dwmac_reg_write(sc, DWMAC_MAC_HTHIGH, 0);
	dwmac_reg_write(sc, DWMAC_MAC_HTLOW, 0);
	dwmac_reg_write(sc, DWMAC_MAC_ADDR0HI, (sc->dev_addr[5] << 8) | sc->dev_addr[4]);
	dwmac_reg_write(sc, DWMAC_MAC_ADDR0LO, (sc->dev_addr[3] << 24) | (sc->dev_addr[2] << 16) | (sc->dev_addr[1] << 8) | sc->dev_addr[0]);

	dwmac_reg_write(sc, DWMAC_MAC_INTMASK, 0xffffffff);
	dwmac_reg_read(sc, DWMAC_MAC_INTR);
	dwmac_reg_read(sc, DWMAC_MII_STATUS);

	uint32_t busmode = 0;
	busmode |= GMAC_BUSMODE_4PBL;
	busmode &= ~GMAC_BUSMODE_PBL;
	busmode |= (8 << GMAC_BUSMODE_PBL_SHIFT);
	busmode &= ~GMAC_BUSMODE_RPBL;
	busmode |= (8 << GMAC_BUSMODE_RPBL_SHIFT);
	dwmac_reg_write(sc, DWMAC_DMA_BUSMODE, busmode);

	dwmac_reg_write(sc, DWMAC_DMA_STATUS, 0xffffffff);
	dwmac_reg_write(sc, DWMAC_DMA_STATUS, 0);
	dwmac_reg_write(sc, DWMAC_DMA_INTENABLE, 0);

	dwmac_reg_write(sc, DWMAC_DMA_TX_ADDR, sc->tx_ring.desc.dmac_addr);
	dwmac_reg_write(sc, DWMAC_DMA_RX_ADDR, sc->rx_ring.desc.dmac_addr);

	dwmac_reg_write(sc, DWMAC_DMA_INTENABLE,
	    GMAC_DMA_INT_TIE | GMAC_DMA_INT_RIE | GMAC_DMA_INT_NIE | GMAC_DMA_INT_AIE | GMAC_DMA_INT_FBE | GMAC_DMA_INT_UNE);
}

static void
dwmac_gmac_update(struct dwmac_sc *sc)
{
	uint32_t mac_conf = dwmac_reg_read(sc, DWMAC_MAC_CONF);
	mac_conf &= ~(DWMAC_MAC_CONF_FES | DWMAC_MAC_CONF_PS | DWMAC_MAC_CONF_DM);

	if (sc->phy_duplex == LINK_DUPLEX_FULL)
		mac_conf |= DWMAC_MAC_CONF_DM;
	if (sc->phy_speed == 100)
		mac_conf |= DWMAC_MAC_CONF_FES;
	if (sc->phy_speed != 1000)
		mac_conf |= DWMAC_MAC_CONF_PS;

	dwmac_reg_write(sc, DWMAC_MAC_CONF, mac_conf);
}


static void
dwmac_gmac_enable(struct dwmac_sc *sc)
{
	uint32_t mac_conf = dwmac_reg_read(sc, DWMAC_MAC_CONF);
	mac_conf |= DWMAC_MAC_CONF_TE;
	mac_conf |= DWMAC_MAC_CONF_RE;
	dwmac_reg_write(sc, DWMAC_MAC_CONF, mac_conf);

	dwmac_reg_write(sc, DWMAC_DMA_OPMODE,
	    GMAC_DMA_OP_OSF |
	    GMAC_DMA_OP_RXSTART | GMAC_DMA_OP_TXSTART |
	    GMAC_DMA_OP_RXSTOREFORWARD | GMAC_DMA_OP_TXSTOREFORWARD);
}

static void
dwmac_gmac_disable(struct dwmac_sc *sc)
{
	uint32_t mac_conf = dwmac_reg_read(sc, DWMAC_MAC_CONF);
	mac_conf &= ~(DWMAC_MAC_CONF_TE | DWMAC_MAC_CONF_RE);
	dwmac_reg_write(sc, DWMAC_MAC_CONF, mac_conf);

	// stop
	dwmac_reg_write(sc, DWMAC_DMA_OPMODE, 0);
	dwmac_reg_write(sc, DWMAC_DMA_OPMODE, dwmac_reg_read(sc, DWMAC_DMA_OPMODE) | GMAC_DMA_OP_FLUSHTX);
	dwmac_reg_write(sc, DWMAC_DMA_OPMODE, 0);
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
	0x00000000FFFFFFFFull,		/* dma_attr_addr_hi	*/
	0x00000000FFFFFFFFull,		/* dma_attr_count_max	*/
	0x0000000000000001ull,		/* dma_attr_align	*/
	0x00000FFF,			/* dma_attr_burstsizes	*/
	0x00000001,			/* dma_attr_minxfer	*/
	0x00000000FFFFFFFFull,		/* dma_attr_maxxfer	*/
	0x00000000FFFFFFFFull,		/* dma_attr_seg		*/
	1,				/* dma_attr_sgllen	*/
	0x00000001,			/* dma_attr_granular	*/
	DDI_DMA_FLAGERR			/* dma_attr_flags	*/
};

static void
dwmac_free_rx_packet(struct dwmac_rx_packet *pkt)
{
	struct dwmac_sc *sc = pkt->sc;
	if (sc->running && sc->rx_pkt_num < RX_PKT_NUM_MAX) {
		pkt->mp = desballoc((unsigned char *)pkt->dma.addr, PKT_BUFFER_SIZE, BPRI_MED, &pkt->free_rtn);
	} else {
		pkt->mp = NULL;
	}
	if (pkt->mp == NULL) {
		ddi_dma_unbind_handle(pkt->dma.dma_handle);
		ddi_dma_mem_free(&pkt->dma.mem_handle);
		ddi_dma_free_handle(&pkt->dma.dma_handle);
		kmem_free(pkt, sizeof(struct dwmac_rx_packet));
	} else {
		mutex_enter(&sc->rx_pkt_lock);
		pkt->next = sc->rx_pkt_free;
		sc->rx_pkt_free = pkt;
		sc->rx_pkt_num++;
		mutex_exit(&sc->rx_pkt_lock);
	}
}

static struct dwmac_rx_packet *
dwmac_alloc_rx_packet(struct dwmac_sc *sc)
{
	struct dwmac_rx_packet *pkt;
	ddi_dma_attr_t desc_dma_attr = dma_attr;
	desc_dma_attr.dma_attr_align = PKT_BUFFER_SIZE;

	mutex_enter(&sc->rx_pkt_lock);
	pkt = sc->rx_pkt_free;
	if (pkt) {
		sc->rx_pkt_free = pkt->next;
		sc->rx_pkt_num--;
	}
	mutex_exit(&sc->rx_pkt_lock);

	if (pkt == NULL) {
		pkt = (struct dwmac_rx_packet *)kmem_zalloc(sizeof(struct dwmac_rx_packet), KM_NOSLEEP);
		if (pkt) {
			if (ddi_dma_alloc_handle(sc->dip, &desc_dma_attr, DDI_DMA_SLEEP, 0, &pkt->dma.dma_handle) != DDI_SUCCESS) {
				kmem_free(pkt, sizeof(struct dwmac_rx_packet));
				pkt= NULL;
			}
		}

		if (pkt) {
			if (ddi_dma_mem_alloc(pkt->dma.dma_handle, PKT_BUFFER_SIZE, &mem_acc_attr, DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, 0,
				    &pkt->dma.addr, &pkt->dma.size, &pkt->dma.mem_handle)) {
				ddi_dma_free_handle(&pkt->dma.dma_handle);
				kmem_free(pkt, sizeof(struct dwmac_rx_packet));
				pkt= NULL;
			} else {
				ASSERT(pkt->dma.size >= PKT_BUFFER_SIZE);
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
				pkt->free_rtn.free_func = dwmac_free_rx_packet;
				pkt->free_rtn.free_arg = (char *)pkt;

				pkt->mp = desballoc((unsigned char *)pkt->dma.addr, PKT_BUFFER_SIZE, BPRI_MED, &pkt->free_rtn);
				if (pkt->mp == NULL) {
					ddi_dma_unbind_handle(pkt->dma.dma_handle);
					ddi_dma_mem_free(&pkt->dma.mem_handle);
					ddi_dma_free_handle(&pkt->dma.dma_handle);
					kmem_free(pkt, sizeof(struct dwmac_rx_packet));
					pkt= NULL;
				}
			} else {
				ddi_dma_mem_free(&pkt->dma.mem_handle);
				ddi_dma_free_handle(&pkt->dma.dma_handle);
				kmem_free(pkt, sizeof(struct dwmac_rx_packet));
				pkt= NULL;
			}
		}
	}

	return pkt;
}

static bool
dwmac_alloc_desc_ring(struct dwmac_sc *sc, struct dwmac_dma *desc_dma, size_t num)
{
	size_t size = num * sizeof(struct dwmac_desc);
	ddi_dma_attr_t desc_dma_attr = dma_attr;
	desc_dma_attr.dma_attr_align = sizeof(struct dwmac_desc);

	if (ddi_dma_alloc_handle(sc->dip, &desc_dma_attr, DDI_DMA_SLEEP, 0, &desc_dma->dma_handle) != DDI_SUCCESS) {
		return false;
	}

	if (ddi_dma_mem_alloc(desc_dma->dma_handle, size, &mem_acc_attr, DDI_DMA_CONSISTENT | IOMEM_DATA_UC_WR_COMBINE, DDI_DMA_SLEEP, 0,
		    &desc_dma->addr, &desc_dma->size, &desc_dma->mem_handle)) {
		return false;
	}

	ddi_dma_cookie_t cookie;
	uint_t ccount;
	int result = ddi_dma_addr_bind_handle(
	    desc_dma->dma_handle, NULL, desc_dma->addr, desc_dma->size, DDI_DMA_RDWR | DDI_DMA_CONSISTENT,
	    DDI_DMA_SLEEP, NULL, &cookie, &ccount);
	if (result == DDI_DMA_MAPPED) {
		ASSERT(ccount == 1);
	} else {
		return false;
	}
	ASSERT(desc_dma->size >= size);
	desc_dma->dmac_addr = cookie.dmac_laddress;

	return true;
}

static void
dwmac_free_desc_ring(struct dwmac_dma *desc_dma)
{
	if (desc_dma->dmac_addr)
		ddi_dma_unbind_handle(desc_dma->dma_handle);
	desc_dma->dmac_addr = 0;

	if (desc_dma->mem_handle)
		ddi_dma_mem_free(&desc_dma->mem_handle);
	desc_dma->mem_handle = 0;

	if (desc_dma->dma_handle)
		ddi_dma_free_handle(&desc_dma->dma_handle);
	desc_dma->dma_handle = 0;
}

static bool
gxbb_get_macaddr(struct dwmac_sc *sc)
{
	uint64_t ret = gxbb_efuse_read(52, 6);
	if (ret != 6)
		return false;

	memcpy(sc->dev_addr, (void *)(gxbb_share_mem_out_base() + SEGKPM_BASE), 6);
	return true;
}

static void
dwmac_destroy(struct dwmac_sc *sc)
{
	if (sc->intr_handle) {
		ddi_intr_disable(sc->intr_handle);
		ddi_intr_remove_handler(sc->intr_handle);
		ddi_intr_free(sc->intr_handle);
	}
	sc->intr_handle = 0;

	if (sc->mii_handle)
		mii_free(sc->mii_handle);
	sc->mii_handle = 0;

	if (sc->mac_handle) {
		mac_unregister(sc->mac_handle);
		mac_free(sc->macp);
	}
	sc->mac_handle = 0;

	for (int i = 0; i < TX_DESC_NUM; i++) {
		if (sc->tx_ring.pkt[i].dmac_addr)
			ddi_dma_unbind_handle(sc->tx_ring.pkt[i].dma_handle);
		sc->tx_ring.pkt[i].dmac_addr = 0;

		if (sc->tx_ring.pkt[i].mem_handle)
			ddi_dma_mem_free(&sc->tx_ring.pkt[i].mem_handle);
		sc->tx_ring.pkt[i].mem_handle = 0;

		if (sc->tx_ring.pkt[i].dma_handle)
			ddi_dma_free_handle(&sc->tx_ring.pkt[i].dma_handle);
		sc->tx_ring.pkt[i].dma_handle = 0;
	}
	dwmac_free_desc_ring(&sc->tx_ring.desc);

	for (;;) {
		struct dwmac_rx_packet *pkt = sc->rx_pkt_free;
		if (pkt == NULL)
			break;
		freemsg(pkt->mp);
	}
	dwmac_free_desc_ring(&sc->rx_ring.desc);

	if (sc->reg.handle)
		ddi_regs_map_free(&sc->reg.handle);
	sc->reg.handle = 0;

	ddi_set_driver_private(sc->dip, NULL);
	struct dwmac_mcast *mc;
	while ((mc = list_head(&sc->mcast)) != NULL) {
		list_remove(&sc->mcast, mc);
		kmem_free(mc, sizeof (*mc));
	}
	list_destroy(&sc->mcast);
	mutex_destroy(&sc->intrlock);
	mutex_destroy(&sc->rx_pkt_lock);
	kmem_free(sc, sizeof (*sc));
}

static bool
dwmac_open(struct dwmac_sc *sc)
{
	int len;

	if (!dwmac_alloc_desc_ring(sc, &sc->rx_ring.desc, RX_DESC_NUM))
		return false;
	for (int i = 0; i < RX_DESC_NUM; i++) {
		struct dwmac_rx_packet *pkt = dwmac_alloc_rx_packet(sc);
		if (!pkt)
			return false;
		sc->rx_ring.pkt[i] = pkt;
		volatile struct dwmac_desc *rx_desc = (volatile struct dwmac_desc *)sc->rx_ring.desc.addr + i;
		rx_desc->ddesc_data = (uint32_t)(pkt->dma.dmac_addr);
		rx_desc->ddesc_next = (uint32_t)(sc->rx_ring.desc.dmac_addr + sizeof(struct dwmac_desc) * ((i + 1) % RX_DESC_NUM));
		rx_desc->ddesc_cntl = (PKT_SIZE << DDESC_CNTL_SIZE1SHIFT) | DDESC_CNTL_RXCHAIN;
		rx_desc->ddesc_status = DDESC_STATUS_OWNEDBYDEV;
		dcache_flush(pkt->dma.addr, pkt->dma.size);
	}
	sc->rx_ring.index = 0;

	if (!dwmac_alloc_desc_ring(sc, &sc->tx_ring.desc, TX_DESC_NUM))
		return false;

	for (int i = 0; i < TX_DESC_NUM; i++) {
		if (ddi_dma_alloc_handle(sc->dip, &dma_attr, DDI_DMA_SLEEP, 0, &sc->tx_ring.pkt[i].dma_handle) != DDI_SUCCESS)
			return false;

		if (ddi_dma_mem_alloc(sc->tx_ring.pkt[i].dma_handle, PKT_BUFFER_SIZE, &mem_acc_attr, DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, 0,
			    &sc->tx_ring.pkt[i].addr, &sc->tx_ring.pkt[i].size, &sc->tx_ring.pkt[i].mem_handle))
			return false;

		ddi_dma_cookie_t cookie;
		uint_t ccount;
		int result = ddi_dma_addr_bind_handle(sc->tx_ring.pkt[i].dma_handle, NULL, sc->tx_ring.pkt[i].addr, sc->tx_ring.pkt[i].size, DDI_DMA_RDWR | DDI_DMA_CONSISTENT,
		    DDI_DMA_SLEEP, NULL, &cookie, &ccount);
		if (result != DDI_DMA_MAPPED)
			return false;

		ASSERT(ccount == 1);
		sc->tx_ring.pkt[i].dmac_addr = cookie.dmac_laddress;

		volatile struct dwmac_desc *tx_desc = (volatile struct dwmac_desc *)sc->tx_ring.desc.addr + i;
		tx_desc->ddesc_data = (uint32_t)(sc->tx_ring.pkt[i].dmac_addr);
		tx_desc->ddesc_next = (uint32_t)(sc->tx_ring.desc.dmac_addr + sizeof(struct dwmac_desc) * ((i + 1) % TX_DESC_NUM));
		tx_desc->ddesc_cntl = 0;
		tx_desc->ddesc_status = 0;
	}
	sc->tx_ring.head = 0;
	sc->tx_ring.tail = 0;

	if (!gxbb_get_macaddr(sc))
		return false;

	dwmac_gmac_reset(sc);

	dwmac_gmac_init(sc);

	return true;
}

static void
dwmac_mii_write(void *arg, uint8_t phy, uint8_t reg, uint16_t value)
{
	struct dwmac_sc *sc = arg;

	dwmac_mutex_enter(sc);

	if (!(dwmac_reg_read(sc, DWMAC_MAC_MIIADDR) & GMAC_MII_BUSY)) {
		dwmac_reg_write(sc, DWMAC_MAC_MIIDATA, value);
		uint16_t mii =
		    (phy << GMAC_MII_PHY_SHIFT) |
		    (reg << GMAC_MII_REG_SHIFT) |
		    (GMAC_MII_CLK_100_150M_DIV62 << GMAC_MII_CLK_SHIFT) |
		    GMAC_MII_WRITE | GMAC_MII_BUSY;
		dwmac_reg_write(sc, DWMAC_MAC_MIIADDR, mii);

		for (int i = 0; i < 1000; i++) {
			dwmac_usecwait(100);
			if (!(dwmac_reg_read(sc, DWMAC_MAC_MIIADDR) & GMAC_MII_BUSY))
				break;
		}

		if (dwmac_reg_read(sc, DWMAC_MAC_MIIADDR) & GMAC_MII_BUSY) {
			cmn_err(CE_WARN, "%s%d: MII write failed",
			    ddi_driver_name(sc->dip), ddi_get_instance(sc->dip));
		}
	} else {
		cmn_err(CE_WARN, "%s%d: MII busy",
		    ddi_driver_name(sc->dip), ddi_get_instance(sc->dip));
	}

	dwmac_mutex_exit(sc);
}

static uint16_t
dwmac_mii_read(void *arg, uint8_t phy, uint8_t reg)
{
	struct dwmac_sc *sc = arg;

	uint16_t data = 0xffff;

	dwmac_mutex_enter(sc);

	if (!(dwmac_reg_read(sc, DWMAC_MAC_MIIADDR) & GMAC_MII_BUSY)) {
		uint16_t mii =
		    (phy << GMAC_MII_PHY_SHIFT) |
		    (reg << GMAC_MII_REG_SHIFT) |
		    (GMAC_MII_CLK_100_150M_DIV62 << GMAC_MII_CLK_SHIFT) |
		    GMAC_MII_BUSY;

		dwmac_reg_write(sc, DWMAC_MAC_MIIADDR, mii);

		for (int i = 0; i < 1000; i++) {
			dwmac_usecwait(100);
			if (!(dwmac_reg_read(sc, DWMAC_MAC_MIIADDR) & GMAC_MII_BUSY))
				break;
		}

		if (dwmac_reg_read(sc, DWMAC_MAC_MIIADDR) & GMAC_MII_BUSY) {
			cmn_err(CE_WARN, "%s%d: MII read failed",
			    ddi_driver_name(sc->dip), ddi_get_instance(sc->dip));
		} else {
			data = dwmac_reg_read(sc, DWMAC_MAC_MIIDATA);
		}
	} else {
		cmn_err(CE_WARN, "%s%d: MII busy",
		    ddi_driver_name(sc->dip), ddi_get_instance(sc->dip));
	}
	dwmac_mutex_exit(sc);

	return data;
}

static int
dwmac_probe(dev_info_t *dip)
{
	int len;
	char buf[80];
	pnode_t node = ddi_get_nodeid(dip);
	if (node < 0)
		return (DDI_PROBE_FAILURE);

	len = prom_getproplen(node, "status");
	if (len <= 0 || len >= sizeof(buf))
		return (DDI_PROBE_FAILURE);

	prom_getprop(node, "status", (caddr_t)buf);
	if (strcmp(buf, "ok") != 0 && (strcmp(buf, "okay") != 0))
		return (DDI_PROBE_FAILURE);

	return (DDI_PROBE_SUCCESS);
}

static void
dwmac_mii_notify(void *arg, link_state_t link)
{
	struct dwmac_sc *sc = arg;
	uint32_t gmac;
	uint32_t gpcr;
	link_flowctrl_t fc;
	link_duplex_t duplex;
	int speed;

	fc = mii_get_flowctrl(sc->mii_handle);
	duplex = mii_get_duplex(sc->mii_handle);
	speed = mii_get_speed(sc->mii_handle);

	dwmac_mutex_enter(sc);

	if (link == LINK_STATE_UP) {
		sc->phy_speed = speed;
		sc->phy_duplex = duplex;
		dwmac_gmac_update(sc);
	} else {
		sc->phy_speed = -1;
		sc->phy_duplex = LINK_DUPLEX_UNKNOWN;
	}

	dwmac_mutex_exit(sc);

	mac_link_update(sc->mac_handle, link);
}

static void
dwmac_mii_reset(void *arg)
{
	struct dwmac_sc *sc = arg;
	int phy = mii_get_addr(sc->mii_handle);

	dwmac_mii_write(sc, phy, 0x0d, 0x7);
	dwmac_mii_write(sc, phy, 0x0e, 0x3c);
	dwmac_mii_write(sc, phy, 0x0d, 0x4007);
	dwmac_mii_write(sc, phy, 0x0e, 0);

	uint16_t v = dwmac_mii_read(sc, phy, 9);
	dwmac_mii_write(sc, phy, 9, v & ~(1u << 9));
}

static mii_ops_t dwmac_mii_ops = {
	MII_OPS_VERSION,
	dwmac_mii_read,
	dwmac_mii_write,
	dwmac_mii_notify,
	dwmac_mii_reset	/* reset */
};

static int
dwmac_phy_install(struct dwmac_sc *sc)
{
	sc->mii_handle = mii_alloc(sc, sc->dip, &dwmac_mii_ops);
	if (sc->mii_handle == NULL) {
		return (DDI_FAILURE);
	}
	//mii_set_pauseable(sc->mii_handle, B_FALSE, B_FALSE);

	return DDI_SUCCESS;
}

static mblk_t *
dwmac_send(struct dwmac_sc *sc, mblk_t *mp)
{
	if (((sc->tx_ring.head - sc->tx_ring.tail + TX_DESC_NUM) % TX_DESC_NUM) == (TX_DESC_NUM - 1)) {
		return mp;
	}

	int index = sc->tx_ring.head;
	size_t mblen = 0;
	size_t frags = 0;

	for (mblk_t *bp = mp; bp != NULL; bp = bp->b_cont) {
		size_t frag_len = MBLKL(bp);
		if (frag_len == 0)
			continue;
		frags++;
		mblen += frag_len;
	}

	mcopymsg(mp, sc->tx_ring.pkt[index].addr);

	dcache_flush(sc->tx_ring.pkt[index].addr, sc->tx_ring.pkt[index].size);
	volatile struct dwmac_desc *tx_desc = (volatile struct dwmac_desc *)sc->tx_ring.desc.addr + index;
	tx_desc->ddesc_cntl = DDESC_CNTL_TXFIRST | DDESC_CNTL_TXLAST | DDESC_CNTL_TXCHAIN | DDESC_CNTL_TXINT | ((mblen < ETHERMIN) ? ETHERMIN: mblen);
	dsb();
	tx_desc->ddesc_status = DDESC_STATUS_OWNEDBYDEV;
	dsb();
	dwmac_reg_write(sc, DWMAC_DMA_TXPOLL, 0xffffffff);

	sc->tx_ring.head = (sc->tx_ring.head + 1) % TX_DESC_NUM;

	return (NULL);
}

static mblk_t *
dwmac_m_tx(void *arg, mblk_t *mp)
{
	struct dwmac_sc *sc = arg;
	mblk_t *nmp;

	dwmac_mutex_enter(sc);

	while (mp != NULL) {
		nmp = mp->b_next;
		mp->b_next = NULL;
		if ((mp = dwmac_send(sc, mp)) != NULL) {
			mp->b_next = nmp;
			break;
		}
		mp = nmp;
	}

	dwmac_mutex_exit(sc);

	return (mp);
}


static mblk_t *
dwmac_rx_intr(struct dwmac_sc *sc)
{
	int index = sc->rx_ring.index;

	mblk_t *mblk_head = NULL;
	mblk_t **mblk_tail = &mblk_head;

	for (;;) {
		volatile struct dwmac_desc *rx_desc = (volatile struct dwmac_desc *)sc->rx_ring.desc.addr + index;
		dsb();
		uint32_t status = rx_desc->ddesc_status;

		size_t len = 0;

		if (status & DDESC_STATUS_OWNEDBYDEV)
			break;
		if (!(status & (DDESC_STATUS_RXERROR|DDESC_STATUS_RXTRUNCATED))) {
			len = ((status &  DDESC_STATUS_FRMLENMSK) >> DDESC_STATUS_FRMLENSHIFT);
			if (len >= 64) {
				len -= 4;
			}
		}

		if (len > 0) {
			struct dwmac_rx_packet *pkt = dwmac_alloc_rx_packet(sc);
			if (pkt) {
				mblk_t *mp = sc->rx_ring.pkt[index]->mp;
				dcache_flush(sc->rx_ring.pkt[index]->dma.addr, len);
				mp->b_wptr += len;
				sc->rx_ring.pkt[index] = pkt;

				*mblk_tail = mp;
				mblk_tail = &mp->b_next;
			}
		}

		{
			struct dwmac_rx_packet *pkt = sc->rx_ring.pkt[index];
			dcache_flush(pkt->dma.addr, pkt->dma.size);
			rx_desc->ddesc_data = (uint32_t)(pkt->dma.dmac_addr);
			rx_desc->ddesc_cntl = (PKT_SIZE << DDESC_CNTL_SIZE1SHIFT) | DDESC_CNTL_RXCHAIN;
			dsb();
			rx_desc->ddesc_status = DDESC_STATUS_OWNEDBYDEV;
		}
		index = (index + 1) % RX_DESC_NUM;
	}

	sc->rx_ring.index = index;

	return mblk_head;
}


static int
dwmac_tx_intr(struct dwmac_sc *sc)
{
	int index = sc->tx_ring.tail;
	int ret = 0;
	while (index != sc->tx_ring.head) {
		volatile struct dwmac_desc *tx_desc = (volatile struct dwmac_desc *)sc->tx_ring.desc.addr + index;
		dsb();
		uint32_t status = tx_desc->ddesc_status;
		if (status & DDESC_STATUS_OWNEDBYDEV)
			break;
		index = (index + 1) % TX_DESC_NUM;
		ret++;
	}
	sc->tx_ring.tail = index;
	return ret;
}

static uint_t
dwmac_intr(caddr_t arg, caddr_t unused)
{
	struct dwmac_sc *sc = (struct dwmac_sc *)arg;
	uint32_t dma_status;

	dwmac_mutex_enter(sc);

	dma_status = dwmac_reg_read(sc, DWMAC_DMA_STATUS);
	dwmac_reg_write(sc, DWMAC_DMA_STATUS, dma_status);

	if (dma_status & GMAC_DMA_INT_RIE) {
		mblk_t *mp = dwmac_rx_intr(sc);
		if (mp) {
			dwmac_mutex_exit(sc);
			mac_rx(sc->mac_handle, NULL, mp);
			dwmac_mutex_enter(sc);
		}
	}

	if (dma_status & GMAC_DMA_INT_TIE) {
		int tx = 0;

		tx = dwmac_tx_intr(sc);

		if (tx) {
			dwmac_mutex_exit(sc);
			mac_tx_update(sc->mac_handle);
			dwmac_mutex_enter(sc);
		}
	}
	dwmac_mutex_exit(sc);

	return (DDI_INTR_CLAIMED);
}


static int dwmac_attach(dev_info_t *dip, ddi_attach_cmd_t cmd);
static int
dwmac_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	switch (cmd) {
	case DDI_DETACH:
		break;
	default:
		return (DDI_FAILURE);
	}
	struct dwmac_sc *sc = ddi_get_driver_private(dip);

	dwmac_m_stop(sc);

	if (mac_disable(sc->mac_handle) != 0)
		return (DDI_FAILURE);

	dwmac_destroy(sc);

	return DDI_SUCCESS;
}

static int
dwmac_quiesce(dev_info_t *dip)
{
	cmn_err(CE_WARN, "%s%d: dwmac_quiesce is not implemented",
	    ddi_driver_name(dip), ddi_get_instance(dip));
	return DDI_FAILURE;
}

static uint32_t
bitreverse(uint32_t x)
{
	x = (((x & 0xaaaaaaaa) >> 1) | ((x & 0x55555555) << 1));
	x = (((x & 0xcccccccc) >> 2) | ((x & 0x33333333) << 2));
	x = (((x & 0xf0f0f0f0) >> 4) | ((x & 0x0f0f0f0f) << 4));
	x = (((x & 0xff00ff00) >> 8) | ((x & 0x00ff00ff) << 8));

	return (x >> 16) | (x << 16);
}

static void
dwmac_update_filter(struct dwmac_sc *sc)
{
	uint32_t hash[2] = {0};
	uint32_t ffilt = dwmac_reg_read(sc, DWMAC_MAC_FFILT);
	if (ffilt & DWMAC_MAC_FFILT_PR) {
		hash[0] = 0xffffffff;
		hash[1] = 0xffffffff;
	} else {
		for (struct dwmac_mcast *mc = list_head(&sc->mcast); mc; mc = list_next(&sc->mcast, mc)) {
			uint32_t crc;
			CRC32(crc, mc->addr, sizeof(mc->addr), -1U, crc32_table);
			uint32_t val = (bitreverse(~crc) >> 26);
			hash[(val >> 5)] |= (1 << (val & 31));
		}
	}
	dwmac_reg_write(sc, DWMAC_MAC_HTHIGH, hash[1]);
	dwmac_reg_write(sc, DWMAC_MAC_HTLOW,  hash[0]);
}

static int
dwmac_m_setpromisc(void *a, boolean_t b)
{
	struct dwmac_sc *sc = a;
	dwmac_mutex_enter(sc);

	uint32_t ffilt = dwmac_reg_read(sc, DWMAC_MAC_FFILT);
	if (b)
		ffilt |= DWMAC_MAC_FFILT_PR;
	else
		ffilt &= ~DWMAC_MAC_FFILT_PR;
	dwmac_reg_write(sc, DWMAC_MAC_FFILT, ffilt);
	dwmac_update_filter(sc);

	dwmac_mutex_exit(sc);

	return 0;
}

static int
dwmac_m_multicst(void *a, boolean_t b, const uint8_t *c)
{
	struct dwmac_sc *sc = a;
	struct dwmac_mcast *mc;

	dwmac_mutex_enter(sc);

	if (b) {
		mc = kmem_alloc(sizeof (*mc), KM_NOSLEEP);
		if (!mc) {
			dwmac_mutex_exit(sc);
			return ENOMEM;
		}

		memcpy(mc->addr, c, sizeof(mc->addr));
		list_insert_head(&sc->mcast, mc);
	} else {
		for (mc = list_head(&sc->mcast); mc; mc = list_next(&sc->mcast, mc)) {
			if (memcmp(mc->addr, c, sizeof(mc->addr)) == 0) {
				list_remove(&sc->mcast, mc);
				kmem_free(mc, sizeof (*mc));
				break;
			}
		}
	}

	dwmac_update_filter(sc);

	dwmac_mutex_exit(sc);
	return 0;
}

static int
dwmac_m_unicst(void *arg, const uint8_t *dev_addr)
{
	struct dwmac_sc *sc = arg;

	dwmac_mutex_enter(sc);

	memcpy(sc->dev_addr, dev_addr, sizeof(sc->dev_addr));

	dwmac_gmac_disable(sc);

	dwmac_reg_write(sc, DWMAC_MAC_ADDR0HI, (sc->dev_addr[5] << 8) | sc->dev_addr[4]);
	dwmac_reg_write(sc, DWMAC_MAC_ADDR0LO, (sc->dev_addr[3] << 24) | (sc->dev_addr[2] << 16) | (sc->dev_addr[1] << 8) | sc->dev_addr[0]);

	dwmac_gmac_enable(sc);

	dwmac_mutex_exit(sc);

	return 0;
}

static int
dwmac_m_start(void *arg)
{
	struct dwmac_sc *sc = arg;

	dwmac_mutex_enter(sc);

	sc->running = 1;
	dwmac_gmac_enable(sc);

	if (ddi_intr_enable(sc->intr_handle) != DDI_SUCCESS) {
		dwmac_gmac_disable(sc);
		sc->running = 0;
		dwmac_mutex_exit(sc);
		return EIO;
	}

	dwmac_mutex_exit(sc);

	mii_start(sc->mii_handle);

	return 0;
}

static void
dwmac_m_stop(void *arg)
{
	struct dwmac_sc *sc = arg;

	mii_stop(sc->mii_handle);

	dwmac_mutex_enter(sc);

	ddi_intr_disable(sc->intr_handle);

	sc->running = 0;
	dwmac_gmac_disable(sc);

	dwmac_mutex_exit(sc);
}

static int
dwmac_m_getstat(void *arg, uint_t stat, uint64_t *val)
{
	struct dwmac_sc *sc = arg;
	return mii_m_getstat(sc->mii_handle, stat, val);
}

static int
dwmac_m_setprop(void *arg, const char *name, mac_prop_id_t num, uint_t sz, const void *val)
{
	struct dwmac_sc *sc = arg;
	return mii_m_setprop(sc->mii_handle, name, num, sz, val);
}

static int
dwmac_m_getprop(void *arg, const char *name, mac_prop_id_t num, uint_t sz, void *val)
{
	struct dwmac_sc *sc = arg;
	return mii_m_getprop(sc->mii_handle, name, num, sz, val);
}

static void
dwmac_m_propinfo(void *arg, const char *name, mac_prop_id_t num, mac_prop_info_handle_t prh)
{
	struct dwmac_sc *sc = arg;
	mii_m_propinfo(sc->mii_handle, name, num, prh);
}

static void
dwmac_m_ioctl(void *arg, queue_t *wq, mblk_t *mp)
{
	struct dwmac_sc *sc = arg;
	if (mii_m_loop_ioctl(sc->mii_handle, wq, mp))
		return;

	miocnak(wq, mp, 0, EINVAL);
}

extern struct mod_ops mod_driverops;

DDI_DEFINE_STREAM_OPS(dwmac_devops, nulldev, dwmac_probe, dwmac_attach,
    dwmac_detach, nodev, NULL, D_MP, NULL, dwmac_quiesce);

static struct modldrv dwmac_modldrv = {
	&mod_driverops,		/* Type of module.  This one is a driver */
	"Amlogic dwmac",	/* short description */
	&dwmac_devops		/* driver specific ops */
};

static struct modlinkage dwmac_modlinkage = {
	MODREV_1,		/* ml_rev */
	{ &dwmac_modldrv, NULL }	/* ml_linkage */
};

static mac_callbacks_t dwmac_m_callbacks = {
	0,	/* mc_callbacks */
	dwmac_m_getstat,	/* mc_getstat */
	dwmac_m_start,		/* mc_start */
	dwmac_m_stop,		/* mc_stop */
	dwmac_m_setpromisc,	/* mc_setpromisc */
	dwmac_m_multicst,	/* mc_multicst */
	dwmac_m_unicst,		/* mc_unicst */
	dwmac_m_tx,		/* mc_tx */
	NULL,
	dwmac_m_ioctl,		/* mc_ioctl */
	NULL,			/* mc_getcapab */
	NULL,			/* mc_open */
	NULL,			/* mc_close */
	dwmac_m_setprop,
	dwmac_m_getprop,
	dwmac_m_propinfo
};

static int
dwmac_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	switch (cmd) {
	case DDI_ATTACH:
		break;
	default:
		return (DDI_FAILURE);
	}

	struct dwmac_sc *sc = kmem_zalloc(sizeof(struct dwmac_sc), KM_SLEEP);
	ddi_set_driver_private(dip, sc);
	sc->dip = dip;

	mutex_init(&sc->intrlock, NULL, MUTEX_DRIVER, NULL);
	mutex_init(&sc->rx_pkt_lock, NULL, MUTEX_DRIVER, NULL);
	list_create(&sc->mcast, sizeof (struct dwmac_mcast), offsetof(struct dwmac_mcast, node));

	if (ddi_regs_map_setup(sc->dip, 0, &sc->reg.addr, 0, 0, &reg_acc_attr, &sc->reg.handle) != DDI_SUCCESS) {
		goto err_exit;
	}

	dwmac_mutex_enter(sc);
	if (!dwmac_open(sc)) {
		dwmac_mutex_exit(sc);
		goto err_exit;
	}
	dwmac_mutex_exit(sc);

	mac_register_t *macp;
	if ((macp = mac_alloc(MAC_VERSION)) == NULL) {
		goto err_exit;
	}
	sc->macp = macp;

	macp->m_type_ident = MAC_PLUGIN_IDENT_ETHER;
	macp->m_driver = sc;
	macp->m_dip = dip;
	macp->m_src_addr = sc->dev_addr;
	macp->m_callbacks = &dwmac_m_callbacks;
	macp->m_min_sdu = 0;
	macp->m_max_sdu = ETHERMTU;
	macp->m_margin = VLAN_TAGSZ;

	if (mac_register(macp, &sc->mac_handle) != 0) {
		mac_free(sc->macp);
		sc->mac_handle = 0;
		goto err_exit;
	}

	if (dwmac_phy_install(sc) != DDI_SUCCESS) {
		goto err_exit;
	}

	int actual;
	if (ddi_intr_alloc(dip, &sc->intr_handle, DDI_INTR_TYPE_FIXED, 0, 1, &actual, DDI_INTR_ALLOC_STRICT) != DDI_SUCCESS) {
		goto err_exit;
	}

	if (ddi_intr_add_handler(sc->intr_handle, dwmac_intr, sc, NULL) != DDI_SUCCESS) {
		ddi_intr_free(sc->intr_handle);
		sc->intr_handle = 0;
		goto err_exit;
	}

	return DDI_SUCCESS;
err_exit:
	dwmac_destroy(sc);
	return (DDI_FAILURE);
}

int
_init(void)
{
	int i;

	mac_init_ops(&dwmac_devops, "platmac");

	if ((i = mod_install(&dwmac_modlinkage)) != 0) {
		mac_fini_ops(&dwmac_devops);
	}
	return (i);
}

int
_fini(void)
{
	int i;

	if ((i = mod_remove(&dwmac_modlinkage)) == 0) {
		mac_fini_ops(&dwmac_devops);
	}
	return (i);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&dwmac_modlinkage, modinfop));
}
