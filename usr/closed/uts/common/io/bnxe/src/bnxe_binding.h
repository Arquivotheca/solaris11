
/*
 * The information contained in this file is confidential and proprietary to
 * Broadcom Corporation.  No part of this file may be reproduced or
 * distributed, in any form or by any means for any purpose, without the
 * express written permission of Broadcom Corporation.
 *
 * (c) COPYRIGHT 2008-2011 Broadcom Corporation, ALL RIGHTS RESERVED.
 */

#ifndef __BNXE_BINDING_H
#define __BNXE_BINDING_H

#include "bcmtype.h"
#include "bnxe_fcoeio_ioctl.h"

/* cb_ioctl commands sent to bnxe */
#define BNXE_BIND_FCOE    0x0ead0001
#define BNXE_UNBIND_FCOE  0x0ead0002


/* default FCoE max exchanges is 4096 for SF and 2048 for MF */
#define FCOE_MAX_EXCHANGES_SF 4096
#define FCOE_MAX_EXCHANGES_MF 2048

typedef struct bnxe_fcoe_info
{
    u32_t                           flags;              /* zero for now */
    u32_t                           max_fcoe_conn;
    u32_t                           max_fcoe_exchanges;
    bnxe_fcoeio_create_port_param_t cp_params; 
} BnxeFcoeInfo;


/*
 * cli_ctl - misc control interface up to the client
 *
 *  cmd: CLI_CTL_LINK_UP   - link up event, no data passed
 *       CLI_CTL_LINK_DOWN - link down event, no data passed
 *       CLI_CTL_UNLOAD    - graceful unload event, no data passed
 *
 *  pData:    pointer to command data or NULL
 *
 *  dataLen:  length of command data or 0
 */
#define CLI_CTL_LINK_UP    1
#define CLI_CTL_LINK_DOWN  2
#define CLI_CTL_UNLOAD     3
typedef boolean_t (*cli_ctl)(dev_info_t * pDev,
                             int          cmd,
                             void *       pData,
                             int          dataLen);

typedef boolean_t (*cli_indicate_tx)(dev_info_t * pDev,
                                     mblk_t *     pMblk);

typedef boolean_t (*cli_indicate_rx)(dev_info_t * pDev,
                                     mblk_t *     pMblk);

typedef boolean_t (*cli_indicate_cqes)(dev_info_t * pDev,
                                       void *       cqes[],
                                       int          cqeCnt);


/*
 * prv_ctl - misc control interface down to the provider
 *
 *  cmd: PRV_CTL_GET_MAC_ADDR - get MAC Address, data buffer to hold addr
 *       PRV_CTL_SET_MAC_ADDR - set MAC Address, data buffer contains addr
 *       PRV_CTL_QUERY_PARAMS - query related params, pass BnxeXXXInfo struct
 *       PRV_CTL_DISABLE_INTR - disable interrupts, no data passed
 *       PRV_CTL_ENABLE_INTR  - enable interrupts, no data passed
 *       PRV_CTL_CRASH_DUMP   - XXX not implemented yet
 *       PRV_CTL_LINK_STATE   - query the link state, data buffer to hold boolean
 *
 *  pData:    pointer to command data or NULL
 *
 *  dataLen:  length of command data or 0
 *
 *  returns:  TRUE upon success, FALSE otherwise
 */
#define PRV_CTL_GET_MAC_ADDR  1
#define PRV_CTL_SET_MAC_ADDR  2
#define PRV_CTL_QUERY_PARAMS  3
#define PRV_CTL_DISABLE_INTR  4
#define PRV_CTL_ENABLE_INTR   5
#define PRV_CTL_CRASH_DUMP    6
#define PRV_CTL_LINK_STATE    7
typedef boolean_t (*prv_ctl)(dev_info_t * pDev,
                             int          cmd,
                             void *       pData,
                             int          dataLen);

#define PRV_TX_VLAN_TAG  1
typedef mblk_t * (*prv_tx)(dev_info_t * pDev,
                           mblk_t *     pMblk,
                           u32_t        flags,
                           u16_t        vlan_tag);

typedef boolean_t (*prv_poll)(dev_info_t * pDev);

typedef boolean_t (*prv_send_wqes)(dev_info_t * pDev,
                                   void *       wqes[],
                                   int          wqeCnt);

typedef boolean_t (*prv_map_mailboxq)(dev_info_t *       pDev,
                                      u32_t              cid,
                                      void **            ppMap,
                                      ddi_acc_handle_t * pAccHandle);

typedef boolean_t (*prv_unmap_mailboxq)(dev_info_t *     pDev,
                                        u32_t            cid,
                                        void *           pMap,
                                        ddi_acc_handle_t accHandle);


typedef struct bnxe_binding
{
    dev_info_t *       pCliDev; /* bnxe client */

    cli_ctl            cliCtl;
    cli_indicate_tx    cliIndicateTx;
    cli_indicate_rx    cliIndicateRx;
    cli_indicate_cqes  cliIndicateCqes;

    u32_t              numRxDescs;
    u32_t              numTxDescs;

    dev_info_t *       pPrvDev; /* bnxe */

    prv_ctl            prvCtl;
    prv_tx             prvTx;
    prv_poll           prvPoll;
    prv_send_wqes      prvSendWqes;
    prv_map_mailboxq   prvMapMailboxq;
    prv_unmap_mailboxq prvUnmapMailboxq;
} BnxeBinding;

#endif /* __BNXE_BINDING_H */

