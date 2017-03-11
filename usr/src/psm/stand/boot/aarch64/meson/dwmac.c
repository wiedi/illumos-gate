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

#include <sys/types.h>
#include <sys/param.h>
#include <sys/promif.h>
#include <sys/salib.h>
#include <sys/byteorder.h>
#include <sys/sysmacros.h>
#include <sys/controlregs.h>

#include <sys/gxbb_smc.h>
#include <sys/miiregs.h>
#include <sys/ethernet.h>
#include "prom_dev.h"
#include "dwmac.h"
#include "dwmacreg.h"
#include "boot_plat.h"

#define TX_DESC_NUM 32
#define RX_DESC_NUM 48

#define ENET_ALIGN  DCACHE_LINE
#define BUFFER_SIZE 1536

struct dwmac_sc
{
	pnode_t node;
	uint64_t base;
	uint8_t mac_addr[6];
	int phy_id;
	int phy_speed;
	int phy_fullduplex;


	struct dwmac_desc *tx_desc;
	struct dwmac_desc *rx_desc;
	int tx_index;
	int rx_head;
	caddr_t tx_buffer;
	caddr_t rx_buffer;
	paddr_t tx_buffer_phys;
	paddr_t tx_desc_phys;
	paddr_t rx_desc_phys;
	paddr_t rx_buffer_phys;
};

static struct dwmac_sc *dwmac_dev[3];

static void
dwmac_usecwait(int usec)
{
	uint64_t cnt = (read_cntpct() / (read_cntfrq() / 1000000)) + usec + 2;
	for (;;) {
		if ((read_cntpct() / (read_cntfrq() / 1000000)) > cnt)
			break;
	}
}

static void
dwmac_cache_flush(void *addr, size_t size)
{
	uintptr_t end = roundup((uintptr_t)addr + size, DCACHE_LINE);
	while ((uintptr_t)addr < end) {
		flush_data_cache((uintptr_t)addr);
		addr = (void *)((uintptr_t)addr + DCACHE_LINE);
	}
	dsb();
}

static int
dwmac_alloc_buffer(struct dwmac_sc *sc)
{
	size_t size = 0;
	size += sizeof(struct dwmac_desc) * TX_DESC_NUM;
	size = roundup(size, ENET_ALIGN);
	size += sizeof(struct dwmac_desc) * RX_DESC_NUM;
	size = roundup(size, ENET_ALIGN);
	size += BUFFER_SIZE * TX_DESC_NUM;
	size = roundup(size, ENET_ALIGN);
	size += BUFFER_SIZE * RX_DESC_NUM;

	size_t alloc_size = size + 2 * MMU_PAGESIZE;
	uintptr_t orig_addr = (uintptr_t)kmem_alloc(alloc_size, 0);
	uintptr_t buf_addr = roundup(orig_addr, MMU_PAGESIZE);
	size_t buf_size = roundup(size, MMU_PAGESIZE);
	uintptr_t buf_vaddr = memlist_get(buf_size, MMU_PAGESIZE, &ptmplistp);

	map_phys(PTE_XN | PTE_AF | PTE_SH_INNER | PTE_AP_KRWUNA | PTE_ATTR_STRONG, (caddr_t)buf_vaddr, buf_addr, buf_size);
	dwmac_cache_flush((caddr_t)orig_addr, alloc_size);
	size_t offset = 0;
	sc->tx_desc_phys = (paddr_t)(buf_addr + offset);
	sc->tx_desc = (struct dwmac_desc *)(buf_vaddr + offset);
	offset += sizeof(struct dwmac_desc) * TX_DESC_NUM;

	offset = roundup(offset, ENET_ALIGN);
	sc->rx_desc_phys = (paddr_t)(buf_addr + offset);
	sc->rx_desc = (struct dwmac_desc *)(buf_vaddr + offset);
	offset += sizeof(struct dwmac_desc) * RX_DESC_NUM;

	offset = roundup(offset, ENET_ALIGN);
	sc->tx_buffer_phys = (paddr_t)(buf_addr + offset);
	sc->tx_buffer = (caddr_t)(buf_vaddr + offset);
	offset += BUFFER_SIZE * TX_DESC_NUM;

	offset = roundup(offset, ENET_ALIGN);
	sc->rx_buffer_phys = (paddr_t)(buf_addr + offset);
	sc->rx_buffer = (caddr_t)(buf_vaddr + offset);
	offset += BUFFER_SIZE * RX_DESC_NUM;

	memset(sc->tx_desc, 0, sizeof(struct dwmac_desc) * TX_DESC_NUM);
	memset(sc->rx_desc, 0, sizeof(struct dwmac_desc) * RX_DESC_NUM);
	memset(sc->tx_buffer, 0, BUFFER_SIZE * TX_DESC_NUM);
	memset(sc->rx_buffer, 0, BUFFER_SIZE * RX_DESC_NUM);

	return 0;
}

static int
gxbb_get_macaddr(struct dwmac_sc *sc)
{
	uint64_t ret = gxbb_efuse_read(52, 6);
	if (ret != 6)
		return -1;

	memcpy(sc->mac_addr, (void *)gxbb_share_mem_out_base(), 6);
	return 0;
}

static int
dwmac_match(const char *name)
{
	pnode_t node = prom_finddevice(name);
	if (node <= 0)
		return 0;
	if (prom_is_compatible(node, "amlogic,gxbb-rgmii-dwmac"))
		return 1;
	return 0;
}

static void
dwmac_reg_write(struct dwmac_sc *sc, size_t offset, uint32_t val)
{
	*(volatile uint32_t *)(sc->base + offset) = val;
}

static uint32_t
dwmac_reg_read(struct dwmac_sc *sc, size_t offset)
{
	return *(volatile uint32_t *)(sc->base + offset);
}

static void
dwmac_mii_write(struct dwmac_sc *sc, int offset, uint16_t val)
{
	if ((dwmac_reg_read(sc, DWMAC_MAC_MIIADDR) & GMAC_MII_BUSY))
		return;

	dwmac_reg_write(sc, DWMAC_MAC_MIIDATA, val);

	uint16_t mii =
	    ((sc->phy_id) << GMAC_MII_PHY_SHIFT) |
	    (offset << GMAC_MII_REG_SHIFT) |
	    (GMAC_MII_CLK_100_150M_DIV62 << GMAC_MII_CLK_SHIFT) |
	    GMAC_MII_WRITE | GMAC_MII_BUSY;
	dwmac_reg_write(sc, DWMAC_MAC_MIIADDR, mii);

	for (int i = 0; i < 1000; i++) {
		dwmac_usecwait(100);
		if (!(dwmac_reg_read(sc, DWMAC_MAC_MIIADDR) & GMAC_MII_BUSY))
			break;
	}
}

static uint16_t
dwmac_mii_read(struct dwmac_sc *sc, int offset)
{
	if ((dwmac_reg_read(sc, DWMAC_MAC_MIIADDR) & GMAC_MII_BUSY))
		return 0xffff;

	uint16_t mii =
	    ((sc->phy_id) << GMAC_MII_PHY_SHIFT) |
	    (offset << GMAC_MII_REG_SHIFT) |
	    (GMAC_MII_CLK_100_150M_DIV62 << GMAC_MII_CLK_SHIFT) |
	    GMAC_MII_BUSY;
	dwmac_reg_write(sc, DWMAC_MAC_MIIADDR, mii);

	for (int i = 0; i < 1000; i++) {
		dwmac_usecwait(100);
		if (!(dwmac_reg_read(sc, DWMAC_MAC_MIIADDR) & GMAC_MII_BUSY))
			break;
	}

	if ((dwmac_reg_read(sc, DWMAC_MAC_MIIADDR) & GMAC_MII_BUSY))
		return 0xffff;

	return dwmac_reg_read(sc, DWMAC_MAC_MIIDATA);
}

static int
dwmac_phy_reset(struct dwmac_sc *sc)
{
	uint16_t advert = dwmac_mii_read(sc, MII_AN_ADVERT) & 0x1F;
	advert |= MII_ABILITY_100BASE_TX_FD;
	advert |= MII_ABILITY_100BASE_TX;
	advert |= MII_ABILITY_10BASE_T_FD;
	advert |= MII_ABILITY_10BASE_T;
	uint16_t gigctrl =  MII_MSCONTROL_1000T_FD | MII_MSCONTROL_1000T;

	dwmac_mii_write(sc, MII_AN_ADVERT, advert);
	dwmac_mii_write(sc, MII_MSCONTROL, gigctrl);

	uint16_t bmcr = MII_CONTROL_ANE | MII_CONTROL_RSAN | MII_CONTROL_1GB | MII_CONTROL_FDUPLEX;
	dwmac_mii_write(sc, MII_CONTROL, bmcr);

	int i;
	uint16_t bmsr = 0;
	for (i = 0; i < 10000; i++) {
		dwmac_usecwait(1000);
		bmsr = dwmac_mii_read(sc, MII_STATUS);
		if (bmsr == 0xffff)
			continue;
		if (bmsr & MII_STATUS_LINKUP)
			break;
	}
	if (i == 10000 || !(bmsr & MII_STATUS_LINKUP))
		return -1;

	uint16_t lpar = dwmac_mii_read(sc, MII_AN_LPABLE);
	uint16_t msstat = dwmac_mii_read(sc, MII_MSSTATUS);
	if (msstat & MII_MSSTATUS_LP1000T_FD) {
		sc->phy_speed = 1000;
		sc->phy_fullduplex = 1;
	} else if (msstat & MII_MSSTATUS_LP1000T) {
		sc->phy_speed = 1000;
		sc->phy_fullduplex = 0;
	} else if (lpar & MII_ABILITY_100BASE_TX_FD) {
		sc->phy_speed = 100;
		sc->phy_fullduplex = 1;
	} else if (lpar & MII_ABILITY_100BASE_TX) {
		sc->phy_speed = 100;
		sc->phy_fullduplex = 0;
	} else if (lpar & MII_ABILITY_10BASE_T_FD) {
		sc->phy_speed = 10;
		sc->phy_fullduplex = 1;
	} else if (lpar & MII_ABILITY_10BASE_T) {
		sc->phy_speed = 10;
		sc->phy_fullduplex = 0;
	} else {
		sc->phy_speed = 0;
		sc->phy_fullduplex = 0;
	}
	prom_printf("%s:%d speed=%d\n",__func__,__LINE__, sc->phy_speed);

	return 0;
}

static void
dwmac_setup_rx_buffer(struct dwmac_sc *sc, int i)
{
	volatile struct dwmac_desc *rx_desc = &sc->rx_desc[i];
	dsb();
	rx_desc->ddesc_data = (uint32_t)(sc->rx_buffer_phys + BUFFER_SIZE * i);
	rx_desc->ddesc_next = (uint32_t)(sc->rx_desc_phys + sizeof (struct dwmac_desc) * ((i + 1) % RX_DESC_NUM));
	rx_desc->ddesc_cntl = (BUFFER_SIZE << DDESC_CNTL_SIZE1SHIFT) | DDESC_CNTL_RXCHAIN;
	dsb();
	rx_desc->ddesc_status = DDESC_STATUS_OWNEDBYDEV;
}

static void
dwmac_setup_tx_buffer(struct dwmac_sc *sc, int i)
{
	volatile struct dwmac_desc *tx_desc = &sc->tx_desc[i];
	tx_desc->ddesc_data = (uint32_t)(sc->tx_buffer_phys + BUFFER_SIZE * i);
	tx_desc->ddesc_next = (uint32_t)(sc->tx_desc_phys + sizeof (struct dwmac_desc) * ((i + 1) % TX_DESC_NUM));
	tx_desc->ddesc_cntl = 0;
	tx_desc->ddesc_status = 0;
}

static int
dwmac_open(const char *name)
{
	pnode_t node = prom_finddevice(name);
	if (node <= 0)
		return -1;
	if (!prom_is_compatible(node, "amlogic,gxbb-rgmii-dwmac"))
		return -1;

	int fd;

	for (fd = 0; fd < sizeof(dwmac_dev) / sizeof(dwmac_dev[0]); fd++) {
		if (dwmac_dev[fd] == NULL)
			break;
	}
	if (fd == sizeof(dwmac_dev) / sizeof(dwmac_dev[0]))
		return -1;
	struct dwmac_sc *sc = kmem_alloc(sizeof(struct dwmac_sc), 0);
	sc->node = node;

	// power-on
	struct prom_hwreset hwreset;
	if (prom_get_reset(sc->node, 0, &hwreset) == 0) {
		uint64_t base;
		if (prom_get_reg(hwreset.node, 0, &base) == 0) {
			*(volatile uint32_t *)(base + ((0x50 + hwreset.id / 32) << 2)) |= (1u << (hwreset.id % 32));
		}
	}

	if (gxbb_get_macaddr(sc))
		return -1;

	if (prom_get_reg(sc->node, 0, &sc->base) != 0)
		return -1;

	uint64_t preg_eth0;
	if (prom_get_reg(sc->node, 1, &preg_eth0) != 0)
		return -1;

	if (prom_getproplen(sc->node, "mc_val") != sizeof(uint32_t))
		return -1;

	uint32_t mc_val;
	prom_getprop(sc->node, "mc_val", (caddr_t)&mc_val);
	mc_val = ntohl(mc_val);
	*(volatile uint32_t *)preg_eth0 = mc_val;

	// stop
	dwmac_reg_write(sc, DWMAC_DMA_OPMODE, 0);
	dwmac_reg_write(sc, DWMAC_DMA_OPMODE,
	    dwmac_reg_read(sc, DWMAC_DMA_OPMODE) | GMAC_DMA_OP_FLUSHTX);
	dwmac_reg_write(sc, DWMAC_DMA_OPMODE, 0);

	// reset
	dwmac_reg_write(sc, DWMAC_DMA_BUSMODE, GMAC_BUSMODE_RESET);
	dsb();
	for (int i = 0; i < 1000; i++) {
		dwmac_usecwait(100);
		if (!(dwmac_reg_read(sc, DWMAC_DMA_BUSMODE) & GMAC_BUSMODE_RESET))
			break;
	}

	if ((dwmac_reg_read(sc, DWMAC_DMA_BUSMODE) & GMAC_BUSMODE_RESET))
		return -1;

	// detect phy
	sc->phy_id = -1;
	for (int i = 0; i < 32; i++) {
		int phy_id = (i + 1) % 32;
		uint16_t mii =
		    ((phy_id) << GMAC_MII_PHY_SHIFT) |
		    (MII_STATUS << GMAC_MII_REG_SHIFT) |
		    (GMAC_MII_CLK_100_150M_DIV62 << GMAC_MII_CLK_SHIFT) |
		    GMAC_MII_BUSY;
		dwmac_reg_write(sc, DWMAC_MAC_MIIADDR, mii);

		for (int i = 0; i < 1000; i++) {
			dwmac_usecwait(100);
			if (!(dwmac_reg_read(sc, DWMAC_MAC_MIIADDR) & GMAC_MII_BUSY))
				break;
		}

		if ((dwmac_reg_read(sc, DWMAC_MAC_MIIADDR) & GMAC_MII_BUSY))
			return -1;
		uint16_t mii_data = dwmac_reg_read(sc, DWMAC_MAC_MIIDATA);
		if (mii_data != 0 && mii_data != 0xffff) {
			sc->phy_id = phy_id;
			break;
		}
	}
	if (sc->phy_id < 0)
		return -1;

	if (dwmac_phy_reset(sc))
		return -1;

	if (dwmac_alloc_buffer(sc))
		return -1;

	for (int i = 0; i < RX_DESC_NUM; i++)
		dwmac_setup_rx_buffer(sc, i);

	for (int i = 0; i < TX_DESC_NUM; i++)
		dwmac_setup_tx_buffer(sc, i);

	uint32_t mac_conf = dwmac_reg_read(sc, DWMAC_MAC_CONF);
	mac_conf &= ~(DWMAC_MAC_CONF_FES | DWMAC_MAC_CONF_PS | DWMAC_MAC_CONF_DM);
	if (sc->phy_fullduplex)
		mac_conf |= DWMAC_MAC_CONF_DM;
	if (sc->phy_speed == 100)
		mac_conf |= DWMAC_MAC_CONF_FES;
	if (sc->phy_speed != 1000)
		mac_conf |= DWMAC_MAC_CONF_PS;
	mac_conf |= DWMAC_MAC_CONF_JD;
	mac_conf |= DWMAC_MAC_CONF_BE;
	mac_conf |= DWMAC_MAC_CONF_ACS;
	mac_conf |= DWMAC_MAC_CONF_DCRS;
	mac_conf |= DWMAC_MAC_CONF_TE;
	mac_conf |= DWMAC_MAC_CONF_RE;
	dwmac_reg_write(sc, DWMAC_MAC_CONF, mac_conf);

	// FIXME
	dwmac_reg_write(sc, DWMAC_MAC_FFILT, DWMAC_MAC_FFILT_HMC);
	dwmac_reg_write(sc, DWMAC_MAC_HTHIGH, 0);
	dwmac_reg_write(sc, DWMAC_MAC_HTLOW, 0);
	dwmac_reg_write(sc, DWMAC_MAC_ADDR0HI, (sc->mac_addr[5] << 8) | sc->mac_addr[4]);
	dwmac_reg_write(sc, DWMAC_MAC_ADDR0LO, (sc->mac_addr[3] << 24) | (sc->mac_addr[2] << 16) | (sc->mac_addr[1] << 8) | sc->mac_addr[0]);


	dwmac_reg_write(sc, DWMAC_MAC_INTMASK, 0);
	dwmac_reg_read(sc, DWMAC_MAC_INTR);
	dwmac_reg_read(sc, DWMAC_MII_STATUS);

	uint32_t busmode = dwmac_reg_read(sc, DWMAC_DMA_BUSMODE);
	busmode |= GMAC_BUSMODE_4PBL;
	busmode |= GMAC_BUSMODE_FIXEDBURST;
	busmode &= ~GMAC_BUSMODE_PBL;
	busmode |= (8u << GMAC_BUSMODE_PBL_SHIFT);
	dwmac_reg_write(sc, DWMAC_DMA_BUSMODE, busmode);

	dwmac_reg_write(sc, DWMAC_DMA_STATUS, 0xffffffff);
	dwmac_reg_write(sc, DWMAC_DMA_STATUS, 0);
	dwmac_reg_write(sc, DWMAC_DMA_INTENABLE, 0);

	dwmac_reg_write(sc, DWMAC_DMA_TX_ADDR, sc->tx_desc_phys);
	dwmac_reg_write(sc, DWMAC_DMA_RX_ADDR, sc->rx_desc_phys);

	dwmac_reg_write(sc, DWMAC_DMA_OPMODE,
	    GMAC_DMA_OP_RXSTART | GMAC_DMA_OP_TXSTART |
	    GMAC_DMA_OP_RXSTOREFORWARD | GMAC_DMA_OP_TXSTOREFORWARD);
	phandle_t chosen = prom_chosennode();

	char *str;
	str = "bootp";
	prom_setprop(chosen, "net-config-strategy", (caddr_t)str, strlen(str));
	str = "ethernet,100,rj45,full";
	prom_setprop(chosen, "network-interface-type", (caddr_t)str, strlen(str));

	dwmac_dev[fd] = sc;
	return fd;
}

static ssize_t
dwmac_send(int dev, caddr_t data, size_t packet_length, uint_t startblk)
{
	if (!(0 <= dev && dev < sizeof(dwmac_dev) / sizeof(dwmac_dev[0])))
		return -1;

	struct dwmac_sc *sc = dwmac_dev[dev];
	if (!sc)
		return -1;

	if (packet_length > BUFFER_SIZE)
		return -1;

	int index = sc->tx_index;
	volatile struct dwmac_desc *tx_desc = &sc->tx_desc[index];

	while (tx_desc->ddesc_status & DDESC_STATUS_OWNEDBYDEV) {}
	caddr_t buffer = sc->tx_buffer + BUFFER_SIZE * index;
	memcpy(buffer, data, packet_length);

	tx_desc->ddesc_cntl = DDESC_CNTL_TXFIRST | DDESC_CNTL_TXLAST | DDESC_CNTL_TXCHAIN | ((packet_length < ETHERMIN) ? ETHERMIN: packet_length);
	dsb();
	tx_desc->ddesc_status = DDESC_STATUS_OWNEDBYDEV;
	dwmac_reg_write(sc, DWMAC_DMA_TXPOLL, 0xffffffff);

	sc->tx_index = (sc->tx_index + 1) % TX_DESC_NUM;

	return packet_length;
}

static ssize_t
dwmac_recv(int dev, caddr_t buf, size_t buf_len, uint_t startblk)
{
	if (!(0 <= dev && dev < sizeof(dwmac_dev) / sizeof(dwmac_dev[0])))
		return -1;

	struct dwmac_sc *sc = dwmac_dev[dev];
	if (!sc)
		return -1;

	int index = sc->rx_head;
	size_t len = 0;

	volatile struct dwmac_desc *rx_desc = &sc->rx_desc[index];
	dsb();
	uint32_t status = rx_desc->ddesc_status;
	dsb();
	if (status & DDESC_STATUS_OWNEDBYDEV) {
		return 0;
	}

	if (!(status & (DDESC_STATUS_RXERROR|DDESC_STATUS_RXTRUNCATED))) {
		len = ((status &  DDESC_STATUS_FRMLENMSK) >> DDESC_STATUS_FRMLENSHIFT);
		if (len >= 64) {
			len -= 4;
			caddr_t buffer = sc->rx_buffer + BUFFER_SIZE * index;
			memcpy(buf, buffer, len);
		}
	}

	dwmac_setup_rx_buffer(sc, index);
	index = (index + 1) % RX_DESC_NUM;
	sc->rx_head = index;

	return len;
}

static int
dwmac_getmacaddr(ihandle_t dev, caddr_t ea)
{
	if (!(0 <= dev && dev < sizeof(dwmac_dev) / sizeof(dwmac_dev[0])))
		return -1;

	struct dwmac_sc *sc = dwmac_dev[dev];
	if (!sc)
		return -1;
	memcpy(ea, sc->mac_addr, 6);
	return 0;
}

static int
dwmac_close(int dev)
{
	if (!(0 <= dev && dev < sizeof(dwmac_dev) / sizeof(dwmac_dev[0])))
		return -1;
	struct dwmac_sc *sc = dwmac_dev[dev];
	if (!sc)
		return -1;

	dwmac_reg_write(sc, DWMAC_DMA_OPMODE, 0);
	dwmac_reg_write(sc, DWMAC_DMA_OPMODE,
	    dwmac_reg_read(sc, DWMAC_DMA_OPMODE) | GMAC_DMA_OP_FLUSHTX);
	dwmac_reg_write(sc, DWMAC_DMA_OPMODE, 0);
	dwmac_reg_write(sc, DWMAC_MAC_CONF, 0);

	dwmac_dev[dev] = NULL;
	return 0;
}

static struct prom_dev dwmac_prom_dev =
{
	.match = dwmac_match,
	.open = dwmac_open,
	.write = dwmac_send,
	.read = dwmac_recv,
	.close = dwmac_close,
	.getmacaddr = dwmac_getmacaddr,
};

void init_dwmac(void)
{
	prom_register(&dwmac_prom_dev);
}

