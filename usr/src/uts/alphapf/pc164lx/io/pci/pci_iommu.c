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
#include <sys/kmem.h>
#include <sys/sysmacros.h>
#include <sys/sunddi.h>
#include <sys/ddi_impldefs.h>
#include <sys/vmem.h>
#include <sys/machsystm.h>
#include <sys/pci/pci_obj.h>
#include <sys/pal.h>
#include <sys/promif.h>

#define DEBUG_PRINTF(...)	// prom_printf(__VA_ARGS__)

#define	PYXIS_PCI_TBIA		(*(volatile uint32_t *)0xfffffc8760000100UL)
#define	PYXIS_CTRL		(*(volatile uint32_t *)0xfffffc8740000100UL)
#define	PYXIS_CTRL_PCI_LOOP_EN	(1<<2)
#define	PYXIS_PCI_DENSE(x)	(*(volatile uint32_t *)(0xfffffc8600000000UL + (x)))

#define	PYXIS_PCI_WnBASE(n)	(*(volatile uint32_t *)(0xfffffc8760000400UL + 0x100 * (n)))
#define	PYXIS_PCI_WnMASK(n)	(*(volatile uint32_t *)(0xfffffc8760000440UL + 0x100 * (n)))
#define	PYXIS_PCI_TnBASE(n)	(*(volatile uint32_t *)(0xfffffc8760000480UL + 0x100 * (n)))

void
iommu_create(pci_t *pci_p)
{
	dev_info_t *dip = pci_p->pci_dip;
	iommu_t *iommu_p;

	for (int i = 0; i < 4; i++) {
		DEBUG_PRINTF("%s(): PYXIS_PCI_W%dBASE=%08x\n", __FUNCTION__, i, PYXIS_PCI_WnBASE(i));
		DEBUG_PRINTF("%s(): PYXIS_PCI_W%dMASK=%08x\n", __FUNCTION__, i, PYXIS_PCI_WnMASK(i));
		DEBUG_PRINTF("%s(): PYXIS_PCI_T%dBASE=%08x\n", __FUNCTION__, i, PYXIS_PCI_TnBASE(i));
	}

	DEBUG_PRINTF("%s(): PYXIS_CTRL=%08x\n", __FUNCTION__, PYXIS_CTRL);

	iommu_p = (iommu_t *)kmem_zalloc(sizeof (iommu_t), KM_SLEEP);
	pci_p->pci_iommu_p = iommu_p;
	iommu_p->iommu_pci_p = pci_p;
	iommu_p->iommu_inst = ddi_get_instance(dip);
	iommu_p->iommu_dvma_clid = 0;

	for (int i = 0; i < 4; i++) {
		if (PYXIS_PCI_WnBASE(i) & 1) {
			char buf[10];
			sprintf(buf, "iommu_w%d", i);
			iommu_p->iommu_window[i].window_base = PYXIS_PCI_WnBASE(i) & 0xfff00000;
			iommu_p->iommu_window[i].window_size = (PYXIS_PCI_WnMASK(i) & 0xfff00000) + 0x00100000ul;
			iommu_p->iommu_window[i].trans_base  = (uint64_t *)(((uintptr_t)PYXIS_PCI_TnBASE(i) << 2) + 0xfffffc0000000000UL);
			if (PYXIS_PCI_WnBASE(i) & 2) {
				iommu_p->iommu_window[i].type = IOMMU_SG;
				iommu_p->iommu_window[i].dvma_map =
				    vmem_create(buf,
					(void *)iommu_p->iommu_window[i].window_base,
					iommu_p->iommu_window[i].window_size,
					IOMMU_PAGE_SIZE, NULL, NULL, NULL,
					IOMMU_PAGE_SIZE, VM_SLEEP);
			} else {
				iommu_p->iommu_window[i].type = IOMMU_DIRECT;
				iommu_p->iommu_window[i].dvma_map = 0;
			}
		}
	}

	for (int i=0; i < 4; i++) {
		DEBUG_PRINTF("%s(): iommu_window[%d].window_base=%016lx\n", __FUNCTION__, i, iommu_p->iommu_window[i].window_base);
		DEBUG_PRINTF("%s(): iommu_window[%d].window_size=%016lx\n", __FUNCTION__, i, iommu_p->iommu_window[i].window_size);
		DEBUG_PRINTF("%s(): iommu_window[%d].trans_base= %016lx\n", __FUNCTION__, i, iommu_p->iommu_window[i].trans_base);
	}
}


void
iommu_tbia(pci_t *pci_p)
{
	ulong_t	flag;
	iommu_t *iommu_p;
	iommu_p = pci_p->pci_iommu_p;
	volatile uint32_t ctrl;
	volatile uint32_t dummy;
	flag = pal_swpipl(7);
	__asm__ __volatile__("mb":::"memory");
	ctrl = PYXIS_CTRL;
	PYXIS_CTRL = ctrl | PYXIS_CTRL_PCI_LOOP_EN;
	__asm__ __volatile__("mb":::"memory");

	for (int i = 0; i < 12; i++) {
		dummy = PYXIS_PCI_DENSE(iommu_p->iommu_window[2].window_base + i * 0x8000);
	}

	__asm__ __volatile__("mb":::"memory");
	PYXIS_CTRL = ctrl;
	__asm__ __volatile__("mb":::"memory");
	pal_swpipl(flag);
}

void
iommu_destroy(pci_t *pci_p)
{
	iommu_t *iommu_p;

	iommu_p = pci_p->pci_iommu_p;

	for (int i = 0; i < IOMMU_WINDOW_NUM; i++) {
		if (iommu_p->iommu_window[i].dvma_map)
			vmem_destroy(iommu_p->iommu_window[i].dvma_map);
	}
	kmem_free(iommu_p, sizeof (iommu_t));
	pci_p->pci_iommu_p = NULL;
}
