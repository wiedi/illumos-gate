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
 * PCI nexus DVMA and DMA core routines:
 *	dma_map/dma_bind_handle implementation
 *	bypass and peer-to-peer support
 *	fast track DVMA space allocation
 *	runtime DVMA debug
 */
#include <sys/types.h>
#include <sys/kmem.h>
#include <sys/sysmacros.h>
#include <sys/sunddi.h>
#include <sys/machsystm.h>	/* lddphys() */
#include <sys/ddi_impldefs.h>
#include <vm/hat.h>
#include <sys/pci/pci_obj.h>
#include <sys/promif.h>

ddi_dma_impl_t *
pci_dma_allocmp(dev_info_t *dip, dev_info_t *rdip, int (*waitfp)(caddr_t), caddr_t arg, uint_t sgllen)
{
	ddi_dma_impl_t *mp;
	pci_dma_hdl_t *hp;
	int sleep = (waitfp == DDI_DMA_SLEEP) ? KM_SLEEP : KM_NOSLEEP;
	uint_t max_cookies = sgllen;
	if (max_cookies == 0)
		max_cookies = 1;

	int instance = ddi_get_instance(dip);
	pci_t *pci_p = get_pci_soft_state(instance);

	if (max_cookies <= MAX_DMA_COOKIE) {
		hp = kmem_cache_alloc(pci_p->pci_dma_cache, sleep);
	} else {
		hp = kmem_zalloc(sizeof(pci_dma_hdl_t) + sizeof(ddi_dma_cookie_t) * max_cookies, sleep);
	}
	if (hp == 0) {
		cmn_err(CE_WARN, "%s%d: can't allocate pci dma %p %d", ddi_driver_name(dip), instance, pci_p->pci_dma_cache, sleep);
		if (waitfp != DDI_DMA_DONTWAIT) {
			ddi_set_callback(waitfp, arg, &pci_kmem_clid);
		}
		return 0;
	}

	hp->max_cookies = max_cookies;

	mp = &hp->pdh_ddi_hdl;
	mp->dmai_rdip = rdip;
	mp->dmai_attr.dma_attr_version = (uint_t)DMA_ATTR_VERSION;
	mp->dmai_window_index = -1;

	mp->dmai_error.err_ena = 0;
	mp->dmai_error.err_status = DDI_FM_OK;
	mp->dmai_error.err_expected = DDI_FM_ERR_UNEXPECTED;

	ndi_fmc_insert(rdip, DMA_HANDLE, mp, NULL);

	return (mp);
}

void
pci_dma_freemp(dev_info_t *dip, dev_info_t *rdip, ddi_dma_impl_t *mp)
{
	int instance = ddi_get_instance(dip);
	pci_t *pci_p = get_pci_soft_state(instance);
	ndi_fmc_remove(mp->dmai_rdip, DMA_HANDLE, mp);
	pci_dma_hdl_t *hp = (pci_dma_hdl_t *)mp;
	if (hp->max_cookies <= MAX_DMA_COOKIE)
		kmem_cache_free(pci_p->pci_dma_cache, hp);
	else
		kmem_free(hp, sizeof(pci_dma_hdl_t) + sizeof(ddi_dma_cookie_t) * hp->max_cookies);
}

int
pci_dma_attr2hdl(pci_t *pci_p, ddi_dma_impl_t *mp)
{
	iommu_t *iommu_p = pci_p->pci_iommu_p;
	uint64_t syslo, syshi;
	ddi_dma_attr_t *attrp	= DEV_ATTR(mp);
	uint64_t hi		= attrp->dma_attr_addr_hi;
	uint64_t lo		= attrp->dma_attr_addr_lo;
	uint64_t align		= attrp->dma_attr_align;
	uint64_t nocross	= attrp->dma_attr_seg;
	uint64_t count_max	= attrp->dma_attr_count_max;
	int i;

	if (!nocross)
		nocross--;

	if (!count_max)
		count_max--;

	if (attrp->dma_attr_flags & DDI_DMA_FORCE_PHYSICAL) {
		dev_info_t *rdip = mp->dmai_rdip;
		cmn_err(CE_WARN,
		    "%s%d not support DDI_DMA_FORCE_PHYSICAL", NAMEINST(rdip));
		return (DDI_DMA_BADATTR);
	}
	if (hi <= lo) {
		dev_info_t *rdip = mp->dmai_rdip;
		cmn_err(CE_WARN, "%s%d limits out of range", NAMEINST(rdip));
		return (DDI_DMA_BADATTR);
	}

	align = MAX(align, IOMMU_PAGE_SIZE) - 1;
	if (count_max > align && (align & nocross) != align) {
		dev_info_t *rdip = mp->dmai_rdip;
		cmn_err(CE_WARN, "%s%d dma_attr_seg not aligned",
		    NAMEINST(rdip));
		return (DDI_DMA_BADATTR);
	}

	for (i = 0; i < IOMMU_WINDOW_NUM; i++) {
		if (iommu_p->iommu_window[i].window_base < hi &&
		    lo < iommu_p->iommu_window[i].window_base +
		    iommu_p->iommu_window[i].window_size)
		mp->dmai_flags |= DMAI_FLAGS_WINEN(i);
	}

	if ((mp->dmai_flags & DMAI_FLAGS_WINMASK) == 0) {
		dev_info_t *rdip = mp->dmai_rdip;
		cmn_err(CE_WARN, "%s%d limits out of range", NAMEINST(rdip));
		return (DDI_DMA_BADATTR);
	}

	count_max = MIN(count_max, hi - lo);

	mp->dmai_minxfer	= attrp->dma_attr_minxfer;
	mp->dmai_burstsizes	= attrp->dma_attr_burstsizes;
	attrp = &mp->dmai_attr;
	SET_DMAATTR(attrp, lo, hi, nocross, count_max);

	return (DDI_SUCCESS);
}

int
pci_dma_map(pci_t *pci_p, ddi_dma_req_t *dmareq, ddi_dma_impl_t *mp)
{
	dev_info_t *dip = pci_p->pci_dip;
	ddi_dma_obj_t *dobj_p = &dmareq->dmar_object;
	page_t **pplist;
	struct as *as_p;
	uint32_t offset;
	caddr_t vaddr;
	pfn_t pfn0;
	uint_t npages;
	void *dvma_addr;
	iommu_t *iommu_p = pci_p->pci_iommu_p;
	page_t *pp;
	int i;
#if 0
	prom_printf("%s(): %d\n", __FUNCTION__, __LINE__);
#endif
	switch (dobj_p->dmao_type) {
	case DMA_OTYP_BUFVADDR:
	case DMA_OTYP_VADDR:
		vaddr = dobj_p->dmao_obj.virt_obj.v_addr;
		pplist = dobj_p->dmao_obj.virt_obj.v_priv;
		offset = (ulong_t)vaddr & IOMMU_PAGE_OFFSET;
		if (pplist) {
			mp->dmai_flags |= DMAI_FLAGS_PGPFN;
			ASSERT(PAGE_LOCKED(*pplist));
			pfn0 = page_pptonum(*pplist);
		} else {
			as_p = dobj_p->dmao_obj.virt_obj.v_as;
			if (as_p == NULL)
				as_p = &kas;
			pfn0 = hat_getpfnum(as_p->a_hat, vaddr);
		}
		break;

	case DMA_OTYP_PAGES:
		offset = dobj_p->dmao_obj.pp_obj.pp_offset;
		mp->dmai_flags |= DMAI_FLAGS_PGPFN;
		pp = dobj_p->dmao_obj.pp_obj.pp_pp;
		pfn0 = page_pptonum(pp);
		ASSERT(PAGE_LOCKED(pp));
		break;

	case DMA_OTYP_PADDR:
	default:
		cmn_err(CE_WARN, "%s%d requested unsupported dma type %x",
		    NAMEINST(mp->dmai_rdip), dobj_p->dmao_type);
		return (DDI_DMA_NOMAPPING);
	}

	if (pfn0 == PFN_INVALID) {
		cmn_err(CE_WARN, "%s%d: invalid pfn0 for DMA object %p",
		    NAMEINST(dip), dobj_p);
		return (DDI_DMA_NOMAPPING);
	}

	mp->dmai_object	 = *dobj_p;			/* whole object    */
	mp->dmai_pfn0	 = (void *)pfn0;		/* cache pfn0	   */
	mp->dmai_roffset = offset;			/* win0 pg0 offset */
	mp->dmai_ndvmapages = IOMMU_BTOPR(offset + mp->dmai_object.dmao_size);

	pci_dma_hdl_t *hp = (pci_dma_hdl_t *)mp;
	mp->dmai_window_index = 1;
	window_t *window_p = &iommu_p->iommu_window[mp->dmai_window_index];
	mp->dmai_nwin = 1;
	mp->dmai_mapping = 0;

	// check
	int debug_log = 0;
	int ncookies;
	pfn_t prev_pfn;
	size_t prev_npfn;
	size_t left;
	size_t map_size;
retry:
	ncookies = 0;
	prev_pfn = pfn0;
	prev_npfn = 1;
	left = mp->dmai_object.dmao_size;
	if (debug_log) prom_printf("%s:%d %d %d\n",__func__,__LINE__,ncookies, hp->max_cookies);
	if (debug_log) prom_printf("%s:%d pfn0=%lx offset=%lx left=%lx\n",__func__,__LINE__,pfn0, offset, left);
	for (i = 1; i < mp->dmai_ndvmapages; i++) {
		pfn_t pfn = 0;
		if (mp->dmai_flags & DMAI_FLAGS_PGPFN) {
			switch (mp->dmai_object.dmao_type) {
			case DMA_OTYP_BUFVADDR:
			case DMA_OTYP_VADDR:
				pfn = page_pptonum(pplist[i]);
				ASSERT(PAGE_LOCKED(pplist[i]));
				break;
			case DMA_OTYP_PAGES:
				pp = pp->p_next;
				pfn = page_pptonum(pp);
				ASSERT(PAGE_LOCKED(pp));
				break;
			}
		} else {
			pfn = hat_getpfnum(as_p->a_hat, vaddr + i * IOMMU_PAGE_SIZE);
		}
		if (debug_log) prom_printf("%s:%d pfn=%lx prev_pfn=%lx prev_npfn=%lx left=%lx\n",__func__,__LINE__,pfn, prev_pfn, prev_npfn, left);

		if (prev_pfn + prev_npfn == pfn) {
			prev_npfn++;
		} else {
			if (ncookies >= hp->max_cookies) {
				cmn_err(CE_WARN, "%s%d: invalid pfn for DMA object %p",
				    NAMEINST(dip), dobj_p);
				prom_printf("%s:%d %d %d\n",__func__,__LINE__,ncookies, hp->max_cookies);
				if (debug_log == 0) {
					debug_log = 1;
					goto retry;
				}
				return (DDI_DMA_NOMAPPING);
			}
			dvma_addr = (caddr_t)window_p->window_base + IOMMU_PTOB(prev_pfn) + ((ncookies == 0)? offset: 0);
			map_size = prev_npfn * IOMMU_PAGE_SIZE - ((ncookies == 0)? offset: 0);
			MAKE_DMA_COOKIE(hp->cookies + ncookies, (uintptr_t)dvma_addr, map_size);
			left -= map_size;

			ncookies++;
			prev_npfn = 1;
			prev_pfn = pfn;
		}
	}
	if (debug_log) prom_printf("%s:%d  prev_pfn=%lx prev_npfn=%lx left=%lx\n",__func__,__LINE__, prev_pfn, prev_npfn, left);

	if (ncookies >= hp->max_cookies) {
		cmn_err(CE_WARN, "%s%d: invalid pfn for DMA object %p",
		    NAMEINST(dip), dobj_p);
		prom_printf("%s:%d %d %d\n",__func__,__LINE__,ncookies, hp->max_cookies);
		if (debug_log == 0) {
			debug_log = 1;
			goto retry;
		}
		return (DDI_DMA_NOMAPPING);
	}
	dvma_addr = (caddr_t)window_p->window_base + IOMMU_PTOB(prev_pfn) + ((ncookies == 0)? (offset & IOMMU_PAGE_OFFSET): 0);
	MAKE_DMA_COOKIE(hp->cookies + ncookies, (uintptr_t)dvma_addr, left);
	ASSERT((((uint64_t)dvma_addr + left + IOMMU_PAGE_SIZE - 1) & IOMMU_PAGE_MASK) == window_p->window_base + IOMMU_PTOB(prev_pfn + prev_npfn));
	ncookies++;

	hp->ncookies = ncookies;

	return (DDI_SUCCESS);
}

void
pci_dma_unmap(pci_t *pci_p, ddi_dma_impl_t *mp)
{
	iommu_t *iommu_p = pci_p->pci_iommu_p;
	pci_dma_hdl_t *hp = (pci_dma_hdl_t *)mp;
	mp->dmai_flags &= ~DMAI_FLAGS_PGPFN;
	mp->dmai_mapping = 0;
	mp->dmai_ndvmapages = 0;
	mp->dmai_window_index = -1;
	mp->dmai_pfn0 = 0;
	mp->dmai_roffset = 0;
	hp->ncookies = 0;
}
