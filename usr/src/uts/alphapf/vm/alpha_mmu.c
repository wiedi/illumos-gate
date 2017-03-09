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
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <sys/t_lock.h>
#include <sys/memlist.h>
#include <sys/cpuvar.h>
#include <sys/vmem.h>
#include <sys/mman.h>
#include <sys/vm.h>
#include <sys/kmem.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>
#include <sys/vm_machparam.h>
#include <sys/vnode.h>
#include <vm/hat.h>
#include <vm/anon.h>
#include <vm/as.h>
#include <vm/page.h>
#include <vm/seg.h>
#include <vm/seg_kmem.h>
#include <vm/seg_map.h>
#include <vm/hat_alpha.h>
#include <vm/hat_pte.h>
#include <sys/promif.h>
#include <sys/systm.h>
#include <sys/archsystm.h>
#include <sys/sunddi.h>
#include <sys/ddidmareq.h>
#include <sys/controlregs.h>
#include <sys/reboot.h>
#include <sys/kdi.h>
#include <sys/bootconf.h>
#include <vm/kboot_mmu.h>
#include <sys/hwrpb.h>
#include <sys/machsystm.h>

/*
 * Flag is not set early in boot. Once it is set we are no longer
 * using boot's page tables.
 */
uint_t khat_running = 0;

static size_t ptbl_count(void);
static pte_t *get_l1ptbl(void);

/*
 * Initialize a special area in the kernel that always holds some PTEs for
 * faster performance. This always holds segmap's PTEs.
 * In the 32 bit kernel this maps the kernel heap too.
 */
void
hat_kmap_init(uintptr_t base, size_t len)
{
	uintptr_t map_addr;	/* base rounded down to large page size */
	uintptr_t map_eaddr;	/* base + len rounded up */

	map_addr = base & LEVEL_MASK(PAGE_LEVEL + 1);
	map_eaddr = (base + len + LEVEL_SIZE(PAGE_LEVEL + 1) - 1) & LEVEL_MASK(PAGE_LEVEL + 1);

	mmu.kmap_htables = kmem_alloc(((map_eaddr - map_addr) >> LEVEL_SHIFT(PAGE_LEVEL + 1)) * sizeof (htable_t *), KM_SLEEP);
	ulong_t i = 0;
	for (uintptr_t va = map_addr; va < map_eaddr; va += LEVEL_SIZE(PAGE_LEVEL + 1)) {
		htable_t *ht = htable_create(kas.a_hat, va, PAGE_LEVEL, NULL);
		mmu.kmap_htables[i++] = ht;
	}

	mmu.kmap_addr = map_addr;
	mmu.kmap_eaddr = map_eaddr;
}

extern caddr_t	kpm_vbase;
extern size_t	kpm_size;

/*
 * Routine to pre-allocate data structures for hat_kern_setup(). It computes
 * how many pagetables it needs by walking the boot loader's page tables.
 */
/*ARGSUSED*/
void
hat_kern_alloc(
	caddr_t	segmap_base,
	size_t	segmap_size,
	caddr_t	ekernelheap)
{
	uint_t table_cnt = 1;
	uint_t mapping_cnt;

	/*
	 * Add 1/4 more into table_cnt for extra slop.  The unused
	 * slop is freed back when we htable_adjust_reserve() later.
	 */
	table_cnt += ptbl_count();
	table_cnt += table_cnt >> 2;

	/*
	 * We only need mapping entries (hments) for shared pages.
	 * This should be far, far fewer than the total possible,
	 * We'll allocate enough for 1/16 of all possible PTEs.
	 */
	mapping_cnt = (table_cnt * NPTEPERPT) >> 4;

	/*
	 * Now create the initial htable/hment reserves
	 */
	htable_initial_reserve(table_cnt);
	hment_reserve(mapping_cnt);
}


/*
 * This routine handles the work of creating the kernel's initial mappings
 * by deciphering the mappings in the page tables created by the boot program.
 *
 * We maintain large page mappings, but only to a level 1 pagesize.
 * The boot loader can only add new mappings once this function starts.
 * In particular it can not change the pagesize used for any existing
 * mappings or this code breaks!
 */

void
hat_kern_setup(void)
{
	pte_t *l1_ptbl = get_l1ptbl();
	pfn_t pfn = mmu_btop((uintptr_t)l1_ptbl & ~KSEG_BASE);

	/*
	 * Attach htables to the existing pagetables
	 */
	htable_attach(kas.a_hat, 0, mmu.max_level, NULL, pfn);

	/*
	 * The kernel HAT is now officially open for business.
	 */
	khat_running = 1;

	CPU->cpu_current_hat = kas.a_hat;
}

/*
 * This routine is like page_numtopp, but accepts only free pages, which
 * it allocates (unfrees) and returns with the exclusive lock held.
 * It is used by machdep.c/dma_init() to find contiguous free pages.
 *
 * XXX this and some others should probably be in vm_machdep.c
 */
page_t *
page_numtopp_alloc(pfn_t pfnum)
{
	page_t *pp;

retry:
	pp = page_numtopp_nolock(pfnum);
	if (pp == NULL) {
		return (NULL);
	}

	if (!page_trylock(pp, SE_EXCL)) {
		return (NULL);
	}

	if (page_pptonum(pp) != pfnum) {
		page_unlock(pp);
		goto retry;
	}

	if (!PP_ISFREE(pp)) {
		page_unlock(pp);
		return (NULL);
	}
	if (pp->p_szc) {
		page_demote_free_pages(pp);
		page_unlock(pp);
		goto retry;
	}

	/* If associated with a vnode, destroy mappings */

	if (pp->p_vnode) {

		page_destroy_free(pp);

		if (!page_lock(pp, SE_EXCL, (kmutex_t *)NULL, P_NO_RECLAIM)) {
			return (NULL);
		}

		if (page_pptonum(pp) != pfnum) {
			page_unlock(pp);
			goto retry;
		}
	}

	if (!PP_ISFREE(pp)) {
		page_unlock(pp);
		return (NULL);
	}

	if (!page_reclaim(pp, (kmutex_t *)NULL))
		return (NULL);

	return (pp);
}

static uint_t
pte_entry(uintptr_t va, level_t l)
{
	return ((va >> LEVEL_SHIFT(l)) & ((1<<NPTESHIFT) - 1));
}

static pte_t *get_l1ptbl(void)
{
	pte_t *ptbl;
	uint64_t vptb_idx;

	vptb_idx = pte_entry(VPT_BASE, mmu.max_level);
	ptbl = (pte_t *)(VPT_BASE | (vptb_idx << PAGESHIFT) |
	    (vptb_idx << (PAGESHIFT + NPTESHIFT)) |
	    (vptb_idx << (PAGESHIFT + NPTESHIFT * 2)));
	return (pte_t *)(KSEG_BASE + mmu_ptob(PTE_TO_PFN(ptbl[vptb_idx])));
}
static pte_t *
find_pte(uintptr_t va, level_t level)
{
	pte_t *ptbl;
	pte_t *pte = 0;
	level_t l;

	ptbl = get_l1ptbl();
	for (l = mmu.max_level; l >= level; l--) {
		pte = &ptbl[pte_entry(va, l)];
		if (!PTE_ISVALID(*pte))
			break;
		ptbl = (pte_t *)(KSEG_BASE + mmu_ptob(PTE_TO_PFN(*pte)));
	}
	return pte;
}

int
kbm_probe(uintptr_t *va, size_t *len, pfn_t *pfn, uint_t *prot)
{
	uintptr_t	probe_va;
	pte_t	*ptep;
	paddr_t		pte_physaddr;
	pte_t	pte_val;
	level_t		l;

	if (khat_running)
		panic("kbm_probe() called too late");
	*len = 0;
	*pfn = PFN_INVALID;
	*prot = 0;
	probe_va = *va;
restart_new_va:
	l = mmu.max_level;
	for (;;) {
		if (IN_VA_HOLE(probe_va))
			probe_va = hole_end;

		/*
		 * If we don't have a valid PTP/PTE at this level
		 * then we can bump VA by this level's pagesize and try again.
		 * When the probe_va wraps around, we are done.
		 */
		pte_val = *find_pte(probe_va, l);
		if (!PTE_ISVALID(pte_val)) {
			probe_va = (probe_va & LEVEL_MASK(l)) + LEVEL_SIZE(l);
			if (probe_va <= *va)
				return (0);
			goto restart_new_va;
		}

		/*
		 * If this entry is a pointer to a lower level page table
		 * go down to it.
		 */
		if (l != PAGE_LEVEL) {
			l--;
			continue;
		}

		/*
		 * We found a boot level page table entry
		 */
		*len = LEVEL_SIZE(l);
		*va = probe_va & ~(*len - 1);
		*pfn = PTE2PFN(pte_val, l);

		*prot = PROT_READ;
		if (PTE_GET(pte_val, PTE_KWE))
			*prot |= PROT_WRITE;

		if (!PTE_GET(pte_val, PTE_FOE))
			*prot |= PROT_EXEC;

		return (1);
	}
}

/*
 * This procedure is callable only while the boot loader is in charge of the
 * MMU. It assumes that PA == VA for page table pointers.  It doesn't live in
 * kboot_mmu.c since it's used from common code.
 */
pfn_t
va_to_pfn(void *vaddr)
{
	pte_t *ptep;
	if (khat_running)
		panic("va_to_pfn(): called too late\n");
	ptep = find_pte((uintptr_t)vaddr, PAGE_LEVEL);
	if (ptep == NULL)
		return PFN_INVALID;
	if (!PTE_ISVALID(*ptep))
		return PFN_INVALID;
	return PTE_TO_PFN(*ptep);
}

void boot_reserve(void)
{
	uintptr_t probe_va = SEG1_BASE;
	size_t count = 0;
	pfn_t pfn;
	page_t *pp;
	pte_t *l1_ptbl = get_l1ptbl(), pte_val;
	level_t l;
	int i;

	pfn = mmu_btop((uintptr_t)l1_ptbl & ~KSEG_BASE);
	pp = page_numtopp_nolock(pfn);
	ASSERT(pp != NULL);
	ASSERT(PAGE_EXCL(pp));
	ASSERT(pp->p_lckcnt == 1);

	l = mmu.max_level;
	for (;;) {
		pte_val = *find_pte(probe_va, l);
		if (probe_va == VPT_BASE || !PTE_ISVALID(pte_val)) {
			probe_va = (probe_va & LEVEL_MASK(l)) + LEVEL_SIZE(l);
			if (probe_va == 0)
				break;
			if (l != mmu.max_level && (probe_va & LEVEL_OFFSET(l + 1)) == 0) {
				l++;
			}
			continue;
		}
		pfn = PTE_TO_PFN(pte_val);
		pp = page_numtopp_nolock(pfn);
		ASSERT(pp != NULL);
		ASSERT(pp->p_lckcnt == 1);
		ASSERT(PAGE_EXCL(pp));

		if (l != PAGE_LEVEL) {
			l--;
			continue;
		}

		/*
		 * consoleがエイリアスを作成するための回避。
		 */
		if (pp->p_vnode == NULL) {
			page_hashin(pp, &kvp, probe_va, NULL);
		}

		probe_va = (probe_va & LEVEL_MASK(l)) + LEVEL_SIZE(l);
		if (probe_va == 0)
			break;
		if ((probe_va & LEVEL_OFFSET(PAGE_LEVEL + 1)) == 0) {
			l++;
		}
		count++;
	}
	if (page_resv(count, KM_NOSLEEP) == 0)
		panic("boot_reserve: page_resv failed");
	/*
	 * Clear Boot Mapping
	 */
	for (i = 0; i < pte_entry(SEG1_BASE, mmu.max_level); i++) {
		l1_ptbl[i] = 0;
	}
	pal_tbia();
}

static size_t ptbl_count(void)
{
	uintptr_t probe_va = SEG1_BASE;
	size_t count = 1;
	level_t l;

	l = mmu.max_level;
	for (;;) {
		if (probe_va == VPT_BASE ||
		    !PTE_ISVALID(*find_pte(probe_va, l))) {
			probe_va = (probe_va & LEVEL_MASK(l)) + LEVEL_SIZE(l);
			if (probe_va == 0)
				break;
			if (l != mmu.max_level && (probe_va & LEVEL_OFFSET(l + 1)) == 0) {
				l++;
			}
		} else if (l == mmu.max_level) {
			count++;
			l--;
		} else {
			count++;
			probe_va = (probe_va & LEVEL_MASK(l)) + LEVEL_SIZE(l);
			if (probe_va == 0)
				break;
			if ((probe_va & LEVEL_OFFSET(l + 1)) == 0) {
				l++;
			}
		}
	}
	return count;
}
