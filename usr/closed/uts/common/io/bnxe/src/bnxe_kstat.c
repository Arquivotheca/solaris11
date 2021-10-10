
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

typedef struct _BnxeKstat
{
    kstat_named_t umdev_hi;
    kstat_named_t umdev_lo;
    kstat_named_t version;
    kstat_named_t versionFW;
    kstat_named_t versionBC;
    kstat_named_t chipName;
    kstat_named_t chipID;
    kstat_named_t multiFunction;
    kstat_named_t multiFunctionVnics;
    kstat_named_t macAddr;
    kstat_named_t hwInitDone;
    kstat_named_t clientsHw;
    kstat_named_t clientsDevi;
    kstat_named_t clientsBound;
    kstat_named_t txMsgPullUp;
    kstat_named_t intrAlloc;
    kstat_named_t intrFired;
    kstat_named_t timerFired;
    kstat_named_t timerReply;
    kstat_named_t timerNoReplyTotal;
    kstat_named_t timerNoReplyCurrent;
    kstat_named_t timerDone;
    kstat_named_t workQueueInstCnt;
    kstat_named_t workItemInstQueued;
    kstat_named_t workItemInstError;
    kstat_named_t workItemInstComplete;
    kstat_named_t workQueueDelayCnt;
    kstat_named_t workItemDelayQueued;
    kstat_named_t workItemDelayError;
    kstat_named_t workItemDelayComplete;
    kstat_named_t memAllocBlocks;
    kstat_named_t memAllocDMAs;
    kstat_named_t memAllocBARs;
} BnxeKstat;

#define BNXE_KSTAT_SIZE (sizeof(BnxeKstat) / sizeof(kstat_named_t))

typedef struct _BnxeKstatLink
{
    kstat_named_t clients;
    kstat_named_t uptime;
    kstat_named_t mtuL2;
    kstat_named_t mtuFCOE;
    kstat_named_t speed;
    kstat_named_t link;
    kstat_named_t duplex;
    kstat_named_t pauseRx;
    kstat_named_t pauseTx;
} BnxeKstatLink;

#define BNXE_KSTAT_LINK_SIZE (sizeof(BnxeKstatLink) / sizeof(kstat_named_t))

typedef struct _BnxeKstatIntr
{
    kstat_named_t intrAlloc;
    kstat_named_t intrFired;
    kstat_named_t intrWrongState;
    kstat_named_t intrInDisabled;
    kstat_named_t intrZeroStatus;
    kstat_named_t sb_00;
    kstat_named_t sb_01;
    kstat_named_t sb_02;
    kstat_named_t sb_03;
    kstat_named_t sb_04;
    kstat_named_t sb_05;
    kstat_named_t sb_06;
    kstat_named_t sb_07;
    kstat_named_t sb_08;
    kstat_named_t sb_09;
    kstat_named_t sb_10;
    kstat_named_t sb_11;
    kstat_named_t sb_12;
    kstat_named_t sb_13;
    kstat_named_t sb_14;
    kstat_named_t sb_15;
    kstat_named_t sb_16;
    kstat_named_t sb_nc_00;
    kstat_named_t sb_nc_01;
    kstat_named_t sb_nc_02;
    kstat_named_t sb_nc_03;
    kstat_named_t sb_nc_04;
    kstat_named_t sb_nc_05;
    kstat_named_t sb_nc_06;
    kstat_named_t sb_nc_07;
    kstat_named_t sb_nc_08;
    kstat_named_t sb_nc_09;
    kstat_named_t sb_nc_10;
    kstat_named_t sb_nc_11;
    kstat_named_t sb_nc_12;
    kstat_named_t sb_nc_13;
    kstat_named_t sb_nc_14;
    kstat_named_t sb_nc_15;
    kstat_named_t sb_nc_16;
    kstat_named_t sb_poll_00;
    kstat_named_t sb_poll_01;
    kstat_named_t sb_poll_02;
    kstat_named_t sb_poll_03;
    kstat_named_t sb_poll_04;
    kstat_named_t sb_poll_05;
    kstat_named_t sb_poll_06;
    kstat_named_t sb_poll_07;
    kstat_named_t sb_poll_08;
    kstat_named_t sb_poll_09;
    kstat_named_t sb_poll_10;
    kstat_named_t sb_poll_11;
    kstat_named_t sb_poll_12;
    kstat_named_t sb_poll_13;
    kstat_named_t sb_poll_14;
    kstat_named_t sb_poll_15;
    kstat_named_t sb_poll_16;
    kstat_named_t sb_poll_nc_00;
    kstat_named_t sb_poll_nc_01;
    kstat_named_t sb_poll_nc_02;
    kstat_named_t sb_poll_nc_03;
    kstat_named_t sb_poll_nc_04;
    kstat_named_t sb_poll_nc_05;
    kstat_named_t sb_poll_nc_06;
    kstat_named_t sb_poll_nc_07;
    kstat_named_t sb_poll_nc_08;
    kstat_named_t sb_poll_nc_09;
    kstat_named_t sb_poll_nc_10;
    kstat_named_t sb_poll_nc_11;
    kstat_named_t sb_poll_nc_12;
    kstat_named_t sb_poll_nc_13;
    kstat_named_t sb_poll_nc_14;
    kstat_named_t sb_poll_nc_15;
    kstat_named_t sb_poll_nc_16;
} BnxeKstatIntr;

#define BNXE_KSTAT_INTR_SIZE (sizeof(BnxeKstatIntr) / sizeof(kstat_named_t))

typedef struct _BnxeKstatL2Chip
{
    kstat_named_t IfHCInOctets;
    kstat_named_t IfHCInBadOctets;
    kstat_named_t IfHCOutOctets;
    kstat_named_t IfHCOutBadOctets;
    kstat_named_t IfHCOutPkts;
    kstat_named_t IfHCInPkts;
    kstat_named_t IfHCInUcastPkts;
    kstat_named_t IfHCInMulticastPkts;
    kstat_named_t IfHCInBroadcastPkts;
    kstat_named_t IfHCOutUcastPkts;
    kstat_named_t IfHCOutMulticastPkts;
    kstat_named_t IfHCOutBroadcastPkts;
    kstat_named_t IfHCInUcastOctets;
    kstat_named_t IfHCInMulticastOctets;
    kstat_named_t IfHCInBroadcastOctets;
    kstat_named_t IfHCOutUcastOctets;
    kstat_named_t IfHCOutMulticastOctets;
    kstat_named_t IfHCOutBroadcastOctets;
    kstat_named_t IfHCOutDiscards;
    kstat_named_t IfHCInFalseCarrierErrors;
    kstat_named_t Dot3StatsInternalMacTransmitErrors;
    kstat_named_t Dot3StatsCarrierSenseErrors;
    kstat_named_t Dot3StatsFCSErrors;
    kstat_named_t Dot3StatsAlignmentErrors;
    kstat_named_t Dot3StatsSingleCollisionFrames;
    kstat_named_t Dot3StatsMultipleCollisionFrames;
    kstat_named_t Dot3StatsDeferredTransmissions;
    kstat_named_t Dot3StatsExcessiveCollisions;
    kstat_named_t Dot3StatsLateCollisions;
    kstat_named_t EtherStatsCollisions;
    kstat_named_t EtherStatsFragments;
    kstat_named_t EtherStatsJabbers;
    kstat_named_t EtherStatsUndersizePkts;
    kstat_named_t EtherStatsOverrsizePkts;
    kstat_named_t EtherStatsTx64Octets;
    kstat_named_t EtherStatsTx65to127Octets;
    kstat_named_t EtherStatsTx128to255Octets;
    kstat_named_t EtherStatsTx256to511Octets;
    kstat_named_t EtherStatsTx512to1023Octets;
    kstat_named_t EtherStatsTx1024to1522Octets;
    kstat_named_t EtherStatsTxOver1522Octets;
    kstat_named_t XonPauseFramesReceived;
    kstat_named_t XoffPauseFramesReceived;
    kstat_named_t OutXonSent;
    kstat_named_t OutXoffSent;
    kstat_named_t FlowControlDone;
    kstat_named_t MacControlFramesReceived;
    kstat_named_t XoffStateEntered;
    kstat_named_t IfInFramesL2FilterDiscards;
    kstat_named_t IfInTTL0Discards;
    kstat_named_t IfInxxOverflowDiscards;
    kstat_named_t IfInMBUFDiscards;
    kstat_named_t IfInErrors;
    kstat_named_t IfInErrorsOctets;
    kstat_named_t IfInNoBrbBuffer;
    //kstat_named_t NigBrbPacket;
    //kstat_named_t NigBrbTruncate;
    //kstat_named_t NigFlowCtrlDiscard;
    //kstat_named_t NigFlowCtrlOctets;
    //kstat_named_t NigFlowCtrlPacket;
    //kstat_named_t NigMngDiscard;
    //kstat_named_t NigMngOctetInp;
    //kstat_named_t NigMngOctetOut;
    //kstat_named_t NigMngPacketInp;
    //kstat_named_t NigMngPacketOut;
    //kstat_named_t NigPbfOctets;
    //kstat_named_t NigPbfPacket;
    //kstat_named_t NigSafcInp;
} BnxeKstatL2Chip;

#define BNXE_KSTAT_L2_CHIP_SIZE (sizeof(BnxeKstatL2Chip) / sizeof(kstat_named_t))

typedef struct _BnxeKstatL2Driver
{
    kstat_named_t RxIPv4FragCount;
    kstat_named_t RxIpCsErrorCount;
    kstat_named_t RxTcpCsErrorCount;
    kstat_named_t RxLlcSnapCount;
    kstat_named_t RxPhyErrorCount;
    kstat_named_t RxIpv6ExtCount;
    kstat_named_t TxNoL2Bd;
    kstat_named_t TxNoSqWqe;
    kstat_named_t TxL2AssemblyBufUse;
} BnxeKstatL2Driver;

#define BNXE_KSTAT_L2_DRIVER_SIZE (sizeof(BnxeKstatL2Driver) / sizeof(kstat_named_t))

typedef struct _BnxeKstatEthStats
{
    /* lm_stats_t */
    kstat_named_t txFramesOk;         // LM_STATS_FRAMES_XMITTED_OK
    kstat_named_t rxFramesOk;         // LM_STATS_FRAMES_RECEIVED_OK
    kstat_named_t txErr;              // LM_STATS_ERRORED_TRANSMIT_CNT
    kstat_named_t rxErr;              // LM_STATS_ERRORED_RECEIVE_CNT
    kstat_named_t rxCrcErr;           // LM_STATS_RCV_CRC_ERROR
    kstat_named_t alignErr;           // LM_STATS_ALIGNMENT_ERROR
    kstat_named_t collisionsSingle;   // LM_STATS_SINGLE_COLLISION_FRAMES
    kstat_named_t collisionsMultiple; // LM_STATS_MULTIPLE_COLLISION_FRAMES
    kstat_named_t framesDeferred;     // LM_STATS_FRAMES_DEFERRED
    kstat_named_t collisonsMax;       // LM_STATS_MAX_COLLISIONS
    kstat_named_t rxOverrun;          // LM_STATS_RCV_OVERRUN
    kstat_named_t txOverrun;          // LM_STATS_XMIT_UNDERRUN
    kstat_named_t txFramesUnicast;    // LM_STATS_UNICAST_FRAMES_XMIT
    kstat_named_t txFramesMulticast;  // LM_STATS_MULTICAST_FRAMES_XMIT
    kstat_named_t txFramesBroadcast;  // LM_STATS_BROADCAST_FRAMES_XMIT
    kstat_named_t rxFramesUnicast;    // LM_STATS_UNICAST_FRAMES_RCV
    kstat_named_t rxFramesMulticast;  // LM_STATS_MULTICAST_FRAMES_RCV
    kstat_named_t rxFramesBroadcast;  // LM_STATS_BROADCAST_FRAMES_RCV
    kstat_named_t rxNoBufferDrop;     // LM_STATS_RCV_NO_BUFFER_DROP
    kstat_named_t rxBytes;            // LM_STATS_BYTES_RCV
    kstat_named_t txBytes;            // LM_STATS_BYTES_XMIT
    kstat_named_t offloadIP4;         // LM_STATS_IP4_OFFLOAD
    kstat_named_t offloadTCP;         // LM_STATS_TCP_OFFLOAD
    kstat_named_t ifInDiscards;       // LM_STATS_IF_IN_DISCARDS
    kstat_named_t ifInErrors;         // LM_STATS_IF_IN_ERRORS
    kstat_named_t ifOutErrors;        // LM_STATS_IF_OUT_ERRORS
    kstat_named_t offloadIP6;         // LM_STATS_IP6_OFFLOAD
    kstat_named_t offloadTCP6;        // LM_STATS_TCP6_OFFLOAD
    kstat_named_t txDiscards;         // LM_STATS_XMIT_DISCARDS
    kstat_named_t rxBytesUnicast;     // LM_STATS_DIRECTED_BYTES_RCV
    kstat_named_t rxBytesMulticast;   // LM_STATS_MULTICAST_BYTES_RCV
    kstat_named_t rxBytesBroadcast;   // LM_STATS_BROADCAST_BYTES_RCV
    kstat_named_t txBytesUnicast;     // LM_STATS_DIRECTED_BYTES_XMIT
    kstat_named_t txBytesMulticast;   // LM_STATS_MULTICAST_BYTES_XMIT
    kstat_named_t txBytesBroadcast;   // LM_STATS_BROADCAST_BYTES_XMIT
} BnxeKstatEthStats;

#define BNXE_KSTAT_ETH_STATS_SIZE (sizeof(BnxeKstatEthStats) / sizeof(kstat_named_t))

typedef struct _BnxeKstatFcoe
{
    kstat_named_t pdev_hi;
    kstat_named_t pdev_lo;
    kstat_named_t macAddr;
    kstat_named_t initWqeTx;
    kstat_named_t initWqeTxErr;
    kstat_named_t initCqeRx;
    kstat_named_t initCqeRxErr;
    kstat_named_t offloadConnWqeTx;
    kstat_named_t offloadConnWqeTxErr;
    kstat_named_t offloadConnCqeRx;
    kstat_named_t offloadConnCqeRxErr;
    kstat_named_t enableConnWqeTx;
    kstat_named_t enableConnWqeTxErr;
    kstat_named_t enableConnCqeRx;
    kstat_named_t enableConnCqeRxErr;
    kstat_named_t disableConnWqeTx;
    kstat_named_t disableConnWqeTxErr;
    kstat_named_t disableConnCqeRx;
    kstat_named_t disableConnCqeRxErr;
    kstat_named_t destroyConnWqeTx;
    kstat_named_t destroyConnWqeTxErr;
    kstat_named_t destroyConnCqeRx;
    kstat_named_t destroyConnCqeRxErr;
    kstat_named_t destroyWqeTx;
    kstat_named_t destroyWqeTxErr;
    kstat_named_t destroyCqeRx;
    kstat_named_t destroyCqeRxErr;
    kstat_named_t compRequestCqeRx;
    kstat_named_t compRequestCqeRxErr;
    kstat_named_t statWqeTx;
    kstat_named_t statWqeTxErr;
    kstat_named_t statCqeRx;
    kstat_named_t statCqeRxErr;
} BnxeKstatFcoe;

#define BNXE_KSTAT_FCOE_SIZE (sizeof(BnxeKstatFcoe) / sizeof(kstat_named_t))

typedef struct _BnxeKstatRxq
{
    kstat_named_t rxFreeDesc;
    kstat_named_t rxActiveDesc;
    kstat_named_t rxDonePkts;
    kstat_named_t rxWaitingPkts;
    kstat_named_t rxBDsAvail;
    kstat_named_t rxDiscards;
    kstat_named_t rxCopied;
    kstat_named_t rxLowWater;
    kstat_named_t rxBufUpInStack;
    kstat_named_t genNumber;
    kstat_named_t inPollMode;
    kstat_named_t intrDisableCnt;
    kstat_named_t intrEnableCnt;
    kstat_named_t pollCnt;
} BnxeKstatRxq;

#define BNXE_KSTAT_RXQ_SIZE (sizeof(BnxeKstatRxq) / sizeof(kstat_named_t))

typedef struct _BnxeKstatTxq
{
    kstat_named_t txSentPkts;
    kstat_named_t txFreeDesc;
    kstat_named_t txWaitingPkts;
    kstat_named_t txBDsAvail;
    kstat_named_t txFailed;
    kstat_named_t txDiscards;
    kstat_named_t txRecycle;
    kstat_named_t txCopied;
    kstat_named_t txBlocked;
    kstat_named_t txWait;
    kstat_named_t txLowWater;
} BnxeKstatTxq;

#define BNXE_KSTAT_TXQ_SIZE (sizeof(BnxeKstatTxq) / sizeof(kstat_named_t))


static int BnxeKstatUpdate(kstat_t * kstats,
                           int       rw)
{
    BnxeKstat *   pStats = (BnxeKstat *)kstats->ks_data;
    um_device_t * pUM    = (um_device_t *)kstats->ks_private;
    lm_device_t * pLM    = (lm_device_t *)pUM;
    char buf[17]; /* 16 max for kstat string */

    if (rw == KSTAT_WRITE)
    {
        return EACCES;
    }

    BNXE_LOCK_ENTER_STATS(pUM);

    snprintf(buf, sizeof(buf), "%016p", (void *)pUM);
    strncpy(pStats->umdev_hi.value.c, &buf[0], 8);
    pStats->umdev_hi.value.c[8] = 0;
    strncpy(pStats->umdev_lo.value.c, &buf[8], 8);
    pStats->umdev_lo.value.c[8] = 0;

    strncpy(pStats->version.value.c,   pUM->version,   sizeof(pStats->version.value.c));
    strncpy(pStats->versionFW.value.c, pUM->versionFW, sizeof(pStats->versionFW.value.c));
    strncpy(pStats->versionBC.value.c, pUM->versionBC, sizeof(pStats->versionBC.value.c));

    strncpy(pStats->chipName.value.c, pUM->chipName, sizeof(pStats->chipName.value.c));
    strncpy(pStats->chipID.value.c,   pUM->chipID,   sizeof(pStats->chipID.value.c));

    pStats->multiFunction.value.ui64      = IS_MULTI_VNIC(&pUM->lm_dev) ? 1 : 0;
    pStats->multiFunctionVnics.value.ui64 = IS_MULTI_VNIC(&pUM->lm_dev) ? pLM->params.vnics_per_port : 0;

    snprintf(pStats->macAddr.value.c, 16, "%02x%02x%02x%02x%02x%02x",
             pLM->params.mac_addr[0], pLM->params.mac_addr[1],
             pLM->params.mac_addr[2], pLM->params.mac_addr[3],
             pLM->params.mac_addr[4], pLM->params.mac_addr[5]);

    pStats->hwInitDone.value.ui64 = pUM->hwInitDone;

    snprintf(pStats->clientsHw.value.c, 16, BnxeClientsHw(pUM));
    snprintf(pStats->clientsDevi.value.c, 16, BnxeClientsDevi(pUM));
    snprintf(pStats->clientsBound.value.c, 16, BnxeClientsBound(pUM));

    pStats->txMsgPullUp.value.ui64 = pUM->txMsgPullUp;

    strncpy(pStats->intrAlloc.value.c, pUM->intrAlloc, sizeof(pStats->intrAlloc.value.c));
    pStats->intrFired.value.ui64 = pUM->intrFired;

    pStats->timerFired.value.ui64          = pLM->vars.stats.stats_collect.timer_wakeup;
    pStats->timerReply.value.ui64          = pLM->vars.stats.stats_collect.stats_fw.drv_counter;
    pStats->timerNoReplyTotal.value.ui64   = pLM->vars.stats.stats_collect.stats_fw.timer_wakeup_no_completion_total;
    pStats->timerNoReplyCurrent.value.ui64 = pLM->vars.stats.stats_collect.stats_fw.timer_wakeup_no_completion_current;
    pStats->timerDone.value.ui64           = pLM->vars.stats.stats_collect.stats_fw.b_completion_done;

    pStats->workQueueInstCnt.value.ui64     = s_list_entry_cnt(&pUM->workqs.instq.workQueue);
    pStats->workItemInstQueued.value.ui64   = pUM->workqs.instq.workItemQueued;
    pStats->workItemInstError.value.ui64    = pUM->workqs.instq.workItemError;
    pStats->workItemInstComplete.value.ui64 = pUM->workqs.instq.workItemComplete;

    pStats->workQueueDelayCnt.value.ui64     = s_list_entry_cnt(&pUM->workqs.delayq.workQueue);
    pStats->workItemDelayQueued.value.ui64   = pUM->workqs.delayq.workItemQueued;
    pStats->workItemDelayError.value.ui64    = pUM->workqs.delayq.workItemError;
    pStats->workItemDelayComplete.value.ui64 = pUM->workqs.delayq.workItemComplete;

    pStats->memAllocBlocks.value.ui64 = d_list_entry_cnt(&pUM->memBlockList);
    pStats->memAllocDMAs.value.ui64   = d_list_entry_cnt(&pUM->memDmaList);
    pStats->memAllocBARs.value.ui64   = d_list_entry_cnt(&pUM->memRegionList);

    BNXE_LOCK_EXIT_STATS(pUM);

    return 0;
}


static int BnxeKstatLinkUpdate(kstat_t * kstats,
                               int       rw)
{
    BnxeKstatLink * pStats = (BnxeKstatLink *)kstats->ks_data;
    um_device_t *   pUM    = (um_device_t *)kstats->ks_private;
    lm_device_t *   pLM    = (lm_device_t *)pUM;

    if (rw == KSTAT_WRITE)
    {
        return EACCES;
    }

    BNXE_LOCK_ENTER_STATS(pUM);

    snprintf(pStats->clients.value.c, 16, BnxeClientsBound(pUM));
    pStats->uptime.value.ui64  = (pUM->props.link_speed) ?
                                     (ddi_get_time() - pUM->props.uptime) : 0;
    pStats->mtuL2.value.ui64   = pUM->lm_dev.params.mtu[LM_CLI_IDX_NDIS];
    pStats->mtuFCOE.value.ui64 = pUM->lm_dev.params.mtu[LM_CLI_IDX_FCOE];
    pStats->speed.value.ui64   = pUM->props.link_speed;
    pStats->link.value.ui64    = (!pUM->props.link_speed) ? 0 : 1;
    pStats->duplex.value.ui64  = pUM->props.link_duplex;
    pStats->pauseRx.value.ui64 = pUM->props.link_rxpause;
    pStats->pauseTx.value.ui64 = pUM->props.link_txpause;

    BNXE_LOCK_EXIT_STATS(pUM);

    return 0;
}


static int BnxeKstatIntrUpdate(kstat_t * kstats,
                               int       rw)
{
    BnxeKstatIntr * pStats = (BnxeKstatIntr *)kstats->ks_data;
    um_device_t *   pUM    = (um_device_t *)kstats->ks_private;
    lm_device_t *   pLM    = (lm_device_t *)pUM;

    if (rw == KSTAT_WRITE)
    {
        return EACCES;
    }

    BNXE_LOCK_ENTER_STATS(pUM);

    strncpy(pStats->intrAlloc.value.c, pUM->intrAlloc, sizeof(pStats->intrAlloc.value.c));
    pStats->intrFired.value.ui64      = pUM->intrFired;
    pStats->intrWrongState.value.ui64 = pLM->vars.dbg_intr_in_wrong_state;
    pStats->intrInDisabled.value.ui64 = pLM->vars.dbg_intr_in_disabled;
    pStats->intrZeroStatus.value.ui64 = pLM->vars.dbg_intr_zero_status;
    pStats->sb_00.value.ui64          = pUM->intrSbCnt[0];
    pStats->sb_01.value.ui64          = pUM->intrSbCnt[1];
    pStats->sb_02.value.ui64          = pUM->intrSbCnt[2];
    pStats->sb_03.value.ui64          = pUM->intrSbCnt[3];
    pStats->sb_04.value.ui64          = pUM->intrSbCnt[4];
    pStats->sb_05.value.ui64          = pUM->intrSbCnt[5];
    pStats->sb_06.value.ui64          = pUM->intrSbCnt[6];
    pStats->sb_07.value.ui64          = pUM->intrSbCnt[7];
    pStats->sb_08.value.ui64          = pUM->intrSbCnt[8];
    pStats->sb_09.value.ui64          = pUM->intrSbCnt[9];
    pStats->sb_10.value.ui64          = pUM->intrSbCnt[10];
    pStats->sb_11.value.ui64          = pUM->intrSbCnt[11];
    pStats->sb_12.value.ui64          = pUM->intrSbCnt[12];
    pStats->sb_13.value.ui64          = pUM->intrSbCnt[13];
    pStats->sb_14.value.ui64          = pUM->intrSbCnt[14];
    pStats->sb_15.value.ui64          = pUM->intrSbCnt[15];
    pStats->sb_16.value.ui64          = pUM->intrSbCnt[16];
    pStats->sb_nc_00.value.ui64       = pUM->intrSbNoChangeCnt[0];
    pStats->sb_nc_01.value.ui64       = pUM->intrSbNoChangeCnt[1];
    pStats->sb_nc_02.value.ui64       = pUM->intrSbNoChangeCnt[2];
    pStats->sb_nc_03.value.ui64       = pUM->intrSbNoChangeCnt[3];
    pStats->sb_nc_04.value.ui64       = pUM->intrSbNoChangeCnt[4];
    pStats->sb_nc_05.value.ui64       = pUM->intrSbNoChangeCnt[5];
    pStats->sb_nc_06.value.ui64       = pUM->intrSbNoChangeCnt[6];
    pStats->sb_nc_07.value.ui64       = pUM->intrSbNoChangeCnt[7];
    pStats->sb_nc_08.value.ui64       = pUM->intrSbNoChangeCnt[8];
    pStats->sb_nc_09.value.ui64       = pUM->intrSbNoChangeCnt[9];
    pStats->sb_nc_10.value.ui64       = pUM->intrSbNoChangeCnt[10];
    pStats->sb_nc_11.value.ui64       = pUM->intrSbNoChangeCnt[11];
    pStats->sb_nc_12.value.ui64       = pUM->intrSbNoChangeCnt[12];
    pStats->sb_nc_13.value.ui64       = pUM->intrSbNoChangeCnt[13];
    pStats->sb_nc_14.value.ui64       = pUM->intrSbNoChangeCnt[14];
    pStats->sb_nc_15.value.ui64       = pUM->intrSbNoChangeCnt[15];
    pStats->sb_nc_16.value.ui64       = pUM->intrSbNoChangeCnt[16];
    pStats->sb_poll_00.value.ui64     = pUM->intrSbPollCnt[0];
    pStats->sb_poll_01.value.ui64     = pUM->intrSbPollCnt[1];
    pStats->sb_poll_02.value.ui64     = pUM->intrSbPollCnt[2];
    pStats->sb_poll_03.value.ui64     = pUM->intrSbPollCnt[3];
    pStats->sb_poll_04.value.ui64     = pUM->intrSbPollCnt[4];
    pStats->sb_poll_05.value.ui64     = pUM->intrSbPollCnt[5];
    pStats->sb_poll_06.value.ui64     = pUM->intrSbPollCnt[6];
    pStats->sb_poll_07.value.ui64     = pUM->intrSbPollCnt[7];
    pStats->sb_poll_08.value.ui64     = pUM->intrSbPollCnt[8];
    pStats->sb_poll_09.value.ui64     = pUM->intrSbPollCnt[9];
    pStats->sb_poll_10.value.ui64     = pUM->intrSbPollCnt[10];
    pStats->sb_poll_11.value.ui64     = pUM->intrSbPollCnt[11];
    pStats->sb_poll_12.value.ui64     = pUM->intrSbPollCnt[12];
    pStats->sb_poll_13.value.ui64     = pUM->intrSbPollCnt[13];
    pStats->sb_poll_14.value.ui64     = pUM->intrSbPollCnt[14];
    pStats->sb_poll_15.value.ui64     = pUM->intrSbPollCnt[15];
    pStats->sb_poll_16.value.ui64     = pUM->intrSbPollCnt[16];
    pStats->sb_poll_nc_00.value.ui64  = pUM->intrSbPollNoChangeCnt[0];
    pStats->sb_poll_nc_01.value.ui64  = pUM->intrSbPollNoChangeCnt[1];
    pStats->sb_poll_nc_02.value.ui64  = pUM->intrSbPollNoChangeCnt[2];
    pStats->sb_poll_nc_03.value.ui64  = pUM->intrSbPollNoChangeCnt[3];
    pStats->sb_poll_nc_04.value.ui64  = pUM->intrSbPollNoChangeCnt[4];
    pStats->sb_poll_nc_05.value.ui64  = pUM->intrSbPollNoChangeCnt[5];
    pStats->sb_poll_nc_06.value.ui64  = pUM->intrSbPollNoChangeCnt[6];
    pStats->sb_poll_nc_07.value.ui64  = pUM->intrSbPollNoChangeCnt[7];
    pStats->sb_poll_nc_08.value.ui64  = pUM->intrSbPollNoChangeCnt[8];
    pStats->sb_poll_nc_09.value.ui64  = pUM->intrSbPollNoChangeCnt[9];
    pStats->sb_poll_nc_10.value.ui64  = pUM->intrSbPollNoChangeCnt[10];
    pStats->sb_poll_nc_11.value.ui64  = pUM->intrSbPollNoChangeCnt[11];
    pStats->sb_poll_nc_12.value.ui64  = pUM->intrSbPollNoChangeCnt[12];
    pStats->sb_poll_nc_13.value.ui64  = pUM->intrSbPollNoChangeCnt[13];
    pStats->sb_poll_nc_14.value.ui64  = pUM->intrSbPollNoChangeCnt[14];
    pStats->sb_poll_nc_15.value.ui64  = pUM->intrSbPollNoChangeCnt[15];
    pStats->sb_poll_nc_16.value.ui64  = pUM->intrSbPollNoChangeCnt[16];

    BNXE_LOCK_EXIT_STATS(pUM);

    return 0;
}


static int BnxeKstatL2ChipUpdate(kstat_t * kstats,
                                 int       rw)
{
    BnxeKstatL2Chip * pStats = (BnxeKstatL2Chip *)kstats->ks_data;
    um_device_t *     pUM    = (um_device_t *)kstats->ks_private;
    lm_device_t *     pLM    = (lm_device_t *)pUM;
    b10_l2_chip_statistics_t b10_l2_stats;

    if (rw == KSTAT_WRITE)
    {
        return EACCES;
    }

    BNXE_LOCK_ENTER_STATS(pUM);

    lm_stats_get_l2_chip_stats(pLM, &b10_l2_stats);

    pStats->IfHCInOctets.value.ui64                       = b10_l2_stats.IfHCInOctets;
    pStats->IfHCInBadOctets.value.ui64                    = b10_l2_stats.IfHCInBadOctets;
    pStats->IfHCOutOctets.value.ui64                      = b10_l2_stats.IfHCOutOctets;
    pStats->IfHCOutBadOctets.value.ui64                   = b10_l2_stats.IfHCOutBadOctets;
    pStats->IfHCOutPkts.value.ui64                        = b10_l2_stats.IfHCOutPkts;
    pStats->IfHCInPkts.value.ui64                         = b10_l2_stats.IfHCInPkts;
    pStats->IfHCInUcastPkts.value.ui64                    = b10_l2_stats.IfHCInUcastPkts;
    pStats->IfHCInMulticastPkts.value.ui64                = b10_l2_stats.IfHCInMulticastPkts;
    pStats->IfHCInBroadcastPkts.value.ui64                = b10_l2_stats.IfHCInBroadcastPkts;
    pStats->IfHCOutUcastPkts.value.ui64                   = b10_l2_stats.IfHCOutUcastPkts;
    pStats->IfHCOutMulticastPkts.value.ui64               = b10_l2_stats.IfHCOutMulticastPkts;
    pStats->IfHCOutBroadcastPkts.value.ui64               = b10_l2_stats.IfHCOutBroadcastPkts;
    pStats->IfHCInUcastOctets.value.ui64                  = b10_l2_stats.IfHCInUcastOctets;
    pStats->IfHCInMulticastOctets.value.ui64              = b10_l2_stats.IfHCInMulticastOctets;
    pStats->IfHCInBroadcastOctets.value.ui64              = b10_l2_stats.IfHCInBroadcastOctets;
    pStats->IfHCOutUcastOctets.value.ui64                 = b10_l2_stats.IfHCOutUcastOctets;
    pStats->IfHCOutMulticastOctets.value.ui64             = b10_l2_stats.IfHCOutMulticastOctets;
    pStats->IfHCOutBroadcastOctets.value.ui64             = b10_l2_stats.IfHCOutBroadcastOctets;
    pStats->IfHCOutDiscards.value.ui64                    = b10_l2_stats.IfHCOutDiscards;
    pStats->IfHCInFalseCarrierErrors.value.ui64           = b10_l2_stats.IfHCInFalseCarrierErrors;
    pStats->Dot3StatsInternalMacTransmitErrors.value.ui64 = b10_l2_stats.Dot3StatsInternalMacTransmitErrors;
    pStats->Dot3StatsCarrierSenseErrors.value.ui64        = b10_l2_stats.Dot3StatsCarrierSenseErrors;
    pStats->Dot3StatsFCSErrors.value.ui64                 = b10_l2_stats.Dot3StatsFCSErrors;
    pStats->Dot3StatsAlignmentErrors.value.ui64           = b10_l2_stats.Dot3StatsAlignmentErrors;
    pStats->Dot3StatsSingleCollisionFrames.value.ui64     = b10_l2_stats.Dot3StatsSingleCollisionFrames;
    pStats->Dot3StatsMultipleCollisionFrames.value.ui64   = b10_l2_stats.Dot3StatsMultipleCollisionFrames;
    pStats->Dot3StatsDeferredTransmissions.value.ui64     = b10_l2_stats.Dot3StatsDeferredTransmissions;
    pStats->Dot3StatsExcessiveCollisions.value.ui64       = b10_l2_stats.Dot3StatsExcessiveCollisions;
    pStats->Dot3StatsLateCollisions.value.ui64            = b10_l2_stats.Dot3StatsLateCollisions;
    pStats->EtherStatsCollisions.value.ui64               = b10_l2_stats.EtherStatsCollisions;
    pStats->EtherStatsFragments.value.ui64                = b10_l2_stats.EtherStatsFragments;
    pStats->EtherStatsJabbers.value.ui64                  = b10_l2_stats.EtherStatsJabbers;
    pStats->EtherStatsUndersizePkts.value.ui64            = b10_l2_stats.EtherStatsUndersizePkts;
    pStats->EtherStatsOverrsizePkts.value.ui64            = b10_l2_stats.EtherStatsOverrsizePkts;
    pStats->EtherStatsTx64Octets.value.ui64               = b10_l2_stats.EtherStatsPktsTx64Octets;
    pStats->EtherStatsTx65to127Octets.value.ui64          = b10_l2_stats.EtherStatsPktsTx65Octetsto127Octets;
    pStats->EtherStatsTx128to255Octets.value.ui64         = b10_l2_stats.EtherStatsPktsTx128Octetsto255Octets;
    pStats->EtherStatsTx256to511Octets.value.ui64         = b10_l2_stats.EtherStatsPktsTx256Octetsto511Octets;
    pStats->EtherStatsTx512to1023Octets.value.ui64        = b10_l2_stats.EtherStatsPktsTx512Octetsto1023Octets;
    pStats->EtherStatsTx1024to1522Octets.value.ui64       = b10_l2_stats.EtherStatsPktsTx1024Octetsto1522Octets;
    pStats->EtherStatsTxOver1522Octets.value.ui64         = b10_l2_stats.EtherStatsPktsTxOver1522Octets;
    pStats->XonPauseFramesReceived.value.ui64             = b10_l2_stats.XonPauseFramesReceived;
    pStats->XoffPauseFramesReceived.value.ui64            = b10_l2_stats.XoffPauseFramesReceived;
    pStats->OutXonSent.value.ui64                         = b10_l2_stats.OutXonSent;
    pStats->OutXoffSent.value.ui64                        = b10_l2_stats.OutXoffSent;
    pStats->FlowControlDone.value.ui64                    = b10_l2_stats.FlowControlDone;
    pStats->MacControlFramesReceived.value.ui64           = b10_l2_stats.MacControlFramesReceived;
    pStats->XoffStateEntered.value.ui64                   = b10_l2_stats.XoffStateEntered;
    pStats->IfInFramesL2FilterDiscards.value.ui64         = b10_l2_stats.IfInFramesL2FilterDiscards;
    pStats->IfInTTL0Discards.value.ui64                   = b10_l2_stats.IfInTTL0Discards;
    pStats->IfInxxOverflowDiscards.value.ui64             = b10_l2_stats.IfInxxOverflowDiscards;
    pStats->IfInMBUFDiscards.value.ui64                   = b10_l2_stats.IfInMBUFDiscards;
    pStats->IfInErrors.value.ui64                         = b10_l2_stats.IfInErrors;
    pStats->IfInErrorsOctets.value.ui64                   = b10_l2_stats.IfInErrorsOctets;
    pStats->IfInNoBrbBuffer.value.ui64                    = b10_l2_stats.IfInNoBrbBuffer;
    //pStats->NigBrbPacket.value.ui64                       = b10_l2_stats.Nig_brb_packet;
    //pStats->NigBrbTruncate.value.ui64                     = b10_l2_stats.Nig_brb_truncate;
    //pStats->NigFlowCtrlDiscard.value.ui64                 = b10_l2_stats.Nig_flow_ctrl_discard;
    //pStats->NigFlowCtrlOctets.value.ui64                  = b10_l2_stats.Nig_flow_ctrl_octets;
    //pStats->NigFlowCtrlPacket.value.ui64                  = b10_l2_stats.Nig_flow_ctrl_packet;
    //pStats->NigMngDiscard.value.ui64                      = b10_l2_stats.Nig_mng_discard;
    //pStats->NigMngOctetInp.value.ui64                     = b10_l2_stats.Nig_mng_octet_inp;
    //pStats->NigMngOctetOut.value.ui64                     = b10_l2_stats.Nig_mng_octet_out;
    //pStats->NigMngPacketInp.value.ui64                    = b10_l2_stats.Nig_mng_packet_inp;
    //pStats->NigMngPacketOut.value.ui64                    = b10_l2_stats.Nig_mng_packet_out;
    //pStats->NigPbfOctets.value.ui64                       = b10_l2_stats.Nig_pbf_octets;
    //pStats->NigPbfPacket.value.ui64                       = b10_l2_stats.Nig_pbf_packet;
    //pStats->NigSafcInp.value.ui64                         = b10_l2_stats.Nig_safc_inp;

    BNXE_LOCK_EXIT_STATS(pUM);

    return 0;
}


static int BnxeKstatL2DriverUpdate(kstat_t * kstats,
                                   int       rw)
{
    BnxeKstatL2Driver * pStats = (BnxeKstatL2Driver *)kstats->ks_data;
    um_device_t *       pUM    = (um_device_t *)kstats->ks_private;
    lm_device_t *       pLM    = (lm_device_t *)pUM;
    b10_l2_driver_statistics_t b10_l2_stats;

    if (rw == KSTAT_WRITE)
    {
        return EACCES;
    }

    BNXE_LOCK_ENTER_STATS(pUM);

    lm_stats_get_l2_driver_stats(pLM, &b10_l2_stats);

    pStats->RxIPv4FragCount.value.ui64    = b10_l2_stats.RxIPv4FragCount;
    pStats->RxIpCsErrorCount.value.ui64   = b10_l2_stats.RxIpCsErrorCount;
    pStats->RxTcpCsErrorCount.value.ui64  = b10_l2_stats.RxTcpCsErrorCount;
    pStats->RxLlcSnapCount.value.ui64     = b10_l2_stats.RxLlcSnapCount;
    pStats->RxPhyErrorCount.value.ui64    = b10_l2_stats.RxPhyErrorCount;
    pStats->RxIpv6ExtCount.value.ui64     = b10_l2_stats.RxIpv6ExtCount;
    pStats->TxNoL2Bd.value.ui64           = b10_l2_stats.TxNoL2Bd;
    pStats->TxNoSqWqe.value.ui64          = b10_l2_stats.TxNoSqWqe;
    pStats->TxL2AssemblyBufUse.value.ui64 = b10_l2_stats.TxL2AssemblyBufUse;

    BNXE_LOCK_EXIT_STATS(pUM);

    return 0;
}


static int BnxeKstatL2StatsUpdate(kstat_t * kstats,
                                  int       rw)
{
    BnxeKstatEthStats * pStats = (BnxeKstatEthStats *)kstats->ks_data;
    um_device_t *       pUM    = (um_device_t *)kstats->ks_private;
    lm_device_t *       pLM    = (lm_device_t *)pUM;

    if (rw == KSTAT_WRITE)
    {
        return EACCES;
    }

    BNXE_LOCK_ENTER_STATS(pUM);

    lm_get_stats(pLM, LM_STATS_FRAMES_XMITTED_OK,         (u64_t *)&pStats->txFramesOk.value.ui64);
    lm_get_stats(pLM, LM_STATS_FRAMES_RECEIVED_OK,        (u64_t *)&pStats->rxFramesOk.value.ui64);
    lm_get_stats(pLM, LM_STATS_ERRORED_TRANSMIT_CNT,      (u64_t *)&pStats->txErr.value.ui64);
    lm_get_stats(pLM, LM_STATS_ERRORED_RECEIVE_CNT,       (u64_t *)&pStats->rxErr.value.ui64);
    lm_get_stats(pLM, LM_STATS_RCV_CRC_ERROR,             (u64_t *)&pStats->rxCrcErr.value.ui64);
    lm_get_stats(pLM, LM_STATS_ALIGNMENT_ERROR,           (u64_t *)&pStats->alignErr.value.ui64);
    lm_get_stats(pLM, LM_STATS_SINGLE_COLLISION_FRAMES,   (u64_t *)&pStats->collisionsSingle.value.ui64);
    lm_get_stats(pLM, LM_STATS_MULTIPLE_COLLISION_FRAMES, (u64_t *)&pStats->collisionsMultiple.value.ui64);
    lm_get_stats(pLM, LM_STATS_FRAMES_DEFERRED,           (u64_t *)&pStats->framesDeferred.value.ui64);
    lm_get_stats(pLM, LM_STATS_MAX_COLLISIONS,            (u64_t *)&pStats->collisonsMax.value.ui64);
    lm_get_stats(pLM, LM_STATS_RCV_OVERRUN,               (u64_t *)&pStats->rxOverrun.value.ui64);
    lm_get_stats(pLM, LM_STATS_XMIT_UNDERRUN,             (u64_t *)&pStats->txOverrun.value.ui64);
    lm_get_stats(pLM, LM_STATS_UNICAST_FRAMES_XMIT,       (u64_t *)&pStats->txFramesUnicast.value.ui64);
    lm_get_stats(pLM, LM_STATS_MULTICAST_FRAMES_XMIT,     (u64_t *)&pStats->txFramesMulticast.value.ui64);
    lm_get_stats(pLM, LM_STATS_BROADCAST_FRAMES_XMIT,     (u64_t *)&pStats->txFramesBroadcast.value.ui64);
    lm_get_stats(pLM, LM_STATS_UNICAST_FRAMES_RCV,        (u64_t *)&pStats->rxFramesUnicast.value.ui64);
    lm_get_stats(pLM, LM_STATS_MULTICAST_FRAMES_RCV,      (u64_t *)&pStats->rxFramesMulticast.value.ui64);
    lm_get_stats(pLM, LM_STATS_BROADCAST_FRAMES_RCV,      (u64_t *)&pStats->rxFramesBroadcast.value.ui64);
    lm_get_stats(pLM, LM_STATS_RCV_NO_BUFFER_DROP,        (u64_t *)&pStats->rxNoBufferDrop.value.ui64);
    lm_get_stats(pLM, LM_STATS_BYTES_RCV,                 (u64_t *)&pStats->rxBytes.value.ui64);
    lm_get_stats(pLM, LM_STATS_BYTES_XMIT,                (u64_t *)&pStats->txBytes.value.ui64);
    lm_get_stats(pLM, LM_STATS_IP4_OFFLOAD,               (u64_t *)&pStats->offloadIP4.value.ui64);
    lm_get_stats(pLM, LM_STATS_TCP_OFFLOAD,               (u64_t *)&pStats->offloadTCP.value.ui64);
    lm_get_stats(pLM, LM_STATS_IF_IN_DISCARDS,            (u64_t *)&pStats->ifInDiscards.value.ui64);
    lm_get_stats(pLM, LM_STATS_IF_IN_ERRORS,              (u64_t *)&pStats->ifInErrors.value.ui64);
    lm_get_stats(pLM, LM_STATS_IF_OUT_ERRORS,             (u64_t *)&pStats->ifOutErrors.value.ui64);
    lm_get_stats(pLM, LM_STATS_IP6_OFFLOAD,               (u64_t *)&pStats->offloadIP6.value.ui64);
    lm_get_stats(pLM, LM_STATS_TCP6_OFFLOAD,              (u64_t *)&pStats->offloadTCP6.value.ui64);
    lm_get_stats(pLM, LM_STATS_XMIT_DISCARDS,             (u64_t *)&pStats->txDiscards.value.ui64);
    lm_get_stats(pLM, LM_STATS_DIRECTED_BYTES_RCV,        (u64_t *)&pStats->rxBytesUnicast.value.ui64);
    lm_get_stats(pLM, LM_STATS_MULTICAST_BYTES_RCV,       (u64_t *)&pStats->rxBytesMulticast.value.ui64);
    lm_get_stats(pLM, LM_STATS_BROADCAST_BYTES_RCV,       (u64_t *)&pStats->rxBytesBroadcast.value.ui64);
    lm_get_stats(pLM, LM_STATS_DIRECTED_BYTES_XMIT,       (u64_t *)&pStats->txBytesUnicast.value.ui64);
    lm_get_stats(pLM, LM_STATS_MULTICAST_BYTES_XMIT,      (u64_t *)&pStats->txBytesMulticast.value.ui64);
    lm_get_stats(pLM, LM_STATS_BROADCAST_BYTES_XMIT,      (u64_t *)&pStats->txBytesBroadcast.value.ui64);

    BNXE_LOCK_EXIT_STATS(pUM);

    return 0;
}


static int BnxeKstatFcoeUpdate(kstat_t * kstats,
                               int       rw)
{
    BnxeKstatFcoe * pStats = (BnxeKstatFcoe *)kstats->ks_data;
    um_device_t * pUM      = (um_device_t *)kstats->ks_private;
    lm_device_t * pLM      = (lm_device_t *)pUM;
    char buf[17]; /* 16 max for kstat string */

    if (rw == KSTAT_WRITE)
    {
        return EACCES;
    }

    BNXE_LOCK_ENTER_STATS(pUM);

    snprintf(buf, sizeof(buf), "%016p", (void *)pUM->fcoe.pDev);
    strncpy(pStats->pdev_hi.value.c, &buf[0], 8);
    pStats->pdev_hi.value.c[8] = 0;
    strncpy(pStats->pdev_lo.value.c, &buf[8], 8);
    pStats->pdev_lo.value.c[8] = 0;

    snprintf(pStats->macAddr.value.c, 16, "%02x%02x%02x%02x%02x%02x",
             pLM->hw_info.fcoe_mac_addr[0], pLM->hw_info.fcoe_mac_addr[1],
             pLM->hw_info.fcoe_mac_addr[2], pLM->hw_info.fcoe_mac_addr[3],
             pLM->hw_info.fcoe_mac_addr[4], pLM->hw_info.fcoe_mac_addr[5]);

    pStats->initWqeTx.value.ui64           = pUM->fcoe.stats.initWqeTx;
    pStats->initWqeTxErr.value.ui64        = pUM->fcoe.stats.initWqeTxErr;
    pStats->initCqeRx.value.ui64           = pUM->fcoe.stats.initCqeRx;
    pStats->initCqeRxErr.value.ui64        = pUM->fcoe.stats.initCqeRxErr;
    pStats->offloadConnWqeTx.value.ui64    = pUM->fcoe.stats.offloadConnWqeTx;
    pStats->offloadConnWqeTxErr.value.ui64 = pUM->fcoe.stats.offloadConnWqeTxErr;
    pStats->offloadConnCqeRx.value.ui64    = pUM->fcoe.stats.offloadConnCqeRx;
    pStats->offloadConnCqeRxErr.value.ui64 = pUM->fcoe.stats.offloadConnCqeRxErr;
    pStats->enableConnWqeTx.value.ui64     = pUM->fcoe.stats.enableConnWqeTx;
    pStats->enableConnWqeTxErr.value.ui64  = pUM->fcoe.stats.enableConnWqeTxErr;
    pStats->enableConnCqeRx.value.ui64     = pUM->fcoe.stats.enableConnCqeRx;
    pStats->enableConnCqeRxErr.value.ui64  = pUM->fcoe.stats.enableConnCqeRxErr;
    pStats->disableConnWqeTx.value.ui64    = pUM->fcoe.stats.disableConnWqeTx;
    pStats->disableConnWqeTxErr.value.ui64 = pUM->fcoe.stats.disableConnWqeTxErr;
    pStats->disableConnCqeRx.value.ui64    = pUM->fcoe.stats.disableConnCqeRx;
    pStats->disableConnCqeRxErr.value.ui64 = pUM->fcoe.stats.disableConnCqeRxErr;
    pStats->destroyConnWqeTx.value.ui64    = pUM->fcoe.stats.destroyConnWqeTx;
    pStats->destroyConnWqeTxErr.value.ui64 = pUM->fcoe.stats.destroyConnWqeTxErr;
    pStats->destroyConnCqeRx.value.ui64    = pUM->fcoe.stats.destroyConnCqeRx;
    pStats->destroyConnCqeRxErr.value.ui64 = pUM->fcoe.stats.destroyConnCqeRxErr;
    pStats->destroyWqeTx.value.ui64        = pUM->fcoe.stats.destroyWqeTx;
    pStats->destroyWqeTxErr.value.ui64     = pUM->fcoe.stats.destroyWqeTxErr;
    pStats->destroyCqeRx.value.ui64        = pUM->fcoe.stats.destroyCqeRx;
    pStats->destroyCqeRxErr.value.ui64     = pUM->fcoe.stats.destroyCqeRxErr;
    pStats->compRequestCqeRx.value.ui64    = pUM->fcoe.stats.compRequestCqeRx;
    pStats->compRequestCqeRxErr.value.ui64 = pUM->fcoe.stats.compRequestCqeRxErr;
    pStats->statWqeTx.value.ui64           = pUM->fcoe.stats.statWqeTx;
    pStats->statWqeTxErr.value.ui64        = pUM->fcoe.stats.statWqeTxErr;
    pStats->statCqeRx.value.ui64           = pUM->fcoe.stats.statCqeRx;
    pStats->statCqeRxErr.value.ui64        = pUM->fcoe.stats.statCqeRxErr;

    BNXE_LOCK_EXIT_STATS(pUM);

    return 0;
}


static int BnxeKstatRxRingUpdate(kstat_t * kstats,
                                 int       rw)
{
    BnxeKstatRxq * pStats = (BnxeKstatRxq *)kstats->ks_data;
    KstatRingMap * pMap   = (KstatRingMap *)kstats->ks_private;
    um_device_t *  pUM    = (um_device_t *)pMap->pUM;
    int            idx    = pMap->idx;
    lm_device_t *  pLM    = (lm_device_t *)pUM;

    if (rw == KSTAT_WRITE)
    {
        return EACCES;
    }

    BNXE_LOCK_ENTER_STATS(pUM);

    pStats->rxFreeDesc.value.ui64     = s_list_entry_cnt(&LM_RXQ(pLM, idx).free_descq);
    pStats->rxActiveDesc.value.ui64   = s_list_entry_cnt(&LM_RXQ(pLM, idx).active_descq);
    pStats->rxDonePkts.value.ui64     = s_list_entry_cnt(&pUM->rxq[idx].doneRxQ);
    pStats->rxWaitingPkts.value.ui64  = s_list_entry_cnt(&pUM->rxq[idx].waitRxQ);
    pStats->rxBDsAvail.value.ui64     = LM_RCQ(pLM, idx).bd_chain.bd_left;
    pStats->rxDiscards.value.ui64     = pUM->rxq[idx].rxDiscards;
    pStats->rxCopied.value.ui64       = pUM->rxq[idx].rxCopied;
    pStats->rxBufUpInStack.value.ui64 = pUM->rxq[idx].rxBufUpInStack;
    pStats->rxLowWater.value.ui64     = pUM->rxq[idx].rxLowWater;
    pStats->genNumber.value.ui64      = pUM->rxq[idx].genNumber;
    pStats->inPollMode.value.ui64     = pUM->rxq[idx].inPollMode;
    pStats->intrDisableCnt.value.ui64 = pUM->rxq[idx].intrDisableCnt;
    pStats->intrEnableCnt.value.ui64  = pUM->rxq[idx].intrEnableCnt;
    pStats->pollCnt.value.ui64        = pUM->rxq[idx].pollCnt;

    BNXE_LOCK_EXIT_STATS(pUM);

    return 0;
}


static int BnxeKstatTxRingUpdate(kstat_t * kstats,
                                 int       rw)
{
    BnxeKstatTxq * pStats = (BnxeKstatTxq *)kstats->ks_data;
    KstatRingMap * pMap   = (KstatRingMap *)kstats->ks_private;
    um_device_t *  pUM    = (um_device_t *)pMap->pUM;
    int            idx    = pMap->idx;
    lm_device_t *  pLM    = (lm_device_t *)pUM;

    if (rw == KSTAT_WRITE)
    {
        return EACCES;
    }

    BNXE_LOCK_ENTER_STATS(pUM);

    pStats->txSentPkts.value.ui64    = s_list_entry_cnt(&pUM->txq[idx].sentTxQ);
    pStats->txFreeDesc.value.ui64    = s_list_entry_cnt(&pUM->txq[idx].freeTxDescQ);
    pStats->txWaitingPkts.value.ui64 = s_list_entry_cnt(&pUM->txq[idx].waitTxDescQ);
    pStats->txBDsAvail.value.ui64    = pLM->tx_info.chain[idx].bd_chain.bd_left;
    pStats->txFailed.value.ui64      = pUM->txq[idx].txFailed;
    pStats->txDiscards.value.ui64    = pUM->txq[idx].txDiscards;
    pStats->txRecycle.value.ui64     = pUM->txq[idx].txRecycle;
    pStats->txCopied.value.ui64      = pUM->txq[idx].txCopied;
    pStats->txBlocked.value.ui64     = pUM->txq[idx].txBlocked;
    pStats->txWait.value.ui64        = pUM->txq[idx].txWait;
    pStats->txLowWater.value.ui64    = pUM->txq[idx].txLowWater;

    BNXE_LOCK_EXIT_STATS(pUM);

    return 0;
}


boolean_t BnxeKstatInitRxQ(um_device_t * pUM,
                           int           idx)
{
    char buf[32];

    BnxeKstatRxq * pStatsRxq;
#define BNXE_KSTAT_RXQ(f, t)  kstat_named_init(&pStatsRxq->f, #f, t)

    snprintf(buf, sizeof(buf), "rxq%d", idx);

    if ((pUM->kstats_rxq[idx] = kstat_create("bnxe",
                                             pUM->instance,
                                             buf,
                                             "net",
                                             KSTAT_TYPE_NAMED,
                                             BNXE_KSTAT_RXQ_SIZE,
                                             0)) == NULL)
    {
        BnxeLogWarn(pUM, "Failed to create rxq%d kstat", idx);
        return B_FALSE;
    }

    pStatsRxq = (BnxeKstatRxq *)pUM->kstats_rxq[idx]->ks_data;

    BNXE_KSTAT_RXQ(rxFreeDesc,     KSTAT_DATA_UINT64);
    BNXE_KSTAT_RXQ(rxActiveDesc,   KSTAT_DATA_UINT64);
    BNXE_KSTAT_RXQ(rxDonePkts,     KSTAT_DATA_UINT64);
    BNXE_KSTAT_RXQ(rxWaitingPkts,  KSTAT_DATA_UINT64);
    BNXE_KSTAT_RXQ(rxBDsAvail,     KSTAT_DATA_UINT64);
    BNXE_KSTAT_RXQ(rxDiscards,     KSTAT_DATA_UINT64);
    BNXE_KSTAT_RXQ(rxCopied,       KSTAT_DATA_UINT64);
    BNXE_KSTAT_RXQ(rxLowWater,     KSTAT_DATA_UINT64);
    BNXE_KSTAT_RXQ(rxBufUpInStack, KSTAT_DATA_UINT64);
    BNXE_KSTAT_RXQ(genNumber,      KSTAT_DATA_UINT64);
    BNXE_KSTAT_RXQ(inPollMode,     KSTAT_DATA_UINT64);
    BNXE_KSTAT_RXQ(intrDisableCnt, KSTAT_DATA_UINT64);
    BNXE_KSTAT_RXQ(intrEnableCnt,  KSTAT_DATA_UINT64);
    BNXE_KSTAT_RXQ(pollCnt,        KSTAT_DATA_UINT64);

    pUM->kstats_rxq_map[idx].idx = idx;
    pUM->kstats_rxq_map[idx].pUM = pUM;

    pUM->kstats_rxq[idx]->ks_update  = BnxeKstatRxRingUpdate;
    pUM->kstats_rxq[idx]->ks_private = (void *)&pUM->kstats_rxq_map[idx];

    kstat_install(pUM->kstats_rxq[idx]);

    return B_TRUE;
}


boolean_t BnxeKstatInitTxQ(um_device_t * pUM,
                           int           idx)
{
    char buf[32];

    BnxeKstatTxq * pStatsTxq;
#define BNXE_KSTAT_TXQ(f, t)  kstat_named_init(&pStatsTxq->f, #f, t)

    snprintf(buf, sizeof(buf), "txq%d", idx);

    if ((pUM->kstats_txq[idx] = kstat_create("bnxe",
                                             pUM->instance,
                                             buf,
                                             "net",
                                             KSTAT_TYPE_NAMED,
                                             BNXE_KSTAT_TXQ_SIZE,
                                             0)) == NULL)
    {
        BnxeLogWarn(pUM, "Failed to create txq%d kstat", idx);
        return B_FALSE;
    }

    pStatsTxq = (BnxeKstatTxq *)pUM->kstats_txq[idx]->ks_data;

    BNXE_KSTAT_TXQ(txSentPkts,    KSTAT_DATA_UINT64);
    BNXE_KSTAT_TXQ(txFreeDesc,    KSTAT_DATA_UINT64);
    BNXE_KSTAT_TXQ(txWaitingPkts, KSTAT_DATA_UINT64);
    BNXE_KSTAT_TXQ(txBDsAvail,    KSTAT_DATA_UINT64);
    BNXE_KSTAT_TXQ(txFailed,      KSTAT_DATA_UINT64);
    BNXE_KSTAT_TXQ(txDiscards,    KSTAT_DATA_UINT64);
    BNXE_KSTAT_TXQ(txRecycle,     KSTAT_DATA_UINT64);
    BNXE_KSTAT_TXQ(txCopied,      KSTAT_DATA_UINT64);
    BNXE_KSTAT_TXQ(txBlocked,     KSTAT_DATA_UINT64);
    BNXE_KSTAT_TXQ(txWait,        KSTAT_DATA_UINT64);
    BNXE_KSTAT_TXQ(txLowWater,    KSTAT_DATA_UINT64);

    pUM->kstats_txq_map[idx].idx = idx;
    pUM->kstats_txq_map[idx].pUM = pUM;

    pUM->kstats_txq[idx]->ks_update  = BnxeKstatTxRingUpdate;
    pUM->kstats_txq[idx]->ks_private = (void *)&pUM->kstats_txq_map[idx];

    kstat_install(pUM->kstats_txq[idx]);

    return B_TRUE;
}


boolean_t BnxeKstatInit(um_device_t * pUM)
{
    lm_device_t * pLM = (lm_device_t *)pUM;
    char buf[32];
    int idx;

    BnxeKstat *         pStats;
    BnxeKstatLink *     pStatsLink;
    BnxeKstatIntr *     pStatsIntr;
    BnxeKstatL2Chip *   pStatsL2Chip;
    BnxeKstatL2Driver * pStatsL2Driver;
    BnxeKstatEthStats * pStatsL2Stats;
    BnxeKstatFcoe *     pStatsFcoe;
#define BNXE_KSTAT(f, t)            kstat_named_init(&pStats->f, #f, t)
#define BNXE_KSTAT_LINK(f, t)       kstat_named_init(&pStatsLink->f, #f, t)
#define BNXE_KSTAT_INTR(f, t)       kstat_named_init(&pStatsIntr->f, #f, t)
#define BNXE_KSTAT_L2_CHIP(f, t)    kstat_named_init(&pStatsL2Chip->f, #f, t)
#define BNXE_KSTAT_L2_DRIVER(f, t)  kstat_named_init(&pStatsL2Driver->f, #f, t)
#define BNXE_KSTAT_L2_STATS(f, t)   kstat_named_init(&pStatsL2Stats->f, #f, t)
#define BNXE_KSTAT_FCOE(f, t)       kstat_named_init(&pStatsFcoe->f, #f, t)

    /****************************************************************/

    if ((pUM->kstats = kstat_create("bnxe",
                                    pUM->instance,
                                    "stats",
                                    "net",
                                    KSTAT_TYPE_NAMED,
                                    BNXE_KSTAT_SIZE,
                                    0)) == NULL)
    {
        BnxeLogWarn(pUM, "Failed to create kstat");
        return B_FALSE;
    }

    pStats = (BnxeKstat *)pUM->kstats->ks_data;

    BNXE_KSTAT(umdev_hi,              KSTAT_DATA_CHAR);
    BNXE_KSTAT(umdev_lo,              KSTAT_DATA_CHAR);
    BNXE_KSTAT(version,               KSTAT_DATA_CHAR);
    BNXE_KSTAT(versionFW,             KSTAT_DATA_CHAR);
    BNXE_KSTAT(versionBC,             KSTAT_DATA_CHAR);
    BNXE_KSTAT(chipName,              KSTAT_DATA_CHAR);
    BNXE_KSTAT(chipID,                KSTAT_DATA_CHAR);
    BNXE_KSTAT(multiFunction,         KSTAT_DATA_UINT64);
    BNXE_KSTAT(multiFunctionVnics,    KSTAT_DATA_UINT64);
    BNXE_KSTAT(macAddr,               KSTAT_DATA_CHAR);
    BNXE_KSTAT(hwInitDone,            KSTAT_DATA_UINT64);
    BNXE_KSTAT(clientsHw,             KSTAT_DATA_CHAR);
    BNXE_KSTAT(clientsDevi,           KSTAT_DATA_CHAR);
    BNXE_KSTAT(clientsBound,          KSTAT_DATA_CHAR);
    BNXE_KSTAT(txMsgPullUp,           KSTAT_DATA_UINT64);
    BNXE_KSTAT(intrAlloc,             KSTAT_DATA_CHAR);
    BNXE_KSTAT(intrFired,             KSTAT_DATA_UINT64);
    BNXE_KSTAT(timerFired,            KSTAT_DATA_UINT64);
    BNXE_KSTAT(timerReply,            KSTAT_DATA_UINT64);
    BNXE_KSTAT(timerNoReplyTotal,     KSTAT_DATA_UINT64);
    BNXE_KSTAT(timerNoReplyCurrent,   KSTAT_DATA_UINT64);
    BNXE_KSTAT(timerDone,             KSTAT_DATA_UINT64);
    BNXE_KSTAT(workQueueInstCnt,      KSTAT_DATA_UINT64);
    BNXE_KSTAT(workItemInstQueued,    KSTAT_DATA_UINT64);
    BNXE_KSTAT(workItemInstError,     KSTAT_DATA_UINT64);
    BNXE_KSTAT(workItemInstComplete,  KSTAT_DATA_UINT64);
    BNXE_KSTAT(workQueueDelayCnt,     KSTAT_DATA_UINT64);
    BNXE_KSTAT(workItemDelayQueued,   KSTAT_DATA_UINT64);
    BNXE_KSTAT(workItemDelayError,    KSTAT_DATA_UINT64);
    BNXE_KSTAT(workItemDelayComplete, KSTAT_DATA_UINT64);
    BNXE_KSTAT(memAllocBlocks,        KSTAT_DATA_UINT64);
    BNXE_KSTAT(memAllocDMAs,          KSTAT_DATA_UINT64);
    BNXE_KSTAT(memAllocBARs,          KSTAT_DATA_UINT64);

    pUM->kstats->ks_update  = BnxeKstatUpdate;
    pUM->kstats->ks_private = (void *)pUM;

    kstat_install(pUM->kstats);

    /****************************************************************/

    if ((pUM->kstatsLink = kstat_create("bnxe",
                                        pUM->instance,
                                        "link",
                                        "net",
                                        KSTAT_TYPE_NAMED,
                                        BNXE_KSTAT_LINK_SIZE,
                                        0)) == NULL)
    {
        BnxeLogWarn(pUM, "Failed to create link kstat");
        return B_FALSE;
    }

    pStatsLink = (BnxeKstatLink *)pUM->kstatsLink->ks_data;

    BNXE_KSTAT_LINK(clients, KSTAT_DATA_CHAR);
    BNXE_KSTAT_LINK(uptime,  KSTAT_DATA_UINT64);
    BNXE_KSTAT_LINK(mtuL2,   KSTAT_DATA_UINT64);
    BNXE_KSTAT_LINK(mtuFCOE, KSTAT_DATA_UINT64);
    BNXE_KSTAT_LINK(speed,   KSTAT_DATA_UINT64);
    BNXE_KSTAT_LINK(link,    KSTAT_DATA_UINT64);
    BNXE_KSTAT_LINK(duplex,  KSTAT_DATA_UINT64);
    BNXE_KSTAT_LINK(pauseRx, KSTAT_DATA_UINT64);
    BNXE_KSTAT_LINK(pauseTx, KSTAT_DATA_UINT64);

    pUM->kstatsLink->ks_update  = BnxeKstatLinkUpdate;
    pUM->kstatsLink->ks_private = (void *)pUM;

    kstat_install(pUM->kstatsLink);

    /****************************************************************/

    if ((pUM->kstatsIntr = kstat_create("bnxe",
                                        pUM->instance,
                                        "intr",
                                        "net",
                                        KSTAT_TYPE_NAMED,
                                        BNXE_KSTAT_INTR_SIZE,
                                        0)) == NULL)
    {
        BnxeLogWarn(pUM, "Failed to create intr kstat");
        return B_FALSE;
    }

    pStatsIntr = (BnxeKstatIntr *)pUM->kstatsIntr->ks_data;

    BNXE_KSTAT_INTR(intrAlloc,      KSTAT_DATA_CHAR);
    BNXE_KSTAT_INTR(intrFired,      KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(intrWrongState, KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(intrInDisabled, KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(intrZeroStatus, KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_00,          KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_01,          KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_02,          KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_03,          KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_04,          KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_05,          KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_06,          KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_07,          KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_08,          KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_09,          KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_10,          KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_11,          KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_12,          KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_13,          KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_14,          KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_15,          KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_16,          KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_nc_00,       KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_nc_01,       KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_nc_02,       KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_nc_03,       KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_nc_04,       KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_nc_05,       KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_nc_06,       KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_nc_07,       KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_nc_08,       KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_nc_09,       KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_nc_10,       KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_nc_11,       KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_nc_12,       KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_nc_13,       KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_nc_14,       KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_nc_15,       KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_nc_16,       KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_poll_00,     KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_poll_01,     KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_poll_02,     KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_poll_03,     KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_poll_04,     KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_poll_05,     KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_poll_06,     KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_poll_07,     KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_poll_08,     KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_poll_09,     KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_poll_10,     KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_poll_11,     KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_poll_12,     KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_poll_13,     KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_poll_14,     KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_poll_15,     KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_poll_16,     KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_poll_nc_00,  KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_poll_nc_01,  KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_poll_nc_02,  KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_poll_nc_03,  KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_poll_nc_04,  KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_poll_nc_05,  KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_poll_nc_06,  KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_poll_nc_07,  KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_poll_nc_08,  KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_poll_nc_09,  KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_poll_nc_10,  KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_poll_nc_11,  KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_poll_nc_12,  KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_poll_nc_13,  KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_poll_nc_14,  KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_poll_nc_15,  KSTAT_DATA_UINT64);
    BNXE_KSTAT_INTR(sb_poll_nc_16,  KSTAT_DATA_UINT64);

    pUM->kstatsIntr->ks_update  = BnxeKstatIntrUpdate;
    pUM->kstatsIntr->ks_private = (void *)pUM;

    kstat_install(pUM->kstatsIntr);

    /****************************************************************/

    if ((pUM->kstatsL2Chip = kstat_create("bnxe",
                                          pUM->instance,
                                          "l2chip",
                                          "net",
                                          KSTAT_TYPE_NAMED,
                                          BNXE_KSTAT_L2_CHIP_SIZE,
                                          0)) == NULL)
    {
        BnxeLogWarn(pUM, "Failed to create l2chip kstat");
        return B_FALSE;
    }

    pStatsL2Chip = (BnxeKstatL2Chip *)pUM->kstatsL2Chip->ks_data;

    BNXE_KSTAT_L2_CHIP(IfHCInOctets,                       KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_CHIP(IfHCInBadOctets,                    KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_CHIP(IfHCOutOctets,                      KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_CHIP(IfHCOutBadOctets,                   KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_CHIP(IfHCOutPkts,                        KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_CHIP(IfHCInPkts,                         KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_CHIP(IfHCInUcastPkts,                    KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_CHIP(IfHCInMulticastPkts,                KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_CHIP(IfHCInBroadcastPkts,                KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_CHIP(IfHCOutUcastPkts,                   KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_CHIP(IfHCOutMulticastPkts,               KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_CHIP(IfHCOutBroadcastPkts,               KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_CHIP(IfHCInUcastOctets,                  KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_CHIP(IfHCInMulticastOctets,              KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_CHIP(IfHCInBroadcastOctets,              KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_CHIP(IfHCOutUcastOctets,                 KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_CHIP(IfHCOutMulticastOctets,             KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_CHIP(IfHCOutBroadcastOctets,             KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_CHIP(IfHCOutDiscards,                    KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_CHIP(IfHCInFalseCarrierErrors,           KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_CHIP(Dot3StatsInternalMacTransmitErrors, KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_CHIP(Dot3StatsCarrierSenseErrors,        KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_CHIP(Dot3StatsFCSErrors,                 KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_CHIP(Dot3StatsAlignmentErrors,           KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_CHIP(Dot3StatsSingleCollisionFrames,     KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_CHIP(Dot3StatsMultipleCollisionFrames,   KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_CHIP(Dot3StatsDeferredTransmissions,     KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_CHIP(Dot3StatsExcessiveCollisions,       KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_CHIP(Dot3StatsLateCollisions,            KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_CHIP(EtherStatsCollisions,               KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_CHIP(EtherStatsFragments,                KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_CHIP(EtherStatsJabbers,                  KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_CHIP(EtherStatsUndersizePkts,            KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_CHIP(EtherStatsOverrsizePkts,            KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_CHIP(EtherStatsTx64Octets,               KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_CHIP(EtherStatsTx65to127Octets,          KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_CHIP(EtherStatsTx128to255Octets,         KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_CHIP(EtherStatsTx256to511Octets,         KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_CHIP(EtherStatsTx512to1023Octets,        KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_CHIP(EtherStatsTx1024to1522Octets,       KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_CHIP(EtherStatsTxOver1522Octets,         KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_CHIP(XonPauseFramesReceived,             KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_CHIP(XoffPauseFramesReceived,            KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_CHIP(OutXonSent,                         KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_CHIP(OutXoffSent,                        KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_CHIP(FlowControlDone,                    KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_CHIP(MacControlFramesReceived,           KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_CHIP(XoffStateEntered,                   KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_CHIP(IfInFramesL2FilterDiscards,         KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_CHIP(IfInTTL0Discards,                   KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_CHIP(IfInxxOverflowDiscards,             KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_CHIP(IfInMBUFDiscards,                   KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_CHIP(IfInErrors,                         KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_CHIP(IfInErrorsOctets,                   KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_CHIP(IfInNoBrbBuffer,                    KSTAT_DATA_UINT64);
    //BNXE_KSTAT_L2_CHIP(NigBrbPacket,                       KSTAT_DATA_UINT64);
    //BNXE_KSTAT_L2_CHIP(NigBrbTruncate,                     KSTAT_DATA_UINT64);
    //BNXE_KSTAT_L2_CHIP(NigFlowCtrlDiscard,                 KSTAT_DATA_UINT64);
    //BNXE_KSTAT_L2_CHIP(NigFlowCtrlOctets,                  KSTAT_DATA_UINT64);
    //BNXE_KSTAT_L2_CHIP(NigFlowCtrlPacket,                  KSTAT_DATA_UINT64);
    //BNXE_KSTAT_L2_CHIP(NigMngDiscard,                      KSTAT_DATA_UINT64);
    //BNXE_KSTAT_L2_CHIP(NigMngOctetInp,                     KSTAT_DATA_UINT64);
    //BNXE_KSTAT_L2_CHIP(NigMngOctetOut,                     KSTAT_DATA_UINT64);
    //BNXE_KSTAT_L2_CHIP(NigMngPacketInp,                    KSTAT_DATA_UINT64);
    //BNXE_KSTAT_L2_CHIP(NigMngPacketOut,                    KSTAT_DATA_UINT64);
    //BNXE_KSTAT_L2_CHIP(NigPbfOctets,                       KSTAT_DATA_UINT64);
    //BNXE_KSTAT_L2_CHIP(NigPbfPacket,                       KSTAT_DATA_UINT64);
    //BNXE_KSTAT_L2_CHIP(NigSafcInp,                         KSTAT_DATA_UINT64);

    pUM->kstatsL2Chip->ks_update  = BnxeKstatL2ChipUpdate;
    pUM->kstatsL2Chip->ks_private = (void *)pUM;

    kstat_install(pUM->kstatsL2Chip);

    /****************************************************************/

    if ((pUM->kstatsL2Driver = kstat_create("bnxe",
                                            pUM->instance,
                                            "l2driver",
                                            "net",
                                            KSTAT_TYPE_NAMED,
                                            BNXE_KSTAT_L2_DRIVER_SIZE,
                                            0)) == NULL)
    {
        BnxeLogWarn(pUM, "Failed to create l2driver kstat");
        return B_FALSE;
    }

    pStatsL2Driver = (BnxeKstatL2Driver *)pUM->kstatsL2Driver->ks_data;

    BNXE_KSTAT_L2_DRIVER(RxIPv4FragCount,    KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_DRIVER(RxIpCsErrorCount,   KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_DRIVER(RxTcpCsErrorCount,  KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_DRIVER(RxLlcSnapCount,     KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_DRIVER(RxPhyErrorCount,    KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_DRIVER(RxIpv6ExtCount,     KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_DRIVER(TxNoL2Bd,           KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_DRIVER(TxNoSqWqe,          KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_DRIVER(TxL2AssemblyBufUse, KSTAT_DATA_UINT64);

    pUM->kstatsL2Driver->ks_update  = BnxeKstatL2DriverUpdate;
    pUM->kstatsL2Driver->ks_private = (void *)pUM;

    kstat_install(pUM->kstatsL2Driver);

    /****************************************************************/

    if ((pUM->kstatsL2Stats = kstat_create("bnxe",
                                           pUM->instance,
                                           "l2stats",
                                           "net",
                                           KSTAT_TYPE_NAMED,
                                           BNXE_KSTAT_ETH_STATS_SIZE,
                                           0)) == NULL)
    {
        BnxeLogWarn(pUM, "Failed to create l2stats kstat");
        return B_FALSE;
    }

    pStatsL2Stats = (BnxeKstatEthStats *)pUM->kstatsL2Stats->ks_data;

    BNXE_KSTAT_L2_STATS(txFramesOk,         KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_STATS(rxFramesOk,         KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_STATS(txErr,              KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_STATS(rxErr,              KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_STATS(rxCrcErr,           KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_STATS(alignErr,           KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_STATS(collisionsSingle,   KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_STATS(collisionsMultiple, KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_STATS(framesDeferred,     KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_STATS(collisonsMax,       KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_STATS(rxOverrun,          KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_STATS(txOverrun,          KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_STATS(txFramesUnicast,    KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_STATS(txFramesMulticast,  KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_STATS(txFramesBroadcast,  KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_STATS(rxFramesUnicast,    KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_STATS(rxFramesMulticast,  KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_STATS(rxFramesBroadcast,  KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_STATS(rxNoBufferDrop,     KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_STATS(rxBytes,            KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_STATS(txBytes,            KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_STATS(offloadIP4,         KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_STATS(offloadTCP,         KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_STATS(ifInDiscards,       KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_STATS(ifInErrors,         KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_STATS(ifOutErrors,        KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_STATS(offloadIP6,         KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_STATS(offloadTCP6,        KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_STATS(txDiscards,         KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_STATS(rxBytesUnicast,     KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_STATS(rxBytesMulticast,   KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_STATS(rxBytesBroadcast,   KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_STATS(txBytesUnicast,     KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_STATS(txBytesMulticast,   KSTAT_DATA_UINT64);
    BNXE_KSTAT_L2_STATS(txBytesBroadcast,   KSTAT_DATA_UINT64);

    pUM->kstatsL2Stats->ks_update  = BnxeKstatL2StatsUpdate;
    pUM->kstatsL2Stats->ks_private = (void *)pUM;

    kstat_install(pUM->kstatsL2Stats);

    /****************************************************************/

    if (BNXE_FCOE(pUM))
    {
        if ((pUM->kstatsFcoe = kstat_create("bnxe",
                                            pUM->instance,
                                            "fcoe",
                                            "net",
                                            KSTAT_TYPE_NAMED,
                                            BNXE_KSTAT_FCOE_SIZE,
                                            0)) == NULL)
        {
            BnxeLogWarn(pUM, "Failed to create fcoe kstat");
            BnxeKstatFini(pUM);
            return B_FALSE;
        }

        pStatsFcoe = (BnxeKstatFcoe *)pUM->kstatsFcoe->ks_data;

        BNXE_KSTAT_FCOE(pdev_hi,             KSTAT_DATA_CHAR);
        BNXE_KSTAT_FCOE(pdev_lo,             KSTAT_DATA_CHAR);
        BNXE_KSTAT_FCOE(macAddr,             KSTAT_DATA_CHAR);
        BNXE_KSTAT_FCOE(initWqeTx,           KSTAT_DATA_UINT64);
        BNXE_KSTAT_FCOE(initWqeTxErr,        KSTAT_DATA_UINT64);
        BNXE_KSTAT_FCOE(initCqeRx,           KSTAT_DATA_UINT64);
        BNXE_KSTAT_FCOE(initCqeRxErr,        KSTAT_DATA_UINT64);
        BNXE_KSTAT_FCOE(offloadConnWqeTx,    KSTAT_DATA_UINT64);
        BNXE_KSTAT_FCOE(offloadConnWqeTxErr, KSTAT_DATA_UINT64);
        BNXE_KSTAT_FCOE(offloadConnCqeRx,    KSTAT_DATA_UINT64);
        BNXE_KSTAT_FCOE(offloadConnCqeRxErr, KSTAT_DATA_UINT64);
        BNXE_KSTAT_FCOE(enableConnWqeTx,     KSTAT_DATA_UINT64);
        BNXE_KSTAT_FCOE(enableConnWqeTxErr,  KSTAT_DATA_UINT64);
        BNXE_KSTAT_FCOE(enableConnCqeRx,     KSTAT_DATA_UINT64);
        BNXE_KSTAT_FCOE(enableConnCqeRxErr,  KSTAT_DATA_UINT64);
        BNXE_KSTAT_FCOE(disableConnWqeTx,    KSTAT_DATA_UINT64);
        BNXE_KSTAT_FCOE(disableConnWqeTxErr, KSTAT_DATA_UINT64);
        BNXE_KSTAT_FCOE(disableConnCqeRx,    KSTAT_DATA_UINT64);
        BNXE_KSTAT_FCOE(disableConnCqeRxErr, KSTAT_DATA_UINT64);
        BNXE_KSTAT_FCOE(destroyConnWqeTx,    KSTAT_DATA_UINT64);
        BNXE_KSTAT_FCOE(destroyConnWqeTxErr, KSTAT_DATA_UINT64);
        BNXE_KSTAT_FCOE(destroyConnCqeRx,    KSTAT_DATA_UINT64);
        BNXE_KSTAT_FCOE(destroyConnCqeRxErr, KSTAT_DATA_UINT64);
        BNXE_KSTAT_FCOE(destroyWqeTx,        KSTAT_DATA_UINT64);
        BNXE_KSTAT_FCOE(destroyWqeTxErr,     KSTAT_DATA_UINT64);
        BNXE_KSTAT_FCOE(destroyCqeRx,        KSTAT_DATA_UINT64);
        BNXE_KSTAT_FCOE(destroyCqeRxErr,     KSTAT_DATA_UINT64);
        BNXE_KSTAT_FCOE(compRequestCqeRx,    KSTAT_DATA_UINT64);
        BNXE_KSTAT_FCOE(compRequestCqeRxErr, KSTAT_DATA_UINT64);
        BNXE_KSTAT_FCOE(statWqeTx,           KSTAT_DATA_UINT64);
        BNXE_KSTAT_FCOE(statWqeTxErr,        KSTAT_DATA_UINT64);
        BNXE_KSTAT_FCOE(statCqeRx,           KSTAT_DATA_UINT64);
        BNXE_KSTAT_FCOE(statCqeRxErr,        KSTAT_DATA_UINT64);

        pUM->kstatsFcoe->ks_update  = BnxeKstatFcoeUpdate;
        pUM->kstatsFcoe->ks_private = (void *)pUM;

        kstat_install(pUM->kstatsFcoe);

        if (!BnxeKstatInitRxQ(pUM, FCOE_CID(pLM)))
        {
            BnxeKstatFini(pUM);
            return B_FALSE;
        }

        if (!BnxeKstatInitTxQ(pUM, FCOE_CID(pLM)))
        {
            BnxeKstatFini(pUM);
            return B_FALSE;
        }
    }

    /****************************************************************/

    LM_FOREACH_RSS_IDX(pLM, idx)
    {
        if (!BnxeKstatInitRxQ(pUM, idx))
        {
            BnxeKstatFini(pUM);
            return B_FALSE;
        }
    }

    /****************************************************************/

    LM_FOREACH_TSS_IDX(pLM, idx)
    {
        if (!BnxeKstatInitTxQ(pUM, idx))
        {
            BnxeKstatFini(pUM);
            return B_FALSE;
        }
    }

    /****************************************************************/

    return B_TRUE;
}


void BnxeKstatFini(um_device_t * pUM)
{
    lm_device_t * pLM = (lm_device_t *)pUM;
    int idx;

    if (pUM->kstats)
    {
        kstat_delete(pUM->kstats);
        pUM->kstats = NULL;
    }

    if (pUM->kstatsLink)
    {
        kstat_delete(pUM->kstatsLink);
        pUM->kstatsLink = NULL;
    }

    if (pUM->kstatsIntr)
    {
        kstat_delete(pUM->kstatsIntr);
        pUM->kstatsIntr = NULL;
    }

    if (pUM->kstatsL2Chip)
    {
        kstat_delete(pUM->kstatsL2Chip);
        pUM->kstatsL2Chip = NULL;
    }

    if (pUM->kstatsL2Driver)
    {
        kstat_delete(pUM->kstatsL2Driver);
        pUM->kstatsL2Driver = NULL;
    }

    if (pUM->kstatsL2Stats)
    {
        kstat_delete(pUM->kstatsL2Stats);
        pUM->kstatsL2Stats = NULL;
    }

    if (BNXE_FCOE(pUM))
    {
        if (pUM->kstatsFcoe)
        {
            kstat_delete(pUM->kstatsFcoe);
            pUM->kstatsFcoe = NULL;
        }

        idx = FCOE_CID(pLM);

        if (pUM->kstats_rxq[idx])
        {
            kstat_delete(pUM->kstats_rxq[idx]);
            pUM->kstats_rxq[idx] = NULL;
        }

        pUM->kstats_rxq_map[idx].idx = 0;
        pUM->kstats_rxq_map[idx].pUM = NULL;

        if (pUM->kstats_txq[idx])
        {
            kstat_delete(pUM->kstats_txq[idx]);
            pUM->kstats_txq[idx] = NULL;
        }

        pUM->kstats_txq_map[idx].idx = 0;
        pUM->kstats_txq_map[idx].pUM = NULL;
    }

    LM_FOREACH_RSS_IDX(pLM, idx)
    {
        if (pUM->kstats_rxq[idx])
        {
            kstat_delete(pUM->kstats_rxq[idx]);
            pUM->kstats_rxq[idx] = NULL;
        }

        pUM->kstats_rxq_map[idx].idx = 0;
        pUM->kstats_rxq_map[idx].pUM = NULL;
    }

    LM_FOREACH_TSS_IDX(pLM, idx)
    {
        if (pUM->kstats_txq[idx])
        {
            kstat_delete(pUM->kstats_txq[idx]);
            pUM->kstats_txq[idx] = NULL;
        }

        pUM->kstats_txq_map[idx].idx = 0;
        pUM->kstats_txq_map[idx].pUM = NULL;
    }
}

