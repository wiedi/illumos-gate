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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */


/*
 * HAT interfaces used by the kernel debugger to interact with the VM system.
 * These interfaces are invoked when the world is stopped.  As such, no blocking
 * operations may be performed.
 */

#include <sys/cpuvar.h>
#include <sys/kdi_impl.h>
#include <sys/errno.h>
#include <sys/systm.h>
#include <sys/sysmacros.h>
#include <sys/mman.h>
#include <sys/bootconf.h>
#include <sys/cmn_err.h>
#include <vm/seg_kmem.h>
#include <vm/hat_alpha.h>
#include <sys/bootinfo.h>
#include <vm/kboot_mmu.h>
#include <sys/machsystm.h>

/*
 * Get the address for remapping physical pages during boot
 */
void
hat_boot_kdi_init(void)
{
}

/*
 * Switch to using a page in the kernel's va range for physical memory access.
 * We need to allocate a virtual page, then permanently map in the page that
 * contains the PTE to it.
 */
void
hat_kdi_init(void)
{
}

/*ARGSUSED*/
int
kdi_vtop(uintptr_t va, uint64_t *pap)
{
	uintptr_t vaddr = va;
	size_t	len;
	pfn_t	pfn;
	uint_t	prot;
	int	level;
	pte_t pte;
	int	index;

	if (SEGKPM_BASE <= va &&
	    va < (SEGKPM_BASE + SEGKPM_SIZE)) {
		*pap = va & MMU_SEGMASK;
	} else {
		/*
		 * if the mmu struct isn't relevant yet, we need to probe
		 * the boot loader's pagetables.
		 */
		if (kbm_probe(&vaddr, &len, &pfn, &prot) == 0)
			return (ENOENT);
		if (vaddr > va)
			return (ENOENT);
		if (vaddr < va)
			pfn += mmu_btop(va - vaddr);
		*pap = pfn_to_pa(pfn) + (vaddr & MMU_PAGEOFFSET);
	}
	return (0);
}

int
kdi_pread(caddr_t buf, size_t nbytes, uint64_t addr, size_t *ncopiedp)
{
	caddr_t va = (caddr_t)(addr + SEGKPM_BASE);
	bcopy(va, buf, nbytes);
	*ncopiedp = nbytes;
	return (0);
}

int
kdi_pwrite(caddr_t buf, size_t nbytes, uint64_t addr, size_t *ncopiedp)
{
	caddr_t va = (caddr_t)(addr + SEGKPM_BASE);
	bcopy(buf, va, nbytes);
	*ncopiedp = nbytes;
	return (0);
}


/*
 * Return the number of bytes, relative to the beginning of a given range, that
 * are non-toxic (can be read from and written to with relative impunity).
 */
/*ARGSUSED*/
size_t
kdi_range_is_nontoxic(uintptr_t va, size_t sz, int write)
{
	extern uintptr_t toxic_addr;
	extern size_t	toxic_size;

	/*
	 * Check 64 bit toxic range.
	 */
	if (toxic_addr != 0 &&
	    va + sz >= toxic_addr &&
	    va < toxic_addr + toxic_size)
		return (va < toxic_addr ? toxic_addr - va : 0);

	/*
	 * avoid any Virtual Address hole
	 */
	if (va + sz >= hole_start && va < hole_end)
		return (va < hole_start ? hole_start - va : 0);

	return (sz);
}
