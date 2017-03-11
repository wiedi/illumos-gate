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
#include <sys/sysmacros.h>
#include <sys/machparam.h>
#include <sys/controlregs.h>
#include <string.h>
#include <sys/platform.h>
#include <sys/platmod.h>
#include <sys/byteorder.h>
#include <sys/mutex.h>
#include <sys/debug.h>


static kmutex_t plat_clk_lock;


static int
clk_enable_impl(pnode_t node)
{
	int ret = 0;
	uint64_t base;
	if (prom_is_compatible(node, "apm,xgene-device-clock")) {
		if (prom_get_reg_by_name(node, "csr-reg", &base) == 0) {
			uint32_t csr_offset = prom_get_prop_int(node, "csr-offset", 0);
			uint32_t csr_mask = prom_get_prop_int(node, "csr-mask", 0xf);
			uint32_t enable_offset = prom_get_prop_int(node, "enable-offset", 8);
			uint32_t enable_mask = prom_get_prop_int(node, "enable-mask", 0xf);
			base += SEGKPM_BASE;
			*((volatile uint32_t *)(base + enable_offset)) |= enable_mask;
			*((volatile uint32_t *)(base + csr_offset)) &= ~csr_mask;
		}
	} else if (prom_is_compatible(node, "apm,xgene-socpll-clock")) {
	} else if (prom_is_compatible(node, "apm,xgene-pcppll-clock")) {
	} else if (prom_is_compatible(node, "fixed-clock")) {
	} else if (prom_is_compatible(node, "fixed-factor-clock")) {
	} else {
		return -1;
	}

	pnode_t clock_node = 0;
	int len = prom_getproplen(node, "clocks");
	if (len == 8) {
		int clocks[2];
		prom_getprop(node, "clocks", (caddr_t)clocks);
		clock_node = prom_findnode_by_phandle(htonl(clocks[0]));
	}
	if (clock_node > 0) {
		ret = clk_enable_impl(clock_node);
	}
	return ret;
}

static int
clk_disable_impl(pnode_t node)
{
	int ret = 0;
	uint64_t base;
	if (prom_is_compatible(node, "apm,xgene-device-clock")) {
		if (prom_get_reg_by_name(node, "csr-reg", &base) == 0) {
			uint32_t csr_offset = prom_get_prop_int(node, "csr-offset", 0);
			uint32_t csr_mask = prom_get_prop_int(node, "csr-mask", 0xf);
			uint32_t enable_offset = prom_get_prop_int(node, "enable-offset", 8);
			uint32_t enable_mask = prom_get_prop_int(node, "enable-mask", 0xf);
			base += SEGKPM_BASE;
			*((volatile uint32_t *)(base + csr_offset)) |= csr_mask;
			*((volatile uint32_t *)(base + enable_offset)) &= ~enable_mask;
		}
	} else if (prom_is_compatible(node, "apm,xgene-socpll-clock")) {
	} else if (prom_is_compatible(node, "apm,xgene-pcppll-clock")) {
	} else if (prom_is_compatible(node, "fixed-clock")) {
	} else if (prom_is_compatible(node, "fixed-factor-clock")) {
	} else {
		return -1;
	}
	return ret;
}

int plat_clk_enable(const char *name)
{
	char buf[80];
	pnode_t clock = prom_finddevice("/soc/clocks");
	if (clock <= 0) {
		return -1;
	}

	pnode_t node = prom_childnode(clock);
	while (node > 0) {
		int len = prom_getproplen(node, "clock-output-names");
		if (0 < len && len < 80) {
			prom_getprop(node, "clock-output-names", buf);
			if (strcmp(name, buf) == 0)
				break;
		}
		node = prom_nextnode(node);
	}
	if (node <= 0) {
		return -1;
	}

	mutex_enter(&plat_clk_lock);
	int ret = clk_enable_impl(node);
	mutex_exit(&plat_clk_lock);

	return ret;
}

int plat_clk_disable(const char *name)
{
	char buf[80];
	pnode_t clock = prom_finddevice("/soc/clocks");
	if (clock <= 0) {
		return -1;
	}

	pnode_t node = prom_childnode(clock);
	while (node > 0) {
		int len = prom_getproplen(node, "clock-output-names");
		if (0 < len && len < 80) {
			prom_getprop(node, "clock-output-names", buf);
			if (strcmp(name, buf) == 0)
				break;
		}
		node = prom_nextnode(node);
	}
	if (node <= 0) {
		return -1;
	}

	mutex_enter(&plat_clk_lock);
	int ret = clk_disable_impl(node);
	mutex_exit(&plat_clk_lock);

	return ret;
}

int plat_clk_get_rate(const char *name)
{
	ASSERT(1);
	return -1;
}

int plat_clk_set_rate(const char *name, int rate)
{
	ASSERT(1);
	return -1;
}

void xgene_init_clock(void)
{
	mutex_init(&plat_clk_lock, NULL, MUTEX_DRIVER, NULL);
}
