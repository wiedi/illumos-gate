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

#define RTC_BASE	0x10510000
#define PHYS_OFFSET	SEGKPM_BASE

static void
todxgene_set(timestruc_t ts)
{
	uint32_t sec = ts.tv_sec - ggmtl();
	*(volatile uint32_t *)(RTC_BASE + PHYS_OFFSET + 0x8) = sec;
	asm volatile ("dsb sy":::"memory");
}

static timestruc_t
todxgene_get(void)
{
	uint32_t sec = *(volatile uint32_t *)(RTC_BASE + PHYS_OFFSET + 0);

	timestruc_t ts = { .tv_sec = sec + ggmtl(), .tv_nsec = 0};
	return ts;
}

static struct modlmisc modlmisc = {
	&mod_miscops, "todxgene"
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modlmisc, NULL
};

int
_init(void)
{
	extern tod_ops_t tod_ops;
	if (strcmp(tod_module_name, "todxgene") == 0) {
		tod_ops.tod_get = todxgene_get;
		tod_ops.tod_set = todxgene_set;
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
