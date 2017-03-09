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

#include <sys/boot.h>
#include <sys/promif.h>
#include <sys/platform.h>
#include <sys/controlregs.h>
#include "prom_dev.h"
#include "boot_plat.h"


char *default_name = "aarch64";
char *default_path = "/platform/aarch64/kernel";
extern void exception_vector(void);

boolean_t
is_netdev(char *devpath)
{
	return prom_is_netdev(devpath);
}

void
fiximp(void)
{
	extern int use_align;

	use_align = 1;

	write_vbar((uint64_t)&exception_vector);

	if ((4u << ((read_ctr_el0() >> 16) & 0xF)) != DCACHE_LINE) {
		prom_printf("CTR_EL0=%08x DCACHE_LINE=%ld\n", (uint32_t)read_ctr_el0(), DCACHE_LINE);
		prom_reset();
	}

}

void dump_exception(uint64_t *regs)
{
	uint64_t pc;
	uint64_t esr;
	uint64_t far;
	asm volatile ("mrs %0, elr_el1":"=r"(pc));
	asm volatile ("mrs %0, esr_el1":"=r"(esr));
	asm volatile ("mrs %0, far_el1":"=r"(far));
	prom_printf("%s\n", __func__);
	prom_printf("pc  = %016lx\n",  pc);
	prom_printf("esr = %016lx\n",  esr);
	prom_printf("far = %016lx\n",  far);
	for (int i = 0; i < 31; i++)
		prom_printf("x%d%s = %016lx\n", i, ((i >= 10)?" ":""),regs[i]);
	prom_reset();
}

