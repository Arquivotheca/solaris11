/*******************************************************************************
 * The information contained in this file is confidential and proprietary to
 * Broadcom Corporation.  No part of this file may be reproduced or
 * distributed, in any form or by any means for any purpose, without the
 * express written permission of Broadcom Corporation.
 *
 * (c) COPYRIGHT 2001-2010 Broadcom Corporation, ALL RIGHTS RESERVED.
 *
 *
 * Module Description:
 *
 *
 * History:
 *    11/29/10 Alon Elhanani    Inception.
 ******************************************************************************/

#include "lm5710.h"
#include "mcp_shmem.h"
#include "mac_stats.h"

/**lm_niv_vif_enable
 * enable current function or change its parameters. This
 * function must be run in PASSIVE IRQL.
 *
 * @param pdev the device to use
 *
 * @return lm_status_t LM_STATUS_SUCCESS on success, some other
 *         failure code on failure.
 */
static lm_status_t lm_niv_vif_enable(lm_device_t *pdev)
{
    lm_status_t lm_status = LM_STATUS_FAILURE;
    u16_t       vif_id             = 0;
    u16_t       default_vlan       = 0;
    u8_t        allowed_priorities = 0;
    u8_t        prev_max_bw        = 0;
    u8_t        cur_max_bw         = 0;

    ///Refresh MF CFG values
    lm_status = lm_get_shmem_mf_cfg_info_niv(pdev);

    if (LM_STATUS_SUCCESS != lm_status)
    {
        return lm_status;
    }

    ///Reconfigure rate-limit and notify MCP if it changed
    prev_max_bw = pdev->hw_info.mf_info.max_bw[VNIC_ID(pdev)];
    lm_cmng_get_shmem_info(pdev);
    cur_max_bw = pdev->hw_info.mf_info.max_bw[VNIC_ID(pdev)];

    DbgBreakIf(0 != pdev->hw_info.mf_info.min_bw[VNIC_ID(pdev)]); //NIV does not support min-BW, so we expect it to be 0.

    if (prev_max_bw != cur_max_bw)
    {
        lm_mcp_set_mf_bw(pdev, 0/*min*/, cur_max_bw);
    }

    ///Send function-update ramrod and wait for completion
    vif_id             = VIF_ID(pdev);
    default_vlan       = NIV_DEFAULT_VLAN(pdev);
    allowed_priorities = NIV_PRIORITY(pdev);

    lm_status = lm_niv_vif_update(pdev,vif_id, default_vlan, allowed_priorities);
    if (LM_STATUS_SUCCESS != lm_status)
    {
        return lm_status;
    }

    ///notify "link-up" to miniport
    MM_ACQUIRE_PHY_LOCK(pdev);
    pdev->vars.link_status = LM_STATUS_LINK_ACTIVE;
    mm_indicate_link(pdev, pdev->vars.link_status, pdev->vars.medium);
    MM_RELEASE_PHY_LOCK(pdev);

    return lm_status;
}

/** lm_niv_vif_disable
 * disable current function. This function must be run in
 * PASSIVE IRQL.
 *
 * @param pdev the device to use
 *
 * @return lm_status_t LM_STATUS_SUCCESS on success, some other
 *         failure code on failure.
 */
static lm_status_t lm_niv_vif_disable(lm_device_t *pdev)
{
    lm_status_t lm_status = LM_STATUS_FAILURE;

    ///indicate "link-down"
    MM_ACQUIRE_PHY_LOCK(pdev);

    pdev->vars.link_status = LM_STATUS_LINK_DOWN;
    mm_indicate_link(pdev, pdev->vars.link_status, pdev->vars.medium);

    MM_RELEASE_PHY_LOCK(pdev);

    ///Send function-update ramrod with vif_id=0xFFFF and wait for completion
    lm_status = lm_niv_vif_update(pdev,INVALID_VIF_ID, 0, 0);
    if (LM_STATUS_SUCCESS != lm_status)
    {
        return lm_status;
    }

    return lm_status;
}

/**lm_niv_vif_delete
 * Delete current function. . This function must be run in
 * PASSIVE IRQL.
 *
 * @param pdev the device to use
 *
 * @return lm_status_t LM_STATUS_SUCCESS on success, some other
 *         failure code on failure.
 */
static lm_status_t lm_niv_vif_delete(lm_device_t *pdev)
{
    lm_status_t lm_status = LM_STATUS_FAILURE;

#if 0 ///disabled per latest spec.
    ///Send a vif-list ramrod with VIF_LIST_RULE_CLEAR_FUNC opcode and wait for completion
    lm_status = lm_niv_vif_list_update(pdev, VIF_LIST_RULE_CLEAR_FUNC, 0/*list_index*/, 0/*func_bit_map*/ ,ABS_FUNC_ID(pdev)/*func_to_clear*/);
    if (LM_STATUS_SUCCESS != lm_status)
    {
        return lm_status;
    }
#endif
    lm_status = lm_niv_vif_disable(pdev);
    if (LM_STATUS_SUCCESS != lm_status)
    {
        return lm_status;
    }

    return lm_status;
}

#define NIV_STATS_ASSIGN_HI_LO(_field, _val) _field##_hi = U64_HI((_val));\
                                             _field##_lo = U64_LO((_val));
/**lm_chip_stats_to_niv_stats
 * Copy relevant fields from driver statistics to the format
 * written to the SHMEM for NIV stats.
 *
 * @param pdev the device to take the stats from
 * @param niv_stats the SHMEM structure
 */
static void lm_niv_chip_stats_to_niv_stats(lm_device_t* pdev, OUT struct vntag_stats* niv_stats)
{
    b10_l2_chip_statistics_t stats;
    lm_stats_fw_t *fw_stats = &pdev->vars.stats.stats_mirror.stats_fw;
    lm_stats_get_l2_chip_stats(pdev, &stats);

    NIV_STATS_ASSIGN_HI_LO(niv_stats->tx_unicast_frames, stats.IfHCOutUcastPkts );
    NIV_STATS_ASSIGN_HI_LO(niv_stats->tx_unicast_bytes, stats.IfHCOutUcastOctets );
    NIV_STATS_ASSIGN_HI_LO(niv_stats->tx_multicast_frames, stats.IfHCOutMulticastPkts );
    NIV_STATS_ASSIGN_HI_LO(niv_stats->tx_multicast_bytes, stats.IfHCOutMulticastOctets );
    NIV_STATS_ASSIGN_HI_LO(niv_stats->tx_broadcast_frames, stats.IfHCOutBroadcastPkts );
    NIV_STATS_ASSIGN_HI_LO(niv_stats->tx_broadcast_bytes, stats.IfHCOutBroadcastOctets );
    NIV_STATS_ASSIGN_HI_LO(niv_stats->tx_frames_discarded, 0 );
    NIV_STATS_ASSIGN_HI_LO(niv_stats->tx_frames_dropped, fw_stats->eth_xstorm_common.client_statistics[LM_CLI_IDX_NDIS].error_drop_pkts);
    
    NIV_STATS_ASSIGN_HI_LO(niv_stats->rx_unicast_frames, stats.IfHCInUcastPkts );
    NIV_STATS_ASSIGN_HI_LO(niv_stats->rx_unicast_bytes, stats.IfHCInUcastOctets );
    NIV_STATS_ASSIGN_HI_LO(niv_stats->rx_multicast_frames, stats.IfHCInMulticastPkts );
    NIV_STATS_ASSIGN_HI_LO(niv_stats->rx_multicast_bytes, stats.IfHCInMulticastOctets );
    NIV_STATS_ASSIGN_HI_LO(niv_stats->rx_broadcast_frames, stats.IfHCInBroadcastPkts );
    NIV_STATS_ASSIGN_HI_LO(niv_stats->rx_broadcast_bytes, stats.IfHCInBroadcastOctets );
    NIV_STATS_ASSIGN_HI_LO(niv_stats->rx_frames_discarded, stats.IfInTTL0Discards +
                                                        stats.EtherStatsOverrsizePkts +
                                                        fw_stats->eth_tstorm_common.client_statistics[LM_CLI_IDX_NDIS].checksum_discard);
    NIV_STATS_ASSIGN_HI_LO(niv_stats->rx_frames_dropped, stats.IfInMBUFDiscards );
}

/**lm_niv_stats_get
 * Update NIV statistics in SHMEM. This function runs in PASSIVE
 * IRQL as an LPME.
 *
 * @param pdev the device to use
 */
static void lm_niv_stats_get(lm_device_t *pdev)
{
    u32_t       mcp_resp        = 0;
    u32_t       output_offset   = 0;
    u32_t       *field_ptr      = NULL;
    int         bytes_written   = 0;
    const u32_t func_mailbox_id = FUNC_MAILBOX_ID(pdev);
    const u32_t offset          = OFFSETOF(shmem2_region_t, vntag_scratchpad_addr_to_write[func_mailbox_id]);

    struct vntag_stats  niv_stats   = {0};
    lm_niv_chip_stats_to_niv_stats(pdev, &niv_stats);

    ///Read from SHMEM2 the address where the response should be placed
    LM_SHMEM2_READ(pdev, offset, &output_offset);

    ///Write the response to the scratchpad field by field.
    field_ptr = (u32_t*)&niv_stats;
    for (bytes_written = 0; bytes_written  < sizeof(niv_stats); bytes_written += sizeof(u32_t))
    {
        REG_WR(pdev, output_offset + bytes_written, *field_ptr);
        ++field_ptr;
    }
    ///ACK the MCP message
    lm_mcp_cmd_send_recieve(pdev, lm_mcp_mb_header, DRV_MSG_CODE_VNTAG_STATSGET_ACK, 0, MCP_CMD_DEFAULT_TIMEOUT, &mcp_resp);
    DbgBreakIf(mcp_resp != FW_MSG_CODE_VNTAG_STATSGET_ACK);
}

/**lm_niv_vif_list_set
 * Modify local information about VIF lists. This function runs
 * in PASSIVE IRQL as an LPME. (PMF only)
 *
 * @param pdev the device to use
 */
static void lm_niv_vif_list_set(lm_device_t *pdev)
{
    lm_status_t lm_status       = LM_STATUS_FAILURE;
    u32_t       list_idx        = 0;
    u32_t       list_bitmap     = 0;
    u32_t       mcp_resp        = 0;
    const u32_t func_mailbox_id = FUNC_MAILBOX_ID(pdev);
    u32_t       offset          = 0;

    ///Read VIF list id+bitfield from SHMEM2
    offset          = OFFSETOF(struct shmem2_region, vntag_param1_to_driver[func_mailbox_id]);
    LM_SHMEM2_READ(pdev, offset, &list_idx);
    DbgBreakIf(list_idx > 0xFFFF);

    offset          = OFFSETOF(struct shmem2_region, vntag_param2_to_driver[func_mailbox_id]);
    LM_SHMEM2_READ(pdev, offset, &list_bitmap);
    DbgBreakIf(list_bitmap > 0xFF);

    ///Send a vif-list ramrod with VIF_LIST_RULE_SET opcode and wait for completion
    lm_status = lm_niv_vif_list_update(pdev, VIF_LIST_RULE_SET,(u16_t)list_idx, (u8_t)list_bitmap,0);
    DbgBreakIf(lm_status != LM_STATUS_SUCCESS);

    ///ACK the MCP message
    lm_mcp_cmd_send_recieve(pdev, lm_mcp_mb_header, DRV_MSG_CODE_VNTAG_LISTSET_ACK, 0, MCP_CMD_DEFAULT_TIMEOUT, &mcp_resp);
    DbgBreakIf(mcp_resp != FW_MSG_CODE_VNTAG_LISTSET_ACK);
}

/**lm_niv_vif_list_get
 * Update NIV statistics in SHMEM. This function runs in PASSIVE
 * IRQL as an LPME.
 *
 * @param pdev the device to use
 *
 */
static void lm_niv_vif_list_get(lm_device_t *pdev)
{
    lm_status_t lm_status       = LM_STATUS_FAILURE;
    u32_t       list_idx        = 0;
    u32_t       mcp_resp        = 0;
    const u32_t func_mailbox_id = FUNC_MAILBOX_ID(pdev);
    const u32_t offset          = OFFSETOF(struct shmem2_region, vntag_param1_to_driver[func_mailbox_id]);

    ///Read list ID from SHMEM2
    LM_SHMEM2_READ(pdev, offset, &list_idx);
    DbgBreakIf(list_idx > 0xFFFF);

    ///Send a vif-list ramrod with VIF_LIST_RULE_GET opcode and wait for completion
    lm_status = lm_niv_vif_list_update(pdev, VIF_LIST_RULE_GET, (u16_t)list_idx, 0, 0);
    DbgBreakIf (LM_STATUS_SUCCESS != lm_status);

    ///Write response to SHMEM and ACK the MCP message
    lm_mcp_cmd_send_recieve(pdev, lm_mcp_mb_header, DRV_MSG_CODE_VNTAG_LISTGET_ACK, pdev->slowpath_info.last_vif_list_bitmap, MCP_CMD_DEFAULT_TIMEOUT, &mcp_resp);
    DbgBreakIf(mcp_resp != FW_MSG_CODE_VNTAG_LISTGET_ACK);
}

/**lm_niv_vif_set
 * Handle a VIF-SET command. This function runs in PASSIVE IRQL
 * as an LPME.
 *
 * @param pdev the device to use
 */
static void lm_niv_vif_set(lm_device_t *pdev)
{

    u32_t       func_mf_config  = 0;
    u32_t       mcp_resp        = 0;
    const u32_t abs_func_id     = ABS_FUNC_ID(pdev);
    const u32_t offset          = OFFSETOF(mf_cfg_t, func_mf_config[abs_func_id].config);

    ///read FUNC-DISABLED and FUNC-DELETED from func_mf_cfg
    LM_MFCFG_READ(pdev, offset, &func_mf_config);

    pdev->hw_info.mf_info.func_mf_cfg = func_mf_config ;

    ///if it's enable, call lm_niv_vif_enable
    ///if it's disable, call lm_niv_vif_disable
    ///if it's delete, call lm_niv_vif_delete
    switch(GET_FLAGS(func_mf_config, FUNC_MF_CFG_FUNC_DISABLED|FUNC_MF_CFG_FUNC_DELETED))
    {
    case FUNC_MF_CFG_FUNC_DISABLED:
        {
            lm_niv_vif_disable(pdev);        
        }
        break;

    case FUNC_MF_CFG_FUNC_DELETED:
        {
            lm_niv_vif_delete(pdev);
        }
        break;

    case 0: //neither=enabled
        {
            lm_niv_vif_enable(pdev);
        }
        break;

    default:
        {
            DbgBreakIf(1);//invalid value
        }
        break;
    }

    ///ACK the MCP message
    lm_mcp_cmd_send_recieve(pdev, lm_mcp_mb_header, DRV_MSG_CODE_VNTAG_VIFSET_ACK, 0, MCP_CMD_DEFAULT_TIMEOUT, &mcp_resp);
    DbgBreakIf(mcp_resp != FW_MSG_CODE_VNTAG_VIFSET_ACK);
}

typedef struct _lm_niv_event_function_t
{
    u32_t niv_event_flag;
    void (*function)(lm_device_t*);
} lm_niv_event_function_t;

/**lm_niv_event
 * handle a NIV-related MCP general attention by scheduling the
 * appropriate work item.
 *
 * @param pdev the device to use
 * @param niv_event the DRIVER_STATUS flags that the MCP sent.
 *                  It's assumed that only NIV-related flags are
 *                  set.
 *
 * @return lm_status_t LM_STATUS_SUCCESS on success, some other
 *         failure code on failure.
 */
lm_status_t lm_niv_event(lm_device_t *pdev, const u32_t niv_event)
{
    lm_status_t                          lm_status              = LM_STATUS_FAILURE;
    u32_t                                event_idx              = 0;
    u32_t                                handled_events         = 0;
    u32_t                                cur_event              = 0;
    static const lm_niv_event_function_t event_functions_arr[]  = { {DRV_STATUS_VNTAG_VIFSET_REQ,   lm_niv_vif_set},
                                                                    {DRV_STATUS_VNTAG_LISTGET_REQ,  lm_niv_vif_list_get},
                                                                    {DRV_STATUS_VNTAG_LISTSET_REQ,  lm_niv_vif_list_set},
                                                                    {DRV_STATUS_VNTAG_STATSGET_REQ, lm_niv_stats_get},
                                                                  };

    //for every possible flag: if it's set, schedule a WI with the associated function and set the same flag in handled_events
    for (event_idx = 0; event_idx < ARRSIZE(event_functions_arr); ++event_idx)
    {
        cur_event = event_functions_arr[event_idx].niv_event_flag;

        if (GET_FLAGS(niv_event, cur_event))
        {
            lm_status = MM_REGISTER_LPME(pdev, event_functions_arr[event_idx].function, TRUE, TRUE);
            if (lm_status != LM_STATUS_SUCCESS)
            {
                DbgBreakIf(lm_status != LM_STATUS_SUCCESS);
                return lm_status;
            }
            SET_FLAGS(handled_events, cur_event);
        }
    }

    //make sure there we no unknown events set.
    if (handled_events != niv_event)
    {
        DbgBreakIf(handled_events != niv_event);
        return LM_STATUS_INVALID_PARAMETER;
    }

    return lm_status;
}
