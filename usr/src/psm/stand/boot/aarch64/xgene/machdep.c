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
#include <sys/types.h>
#include <sys/param.h>
#include <sys/promif.h>
#include <sys/salib.h>
#include <sys/bootconf.h>
#include <sys/boot.h>
#include <sys/bootinfo.h>
#include <sys/sysmacros.h>
#include <sys/machparam.h>
#include <sys/memlist.h>
#include <sys/memlist_impl.h>
#include <sys/controlregs.h>
#include <libfdt.h>
#include <sys/saio.h>
#include <sys/bootsyms.h>
#include <sys/fcntl.h>
#include <sys/platform.h>
#include "ramdisk.h"
#include "sata.h"
#include "enet.h"
#include "boot_plat.h"
#include <sys/platnames.h>
#include <alloca.h>
#include <netinet/inetutil.h>
#include <sys/bootvfs.h>

extern char _BootScratch[];
extern char _RamdiskStart[];
extern char _BootStart[];
extern char _BootEnd[];
extern char _RamdiskStart[];
extern char _RamdiskEnd[];
extern char filename[];
static struct xboot_info xboot_info;
static char zfs_bootfs[256];
char v2args_buf[V2ARGS_BUF_SZ];
char *v2args = v2args_buf;
extern char *bootp_response;
extern uint64_t boot_args[];

extern	int (*readfile(int fd, int print))();
extern	void kmem_init(void);
extern	void *kmem_alloc(size_t, int);
extern	void kmem_free(void *, size_t);
extern	void get_boot_args(char *buf);
extern	void setup_bootops(void);
extern	struct	bootops bootops;
extern	void exitto(int (*entrypoint)());
extern	int openfile(char *filename);
extern int determine_fstype_and_mountroot(char *);
extern	ssize_t xread(int, char *, size_t);
extern	void _reset(void);

static struct memlist *plinearlistp;

void
setup_aux(void)
{
}

void
init_physmem(void)
{
	int err;
	extern char _dtb_start[];
	void *fdtp = (void *)boot_args[0];
	err = fdt_check_header(fdtp);
	if (err) {
		prom_printf("fdt_check_header ng\n");
		return;
	}
	size_t total_size = fdt_totalsize(fdtp);
	if ((uintptr_t)_dtb_start != (uintptr_t)fdtp)
		memcpy(_dtb_start, fdtp, total_size);

	int address_cells = fdt_address_cells(_dtb_start, 0);
	int size_cells = fdt_size_cells(_dtb_start, 0);

	int nodeoffset = fdt_subnode_offset(_dtb_start, 0, "memory");
	if (nodeoffset < 0) {
		prom_printf("fdt memory not found\n");
		return;
	}
	if (!(address_cells == 2 || address_cells == 1)) {
		prom_printf("fdt invalid address_cells %d\n", address_cells);
		return;
	}
	if (!(size_cells == 2 || size_cells == 1)) {
		prom_printf("fdt invalid size_cells %d\n", size_cells);
		return;
	}

	int len;
	const volatile uint32_t *reg = fdt_getprop(_dtb_start, nodeoffset, "reg", &len);
	for (int i = 0; i < len / (sizeof(uint32_t) * (address_cells + size_cells)); i++) {
		uint64_t addr = 0;
		uint64_t size = 0;
		if (address_cells == 2) {
			addr = ((uint64_t)(ntohl(*reg)) << 32) | ntohl(*(reg + 1));
			reg += 2;
		} else {
			addr = ntohl(*reg);
			reg += 1;
		}
		if (size_cells == 2) {
			size = ((uint64_t)(ntohl(*reg)) << 32) | ntohl(*(reg + 1));
			reg += 2;
		} else {
			size = ntohl(*reg);
			reg += 1;
		}
		if (size != 0) {
			prom_printf("phys memory add %016lx - %016lx\n", addr, addr + size - 1);
			memlist_add_span(addr, size, &pinstalledp);
			memlist_add_span(addr, size, &plinearlistp);
			memlist_add_span(addr, size, &pfreelistp);
		}
	}
	for (int i = 0;; i++) {
		uint64_t addr;
		uint64_t size;
		fdt_get_mem_rsv(_dtb_start, i, &addr, &size);
		if (size == 0)
			break;
		prom_printf("memory resv %016lx - %016lx\n", addr, addr + size - 1);
		memlist_delete_span(addr, size, &pfreelistp);
	}

	memlist_delete_span((uintptr_t)_BootStart, (uintptr_t)_BootEnd - (uintptr_t)_BootStart, &pfreelistp);
	memlist_add_span   ((uintptr_t)_BootStart, (uintptr_t)_BootEnd - (uintptr_t)_BootStart, &pscratchlistp);

	memlist_add_span(PERIPHERAL0_PHYS, PERIPHERAL0_SIZE, &piolistp);
	memlist_add_span(PERIPHERAL1_PHYS, PERIPHERAL1_SIZE, &piolistp);
}


void exitto(int (*entrypoint)())
{
	for (struct memlist *ml = plinearlistp; ml != NULL; ml = ml->ml_next) {
		uintptr_t pa = ml->ml_address;
		uintptr_t sz = ml->ml_size;
		map_phys(PTE_XN | PTE_AF | PTE_SH_INNER | PTE_AP_KRWUNA | PTE_ATTR_NORMEM, (caddr_t)(SEGKPM_BASE + pa), pa, sz);
	}
	for (struct memlist *ml = piolistp; ml != NULL; ml = ml->ml_next) {
		uintptr_t pa = ml->ml_address;
		uintptr_t sz = ml->ml_size;
		map_phys(PTE_XN | PTE_AF | PTE_SH_INNER | PTE_AP_KRWUNA | PTE_ATTR_DEVICE, (caddr_t)(SEGKPM_BASE + pa), pa, sz);
	}

	phandle_t chosen = prom_chosennode();

	uint64_t v;
	const char *str;

	v = htonll((uint64_t)_RamdiskStart);
	prom_setprop(chosen, "ramdisk_start", (caddr_t)&v, sizeof(v));
	v = htonll((uint64_t)_RamdiskEnd);
	prom_setprop(chosen, "ramdisk_end", (caddr_t)&v, sizeof(v));
	v = htonll((uint64_t)pfreelistp);
	prom_setprop(chosen, "phys-avail", (caddr_t)&v, sizeof(v));
	v = htonll((uint64_t)pinstalledp);
	prom_setprop(chosen, "phys-installed", (caddr_t)&v, sizeof(v));
	v = htonll((uint64_t)pscratchlistp);
	prom_setprop(chosen, "boot-scratch", (caddr_t)&v, sizeof(v));
	if (bootp_response) {
		uint_t blen = strlen(bootp_response) / 2;
		char *pktbuf = alloca(blen);
		hexascii_to_octet(bootp_response, blen * 2, pktbuf, &blen);
		prom_setprop(chosen, "bootp-response", pktbuf, blen);
	} else {
	}
	str = "";
	prom_setprop(chosen, "boot-args", (caddr_t)str, strlen(str) + 1);
	str = "";
	prom_setprop(chosen, "bootargs", (caddr_t)str, strlen(str) + 1);
	str = filename;
	prom_setprop(chosen, "whoami", (caddr_t)str, strlen(str) + 1);
	str = filename;
	prom_setprop(chosen, "boot-file", (caddr_t)str, strlen(str) + 1);

	if (prom_getproplen(chosen, "impl-arch-name") < 0) {
		str = "aarch64";
		prom_setprop(chosen, "impl-arch-name", (caddr_t)str, strlen(str) + 1);
	}

	str = get_mfg_name();
	prom_setprop(chosen, "mfg-name", (caddr_t)str, strlen(str) + 1);
	str = "115200,8,n,1,-";
	prom_setprop(chosen, "ttya-mode", (caddr_t)str, strlen(str) + 1);
	prom_setprop(chosen, "ttyb-mode", (caddr_t)str, strlen(str) + 1);

	xboot_info.bi_fdt = SEGKPM_BASE + (uint64_t)get_fdtp();
	entrypoint(&xboot_info);
}

extern void get_boot_zpool(char *);
static void 
set_zfs_bootfs(void)
{
	get_boot_zpool(zfs_bootfs);
	phandle_t chosen = prom_chosennode();
	prom_setprop(chosen, "zfs-bootfs", (caddr_t)zfs_bootfs, strlen(zfs_bootfs) + 1);
}

static void
set_rootfs(char *bpath, char *fstype)
{
	char *str;
	phandle_t chosen = prom_chosennode();
	prom_printf("bpath=%s\n", bpath);
	if (strcmp(fstype, "nfs") == 0) {
#if 1
		str = "nfsdyn";
		prom_setprop(chosen, "fstype", (caddr_t)str, strlen(str) + 1);
#else
		str = "zfs";
		prom_setprop(chosen, "fstype", (caddr_t)str, strlen(str) + 1);
		str = "/soc/sata@1a400000/disk@0,0:a";
		prom_setprop(chosen, "bootpath", (caddr_t)str, strlen(str) + 1);
		str = "rpool/ROOT/illumos";
		prom_setprop(chosen, "zfs-bootfs", (caddr_t)str, strlen(str) + 1);
#endif
	} else {
		str = fstype;
		prom_setprop(chosen, "fstype", (caddr_t)str, strlen(str) + 1);
		str = bpath;
		prom_setprop(chosen, "bootpath", (caddr_t)str, strlen(str) + 1);
	}
}

void
load_ramdisk(void *virt, const char *name)
{
	static char	tmpname[MAXPATHLEN];

	if (determine_fstype_and_mountroot(prom_bootpath()) == VFS_SUCCESS) {
		set_rootfs(prom_bootpath(), get_default_fs()->fsw_name);
		if (strcmp(get_default_fs()->fsw_name, "zfs") == 0)
			set_zfs_bootfs();

		strcpy(tmpname, name);
		int fd = openfile(tmpname);
		if (fd >= 0) {
			struct stat st;
			if (fstat(fd, &st) == 0) {
				xread(fd, (char *)virt, st.st_size);
			}
		} else {
			prom_printf("open failed %s\n", tmpname);
			prom_reset();
		}
		closeall(1);
		unmountroot();
	} else {
		prom_printf("mountroot failed\n");
		prom_reset();
	}
}

#define	MAXNMLEN	80		/* # of chars in an impl-arch name */

/*
 * Return the manufacturer name for this platform.
 *
 * This is exported (solely) as the rootnode name property in
 * the kernel's devinfo tree via the 'mfg-name' boot property.
 * So it's only used by boot, not the boot blocks.
 */
char *
get_mfg_name(void)
{
	pnode_t n;
	int len;

	static char mfgname[MAXNMLEN];

	if ((n = prom_rootnode()) != OBP_NONODE &&
	    (len = prom_getproplen(n, "mfg-name")) > 0 && len < MAXNMLEN) {
		(void) prom_getprop(n, "mfg-name", mfgname);
		mfgname[len] = '\0'; /* broken clones don't terminate name */
		prom_printf("mfg_name=%s\n", mfgname);
		return (mfgname);
	}

	return ("Unknown");
}

char *
get_default_bootpath(void)
{
#if 1
	static char def_bootpath[] = "/soc/ethernet@17020000";
#else
	static char def_bootpath[] = "/soc/sata@1a400000/disk@0,0:a";
#endif
	return def_bootpath;
}

void _reset(void)
{
	prom_printf("%s:%d\n",__func__,__LINE__);
	*(volatile uint32_t *)RESET_PHYS = 1;
	for (;;) {}
}

void init_machdev(void)
{
	init_menet();
	init_sata();
}
