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
 * Copyright (c) 1992, 2010, Oracle and/or its affiliates. All rights reserved.
 */
/*
 * Copyright 2017 Hayashi Naoyuki
 * Copyright 2011 Nexenta Systems, Inc.  All rights reserved.
 * Copyright (c) 2011 Bayard G. Bell.  All rights reserved.
 * Copyright 2012 Garrett D'Amore <garrett@damore.org>.  All rights reserved.
 */

/*
 * X-Gene root nexus driver
 */

#include <sys/sysmacros.h>
#include <sys/conf.h>
#include <sys/autoconf.h>
#include <sys/sysmacros.h>
#include <sys/debug.h>
#include <sys/psw.h>
#include <sys/ddidmareq.h>
#include <sys/promif.h>
#include <sys/devops.h>
#include <sys/kmem.h>
#include <sys/cmn_err.h>
#include <vm/seg.h>
#include <vm/seg_kmem.h>
#include <vm/seg_dev.h>
#include <sys/vmem.h>
#include <sys/mman.h>
#include <vm/hat.h>
#include <vm/as.h>
#include <vm/page.h>
#include <sys/avintr.h>
#include <sys/errno.h>
#include <sys/modctl.h>
#include <sys/ddi_impldefs.h>
#include <sys/sunddi.h>
#include <sys/sunndi.h>
#include <sys/mach_intr.h>
#include <sys/ontrap.h>
#include <sys/atomic.h>
#include <sys/sdt.h>
#include <sys/rootnex.h>
#include <vm/hat_aarch64.h>
#include <sys/ddifm.h>
#include <sys/ddi_isa.h>
#include <sys/spl.h>
#include <sys/machparam.h>
#include <sys/platform.h>

#define	DEV_ATTR(mp)		(&((rootnex_dma_hdl_t *)(mp))->pdh_attr_dev)
#define	NAMEINST(dip)	ddi_driver_name(dip), ddi_get_instance(dip)
#define	SET_DMAATTR(p, lo, hi, nocross, cntmax)	\
	(p)->dma_attr_addr_lo	= (lo); \
	(p)->dma_attr_addr_hi	= (hi); \
	(p)->dma_attr_seg	= (nocross); \
	(p)->dma_attr_count_max	= (cntmax)

static uintptr_t rootnex_kmem_clid = 0;

/*
 * enable/disable extra checking of function parameters. Useful for debugging
 * drivers.
 */
#ifdef	DEBUG
int rootnex_alloc_check_parms = 1;
int rootnex_bind_check_parms = 1;
int rootnex_bind_check_inuse = 1;
int rootnex_unbind_verify_buffer = 0;
int rootnex_sync_check_parms = 1;
#else
int rootnex_alloc_check_parms = 0;
int rootnex_bind_check_parms = 0;
int rootnex_bind_check_inuse = 0;
int rootnex_unbind_verify_buffer = 0;
int rootnex_sync_check_parms = 0;
#endif

boolean_t rootnex_dmar_not_setup;

/* Master Abort and Target Abort panic flag */
int rootnex_fm_ma_ta_panic_flag = 0;

/* Semi-temporary patchables to phase in bug fixes, test drivers, etc. */
int rootnex_bind_fail = 1;
int rootnex_bind_warn = 1;
uint8_t *rootnex_warn_list;
/* bitmasks for rootnex_warn_list. Up to 8 different warnings with uint8_t */
#define	ROOTNEX_BIND_WARNING	(0x1 << 0)

/*
 * revert back to old broken behavior of always sync'ing entire copy buffer.
 * This is useful if be have a buggy driver which doesn't correctly pass in
 * the offset and size into ddi_dma_sync().
 */
int rootnex_sync_ignore_params = 0;

/*
 * For the 64-bit kernel, pre-alloc enough cookies for a 256K buffer plus 1
 * page for alignment. For the 32-bit kernel, pre-alloc enough cookies for a
 * 64K buffer plus 1 page for alignment (we have less kernel space in a 32-bit
 * kernel). Allocate enough windows to handle a 256K buffer w/ at least 65
 * sgllen DMA engine, and enough copybuf buffer state pages to handle 2 pages
 * (< 8K). We will still need to allocate the copy buffer during bind though
 * (if we need one). These can only be modified in /etc/system before rootnex
 * attach.
 */
int rootnex_prealloc_cookies = 65;
int rootnex_prealloc_windows = 4;
int rootnex_prealloc_copybuf = 2;

/* driver global state */
static rootnex_state_t *rootnex_state;

#ifdef DEBUG
/* shortcut to rootnex counters */
static uint64_t *rootnex_cnt;
#endif

/*
 * XXX - does x86 even need these or are they left over from the SPARC days?
 */
/* statically defined integer/boolean properties for the root node */
static rootnex_intprop_t rootnex_intprp[] = {
	{ "PAGESIZE",			PAGESIZE },
	{ "MMU_PAGESIZE",		MMU_PAGESIZE },
	{ "MMU_PAGEOFFSET",		MMU_PAGEOFFSET },
	{ DDI_RELATIVE_ADDRESSING,	1 },
};
#define	NROOT_INTPROPS	(sizeof (rootnex_intprp) / sizeof (rootnex_intprop_t))

/*
 * If we're dom0, we're using a real device so we need to load
 * the cookies with MFNs instead of PFNs.
 */
#ifdef __xpv
typedef maddr_t rootnex_addr_t;
#define	ROOTNEX_PADDR_TO_RBASE(pa)	\
	(DOMAIN_IS_INITDOMAIN(xen_info) ? pa_to_ma(pa) : (pa))
#else
typedef paddr_t rootnex_addr_t;
#define	ROOTNEX_PADDR_TO_RBASE(pa)	(pa)
#endif

static struct cb_ops rootnex_cb_ops = {
	nodev,		/* open */
	nodev,		/* close */
	nodev,		/* strategy */
	nodev,		/* print */
	nodev,		/* dump */
	nodev,		/* read */
	nodev,		/* write */
	nodev,		/* ioctl */
	nodev,		/* devmap */
	nodev,		/* mmap */
	nodev,		/* segmap */
	nochpoll,	/* chpoll */
	ddi_prop_op,	/* cb_prop_op */
	NULL,		/* struct streamtab */
	D_NEW | D_MP | D_HOTPLUG, /* compatibility flags */
	CB_REV,		/* Rev */
	nodev,		/* cb_aread */
	nodev		/* cb_awrite */
};

static int rootnex_map(dev_info_t *dip, dev_info_t *rdip, ddi_map_req_t *mp,
    off_t offset, off_t len, caddr_t *vaddrp);
static int rootnex_map_fault(dev_info_t *dip, dev_info_t *rdip,
    struct hat *hat, struct seg *seg, caddr_t addr,
    struct devpage *dp, pfn_t pfn, uint_t prot, uint_t lock);
static int rootnex_dma_allochdl(dev_info_t *dip, dev_info_t *rdip,
    ddi_dma_attr_t *attr, int (*waitfp)(caddr_t), caddr_t arg,
    ddi_dma_handle_t *handlep);
static int rootnex_dma_freehdl(dev_info_t *dip, dev_info_t *rdip,
    ddi_dma_handle_t handle);
static int rootnex_dma_bindhdl(dev_info_t *dip, dev_info_t *rdip,
    ddi_dma_handle_t handle, struct ddi_dma_req *dmareq,
    ddi_dma_cookie_t *cookiep, uint_t *ccountp);
static int rootnex_dma_unbindhdl(dev_info_t *dip, dev_info_t *rdip,
    ddi_dma_handle_t handle);
static int rootnex_dma_sync(dev_info_t *dip, dev_info_t *rdip,
    ddi_dma_handle_t handle, off_t off, size_t len, uint_t cache_flags);
static int rootnex_dma_win(dev_info_t *dip, dev_info_t *rdip,
    ddi_dma_handle_t handle, uint_t win, off_t *offp, size_t *lenp,
    ddi_dma_cookie_t *cookiep, uint_t *ccountp);
static int rootnex_dma_mctl(dev_info_t *dip, dev_info_t *rdip,
    ddi_dma_handle_t handle, enum ddi_dma_ctlops request,
    off_t *offp, size_t *lenp, caddr_t *objp, uint_t cache_flags);
static int rootnex_ctlops(dev_info_t *dip, dev_info_t *rdip,
    ddi_ctl_enum_t ctlop, void *arg, void *result);
static int rootnex_fm_init(dev_info_t *dip, dev_info_t *tdip, int tcap,
    ddi_iblock_cookie_t *ibc);
static int rootnex_intr_ops(dev_info_t *pdip, dev_info_t *rdip,
    ddi_intr_op_t intr_op, ddi_intr_handle_impl_t *hdlp, void *result);

static int rootnex_coredma_allochdl(dev_info_t *dip, dev_info_t *rdip,
    ddi_dma_attr_t *attr, int (*waitfp)(caddr_t), caddr_t arg,
    ddi_dma_handle_t *handlep);
static int rootnex_coredma_freehdl(dev_info_t *dip, dev_info_t *rdip,
    ddi_dma_handle_t handle);
static int rootnex_coredma_bindhdl(dev_info_t *dip, dev_info_t *rdip,
    ddi_dma_handle_t handle, struct ddi_dma_req *dmareq,
    ddi_dma_cookie_t *cookiep, uint_t *ccountp);
static int rootnex_coredma_unbindhdl(dev_info_t *dip, dev_info_t *rdip,
    ddi_dma_handle_t handle);
static int rootnex_coredma_sync(dev_info_t *dip, dev_info_t *rdip,
    ddi_dma_handle_t handle, off_t off, size_t len, uint_t cache_flags);
static int rootnex_coredma_win(dev_info_t *dip, dev_info_t *rdip,
    ddi_dma_handle_t handle, uint_t win, off_t *offp, size_t *lenp,
    ddi_dma_cookie_t *cookiep, uint_t *ccountp);


static struct bus_ops rootnex_bus_ops = {
	BUSO_REV,
	rootnex_map,
	NULL,
	NULL,
	NULL,
	rootnex_map_fault,
	0,
	rootnex_dma_allochdl,
	rootnex_dma_freehdl,
	rootnex_dma_bindhdl,
	rootnex_dma_unbindhdl,
	rootnex_dma_sync,
	rootnex_dma_win,
	rootnex_dma_mctl,
	rootnex_ctlops,
	ddi_bus_prop_op,
	i_ddi_rootnex_get_eventcookie,
	i_ddi_rootnex_add_eventcall,
	i_ddi_rootnex_remove_eventcall,
	i_ddi_rootnex_post_event,
	0,			/* bus_intr_ctl */
	0,			/* bus_config */
	0,			/* bus_unconfig */
	rootnex_fm_init,	/* bus_fm_init */
	NULL,			/* bus_fm_fini */
	NULL,			/* bus_fm_access_enter */
	NULL,			/* bus_fm_access_exit */
	NULL,			/* bus_powr */
	rootnex_intr_ops	/* bus_intr_op */
};

static int rootnex_attach(dev_info_t *dip, ddi_attach_cmd_t cmd);
static int rootnex_detach(dev_info_t *dip, ddi_detach_cmd_t cmd);
static int rootnex_quiesce(dev_info_t *dip);

static struct dev_ops rootnex_ops = {
	DEVO_REV,
	0,
	ddi_no_info,
	nulldev,
	nulldev,
	rootnex_attach,
	rootnex_detach,
	nulldev,
	&rootnex_cb_ops,
	&rootnex_bus_ops,
	NULL,
	rootnex_quiesce,		/* quiesce */
};

static struct modldrv rootnex_modldrv = {
	&mod_driverops,
	"X-Gene root nexus",
	&rootnex_ops
};

static struct modlinkage rootnex_modlinkage = {
	MODREV_1,
	(void *)&rootnex_modldrv,
	NULL
};

/*
 *  extern hacks
 */
extern struct seg_ops segdev_ops;
extern int ignore_hardware_nodes;	/* force flag from ddi_impl.c */
#ifdef	DDI_MAP_DEBUG
extern int ddi_map_debug_flag;
#define	ddi_map_debug	if (ddi_map_debug_flag) prom_printf
#endif
extern int impl_ddi_sunbus_initchild(dev_info_t *dip);
extern void impl_ddi_sunbus_removechild(dev_info_t *dip);

/*
 * Use device arena to use for device control register mappings.
 * Various kernel memory walkers (debugger, dtrace) need to know
 * to avoid this address range to prevent undesired device activity.
 */
extern void *device_arena_alloc(size_t size, int vm_flag);
extern void device_arena_free(void * vaddr, size_t size);


/*
 *  Internal functions
 */
static int rootnex_dma_init();
static void rootnex_add_props(dev_info_t *);
static int rootnex_ctl_reportdev(dev_info_t *dip);
static struct intrspec *rootnex_get_ispec(dev_info_t *rdip, int inum);
static int rootnex_map_regspec(ddi_map_req_t *mp, caddr_t *vaddrp);
static int rootnex_unmap_regspec(ddi_map_req_t *mp, caddr_t *vaddrp);
static int rootnex_map_handle(ddi_map_req_t *mp, off_t offset);

/*
 * _init()
 *
 */
int
_init(void)
{

	rootnex_state = NULL;
	return (mod_install(&rootnex_modlinkage));
}


/*
 * _info()
 *
 */
int
_info(struct modinfo *modinfop)
{
	return (mod_info(&rootnex_modlinkage, modinfop));
}


/*
 * _fini()
 *
 */
int
_fini(void)
{
	return (EBUSY);
}


/*
 * rootnex_attach()
 *
 */
static int
rootnex_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	int fmcap;
	int e;

	switch (cmd) {
	case DDI_ATTACH:
		break;
	case DDI_RESUME:
		return (DDI_SUCCESS);
	default:
		return (DDI_FAILURE);
	}

	/*
	 * We should only have one instance of rootnex. Save it away since we
	 * don't have an easy way to get it back later.
	 */
	ASSERT(rootnex_state == NULL);
	rootnex_state = kmem_zalloc(sizeof (rootnex_state_t), KM_SLEEP);

	rootnex_state->r_dip = dip;
	rootnex_state->r_err_ibc = (ddi_iblock_cookie_t)ipltospl(15);

	ddi_system_fmcap = DDI_FM_EREPORT_CAPABLE | DDI_FM_ERRCB_CAPABLE |
	    DDI_FM_ACCCHK_CAPABLE | DDI_FM_DMACHK_CAPABLE;
	fmcap = ddi_system_fmcap;
	ddi_fm_init(dip, &fmcap, &rootnex_state->r_err_ibc);

	/* Add static root node properties */
	rootnex_add_props(dip);

	/* since we can't call ddi_report_dev() */
	cmn_err(CE_CONT, "?root nexus = %s\n", ddi_get_name(dip));

	/* Initialize rootnex event handle */
	i_ddi_rootnex_init_events(dip);

	return (DDI_SUCCESS);
}


/*
 * rootnex_detach()
 *
 */
/*ARGSUSED*/
static int
rootnex_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	switch (cmd) {
	case DDI_SUSPEND:
		return (DDI_SUCCESS);
	default:
		return (DDI_FAILURE);
	}
	/*NOTREACHED*/

}


/*
 * rootnex_add_props()
 *
 */
static void
rootnex_add_props(dev_info_t *dip)
{
	rootnex_intprop_t *rpp;
	int i;

	/* Add static integer/boolean properties to the root node */
	rpp = rootnex_intprp;
	for (i = 0; i < NROOT_INTPROPS; i++) {
		(void) e_ddi_prop_update_int(DDI_DEV_T_NONE, dip,
		    rpp[i].prop_name, rpp[i].prop_value);
	}
}



/*
 * *************************
 *  ctlops related routines
 * *************************
 */

/*
 * rootnex_ctlops()
 *
 */
/*ARGSUSED*/
static int
rootnex_ctlops(dev_info_t *dip, dev_info_t *rdip, ddi_ctl_enum_t ctlop,
    void *arg, void *result)
{
	int n, *ptr;
	struct ddi_parent_private_data *pdp;

	switch (ctlop) {
	case DDI_CTLOPS_DMAPMAPC:
		/*
		 * Return 'partial' to indicate that dma mapping
		 * has to be done in the main MMU.
		 */
		return (DDI_DMA_PARTIAL);

	case DDI_CTLOPS_BTOP:
		/*
		 * Convert byte count input to physical page units.
		 * (byte counts that are not a page-size multiple
		 * are rounded down)
		 */
		*(ulong_t *)result = btop(*(ulong_t *)arg);
		return (DDI_SUCCESS);

	case DDI_CTLOPS_PTOB:
		/*
		 * Convert size in physical pages to bytes
		 */
		*(ulong_t *)result = ptob(*(ulong_t *)arg);
		return (DDI_SUCCESS);

	case DDI_CTLOPS_BTOPR:
		/*
		 * Convert byte count input to physical page units
		 * (byte counts that are not a page-size multiple
		 * are rounded up)
		 */
		*(ulong_t *)result = btopr(*(ulong_t *)arg);
		return (DDI_SUCCESS);

	case DDI_CTLOPS_INITCHILD:
		return (impl_ddi_sunbus_initchild(arg));

	case DDI_CTLOPS_UNINITCHILD:
		impl_ddi_sunbus_removechild(arg);
		return (DDI_SUCCESS);

	case DDI_CTLOPS_REPORTDEV:
		return (rootnex_ctl_reportdev(rdip));

	case DDI_CTLOPS_IOMIN:
		/*
		 * Nothing to do here but reflect back..
		 */
		return (DDI_SUCCESS);

	case DDI_CTLOPS_REGSIZE:
	case DDI_CTLOPS_NREGS:
		break;

	case DDI_CTLOPS_SIDDEV:
		if (ndi_dev_is_prom_node(rdip))
			return (DDI_SUCCESS);
		if (ndi_dev_is_persistent_node(rdip))
			return (DDI_SUCCESS);
		return (DDI_FAILURE);

	case DDI_CTLOPS_POWER:
		return ((*pm_platform_power)((power_req_t *)arg));

	case DDI_CTLOPS_RESERVED0: /* Was DDI_CTLOPS_NINTRS, obsolete */
	case DDI_CTLOPS_RESERVED1: /* Was DDI_CTLOPS_POKE_INIT, obsolete */
	case DDI_CTLOPS_RESERVED2: /* Was DDI_CTLOPS_POKE_FLUSH, obsolete */
	case DDI_CTLOPS_RESERVED3: /* Was DDI_CTLOPS_POKE_FINI, obsolete */
	case DDI_CTLOPS_RESERVED4: /* Was DDI_CTLOPS_INTR_HILEVEL, obsolete */
	case DDI_CTLOPS_RESERVED5: /* Was DDI_CTLOPS_XLATE_INTRS, obsolete */
		return (DDI_FAILURE);

	default:
		return (DDI_FAILURE);
	}
	/*
	 * The rest are for "hardware" properties
	 */
	if ((pdp = ddi_get_parent_data(rdip)) == NULL)
		return (DDI_FAILURE);

	if (ctlop == DDI_CTLOPS_NREGS) {
		ptr = (int *)result;
		*ptr = pdp->par_nreg;
	} else {
		off_t *size = (off_t *)result;

		ptr = (int *)arg;
		n = *ptr;
		if (n >= pdp->par_nreg) {
			return (DDI_FAILURE);
		}
		uint64_t regspec_size = pdp->par_reg[n].regspec_size;
		regspec_size |= (((uint64_t)((pdp->par_reg[n].regspec_bustype >> 16) & 0xfff)) << 32);
		*size = (off_t)regspec_size;
	}
	return (DDI_SUCCESS);
}


/*
 * rootnex_ctl_reportdev()
 *
 */
static int
rootnex_ctl_reportdev(dev_info_t *dev)
{
	int i, n, len, f_len = 0;
	char *buf;

	buf = kmem_alloc(REPORTDEV_BUFSIZE, KM_SLEEP);
	f_len += snprintf(buf, REPORTDEV_BUFSIZE,
	    "%s%d at root", ddi_driver_name(dev), ddi_get_instance(dev));
	len = strlen(buf);

	for (i = 0; i < sparc_pd_getnreg(dev); i++) {

		struct regspec *rp = sparc_pd_getreg(dev, i);

		if (i == 0)
			f_len += snprintf(buf + len, REPORTDEV_BUFSIZE - len,
			    ": ");
		else
			f_len += snprintf(buf + len, REPORTDEV_BUFSIZE - len,
			    " and ");
		len = strlen(buf);

		uint64_t regspec_addr = rp->regspec_addr;
		regspec_addr |= (((uint64_t)(rp->regspec_bustype & 0xffff)) << 32);

		switch (rp->regspec_bustype >> 28) {
		default:
			f_len += snprintf(buf + len, REPORTDEV_BUFSIZE - len,
			    "space %x offset %lx",
			    rp->regspec_bustype >> 28, regspec_addr);
			break;
		}
		len = strlen(buf);
	}
	for (i = 0, n = sparc_pd_getnintr(dev); i < n; i++) {
		int pri;

		if (i != 0) {
			f_len += snprintf(buf + len, REPORTDEV_BUFSIZE - len,
			    ",");
			len = strlen(buf);
		}
		pri = INT_IPL(sparc_pd_getintr(dev, i)->intrspec_pri);
		f_len += snprintf(buf + len, REPORTDEV_BUFSIZE - len,
		    " sparc ipl %d", pri);
		len = strlen(buf);
	}
#ifdef DEBUG
	if (f_len + 1 >= REPORTDEV_BUFSIZE) {
		cmn_err(CE_NOTE, "next message is truncated: "
		    "printed length 1024, real length %d", f_len);
	}
#endif /* DEBUG */
	cmn_err(CE_CONT, "?%s\n", buf);
	kmem_free(buf, REPORTDEV_BUFSIZE);
	return (DDI_SUCCESS);
}


/*
 * ******************
 *  map related code
 * ******************
 */

/*
 * rootnex_map()
 *
 */
static int
rootnex_map(dev_info_t *dip, dev_info_t *rdip, ddi_map_req_t *mp, off_t offset,
    off_t len, caddr_t *vaddrp)
{
	struct regspec *rp, tmp_reg;
	ddi_map_req_t mr = *mp;
	int error;
	ddi_acc_hdl_t *hp = NULL;

	mp = &mr;

	switch (mp->map_op)  {
	case DDI_MO_MAP_LOCKED:
	case DDI_MO_UNMAP:
	case DDI_MO_MAP_HANDLE:
		break;
	default:
		cmn_err(CE_WARN, "rootnex_map: unimplemented map "
		    "op %d.", mp->map_op);
		return (DDI_ME_UNIMPLEMENTED);
	}

	if (mp->map_flags & DDI_MF_USER_MAPPING)  {
		cmn_err(CE_WARN, "rootnex_map: unimplemented map "
		    "type: user.");
		return (DDI_ME_UNIMPLEMENTED);
	}

	if (mp->map_type == DDI_MT_RNUMBER)  {
		int rnumber = mp->map_obj.rnumber;

		rp = i_ddi_rnumber_to_regspec(rdip, rnumber);
		if (rp == (struct regspec *)0)  {
			cmn_err(CE_WARN, "rootnex_map: Out of "
			    "range rnumber <%d>, device <%s>", rnumber,
			    ddi_get_name(rdip));
			return (DDI_ME_RNUMBER_RANGE);
		}

		mp->map_type = DDI_MT_REGSPEC;
		mp->map_obj.rp = rp;
	}

	tmp_reg = *(mp->map_obj.rp);
	rp = mp->map_obj.rp = &tmp_reg;

	if (len != 0)
		rp->regspec_size = (uint_t)len;

	if ((error = i_ddi_apply_range(dip, rdip, mp->map_obj.rp)) != 0)
		return (error);

	switch (mp->map_op)  {
	case DDI_MO_MAP_LOCKED:
		if (mp->map_handlep) {
			hp = mp->map_handlep;
		}
		if (rp->regspec_bustype >> 28) {
			if (mp->map_flags & DDI_MF_DEVICE_MAPPING)
				return (DDI_ME_INVAL);
			error = rootnex_map_regspec(mp, vaddrp);
			if (error == 0) {
				if (hp) {
					ddi_acc_impl_t *ap = (ddi_acc_impl_t *)hp->ah_platform_private;
					ap->ahi_acc_attr |= DDI_ACCATTR_IO_SPACE;
					hp->ah_addr = (caddr_t)offset;
					ap->ahi_io_port_base = (ulong_t)*vaddrp;
					*vaddrp = (caddr_t)offset;
					hp->ah_hat_flags = 0;
					impl_acc_hdl_init(hp);
				}
				*vaddrp = (caddr_t)offset;
			}
		} else {
			uint64_t regspec_addr = rp->regspec_addr;
			regspec_addr |= (((uint64_t)(rp->regspec_bustype & 0xffff)) << 32);
			regspec_addr += offset;

			rp->regspec_addr = regspec_addr & 0xffffffff;
			rp->regspec_bustype &= ~0xffff;
			rp->regspec_bustype |= ((regspec_addr >> 32) & 0xffff);

			error = rootnex_map_regspec(mp, vaddrp);
			if (error == 0) {
				if (hp) {
					ddi_acc_impl_t *ap = (ddi_acc_impl_t *)hp->ah_platform_private;
					ap->ahi_acc_attr |= DDI_ACCATTR_CPU_VADDR;
					hp->ah_addr = *vaddrp;
					hp->ah_hat_flags = 0;
					impl_acc_hdl_init(hp);
				}
			}
		}
		return (error);

	case DDI_MO_UNMAP:
		return (rootnex_unmap_regspec(mp, vaddrp));

	case DDI_MO_MAP_HANDLE:
		return (rootnex_map_handle(mp, offset));
	}

	return (DDI_ME_UNIMPLEMENTED);
}


/*
 * rootnex_map_fault()
 *
 *	fault in mappings for requestors
 */
/*ARGSUSED*/
static int
rootnex_map_fault(dev_info_t *dip, dev_info_t *rdip, struct hat *hat,
    struct seg *seg, caddr_t addr, struct devpage *dp, pfn_t pfn, uint_t prot,
    uint_t lock)
{

#ifdef	DDI_MAP_DEBUG
	ddi_map_debug("rootnex_map_fault: address <%x> pfn <%x>", addr, pfn);
	ddi_map_debug(" Seg <%s>\n",
	    seg->s_ops == &segdev_ops ? "segdev" :
	    seg == &kvseg ? "segkmem" : "NONE!");
#endif	/* DDI_MAP_DEBUG */

	/*
	 * This is all terribly broken, but it is a start
	 *
	 * XXX	Note that this test means that segdev_ops
	 *	must be exported from seg_dev.c.
	 * XXX	What about devices with their own segment drivers?
	 */
	if (seg->s_ops == &segdev_ops) {
		struct segdev_data *sdp = (struct segdev_data *)seg->s_data;

		if (hat == NULL) {
			/*
			 * This is one plausible interpretation of
			 * a null hat i.e. use the first hat on the
			 * address space hat list which by convention is
			 * the hat of the system MMU.  At alternative
			 * would be to panic .. this might well be better ..
			 */
			ASSERT(AS_READ_HELD(seg->s_as));
			hat = seg->s_as->a_hat;
			cmn_err(CE_NOTE, "rootnex_map_fault: nil hat");
		}
		hat_devload(hat, addr, MMU_PAGESIZE, pfn, prot | sdp->hat_attr,
		    (lock ? HAT_LOAD_LOCK : HAT_LOAD));
	} else if (seg == &kvseg && dp == NULL) {
		hat_devload(kas.a_hat, addr, MMU_PAGESIZE, pfn, prot,
		    HAT_LOAD_LOCK);
	} else
		return (DDI_FAILURE);
	return (DDI_SUCCESS);
}


/*
 * rootnex_map_regspec()
 *     we don't support mapping of I/O cards above 4Gb
 */
static int
rootnex_map_regspec(ddi_map_req_t *mp, caddr_t *vaddrp)
{
	caddr_t kaddr;
	pfn_t	pfn;
	struct regspec *rp = mp->map_obj.rp;
	ddi_acc_hdl_t *hp;
	uint_t	pgoffset;

	uint64_t regspec_size = rp->regspec_size;
	regspec_size |= (((uint64_t)((rp->regspec_bustype >> 16) & 0xfff)) << 32);

	if (regspec_size == 0) {
		cmn_err(CE_NOTE, "rootnex_map_regspec: zero regspec_size\n");
		return (DDI_ME_INVAL);
	}

	uint64_t regspec_addr = rp->regspec_addr;
	regspec_addr |= (((uint64_t)(rp->regspec_bustype & 0xffff)) << 32);
	pfn = mmu_btop(regspec_addr);
	if (mp->map_flags & DDI_MF_DEVICE_MAPPING)
		*vaddrp = (caddr_t)pfn;
	else {
		pgoffset = (ulong_t)regspec_addr & MMU_PAGEOFFSET;
		paddr_t pa = mmu_ptob(pfn) | pgoffset;
		*vaddrp = (caddr_t)(SEGKPM_BASE | pa);
		cmn_err(CE_CONT, "rootnex_map_regspec: %p -> %p\n", (void *)pa, *vaddrp);
		hp = mp->map_handlep;
		if (hp) {
			hp->ah_pfn = pfn;
			hp->ah_pnum = mmu_btopr(regspec_size + pgoffset);
		}
	}

	return (0);
}


/*
 * rootnex_unmap_regspec()
 *
 */
static int
rootnex_unmap_regspec(ddi_map_req_t *mp, caddr_t *vaddrp)
{
	struct regspec *rp;

	if (mp->map_flags & DDI_MF_DEVICE_MAPPING)
		return (0);

	rp = mp->map_obj.rp;

	uint64_t regspec_size = rp->regspec_size;
	regspec_size |= (((uint64_t)((rp->regspec_bustype >> 16) & 0xfff)) << 32);

	if (regspec_size == 0) {
		cmn_err(CE_WARN, "rootnex_unmap_regspec: zero regspec_size\n");
		return (DDI_ME_INVAL);
	}

	*vaddrp = (caddr_t)0;

	return (0);
}


/*
 * rootnex_map_handle()
 *
 */
static int
rootnex_map_handle(ddi_map_req_t *mp, off_t offset)
{
	ddi_acc_hdl_t *hp;
	uint_t hat_flags;
	register struct regspec *rp;

	hp = mp->map_handlep;
	rp = mp->map_obj.rp;

	uint64_t regspec_addr = rp->regspec_addr;
	regspec_addr |= (((uint64_t)(rp->regspec_bustype & 0xffff)) << 32);

	uint64_t regspec_size = rp->regspec_size;
	regspec_size |= (((uint64_t)((rp->regspec_bustype >> 16) & 0xfff)) << 32);

	if (regspec_size == 0)
		return (DDI_ME_INVAL);

	hp->ah_hat_flags = 0;
	hp->ah_pfn = mmu_btop(regspec_addr + offset);
	hp->ah_pnum = mmu_btopr(regspec_addr + offset + regspec_size) - hp->ah_pfn;
	return (DDI_SUCCESS);
}



/*
 * ************************
 *  interrupt related code
 * ************************
 */

/*
 * rootnex_intr_ops()
 *	bus_intr_op() function for interrupt support
 */
/* ARGSUSED */
static int
rootnex_intr_ops(dev_info_t *pdip, dev_info_t *rdip, ddi_intr_op_t intr_op,
    ddi_intr_handle_impl_t *hdlp, void *result)
{
	struct intrspec			*ispec;
	struct ddi_parent_private_data	*pdp;
	int ret;

	DDI_INTR_NEXDBG((CE_CONT,
	    "rootnex_intr_ops: pdip = %p, rdip = %p, intr_op = %x, hdlp = %p\n",
	    (void *)pdip, (void *)rdip, intr_op, (void *)hdlp));

	switch (intr_op) {
	case DDI_INTROP_GETCAP:
		break;
	case DDI_INTROP_SETCAP:
		break;
	case DDI_INTROP_ALLOC:
		hdlp->ih_pri = 0;
		*(int *)result = hdlp->ih_scratch1;
		break;
	case DDI_INTROP_FREE:
		break;
	case DDI_INTROP_GETPRI:
		*(int *)result = 0;
		break;
	case DDI_INTROP_SETPRI:
		break;
	case DDI_INTROP_ADDISR:
		hdlp->ih_vector = hdlp->ih_inum;
		break;
	case DDI_INTROP_REMISR:
		break;
	case DDI_INTROP_ENABLE:
		ret = add_avintr((void *)hdlp,
				 hdlp->ih_pri,
				 hdlp->ih_cb_func,
				 DEVI(rdip)->devi_name,
				 hdlp->ih_vector,
				 hdlp->ih_cb_arg1,
				 hdlp->ih_cb_arg2,
				 NULL,
				 rdip);
		if (ret == 0) {
			return (DDI_FAILURE);
		}
		break;
	case DDI_INTROP_DISABLE:
		rem_avintr((void *)hdlp,
			   hdlp->ih_pri,
			   hdlp->ih_cb_func,
			   hdlp->ih_vector);
		break;
	case DDI_INTROP_SETMASK:
		break;
	case DDI_INTROP_CLRMASK:
		break;
	case DDI_INTROP_GETPENDING:
		break;
	case DDI_INTROP_NAVAIL:
	case DDI_INTROP_NINTRS:
		*(int *)result = i_ddi_get_intx_nintrs(rdip);
		break;
	case DDI_INTROP_SUPPORTED_TYPES:
		*(int *)result = DDI_INTR_TYPE_FIXED;
		break;
	default:
		return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);
}


/*
 * rootnex_get_ispec()
 *	convert an interrupt number to an interrupt specification.
 *	The interrupt number determines which interrupt spec will be
 *	returned if more than one exists.
 *
 *	Look into the parent private data area of the 'rdip' to find out
 *	the interrupt specification.  First check to make sure there is
 *	one that matchs "inumber" and then return a pointer to it.
 *
 *	Return NULL if one could not be found.
 *
 *	NOTE: This is needed for rootnex_intr_ops()
 */
static struct intrspec *
rootnex_get_ispec(dev_info_t *rdip, int inum)
{
	struct ddi_parent_private_data *pdp = ddi_get_parent_data(rdip);

	/*
	 * Special case handling for drivers that provide their own
	 * intrspec structures instead of relying on the DDI framework.
	 *
	 * A broken hardware driver in ON could potentially provide its
	 * own intrspec structure, instead of relying on the hardware.
	 * If these drivers are children of 'rootnex' then we need to
	 * continue to provide backward compatibility to them here.
	 *
	 * Following check is a special case for 'pcic' driver which
	 * was found to have broken hardwre andby provides its own intrspec.
	 *
	 * Verbatim comments from this driver are shown here:
	 * "Don't use the ddi_add_intr since we don't have a
	 * default intrspec in all cases."
	 *
	 * Since an 'ispec' may not be always created for it,
	 * check for that and create one if so.
	 *
	 * NOTE: Currently 'pcic' is the only driver found to do this.
	 */
	if (!pdp->par_intr && strcmp(ddi_get_name(rdip), "pcic") == 0) {
		pdp->par_nintr = 1;
		pdp->par_intr = kmem_zalloc(sizeof (struct intrspec) *
		    pdp->par_nintr, KM_SLEEP);
	}

	/* Validate the interrupt number */
	if (inum >= pdp->par_nintr)
		return (NULL);

	/* Get the interrupt structure pointer and return that */
	return ((struct intrspec *)&pdp->par_intr[inum]);
}

/*
 * ******************
 *  dma related code
 * ******************
 */
/*
 * rootnex_dma_allochdl()
 *    called from ddi_dma_alloc_handle().
 */
static ddi_dma_impl_t *
rootnex_dma_allocmp(dev_info_t *dip, dev_info_t *rdip, int (*waitfp)(caddr_t), caddr_t arg, uint_t sgllen)
{
	ddi_dma_impl_t *mp;
	rootnex_dma_hdl_t *hp;
	int sleep = (waitfp == DDI_DMA_SLEEP) ? KM_SLEEP : KM_NOSLEEP;
	uint_t max_cookies = sgllen;
	if (max_cookies == 0)
		max_cookies = 1;

	if ((hp = kmem_zalloc(sizeof(rootnex_dma_hdl_t) + sizeof(ddi_dma_cookie_t) * max_cookies, sleep)) == 0) {
		if (waitfp != DDI_DMA_DONTWAIT) {
			ddi_set_callback(waitfp, arg, &rootnex_kmem_clid);
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

static int
rootnex_dma_attr2hdl(ddi_dma_impl_t *mp)
{
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

	align = MAX(align, PAGESIZE) - 1;
	if (count_max > align && (align & nocross) != align) {
		dev_info_t *rdip = mp->dmai_rdip;
		cmn_err(CE_WARN, "%s%d dma_attr_seg not aligned",
		    NAMEINST(rdip));
		return (DDI_DMA_BADATTR);
	}

	count_max = MIN(count_max, hi - lo);

	mp->dmai_minxfer	= attrp->dma_attr_minxfer;
	mp->dmai_burstsizes	= attrp->dma_attr_burstsizes;
	attrp = &mp->dmai_attr;
	SET_DMAATTR(attrp, lo, hi, nocross, count_max);

	return (DDI_SUCCESS);
}

static int
rootnex_dma_allochdl(dev_info_t *dip, dev_info_t *rdip, ddi_dma_attr_t *attr,
    int (*waitfp)(caddr_t), caddr_t arg, ddi_dma_handle_t *handlep)
{
	ddi_dma_impl_t *mp;
	int rval;

	if (attr->dma_attr_version != DMA_ATTR_V0)
		return (DDI_DMA_BADATTR);

	if (!(mp = rootnex_dma_allocmp(dip, rdip, waitfp, arg, attr->dma_attr_sgllen)))
		return (DDI_DMA_NORESOURCES);

	/*
	 * Save requestor's information
	 */
	mp->dmai_attr	= *attr; /* whole object - augmented later  */
	*DEV_ATTR(mp)	= *attr; /* whole object - device orig attr */

	/* check and convert dma attributes to handle parameters */
	if (rval = rootnex_dma_attr2hdl(mp)) {
		rootnex_dma_freehdl(dip, rdip, (ddi_dma_handle_t)mp);
		*handlep = NULL;
		return (rval);
	}
	*handlep = (ddi_dma_handle_t)mp;
	return (DDI_SUCCESS);
}

static void
rootnex_dma_freemp(ddi_dma_impl_t *mp)
{
	ndi_fmc_remove(mp->dmai_rdip, DMA_HANDLE, mp);
	rootnex_dma_hdl_t *hp = (rootnex_dma_hdl_t *)mp;
	kmem_free(mp, sizeof(rootnex_dma_hdl_t) + sizeof(ddi_dma_cookie_t) * hp->max_cookies);
}

/*
 * rootnex_dma_freehdl()
 *    called from ddi_dma_free_handle().
 */
static int
rootnex_dma_freehdl(dev_info_t *dip, dev_info_t *rdip, ddi_dma_handle_t handle)
{
	rootnex_dma_freemp((ddi_dma_impl_t *)handle);

	if (rootnex_kmem_clid) {
		ddi_run_callback(&rootnex_kmem_clid);
	}
	return (DDI_SUCCESS);
}

static int
rootnex_dma_map(ddi_dma_req_t *dmareq, ddi_dma_impl_t *mp)
{
	ddi_dma_obj_t *dobj_p = &dmareq->dmar_object;
	page_t **pplist;
	struct as *as_p;
	uint32_t offset;
	caddr_t vaddr;
	pfn_t pfn0;
	uint_t npages;
	page_t *pp;
	int i;
	uint64_t paddr;
#if 0
	prom_printf("%s(): %d\n", __FUNCTION__, __LINE__);
#endif
	switch (dobj_p->dmao_type) {
	case DMA_OTYP_BUFVADDR:
	case DMA_OTYP_VADDR:
		vaddr = dobj_p->dmao_obj.virt_obj.v_addr;
		offset = (ulong_t)vaddr & PAGEOFFSET;
		pplist = dobj_p->dmao_obj.virt_obj.v_priv;
		as_p = dobj_p->dmao_obj.virt_obj.v_as;
		if (as_p == NULL)
			as_p = &kas;
		break;

	case DMA_OTYP_PAGES:
		offset = dobj_p->dmao_obj.pp_obj.pp_offset;
		pp = dobj_p->dmao_obj.pp_obj.pp_pp;
		ASSERT(PAGE_LOCKED(pp));
		break;

	case DMA_OTYP_PADDR:
		paddr = dobj_p->dmao_obj.phys_obj.p_addr;
		offset = paddr & PAGEOFFSET;
		break;

	default:
		cmn_err(CE_WARN, "%s%d requested unsupported dma type %x",
		    NAMEINST(mp->dmai_rdip), dobj_p->dmao_type);
		return (DDI_DMA_NOMAPPING);
	}

	mp->dmai_object	 = *dobj_p;			/* whole object    */
	mp->dmai_roffset = offset;			/* win0 pg0 offset */
	mp->dmai_ndvmapages = btop(offset + mp->dmai_object.dmao_size);
	mp->dmai_nwin = 1;
	mp->dmai_mapping = 0;

	rootnex_dma_hdl_t *hp = (rootnex_dma_hdl_t *)mp;
	uint64_t left = mp->dmai_object.dmao_size;

	for (i = 0; i < hp->max_cookies && left > 0; i++) {
		size_t map_size = 0;
		switch (dobj_p->dmao_type) {
		case DMA_OTYP_BUFVADDR:
		case DMA_OTYP_VADDR:
			if (pplist) {
				paddr = ptob(page_pptonum(*pplist)) + offset;
				map_size += ((left < (PAGESIZE - offset)) ? left: (PAGESIZE - offset));
				left -= ((left < (PAGESIZE - offset)) ? left: (PAGESIZE - offset));
				offset = 0;
				while (left > 0) {
					pplist++;
					ASSERT(PAGE_LOCKED(*pplist));
					if (ptob(page_pptonum(*pplist)) != paddr + map_size)
						break;
					map_size += ((left < PAGESIZE) ? left: PAGESIZE);
					left -= ((left < PAGESIZE) ? left: PAGESIZE);
				}
			} else {
				paddr = ptob(hat_getpfnum(as_p->a_hat, vaddr)) + offset;
				map_size += ((left < (PAGESIZE - offset)) ? left: (PAGESIZE - offset));
				vaddr += ((left < (PAGESIZE - offset)) ? left: (PAGESIZE - offset));
				left -= ((left < (PAGESIZE - offset)) ? left: (PAGESIZE - offset));
				offset = 0;
				while (left > 0) {
					uint64_t paddr0 = ptob(hat_getpfnum(as_p->a_hat, vaddr));
					if (paddr0 != paddr + map_size)
						break;
					map_size += ((left < PAGESIZE) ? left: PAGESIZE);
					vaddr += ((left < PAGESIZE) ? left: PAGESIZE);
					left -= ((left < PAGESIZE) ? left: PAGESIZE);
				}
			}
			break;

		case DMA_OTYP_PAGES:
			paddr = ptob(page_pptonum(pp)) + offset;
			map_size += ((left < (PAGESIZE - offset)) ? left: (PAGESIZE - offset));
			left -= ((left < (PAGESIZE - offset)) ? left: (PAGESIZE - offset));
			offset = 0;
			while (left > 0) {
				pp = pp->p_next;
				ASSERT(PAGE_LOCKED(pp));
				if (ptob(page_pptonum(pp)) != paddr + map_size)
					break;
				map_size += ((left < PAGESIZE) ? left: PAGESIZE);
				left -= ((left < PAGESIZE) ? left: PAGESIZE);
			}
			break;

		case DMA_OTYP_PADDR:
			map_size += left;
			left -= map_size;
			break;
		}
		MAKE_DMA_COOKIE(hp->cookies + i, paddr, map_size);
		size_t line_size = CTR_TO_DATA_LINESIZE(read_ctr_el0());
		for (uintptr_t addr = ((paddr + SEGKPM_BASE) & ~(line_size - 1)); addr < paddr + SEGKPM_BASE + map_size; addr += line_size) {
			flush_data_cache(addr);
		}
	}

	if (left != 0) {
		return (DDI_DMA_NORESOURCES);
	}
	hp->ncookies = i;

	return (DDI_SUCCESS);
}

/*
 * rootnex_dma_bindhdl()
 *    called from ddi_dma_addr_bind_handle() and ddi_dma_buf_bind_handle().
 */
static int
rootnex_dma_bindhdl(dev_info_t *dip, dev_info_t *rdip,
    ddi_dma_handle_t handle, struct ddi_dma_req *dmareq,
    ddi_dma_cookie_t *cookiep, uint_t *ccountp)
{
	ddi_dma_impl_t *mp = (ddi_dma_impl_t *)handle;
	rootnex_dma_hdl_t *hp = (rootnex_dma_hdl_t *)mp;
	int ret;

	if (ret = rootnex_dma_map(dmareq, mp)) {
		return (ret);
	}
	*cookiep = hp->cookies[0];
	*ccountp = hp->ncookies;
	mp->dmai_cookie = &hp->cookies[0];
	mp->dmai_cookie++;

	if (mp->dmai_attr.dma_attr_flags & DDI_DMA_FLAGERR)
		mp->dmai_error.err_cf = impl_dma_check;

	if (mp->dmai_nwin != 1) {
		return DDI_DMA_PARTIAL_MAP;
	}
	return DDI_DMA_MAPPED;
}

static void
rootnex_dma_unmap(ddi_dma_impl_t *mp)
{
	rootnex_dma_hdl_t *hp = (rootnex_dma_hdl_t *)mp;
	mp->dmai_mapping = 0;
	mp->dmai_ndvmapages = 0;
	mp->dmai_window_index = -1;
	mp->dmai_pfn0 = 0;
	mp->dmai_roffset = 0;
	hp->ncookies = 0;
}

/*
 * rootnex_dma_unbindhdl()
 *    called from ddi_dma_unbind_handle()
 */
/*ARGSUSED*/
static int
rootnex_dma_unbindhdl(dev_info_t *dip, dev_info_t *rdip,
    ddi_dma_handle_t handle)
{
	ddi_dma_impl_t *mp = (ddi_dma_impl_t *)handle;

	rootnex_dma_unmap(mp);

	if (rootnex_kmem_clid) {
		ddi_run_callback(&rootnex_kmem_clid);
	}
	mp->dmai_error.err_cf = NULL;
	return (DDI_SUCCESS);
}

/*
 * rootnex_dma_sync()
 *    called from ddi_dma_sync() if DMP_NOSYNC is not set in hp->dmai_rflags.
 *    We set DMP_NOSYNC if we're not using the copy buffer. If DMP_NOSYNC
 *    is set, ddi_dma_sync() returns immediately passing back success.
 */
/*ARGSUSED*/
static int
rootnex_dma_sync(dev_info_t *dip, dev_info_t *rdip, ddi_dma_handle_t handle,
    off_t off, size_t len, uint_t cache_flags)
{
	ddi_dma_impl_t *mp = (ddi_dma_impl_t *)handle;
	rootnex_dma_hdl_t *hp = (rootnex_dma_hdl_t *)mp;

	for (int i = 0; i < hp->ncookies; i++) {
		uintptr_t paddr = hp->cookies[i].dmac_laddress;
		size_t map_size = hp->cookies[i].dmac_size;
		size_t line_size = CTR_TO_DATA_LINESIZE(read_ctr_el0());
		for (uintptr_t addr = ((paddr + SEGKPM_BASE) & ~(line_size - 1)); addr < paddr + SEGKPM_BASE + map_size; addr += line_size) {
			flush_data_cache(addr);
		}
	}
	return (DDI_SUCCESS);
}

/*ARGSUSED*/
static int
rootnex_dma_win(dev_info_t *dip, dev_info_t *rdip, ddi_dma_handle_t handle,
    uint_t win, off_t *offp, size_t *lenp, ddi_dma_cookie_t *cookiep,
    uint_t *ccountp)
{
	ddi_dma_impl_t *mp = (ddi_dma_impl_t *)handle;
	rootnex_dma_hdl_t *hp = (rootnex_dma_hdl_t *)mp;

	if (win >= mp->dmai_nwin) {
		return (DDI_FAILURE);
	}

	if (cookiep)
		*cookiep = hp->cookies[0];
	if (ccountp)
		*ccountp = hp->ncookies;
	if (lenp)
		*lenp = mp->dmai_object.dmao_size;
	if (offp)
		*offp = 0;
	mp->dmai_cookie = &hp->cookies[0];
	mp->dmai_cookie++;

	return (DDI_SUCCESS);
}

/*
 * ************************
 *  obsoleted dma routines
 * ************************
 */

/*
 * rootnex_dma_mctl()
 *
 * We don't support this legacy interface any more on x86.
 */
/* ARGSUSED */
static int
rootnex_dma_mctl(dev_info_t *dip, dev_info_t *rdip, ddi_dma_handle_t handle,
    enum ddi_dma_ctlops request, off_t *offp, size_t *lenp, caddr_t *objpp,
    uint_t cache_flags)
{
	/*
	 * The only thing dma_mctl is usef for anymore is legacy SPARC
	 * dvma and sbus-specific routines.
	 */
	return (DDI_FAILURE);
}

/*
 * *********
 *  FMA Code
 * *********
 */

/*
 * rootnex_fm_init()
 *    FMA init busop
 */
/* ARGSUSED */
static int
rootnex_fm_init(dev_info_t *dip, dev_info_t *tdip, int tcap,
    ddi_iblock_cookie_t *ibc)
{
	*ibc = rootnex_state->r_err_ibc;

	return (ddi_system_fmcap);
}

/*ARGSUSED*/
static int
rootnex_quiesce(dev_info_t *dip)
{
	return (DDI_SUCCESS);
}
