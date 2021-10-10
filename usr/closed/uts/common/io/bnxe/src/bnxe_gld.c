
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

#include <sys/mac.h>
#include <sys/mac_ether.h>
#include <sys/dlpi.h>

#ifndef __Solaris_11 

/* so pointless :-( */
#define mri_driver  mr_driver
#define mri_start   mr_start
#define mri_stop    mr_stop
#define mri_intr    mr_intr
#define mri_poll    mr_poll
#define mri_tx      mr_send
#define mgi_driver  mrg_driver
#define mgi_start   mrg_start
#define mgi_stop    mrg_stop
#define mgi_count   mrg_count
#define mgi_addmac  mrg_addmac
#define mgi_remmac  mrg_addmac
#define mr_gaddring mr_gadd_ring
#define mr_gremring mr_grem_ring

#endif /* not __Solaris_11 */

/*
 * Reconfiguring the network devices parameters require net_config
 * privilege starting Solaris 10.  Only root user is allowed to
 * update device parameter in Solaris 9 and earlier version. Following
 * declaration allows single binary image to run on all OS versions.
 */
extern int secpolicy_net_config(const cred_t *, boolean_t);
extern int drv_priv(cred_t *);
#pragma weak secpolicy_net_config
#pragma weak drv_priv

#ifdef MC_SETPROP

char * bnxeLink_priv_props[] =
{
    "_adv_2500fdx_cap",
    "_en_2500fdx_cap",
    "_adv_txpause_cap",
    "_en_txpause_cap",
    "_txpause",
    "_adv_rxpause_cap",
    "_en_rxpause_cap",
    "_rxpause",
    "_autoneg_flow",
    "_checksum",
    "_num_rings",
    "_rx_descs",
    "_rx_free_reclaim",
    "_rx_copy_threshold",
    "_tx_descs",
    "_tx_free_reclaim",
    "_tx_copy_threshold",
    "_tx_ring_policy",
    "_interrupt_coalesce",
    "_rx_interrupt_coalesce_usec",
    "_tx_interrupt_coalesce_usec",
    "_disable_msix",
    "_l2_fw_flow_ctrl",
    "_lso_enable",
    "_log_enable",
#ifdef BNXE_FCOE_SUPPORT
    "_fcoe_enable",
#endif
    NULL
};

#endif /* MC_SETPROP */


static int BnxeMacStats(void *     pArg,
                        uint_t     stat,
                        uint64_t * pVal)
{
    um_device_t * pUM = (um_device_t *)pArg;
    lm_device_t * pLM;
    b10_l2_chip_statistics_t b10_l2_stats;
    int rc = 0;

    if ((pUM == NULL) || (pVal == NULL))
    {
        return EINVAL;
    }

    pLM = &pUM->lm_dev;

    BNXE_LOCK_ENTER_GLD(pUM);

    if (!pUM->plumbed)
    {
        BNXE_LOCK_EXIT_GLD(pUM);
        return EAGAIN;
    }

    *pVal = 0;

    switch (stat)
    {
    case MAC_STAT_IFSPEED:
        *pVal = (pUM->props.link_speed * 1000000ULL);
        break;

    case MAC_STAT_MULTIRCV:
        lm_stats_get_l2_chip_stats(pLM, &b10_l2_stats);
        *pVal = b10_l2_stats.IfHCInMulticastPkts;
        break;

    case MAC_STAT_BRDCSTRCV:
        lm_stats_get_l2_chip_stats(pLM, &b10_l2_stats);
        *pVal = b10_l2_stats.IfHCInBroadcastPkts;
        break;

    case MAC_STAT_MULTIXMT:
        lm_stats_get_l2_chip_stats(pLM, &b10_l2_stats);
        *pVal = b10_l2_stats.IfHCOutMulticastPkts;
        break;

    case MAC_STAT_BRDCSTXMT:
        lm_stats_get_l2_chip_stats(pLM, &b10_l2_stats);
        *pVal = b10_l2_stats.IfHCOutBroadcastPkts;
        break;

    case MAC_STAT_NORCVBUF:
        lm_get_stats(pLM, LM_STATS_RCV_NO_BUFFER_DROP, (u64_t *)pVal);
        break;

    case MAC_STAT_IERRORS:
    case ETHER_STAT_MACRCV_ERRORS:
        lm_stats_get_l2_chip_stats(pLM, &b10_l2_stats);
        *pVal = b10_l2_stats.IfInErrors;
        break;

    case MAC_STAT_OERRORS:
        /* XXX not available */
        break;

    case MAC_STAT_COLLISIONS:
        lm_stats_get_l2_chip_stats(pLM, &b10_l2_stats);
        *pVal = b10_l2_stats.EtherStatsCollisions;
        break;

    case MAC_STAT_RBYTES:
        lm_stats_get_l2_chip_stats(pLM, &b10_l2_stats);
        *pVal = b10_l2_stats.IfHCInOctets;
        break;

    case MAC_STAT_IPACKETS:
        lm_stats_get_l2_chip_stats(pLM, &b10_l2_stats);
        *pVal = b10_l2_stats.IfHCInPkts;
        break;

    case MAC_STAT_OBYTES:
        lm_stats_get_l2_chip_stats(pLM, &b10_l2_stats);
        *pVal = b10_l2_stats.IfHCOutOctets;
        break;

    case MAC_STAT_OPACKETS:
        lm_stats_get_l2_chip_stats(pLM, &b10_l2_stats);
        *pVal = b10_l2_stats.IfHCOutPkts;
        break;

    case ETHER_STAT_ALIGN_ERRORS:
        lm_stats_get_l2_chip_stats(pLM, &b10_l2_stats);
        *pVal = b10_l2_stats.Dot3StatsAlignmentErrors;
        break;

    case ETHER_STAT_FCS_ERRORS:
        lm_stats_get_l2_chip_stats(pLM, &b10_l2_stats);
        *pVal = b10_l2_stats.Dot3StatsFCSErrors;
        break;

    case ETHER_STAT_FIRST_COLLISIONS:
        lm_stats_get_l2_chip_stats(pLM, &b10_l2_stats);
        *pVal = b10_l2_stats.Dot3StatsSingleCollisionFrames;
        break;

    case ETHER_STAT_MULTI_COLLISIONS:
        lm_stats_get_l2_chip_stats(pLM, &b10_l2_stats);
        *pVal = b10_l2_stats.Dot3StatsMultipleCollisionFrames;
        break;

    case ETHER_STAT_DEFER_XMTS:
        lm_stats_get_l2_chip_stats(pLM, &b10_l2_stats);
        *pVal = b10_l2_stats.Dot3StatsDeferredTransmissions;
        break;

    case ETHER_STAT_TX_LATE_COLLISIONS:
        lm_stats_get_l2_chip_stats(pLM, &b10_l2_stats);
        *pVal = b10_l2_stats.Dot3StatsLateCollisions;
        break;

    case ETHER_STAT_EX_COLLISIONS:
        lm_stats_get_l2_chip_stats(pLM, &b10_l2_stats);
        *pVal = b10_l2_stats.Dot3StatsExcessiveCollisions;
        break;

    case ETHER_STAT_MACXMT_ERRORS:
        lm_stats_get_l2_chip_stats(pLM, &b10_l2_stats);
        *pVal = b10_l2_stats.Dot3StatsInternalMacTransmitErrors;
        break;

    case ETHER_STAT_CARRIER_ERRORS:
        lm_stats_get_l2_chip_stats(pLM, &b10_l2_stats);
        *pVal = b10_l2_stats.Dot3StatsCarrierSenseErrors;
        break;

    case ETHER_STAT_TOOLONG_ERRORS:
        lm_stats_get_l2_chip_stats(pLM, &b10_l2_stats);
        *pVal = b10_l2_stats.EtherStatsOverrsizePkts;
        break;

#if (MAC_VERSION > 1)
    case ETHER_STAT_TOOSHORT_ERRORS:
        lm_stats_get_l2_chip_stats(pLM, &b10_l2_stats);
        *pVal = b10_l2_stats.EtherStatsUndersizePkts;
        break;
#endif

    case ETHER_STAT_XCVR_ADDR:
        *pVal = pLM->vars.phy_addr;
        break;

    case ETHER_STAT_XCVR_ID:
        *pVal = pLM->hw_info.phy_id;
        break;

    case ETHER_STAT_XCVR_INUSE:
        switch (pUM->props.link_speed)
        {
        case 0: /* no speed then status is down */
            *pVal = XCVR_NONE;
            break;

        case 1000:
            *pVal = XCVR_1000X;
            break;

        case 100:
            *pVal = XCVR_100X;
            break;

        case 10:
            *pVal = XCVR_10;
            break;

        default:
            /* catches 2500/10000 */
            *pVal = XCVR_UNDEFINED;
        }
        break;

#if (MAC_VERSION > 1)
    case ETHER_STAT_CAP_10GFDX:
        *pVal = 1;
        break;
#endif

    case ETHER_STAT_CAP_1000FDX:
        *pVal = 1;
        break;

#if 0
    case ETHER_STAT_CAP_1000HDX:
        //*pVal = linkconf->param_1000hdx;
        *pVal = 0;
        break;
#endif

    case ETHER_STAT_CAP_100FDX:
        //*pVal = linkconf->param_100fdx;
        *pVal = 1;
        break;

    case ETHER_STAT_CAP_100HDX:
        //*pVal = linkconf->param_100hdx;
        *pVal = 1;
        break;

    case ETHER_STAT_CAP_10FDX:
        //*pVal = linkconf->param_10fdx;
        *pVal = 1;
        break;

    case ETHER_STAT_CAP_10HDX:
        //*pVal = linkconf->param_10hdx;
        *pVal = 1;
        break;

    case ETHER_STAT_CAP_ASMPAUSE:
        *pVal = 1;
        break;

    case ETHER_STAT_CAP_PAUSE:
        *pVal = 1;
        break;

    case ETHER_STAT_CAP_AUTONEG:
        *pVal = 1;
        break;

#if (MAC_VERSION > 1)
    case ETHER_STAT_CAP_REMFAULT:
        *pVal = 1;
        break;
#endif

#if (MAC_VERSION > 1)
    case ETHER_STAT_ADV_CAP_10GFDX:
        *pVal = pUM->curcfg.lnkcfg.param_10000fdx;
        break;
#endif

    case ETHER_STAT_ADV_CAP_1000FDX:
        *pVal = pUM->curcfg.lnkcfg.param_1000fdx;
        break;

#if 0
    case ETHER_STAT_ADV_CAP_1000HDX:
        //*pVal = pUM->curcfg.lnkcfg.param_1000hdx;
        *pVal = 0;
        break;
#endif

    case ETHER_STAT_ADV_CAP_100FDX:
        *pVal = pUM->curcfg.lnkcfg.param_100fdx;
        break;

    case ETHER_STAT_ADV_CAP_100HDX:
        *pVal = pUM->curcfg.lnkcfg.param_100hdx;
        break;

    case ETHER_STAT_ADV_CAP_10FDX:
        *pVal = pUM->curcfg.lnkcfg.param_10fdx;
        break;

    case ETHER_STAT_ADV_CAP_10HDX:
        *pVal = pUM->curcfg.lnkcfg.param_10hdx;
        break;

    case ETHER_STAT_ADV_CAP_ASMPAUSE:
        *pVal = 1;
        break;

    case ETHER_STAT_ADV_CAP_PAUSE:
        *pVal = 1;
        break;

    case ETHER_STAT_ADV_CAP_AUTONEG:
        *pVal = pUM->curcfg.lnkcfg.link_autoneg;
        break;

#if (MAC_VERSION > 1)
    case ETHER_STAT_ADV_REMFAULT:
        *pVal = 1;
        break;
#endif

#if 0 /* LP caps not supported */
#if (MAC_VERSION > 1)
    case ETHER_STAT_LP_CAP_10GFDX:
        *pVal = pUM->remote.param_10000fdx;
        break;
#endif

    case ETHER_STAT_LP_CAP_1000FDX:
        *pVal = pUM->remote.param_1000fdx;
        break;

#if 0
    case ETHER_STAT_LP_CAP_1000HDX:
        //*pVal = pUM->remote.param_1000hdx;
        *pVal = 0;
        break;
#endif

    case ETHER_STAT_LP_CAP_100FDX:
        *pVal = pUM->remote.param_100fdx;
        break;

    case ETHER_STAT_LP_CAP_100HDX:
        *pVal = pUM->remote.param_100hdx;
        break;

    case ETHER_STAT_LP_CAP_10FDX:
        *pVal = pUM->remote.param_10fdx;
        break;

    case ETHER_STAT_LP_CAP_10HDX:
        *pVal = pUM->remote.param_10hdx;
        break;

#if 0
    case ETHER_STAT_LP_CAP_ASMPAUSE:
        /* XXX implement LP_ASYM_PAUSE stat */
        break;

    case ETHER_STAT_LP_CAP_PAUSE:
        /* XXX implement LP_PAUSE stat */
        break;
#endif

    case ETHER_STAT_LP_CAP_AUTONEG:
        *pVal = pUM->remote.link_autoneg;
        break;

    case ETHER_STAT_LP_REMFAULT:
        /* XXX implement LP_REMFAULT stat */
        break;
#endif /* LP caps not supported */

#if 0
    case ETHER_STAT_LINK_ASMPAUSE:
        /* XXX implement ASMPAUSE stat */
        break;

    case ETHER_STAT_LINK_PAUSE:
        /* XXX implement PAUSE stat */
        break;
#endif

    case ETHER_STAT_LINK_AUTONEG:
        *pVal = pUM->curcfg.lnkcfg.link_autoneg;
        break;

    case ETHER_STAT_LINK_DUPLEX:
	if (pUM->props.link_duplex == B_TRUE)
		*pVal = (pUM->props.link_duplex == B_TRUE) ?
		    LINK_DUPLEX_FULL : LINK_DUPLEX_HALF;
	else
		*pVal = LINK_DUPLEX_UNKNOWN;
        break;

    default:
        rc = ENOTSUP;
    }

    BNXE_LOCK_EXIT_GLD(pUM);

    return rc;
}



/*
 * This routine is called by GLD to enable device for packet reception and
 * enable interrupts.
 */
static int BnxeMacStart(void * pArg)
{
    um_device_t * pUM = (um_device_t *)pArg;

    BNXE_LOCK_ENTER_GLD(pUM);

    if (pUM->plumbed)
    {
        /* already started */
        BNXE_LOCK_EXIT_GLD(pUM);
        return EAGAIN;
    }

    /* Always report the initial link state as unknown. */
    mac_link_update(pUM->pMac, LINK_STATE_UNKNOWN);

#if 0
    /*
     * XXX Ethernet has to be started for FCoE create-port ioctl
     * to succeed.  Note that the interface is started but immediately
     * brought back down in that case.
     */
    if (!BnxeProtoSupport(pUM, LM_PROTO_SUPPORT_ETHERNET))
    {
        BnxeLogInfo(pUM, "ERROR: Ethernet unavailable on %s",
                    BnxeDevName(pUM));
        BNXE_LOCK_EXIT_GLD(pUM);
        return EAGAIN;
    }
#endif

    if (BnxeHwStartL2(pUM))
    {
        BNXE_LOCK_EXIT_GLD(pUM);
        return EIO;
    }

    atomic_swap_32(&pUM->plumbed, B_TRUE);

    mutex_enter(&bnxeLoaderMutex);
    bnxeNumPlumbed++;
    mutex_exit(&bnxeLoaderMutex);

    BNXE_LOCK_EXIT_GLD(pUM);

    return 0;
}


/*
 * This routine stops packet reception by clearing RX MASK register.  Also
 * interrupts are disabled for this device.
 */
static void BnxeMacStop(void * pArg)
{
    um_device_t * pUM = (um_device_t *)pArg;

    BNXE_LOCK_ENTER_GLD(pUM);

    if (pUM->plumbed)
    {
        atomic_swap_32(&pUM->plumbed, B_FALSE);

        BnxeHwStopL2(pUM);

        /* Report the link state back to unknown. */
        mac_link_update(pUM->pMac, LINK_STATE_UNKNOWN);

        mutex_enter(&bnxeLoaderMutex);
        bnxeNumPlumbed--;
        mutex_exit(&bnxeLoaderMutex);
    }

    BNXE_LOCK_EXIT_GLD(pUM);
}

/* (flag) TRUE = on, FALSE = off */
static int BnxeMacPromiscuous(void *    pArg,
                              boolean_t flag)
{
    um_device_t * pUM = (um_device_t *)pArg;

    BNXE_LOCK_ENTER_GLD(pUM);

    if (!pUM->plumbed)
    {
        BNXE_LOCK_EXIT_GLD(pUM);
        return EAGAIN;
    }

    if (flag)
    {
        pUM->devParams.rx_filter_mask[LM_CLI_IDX_NDIS] |=
            LM_RX_MASK_PROMISCUOUS_MODE;
    }
    else
    {
        pUM->devParams.rx_filter_mask[LM_CLI_IDX_NDIS] &=
            ~LM_RX_MASK_PROMISCUOUS_MODE;
    }

    BNXE_LOCK_ENTER_HWINIT(pUM);

    if (BnxeRxMask(pUM, LM_CLI_IDX_NDIS,
                   pUM->devParams.rx_filter_mask[LM_CLI_IDX_NDIS]) < 0)
    {
        BNXE_LOCK_EXIT_HWINIT(pUM);
        BNXE_LOCK_EXIT_GLD(pUM);
        return ECANCELED;
    }

    BNXE_LOCK_EXIT_HWINIT(pUM);

    BNXE_LOCK_EXIT_GLD(pUM);

    return 0;
}


/*
 * This function is used to enable or disable multicast packet reception for
 * particular multicast addresses.
 * (flag) TRUE = add, FALSE = remove
 */
static int BnxeMacMulticast(void *          pArg,
                            boolean_t       flag,
                            const uint8_t * pMcastAddr)
{
    um_device_t * pUM = (um_device_t *)pArg;
    int rc;

    BNXE_LOCK_ENTER_GLD(pUM);

    if (!pUM->plumbed)
    {
        BNXE_LOCK_EXIT_GLD(pUM);
        return EAGAIN;
    }

    BNXE_LOCK_ENTER_HWINIT(pUM);
    rc = BnxeMulticast(pUM, LM_CLI_IDX_NDIS, flag, pMcastAddr, B_TRUE);
    BNXE_LOCK_EXIT_HWINIT(pUM);

    BNXE_LOCK_EXIT_GLD(pUM);

    return rc;
}


#ifdef BNXE_RINGS

static int BnxeRxRingGroupAddMac(void *          groupHandle,
                                 const uint8_t * pMacAddr,
				 uint64_t flags)
{
    _NOTE(ARGUNUSED(flags))

    RxQueueGroup * pRxQGroup = (RxQueueGroup *)groupHandle;
    um_device_t *  pUM       = (um_device_t *)pRxQGroup->pUM;
    //u32_t          idx       = pRxQGroup->idx;
    int rc;

    BNXE_LOCK_ENTER_GLD(pUM);

    if (!pUM->plumbed)
    {
        BNXE_LOCK_EXIT_GLD(pUM);
        return ECANCELED;
    }

    /* Validate MAC address */
    if (IS_ETH_MULTICAST(pMacAddr))
    {
        BnxeLogWarn(pUM, "Cannot program a mcast/bcast address as a MAC Address.");
        BNXE_LOCK_EXIT_GLD(pUM);
        return EINVAL;
    }

    if (pUM->ucastTableLen == LM_MAX_UC_TABLE_SIZE)
    {
        BNXE_LOCK_EXIT_GLD(pUM);
        return ENOMEM;
    }

    BNXE_LOCK_ENTER_HWINIT(pUM);

    COPY_ETH_ADDRESS(pMacAddr, pUM->lm_dev.params.mac_addr);

    rc = BnxeMacAddress(pUM, LM_CLI_IDX_NDIS, B_TRUE,
                        pUM->lm_dev.params.mac_addr);

    BNXE_LOCK_EXIT_HWINIT(pUM);

    if (rc < 0)
    {
        BNXE_LOCK_EXIT_GLD(pUM);
        return ECANCELED;
    }

    pUM->ucastTableLen++;

    BNXE_LOCK_EXIT_GLD(pUM);
    return 0;
}


static int BnxeRxRingGroupRemMac(void *          groupHandle,
                                 const uint8_t * pMacAddr)
{
    RxQueueGroup * pRxQGroup = (RxQueueGroup *)groupHandle;
    um_device_t *  pUM       = (um_device_t *)pRxQGroup->pUM;
    //u32_t          idx       = pRxQGroup->idx;
    int rc;

    BNXE_LOCK_ENTER_GLD(pUM);

    if (!pUM->plumbed)
    {
        BNXE_LOCK_EXIT_GLD(pUM);
        return ECANCELED;
    }

    if (pUM->ucastTableLen == 0)
    {
        BNXE_LOCK_EXIT_GLD(pUM);
        return EINVAL;
    }

    BNXE_LOCK_ENTER_HWINIT(pUM);

    /* XXX verify pMacAddr equals lm_dev.params.mac_addr */
    memset(pUM->lm_dev.params.mac_addr, 0, sizeof(pUM->lm_dev.params.mac_addr));

    rc = BnxeMacAddress(pUM, LM_CLI_IDX_NDIS, B_FALSE,
                        pUM->lm_dev.params.mac_addr);

    BNXE_LOCK_EXIT_HWINIT(pUM);

    if (rc < 0)
    {
        BNXE_LOCK_EXIT_GLD(pUM);
        return ECANCELED;
    }

    pUM->ucastTableLen--;

    BNXE_LOCK_EXIT_GLD(pUM);
    return 0;
}


static mblk_t * BnxeTxRingSend(void *   ringHandle,
                               mblk_t * pMblk)
{
    TxQueue *     pTxQ  = (TxQueue *)ringHandle;
    um_device_t * pUM   = (um_device_t *)pTxQ->pUM;
    u32_t         idx   = pTxQ->idx;
    mblk_t *      pNextMblk;
    int rc;

    while (pMblk)
    {
        pNextMblk = pMblk->b_next;
        pMblk->b_next = NULL;

        rc = BnxeTxSendMblk(pUM, idx, pMblk, 0, 0);

        if (rc == BNXE_TX_GOODXMIT)
        {
            pMblk = pNextMblk;
            continue;
        }
        else if (rc == BNXE_TX_DEFERPKT)
        {
            pMblk = pNextMblk;
        }
        else
        {
            pMblk->b_next = pNextMblk;
        }

        break;
    }

    return pMblk;
}

#endif /* BNXE_RINGS */


static int BnxeMacUnicast(void *          pArg,
                          const uint8_t * pMacAddr)
{
    um_device_t * pUM = (um_device_t *)pArg;
    int rc;

    BNXE_LOCK_ENTER_GLD(pUM);

    if (!pUM->plumbed)
    {
        memcpy(pUM->gldMac, pMacAddr, ETHERNET_ADDRESS_SIZE);
        BNXE_LOCK_EXIT_GLD(pUM);
        return 0;
    }

    /* Validate MAC address */
    if (IS_ETH_MULTICAST(pMacAddr))
    {
        BnxeLogWarn(pUM, "Cannot program a mcast/bcast address as a MAC Address.");
        BNXE_LOCK_EXIT_GLD(pUM);
        return EINVAL;
    }

    BNXE_LOCK_ENTER_HWINIT(pUM);

    COPY_ETH_ADDRESS(pMacAddr, pUM->lm_dev.params.mac_addr);

    rc = BnxeMacAddress(pUM, LM_CLI_IDX_NDIS, B_TRUE,
                        pUM->lm_dev.params.mac_addr);

    BNXE_LOCK_EXIT_HWINIT(pUM);

    if (rc < 0)
    {
        BNXE_LOCK_EXIT_GLD(pUM);
        return EAGAIN;
    }

    BNXE_LOCK_EXIT_GLD(pUM);
    return 0;
}


static mblk_t * BnxeMacTx(void *   pArg,
                          mblk_t * pMblk)
{
    um_device_t * pUM = (um_device_t *)pArg;
    mblk_t *      pNextMblk;
    int ring, rc;

    BNXE_LOCK_ENTER_GLDTX(pUM, RW_READER);

    if (!pUM->plumbed)
    {
        freemsgchain(pMblk);
        BNXE_LOCK_EXIT_GLDTX(pUM);

        return NULL;
    }

    ring = BnxeRouteTxRing(pUM, pMblk);

    while (pMblk)
    {
        pNextMblk = pMblk->b_next;
        pMblk->b_next = NULL;

        //rc = BnxeTxSendMblk(pUM, NDIS_CID(&pUM->lm_dev), pMblk, 0, 0);
        rc = BnxeTxSendMblk(pUM, ring, pMblk, 0, 0);

        if (rc == BNXE_TX_GOODXMIT)
        {
            pMblk = pNextMblk;
            continue;
        }
        else if (rc == BNXE_TX_DEFERPKT)
        {
            pMblk = pNextMblk;
        }
        else
        {
            pMblk->b_next = pNextMblk;
        }

        break;
    }

    BNXE_LOCK_EXIT_GLDTX(pUM);

    return pMblk;
}


#ifdef MC_RESOURCES

static void BnxeBlank(void * pArg,
                      time_t tick_cnt,
                      uint_t pkt_cnt)
{
    um_device_t * pUM = (um_device_t *)pArg;

    if (!pUM->plumbed)
    {
        return;
    }

    /* XXX
     * Need to dynamically reconfigure the hw with new interrupt
     * coalescing params...
     */
}


static void BnxeMacResources(void * pArg)
{
    um_device_t * pUM = (um_device_t *)pArg;
    mac_rx_fifo_t mrf;
    int idx;

    mrf.mrf_type              = MAC_RX_FIFO;
    mrf.mrf_blank             = BnxeBlank;
    mrf.mrf_arg               = (void *)pUM;
    mrf.mrf_normal_blank_time = 25;
    mrf.mrf_normal_pkt_count  = 8;

    LM_FOREACH_RSS_IDX(&pUM->lm_dev, idx)
    {
        pUM->macRxResourceHandles[idx] =
            mac_resource_add(pUM->pMac, (mac_resource_t *)&mrf);
    }
}

#endif /* MC_RESOURCES */


static void BnxeMacIoctl(void *    pArg,
                         queue_t * pQ,
                         mblk_t *  pMblk)
{
    um_device_t * pUM = (um_device_t *)pArg;
    struct iocblk * pIoctl;
    int rc;

    if ((pQ == NULL) || (pMblk == NULL))
    {
        return;
    }

    if (pMblk->b_datap->db_type != M_IOCTL)
    {
        miocnak(pQ, pMblk, 0, EINVAL);
        return;
    }

    pIoctl = (struct iocblk *)pMblk->b_rptr;

    BNXE_LOCK_ENTER_GLD(pUM);

    switch (pIoctl->ioc_cmd)
    {
    case GIOCBNXELLDP:

        if ((pIoctl->ioc_count != sizeof(b10_lldp_params_get_t)) ||
            (pMblk->b_cont == NULL) || (pMblk->b_cont->b_rptr == NULL) ||
            (miocpullup(pMblk, sizeof(b10_lldp_params_get_t)) < 0))
        {
            miocnak(pQ, pMblk, 0, EINVAL);
            break;
        }

        if (((b10_lldp_params_get_t *)pMblk->b_cont->b_rptr)->ver_num !=
            LLDP_PARAMS_VER_NUM)
        {
            miocnak(pQ, pMblk, 0, EINVAL);
            break;
        }

        if (lm_dcbx_lldp_read_params(&pUM->lm_dev,
                         (b10_lldp_params_get_t *)pMblk->b_cont->b_rptr) !=
            LM_STATUS_SUCCESS)
        {
            miocnak(pQ, pMblk, 0, EINVAL);
            break;
        }

        miocack(pQ, pMblk, pIoctl->ioc_count, 0);
        break;

    case GIOCBNXEDCBX:

        if ((pIoctl->ioc_count != sizeof(b10_dcbx_params_get_t)) ||
            (pMblk->b_cont == NULL) || (pMblk->b_cont->b_rptr == NULL) ||
            (miocpullup(pMblk, sizeof(b10_dcbx_params_get_t)) < 0))
        {
            miocnak(pQ, pMblk, 0, EINVAL);
            break;
        }

        if (((b10_dcbx_params_get_t *)pMblk->b_cont->b_rptr)->ver_num !=
            DCBX_PARAMS_VER_NUM)
        {
            miocnak(pQ, pMblk, 0, EINVAL);
            break;
        }

        if (lm_dcbx_read_params(&pUM->lm_dev,
                         (b10_dcbx_params_get_t *)pMblk->b_cont->b_rptr) !=
            LM_STATUS_SUCCESS)
        {
            miocnak(pQ, pMblk, 0, EINVAL);
            break;
        }

        miocack(pQ, pMblk, pIoctl->ioc_count, 0);
        break;

    case SIOCBNXEDCBX:

        /* XXX */
        miocnak(pQ, pMblk, 0, EINVAL);
        break;

    case BNXE_FCOEIO_CMD:

#ifdef BNXE_FCOE_SUPPORT

        if (pIoctl->ioc_count < sizeof(bnxe_fcoeio_t))
        {
            BnxeLogWarn(pUM, "BNXE_FCOEIO_CMD: bad size for bnxe_fcoeio_t (%d), expected (%d)",
                        pIoctl->ioc_count, sizeof(bnxe_fcoeio_t));
            miocnak(pQ, pMblk, 0, EINVAL);
            break;
        }

        if ((pMblk->b_cont == NULL) || (pMblk->b_cont->b_rptr == NULL) ||
            (miocpullup(pMblk, sizeof(bnxe_fcoeio_t)) < 0))
        {
            BnxeLogWarn(pUM, "BNXE_FCOEIO_CMD: invalid bnxe_fcoeio_t ioctl");
            miocnak(pQ, pMblk, 0, EINVAL);
            break;
        }

        if (((bnxe_fcoeio_t *)pMblk->b_cont->b_rptr)->fcoeio_version !=
            BNXE_FCOEIO_VERSION)
        {
            BnxeLogWarn(pUM, "BNXE_FCOEIO_CMD: bad version for bnxe_fcoeio_t (%d), expected (%d)",
                        ((bnxe_fcoeio_t *)pMblk->b_cont->b_rptr)->fcoeio_version,
                        BNXE_FCOEIO_VERSION);
            miocnak(pQ, pMblk, 0, EINVAL);
            break;
        }

        rc = BnxeFcoeAdmIoctl(pUM, (bnxe_fcoeio_t *)pMblk->b_cont->b_rptr);
        if (rc != 0)
        {
            miocnak(pQ, pMblk, 0, rc); /* rc = errno */
            break;
        }

        miocack(pQ, pMblk, pIoctl->ioc_count, 0);
        break;

#else /* not BNXE_FCOE_SUPPORT */

        BnxeLogWarn(pUM, "BNXE_FCOEIO_CMD: FCoE not supported");
        miocnak(pQ, pMblk, 0, EINVAL);
        break;

#endif /* BNXE_FCOE_SUPPORT */

    default:

        miocnak(pQ, pMblk, 0, EINVAL);
        break;
    }

    BNXE_LOCK_EXIT_GLD(pUM);
}


#ifdef BNXE_RINGS

static mblk_t * BnxeRxRingPoll(void * ringHandle,
                               int    numBytes,
			       int    numPkts)
{
    _NOTE(ARGUNUSED(numPkts))

    RxQueue *     pRxQ  = (RxQueue *)ringHandle;
    um_device_t * pUM   = (um_device_t *)pRxQ->pUM;
    u32_t         idx   = pRxQ->idx;
    mblk_t *      pMblk = NULL;
    u32_t         pktsRxed = 0;
    u32_t         pktsTxed = 0;

    if (numBytes <= 0)
    {
        return NULL;
    }

    if (pRxQ->inPollMode == B_FALSE)
    {
        BnxeLogWarn(pUM, "Polling on ring %d when NOT is poll mode!", idx);
        return NULL;
    }

    pRxQ->pollCnt++;

    BNXE_LOCK_ENTER_INTR(pUM, idx);

    BnxePollRxRing(pUM, idx, &pktsRxed, &pktsTxed);

    if (pktsTxed)
    {
        BnxeTxRingProcess(pUM, idx);
    }

    if (pktsRxed)
    {
        pMblk = BnxeRxRingRecv(pUM, idx, TRUE, numBytes);
    }

    BNXE_LOCK_EXIT_INTR(pUM, idx);

    return pMblk;
}


static int BnxeRxRingStart(mac_ring_driver_t ringHandle
#ifdef __Solaris_11
                         , uint64_t          genNumber
#endif
                           )
{
    RxQueue *     pRxQ = (RxQueue *)ringHandle;
    um_device_t * pUM  = (um_device_t *)pRxQ->pUM;
    u32_t         idx  = pRxQ->idx;

    BnxeLogDbg(pUM, "Starting Rx Ring %d", idx);

    BNXE_LOCK_ENTER_RX(pUM, idx);
#ifdef __Solaris_11
    pRxQ->genNumber      = genNumber;
#endif
    pRxQ->inPollMode     = B_FALSE;
    pRxQ->intrDisableCnt = 0;
    pRxQ->intrEnableCnt  = 0;
    pRxQ->pollCnt        = 0;
    BNXE_LOCK_EXIT_RX(pUM, idx);

    return 0;
}


static int BnxeRxRingIntrEnable(mac_ring_driver_t intrHandle)
{
    RxQueue *     pRxQ = (RxQueue *)intrHandle;
    um_device_t * pUM  = (um_device_t *)pRxQ->pUM;

    BnxeLogDbg(pUM, "Enabling Interrupt for Rx Ring %d", pRxQ->idx);
    BnxeIntrIguSbEnable(pUM, pRxQ->idx, B_FALSE);

    return 0;
}


static int BnxeRxRingIntrDisable(mac_ring_driver_t intrHandle)
{
    RxQueue *     pRxQ = (RxQueue *)intrHandle;
    um_device_t * pUM  = (um_device_t *)pRxQ->pUM;

    BnxeLogDbg(pUM, "Disabling Interrupt for Rx Ring %d", pRxQ->idx);

    /* XXX
     * polling not yet allowed on LM_NON_RSS_SB when overlapped with FCoE
     */
    if (CLIENT_BOUND(pUM, LM_CLI_IDX_FCOE) &&
        (pUM->rssIntr.intrCount == LM_MAX_RSS_CHAINS(&pUM->lm_dev)))
    {
        return -1;
    }

    BnxeIntrIguSbDisable(pUM, pRxQ->idx, B_FALSE);

    return 0;
}

/* ARGUSED */
int BnxeRxRingStat(mac_ring_driver_t rh, uint_t stat, uint64_t *val)
{
	*val = 0;

	return (ENOTSUP);
}

/* ARGUSED */
int BnxeTxRingStat(mac_ring_driver_t rh, uint_t stat, uint64_t *val)
{
	*val = 0;
	
	return (ENOTSUP);
}

/* callback function for MAC layer to register rings */
static void BnxeFillRing(void *            arg,
                         mac_ring_type_t   ringType,
                         const int         ringGroupIndex,
                         const int         ringIndex,
                         mac_ring_info_t * pRingInfo,
                         mac_ring_handle_t ringHandle)
{
    um_device_t * pUM = (um_device_t *)arg;
    RxQueue *     pRxQ;
    TxQueue *     pTxQ;

    switch (ringType)
    {
    case MAC_RING_TYPE_RX:

        BnxeLogInfo(pUM, "Initializing Rx Ring %d", ringIndex);

        ASSERT(ringGroupIndex == 0);
        ASSERT(ringIndex < pUM->devParams.numRings);

        pRxQ = &pUM->rxq[ringIndex];
        pRxQ->ringHandle = ringHandle;

        pRingInfo->mri_driver = (mac_ring_driver_t)pRxQ;
        pRingInfo->mri_start  = BnxeRxRingStart;
        pRingInfo->mri_stop   = NULL;
        pRingInfo->mri_poll   = BnxeRxRingPoll;
	pRingInfo->mri_stat   = BnxeRxRingStat;

        pRingInfo->mri_intr.mi_enable  = (mac_intr_enable_t)BnxeRxRingIntrEnable;
        pRingInfo->mri_intr.mi_disable = (mac_intr_disable_t)BnxeRxRingIntrDisable;

#if 0
        /* support cpu re-targeting... */
        if (pUM->intrType & DDI_INTR_TYPE_MSIX)
        {
            pRingInfo->mri_intr.mi_ddi_handle =
                pUM->rssIntr.pIntrHandleBlock[ringIndex];
        }
#endif

        break;

    case MAC_RING_TYPE_TX:

        BnxeLogInfo(pUM, "Initializing Tx Ring %d", ringIndex);

        ASSERT(ringGroupIndex == -1);
        ASSERT(ringIndex < pUM->devParams.numRings);

        pTxQ = &pUM->txq[ringIndex];
        pTxQ->ringHandle = ringHandle;

        pRingInfo->mri_driver = (mac_ring_driver_t)pTxQ;
        pRingInfo->mri_start  = NULL;
        pRingInfo->mri_stop   = NULL;
        pRingInfo->mri_tx     = (mac_ring_send_t)BnxeTxRingSend;
	pRingInfo->mri_stat   = BnxeTxRingStat;

        break;

    default:
        break;
    }
}


/* callback function for MAC layer to register groups */
static void BnxeFillGroup(void *             arg,
                          mac_ring_type_t    ringType,
                          const int          ringGroupIndex,
                          mac_group_info_t * pGroupInfo,
                          mac_group_handle_t groupHandle)
{
    um_device_t *  pUM = (um_device_t *)arg;
    RxQueueGroup * pRxQGroup;

    switch (ringType)
    {
    case MAC_RING_TYPE_RX:

        BnxeLogInfo(pUM, "Initializing Rx Group %d", ringGroupIndex);

        pRxQGroup = &pUM->rxqGroup[ringGroupIndex];
        pRxQGroup->groupHandle = groupHandle;

        pGroupInfo->mgi_driver = (mac_group_driver_t)pRxQGroup;
        pGroupInfo->mgi_start  = NULL;
        pGroupInfo->mgi_stop   = NULL;
        pGroupInfo->mgi_addmac = BnxeRxRingGroupAddMac;
        pGroupInfo->mgi_remmac = BnxeRxRingGroupRemMac;
        pGroupInfo->mgi_count  = (pUM->devParams.numRings /
                                  USER_OPTION_RX_RING_GROUPS_DEFAULT);
	pGroupInfo->mgi_flags  = MAC_GROUP_DEFAULT;

        break;

    case MAC_RING_TYPE_TX:
    default:
        break;
    }
}

#endif /* BNXE_RINGS */


static boolean_t BnxeMacGetCapability(void *      pArg,
                                      mac_capab_t capability,
                                      void *      pCapabilityData)
{
    um_device_t * pUM = (um_device_t *)pArg;
    mac_capab_lso_t *   pCapLSO;
    mac_capab_rings_t * pCapRings;

    switch (capability)
    {
    case MAC_CAPAB_HCKSUM:

        *((u32_t *)pCapabilityData) = 0;

        if (pUM->devParams.enabled_oflds &
            (LM_OFFLOAD_TX_IP_CKSUM | LM_OFFLOAD_RX_IP_CKSUM))
        {
            *((u32_t *)pCapabilityData) |= HCKSUM_IPHDRCKSUM;
        }

        if (pUM->devParams.enabled_oflds &
            (LM_OFFLOAD_TX_TCP_CKSUM | LM_OFFLOAD_TX_UDP_CKSUM |
             LM_OFFLOAD_RX_TCP_CKSUM | LM_OFFLOAD_RX_UDP_CKSUM))
        {
            *((u32_t *)pCapabilityData) |= HCKSUM_INET_PARTIAL;
        }

        break;

    case MAC_CAPAB_LSO:

        pCapLSO = pCapabilityData;

        if (pUM->devParams.lsoEnable)
        {
            pCapLSO->lso_flags                  = LSO_TX_BASIC_TCP_IPV4;
            pCapLSO->lso_basic_tcp_ipv4.lso_max = BNXE_LSO_MAXLEN;
            break;
        }

        return B_FALSE;

#ifdef BNXE_RINGS

    case MAC_CAPAB_RINGS:

        if (!pUM->devParams.numRings)
        {
            return B_FALSE;
        }

        pCapRings = pCapabilityData;

        pCapRings->mr_version    = MAC_RINGS_VERSION_1;
        pCapRings->mr_rget       = BnxeFillRing;
        pCapRings->mr_gaddring   = NULL;
        pCapRings->mr_gremring   = NULL;

        switch (pCapRings->mr_type)
        {
        case MAC_RING_TYPE_RX:
            pCapRings->mr_group_type = MAC_GROUP_TYPE_STATIC;
            pCapRings->mr_rnum       = pUM->devParams.numRings;
            pCapRings->mr_gnum       = USER_OPTION_RX_RING_GROUPS_DEFAULT;
            pCapRings->mr_gget       = BnxeFillGroup;
            break;

        case MAC_RING_TYPE_TX:

            pCapRings->mr_group_type = MAC_GROUP_TYPE_STATIC;
            pCapRings->mr_rnum       = pUM->devParams.numRings;
            pCapRings->mr_gnum       = 1;
            pCapRings->mr_gget       = NULL;
            break;

        default:

            return B_FALSE;
        }

        break;

#endif /* BNXE_RINGS */

#ifndef __Solaris_11

    case MAC_CAPAB_POLL:

        /*
         * There's nothing for us to fill in, simply returning B_TRUE stating
         * that we support polling is sufficient.
         */
        break;

#endif /* not __Solaris_11 */

    default:

        return B_FALSE;
    }

    return B_TRUE;
}


#ifdef MC_SETPROP

static int BnxeSetPrivateProperty(um_device_t * pUM,
                                  const char *  pr_name,
                                  uint_t        pr_valsize,
                                  const void *  pr_val)
{
    int err = 0;
    long result;

    if (strcmp(pr_name, "_en_2500fdx_cap") == 0)
    {
        if (ddi_strtol(pr_val, (char **)NULL, 0, &result))
        {
            return EINVAL;
        }

        if ((result > 1) || (result < 0))
        {
            return EINVAL;
        }

        if (pUM->hwinit.lnkcfg.param_2500fdx == (uint32_t)result)
        {
            return 0;
        }

        pUM->hwinit.lnkcfg.param_2500fdx = (uint32_t)result;
        pUM->curcfg.lnkcfg.param_2500fdx = (uint32_t)result;
        BnxeUpdatePhy(pUM, B_TRUE);
    }
    else if (strcmp(pr_name, "_en_txpause_cap") == 0)
    {
        if (ddi_strtol(pr_val, (char **)NULL, 0, &result))
        {
            return EINVAL;
        }

        if ((result > 1) || (result < 0))
        {
            return EINVAL;
        }

        if (pUM->hwinit.lnkcfg.param_txpause == (uint32_t)result)
        {
            return 0;
        }

        pUM->hwinit.lnkcfg.param_txpause = (uint32_t)result;
        pUM->curcfg.lnkcfg.param_txpause = (uint32_t)result;
        BnxeUpdatePhy(pUM, B_TRUE);
    }
    else if (strcmp(pr_name, "_en_rxpause_cap") == 0)
    {
        if (ddi_strtol(pr_val, (char **)NULL, 0, &result))
        {
            return EINVAL;
        }

        if ((result > 1) || (result < 0))
        {
            return EINVAL;
        }

        if (pUM->hwinit.lnkcfg.param_rxpause == (uint32_t)result)
        {
            return 0;
        }

        pUM->hwinit.lnkcfg.param_rxpause = (uint32_t)result;
        pUM->curcfg.lnkcfg.param_rxpause = (uint32_t)result;
        BnxeUpdatePhy(pUM, B_TRUE);
    }
    else if (strcmp(pr_name, "_autoneg_flow") == 0)
    {
        if (ddi_strtol(pr_val, (char **)NULL, 0, &result))
        {
            return EINVAL;
        }

        if ((result > 1) || (result < 0))
        {
            return EINVAL;
        }

        if (pUM->hwinit.flow_autoneg == (uint32_t)result)
        {
            return 0;
        }

        pUM->hwinit.flow_autoneg = (uint32_t)result;
        pUM->curcfg.flow_autoneg = (uint32_t)result;
        BnxeUpdatePhy(pUM, B_TRUE);
    }
    else if (strcmp(pr_name, "_checksum") == 0)
    {
        if (pUM->plumbed)
        {
            return EBUSY;
        }

        if (ddi_strtol(pr_val, (char **)NULL, 0, &result))
        {
            return EINVAL;
        }

        switch (result)
        {
        case USER_OPTION_CKSUM_NONE:

            pUM->devParams.enabled_oflds = LM_OFFLOAD_NONE;
            break;

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

        default:

            return EINVAL;
        }

        pUM->devParams.checksum = (uint32_t)result;
    }
    else if (strcmp(pr_name, "_tx_ring_policy") == 0)
    {
        if (ddi_strtol(pr_val, (char **)NULL, 0, &result))
        {
            return EINVAL;
        }

        switch (result)
        {
        case BNXE_ROUTE_RING_NONE:
        case BNXE_ROUTE_RING_TCPUDP:
        case BNXE_ROUTE_RING_DEST_MAC:
        case BNXE_ROUTE_RING_MSG_PRIO:

            break;

        default:

            return EINVAL;
        }

        pUM->devParams.routeTxRingPolicy = (uint32_t)result;
    }
    else if (strcmp(pr_name, "_num_rings") == 0)
    {
        if (pUM->plumbed)
        {
            return EBUSY;
        }

        if (ddi_strtol(pr_val, (char **)NULL, 0, &result))
        {
            return EINVAL;
        }

        if ((result < USER_OPTION_NUM_RINGS_MIN) ||
            (result > USER_OPTION_NUM_RINGS_MAX))
        {
            return EINVAL;
        }

        pUM->devParams.numRings = (uint32_t)result;
    }
    else if (strcmp(pr_name, "_rx_descs") == 0)
    {
        if (pUM->plumbed)
        {
            return EBUSY;
        }

        if (ddi_strtol(pr_val, (char **)NULL, 0, &result))
        {
            return EINVAL;
        }

        if ((result < USER_OPTION_BDS_MIN) || (result > USER_OPTION_BDS_MAX))
        {
            return EINVAL;
        }

        pUM->devParams.numRxDesc[LM_CLI_IDX_NDIS] = (uint32_t)result;
    }
    else if (strcmp(pr_name, "_rx_free_reclaim") == 0)
    {
        if (ddi_strtol(pr_val, (char **)NULL, 0, &result))
        {
            return EINVAL;
        }

        if ((result < USER_OPTION_BDS_MIN) || (result > USER_OPTION_BDS_MAX))
        {
            return EINVAL;
        }

        pUM->devParams.maxRxFree = (uint32_t)result;
    }
    else if (strcmp(pr_name, "_tx_descs") == 0)
    {
        if (pUM->plumbed)
        {
            return EBUSY;
        }

        if (ddi_strtol(pr_val, (char **)NULL, 0, &result))
        {
            return EINVAL;
        }

        if ((result < USER_OPTION_BDS_MIN) || (result > USER_OPTION_BDS_MAX))
        {
            return EINVAL;
        }

        pUM->devParams.numTxDesc[LM_CLI_IDX_NDIS] = (uint32_t)result;
    }
    else if (strcmp(pr_name, "_tx_free_reclaim") == 0)
    {
        if (ddi_strtol(pr_val, (char **)NULL, 0, &result))
        {
            return EINVAL;
        }

        if ((result < USER_OPTION_BDS_MIN) || (result > USER_OPTION_BDS_MAX))
        {
            return EINVAL;
        }

        pUM->devParams.maxTxFree = (uint32_t)result;
    }
    else if (strcmp(pr_name, "_rx_copy_threshold") == 0)
    {
        if (ddi_strtol(pr_val, (char **)NULL, 0, &result))
        {
            return EINVAL;
        }

        pUM->devParams.rxCopyThreshold = (uint32_t)result;
    }
    else if (strcmp(pr_name, "_tx_copy_threshold") == 0)
    {
        if (ddi_strtol(pr_val, (char **)NULL, 0, &result))
        {
            return EINVAL;
        }

        pUM->devParams.txCopyThreshold = (uint32_t)result;
    }
    else if (strcmp(pr_name, "_interrupt_coalesce") == 0)
    {
        if (pUM->plumbed)
        {
            return EBUSY;
        }

        if (ddi_strtol(pr_val, (char **)NULL, 0, &result))
        {
            return EINVAL;
        }

        if ((result > 1) || (result < 0))
        {
            return EINVAL;
        }

        pUM->devParams.intrCoalesce = (uint32_t)result;
    }
    else if (strcmp(pr_name, "_rx_interrupt_coalesce_usec") == 0)
    {
        if (pUM->plumbed)
        {
            return EBUSY;
        }

        if (ddi_strtol(pr_val, (char **)NULL, 0, &result))
        {
            return EINVAL;
        }

        if ((result < USER_OPTION_INTR_COALESCE_MIN) ||
            (result < USER_OPTION_INTR_COALESCE_MAX))
        {
            return EINVAL;
        }

        pUM->devParams.intrRxPerSec = (uint32_t)(1000000 / result);
    }
    else if (strcmp(pr_name, "_tx_interrupt_coalesce_usec") == 0)
    {
        if (pUM->plumbed)
        {
            return EBUSY;
        }

        if (ddi_strtol(pr_val, (char **)NULL, 0, &result))
        {
            return EINVAL;
        }

        if ((result < USER_OPTION_INTR_COALESCE_MIN) ||
            (result < USER_OPTION_INTR_COALESCE_MAX))
        {
            return EINVAL;
        }

        pUM->devParams.intrTxPerSec = (uint32_t)(1000000 / result);
    }
    else if (strcmp(pr_name, "_disable_msix") == 0)
    {
        if (pUM->plumbed)
        {
            return EBUSY;
        }

        if (ddi_strtol(pr_val, (char **)NULL, 0, &result))
        {
            return EINVAL;
        }

        if ((result > 1) || (result < 0))
        {
            return EINVAL;
        }

        pUM->devParams.disableMsix = (uint32_t)result;
    }
    else if (strcmp(pr_name, "_l2_fw_flow_ctrl") == 0)
    {
        if (pUM->plumbed)
        {
            return EBUSY;
        }

        if (ddi_strtol(pr_val, (char **)NULL, 0, &result))
        {
            return EINVAL;
        }

        if ((result > 1) || (result < 0))
        {
            return EINVAL;
        }

        pUM->devParams.l2_fw_flow_ctrl = (uint32_t)result;
    }
    else if (strcmp(pr_name, "_lso_enable") == 0)
    {
        if (pUM->plumbed)
        {
            return EBUSY;
        }

        if (ddi_strtol(pr_val, (char **)NULL, 0, &result))
        {
            return EINVAL;
        }

        if ((result > 1) || (result < 0))
        {
            return EINVAL;
        }

        pUM->devParams.lsoEnable = (uint32_t)result;
    }
    else if (strcmp(pr_name, "_log_enable") == 0)
    {
        if (pUM->plumbed)
        {
            return EBUSY;
        }

        if (ddi_strtol(pr_val, (char **)NULL, 0, &result))
        {
            return EINVAL;
        }

        if ((result > 1) || (result < 0))
        {
            return EINVAL;
        }

        pUM->devParams.logEnable = (uint32_t)result;
    }
#ifdef BNXE_FCOE_SUPPORT
    else if (strcmp(pr_name, "_fcoe_enable") == 0)
    {
        if (pUM->plumbed)
        {
            return EBUSY;
        }

        if (ddi_strtol(pr_val, (char **)NULL, 0, &result))
        {
            return EINVAL;
        }

        if ((result > 1) || (result < 0))
        {
            return EINVAL;
        }

        pUM->devParams.fcoeEnable = (uint32_t)result;
    }
#endif
    else
    {
        err = ENOTSUP;
    }

    return err;
}


static int BnxeMacSetProperty(void *        barg,
                              const char *  pr_name,
                              mac_prop_id_t pr_num,
                              uint_t        pr_valsize,
                              const void *  pr_val)
{
    um_device_t *   pUM = barg;
    boolean_t       reprogram = B_FALSE;
    boolean_t       rxpause;
    boolean_t       txpause;
    uint32_t        mtu;
    link_flowctrl_t fl;
    int err = 0;

    BNXE_LOCK_ENTER_GLD(pUM);

    switch (pr_num)
    {
    /* read-only props */
    case MAC_PROP_ADV_10GFDX_CAP:
    case MAC_PROP_ADV_1000FDX_CAP:
    case MAC_PROP_EN_1000HDX_CAP:
    case MAC_PROP_ADV_1000HDX_CAP:
    case MAC_PROP_ADV_100FDX_CAP:
    case MAC_PROP_ADV_100HDX_CAP:
    case MAC_PROP_ADV_10FDX_CAP:
    case MAC_PROP_ADV_10HDX_CAP:
    case MAC_PROP_ADV_100T4_CAP:
    case MAC_PROP_EN_100T4_CAP:
    case MAC_PROP_STATUS:
    case MAC_PROP_SPEED:
    case MAC_PROP_DUPLEX:
    default:

        err = ENOTSUP;
        break;

    case MAC_PROP_EN_10GFDX_CAP:

        if (pUM->hwinit.lnkcfg.param_10000fdx == *(uint8_t *)pr_val)
        {
            break;
        }

        pUM->hwinit.lnkcfg.param_10000fdx = *(uint8_t *)pr_val;
        pUM->curcfg.lnkcfg.param_10000fdx = *(uint8_t *)pr_val;
        reprogram = B_TRUE;
        break;

    case MAC_PROP_EN_1000FDX_CAP:

        if (pUM->hwinit.lnkcfg.param_1000fdx == *(uint8_t *)pr_val)
        {
            break;
        }

        pUM->hwinit.lnkcfg.param_1000fdx = *(uint8_t *)pr_val;
        pUM->curcfg.lnkcfg.param_1000fdx = *(uint8_t *)pr_val;
        reprogram = B_TRUE;
        break;

    case MAC_PROP_EN_100FDX_CAP:

        if (pUM->hwinit.lnkcfg.param_100fdx == *(uint8_t *)pr_val)
        {
            break;
        }

        pUM->hwinit.lnkcfg.param_100fdx = *(uint8_t *)pr_val;
        pUM->curcfg.lnkcfg.param_100fdx = *(uint8_t *)pr_val;
        reprogram = B_TRUE;
        break;

    case MAC_PROP_EN_100HDX_CAP:

        if (pUM->hwinit.lnkcfg.param_100hdx == *(uint8_t *)pr_val)
        {
            break;
        }

        pUM->hwinit.lnkcfg.param_100hdx = *(uint8_t *)pr_val;
        pUM->curcfg.lnkcfg.param_100hdx = *(uint8_t *)pr_val;
        reprogram = B_TRUE;
        break;

    case MAC_PROP_EN_10FDX_CAP:

        if (pUM->hwinit.lnkcfg.param_10fdx == *(uint8_t *)pr_val)
        {
            break;
        }

        pUM->hwinit.lnkcfg.param_10fdx = *(uint8_t *)pr_val;
        pUM->curcfg.lnkcfg.param_10fdx = *(uint8_t *)pr_val;
        reprogram = B_TRUE;
        break;

    case MAC_PROP_EN_10HDX_CAP:

        if (pUM->hwinit.lnkcfg.param_10hdx == *(uint8_t *)pr_val)
        {
            break;
        }

        pUM->hwinit.lnkcfg.param_10hdx = *(uint8_t *)pr_val;
        pUM->curcfg.lnkcfg.param_10hdx = *(uint8_t *)pr_val;
        reprogram = B_TRUE;
        break;

    case MAC_PROP_AUTONEG:

        if (pUM->hwinit.lnkcfg.link_autoneg == *(uint8_t *)pr_val)
        {
            break;
        }

        pUM->hwinit.lnkcfg.link_autoneg = *(uint8_t *)pr_val;
        pUM->curcfg.lnkcfg.link_autoneg = *(uint8_t *)pr_val;
        reprogram = B_TRUE;
        break;

    case MAC_PROP_FLOWCTRL:

        bcopy(pr_val, &fl, sizeof(fl));

        switch (fl)
        {
        case LINK_FLOWCTRL_NONE:

            rxpause = B_FALSE;
            txpause = B_FALSE;
            break;

        case LINK_FLOWCTRL_RX:

            rxpause = B_TRUE;
            txpause = B_FALSE;
            break;

        case LINK_FLOWCTRL_TX:

            rxpause = B_FALSE;
            txpause = B_TRUE;
            break;

        case LINK_FLOWCTRL_BI:

            rxpause = B_TRUE;
            txpause = B_TRUE;
            break;

        default:

            err = ENOTSUP;
            break;
        }

        if (err == 0)
        {
            if ((pUM->hwinit.lnkcfg.param_rxpause == rxpause) &&
                (pUM->hwinit.lnkcfg.param_txpause == txpause))
            {
                break;
            }

            pUM->hwinit.lnkcfg.param_rxpause = rxpause;
            pUM->hwinit.lnkcfg.param_txpause = txpause;
            pUM->curcfg.lnkcfg.param_rxpause = rxpause;
            pUM->curcfg.lnkcfg.param_txpause = txpause;
            reprogram = B_TRUE;
        }

        break;

    case MAC_PROP_MTU:

        if (pUM->plumbed)
        {
            err = EBUSY;
            break;
        }

        bcopy(pr_val, &mtu, sizeof (mtu));

        if ((mtu < USER_OPTION_MTU_MIN) || (mtu > USER_OPTION_MTU_MAX))
        {
            err = EINVAL;
            break;
        }

        if (pUM->devParams.mtu[LM_CLI_IDX_NDIS] == mtu)
        {
            break;
        }

        pUM->devParams.mtu[LM_CLI_IDX_NDIS] = mtu;
        err = mac_maxsdu_update(pUM->pMac, pUM->devParams.mtu[LM_CLI_IDX_NDIS]);
        pUM->lm_dev.params.mtu[LM_CLI_IDX_NDIS] = pUM->devParams.mtu[LM_CLI_IDX_NDIS];
        break;

    case MAC_PROP_PRIVATE:

        err = BnxeSetPrivateProperty(pUM, pr_name, pr_valsize, pr_val);
        break;
    }

    if (!err && reprogram)
    {
        BnxeUpdatePhy(pUM, B_TRUE);
    }

    BNXE_LOCK_EXIT_GLD(pUM);
    return err;
}

#endif /* MC_SETPROP */


#ifdef MC_GETPROP

static int BnxeGetPrivateProperty(um_device_t * pUM,
                                  const char *  pr_name,
                                  uint_t        pr_valsize,
                                  void *        pr_val)
{
    BnxeLinkCfg * lnk_cfg = &pUM->curcfg.lnkcfg;
    BnxeLinkCfg * hw_cfg  = &pUM->hwinit.lnkcfg;
    int value;
    int err = 0;

    if (strcmp(pr_name, "_adv_2500fdx_cap") == 0)
    {
        value = lnk_cfg->param_2500fdx;
    }
    else if (strcmp(pr_name, "_en_2500fdx_cap") == 0)
    {
        value = hw_cfg->param_2500fdx;
    }
    else if (strcmp(pr_name, "_adv_txpause_cap") == 0)
    {
        value = lnk_cfg->param_txpause;
    }
    else if (strcmp(pr_name, "_en_txpause_cap") == 0)
    {
        value = hw_cfg->param_txpause;
    }
    else if (strcmp(pr_name, "_txpause") == 0)
    {
        value = pUM->props.link_txpause;
    }
    else if (strcmp(pr_name, "_adv_rxpause_cap") == 0)
    {
        value = lnk_cfg->param_rxpause;
    }
    else if (strcmp(pr_name, "_en_rxpause_cap") == 0)
    {
        value = hw_cfg->param_rxpause;
    }
    else if (strcmp(pr_name, "_rxpause") == 0)
    {
        value = pUM->props.link_rxpause;
    }
    else if (strcmp(pr_name, "_autoneg_flow") == 0)
    {
        value = pUM->hwinit.flow_autoneg;
    }
    else if (strcmp(pr_name, "_checksum") == 0)
    {
        value = pUM->devParams.checksum;
    }
    else if (strcmp(pr_name, "_tx_ring_policy") == 0)
    {
        value = pUM->devParams.routeTxRingPolicy;
    }
    else if (strcmp(pr_name, "_num_rings") == 0)
    {
        value = pUM->devParams.numRings;
    }
    else if (strcmp(pr_name, "_rx_descs") == 0)
    {
        value = pUM->devParams.numRxDesc[LM_CLI_IDX_NDIS];
    }
    else if (strcmp(pr_name, "_rx_free_reclaim") == 0)
    {
        value = pUM->devParams.maxRxFree;
    }
    else if (strcmp(pr_name, "_tx_descs") == 0)
    {
        value = pUM->devParams.numTxDesc[LM_CLI_IDX_NDIS];
    }
    else if (strcmp(pr_name, "_tx_free_reclaim") == 0)
    {
        value = pUM->devParams.maxTxFree;
    }
    else if (strcmp(pr_name, "_rx_copy_threshold") == 0)
    {
        value = pUM->devParams.rxCopyThreshold;
    }
    else if (strcmp(pr_name, "_tx_copy_threshold") == 0)
    {
        value = pUM->devParams.txCopyThreshold;
    }
    else if (strcmp(pr_name, "_interrupt_coalesce") == 0)
    {
        value = pUM->devParams.intrCoalesce;
    }
    else if (strcmp(pr_name, "_rx_interrupt_coalesce_usec") == 0)
    {
        value = pUM->devParams.intrRxPerSec;
    }
    else if (strcmp(pr_name, "_tx_interrupt_coalesce_usec") == 0)
    {
        value = pUM->devParams.intrTxPerSec;
    }
    else if (strcmp(pr_name, "_disable_msix") == 0)
    {
        value = pUM->devParams.disableMsix;
    }
    else if (strcmp(pr_name, "_l2_fw_flow_ctrl") == 0)
    {
        value = pUM->devParams.l2_fw_flow_ctrl;
    }
    else if (strcmp(pr_name, "_lso_enable") == 0)
    {
        value = pUM->devParams.lsoEnable;
    }
    else if (strcmp(pr_name, "_log_enable") == 0)
    {
        value = pUM->devParams.logEnable;
    }
#ifdef BNXE_FCOE_SUPPORT
    else if (strcmp(pr_name, "_fcoe_enable") == 0)
    {
        value = pUM->devParams.fcoeEnable;
    }
#endif
    else
    {
        err = ENOTSUP;
    }

    if (!err)
    {
        (void)snprintf(pr_val, pr_valsize, "%d", value);
    }

    return err;
}


static int BnxeMacGetProperty(void *        barg,
                              const char *  pr_name,
                              mac_prop_id_t pr_num,
                              uint_t        pr_valsize,
                              void *        pr_val)
{
    um_device_t *   pUM = barg;
    link_flowctrl_t link_flowctrl;
    link_state_t    link_state;
    link_duplex_t   link_duplex;
    uint64_t        link_speed;
    BnxeLinkCfg * lnk_cfg = &pUM->curcfg.lnkcfg;
    BnxeLinkCfg * hw_cfg  = &pUM->hwinit.lnkcfg;

    switch (pr_num)
    {
    case MAC_PROP_MTU:

        ASSERT(pr_valsize >= sizeof(u32_t));

        bcopy(&pUM->devParams.mtu[LM_CLI_IDX_NDIS], pr_val, sizeof(u32_t));
        break;

    case MAC_PROP_DUPLEX:

        ASSERT(pr_valsize >= sizeof(link_duplex_t));

        link_duplex = pUM->props.link_duplex ?
                          LINK_DUPLEX_FULL : LINK_DUPLEX_HALF;
        bcopy(&link_duplex, pr_val, sizeof(link_duplex_t));
        break;

    case MAC_PROP_SPEED:

        ASSERT(pr_valsize >= sizeof(link_speed));

        link_speed = (pUM->props.link_speed * 1000000ULL);
        bcopy(&link_speed, pr_val, sizeof(link_speed));
        break;

    case MAC_PROP_STATUS:

        ASSERT(pr_valsize >= sizeof(link_state_t));

        link_state = pUM->props.link_speed ?
                         LINK_STATE_UP : LINK_STATE_DOWN;
        bcopy(&link_state, pr_val, sizeof(link_state_t));
        break;

    case MAC_PROP_AUTONEG:

        *(uint8_t *)pr_val = lnk_cfg->link_autoneg;
        break;

    case MAC_PROP_FLOWCTRL:

        ASSERT(pr_valsize >= sizeof(link_flowctrl_t));

        if (!lnk_cfg->param_rxpause && !lnk_cfg->param_txpause)
        {
            link_flowctrl = LINK_FLOWCTRL_NONE;
        }
        if (lnk_cfg->param_rxpause && !lnk_cfg->param_txpause)
        {
            link_flowctrl = LINK_FLOWCTRL_RX;
        }
        if (!lnk_cfg->param_rxpause && lnk_cfg->param_txpause)
        {
            link_flowctrl = LINK_FLOWCTRL_TX;
        }
        if (lnk_cfg->param_rxpause && lnk_cfg->param_txpause)
        {
            link_flowctrl = LINK_FLOWCTRL_BI;
        }

        bcopy(&link_flowctrl, pr_val, sizeof(link_flowctrl_t));
        break;

    case MAC_PROP_ADV_10GFDX_CAP:

        *(uint8_t *)pr_val = lnk_cfg->param_10000fdx;
        break;

    case MAC_PROP_EN_10GFDX_CAP:

        *(uint8_t *)pr_val = hw_cfg->param_10000fdx;
        break;

    case MAC_PROP_ADV_1000FDX_CAP:

        *(uint8_t *)pr_val = lnk_cfg->param_1000fdx;
        break;

    case MAC_PROP_EN_1000FDX_CAP:

        *(uint8_t *)pr_val = hw_cfg->param_1000fdx;
        break;

    case MAC_PROP_ADV_1000HDX_CAP:
    case MAC_PROP_EN_1000HDX_CAP:

        *(uint8_t *)pr_val = 0;
        break;

    case MAC_PROP_ADV_100FDX_CAP:

        *(uint8_t *)pr_val = lnk_cfg->param_100fdx;
        break;

    case MAC_PROP_EN_100FDX_CAP:

        *(uint8_t *)pr_val = hw_cfg->param_100fdx;
        break;

    case MAC_PROP_ADV_100HDX_CAP:

        *(uint8_t *)pr_val = lnk_cfg->param_100hdx;
        break;

    case MAC_PROP_EN_100HDX_CAP:

        *(uint8_t *)pr_val = hw_cfg->param_100hdx;
        break;

    case MAC_PROP_ADV_100T4_CAP:
    case MAC_PROP_EN_100T4_CAP:

        *(uint8_t *)pr_val = 0;
        break;

    case MAC_PROP_ADV_10FDX_CAP:

        *(uint8_t *)pr_val = lnk_cfg->param_10fdx;
        break;

    case MAC_PROP_EN_10FDX_CAP:

        *(uint8_t *)pr_val = hw_cfg->param_10fdx;
        break;

    case MAC_PROP_ADV_10HDX_CAP:

        *(uint8_t *)pr_val = lnk_cfg->param_10hdx;
        break;

    case MAC_PROP_EN_10HDX_CAP:

        *(uint8_t *)pr_val = hw_cfg->param_10hdx;
        break;

    case MAC_PROP_PRIVATE:

        return BnxeGetPrivateProperty(pUM,
                                      pr_name,
                                      pr_valsize,
                                      pr_val);

    default:

        return ENOTSUP;
    }

    return 0;
}

#endif /* MC_GETPROP */


#ifdef MC_PROPINFO

static void BnxeMacPrivatePropertyInfo(um_device_t *          pUM,
                                       const char *           pr_name,
                                       mac_prop_info_handle_t prh)
{
    char valstr[64];
    BnxeLinkCfg * default_cfg = &bnxeLinkCfg;
    int default_val;

    bzero(valstr, sizeof (valstr));

    if (strcmp(pr_name, "_adv_2500fdx_cap") == 0 ||
        strcmp(pr_name, "_adv_txpause_cap") == 0 ||
        strcmp(pr_name, "_txpause")         == 0 ||
        strcmp(pr_name, "_adv_rxpause_cap") == 0 ||
        strcmp(pr_name, "_rxpause")         == 0)
    {
        mac_prop_info_set_perm(prh, MAC_PROP_PERM_READ);
        return;
    }

    if (strcmp(pr_name, "_autoneg_flow") == 0)
    {
        default_val = B_TRUE;
    }
    else if (strcmp(pr_name, "_checksum") == 0)
    {
        default_val = USER_OPTION_CKSUM_DEFAULT;
    }
    else if (strcmp(pr_name, "_tx_ring_policy") == 0)
    {
        default_val = BNXE_ROUTE_RING_TCPUDP;
    }
    else if (strcmp(pr_name, "_num_rings") == 0)
    {
        default_val = USER_OPTION_NUM_RINGS_DEFAULT;
    }
    else if (strcmp(pr_name, "_rx_descs") == 0)
    {
        default_val = USER_OPTION_RX_BDS_DEFAULT;
    }
    else if (strcmp(pr_name, "_rx_free_reclaim") == 0)
    {
        default_val = USER_OPTION_RX_MAX_FREE_DEFAULT;
    }
    else if (strcmp(pr_name, "_tx_descs") == 0)
    {
        default_val = USER_OPTION_TX_BDS_DEFAULT;
    }
    else if (strcmp(pr_name, "_tx_free_reclaim") == 0)
    {
        default_val = USER_OPTION_TX_MAX_FREE_DEFAULT;
    }
    else if (strcmp(pr_name, "_rx_copy_threshold") == 0)
    {
        default_val = USER_OPTION_RX_DCOPY_THRESH_DEFAULT;
    }
    else if (strcmp(pr_name, "_tx_copy_threshold") == 0)
    {
        default_val = USER_OPTION_TX_DCOPY_THRESH_DEFAULT;
    }
    else if (strcmp(pr_name, "_interrupt_coalesce") == 0)
    {
        default_val = B_TRUE;
    }
    else if (strcmp(pr_name, "_rx_interrupt_coalesce_usec") == 0)
    {
        default_val = USER_OPTION_INTR_COALESCE_RX_DEFAULT;
    }
    else if (strcmp(pr_name, "_tx_interrupt_coalesce_usec") == 0)
    {
        default_val = USER_OPTION_INTR_COALESCE_TX_DEFAULT;
    }
    else if (strcmp(pr_name, "_disable_msix") == 0)
    {
        default_val = B_FALSE;
    }
    else if (strcmp(pr_name, "_l2_fw_flow_ctrl") == 0)
    {
        default_val = B_FALSE;
    }
    else if (strcmp(pr_name, "_lso_enable") == 0)
    {
        default_val = B_TRUE;
    }
    else if (strcmp(pr_name, "_log_enable") == 0)
    {
        default_val = B_TRUE;
    }
#ifdef BNXE_FCOE_SUPPORT
    else if (strcmp(pr_name, "_fcoe_enable") == 0)
    {
        default_val = B_TRUE;
    }
#endif
    else
    {
        return;
    }

    snprintf(valstr, sizeof (valstr), "%d", default_val);
    mac_prop_info_set_default_str(prh, valstr);
}


static void BnxeMacPropertyInfo(void *                 barg,
                                const char *           pr_name,
                                mac_prop_id_t          pr_num, 
                                mac_prop_info_handle_t prh)
{
    um_device_t * pUM = barg;
    link_flowctrl_t link_flowctrl;
    BnxeLinkCfg * default_cfg = &bnxeLinkCfg;

    switch (pr_num)
    {
    case MAC_PROP_DUPLEX:
    case MAC_PROP_SPEED:
    case MAC_PROP_STATUS:
    case MAC_PROP_ADV_10GFDX_CAP:
    case MAC_PROP_ADV_1000FDX_CAP:
    case MAC_PROP_ADV_1000HDX_CAP:
    case MAC_PROP_EN_1000HDX_CAP:
    case MAC_PROP_ADV_100FDX_CAP:
    case MAC_PROP_ADV_100HDX_CAP:
    case MAC_PROP_ADV_100T4_CAP:
    case MAC_PROP_EN_100T4_CAP:
    case MAC_PROP_ADV_10FDX_CAP:
    case MAC_PROP_ADV_10HDX_CAP:

        mac_prop_info_set_perm(prh, MAC_PROP_PERM_READ);
        break;

    case MAC_PROP_EN_10GFDX_CAP:
    case MAC_PROP_EN_1000FDX_CAP:
    case MAC_PROP_EN_100FDX_CAP:
    case MAC_PROP_EN_100HDX_CAP:
    case MAC_PROP_EN_10FDX_CAP:
    case MAC_PROP_EN_10HDX_CAP:
    case MAC_PROP_MTU:

        mac_prop_info_set_perm(prh, MAC_PROP_PERM_RW);
        break;

    case MAC_PROP_AUTONEG:

        mac_prop_info_set_default_uint8(prh, default_cfg->link_autoneg);
        break;

    case MAC_PROP_FLOWCTRL:

        if (!default_cfg->param_rxpause && !default_cfg->param_txpause)
        {
            link_flowctrl = LINK_FLOWCTRL_NONE;
        }

        if (default_cfg->param_rxpause && !default_cfg->param_txpause)
        {
            link_flowctrl = LINK_FLOWCTRL_RX;
        }

        if (!default_cfg->param_rxpause && default_cfg->param_txpause)
        {
            link_flowctrl = LINK_FLOWCTRL_TX;
        }

        if (default_cfg->param_rxpause && default_cfg->param_txpause)
        {
            link_flowctrl = LINK_FLOWCTRL_BI;
        }

        mac_prop_info_set_default_link_flowctrl(prh, link_flowctrl);
        break;

    case MAC_PROP_PRIVATE:

        BnxeMacPrivatePropertyInfo(pUM, pr_name, prh);
        break;
    }
}

#endif /* MC_PROPINFO */


static mac_callbacks_t bnxe_callbacks =
{
    (
      MC_IOCTL
#ifdef MC_RESOURCES
    | MC_RESOURCES
#endif
#ifdef MC_SETPROP
    | MC_SETPROP
#endif
#ifdef MC_GETPROP
    | MC_GETPROP
#endif
#ifdef MC_PROPINFO
    | MC_PROPINFO
#endif
    | MC_GETCAPAB
    ),
    BnxeMacStats,
    BnxeMacStart,
    BnxeMacStop,
    BnxeMacPromiscuous,
    BnxeMacMulticast,
#if defined(BNXE_RINGS) && defined (__Solaris_11)
    NULL,
    NULL,
#else
    BnxeMacUnicast,
    BnxeMacTx,
#endif
#ifdef MC_RESOURCES
    BnxeMacResources,
#else
    NULL,
#endif
    BnxeMacIoctl,
    BnxeMacGetCapability,
#ifdef MC_OPEN
    NULL,
    NULL,
#endif
#ifdef MC_SETPROP
    BnxeMacSetProperty,
#endif
#ifdef MC_GETPROP
    BnxeMacGetProperty,
#endif
#ifdef MC_PROPINFO
    BnxeMacPropertyInfo
#endif
};


boolean_t BnxeGldInit(um_device_t * pUM)
{
    mac_register_t * pMac;
    int rc;

    atomic_swap_32(&pUM->plumbed, B_FALSE);

    if ((pMac = mac_alloc(MAC_VERSION)) == NULL)
    {
        BnxeLogWarn(pUM, "Failed to allocate GLD MAC memory");
        return B_FALSE;
    }

    pMac->m_driver     = pUM;
    pMac->m_dip        = pUM->pDev;
    pMac->m_type_ident = MAC_PLUGIN_IDENT_ETHER;
    pMac->m_callbacks  = &bnxe_callbacks;
    pMac->m_min_sdu    = 0;
    pMac->m_max_sdu    = pUM->devParams.mtu[LM_CLI_IDX_NDIS];
    pMac->m_src_addr   = &(pUM->lm_dev.params.mac_addr[0]);

#ifdef MC_OPEN
    pMac->m_margin = VLAN_TAGSZ;
#endif

#ifdef MC_SETPROP
    pMac->m_priv_props = bnxeLink_priv_props;
#endif

    rc = mac_register(pMac, &pUM->pMac);

    mac_free(pMac);

    if (rc != 0)
    {
        BnxeLogWarn(pUM, "Failed to register with GLD (%d)", rc);
        return B_FALSE;
    }

    /* Always report the initial link state as unknown. */
    mac_link_update(pUM->pMac, LINK_STATE_UNKNOWN);

    return B_TRUE;
}


boolean_t BnxeGldFini(um_device_t * pUM)
{
    int cnt;

    if (pUM->plumbed)
    {
        BnxeLogWarn(pUM, "Detaching device from GLD that is started!");
        return B_FALSE;
    }

    /* We must not detach until all packets held by stack are retrieved. */
    if (!BnxeWaitForPacketsFromClient(pUM, LM_CLI_IDX_NDIS))
    {
        return B_FALSE;
    }

    if (pUM->pMac)
    {
        if (mac_unregister(pUM->pMac))
        {
            BnxeLogWarn(pUM, "Failed to unregister with the GLD");
            return B_FALSE;
        }
    }

    return B_TRUE;
}


void BnxeGldLink(um_device_t * pUM,
                 link_state_t  state)
{
    mac_link_update(pUM->pMac, state);
}

