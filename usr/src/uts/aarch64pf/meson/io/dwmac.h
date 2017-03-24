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

#pragma once

#include <sys/types.h>
#include <stdbool.h>
#include <sys/platform.h>
#include <sys/mac.h>
#include <sys/mii.h>
#include <sys/ethernet.h>
#include <sys/sunddi.h>
#include <sys/ddi_impldefs.h>

struct dwmac_reg {
	ddi_acc_handle_t handle;
	caddr_t addr;
};

struct dwmac_dma {
	caddr_t addr;
	size_t size;
	uint64_t dmac_addr;
	ddi_dma_handle_t dma_handle;
	ddi_acc_handle_t mem_handle;
};

struct dwmac_sc;
struct dwmac_rx_packet {
	struct dwmac_rx_packet *next;
	struct dwmac_dma dma;
	mblk_t *mp;
	frtn_t free_rtn;
	struct dwmac_sc *sc;
};

struct dwmac_desc_ring {
	int slots;
	int index;
};

struct dwmac_mcast {
	list_node_t		node;
	uint8_t			addr[ETHERADDRL];
};

#define TX_DESC_NUM	64
#define RX_DESC_NUM	64
#define RX_PKT_NUM_MAX	64

struct dwmac_desc_tx_ring {
	struct dwmac_dma desc;
	struct dwmac_dma pkt[TX_DESC_NUM];
	int head;
	int tail;
};

struct dwmac_desc_rx_ring {
	struct dwmac_dma desc;
	struct dwmac_rx_packet *pkt[RX_DESC_NUM];
	int index;
};

struct dwmac_sc {
	dev_info_t *dip;
	kmutex_t intrlock;
	kmutex_t rx_pkt_lock;
	int rx_pkt_num;

	mac_handle_t mac_handle;
	mii_handle_t mii_handle;
	ddi_intr_handle_t intr_handle;
	link_duplex_t phy_duplex;

	mac_register_t *macp;
	struct dwmac_reg reg;
	struct dwmac_desc_tx_ring tx_ring;
	struct dwmac_desc_rx_ring rx_ring;
	int running;
	int phy_speed;
	int phy_id;
	uint8_t dev_addr[ETHERADDRL];
	struct dwmac_rx_packet *rx_pkt_free;
	list_t mcast;
};

