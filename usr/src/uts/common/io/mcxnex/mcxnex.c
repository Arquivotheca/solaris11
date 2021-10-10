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
 * mcxnex.c
 *    ConnectX-2 HCA nexus attach/detach Routines
 *
 *    Implements all the routines necessary for the attach, setup,
 *    initialization (and subsequent possible teardown and detach) of the
 *    ConnectX-2 HCA nexus driver.
 */

#include <sys/types.h>
#include <sys/file.h>
#include <sys/open.h>
#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/modctl.h>
#include <sys/stat.h>
#include <sys/pci.h>
#include <sys/pci_cap.h>
#include <sys/bitmap.h>
#include <sys/policy.h>
#include <sys/sdt.h>
#include <sys/sunndi.h>

#include "mcxnex.h"

/* The following works around a problem in pre-2_7_000 firmware. */
#define	MCXNEXUS_FW_WORKAROUND

int mcxnex_verbose = 0;

/* Nexus State Pointer */
void *mcxnex_statep;

int debug_vpd = 0;

/* Disable the internal error-check polling thread */
int mcxnex_no_inter_err_chk = 0;

/*
 * The Mcxnex "userland resource database" is common to instances of the
 * Mcxnex HCA driver.  This structure "mcxnex_userland_rsrc_db" contains all
 * the necessary information to maintain it.
 */
mcxnex_umap_db_t mcxnex_userland_rsrc_db;

static int mcxnex_attach(dev_info_t *, ddi_attach_cmd_t);
static int mcxnex_detach(dev_info_t *, ddi_detach_cmd_t);
static int mcxnex_open(dev_t *, int, int, cred_t *);
static int mcxnex_close(dev_t, int, int, cred_t *);
static int mcxnex_getinfo(dev_info_t *, ddi_info_cmd_t, void *, void **);

static int mcxnex_drv_init(mcxnex_state_t *, dev_info_t *, int);
static void mcxnex_drv_fini(mcxnex_state_t *);
static void mcxnex_drv_fini2(mcxnex_state_t *);
static int mcxnex_isr_init(mcxnex_state_t *);
static void mcxnex_isr_fini(mcxnex_state_t *);

static int mcxnex_hw_init(mcxnex_state_t *);

static void mcxnex_hw_fini(mcxnex_state_t *, mcxnex_drv_cleanup_level_t);
static int mcxnex_soft_state_init(mcxnex_state_t *);
static void mcxnex_soft_state_fini(mcxnex_state_t *);
static int mcxnex_icm_config_setup(mcxnex_state_t *,
    mcxnex_hw_initqueryhca_t *);
static void mcxnex_icm_tables_init(mcxnex_state_t *);
static void mcxnex_icm_tables_fini(mcxnex_state_t *);
static int mcxnex_icm_dma_init(mcxnex_state_t *);
static void mcxnex_icm_dma_fini(mcxnex_state_t *);
static void mcxnex_inithca_set(mcxnex_state_t *, mcxnex_hw_initqueryhca_t *);
static int mcxnex_hca_ports_shutdown(mcxnex_state_t *, uint_t);
static int mcxnex_internal_uarpg_init(mcxnex_state_t *);
static void mcxnex_internal_uarpg_fini(mcxnex_state_t *);
static int mcxnex_special_qp_contexts_reserve(mcxnex_state_t *);
static void mcxnex_special_qp_contexts_unreserve(mcxnex_state_t *);
static int mcxnex_sw_reset(mcxnex_state_t *);
static int mcxnex_mcg_init(mcxnex_state_t *);
static void mcxnex_mcg_fini(mcxnex_state_t *);
static int mcxnex_fw_version_check(mcxnex_state_t *);
static void mcxnex_device_info_report(mcxnex_state_t *);
static int mcxnex_pci_capability_list(mcxnex_state_t *, ddi_acc_handle_t);
static void mcxnex_pci_capability_vpd(mcxnex_state_t *, ddi_acc_handle_t,
    uint_t);
static int mcxnex_pci_read_vpd(ddi_acc_handle_t, uint_t, uint32_t, uint32_t *);
static int mcxnex_intr_or_msi_init(mcxnex_state_t *);
static int mcxnex_add_intrs(mcxnex_state_t *, int);
static int mcxnex_intr_or_msi_fini(mcxnex_state_t *);
void mcxnex_pci_capability_msix(mcxnex_state_t *, ddi_acc_handle_t, uint_t);

static uint64_t mcxnex_size_icm(mcxnex_state_t *);

/* X86 fastreboot support */
static ushort_t get_msix_ctrl(dev_info_t *);
static size_t get_msix_tbl_size(dev_info_t *);
static size_t get_msix_pba_size(dev_info_t *);
static void mcxnex_set_msix_info(mcxnex_state_t *);
static int mcxnex_intr_disable(mcxnex_state_t *);
static int mcxnex_quiesce(dev_info_t *);
static int mcxnex_attach_ether(mcxnex_state_t *, int);
static int mcxnex_bus_ctl(dev_info_t *, dev_info_t *, ddi_ctl_enum_t,
    void *, void *);
static int mcxnex_bus_config(dev_info_t *, uint_t, ddi_bus_config_op_t,
    void *, dev_info_t **);
static int mcxnex_bus_unconfig(dev_info_t *, uint_t, ddi_bus_config_op_t,
    void *);


/* Character/Block Operations */
static struct cb_ops mcxnex_cb_ops = {
	mcxnex_open,		/* open */
	mcxnex_close,		/* close */
	nodev,			/* strategy (block) */
	nodev,			/* print (block) */
	nodev,			/* dump (block) */
	nodev,			/* read */
	nodev,			/* write */
	mcxnex_ioctl,		/* ioctl */
	NULL,			/* devmap */
	NULL,			/* mmap */
	nodev,			/* segmap */
	nochpoll,		/* chpoll */
	ddi_prop_op,		/* prop_op */
	NULL,			/* streams */
	D_NEW | D_MP |
	D_64BIT | D_DEVMAP,	/* flags */
	CB_REV			/* rev */
};

/* Bus Operations */
static struct bus_ops mcxnex_bus_ops = {
	BUSO_REV,		/* busops_rev */
	nullbusmap,		/* bus_map */
	NULL,			/* bus_get_intrspec (OBSOLETE) */
	NULL,			/* bus_add_intrspec (OBSOLETE) */
	NULL,			/* bus_remove_intrspec (OBSOLETE) */
	i_ddi_map_fault,	/* bus_map_fault */
	ddi_dma_map,		/* bus_dma_map */
	ddi_dma_allochdl,	/* bus_dma_allochdl */
	ddi_dma_freehdl,	/* bus_dma_freehdl */
	ddi_dma_bindhdl,	/* bus_dma_bindhdl */
	ddi_dma_unbindhdl,	/* bus_dma_unbindhdl */
	ddi_dma_flush,		/* bus_dma_flush */
	ddi_dma_win,		/* bus_dma_win */
	ddi_dma_mctl,		/* bus_dma_ctl */
	mcxnex_bus_ctl,		/* bus_ctl */
	ddi_bus_prop_op,	/* bus_prop_op */
	NULL,			/* bus_get_eventcookie */
	NULL,			/* bus_add_eventcall */
	NULL,			/* bus_remove_eventcall */
	NULL,			/* bus_post_event */
	NULL,			/* bus_intr_ctl (OBSOLETE) */
	mcxnex_bus_config, 	/* bus_config */
	mcxnex_bus_unconfig,	/* bus_unconfig */
	NULL,			/* bus_fm_init */
	NULL,			/* bus_fm_fini */
	NULL,			/* bus_fm_access_enter */
	NULL,			/* bus_fm_access_exit */
	NULL,			/* bus_power */
	NULL,			/* bus_intr_op */
};

/* Driver Operations */
static struct dev_ops mcxnex_ops = {
	DEVO_REV,		/* struct rev */
	0,			/* refcnt */
	mcxnex_getinfo,		/* getinfo */
	nulldev,		/* identify */
	nulldev,		/* probe */
	mcxnex_attach,		/* attach */
	mcxnex_detach,		/* detach */
	nodev,			/* reset */
	&mcxnex_cb_ops,		/* cb_ops */
	&mcxnex_bus_ops,	/* bus_ops */
	nodev,			/* power */
	mcxnex_quiesce,		/* devo_quiesce */
};

/* Module Driver Info */
static struct modldrv mcxnex_modldrv = {
	&mod_driverops,
	"Mellanox ConnectX-2 Nexus Driver",
	&mcxnex_ops
};

/* Module Linkage */
static struct modlinkage mcxnex_modlinkage = {
	MODREV_1,
	&mcxnex_modldrv,
	NULL
};


/*
 * _init()
 */
int
_init(void)
{
	int	status;

	status = ddi_soft_state_init(&mcxnex_statep, sizeof (mcxnex_state_t),
	    (size_t)MCXNEX_INITIAL_STATES);
	if (status != 0) {
		return (status);
	}

	status = mod_install(&mcxnex_modlinkage);
	if (status != 0) {
		ddi_soft_state_fini(&mcxnex_statep);
		return (status);
	}

	/* Initialize the Mcxnex "userland resources database" */
	mcxnex_umap_db_init();

	return (status);
}


/*
 * _info()
 */
int
_info(struct modinfo *modinfop)
{
	return (mod_info(&mcxnex_modlinkage, modinfop));
}


/*
 * _fini()
 */
int
_fini(void)
{
	int	status;

	status = mod_remove(&mcxnex_modlinkage);
	if (status != 0) {
		return (status);
	}

	/* Destroy the Mcxnex "userland resources database" */
	mcxnex_umap_db_fini();

	ddi_soft_state_fini(&mcxnex_statep);

	return (status);
}


/*
 * mcxnex_getinfo()
 */
static int
mcxnex_getinfo(dev_info_t *dip, ddi_info_cmd_t cmd, void *arg, void **result)
{
	dev_t		dev;
	mcxnex_state_t 	*state;
	minor_t		instance;

	_NOTE(ARGUNUSED(dip));

	switch (cmd) {
	case DDI_INFO_DEVT2DEVINFO:
		dev = (dev_t)arg;
		instance = MCXNEX_DEV_INSTANCE(dev);
		state = ddi_get_soft_state(mcxnex_statep, instance);
		if (state == NULL) {
			return (DDI_FAILURE);
		}
		*result = (void *)state->hs_dip;
		return (DDI_SUCCESS);

	case DDI_INFO_DEVT2INSTANCE:
		dev = (dev_t)arg;
		instance = MCXNEX_DEV_INSTANCE(dev);
		*result = (void *)(uintptr_t)instance;
		return (DDI_SUCCESS);

	default:
		break;
	}

	return (DDI_FAILURE);
}


/*
 * mcxnex_open()
 */
/* ARGSUSED */
static int
mcxnex_open(dev_t *devp, int flag, int otyp, cred_t *credp)
{
	mcxnex_state_t		*state;
	mcxnex_rsrc_t 		*rsrcp;
	mcxnex_umap_db_entry_t	*umapdb, *umapdb2;
	minor_t			instance;
	uint64_t		key, value;
	uint_t			hr_indx;
	dev_t			dev;
	int			status;

	instance = MCXNEX_DEV_INSTANCE(*devp);
	state = ddi_get_soft_state(mcxnex_statep, instance);
	if (state == NULL) {
		return (ENXIO);
	}

	if (otyp == OTYP_LYR) {
		/*
		 * The 10GbE driver opens us with a layered open to hold
		 * the instance active.  We don't actually do any work, since
		 * this open is purely for the benefit of the DDI.
		 */
		return (0);
	}

	/*
	 * Only allow driver to be opened for character access, and verify
	 * whether exclusive access is allowed.
	 */
	if ((otyp != OTYP_CHR) || ((flag & FEXCL) &&
	    secpolicy_excl_open(credp) != 0)) {
		return (EINVAL);
	}

	/*
	 * Search for the current process PID in the "userland resources
	 * database".  If it is not found, then attempt to allocate a UAR
	 * page and add the ("key", "value") pair to the database.
	 * Note:  As a last step we always return a devp appropriate for
	 * the open.  Either we return a new minor number (based on the
	 * instance and the UAR page index) or we return the current minor
	 * number for the given client process.
	 *
	 * We also add an entry to the database to allow for lookup from
	 * "dev_t" to the current process PID.  This is necessary because,
	 * under certain circumstance, the process PID that calls the Mcxnex
	 * close() entry point may not be the same as the one who called
	 * open().  Specifically, this can happen if a child process calls
	 * the Mcxnex's open() entry point, gets a UAR page, maps it out (using
	 * mmap()), and then exits without calling munmap().  Because mmap()
	 * adds a reference to the file descriptor, at the exit of the child
	 * process the file descriptor is "inherited" by the parent (and will
	 * be close()'d by the parent's PID only when it exits).
	 *
	 * Note: We use the mcxnex_umap_db_find_nolock() and
	 * mcxnex_umap_db_add_nolock() database access routines below (with
	 * an explicit mutex_enter of the database lock - "hdl_umapdb_lock")
	 * to ensure that the multiple accesses (in this case searching for,
	 * and then adding _two_ database entries) can be done atomically.
	 */
	key = ddi_get_pid();
	mutex_enter(&mcxnex_userland_rsrc_db.hdl_umapdb_lock);
	status = mcxnex_umap_db_find_nolock(instance, key,
	    MLNX_UMAP_UARPG_RSRC, &value, 0, NULL);
	if (status != DDI_SUCCESS) {
		/*
		 * If we are in 'maintenance mode', we cannot alloc a UAR page.
		 * But we still need some rsrcp value, and a mostly unique
		 * hr_indx value.  So we set rsrcp to NULL for maintenance
		 * mode, and use a rolling count for hr_indx.  The field
		 * 'hs_open_hr_indx' is used only in this maintenance mode
		 * condition.
		 *
		 * Otherwise, if we are in operational mode then we allocate
		 * the UAR page as normal, and use the rsrcp value and tr_indx
		 * value from that allocation.
		 */
		if (!MCXNEX_IS_OPERATIONAL(state->hs_operational_mode)) {
			rsrcp = NULL;
			hr_indx = state->hs_open_ar_indx++;
		} else {
			/* Allocate a new UAR page for this process */
			status = mcxnex_rsrc_alloc(state, MCXNEX_UARPG, 1,
			    DDI_NOSLEEP, &rsrcp);
			if (status != DDI_SUCCESS) {
				mutex_exit(
				    &mcxnex_userland_rsrc_db.hdl_umapdb_lock);
				return (EAGAIN);
			}

			hr_indx = rsrcp->hr_indx;
		}

		/*
		 * Allocate an entry to track the UAR page resource in the
		 * "userland resources database".
		 */
		umapdb = mcxnex_umap_db_alloc(instance, key,
		    MLNX_UMAP_UARPG_RSRC, (uint64_t)(uintptr_t)rsrcp);
		if (umapdb == NULL) {
			mutex_exit(&mcxnex_userland_rsrc_db.hdl_umapdb_lock);
			/* If in "maintenance mode", don't free the rsrc */
			if (MCXNEX_IS_OPERATIONAL(state->hs_operational_mode)) {
				mcxnex_rsrc_free(state, &rsrcp);
			}
			return (EAGAIN);
		}

		/*
		 * Create a new device number.  Minor number is a function of
		 * the UAR page index (15 bits) and the device instance number
		 * (3 bits).
		 */
		dev = makedevice(getmajor(*devp), (hr_indx <<
		    MCXNEX_MINORNUM_SHIFT) | instance);

		/*
		 * Allocate another entry in the "userland resources database"
		 * to track the association of the device number (above) to
		 * the current process ID (in "key").
		 */
		umapdb2 = mcxnex_umap_db_alloc(instance, dev,
		    MLNX_UMAP_PID_RSRC, (uint64_t)key);
		if (umapdb2 == NULL) {
			mutex_exit(&mcxnex_userland_rsrc_db.hdl_umapdb_lock);
			mcxnex_umap_db_free(umapdb);
			/* If in "maintenance mode", don't free the rsrc */
			if (MCXNEX_IS_OPERATIONAL(state->hs_operational_mode)) {
				mcxnex_rsrc_free(state, &rsrcp);
			}
			return (EAGAIN);
		}

		/* Add the entries to the database */
		mcxnex_umap_db_add_nolock(umapdb);
		mcxnex_umap_db_add_nolock(umapdb2);

	} else {
		/*
		 * Return the same device number as on the original open()
		 * call.  This was calculated as a function of the UAR page
		 * index (top 16 bits) and the device instance number
		 */
		rsrcp = (mcxnex_rsrc_t *)(uintptr_t)value;
		dev = makedevice(getmajor(*devp), (rsrcp->hr_indx <<
		    MCXNEX_MINORNUM_SHIFT) | instance);
	}
	mutex_exit(&mcxnex_userland_rsrc_db.hdl_umapdb_lock);

	*devp = dev;

	return (0);
}


/*
 * mcxnex_close()
 */
/* ARGSUSED */
static int
mcxnex_close(dev_t dev, int flag, int otyp, cred_t *credp)
{
	mcxnex_state_t		*state;
	mcxnex_rsrc_t		*rsrcp;
	mcxnex_umap_db_entry_t	*umapdb;
	mcxnex_umap_db_priv_t	*priv;
	minor_t			instance;
	uint64_t		key, value;
	int			status, reset_status = 0;

	instance = MCXNEX_DEV_INSTANCE(dev);
	state = ddi_get_soft_state(mcxnex_statep, instance);
	if (state == NULL) {
		return (ENXIO);
	}

	/*
	 * Layered driver open for the benefit of the DDI.  See the
	 * comments in mcxnex_open() for OTYP_LYR.
	 */
	if (otyp == OTYP_LYR) {
		return (0);
	}

	/*
	 * Search for "dev_t" in the "userland resources database".  As
	 * explained above in mcxnex_open(), we can't depend on using the
	 * current process ID here to do the lookup because the process
	 * that ultimately closes may not be the same one who opened
	 * (because of inheritance).
	 * So we lookup the "dev_t" (which points to the PID of the process
	 * that opened), and we remove the entry from the database (and free
	 * it up).  Then we do another query based on the PID value.  And when
	 * we find that database entry, we free it up too and then free the
	 * Mcxnex UAR page resource.
	 *
	 * Note: We use the mcxnex_umap_db_find_nolock() database access
	 * routine below (with an explicit mutex_enter of the database lock)
	 * to ensure that the multiple accesses (which attempt to remove the
	 * two database entries) can be done atomically.
	 *
	 * This works the same in both maintenance mode and HCA mode, except
	 * for the call to mcxnex_rsrc_free().  In the case of maintenance mode,
	 * this call is not needed, as it was not allocated in mcxnex_open()
	 * above.
	 */
	key = dev;
	mutex_enter(&mcxnex_userland_rsrc_db.hdl_umapdb_lock);
	status = mcxnex_umap_db_find_nolock(instance, key, MLNX_UMAP_PID_RSRC,
	    &value, MCXNEX_UMAP_DB_REMOVE, &umapdb);
	if (status == DDI_SUCCESS) {
		/*
		 * If the "hdb_priv" field is non-NULL, it indicates that
		 * some "on close" handling is still necessary.  Call
		 * mcxnex_umap_db_handle_onclose_cb() to do the handling (i.e.
		 * to invoke all the registered callbacks).  Then free up
		 * the resources associated with "hdb_priv" and continue
		 * closing.
		 */
		priv = (mcxnex_umap_db_priv_t *)umapdb->hdbe_common.hdb_priv;
		if (priv != NULL) {
			reset_status = mcxnex_umap_db_handle_onclose_cb(priv);
			kmem_free(priv, sizeof (mcxnex_umap_db_priv_t));
			umapdb->hdbe_common.hdb_priv = (void *)NULL;
		}

		mcxnex_umap_db_free(umapdb);

		/*
		 * Now do another lookup using PID as the key (copy it from
		 * "value").  When this lookup is complete, the "value" field
		 * will contain the mcxnex_rsrc_t pointer for the UAR page
		 * resource.
		 */
		key = value;
		status = mcxnex_umap_db_find_nolock(instance, key,
		    MLNX_UMAP_UARPG_RSRC, &value, MCXNEX_UMAP_DB_REMOVE,
		    &umapdb);
		if (status == DDI_SUCCESS) {
			mcxnex_umap_db_free(umapdb);
			/* If in "maintenance mode", don't free the rsrc */
			if (MCXNEX_IS_OPERATIONAL(state->hs_operational_mode)) {
				rsrcp = (mcxnex_rsrc_t *)(uintptr_t)value;
				mcxnex_rsrc_free(state, &rsrcp);
			}
		}
	}
	mutex_exit(&mcxnex_userland_rsrc_db.hdl_umapdb_lock);
	return (reset_status);
}


/*
 * mcxnex_attach()
 *    Context: Only called from attach() path context
 */
static int
mcxnex_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	mcxnex_state_t	*state;
	int		instance;
	int		status;

#ifdef __lock_lint
	(void) mcxnex_quiesce(dip);
#endif
	switch (cmd) {
	case DDI_ATTACH:
		instance = ddi_get_instance(dip);
		status = ddi_soft_state_zalloc(mcxnex_statep, instance);
		if (status != DDI_SUCCESS) {
			cmn_err(CE_NOTE, "%s%d: driver failed to attach: "
			    "attach_ssz_fail", ddi_driver_name(dip), instance);
			goto fail_attach_nomsg;

		}
		state = ddi_get_soft_state(mcxnex_statep, instance);
		ddi_set_driver_private(dip, state);
		ASSERT(state != NULL);

		/* initialize parent data */
		for (int i = 0; i < MCXNEX_MAX_PORTS; i++) {
			state->hs_ppd[i].cp_state = state;
			state->hs_ppd[i].cp_port = i;
		}

		/* clear the attach error buffer */
		MCXNEX_ATTACH_MSG_INIT(state->hs_attach_buf);

		/* Save away devinfo and instance before mcxnex_fm_init() */
		state->hs_dip = dip;
		state->hs_instance = instance;

		mcxnex_fm_init(state);

		/*
		 * Initialize driver and hardware.
		 *
		 * Note: If this initialization fails we may still
		 * wish to create a device node and remain operational
		 * so that firmware can be updated/flashed
		 * (i.e. "maintenance mode").  If this is the case,
		 * then "hs_operational_mode" will be equal to
		 * MCXNEX_MAINTENANCE_MODE.  We will not attempt to
		 * attach attach child nodes.
		 */
		status = mcxnex_drv_init(state, dip, instance);
		if ((status != DDI_SUCCESS) &&
		    (MCXNEX_IS_OPERATIONAL(state->hs_operational_mode))) {
			goto fail_attach;
		}

		/*
		 * Change the Mcxnex FM mode
		 */
		if ((mcxnex_get_state(state) & HCA_PIO_FM) &&
		    MCXNEX_IS_OPERATIONAL(state->hs_operational_mode)) {
			/*
			 * Now we wait for 50ms to give an opportunity
			 * to Solaris FMA so that HW errors can be notified.
			 * Then check if there are HW errors or not. If
			 * a HW error is detected, the Mcxnex attachment
			 * must be failed.
			 */
			delay(drv_usectohz(50000));
			if (mcxnex_init_failure(state)) {
				mcxnex_drv_fini(state);
				MCXNEX_WARNING(state, "unable to "
				    "attach due to a HW error");
				MCXNEX_ATTACH_MSG(state->hs_attach_buf,
				    "mcxnex_attach_failure due to a HW error");
				goto fail_attach;
			}

			/*
			 * There seems no HW errors during the attachment,
			 * so let's change the Mcxnex FM state to the
			 * ereport only mode.
			 */
			if (mcxnex_fm_ereport_init(state) != DDI_SUCCESS) {
				/* unwind the resources */
				mcxnex_drv_fini(state);
				MCXNEX_ATTACH_MSG(state->hs_attach_buf,
				    "mcxnex_attach_failure");
				goto fail_attach;
			}
		}

		/* Create the minor node for device */
		status = ddi_create_minor_node(dip, "devctl", S_IFCHR, instance,
		    DDI_PSEUDO, 0);
		if (status != DDI_SUCCESS) {
			MCXNEX_WARNING(state, "unable to create minor node");
			mcxnex_drv_fini(state);
			MCXNEX_ATTACH_MSG(state->hs_attach_buf,
			    "attach_create_mn_fail");
			goto fail_attach;
		}

		/*
		 * If we are in "maintenance mode", then we don't want to
		 * register with the IBTF.  All InfiniBand interfaces are
		 * uninitialized, and the device is only capable of handling
		 * requests to update/flash firmware (or test/debug requests).
		 */
		if (MCXNEX_IS_OPERATIONAL(state->hs_operational_mode)) {
			state->hs_ibtfpriv = dip;
			if (mcxnex_verbose)
				MCXNEX_NOTE(state, "!HCA is operational");
		}

		/* Report attach in maintenance mode, if appropriate */
		if (!(MCXNEX_IS_OPERATIONAL(state->hs_operational_mode))) {
			MCXNEX_NOTE(state, "driver attached "
			    "(maintenance mode only)");
			mcxnex_fm_ereport(state, HCA_IBA_ERR, HCA_ERR_DEGRADED);
		}

		/* Report that driver was loaded */
		ddi_report_dev(dip);

		/* Send device information to log file */
		mcxnex_device_info_report(state);

		return (DDI_SUCCESS);

	case DDI_RESUME:
		/* Add code here for DDI_RESUME XXX */
		return (DDI_FAILURE);

	default:
		return (DDI_FAILURE);
	}

fail_attach:
	MCXNEX_WARNING(state, "driver failed to attach: %s",
	    state->hs_attach_buf);
	if (mcxnex_get_state(state) & HCA_EREPORT_FM) {
		mcxnex_fm_ereport(state, HCA_SYS_ERR, HCA_ERR_SRV_LOST);
	}
	mcxnex_drv_fini2(state);
	mcxnex_fm_fini(state);
	ddi_soft_state_free(mcxnex_statep, instance);

fail_attach_nomsg:
	return (DDI_FAILURE);
}


/*
 * mcxnex_detach()
 *    Context: Only called from detach() path context
 */
static int
mcxnex_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	mcxnex_state_t	*state;

	state = ddi_get_driver_private(dip);
	if (state == NULL) {
		return (DDI_FAILURE);
	}

	switch (cmd) {
	case DDI_DETACH:

		/* Remove the minor node for device */
		ddi_remove_minor_node(dip, "devctl");

		/*
		 * Only call mcxnex_drv_fini() if we are in HCA mode.
		 * (Because if we are in "maintenance mode", then we never
		 * successfully finished init.)  Only report successful
		 * detach for normal HCA mode.
		 */
		if (MCXNEX_IS_OPERATIONAL(state->hs_operational_mode)) {
			/* Cleanup driver resources and shutdown hardware */
			mcxnex_drv_fini(state);
		}

		mcxnex_drv_fini2(state);
		mcxnex_fm_fini(state);
		ddi_soft_state_free(mcxnex_statep, ddi_get_instance(dip));

		return (DDI_SUCCESS);

	case DDI_SUSPEND:
		/* Add code here for DDI_SUSPEND XXX */
		return (DDI_FAILURE);

	default:
		return (DDI_FAILURE);
	}
}

/*
 * mcxnex_dma_attr_init()
 *    Context: Can be called from interrupt or base context.
 */

/* ARGSUSED */
void
mcxnex_dma_attr_init(mcxnex_state_t *state, ddi_dma_attr_t *dma_attr)
{
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(*dma_attr))

	dma_attr->dma_attr_version	= DMA_ATTR_V0;
	dma_attr->dma_attr_addr_lo	= 0;
	dma_attr->dma_attr_addr_hi	= 0xFFFFFFFFFFFFFFFFull;
	dma_attr->dma_attr_count_max	= 0xFFFFFFFFFFFFFFFFull;
	dma_attr->dma_attr_align	= MCXNEX_PAGESIZE;  /* default 4K */
	dma_attr->dma_attr_burstsizes	= 0x3FF;
	dma_attr->dma_attr_minxfer	= 1;
	dma_attr->dma_attr_maxxfer	= 0xFFFFFFFFFFFFFFFFull;
	dma_attr->dma_attr_seg		= 0xFFFFFFFFFFFFFFFFull;
	dma_attr->dma_attr_sgllen	= 0x7FFFFFFF;
	dma_attr->dma_attr_granular	= 1;
	dma_attr->dma_attr_flags	= 0;
}

/*
 * mcxnex_dma_alloc()
 *    Context: Can be called from base context.
 */
int
mcxnex_dma_alloc(mcxnex_state_t *state, mcxnex_dma_info_t *dma_info,
    uint16_t opcode)
{
	ddi_dma_handle_t	dma_hdl;
	ddi_dma_attr_t		dma_attr;
	ddi_acc_handle_t	acc_hdl;
	ddi_dma_cookie_t	cookie;
	uint64_t		kaddr;
	uint64_t		real_len;
	uint_t			ccount;
	int			status;

	mcxnex_dma_attr_init(state, &dma_attr);

	/* Allocate a DMA handle */
	status = ddi_dma_alloc_handle(state->hs_dip, &dma_attr, DDI_DMA_SLEEP,
	    NULL, &dma_hdl);
	if (status != DDI_SUCCESS) {
		MCXNEX_WARNING(state, "DMA alloc handle failed (status %d)",
		    status);
		return (DDI_FAILURE);
	}

	/* Allocate DMA memory */
	status = ddi_dma_mem_alloc(dma_hdl, dma_info->length,
	    &state->hs_reg_accattr, DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, NULL,
	    (caddr_t *)&kaddr, (size_t *)&real_len, &acc_hdl);
	if (status != DDI_SUCCESS) {
		ddi_dma_free_handle(&dma_hdl);
		MCXNEX_WARNING(state, "DMA memory alloc failed (status %d)",
		    status);
		return (DDI_FAILURE);
	}
	bzero((caddr_t)(uintptr_t)kaddr, real_len);

	/* Bind the memory to the handle */
	status = ddi_dma_addr_bind_handle(dma_hdl, NULL,
	    (caddr_t)(uintptr_t)kaddr, (size_t)real_len, DDI_DMA_RDWR |
	    DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, NULL, &cookie, &ccount);
	if (status != DDI_SUCCESS) {
		ddi_dma_mem_free(&acc_hdl);
		ddi_dma_free_handle(&dma_hdl);
		MCXNEX_WARNING(state, "DMA bind handle failed (status %d)",
		    status);
		return (DDI_FAILURE);
	}

	/* Package the mcxnex_dma_info contents and return */
	dma_info->vaddr   = kaddr;
	dma_info->dma_hdl = dma_hdl;
	dma_info->acc_hdl = acc_hdl;

	/* Pass the mapping information to the firmware */
	status = mcxnex_map_cmd_post(state, dma_info, opcode, cookie, ccount);
	if (status != DDI_SUCCESS) {
		char *s;
		mcxnex_dma_free(dma_info);
		switch (opcode) {
		case MAP_ICM:
			s = "MAP_ICM";
			break;
		case MAP_FA:
			s = "MAP_FA";
			break;
		case MAP_ICM_AUX:
			s = "MAP_ICM_AUX";
			break;
		default:
			s = "UNKNOWN";
		}
		MCXNEX_WARNING(state, "Map cmd '%s' failed, status %x",
		    s, status);
		return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);
}

/*
 * mcxnex_dma_free()
 *    Context: Can be called from base context.
 */
void
mcxnex_dma_free(mcxnex_dma_info_t *info)
{
	/* Unbind the handles and free the memory */
	(void) ddi_dma_unbind_handle(info->dma_hdl);
	ddi_dma_mem_free(&info->acc_hdl);
	ddi_dma_free_handle(&info->dma_hdl);
}

/* These macros are valid for use only in mcxnex_icm_alloc/mcxnex_icm_free. */
#define	MCXNEX_ICM_ALLOC(rsrc) \
	mcxnex_icm_alloc(state, rsrc, index1, index2)
#define	MCXNEX_ICM_FREE(rsrc) \
	mcxnex_icm_free(state, rsrc, index1, index2)

/*
 * mcxnex_icm_alloc()
 *    Context: Can be called from base context.
 *
 * Only one thread can be here for a given mcxnex_rsrc_type_t "type".
 */
int
mcxnex_icm_alloc(mcxnex_state_t *state, mcxnex_rsrc_type_t type,
    uint32_t index1, uint32_t index2)
{
	mcxnex_icm_table_t	*icm;
	mcxnex_dma_info_t	*dma_info;
	uint8_t			*bitmap;
	int			status;

	icm = &state->hs_icm[type];

	switch (type) {
	case MCXNEX_QPC:
		status = MCXNEX_ICM_ALLOC(MCXNEX_CMPT_QPC);
		if (status != DDI_SUCCESS) {
			return (status);
		}
		status = MCXNEX_ICM_ALLOC(MCXNEX_RDB);
		if (status != DDI_SUCCESS) {	/* undo icm_alloc's */
			MCXNEX_ICM_FREE(MCXNEX_CMPT_QPC);
			return (status);
		}
		status = MCXNEX_ICM_ALLOC(MCXNEX_ALTC);
		if (status != DDI_SUCCESS) {	/* undo icm_alloc's */
			MCXNEX_ICM_FREE(MCXNEX_RDB);
			MCXNEX_ICM_FREE(MCXNEX_CMPT_QPC);
			return (status);
		}
		status = MCXNEX_ICM_ALLOC(MCXNEX_AUXC);
		if (status != DDI_SUCCESS) {	/* undo icm_alloc's */
			MCXNEX_ICM_FREE(MCXNEX_ALTC);
			MCXNEX_ICM_FREE(MCXNEX_RDB);
			MCXNEX_ICM_FREE(MCXNEX_CMPT_QPC);
			return (status);
		}
		break;
	case MCXNEX_SRQC:
		status = MCXNEX_ICM_ALLOC(MCXNEX_CMPT_SRQC);
		if (status != DDI_SUCCESS) {
			return (status);
		}
		break;
	case MCXNEX_CQC:
		status = MCXNEX_ICM_ALLOC(MCXNEX_CMPT_CQC);
		if (status != DDI_SUCCESS) {
			return (status);
		}
		break;
	case MCXNEX_EQC:
		status = MCXNEX_ICM_ALLOC(MCXNEX_CMPT_EQC);
		if (status != DDI_SUCCESS) {	/* undo icm_alloc's */
			return (status);
		}
		break;
	}

	/* ensure existence of bitmap and dmainfo, sets "dma_info" */
	mcxnex_bitmap(bitmap, dma_info, icm, index1);

	/* Set up the DMA handle for allocation and mapping */
	dma_info = icm->icm_dma[index1] + index2;
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(*dma_info))
	dma_info->length  = icm->span << icm->log_object_size;
	dma_info->icmaddr = icm->icm_baseaddr +
	    (((index1 << icm->split_shift) +
	    (index2 << icm->span_shift)) << icm->log_object_size);

	DTRACE_PROBE6(mcxnex__alloc_icm_dma,
	    mcxnex_state_t *, state,
	    mcxnex_rsrc_type_t, type,
	    uint32_t, index1,
	    uint32_t, index2,
	    mcxnex_dma_info_t *, dma_info,
	    uint8_t *, bitmap);

	if (dma_info->length < MCXNEX_PAGESIZE)
		dma_info->length = MCXNEX_PAGESIZE;

	/* Allocate and map memory for this span */
	status = mcxnex_dma_alloc(state, dma_info, MAP_ICM);
	if (status != DDI_SUCCESS) {

		switch (type) {
		case MCXNEX_QPC:
			MCXNEX_ICM_FREE(MCXNEX_AUXC);
			MCXNEX_ICM_FREE(MCXNEX_ALTC);
			MCXNEX_ICM_FREE(MCXNEX_RDB);
			MCXNEX_ICM_FREE(MCXNEX_CMPT_QPC);
			break;
		case MCXNEX_SRQC:
			MCXNEX_ICM_FREE(MCXNEX_CMPT_SRQC);
			break;
		case MCXNEX_CQC:
			MCXNEX_ICM_FREE(MCXNEX_CMPT_CQC);
			break;
		case MCXNEX_EQC:
			MCXNEX_ICM_FREE(MCXNEX_CMPT_EQC);
			break;
		}

		return (DDI_FAILURE);
	}

	DTRACE_PROBE5(mcxnex__mapped_icm_dma,
	    mcxnex_state_t *, state,
	    mcxnex_rsrc_type_t, type,
	    uint32_t, index1,
	    uint32_t, index2,
	    mcxnex_dma_info_t *, dma_info);

	/* Set the bit for this slot in the table bitmap */
	MCXNEX_BMAP_BIT_SET(icm->icm_bitmap[index1], index2);

	return (DDI_SUCCESS);
}

/*
 * mcxnex_icm_free()
 *    Context: Can be called from base context.
 *
 * ICM resources have been successfully returned from mcxnex_icm_alloc().
 * Associated dma_info is no longer in use.  Free the ICM backing memory.
 */
void
mcxnex_icm_free(mcxnex_state_t *state, mcxnex_rsrc_type_t type,
    uint32_t index1, uint32_t index2)
{
	mcxnex_icm_table_t	*icm;
	mcxnex_dma_info_t	*dma_info;
	int			status;

	icm = &state->hs_icm[type];
	ASSERT(icm->icm_dma[index1][index2].icm_refcnt == 0);

	dma_info = icm->icm_dma[index1] + index2;

	/* The following only happens if attach() is failing. */
	if (dma_info == NULL)
		return;

	/* Unmap the ICM allocation, then free the backing DMA memory */
	status = mcxnex_unmap_icm_cmd_post(state, dma_info);
	if (status != DDI_SUCCESS) {
		MCXNEX_WARNING(state, "UNMAP_ICM failure: %x", status);
	}
	mcxnex_dma_free(dma_info);

	/* Clear the bit in the ICM table bitmap */
	MCXNEX_BMAP_BIT_CLR(icm->icm_bitmap[index1], index2);

	switch (type) {
	case MCXNEX_QPC:
		MCXNEX_ICM_FREE(MCXNEX_AUXC);
		MCXNEX_ICM_FREE(MCXNEX_ALTC);
		MCXNEX_ICM_FREE(MCXNEX_RDB);
		MCXNEX_ICM_FREE(MCXNEX_CMPT_QPC);
		break;
	case MCXNEX_SRQC:
		MCXNEX_ICM_FREE(MCXNEX_CMPT_SRQC);
		break;
	case MCXNEX_CQC:
		MCXNEX_ICM_FREE(MCXNEX_CMPT_CQC);
		break;
	case MCXNEX_EQC:
		MCXNEX_ICM_FREE(MCXNEX_CMPT_EQC);
		break;

	}
}

/*
 * mcxnex_drv_init()
 *    Context: Only called from attach() path context
 */
/* ARGSUSED */
static int
mcxnex_drv_init(mcxnex_state_t *state, dev_info_t *dip, int instance)
{
	int	status;

	/*
	 * Check and set the operational mode of the device. If the driver is
	 * bound to the Mcxnex device in "maintenance mode", then this generally
	 * means that either the device has been specifically jumpered to
	 * start in this mode or the firmware boot process has failed to
	 * successfully load either the primary or the secondary firmware
	 * image.
	 */
	if (MCXNEX_IS_HCA_MODE(state->hs_dip)) {
		state->hs_operational_mode = MCXNEX_HCA_MODE;
		state->hs_cfg_profile_setting = MCXNEX_CFG_MEMFREE;

	} else if (MCXNEX_IS_MAINTENANCE_MODE(state->hs_dip)) {
		MCXNEX_FMANOTE(state, MCXNEX_FMA_MAINT);
		state->hs_operational_mode = MCXNEX_MAINTENANCE_MODE;
		state->hs_fm_degraded_reason = HCA_FW_MISC; /* not fw reason */
		return (DDI_FAILURE);

	} else {
		state->hs_operational_mode = 0;	/* invalid operational mode */
		MCXNEX_FMANOTE(state, MCXNEX_FMA_PCIID);
		MCXNEX_WARNING(state, "unexpected device type detected");
		return (DDI_FAILURE);
	}

	/*
	 * Initialize the hardware.
	 *
	 * Note:  If this routine returns an error, it is often a reasonably
	 * good indication that something Mcxnex firmware-related has caused
	 * the failure or some HW related errors have caused the failure.
	 * (also there are few possibilities that SW (e.g. SW resource
	 * shortage) can cause the failure, but the majority case is due to
	 * either a firmware related error or a HW related one) In order to
	 * give the user an opportunity (if desired) to update or reflash
	 * the Mcxnex firmware image, we set "hs_operational_mode" flag
	 * (described above) to indicate that we wish to enter maintenance
	 * mode in case of the firmware-related issue.
	 */
	status = mcxnex_hw_init(state);
	if (status != DDI_SUCCESS) {
		MCXNEX_WARNING(state, "error during attach: %s",
		    state->hs_attach_buf);
		return (DDI_FAILURE);
	}

	/*
	 * Now that the ISR has been setup, arm all the EQs for event
	 * generation.
	 */

	status = mcxnex_eq_arm_all(state);
	if (status != DDI_SUCCESS) {
		MCXNEX_WARNING(state, "EQ Arm All failed");
		mcxnex_hw_fini(state, MCXNEX_DRV_CLEANUP_ALL);
		return (DDI_FAILURE);
	}

	/* test interrupts and event queues */
	status = mcxnex_nop_post(state, 0x0, 0x0);
	if (status != DDI_SUCCESS) {
		MCXNEX_WARNING(state, "Interrupts/EQs failed");
		mcxnex_hw_fini(state, MCXNEX_DRV_CLEANUP_ALL);
		return (DDI_FAILURE);
	}

	/* Initialize softstate */
	status = mcxnex_soft_state_init(state);
	if (status != DDI_SUCCESS) {
		MCXNEX_WARNING(state, "Failed to init soft state");
		mcxnex_hw_fini(state, MCXNEX_DRV_CLEANUP_ALL);
		return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);
}


/*
 * mcxnex_drv_fini()
 *    Context: Only called from attach() and/or detach() path contexts
 */
static void
mcxnex_drv_fini(mcxnex_state_t *state)
{
	/* Cleanup Mcxnex softstate */
	mcxnex_soft_state_fini(state);

	/* Cleanup Mcxnex resources and shutdown hardware */
	mcxnex_hw_fini(state, MCXNEX_DRV_CLEANUP_ALL);
}


/*
 * mcxnex_drv_fini2()
 *    Context: Only called from attach() and/or detach() path contexts
 */
static void
mcxnex_drv_fini2(mcxnex_state_t *state)
{
	if (state->hs_fm_poll_thread) {
		ddi_periodic_delete(state->hs_fm_poll_thread);
		state->hs_fm_poll_thread = NULL;
	}

	/* MCXNEX_DRV_CLEANUP_LEVEL1 */
	if (state->hs_fm_cmdhdl) {
		mcxnex_regs_map_free(state, &state->hs_fm_cmdhdl);
		state->hs_fm_cmdhdl = NULL;
	}

	if (state->hs_reg_cmdhdl) {
		ddi_regs_map_free(&state->hs_reg_cmdhdl);
		state->hs_reg_cmdhdl = NULL;
	}

	/* MCXNEX_DRV_CLEANUP_LEVEL0 */
	if (state->hs_msix_tbl_entries) {
		kmem_free(state->hs_msix_tbl_entries,
		    state->hs_msix_tbl_size);
		state->hs_msix_tbl_entries = NULL;
	}

	if (state->hs_msix_pba_entries) {
		kmem_free(state->hs_msix_pba_entries,
		    state->hs_msix_pba_size);
		state->hs_msix_pba_entries = NULL;
	}

	if (state->hs_fm_msix_tblhdl) {
		mcxnex_regs_map_free(state, &state->hs_fm_msix_tblhdl);
		state->hs_fm_msix_tblhdl = NULL;
	}

	if (state->hs_reg_msix_tblhdl) {
		ddi_regs_map_free(&state->hs_reg_msix_tblhdl);
		state->hs_reg_msix_tblhdl = NULL;
	}

	if (state->hs_fm_msix_pbahdl) {
		mcxnex_regs_map_free(state, &state->hs_fm_msix_pbahdl);
		state->hs_fm_msix_pbahdl = NULL;
	}

	if (state->hs_reg_msix_pbahdl) {
		ddi_regs_map_free(&state->hs_reg_msix_pbahdl);
		state->hs_reg_msix_pbahdl = NULL;
	}

	if (state->hs_fm_pcihdl) {
		mcxnex_pci_config_teardown(state, &state->hs_fm_pcihdl);
		state->hs_fm_pcihdl = NULL;
	}

	if (state->hs_reg_pcihdl) {
		pci_config_teardown(&state->hs_reg_pcihdl);
		state->hs_reg_pcihdl = NULL;
	}
}


/*
 * mcxnex_isr_init()
 *    Context: Only called from attach() path context
 */
static int
mcxnex_isr_init(mcxnex_state_t *state)
{
	int	status;
	int	intr;

	for (intr = 0; intr < state->hs_intrmsi_allocd; intr++) {

		/*
		 * Add a handler for the interrupt or MSI
		 */
		status = ddi_intr_add_handler(state->hs_intrmsi_hdl[intr],
		    mcxnex_isr, (caddr_t)state, (void *)(uintptr_t)intr);
		if (status  != DDI_SUCCESS) {
			return (DDI_FAILURE);
		}

		/*
		 * Enable the software interrupt.  Note: depending on the value
		 * returned in the capability flag, we have to call either
		 * ddi_intr_block_enable() or ddi_intr_enable().
		 */
		if (state->hs_intrmsi_cap & DDI_INTR_FLAG_BLOCK) {
			status = ddi_intr_block_enable(
			    &state->hs_intrmsi_hdl[intr], 1);
			if (status != DDI_SUCCESS) {
				return (DDI_FAILURE);
			}
		} else {
			status = ddi_intr_enable(state->hs_intrmsi_hdl[intr]);
			if (status != DDI_SUCCESS) {
				return (DDI_FAILURE);
			}
		}
	}

	/*
	 * Now that the ISR has been enabled, defer arm_all  EQs for event
	 * generation until later, in case MSIX is enabled
	 */
	return (DDI_SUCCESS);
}


/*
 * mcxnex_isr_fini()
 *    Context: Only called from attach() and/or detach() path contexts
 */
static void
mcxnex_isr_fini(mcxnex_state_t *state)
{
	int	intr;

	for (intr = 0; intr < state->hs_intrmsi_allocd; intr++) {
		/* Disable the software interrupt */
		if (state->hs_intrmsi_cap & DDI_INTR_FLAG_BLOCK) {
			(void) ddi_intr_block_disable(
			    &state->hs_intrmsi_hdl[intr], 1);
		} else {
			(void) ddi_intr_disable(state->hs_intrmsi_hdl[intr]);
		}

		/*
		 * Remove the software handler for the interrupt or MSI
		 */
		(void) ddi_intr_remove_handler(state->hs_intrmsi_hdl[intr]);
	}
}


/*
 * Sum of ICM configured values:
 *     cMPT, dMPT, MTT, QPC, SRQC, RDB, CQC, ALTC, AUXC, EQC, MCG
 *
 */
static uint64_t
mcxnex_size_icm(mcxnex_state_t *state)
{
	mcxnex_hw_querydevlim_t	*devlim;
	mcxnex_cfg_profile_t	*cfg;
	uint64_t		num_cmpts, num_dmpts, num_mtts;
	uint64_t		num_qpcs, num_srqc, num_rdbs;
#ifndef MCXNEXUS_FW_WORKAROUND
	uint64_t		num_auxc;
#endif
	uint64_t		num_cqcs, num_altc;
	uint64_t		num_eqcs, num_mcgs;
	uint64_t		size;

	devlim = &state->hs_devlim;
	cfg = state->hs_cfg_profile;
	/* number of respective entries */
	num_cmpts = (uint64_t)0x1 << cfg->cp_log_num_cmpt;
	num_mtts = (uint64_t)0x1 << cfg->cp_log_num_mtt;
	num_dmpts = (uint64_t)0x1 << cfg->cp_log_num_dmpt;
	num_qpcs = (uint64_t)0x1 << cfg->cp_log_num_qp;
	num_srqc = (uint64_t)0x1 << cfg->cp_log_num_srq;
	num_rdbs = (uint64_t)0x1 << cfg->cp_log_num_rdb;
	num_cqcs = (uint64_t)0x1 << cfg->cp_log_num_cq;
	num_altc = (uint64_t)0x1 << cfg->cp_log_num_qp;
#ifndef MCXNEXUS_FW_WORKAROUND
	num_auxc = (uint64_t)0x1 << cfg->cp_log_num_qp;
#endif
	num_eqcs = (uint64_t)0x1 << cfg->cp_log_num_eq;
	num_mcgs = (uint64_t)0x1 << cfg->cp_log_num_mcg;

	size =
	    num_cmpts 	* devlim->cmpt_entry_sz +
	    num_dmpts	* devlim->dmpt_entry_sz +
	    num_mtts	* devlim->mtt_entry_sz +
	    num_qpcs	* devlim->qpc_entry_sz +
	    num_srqc	* devlim->srq_entry_sz +
	    num_rdbs	* devlim->rdmardc_entry_sz +
	    num_cqcs	* devlim->cqc_entry_sz +
	    num_altc	* devlim->altc_entry_sz +
#ifdef MCXNEXUS_FW_WORKAROUND
	    0x80000000ull +
#else
	    num_auxc	* devlim->aux_entry_sz	+
#endif
	    num_eqcs	* devlim->eqc_entry_sz +
	    num_mcgs	* MCXNEX_MCGMEM_SZ(state);
	return (size);
}


/*
 * mcxnex_hw_init()
 *    Context: Only called from attach() path context
 */
static int
mcxnex_hw_init(mcxnex_state_t *state)
{
	mcxnex_drv_cleanup_level_t	cleanup;
	sm_nodeinfo_t			nodeinfo;
	uint64_t			clr_intr_offset;
	int				status;
	uint32_t			fw_size;	/* in page */
	uint64_t			offset;

	/* This is where driver initialization begins */
	cleanup = MCXNEX_DRV_CLEANUP_LEVEL0;

	/* Setup device access attributes */
	state->hs_reg_accattr.devacc_attr_version = DDI_DEVICE_ATTR_V1;
	state->hs_reg_accattr.devacc_attr_endian_flags = DDI_STRUCTURE_BE_ACC;
	state->hs_reg_accattr.devacc_attr_dataorder = DDI_STRICTORDER_ACC;
	state->hs_reg_accattr.devacc_attr_access = DDI_DEFAULT_ACC;

	/* Setup fma-protected access attributes */
	state->hs_fm_accattr.devacc_attr_version =
	    mcxnex_devacc_attr_version(state);
	state->hs_fm_accattr.devacc_attr_endian_flags = DDI_STRUCTURE_BE_ACC;
	state->hs_fm_accattr.devacc_attr_dataorder = DDI_STRICTORDER_ACC;
	/* set acc err protection type */
	state->hs_fm_accattr.devacc_attr_access =
	    mcxnex_devacc_attr_access(state);

	/* Setup for PCI config read/write of HCA device */
	status = mcxnex_pci_config_setup(state, &state->hs_fm_pcihdl);
	if (status != DDI_SUCCESS) {
		mcxnex_hw_fini(state, cleanup);
		MCXNEX_ATTACH_MSG(state->hs_attach_buf,
		    "hw_init_PCI_config_space_regmap_fail");
		/* This case is not the degraded one */
		return (DDI_FAILURE);
	}

	/* Map PCI config space and MSI-X tables/pba */
	mcxnex_set_msix_info(state);

	/* Map in Mcxnex registers (CMD, UAR, MSIX) and setup offsets */
	status = mcxnex_regs_map_setup(state, MCXNEX_CMD_BAR,
	    &state->hs_reg_cmd_baseaddr, 0, 0, &state->hs_fm_accattr,
	    &state->hs_fm_cmdhdl);
	if (status != DDI_SUCCESS) {
		mcxnex_hw_fini(state, cleanup);
		MCXNEX_ATTACH_MSG(state->hs_attach_buf,
		    "hw_init_CMD_BAR_regmap_fail");
		/* This case is not the degraded one */
		return (DDI_FAILURE);
	}

	cleanup = MCXNEX_DRV_CLEANUP_LEVEL1;
	/*
	 * We defer UAR-BAR mapping until later.  Need to know if
	 * blueflame mapping is to be done, and don't know that until after
	 * we get the dev_caps, so do it right after that
	 */

	/*
	 * There is a third BAR defined for Mcxnex - it is for MSIX
	 *
	 * Will need to explore it's possible need/use w/ Mellanox
	 * [es] Temporary mapping maybe
	 */

#ifdef MCXNEX_SUPPORTS_MSIX_BAR
	status = ddi_regs_map_setup(state->hs_dip, MCXNEX_MSIX_BAR,
	    &state->hs_reg_msi_baseaddr, 0, 0, &state->hs_reg_accattr,
	    &state->hs_reg_msihdl);
	if (status != DDI_SUCCESS) {
		mcxnex_hw_fini(state, cleanup);
		MCXNEX_ATTACH_MSG(state->hs_attach_buf,
		    "hw_init_MSIX_BAR_regmap_fail");
		/* This case is not the degraded one */
		return (DDI_FAILURE);
	}
#endif

	cleanup = MCXNEX_DRV_CLEANUP_LEVEL2;

	/*
	 * Save interesting registers away. The offsets of the first two
	 * here (HCR and sw_reset) are detailed in the PRM, the others are
	 * derived from values in the QUERY_FW output, so we'll save them
	 * off later.
	 */
	/* Host Command Register (HCR) */
	state->hs_cmd_regs.hcr = (mcxnex_hw_hcr_t *)
	    ((uintptr_t)state->hs_reg_cmd_baseaddr + MCXNEX_CMD_HCR_OFFSET);
	state->hs_cmd_toggle = 0;	/* initialize it for use */

	/* Software Reset register (sw_reset) and semaphore */
	state->hs_cmd_regs.sw_reset = (uint32_t *)
	    ((uintptr_t)state->hs_reg_cmd_baseaddr +
	    MCXNEX_CMD_SW_RESET_OFFSET);
	state->hs_cmd_regs.sw_semaphore = (uint32_t *)
	    ((uintptr_t)state->hs_reg_cmd_baseaddr +
	    MCXNEX_CMD_SW_SEMAPHORE_OFFSET);

	/* Retrieve PCI device, vendor and rev IDs */
	state->hs_vendor_id	 = MCXNEX_GET_VENDOR_ID(state->hs_dip);
	state->hs_device_id	 = MCXNEX_GET_DEVICE_ID(state->hs_dip);
	state->hs_revision_id	 = MCXNEX_GET_REVISION_ID(state->hs_dip);

	/* make sure init'd before we start filling things in */
	bzero(&state->hs_hcaparams, sizeof (struct mcxnex_hw_initqueryhca_s));

	/* Initialize the Phase1 configuration profile */
	status = mcxnex_cfg_profile_init_phase1(state);
	if (status != DDI_SUCCESS) {
		mcxnex_hw_fini(state, cleanup);
		MCXNEX_ATTACH_MSG(state->hs_attach_buf,
		    "hw_init_cfginit1_fail");
		/* This case is not the degraded one */
		return (DDI_FAILURE);
	}
	cleanup = MCXNEX_DRV_CLEANUP_LEVEL3;

	/* Do a software reset of the adapter to ensure proper state */
	status = mcxnex_sw_reset(state);
	if (status != MCXNEX_CMD_SUCCESS) {
		mcxnex_hw_fini(state, cleanup);
		MCXNEX_ATTACH_MSG(state->hs_attach_buf,
		    "hw_init_sw_reset_fail");
		/* This case is not the degraded one */
		return (DDI_FAILURE);
	}

	/* Initialize mailboxes */
	status = mcxnex_rsrc_init_phase1(state);
	if (status != DDI_SUCCESS) {
		mcxnex_hw_fini(state, cleanup);
		MCXNEX_ATTACH_MSG(state->hs_attach_buf,
		    "hw_init_rsrcinit1_fail");
		/* This case is not the degraded one */
		return (DDI_FAILURE);
	}
	cleanup = MCXNEX_DRV_CLEANUP_LEVEL4;

	/* Post QUERY_FW */
	status = mcxnex_cmn_query_cmd_post(state, QUERY_FW, 0, 0, &state->hs_fw,
	    sizeof (state->hs_fw), DDI_NOSLEEP);
	if (status != MCXNEX_CMD_SUCCESS) {
		MCXNEX_WARNING(state, "QUERY_FW failed: %x", status);
		mcxnex_hw_fini(state, cleanup);
		MCXNEX_ATTACH_MSG(state->hs_attach_buf,
		    "hw_init_query_fw_cmd_fail");
		/* This case is not the degraded one */
		return (DDI_FAILURE);
	}

	/* Validate what/that MCXNEX FW version is appropriate */

	status = mcxnex_fw_version_check(state);
	if (status != DDI_SUCCESS) {
		MCXNEX_FMANOTE(state, MCXNEX_FMA_FWVER);
		if (state->hs_operational_mode == MCXNEX_HCA_MODE) {
			MCXNEX_NOTE(state, "Unsupported firmware version: "
			    "expected: %04d.%04d.%04d, "
			    "actual: %04d.%04d.%04d",
			    MCXNEX_FW_VER_MAJOR,
			    MCXNEX_FW_VER_MINOR,
			    MCXNEX_FW_VER_SUBMINOR,
			    state->hs_fw.fw_rev_major,
			    state->hs_fw.fw_rev_minor,
			    state->hs_fw.fw_rev_subminor);
		} else {
			MCXNEX_NOTE(state, "Unsupported FW version: "
			    "%04d.%04d.%04d",
			    state->hs_fw.fw_rev_major,
			    state->hs_fw.fw_rev_minor,
			    state->hs_fw.fw_rev_subminor);
		}
		state->hs_operational_mode = MCXNEX_MAINTENANCE_MODE;
		state->hs_fm_degraded_reason = HCA_FW_MISMATCH;
		mcxnex_hw_fini(state, cleanup);
		MCXNEX_ATTACH_MSG(state->hs_attach_buf,
		    "hw_init_checkfwver_fail");
		/* This case is the degraded one */
		return (MCXNEX_CMD_BAD_NVMEM);
	}

	if (MCXNEX_FW_VER_NUM(state) > MCXNEX_FW_VER_VLAN_STRIP_CAP)
		state->hs_vlan_strip_off_cap = 1;

	/*
	 * Save off the rest of the interesting registers that we'll be using.
	 * Setup the offsets for the other registers.
	 */

	/*
	 * Mcxnex does the intr_offset from the BAR - technically should get the
	 * BAR info from the response, but PRM says it's from BAR0-1, which is
	 * for us the CMD BAR
	 */

	clr_intr_offset	 = state->hs_fw.clr_intr_offs & MCXNEX_CMD_OFFSET_MASK;

	/* Save Clear Interrupt address */
	state->hs_cmd_regs.clr_intr = (uint64_t *)
	    (uintptr_t)(state->hs_reg_cmd_baseaddr + clr_intr_offset);

	/*
	 * Set the error buffer also into the structure - used in mcxnex_event.c
	 * to check for internal error on the HCA, not reported in eqe or
	 * (necessarily) by interrupt
	 */
	state->hs_cmd_regs.fw_err_buf = (uint32_t *)(uintptr_t)
	    (state->hs_reg_cmd_baseaddr + state->hs_fw.error_buf_addr);

	/*
	 * Invoke a polling thread to check the error buffer periodically.
	 */
	if (!mcxnex_no_inter_err_chk) {
		state->hs_fm_poll_thread = ddi_periodic_add(
		    mcxnex_inter_err_chk, (void *)state, FM_POLL_INTERVAL,
		    DDI_IPL_0);
	}

	cleanup = MCXNEX_DRV_CLEANUP_LEVEL5;

	/*
	 * Allocate, map, and run the HCA Firmware.
	 */

	/* Allocate memory for the firmware to load into and map it */

	/* get next higher power of 2 */
	fw_size = 1 << highbit(state->hs_fw.fw_pages);
	state->hs_fw_dma.length = fw_size << MCXNEX_PAGESHIFT;
	status = mcxnex_dma_alloc(state, &state->hs_fw_dma, MAP_FA);
	if (status != DDI_SUCCESS) {
		MCXNEX_WARNING(state, "FW alloc failed");
		mcxnex_hw_fini(state, cleanup);
		MCXNEX_ATTACH_MSG(state->hs_attach_buf,
		    "hw_init_dma_alloc_fw_fail");
		/* This case is not the degraded one */
		return (DDI_FAILURE);
	}

	cleanup = MCXNEX_DRV_CLEANUP_LEVEL6;

	/* Invoke the RUN_FW cmd to run the firmware */
	status = mcxnex_run_fw_cmd_post(state);
	if (status != DDI_SUCCESS) {
		MCXNEX_WARNING(state, "RUN_FW command failed: %x", status);
		if (status == MCXNEX_CMD_BAD_NVMEM) {
			state->hs_operational_mode = MCXNEX_MAINTENANCE_MODE;
			state->hs_fm_degraded_reason = HCA_FW_CORRUPT;
		}
		mcxnex_hw_fini(state, cleanup);
		MCXNEX_ATTACH_MSG(state->hs_attach_buf, "hw_init_run_fw_fail");
		/*
		 * If the status is MCXNEX_CMD_BAD_NVMEM, it's likely the
		 * firmware is corrupted, so the mode falls into the
		 * maintenance mode.
		 */
		return (status == MCXNEX_CMD_BAD_NVMEM ? MCXNEX_CMD_BAD_NVMEM :
		    DDI_FAILURE);
	}


	/*
	 * QUERY DEVICE LIMITS/CAPABILITIES
	 * NOTE - in Mcxnex, the command is changed to QUERY_DEV_CAP,
	 * but for familiarity we have kept the structure name the
	 * same as Tavor/Arbel
	 */

	status = mcxnex_cmn_query_cmd_post(state, QUERY_DEV_CAP, 0, 0,
	    &state->hs_devlim, sizeof (state->hs_devlim), DDI_NOSLEEP);
	if (status != MCXNEX_CMD_SUCCESS) {
		MCXNEX_WARNING(state, "QUERY_DEV_CAP failed: %x", status);
		mcxnex_hw_fini(state, cleanup);
		MCXNEX_ATTACH_MSG(state->hs_attach_buf, "hw_init_devcap_fail");
		/* This case is not the degraded one */
		return (DDI_FAILURE);
	}

	state->hs_devlim.num_rsvd_eq = max(state->hs_devlim.num_rsvd_eq,
	    (4 * state->hs_devlim.num_rsvd_uar));	/* lesser of resvd's */

	/* now we have enough info to map in the UAR BAR */
	/*
	 * First, we figure out how to map the BAR for UAR - use only half if
	 * BlueFlame is enabled - in that case the mapped length is 1/2 the
	 * log_max_uar_sz (max__uar - 1) * 1MB ( +20).
	 */

	if (state->hs_devlim.blu_flm) {		/* Blue Flame Enabled */
		offset = (uint64_t)1 << (state->hs_devlim.log_max_uar_sz + 20);
	} else {
		offset = 0;	/* a zero length means map the whole thing */
	}
	status = mcxnex_regs_map_setup(state, MCXNEX_UAR_BAR,
	    &state->hs_reg_uar_baseaddr, 0, offset, &state->hs_fm_accattr,
	    &state->hs_fm_uarhdl);
	if (status != DDI_SUCCESS) {
		MCXNEX_ATTACH_MSG(state->hs_attach_buf, "UAR BAR mapping");
		/* This case is not the degraded one */
		return (DDI_FAILURE);
	}

	/* and if BlueFlame is enabled, map the other half there */
	if (state->hs_devlim.blu_flm) {		/* Blue Flame Enabled */
		offset = (uint64_t)1 << (state->hs_devlim.log_max_uar_sz + 20);
		status = ddi_regs_map_setup(state->hs_dip, MCXNEX_UAR_BAR,
		    &state->hs_reg_bf_baseaddr, offset, offset,
		    &state->hs_reg_accattr, &state->hs_reg_bfhdl);
		if (status != DDI_SUCCESS) {
			MCXNEX_ATTACH_MSG(state->hs_attach_buf,
			    "BlueFlame BAR mapping");
			/* This case is not the degraded one */
			return (DDI_FAILURE);
		}
		/* This will be used in hw_fini if we fail to init. */
		state->hs_bf_offset = offset;
	}
	cleanup = MCXNEX_DRV_CLEANUP_LEVEL7;

	/* Mcxnex has a couple of things needed for phase 2 in query port */

	status = mcxnex_cmn_query_cmd_post(state, QUERY_PORT, 0, 0x01,
	    &state->hs_queryport, sizeof (state->hs_queryport), DDI_NOSLEEP);
	if (status != MCXNEX_CMD_SUCCESS) {
		MCXNEX_WARNING(state, "QUERY_PORT failed: %x", status);
		mcxnex_hw_fini(state, cleanup);
		MCXNEX_ATTACH_MSG(state->hs_attach_buf,
		    "hw_init_queryport_fail");
		/* This case is not the degraded one */
		return (DDI_FAILURE);
	}

	/* Initialize the Phase2 Mcxnex configuration profile */
	status = mcxnex_cfg_profile_init_phase2(state);
	if (status != DDI_SUCCESS) {
		MCXNEX_WARNING(state, "CFG phase 2 failed: %x", status);
		mcxnex_hw_fini(state, cleanup);
		MCXNEX_ATTACH_MSG(state->hs_attach_buf,
		    "hw_init_cfginit2_fail");
		/* This case is not the degraded one */
		return (DDI_FAILURE);
	}

	/* Determine and set the ICM size */
	state->hs_icm_sz = mcxnex_size_icm(state);
	status		 = mcxnex_set_icm_size_cmd_post(state);
	if (status != DDI_SUCCESS) {
		MCXNEX_WARNING(state, "SET_ICM_SIZE failed: %x", status);
		mcxnex_hw_fini(state, cleanup);
		MCXNEX_ATTACH_MSG(state->hs_attach_buf,
		    "hw_init_seticmsz_fail");
		/* This case is not the degraded one */
		return (DDI_FAILURE);
	}
	/* alloc icm aux physical memory and map it */

	state->hs_icma_dma.length = 1 << highbit(state->hs_icma_sz);

	status = mcxnex_dma_alloc(state, &state->hs_icma_dma, MAP_ICM_AUX);
	if (status != DDI_SUCCESS) {
		MCXNEX_WARNING(state, "failed to alloc (0x%llx) bytes for ICMA",
		    (longlong_t)state->hs_icma_dma.length);
		mcxnex_hw_fini(state, cleanup);
		MCXNEX_ATTACH_MSG(state->hs_attach_buf,
		    "hw_init_dma_alloc_icm_aux_fail");
		/* This case is not the degraded one */
		return (DDI_FAILURE);
	}
	cleanup = MCXNEX_DRV_CLEANUP_LEVEL8;

	cleanup = MCXNEX_DRV_CLEANUP_LEVEL9;

	/* Allocate an array of structures to house the ICM tables */
	state->hs_icm = kmem_zalloc(MCXNEX_NUM_ICM_RESOURCES *
	    sizeof (mcxnex_icm_table_t), KM_SLEEP);

	/* Set up the ICM address space and the INIT_HCA command input */
	status = mcxnex_icm_config_setup(state, &state->hs_hcaparams);
	if (status != MCXNEX_CMD_SUCCESS) {
		MCXNEX_WARNING(state, "ICM configuration failed, %x", status);
		mcxnex_hw_fini(state, cleanup);
		MCXNEX_ATTACH_MSG(state->hs_attach_buf,
		    "hw_init_icm_config_setup_fail");
		/* This case is not the degraded one */
		return (DDI_FAILURE);
	}
	cleanup = MCXNEX_DRV_CLEANUP_LEVEL10;

	/* Initialize the adapter with the INIT_HCA cmd */
	status = mcxnex_init_hca_cmd_post(state, &state->hs_hcaparams,
	    DDI_NOSLEEP);
	if (status != MCXNEX_CMD_SUCCESS) {
		MCXNEX_WARNING(state, "INIT_HCA failed: %x", status);
		mcxnex_hw_fini(state, cleanup);
		MCXNEX_ATTACH_MSG(state->hs_attach_buf, "hw_init_hca_fail");
		/* This case is not the degraded one */
		return (DDI_FAILURE);
	}
	cleanup = MCXNEX_DRV_CLEANUP_LEVEL11;

	/* Enter the second phase of init for Mcxnex configuration/resources */
	status = mcxnex_rsrc_init_phase2(state);
	if (status != DDI_SUCCESS) {
		mcxnex_hw_fini(state, cleanup);
		MCXNEX_ATTACH_MSG(state->hs_attach_buf,
		    "hw_init_rsrcinit2_fail");
		/* This case is not the degraded one */
		return (DDI_FAILURE);
	}
	cleanup = MCXNEX_DRV_CLEANUP_LEVEL12;

	/* Query the adapter via QUERY_ADAPTER */
	status = mcxnex_cmn_query_cmd_post(state, QUERY_ADAPTER, 0, 0,
	    &state->hs_adapter, sizeof (state->hs_adapter), DDI_NOSLEEP);
	if (status != MCXNEX_CMD_SUCCESS) {
		MCXNEX_WARNING(state, "QUERY_ADAPTER failed: %x", status);
		mcxnex_hw_fini(state, cleanup);
		MCXNEX_ATTACH_MSG(state->hs_attach_buf,
		    "hw_init_query_adapter_fail");
		/* This case is not the degraded one */
		return (DDI_FAILURE);
	}

	/* Allocate protection domain (PD) for Mcxnex internal use */
	status = mcxnex_pd_alloc(state, &state->hs_pdhdl_internal, DDI_SLEEP);
	if (status != DDI_SUCCESS) {
		MCXNEX_WARNING(state,
		    "failed to alloc internal PD: %x", status);
		mcxnex_hw_fini(state, cleanup);
		MCXNEX_ATTACH_MSG(state->hs_attach_buf,
		    "hw_init_internal_pd_alloc_fail");
		/* This case is not the degraded one */
		return (DDI_FAILURE);
	}
	cleanup = MCXNEX_DRV_CLEANUP_LEVEL13;

	/* Setup UAR page for kernel use */
	status = mcxnex_internal_uarpg_init(state);
	if (status != DDI_SUCCESS) {
		MCXNEX_WARNING(state,
		    "failed to setup internal UAR: %x", status);
		mcxnex_hw_fini(state, cleanup);
		MCXNEX_ATTACH_MSG(state->hs_attach_buf,
		    "hw_init_internal_uarpg_alloc_fail");
		/* This case is not the degraded one */
		return (DDI_FAILURE);
	}
	cleanup = MCXNEX_DRV_CLEANUP_LEVEL14;

	/* Query and initialize the Mcxnex interrupt/MSI information */
	status = mcxnex_intr_or_msi_init(state);
	if (status != DDI_SUCCESS) {
		MCXNEX_WARNING(state, "failed to setup INTR/MSI: %x", status);
		mcxnex_hw_fini(state, cleanup);
		MCXNEX_ATTACH_MSG(state->hs_attach_buf,
		    "hw_init_intr_or_msi_init_fail");
		/* This case is not the degraded one */
		return (DDI_FAILURE);
	}
	cleanup = MCXNEX_DRV_CLEANUP_LEVEL15;

	status = mcxnex_isr_init(state);	/* set up the isr */
	if (status != DDI_SUCCESS) {
		MCXNEX_WARNING(state, "failed to init isr: %x", status);
		mcxnex_hw_fini(state, cleanup);
		MCXNEX_ATTACH_MSG(state->hs_attach_buf,
		    "hw_init_isrinit_fail");
		/* This case is not the degraded one */
		return (DDI_FAILURE);
	}
	cleanup = MCXNEX_DRV_CLEANUP_LEVEL16;

	/* Setup the event queues */
	status = mcxnex_eq_init_all(state);
	if (status != DDI_SUCCESS) {
		MCXNEX_WARNING(state, "failed to init EQs: %x", status);
		mcxnex_hw_fini(state, cleanup);
		MCXNEX_ATTACH_MSG(state->hs_attach_buf,
		    "hw_init_eqinitall_fail");
		/* This case is not the degraded one */
		return (DDI_FAILURE);
	}
	cleanup = MCXNEX_DRV_CLEANUP_LEVEL17;



	/* Reserve contexts for QP0 and QP1 */
	status = mcxnex_special_qp_contexts_reserve(state);
	if (status != DDI_SUCCESS) {
		MCXNEX_WARNING(state, "failed to init special QPs: %x", status);
		mcxnex_hw_fini(state, cleanup);
		MCXNEX_ATTACH_MSG(state->hs_attach_buf,
		    "hw_init_rsrv_sqp_fail");
		/* This case is not the degraded one */
		return (DDI_FAILURE);
	}
	cleanup = MCXNEX_DRV_CLEANUP_LEVEL18;

	/* Initialize for multicast group handling */
	status = mcxnex_mcg_init(state);
	if (status != DDI_SUCCESS) {
		MCXNEX_WARNING(state, "failed to init multicast: %x", status);
		mcxnex_hw_fini(state, cleanup);
		MCXNEX_ATTACH_MSG(state->hs_attach_buf,
		    "hw_init_mcg_init_fail");
		/* This case is not the degraded one */
		return (DDI_FAILURE);
	}
	cleanup = MCXNEX_DRV_CLEANUP_LEVEL19;

	/* Determine NodeGUID and SystemImageGUID */
	status = mcxnex_getnodeinfo_cmd_post(state, DDI_NOSLEEP, &nodeinfo);
	if (status != MCXNEX_CMD_SUCCESS) {
		MCXNEX_WARNING(state, "GetNodeInfo failed: %x", status);
		mcxnex_hw_fini(state, cleanup);
		MCXNEX_ATTACH_MSG(state->hs_attach_buf,
		    "hw_init_hca_port_init_fail");
		/* This case is not the degraded one */
		return (DDI_FAILURE);
	}

	/* Get NodeDescription */
	status = mcxnex_getnodedesc_cmd_post(state, DDI_NOSLEEP,
	    (sm_nodedesc_t *)&state->hs_nodedesc);
	if (status != MCXNEX_CMD_SUCCESS) {
		MCXNEX_WARNING(state, "GetNodeDesc failed: %x", status);
		mcxnex_hw_fini(state, cleanup);
		MCXNEX_ATTACH_MSG(state->hs_attach_buf,
		    "hw_init_getnodedesc_cmd_fail");
		/* This case is not the degraded one */
		return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);
}


/*
 * mcxnex_hw_fini()
 *    Context: Only called from attach() and/or detach() path contexts
 */
static void
mcxnex_hw_fini(mcxnex_state_t *state, mcxnex_drv_cleanup_level_t cleanup)
{
	uint_t		num_ports;
	int		i, status;


	/*
	 * JBDB - We might not want to run these returns in all cases of
	 * Bad News. We should still attempt to free all of the DMA memory
	 * resources...  This needs to be worked last, after all allocations
	 * are implemented. For now, and possibly for later, this works.
	 */

	switch (cleanup) {
	/*
	 * If we add more driver initialization steps that should be cleaned
	 * up here, we need to ensure that MCXNEX_DRV_CLEANUP_ALL is still the
	 * first entry (i.e. corresponds to the last init step).
	 */
	case MCXNEX_DRV_CLEANUP_ALL:
		/* Shutdown the Mcxnex IB port(s) */
		num_ports = state->hs_cfg_profile->cp_num_ports;
		(void) mcxnex_hca_ports_shutdown(state, num_ports);
		/* FALLTHROUGH */

	case MCXNEX_DRV_CLEANUP_LEVEL19:
		/* Teardown resources used for multicast group handling */
		mcxnex_mcg_fini(state);
		/* FALLTHROUGH */

	case MCXNEX_DRV_CLEANUP_LEVEL18:
		/* Unreserve the special QP contexts */
		mcxnex_special_qp_contexts_unreserve(state);
		/* FALLTHROUGH */

	case MCXNEX_DRV_CLEANUP_LEVEL17:
		/*
		 * Attempt to teardown all event queues (EQ).  If we fail
		 * here then print a warning message and return.  Something
		 * (either in HW or SW) has gone seriously wrong.
		 */
		status = mcxnex_eq_fini_all(state);
		if (status != DDI_SUCCESS) {
			MCXNEX_WARNING(state, "failed to teardown EQs");
			return;
		}
		/* FALLTHROUGH */
	case MCXNEX_DRV_CLEANUP_LEVEL16:
		/* Teardown Mcxnex interrupts */
		mcxnex_isr_fini(state);
		/* FALLTHROUGH */

	case MCXNEX_DRV_CLEANUP_LEVEL15:
		status = mcxnex_intr_or_msi_fini(state);
		if (status != DDI_SUCCESS) {
			MCXNEX_WARNING(state, "failed to free intr/MSI");
			return;
		}
		/* FALLTHROUGH */

	case MCXNEX_DRV_CLEANUP_LEVEL14:
		/* Free the resources for the Mcxnex internal UAR pages */
		mcxnex_internal_uarpg_fini(state);
		/* FALLTHROUGH */

	case MCXNEX_DRV_CLEANUP_LEVEL13:
		/*
		 * Free the PD that was used internally by Mcxnex software.  If
		 * we fail here then print a warning and return.  Something
		 * (probably software-related, but perhaps HW) has gone wrong.
		 */
		status = mcxnex_pd_free(state, &state->hs_pdhdl_internal);
		if (status != DDI_SUCCESS) {
			MCXNEX_WARNING(state, "failed to free internal PD");
			return;
		}
		/* FALLTHROUGH */

	case MCXNEX_DRV_CLEANUP_LEVEL12:
		/* Cleanup all the phase2 resources first */
		mcxnex_rsrc_fini(state, MCXNEX_RSRC_CLEANUP_ALL);
		/* FALLTHROUGH */

	case MCXNEX_DRV_CLEANUP_LEVEL11:
		/* LEVEL11 is after INIT_HCA */
		/* FALLTHROUGH */


	case MCXNEX_DRV_CLEANUP_LEVEL10:
		/*
		 * Unmap the ICM memory area with UNMAP_ICM command.
		 */
		status = mcxnex_unmap_icm_cmd_post(state, NULL);
		if (status != DDI_SUCCESS) {
			MCXNEX_WARNING(state, "failed to unmap ICM");
		}

		/* Free the initial ICM DMA handles */
		mcxnex_icm_dma_fini(state);

		/* Free the ICM table structures */
		mcxnex_icm_tables_fini(state);

		/* Free the ICM table handles */
		kmem_free(state->hs_icm, MCXNEX_NUM_ICM_RESOURCES *
		    sizeof (mcxnex_icm_table_t));

		/* FALLTHROUGH */

	case MCXNEX_DRV_CLEANUP_LEVEL9:
		/*
		 * Unmap the ICM Aux memory area with UNMAP_ICM_AUX command.
		 */
		status = mcxnex_unmap_icm_aux_cmd_post(state);
		if (status != MCXNEX_CMD_SUCCESS) {
			MCXNEX_WARNING(state, "failed to unmap ICMA");
		}
		/* FALLTHROUGH */

	case MCXNEX_DRV_CLEANUP_LEVEL8:
		/*
		 * Deallocate ICM Aux DMA memory.
		 */
		mcxnex_dma_free(&state->hs_icma_dma);
		/* FALLTHROUGH */

	case MCXNEX_DRV_CLEANUP_LEVEL7:
		if (state->hs_fm_uarhdl) {
			mcxnex_regs_map_free(state, &state->hs_fm_uarhdl);
			state->hs_fm_uarhdl = NULL;
		}

		if (state->hs_reg_uarhdl) {
			ddi_regs_map_free(&state->hs_reg_uarhdl);
			state->hs_reg_uarhdl = NULL;
		}

		if (state->hs_bf_offset != 0 && state->hs_reg_bfhdl) {
			ddi_regs_map_free(&state->hs_reg_bfhdl);
			state->hs_reg_bfhdl = NULL;
		}

		for (i = 0; i < MCXNEX_MAX_PORTS; i++) {
			if (state->hs_pkey[i]) {
				kmem_free(state->hs_pkey[i], (1 <<
				    state->hs_cfg_profile->cp_log_max_pkeytbl) *
				    sizeof (ib_pkey_t));
				state->hs_pkey[i] = NULL;
			}
			if (state->hs_guid[i]) {
				kmem_free(state->hs_guid[i], (1 <<
				    state->hs_cfg_profile->cp_log_max_gidtbl) *
				    sizeof (ib_guid_t));
				state->hs_guid[i] = NULL;
			}
		}
		/* FALLTHROUGH */

	case MCXNEX_DRV_CLEANUP_LEVEL6:
		/*
		 * Unmap the firmware memory area with UNMAP_FA command.
		 */
		status = mcxnex_unmap_fa_cmd_post(state);

		if (status != MCXNEX_CMD_SUCCESS) {
			MCXNEX_WARNING(state, "failed to unmap FW");
		}

		/*
		 * Deallocate firmware DMA memory.
		 */
		mcxnex_dma_free(&state->hs_fw_dma);
		/* FALLTHROUGH */

	case MCXNEX_DRV_CLEANUP_LEVEL5:
		/* stop the poll thread */
		if (state->hs_fm_poll_thread) {
			ddi_periodic_delete(state->hs_fm_poll_thread);
			state->hs_fm_poll_thread = NULL;
		}
		/* FALLTHROUGH */

	case MCXNEX_DRV_CLEANUP_LEVEL4:
		/* Then cleanup the phase1 resources */
		mcxnex_rsrc_fini(state, MCXNEX_RSRC_CLEANUP_PHASE1_COMPLETE);
		/* FALLTHROUGH */

	case MCXNEX_DRV_CLEANUP_LEVEL3:
		/* Teardown any resources allocated for the config profile */
		mcxnex_cfg_profile_fini(state);
		/* FALLTHROUGH */

	case MCXNEX_DRV_CLEANUP_LEVEL2:
#ifdef MCXNEX_SUPPORTS_MSIX_BAR
		/*
		 * unmap 3rd BAR, MSIX BAR
		 */
		if (state->hs_reg_msihdl) {
			ddi_regs_map_free(&state->hs_reg_msihdl);
			state->hs_reg_msihdl = NULL;
		}
		/* FALLTHROUGH */
#endif
	case MCXNEX_DRV_CLEANUP_LEVEL1:
	case MCXNEX_DRV_CLEANUP_LEVEL0:
		/*
		 * LEVEL1 and LEVEL0 resources are freed in
		 * mcxnex_drv_fini2().
		 */
		break;

	default:
		MCXNEX_WARNING(state, "unexpected driver cleanup level");
		return;
	}
}


/*
 * mcxnex_soft_state_init()
 *    Context: Only called from attach() path context
 */
static int
mcxnex_soft_state_init(mcxnex_state_t *state)
{
	ibt_hca_attr_t		*hca_attr;
	uint64_t		maxval, val;
	ibt_hca_flags_t		caps = IBT_HCA_NO_FLAGS;
	ibt_hca_flags2_t	caps2 = IBT_HCA2_NO_FLAGS;
	int			status;
	int			max_send_wqe_bytes;
	int			max_recv_wqe_bytes;

	/*
	 * The ibc_hca_info_t struct is passed to the IBTF.  This is the
	 * routine where we initialize it.  Many of the init values come from
	 * either configuration variables or successful queries of the Mcxnex
	 * hardware abilities
	 */
	state->hs_ibtfinfo.hca_ci_vers	= IBCI_V3;
	state->hs_ibtfinfo.hca_handle	= (ibc_hca_hdl_t)state;

	hca_attr = kmem_zalloc(sizeof (ibt_hca_attr_t), KM_SLEEP);
	state->hs_ibtfinfo.hca_attr = hca_attr;

	/* hca_attr->hca_dip = state->hs_dip; */
	hca_attr->hca_fw_major_version = state->hs_fw.fw_rev_major;
	hca_attr->hca_fw_minor_version = state->hs_fw.fw_rev_minor;
	hca_attr->hca_fw_micro_version = state->hs_fw.fw_rev_subminor;

	/* CQ interrupt moderation maximums - each limited to 16 bits */
	hca_attr->hca_max_cq_mod_count = 0xFFFF;
	hca_attr->hca_max_cq_mod_usec = 0xFFFF;

	/* CQ relocation to other EQs - change when multiple MSI-Xs are used */
	hca_attr->hca_max_cq_handlers = 1;

	/*
	 * Determine HCA capabilities:
	 * No default support for IBT_HCA_RD, IBT_HCA_RAW_MULTICAST,
	 *    IBT_HCA_ATOMICS_GLOBAL, IBT_HCA_RESIZE_CHAN, IBT_HCA_INIT_TYPE,
	 *    or IBT_HCA_SHUTDOWN_PORT
	 * But IBT_HCA_AH_PORT_CHECK, IBT_HCA_SQD_RTS_PORT, IBT_HCA_SI_GUID,
	 *    IBT_HCA_RNR_NAK, IBT_HCA_CURRENT_QP_STATE, IBT_HCA_PORT_UP,
	 *    IBT_HCA_SRQ, IBT_HCA_RESIZE_SRQ and IBT_HCA_FMR are always
	 *    supported
	 * All other features are conditionally supported, depending on the
	 *    status return by the Mcxnex HCA in QUERY_DEV_LIM.
	 */
	if (state->hs_devlim.ud_multi) {
		caps |= IBT_HCA_UD_MULTICAST;
	}
	if (state->hs_devlim.atomic) {
		caps |= IBT_HCA_ATOMICS_HCA;
	}
	if (state->hs_devlim.apm) {
		caps |= IBT_HCA_AUTO_PATH_MIG;
	}
	if (state->hs_devlim.pkey_v) {
		caps |= IBT_HCA_PKEY_CNTR;
	}
	if (state->hs_devlim.qkey_v) {
		caps |= IBT_HCA_QKEY_CNTR;
	}
	if (state->hs_devlim.ipoib_cksm) {
		caps |= IBT_HCA_CKSUM_FULL;
		caps2 |= IBT_HCA2_IP_CLASS;
	}
	if (state->hs_devlim.mod_wr_srq) {
		caps |= IBT_HCA_RESIZE_SRQ;
	}
	if (state->hs_devlim.lif) {
		caps |= IBT_HCA_LOCAL_INVAL_FENCE;
	}
	if (state->hs_devlim.reserved_lkey) {
		caps2 |= IBT_HCA2_RES_LKEY;
		hca_attr->hca_reserved_lkey = state->hs_devlim.rsv_lkey;
	}
	if (state->hs_devlim.local_inv && state->hs_devlim.remote_inv &&
	    state->hs_devlim.fast_reg_wr) {	/* fw needs to be >= 2.6.636 */
		if (state->hs_fw.fw_rev_major > 2)
			caps2 |= IBT_HCA2_MEM_MGT_EXT;
		else if (state->hs_fw.fw_rev_major == 2)
			if (state->hs_fw.fw_rev_minor > 6)
				caps2 |= IBT_HCA2_MEM_MGT_EXT;
			else if (state->hs_fw.fw_rev_minor == 6)
				if (state->hs_fw.fw_rev_subminor >= 636)
					caps2 |= IBT_HCA2_MEM_MGT_EXT;
	}
	if (state->hs_devlim.mps) {
		caps |= IBT_HCA_ZERO_BASED_VA;
	}
	if (state->hs_devlim.zb) {
		caps |= IBT_HCA_MULT_PAGE_SZ_MR;
	}
	caps |= (IBT_HCA_AH_PORT_CHECK | IBT_HCA_SQD_SQD_PORT |
	    IBT_HCA_SI_GUID | IBT_HCA_RNR_NAK | IBT_HCA_CURRENT_QP_STATE |
	    IBT_HCA_PORT_UP | IBT_HCA_RC_SRQ | IBT_HCA_UD_SRQ | IBT_HCA_FMR);

	if (state->hs_devlim.log_max_gso_sz) {
		hca_attr->hca_max_lso_size =
		    (1 << state->hs_devlim.log_max_gso_sz);
		/* 64 = ctrl & datagram seg, 4 = LSO seg, 16 = 1 SGL */
		hca_attr->hca_max_lso_hdr_size =
		    state->hs_devlim.max_desc_sz_sq - (64 + 4 + 16);
	}

	caps |= IBT_HCA_WQE_SIZE_INFO;
	max_send_wqe_bytes = state->hs_devlim.max_desc_sz_sq;
	max_recv_wqe_bytes = state->hs_devlim.max_desc_sz_rq;
	hca_attr->hca_ud_send_sgl_sz = (max_send_wqe_bytes / 16) - 4;
	hca_attr->hca_conn_send_sgl_sz = (max_send_wqe_bytes / 16) - 1;
	hca_attr->hca_conn_rdma_sgl_overhead = 1;
	hca_attr->hca_recv_sgl_sz = max_recv_wqe_bytes / 16;

	/* We choose not to support "inline" unless it improves performance */
	hca_attr->hca_max_inline_size = 0;
	hca_attr->hca_ud_send_inline_sz = 0;
	hca_attr->hca_conn_send_inline_sz = 0;
	hca_attr->hca_conn_rdmaw_inline_overhead = 4;

	hca_attr->hca_flags = caps;
	hca_attr->hca_flags2 = caps2;

	/*
	 * Set hca_attr's IDs
	 */
	hca_attr->hca_vendor_id	 = state->hs_vendor_id;
	hca_attr->hca_device_id	 = state->hs_device_id;
	hca_attr->hca_version_id = state->hs_revision_id;

	/*
	 * Determine number of available QPs and max QP size.  Number of
	 * available QPs is determined by subtracting the number of
	 * "reserved QPs" (i.e. reserved for firmware use) from the
	 * total number configured.
	 */
	val = ((uint64_t)1 << state->hs_cfg_profile->cp_log_num_qp);
	hca_attr->hca_max_qp = val - ((uint64_t)1 <<
	    state->hs_devlim.log_rsvd_qp);
	maxval	= ((uint64_t)1 << state->hs_devlim.log_max_qp_sz);
	val	= ((uint64_t)1 << state->hs_cfg_profile->cp_log_max_qp_sz);
	if (val > maxval) {
		kmem_free(hca_attr, sizeof (ibt_hca_attr_t));
		MCXNEX_ATTACH_MSG(state->hs_attach_buf,
		    "soft_state_init_maxqpsz_toobig_fail");
		return (DDI_FAILURE);
	}
	/* we need to reduce this by the max space needed for headroom */
	hca_attr->hca_max_qp_sz = (uint_t)val - (MCXNEX_QP_OH_SIZE >>
	    MCXNEX_QP_WQE_LOG_MINIMUM) - 1;

	/*
	 * Determine max scatter-gather size in WQEs. The HCA has split
	 * the max sgl into rec'v Q and send Q values. Use the least.
	 *
	 * This is mainly useful for legacy clients.  Smart clients
	 * such as IPoIB will use the IBT_HCA_WQE_SIZE_INFO sgl info.
	 */
	if (state->hs_devlim.max_sg_rq <= state->hs_devlim.max_sg_sq) {
		maxval = state->hs_devlim.max_sg_rq;
	} else {
		maxval = state->hs_devlim.max_sg_sq;
	}
	val	= state->hs_cfg_profile->cp_wqe_max_sgl;
	if (val > maxval) {
		kmem_free(hca_attr, sizeof (ibt_hca_attr_t));
		MCXNEX_ATTACH_MSG(state->hs_attach_buf,
		    "soft_state_init_toomanysgl_fail");
		return (DDI_FAILURE);
	}
	/* If the rounded value for max SGL is too large, cap it */
	if (state->hs_cfg_profile->cp_wqe_real_max_sgl > maxval) {
		state->hs_cfg_profile->cp_wqe_real_max_sgl = (uint32_t)maxval;
		val = maxval;
	} else {
		val = state->hs_cfg_profile->cp_wqe_real_max_sgl;
	}

	hca_attr->hca_max_sgl	 = (uint_t)val;
	hca_attr->hca_max_rd_sgl = 0;	/* zero because RD is unsupported */

	/*
	 * Determine number of available CQs and max CQ size. Number of
	 * available CQs is determined by subtracting the number of
	 * "reserved CQs" (i.e. reserved for firmware use) from the
	 * total number configured.
	 */
	val = ((uint64_t)1 << state->hs_cfg_profile->cp_log_num_cq);
	hca_attr->hca_max_cq = val - ((uint64_t)1 <<
	    state->hs_devlim.log_rsvd_cq);
	maxval	= ((uint64_t)1 << state->hs_devlim.log_max_cq_sz);
	val	= ((uint64_t)1 << state->hs_cfg_profile->cp_log_max_cq_sz) - 1;
	if (val > maxval) {
		kmem_free(hca_attr, sizeof (ibt_hca_attr_t));
		MCXNEX_ATTACH_MSG(state->hs_attach_buf,
		    "soft_state_init_maxcqsz_toobig_fail");
		return (DDI_FAILURE);
	}
	hca_attr->hca_max_cq_sz = (uint_t)val;

	/*
	 * Determine number of available SRQs and max SRQ size. Number of
	 * available SRQs is determined by subtracting the number of
	 * "reserved SRQs" (i.e. reserved for firmware use) from the
	 * total number configured.
	 */
	val = ((uint64_t)1 << state->hs_cfg_profile->cp_log_num_srq);
	hca_attr->hca_max_srqs = val - ((uint64_t)1 <<
	    state->hs_devlim.log_rsvd_srq);
	maxval  = ((uint64_t)1 << state->hs_devlim.log_max_srq_sz);
	val	= ((uint64_t)1 << state->hs_cfg_profile->cp_log_max_srq_sz);

	if (val > maxval) {
		kmem_free(hca_attr, sizeof (ibt_hca_attr_t));
		MCXNEX_ATTACH_MSG(state->hs_attach_buf,
		    "soft_state_init_maxsrqsz_toobig_fail");
		return (DDI_FAILURE);
	}
	hca_attr->hca_max_srqs_sz = (uint_t)val;

	val	= hca_attr->hca_recv_sgl_sz - 1; /* SRQ has a list link */
	maxval	= state->hs_devlim.max_sg_rq - 1;
	if (val > maxval) {
		kmem_free(hca_attr, sizeof (ibt_hca_attr_t));
		MCXNEX_ATTACH_MSG(state->hs_attach_buf,
		    "soft_state_init_toomanysrqsgl_fail");
		return (DDI_FAILURE);
	}
	hca_attr->hca_max_srq_sgl = (uint_t)val;

	/*
	 * Determine supported HCA page sizes
	 * XXX
	 * For now we simply return the system pagesize as the only supported
	 * pagesize
	 */
	hca_attr->hca_page_sz = ((PAGESIZE == (1 << 13)) ? IBT_PAGE_8K :
	    IBT_PAGE_4K);

	/*
	 * Determine number of available MemReg, MemWin, and their max size.
	 * Number of available MRs and MWs is determined by subtracting
	 * the number of "reserved MPTs" (i.e. reserved for firmware use)
	 * from the total number configured for each.
	 */
	val = ((uint64_t)1 << state->hs_cfg_profile->cp_log_num_dmpt);
	hca_attr->hca_max_memr	  = val - ((uint64_t)1 <<
	    state->hs_devlim.log_rsvd_dmpt);
	hca_attr->hca_max_mem_win = state->hs_devlim.mem_win ? (val -
	    ((uint64_t)1 << state->hs_devlim.log_rsvd_dmpt)) : 0;
	maxval	= state->hs_devlim.log_max_mrw_sz;
	val	= state->hs_cfg_profile->cp_log_max_mrw_sz;
	if (val > maxval) {
		kmem_free(hca_attr, sizeof (ibt_hca_attr_t));
		MCXNEX_ATTACH_MSG(state->hs_attach_buf,
		    "soft_state_init_maxmrwsz_toobig_fail");
		return (DDI_FAILURE);
	}
	hca_attr->hca_max_memr_len = ((uint64_t)1 << val);

	/* Determine RDMA/Atomic properties */
	val = ((uint64_t)1 << state->hs_cfg_profile->cp_log_num_rdb);
	hca_attr->hca_max_rsc = (uint_t)val;
	val = state->hs_cfg_profile->cp_hca_max_rdma_in_qp;
	hca_attr->hca_max_rdma_in_qp  = (uint8_t)val;
	val = state->hs_cfg_profile->cp_hca_max_rdma_out_qp;
	hca_attr->hca_max_rdma_out_qp = (uint8_t)val;
	hca_attr->hca_max_rdma_in_ee  = 0;
	hca_attr->hca_max_rdma_out_ee = 0;

	/*
	 * Determine maximum number of raw IPv6 and Ether QPs.  Set to 0
	 * because neither type of raw QP is supported
	 */
	hca_attr->hca_max_ipv6_qp  = 0;
	hca_attr->hca_max_ether_qp = 0;

	/* Determine max number of MCGs and max QP-per-MCG */
	val = ((uint64_t)1 << state->hs_cfg_profile->cp_log_num_qp);
	hca_attr->hca_max_mcg_qps   = (uint_t)val;
	val = ((uint64_t)1 << state->hs_cfg_profile->cp_log_num_mcg);
	hca_attr->hca_max_mcg	    = (uint_t)val;
	val = state->hs_cfg_profile->cp_num_qp_per_mcg;
	hca_attr->hca_max_qp_per_mcg = (uint_t)val;

	/* Determine max number partitions (i.e. PKeys) */
	maxval	= ((uint64_t)state->hs_cfg_profile->cp_num_ports <<
	    state->hs_queryport.log_max_pkey);
	val	= ((uint64_t)state->hs_cfg_profile->cp_num_ports <<
	    state->hs_cfg_profile->cp_log_max_pkeytbl);

	if (val > maxval) {
		kmem_free(hca_attr, sizeof (ibt_hca_attr_t));
		MCXNEX_ATTACH_MSG(state->hs_attach_buf,
		    "soft_state_init_toomanypkey_fail");
		return (DDI_FAILURE);
	}
	hca_attr->hca_max_partitions = (uint16_t)val;

	/* Determine number of ports */
	maxval = state->hs_devlim.num_ports;
	val = state->hs_cfg_profile->cp_num_ports;
	if ((val > maxval) || (val == 0)) {
		kmem_free(hca_attr, sizeof (ibt_hca_attr_t));
		MCXNEX_ATTACH_MSG(state->hs_attach_buf,
		    "soft_state_init_toomanyports_fail");
		return (DDI_FAILURE);
	}
	hca_attr->hca_nports = (uint8_t)val;

	/* Copy NodeGUID and SystemImageGUID from softstate */
	hca_attr->hca_node_guid = state->hs_nodeguid;
	hca_attr->hca_si_guid	= state->hs_sysimgguid;

	/*
	 * Determine local ACK delay.  Use the value suggested by the Mcxnex
	 * hardware (from the QUERY_DEV_CAP command)
	 */
	hca_attr->hca_local_ack_delay = state->hs_devlim.ca_ack_delay;

	/* Determine max SGID table and PKey table sizes */
	val	= ((uint64_t)1 << state->hs_cfg_profile->cp_log_max_gidtbl);
	hca_attr->hca_max_port_sgid_tbl_sz = (uint_t)val;
	val	= ((uint64_t)1 << state->hs_cfg_profile->cp_log_max_pkeytbl);
	hca_attr->hca_max_port_pkey_tbl_sz = (uint16_t)val;

	/* Determine max number of PDs */
	maxval	= ((uint64_t)1 << state->hs_devlim.log_max_pd);
	val	= ((uint64_t)1 << state->hs_cfg_profile->cp_log_num_pd);
	if (val > maxval) {
		kmem_free(hca_attr, sizeof (ibt_hca_attr_t));
		MCXNEX_ATTACH_MSG(state->hs_attach_buf,
		    "soft_state_init_toomanypd_fail");
		return (DDI_FAILURE);
	}
	hca_attr->hca_max_pd = (uint_t)val;

	/* Determine max number of Address Handles (NOT IN ARBEL or MCXNEX) */
	hca_attr->hca_max_ah = 0;

	/* No RDDs or EECs (since Reliable Datagram is not supported) */
	hca_attr->hca_max_rdd = 0;
	hca_attr->hca_max_eec = 0;

	/* Initialize lock for reserved UAR page access */
	mutex_init(&state->hs_uar_lock, NULL, MUTEX_DRIVER,
	    DDI_INTR_PRI(state->hs_intrmsi_pri));

	/* Initialize the flash fields */
	state->hs_fw_flashstarted = 0;
	mutex_init(&state->hs_fw_flashlock, NULL, MUTEX_DRIVER,
	    DDI_INTR_PRI(state->hs_intrmsi_pri));

	/* Initialize the lock for the info ioctl */
	mutex_init(&state->hs_info_lock, NULL, MUTEX_DRIVER,
	    DDI_INTR_PRI(state->hs_intrmsi_pri));

	/* Initialize the AVL tree for QP number support */
	mcxnex_qpn_avl_init(state);

	/* Initialize the kstat info structure */
	status = mcxnex_kstat_init(state);
	if (status != DDI_SUCCESS) {
		mcxnex_qpn_avl_fini(state);
		mutex_destroy(&state->hs_info_lock);
		mutex_destroy(&state->hs_fw_flashlock);
		mutex_destroy(&state->hs_uar_lock);
		kmem_free(hca_attr, sizeof (ibt_hca_attr_t));
		MCXNEX_ATTACH_MSG(state->hs_attach_buf,
		    "soft_state_init_kstatinit_fail");
		return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);
}


/*
 * mcxnex_soft_state_fini()
 *    Context: Called only from detach() path context
 */
static void
mcxnex_soft_state_fini(mcxnex_state_t *state)
{

	/* Teardown the kstat info */
	mcxnex_kstat_fini(state);

	/* Teardown the AVL tree for QP number support */
	mcxnex_qpn_avl_fini(state);

	/* Free up info ioctl mutex */
	mutex_destroy(&state->hs_info_lock);

	/* Free up flash mutex */
	mutex_destroy(&state->hs_fw_flashlock);

	/* Free up the UAR page access mutex */
	mutex_destroy(&state->hs_uar_lock);

	/* Free up the hca_attr struct */
	kmem_free(state->hs_ibtfinfo.hca_attr, sizeof (ibt_hca_attr_t));

}

/*
 * mcxnex_icm_config_setup()
 *    Context: Only called from attach() path context
 */
static int
mcxnex_icm_config_setup(mcxnex_state_t *state,
    mcxnex_hw_initqueryhca_t *inithca)
{
	mcxnex_hw_querydevlim_t	*devlim;
	mcxnex_cfg_profile_t	*cfg;
	mcxnex_icm_table_t	*icm_p[MCXNEX_NUM_ICM_RESOURCES];
	mcxnex_icm_table_t	*icm;
	mcxnex_icm_table_t	*tmp;
	uint64_t		icm_addr;
	uint64_t		icm_size;
	int			status, i, j;


	/* Bring in local devlims, cfg_profile and hs_icm table list */
	devlim = &state->hs_devlim;
	cfg = state->hs_cfg_profile;
	icm = state->hs_icm;

	/*
	 * Assign each ICM table's entry size from data in the devlims,
	 * except for RDB and MCG sizes, which are not returned in devlims
	 * but do have a fixed size, and the UAR context entry size, which
	 * we determine. For this, we use the "cp_num_pgs_per_uce" value
	 * from our hs_cfg_profile.
	 */
	icm[MCXNEX_CMPT].object_size	= devlim->cmpt_entry_sz;
	icm[MCXNEX_CMPT_QPC].object_size	= devlim->cmpt_entry_sz;
	icm[MCXNEX_CMPT_SRQC].object_size	= devlim->cmpt_entry_sz;
	icm[MCXNEX_CMPT_CQC].object_size	= devlim->cmpt_entry_sz;
	icm[MCXNEX_CMPT_EQC].object_size	= devlim->cmpt_entry_sz;
	icm[MCXNEX_MTT].object_size	= devlim->mtt_entry_sz;
	icm[MCXNEX_DMPT].object_size	= devlim->dmpt_entry_sz;
	icm[MCXNEX_QPC].object_size	= devlim->qpc_entry_sz;
	icm[MCXNEX_CQC].object_size	= devlim->cqc_entry_sz;
	icm[MCXNEX_SRQC].object_size	= devlim->srq_entry_sz;
	icm[MCXNEX_EQC].object_size	= devlim->eqc_entry_sz;
	icm[MCXNEX_RDB].object_size	= devlim->rdmardc_entry_sz *
	    cfg->cp_hca_max_rdma_in_qp;
	icm[MCXNEX_MCG].object_size	= MCXNEX_MCGMEM_SZ(state);
	icm[MCXNEX_ALTC].object_size	= devlim->altc_entry_sz;
	icm[MCXNEX_AUXC].object_size	= devlim->aux_entry_sz;

	/* Assign each ICM table's log2 number of entries */
	icm[MCXNEX_CMPT].log_num_entries = cfg->cp_log_num_cmpt;
	icm[MCXNEX_CMPT_QPC].log_num_entries = cfg->cp_log_num_qp;
	icm[MCXNEX_CMPT_SRQC].log_num_entries = cfg->cp_log_num_srq;
	icm[MCXNEX_CMPT_CQC].log_num_entries = cfg->cp_log_num_cq;
	icm[MCXNEX_CMPT_EQC].log_num_entries = MCXNEX_NUM_EQ_SHIFT;
	icm[MCXNEX_MTT].log_num_entries	= cfg->cp_log_num_mtt;
	icm[MCXNEX_DMPT].log_num_entries = cfg->cp_log_num_dmpt;
	icm[MCXNEX_QPC].log_num_entries	= cfg->cp_log_num_qp;
	icm[MCXNEX_SRQC].log_num_entries = cfg->cp_log_num_srq;
	icm[MCXNEX_CQC].log_num_entries	= cfg->cp_log_num_cq;
	icm[MCXNEX_EQC].log_num_entries	= MCXNEX_NUM_EQ_SHIFT;
	icm[MCXNEX_RDB].log_num_entries	= cfg->cp_log_num_qp;
	icm[MCXNEX_MCG].log_num_entries	= cfg->cp_log_num_mcg;
	icm[MCXNEX_ALTC].log_num_entries = cfg->cp_log_num_qp;
	icm[MCXNEX_AUXC].log_num_entries = cfg->cp_log_num_qp;

	/* Initialize the ICM tables */
	mcxnex_icm_tables_init(state);

	/*
	 * ICM tables must be aligned on their size in the ICM address
	 * space. So, here we order the tables from largest total table
	 * size to the smallest. All tables are a power of 2 in size, so
	 * this will ensure that all tables are aligned on their own size
	 * without wasting space in the ICM.
	 *
	 * In order to easily set the ICM addresses without needing to
	 * worry about the ordering of our table indices as relates to
	 * the mcxnex_rsrc_type_t enum, we will use a list of pointers
	 * representing the tables for the sort, then assign ICM addresses
	 * below using it.
	 */
	for (i = 0; i < MCXNEX_NUM_ICM_RESOURCES; i++) {
		icm_p[i] = &icm[i];
	}
	for (i = MCXNEX_NUM_ICM_RESOURCES; i > 0; i--) {
		switch (i) {
		case MCXNEX_CMPT_QPC:
		case MCXNEX_CMPT_SRQC:
		case MCXNEX_CMPT_CQC:
		case MCXNEX_CMPT_EQC:
			continue;
		}
		for (j = 1; j < i; j++) {
			if (icm_p[j]->table_size > icm_p[j - 1]->table_size) {
				tmp		= icm_p[j];
				icm_p[j]	= icm_p[j - 1];
				icm_p[j - 1]	= tmp;
			}
		}
	}

	/* Initialize the ICM address and ICM size */
	icm_addr = icm_size = 0;

	/*
	 * Set the ICM base address of each table, using our sorted
	 * list of pointers from above.
	 */
	for (i = 0; i < MCXNEX_NUM_ICM_RESOURCES; i++) {
		j = icm_p[i]->icm_type;
		switch (j) {
		case MCXNEX_CMPT_QPC:
		case MCXNEX_CMPT_SRQC:
		case MCXNEX_CMPT_CQC:
		case MCXNEX_CMPT_EQC:
			continue;
		}
		if (icm[j].table_size) {
			/*
			 * Set the ICM base address in the table, save the
			 * ICM offset in the rsrc pool and increment the
			 * total ICM allocation.
			 */
			icm[j].icm_baseaddr = icm_addr;
			icm_size += icm[j].table_size;
		}

		/* Verify that we don't exceed maximum ICM size */
		if (icm_size > devlim->max_icm_size) {
			/* free the ICM table memory resources */
			mcxnex_icm_tables_fini(state);
			MCXNEX_WARNING(state,
			    "ICM configuration exceeds maximum "
			    "configuration: max (0x%lx) requested (0x%lx)",
			    (ulong_t)devlim->max_icm_size, (ulong_t)icm_size);
			MCXNEX_ATTACH_MSG(state->hs_attach_buf,
			    "icm_config_toobig_fail");
			return (DDI_FAILURE);
		}

		/* assign address to the 4 pieces of the CMPT */
		if (j == MCXNEX_CMPT) {
			uint64_t cmpt_size = icm[j].table_size >> 2;
#define	init_cmpt_icm_baseaddr(rsrc, indx)				\
	icm[rsrc].icm_baseaddr	= icm_addr + (indx * cmpt_size);
			init_cmpt_icm_baseaddr(MCXNEX_CMPT_QPC, 0);
			init_cmpt_icm_baseaddr(MCXNEX_CMPT_SRQC, 1);
			init_cmpt_icm_baseaddr(MCXNEX_CMPT_CQC, 2);
			init_cmpt_icm_baseaddr(MCXNEX_CMPT_EQC, 3);
		}

		/* Increment the ICM address for the next table */
		icm_addr += icm[j].table_size;
	}

	/* Populate the structure for the INIT_HCA command */
	mcxnex_inithca_set(state, inithca);

	/*
	 * Prior to invoking INIT_HCA, we must have ICM memory in place
	 * for the reserved objects in each table. We will allocate and map
	 * this initial ICM memory here. Note that given the assignment
	 * of span_size above, tables that are smaller or equal in total
	 * size to the default span_size will be mapped in full.
	 */
	status = mcxnex_icm_dma_init(state);
	if (status != DDI_SUCCESS) {
		/* free the ICM table memory resources */
		mcxnex_icm_tables_fini(state);
		MCXNEX_WARNING(state, "Failed to allocate initial ICM: %x",
		    status);
		MCXNEX_ATTACH_MSG(state->hs_attach_buf,
		    "icm_config_dma_init_fail");
		return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);
}

/*
 * mcxnex_inithca_set()
 *    Context: Only called from attach() path context
 */
static void
mcxnex_inithca_set(mcxnex_state_t *state, mcxnex_hw_initqueryhca_t *inithca)
{
	mcxnex_cfg_profile_t	*cfg;
	mcxnex_icm_table_t	*icm;
	int			i;


	/* Populate the INIT_HCA structure */
	icm = state->hs_icm;
	cfg = state->hs_cfg_profile;

	/* set version */
	inithca->version = 0x02;	/* PRM 0.36 */
	/* set cacheline - log2 in 16-byte chunks */
	inithca->log2_cacheline = 0x2;	/* optimized for 64 byte cache */

	/* we need to update the inithca info with thie UAR info too */
	inithca->uar.log_max_uars = highbit(cfg->cp_log_num_uar);
	inithca->uar.uar_pg_sz = PAGESHIFT - MCXNEX_PAGESHIFT;

	/* Set endianess */
#ifdef	_LITTLE_ENDIAN
	inithca->big_endian	= 0;
#else
	inithca->big_endian	= 1;
#endif

	/* Port Checking is on by default */
	inithca->udav_port_chk	= MCXNEX_UDAV_PORTCHK_ENABLED;

	/* Enable IPoIB checksum */
	if (state->hs_devlim.ipoib_cksm)
		inithca->chsum_en = 1;

	/* Set each ICM table's attributes */
	for (i = 0; i < MCXNEX_NUM_ICM_RESOURCES; i++) {
		switch (icm[i].icm_type) {
		case MCXNEX_CMPT:
			inithca->tpt.cmpt_baseaddr = icm[i].icm_baseaddr;
			break;

		case MCXNEX_MTT:
			inithca->tpt.mtt_baseaddr = icm[i].icm_baseaddr;
			break;

		case MCXNEX_DMPT:
			inithca->tpt.dmpt_baseaddr = icm[i].icm_baseaddr;
			inithca->tpt.log_dmpt_sz   = icm[i].log_num_entries;
			inithca->tpt.pgfault_rnr_to = 0; /* just in case */
			break;

		case MCXNEX_QPC:
			inithca->context.log_num_qp = icm[i].log_num_entries;
			inithca->context.qpc_baseaddr_h =
			    icm[i].icm_baseaddr >> 32;
			inithca->context.qpc_baseaddr_l =
			    (icm[i].icm_baseaddr & 0xFFFFFFFF) >> 5;
			break;

		case MCXNEX_CQC:
			inithca->context.log_num_cq = icm[i].log_num_entries;
			inithca->context.cqc_baseaddr_h =
			    icm[i].icm_baseaddr >> 32;
			inithca->context.cqc_baseaddr_l =
			    (icm[i].icm_baseaddr & 0xFFFFFFFF) >> 5;
			break;

		case MCXNEX_SRQC:
			inithca->context.log_num_srq = icm[i].log_num_entries;
			inithca->context.srqc_baseaddr_h =
			    icm[i].icm_baseaddr >> 32;
			inithca->context.srqc_baseaddr_l =
			    (icm[i].icm_baseaddr & 0xFFFFFFFF) >> 5;
			break;

		case MCXNEX_EQC:
			inithca->context.log_num_eq = icm[i].log_num_entries;
			inithca->context.eqc_baseaddr_h =
			    icm[i].icm_baseaddr >> 32;
			inithca->context.eqc_baseaddr_l =
			    (icm[i].icm_baseaddr & 0xFFFFFFFF) >> 5;
			break;

		case MCXNEX_RDB:
			inithca->context.rdmardc_baseaddr_h =
			    icm[i].icm_baseaddr >> 32;
			inithca->context.rdmardc_baseaddr_l =
			    (icm[i].icm_baseaddr & 0xFFFFFFFF) >> 5;
			inithca->context.log_num_rdmardc =
			    cfg->cp_log_num_rdb - cfg->cp_log_num_qp;
			break;

		case MCXNEX_MCG:
			inithca->multi.mc_baseaddr    = icm[i].icm_baseaddr;
			inithca->multi.log_mc_tbl_sz  = icm[i].log_num_entries;
			inithca->multi.log_mc_tbl_ent =
			    highbit(MCXNEX_MCGMEM_SZ(state)) - 1;
			inithca->multi.log_mc_tbl_hash_sz =
			    cfg->cp_log_num_mcg_hash;
			inithca->multi.mc_hash_fn = MCXNEX_MCG_DEFAULT_HASH_FN;
			break;

		case MCXNEX_ALTC:
			inithca->context.altc_baseaddr = icm[i].icm_baseaddr;
			break;

		case MCXNEX_AUXC:
			inithca->context.auxc_baseaddr = icm[i].icm_baseaddr;
			break;

		default:
			break;

		}
	}

}

/*
 * mcxnex_icm_tables_init()
 *    Context: Only called from attach() path context
 *
 * Dynamic ICM breaks the various ICM tables into "span_size" chunks
 * to enable allocation of backing memory on demand.  Arbel used a
 * fixed size ARBEL_ICM_SPAN_SIZE (initially was 512KB) as the
 * span_size for all ICM chunks.  Mcxnex has other considerations,
 * so the span_size used differs from Arbel.
 *
 * The basic considerations for why Mcxnex differs are:
 *
 *	1) ICM memory is in units of MCXNEX pages.
 *
 *	2) The AUXC table is approximately 1 byte per QP.
 *
 *	3) ICM memory for AUXC, ALTC, and RDB is allocated when
 *	the ICM memory for the corresponding QPC is allocated.
 *
 *	4) ICM memory for the CMPT corresponding to the various primary
 *	resources (QPC, SRQC, CQC, and EQC) is allocated when the ICM
 *	memory for the primary resource is allocated.
 *
 * One MCXNEX page (4KB) would typically map 4K QPs worth of AUXC.
 * So, the minimum chunk for the various QPC related ICM memory should
 * all be allocated to support the 4K QPs.  Currently, this means the
 * amount of memory for the various QP chunks is:
 *
 *	QPC	256*4K bytes
 *	RDB	128*4K bytes
 *	CMPT	 64*4K bytes
 *	ALTC	 64*4K bytes
 *	AUXC	  1*4K bytes
 *
 * The span_size chosen for the QP resource is 4KB of AUXC entries,
 * or 1 MCXNEX_PAGESIZE worth, which is the minimum ICM mapping size.
 *
 * Other ICM resources can have their span_size be more arbitrary.
 * This is 4K (MCXNEX_ICM_SPAN), except for MTTs because they are tiny.
 */

/* macro to make the code below cleaner */
#define	init_dependent(rsrc, dep)				\
	icm[dep].span		= icm[rsrc].span;		\
	icm[dep].num_spans	= icm[rsrc].num_spans;		\
	icm[dep].split_shift	= icm[rsrc].split_shift;	\
	icm[dep].span_mask	= icm[rsrc].span_mask;		\
	icm[dep].span_shift	= icm[rsrc].span_shift;		\
	icm[dep].rsrc_mask	= icm[rsrc].rsrc_mask;

static void
mcxnex_icm_tables_init(mcxnex_state_t *state)
{
	mcxnex_icm_table_t	*icm;
	int			i, k;
	uint32_t		per_split;


	icm = state->hs_icm;

	for (i = 0; i < MCXNEX_NUM_ICM_RESOURCES; i++) {
		icm[i].icm_type		= i;
		icm[i].num_entries	= 1 << icm[i].log_num_entries;
		icm[i].log_object_size	= highbit(icm[i].object_size) - 1;
		icm[i].table_size	= icm[i].num_entries <<
		    icm[i].log_object_size;

		/* deal with "dependent" resource types */
		switch (i) {
		case MCXNEX_AUXC:
#ifdef MCXNEXUS_FW_WORKAROUND
			icm[i].table_size = 0x80000000ull;
			/* FALLTHROUGH */
#endif
		case MCXNEX_CMPT_QPC:
		case MCXNEX_RDB:
		case MCXNEX_ALTC:
			init_dependent(MCXNEX_QPC, i);
			continue;
		case MCXNEX_CMPT_SRQC:
			init_dependent(MCXNEX_SRQC, i);
			continue;
		case MCXNEX_CMPT_CQC:
			init_dependent(MCXNEX_CQC, i);
			continue;
		case MCXNEX_CMPT_EQC:
			init_dependent(MCXNEX_EQC, i);
			continue;
		}

		icm[i].span = MCXNEX_ICM_SPAN;	/* default #rsrc's in 1 span */
		if (i == MCXNEX_MTT) /* Alloc enough MTTs to map 256MB */
			icm[i].span = MCXNEX_ICM_SPAN * 16;
		icm[i].num_spans = icm[i].num_entries / icm[i].span;
		if (icm[i].num_spans == 0) {
			icm[i].span = icm[i].num_entries;
			per_split = 1;
			icm[i].num_spans = icm[i].num_entries / icm[i].span;
		} else {
			per_split = icm[i].num_spans / MCXNEX_ICM_SPLIT;
			if (per_split == 0) {
				per_split = 1;
			}
		}

		/*
		 * Ensure a minimum table size of an ICM page, and a
		 * maximum span size of the ICM table size.  This ensures
		 * that we don't have less than an ICM page to map, which is
		 * impossible, and that we will map an entire table at
		 * once if it's total size is less than the span size.
		 */
		icm[i].table_size = max(icm[i].table_size, MCXNEX_PAGESIZE);

		icm[i].span_shift = 0;
		for (k = icm[i].span; k != 1; k >>= 1)
			icm[i].span_shift++;
		icm[i].split_shift = icm[i].span_shift;
		for (k = per_split; k != 1; k >>= 1)
			icm[i].split_shift++;
		icm[i].span_mask = (1 << icm[i].split_shift) -
		    (1 << icm[i].span_shift);
		icm[i].rsrc_mask = (1 << icm[i].span_shift) - 1;

		DTRACE_PROBE3(mcxnex__icm_resource_spans,
		    mcxnex_state_t *, state,
		    int, i,
		    mcxnex_icm_table_t *, &icm[i]);


		/* Initialize the table lock */
		mutex_init(&icm[i].icm_table_lock, NULL, MUTEX_DRIVER,
		    DDI_INTR_PRI(state->hs_intrmsi_pri));
		cv_init(&icm[i].icm_table_cv, NULL, CV_DRIVER, NULL);
	}

}

/*
 * mcxnex_icm_tables_fini()
 *    Context: Only called from attach() path context
 *
 * Clean up all icm_tables.  Free the bitmap and dma_info arrays.
 */
static void
mcxnex_icm_tables_fini(mcxnex_state_t *state)
{
	mcxnex_icm_table_t	*icm;
	int			nspans;
	int			i, j;


	icm = state->hs_icm;

	for (i = 0; i < MCXNEX_NUM_ICM_RESOURCES; i++) {

		mutex_enter(&icm[i].icm_table_lock);
		nspans = icm[i].num_spans;

		for (j = 0; j < MCXNEX_ICM_SPLIT; j++) {
			if (icm[i].icm_dma[j])
				/* Free the ICM DMA slots */
				kmem_free(icm[i].icm_dma[j],
				    nspans * sizeof (mcxnex_dma_info_t));

			if (icm[i].icm_bitmap[j])
				/* Free the table bitmap */
				kmem_free(icm[i].icm_bitmap[j],
				    (nspans + 7) / 8);
		}
		/* Destroy the table lock */
		cv_destroy(&icm[i].icm_table_cv);
		mutex_exit(&icm[i].icm_table_lock);
		mutex_destroy(&icm[i].icm_table_lock);
	}

}

/*
 * mcxnex_icm_dma_init()
 *    Context: Only called from attach() path context
 */
static int
mcxnex_icm_dma_init(mcxnex_state_t *state)
{
	mcxnex_icm_table_t	*icm;
	mcxnex_rsrc_type_t	type;
	int			status;


	/*
	 * This routine will allocate initial ICM DMA resources for ICM
	 * tables that have reserved ICM objects. This is the only routine
	 * where we should have to allocate ICM outside of mcxnex_rsrc_alloc().
	 * We need to allocate ICM here explicitly, rather than in
	 * mcxnex_rsrc_alloc(), because we've not yet completed the resource
	 * pool initialization. When the resource pools are initialized
	 * (in mcxnex_rsrc_init_phase2(), see mcxnex_rsrc.c for more
	 * information), resource preallocations will be invoked to match
	 * the ICM allocations seen here. We will then be able to use the
	 * normal allocation path.  Note we don't need to set a refcnt on
	 * these initial allocations because that will be done in the calls
	 * to mcxnex_rsrc_alloc() from mcxnex_hw_entries_init() for the
	 * "prealloc" objects (see mcxnex_rsrc.c for more information).
	 */
	for (type = 0; type < MCXNEX_NUM_ICM_RESOURCES; type++) {

		/* ICM for these is allocated within mcxnex_icm_alloc() */
		switch (type) {
		case MCXNEX_CMPT:
		case MCXNEX_CMPT_QPC:
		case MCXNEX_CMPT_SRQC:
		case MCXNEX_CMPT_CQC:
		case MCXNEX_CMPT_EQC:
		case MCXNEX_AUXC:
		case MCXNEX_ALTC:
		case MCXNEX_RDB:
			continue;
		}

		icm = &state->hs_icm[type];

		mutex_enter(&icm->icm_table_lock);
		status = mcxnex_icm_alloc(state, type, 0, 0);
		mutex_exit(&icm->icm_table_lock);
		if (status != DDI_SUCCESS) {
			while (type--) {
				icm = &state->hs_icm[type];
				mutex_enter(&icm->icm_table_lock);
				mcxnex_icm_free(state, type, 0, 0);
				mutex_exit(&icm->icm_table_lock);
			}
			return (DDI_FAILURE);
		}
	}

	return (DDI_SUCCESS);
}

/*
 * mcxnex_icm_dma_fini()
 *    Context: Only called from attach() path context
 *
 * ICM has been completely unmapped.  We just free the memory here.
 */
static void
mcxnex_icm_dma_fini(mcxnex_state_t *state)
{
	mcxnex_icm_table_t	*icm;
	mcxnex_dma_info_t	*dma_info;
	mcxnex_rsrc_type_t	type;
	int			index1, index2;


	for (type = 0; type < MCXNEX_NUM_ICM_RESOURCES; type++) {
		icm = &state->hs_icm[type];
		for (index1 = 0; index1 < MCXNEX_ICM_SPLIT; index1++) {
			dma_info = icm->icm_dma[index1];
			if (dma_info == NULL)
				continue;
			for (index2 = 0; index2 < icm->num_spans; index2++) {
				if (dma_info[index2].dma_hdl)
					mcxnex_dma_free(&dma_info[index2]);
				dma_info[index2].dma_hdl = NULL;
			}
		}
	}

}

/*
 * mcxnex_hca_ports_shutdown()
 *    Context: Only called from attach() and/or detach() path contexts
 */
static int
mcxnex_hca_ports_shutdown(mcxnex_state_t *state, uint_t num_init)
{
	int	i, status;

	/*
	 * Post commands to shutdown all init'd Mcxnex HCA ports.  Note: if
	 * any of these commands fail for any reason, it would be entirely
	 * unexpected and probably indicative a serious problem (HW or SW).
	 * Although we do return void from this function, this type of failure
	 * should not go unreported.  That is why we have the warning message.
	 */
	for (i = 0; i < num_init; i++) {
		status = mcxnex_close_port_cmd_post(state, i + 1, DDI_NOSLEEP);
		if (status != MCXNEX_CMD_SUCCESS) {
			MCXNEX_WARNING(state, "failed to shutdown HCA port, %d",
			    status);
			return (status);
		}
	}
	return (MCXNEX_CMD_SUCCESS);
}


/*
 * mcxnex_internal_uarpg_init
 *    Context: Only called from attach() path context
 */
static int
mcxnex_internal_uarpg_init(mcxnex_state_t *state)
{
	int	status;
	mcxnex_dbr_info_t 	*info;

	/*
	 * Allocate the UAR page for kernel use. This UAR page is
	 * the privileged UAR page through which all kernel generated
	 * doorbells will be rung. There are a number of UAR pages
	 * reserved by hardware at the front of the UAR BAR, indicated
	 * by DEVCAP.num_rsvd_uar, which we have already allocated. So,
	 * the kernel page, or UAR page index num_rsvd_uar, will be
	 * allocated here for kernel use.
	 */

	status = mcxnex_rsrc_alloc(state, MCXNEX_UARPG, 1, DDI_SLEEP,
	    &state->hs_uarkpg_rsrc);
	if (status != DDI_SUCCESS) {
		return (DDI_FAILURE);
	}

	/* Setup pointer to kernel UAR page */
	state->hs_uar = (mcxnex_hw_uar_t *)state->hs_uarkpg_rsrc->hr_addr;

	/* need to set up DBr tracking as well */
	status = mcxnex_dbr_page_alloc(state, &info);
	if (status != DDI_SUCCESS) {
		return (DDI_FAILURE);
	}
	state->hs_kern_dbr = info;
	return (DDI_SUCCESS);
}


/*
 * mcxnex_internal_uarpg_fini
 *    Context: Only called from attach() and/or detach() path contexts
 */
static void
mcxnex_internal_uarpg_fini(mcxnex_state_t *state)
{
	/* Free up Mcxnex UAR page #1 (kernel driver doorbells) */
	mcxnex_rsrc_free(state, &state->hs_uarkpg_rsrc);
}


/*
 * mcxnex_special_qp_contexts_reserve()
 *    Context: Only called from attach() path context
 */
static int
mcxnex_special_qp_contexts_reserve(mcxnex_state_t *state)
{
	mcxnex_rsrc_t	*qp0_rsrc, *qp1_rsrc, *qp_resvd;
	int		status;

	/* Initialize the lock used for special QP rsrc management */
	mutex_init(&state->hs_spec_qplock, NULL, MUTEX_DRIVER,
	    DDI_INTR_PRI(state->hs_intrmsi_pri));

	/*
	 * Reserve contexts for QP0.  These QP contexts will be setup to
	 * act as aliases for the real QP0.  Note: We are required to grab
	 * two QPs (one per port) even if we are operating in single-port
	 * mode.
	 */
	status = mcxnex_rsrc_alloc(state, MCXNEX_QPC, 2, DDI_SLEEP, &qp0_rsrc);
	if (status != DDI_SUCCESS) {
		mutex_destroy(&state->hs_spec_qplock);
		return (DDI_FAILURE);
	}
	state->hs_spec_qp0 = qp0_rsrc;

	/*
	 * Reserve contexts for QP1.  These QP contexts will be setup to
	 * act as aliases for the real QP1.  Note: We are required to grab
	 * two QPs (one per port) even if we are operating in single-port
	 * mode.
	 */
	status = mcxnex_rsrc_alloc(state, MCXNEX_QPC, 2,
	    DDI_SLEEP, &qp1_rsrc);
	if (status != DDI_SUCCESS) {
		mcxnex_rsrc_free(state, &qp0_rsrc);
		mutex_destroy(&state->hs_spec_qplock);
		return (DDI_FAILURE);
	}
	state->hs_spec_qp1 = qp1_rsrc;

	status = mcxnex_rsrc_alloc(state, MCXNEX_QPC, 4,
	    DDI_SLEEP, &qp_resvd);
	if (status != DDI_SUCCESS) {
		mcxnex_rsrc_free(state, &qp1_rsrc);
		mcxnex_rsrc_free(state, &qp0_rsrc);
		mutex_destroy(&state->hs_spec_qplock);
		return (DDI_FAILURE);
	}
	state->hs_spec_qp_unused = qp_resvd;

	return (DDI_SUCCESS);
}


/*
 * mcxnex_special_qp_contexts_unreserve()
 *    Context: Only called from attach() and/or detach() path contexts
 */
static void
mcxnex_special_qp_contexts_unreserve(mcxnex_state_t *state)
{

	/* Unreserve contexts for spec_qp_unused */
	mcxnex_rsrc_free(state, &state->hs_spec_qp_unused);

	/* Unreserve contexts for QP1 */
	mcxnex_rsrc_free(state, &state->hs_spec_qp1);

	/* Unreserve contexts for QP0 */
	mcxnex_rsrc_free(state, &state->hs_spec_qp0);

	/* Destroy the lock used for special QP rsrc management */
	mutex_destroy(&state->hs_spec_qplock);
}


/*
 * mcxnex_sw_reset()
 *    Context: Currently called only from attach() path context
 */
static int
mcxnex_sw_reset(mcxnex_state_t *state)
{
	ddi_acc_handle_t	hdl = mcxnex_get_pcihdl(state);
	ddi_acc_handle_t	cmdhdl = mcxnex_get_cmdhdl(state);
	uint32_t		reset_delay;
	int			status, i;
	uint32_t		sem;
	uint_t			offset;
	uint32_t		data32;		/* for devctl & linkctl */
	int			loopcnt;

	/* initialize the FMA retry loop */
	mcxnex_pio_init(fm_loop_cnt, fm_status, fm_test);
	mcxnex_pio_init(fm_loop_cnt2, fm_status2, fm_test2);

	/*
	 * If the configured software reset delay is set to zero, then we
	 * will not attempt a software reset of the Mcxnex device.
	 */
	reset_delay = state->hs_cfg_profile->cp_sw_reset_delay;
	if (reset_delay == 0) {
		return (DDI_SUCCESS);
	}

	/* the FMA retry loop starts. */
	mcxnex_pio_start(state, cmdhdl, pio_error, fm_loop_cnt, fm_status,
	    fm_test);
	mcxnex_pio_start(state, hdl, pio_error2, fm_loop_cnt2, fm_status2,
	    fm_test2);

	/* Query the PCI capabilities of the HCA device */
	/* but don't process the VPD until after reset */
	status = mcxnex_pci_capability_list(state, hdl);
	if (status != DDI_SUCCESS) {
		MCXNEX_WARNING(state,
		    "failed to get pci capabilities list (%x)", status);
		return (DDI_FAILURE);
	}

	/*
	 * Read all PCI config info (reg0...reg63).  Note: According to the
	 * Mcxnex software reset application note, we should not read or
	 * restore the values in reg22 and reg23.
	 * NOTE:  For Mcxnex (and Arbel too) it says to restore the command
	 * register LAST, and technically, you need to restore the
	 * PCIE Capability "device control" and "link control" (word-sized,
	 * at offsets 0x08 and 0x10 from the capbility ID respectively).
	 * We hold off restoring the command register - offset 0x4 - till last
	 */

	/* 1st, wait for the semaphore assure accessibility - per PRM */
	status = -1;
	for (i = 0; i < NANOSEC/MICROSEC /* 1sec timeout */; i++) {
		sem = ddi_get32(cmdhdl, state->hs_cmd_regs.sw_semaphore);
		if (sem == 0) {
			status = 0;
			break;
		}
		drv_usecwait(1);
	}

	/* Check if timeout happens */
	if (status == -1) {
		/*
		 * Remove this acc handle from Mcxnex, then log
		 * the error.
		 */
		mcxnex_pci_config_teardown(state, &hdl);

		MCXNEX_WARNING(state, "sw_reset timeout: "
		    "failed to get the semaphore(0x%p)",
		    (void *)state->hs_cmd_regs.sw_semaphore);

		mcxnex_fm_ereport(state, HCA_IBA_ERR, HCA_ERR_NON_FATAL);
		return (DDI_FAILURE);
	}

	for (i = 0; i < MCXNEX_SW_RESET_NUMREGS; i++) {
		if ((i != MCXNEX_SW_RESET_REG22_RSVD) &&
		    (i != MCXNEX_SW_RESET_REG23_RSVD)) {
			state->hs_cfg_data[i]  = pci_config_get32(hdl, i << 2);
		}
	}

	/*
	 * Perform the software reset (by writing 1 at offset 0xF0010)
	 */
	ddi_put32(cmdhdl, state->hs_cmd_regs.sw_reset, MCXNEX_SW_RESET_START);

	/*
	 * This delay is required so as not to cause a panic here. If the
	 * device is accessed too soon after reset it will not respond to
	 * config cycles, causing a Master Abort and panic.
	 */
	drv_usecwait(reset_delay);

	/*
	 * Poll waiting for the device to finish resetting.
	 */
	loopcnt = 100;	/* 100 times @ 100 usec - total delay 10 msec */
	while ((pci_config_get32(hdl, 0) & 0x0000FFFF) != PCI_VENID_MLX) {
		drv_usecwait(MCXNEX_SW_RESET_POLL_DELAY);
		if (--loopcnt == 0)
			break;	/* just in case, break and go on */
	}
	if (loopcnt == 0)
		cmn_err(CE_CONT, "!Never see VEND_ID - read == %X",
		    pci_config_get32(hdl, 0));

	/*
	 * Restore the config info
	 */
	for (i = 0; i < MCXNEX_SW_RESET_NUMREGS; i++) {
		if (i == 1) continue;	/* skip the status/ctrl reg */
		if ((i != MCXNEX_SW_RESET_REG22_RSVD) &&
		    (i != MCXNEX_SW_RESET_REG23_RSVD)) {
			pci_config_put32(hdl, i << 2, state->hs_cfg_data[i]);
		}
	}

	/*
	 * PCI Express Capability - we saved during capability list, and
	 * we'll restore them here.
	 */
	offset = state->hs_pci_cap_offset;
	data32 = state->hs_pci_cap_devctl;
	pci_config_put32(hdl, offset + MCXNEX_PCI_CAP_DEV_OFFS, data32);
	data32 = state->hs_pci_cap_lnkctl;
	pci_config_put32(hdl, offset + MCXNEX_PCI_CAP_LNK_OFFS, data32);

	pci_config_put32(hdl, 0x04, (state->hs_cfg_data[1] | 0x0006));

	/* the FMA retry loop ends. */
	mcxnex_pio_end(state, hdl, pio_error2, fm_loop_cnt2, fm_status2,
	    fm_test2);
	mcxnex_pio_end(state, cmdhdl, pio_error, fm_loop_cnt, fm_status,
	    fm_test);

	return (DDI_SUCCESS);

pio_error2:
	/* fall through */
pio_error:
	mcxnex_fm_ereport(state, HCA_SYS_ERR, HCA_ERR_NON_FATAL);
	return (DDI_FAILURE);
}


/*
 * mcxnex_mcg_init()
 *    Context: Only called from attach() path context
 */
static int
mcxnex_mcg_init(mcxnex_state_t *state)
{
	uint_t		mcg_tmp_sz;


	/*
	 * Allocate space for the MCG temporary copy buffer.  This is
	 * used by the Attach/Detach Multicast Group code
	 */
	mcg_tmp_sz = MCXNEX_MCGMEM_SZ(state);
	state->hs_mcgtmp = kmem_zalloc(mcg_tmp_sz, KM_SLEEP);

	/*
	 * Initialize the multicast group mutex.  This ensures atomic
	 * access to add, modify, and remove entries in the multicast
	 * group hash lists.
	 */
	mutex_init(&state->hs_mcglock, NULL, MUTEX_DRIVER,
	    DDI_INTR_PRI(state->hs_intrmsi_pri));

	return (DDI_SUCCESS);
}


/*
 * mcxnex_mcg_fini()
 *    Context: Only called from attach() and/or detach() path contexts
 */
static void
mcxnex_mcg_fini(mcxnex_state_t *state)
{
	uint_t		mcg_tmp_sz;


	/* Free up the space used for the MCG temporary copy buffer */
	mcg_tmp_sz = MCXNEX_MCGMEM_SZ(state);
	kmem_free(state->hs_mcgtmp, mcg_tmp_sz);

	/* Destroy the multicast group mutex */
	mutex_destroy(&state->hs_mcglock);

}


/*
 * mcxnex_fw_version_check()
 *    Context: Only called from attach() path context
 */
static int
mcxnex_fw_version_check(mcxnex_state_t *state)
{

	uint_t	mcxnex_fw_ver_major;
	uint_t	mcxnex_fw_ver_minor;
	uint_t	mcxnex_fw_ver_subminor;

#ifdef FMA_TEST
	if (mcxnex_test_num == -1) {
		return (DDI_FAILURE);
	}
#endif

	/*
	 * Depending on which version of driver we have attached, and which
	 * HCA we've attached, the firmware version checks will be different.
	 * We set up the comparison values for both Arbel and Sinai HCAs.
	 */
	switch (state->hs_operational_mode) {
	case MCXNEX_HCA_MODE:
		mcxnex_fw_ver_major = MCXNEX_FW_VER_MAJOR;
		mcxnex_fw_ver_minor = MCXNEX_FW_VER_MINOR;
		mcxnex_fw_ver_subminor = MCXNEX_FW_VER_SUBMINOR;
		break;

	default:
		return (DDI_FAILURE);
	}

	/*
	 * If FW revision major number is less than acceptable,
	 * return failure, else if greater return success.  If
	 * the major numbers are equal than check the minor number
	 */
	if (state->hs_fw.fw_rev_major < mcxnex_fw_ver_major) {
		return (DDI_FAILURE);
	} else if (state->hs_fw.fw_rev_major > mcxnex_fw_ver_major) {
		return (DDI_SUCCESS);
	}

	/*
	 * Do the same check as above, except for minor revision numbers
	 * If the minor numbers are equal than check the subminor number
	 */
	if (state->hs_fw.fw_rev_minor < mcxnex_fw_ver_minor) {
		return (DDI_FAILURE);
	} else if (state->hs_fw.fw_rev_minor > mcxnex_fw_ver_minor) {
		return (DDI_SUCCESS);
	}

	/*
	 * Once again we do the same check as above, except for the subminor
	 * revision number.  If the subminor numbers are equal here, then
	 * these are the same firmware version, return success
	 */
	if (state->hs_fw.fw_rev_subminor < mcxnex_fw_ver_subminor) {
		return (DDI_FAILURE);
	} else if (state->hs_fw.fw_rev_subminor > mcxnex_fw_ver_subminor) {
		return (DDI_SUCCESS);
	}

	return (DDI_SUCCESS);
}


/*
 * mcxnex_device_info_report()
 *    Context: Only called from attach() path context
 */
static void
mcxnex_device_info_report(mcxnex_state_t *state)
{

	cmn_err(CE_CONT, "?%s%d: FW ver: %04d.%04d.%04d, "
	    "HW rev: %02d\n", ddi_driver_name(state->hs_dip),
	    state->hs_instance, state->hs_fw.fw_rev_major,
	    state->hs_fw.fw_rev_minor, state->hs_fw.fw_rev_subminor,
	    state->hs_revision_id);
}


/*
 * mcxnex_pci_capability_list()
 *    Context: Only called from attach() path context
 */
static int
mcxnex_pci_capability_list(mcxnex_state_t *state, ddi_acc_handle_t hdl)
{
	uint_t		offset, data;
	uint32_t	data32;

	state->hs_pci_cap_offset = 0;		/* make sure it's cleared */

	/*
	 * Check for the "PCI Capabilities" bit in the "Status Register".
	 * Bit 4 in this register indicates the presence of a "PCI
	 * Capabilities" list.
	 *
	 * PCI-Express requires this bit to be set to 1.
	 */
	data = pci_config_get16(hdl, 0x06);
	if ((data & 0x10) == 0) {
		return (DDI_FAILURE);
	}

	/*
	 * Starting from offset 0x34 in PCI config space, find the
	 * head of "PCI capabilities" list, and walk the list.  If
	 * capabilities of a known type are encountered (e.g.
	 * "PCI-X Capability"), then call the appropriate handler
	 * function.
	 */
	offset = pci_config_get8(hdl, 0x34);
	while (offset != 0x0) {
		data = pci_config_get8(hdl, offset);
		/*
		 * Check for known capability types.  Mcxnex has the
		 * following:
		 *    o Power Mgmt	 (0x02)
		 *    o VPD Capability   (0x03)
		 *    o PCI-E Capability (0x10)
		 *    o MSIX Capability  (0x11)
		 */
		switch (data) {
		case 0x01:
			/* power mgmt handling */
			break;
		case 0x03:

/*
 * Reading the PCIe VPD is inconsistent - that is, sometimes causes
 * problems on (mostly) X64, though we've also seen problems w/ Sparc
 * and Tavor --- so, for now until it's root caused, don't try and
 * read it
 */
#ifdef MCXNEX_VPD_WORKS
			mcxnex_pci_capability_vpd(state, hdl, offset);
#else
			delay(100);
			mcxnex_pci_capability_vpd(state, hdl, offset);
#endif
			break;
		case 0x10:
			/*
			 * PCI Express Capability - save offset & contents
			 * for later in reset
			 */
			state->hs_pci_cap_offset = offset;
			data32 = pci_config_get32(hdl,
			    offset + MCXNEX_PCI_CAP_DEV_OFFS);
			state->hs_pci_cap_devctl = data32;
			data32 = pci_config_get32(hdl,
			    offset + MCXNEX_PCI_CAP_LNK_OFFS);
			state->hs_pci_cap_lnkctl = data32;
			break;
		case 0x11:
			/*
			 * MSIX support - nothing to do, taken care of in the
			 * MSI/MSIX interrupt frameworkd
			 */
			break;
		default:
			/* just go on to the next */
			break;
		}

		/* Get offset of next entry in list */
		offset = pci_config_get8(hdl, offset + 1);
	}

	return (DDI_SUCCESS);
}

/*
 * mcxnex_pci_read_vpd()
 *    Context: Only called from attach() path context
 *    utility routine for mcxnex_pci_capability_vpd()
 */
static int
mcxnex_pci_read_vpd(ddi_acc_handle_t hdl, uint_t offset, uint32_t addr,
    uint32_t *data)
{
	int		retry = 40;  /* retry counter for EEPROM poll */
	uint32_t	val;
	int		vpd_addr = offset + 2;
	int		vpd_data = offset + 4;

	/*
	 * In order to read a 32-bit value from VPD, we are to write down
	 * the address (offset in the VPD itself) to the address register.
	 * To signal the read, we also clear bit 31.  We then poll on bit 31
	 * and when it is set, we can then read our 4 bytes from the data
	 * register.
	 */
	(void) pci_config_put32(hdl, offset, addr << 16);
	do {
		drv_usecwait(1000);
		val = pci_config_get16(hdl, vpd_addr);
		if (val & 0x8000) {		/* flag bit set */
			*data = pci_config_get32(hdl, vpd_data);
			return (DDI_SUCCESS);
		}
	} while (--retry);
	/* read of flag failed write one message but count the failures */
	if (debug_vpd == 0 && mcxnex_verbose)
		cmn_err(CE_NOTE,
		    "!Failed to see flag bit after VPD addr write");
	debug_vpd++;


vpd_read_fail:
	return (DDI_FAILURE);
}



/*
 *   mcxnex_pci_capability_vpd()
 *    Context: Only called from attach() path context
 */
static void
mcxnex_pci_capability_vpd(mcxnex_state_t *state, ddi_acc_handle_t hdl,
    uint_t offset)
{
	uint8_t			name_length;
	uint8_t			pn_length;
	int			i, err = 0;
	int			vpd_str_id = 0;
	int			vpd_ro_desc;
	int			vpd_ro_pn_desc;
#ifdef _BIG_ENDIAN
	uint32_t		data32;
#endif /* _BIG_ENDIAN */
	union {
		uint32_t	vpd_int[MCXNEX_VPD_HDR_DWSIZE];
		uchar_t		vpd_char[MCXNEX_VPD_HDR_BSIZE];
	} vpd;


	/*
	 * Read in the Vital Product Data (VPD) to the extend needed
	 * by the fwflash utility
	 */
	for (i = 0; i < MCXNEX_VPD_HDR_DWSIZE; i++) {
		err = mcxnex_pci_read_vpd(hdl, offset, i << 2, &vpd.vpd_int[i]);
		if (err != DDI_SUCCESS) {
			if (mcxnex_verbose)
				cmn_err(CE_NOTE, "!VPD read failed\n");
			goto out;
		}
	}

#ifdef _BIG_ENDIAN
	/* Need to swap bytes for big endian. */
	for (i = 0; i < MCXNEX_VPD_HDR_DWSIZE; i++) {
		data32 = vpd.vpd_int[i];
		vpd.vpd_char[(i << 2) + 3] =
		    (uchar_t)((data32 & 0xFF000000) >> 24);
		vpd.vpd_char[(i << 2) + 2] =
		    (uchar_t)((data32 & 0x00FF0000) >> 16);
		vpd.vpd_char[(i << 2) + 1] =
		    (uchar_t)((data32 & 0x0000FF00) >> 8);
		vpd.vpd_char[i << 2] = (uchar_t)(data32 & 0x000000FF);
	}
#endif	/* _BIG_ENDIAN */

	/* Check for VPD String ID Tag */
	if (vpd.vpd_char[vpd_str_id] == 0x82) {
		/* get the product name */
		name_length = (uint8_t)vpd.vpd_char[vpd_str_id + 1];
		if (name_length > sizeof (state->hs_hca_name)) {
			cmn_err(CE_NOTE, "!VPD name too large (0x%x)\n",
			    name_length);
			goto out;
		}
		(void) memcpy(state->hs_hca_name, &vpd.vpd_char[vpd_str_id + 3],
		    name_length);
		state->hs_hca_name[name_length] = 0;

		/* get the part number */
		vpd_ro_desc = name_length + 3; /* read-only tag location */
		vpd_ro_pn_desc = vpd_ro_desc + 3; /* P/N keyword location */

		/* Verify read-only tag and Part Number keyword. */
		if (vpd.vpd_char[vpd_ro_desc] != 0x90 ||
		    (vpd.vpd_char[vpd_ro_pn_desc] != 'P' &&
		    vpd.vpd_char[vpd_ro_pn_desc + 1] != 'N')) {
			cmn_err(CE_NOTE, "!VPD Part Number not found\n");
			goto out;
		}

		pn_length = (uint8_t)vpd.vpd_char[vpd_ro_pn_desc + 2];
		if (pn_length > sizeof (state->hs_hca_pn)) {
			cmn_err(CE_NOTE, "!VPD part number too large (0x%x)\n",
			    name_length);
			goto out;
		}
		(void) memcpy(state->hs_hca_pn,
		    &vpd.vpd_char[vpd_ro_pn_desc + 3],
		    pn_length);
		state->hs_hca_pn[pn_length] = 0;
		state->hs_hca_pn_len = pn_length;
		cmn_err(CE_CONT, "!vpd %s\n", state->hs_hca_pn);
	} else {
		/* Wrong VPD String ID Tag */
		cmn_err(CE_NOTE, "!VPD String ID Tag not found, tag: %02x\n",
		    vpd.vpd_char[0]);
		goto out;
	}
	return;
out:
	state->hs_hca_pn_len = 0;
}



/*
 * mcxnex_intr_or_msi_init()
 *    Context: Only called from attach() path context
 */
static int
mcxnex_intr_or_msi_init(mcxnex_state_t *state)
{
	int	status;


	/* Query for the list of supported interrupt event types */
	status = ddi_intr_get_supported_types(state->hs_dip,
	    &state->hs_intr_types_avail);
	if (status != DDI_SUCCESS) {
		return (DDI_FAILURE);
	}

	/*
	 * If hardware supports MSI-X in this system (and, if it
	 * hasn't been overridden by a configuration variable), then
	 * the default behavior is to use a single MSI-X.  Otherwise,
	 * fallback to using legacy interrupts.  Also, if MSI-X is chosen,
	 * but fails for whatever reasons, then next try MSI
	 */
	if ((state->hs_cfg_profile->cp_use_msi_if_avail != 0) &&
	    (state->hs_intr_types_avail & DDI_INTR_TYPE_MSIX)) {
		status = mcxnex_add_intrs(state, DDI_INTR_TYPE_MSIX);
		if (status == DDI_SUCCESS) {
			state->hs_intr_type_chosen = DDI_INTR_TYPE_MSIX;
			return (DDI_SUCCESS);
		}
	}

	/*
	 * If hardware supports MSI in this system (and, if it
	 * hasn't been overridden by a configuration variable), then
	 * the default behavior is to use a single MSIX.  Otherwise,
	 * fallback to using legacy interrupts.  Also, if MSI is chosen,
	 * but fails for whatever reasons, then fallback to using legacy
	 * interrupts.
	 */
	if ((state->hs_cfg_profile->cp_use_msi_if_avail != 0) &&
	    (state->hs_intr_types_avail & DDI_INTR_TYPE_MSI)) {
		status = mcxnex_add_intrs(state, DDI_INTR_TYPE_MSI);
		if (status == DDI_SUCCESS) {
			state->hs_intr_type_chosen = DDI_INTR_TYPE_MSI;
			return (DDI_SUCCESS);
		}
	}

	/*
	 * MSI interrupt allocation failed, or was not available.  Fallback to
	 * legacy interrupt support.
	 */
	if (state->hs_intr_types_avail & DDI_INTR_TYPE_FIXED) {
		status = mcxnex_add_intrs(state, DDI_INTR_TYPE_FIXED);
		if (status == DDI_SUCCESS) {
			state->hs_intr_type_chosen = DDI_INTR_TYPE_FIXED;
			return (DDI_SUCCESS);
		}
	}

	/*
	 * None of MSI, MSI-X, nor legacy interrupts were successful.
	 * Return failure.
	 */
	return (DDI_FAILURE);
}

/*
 * mcxnex_add_intrs()
 *    Context: Only called from attach() patch context
 */
static int
mcxnex_add_intrs(mcxnex_state_t *state, int intr_type)
{
	int	status;


	/* Get number of interrupts/MSI supported */
	status = ddi_intr_get_nintrs(state->hs_dip, intr_type,
	    &state->hs_intrmsi_count);
	if (status != DDI_SUCCESS) {
		return (DDI_FAILURE);
	}

	/* Get number of available interrupts/MSI */
	status = ddi_intr_get_navail(state->hs_dip, intr_type,
	    &state->hs_intrmsi_avail);
	if (status != DDI_SUCCESS) {
		return (DDI_FAILURE);
	}

	/* Ensure that we have at least one (1) usable MSI or interrupt */
	if ((state->hs_intrmsi_avail < 1) || (state->hs_intrmsi_count < 1)) {
		return (DDI_FAILURE);
	}

	/* Attempt to allocate the maximum #interrupt/MSI handles */
	status = ddi_intr_alloc(state->hs_dip, &state->hs_intrmsi_hdl[0],
	    intr_type, 0, min(MCXNEX_MSIX_MAX, state->hs_intrmsi_avail),
	    &state->hs_intrmsi_allocd, DDI_INTR_ALLOC_NORMAL);
	if (status != DDI_SUCCESS) {
		return (DDI_FAILURE);
	}

	/* Ensure that we have allocated at least one (1) MSI or interrupt */
	if (state->hs_intrmsi_allocd < 1) {
		return (DDI_FAILURE);
	}
	state->hs_eq_dist = state->hs_intrmsi_allocd - 1; /* start at 0 */

	/*
	 * Extract the priority for the allocated interrupt/MSI.  This
	 * will be used later when initializing certain mutexes.
	 */
	status = ddi_intr_get_pri(state->hs_intrmsi_hdl[0],
	    &state->hs_intrmsi_pri);
	if (status != DDI_SUCCESS) {
		/* Free the allocated interrupt/MSI handle */
		(void) ddi_intr_free(state->hs_intrmsi_hdl[0]);

		return (DDI_FAILURE);
	}

	/* Make sure the interrupt/MSI priority is below 'high level' */
	if (state->hs_intrmsi_pri >= ddi_intr_get_hilevel_pri()) {
		/* Free the allocated interrupt/MSI handle */
		(void) ddi_intr_free(state->hs_intrmsi_hdl[0]);

		return (DDI_FAILURE);
	}

	/* Get add'l capability information regarding interrupt/MSI */
	status = ddi_intr_get_cap(state->hs_intrmsi_hdl[0],
	    &state->hs_intrmsi_cap);
	if (status != DDI_SUCCESS) {
		/* Free the allocated interrupt/MSI handle */
		(void) ddi_intr_free(state->hs_intrmsi_hdl[0]);

		return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);
}


/*
 * mcxnex_intr_or_msi_fini()
 *    Context: Only called from attach() and/or detach() path contexts
 */
static int
mcxnex_intr_or_msi_fini(mcxnex_state_t *state)
{
	int	status;
	int	intr;

	for (intr = 0; intr < state->hs_intrmsi_allocd; intr++) {

		/* Free the allocated interrupt/MSI handle */
		status = ddi_intr_free(state->hs_intrmsi_hdl[intr]);
		if (status != DDI_SUCCESS) {
			return (DDI_FAILURE);
		}
	}
	return (DDI_SUCCESS);
}


/*ARGSUSED*/
void
mcxnex_pci_capability_msix(mcxnex_state_t *state, ddi_acc_handle_t hdl,
    uint_t offset)
{
	uint32_t	msix_data;
	uint16_t	msg_cntr;
	uint32_t	t_offset;	/* table offset */
	uint32_t	t_bir;
	uint32_t	p_offset;	/* pba */
	uint32_t	p_bir;
	int		t_size;		/* size in entries - each is 4 dwords */

	/* come in with offset pointing at the capability structure */

	msix_data = pci_config_get32(hdl, offset);
	cmn_err(CE_CONT, "Full cap structure dword = %X\n", msix_data);
	msg_cntr =  pci_config_get16(hdl, offset+2);
	cmn_err(CE_CONT, "MSIX msg_control = %X\n", msg_cntr);
	offset += 4;
	msix_data = pci_config_get32(hdl, offset);	/* table info */
	t_offset = (msix_data & 0xFFF8) >> 3;
	t_bir = msix_data & 0x07;
	offset += 4;
	cmn_err(CE_CONT, "  table %X --offset = %X, bir(bar) = %X\n",
	    msix_data, t_offset, t_bir);
	msix_data = pci_config_get32(hdl, offset);	/* PBA info */
	p_offset = (msix_data & 0xFFF8) >> 3;
	p_bir = msix_data & 0x07;

	cmn_err(CE_CONT, "  PBA   %X --offset = %X, bir(bar) = %X\n",
	    msix_data, p_offset, p_bir);
	t_size = msg_cntr & 0x7FF;		/* low eleven bits */
	cmn_err(CE_CONT, "    table size = %X entries\n", t_size);

	offset = t_offset;		/* reuse this for offset from BAR */
#ifdef MCXNEX_SUPPORTS_MSIX_BAR
	cmn_err(CE_CONT, "First 2 table entries behind BAR2 \n");
	for (i = 0; i < 2; i++) {
		for (j = 0; j < 4; j++, offset += 4) {
			msix_data = ddi_get32(state->hs_reg_msihdl,
			    (uint32_t *)((uintptr_t)state->hs_reg_msi_baseaddr
			    + offset));
			cmn_err(CE_CONT, "MSI table entry %d, dword %d == %X\n",
			    i, j, msix_data);
		}
	}
#endif

}

/*
 * X86 fastreboot support functions.
 * These functions are used to save/restore MSI-X table/PBA and also
 * to disable MSI-X interrupts in mcxnex_quiesce().
 */

/* Return the message control for MSI-X */
static ushort_t
get_msix_ctrl(dev_info_t *dip)
{
	ushort_t msix_ctrl = 0, caps_ctrl = 0;
	mcxnex_state_t *state = ddi_get_driver_private(dip);
	ddi_acc_handle_t pci_cfg_hdl = mcxnex_get_pcihdl(state);
	ASSERT(pci_cfg_hdl != NULL);

	if ((PCI_CAP_LOCATE(pci_cfg_hdl,
	    PCI_CAP_ID_MSI_X, &caps_ctrl) == DDI_SUCCESS)) {
		if ((msix_ctrl = PCI_CAP_GET16(pci_cfg_hdl, NULL, caps_ctrl,
		    PCI_MSIX_CTRL)) == PCI_CAP_EINVAL16)
			return (0);
	}
	ASSERT(msix_ctrl != 0);

	return (msix_ctrl);
}

/* Return the MSI-X table size */
static size_t
get_msix_tbl_size(dev_info_t *dip)
{
	ushort_t msix_ctrl = get_msix_ctrl(dip);
	ASSERT(msix_ctrl != 0);

	return (((msix_ctrl & PCI_MSIX_TBL_SIZE_MASK) + 1) *
	    PCI_MSIX_VECTOR_SIZE);
}

/* Return the MSI-X PBA size */
static size_t
get_msix_pba_size(dev_info_t *dip)
{
	ushort_t msix_ctrl = get_msix_ctrl(dip);
	ASSERT(msix_ctrl != 0);

	return (((msix_ctrl & PCI_MSIX_TBL_SIZE_MASK) + 64) / 64 * 8);
}

/* Set up the MSI-X table/PBA save area */
static void
mcxnex_set_msix_info(mcxnex_state_t *state)
{
	uint_t			rnumber, breg, nregs;
	ushort_t		caps_ctrl, msix_ctrl;
	pci_regspec_t		*rp;
	int			reg_size, addr_space, offset, *regs_list, i;

	/*
	 * MSI-X BIR Index Table:
	 * BAR indicator register (BIR) to Base Address register.
	 */
	uchar_t pci_msix_bir_index[8] = {0x10, 0x14, 0x18, 0x1c,
	    0x20, 0x24, 0xff, 0xff};

	/* Fastreboot data access  attribute */
	ddi_device_acc_attr_t	dev_attr = {
		0,				/* version */
		DDI_STRUCTURE_LE_ACC,
		DDI_STRICTORDER_ACC,		/* attr access */
		0
	};

	ddi_acc_handle_t pci_cfg_hdl = mcxnex_get_pcihdl(state);
	ASSERT(pci_cfg_hdl != NULL);

	if ((PCI_CAP_LOCATE(pci_cfg_hdl,
	    PCI_CAP_ID_MSI_X, &caps_ctrl) == DDI_SUCCESS)) {
		if ((msix_ctrl = PCI_CAP_GET16(pci_cfg_hdl, NULL, caps_ctrl,
		    PCI_MSIX_CTRL)) == PCI_CAP_EINVAL16)
			return;
	}
	ASSERT(msix_ctrl != 0);

	state->hs_msix_tbl_offset = PCI_CAP_GET32(pci_cfg_hdl, NULL, caps_ctrl,
	    PCI_MSIX_TBL_OFFSET);

	/* Get the BIR for MSI-X table */
	breg = pci_msix_bir_index[state->hs_msix_tbl_offset &
	    PCI_MSIX_TBL_BIR_MASK];
	ASSERT(breg != 0xFF);

	/* Set the MSI-X table offset */
	state->hs_msix_tbl_offset = state->hs_msix_tbl_offset &
	    ~PCI_MSIX_TBL_BIR_MASK;

	/* Set the MSI-X table size */
	state->hs_msix_tbl_size = ((msix_ctrl & PCI_MSIX_TBL_SIZE_MASK) + 1) *
	    PCI_MSIX_VECTOR_SIZE;

	if (ddi_prop_lookup_int_array(DDI_DEV_T_ANY, state->hs_dip,
	    DDI_PROP_DONTPASS, "reg", &regs_list, &nregs) !=
	    DDI_PROP_SUCCESS) {
		return;
	}
	reg_size = sizeof (pci_regspec_t) / sizeof (int);

	/* Check the register number for MSI-X table */
	for (i = 1, rnumber = 0; i < nregs/reg_size; i++) {
		rp = (pci_regspec_t *)&regs_list[i * reg_size];
		addr_space = rp->pci_phys_hi & PCI_ADDR_MASK;
		offset = PCI_REG_REG_G(rp->pci_phys_hi);

		if ((offset == breg) && ((addr_space == PCI_ADDR_MEM32) ||
		    (addr_space == PCI_ADDR_MEM64))) {
			rnumber = i;
			break;
		}
	}
	ASSERT(rnumber != 0);
	state->hs_msix_tbl_rnumber = rnumber;

	/* Set device attribute version and access according to Mcxnex FM */
	dev_attr.devacc_attr_version = mcxnex_devacc_attr_version(state);
	dev_attr.devacc_attr_access = mcxnex_devacc_attr_access(state);

	/* Map the entire MSI-X vector table */
	if (mcxnex_regs_map_setup(state, state->hs_msix_tbl_rnumber,
	    (caddr_t *)&state->hs_msix_tbl_addr, state->hs_msix_tbl_offset,
	    state->hs_msix_tbl_size, &dev_attr,
	    &state->hs_fm_msix_tblhdl) != DDI_SUCCESS) {
		ddi_prop_free(regs_list);
		return;
	}

	state->hs_msix_pba_offset = PCI_CAP_GET32(pci_cfg_hdl, NULL, caps_ctrl,
	    PCI_MSIX_PBA_OFFSET);

	/* Get the BIR for MSI-X PBA */
	breg = pci_msix_bir_index[state->hs_msix_pba_offset &
	    PCI_MSIX_PBA_BIR_MASK];
	ASSERT(breg != 0xFF);

	/* Set the MSI-X PBA offset */
	state->hs_msix_pba_offset = state->hs_msix_pba_offset &
	    ~PCI_MSIX_PBA_BIR_MASK;

	/* Set the MSI-X PBA size */
	state->hs_msix_pba_size =
	    ((msix_ctrl & PCI_MSIX_TBL_SIZE_MASK) + 64) / 64 * 8;

	/* Check the register number for MSI-X PBA */
	for (i = 1, rnumber = 0; i < nregs/reg_size; i++) {
		rp = (pci_regspec_t *)&regs_list[i * reg_size];
		addr_space = rp->pci_phys_hi & PCI_ADDR_MASK;
		offset = PCI_REG_REG_G(rp->pci_phys_hi);

		if ((offset == breg) && ((addr_space == PCI_ADDR_MEM32) ||
		    (addr_space == PCI_ADDR_MEM64))) {
			rnumber = i;
			break;
		}
	}
	ASSERT(rnumber != 0);
	state->hs_msix_pba_rnumber = rnumber;
	ddi_prop_free(regs_list);

	/* Map in the MSI-X Pending Bit Array */
	if (mcxnex_regs_map_setup(state, state->hs_msix_pba_rnumber,
	    (caddr_t *)&state->hs_msix_pba_addr, state->hs_msix_pba_offset,
	    state->hs_msix_pba_size, &dev_attr,
	    &state->hs_fm_msix_pbahdl) != DDI_SUCCESS) {
		mcxnex_regs_map_free(state, &state->hs_fm_msix_tblhdl);
		state->hs_fm_msix_tblhdl = NULL;
		return;
	}

	/* Set the MSI-X table save area */
	state->hs_msix_tbl_entries = kmem_alloc(state->hs_msix_tbl_size,
	    KM_SLEEP);

	/* Set the MSI-X PBA save area */
	state->hs_msix_pba_entries = kmem_alloc(state->hs_msix_pba_size,
	    KM_SLEEP);
}

/* Disable Mcxnex interrupts */
static int
mcxnex_intr_disable(mcxnex_state_t *state)
{
	ushort_t msix_ctrl = 0, caps_ctrl = 0;
	ddi_acc_handle_t pci_cfg_hdl = mcxnex_get_pcihdl(state);
	ddi_acc_handle_t msix_tblhdl = mcxnex_get_msix_tblhdl(state);
	int i, j;
	ASSERT(pci_cfg_hdl != NULL && msix_tblhdl != NULL);
	ASSERT(state->hs_intr_types_avail &
	    (DDI_INTR_TYPE_FIXED | DDI_INTR_TYPE_MSI | DDI_INTR_TYPE_MSIX));

	/*
	 * Check if MSI-X interrupts are used. If so, disable MSI-X interrupts.
	 * If not, since Mcxnex doesn't support MSI interrupts, assuming the
	 * legacy interrupt is used instead, disable the legacy interrupt.
	 */
	if ((state->hs_cfg_profile->cp_use_msi_if_avail != 0) &&
	    (state->hs_intr_types_avail & DDI_INTR_TYPE_MSIX)) {

		if ((PCI_CAP_LOCATE(pci_cfg_hdl,
		    PCI_CAP_ID_MSI_X, &caps_ctrl) == DDI_SUCCESS)) {
			if ((msix_ctrl = PCI_CAP_GET16(pci_cfg_hdl, NULL,
			    caps_ctrl, PCI_MSIX_CTRL)) == PCI_CAP_EINVAL16)
				return (DDI_FAILURE);
		}
		ASSERT(msix_ctrl != 0);

		if (!(msix_ctrl & PCI_MSIX_ENABLE_BIT))
			return (DDI_SUCCESS);

		/* Clear all inums in MSI-X table */
		for (i = 0; i < get_msix_tbl_size(state->hs_dip);
		    i += PCI_MSIX_VECTOR_SIZE) {
			for (j = 0; j < PCI_MSIX_VECTOR_SIZE; j += 4) {
				char *addr = state->hs_msix_tbl_addr + i + j;
				ddi_put32(msix_tblhdl,
				    (uint32_t *)(uintptr_t)addr, 0x0);
			}
		}

		/* Disable MSI-X interrupts */
		msix_ctrl &= ~PCI_MSIX_ENABLE_BIT;
		PCI_CAP_PUT16(pci_cfg_hdl, NULL, caps_ctrl, PCI_MSIX_CTRL,
		    msix_ctrl);

	} else {
		uint16_t cmdreg = pci_config_get16(pci_cfg_hdl, PCI_CONF_COMM);
		ASSERT(state->hs_intr_types_avail & DDI_INTR_TYPE_FIXED);

		/* Disable the legacy interrupts */
		cmdreg |= PCI_COMM_INTX_DISABLE;
		pci_config_put16(pci_cfg_hdl, PCI_CONF_COMM, cmdreg);
	}

	return (DDI_SUCCESS);
}

/* Mcxnex quiesce(9F) entry */
static int
mcxnex_quiesce(dev_info_t *dip)
{
	mcxnex_state_t *state = ddi_get_driver_private(dip);
	ddi_acc_handle_t pcihdl = mcxnex_get_pcihdl(state);
	ddi_acc_handle_t cmdhdl = mcxnex_get_cmdhdl(state);
	ddi_acc_handle_t msix_tbl_hdl = mcxnex_get_msix_tblhdl(state);
	ddi_acc_handle_t msix_pba_hdl = mcxnex_get_msix_pbahdl(state);
	uint32_t sem, reset_delay = state->hs_cfg_profile->cp_sw_reset_delay;
	uint64_t data64;
	uint32_t data32;
	int status, i, j, loopcnt;
	uint_t offset;

	ASSERT(state != NULL);

	/* start fastreboot */
	state->hs_quiescing = B_TRUE;

	/* If it's in maintenance mode, do nothing but return with SUCCESS */
	if (!MCXNEX_IS_OPERATIONAL(state->hs_operational_mode)) {
		return (DDI_SUCCESS);
	}

	/* suppress Mcxnex FM ereports */
	if (mcxnex_get_state(state) & HCA_EREPORT_FM) {
		mcxnex_clr_state_nolock(state, HCA_EREPORT_FM);
	}

	/* Shutdown HCA ports */
	if (mcxnex_hca_ports_shutdown(state,
	    state->hs_cfg_profile->cp_num_ports) != MCXNEX_CMD_SUCCESS) {
		state->hs_quiescing = B_FALSE;
		return (DDI_FAILURE);
	}

	/* Close HCA */
	if (mcxnex_close_hca_cmd_post(state, DDI_NOSLEEP) !=
	    MCXNEX_CMD_SUCCESS) {
		state->hs_quiescing = B_FALSE;
		return (DDI_FAILURE);
	}

	/* Disable interrupts */
	if (mcxnex_intr_disable(state) != DDI_SUCCESS) {
		state->hs_quiescing = B_FALSE;
		return (DDI_FAILURE);
	}

	/*
	 * Query the PCI capabilities of the HCA device, but don't process
	 * the VPD until after reset.
	 */
	if (mcxnex_pci_capability_list(state, pcihdl) != DDI_SUCCESS) {
		state->hs_quiescing = B_FALSE;
		return (DDI_FAILURE);
	}

	/*
	 * Read all PCI config info (reg0...reg63).  Note: According to the
	 * Mcxnex software reset application note, we should not read or
	 * restore the values in reg22 and reg23.
	 * NOTE:  For Mcxnex (and Arbel too) it says to restore the command
	 * register LAST, and technically, you need to restore the
	 * PCIE Capability "device control" and "link control" (word-sized,
	 * at offsets 0x08 and 0x10 from the capbility ID respectively).
	 * We hold off restoring the command register - offset 0x4 - till last
	 */

	/* 1st, wait for the semaphore assure accessibility - per PRM */
	status = -1;
	for (i = 0; i < NANOSEC/MICROSEC /* 1sec timeout */; i++) {
		sem = ddi_get32(cmdhdl, state->hs_cmd_regs.sw_semaphore);
		if (sem == 0) {
			status = 0;
			break;
		}
		drv_usecwait(1);
	}

	/* Check if timeout happens */
	if (status == -1) {
		state->hs_quiescing = B_FALSE;
		return (DDI_FAILURE);
	}

	/* MSI-X interrupts are used, save the MSI-X table */
	if (msix_tbl_hdl && msix_pba_hdl) {
		/* save MSI-X table */
		for (i = 0; i < get_msix_tbl_size(state->hs_dip);
		    i += PCI_MSIX_VECTOR_SIZE) {
			for (j = 0; j < PCI_MSIX_VECTOR_SIZE; j += 4) {
				char *addr = state->hs_msix_tbl_addr + i + j;
				data32 = ddi_get32(msix_tbl_hdl,
				    (uint32_t *)(uintptr_t)addr);
				*(uint32_t *)(uintptr_t)(state->
				    hs_msix_tbl_entries + i + j) = data32;
			}
		}
		/* save MSI-X PBA */
		for (i = 0; i < get_msix_pba_size(state->hs_dip); i += 8) {
			char *addr = state->hs_msix_pba_addr + i;
			data64 = ddi_get64(msix_pba_hdl,
			    (uint64_t *)(uintptr_t)addr);
			*(uint64_t *)(uintptr_t)(state->
			    hs_msix_pba_entries + i) = data64;
		}
	}

	/* save PCI config space */
	for (i = 0; i < MCXNEX_SW_RESET_NUMREGS; i++) {
		if ((i != MCXNEX_SW_RESET_REG22_RSVD) &&
		    (i != MCXNEX_SW_RESET_REG23_RSVD)) {
			state->hs_cfg_data[i]  =
			    pci_config_get32(pcihdl, i << 2);
		}
	}

	/* SW-reset HCA */
	ddi_put32(cmdhdl, state->hs_cmd_regs.sw_reset, MCXNEX_SW_RESET_START);

	/*
	 * This delay is required so as not to cause a panic here. If the
	 * device is accessed too soon after reset it will not respond to
	 * config cycles, causing a Master Abort and panic.
	 */
	drv_usecwait(reset_delay);

	/* Poll waiting for the device to finish resetting */
	loopcnt = 100;	/* 100 times @ 100 usec - total delay 10 msec */
	while ((pci_config_get32(pcihdl, 0) & 0x0000FFFF) != PCI_VENID_MLX) {
		drv_usecwait(MCXNEX_SW_RESET_POLL_DELAY);
		if (--loopcnt == 0)
			break;	/* just in case, break and go on */
	}
	if (loopcnt == 0) {
		state->hs_quiescing = B_FALSE;
		return (DDI_FAILURE);
	}

	/* Restore the config info */
	for (i = 0; i < MCXNEX_SW_RESET_NUMREGS; i++) {
		if (i == 1) continue;	/* skip the status/ctrl reg */
		if ((i != MCXNEX_SW_RESET_REG22_RSVD) &&
		    (i != MCXNEX_SW_RESET_REG23_RSVD)) {
			pci_config_put32(pcihdl, i << 2, state->hs_cfg_data[i]);
		}
	}

	/* If MSI-X interrupts are used, restore the MSI-X table */
	if (msix_tbl_hdl && msix_pba_hdl) {
		/* restore MSI-X PBA */
		for (i = 0; i < get_msix_pba_size(state->hs_dip); i += 8) {
			char *addr = state->hs_msix_pba_addr + i;
			data64 = *(uint64_t *)(uintptr_t)
			    (state->hs_msix_pba_entries + i);
			ddi_put64(msix_pba_hdl,
			    (uint64_t *)(uintptr_t)addr, data64);
		}
		/* restore MSI-X table */
		for (i = 0; i < get_msix_tbl_size(state->hs_dip);
		    i += PCI_MSIX_VECTOR_SIZE) {
			for (j = 0; j < PCI_MSIX_VECTOR_SIZE; j += 4) {
				char *addr = state->hs_msix_tbl_addr + i + j;
				data32 = *(uint32_t *)(uintptr_t)
				    (state->hs_msix_tbl_entries + i + j);
				ddi_put32(msix_tbl_hdl,
				    (uint32_t *)(uintptr_t)addr, data32);
			}
		}
	}

	/*
	 * PCI Express Capability - we saved during capability list, and
	 * we'll restore them here.
	 */
	offset = state->hs_pci_cap_offset;
	data32 = state->hs_pci_cap_devctl;
	pci_config_put32(pcihdl, offset + MCXNEX_PCI_CAP_DEV_OFFS, data32);
	data32 = state->hs_pci_cap_lnkctl;
	pci_config_put32(pcihdl, offset + MCXNEX_PCI_CAP_LNK_OFFS, data32);

	/* restore the command register */
	pci_config_put32(pcihdl, 0x04, (state->hs_cfg_data[1] | 0x0006));

	return (DDI_SUCCESS);
}

static dev_info_t *
mcxnex_find_child(dev_info_t *parent, const char *name, int addr)
{
	dev_info_t	*child;
	uint_t		nregs;
	int		*regs;
	int		reg;

	for (child = ddi_get_child(parent);
	    child != NULL;
	    child = ddi_get_next_sibling(child)) {
		if (strcmp(ddi_node_name(child), name) != 0)
			continue;
		if (ddi_prop_lookup_int_array(DDI_DEV_T_ANY, child,
		    DDI_PROP_DONTPASS, "reg", &regs, &nregs) !=
		    DDI_PROP_SUCCESS)
			continue;
		reg = *regs;
		ddi_prop_free(regs);
		if (addr == reg) {
			break;
		}
	}
	return (child);
}

static int
mcxnex_bus_config(dev_info_t *dip, uint_t flags, ddi_bus_config_op_t op,
    void *arg, dev_info_t **cdipp)
{
	mcxnex_state_t	*state = ddi_get_driver_private(dip);
	int		circ;

	ndi_devi_enter(dip, &circ);

	/*
	 * We want to register ethernet ports.  For now we hard code
	 * some basic assumptions -- that we are always handling
	 * ethernet for each port.  This assumption will probably need
	 * to be fixed later.
	 */
	for (int i = 0; i < state->hs_devlim.num_ports; i++) {
		if (!mcxnex_stat_cfg_is_port_enabled(state, i + 1))
			continue;	/* the port is disabled */

		if (mcxnex_attach_ether(state, i) != DDI_SUCCESS) {
			MCXNEX_WARNING(state,
			    "ethernet port %d not attached", i);
		}
	}

	ndi_devi_exit(dip, circ);

	return (ndi_busop_bus_config(dip, flags, op, arg, cdipp, 0));
}


static int
mcxnex_attach_ether(mcxnex_state_t *state, int port)
{
	/*
	 * We want to register ethernet ports.  For now we hard code
	 * some basic assumptions -- that we are always handling
	 * ethernet for each port.  This assumption will probably need
	 * to be fixed later.
	 */
	dev_info_t *cdip;
	dev_info_t *dip = state->hs_dip;

	if ((cdip = mcxnex_find_child(dip, "mcxe", port)) == NULL) {
		if (ndi_devi_alloc(dip, "mcxe", DEVI_SID_NODEID, &cdip) !=
		    NDI_SUCCESS) {
			MCXNEX_WARNING(state,
			    "ndi_devi_alloc failed for ethernet port %d",
			    port);
			return (DDI_FAILURE);
		}

		/* Setup properties for the children. */
		(void) ndi_prop_update_int(DDI_DEV_T_NONE, cdip, "pm-capable",
		    1);
		/* This property required for netboot! */
		(void) ndi_prop_update_string(DDI_DEV_T_NONE,  cdip, "model",
		    "Ethernet controller");
		(void) ndi_prop_update_int_array(DDI_DEV_T_NONE, cdip, "reg",
		    &port, 1);

		(void) ndi_devi_bind_driver(cdip, 0);
	}
	return (DDI_SUCCESS);
}

static int
mcxnex_bus_ctl(dev_info_t *dip, dev_info_t *rdip, ddi_ctl_enum_t ctlop,
    void *arg, void *result)
{
	switch (ctlop) {
	case DDI_CTLOPS_REPORTDEV:
	{
		cmn_err(CE_CONT, "?ConnectX2-device: %s@%s, %s%d\n",
		    ddi_node_name(rdip), ddi_get_name_addr(rdip),
		    ddi_driver_name(rdip), ddi_get_instance(rdip));

		return (DDI_SUCCESS);
	}

	case DDI_CTLOPS_INITCHILD:
	{
		dev_info_t	*child_dip = (dev_info_t *)arg;
		dev_info_t	*parent;
		mcxnex_state_t	*state;
		char		addr[16];
		int		port;
		int		*reg;
		uint_t		reglen;

		if (ddi_prop_lookup_int_array(DDI_DEV_T_ANY, child_dip,
		    DDI_PROP_DONTPASS, "reg", &reg, &reglen) !=
		    DDI_PROP_SUCCESS) {
			return (DDI_NOT_WELL_FORMED);
		}
		ASSERT(reglen == 1);
		port = *reg;
		ddi_prop_free(reg);

		if ((parent = ddi_get_parent(child_dip)) == NULL) {
			return (DDI_NOT_WELL_FORMED);
		}
		if ((state = ddi_get_driver_private(parent)) == NULL) {
			return (DDI_NOT_WELL_FORMED);
		}

		/* Stash a pointer to our own state. */
		ddi_set_parent_data(child_dip, &state->hs_ppd[port]);

		/*
		 * Stash the address in the devinfo node.
		 */
		(void) snprintf(addr, sizeof (addr), "%x", port);
		ddi_set_name_addr(child_dip, addr);

		return (DDI_SUCCESS);
	}

	case DDI_CTLOPS_UNINITCHILD:
	{
		dev_info_t	*child_dip = (dev_info_t *)arg;

		/*
		 * Don't use NDI prop remove, so that we retain the underlying
		 * HW_DEF properties... specifically "reg".
		 */
		ddi_prop_remove_all(child_dip);
		ddi_set_name_addr(child_dip, NULL);
		ddi_set_parent_data(child_dip, NULL);
		return (DDI_SUCCESS);
	}

	case DDI_CTLOPS_SIDDEV:
		/*
		 * Target devices are self-identifying.
		 */
		return (DDI_SUCCESS);

	case DDI_CTLOPS_AFFINITY:
		/*
		 * NB: We may want to revisit this later, so that functions
		 * on one card can see other functions on the same card.
		 * Right now there is no need.
		 */
		return (DDI_FAILURE);

	case DDI_CTLOPS_REPORTINT:
	case DDI_CTLOPS_POKE:
	case DDI_CTLOPS_PEEK:
	case DDI_CTLOPS_NREGS:
	case DDI_CTLOPS_REGSIZE:
		/*
		 * We don't support any of these (yet?).
		 */
		return (DDI_FAILURE);

	default:
		/*
		 * Everything else goes to the parent nexus.
		 */
		return (ddi_ctlops(dip, rdip, ctlop, arg, result));
	}
}

static int
mcxnex_bus_unconfig(dev_info_t *dip, uint_t flags, ddi_bus_config_op_t op,
    void *arg)
{
	/*
	 * The NDI_UNCONFIG flag allows the reference count on this nexus to be
	 * decremented when children's drivers are unloaded, enabling the nexus
	 * itself to be unloaded.
	 */
	return (ndi_busop_bus_unconfig(dip, flags | NDI_UNCONFIG, op, arg));
}

void
mcxnex_printf(mcxnex_state_t *state, int level, const char *fmt, ...)
{
	va_list	va;
	char	newfmt[128];

	(void) snprintf(newfmt, sizeof (newfmt), "!%s%d: %s",
	    ddi_driver_name(state->hs_dip), ddi_get_instance(state->hs_dip),
	    fmt);

	va_start(va, fmt);
	vcmn_err(level, newfmt, va);
	va_end(va);
}
