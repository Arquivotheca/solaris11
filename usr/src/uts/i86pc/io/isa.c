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
 *	ISA bus nexus driver
 */

#include <sys/types.h>
#include <sys/cmn_err.h>
#include <sys/conf.h>
#include <sys/modctl.h>
#include <sys/autoconf.h>
#include <sys/errno.h>
#include <sys/debug.h>
#include <sys/kmem.h>
#include <sys/psm.h>
#include <sys/ddidmareq.h>
#include <sys/ddi_impldefs.h>
#include <sys/ddi_subrdefs.h>
#include <sys/dma_engine.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/sunndi.h>
#include <sys/acpi/acpi_enum.h>
#include <sys/mach_intr.h>
#include <sys/pci.h>
#include <sys/note.h>
#include <sys/bootinfo.h>
#include <sys/boot_console.h>
#include <sys/apic.h>
#include <sys/isabus.h>
#include <sys/iommu.h>


extern int pseudo_isa;
extern int isa_resource_setup(void);
extern int (*psm_intr_ops)(dev_info_t *, ddi_intr_handle_impl_t *,
    psm_intr_op_t, int *);
extern void pci_register_isa_resources(int, uint32_t, uint32_t);
static char USED_RESOURCES[] = "used-resources";
static void isa_enumerate(int);
static void enumerate_firmware_serial(dev_info_t *);
static void adjust_prtsz(dev_info_t *isa_dip);
static void isa_create_ranges_prop(dev_info_t *);
static void free_isa_ppd(dev_info_t *);

/*
 * The following typedef is used to represent an entry in the "ranges"
 * property of a pci-isa bridge device node.
 */
typedef struct {
	uint32_t child_high;
	uint32_t child_low;
	uint32_t parent_high;
	uint32_t parent_mid;
	uint32_t parent_low;
	uint32_t size;
} pib_ranges_t;

typedef struct {
	uint32_t base;
	uint32_t len;
} used_ranges_t;

#define	USED_CELL_SIZE	2	/* 1 byte addr, 1 byte size */
#define	ISA_ADDR_IO	1	/* IO address space */
#define	ISA_ADDR_MEM	0	/* memory adress space */
#define	BIOS_DATA_AREA	0x400
/*
 * #define ISA_DEBUG 1
 */

/*
 * For serial ports not enumerated by ACPI, and parallel ports with
 * illegal size. Typically, a system can have as many as 4 serial
 * ports and 3 parallel ports.
 */
#define	MAX_EXTRA_RESOURCE	7
static isa_regspec_t isa_extra_resource[MAX_EXTRA_RESOURCE];
static int isa_extra_count = 0;

/*
 * Tunable to force allocation of serial ports on UEFI systems
 * The value is a bitmap (bit 0 means allocate ttya, bit 1 = ttyb)
 * The default is not to allocate any explicitly (rely on ACPI to
 * specify the serial ports).
 */
int force_alloc_serial_ports = 0;

/*
 *      Local data
 */
static ddi_dma_lim_t ISA_dma_limits = {
	0,		/* address low				*/
	0x00ffffff,	/* address high				*/
	0,		/* counter max				*/
	1,		/* burstsize				*/
	DMA_UNIT_8,	/* minimum xfer				*/
	0,		/* dma speed				*/
	(uint_t)DMALIM_VER0, /* version				*/
	0x0000ffff,	/* address register			*/
	0x0000ffff,	/* counter register			*/
	1,		/* sector size				*/
	0x00000001,	/* scatter/gather list length		*/
	(uint_t)0xffffffff /* request size			*/
};

static ddi_dma_attr_t ISA_dma_attr = {
	DMA_ATTR_V0,
	(unsigned long long)0,
	(unsigned long long)0x00ffffff,
	0x0000ffff,
	1,
	1,
	1,
	(unsigned long long)0xffffffff,
	(unsigned long long)0x0000ffff,
	1,
	1,
	0
};


/*
 * Config information
 */

static int
isa_bus_map(dev_info_t *dip, dev_info_t *rdip, ddi_map_req_t *mp,
    off_t offset, off_t len, caddr_t *vaddrp);

static int
isa_dma_allochdl(dev_info_t *, dev_info_t *, ddi_dma_attr_t *,
    int (*waitfp)(caddr_t), caddr_t arg, ddi_dma_handle_t *);

static int
isa_dma_mctl(dev_info_t *, dev_info_t *, ddi_dma_handle_t, enum ddi_dma_ctlops,
    off_t *, size_t *, caddr_t *, uint_t);

static int
isa_ctlops(dev_info_t *, dev_info_t *, ddi_ctl_enum_t, void *, void *);

static int
isa_intr_ops(dev_info_t *pdip, dev_info_t *rdip, ddi_intr_op_t intr_op,
    ddi_intr_handle_impl_t *hdlp, void *result);
static int isa_alloc_intr_fixed(dev_info_t *, ddi_intr_handle_impl_t *, void *);
static int isa_free_intr_fixed(dev_info_t *, ddi_intr_handle_impl_t *);

struct bus_ops isa_bus_ops = {
	BUSO_REV,
	isa_bus_map,
	NULL,
	NULL,
	NULL,
	i_ddi_map_fault,
	ddi_dma_map,
	isa_dma_allochdl,
	ddi_dma_freehdl,
	ddi_dma_bindhdl,
	ddi_dma_unbindhdl,
	ddi_dma_flush,
	ddi_dma_win,
	isa_dma_mctl,
	isa_ctlops,
	ddi_bus_prop_op,
	NULL,		/* (*bus_get_eventcookie)();	*/
	NULL,		/* (*bus_add_eventcall)();	*/
	NULL,		/* (*bus_remove_eventcall)();	*/
	NULL,		/* (*bus_post_event)();		*/
	NULL,		/* (*bus_intr_ctl)(); */
	NULL,		/* (*bus_config)(); */
	NULL,		/* (*bus_unconfig)(); */
	NULL,		/* (*bus_fm_init)(); */
	NULL,		/* (*bus_fm_fini)(); */
	NULL,		/* (*bus_fm_access_enter)(); */
	NULL,		/* (*bus_fm_access_exit)(); */
	NULL,		/* (*bus_power)(); */
	isa_intr_ops	/* (*bus_intr_op)(); */
};


static int isa_attach(dev_info_t *devi, ddi_attach_cmd_t cmd);

/*
 * Internal isa ctlops support routines
 */
static int isa_initchild(dev_info_t *child);

struct dev_ops isa_ops = {
	DEVO_REV,		/* devo_rev, */
	0,			/* refcnt  */
	ddi_no_info,		/* info */
	nulldev,		/* identify */
	nulldev,		/* probe */
	isa_attach,		/* attach */
	nulldev,		/* detach */
	nodev,			/* reset */
	(struct cb_ops *)0,	/* driver operations */
	&isa_bus_ops,		/* bus operations */
	NULL,			/* power */
	ddi_quiesce_not_needed,		/* quiesce */
};

/*
 * Module linkage information for the kernel.
 */

static struct modldrv modldrv = {
	&mod_driverops, /* Type of module.  This is ISA bus driver */
	"isa nexus driver for 'ISA'",
	&isa_ops,	/* driver ops */
};

static struct modlinkage modlinkage = {
	MODREV_1,
	&modldrv,
	NULL
};

int
_init(void)
{
	int	err;

	if ((err = mod_install(&modlinkage)) != 0)
		return (err);

	impl_bus_add_probe(isa_enumerate);
	return (0);
}

int
_fini(void)
{
	int	err;

	impl_bus_delete_probe(isa_enumerate);

	if ((err = mod_remove(&modlinkage)) != 0)
		return (err);

	return (0);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

/*ARGSUSED*/
static int
isa_attach(dev_info_t *devi, ddi_attach_cmd_t cmd)
{

#if !defined(__xpv)
	int rval;
	switch (cmd) {
	case DDI_ATTACH:
		break;
	case DDI_RESUME:
		return (DDI_SUCCESS);
	default:
		return (DDI_FAILURE);
	}

	if ((rval = i_dmae_init(devi)) == DDI_SUCCESS)
		ddi_report_dev(devi);

	return (rval);
#else
	/*
	 * don't allow isa to attach in domU. this can happen if someone sets
	 * the console wrong, etc. ISA devices assume the H/W is there and
	 * will cause the domU to panic.
	 */
	return (DDI_FAILURE);
#endif /* !__xpv */

}

#define	SET_RNGS(rng_p, used_p, ctyp, ptyp) do {			\
		(rng_p)->child_high = (ctyp);				\
		(rng_p)->child_low = (rng_p)->parent_low = (used_p)->base; \
		(rng_p)->parent_high = (ptyp);				\
		(rng_p)->parent_mid = 0;				\
		(rng_p)->size = (used_p)->len;				\
		_NOTE(CONSTCOND) } while (0)
static uint_t
isa_used_to_ranges(int ctype, int *array, uint_t size, pib_ranges_t *ranges)
{
	used_ranges_t *used_p;
	pib_ranges_t *rng_p = ranges;
	uint_t	i, ptype;

	ptype = (ctype == ISA_ADDR_IO) ? PCI_ADDR_IO : PCI_ADDR_MEM32;
	ptype |= PCI_REG_REL_M;
	size /= USED_CELL_SIZE;
	used_p = (used_ranges_t *)array;
	SET_RNGS(rng_p, used_p, ctype, ptype);
	for (i = 1, used_p++; i < size; i++, used_p++) {
		/* merge ranges record if applicable */
		if (rng_p->child_low + rng_p->size == used_p->base)
			rng_p->size += used_p->len;
		else {
			rng_p++;
			SET_RNGS(rng_p, used_p, ctype, ptype);
		}
	}
	return (rng_p - ranges + 1);
}

void
isa_remove_res_from_pci(int type, int *array, uint_t size)
{
	int i;
	used_ranges_t *used_p;

	size /= USED_CELL_SIZE;
	used_p = (used_ranges_t *)array;
	for (i = 0; i < size; i++, used_p++)
		pci_register_isa_resources(type, used_p->base, used_p->len);
}

static void
isa_create_ranges_prop(dev_info_t *dip)
{
	dev_info_t *used;
	int *ioarray, *memarray, status;
	uint_t nio = 0, nmem = 0, nrng = 0, n;
	pib_ranges_t *ranges;

	used = ddi_find_devinfo("used-resources", -1, 0);
	if (used == NULL) {
		cmn_err(CE_WARN, "Failed to find used-resources <%s>\n",
		    ddi_get_name(dip));
		return;
	}
	status = ddi_prop_lookup_int_array(DDI_DEV_T_ANY, used,
	    DDI_PROP_DONTPASS, "io-space", &ioarray, &nio);
	if (status != DDI_PROP_SUCCESS && status != DDI_PROP_NOT_FOUND) {
		cmn_err(CE_WARN, "io-space property failure for %s (%x)\n",
		    ddi_get_name(used), status);
		return;
	}
	status = ddi_prop_lookup_int_array(DDI_DEV_T_ANY, used,
	    DDI_PROP_DONTPASS, "device-memory", &memarray, &nmem);
	if (status != DDI_PROP_SUCCESS && status != DDI_PROP_NOT_FOUND) {
		cmn_err(CE_WARN, "device-memory property failure for %s (%x)\n",
		    ddi_get_name(used), status);
		return;
	}
	n = (nio + nmem) / USED_CELL_SIZE;
	ranges =  (pib_ranges_t *)kmem_zalloc(sizeof (pib_ranges_t) * n,
	    KM_SLEEP);

	if (nio != 0) {
		nrng = isa_used_to_ranges(ISA_ADDR_IO, ioarray, nio, ranges);
		isa_remove_res_from_pci(ISA_ADDR_IO, ioarray, nio);
		ddi_prop_free(ioarray);
	}
	if (nmem != 0) {
		nrng += isa_used_to_ranges(ISA_ADDR_MEM, memarray, nmem,
		    ranges + nrng);
		isa_remove_res_from_pci(ISA_ADDR_MEM, memarray, nmem);
		ddi_prop_free(memarray);
	}

	if (!pseudo_isa)
		(void) ndi_prop_update_int_array(DDI_DEV_T_NONE, dip, "ranges",
		    (int *)ranges, nrng * sizeof (pib_ranges_t) / sizeof (int));
	kmem_free(ranges, sizeof (pib_ranges_t) * n);
}

static void
isa_create_cells_props(dev_info_t *dip)
{
	(void) ndi_prop_update_int(DDI_DEV_T_NONE, dip, "#address-cells", 2);
	(void) ndi_prop_update_int(DDI_DEV_T_NONE, dip, "#size-cells", 1);
}

/*ARGSUSED*/
static int
isa_apply_range(dev_info_t *dip, isa_regspec_t *isa_reg_p,
    pci_regspec_t *pci_reg_p)
{
	pib_ranges_t *ranges, *rng_p;
	int len, i, offset, nrange;

	if (ddi_getlongprop(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "ranges", (caddr_t)&ranges, &len) != DDI_SUCCESS) {
		cmn_err(CE_WARN, "Can't get %s ranges property",
		    ddi_get_name(dip));
		return (DDI_ME_REGSPEC_RANGE);
	}
	nrange = len / sizeof (pib_ranges_t);
	rng_p = ranges;
	for (i = 0; i < nrange; i++, rng_p++) {
		/* Check for correct space */
		if (isa_reg_p->phys_hi != rng_p->child_high)
			continue;

		/* Detect whether request entirely fits within a range */
		if (isa_reg_p->phys_lo < rng_p->child_low)
			continue;
		if ((isa_reg_p->phys_lo + isa_reg_p->size - 1) >
		    (rng_p->child_low + rng_p->size - 1))
			continue;

		offset = isa_reg_p->phys_lo - rng_p->child_low;

		pci_reg_p->pci_phys_hi = rng_p->parent_high;
		pci_reg_p->pci_phys_mid = 0;
		pci_reg_p->pci_phys_low = rng_p->parent_low + offset;
		pci_reg_p->pci_size_hi = 0;
		pci_reg_p->pci_size_low = isa_reg_p->size;

		break;
	}
	kmem_free(ranges, len);

	if (i < nrange)
		return (DDI_SUCCESS);

	/*
	 * Check extra resource range specially for serial and parallel
	 * devices, which are treated differently from all other ISA
	 * devices. On some machines, serial ports are not enumerated
	 * by ACPI but by BIOS, with io base addresses noted in legacy
	 * BIOS data area.  On some machines parallel ports are noted with
	 * an incorrect size.
	 */
	if (isa_reg_p->phys_hi != ISA_ADDR_IO)
		goto out_of_range;

	for (i = 0; i < isa_extra_count; i++) {
		isa_regspec_t *reg_p = &isa_extra_resource[i];

		if (isa_reg_p->phys_lo < reg_p->phys_lo)
			continue;
		if ((isa_reg_p->phys_lo + isa_reg_p->size) >
		    (reg_p->phys_lo + reg_p->size))
			continue;

		pci_reg_p->pci_phys_hi = PCI_ADDR_IO | PCI_REG_REL_M;
		pci_reg_p->pci_phys_mid = 0;
		pci_reg_p->pci_phys_low = isa_reg_p->phys_lo;
		pci_reg_p->pci_size_hi = 0;
		pci_reg_p->pci_size_low = isa_reg_p->size;
		break;
	}
	if (i < isa_extra_count)
		return (DDI_SUCCESS);

out_of_range:
	cmn_err(CE_WARN, "isa_apply_range: Out of range base <0x%x>, size <%d>",
	    isa_reg_p->phys_lo, isa_reg_p->size);
	return (DDI_ME_REGSPEC_RANGE);
}

static int
isa_bus_map(dev_info_t *dip, dev_info_t *rdip, ddi_map_req_t *mp,
    off_t offset, off_t len, caddr_t *vaddrp)
{
	isa_regspec_t *isa_rp, *isa_regs;
	pci_regspec_t vreg;
	ddi_map_req_t mr;
	uint_t reglen;
	int error;

	/*
	 * First, if given an rnumber, convert it to an isa_regspec...
	 */
	if (mp->map_type == DDI_MT_RNUMBER) {

		int rnumber = mp->map_obj.rnumber;
		int n;

		if (ddi_prop_lookup_int_array(DDI_DEV_T_ANY, rdip,
		    DDI_PROP_DONTPASS, "reg", (int **)&isa_regs,
		    &reglen) != DDI_SUCCESS)
			return (DDI_ME_RNUMBER_RANGE);

		n = (reglen * sizeof (int)) / sizeof (isa_regspec_t);

		if (rnumber < 0 || rnumber >= n) {
			ddi_prop_free(isa_regs);
			return (DDI_ME_RNUMBER_RANGE);
		}

		isa_rp = &isa_regs[rnumber];
	} else if (mp->map_type == DDI_MT_REGSPEC) {
		isa_rp = (isa_regspec_t *)mp->map_obj.rp;
	} else {
		return (DDI_ME_INVAL);
	}

	if (pseudo_isa) {
		struct regspec reg;

		if (mp->map_type == DDI_MT_RNUMBER) {
			reg.regspec_bustype = isa_rp->phys_hi;
			reg.regspec_addr_high = 0;
			reg.regspec_addr_low = isa_rp->phys_lo;
			reg.regspec_size_high = 0;
			reg.regspec_size_low = isa_rp->size;
			ddi_prop_free(isa_regs);

			mr = *mp;
			mr.map_type = DDI_MT_REGSPEC;
			mr.map_obj.rp = (struct regspec *)&reg;
			mp = &mr;
		}

		return (i_ddi_bus_map(dip, rdip, mp, offset, len, vaddrp));
	}

	/*
	 * Adjust offset and length correspnding to called values...
	 * XXX: A non-zero length means override the one in the regspec.
	 * XXX: (Regardless of what's in the parent's range)
	 */

	isa_rp->phys_lo += (uint32_t)offset;
	if (len != 0)
		isa_rp->size = (uint32_t)len;

	error = isa_apply_range(dip, isa_rp, &vreg);

	if (mp->map_type == DDI_MT_RNUMBER)
		ddi_prop_free(isa_regs);

	if (error != DDI_SUCCESS)
		return (error);

	mr = *mp;
	mr.map_type = DDI_MT_REGSPEC;
	mr.map_obj.rp = (struct regspec *)&vreg;

	/*
	 * Call my parents bus_map function with modified values...
	 */

	return (ddi_map(dip, &mr, (off_t)0, (off_t)0, vaddrp));
}

static int
isa_dma_allochdl(dev_info_t *dip, dev_info_t *rdip, ddi_dma_attr_t *dma_attr,
    int (*waitfp)(caddr_t), caddr_t arg, ddi_dma_handle_t *handlep)
{
	ddi_dma_attr_merge(dma_attr, &ISA_dma_attr);
	return (ddi_dma_allochdl(dip, rdip, dma_attr, waitfp, arg, handlep));
}

static int
isa_dma_mctl(dev_info_t *dip, dev_info_t *rdip,
    ddi_dma_handle_t handle, enum ddi_dma_ctlops request,
    off_t *offp, size_t *lenp, caddr_t *objp, uint_t flags)
{
	int rval;
	ddi_dma_lim_t defalt;
	int arg = (int)(uintptr_t)objp;

	switch (request) {

	case DDI_DMA_E_PROG:
		return (i_dmae_prog(rdip, (struct ddi_dmae_req *)offp,
		    (ddi_dma_cookie_t *)lenp, arg));

	case DDI_DMA_E_ACQUIRE:
		return (i_dmae_acquire(rdip, arg, (int(*)(caddr_t))offp,
		    (caddr_t)lenp));

	case DDI_DMA_E_FREE:
		return (i_dmae_free(rdip, arg));

	case DDI_DMA_E_STOP:
		i_dmae_stop(rdip, arg);
		return (DDI_SUCCESS);

	case DDI_DMA_E_ENABLE:
		i_dmae_enable(rdip, arg);
		return (DDI_SUCCESS);

	case DDI_DMA_E_DISABLE:
		i_dmae_disable(rdip, arg);
		return (DDI_SUCCESS);

	case DDI_DMA_E_GETCNT:
		i_dmae_get_chan_stat(rdip, arg, NULL, (int *)lenp);
		return (DDI_SUCCESS);

	case DDI_DMA_E_SWSETUP:
		return (i_dmae_swsetup(rdip, (struct ddi_dmae_req *)offp,
		    (ddi_dma_cookie_t *)lenp, arg));

	case DDI_DMA_E_SWSTART:
		i_dmae_swstart(rdip, arg);
		return (DDI_SUCCESS);

	case DDI_DMA_E_GETLIM:
		bcopy(&ISA_dma_limits, objp, sizeof (ddi_dma_lim_t));
		return (DDI_SUCCESS);

	case DDI_DMA_E_GETATTR:
		bcopy(&ISA_dma_attr, objp, sizeof (ddi_dma_attr_t));
		return (DDI_SUCCESS);

	case DDI_DMA_E_1STPTY:
		{
			struct ddi_dmae_req req1stpty =
			    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
			if (arg == 0) {
				req1stpty.der_command = DMAE_CMD_TRAN;
				req1stpty.der_trans = DMAE_TRANS_DMND;
			} else {
				req1stpty.der_trans = DMAE_TRANS_CSCD;
			}
			return (i_dmae_prog(rdip, &req1stpty, NULL, arg));
		}

	case DDI_DMA_IOPB_ALLOC:	/* get contiguous DMA-able memory */
	case DDI_DMA_SMEM_ALLOC:
		if (!offp) {
			defalt = ISA_dma_limits;
			offp = (off_t *)&defalt;
		}
		/*FALLTHROUGH*/
	default:
		rval = ddi_dma_mctl(dip, rdip, handle, request, offp,
		    lenp, objp, flags);
	}
	return (rval);
}

/*
 * Check if driver should be treated as an old pre 2.6 driver
 */
static int
old_driver(dev_info_t *dip)
{
	extern int ignore_hardware_nodes;	/* force flag from ddi_impl.c */

	if (ndi_dev_is_persistent_node(dip)) {
		if (ignore_hardware_nodes)
			return (1);
		if (ddi_getprop(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
		    "ignore-hardware-nodes", -1) != -1)
			return (1);
	}
	return (0);
}

/*
 * Return non-zero if device in tree is a PnP isa device
 */
static int
is_pnpisa(dev_info_t *dip)
{
	isa_regspec_t *isa_regs;
	int proplen, pnpisa;

	if (ddi_getlongprop(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS, "reg",
	    (caddr_t)&isa_regs, &proplen) != DDI_PROP_SUCCESS) {
		return (0);
	}
	pnpisa = isa_regs[0].phys_hi & 0x80000000;
	/*
	 * free the memory allocated by ddi_getlongprop().
	 */
	kmem_free(isa_regs, proplen);
	if (pnpisa)
		return (1);
	else
		return (0);
}

/*ARGSUSED*/
static int
isa_ctlops(dev_info_t *dip, dev_info_t *rdip,
	ddi_ctl_enum_t ctlop, void *arg, void *result)
{
	isa_regspec_t *regp;
	uint_t reglen;
	int nreg;

	switch (ctlop) {
	case DDI_CTLOPS_REPORTDEV:
		if (rdip == (dev_info_t *)0)
			return (DDI_FAILURE);
		cmn_err(CE_CONT, "?ISA-device: %s%d\n",
		    ddi_driver_name(rdip), ddi_get_instance(rdip));
		return (DDI_SUCCESS);

	case DDI_CTLOPS_INITCHILD:
		/*
		 * older drivers aren't expecting the "standard" device
		 * node format used by the hardware nodes.  these drivers
		 * only expect their own properties set in their driver.conf
		 * files.  so they tell us not to call them with hardware
		 * nodes by setting the property "ignore-hardware-nodes".
		 */
		if (old_driver((dev_info_t *)arg)) {
			return (DDI_NOT_WELL_FORMED);
		}

		return (isa_initchild((dev_info_t *)arg));

	case DDI_CTLOPS_UNINITCHILD:
		iommu_teardown((dev_info_t *)arg);
		free_isa_ppd((dev_info_t *)arg);
		impl_ddi_sunbus_removechild((dev_info_t *)arg);
		return (DDI_SUCCESS);

	case DDI_CTLOPS_SIDDEV:
		if (ndi_dev_is_persistent_node(rdip))
			return (DDI_SUCCESS);
		/*
		 * All ISA devices need to do confirming probes
		 * unless they are PnP ISA.
		 */
		if (is_pnpisa(rdip))
			return (DDI_SUCCESS);
		else
			return (DDI_FAILURE);

	case DDI_CTLOPS_REGSIZE:
	case DDI_CTLOPS_NREGS:
		if (rdip == (dev_info_t *)0)
			return (DDI_FAILURE);

		*(int *)result = 0;
		if (ddi_prop_lookup_int_array(DDI_DEV_T_ANY, rdip,
		    DDI_PROP_DONTPASS, "reg", (int **)&regp,
		    &reglen) != DDI_PROP_SUCCESS) {
			return (DDI_FAILURE);
		}

		nreg = (reglen * sizeof (int)) / sizeof (isa_regspec_t);
		if (ctlop == DDI_CTLOPS_NREGS)
			*(int *)result = nreg;
		else if (ctlop == DDI_CTLOPS_REGSIZE) {
			int rn = *(int *)arg;
			if (rn >= nreg) {
				ddi_prop_free(regp);
				return (DDI_FAILURE);
			}
			*(off_t *)result = regp[rn].size;
		}
		ddi_prop_free(regp);

		return (DDI_SUCCESS);

	case DDI_CTLOPS_ATTACH:
	case DDI_CTLOPS_DETACH:
	case DDI_CTLOPS_PEEK:
	case DDI_CTLOPS_POKE:
		return (DDI_FAILURE);

	default:
		return (ddi_ctlops(dip, rdip, ctlop, arg, result));
	}
}

static struct intrspec *
isa_get_ispec(dev_info_t *rdip, int inum)
{
	struct isa_parent_private_data *pdp = ddi_get_parent_data(rdip);

	/* Validate the interrupt number */
	if (inum >= pdp->par_nintr)
		return (NULL);

	/* Get the interrupt structure pointer and return that */
	return ((struct intrspec *)&pdp->par_intr[inum]);
}

static int
isa_intr_ops(dev_info_t *pdip, dev_info_t *rdip, ddi_intr_op_t intr_op,
    ddi_intr_handle_impl_t *hdlp, void *result)
{
	struct intrspec *ispec;

	if (pseudo_isa)
		return (i_ddi_intr_ops(pdip, rdip, intr_op, hdlp, result));


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
		return (isa_alloc_intr_fixed(rdip, hdlp, result));
	case DDI_INTROP_FREE:
		ASSERT(hdlp->ih_type == DDI_INTR_TYPE_FIXED);
		return (isa_free_intr_fixed(rdip, hdlp));
	case DDI_INTROP_GETPRI:
		if ((ispec = isa_get_ispec(rdip, hdlp->ih_inum)) == NULL)
			return (DDI_FAILURE);
		*(int *)result = ispec->intrspec_pri;
		break;
	case DDI_INTROP_SETPRI:
		/* Validate the interrupt priority passed to us */
		if (*(int *)result > LOCK_LEVEL)
			return (DDI_FAILURE);

		/* Ensure that PSM is all initialized and ispec is ok */
		if ((psm_intr_ops == NULL) ||
		    ((ispec = isa_get_ispec(rdip, hdlp->ih_inum)) == NULL))
			return (DDI_FAILURE);

		/* update the ispec with the new priority */
		ispec->intrspec_pri =  *(int *)result;
		break;
	case DDI_INTROP_ADDISR:
		if ((ispec = isa_get_ispec(rdip, hdlp->ih_inum)) == NULL)
			return (DDI_FAILURE);
		ispec->intrspec_func = hdlp->ih_cb_func;
		break;
	case DDI_INTROP_REMISR:
		if (hdlp->ih_type != DDI_INTR_TYPE_FIXED)
			return (DDI_FAILURE);
		if ((ispec = isa_get_ispec(rdip, hdlp->ih_inum)) == NULL)
			return (DDI_FAILURE);
		ispec->intrspec_func = (uint_t (*)()) 0;
		break;
	case DDI_INTROP_ENABLE:
		if ((ispec = isa_get_ispec(rdip, hdlp->ih_inum)) == NULL)
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
		if ((ispec = isa_get_ispec(rdip, hdlp->ih_inum)) == NULL)
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
 * Allocate interrupt vector for FIXED (legacy) type.
 */
static int
isa_alloc_intr_fixed(dev_info_t *rdip, ddi_intr_handle_impl_t *hdlp,
    void *result)
{
	struct intrspec		*ispec;
	ddi_intr_handle_impl_t	info_hdl;
	int			ret;
	int			free_phdl = 0;
	apic_get_type_t		type_info;

	if (psm_intr_ops == NULL)
		return (DDI_FAILURE);

	if ((ispec = isa_get_ispec(rdip, hdlp->ih_inum)) == NULL)
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
isa_free_intr_fixed(dev_info_t *rdip, ddi_intr_handle_impl_t *hdlp)
{
	struct intrspec			*ispec;
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
		if ((ispec = isa_get_ispec(rdip, hdlp->ih_inum)) == NULL)
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

	return (ret);
}

static void
isa_vendor(uint32_t id, char *vendor)
{
	vendor[0] = '@' + ((id >> 26) & 0x1f);
	vendor[1] = '@' + ((id >> 21) & 0x1f);
	vendor[2] = '@' + ((id >> 16) & 0x1f);
	vendor[3] = 0;
}

#define	VEC_MIN 1
#define	VEC_MAX 255

static int
isa_xlate_intrs(dev_info_t *child, int *in,
    struct isa_parent_private_data *pdptr)
{
	size_t size;
	int n;
	struct intrspec *new;
	caddr_t got_prop;
	int *inpri;
	int got_len;

	static char bad_intr_fmt[] =
	    "bad interrupt spec from %s%d - ipl %d, irq %d\n";

	inpri = NULL;
	/* the new style "interrupts" property... */

	/*
	 * The list consists of <vec> elements
	 */
	if ((n = (*in++)) < 1)
		return (DDI_FAILURE);

	pdptr->par_nintr = n;
	size = n * sizeof (struct intrspec);
	new = pdptr->par_intr = kmem_zalloc(size, KM_SLEEP);

	/* XXX check for "interrupt-priorities" property... */
	if (ddi_getlongprop(DDI_DEV_T_ANY, child, DDI_PROP_DONTPASS,
	    "interrupt-priorities", (caddr_t)&got_prop, &got_len)
	    == DDI_PROP_SUCCESS) {
		if (n != (got_len / sizeof (int))) {
			cmn_err(CE_CONT,
			    "bad interrupt-priorities length"
			    " from %s%d: expected %d, got %d\n",
			    DEVI(child)->devi_name,
			    DEVI(child)->devi_instance, n,
			    (int)(got_len / sizeof (int)));
			goto broken;
		}
		inpri = (int *)got_prop;
	}

	while (n--) {
		int level;
		int vec = *in++;

		if (inpri == NULL)
			level = 5;
		else
			level = *inpri++;

		if (level < 1 || level > MAXIPL ||
		    vec < VEC_MIN || vec > VEC_MAX) {
			cmn_err(CE_CONT, bad_intr_fmt,
			    DEVI(child)->devi_name,
			    DEVI(child)->devi_instance, level, vec);
			goto broken;
		}
		new->intrspec_pri = level;
		if (vec != 2)
			new->intrspec_vec = vec;
		else
			/*
			 * irq 2 on the PC bus is tied to irq 9
			 * on ISA, EISA and MicroChannel
			 */
			new->intrspec_vec = 9;
		new++;
	}

	if (inpri != NULL)
		kmem_free(got_prop, got_len);
	return (DDI_SUCCESS);

broken:
	kmem_free(pdptr->par_intr, size);
	pdptr->par_intr = NULL;
	pdptr->par_nintr = 0;
	if (inpri != NULL)
		kmem_free(got_prop, got_len);

	return (DDI_FAILURE);
}

static void
make_isa_ppd(dev_info_t *child, struct isa_parent_private_data **ppd)
{
	struct isa_parent_private_data *pdptr;
	int *reg_prop, *rng_prop, *irupts_prop;
	uint_t reg_len, rng_len, irupts_len;

	*ppd = pdptr = kmem_zalloc(sizeof (*pdptr), KM_SLEEP);

	/*
	 * Handle the 'reg' property.
	 */
	if ((ddi_prop_lookup_int_array(DDI_DEV_T_ANY, child, DDI_PROP_DONTPASS,
	    "reg", &reg_prop, &reg_len) == DDI_PROP_SUCCESS) &&
	    (reg_len != 0)) {
		pdptr->par_nreg = (reg_len * sizeof (int)) /
		    sizeof (isa_regspec_t);
		pdptr->par_reg = (isa_regspec_t *)reg_prop;
	}

	/*
	 * Handle the 'ranges' property.
	 */
	if (ddi_prop_lookup_int_array(DDI_DEV_T_ANY, child, DDI_PROP_DONTPASS,
	    "ranges", &rng_prop, &rng_len) == DDI_PROP_SUCCESS) {
		pdptr->par_nrng = (rng_len * sizeof (int)) /
		    sizeof (struct rangespec);
		pdptr->par_rng = (struct rangespec *)rng_prop;
	}

	/*
	 * Handle the 'interrupts' property.
	 */
	if ((ddi_prop_lookup_int_array(DDI_DEV_T_ANY, child, DDI_PROP_DONTPASS,
	    "interrupts", &irupts_prop, &irupts_len) == DDI_PROP_SUCCESS) &&
	    (irupts_len != 0)) {
		size_t size;
		int *out;

		size = (1 + irupts_len) * sizeof (int);
		out = kmem_alloc(size, KM_SLEEP);
		*out = irupts_len;
		bcopy(irupts_prop, out + 1, (size_t)irupts_len * sizeof (int));
		ddi_prop_free((void *)irupts_prop);
		if (isa_xlate_intrs(child, out, pdptr) != DDI_SUCCESS) {
			cmn_err(CE_CONT,
			    "Unable to translate 'interrupts' for %s%d\n",
			    DEVI(child)->devi_binding_name,
			    DEVI(child)->devi_instance);
		}
		kmem_free(out, size);
	}
}

static void
free_isa_ppd(dev_info_t *dip)
{
	struct isa_parent_private_data *pdptr;
	size_t n;

	if ((pdptr = (struct isa_parent_private_data *)
	    ddi_get_parent_data(dip)) == NULL)
		return;

	if ((n = (size_t)pdptr->par_nintr) != 0)
		/*
		 * Note that kmem_free is used here (instead of
		 * ddi_prop_free) because the contents of the
		 * property were placed into a separate buffer and
		 * mucked with a bit before being stored in par_intr.
		 * The actual return value from the prop lookup
		 * was freed with ddi_prop_free previously.
		 */
		kmem_free(pdptr->par_intr, n * sizeof (struct intrspec));

	if (pdptr->par_nrng != 0)
		ddi_prop_free((void *)pdptr->par_rng);

	if ((n = pdptr->par_nreg) != 0)
		ddi_prop_free((void *)pdptr->par_reg);

	kmem_free(pdptr, sizeof (*pdptr));
	ddi_set_parent_data(dip, NULL);
}

/*
 * Name a child
 */
static int
isa_name_child(dev_info_t *child, char *name, int namelen)
{
	char vendor[8];
	int device;
	uint32_t serial;
	int func;
	int bustype;
	uint32_t base;
	int proplen;
	int pnpisa = 0;
	isa_regspec_t *isa_regs;

	/*
	 * older drivers aren't expecting the "standard" device
	 * node format used by the hardware nodes.  these drivers
	 * only expect their own properties set in their driver.conf
	 * files.  so they tell us not to call them with hardware
	 * nodes by setting the property "ignore-hardware-nodes".
	 */
	if (old_driver(child))
		return (DDI_FAILURE);

	/*
	 * Fill in parent-private data
	 */
	if (ddi_get_parent_data(child) == NULL) {
		struct isa_parent_private_data *pdptr;
		make_isa_ppd(child, &pdptr);
		ddi_set_parent_data(child, pdptr);
	}

	if (ndi_dev_is_persistent_node(child) == 0) {
		/*
		 * For .conf nodes, generate name from parent private data
		 */
		name[0] = '\0';
		if (sparc_pd_getnreg(child) > 0) {
			(void) snprintf(name, namelen, "%x,%x",
			    (uint_t)sparc_pd_getreg(child, 0)->regspec_bustype,
			    (uint_t)sparc_pd_getreg(child, 0)->
			    regspec_addr_low);
		}
		return (DDI_SUCCESS);
	}

	/*
	 * For hw nodes, look up "reg" property
	 */
	if (ddi_getlongprop(DDI_DEV_T_ANY, child, DDI_PROP_DONTPASS, "reg",
	    (caddr_t)&isa_regs, &proplen) != DDI_PROP_SUCCESS) {
		return (DDI_FAILURE);
	}

	/*
	 * extract the device identifications
	 */
	pnpisa = isa_regs[0].phys_hi & 0x80000000;
	if (pnpisa) {
		isa_vendor(isa_regs[0].phys_hi, vendor);
		device = isa_regs[0].phys_hi & 0xffff;
		serial = isa_regs[0].phys_lo;
		func = (isa_regs[0].size >> 24) & 0xff;
		if (func != 0)
			(void) snprintf(name, namelen, "pnp%s,%04x,%x,%x",
			    vendor, device, serial, func);
		else
			(void) snprintf(name, namelen, "pnp%s,%04x,%x",
			    vendor, device, serial);
	} else {
		bustype = isa_regs[0].phys_hi;
		base = isa_regs[0].phys_lo;
		(void) sprintf(name, "%x,%x", bustype, base);
	}

	/*
	 * free the memory allocated by ddi_getlongprop().
	 */
	kmem_free(isa_regs, proplen);

	return (DDI_SUCCESS);
}

static int
isa_initchild(dev_info_t *child)
{
	char name[80];

	if (isa_name_child(child, name, 80) != DDI_SUCCESS)
		return (DDI_FAILURE);
	ddi_set_name_addr(child, name);

	if (ndi_dev_is_persistent_node(child) != 0) {
		iommu_setup(child, IOMMU_SETUP_ISA);
		return (DDI_SUCCESS);
	}

	/*
	 * This is a .conf node, try merge properties onto a
	 * hw node with the same name.
	 */
	if (ndi_merge_node(child, isa_name_child) == DDI_SUCCESS) {
		/*
		 * Return failure to remove node
		 */
		impl_ddi_sunbus_removechild(child);
		return (DDI_FAILURE);
	}
	/*
	 * Cannot merge node, permit pseudo children
	 */
	return (DDI_SUCCESS);
}

/*
 * called when ACPI enumeration is not used
 */
static void
add_known_used_resources(void)
{
	/* needs to be in increasing order */
	int intr[] = {0x1, 0x3, 0x4, 0x6, 0x7, 0xc};
	int dma[] = {0x2};
	int io[] = {0x60, 0x1, 0x64, 0x1, 0x2f8, 0x8, 0x378, 0x8, 0x3f0, 0x10,
	    0x778, 0x4};
	dev_info_t *usedrdip;

	usedrdip = ddi_find_devinfo(USED_RESOURCES, -1, 0);

	if (usedrdip == NULL) {
		(void) ndi_devi_alloc_sleep(ddi_root_node(), USED_RESOURCES,
		    (pnode_t)DEVI_SID_NODEID, &usedrdip);
	}

	(void) ndi_prop_update_int_array(DDI_DEV_T_NONE, usedrdip,
	    "interrupts", (int *)intr, (int)(sizeof (intr) / sizeof (int)));
	(void) ndi_prop_update_int_array(DDI_DEV_T_NONE, usedrdip,
	    "io-space", (int *)io, (int)(sizeof (io) / sizeof (int)));
	(void) ndi_prop_update_int_array(DDI_DEV_T_NONE, usedrdip,
	    "dma-channels", (int *)dma, (int)(sizeof (dma) / sizeof (int)));
	(void) ndi_devi_bind_driver(usedrdip, 0);

}

static void
isa_enumerate(int reprogram)
{
	int circ, i;
	dev_info_t *xdip;
	dev_info_t *isa_dip = ddi_find_devinfo("isa", -1, 0);

	/* hard coded isa stuff */
	isa_regspec_t asy_regs[] = {
		{1, 0x3f8, 0x8},
		{1, 0x2f8, 0x8}
	};
	int asy_intrs[] = {0x4, 0x3};

	isa_regspec_t i8042_regs[] = {
		{1, 0x60, 0x1},
		{1, 0x64, 0x1}
	};
	int i8042_intrs[] = {0x1, 0xc};
	char *acpi_prop;
	int acpi_enum = 1; /* ACPI is default to be on */

	if (reprogram || !isa_dip)
		return;

	bzero(isa_extra_resource, MAX_EXTRA_RESOURCE * sizeof (isa_regspec_t));

	ndi_devi_enter(isa_dip, &circ);

	if (ddi_prop_lookup_string(DDI_DEV_T_ANY, ddi_root_node(),
	    DDI_PROP_DONTPASS, "acpi-enum", &acpi_prop) == DDI_PROP_SUCCESS) {
		acpi_enum = strcmp("off", acpi_prop);
		ddi_prop_free(acpi_prop);
	}

	if (acpi_enum) {
		if (acpi_isa_device_enum(isa_dip)) {
			ndi_devi_exit(isa_dip, circ);
			if (isa_resource_setup() != NDI_SUCCESS) {
				cmn_err(CE_WARN, "isa nexus: isa "
				    "resource setup failed");
			}

			/* serial ports? */
			enumerate_firmware_serial(isa_dip);

			/* adjust parallel port size  */
			adjust_prtsz(isa_dip);

			isa_create_ranges_prop(isa_dip);
			isa_create_cells_props(isa_dip);
			return;
		}
		cmn_err(CE_NOTE, "!Solaris did not detect ACPI BIOS");
	}
	cmn_err(CE_NOTE, "!ACPI is off");

	/* serial ports */
	for (i = 0; i < 2; i++) {
		ndi_devi_alloc_sleep(isa_dip, "asy",
		    (pnode_t)DEVI_SID_NODEID, &xdip);
		(void) ndi_prop_update_int_array(DDI_DEV_T_NONE, xdip,
		    "reg", (int *)&asy_regs[i], 3);
		(void) ndi_prop_update_int(DDI_DEV_T_NONE, xdip,
		    "interrupts", asy_intrs[i]);
		(void) ndi_devi_bind_driver(xdip, 0);
	}

	/* i8042 node */
	ndi_devi_alloc_sleep(isa_dip, "i8042",
	    (pnode_t)DEVI_SID_NODEID, &xdip);
	(void) ndi_prop_update_int_array(DDI_DEV_T_NONE, xdip,
	    "reg", (int *)i8042_regs, 6);
	(void) ndi_prop_update_int_array(DDI_DEV_T_NONE, xdip,
	    "interrupts", (int *)i8042_intrs, 2);
	(void) ndi_prop_update_string(DDI_DEV_T_NONE, xdip,
	    "unit-address", "1,60");
	(void) ndi_devi_bind_driver(xdip, 0);

	add_known_used_resources();

	ndi_devi_exit(isa_dip, circ);

	isa_create_ranges_prop(isa_dip);
	isa_create_cells_props(isa_dip);
}

static void
isa_add_serial_port(dev_info_t *isa_dip, uint16_t base_addr, int irq)
{
	dev_info_t *xdip = NULL;
	isa_regspec_t tmp_asy_regs[] = {
		{1, 0x3f8, 0x8}
	};

	ndi_devi_alloc_sleep(isa_dip, "asy", (pnode_t)DEVI_SID_NODEID, &xdip);
	(void) ndi_prop_update_string(DDI_DEV_T_NONE, xdip, "compatible",
	    "PNP0500");
	/* XXX: This should be retrieved from the master file: */
	(void) ndi_prop_update_string(DDI_DEV_T_NONE, xdip, "model",
	    "Standard serial port");
	tmp_asy_regs[0].phys_lo = base_addr;
	(void) ndi_prop_update_int_array(DDI_DEV_T_NONE, xdip, "reg",
	    (int *)&tmp_asy_regs[0], 3);
	(void) ndi_prop_update_int(DDI_DEV_T_NONE, xdip, "interrupts", irq);
	(void) ndi_devi_bind_driver(xdip, 0);

	ASSERT(isa_extra_count < MAX_EXTRA_RESOURCE);
	bcopy(tmp_asy_regs, isa_extra_resource + isa_extra_count,
	    sizeof (isa_regspec_t));
	isa_extra_count++;
}

static int
isa_find_asy_node_by_baseaddr(dev_info_t *isa_dip, ushort_t base_addr)
{
	dev_info_t *xdip;
	isa_regspec_t *tmpregs;
	int tmpregs_len;

	for (xdip = ddi_get_child(isa_dip); xdip != NULL;
	    xdip = ddi_get_next_sibling(xdip)) {

		if (strncmp(ddi_node_name(xdip), "asy", 3) != 0) {
			/* skip non asy */
			continue;
		}

		/* Match by addr */
		if (ddi_prop_lookup_int_array(DDI_DEV_T_ANY, xdip,
		    DDI_PROP_DONTPASS, "reg", (int **)&tmpregs,
		    (uint_t *)&tmpregs_len) != DDI_PROP_SUCCESS) {
			continue;
		}

		if (tmpregs->phys_lo == base_addr) {
			ddi_prop_free(tmpregs);
			return (1);
		}

		/*
		 * Free the memory allocated by
		 * ddi_prop_lookup_int_array().
		 */
		ddi_prop_free(tmpregs);
	}

	return (0);
}


/*
 * On some machines, serial port 2 isn't listed in the ACPI table.
 * This function goes through the BIOS data area (on BIOS systems) and makes
 * sure all the serial ports there are in the device tree.  If any are missing,
 * this function will add them.  On UEFI systems, the serial ports MUST be
 * reported via ACPI.
 */

static int num_BIOS_serial = 2;	/* number of BIOS serial ports to look at */

#define	MAX_FORCED_SERIAL 4

static void
enumerate_firmware_serial(dev_info_t *isa_dip)
{
	ushort_t *bios_data;
	int i;
	static ushort_t asy_base_addrs[MAX_FORCED_SERIAL] = {
		0x3f8, 0x2f8, 0x3e8, 0x2e8
	};
	static int asy_intrs[] = { 4, 3, 4, 3 };
	static size_t bda_size = 4;

	/*
	 * Do NOT check the BDA on UEFI systems.
	 */
	if (ddi_prop_exists(DDI_DEV_T_ANY, ddi_root_node(), DDI_PROP_DONTPASS,
	    "efi-systab")) {

		for (i = 0; i < MAX_FORCED_SERIAL; i++) {
			if (!(force_alloc_serial_ports & (1 << i)))
				continue;

			/* If the asy node doesn't already exist, create it */
			if (!isa_find_asy_node_by_baseaddr(isa_dip,
			    asy_base_addrs[i])) {

				isa_add_serial_port(isa_dip, asy_base_addrs[i],
				    asy_intrs[i]);
			}
		}

		return;
	}

	/*
	 * The first four 2-byte quantities of the BIOS data area contain
	 * the base I/O addresses of the first four serial ports.
	 */
	bios_data = (ushort_t *)psm_map_new((paddr_t)BIOS_DATA_AREA, bda_size,
	    PSM_PROT_READ);

	for (i = 0; i < num_BIOS_serial; i++) {
		if (bios_data[i] == 0) {
			/* no COM[i]: port */
			continue;
		}

		/* Look for it in the device tree and add it if not found */
		if (!isa_find_asy_node_by_baseaddr(isa_dip, bios_data[i])) {
			isa_add_serial_port(isa_dip, bios_data[i],
			    asy_intrs[i]);
		}
	}

	psm_unmap((caddr_t)bios_data, bda_size);
}

/*
 * Some machine comes with an illegal parallel port size of 3
 * bytes in ACPI, even parallel port mode is ECP.
 */
#define	DEFAULT_PRT_SIZE	8
static void
adjust_prtsz(dev_info_t *isa_dip)
{
	dev_info_t *cdip;
	isa_regspec_t *regs_p, *extreg_p;
	int regs_len, nreg, i;
	char *name;

	for (cdip = ddi_get_child(isa_dip); cdip != NULL;
	    cdip = ddi_get_next_sibling(cdip)) {
		name = ddi_node_name(cdip);
		if ((strncmp(name, "lp", 2) != 0) || (strnlen(name, 3) != 2))
			continue;	/* skip non parallel */

		if (ddi_prop_lookup_int_array(DDI_DEV_T_ANY, cdip,
		    DDI_PROP_DONTPASS, "reg", (int **)&regs_p,
		    (uint_t *)&regs_len) != DDI_PROP_SUCCESS)
			continue;

		nreg = regs_len / (sizeof (isa_regspec_t) / sizeof (int));
		for (i = 0; i < nreg; i++) {
			if (regs_p[i].size == DEFAULT_PRT_SIZE)
				continue;

			ASSERT(isa_extra_count < MAX_EXTRA_RESOURCE);
			extreg_p = &isa_extra_resource[isa_extra_count++];
			extreg_p->phys_hi = ISA_ADDR_IO;
			extreg_p->phys_lo = regs_p[i].phys_lo;
			extreg_p->size = DEFAULT_PRT_SIZE;
		}

		ddi_prop_free(regs_p);
	}
}
