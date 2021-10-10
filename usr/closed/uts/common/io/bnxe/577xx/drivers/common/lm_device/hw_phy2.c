/*******************************************************************************
* The information contained in this file is confidential and proprietary to
* Broadcom Corporation.  No part of this file may be reproduced or
* distributed, in any form or by any means for any purpose, without the
* express written permission of Broadcom Corporation.
*
* (c) COPYRIGHT 2001-2003 Broadcom Corporation, ALL RIGHTS RESERVED.
*
*
* Module Description:
*
*
* History:
*    11/15/01 Hav Khauv        Inception.
******************************************************************************/

#include "lm5710.h"
#include "phy_reg.h"
#include "license.h"
#include "mcp_shmem.h"
#include "lm_stats.h"
#include "577xx_int_offsets.h"

/***********************************************************/
/*              CLC - Common Link Component API            */
/***********************************************************/

/* Driver needs to redefine the cps_cb_st_ptr ( CPS CallBack Struct Pointer ) with its own */

#ifdef ELINK_DEBUG

void elink_cb_dbg(struct elink_dev *bp, char* fmt )
{	
	DbgMessage(bp, WARN, fmt);
}
void elink_cb_dbg1(struct elink_dev *bp, char* fmt, u32 arg1 )
{
    DbgMessage1(bp, WARN, fmt, arg1);
}
void elink_cb_dbg2(struct elink_dev *bp, char* fmt, u32 arg1, u32 arg2 )
{
    DbgMessage2(bp, WARN, fmt, arg1, arg2);
}

void elink_cb_dbg3(struct elink_dev *bp, char* fmt, u32 arg1, u32 arg2, u32 arg3 )
{
    DbgMessage3(bp, WARN, fmt, arg1, arg2, arg3);
}

#endif /* ELINK_DEBUG */

u32 elink_cb_reg_read(struct elink_dev *cb, u32 reg_addr )
{
    return REG_RD(cb, reg_addr);
}

void elink_cb_reg_write(struct elink_dev *cb, u32 reg_addr, u32 val )
{
    REG_WR(cb, reg_addr, val);
}

/* wb_write - pointer to 2 32 bits vars to be passed to the DMAE*/
void elink_cb_reg_wb_write(struct elink_dev *cb, u32 offset, u32 *wb_write, u16 len )
{
   REG_WR_DMAE_LEN(cb, offset, wb_write, len);
}

void elink_cb_reg_wb_read(struct elink_dev *cb, u32 offset, u32 *wb_write, u16 len )
{
   REG_RD_DMAE_LEN(cb, offset, wb_write, len);
}

/* mode - 0( LOW ) /1(HIGH)*/
u8 elink_cb_gpio_write(struct elink_dev *cb, u16 gpio_num, u8 mode, u8 port)
{
    return lm_gpio_write(cb, gpio_num, mode, port);
}

u32 elink_cb_gpio_read(struct elink_dev *cb, u16 gpio_num, u8 port)
{
    u32 val=0;
    lm_gpio_read(cb, gpio_num, &val, port);
    return val;
}

u8 elink_cb_gpio_int_write(struct elink_dev *cb, u16 gpio_num, u8 mode, u8 port)
{
    return lm_gpio_int_write(cb, gpio_num, mode, port);
}
void elink_cb_udelay(struct elink_dev *cb, u32 microsecond)
{
#define MAX_WAIT_INTERVAL 50

    u32_t wait_itr  = (microsecond/MAX_WAIT_INTERVAL) ;
    u32_t cnt       = 0;
    u32_t wait_time = MAX_WAIT_INTERVAL ;

    if( 0 == wait_itr )
    {
        wait_time = microsecond ;
        wait_itr = 1;
    }

    for(cnt = 0; cnt < wait_itr; cnt++)
    {
        mm_wait(cb , wait_time );
    }
}
u32 elink_cb_fw_command(struct elink_dev *cb, u32 command, u32 param)
{
	u32 fw_resp = 0;
	lm_mcp_cmd_send_recieve(cb, lm_mcp_mb_header, command, param, MCP_CMD_DEFAULT_TIMEOUT,
				&fw_resp );
	return fw_resp;
}

void elink_cb_download_progress(struct elink_dev *cb, u32 cur, u32 total)
{
    UNREFERENCED_PARAMETER_(cb);
    UNREFERENCED_PARAMETER_(cur);
    UNREFERENCED_PARAMETER_(total);

#ifdef DOS
    printf("Downloaded %u bytes out of %u bytes\n", cur, total );
#endif // DOS
}

void elink_cb_event_log(struct elink_dev *cb, const elink_log_id_t elink_log_id, ...)
{
    va_list     ap;
    lm_log_id_t lm_log_id = LM_LOG_ID_MAX;

    switch( elink_log_id )
    {
    case ELINK_LOG_ID_OVER_CURRENT:
        lm_log_id = LM_LOG_ID_OVER_CURRENT;
        break;

    case ELINK_LOG_ID_PHY_UNINITIALIZED:
        lm_log_id = LM_LOG_ID_PHY_UNINITIALIZED;
        break;

    case ELINK_LOG_ID_UNQUAL_IO_MODULE:
        lm_log_id = LM_LOG_ID_UNQUAL_IO_MODULE;
        break;

    case ELINK_LOG_ID_MDIO_ACCESS_TIMEOUT:
        lm_log_id = LM_LOG_ID_MDIO_ACCESS_TIMEOUT;
        break;

    default:
        DbgBreakIf(TRUE);
        break;
    } // elink_log_id switch

    va_start(ap, elink_log_id);

    mm_event_log_generic_arg_fwd( cb, lm_log_id, ap );

    va_end(ap);
}

u8 elink_cb_path_id(struct elink_dev *cb)
{
   return PATH_ID(cb);
}
/*******************************************************************************
* Macros.
******************************************************************************/

#define MII_REG(_type, _field)          (OFFSETOF(_type, _field)/2)

#define MDIO_INDIRECT_REG_ADDR      0x1f
#define MDIO_SET_REG_BANK(pdev,reg_bank)\
    lm_mwrite(pdev,MDIO_INDIRECT_REG_ADDR, reg_bank)

#define MDIO_ACCESS_TIMEOUT          1000

#define ELINK_STATUS_TO_LM_STATUS(_rc, _lm_status) switch(_rc)\
{\
case ELINK_STATUS_OK:\
    _lm_status = LM_STATUS_SUCCESS;\
    break;\
case ELINK_STATUS_TIMEOUT:\
    _lm_status = LM_STATUS_TIMEOUT;\
    break;\
case ELINK_STATUS_NO_LINK:\
    _lm_status = LM_STATUS_LINK_DOWN;\
    break;\
case ELINK_STATUS_ERROR:\
default:\
    _lm_status = LM_STATUS_FAILURE;\
    break;\
}

/*******************************************************************************
* Description:
*
* Return:
******************************************************************************/
lm_status_t
lm_mwrite( lm_device_t *pdev,
          u32_t reg,
          u32_t val)
{
    lm_status_t lm_status;
    u32_t tmp;
    u32_t cnt;
    u8_t port = PORT_ID(pdev);
    u32_t emac_base = (port?GRCBASE_EMAC1:GRCBASE_EMAC0);

    REG_WR(pdev,NIG_REG_XGXS0_CTRL_MD_ST + port*0x18, 1);

    DbgMessage(pdev,INFORM,"lm_mwrite\n");

    if(pdev->params.phy_int_mode == PHY_INT_MODE_AUTO_POLLING)
    {
        tmp=REG_RD(pdev,emac_base+EMAC_REG_EMAC_MDIO_MODE);
        tmp &= ~EMAC_MDIO_MODE_AUTO_POLL;

        REG_WR(pdev,emac_base+EMAC_REG_EMAC_MDIO_MODE,tmp);
        
        mm_wait(pdev, 40);
    }

    tmp = (pdev->vars.phy_addr << 21) | (reg << 16) | (val & EMAC_MDIO_COMM_DATA) |
        EMAC_MDIO_COMM_COMMAND_WRITE_22 |
        EMAC_MDIO_COMM_START_BUSY;

    REG_WR(pdev,emac_base+EMAC_REG_EMAC_MDIO_COMM,tmp);
    

    for(cnt = 0; cnt < 1000; cnt++)
    {
        mm_wait(pdev, 10);

        tmp=REG_RD(pdev,emac_base+EMAC_REG_EMAC_MDIO_COMM);
        if(!(tmp & EMAC_MDIO_COMM_START_BUSY))
        {
            mm_wait(pdev, 5);
            break;
        }
    }

    if(tmp & EMAC_MDIO_COMM_START_BUSY)
    {
        DbgBreakMsg("Write phy register failed\n");

        lm_status = LM_STATUS_FAILURE;
    }
    else
    {
        lm_status = LM_STATUS_SUCCESS;
    }

    if(pdev->params.phy_int_mode == PHY_INT_MODE_AUTO_POLLING)
    {
        tmp=REG_RD(pdev,emac_base+EMAC_REG_EMAC_MDIO_MODE);
        tmp |= EMAC_MDIO_MODE_AUTO_POLL;

        REG_WR(pdev,emac_base+EMAC_REG_EMAC_MDIO_MODE,tmp);
    }
    REG_WR(pdev,NIG_REG_XGXS0_CTRL_MD_ST +
		   port*0x18, 0);
    return lm_status;
} /* lm_mwrite */



/*******************************************************************************
* Description:
*
* Return:
******************************************************************************/
lm_status_t
lm_mread( lm_device_t *pdev,
         u32_t reg,
         u32_t *ret_val)
{
    lm_status_t lm_status;
    u32_t val;
    u32_t cnt;
    u8_t port = PORT_ID(pdev);
    u32_t emac_base = (port?GRCBASE_EMAC1:GRCBASE_EMAC0);

    REG_WR(pdev,NIG_REG_XGXS0_CTRL_MD_ST + port*0x18, 1);

    DbgMessage(pdev,INFORM,"lm_mread\n");

    if(pdev->params.phy_int_mode == PHY_INT_MODE_AUTO_POLLING)
    {
        val=REG_RD(pdev,emac_base+EMAC_REG_EMAC_MDIO_MODE);
        val &= ~EMAC_MDIO_MODE_AUTO_POLL;

        REG_WR(pdev,emac_base+EMAC_REG_EMAC_MDIO_MODE,val);

        mm_wait(pdev, 40);
    }

    val = (pdev->vars.phy_addr << 21) | (reg << 16) |
        EMAC_MDIO_COMM_COMMAND_READ_22 |
        EMAC_MDIO_COMM_START_BUSY;
    
    REG_WR(pdev,emac_base+EMAC_REG_EMAC_MDIO_COMM,val);

    for(cnt = 0; cnt < 1000; cnt++)
    {
        mm_wait(pdev, 10);

        val=REG_RD(pdev,emac_base+EMAC_REG_EMAC_MDIO_COMM);
        if(!(val & EMAC_MDIO_COMM_START_BUSY))
        {
            val &= EMAC_MDIO_COMM_DATA;
            break;
        }
    }

    if(val & EMAC_MDIO_COMM_START_BUSY)
    {
        DbgBreakMsg("Read phy register failed\n");

        val = 0;

        lm_status = LM_STATUS_FAILURE;
    }
    else
    {
        lm_status = LM_STATUS_SUCCESS;
    }

    *ret_val = val;

    if(pdev->params.phy_int_mode == PHY_INT_MODE_AUTO_POLLING)
    {
        val=REG_RD(pdev,emac_base+EMAC_REG_EMAC_MDIO_MODE);
        val |= EMAC_MDIO_MODE_AUTO_POLL;

        REG_WR(pdev,emac_base+EMAC_REG_EMAC_MDIO_MODE,val);
    }
    REG_WR(pdev,NIG_REG_XGXS0_CTRL_MD_ST +
           port*0x18, 0);
    return lm_status;
} /* lm_mread */

/*******************************************************************************
* Description:
*
* Return:
******************************************************************************/
lm_status_t
lm_phy45_read(
    lm_device_t *pdev,
    u8_t  phy_addr,
    u8_t dev_addr,
    u16_t reg, // offset
    u16_t *ret_val)
{

    u16_t       rc           = ELINK_STATUS_OK;
    lm_status_t lm_status    = LM_STATUS_SUCCESS;    

    PHY_HW_LOCK(pdev);

    rc = elink_phy_read(&pdev->params.link, phy_addr, dev_addr, reg, ret_val);

    PHY_HW_UNLOCK(pdev);

    ELINK_STATUS_TO_LM_STATUS( rc, lm_status );
 
    return lm_status;
}

/*******************************************************************************
* Description:
*
* Return:
******************************************************************************/
lm_status_t
lm_phy45_write(
    lm_device_t *pdev,
    u8_t  phy_addr,
    u8_t  dev_addr,
    u16_t reg, // offset
    u16_t val)
{
    
    u16_t       rc           = ELINK_STATUS_OK;
    lm_status_t lm_status    = LM_STATUS_SUCCESS;

    PHY_HW_LOCK(pdev);

    rc = elink_phy_write(&pdev->params.link, phy_addr, dev_addr, reg, val);

    PHY_HW_UNLOCK(pdev);

    ELINK_STATUS_TO_LM_STATUS( rc, lm_status );

    return lm_status;
}

lm_status_t
lm_set_phy_addr(lm_device_t *pdev,
                u8_t addr)
{
    if (addr > 0x1f)
    {
        DbgBreakMsg("lm_set_phy_addr: error addr not valid\n");
        return LM_STATUS_FAILURE;
    }
    pdev->vars.phy_addr = addr;
    return LM_STATUS_SUCCESS;
}

static void get_link_params(lm_device_t *pdev)
{
    u8_t  real_speed         = 0; // speed in 100M steps
    u16_t max_bw_in_Mbps     = 0; // In Mbps steps
    u16_t max_bw_in_100Mbps  = 0; // In 100Mbps steps

    if (IS_VFDEV(pdev))
    {
        pdev->vars.cable_is_attached = TRUE;
        pdev->vars.link_status = LM_STATUS_LINK_ACTIVE;
        SET_MEDIUM_SPEED(pdev->vars.medium,LM_MEDIUM_SPEED_10GBPS);
        return;
    }
    // link status

    if (!pdev->vars.link.link_up) 
    {
        pdev->vars.link_status = LM_STATUS_LINK_DOWN;
        pdev->vars.cable_is_attached = FALSE;
        
    }
    else 
    {
        // if we are in multifunction mode and function is disabled indicate OS link down.
        // Note that the CLC link is up so pmf handling is still going on
        if (IS_MULTI_VNIC(pdev) && (GET_FLAGS(pdev->hw_info.mf_info.func_mf_cfg, FUNC_MF_CFG_FUNC_DISABLED)) ) 
        {
            pdev->vars.link_status = LM_STATUS_LINK_DOWN;
            pdev->vars.cable_is_attached = FALSE;
        }
        else
        {
            if(!IS_MF_NIV_MODE(pdev)) //in NIV mode, link_status is modified only from lm_niv_vif_set
            {
                pdev->vars.link_status = LM_STATUS_LINK_ACTIVE;
            }
            pdev->vars.cable_is_attached = TRUE;
        }
        // get speed
        switch(pdev->vars.link.line_speed)
        {
        case ELINK_SPEED_10:
            SET_MEDIUM_SPEED(pdev->vars.medium,LM_MEDIUM_SPEED_10MBPS);
            real_speed = 0;
            break;
        case ELINK_SPEED_100:
            SET_MEDIUM_SPEED(pdev->vars.medium,LM_MEDIUM_SPEED_100MBPS);
            real_speed = 1;
            break;
        case ELINK_SPEED_1000:
            SET_MEDIUM_SPEED(pdev->vars.medium,LM_MEDIUM_SPEED_1000MBPS);
            real_speed = 10;
            break;
        case ELINK_SPEED_2500:
            SET_MEDIUM_SPEED(pdev->vars.medium,LM_MEDIUM_SPEED_2500MBPS);
            real_speed = 25;
            break;
        case ELINK_SPEED_10000:
            SET_MEDIUM_SPEED(pdev->vars.medium,LM_MEDIUM_SPEED_10GBPS);
            real_speed = 100;
            break;	
	 case ELINK_SPEED_20000:
            SET_MEDIUM_SPEED(pdev->vars.medium,LM_MEDIUM_SPEED_20GBPS);
            real_speed = 200;
            break;	
        default:
            DbgBreakIf(1);
            break; 
        }
        // get duplex
        SET_MEDIUM_DUPLEX(pdev->vars.medium,LM_MEDIUM_FULL_DUPLEX);
        if (pdev->vars.link.duplex == DUPLEX_HALF )
        {
            SET_MEDIUM_DUPLEX(pdev->vars.medium,LM_MEDIUM_HALF_DUPLEX);
        }
        // get flow_control
        pdev->vars.flow_control = LM_FLOW_CONTROL_NONE;
        if (pdev->vars.link.flow_ctrl & ELINK_FLOW_CTRL_RX)
        {
            pdev->vars.flow_control |= LM_FLOW_CONTROL_RECEIVE_PAUSE;
        }
        if (pdev->vars.link.flow_ctrl & ELINK_FLOW_CTRL_TX)
        {
            pdev->vars.flow_control |= LM_FLOW_CONTROL_TRANSMIT_PAUSE;
        }
        if (IS_MULTI_VNIC(pdev))
        {

            max_bw_in_Mbps = lm_get_max_bw(pdev,
                                           (real_speed *100),
                                           VNIC_ID(pdev));

            max_bw_in_100Mbps = max_bw_in_Mbps /100; // In 100Mbps steps

            if (real_speed > max_bw_in_100Mbps)
            {
                if (max_bw_in_100Mbps)
                {
                    SET_MEDIUM_SPEED(pdev->vars.medium,(LM_MEDIUM_SPEED_SEQ_START + ((max_bw_in_100Mbps-1)<<8)));
                }
                else
                {
                    // in case the pdev->params.max_bw[VNIC_ID(pdev)] = 0
                    SET_MEDIUM_SPEED(pdev->vars.medium,LM_MEDIUM_SPEED_SEQ_START);
                }
            }
        }
    }
}

void sync_link_status(lm_device_t *pdev)
{
    u32_t i;
    DbgMessage1(pdev, WARN, "sync_link_status: Func %d \n", FUNC_ID(pdev));

    // inform all other port vnics not ourself 
    for( i=0; i<4 ;i++ )
     {
        if (FUNC_ID(pdev) != (i*2 + PORT_ID(pdev)))
        {
            REG_WR(pdev,MISC_REG_AEU_GENERAL_ATTN_12 + 4*(i*2 + PORT_ID(pdev)),0x1);
            DbgMessage1(pdev, WARN, "sync_link_status: send attention to Func %d\n", (i*2 + PORT_ID(pdev)));
        }
    }
}

void 
lm_reset_link(lm_device_t *pdev)
{
    if (IS_VFDEV(pdev))
    {
        DbgMessage(pdev, FATAL, "lm_reset_link not implemented for VF\n");
        return;

    }
    // notify stats
    lm_stats_on_link_update(pdev, FALSE );
    pdev->vars.link_status = LM_STATUS_LINK_DOWN;
    pdev->vars.cable_is_attached = FALSE;
    pdev->vars.mac_type = MAC_TYPE_NONE;
    
    PHY_HW_LOCK(pdev);
    elink_link_reset(&pdev->params.link,&pdev->vars.link,1);
    PHY_HW_UNLOCK(pdev);
}
/**
 * @description
 * Configure cmng the firmware to the right CMNG values if this 
 * device is the PMF ,after link speed/ETS changes. 
 *  
 * @note This function must be called under PHY_LOCK 
 * @param pdev 
 */
void lm_cmng_update(lm_device_t *pdev)
{
    u32_t port_speed = 10;

    /* fairness is only supported for vnics in the meantime... */
    if ((!IS_MULTI_VNIC(pdev)) || 
        (!IS_PMF(pdev)) || 
        (!pdev->vars.link.link_up))
    {
        return;
    }
    switch(pdev->vars.link.line_speed)
    {
    case ELINK_SPEED_10:
        port_speed = 10;
        break;
    case ELINK_SPEED_100:
        port_speed = 100;
        break;
    case ELINK_SPEED_1000:
        port_speed = 1000;
        break;
    case ELINK_SPEED_2500:
        port_speed = 2500;    
        break;
    case ELINK_SPEED_10000:
        port_speed = 10000;
        break;	
    case ELINK_SPEED_20000:
        port_speed = 20000;
        break;
    default:
        DbgBreakIf(1);
        break; 
    }
    lm_cmng_init(pdev,port_speed);
}

void lm_reload_link_and_cmng(lm_device_t *pdev)
{
    if( IS_MULTI_VNIC(pdev) && pdev->hw_info.mcp_detected )
    {
        lm_cmng_get_shmem_info(pdev);
        lm_cmng_calc_params(pdev);
    }

    get_link_params(pdev);

    lm_cmng_update(pdev);
    
}

/**lm_niv_link_down_wi
 * A work item that's called in response to a link-down event in 
 * NIV mode. This function sends a CLEAR_ALL vif-lists ramrod. 
 * 
 * 
 * @param pdev the device
 */
void lm_niv_link_down_wi(lm_device_t *pdev)
{
    lm_niv_vif_list_update(pdev,VIF_LIST_RULE_CLEAR_ALL, 0, 0, 0);
}

// This function is called due to link change attention for none pmf it gets the link status from the 
// shmem 
void lm_link_report(lm_device_t *pdev)
{
    u8_t pause_ena = 0;

    lm_reload_link_and_cmng(pdev);

    if (pdev->vars.link.link_up) 
    {   // link up
        // dropless flow control
        if (IS_PMF(pdev) && pdev->params.l2_fw_flow_ctrl)
        {
            if (pdev->vars.link.flow_ctrl & ELINK_FLOW_CTRL_TX)
            {
                pause_ena = 1;
            }
            LM_INTMEM_WRITE16(pdev,USTORM_ETH_PAUSE_ENABLED_OFFSET(PORT_ID(pdev)), pause_ena, BAR_USTRORM_INTMEM);
        }
        pdev->vars.mac_type = pdev->vars.link.mac_type;
        DbgBreakIf(pdev->vars.mac_type > MAC_TYPE_MAX);

        // indicate link up - except if we're in NIV mode where we wait for the VIF-SET/enable command from the MCP
        if(!IS_MF_NIV_MODE(pdev))
        {
            mm_indicate_link(pdev, pdev->vars.link_status, pdev->vars.medium);
            DbgMessage2(pdev,WARN,"lm_link_update: indicate link %d 0x%x \n",pdev->vars.link_status,pdev->vars.medium);
        }
        // notify stats
        lm_stats_on_link_update(pdev, TRUE );
    }
    else
    {   // link down
        // indicate link down
        pdev->vars.mac_type = MAC_TYPE_NONE;
        pdev->vars.stats.stats_collect.stats_hw.b_is_link_up = FALSE;
        // indicate link down
        mm_indicate_link(pdev, pdev->vars.link_status, pdev->vars.medium);
        //if we're in NIV mode we need to indicate the link down event to the FW.
        if(IS_MF_NIV_MODE(pdev))
        {
            lm_status_t lm_status = MM_REGISTER_LPME(pdev, lm_niv_link_down_wi, TRUE, TRUE);
            DbgBreakIf(LM_STATUS_SUCCESS != lm_status);
        }
        DbgMessage2(pdev,WARN,"lm_link_update: indicate link %d %d \n",pdev->vars.link_status,pdev->vars.medium);
    }
    // notify othres funcs
    if (IS_MULTI_VNIC(pdev) && IS_PMF(pdev)) 
    {
        sync_link_status(pdev);
    }    
}

// This function is called due to link change interrupt for the relevant function
// NOTE: this function must be called under phy lock
lm_status_t lm_link_update(lm_device_t *pdev)
{
    if CHK_NULL( pdev )
    {
        DbgBreakIf(!pdev) ;
        return LM_STATUS_FAILURE;
    }
    // notify stats
    lm_stats_on_link_update(pdev, FALSE );
    PHY_HW_LOCK(pdev);
    elink_link_update(&pdev->params.link,&pdev->vars.link);
    PHY_HW_UNLOCK(pdev);
    lm_link_report(pdev);
    // increment link_chng_cnt counter to indicate there was some link change.
    pdev->vars.link_chng_cnt++;
    return LM_STATUS_SUCCESS;
}

static void lm_set_phy_selection( lm_device_t *pdev, u8_t i)
{
    u32 phy_sel ;
    if (pdev->params.link.multi_phy_config & PORT_HW_CFG_PHY_SWAPPED_ENABLED)
    {
        phy_sel = PORT_HW_CFG_PHY_SELECTION_SECOND_PHY - (i - ELINK_EXT_PHY1);
    }
    else
    {
        phy_sel = PORT_HW_CFG_PHY_SELECTION_FIRST_PHY + (i - ELINK_EXT_PHY1);
    }
    RESET_FLAGS( pdev->params.link.multi_phy_config, PORT_HW_CFG_PHY_SELECTION_MASK );
    SET_FLAGS( pdev->params.link.multi_phy_config, phy_sel);
}

static void lm_set_phy_priority_selection( lm_device_t *pdev, u8_t i)
{
    u32 phy_sel;

    if (pdev->params.link.multi_phy_config & PORT_HW_CFG_PHY_SWAPPED_ENABLED)
    {
        phy_sel = PORT_HW_CFG_PHY_SELECTION_SECOND_PHY_PRIORITY - (i - ELINK_EXT_PHY1);
    }
    else
    {
        phy_sel = PORT_HW_CFG_PHY_SELECTION_FIRST_PHY_PRIORITY + (i - ELINK_EXT_PHY1);
    }
    RESET_FLAGS( pdev->params.link.multi_phy_config, PORT_HW_CFG_PHY_SELECTION_MASK );
    SET_FLAGS( pdev->params.link.multi_phy_config, phy_sel);
}

/*******************************************************************************
 * Description:
 *
 * Return:
 ******************************************************************************/
STATIC
lm_status_t lm_set_phy_priority_mode(lm_device_t *pdev)
{
    lm_status_t lm_status = LM_STATUS_SUCCESS;
    u8_t        i         = 0;

    if (CHK_NULL(pdev))
    {
        return LM_STATUS_INVALID_PARAMETER;
    }

    switch (pdev->params.phy_priority_mode)
    {
    case PHY_PRIORITY_MODE_HW_DEF:
        RESET_FLAGS( pdev->params.link.multi_phy_config, PORT_HW_CFG_PHY_SELECTION_MASK );
        SET_FLAGS( pdev->params.link.multi_phy_config, pdev->hw_info.multi_phy_config);
        break;

    case PHY_PRIORITY_MODE_10GBASET:
        i = ELINK_EXT_PHY1;
        while (i < ELINK_MAX_PHYS)
        {
            if (pdev->params.link.phy[i].media_type == ELINK_ETH_PHY_BASE_T)
            {
                lm_set_phy_priority_selection(pdev, i);
                break;
                }
            i++;
            }
            break;

    case PHY_PRIORITY_MODE_SERDES:
        i = ELINK_EXT_PHY1;
        while (i < ELINK_MAX_PHYS)
    {
            if ((pdev->params.link.phy[i].media_type == ELINK_ETH_PHY_SFP_FIBER) ||
                (pdev->params.link.phy[i].media_type == ELINK_ETH_PHY_XFP_FIBER) ||
		(pdev->params.link.phy[i].media_type == ELINK_ETH_PHY_DA_TWINAX) ||
		(pdev->params.link.phy[i].media_type == ELINK_ETH_PHY_NOT_PRESENT))
    {
         lm_set_phy_priority_selection(pdev, i);
         break;
    }
            i++;
    }
        break;

    case PHY_PRIORITY_MODE_HW_PIN:
        RESET_FLAGS( pdev->params.link.multi_phy_config, PORT_HW_CFG_PHY_SELECTION_MASK );
        SET_FLAGS( pdev->params.link.multi_phy_config, PORT_HW_CFG_PHY_SELECTION_HARDWARE_DEFAULT);
        break;

    default:
        DbgBreak();
        lm_status = LM_STATUS_FAILURE;
        break;
    }

    return lm_status;
}

/*******************************************************************************
 * Description:
 *
 * Return:
 ******************************************************************************/
STATIC
lm_status_t lm_set_phy_link_params(lm_device_t     *pdev,
                                 lm_medium_t        req_medium,
                                 lm_flow_control_t  flow_control,
                                 u8_t               sw_config,
                                 u8_t               phy_num)
{
    lm_medium_t speed = GET_MEDIUM_SPEED(req_medium);
    lm_medium_t duplex = GET_MEDIUM_DUPLEX(req_medium);

    DbgMessage1(pdev,WARN,"lm_set_phy_link_params: speed 0x%x\n",speed);
    // Get speed from shared memory not registry - if mcp is detected... 
    if(pdev->hw_info.mcp_detected && ((speed == LM_MEDIUM_SPEED_HARDWARE_DEFAULT) || (IS_MULTI_VNIC(pdev))))
    {
        switch(pdev->hw_info.link_config[phy_num] & PORT_FEATURE_LINK_SPEED_MASK)
        {
        case PORT_FEATURE_LINK_SPEED_10M_FULL:
            SET_MEDIUM_SPEED(speed,LM_MEDIUM_SPEED_10MBPS);
            SET_MEDIUM_DUPLEX(duplex,LM_MEDIUM_FULL_DUPLEX);
            break;
        case PORT_FEATURE_LINK_SPEED_10M_HALF: 
            SET_MEDIUM_SPEED(speed,LM_MEDIUM_SPEED_10MBPS);
            SET_MEDIUM_DUPLEX(duplex,LM_MEDIUM_HALF_DUPLEX);
            break;
        case PORT_FEATURE_LINK_SPEED_100M_FULL:
            SET_MEDIUM_SPEED(speed,LM_MEDIUM_SPEED_100MBPS);
            SET_MEDIUM_DUPLEX(duplex,LM_MEDIUM_FULL_DUPLEX);
            break;
        case PORT_FEATURE_LINK_SPEED_100M_HALF:    
            SET_MEDIUM_SPEED(speed,LM_MEDIUM_SPEED_100MBPS);
            SET_MEDIUM_DUPLEX(duplex,LM_MEDIUM_HALF_DUPLEX);
            break;
        case PORT_FEATURE_LINK_SPEED_1G:
            SET_MEDIUM_SPEED(speed,LM_MEDIUM_SPEED_1000MBPS);
            SET_MEDIUM_DUPLEX(duplex,LM_MEDIUM_FULL_DUPLEX);
            break;
        case PORT_FEATURE_LINK_SPEED_2_5G:
            SET_MEDIUM_SPEED(speed,LM_MEDIUM_SPEED_2500MBPS);
            SET_MEDIUM_DUPLEX(duplex,LM_MEDIUM_FULL_DUPLEX);
            break;
        case PORT_FEATURE_LINK_SPEED_10G_CX4:         
            if (sw_config == LM_SWCFG_1G)
            {
                DbgMessage1(pdev,WARN,"lm_set_phy_link_params: invalid speed parameter 0x%x\n",speed);
                return LM_STATUS_SUCCESS;
            }
            SET_MEDIUM_SPEED(speed,LM_MEDIUM_SPEED_10GBPS);
            SET_MEDIUM_DUPLEX(duplex,LM_MEDIUM_FULL_DUPLEX);
            break;
        case PORT_FEATURE_LINK_SPEED_AUTO:
            SET_MEDIUM_SPEED(speed,LM_MEDIUM_SPEED_AUTONEG);
            SET_MEDIUM_DUPLEX(duplex,LM_MEDIUM_FULL_DUPLEX);
            break;
        default:
            //Follow Teton solution:We need to do this because Microsoft's definition 
            // is not complete, like speed 2.5gb or some other speeds.
            SET_MEDIUM_SPEED(speed,LM_MEDIUM_SPEED_AUTONEG);
            SET_MEDIUM_DUPLEX(duplex,LM_MEDIUM_FULL_DUPLEX);
            break;
        }

        DbgMessage2(pdev,WARN,"lm_set_phy_link_params: speed 0x%x duplex 0x%x\n",speed,duplex);
    }
    pdev->params.link.req_duplex[phy_num] = DUPLEX_FULL;
    if ( duplex == LM_MEDIUM_HALF_DUPLEX)
    {
        pdev->params.link.req_duplex[phy_num] = DUPLEX_HALF;
    }

    switch (speed)
    {
    case  LM_MEDIUM_SPEED_AUTONEG:   
        pdev->params.link.req_line_speed[phy_num] = ELINK_SPEED_AUTO_NEG;
        break;     
    case  LM_MEDIUM_SPEED_10MBPS:   
        pdev->params.link.req_line_speed[phy_num] = ELINK_SPEED_10;
        break;                  
    case  LM_MEDIUM_SPEED_100MBPS:
        pdev->params.link.req_line_speed[phy_num] = ELINK_SPEED_100;
        break;                 
    case  LM_MEDIUM_SPEED_1000MBPS:
        pdev->params.link.req_line_speed[phy_num] = ELINK_SPEED_1000;
        break;                     
    case  LM_MEDIUM_SPEED_2500MBPS:
        pdev->params.link.req_line_speed[phy_num] = ELINK_SPEED_2500;
        break;                     
    case  LM_MEDIUM_SPEED_10GBPS:
        pdev->params.link.req_line_speed[phy_num] = ELINK_SPEED_10000;
        break;                     
    case  LM_MEDIUM_SPEED_20GBPS:
        pdev->params.link.req_line_speed[phy_num] = ELINK_SPEED_20000; 
        break; 
    default:
        DbgBreakIf(1);
        break;
    }

    pdev->params.link.req_flow_ctrl[phy_num] = 0;
    if (flow_control == LM_FLOW_CONTROL_NONE) 
    {
        pdev->params.link.req_flow_ctrl[phy_num] = ELINK_FLOW_CTRL_NONE;
    }
    else if (flow_control & LM_FLOW_CONTROL_AUTO_PAUSE) 
    {
        pdev->params.link.req_flow_ctrl[phy_num] = ELINK_FLOW_CTRL_AUTO;
    }
    else
    {

        if (flow_control & LM_FLOW_CONTROL_RECEIVE_PAUSE)
        {
            pdev->params.link.req_flow_ctrl[phy_num] |= ELINK_FLOW_CTRL_RX;
        }
        if (flow_control & LM_FLOW_CONTROL_TRANSMIT_PAUSE)
        {
            pdev->params.link.req_flow_ctrl[phy_num] |= ELINK_FLOW_CTRL_TX;
        }
    }

    return LM_STATUS_SUCCESS;
}

/*******************************************************************************
 * Description:
 *
 * Return:
 ******************************************************************************/
lm_status_t
lm_init_phy( lm_device_t       *pdev,
             lm_medium_t       req_medium,
             lm_flow_control_t flow_control,
             u32_t             selective_autoneg,
             u32_t             wire_speed,
             u32_t             wait_link_timeout_us)
{
    u8_t                i               = 0;
    u8_t                sw_config       = 0;
    u8_t                elink_status    = ELINK_STATUS_OK;
    lm_medium_t         speed           = 0;
    lm_medium_t         type            = GET_MEDIUM_TYPE(req_medium);
    struct elink_params *link           = &pdev->params.link;

    UNREFERENCED_PARAMETER_(wait_link_timeout_us);
    UNREFERENCED_PARAMETER_(wire_speed);
    UNREFERENCED_PARAMETER_(selective_autoneg);

    if (IS_VFDEV(pdev))
    {
        return LM_STATUS_SUCCESS;
    }

    //fill clc params
    if CHK_NULL( pdev )
    {
        DbgBreakIf(!pdev) ;
        return LM_STATUS_FAILURE;
    }

    // override preemphasis for specific svid/ssid
    if( 0x1120 == pdev->hw_info.svid )
    {
        switch (pdev->hw_info.ssid)
        {
        case 0x4f70:
        case 0x4375:
            {
                if( pdev->params.preemphasis_enable )
                {
                    // The relevant ssids are from SINGLE_MEDIA board type, so only EXT_PHY1 needs to be set.
                    SET_FLAGS(pdev->params.link.feature_config_flags, ELINK_FEATURE_CONFIG_OVERRIDE_PREEMPHASIS_ENABLED);
                    pdev->params.link.phy[ELINK_EXT_PHY1].rx_preemphasis[0] = (u16_t)pdev->params.preemphasis_rx_0;
                    pdev->params.link.phy[ELINK_EXT_PHY1].rx_preemphasis[1] = (u16_t)pdev->params.preemphasis_rx_1;
                    pdev->params.link.phy[ELINK_EXT_PHY1].rx_preemphasis[2] = (u16_t)pdev->params.preemphasis_rx_2;
                    pdev->params.link.phy[ELINK_EXT_PHY1].rx_preemphasis[3] = (u16_t)pdev->params.preemphasis_rx_3;
                    pdev->params.link.phy[ELINK_EXT_PHY1].tx_preemphasis[0] = (u16_t)pdev->params.preemphasis_tx_0;
                    pdev->params.link.phy[ELINK_EXT_PHY1].tx_preemphasis[1] = (u16_t)pdev->params.preemphasis_tx_1;
                    pdev->params.link.phy[ELINK_EXT_PHY1].tx_preemphasis[2] = (u16_t)pdev->params.preemphasis_tx_2;
                    pdev->params.link.phy[ELINK_EXT_PHY1].tx_preemphasis[3] = (u16_t)pdev->params.preemphasis_tx_3;
                }
            }
            break;

        default:
            break;
        }
    }

    if (CHIP_IS_E1x(pdev) && (!IS_MULTI_VNIC(pdev)) && pdev->params.mtu_max > LM_MTU_FLOW_CTRL_TX_THR)
    {
        pdev->params.link.req_fc_auto_adv = ELINK_FLOW_CTRL_TX;
    }
    else
    {
        pdev->params.link.req_fc_auto_adv = ELINK_FLOW_CTRL_BOTH;
    }

    for (i = 0 ; i < 6 ; i++)
    {
        pdev->params.link.mac_addr[i] = pdev->params.mac_addr[i];
    }

    sw_config = (u8_t)pdev->params.sw_config;
    DbgMessage1(pdev,WARN,"lm_init_phy: sw_config 0x%x\n",sw_config);

    if (sw_config == LM_SWCFG_HW_DEF)
    {
        sw_config = (u8_t)(pdev->params.link.switch_cfg>>PORT_FEATURE_CONNECTED_SWITCH_SHIFT);
        DbgMessage1(pdev,WARN,"lm_init_phy: sw_config 0x%x\n",sw_config);
    }

    switch (sw_config)
    {
    // TODO change to shmem defines
    case LM_SWCFG_1G:
        SET_MEDIUM_TYPE(pdev->vars.medium, LM_MEDIUM_TYPE_SERDES);
        break;
    case LM_SWCFG_10G:
        SET_MEDIUM_TYPE(pdev->vars.medium, LM_MEDIUM_TYPE_XGXS);
        break;
    default:
        DbgBreakIf(1);
        break;
    }
    // Override setting if dual media and phy type specified from miniport
    if ((ELINK_DUAL_MEDIA(link)) &&
        ((type == LM_MEDIUM_TYPE_SERDES) ||
         (type == LM_MEDIUM_TYPE_XGXS)))
    {
        SET_MEDIUM_TYPE(pdev->vars.medium, type);
    }

    lm_set_phy_link_params(pdev, req_medium, flow_control, sw_config, ELINK_INT_PHY);
    if (ELINK_DUAL_MEDIA(link))
    {
        lm_set_phy_link_params(pdev, req_medium, flow_control, sw_config, ELINK_EXT_PHY1);
    }

    if( pdev->hw_info.phy_no_10g_support )
    {
        speed = GET_MEDIUM_SPEED(req_medium);
        if( LM_MEDIUM_SPEED_10GBPS == speed )
        {
            DbgMessage1(pdev,WARN,"lm_init_phy: 10gb speed parameter is blocked 0x%x\n",speed);

            // block this request (elink does not support it) & log
            mm_event_log_generic(pdev, LM_LOG_ID_NO_10G_SUPPORT, PORT_ID(pdev) );
            return LM_STATUS_SUCCESS;
        }
    }

    switch (type) 
    {
    case LM_MEDIUM_TYPE_XGXS_LOOPBACK:
        pdev->params.link.loopback_mode = ELINK_LOOPBACK_XGXS;
        pdev->params.link.req_line_speed[0] = ELINK_SPEED_1000; 
        break;
    case LM_MEDIUM_TYPE_XGXS_10_LOOPBACK:
        pdev->params.link.loopback_mode = ELINK_LOOPBACK_XGXS;
        pdev->params.link.req_line_speed[0] = ELINK_SPEED_10000; 
        break;
    case LM_MEDIUM_TYPE_EMAC_LOOPBACK:
        pdev->params.link.loopback_mode = ELINK_LOOPBACK_EMAC;
        break;
    case LM_MEDIUM_TYPE_BMAC_LOOPBACK:
        pdev->params.link.loopback_mode = ELINK_LOOPBACK_BMAC;
        break;
    case LM_MEDIUM_TYPE_EXT_PHY_LOOPBACK:
        pdev->params.link.loopback_mode = ELINK_LOOPBACK_EXT_PHY;
        pdev->params.link.req_line_speed[0] = ELINK_SPEED_10000;
        // TBD: Dual Media ext PHY loopback test for second ext PHY ?
        break;
    case LM_MEDIUM_TYPE_EXT_LOOPBACK:
        pdev->params.link.loopback_mode = ELINK_LOOPBACK_EXT;
        break;
    case LM_MEDIUM_TYPE_XMAC_LOOPBACK:
        pdev->params.link.loopback_mode = ELINK_LOOPBACK_XMAC;
        break;
    case LM_MEDIUM_TYPE_UMAC_LOOPBACK:
        pdev->params.link.loopback_mode = ELINK_LOOPBACK_UMAC;
        break;
    default:
        pdev->params.link.loopback_mode = ELINK_LOOPBACK_NONE;
        break;        
    }

    // Handle dual media boards, if phy type specified from miniport
    if (ELINK_DUAL_MEDIA(link))
    {
        switch (type)
        {
        case LM_MEDIUM_TYPE_SERDES:
            i = ELINK_EXT_PHY1;
            while (i < ELINK_MAX_PHYS)
            {
                if ((pdev->params.link.phy[i].media_type == ELINK_ETH_PHY_SFP_FIBER) ||
                    (pdev->params.link.phy[i].media_type == ELINK_ETH_PHY_XFP_FIBER) ||
		    (pdev->params.link.phy[i].media_type == ELINK_ETH_PHY_DA_TWINAX))
                {
                    lm_set_phy_selection(pdev, i);
                    break;
                }
                i++;
            }
            break;

        case LM_MEDIUM_TYPE_XGXS:
            i = ELINK_EXT_PHY1;
            while (i < ELINK_MAX_PHYS)
            {
                if ((pdev->params.link.phy[i].media_type == ELINK_ETH_PHY_BASE_T))
                {
                    lm_set_phy_selection(pdev, i);
                    break;
                }
                i++;
            }
            break;

        case LM_MEDIUM_AUTO_DETECT:
            lm_set_phy_priority_mode(pdev);
            break;

        case LM_MEDIUM_TYPE_XGXS_LOOPBACK:
        case LM_MEDIUM_TYPE_XGXS_10_LOOPBACK:
        case LM_MEDIUM_TYPE_EMAC_LOOPBACK:
        case LM_MEDIUM_TYPE_BMAC_LOOPBACK:
        case LM_MEDIUM_TYPE_EXT_PHY_LOOPBACK:
        case LM_MEDIUM_TYPE_EXT_LOOPBACK:
        case LM_MEDIUM_TYPE_XMAC_LOOPBACK:
        case LM_MEDIUM_TYPE_UMAC_LOOPBACK:
            // Do nothing.
            break;
        default:
            DbgBreak();
            break;
        }
    }
    
    DbgMessage1(pdev,WARN,"lm_init_phy: loopback_mode 0x%x\n",pdev->params.link.loopback_mode);
    if (IS_PMF(pdev))
    {
        PHY_HW_LOCK(pdev);
        elink_status = elink_phy_init(&pdev->params.link,&pdev->vars.link);
        PHY_HW_UNLOCK(pdev);
    }
    else
    {
        elink_link_status_update(&pdev->params.link,&pdev->vars.link);
    }
    // Emulation FPGA or LOOPBACK non pmf in multi vnic mode link might be up now
    lm_link_report(pdev);
    return LM_STATUS_SUCCESS;
} /* lm_init_phy */
    
/*
 *Function Name:lm_get_external_phy_fw_version
 *
 *Parameters:
 *
 *Description:
 *  Funciton should be called under PHY_LOCK
 *Returns:
 *
 */        
lm_status_t
lm_get_external_phy_fw_version( lm_device_t *pdev,
                                u8_t *      sz_version,
                                u8_t        len )
{
    u8_t        elink_status = ELINK_STATUS_OK;    

    if ( CHK_NULL( sz_version ) || CHK_NULL( pdev ) )
    {
        return LM_STATUS_INVALID_PARAMETER;
    }

    // reset the returned value to zero
    *sz_version = '\0';

    PHY_HW_LOCK(pdev);
    elink_status = elink_get_ext_phy_fw_version(&pdev->params.link,TRUE,(u8_t *)sz_version, len );
    PHY_HW_UNLOCK(pdev);

    if (elink_status == ELINK_STATUS_OK) 
    {
        // Update internal hw_info structure for debugging purpose
        if( len <= sizeof(pdev->hw_info.sz_ext_phy_fw_ver) )
        {
            mm_memcpy( pdev->hw_info.sz_ext_phy_fw_ver,
                       sz_version,
                       min( (u32_t)sizeof(pdev->hw_info.sz_ext_phy_fw_ver), (u32_t)len) ) ;            
        }
        return LM_STATUS_SUCCESS ;
    }
    else 
    {
        return LM_STATUS_FAILURE;
    }
}

/*
 *Function Name:lm_update_external_phy_fw_prepare
 *
 *Parameters:
 *
 *Description:
 *
 *Returns:
 *
 */
lm_status_t
lm_update_external_phy_fw_prepare( lm_device_t *pdev )
{
    u8_t        elink_status = ELINK_STATUS_OK;
    lm_status_t lm_status  = LM_STATUS_SUCCESS;

    MM_ACQUIRE_PHY_LOCK(pdev);

    PHY_HW_LOCK(pdev);

    do
    {
        u32_t shmem_base[MAX_PATH_NUM], shmem_base2[MAX_PATH_NUM];
        shmem_base[0] = pdev->hw_info.shmem_base;
        shmem_base2[0] = pdev->hw_info.shmem_base2;

        if (!CHIP_IS_E1x(pdev))
        {
            LM_SHMEM2_READ(pdev, OFFSETOF(shmem2_region_t,other_shmem_base_addr), &shmem_base[1]);
            LM_SHMEM2_READ(pdev, OFFSETOF(shmem2_region_t,other_shmem2_base_addr), &shmem_base2[1]);
        }

        elink_common_init_phy(pdev, shmem_base, shmem_base2, CHIP_ID(pdev));

        if( ELINK_STATUS_OK != elink_status )
        {
            break;
        }

        elink_status = elink_phy_init(&pdev->params.link,&pdev->vars.link);
        if( ELINK_STATUS_OK != elink_status )
        {
            break;
        }

        elink_status = elink_link_reset(&pdev->params.link,&pdev->vars.link,0);

    } while(0);

    PHY_HW_UNLOCK(pdev);

    lm_link_report(pdev);

    MM_RELEASE_PHY_LOCK(pdev);

    if( ELINK_STATUS_OK != elink_status )
    {
        goto _exit;
    }

    switch( pdev->params.link.phy[ELINK_EXT_PHY1].type )
    {
    case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_SFX7101:
        {
            lm_gpio_write(pdev, MISC_REGISTERS_GPIO_0, MISC_REGISTERS_GPIO_HIGH, PORT_ID(pdev) );
        }
        break;
    default:
        break;
    } 

_exit:

    ELINK_STATUS_TO_LM_STATUS( elink_status, lm_status );

    return lm_status;
}

/*
 *Function Name:lm_update_external_phy_fw_reinit
 *
 *Parameters:
 *
 *Description:
 *  
 *Returns:
 *
 */
lm_status_t
lm_update_external_phy_fw_reinit( lm_device_t *pdev )
{
    lm_status_t lm_status  = LM_STATUS_SUCCESS;
    u8_t        elink_status = ELINK_STATUS_OK;    

    MM_ACQUIRE_PHY_LOCK(pdev);

    lm_reset_link(pdev);

    PHY_HW_LOCK(pdev);
    elink_status = elink_phy_init(&pdev->params.link,&pdev->vars.link);
    PHY_HW_UNLOCK(pdev);

    DbgBreakIf(ELINK_STATUS_OK != elink_status);

    // Emulation FPGA or LOOPBACK non pmf in multi vnic mode link might be up now
    lm_link_report(pdev);

    ELINK_STATUS_TO_LM_STATUS( elink_status, lm_status );
    
    if( LM_STATUS_SUCCESS == lm_status )
    {
        // in case success -reset version
        pdev->hw_info.sz_ext_phy_fw_ver[0] = '\0';
    }

    MM_RELEASE_PHY_LOCK(pdev);

    return lm_status;
}

/*
 *Function Name:lm_update_external_phy_fw_done
 *
 *Parameters:
 *
 *Description:
 *  
 *Returns:
 *
 */
lm_status_t
lm_update_external_phy_fw_done( lm_device_t *pdev )
{
    lm_status_t lm_status    = LM_STATUS_SUCCESS;
    u8_t        ext_phy_addr = 0;
    u8_t        b_exit       = FALSE; 
    
    MM_ACQUIRE_PHY_LOCK(pdev);
    switch( pdev->params.link.phy[ELINK_EXT_PHY1].type )
    {
    case PORT_HW_CFG_XGXS_EXT_PHY_TYPE_SFX7101:
        break;
    default:
        b_exit = TRUE;
        break;
    }
    if( b_exit )
    {
        MM_RELEASE_PHY_LOCK(pdev);
        return lm_status ;
    }

    ext_phy_addr = pdev->params.link.phy[ELINK_EXT_PHY1].addr;
    
    /* DSP Remove Download Mode */
    lm_gpio_write(pdev, MISC_REGISTERS_GPIO_0, MISC_REGISTERS_GPIO_LOW, PORT_ID(pdev) );
        
    PHY_HW_LOCK(pdev);
    elink_sfx7101_sp_sw_reset(pdev, &pdev->params.link.phy[ELINK_EXT_PHY1] );
    /* wait 0.5 sec to allow it to run */
    mm_wait( pdev, 500000);
    elink_ext_phy_hw_reset( pdev, PORT_ID(pdev) );
    mm_wait(pdev, 500000);    
    PHY_HW_UNLOCK(pdev);
    
    MM_RELEASE_PHY_LOCK(pdev);

    return lm_status;
}
