
/*
 * The information contained in this file is confidential and proprietary to
 * Broadcom Corporation.  No part of this file may be reproduced or
 * distributed, in any form or by any means for any purpose, without the
 * express written permission of Broadcom Corporation.
 *
 * (c) COPYRIGHT 2008-2011 Broadcom Corporation, ALL RIGHTS RESERVED.
 */

#include "bnxe.h"


lm_status_t
mm_sc_comp_l5_request(
        IN struct _lm_device_t *pdev,
        IN struct iscsi_kcqe *kcqes,
        IN u32_t num_kcqes
        )
{
    BnxeDbgBreak((um_device_t *)pdev);
    return 0;
}


lm_status_t
mm_fc_comp_request(
    IN struct _lm_device_t          *pdev,
    IN struct fcoe_kcqe             *kcqes,
    IN u32_t                        num_kcqes)
{
    return (!BnxeFcoeCompRequestCqe((um_device_t *)pdev, kcqes, num_kcqes)) ?
               LM_STATUS_FAILURE : LM_STATUS_SUCCESS;
}


lm_status_t mm_sc_complete_init_request(lm_device_t *pdev, struct iscsi_kcqe *kcqe)
{
    BnxeDbgBreak((um_device_t *)pdev);
    return 0;
}


int
mm_sc_is_omgr_enabled(struct _lm_device_t *_pdev)
{
    BnxeDbgBreak((um_device_t *)_pdev);
    return 0;
}


lm_status_t
mm_sc_omgr_flush_rx(
    IN struct _lm_device_t      *_pdev,
    IN struct iscsi_kcqe        *kcqe_recv,
    IN u32_t                     cid)
{
    BnxeDbgBreak((um_device_t *)_pdev);
    return 0;
}


lm_status_t mm_sc_complete_update_request(lm_device_t *pdev, struct iscsi_kcqe *kcqe)
{
    BnxeDbgBreak((um_device_t *)pdev);
    return 0;
}


lm_status_t
mm_fc_complete_init_request(
    IN    lm_device_t               *pdev,
    IN    struct fcoe_kcqe          *kcqe)
{
    return (!BnxeFcoeInitCqe((um_device_t *)pdev, kcqe)) ?
               LM_STATUS_FAILURE : LM_STATUS_SUCCESS;
}


lm_status_t
mm_fc_complete_destroy_request(
    IN    lm_device_t               *pdev,
    IN    struct fcoe_kcqe          *kcqe)
{
    return (!BnxeFcoeDestroyCqe((um_device_t *)pdev, kcqe)) ?
               LM_STATUS_FAILURE : LM_STATUS_SUCCESS;
}


lm_status_t
mm_fc_complete_ofld_request(
    IN    lm_device_t               *pdev,
    IN    lm_fcoe_state_t           *fcoe,
    IN    struct fcoe_kcqe          *kcqe)
{
    return (!BnxeFcoeOffloadConnCqe((um_device_t *)pdev,
                                    (BnxeFcoeState *)fcoe,
                                    kcqe)) ?
               LM_STATUS_FAILURE : LM_STATUS_SUCCESS;
}


lm_status_t
mm_fc_complete_enable_request(
    IN    lm_device_t               *pdev,
    IN    lm_fcoe_state_t           *fcoe,
    IN    struct fcoe_kcqe          *kcqe)
{
    return (!BnxeFcoeEnableConnCqe((um_device_t *)pdev,
                                   (BnxeFcoeState *)fcoe,
                                   kcqe)) ?
               LM_STATUS_FAILURE : LM_STATUS_SUCCESS;
}


lm_status_t
mm_fc_complete_stat_request(
    IN    lm_device_t               *pdev,
    IN    struct fcoe_kcqe          *kcqe)
{
    return (!BnxeFcoeStatCqe((um_device_t *)pdev, kcqe)) ?
               LM_STATUS_FAILURE : LM_STATUS_SUCCESS;
}


lm_status_t
mm_fc_complete_disable_request(
    IN    lm_device_t               *pdev,
    IN    lm_fcoe_state_t           *fcoe,
    IN    struct fcoe_kcqe          *kcqe)
{
    return (!BnxeFcoeDisableConnCqe((um_device_t *)pdev,
                                    (BnxeFcoeState *)fcoe,
                                    kcqe)) ?
               LM_STATUS_FAILURE : LM_STATUS_SUCCESS;
}


lm_status_t
mm_fc_complete_terminate_request(
    IN    lm_device_t               *pdev,
    IN    lm_fcoe_state_t           *fcoe,
    IN    struct fcoe_kcqe          *kcqe)
{
    return (!BnxeFcoeDestroyConnCqe((um_device_t *)pdev,
                                    (BnxeFcoeState *)fcoe,
                                    kcqe)) ?
               LM_STATUS_FAILURE : LM_STATUS_SUCCESS;
}


lm_status_t mm_sc_complete_offload_request(
    IN    lm_device_t                *pdev,
    IN    lm_iscsi_state_t           *iscsi,
    IN    lm_status_t                 comp_status
    )
{
    BnxeDbgBreak((um_device_t *)pdev);
    return 0;
}

