
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


#ifdef BNXE_DEBUG_DMA_LIST

static void BnxeVerifySavedDmaList(um_device_t * pUM)
{
    BnxeMemDma * pTmp;
    int i;

    BNXE_LOCK_ENTER_MEM(pUM);

    pTmp = (BnxeMemDma *)d_list_peek_head(&pUM->memDmaListSaved);
    while (pTmp)
    {
        BnxeLogWarn(pUM, "testing dma block %p / %p %d",
                    pTmp, pTmp->pDmaVirt, pTmp->size);
        for (i = 0; i < pTmp->size; i++)
        {
            if (((u8_t *)pTmp->pDmaVirt)[i] != 0x0)
            {
                BnxeDbgBreakMsg(pUM, "old dma block wacked %p (byte %i)",
                                pTmp, i);
            }
        }

        pTmp = (BnxeMemDma *)d_list_next_entry(&pTmp->link);
    }

    BNXE_LOCK_EXIT_MEM(pUM);
}

#endif /* BNXE_DEBUG_DMA_LIST */


static boolean_t BnxeRssEnable(um_device_t * pUM)
{
    #define BNXE_RSS_HASH_KEY_SIZE 40
    u8_t hashKey[BNXE_RSS_HASH_KEY_SIZE];
    #define BNXE_RSS_INDIRECTION_TABLE_SIZE 128 /* must be a power of 2 */
    u8_t indirectionTable[BNXE_RSS_INDIRECTION_TABLE_SIZE];
    lm_rss_hash_t hashType;
    int i, rc;

    if (!pUM->devParams.numRings)
    {
        return B_TRUE;
    }

    /* fill out the indirection table */
    for (i = 0; i < BNXE_RSS_INDIRECTION_TABLE_SIZE; i++)
    {
        indirectionTable[i] = (i % pUM->devParams.numRings);
    }

    /* seed the hash function with random data */
    random_get_pseudo_bytes(hashKey, BNXE_RSS_HASH_KEY_SIZE);

    hashType = (LM_RSS_HASH_IPV4     |
                LM_RSS_HASH_TCP_IPV4 |
                LM_RSS_HASH_IPV6     |
                LM_RSS_HASH_TCP_IPV6);

    rc = lm_enable_rss((lm_device_t *)pUM,
                       indirectionTable,
                       BNXE_RSS_INDIRECTION_TABLE_SIZE,
                       hashKey,
                       BNXE_RSS_HASH_KEY_SIZE,
                       hashType,
                       FALSE,
                       NULL);

    if (rc == LM_STATUS_PENDING)
    {
        if ((rc = lm_wait_config_rss_done(&pUM->lm_dev)) != LM_STATUS_SUCCESS)
        {
            BnxeLogWarn(pUM, "Failed to enable RSS from pending operation (%d)", rc);
            BnxeFmErrorReport(pUM, DDI_FM_DEVICE_NO_RESPONSE);
        }
    }
    else if (rc != LM_STATUS_SUCCESS)
    {
        BnxeLogWarn(pUM, "Failed to enable RSS (%d)", rc);
        BnxeFmErrorReport(pUM, DDI_FM_DEVICE_INVAL_STATE);
    }

    return (rc == LM_STATUS_SUCCESS) ? B_TRUE : B_FALSE;
}


static lm_status_t BnxeRssDisable(um_device_t * pUM)
{
    int rc;

    rc = lm_disable_rss((lm_device_t *)pUM, FALSE, NULL);

    if (rc == LM_STATUS_PENDING)
    {
        if ((rc = lm_wait_config_rss_done(&pUM->lm_dev)) != LM_STATUS_SUCCESS)
        {
            BnxeLogWarn(pUM, "Failed to disable RSS from pending operation (%d)", rc);
            BnxeFmErrorReport(pUM, DDI_FM_DEVICE_NO_RESPONSE);
        }
    }
    else if (rc != LM_STATUS_SUCCESS)
    {
        BnxeLogWarn(pUM, "Failed to disable RSS (%d)", rc);
        BnxeFmErrorReport(pUM, DDI_FM_DEVICE_INVAL_STATE);
    }

    return (rc == LM_STATUS_SUCCESS) ? B_TRUE : B_FALSE;
}


void BnxeUpdatePhy(um_device_t * pUM,
                   boolean_t     cfgMap)
{
    lm_device_t * pLM = &pUM->lm_dev;
    int rc;

    if (cfgMap)
    {
        /* remap the phy settings based on the new config */
        BnxeCfgMapPhy(pUM);
    }

    BNXE_LOCK_ENTER_PHY(pUM);

    //lm_reset_link(pLM);

    rc = lm_init_phy(pLM,
                     pLM->params.req_medium,
                     pLM->params.flow_ctrl_cap,
                     pLM->params.selective_autoneg,
                     pLM->params.wire_speed,
                     0);

    if (pUM->fmCapabilities &&
        BnxeCheckAccHandle(pLM->vars.reg_handle[BAR_0]) != DDI_FM_OK)
    {
        ddi_fm_service_impact(pUM->pDev, DDI_SERVICE_DEGRADED);
    }

    if (rc != LM_STATUS_SUCCESS)
    {
        BnxeLogWarn(pUM, "Failed to initialize the phy");
        BnxeFmErrorReport(pUM, DDI_FM_DEVICE_INVAL_STATE);
    }

    BNXE_LOCK_EXIT_PHY(pUM);
}


/*
 * (flag) TRUE = add, FALSE = remove
 *
 * This function must be called with BNXE_LOCK_ENTER_HWINIT held because this
 * is shared between GLDv3 and FCoE entry points.
 */
int BnxeMacAddress(um_device_t *   pUM,
                   int             cliIdx,
                   boolean_t       flag,
                   const uint8_t * pMacAddr)
{
    int i, rc;

    if ((cliIdx != LM_CLI_IDX_NDIS) && (cliIdx != LM_CLI_IDX_FCOE))
    {
        return EINVAL;
    }

    rc = lm_set_mac_addr(&pUM->lm_dev,
                         (u8_t *)pMacAddr,
                         /* XXX */ LM_SET_CAM_NO_VLAN_FILTER,
                         LM_CLI_CID(&pUM->lm_dev, cliIdx),
                         NULL, flag);

    if (rc == LM_STATUS_PENDING)
    {
        if ((rc = lm_wait_set_mac_done(&pUM->lm_dev,
                                       LM_CLI_CID(&pUM->lm_dev, cliIdx))) !=
            LM_STATUS_SUCCESS)
        {
            BnxeLogWarn(pUM, "Failed to %s MAC Address from pending operation (%d)",
                        (flag) ? "set" : "remove", rc);
            BnxeFmErrorReport(pUM, DDI_FM_DEVICE_NO_RESPONSE);
            return ENOMEM;
        }
    }
    else if ((rc != LM_STATUS_PENDING) && (rc != ECORE_EXISTS))
    {
        BnxeLogWarn(pUM, "Failed to %s MAC Address (%d)",
                    (flag) ? "set" : "remove", rc);
        BnxeFmErrorReport(pUM, DDI_FM_DEVICE_INVAL_STATE);
        return ENOMEM;
    }

    return 0;
}


/*
 * This function is used to enable or disable multicast packet reception for
 * particular multicast addresses.  (flag) TRUE = add, FALSE = remove.
 *
 * This function must be called with BNXE_LOCK_ENTER_HWINIT held because this
 * is shared between GLDv3 and FCoE entry points.
 */
int BnxeMulticast(um_device_t *   pUM,
                  int             cliIdx,
                  boolean_t       flag,
                  const uint8_t * pMcastAddr,
                  boolean_t       hwSet)
{
    int mcTableSize;
    int i, rc;

    if ((cliIdx != LM_CLI_IDX_NDIS) && (cliIdx != LM_CLI_IDX_FCOE))
    {
        return EINVAL;
    }

    mcTableSize = (cliIdx == LM_CLI_IDX_NDIS) ? LM_MC_NDIS_TABLE_SIZE :
                                                LM_MC_FCOE_TABLE_SIZE;

    if (flag && (pUM->mcastTableCnt[cliIdx] == mcTableSize))
    {
        /* if adding a new address and the table is full then bail */
        return ENOMEM;
    }
    else if (flag && (pMcastAddr == NULL))
    {
        /* adding a new address that isn't specified...? */
        return EINVAL;
    }
    else if (!flag && (pMcastAddr == NULL))
    {
        /* clear all multicast addresses */

        memset((u8_t *)(pUM->mcastTable +
                        (pUM->mcastTableBaseIdx[cliIdx] *
                         ETHERNET_ADDRESS_SIZE)),
               0, (mcTableSize * ETHERNET_ADDRESS_SIZE));
        pUM->mcastTableCnt[cliIdx] = 0;

        if (!hwSet)
        {
            return 0;
        }

        rc = lm_set_mc(&pUM->lm_dev, NULL, 0, NULL, cliIdx);

        if (rc == LM_STATUS_PENDING)
        {
            if ((rc = lm_wait_set_mc_done(&pUM->lm_dev, cliIdx)) !=
                LM_STATUS_SUCCESS)
            {
                BnxeLogWarn(pUM, "Failed to clear Multicast Address table from pending operation (%d)", rc);
                BnxeFmErrorReport(pUM, DDI_FM_DEVICE_NO_RESPONSE);
                return ENOMEM;
            }
        }
        else if (rc != LM_STATUS_SUCCESS)
        {
            BnxeLogWarn(pUM, "Failed to clear Multicast Address table (%d)", rc);
            BnxeFmErrorReport(pUM, DDI_FM_DEVICE_INVAL_STATE);
            return ENOMEM;
        }

        return 0;
    }

    for (i = pUM->mcastTableBaseIdx[cliIdx];
         i < (pUM->mcastTableBaseIdx[cliIdx] + pUM->mcastTableCnt[cliIdx]);
         i++)
    {
        if (IS_ETH_ADDRESS_EQUAL(pMcastAddr,
                                 &pUM->mcastTable[i * ETHERNET_ADDRESS_SIZE]))
        {
            break;
        }
    }

    if (flag)
    {
        /* only add the address if the table is empty or address not found */
        if ((pUM->mcastTableCnt[cliIdx] == 0) ||
            (i == (pUM->mcastTableBaseIdx[cliIdx] +
                   pUM->mcastTableCnt[cliIdx])))
        {
            COPY_ETH_ADDRESS(pMcastAddr,
                             &pUM->mcastTable[(pUM->mcastTableBaseIdx[cliIdx] +
                                               pUM->mcastTableCnt[cliIdx]) *
                                              ETHERNET_ADDRESS_SIZE]);
            pUM->mcastTableCnt[cliIdx]++;
        }
    }
    else /* (!flag) */
    {
        if (i == (pUM->mcastTableBaseIdx[cliIdx] + pUM->mcastTableCnt[cliIdx]))
        {
            /* the address isn't in the table */
            return ENXIO;
        }

        /* remove the address from the table by copying in the last entry */

        if (i < (pUM->mcastTableBaseIdx[cliIdx] + pUM->mcastTableCnt[cliIdx] - 1))
        {
            COPY_ETH_ADDRESS(&pUM->mcastTable[(pUM->mcastTableBaseIdx[cliIdx] +
                                               pUM->mcastTableCnt[cliIdx] - 1) *
                                              ETHERNET_ADDRESS_SIZE],
                             &pUM->mcastTable[i * ETHERNET_ADDRESS_SIZE]);
        }

        /* clear the last entry for sanity when debugging */
        memset(&pUM->mcastTable[(pUM->mcastTableBaseIdx[cliIdx] +
                                 pUM->mcastTableCnt[cliIdx] - 1) *
                                ETHERNET_ADDRESS_SIZE],
               0, ETHERNET_ADDRESS_SIZE);

        pUM->mcastTableCnt[cliIdx]--;
    }

    if (!hwSet)
    {
        return 0;
    }

    rc = lm_set_mc(&pUM->lm_dev,
                   (u8_t *)(pUM->mcastTable +
                            (pUM->mcastTableBaseIdx[cliIdx] *
                             ETHERNET_ADDRESS_SIZE)),
                   (pUM->mcastTableCnt[cliIdx] * ETHERNET_ADDRESS_SIZE),
                   NULL, cliIdx);

    if (rc == LM_STATUS_PENDING)
    {
        if ((rc = lm_wait_set_mc_done(&pUM->lm_dev, cliIdx)) !=
            LM_STATUS_SUCCESS)
        {
            BnxeLogWarn(pUM, "Failed to set Multicast Address table from pending operation (%d)", rc);
            BnxeFmErrorReport(pUM, DDI_FM_DEVICE_NO_RESPONSE);
            return ENOMEM;
        }
    }
    else if (rc != LM_STATUS_SUCCESS)
    {
        BnxeLogWarn(pUM, "Failed to set Multicast Address table (%d)", rc);
        BnxeFmErrorReport(pUM, DDI_FM_DEVICE_INVAL_STATE);
        return ENOMEM;
    }

    return 0;
}


/*
 * This function must be called with BNXE_LOCK_ENTER_HWINIT held because this
 * is shared between GLDv3 and FCoE entry points.
 */
int BnxeRxMask(um_device_t * pUM,
               int           cliIdx,
               lm_rx_mask_t  mask)
{
    int rc;

    if ((cliIdx != LM_CLI_IDX_NDIS) && (cliIdx != LM_CLI_IDX_FCOE))
    {
        return EINVAL;
    }

    pUM->devParams.rx_filter_mask[cliIdx] = mask;

    rc = lm_set_rx_mask(&pUM->lm_dev,
                        LM_CLI_CID(&pUM->lm_dev, cliIdx), mask, NULL);

    if (rc == LM_STATUS_PENDING)
    {
        if ((rc =
             lm_wait_set_rx_mask_done(&pUM->lm_dev,
                                      LM_CLI_CID(&pUM->lm_dev, cliIdx))) !=
            LM_STATUS_SUCCESS)
        {
            BnxeLogWarn(pUM, "Failed to set Rx mask from pending operation (%d)", rc);
            BnxeFmErrorReport(pUM, DDI_FM_DEVICE_NO_RESPONSE);
            return ENOMEM;
        }
    }

    if (pUM->fmCapabilities &&
        BnxeCheckAccHandle(pUM->lm_dev.vars.reg_handle[BAR_0]) != DDI_FM_OK)
    {
        BnxeLogWarn(pUM, "DMA fault when setting Rx mask");
        ddi_fm_service_impact(pUM->pDev, DDI_SERVICE_LOST);
        return ENOMEM;
    }

    if (rc != LM_STATUS_SUCCESS)
    {
        BnxeLogWarn(pUM, "Failed to set Rx mask (%d)", rc);
        BnxeFmErrorReport(pUM, DDI_FM_DEVICE_INVAL_STATE);
        return ENOMEM;
    }

    return 0;
}


boolean_t BnxeEstablishHwConn(um_device_t * pUM,
                              int           cid)
{
    lm_device_t * pLM = &pUM->lm_dev;
    lm_client_con_params_t cliParams;
    int sb_id;
    int rc;

    sb_id = lm_sb_id_from_chain(&pUM->lm_dev, cid);

    memset(&cliParams, 0, sizeof(cliParams));
    cliParams.mtu         = pUM->devParams.mtu[LM_CHAIN_IDX_CLI(pLM, cid)];
    //cliParams.lah_size    = pUM->devParams.mtu[LM_CHAIN_IDX_CLI(pLM, cid)];
    cliParams.lah_size    = 0;
    cliParams.num_rx_desc = pUM->devParams.numRxDesc[LM_CHAIN_IDX_CLI(pLM, cid)];
    cliParams.num_tx_desc = pUM->devParams.numTxDesc[LM_CHAIN_IDX_CLI(pLM, cid)];
    cliParams.attributes  = (LM_CLIENT_ATTRIBUTES_RX | LM_CLIENT_ATTRIBUTES_TX);

    BnxeLogDbg(pUM, "Setting up client for cid %d", cid);
    if (lm_setup_client_con_params(pLM, cid, &cliParams) != LM_STATUS_SUCCESS)
    {
        BnxeLogWarn(pUM, "Failed to setup client for cid %d", cid);
        return B_FALSE;
    }

    /*********************************************************/

    BnxeLogDbg(pUM, "Initializing client for cid %d", cid);
    rc = lm_init_client_con(pLM, cid, TRUE);

    if (pUM->fmCapabilities &&
        BnxeCheckAccHandle(pUM->pPciCfg) != DDI_FM_OK)
    {
        ddi_fm_service_impact(pUM->pDev, DDI_SERVICE_LOST);
        return B_FALSE;
    }

    if (pUM->fmCapabilities &&
        BnxeCheckAccHandle(pLM->vars.reg_handle[BAR_0]) != DDI_FM_OK)
    {
        ddi_fm_service_impact(pUM->pDev, DDI_SERVICE_LOST);
        return B_FALSE;
    }

    if (rc != LM_STATUS_SUCCESS)
    {
        BnxeLogWarn(pUM, "Failed to initialize client for cid %d", cid);
        BnxeFmErrorReport(pUM, DDI_FM_DEVICE_INVAL_STATE);
        return B_FALSE;
    }

    /*********************************************************/

    BnxeLogDbg(pUM, "Establishing client for cid %d", cid);
    rc = lm_establish_eth_con(pLM, cid, sb_id);

    if (pUM->fmCapabilities &&
        BnxeCheckAccHandle(pUM->pPciCfg) != DDI_FM_OK)
    {
        ddi_fm_service_impact(pUM->pDev, DDI_SERVICE_LOST);
        return B_FALSE;
    }

    if (pUM->fmCapabilities &&
        BnxeCheckAccHandle(pLM->vars.reg_handle[BAR_0]) != DDI_FM_OK)
    {
        ddi_fm_service_impact(pUM->pDev, DDI_SERVICE_LOST);
        return B_FALSE;
    }

    if (rc != LM_STATUS_SUCCESS)
    {
        BnxeLogWarn(pUM, "Failed to establish client connection");
        BnxeFmErrorReport(pUM, DDI_FM_DEVICE_INVAL_STATE);
        return B_FALSE;
    }

    return B_TRUE;
}


int BnxeHwStartFCOE(um_device_t * pUM)
{
    lm_device_t * pLM = &pUM->lm_dev;
    int rc;

    if (!BNXE_FCOE(pUM))
    {
        BnxeDbgBreakMsg(pUM, "Inside BnxeHwStartFCOE and FCoE not enabled!");
        return -1;
    }

    BNXE_LOCK_ENTER_HWINIT(pUM);

    BnxeLogInfo(pUM, "BnxeHwStartFCOE: Starting FCoE (clients %s)",
                BnxeClientsHw(pUM));

    if (BnxeHwStartCore(pUM))
    {
        goto BnxeHwStartFCOE_error;
    }

    if (!pUM->hwInitDone)
    {
        BnxeLogWarn(pUM, "BnxeHwStartFCOE: Failed, hardware not initialized (clients %s)",
                    BnxeClientsHw(pUM));
        goto BnxeHwStartFCOE_error;
    }

    /*********************************************************/

    BnxeLogDbg(pUM, "Allocating FCoE Resources");

    if (lm_fc_alloc_resc(&pUM->lm_dev) != LM_STATUS_SUCCESS)
    {
        BnxeLogWarn(pUM, "Failed to allocate FCoE resources");
        goto BnxeHwStartFCOE_error;
    }

    /*********************************************************/

    BnxeLogDbg(pUM, "Opening FCoE Ethernet Connection");

    pUM->lm_dev.ofld_info.state_blks[STATE_BLOCK_FCOE] =
        &pUM->lm_dev.fcoe_info.run_time.state_blk;

    if (!BnxeEstablishHwConn(pUM, FCOE_CID(pLM)))
    {
        goto BnxeHwStartFCOE_error;
    }

    /*********************************************************/

    BnxeLogDbg(pUM, "Initializing FCoE Tx Pkts");

    if (BnxeTxPktsInit(pUM, LM_CLI_IDX_FCOE))
    {
        BnxeLogWarn(pUM, "Failed to allocate FCoE Tx resources");
        goto BnxeHwStartFCOE_error;
    }

    /*********************************************************/

    BnxeLogDbg(pUM, "Initializing FCoE Rx Pkts");

    if (BnxeRxPktsInit(pUM, LM_CLI_IDX_FCOE))
    {
        BnxeLogWarn(pUM, "Failed to allocate FCoE Rx resources");
        goto BnxeHwStartFCOE_error;
    }

    if (BnxeRxPktsInitPostBuffers(pUM, LM_CLI_IDX_FCOE))
    {
        BnxeLogWarn(pUM, "Failed to post FCoE Rx buffers");
        goto BnxeHwStartFCOE_error;
    }

    /*********************************************************/

    BnxeLogDbg(pUM, "Setting FCoE MAC Address");

    if (BnxeMacAddress(pUM, LM_CLI_IDX_FCOE, B_TRUE,
                       pLM->hw_info.fcoe_mac_addr) < 0)
    {
        goto BnxeHwStartFCOE_error;
    }

    /*********************************************************/

    BnxeLogDbg(pUM, "Setting FCoE Multicast Addresses");

#define ALL_FCOE_MACS   (const uint8_t *)"\x01\x10\x18\x01\x00\x00"
#define ALL_ENODE_MACS  (const uint8_t *)"\x01\x10\x18\x01\x00\x01"

    if ((BnxeMulticast(pUM, LM_CLI_IDX_FCOE, B_TRUE, ALL_FCOE_MACS, B_FALSE) < 0) ||
        (BnxeMulticast(pUM, LM_CLI_IDX_FCOE, B_TRUE, ALL_ENODE_MACS, B_TRUE) < 0))
    {
        goto BnxeHwStartFCOE_error;
    }

    /*********************************************************/

    BnxeLogDbg(pUM, "Turning on FCoE Rx Mask");

    if (BnxeRxMask(pUM, LM_CLI_IDX_FCOE, (
                                          LM_RX_MASK_ACCEPT_UNICAST
                                      //| LM_RX_MASK_ACCEPT_ALL_MULTICAST
                                        | LM_RX_MASK_ACCEPT_MULTICAST
                                      //| LM_RX_MASK_ACCEPT_BROADCAST
                                      //| LM_RX_MASK_PROMISCUOUS_MODE
                                         )) < 0)
    {
        goto BnxeHwStartFCOE_error;
    }

    /*********************************************************/

    CLIENT_HW_SET(pUM, LM_CLI_IDX_FCOE);

    BnxeLogInfo(pUM, "BnxeHwStartFCOE: FCoE started (clients %s)",
                BnxeClientsHw(pUM));

    BNXE_LOCK_EXIT_HWINIT(pUM);
    return 0;

BnxeHwStartFCOE_error:

    BNXE_LOCK_EXIT_HWINIT(pUM);
    return -1;
}


int BnxeHwStartL2(um_device_t * pUM)
{
    lm_device_t * pLM = &pUM->lm_dev;
    int idx, rc;

    BNXE_LOCK_ENTER_HWINIT(pUM);

    BnxeLogInfo(pUM, "BnxeHwStartL2: Starting L2 (clients %s)",
                BnxeClientsHw(pUM));

    if (BnxeHwStartCore(pUM))
    {
        goto BnxeHwStartL2_error;
    }

    if (!pUM->hwInitDone)
    {
        BnxeLogWarn(pUM, "BnxeHwStartL2: Failed, hardware not initialized (clients %s)",
                    BnxeClientsHw(pUM));
        goto BnxeHwStartL2_error;
    }

    /*********************************************************/

    BnxeLogDbg(pUM, "Opening L2 Ethernet Connections (%d)",
               pLM->params.rss_chain_cnt);

#if 0
    LM_FOREACH_RSS_IDX_SKIP_LEADING(pLM, idx)
#else
    LM_FOREACH_RSS_IDX(pLM, idx)
#endif
    {
        if (!BnxeEstablishHwConn(pUM, idx))
        {
            goto BnxeHwStartL2_error;
        }
    }

    /*********************************************************/

    BnxeLogDbg(pUM, "Initializing Tx Pkts");

    if (BnxeTxPktsInit(pUM, LM_CLI_IDX_NDIS))
    {
        BnxeLogWarn(pUM, "Failed to allocate tx resources");
        goto BnxeHwStartL2_error;
    }

    /*********************************************************/

    BnxeLogDbg(pUM, "Initializing Rx Pkts");

    if (BnxeRxPktsInit(pUM, LM_CLI_IDX_NDIS))
    {
        BnxeLogWarn(pUM, "Failed to allocate L2 Rx resources");
        goto BnxeHwStartL2_error;
    }

    if (BnxeRxPktsInitPostBuffers(pUM, LM_CLI_IDX_NDIS))
    {
        BnxeLogWarn(pUM, "Failed to post L2 Rx buffers");
        goto BnxeHwStartL2_error;
    }

    /*********************************************************/

    BnxeLogDbg(pUM, "Enabling RSS");

    if (!BnxeRssEnable(pUM))
    {
        goto BnxeHwStartL2_error;
    }

    /*********************************************************/

    BnxeLogDbg(pUM, "Setting L2 MAC Address");

    /* use the hw programmed address (GLDv3 will overwrite if needed) */ 

    {
        u8_t zero_mac_addr[ETHERNET_ADDRESS_SIZE];
        memset(zero_mac_addr, 0, ETHERNET_ADDRESS_SIZE);

        if (IS_ETH_ADDRESS_EQUAL(pUM->gldMac, zero_mac_addr))
        {
            COPY_ETH_ADDRESS(pUM->lm_dev.hw_info.mac_addr,
                             pUM->lm_dev.params.mac_addr);
        }
        else
        {
            COPY_ETH_ADDRESS(pUM->gldMac,
                             pUM->lm_dev.params.mac_addr);
        }
    }

    if (BnxeMacAddress(pUM, LM_CLI_IDX_NDIS, B_TRUE,
                       pUM->lm_dev.params.mac_addr) < 0)
    {
        goto BnxeHwStartL2_error;
    }

    /*********************************************************/

    BnxeLogDbg(pUM, "Turning on L2 Rx Mask");

    if (BnxeRxMask(pUM, LM_CLI_IDX_NDIS, (
                                          LM_RX_MASK_ACCEPT_UNICAST
                                      //| LM_RX_MASK_ACCEPT_ALL_MULTICAST
                                        | LM_RX_MASK_ACCEPT_MULTICAST
                                        | LM_RX_MASK_ACCEPT_BROADCAST
                                      //| LM_RX_MASK_PROMISCUOUS_MODE
                                         )) < 0)
    {
        goto BnxeHwStartL2_error;
    }

    /*********************************************************/

    CLIENT_HW_SET(pUM, LM_CLI_IDX_NDIS);

    BNXE_LOCK_EXIT_HWINIT(pUM);

    /*********************************************************/

    /*
     * Force a link update.  Another client might already be up in which case
     * the link status won't change during this plumb of the L2 client.
     */
    BnxeGldLink(pUM, (pUM->devParams.lastIndLink == LM_STATUS_LINK_ACTIVE) ?
                         LINK_STATE_UP : LINK_STATE_DOWN);

    BnxeLogInfo(pUM, "BnxeHwStartL2: L2 started (clients %s)",
                BnxeClientsHw(pUM));

    return 0;

BnxeHwStartL2_error:

    /* XXX Need cleanup! */

    BNXE_LOCK_EXIT_HWINIT(pUM);
    return -1;
}


/* Must be called with BNXE_LOCK_ENTER_HWINIT taken! */
int BnxeHwStartCore(um_device_t * pUM)
{
    lm_device_t * pLM = &pUM->lm_dev;
    int rc;

    if (pUM->hwInitDone)
    {
        /* already initialized */
        BnxeLogInfo(pUM, "BnxeHwStartCore: Hardware already initialized (clients %s)",
                    BnxeClientsHw(pUM));
        return 0;
    }

    BnxeLogInfo(pUM, "BnxeHwStartCore: Starting hardware (clients %s)",
                BnxeClientsHw(pUM));

    /*********************************************************/

    /* reset the configuration to the hardware default */
    BnxeCfgReset(pUM);

    /*********************************************************/

    BnxeLogDbg(pUM, "Allocating LM Resources");

    if (lm_alloc_resc(pLM) != LM_STATUS_SUCCESS)
    {
        BnxeLogWarn(pUM, "Failed to allocate resources");
        goto BnxeHwStartCore_error;
    }

    /*********************************************************/

    BnxeLogDbg(pUM, "Initializing BRCM Chip");

    rc = lm_chip_init(pLM);

    if (pUM->fmCapabilities &&
        BnxeCheckAccHandle(pUM->pPciCfg) != DDI_FM_OK)
    {
        ddi_fm_service_impact(pUM->pDev, DDI_SERVICE_LOST);
        goto BnxeHwStartCore_error;
    }

    if (pUM->fmCapabilities &&
        BnxeCheckAccHandle(pLM->vars.reg_handle[BAR_0]) != DDI_FM_OK)
    {
        ddi_fm_service_impact(pUM->pDev, DDI_SERVICE_LOST);
        goto BnxeHwStartCore_error;
    }

    if (rc != LM_STATUS_SUCCESS)
    {
        BnxeLogWarn(pUM, "Failed to initialize chip");
        BnxeFmErrorReport(pUM, DDI_FM_DEVICE_INVAL_STATE);
        goto BnxeHwStartCore_error;
    }

    /*********************************************************/

    BnxeLogDbg(pUM, "Enabling Interrupts");

    if (BnxeIntrEnable(pUM))
    {
        BnxeLogWarn(pUM, "Failed to enable interrupts");
        goto BnxeHwStartCore_error;
    }

    /*********************************************************/

    BnxeLogDbg(pUM, "Starting BRCM Chip");

    rc = lm_chip_start(pLM);

    if (pUM->fmCapabilities &&
        BnxeCheckAccHandle(pLM->vars.reg_handle[BAR_0]) != DDI_FM_OK)
    {
        ddi_fm_service_impact(pUM->pDev, DDI_SERVICE_LOST);
        goto BnxeHwStartCore_error;
    }

    if (rc != LM_STATUS_SUCCESS)
    {
        BnxeLogWarn(pUM, "Failed to start chip");
        BnxeFmErrorReport(pUM, DDI_FM_DEVICE_INVAL_STATE);
        goto BnxeHwStartCore_error;
    }

    /*********************************************************/

    BnxeLogDbg(pUM, "Initializing DCBX");

    lm_dcbx_init(pLM);

    /*********************************************************/

    BnxeLogDbg(pUM, "Initializing Phy");

    BnxeUpdatePhy(pUM, B_FALSE);

#if 0
    /*********************************************************/

    BnxeLogDbg(pUM, "Establishing Leading Ethernet Connection");

    if (!BnxeEstablishHwConn(pUM, LM_SW_LEADING_RSS_CID(pdev)))
    {
        goto BnxeHwStartCore_error;
    }
#endif

    /*********************************************************/

    BnxeLogDbg(pUM, "Starting Timer");

    BnxeTimerStart(pUM);

    /*********************************************************/

    atomic_swap_32(&pUM->hwInitDone, B_TRUE);

    BnxeLogInfo(pUM, "BnxeHwStartCore: Hardware started (clients %s)",
                BnxeClientsHw(pUM));

    return 0;

BnxeHwStartCore_error:

    return -1;
}


void BnxeHwStopFCOE(um_device_t * pUM)
{
    lm_device_t * pLM = &pUM->lm_dev;
    int rc;

    if (!BNXE_FCOE(pUM))
    {
        BnxeDbgBreakMsg(pUM, "Inside BnxeHwStopFCOE and FCoE not enabled!");
        return;
    }

    BNXE_LOCK_ENTER_HWINIT(pUM);

    BnxeLogInfo(pUM, "BnxeHwStopFCOE: Stopping FCoE (clients %s)",
                BnxeClientsHw(pUM));

    CLIENT_HW_RESET(pUM, LM_CLI_IDX_FCOE);

    /*********************************************************/

    BnxeLogDbg(pUM, "Turning off FCoE RX Mask");

    BnxeRxMask(pUM, LM_CLI_IDX_FCOE, LM_RX_MASK_ACCEPT_NONE);

    /*********************************************************/

    BnxeLogDbg(pUM, "Clearing the FCoE Multicast Table");

    BnxeMulticast(pUM, LM_CLI_IDX_FCOE, B_FALSE, NULL, B_TRUE);

    /*********************************************************/

    BnxeLogDbg(pUM, "Closing FCoE Connection");

    if ((rc = lm_close_eth_con(pLM, FCOE_CID(pLM))) != LM_STATUS_SUCCESS)
    {
        BnxeLogWarn(pUM, "Failed to close FCoE conn %d (%d)",
                    FCOE_CID(pLM), rc);
        BnxeFmErrorReport(pUM, DDI_FM_DEVICE_INVAL_STATE);
    }

    /*********************************************************/

    BnxeLogDbg(pUM, "Aborting FCoE TX Chains");

    BnxeTxPktsAbort(pUM, LM_CLI_IDX_FCOE);

    /*********************************************************/

    BnxeLogDbg(pUM, "Aborting FCoE RX Chains");

    BnxeRxPktsAbort(pUM, LM_CLI_IDX_FCOE);

    /*********************************************************/

    BnxeLogDbg(pUM, "Cleaning up FCoE Tx Pkts");

    BnxeTxPktsFini(pUM, LM_CLI_IDX_FCOE);

    /*********************************************************/

    BnxeLogDbg(pUM, "Cleaning up FCoE Rx Pkts");

    BnxeRxPktsFini(pUM, LM_CLI_IDX_FCOE);

    /*********************************************************/

    BnxeLogDbg(pUM, "Clearing FCoE Resources");

    if ((rc = lm_fc_clear_resc(pLM)) != LM_STATUS_SUCCESS)
    {
        BnxeLogWarn(pUM, "Failed to clear FCoE resources (%d)\n", rc);
    }

    lm_cid_recycled_cb_deregister(pLM, FCOE_CONNECTION_TYPE);

    /*********************************************************/

    BnxeHwStopCore(pUM);

    /*********************************************************/

    BnxeLogInfo(pUM, "BnxeHwStopFCOE: FCoE stopped (clients %s)",
                BnxeClientsHw(pUM));

    BNXE_LOCK_EXIT_HWINIT(pUM);
}


void BnxeHwStopL2(um_device_t * pUM)
{
    lm_device_t * pLM = &pUM->lm_dev;
    int idx, rc;

    BNXE_LOCK_ENTER_HWINIT(pUM);

    BnxeLogInfo(pUM, "BnxeHwStopL2: Stopping L2 (clients %s)",
                BnxeClientsHw(pUM));

    CLIENT_HW_RESET(pUM, LM_CLI_IDX_NDIS);

    /*********************************************************/

    BnxeLogDbg(pUM, "Turning off L2 RX Mask");

    BnxeRxMask(pUM, LM_CLI_IDX_NDIS, LM_RX_MASK_ACCEPT_NONE);

    /*********************************************************/

    BnxeLogDbg(pUM, "Clearing the L2 MAC Address");

    /*
     * Reset the mac_addr to hw programmed default and then clear
     * it in the firmware.
     */
    {
        u8_t zero_mac_addr[ETHERNET_ADDRESS_SIZE];
        memset(zero_mac_addr, 0, ETHERNET_ADDRESS_SIZE);

        COPY_ETH_ADDRESS(pUM->lm_dev.hw_info.mac_addr,
                         pUM->lm_dev.params.mac_addr);
        memset(pUM->gldMac, 0, ETHERNET_ADDRESS_SIZE);

        BnxeMacAddress(pUM, LM_CLI_IDX_NDIS, B_FALSE, zero_mac_addr);
    }

    /*********************************************************/

    BnxeLogDbg(pUM, "Clearing the L2 Multicast Table");

    BnxeMulticast(pUM, LM_CLI_IDX_NDIS, B_FALSE, NULL, B_TRUE);

    /*********************************************************/

    BnxeLogDbg(pUM, "Disabling RSS");

    BnxeRssDisable(pUM);

    /*********************************************************/

    /*
     * In Solaris when RX traffic is accepted, the system might generate and
     * attempt to send some TX packets (from within gld_recv()!).  Claiming any
     * TX locks before this point would create a deadlock.  The ISR would be
     * waiting for a lock acquired here that would never be freed, since we
     * in-turn would be waiting for the ISR to finish here. Consequently, we
     * acquire the TX lock as soon as we know that no TX traffic is a result of
     * RX traffic.
     */
    BNXE_LOCK_ENTER_GLDTX(pUM, RW_WRITER);

    /*********************************************************/

    BnxeLogDbg(pUM, "Closing L2 Ethernet Connections (%d)",
               pLM->params.rss_chain_cnt);

#if 0
    LM_FOREACH_RSS_IDX_SKIP_LEADING(pLM, idx)
#else
    LM_FOREACH_RSS_IDX(pLM, idx)
#endif
    {
        if ((rc = lm_close_eth_con(pLM, idx)) != LM_STATUS_SUCCESS)
        {
            BnxeLogWarn(pUM, "Failed to close Ethernet conn on RSS %d (%d)",
                        idx, rc);
            BnxeFmErrorReport(pUM, DDI_FM_DEVICE_INVAL_STATE);
        }
    }

    /*********************************************************/

    BnxeLogDbg(pUM, "Aborting L2 Tx Chains");

    BnxeTxPktsAbort(pUM, LM_CLI_IDX_NDIS);

    /*********************************************************/

    BnxeLogDbg(pUM, "Aborting L2 Rx Chains");

    BnxeRxPktsAbort(pUM, LM_CLI_IDX_NDIS);

    /*********************************************************/

    BNXE_LOCK_EXIT_GLDTX(pUM);

    /*********************************************************/

    BnxeLogDbg(pUM, "Cleaning up L2 Tx Pkts");

    BnxeTxPktsFini(pUM, LM_CLI_IDX_NDIS);

    /*********************************************************/

    BnxeLogDbg(pUM, "Cleaning up L2 Rx Pkts");

    BnxeRxPktsFini(pUM, LM_CLI_IDX_NDIS);

    /*********************************************************/

    BnxeHwStopCore(pUM);

    /*********************************************************/

    BnxeLogInfo(pUM, "BnxeHwStopL2: L2 stopped (clients %s)",
                BnxeClientsHw(pUM));

    BNXE_LOCK_EXIT_HWINIT(pUM);
}


/* Must be called with BNXE_LOCK_ENTER_HWINIT taken! */
void BnxeHwStopCore(um_device_t * pUM)
{
    lm_device_t *  pLM = &pUM->lm_dev;
    BnxeMemBlock * pMemBlock;
    BnxeMemDma *   pMemDma;
    lm_address_t   physAddr;
    int rc;

    physAddr.as_ptr = NULL;

    if (!pUM->hwInitDone)
    {
        /* already finished? (should never get here) */
        BnxeLogWarn(pUM, "BnxeHwStopCore: Hardware already stopped (clients %s)",
                    BnxeClientsHw(pUM));
        return;
    }

    if (BnxeIsClientBound(pUM))
    {
        BnxeLogInfo(pUM, "BnxeHwStopCore: Hardware cannot be stopped (clients %s)",
                    BnxeClientsHw(pUM));
        return;
    }

    BnxeLogInfo(pUM, "BnxeHwStopCore: Stopping hardware (clients %s)",
                BnxeClientsHw(pUM));

    mm_indicate_link(pLM, LM_STATUS_LINK_DOWN, pUM->devParams.lastIndMedium);

    /*********************************************************/

    BnxeLogDbg(pUM, "Stopping Timer");

    BnxeTimerStop(pUM);

#if 0
    /*********************************************************/

    BnxeLogDbg(pUM, "Closing Leading Ethernet Connection");

    if ((rc = lm_close_eth_con(pLM, LM_SW_LEADING_RSS_CID(pLM))) !=
         LM_STATUS_SUCCESS)
    {
        BnxeLogWarn(pUM, "Failed to close Leading conn %d (%d)",
                    LM_SW_LEADING_RSS_CID(pLM), rc);
        BnxeFmErrorReport(pUM, DDI_FM_DEVICE_INVAL_STATE);
    }
#endif

    /*********************************************************/

    BnxeLogDbg(pUM, "Stopping DCBX");

    lm_dcbx_free_resc(pLM);

    /*********************************************************/

    BnxeLogDbg(pUM, "Stopping BRCM Chip");

    rc = lm_chip_stop(pLM);

    if (pUM->fmCapabilities &&
        BnxeCheckAccHandle(pLM->vars.reg_handle[BAR_0]) != DDI_FM_OK)
    {
        ddi_fm_service_impact(pUM->pDev, DDI_SERVICE_DEGRADED);
    }

    if (rc != LM_STATUS_SUCCESS)
    {
        BnxeFmErrorReport(pUM, DDI_FM_DEVICE_INVAL_STATE);
    }

    /*********************************************************/

    BnxeLogDbg(pUM, "Disabling Interrupts");

    BnxeIntrDisable(pUM);

    /*********************************************************/

    BnxeLogDbg(pUM, "Resetting BRCM Chip");

    lm_chip_reset(pLM, LM_REASON_DRIVER_SHUTDOWN);

    if (pUM->fmCapabilities &&
        BnxeCheckAccHandle(pUM->pPciCfg) != DDI_FM_OK)
    {
        ddi_fm_service_impact(pUM->pDev, DDI_SERVICE_DEGRADED);
    }

    if (pUM->fmCapabilities &&
        BnxeCheckAccHandle(pLM->vars.reg_handle[BAR_0]) != DDI_FM_OK)
    {
        ddi_fm_service_impact(pUM->pDev, DDI_SERVICE_DEGRADED);
    }

    /*********************************************************/

    while (!d_list_is_empty(&pUM->memBlockList))
    {
        pMemBlock = (BnxeMemBlock *)d_list_peek_head(&pUM->memBlockList);
        mm_rt_free_mem(pLM,
                       ((char *)pMemBlock->pBuf + BNXE_MEM_CHECK_LEN),
                       (pMemBlock->size - (BNXE_MEM_CHECK_LEN * 2)),
                       LM_CLI_IDX_NDIS);
    }

#ifndef BNXE_DEBUG_DMA_LIST
    while (!d_list_is_empty(&pUM->memDmaList))
    {
        pMemDma = (BnxeMemDma *)d_list_peek_head(&pUM->memDmaList);
        mm_rt_free_phys_mem(pLM,
                            pMemDma->size,
                            pMemDma->pDmaVirt,
                            physAddr,
                            LM_CLI_IDX_NDIS);
    }
#else
    {
        BnxeMemDma * pTmp;
        int i;

        BNXE_LOCK_ENTER_MEM(pUM);

        pTmp = (BnxeMemDma *)d_list_peek_head(&pUM->memDmaList);
        while (pTmp)
        {
            for (i = 0; i < pTmp->size; i++)
            {
                ((u8_t *)pTmp->pDmaVirt)[i] = 0x0;
            }

            pTmp = (BnxeMemDma *)d_list_next_entry(&pTmp->link);
        }

        d_list_add_head(&pUM->memDmaListSaved, &pUM->memDmaList);
        d_list_clear(&pUM->memDmaList);

        BNXE_LOCK_EXIT_MEM(pUM);

        BnxeVerifySavedDmaList(pUM);
    }
#endif /* BNXE_DEBUG_DMA_LIST */

    atomic_swap_32(&pUM->hwInitDone, B_FALSE);

    BnxeLogInfo(pUM, "BnxeHwStopCore: Hardware stopped (clients %s)",
                BnxeClientsHw(pUM));
}


int BnxeHwResume(um_device_t * pUM)
{
    lm_device_t * pLM = &pUM->lm_dev;
    int rc;

    BnxeLogDbg(pUM, "Setting Power State");
    lm_set_power_state(pLM, LM_POWER_STATE_D0, LM_WAKE_UP_MODE_NONE, FALSE);

    /* XXX Do we need it? */
    BnxeLogDbg(pUM, "Enabling PCI DMA");
    lm_enable_pci_dma(pLM);

    if (pUM->fmCapabilities &&
        BnxeCheckAccHandle(pLM->vars.reg_handle[BAR_0]) != DDI_FM_OK)
    {
        ddi_fm_service_impact(pUM->pDev, DDI_SERVICE_LOST);
        return -1;
    }

    if (!pUM->plumbed)
    {
        /* XXX
         * Won't work under new model with multiple clients. Need an
         * extra pause mechanism/layer for suspend and resume.
         */
        if (BnxeHwStartCore(pUM))
        {
            return -1;
        }

        atomic_swap_32(&pUM->plumbed, B_TRUE);
    }

    return 0;
}


int BnxeHwSuspend(um_device_t * pUM)
{
    lm_device_t * pLM = &pUM->lm_dev;

    lm_reset_set_inprogress(pLM);
    lm_reset_mask_attn(pLM);

    disable_blocks_attention(pLM);

    if (pUM->plumbed)
    {
        /* XXX
         * Won't work under new model with multiple clients. Need an
         * extra pause mechanism/layer for suspend and resume.
         */
        BnxeHwStopCore(pUM);
        atomic_swap_32(&pUM->plumbed, B_FALSE);
    }

    /* XXX proper lm_wake_up_mode_t when WOL supported */
    lm_set_d3_nwuf(pLM, LM_WAKE_UP_MODE_NONE);

    if (pUM->fmCapabilities &&
        BnxeCheckAccHandle(pUM->pPciCfg) != DDI_FM_OK)
    {
        ddi_fm_service_impact(pUM->pDev, DDI_SERVICE_DEGRADED);
        return LM_STATUS_FAILURE;
    }

    if (pUM->fmCapabilities &&
        BnxeCheckAccHandle(pLM->vars.reg_handle[BAR_0]) != DDI_FM_OK)
    {
        ddi_fm_service_impact(pUM->pDev, DDI_SERVICE_DEGRADED);
        return -1;
    }

    /* XXX proper lm_wake_up_mode_t when WOL supported */
    lm_set_d3_mpkt(pLM, LM_WAKE_UP_MODE_NONE);

    if (pUM->fmCapabilities &&
        BnxeCheckAccHandle(pLM->vars.reg_handle[BAR_0]) != DDI_FM_OK)
    {
        ddi_fm_service_impact(pUM->pDev, DDI_SERVICE_DEGRADED);
        return -1;
    }

    /* XXX Do we need it? */
    BnxeLogDbg(pUM, "Disabling PCI DMA");
    lm_disable_pci_dma(pLM);

    if (pUM->fmCapabilities &&
        BnxeCheckAccHandle(pLM->vars.reg_handle[BAR_0]) != DDI_FM_OK)
    {
        ddi_fm_service_impact(pUM->pDev, DDI_SERVICE_DEGRADED);
        return -1;
    }

    return 0;
}


#if (DEVO_REV > 3)

/*
 * This is a non-blocking function to make sure no more interrupt and dma memory
 * access of this hardware. We don't have to free any resource here.
 */
int BnxeHwQuiesce(um_device_t * pUM)
{
    lm_device_t * pLM = &pUM->lm_dev;

#if 0
    lm_chip_stop(pLM);
#endif

    lm_disable_int(pLM);

    lm_chip_reset(pLM, LM_REASON_DRIVER_SHUTDOWN);

    BnxeRxPktsAbort(pUM, LM_CLI_IDX_NDIS);
    BnxeTxPktsAbort(pUM, LM_CLI_IDX_NDIS);

    return 0;
}

#endif

