
/*
 * The information contained in this file is confidential and proprietary to
 * Broadcom Corporation.  No part of this file may be reproduced or
 * distributed, in any form or by any means for any purpose, without the
 * express written permission of Broadcom Corporation.
 *
 * (c) COPYRIGHT 2008-2011 Broadcom Corporation, ALL RIGHTS RESERVED.
 */

/*
 * Copyright (c) 2002, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include "bnxe.h"

#define BNXE_PRODUCT_BANNER "Broadcom NetXtreme II 10 Gigabit Ethernet Driver v" BRCMVERSION
#define BNXE_PRODUCT_INFO   "Broadcom NXII 10 GbE v" BRCMVERSION

#define BNXE_REGISTER_BAR_NUM      1
#define BNXE_REGS_MAP_OFFSET       0
#define BNXE_L2_MEMORY_WINDOW_SIZE 0x40000 /* 256K for PCI Config Registers */

//#define BNXE_FORCE_FCOE_LOAD

u32_t    dbg_code_path   = CP_ALL;
u8_t     dbg_trace_level = LV_VERBOSE;
u32_t    g_dbg_flags     = 0;

kmutex_t bnxeLoaderMutex;
u32_t    bnxeNumPlumbed;

extern ddi_dma_attr_t bnxeDmaPageAttrib;
extern ddi_dma_attr_t bnxeRxDmaAttrib;
extern ddi_dma_attr_t bnxeTxDmaAttrib;
extern ddi_dma_attr_t bnxeTxCbDmaAttrib;


/* pass in pointer to either lm_device_t or um_device_t */
char * BnxeDevName(void * pDev)
{
    um_device_t * pUM = (um_device_t *)pDev;
    return ((pUM == NULL) || (*pUM->devName == 0)) ? "(bnxe)" : pUM->devName;
}


char * BnxeChipName(um_device_t * pUM)
{
    switch (CHIP_NUM(&pUM->lm_dev) >> 16)
    {
    case 0x164e: return "BCM57710";
    case 0x164f: return "BCM57711";
    case 0x1650: return "BCM57711E";
    case 0x1662: return "BCM57712";
    case 0x1663: return "BCM57712E";
    case 0x1651: return "BCM57713";
    case 0x1652: return "BCM57713E";
    default:     return "UNKNOWN";
    }
}


boolean_t BnxeProtoSupport(um_device_t * pUM, int proto)
{
    boolean_t do_eth  = B_TRUE;
    boolean_t do_fcoe = B_TRUE;

    if (IS_MULTI_VNIC(&pUM->lm_dev))
    {
        do_eth  = B_FALSE;
        do_fcoe = B_FALSE;

        if (pUM->lm_dev.hw_info.mcp_detected == 1)
        {
            if (pUM->lm_dev.params.mf_proto_support_flags &
                LM_PROTO_SUPPORT_ETHERNET)
            {
                do_eth = B_TRUE;
            }

            if (pUM->lm_dev.params.mf_proto_support_flags &
                LM_PROTO_SUPPORT_FCOE)
            {
                do_fcoe = B_TRUE;
            }
        }
        else
        {
            /* mcp is not present so allow enumeration */
            do_eth  = B_TRUE;
            do_fcoe = B_TRUE;
        }
    }

    if (pUM->lm_dev.params.max_func_fcoe_cons == 0)
    {
        do_fcoe = B_FALSE;
    }

    return (((proto == LM_PROTO_SUPPORT_ETHERNET) && do_eth) ||
            ((proto == LM_PROTO_SUPPORT_FCOE) && do_fcoe));
}


static boolean_t BnxePciInit(um_device_t * pUM)
{
    /* setup resources needed for accessing the PCI configuration space */
    if (pci_config_setup(pUM->pDev, &pUM->pPciCfg) != DDI_SUCCESS)
    {
        BnxeLogWarn(pUM, "Failed to setup PCI config");
        return B_FALSE;
    }

    return B_TRUE;
}


static void BnxePciDestroy(um_device_t * pUM)
{
    if (pUM->pPciCfg)
    {
        pci_config_teardown(&pUM->pPciCfg);
        pUM->pPciCfg = NULL;
    }
}


static void BnxeBarMemDestroy(um_device_t * pUM)
{
    BnxeMemRegion * pMemRegion;

    /* free the BAR mappings */
    while (!d_list_is_empty(&pUM->memRegionList))
    {
        pMemRegion = (BnxeMemRegion *)d_list_peek_head(&pUM->memRegionList);
        mm_unmap_io_space(&pUM->lm_dev,
                          pMemRegion->pRegAddr,
                          pMemRegion->size);
    }
}


static void BnxeMutexInit(um_device_t * pUM)
{
    lm_device_t * pLM = &pUM->lm_dev;
    int idx;

    for (idx = 0; idx < (MAX_RSS_CHAINS + 1); idx++)
    {
        mutex_init(&pUM->intrMutex[idx], NULL,
                   MUTEX_DRIVER, DDI_INTR_PRI(pUM->intrPriority));
        mutex_init(&pUM->intrFlipMutex[idx], NULL,
                   MUTEX_DRIVER, DDI_INTR_PRI(pUM->intrPriority));
        mutex_init(&pUM->sbMutex[idx], NULL,
                   MUTEX_DRIVER, DDI_INTR_PRI(pUM->intrPriority));
    }

    for (idx = 0; idx < MAX_ETH_CONS; idx++)
    {
        mutex_init(&pUM->txq[idx].txMutex, NULL,
                   MUTEX_DRIVER, DDI_INTR_PRI(pUM->intrPriority));
        mutex_init(&pUM->txq[idx].freeTxDescMutex, NULL,
                   MUTEX_DRIVER, DDI_INTR_PRI(pUM->intrPriority));
        pUM->txq[idx].pUM = pUM;
        pUM->txq[idx].idx = idx;
    }

    for (idx = 0; idx < MAX_ETH_CONS; idx++)
    {
        mutex_init(&pUM->rxq[idx].rxMutex, NULL,
                   MUTEX_DRIVER, DDI_INTR_PRI(pUM->intrPriority));
        mutex_init(&pUM->rxq[idx].doneRxMutex, NULL,
                   MUTEX_DRIVER, DDI_INTR_PRI(pUM->intrPriority));
        pUM->rxq[idx].pUM = pUM;
        pUM->rxq[idx].idx = idx;
    }

    for (idx = 0; idx < USER_OPTION_RX_RING_GROUPS_MAX; idx++)
    {
        pUM->rxqGroup[idx].pUM = pUM;
        pUM->rxqGroup[idx].idx = idx;
    }

    mutex_init(&pUM->mcpMutex, NULL,
               MUTEX_DRIVER, DDI_INTR_PRI(pUM->intrPriority));
    mutex_init(&pUM->phyMutex, NULL,
               MUTEX_DRIVER, DDI_INTR_PRI(pUM->intrPriority));
    mutex_init(&pUM->dmaeStatsMutex, NULL,
               MUTEX_DRIVER, DDI_INTR_PRI(pUM->intrPriority));
    mutex_init(&pUM->dmaeMiscMutex, NULL,
               MUTEX_DRIVER, DDI_INTR_PRI(pUM->intrPriority));
    mutex_init(&pUM->indMutex, NULL,
               MUTEX_DRIVER, DDI_INTR_PRI(pUM->intrPriority));
    mutex_init(&pUM->cidMutex, NULL,
               MUTEX_DRIVER, DDI_INTR_PRI(pUM->intrPriority));
    mutex_init(&pUM->spqMutex, NULL,
               MUTEX_DRIVER, DDI_INTR_PRI(pUM->intrPriority));
    mutex_init(&pUM->spReqMutex, NULL,
               MUTEX_DRIVER, DDI_INTR_PRI(pUM->intrPriority));
    mutex_init(&pUM->rrReqMutex, NULL,
               MUTEX_DRIVER, DDI_INTR_PRI(pUM->intrPriority));
    mutex_init(&pUM->islesCtrlMutex, NULL,
               MUTEX_DRIVER, DDI_INTR_PRI(pUM->intrPriority));
    mutex_init(&pUM->toeMutex, NULL,
               MUTEX_DRIVER, DDI_INTR_PRI(pUM->intrPriority));
    mutex_init(&pUM->memMutex, NULL,
               MUTEX_DRIVER, DDI_INTR_PRI(pUM->intrPriority));
    mutex_init(&pUM->offloadMutex, NULL,
               MUTEX_DRIVER, DDI_INTR_PRI(pUM->intrPriority));
    mutex_init(&pUM->hwInitMutex, NULL,
               MUTEX_DRIVER, DDI_INTR_PRI(pUM->intrPriority));
    mutex_init(&pUM->gldMutex, NULL,
               MUTEX_DRIVER, DDI_INTR_PRI(pUM->intrPriority));
    rw_init(&pUM->gldTxMutex, NULL, RW_DRIVER, NULL);
    mutex_init(&pUM->timerMutex, NULL,
               MUTEX_DRIVER, DDI_INTR_PRI(pUM->intrPriority));
    mutex_init(&pUM->kstatMutex, NULL,
               MUTEX_DRIVER, DDI_INTR_PRI(pUM->intrPriority));
}


static void BnxeMutexDestroy(um_device_t * pUM)
{
    lm_device_t * pLM = &pUM->lm_dev;
    int idx;

    for (idx = 0; idx < (MAX_RSS_CHAINS + 1); idx++)
    {
        mutex_destroy(&pUM->intrMutex[idx]);
        mutex_destroy(&pUM->intrFlipMutex[idx]);
        mutex_destroy(&pUM->sbMutex[idx]);
    }

    for (idx = 0; idx < MAX_ETH_CONS; idx++)
    {
        mutex_destroy(&pUM->txq[idx].txMutex);
        mutex_destroy(&pUM->txq[idx].freeTxDescMutex);
    }

    for (idx = 0; idx < MAX_ETH_CONS; idx++)
    {
        mutex_destroy(&pUM->rxq[idx].rxMutex);
        mutex_destroy(&pUM->rxq[idx].doneRxMutex);
    }

    for (idx = 0; idx < USER_OPTION_RX_RING_GROUPS_MAX; idx++)
    {
        pUM->rxqGroup[idx].pUM = pUM;
        pUM->rxqGroup[idx].idx = idx;
    }

    mutex_destroy(&pUM->mcpMutex);
    mutex_destroy(&pUM->phyMutex);
    mutex_destroy(&pUM->dmaeStatsMutex);
    mutex_destroy(&pUM->dmaeMiscMutex);
    mutex_destroy(&pUM->indMutex);
    mutex_destroy(&pUM->cidMutex);
    mutex_destroy(&pUM->spqMutex);
    mutex_destroy(&pUM->spReqMutex);
    mutex_destroy(&pUM->rrReqMutex);
    mutex_destroy(&pUM->islesCtrlMutex);
    mutex_destroy(&pUM->toeMutex);
    mutex_destroy(&pUM->memMutex);   /* not until all mem deleted */
    mutex_destroy(&pUM->offloadMutex);
    mutex_destroy(&pUM->hwInitMutex);
    mutex_destroy(&pUM->gldMutex);
    rw_destroy(&pUM->gldTxMutex);
    mutex_destroy(&pUM->timerMutex);
    mutex_destroy(&pUM->kstatMutex);
}


/* FMA support */

int BnxeCheckAccHandle(ddi_acc_handle_t handle)
{
    ddi_fm_error_t de;

    ddi_fm_acc_err_get(handle, &de, DDI_FME_VERSION);
    ddi_fm_acc_err_clear(handle, DDI_FME_VERSION);

    return (de.fme_status);
}


int BnxeCheckDmaHandle(ddi_dma_handle_t handle)
{
    ddi_fm_error_t de;

    ddi_fm_dma_err_get(handle, &de, DDI_FME_VERSION);

    return (de.fme_status);
}


/* The IO fault service error handling callback function */
static int BnxeFmErrorCb(dev_info_t *     pDev,
                         ddi_fm_error_t * err,
                         const void *     impl_data)
{
    /*
     * As the driver can always deal with an error in any dma or
     * access handle, we can just return the fme_status value.
     */
    pci_ereport_post(pDev, err, NULL);

    return (err->fme_status);
}


static void BnxeFmInit(um_device_t * pUM)
{
    ddi_iblock_cookie_t iblk;
    int fma_acc_flag;
    int fma_dma_flag;

    /* Only register with IO Fault Services if we have some capability */
    if (pUM->fmCapabilities & DDI_FM_ACCCHK_CAPABLE)
    {
        bnxeAccessAttribBAR.devacc_attr_version = DDI_DEVICE_ATTR_V1;
        bnxeAccessAttribBAR.devacc_attr_access = DDI_FLAGERR_ACC;
    }

    if (pUM->fmCapabilities & DDI_FM_DMACHK_CAPABLE)
    {
        bnxeDmaPageAttrib.dma_attr_flags = DDI_DMA_FLAGERR;
        bnxeRxDmaAttrib.dma_attr_flags   = DDI_DMA_FLAGERR;
        bnxeTxDmaAttrib.dma_attr_flags   = DDI_DMA_FLAGERR;
        bnxeTxCbDmaAttrib.dma_attr_flags = DDI_DMA_FLAGERR;
    }

    if (pUM->fmCapabilities) 
    {
        /* Register capabilities with IO Fault Services */
        ddi_fm_init(pUM->pDev, &pUM->fmCapabilities, &iblk);

        /* Initialize pci ereport capabilities if ereport capable */
        if (DDI_FM_EREPORT_CAP(pUM->fmCapabilities) ||
            DDI_FM_ERRCB_CAP(pUM->fmCapabilities))
        {
            pci_ereport_setup(pUM->pDev);
        }

        /* Register error callback if error callback capable */
        if (DDI_FM_ERRCB_CAP(pUM->fmCapabilities))
        {
            ddi_fm_handler_register(pUM->pDev, BnxeFmErrorCb, (void *)pUM);
        }
    }
}


static void BnxeFmFini(um_device_t * pUM)
{
    /* Only unregister FMA capabilities if we registered some */
    if (pUM->fmCapabilities) 
    {
        /* Release any resources allocated by pci_ereport_setup() */
        if (DDI_FM_EREPORT_CAP(pUM->fmCapabilities) ||
            DDI_FM_ERRCB_CAP(pUM->fmCapabilities))
        {
            pci_ereport_teardown(pUM->pDev);
        }

        /* Un-register error callback if error callback capable */
        if (DDI_FM_ERRCB_CAP(pUM->fmCapabilities))
        {
            ddi_fm_handler_unregister(pUM->pDev);
        }

        /* Unregister from IO Fault Services */
        ddi_fm_fini(pUM->pDev);
    }
}


void BnxeFmErrorReport(um_device_t * pUM,
                       char *        detail)
{
    uint64_t ena;
    char buf[FM_MAX_CLASS];

    (void) snprintf(buf, FM_MAX_CLASS, "%s.%s", DDI_FM_DEVICE, detail);

    ena = fm_ena_generate(0, FM_ENA_FMT1);

    if (DDI_FM_EREPORT_CAP(pUM->fmCapabilities))
    {
        ddi_fm_ereport_post(pUM->pDev, buf, ena, DDI_NOSLEEP,
                            FM_VERSION, DATA_TYPE_UINT8,
                            FM_EREPORT_VERS0, NULL);
    }
}


static void BnxeInitCamTableOffset(um_device_t * pUM)
{
    u8_t first_client_found  = B_FALSE;
    u8_t last_client_handled = 0;
    u8_t idx;

    for (idx = 0; idx < ARRSIZE(pUM->mcastTableBaseIdx); idx++)
    {
        if (LM_MC_TABLE_SIZE(&pUM->lm_dev, idx) > 0)
        {
            if (first_client_found == B_FALSE)
            {
                first_client_found          = B_TRUE;
                last_client_handled         = idx;
                pUM->mcastTableBaseIdx[idx] = 0;
            }
            else
            {
                pUM->mcastTableBaseIdx[idx] =
                    (pUM->mcastTableBaseIdx[last_client_handled] +
                     LM_MC_TABLE_SIZE(&pUM->lm_dev, last_client_handled));
                last_client_handled = idx;
            }
        }
        else
        {
            pUM->mcastTableBaseIdx[idx] = LM_INVALID_CAM_BASE_IDX;
        }
    }
}


static boolean_t BnxeAttachDevice(um_device_t * pUM)
{
    int rc;

    /* fm-capable in bnxe.conf can be used to set fmCapabilities. */
    pUM->fmCapabilities = ddi_prop_get_int(DDI_DEV_T_ANY,
                                           pUM->pDev,
                                           DDI_PROP_DONTPASS,
                                           "fm-capable",
                                           (DDI_FM_EREPORT_CAPABLE |
                                            DDI_FM_ACCCHK_CAPABLE  |
                                            DDI_FM_DMACHK_CAPABLE  |
                                            DDI_FM_ERRCB_CAPABLE));

    /* Register capabilities with IO Fault Services. */
    BnxeFmInit(pUM);

    if (!BnxePciInit(pUM))
    {
        BnxeFmFini(pUM);

        return B_FALSE;
    }

    BnxeMutexInit(pUM);

    rc = lm_get_dev_info(&pUM->lm_dev);

    if (pUM->fmCapabilities &&
        BnxeCheckAccHandle(pUM->pPciCfg) != DDI_FM_OK)
    {
        ddi_fm_service_impact(pUM->pDev, DDI_SERVICE_LOST);
        BnxeMutexDestroy(pUM);
        BnxePciDestroy(pUM);
        BnxeFmFini(pUM);

        return B_FALSE;
    }

    if (pUM->fmCapabilities &&
        BnxeCheckAccHandle(pUM->lm_dev.vars.reg_handle[BAR_0]) != DDI_FM_OK)
    {
        ddi_fm_service_impact(pUM->pDev, DDI_SERVICE_LOST);
        BnxeMutexDestroy(pUM);
        BnxePciDestroy(pUM);
        BnxeFmFini(pUM);

        return B_FALSE;
    }

    if (rc != LM_STATUS_SUCCESS)
    {
        BnxeFmErrorReport(pUM, DDI_FM_DEVICE_INVAL_STATE);
        ddi_fm_service_impact(pUM->pDev, DDI_SERVICE_LOST);
        BnxeMutexDestroy(pUM);
        BnxePciDestroy(pUM);
        BnxeFmFini(pUM);

        BnxeLogWarn(pUM, "Failed to get device information");
        return B_FALSE;
    }

    BnxeInitCamTableOffset(pUM);

    if (!BnxeIntrInit(pUM))
    {
        BnxeBarMemDestroy(pUM);
        BnxeMutexDestroy(pUM);
        BnxePciDestroy(pUM);
        BnxeFmFini(pUM);

        return B_FALSE;
    }

    if (!BnxeKstatInit(pUM))
    {
        BnxeIntrFini(pUM);
        BnxeBarMemDestroy(pUM);
        BnxeMutexDestroy(pUM);
        BnxePciDestroy(pUM);
        BnxeFmFini(pUM);

        return B_FALSE;
    }

    if (!BnxeGldInit(pUM))
    {
        BnxeKstatFini(pUM);
        BnxeIntrFini(pUM);
        BnxeBarMemDestroy(pUM);
        BnxeMutexDestroy(pUM);
        BnxePciDestroy(pUM);
        BnxeFmFini(pUM);

        return B_FALSE;
    }

    if (!BnxeWorkQueueInit(pUM))
    {
        BnxeGldFini(pUM);
        BnxeKstatFini(pUM);
        BnxeIntrFini(pUM);
        BnxeBarMemDestroy(pUM);
        BnxeMutexDestroy(pUM);
        BnxePciDestroy(pUM);
        BnxeFmFini(pUM);

        return B_FALSE;
    }

    snprintf(pUM->version,
             sizeof(pUM->version),
             "%s",
             BRCMVERSION);

    snprintf(pUM->versionLM,
             sizeof(pUM->versionLM),
             "%d.%d.%d",
             LM_DRIVER_MAJOR_VER,
             LM_DRIVER_MINOR_VER,
             LM_DRIVER_FIX_NUM);

    snprintf(pUM->versionFW,
             sizeof(pUM->versionFW),
             "%d.%d.%d.%d",
             BCM_5710_FW_MAJOR_VERSION,
             BCM_5710_FW_MINOR_VERSION,
             BCM_5710_FW_REVISION_VERSION,
             BCM_5710_FW_ENGINEERING_VERSION);

    snprintf(pUM->versionBC,
             sizeof(pUM->versionBC),
             "%d.%d.%d",
             ((pUM->lm_dev.hw_info.bc_rev >> 24) & 0xff),
             ((pUM->lm_dev.hw_info.bc_rev >> 16) & 0xff),
             ((pUM->lm_dev.hw_info.bc_rev >>  8) & 0xff));

    snprintf(pUM->chipName,
             sizeof(pUM->chipName),
             "%s",
             BnxeChipName(pUM));

    snprintf(pUM->chipID,
             sizeof(pUM->chipID),
             "0x%x",
             pUM->lm_dev.hw_info.chip_id);

    snprintf(pUM->intrAlloc,
             sizeof(pUM->intrAlloc),
             "%d %s",
             (pUM->intrType == DDI_INTR_TYPE_FIXED) ? 1 : (pUM->defIntr.intrCount +
                                                           pUM->fcoeIntr.intrCount +
                                                           pUM->rssIntr.intrCount),
             (pUM->intrType == DDI_INTR_TYPE_MSIX) ? "MSIX" :
             (pUM->intrType == DDI_INTR_TYPE_MSI)  ? "MSI"  :
                                                     "Fixed");

    BnxeLogInfo(pUM,
                "(0x%p) %s %s - v%s - FW v%s - BC v%s - %s (%s)",
                pUM,
                pUM->chipName,
                pUM->chipID,
                pUM->version,
                pUM->versionFW,
                pUM->versionBC,
                IS_MULTI_VNIC(&pUM->lm_dev) ? "MF" : "SF",
                pUM->intrAlloc);

    return B_TRUE;
}


static boolean_t BnxeDetachDevice(um_device_t * pUM)
{
    int rc;

#ifdef BNXE_FCOE_SUPPORT

    rc = BnxeFcoeFini(pUM);

    if ((rc != 0) && (rc != ENOTSUP) && (rc != ENODEV))
    {
        return B_FALSE;
    }

#endif /* BNXE_FCOE_SUPPORT */

    BnxeWorkQueueWaitAndDestroy(pUM);

    if (!BnxeGldFini(pUM))
    {
        return B_FALSE;
    }

    BnxeKstatFini(pUM);
    BnxeIntrFini(pUM);
    BnxeBarMemDestroy(pUM);
    BnxeMutexDestroy(pUM);
    BnxePciDestroy(pUM);
    BnxeFmFini(pUM);

    return B_TRUE;
}


#ifdef BNXE_FORCE_FCOE_LOAD

static void BnxeForceFcoeLoad(um_device_t * pUM)
{
    bnxe_fcoeio_t cp;

    memset(&cp, 0, sizeof(cp));
    cp.fcoeio_input.create_port.fcp_port_type = 1;

    if (BnxeFcoeInit(pUM, &cp) != 0)
    {
        BnxeLogInfo(pUM, "FAILED TO FORCE LOAD FCOE!");
    }
}

#endif /* BNXE_FORCE_FCOE_LOAD */


static int BnxeAttach(dev_info_t * pDev, ddi_attach_cmd_t cmd)
{
    um_device_t * pUM;

    switch (cmd)
    {
    case DDI_ATTACH:

        if ((pUM = kmem_zalloc(sizeof(um_device_t), KM_SLEEP)) == NULL)
        {
            BnxeLogWarn(NULL, "failed to allocate device structure");
            return DDI_FAILURE;
        }

        ddi_set_driver_private(pDev, pUM);

        /* set magic number for identification */
        pUM->magic = BNXE_MAGIC;

        /* save dev_info_t in the driver structure */
        pUM->pDev = pDev;

        d_list_clear(&pUM->memBlockList);
        d_list_clear(&pUM->memDmaList);
        d_list_clear(&pUM->memRegionList);
#ifdef BNXE_DEBUG_DMA_LIST
        d_list_clear(&pUM->memDmaListSaved);
#endif

        /* obtain a human-readable device name log messages with */
        pUM->instance = ddi_get_instance(pDev);
        snprintf(pUM->devName, sizeof(pUM->devName),
                 "bnxe%d", pUM->instance);

        if (!BnxeAttachDevice(pUM))
        {
            kmem_free(pUM, sizeof(um_device_t));
            return DDI_FAILURE;
        }

#ifdef BNXE_FORCE_FCOE_LOAD
        BnxeWorkQueueAddDelayGeneric(pUM, BnxeForceFcoeLoad, 1000);
#endif

        return DDI_SUCCESS;

    case DDI_RESUME:

        pUM = (um_device_t *)ddi_get_driver_private(pDev);

        /* sanity check */
        if (pUM == NULL || pUM->pDev != pDev)
        {
            BnxeLogWarn(NULL, "%s: dev_info_t match failed", __func__);
            return DDI_FAILURE;
        }

        if (BnxeHwResume(pUM) != 0)
        {
            BnxeLogWarn(pUM, "Fail to resume this device!");
            return DDI_FAILURE;
        }

        return DDI_SUCCESS;

    default:

        return DDI_FAILURE;
    }
}


static int BnxeDetach(dev_info_t * pDev, ddi_detach_cmd_t cmd)
{
    um_device_t * pUM;

    switch (cmd)
    {
    case DDI_DETACH:

        pUM = (um_device_t *)ddi_get_driver_private(pDev);

        /* sanity check */
        if (pUM == NULL || pUM->pDev != pDev)
        {
            BnxeLogWarn(NULL, "%s: dev_info_t match failed", __func__);
            return DDI_FAILURE;
        }

        if (pUM->intrEnabled != B_FALSE)
        {
            BnxeLogWarn(pUM, "Detaching a device that is currently running!");
            return DDI_FAILURE;
        }

        if (!BnxeDetachDevice(pUM))
        {
            BnxeLogWarn(pUM, "Can't detach it now, please try again later!");
            return DDI_FAILURE;
        }

        kmem_free(pUM, sizeof(um_device_t));

        return DDI_SUCCESS;

    case DDI_SUSPEND:

        pUM = (um_device_t *)ddi_get_driver_private(pDev);

        /* sanity check */
        if (pUM == NULL || pUM->pDev != pDev)
        {
            BnxeLogWarn(NULL, "%s: dev_info_t match failed", __func__);
            return DDI_FAILURE;
        }

        if (BnxeHwSuspend(pUM) != 0)
        {
            BnxeLogWarn(pUM, "Fail to suspend this device!");
            return DDI_FAILURE;
        }

        return DDI_SUCCESS;

    default:

        return DDI_FAILURE;
    }
}


#if (DEVO_REV > 3)

static int BnxeQuiesce(dev_info_t * pDev)
{
    um_device_t * pUM;

    pUM = (um_device_t *)ddi_get_driver_private(pDev);

    /* sanity check */
    if (pUM == NULL || pUM->pDev != pDev)
    {
        BnxeLogWarn(NULL, "%s: dev_info_t match failed", __func__);
        return DDI_FAILURE;
    }

    if (!pUM->plumbed)
    {
        return DDI_SUCCESS;
    }

    if (BnxeHwQuiesce(pUM) != 0)
    {
        BnxeLogWarn(pUM, "Failed to quiesce the device!");
        return DDI_FAILURE;
    }

    return DDI_SUCCESS;
}

#endif


void BnxeFcoeInitChild(dev_info_t * pDev,
                       dev_info_t * cDip)
{
    um_device_t *pUM = (um_device_t *) ddi_get_driver_private(pDev);

    if ((pUM == NULL) || (pUM->pDev != pDev))
    {
        BnxeLogWarn(NULL, "%s: dev_info_t match failed ", __func__);
        return;
    }

    ddi_set_name_addr(cDip, ddi_get_name_addr(pUM->pDev));
}


void BnxeFcoeUninitChild(dev_info_t * pDev,
                         dev_info_t * cDip)
{
	ddi_set_name_addr(cDip, NULL);
}


static int BnxeBusCtl(dev_info_t *   pDev,
                      dev_info_t *   pRDev,
                      ddi_ctl_enum_t op,
                      void *         pArg,
                      void *         pResult)
{
    um_device_t * pUM = (um_device_t *)ddi_get_driver_private(pDev);

    /* sanity check */
    if (pUM == NULL || pUM->pDev != pDev)
    {
        BnxeLogWarn(NULL, "%s: dev_info_t match failed", __func__);
        return DDI_FAILURE;
    }

    BnxeLogDbg(pUM, "BnxeBusCtl (%d)", op);

    switch (op)
    {
    case DDI_CTLOPS_REPORTDEV:
    case DDI_CTLOPS_IOMIN:
        break;
    case DDI_CTLOPS_INITCHILD:
        BnxeFcoeInitChild(pDev, (dev_info_t *) pArg);
        break;
    case DDI_CTLOPS_UNINITCHILD:
        BnxeFcoeUninitChild(pDev, (dev_info_t *) pArg);
        break;

    default:

        return (ddi_ctlops(pDev, pRDev, op, pArg, pResult));
    }

    return DDI_SUCCESS;
}


static int BnxeCbIoctl(dev_t    dev,
                       int      cmd,
                       intptr_t arg,
                       int      mode,
                       cred_t * credp,
                       int *    rvalp)
{
    BnxeBinding * pBinding = (BnxeBinding *)arg;
    um_device_t * pUM;

    (void)dev;
    (void)mode;
    (void)credp;
    (void)rvalp;

    if ((pBinding == NULL) ||
        (pBinding->pCliDev == NULL) ||
        (pBinding->pPrvDev == NULL))
    {
        BnxeLogWarn(NULL, "Invalid binding arg to ioctl %d", cmd);
        return -1;
    }

    pUM = (um_device_t *)ddi_get_driver_private(pBinding->pPrvDev);

    /* sanity check */
    if ((pUM == NULL) ||
        (pUM->fcoe.pDev != pBinding->pCliDev) ||
        (pUM->pDev != pBinding->pPrvDev))
    {
        BnxeLogWarn(NULL, "%s: dev_info_t match failed", __func__);
        return DDI_FAILURE;
    }

    switch (cmd)
    {
    case BNXE_BIND_FCOE:

        /* copy the binding struct and fill in the provider callback */

        BnxeLogInfo(pUM, "FCoE BIND start");

        if (!CLIENT_DEVI(pUM, LM_CLI_IDX_FCOE))
        {
            BnxeLogWarn(pUM, "FCoE BIND when DEVI is offline!");
            return -1;
        }

        if (CLIENT_BOUND(pUM, LM_CLI_IDX_FCOE))
        {
            BnxeLogWarn(pUM, "FCoE BIND when alread bound!");
            return -1;
        }

        pUM->fcoe.bind = *pBinding;

        pUM->fcoe.bind.prvCtl           = pBinding->prvCtl           = BnxeFcoePrvCtl;
        pUM->fcoe.bind.prvTx            = pBinding->prvTx            = BnxeFcoePrvTx;
        pUM->fcoe.bind.prvPoll          = pBinding->prvPoll          = BnxeFcoePrvPoll;
        pUM->fcoe.bind.prvSendWqes      = pBinding->prvSendWqes      = BnxeFcoePrvSendWqes;
        pUM->fcoe.bind.prvMapMailboxq   = pBinding->prvMapMailboxq   = BnxeFcoePrvMapMailboxq;
        pUM->fcoe.bind.prvUnmapMailboxq = pBinding->prvUnmapMailboxq = BnxeFcoePrvUnmapMailboxq;

        pUM->devParams.numRxDesc[LM_CLI_IDX_FCOE] = pBinding->numRxDescs;
        pUM->devParams.numTxDesc[LM_CLI_IDX_FCOE] = pBinding->numTxDescs;

        pUM->lm_dev.params.l2_rx_desc_cnt[LM_CLI_IDX_FCOE] = pBinding->numRxDescs;
        BnxeInitBdCnts(pUM, LM_CLI_IDX_FCOE);

        if (BnxeHwStartFCOE(pUM))
        {
            return -1;
        }

        CLIENT_BIND_SET(pUM, LM_CLI_IDX_FCOE);

        BnxeLogInfo(pUM, "FCoE BIND done");
        return 0;

    case BNXE_UNBIND_FCOE:

        /* clear the binding struct and stats */

        BnxeLogInfo(pUM, "FCoE UNBIND start");

        if (CLIENT_DEVI(pUM, LM_CLI_IDX_FCOE))
        {
            BnxeLogWarn(pUM, "FCoE UNBIND when DEVI is online!");
            return -1;
        }

        if (!CLIENT_BOUND(pUM, LM_CLI_IDX_FCOE))
        {
            BnxeLogWarn(pUM, "FCoE UNBIND when not bound!");
            return -1;
        }

        /* We must not detach until all packets held by fcoe are retrieved. */
        if (!BnxeWaitForPacketsFromClient(pUM, LM_CLI_IDX_FCOE))
        {
            return -1;
        }

        CLIENT_BIND_RESET(pUM, LM_CLI_IDX_FCOE);

        BnxeHwStopFCOE(pUM);

        memset(&pUM->fcoe.bind, 0, sizeof(pUM->fcoe.bind));
        memset(&pUM->fcoe.stats, 0, sizeof(pUM->fcoe.stats));

        pBinding->prvCtl           = NULL;
        pBinding->prvTx            = NULL;
        pBinding->prvPoll          = NULL;
        pBinding->prvSendWqes      = NULL;
        pBinding->prvMapMailboxq   = NULL;
        pBinding->prvUnmapMailboxq = NULL;

        pUM->fcoe.pDev = NULL; /* sketchy? */

        BnxeLogInfo(pUM, "FCoE UNBIND done");
        return 0;

    default:

        BnxeLogWarn(pUM, "Unknown ioctl %d", cmd);
        return -1;
    }
}


static struct bus_ops bnxe_bus_ops =
{
    BUSO_REV,
    nullbusmap,        /* bus_map */
    NULL,              /* bus_get_intrspec */
    NULL,              /* bus_add_intrspec */
    NULL,              /* bus_remove_intrspec */
    i_ddi_map_fault,   /* bus_map_fault */
    ddi_dma_map,       /* bus_dma_map */
    ddi_dma_allochdl,  /* bus_dma_allochdl */
    ddi_dma_freehdl,   /* bus_dma_freehdl */
    ddi_dma_bindhdl,   /* bus_dma_bindhdl */
    ddi_dma_unbindhdl, /* bus_unbindhdl */
    ddi_dma_flush,     /* bus_dma_flush */
    ddi_dma_win,       /* bus_dma_win */
    ddi_dma_mctl,      /* bus_dma_ctl */
    BnxeBusCtl,        /* bus_ctl */
    ddi_bus_prop_op,   /* bus_prop_op */
    NULL,              /* bus_get_eventcookie */
    NULL,              /* bus_add_eventcall */
    NULL,              /* bus_remove_event */
    NULL,              /* bus_post_event */
    NULL,              /* bus_intr_ctl */
    NULL,              /* bus_config */
    NULL,              /* bus_unconfig */
    NULL,              /* bus_fm_init */
    NULL,              /* bus_fm_fini */
    NULL,              /* bus_fm_access_enter */
    NULL,              /* bus_fm_access_exit */
    NULL,              /* bus_power */
    NULL
};


static struct cb_ops bnxe_cb_ops =
{
    nulldev,               /* cb_open */
    nulldev,               /* cb_close */
    nodev,                 /* cb_strategy */
    nodev,                 /* cb_print */
    nodev,                 /* cb_dump */
    nodev,                 /* cb_read */
    nodev,                 /* cb_write */
    BnxeCbIoctl,           /* cb_ioctl */
    nodev,                 /* cb_devmap */
    nodev,                 /* cb_mmap */
    nodev,                 /* cb_segmap */
    nochpoll,              /* cb_chpoll */
    ddi_prop_op,           /* cb_prop_op */
    NULL,                  /* cb_stream */
    (int)(D_MP | D_64BIT), /* cb_flag */
    CB_REV,                /* cb_rev */
    nodev,                 /* cb_aread */
    nodev,                 /* cb_awrite */
};


#if (DEVO_REV > 3)

static struct dev_ops bnxe_dev_ops =
{
    DEVO_REV,      /* devo_rev */
    0,             /* devo_refcnt */
    NULL,          /* devo_getinfo */
    nulldev,       /* devo_identify */
    nulldev,       /* devo_probe */
    BnxeAttach,    /* devo_attach */
    BnxeDetach,    /* devo_detach */
    nodev,         /* devo_reset */
    &bnxe_cb_ops,  /* devo_cb_ops */
    &bnxe_bus_ops, /* devo_bus_ops */
    NULL,          /* devo_power */
    BnxeQuiesce    /* devo_quiesce */
};

#else

static struct dev_ops bnxe_dev_ops =
{
    DEVO_REV,      /* devo_rev */
    0,             /* devo_refcnt */
    NULL,          /* devo_getinfo */
    nulldev,       /* devo_identify */
    nulldev,       /* devo_probe */
    BnxeAttach,    /* devo_attach */
    BnxeDetach,    /* devo_detach */
    nodev,         /* devo_reset */
    &bnxe_cb_ops,  /* devo_cb_ops */
    &bnxe_bus_ops, /* devo_bus_ops */
    NULL           /* devo_power */
};

#endif


static struct modldrv bnxe_modldrv =
{
    &mod_driverops,    /* drv_modops (must be mod_driverops for drivers) */
    BNXE_PRODUCT_INFO, /* drv_linkinfo (string displayed by modinfo) */
    &bnxe_dev_ops      /* drv_dev_ops */
};


static struct modlinkage bnxe_modlinkage =
{
    MODREV_1,        /* ml_rev */
    {
      &bnxe_modldrv, /* ml_linkage */
      NULL           /* NULL termination */
    }
};


int _init(void)
{
    int rc;

    mac_init_ops(&bnxe_dev_ops, "bnxe");

    /* Install module information with O/S */
    if ((rc = mod_install(&bnxe_modlinkage)) != DDI_SUCCESS)
    {
        BnxeLogWarn(NULL, "mod_install returned 0x%x", rc);
        mac_fini_ops(&bnxe_dev_ops);
        return rc;
    }

    mutex_init(&bnxeLoaderMutex, NULL, MUTEX_DRIVER, NULL);
    bnxeNumPlumbed = 0;

    BnxeLogInfo(NULL, "%s", BNXE_PRODUCT_BANNER);

    return rc;
}


int _fini(void)
{
    int rc;

    if ((rc = mod_remove(&bnxe_modlinkage)) == DDI_SUCCESS)
    {
        mac_fini_ops(&bnxe_dev_ops);
        mutex_destroy(&bnxeLoaderMutex);

        if (bnxeNumPlumbed > 0)
        {
            /*
             * This shouldn't be possible since modunload must only call _fini
             * when no instances are currently plumbed.
             */
            BnxeLogWarn(NULL, "%d instances have not been unplumbed", bnxeNumPlumbed);
        }
    }

    return rc;
}


int _info(struct modinfo * pModinfo)
{
    return mod_info(&bnxe_modlinkage, pModinfo);
}

