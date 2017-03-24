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

#include <sys/param.h>
#include <sys/time.h>
#include <sys/clock.h>
#include <sys/rtc.h>
#include <sys/sysmacros.h>
#include <sys/stat.h>
#include <sys/sunddi.h>
#include <sys/promif.h>
#include <sys/platmod.h>
#include <stdbool.h>

static pnode_t rtc_node = -1;
static int rtc_address = -1;
static pnode_t i2c_node = -1;
static uint64_t i2c_base;

#define I2C_M_0_CONTROL(base)		(*(volatile uint32_t *)((base) + 0x00))
#define I2C_M_0_SLAVE_ADDRESS(base)	(*(volatile uint32_t *)((base) + 0x04))
#define I2C_M_0_TOKEN_LIST_REG0(base)	(*(volatile uint32_t *)((base) + 0x08))
#define I2C_M_0_TOKEN_LIST_REG1(base)	(*(volatile uint32_t *)((base) + 0x0c))
#define I2C_M_0_TOKEN_WDATA_REG0(base)	(*(volatile uint32_t *)((base) + 0x10))
#define I2C_M_0_TOKEN_WDATA_REG1(base)	(*(volatile uint32_t *)((base) + 0x14))
#define I2C_M_0_TOKEN_RDATA_REG0(base)	(*(volatile uint32_t *)((base) + 0x18))
#define I2C_M_0_TOKEN_RDATA_REG1(base)	(*(volatile uint32_t *)((base) + 0x1c))

union i2c_control {
	uint32_t dw;
	struct {
		uint32_t start		:	1;
		uint32_t ack_ignore	:	1;
		uint32_t status		:	1;
		uint32_t error		:	1;
		uint32_t current_token	:	4;
		uint32_t read_data_count:	4;
		uint32_t qtr_clk_dly	:	10;
		uint32_t manual		:	1;
		uint32_t lscl		:	1;
		uint32_t lsda		:	1;
		uint32_t scl		:	1;
		uint32_t sda		:	1;
		uint32_t		:	1;
		uint32_t qtr_clk_ext	:	2;
		uint32_t		:	1;
		uint32_t ctrl_jic	:	1;
	};
};

union i2c_slave_address {
	uint32_t dw;
	struct {
		uint32_t slave_address		:	8;
		uint32_t sda_filter		:	3;
		uint32_t scl_filter		:	3;
		uint32_t			:	2;
		uint32_t scl_low_delay		:	12;
		uint32_t use_cntl_scl_low	:	1;
		uint32_t			:	3;
	};
};

enum i2c_token {
	I2C_TOKEN_END = 0,
	I2C_TOKEN_START,
	I2C_TOKEN_SLAVE_ADDR_WRITE,
	I2C_TOKEN_SLAVE_ADDR_READ,
	I2C_TOKEN_DATA,
	I2C_TOKEN_DATA_LAST,
	I2C_TOKEN_STOP,
};

static bool
prom_is_ok(pnode_t node)
{
	int len;
	char buf[80];

	len = prom_getproplen(node, "status");
	if (0 <= len &&  len <= sizeof(buf)) {
		prom_getprop(node, "status", (caddr_t)buf);
		if (strcmp(buf, "ok") == 0 || strcmp(buf, "okay") == 0)
			return true;
	}
	return false;
}

static pnode_t
find_compatible_node(pnode_t node, const char *compatible)
{
	static char buf[120];
	prom_getprop(node, "name", buf);
	if (prom_is_compatible(node, compatible)) {
		prom_getprop(node, "name", buf);
		if (prom_is_ok(node)) {
			prom_getprop(node, "name", buf);
			return node;
		}
	}

	pnode_t child = prom_childnode(node);
	while (child > 0) {
		prom_getprop(child, "name", buf);
		node = find_compatible_node(child, compatible);
		if (node > 0)
			return node;
		child = prom_nextnode(child);
	}
	return OBP_NONODE;
}

#define CLK81_FREQ	166666666

static bool
i2c_setup_pinmux(pnode_t node)
{
	char name[80];
	int index = prom_get_prop_index(node, "pinctrl-names", "default");
	if (index < 0)
		return false;
	sprintf(name, "pinctrl-%d", index);

	int len = prom_getproplen(node, name);
	if (len == 0)
		return true;
	if (len != sizeof(uint32_t))
		return false;

	uint32_t pinctrl;
	prom_getprop(node, name, (caddr_t)&pinctrl);

	pnode_t pinctrl_node;
	pinctrl_node = prom_findnode_by_phandle(htonl(pinctrl));
	if (pinctrl_node < 0)
		return false;

	if (plat_pinmux_set(pinctrl_node) < 0)
		return false;
	drv_usecwait(10);
	return true;
}

static bool
i2c_prepare(pnode_t node, uint64_t base, int slave)
{
	int master_i2c_speed = prom_get_prop_int(node, "master_i2c_speed", 400000);

	uint32_t i2c_clock_set = ((CLK81_FREQ / master_i2c_speed) >> 1);

	union i2c_control control;
	union i2c_slave_address slave_address;

	control.dw = I2C_M_0_CONTROL(SEGKPM_BASE + base);
	control.qtr_clk_dly = i2c_clock_set;
	control.qtr_clk_ext = i2c_clock_set >> 10;
	I2C_M_0_CONTROL(SEGKPM_BASE + base) = control.dw;
	slave_address.dw = I2C_M_0_SLAVE_ADDRESS(SEGKPM_BASE + base);
	slave_address.scl_low_delay = i2c_clock_set >> 1;
	slave_address.use_cntl_scl_low = 1;
	slave_address.sda_filter = 0;
	slave_address.scl_filter = 0;
	slave_address.slave_address = slave << 1;
	I2C_M_0_SLAVE_ADDRESS(SEGKPM_BASE + base) = slave_address.dw;

	return true;
}


static bool
i2c_xfer_token(uint64_t base, uint8_t *token)
{
	uint32_t val;
	val = 0;
	for (int i = 0; i < 8; i++)
		val |= (token[i] << (i * 4));
	I2C_M_0_TOKEN_LIST_REG0(SEGKPM_BASE + base) = val;
	I2C_M_0_TOKEN_LIST_REG1(SEGKPM_BASE + base) = 0;

	// start
	union i2c_control control;
	control.dw = I2C_M_0_CONTROL(SEGKPM_BASE + base);
	control.start = 0;
	I2C_M_0_CONTROL(SEGKPM_BASE + base) = control.dw;
	control.start = 1;
	I2C_M_0_CONTROL(SEGKPM_BASE + base) = control.dw;

	for (int i = 0; i < 1000000; i++) {
		drv_usecwait(10);
		control.dw = I2C_M_0_CONTROL(SEGKPM_BASE + base);
		if (control.status == 0)
			break;
	}
	if (control.status) {
		cmn_err(CE_WARN, "%s:%d busy",__func__,__LINE__);
		return false;
	}
	if (control.error) {
		cmn_err(CE_WARN, "%s:%d error",__func__,__LINE__);
		return false;
	}
	return true;
}

static void
i2c_stop(uint64_t base)
{
	union i2c_control control;
	control.dw = I2C_M_0_CONTROL(SEGKPM_BASE + base);
	if (control.error== 0) {
		uint8_t token[8] = {0};
		token[0] = I2C_TOKEN_STOP;
		i2c_xfer_token(base, token);
	}
}

static bool
i2c_read(uint64_t base, int slave, uint8_t *buf, size_t size)
{
	{
		uint8_t token[8] = {0};
		token[0] = I2C_TOKEN_START;
		token[1] = I2C_TOKEN_SLAVE_ADDR_READ;
		if (!i2c_xfer_token(base, token))
			return false;
	}

	while (size > 0) {
		size_t read_len = ((size > 8)? 8: size);
		uint8_t token[8] = {0};
		for (int i = 0; i < read_len; i++)
			token[i] = I2C_TOKEN_DATA;
		if (size == read_len)
			token[read_len - 1] = I2C_TOKEN_DATA_LAST;

		if (!i2c_xfer_token(base, token))
			return false;

		union {
			uint32_t rdata[2];
			uint8_t uc[8];
		} u;
		u.rdata[0] = I2C_M_0_TOKEN_RDATA_REG0(SEGKPM_BASE + base);
		u.rdata[1] = I2C_M_0_TOKEN_RDATA_REG1(SEGKPM_BASE + base);

		for (int i = 0; i < read_len; i++)
			buf[i] = u.uc[i];
		size -= read_len;
		buf += read_len;
	}

	i2c_stop(base);

	return true;
}

static bool
i2c_write(uint64_t base, int slave, const uint8_t *buf, size_t size)
{
	{
		uint8_t token[8] = {0};
		token[0] = I2C_TOKEN_START;
		token[1] = I2C_TOKEN_SLAVE_ADDR_WRITE;
		if (!i2c_xfer_token(base, token))
			return false;
	}

	while (size > 0) {
		size_t write_len = ((size > 8)? 8: size);
		uint8_t token[8] = {0};
		for (int i = 0; i < write_len; i++)
			token[i] = I2C_TOKEN_DATA;

		union {
			uint32_t wdata[2];
			uint8_t uc[8];
		} u = {0};
		for (int i = 0; i < write_len; i++)
			u.uc[i] = buf[i];

		I2C_M_0_TOKEN_WDATA_REG0(SEGKPM_BASE + base) = u.wdata[0];
		I2C_M_0_TOKEN_WDATA_REG1(SEGKPM_BASE + base) = u.wdata[1];

		if (!i2c_xfer_token(base, token))
			return false;

		size -= write_len;
		buf += write_len;
	}

	i2c_stop(base);

	return true;
}

static bool
i2c_probe(pnode_t node, uint64_t base, int slave)
{
	if (!i2c_setup_pinmux(node))
		return false;

	if (!i2c_prepare(node, base, slave))
		return false;

	uint8_t buf[1] = {0};
	return i2c_write(base, slave, buf, 1);
}


static void
i2c_poweron(pnode_t node)
{
	struct prom_hwreset hwreset;
	if (prom_get_reset(node, 0, &hwreset) == 0) {
		uint64_t base;
		if (prom_get_reg(hwreset.node, 0, &base) == 0) {
			*(volatile uint32_t *)(SEGKPM_BASE + base + ((0x50 + hwreset.id / 32) << 2)) |= (1u << (hwreset.id % 32));
			drv_usecwait(10);
		}
	}
}

static void
init_rtc(void)
{
	pnode_t node;

	node = find_compatible_node(prom_rootnode(), "nxp,pcf8563");
	if (node > 0) {
		uint64_t slave;
		if (prom_get_reg(node, 0, &slave) == 0) {
			pnode_t parent = prom_parentnode(node);
			if (prom_is_compatible(parent, "amlogic, meson-i2c") && prom_is_ok(parent)) {
				i2c_poweron(parent);
				uint64_t base;
				if (prom_get_reg(parent, 0, &base) == 0) {
					if (i2c_probe(parent, base, slave)) {
						rtc_node = node;
						rtc_address = slave;
						i2c_node = parent;
						i2c_base = base;
					}
				}
			}
		}
	}
}

static void
todmeson_set(timestruc_t ts)
{
	todinfo_t tod = utc_to_tod(ts.tv_sec - ggmtl());

	ASSERT(MUTEX_HELD(&tod_lock));

	if (rtc_node < 0)
		init_rtc();

	if (rtc_node < 0)
		goto err_exit;

	if (!i2c_prepare(i2c_node, i2c_base, rtc_address))
		goto err_exit;

	uint8_t buf[7] = {0};
	buf[0]	= BYTE_TO_BCD(tod.tod_sec);
	buf[1]	= BYTE_TO_BCD(tod.tod_min);
	buf[2]	= BYTE_TO_BCD(tod.tod_hour);
	buf[3]	= BYTE_TO_BCD(tod.tod_day);
	buf[4]	= (tod.tod_dow & 0x7) + 1;
	buf[5]	= BYTE_TO_BCD(tod.tod_month);
	buf[6]	= BYTE_TO_BCD(tod.tod_year % 100);
	if (tod.tod_year >= 100)
		buf[5] |= 0x80;

	for (int i = 0; i < 7; i++) {
		uint8_t data[2] = { i + 2, buf[i]};
		if (!i2c_write(i2c_base, rtc_address, data, 2))
			goto err_exit;
	}
err_exit:
	return;
}

static timestruc_t
todmeson_get(void)
{
	timestruc_t ts;
	todinfo_t tod;

	ASSERT(MUTEX_HELD(&tod_lock));

	if (rtc_node < 0)
		init_rtc();

	if (rtc_node < 0)
		goto err_exit;

	if (!i2c_prepare(i2c_node, i2c_base, rtc_address))
		goto err_exit;

	uint8_t buf[9] = {0};
	if (!i2c_write(i2c_base, rtc_address, buf, 1) || !i2c_read(i2c_base, rtc_address, buf, 9)) {
		goto err_exit;
	}

	tod.tod_sec	= BCD_TO_BYTE(buf[2] & 0x7F);
	tod.tod_min	= BCD_TO_BYTE(buf[3] & 0x7F);
	tod.tod_hour	= BCD_TO_BYTE(buf[4] & 0x3F);
	tod.tod_day	= BCD_TO_BYTE(buf[5] & 0x3F);
	tod.tod_dow	= (buf[6] & 0x7) + 1;
	tod.tod_month	= BCD_TO_BYTE(buf[7] & 0x1F);
	tod.tod_year	= BCD_TO_BYTE(buf[8]);
	if (tod.tod_year < 70) {
		tod.tod_year += 100;
	}

	tod_status_clear(TOD_GET_FAILED);

	ts.tv_sec = tod_to_utc(tod) + ggmtl();
	ts.tv_nsec = 0;

	return ts;

err_exit:
	ts.tv_sec = 0;
	ts.tv_nsec = 0;
	tod_status_set(TOD_GET_FAILED);
	return (ts);
}

static struct modlmisc modlmisc = {
	&mod_miscops, "todmeson"
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modlmisc, NULL
};

int
_init(void)
{
	extern tod_ops_t tod_ops;
	if (strcmp(tod_module_name, "todmeson") == 0) {
		tod_ops.tod_get = todmeson_get;
		tod_ops.tod_set = todmeson_set;
		tod_ops.tod_set_watchdog_timer = NULL;
		tod_ops.tod_clear_watchdog_timer = NULL;
		tod_ops.tod_set_power_alarm = NULL;
		tod_ops.tod_clear_power_alarm = NULL;
	}

	return mod_install(&modlinkage);
}

int
_fini(void)
{
	return (EBUSY);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}
