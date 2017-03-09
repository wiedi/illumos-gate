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
 * Copyright (c) 2004, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * UNIX machine dependent virtual memory support.
 */

#ifndef	_VM_DEP_H
#define	_VM_DEP_H


#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/clock.h>
#include <vm/hat_pte.h>
#include <sys/param.h>
#include <sys/memnode.h>
#include <sys/atomic.h>

#define	GETTICK()	 ((uint32_t)__builtin_alpha_rpcc())

extern page_t ****page_freelists;
extern page_t ***page_cachelists;

extern int memrange_num(pfn_t);
extern int pfn_2_mtype(pfn_t);
extern int mtype_func(int, int, uint_t);
extern void mtype_modify_max(pfn_t, long);
extern int mnode_pgcnt(int);
extern int mnode_range_cnt(int);
extern page_t *page_get_mnode_freelist(int, uint_t, int, uchar_t, uint_t);
extern page_t *page_get_mnode_cachelist(uint_t, uint_t, int, int);

/*
 * page list count per mnode.
 */
typedef struct {
	/* maintain page list stats */
	pgcnt_t	plc_mt_pgmax;		/* mnode/mtype max page cnt */
	pgcnt_t	plc_mt_clpgcnt;		/* cache list cnt */
	pgcnt_t	plc_mt_flpgcnt;		/* free list cnt - small pages */
	pgcnt_t	plc_mt_lgpgcnt;		/* free list cnt - large pages */
#ifdef	DEBUG
	struct plc_mts {		/* mnode/mtype szc stats */
		pgcnt_t	plc_mts_pgcnt;	/* per page size count */
		int	plc_mts_colors;
		pgcnt_t *plc_mtsc_pgcnt;	/* per color bin count */
	} plc_mts[MMU_PAGE_SIZES];
#endif	/* DEBUG */
} plcnt_t;

#ifdef	DEBUG
#define	PLCNT_SZ(ctrs_sz)						\
	do {								\
		int __szc;						\
		for (__szc = 0; __szc < mmu_page_sizes; __szc++) {	\
			int __colors = page_get_pagecolors(__szc);	\
			ctrs_sz += sizeof(pgcnt_t) * __colors;		\
		}							\
	} while (0)


#define	PLCNT_INIT(addr)						\
	do {								\
		int __szc;						\
		for (__szc = 0; __szc < mmu_page_sizes; __szc++) {	\
			int __colors = page_get_pagecolors(__szc);	\
			plcnt.plc_mts[__szc].plc_mts_colors = __colors;	\
			plcnt.plc_mts[__szc].plc_mtsc_pgcnt =		\
				(pgcnt_t *)addr;			\
			addr += (sizeof(pgcnt_t) * __colors);		\
		}							\
	} while (0)

#define	PLCNT_DO(pp, mtype, szc, cnt, flags)				\
	do {								\
		int	__bin = PP_2_BIN(pp);				\
		if ((flags) & PG_CACHE_LIST) {				\
			atomic_add_long(&plcnt.plc_mt_clpgcnt, cnt);	\
		}							\
		else if (szc) {						\
			atomic_add_long(&plcnt.plc_mt_lgpgcnt, cnt);	\
		}							\
		else {							\
			atomic_add_long(&plcnt.plc_mt_flpgcnt, cnt);	\
		}							\
		atomic_add_long(&plcnt.plc_mts[szc].plc_mts_pgcnt, cnt); \
		atomic_add_long(&plcnt.plc_mts[szc].			\
				plc_mtsc_pgcnt[__bin], cnt);		\
	} while (0)

#else	/* !DEBUG */

#define	PLCNT_SZ(ctrs_sz)

#define	PLCNT_INIT(addr)

#define	PLCNT_DO(pp, mtype, szc, cnt, flags)				\
	do {								\
		if ((flags) & PG_CACHE_LIST) {				\
			atomic_add_long(&plcnt.plc_mt_clpgcnt, cnt);	\
		}							\
		else if (szc) {						\
			atomic_add_long(&plcnt.plc_mt_lgpgcnt, cnt);	\
		}							\
		else {							\
			atomic_add_long(&plcnt.plc_mt_flpgcnt, cnt);	\
		}							\
	} while (0)

#endif	/* DEBUG */

#define	PLCNT_INCR(pp, mnode, mtype, szc, flags)			\
	do {								\
		long	__cnt = (1 << PAGE_BSZS_SHIFT(szc));		\
		ASSERT(mtype == PP_2_MTYPE(pp));			\
		PLCNT_DO(pp, mtype, szc, __cnt, flags);			\
	} while (0)

#define	PLCNT_DECR(pp, mnode, mtype, szc, flags)			\
	do {								\
		long	__cnt = ((-1ul) << PAGE_BSZS_SHIFT(szc));	\
		ASSERT(mtype == PP_2_MTYPE(pp));			\
		PLCNT_DO(pp, mtype, szc, __cnt, flags);			\
	} while (0)


/*
 * macro to update page list max counts.  no-op on Alpha.
 */
#define	PLCNT_XFER_NORELOC(pp)

/*
 * Macro to modify the page list max counts when memory is added to
 * the page lists during startup (add_physmem).
 */
#define	PLCNT_MODIFY_MAX(pfn, cnt)			\
	atomic_add_long(&plcnt.plc_mt_pgmax, (cnt))

extern plcnt_t		plcnt;

/*
 * candidate counters in vm_pagelist.c are indexed by color and range
 */
#define	NUM_MEM_RANGES		3		/* memory range types */
#define	MAX_MNODE_MRANGES	NUM_MEM_RANGES
#define	MNODE_RANGE_CNT(mnode)	mnode_range_cnt(mnode)
#define	MNODE_MAX_MRANGE(mnode)	memrange_num(mem_node_config[mnode].physbase)
extern int mtype_2_mrange(int);
#define	MTYPE_2_MRANGE(mnode, mtype)	\
	(mnode_maxmrange[mnode] - mtype_2_mrange(mtype))

/*
 * this structure is used for walking free page lists
 * controls when to split large pages into smaller pages,
 * and when to coalesce smaller pages into larger pages
 */
typedef struct page_list_walker {
	uint_t	plw_colors;		/* num of colors for szc */
	uint_t  plw_color_mask;		/* colors-1 */
	uint_t	plw_bin_step;		/* next bin: 1 or 2 */
	uint_t  plw_count;		/* loop count */
	uint_t	plw_bin0;		/* starting bin */
	uint_t  plw_bin_marker;		/* bin after initial jump */
	uint_t  plw_bin_split_prev;	/* last bin we tried to split */
	uint_t  plw_do_split;		/* set if OK to split */
	uint_t  plw_split_next;		/* next bin to split */
	uint_t	plw_ceq_dif;		/* number of different color groups */
					/* to check */
	uint_t	plw_ceq_mask[MMU_PAGE_SIZES + 1]; /* color equiv mask */
	uint_t	plw_bins[MMU_PAGE_SIZES + 1];	/* num of bins */
} page_list_walker_t;

void	page_list_walk_init(uchar_t szc, uint_t flags, uint_t bin,
    int can_split, int use_ceq, page_list_walker_t *plw);
extern uint_t page_list_walk_next_bin(uchar_t szc, uint_t bin, page_list_walker_t *plw);


#define	PAGE_FREELISTS(mnode, szc, color, mtype)		\
	(*(page_freelists[mtype][szc] + (color)))

#define	PAGE_GET_FREELISTS(pp, vp, off, seg, vaddr, size, flags, lgrp)	\
	pp = ufltp->pflt_get_free((vp), (off), (seg), (vaddr), (size),	\
	    (flags), (lgrp))

#define	PAGE_CACHELISTS(mnode, color, mtype) 		\
	(*(page_cachelists[mtype] + (color)))

#define	PAGE_GET_CACHELISTS(pp, vp, off, seg, vaddr, flags, lgrp)	  \
	pp = ucltp->pflt_get_free((vp), (off), (seg), (vaddr), 0, (flags),\
	    (lgrp))

#define	PAGE_GET_ANY_FREELIST(mnode, color, mtype, szc, flags) 		   \
	page_get_mnode_freelist((ufltp), (mnode), (color), (mtype), (szc), \
	    (flags))

#define	PAGE_GET_ANY_CACHELIST(mnode, color, mtype, flags)		\
	page_get_mnode_cachelist((ucltp), (mnode), (color), (mtype), (flags))

#define	PAGE_GET_FREELISTS_POLICY(fp, i) 				\
	(fp->pflt_policy[i])

#define	PAGE_LIST_WALK_INIT(fp, szc, flags, bin, can_split, use_ceq, plw) \
	fp->pflt_walk_init((szc), (flags), (bin), (can_split), (use_ceq), \
	    (plw))

#define	PAGE_LIST_WALK_NEXT(fp, szc, bin, plw) 			        \
	fp->pflt_walk_next((szc), (bin), (plw))

/*
 * There are mutexes for both the page freelist
 * and the page cachelist.  We want enough locks to make contention
 * reasonable, but not too many -- otherwise page_freelist_lock() gets
 * so expensive that it becomes the bottleneck!
 */

#define	NPC_MUTEX	16

extern kmutex_t	*fpc_mutex[NPC_MUTEX];
extern kmutex_t	*cpc_mutex[NPC_MUTEX];

/* mem node iterator is not used on Alpha */
#define	MEM_NODE_ITERATOR_DECL(it)
#define	MEM_NODE_ITERATOR_INIT(pfn, mnode, szc, it)

/*
 * interleaved_mnodes mode is never set on Alpha, therefore,
 * simply return the limits of the given mnode, which then
 * determines the length of hpm_counters array for the mnode.
 */
#define	HPM_COUNTERS_LIMITS(mnode, physbase, physmax, first) 	\
	{							\
		(physbase) = mem_node_config[(mnode)].physbase;	\
		(physmax) = mem_node_config[(mnode)].physmax;	\
		(first) = (mnode);				\
	}

#define	PAGE_CTRS_WRITE_LOCK(mnode)				\
	{							\
		rw_enter(&page_ctrs_rwlock[(mnode)], RW_WRITER);\
		page_freelist_lock(mnode);			\
	}

#define	PAGE_CTRS_WRITE_UNLOCK(mnode)				\
	{							\
		page_freelist_unlock(mnode);			\
		rw_exit(&page_ctrs_rwlock[(mnode)]);		\
	}

/*
 * macro to call page_ctrs_adjust() when memory is added
 * during a DR operation.
 */
#define	PAGE_CTRS_ADJUST(pfn, cnt, rv) {				       \
	spgcnt_t _cnt = (spgcnt_t)(cnt);				       \
	int _mn;							       \
	pgcnt_t _np;							       \
	pfn_t _pfn = (pfn);						       \
	pfn_t _endpfn = _pfn + _cnt;					       \
	while (_pfn < _endpfn) {					       \
		_mn = PFN_2_MEM_NODE(_pfn);				       \
		_np = MIN(_endpfn, mem_node_config[_mn].physmax + 1) - _pfn;   \
		_pfn += _np;						       \
		if ((rv = page_ctrs_adjust(_mn)) != 0)			       \
			break;						       \
	}								       \
}

#define	PAGE_GET_COLOR_SHIFT(szc, nszc)				\
	    (hw_page_array[(nszc)].hp_shift - hw_page_array[(szc)].hp_shift)

#define	PAGE_CONVERT_COLOR(ncolor, szc, nszc)			\
	    ((ncolor) << PAGE_GET_COLOR_SHIFT((szc), (nszc)))

#define	PFN_2_COLOR(pfn, szc, it)					\
	(((pfn) & page_colors_mask) >>			                \
	(hw_page_array[szc].hp_shift - hw_page_array[0].hp_shift))

#define	PNUM_SIZE(szc)							\
	(hw_page_array[(szc)].hp_pgcnt)
#define	PNUM_SHIFT(szc)							\
	(hw_page_array[(szc)].hp_shift - hw_page_array[0].hp_shift)
#define	PAGE_GET_SHIFT(szc)						\
	(hw_page_array[(szc)].hp_shift)
#define	PAGE_GET_PAGECOLORS(szc)					\
	(hw_page_array[(szc)].hp_colors)

/*
 * This macro calculates the next sequential pfn with the specified
 * color using color equivalency mask
 */
#define	PAGE_NEXT_PFN_FOR_COLOR(pfn, szc, color, ceq_mask, color_mask, it)    \
	{								      \
		uint_t	pfn_shift = PAGE_BSZS_SHIFT(szc);                     \
		pfn_t	spfn = pfn >> pfn_shift;                              \
		pfn_t	stride = (ceq_mask) + 1;                              \
		ASSERT(((color) & ~(ceq_mask)) == 0);                         \
		ASSERT((((ceq_mask) + 1) & (ceq_mask)) == 0);                 \
		if (((spfn ^ (color)) & (ceq_mask)) == 0) {                   \
			pfn += stride << pfn_shift;                           \
		} else {                                                      \
			pfn = (spfn & ~(pfn_t)(ceq_mask)) | (color);          \
			pfn = (pfn > spfn ? pfn : pfn + stride) << pfn_shift; \
		}                                                             \
	}

/* get the color equivalency mask for the next szc */
#define	PAGE_GET_NSZ_MASK(szc, mask)                                         \
	((mask) >> (PAGE_GET_SHIFT((szc) + 1) - PAGE_GET_SHIFT(szc)))

/* get the color of the next szc */
#define	PAGE_GET_NSZ_COLOR(szc, color)                                       \
	((color) >> (PAGE_GET_SHIFT((szc) + 1) - PAGE_GET_SHIFT(szc)))

/* Find the bin for the given page if it was of size szc */
#define	PP_2_BIN_SZC(pp, szc)	(PFN_2_COLOR(pp->p_pagenum, szc, NULL))

#define	PP_2_BIN(pp)		(PP_2_BIN_SZC(pp, pp->p_szc))

#define	PP_2_MEM_NODE(pp)	(PFN_2_MEM_NODE(pp->p_pagenum))
#define	PP_2_MTYPE(pp)		(pfn_2_mtype(pp->p_pagenum))
#define	PP_2_SZC(pp)		(pp->p_szc)

#define	SZCPAGES(szc)		(1 << PAGE_BSZS_SHIFT(szc))
#define	PFN_BASE(pfnum, szc)	(pfnum & ~(SZCPAGES(szc) - 1))

extern struct cpu	cpus[];
#define	CPU0		cpus

#define	MTYPE_INIT(mtype, vp, vaddr, flags, pgsz)	\
	do {						\
		(mtype) = 0;				\
	} while (0)

/*
 * macros to loop through the mtype range (page_get_mnode_{free,cache,any}list,
 * and page_get_contig_pages)
 *
 * MTYPE_START sets the initial mtype. -1 if the mtype range specified does
 * not contain mnode.
 *
 * MTYPE_NEXT sets the next mtype. -1 if there are no more valid
 * mtype in the range.
 */
#define	MTYPE_START(mnode, mtype, flags)	\
	do {					\
		(mtype) = 0;			\
	} while (0)

#define	MTYPE_NEXT(mnode, mtype, flags)		\
	do {					\
		(mtype) = -1;			\
	} while (0)

/* mtype init for page_get_replacement_page */

#define	MTYPE_PGR_INIT(mtype, flags, pp, mnode, pgcnt)	\
	do {						\
		mtype = 0;				\
	} while (0)

#define	MNODE_PGCNT(mnode)						\
	(plcnt.plc_mt_clpgcnt + plcnt.plc_mt_flpgcnt + plcnt.plc_mt_lgpgcnt)

#define	MNODETYPE_2_PFN(mnode, mtype, pfnlo, pfnhi)			\
	do {								\
		ASSERT(mtype == 0);					\
		pfnlo = mem_node_config[0].physbase;			\
		pfnhi = mem_node_config[0].physmax;			\
	} while (0)

#define	PC_BIN_MUTEX(mnode, bin, flags) ((flags & PG_FREE_LIST) ?	\
	&fpc_mutex[(bin) & (NPC_MUTEX - 1)][mnode] :			\
	&cpc_mutex[(bin) & (NPC_MUTEX - 1)][mnode])

#define	FPC_MUTEX(mnode, i)	(&fpc_mutex[i][mnode])
#define	CPC_MUTEX(mnode, i)	(&cpc_mutex[i][mnode])

#define	CHK_LPG(pp, szc)

#define	FULL_REGION_CNT(rg_szc)	(1 << (MMU_PAGESHIFT64K - MMU_PAGESHIFT))

/* Return the leader for this mapping size */
#define	PP_GROUPLEADER(pp, szc) \
	(&(pp)[-(int)((pp)->p_pagenum & (SZCPAGES(szc)-1))])

/* Return the root page for this page based on p_szc */
#define	PP_PAGEROOT(pp) ((pp)->p_szc == 0 ? (pp) : \
	PP_GROUPLEADER((pp), (pp)->p_szc))

/*
 * The counter base must be per page_counter element to prevent
 * races when re-indexing, and the base page size element should
 * be aligned on a boundary of the given region size.
 *
 * We also round up the number of pages spanned by the counters
 * for a given region to PC_BASE_ALIGN in certain situations to simplify
 * the coding for some non-performance critical routines.
 */

#define	PC_BASE_ALIGN		((pfn_t)1 << PAGE_BSZS_SHIFT(MMU_PAGE_SIZES-1))
#define	PC_BASE_ALIGN_MASK	(PC_BASE_ALIGN - 1)

/*
 * cpu/mmu-dependent vm variables
 */
#define	mmu_page_sizes			MMU_PAGE_SIZES
#define	mmu_exported_page_sizes		MMU_PAGE_SIZES
#define	mmu_legacy_page_sizes		mmu_exported_page_sizes

/* For Alpha, userszc is the same as the kernel's szc */
#define	USERSZC_2_SZC(userszc)	(userszc)
#define	SZC_2_USERSZC(szc)	(szc)

/*
 * for hw_page_map_t, sized to hold the ratio of large page to base
 * pagesize (1024 max)
 */
typedef	short	hpmctr_t;

/*
 * get the setsize of the current cpu - assume homogenous for Alpha
 */
extern int	l2cache_sz, l2cache_linesz, l2cache_assoc;

#define	L2CACHE_ALIGN		l2cache_linesz
#define	L2CACHE_ALIGN_MAX	64
#define	CPUSETSIZE()		(l2cache_assoc ? (l2cache_sz / l2cache_assoc) : MMU_PAGESIZE)

/*
 * Return the log2(pagesize(szc) / MMU_PAGESIZE) --- or the shift count
 * for the number of base pages in this pagesize
 */
#define	PAGE_BSZS_SHIFT(szc) ((MMU_PAGESHIFT64K - MMU_PAGESHIFT) * (szc))

/*
 * Internal PG_ flags.
 */
#define	PGI_RELOCONLY	0x010000	/* opposite of PG_NORELOC */
#define	PGI_NOCAGE	0x020000	/* cage is disabled */
#define	PGI_PGCPHIPRI	0x040000	/* page_get_contig_page pri alloc */
#define	PGI_PGCPSZC0	0x080000	/* relocate base pagesize page */

/*
 * Maximum and default values for user heap, stack, private and shared
 * anonymous memory, and user text and initialized data.
 * Used by map_pgsz*() routines.
 */
extern size_t max_uheap_lpsize;
extern size_t default_uheap_lpsize;
extern size_t max_ustack_lpsize;
extern size_t default_ustack_lpsize;
extern size_t max_privmap_lpsize;
extern size_t max_uidata_lpsize;
extern size_t max_utext_lpsize;
extern size_t max_shm_lpsize;

/*
 * Sanity control. Don't use large pages regardless of user
 * settings if there's less than priv or shm_lpg_min_physmem memory installed.
 * The units for this variable are 8K pages.
 */
extern pgcnt_t privm_lpg_min_physmem;
extern pgcnt_t shm_lpg_min_physmem;

/*
 * hash as and addr to get a bin.
 */

#define	AS_2_BIN(as, seg, vp, addr, bin, szc)			    \
	bin = (((((uintptr_t)(addr) >> PAGESHIFT) + ((uintptr_t)(as) >> 4)) \
	    & page_colors_mask) >>					    \
	    (hw_page_array[szc].hp_shift - hw_page_array[0].hp_shift))

/*
 * cpu private vm data - accessed thru CPU->cpu_vm_data
 *	vc_pnum_memseg: tracks last memseg visited in page_numtopp_nolock()
 *	vc_pnext_memseg: tracks last memseg visited in page_nextn()
 *	vc_kmptr: orignal unaligned kmem pointer for this vm_cpu_data_t
 *	vc_kmsize: orignal kmem size for this vm_cpu_data_t
 */

typedef struct {
	struct memseg	*vc_pnum_memseg;
	struct memseg	*vc_pnext_memseg;
	void		*vc_kmptr;
	size_t		vc_kmsize;
} vm_cpu_data_t;

/* allocation size to ensure vm_cpu_data_t resides in its own cache line */
#define	VM_CPU_DATA_PADSIZE						\
	(P2ROUNDUP(sizeof (vm_cpu_data_t), L2CACHE_ALIGN_MAX))

/*
 * When a bin is empty, and we can't satisfy a color request correctly,
 * we scan.  If we assume that the programs have reasonable spatial
 * behavior, then it will not be a good idea to use the adjacent color.
 * Using the adjacent color would result in virtually adjacent addresses
 * mapping into the same spot in the cache.  So, if we stumble across
 * an empty bin, skip a bunch before looking.  After the first skip,
 * then just look one bin at a time so we don't miss our cache on
 * every look. Be sure to check every bin.  Page_create() will panic
 * if we miss a page.
 *
 * This also explains the `<=' in the for loops in both page_get_freelist()
 * and page_get_cachelist().  Since we checked the target bin, skipped
 * a bunch, then continued one a time, we wind up checking the target bin
 * twice to make sure we get all of them bins.
 */
#define	BIN_STEP	19

#ifdef VM_STATS
struct vmm_vmstats_str {
				/* page_get_uflt and page_get_kflt */
	ulong_t pgf_alloc[MMU_PAGE_SIZES];
	ulong_t pgf_allocok[MMU_PAGE_SIZES];
	ulong_t pgf_allocokrem[MMU_PAGE_SIZES];
	ulong_t pgf_allocfailed[MMU_PAGE_SIZES];
	ulong_t	pgf_allocdeferred;
	ulong_t	pgf_allocretry[MMU_PAGE_SIZES];
	ulong_t pgik_allocok;			/* page_import_kflt */
	ulong_t pgik_allocfailed;
	ulong_t pgkx_allocok;			/* kflt_expand */
	ulong_t pgkx_allocfailed;
	ulong_t puak_allocok;			/* page_user_alloc_kflt */
	ulong_t puak_allocfailed;
	ulong_t pgexportok;			/* kflt_export */
	ulong_t pgexportfail;
	ulong_t pgkflt_disable;			/* kflt_user_evict */
	ulong_t pgc_alloc;	/* page_get_cachelist */
	ulong_t pgc_allocok;
	ulong_t pgc_allocokrem;
	ulong_t pgc_allocokdeferred;
	ulong_t pgc_allocfailed;
	ulong_t	pgcp_alloc[MMU_PAGE_SIZES];	/* page_get_contig_pages */
	ulong_t	pgcp_allocfailed[MMU_PAGE_SIZES];
	ulong_t	pgcp_allocempty[MMU_PAGE_SIZES];
	ulong_t	pgcp_allocok[MMU_PAGE_SIZES];
	ulong_t	ptcp[MMU_PAGE_SIZES];		/* page_trylock_contig_pages */
	ulong_t	ptcpfreethresh[MMU_PAGE_SIZES];
	ulong_t	ptcpfailexcl[MMU_PAGE_SIZES];
	ulong_t	ptcpfailszc[MMU_PAGE_SIZES];
	ulong_t	ptcpfailcage[MMU_PAGE_SIZES];
	ulong_t	ptcpfailkflt[MMU_PAGE_SIZES];
	ulong_t	ptcpok[MMU_PAGE_SIZES];
	ulong_t	pgmf_alloc[MMU_PAGE_SIZES];	/* page_get_mnode_freelist */
	ulong_t	pgmf_allocfailed[MMU_PAGE_SIZES];
	ulong_t	pgmf_allocempty[MMU_PAGE_SIZES];
	ulong_t	pgmf_allocok[MMU_PAGE_SIZES];
	ulong_t	pgmc_alloc;			/* page_get_mnode_cachelist */
	ulong_t	pgmc_allocfailed;
	ulong_t	pgmc_allocempty;
	ulong_t	pgmc_allocok;
	ulong_t	pladd_free[MMU_PAGE_SIZES];	/* page_list_add/sub */
	ulong_t	plsub_free[MMU_PAGE_SIZES];
	ulong_t	pladd_cache;
	ulong_t	plsub_cache;
	ulong_t	plsubpages_szcbig;
	ulong_t	plsubpages_szc0;
	ulong_t	pfs_req[MMU_PAGE_SIZES];	/* page_freelist_split */
	ulong_t	pfs_demote[MMU_PAGE_SIZES];
	ulong_t	pfc_coalok[MMU_PAGE_SIZES][MAX_MNODE_MRANGES];
	ulong_t	ppr_reloc[MMU_PAGE_SIZES];	/* page_relocate */
	ulong_t ppr_relocnoroot[MMU_PAGE_SIZES];
	ulong_t ppr_reloc_replnoroot[MMU_PAGE_SIZES];
	ulong_t ppr_relocnolock[MMU_PAGE_SIZES];
	ulong_t ppr_relocnomem[MMU_PAGE_SIZES];
	ulong_t ppr_relocok[MMU_PAGE_SIZES];
	ulong_t ppr_krelocfail[MMU_PAGE_SIZES];
	ulong_t ppr_copyfail;;
	/* page coalesce counter */
	ulong_t page_ctrs_coalesce[MMU_PAGE_SIZES][MAX_MNODE_MRANGES];
	/* candidates useful */
	ulong_t page_ctrs_cands_skip[MMU_PAGE_SIZES][MAX_MNODE_MRANGES];
	/* ctrs changed after locking */
	ulong_t page_ctrs_changed[MMU_PAGE_SIZES][MAX_MNODE_MRANGES];
	/* page_freelist_coalesce failed */
	ulong_t page_ctrs_failed[MMU_PAGE_SIZES][MAX_MNODE_MRANGES];
	ulong_t page_ctrs_coalesce_all;	/* page coalesce all counter */
	ulong_t page_ctrs_cands_skip_all; /* candidates useful for all func */
	ulong_t	restrict4gcnt;
	ulong_t	unrestrict16mcnt;	/* non-DMA 16m allocs allowed */
	ulong_t	pgpanicalloc;		/* PG_PANIC allocation */
	ulong_t	pcf_deny[MMU_PAGE_SIZES];	/* page_chk_freelist */
	ulong_t	pcf_allow[MMU_PAGE_SIZES];
};
extern struct vmm_vmstats_str vmm_vmstats;
#endif	/* VM_STATS */
extern u_longlong_t randtick();
extern size_t page_ctrs_sz(void);
extern caddr_t page_ctrs_alloc(caddr_t);
extern void page_ctr_sub(int, int, page_t *, int);
extern page_t *page_freelist_split(uchar_t, uint_t, int, int, pfn_t, pfn_t, page_list_walker_t *);
extern page_t *page_freelist_coalesce(int, uchar_t, uint_t, uint_t, int, pfn_t);
extern void page_freelist_coalesce_all(int);
extern uint_t page_get_pagecolors(uint_t);
extern void pfnzero(pfn_t, uint_t, uint_t);

#ifdef	__cplusplus
}
#endif

#endif	/* _VM_DEP_H */
