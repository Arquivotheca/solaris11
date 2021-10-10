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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * ctsmc_ddi - Driver initialization and setup
 */
#include <sys/types.h>
#include <sys/param.h>
#include <sys/cred.h>
#include <sys/log.h>
#include <sys/uio.h>
#include <sys/stat.h>
#include <sys/vnode.h>
#include <sys/file.h>
#include <sys/open.h>
#include <sys/kmem.h>

#include <sys/stream.h>
#include <sys/strsubr.h>
#include <sys/strsun.h>
#include <sys/poll.h>

#include <sys/debug.h>

#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/modctl.h>

#include <sys/inttypes.h>
#include <sys/ksynch.h>

#include <sys/ctsmc_debug.h>
#include <sys/ctsmc.h>

static int ctsmc_bus_ctl(dev_info_t *dip, dev_info_t *rdip,
		ddi_ctl_enum_t op, void *arg, void *result);
static int ctsmc_intr_ops(dev_info_t *dip, dev_info_t *rdip,
	ddi_intr_op_t intr_op, ddi_intr_handle_impl_t *hdlp, void *result);
static int ctsmc_getinfo(dev_info_t *, ddi_info_cmd_t, void *, void **);
static int ctsmc_attach(dev_info_t *, ddi_attach_cmd_t);
static int ctsmc_detach(dev_info_t *, ddi_detach_cmd_t);

/*
 * Supporting routines
 */
extern int ctsmc_i2c_initchild(dev_info_t *cdip);
extern void ctsmc_i2c_uninitchild(dev_info_t *cdip);
extern void ctsmc_i2c_reportdev(dev_info_t *dip, dev_info_t *rdip);
extern void ctsmc_wait_until_seq_timeout_exits(ctsmc_state_t *ctsmc);
extern int ctsmc_intr_load(dev_info_t *dip, ctsmc_state_t *ctsmc,
		int (*hostintr)(ctsmc_state_t *));
extern void ctsmc_intr_unload(dev_info_t *dip, void *hwptr);
extern void ctsmc_setup_intr(ctsmc_state_t *ctsmc, dev_info_t *dip);

extern void ctsmc_cmd_alloc();
extern void ctsmc_cmd_fini();
extern void ctsmc_set_command_flags(ctsmc_state_t *ctsmc);
extern void ctsmc_sequence_timeout_handler(void *arg);
extern int ctsmc_pkt_receive(ctsmc_state_t *ctsmc);
extern int ctsmc_i2c_attach(ctsmc_state_t *ctsmc, dev_info_t *dip);
extern void ctsmc_i2c_detach(ctsmc_state_t *ctsmc, dev_info_t *dip);
extern void ctsmc_init_minor_list(ctsmc_state_t *ctsmc);
extern int ctsmc_free_minor_list(ctsmc_state_t *ctsmc);
extern void ctsmc_init_seqlist(ctsmc_state_t *ctsmc);
extern void ctsmc_free_seqlist(ctsmc_state_t *ctsmc);
extern void ctsmc_init_cmdspec_list(ctsmc_state_t *ctsmc);
extern void ctsmc_free_cmdspec_list(ctsmc_state_t *ctsmc);
extern void ctsmc_initIPMSeqList(ctsmc_state_t *ctsmc);
extern void ctsmc_freeIPMSeqList(ctsmc_state_t *ctsmc);
extern int ctsmc_alloc_kstat(ctsmc_state_t *ctsmc);
extern void ctsmc_free_kstat(ctsmc_state_t *ctsmc);
extern void ctsmc_setup_ipmi_logs(ctsmc_state_t *ctsmc);
extern void ctsmc_destroy_ipmi_logs(ctsmc_state_t *ctsmc);
extern uchar_t ctsmc_nct_hs_activate(ctsmc_state_t *ctsmc);

extern struct streamtab	ctsmc_stab;

static struct bus_ops ctsmc_busops = {
	BUSO_REV,
	nullbusmap,		/* bus_map */
	NULL,			/* bus_get_intrspec */
	NULL,			/* bus_add_intrspec */
	NULL,			/* bus_remove_intrspec */
	NULL,			/* bus_map_fault */
	ddi_no_dma_map,		/* bus_dma_map */
	ddi_no_dma_allochdl,	/* bus_dma_allochdl */
	ddi_no_dma_freehdl,	/* bus_dma_freehdl */
	ddi_no_dma_bindhdl,	/* bus_dma_bindhdl */
	ddi_no_dma_unbindhdl,	/* bus_unbindhdl */
	ddi_no_dma_flush,	/* bus_dma_flush */
	ddi_no_dma_win,		/* bus_dma_win */
	ddi_no_dma_mctl,	/* bus_dma_ctl */
	ctsmc_bus_ctl,		/* bus_ctl */
	ddi_bus_prop_op,	/* bus_prop_op */
	NULL,			/* bus_get_eventcookie */
	NULL,			/* bus_add_eventcall */
	NULL,			/* bus_remove_eventcall */
	NULL,			/* bus_post_event */
	0,			/* bus_intr_ctl */
	0,			/* bus_config		*/
	0,			/* bus_unconfig		*/
	0,			/* bus_fm_init		*/
	0,			/* bus_fm_fini		*/
	0,			/* bus_fm_access_enter	*/
	0,			/* bus_fm_access_exit	*/
	0,			/* bus_power		*/
	ctsmc_intr_ops		/* bus_intr_op		*/
};

static struct cb_ops ctsmc_cb_ops = {
	nulldev,		/* open */
	nulldev,		/* close */
	nodev,			/* strategy */
	nodev,			/* print */
	nodev,			/* dump */
	nodev,			/* read */
	nodev,			/* write */
	nodev,			/* ioctl */
	nodev,			/* devmap */
	nodev,			/* mmap */
	nodev, 			/* segmap */
	nochpoll,		/* poll */
	ddi_prop_op,		/* cb_prop_op */
	&ctsmc_stab,		/* streamtab  */
	D_MP,			/* Driver compatibility flag */
	CB_REV,			/* rev */
	nodev,			/* int (*cb_aread)() */
	nodev			/* int (*cb_awrite)() */
};

static struct dev_ops ctsmc_ops = {
	DEVO_REV,		/* devo_rev, */
	0,			/* refcnt  */
	ctsmc_getinfo,		/* info */
	nulldev,		/* identify */
	nulldev,		/* probe */
	ctsmc_attach,		/* attach */
	ctsmc_detach,		/* detach */
	nodev,			/* reset */
	&ctsmc_cb_ops,		/* driver operations */
	&ctsmc_busops,		/* bus operations */
	NULL,
	ddi_quiesce_not_supported,	/* devo_quiesce */
};

/*
 * Module linkage information for the kernel.
 */

static struct modldrv modldrv = {
	&mod_driverops, /* Type of module.  This one is a driver */
	"Sys Mgmt Ctrl (SMC) driver",
	&ctsmc_ops,	/* driver ops */
};


static struct modlinkage modlinkage = {
	MODREV_1,
	(void *)&modldrv,
	NULL
};

/*
 * Driver wide exclusion lock
 */
kmutex_t	ctsmc_excl_lock;
static	void	*ctsmc_soft_head = NULL;
static	int	ctsmc_instance_num = -1;

extern enum_intr_desc_t enum_intr_desc;
static int ctsmc_mct_fullhs_intr_hack = 1;

/*
 * For MC/T, ENUM# interrupt is processed here. This
 * variable is set to TRUE when a child driver does a
 * ddi_add_intr() to add it's interrupt handler for
 * ENUM#
 */
int ctsmc_mct_process_enum = 0;

/*
 * Initializes necessary software data structures, setup register
 * mapping etc.
 */
static void
ctsmc_state_init(ctsmc_state_t *ctsmc, dev_info_t *dip, int unit)
{
	ctsmc->ctsmc_instance = unit;
	ctsmc->ctsmc_dip = dip;

	/* Initialize SMC minor number list */
	ctsmc_init_minor_list(ctsmc);

	/* Initialize SMC sequence number space */
	ctsmc_init_seqlist(ctsmc);

	/* Initialize interest list for commands/events */
	ctsmc_init_cmdspec_list(ctsmc);

	ctsmc_initBufList(ctsmc);
	ctsmc_initIPMSeqList(ctsmc);

	ctsmc->ctsmc_init = 0;
	mutex_init(&ctsmc_excl_lock, NULL, MUTEX_DRIVER, NULL);
	mutex_init(&ctsmc->lock, NULL, MUTEX_DRIVER, NULL);
	cv_init(&ctsmc->ctsmc_cv, NULL, CV_DRIVER, NULL);
	cv_init(&ctsmc->exit_cv, NULL, CV_DRIVER, NULL);
	ctsmc->ctsmc_opens = 0;
	ctsmc->ctsmc_tid = 0;
	ctsmc->ctsmc_flag = 0;
	ctsmc->ctsmc_mode = 0;
	ctsmc->ctsmc_rsppkt = NEW(1, ctsmc_rsppkt_t);
}

static void
ctsmc_state_fini(ctsmc_state_t *ctsmc)
{
	int ret;

	/*
	 * Make sure all pending timeout handlers are
	 * run to completion
	 */
	FREE(ctsmc->ctsmc_rsppkt, 1, ctsmc_rsppkt_t);

	ret = ctsmc_free_minor_list(ctsmc);

	if (ret != SMC_SUCCESS)
		return;

	ctsmc_freeIPMSeqList(ctsmc);
	ctsmc_freeBufList(ctsmc);
	ctsmc_free_seqlist(ctsmc);

	ctsmc_free_cmdspec_list(ctsmc);

	cv_destroy(&ctsmc->exit_cv);
	cv_destroy(&ctsmc->ctsmc_cv);
	mutex_destroy(&ctsmc->lock);
	mutex_destroy(&ctsmc_excl_lock);
}

/*
 * The purpose of this routine is to change the second entry of
 * the interrupt-map property in the ctsmc node. The ENUM# interrupt
 * corresponds to interrupt 2, however the ENUM# signal never reaches
 * the CPU directly. So, here we change the interrupt-parent of the
 * for this interrupt to the SMC node itself, making it possible to
 * intercept the ddi_add/remove_intr calls made by the child scsb
 * node. Much of the code is borrowed from sun4u/os/ddi_impl.c
 */
static int
ctsmc_update_intrmap(dev_info_t *dip)
{
	uint32_t *imap, imap_sz;
	int32_t addr_cells, intr_cells, ret = DDI_SUCCESS;

	/*
	 * Get the "interrupt-map" property of the scsb node
	 */
	if (ddi_prop_lookup_int_array(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "interrupt-map", (int **)&imap, &imap_sz) !=
	    DDI_PROP_SUCCESS) {
		/*
		 * We don't have an "interrupt-map" property, abort
		 */

		return (DDI_FAILURE);
	}

	/* Get the address cell size */
	addr_cells = ddi_prop_get_int(DDI_DEV_T_ANY, dip, 0,
	    "#address-cells", 2);

	/* Get the interrupts cell size */
	intr_cells = ddi_prop_get_int(DDI_DEV_T_ANY, dip, 0,
	    "#interrupt-cells", 1);

	/*
	 * The total size of an interrupt specifier is #address-cells
	 * + #interrupt-cells + 1 ( phandle ) + 1 (parent interrupt
	 * specifier)
	 */

	imap[2*(addr_cells+intr_cells)+2] = ddi_get_nodeid(dip);

	/*
	 * Now update the interrupt-map property of the ctsmc node.
	 */
	if ((ret = ddi_prop_update_int_array(DDI_DEV_T_NONE, dip,
	    "interrupt-map", (int *)imap, imap_sz)) !=
	    DDI_PROP_SUCCESS) {
		cmn_err(CE_WARN, "%s #%d: Could not update interrupt-map"
		    " property", ddi_driver_name(dip),
		    ddi_get_instance(dip));
	}

	ddi_prop_free(imap);

	return (ret);
}

int
_init(void)
{
	int	e;

	e = ddi_soft_state_init(&ctsmc_soft_head, sizeof (ctsmc_state_t),
	    SMC_NO_OF_BOARDS);
	if (e != 0) {
		return (e);
	}

	ctsmc_cmd_alloc();	/* Initialize command table */
	e = mod_install(&modlinkage);
	if (e != 0) {
		ctsmc_cmd_fini();
		ddi_soft_state_fini(&ctsmc_soft_head);
	}

	return (e);
}

int
_fini(void)
{
	int	e;

	SMC_DEBUG0(SMC_DEVI_DEBUG, "_fini");

	if ((e = mod_remove(&modlinkage)) == 0) {
		SMC_DEBUG(SMC_DEVI_DEBUG, "_fini: mod_remove: %d", e);
		/*
		 * destroy global data
		 */
		ctsmc_cmd_fini();
		ddi_soft_state_fini(&ctsmc_soft_head);
	}

	return (e);
}

int
_info(struct modinfo *modinfop)
{
	SMC_DEBUG0(SMC_DEVI_DEBUG, "_info");

	return (mod_info(&modlinkage, modinfop));
}

/*
 * DEVOPS for ctsmc
 */

/*
 * attach(9E) entry point for SMC driver.
 */
static int
ctsmc_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	ctsmc_state_t	*ctsmc;
	int		unit;
	int		e, ret;
	minor_t mdev;
	clock_t intval = drv_usectohz(90 * MICROSEC);

	ctsmc_instance_num = unit = ddi_get_instance(dip);

	SMC_DEBUG(SMC_DEVI_DEBUG, "attach(instance=%d)", unit);

	switch (cmd) {
	case DDI_ATTACH:
		break;

	case DDI_RESUME:
		cmn_err(CE_WARN, "%s%d: attach: suspend/resume not supported",
		    ddi_driver_name(dip), unit);
		return (DDI_FAILURE);
	}

	e = ddi_soft_state_zalloc(ctsmc_soft_head, unit);

	if (e != DDI_SUCCESS) {
		cmn_err(CE_WARN, "sc%d: soft state alloc failed", unit);
		return (DDI_FAILURE);
	}

	ctsmc = (ctsmc_state_t *)ddi_get_soft_state(ctsmc_soft_head, unit);
	if (ctsmc == NULL) {
		cmn_err(CE_WARN, "%s%d: attach: cannot get soft state",
		    ddi_driver_name(dip), unit);
		return (DDI_FAILURE);
	}

	/*
	 * map in registers, and setup HW-specific
	 * private data.
	 */
	ctsmc->ctsmc_hw = ctsmc_regs_map(dip);

	/*
	 * Clear pending messages and get iblock cookie
	 */
	if (ctsmc->ctsmc_hw) {
		ctsmc_setup_intr(ctsmc, dip);
		ctsmc->ctsmc_hw->smh_smc_num_pend_req = 0;
	} else {
		ddi_soft_state_free(ctsmc_soft_head, unit);
		return (DDI_FAILURE);
	}

	/*
	 * initialize soft structure for this instance
	 */

	ctsmc_state_init(ctsmc, dip, unit);
	ctsmc->ctsmc_init |= SMC_STATE_INIT;

	if (ddi_prop_exists(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "poll-mode") == 1) {
		ctsmc->ctsmc_mode |= SMC_POLL_MODE;
		SMC_DEBUG0(SMC_DEVI_DEBUG, "POLLING MODE");
	}

	/*
	 * Update command execution flag, e.g. whether to honor or
	 * reject unknown commands. By default, all unknown commands
	 * are honored, unless the property "reject-unknown-commands"
	 * exist
	 */
	ctsmc_set_command_flags(ctsmc);

	ctsmc->ctsmc_tid = timeout(ctsmc_sequence_timeout_handler,
	    ctsmc, intval);
	/*
	 * add interrupt handler(s). Before we do this, we really need
	 * to make sure that an interrupt is not already pending because
	 * an async message may be waiting to be received
	 */
	e = ctsmc_intr_load(dip, ctsmc, ctsmc_pkt_receive);

	if (e != DDI_SUCCESS) {
		ddi_remove_minor_node(dip, NULL);
		ctsmc_state_fini(ctsmc);
		ddi_soft_state_free(ctsmc_soft_head, unit);
		cmn_err(CE_WARN, "sc%d: add intr handler failed", unit);
		return (DDI_FAILURE);
	}

	e = ctsmc_i2c_attach(ctsmc, dip);
	if (e != DDI_SUCCESS)
		cmn_err(CE_WARN, "sc%d: i2c nexus support not exist", unit);

	if (ctsmc_mct_fullhs_intr_hack) {
		/*
		 * Update the SMC interrupt-map for the ENUM# intr
		 */
		(void) ctsmc_update_intrmap(dip);
	}

	(void) ctsmc_alloc_kstat(ctsmc);

	ctsmc_setup_ipmi_logs(ctsmc);

	/*
	 * If all went well, create the minor nodes for user level access.
	 *
	 */
	mdev = (unit << SMC_CTRL_SHIFT);
	ctsmc->ctsmc_init |= SMC_IS_ATTACHED;
	if ((ret = ddi_create_minor_node(dip, SMC_CLONE_DEV, S_IFCHR,
	    mdev, DDI_PSEUDO, CLONE_DEV)) != DDI_SUCCESS) {
		ddi_remove_minor_node(dip, NULL);
		ctsmc_state_fini(ctsmc);
		ddi_soft_state_free(ctsmc_soft_head, unit);
		return (ret);
	}

	ddi_report_dev(dip);

	return (DDI_SUCCESS);
}

/*
 * detach(9E) entry point for SMC driver.
 */
static int
ctsmc_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	int		unit;
	ctsmc_state_t	*ctsmc;

	unit = ddi_get_instance(dip);

	SMC_DEBUG(SMC_DEVI_DEBUG, "detach(%d)", unit);

	switch (cmd) {
	case DDI_DETACH:
		/*
		 * SMC specific conditions for allowing detach of an instance
		 * should be met before proceeding with detach.
		 */
		break;

	case DDI_SUSPEND:
		cmn_err(CE_WARN, "%s%d: detach: suspend/resume not supported",
		    ddi_driver_name(dip), unit);
		return (DDI_FAILURE);
	}

	ctsmc = (ctsmc_state_t *)ddi_get_soft_state(ctsmc_soft_head, unit);

	if (ctsmc == NULL) {
		cmn_err(CE_NOTE, "SMC driver already detached");
		return (DDI_SUCCESS);
	}

	if (ctsmc->ctsmc_opens != 0) {
		/* device still has open streams */
		cmn_err(CE_NOTE, "%s%d: detach: open streams - cannot detach",
		    ddi_driver_name(dip), unit);
		return (DDI_FAILURE);
	}

	ctsmc->ctsmc_init |= SMC_IS_DETACHING;
	/*
	 * Remove the minor node.
	 */
	ddi_remove_minor_node(dip, NULL);

	ctsmc_destroy_ipmi_logs(ctsmc);
	ctsmc_free_kstat(ctsmc);
	/*
	 * unload interrupt handler and terminate
	 * event handler daemon threads.
	 */

	ctsmc_intr_unload(dip, ctsmc->ctsmc_hw);

	/*
	 * Before we exit, we need to make sure all timeout handlers
	 * exit
	 */
	ctsmc_wait_until_seq_timeout_exits(ctsmc);

	/*
	 * eliminate any allocated data for I2C interface
	 */
	ctsmc_i2c_detach(ctsmc, dip);

	/*
	 * eliminate any allocated data hung onto the
	 * soft structure.
	 */
	ctsmc_state_fini(ctsmc);

	/*
	 * unmap registers and throw away HW-specific private
	 * data.
	 */
	ctsmc_regs_unmap(ctsmc->ctsmc_hw);

	/*
	 * free all SMC functional related data from state structure
	 * before freeing the state structure itself.
	 */

	ddi_soft_state_free(ctsmc_soft_head, unit);

#ifdef	DEBUG
	/* ctsmc_debug.mem_print(); */
#endif	/* DEBUG */

	return (DDI_SUCCESS);
}


/*
 * getinfo(9E) entry point for SMC driver.
 */
/* ARGSUSED */
static int
ctsmc_getinfo(dev_info_t *dip, ddi_info_cmd_t op, void *arg, void **result)
{
	int		e = DDI_FAILURE;
	int		unit;
	ctsmc_state_t	*ctsmc;

	SMC_DEBUG0(SMC_DEVI_DEBUG, "getinfo");

	unit = SMC_UNIT(getminor((dev_t)arg));

	switch (op) {
	case DDI_INFO_DEVT2DEVINFO:
		ctsmc = (ctsmc_state_t *)
		    ddi_get_soft_state(ctsmc_soft_head, unit);
		SMC_DEBUG2(SMC_DEVI_DEBUG, "getinfo: DEVINFO ctsmc%x %p",
		    unit, ctsmc);
		if (ctsmc != NULL) {
			*result = (void *) ctsmc->ctsmc_dip;
			e = DDI_SUCCESS;
		} else {
			*result = NULL;
		}
		break;

	case DDI_INFO_DEVT2INSTANCE:
		SMC_DEBUG(SMC_DEVI_DEBUG, "getinfo: INSTANCE %x", unit);

		*result = (void *)(uintptr_t)unit;
		e = DDI_SUCCESS;
		break;

	default:
		break;
	}

	return (e);
}

ctsmc_state_t *
ctsmc_get_soft_state(int unit)
{
	if (ctsmc_instance_num < 0)
		return (NULL);

	return ((ctsmc_state_t *)ddi_get_soft_state(ctsmc_soft_head,
	    unit));
}

static int
ctsmc_bus_ctl(dev_info_t *dip, dev_info_t *rdip, ddi_ctl_enum_t op,
			void *arg, void *result)
{
	SMC_DEBUG0(SMC_I2C_DEBUG, "ctsmc_bus_ctl");
	switch (op) {
	case DDI_CTLOPS_INITCHILD:
		return (ctsmc_i2c_initchild((dev_info_t *)arg));

	case DDI_CTLOPS_UNINITCHILD:
		ctsmc_i2c_uninitchild((dev_info_t *)arg);
		return (DDI_SUCCESS);

	case DDI_CTLOPS_REPORTDEV:
		ctsmc_i2c_reportdev(dip, rdip);
		return (DDI_SUCCESS);

	case DDI_CTLOPS_DMAPMAPC:
	case DDI_CTLOPS_POKE:
	case DDI_CTLOPS_PEEK:
	case DDI_CTLOPS_IOMIN:
	case DDI_CTLOPS_REPORTINT:
	case DDI_CTLOPS_SIDDEV:
	case DDI_CTLOPS_SLAVEONLY:
	case DDI_CTLOPS_AFFINITY:
	case DDI_CTLOPS_PTOB:
	case DDI_CTLOPS_BTOP:
	case DDI_CTLOPS_BTOPR:
	case DDI_CTLOPS_DVMAPAGESIZE:
		SMC_DEBUG(SMC_I2C_DEBUG, "ctsmc_bus_ctl: unsupported flag %d",
		    op);
		return (DDI_FAILURE);
	default:
		SMC_DEBUG(SMC_I2C_DEBUG, "ctsmc_bus_ctl: default Op %d", op);
		return (ddi_ctlops(dip, rdip, op, arg, result));
	}
}

/*
 * In this routine, we will intercept the ddi_add/remove_intr
 * calls by all children. The interesting stuff is the interrupt 2
 * of the scsb node (ENUM#). The job here is to call the interrupt
 * handler corresponding to this interrupt in the ctsmc driver when
 * a ENUM# event message is received in the host-ctsmc interface channel
 * All other interrupts are passed over to the default handler.
 */
static int
ctsmc_intr_ops(dev_info_t *dip, dev_info_t *rdip, ddi_intr_op_t intr_op,
    ddi_intr_handle_impl_t *hdlp, void *result)
{
	/*
	 * NOTE: These ops below will never be supported in this nexus
	 * driver, hence they always return immediately.
	 */
	switch (intr_op) {
	case DDI_INTROP_GETCAP:
		*(int *)result = DDI_INTR_FLAG_LEVEL;
		return (DDI_SUCCESS);
	case DDI_INTROP_SETCAP:
	case DDI_INTROP_SETMASK:
	case DDI_INTROP_CLRMASK:
	case DDI_INTROP_GETPENDING:
		return (DDI_ENOTSUP);
	default:
		break;
	}

	if (intr_op == DDI_INTROP_ADDISR) {
		int		instance = ddi_get_instance(dip);
		ctsmc_state_t	*ctsmc = (ctsmc_state_t *)
		    ddi_get_soft_state(ctsmc_soft_head, instance);

		SMC_DEBUG3(SMC_I2C_DEBUG, "ddi_add_intr for node %s%d, "
		    "interrupt = %d", ddi_node_name(rdip),
		    ddi_get_instance(rdip), hdlp->ih_inum);

		if ((strcmp(ddi_node_name(rdip), "sysctrl") == 0) &&
		    (hdlp->ih_inum == 1)) { /* ENUM# intr */

			enum_intr_desc.enum_handler =
			    (intr_handler_t)hdlp->ih_cb_func;
			enum_intr_desc.intr_arg = hdlp->ih_cb_arg1;
			ctsmc_mct_process_enum = 1;

			/* Enable ENUM# */
			(void) ctsmc_nct_hs_activate(ctsmc);
		}
	}

	return (i_ddi_intr_ops(dip, rdip, intr_op, hdlp, result));
}
