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
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */
/*
 * Copyright (c) 2010 by Chelsio Communications, Inc.
 */

#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/sunndi.h>
#include <sys/conf.h>
#include <sys/kmem.h>
#include <sys/ksynch.h>
#include <sys/debug.h>
#include <sys/modctl.h>
#include <sys/proc.h>
#include <sys/sysmacros.h>
#include <sys/stat.h>
#include <sys/autoconf.h>
#include <sys/open.h>
#include <sys/atomic.h>
#include <sys/cpuvar.h>
#include <sys/ddi_impldefs.h>
#include <sys/pci.h>
#include <sys/ethernet.h>
#include <netinet/in.h>

#include "cxge_common.h"
#include "cxge_regs.h"
#include "cxge_fw_exports.h"
#include "cxge_version.h"
#include "cxgen.h"

#define	CXGEN_TICKS	drv_usectohz(1000000)

ddi_device_acc_attr_t cxgen_acc_attr = {
	.devacc_attr_version = DDI_DEVICE_ATTR_V0,
	.devacc_attr_endian_flags = DDI_STRUCTURE_LE_ACC,
	.devacc_attr_dataorder = DDI_UNORDERED_OK_ACC
};

/*
 * Bus Operation functions
 */
static int cxgen_ctlops(dev_info_t *, dev_info_t *,
			ddi_ctl_enum_t, void *, void *);
static int cxgen_bus_config(dev_info_t *, uint_t, ddi_bus_config_op_t, void *,
	dev_info_t **);
static int cxgen_bus_unconfig(dev_info_t *, uint_t, ddi_bus_config_op_t,
	void *);
/*
 * Device Node Operation functions
 */
static int cxgen_attach(dev_info_t *, ddi_attach_cmd_t);
static int cxgen_detach(dev_info_t *, ddi_detach_cmd_t);
static int cxgen_info(dev_info_t *, ddi_info_cmd_t, void *, void **);
static int cxgen_quiesce(dev_info_t *);
static int cxgen_suspend(dev_info_t *);
static int cxgen_resume(dev_info_t *);

/*
 * Character/Block Operation functions
 */
static int cxgen_open(dev_t *, int, int, cred_t *);
static int cxgen_close(dev_t, int, int, cred_t *);
extern int cxgen_ioctl(dev_t, int, intptr_t, int, cred_t *, int *);

/*
 * Internal routines in support of particular cxgen_ctlops.
 */
static int cxgen_ctlop_initchild(dev_info_t *, dev_info_t *);

/*
 * Misc. helper functions.
 */
static void cxgen_cleanup(p_adapter_t, uint_t);
static const struct adapter_info *cxgen_identify_card(p_adapter_t);
static int cxgen_map_regs(p_adapter_t);
static void cxgen_unmap_regs(p_adapter_t);
static int cxgen_setup_mutexes(p_adapter_t);
static void cxgen_destroy_mutexes(p_adapter_t);
static int cxgen_autoupdate_fw(p_adapter_t);
static int cxgen_add_node(p_adapter_t, int);
static void cxgen_remove_node(p_adapter_t, int);
static void cxgen_start_tick(p_adapter_t);
static void cxgen_tick_handler(void *);
static void cxgen_stop_tick(p_adapter_t);
static int setup_sge_qsets(p_adapter_t);
static int release_sge_qsets(p_adapter_t);
static void cxgen_create_rx_threads(p_adapter_t cxgenp);
static void cxgen_destroy_rx_threads(p_adapter_t cxgenp);
static void cxgen_rx_thread(struct sge_qset *);
static void setup_rss(adapter_t *);

struct cb_ops cxgen_cb_ops = {
	cxgen_open,			/* open */
	cxgen_close,			/* close */
	nodev,				/* strategy */
	nodev,				/* print */
	nodev,				/* dump */
	nodev,				/* read */
	nodev,				/* write */
	cxgen_ioctl,			/* ioctl */
	nodev,				/* devmap */
	nodev,				/* mmap */
	nodev,				/* segmap */
	nochpoll,			/* poll */
	ddi_prop_op,			/* cb_prop_op */
	NULL,				/* streamtab */
	D_NEW | D_MP | D_HOTPLUG,	/* Driver compatibility flag */
	CB_REV,				/* rev */
	nodev,				/* int (*cb_aread)() */
	nodev				/* int (*cb_awrite)() */
};

struct bus_ops cxgen_bus_ops = {
	BUSO_REV,		/* busops_rev */
	NULL,			/* bus_map */
	NULL,			/* bus_get_intrspec */
	NULL,			/* bus_add_intrspec */
	NULL,			/* bus_remove_intrspec */
	NULL,			/* bus_map_fault */
	NULL,			/* bus_dma_map */
	NULL,			/* bus_dma_allochdl */
	NULL,			/* bus_dma_freehdl */
	NULL,			/* bus_dma_bindhdl */
	NULL,			/* bus_dma_unbindhdl */
	NULL,			/* bus_dma_flush */
	NULL,			/* bus_dma_win */
	NULL,			/* bus_dma_ctl */
	cxgen_ctlops,		/* bus_ctl */
	ddi_bus_prop_op,	/* bus_prop_op; for ddi_prop_* calls in cxge */
	NULL,			/* bus_get_eventcookie */
	NULL,			/* bus_add_eventcall */
	NULL,			/* bus_remove_eventcall */
	NULL,			/* bus_post_event */
	NULL,			/* bus_intr_ctl, */
	cxgen_bus_config, 	/* bus_config */
	cxgen_bus_unconfig, 	/* bus_unconfig */
	NULL,			/* bus_fm_init */
	NULL,			/* bus_fm_fini */
	NULL,			/* bus_fm_access_enter */
	NULL,			/* bus_fm_access_exit */
	NULL,			/* bus_power */
	NULL			/* bus_intr_op */
};

struct dev_ops cxgen_ops = {
	DEVO_REV,		/* devo_rev */
	0,			/* refcnt  */
	cxgen_info,		/* info */
	nulldev,		/* identify */
	nulldev,		/* probe */
	cxgen_attach,		/* attach */
	cxgen_detach,		/* detach */
	nodev,			/* reset */
	&cxgen_cb_ops,		/* driver operations */
	&cxgen_bus_ops,		/* bus operations */
	nulldev,		/* power management */
	cxgen_quiesce		/* quiesce - fast-reboot */
};

/*
 * Module linkage information for the kernel.
 */
static struct modldrv modldrv = {
	&mod_driverops, /* Type of module */
	"CXGEN Driver " DRV_VERSION,
	&cxgen_ops,	/* driver ops */
};

static struct modlinkage modlinkage = {
	MODREV_1,
	(void *)&modldrv,
	NULL
};

/*
 * This list contains the instance structures for the Terminator 3
 * devices present in the system.
 */
void *cxgen_list;

int
_init(void)
{
	int status;

	status = ddi_soft_state_init(&cxgen_list, sizeof (adapter_t), 0);
	if (status != 0)
		return (status);

	return (mod_install(&modlinkage));
}

int
_fini(void)
{
	int status;

	status = mod_remove(&modlinkage);
	if (status != 0)
		return (status);

	ddi_soft_state_fini(&cxgen_list);
	return (0);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

/*
 * Bus Operation functions
 */

static int
cxgen_ctlops(dev_info_t *dip, dev_info_t *rdip,
	ddi_ctl_enum_t ctlop, void *arg, void *result)
{
	dev_info_t *child = (dev_info_t *)arg;

	switch (ctlop) {
	case DDI_CTLOPS_REPORTDEV:
		cmn_err(CE_CONT, "?%s%d is port#%s on %s%d\n",
		    ddi_node_name(rdip), ddi_get_instance(rdip),
		    ddi_get_name_addr(rdip), ddi_driver_name(dip),
		    ddi_get_instance(dip));
		return (DDI_SUCCESS);

	case DDI_CTLOPS_INITCHILD:
		/* give a decent name_addr to the child (based on its u-a) */
		return (cxgen_ctlop_initchild(dip, child));

	case DDI_CTLOPS_UNINITCHILD:
		ddi_set_name_addr(child, NULL);
		return (DDI_SUCCESS);

	case DDI_CTLOPS_ATTACH:
	/* FALLTHRU */
	case DDI_CTLOPS_DETACH:
		return (DDI_SUCCESS);

	/* log message and fall through for everything else */
	default:
#ifdef DEBUG
		cmn_err(CE_NOTE, "%s: unimplemented cxgen_ctlop %d",
		    __func__, ctlop);
#endif
		break;
	}

	/* pass it up */
	return (ddi_ctlops(dip, rdip, ctlop, arg, result));
}

static int
cxgen_bus_config(dev_info_t *pdip, uint_t flags, ddi_bus_config_op_t op,
    void *arg, dev_info_t **childp)
{
	p_adapter_t cxgenp;
	int instance, node = -1;
	char *c;

	ASSERT(pdip);
	instance = ddi_get_instance(pdip);
	cxgenp = ddi_get_soft_state(cxgen_list, instance);
	ASSERT(cxgenp);

	switch (op) {
	case BUS_CONFIG_ONE:
		/* very simplistic - last char of arg is the child's u-a */
		if (arg == NULL)
			return (DDI_FAILURE);
		c = arg;
		while (*(c + 1))
			c++;
		ASSERT(*(c - 1) == '@');

		node = *c - '0';
		if (node < 0 || node >= cxgenp->params.nports)
			return (DDI_FAILURE);

	/* FALLTHRU */
	case BUS_CONFIG_ALL:
	/* FALLTHRU */
	case BUS_CONFIG_DRIVER:

		/* allocate and bind requested child device nodes */
		(void) cxgen_add_node(cxgenp, node);

		flags |= NDI_ONLINE_ATTACH;
		return (ndi_busop_bus_config(pdip, flags, op, arg, childp, 0));

	/* log message and fall through for everything else */
	default:
#ifdef DEBUG
		cmn_err(CE_NOTE, "%s: unimplemented bus_config %d",
		    __func__, op);
#endif
		break;
	}

	/* pass it up */
	return (ndi_busop_bus_config(pdip, flags, op, arg, childp, 0));
}

static int
cxgen_bus_unconfig(dev_info_t *pdip, uint_t flags, ddi_bus_config_op_t op,
    void *arg)
{
	p_adapter_t cxgenp;
	int instance, node = -1, rc;
	char *c;

	ASSERT(pdip);
	instance = ddi_get_instance(pdip);
	cxgenp = ddi_get_soft_state(cxgen_list, instance);
	ASSERT(cxgenp);

	switch (op) {
	case BUS_UNCONFIG_ONE:
		/* very simplistic - last char of arg is the child's u-a */
		if (arg == NULL)
			return (DDI_FAILURE);
		c = arg;
		while (*(c + 1))
			c++;
		ASSERT(*(c - 1) == '@');

		node = *c - '0';
		if (node < 0 || node >= cxgenp->params.nports)
			return (DDI_FAILURE);

	/* FALLTHRU */
	case BUS_UNCONFIG_ALL:
	/* FALLTHRU */
	case BUS_UNCONFIG_DRIVER:

		/* unconfig is not always honoured, eg with NDI_AUTODETACH */
		flags |= NDI_UNCONFIG;
		rc = ndi_busop_bus_unconfig(pdip, flags, op, arg);

		/* free child device nodes, the children are gone */
		cxgen_remove_node(cxgenp, node);

		return (rc);

	/* log message and fall through for everything else */
	default:
#ifdef DEBUG
		cmn_err(CE_NOTE, "%s: unimplemented bus_unconfig %d",
		    __func__, op);
#endif
		break;
	}

	/* pass it up */
	return (ndi_busop_bus_unconfig(pdip, flags, op, arg));
}

/*
 * Device Node Operation functions
 */

#define	CXGEN_CLEANUP_SOFTSTATE	0x01
#define	CXGEN_CLEANUP_FMA	0x02
#define	CXGEN_CLEANUP_REGMAPS	0x04
#define	CXGEN_CLEANUP_INTRS	0x08
#define	CXGEN_CLEANUP_MUTEXES	0x10
#define	CXGEN_CLEANUP_KSTATS	0x20
#define	CXGEN_CLEANUP_MINORNODE	0x40
#define	CXGEN_CLEANUP_TASKQ	0x80

static int
cxgen_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	int status, instance, i, intr_types;
	p_adapter_t cxgenp;
	char name[32];
	uint_t cleanup = 0, qsets_per_port, nports;
	const struct adapter_info *ai;
	uint32_t vers;

	switch (cmd) {
	case DDI_ATTACH:
		break;

	case DDI_RESUME:
		status = cxgen_resume(dip);
		goto cxgen_resume_exit;

	default:
		status = DDI_FAILURE;
		goto cxgen_attach_exit;
	}

	/*
	 * Get the device instance since we'll need to setup
	 * or retrieve a soft state for this instance.
	 */
	instance = ddi_get_instance(dip);

	/*
	 * Allocate soft device data structure
	 */
	status = ddi_soft_state_zalloc(cxgen_list, instance);
	if (status != DDI_SUCCESS) {
		cmn_err(CE_WARN, "Failed to allocate soft_state: %d", status);
		goto cxgen_attach_exit;
	}
	cleanup |= CXGEN_CLEANUP_SOFTSTATE;

	cxgenp = ddi_get_soft_state(cxgen_list, instance);
	ASSERT(cxgenp);

	cxgenp->instance = instance;
	cxgenp->dip = dip;

	/* FMA */
	cxgenp->fma_cap = ddi_prop_get_int(DDI_DEV_T_ANY, dip,
	    DDI_PROP_DONTPASS, "fm-capable", (DDI_FM_EREPORT_CAPABLE |
	    DDI_FM_ACCCHK_CAPABLE | DDI_FM_DMACHK_CAPABLE |
	    DDI_FM_ERRCB_CAPABLE));
	cxgen_fm_init(cxgenp);
	cleanup |= CXGEN_CLEANUP_FMA;

	/*
	 * Map PCI config space and MMIO registers.
	 */
	status = cxgen_map_regs(cxgenp);
	if (status != DDI_SUCCESS) {
		/* Error logged already */
		goto cxgen_attach_exit;
	}
	cleanup |= CXGEN_CLEANUP_REGMAPS;

	/*
	 * Identify the model of the card.
	 */
	ai = cxgen_identify_card(cxgenp);
	if (ai == NULL) {
		/* Error logged already */
		status = DDI_FAILURE;
		goto cxgen_attach_exit;
	}
	/* nothing to add to cleanup */

	/*
	 * Calculate number of qsets per port based on sge-engines property.
	 * If it is non-zero, use it (unless it is wrong - more than 8, etc.)
	 *
	 * Accessing ai->nportsX is a bit hackish, we should let t3_prep_adapter
	 * set cxgenp->params.nports for us.  But we need the total number of
	 * ports in order to decide how many interrupts to allocate, and we need
	 * the interrupts allocated before we initialize mutexes, and the
	 * mutexes have to be initialized before t3_prep_adapter is called.
	 */
	cxgenp->params.nports = nports = ai->nports0 + ai->nports1;
	intr_types = ddi_prop_get_int(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "interrupt-types", 0x7);
	qsets_per_port = ddi_prop_get_int(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "sge-engines", min(SGE_QSETS / nports, ncpus_online));
	while (qsets_per_port * nports > SGE_QSETS)
		qsets_per_port--;

#if defined(__sparc)
	/*
	 * Adjust count further downwards if we will try MSI-X later.  This is
	 * because Solaris on SPARC allows the allocation of 8 vectors max.  Out
	 * of that we can give 7 to the qsets.
	 */
	if (intr_types & DDI_INTR_TYPE_MSIX) {
		while (qsets_per_port * nports > 7)
			qsets_per_port--;
	}
#endif
	cxgenp->qsets_per_port = qsets_per_port;
	/* nothing to add to cleanup */

	/*
	 * Initialize interrupts.  Must be done before locks can be init'ed.
	 * Uses qsets_per_port and cxgenp->params.nports, which must be setup
	 * before this point.
	 *
	 */
	status = cxgen_alloc_intr(cxgenp, intr_types);
	if (status != DDI_SUCCESS) {
		cmn_err(CE_WARN, "Failed to set up interrupts (tried %x)",
		    intr_types);
		goto cxgen_attach_exit;
	}
	cleanup |= CXGEN_CLEANUP_INTRS;

	/*
	 * Initialize locks.  Must be done before t3_prep_adapter is called, as
	 * some of the locks are for common code and may be used when we call
	 * into t3_prep_adapter.
	 *
	 * Once again we rely on cxgenp->params.nports being set even before
	 * calling t3_prep_adapter.
	 */
	status = cxgen_setup_mutexes(cxgenp);
	if (status != DDI_SUCCESS) {
		cmn_err(CE_WARN, "Failed to set up driver locks: %d", status);
		goto cxgen_attach_exit;
	}
	cleanup |= CXGEN_CLEANUP_MUTEXES;

	/*
	 * Populate port_info structures for all ports that exist.  Must be done
	 * before t3_prep_adapter as cphy_init needs a good pi->adapter.
	 *
	 * Yet again we rely on cxgenp->params.nports being set even before
	 * calling t3_prep_adapter.
	 */
	ASSERT(cxgenp->params.nports);
	for (i = 0; i < cxgenp->params.nports; i++) {
		struct port_info *pi;

		/* Fill up the port_info structure */
		pi = &cxgenp->port[i];
		pi->adapter = cxgenp;
		pi->port_id = (uint8_t)i;
		pi->nqsets = qsets_per_port;
		pi->first_qset = i * qsets_per_port;
		pi->tx_chan = i >= ai->nports0;
		pi->txpkt_intf = pi->tx_chan ? 2 * (i - ai->nports0) + 1 :
		    2 * i;
		cxgenp->rxpkt_map[pi->txpkt_intf] = (uint8_t)i;
	}

	/*
	 * Prepare the adapter.  Also populates cxgen->params.
	 */
	status = t3_prep_adapter(cxgenp, ai, 1);
	if (status < 0) {
		cmn_err(CE_WARN, "Unable to prepare adapter: %d", status);
		cxgen_fm_ereport_post(cxgenp, DDI_FM_DEVICE_INVAL_STATE);
		status = DDI_FAILURE;
		goto cxgen_attach_exit;
	}
	/* nothing to add to cleanup, there is no "unprep" */
	if (cxgen_fm_check_acc_handle(cxgenp->regh) != DDI_FM_OK) {
		ddi_fm_service_impact(cxgenp->dip, DDI_SERVICE_LOST);
		status = DDI_FAILURE;
		goto cxgen_attach_exit;
	}

	/*
	 * Create the device node. Shows up as /dev/cxgenX
	 */
	(void) sprintf(name, CXGEN_DEVNAME "%d", instance);
	status = ddi_create_minor_node(dip, name, S_IFCHR, instance,
	    DDI_NT_NEXUS, 0);
	if (status != DDI_SUCCESS) {
		cmn_err(CE_WARN, "Failed to create minor node: %d", status);
		goto cxgen_attach_exit;
	}
	cleanup |= CXGEN_CLEANUP_MINORNODE;

	/*
	 * Auto-update the firmware.
	 */
	status = cxgen_autoupdate_fw(cxgenp);
	if (status != DDI_SUCCESS) {
		/* Error logged already */
		goto cxgen_attach_exit;
	}
	(void) t3_get_fw_version(cxgenp, &vers);
	(void) snprintf(cxgenp->fw_vers, sizeof (cxgenp->fw_vers), "%c%d.%d.%d",
	    G_FW_VERSION_TYPE(vers) ? 'T' : 'N', G_FW_VERSION_MAJOR(vers),
	    G_FW_VERSION_MINOR(vers), G_FW_VERSION_MICRO(vers));
	(void) t3_get_tp_version(cxgenp, &vers);
	(void) snprintf(cxgenp->mc_vers, sizeof (cxgenp->mc_vers), "%d.%d.%d",
	    G_TP_VERSION_MAJOR(vers), G_TP_VERSION_MINOR(vers),
	    G_TP_VERSION_MICRO(vers));

	/* FMA: Check for Access Error */
	if (cxgen_fm_check_acc_handle(cxgenp->regh) != DDI_FM_OK) {
		status = DDI_FAILURE;
		ddi_fm_service_impact(cxgenp->dip, DDI_SERVICE_LOST);
		goto cxgen_attach_exit;
	}

	/*
	 * kstats
	 */
	status = cxgen_setup_kstats(cxgenp);
	if (status != DDI_SUCCESS) {
		cmn_err(CE_WARN, "Failed to create config kstats: %d", status);
		goto cxgen_attach_exit;
	}
	cleanup |= CXGEN_CLEANUP_KSTATS;

	/*
	 * Task queue.
	 */
	(void) sprintf(name, "tq%d", instance);
	cxgenp->tq = ddi_taskq_create(dip, name, 1, TASKQ_DEFAULTPRI, 0);
	if (cxgenp->tq == NULL) {
		cmn_err(CE_WARN, "Failed to create task queue.");
		goto cxgen_attach_exit;
	}
	cleanup |= CXGEN_CLEANUP_TASKQ;

	ASSERT(status == DDI_SUCCESS);

	/*
	 * All done.  Switch on the LED and let everyone know. Full init is not
	 * complete so we don't set flags |= FULL_INIT_DONE here.
	 */
	t3_led_ready(cxgenp);
	if (cxgen_fm_check_acc_handle(cxgenp->regh) != DDI_FM_OK) {
		ddi_fm_service_impact(cxgenp->dip, DDI_SERVICE_LOST);
		status = DDI_FAILURE;
		goto cxgen_attach_exit;
	}
	ddi_report_dev(dip);
	cmn_err(CE_NOTE, CXGEN_DEVNAME "%d: %u qsets per port, %d %s.",
	    instance, cxgenp->port[0].nqsets, cxgenp->intr_count,
	    cxgenp->intr_type == DDI_INTR_TYPE_MSIX ? "MSI-X interrupts" :
	    cxgenp->intr_type == DDI_INTR_TYPE_MSI ? "MSI interrupts" :
	    "fixed interrupt");

cxgen_attach_exit:
	if (status != DDI_SUCCESS)
		cxgen_cleanup(cxgenp, cleanup);

cxgen_resume_exit:
	return (status);
}

static int
cxgen_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	p_adapter_t cxgenp;
	int instance, status = DDI_SUCCESS;

	/*
	 * Get the device instance since we'll need to
	 * retrieve a soft state for this instance.
	 */
	instance = ddi_get_instance(dip);
	cxgenp = ddi_get_soft_state(cxgen_list, instance);
	ASSERT(cxgenp);

	switch (cmd) {
	case DDI_SUSPEND:
		status = cxgen_suspend(dip);
		break;

	case DDI_DETACH:
		if (cxgenp->flags & FULL_INIT_DONE) {
			if ((cxgenp->flags & USING_MSIX) == 0)
				cxgen_destroy_rx_threads(cxgenp);
			(void) release_sge_qsets(cxgenp);
		}

		cxgen_cleanup(cxgenp, ~0);

		break;

	default:
		status = DDI_FAILURE;
		goto cxgen_detach_exit;
	}

cxgen_detach_exit:
	return (status);
}

/* ARGSUSED */
static int
cxgen_info(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg, void **result)
{
	return (DDI_FAILURE);
}

/*
 * cxgen_suspend
 *
 */
static int
cxgen_suspend(dev_info_t *dip)
{
	p_adapter_t cxgenp;
	struct port_info *pi;
	int index = 0, status = DDI_SUCCESS;

	/* Retrieve soft state for this instance */
	index = ddi_get_instance(dip);
	cxgenp = ddi_get_soft_state(cxgen_list, index);

	ASSERT(cxgenp);

	/* Unplumb each plumbed port */
	for (index = 0; index < cxgenp->params.nports; index++) {
		pi = &cxgenp->port[index];

		/*
		 * If the callback port_suspend() is defined, that makes sure
		 * that the port was plumbed. Unplumb those plumbed ports.
		 */
		if (pi->port_suspend) {
			status = (pi->port_suspend)(pi);
			if (status != DDI_SUCCESS)
				break;
		}
	}

	return (status);
}

/*
 * cxgen_resume
 */
static int
cxgen_resume(dev_info_t *dip)
{
	p_adapter_t cxgenp;
	struct port_info *pi;
	int index = 0, status = DDI_SUCCESS;

	/* Retrieve soft state for this instance */
	index = ddi_get_instance(dip);
	cxgenp = ddi_get_soft_state(cxgen_list, index);

	ASSERT(cxgenp);

	/* Plumb each port */
	for (index = 0; index < cxgenp->params.nports; index++) {
		pi = &cxgenp->port[index];

		/*
		 * port_resume is defined if that port was plumbed
		 * This make sures that we plumb only those ports which were
		 * plumbed before suspend.
		 */
		if (pi->port_resume) {
			status = (pi->port_resume)(pi);
			if (status != DDI_SUCCESS)
				break;
		}
	}

	return (status);
}

/*
 * cxge_quiesce
 *
 * This function quiesces the device so that the device no longer generates
 * interrupts and modifies or accesses memory. This function enables
 * fast-reboot.
 */
static int
cxgen_quiesce(dev_info_t *dip)
{
	p_adapter_t cxgenp = NULL;
	struct cphy *phy = NULL;
	int index = 0;

	/* Retrieve soft state for this instance */
	index = ddi_get_instance(dip);
	cxgenp = ddi_get_soft_state(cxgen_list, index);
	ASSERT(cxgenp);

	/* Disable T3 interrupts */
	t3_intr_disable(cxgenp);

	/* Stop SGE */
	t3_sge_stop(cxgenp);

	/* Power down ports */
	for (index = 0; index < cxgenp->params.nports; index++) {
		phy = &cxgenp->port[index].phy;
		phy->ops->power_down(phy, 1);
	}

	return (DDI_SUCCESS);
}


/*
 * Character/Block Operation functions
 */

/* ARGSUSED */
static int
cxgen_open(dev_t *devp, int flag, int otyp, cred_t *credp)
{
	int status;
	int instance;
	p_adapter_t cxgenp;

	status = EINVAL;
	if (otyp != OTYP_CHR)
		goto cxgen_open_exit;

	status = ENXIO;
	instance = getminor(*devp);
	cxgenp = ddi_get_soft_state(cxgen_list, instance);
	if (cxgenp == NULL)
		goto cxgen_open_exit;

	status = atomic_cas_uint(&cxgenp->open, 0, EBUSY);

cxgen_open_exit:
	return (status);
}

/* ARGSUSED */
static int
cxgen_close(dev_t dev, int flag, int otyp, cred_t *credp)
{
	int instance;
	p_adapter_t cxgenp;

	instance = getminor(dev);
	cxgenp = ddi_get_soft_state(cxgen_list, instance);
	if (cxgenp == NULL)
		goto cxgen_close_exit;

	(void) atomic_swap_uint(&cxgenp->open, 0);

cxgen_close_exit:
	return (0);
}

/*
 * Internal routines in support of particular cxgen_ctlops.
 */

static int
cxgen_ctlop_initchild(dev_info_t *dip, dev_info_t *child)
{
	char *unit_addr;
	int rc, instance;

	instance = ddi_get_instance(dip);

	ASSERT(instance >= 0);
	ASSERT(dip == ddi_get_parent(child));

	if (ndi_dev_is_persistent_node(child) == 0) {
		/*
		 * pseudo-node: as long as cxge.conf has global properties only,
		 * no pseudo-node is created and we'll be just fine.  Log a
		 * warning if the conf file needs a node merge.
		 *
		 * This cxgen.conf will work:
		 * accept-jumbo = 1;
		 *
		 * This won't:
		 * name="cxge" parent="cxgen" instance=0 accept-jumbo=1;
		 */
#ifdef DEBUG
		cmn_err(CE_WARN, "%s: %s.conf support not fully implemented.",
		    __func__, ddi_driver_name(child));
#endif
		return (DDI_NOT_WELL_FORMED);
	}

	rc = ddi_prop_lookup_string(DDI_DEV_T_ANY, child, DDI_PROP_DONTPASS,
	    "unit-address", &unit_addr);
	if (rc != DDI_PROP_SUCCESS) {
		cmn_err(CE_WARN, "unit-address prop lookup failure: %d, "
		    "child = %p", rc, (void *)child);
		return (DDI_NOT_WELL_FORMED);
	}

	/* This was the entire purpose of initchild */
	ddi_set_name_addr(child, unit_addr);

	ddi_prop_free(unit_addr);

	return (DDI_SUCCESS);
}

/*
 * Cleans up the given adapter_t structure.  What is to be cleaned is specified
 * in the mask.
 */
static void
cxgen_cleanup(p_adapter_t cxgenp, uint_t mask)
{
	char name[32];

	/*
	 * The order in which things are cleaned up is important!  Be careful if
	 * you rearrange anything.  Best is to stick to the reverse of the order
	 * in cxgen_attach.
	 */

	if (mask & CXGEN_CLEANUP_TASKQ) {
		ddi_taskq_destroy(cxgenp->tq);
		cxgenp->tq = NULL;
	}

	if (mask & CXGEN_CLEANUP_KSTATS)
		cxgen_destroy_kstats(cxgenp);

	if (mask & CXGEN_CLEANUP_MINORNODE) {
		(void) sprintf(name, CXGEN_DEVNAME "%d", cxgenp->instance);
		ddi_remove_minor_node(cxgenp->dip, name);
	}

	if (mask & CXGEN_CLEANUP_MUTEXES)
		cxgen_destroy_mutexes(cxgenp);

	if (mask & CXGEN_CLEANUP_INTRS)
		cxgen_free_intr(cxgenp);

	if (mask & CXGEN_CLEANUP_REGMAPS)
		cxgen_unmap_regs(cxgenp);

	if (mask & CXGEN_CLEANUP_FMA)
		cxgen_fm_fini(cxgenp);

	if (mask & CXGEN_CLEANUP_SOFTSTATE)
		ddi_soft_state_free(cxgen_list, cxgenp->instance);
}

/*
 * Identifies the exact model of the card we're working with.
 *
 * PCI config space must be mapped before this can be called.
 */
static const struct adapter_info *
cxgen_identify_card(p_adapter_t cxgenp)
{
	const struct adapter_info *rc = NULL;
	uint16_t vendor, device;
	struct cxgen_ident {
		uint16_t	device; /* pci device-id */
		int		index;	/* index into t3_adap_info */
	} *ci, cxgen_identifiers[] = {
		{0x20, 0},  /* PE9000 */
		{0x21, 1},  /* T302E */
		{0x22, 2},  /* T310E */
		{0x23, 3},  /* T320X */
		{0x24, 1},  /* T302X */
		{0x25, 3},  /* T320E */
		{0x26, 2},  /* T310X */
		{0x30, 2},  /* T3B10 */
		{0x31, 3},  /* T3B20 */
		{0x32, 1},  /* T3B02 */
		{0x33, 4},  /* T3B04 */
		{0x35, 6},  /* N310E */
		{0x36, 3},  /* LP-CR */
		{0x37, 7},  /* Gen 2 */
		{ 0, -1}
	};

	t3_os_pci_read_config_2(cxgenp, PCI_CONF_VENID, &vendor);
	t3_os_pci_read_config_2(cxgenp, PCI_CONF_DEVID, &device);

	/* Match device-id, ci->device will be zero if no match found */
	for (ci = cxgen_identifiers; ci->device; ci++) {
		if (ci->device == device)
			break;
	}

	if (vendor != PCI_VENDOR_ID_CHELSIO || ci->device == 0) {
		cmn_err(CE_WARN, "Unknown PCI device %x:%x", vendor, device);
		return (NULL);
	}

	ASSERT(ci->index >= 0);
	rc = t3_get_adapter_info(ci->index);
	ASSERT(rc);

	cxgenp->pci_vendor = vendor;
	cxgenp->pci_device = device;

	return (rc);
}

/*
 * Maps PCI config space and MMIO registers.
 *
 * Sets up these fields: pci_regh, regh, regp
 */
static int
cxgen_map_regs(p_adapter_t cxgenp)
{
	int status;

	status = pci_config_setup(cxgenp->dip, &cxgenp->pci_regh);
	if (status != DDI_SUCCESS) {
		cmn_err(CE_WARN, "Unable to map PCI registers: %d.", status);
		cxgen_fm_ereport_post(cxgenp, DDI_FM_DEVICE_INVAL_STATE);
		goto cxgen_map_regs_exit;
	}

	if (cxgen_fm_check_acc_handle(cxgenp->pci_regh) != DDI_FM_OK) {
		ddi_fm_service_impact(cxgenp->dip, DDI_SERVICE_LOST);
		status = DDI_FAILURE;
		goto cxgen_map_regs_exit;
	}

	status = ddi_regs_map_setup(cxgenp->dip, 1, &cxgenp->regp,
	    0, 0, &cxgen_acc_attr, &cxgenp->regh);
	if (status != DDI_SUCCESS) {
		pci_config_teardown(&cxgenp->pci_regh);
		cmn_err(CE_WARN, "Unable to map registers: %d", status);
		cxgen_fm_ereport_post(cxgenp, DDI_FM_DEVICE_INVAL_STATE);
		goto cxgen_map_regs_exit;
	}

	if (cxgen_fm_check_acc_handle(cxgenp->regh) != DDI_FM_OK) {
		ddi_fm_service_impact(cxgenp->dip, DDI_SERVICE_LOST);
		status = DDI_FAILURE;
	}

cxgen_map_regs_exit:
	return (status);
}

/* Undoes the work done by cxgen_map_regs */
static void
cxgen_unmap_regs(p_adapter_t cxgenp)
{
	ddi_regs_map_free(&cxgenp->regh);
	pci_config_teardown(&cxgenp->pci_regh);
}

static int
cxgen_setup_mutexes(p_adapter_t cxgenp)
{
	int i;

	/*
	 * TODO: identify which ones will not be grabbed in intr and use NULL
	 * priority for them.
	 */

	mutex_init(&cxgenp->lock, NULL, MUTEX_DRIVER,
	    DDI_INTR_PRI(cxgenp->intr_lo_priority));

	mutex_init(&cxgenp->sge.reg_lock, NULL, MUTEX_DRIVER,
	    DDI_INTR_PRI(cxgenp->intr_lo_priority));

	mutex_init(&cxgenp->mdio_lock, NULL, MUTEX_DRIVER,
	    DDI_INTR_PRI(cxgenp->intr_lo_priority));

	mutex_init(&cxgenp->elmr_lock, NULL, MUTEX_DRIVER,
	    DDI_INTR_PRI(cxgenp->intr_lo_priority));

	ASSERT(cxgenp->params.nports);
	for (i = 0; i < cxgenp->params.nports; i++) {
		mutex_init(&cxgenp->port[i].lock, NULL, MUTEX_DRIVER,
		    DDI_INTR_PRI(cxgenp->intr_lo_priority));
		rw_init(&cxgenp->port[i].rxmode_lock, NULL, RW_DRIVER,
		    DDI_INTR_PRI(cxgenp->intr_lo_priority));
	}

cxgen_setup_mutexes_exit:
	return (DDI_SUCCESS);
}

static void
cxgen_destroy_mutexes(p_adapter_t cxgenp)
{
	int i;

	mutex_destroy(&cxgenp->mdio_lock);
	mutex_destroy(&cxgenp->elmr_lock);
	mutex_destroy(&cxgenp->lock);

	for (i = 0; i < cxgenp->params.nports; i++) {
		mutex_destroy(&cxgenp->port[i].lock);
		rw_destroy(&cxgenp->port[i].rxmode_lock);
	}
}

#define	t3rev2char(x)	((x)->params.rev == T3_REV_A ? 'a' : \
			((x)->params.rev == T3_REV_B ? 'b' : \
			((x)->params.rev == T3_REV_B2 ? 'b' : \
			((x)->params.rev == T3_REV_C ? 'c' : \
			'z'))))
static int
cxgen_autoupdate_fw(p_adapter_t cxgenp)
{
	if (t3_check_fw_version(cxgenp) == -EINVAL) {

		cmn_err(CE_NOTE, "Firmware will be updated to %d.%d.%d",
		    FW_VERSION_MAJOR, FW_VERSION_MINOR, FW_VERSION_MICRO);

		if (t3_load_fw(cxgenp, t3_fw_image, t3_fw_image_sz))
			return (DDI_FAILURE);
	}

	if (t3_check_tpsram_version(cxgenp) == -EINVAL) {

		cmn_err(CE_NOTE, "SRAM needs to be updated to version "
		    "%c-%d.%d.%d\n", t3rev2char(cxgenp), TP_VERSION_MAJOR,
		    TP_VERSION_MINOR, TP_VERSION_MICRO);

		/* TODO: Fill this up.  Need to generate 2 more images */

	}

	return (DDI_SUCCESS);
}

/*
 * Add the child device node for the port_info at index idx in cxgenp.  If index
 * is -1, it means create all children.
 */
static int
cxgen_add_node(p_adapter_t cxgenp, int node)
{
	struct port_info *pi;
	char ua[16];
	int rc = DDI_SUCCESS, i;

	for (i = 0; i < cxgenp->params.nports; i++) {
		if (node != -1 && i != node)
			continue;

		pi = &cxgenp->port[i];
		mutex_enter(&pi->lock);
		if (pi->dip == NULL) {
			(void) sprintf(ua, "%u", i);
			ndi_devi_alloc_sleep(cxgenp->dip, CXGE_DEVNAME,
			    DEVI_SID_NODEID, &pi->dip);
			ASSERT(pi->dip); /* alloc_sleep should never fail */

			/* This must be added successfully */
			rc = ndi_prop_update_string(DDI_DEV_T_NONE, pi->dip,
			    "unit-address", ua);

			/* We don't really care about these two */
			(void) ndi_prop_update_int(DDI_DEV_T_NONE, pi->dip,
			    "sge-engines", pi->nqsets);
			(void) ndi_prop_update_byte_array(DDI_DEV_T_NONE,
			    pi->dip, "local-mac-address",
			    pi->hw_addr, ETHERADDRL);

			/* Promote to DS_BOUND */
			(void) ndi_devi_bind_driver(pi->dip, 0);
		}
		mutex_exit(&pi->lock);

		if (rc != DDI_SUCCESS)
			break;
	}

	return (rc);
}

/*
 * Remove the specified child nodes.  If nodes can't be removed, they're
 * probably still at DS_INITIALIZED or higher, and can be reused by add_node.
 * Don't demote them, don't remove them forcibly.
 */
static void
cxgen_remove_node(p_adapter_t cxgenp, int node)
{
	struct port_info *pi;
	int i;

	for (i = 0; i < cxgenp->params.nports; i++) {
		if (node != -1 && i != node)
			continue;

		pi = &cxgenp->port[i];
		mutex_enter(&pi->lock);
		if (pi->dip && ndi_devi_free(pi->dip) == DDI_SUCCESS) {
			/* device node has been freed successfully */
			pi->dip = NULL;

			/* Too late to check, but what the heck */
			ASSERT(pi->mcaddr_list == NULL);
			ASSERT((cxgenp->open_device_map & (1 << i)) == 0);
		}
		mutex_exit(&pi->lock);
	}
}

struct port_info *
cxgen_get_portinfo(dev_info_t *pdip, dev_info_t *dip)
{
	adapter_t *cxgenp;
	int i;

	cxgenp = ddi_get_soft_state(cxgen_list, ddi_get_instance(pdip));

	for (i = 0; i < cxgenp->params.nports; i++) {
		if (cxgenp->port[i].dip == dip)
			return (&cxgenp->port[i]);
	}

	return (NULL);
}

static void
cxgen_start_tick(p_adapter_t cxgenp)
{
	ASSERT(mutex_owned(&cxgenp->lock));
	cxgenp->timer = timeout(cxgen_tick_handler, cxgenp, CXGEN_TICKS);
	ASSERT(cxgenp->timer);
}

/*
 * Tick handler.  Counts mostly-harmless SGE errors, once per second.
 */
static void
cxgen_tick_handler(void *arg)
{
	p_adapter_t cxgenp = arg;
	unsigned int err;

	err = t3_read_reg(cxgenp, A_SG_INT_CAUSE) & (F_RSPQSTARVE | F_FLEMPTY);
	if (err) {
		struct sge_qset *qs = cxgenp->sge.qs;
		uint32_t mask, i, v;

		if (err & F_RSPQSTARVE)
			cxgenp->sge_stats.rspq_starved++;

		if (err & F_FLEMPTY)
			cxgenp->sge_stats.fl_empty++;

		v = t3_read_reg(cxgenp, A_SG_RSPQ_FL_STATUS) & ~0xff00;

		mask = 1;
		for (i = 0; i < SGE_QSETS; i++) {
			if (v & mask)
				qs[i].rspq.stats.starved++;
			mask <<= 1;
		}

		mask <<= SGE_QSETS; /* skip RSPQXDISABLED */

		for (i = 0; i < SGE_QSETS * 2; i++) {
			if (v & mask) {
				qs[i / 2].fl[i % 2].stats.empty++;
			}
			mask <<= 1;
		}

		/* clear */
		t3_write_reg(cxgenp, A_SG_RSPQ_FL_STATUS, v);
		t3_write_reg(cxgenp, A_SG_INT_CAUSE, err);

		if (cxgen_fm_check_acc_handle(cxgenp->regh) != DDI_FM_OK)
			ddi_fm_service_impact(cxgenp->dip,
			    DDI_SERVICE_DEGRADED);
	}

	mutex_enter(&cxgenp->lock);
	if (cxgenp->flags & CXGEN_STOP_TIMER) {
		mutex_exit(&cxgenp->lock);
		return;
	}
	cxgenp->timer = timeout(cxgen_tick_handler, cxgenp, CXGEN_TICKS);
	ASSERT(cxgenp->timer);
	mutex_exit(&cxgenp->lock);
}

static void
cxgen_stop_tick(p_adapter_t cxgenp)
{
	timeout_id_t timer;

	ASSERT(mutex_owned(&cxgenp->lock));

	/* Instruct the tick handler not to reschedule itself */
	cxgenp->flags |= CXGEN_STOP_TIMER;

	/*
	 * Note the id of the timer that is either running or will run next.
	 * This is what we'll kill, but after releasing the lock.
	 */
	timer = cxgenp->timer;

	/* Release it during the untimeout() call and regrab */
	mutex_exit(&cxgenp->lock);

	/*
	 * Prevent the timeout or let it run to completion.  Either way, we are
	 * guaranteed to have stopped the cycle once untimeout returns.
	 */
	(void) untimeout(timer);

	mutex_enter(&cxgenp->lock);
	cxgenp->flags &= ~CXGEN_STOP_TIMER;

	/* Must not have changed. */
	ASSERT(cxgenp->timer == timer);
}

/*
 * Implementation of OS specific funtions (required by common code).
 */

int
t3_os_find_pci_capability(p_adapter_t cxgenp, int cap)
{
	uint16_t stat;
	uint8_t cap_ptr, cap_id;

	t3_os_pci_read_config_2(cxgenp, PCI_CONF_STAT, &stat);
	if ((stat & PCI_STAT_CAP) == 0)
		return (0); /* does not implement capabilities */

	t3_os_pci_read_config_1(cxgenp, PCI_CONF_CAP_PTR, &cap_ptr);
	while (cap_ptr) {
		t3_os_pci_read_config_1(cxgenp, cap_ptr + PCI_CAP_ID, &cap_id);
		if (cap_id == cap)
			return (cap_ptr); /* found */
		t3_os_pci_read_config_1(cxgenp, cap_ptr + PCI_CAP_NEXT_PTR,
		    &cap_ptr);
	}

	return (0); /* not found */
}

void
t3_os_link_changed(p_adapter_t adapter, int port_id, int link_status,
    int speed, int duplex, int fc, int mac_was_reset)
{
	struct port_info *pi = &adapter->port[port_id];

	/*
	 * Inform the port of the change.  A callback MUST be registered by the
	 * time t3_link_changed is called (and calls this routine).  Or else
	 * prepare for a NULL ptr dereference here.
	 */
	(pi->link_change)(pi, link_status, speed, duplex, fc, mac_was_reset);
}

static void
t3_port_link_fault(void *arg)
{
	struct port_info *pi = arg;
	pi->link_fault = LF_MAYBE;
}

void
t3_os_link_fault_handler(adapter_t *adapter, int port_id)
{
	struct port_info *pi = &adapter->port[port_id];

	(void) ddi_taskq_dispatch(adapter->tq, t3_port_link_fault, pi,
	    DDI_NOSLEEP);
}

/*
 *	t3_os_phymod_changed - handle PHY module changes
 *	@phy: the PHY reporting the module change
 *	@mod_type: new module type
 *
 *	This is the OS-dependent handler for PHY module changes.  It is
 *	invoked when a PHY module is removed or inserted for any OS-specific
 *	processing.
 */
void
t3_os_phymod_changed(struct adapter *adap, int port_id)
{
	struct port_info *pi = &adap->port[port_id];
	static const char *mod_str[] = {
		NULL, "SR", "LR", "LRM", "TWINAX", "TWINAX", "unknown"
	};

	if (pi->phy.modtype == phy_modtype_none)
		cmn_err(CE_NOTE, "%s%d: PHY module unplugged.", CXGE_DEVNAME,
		    ddi_get_instance(pi->dip));
	else
		cmn_err(CE_NOTE, "%s%d: %s PHY module inserted.", CXGE_DEVNAME,
		    ddi_get_instance(pi->dip), mod_str[pi->phy.modtype]);
}

#define	SGE_PARERR (F_CPPARITYERROR | F_OCPARITYERROR | F_RCPARITYERROR | \
		    F_IRPARITYERROR | V_ITPARITYERROR(M_ITPARITYERROR) | \
		    V_FLPARITYERROR(M_FLPARITYERROR) | F_LODRBPARITYERROR | \
		    F_HIDRBPARITYERROR | F_LORCQPARITYERROR | \
		    F_HIRCQPARITYERROR)
#define	SGE_FRAMINGERR (F_UC_REQ_FRAMINGERROR | F_R_REQ_FRAMINGERROR)
#define	SGE_FATALERR (SGE_PARERR | SGE_FRAMINGERR | F_RSPQCREDITOVERFOW | \
	F_RSPQDISABLED)

/*
 *	t3_sge_err_intr_handler - SGE async event interrupt handler
 *	@adapter: the adapter
 *
 *	Interrupt handler for SGE asynchronous (non-data) events.
 */
void
t3_sge_err_intr_handler(p_adapter_t adapter)
{
	unsigned int status;

	status = t3_read_reg(adapter, A_SG_INT_CAUSE);

	if (status & SGE_PARERR)
		adapter->sge_stats.parity_errors++;

	if (status & SGE_FRAMINGERR)
		adapter->sge_stats.framing_errors++;

	if (status & F_RSPQCREDITOVERFOW)
		adapter->sge_stats.rspq_credit_overflows++;

	if (status & F_RSPQDISABLED) {
		struct sge_qset *qs = adapter->sge.qs;
		uint32_t mask, i, v;

		adapter->sge_stats.rspq_disabled++;

		v = t3_read_reg(adapter, A_SG_RSPQ_FL_STATUS) & 0xff00;

		mask = 1 << SGE_QSETS; /* skip RSPQXSTARVED */
		for (i = 0; i < SGE_QSETS; i++) {
			if (v & mask)
				qs[i].rspq.stats.disabled++;
			mask <<= 1;
		}

		t3_write_reg(adapter, A_SG_RSPQ_FL_STATUS, v);

		if (cxgen_fm_check_acc_handle(adapter->regh) != DDI_FM_OK)
			cxgen_fm_err_report(adapter, DDI_FM_DEVICE_STALL,
			    DDI_SERVICE_DEGRADED);
	}

	if (status & F_LOPRIORITYDBEMPTY)
		adapter->sge_stats.lo_db_empty++;

	if (status & F_LOPRIORITYDBFULL)
		adapter->sge_stats.lo_db_full++;

	if (status & F_HIPRIORITYDBEMPTY)
		adapter->sge_stats.hi_db_empty++;

	if (status & F_HIPRIORITYDBFULL)
		adapter->sge_stats.hi_db_full++;

	if (status & F_LOCRDTUNDFLOWERR)
		adapter->sge_stats.lo_credit_underflows++;

	if (status & F_HICRDTUNDFLOWERR)
		adapter->sge_stats.hi_credit_underflows++;

	t3_write_reg(adapter, A_SG_INT_CAUSE, status);

	if (status & SGE_FATALERR)
		t3_fatal_err(adapter);
}

static void
phy_intr(void *arg)
{
	p_adapter_t cxgenp = arg;

	t3_phy_intr_handler(cxgenp);

	mutex_enter(&cxgenp->lock);
	if (cxgenp->slow_intr_mask) {
		cxgenp->slow_intr_mask |= F_T3DBG;
		t3_write_reg(cxgenp, A_PL_INT_CAUSE0, F_T3DBG);
		t3_write_reg(cxgenp, A_PL_INT_ENABLE0, cxgenp->slow_intr_mask);

		if (cxgen_fm_check_acc_handle(cxgenp->regh) != DDI_FM_OK)
			cxgen_fm_err_report(cxgenp, DDI_FM_DEVICE_INVAL_STATE,
			    DDI_SERVICE_DEGRADED);
	}
	mutex_exit(&cxgenp->lock);
}

void
t3_os_ext_intr_handler(p_adapter_t cxgenp)
{
	mutex_enter(&cxgenp->lock);
	if (cxgenp->slow_intr_mask) {
		cxgenp->slow_intr_mask &= ~F_T3DBG;
		t3_write_reg(cxgenp, A_PL_INT_ENABLE0, cxgenp->slow_intr_mask);
		(void) ddi_taskq_dispatch(cxgenp->tq, phy_intr, cxgenp,
		    DDI_NOSLEEP);

		if (cxgen_fm_check_acc_handle(cxgenp->regh) != DDI_FM_OK)
			cxgen_fm_err_report(cxgenp, DDI_FM_DEVICE_INVAL_STATE,
			    DDI_SERVICE_DEGRADED);
	}
	mutex_exit(&cxgenp->lock);
}

void
t3_fatal_err(p_adapter_t cxgenp)
{
	uint_t fw_status[4];

	if (cxgenp->flags & FULL_INIT_DONE) {
		t3_sge_stop(cxgenp);
		t3_write_reg(cxgenp, A_XGM_TX_CTRL, 0);
		t3_write_reg(cxgenp, A_XGM_RX_CTRL, 0);
		t3_write_reg(cxgenp, XGM_REG(A_XGM_TX_CTRL, 1), 0);
		t3_write_reg(cxgenp, XGM_REG(A_XGM_RX_CTRL, 1), 0);
		t3_intr_disable(cxgenp);
	}
	cmn_err(CE_WARN, "encountered fatal error, operation suspended\n");
	if (!t3_cim_ctl_blk_read(cxgenp, 0xa0, 4, fw_status))
		cmn_err(CE_WARN, "FW_ status: 0x%x, 0x%x, 0x%x, 0x%x\n",
		    fw_status[0], fw_status[1], fw_status[2], fw_status[3]);

	cxgen_fm_ereport_post(cxgenp, DDI_FM_DEVICE_INVAL_STATE);
}

unsigned long
simple_strtoul(unsigned char *str, char **endptr, int base)
{
	unsigned long rc;

	return (ddi_strtoul((const char *)str, endptr, base, &rc) ? 0 : rc);
}

/*
 * Allocates and configures resources for all qsets for all ports.
 */
static int
setup_sge_qsets(p_adapter_t cxgenp)
{
	int i, j, rc, qsidx = 0, vec;
	struct port_info *pi;
	struct qset_params *qp;

	for (i = 0; i < cxgenp->params.nports; i++) {
		pi = &cxgenp->port[i];

		ASSERT(pi->nqsets);

		for (j = 0; j < pi->nqsets; j++, qsidx++) {

			if (cxgenp->flags & USING_MSIX)
				vec = qsidx + 1;
			else if (cxgenp->flags & USING_MSI)
				vec = qsidx & 1; /* odd/even */
			else
				vec = -1;

			qp = &cxgenp->params.sge.qset[qsidx];

			rc = t3_sge_alloc_qset(cxgenp, qsidx, vec, qp, pi);
			if (rc) {
				/* error message already logged */
				while (--qsidx >= 0) {
					cxgen_destroy_qset_kstats(cxgenp,
					    qsidx);
					t3_sge_free_qset(cxgenp, qsidx);
				}

				goto setup_sge_qsets_exit;
			}

			/* Don't care if this works or not */
			cxgen_setup_qset_kstats(cxgenp, qsidx);

		}
	}

	if (cxgen_fm_check_acc_handle(cxgenp->regh) != DDI_FM_OK) {
		cxgen_fm_err_report(cxgenp, DDI_FM_DEVICE_INVAL_STATE,
		    DDI_SERVICE_DEGRADED);
		rc = DDI_FAILURE;
	}

setup_sge_qsets_exit:
	return (rc);
}

/* Undoes what setup_sge_qsets did. */
static int
release_sge_qsets(p_adapter_t cxgenp)
{
	int i = 0, j, qsidx = 0;
	struct port_info *pi = &cxgenp->port[i];

	for (i = 0; i < cxgenp->params.nports; i++) {
		pi = &cxgenp->port[i];

		ASSERT(pi->nqsets);

		for (j = 0; j < pi->nqsets; j++, qsidx++) {
			cxgen_destroy_qset_kstats(cxgenp, qsidx);
			t3_sge_free_qset(cxgenp, qsidx);
		}
	}

	return (0);
}

/*
 * Starts one rx thread per qset.  A cv is also required for rx_thread operation
 * and that too is init'ed here.
 */
extern pri_t maxclsyspri;
extern proc_t p0;
static void
cxgen_create_rx_threads(p_adapter_t cxgenp)
{
	int i, j, qsidx = 0;
	struct port_info *pi;
	struct sge_qset *qs;
	struct sge_rspq *q;

	/* No rx threads for MSI-X */
	ASSERT(!(cxgenp->flags & USING_MSIX));

	for (i = 0; i < cxgenp->params.nports; i++) {
		pi = &cxgenp->port[i];

		ASSERT(pi->nqsets);

		for (j = 0; j < pi->nqsets; j++, qsidx++) {
			qs = &cxgenp->sge.qs[qsidx];
			q = &qs->rspq;

			q->state = THRD_IDLE;
			cv_init(&q->cv, NULL, CV_DRIVER, NULL);
			q->thread = thread_create(NULL, 0, cxgen_rx_thread, qs,
			    0, &p0, TS_RUN, maxclsyspri);

			/*
			 * We used to bind this thread to a CPU.
			 */
		}
	}
}

/*
 * Undoes what cxgen_create_rx_threads did.
 */
static void
cxgen_destroy_rx_threads(p_adapter_t cxgenp)
{
	int i, j, qsidx = 0;
	struct port_info *pi;
	struct sge_qset *qs;
	struct sge_rspq *q;

	/* No rx threads for MSI-X */
	ASSERT(!(cxgenp->flags & USING_MSIX));

	for (i = 0; i < cxgenp->params.nports; i++) {
		pi = &cxgenp->port[i];

		ASSERT(pi->nqsets);

		for (j = 0; j < pi->nqsets; j++, qsidx++) {
			qs = &cxgenp->sge.qs[qsidx];
			q = &qs->rspq;

			/* Terminate the thread */
			mutex_enter(&q->lock);
			q->state = THRD_EXITING;
			cv_signal(&q->cv);
			mutex_exit(&q->lock);

			/* Wait till the thread exits. */
			thread_join(q->thread->t_did);
			ASSERT(q->state == THRD_EXITED);

			cv_destroy(&q->cv);
		}
	}
}

/*
 * Thread used to service the rx interrupts for INTx and MSI.  There is one such
 * thread per qset.
 */
static void
cxgen_rx_thread(struct sge_qset *qs)
{
	struct sge_rspq *q = &qs->rspq;
	struct port_info *pi = qs->port;
	adapter_t *cxgenp = pi->adapter;
	mblk_t *m;

	mutex_enter(&q->lock);
	while (q->state != THRD_EXITING) {

		/*
		 * We let go of the lock while making the upcall into gldv3 mac.
		 * So the check for BUSY has to be in a loop (in case state was
		 * set to BUSY by the intr handler when we didn't have the lock.
		 */
		while (q->state == THRD_BUSY || q->more) {

			if (q->polling) {
				q->state = THRD_IDLE;
				break;
			}

			m = sge_rx_data(qs);
			q->state = THRD_IDLE;

			/*
			 * Ring the doorbell only if we were done processing
			 * the response queue.
			 */
			if (!q->more) {
				t3_write_reg(cxgenp, A_SG_GTS,
				    V_RSPQ(q->cntxt_id) |
				    V_NEWTIMER(q->next_holdoff) |
				    V_NEWINDEX(q->index));
			}

			if (m) {
				mutex_exit(&q->lock);

				/* Pass up with no locks held */
				(pi->rx)(qs, m);

				mutex_enter(&q->lock);
			}
		}

		cv_wait(&q->cv, &q->lock);
	}
	q->state = THRD_EXITED;
	mutex_exit(&q->lock);

	thread_exit();

	if (cxgen_fm_check_acc_handle(cxgenp->regh) != DDI_FM_OK)
		cxgen_fm_err_report(cxgenp, DDI_FM_DEVICE_INVAL_STATE,
		    DDI_SERVICE_DEGRADED);
}

/*
 *	setup_rss - configure Receive Side Steering (per-queue connection demux)
 *	@adap: the adapter
 *
 *	Sets up RSS to distribute packets to multiple receive queues.  We
 *	configure the RSS CPU lookup table to distribute to the number of HW
 *	receive queues, and the response queue lookup table to narrow that
 *	down to the response queues actually configured for each port.
 *	We always configure the RSS mapping for two ports since the mapping
 *	table has plenty of entries.
 */
static void
setup_rss(adapter_t *adap)
{
	int i;
	uint_t nq[2];
	uint8_t cpus[SGE_QSETS + 1];
	uint16_t rspq_map[RSS_TABLE_SIZE];

	for (i = 0; i < SGE_QSETS; ++i)
		cpus[i] = (uint8_t)i;
	cpus[SGE_QSETS] = 0xff;

	nq[0] = nq[1] = 0;
	for_each_port(adap, i) {
		const struct port_info *pi = adap2pinfo(adap, i);

		nq[pi->tx_chan] += pi->nqsets;
	}
	for (i = 0; i < RSS_TABLE_SIZE / 2; ++i) {
		rspq_map[i] = nq[0] ? i % nq[0] : 0;
		rspq_map[i + RSS_TABLE_SIZE / 2] = nq[1] ? i % nq[1] +
		    nq[0] : 0;
	}
	/* Calculate the reverse RSS map table */
	for (i = 0; i < RSS_TABLE_SIZE; ++i)
		if (adap->rrss_map[rspq_map[i]] == 0xff)
			adap->rrss_map[rspq_map[i]] = (uint8_t)i;

	t3_config_rss(adap, F_RQFEEDBACKENABLE | F_TNLLKPEN | F_TNLMAPEN |
	    F_TNLPRTEN | F_TNL2TUPEN | F_TNL4TUPEN | F_OFDMAPEN |
	    F_RRCPLMAPEN | V_RRCPLCPUSIZE(6) | F_HASHTOEPLITZ,
	    cpus, rspq_map);

	if (cxgen_fm_check_acc_handle(adap->regh) != DDI_FM_OK)
		cxgen_fm_err_report(adap, DDI_FM_DEVICE_INVAL_STATE,
		    DDI_SERVICE_DEGRADED);
}

/*
 * This is cxgb_up with a new name (I wasn't sure whether to call it cxgen_up or
 * cxge_up so it's called neither).
 */
int
first_port_up(p_adapter_t cxgenp)
{
	int rc = 0;

	ASSERT(mutex_owned(&cxgenp->lock));

	if ((cxgenp->flags & FULL_INIT_DONE) == 0) {

		rc = -t3_init_hw(cxgenp, 0);
		if (rc) {
			cmn_err(CE_WARN, "%s: unable to initialize hardware "
			    "(%d)", __func__, rc);
			goto port_up_exit;
		}

		t3_set_reg_field(cxgenp, A_TP_PARA_REG5, 0, F_RXDDPOFFINIT);
		t3_write_reg(cxgenp, A_ULPRX_TDDP_PSZ, V_HPZ0(PAGESHIFT - 12));

		rc = setup_sge_qsets(cxgenp);
		if (rc)
			goto port_up_exit; /* err already logged */

		if ((cxgenp->flags & USING_MSIX) == 0)
			cxgen_create_rx_threads(cxgenp);

		setup_rss(cxgenp);

		/* hw now initialized, setup any remaining config kstats */
		cxgen_finalize_config_kstats(cxgenp);

		cxgenp->flags |= FULL_INIT_DONE;
	}

	cxgen_start_tick(cxgenp);

	t3_intr_clear(cxgenp);
	t3_sge_start(cxgenp);
	t3_intr_enable(cxgenp);

	if (!(cxgenp->flags & QUEUES_BOUND)) {
		bind_qsets(cxgenp);
		cxgenp->flags |= QUEUES_BOUND;
	}

port_up_exit:
	if (rc && (cxgen_fm_check_acc_handle(cxgenp->regh) != DDI_FM_OK))
		cxgen_fm_err_report(cxgenp, DDI_FM_DEVICE_INVAL_STATE,
		    DDI_SERVICE_LOST);

	return (rc);
}

/*
 * Undoes what first_port_up does.  Called when the last port goes down.  Note
 * that this does NOT let go of the resources grabbed in order to reach
 * FULL_INIT_DONE.  This means we do not release the resources allocated by
 * setup_sge_qsets here.  Those are released during cxgen detach.  Why? I think
 * it's because contexts etc. can not be reprogrammed without a full reset,
 * which only happens at cxgen_attach -> t3_prep_adapter -> t3_reset_adapter.
 * So we hold on to the qset settings in case a port comes back up.  In that
 * case first_port_up will skip the FULL_INIT_DONE part.
 */
int
last_port_down(p_adapter_t cxgenp)
{
	ASSERT(mutex_owned(&cxgenp->lock));

	t3_sge_stop(cxgenp);
	t3_intr_disable(cxgenp);

	cxgen_stop_tick(cxgenp);

	if (cxgen_fm_check_acc_handle(cxgenp->regh) != DDI_FM_OK)
		cxgen_fm_err_report(cxgenp, DDI_FM_DEVICE_INVAL_STATE,
		    DDI_SERVICE_LOST);

	return (0);
}

/*
 * Fault Management Architecture (FMA) Routines
 */

/*
 * cxgenp_fm_init
 * Initialize FM Capabilities
 */
void
cxgen_fm_init(p_adapter_t cxgenp)
{
	ddi_iblock_cookie_t ibc;

	cxgen_acc_attr.devacc_attr_access = DDI_DEFAULT_ACC;

	if (!cxgenp->fma_cap)
		return;

	if (DDI_FM_ACC_ERR_CAP(cxgenp->fma_cap))
		cxgen_acc_attr.devacc_attr_access = DDI_FLAGERR_ACC;

	/* Register FMA Capabilities with IO Fault Services */
	ddi_fm_init(cxgenp->dip, &cxgenp->fma_cap, &ibc);

	/* Initialize PCI eReport Capabilities */
	if (DDI_FM_EREPORT_CAP(cxgenp->fma_cap) ||
	    DDI_FM_ERRCB_CAP(cxgenp->fma_cap)) {
		pci_ereport_setup(cxgenp->dip);
	}

	/* Register Error Callback */
	if (DDI_FM_ERRCB_CAP(cxgenp->fma_cap)) {
		ddi_fm_handler_register(cxgenp->dip, cxgen_fm_errcb,
		    (void *) cxgenp);
	}
}

/*
 * cxgen_fm_fini
 * Unregister/Terminate FMA Capabilities
 */
void
cxgen_fm_fini(p_adapter_t cxgenp)
{
	/* Nothing to do here if FMA Capability is disabled */
	if (!cxgenp->fma_cap)
		return;

	/* Teardown PCI eReport Capability */
	if (DDI_FM_EREPORT_CAP(cxgenp->fma_cap) ||
	    DDI_FM_ERRCB_CAP(cxgenp->fma_cap)) {
		pci_ereport_teardown(cxgenp->dip);
	}

	/* Unregister Error Callback */
	if (DDI_FM_ERRCB_CAP(cxgenp->fma_cap))
		ddi_fm_handler_unregister(cxgenp->dip);

	/* Unregister from IO Fault Services */
	ddi_fm_fini(cxgenp->dip);
}

/*
 * cxgen_fm_check_acc_handle
 */
int
cxgen_fm_check_acc_handle(ddi_acc_handle_t handle)
{
	ddi_fm_error_t fme;

	ddi_fm_acc_err_get(handle, &fme, DDI_FME_VERSION);

	ddi_fm_acc_err_clear(handle, DDI_FME_VERSION);

	return (fme.fme_status);
}

/*
 * cxgen_fm_check_dma_handle
 */
int
cxgen_fm_check_dma_handle(ddi_dma_handle_t handle)
{
	ddi_fm_error_t fme;

	ddi_fm_dma_err_get(handle, &fme, DDI_FME_VERSION);

	return (fme.fme_status);
}

/*
 * cxgen_fm_ereport_post
 */
void
cxgen_fm_ereport_post(p_adapter_t cxgenp, char *details)
{
	uint64_t ena;		/* ENA: Error Numeric Association */
	char buf[FM_MAX_CLASS];	/* FM_MAX_CLASS: 100 */

	(void) snprintf(buf, FM_MAX_CLASS, "%s.%s", DDI_FM_DEVICE, details);

	/* Timestamp: 0, FM_ENA_FMT1: ENA Format Type 1 */
	ena = fm_ena_generate(0, FM_ENA_FMT1);

	if (DDI_FM_EREPORT_CAP(cxgenp->fma_cap)) {
		ddi_fm_ereport_post(cxgenp->dip, buf, ena, DDI_NOSLEEP,
		    FM_VERSION, DATA_TYPE_UINT8, FM_EREPORT_VERS0, NULL);
	}
}

/*
 * cxgen_fm_errcb
 * FM Error Callback
 */
/* ARGSUSED */
int
cxgen_fm_errcb(dev_info_t *dip, ddi_fm_error_t *err, const void *impl_data)
{
	pci_ereport_post(dip, err, NULL);
	return (err->fme_status);
}

/*
 * cxgen_fm_err_report
 * Report error to FMA and post ereport
 */
void
cxgen_fm_err_report(p_adapter_t cxgenp, char *details, int impact)
{
	ddi_fm_service_impact(cxgenp->dip, impact);
	cxgen_fm_ereport_post(cxgenp, details);
}
