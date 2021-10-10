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
 * Copyright 2011 Emulex.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Source file containing the implementation of the Hardware specific
 * functions
 */

#include <oce_impl.h>
#include <oce_stat.h>
#include <oce_ioctl.h>

static ddi_device_acc_attr_t reg_accattr = {
	DDI_DEVICE_ATTR_V1,
	DDI_STRUCTURE_LE_ACC,
	DDI_STRICTORDER_ACC,
	DDI_FLAGERR_ACC
};

extern int oce_destroy_q(struct oce_dev *dev, struct oce_mbx *mbx,
    size_t req_size, enum qtype qtype);

static int
oce_map_regs(struct oce_dev *dev)
{
	int ret = 0;
	off_t bar_size = 0;

	ASSERT(NULL != dev);
	ASSERT(NULL != dev->dip);

	/* get number of supported bars */
	ret = ddi_dev_nregs(dev->dip, &dev->num_bars);
	if (ret != DDI_SUCCESS) {
		oce_log(dev, CE_WARN, MOD_CONFIG,
		    "%d: could not retrieve num_bars", MOD_CONFIG);
		return (DDI_FAILURE);
	}

	/* verify each bar and map it accordingly */
	/* PCI CFG */
	ret = ddi_dev_regsize(dev->dip, OCE_DEV_CFG_BAR, &bar_size);
	if (ret != DDI_SUCCESS) {
		oce_log(dev, CE_WARN, MOD_CONFIG,
		    "Could not get sizeof BAR %d",
		    OCE_DEV_CFG_BAR);
		return (DDI_FAILURE);
	}

	ret = ddi_regs_map_setup(dev->dip, OCE_DEV_CFG_BAR, &dev->dev_cfg_addr,
	    0, bar_size, &reg_accattr, &dev->dev_cfg_handle);

	if (ret != DDI_SUCCESS) {
		oce_log(dev, CE_WARN, MOD_CONFIG,
		    "Could not map bar %d",
		    OCE_DEV_CFG_BAR);
		return (DDI_FAILURE);
	}

	/* CSR */
	ret = ddi_dev_regsize(dev->dip, OCE_PCI_CSR_BAR, &bar_size);

	if (ret != DDI_SUCCESS) {
		oce_log(dev, CE_WARN, MOD_CONFIG,
		    "Could not get sizeof BAR %d",
		    OCE_PCI_CSR_BAR);
		return (DDI_FAILURE);
	}

	ret = ddi_regs_map_setup(dev->dip, OCE_PCI_CSR_BAR, &dev->csr_addr,
	    0, bar_size, &reg_accattr, &dev->csr_handle);
	if (ret != DDI_SUCCESS) {
		oce_log(dev, CE_WARN, MOD_CONFIG,
		    "Could not map bar %d",
		    OCE_PCI_CSR_BAR);
		ddi_regs_map_free(&dev->dev_cfg_handle);
		return (DDI_FAILURE);
	}

	/* Doorbells */
	ret = ddi_dev_regsize(dev->dip, OCE_PCI_DB_BAR, &bar_size);
	if (ret != DDI_SUCCESS) {
		oce_log(dev, CE_WARN, MOD_CONFIG,
		    "%d Could not get sizeof BAR %d",
		    ret, OCE_PCI_DB_BAR);
		ddi_regs_map_free(&dev->csr_handle);
		ddi_regs_map_free(&dev->dev_cfg_handle);
		return (DDI_FAILURE);
	}

	ret = ddi_regs_map_setup(dev->dip, OCE_PCI_DB_BAR, &dev->db_addr,
	    0, 0, &reg_accattr, &dev->db_handle);
	if (ret != DDI_SUCCESS) {
		oce_log(dev, CE_WARN, MOD_CONFIG,
		    "Could not map bar %d", OCE_PCI_DB_BAR);
		ddi_regs_map_free(&dev->csr_handle);
		ddi_regs_map_free(&dev->dev_cfg_handle);
		return (DDI_FAILURE);
	}
	return (DDI_SUCCESS);
}
static void
oce_unmap_regs(struct oce_dev *dev)
{

	ASSERT(NULL != dev);
	ASSERT(NULL != dev->dip);

	ddi_regs_map_free(&dev->db_handle);
	ddi_regs_map_free(&dev->csr_handle);
	ddi_regs_map_free(&dev->dev_cfg_handle);

}





/*
 * function to map the device memory
 *
 * dev - handle to device private data structure
 *
 */
int
oce_pci_init(struct oce_dev *dev)
{
	int ret = 0;

	ret = oce_map_regs(dev);

	if (ret != DDI_SUCCESS) {
		return (DDI_FAILURE);
	}
	dev->fn =  OCE_PCI_FUNC(dev);
	if (oce_fm_check_acc_handle(dev, dev->dev_cfg_handle) != DDI_FM_OK) {
		ddi_fm_service_impact(dev->dip, DDI_SERVICE_DEGRADED);
	}

	if (ret != DDI_FM_OK) {
		oce_pci_fini(dev);
		return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);
} /* oce_pci_init */

/*
 * function to free device memory mapping mapped using
 * oce_pci_init
 *
 * dev - handle to device private data
 */
void
oce_pci_fini(struct oce_dev *dev)
{
	oce_unmap_regs(dev);
} /* oce_pci_fini */

/*
 * function to read the PCI Bus, Device, and function numbers for the
 * device instance.
 *
 * dev - handle to device private data
 */
int
oce_get_bdf(struct oce_dev *dev)
{
	pci_regspec_t *pci_rp;
	uint32_t length;
	int rc;

	/* Get "reg" property */
	rc = ddi_prop_lookup_int_array(DDI_DEV_T_ANY, dev->dip,
	    0, "reg", (int **)&pci_rp, (uint_t *)&length);

	if ((rc != DDI_SUCCESS) ||
	    (length < (sizeof (pci_regspec_t) / sizeof (int)))) {
		oce_log(dev, CE_WARN, MOD_CONFIG,
		    "Failed to read \"reg\" property, Status = 0x%x", rc);
		return (rc);
	}

	dev->pci_bus = PCI_REG_BUS_G(pci_rp->pci_phys_hi);
	dev->pci_device = PCI_REG_DEV_G(pci_rp->pci_phys_hi);
	dev->pci_function = PCI_REG_FUNC_G(pci_rp->pci_phys_hi);

	oce_log(dev, CE_NOTE, MOD_CONFIG,
	    "\"reg\" property num=%d, Bus=%d, Device=%d, Function=%d",
	    length, dev->pci_bus, dev->pci_device, dev->pci_function);

	/* Free the memory allocated by ddi_prop_lookup_int_array() */
	ddi_prop_free(pci_rp);
	return (rc);
}

int
oce_identify_hw(struct oce_dev *dev)
{
	int ret = DDI_SUCCESS;

	dev->vendor_id = pci_config_get16(dev->pci_cfg_handle,
	    PCI_CONF_VENID);
	dev->device_id = pci_config_get16(dev->pci_cfg_handle,
	    PCI_CONF_DEVID);
	dev->subsys_id = pci_config_get16(dev->pci_cfg_handle,
	    PCI_CONF_SUBSYSID);
	dev->subvendor_id = pci_config_get16(dev->pci_cfg_handle,
	    PCI_CONF_SUBVENID);

	switch (dev->device_id) {

	case DEVID_TIGERSHARK:
		dev->chip_rev = OC_CNA_GEN2;
		break;
	case DEVID_TOMCAT:
		dev->chip_rev = OC_CNA_GEN3;
		break;
	default:
		dev->chip_rev = 0;
		ret = DDI_FAILURE;
		break;
	}
	return (ret);
}


/*
 * function to check if a reset is required
 *
 * dev - software handle to the device
 *
 */
boolean_t
oce_is_reset_pci(struct oce_dev *dev)
{
	mpu_ep_semaphore_t post_status;

	ASSERT(dev != NULL);
	ASSERT(dev->dip != NULL);

	post_status.dw0 = 0;
	post_status.dw0 = OCE_CSR_READ32(dev, MPU_EP_SEMAPHORE);

	if (post_status.bits.stage == POST_STAGE_ARMFW_READY) {
		return (B_FALSE);
	}
	return (B_TRUE);
} /* oce_is_reset_pci */

/*
 * function to do a soft reset on the device
 *
 * dev - software handle to the device
 *
 */
int
oce_pci_soft_reset(struct oce_dev *dev)
{
	pcicfg_soft_reset_t soft_rst;
	/* struct mpu_ep_control ep_control; */
	/* struct pcicfg_online1 online1; */
	clock_t tmo;
	clock_t earlier = ddi_get_lbolt();

	ASSERT(dev != NULL);

	/* issue soft reset */
	soft_rst.dw0 = OCE_CFG_READ32(dev, PCICFG_SOFT_RESET);
	soft_rst.bits.soft_reset = 0x01;
	OCE_CFG_WRITE32(dev, PCICFG_SOFT_RESET, soft_rst.dw0);

	/* wait till soft reset bit deasserts */
	tmo = drv_usectohz(60000000); /* 1.0min */
	do {
		if ((ddi_get_lbolt() - earlier) > tmo) {
			tmo = 0;
			break;
		}

		soft_rst.dw0 = OCE_CFG_READ32(dev, PCICFG_SOFT_RESET);
		if (soft_rst.bits.soft_reset)
			drv_usecwait(100);
	} while (soft_rst.bits.soft_reset);

	if (soft_rst.bits.soft_reset) {
		oce_log(dev, CE_WARN, MOD_CONFIG,
		    "0x%x soft_reset"
		    "bit asserted[1]. Reset failed",
		    soft_rst.dw0);
		return (DDI_FAILURE);
	}

	return (oce_POST(dev));
} /* oce_pci_soft_reset */
/*
 * function to trigger a POST on the device
 *
 * dev - software handle to the device
 *
 */
int
oce_POST(struct oce_dev *dev)
{
	mpu_ep_semaphore_t post_status;
	clock_t tmo;
	clock_t earlier = ddi_get_lbolt();

	/* read semaphore CSR */
	post_status.dw0 = OCE_CSR_READ32(dev, MPU_EP_SEMAPHORE);
	if (oce_fm_check_acc_handle(dev, dev->csr_handle) != DDI_FM_OK) {
		ddi_fm_service_impact(dev->dip, DDI_SERVICE_DEGRADED);
		return (DDI_FAILURE);
	}
	/* if host is ready then wait for fw ready else send POST */
	if (post_status.bits.stage <= POST_STAGE_AWAITING_HOST_RDY) {
		post_status.bits.stage = POST_STAGE_CHIP_RESET;
		OCE_CSR_WRITE32(dev, MPU_EP_SEMAPHORE, post_status.dw0);
		if (oce_fm_check_acc_handle(dev, dev->csr_handle) !=
		    DDI_FM_OK) {
			ddi_fm_service_impact(dev->dip, DDI_SERVICE_DEGRADED);
			return (DDI_FAILURE);
		}
	}

	/* wait for FW ready */
	tmo = drv_usectohz(60000000); /* 1.0min */
	for (;;) {
		if ((ddi_get_lbolt() - earlier) > tmo) {
			tmo = 0;
			break;
		}

		post_status.dw0 = OCE_CSR_READ32(dev, MPU_EP_SEMAPHORE);
		if (oce_fm_check_acc_handle(dev, dev->csr_handle) !=
		    DDI_FM_OK) {
			ddi_fm_service_impact(dev->dip, DDI_SERVICE_DEGRADED);
			return (DDI_FAILURE);
		}
		if (post_status.bits.error) {
			oce_log(dev, CE_WARN, MOD_CONFIG,
			    "0x%x POST ERROR!!", post_status.dw0);
			return (DDI_FAILURE);
		}
		if (post_status.bits.stage == POST_STAGE_ARMFW_READY)
			return (DDI_SUCCESS);

		drv_usecwait(100);
	}
	return (DDI_FAILURE);
} /* oce_POST */
/*
 * function to modify register access attributes corresponding to the
 * FM capabilities configured by the user
 *
 * fm_caps - fm capability configured by the user and accepted by the driver
 */
void
oce_set_reg_fma_flags(int fm_caps)
{
	if (fm_caps == DDI_FM_NOT_CAPABLE) {
		return;
	}
	if (DDI_FM_ACC_ERR_CAP(fm_caps)) {
		reg_accattr.devacc_attr_access = DDI_FLAGERR_ACC;
	} else {
		reg_accattr.devacc_attr_access = DDI_DEFAULT_ACC;
	}
} /* oce_set_fma_flags */


int
oce_create_nw_interface(struct oce_dev *dev)
{
	int ret;
	uint32_t capab_flags = OCE_CAPAB_FLAGS;
	uint32_t capab_en_flags = OCE_CAPAB_ENABLE;

	if (dev->rss_enable) {
		capab_flags |= MBX_RX_IFACE_FLAGS_RSS;
		capab_en_flags |= MBX_RX_IFACE_FLAGS_RSS;
	}

	/* create an interface for the device with out mac */
	ret = oce_if_create(dev, capab_flags, capab_en_flags,
	    0, &dev->mac_addr[0], (uint32_t *)&dev->if_id);
	if (ret != 0) {
		oce_log(dev, CE_WARN, MOD_CONFIG,
		    "Interface creation failed: 0x%x", ret);
		return (ret);
	}
	atomic_inc_32(&dev->nifs);

	dev->if_cap_flags = capab_en_flags;

	/* Enable VLAN Promisc on HW */
	ret = oce_config_vlan(dev, (uint8_t)dev->if_id, NULL, 0,
	    B_TRUE, B_TRUE);
	if (ret != 0) {
		oce_log(dev, CE_WARN, MOD_CONFIG,
		    "Config vlan failed: %d", ret);
		oce_delete_nw_interface(dev);
		return (ret);

	}

	/* set default flow control */
	ret = oce_set_flow_control(dev, dev->flow_control);
	if (ret != 0) {
		oce_log(dev, CE_NOTE, MOD_CONFIG,
		    "Set flow control failed: %d", ret);
	}
	ret = oce_set_promiscuous(dev, dev->promisc);

	if (ret != 0) {
		oce_log(dev, CE_NOTE, MOD_CONFIG,
		    "Set Promisc failed: %d", ret);
	}

	return (0);
}

void
oce_delete_nw_interface(struct oce_dev *dev) {

	/* currently only single interface is implmeneted */
	if (dev->nifs > 0) {
		(void) oce_if_del(dev, dev->if_id);
		atomic_dec_32(&dev->nifs);
	}
}

static void
oce_create_itbl(struct oce_dev *dev, char *itbl)
{
	int i;
	struct oce_rq **rss_queuep = &dev->rq[1];
	int nrss  = dev->nrqs - 1;
	/* fill the indirection table rq 0 is default queue */
	for (i = 0; i < OCE_ITBL_SIZE; i++) {
		itbl[i] = rss_queuep[i % nrss]->rss_cpuid;
	}
}

int
oce_setup_adapter(struct oce_dev *dev)
{
	int ret;
	char itbl[OCE_ITBL_SIZE];
	char hkey[OCE_HKEY_SIZE];

	/* disable the interrupts here and enable in start */
	oce_chip_di(dev);

	ret = oce_create_nw_interface(dev);
	if (ret != DDI_SUCCESS) {
		return (DDI_FAILURE);
	}
	ret = oce_create_queues(dev);
	if (ret != DDI_SUCCESS) {
		oce_delete_nw_interface(dev);
		return (DDI_FAILURE);
	}
	if (dev->rss_enable) {
		(void) oce_create_itbl(dev, itbl);
		(void) oce_gen_hkey(hkey, OCE_HKEY_SIZE);
		ret = oce_config_rss(dev, dev->if_id, hkey, itbl, OCE_ITBL_SIZE,
		    OCE_DEFAULT_RSS_TYPE, B_TRUE);
		if (ret != DDI_SUCCESS) {
			oce_log(dev, CE_NOTE, MOD_CONFIG, "%s",
			    "Failed to Configure RSS");
			oce_delete_queues(dev);
			oce_delete_nw_interface(dev);
			return (ret);
		}
	}
	ret = oce_setup_handlers(dev);
	if (ret != DDI_SUCCESS) {
		oce_log(dev, CE_NOTE, MOD_CONFIG, "%s",
		    "Failed to Setup handlers");
		oce_delete_queues(dev);
		oce_delete_nw_interface(dev);
		return (ret);
	}
	return (DDI_SUCCESS);
}

void
oce_unsetup_adapter(struct oce_dev *dev)
{
	oce_remove_handler(dev);
	if (dev->rss_enable) {
		char itbl[OCE_ITBL_SIZE] = {0};
		char hkey[OCE_HKEY_SIZE] = {0};
		int ret = 0;

		ret = oce_config_rss(dev, dev->if_id, hkey, itbl, OCE_ITBL_SIZE,
		    RSS_ENABLE_NONE, B_TRUE);

		if (ret != DDI_SUCCESS) {
			oce_log(dev, CE_NOTE, MOD_CONFIG, "%s",
			    "Failed to Disable RSS");
		}
	}
	oce_delete_queues(dev);
	oce_delete_nw_interface(dev);
}

int
oce_hw_init(struct oce_dev *dev)
{
	int  ret;
	struct mac_address_format mac_addr;

	ret = oce_POST(dev);
	if (ret != DDI_SUCCESS) {
		oce_log(dev, CE_WARN, MOD_CONFIG, "%s",
		    "!!!HW POST1 FAILED");
		/* ADD FM FAULT */
		return (DDI_FAILURE);
	}
	/* create bootstrap mailbox */
	dev->bmbx = oce_alloc_dma_buffer(dev,
	    sizeof (struct oce_bmbx), NULL, DDI_DMA_CONSISTENT);
	if (dev->bmbx == NULL) {
		oce_log(dev, CE_WARN, MOD_CONFIG,
		    "Failed to allocate bmbx: size = %u",
		    (uint32_t)sizeof (struct oce_bmbx));
		return (DDI_FAILURE);
	}

	ret = oce_reset_fun(dev);
	if (ret != 0) {
		oce_log(dev, CE_WARN, MOD_CONFIG, "%s",
		    "!!!FUNCTION RESET FAILED");
		goto init_fail;
	}

	/* reset the Endianess of BMBX */
	ret = oce_mbox_init(dev);
	if (ret != 0) {
		oce_log(dev, CE_WARN, MOD_CONFIG,
		    "Mailbox initialization2 Failed with %d", ret);
		goto init_fail;
	}

	/* read the firmware version */
	ret = oce_get_fw_version(dev);
	if (ret != 0) {
		oce_log(dev, CE_WARN, MOD_CONFIG,
		    "Firmaware version read failed with %d", ret);
		goto init_fail;
	}

	/* read the fw config */
	ret = oce_get_fw_config(dev);
	if (ret != 0) {
		oce_log(dev, CE_WARN, MOD_CONFIG,
		    "Firmware configuration read failed with %d", ret);
		goto init_fail;
	}

	/* read the Factory MAC address */
	ret = oce_read_mac_addr(dev, 0, 1,
	    MAC_ADDRESS_TYPE_NETWORK, &mac_addr);
	if (ret != 0) {
		oce_log(dev, CE_WARN, MOD_CONFIG,
		    "MAC address read failed with %d", ret);
		goto init_fail;
	}
	bcopy(&mac_addr.mac_addr[0], &dev->mac_addr[0], ETHERADDRL);

	/* cache the ue mask registers for ue detection */
	dev->ue_mask_lo = OCE_CFG_READ32(dev, PCICFG_UE_STATUS_LO_MASK);
	dev->ue_mask_hi = OCE_CFG_READ32(dev, PCICFG_UE_STATUS_HI_MASK);

	return (DDI_SUCCESS);
init_fail:
	oce_hw_fini(dev);
	return (DDI_FAILURE);
}
void
oce_hw_fini(struct oce_dev *dev)
{
	if (dev->bmbx != NULL) {
		oce_free_dma_buffer(dev, dev->bmbx);
		dev->bmbx = NULL;
	}
}

boolean_t
oce_check_ue(struct oce_dev *dev)
{
	uint32_t ue_lo;
	uint32_t ue_hi;

	/* check for  the Hardware unexpected error */
	ue_lo = OCE_CFG_READ32(dev, PCICFG_UE_STATUS_LO);
	ue_hi = OCE_CFG_READ32(dev, PCICFG_UE_STATUS_HI);

	if ((~dev->ue_mask_lo & ue_lo) ||
	    (~dev->ue_mask_hi & ue_hi)) {
		/* Unrecoverable error detected */
		oce_log(dev, CE_WARN, MOD_CONFIG,
		    "Hardware UE Detected: "
		    "UE_LOW:%08x"
		    "UE_HI:%08x "
		    "UE_MASK_LO:%08x "
		    "UE_MASK_HI:%08x",
		    ue_lo, ue_hi,
		    dev->ue_mask_lo,
		    dev->ue_mask_hi);
		return (B_TRUE);
	}
	return (B_FALSE);
}
