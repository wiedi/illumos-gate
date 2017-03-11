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

#include <sys/systm.h>
#include <sys/salib.h>
#include <sys/promif.h>
#include <util/sscanf.h>
#include <sys/byteorder.h>
#include <sys/platform.h>
#include <util/sscanf.h>
#include <sys/memlist.h>
#include <sys/memlist_impl.h>
#include <sys/machparam.h>
#include <sys/controlregs.h>
#include "boot_plat.h"
#include "sata.h"
#include "prom_dev.h"

extern struct memlist	*pfreelistp, *pinstalledp, *pscratchlistp, *piolistp;

static void
usecwait(int usec)
{
	uint64_t cnt = (read_cntpct() / (read_cntfrq() / 1000000)) + usec + 2;
	for (;;) {
		if ((read_cntpct() / (read_cntfrq() / 1000000)) > cnt)
			break;
	}

}

static int
get_address_cells(pnode_t node)
{
	int address_cells = 0;

	ASSERT(node >= 0);

	node = prom_parentnode(node);
	while (node > 0) {
		int len = prom_getproplen(node, "#address-cells");
		if (len > 0) {
			ASSERT(len == sizeof(int));
			int prop;
			prom_getprop(node, "#address-cells", (caddr_t)&prop);
			address_cells = ntohl(prop);
			break;
		}
		node = prom_parentnode(node);
	}
	return address_cells;
}

static int
get_size_cells(pnode_t node)
{
	int size_cells = 0;

	ASSERT(node >= 0);

	node = prom_parentnode(node);
	while (node > 0) {
		int len = prom_getproplen(node, "#size-cells");
		if (len > 0) {
			ASSERT(len == sizeof(int));
			int prop;
			prom_getprop(node, "#size-cells", (caddr_t)&prop);
			size_cells = ntohl(prop);
			break;
		}
		node = prom_parentnode(node);
	}
	return size_cells;
}

enum {
	REG_HOST,
	REG_CSR,
	REG_DIAG,
	REG_AXI,
	REG_MUX,
	REG_NUM,
};

static uint32_t
reg_read(uintptr_t addr, int offset)
{
    return *(volatile uint32_t *)(addr + offset);
}
static
void reg_write(uintptr_t addr, int offset, uint32_t val)
{
    *(volatile uint32_t *)(addr + offset) = val;
}

static uintptr_t
get_reg(pnode_t node, int reg_num)
{
	int address_cells = get_address_cells(node);
	int size_cells = get_size_cells(node);
	int len = prom_getproplen(node, "reg");
	if (len <= 0)
		return 0;

	if (len < sizeof(uint32_t) * (address_cells + size_cells) * reg_num)
		return 0;

	uint32_t *reg = __builtin_alloca(len);
	prom_getprop(node, "reg", (caddr_t)reg);

	uint64_t addr = 0;
	reg += (address_cells + size_cells) * reg_num;
	if (address_cells == 2) {
		addr = ((uint64_t)(ntohl(*reg)) << 32) | ntohl(*(reg + 1));
	} else {
		addr = ntohl(*reg);
	}

	return addr;
}

static int
init_ctrl(pnode_t node)
{
	uint64_t mux_addr = get_reg(node, REG_MUX);
	uint64_t diag_addr = get_reg(node, REG_DIAG);
	uint64_t host_addr = get_reg(node, REG_HOST);
	uint64_t csr_addr = get_reg(node, REG_CSR);
	uint64_t axi_addr = get_reg(node, REG_AXI);

	if (diag_addr == 0 || host_addr == 0 || csr_addr == 0 || axi_addr == 0)
		return -1;

	if (mux_addr) {
		uint32_t val = reg_read(mux_addr, 0);
		reg_write(mux_addr, 0, val & ~0x1);
		reg_read(mux_addr, 0);
	}

	if (reg_read(diag_addr, 0x70) != 0) {
		reg_write(diag_addr, 0x70, 0);
		usecwait(1000);
		if (reg_read(diag_addr, 0x70) != 0)
			return -1;
		if (reg_read(diag_addr, 0x74) != 0xffffffff)
			return -1;
	}

	int num_ports = (reg_read(host_addr, 0x00) & 0x1f);

	for (int i = 0; i < num_ports; i++) {
		// PORTCFG
		reg_write(host_addr, 0xa4, (reg_read(host_addr, 0xa4) & ~0x3f) | (i + 2));
		reg_read(host_addr, 0xa4);

		// PORTPHY1CFG
		reg_write(host_addr, 0xa8, 0x0001fffe);
		reg_read(host_addr, 0xa8);

		// PORTPHY2CFG
		reg_write(host_addr, 0xac, 0x28183219);
		reg_read(host_addr, 0xac);

		// PORTPHY3CFG
		reg_write(host_addr, 0xb0, 0x13081008);
		reg_read(host_addr, 0xb0);

		// PORTPHY4CFG
		reg_write(host_addr, 0xb4, 0x00480815);
		reg_read(host_addr, 0xb4);

		// PORTPHY5CFG
		reg_write(host_addr, 0xb8, (reg_read(host_addr, 0xb8) & ~(0xfff << 20)) | (0x300 << 20));
		reg_read(host_addr, 0xb8);

		// PORTAXICFG
		reg_write(host_addr, 0xbc, (reg_read(host_addr, 0xbc) & ~(0xf << 20)) | (0xe << 20) | (1 << 24));
		reg_read(host_addr, 0xbc);

		// PORTRANSCFG
		reg_write(host_addr, 0xc8, (reg_read(host_addr, 0xc8) & ~0x7f) | 0x30);
		reg_read(host_addr, 0xc8);
	}

	// clear interrupt status
	reg_write(host_addr, 0x08, 0xffffffff);
	reg_read(host_addr, 0x08);

	// unmask interrupt mask
	reg_write(csr_addr, 0x2c, 0);
	reg_read(csr_addr, 0x2c);

	// unmask error interrupt mask
	reg_write(csr_addr, 0x34, 0);
	reg_read(csr_addr, 0x34);

	// unmask timeout mask
	reg_write(axi_addr, 0x10, 0);
	reg_read(axi_addr, 0x10);

	// clear error
	reg_write(csr_addr, 0x00, 0xffffffff); // slave read error
	reg_write(csr_addr, 0x04, 0xffffffff); // slave write error
	reg_write(csr_addr, 0x08, 0xffffffff); // master read error
	reg_write(csr_addr, 0x0c, 0xffffffff); // master write error

	// enable coherency
	reg_write(csr_addr, 0x14, reg_read(csr_addr, 0x14) & ~0x3);
	reg_read(csr_addr, 0x14);

	reg_write(csr_addr, 0x18, reg_read(csr_addr, 0x18) | ((1u << 3) | (1u << 9)));
	reg_read(csr_addr, 0x18);

	// reset
	reg_write(host_addr, 0x04, reg_read(host_addr, 0x04) | (1u << 0));
	while (reg_read(host_addr, 0x04) & (1u << 0)) {}

	// enable ahci
	reg_write(host_addr, 0x04, (1u << 31));

	reg_write(host_addr, 0x00, (reg_read(host_addr, 0x00) & ((1u << 28) | (1u << 17))) | (1u << 27));
	reg_write(host_addr, 0x0c, 0xf);

	return 0;
}

struct sata_sc {
	pnode_t node;
	uint32_t attached_port;
	uintptr_t buffer[32];
};

#define CMD_LIST_BUFFER_OFFSET		0x000
#define FIS_BUFFER_OFFSET		0x100
#define CMD_TABLE_BUFFER_OFFSET		0x200
#define CMD_TABLE_SG_BUFFER_OFFSET	0x280
#define DATA_BUFFER_OFFSET		0x800

static struct sata_sc sata_dev[3];

static int
sata_open(const char *name)
{
	int i;
	uint32_t addr;
	uint32_t port;
	char buf[80];
	if (sscanf(name, "/soc/sata@%x/disk@%d,0", &addr, &port) != 2) {
		return -1;
	}
	sprintf(buf, "/soc/sata@%x", addr);

	pnode_t node = prom_finddevice(buf);
	if (node < 0)
		return -1;

	int len = prom_getproplen(node, "status");
	if (len <= 0 || len >= sizeof(buf))
		return -1;
	prom_getprop(node, "status", (caddr_t)buf);
	if (strcmp(buf, "ok") != 0)
		return -1;

	for (i = 0; i < sizeof(sata_dev) / sizeof(sata_dev[0]); i++) {
		if (sata_dev[i].node == node) {
			break;
		}
	}
	if (i == sizeof(sata_dev) / sizeof(sata_dev[0])) {
		for (i = 0; i < sizeof(sata_dev) / sizeof(sata_dev[0]); i++) {
			if (sata_dev[i].node == 0) {
				break;
			}
		}
		if (i == sizeof(sata_dev) / sizeof(sata_dev[0])) {
			return -1;
		}

		if (init_ctrl(node) < 0)
			return -1;

	} else {
		if (sata_dev[i].attached_port & (1u << port))
			return -1;
	}

	usecwait(100000);
	uintptr_t host_addr = get_reg(node, REG_HOST);
	uintptr_t port_addr = host_addr + 0x100 + port * 0x80;

	int num_ports = (reg_read(host_addr, 0x00) & 0x1f) + 1;
	uint32_t port_map = reg_read(host_addr, 0x0c);
	if (port >= num_ports || ((1u << port) & port_map) == 0)
		return -1;

	reg_write(port_addr, 0x18, reg_read(port_addr, 0x18) | (1u << 1));
	int j = 0;
	while (j < 200) {
		usecwait(1000);
		if ((reg_read(port_addr, 0x28) & 0x3) == 0x3)
			break;
		j++;
	}
	if (j == 200) {
		return -1;
	}
	// clear error
	reg_write(port_addr, 0x30, reg_read(port_addr, 0x30));
	// clear irq
	reg_write(port_addr, 0x10, reg_read(port_addr, 0x10));

	reg_write(host_addr, 0x08, 1u << i);
	reg_write(host_addr, 0x04, reg_read(host_addr, 0x04) | (1u << 1));

	sata_dev[i].node = node;
	sata_dev[i].attached_port |= (1u << port);
	sata_dev[i].buffer[port] = memlist_get(MMU_PAGESIZE, MMU_PAGESIZE, &pfreelistp);
	memset((void *)sata_dev[i].buffer[port], 0, MMU_PAGESIZE);
	// Command List Base
	uintptr_t cmd_list = sata_dev[i].buffer[port] + CMD_LIST_BUFFER_OFFSET;
	uintptr_t fis = sata_dev[i].buffer[port] + FIS_BUFFER_OFFSET;
	reg_write(port_addr, 0x00, cmd_list& 0xffffffff);
	reg_write(port_addr, 0x04, (cmd_list >> 32) & 0xffffffff);
	reg_write(port_addr, 0x08, fis & 0xffffffff);
	reg_write(port_addr, 0x0c, (fis >> 32) & 0xffffffff);

	reg_write(port_addr, 0x18, (1 << 28) | (1 << 4) | (1 << 2) | (1 << 1) | (1 << 0));

	return i | (port << 8);
}

static ssize_t
sata_read(int dev, caddr_t buf, size_t buf_len, uint_t startblk)
{
	int index = dev & 0xFF;
	int port = (dev >> 8) & 0xFF;

	uintptr_t host_addr = get_reg(sata_dev[index].node, REG_HOST);
	uintptr_t port_addr = host_addr + 0x100 + port * 0x80;

	uintptr_t cmd_addr = (sata_dev[index].buffer[port] + CMD_LIST_BUFFER_OFFSET);
	uintptr_t sg_addr = (sata_dev[index].buffer[port] + CMD_TABLE_SG_BUFFER_OFFSET);
	uintptr_t fis_addr = (sata_dev[index].buffer[port] + CMD_TABLE_BUFFER_OFFSET);
	uintptr_t buf_addr = (sata_dev[index].buffer[port] + DATA_BUFFER_OFFSET);

	size_t left_block = buf_len / 512;
	while (left_block) {
		size_t read_block = ((left_block > 4)? 4: left_block);

		uint8_t *fis = (void *)fis_addr;
		memset(fis, 0, 20);

		fis[0] = 0x27;
		fis[1] = (1u << 7);
		fis[2] = 0x24; // 0x25?
		fis[3] = 0xe0;
		fis[4] = (startblk >> 0) & 0xff;
		fis[5] = (startblk >> 8) & 0xff;
		fis[6] = (startblk >> 16) & 0xff;
		fis[7] = 1 << 6;
		fis[8] = (startblk >> 24) & 0xff;
		fis[12] = (read_block >> 0) & 0xff;
		fis[13] = (read_block >> 8) & 0xff;

		struct {
			uint32_t	addr;
			uint32_t	addr_hi;
			uint32_t	rsv;
			uint32_t	size;
		} *sg = (void *)sg_addr;

		sg->addr_hi = (buf_addr >> 32);
		sg->addr = (buf_addr & 0xFFFFFFFF);
		sg->size = read_block * 512;

		struct {
			uint32_t	opts;
			uint32_t	status;
			uint32_t	tbl_addr;
			uint32_t	tbl_addr_hi;
			uint32_t	rsv[4];
		} *cmd = (void *)cmd_addr;

		cmd->opts = (1 << 16) | (0 << 6) | (20 >> 2);
		cmd->status = 0;
		cmd->tbl_addr = fis_addr & 0xffffffff;
		cmd->tbl_addr_hi = fis_addr >> 32;

		memset((void *)buf_addr, 0, read_block * 512);

		dsb();
		reg_write(port_addr, 0x38, 1);
		int j = 0;
		while (j < 2000) {
			usecwait(1000);
			if ((reg_read(port_addr, 0x38) & 0x1) == 0)
				break;
			j++;
		}
		if (j == 2000) {
			prom_printf("port_stat %08x\n", reg_read(port_addr, 0x28));
			prom_printf("cmd issue %08x\n", reg_read(port_addr, 0x38));
			return -1;
		}

		memcpy(buf, (void *)buf_addr, read_block * 512);

		startblk += read_block;
		left_block -= read_block;
		buf += read_block * 512;
	}

	return buf_len ;
}

static int
sata_match(const char *path)
{
	const char *cmp = "/soc/sata@";
	if (strncmp(path, cmp, strlen(cmp)) == 0)
		return 1;
	return 0;
}
static struct prom_dev sata_prom_dev =
{
	.match = sata_match,
	.open = sata_open,
	.read = sata_read,
};

void init_sata(void)
{
	prom_register(&sata_prom_dev);
}

