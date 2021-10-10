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
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */
/*
 * Copyright (c) 2009, Intel Corporation.
 * All rights reserved.
 */

/*
 * Intel IOMMU implementation
 * This file contains Intel IOMMU code exported
 * to the rest of the system and code that deals
 * with the Intel IOMMU as a whole.
 */

#include <sys/conf.h>
#include <sys/modctl.h>
#include <sys/pci.h>
#include <sys/pci_impl.h>
#include <sys/sysmacros.h>
#include <sys/ddi.h>
#include <sys/ddidmareq.h>
#include <sys/ddi_impldefs.h>
#include <sys/ddifm.h>
#include <sys/sunndi.h>
#include <sys/debug.h>
#include <sys/fm/protocol.h>
#include <sys/note.h>
#include <sys/apic.h>
#include <sys/apic_common.h>
#include <vm/hat_i86.h>
#include <sys/smp_impldefs.h>
#include <sys/spl.h>
#include <sys/archsystm.h>
#include <sys/x86_archext.h>
#include <sys/avl.h>
#include <sys/bootconf.h>
#include <sys/bootinfo.h>
#include <sys/atomic.h>
#include <sys/immu.h>
#include <sys/iommu.h>
#include <sys/smbios.h>
#include <util/sscanf.h>

/*
 * Global switches (boolean) that can be toggled either via boot options
 * or via /etc/system or kmdb
 */

/* Various features */
int immu_enable = 1;
int immu_dvma_enable = 1;

/* accessed in other files so not static */
int immu_intrmap_enable = 1;
int immu_qinv_enable = 1;

/* various quirks that need working around */

boolean_t immu_quirk_mobile4;

/* debug messages */

/*
 * Global used internally by Intel IOMMU code
 */
dev_info_t *root_devinfo;
kmutex_t immu_lock;
boolean_t immu_setup;
boolean_t immu_running;
boolean_t immu_quiesced;

/* Globals used only in this file */
static char **black_array;
static uint_t nblacks;

static int immu_attach(dev_info_t *dip, ddi_attach_cmd_t cmd);
static void immu_startup(void);
static int immu_quiesce(int);
static int immu_unquiesce(void);

int immu_init(void);

struct dev_ops immu_ops = {
	DEVO_REV,		/* devo_rev, */
	0,			/* refcnt  */
	ddi_no_info,		/* info */
	nulldev,		/* identify */
	nulldev,		/* probe */
	immu_attach,		/* attach */
	nulldev,		/* detach */
	nodev,			/* reset */
	NULL,			/* driver operations */
	NULL,			/* bus operations */
	NULL,			/* power */
	ddi_quiesce_not_needed,	/* quiesce done via iommulib by rootnex */
};

static struct modldrv modldrv = {
	&mod_driverops,
	"Intel IOMMU driver",
	&immu_ops,
};

static struct modlinkage modlinkage = {
	MODREV_1,
	&modldrv,
	NULL
};

int
_init(void)
{
	int err;

	err = immu_init();

	if (err == DDI_SUCCESS)
		err = mod_install(&modlinkage);

	if (err != DDI_SUCCESS)
		iommu_ops = NULL;

	return (err);
}

int
_fini(void)
{
	if (immu_setup)
		return (EBUSY);

	iommu_ops = NULL;

	return (mod_remove(&modlinkage));
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

struct iommu_ops intel_iommu_ops = {
	.ioo_start_units = immu_startup,
	.ioo_quiesce_units = immu_quiesce,
	.ioo_unquiesce_units = immu_unquiesce,
	.ioo_find_unit = immu_find_dvma_unit,
	.ioo_dev_reserved = immu_dev_reserved,
	.ioo_iommu_reserved = immu_iommu_reserved,
	.ioo_set_root_table = immu_set_root_table,
	.ioo_flush_domain = immu_flush_domain,
	.ioo_flush_pages = immu_flush_pages,
	.ioo_flush_buffers = immu_flush_buffers,

	.ioo_rmask = PDTE_MASK_R,
	.ioo_wmask = PDTE_MASK_W,
	.ioo_ptemask = { 0 },

	.ioo_dma_allochdl = iommu_dma_allochdl,
	.ioo_dma_freehdl = iommu_dma_freehdl,
	.ioo_dma_bindhdl = iommu_dma_bindhdl,
	.ioo_dma_unbindhdl = iommu_dma_unbindhdl,
	.ioo_dma_win = iommu_dma_win
};

static int
immu_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	immu_t *immu;
	int ret;

	mutex_enter(&immu_lock);

	immu = (immu_t *)iommulib_find_iommu(dip);

	if (immu == NULL) {
		mutex_exit(&immu_lock);
		return (DDI_FAILURE);
	}

	switch (cmd) {
	case DDI_ATTACH:
		mutex_enter(&immu->immu_iommu.iommu_lock);

		immu_intr_register(immu);
		immu_dvma_startup(immu);
		immu_intrmap_startup(immu);
		immu_qinv_startup(immu);

		/*
		 * Don't acually activate the unit yet. This
		 * will be done later.
		 */
		mutex_exit(&immu->immu_iommu.iommu_lock);
		ret = DDI_SUCCESS;
		break;
	case DDI_RESUME:
		ret = DDI_SUCCESS;
		break;
	default:
		ret = DDI_FAILURE;
		break;
	}

	mutex_exit(&immu_lock);

	return (ret);
}

/*
 * Check if the device has mobile 4 chipset
 */
static int
check_mobile4(dev_info_t *dip, void *arg)
{
	int vendor, device;
	int *ip = (int *)arg;

	vendor = ddi_prop_get_int(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "vendor-id", -1);
	device = ddi_prop_get_int(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "device-id", -1);

	if (vendor == 0x8086 && device == 0x2a40) {
		*ip = B_TRUE;
		ddi_err(DER_LOG, NULL, "iommu: Mobile 4 chipset detected. "
		    "Force setting IOMMU write buffer");
		return (DDI_WALK_TERMINATE);
	} else {
		return (DDI_WALK_CONTINUE);
	}
}

static void
walk_tree(int (*f)(dev_info_t *, void *), void *arg)
{
	int count;

	ndi_devi_enter(root_devinfo, &count);
	ddi_walk_devs(ddi_get_child(root_devinfo), f, arg);
	ndi_devi_exit(root_devinfo, count);
}

static int
check_pre_setup_quirks(dev_info_t *dip, void *arg)
{
	/* just 1 check right now */
	return (check_mobile4(dip, arg));
}

static void
pre_setup_quirks(void)
{
	walk_tree(check_pre_setup_quirks, &immu_quirk_mobile4);
}

static boolean_t
blacklisted_smbios(void)
{
	id_t smid;
	smbios_info_t sminf;
	smbios_system_t smsys;
	char *mfg, *product, *version;
	char **strptr;
	int i;

	/* need at least 4 strings for this setting */
	if (nblacks < 4) {
		return (B_FALSE);
	}

	if (ksmbios == NULL)
		return (B_FALSE);

	if ((smid = smbios_info_system(ksmbios, &smsys)) == SMB_ERR ||
	    smbios_info_common(ksmbios, smid, &sminf) == SMB_ERR) {
		return (B_FALSE);
	}

	mfg = (char *)sminf.smbi_manufacturer;
	product = (char *)sminf.smbi_product;
	version = (char *)sminf.smbi_version;

	ddi_err(DER_CONT, NULL, "?System SMBIOS information:\n");
	ddi_err(DER_CONT, NULL, "?Manufacturer = <%s>\n", mfg);
	ddi_err(DER_CONT, NULL, "?Product = <%s>\n", product);
	ddi_err(DER_CONT, NULL, "?Version = <%s>\n", version);

	for (i = 0; nblacks - i > 3; i++) {
		strptr = &black_array[i];
		if (strcmp(*strptr++, "SMBIOS") == 0) {
			if (strcmp(*strptr++, mfg) == 0 &&
			    ((char *)strptr == '\0' ||
			    strcmp(*strptr++, product) == 0) &&
			    ((char *)strptr == '\0' ||
			    strcmp(*strptr++, version) == 0)) {
				return (B_TRUE);
			}
			i += 3;
		}
	}

	return (B_FALSE);
}

static boolean_t
blacklisted_acpi(void)
{
	if (nblacks == 0) {
		return (B_FALSE);
	}

	return (immu_dmar_blacklisted(black_array, nblacks));
}

/*
 * Now set all the fields in the order they are defined
 * We do this only as a defensive-coding practice, it is
 * not a correctness issue.
 */
static void *
immu_state_alloc(int seg, void *dmar_unit)
{
	immu_t *immu;
	int instance;

	dmar_unit = immu_dmar_walk_units(seg, dmar_unit);
	if (dmar_unit == NULL) {
		/* No more IOMMUs in this segment */
		return (NULL);
	}

	immu = kmem_zalloc(sizeof (immu_t), KM_SLEEP);

	mutex_init(&immu->immu_iommu.iommu_lock, NULL, MUTEX_DRIVER, NULL);

	immu->immu_dmar_unit = dmar_unit;
	immu->immu_iommu.iommu_dip = immu_dmar_unit_dip(dmar_unit);
	immu->immu_iommu.iommu_seg = immu_dmar_unit_seg(dmar_unit);

	instance = ddi_get_instance(immu->immu_iommu.iommu_dip);

	immu->immu_name = iommulib_alloc_name(IMMU_UNIT_NAME, instance, NULL);
	if (immu->immu_name == NULL) {
		kmem_free(immu, sizeof (immu_t));
		return (NULL);
	}

	/*
	 * the immu_intr_lock mutex is grabbed by the IOMMU
	 * unit's interrupt handler so we need to use an
	 * interrupt cookie for the mutex
	 */
	mutex_init(&immu->immu_intr_lock, NULL, MUTEX_DRIVER,
	    (void *)ipltospl(IMMU_INTR_IPL));

	/* IOMMU regs related */
	mutex_init(&immu->immu_regs_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&immu->immu_regs_cv, NULL, CV_DEFAULT, NULL);
	immu->immu_regs_busy = B_FALSE;

	/* DVMA context related */
	mutex_init(&immu->immu_ctx_lock, NULL, MUTEX_DEFAULT, NULL);

	/* interrupt remapping related */
	mutex_init(&immu->immu_intrmap_lock, NULL, MUTEX_DEFAULT, NULL);

	/* qinv related */
	mutex_init(&immu->immu_qinv_lock, NULL, MUTEX_DEFAULT, NULL);

	/*
	 * insert this immu unit into the system-wide list
	 */
	iommulib_list_insert((iommu_t *)immu);

	immu_dmar_set_immu(dmar_unit, immu);

	return (dmar_unit);
}

static void
immu_subsystems_setup(void)
{
	int seg;
	void *unit_hdl;

	mutex_init(&immu_lock, NULL, MUTEX_DEFAULT, NULL);

	unit_hdl = NULL;
	for (seg = 0; seg < IMMU_MAXSEG; seg++) {
		while (unit_hdl = immu_state_alloc(seg, unit_hdl)) {
			;
		}
	}

	immu_regs_setup();	/* subsequent code needs this first */
	immu_regs_cleanup();
	if (immu_qinv_setup() == DDI_SUCCESS)
		immu_intrmap_setup();
	else
		immu_intrmap_enable = 0;
	immu_dvma_setup();
}

/*
 * immu_init()
 *	called from rootnex_attach(). setup but don't startup the Intel IOMMU
 *      This is the first function called in Intel IOMMU code
 */
int
immu_init(void)
{
	/* Set some global shorthands that are needed by all of IOMMU code */
	root_devinfo = ddi_root_node();

	/*
	 * Check the IOMMU enable boot-options first.
	 * This is so that we can skip parsing the ACPI table
	 * if necessary because that may cause problems in
	 * systems with buggy BIOS or ACPI tables
	 */

	iommulib_bootops(IMMU_DRIVER_NAME, &immu_enable, &immu_dvma_enable,
	    &immu_intrmap_enable);

	if (!immu_enable)
		return (ENOTSUP);

	if (immu_intrmap_enable)
		immu_qinv_enable = 1;

	/*
	 * Next, check if the system even has an Intel IOMMU
	 * We use the presence or absence of the IOMMU ACPI
	 * table to detect Intel IOMMU.
	 */
	if (immu_dmar_setup() != DDI_SUCCESS) {
		immu_enable = 0;
		return (ENOTSUP);
	}

	/*
	 * Check blacklists
	 */

	if (blacklisted_smbios()) {
		immu_enable = 0;
		return (ENOTSUP);
	}

	/*
	 * Read the "raw" DMAR ACPI table to get information
	 * and convert into a form we can use.
	 */
	if (immu_dmar_parse() != DDI_SUCCESS) {
		immu_enable = 0;
		return (ENOTSUP);
	}

	/*
	 * now that we have processed the ACPI table
	 * check if we need to blacklist this system
	 * based on ACPI info
	 */
	if (blacklisted_acpi()) {
		immu_dmar_destroy();
		immu_enable = 0;
		return (ENOTSUP);
	}

	/*
	 * Check if system has HW quirks.
	 */
	pre_setup_quirks();

	/* Now do the rest of the setup */
	immu_subsystems_setup();

	immu_setup = B_TRUE;

	iommu_ops = &intel_iommu_ops;

	return (0);
}

/*ARGSUSED*/
static int
immu_startup_unit(iommu_t *iommu, void *arg)
{
	mutex_enter(&iommu->iommu_lock);
	immu_regs_startup((immu_t *)iommu);
	mutex_exit(&iommu->iommu_lock);

	immu_running = B_TRUE;

	return (IWALK_SUCCESS_CONTINUE);
}

static void
immu_startup(void)
{
	iommulib_list_walk(immu_startup_unit, NULL, NULL);
}

/*
 * Hook to notify IOMMU code of device tree changes
 */
void
immu_device_tree_changed(void)
{
	if (!immu_setup)
		return;

	ddi_err(DER_WARN, NULL, "Intel IOMMU currently "
	    "does not use device tree updates");
}

/*ARGSUSED*/
static int
immu_quiesce_unit(iommu_t *iommu, void *arg)
{
	immu_t *immu;

	immu = (immu_t *)iommu;
	/* if immu is not running, we dont quiesce */
	if (!immu->immu_regs_running)
		return (IWALK_SUCCESS_CONTINUE);

	/* flush caches */
	mutex_enter(&immu->immu_ctx_lock);
	immu_flush_context_gbl(immu, &immu->immu_ctx_inv_wait);
	immu_flush_iotlb_gbl(immu, &immu->immu_ctx_inv_wait);
	mutex_exit(&immu->immu_ctx_lock);
	immu_regs_wbf_flush(immu);

	if (immu_dvma_enable)
		immu_regs_dvma_disable(immu);

	if (immu_intrmap_enable)
		immu_regs_intrmap_disable(immu);

	return (IWALK_SUCCESS_CONTINUE);
}

/*ARGSUSED*/
static int
immu_suspend_unit(iommu_t *iommu, void *arg)
{
	immu_t *immu;

	immu = (immu_t *)iommu;

	mutex_enter(&iommu->iommu_lock);

	if (immu_qinv_enable)
		immu_regs_qinv_disable(immu);

	immu_regs_unmap(immu);

	immu->immu_regs_quiesced = B_TRUE;
	immu->immu_regs_running = B_FALSE;

	mutex_exit(&iommu->iommu_lock);

	return (IWALK_SUCCESS_CONTINUE);
}

/*
 * immu_quiesce()
 * 	quiesce all units that are running
 */
/*ARGSUSED*/
int
immu_quiesce(int reboot)
{
	int nerr;

	mutex_enter(&immu_lock);

	if (!immu_running) {
		mutex_exit(&immu_lock);
		return (DDI_SUCCESS);
	}

	/*
	 * On some systems, IOMMU units are interdependent in
	 * such a way that disabling queued invalidation on
	 * one unit will disable it on all. Therefore,
	 * there are two separate loops here: one to
	 * prepare for shutdown (using queued invalidation
	 * for flushes), and a second loop to actually do
	 * the shutdown.
	 */
	iommulib_list_walk(immu_quiesce_unit, NULL, NULL);
	iommulib_list_walk(immu_suspend_unit, NULL, &nerr);

	if (nerr == 0) {
		immu_running = B_FALSE;
		immu_quiesced = B_TRUE;
	}
	mutex_exit(&immu_lock);

	return (immu_quiesced ? DDI_SUCCESS : DDI_FAILURE);
}

/*ARGSUSED*/
static int
immu_resume_unit(iommu_t *iommu, void *arg)
{
	immu_t *immu;

	immu = (immu_t *)iommu;

	mutex_enter(&iommu->iommu_lock);

	if (!immu->immu_regs_quiesced) {
		mutex_exit(&iommu->iommu_lock);
		return (IWALK_SUCCESS_CONTINUE);
	}

	if (immu_regs_map(immu) != DDI_SUCCESS) {
		mutex_exit(&iommu->iommu_lock);
		return (IWALK_FAILURE_CONTINUE);
	}

	if (immu->immu_regs_intr_msi_addr != 0)
		immu_regs_intr_enable(immu, immu->immu_regs_intr_msi_addr,
		    immu->immu_regs_intr_msi_data, immu->immu_regs_intr_uaddr);

	(void) immu_intr_handler(immu);

	if (immu_qinv_enable)
		immu_regs_qinv_enable(immu, immu->immu_qinv_reg_value);

	mutex_exit(&iommu->iommu_lock);

	return (IWALK_SUCCESS_CONTINUE);
}

/*ARGSUSED*/
static int
immu_unquiesce_unit(iommu_t *iommu, void *arg)
{
	immu_t *immu;

	immu = (immu_t *)iommu;

	mutex_enter(&iommu->iommu_lock);

	if (immu_intrmap_enable)
		immu_regs_intrmap_enable(immu, immu->immu_intrmap_irta_reg);

	if (immu_dvma_enable)
		immu_regs_set_root_table(immu);

	mutex_enter(&immu->immu_ctx_lock);
	immu_flush_context_gbl(immu, &immu->immu_ctx_inv_wait);
	immu_flush_iotlb_gbl(immu, &immu->immu_ctx_inv_wait);
	mutex_exit(&immu->immu_ctx_lock);

	if (immu_dvma_enable)
		immu_regs_dvma_enable(immu);

	immu->immu_regs_quiesced = B_FALSE;
	immu->immu_regs_running = B_TRUE;

	mutex_exit(&iommu->iommu_lock);

	return (IWALK_SUCCESS_CONTINUE);
}

/*
 * immu_unquiesce()
 * 	unquiesce all units
 */
int
immu_unquiesce(void)
{
	int nerr;

	mutex_enter(&immu_lock);

	if (!immu_quiesced) {
		mutex_exit(&immu_lock);
		return (DDI_SUCCESS);
	}

	iommulib_list_walk(immu_resume_unit, NULL, &nerr);
	iommulib_list_walk(immu_unquiesce_unit, NULL, &nerr);

	if (nerr == 0) {
		immu_quiesced = B_FALSE;
		immu_running = B_TRUE;
	}

	mutex_exit(&immu_lock);

	return (nerr == 0 ? DDI_SUCCESS : DDI_FAILURE);
}

void
immu_flush_domain(iommu_t *iommu, uint_t domid, iommu_wait_t *iwp)
{
	iommu_wait_t iw;

	if (iwp == NULL) {
		iommulib_init_wait(&iw, "domflush", B_TRUE);
		iwp = &iw;
	}

	immu_flush_iotlb_dsi((immu_t *)iommu, domid, iwp);
}

void
immu_flush_pages(iommu_t *iommu, uint_t domid, uint64_t sdvma,
    uint_t npages, iommu_wait_t *iwp)
{
	iommu_wait_t iw;

	if (iwp == NULL) {
		iommulib_init_wait(&iw, "pageflush", B_TRUE);
		iwp = &iw;
	}

	immu_flush_iotlb_psi((immu_t *)iommu, domid, sdvma, npages,
	    TLB_IVA_WHOLE, iwp);
}

void
immu_flush_buffers(iommu_t *iommu)
{
	immu_regs_wbf_flush((immu_t *)iommu);
}
