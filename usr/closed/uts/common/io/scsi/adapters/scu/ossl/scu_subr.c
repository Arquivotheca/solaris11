/*
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 *
 * This file may contain confidential information of Intel Corporation
 * and should not be distributed in source form without approval
 * from Oracle Legal.
 */

#include <sys/scsi/adapters/scu/scu_var.h>

/*
 * Local functions
 */
static	void scu_phy_get_sas_address(SCI_CONTROLLER_HANDLE_T,
    SCI_PHY_HANDLE_T, SCI_SAS_ADDRESS_T *);
static	void scu_tgtmap_activate_cb(void *, char *, scsi_tgtmap_tgt_type_t,
    void **tgt_privp);
static	boolean_t scu_tgtmap_deactivate_cb(void *, char *,
    scsi_tgtmap_tgt_type_t, void *, scsi_tgtmap_deact_rsn_t);
static	void scu_iport_report_tgts(void *);
static	void scu_discover(void *);
static	ddi_acc_handle_t *scu_get_pci_acc_handle(scu_ctl_t *, void *);
static	void *scu_look_up_md_by_pa(scu_subctl_t *, SCI_PHYSICAL_ADDRESS);

/*
 * This value is used for damap_create, which means # of quiescent
 * microseconds before report/map is stable.
 */
int scu_tgtmap_stable_usec = 1000000; /* 1 sec */

/*
 * This value can be tuned at /etc/system, the default value is
 * the domain discover time-out value multiplied by 1.5
 */
int scu_tgtmap_csync_usec = 0;

static int scu_tgtmap_error_usec = 10 * MICROSEC;
static int scu_tgtmap_error_thresh = 25;
static int scu_tgtmap_error_iops_thresh = 100;	/* Average of 100 IOPs */

extern	char scu_lib_log_buf[256];
extern	kmutex_t scu_lib_log_lock;

/*
 * SCU FMA
 */
static int scu_check_acc_handle(ddi_acc_handle_t);
static int scu_check_dma_handle(ddi_dma_handle_t);
static int scu_check_ctl_handle(scu_ctl_t *);
static int scu_check_scil_mem_handle(scu_ctl_t *);
static int scu_check_slot_handle(scu_io_slot_t *);

/*
 * Return the iport that ua is associated with
 */
scu_iport_t *
scu_get_iport_by_ua(scu_ctl_t *scu_ctlp, char *ua)
{
	scu_iport_t	*scu_iportp = NULL;

	mutex_enter(&scu_ctlp->scu_iport_lock);
	for (scu_iportp = list_head(&scu_ctlp->scu_iports);
	    scu_iportp != NULL;
	    scu_iportp = list_next(&scu_ctlp->scu_iports, scu_iportp)) {
		mutex_enter(&scu_iportp->scui_lock);
		if (strcmp(scu_iportp->scui_ua, ua) == 0) {
			mutex_exit(&scu_iportp->scui_lock);
			break;
		}
		mutex_exit(&scu_iportp->scui_lock);
	}
	mutex_exit(&scu_ctlp->scu_iport_lock);

	return (scu_iportp);
}

/*
 * Find the root phy for the supplied sas address
 */
scu_phy_t *
scu_find_phy_by_sas_address(scu_ctl_t *scu_ctlp, scu_iport_t *scu_iportp,
    char *sas_address)
{
	scu_phy_t	*scu_phyp;
	uint64_t	wwn;
	char		unit_address[SCU_MAX_UA_SIZE];
	int		i;

	SCUDBG(scu_ctlp, SCUDBG_PHY, SCUDBG_TRACE,
	    "%s: enter iport 0x%p sas_address %s",
	    __func__, (void *)scu_iportp, sas_address);

	scu_phyp = scu_ctlp->scu_root_phys;

	for (i = 0; i < scu_ctlp->scu_root_phy_num; i++) {
		if (scu_phyp->scup_iportp != scu_iportp)
			goto next_phy;

		wwn = scu_phyp->scup_remote_sas_address;
		(void *)scsi_wwn_to_wwnstr(wwn, 1, unit_address);

		if (strncmp(unit_address, sas_address,
		    strlen(unit_address)) == 0) {
			SCUDBG(scu_ctlp, SCUDBG_PHY, SCUDBG_INFO,
			    "%s: scu_phy 0x%p @ %s is found",
			    __func__, (void *)scu_phyp, sas_address);
			return (scu_phyp);
		}

next_phy:
		scu_phyp++;
	}

	SCUDBG(scu_ctlp, SCUDBG_PHY, SCUDBG_WARNING,
	    "%s: cannot find scu_phy for sas address %s",
	    __func__, sas_address);

	return (NULL);
}

/*
 * Find out the target for the supplied wwn
 */
scu_tgt_t *
scu_get_tgt_by_wwn(scu_ctl_t *scu_ctlp, uint64_t wwn)
{
	scu_tgt_t	*scu_tgtp = NULL;
	char		unit_address[SCU_MAX_UA_SIZE];
	int	i;

	(void *)scsi_wwn_to_wwnstr(wwn, 1, unit_address);

	mutex_enter(&scu_ctlp->scu_lock);
	for (i = 0; i < scu_ctlp->scu_max_dev; i++) {
		scu_tgtp = scu_ctlp->scu_tgts[i];
		if (scu_tgtp == NULL)
			continue;
		if (strncmp(unit_address, scu_tgtp->scut_unit_address,
		    strlen(unit_address)) == 0) {
			mutex_exit(&scu_ctlp->scu_lock);
			return (scu_tgtp);
		}
	}
	mutex_exit(&scu_ctlp->scu_lock);

	return (NULL);
}

/*
 * Check whether there is still target attached to the supplied iport
 */
/*ARGSUSED*/
boolean_t
scu_iport_has_tgts(scu_ctl_t *scu_ctlp, scu_iport_t *scu_iportp)
{
	scu_tgt_t	*scu_tgt = NULL;
	int	i;

	mutex_enter(&scu_ctlp->scu_lock);
	if (!scu_ctlp->scu_tgts || !scu_ctlp->scu_max_dev) {
		mutex_exit(&scu_ctlp->scu_lock);
		return (B_FALSE);
	}

	for (i = 0; i < scu_ctlp->scu_max_dev; i++) {
		scu_tgt = scu_ctlp->scu_tgts[i];
		if ((scu_tgt == NULL) || (scu_tgt->scut_phyp == NULL) ||
		    (scu_tgt->scut_phyp->scup_iportp != scu_iportp)) {
			continue;
		}

		mutex_exit(&scu_ctlp->scu_lock);
		return (B_TRUE);
	}

	mutex_exit(&scu_ctlp->scu_lock);
	return (B_FALSE);
}

/*
 * Time is too early to call scic_remote_device_get_sas_address(),
 * so we have to hack out the sas_address the same way as SCIC will do.
 */
/*ARGSUSED*/
static void
scu_phy_get_sas_address(SCI_CONTROLLER_HANDLE_T controller,
    SCI_PHY_HANDLE_T phy_handle, SCI_SAS_ADDRESS_T *sas_address)
{
	SCIC_PHY_PROPERTIES_T	scic_props;
	SCIC_SDS_PHY_T		*scic_phyp;

	sas_address->high = sas_address->low = 0;
	scic_phyp = (SCIC_SDS_PHY_T *)phy_handle;

	if (scic_phy_get_properties(phy_handle, &scic_props) == SCI_SUCCESS) {
		if (scic_props.protocols.u.bits.stp_target == 0) {
			scic_sds_phy_get_attached_sas_address(scic_phyp,
			    sas_address);
		} else {
			scic_sds_phy_get_sas_address(scic_phyp, sas_address);
			sas_address->low += scic_phyp->phy_index;
		}
	}
}

/*
 * activate_cb for sas_phymap_create
 */
void
scu_phymap_activate(void *arg, char *ua, void **privp)
{
	scu_ctl_t	*scu_ctlp = (scu_ctl_t *)arg;
	scu_iport_t	*scu_iportp = NULL;

	SCUDBG(scu_ctlp, SCUDBG_MAP, SCUDBG_TRACE,
	    "%s: enter unit address %s", __func__, ua);

	mutex_enter(&scu_ctlp->scu_lock);
	scu_ctlp->scu_phymap_active++;
	mutex_exit(&scu_ctlp->scu_lock);

	if (scsi_hba_iportmap_iport_add(scu_ctlp->scu_iportmap,
	    ua, NULL) != DDI_SUCCESS) {
		SCUDBG(scu_ctlp, SCUDBG_MAP, SCUDBG_ERROR,
		    "%s: failed to add iport handle on unit address %s",
		    __func__, ua);
	} else {
		SCUDBG(scu_ctlp, SCUDBG_MAP, SCUDBG_INFO,
		    "%s: add iport handle on unit address %s, "
		    "scu_phymap_active %d",
		    __func__, ua, scu_ctlp->scu_phymap_active);
	}

	/* Set the HBA soft state as the private data for this unit address */
	*privp = (void *)scu_ctlp;

	/*
	 * We are waiting on attach for this iport node
	 */
	scu_iportp = scu_get_iport_by_ua(scu_ctlp, ua);
	if (scu_iportp != NULL) {
		SCUDBG(scu_ctlp, SCUDBG_MAP, SCUDBG_INFO,
		    "%s: scu_iport 0x%p already exists on unit address %s",
		    __func__, (void *)scu_iportp, ua);
	}

	SCUDBG(scu_ctlp, SCUDBG_MAP, SCUDBG_TRACE,
	    "%s: exit unit address %s", __func__, ua);
}

/*
 * deactivate_cb for sas_phymap_create
 */
/*ARGSUSED*/
void
scu_phymap_deactivate(void *arg, char *ua, void *privp)
{
	scu_ctl_t	*scu_ctlp = (scu_ctl_t *)arg;
	scu_iport_t	*scu_iportp;

	SCUDBG(scu_ctlp, SCUDBG_MAP, SCUDBG_TRACE,
	    "%s: enter unit address %s", __func__, ua);

	mutex_enter(&scu_ctlp->scu_lock);
	scu_ctlp->scu_phymap_active--;
	mutex_exit(&scu_ctlp->scu_lock);

	if (scsi_hba_iportmap_iport_remove(scu_ctlp->scu_iportmap, ua)
	    != DDI_SUCCESS) {
		SCUDBG(scu_ctlp, SCUDBG_MAP, SCUDBG_ERROR,
		    "%s: failed to remote iport on unit address %s",
		    __func__, ua);
	} else {
		SCUDBG(scu_ctlp, SCUDBG_MAP, SCUDBG_INFO,
		    "%s: removed iport on unit address %s, "
		    "phymap_active %d",
		    __func__, ua, scu_ctlp->scu_phymap_active);
	}

	scu_iportp = scu_get_iport_by_ua(scu_ctlp, ua);
	if (scu_iportp == NULL) {
		SCUDBG(scu_ctlp, SCUDBG_MAP, SCUDBG_ERROR,
		    "%s: failed to look up of iport on unit address %s",
		    __func__, ua);
		return;
	}

	mutex_enter(&scu_iportp->scui_lock);
	scu_iportp->scui_ua_state = SCU_UA_INACTIVE;
	mutex_exit(&scu_iportp->scui_lock);

	SCUDBG(scu_ctlp, SCUDBG_MAP, SCUDBG_TRACE,
	    "%s: exit unit address %s", __func__, ua);
}

/*
 * Query the phymap and populate the iport handle passed in
 * note that the iport lock is held
 *
 * Note: At this time, we cannot set the association between iport
 * and lib domain because the domain may be still in discover process,
 * and cannot grab the domain by checking lib phy.
 *
 * Instead, the association is set up at scu_get_iport_by_domain.
 */
int
scu_iport_configure_phys(scu_iport_t *scu_iportp)
{
	scu_ctl_t		*scu_ctlp;
	scu_subctl_t		*scu_subctlp = NULL;
	scu_phy_t		*scu_phyp;
	int			phy_index;
	sas_phymap_phys_t	*phys_ptr;
	SCI_CONTROLLER_HANDLE_T	scic_ctlp;
	SCI_PHY_HANDLE_T	scic_phyp;
	uint8_t			lib_index;


	ASSERT(mutex_owned(&scu_iportp->scui_lock));
	scu_ctlp = scu_iportp->scui_ctlp;

	SCUDBG(scu_ctlp, SCUDBG_IPORT|SCUDBG_PHY, SCUDBG_TRACE,
	    "%s: enter iport 0x%p", __func__, (void *)scu_iportp);

	mutex_enter(&scu_ctlp->scu_lock);
	ASSERT(scu_ctlp->scu_root_phys != NULL);

	/*
	 * Query the phymap regarding the phys in this iport and
	 * populate this iport's phys list.
	 *
	 * Hereafter, the iport's phy list will be maintained via
	 * port up and down events
	 */
	ASSERT(list_is_empty(&scu_iportp->scui_phys));

	phys_ptr = sas_phymap_ua2phys(scu_ctlp->scu_phymap,
	    scu_iportp->scui_ua);
	ASSERT(phys_ptr != NULL);

	while ((phy_index = sas_phymap_phys_next(phys_ptr)) != -1) {
		/* Grab the phy pointer from root_phys */
		scu_phyp = scu_ctlp->scu_root_phys + phy_index;
		ASSERT(scu_phyp != NULL);
		ASSERT(scu_phyp->scup_hba_index == phy_index);

		scu_subctlp =
		    &(scu_ctlp->scu_subctls[phy_index / SCI_MAX_PHYS]);
		ASSERT(scu_subctlp == scu_phyp->scup_subctlp);

		scic_ctlp = scu_subctlp->scus_scic_ctl_handle;
		lib_index = SCU_HBA2LIB_PHY_INDEX(phy_index);
		if (scic_controller_get_phy_handle(scic_ctlp, lib_index,
		    &scic_phyp) != SCI_SUCCESS) {
			SCUDBG(scu_ctlp, SCUDBG_IPORT|SCUDBG_PHY, SCUDBG_ERROR,
			    "%s: cannot get phy handle for phy %d",
			    __func__, phy_index);
			continue;
		}

		SCUDBG(scu_ctlp, SCUDBG_IPORT|SCUDBG_PHY, SCUDBG_INFO,
		    "%s: scu_phyp 0x%p is configured to iport 0x%p",
		    __func__, (void *)scu_phyp, (void *)scu_iportp);

		/* Set the SCIL phy to this phy */
		scu_phyp->scup_lib_phyp = scic_phyp;

		/* Set the iport pointer to this phy */
		scu_phyp->scup_iportp = scu_iportp;

		/*
		 * Finally add the phy to the list
		 */
		scu_iportp->scui_phy_num++;
		list_insert_tail(&scu_iportp->scui_phys, scu_phyp);

		mutex_enter(&scu_phyp->scup_lock);
		scu_smhba_create_one_phy_stats(scu_iportp, scu_phyp);
		mutex_exit(&scu_phyp->scup_lock);
	}

	scu_iportp->scui_subctlp = scu_subctlp;

	mutex_exit(&scu_ctlp->scu_lock);
	sas_phymap_phys_free(phys_ptr);

	SCUDBG(scu_ctlp, SCUDBG_IPORT|SCUDBG_PHY, SCUDBG_TRACE,
	    "%s: exit iport 0x%p", __func__, (void *)scu_iportp);

	return (SCU_SUCCESS);
}

/*ARGSUSED*/
static void
scu_tgtmap_activate_cb(void *tgtmap_priv, char *tgt_addr,
    scsi_tgtmap_tgt_type_t tgt_type, void **tgt_privp)
{

	/* look up target */
}

/*ARGSUSED*/
static boolean_t
scu_tgtmap_deactivate_cb(void *tgtmap_priv, char *tgt_addr,
    scsi_tgtmap_tgt_type_t tgt_type, void *tgt_priv,
    scsi_tgtmap_deact_rsn_t tgt_deact_rsn)
{

	return (TRUE);
}

/*
 * Called by scu_iport_attach
 */
int
scu_iport_tgtmap_create(scu_iport_t *scu_iportp)
{
	scu_ctl_t		*scu_ctlp;
	scsi_tgtmap_params_t	scu_tgtmap_params;

	if (scu_iportp == NULL)
		return (SCU_FAILURE);
	else
		scu_ctlp = scu_iportp->scui_ctlp;

	SCUDBG(scu_ctlp, SCUDBG_IPORT|SCUDBG_MAP, SCUDBG_TRACE,
	    "%s: enter scu_iport 0x%p", __func__, (void *)scu_iportp);

	scu_tgtmap_params.csync_usec = scu_tgtmap_csync_usec;
	scu_tgtmap_params.stable_usec = scu_tgtmap_stable_usec;
	scu_tgtmap_params.error_usec = scu_tgtmap_error_usec;
	scu_tgtmap_params.error_thresh = scu_tgtmap_error_thresh;
	scu_tgtmap_params.error_iops_thresh = scu_tgtmap_error_iops_thresh;

	/* Create target map */

	/*
	 * Here we must be careful about the value of csync_usec.
	 *
	 * csync_usec value is selected based on how long it takes the HBA
	 * driver to get from map creation to initial observation for
	 * something already plugged in. Must estimate high, a low estimate
	 * can result in devices not showing up correctly on first reference.
	 *
	 * Therefore, we assign the value to the domain discover timeout
	 * multiplied by 1.5.
	 */
	if (scu_tgtmap_csync_usec == 0)
		scu_tgtmap_csync_usec = (int) \
		    1.5 * 1000 * scu_domain_discover_timeout * \
		    scif_domain_get_suggested_discover_timeout(NULL);
	if (scsi_hba_tgtmap_create(scu_iportp->scui_dip, SCSI_TM_FULLSET,
	    &scu_tgtmap_params, (void *)scu_iportp,
	    scu_tgtmap_activate_cb, scu_tgtmap_deactivate_cb,
	    &scu_iportp->scui_iss_tgtmap) != DDI_SUCCESS) {
		SCUDBG(scu_iportp->scui_ctlp, SCUDBG_IPORT|SCUDBG_MAP,
		    SCUDBG_ERROR|SCUDBG_TRACE,
		    "%s: failed to create tgtmap for scu_iport 0x%p",
		    __func__, (void *)scu_iportp);
		return (SCU_FAILURE);
	}

	SCUDBG(scu_ctlp, SCUDBG_IPORT|SCUDBG_MAP, SCUDBG_TRACE,
	    "%s: return successfully for scu_iport 0x%p",
	    __func__, (void *)scu_iportp);

	return (SCU_SUCCESS);
}

/*
 * Called by scu_iport_detach
 */
int
scu_iport_tgtmap_destroy(scu_iport_t *scu_iportp)
{
	scu_ctl_t	*scu_ctlp;

	if (scu_iportp == NULL)
		return (SCU_FAILURE);
	else
		scu_ctlp = scu_iportp->scui_ctlp;

	SCUDBG(scu_ctlp, SCUDBG_IPORT|SCUDBG_MAP, SCUDBG_TRACE,
	    "%s: enter scu_iport 0x%p", __func__, (void *)scu_iportp);

	if (scu_iportp->scui_iss_tgtmap == NULL) {
		SCUDBG(scu_ctlp, SCUDBG_IPORT|SCUDBG_MAP,
		    SCUDBG_ERROR|SCUDBG_TRACE,
		    "%s: failed due to no tgtmap for scu_iport 0x%p",
		    __func__, (void *)scu_iportp);
		return (SCU_FAILURE);
	}

	/* Destroy target map */
	scsi_hba_tgtmap_destroy(scu_iportp->scui_iss_tgtmap);

	SCUDBG(scu_ctlp, SCUDBG_IPORT|SCUDBG_MAP, SCUDBG_TRACE,
	    "%s: return successfully for scu_iport 0x%p",
	    __func__, (void *)scu_iportp);

	return (SCU_SUCCESS);
}

/*
 * FMA functions
 */
static int
scu_check_acc_handle(ddi_acc_handle_t handle)
{
	ddi_fm_error_t	de;

	if (handle == NULL) {
		return (DDI_FAILURE);
	}

	ddi_fm_acc_err_get(handle, &de, DDI_FME_VERSION);
	return (de.fme_status);
}

static int
scu_check_dma_handle(ddi_dma_handle_t handle)
{
	ddi_fm_error_t	de;

	if (handle == NULL) {
		return (DDI_FAILURE);
	}

	ddi_fm_dma_err_get(handle, &de, DDI_FME_VERSION);
	return (de.fme_status);
}

/*
 * FMA ereport
 */
int
scu_check_all_handle(scu_ctl_t *scu_ctlp)
{
	int		i, ctl_num;
	int		ctl_count;
	scu_io_slot_t	*io_slot;

	ctl_count = scu_ctlp->scu_lib_ctl_num;

	if (scu_check_ctl_handle(scu_ctlp) != DDI_SUCCESS) {
		SCUDBG(scu_ctlp, SCUDBG_FMA, SCUDBG_ERROR,
		    "%s: failed to check ctl handle", __func__);
		return (DDI_FAILURE);
	}
	if (scu_check_scil_mem_handle(scu_ctlp) != DDI_SUCCESS) {
		SCUDBG(scu_ctlp, SCUDBG_FMA, SCUDBG_ERROR,
		    "%s: failed to check scil_mem_handle",
		    __func__);
		return (DDI_FAILURE);
	}
	for (ctl_num = 0; ctl_num < ctl_count; ctl_num++) {
		io_slot = scu_ctlp->scu_subctls[ctl_num].scus_io_slots;
		for (i = 0; i < scu_ctlp->scu_subctls[ctl_num].scus_slot_num;
		    i++) {
			if (scu_check_slot_handle(io_slot)
			    != DDI_SUCCESS) {
				SCUDBG(scu_ctlp, SCUDBG_FMA, SCUDBG_ERROR,
				    "%s: failed to check %dctl "
				    "%dio_slot handle",
				    __func__, ctl_num, i);
				return (DDI_FAILURE);
			}
			io_slot++;
		}
	}
	return (DDI_SUCCESS);
}

void
scu_fm_ereport(scu_ctl_t *scu_ctlp, char *detail)
{
	char		buf[FM_MAX_CLASS];
	uint64_t	ena;

	(void) snprintf(buf, FM_MAX_CLASS, "%s.%s", DDI_FM_DEVICE, detail);

	ena = fm_ena_generate(0, FM_ENA_FMT1);

	if (DDI_FM_EREPORT_CAP(scu_ctlp->scu_fm_cap)) {
		ddi_fm_ereport_post(scu_ctlp->scu_dip, buf, ena, DDI_NOSLEEP,
		    FM_VERSION, DATA_TYPE_UINT8, FM_EREPORT_VERS0, NULL);
	}

}

static int
scu_check_ctl_handle(scu_ctl_t *scu_ctlp)
{
	int	i;

	if (scu_check_acc_handle(scu_ctlp->
	    scu_pcicfg_handle) != DDI_FM_OK) {
		return (DDI_FAILURE);
	}

	for (i = 0; i < SCU_MAX_BAR; i++) {
		if (scu_ctlp->scu_bar_map[i] == NULL)
			continue;
		if (scu_check_acc_handle(scu_ctlp->
		    scu_bar_map[i]) != DDI_FM_OK) {
			return (DDI_FAILURE);
		}
	}

	return (DDI_SUCCESS);
}

static int
scu_check_scil_mem_handle(scu_ctl_t *scu_ctlp)
{
	int	i, des_num, ctl_num, ctl_count;

	ctl_count = scu_ctlp->scu_lib_ctl_num;

	for (ctl_num = 0; ctl_num < ctl_count; ctl_num++) {
		des_num = scu_ctlp->scu_subctls[ctl_num].
		    scus_lib_memory_descriptor_count;
		for (i = 0; i < des_num; i++) {
			if (scu_check_acc_handle(scu_ctlp->scu_subctls[ctl_num].
			    scus_lib_memory_descriptors[i].
			    scu_lib_md_acc_handle)
			    != DDI_FM_OK || scu_check_dma_handle(
			    scu_ctlp->scu_subctls[ctl_num].
			    scus_lib_memory_descriptors[i].
			    scu_lib_md_dma_handle) != DDI_FM_OK) {
				return (DDI_FAILURE);
			}
		}
	}

	return (DDI_SUCCESS);
}

static int
scu_check_slot_handle(scu_io_slot_t *scu_slot)
{
	if (scu_check_acc_handle(scu_slot->
	    scu_io_acc_handle) != DDI_FM_OK ||
	    scu_check_dma_handle(scu_slot->
	    scu_io_dma_handle) != DDI_FM_OK) {
		return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);
}

/*
 * Discover routine - this is for the initial discovery
 *
 * This routine is called when controller start is completed, however,
 * at that time, the domain may be not in ready state yet, so sometimes
 * we will see scif_domain_discover failed.
 */
static void
scu_discover(void *arg)
{
	scu_subctl_t		*scu_subctlp = (scu_subctl_t *)arg;
	scu_ctl_t		*scu_ctlp;
	int			i;
	scu_domain_t		*scu_domain;
	SCI_DOMAIN_HANDLE_T	domain_handle;
	uint32_t		domain_timeout;
	SCI_STATUS		sci_status;

	scu_ctlp = scu_subctlp->scus_ctlp;
	SCUDBG(scu_ctlp, SCUDBG_HBA, SCUDBG_TRACE, "%s: enter", __func__);

	/* Now discovery can be started */
	for (i = 0; i < SCI_MAX_DOMAINS; i++) {
		scu_domain = &(scu_subctlp->scus_domains[i]);
		domain_handle = scu_domain->scu_lib_domain_handle;
		if (domain_handle == SCI_INVALID_HANDLE)
			continue;

		SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_DOMAIN, SCUDBG_INFO,
		    "%s: try to call scif_domain_discover for domain 0x%p "
		    "controller 0x%p", __func__, (void *)domain_handle,
		    (void *)scu_subctlp->scus_scif_ctl_handle);

		domain_timeout =
		    scu_domain_discover_timeout * \
		    scif_domain_get_suggested_discover_timeout(domain_handle);

		/*
		 * scif_domain_discover will return failure if domain is not
		 * in READY or STOPPED state
		 */
		if ((sci_status = scif_domain_discover(domain_handle,
		    domain_timeout, scu_device_discover_timeout))
		    != SCI_SUCCESS) {
			SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_DOMAIN,
			    SCUDBG_WARNING,
			    "%s: scif_domain_discover failed for domain 0x%p "
			    "sci_status %d controller 0x%p", __func__,
			    (void *)domain_handle, sci_status,
			    (void *)scu_subctlp->scus_scif_ctl_handle);
			continue;
		}
	}

	SCUDBG(scu_ctlp, SCUDBG_HBA, SCUDBG_TRACE, "%s: exit", __func__);
}

void
scu_set_pkt_reason(scu_ctl_t *scu_ctlp, scu_cmd_t *scu_cmdp,
    uchar_t reason, uint_t state, uint_t stat)
{
	SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_IO,
	    (reason == CMD_CMPLT) ? SCUDBG_TRACE : SCUDBG_INFO,
	    "%s: cmd 0x%p, reason 0x%x, state 0x%x, stat 0x%x",
	    __func__, (void *)scu_cmdp, reason, state, stat);

	ASSERT((scu_cmdp != NULL) && (scu_cmdp->cmd_pkt != NULL));

	scu_cmdp->cmd_pkt->pkt_reason = reason;
	scu_cmdp->cmd_pkt->pkt_state |= state;
	scu_cmdp->cmd_pkt->pkt_statistics |= stat;
}

/*
 * Callback of SCIF - will be invoked to allocate memory dynamically
 */
/*ARGSUSED*/
void
scif_cb_controller_allocate_memory(SCI_CONTROLLER_HANDLE_T controller,
    SCI_PHYSICAL_MEMORY_DESCRIPTOR_T *mde)
{

}

/*
 * Callback of SCIF - will be invoked to free memory dynamically
 */
/*ARGSUSED*/
void
scif_cb_controller_free_memory(SCI_CONTROLLER_HANDLE_T controller,
    SCI_PHYSICAL_MEMORY_DESCRIPTOR_T *mde)
{

}

/*
 * Callback of SCIF - inform the user that the controller has had
 * a serious unexpected error
 */
void
scif_cb_controller_error(SCI_CONTROLLER_HANDLE_T controller)
{
	scu_subctl_t	*scu_subctlp;
	scu_ctl_t	*scu_ctlp;

	scu_subctlp = sci_object_get_association(controller);
	ASSERT(scu_subctlp != NULL);
	scu_ctlp = scu_subctlp->scus_ctlp;

	SCUDBG(scu_ctlp, SCUDBG_HBA, SCUDBG_TRACE,
	    "%s: enter SCIF controller 0x%p",
	    __func__, (void *)controller);

	/*
	 * Dispatch recover task
	 */
	scu_controller_error(scu_subctlp);
}

/*
 * Callback of SCIF - it will set a flag to indicate topology discovery
 * can be started
 *
 * The routine is the callback when scif_controller_start is completed
 */
void
scif_cb_controller_start_complete(SCI_CONTROLLER_HANDLE_T controller,
    SCI_STATUS complete_status)
{
	scu_subctl_t		*scu_subctlp;
	scu_ctl_t		*scu_ctlp;
	SCI_CONTROLLER_HANDLE_T	scic_ctlp;
	SCIC_SDS_PHY_T		*scic_phyp;
	SCI_SAS_ADDRESS_T	local_phy_sas_address;
	SCI_SAS_ADDRESS_T	remote_phy_sas_address;
	uint64_t		local_sas_address;
	uint64_t		remote_sas_address;
	int			i, count = 0;

	scu_subctlp = sci_object_get_association(controller);
	ASSERT(scu_subctlp != NULL);
	scu_ctlp = scu_subctlp->scus_ctlp;

	SCUDBG(scu_ctlp, SCUDBG_HBA, SCUDBG_TRACE|SCUDBG_INFO,
	    "%s: enter scu_subctlp 0x%p SCIF controller 0x%p "
	    "complete_status %d", __func__, (void *)scu_subctlp,
	    (void *)controller, complete_status);

	/* controller_start finished */
	scu_subctlp->scus_adapter_is_ready = 1;

	/*
	 * Continue to go through since controller will be ready
	 * even start got time-out
	 */
	if (complete_status != SCI_SUCCESS) {
		scu_subctlp->scus_lib_start_timeout = 1;
		SCUDBG(scu_ctlp, SCUDBG_HBA, SCUDBG_ERROR,
		    "%s: controller 0x%p start got time-out, "
		    "complete_status %d", __func__,
		    (void *)controller, complete_status);
	}

	/* Now set up phymap related operation */
	scic_ctlp = scu_subctlp->scus_scic_ctl_handle;

	/* Report activated root phys since controller start is completed */
	for (i = 0; i < SCI_MAX_PHYS; i++) {
		if (scic_controller_get_phy_handle(scic_ctlp, i,
		    (SCI_PHY_HANDLE_T *)&scic_phyp) != SCI_SUCCESS) {
			SCUDBG(scu_ctlp, SCUDBG_HBA, SCUDBG_ERROR,
			    "%s: cannot get phy handle for phy %d",
			    __func__, i);
			continue;
		}

		/*
		 * Here we cannot simply report those phys which are in ready
		 * state because SCIL supports staggered spin up and probably
		 * phy is still in waiting queue for power control when
		 * controller start is completed
		 */
		if (scic_phyp->protocol == SCIC_SDS_PHY_PROTOCOL_SAS ||
		    scic_phyp->protocol == SCIC_SDS_PHY_PROTOCOL_SATA) {

			/* Get local phy sas address */
			scic_sds_phy_get_sas_address(scic_phyp,
			    &local_phy_sas_address);

			/* Get remote phy sas address */
			scu_phy_get_sas_address(scic_ctlp, scic_phyp,
			    &remote_phy_sas_address);

			/* Notice whether swap is needed */
			local_sas_address = SCU_SAS_ADDRESS(
			    BE_32(local_phy_sas_address.high),
			    BE_32(local_phy_sas_address.low));
			remote_sas_address = SCU_SAS_ADDRESS(
			    BE_32(remote_phy_sas_address.high),
			    BE_32(remote_phy_sas_address.low));

			/* Add phy to phymap */
			(void) sas_phymap_phy_add(scu_ctlp->scu_phymap,
			    SCU_LIB2HBA_PHY_INDEX(scu_subctlp->scus_num,
			    scic_phyp->phy_index),
			    local_sas_address, remote_sas_address);

			SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_MAP,
			    SCUDBG_INFO,
			    "%s: sas_phymap_phy_add phy_index %d, "
			    "local_sas_address 0x%"PRIx64", "
			    "remote_sas_address 0x%"PRIx64", %s",
			    __func__, scic_phyp->phy_index,
			    local_sas_address, remote_sas_address,
			    (scic_phyp->protocol ==
			    SCIC_SDS_PHY_PROTOCOL_SAS) ?  "SAS" : "SATA");

			count++;
		}
	}

	SCUDBG(scu_ctlp, SCUDBG_HBA, SCUDBG_INFO,
	    "%s: got %d active phys", __func__, count);

	if (scu_subctlp->scus_resetting) {
		SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_RECOVER, SCUDBG_INFO,
		    "%s: subctl 0x%p ready",
		    __func__, (void *)scu_subctlp);

		scu_subctlp->scus_failed = \
		    (complete_status == SCI_SUCCESS) ? 0 : 1;
		cv_signal(&scu_subctlp->scus_reset_complete_cv);
	}

	/*
	 * Dispatch discover taskq
	 */
	if (ddi_taskq_dispatch(scu_ctlp->scu_discover_taskq, scu_discover,
	    scu_subctlp, DDI_SLEEP) != DDI_SUCCESS) {
		SCUDBG(scu_ctlp, SCUDBG_HBA, SCUDBG_ERROR|SCUDBG_TRACE,
		    "%s: ddi_taskq_dispatch failed for scu_discover routine",
		    __func__);
	}

	SCUDBG(scu_ctlp, SCUDBG_HBA, SCUDBG_TRACE,
	    "%s: exit scu_subctlp 0x%p SCIF controller 0x%p",
	    __func__, (void *)scu_subctlp, (void *)controller);
}

/*
 * Callback of SCIF
 */
/*ARGSUSED*/
void
scif_cb_controller_stop_complete(SCI_CONTROLLER_HANDLE_T controller,
    SCI_STATUS completion_status)
{
	scu_subctl_t		*scu_subctlp;
	scu_ctl_t		*scu_ctlp;

	scu_subctlp = sci_object_get_association(controller);
	ASSERT(scu_subctlp != NULL);
	scu_ctlp = scu_subctlp->scus_ctlp;

	SCUDBG(scu_ctlp, SCUDBG_HBA, SCUDBG_TRACE|SCUDBG_INFO,
	    "%s: enter scu_subctlp 0x%p SCIF controller 0x%p "
	    "complete_status %d", __func__, (void *)scu_subctlp,
	    (void *)controller, completion_status);

	if (completion_status == SCI_SUCCESS) {
		scu_subctlp->scus_adapter_is_ready = 0;
		scu_subctlp->scus_stopped = 1;
	}

	SCUDBG(scu_ctlp, SCUDBG_HBA, SCUDBG_TRACE,
	    "%s: exit scu_subctlp 0x%p SCIF controller 0x%p",
	    __func__, (void *)scu_subctlp, (void *)controller);
}

/*
 * Discovery routine for the supplied domain
 */
static void
scu_domain_discover(void *arg)
{
	SCI_DOMAIN_HANDLE_T	domain_handle = (SCI_DOMAIN_HANDLE_T)arg;
	SCIF_SAS_DOMAIN_T	*fw_domain;
	SCI_CONTROLLER_HANDLE_T	controller;
	uint32_t		domain_timeout;
	scu_iport_t		*scu_iportp;
	scu_subctl_t		*scu_subctlp;
	scu_ctl_t		*scu_ctlp;

	scu_iportp = sci_object_get_association(domain_handle);

	fw_domain = (SCIF_SAS_DOMAIN_T *)domain_handle;
	controller = fw_domain->controller;
	scu_subctlp = sci_object_get_association(controller);
	scu_ctlp = scu_subctlp->scus_ctlp;

	SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_DOMAIN, SCUDBG_TRACE,
	    "%s: enter domain 0x%p iport 0x%p controller 0x%p",
	    __func__, (void *)domain_handle, (void *)scu_iportp,
	    (void *)controller);

	domain_timeout =
	    scu_domain_discover_timeout * \
	    scif_domain_get_suggested_discover_timeout(domain_handle);

	if (scif_domain_discover(domain_handle, domain_timeout,
	    scu_device_discover_timeout) != SCI_SUCCESS) {
		SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_DOMAIN, SCUDBG_ERROR,
		    "%s: scif_domain_discover failed for domain 0x%p "
		    "controller 0x%p", __func__, (void *)domain_handle,
		    (void *)controller);
	}

	SCUDBG(scu_ctlp, SCUDBG_HBA|SCUDBG_DOMAIN, SCUDBG_TRACE,
	    "%s: exit domain 0x%p controller 0x%p", __func__,
	    (void *)domain_handle, (void *)controller);
}

/*
 * Callback of SCIF - inform the user that something in the supplied
 * domain has changed
 *
 * The routine will be called:
 *	scif_sas_domain_ready_state_enter
 *	scif_sas_domain_stopped_state_enter
 *	scic_cb_port_bc_change_primitive_recieved
 *	scif_sas_remote_device_target_reset_complete
 *
 * Discover process should be kicked off upon this callback except
 * the initial discovery.
 *
 * scif_cb_controller_start_complete will trigger the initial topology
 * discovery.
 */
void
scif_cb_domain_change_notification(SCI_CONTROLLER_HANDLE_T controller,
    SCI_DOMAIN_HANDLE_T domain)
{
	scu_subctl_t	*scu_subctlp;
	scu_ctl_t	*scu_ctlp;

	scu_subctlp = sci_object_get_association(controller);
	ASSERT(scu_subctlp != NULL);
	scu_ctlp = scu_subctlp->scus_ctlp;

	SCUDBG(scu_ctlp, SCUDBG_DOMAIN, SCUDBG_TRACE,
	    "%s: enter domain 0x%p controller 0x%p",
	    __func__, (void *)domain, (void *)controller);

	/*
	 * Do nothing when controller in starting process
	 */
	if (scu_subctlp->scus_adapter_is_ready == 0) {
		SCUDBG(scu_ctlp, SCUDBG_DOMAIN,
		    SCUDBG_TRACE,
		    "%s: enter while sub-controller 0x%p is not ready, "
		    "domain 0x%p controller 0x%p", __func__,
		    (void *)scu_subctlp, (void *)domain, (void *)controller);
		return;
	} else {

		/* Kick off a domain discovery taskq */
		if (ddi_taskq_dispatch(scu_ctlp->scu_discover_taskq,
		    scu_domain_discover, domain, DDI_SLEEP) != DDI_SUCCESS) {
			SCUDBG(scu_ctlp, SCUDBG_HBA, SCUDBG_ERROR|SCUDBG_TRACE,
			    "%s: ddi_taskq_dispatch failed for domain 0x%p "
			    "discover controller 0x%p", __func__,
			    (void *)domain, (void *)controller);
		} else {
			SCUDBG(scu_ctlp, SCUDBG_HBA, SCUDBG_INFO,
			    "%s: ddi_taskq_dispatch for domain 0x%p discover "
			    "controller 0x%p", __func__, (void *)domain,
			    (void *)controller);
		}
	}
}

/*
 * Callback of SCIF - inform the user that a new direct attached device
 * was found in the domain
 */
void
scif_cb_domain_da_device_added(SCI_CONTROLLER_HANDLE_T controller,
    SCI_DOMAIN_HANDLE_T domain, SCI_SAS_ADDRESS_T * sas_address,
    SCI_SAS_IDENTIFY_ADDRESS_FRAME_PROTOCOLS_T * protocols)
{
	scu_subctl_t			*scu_subctlp;
	scu_ctl_t			*scu_ctlp;
	SCI_REMOTE_DEVICE_HANDLE_T	device_handle;
	void				*device;
	uint64_t			wwn;
	scu_tgt_t			*scu_tgtp;

	scu_subctlp = sci_object_get_association(controller);
	ASSERT(scu_subctlp != NULL);
	scu_ctlp = scu_subctlp->scus_ctlp;

	wwn = SCU_SAS_ADDRESS(BE_32(sas_address->high),
	    BE_32(sas_address->low));

	SCUDBG(scu_ctlp, SCUDBG_DOMAIN, SCUDBG_TRACE,
	    "%s: enter domain 0x%p controller 0x%p",
	    __func__, (void *)domain, (void *)controller);

	scu_tgtp = scu_get_tgt_by_wwn(scu_ctlp, wwn);
	mutex_enter(&scu_subctlp->scus_lib_remote_device_lock);
	if (scu_tgtp) {
		device = scu_tgtp->scut_lib_remote_device;
		if (device != NULL) {
			(void) memset(device, 0,
			    scif_remote_device_get_object_size());
		} else {
			SCUDBG(scu_ctlp, SCUDBG_DOMAIN, SCUDBG_ERROR,
			    "%s: target0x%p got a NULL pointer for "
			    "member scut_lib_remote_device",
			    __func__, (void *)scu_tgtp);

		}
	} else {
		device = kmem_zalloc(scif_remote_device_get_object_size(),
		    KM_SLEEP);
	}
	/* construct SCIF remote device struct */
	scif_remote_device_construct(domain, device, &device_handle);

	/* Now construct direct attached remote device struct */
	if (scif_remote_device_da_construct(device_handle, sas_address,
	    protocols) != SCI_SUCCESS) {
		SCUDBG(scu_ctlp, SCUDBG_DOMAIN, SCUDBG_ERROR,
		    "%s: scif_remote_device_da_construct failure "
		    "for domain 0x%p controller 0x%p",
		    __func__, (void *)domain, (void *)controller);
	}
	mutex_exit(&scu_subctlp->scus_lib_remote_device_lock);

	SCUDBG(scu_ctlp, SCUDBG_DOMAIN, SCUDBG_TRACE,
	    "%s: exit domain 0x%p controller 0x%p",
	    __func__, (void *)domain, (void *)controller);
}

/*
 * Callback of SCIF - inform the user that a device has been removed from
 * the domain.
 */
/*ARGSUSED*/
void
scif_cb_domain_device_removed(SCI_CONTROLLER_HANDLE_T controller,
    SCI_DOMAIN_HANDLE_T domain, SCI_REMOTE_DEVICE_HANDLE_T remote_device)
{
	scu_subctl_t	*scu_subctlp;
	scu_ctl_t	*scu_ctlp;
	scu_tgt_t	*scu_tgtp;
	void		*device;

	scu_subctlp = sci_object_get_association(controller);
	ASSERT(scu_subctlp != NULL);
	scu_ctlp = scu_subctlp->scus_ctlp;

	SCUDBG(scu_ctlp, SCUDBG_DOMAIN|SCUDBG_HOTPLUG, SCUDBG_TRACE,
	    "%s: enter domain 0x%p controller 0x%p remote_device 0x%p",
	    __func__, (void *)domain, (void *)controller,
	    (void *)remote_device);

	scu_tgtp = sci_object_get_association(remote_device);
	if (scu_tgtp != NULL) {
		mutex_enter(&scu_tgtp->scut_lock);
		scu_tgtp->scut_lib_tgt_valid = 0;
		scu_tgtp->scut_lib_remote_device = NULL;
		SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_HOTPLUG, SCUDBG_INFO,
		    "%s: SCIF remote device of scu_tgt 0x%p will gone",
		    __func__, (void *)scu_tgtp);
		mutex_exit(&scu_tgtp->scut_lock);
	} else {
		SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_HOTPLUG, SCUDBG_WARNING,
		    "%s: no associated scu_tgt for remote device 0x%p",
		    __func__, (void *)remote_device);
	}

	mutex_enter(&scu_subctlp->scus_lib_remote_device_lock);
	device = remote_device;

	/* Call SCIF remote device destruct */
	(void) scif_remote_device_destruct(remote_device);

	kmem_free(device, scif_remote_device_get_object_size());
	mutex_exit(&scu_subctlp->scus_lib_remote_device_lock);

	SCUDBG(scu_ctlp, SCUDBG_DOMAIN|SCUDBG_HOTPLUG, SCUDBG_TRACE,
	    "%s: exit domain 0x%p controller 0x%p remote_device 0x%p",
	    __func__, (void *)domain, (void *)controller,
	    (void *)remote_device);
}

SCI_DOMAIN_HANDLE_T
scu_get_domain_by_iport(scu_ctl_t *scu_ctlp, scu_iport_t *scu_iportp)
{
	SCI_DOMAIN_HANDLE_T	scif_domain = SCI_INVALID_HANDLE;
	SCIF_SAS_DOMAIN_T	*fw_domain;
	scu_phy_t		*scu_phyp;
	SCIC_SDS_PHY_T		*scic_phy;
	SCIC_SDS_PORT_T		*scic_port;
	uint32_t		state;

	if (scu_iportp->scui_lib_domain != SCI_INVALID_HANDLE)
		return (scu_iportp->scui_lib_domain);

	scu_phyp = scu_iportp->scui_primary_phy;
	if (scu_phyp == NULL)
		return (scif_domain);

	scic_phy = (SCIC_SDS_PHY_T *)scu_phyp->scup_lib_phyp;
	if (scic_phy == SCI_INVALID_HANDLE)
		return (scif_domain);

	scic_port = (SCIC_SDS_PORT_T *)scic_phy->owning_port;
	if (scic_port == SCI_INVALID_HANDLE)
		return (scif_domain);

	scif_domain = sci_object_get_association(scic_port);
	fw_domain = (SCIF_SAS_DOMAIN_T *)scif_domain;

	state =
	    sci_base_state_machine_get_state(&fw_domain->parent.state_machine);
	if (state == SCI_BASE_DOMAIN_STATE_DISCOVERING) {
		if (fw_domain->operation.status == SCI_FAILURE_TIMEOUT) {
			SCUDBG(scu_ctlp, SCUDBG_DOMAIN|SCUDBG_IPORT,
			    SCUDBG_WARNING,
			    "%s: iport 0x%p associated domain 0x%p "
			    "get discovery time-out", __func__,
			    (void *)scu_iportp, (void *)scif_domain);
			scif_domain = SCI_INVALID_HANDLE;
		}
	}

	/*
	 * The association between iport and domain is set up in
	 * scu_iport_report_tgts after scif_domain_discover is completed
	 */
	SCUDBG(scu_ctlp, SCUDBG_DOMAIN|SCUDBG_IPORT,
	    SCUDBG_TRACE|SCUDBG_WARNING,
	    "%s: the association between iport 0x%p and domain 0x%p "
	    "not set up yet, domain's state machine state: 0x%x",
	    __func__, (void *)scu_iportp, (void *)scif_domain, state);

	return (scif_domain);
}


/*
 * note that scu_iport_lock is held
 *
 * This routine is called under below scenario:
 *	scu_iport_report_tgts
 *
 */
static scu_iport_t *
scu_get_iport_by_domain(scu_ctl_t *scu_ctlp, SCI_DOMAIN_HANDLE_T domain)
{
	scu_iport_t		*scu_iportp = NULL;
	SCI_PORT_HANDLE_T	lib_port;
	SCIC_SDS_PORT_T		*scic_port;
	SCIC_SDS_PHY_T		*scic_phy;
	scu_phy_t		*scu_phyp;
	uint8_t			hba_index;
	uint8_t			controller_index;
	scu_subctl_t		*scu_subctlp;
	SCI_SAS_ADDRESS_T	sas_address;

	SCUDBG(scu_ctlp, SCUDBG_DOMAIN|SCUDBG_IPORT, SCUDBG_TRACE,
	    "%s: enter domain 0x%p", __func__, (void *)domain);

	scu_iportp = sci_object_get_association(domain);
	if (scu_iportp != NULL)
		goto exit;

	lib_port = scif_domain_get_scic_port_handle(domain);
	if (lib_port == SCI_INVALID_HANDLE) {
		SCUDBG(scu_ctlp, SCUDBG_DOMAIN|SCUDBG_IPORT,
		    SCUDBG_INFO,
		    "%s: no scic port assigned to domain 0x%p yet",
		    __func__, (void *)domain);
		goto exit;
	}

	scic_port = (SCIC_SDS_PORT_T *)lib_port;
	scic_phy = scic_sds_port_get_a_connected_phy(scic_port);
	if (scic_phy == NULL) {
		SCUDBG(scu_ctlp, SCUDBG_DOMAIN|SCUDBG_IPORT,
		    SCUDBG_INFO,
		    "%s: no active phy for scic port 0x%p domain 0x%p",
		    __func__, (void *)lib_port, (void *)domain);
		goto exit;
	}

	controller_index =
	    scic_phy->owning_port->owning_controller->controller_index;
	hba_index =
	    SCU_LIB2HBA_PHY_INDEX(controller_index, scic_phy->phy_index);
	scu_phyp = scu_ctlp->scu_root_phys + hba_index;
	ASSERT(scu_phyp != NULL);
	ASSERT(scu_phyp->scup_lib_index == scic_phy->phy_index);

	scu_iportp = scu_phyp->scup_iportp;
	if (scu_iportp == NULL) {
		SCUDBG(scu_ctlp, SCUDBG_DOMAIN|SCUDBG_IPORT,
		    SCUDBG_INFO,
		    "%s: iport not created yet, domain 0x%p",
		    __func__, (void *)domain);
		goto exit;
	}

	mutex_enter(&scu_iportp->scui_lock);

	SCUDBG(scu_ctlp, SCUDBG_DOMAIN|SCUDBG_IPORT, SCUDBG_INFO,
	    "%s: iport 0x%p found, domain 0x%p",
	    __func__, (void *)scu_iportp, (void *)domain);

	ASSERT(scu_phyp->scup_lib_phyp == scic_phy);

	/* Get scic phy properties */
	(void) scic_phy_get_properties(scic_phy, &scu_phyp->scup_lib_phy_prop);

	scu_subctlp = &(scu_ctlp->scu_subctls[controller_index]);

	/* Get scic phy remote sas address */
	scu_phy_get_sas_address(scu_subctlp->scus_scic_ctl_handle,
	    scic_phy, &sas_address);

	scu_phyp->scup_remote_sas_address =
	    SCU_SAS_ADDRESS(BE_32(sas_address.high), BE_32(sas_address.low));

	/* Update phy status */
	scu_phyp->scup_ready = 1;

	SCUDBG(scu_ctlp, SCUDBG_PHY, SCUDBG_INFO,
	    "%s: scu_phyp 0x%p remote_sas_address 0x%"PRIx64,
	    __func__, (void *)scu_phyp, scu_phyp->scup_remote_sas_address);

	/*
	 * If the phy is the primary, then set it to the iport
	 */
	if (scu_phyp->scup_wide_port == 0) {
		scu_iportp->scui_primary_phy = scu_phyp;

		/*
		 * Set the association between iport and SCIF domain
		 */
		(void) sci_object_set_association(domain, scu_iportp);
		scu_iportp->scui_lib_domain = domain;
	}

	mutex_exit(&scu_iportp->scui_lock);

exit:
	SCUDBG(scu_ctlp, SCUDBG_DOMAIN|SCUDBG_IPORT, SCUDBG_TRACE,
	    "%s: exit domain 0x%p iport 0x%p",
	    __func__, (void *)domain, (void *)scu_iportp);

	return (scu_iportp);
}

/*
 * Called by scif_cb_domain_discovery_complete
 */
static void
scu_iport_report_tgts(void *arg)
{
	SCI_DOMAIN_HANDLE_T		domain = (SCI_DOMAIN_HANDLE_T)arg;
	SCI_CONTROLLER_HANDLE_T		controller;
	SCIF_SAS_DOMAIN_T		*fw_domain;
	scu_subctl_t			*scu_subctlp;
	scu_ctl_t			*scu_ctlp;
	scu_iport_t			*scu_iportp;
	scu_phy_t			*primary_phy;
	SCI_ABSTRACT_ELEMENT_T		*current_element;
	SCIF_SAS_REMOTE_DEVICE_T	*current_device;
	scsi_hba_tgtmap_t		*tgtmap;
	scsi_tgtmap_tgt_type_t		tgt_type;
	SCI_SAS_ADDRESS_T		sas_address;
	SMP_DISCOVER_RESPONSE_PROTOCOLS_T	dev_protocols;
	char				*ua, *ap;
	uint64_t			wwn;
	int				ea = 0;
	int				loop = 0;
	uint32_t			state;

	fw_domain = (SCIF_SAS_DOMAIN_T *)domain;
	controller = fw_domain->controller;
	scu_subctlp =
	    (scu_subctl_t *)sci_object_get_association(controller);

	scu_ctlp = scu_subctlp->scus_ctlp;
	SCUDBG(scu_ctlp, SCUDBG_DOMAIN|SCUDBG_IPORT, SCUDBG_TRACE,
	    "%s: enter domain 0x%p", __func__, (void *)domain);

	/* Do nothing when controller in starting process */
	mutex_enter(&scu_ctlp->scu_lock);
	if (scu_subctlp->scus_adapter_is_ready == 0) {
		SCUDBG(scu_ctlp, SCUDBG_DOMAIN|SCUDBG_IPORT,
		    SCUDBG_TRACE|SCUDBG_WARNING,
		    "%s: controller 0x%p not ready yet",
		    __func__, (void *)controller);
		mutex_exit(&scu_ctlp->scu_lock);
		return;
	}
	mutex_exit(&scu_ctlp->scu_lock);

	/* Now discovering is completed, and we can report the target */
	mutex_enter(&scu_ctlp->scu_iport_lock);

	/* set up timeout value */
	loop =
	    scu_domain_discover_timeout * \
	    scif_domain_get_suggested_discover_timeout(domain);

	/* Here we need to wait iport created */
	scu_iportp = scu_get_iport_by_domain(scu_ctlp, domain);
	while ((scu_iportp == NULL) && (loop >= 0)) {
		mutex_exit(&scu_ctlp->scu_iport_lock);
		delay(drv_usectohz(1000)); /* 1 millisecond */
		mutex_enter(&scu_ctlp->scu_iport_lock);

		scu_iportp = scu_get_iport_by_domain(scu_ctlp, domain);
		loop--;
	}

	if (scu_iportp == NULL) {
		SCUDBG(scu_ctlp, SCUDBG_DOMAIN|SCUDBG_IPORT,
		    SCUDBG_TRACE|SCUDBG_WARNING,
		    "%s: cannot find out iport for controller 0x%p "
		    "domain 0x%p, loop = %d", __func__,
		    (void *)controller, (void *)domain, loop);
		mutex_exit(&scu_ctlp->scu_iport_lock);
		return;
	}

	tgtmap = scu_iportp->scui_iss_tgtmap;
	ASSERT(tgtmap != NULL);

	/* Before tgts report, check the status of the domain */
	state =
	    sci_base_state_machine_get_state(&fw_domain->parent.state_machine);
	if (state != SCI_BASE_DOMAIN_STATE_READY) {
		SCUDBG(scu_ctlp, SCUDBG_DOMAIN|SCUDBG_IPORT|SCUDBG_MAP,
		    SCUDBG_TRACE|SCUDBG_WARNING,
		    "%s: domain 0x%p 's state machine state 0x%x, not ready, "
		    "therefore don't report tgt",
		    __func__, (void *)domain, state);
		mutex_exit(&scu_ctlp->scu_iport_lock);
		return;
	}

	/* First call begin operation */
	if (scsi_hba_tgtmap_set_begin(tgtmap) != DDI_SUCCESS) {
		SCUDBG(scu_ctlp, SCUDBG_DOMAIN|SCUDBG_IPORT|SCUDBG_MAP,
		    SCUDBG_TRACE|SCUDBG_ERROR,
		    "%s: scsi_hba_tgtmap_set_begin failed tgtmap 0x%p",
		    __func__, (void *)tgtmap);
		mutex_exit(&scu_ctlp->scu_iport_lock);
		return;
	}

	current_element = sci_abstract_list_get_front(
	    &fw_domain->remote_device_list);

	for (; current_element != NULL;
	    current_element = sci_abstract_list_get_next(current_element)) {
		current_device = (SCIF_SAS_REMOTE_DEVICE_T *)
		    sci_abstract_list_get_object(current_element);
		ASSERT(current_device->is_currently_discovered == TRUE);

		/*
		 * Check it's direct-attached device or expander-attached device
		 */
		if (current_device->containing_device != NULL)
			ea = 1;

		/* Get tgt type */
		scic_remote_device_get_protocols(current_device->core_object,
		    &dev_protocols);
		if (dev_protocols.u.bits.attached_smp_target) {
			tgt_type = SCSI_TGT_SMP_DEVICE;
			SCUDBG(scu_ctlp, SCUDBG_DOMAIN|SCUDBG_IPORT|SCUDBG_MAP,
			    SCUDBG_INFO,
			    "%s: %s SCIF device 0x%p is a smp device contained "
			    "in domain 0x%p",
			    __func__, (ea == 1) ? "EA" : "DA",
			    (void *)current_device, (void *)domain);

		} else if (dev_protocols.u.bits.attached_ssp_target) {
			tgt_type = SCSI_TGT_SCSI_DEVICE;
			SCUDBG(scu_ctlp, SCUDBG_DOMAIN|SCUDBG_IPORT|SCUDBG_MAP,
			    SCUDBG_INFO,
			    "%s: %s SCIF device 0x%p is a sas device contained "
			    "in domain 0x%p",
			    __func__, (ea == 1) ? "EA" : "DA",
			    (void *)current_device, (void *)domain);

		} else if (dev_protocols.u.bits.attached_stp_target ||
		    dev_protocols.u.bits.attached_sata_device) {
			tgt_type = SCSI_TGT_SCSI_DEVICE;
			SCUDBG(scu_ctlp, SCUDBG_DOMAIN|SCUDBG_IPORT|SCUDBG_MAP,
			    SCUDBG_INFO,
			    "%s: %s SCIF device 0x%p is a sata device "
			    "contained in domain 0x%p",
			    __func__, (ea == 1) ? "EA" : "DA",
			    (void *)current_device, (void *)domain);

		} else {
			SCUDBG(scu_ctlp, SCUDBG_DOMAIN|SCUDBG_IPORT|SCUDBG_MAP,
			    SCUDBG_TRACE,
			    "%s: scsi_hba_tgtmap_set_add failed "
			    "tgtmap 0x%p dev_protocols not supported 0x%x",
			    __func__, (void *)tgtmap, dev_protocols.u.all);
			continue;
		}

		/* Get the ua */
		scic_remote_device_get_sas_address(current_device->core_object,
		    &sas_address);

		ASSERT(sas_address.high != 0 || sas_address.low != 0);

		wwn = SCU_SAS_ADDRESS(BE_32(sas_address.high),
		    BE_32(sas_address.low));
		ua = scsi_wwn_to_wwnstr(wwn, 1, NULL);

		SCUDBG(scu_ctlp, SCUDBG_DOMAIN|SCUDBG_IPORT|SCUDBG_MAP,
		    SCUDBG_INFO, "%s: SCIF device 0x%p ua %s",
		    __func__, (void *)current_device, ua);

		/*
		 * Add tgtmap
		 *
		 * NOTE: The driver should have visibility into the
		 * DISCOVER_PHY response from the library in order to populate
		 * the last argument with a proper jitter value.
		 */
		if (scsi_hba_tgtmap_set_add(tgtmap, tgt_type, ua, NULL, 0) !=
		    SCI_SUCCESS) {
			SCUDBG(scu_ctlp, SCUDBG_DOMAIN|SCUDBG_IPORT|SCUDBG_MAP,
			    SCUDBG_TRACE|SCUDBG_ERROR,
			    "%s: scsi_hba_tgtmap_set_add failed "
			    "tgtmap 0x%p tgt_type %d ua %s",
			    __func__, (void *)tgtmap, tgt_type, ua);

			scsi_free_wwnstr(ua);
			/* Abandon the set */
			(void) scsi_hba_tgtmap_set_flush(tgtmap);
			mutex_exit(&scu_ctlp->scu_iport_lock);
			return;
		} else {
			SCUDBG(scu_ctlp, SCUDBG_DOMAIN|SCUDBG_IPORT|SCUDBG_MAP,
			    SCUDBG_INFO,
			    "%s: scsi_hba_tgtmap_set_add success "
			    "tgtmap 0x%p tgt_type %d ua %s",
			    __func__, (void *)tgtmap, tgt_type, ua);
		}

		scsi_free_wwnstr(ua);
	}

err_out:
	/* Last call end operation */
	if (scsi_hba_tgtmap_set_end(tgtmap, 0) != DDI_SUCCESS) {
		SCUDBG(scu_ctlp, SCUDBG_DOMAIN|SCUDBG_IPORT|SCUDBG_MAP,
		    SCUDBG_TRACE|SCUDBG_ERROR,
		    "%s: scsi_hba_tgtmap_set_end failed tgtmap 0x%p",
		    __func__, (void *)tgtmap);
	}

	mutex_enter(&scu_iportp->scui_lock);
	primary_phy = scu_iportp->scui_primary_phy;
	mutex_exit(&scu_iportp->scui_lock);

	ap = kmem_zalloc(SCU_MAX_UA_SIZE, KM_SLEEP);

	if (primary_phy == NULL) {
		/* this iport is down */
		(void) snprintf(ap, 1, "%s", "0");
	} else {
		wwn = primary_phy->scup_remote_sas_address;
		(void) scsi_wwn_to_wwnstr(wwn, 1, ap);
	}

	/* Now update the property */
	if (ndi_prop_update_string(DDI_DEV_T_NONE, scu_iportp->scui_dip,
	    SCSI_ADDR_PROP_ATTACHED_PORT, ap) != DDI_SUCCESS) {
		SCUDBG(scu_ctlp, SCUDBG_DOMAIN|SCUDBG_IPORT,
		    SCUDBG_TRACE|SCUDBG_ERROR,
		    "%s: ndi_prop_update_string failed for iport 0x%p",
		    __func__, (void *)scu_iportp);
	}

	kmem_free(ap, SCU_MAX_UA_SIZE);

	SCUDBG(scu_ctlp, SCUDBG_DOMAIN|SCUDBG_IPORT, SCUDBG_TRACE,
	    "%s: exit domain 0x%p", __func__, (void *)domain);
	mutex_exit(&scu_ctlp->scu_iport_lock);
}

/*
 * Callback of SCIF - informs the user that a previously requested discovery
 * operation on the domain has completed.
 *
 * In this callback, scu_iport_report_tgts taskq will be dispatched.
 */
void
scif_cb_domain_discovery_complete(SCI_CONTROLLER_HANDLE_T  controller,
    SCI_DOMAIN_HANDLE_T domain, SCI_STATUS completion_status)
{
	scu_subctl_t	*scu_subctlp;
	scu_ctl_t	*scu_ctlp;

	scu_subctlp = sci_object_get_association(controller);
	ASSERT(scu_subctlp != NULL);
	scu_ctlp = scu_subctlp->scus_ctlp;

	SCUDBG(scu_ctlp, SCUDBG_DOMAIN, SCUDBG_TRACE,
	    "%s: enter domain 0x%p controller 0x%p status %d",
	    __func__, (void *)domain, (void *)controller,
	    completion_status);

	/*
	 * Dispatch taskq to handle target report for iport
	 */
	if (ddi_taskq_dispatch(scu_ctlp->scu_discover_taskq,
	    scu_iport_report_tgts, domain, DDI_SLEEP) != DDI_SUCCESS) {
		SCUDBG(scu_ctlp, SCUDBG_HBA,
		    SCUDBG_ERROR|SCUDBG_TRACE,
		    "%s: ddi_taskq_dispatch for scu_iport_report_tgts failed "
		    "domain 0x%p controller 0x%p", __func__,
		    (void *)domain, (void *)controller);
	}

	SCUDBG(scu_ctlp, SCUDBG_DOMAIN, SCUDBG_TRACE,
	    "%s: exit domain 0x%p controller 0x%p",
	    __func__, (void *)domain, (void *)controller);
}

/*
 * Callback of SCIF - informs the framework user that a new expander attached
 * device was found in the domain
 */
/*ARGSUSED*/
void
scif_cb_domain_ea_device_added(SCI_CONTROLLER_HANDLE_T controller,
    SCI_DOMAIN_HANDLE_T domain, SCI_REMOTE_DEVICE_HANDLE_T containing_device,
    SMP_RESPONSE_DISCOVER_T *smp_response)
{
	scu_subctl_t			*scu_subctlp;
	scu_ctl_t			*scu_ctlp;
	SCI_REMOTE_DEVICE_HANDLE_T	device_handle;
	void				*device;

	scu_subctlp = sci_object_get_association(controller);
	ASSERT(scu_subctlp != NULL);
	scu_ctlp = scu_subctlp->scus_ctlp;

	SCUDBG(scu_ctlp, SCUDBG_DOMAIN, SCUDBG_TRACE,
	    "%s: enter domain 0x%p controller 0x%p",
	    __func__, (void *)domain, (void *)controller);

	mutex_enter(&scu_subctlp->scus_lib_remote_device_lock);
	device = kmem_zalloc(scif_remote_device_get_object_size(), KM_SLEEP);

	/* construct SCIF remote device struct */
	scif_remote_device_construct(domain, device, &device_handle);

	/* Now construct expander attached remote device struct */
	if (scif_remote_device_ea_construct(device_handle,
	    containing_device, smp_response) != SCI_SUCCESS) {
		SCUDBG(scu_ctlp, SCUDBG_DOMAIN, SCUDBG_ERROR,
		    "%s: scif_remote_device_ea_construct failure "
		    "for domain 0x%p controller 0x%p",
		    __func__, (void *)domain, (void *)controller);
	}
	mutex_exit(&scu_subctlp->scus_lib_remote_device_lock);

	SCUDBG(scu_ctlp, SCUDBG_DOMAIN, SCUDBG_TRACE,
	    "%s: exit domain 0x%p controller 0x%p",
	    __func__, (void *)domain, (void *)controller);
}

/*
 * Callback of SCIF - inform the user that the domain is no longer ready, thus
 * cannot process IO requests for devices found inside it
 *
 * This callback will be received when the SCIL domain state exits the ready
 * state, now it's only for debug.
 */
/*ARGSUSED*/
void
scif_cb_domain_not_ready(SCI_CONTROLLER_HANDLE_T controller,
    SCI_DOMAIN_HANDLE_T domain)
{
	scu_subctl_t	*scu_subctlp;
	scu_ctl_t	*scu_ctlp;
	scu_iport_t	*scu_iportp;

	scu_subctlp = sci_object_get_association(controller);
	ASSERT(scu_subctlp != NULL);
	scu_ctlp = scu_subctlp->scus_ctlp;

	scu_iportp = sci_object_get_association(domain);

	SCUDBG(scu_ctlp, SCUDBG_DOMAIN, SCUDBG_TRACE,
	    "%s: enter domain 0x%p controller 0x%p iport 0x%p",
	    __func__, (void *)domain, (void *)controller,
	    (void *)scu_iportp);
}

/*
 * Callback of SCIF - inform the user that the domain is ready and
 * capable of processing IO requests for devices found inside it
 *
 * This callback is primarily useful for debug, it will occur when
 * the SCIL domain state machine transitions to the ready state and
 * also at the completion of discover.
 */
/*ARGSUSED*/
void
scif_cb_domain_ready(SCI_CONTROLLER_HANDLE_T controller,
    SCI_DOMAIN_HANDLE_T domain)
{
	scu_subctl_t	*scu_subctlp;
	scu_ctl_t	*scu_ctlp;
	scu_iport_t	*scu_iportp;

	scu_subctlp = sci_object_get_association(controller);
	ASSERT(scu_subctlp != NULL);
	scu_ctlp = scu_subctlp->scus_ctlp;

	scu_iportp = sci_object_get_association(controller);

	SCUDBG(scu_ctlp, SCUDBG_DOMAIN, SCUDBG_TRACE,
	    "%s: enter domain 0x%p controller 0x%p iport 0x%p",
	    __func__, (void *)domain, (void *)controller,
	    (void *)scu_iportp);
}

/*
 * Callback of SCIF
 */
/*ARGSUSED*/
void
scif_cb_domain_reset_complete(SCI_CONTROLLER_HANDLE_T controller,
    SCI_DOMAIN_HANDLE_T domain, SCI_STATUS completion_status)
{

}

static int
scu_is_task_start_thread(scu_subctl_t *scu_subctlp)
{
	int			rval = 0;
	if (scu_subctlp->scus_task_start_thread == curthread) {
		rval = 1;
		ASSERT(mutex_owned(&scu_subctlp->scus_slot_lock));
	}
	return (rval);
}

/*
 * Callback of SCIF - inform the user that an IO request has completed
 */
void
scif_cb_io_request_complete(SCI_CONTROLLER_HANDLE_T controller,
    SCI_REMOTE_DEVICE_HANDLE_T remote_device,
    SCI_IO_REQUEST_HANDLE_T io_request, SCI_IO_STATUS completion_status)
{
	scu_subctl_t		*scu_subctlp;
	scu_ctl_t		*scu_ctlp;
	scu_cmd_t		*scu_cmdp;
	scu_io_slot_t		*scu_slotp;
	scu_tgt_t		*scu_tgtp;
	int			slot_locked = 0;
	int			cmd_poll;

	scu_subctlp = (scu_subctl_t *)sci_object_get_association(controller);
	ASSERT(scu_subctlp != NULL);
	scu_ctlp = scu_subctlp->scus_ctlp;
	scu_cmdp = (scu_cmd_t *)sci_object_get_association(io_request);

	scu_slotp = scu_cmdp->cmd_slot;
	scu_tgtp = scu_cmdp->cmd_tgtp;

	SCUDBG(scu_ctlp, SCUDBG_IO|SCUDBG_INTR,
	    SCUDBG_TRACE, "%s: enter scu_tgtp 0x%p, scu_cmdp 0x%p, "
	    "%s_pkt 0x%p, completion_status %d", __func__, (void *)scu_tgtp,
	    (void *)scu_cmdp, (scu_cmdp->cmd_smp) ? "smp" : "scsi",
	    (void *)scu_cmdp->cmd_pkt, completion_status);

	if (scu_cmdp->cmd_smp == 0) {
		/*
		 * SSP IO requests completion handling
		 */
		struct scsi_pkt		*pkt = scu_cmdp->cmd_pkt;
		uint16_t		io_tag = scu_cmdp->cmd_tag;
		uint32_t		sense_length, io_length;
		SCI_SSP_RESPONSE_IU_T	*response_buffer;
		struct scsi_arq_status	*arqstat;

		/* First check completion status */
		switch (completion_status) {
		case SCI_IO_SUCCESS:
			SCUDBG(scu_ctlp, SCUDBG_IO|SCUDBG_INTR,
			    SCUDBG_INFO,
			    "%s: scu_cmdp 0x%p on slot %d got succeeded",
			    __func__, (void *)scu_cmdp, io_tag);

			pkt->pkt_scbp[0] = STATUS_GOOD;
			scu_set_pkt_reason(scu_ctlp, scu_cmdp, CMD_CMPLT,
			    STATE_GOT_BUS|STATE_GOT_TARGET|STATE_SENT_CMD|
			    STATE_GOT_STATUS, 0);
			if (scu_cmdp->cmd_dma) {
				pkt->pkt_state |= STATE_XFERRED_DATA;
				pkt->pkt_resid = 0;
			}

			break;

		case SCI_IO_SUCCESS_COMPLETE_BEFORE_START:
			SCUDBG(scu_ctlp, SCUDBG_IO|SCUDBG_INTR,
			    SCUDBG_WARNING,
			    "%s: scu_cmdp 0x%p on slot %d got succeeded "
			    "before start",
			    __func__, (void *)scu_cmdp, io_tag);

			pkt->pkt_scbp[0] = STATUS_GOOD;
			scu_set_pkt_reason(scu_ctlp, scu_cmdp, CMD_CMPLT,
			    STATE_GOT_BUS|STATE_GOT_TARGET|STATE_SENT_CMD|
			    STATE_GOT_STATUS, 0);
			if (scu_cmdp->cmd_dma) {
				pkt->pkt_state |= STATE_XFERRED_DATA;
				pkt->pkt_resid = 0;
			}

			break;

		case SCI_IO_SUCCESS_IO_DONE_EARLY:
			io_length =
			    scif_io_request_get_number_of_bytes_transferred(
			    io_request);
			SCUDBG(scu_ctlp, SCUDBG_IO|SCUDBG_INTR,
			    SCUDBG_WARNING,
			    "%s: scu_cmdp 0x%p on slot %d got "
			    "io done early success status, real io length 0x%x "
			    "while request length %lu",
			    __func__, (void *)scu_cmdp, io_tag, io_length,
			    pkt->pkt_dma_len);

			pkt->pkt_scbp[0] = STATUS_GOOD;
			scu_set_pkt_reason(scu_ctlp, scu_cmdp, CMD_CMPLT,
			    STATE_GOT_BUS|STATE_GOT_TARGET|STATE_SENT_CMD|
			    STATE_GOT_STATUS, 0);
			if (scu_cmdp->cmd_dma) {
				pkt->pkt_state |= STATE_XFERRED_DATA;
				if (io_length <= pkt->pkt_dma_len) {
					pkt->pkt_resid = pkt->pkt_dma_len -
					    io_length;
				}
			}
			break;

		case SCI_IO_FAILURE_RESPONSE_VALID:
			SCUDBG(scu_ctlp, SCUDBG_IO|SCUDBG_INTR,
			    SCUDBG_INFO|SCUDBG_WARNING,
			    "%s: scu_cmdp 0x%p on slot %d got failure "
			    "with valid response", __func__,
			    (void *)scu_cmdp, io_tag);

			response_buffer = (SCI_SSP_RESPONSE_IU_T *)
			    scif_io_request_get_response_iu_address(io_request);

			pkt->pkt_scbp[0] = response_buffer->status;

			sense_length = sci_ssp_get_sense_data_length(
			    response_buffer->sense_data_length);

			SCUDBG(scu_ctlp, SCUDBG_IO|SCUDBG_INTR,
			    SCUDBG_WARNING,
			    "%s: scu_cmdp 0x%p sense_length %d "
			    "sensedata_present %d", __func__, (void *)scu_cmdp,
			    sense_length, response_buffer->data_present);

			if (sense_length > 0) {
				scu_set_pkt_reason(scu_ctlp, scu_cmdp,
				    CMD_CMPLT,
				    STATE_GOT_BUS|STATE_GOT_TARGET| \
				    STATE_SENT_CMD|STATE_GOT_STATUS| \
				    STATE_ARQ_DONE, 0);

				arqstat = (struct scsi_arq_status *)
				    (void *)(pkt->pkt_scbp);
				arqstat->sts_rqpkt_reason = pkt->pkt_reason;
				arqstat->sts_rqpkt_state = pkt->pkt_state;
				arqstat->sts_rqpkt_state |= STATE_XFERRED_DATA;
				arqstat->sts_rqpkt_statistics =
				    pkt->pkt_statistics;

				if (sense_length > SENSE_LENGTH) {
					SCUDBG(scu_ctlp,
					    SCUDBG_IO|SCUDBG_INTR,
					    SCUDBG_WARNING,
					    "%s: sense_length %d more than "
					    "SENSE_LENGTH %d",
					    __func__, sense_length,
					    (int)SENSE_LENGTH);
					sense_length = SENSE_LENGTH;
				}

				bcopy((uint8_t *)(response_buffer->data),
				    (uint8_t *)&(arqstat->sts_sensedata),
				    sense_length);
			} else {
				scu_set_pkt_reason(scu_ctlp, scu_cmdp,
				    CMD_CMPLT,
				    STATE_GOT_BUS|STATE_GOT_TARGET| \
				    STATE_SENT_CMD|STATE_GOT_STATUS, 0);
			}
			break;

		case SCI_IO_FAILURE_REQUIRES_SCSI_ABORT:
			SCUDBG(scu_ctlp, SCUDBG_IO|SCUDBG_INTR,
			    SCUDBG_WARNING,
			    "%s: scu_cmdp 0x%p on slot %d got failure "
			    "with requires scsi abort status",
			    __func__, (void *)scu_cmdp, io_tag);
			scu_set_pkt_reason(scu_ctlp, scu_cmdp, CMD_ABORTED, 0,
			    (scu_cmdp->cmd_timeout ? STAT_TIMEOUT : 0) | \
			    STAT_ABORTED);
			break;

		case SCI_IO_FAILURE_TERMINATED:
			SCUDBG(scu_ctlp, SCUDBG_IO|SCUDBG_INTR,
			    SCUDBG_WARNING,
			    "%s: scu_cmdp 0x%p on slot %d got failure "
			    "with terminated status",
			    __func__, (void *)scu_cmdp, io_tag);
			scu_set_pkt_reason(scu_ctlp, scu_cmdp, CMD_RESET, 0,
			    (scu_cmdp->cmd_timeout ? STAT_TIMEOUT : 0) | \
			    STAT_TERMINATED);
			break;

		case SCI_IO_FAILURE_INVALID_REMOTE_DEVICE:
			SCUDBG(scu_ctlp, SCUDBG_IO|SCUDBG_INTR,
			    SCUDBG_ERROR,
			    "%s: scu_cmdp 0x%p on slot %d got failure "
			    "with invalid remote device status",
			    __func__, (void *)scu_cmdp, io_tag);

			scu_set_pkt_reason(scu_ctlp, scu_cmdp, CMD_DEV_GONE,
			    STATE_GOT_BUS, 0);
			break;

		case SCI_IO_FAILURE_INVALID_PARAMETER_VALUE:
			SCUDBG(scu_ctlp, SCUDBG_IO|SCUDBG_INTR,
			    SCUDBG_ERROR,
			    "%s: scu_cmdp 0x%p on slot %d got failure "
			    "with invalid parameter value status",
			    __func__, (void *)scu_cmdp, io_tag);

			scu_set_pkt_reason(scu_ctlp, scu_cmdp, CMD_TERMINATED,
			    STATE_GOT_BUS|STATE_GOT_TARGET, STAT_TERMINATED);
			break;

		case SCI_IO_FAILURE_UNSUPPORTED_PROTOCOL:
			SCUDBG(scu_ctlp, SCUDBG_IO|SCUDBG_INTR,
			    SCUDBG_ERROR,
			    "%s: scu_cmdp 0x%p on slot %d got failure "
			    "with unsupported protocol",
			    __func__, (void *)scu_cmdp, io_tag);

			scu_set_pkt_reason(scu_ctlp, scu_cmdp, CMD_TERMINATED,
			    STATE_GOT_BUS|STATE_GOT_TARGET, STAT_TERMINATED);
			break;

		case SCI_IO_FAILURE_PROTOCOL_VIOLATION:
			SCUDBG(scu_ctlp, SCUDBG_IO|SCUDBG_INTR,
			    SCUDBG_ERROR,
			    "%s: scu_cmdp 0x%p on slot %d got failure "
			    "with protocol violation status",
			    __func__, (void *)scu_cmdp, io_tag);

			scu_set_pkt_reason(scu_ctlp, scu_cmdp, CMD_TERMINATED,
			    STATE_GOT_BUS|STATE_GOT_TARGET, STAT_TERMINATED);
			break;

		case SCI_IO_FAILURE_RETRY_LIMIT_REACHED:
			SCUDBG(scu_ctlp, SCUDBG_IO|SCUDBG_INTR,
			    SCUDBG_WARNING,
			    "%s: scu_cmdp 0x%p on slot %d got failure "
			    "with retry limit reached status",
			    __func__, (void *)scu_cmdp, io_tag);

			scu_set_pkt_reason(scu_ctlp, scu_cmdp, CMD_TERMINATED,
			    STATE_GOT_BUS|STATE_GOT_TARGET, STAT_TERMINATED);
			break;

		case SCI_IO_FAILURE_REMOTE_DEVICE_RESET_REQUIRED:
			SCUDBG(scu_ctlp, SCUDBG_IO|SCUDBG_INTR,
			    SCUDBG_WARNING,
			    "%s: scu_cmdp 0x%p on slot %d got failure "
			    "with remote device reset required status",
			    __func__, (void *)scu_cmdp, io_tag);

			scu_set_pkt_reason(scu_ctlp, scu_cmdp, CMD_RESET,
			    0, STAT_DEV_RESET);
			break;

		case SCI_IO_FAILURE:
			SCUDBG(scu_ctlp, SCUDBG_IO|SCUDBG_INTR,
			    SCUDBG_ERROR,
			    "%s: scu_cmdp 0x%p on slot %d got failure",
			    __func__, (void *)scu_cmdp, io_tag);

			scu_set_pkt_reason(scu_ctlp, scu_cmdp, CMD_TRAN_ERR,
			    0, STAT_ABORTED);
			break;

		case SCI_IO_FAILURE_CONTROLLER_SPECIFIC_ERR:
			SCUDBG(scu_ctlp, SCUDBG_IO|SCUDBG_INTR,
			    SCUDBG_ERROR,
			    "%s: scu_cmdp 0x%p on slot %d got failure "
			    "with controller specific error status",
			    __func__, (void *)scu_cmdp, io_tag);

			scu_set_pkt_reason(scu_ctlp, scu_cmdp, CMD_TRAN_ERR,
			    0, STAT_ABORTED);
			break;

		case SCI_IO_FAILURE_NO_NCQ_TAG_AVAILABLE:
			SCUDBG(scu_ctlp, SCUDBG_IO|SCUDBG_INTR,
			    SCUDBG_INFO|SCUDBG_ERROR,
			    "%s: scu_cmdp 0x%p on slot %d got failure "
			    "with no ncq tag available status",
			    __func__, (void *)scu_cmdp, io_tag);
			if (scu_cmdp->cmd_noretry == 0)
				goto retry;
			scu_set_pkt_reason(scu_ctlp, scu_cmdp, CMD_TRAN_ERR,
			    0, STAT_ABORTED);
			break;

		case SCI_IO_FAILURE_INVALID_STATE:
			SCUDBG(scu_ctlp, SCUDBG_IO|SCUDBG_INTR,
			    SCUDBG_INFO|SCUDBG_ERROR,
			    "%s: scu_cmdp 0x%p on slot %d got failure "
			    "with invalid status",
			    __func__, (void *)scu_cmdp, io_tag);
			if (scu_cmdp->cmd_noretry == 0)
				goto retry;
			scu_set_pkt_reason(scu_ctlp, scu_cmdp, CMD_TRAN_ERR,
			    0, STAT_ABORTED);
			break;

		case SCI_IO_FAILURE_RETRY_REQUIRED:
			SCUDBG(scu_ctlp, SCUDBG_IO|SCUDBG_INTR,
			    SCUDBG_INFO|SCUDBG_ERROR,
			    "%s: scu_cmdp 0x%p on slot %d got failure "
			    "with retry required status",
			    __func__, (void *)scu_cmdp, io_tag);
			if (scu_cmdp->cmd_noretry == 0)
				goto retry;
			scu_set_pkt_reason(scu_ctlp, scu_cmdp, CMD_TRAN_ERR,
			    0, STAT_ABORTED);
			break;

		case SCI_IO_FAILURE_INSUFFICIENT_RESOURCES:
			SCUDBG(scu_ctlp, SCUDBG_IO|SCUDBG_INTR,
			    SCUDBG_INFO|SCUDBG_ERROR,
			    "%s: scu_cmdp 0x%p on slot %d got failure "
			    "with insufficient resources status",
			    __func__, (void *)scu_cmdp, io_tag);
			if (scu_cmdp->cmd_noretry == 0)
				goto retry;
			scu_set_pkt_reason(scu_ctlp, scu_cmdp, CMD_TRAN_ERR,
			    0, STAT_ABORTED);
			break;

		default:
			scu_log(scu_ctlp, CE_WARN,
			    "!%s: scu_cmdp 0x%p on slot %d got failure "
			    "with unrecognized %d status",
			    __func__, (void *)scu_cmdp, io_tag,
			    completion_status);
			break;
		}

		SCUDBG(scu_ctlp, SCUDBG_IO|SCUDBG_INTR,
		    SCUDBG_INFO,
		    "%s: scu_cmdp 0x%p for scu_tgtp 0x%p done, pkt 0x%p reason "
		    "0x%x state 0x%x residue %ld status 0x%x", __func__,
		    (void *)scu_cmdp, (void *)scu_tgtp,
		    (void *)pkt, pkt->pkt_reason, pkt->pkt_state,
		    pkt->pkt_resid, pkt->pkt_scbp[0]);

	} else {
		/*
		 * SMP IO requests completion handling
		 */
		struct smp_pkt	*smp_pkt = scu_cmdp->cmd_smp_pkt;
		void		*smp_resp_buf;

		switch (completion_status) {
		case SCI_IO_SUCCESS:
			SCUDBG(scu_ctlp, SCUDBG_IO|SCUDBG_INTR|SCUDBG_SMP,
			    SCUDBG_INFO,
			    "%s: SMP scu_cmdp 0x%p got succeeded",
			    __func__, (void *)scu_cmdp);

			smp_resp_buf = scif_io_request_get_response_iu_address(
			    io_request);
			(void) memcpy(smp_pkt->smp_pkt_rsp, smp_resp_buf,
			    scu_cmdp->cmd_smp_resplen);
			break;

		case SCI_IO_FAILURE_TERMINATED:
		case SCI_IO_FAILURE_REQUIRES_SCSI_ABORT:
			smp_pkt->smp_pkt_reason = ETIMEDOUT;
			break;

		case SCI_IO_FAILURE_CONTROLLER_SPECIFIC_ERR:
		default:
			SCUDBG(scu_ctlp, SCUDBG_IO|SCUDBG_INTR|SCUDBG_SMP,
			    SCUDBG_WARNING, "%s: SMP scu_cmdp 0x%p err",
			    __func__, (void *)scu_cmdp);

			smp_pkt->smp_pkt_reason = EIO;
			break;
		}

		SCUDBG(scu_ctlp, SCUDBG_IO|SCUDBG_INTR,
		    SCUDBG_INFO, "%s: scu_cmdp 0x%p for scu_tgtp 0x%p done, "
		    "smp_pkt 0x%p reason 0x%x",
		    __func__, (void *)scu_cmdp, (void *)scu_tgtp,
		    (void *)smp_pkt, smp_pkt->smp_pkt_reason);
	}

	/*
	 * XXX Conditional locking is ugly, but sadly SCIL calls
	 * scif_cb_{io,task}_request_complete() in three contexts:
	 * interrupt, timer taskq, and start task thread. SCI_SAS_HARD_RESET
	 * have aborted io/task requests completed in its same context, which
	 * will result in recursive mutex enter.
	 */
	if (scu_is_task_start_thread(scu_subctlp) == 0) {
		slot_locked = 0;
		mutex_enter(&scu_subctlp->scus_slot_lock);
	} else {
		slot_locked = 1;
	}
	/*
	 * Release SCIF/SCIC resources
	 * Commands not completed by card needn't call scif to complete io
	 */
	if (scu_cmdp->cmd_started) {
		(void) scif_controller_complete_io(controller, remote_device,
		    io_request);
		scu_subctlp->scus_slot_active_num--;
	}
	/* Detach slot */
	if (scu_slotp->scu_io_lib_tag != SCI_CONTROLLER_INVALID_IO_TAG) {
		(void) scu_detach_cmd(scu_cmdp);
		scu_free_tag(scu_subctlp, scu_slotp);
	}
	/* Update command state */
	if (scu_cmdp->cmd_finished == 0)
		scu_cmdp->cmd_finished = 1;
	cmd_poll = scu_cmdp->cmd_poll;
	if (slot_locked == 0)
		mutex_exit(&scu_subctlp->scus_slot_lock);

	if (cmd_poll == 0) {
		/* Add to the completed command queue */
		scu_flush_cmd(scu_ctlp, scu_cmdp);
	}
	return;

retry:
	/* Add to the waiting queue and re-try the command */
	scu_cmdp->cmd_retried = 1;

	mutex_enter(&scu_tgtp->scut_wq_lock);
	scu_cmdp->cmd_wq_queued = 1;
	STAILQ_INSERT_HEAD(&scu_tgtp->scut_wq, scu_cmdp, cmd_next);
	mutex_exit(&scu_tgtp->scut_wq_lock);
}

/*
 * Callback of SCIF - ask the user to provide the address for the
 * command descriptor block (CDB) associated with this IO request
 */
void *
scif_cb_io_request_get_cdb_address(void *scif_user_io_request)
{
	scu_cmd_t	*scu_cmdp;

	scu_cmdp = (scu_cmd_t *)scif_user_io_request;

	SCUDBG(scu_cmdp->cmd_tgtp->scut_ctlp, SCUDBG_IO,
	    SCUDBG_TRACE|SCUDBG_INFO,
	    "%s: scu_cmdp 0x%p, cdb 0x%p", __func__, (void *)scu_cmdp,
	    (void *)scu_cmdp->cmd_pkt->pkt_cdbp);

	return (scu_cmdp->cmd_pkt->pkt_cdbp);
}

/*
 * Callback of SCIF - ask the user to provide the length of the
 * command descriptor block (CDB) associated with this IO request
 */
U32
scif_cb_io_request_get_cdb_length(void *scif_user_io_request)
{
	scu_cmd_t	*scu_cmdp;

	scu_cmdp = (scu_cmd_t *)scif_user_io_request;

	SCUDBG(scu_cmdp->cmd_tgtp->scut_ctlp, SCUDBG_IO,
	    SCUDBG_TRACE|SCUDBG_INFO,
	    "%s: scu_cmdp 0x%p, cdb length %d", __func__, (void *)scu_cmdp,
	    scu_cmdp->cmd_pkt->pkt_cdblen);

	return (scu_cmdp->cmd_pkt->pkt_cdblen);
}

/*
 * Callback of SCIF - ask the user to provide the command priority
 * associated with this IO request
 */
U32
scif_cb_io_request_get_command_priority(void *scif_user_io_request)
{
	scu_cmd_t	*scu_cmdp;

	scu_cmdp = (scu_cmd_t *)scif_user_io_request;

	/* currently SCSA doesn't support this field yet */
	SCUDBG(scu_cmdp->cmd_tgtp->scut_ctlp, SCUDBG_IO,
	    SCUDBG_TRACE|SCUDBG_INFO,
	    "%s: scu_cmdp 0x%p, command priority 0",
	    __func__, (void *)scu_cmdp);

	return (0);
}

/*
 * Callback of SCIF - ask the user to provide the data direction
 * for this request
 */
SCI_IO_REQUEST_DATA_DIRECTION
scif_cb_io_request_get_data_direction(void *scif_user_io_request)
{
	scu_cmd_t	*scu_cmdp;
	SCI_IO_REQUEST_DATA_DIRECTION direction;

	scu_cmdp = (scu_cmd_t *)scif_user_io_request;

	if (scu_cmdp->cmd_dma) {
		if (scu_cmdp->cmd_pkt->pkt_dma_flags & DDI_DMA_WRITE) {
			direction = SCI_IO_REQUEST_DATA_OUT;
		} else {
			direction = SCI_IO_REQUEST_DATA_IN;
		}
	} else {
			direction = SCI_IO_REQUEST_NO_DATA;
	}

	SCUDBG(scu_cmdp->cmd_tgtp->scut_ctlp, SCUDBG_IO,
	    SCUDBG_TRACE|SCUDBG_INFO, "%s: scu_cmdp 0x%p, direction %d",
	    __func__, (void *)scu_cmdp, direction);

	return (direction);
}

/*
 * Callback of SCIF - ask the user to provide the Logical Unit (LUN)
 * associated with this IO request
 */
U32
scif_cb_io_request_get_lun(void *scif_user_io_request)
{
	scu_cmd_t	*scu_cmdp;
	uint32_t	lun_num;

	scu_cmdp = (scu_cmd_t *)scif_user_io_request;
	lun_num = scu_cmdp->cmd_lunp->scul_lun_num & 0xffffffff;

	SCUDBG(scu_cmdp->cmd_tgtp->scut_ctlp, SCUDBG_IO,
	    SCUDBG_TRACE|SCUDBG_INFO, "%s: scu_cmdp 0x%p, lun %u",
	    __func__, (void *)scu_cmdp, lun_num);

	return (lun_num);
}

/*
 * Callback of SCIF - ask the user to provide the address to where the
 * next Scatter-Gather Element is located
 */
void
scif_cb_io_request_get_next_sge(void *scif_user_io_request,
    void *current_sge_address, void **next_sge)
{
	scu_cmd_t	*scu_cmdp = (scu_cmd_t *)scif_user_io_request;
	struct scsi_pkt	*pkt = scu_cmdp->cmd_pkt;
	int		num;

	if (current_sge_address == NULL) {
		*next_sge = pkt->pkt_cookies;
	} else {
		num = ((uintptr_t)current_sge_address -	\
		    (uintptr_t)(pkt->pkt_cookies)) / sizeof (ddi_dma_cookie_t);

		if ((num + 1) == pkt->pkt_numcookies) {
			*next_sge = NULL;
		} else {
			*next_sge = (uint8_t *)current_sge_address +
			    sizeof (ddi_dma_cookie_t);
		}
	}

	SCUDBG(scu_cmdp->cmd_tgtp->scut_ctlp, SCUDBG_IO,
	    SCUDBG_INFO|SCUDBG_TRACE,
	    "%s: current_sge 0x%p, next_sge 0x%p", __func__,
	    current_sge_address, *next_sge);
}

/*
 * Callback of SCIF - ask the user to provide the task attribute
 * associated with this IO request
 */
U32
scif_cb_io_request_get_task_attribute(void *scif_user_io_request)
{
	scu_cmd_t	*scu_cmdp;
	uint32_t	task_attribute;

	scu_cmdp = (scu_cmd_t *)scif_user_io_request;

	switch (scu_cmdp->cmd_pkt->pkt_flags & FLAG_TAGMASK) {
	case FLAG_HTAG:
		task_attribute = SAS_CMD_TASK_ATTR_HEAD;
		break;

	case FLAG_OTAG:
		task_attribute = SAS_CMD_TASK_ATTR_ORDERED;
		break;

	case FLAG_STAG:
	default:
		task_attribute = SAS_CMD_TASK_ATTR_SIMPLE;
		break;
	}

	SCUDBG(scu_cmdp->cmd_tgtp->scut_ctlp, SCUDBG_IO,
	    SCUDBG_TRACE|SCUDBG_INFO,
	    "%s: scu_cmdp 0x%p, task attribute 0x%x",
	    __func__, (void *)scu_cmdp, task_attribute);

	return (task_attribute);
}

/*
 * Callback of SCIF - ask the user to provide the number of bytes
 * to be transfered as part of this request
 */
U32
scif_cb_io_request_get_transfer_length(void *scif_user_io_request)
{
	scu_cmd_t	*scu_cmdp;
	uint32_t	dma_len;

	scu_cmdp = (scu_cmd_t *)scif_user_io_request;
	dma_len = scu_cmdp->cmd_pkt->pkt_dma_len;

	SCUDBG(scu_cmdp->cmd_tgtp->scut_ctlp, SCUDBG_IO,
	    SCUDBG_TRACE|SCUDBG_INFO,
	    "%s: scu_cmdp 0x%p, transfer len %u",
	    __func__, (void *)scu_cmdp, dma_len);

	return (dma_len);
}

/*
 * Callback of SCIF - ask the user to simply return the virtual address
 * associated with the scsi_io and byte_offset supplied parameters
 *
 * note that this method is only used by SATI
 */
U8 *
scif_cb_io_request_get_virtual_address_from_sgl(void *scif_user_io_request,
    U32 byte_offset)
{
	scu_cmd_t	*scu_cmdp = (scu_cmd_t *)scif_user_io_request;
	struct buf	*bp = scsi_pkt2bp(scu_cmdp->cmd_pkt);

	ASSERT(byte_offset < bp->b_bcount);
	bp_mapin(bp);
	return ((U8 *)&bp->b_un.b_addr[byte_offset]);
}

/*
 * Callback of SCIF - ask the user to acquire/get the lock
 */
/*ARGSUSED*/
void
scif_cb_lock_acquire(SCI_CONTROLLER_HANDLE_T controller, SCI_LOCK_HANDLE_T lock)
{
#ifdef DEBUG
	scu_subctl_t	*scu_subctlp;

	scu_subctlp = sci_object_get_association(controller);
	ASSERT(scu_subctlp != NULL);
#endif

	mutex_enter(lock);
}

/*
 * Callback of SCIF - ask the user to associate the supplied lock with an
 * operating environment specific locking construct
 *
 * currently it's called at one place, if in futuer there are more than
 * one place to call, then we need to allocate a mutex array.
 */
void
scif_cb_lock_associate(SCI_CONTROLLER_HANDLE_T controller,
    SCI_LOCK_HANDLE_T lock)
{
	scu_subctl_t	*scu_subctlp;
	scu_ctl_t	*scu_ctlp;

	scu_subctlp = sci_object_get_association(controller);
	ASSERT(scu_subctlp != NULL);
	scu_ctlp = scu_subctlp->scus_ctlp;

	lock = &scu_subctlp->scus_lib_hprq_lock;

	SCUDBG(scu_ctlp, SCUDBG_LOCK, SCUDBG_TRACE|SCUDBG_INFO,
	    "%s: get ossl lock 0x%p for SCIF high priority request queue",
	    __func__, lock);
}

/*
 * Callback of SCIF - ask the user to de-associate the supplied lock with
 * an operating environment specific locking construct
 */
void
scif_cb_lock_disassociate(SCI_CONTROLLER_HANDLE_T controller,
    SCI_LOCK_HANDLE_T lock)
{
	scu_subctl_t	*scu_subctlp;
	scu_ctl_t	*scu_ctlp = NULL;

	scu_subctlp = sci_object_get_association(controller);
	if (scu_subctlp != NULL)
		scu_ctlp = scu_subctlp->scus_ctlp;

	if (lock != NULL)
		lock = NULL;

	SCUDBG(scu_ctlp, SCUDBG_LOCK, SCUDBG_TRACE|SCUDBG_INFO,
	    "%s: release ossl lock for SCIF high priority request queue",
	    __func__);
}

/*
 * Callback of SCIF - ask the user to release a lock
 */
/*ARGSUSED*/
void
scif_cb_lock_release(SCI_CONTROLLER_HANDLE_T controller,
    SCI_LOCK_HANDLE_T lock)
{
#ifdef DEBUG
	scu_subctl_t	*scu_subctlp;

	scu_subctlp = sci_object_get_association(controller);
	ASSERT(scu_subctlp != NULL);
#endif

	mutex_exit(lock);
}

/*
 * Callback of SCIF - the user is expected to log the supplied error
 * information
 */
void
scif_cb_logger_log_error(SCI_LOGGER_HANDLE_T logger_object,
    U32 log_object_mask, char *log_message, ...)
{
	va_list ap;

	mutex_enter(&scu_lib_log_lock);
	va_start(ap, log_message);
	(void) vsprintf(scu_lib_log_buf, log_message, ap);
	va_end(ap);

	if (sci_logger_is_enabled(logger_object, log_object_mask,
	    SCI_LOG_VERBOSITY_ERROR)) {
		SCUDBG(NULL, SCUDBG_SCIF, SCUDBG_ERROR, "%s",
		    scu_lib_log_buf);
	}
	mutex_exit(&scu_lib_log_lock);
}

/*
 * Callback of SCIF - the user is expected to log the supplied warnning
 * information
 */
void
scif_cb_logger_log_warning(SCI_LOGGER_HANDLE_T logger_object,
    U32 log_object_mask, char *log_message, ...)
{
	va_list ap;

	mutex_enter(&scu_lib_log_lock);
	va_start(ap, log_message);
	(void) vsprintf(scu_lib_log_buf, log_message, ap);
	va_end(ap);

	if (sci_logger_is_enabled(logger_object, log_object_mask,
	    SCI_LOG_VERBOSITY_WARNING)) {
		SCUDBG(NULL, SCUDBG_SCIF, SCUDBG_WARNING, "%s",
		    scu_lib_log_buf);
	}
	mutex_exit(&scu_lib_log_lock);
}

/*
 * Callback of SCIF - the user is expected to log the supplied debug
 * information
 */
void
scif_cb_logger_log_info(SCI_LOGGER_HANDLE_T logger_object,
    U32 log_object_mask, char *log_message, ...)
{
	va_list ap;

	mutex_enter(&scu_lib_log_lock);
	va_start(ap, log_message);
	(void) vsprintf(scu_lib_log_buf, log_message, ap);
	va_end(ap);

	if (sci_logger_is_enabled(logger_object, log_object_mask,
	    SCI_LOG_VERBOSITY_INFO)) {
		SCUDBG(NULL, SCUDBG_SCIF, SCUDBG_INFO, "%s",
		    scu_lib_log_buf);
	}
	mutex_exit(&scu_lib_log_lock);
}

/*
 * Callback of SCIF - the user is expected to log the supplied function
 * trace information
 */
void
scif_cb_logger_log_trace(SCI_LOGGER_HANDLE_T logger_object,
    U32 log_object_mask, char *log_message, ...)
{
	va_list ap;

	mutex_enter(&scu_lib_log_lock);
	va_start(ap, log_message);
	(void) vsprintf(scu_lib_log_buf, log_message, ap);
	va_end(ap);

	if (sci_logger_is_enabled(logger_object, log_object_mask,
	    SCI_LOG_VERBOSITY_TRACE)) {
		SCUDBG(NULL, SCUDBG_SCIF, SCUDBG_TRACE, "%s",
		    scu_lib_log_buf);
	}
	mutex_exit(&scu_lib_log_lock);
}

/*
 * Callback of SCIF - the user is expected to log the supplied state
 * transition information
 */
void
scif_cb_logger_log_states(SCI_LOGGER_HANDLE_T logger_object,
    U32 log_object_mask, char *log_message, ...)
{
	va_list ap;

	mutex_enter(&scu_lib_log_lock);
	va_start(ap, log_message);
	(void) vsprintf(scu_lib_log_buf, log_message, ap);
	va_end(ap);

	if (sci_logger_is_enabled(logger_object, log_object_mask,
	    SCI_LOG_VERBOSITY_STATES)) {
		SCUDBG(NULL, SCUDBG_SCIF, SCUDBG_STATES, "%s",
		    scu_lib_log_buf);
	}
	mutex_exit(&scu_lib_log_lock);
}

/*
 * Callback of SCIF
 */
/*ARGSUSED*/
void
scif_cb_remote_device_failed(SCI_CONTROLLER_HANDLE_T controller,
    SCI_DOMAIN_HANDLE_T domain, SCI_REMOTE_DEVICE_HANDLE_T remote_device,
    SCI_STATUS status)
{
	scu_subctl_t	*scu_subctlp;
	scu_ctl_t	*scu_ctlp;
	scu_iport_t	*scu_iportp;
	scu_tgt_t	*scu_tgtp;

	scu_subctlp = sci_object_get_association(controller);
	ASSERT(scu_subctlp != NULL);
	scu_ctlp = scu_subctlp->scus_ctlp;

	scu_iportp = sci_object_get_association(domain);

	SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_DOMAIN, SCUDBG_TRACE|SCUDBG_INFO,
	    "%s: scu_ctl 0x%p controller 0x%p scu_iport 0x%p domain 0x%p "
	    "remote_device 0x%p",
	    __func__, (void *)scu_ctlp, (void *)controller,
	    (void *)scu_iportp, (void *)domain, (void *)remote_device);

	scu_tgtp = sci_object_get_association(remote_device);
	if (scu_tgtp == NULL) {
		SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_DOMAIN,
		    SCUDBG_TRACE,
		    "%s: no scu_tgt is associated for remote device 0x%p",
		    __func__, (void *)remote_device);
		return;
	}

	mutex_enter(&scu_tgtp->scut_lock);
	if (scu_tgtp->scut_lib_tgt_valid == 1) {
		scu_tgtp->scut_lib_tgt_ready = 0;
		SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_DOMAIN, SCUDBG_WARNING,
		    "%s: scu_tgt 0x%p associated remote device 0x%p "
		    "in failed state",
		    __func__, (void *)scu_tgtp, (void *)remote_device);
	}
	mutex_exit(&scu_tgtp->scut_lock);
}

/*
 * Callback of SCIF - informs the framework user that the remote device
 * is not ready
 */
/*ARGSUSED*/
void
scif_cb_remote_device_not_ready(SCI_CONTROLLER_HANDLE_T controller,
    SCI_DOMAIN_HANDLE_T domain, SCI_REMOTE_DEVICE_HANDLE_T remote_device)
{
	scu_subctl_t	*scu_subctlp;
	scu_ctl_t	*scu_ctlp;
	scu_iport_t	*scu_iportp;
	scu_tgt_t	*scu_tgtp;

	scu_subctlp = sci_object_get_association(controller);
	ASSERT(scu_subctlp != NULL);
	scu_ctlp = scu_subctlp->scus_ctlp;

	scu_iportp = sci_object_get_association(domain);

	SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_DOMAIN, SCUDBG_TRACE,
	    "%s: scu_ctl 0x%p controller 0x%p scu_iport 0x%p domain 0x%p "
	    "remote_device 0x%p",
	    __func__, (void *)scu_ctlp, (void *)controller,
	    (void *)scu_iportp, (void *)domain, (void *)remote_device);

	scu_tgtp = sci_object_get_association(remote_device);
	if (scu_tgtp == NULL) {
		SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_DOMAIN, SCUDBG_TRACE,
		    "%s: no scu_tgt is associated for remote device 0x%p",
		    __func__, (void *)remote_device);
		return;
	}

	mutex_enter(&scu_tgtp->scut_lock);
	if (scu_tgtp->scut_lib_tgt_valid == 1) {
		scu_tgtp->scut_lib_tgt_ready = 0;
		SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_DOMAIN, SCUDBG_WARNING,
		    "%s: scu_tgt 0x%p associated remote device 0x%p "
		    "out of ready state",
		    __func__, (void *)scu_tgtp, (void *)remote_device);
	}
	mutex_exit(&scu_tgtp->scut_lock);
}

/*
 * Callback of SCIF - informs the framework user that the remote device
 * is ready and capable of processing IO requests
 */
/*ARGSUSED*/
void
scif_cb_remote_device_ready(SCI_CONTROLLER_HANDLE_T controller,
    SCI_DOMAIN_HANDLE_T domain, SCI_REMOTE_DEVICE_HANDLE_T remote_device)
{
	scu_subctl_t	*scu_subctlp;
	scu_ctl_t	*scu_ctlp;
	scu_iport_t	*scu_iportp;
	scu_tgt_t	*scu_tgtp;

	scu_subctlp = sci_object_get_association(controller);
	ASSERT(scu_subctlp != NULL);
	scu_ctlp = scu_subctlp->scus_ctlp;

	scu_iportp = sci_object_get_association(domain);

	SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_DOMAIN, SCUDBG_TRACE|SCUDBG_INFO,
	    "%s: scu_ctl 0x%p controller 0x%p scu_iport 0x%p domain 0x%p "
	    "remote_device 0x%p",
	    __func__, (void *)scu_ctlp, (void *)controller,
	    (void *)scu_iportp, (void *)domain, (void *)remote_device);

	scu_tgtp = sci_object_get_association(remote_device);
	if (scu_tgtp == NULL) {
		SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_DOMAIN,
		    SCUDBG_TRACE|SCUDBG_INFO,
		    "%s: no scu_tgt is associated for remote device 0x%p",
		    __func__, (void *)remote_device);
		return;
	}

	mutex_enter(&scu_tgtp->scut_lock);
	if (scu_tgtp->scut_lib_tgt_valid == 1) {
		scu_tgtp->scut_lib_tgt_ready = 1;
		SCUDBG(scu_ctlp, SCUDBG_TGT|SCUDBG_DOMAIN, SCUDBG_INFO,
		    "%s: scu_tgt 0x%p associated remote device 0x%p "
		    "in ready state",
		    __func__, (void *)scu_tgtp, (void *)remote_device);
		cv_signal(&scu_tgtp->scut_reset_complete_cv);
	}
	mutex_exit(&scu_tgtp->scut_lock);

	mutex_enter(&scu_ctlp->scu_event_lock);
	scu_event_dispatch(scu_ctlp, SCU_EVENT_ID_DEVICE_READY,
	    scu_tgtp->scut_tgt_num);
	mutex_exit(&scu_ctlp->scu_event_lock);
}

/*
 * Callback of SCIF - ask the user to provide the contents of the "address"
 * field in the Scatter-Gather Element
 */
SCI_PHYSICAL_ADDRESS
scif_cb_sge_get_address_field(void *scif_user_io_request, void *sge_address)
{
	scu_cmd_t	*scu_cmdp = (scu_cmd_t *)scif_user_io_request;
	scu_ctl_t *scu_ctlp = scu_cmdp->cmd_tgtp->scut_ctlp;

	ddi_dma_cookie_t *cookie = (ddi_dma_cookie_t *)sge_address;
	ASSERT(cookie != NULL);

	SCUDBG(scu_ctlp, SCUDBG_IO, SCUDBG_TRACE|SCUDBG_INFO,
	    "%s: sge 0x%p addr field 0x%"PRIx64, __func__, sge_address,
	    cookie->dmac_laddress);

	return (cookie->dmac_laddress);
}

/*
 * Callback of SCIF - ask the user to provide the contents of the "length"
 * field in the Scatter-Gather Element
 */
U32
scif_cb_sge_get_length_field(void *scif_user_io_request, void *sge_address)
{
	scu_cmd_t	*scu_cmdp = (scu_cmd_t *)scif_user_io_request;
	scu_ctl_t *scu_ctlp = scu_cmdp->cmd_tgtp->scut_ctlp;

	ddi_dma_cookie_t *cookie = (ddi_dma_cookie_t *)sge_address;
	ASSERT(cookie != NULL);

	SCUDBG(scu_ctlp, SCUDBG_IO, SCUDBG_TRACE|SCUDBG_INFO,
	    "%s: sge 0x%p len field %lu", __func__, sge_address,
	    cookie->dmac_size);

	return (cookie->dmac_size);
}

/*
 * Callback of SCIF - create an OS specific deferred task for internal usage
 */
void
scif_cb_start_internal_io_task_create(SCI_CONTROLLER_HANDLE_T controller)
{
	scu_subctl_t	*scu_subctlp;
	scu_ctl_t	*scu_ctlp;

	scu_subctlp = sci_object_get_association(controller);
	ASSERT(scu_subctlp != NULL);
	scu_ctlp = scu_subctlp->scus_ctlp;

	if (scu_subctlp->scus_lib_internal_taskq == NULL) {
		SCUDBG(scu_ctlp, SCUDBG_IO, SCUDBG_TRACE|SCUDBG_ERROR,
		    "%s: cannot find ossl related taskq for SCIF "
		    "internal io task request", __func__);
		return;
	}

	SCUDBG(scu_ctlp, SCUDBG_IO, SCUDBG_TRACE|SCUDBG_INFO,
	    "%s: find ossl related taskq 0x%p for SCIF internal "
	    "io task request", __func__,
	    (void *)scu_subctlp->scus_lib_internal_taskq);
}

/*
 * Callback of SCIF - schedule a OS specific deferred task
 */
void
scif_cb_start_internal_io_task_schedule(SCI_CONTROLLER_HANDLE_T controller,
    FUNCPTR start_internal_io_task_routine, void *context)
{
	scu_subctl_t	*scu_subctlp;
	scu_ctl_t	*scu_ctlp;

	scu_subctlp = sci_object_get_association(controller);
	ASSERT(scu_subctlp != NULL);
	ASSERT(scu_subctlp->scus_lib_internal_taskq != NULL);
	scu_ctlp = scu_subctlp->scus_ctlp;

	if (ddi_taskq_dispatch(scu_subctlp->scus_lib_internal_taskq,
	    (void (*)(void *))start_internal_io_task_routine,
	    context, DDI_NOSLEEP) != DDI_SUCCESS) {
		SCUDBG(scu_ctlp, SCUDBG_IO, SCUDBG_TRACE|SCUDBG_ERROR,
		    "%s: ddi_taskq_dispatch failed for routine 0x%p",
		    __func__, (void *)start_internal_io_task_routine);
		return;
	}

	SCUDBG(scu_ctlp, SCUDBG_IO, SCUDBG_TRACE|SCUDBG_INFO,
	    "%s: ddi_taskq_dispatch succeed for routine 0x%p",
	    __func__, (void *)start_internal_io_task_routine);
}

/*
 * Callback of SCIF - inform the user that a task management request completed
 */
void
scif_cb_task_request_complete(SCI_CONTROLLER_HANDLE_T controller,
    SCI_REMOTE_DEVICE_HANDLE_T remote_device,
    SCI_TASK_REQUEST_HANDLE_T task_request, SCI_TASK_STATUS completion_status)
{
	scu_subctl_t	*scu_subctlp;
	scu_ctl_t	*scu_ctlp;
	scu_io_slot_t	*scu_slotp;
	scu_cmd_t	*scu_cmdp;
	scu_tgt_t	*scu_tgtp;
	int		slot_locked;
	int		cmd_poll;

	scu_subctlp = (scu_subctl_t *)sci_object_get_association(controller);
	scu_ctlp = scu_subctlp->scus_ctlp;
	scu_slotp = (scu_io_slot_t *)sci_object_get_association(task_request);

	/*
	 * XXX Conditional locking is ugly, but see
	 * scif_cb_io_request_complete() for detail
	 */
	if (scu_is_task_start_thread(scu_subctlp) == 0) {
		slot_locked = 0;
		mutex_enter(&scu_subctlp->scus_slot_lock);
	} else {
		slot_locked = 1;
	}

	scu_cmdp = scu_slotp->scu_io_slot_cmdp;
	if (scu_cmdp) {
		scu_tgtp = scu_cmdp->cmd_tgtp;
		SCUDBG(scu_ctlp, SCUDBG_TASK|SCUDBG_INTR,
		    SCUDBG_TRACE|SCUDBG_INFO,
		    "%s: enter scu_tgtp 0x%p, scu_cmdp 0x%p, "
		    "completion_status %d", __func__, (void *)scu_tgtp,
		    (void *)scu_cmdp, completion_status);
		/* First check completion status */
		switch (completion_status) {
		case SCI_TASK_SUCCESS:
			SCUDBG(scu_ctlp, SCUDBG_TASK|SCUDBG_INTR,
			    SCUDBG_INFO, "%s: scu_cmdp 0x%p got succeeded",
			    __func__, (void *)scu_cmdp);
			scu_cmdp->cmd_task_reason = SCU_TASK_GOOD;
			break;

		case SCI_TASK_FAILURE_RESPONSE_VALID:
			SCUDBG(scu_ctlp, SCUDBG_TASK|SCUDBG_INTR,
			    SCUDBG_INFO, "%s: scu_cmdp 0x%p got failure with "
			    "response valid status", __func__,
			    (void *)scu_cmdp);
			scu_cmdp->cmd_task_reason = SCU_TASK_GOOD;
			break;

		case SCI_TASK_FAILURE_RESET_DEVICE_PARTIAL_SUCCESS:
			SCUDBG(scu_ctlp, SCUDBG_TASK|SCUDBG_INTR,
			    SCUDBG_INFO, "%s: scu_cmdp 0x%p got failure with "
			    "reset device partial success",
			    __func__, (void *)scu_cmdp);
			scu_cmdp->cmd_task_reason = SCU_TASK_GOOD;
			break;

		case SCI_TASK_FAILURE_INVALID_STATE:
			SCUDBG(scu_ctlp, SCUDBG_TASK|SCUDBG_INTR,
			    SCUDBG_INFO|SCUDBG_ERROR,
			    "%s: scu_cmdp 0x%p got failed with invalid state",
			    __func__, (void *)scu_cmdp);
			scu_cmdp->cmd_task_reason = SCU_TASK_BUSY;
			break;

		case SCI_TASK_FAILURE_INSUFFICIENT_RESOURCES:
			SCUDBG(scu_ctlp, SCUDBG_TASK|SCUDBG_INTR,
			    SCUDBG_INFO|SCUDBG_ERROR,
			    "%s: scu_cmdp 0x%p failed with "
			    "insufficient resources",
			    __func__, (void *)scu_cmdp);
			scu_cmdp->cmd_task_reason = SCU_TASK_BUSY;
			break;

		case SCI_TASK_FAILURE_UNSUPPORTED_PROTOCOL:
			SCUDBG(scu_ctlp, SCUDBG_TASK|SCUDBG_INTR,
			    SCUDBG_INFO|SCUDBG_ERROR,
			    "%s: scu_cmdp 0x%p failed with "
			    "unsupported protocol",
			    __func__, (void *)scu_cmdp);
			scu_cmdp->cmd_task_reason = SCU_TASK_ERROR;
			break;

		case SCI_TASK_FAILURE_INVALID_TAG:
			SCUDBG(scu_ctlp, SCUDBG_TASK|SCUDBG_INTR,
			    SCUDBG_INFO|SCUDBG_ERROR,
			    "%s: scu_cmdp 0x%p got failed with invalid tag",
			    __func__, (void *)scu_cmdp);
			scu_cmdp->cmd_task_reason = SCU_TASK_ERROR;
			break;

		case SCI_TASK_FAILURE_TERMINATED:
			SCUDBG(scu_ctlp, SCUDBG_TASK|SCUDBG_INTR,
			    SCUDBG_INFO|SCUDBG_ERROR,
			    "%s: scu_cmdp 0x%p failed with terminated status",
			    __func__, (void *)scu_cmdp);
			scu_cmdp->cmd_task_reason = SCU_TASK_ERROR;
			break;

		case SCI_TASK_FAILURE_INVALID_PARAMETER_VALUE:
			SCUDBG(scu_ctlp, SCUDBG_TASK|SCUDBG_INTR,
			    SCUDBG_INFO|SCUDBG_ERROR, "%s: scu_cmdp 0x%p got "
			    "failed with invalid parameter value status",
			    __func__, (void *)scu_cmdp);
			scu_cmdp->cmd_task_reason = SCU_TASK_ERROR;
			break;

		case SCI_TASK_FAILURE:
			SCUDBG(scu_ctlp, SCUDBG_TASK|SCUDBG_INTR,
			    SCUDBG_INFO|SCUDBG_ERROR, "%s: scu_cmdp 0x%p got "
			    "failed with failure status",
			    __func__, (void *)scu_cmdp);
			scu_cmdp->cmd_task_reason = SCU_TASK_ERROR;
			break;

		case SCI_TASK_FAILURE_CONTROLLER_SPECIFIC_ERR:
			SCUDBG(scu_ctlp, SCUDBG_TASK|SCUDBG_INTR,
			    SCUDBG_INFO|SCUDBG_ERROR, "%s: scu_cmdp 0x%p got "
			    "failed with controller specific err status",
			    __func__, (void *)scu_cmdp);
			scu_cmdp->cmd_task_reason = SCU_TASK_ERROR;
			break;

		case SCI_TASK_FAILURE_REMOTE_DEVICE_RESET_REQUIRED:
			SCUDBG(scu_ctlp, SCUDBG_TASK|SCUDBG_INTR,
			    SCUDBG_INFO|SCUDBG_ERROR, "%s: scu_cmdp 0x%p got "
			    "failed with remote device reset required status",
			    __func__, (void *)scu_cmdp);
			scu_cmdp->cmd_task_reason = SCU_TASK_DEVICE_RESET;
			break;

		case SCI_FAILURE_TIMEOUT:
			SCUDBG(scu_ctlp, SCUDBG_TASK|SCUDBG_INTR,
			    SCUDBG_INFO|SCUDBG_ERROR, "%s: scu_cmdp 0x%p got "
			    "failed with remote device reset required status",
			    __func__, (void *)scu_cmdp);
			scu_cmdp->cmd_task_reason = SCU_TASK_ERROR;
			break;

		default:
			scu_log(scu_ctlp, CE_WARN,
			    "!%s: scu_cmdp 0x%p got failure with unrecognized "
			    "%d status", __func__,
			    (void *)scu_cmdp, completion_status);
			scu_cmdp->cmd_task_reason = SCU_TASK_ERROR;
			break;
		}

		ASSERT(scu_cmdp->cmd_started);
		/* Detach slot */
		(void) scu_detach_cmd(scu_cmdp);
		/* Update command state */
		if (scu_cmdp->cmd_finished == 0)
			scu_cmdp->cmd_finished = 1;
		cmd_poll = scu_cmdp->cmd_poll;
	} else {
		SCUDBG(scu_ctlp, SCUDBG_TASK|SCUDBG_INTR,
		    SCUDBG_TRACE|SCUDBG_INFO,
		    "%s: enter scu_tgtp NULL, scu_cmdp NULL, "
		    "completion_status %d", __func__, completion_status);
		cmd_poll = 1;
	}

	(void) scif_controller_complete_task(controller,
	    remote_device, task_request);
	scu_subctlp->scus_slot_active_num--;
	if (scu_slotp->scu_io_lib_tag != SCI_CONTROLLER_INVALID_IO_TAG)
		scu_free_tag(scu_subctlp, scu_slotp);
	if (slot_locked == 0)
		mutex_exit(&scu_subctlp->scus_slot_lock);

	if (cmd_poll == 0) {
		/* Do cv here to avoid possible cq handler recursive wait */
		ASSERT(scu_cmdp->cmd_sync);
		cv_broadcast(&scu_ctlp->scu_cmd_complete_cv);
	}
}

/*
 * Callback of SCIF - returns the task management function to be utilized for
 * this task request
 */
U8
scif_cb_task_request_get_function(void *scif_user_task_request)
{
	scu_cmd_t	*scu_cmdp;

	scu_cmdp = ((scu_io_slot_t *)scif_user_task_request)->scu_io_slot_cmdp;

	SCUDBG(scu_cmdp->cmd_tgtp->scut_ctlp, SCUDBG_TASK,
	    SCUDBG_TRACE|SCUDBG_INFO,
	    "%s: scu_cmdp 0x%p, function %d", __func__, (void *)scu_cmdp,
	    scu_cmdp->cmd_task_arg.scu_task_function);

	return (scu_cmdp->cmd_task_arg.scu_task_function);
}

/*
 * Callback of SCIF - returns the task management IO tag to be managed
 */
U16
scif_cb_task_request_get_io_tag_to_manage(void *scif_user_task_request)
{
	scu_cmd_t	*scu_cmdp;

	scu_cmdp = ((scu_io_slot_t *)scif_user_task_request)->scu_io_slot_cmdp;

	SCUDBG(scu_cmdp->cmd_tgtp->scut_ctlp, SCUDBG_TASK,
	    SCUDBG_TRACE,
	    "%s: scu_cmdp 0x%p, io_tag %d to be managed", __func__,
	    (void *)scu_cmdp, scu_cmdp->cmd_task_arg.scu_task_tag);

	return (scu_cmdp->cmd_task_arg.scu_task_tag);
}

/*
 * Callback of SCIF - returns the Logical Unit to be utilized for this
 * task management request
 */
U32
scif_cb_task_request_get_lun(void *scif_user_task_request)
{
	scu_cmd_t	*scu_cmdp;
	uint32_t	lun_num;

	scu_cmdp = ((scu_io_slot_t *)scif_user_task_request)->scu_io_slot_cmdp;
	lun_num = scu_cmdp->cmd_task_arg.scu_task_lun & 0xffffffff;

	SCUDBG(scu_cmdp->cmd_tgtp->scut_ctlp, SCUDBG_TASK,
	    SCUDBG_TRACE,
	    "%s: scu_cmdp 0x%p, lun num %u",
	    __func__, (void *)scu_cmdp, lun_num);

	return (lun_num);
}

/*
 * Callback of SCIF - asks the user to provide the virtual address of
 * the repsonse data buffer for the supplied IO request
 */
void *
scif_cb_task_request_get_response_data_address(void *scif_user_task_request)
{
	scu_io_slot_t	*scu_slotp = scif_user_task_request;
	scu_subctl_t	*scu_subctlp = scu_slotp->scu_io_subctlp;
	scu_cmd_t	*scu_cmdp;
	void		*addr;

	mutex_enter(&scu_subctlp->scus_slot_lock);
	scu_cmdp = scu_slotp->scu_io_slot_cmdp;
	if (scu_cmdp) {
		addr = (void *)scu_cmdp->cmd_task_arg.scu_task_resp_addr;
		SCUDBG(scu_cmdp->cmd_tgtp->scut_ctlp, SCUDBG_TASK, SCUDBG_TRACE,
		    "%s: scu_cmdp 0x%p, response addr 0x%p",
		    __func__, (void *)scu_cmdp, addr);
	} else {
		addr = NULL;
		SCUDBG(scu_subctlp->scus_ctlp, SCUDBG_TASK, SCUDBG_INFO,
		    "%s: scu_slotp 0x%p not attached",
		    __func__, (void *)scu_slotp);
	}
	mutex_exit(&scu_subctlp->scus_slot_lock);
	return (addr);
}

/*
 * Callback of SCIF - asks the user to provide the length of the response
 * data buffer for the supplied IO request
 */
U32
scif_cb_task_request_get_response_data_length(void *scif_user_task_request)
{
	scu_io_slot_t	*scu_slotp = scif_user_task_request;
	scu_subctl_t	*scu_subctlp = scu_slotp->scu_io_subctlp;
	scu_cmd_t	*scu_cmdp;
	U32		len;

	mutex_enter(&scu_subctlp->scus_slot_lock);
	scu_cmdp = scu_slotp->scu_io_slot_cmdp;
	if (scu_cmdp) {
		len = scu_cmdp->cmd_task_arg.scu_task_resp_len;
		SCUDBG(scu_cmdp->cmd_tgtp->scut_ctlp, SCUDBG_TASK, SCUDBG_TRACE,
		    "%s: scu_cmdp 0x%p, response len %u",
		    __func__, (void *)scu_cmdp, len);
	} else {
		len = 0;
		SCUDBG(scu_subctlp->scus_ctlp, SCUDBG_TASK, SCUDBG_INFO,
		    "%s: scu_slotp 0x%p not attached",
		    __func__, (void *)scu_slotp);
	}
	mutex_exit(&scu_subctlp->scus_slot_lock);
	return (len);
}

/*
 * Callback of SCIF - create a timer and provide a handle for use
 */
void *
scif_cb_timer_create(SCI_CONTROLLER_HANDLE_T controller,
    SCI_TIMER_CALLBACK_T timer_callback, void *cookie)
{
	scu_timer_t	*scu_timer;
	scu_subctl_t	*scu_subctlp;
	scu_ctl_t	*scu_ctlp;

	scu_subctlp = (scu_subctl_t *)sci_object_get_association(controller);
	scu_ctlp = scu_subctlp->scus_ctlp;

	scu_timer = kmem_zalloc(sizeof (scu_timer_t), KM_SLEEP);

	scu_timer->timeout_id = 0;
	scu_timer->callback = timer_callback;
	scu_timer->args = cookie;

	SCUDBG(scu_ctlp, SCUDBG_TIMER, SCUDBG_INFO|SCUDBG_TRACE,
	    "%s: scu_timer 0x%p is created", __func__, (void *)scu_timer);

	return (scu_timer);
}

/*
 * Callback of SCIF - ask the user to destroy the supplied timer
 */
void
scif_cb_timer_destroy(SCI_CONTROLLER_HANDLE_T controller, void *timer)
{
	scu_timer_t	*scu_timer = timer;
	scu_subctl_t	*scu_subctlp;
	scu_ctl_t	*scu_ctlp;

	scu_subctlp = (scu_subctl_t *)sci_object_get_association(controller);
	scu_ctlp = scu_subctlp->scus_ctlp;

	SCUDBG(scu_ctlp, SCUDBG_TIMER, SCUDBG_INFO|SCUDBG_TRACE,
	    "%s: scu_timer 0x%p is destroyed", __func__, (void *)scu_timer);

	if (scu_timer->timeout_id != 0)
		(void) untimeout(scu_timer->timeout_id);

	kmem_free(scu_timer, sizeof (scu_timer_t));
	scu_timer = NULL;
}

/*
 * Callback of SCIF - ask the user to start the supplied timer
 */
void
scif_cb_timer_start(SCI_CONTROLLER_HANDLE_T controller, void *timer,
    U32 milliseconds)
{
	scu_timer_t	*scu_timer = timer;
	scu_subctl_t	*scu_subctlp;
	scu_ctl_t	*scu_ctlp;

	scu_subctlp = (scu_subctl_t *)sci_object_get_association(controller);
	scu_ctlp = scu_subctlp->scus_ctlp;

	SCUDBG(scu_ctlp, SCUDBG_TIMER, SCUDBG_INFO|SCUDBG_TRACE,
	    "%s: scu_timer 0x%p is started", __func__, (void *)scu_timer);

	scu_timer->timeout_id = timeout((void (*)(void *))scu_timer->callback,
	    (caddr_t)scu_timer->args, milliseconds * drv_usectohz(1000));
}

/*
 * Callback of SCIF - ask the user to stop the supplied timer
 */
void
scif_cb_timer_stop(SCI_CONTROLLER_HANDLE_T controller, void *timer)
{
	scu_timer_t	*scu_timer = timer;
	scu_subctl_t	*scu_subctlp;
	scu_ctl_t	*scu_ctlp;

	scu_subctlp = (scu_subctl_t *)sci_object_get_association(controller);
	scu_ctlp = scu_subctlp->scus_ctlp;

	SCUDBG(scu_ctlp, SCUDBG_TIMER, SCUDBG_INFO|SCUDBG_TRACE,
	    "%s: scu_timer 0x%p is stopped", __func__, (void *)scu_timer);

	if (scu_timer->timeout_id != 0) {
		(void) untimeout(scu_timer->timeout_id);
		scu_timer->timeout_id = 0;
	}
}


/*
 * Callback of SCIC - request an immediate blocking delay
 */
void
scic_cb_stall_execution(U32  microseconds)
{
	drv_usecwait(microseconds);
}

/*
 * Callback of SCIC - Requests the mapping address for the specified BAR#
 */
void *
scic_cb_pci_get_bar(SCI_CONTROLLER_HANDLE_T controller, U16 bar_number)
{
	void		*scif_controller;
	scu_subctl_t	*scu_subctlp;
	scu_ctl_t	*scu_ctlp;

	scif_controller = sci_object_get_association(controller);
	scu_subctlp =
	    (scu_subctl_t *)sci_object_get_association(scif_controller);
	ASSERT(scu_subctlp != NULL);
	scu_ctlp = scu_subctlp->scus_ctlp;

	if (bar_number >= SCU_MAX_BAR) {
		SCUDBG(scu_ctlp, SCUDBG_ADDR, SCUDBG_TRACE|SCUDBG_ERROR,
		    "%s: bar number %d SCU_MAX_BAR %d",
		    __func__, bar_number, SCU_MAX_BAR);
		return (NULL);
	}

	return (scu_ctlp->scu_bar_addr[bar_number]);
}

/*
 * Sub-routine for scic_cb_pci_read_dword and scic_cb_pci_write_dword
 */
static ddi_acc_handle_t *
scu_get_pci_acc_handle(scu_ctl_t *scu_ctlp, void *address)
{
	ddi_acc_handle_t	*handle = NULL;
	caddr_t			addr;
	int			i, j;

	for (i = 0; i < SCU_MAX_BAR; i++) {
		addr = scu_ctlp->scu_bar_addr[i];
		if (addr == 0 || addr > (caddr_t)address)
			continue;

		if (addr == address) {
			return (&(scu_ctlp->scu_bar_map[i]));
		}

		if (handle == NULL) {
			handle = &(scu_ctlp->scu_bar_map[i]);
			j = i;
		} else if (addr > scu_ctlp->scu_bar_addr[j]) {
			handle = &(scu_ctlp->scu_bar_map[i]);
			j = i;
		}
	}

	if (scu_check_acc_handle(*handle) != DDI_FM_OK) {
		ddi_fm_service_impact(scu_ctlp->scu_dip,
		    DDI_SERVICE_UNAFFECTED);
		ddi_fm_acc_err_clear(*handle, DDI_FME_VERSION);
		return (NULL);
	}

	return (handle);
}

/*
 * Callback of SCIC - Request a DWORD PCI read of the supplied address
 */
U32
scic_cb_pci_read_dword(SCI_CONTROLLER_HANDLE_T controller, void *address)
{
	void			*scif_controller;
	scu_subctl_t		*scu_subctlp;
	scu_ctl_t		*scu_ctlp;
	ddi_acc_handle_t	*handle;
	U32			ret_val;

	scif_controller = sci_object_get_association(controller);
	scu_subctlp =
	    (scu_subctl_t *)sci_object_get_association(scif_controller);
	ASSERT(scu_subctlp != NULL);
	scu_ctlp = scu_subctlp->scus_ctlp;

	handle = scu_get_pci_acc_handle(scu_ctlp, address);
	if (handle == NULL) {
		SCUDBG(scu_ctlp, SCUDBG_ADDR, SCUDBG_TRACE|SCUDBG_ERROR,
		    "%s: cannot get ossl related pci handle for addr 0x%p",
		    __func__, address);
		return (NULL);
	} else {
		SCUDBG(scu_ctlp, SCUDBG_ADDR, SCUDBG_TRACE|SCUDBG_INFO,
		    "%s: get ossl related pci handle 0x%p for addr 0x%p",
		    __func__, (void *)handle, address);
		ret_val = ddi_get32(*handle, (uint32_t *)address);
		if (scu_check_acc_handle(*handle) != DDI_FM_OK) {
			ddi_fm_service_impact(scu_ctlp->scu_dip,
			    DDI_SERVICE_UNAFFECTED);
			ddi_fm_acc_err_clear(*handle, DDI_FME_VERSION);
			return (NULL);
		} else {
			return (ret_val);
		}
	}
}

/*
 * Callback of SCIC - Request a DWORD PCI write of the supplied value
 */
void
scic_cb_pci_write_dword(SCI_CONTROLLER_HANDLE_T controller,
    void *address, U32 write_value)
{
	void			*scif_controller;
	scu_subctl_t		*scu_subctlp;
	scu_ctl_t		*scu_ctlp;
	ddi_acc_handle_t	*handle;

	scif_controller = sci_object_get_association(controller);
	scu_subctlp =
	    (scu_subctl_t *)sci_object_get_association(scif_controller);
	ASSERT(scu_subctlp != NULL);
	scu_ctlp = scu_subctlp->scus_ctlp;

	handle = scu_get_pci_acc_handle(scu_ctlp, address);
	if (handle == NULL) {
		SCUDBG(scu_ctlp, SCUDBG_ADDR, SCUDBG_TRACE|SCUDBG_ERROR,
		    "%s: cannot get ossl related pci handle for addr 0x%p",
		    __func__, address);
		return;
	} else {
		SCUDBG(scu_ctlp, SCUDBG_ADDR, SCUDBG_TRACE|SCUDBG_INFO,
		    "%s: get ossl related pci handle 0x%p for addr 0x%p",
		    __func__, (void *)handle, address);
		ddi_put32(*handle, (uint32_t *)address, write_value);
	}
}

/*
 * Callback of SCIC - Return physical address for given virtual address
 *
 * It's called by scic_cb_io_request_get_physical_address.
 */
SCI_PHYSICAL_ADDRESS
scic_cb_get_physical_address(SCI_CONTROLLER_HANDLE_T controller,
    void *virtual_address)
{
	void		*scif_controller;
	scu_subctl_t	*scu_subctlp;
	scu_ctl_t	*scu_ctlp;
	uint64_t	offset;
	uint64_t	paddr;

	scif_controller = sci_object_get_association(controller);
	scu_subctlp =
	    (scu_subctl_t *)sci_object_get_association(scif_controller);
	scu_ctlp = scu_subctlp->scus_ctlp;

	offset = (uintptr_t)virtual_address & MMU_PAGEOFFSET;

	paddr = mmu_ptob((paddr_t)(hat_getpfnum(kas.a_hat, virtual_address)));
	paddr += offset;

	SCUDBG(scu_ctlp, SCUDBG_IO|SCUDBG_ADDR, SCUDBG_TRACE|SCUDBG_INFO,
	    "%s: virtual_address 0x%p, physical_address 0x%"PRIx64,
	    __func__, (void *)virtual_address, paddr);

	return (paddr);
}

/*
 * Find out the corresponding virtual address for the supplied physical address
 * in the allocated memory descriptors
 */
static void *
scu_look_up_md_by_pa(scu_subctl_t *scu_subctlp,
    SCI_PHYSICAL_ADDRESS physical_address)
{
	SCI_CONTROLLER_HANDLE_T			controller;
	SCI_MEMORY_DESCRIPTOR_LIST_HANDLE_T	mdl_handle;
	SCI_PHYSICAL_MEMORY_DESCRIPTOR_T	*current_mde;
	void					*virtual_address = NULL;

	controller = scu_subctlp->scus_scif_ctl_handle;
	mdl_handle = sci_controller_get_memory_descriptor_list_handle(
	    controller);

	/* Rewind to the first memory descriptor entry in the list */
	sci_mdl_first_entry(mdl_handle);

	/* Get the current memory descriptor entry */
	current_mde = sci_mdl_get_current_entry(mdl_handle);

	while (current_mde) {
		if ((physical_address >= current_mde->physical_address) &&
		    (physical_address <= (current_mde->physical_address +
		    current_mde->constant_memory_size))) {
			virtual_address =
			    (caddr_t)((POINTER_UINT)
			    current_mde->virtual_address +
			    (uint32_t)(physical_address -
			    current_mde->physical_address));
			return (virtual_address);
		}

		/* Move pointer to next sequential memory descriptor */
		sci_mdl_next_entry(mdl_handle);
		current_mde = sci_mdl_get_current_entry(mdl_handle);
	}

	return (virtual_address);
}

/*
 * Callback of SCIC - Return virtual address for given physical address
 *
 * currently there are three functions which will call this method
 *	scic_sds_stp_request_pio_data_in_copy_data_buffer
 *	scic_sds_stp_request_pio_get_next_sgl
 *	scic_sds_unsolicited_frame_control_construct
 *
 * If somehow SATA PIO read is used, this cb should be modified. Otherwise,
 * we are safe to do translation only for MDL elements.
 */
void *
scic_cb_get_virtual_address(SCI_CONTROLLER_HANDLE_T controller,
    SCI_PHYSICAL_ADDRESS physical_address)
{
	void		*scif_controller;
	scu_subctl_t	*scu_subctlp;
	scu_ctl_t	*scu_ctlp;
	void		*virtual_address;

	scif_controller = sci_object_get_association(controller);
	scu_subctlp =
	    (scu_subctl_t *)sci_object_get_association(scif_controller);
	scu_ctlp = scu_subctlp->scus_ctlp;

	SCUDBG(scu_ctlp, SCUDBG_ADDR, SCUDBG_TRACE,
	    "%s: enter physical_address 0x%"PRIx64,
	    __func__, physical_address);

	/* First look up memory descriptors */
	virtual_address = scu_look_up_md_by_pa(scu_subctlp, physical_address);

	SCUDBG(scu_ctlp, SCUDBG_ADDR, SCUDBG_TRACE,
	    "%s: return virtual_address 0x%p",
	    __func__, (void *)virtual_address);

	if (virtual_address == NULL) {
		SCUDBG(scu_ctlp, SCUDBG_ADDR, SCUDBG_ERROR,
		    "%s: cannot find virtual address for "
		    "physical address 0x%"PRIx64" in md",
		    __func__, physical_address);
	}

	return (virtual_address);
}

/*
 * Callback of SCIC - the user is expected to log the supplied error
 * information
 */
void
scic_cb_logger_log_error(SCI_LOGGER_HANDLE_T logger_object,
    U32 log_object_mask, char *log_message, ...)
{
	va_list ap;

	mutex_enter(&scu_lib_log_lock);
	va_start(ap, log_message);
	(void) vsprintf(scu_lib_log_buf, log_message, ap);
	va_end(ap);

	if (sci_logger_is_enabled(logger_object, log_object_mask,
	    SCI_LOG_VERBOSITY_ERROR)) {
		SCUDBG(NULL, SCUDBG_SCIC, SCUDBG_ERROR, "%s",
		    scu_lib_log_buf);
	}
	mutex_exit(&scu_lib_log_lock);
}

/*
 * Callback of SCIC - the user is expected to log the supplied warnning
 * information
 */
void
scic_cb_logger_log_warning(SCI_LOGGER_HANDLE_T logger_object,
    U32 log_object_mask, char *log_message, ...)
{
	va_list ap;

	mutex_enter(&scu_lib_log_lock);
	va_start(ap, log_message);
	(void) vsprintf(scu_lib_log_buf, log_message, ap);
	va_end(ap);

	if (sci_logger_is_enabled(logger_object, log_object_mask,
	    SCI_LOG_VERBOSITY_WARNING)) {
		SCUDBG(NULL, SCUDBG_SCIC, SCUDBG_WARNING, "%s",
		    scu_lib_log_buf);
	}
	mutex_exit(&scu_lib_log_lock);
}

/*
 * Callback of SCIC - the user is expected to log the supplied debug
 * information
 */
void
scic_cb_logger_log_info(SCI_LOGGER_HANDLE_T logger_object,
    U32 log_object_mask, char *log_message, ...)
{
	va_list ap;

	mutex_enter(&scu_lib_log_lock);
	va_start(ap, log_message);
	(void) vsprintf(scu_lib_log_buf, log_message, ap);
	va_end(ap);

	if (sci_logger_is_enabled(logger_object, log_object_mask,
	    SCI_LOG_VERBOSITY_INFO)) {
		SCUDBG(NULL, SCUDBG_SCIC, SCUDBG_INFO, "%s",
		    scu_lib_log_buf);
	}
	mutex_exit(&scu_lib_log_lock);
}

/*
 * Callback of SCIC - the user is expected to log the supplied function
 * trace information
 */
void
scic_cb_logger_log_trace(SCI_LOGGER_HANDLE_T logger_object,
    U32 log_object_mask, char *log_message, ...)
{
	va_list ap;

	mutex_enter(&scu_lib_log_lock);
	va_start(ap, log_message);
	(void) vsprintf(scu_lib_log_buf, log_message, ap);
	va_end(ap);

	if (sci_logger_is_enabled(logger_object, log_object_mask,
	    SCI_LOG_VERBOSITY_TRACE)) {
		SCUDBG(NULL, SCUDBG_SCIC, SCUDBG_TRACE, "%s",
		    scu_lib_log_buf);
	}
	mutex_exit(&scu_lib_log_lock);
}

/*
 * Callback of SCIF - the user is expected to log the supplied state
 * transition information
 */
void
scic_cb_logger_log_states(SCI_LOGGER_HANDLE_T logger_object,
    U32 log_object_mask, char *log_message, ...)
{
	va_list ap;

	mutex_enter(&scu_lib_log_lock);
	va_start(ap, log_message);
	(void) vsprintf(scu_lib_log_buf, log_message, ap);
	va_end(ap);

	if (sci_logger_is_enabled(logger_object, log_object_mask,
	    SCI_LOG_VERBOSITY_STATES)) {
		SCUDBG(NULL, SCUDBG_SCIC, SCUDBG_STATES, "%s",
		    scu_lib_log_buf);
	}
	mutex_exit(&scu_lib_log_lock);
}

/*
 * Callback of SCIC - SCIC will call when a phy/link became ready, but it's
 * not allowed in the port.
 */
/*ARGSUSED*/
void scic_cb_port_invalid_link_up(SCI_CONTROLLER_HANDLE_T controller,
    SCI_PORT_HANDLE_T port, SCI_PHY_HANDLE_T phy)
{
	void		*scif_controller;
	scu_subctl_t	*scu_subctlp;
	scu_ctl_t	*scu_ctlp;

	scif_controller = sci_object_get_association(controller);
	scu_subctlp =
	    (scu_subctl_t *)sci_object_get_association(scif_controller);
	scu_ctlp = scu_subctlp->scus_ctlp;

	SCUDBG(scu_ctlp, SCUDBG_PHY, SCUDBG_ERROR,
	    "%s: enter scu_ctl 0x%p port 0x%p phy 0x%p",
	    __func__, (void *)scu_ctlp, (void *)port, (void *)phy);
}

/*
 * Callback of SCIC - ask the user to provide the physical address for
 * the supplied virtual address when building an io request object
 *
 * This callback won't be used when the below MACRO is defined
 * SCI_GET_PHYSICAL_ADDRESS_OPTIMIZATION_ENABLED in sci_environment.h
 *
 * Currently it's called by below functions:
 *	scu_ssp_reqeust_construct_task_context in scic_sds_request.c
 *	scic_sds_request_build_sgl in scic_sds_request.c
 *	scu_smp_request_construct_task_context in scic_sds_smp_request.c
 *	scu_sata_reqeust_construct_task_context in scic_sds_stp_request.c
 */
/*ARGSUSED*/
void scic_cb_io_request_get_physical_address(SCI_CONTROLLER_HANDLE_T controller,
    SCI_IO_REQUEST_HANDLE_T io_request, void *virtual_address,
    SCI_PHYSICAL_ADDRESS *physical_address)
{
	void		*scif_controller;
	scu_subctl_t	*scu_subctlp;
	scu_ctl_t	*scu_ctlp;

	scif_controller = sci_object_get_association(controller);
	scu_subctlp =
	    (scu_subctl_t *)sci_object_get_association(scif_controller);
	scu_ctlp = scu_subctlp->scus_ctlp;

	*physical_address =
	    scic_cb_get_physical_address(controller, virtual_address);

	SCUDBG(scu_ctlp, SCUDBG_IO|SCUDBG_ADDR, SCUDBG_TRACE|SCUDBG_INFO,
	    "%s: virtual_address 0x%p, physical_address 0x%"PRIx64,
	    __func__, (void *)virtual_address, *physical_address);
}

/*
 * get the phy identifier for passthrough request
 */
/*ARGSUSED*/
U32
scu_scic_cb_passthru_get_phy_identifier(void *arg1, U8 *arg2)
{
	return (0);
}

/*
 * get the port identifier for passthrough request
 */
/*ARGSUSED*/
U32
scu_scic_cb_passthru_get_port_identifier(void *arg1, U8 *arg2)
{
	return (0);
}

/*
 * get the connection rate for passthrough request
 */
/*ARGSUSED*/
U32
scu_scic_cb_passthru_get_connection_rate(void *arg1, void *arg2)
{
	return (0);
}

/*
 * get the destination sas address for passthrough request
 */
/*ARGSUSED*/
void
scu_scic_cb_passthru_get_destination_sas_address(void *arg1, U8 **arg2)
{
}

/*
 * get the transfer length for passthrough request
 */
/*ARGSUSED*/
U32
scu_scic_cb_passthru_get_transfer_length(void *request)
{
	return (0);
}

/*
 * get the data direction for passthrough request
 */
/*ARGSUSED*/
U32
scu_scic_cb_passthru_get_data_direction(void *request)
{
	return (0);
}

/*
 * get the buffer and length for the smp request
 */
U32
scu_scic_cb_smp_passthru_get_request(void *request, U8 **request_buffer)
{
	SCI_IO_REQUEST_HANDLE_T	scif_request;
	scu_cmd_t		*scu_cmdp;
	smp_request_frame_t	*srq;
	U32			request_length;

	scif_request = sci_object_get_association(request);
	scu_cmdp = sci_object_get_association(scif_request);
	ASSERT(scu_cmdp != NULL);

	srq = (smp_request_frame_t *)scu_cmdp->cmd_smp_pkt->smp_pkt_req;
	request_length = 4 * (srq->srf_request_len);

	if (request_length == 0) {
		SCUDBG(NULL, SCUDBG_SMP, SCUDBG_INFO,
		    "%s: srf_request_length is zero for smp_pkt 0x%p",
		    __func__, (void *)scu_cmdp->cmd_smp_pkt);
		request_length =
		    (U32)(scu_cmdp->cmd_smp_pkt->smp_pkt_reqsize -
		    SMP_REQ_MINLEN);
	}

	*request_buffer = (U8 *)
	    (scu_cmdp->cmd_smp_pkt->smp_pkt_req +
	    offsetof(smp_request_frame_t, srf_data[0]));

	SCUDBG(NULL, SCUDBG_SMP, SCUDBG_INFO,
	    "%s: scu_cmd 0x%p smp_pkt 0x%p request_buffer 0x%p "
	    "request_length %u",
	    __func__, (void *)scu_cmdp, (void *)scu_cmdp->cmd_smp_pkt,
	    (void *)(*request_buffer), request_length);

	return (request_length);
}

/*
 * get the frame type of the smp request
 */
U8
scu_scic_cb_smp_passthru_get_frame_type(void *request)
{
	SCI_IO_REQUEST_HANDLE_T	scif_request;
	scu_cmd_t		*scu_cmdp;
	smp_request_frame_t	*srq;
	U8			frame_type;

	scif_request = sci_object_get_association(request);
	scu_cmdp = sci_object_get_association(scif_request);
	ASSERT(scu_cmdp != NULL);

	srq = (smp_request_frame_t *)scu_cmdp->cmd_smp_pkt->smp_pkt_req;
	frame_type = srq->srf_frame_type;

	SCUDBG(NULL, SCUDBG_SMP, SCUDBG_INFO,
	    "%s: scu_cmd 0x%p smp_pkt 0x%p frame type %u",
	    __func__, (void *)scu_cmdp, (void *)scu_cmdp->cmd_smp_pkt,
	    frame_type);

	return (frame_type);
}

/*
 * get the function of the smp request
 */
U8
scu_scic_cb_smp_passthru_get_function(void *request)
{
	SCI_IO_REQUEST_HANDLE_T	scif_request;
	scu_cmd_t		*scu_cmdp;
	smp_request_frame_t	*srq;
	U8			function;

	scif_request = sci_object_get_association(request);
	scu_cmdp = sci_object_get_association(scif_request);
	ASSERT(scu_cmdp != NULL);

	srq = (smp_request_frame_t *)scu_cmdp->cmd_smp_pkt->smp_pkt_req;
	function = srq->srf_function;

	SCUDBG(NULL, SCUDBG_SMP, SCUDBG_INFO,
	    "%s: scu_cmd 0x%p smp_pkt 0x%p function %u",
	    __func__, (void *)scu_cmdp, (void *)scu_cmdp->cmd_smp_pkt,
	    function);

	return (function);
}

/*
 * get the allocated response length of the smp request
 */
U8
scu_scic_cb_smp_passthru_get_allocated_response_length(void *request)
{
	SCI_IO_REQUEST_HANDLE_T	scif_request;
	scu_cmd_t		*scu_cmdp;
	U8			response_length;

	scif_request = sci_object_get_association(request);
	scu_cmdp = sci_object_get_association(scif_request);
	ASSERT(scu_cmdp != NULL);

	response_length = (U8) (scu_cmdp->cmd_smp_resplen >> 2);

	SCUDBG(NULL, SCUDBG_SMP, SCUDBG_INFO,
	    "%s: scu_cmd 0x%p smp_pkt 0x%p response length %u",
	    __func__, (void *)scu_cmdp, (void *)scu_cmdp->cmd_smp_pkt,
	    response_length);

	return (response_length);
}
