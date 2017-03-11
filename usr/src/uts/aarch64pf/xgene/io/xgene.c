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
#include <sys/platform.h>
#include <sys/modctl.h>
#include <sys/errno.h>
#include <sys/bootsvcs.h>
#include <sys/controlregs.h>

#define UART_ADDR	(UART_PHYS + SEGKPM_BASE)
#define UART_RBR	(*(volatile uint32_t *)(UART_ADDR + 0x00))
#define UART_THR	(*(volatile uint32_t *)(UART_ADDR + 0x00))
#define UART_IER	(*(volatile uint32_t *)(UART_ADDR + 0x04))
#define UART_IIR	(*(volatile uint32_t *)(UART_ADDR + 0x08))
#define UART_FCR	(*(volatile uint32_t *)(UART_ADDR + 0x08))
#define UART_LCR	(*(volatile uint32_t *)(UART_ADDR + 0x0c))
#define UART_MCR	(*(volatile uint32_t *)(UART_ADDR + 0x10))
#define UART_LSR	(*(volatile uint32_t *)(UART_ADDR + 0x14))
#define UART_MSR	(*(volatile uint32_t *)(UART_ADDR + 0x18))
#define UART_SCR	(*(volatile uint32_t *)(UART_ADDR + 0x1C))
#define UART_DLL	(*(volatile uint32_t *)(UART_ADDR + 0x00))
#define UART_DLM	(*(volatile uint32_t *)(UART_ADDR + 0x04))

#define UART_LSR_THRE	0x20
#define UART_LSR_DR	0x01


static void yield()
{
	asm volatile ("yield":::"memory");
}

static int _getchar()
{
	while (!(UART_LSR & UART_LSR_DR)) yield();
	return UART_RBR;
}

static void _putchar(int c)
{
	while (!(UART_LSR & UART_LSR_THRE)) yield();
	UART_THR = c;
	if (c == '\n')
		_putchar('\r');
	while (!(UART_LSR & UART_LSR_THRE)) yield();
}

static int _ischar()
{
	return (!!(UART_LSR & UART_LSR_DR));
}

static void _reset() __NORETURN;
static void _reset()
{
	_putchar('r');
	_putchar('e');
	_putchar('s');
	_putchar('e');
	_putchar('t');
	_putchar('\r');
	_putchar('\n');
	*(volatile uint32_t *)(SEGKPM_BASE + RESET_PHYS) = 1;
	for (;;) {}
}

static struct boot_syscalls _sysp =
{
	.bsvc_getchar = _getchar,
	.bsvc_putchar = _putchar,
	.bsvc_ischar = _ischar,
	.bsvc_reset = _reset,
};



void set_platform_defaults(void)
{
	tod_module_name = "todxgene";
	sysp = &_sysp;
}

uint64_t plat_get_cpu_clock(int cpu_no)
{
	int domain = cpu_no >> 1;

	uintptr_t addr = 0x7e200200 + (domain << 4) + SEGKPM_BASE;
	uint32_t pxccr = *(volatile uint32_t *)(0x7e200200 + (domain << 4) + SEGKPM_BASE);
	uint32_t div = ((pxccr >> 12) & 3) + 1;
	uint32_t pcppll = *(volatile uint32_t *)(0x17000100 + SEGKPM_BASE);
	uint64_t vco = 100000000ul * ((pcppll & 0x1ff) + 4);
	return (vco / 2) / div;
}

char *plat_get_cpu_str()
{
	uint32_t rev = read_midr() & 0xF;
	switch (rev) {
	case 0:
		rev = read_revidr() & 0x7;
		switch (rev) {
		case 0:
			return "APM X-Gene Rev A0";
		case 1:
			return "APM X-Gene Rev A0";
		case 2:
			rev = ((*(volatile uint32_t *)(0x1054a000 + SEGKPM_BASE)) >> 28) & 0xF;
			switch (rev) {
			case 1:
				return "APM X-Gene Rev A2";
			case 2:
				return "APM X-Gene Rev A3";
			default:
				return "Unknown CPU";
			}
		default:
			return "Unknown CPU";
		}
	case 1:
		return "APM X-Gene Rev B0";
	default:
		return "Unknown CPU";
	}
}


static struct modlmisc modlmisc = {
	&mod_miscops, "platmod"
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modlmisc, NULL
};

extern void xgene_init_clock(void);

int
_init(void)
{
	xgene_init_clock();
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
