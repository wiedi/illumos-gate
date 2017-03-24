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
#include <sys/machclock.h>
#include <sys/platmod.h>

int plat_pinmux_set(pnode_t node)
{ return -1; }
int plat_gpio_direction_output(struct gpio_ctrl *gpio, int v)
{ return -1; }
int plat_gpio_direction_input(struct gpio_ctrl *gpio)
{ return -1; }
int plat_gpio_get(struct gpio_ctrl *gpio)
{ return -1; }
int plat_gpio_set(struct gpio_ctrl *gpio, int v)
{ return -1; }
int plat_gpio_set_pullup(struct gpio_ctrl *gpio, int v)
{ return -1; }
