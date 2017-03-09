/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
#include <sys/promif.h>
#include <sys/promimpl.h>
#include <sys/systm.h>
#include <sys/salib.h>
#include <sys/bootvfs.h>
#include <util/getoptstr.h>
#include "boot_plat.h"
#include <libfdt.h>
#include <sys/platnames.h>
#include <stdbool.h>

void
prom_node_init(void)
{
	int err;
	extern char _dtb_start[];

	err = fdt_check_header(_dtb_start);
	if (err) {
		prom_printf("fdt_check_header ng\n");
		return;
	}

	size_t total_size = fdt_totalsize(_dtb_start);
	size_t size = ((total_size + MMU_PAGESIZE - 1) & ~(MMU_PAGESIZE - 1));
	size += MMU_PAGESIZE;
	void *fdtp = (void *)memlist_get(size, MMU_PAGESIZE, &pfreelistp);
	memcpy(fdtp, _dtb_start, total_size);
	fdt_open_into(fdtp, fdtp, size);
	set_fdtp(fdtp);

	phandle_t chosen = prom_chosennode();
	if (chosen == OBP_NONODE) {
		prom_add_subnode(prom_rootnode(), "chosen");
	}
	chosen = prom_chosennode();

	char *str;
	static char buf[OBP_MAXPATHLEN];
	int len = prom_getproplen(chosen, "bootargs");
	if (0 < len && len < sizeof(buf)) {
		prom_getprop(chosen, "bootargs", buf);
		struct gos_params params;
		params.gos_opts = "D:";
		params.gos_strp = buf;
		getoptstr_init(&params);
		int c;
		while ((c = getoptstr(&params)) != -1) {
			switch (c) {
			case 'D':
				buf[params.gos_optargp - buf + params.gos_optarglen] = 0;
				str = &buf[params.gos_optargp - buf];
				prom_setprop(chosen, "__bootpath", (caddr_t)str, strlen(str) + 1);
				break;
			default:
				break;
			}
		}
	}
	if (prom_getproplen(chosen, "__bootpath") == 0) {
		str = get_default_bootpath();
		prom_setprop(chosen, "__bootpath", (caddr_t)str, strlen(str) + 1);
	}
}

