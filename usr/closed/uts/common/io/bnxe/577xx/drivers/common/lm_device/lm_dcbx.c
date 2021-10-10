/*******************************************************************************
 * The information contained in this file is confidential and proprietary to
 * Broadcom Corporation.  No part of this file may be reproduced or
 * distributed, in any form or by any means for any purpose, without the
 * express written permission of Broadcom Corporation.
 *
 * (c) COPYRIGHT 2001-2007 Broadcom Corporation, ALL RIGHTS RESERVED.
 *
 *
 * Module Description:
 *
 *
 * History:
 *    01/10/10 Shay Haroush    Inception.
 ******************************************************************************/

#include "lm5710.h"
#include "license.h"
#include "mcp_shmem.h"
#include "577xx_int_offsets.h"
#include "command.h"


#define DCBX_ILLEGAL_PG     (0xFF)
typedef struct _pg_entry_help_data_t
{
    u8_t    num_of_dif_pri;
    u8_t    pg;
    u32_t   pg_priority;
}pg_entry_help_data_t;

typedef struct _pg_help_data_t
{
    pg_entry_help_data_t    pg_entry_data[LLFC_DRIVER_TRAFFIC_TYPE_MAX];
    u8_t                    num_of_pg;
}pg_help_data_t;

#define DCBX_STRICT_PRIORITY        (15)
#define DCBX_INVALID_COS_BW         (0xFFFFFFFF)
#define DCBX_MAX_COS_BW             (0xFF)

typedef struct _cos_entry_help_data_t
{
    u32_t                   pri_join_mask;
    u32_t                   cos_bw;
    dcbx_cos_params_strict  strict;
    u8_t                    b_pausable;
}cos_entry_help_data_t;

typedef struct _cos_help_data_t
{
    cos_entry_help_data_t   entry_data[E2_NUM_OF_COS];
    u8_t                    num_of_cos;
}cos_help_data_t;

/*******************************************************************************
 **********************foreword declaration*************************************
 *******************************************************************************
 * Description: Parse ets_pri_pg data and spread it from nibble to 32 bits.
 *
 * Return:
 ******************************************************************************/
STATIC void 
lm_dcbx_get_ets_pri_pg_tbl(struct _lm_device_t      * pdev,
                           OUT u32_t                * set_configuration_ets_pg,
                           IN const u32_t           * mcp_pri_pg_tbl,
                           IN const u8_t            set_priority_app_size,
                           IN const u8_t            mcp_pri_pg_tbl_size);


/**
 * @description
 * Function is needed for PMF migration in order to synchronize 
 * the new PMF that DCBX results has ended. 
 * @param pdev 
 * 
 * @return u8_t 
 * This function returns TRUE if DCBX completion received on 
 * this port 
 */
STATIC u8_t
lm_dcbx_is_comp_recv_on_port(
    IN lm_device_t *pdev);

void lm_dcbx_update_lpme_set_params(struct _lm_device_t *pdev);
/**********************Start of PFC code**************************************/


/*******************************************************************************
 * Description: Fill Fw struct that will be sent in DCBX start ramrod
 *
 * Return:
 ******************************************************************************/
void
lm_dcbx_print_cos_params(
    IN OUT   lm_device_t                    *pdev,
    IN struct flow_control_configuration    *pfc_fw_cfg)
{
#if DBG
    u8_t   pri                                      = 0;
    u8_t   cos                                      = 0;

    DbgMessage(pdev,INFORM,"******************DCBX configuration******************************\n");
    DbgMessage1(pdev,INFORM,"pfc_fw_cfg->dcb_version %x\n",pfc_fw_cfg->dcb_version);
    DbgMessage1(pdev,INFORM,"pdev->params.dcbx_port_params.pfc.priority_non_pauseable_mask %x\n",
                pdev->params.dcbx_port_params.pfc.priority_non_pauseable_mask);
    
    for( cos =0 ; cos < pdev->params.dcbx_port_params.ets.num_of_cos ; cos++)
    {   
        DbgMessage2(pdev,INFORM,"pdev->params.dcbx_port_params.ets.cos_params[%d].pri_bitmask %x\n",cos,
                pdev->params.dcbx_port_params.ets.cos_params[cos].pri_bitmask);

        DbgMessage2(pdev,INFORM,"pdev->params.dcbx_port_params.ets.cos_params[%d].bw_tbl %x\n",cos,
                pdev->params.dcbx_port_params.ets.cos_params[cos].bw_tbl);
        
        DbgMessage2(pdev,INFORM,"pdev->params.dcbx_port_params.ets.cos_params[%d].strict %x\n",cos,
                pdev->params.dcbx_port_params.ets.cos_params[cos].strict);

        DbgMessage2(pdev,INFORM,"pdev->params.dcbx_port_params.ets.cos_params[%d].pauseable %x\n",cos,
                pdev->params.dcbx_port_params.ets.cos_params[cos].pauseable);
    }

    for (pri = 0; pri < ARRSIZE(pdev->params.dcbx_port_params.app.traffic_type_priority); pri++)
    {
        DbgMessage2(pdev,INFORM,"pfc_fw_cfg->traffic_type_to_priority_cos[%d].priority %x\n",pri,
                    pfc_fw_cfg->traffic_type_to_priority_cos[pri].priority);

        DbgMessage2(pdev,INFORM,"pfc_fw_cfg->traffic_type_to_priority_cos[%d].cos %x\n",pri,
                    pfc_fw_cfg->traffic_type_to_priority_cos[pri].cos);
    }

#endif //DBG
}
/*******************************************************************************
 * Description: Fill Fw struct that will be sent in DCBX start ramrod
 *
 * Return:
 ******************************************************************************/
void
lm_pfc_fw_struct_e2(
    IN OUT   lm_device_t     *pdev)
{
    struct flow_control_configuration   *pfc_fw_cfg = NULL;
    u16_t  pri_bit                                  = 0;
    u8_t   cos                                      = 0;
    u8_t   pri                                      = 0;

    if(CHK_NULL(pdev->dcbx_info.pfc_fw_cfg_virt))
    {
        DbgBreakMsg("lm_pfc_fw_struct_e2:pfc_fw_cfg_virt was not allocated DCBX should have been disabled ");
        return;
    }
    pfc_fw_cfg = (struct flow_control_configuration*)pdev->dcbx_info.pfc_fw_cfg_virt;
    mm_memset(pfc_fw_cfg, 0, sizeof(struct flow_control_configuration));

    // Fw version should be incemented each
    pfc_fw_cfg->dcb_version = 0; // Reserved field 
    pfc_fw_cfg->dcb_enabled = 1;
    pfc_fw_cfg->dont_add_pri_0 = 1;

    // patch until FW can be rooled back to not using priorities
    if(FALSE == pdev->params.dcbx_port_params.app.enabled)
    {
        return;
    }

    // Default initialization
    for (pri = 0; pri < ARRSIZE(pfc_fw_cfg->traffic_type_to_priority_cos) ; pri++)
    { 
        pfc_fw_cfg->traffic_type_to_priority_cos[pri].priority = LLFC_TRAFFIC_TYPE_TO_PRIORITY_UNMAPPED;
        pfc_fw_cfg->traffic_type_to_priority_cos[pri].cos      = 0;
    }

    // Fill priority parameters
    for (pri = 0; pri < ARRSIZE(pdev->params.dcbx_port_params.app.traffic_type_priority); pri++)
    {
        DbgBreakIf(pdev->params.dcbx_port_params.app.traffic_type_priority[pri] >= MAX_PFC_PRIORITIES);
        pfc_fw_cfg->traffic_type_to_priority_cos[pri].priority = 
            (u8_t)pdev->params.dcbx_port_params.app.traffic_type_priority[pri];

        pri_bit = 1 << pfc_fw_cfg->traffic_type_to_priority_cos[pri].priority;
        // Fill COS parameters based on COS calculated to make it more generally for future use
        for( cos =0 ; cos < pdev->params.dcbx_port_params.ets.num_of_cos ; cos++)
        {   
            if (pdev->params.dcbx_port_params.ets.cos_params[cos].pri_bitmask & pri_bit)
            {
                pfc_fw_cfg->traffic_type_to_priority_cos[pri].cos = cos;
            }   
        }
    }
    lm_dcbx_print_cos_params(pdev,
                             pfc_fw_cfg);

}
/*******************************************************************************
 * Description:
 *
 * Return:
 ******************************************************************************/
static void 
lm_pfc_clear(lm_device_t *pdev)
{
    MM_ACQUIRE_PHY_LOCK(pdev);
    RESET_FLAGS(pdev->params.link.feature_config_flags, ELINK_FEATURE_CONFIG_PFC_ENABLED);
    elink_update_pfc(&pdev->params.link, &pdev->vars.link, 0);
    MM_RELEASE_PHY_LOCK(pdev);
}
/*******************************************************************************
 * Description:
 *
 * Return:
 ******************************************************************************/
void lm_pfc_set_clc(lm_device_t *pdev)
{
    struct elink_nig_brb_pfc_port_params    pfc_params = {0};
    pg_params_t                             *ets        = &pdev->params.dcbx_port_params.ets;
    u32_t                                   val         = 0;
    u32_t                                   pri_bit     = 0;
    u8_t                                    pri         = 0;

    // Tx COS configuration
    if(TRUE == ets->cos_params[0].pauseable)
    {
        pfc_params.rx_cos0_priority_mask =
                        ets->cos_params[0].pri_bitmask;
    }

    if(TRUE == ets->cos_params[1].pauseable)
    {
        pfc_params.rx_cos1_priority_mask =
                        ets->cos_params[1].pri_bitmask;
    }

    // Rx COS configuration
    // Changing PFC RX configuration . In RX COS0 will always be configured to lossy 
    // and COS1 to lossless.
    for(pri = 0 ; pri < MAX_PFC_PRIORITIES ; pri++) 
    {
        pri_bit = 1 << pri;

        if(pri_bit & DCBX_PFC_PRI_PAUSE_MASK(pdev))
        {
            val |= 1 << (pri * 4);
        }
    }

    pfc_params.pkt_priority_to_cos = val;

    // RX COS0 
    pfc_params.llfc_low_priority_classes = 0;
    // RX COS1 
    pfc_params.llfc_high_priority_classes =
                    DCBX_PFC_PRI_PAUSE_MASK(pdev);

    pfc_params.cos0_pauseable = FALSE;
    pfc_params.cos1_pauseable = TRUE;

    MM_ACQUIRE_PHY_LOCK(pdev);
    SET_FLAGS(pdev->params.link.feature_config_flags, ELINK_FEATURE_CONFIG_PFC_ENABLED);
    elink_update_pfc(&pdev->params.link, &pdev->vars.link, &pfc_params);
    MM_RELEASE_PHY_LOCK(pdev);
}
/*******************************************************************************
 * Description:
 *
 * Return:
 ******************************************************************************/
void
lm_pfc_set_pfc(
    lm_device_t *pdev)
{
    if(!CHIP_IS_E1x(pdev))
    {
        //1.	Fills up common PFC structures if required.
        //2.	Configure BRB
        //3.	Configure NIG.
        //4.	Configure the MAC via the CLC:
        //"	CLC must first check if BMAC is not in reset and only then configures the BMAC
        //"	Or, configure EMAC.
        lm_pfc_set_clc(pdev);
    }
}

/*******************************************************************************
 * Description:
 *
 * Return:
 ******************************************************************************/
void
lm_pfc_handle_pfc(
    lm_device_t *pdev)
{
    if(!CHIP_IS_E1x(pdev))
    {
        lm_pfc_fw_struct_e2(pdev);
    }
    
    // Only for testing DCBX client the registry key won't be 0 or 1
    // Only a call from lm_chip_init can be not 0 or 1
    if(TRUE == pdev->params.dcbx_port_params.pfc.enabled)
    {// PFC enabled
        lm_pfc_set_pfc(pdev);
    }
    else
    {// PFC disabled go back to pause if needed
        if(!CHIP_IS_E1x(pdev))
        {
            lm_pfc_clear(pdev);
        }
    }
}
/*******************************************************************************
 * Description:
 *
 * Return:
 ******************************************************************************/
void
lm_dcbx_update_ets_params(
    lm_device_t *pdev)
{
    pg_params_t *ets = &(pdev->params.dcbx_port_params.ets);
    u8_t        elink_status = ELINK_STATUS_OK;
    u32_t       bw_tbl_0 = 0;
    u32_t       bw_tbl_1 = 0;

    elink_ets_disabled(&pdev->params.link);

    if(FALSE == ets->enabled)
    {
        return;
    }
    if ((0 == ets->num_of_cos ) ||
        (E2_NUM_OF_COS < ets->num_of_cos))
    {
        DbgMessage1(pdev,FATAL," illegal num of cos= %x",ets->num_of_cos);
        DbgBreakIf(1);
        return;
    }
    //valid COS entries
    if( 1 == ets->num_of_cos)
    {// No ETS
        return;
    }
    DbgBreakIf(2 != ets->num_of_cos);

    if(((DCBX_COS_NOT_STRICT == ets->cos_params[0].strict)&&
       (DCBX_INVALID_COS_BW == ets->cos_params[0].bw_tbl)) ||
       ((DCBX_COS_NOT_STRICT == ets->cos_params[1].strict)&&
       (DCBX_INVALID_COS_BW == ets->cos_params[1].bw_tbl)))
    {
        DbgMessage4(pdev,FATAL,"We expect all the COS to have at least bw_limit or strict"
                               "ets->cos_params[0].strict= %x"
                               "ets->cos_params[0].bw_tbl= %x"
                               "ets->cos_params[1].strict= %x"
                               "ets->cos_params[1].bw_tbl= %x"
                                ,ets->cos_params[0].strict, ets->cos_params[0].bw_tbl
                                ,ets->cos_params[1].strict, ets->cos_params[1].bw_tbl);

        // CQ47518,CQ47504 Assert in the eVBD because of illegal ETS parameters reception. When 
        //switch changes configuration in runtime it sends several packets that
        //contain illegal configuration until the actual configuration is merged.
        //DbgBreakIf(1);
        return;
    }
    // If we join a group and there is bw_tbl and strict then bw rules. 
    if ((DCBX_INVALID_COS_BW != ets->cos_params[0].bw_tbl) &&
        (DCBX_INVALID_COS_BW != ets->cos_params[1].bw_tbl))
    {
        DbgBreakIf(0 == (ets->cos_params[0].bw_tbl + ets->cos_params[1].bw_tbl));
        // ETS 0 100 PBF bug.
        bw_tbl_0 = ets->cos_params[0].bw_tbl;
        bw_tbl_1 = ets->cos_params[1].bw_tbl;

        if((0 == bw_tbl_0)||
           (0 == bw_tbl_1))
        {
            if(0 == bw_tbl_0)
            {
                bw_tbl_0 = 1;
                bw_tbl_1 = 99;
            }
            else
    {
                bw_tbl_0 = 99;
                bw_tbl_1 = 1;
            }
            
        }
        elink_ets_bw_limit(&pdev->params.link,
                           bw_tbl_0,
                           bw_tbl_1);   
    }
    else 
    {
        DbgBreakIf(ets->cos_params[0].strict == ets->cos_params[1].strict);
        if(DCBX_COS_HIGH_STRICT ==  ets->cos_params[0].strict)
        {
            elink_status = elink_ets_strict(&pdev->params.link,0);
        }
        else if(DCBX_COS_HIGH_STRICT ==  ets->cos_params[1].strict)
        {
            elink_status = elink_ets_strict(&pdev->params.link,1);
        }

        if(ELINK_STATUS_OK != elink_status)
        {
            DbgBreakMsg("lm_dcbx_update_ets_params: elinc_ets_strict failed ");
        }
    }
}
/**********************End of PFC code**************************************/
/**********************start DCBX Common FUNCTIONS**************************************/
#define ETH_TYPE_FCOE                   (0x8906)
#define TCP_PORT_ISCSI                  (0xCBC)


/*******************************************************************************
 * Description:
 *              Runtime changes can take more than 1 second and can't be handled
 *              from DPC.
 *              When the PMF detects a DCBX update it will schedule a WI that 
 *              will handle the job.
 *              Also the function lm_dcbx_stop_HW_TX/lm_dcbx_resume_HW_TX must be 
 *              called in mutual exclusion.
 *              lm_mcp_cmd_send_recieve must be called from default DPC, so when the 
 *              WI will finish the processing an interrupt that will be called from
 *              The WI will cause us to enter this function again and send the Ack.
 * 
 * Return:
******************************************************************************/
void 
lm_dcbx_event(lm_device_t *pdev,
              u32_t         drv_status)
{

    u32_t fw_resp = 0; 
    lm_status_t lm_status         = LM_STATUS_SUCCESS ;

    if(IS_PMF(pdev))
    {
        if( GET_FLAGS( drv_status, DRV_STATUS_DCBX_NEGOTIATION_RESULTS))
        {
            switch(pdev->dcbx_info.dcbx_update_lpme_task_state) 
            {
            case DCBX_UPDATE_TASK_STATE_FREE:
                // free: this is the first time we saw 
                // this DRV_STATUS_DCBX_NEGOTIATION_RES
                if(FALSE == IS_DCB_ENABLED(pdev))
                {
                    return;
                }
                pdev->dcbx_info.dcbx_update_lpme_task_state = 
                    DCBX_UPDATE_TASK_STATE_SCHEDULE;
                lm_status = MM_REGISTER_LPME(pdev, 
                                             lm_dcbx_update_lpme_set_params,
                                             TRUE,
                                             FALSE);// DCBX sends ramrods

                if (LM_STATUS_SUCCESS != lm_status)
                {
                    pdev->dcbx_info.dcbx_update_lpme_task_state = 
                        DCBX_UPDATE_TASK_STATE_FREE;
                    if(LM_STATUS_REQUEST_NOT_ACCEPTED == lm_status)
                    {// DCBX MM_REGISTER_LPME can fail
                        pdev->dcbx_info.lpme_failed_cnt++;
                        return;
                    }
                    pdev->dcbx_info.dcbx_error |= DCBX_ERROR_REGISTER_LPME;
                    // No rollback
                    // Problem because we won't get to DCBX_UPDATE_TASK_STATE_HANDLED (we won't schedule an interrupt)
                    DbgBreakMsg("lm_dcbx_int : The chip QM queues are stuck until an interrupt from MCP");
                    //Free the MCP
                    lm_status = lm_mcp_cmd_send_recieve( pdev, 
                                                     lm_mcp_mb_header, 
                                                     DRV_MSG_CODE_DCBX_PMF_DRV_OK,
                                                     0,
                                                     MCP_CMD_DEFAULT_TIMEOUT, 
                                                     &fw_resp ) ;

                   DbgBreakIf( lm_status != LM_STATUS_SUCCESS );

                }
                break;

            case DCBX_UPDATE_TASK_STATE_SCHEDULE:
                // Schedule: We saw before that DRV_STATUS_DCBX_NEGOTIATION_RES 
                // is set before, and didn’t handle it yet
                break;

            case DCBX_UPDATE_TASK_STATE_HANDLED:
                // handled: the WI handled was handled ,The MCP needs to updated
                pdev->dcbx_info.dcbx_update_lpme_task_state = 
                    DCBX_UPDATE_TASK_STATE_FREE;

                lm_status = lm_mcp_cmd_send_recieve( pdev, 
                                                     lm_mcp_mb_header, 
                                                     DRV_MSG_CODE_DCBX_PMF_DRV_OK,
                                                     0,
                                                     MCP_CMD_DEFAULT_TIMEOUT, 
                                                     &fw_resp ) ;

                DbgBreakIf( lm_status != LM_STATUS_SUCCESS );
                break;
            default:
                DbgBreakMsg("illegal value for dcbx_update_lpme_task_state");
                break;
            }
        }
    }
}

/*******************************************************************************
 * Description: Calculate the number of priority PG.
 * The number of priority pg should be derived from the available traffic type 
 * and pg_pri_orginal_spread configured priorities 
 *
 * Return:
 ******************************************************************************/
STATIC void
lm_dcbx_get_num_of_pg_traf_type(
    IN  lm_device_t     *pdev,
    IN  u32_t           pg_pri_orginal_spread[DCBX_MAX_NUM_PRI_PG_ENTRIES],
    OUT pg_help_data_t  *pg_help_data)
{
    u8_t    i                                       = 0;
    u8_t    b_pg_found                              = FALSE;
    u8_t    search_traf_type                        = 0;
    u8_t    add_traf_type                           = 0;
    u8_t    add_pg                                  = 0;

    ASSERT_STATIC( DCBX_MAX_NUM_PRI_PG_ENTRIES == 8);

    // Set to invalid
    for (i = 0; i < ARRSIZE(pg_help_data->pg_entry_data); i++)
    {
        pg_help_data->pg_entry_data[i].pg = DCBX_ILLEGAL_PG;
    }
   
    for (add_traf_type = 0; add_traf_type < ARRSIZE(pg_help_data->pg_entry_data); add_traf_type++)
    {
        ASSERT_STATIC(ARRSIZE(pg_help_data->pg_entry_data) == 
                      ARRSIZE(pdev->params.dcbx_port_params.app.traffic_type_priority));

        b_pg_found = FALSE;
        if (pdev->params.dcbx_port_params.app.traffic_type_priority[add_traf_type] < MAX_PFC_PRIORITIES)
        {
            add_pg = (u8_t)pg_pri_orginal_spread[pdev->params.dcbx_port_params.app.traffic_type_priority[add_traf_type]];
            for (search_traf_type = 0; search_traf_type < ARRSIZE(pg_help_data->pg_entry_data); search_traf_type++)
            {
                if (pg_help_data->pg_entry_data[search_traf_type].pg == add_pg)
                {
                    if(0 == (pg_help_data->pg_entry_data[search_traf_type].pg_priority & 
                             (1 << pdev->params.dcbx_port_params.app.traffic_type_priority[add_traf_type])))
                    {
                        pg_help_data->pg_entry_data[search_traf_type].num_of_dif_pri++;
                    }
                    pg_help_data->pg_entry_data[search_traf_type].pg_priority |= 
                        (1 << pdev->params.dcbx_port_params.app.traffic_type_priority[add_traf_type]);

                    b_pg_found = TRUE;
                    break;
                }
            }
            if(FALSE == b_pg_found)
            {
                pg_help_data->pg_entry_data[pg_help_data->num_of_pg].pg             = add_pg;
                pg_help_data->pg_entry_data[pg_help_data->num_of_pg].pg_priority    = (1 << pdev->params.dcbx_port_params.app.traffic_type_priority[add_traf_type]);
                pg_help_data->pg_entry_data[pg_help_data->num_of_pg].num_of_dif_pri = 1;
                pg_help_data->num_of_pg++;
            }
        }
    }
    DbgBreakIf(pg_help_data->num_of_pg > LLFC_DRIVER_TRAFFIC_TYPE_MAX); 
}
/*******************************************************************************
 * Description: Still 
 *
 * Return:
 ******************************************************************************/
STATIC void
lm_dcbx_fill_cos_entry(
    lm_device_t         *pdev,
    dcbx_cos_params_t   *cos_params,
    const u32_t         pri_join_mask,
    const u32_t         bw,
    const u8_t          pauseable,
    const u8_t          strict)
{
    cos_params->pauseable   = pauseable;
    cos_params->strict      = strict;
    if(pauseable)
    {
        cos_params->pri_bitmask = 
            DCBX_PFC_PRI_GET_PAUSE(pdev,pri_join_mask);
    }
    else
    {
        cos_params->pri_bitmask = 
            DCBX_PFC_PRI_GET_NON_PAUSE(pdev,pri_join_mask);
    }
    DbgBreakIf((0 != pri_join_mask) && (0 == cos_params->pri_bitmask));
    cos_params->bw_tbl = bw;
}

/*******************************************************************************
 * Description: single priority group 
 *
 * Return:
 ******************************************************************************/
STATIC void
lm_dcbx_ets_disabled_entry_data(
    IN  lm_device_t                 *pdev,
    OUT cos_help_data_t             *cos_data,
    IN  u32_t                       pri_join_mask)
{
      // Only one priority than only one COS
        cos_data->entry_data[0].b_pausable      = IS_DCBX_PFC_PRI_ONLY_PAUSE(pdev,pri_join_mask);
        cos_data->entry_data[0].pri_join_mask   = pri_join_mask;
        cos_data->entry_data[0].cos_bw          = 100;   
        cos_data->num_of_cos = 1;
}

/*******************************************************************************
 * Description: Updating the cos bw.
 *
 * Return:
 ******************************************************************************/
STATIC void
lm_dcbx_add_to_cos_bw(
    IN      lm_device_t             *pdev,
    OUT     cos_entry_help_data_t   *entry_data,
    IN      u8_t                    pg_bw)
{

    if(DCBX_INVALID_COS_BW == entry_data->cos_bw )
    {

        entry_data->cos_bw =  pg_bw;
    }
    else
    {
        entry_data->cos_bw +=  pg_bw;
    }
    DbgBreakIf(entry_data->cos_bw > DCBX_MAX_COS_BW);
}
/*******************************************************************************
 * Description: single priority group 
 *
 * Return:
 ******************************************************************************/
STATIC void
lm_dcbx_separate_pauseable_from_non(
    IN  lm_device_t                 *pdev,
    OUT cos_help_data_t             *cos_data,
    IN  const u32_t                 *pg_pri_orginal_spread,
    IN  const dcbx_ets_feature_t    *ets
    )
{
    u32_t       pri_tested      = 0;
    u8_t        i               = 0;
    u8_t        entry           = 0;
    u8_t        pg_entry        = 0;
    const u8_t  num_of_pri      = ARRSIZE(pdev->params.dcbx_port_params.app.traffic_type_priority);

    cos_data->entry_data[0].b_pausable = TRUE;
    cos_data->entry_data[1].b_pausable = FALSE;
    cos_data->entry_data[0].pri_join_mask = cos_data->entry_data[1].pri_join_mask = 0;

    for(i=0 ; i < num_of_pri ; i++)
    {
        DbgBreakIf(pdev->params.dcbx_port_params.app.traffic_type_priority[i] >= MAX_PFC_PRIORITIES);
        pri_tested = 1 << pdev->params.dcbx_port_params.app.traffic_type_priority[i];

        if(pri_tested & DCBX_PFC_PRI_NON_PAUSE_MASK(pdev))
        {
            cos_data->entry_data[1].pri_join_mask |= pri_tested;
            entry       = 1;
        }
        else
        {
            cos_data->entry_data[0].pri_join_mask |= pri_tested;
            entry = 0;
            
        }
        pg_entry    = (u8_t)pg_pri_orginal_spread[pdev->params.dcbx_port_params.app.traffic_type_priority[i]];
        // There can be only one strict pg
        if( pg_entry < DCBX_MAX_NUM_PRI_PG_ENTRIES)
        {
            lm_dcbx_add_to_cos_bw(pdev, 
                                  &(cos_data->entry_data[entry]), 
                                  DCBX_PG_BW_GET(ets->pg_bw_tbl, pg_entry));
        }
        else
        {
            // If we join a group and one is strict than the bw rulls 
            cos_data->entry_data[entry].strict = DCBX_COS_HIGH_STRICT;
        }
    }//end of for
    // Both groups must have priorities
    DbgBreakIf(( 0 == cos_data->entry_data[0].pri_join_mask) && ( 0 == cos_data->entry_data[1].pri_join_mask));
}
/*******************************************************************************
 * Description: single priority group 
 *
 * Return:
 ******************************************************************************/
STATIC void
lm_dcbx_single_pg_to_cos_params(
    IN  lm_device_t                 *pdev,
    IN  pg_help_data_t              *pg_help_data,
    OUT cos_help_data_t             *cos_data,
    IN  u32_t                       pri_join_mask,
    IN  u8_t                        num_of_dif_pri
    )
{
    u8_t                    i                           = 0;
    u32_t                   pri_tested                  = 0;
    u32_t                   pri_mask_without_pri        = 0;

    if(1 == num_of_dif_pri)
    {
      // Only one priority than only one COS
        lm_dcbx_ets_disabled_entry_data(pdev,cos_data,pri_join_mask);
        return;
    }
    
    if( pg_help_data->pg_entry_data[0].pg < DCBX_MAX_NUM_PG_BW_ENTRIES)
    {// BW limited
        // If there are both pauseable and non-pauseable priorities, the pauseable priorities go to the first queue and the non-pauseable 
        // priorities go to the second queue. 
        if(IS_DCBX_PFC_PRI_MIX_PAUSE(pdev,pri_join_mask))
        {
            DbgBreakIf( 1 == num_of_dif_pri );
            // Pause able
            cos_data->entry_data[0].b_pausable = TRUE;
            // Non pause able.
            cos_data->entry_data[1].b_pausable = FALSE;

            if(2 == num_of_dif_pri)
            {
                cos_data->entry_data[0].cos_bw = 50;
                cos_data->entry_data[1].cos_bw = 50;
            }
            if (3 == num_of_dif_pri) 
            {
                // We need to find out how has only one priority and how has two priorities.
                // If the pri_bitmask is a power of 2 than there is only one priority.
                if(POWER_OF_2(DCBX_PFC_PRI_GET_PAUSE(pdev,pri_join_mask)))
                {
                    DbgBreakIf(POWER_OF_2(DCBX_PFC_PRI_GET_NON_PAUSE(pdev,pri_join_mask)));
                    cos_data->entry_data[0].cos_bw = 33;
                    cos_data->entry_data[1].cos_bw = 67;
                }
                else
                {
                    DbgBreakIf(FALSE == POWER_OF_2(DCBX_PFC_PRI_GET_NON_PAUSE(pdev,pri_join_mask)));
                    cos_data->entry_data[0].cos_bw = 67;
                    cos_data->entry_data[1].cos_bw = 33;
                }
            }
        }
        else if(IS_DCBX_PFC_PRI_ONLY_PAUSE(pdev,pri_join_mask))
        {// If there are only pauseable priorities, then one/two priorities go 
         // to the first queue and one priority goes to the second queue.
            if(2 == num_of_dif_pri)
            {
                cos_data->entry_data[0].cos_bw = 50;
                cos_data->entry_data[1].cos_bw = 50;
            }
            else
            {
                DbgBreakIf(3 != num_of_dif_pri);
                cos_data->entry_data[0].cos_bw = 67;
                cos_data->entry_data[1].cos_bw = 33;
            }
            cos_data->entry_data[0].b_pausable       = cos_data->entry_data[1].b_pausable = TRUE;
            // All priorities except FCOE
            cos_data->entry_data[0].pri_join_mask    = (pri_join_mask & ((u8_t)~(1 << pdev->params.dcbx_port_params.app.traffic_type_priority[LLFC_TRAFFIC_TYPE_FCOE])));
            // Only FCOE priority.
            cos_data->entry_data[1].pri_join_mask    = (1 << pdev->params.dcbx_port_params.app.traffic_type_priority[LLFC_TRAFFIC_TYPE_FCOE]);
        }
        else
        {//If there are only non-pauseable priorities, they will all go to the same queue.
            DbgBreakIf(FALSE == IS_DCBX_PFC_PRI_ONLY_NON_PAUSE(pdev,pri_join_mask));
            lm_dcbx_ets_disabled_entry_data(pdev,cos_data,pri_join_mask);
        }
    }
    else
    {
        // priority group which is not BW limited (PG#15):
        DbgBreakIf(DCBX_STRICT_PRIORITY != pg_help_data->pg_entry_data[0].pg);
        if(IS_DCBX_PFC_PRI_MIX_PAUSE(pdev,pri_join_mask))
        {
            // If there are both pauseable and non-pauseable priorities, the pauseable priorities go 
            // to the first queue and the non-pauseable priorities go to the second queue.
            if(DCBX_PFC_PRI_GET_PAUSE(pdev,pri_join_mask) > DCBX_PFC_PRI_GET_NON_PAUSE(pdev,pri_join_mask))
            {
                cos_data->entry_data[0].strict        = DCBX_COS_HIGH_STRICT;
                cos_data->entry_data[1].strict        = DCBX_COS_LOW_STRICT;
            }
            else
            {
                cos_data->entry_data[0].strict        = DCBX_COS_LOW_STRICT;
                cos_data->entry_data[1].strict        = DCBX_COS_HIGH_STRICT;
            }
            // Pause-able
            cos_data->entry_data[0].b_pausable = TRUE;
            // Non pause-able.
            cos_data->entry_data[1].b_pausable = FALSE;
        }
        else
        {
            // If there are only pauseable priorities or only non-pauseable, the lower priorities
            // go to the first queue and the higher priorities go to the second queue.
            cos_data->entry_data[0].b_pausable = cos_data->entry_data[1].b_pausable = IS_DCBX_PFC_PRI_ONLY_PAUSE(pdev,pri_join_mask);

            for(i=0 ; i < ARRSIZE(pdev->params.dcbx_port_params.app.traffic_type_priority) ; i++)
            {
                DbgBreakIf(pdev->params.dcbx_port_params.app.traffic_type_priority[i] >= MAX_PFC_PRIORITIES);
                pri_tested = 1 << pdev->params.dcbx_port_params.app.traffic_type_priority[i];
                // Remove priority tested
                pri_mask_without_pri = (pri_join_mask & ((u8_t)(~pri_tested)));
                if( pri_mask_without_pri < pri_tested )
                {
                    break;
                }
            }

            if(i == ARRSIZE(pdev->params.dcbx_port_params.app.traffic_type_priority))
            {
                DbgBreakMsg("lm_dcbx_fill_cos_params : Invalid value for pri_join_mask could not find a priority \n");
            }
            cos_data->entry_data[0].pri_join_mask = pri_mask_without_pri;
            cos_data->entry_data[1].pri_join_mask = pri_tested;
            // Both queues are strict priority, and that with the highest priority
            // gets the highest strict priority in the arbiter.
            cos_data->entry_data[0].strict  = DCBX_COS_LOW_STRICT;
            cos_data->entry_data[1].strict  = DCBX_COS_HIGH_STRICT;
        }
    }

}
/*******************************************************************************
 * Description: Still 
 *
 * Return:
 ******************************************************************************/
STATIC void
lm_dcbx_two_pg_to_cos_params(
    IN  lm_device_t                 *pdev,
    IN  pg_help_data_t              *pg_help_data,
    IN  const dcbx_ets_feature_t    *ets,
    OUT cos_help_data_t             *cos_data,
    IN  const u32_t                 *pg_pri_orginal_spread,
    IN  u32_t                       pri_join_mask,
    IN  u8_t                        num_of_dif_pri)
{
    u8_t                    i                           = 0;
    u8_t                    pg[E2_NUM_OF_COS]           = {0};

    // If there are both pauseable and non-pauseable priorities, the pauseable priorities
    // go to the first queue and the non-pauseable priorities go to the second queue. 
    if(IS_DCBX_PFC_PRI_MIX_PAUSE(pdev,pri_join_mask))
    {
        if(IS_DCBX_PFC_PRI_MIX_PAUSE(pdev, pg_help_data->pg_entry_data[0].pg_priority) ||
                IS_DCBX_PFC_PRI_MIX_PAUSE(pdev, pg_help_data->pg_entry_data[1].pg_priority))
        {
            // If one PG contains both pauseable and non-pauseable priorities then ETS is disabled.
            DbgMessage(pdev,WARN,"lm_dcbx_fill_cos_params : PG contains both pauseable and non-pauseable "
                        "priorities -> ETS is disabled. \n");
            lm_dcbx_separate_pauseable_from_non(pdev,cos_data,pg_pri_orginal_spread,ets);            
            // ETS disabled wrong configuration
            pdev->params.dcbx_port_params.ets.enabled = FALSE;
            return;
        }

        // Pause-able
        cos_data->entry_data[0].b_pausable = TRUE;
        // Non pause-able.
        cos_data->entry_data[1].b_pausable = FALSE;
        if(IS_DCBX_PFC_PRI_ONLY_PAUSE(pdev, pg_help_data->pg_entry_data[0].pg_priority))
        {// 0 is pause-able
            cos_data->entry_data[0].pri_join_mask    = pg_help_data->pg_entry_data[0].pg_priority;
            pg[0]                                   = pg_help_data->pg_entry_data[0].pg;
            cos_data->entry_data[1].pri_join_mask    = pg_help_data->pg_entry_data[1].pg_priority;
            pg[1]                                   = pg_help_data->pg_entry_data[1].pg;
        }
        else
        {// 1 is pause-able
            cos_data->entry_data[0].pri_join_mask    = pg_help_data->pg_entry_data[1].pg_priority;
            pg[0]                                   = pg_help_data->pg_entry_data[1].pg;
            cos_data->entry_data[1].pri_join_mask    = pg_help_data->pg_entry_data[0].pg_priority;
            pg[1]                                   = pg_help_data->pg_entry_data[0].pg;
        }
    }
    else
    {
        //If there are only pauseable priorities or only non-pauseable, each PG goes to a queue.
        cos_data->entry_data[0].b_pausable       = cos_data->entry_data[1].b_pausable = IS_DCBX_PFC_PRI_ONLY_PAUSE(pdev,pri_join_mask);
        cos_data->entry_data[0].pri_join_mask    = pg_help_data->pg_entry_data[0].pg_priority;
        pg[0]                                   = pg_help_data->pg_entry_data[0].pg;
        cos_data->entry_data[1].pri_join_mask    = pg_help_data->pg_entry_data[1].pg_priority;
        pg[1]                                   = pg_help_data->pg_entry_data[1].pg;
    }

    // There can be only one strict pg
    for(i=0 ; i < ARRSIZE(pg) ; i++)
    {
        if( pg[i] < DCBX_MAX_NUM_PG_BW_ENTRIES)
        {
            cos_data->entry_data[i].cos_bw =  DCBX_PG_BW_GET( ets->pg_bw_tbl,pg[i]);
        }
        else
        {
            cos_data->entry_data[i].strict = DCBX_COS_HIGH_STRICT;
        }
    }
}

/*******************************************************************************
 * Description: Still 
 *
 * Return:
 ******************************************************************************/
STATIC void
lm_dcbx_three_pg_to_cos_params(
    IN  lm_device_t                 *pdev,
    IN  pg_help_data_t              *pg_help_data,
    IN  const dcbx_ets_feature_t    *ets,
    OUT cos_help_data_t             *cos_data,
    IN  const u32_t                 *pg_pri_orginal_spread,
    IN  u32_t                       pri_join_mask,
    IN  u8_t                        num_of_dif_pri)
{
    u8_t        i               = 0;
    u32_t       pri_tested      = 0;
    u8_t        entry           = 0;
    u8_t        pg_entry        = 0;
    u8_t        b_found_strict  = FALSE;
    u8_t        num_of_pri      = ARRSIZE(pdev->params.dcbx_port_params.app.traffic_type_priority);
    DbgBreakIf(3 != num_of_pri);
    cos_data->entry_data[0].pri_join_mask = cos_data->entry_data[1].pri_join_mask = 0;
    //- If there are both pauseable and non-pauseable priorities, the pauseable priorities go to the first 
    // queue and the non-pauseable priorities go to the second queue. 
    if(IS_DCBX_PFC_PRI_MIX_PAUSE(pdev,pri_join_mask))
    {
        lm_dcbx_separate_pauseable_from_non(pdev,cos_data,pg_pri_orginal_spread,ets);            
    }
    else
    {
        DbgBreakIf(!(IS_DCBX_PFC_PRI_ONLY_NON_PAUSE(pdev,pri_join_mask) ||
            IS_DCBX_PFC_PRI_ONLY_PAUSE(pdev,pri_join_mask)));

        //- If two BW-limited PG-s were combined to one queue, the BW is their sum.  
        //- If there are only pauseable priorities or only non-pauseable, and there are both BW-limited and 
        // non-BW-limited PG-s, the BW-limited PG/s go to one queue and the non-BW-limited PG/s go to the 
        // second queue.
        //- If there are only pauseable priorities or only non-pauseable and all are BW limited, then 
        // two priorities go to the first queue and one priority goes to the second queue.

        //  We will join this two cases: 
        // if one is BW limited he will go to the secoend queue otherwise the last priority will get it 
       
        cos_data->entry_data[0].b_pausable = cos_data->entry_data[1].b_pausable = IS_DCBX_PFC_PRI_ONLY_PAUSE(pdev,pri_join_mask);

        for(i=0 ; i < num_of_pri; i++)
        {
            DbgBreakIf(pdev->params.dcbx_port_params.app.traffic_type_priority[i] >= MAX_PFC_PRIORITIES);
            pri_tested = 1 << pdev->params.dcbx_port_params.app.traffic_type_priority[i];
            pg_entry    = (u8_t)pg_pri_orginal_spread[pdev->params.dcbx_port_params.app.traffic_type_priority[i]];

            if(pg_entry < DCBX_MAX_NUM_PG_BW_ENTRIES)
            {
                entry = 0;

                if((i == (num_of_pri-1))&&
                   (FALSE == b_found_strict) )
                {/* last entry will be handled separately */
                    // If no priority is strict than last enty goes to last queue.
                    entry = 1; 
                }
                cos_data->entry_data[entry].pri_join_mask |= pri_tested;
                lm_dcbx_add_to_cos_bw(pdev, &(cos_data->entry_data[entry]), DCBX_PG_BW_GET(ets->pg_bw_tbl, pg_entry));
            }
            else
            {
                DbgBreakIf(TRUE == b_found_strict );
                b_found_strict = TRUE;
                cos_data->entry_data[1].pri_join_mask |= pri_tested;
                // If we join a group and one is strict than the bw rulls 
                cos_data->entry_data[1].strict = DCBX_COS_HIGH_STRICT;
            }

        }//end of for
    }
}
/**
 * 
 * 
 * @description
 * 
 * @param pdev 
 * @param pg_help_data 
 * @param ets 
 * @param pg_pri_orginal_spread 
 *
 * @return STATIC void 
 */
STATIC void
lm_dcbx_fill_cos_params(
    IN  lm_device_t                 *pdev,
    IN  pg_help_data_t              *pg_help_data,
    IN  const dcbx_ets_feature_t    *ets,
    IN  const u32_t                 *pg_pri_orginal_spread)
{
    cos_help_data_t         cos_data                    = {{{0}}};
    u8_t                    i                           = 0;
    u32_t                   pri_join_mask               = 0;
    u8_t                    num_of_dif_pri              = 0;


    // Validate the pg value
    for(i=0; i < pg_help_data->num_of_pg ; i++)
    {
        DbgBreakIf((DCBX_STRICT_PRIORITY != pg_help_data->pg_entry_data[i].pg) &&
                   (DCBX_MAX_NUM_PG_BW_ENTRIES <= pg_help_data->pg_entry_data[i].pg));
        pri_join_mask   |=  pg_help_data->pg_entry_data[i].pg_priority;
        num_of_dif_pri  += pg_help_data->pg_entry_data[i].num_of_dif_pri;
    }

    //default settings
    cos_data.num_of_cos = 2;
    for(i=0; i < ARRSIZE(cos_data.entry_data) ; i++)
    {
        cos_data.entry_data[i].pri_join_mask    = pri_join_mask;
        cos_data.entry_data[i].b_pausable       = FALSE;
        cos_data.entry_data[i].strict           = DCBX_COS_NOT_STRICT;
        cos_data.entry_data[i].cos_bw           = DCBX_INVALID_COS_BW;
    }

    DbgBreakIf((0 == num_of_dif_pri) && (3 < num_of_dif_pri));
    
    switch(pg_help_data->num_of_pg)
    {
    case 1:
        //single priority group
        lm_dcbx_single_pg_to_cos_params(
            pdev,
            pg_help_data,
            &cos_data,
            pri_join_mask,
            num_of_dif_pri);
    break;
    case 2:
        lm_dcbx_two_pg_to_cos_params(
            pdev,
            pg_help_data,
            ets,
            &cos_data,
            pg_pri_orginal_spread,
            pri_join_mask,
            num_of_dif_pri);
    break;

    case 3:
        // Three pg must mean three priorities.

        lm_dcbx_three_pg_to_cos_params(
            pdev,
            pg_help_data,
            ets,
            &cos_data,
            pg_pri_orginal_spread,
            pri_join_mask,
            num_of_dif_pri);
        
    break;
    default:
        DbgBreakMsg("lm_dcbx_fill_cos_params :Wrong pg_help_data->num_of_pg \n");
        lm_dcbx_ets_disabled_entry_data(pdev,&cos_data,pri_join_mask);
    }

    for(i=0; i < cos_data.num_of_cos ; i++)
    {
        lm_dcbx_fill_cos_entry(pdev,
                               &pdev->params.dcbx_port_params.ets.cos_params[i],
                               cos_data.entry_data[i].pri_join_mask,
                               cos_data.entry_data[i].cos_bw,
                               cos_data.entry_data[i].b_pausable,
                               cos_data.entry_data[i].strict);
    }

    DbgBreakIf(0 == cos_data.num_of_cos);

    DbgBreakIf(pri_join_mask != (pdev->params.dcbx_port_params.ets.cos_params[0].pri_bitmask | 
                                 pdev->params.dcbx_port_params.ets.cos_params[1].pri_bitmask));

    DbgBreakIf(0 != (pdev->params.dcbx_port_params.ets.cos_params[0].pri_bitmask & 
                                 pdev->params.dcbx_port_params.ets.cos_params[1].pri_bitmask));

    pdev->params.dcbx_port_params.ets.num_of_cos = cos_data.num_of_cos ;
}
/*******************************************************************************
 * Description: Translate from ETS parameter to COS paramters 
 *
 * Return:
 ******************************************************************************/
STATIC void
lm_dcbx_get_ets_feature(
    INOUT       lm_device_t         *pdev, 
    INOUT       dcbx_ets_feature_t  *ets,
    IN const    u32_t               error)
{
    u32_t           pg_pri_orginal_spread[DCBX_MAX_NUM_PRI_PG_ENTRIES]  = {0};
    pg_help_data_t  pg_help_data                                        = {{{0}}};
    u8_t            i                                                   = 0;

    // Clean up old settings of ets on COS
    pdev->params.dcbx_port_params.ets.num_of_cos = 0 ;    
    for(i=0; i < ARRSIZE(pdev->params.dcbx_port_params.ets.cos_params) ; i++)
    {
        lm_dcbx_fill_cos_entry(pdev,
                               &pdev->params.dcbx_port_params.ets.cos_params[i],
                               0,
                               DCBX_INVALID_COS_BW,
                               FALSE,
                               DCBX_COS_NOT_STRICT);
    }

    if((TRUE == pdev->params.dcbx_port_params.app.enabled)&&
       (!GET_FLAGS(error,DCBX_LOCAL_ETS_ERROR))&&
        ets->enabled)
    {
        pdev->params.dcbx_port_params.ets.enabled = TRUE;
    }
    else
    {
        pdev->params.dcbx_port_params.ets.enabled = FALSE;
        // When ETS is disabled we treat it like single priority BW limited.
        // Aim all the priorities to PG 0
        mm_mem_zero((void *) ((ets->pri_pg_tbl)),sizeof(ets->pri_pg_tbl));
        // This has no effect all of them are target to PG 0.
        for(i = 0; i < DCBX_MAX_NUM_PG_BW_ENTRIES ; i++)
        {
            DCBX_PG_BW_SET(ets->pg_bw_tbl, i, 1);
        }
    }


    //Parse pg_pri_orginal_spread data and spread it from nibble to 32 bits
    lm_dcbx_get_ets_pri_pg_tbl(pdev,
                               pg_pri_orginal_spread,
                               ets->pri_pg_tbl,
                               ARRSIZE(pg_pri_orginal_spread),
                               DCBX_MAX_NUM_PRI_PG_ENTRIES);
    
    lm_dcbx_get_num_of_pg_traf_type(pdev,
                                    pg_pri_orginal_spread,
                                    &pg_help_data);

    lm_dcbx_fill_cos_params(pdev,
                            &pg_help_data,
                            ets,
                            pg_pri_orginal_spread);

}

/*******************************************************************************
 * Description:
 *
 * Return:
 ******************************************************************************/
STATIC void
lm_dcbx_get_pfc_feature(
    INOUT lm_device_t              *pdev, 
    IN const dcbx_pfc_feature_t    *pfc,
    IN const u32_t                 error
    )
{

    if((TRUE == pdev->params.dcbx_port_params.app.enabled)&&
       (!GET_FLAGS(error,(DCBX_LOCAL_PFC_ERROR | DCBX_LOCAL_PFC_MISMATCH )))&&
        pfc->enabled)
    {
        pdev->params.dcbx_port_params.pfc.enabled = TRUE;
        pdev->params.dcbx_port_params.pfc.priority_non_pauseable_mask = (u8_t)(~(pfc->pri_en_bitmap));
    }
    else
    {
        pdev->params.dcbx_port_params.pfc.enabled = FALSE;
        pdev->params.dcbx_port_params.pfc.priority_non_pauseable_mask = 0;
    }

}

/*******************************************************************************
 * Description: Take the highest priority available.(This function don't do the 
 *              initialization of priority.) 
 *
 * Return:
******************************************************************************/
STATIC void
lm_dcbx_get_ap_priority(
    lm_device_t                     *pdev, 
    u8_t                            pri_bitmap,
    u8_t                            llfc_traffic_type)
{
    u8_t pri = MAX_PFC_PRIORITIES;
    u8_t index = MAX_PFC_PRIORITIES -1 ;   
    u8_t pri_mask = 0;

    //Chose the highest priority
    while((MAX_PFC_PRIORITIES == pri )&&
          (0 != index))
    {
        pri_mask = 1 <<(index);
        if(GET_FLAGS(pri_bitmap , pri_mask)) 
        {
            pri = index ;        
        }
        index--;
    }

    if(pri < MAX_PFC_PRIORITIES)
    {
        pdev->params.dcbx_port_params.app.traffic_type_priority[llfc_traffic_type] = 
            max(pdev->params.dcbx_port_params.app.traffic_type_priority[llfc_traffic_type],
                pri);
    
    }
}
/*******************************************************************************
 *  Description:
 *  Traffic type (protocol) identification:
 *	Networking is identified by Ether-Type = IPv4 or Ether-Type =IPv6.
 *	iSCSI is  by TCP-port = iSCSI well know port (3260)
 *	FCoE is identified by Ether-type = FCoE
 *	Theoretically each protocol can be associated with multiple priorities (a priority bit map). In this case we choose the highest one.
 *	Priority assignment for networking:
 *	1.	If IPv4 is identified, the networking priority is the IPv4 priority (highest one as mentioned above).
 *	2.	Otherwise if IPv6 is identified, the networking priority is the IPv6 priority.
 *	3.	Otherwise the networking priority is set 0. (All other protocol TLVs which are not iSCSI or FCoE are ignored).
 *	
 *	Priority assignment for iSCSI:
 *	1.	If iSCSI is identified, then obviously this is the iSCSI priority (again the highest one).
 *	2.	Otherwise iSCSI priority is set to 0.
 *	
 *	Priority assignment for FCoE:
 *	1.	If FCoE is identified, then obviously this is the FCoE priority (again the highest one).
 *	2.	Otherwise FCoE priority is set to 0.
 * Return:
 ******************************************************************************/
STATIC void
lm_dcbx_get_ap_feature(
    lm_device_t                             *pdev, 
    IN const dcbx_app_priority_feature_t    *app,
    IN const u32_t                          error)
{
    u8_t    index = 0;
    u16_t   app_protocol_id     = 0;

    if((app->enabled)&&
       (!GET_FLAGS(error,(DCBX_LOCAL_APP_ERROR | DCBX_LOCAL_APP_MISMATCH))))
    {

        pdev->params.dcbx_port_params.app.enabled = TRUE;

        for( index=0 ; index < ARRSIZE(pdev->params.dcbx_port_params.app.traffic_type_priority) ;index++)
        {
            pdev->params.dcbx_port_params.app.traffic_type_priority[index] = 0;
        }
    
        if(app->default_pri < MAX_PFC_PRIORITIES)
        {
            pdev->params.dcbx_port_params.app.traffic_type_priority[LLFC_TRAFFIC_TYPE_NW] = 
                app->default_pri;
        }

        for(index=0 ;
             (index < DCBX_MAX_APP_PROTOCOL);
              index++)
        {
            
            app_protocol_id = app->app_pri_tbl[index].app_id;
            
            // This has no logic this is only done for supporting old bootcodes.
            // The boot code still expexts u8 [2] instead of u16
            app_protocol_id = app_protocol_id;
            if(GET_FLAGS(app->app_pri_tbl[index].appBitfield,DCBX_APP_SF_ETH_TYPE)&&
               (ETH_TYPE_FCOE == app_protocol_id))
            {    
                lm_dcbx_get_ap_priority(pdev,
                                        app->app_pri_tbl[index].pri_bitmap,
                                        LLFC_TRAFFIC_TYPE_FCOE);
            }

            if(GET_FLAGS(app->app_pri_tbl[index].appBitfield,DCBX_APP_SF_PORT)&&
               (TCP_PORT_ISCSI == app_protocol_id))
            {
                lm_dcbx_get_ap_priority(pdev,
                                        app->app_pri_tbl[index].pri_bitmap,
                                        LLFC_TRAFFIC_TYPE_ISCSI);
            }
        }
    }
    else
    {
        pdev->params.dcbx_port_params.app.enabled = FALSE;
        for( index=0 ; 
             index < ARRSIZE(pdev->params.dcbx_port_params.app.traffic_type_priority) ;
             index++)
        {
            pdev->params.dcbx_port_params.app.traffic_type_priority[index] = 
                INVALID_TRAFFIC_TYPE_PRIORITY;
        }
    }
}
/*******************************************************************************
 * Description: Translate PFC/PG parameters to VBD parameters and call relevent 
 * Function to set the parameters.
 *
 * Return:
******************************************************************************/
STATIC void 
lm_print_dcbx_drv_param(IN struct _lm_device_t     *pdev,
                        IN const lldp_local_mib_t  *local_mib)
{
#if DBG
    u8_t i =0;
    DbgMessage1(pdev,INFORM,"local_mib.error %x\n",local_mib->error);

    //Pg
    DbgMessage1(pdev,INFORM,"local_mib.features.ets.enabled %x\n",local_mib->features.ets.enabled);
    for(i=0;i<DCBX_MAX_NUM_PG_BW_ENTRIES;i++) 
    {
        DbgMessage2(pdev,INFORM,"local_mib.features.ets.pg_bw_tbl[%x] %x\n",i,DCBX_PG_BW_GET(local_mib->features.ets.pg_bw_tbl,i));
    }
    for(i=0;i<DCBX_MAX_NUM_PRI_PG_ENTRIES;i++) 
    {
        DbgMessage2(pdev,INFORM,"local_mib.features.ets.pri_pg_tbl[%x] %x\n",i,DCBX_PRI_PG_GET(local_mib->features.ets.pri_pg_tbl,i));
    }

    //pfc
    DbgMessage1(pdev,INFORM,"local_mib.features.pfc.pri_en_bitmap %x\n",local_mib->features.pfc.pri_en_bitmap);
    DbgMessage1(pdev,INFORM,"local_mib.features.pfc.pfc_caps %x\n",local_mib->features.pfc.pfc_caps);
    DbgMessage1(pdev,INFORM,"local_mib.features.pfc.enabled %x\n",local_mib->features.pfc.enabled);

    DbgMessage1(pdev,INFORM,"local_mib.features.app.default_pri %x\n",local_mib->features.app.default_pri);
    DbgMessage1(pdev,INFORM,"local_mib.features.app.tc_supported %x\n",local_mib->features.app.tc_supported);
    DbgMessage1(pdev,INFORM,"local_mib.features.app.enabled %x\n",local_mib->features.app.enabled);
    for(i=0;i<DCBX_MAX_APP_PROTOCOL;i++)
    {

        // This has no logic this is only done for supporting old bootcodes.
        // The boot code still expexts u8 [2] instead of u16
        DbgMessage2(pdev,INFORM,"local_mib.features.app.app_pri_tbl[%x].app_id %x\n",
                    i,local_mib->features.app.app_pri_tbl[i].app_id);
        DbgMessage2(pdev,INFORM,"local_mib.features.app.app_pri_tbl[%x].pri_bitmap %x\n",
                    i,local_mib->features.app.app_pri_tbl[i].pri_bitmap);
        DbgMessage2(pdev,INFORM,"local_mib.features.app.app_pri_tbl[%x].appBitfield %x\n",
                    i,local_mib->features.app.app_pri_tbl[i].appBitfield);
    }
#endif
}
/*******************************************************************************
 * Description: Translate PFC/PG parameters to VBD parameters and call relevent 
 * Function to set the parameters.
 *
 * Return:
 ******************************************************************************/
STATIC void 
lm_get_dcbx_drv_param(INOUT     lm_device_t         *pdev,
                      IN const  dcbx_port_params_t  *dcbx_port_params,
                      INOUT     lldp_local_mib_t    *local_mib)
{
    lm_dcbx_get_ap_feature(
        pdev,
        &(local_mib->features.app),
        local_mib->error);
    
    lm_dcbx_get_pfc_feature(
        pdev,
        &(local_mib->features.pfc),
        local_mib->error);

    lm_dcbx_get_ets_feature(
        pdev,
        &(local_mib->features.ets),
        local_mib->error);          
}
/*******************************************************************************
 * Description: Should be integrate with write and moved to common code
 *
 * Return:
******************************************************************************/
STATIC void 
lm_dcbx_read_shmem2_mcp_fields(struct _lm_device_t * pdev,
                               u32_t                 offset,
                               u32_t               * val)
{
    u32_t shmem2_size;
    
    if (pdev->hw_info.shmem_base2 != 0) 
    {
        LM_SHMEM2_READ(pdev, OFFSETOF(shmem2_region_t,size), &shmem2_size);
        if (shmem2_size > offset) 
        {
            LM_SHMEM2_READ(pdev, offset, val);
        }
    }
}

/*******************************************************************************
 * Description:Should be integrate with read and moved to common code
 *
 * Return:
******************************************************************************/
STATIC void 
lm_dcbx_write_shmem2_mcp_fields(struct _lm_device_t *pdev,
                                u32_t               offset,
                                u32_t               val)
{
    u32_t shmem2_size;
    
    if (pdev->hw_info.shmem_base2 != 0) 
    {
        LM_SHMEM2_READ(pdev, OFFSETOF(shmem2_region_t,size), &shmem2_size);
        if (shmem2_size > offset) 
        {
            LM_SHMEM2_WRITE(pdev, offset, val);
        }
    }
}
/*******************************************************************************
 * Description:
 *
 * Return:
******************************************************************************/
STATIC void 
lm_dcbx_stop_hw_tx(struct _lm_device_t * pdev)
{

    // TODO DCBX change to cmd_id
    lm_eq_ramrod_post_sync(pdev,
                           RAMROD_CMD_ID_COMMON_STOP_TRAFFIC,
                           0,
                           CMD_PRIORITY_MEDIUM,/* Called from WI must be done ASAP*/
                           &(pdev->dcbx_info.dcbx_ramrod_state),
                           FUNCTION_DCBX_STOP_POSTED, 
                           FUNCTION_DCBX_STOP_COMPLETED); 

}
/*******************************************************************************
 * Description:
 *
 * Return:
******************************************************************************/
STATIC void 
lm_dcbx_resume_hw_tx(struct _lm_device_t * pdev)
{

    lm_eq_ramrod_post_sync(pdev,
                           RAMROD_CMD_ID_COMMON_START_TRAFFIC,
                           pdev->dcbx_info.pfc_fw_cfg_phys.as_u64,
                           CMD_PRIORITY_HIGH,/* Called from WI must be done ASAP*/
                           &(pdev->dcbx_info.dcbx_ramrod_state),
                           FUNCTION_DCBX_START_POSTED, 
                           FUNCTION_DCBX_START_COMPLETED); 

}
/*******************************************************************************
 * Description:
 *
 * Return:
******************************************************************************/
#define DCBX_LOCAL_MIB_MAX_TRY_READ             (100)
STATIC lm_status_t 
lm_dcbx_read_remote_local_mib(IN        struct _lm_device_t  *pdev,
                              OUT       u32_t                *base_mib_addr,
                              IN        u32_t                offset,
                              IN const  dcbx_read_mib_type   read_mib_type)
{
    static const u8_t dcbx_local_mib_max_try_read = DCBX_LOCAL_MIB_MAX_TRY_READ;
    u8_t    max_try_read            = 0 ,i =0;
    u32_t * buff                    = NULL;
    u32_t   mib_size                = 0,prefix_seq_num = 0 ,suffix_seq_num = 0;
    lldp_remote_mib_t *remote_mib   = NULL;
    lldp_local_mib_t  *local_mib    = NULL;

    // verify no wraparound on while loop
    ASSERT_STATIC( sizeof( max_try_read ) == sizeof(u8_t) );
    ASSERT_STATIC(DCBX_LOCAL_MIB_MAX_TRY_READ < ((u8_t)-1));
    
    switch (read_mib_type) 
    {
    case DCBX_READ_LOCAL_MIB:
        mib_size = sizeof(lldp_local_mib_t);
        break;
    case DCBX_READ_REMOTE_MIB:
        mib_size = sizeof(lldp_remote_mib_t);
        break;
    default:
        DbgBreakIf(1);
        return LM_STATUS_FAILURE;
    }
    
    offset += PORT_ID(pdev) * mib_size;

    do 
    {
        buff = base_mib_addr;
    
        for(i=0 ;i<mib_size; i+=4,buff++) 
        {
            *buff = REG_RD(pdev,
                          offset + i);
        }
        max_try_read++;


        switch (read_mib_type) 
        {
        case DCBX_READ_LOCAL_MIB:
            local_mib   = (lldp_local_mib_t *) base_mib_addr;
            prefix_seq_num = local_mib->prefix_seq_num;
            suffix_seq_num = local_mib->suffix_seq_num;
            break;
        case DCBX_READ_REMOTE_MIB:
            remote_mib   = (lldp_remote_mib_t *) base_mib_addr;
            prefix_seq_num = remote_mib->prefix_seq_num;
            suffix_seq_num = remote_mib->suffix_seq_num;
            break;
        default:
            DbgBreakIf(1);
            return LM_STATUS_FAILURE;
        }
    }while((prefix_seq_num != suffix_seq_num)&&
           (max_try_read <dcbx_local_mib_max_try_read));

    
    if(max_try_read >= dcbx_local_mib_max_try_read)
    {
        DbgBreakMsg("prefix_seq_num doesnt equal suffix_seq_num for to much time");
        return LM_STATUS_FAILURE;
    }

    return LM_STATUS_SUCCESS;
}
/*******************************************************************************
 * Description:
 *
 * Return:
******************************************************************************/
STATIC void 
lm_dcbx_set_params(struct _lm_device_t * pdev)
{
    u32_t       dcbx_neg_res_offset     = SHMEM_DCBX_NEG_RES_NONE;
    const u32_t mcp_dcbx_neg_res_offset = OFFSETOF(shmem2_region_t,dcbx_neg_res_offset);
    lldp_local_mib_t local_mib          = {0};
    lm_status_t      lm_status          = LM_STATUS_SUCCESS;                        

    mm_mem_zero(&local_mib,      sizeof(local_mib));
    if(!IS_PMF(pdev))
    {
        DbgBreakMsg("lm_dcbx_update_lpme_set_params error");
        return;
    }

    lm_dcbx_read_shmem2_mcp_fields(pdev,
                                   mcp_dcbx_neg_res_offset,
                                   &dcbx_neg_res_offset);

    if (SHMEM_DCBX_NEG_RES_NONE == dcbx_neg_res_offset)
    {
        DbgBreakMsg("mcp doesn't support dcbx_neg_res_offset");
        return;
    }
        
    lm_status = lm_dcbx_read_remote_local_mib(pdev,
                                              (u32_t *)&local_mib,
                                              dcbx_neg_res_offset,
                                              DCBX_READ_LOCAL_MIB);

    if(lm_status != LM_STATUS_SUCCESS)
    {
        return;
    }
 
    if(FALSE == pdev->dcbx_info.is_dcbx_neg_received)
    {
        pdev->dcbx_info.is_dcbx_neg_received = TRUE;
        // Setting the completion bit to TRUE can be 
        // done only once but will done on each PMF 
        // migration because is_dcbx_neg_received is 
        // per function.
        lm_dcbx_set_comp_recv_on_port_bit(pdev, TRUE);
    }

    lm_print_dcbx_drv_param(pdev,
                            &local_mib);

    lm_get_dcbx_drv_param(pdev,
                          &(pdev->params.dcbx_port_params),
                          &local_mib);

    MM_ACQUIRE_PHY_LOCK(pdev);
    lm_cmng_update(pdev);
    MM_RELEASE_PHY_LOCK(pdev);

    lm_dcbx_stop_hw_tx(pdev);

    lm_pfc_handle_pfc(pdev);

    lm_dcbx_update_ets_params(pdev);

    lm_dcbx_resume_hw_tx(pdev);
}
/**********************start DCBX INIT FUNCTIONS**************************************/

/*******************************************************************************
 * Description: 
 *
 * Return:
******************************************************************************/
STATIC lm_status_t 
lm_dcbx_init_check_params_valid(INOUT       lm_device_t     *pdev,
                                OUT         u32_t           *buff_check,
                                IN const    u32_t           buff_size)
{
    u32_t i=0;
    lm_status_t ret_val = LM_STATUS_SUCCESS;

    for (i=0 ; i < buff_size ; i++,buff_check++) 
    {
        if( DCBX_CONFIG_INV_VALUE == *buff_check)
        {
            ret_val = LM_STATUS_INVALID_PARAMETER;
        }
    }
    return ret_val;
}
/*******************************************************************************
 * Description: Read lldp parameters.
 * Return:
******************************************************************************/
lm_status_t 
lm_dcbx_lldp_read_params(struct _lm_device_t            * pdev,
                         b10_lldp_params_get_t          * lldp_params)
{
    lldp_params_t       mcp_lldp_params                 = {0};
    lldp_dcbx_stat_t    mcp_dcbx_stat                   = {{0}};
    u32_t               i                               = 0;
    u32_t               *buff                           = NULL ;
    u32_t               offset                          = 0;
    lm_status_t         lm_status                       = LM_STATUS_SUCCESS;
    const u32_t         mcp_dcbx_lldp_params_offset     = OFFSETOF(shmem2_region_t,dcbx_lldp_params_offset);
    const u32_t         mcp_dcbx_lldp_dcbx_stat_offset  = OFFSETOF(shmem2_region_t,dcbx_lldp_dcbx_stat_offset);

    mm_mem_zero(lldp_params, sizeof(b10_lldp_params_get_t));


    offset     = SHMEM_LLDP_DCBX_PARAMS_NONE;

    lm_dcbx_read_shmem2_mcp_fields(pdev,
                                   mcp_dcbx_lldp_params_offset,
                                   &offset);

    if((!IS_DCB_ENABLED(pdev)) ||
       (SHMEM_LLDP_DCBX_PARAMS_NONE == offset))
    {//DCBX isn't supported on E1
        return LM_STATUS_FAILURE;
    }

    lldp_params->config_lldp_params.overwrite_settings = 
        pdev->params.lldp_config_params.overwrite_settings;

    if (SHMEM_LLDP_DCBX_PARAMS_NONE != offset)
    {
        offset += PORT_ID(pdev) * sizeof(lldp_params_t);

        //Read the data first
        buff = (u32_t *)&mcp_lldp_params;
        for(i=0 ;i<sizeof(lldp_params_t); i+=4,buff++) 
        {
            *buff = REG_RD(pdev,
                          (offset + i));
        }
        lldp_params->ver_num                                     = LLDP_PARAMS_VER_NUM;
        lldp_params->config_lldp_params.msg_tx_hold              = mcp_lldp_params.msg_tx_hold;    
        lldp_params->config_lldp_params.msg_fast_tx              = mcp_lldp_params.msg_fast_tx_interval;
        lldp_params->config_lldp_params.tx_credit_max            = mcp_lldp_params.tx_crd_max;
        lldp_params->config_lldp_params.msg_tx_interval          = mcp_lldp_params.msg_tx_interval;
        lldp_params->config_lldp_params.tx_fast                  = mcp_lldp_params.tx_fast;


        // Preparation for new shmem
        ASSERT_STATIC(ARRSIZE(lldp_params->remote_chassis_id) >= ARRSIZE(mcp_lldp_params.peer_chassis_id));
        ASSERT_STATIC(sizeof(lldp_params->remote_chassis_id[0]) == sizeof(mcp_lldp_params.peer_chassis_id[0]));
        for(i=0 ; i< ARRSIZE(mcp_lldp_params.peer_chassis_id) ; i++)
        {
            lldp_params->remote_chassis_id[i]    = mcp_lldp_params.peer_chassis_id[i];
        }

        ASSERT_STATIC(sizeof(lldp_params->remote_port_id[0]) == sizeof(mcp_lldp_params.peer_port_id[0]));
        ASSERT_STATIC(ARRSIZE(lldp_params->remote_port_id) > ARRSIZE(mcp_lldp_params.peer_port_id));
        for(i=0 ; i<ARRSIZE(mcp_lldp_params.peer_port_id) ; i++)
        {
            lldp_params->remote_port_id[i]    = mcp_lldp_params.peer_port_id[i];
        }

        lldp_params->admin_status                                = mcp_lldp_params.admin_status;
    }
    else
    {// DCBX not supported in MCP
        DbgBreakMsg("DCBX DCBX params supported");
        lm_status= LM_STATUS_FAILURE;
    }

    offset     = SHMEM_LLDP_DCBX_STAT_NONE;

    lm_dcbx_read_shmem2_mcp_fields(pdev,
                                   mcp_dcbx_lldp_dcbx_stat_offset,
                                   &offset);

    if (SHMEM_LLDP_DCBX_STAT_NONE != offset)
    {
        offset += PORT_ID(pdev) * sizeof(mcp_dcbx_stat);

        //Read the data first
        buff = (u32_t *)&mcp_dcbx_stat;
        for(i=0 ;i<sizeof(mcp_dcbx_stat); i+=4,buff++) 
        {
            *buff = REG_RD(pdev,
                          (offset + i));
        }
        // Preparation for new shmem
        
        ASSERT_STATIC(ARRSIZE(lldp_params->local_chassis_id) >= ARRSIZE(mcp_dcbx_stat.local_chassis_id));
        ASSERT_STATIC(sizeof(lldp_params->local_chassis_id[0]) >= sizeof(mcp_dcbx_stat.local_chassis_id[0]));
        for(i=0 ; i< ARRSIZE(mcp_dcbx_stat.local_chassis_id) ; i++)
        {
            lldp_params->local_chassis_id[i]    = mcp_dcbx_stat.local_chassis_id[i];
        }

        ASSERT_STATIC(ARRSIZE(lldp_params->local_port_id) >= ARRSIZE(mcp_dcbx_stat.local_port_id));
        ASSERT_STATIC(sizeof(lldp_params->local_port_id[0]) >= sizeof(mcp_dcbx_stat.local_port_id[0]));
        for(i=0 ; i< ARRSIZE(mcp_dcbx_stat.local_port_id) ; i++)
        {
            lldp_params->local_port_id[i]    = mcp_dcbx_stat.local_port_id[i];
        }
    }
    else
    {// DCBX not supported in MCP
        DbgBreakMsg("DCBX DCBX stats supported");
        lm_status= LM_STATUS_FAILURE;
    }

    return lm_status;
}
/*******************************************************************************
 * Description: 
 *              mcp_pg_bw_tbl_size: In elements.
 *              set_configuration_bw_size: In elements.
 * Return:
******************************************************************************/
STATIC void 
lm_dcbx_get_bw_percentage_tbl(struct _lm_device_t   * pdev,
                              OUT u32_t             * set_configuration_bw,
                              IN u32_t              * mcp_pg_bw_tbl,
                              IN const u8_t         set_configuration_bw_size,
                              IN const u8_t         mcp_pg_bw_tbl_size)
{

    u8_t        i       = 0;
    const u8_t  mcp_pg_bw_tbl_size_in_bytes = (sizeof(*mcp_pg_bw_tbl)*(mcp_pg_bw_tbl_size));

    DbgBreakIf(set_configuration_bw_size != mcp_pg_bw_tbl_size);

    DbgBreakIf(0 != (mcp_pg_bw_tbl_size_in_bytes % sizeof(u32_t)));
    for(i=0 ;i<set_configuration_bw_size ;i++) 
    {
        set_configuration_bw[i] = DCBX_PG_BW_GET(mcp_pg_bw_tbl,i);
    }
}
/*******************************************************************************
 * Description: Parse ets_pri_pg data and spread it from nibble to 32 bits.
 *
 * Return:
******************************************************************************/
STATIC void 
lm_dcbx_get_ets_pri_pg_tbl(struct _lm_device_t      * pdev,
                           OUT      u32_t           * set_configuration_ets_pg,
                           IN const u32_t           * mcp_pri_pg_tbl,
                           IN const u8_t            set_priority_app_size,
                           IN const u8_t            mcp_pri_pg_tbl_size)
{
    u8_t        i       = 0;
    const u8_t  mcp_pri_pg_tbl_size_in_bytes = (sizeof(*mcp_pri_pg_tbl)*(mcp_pri_pg_tbl_size));

    DbgBreakIf(set_priority_app_size != (mcp_pri_pg_tbl_size));
    
    // Arrays that there cell are less than 32 bit are still 
    // in big endian mode.
    DbgBreakIf(0 != (mcp_pri_pg_tbl_size_in_bytes % sizeof(u32_t)));

    // Nibble handling
    for(i=0 ; i < set_priority_app_size ; i++) 
    {
            set_configuration_ets_pg[i] = DCBX_PRI_PG_GET(mcp_pri_pg_tbl,i);
    }
}
/*******************************************************************************
 * Description: Parse priority app data.
 *
 * Return:
******************************************************************************/
STATIC void 
lm_dcbx_get_priority_app_table(struct _lm_device_t                      * pdev,
                              OUT struct _admin_priority_app_table_t    * set_priority_app,
                              IN struct dcbx_app_priority_entry         * mcp_array,
                              IN const u8_t                              set_priority_app_size,
                              IN const u8_t                              mcp_array_size)
{
    u8_t    i           = 0;

    if(set_priority_app_size > mcp_array_size)
    {
        DbgBreakIf(1);
        return;
    }

    for(i=0 ;i<set_priority_app_size ;i++)
    {
        if(GET_FLAGS(mcp_array[i].appBitfield,DCBX_APP_ENTRY_VALID)) 
        {
            set_priority_app[i].valid = TRUE;
        }
        
        if(GET_FLAGS(mcp_array[i].appBitfield,DCBX_APP_SF_ETH_TYPE)) 
        {
            set_priority_app[i].traffic_type = TRAFFIC_TYPE_ETH;
        }
        else
        {
            set_priority_app[i].traffic_type = TRAFFIC_TYPE_PORT;
        }
        set_priority_app[i].priority = mcp_array[i].pri_bitmap;


        // This has no logic this is only done for supporting old bootcodes.
        // The boot code still expexts u8 [2] instead of u16
        set_priority_app[i].app_id = mcp_array[i].app_id;                 
    }
    
}
/**
 * @description
 * Fill the operational parameters. 
 * @param pdev 
 * @param dcbx_params 
 * 
 * @return STATIC void 
 */
STATIC void
lm_dcbx_read_params_fill_oper_state(struct _lm_device_t            * pdev,
                                    b10_dcbx_params_get_t          * dcbx_params)
{
    const u8_t is_completion_received_on_port = 
        lm_dcbx_is_comp_recv_on_port(pdev); 

    if(TRUE == is_completion_received_on_port)
    {
        SET_FLAGS(dcbx_params->dcb_current_oper_state_bitmap,DCBX_CURRENT_STATE_IS_SYNC);
    }
    if(TRUE == pdev->params.dcbx_port_params.app.enabled)
    {
        SET_FLAGS(dcbx_params->dcb_current_oper_state_bitmap,PRIORITY_TAGGING_IS_CURRENTLY_OPERATIONAL);
    }

    if(TRUE == pdev->params.dcbx_port_params.pfc.enabled)
    {
        SET_FLAGS(dcbx_params->dcb_current_oper_state_bitmap,PFC_IS_CURRENTLY_OPERATIONAL);
    }

    if(TRUE == pdev->params.dcbx_port_params.ets.enabled)
    {
        SET_FLAGS(dcbx_params->dcb_current_oper_state_bitmap,ETS_IS_CURRENTLY_OPERATIONAL);
    }
}
/*******************************************************************************
 * Description: Read DCBX parameters from admin/local and remote MIBs. 
 *
 * Return:
 *              LM_STATUS_FAILURE - All/Some of the parameters could not be read. 
 *              LM_STATUS_SUCCESS - All the MIBs where read successfully.
******************************************************************************/
lm_status_t 
lm_dcbx_read_params(struct _lm_device_t            * pdev,
                    b10_dcbx_params_get_t          * dcbx_params)
{
    lldp_admin_mib_t    admin_mib                       = {0};
    lldp_local_mib_t    local_mib                       = {0};
    lldp_remote_mib_t   remote_mib                      = {0};
    lldp_dcbx_stat_t    mcp_dcbx_stat                   = {{0}};
    u32_t               pfc_frames_sent[2]              = {0};
    u32_t               pfc_frames_received[2]          = {0};
    u32_t               i                               = 0;
    u32_t               *buff                           = NULL;
    u32_t               offset                          = SHMEM_LLDP_DCBX_PARAMS_NONE;
    lm_status_t         lm_status                       = LM_STATUS_SUCCESS;
    const u32_t         mcp_dcbx_lldp_params_offset     = OFFSETOF(shmem2_region_t,dcbx_lldp_params_offset);
    const u32_t         mcp_dcbx_neg_res_offset         = OFFSETOF(shmem2_region_t,dcbx_neg_res_offset);
    const u32_t         mcp_dcbx_remote_mib_offset      = OFFSETOF(shmem2_region_t,dcbx_remote_mib_offset);
    const u32_t         mcp_dcbx_lldp_dcbx_stat_offset  = OFFSETOF(shmem2_region_t,dcbx_lldp_dcbx_stat_offset);

    mm_mem_zero(dcbx_params, sizeof(b10_dcbx_params_get_t));

    lm_dcbx_read_params_fill_oper_state(pdev,dcbx_params);

    lm_dcbx_read_shmem2_mcp_fields(pdev,
                                   mcp_dcbx_lldp_params_offset,
                                   &offset);

    if((!IS_DCB_ENABLED(pdev)) ||
       (SHMEM_LLDP_DCBX_PARAMS_NONE == offset))
    {//DCBX isn't supported on E1
        return LM_STATUS_FAILURE;
    }

    dcbx_params->config_dcbx_params.overwrite_settings = 
        pdev->params.dcbx_config_params.overwrite_settings;

    // E3.0 might be 4...not supported in current shmem
    ASSERT_STATIC( 2 == PORT_MAX );

    if (SHMEM_LLDP_DCBX_PARAMS_NONE != offset)
    {
        offset += PORT_MAX * sizeof(lldp_params_t) +               
                PORT_ID(pdev) * sizeof(lldp_admin_mib_t);

        //Read the data first
        buff = (u32_t *)&admin_mib;
        for(i=0 ;i<sizeof(lldp_admin_mib_t); i+=4,buff++) 
        {
            *buff = REG_RD(pdev,
                          (offset + i));
        }
        
        dcbx_params->config_dcbx_params.dcb_enable          = IS_DCB_ENABLED(pdev) ;

        if(GET_FLAGS(admin_mib.ver_cfg_flags,DCBX_DCBX_ENABLED))
        {
            dcbx_params->config_dcbx_params.admin_dcbx_enable   = 1 ;
        }

        if(GET_FLAGS(admin_mib.ver_cfg_flags,DCBX_VERSION_CEE))
        {
            dcbx_params->config_dcbx_params.admin_dcbx_version  = ADMIN_DCBX_VERSION_CEE;
        }
        else if(GET_FLAGS(admin_mib.ver_cfg_flags,DCBX_VERSION_IEEE))
        {
            dcbx_params->config_dcbx_params.admin_dcbx_version  = ADMIN_DCBX_VERSION_IEEE;
        }
        else
        {
            dcbx_params->config_dcbx_params.admin_dcbx_version  = OVERWRITE_SETTINGS_INVALID;
            DbgMessage(pdev,WARN," unknown DCBX version ");
        }

        dcbx_params->config_dcbx_params.admin_ets_enable    = admin_mib.features.ets.enabled;

        dcbx_params->config_dcbx_params.admin_pfc_enable    = admin_mib.features.pfc.enabled;
        
        //FOR IEEE pdev->params.dcbx_config_params.admin_tc_supported_tx_enable
        if(GET_FLAGS(admin_mib.ver_cfg_flags,DCBX_ETS_CONFIG_TX_ENABLED))
        {
            dcbx_params->config_dcbx_params.admin_ets_configuration_tx_enable = TRUE;
        }
        //For IEEE admin_ets_recommendation_tx_enable
        
        if(GET_FLAGS(admin_mib.ver_cfg_flags,DCBX_PFC_CONFIG_TX_ENABLED))
        {
            dcbx_params->config_dcbx_params.admin_pfc_tx_enable = TRUE;
        }

        if(GET_FLAGS(admin_mib.ver_cfg_flags,DCBX_APP_CONFIG_TX_ENABLED))
        {
            dcbx_params->config_dcbx_params.admin_application_priority_tx_enable = TRUE;
        }


        if(GET_FLAGS(admin_mib.ver_cfg_flags,DCBX_ETS_WILLING))
        {
            dcbx_params->config_dcbx_params.admin_ets_willing = TRUE;
        }

        //For IEEE admin_ets_reco_valid
        
        if(GET_FLAGS(admin_mib.ver_cfg_flags,DCBX_PFC_WILLING))
        {
            dcbx_params->config_dcbx_params.admin_pfc_willing = TRUE;
        }


        if(GET_FLAGS(admin_mib.ver_cfg_flags,DCBX_APP_WILLING))
        {
            dcbx_params->config_dcbx_params.admin_app_priority_willing = TRUE;
        }

        
        lm_dcbx_get_bw_percentage_tbl(pdev,
                              dcbx_params->config_dcbx_params.admin_configuration_bw_percentage,
                              admin_mib.features.ets.pg_bw_tbl,
                              ARRSIZE(dcbx_params->config_dcbx_params.admin_configuration_bw_percentage),
                              DCBX_MAX_NUM_PG_BW_ENTRIES);

        lm_dcbx_get_ets_pri_pg_tbl(pdev,
                                   dcbx_params->config_dcbx_params.admin_configuration_ets_pg,
                                   admin_mib.features.ets.pri_pg_tbl,
                                   ARRSIZE(dcbx_params->config_dcbx_params.admin_configuration_ets_pg),
                                   DCBX_MAX_NUM_PRI_PG_ENTRIES);


        //For IEEE admin_recommendation_bw_percentage
        //For IEEE admin_recommendation_ets_pg
        dcbx_params->config_dcbx_params.admin_pfc_bitmap = admin_mib.features.pfc.pri_en_bitmap;

        lm_dcbx_get_priority_app_table(pdev,
                                  dcbx_params->config_dcbx_params.admin_priority_app_table,
                                  admin_mib.features.app.app_pri_tbl,
                                  ARRSIZE(dcbx_params->config_dcbx_params.admin_priority_app_table),
                                  ARRSIZE(admin_mib.features.app.app_pri_tbl));

        dcbx_params->config_dcbx_params.admin_default_priority = admin_mib.features.app.default_pri;
    }
    else
    {// DCBX not supported in MCP
        DbgBreakMsg("DCBX DCBX params not supported");
        lm_status= LM_STATUS_FAILURE;
    }
    // Get negotiation results MIB data
    offset  = SHMEM_DCBX_NEG_RES_NONE;
    
    lm_dcbx_read_shmem2_mcp_fields(pdev,
                                   mcp_dcbx_neg_res_offset,
                                   &offset);

    if (SHMEM_DCBX_NEG_RES_NONE != offset)
    {
        lm_status = lm_dcbx_read_remote_local_mib(pdev,
                                           (u32_t *)&local_mib,
                                           offset,
                                           DCBX_READ_LOCAL_MIB);

        DbgBreakIf(lm_status != LM_STATUS_SUCCESS);

        dcbx_params->ver_num            = DCBX_PARAMS_VER_NUM;
        dcbx_params->local_tc_supported = local_mib.features.app.tc_supported;
        dcbx_params->local_pfc_caps     = local_mib.features.pfc.pfc_caps;
        dcbx_params->local_ets_enable   = local_mib.features.ets.enabled;
        dcbx_params->local_pfc_enable   = local_mib.features.pfc.enabled;

        lm_dcbx_get_bw_percentage_tbl(pdev,
                              dcbx_params->local_configuration_bw_percentage,
                              local_mib.features.ets.pg_bw_tbl,
                              ARRSIZE(dcbx_params->local_configuration_bw_percentage),
                              DCBX_MAX_NUM_PG_BW_ENTRIES);
        
        lm_dcbx_get_ets_pri_pg_tbl(pdev,
                                   dcbx_params->local_configuration_ets_pg,
                                   local_mib.features.ets.pri_pg_tbl,
                                   ARRSIZE(dcbx_params->local_configuration_ets_pg),
                                   DCBX_MAX_NUM_PRI_PG_ENTRIES);

        dcbx_params->local_pfc_bitmap = local_mib.features.pfc.pri_en_bitmap;
        
        lm_dcbx_get_priority_app_table(pdev,
                                  dcbx_params->local_priority_app_table,
                                  local_mib.features.app.app_pri_tbl,
                                  ARRSIZE(dcbx_params->local_priority_app_table),
                                  ARRSIZE(local_mib.features.app.app_pri_tbl));

        if(GET_FLAGS(local_mib.error,DCBX_LOCAL_PFC_MISMATCH))
        {
            dcbx_params->pfc_mismatch = TRUE;
        }

        if(GET_FLAGS(local_mib.error,DCBX_LOCAL_APP_MISMATCH))
        {
            dcbx_params->priority_app_mismatch = TRUE;
        }
    }
    else
    {// DCBX not supported in MCP
        DbgBreakMsg("DCBX Negotiation result not supported");
        lm_status= LM_STATUS_FAILURE;
    }
    // Get remote MIB data
    offset  = SHMEM_DCBX_REMOTE_MIB_NONE;
    
    lm_dcbx_read_shmem2_mcp_fields(pdev,
                                   mcp_dcbx_remote_mib_offset,
                                   &offset);

    if (SHMEM_DCBX_REMOTE_MIB_NONE != offset)
    {
        lm_status = lm_dcbx_read_remote_local_mib(pdev,
                                                  (u32_t *)&remote_mib,
                                                  offset,
                                                  DCBX_READ_REMOTE_MIB);

        DbgBreakIf(lm_status != LM_STATUS_SUCCESS);

        dcbx_params->remote_tc_supported = remote_mib.features.app.tc_supported;
        dcbx_params->remote_pfc_cap = remote_mib.features.pfc.pfc_caps;
        if(GET_FLAGS(remote_mib.flags,DCBX_REMOTE_ETS_RECO_VALID))
        {
            dcbx_params->remote_ets_reco_valid = TRUE;
        }

        if(GET_FLAGS(remote_mib.flags,DCBX_ETS_REM_WILLING))
        {
            dcbx_params->remote_ets_willing = TRUE;
        }
        
        if(GET_FLAGS(remote_mib.flags,DCBX_PFC_REM_WILLING))
        {
            dcbx_params->remote_pfc_willing = TRUE;
        }

        if(GET_FLAGS(remote_mib.flags,DCBX_APP_REM_WILLING))
        {
            dcbx_params->remote_app_priority_willing = TRUE;
        }

        lm_dcbx_get_bw_percentage_tbl(pdev,
                              dcbx_params->remote_configuration_bw_percentage,
                              remote_mib.features.ets.pg_bw_tbl,
                              ARRSIZE(dcbx_params->remote_configuration_bw_percentage),
                              DCBX_MAX_NUM_PG_BW_ENTRIES);
        
        lm_dcbx_get_ets_pri_pg_tbl(pdev,
                                   dcbx_params->remote_configuration_ets_pg,
                                   remote_mib.features.ets.pri_pg_tbl,
                                   ARRSIZE(dcbx_params->remote_configuration_ets_pg),
                                   DCBX_MAX_NUM_PRI_PG_ENTRIES);
        // For IEEE remote_recommendation_bw_percentage
        // For IEEE remote_recommendation_ets_pg
        
        dcbx_params->remote_pfc_bitmap = remote_mib.features.pfc.pri_en_bitmap;

        lm_dcbx_get_priority_app_table(pdev,
                                  dcbx_params->remote_priority_app_table,
                                  remote_mib.features.app.app_pri_tbl,
                                  ARRSIZE(dcbx_params->remote_priority_app_table),
                                  ARRSIZE(remote_mib.features.app.app_pri_tbl));
    }
    else
    {// DCBX not supported in MCP
        DbgBreakMsg("DCBX remote MIB not supported");
        lm_status= LM_STATUS_FAILURE;
    }

    // Get negotiation results MIB data
    offset  = SHMEM_LLDP_DCBX_STAT_NONE;

    lm_dcbx_read_shmem2_mcp_fields(pdev,
                                   mcp_dcbx_lldp_dcbx_stat_offset,
                                   &offset);

    // E3.0 might be 4...not supported in current shmem
    ASSERT_STATIC( 2 == PORT_MAX );

    if (SHMEM_LLDP_DCBX_STAT_NONE != offset)
    {
        offset += PORT_ID(pdev) * sizeof(mcp_dcbx_stat);

        //Read the data first
        buff = (u32_t *)&mcp_dcbx_stat;
        for(i=0 ;i<sizeof(mcp_dcbx_stat); i+=4,buff++) 
        {
            *buff = REG_RD(pdev,
                          (offset + i));
        }
        
        dcbx_params->dcbx_frames_sent       = mcp_dcbx_stat.num_tx_dcbx_pkts;
        dcbx_params->dcbx_frames_received   = mcp_dcbx_stat.num_rx_dcbx_pkts;
    }
    else
    {// DCBX not supported in MCP
        DbgBreakMsg("DCBX statistic not supported");
        lm_status= LM_STATUS_FAILURE;
    }
    // TODO - Move to lm_stat

    MM_ACQUIRE_PHY_LOCK(pdev);
    elink_pfc_statistic(&pdev->params.link, &pdev->vars.link, 
                        pfc_frames_sent, pfc_frames_received);
    MM_RELEASE_PHY_LOCK(pdev);

    dcbx_params->pfc_frames_sent = ((u64_t)(pfc_frames_sent[1]) << 32) + pfc_frames_sent[0];

    dcbx_params->pfc_frames_received = ((u64_t)(pfc_frames_received[1]) << 32) + pfc_frames_received[0];

    return lm_status;
}
/*******************************************************************************
 * Description:
 *
 * Return:
******************************************************************************/
void 
lm_dcbx_init_lpme_set_params(struct _lm_device_t *pdev)
{

    if( TRUE == pdev->dcbx_info.is_dcbx_neg_received)
    {
        // DCBX negotiation ended normaly. 
        return;
    }
    //DbgBreakMsg(" lm_dcbx_init_lpme_set_params : DCBX timer configuration \n");
    //DbgMessage(pdev,FATAL,"lm_dcbx_init_lpme_set_params : DCBX timer configuration \n");
    // DCBX negotiation didn’t ended normaly yet. 
    // No lock is needed to be taken because lm_dcbx_set_params is only called from a WI
    lm_dcbx_set_params(pdev);
}
/*******************************************************************************
 * Description: Update admin MIB that changes deafault DCBX configuration
 *              "admin_dcbx_enable" and "dcb_enable" are stand alone registry keys
 *              (if present will always be valid and not ignored), for all other 
 *              DCBX registry set only if the entire DCBX registry set is present 
 *              and differ from 0xFFFFFFFF (invalid value) the DCBX registry 
 *              parameters are taken, otherwise the registry key set is ignored.)
 *              (Expect "admin_dcbx_enable" and "dcb_enable")
 * Return:
******************************************************************************/
STATIC void 
lm_dcbx_admin_mib_updated_params(struct _lm_device_t * pdev,
                                 u32_t                 mf_cfg_offset_value)
{
    lldp_admin_mib_t admin_mib          = {0};
    u32_t           i                   = 0;
    u32_t           other_traf_type     = PREDEFINED_APP_IDX_MAX,   traf_type=0;
    u32_t           *buff               = NULL ;
    lm_status_t     lm_status           = LM_STATUS_SUCCESS;
    u32_t           offest              = mf_cfg_offset_value +
        PORT_MAX * sizeof(lldp_params_t) +               
        PORT_ID(pdev) * sizeof(lldp_admin_mib_t);      

    buff = (u32_t *)&admin_mib;
    //Read the data first
    for(i=0 ;i<sizeof(lldp_admin_mib_t); i+=4,buff++) 
    {
        *buff = REG_RD(pdev,
                      (offest+ i));
    }


    if(DCBX_CONFIG_INV_VALUE != 
       pdev->params.dcbx_config_params.admin_dcbx_enable) 
    {
        if(pdev->params.dcbx_config_params.admin_dcbx_enable) 
        {
            SET_FLAGS(admin_mib.ver_cfg_flags,DCBX_DCBX_ENABLED);
        }
        else
        {
            RESET_FLAGS(admin_mib.ver_cfg_flags,DCBX_DCBX_ENABLED);
        }
    }
    lm_status = lm_dcbx_init_check_params_valid(pdev,
                                    (u32_t *)(&(pdev->params.dcbx_config_params.overwrite_settings)),
                                    ((sizeof(pdev->params.dcbx_config_params)-
                                     OFFSETOF(config_dcbx_params_t , overwrite_settings))/sizeof(u32_t)));

    if((LM_STATUS_SUCCESS == lm_status)&&
       (OVERWRITE_SETTINGS_ENABLE == pdev->params.dcbx_config_params.overwrite_settings))
    {
        RESET_FLAGS(admin_mib.ver_cfg_flags,DCBX_CEE_VERSION_MASK);
        admin_mib.ver_cfg_flags |= 
            (pdev->params.dcbx_config_params.admin_dcbx_version << DCBX_CEE_VERSION_SHIFT) & DCBX_CEE_VERSION_MASK;

        admin_mib.features.ets.enabled = (u8_t) 
            pdev->params.dcbx_config_params.admin_ets_enable;


        admin_mib.features.pfc.enabled =(u8_t) 
            pdev->params.dcbx_config_params.admin_pfc_enable;


        //FOR IEEE pdev->params.dcbx_config_params.admin_tc_supported_tx_enable
        if(pdev->params.dcbx_config_params.admin_ets_configuration_tx_enable) 
        {
            SET_FLAGS(admin_mib.ver_cfg_flags,DCBX_ETS_CONFIG_TX_ENABLED);
        }
        else
        {
            RESET_FLAGS(admin_mib.ver_cfg_flags,DCBX_ETS_CONFIG_TX_ENABLED);
        }
        //For IEEE admin_ets_recommendation_tx_enable
        
        if(pdev->params.dcbx_config_params.admin_pfc_tx_enable) 
        {
            SET_FLAGS(admin_mib.ver_cfg_flags,DCBX_PFC_CONFIG_TX_ENABLED);
        }
        else
        {
            RESET_FLAGS(admin_mib.ver_cfg_flags,DCBX_PFC_CONFIG_TX_ENABLED);
        }

        if(pdev->params.dcbx_config_params.admin_application_priority_tx_enable) 
        {
            SET_FLAGS(admin_mib.ver_cfg_flags,DCBX_APP_CONFIG_TX_ENABLED);
        }
        else
        {
            RESET_FLAGS(admin_mib.ver_cfg_flags,DCBX_APP_CONFIG_TX_ENABLED);
        }


        if(pdev->params.dcbx_config_params.admin_ets_willing) 
        {
            SET_FLAGS(admin_mib.ver_cfg_flags,DCBX_ETS_WILLING);
        }
        else
        {
            RESET_FLAGS(admin_mib.ver_cfg_flags,DCBX_ETS_WILLING);
        }
        //For IEEE admin_ets_reco_valid
        if(pdev->params.dcbx_config_params.admin_pfc_willing) 
        {
            SET_FLAGS(admin_mib.ver_cfg_flags,DCBX_PFC_WILLING);
        }
        else
        {
            RESET_FLAGS(admin_mib.ver_cfg_flags,DCBX_PFC_WILLING);
        }

        if(pdev->params.dcbx_config_params.admin_app_priority_willing) 
        {
            SET_FLAGS(admin_mib.ver_cfg_flags,DCBX_APP_WILLING);
        }
        else
        {
            RESET_FLAGS(admin_mib.ver_cfg_flags,DCBX_APP_WILLING);
        }

        ASSERT_STATIC(ARRSIZE(pdev->params.dcbx_config_params.admin_configuration_bw_percentage) == 
                      DCBX_MAX_NUM_PG_BW_ENTRIES);
        for(i=0; i<DCBX_MAX_NUM_PG_BW_ENTRIES ;i++) 
        {
            DCBX_PG_BW_SET(admin_mib.features.ets.pg_bw_tbl,
                           i,
                           pdev->params.dcbx_config_params.admin_configuration_bw_percentage[i]);
        }
        
        ASSERT_STATIC(ARRSIZE(pdev->params.dcbx_config_params.admin_configuration_ets_pg) == 
                      DCBX_MAX_NUM_PRI_PG_ENTRIES);

        for(i=0;i<DCBX_MAX_NUM_PRI_PG_ENTRIES;i++) 
        {
            DCBX_PRI_PG_SET(admin_mib.features.ets.pri_pg_tbl,
                            i,
                            pdev->params.dcbx_config_params.admin_configuration_ets_pg[i]);
        }
        //For IEEE admin_recommendation_bw_percentage
        //For IEEE admin_recommendation_ets_pg
        admin_mib.features.pfc.pri_en_bitmap = (u8_t)pdev->params.dcbx_config_params.admin_pfc_bitmap;

        for(i=0;i<ARRSIZE(pdev->params.dcbx_config_params.admin_priority_app_table);i++)
        {
            if(pdev->params.dcbx_config_params.admin_priority_app_table[i].valid)
            {
                if((ETH_TYPE_FCOE == pdev->params.dcbx_config_params.admin_priority_app_table[i].app_id)&&
                   (TRAFFIC_TYPE_ETH == pdev->params.dcbx_config_params.admin_priority_app_table[i].traffic_type))
                {
                    traf_type = FCOE_APP_IDX;
                }
                else if((TCP_PORT_ISCSI == pdev->params.dcbx_config_params.admin_priority_app_table[i].app_id)&&
                   (TRAFFIC_TYPE_PORT == pdev->params.dcbx_config_params.admin_priority_app_table[i].traffic_type))
                {
                    traf_type = ISCSI_APP_IDX;
                }
                else
                {
                    traf_type = other_traf_type++;
                }

                ASSERT_STATIC( ARRSIZE(admin_mib.features.app.app_pri_tbl) > PREDEFINED_APP_IDX_MAX ) ;
                // This has no logic this is only done for supporting old bootcodes.
                //The boot code still expexts u8 [2] instead of u16
                admin_mib.features.app.app_pri_tbl[traf_type].app_id = 
                    (u16_t)pdev->params.dcbx_config_params.admin_priority_app_table[i].app_id;

                admin_mib.features.app.app_pri_tbl[traf_type].pri_bitmap =(u8_t)
                    (1 << pdev->params.dcbx_config_params.admin_priority_app_table[i].priority);

                admin_mib.features.app.app_pri_tbl[traf_type].appBitfield = 
                    (DCBX_APP_ENTRY_VALID);

                if(TRAFFIC_TYPE_ETH == pdev->params.dcbx_config_params.admin_priority_app_table[i].traffic_type)
                {
                    admin_mib.features.app.app_pri_tbl[traf_type].appBitfield |= DCBX_APP_SF_ETH_TYPE;
                }
                else
                {
                    admin_mib.features.app.app_pri_tbl[traf_type].appBitfield |= DCBX_APP_SF_PORT;
                }
            }
        }

        admin_mib.features.app.default_pri = (u8_t)pdev->params.dcbx_config_params.admin_default_priority;

    }
    else
    {
        if(OVERWRITE_SETTINGS_ENABLE == pdev->params.dcbx_config_params.overwrite_settings)
        {
            pdev->params.dcbx_config_params.overwrite_settings = OVERWRITE_SETTINGS_INVALID;
        }

    }

    //Write the data.
    buff = (u32_t *)&admin_mib;
    for(i=0 ;i<sizeof(lldp_admin_mib_t); i+=4,buff++) 
    {
        REG_WR(pdev, (offest+ i) , *buff);
    }
}
/*******************************************************************************
 * Description: Update LLDP that changes deafault LLDP configuration.
 *              Only if the entire LLDP registry set is present and differ from 
 *              0xFFFFFFFF (invalid value) the LLDP registry parameters are taken, 
 *              otherwise the registry keys are ignored.
 * Return:
 *              LM_STATUS_FAILURE - All/Some of the parameters could not be read. 
 *              LM_STATUS_SUCCESS - All the MIBs where read successfully.
******************************************************************************/
STATIC void 
lm_dcbx_init_lldp_updated_params(struct _lm_device_t * pdev,
                                 u32_t                 mf_cfg_offset_value)
{

    lldp_params_t   lldp_params = {0};
    u32_t           i           = 0;
    u32_t           *buff       = NULL ;
    lm_status_t     lm_status   = LM_STATUS_SUCCESS;
    u32_t           offest      = mf_cfg_offset_value + 
        PORT_ID(pdev) * sizeof(lldp_params_t); 

    lm_status = lm_dcbx_init_check_params_valid(pdev,
                                    (u32_t *)(&(pdev->params.lldp_config_params)),
                                    (sizeof(pdev->params.lldp_config_params)/sizeof(u32_t)));

    if((LM_STATUS_SUCCESS == lm_status)&&
       (OVERWRITE_SETTINGS_ENABLE == pdev->params.lldp_config_params.overwrite_settings))
    {
        
        //Read the data first
        buff = (u32_t *)&lldp_params;
        for(i=0 ;i<sizeof(lldp_params_t); i+=4,buff++) 
        {
            *buff = REG_RD(pdev,
                          (offest+ i));
        }
        lldp_params.msg_tx_hold             = (u8_t)pdev->params.lldp_config_params.msg_tx_hold;    
        lldp_params.msg_fast_tx_interval    = (u8_t)pdev->params.lldp_config_params.msg_fast_tx;
        lldp_params.tx_crd_max              = (u8_t)pdev->params.lldp_config_params.tx_credit_max;
        lldp_params.msg_tx_interval         = (u8_t)pdev->params.lldp_config_params.msg_tx_interval;
        lldp_params.tx_fast                 = (u8_t)pdev->params.lldp_config_params.tx_fast;

        //Write the data.
        buff = (u32_t *)&lldp_params;
        for(i=0 ;i<sizeof(lldp_params_t); i+=4,buff++) 
        {
            REG_WR(pdev, (offest+ i) , *buff);//Change to write
        }    
    }
    else
    {
        if(OVERWRITE_SETTINGS_ENABLE == pdev->params.lldp_config_params.overwrite_settings)
        {
            pdev->params.lldp_config_params.overwrite_settings = OVERWRITE_SETTINGS_INVALID;
        }

    }

}
/*******************************************************************************
 * Description:
 *              Allocate physical memory for DCBX start ramrod
 *
 * Return:
******************************************************************************/
lm_status_t
lm_dcbx_get_pfc_fw_cfg_phys_mem(
    IN struct _lm_device_t *pdev)
{
    if (CHK_NULL(pdev->dcbx_info.pfc_fw_cfg_virt))
    {
        pdev->dcbx_info.pfc_fw_cfg_virt = 
            mm_alloc_phys_mem(pdev,
                              sizeof(struct flow_control_configuration),
                              &pdev->dcbx_info.pfc_fw_cfg_phys,
                              0,
                              LM_CLI_IDX_MAX);
        
        if CHK_NULL(pdev->dcbx_info.pfc_fw_cfg_virt)
        {
            return LM_STATUS_RESOURCE;
        }
    }
   
    return LM_STATUS_SUCCESS;
}
/**
 * @description
 *  Called to clean dcbx info after D3
 * @param pdev 
 * 
 * @return lm_status_t 
 */
lm_status_t
lm_dcbx_init_info(
    IN lm_device_t *pdev
    )
{
    pdev->dcbx_info.is_enabled   = FALSE;

    return LM_STATUS_SUCCESS;
}
/*******************************************************************************
 * Description:
 *             
 *
 * Return:
******************************************************************************/
lm_status_t
lm_dcbx_free_resc(
    IN struct _lm_device_t *pdev
    )
{
    pdev->dcbx_info.pfc_fw_cfg_virt = NULL;
    pdev->dcbx_info.is_enabled      = FALSE;
    return LM_STATUS_SUCCESS;
}
/*******************************************************************************
 * Description:
 *              Allocate physical memory for DCBX start ramrod
 *
 * Return:
******************************************************************************/

/* Get dma memory for init ramrod */
lm_status_t
lm_dcbx_init_params(
    IN struct _lm_device_t *pdev)
{
    lm_status_t lm_status                       = LM_STATUS_SUCCESS;

    pdev->dcbx_info.dcbx_update_lpme_task_state = DCBX_UPDATE_TASK_STATE_FREE;
    pdev->dcbx_info.is_dcbx_neg_received        = FALSE;
    mm_mem_zero(&(pdev->params.dcbx_port_params), sizeof(pdev->params.dcbx_port_params));

    lm_status                                   = lm_dcbx_get_pfc_fw_cfg_phys_mem(pdev);
    
    if(LM_STATUS_SUCCESS != lm_status )
    {
        DbgBreakMsg("lm_dcbx_init_params : resource ");
        pdev->dcbx_info.dcbx_error |= DCBX_ERROR_RESOURCE;
        return lm_status;
    }

    return lm_status;
}
/**
 * @description 
 * Set in a shared port memory place if DCBX completion was 
 * received. Function is needed for PMF migration in order to 
 * synchronize the new PMF that DCBX results has ended. 
 * @param pdev 
 * @param is_completion_recv 
 */
void
lm_dcbx_set_comp_recv_on_port_bit(
    IN          lm_device_t *pdev,
    IN const    u8_t        is_completion_recv)
{
    const   u32_t drv_flags_offset = OFFSETOF(shmem2_region_t,drv_flags);
    u32_t   drv_flags = 0;
    lm_status_t lm_status = LM_STATUS_SUCCESS;

    if(!IS_PMF(pdev))
    {
        DbgBreakMsg("lm_dcbx_is_comp_recv_on_port error only PMF can access this field ");
        return;
    }
    
    lm_status = lm_hw_lock(pdev, HW_LOCK_DRV_FLAGS, TRUE);

    if(LM_STATUS_SUCCESS != lm_status)
    {
        DbgBreakMsg("lm_dcbx_set_comp_recv_on_port_bit lm_hw_lock failed ");
        return;
    }

    lm_dcbx_read_shmem2_mcp_fields(pdev,
                                   drv_flags_offset,
                                   &drv_flags);

    if(TRUE == is_completion_recv)
    {
        SET_FLAGS(drv_flags,DRV_FLAGS_DCB_CONFIGURED);
    }
    else
    {
        RESET_FLAGS(drv_flags,DRV_FLAGS_DCB_CONFIGURED);
    }

    lm_dcbx_write_shmem2_mcp_fields(pdev,
                                   drv_flags_offset,
                                   drv_flags);

    lm_hw_unlock(pdev, HW_LOCK_DRV_FLAGS);
}
/**
 * @description
 * Function is needed for PMF migration in order to synchronize 
 * the new PMF that DCBX results has ended. 
 * @param pdev 
 * 
 * @return u8_t 
 * This function returns TRUE if DCBX completion received on 
 * this port 
 */
STATIC u8_t
lm_dcbx_is_comp_recv_on_port(
    IN lm_device_t *pdev)
{
    const   u32_t drv_flags_offset = OFFSETOF(shmem2_region_t,drv_flags);
    u32_t   drv_flags = 0;
    u8_t    is_completed = FALSE;

    if(!IS_PMF(pdev))
    {
        DbgBreakMsg("lm_dcbx_is_comp_recv_on_port error only PMF can access this field ");
        return FALSE;
    }

    lm_dcbx_read_shmem2_mcp_fields(pdev,
                                   drv_flags_offset,
                                   &drv_flags);

    if(GET_FLAGS(drv_flags, DRV_FLAGS_DCB_CONFIGURED))
    {
        is_completed = TRUE;
    }

    return is_completed;
}
/**
 * @description
 * 1. Make sure all the DCBX init parameters for this function 
 * are correct. 
 * 2. Register a set DCBX params in order to let the new PMF 
 * migration function to know the current DCBX settings and that
 * the pdev varibales will mach the HW configuration. 
 * for example in MF when DCBX is configured to static 
 * configuration ELINK_FEATURE_CONFIG_PFC_ENABLED is set in pdev 
 * (we get only one interrupt)of only the original 
 * function.After PMF migration the first link updated will 
 * cause the PFC state to be incompatible.The function that 
 * become PMF doesn't have ELINK_FEATURE_CONFIG_PFC_ENABLED set 
 * @param pdev 
 */
void
lm_dcbx_pmf_migration(
    IN struct _lm_device_t *pdev)
{
    lm_status_t lm_status = LM_STATUS_SUCCESS;
    const u8_t is_completion_received_on_port = 
        lm_dcbx_is_comp_recv_on_port(pdev); 

    DbgBreakIf(TRUE != IS_DCB_ENABLED(pdev));

    // We called lm_dcbx_init_params at the beginning
    // verify all the parameters are correct and that there is no error.    
    DbgBreakIf(FALSE != pdev->dcbx_info.is_dcbx_neg_received);
    DbgBreakIf(DCBX_UPDATE_TASK_STATE_FREE != pdev->dcbx_info.dcbx_update_lpme_task_state);
    DbgBreakIf(CHK_NULL(pdev->dcbx_info.pfc_fw_cfg_virt));

    // for this function the only error possibole is that the pfc_fw_cfg_virt wasn't allocated.
    if((DCBX_ERROR_NO_ERROR != pdev->dcbx_info.dcbx_error) ||
        (FALSE == IS_MULTI_VNIC(pdev)))
    {
        DbgBreakMsg("lm_dcbx_init : lm_mcp_cmd_send_recieve failed ");
        return;
    }

    // If we received the DCBX parameters before on this port the new PMF 
    // will read the current DCBX parameters
    if(FALSE == is_completion_received_on_port)
    {
        // DCBX parameters were not received before
        return;
    }
    // Register a set params in order to let the new PMF migration 
    // function to know the current DCBX settings.
    // A side effect of this function be to set the DCBX parameters again,
    // but this is a must because in case of an error(or if we have an 
    // innterrupt from MCP that eas not handled) the seetings that are 
    // currently on the chip may not be equale to the local settings. 
    lm_status = MM_REGISTER_LPME(pdev, 
                                 lm_dcbx_init_lpme_set_params,
                                 TRUE,
                                 FALSE);// DCBX sends ramrods

    if (LM_STATUS_SUCCESS != lm_status)
    {
        pdev->dcbx_info.dcbx_error |= DCBX_ERROR_REGISTER_LPME;
        // No rollback
        // Problem because if DCBX interrupt isn't receive the chip will be 
        // stuck beacuse QM queues are stopped.
        // For release version this will call DCBX start that will restart QM queues.
        DbgBreakMsg("lm_dcbx_int : The chip QM queues are stuck until an interrupt from MCP");
    }
}
/*******************************************************************************
 * Description:
 *              The PMF function starts the DCBX negotiation after sending the 
 *              MIB DRV_MSG_LLDP_PMF_MSG with new LLDP/DCBX configurations if available.
 *              The PMF will call the function dcbx_stop_Hw_TX () that will ensure
 *              that no traffic can be sent. (The driver will send a ramrod to the
 *              FW that will stop all the queues in the QM) 
 *              After 1 second (a timer elapsed) if DCBX negotiation didn't end 
 *              (pdev.vars.dcbx_neg_received =0) and link is up a WI lm_dcbx_resume_TX()
 *              is scheduled .
 *              In WI read the configuration from local MIB and set DCBX parameters 
 *              to the value in local_MIB.
 *
 * Return:
******************************************************************************/
void 
lm_dcbx_init(struct _lm_device_t * pdev)
{
    u32_t       fw_resp                     = 0 ;
    lm_status_t lm_status                   = LM_STATUS_FAILURE ;
    u32_t       dcbx_lldp_params_offset     = SHMEM_LLDP_DCBX_PARAMS_NONE;
    const u32_t mcp_dcbx_lldp_params_offset = OFFSETOF(shmem2_region_t,dcbx_lldp_params_offset);

    DbgBreakIf(FALSE != IS_DCB_ENABLED(pdev));
    
    if(IS_DCB_SUPPORTED(pdev))
    {// DCBX is supported on E1H. E2 only in 2 port mode.
        
            lm_dcbx_read_shmem2_mcp_fields(pdev,
                                           mcp_dcbx_lldp_params_offset,
                                           &dcbx_lldp_params_offset);
    
            if (SHMEM_LLDP_DCBX_PARAMS_NONE != dcbx_lldp_params_offset)
            {// DCBX supported in MCP

                lm_status = lm_dcbx_init_params(pdev);
                if(LM_STATUS_SUCCESS != lm_status)
                {// If dcbx pfc_fw_cfg could not be allocated DCBX isn't supported
                    return;
                }
                if(IS_PMF_ORIGINAL(pdev))
                {//Only the PMF starts and handles
                    pdev->dcbx_info.is_enabled = TRUE;
    
                    lm_dcbx_init_lldp_updated_params(pdev,
                                                     dcbx_lldp_params_offset);
        
                    lm_dcbx_admin_mib_updated_params(pdev,
                                                     dcbx_lldp_params_offset);
                            
                    lm_status = lm_mcp_cmd_send_recieve( pdev, 
                                                         lm_mcp_mb_header, 
                                                         DRV_MSG_CODE_DCBX_ADMIN_PMF_MSG,
                                                         0,
                                                         MCP_CMD_DEFAULT_TIMEOUT, 
                                                         &fw_resp ) ;
                    
                    if( lm_status != LM_STATUS_SUCCESS )
                    {
                        pdev->dcbx_info.dcbx_error |= DCBX_ERROR_MCP_CMD_FAILED;
                        DbgBreakMsg("lm_dcbx_init : lm_mcp_cmd_send_recieve failed ");
                        return;
                    }
    
                }//PMF Original
                else
                {
                    pdev->dcbx_info.is_enabled = TRUE;
                    if(IS_PMF_MIGRATION(pdev))
                    {
                        // Send an attention on this Function.
                        // We create an interrupt on this function to make sure we will wake up another time 
                        // to send the MCP ACK.
                        LM_GENERAL_ATTN_INTERRUPT_SET(pdev,FUNC_ID(pdev));
                    }
                }
            }// DCBX supported in MCP
    } //DCBX enabled.
}
/**********************end DCBX INIT FUNCTIONS**************************************/

/**********************start DCBX UPDATE FUNCTIONS**************************************/
/*******************************************************************************
 * Description:
 *              Any DCBX update will be treated as a runtime change.
 *              Runtime changes can take more than 1 second and can't be handled
 *              from DPC.
 *              When the PMF detects a DCBX update it will schedule a WI that
 *              will handle the job.
 *              This function should be called in PASSIVE IRQL (Currently called from
 *              DPC) and in mutual exclusion any acces to lm_dcbx_stop_HW_TX
 *              /lm_dcbx_resume_HW_TX.
 *
 * Return:
******************************************************************************/
void 
lm_dcbx_update_lpme_set_params(struct _lm_device_t *pdev)
{
    u32_t offset        = 0;
    u32_t drv_status    = 0;

    offset = OFFSETOF(shmem_region_t, func_mb[FUNC_MAILBOX_ID(pdev)].drv_status) ;

    // drv_status
    LM_SHMEM_READ(pdev,
                  offset,
                  &drv_status);

    if((IS_PMF(pdev))&&
       (GET_FLAGS( drv_status, DRV_STATUS_DCBX_NEGOTIATION_RESULTS))&&
       (DCBX_UPDATE_TASK_STATE_SCHEDULE == pdev->dcbx_info.dcbx_update_lpme_task_state))
    {
        // No lock is needed to be taken because lm_dcbx_set_params is only called from a WI
        lm_dcbx_set_params(pdev);

        pdev->dcbx_info.dcbx_update_lpme_task_state = 
            DCBX_UPDATE_TASK_STATE_HANDLED;
        // Send an attention on this Function.
        // We create an interrupt on this function to make sure we will wake up another time 
        // to send the MCP ACK.
        LM_GENERAL_ATTN_INTERRUPT_SET(pdev,FUNC_ID(pdev));
    }
    else
    {
        DbgBreakMsg("lm_dcbx_update_lpme_set_params error");
    }
}
/**********************end DCBX UPDATE FUNCTIONS**************************************/




