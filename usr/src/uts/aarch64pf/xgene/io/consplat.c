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
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * isa-specific console configuration routines
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/cmn_err.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/debug.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/sunndi.h>
#include <sys/esunddi.h>
#include <sys/ddi_impldefs.h>
#include <sys/promif.h>
#include <sys/modctl.h>
#include <sys/termios.h>

int
plat_use_polled_debug()
{
	return (0);
}

int
plat_support_serial_kbd_and_ms()
{
	return (0);
}

int
plat_stdin_is_keyboard(void)
{
	return 0;
}

int
plat_stdout_is_framebuffer(void)
{
	return 0;
}

char *
plat_kbdpath(void)
{
	return (NULL);
}

char *
plat_fbpath(void)
{
	return (NULL);
}

char *
plat_mousepath(void)
{
	return (NULL);
}

static char *
plat_ttypath(void)
{
	int len;
	len = prom_getproplen(prom_chosennode(), "stdout-path");
	if (len > 0) {
		char *buf = __builtin_alloca(len + 1);
		prom_getprop(prom_chosennode(), "stdout-path", buf);
		buf[len] = '\0';
		char *p = strchr(buf, ':');
		if (p)
			*p = '\0';

		dev_info_t *dip;
		if (resolve_pathname(buf, &dip, NULL, NULL) == 0) {
			static char path[MAXPATHLEN];
			(void) ddi_pathname(dip, path);
			char *bp = path + strlen(path);
			(void) snprintf(bp, 3, ":%s", DEVI(dip)->devi_minor->ddm_name);
			return path;
		}
	}
	return NULL;
}

char *
plat_stdinpath(void)
{
	char *str = plat_ttypath();
	return str;
}

char *
plat_stdoutpath(void)
{
	char *str = plat_ttypath();
	return str;
}

void
plat_tem_get_inverses(int *inverse, int *inverse_screen)
{
	*inverse = 0;
	*inverse_screen = 0;
}

void
plat_tem_get_prom_font_size(int *charheight, int *windowtop)
{
	*charheight = 0;
	*windowtop = 0;
}

void
plat_tem_get_prom_size(size_t *height, size_t *width)
{
	panic("unimplemented at line %d of %s", __LINE__, __FILE__);
}

void
plat_tem_hide_prom_cursor(void)
{
	panic("unimplemented at line %d of %s", __LINE__, __FILE__);
}

void
plat_tem_get_prom_pos(uint32_t *row, uint32_t *col)
{
	panic("unimplemented at line %d of %s", __LINE__, __FILE__);
}
