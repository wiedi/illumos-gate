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
#include <sys/promif.h>

extern char *plat_get_cpu_str(void);
extern uint64_t plat_get_cpu_clock(int cpu_no);
extern int plat_clk_enable(const char *name);
extern int plat_clk_disable(const char *name);
extern int plat_clk_set_rate(const char *name, int rate);
extern int plat_clk_get_rate(const char *name);
extern int plat_pinmux_set(pnode_t);

struct gpio_ctrl;
extern int plat_gpio_direction_output(struct gpio_ctrl *, int);
extern int plat_gpio_direction_input(struct gpio_ctrl *);
extern int plat_gpio_get(struct gpio_ctrl *);
extern int plat_gpio_set(struct gpio_ctrl *, int);
extern int plat_gpio_set_pullup(struct gpio_ctrl *, int);

