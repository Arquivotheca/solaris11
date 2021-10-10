/*
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 *
 * This file may contain confidential information of Intel Corporation
 * and should not be distributed in source form without approval
 * from Oracle Legal.
 */

#include <sys/scsi/adapters/scu/scu_var.h>

void
scu_smhba_add_hba_prop(scu_ctl_t *scu_ctlp, data_type_t dt,
    char *prop_name, void *prop_val)
{
	ASSERT(scu_ctlp != NULL);

	switch (dt) {
	case DATA_TYPE_INT32:
		if (ddi_prop_update_int(DDI_DEV_T_NONE, scu_ctlp->scu_dip,
		    prop_name, *(int *)prop_val)) {
			SCUDBG(scu_ctlp, SCUDBG_SMHBA, SCUDBG_WARNING,
			    "%s: %s prop update failed", __func__, prop_name);
		}
		break;
	case DATA_TYPE_STRING:
		if (ddi_prop_update_string(DDI_DEV_T_NONE, scu_ctlp->scu_dip,
		    prop_name, (char *)prop_val)) {
			SCUDBG(scu_ctlp, SCUDBG_SMHBA, SCUDBG_WARNING,
			    "%s: %s prop update failed", __func__, prop_name);
		}
		break;
	default:
		SCUDBG(scu_ctlp, SCUDBG_SMHBA, SCUDBG_WARNING,
		    "%s: "
		    "Unhandled datatype(%d) for (%s). Skipping prop update.",
		    __func__, dt, prop_name);
	}
}

/*
 * Called with iport lock held.
 */
void
scu_smhba_add_iport_prop(scu_iport_t *iport, data_type_t dt,
    char *prop_name, void *prop_val)
{
	ASSERT(iport != NULL);
	ASSERT(mutex_owned(&iport->scui_lock));

	switch (dt) {
	case DATA_TYPE_INT32:
		if (ddi_prop_update_int(DDI_DEV_T_NONE, iport->scui_dip,
		    prop_name, *(int *)prop_val)) {
			SCUDBG(iport->scui_ctlp, SCUDBG_SMHBA, SCUDBG_WARNING,
			    "%s: %s prop update failed", __func__, prop_name);
		}
		break;
	case DATA_TYPE_STRING:
		if (ddi_prop_update_string(DDI_DEV_T_NONE, iport->scui_dip,
		    prop_name, (char *)prop_val)) {
			SCUDBG(iport->scui_ctlp, SCUDBG_SMHBA, SCUDBG_WARNING,
			    "%s: %s prop update failed", __func__, prop_name);
		}
		break;
	default:
		SCUDBG(iport->scui_ctlp, SCUDBG_SMHBA, SCUDBG_WARNING,
		    "%s: "
		    "Unhandled datatype(%d) for(%s). Skipping prop update.",
		    __func__, dt, prop_name);
	}

	scu_smhba_set_phy_props(iport);
}

void
scu_smhba_add_tgt_prop(scu_tgt_t *tgt, data_type_t dt,
    char *prop_name, void *prop_val)
{
	ASSERT(tgt != NULL);

	switch (dt) {
	case DATA_TYPE_INT32:
		if (ddi_prop_update_int(DDI_DEV_T_NONE, tgt->scut_dip,
		    prop_name, *(int *)prop_val)) {
			SCUDBG(tgt->scut_ctlp, SCUDBG_SMHBA, SCUDBG_WARNING,
			    "%s: %s prop update failed", __func__, prop_name);
		}
		break;
	case DATA_TYPE_STRING:
		if (ddi_prop_update_string(DDI_DEV_T_NONE, tgt->scut_dip,
		    prop_name, (char *)prop_val)) {
			SCUDBG(tgt->scut_ctlp, SCUDBG_SMHBA, SCUDBG_WARNING,
			    "%s: %s prop update failed", __func__, prop_name);
		}
		break;
	default:
		SCUDBG(tgt->scut_ctlp, SCUDBG_SMHBA, SCUDBG_WARNING, "%s: "
		    "Unhandled datatype(%d) for (%s). Skipping prop update.",
		    __func__, dt, prop_name);
	}
}

/* ARGSUSED */
void
scu_smhba_set_scsi_device_props(scu_ctl_t *scu_ctlp, scu_tgt_t *ptgt,
    struct scsi_device *sd)
{
	char				*paddr, *addr;
	uint64_t			wwn, pwwn;
	SCI_REMOTE_DEVICE_HANDLE_T 	current_device;
	SCI_REMOTE_DEVICE_HANDLE_T 	current_core_device;
	SCI_REMOTE_DEVICE_HANDLE_T 	parent_device;
	SCI_REMOTE_DEVICE_HANDLE_T	parent_core_device;
	SCI_SAS_ADDRESS_T		sas_address;
	SCI_SAS_ADDRESS_T		p_sas_address;
	SCI_STATUS			sci_status;
	SMP_DISCOVER_RESPONSE_PROTOCOLS_T	dev_protocols;

	if (ptgt == NULL || scu_ctlp == NULL)
		return;

	current_device = ptgt->scut_lib_remote_device;

	if (current_device == NULL)
		return;

	/* Get the sas address */
	scic_remote_device_get_sas_address(
	    scif_remote_device_get_scic_handle(current_device),
	    &sas_address);

	ASSERT(sas_address.high != 0 || sas_address.low != 0);

	wwn = SCU_SAS_ADDRESS(BE_32(sas_address.high),
	    BE_32(sas_address.low));
	addr = scsi_wwn_to_wwnstr(wwn, 1, NULL);

	/* Get the parent sas address */
	sci_status =
	    scif_remote_device_get_containing_device(
	    current_device, &parent_device);

	if (sci_status != SCI_SUCCESS) {
		/* directed attached device */
		scic_sds_phy_get_sas_address(
		    ptgt->scut_iportp->scui_primary_phy->scup_lib_phyp,
		    &p_sas_address);
	} else {
		/* expander behind attached device */
		parent_core_device =
		    scif_remote_device_get_scic_handle(parent_device);
		scic_remote_device_get_sas_address(parent_core_device,
		    &p_sas_address);
	}

	ASSERT(p_sas_address.high != 0 || p_sas_address.low != 0);

	pwwn = SCU_SAS_ADDRESS(BE_32(p_sas_address.high),
	    BE_32(p_sas_address.low));
	paddr = scsi_wwn_to_wwnstr(pwwn, 1, NULL);

	/* Get tgt type */
	current_core_device =
	    scif_remote_device_get_scic_handle(current_device);
	scic_remote_device_get_protocols(current_core_device,
	    &dev_protocols);
	if (dev_protocols.u.bits.attached_stp_target ||
	    dev_protocols.u.bits.attached_sata_device) {
		(void) scsi_device_prop_update_string(sd,
		    SCSI_DEVICE_PROP_PATH,
		    SCSI_ADDR_PROP_BRIDGE_PORT, addr);
	}

	(void) scsi_device_prop_update_string(sd,
	    SCSI_DEVICE_PROP_PATH,
	    SCSI_ADDR_PROP_ATTACHED_PORT, paddr);

	scsi_free_wwnstr(addr);
	scsi_free_wwnstr(paddr);
}

void
scu_smhba_set_phy_props(scu_iport_t *iport)
{
	int		i;
	size_t		packed_size;
	char		*packed_data;
	scu_ctl_t	*scu_ctlp = iport->scui_ctlp;
	scu_phy_t	*phy_ptr;
	nvlist_t	**phy_props;
	nvlist_t	*nvl;

	ASSERT(mutex_owned(&iport->scui_lock));
	if (iport->scui_phy_num == 0) {
		return;
	}

	if (nvlist_alloc(&nvl, NV_UNIQUE_NAME, 0) != 0) {
		SCUDBG(scu_ctlp, SCUDBG_SMHBA, SCUDBG_WARNING,
		    "%s: nvlist_alloc() failed", __func__);
	}

	phy_props = kmem_zalloc(sizeof (nvlist_t *) *
	    iport->scui_phy_num, KM_SLEEP);

	for (phy_ptr = list_head(&iport->scui_phys), i = 0;
	    phy_ptr != NULL;
	    phy_ptr = list_next(&iport->scui_phys, phy_ptr), i++) {
		(void) scic_phy_get_properties(phy_ptr->scup_lib_phyp,
		    &phy_ptr->scup_lib_phy_prop);
		(void) nvlist_alloc(&phy_props[i], NV_UNIQUE_NAME, 0);

		(void) nvlist_add_uint8(phy_props[i], SAS_PHY_ID,
		    phy_ptr->scup_hba_index);
		(void) nvlist_add_int8(phy_props[i], SAS_NEG_LINK_RATE,
		    phy_ptr->scup_lib_phy_prop.negotiated_link_rate);
		(void) nvlist_add_int8(phy_props[i], SAS_PROG_MIN_LINK_RATE,
		    SCI_SAS_150_GB);
		(void) nvlist_add_int8(phy_props[i], SAS_HW_MIN_LINK_RATE,
		    SCI_SAS_150_GB);
		(void) nvlist_add_int8(phy_props[i], SAS_PROG_MAX_LINK_RATE,
		    SCI_SAS_600_GB);
		(void) nvlist_add_int8(phy_props[i], SAS_HW_MAX_LINK_RATE,
		    SCI_SAS_600_GB);

	}

	(void) nvlist_add_nvlist_array(nvl, SAS_PHY_INFO_NVL, phy_props,
	    iport->scui_phy_num);

	(void) nvlist_size(nvl, &packed_size, NV_ENCODE_NATIVE);
	packed_data = kmem_zalloc(packed_size, KM_SLEEP);
	(void) nvlist_pack(nvl, &packed_data, &packed_size,
	    NV_ENCODE_NATIVE, 0);

	(void) ddi_prop_update_byte_array(DDI_DEV_T_NONE, iport->scui_dip,
	    SAS_PHY_INFO, (uchar_t *)packed_data, packed_size);

	for (i = 0; i < iport->scui_phy_num && phy_props[i] != NULL; i++) {
		nvlist_free(phy_props[i]);
	}
	nvlist_free(nvl);
	kmem_free(phy_props, sizeof (nvlist_t *) * iport->scui_phy_num);
	kmem_free(packed_data, packed_size);
}

/*
 * Called with PHY lock held on phyp
 */
void
scu_smhba_log_sysevent(scu_ctl_t *scu_ctlp, char *subclass, char *etype,
    scu_phy_t *phyp)
{
	nvlist_t	*attr_list;
	char		*pname;
	char		sas_addr[SCU_MAX_UA_SIZE];
	uint8_t		phynum = 0;
	uint8_t		lrate = 0;
	int		ua_form = 1;

	if (scu_ctlp->scu_dip == NULL)
		return;
	if (phyp == NULL)
		return;

	pname = kmem_zalloc(MAXPATHLEN, KM_SLEEP);
	if (pname == NULL)
		return;

	if ((strcmp(subclass, ESC_SAS_PHY_EVENT) == 0) ||
	    (strcmp(subclass, ESC_SAS_HBA_PORT_BROADCAST) == 0)) {
		ASSERT(phyp != NULL);
		(void) ddi_pathname(scu_ctlp->scu_dip, pname);
		phynum = phyp->scup_hba_index;
		(void) scsi_wwn_to_wwnstr(phyp->scup_sas_address,
		    ua_form, sas_addr);
		if (strcmp(etype, SAS_PHY_ONLINE) == 0) {
			lrate = phyp->scup_lib_phy_prop.negotiated_link_rate;
		}
	}
	if (strcmp(subclass, ESC_SAS_HBA_PORT_BROADCAST) == 0) {
		(void) ddi_pathname(scu_ctlp->scu_dip, pname);
	}

	if (nvlist_alloc(&attr_list, NV_UNIQUE_NAME_TYPE, 0) != 0) {
		SCUDBG(scu_ctlp, SCUDBG_SMHBA, SCUDBG_WARNING,
		    "%s: Failed to post sysevent", __func__);
		kmem_free(pname, MAXPATHLEN);
		return;
	}

	if (nvlist_add_int32(attr_list, SAS_DRV_INST,
	    ddi_get_instance(scu_ctlp->scu_dip)) != 0)
		goto fail;

	if (nvlist_add_string(attr_list, SAS_PORT_ADDR, sas_addr) != 0)
		goto fail;

	if (nvlist_add_string(attr_list, SAS_DEVFS_PATH, pname) != 0)
		goto fail;

	if (nvlist_add_uint8(attr_list, SAS_PHY_ID, phynum) != 0)
		goto fail;

	if (strcmp(etype, SAS_PHY_ONLINE) == 0) {
		if (nvlist_add_uint8(attr_list, SAS_LINK_RATE, lrate) != 0)
			goto fail;
	}

	if (nvlist_add_string(attr_list, SAS_EVENT_TYPE, etype) != 0)
		goto fail;

	(void) ddi_log_sysevent(scu_ctlp->scu_dip, DDI_VENDOR_SUNW,
	    EC_HBA, subclass, attr_list, NULL, DDI_SLEEP);

fail:
	kmem_free(pname, MAXPATHLEN);
	nvlist_free(attr_list);
}

/* Called with iport_lock and phy lock held */
void
scu_smhba_create_one_phy_stats(scu_iport_t *iport, scu_phy_t *phyp)
{
	sas_phy_stats_t		*phy_stats;
	scu_ctl_t		*scu_ctlp;
	int			ndata;
	char			ks_name[KSTAT_STRLEN];

	ASSERT(mutex_owned(&iport->scui_lock));
	scu_ctlp = iport->scui_ctlp;
	ASSERT(scu_ctlp != NULL);
	ASSERT(mutex_owned(&phyp->scup_lock));

	/*
	 * We need to call function scic_phy_enable_counter()
	 * but it is an empty function in current SCIC lib.
	 * so we leave it until it has been finished
	 */
	if (phyp->phy_stats != NULL) {
		/*
		 * Delete existing kstats with name containing
		 * old iport instance# and allow creation of
		 * new kstats with new iport instance# in the name.
		 */
		kstat_delete(phyp->phy_stats);
	}

	ndata = (sizeof (sas_phy_stats_t)/sizeof (kstat_named_t));

	(void) snprintf(ks_name, sizeof (ks_name),
	    "%s.%llx.%d.%d", ddi_driver_name(iport->scui_dip),
	    (longlong_t)scu_ctlp->sas_wwns[0],
	    ddi_get_instance(iport->scui_dip), phyp->scup_hba_index);

	phyp->phy_stats = kstat_create("scu",
	    ddi_get_instance(iport->scui_dip), ks_name, KSTAT_SAS_PHY_CLASS,
	    KSTAT_TYPE_NAMED, ndata, 0);

	if (phyp->phy_stats == NULL) {
		SCUDBG(scu_ctlp, SCUDBG_SMHBA, SCUDBG_WARNING,
		    "%s: Failed to create %s kstats for PHY(0x%p)",
		    __func__, ks_name, (void *)phyp);
		return;
	}

	phy_stats = (sas_phy_stats_t *)phyp->phy_stats->ks_data;

	kstat_named_init(&phy_stats->seconds_since_last_reset,
	    "SecondsSinceLastReset", KSTAT_DATA_ULONGLONG);
	kstat_named_init(&phy_stats->tx_frames,
	    "TxFrames", KSTAT_DATA_ULONGLONG);
	kstat_named_init(&phy_stats->rx_frames,
	    "RxFrames", KSTAT_DATA_ULONGLONG);
	kstat_named_init(&phy_stats->tx_words,
	    "TxWords", KSTAT_DATA_ULONGLONG);
	kstat_named_init(&phy_stats->rx_words,
	    "RxWords", KSTAT_DATA_ULONGLONG);
	kstat_named_init(&phy_stats->invalid_dword_count,
	    "InvalidDwordCount", KSTAT_DATA_ULONGLONG);
	kstat_named_init(&phy_stats->running_disparity_error_count,
	    "RunningDisparityErrorCount", KSTAT_DATA_ULONGLONG);
	kstat_named_init(&phy_stats->loss_of_dword_sync_count,
	    "LossofDwordSyncCount", KSTAT_DATA_ULONGLONG);
	kstat_named_init(&phy_stats->phy_reset_problem_count,
	    "PhyResetProblemCount", KSTAT_DATA_ULONGLONG);

	phyp->phy_stats->ks_private = phyp;
	phyp->phy_stats->ks_update = scu_smhba_update_phy_stats;
	kstat_install(phyp->phy_stats);
}

void
scu_smhba_create_all_phy_stats(scu_iport_t *iport)
{
	scu_phy_t	*scu_phyp;

	ASSERT(iport != NULL);

	mutex_enter(&iport->scui_lock);

	for (scu_phyp = list_head(&iport->scui_phys);
	    scu_phyp != NULL;
	    scu_phyp = list_next(&iport->scui_phys, scu_phyp)) {

		mutex_enter(&scu_phyp->scup_lock);
		scu_smhba_create_one_phy_stats(iport, scu_phyp);
		mutex_exit(&scu_phyp->scup_lock);
	}

	mutex_exit(&iport->scui_lock);
}

int
scu_smhba_update_phy_stats(kstat_t *ks, int rw)
{
	int			ret = DDI_FAILURE;
	scu_phy_t		*pptr = (scu_phy_t *)ks->ks_private;
	scu_ctl_t		*scu_ctlp = pptr->scup_ctlp;

	_NOTE(ARGUNUSED(rw));
	ASSERT((pptr != NULL) && (scu_ctlp != NULL));

	/*
	 * We just want to lock against other invocations of kstat;
	 * we don't need to pmcs_lock_phy() for this.
	 */
	mutex_enter(&pptr->scup_lock);

	/*
	 * We need to call function scic_phy_get_counter()
	 * but it is an empty function in current SCIC lib.
	 * so we leave it until it has been finished
	 */
	/* Get Stats from Chip */
	ret = DDI_SUCCESS;
fail:
	mutex_exit(&pptr->scup_lock);
	return (ret);
}

/*
 * Update a PHY's attached-port-pm and target-port-pm properties
 *
 * phyp: PHY whose properties are to be updated
 *
 */
void
scu_smhba_update_tgt_pm_props(scu_tgt_t *tgtp)
{
	uint32_t			i;
	uint32_t			tgt_width = 1;
	scu_phy_t			*phyp;
	uint64_t			att_port_pm = 0;
	uint64_t			tgt_port_pm = 0;
	SCIF_SAS_REMOTE_DEVICE_T	*smp_device;

	if (tgtp == NULL)
		return;

	ASSERT(mutex_owned(&tgtp->scut_lock));

	phyp = tgtp->scut_phyp;
	if (phyp == NULL)
		return;

	for (i = 0; i < phyp->scup_iportp->scui_phy_num; i++) {
		att_port_pm |=
		    (1ull << (phyp->scup_hba_index + i));
	}

	(void) snprintf(phyp->att_port_pm_str, SCU_PM_MAX_NAMELEN,
	    "%"PRIx64, att_port_pm);

	if (tgtp->scut_dtype == SCU_DTYPE_EXPANDER) {
		smp_device =
		    (SCIF_SAS_REMOTE_DEVICE_T *)tgtp->scut_lib_remote_device;
		if (smp_device != NULL) {
			tgt_width =
			    smp_device->protocol_device.smp_device
			    .number_of_phys;
		}
	}
	for (i = 0; i < tgt_width; i++) {
		tgt_port_pm |= (1ull << (i));
	}

	(void) snprintf(phyp->tgt_port_pm_str, SCU_PM_MAX_NAMELEN,
	    "%"PRIx64, tgt_port_pm);

	if (!list_is_empty(&tgtp->scut_luns)) {
		scu_lun_t *lunp;

		lunp = list_head(&tgtp->scut_luns);
		while (lunp) {
			(void) scsi_device_prop_update_string(lunp->scul_sd,
			    SCSI_DEVICE_PROP_PATH,
			    SCSI_ADDR_PROP_ATTACHED_PORT_PM,
			    phyp->att_port_pm_str);
			(void) scsi_device_prop_update_string(lunp->scul_sd,
			    SCSI_DEVICE_PROP_PATH,
			    SCSI_ADDR_PROP_TARGET_PORT_PM,
			    phyp->tgt_port_pm_str);
			lunp = list_next(&tgtp->scut_luns, lunp);
		}
	} else if (tgtp->scut_smp_sd) {
		(void) smp_device_prop_update_string(tgtp->scut_smp_sd,
		    SCSI_ADDR_PROP_ATTACHED_PORT_PM,
		    phyp->att_port_pm_str);
		(void) smp_device_prop_update_string(tgtp->scut_smp_sd,
		    SCSI_ADDR_PROP_TARGET_PORT_PM,
		    phyp->tgt_port_pm_str);
	}
}
