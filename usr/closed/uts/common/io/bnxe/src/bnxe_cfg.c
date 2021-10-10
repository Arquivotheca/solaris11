
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


BnxeLinkCfg bnxeLinkCfg =
{
    B_TRUE, /* link_autoneg   */
    B_TRUE, /* param_10000fdx */
    B_TRUE, /* param_2500fdx  */
    B_TRUE, /* param_1000fdx  */
    B_TRUE, /* param_100fdx   */
    B_TRUE, /* param_100hdx   */
    B_TRUE, /* param_10fdx    */
    B_TRUE, /* param_10hdx    */
    B_TRUE, /* param_txpause  */
    B_TRUE  /* param_rxpause  */
};


static lm_medium_t BnxeCfgMapLinkSettings(um_device_t * pUM)
{
    lm_device_t * pLM = &pUM->lm_dev;
    lm_medium_t   medium;

    medium = LM_MEDIUM_AUTO_DETECT;

    if (!pUM->curcfg.lnkcfg.link_autoneg)
    {
        if (pUM->curcfg.lnkcfg.param_10000fdx)
        {
            medium |= (LM_MEDIUM_SPEED_10GBPS | LM_MEDIUM_FULL_DUPLEX);
        }
        else if (pUM->curcfg.lnkcfg.param_1000fdx)
        {
            medium |= (LM_MEDIUM_SPEED_1000MBPS | LM_MEDIUM_FULL_DUPLEX);
        }
        else if (pUM->curcfg.lnkcfg.param_100fdx)
        {
            medium |= (LM_MEDIUM_SPEED_100MBPS | LM_MEDIUM_FULL_DUPLEX);
        }
        else if (pUM->curcfg.lnkcfg.param_100hdx)
        {
            medium |= (LM_MEDIUM_SPEED_100MBPS | LM_MEDIUM_HALF_DUPLEX);
        }
        else if (pUM->curcfg.lnkcfg.param_10fdx)
        {
            medium |= (LM_MEDIUM_SPEED_10MBPS | LM_MEDIUM_FULL_DUPLEX);
        }
        else if (pUM->curcfg.lnkcfg.param_10hdx)
        {
            medium |= (LM_MEDIUM_SPEED_10MBPS | LM_MEDIUM_HALF_DUPLEX);
        }
        else
        {
            /* configuration error */
            medium |= LM_MEDIUM_SPEED_AUTONEG;
            return medium;
        }

        return medium;
    }

    /* pUM->curcfg.lnkcfg.link_autoneg is set */

    if (pUM->curcfg.lnkcfg.param_10000fdx &&
        pUM->curcfg.lnkcfg.param_1000fdx  &&
        pUM->curcfg.lnkcfg.param_100fdx   &&
        pUM->curcfg.lnkcfg.param_100hdx   &&
        pUM->curcfg.lnkcfg.param_10fdx    &&
        pUM->curcfg.lnkcfg.param_10hdx)
    {
        /* all autoneg speeds are advertised so don't specify a speed */
        medium |= LM_MEDIUM_SPEED_AUTONEG;
        return medium;
    }

    pLM->params.selective_autoneg = SELECTIVE_AUTONEG_SINGLE_SPEED;

    if (pUM->curcfg.lnkcfg.param_10000fdx)
    {
        medium |= (LM_MEDIUM_SPEED_10GBPS | LM_MEDIUM_FULL_DUPLEX);

        if (pUM->curcfg.lnkcfg.param_1000fdx &&
            pUM->curcfg.lnkcfg.param_100fdx  &&
            pUM->curcfg.lnkcfg.param_100hdx  &&
            pUM->curcfg.lnkcfg.param_10fdx   &&
            pUM->curcfg.lnkcfg.param_10hdx)
        {
            pLM->params.selective_autoneg =
                SELECTIVE_AUTONEG_ENABLE_SLOWER_SPEEDS;
        }
    }
    else if (pUM->curcfg.lnkcfg.param_1000fdx)
    {
        medium |= (LM_MEDIUM_SPEED_1000MBPS | LM_MEDIUM_FULL_DUPLEX);

        if( pUM->curcfg.lnkcfg.param_100fdx &&
            pUM->curcfg.lnkcfg.param_100hdx &&
            pUM->curcfg.lnkcfg.param_10fdx  &&
            pUM->curcfg.lnkcfg.param_10hdx)
        {
            pLM->params.selective_autoneg =
                SELECTIVE_AUTONEG_ENABLE_SLOWER_SPEEDS;
        }
    }
    else if (pUM->curcfg.lnkcfg.param_100fdx)
    {
        medium |= (LM_MEDIUM_SPEED_100MBPS | LM_MEDIUM_FULL_DUPLEX);

        if (pUM->curcfg.lnkcfg.param_100hdx &&
            pUM->curcfg.lnkcfg.param_10fdx  &&
            pUM->curcfg.lnkcfg.param_10hdx)
        {
            pLM->params.selective_autoneg =
                SELECTIVE_AUTONEG_ENABLE_SLOWER_SPEEDS;
        }
    }
    else if (pUM->curcfg.lnkcfg.param_100hdx)
    {
        medium |= (LM_MEDIUM_SPEED_100MBPS | LM_MEDIUM_HALF_DUPLEX);

        if (pUM->curcfg.lnkcfg.param_10fdx &&
            pUM->curcfg.lnkcfg.param_10hdx)
        {
            pLM->params.selective_autoneg =
                SELECTIVE_AUTONEG_ENABLE_SLOWER_SPEEDS;
        }
    }
    else if (pUM->curcfg.lnkcfg.param_10fdx)
    {
        medium |= (LM_MEDIUM_SPEED_10MBPS | LM_MEDIUM_FULL_DUPLEX);

        if (pUM->curcfg.lnkcfg.param_10hdx)
        {
            pLM->params.selective_autoneg =
                SELECTIVE_AUTONEG_ENABLE_SLOWER_SPEEDS;
        }
    }
    else if (pUM->curcfg.lnkcfg.param_10hdx)
    {
        medium |= (LM_MEDIUM_SPEED_10MBPS | LM_MEDIUM_HALF_DUPLEX);
    }
    else
    {
        /* configuration error */
        pLM->params.selective_autoneg = SELECTIVE_AUTONEG_OFF;
        medium |= LM_MEDIUM_SPEED_AUTONEG;
    }

    return medium;
}


void BnxeCfgMapPhy(um_device_t * pUM)
{
    lm_device_t *     pLM = &pUM->lm_dev;
    lm_flow_control_t flowctrl;

    /* selective autonegotiation is by default off */
    pLM->params.selective_autoneg = SELECTIVE_AUTONEG_OFF;

    pLM->params.req_medium = BnxeCfgMapLinkSettings(pUM);

    flowctrl = LM_FLOW_CONTROL_NONE;

    if (pUM->curcfg.lnkcfg.param_txpause)
    {
        flowctrl |= LM_FLOW_CONTROL_TRANSMIT_PAUSE;
    }

    if (pUM->curcfg.lnkcfg.param_rxpause)
    {
        flowctrl |= LM_FLOW_CONTROL_RECEIVE_PAUSE;
    }

    if (pUM->curcfg.flow_autoneg)
    {
        /*
         * This value can be or'ed with RECEIVE_PAUSE and TRANSMIT_PAUSE.  If
         * the auto-negotiation is disabled and the RECEIVE_PAUSE and
         * TRANSMIT_PAUSE bits are set, then flow control is enabled regardless
         * of link partner's flow control capability.  Otherwise, if this bit is
         * set, then flow is negotiated with the link partner.  Values
         * 0x80000000 and 0x80000003 are equivalent.
         */
        flowctrl |= LM_FLOW_CONTROL_AUTO_PAUSE;
    }

    pLM->params.flow_ctrl_cap = flowctrl;
}


static void BnxeCfgGetVal(um_device_t * pUM,
                          char *        pName,
                          void *        pVal,
                          int           defaultVal,
                          boolean_t     boolVal)
{
    int val;
    #define BNXE_CFG_NAME_LEN_MAX 128
    char name[BNXE_CFG_NAME_LEN_MAX];

    /* first check if the hardcoded default has been overridden */

    snprintf(name, BNXE_CFG_NAME_LEN_MAX, "default_%s", pName);

    val = ddi_prop_get_int(DDI_DEV_T_ANY,
                           pUM->pDev,
                           (DDI_PROP_NOTPROM | DDI_PROP_DONTPASS),
                           name,
                           defaultVal);

    /* now check for a config for this specific instance */

    snprintf(name, BNXE_CFG_NAME_LEN_MAX, "bnxe%d_%s", pUM->instance, pName);

    val = ddi_prop_get_int(DDI_DEV_T_ANY,
                           pUM->pDev,
                           (DDI_PROP_NOTPROM | DDI_PROP_DONTPASS),
                           name,
                           val);

    if (boolVal)
    {
        *((boolean_t *)pVal) = (val) ? B_TRUE : B_FALSE;
    }
    else
    {
        *((int *)pVal) = val;
    }
}


void BnxeCfg_LLDP_DCBX(um_device_t * pUM)
{
    lm_device_t * pLM = &pUM->lm_dev;
    char name[64];
    int i;

    /* DCBX defaults configuration: DCBX not supported by default. */
    pLM->params.dcbx_port_params.dcbx_enabled = FALSE;

    /* DCBX defaults configuration: PFC not supported. */
    pLM->params.dcbx_port_params.pfc.enabled = FALSE;

    for (i = 0; i < LLFC_DRIVER_TRAFFIC_TYPE_MAX; i++)
    {
        pLM->params.dcbx_port_params.app.traffic_type_priority[i] =
            INVALID_TRAFFIC_TYPE_PRIORITY;
    }

    pLM->params.dcbx_port_params.pfc.priority_non_pauseable_mask = 0;

    memset(&pLM->params.lldp_config_params, 0xFF,
           sizeof(pLM->params.lldp_config_params));
    memset(&pLM->params.dcbx_config_params, 0xFF,
           sizeof(pLM->params.dcbx_config_params));

    pLM->params.dcbx_config_params.dcb_enable        = 1;
    pLM->params.dcbx_config_params.admin_dcbx_enable = 1;

    BnxeCfgGetVal(pUM, "lldp_overwrite_settings",
                  &pLM->params.lldp_config_params.overwrite_settings,
                  pLM->params.lldp_config_params.overwrite_settings,
                  B_FALSE);

    BnxeCfgGetVal(pUM, "lldp_msg_tx_hold",
                  &pLM->params.lldp_config_params.msg_tx_hold,
                  pLM->params.lldp_config_params.msg_tx_hold,
                  B_FALSE);

    BnxeCfgGetVal(pUM, "lldp_msg_fast_tx",
                  &pLM->params.lldp_config_params.msg_fast_tx,
                  pLM->params.lldp_config_params.msg_fast_tx,
                  B_FALSE);

    BnxeCfgGetVal(pUM, "lldp_tx_credit_max",
                  &pLM->params.lldp_config_params.tx_credit_max,
                  pLM->params.lldp_config_params.tx_credit_max,
                  B_FALSE);

    BnxeCfgGetVal(pUM, "lldp_msg_tx_interval",
                  &pLM->params.lldp_config_params.msg_tx_interval,
                  pLM->params.lldp_config_params.msg_tx_interval,
                  B_FALSE);

    BnxeCfgGetVal(pUM, "lldp_tx_fast",
                  &pLM->params.lldp_config_params.tx_fast,
                  pLM->params.lldp_config_params.tx_fast,
                  B_FALSE);

    BnxeCfgGetVal(pUM, "dcbx_dcb_enable",
                  &pLM->params.dcbx_config_params.dcb_enable,
                  pLM->params.dcbx_config_params.admin_dcbx_enable,
                  B_FALSE);

    BnxeCfgGetVal(pUM, "dcbx_admin_dcbx_enable",
                  &pLM->params.dcbx_config_params.admin_dcbx_enable,
                  pLM->params.dcbx_config_params.admin_dcbx_enable,
                  B_FALSE);

    BnxeCfgGetVal(pUM, "dcbx_overwrite_settings",
                  &pLM->params.dcbx_config_params.overwrite_settings,
                  pLM->params.dcbx_config_params.overwrite_settings,
                  B_FALSE);

    BnxeCfgGetVal(pUM, "dcbx_admin_dcbx_version",
                  &pLM->params.dcbx_config_params.admin_dcbx_version,
                  pLM->params.dcbx_config_params.admin_dcbx_version,
                  B_FALSE);

    BnxeCfgGetVal(pUM, "dcbx_admin_ets_enable",
                  &pLM->params.dcbx_config_params.admin_ets_enable,
                  pLM->params.dcbx_config_params.admin_ets_enable,
                  B_FALSE);

    BnxeCfgGetVal(pUM, "dcbx_admin_pfc_enable",
                  &pLM->params.dcbx_config_params.admin_pfc_enable,
                  pLM->params.dcbx_config_params.admin_pfc_enable,
                  B_FALSE);

    BnxeCfgGetVal(pUM, "dcbx_admin_tc_supported_tx_enable",
                  &pLM->params.dcbx_config_params.admin_tc_supported_tx_enable,
                  pLM->params.dcbx_config_params.admin_tc_supported_tx_enable,
                  B_FALSE);

    BnxeCfgGetVal(pUM, "dcbx_admin_ets_configuration_tx_enable",
                  &pLM->params.dcbx_config_params.admin_ets_configuration_tx_enable,
                  pLM->params.dcbx_config_params.admin_ets_configuration_tx_enable,
                  B_FALSE);

    BnxeCfgGetVal(pUM, "dcbx_admin_ets_recommendation_tx_enable",
                  &pLM->params.dcbx_config_params.admin_ets_recommendation_tx_enable,
                  pLM->params.dcbx_config_params.admin_ets_recommendation_tx_enable,
                  B_FALSE);

    BnxeCfgGetVal(pUM, "dcbx_admin_pfc_tx_enable",
                  &pLM->params.dcbx_config_params.admin_pfc_tx_enable,
                  pLM->params.dcbx_config_params.admin_pfc_tx_enable,
                  B_FALSE);

    BnxeCfgGetVal(pUM, "dcbx_admin_application_priority_tx_enable",
                  &pLM->params.dcbx_config_params.admin_application_priority_tx_enable,
                  pLM->params.dcbx_config_params.admin_application_priority_tx_enable,
                  B_FALSE);

    BnxeCfgGetVal(pUM, "dcbx_admin_ets_willing",
                  &pLM->params.dcbx_config_params.admin_ets_willing,
                  pLM->params.dcbx_config_params.admin_ets_willing,
                  B_FALSE);

    BnxeCfgGetVal(pUM, "dcbx_admin_pfc_willing",
                  &pLM->params.dcbx_config_params.admin_pfc_willing,
                  pLM->params.dcbx_config_params.admin_pfc_willing,
                  B_FALSE);

    BnxeCfgGetVal(pUM, "dcbx_admin_ets_reco_valid",
                  &pLM->params.dcbx_config_params.admin_ets_reco_valid,
                  pLM->params.dcbx_config_params.admin_ets_reco_valid,
                  B_FALSE);

    BnxeCfgGetVal(pUM, "dcbx_admin_app_priority_willing",
                  &pLM->params.dcbx_config_params.admin_app_priority_willing,
                  pLM->params.dcbx_config_params.admin_app_priority_willing,
                  B_FALSE);

    for (i = 0; i < 8; i++)
    {
        snprintf(name, sizeof(name), "dcbx_admin_configuration_bw_percentage_%d", i);
        BnxeCfgGetVal(pUM, name,
                      &pLM->params.dcbx_config_params.admin_configuration_bw_percentage[i],
                      pLM->params.dcbx_config_params.admin_configuration_bw_percentage[i],
                      B_FALSE);
    }

    for (i = 0; i < 8; i++)
    {
        snprintf(name, sizeof(name), "dcbx_admin_configuration_ets_pg_%d", i);
        BnxeCfgGetVal(pUM, name,
                      &pLM->params.dcbx_config_params.admin_configuration_ets_pg[i],
                      pLM->params.dcbx_config_params.admin_configuration_ets_pg[i],
                      B_FALSE);
    }

    for (i = 0; i < 8; i++)
    {
        snprintf(name, sizeof(name), "dcbx_admin_recommendation_bw_percentage_%d", i);
        BnxeCfgGetVal(pUM, name,
                      &pLM->params.dcbx_config_params.admin_recommendation_bw_percentage[i],
                      pLM->params.dcbx_config_params.admin_recommendation_bw_percentage[i],
                      B_FALSE);
    }

    for (i = 0; i < 8; i++)
    {
        snprintf(name, sizeof(name), "dcbx_admin_recommendation_ets_pg_%d", i);
        BnxeCfgGetVal(pUM, name,
                      &pLM->params.dcbx_config_params.admin_recommendation_ets_pg[i],
                      pLM->params.dcbx_config_params.admin_recommendation_ets_pg[i],
                      B_FALSE);
    }

    BnxeCfgGetVal(pUM, "dcbx_admin_pfc_bitmap",
                  &pLM->params.dcbx_config_params.admin_pfc_bitmap,
                  pLM->params.dcbx_config_params.admin_pfc_bitmap,
                  B_FALSE);

    for (i = 0; i < 4; i++)
    {
        snprintf(name, sizeof(name), "dcbx_admin_priority_app_table_%d_valid", i);
        BnxeCfgGetVal(pUM, name,
                      &pLM->params.dcbx_config_params.admin_priority_app_table[i].valid,
                      pLM->params.dcbx_config_params.admin_priority_app_table[i].valid,
                      B_FALSE);

        snprintf(name, sizeof(name), "dcbx_admin_priority_app_table_%d_priority", i);
        BnxeCfgGetVal(pUM, name,
                      &pLM->params.dcbx_config_params.admin_priority_app_table[i].priority,
                      pLM->params.dcbx_config_params.admin_priority_app_table[i].priority,
                      B_FALSE);

        snprintf(name, sizeof(name), "dcbx_admin_priority_app_table_%d_traffic_type", i);
        BnxeCfgGetVal(pUM, name,
                      &pLM->params.dcbx_config_params.admin_priority_app_table[i].traffic_type,
                      pLM->params.dcbx_config_params.admin_priority_app_table[i].traffic_type,
                      B_FALSE);

        snprintf(name, sizeof(name), "dcbx_admin_priority_app_table_%d_app_id", i);
        BnxeCfgGetVal(pUM, name,
                      &pLM->params.dcbx_config_params.admin_priority_app_table[i].app_id,
                      pLM->params.dcbx_config_params.admin_priority_app_table[i].app_id,
                      B_FALSE);
    }

    BnxeCfgGetVal(pUM, "dcbx_admin_default_priority",
                  &pLM->params.dcbx_config_params.admin_default_priority,
                  pLM->params.dcbx_config_params.admin_default_priority,
                  B_FALSE);
}


void BnxeCfgInit(um_device_t * pUM)
{
    int option, i;

    /* set the defaults */
    bcopy(&bnxeLinkCfg, &pUM->hwinit.lnkcfg, sizeof(BnxeLinkCfg));

    pUM->hwinit.flow_autoneg                  = B_TRUE;
    pUM->devParams.checksum                   = USER_OPTION_CKSUM_DEFAULT;
    pUM->devParams.enabled_oflds              = LM_OFFLOAD_NONE;
    pUM->devParams.mtu[LM_CLI_IDX_NDIS]       = USER_OPTION_MTU_DEFAULT;
    pUM->devParams.numRings                   = USER_OPTION_NUM_RINGS_DEFAULT;
    pUM->devParams.numRxDesc[LM_CLI_IDX_NDIS] = USER_OPTION_RX_BDS_DEFAULT;
    pUM->devParams.numTxDesc[LM_CLI_IDX_NDIS] = USER_OPTION_TX_BDS_DEFAULT;
    pUM->devParams.maxRxFree                  = USER_OPTION_RX_MAX_FREE_DEFAULT;
    pUM->devParams.maxTxFree                  = USER_OPTION_TX_MAX_FREE_DEFAULT;
    pUM->devParams.rxCopyThreshold            = USER_OPTION_RX_DCOPY_THRESH_DEFAULT;
    pUM->devParams.txCopyThreshold            = USER_OPTION_TX_DCOPY_THRESH_DEFAULT;
    pUM->devParams.intrCoalesce               = B_TRUE;
    pUM->devParams.intrRxPerSec               = USER_OPTION_INTR_COALESCE_RX_DEFAULT;
    pUM->devParams.intrTxPerSec               = USER_OPTION_INTR_COALESCE_TX_DEFAULT;
    pUM->devParams.disableMsix                = B_FALSE;
    pUM->devParams.l2_fw_flow_ctrl            = B_FALSE;
    pUM->devParams.lsoEnable                  = B_TRUE;
    pUM->devParams.logEnable                  = B_TRUE;
    pUM->devParams.routeTxRingPolicy          = BNXE_ROUTE_RING_TCPUDP;
#ifdef BNXE_FCOE_SUPPORT
    pUM->devParams.fcoeEnable                 = B_TRUE;
#else
    pUM->devParams.fcoeEnable                 = B_FALSE;
#endif /* BNXE_FCOE_SUPPORT */

    /* set the LLDP/DCBX defaults and get settings from bnxe.conf */
    BnxeCfg_LLDP_DCBX(pUM);

    /* override the defaults based on what is set in bnxe.conf */

    BnxeCfgGetVal(pUM, "adv_autoneg_cap",
                  &pUM->hwinit.lnkcfg.link_autoneg,
                  pUM->hwinit.lnkcfg.link_autoneg,
                  B_TRUE);

    BnxeCfgGetVal(pUM, "adv_10000fdx_cap",
                  &pUM->hwinit.lnkcfg.param_10000fdx,
                  pUM->hwinit.lnkcfg.param_10000fdx,
                  B_TRUE);

    BnxeCfgGetVal(pUM, "adv_2500fdx_cap",
                  &pUM->hwinit.lnkcfg.param_2500fdx,
                  pUM->hwinit.lnkcfg.param_2500fdx,
                  B_TRUE);

    BnxeCfgGetVal(pUM, "adv_1000fdx_cap",
                  &pUM->hwinit.lnkcfg.param_1000fdx,
                  pUM->hwinit.lnkcfg.param_1000fdx,
                  B_TRUE);

    BnxeCfgGetVal(pUM, "adv_100fdx_cap",
                  &pUM->hwinit.lnkcfg.param_100fdx,
                  pUM->hwinit.lnkcfg.param_100fdx,
                  B_TRUE);

    BnxeCfgGetVal(pUM, "adv_100hdx_cap",
                  &pUM->hwinit.lnkcfg.param_100hdx,
                  pUM->hwinit.lnkcfg.param_100hdx,
                  B_TRUE);

    BnxeCfgGetVal(pUM, "adv_10fdx_cap",
                  &pUM->hwinit.lnkcfg.param_10fdx,
                  pUM->hwinit.lnkcfg.param_10fdx,
                  B_TRUE);

    BnxeCfgGetVal(pUM, "adv_10hdx_cap",
                  &pUM->hwinit.lnkcfg.param_10hdx,
                  pUM->hwinit.lnkcfg.param_10hdx,
                  B_TRUE);

    BnxeCfgGetVal(pUM, "txpause_cap",
                  &pUM->hwinit.lnkcfg.param_txpause,
                  pUM->hwinit.lnkcfg.param_txpause,
                  B_TRUE);

    BnxeCfgGetVal(pUM, "rxpause_cap",
                  &pUM->hwinit.lnkcfg.param_rxpause,
                  pUM->hwinit.lnkcfg.param_rxpause,
                  B_TRUE);

    BnxeCfgGetVal(pUM, "autoneg_flow",
                  &pUM->hwinit.flow_autoneg,
                  pUM->hwinit.flow_autoneg,
                  B_TRUE);

    BnxeCfgGetVal(pUM, "checksum",
                  &pUM->devParams.checksum,
                  pUM->devParams.checksum,
                  B_FALSE);
    switch (pUM->devParams.checksum)
    {
    case USER_OPTION_CKSUM_L3:
        pUM->devParams.enabled_oflds = (LM_OFFLOAD_TX_IP_CKSUM |
                                        LM_OFFLOAD_RX_IP_CKSUM);
        break;

    case USER_OPTION_CKSUM_L3_L4:
        pUM->devParams.enabled_oflds = (LM_OFFLOAD_TX_IP_CKSUM  |
                                        LM_OFFLOAD_RX_IP_CKSUM  |
                                        LM_OFFLOAD_TX_TCP_CKSUM |
                                        LM_OFFLOAD_RX_TCP_CKSUM |
                                        LM_OFFLOAD_TX_UDP_CKSUM |
                                        LM_OFFLOAD_RX_UDP_CKSUM);
        break;

    case USER_OPTION_CKSUM_NONE:
    default:
        pUM->devParams.enabled_oflds = LM_OFFLOAD_NONE;
        break;
    }

    BnxeCfgGetVal(pUM, "mtu",
                  &option,
                  pUM->devParams.mtu[LM_CLI_IDX_NDIS],
                  B_FALSE);
    pUM->devParams.mtu[LM_CLI_IDX_NDIS] =
        (option < USER_OPTION_MTU_MIN) ?
            USER_OPTION_MTU_MIN :
            (option > USER_OPTION_MTU_MAX) ?
                USER_OPTION_MTU_MAX :
                option;
    pUM->lm_dev.params.mtu[LM_CLI_IDX_NDIS] = pUM->devParams.mtu[LM_CLI_IDX_NDIS];

    pUM->devParams.mtu[LM_CLI_IDX_FCOE]     = LM_MTU_FCOE_DEFAULT;
    pUM->lm_dev.params.mtu[LM_CLI_IDX_FCOE] = LM_MTU_FCOE_DEFAULT;

    pUM->lm_dev.params.mtu_max = (pUM->lm_dev.params.mtu[LM_CLI_IDX_NDIS] >
                                  pUM->lm_dev.params.mtu[LM_CLI_IDX_FCOE]) ?
                                     pUM->lm_dev.params.mtu[LM_CLI_IDX_NDIS] :
                                     pUM->lm_dev.params.mtu[LM_CLI_IDX_FCOE];

    BnxeCfgGetVal(pUM, "route_tx_ring_policy",
                  &pUM->devParams.routeTxRingPolicy,
                  pUM->devParams.routeTxRingPolicy,
                  B_FALSE);
    if ((pUM->devParams.routeTxRingPolicy != BNXE_ROUTE_RING_NONE) &&
        (pUM->devParams.routeTxRingPolicy != BNXE_ROUTE_RING_TCPUDP) &&
        (pUM->devParams.routeTxRingPolicy != BNXE_ROUTE_RING_DEST_MAC) &&
        (pUM->devParams.routeTxRingPolicy != BNXE_ROUTE_RING_MSG_PRIO))
    {
        pUM->devParams.routeTxRingPolicy = BNXE_ROUTE_RING_TCPUDP;
    }

    BnxeCfgGetVal(pUM, "num_rings",
                  &option,
                  pUM->devParams.numRings,
                  B_FALSE);
    pUM->devParams.numRings = (option < USER_OPTION_NUM_RINGS_MIN) ?
                                  USER_OPTION_NUM_RINGS_MIN :
                                  (option > USER_OPTION_NUM_RINGS_MAX) ?
                                      USER_OPTION_NUM_RINGS_MAX :
                                      option;

    /* numRings must be a power of two and <= max rss chains allowed */
    for (i = 1; pUM->devParams.numRings >> i; i++) { ; }
    pUM->devParams.numRings = (1 << (i - 1));
    if (pUM->devParams.numRings > LM_MAX_RSS_CHAINS(&pUM->lm_dev))
    {
        pUM->devParams.numRings = LM_MAX_RSS_CHAINS(&pUM->lm_dev);
    }

    BnxeCfgGetVal(pUM, "rx_descs",
                  &option,
                  pUM->devParams.numRxDesc[LM_CLI_IDX_NDIS],
                  B_FALSE);
    pUM->devParams.numRxDesc[LM_CLI_IDX_NDIS] =
        (option < USER_OPTION_BDS_MIN) ?
            USER_OPTION_BDS_MIN :
            (option > USER_OPTION_BDS_MAX) ?
                USER_OPTION_BDS_MAX :
                option;

    BnxeCfgGetVal(pUM, "tx_descs",
                  &option,
                  pUM->devParams.numTxDesc[LM_CLI_IDX_NDIS],
                  B_FALSE);
    pUM->devParams.numTxDesc[LM_CLI_IDX_NDIS] =
        (option < USER_OPTION_BDS_MIN) ?
            USER_OPTION_BDS_MIN :
            (option > USER_OPTION_BDS_MAX) ?
                USER_OPTION_BDS_MAX :
                option;

    BnxeCfgGetVal(pUM, "rx_free_reclaim",
                  &option,
                  pUM->devParams.maxRxFree,
                  B_FALSE);
    pUM->devParams.maxRxFree =
        (option < 0) ?
            0 :
            (option > pUM->devParams.numRxDesc[LM_CLI_IDX_NDIS]) ?
                pUM->devParams.numRxDesc[LM_CLI_IDX_NDIS] :
                option;

    BnxeCfgGetVal(pUM, "tx_free_reclaim",
                  &option,
                  pUM->devParams.maxTxFree,
                  B_FALSE);
    pUM->devParams.maxTxFree =
        (option < 0) ?
            0 :
            (option > pUM->devParams.numTxDesc[LM_CLI_IDX_NDIS]) ?
                pUM->devParams.numTxDesc[LM_CLI_IDX_NDIS] :
                option;

    /* threshold to enable double copy of receive packet */
    BnxeCfgGetVal(pUM, "rx_copy_threshold",
                  &pUM->devParams.rxCopyThreshold,
                  pUM->devParams.rxCopyThreshold,
                  B_FALSE);

    /* threshold to enable double copy of transmit packet */
    BnxeCfgGetVal(pUM, "tx_copy_threshold",
                  &pUM->devParams.txCopyThreshold,
                  pUM->devParams.txCopyThreshold,
                  B_FALSE);

    BnxeCfgGetVal(pUM, "interrupt_coalesce",
                  &pUM->devParams.intrCoalesce,
                  pUM->devParams.intrCoalesce,
                  B_TRUE);

    BnxeCfgGetVal(pUM, "rx_interrupt_coalesce_usec",
                  &option,
                  pUM->devParams.intrRxPerSec,
                  B_FALSE);
    option = (option < USER_OPTION_INTR_COALESCE_MIN) ?
                  USER_OPTION_INTR_COALESCE_MIN :
                  (option > USER_OPTION_INTR_COALESCE_MAX) ?
                      USER_OPTION_INTR_COALESCE_MAX :
                      option;
    pUM->devParams.intrRxPerSec = (1000000 / option); /* intrs per sec */

    BnxeCfgGetVal(pUM, "tx_interrupt_coalesce_usec",
                  &option,
                  pUM->devParams.intrTxPerSec,
                  B_FALSE);
    option = (option < USER_OPTION_INTR_COALESCE_MIN) ?
                 USER_OPTION_INTR_COALESCE_MIN :
                 (option > USER_OPTION_INTR_COALESCE_MAX) ?
                     USER_OPTION_INTR_COALESCE_MAX :
                     option;
    pUM->devParams.intrTxPerSec = (1000000 / option); /* intrs per sec */

    BnxeCfgGetVal(pUM, "disable_msix",
                  &pUM->devParams.disableMsix,
                  pUM->devParams.disableMsix,
                  B_TRUE);

    BnxeCfgGetVal(pUM, "l2_fw_flow_ctrl",
                  &pUM->devParams.l2_fw_flow_ctrl,
                  pUM->devParams.l2_fw_flow_ctrl,
                  B_TRUE);

    BnxeCfgGetVal(pUM, "lso_enable",
                  &pUM->devParams.lsoEnable,
                  pUM->devParams.lsoEnable,
                  B_TRUE);

    /* Only allow LSO if Tx TCP checksum is turned on. */
    if (!(pUM->devParams.enabled_oflds & LM_OFFLOAD_TX_TCP_CKSUM))
    {
        pUM->devParams.lsoEnable = B_FALSE;
    }

    BnxeCfgGetVal(pUM, "log_enable",
                  &pUM->devParams.logEnable,
                  pUM->devParams.logEnable,
                  B_TRUE);

#ifdef BNXE_FCOE_SUPPORT

    BnxeCfgGetVal(pUM, "fcoe_enable",
                  &pUM->devParams.fcoeEnable,
                  pUM->devParams.fcoeEnable,
                  B_TRUE);

#endif /* BNXE_FCOE_SUPPORT */

    /* Adjust the number of rx/tx descriptors if in multi-function mode. */

    if (IS_MULTI_VNIC(&pUM->lm_dev))
    {
        if (!(pUM->devParams.numRxDesc[LM_CLI_IDX_NDIS] /=
              USER_OPTION_MF_BDS_DIVISOR))
        {
            pUM->devParams.numRxDesc[LM_CLI_IDX_NDIS] = USER_OPTION_BDS_MIN;
        }

        if (!(pUM->devParams.numTxDesc[LM_CLI_IDX_NDIS] /=
              USER_OPTION_MF_BDS_DIVISOR))
        {
            pUM->devParams.numTxDesc[LM_CLI_IDX_NDIS] = USER_OPTION_BDS_MIN;
        }
    }

    /* map the phy settings based on the initial config */
    BnxeCfgMapPhy(pUM);
}


void BnxeCfgReset(um_device_t * pUM)
{
    /* reset the link status */
    pUM->props.link_speed   = 0;
    pUM->props.link_duplex  = B_FALSE;
    pUM->props.link_txpause = B_FALSE;
    pUM->props.link_rxpause = B_FALSE;

    /* reset the link partner status */
    pUM->remote.link_autoneg   = B_FALSE;
    pUM->remote.param_10000fdx = B_FALSE;
    pUM->remote.param_2500fdx  = B_FALSE;
    pUM->remote.param_1000fdx  = B_FALSE;
    pUM->remote.param_100fdx   = B_FALSE;
    pUM->remote.param_100hdx   = B_FALSE;
    pUM->remote.param_10fdx    = B_FALSE;
    pUM->remote.param_10hdx    = B_FALSE;
    pUM->remote.param_txpause  = B_FALSE;
    pUM->remote.param_rxpause  = B_FALSE;

    /* reset the configuration from the configured hardware default */
    bcopy(&pUM->hwinit, &pUM->curcfg, sizeof(BnxePhyCfg));

    /* remap the phy settings based on the initial config */
    BnxeCfgMapPhy(pUM);
}

