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
 * Copyright (c) 1992, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * x86 root nexus driver
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
#include <sys/psm.h>
#include <sys/ontrap.h>
#include <sys/atomic.h>
#include <sys/sdt.h>
#include <sys/rootnex.h>
#include <vm/hat_i86.h>
#include <sys/ddifm.h>
#include <sys/ddi_isa.h>
#include <sys/apic.h>
#include <sys/io_mvec.h>
#include <sys/io_tag.h>
#include <sys/x86_archext.h>

#ifdef __xpv
#include <sys/bootinfo.h>
#include <sys/hypervisor.h>
#include <sys/bootconf.h>
#include <vm/kboot_mmu.h>
#endif

#if !defined(__xpv)
#include <sys/iommu.h>
#endif

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
	{ "#address-cells",		3 },
	{ "#size-cells",		2 },
};
#define	NROOT_INTPROPS	(sizeof (rootnex_intprp) / sizeof (rootnex_intprop_t))

#if !defined(__xpv)
char _depends_on[] = "misc/acpica";
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
static int rootnex_dma_map(dev_info_t *dip, dev_info_t *rdip,
    struct ddi_dma_req *dmareq, ddi_dma_handle_t *handlep);
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
static int rootnex_alloc_intr_fixed(dev_info_t *, ddi_intr_handle_impl_t *,
    void *);
static int rootnex_free_intr_fixed(dev_info_t *, ddi_intr_handle_impl_t *);

static struct bus_ops rootnex_bus_ops = {
	BUSO_REV,
	rootnex_map,
	NULL,
	NULL,
	NULL,
	rootnex_map_fault,
	rootnex_dma_map,
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
	"i86pc root nexus",
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
extern void i86_pp_map(page_t *pp, caddr_t kaddr);
extern void i86_va_map(caddr_t vaddr, struct as *asp, caddr_t kaddr);
extern int (*psm_intr_ops)(dev_info_t *, ddi_intr_handle_impl_t *,
    psm_intr_op_t, int *);
extern int impl_ddi_sunbus_initchild(dev_info_t *dip);
extern void impl_ddi_sunbus_removechild(dev_info_t *dip);
extern void io_parse_dmarobj(ddi_dma_obj_t *dmar_object, uint_t iovcnt,
    struct as **asp, caddr_t *vaddr, uint32_t *size, page_t ***pplist,
    page_t **pp, uint64_t *offset);
extern int io_get_page_cnt(ddi_dma_obj_t *dmar_object, offset_t offset,
    size_t size);


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
static int rootnex_map_handle(ddi_map_req_t *mp);
static void rootnex_clean_dmahdl(ddi_dma_impl_t *hp);
static int rootnex_valid_alloc_parms(ddi_dma_attr_t *attr, uint_t maxsegsize);
static int rootnex_valid_bind_parms(ddi_dma_req_t *dmareq,
    ddi_dma_attr_t *attr);
static void rootnex_get_sgl(ddi_dma_obj_t *dmar_object, ddi_dma_cookie_t *sgl,
    rootnex_sglinfo_t *sglinfo);
static int rootnex_bind_slowpath(ddi_dma_impl_t *hp, struct ddi_dma_req *dmareq,
    rootnex_dma_t *dma, ddi_dma_attr_t *attr, ddi_dma_obj_t *dmao, int kmflag);
static int rootnex_setup_copybuf(ddi_dma_impl_t *hp, struct ddi_dma_req *dmareq,
    rootnex_dma_t *dma, ddi_dma_attr_t *attr);
static void rootnex_teardown_copybuf(rootnex_dma_t *dma);
static int rootnex_setup_windows(ddi_dma_impl_t *hp, rootnex_dma_t *dma,
    ddi_dma_attr_t *attr, ddi_dma_obj_t *dmao, int kmflag);
static void rootnex_teardown_windows(rootnex_dma_t *dma);
static void rootnex_init_win(ddi_dma_impl_t *hp, rootnex_dma_t *dma,
    rootnex_window_t *window, ddi_dma_cookie_t *cookie, off_t cur_offset);
static int rootnex_setup_cookie(rootnex_dma_t *dma, int kmflags,
    ddi_dma_cookie_t *cookie, off_t cur_offset, size_t *copybuf_used,
    page_t **cur_pp);
static int rootnex_sgllen_window_boundary(ddi_dma_impl_t *hp,
    rootnex_dma_t *dma, rootnex_window_t **windowp, ddi_dma_cookie_t *cookie,
    ddi_dma_attr_t *attr, off_t cur_offset);
static int rootnex_copybuf_window_boundary(ddi_dma_impl_t *hp,
    rootnex_dma_t *dma, rootnex_window_t **windowp,
    ddi_dma_cookie_t *cookie, off_t cur_offset, size_t *copybuf_used);
static int rootnex_maxxfer_window_boundary(ddi_dma_impl_t *hp,
    rootnex_dma_t *dma, rootnex_window_t **windowp, ddi_dma_cookie_t *cookie);
static int rootnex_valid_sync_parms(ddi_dma_impl_t *hp, rootnex_window_t *win,
    off_t offset, size_t size, uint_t cache_flags);
static int rootnex_verify_buffer(rootnex_dma_t *dma);
static int rootnex_dma_check(dev_info_t *dip, const void *handle,
    const void *comp_addr, const void *not_used);
static boolean_t rootnex_need_bounce_seg(ddi_dma_obj_t *dmar_object,
    rootnex_sglinfo_t *sglinfo);
static struct as *rootnex_get_as(ddi_dma_obj_t *dmar_object);

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


extern boolean_t pm_cache_disabled;

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
#if !defined(__xpv)
		if (iommu_ops != NULL)
			iommu_ops->ioo_unquiesce_units();
#endif
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
	rootnex_state->r_reserved_msg_printed = B_FALSE;
#ifdef DEBUG
	rootnex_cnt = &rootnex_state->r_counters[0];
#endif

	/*
	 * Set minimum fm capability level for i86pc platforms and then
	 * initialize error handling. Since we're the rootnex, we don't
	 * care what's returned in the fmcap field.
	 */
	ddi_system_fmcap = DDI_FM_EREPORT_CAPABLE | DDI_FM_ERRCB_CAPABLE |
	    DDI_FM_ACCCHK_CAPABLE | DDI_FM_DMACHK_CAPABLE;
	fmcap = ddi_system_fmcap;
	ddi_fm_init(dip, &fmcap, &rootnex_state->r_err_ibc);

	/* initialize DMA related state */
	e = rootnex_dma_init();
	if (e != DDI_SUCCESS) {
		kmem_free(rootnex_state, sizeof (rootnex_state_t));
		return (DDI_FAILURE);
	}

	/* Add static root node properties */
	rootnex_add_props(dip);

	/* since we can't call ddi_report_dev() */
	cmn_err(CE_CONT, "?root nexus = %s\n", ddi_get_name(dip));

	/* Initialize rootnex event handle */
	i_ddi_rootnex_init_events(dip);

#if !defined(__xpv)

	pm_cache_disabled = B_FALSE;
	/*
	 * If x86 IOMMU has limit, then set pm_enforce_dvma_limit here
	 */

#endif
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
#if !defined(__xpv)
		if (iommu_ops != NULL)
			iommu_ops->ioo_quiesce_units(0);
#endif
		return (DDI_SUCCESS);
	default:
		return (DDI_FAILURE);
	}
	/*NOTREACHED*/

}


/*
 * rootnex_dma_init()
 *
 */
/*ARGSUSED*/
static int
rootnex_dma_init()
{
	size_t bufsize;


	/*
	 * size of our cookie/window/copybuf state needed in dma bind that we
	 * pre-alloc in dma_alloc_handle
	 */
	rootnex_state->r_prealloc_cookies = rootnex_prealloc_cookies;
	rootnex_state->r_prealloc_size =
	    (rootnex_state->r_prealloc_cookies * sizeof (ddi_dma_cookie_t)) +
	    (rootnex_prealloc_windows * sizeof (rootnex_window_t)) +
	    (rootnex_prealloc_copybuf * sizeof (rootnex_pgmap_t));

	/*
	 * setup DDI DMA handle kmem cache, align each handle on 64 bytes,
	 * allocate 16 extra bytes for struct pointer alignment
	 * (p->dmai_private & dma->dp_prealloc_buffer)
	 */
	bufsize = sizeof (ddi_dma_impl_t) + sizeof (rootnex_dma_t) +
	    rootnex_state->r_prealloc_size + 0x10;
	rootnex_state->r_dmahdl_cache = kmem_cache_create("rootnex_dmahdl",
	    bufsize, 64, NULL, NULL, NULL, NULL, NULL, 0);
	if (rootnex_state->r_dmahdl_cache == NULL) {
		return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);
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

	/* Make sure that we get called for suspend / resume */
	(void) ddi_prop_update_string(DDI_DEV_T_NONE, dip,
	    "pm-hardware-state", "needs-suspend-resume");
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
		if (!rootnex_state->r_reserved_msg_printed) {
			rootnex_state->r_reserved_msg_printed = B_TRUE;
			cmn_err(CE_WARN, "Failing ddi_ctlops call(s) for "
			    "1 or more reserved/obsolete operations.");
		}
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
		*size = REGSPEC_SIZE64(&(pdp->par_reg[n]));
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

		switch (rp->regspec_bustype) {

		case BTEISA:
			f_len += snprintf(buf + len, REPORTDEV_BUFSIZE - len,
			    "%s 0x%"PRIx64, DEVI_EISA_NEXNAME,
			    REGSPEC_ADDR64(rp));
			break;

		case BTISA:
			f_len += snprintf(buf + len, REPORTDEV_BUFSIZE - len,
			    "%s 0x%"PRIx64, DEVI_ISA_NEXNAME,
			    REGSPEC_ADDR64(rp));
			break;

		default:
			f_len += snprintf(buf + len, REPORTDEV_BUFSIZE - len,
			    "space %x offset %"PRIx64,
			    rp->regspec_bustype, REGSPEC_ADDR64(rp));
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
	ddi_map_req_t mr = *mp;		/* Get private copy of request */
	int error;

	mp = &mr;

	switch (mp->map_op)  {
	case DDI_MO_MAP_LOCKED:
	case DDI_MO_UNMAP:
	case DDI_MO_MAP_HANDLE:
		break;
	default:
#ifdef	DDI_MAP_DEBUG
		cmn_err(CE_WARN, "rootnex_map: unimplemented map op %d.",
		    mp->map_op);
#endif	/* DDI_MAP_DEBUG */
		return (DDI_ME_UNIMPLEMENTED);
	}

	if (mp->map_flags & DDI_MF_USER_MAPPING)  {
#ifdef	DDI_MAP_DEBUG
		cmn_err(CE_WARN, "rootnex_map: unimplemented map type: user.");
#endif	/* DDI_MAP_DEBUG */
		return (DDI_ME_UNIMPLEMENTED);
	}

	/*
	 * First, if given an rnumber, convert it to a regspec...
	 * (Presumably, this is on behalf of a child of the root node?)
	 */

	if (mp->map_type == DDI_MT_RNUMBER)  {

		int rnumber = mp->map_obj.rnumber;
#ifdef	DDI_MAP_DEBUG
		static char *out_of_range =
		    "rootnex_map: Out of range rnumber <%d>, device <%s>";
#endif	/* DDI_MAP_DEBUG */

		rp = i_ddi_rnumber_to_regspec(rdip, rnumber);
		if (rp == NULL)  {
#ifdef	DDI_MAP_DEBUG
			cmn_err(CE_WARN, out_of_range, rnumber,
			    ddi_get_name(rdip));
#endif	/* DDI_MAP_DEBUG */
			return (DDI_ME_RNUMBER_RANGE);
		}

		/*
		 * Convert the given ddi_map_req_t from rnumber to regspec...
		 */

		mp->map_type = DDI_MT_REGSPEC;
		mp->map_obj.rp = rp;
	}

	/*
	 * Adjust offset and length correspnding to called values...
	 * XXX: A non-zero length means override the one in the regspec
	 * XXX: (regardless of what's in the parent's range?)
	 */

	tmp_reg = *(mp->map_obj.rp);		/* Preserve underlying data */
	rp = mp->map_obj.rp = &tmp_reg;		/* Use tmp_reg in request */

#ifdef	DDI_MAP_DEBUG
	cmn_err(CE_CONT, "rootnex: <%s,%s> <0x%x, 0x%"PRIx64", 0x%"PRIx64">"
	    " offset %d len %d handle 0x%x\n", ddi_get_name(dip),
	    ddi_get_name(rdip), rp->regspec_bustype,
	    REGSPEC_ADDR64(rp), REGSPEC_SIZE64(rp),
	    offset, len, mp->map_handlep);
#endif	/* DDI_MAP_DEBUG */

	/*
	 * I/O or memory mapping:
	 *
	 *	<bustype=0, addr=x, len=x>: memory
	 *	<bustype=1, addr=x, len=x>: i/o
	 *	<bustype>1, addr=0, len=x>: x86-compatibility i/o
	 */

	if (rp->regspec_bustype > 1 && rp->regspec_addr_low != 0) {
		cmn_err(CE_WARN, "<%s,%s> invalid register spec"
		    " <0x%x, 0x%"PRIx64", 0x%"PRIx64">", ddi_get_name(dip),
		    ddi_get_name(rdip), rp->regspec_bustype,
		    REGSPEC_ADDR64(rp), REGSPEC_SIZE64(rp));
		return (DDI_ME_INVAL);
	}

	if (rp->regspec_bustype > 1 && REGSPEC_ADDR64(rp) == 0) {
		/*
		 * compatibility i/o mapping
		 */
		rp->regspec_bustype += (uint_t)offset;
	} else {
		/*
		 * Normal memory or i/o mapping
		 */
		uint64_t tmp64 = REGSPEC_ADDR64(rp);
		tmp64 += (ulong_t)offset;
		SPLIT_REGSPEC_ADDR(tmp64, rp);
	}

	if (len != 0)
		SPLIT_REGSPEC_SIZE(len, rp);

#ifdef	DDI_MAP_DEBUG
	cmn_err(CE_CONT, "             <%s,%s> <0x%x, 0x%"PRIx64", "
	    "0x%"PRIx64"> offset %d len %d handle 0x%x\n",
	    ddi_get_name(dip), ddi_get_name(rdip), rp->regspec_bustype,
	    REGSPEC_ADDR64(rp), REGSPEC_SIZE64(rp),
	    offset, len, mp->map_handlep);
#endif	/* DDI_MAP_DEBUG */

	/*
	 * Apply any parent ranges at this level, if applicable.
	 * (This is where nexus specific regspec translation takes place.
	 * Use of this function is implicit agreement that translation is
	 * provided via ddi_apply_range.)
	 */

#ifdef	DDI_MAP_DEBUG
	ddi_map_debug("applying range of parent <%s> to child <%s>...\n",
	    ddi_get_name(dip), ddi_get_name(rdip));
#endif	/* DDI_MAP_DEBUG */

	if ((error = i_ddi_apply_range(dip, rdip, mp->map_obj.rp)) != 0)
		return (error);

	/*
	 * Check memory address against number of PA bits supported by CPU.
	 */
	if (rp->regspec_bustype == 0) {
		uint64_t tmp64 = REGSPEC_ADDR64(rp);
		uint_t pa_bits;

		cpuid_get_addrsize(CPU, &pa_bits, NULL);
		if (pa_bits > 0 && pa_bits < 64)
			if ((tmp64 >> pa_bits) != 0)
				return (DDI_ME_REGSPEC_RANGE);
	}

	switch (mp->map_op)  {
	case DDI_MO_MAP_LOCKED:

		/*
		 * Set up the locked down kernel mapping to the regspec...
		 */

		return (rootnex_map_regspec(mp, vaddrp));

	case DDI_MO_UNMAP:

		/*
		 * Release mapping...
		 */

		return (rootnex_unmap_regspec(mp, vaddrp));

	case DDI_MO_MAP_HANDLE:

		return (rootnex_map_handle(mp));

	default:
		return (DDI_ME_UNIMPLEMENTED);
	}
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
			ASSERT(AS_READ_HELD(seg->s_as, &seg->s_as->a_lock));
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
	paddr_t rbase;
	void *cvaddr;
	uint_t npages, pgoffset;
	struct regspec *rp;
	ddi_acc_hdl_t *hp;
	ddi_acc_impl_t *ap;
	uint_t	hat_acc_flags;
	paddr_t pbase;
	uint64_t rp_addr, rp_size;

	rp = mp->map_obj.rp;
	hp = mp->map_handlep;

	rp_addr = REGSPEC_ADDR64(rp);
	rp_size = REGSPEC_SIZE64(rp);
#ifdef	DDI_MAP_DEBUG
	ddi_map_debug(
	    "rootnex_map_regspec: <0x%x 0x%"PRIx64" 0x%"PRIx64"> handle 0x%x\n",
	    rp->regspec_bustype, rp_addr, rp_size, mp->map_handlep);
#endif	/* DDI_MAP_DEBUG */

	/*
	 * I/O or memory mapping
	 *
	 *	<bustype=0, addr=x, len=x>: memory
	 *	<bustype=1, addr=x, len=x>: i/o
	 *	<bustype>1, addr=0, len=x>: x86-compatibility i/o
	 */

	if (rp->regspec_bustype > 1 && rp_addr != 0) {
		cmn_err(CE_WARN, "rootnex: invalid register spec"
		    " <0x%x, 0x%"PRIx64", 0x%"PRIx64">",
		    rp->regspec_bustype, rp_addr, rp_size);
		return (DDI_FAILURE);
	}

	if (rp->regspec_bustype != 0) {
		/*
		 * I/O space - needs a handle.
		 */
		if (hp == NULL) {
			return (DDI_FAILURE);
		}
		ap = (ddi_acc_impl_t *)hp->ah_platform_private;
		ap->ahi_acc_attr |= DDI_ACCATTR_IO_SPACE;
		impl_acc_hdl_init(hp);

		if (mp->map_flags & DDI_MF_DEVICE_MAPPING) {
#ifdef  DDI_MAP_DEBUG
			ddi_map_debug("rootnex_map_regspec: mmap() "
			    "to I/O space is not supported.\n");
#endif  /* DDI_MAP_DEBUG */
			return (DDI_ME_INVAL);
		} else {
			/*
			 * 1275-compliant vs. compatibility i/o mapping
			 */
			*vaddrp =
			    (rp->regspec_bustype > 1 && rp_addr == 0) ?
			    ((caddr_t)(uintptr_t)rp->regspec_bustype) :
			    ((caddr_t)(uintptr_t)rp_addr);
			hp->ah_pfn = mmu_btop((ulong_t)rp_addr &
			    MMU_PAGEMASK);
			hp->ah_pnum = mmu_btopr(rp_size +
			    (ulong_t)rp_addr & MMU_PAGEOFFSET);
		}

#ifdef	DDI_MAP_DEBUG
		ddi_map_debug("rootnex_map_regspec: \"Mapping\" %"PRId64
		    " bytes I/O space at 0x%x\n", rp_size, *vaddrp);
#endif	/* DDI_MAP_DEBUG */
		return (DDI_SUCCESS);
	}

	/*
	 * Memory space
	 */

	if (hp != NULL) {
		/*
		 * hat layer ignores
		 * hp->ah_acc.devacc_attr_endian_flags.
		 */
		switch (hp->ah_acc.devacc_attr_dataorder) {
		case DDI_STRICTORDER_ACC:
			hat_acc_flags = HAT_STRICTORDER;
			break;
		case DDI_UNORDERED_OK_ACC:
			hat_acc_flags = HAT_UNORDERED_OK;
			break;
		case DDI_MERGING_OK_ACC:
			hat_acc_flags = HAT_MERGING_OK;
			break;
		case DDI_LOADCACHING_OK_ACC:
			hat_acc_flags = HAT_LOADCACHING_OK;
			break;
		case DDI_STORECACHING_OK_ACC:
			hat_acc_flags = HAT_STORECACHING_OK;
			break;
		}
		ap = (ddi_acc_impl_t *)hp->ah_platform_private;
		ap->ahi_acc_attr |= DDI_ACCATTR_CPU_VADDR;
		impl_acc_hdl_init(hp);
		hp->ah_hat_flags = hat_acc_flags;
	} else {
		hat_acc_flags = HAT_STRICTORDER;
	}

	rbase = (paddr_t)(rp_addr & MMU_PAGEMASK);
	pbase = rbase;
	pgoffset = (ulong_t)rp_addr & MMU_PAGEOFFSET;

	if (rp_size == 0) {
#ifdef  DDI_MAP_DEBUG
		ddi_map_debug("rootnex_map_regspec: zero regspec_size\n");
#endif  /* DDI_MAP_DEBUG */
		return (DDI_ME_INVAL);
	}

	if (mp->map_flags & DDI_MF_DEVICE_MAPPING) {
		/* extra cast to make gcc happy */
		*vaddrp = (caddr_t)((uintptr_t)mmu_btop(pbase));
	} else {
		npages = mmu_btopr(rp_size + pgoffset);

#ifdef	DDI_MAP_DEBUG
		ddi_map_debug("rootnex_map_regspec: Mapping %d pages "
		    "physical %llx", npages, pbase);
#endif	/* DDI_MAP_DEBUG */

		cvaddr = device_arena_alloc(ptob(npages), VM_NOSLEEP);
		if (cvaddr == NULL)
			return (DDI_ME_NORESOURCES);

		/*
		 * Now map in the pages we've allocated...
		 */
		hat_devload(kas.a_hat, cvaddr, mmu_ptob(npages),
		    mmu_btop(pbase), mp->map_prot | hat_acc_flags,
		    HAT_LOAD_LOCK);
		*vaddrp = (caddr_t)cvaddr + pgoffset;

		/* save away pfn and npages for FMA */
		hp = mp->map_handlep;
		if (hp) {
			hp->ah_pfn = mmu_btop(pbase);
			hp->ah_pnum = npages;
		}
	}

#ifdef	DDI_MAP_DEBUG
	ddi_map_debug("at virtual 0x%x\n", *vaddrp);
#endif	/* DDI_MAP_DEBUG */
	return (DDI_SUCCESS);
}


/*
 * rootnex_unmap_regspec()
 *
 */
static int
rootnex_unmap_regspec(ddi_map_req_t *mp, caddr_t *vaddrp)
{
	caddr_t addr = (caddr_t)*vaddrp;
	uint_t npages, pgoffset;
	struct regspec *rp;
	uint64_t rp_size;

	if (mp->map_flags & DDI_MF_DEVICE_MAPPING)
		return (0);

	rp = mp->map_obj.rp;
	rp_size = REGSPEC_SIZE64(rp);

	if (rp_size == 0) {
#ifdef  DDI_MAP_DEBUG
		ddi_map_debug("rootnex_unmap_regspec: zero regspec_size\n");
#endif  /* DDI_MAP_DEBUG */
		return (DDI_ME_INVAL);
	}

	/*
	 * I/O or memory mapping:
	 *
	 *	<bustype=0, addr=x, len=x>: memory
	 *	<bustype=1, addr=x, len=x>: i/o
	 *	<bustype>1, addr=0, len=x>: x86-compatibility i/o
	 */
	if (rp->regspec_bustype != 0) {
		/*
		 * This is I/O space, which requires no particular
		 * processing on unmap since it isn't mapped in the
		 * first place.
		 */
		return (DDI_SUCCESS);
	}

	/*
	 * Memory space
	 */
	pgoffset = (uintptr_t)addr & MMU_PAGEOFFSET;
	npages = mmu_btopr(rp_size + pgoffset);
	hat_unload(kas.a_hat, addr - pgoffset, ptob(npages), HAT_UNLOAD_UNLOCK);
	device_arena_free(addr - pgoffset, ptob(npages));

	/*
	 * Destroy the pointer - the mapping has logically gone
	 */
	*vaddrp = NULL;

	return (DDI_SUCCESS);
}


/*
 * rootnex_map_handle()
 *
 */
static int
rootnex_map_handle(ddi_map_req_t *mp)
{
	paddr_t rbase;
	ddi_acc_hdl_t *hp;
	uint_t pgoffset;
	struct regspec *rp;
	paddr_t pbase;
	uint64_t rp_addr, rp_size;

	rp = mp->map_obj.rp;

	rp_addr = REGSPEC_ADDR64(rp);
	rp_size = REGSPEC_SIZE64(rp);
#ifdef	DDI_MAP_DEBUG
	ddi_map_debug(
	    "rootnex_map_handle: <0x%x 0x%"PRIx64" 0x%"PRIx64"> handle 0x%x\n",
	    rp->regspec_bustype, rp_addr, rp_size, mp->map_handlep);
#endif	/* DDI_MAP_DEBUG */

	/*
	 * I/O or memory mapping:
	 *
	 *	<bustype=0, addr=x, len=x>: memory
	 *	<bustype=1, addr=x, len=x>: i/o
	 *	<bustype>1, addr=0, len=x>: x86-compatibility i/o
	 */
	if (rp->regspec_bustype != 0) {
		/*
		 * This refers to I/O space, and we don't support "mapping"
		 * I/O space to a user.
		 */
		return (DDI_FAILURE);
	}

	/*
	 * Set up the hat_flags for the mapping.
	 */
	hp = mp->map_handlep;

	switch (hp->ah_acc.devacc_attr_endian_flags) {
	case DDI_NEVERSWAP_ACC:
		hp->ah_hat_flags = HAT_NEVERSWAP | HAT_STRICTORDER;
		break;
	case DDI_STRUCTURE_LE_ACC:
		hp->ah_hat_flags = HAT_STRUCTURE_LE;
		break;
	case DDI_STRUCTURE_BE_ACC:
		return (DDI_FAILURE);
	default:
		return (DDI_REGS_ACC_CONFLICT);
	}

	switch (hp->ah_acc.devacc_attr_dataorder) {
	case DDI_STRICTORDER_ACC:
		break;
	case DDI_UNORDERED_OK_ACC:
		hp->ah_hat_flags |= HAT_UNORDERED_OK;
		break;
	case DDI_MERGING_OK_ACC:
		hp->ah_hat_flags |= HAT_MERGING_OK;
		break;
	case DDI_LOADCACHING_OK_ACC:
		hp->ah_hat_flags |= HAT_LOADCACHING_OK;
		break;
	case DDI_STORECACHING_OK_ACC:
		hp->ah_hat_flags |= HAT_STORECACHING_OK;
		break;
	default:
		return (DDI_FAILURE);
	}

	rbase = (paddr_t)rp_addr &
	    (~(paddr_t)MMU_PAGEOFFSET);
	pgoffset = (ulong_t)rp_addr & MMU_PAGEOFFSET;

	if (rp_size == 0)
		return (DDI_ME_INVAL);

	pbase = rbase;

	hp->ah_pfn = mmu_btop(pbase);
	hp->ah_pnum = mmu_btopr(rp_size + pgoffset);

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

	DDI_INTR_NEXDBG((CE_CONT,
	    "rootnex_intr_ops: pdip = %p, rdip = %p, intr_op = %x, hdlp = %p\n",
	    (void *)pdip, (void *)rdip, intr_op, (void *)hdlp));

	/* Process the interrupt operation */
	switch (intr_op) {
	case DDI_INTROP_GETCAP:
		/* First check with pcplusmp */
		if (psm_intr_ops == NULL)
			return (DDI_FAILURE);

		if ((*psm_intr_ops)(rdip, hdlp, PSM_INTR_OP_GET_CAP, result)) {
			*(int *)result = 0;
			return (DDI_FAILURE);
		}
		break;
	case DDI_INTROP_SETCAP:
		if (psm_intr_ops == NULL)
			return (DDI_FAILURE);

		if ((*psm_intr_ops)(rdip, hdlp, PSM_INTR_OP_SET_CAP, result))
			return (DDI_FAILURE);
		break;
	case DDI_INTROP_ALLOC:
		ASSERT(hdlp->ih_type == DDI_INTR_TYPE_FIXED);
		return (rootnex_alloc_intr_fixed(rdip, hdlp, result));
	case DDI_INTROP_FREE:
		ASSERT(hdlp->ih_type == DDI_INTR_TYPE_FIXED);
		return (rootnex_free_intr_fixed(rdip, hdlp));
	case DDI_INTROP_GETPRI:
		if ((ispec = rootnex_get_ispec(rdip, hdlp->ih_inum)) == NULL)
			return (DDI_FAILURE);
		*(int *)result = ispec->intrspec_pri;
		break;
	case DDI_INTROP_SETPRI:
		/* Validate the interrupt priority passed to us */
		if (*(int *)result > LOCK_LEVEL)
			return (DDI_FAILURE);

		/* Ensure that PSM is all initialized and ispec is ok */
		if ((psm_intr_ops == NULL) ||
		    ((ispec = rootnex_get_ispec(rdip, hdlp->ih_inum)) == NULL))
			return (DDI_FAILURE);

		/* Change the priority */
		if ((*psm_intr_ops)(rdip, hdlp, PSM_INTR_OP_SET_PRI, result) ==
		    PSM_FAILURE)
			return (DDI_FAILURE);

		/* update the ispec with the new priority */
		ispec->intrspec_pri =  *(int *)result;
		break;
	case DDI_INTROP_ADDISR:
		if ((ispec = rootnex_get_ispec(rdip, hdlp->ih_inum)) == NULL)
			return (DDI_FAILURE);
		ispec->intrspec_func = hdlp->ih_cb_func;
		break;
	case DDI_INTROP_REMISR:
		if ((ispec = rootnex_get_ispec(rdip, hdlp->ih_inum)) == NULL)
			return (DDI_FAILURE);
		ispec->intrspec_func = (uint_t (*)()) 0;
		break;
	case DDI_INTROP_ENABLE:
		if ((ispec = rootnex_get_ispec(rdip, hdlp->ih_inum)) == NULL)
			return (DDI_FAILURE);

		/* Call psmi to translate irq with the dip */
		if (psm_intr_ops == NULL)
			return (DDI_FAILURE);

		((ihdl_plat_t *)hdlp->ih_private)->ip_ispecp = ispec;
		if ((*psm_intr_ops)(rdip, hdlp, PSM_INTR_OP_XLATE_VECTOR,
		    (int *)&hdlp->ih_vector) == PSM_FAILURE)
			return (DDI_FAILURE);

		/* Add the interrupt handler */
		if (!add_avintr((void *)hdlp, ispec->intrspec_pri,
		    hdlp->ih_cb_func, DEVI(rdip)->devi_name, hdlp->ih_vector,
		    hdlp->ih_cb_arg1, hdlp->ih_cb_arg2, NULL, rdip))
			return (DDI_FAILURE);
		break;
	case DDI_INTROP_DISABLE:
		if ((ispec = rootnex_get_ispec(rdip, hdlp->ih_inum)) == NULL)
			return (DDI_FAILURE);

		/* Call psm_ops() to translate irq with the dip */
		if (psm_intr_ops == NULL)
			return (DDI_FAILURE);

		((ihdl_plat_t *)hdlp->ih_private)->ip_ispecp = ispec;
		(void) (*psm_intr_ops)(rdip, hdlp,
		    PSM_INTR_OP_XLATE_VECTOR, (int *)&hdlp->ih_vector);

		/* Remove the interrupt handler */
		rem_avintr((void *)hdlp, ispec->intrspec_pri,
		    hdlp->ih_cb_func, hdlp->ih_vector);
		break;
	case DDI_INTROP_SETMASK:
		if (psm_intr_ops == NULL)
			return (DDI_FAILURE);

		if ((*psm_intr_ops)(rdip, hdlp, PSM_INTR_OP_SET_MASK, NULL))
			return (DDI_FAILURE);
		break;
	case DDI_INTROP_CLRMASK:
		if (psm_intr_ops == NULL)
			return (DDI_FAILURE);

		if ((*psm_intr_ops)(rdip, hdlp, PSM_INTR_OP_CLEAR_MASK, NULL))
			return (DDI_FAILURE);
		break;
	case DDI_INTROP_GETPENDING:
		if (psm_intr_ops == NULL)
			return (DDI_FAILURE);

		if ((*psm_intr_ops)(rdip, hdlp, PSM_INTR_OP_GET_PENDING,
		    result)) {
			*(int *)result = 0;
			return (DDI_FAILURE);
		}
		break;
	case DDI_INTROP_NAVAIL:
	case DDI_INTROP_NINTRS:
		*(int *)result = i_ddi_get_intx_nintrs(rdip);
		if (*(int *)result == 0) {
			/*
			 * Special case for 'pcic' driver' only. This driver
			 * driver is a child of 'isa' and 'rootnex' drivers.
			 *
			 * See detailed comments on this in the function
			 * rootnex_get_ispec().
			 *
			 * Children of 'pcic' send 'NINITR' request all the
			 * way to rootnex driver. But, the 'pdp->par_nintr'
			 * field may not initialized. So, we fake it here
			 * to return 1 (a la what PCMCIA nexus does).
			 */
			if (strcmp(ddi_get_name(rdip), "pcic") == 0)
				*(int *)result = 1;
			else
				return (DDI_FAILURE);
		}
		break;
	case DDI_INTROP_SUPPORTED_TYPES:
		*(int *)result = DDI_INTR_TYPE_FIXED;	/* Always ... */
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
 * Allocate interrupt vector for FIXED (legacy) type.
 */
static int
rootnex_alloc_intr_fixed(dev_info_t *rdip, ddi_intr_handle_impl_t *hdlp,
    void *result)
{
	struct intrspec		*ispec;
	ddi_intr_handle_impl_t	info_hdl;
	int			ret;
	int			free_phdl = 0;
	apic_get_type_t		type_info;

	if (psm_intr_ops == NULL)
		return (DDI_FAILURE);

	if ((ispec = rootnex_get_ispec(rdip, hdlp->ih_inum)) == NULL)
		return (DDI_FAILURE);

	/*
	 * If the PSM module is "APIX" then pass the request for it
	 * to allocate the vector now.
	 */
	bzero(&info_hdl, sizeof (ddi_intr_handle_impl_t));
	info_hdl.ih_private = &type_info;
	if ((*psm_intr_ops)(NULL, &info_hdl, PSM_INTR_OP_APIC_TYPE, NULL) ==
	    PSM_SUCCESS && strcmp(type_info.avgi_type, APIC_APIX_NAME) == 0) {
		if (hdlp->ih_private == NULL) { /* allocate phdl structure */
			free_phdl = 1;
			i_ddi_alloc_intr_phdl(hdlp);
		}
		((ihdl_plat_t *)hdlp->ih_private)->ip_ispecp = ispec;
		ret = (*psm_intr_ops)(rdip, hdlp,
		    PSM_INTR_OP_ALLOC_VECTORS, result);
		if (free_phdl) { /* free up the phdl structure */
			free_phdl = 0;
			i_ddi_free_intr_phdl(hdlp);
			hdlp->ih_private = NULL;
		}
	} else {
		/*
		 * No APIX module; fall back to the old scheme where the
		 * interrupt vector is allocated during ddi_enable_intr() call.
		 */
		hdlp->ih_pri = ispec->intrspec_pri;
		*(int *)result = hdlp->ih_scratch1;
		ret = DDI_SUCCESS;
	}

	return (ret);
}

/*
 * Free up interrupt vector for FIXED (legacy) type.
 */
static int
rootnex_free_intr_fixed(dev_info_t *rdip, ddi_intr_handle_impl_t *hdlp)
{
	struct intrspec			*ispec;
	struct ddi_parent_private_data	*pdp;
	ddi_intr_handle_impl_t		info_hdl;
	int				ret;
	apic_get_type_t			type_info;

	if (psm_intr_ops == NULL)
		return (DDI_FAILURE);

	/*
	 * If the PSM module is "APIX" then pass the request for it
	 * to free up the vector now.
	 */
	bzero(&info_hdl, sizeof (ddi_intr_handle_impl_t));
	info_hdl.ih_private = &type_info;
	if ((*psm_intr_ops)(NULL, &info_hdl, PSM_INTR_OP_APIC_TYPE, NULL) ==
	    PSM_SUCCESS && strcmp(type_info.avgi_type, APIC_APIX_NAME) == 0) {
		if ((ispec = rootnex_get_ispec(rdip, hdlp->ih_inum)) == NULL)
			return (DDI_FAILURE);
		((ihdl_plat_t *)hdlp->ih_private)->ip_ispecp = ispec;
		ret = (*psm_intr_ops)(rdip, hdlp,
		    PSM_INTR_OP_FREE_VECTORS, NULL);
	} else {
		/*
		 * No APIX module; fall back to the old scheme where
		 * the interrupt vector was already freed during
		 * ddi_disable_intr() call.
		 */
		ret = DDI_SUCCESS;
	}

	pdp = ddi_get_parent_data(rdip);

	/*
	 * Special case for 'pcic' driver' only.
	 * If an intrspec was created for it, clean it up here
	 * See detailed comments on this in the function
	 * rootnex_get_ispec().
	 */
	if (pdp->par_intr && strcmp(ddi_get_name(rdip), "pcic") == 0) {
		kmem_free(pdp->par_intr, sizeof (struct intrspec) *
		    pdp->par_nintr);
		/*
		 * Set it to zero; so that
		 * DDI framework doesn't free it again
		 */
		pdp->par_intr = NULL;
		pdp->par_nintr = 0;
	}

	return (ret);
}


/*
 * ******************
 *  dma related code
 * ******************
 */

/*ARGSUSED*/
static int
rootnex_dma_allochdl(dev_info_t *dip, dev_info_t *rdip,
    ddi_dma_attr_t *attr, int (*waitfp)(caddr_t), caddr_t arg,
    ddi_dma_handle_t *handlep)
{
	uint64_t maxsegmentsize_ll;
	uint_t maxsegmentsize;
	ddi_dma_impl_t *hp;
	rootnex_dma_t *dma;
	uint64_t count_max;
	uint64_t seg;
	int kmflag;
	int e;

	/* convert our sleep flags */
	if (waitfp == DDI_DMA_SLEEP) {
		kmflag = KM_SLEEP;
	} else {
		kmflag = KM_NOSLEEP;
	}

	/*
	 * We try to do only one memory allocation here. We'll do a little
	 * pointer manipulation later. If the bind ends up taking more than
	 * our prealloc's space, we'll have to allocate more memory in the
	 * bind operation. Not great, but much better than before and the
	 * best we can do with the current bind interfaces.
	 */
	hp = kmem_cache_alloc(rootnex_state->r_dmahdl_cache, kmflag);
	if (hp == NULL) {
		if (waitfp != DDI_DMA_DONTWAIT) {
			ddi_set_callback(waitfp, arg,
			    &rootnex_state->r_dvma_call_list_id);
		}
		return (DDI_DMA_NORESOURCES);
	}

	/* Do our pointer manipulation now, align the structures */
	hp->dmai_private = (void *)(((uintptr_t)hp +
	    (uintptr_t)sizeof (ddi_dma_impl_t) + 0x7) & ~0x7);
	dma = (rootnex_dma_t *)hp->dmai_private;
	dma->dp_prealloc_buffer = (uchar_t *)(((uintptr_t)dma +
	    sizeof (rootnex_dma_t) + 0x7) & ~0x7);

	/* setup the handle */
	rootnex_clean_dmahdl(hp);
	hp->dmai_error.err_fep = NULL;
	hp->dmai_error.err_cf = NULL;
	dma->dp_dip = rdip;
	dma->dp_sglinfo.si_flags = attr->dma_attr_flags;
	dma->dp_sglinfo.si_min_addr = attr->dma_attr_addr_lo;

	dma->dp_sglinfo.si_max_addr = attr->dma_attr_addr_hi;

	hp->dmai_minxfer = attr->dma_attr_minxfer;
	hp->dmai_burstsizes = attr->dma_attr_burstsizes;
	hp->dmai_rdip = rdip;
	hp->dmai_attr = *attr;

	if (attr->dma_attr_seg >= dma->dp_sglinfo.si_max_addr)
		dma->dp_sglinfo.si_cancross = B_FALSE;
	else
		dma->dp_sglinfo.si_cancross = B_TRUE;

	/* we don't need to worry about the SPL since we do a tryenter */
	mutex_init(&dma->dp_mutex, NULL, MUTEX_DRIVER, NULL);

	/*
	 * Figure out our maximum segment size. If the segment size is greater
	 * than 4G, we will limit it to (4G - 1) since the max size of a dma
	 * object (ddi_dma_obj_t.dmao_size) is 32 bits. dma_attr_seg and
	 * dma_attr_count_max are size-1 type values.
	 *
	 * Maximum segment size is the largest physically contiguous chunk of
	 * memory that we can return from a bind (i.e. the maximum size of a
	 * single cookie).
	 */

	/* handle the rollover cases */
	seg = attr->dma_attr_seg + 1;
	if (seg < attr->dma_attr_seg) {
		seg = attr->dma_attr_seg;
	}
	count_max = attr->dma_attr_count_max + 1;
	if (count_max < attr->dma_attr_count_max) {
		count_max = attr->dma_attr_count_max;
	}

	/*
	 * granularity may or may not be a power of two. If it isn't, we can't
	 * use a simple mask.
	 */
	if (attr->dma_attr_granular & (attr->dma_attr_granular - 1)) {
		dma->dp_granularity_power_2 = B_FALSE;
	} else {
		dma->dp_granularity_power_2 = B_TRUE;
	}

	/*
	 * maxxfer should be a whole multiple of granularity. If we're going to
	 * break up a window because we're greater than maxxfer, we might as
	 * well make sure it's maxxfer is a whole multiple so we don't have to
	 * worry about triming the window later on for this case.
	 */
	if (attr->dma_attr_granular > 1) {
		if (dma->dp_granularity_power_2) {
			dma->dp_maxxfer = attr->dma_attr_maxxfer -
			    (attr->dma_attr_maxxfer &
			    (attr->dma_attr_granular - 1));
		} else {
			dma->dp_maxxfer = attr->dma_attr_maxxfer -
			    (attr->dma_attr_maxxfer % attr->dma_attr_granular);
		}
	} else {
		dma->dp_maxxfer = attr->dma_attr_maxxfer;
	}

	maxsegmentsize_ll = MIN(seg, dma->dp_maxxfer);
	maxsegmentsize_ll = MIN(maxsegmentsize_ll, count_max);
	if (maxsegmentsize_ll == 0 || (maxsegmentsize_ll > 0xFFFFFFFF)) {
		maxsegmentsize = 0xFFFFFFFF;
	} else {
		maxsegmentsize = maxsegmentsize_ll;
	}
	dma->dp_sglinfo.si_max_cookie_size = maxsegmentsize;
	dma->dp_sglinfo.si_segmask = attr->dma_attr_seg;

	/* check the ddi_dma_attr arg to make sure it makes a little sense */
	if (rootnex_alloc_check_parms) {
		e = rootnex_valid_alloc_parms(attr, maxsegmentsize);
		if (e != DDI_SUCCESS) {
			ROOTNEX_DPROF_INC(&rootnex_cnt[ROOTNEX_CNT_ALLOC_FAIL]);
			(void) rootnex_dma_freehdl(dip, rdip,
			    (ddi_dma_handle_t)hp);
			return (e);
		}
	}

	*handlep = (ddi_dma_handle_t)hp;

	ndi_fmc_insert(rdip, DMA_HANDLE, *handlep, NULL);

	ROOTNEX_DPROF_INC(&rootnex_cnt[ROOTNEX_CNT_ACTIVE_HDLS]);
	ROOTNEX_DPROBE1(rootnex__alloc__handle, uint64_t,
	    rootnex_cnt[ROOTNEX_CNT_ACTIVE_HDLS]);

	return (DDI_SUCCESS);
}

/*ARGSUSED*/
static int
rootnex_dma_freehdl(dev_info_t *dip, dev_info_t *rdip, ddi_dma_handle_t handle)
{
	ddi_dma_impl_t *hp;
	rootnex_dma_t *dma;

	ndi_fmc_remove(rdip, DMA_HANDLE, handle);

	hp = (ddi_dma_impl_t *)handle;
	dma = (rootnex_dma_t *)hp->dmai_private;

	/* unbind should have been called first */
	ASSERT(!dma->dp_inuse);

	mutex_destroy(&dma->dp_mutex);
	kmem_cache_free(rootnex_state->r_dmahdl_cache, hp);

	if (rootnex_state->r_dvma_call_list_id)
		ddi_run_callback(&rootnex_state->r_dvma_call_list_id);

	ROOTNEX_DPROF_DEC(&rootnex_cnt[ROOTNEX_CNT_ACTIVE_HDLS]);
	ROOTNEX_DPROBE1(rootnex__free__handle, uint64_t,
	    rootnex_cnt[ROOTNEX_CNT_ACTIVE_HDLS]);

	return (DDI_SUCCESS);
}

/*ARGSUSED*/
static int
rootnex_dma_bindhdl(dev_info_t *dip, dev_info_t *rdip,
    ddi_dma_handle_t handle, struct ddi_dma_req *dmareq,
    ddi_dma_cookie_t *cookiep, uint_t *ccountp)
{
	rootnex_sglinfo_t *sinfo;
	ddi_dma_obj_t *dmao;
	io_mvector_t *mvector;
	ddi_dma_attr_t *attr;
	ddi_dma_impl_t *hp;
	rootnex_dma_t *dma;
	int kmflag;
	int e;

	hp = (ddi_dma_impl_t *)handle;
	dma = (rootnex_dma_t *)hp->dmai_private;
	dmao = &dma->dp_dma;
	sinfo = &dma->dp_sglinfo;
	attr = &hp->dmai_attr;

	if (dmareq->dmar_object.dmao_type == DMA_OTYP_MVECTOR) {
		mvector = dmareq->dmar_object.dmao_obj.mvector;
		io_tag(&mvector->im_tags, IO_TAG_TIMESTAMP, "dma-bind", 0);
		io_tag(&mvector->im_tags, IO_TAG_STATE, "rootnex_dma_t",
		    (uint64_t)(uintptr_t)dma);
	}

	if (dmareq->dmar_fp == DDI_DMA_SLEEP) {
		kmflag = KM_SLEEP;
	} else {
		kmflag = KM_NOSLEEP;
	}

	hp->dmai_rflags = dmareq->dmar_flags & DMP_DDIFLAGS;

	/*
	 * This is useful for debugging a driver. Not as useful in a production
	 * system. The only time this will fail is if you have a driver bug.
	 */
	if (rootnex_bind_check_inuse) {
		/*
		 * No one else should ever have this lock unless someone else
		 * is trying to use this handle. So contention on the lock
		 * is the same as inuse being set.
		 */
		e = mutex_tryenter(&dma->dp_mutex);
		if (e == 0) {
			ROOTNEX_DPROF_INC(&rootnex_cnt[ROOTNEX_CNT_BIND_FAIL]);
			return (DDI_DMA_INUSE);
		}
		if (dma->dp_inuse) {
			mutex_exit(&dma->dp_mutex);
			ROOTNEX_DPROF_INC(&rootnex_cnt[ROOTNEX_CNT_BIND_FAIL]);
			return (DDI_DMA_INUSE);
		}
		dma->dp_inuse = B_TRUE;
		mutex_exit(&dma->dp_mutex);
	}

	/* check the ddi_dma_attr arg to make sure it makes a little sense */
	if (rootnex_bind_check_parms) {
		e = rootnex_valid_bind_parms(dmareq, attr);
		if (e != DDI_SUCCESS) {
			ROOTNEX_DPROF_INC(&rootnex_cnt[ROOTNEX_CNT_BIND_FAIL]);
			rootnex_clean_dmahdl(hp);
			return (e);
		}
	}

	/* save away the original bind info */
	dma->dp_dma = dmareq->dmar_object;

	/* Figure out what maximum number of cookies this buffer could use */
	sinfo->si_max_pages = io_get_page_cnt(&dma->dp_dma, 0, 0);

	/*
	 * We'll use the pre-allocated cookies for any bind that will *always*
	 * fit (more important to be consistent, we don't want to create
	 * additional degenerate cases).
	 */
	if (sinfo->si_max_pages <= rootnex_state->r_prealloc_cookies) {
		dma->dp_cookies = (ddi_dma_cookie_t *)dma->dp_prealloc_buffer;
		dma->dp_need_to_free_cookie = B_FALSE;
		ROOTNEX_DPROBE2(rootnex__bind__prealloc, dev_info_t *, rdip,
		    uint_t, sinfo->si_max_pages);

	/*
	 * For anything larger than that, we'll go ahead and allocate the
	 * maximum number of pages we expect to see. Hopefuly, we won't be
	 * seeing this path in the fast path for high performance devices very
	 * frequently.
	 *
	 * a ddi bind interface that allowed the driver to provide storage to
	 * the bind interface would speed this case up.
	 */
	} else {
		/*
		 * Save away how much memory we allocated. If we're doing a
		 * nosleep, the alloc could fail...
		 */
		dma->dp_cookie_size = sinfo->si_max_pages *
		    sizeof (ddi_dma_cookie_t);
		dma->dp_cookies = kmem_alloc(dma->dp_cookie_size, kmflag);
		if (dma->dp_cookies == NULL) {
			ROOTNEX_DPROF_INC(&rootnex_cnt[ROOTNEX_CNT_BIND_FAIL]);
			rootnex_clean_dmahdl(hp);
			if (dmareq->dmar_fp != DDI_DMA_DONTWAIT) {
				ddi_set_callback(dmareq->dmar_fp,
				    dmareq->dmar_arg,
				    &rootnex_state->r_dvma_call_list_id);
			}
			return (DDI_DMA_NORESOURCES);
		}
		dma->dp_need_to_free_cookie = B_TRUE;
		ROOTNEX_DPROBE2(rootnex__bind__alloc, dev_info_t *, rdip,
		    uint_t, sinfo->si_max_pages);
	}
	hp->dmai_cookie = dma->dp_cookies;

	/*
	 * Get the real sgl. rootnex_get_sgl will fill in cookie array while
	 * looking at the constraints in the dma structure. It will then put
	 * some additional state about the sgl in the dma struct (i.e. is
	 * the sgl clean, or do we need to do some munging; how many pages
	 * need to be copied, etc.)
	 */
	rootnex_get_sgl(dmao, dma->dp_cookies, &dma->dp_sglinfo);

out:
	ASSERT(sinfo->si_sgl_size <= sinfo->si_max_pages);
	/* if we don't need a copy buffer, we don't need to sync */
	if (sinfo->si_copybuf_req == 0) {
		hp->dmai_rflags |= DMP_NOSYNC;
	}

	/*
	 * if we don't need the copybuf and we don't need to do a partial,  we
	 * hit the fast path. All the high performance devices should be trying
	 * to hit this path. To hit this path, a device should be able to reach
	 * all of memory, shouldn't try to bind more than it can transfer, and
	 * the buffer shouldn't require more cookies than the driver/device can
	 * handle [sgllen]).
	 */
	if ((sinfo->si_copybuf_req == 0) &&
	    (sinfo->si_sgl_size <= attr->dma_attr_sgllen) &&
	    (dmao->dmao_size < dma->dp_maxxfer)) {
fast:
		/*
		 * If the driver supports FMA, insert the handle in the FMA DMA
		 * handle cache.
		 */
		if (attr->dma_attr_flags & DDI_DMA_FLAGERR)
			hp->dmai_error.err_cf = rootnex_dma_check;

		/*
		 * copy out the first cookie and ccountp, set the cookie
		 * pointer to the second cookie. The first cookie is passed
		 * back on the stack. Additional cookies are accessed via
		 * ddi_dma_nextcookie()
		 */
		*cookiep = dma->dp_cookies[0];
		*ccountp = sinfo->si_sgl_size;
		hp->dmai_cookie++;
		hp->dmai_rflags &= ~DDI_DMA_PARTIAL;
		ROOTNEX_DPROF_INC(&rootnex_cnt[ROOTNEX_CNT_ACTIVE_BINDS]);
		ROOTNEX_DPROBE4(rootnex__bind__fast, dev_info_t *, rdip,
		    uint64_t, rootnex_cnt[ROOTNEX_CNT_ACTIVE_BINDS],
		    uint_t, dmao->dmao_size, uint_t, *ccountp);


		return (DDI_DMA_MAPPED);
	}

	/*
	 * go to the slow path, we may need to alloc more memory, create
	 * multiple windows, and munge up a sgl to make the device happy.
	 */

	e = rootnex_bind_slowpath(hp, dmareq, dma, attr, &dma->dp_dma, kmflag);

	if ((e != DDI_DMA_MAPPED) && (e != DDI_DMA_PARTIAL_MAP)) {
		if (dma->dp_need_to_free_cookie) {
			kmem_free(dma->dp_cookies, dma->dp_cookie_size);
		}
		ROOTNEX_DPROF_INC(&rootnex_cnt[ROOTNEX_CNT_BIND_FAIL]);
		rootnex_clean_dmahdl(hp); /* must be after free cookie */
		return (e);
	}

	/*
	 * If the driver supports FMA, insert the handle in the FMA DMA handle
	 * cache.
	 */
	if (attr->dma_attr_flags & DDI_DMA_FLAGERR)
		hp->dmai_error.err_cf = rootnex_dma_check;

	/* if the first window uses the copy buffer, sync it for the device */
	if ((dma->dp_window[dma->dp_current_win].wd_dosync) &&
	    (hp->dmai_rflags & DDI_DMA_WRITE)) {
		(void) rootnex_dma_sync(dip, rdip, handle, 0, 0,
		    DDI_DMA_SYNC_FORDEV);
	}

	/*
	 * copy out the first cookie and ccountp, set the cookie pointer to the
	 * second cookie. Make sure the partial flag is set/cleared correctly.
	 * If we have a partial map (i.e. multiple windows), the number of
	 * cookies we return is the number of cookies in the first window.
	 */
	if (e == DDI_DMA_MAPPED) {
		hp->dmai_rflags &= ~DDI_DMA_PARTIAL;
		*ccountp = sinfo->si_sgl_size;
		hp->dmai_nwin = 1;
	} else {
		hp->dmai_rflags |= DDI_DMA_PARTIAL;
		*ccountp = dma->dp_window[dma->dp_current_win].wd_cookie_cnt;
		ASSERT(hp->dmai_nwin <= dma->dp_max_win);
	}
	*cookiep = dma->dp_cookies[0];
	hp->dmai_cookie++;

	ROOTNEX_DPROF_INC(&rootnex_cnt[ROOTNEX_CNT_ACTIVE_BINDS]);
	ROOTNEX_DPROBE4(rootnex__bind__slow, dev_info_t *, rdip, uint64_t,
	    rootnex_cnt[ROOTNEX_CNT_ACTIVE_BINDS], uint_t,
	    dmao->dmao_size, uint_t, *ccountp);
	return (e);
}

/*ARGSUSED*/
static int
rootnex_dma_unbindhdl(dev_info_t *dip, dev_info_t *rdip,
    ddi_dma_handle_t handle)
{
	ddi_dma_impl_t *hp;
	rootnex_dma_t *dma;
	int e;

	hp = (ddi_dma_impl_t *)handle;
	dma = (rootnex_dma_t *)hp->dmai_private;

	/* make sure the buffer wasn't free'd before calling unbind */
	if (rootnex_unbind_verify_buffer) {
		e = rootnex_verify_buffer(dma);
		if (e != DDI_SUCCESS) {
			ASSERT(0);
			return (DDI_FAILURE);
		}
	}

	/* sync the current window before unbinding the buffer */
	if (dma->dp_window && dma->dp_window[dma->dp_current_win].wd_dosync &&
	    (hp->dmai_rflags & DDI_DMA_READ)) {
		(void) rootnex_dma_sync(dip, rdip, handle, 0, 0,
		    DDI_DMA_SYNC_FORCPU);
	}

	/*
	 * cleanup and copy buffer or window state. if we didn't use the copy
	 * buffer or windows, there won't be much to do :-)
	 */
	rootnex_teardown_copybuf(dma);
	rootnex_teardown_windows(dma);

	/*
	 * If we had to allocate space to for the worse case sgl (it didn't
	 * fit into our pre-allocate buffer), free that up now
	 */
	if (dma->dp_need_to_free_cookie) {
		kmem_free(dma->dp_cookies, dma->dp_cookie_size);
	}

	/*
	 * clean up the handle so it's ready for the next bind (i.e. if the
	 * handle is reused).
	 */
	rootnex_clean_dmahdl(hp);
	hp->dmai_error.err_cf = NULL;

	ROOTNEX_DPROF_DEC(&rootnex_cnt[ROOTNEX_CNT_ACTIVE_BINDS]);
	ROOTNEX_DPROBE1(rootnex__unbind, uint64_t,
	    rootnex_cnt[ROOTNEX_CNT_ACTIVE_BINDS]);

	if (dma->dp_dma.dmao_type == DMA_OTYP_MVECTOR) {
		io_mvector_t *mvector;

		mvector = dma->dp_dma.dmao_obj.mvector;
		io_tag(&mvector->im_tags, IO_TAG_TIMESTAMP,
		    "dma-unbind", 0);
	}

	if (rootnex_state->r_dvma_call_list_id)
		ddi_run_callback(&rootnex_state->r_dvma_call_list_id);

	return (DDI_SUCCESS);
}

static struct as *
rootnex_get_as(ddi_dma_obj_t *dmao)
{
	struct as *asp;

	switch (dmao->dmao_type) {
	case DMA_OTYP_VADDR:
	case DMA_OTYP_BUFVADDR:
		asp = dmao->dmao_obj.virt_obj.v_as;
		if (asp == NULL)
			asp = &kas;
		break;
	default:
		asp = NULL;
		break;
	}
	return (asp);
}

/*
 * rootnex_verify_buffer()
 *   verify buffer wasn't free'd
 */
static int
rootnex_verify_buffer(rootnex_dma_t *dma)
{
	page_t **pplist;
	caddr_t vaddr;
	uint_t pcnt;
	uint_t poff;
	page_t *pp;
	char b;
	int i;

	/* Figure out how many pages this buffer occupies */
	if (dma->dp_dma.dmao_type == DMA_OTYP_PAGES) {
		poff = dma->dp_dma.dmao_obj.pp_obj.pp_offset & MMU_PAGEOFFSET;
	} else {
		vaddr = dma->dp_dma.dmao_obj.virt_obj.v_addr;
		poff = (uintptr_t)vaddr & MMU_PAGEOFFSET;
	}
	pcnt = mmu_btopr(dma->dp_dma.dmao_size + poff);

	switch (dma->dp_dma.dmao_type) {
	case DMA_OTYP_PAGES:
		/*
		 * for a linked list of pp's walk through them to make sure
		 * they're locked and not free.
		 */
		pp = dma->dp_dma.dmao_obj.pp_obj.pp_pp;
		for (i = 0; i < pcnt; i++) {
			if (PP_ISFREE(pp) || !PAGE_LOCKED(pp)) {
				return (DDI_FAILURE);
			}
			pp = pp->p_next;
		}
		break;

	case DMA_OTYP_VADDR:
	case DMA_OTYP_BUFVADDR:
		pplist = dma->dp_dma.dmao_obj.virt_obj.v_priv;
		/*
		 * for an array of pp's walk through them to make sure they're
		 * not free. It's possible that they may not be locked.
		 */
		if (pplist) {
			for (i = 0; i < pcnt; i++) {
				if (PP_ISFREE(pplist[i])) {
					return (DDI_FAILURE);
				}
			}

		/* For a virtual address, try to peek at each page */
		} else {
			if (rootnex_get_as(&dma->dp_dma) == &kas) {
				for (i = 0; i < pcnt; i++) {
					if (ddi_peek8(NULL, vaddr, &b) ==
					    DDI_FAILURE)
						return (DDI_FAILURE);
					vaddr += MMU_PAGESIZE;
				}
			}
		}
		break;

	default:
		cmn_err(CE_PANIC, "rootnex_verify_buffer: bad DMA object");
		break;
	}

	return (DDI_SUCCESS);
}


/*
 * rootnex_clean_dmahdl()
 *    Clean the dma handle. This should be called on a handle alloc and an
 *    unbind handle. Set the handle state to the default settings.
 */
static void
rootnex_clean_dmahdl(ddi_dma_impl_t *hp)
{
	rootnex_dma_t *dma;


	dma = (rootnex_dma_t *)hp->dmai_private;

	hp->dmai_nwin = 0;
	dma->dp_current_cookie = 0;
	dma->dp_copybuf_size = 0;
	dma->dp_window = NULL;
	dma->dp_cbaddr = NULL;
	dma->dp_inuse = B_FALSE;
	dma->dp_need_to_free_cookie = B_FALSE;
	dma->dp_need_to_free_window = B_FALSE;
	dma->dp_partial_required = B_FALSE;
	dma->dp_trim_required = B_FALSE;
	dma->dp_sglinfo.si_copybuf_req = 0;

	/* FMA related initialization */
	hp->dmai_fault = 0;
	hp->dmai_fault_check = NULL;
	hp->dmai_fault_notify = NULL;
	hp->dmai_error.err_ena = 0;
	hp->dmai_error.err_status = DDI_FM_OK;
	hp->dmai_error.err_expected = DDI_FM_ERR_UNEXPECTED;
	hp->dmai_error.err_ontrap = NULL;
}


/*
 * rootnex_valid_alloc_parms()
 *    Called in ddi_dma_alloc_handle path to validate its parameters.
 */
static int
rootnex_valid_alloc_parms(ddi_dma_attr_t *attr, uint_t maxsegmentsize)
{
	if ((attr->dma_attr_seg < MMU_PAGEOFFSET) ||
	    (attr->dma_attr_count_max < MMU_PAGEOFFSET) ||
	    (attr->dma_attr_granular > MMU_PAGESIZE) ||
	    (attr->dma_attr_maxxfer < MMU_PAGESIZE)) {
		return (DDI_DMA_BADATTR);
	}

	if (attr->dma_attr_addr_hi <= attr->dma_attr_addr_lo) {
		return (DDI_DMA_BADATTR);
	}

	if ((attr->dma_attr_seg & MMU_PAGEOFFSET) != MMU_PAGEOFFSET ||
	    MMU_PAGESIZE & (attr->dma_attr_granular - 1) ||
	    attr->dma_attr_sgllen <= 0) {
		return (DDI_DMA_BADATTR);
	}

	/* We should be able to DMA into every byte offset in a page */
	if (maxsegmentsize < MMU_PAGESIZE) {
		return (DDI_DMA_BADATTR);
	}

	/* if we're bouncing on seg, seg must be <= addr_hi */
	if ((attr->dma_attr_flags & _DDI_DMA_BOUNCE_ON_SEG) &&
	    (attr->dma_attr_seg > attr->dma_attr_addr_hi)) {
		return (DDI_DMA_BADATTR);
	}
	return (DDI_SUCCESS);
}

/*
 * rootnex_valid_bind_parms()
 *    Called in ddi_dma_*_bind_handle path to validate its parameters.
 */
/* ARGSUSED */
static int
rootnex_valid_bind_parms(ddi_dma_req_t *dmareq, ddi_dma_attr_t *attr)
{
	off_t	poff;
	caddr_t vaddr;

	/*
	 * Verify the alignment for start of DMA (i.e. dma_attr_align
	 * is both an allocation hint and a bind requirement).
	 */
	if (attr->dma_attr_flags & DDI_DMA_ALIGNMENT_REQUIRED) {
		if (dmareq->dmar_object.dmao_type == DMA_OTYP_PAGES)
			poff = dmareq->dmar_object.dmao_obj.pp_obj.pp_offset &
			    MMU_PAGEOFFSET;
		else {
			vaddr = dmareq->dmar_object.dmao_obj.virt_obj.v_addr;
			poff = (uintptr_t)vaddr & MMU_PAGEOFFSET;
		}
		if (poff & (attr->dma_attr_align - 1))
			return (DDI_DMA_NOMAPPING);	/* violation */
	}

	return (DDI_SUCCESS);
}


/*
 * rootnex_need_bounce_seg()
 *    check to see if the buffer lives on both side of the seg.
 */
static boolean_t
rootnex_need_bounce_seg(ddi_dma_obj_t *dmar_object, rootnex_sglinfo_t *sglinfo)
{
	paddr_t raddr;
	boolean_t lower_addr;
	boolean_t upper_addr;
	uint64_t offset;
	page_t **pplist;
	struct as *asp;
	uint64_t paddr;
	uint32_t psize;
	uint32_t size;
	caddr_t vaddr;
	uint_t iovcnt;
	uint_t pcnt;
	page_t *pp;


	lower_addr = B_FALSE;
	upper_addr = B_FALSE;
	iovcnt = 0;

bounce_mvector:
	/*
	 * figure out if we have a list of pages, and array of pages, or a
	 * kernel or user VA for the dma or current io_vector in a mvector.
	 */
	io_parse_dmarobj(dmar_object, iovcnt, &asp, &vaddr, &size, &pplist,
	    &pp, &offset);

	/*
	 * if we were passed down a linked list of pages, i.e. pointer to
	 * page_t, use this to get our physical address and buf offset.
	 */
	if (pp != NULL) {
		paddr = pfn_to_pa(pp->p_pagenum) + offset;
		psize = MIN(size, (MMU_PAGESIZE - offset));
		pp = pp->p_next;

	/*
	 * We weren't passed down a linked list of pages, but if we were passed
	 * down an array of pages, use this to get our physical address and buf
	 * offset.
	 */
	} else if (pplist != NULL) {
		pcnt = 0;
		ASSERT(!PP_ISFREE(pplist[pcnt]));
		paddr = pfn_to_pa(pplist[pcnt]->p_pagenum) + offset;
		psize = MIN(size, (MMU_PAGESIZE - offset));
		pcnt++;

	/*
	 * All we have is a virtual address, we'll need to call into the VM
	 * to get the physical address.
	 */
	} else {
		paddr = pfn_to_pa(hat_getpfnum(asp->a_hat, vaddr)) + offset;
		psize = MIN(size, (MMU_PAGESIZE - offset));
		vaddr += psize;
	}

	raddr = paddr;

	if ((raddr + psize) > sglinfo->si_segmask) {
		upper_addr = B_TRUE;
	} else {
		lower_addr = B_TRUE;
	}
	size -= psize;

	/*
	 * Walk through the rest of the pages in the buffer. Track to see
	 * if we have pages on both sides of the segment boundary.
	 */
	while (size > 0) {
		/* partial or full page */
		psize = MIN(size, MMU_PAGESIZE);

		if (pp != NULL) {
			/* get the paddr from the page_t */
			ASSERT(!PP_ISFREE(pp) && PAGE_LOCKED(pp));
			paddr = pfn_to_pa(pp->p_pagenum);
			pp = pp->p_next;
		} else if (pplist != NULL) {
			/* index into the array of page_t's to get the paddr */
			ASSERT(!PP_ISFREE(pplist[pcnt]));
			paddr = pfn_to_pa(pplist[pcnt]->p_pagenum);
			pcnt++;
		} else {
			/* call into the VM to get the paddr */
			paddr =  pfn_to_pa(hat_getpfnum(asp->a_hat,
			    vaddr));
			vaddr += psize;
		}

		raddr = paddr;

		if ((raddr + psize) > sglinfo->si_segmask) {
			upper_addr = B_TRUE;
		} else {
			lower_addr = B_TRUE;
		}
		/*
		 * if the buffer lives both above and below the segment
		 * boundary, or the current page is the page immediately
		 * after the segment, we will use a copy/bounce buffer for
		 * all pages > seg.
		 */
		if ((lower_addr && upper_addr) ||
		    (raddr == (sglinfo->si_segmask + 1))) {
			return (B_TRUE);
		}

		size -= psize;
	}

	/*
	 * if we are looking at an IO mvector, and there are more vectors
	 * left, check the next vector.
	 */
	if (dmar_object->dmao_type == DMA_OTYP_MVECTOR) {
		iovcnt++;
		if (iovcnt < dmar_object->dmao_obj.mvector->im_iovec_cnt)
			goto bounce_mvector;
	}

	return (B_FALSE);
}

/*
 * rootnex_get_sgl()
 *    Called in bind fastpath to get the sgl. Most of this will be replaced
 *    with a call to the vm layer when vm2.0 comes around...
 */
static void
rootnex_get_sgl(ddi_dma_obj_t *dmar_object, ddi_dma_cookie_t *sgl,
    rootnex_sglinfo_t *sglinfo)
{
	paddr_t raddr;
	uint64_t last_page;
	uint64_t offset;
	uint64_t addrhi;
	uint64_t addrlo;
	uint64_t maxseg;
	page_t **pplist;
	struct as *asp;
	uint64_t paddr;
	uint32_t psize;
	uint32_t size;
	caddr_t vaddr;
	uint_t iovcnt;
	uint_t pcnt;
	page_t *pp;
	uint_t cnt;


	/* shortcuts */
	maxseg = sglinfo->si_max_cookie_size;
	addrhi = sglinfo->si_max_addr;
	addrlo = sglinfo->si_min_addr;

	/*
	 * check to see if we need to use the copy buffer for pages over
	 * the segment attr.
	 */
	sglinfo->si_bounce_on_seg = B_FALSE;
	if (sglinfo->si_flags & _DDI_DMA_BOUNCE_ON_SEG) {
		sglinfo->si_bounce_on_seg = rootnex_need_bounce_seg(
		    dmar_object, sglinfo);
	}

	sglinfo->si_asp = NULL;
	cnt = 0;
	iovcnt = 0;

dmagetsgl_mvector:
	/*
	 * figure out if we have a list of pages, and array of pages, or a
	 * kernel or user VA for the dma or current io_vector in a mvector.
	 */
	io_parse_dmarobj(dmar_object, iovcnt, &asp, &vaddr, &size, &pplist,
	    &pp, &offset);

	/*
	 * at this time, all io_vector's in a mvector must have the same
	 * 'struct as'.
	 */
#ifdef	DEBUG
	if ((sglinfo->si_asp != NULL) && (sglinfo->si_asp != asp))
		ASSERT(0);
#endif
	sglinfo->si_asp = asp;

	/*
	 * if we were passed down a linked list of pages, i.e. pointer to
	 * page_t, use this to get our physical address and buf offset.
	 */
	if (pp != NULL) {
		paddr = pfn_to_pa(pp->p_pagenum) + offset;
		psize = MIN(size, (MMU_PAGESIZE - offset));
		pp = pp->p_next;

	/*
	 * We weren't passed down a linked list of pages, but if we were passed
	 * down an array of pages, use this to get our physical address and buf
	 * offset.
	 */
	} else if (pplist != NULL) {
		pcnt = 0;
		ASSERT(!PP_ISFREE(pplist[pcnt]));
		paddr = pfn_to_pa(pplist[pcnt]->p_pagenum) + offset;
		psize = MIN(size, (MMU_PAGESIZE - offset));
		pcnt++;

	/*
	 * All we have is a virtual address, we'll need to call into the VM
	 * to get the physical address.
	 */
	} else {
		paddr = pfn_to_pa(hat_getpfnum(asp->a_hat, vaddr)) + offset;
		psize = MIN(size, (MMU_PAGESIZE - offset));
		vaddr += psize;
	}

	raddr = paddr;

	/*
	 * Setup the first cookie with the physical address of the page and the
	 * size of the page (which takes into account the initial offset into
	 * the page.
	 */
	sgl[cnt].dmac_laddress = raddr;
	sgl[cnt].dmac_size = psize;
	sgl[cnt].dmac_type = 0;

	/*
	 * Save away the buffer offset into the page. We'll need this later in
	 * the copy buffer code to help figure out the page index within the
	 * buffer and the offset into the current page. This is only applicable
	 * if we don't have an MVECTOR.
	 */
	if (dmar_object->dmao_type != DMA_OTYP_MVECTOR)
		sglinfo->si_buf_offset = offset;

	/*
	 * If we are using the copy buffer for anything over the segment
	 * boundary, and this page is over the segment boundary.
	 *   OR
	 * if the DMA engine can't reach the physical address.
	 */
	if (((sglinfo->si_bounce_on_seg) &&
	    ((raddr + psize) > sglinfo->si_segmask)) ||
	    ((raddr < addrlo) || ((raddr + psize) > addrhi))) {
		/*
		 * Increase how much copy buffer we use. We always increase by
		 * pagesize so we don't have to worry about converting offsets.
		 * Set a flag in the cookies dmac_type to indicate that it uses
		 * the copy buffer. If this isn't the last cookie, go to the
		 * next cookie (since we separate each page which uses the copy
		 * buffer in case the copy buffer is not physically contiguous.
		 */
		sglinfo->si_copybuf_req += MMU_PAGESIZE;
		sgl[cnt].dmac_type = ROOTNEX_USES_COPYBUF;
		if ((cnt + 1) < sglinfo->si_max_pages) {
			cnt++;
			sgl[cnt].dmac_laddress = 0;
			sgl[cnt].dmac_size = 0;
			sgl[cnt].dmac_type = 0;
		}
	}

	/*
	 * save this page's physical address so we can figure out if the next
	 * page is physically contiguous. Keep decrementing size until we are
	 * done with the buffer.
	 */
	last_page = raddr & MMU_PAGEMASK;
	size -= psize;

	while (size > 0) {
		/* Get the size for this page (i.e. partial or full page) */
		psize = MIN(size, MMU_PAGESIZE);

		if (pp != NULL) {
			/* get the paddr from the page_t */
			ASSERT(!PP_ISFREE(pp) && PAGE_LOCKED(pp));
			paddr = pfn_to_pa(pp->p_pagenum);
			pp = pp->p_next;
		} else if (pplist != NULL) {
			/* index into the array of page_t's to get the paddr */
			ASSERT(!PP_ISFREE(pplist[pcnt]));
			paddr = pfn_to_pa(pplist[pcnt]->p_pagenum);
			pcnt++;
		} else {
			/* call into the VM to get the paddr */
			paddr =  pfn_to_pa(hat_getpfnum(asp->a_hat,
			    vaddr));
			vaddr += psize;
		}

		raddr = paddr;

		/*
		 * If we are using the copy buffer for anything over the
		 * segment boundary, and this page is over the segment
		 * boundary.
		 *   OR
		 * if the DMA engine can't reach the physical address.
		 */
		if (((sglinfo->si_bounce_on_seg) &&
		    ((raddr + psize) > sglinfo->si_segmask)) ||
		    ((raddr < addrlo) || ((raddr + psize) > addrhi))) {

			sglinfo->si_copybuf_req += MMU_PAGESIZE;

			/*
			 * if there is something in the current cookie, go to
			 * the next one. We only want one page in a cookie which
			 * uses the copybuf since the copybuf doesn't have to
			 * be physically contiguous.
			 */
			if (sgl[cnt].dmac_size != 0) {
				cnt++;
			}
			sgl[cnt].dmac_laddress = raddr;
			sgl[cnt].dmac_size = psize;
			sgl[cnt].dmac_type = ROOTNEX_USES_COPYBUF;
			/* if this isn't the last cookie, go to the next one */
			if ((cnt + 1) < sglinfo->si_max_pages) {
				cnt++;
				sgl[cnt].dmac_laddress = 0;
				sgl[cnt].dmac_size = 0;
				sgl[cnt].dmac_type = 0;
			}

		/*
		 * this page didn't need the copy buffer, if it's not physically
		 * contiguous, or it would put us over a segment boundary, or it
		 * puts us over the max cookie size, or the current sgl doesn't
		 * have anything in it.
		 */
		} else if (((last_page + MMU_PAGESIZE) != raddr) ||
		    !(raddr & sglinfo->si_segmask) ||
		    ((sgl[cnt].dmac_size + psize) > maxseg) ||
		    (sgl[cnt].dmac_size == 0)) {
			/*
			 * if we're not already in a new cookie, go to the next
			 * cookie.
			 */
			if (sgl[cnt].dmac_size != 0) {
				cnt++;
			}

			/* save the cookie information */
			sgl[cnt].dmac_laddress = raddr;
			sgl[cnt].dmac_size = psize;
			sgl[cnt].dmac_type = 0;

		/*
		 * this page didn't need the copy buffer, it is physically
		 * contiguous with the last page, and it's <= the max cookie
		 * size.
		 */
		} else {
			sgl[cnt].dmac_size += psize;

			/*
			 * if this exactly ==  the maximum cookie size, and
			 * it isn't the last cookie, go to the next cookie.
			 */
			if (((sgl[cnt].dmac_size + psize) == maxseg) &&
			    ((cnt + 1) < sglinfo->si_max_pages)) {
				cnt++;
				sgl[cnt].dmac_laddress = 0;
				sgl[cnt].dmac_size = 0;
				sgl[cnt].dmac_type = 0;
			}
		}

		/*
		 * save this page's physical address so we can figure out if the
		 * next page is physically contiguous. Keep decrementing size
		 * until we are done with the buffer.
		 */
		last_page = raddr;
		size -= psize;
	}

	/*
	 * if we are looking at an IO mvector, and there are more vectors
	 * left, add in the next vector.
	 */
	if (dmar_object->dmao_type == DMA_OTYP_MVECTOR) {
		iovcnt++;
		if (iovcnt < dmar_object->dmao_obj.mvector->im_iovec_cnt) {
			if (sgl[cnt].dmac_size > 0) {
				cnt++;
				sgl[cnt].dmac_laddress = 0;
				sgl[cnt].dmac_size = 0;
				sgl[cnt].dmac_type = 0;
			}
			goto dmagetsgl_mvector;
		}
	}

	/* we're done, save away how many cookies the sgl has */
	if (sgl[cnt].dmac_size == 0) {
		ASSERT(cnt < sglinfo->si_max_pages);
		sglinfo->si_sgl_size = cnt;
	} else {
		sglinfo->si_sgl_size = cnt + 1;
	}
}

/*
 * rootnex_bind_slowpath()
 *    Call in the bind path if the calling driver can't use the sgl without
 *    modifying it. We either need to use the copy buffer and/or we will end up
 *    with a partial bind.
 */
static int
rootnex_bind_slowpath(ddi_dma_impl_t *hp, struct ddi_dma_req *dmareq,
    rootnex_dma_t *dma, ddi_dma_attr_t *attr, ddi_dma_obj_t *dmao, int kmflag)
{
	rootnex_sglinfo_t *sinfo;
	rootnex_window_t *window;
	ddi_dma_cookie_t *cookie;
	size_t copybuf_used;
	size_t dmac_size;
	boolean_t partial;
	off_t cur_offset;
	page_t *cur_pp;
	int e;
	int i;


	sinfo = &dma->dp_sglinfo;
	copybuf_used = 0;
	partial = B_FALSE;

	/*
	 * If we're using the copybuf, set the copybuf state in dma struct.
	 * Needs to be first since it sets the copy buffer size.
	 */
	if (sinfo->si_copybuf_req != 0) {
		e = rootnex_setup_copybuf(hp, dmareq, dma, attr);
		if (e != DDI_SUCCESS) {
			return (e);
		}
	} else {
		dma->dp_copybuf_size = 0;
	}

	/*
	 * Figure out if we need to do a partial mapping. If so, figure out
	 * if we need to trim the buffers when we munge the sgl.
	 */
	if ((dma->dp_copybuf_size < sinfo->si_copybuf_req) ||
	    (dmao->dmao_size > dma->dp_maxxfer) ||
	    (attr->dma_attr_sgllen < sinfo->si_sgl_size)) {
		dma->dp_partial_required = B_TRUE;
		if (attr->dma_attr_granular != 1) {
			dma->dp_trim_required = B_TRUE;
		}
	} else {
		dma->dp_partial_required = B_FALSE;
		dma->dp_trim_required = B_FALSE;
	}

	/*
	 * If we need to do a partial bind, the driver must support it.
	 */
	if (dma->dp_partial_required &&
	    !(dmareq->dmar_flags & DDI_DMA_PARTIAL)) {
		return (DDI_DMA_TOOBIG);
	}

	/*
	 * we might need multiple windows, setup state to handle them. In this
	 * code path, we will have at least one window.
	 */
	e = rootnex_setup_windows(hp, dma, attr, dmao, kmflag);
	if (e != DDI_SUCCESS) {
		rootnex_teardown_copybuf(dma);
		return (e);
	}

	window = &dma->dp_window[0];
	cookie = &dma->dp_cookies[0];
	cur_offset = 0;
	rootnex_init_win(hp, dma, window, cookie, cur_offset);
	if (dmao->dmao_type == DMA_OTYP_PAGES) {
		cur_pp = dmareq->dmar_object.dmao_obj.pp_obj.pp_pp;
	}

	/* loop though all the cookies we got back from get_sgl() */
	for (i = 0; i < sinfo->si_sgl_size; i++) {
		/*
		 * If we're using the copy buffer, check this cookie and setup
		 * its associated copy buffer state. If this cookie uses the
		 * copy buffer, make sure we sync this window during dma_sync.
		 */
		if (dma->dp_copybuf_size > 0) {
			e = rootnex_setup_cookie(dma, kmflag,
			    cookie, cur_offset, &copybuf_used, &cur_pp);
			if (e != DDI_SUCCESS) {
				rootnex_teardown_copybuf(dma);
				rootnex_teardown_windows(dma);
				return (e);
			}
			if (cookie->dmac_type & ROOTNEX_USES_COPYBUF) {
				window->wd_dosync = B_TRUE;
			}
		}

		/*
		 * save away the cookie size, since it could be modified in
		 * the windowing code.
		 */
		dmac_size = cookie->dmac_size;

		/* if we went over max copybuf size */
		if (dma->dp_copybuf_size &&
		    (copybuf_used > dma->dp_copybuf_size)) {
			partial = B_TRUE;
			e = rootnex_copybuf_window_boundary(hp, dma, &window,
			    cookie, cur_offset, &copybuf_used);
			if (e != DDI_SUCCESS) {
				rootnex_teardown_copybuf(dma);
				rootnex_teardown_windows(dma);
				return (e);
			}

			/*
			 * if the coookie uses the copy buffer, make sure the
			 * new window we just moved to is set to sync.
			 */
			if (cookie->dmac_type & ROOTNEX_USES_COPYBUF) {
				window->wd_dosync = B_TRUE;
			}
			ROOTNEX_DPROBE1(rootnex__copybuf__window, dev_info_t *,
			    dma->dp_dip);

		/* if the cookie cnt == max sgllen, move to the next window */
		} else if (window->wd_cookie_cnt >= attr->dma_attr_sgllen) {
			partial = B_TRUE;
			ASSERT(window->wd_cookie_cnt == attr->dma_attr_sgllen);
			e = rootnex_sgllen_window_boundary(hp, dma, &window,
			    cookie, attr, cur_offset);
			if (e != DDI_SUCCESS) {
				rootnex_teardown_copybuf(dma);
				rootnex_teardown_windows(dma);
				return (e);
			}

			/*
			 * if the coookie uses the copy buffer, make sure the
			 * new window we just moved to is set to sync.
			 */
			if (cookie->dmac_type & ROOTNEX_USES_COPYBUF) {
				window->wd_dosync = B_TRUE;
			}
			ROOTNEX_DPROBE1(rootnex__sgllen__window, dev_info_t *,
			    dma->dp_dip);

		/* else if we will be over maxxfer */
		} else if ((window->wd_size + dmac_size) >
		    dma->dp_maxxfer) {
			partial = B_TRUE;
			e = rootnex_maxxfer_window_boundary(hp, dma, &window,
			    cookie);
			if (e != DDI_SUCCESS) {
				rootnex_teardown_copybuf(dma);
				rootnex_teardown_windows(dma);
				return (e);
			}

			/*
			 * if the coookie uses the copy buffer, make sure the
			 * new window we just moved to is set to sync.
			 */
			if (cookie->dmac_type & ROOTNEX_USES_COPYBUF) {
				window->wd_dosync = B_TRUE;
			}
			ROOTNEX_DPROBE1(rootnex__maxxfer__window, dev_info_t *,
			    dma->dp_dip);

		/* else this cookie fits in the current window */
		} else {
			window->wd_cookie_cnt++;
			window->wd_size += dmac_size;
		}

		/* track our offset into the buffer, go to the next cookie */
		ASSERT(dmac_size <= dmao->dmao_size);
		ASSERT(cookie->dmac_size <= dmac_size);
		cur_offset += dmac_size;
		cookie++;
	}

	/* if we ended up with a zero sized window in the end, clean it up */
	if (window->wd_size == 0) {
		hp->dmai_nwin--;
		window--;
	}

	ASSERT(window->wd_trim.tr_trim_last == B_FALSE);

	if (!partial) {
		return (DDI_DMA_MAPPED);
	}

	ASSERT(dma->dp_partial_required);
	return (DDI_DMA_PARTIAL_MAP);
}

/*
 * rootnex_setup_copybuf()
 *    Called in bind slowpath. Figures out if we're going to use the copy
 *    buffer, and if we do, sets up the basic state to handle it.
 */
static int
rootnex_setup_copybuf(ddi_dma_impl_t *hp, struct ddi_dma_req *dmareq,
    rootnex_dma_t *dma, ddi_dma_attr_t *attr)
{
	rootnex_sglinfo_t *sinfo;
	ddi_dma_attr_t lattr;
	size_t max_copybuf;
	int cansleep;
	int e;

	sinfo = &dma->dp_sglinfo;

	/* read this first so it's consistent through the routine  */
	max_copybuf = i_ddi_copybuf_size() & MMU_PAGEMASK;

	/* We need to call into the rootnex on ddi_dma_sync() */
	hp->dmai_rflags &= ~DMP_NOSYNC;

	/* make sure the copybuf size <= the max size */
	dma->dp_copybuf_size = MIN(sinfo->si_copybuf_req, max_copybuf);
	ASSERT((dma->dp_copybuf_size & MMU_PAGEOFFSET) == 0);

	/* convert the sleep flags */
	if (dmareq->dmar_fp == DDI_DMA_SLEEP) {
		cansleep = 1;
	} else {
		cansleep = 0;
	}

	/*
	 * Allocate the actual copy buffer. This needs to fit within the DMA
	 * engine limits, so we can't use kmem_alloc... We don't need
	 * contiguous memory (sgllen) since we will be forcing windows on
	 * sgllen anyway.
	 */
	lattr = *attr;
	lattr.dma_attr_align = MMU_PAGESIZE;
	/*
	 * this should be < 0 to indicate no limit, but due to a bug in
	 * the rootnex, we'll set it to the maximum positive int.
	 */
	lattr.dma_attr_sgllen = 0x7fffffff;
	/*
	 * if we're using the copy buffer because of seg, use that for our
	 * upper address limit.
	 */
	if (sinfo->si_bounce_on_seg) {
		lattr.dma_attr_addr_hi = lattr.dma_attr_seg;
	}
	e = i_ddi_mem_alloc(dma->dp_dip, &lattr, dma->dp_copybuf_size, cansleep,
	    0, NULL, &dma->dp_cbaddr, &dma->dp_cbsize, NULL);
	if (e != DDI_SUCCESS) {
		return (DDI_DMA_NORESOURCES);
	}

	ROOTNEX_DPROBE2(rootnex__alloc__copybuf, dev_info_t *, dma->dp_dip,
	    size_t, dma->dp_copybuf_size);

	return (DDI_SUCCESS);
}


/*
 * rootnex_setup_windows()
 *    Called in bind slowpath to setup the window state. We always have windows
 *    in the slowpath. Even if the window count = 1.
 */
static int
rootnex_setup_windows(ddi_dma_impl_t *hp, rootnex_dma_t *dma,
    ddi_dma_attr_t *attr, ddi_dma_obj_t *dmao, int kmflag)
{
	rootnex_window_t *windowp;
	rootnex_sglinfo_t *sinfo;
	size_t copy_state_size;
	size_t win_state_size;
	size_t state_available;
	size_t space_needed;
	uint_t copybuf_win;
	uint_t maxxfer_win;
	size_t space_used;
	uint_t sglwin;


	sinfo = &dma->dp_sglinfo;

	dma->dp_current_win = 0;
	hp->dmai_nwin = 0;

	/* If we don't need to do a partial, we only have one window */
	if (!dma->dp_partial_required) {
		dma->dp_max_win = 1;

	/*
	 * we need multiple windows, need to figure out the worse case number
	 * of windows.
	 */
	} else {
		/*
		 * if we need windows because we need more copy buffer that
		 * we allow, the worse case number of windows we could need
		 * here would be (copybuf space required / copybuf space that
		 * we have) plus one for remainder, and plus 2 to handle the
		 * extra pages on the trim for the first and last pages of the
		 * buffer (a page is the minimum window size so under the right
		 * attr settings, you could have a window for each page).
		 * The last page will only be hit here if the size is not a
		 * multiple of the granularity (which theoretically shouldn't
		 * be the case but never has been enforced, so we could have
		 * broken things without it).
		 */
		if (sinfo->si_copybuf_req > dma->dp_copybuf_size) {
			ASSERT(dma->dp_copybuf_size > 0);
			copybuf_win = (sinfo->si_copybuf_req /
			    dma->dp_copybuf_size) + 1 + 2;
		} else {
			copybuf_win = 0;
		}

		/*
		 * if we need windows because we have more cookies than the H/W
		 * can handle, the number of windows we would need here would
		 * be (cookie count / cookies count H/W supports minus 1[for
		 * trim]) plus one for remainder.
		 */
		if (attr->dma_attr_sgllen < sinfo->si_sgl_size) {
			sglwin = (sinfo->si_sgl_size /
			    (attr->dma_attr_sgllen - 1)) + 1;
		} else {
			sglwin = 0;
		}

		/*
		 * if we need windows because we're binding more memory than the
		 * H/W can transfer at once, the number of windows we would need
		 * here would be (xfer count / max xfer H/W supports) plus one
		 * for remainder, and plus 2 to handle the extra pages on the
		 * trim (see above comment about trim)
		 */
		if (dmao->dmao_size > dma->dp_maxxfer) {
			maxxfer_win = (dmao->dmao_size /
			    dma->dp_maxxfer) + 1 + 2;
		} else {
			maxxfer_win = 0;
		}
		dma->dp_max_win =  copybuf_win + sglwin + maxxfer_win;
		ASSERT(dma->dp_max_win > 0);
	}
	win_state_size = dma->dp_max_win * sizeof (rootnex_window_t);

	/*
	 * Get space for window and potential copy buffer state. Before we
	 * go and allocate memory, see if we can get away with using what's
	 * left in the pre-allocted state or the dynamically allocated sgl.
	 */
	space_used = (uintptr_t)(sinfo->si_sgl_size *
	    sizeof (ddi_dma_cookie_t));

	/* if we dynamically allocated space for the cookies */
	if (dma->dp_need_to_free_cookie) {
		/* if we have more space in the pre-allocted buffer, use it */
		ASSERT(space_used <= dma->dp_cookie_size);
		if ((dma->dp_cookie_size - space_used) <=
		    rootnex_state->r_prealloc_size) {
			state_available = rootnex_state->r_prealloc_size;
			windowp = (rootnex_window_t *)dma->dp_prealloc_buffer;

		/*
		 * else, we have more free space in the dynamically allocated
		 * buffer, i.e. the buffer wasn't worse case fragmented so we
		 * didn't need a lot of cookies.
		 */
		} else {
			state_available = dma->dp_cookie_size - space_used;
			windowp = (rootnex_window_t *)
			    &dma->dp_cookies[sinfo->si_sgl_size];
		}

	/* we used the pre-alloced buffer */
	} else {
		ASSERT(space_used <= rootnex_state->r_prealloc_size);
		state_available = rootnex_state->r_prealloc_size - space_used;
		windowp = (rootnex_window_t *)
		    &dma->dp_cookies[sinfo->si_sgl_size];
	}

	/*
	 * figure out how much state we need to track the copy buffer. Add an
	 * addition 8 bytes for pointer alignemnt later.
	 */
	if (dma->dp_copybuf_size > 0) {
		copy_state_size = sinfo->si_max_pages *
		    sizeof (rootnex_pgmap_t);
	} else {
		copy_state_size = 0;
	}
	/* add an additional 8 bytes for pointer alignment */
	space_needed = win_state_size + copy_state_size + 0x8;

	/* if we have enough space already, use it */
	if (state_available >= space_needed) {
		dma->dp_window = windowp;
		dma->dp_need_to_free_window = B_FALSE;

	/* not enough space, need to allocate more. */
	} else {
		dma->dp_window = kmem_alloc(space_needed, kmflag);
		if (dma->dp_window == NULL) {
			return (DDI_DMA_NORESOURCES);
		}
		dma->dp_need_to_free_window = B_TRUE;
		dma->dp_window_size = space_needed;
		ROOTNEX_DPROBE2(rootnex__bind__sp__alloc, dev_info_t *,
		    dma->dp_dip, size_t, space_needed);
	}

	/*
	 * we allocate copy buffer state and window state at the same time.
	 * setup our copy buffer state pointers. Make sure it's aligned.
	 */
	if (dma->dp_copybuf_size > 0) {
		dma->dp_pgmap = (rootnex_pgmap_t *)(((uintptr_t)
		    &dma->dp_window[dma->dp_max_win] + 0x7) & ~0x7);
	} else {
		dma->dp_pgmap = NULL;
	}

	return (DDI_SUCCESS);
}


/*
 * rootnex_teardown_copybuf()
 *    cleans up after rootnex_setup_copybuf()
 */
static void
rootnex_teardown_copybuf(rootnex_dma_t *dma)
{
	/* if we allocated a copy buffer, free it */
	if (dma->dp_cbaddr != NULL) {
		i_ddi_mem_free(dma->dp_cbaddr, NULL);
	}
}


/*
 * rootnex_teardown_windows()
 *    cleans up after rootnex_setup_windows()
 */
static void
rootnex_teardown_windows(rootnex_dma_t *dma)
{
	/*
	 * if we had to allocate window state on the last bind (because we
	 * didn't have enough pre-allocated space in the handle), free it.
	 */
	if (dma->dp_need_to_free_window) {
		kmem_free(dma->dp_window, dma->dp_window_size);
	}
}


/*
 * rootnex_init_win()
 *    Called in bind slow path during creation of a new window. Initializes
 *    window state to default values.
 */
/*ARGSUSED*/
static void
rootnex_init_win(ddi_dma_impl_t *hp, rootnex_dma_t *dma,
    rootnex_window_t *window, ddi_dma_cookie_t *cookie, off_t cur_offset)
{
	hp->dmai_nwin++;
	window->wd_dosync = B_FALSE;
	window->wd_offset = cur_offset;
	window->wd_size = 0;
	window->wd_first_cookie = cookie;
	window->wd_cookie_cnt = 0;
	window->wd_trim.tr_trim_first = B_FALSE;
	window->wd_trim.tr_trim_last = B_FALSE;
	window->wd_trim.tr_first_copybuf_win = B_FALSE;
	window->wd_trim.tr_last_copybuf_win = B_FALSE;
}


/*
 * rootnex_get_page_info()
 *    returns the page index, offset into the page, and size of the page from
 *    from an offset into the start of the dma obj buffer.
 */
static void
rootnex_get_page_info(rootnex_dma_t *dma, rootnex_sglinfo_t *sinfo,
    off_t offset, uint_t *pindex, uint_t *poffset, uint_t *psize)
{
	io_mvector_t *mvector;
	uint_t pidx;
	uint_t poff;
	off_t coff;
	int i;


	/*
	 * is this isn't an mvector, the offset and index just take the
	 * initial offset into the first page, and the offset into the
	 * buffer.
	 */
	if (dma->dp_dma.dmao_type != DMA_OTYP_MVECTOR) {
		poff = (sinfo->si_buf_offset + offset) & PAGEOFFSET;
		if (pindex != NULL)
			*pindex = mmu_btop(sinfo->si_buf_offset + offset);
		if (poffset != NULL)
			*poffset = poff;
		if (psize != NULL)
			*psize = PAGESIZE - poff;

		return;
	}

	/* this is a mvector */
	mvector = dma->dp_dma.dmao_obj.mvector;
	coff = offset;
	pidx = 0;

	/* loop through the io_vectors in the mvector */
	for (i = 0; i < mvector->im_iovec_cnt; i++) {
		io_vector_t *iovec;

		iovec = &mvector->im_vects[i];
		/* get the offset into the io_vector */
		if (iovec->iv_flags & IO_VECTOR_FLAG_KADDR) {
			poff = (uintptr_t)iovec->iv_kaddr & PAGEOFFSET;
		} else if (iovec->iv_flags & IO_VECTOR_FLAG_UVA) {
			poff = (uintptr_t)iovec->iv.ib_uva.iu_addr & PAGEOFFSET;
		} else {
			ASSERT(iovec->iv_flags & IO_VECTOR_FLAG_PLIST);
			poff = iovec->iv.ib_plist.ip_offset;
		}

		/*
		 * If the io_vector is less than our starting offset, go to
		 * the next io_vector (after adjusting offset and keeping track
		 * of how many pages we've seen so far).
		 */
		if (iovec->iv_len <= coff) {
			coff -= iovec->iv_len;
			pidx += mmu_btopr(poff + iovec->iv_len);

		/*
		 * we found the io_vector we were looking for, adjust the
		 * page index accounting for the initial offset into the
		 * io_vector (poff) and the remaining buffer offset (coff).
		 */
		} else {
			pidx += mmu_btop(poff + coff);
			break;
		}
	}
	ASSERT(i < mvector->im_iovec_cnt);
	if (pindex != NULL)
		*pindex = pidx;

	poff = (poff + coff) & PAGEOFFSET;
	if (poffset != NULL)
		*poffset = poff;

	if (psize != NULL)
		*psize = MIN((PAGESIZE - poff),
		    (mvector->im_vects[i].iv_len - coff));
}


/*
 * rootnex_get_mvector_kaddr()
 *    Find the page in the mvector at offset "offset:, and return a page
 *    aligned kernel VA for it.
 */
static int
rootnex_get_mvector_kaddr(rootnex_dma_t *dma, int kmflags, off_t offset,
    caddr_t *kaddr)
{
	io_mvector_t *mvector;
	io_vector_t *iovec;
	int e;
	int i;


	/*
	 * mapin the mvector. io_mvector_mapin() will track if we have already
	 * mapped it in. We have implicit knowledge that the VM_*SLEEP flags
	 * match the KM_*SLEEP flags.
	 */
	mvector = dma->dp_dma.dmao_obj.mvector;
	e = io_mvector_mapin(mvector, kmflags & VM_KMFLAGS);
	if (e != 0)
		return (DDI_FAILURE);

	/* loop through the io_vectors in the mvector */
	for (i = 0; i < mvector->im_iovec_cnt; i++) {
		iovec = &mvector->im_vects[i];

		/*
		 * if we found the io vector we are looking for, break out of
		 * the loop.
		 */
		if (iovec->iv_len > offset)
			break;

		/* offset not in this io_vector, go through the loop again */
		offset -= iovec->iv_len;
	}
	ASSERT(i < mvector->im_iovec_cnt);

	/*
	 * XXX - we should improve this for pp list and uva to use kpm when
	 * we are in a 64-bit kernel.
	 */
	*kaddr = (caddr_t)(((uintptr_t)iovec->iv_kaddr + offset) & PAGEMASK);

	return (DDI_SUCCESS);
}


/*
 * rootnex_setup_cookie()
 *    Called in the bind slow path when the sgl uses the copy buffer. If any of
 *    the sgl uses the copy buffer, we need to go through each cookie, figure
 *    out if it uses the copy buffer, and if it does, save away everything we'll
 *    need during sync.
 */
static int
rootnex_setup_cookie(rootnex_dma_t *dma, int kmflags, ddi_dma_cookie_t *cookie,
    off_t cur_offset, size_t *copybuf_used, page_t **cur_pp)
{
	boolean_t copybuf_sz_power_2;
	rootnex_sglinfo_t *sinfo;
	paddr_t paddr;
	uint_t pidx;
	uint_t pcnt;
	uint_t poff;
	pfn_t pfn;

	ASSERT(dma->dp_dma.dmao_type != DMA_OTYP_DVADDR);

	sinfo = &dma->dp_sglinfo;

	/*
	 * get the page index of where the cookie starts relative to the entire
	 * buffer and the offset into the page.
	 */
	rootnex_get_page_info(dma, sinfo, cur_offset, &pidx, &poff,
	    NULL);
	ASSERT(pidx < sinfo->si_max_pages);
	ASSERT(poff == (cookie->dmac_laddress & MMU_PAGEOFFSET));

	/* if this cookie uses the copy buffer */
	if (cookie->dmac_type & ROOTNEX_USES_COPYBUF) {
		/*
		 * NOTE: we know that since this cookie uses the copy buffer, it
		 * is <= MMU_PAGESIZE.
		 */

		/*
		 * For the 64-bit kernel, get the pfn which we'll use with
		 * seg kpm.
		 */
		poff = cookie->dmac_laddress & MMU_PAGEOFFSET;
		/* mfn_to_pfn() is a NOP on i86pc */
		pfn = mfn_to_pfn(cookie->dmac_laddress >> MMU_PAGESHIFT);

		/* figure out if the copybuf size is a power of 2 */
		if (dma->dp_copybuf_size & (dma->dp_copybuf_size - 1)) {
			copybuf_sz_power_2 = B_FALSE;
		} else {
			copybuf_sz_power_2 = B_TRUE;
		}

		/* This page uses the copy buffer */
		dma->dp_pgmap[pidx].pm_uses_copybuf = B_TRUE;

		/*
		 * save the copy buffer KVA that we'll use with this page.
		 * if we still fit within the copybuf, it's a simple add.
		 * otherwise, we need to wrap over using & or % accordingly.
		 */
		if ((*copybuf_used + MMU_PAGESIZE) <= dma->dp_copybuf_size) {
			dma->dp_pgmap[pidx].pm_cbaddr = dma->dp_cbaddr +
			    *copybuf_used;
		} else {
			if (copybuf_sz_power_2) {
				dma->dp_pgmap[pidx].pm_cbaddr = (caddr_t)(
				    (uintptr_t)dma->dp_cbaddr +
				    (*copybuf_used &
				    (dma->dp_copybuf_size - 1)));
			} else {
				dma->dp_pgmap[pidx].pm_cbaddr = (caddr_t)(
				    (uintptr_t)dma->dp_cbaddr +
				    (*copybuf_used % dma->dp_copybuf_size));
			}
		}

		/*
		 * over write the cookie physical address with the address of
		 * the physical address of the copy buffer page that we will
		 * use.
		 */
		paddr = pfn_to_pa(hat_getpfnum(kas.a_hat,
		    dma->dp_pgmap[pidx].pm_cbaddr)) + poff;

		cookie->dmac_laddress = paddr;

		/*
		 * if we have a mvector, we will use the mvector code to
		 * ensure we always have a kaddr.
		 */
		if (dma->dp_dma.dmao_type == DMA_OTYP_MVECTOR) {
			int e;
			e = rootnex_get_mvector_kaddr(dma, kmflags, cur_offset,
			    &dma->dp_pgmap[pidx].pm_kaddr);
			if (e != DDI_SUCCESS)
				return (e);


		/* if we have a kernel VA, it's easy, just save that address */
		} else if ((dma->dp_dma.dmao_type != DMA_OTYP_PAGES) &&
		    (sinfo->si_asp == &kas)) {
			/*
			 * save away the page aligned virtual address of the
			 * driver buffer. Offsets are handled in the sync code.
			 */
			dma->dp_pgmap[pidx].pm_kaddr = (caddr_t)(((uintptr_t)
			    dma->dp_dma.dmao_obj.virt_obj.v_addr + cur_offset)
			    & MMU_PAGEMASK);
		/* we don't have a kernel VA. We need one for the bcopy. */
		} else {
			/*
			 * for the 64-bit kernel, it's easy. We use seg kpm to
			 * get a Kernel VA for the corresponding pfn.
			 */
			dma->dp_pgmap[pidx].pm_kaddr = hat_kpm_pfn2va(pfn);
			/* go to the next page_t */
			if (dma->dp_dma.dmao_type == DMA_OTYP_PAGES) {
				*cur_pp = (*cur_pp)->p_next;
			}
		}

		/* add to the copy buffer count */
		*copybuf_used += MMU_PAGESIZE;

	/*
	 * This cookie doesn't use the copy buffer. Walk through the pages this
	 * cookie occupies to reflect this.
	 */
	} else {
		/*
		 * figure out how many pages the cookie occupies. We need to
		 * use the offset into the first page of the cookie and the
		 * cookie size to do this.
		 */
		pcnt = mmu_btopr(cookie->dmac_size + poff);

		while (pcnt > 0) {
			dma->dp_pgmap[pidx].pm_uses_copybuf = B_FALSE;

			/*
			 * we need to update pidx and cur_pp or we'll lose
			 * track of where we are.
			 */
			if (dma->dp_dma.dmao_type == DMA_OTYP_PAGES) {
				*cur_pp = (*cur_pp)->p_next;
			}
			pidx++;
			pcnt--;
		}
	}

	return (DDI_SUCCESS);
}


/*
 * rootnex_sgllen_window_boundary()
 *    Called in the bind slow path when the next cookie causes us to exceed (in
 *    this case == since we start at 0 and sgllen starts at 1) the maximum sgl
 *    length supported by the DMA H/W.
 */
static int
rootnex_sgllen_window_boundary(ddi_dma_impl_t *hp, rootnex_dma_t *dma,
    rootnex_window_t **windowp, ddi_dma_cookie_t *cookie, ddi_dma_attr_t *attr,
    off_t cur_offset)
{
	off_t new_offset;
	size_t trim_sz;
	off_t coffset;


	/*
	 * if we know we'll never have to trim, it's pretty easy. Just move to
	 * the next window and init it. We're done.
	 */
	if (!dma->dp_trim_required) {
		(*windowp)++;
		rootnex_init_win(hp, dma, *windowp, cookie, cur_offset);
		(*windowp)->wd_cookie_cnt++;
		(*windowp)->wd_size = cookie->dmac_size;
		return (DDI_SUCCESS);
	}

	/* figure out how much we need to trim from the window */
	ASSERT(attr->dma_attr_granular != 0);
	if (dma->dp_granularity_power_2) {
		trim_sz = (*windowp)->wd_size & (attr->dma_attr_granular - 1);
	} else {
		trim_sz = (*windowp)->wd_size % attr->dma_attr_granular;
	}

	/* The window's a whole multiple of granularity. We're done */
	if (trim_sz == 0) {
		(*windowp)++;
		rootnex_init_win(hp, dma, *windowp, cookie, cur_offset);
		(*windowp)->wd_cookie_cnt++;
		(*windowp)->wd_size = cookie->dmac_size;
		return (DDI_SUCCESS);
	}

	/*
	 * The window's not a whole multiple of granularity, since we know this
	 * is due to the sgllen, we need to go back to the last cookie and trim
	 * that one, add the left over part of the old cookie into the new
	 * window, and then add in the new cookie into the new window.
	 */

	/*
	 * make sure the driver isn't making us do something bad... Trimming and
	 * sgllen == 1 don't go together.
	 */
	if (attr->dma_attr_sgllen == 1) {
		return (DDI_DMA_NOMAPPING);
	}

	/*
	 * first, setup the current window to account for the trim. Need to go
	 * back to the last cookie for this.
	 */
	cookie--;
	(*windowp)->wd_trim.tr_trim_last = B_TRUE;
	(*windowp)->wd_trim.tr_last_cookie = cookie;
	(*windowp)->wd_trim.tr_last_paddr = cookie->dmac_laddress;
	ASSERT(cookie->dmac_size > trim_sz);
	(*windowp)->wd_trim.tr_last_size = cookie->dmac_size - trim_sz;
	(*windowp)->wd_size -= trim_sz;

	/* save the buffer offsets for the next window */
	coffset = cookie->dmac_size - trim_sz;
	new_offset = (*windowp)->wd_offset + (*windowp)->wd_size;

	/*
	 * set this now in case this is the first window. all other cases are
	 * set in dma_win()
	 */
	cookie->dmac_size = (*windowp)->wd_trim.tr_last_size;

	/*
	 * initialize the next window using what's left over in the previous
	 * cookie.
	 */
	(*windowp)++;
	rootnex_init_win(hp, dma, *windowp, cookie, new_offset);
	(*windowp)->wd_cookie_cnt++;
	(*windowp)->wd_trim.tr_trim_first = B_TRUE;
	(*windowp)->wd_trim.tr_first_paddr = cookie->dmac_laddress + coffset;
	(*windowp)->wd_trim.tr_first_size = trim_sz;
	if (cookie->dmac_type & ROOTNEX_USES_COPYBUF) {
		(*windowp)->wd_dosync = B_TRUE;
	}

	/*
	 * now go back to the current cookie and add it to the new window. set
	 * the new window size to the what was left over from the previous
	 * cookie and what's in the current cookie.
	 */
	cookie++;
	(*windowp)->wd_cookie_cnt++;
	(*windowp)->wd_size = trim_sz + cookie->dmac_size;

	/*
	 * trim plus the next cookie could put us over maxxfer (a cookie can be
	 * a max size of maxxfer). Handle that case.
	 */
	if ((*windowp)->wd_size > dma->dp_maxxfer) {
		/*
		 * maxxfer is already a whole multiple of granularity, and this
		 * trim will be <= the previous trim (since a cookie can't be
		 * larger than maxxfer). Make things simple here.
		 */
		trim_sz = (*windowp)->wd_size - dma->dp_maxxfer;
		(*windowp)->wd_trim.tr_trim_last = B_TRUE;
		(*windowp)->wd_trim.tr_last_cookie = cookie;
		(*windowp)->wd_trim.tr_last_paddr = cookie->dmac_laddress;
		(*windowp)->wd_trim.tr_last_size = cookie->dmac_size - trim_sz;
		(*windowp)->wd_size -= trim_sz;
		ASSERT((*windowp)->wd_size == dma->dp_maxxfer);

		/* save the buffer offsets for the next window */
		coffset = cookie->dmac_size - trim_sz;
		new_offset = (*windowp)->wd_offset + (*windowp)->wd_size;

		/* setup the next window */
		(*windowp)++;
		rootnex_init_win(hp, dma, *windowp, cookie, new_offset);
		(*windowp)->wd_cookie_cnt++;
		(*windowp)->wd_trim.tr_trim_first = B_TRUE;
		(*windowp)->wd_trim.tr_first_paddr = cookie->dmac_laddress +
		    coffset;
		(*windowp)->wd_trim.tr_first_size = trim_sz;
	}

	return (DDI_SUCCESS);
}


/*
 * rootnex_copybuf_window_boundary()
 *    Called in bind slowpath when we get to a window boundary because we used
 *    up all the copy buffer that we have.
 */
static int
rootnex_copybuf_window_boundary(ddi_dma_impl_t *hp, rootnex_dma_t *dma,
    rootnex_window_t **windowp, ddi_dma_cookie_t *cookie, off_t cur_offset,
    size_t *copybuf_used)
{
	rootnex_sglinfo_t *sinfo;
	off_t new_offset;
	size_t trim_sz;
	paddr_t paddr;
	off_t coffset;
	uint_t pidx;
	off_t poff;


	sinfo = &dma->dp_sglinfo;

	/*
	 * the copy buffer should be a whole multiple of page size. We know that
	 * this cookie is <= MMU_PAGESIZE.
	 */
	ASSERT(cookie->dmac_size <= MMU_PAGESIZE);

	/* reset copybuf used */
	*copybuf_used = 0;

	/*
	 * if we don't have to trim (since granularity is set to 1), go to the
	 * next window and add the current cookie to it. We know the current
	 * cookie uses the copy buffer since we're in this code path.
	 */
	if (!dma->dp_trim_required) {
		(*windowp)++;
		rootnex_init_win(hp, dma, *windowp, cookie, cur_offset);

		/* Add this cookie to the new window */
		(*windowp)->wd_cookie_cnt++;
		(*windowp)->wd_size += cookie->dmac_size;
		*copybuf_used += MMU_PAGESIZE;
		return (DDI_SUCCESS);
	}

	/*
	 * *** may need to trim, figure it out.
	 */

	/* figure out how much we need to trim from the window */
	if (dma->dp_granularity_power_2) {
		trim_sz = (*windowp)->wd_size &
		    (hp->dmai_attr.dma_attr_granular - 1);
	} else {
		trim_sz = (*windowp)->wd_size % hp->dmai_attr.dma_attr_granular;
	}

	/*
	 * if the window's a whole multiple of granularity, go to the next
	 * window, init it, then add in the current cookie. We know the current
	 * cookie uses the copy buffer since we're in this code path.
	 */
	if (trim_sz == 0) {
		(*windowp)++;
		rootnex_init_win(hp, dma, *windowp, cookie, cur_offset);

		/* Add this cookie to the new window */
		(*windowp)->wd_cookie_cnt++;
		(*windowp)->wd_size += cookie->dmac_size;
		*copybuf_used += MMU_PAGESIZE;
		return (DDI_SUCCESS);
	}

	/*
	 * *** We figured it out, we definitly need to trim
	 */

	/*
	 * make sure the driver isn't making us do something bad...
	 * Trimming and sgllen == 1 don't go together.
	 */
	if (hp->dmai_attr.dma_attr_sgllen == 1) {
		return (DDI_DMA_NOMAPPING);
	}

	/*
	 * first, setup the current window to account for the trim. Need to go
	 * back to the last cookie for this. Some of the last cookie will be in
	 * the current window, and some of the last cookie will be in the new
	 * window. All of the current cookie will be in the new window.
	 */
	cookie--;
	(*windowp)->wd_trim.tr_trim_last = B_TRUE;
	(*windowp)->wd_trim.tr_last_cookie = cookie;
	(*windowp)->wd_trim.tr_last_paddr = cookie->dmac_laddress;
	ASSERT(cookie->dmac_size > trim_sz);
	(*windowp)->wd_trim.tr_last_size = cookie->dmac_size - trim_sz;
	(*windowp)->wd_size -= trim_sz;

	/*
	 * we're trimming the last cookie (not the current cookie). So that
	 * last cookie may have or may not have been using the copy buffer (
	 * we know the cookie passed in uses the copy buffer since we're in
	 * this code path).
	 *
	 * If the last cookie doesn't use the copy buffer, nothing special to
	 * do. However, if it does uses the copy buffer, it will be both the
	 * last page in the current window and the first page in the next
	 * window. Since we are reusing the copy buffer (and KVA space on the
	 * 32-bit kernel), this page will use the end of the copy buffer in the
	 * current window, and the start of the copy buffer in the next window.
	 * Track that info... The cookie physical address was already set to
	 * the copy buffer physical address in setup_cookie..
	 */
	if (cookie->dmac_type & ROOTNEX_USES_COPYBUF) {
		rootnex_get_page_info(dma, sinfo,
		    ((*windowp)->wd_offset + (*windowp)->wd_size),
		    &pidx, NULL, NULL);
		(*windowp)->wd_trim.tr_last_copybuf_win = B_TRUE;
		(*windowp)->wd_trim.tr_last_pidx = pidx;
		(*windowp)->wd_trim.tr_last_cbaddr =
		    dma->dp_pgmap[pidx].pm_cbaddr;
	}

	/* save the buffer offsets for the next window */
	coffset = cookie->dmac_size - trim_sz;
	new_offset = (*windowp)->wd_offset + (*windowp)->wd_size;

	/*
	 * set this now in case this is the first window. all other cases are
	 * set in dma_win()
	 */
	cookie->dmac_size = (*windowp)->wd_trim.tr_last_size;

	/*
	 * initialize the next window using what's left over in the previous
	 * cookie.
	 */
	(*windowp)++;
	rootnex_init_win(hp, dma, *windowp, cookie, new_offset);
	(*windowp)->wd_cookie_cnt++;
	(*windowp)->wd_trim.tr_trim_first = B_TRUE;
	(*windowp)->wd_trim.tr_first_paddr = cookie->dmac_laddress + coffset;
	(*windowp)->wd_trim.tr_first_size = trim_sz;

	/*
	 * again, we're tracking if the last cookie uses the copy buffer.
	 * read the comment above for more info on why we need to track
	 * additional state.
	 *
	 * For the first cookie in the new window, we need reset the physical
	 * address to DMA into to the start of the copy buffer plus any
	 * initial page offset which may be present.
	 */
	if (cookie->dmac_type & ROOTNEX_USES_COPYBUF) {
		(*windowp)->wd_dosync = B_TRUE;
		(*windowp)->wd_trim.tr_first_copybuf_win = B_TRUE;
		(*windowp)->wd_trim.tr_first_pidx = pidx;
		(*windowp)->wd_trim.tr_first_cbaddr = dma->dp_cbaddr;
		poff = (*windowp)->wd_trim.tr_first_paddr & MMU_PAGEOFFSET;

		paddr = pfn_to_pa(hat_getpfnum(kas.a_hat, dma->dp_cbaddr)) +
		    poff;
		(*windowp)->wd_trim.tr_first_paddr =
		    paddr;

		/* account for the cookie copybuf usage in the new window */
		*copybuf_used += MMU_PAGESIZE;

		/*
		 * every piece of code has to have a hack, and here is this
		 * ones :-)
		 *
		 * There is a complex interaction between setup_cookie and the
		 * copybuf window boundary. The complexity had to be in either
		 * the maxxfer window, or the copybuf window, and I chose the
		 * copybuf code.
		 *
		 * So in this code path, we have taken the last cookie,
		 * virtually broken it in half due to the trim, and it happens
		 * to use the copybuf which further complicates life. At the
		 * same time, we have already setup the current cookie, which
		 * is now wrong. More background info: the current cookie uses
		 * the copybuf, so it is only a page long max. So we need to
		 * fix the current cookies copy buffer address, physical
		 * address, and kva for the 32-bit kernel. We due this by
		 * bumping them by page size (of course, we can't due this on
		 * the physical address since the copy buffer may not be
		 * physically contiguous).
		 */
		cookie++;
		dma->dp_pgmap[pidx + 1].pm_cbaddr += MMU_PAGESIZE;
		poff = cookie->dmac_laddress & MMU_PAGEOFFSET;

		paddr = pfn_to_pa(hat_getpfnum(kas.a_hat,
		    dma->dp_pgmap[pidx + 1].pm_cbaddr)) + poff;
		cookie->dmac_laddress = paddr;
	} else {
		/* go back to the current cookie */
		cookie++;
	}

	/*
	 * add the current cookie to the new window. set the new window size to
	 * the what was left over from the previous cookie and what's in the
	 * current cookie.
	 */
	(*windowp)->wd_cookie_cnt++;
	(*windowp)->wd_size = trim_sz + cookie->dmac_size;
	ASSERT((*windowp)->wd_size < dma->dp_maxxfer);

	/*
	 * we know that the cookie passed in always uses the copy buffer. We
	 * wouldn't be here if it didn't.
	 */
	*copybuf_used += MMU_PAGESIZE;

	return (DDI_SUCCESS);
}


/*
 * rootnex_maxxfer_window_boundary()
 *    Called in bind slowpath when we get to a window boundary because we will
 *    go over maxxfer.
 */
static int
rootnex_maxxfer_window_boundary(ddi_dma_impl_t *hp, rootnex_dma_t *dma,
    rootnex_window_t **windowp, ddi_dma_cookie_t *cookie)
{
	size_t dmac_size;
	off_t new_offset;
	size_t trim_sz;
	off_t coffset;


	/*
	 * calculate how much we have to trim off of the current cookie to equal
	 * maxxfer. We don't have to account for granularity here since our
	 * maxxfer already takes that into account.
	 */
	trim_sz = ((*windowp)->wd_size + cookie->dmac_size) - dma->dp_maxxfer;
	ASSERT(trim_sz <= cookie->dmac_size);
	ASSERT(trim_sz <= dma->dp_maxxfer);

	/* save cookie size since we need it later and we might change it */
	dmac_size = cookie->dmac_size;

	/*
	 * if we're not trimming the entire cookie, setup the current window to
	 * account for the trim.
	 */
	if (trim_sz < cookie->dmac_size) {
		(*windowp)->wd_cookie_cnt++;
		(*windowp)->wd_trim.tr_trim_last = B_TRUE;
		(*windowp)->wd_trim.tr_last_cookie = cookie;
		(*windowp)->wd_trim.tr_last_paddr = cookie->dmac_laddress;
		(*windowp)->wd_trim.tr_last_size = cookie->dmac_size - trim_sz;
		(*windowp)->wd_size = dma->dp_maxxfer;

		/*
		 * set the adjusted cookie size now in case this is the first
		 * window. All other windows are taken care of in get win
		 */
		cookie->dmac_size = (*windowp)->wd_trim.tr_last_size;
	}

	/*
	 * coffset is the current offset within the cookie, new_offset is the
	 * current offset with the entire buffer.
	 */
	coffset = dmac_size - trim_sz;
	new_offset = (*windowp)->wd_offset + (*windowp)->wd_size;

	/* initialize the next window */
	(*windowp)++;
	rootnex_init_win(hp, dma, *windowp, cookie, new_offset);
	(*windowp)->wd_cookie_cnt++;
	(*windowp)->wd_size = trim_sz;
	if (trim_sz < dmac_size) {
		(*windowp)->wd_trim.tr_trim_first = B_TRUE;
		(*windowp)->wd_trim.tr_first_paddr = cookie->dmac_laddress +
		    coffset;
		(*windowp)->wd_trim.tr_first_size = trim_sz;
	}

	return (DDI_SUCCESS);
}


/*ARGSUSED*/
static int
rootnex_dma_sync(dev_info_t *dip, dev_info_t *rdip, ddi_dma_handle_t handle,
    off_t off, size_t len, uint_t cache_flags)
{
	rootnex_sglinfo_t *sinfo;
	rootnex_pgmap_t *cbpage;
	rootnex_window_t *win;
	ddi_dma_impl_t *hp;
	rootnex_dma_t *dma;
	caddr_t fromaddr;
	caddr_t toaddr;
	uint_t psize;
	off_t offset;
	uint_t pidx;
	size_t size;
	uint_t poff;
	int e;


	hp = (ddi_dma_impl_t *)handle;
	dma = (rootnex_dma_t *)hp->dmai_private;
	sinfo = &dma->dp_sglinfo;

	/*
	 * if we don't have any windows, we don't need to sync. A copybuf
	 * will cause us to have at least one window.
	 */
	if (dma->dp_window == NULL) {
		return (DDI_SUCCESS);
	}

	/* This window may not need to be sync'd */
	win = &dma->dp_window[dma->dp_current_win];
	if (!win->wd_dosync) {
		return (DDI_SUCCESS);
	}

	/* handle off and len special cases */
	if ((off == 0) || (rootnex_sync_ignore_params)) {
		offset = win->wd_offset;
	} else {
		offset = off;
	}
	if ((len == 0) || (rootnex_sync_ignore_params)) {
		size = win->wd_size;
	} else {
		size = len;
	}

	/* check the sync args to make sure they make a little sense */
	if (rootnex_sync_check_parms) {
		e = rootnex_valid_sync_parms(hp, win, offset, size,
		    cache_flags);
		if (e != DDI_SUCCESS) {
			ROOTNEX_DPROF_INC(&rootnex_cnt[ROOTNEX_CNT_SYNC_FAIL]);
			return (DDI_FAILURE);
		}
	}

	/* go through all the pages that we want to sync */
	while (size > 0) {
		/* get the page index relative to the start of the buffer */
		rootnex_get_page_info(dma, sinfo, offset, &pidx, &poff, &psize);
		psize = MIN(psize, size);
		ASSERT(pidx < sinfo->si_max_pages);

		/*
		 * if this page uses the copy buffer, we need to sync it,
		 * otherwise, go on to the next page.
		 */
		cbpage = &dma->dp_pgmap[pidx];
		ASSERT((cbpage->pm_uses_copybuf == B_TRUE) ||
		    (cbpage->pm_uses_copybuf == B_FALSE));
		if (cbpage->pm_uses_copybuf) {
			/* cbaddr and kaddr should be page aligned */
			ASSERT(((uintptr_t)cbpage->pm_cbaddr &
			    MMU_PAGEOFFSET) == 0);
			ASSERT(((uintptr_t)cbpage->pm_kaddr &
			    MMU_PAGEOFFSET) == 0);

			/*
			 * if we're copying for the device, we are going to
			 * copy from the drivers buffer and to the rootnex
			 * allocated copy buffer.
			 */
			if (cache_flags == DDI_DMA_SYNC_FORDEV) {
				fromaddr = cbpage->pm_kaddr + poff;
				toaddr = cbpage->pm_cbaddr + poff;
				ROOTNEX_DPROBE2(rootnex__sync__dev,
				    dev_info_t *, dma->dp_dip, size_t, psize);

			/*
			 * if we're copying for the cpu/kernel, we are going to
			 * copy from the rootnex allocated copy buffer to the
			 * drivers buffer.
			 */
			} else {
				fromaddr = cbpage->pm_cbaddr + poff;
				toaddr = cbpage->pm_kaddr + poff;
				ROOTNEX_DPROBE2(rootnex__sync__cpu,
				    dev_info_t *, dma->dp_dip, size_t, psize);
			}

			bcopy(fromaddr, toaddr, psize);
		}

		/*
		 * decrement size until we're done, update our offset into the
		 * buffer, and get the next page size.
		 */
		size -= psize;
		offset += psize;
	}

	return (DDI_SUCCESS);
}

/*
 * rootnex_valid_sync_parms()
 *    checks the parameters passed to sync to verify they are correct.
 */
static int
rootnex_valid_sync_parms(ddi_dma_impl_t *hp, rootnex_window_t *win,
    off_t offset, size_t size, uint_t cache_flags)
{
	off_t woffset;


	/*
	 * the first part of the test to make sure the offset passed in is
	 * within the window.
	 */
	if (offset < win->wd_offset) {
		return (DDI_FAILURE);
	}

	/*
	 * second and last part of the test to make sure the offset and length
	 * passed in is within the window.
	 */
	woffset = offset - win->wd_offset;
	if ((woffset + size) > win->wd_size) {
		return (DDI_FAILURE);
	}

	/*
	 * if we are sync'ing for the device, the DDI_DMA_WRITE flag should
	 * be set too.
	 */
	if ((cache_flags == DDI_DMA_SYNC_FORDEV) &&
	    (hp->dmai_rflags & DDI_DMA_WRITE)) {
		return (DDI_SUCCESS);
	}

	/*
	 * at this point, either DDI_DMA_SYNC_FORCPU or DDI_DMA_SYNC_FORKERNEL
	 * should be set. Also DDI_DMA_READ should be set in the flags.
	 */
	if (((cache_flags == DDI_DMA_SYNC_FORCPU) ||
	    (cache_flags == DDI_DMA_SYNC_FORKERNEL)) &&
	    (hp->dmai_rflags & DDI_DMA_READ)) {
		return (DDI_SUCCESS);
	}

	return (DDI_FAILURE);
}


/*ARGSUSED*/
static int
rootnex_dma_win(dev_info_t *dip, dev_info_t *rdip, ddi_dma_handle_t handle,
    uint_t win, off_t *offp, size_t *lenp, ddi_dma_cookie_t *cookiep,
    uint_t *ccountp)
{
	rootnex_window_t *window;
	rootnex_trim_t *trim;
	ddi_dma_impl_t *hp;
	rootnex_dma_t *dma;
	ddi_dma_obj_t *dmao;

	hp = (ddi_dma_impl_t *)handle;
	dma = (rootnex_dma_t *)hp->dmai_private;

	/* If we try and get a window which doesn't exist, return failure */
	if (win >= hp->dmai_nwin) {
		ROOTNEX_DPROF_INC(&rootnex_cnt[ROOTNEX_CNT_GETWIN_FAIL]);
		return (DDI_FAILURE);
	}

	dmao = &dma->dp_dma;

	/*
	 * if we don't have any windows, and they're asking for the first
	 * window, setup the cookie pointer to the first cookie in the bind.
	 * setup our return values, then increment the cookie since we return
	 * the first cookie on the stack.
	 */
	if (dma->dp_window == NULL) {
		if (win != 0) {
			ROOTNEX_DPROF_INC(
			    &rootnex_cnt[ROOTNEX_CNT_GETWIN_FAIL]);
			return (DDI_FAILURE);
		}
		hp->dmai_cookie = dma->dp_cookies;
		*offp = 0;
		*lenp = dmao->dmao_size;
		*ccountp = dma->dp_sglinfo.si_sgl_size;
		*cookiep = hp->dmai_cookie[0];
		hp->dmai_cookie++;
		return (DDI_SUCCESS);
	}

	/* sync the old window before moving on to the new one */
	window = &dma->dp_window[dma->dp_current_win];
	if ((window->wd_dosync) && (hp->dmai_rflags & DDI_DMA_READ)) {
		(void) rootnex_dma_sync(dip, rdip, handle, 0, 0,
		    DDI_DMA_SYNC_FORCPU);
	}

	/*
	 * Move to the new window.
	 * NOTE: current_win must be set for sync to work right
	 */
	dma->dp_current_win = win;
	window = &dma->dp_window[win];

	/* if needed, adjust the first and/or last cookies for trim */
	trim = &window->wd_trim;
	if (trim->tr_trim_first) {
		window->wd_first_cookie->dmac_laddress = trim->tr_first_paddr;
		window->wd_first_cookie->dmac_size = trim->tr_first_size;
		if (trim->tr_first_copybuf_win) {
			dma->dp_pgmap[trim->tr_first_pidx].pm_cbaddr =
			    trim->tr_first_cbaddr;
		}
	}
	if (trim->tr_trim_last) {
		trim->tr_last_cookie->dmac_laddress = trim->tr_last_paddr;
		trim->tr_last_cookie->dmac_size = trim->tr_last_size;
		if (trim->tr_last_copybuf_win) {
			dma->dp_pgmap[trim->tr_last_pidx].pm_cbaddr =
			    trim->tr_last_cbaddr;
		}
	}

	/*
	 * setup the cookie pointer to the first cookie in the window. setup
	 * our return values, then increment the cookie since we return the
	 * first cookie on the stack.
	 */
	hp->dmai_cookie = window->wd_first_cookie;
	*offp = window->wd_offset;
	*lenp = window->wd_size;
	*ccountp = window->wd_cookie_cnt;
	*cookiep = hp->dmai_cookie[0];
	hp->dmai_cookie++;

	/* if the new window uses the copy buffer, sync it for the device */
	if ((window->wd_dosync) && (hp->dmai_rflags & DDI_DMA_WRITE)) {
		(void) rootnex_dma_sync(dip, rdip, handle, 0, 0,
		    DDI_DMA_SYNC_FORDEV);
	}

	return (DDI_SUCCESS);
}

/*
 * ************************
 *  obsoleted dma routines
 * ************************
 */

/*
 * rootnex_dma_map()
 *    called from ddi_dma_setup()
 */
/* ARGSUSED */
static int
rootnex_dma_map(dev_info_t *dip, dev_info_t *rdip,
    struct ddi_dma_req *dmareq, ddi_dma_handle_t *handlep)
{
	/*
	 * this interface is not supported in 64-bit x86 kernel. See comment in
	 * rootnex_dma_mctl()
	 */
	return (DDI_DMA_NORESOURCES);
}

/*
 * rootnex_dma_mctl()
 */
/* ARGSUSED */
static int
rootnex_dma_mctl(dev_info_t *dip, dev_info_t *rdip, ddi_dma_handle_t handle,
    enum ddi_dma_ctlops request, off_t *offp, size_t *lenp, caddr_t *objpp,
    uint_t cache_flags)
{
	/*
	 * DDI_DMA_SMEM_ALLOC & DDI_DMA_IOPB_ALLOC we're changed to have a
	 * common implementation in genunix, so they no longer have x86
	 * specific functionality which called into dma_ctl.
	 *
	 * The rest of the obsoleted interfaces were never supported in the
	 * 64-bit x86 kernel. For s10, the obsoleted DDI_DMA_SEGTOC interface
	 * was not ported to the x86 64-bit kernel do to serious x86 rootnex
	 * implementation issues.
	 *
	 * If you can't use DDI_DMA_SEGTOC; DDI_DMA_NEXTSEG, DDI_DMA_FREE, and
	 * DDI_DMA_NEXTWIN are useless since you can get to the cookie, so we
	 * reflect that now too...
	 *
	 * Even though we fixed the pointer problem in DDI_DMA_SEGTOC, we are
	 * not going to put this functionality into the 64-bit x86 kernel now.
	 * It wasn't ported to the 64-bit kernel for s10, no reason to change
	 * that in a future release.
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

/*
 * rootnex_dma_check()
 *    Function called after a dma fault occurred to find out whether the
 *    fault address is associated with a driver that is able to handle faults
 *    and recover from faults.
 */
/* ARGSUSED */
static int
rootnex_dma_check(dev_info_t *dip, const void *handle, const void *addr,
    const void *not_used)
{
	rootnex_window_t *window;
	uint64_t start_addr;
	uint64_t fault_addr;
	ddi_dma_impl_t *hp;
	rootnex_dma_t *dma;
	uint64_t end_addr;
	size_t csize;
	int i;
	int j;


	/* The driver has to set DDI_DMA_FLAGERR to recover from dma faults */
	hp = (ddi_dma_impl_t *)handle;
	ASSERT(hp);

	dma = (rootnex_dma_t *)hp->dmai_private;

	/* Get the address that we need to search for */
	fault_addr = *(uint64_t *)addr;

	/*
	 * if we don't have any windows, we can just walk through all the
	 * cookies.
	 */
	if (dma->dp_window == NULL) {
		/* for each cookie */
		for (i = 0; i < dma->dp_sglinfo.si_sgl_size; i++) {
			/*
			 * if the faulted address is within the physical address
			 * range of the cookie, return DDI_FM_NONFATAL.
			 */
			if ((fault_addr >= dma->dp_cookies[i].dmac_laddress) &&
			    (fault_addr <= (dma->dp_cookies[i].dmac_laddress +
			    dma->dp_cookies[i].dmac_size))) {
				return (DDI_FM_NONFATAL);
			}
		}

		/* fault_addr not within this DMA handle */
		return (DDI_FM_UNKNOWN);
	}

	/* we have mutiple windows, walk through each window */
	for (i = 0; i < hp->dmai_nwin; i++) {
		window = &dma->dp_window[i];

		/* Go through all the cookies in the window */
		for (j = 0; j < window->wd_cookie_cnt; j++) {

			start_addr = window->wd_first_cookie[j].dmac_laddress;
			csize = window->wd_first_cookie[j].dmac_size;

			/*
			 * if we are trimming the first cookie in the window,
			 * and this is the first cookie, adjust the start
			 * address and size of the cookie to account for the
			 * trim.
			 */
			if (window->wd_trim.tr_trim_first && (j == 0)) {
				start_addr = window->wd_trim.tr_first_paddr;
				csize = window->wd_trim.tr_first_size;
			}

			/*
			 * if we are trimming the last cookie in the window,
			 * and this is the last cookie, adjust the start
			 * address and size of the cookie to account for the
			 * trim.
			 */
			if (window->wd_trim.tr_trim_last &&
			    (j == (window->wd_cookie_cnt - 1))) {
				start_addr = window->wd_trim.tr_last_paddr;
				csize = window->wd_trim.tr_last_size;
			}

			end_addr = start_addr + csize;

			/*
			 * if the faulted address is within the physical
			 * address of the cookie, return DDI_FM_NONFATAL.
			 */
			if ((fault_addr >= start_addr) &&
			    (fault_addr <= end_addr)) {
				return (DDI_FM_NONFATAL);
			}
		}
	}

	/* fault_addr not within this DMA handle */
	return (DDI_FM_UNKNOWN);
}

/*ARGSUSED*/
static int
rootnex_quiesce(dev_info_t *dip)
{
#if !defined(__xpv)
	if (iommu_ops != NULL)
		iommu_ops->ioo_quiesce_units(1);
#endif
	return (DDI_SUCCESS);
}
