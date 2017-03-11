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

// enet csr
#define ENET_CSR_MAC_ADDR		(0x0000 + 0x00)
#define ENET_CSR_MAC_COMMAND		(0x0000 + 0x04)
#define ENET_CSR_MAC_WRITE		(0x0000 + 0x08)
#define ENET_CSR_MAC_READ		(0x0000 + 0x0c)
#define ENET_CSR_MAC_COMMAND_DONE	(0x0000 + 0x10)


#define ENET_CSR_RSIF_CONFIG		(0x2000 + 0x0010)
#define ENET_CSR_RSIF_RAM_DBG		(0x2000 + 0x0048)
#define ENET_CSR_RGMII			(0x2000 + 0x07e0)
#define ENET_CSR_CFG_LINK_AGGR_RESUME	(0x2000 + 0x07c8)
#define ENET_CSR_DEBUG			(0x2000 + 0x0700)
#define	ENET_CSR_CFG_BYPASS		(0x2000 + 0x0294)
#define	ENET_CSR_CLE_BYPASS0		(0x2000 + 0x0490)
#define	ENET_CSR_CLE_BYPASS1		(0x2000 + 0x0494)

#define	ENET_CSR_ICM_CONFIG0		(0x2800 + 0x0400)
#define	ENET_CSR_ICM_CONFIG2		(0x2800 + 0x0410)
#define	ENET_CSR_RX_DV_GATE		(0x2800 + 0x05fc)

#define ENET_CSR_CFGSSQMIWQASSOC	(0x9000 + 0xe0)
#define ENET_CSR_CFGSSQMIFPQASSOC	(0x9000 + 0xdc)
#define ENET_CSR_CFGSSQMIQMLITEFPQASSOC	(0x9000 + 0xf0)
#define ENET_CSR_CFGSSQMIQMLITEWQASSOC	(0x9000 + 0xf4)

#define ENET_CSR_CFG_MEM_RAM_SHUTDOWN	(0xd000 + 0x70)
#define ENET_CSR_BLOCK_MEM_RDY		(0xd000 + 0x74)

// enet mac
#define ENET_MAC_CONFIG_1		0x00
#define ENET_MAC_CONFIG_2		0x04
#define ENET_MAC_MII_MGMT_CONFIG	0x20
#define ENET_MAC_MII_MGMT_COMMAND	0x24
#define ENET_MAC_MII_MGMT_ADDRESS	0x28
#define ENET_MAC_MII_MGMT_CONTROL	0x2c
#define ENET_MAC_MII_MGMT_STATUS	0x30
#define ENET_MAC_MII_MGMT_INDICATORS	0x34
#define ENET_MAC_INTERFACE_CONTROL	0x38
#define ENET_MAC_STATION_ADDR0		0x40
#define ENET_MAC_STATION_ADDR1		0x44

// enet ring csr
#define ENET_RING_CSR_ID		0x0008
#define ENET_RING_CSR_ID_BUF		0x000c
#define ENET_RING_CSR_NE_INT_MODE	0x017c
#define ENET_RING_CSR_CONFIG		0x006c
#define ENET_RING_CSR_WR_BASE		0x0070
#define ENET_RING_CSR_CLKEN		0xc208
#define ENET_RING_CSR_SRST		0xc200

// enet ring command
#define ENET_RING_CMD_INC_DEC		0x002c


#define	ENET_CSR_MAC_COMMAND_WRITE	(1u << 31)
#define	ENET_CSR_MAC_COMMAND_READ	(1u << 30)

#define ENET_MAC_MII_MGMT_INDICATORS_BUSY	(1u << 0)
#define ENET_MAC_MII_MGMT_COMMAND_READ		(1u << 0)

struct enet_ringcfg {
	union {
		uint32_t cfg0;
	};
	union {
		uint32_t cfg1;
	};
	union {
		uint32_t cfg2;
		struct {
			uint32_t		: 4;
			uint32_t qcoherent	: 1;
			uint32_t addr_lo	: 27;
		};
	};
	union {
		uint32_t cfg3;
		struct {
			uint32_t addr_hi	: 8;
			uint32_t		: 11;
			uint32_t acceptlerr	: 1;
			uint32_t mode		: 3; // 20
			uint32_t size		: 3; // 23
			uint32_t		: 1;
			uint32_t recombbuf	: 1; // 27
			uint32_t recomtimeout_l	: 4;
		};
	};
	union {
		uint32_t cfg4;
		struct {
			uint32_t recomtimeout_h	: 3;
			uint32_t selthrsh	: 3;
			uint32_t		: 13;
			uint32_t type		: 2; // 19
			uint32_t		: 11;
		};
	};
};

struct enet_desc16 {
	union {
		volatile uint64_t m0;
		struct {
			uint64_t info		: 32;
			uint64_t fpqnum		: 12;
			uint64_t		: 8;
			uint64_t stash		: 2;
			uint64_t		: 6;
			uint64_t lerr		: 3;
			uint64_t		: 1;
		};
	};
	union {
		volatile uint64_t m1;
		struct {
			uint64_t addr		: 42;
			uint64_t		: 6;
			uint64_t len		: 12;
			uint64_t		: 3;
			uint64_t coherent	: 1;
		};
	};
};

struct enet_desc {
	union {
		volatile uint64_t m0;
		struct {
			uint64_t info		: 32;
			uint64_t fpqnum		: 12;
			uint64_t		: 8;
			uint64_t stash		: 2;
			uint64_t		: 6;
			uint64_t lerr		: 3;
			uint64_t		: 1;
		};
	};
	union {
		volatile uint64_t m1;
		struct {
			uint64_t addr		: 42;
			uint64_t		: 6;
			uint64_t len		: 12;
			uint64_t		: 3;
			uint64_t coherent	: 1;
		};
	};
	union {
		volatile uint64_t m2;
	};
	union {
		volatile uint64_t m3;
		struct {
			uint64_t tcphdr		: 6;
			uint64_t iphdr		: 6;
			uint64_t		: 10;
			uint64_t ec		: 1; // 22
			uint64_t		: 1;
			uint64_t is		: 1; // 24
			uint64_t		: 10;
			uint64_t ic		: 1; // 35
			uint64_t		: 8;
			uint64_t type		: 4; // 44
			uint64_t henqnum	: 12;
			uint64_t		: 4;
		};
	};
};

union enet_ring_id {
	struct {
		uint16_t bufnum	: 6;
		uint16_t owner	: 4;
		uint16_t	: 6;
	};
	uint16_t val;
};

union enet_dst_ring {
	struct {
		uint16_t num	: 10;
		uint16_t rm	: 2;
		uint16_t	: 4;
	};
	uint16_t val;
};

#define ENET_RM0	0
#define ENET_RM1	1
#define ENET_RM3	3

#define	ENET_RING_CFGSIZE_512B	0
#define	ENET_RING_CFGSIZE_2KB	1
#define	ENET_RING_CFGSIZE_16KB	2
#define	ENET_RING_CFGSIZE_64KB	3
#define	ENET_RING_CFGSIZE_512KB	4

#define	ENET_RING_OWNER_ETH0	0
#define	ENET_RING_OWNER_ETH1	1
#define	ENET_RING_OWNER_CPU	0xf

#define ENET_DESC_M1_EMPTY	~0ULL

#define ENET_CPU_BUFNUM_START(port)	((port) == 0? 0: 12)
#define ENET_ETH_BUFNUM_START(port)	((port) == 0? 2: 10)
#define ENET_BUFPOOL_BUFNUM_START(port)	((port) == 0? 0x22: 0x2a)
#define ENET_RING_NUM_START(port)	((port) == 0? 8: 264)


enum {
	ENET_CSR,
	RING_CSR,
	RING_CMD,
	NUM_ENET_REGS
};

struct enet_reg {
	caddr_t addr;
};

struct enet_desc_ring {
	union enet_ring_id id;
	union enet_dst_ring dst_ring;
	caddr_t cmd_base;
	struct enet_ringcfg cfg;
	int slots;
	int cfgsize;
	int index;
	uint16_t num;
	bool is_bufpool;

	struct {
		caddr_t addr;
		size_t size;
		uint64_t dmac_addr;
	} desc;

	struct {
		caddr_t addr;
		size_t size;
		uint64_t dmac_addr;
	} buf;
};


struct enet_sc {
	struct enet_reg reg[NUM_ENET_REGS];
	struct enet_desc_ring tx_ring;
	struct enet_desc_ring txc_ring;
	struct enet_desc_ring rx_ring;
	struct enet_desc_ring rxc_ring;
	int running;
	int phy_speed;
	int phy_id;
	int rm;
	int port;
	uint8_t dev_addr[6];
};

void init_menet(void);

