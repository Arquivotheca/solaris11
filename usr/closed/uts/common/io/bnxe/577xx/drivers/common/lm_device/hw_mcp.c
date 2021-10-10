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
 *    11/26/07 Alon Elhanani    Inception.
 ******************************************************************************/

#include "lm5710.h"
#include "license.h"
#include "mcp_shmem.h"
/*
 *Function Name:lm_mcp_cmd_init
 *
 *Parameters:
 *
 *Description:
 *      initiate sequence of mb + verify boot code version
 *Returns:
 *
 */
lm_status_t lm_mcp_cmd_init( struct _lm_device_t *pdev)
{
    u32_t val        = 0 ;
    u32_t bc_rev     = 0 ;
    u32_t offset     = 0 ;
    u8_t  func_mb_id = 0;


    DbgMessage(pdev, INFORMi , "### mcp_cmd_init\n");

    if CHK_NULL(pdev)
    {
        return LM_STATUS_FAILURE ;
    }

    // we are on NO_MCP mode - nothing to do
    if( 0 != GET_FLAGS(pdev->params.test_mode, TEST_MODE_NO_MCP ) )
    {
        return LM_STATUS_SUCCESS ;
    }

    //validtae bc version
    bc_rev = (pdev->hw_info.bc_rev>>8) ;

    if (bc_rev < BC_REV_SUPPORTED)
    {
        DbgMessage2(pdev,FATAL,"bc version is less than 0x%x equal to 0x%x.\n", BC_REV_SUPPORTED, bc_rev );
        DbgBreakMsg("Please upgrade the bootcode version.\n");
        // TODO add event log
        return LM_STATUS_INVALID_PARAMETER;
    }

    // enable optic module verification according to BC version
    if (bc_rev >= REQ_BC_VER_4_VRFY_FIRST_PHY_OPT_MDL)
    {
        SET_FLAGS(pdev->params.link.feature_config_flags, ELINK_FEATURE_CONFIG_BC_SUPPORTS_OPT_MDL_VRFY);
    }

     if (bc_rev >= REQ_BC_VER_4_VRFY_SPECIFIC_PHY_OPT_MDL)
     {
        SET_FLAGS(pdev->params.link.feature_config_flags, ELINK_FEATURE_CONFIG_BC_SUPPORTS_DUAL_PHY_OPT_MDL_VRFY);
     }

     if (bc_rev >= REQ_BC_VER_4_VRFY_VNTAG_SUPPORTED)
     {
         SET_FLAGS(pdev->params.link.feature_config_flags, ELINK_FEATURE_CONFIG_BC_SUPPORTS_VNTAG);
     }

    // regular MCP mode
    func_mb_id = FUNC_MAILBOX_ID(pdev);

    // read first seq number from shared memory
    offset = OFFSETOF(shmem_region_t, func_mb[func_mb_id].drv_mb_header);
    LM_SHMEM_READ(pdev, offset, &val);
    pdev->vars.fw_wr_seq = (u16_t)(val & DRV_MSG_SEQ_NUMBER_MASK);

    // read current mcp_pulse value
    offset = OFFSETOF(shmem_region_t,func_mb[func_mb_id].mcp_pulse_mb) ;
    LM_SHMEM_READ(pdev, offset ,&val);
    pdev->vars.drv_pulse_wr_seq = (u16_t)(val & MCP_PULSE_SEQ_MASK);

    return LM_STATUS_SUCCESS ;
}

lm_status_t lm_mcp_set_mf_bw(struct _lm_device_t *pdev, IN u8_t min_bw, IN u8_t max_bw)
{
    u32_t       minmax_param    = 0;
    u32_t       resp            = 0;
    lm_status_t lm_status       = LM_STATUS_SUCCESS;
    //if in no MCP mode, don't do anything
    if(!lm_is_mcp_detected(pdev))
    {
        DbgMessage(pdev, WARNmi, "No MCP detected.\n");
        return LM_STATUS_SUCCESS;
    }
    //if bootcode is less then REQ_BC_VER_4_SET_MF_BW, fail
    if(pdev->hw_info.bc_rev < REQ_BC_VER_4_SET_MF_BW)
    {
        DbgMessage(pdev, WARNmi, "Invalid bootcode version.\n");
        return LM_STATUS_INVALID_PARAMETER;
    }
    //if not E2 or not MF mode, fail
    if(!CHIP_IS_E2(pdev) || !IS_MULTI_VNIC(pdev))
    {
        DbgMessage(pdev, WARNmi, "Device is E1/E1.5 or in SF mode.\n");
        return LM_STATUS_INVALID_PARAMETER;
    }
    //if the parameters are not valid, fail
    if ((max_bw > 100))
    {
        DbgMessage(pdev, WARNmi, "Invalid parameters.\n");
        return LM_STATUS_INVALID_PARAMETER;
    }
    //build MCP command parameter from min_bw/max_bw
    //we use FUNC_MF_CFG_MIN_BW_SHIFT because the param structure is supposed to
    //be equivalent for this opcode and for the DCC opcode, but there is no define 
    //for this opcode.
    ASSERT_STATIC(FUNC_MF_CFG_MIN_BW_MASK == DRV_MSG_CODE_SET_MF_BW_MIN_MASK);
    ASSERT_STATIC(FUNC_MF_CFG_MAX_BW_MASK == DRV_MSG_CODE_SET_MF_BW_MAX_MASK);
    minmax_param =  (min_bw << FUNC_MF_CFG_MIN_BW_SHIFT)|
                    (max_bw << FUNC_MF_CFG_MAX_BW_SHIFT);

    //call lm_mcp_cmd_send_recieve with DRV_MSG_CODE_SET_MF_BW opcode and the parameter
    lm_mcp_cmd_send_recieve(pdev, lm_mcp_mb_header, DRV_MSG_CODE_SET_MF_BW, minmax_param, MCP_CMD_DEFAULT_TIMEOUT, &resp);

    //make sure that the response is FW_MSG_CODE_SET_MF_BW_SENT 
    if(resp != FW_MSG_CODE_SET_MF_BW_SENT)
    {
        DbgBreakIf(resp != FW_MSG_CODE_SET_MF_BW_SENT);
        return LM_STATUS_FAILURE;
    }

    //return what lm_mcp_cmd_send_recieve returned
    return lm_status;
}

/*
 *Function Name:lm_mcp_cmd_send
 *
 *Parameters:
 *
 *Description:
 *      send
 *Returns:
 *
 */
lm_status_t lm_mcp_cmd_send( struct _lm_device_t *pdev, lm_mcp_mb_type mb_type, u32_t drv_msg, u32_t param )
{
    u16_t* p_seq      = NULL ;
    u32_t  offset     = 0 ;
    u32_t  drv_mask   = 0 ;
    u8_t   func_mb_id = 0 ;

    DbgMessage3(pdev, INFORMi , "### mcp_cmd_send mb_type=0x%x drv_msg=0x%x param=0x%x\n", mb_type, drv_msg, param );

    if CHK_NULL(pdev)
    {
        return LM_STATUS_FAILURE ;
    }

    // we are on NO_MCP mode - nothing to do
    if( 0 != GET_FLAGS(pdev->params.test_mode, TEST_MODE_NO_MCP ) )
    {
        return LM_STATUS_SUCCESS ;
    }

    switch( mb_type )
    {
    case lm_mcp_mb_header:
        func_mb_id = FUNC_MAILBOX_ID(pdev) ;
        p_seq      = &pdev->vars.fw_wr_seq ;
        drv_mask   = DRV_MSG_SEQ_NUMBER_MASK ;
        offset     = OFFSETOF(shmem_region_t, func_mb[func_mb_id].drv_mb_header) ;
        /* Write the parameter to the mcp */
        if (p_seq)
        {
            LM_SHMEM_WRITE(pdev,OFFSETOF(shmem_region_t, func_mb[func_mb_id].drv_mb_param),param);
        }
        break;

    case lm_mcp_mb_pulse:
        func_mb_id = FUNC_MAILBOX_ID(pdev) ;
        p_seq      = &pdev->vars.drv_pulse_wr_seq ;
        drv_mask   = DRV_PULSE_SEQ_MASK ;
        offset     = OFFSETOF(shmem_region_t, func_mb[func_mb_id].mcp_pulse_mb) ;
        break;
    case lm_mcp_mb_param:
    default:
        break;
    }

    if CHK_NULL( p_seq )
    {
        return LM_STATUS_INVALID_PARAMETER ;
    }

    // incremant sequence
    ++(*p_seq);

    // prepare message
    drv_msg |= ( (*p_seq) & drv_mask );

    LM_SHMEM_WRITE(pdev,offset,drv_msg);

    DbgMessage1(pdev, INFORMi , "mcp_cmd_send: Sent driver load cmd to MCP at 0x%x\n", drv_msg);

    return LM_STATUS_SUCCESS ;
}

/*
 *Function Name:lm_mcp_cmd_response
 *
 *Parameters:
 *              TBD - add timeout value
 *Description:
 *              assumption - only one request can be sent simultaneously
 *Returns:
 *
 */
lm_status_t lm_mcp_cmd_response( struct _lm_device_t *pdev,
                                 lm_mcp_mb_type       mcp_mb_type,
                                 u32_t                drv_msg,
                                 u32_t                timeout,
                                 OUT u32_t*           p_fw_resp )
{
    u16_t*      p_seq      = NULL ;
    u32_t       offset     = 0 ;
    u32_t       drv_mask   = 0 ;
    u32_t       fw_mask    = 0 ;
    u32_t       cnt        = 0 ;
    u32_t       wait_itr   = 0 ;
    u32_t       resp_mask  = 0xffffffff ;
    u8_t        func_mb_id = 0;
    lm_status_t lm_status  = LM_STATUS_SUCCESS ;

    UNREFERENCED_PARAMETER_(timeout);

    DbgMessage2(pdev, INFORMi , "### mcp_cmd_response mb_type=0x%x drv_msg=0x%x\n", mcp_mb_type, drv_msg );

    if ( CHK_NULL(pdev) || CHK_NULL(p_fw_resp) )
    {
        return LM_STATUS_FAILURE ;
    }

    switch( mcp_mb_type )
    {
    case lm_mcp_mb_header:
        func_mb_id = FUNC_MAILBOX_ID(pdev);
        p_seq      = &pdev->vars.fw_wr_seq ;
        drv_mask   = DRV_MSG_SEQ_NUMBER_MASK ;
        fw_mask    = FW_MSG_SEQ_NUMBER_MASK ;
        resp_mask  = FW_MSG_CODE_MASK ;
        offset     = OFFSETOF(shmem_region_t, func_mb[func_mb_id].fw_mb_header) ;
        break;

        // TBD - is it needed ??
    case lm_mcp_mb_pulse:
        func_mb_id = FUNC_MAILBOX_ID(pdev);
        p_seq      = &pdev->vars.drv_pulse_wr_seq ;
        drv_mask   = DRV_PULSE_SEQ_MASK ;
        fw_mask    = MCP_PULSE_SEQ_MASK ;
        offset     = OFFSETOF(shmem_region_t, func_mb[func_mb_id].mcp_pulse_mb) ;
        break;

    case lm_mcp_mb_param:
    default:
        break;
    }

    if CHK_NULL( p_seq )
    {
        return LM_STATUS_INVALID_PARAMETER ;
    }

    lm_status = LM_STATUS_TIMEOUT ;

    // Wait for reply 5 sec per unloading function
    //TODO exponential back off
    wait_itr = 240 * FW_ACK_NUM_OF_POLL * PORT_MAX * (u32_t)(IS_MULTI_VNIC(pdev) ? MAX_VNIC_NUM : 1);
    for(cnt = 0; cnt < wait_itr; cnt++)
    {
        mm_wait(pdev, FW_ACK_POLL_TIME_MS * 50);

        LM_SHMEM_READ(pdev, offset, p_fw_resp);

        if(( (*p_fw_resp) & fw_mask) == ( (*p_seq) & drv_mask))
        {
            lm_status = LM_STATUS_SUCCESS ;
            break;
        }
    }

    *p_fw_resp = (*p_fw_resp & resp_mask);

    return lm_status ;
}

lm_status_t lm_mcp_cmd_send_recieve_non_atomic( struct _lm_device_t *pdev,
                                             lm_mcp_mb_type       mcp_mb_type,
                                             u32_t                drv_msg,
                                             u32_t                param,
                                             u32_t                timeout,
                                             OUT u32_t*           p_fw_resp )
{
    lm_status_t lm_status = LM_STATUS_FAILURE;
    u32_t       val       = 0;

    lm_status = lm_mcp_cmd_send( pdev, mcp_mb_type, drv_msg, param) ;

    if( LM_STATUS_SUCCESS != lm_status )
    {
        val = lm_mcp_check(pdev);
        DbgMessage3(pdev,FATAL,"mcp_cmd_send_and_recieve: mcp_cmd_send drv_msg=0x%x failed. lm_status=0x%x mcp_check=0x%x\n", drv_msg, lm_status, val);
        DbgBreakMsg("mcp_cmd_send_and_recieve: mcp_cmd_send failed!\n");
        return lm_status;
    }

    DbgMessage1(pdev, INFORMi , "mcp_cmd_send_and_recieve: Sent driver cmd=0x%x to MCP\n",  drv_msg );

    lm_status = lm_mcp_cmd_response( pdev, mcp_mb_type, drv_msg, timeout, p_fw_resp ) ;

    if( LM_STATUS_SUCCESS != lm_status )
    {
        val = lm_mcp_check(pdev);
        DbgMessage3(pdev,FATAL,"mcp_cmd_send_and_recieve: mcp_cmd_response drv_msg=0x%x failed. lm_status=0x%x mcp_check=0x%x\n", drv_msg, lm_status, val);
        DbgBreakMsg("mcp_cmd_send_and_recieve: mcp_cmd_response failed!\n");
        return lm_status;
    }

    DbgMessage1(pdev, INFORMi , "mcp_cmd_send_and_recieve: Got response 0x%x from MCP\n", *p_fw_resp );

    return LM_STATUS_SUCCESS;
}

/*
 *Function Name:lm_mcp_cmd_send_recieve
 *
 *Parameters:
 *
 *Description:
 *
 *Returns: lm_status_t
 *
 */
lm_status_t lm_mcp_cmd_send_recieve( struct _lm_device_t *pdev,
                                     lm_mcp_mb_type       mcp_mb_type,
                                     u32_t                drv_msg,
                                     u32_t                param,
                                     u32_t                timeout,
                                     OUT u32_t*           p_fw_resp )
{
    lm_status_t lm_status = LM_STATUS_SUCCESS ;

    MM_ACQUIRE_MCP_LOCK(pdev);

    lm_status = lm_mcp_cmd_send_recieve_non_atomic(pdev, mcp_mb_type, drv_msg, param, timeout, p_fw_resp);

    MM_RELEASE_MCP_LOCK(pdev);

    return lm_status ;
}


// check if mcp program counter is advancing, In case it doesn't return the value in case it does, return 0
u32_t lm_mcp_check( struct _lm_device_t *pdev)
{
    static u32_t const offset = MCP_REG_MCPR_CPU_PROGRAM_COUNTER ;
    u32_t              reg    = 0 ;
    u32_t              i      = 0 ;

    reg = REG_RD(pdev, offset);

    for( i = 0; i<4; i++ )
    {
        if( REG_RD(pdev, offset) != reg )
        {
            return 0; // OK
        }
    }
    return reg; // mcp is hang on this value as program counter!
}

