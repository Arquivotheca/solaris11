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
 *    02/05/07 Alon Elhanani    Inception.
 ******************************************************************************/


#include "lm5710.h"
#include "license.h"
#include "mcp_shmem.h"
#include "command.h"

// does HW statistics is active
// only if we are PMF && collect_enabled is on!
#define LM_STATS_IS_HW_ACTIVE(_pdev) ( _pdev->vars.stats.stats_collect.stats_hw.b_collect_enabled && \
                                       IS_PMF(_pdev) )

// do _cmd statement only if in SF mode
// we use this macro since in MF mode we don't maintain non-mandatory statistics so to prevent inconsistently - we don't use them at all
#define LM_STATS_DO_IF_SF(_pdev,_cmd) if( !_pdev->hw_info.mf_info.multi_vnics_mode ){ _cmd; } ;

#define LM_STATS_64_TO_HI_LO( _x_64_, _hi_lo ) ( _hi_lo##_hi = (u32_t)U64_HI( _x_64_ ) ); ( _hi_lo##_lo = (u32_t)U64_LO( _x_64_ ) );
#define LM_STATS_HI_LO_TO_64( _hi_lo, _x_64_ ) ( _x_64_ = (((u64_t)(_hi_lo##_hi) << 32) | (_hi_lo##_lo)) )

/**
 * driver stats are stored as 64bits where the lower bits store
 * the value and the upper bits store the wraparound count.
 * different stat fields are stored with different data sizes
 * and the following macros help in storing values in the
 * "overflow count" part of a 64bit value and seperating it from
 * the actual data.
  */
#define DATA_MASK(_bits) (((u64_t)-1)>>(64-_bits))
#define STATS_DATA(_bits,_val) ( (_val) & DATA_MASK(_bits) )
#define WRAPAROUND_COUNT_MASK(_bits) ( ~ DATA_MASK(_bits) )
#define HAS_WRAPPED_AROUND(_bits,_old,_new) ((STATS_DATA(_bits,_old) ) > (STATS_DATA(_bits,_new) ))
#define INC_WRAPAROUND_COUNT(_bits,_val) (_val + ( 1ull << _bits ) )

/**lm_update_wraparound_if_needed
 * This function checks the old and new values, and returns a
 * either the new data with the old wraparound count, or (if a
 * wraparound has occured) the new data with an incremented
 * wraparound count.
 *
 * val_current can be given in either little-endian or
 * big-endian byte ordering. the values returned are always in
 * host byte order.
 *
 * @param data_bits the number of data bits in the values
 * @param val_current the newly collected value. the byte
 *                    ordering is detemined by
 *                    @param b_swap_bytes
 * @param val_prev the the previously saved value in host byte
 *                 order
 * @param b_swap_bytes TRUE if val_current is byte-swapped (i.e
 *                     given as little-endian on a big-endian
 *                     machine), FALSE otherwise.
 *
 * @return u64_t the new data with an appropriate wraparound
 *         count.
 */

static u64_t lm_update_wraparound_if_needed(u8_t data_bits, u64_t val_current, u64_t val_prev, u8_t b_swap_bytes)
{
    if(b_swap_bytes)
    {
        /*We assume that only 32bit stats will ever need to be byte-swapped. this is because
          all HW data is byte-swapped by DMAE as needed, and the 64bit FW stats are swapped
          by the REGPAIR macros.*/
        DbgBreakIf(data_bits != 32);
        val_current=mm_le32_to_cpu(val_current);
    }
    if (HAS_WRAPPED_AROUND(data_bits,val_prev,val_current))
    {
        val_prev=INC_WRAPAROUND_COUNT(data_bits,val_prev);
    }
    return ((val_prev & WRAPAROUND_COUNT_MASK(data_bits)) |
            (val_current & DATA_MASK(data_bits))); /*take the overflow count we calculated, and the data from the new value*/
}

/**
 * The following macros handle the wraparound-count for FW
 * stats. Note that in the 32bit case (i.e any stats that are
 * not REGPAIRs), the bytes have to swapped if the host byte
 * order is not little-endian.
 */
#define LM_SIGN_EXTEND_VALUE_32( val_current_32, val_prev_64 ) \
    val_prev_64 = lm_update_wraparound_if_needed( 32, val_current_32, val_prev_64, CHANGE_ENDIANITY )
#define LM_SIGN_EXTEND_VALUE_36( val_current_36, val_prev_64 ) \
    val_prev_64 = lm_update_wraparound_if_needed( 36, val_current_36, val_prev_64, FALSE)
#define LM_SIGN_EXTEND_VALUE_42( val_current_42, val_prev_64 ) \
    val_prev_64 = lm_update_wraparound_if_needed( 42, val_current_42, val_prev_64, FALSE )



/* function checks if there is a pending completion for statistics and a pending dpc to handle the completion:
 * for cases where VBD gets a bit starved - we don't want to assert if chip isn't stuck and we have a pending completion
 */
u8_t is_pending_stats_completion(struct _lm_device_t * pdev);

lm_status_t lm_stats_hw_collect( struct _lm_device_t *pdev );

#ifdef _VBD_CMD_
extern volatile u32_t* g_everest_sim_flags_ptr;
#define EVEREST_SIM_STATS       0x02
#endif

/*
 * lm_edebug_if_is_stats_disabled returns TRUE if statistics gathering is
 * disabled according to edebug-driver interface implemented through SHMEM2
 * field named edebug_driver_if. Otherwise, return FALSE.
*/
static u32_t
lm_edebug_if_is_stats_disabled(struct _lm_device_t * pdev)
{
    u32_t shmem2_size;
    u32_t offset = OFFSETOF(shmem2_region_t, edebug_driver_if[1]);
    u32_t val;

    if (pdev->hw_info.shmem_base2 != 0)
    {
        LM_SHMEM2_READ(pdev, OFFSETOF(shmem2_region_t, size), &shmem2_size);

        if (shmem2_size > offset)
        {
            LM_SHMEM2_READ(pdev, offset, &val);


            if (val == EDEBUG_DRIVER_IF_OP_CODE_DISABLE_STAT)
            {
                return TRUE;
            }
        }
    }

    return FALSE;
}


static lm_status_t lm_stats_fw_post_request(lm_device_t *pdev)
{
    lm_status_t lm_status;
    lm_stats_fw_collect_t * stats_fw = &pdev->vars.stats.stats_collect.stats_fw;

    stats_fw->fw_stats_req->hdr.drv_stats_counter = mm_cpu_to_le16(stats_fw->drv_counter);

    // zero no completion counter
    stats_fw->timer_wakeup_no_completion_current = 0 ;

    stats_fw->b_completion_done = FALSE ;
    stats_fw->b_ramrod_completed = FALSE ;

    /* TODO: SRIOV?? */

    /* send FW stats ramrod */
    lm_status = lm_sq_post_entry(pdev,&(stats_fw->stats_sp_list_command),CMD_PRIORITY_HIGH);
    DbgBreakIf( LM_STATUS_SUCCESS != lm_status ) ;

    if (lm_status == LM_STATUS_SUCCESS)
    {
        // increamant ramrod counter (for debugging)
        ++stats_fw->stats_ramrod_cnt ;
    }

    return lm_status;

}
// main stats function called from timer
void lm_stats_on_timer( struct _lm_device_t * pdev )
{
    lm_status_t                         lm_status   = LM_STATUS_SUCCESS ;
    u32_t                               val         = 0 ;

    if CHK_NULL( pdev )
    {
        DbgBreakIf(!pdev) ;
        return;
    }

    ++pdev->vars.stats.stats_collect.timer_wakeup ;

#ifdef _VBD_CMD_
    val = GET_FLAGS(*g_everest_sim_flags_ptr, EVEREST_SIM_STATS);
    pdev->vars.stats.stats_collect.stats_fw.b_collect_enabled = val && pdev->vars.stats.stats_collect.stats_fw.b_collect_enabled;
#endif

    /* if stats gathering is disabled according to edebug-driver i/f - return */
    if(lm_edebug_if_is_stats_disabled(pdev))
    {
        ++pdev->vars.stats.stats_collect.shmem_disabled;
        return;
    }

    if( pdev->vars.stats.stats_collect.stats_fw.b_collect_enabled )
    {
        // verify that previous ramrod cb is finished
        if( lm_stats_fw_complete( pdev ) == LM_STATUS_BUSY)
        {
            // using a variable to have event log since the message is too long
            val = ++pdev->vars.stats.stats_collect.stats_fw.timer_wakeup_no_completion_current ;

            // update timer_wakeup_no_completion_max
            if( pdev->vars.stats.stats_collect.stats_fw.timer_wakeup_no_completion_max < val )
            {
                pdev->vars.stats.stats_collect.stats_fw.timer_wakeup_no_completion_max = val ;
            }
            /* We give up in two case:
             * 1. We got here #NO_COMPLETION times without having a stats-completion pending to be handled
             * 2. There is a completion pending to be handled - but it still hasn't been handled in #COMP_NOT_HANDLED times
             *    we got here. #COMP_NOT_HANDLED > #NO_COMPLETION*/
            if ((!is_pending_stats_completion(pdev) && (val >= MAX_STATS_TIMER_WAKEUP_NO_COMPLETION)) ||
                (val >= MAX_STATS_TIMER_WAKEUP_COMP_NOT_HANDLED))
            {
                if(GET_FLAGS(pdev->params.debug_cap_flags,DEBUG_CAP_FLAGS_STATS_FW))
                {
                    LM_TRIGGER_PCIE(pdev);
                }
                /* shutdown bug - BSOD only if shutdown is not in progress */
                if (!lm_reset_is_inprogress(pdev))
                {
                    /* BSOD */
                    if(GET_FLAGS(pdev->params.debug_cap_flags,DEBUG_CAP_FLAGS_STATS_FW))
                    {
                        DbgBreakIfAll( val >= MAX_STATS_TIMER_WAKEUP_NO_COMPLETION ) ;
                    }
                }
            }

            /* check interrupt mode on 57710A0 boards */
            lm_57710A0_dbg_intr(pdev);

            // this is total wake up no completion - for debuging
            ++pdev->vars.stats.stats_collect.stats_fw.timer_wakeup_no_completion_total ;
        }
        else
        {
            lm_status = lm_stats_fw_post_request(pdev);
            DbgBreakIf(lm_status != LM_STATUS_SUCCESS);
        }
    } // fw collect enabled

    if( LM_STATS_IS_HW_ACTIVE(pdev) )
    {
        // if link is not up - we can simply pass this call (optimization)
        if( pdev->vars.stats.stats_collect.stats_hw.b_is_link_up )
        {
            MM_ACQUIRE_PHY_LOCK_DPC(pdev);

            // we can call dmae only if link is up, and we must check it with lock
            if( pdev->vars.stats.stats_collect.stats_hw.b_is_link_up )
            {
                lm_status = lm_stats_hw_collect( pdev );

                DbgBreakIf( LM_STATUS_SUCCESS != lm_status ) ;

                // assign values for relevant mac type which is up - inside the lock due to consistecy reasons
                lm_stats_hw_assign( pdev ) ;
            }

            // assign to statistics to MCP
            lm_stats_mgmt_assign( pdev ) ;

            MM_RELEASE_PHY_LOCK_DPC(pdev);
        } // link is up
    } // LM_STATS_IS_HW_ACTIVE
    else if( pdev->vars.stats.stats_collect.stats_hw.b_collect_enabled &&
             pdev->vars.stats.stats_collect.stats_hw.b_is_link_up ) // when there is no link - no use writing to mgmt
    {
        MM_ACQUIRE_PHY_LOCK_DPC(pdev);
        lm_stats_mgmt_assign( pdev ) ;
        MM_RELEASE_PHY_LOCK_DPC(pdev);
    }
}

u8_t is_pending_stats_completion(struct _lm_device_t * pdev)
{
    volatile struct hc_sp_status_block * sp_sb=NULL;
    u32_t val=0;

    /* read interrupt mask from IGU - check that default-status-block bit is off... */
    if (INTR_BLK_TYPE(pdev)==INTR_BLK_HC){
        val = REG_RD(pdev,  HC_REG_INT_MASK + 4*PORT_ID(pdev) );
    } // TODO add IGU complement


    sp_sb = lm_get_default_status_block(pdev);

    /* check bit 0 is masked (value 0) and that cstorm in default-status-block has increased. */
    if(!GET_FLAGS(val, 1) && lm_is_eq_completion(pdev))
    {
        return TRUE;
    }
    return FALSE; /* no pending completion */
}

static void* lm_stats_hw_get_sgl_ctx(lm_device_t *pdev )
{
    if(HAS_MSTAT(pdev) || (pdev->vars.mac_type == MAC_TYPE_BMAC) )
    {
        return pdev->vars.stats.stats_collect.stats_hw.dmae_sgl_context_non_emac ;
    }
    else if(pdev->vars.mac_type == MAC_TYPE_EMAC)
    {
        return pdev->vars.stats.stats_collect.stats_hw.dmae_sgl_context_emac ;
    }
    else
    {
        DbgBreakIf((pdev->vars.mac_type != MAC_TYPE_EMAC) && (pdev->vars.mac_type != MAC_TYPE_BMAC));
        return NULL;
    }
}

/*
 *Function Name:lm_stats_dmae
 *
 *Parameters:
 *
 *Description:
 *  collect stats from hw using dmae
 *Returns:
 *
 */

lm_status_t lm_stats_dmae( lm_device_t *pdev )
{
    lm_status_t  lm_status      = LM_STATUS_SUCCESS ;
    void*        dmae_sgl_ctx   = NULL ;
    u32_t        wait_cnt       = 0 ;
    u32_t        wait_cnt_limit = 10000; // 200ms for ASIC, * vars.clk_factor FPAGA/EMUL

    if CHK_NULL( pdev )
    {
        DbgBreakIf(!pdev) ;
        return LM_STATUS_INVALID_PARAMETER ;
    }

    wait_cnt_limit*= pdev->vars.clk_factor ;

    DbgBreakIf( 0 == wait_cnt_limit );

    DbgBreakIf( FALSE == LM_STATS_IS_HW_ACTIVE( pdev ) ) ;

    dmae_sgl_ctx = lm_stats_hw_get_sgl_ctx(pdev);
    if (NULL == dmae_sgl_ctx)
    {
        lm_status = LM_STATUS_LINK_DOWN;
        DbgMessage2(pdev, WARNstat, "### lm_stats_dmae:   mac_type=%d exits with status=0x%x.\n",  pdev->vars.mac_type, lm_status );
        return lm_status ;
    }

    DbgMessage1(pdev, INFORM, "lm_stats_dmae: using ctx %x\n", dmae_sgl_ctx);

    MM_ACQUIRE_DMAE_STATS_LOCK(pdev);

    // Call go command
    lm_status = lm_dmae_sgl_go( pdev, dmae_sgl_ctx ) ;

    if( LM_STATUS_SUCCESS != lm_status )
    {
        MM_RELEASE_DMAE_STATS_LOCK(pdev);
        return lm_status ;
    }

    // busy loop on done
    // TODO_STATS - encapsulate this code with the "wait code" in lm_dmae_command() funcion
    while( LM_STATUS_SUCCESS != ( lm_status = lm_dmae_sgl_is_done(dmae_sgl_ctx) ) )
    {
        // should not happen...
        mm_wait( pdev, 20 );
        if((++wait_cnt > wait_cnt_limit) || (lm_reset_is_inprogress(pdev)))
        {
            LM_TRIGGER_PCIE(pdev) ;

            /* shutdown bug - BSOD only if shutdown is not in progress */
            if (!lm_reset_is_inprogress(pdev))
            {
                DbgBreakIfAll( 1 ) ;
                lm_status = LM_STATUS_FAILURE;
                break;
            }
            else
            {
                lm_status = LM_STATUS_SUCCESS;
                break;
            }
        }

        // in case reset in progress
        // we won't get completion so no need to wait
        if( lm_reset_is_inprogress(pdev) )
        {
            lm_status = LM_STATUS_SUCCESS;
            break;
        }
    }

    MM_RELEASE_DMAE_STATS_LOCK(pdev);
    return lm_status ;
}

/*
 *Function Name:lm_stats_clear_emac_stats
 *
 *Parameters:
 *
 *Description:
 *  resets all emac statistics counter registers
 *Returns:
 *
 */
lm_status_t lm_stats_clear_emac_stats( lm_device_t *pdev )
{
    u32_t i              = 0 ;
    u32_t j              = 0 ;
    u32_t count_limit[3] = { EMAC_REG_EMAC_RX_STAT_AC_COUNT,
                             1,
                             EMAC_REG_EMAC_TX_STAT_AC_COUNT } ;
    u32_t reg_start  [3] = { EMAC_REG_EMAC_RX_STAT_AC,
                             EMAC_REG_EMAC_RX_STAT_AC_28,
                             EMAC_REG_EMAC_TX_STAT_AC } ;
    u32_t emac_base      = 0 ;
    u32_t dummy          = 0 ;

    ASSERT_STATIC( ARRSIZE(reg_start) == ARRSIZE(count_limit) );

    if CHK_NULL( pdev )
    {
        return LM_STATUS_INVALID_PARAMETER ;
    }

    emac_base = ( 0 == PORT_ID(pdev) ) ? GRCBASE_EMAC0 : GRCBASE_EMAC1 ;

    for( i = 0; i< ARRSIZE(reg_start) ; i++ )
    {
        for( j = 0 ; j < count_limit[i]; j++ )
        {
            dummy = REG_RD( pdev, emac_base + reg_start[i]+(j*sizeof(u32_t))) ; /*Clear stats registers by reading from from ReadClear RX/RXerr/TX STAT banks*/
        }
    }
    return LM_STATUS_SUCCESS ;
}

/*
 *Function Name:lm_stats_on_update_state
 *
 *Parameters:
 *
 *Description:
 *  This function should be called on one of two occasions:
 *  - When link is down
 *  - When PMF is going down (meaning - changed to another PMF)
 *  Function must be called under PHY LOCK
 *  1. in case no link - do nothing
 *  2. make last query to hw stats for current link
 *  3. assign to mirror host structures
 *  4. assign to MCP (managment)
 *  5. saves the copy in mirror
 *Returns:
 *
 */
lm_status_t lm_stats_on_update_state(lm_device_t * pdev )
{
    lm_status_t lm_status = LM_STATUS_SUCCESS ;

    if CHK_NULL( pdev )
    {
        DbgBreakIf( !pdev ) ;
        return LM_STATUS_INVALID_PARAMETER ;
    }

    if( MAC_TYPE_NONE == pdev->vars.mac_type )
    {
        DbgMessage(pdev, WARNstat, "lm_stats_on_link_update: linking down when already linked down\n" );
        return LM_STATUS_LINK_DOWN ;
    }

    if ( LM_STATS_IS_HW_ACTIVE(pdev) )
    {
        // call statistics for the last time before link down
        lm_status = lm_stats_dmae( pdev ) ;

        if( LM_STATUS_SUCCESS != lm_status )
        {
            DbgBreakIf( LM_STATUS_SUCCESS != lm_status ) ;
        }

        // assign last values before link down
        lm_stats_hw_assign( pdev ) ;
    }

    // assign to statistics to mgmt
    lm_stats_mgmt_assign( pdev ) ;

    return lm_status;
}
// NOTE: this function must be called under PHY LOCK!
// - 1. Lock with stats timer/dmae, whcih means - no timer request on air when function running
// - 2. Last update of stats from emac/bmac (TBD - do it with reset addresses)
// - 3. keep latest stats in a copy
// - 4. if emac - reset all stats registers!
// - 5. if up - change b_link_down_is_on flag to FALSE
lm_status_t lm_stats_on_link_update( lm_device_t *pdev, const u8_t b_is_link_up )
{
    lm_status_t lm_status = LM_STATUS_SUCCESS ;

    if CHK_NULL( pdev )
    {
        DbgBreakIf( !pdev ) ;
        return LM_STATUS_INVALID_PARAMETER ;
    }

    if( FALSE == b_is_link_up ) // link down
    {
        pdev->vars.stats.stats_collect.stats_hw.b_is_link_up = FALSE ;

        if ( FALSE == LM_STATS_IS_HW_ACTIVE(pdev) )
        {
            return LM_STATUS_SUCCESS;
        }

        // get stats for the last time, assign to managment and save copy to mirror
        lm_status = lm_stats_on_update_state(pdev);

        if( LM_STATUS_SUCCESS != lm_status )
        {
            return lm_status ;
        }

        switch( pdev->vars.mac_type )
        {
        case MAC_TYPE_EMAC:
            lm_stats_clear_emac_stats( pdev ) ; // resest emac stats fields
            break;

        case MAC_TYPE_BMAC: // nothing to do - bigmac resets itself anyway
            break;

        case MAC_TYPE_UMAC: // nothing to do - mstat resets anyway
        case MAC_TYPE_XMAC:
            DbgBreakIf(!CHIP_IS_E3(pdev));
            break;

        default:
        case MAC_TYPE_NONE:
            DbgBreakMsg( "mac_type not acceptable\n" ) ;
            return LM_STATUS_INVALID_PARAMETER ;
        }

        // Set current to 0
        mm_mem_zero( &pdev->vars.stats.stats_mirror.stats_hw.macs[STATS_MACS_IDX_CURRENT],
                     sizeof(pdev->vars.stats.stats_mirror.stats_hw.macs[STATS_MACS_IDX_CURRENT]) ) ;
    }
    else
    {
        pdev->vars.stats.stats_collect.stats_hw.b_is_link_up = TRUE ;
    }

    return lm_status ;
}

/**lm_stats_alloc_hw_query
 * Allocate buffers for the MAC and NIG stats. If the chip has
 * an EMAC block, memory will be allocated for it's stats.
 * otherwise only the non-EMAC and NIG buffers will be
 * allocated. The non-EMAC buffer will be of the proper size for
 * BMAC1/BMAC2/MSTAT, as needed.
 *
 * @param pdev the pdev to initialize
 *
 * @return lm_status_t LM_STATUS_SUCCESS on success,
 *         LM_STATUS_FAILURE on failure.
 */
static lm_status_t lm_stats_alloc_hw_query(lm_device_t *pdev)
{
    lm_stats_hw_collect_t*  stats_hw                = &(pdev->vars.stats.stats_collect.stats_hw);
    u32_t                   alloc_size              = 0 ;
    u32_t                   mac_stats_alloc_size    = 0;
    lm_address_t            phys_addr               = {{0}};

    if(!HAS_MSTAT(pdev)) //MSTAT replaces EMAC/BMAC1/BMAC2 stats.
    {
        DbgMessage(NULL, INFORM, "lm_stats_alloc_hw_query: device has no MSTAT block.\n");
        // Allocate continuous memory for statistics buffers to be read from hardware. This can probably be changed to
        // allocate max(emac, bmac) instead of emac+bmac, but need to make sure there are no races in the transition from 
        // 1G link to 10G link or vice-versa
        mac_stats_alloc_size = sizeof(struct _stats_emac_query_t) + sizeof( union _stats_bmac_query_t);
        alloc_size =  mac_stats_alloc_size + sizeof( struct _stats_nig_query_t ) ;
        stats_hw->u.s.addr_emac_stats_query = mm_alloc_phys_mem(pdev, alloc_size, &phys_addr ,PHYS_MEM_TYPE_NONCACHED, LM_RESOURCE_COMMON );

        stats_hw->mac_stats_phys_addr = phys_addr;
        LM_INC64(&phys_addr, sizeof(struct _stats_emac_query_t));
        stats_hw->bmac_stats_phys_addr = phys_addr;
        LM_INC64(&phys_addr, sizeof( union _stats_bmac_query_t));
        stats_hw->nig_stats_phys_addr= phys_addr;

        DbgMessage2(NULL, INFORM, "lm_stats_alloc_hw_query: allocated a block of size %d at %x\n", alloc_size, stats_hw->u.s.addr_emac_stats_query);
        if CHK_NULL( stats_hw->u.s.addr_emac_stats_query )
        {
            DbgBreakIf(!stats_hw->u.s.addr_emac_stats_query );
            return LM_STATUS_FAILURE ;
        }

        stats_hw->u.s.addr_bmac1_stats_query = (struct _stats_bmac1_query_t*)((u8_t*)stats_hw->u.s.addr_emac_stats_query + sizeof(struct _stats_emac_query_t)) ;
        stats_hw->u.s.addr_bmac2_stats_query = (struct _stats_bmac2_query_t*)((u8_t*)stats_hw->u.s.addr_emac_stats_query + sizeof(struct _stats_emac_query_t)) ;
        stats_hw->addr_nig_stats_query   = (struct _stats_nig_query_t*)((u8_t*)stats_hw->u.s.addr_bmac1_stats_query + sizeof(union _stats_bmac_query_t)) ;
        DbgMessage3(NULL, INFORM, "lm_stats_alloc_hw_query: addr_bmac1_stats_query = %x, addr_bmac2_stats_query=%x, addr_nig_stats_query=%x\n", stats_hw->u.s.addr_bmac1_stats_query, stats_hw->u.s.addr_bmac2_stats_query, stats_hw->addr_nig_stats_query);
    }
    else
    {
        DbgMessage(NULL, INFORM, "lm_stats_alloc_hw_query: device has an MSTAT block.\n");

        mac_stats_alloc_size = sizeof(struct _stats_mstat_query_t);
        alloc_size = mac_stats_alloc_size + sizeof( struct _stats_nig_query_t );

        stats_hw->u.addr_mstat_stats_query = mm_alloc_phys_mem(pdev, alloc_size, &phys_addr ,PHYS_MEM_TYPE_NONCACHED, LM_RESOURCE_COMMON );

        stats_hw->mac_stats_phys_addr = phys_addr;
        LM_INC64(&phys_addr, mac_stats_alloc_size);
        stats_hw->nig_stats_phys_addr = phys_addr;

        DbgMessage2(NULL, INFORM, "lm_stats_alloc_hw_query: allocated a block of size %d at %x\n", alloc_size, stats_hw->u.addr_mstat_stats_query);
        if CHK_NULL( stats_hw->u.addr_mstat_stats_query )
        {
            DbgBreakIf(!stats_hw->u.addr_mstat_stats_query );
            return LM_STATUS_FAILURE ;
        }

        stats_hw->addr_nig_stats_query   = (struct _stats_nig_query_t*)((u8_t*)stats_hw->u.addr_mstat_stats_query + sizeof(struct _stats_mstat_query_t)) ;
        DbgMessage1(NULL, INFORM, "lm_stats_alloc_hw_query: stats_hw->addr_nig_stats_query=%x\n", stats_hw->addr_nig_stats_query);
    }

    return LM_STATUS_SUCCESS;
}

lm_status_t lm_stats_alloc_fw_resc (struct _lm_device_t *pdev)
{
    lm_stats_fw_collect_t * stats_fw        = &pdev->vars.stats.stats_collect.stats_fw;
    u32_t                   num_groups      = 0;
    u32_t                   alloc_size      = 0;
    u8                      num_queue_stats = 1;

    /* Total number of FW statistics requests =
     * 1 for port stats + 1 for PF stats + 1 for queue stats + 1 for toe stats */
    #define NUM_FW_STATS_REQS 4
    stats_fw->fw_stats_num = NUM_FW_STATS_REQS;

    /* TODO SRIOV PF NEEDS MORE...will add to num_queue_stats...  */

    /* Request is built from stats_query_header and an array of
     * stats_query_cmd_group each of which contains
     * STATS_QUERY_CMD_COUNT rules. The real number or requests is
     * configured in the stats_query_header.
     */
    num_groups = (stats_fw->fw_stats_num) / STATS_QUERY_CMD_COUNT +
        (((stats_fw->fw_stats_num) % STATS_QUERY_CMD_COUNT) ? 1 : 0);

    stats_fw->fw_stats_req_sz = sizeof(struct stats_query_header) +
            num_groups * sizeof(struct stats_query_cmd_group);

    /* Data for statistics requests + stats_conter
     *
     * stats_counter holds per-STORM counters that are incremented
     * when STORM has finished with the current request.
     */
    stats_fw->fw_stats_data_sz = sizeof(struct per_port_stats) +
                             sizeof(struct per_pf_stats) +
                             sizeof(struct per_queue_stats) * num_queue_stats +
                             sizeof(struct toe_stats_query) +
                             sizeof(struct stats_counter);

    alloc_size = stats_fw->fw_stats_data_sz + stats_fw->fw_stats_req_sz;
    stats_fw->fw_stats = mm_alloc_phys_mem(pdev, alloc_size, &stats_fw->fw_stats_mapping ,PHYS_MEM_TYPE_NONCACHED, LM_RESOURCE_COMMON );
    if (!stats_fw->fw_stats)
    {
        return LM_STATUS_RESOURCE;
    }
    /* Set shortcuts */
    stats_fw->fw_stats_req = (lm_stats_fw_stats_req_t *)stats_fw->fw_stats;
    stats_fw->fw_stats_req_mapping = stats_fw->fw_stats_mapping;

    stats_fw->fw_stats_data = (lm_stats_fw_stats_data_t *)
        ((u8*)stats_fw->fw_stats + stats_fw->fw_stats_req_sz);

    stats_fw->fw_stats_data_mapping = stats_fw->fw_stats_mapping;
    LM_INC64(&stats_fw->fw_stats_data_mapping, stats_fw->fw_stats_req_sz);

    return LM_STATUS_SUCCESS;
}

// allocate memory both for hw and fw statistics
lm_status_t lm_stats_alloc_resc( struct _lm_device_t* pdev )
{
    lm_variables_t* vars                = NULL ;
    u8_t            dmae_sgl_cmd_idx_0  = (u8_t)(-1) ;
    u8_t            dmae_sgl_cmd_idx_1  = (u8_t)(-1) ;
    u8_t            mm_cli_idx          = 0;
    lm_status_t     lm_status = LM_STATUS_SUCCESS;

    if CHK_NULL(pdev )
    {
        DbgBreakIf(!pdev) ;
        return LM_STATUS_INVALID_PARAMETER ;
    }

    mm_cli_idx = LM_RESOURCE_COMMON;//!!DP mm_cli_idx_to_um_idx(LM_CLI_IDX_MAX);

    vars = &(pdev->vars);

    lm_stats_alloc_fw_resc(pdev);

    lm_status = lm_stats_alloc_hw_query(pdev);
    if(lm_status != LM_STATUS_SUCCESS)
    {
        return lm_status;
    }

    // Allocate DMAE sgl stats contexts:
    // two contexts per port, one for emac reads and one for bmac reads (which can't be called together of course)
    switch( PORT_ID(pdev) )
    {
    case 0:
        dmae_sgl_cmd_idx_0 = DMAE_STATS_GET_PORT_CMD_IDX(0,0) ;
        dmae_sgl_cmd_idx_1 = DMAE_STATS_GET_PORT_CMD_IDX(0,1) ;
        break;
    case 1:
        dmae_sgl_cmd_idx_0 = DMAE_STATS_GET_PORT_CMD_IDX(1,0) ;
        dmae_sgl_cmd_idx_1 = DMAE_STATS_GET_PORT_CMD_IDX(1,1) ;
        break;
    default:
        DbgBreakIf(PORT_ID(pdev)>1) ;
        return LM_STATUS_FAILURE ;
    }

    if(!HAS_MSTAT(pdev))
    {
        DbgMessage(NULL,INFORM, "lm_stats_alloc_resc: device has no MSTAT block\n");
        vars->stats.stats_collect.stats_hw.dmae_sgl_context_emac = lm_dmae_sgl_alloc( pdev,
                                                                                      dmae_sgl_cmd_idx_0,
                                                                                      dmae_sgl_cmd_idx_1,
                                                                                      DMAE_SGL_STATS_NUM_OF_EMAC_COMMANDS ) ;
        if CHK_NULL( vars->stats.stats_collect.stats_hw.dmae_sgl_context_emac )
        {
            DbgBreakIf(!vars->stats.stats_collect.stats_hw.dmae_sgl_context_emac );
            return LM_STATUS_FAILURE ;
        }
        DbgMessage1(NULL,INFORM,"lm_stats_alloc_resc: allocated EMAC SGL at %x\n", vars->stats.stats_collect.stats_hw.dmae_sgl_context_emac);
    }
    else
    {
        vars->stats.stats_collect.stats_hw.dmae_sgl_context_emac = NULL;
    }

    vars->stats.stats_collect.stats_hw.dmae_sgl_context_non_emac = lm_dmae_sgl_alloc( pdev,
                                                                                      dmae_sgl_cmd_idx_0,
                                                                                      dmae_sgl_cmd_idx_1,
                                                                                      HAS_MSTAT(pdev)?DMAE_SGL_STATS_NUM_OF_MSTAT_COMMANDS:DMAE_SGL_STATS_NUM_OF_BIGMAC_COMMANDS ) ;

    if CHK_NULL( vars->stats.stats_collect.stats_hw.dmae_sgl_context_non_emac )
    {
        DbgBreakIf(!vars->stats.stats_collect.stats_hw.dmae_sgl_context_non_emac );
        return LM_STATUS_FAILURE ;
    }
    DbgMessage1(NULL,INFORM,"lm_stats_alloc_resc: allocated BMAC/MSTAT SGL at %x\n", vars->stats.stats_collect.stats_hw.dmae_sgl_context_non_emac);

    return LM_STATUS_SUCCESS ;
}

/**lm_stats_hw_setup_nig
 * Add the DMAE command for reading NIG stats to the non-EMAC
 * DMAE context.
 *
 * @param pdev the device to initialize
 * @param dmae_context the dmae context to initialize
 * @param cmd_seq the index of the new command in the DMAE
 *                context.
 *
 * @return lm_status_t LM_STATUS_SUCCESS on success,
 *         LM_STATUS_FAILURE on failure.
 */
static lm_status_t lm_stats_hw_setup_nig(lm_device_t* pdev, void* dmae_context, u8_t cmd_seq)
{
    lm_status_t     lm_status    = LM_STATUS_FAILURE;
    const u32_t     port         = PORT_ID(pdev) ;
    lm_address_t    phys_addr    = pdev->vars.stats.stats_collect.stats_hw.nig_stats_phys_addr;

    DbgMessage3(NULL,INFORM,"lm_stats_hw_setup_nig: phys_addr = %x, dmae_ctx = %x, cmd_seq = %x\n" , phys_addr.as_ptr, dmae_context, cmd_seq);
    lm_status = lm_dmae_sgl_set_executer_cmd_read_grc_to_pci( pdev,
                                                              dmae_context,
                                                              cmd_seq,
                                                              ( 0 == port ) ? NIG_REG_STAT0_BRB_DISCARD : NIG_REG_STAT1_BRB_DISCARD,
                                                              phys_addr,
                                                              sizeof( struct _stats_nig_query_t ) >> 2 ) ;
    if( LM_STATUS_SUCCESS != lm_status )
    {
        DbgMessage1(pdev, WARN, "lm_stats_hw_setup status=0x%x\n", lm_status );
        return lm_status ;
    }
    return LM_STATUS_SUCCESS;
}

/**The basic structure of lm_stats_hw_setup_??? is as follows:
 *  - setup the DMAE SGL for the proper DMAE context.
 *  - calculate the base registers and sizes for the DMAE
 *    transactions.
 *  - add the transactions to the DMAE context one by one,
 *    increasing the cmd_seq variable for every transaction.
 *  - setup the NIG transaction
 */

/**lm_stats_hw_setup_emac
 * setup the DMAE SGL for the EMAC stats DMAE context
 *
 * @param pdev the device to initialize
 *
 * @return lm_status_t LM_STATUS_SUCCESS on success, some other
 *         value on failure.
 */
static lm_status_t lm_stats_hw_setup_emac(  lm_device_t* pdev)
{
    const u32_t     port         = PORT_ID(pdev) ;
    u32_t           emac_base    = 0 ; // emac: GRCBASE_EMACX, reg name
    lm_status_t     lm_status    = LM_STATUS_FAILURE;
    u8_t            cmd_seq      = 0 ;
    lm_address_t    phys_addr    = pdev->vars.stats.stats_collect.stats_hw.mac_stats_phys_addr;
    void*           dmae_context = pdev->vars.stats.stats_collect.stats_hw.dmae_sgl_context_emac;

    DbgBreakIf(HAS_MSTAT(pdev));

    lm_status = lm_dmae_sgl_setup( pdev, dmae_context ) ;
    if( LM_STATUS_SUCCESS != lm_status )
    {
        DbgBreakIf(1) ;
        return lm_status ;
    }

    switch( port )
    {
    case 0:
        emac_base = GRCBASE_EMAC0 ;
        break;

    case 1:
        emac_base = GRCBASE_EMAC1 ;
        break;
    default:
        DbgBreakIf( port > 1 ) ;
        break;
    }

    cmd_seq = 0 ;

    lm_status = lm_dmae_sgl_set_executer_cmd_read_grc_to_pci(pdev,
                                                             dmae_context,
                                                             cmd_seq++,
                                                             emac_base + EMAC_REG_EMAC_RX_STAT_IFHCINOCTETS, /*EMAC_REG_EMAC_RX_STAT_AC is ReadClear RX_STAT bank*/
                                                             phys_addr,
                                                             EMAC_REG_EMAC_RX_STAT_AC_COUNT /*same 23 registers*/) ;

    if( LM_STATUS_SUCCESS != lm_status )
    {
        DbgMessage1(pdev, WARNstat, "lm_stats_hw_setup status=0x%x\n", lm_status );
        return lm_status ;
    }

    // advance from emac.stats_rx begin to emac.stats_rx_err begin
    LM_INC64( &phys_addr, sizeof( pdev->vars.stats.stats_collect.stats_hw.u.s.addr_emac_stats_query->stats_rx ) ) ;
    lm_status = lm_dmae_sgl_set_executer_cmd_read_grc_to_pci(pdev,
                                                             dmae_context,
                                                             cmd_seq++,
                                                             emac_base + EMAC_REG_EMAC_RX_STAT_FALSECARRIERERRORS, /*EMAC_REG_EMAC_RX_STAT_AC_28 is ReadClear reg*/
                                                             phys_addr,
                                                             1 ) ;

    if( LM_STATUS_SUCCESS != lm_status )
    {
        DbgMessage1(pdev, WARNstat, "lm_stats_hw_setup status=0x%x\n", lm_status );
        return lm_status ;
    }

    // advance from emac.stats_rx_err begin to emac.stats_tx begin
    LM_INC64( &phys_addr, sizeof( pdev->vars.stats.stats_collect.stats_hw.u.s.addr_emac_stats_query->stats_rx_err ) ) ;
    lm_status = lm_dmae_sgl_set_executer_cmd_read_grc_to_pci( pdev,
                                                              dmae_context,
                                                              cmd_seq++,
                                                              emac_base + EMAC_REG_EMAC_TX_STAT_IFHCOUTOCTETS, /*EMAC_REG_EMAC_TX_STAT_AC is ReadClear TX STAT bank*/
                                                              phys_addr,
                                                              EMAC_REG_EMAC_TX_STAT_AC_COUNT /* same 22 registers */) ;

    if( LM_STATUS_SUCCESS != lm_status )
    {
        DbgMessage1(pdev, WARNstat, "lm_stats_hw_setup status=0x%x\n", lm_status );
        return lm_status ;
    }

    lm_status = lm_stats_hw_setup_nig(pdev, dmae_context, cmd_seq++);
    if( LM_STATUS_SUCCESS != lm_status )
    {
        DbgMessage1(pdev, WARNstat, "lm_stats_hw_setup_emac status=0x%x\n", lm_status );
        return lm_status ;
    }

    DbgBreakIf( cmd_seq > DMAE_SGL_STATS_NUM_OF_EMAC_COMMANDS );

    return lm_status;
}

/**lm_stats_hw_setup_non_emac
 * Setup the DMAE SGL for the non-EMAC stats DMAE context. This
 * function assumes that the MAC statistics themselves can be
 * read with 2 DMAE transactions.
 *
 *
 * @param pdev the device to initialize
 * @param grc_base the base GRC address of the required stats
 *                 block (e.g NIG_REG_INGRESS_BMAC0_MEM or
 *                 GRCBASE_MSTAT0)
 * @param block1_start offset of the first register in the first
 *                     transaction.
 * @param block1_size size (in bytes) of the first DMAE
 *                    transaction.
 * @param block2_start offset of the first register in the
 *                     second transaction.
 * @param block2_size size (in bytes) of the second DMAE
 *                    transaction.
 * @param cmd_seq receives the number of DMAE transactions that
 *                were added to the DMAE context.
 *
 * @return lm_status_t LM_STATUS_SUCCESS on success, some other
 *         value on failure.
 */
static lm_status_t lm_stats_hw_setup_non_emac(  lm_device_t* pdev,
                                                u32_t grc_base,
                                                u32_t block1_start, u32_t block1_size,
                                                u32_t block2_start, u32_t block2_size)
{
    lm_status_t             lm_status       = LM_STATUS_FAILURE;
    lm_stats_hw_collect_t*  stats_hw        = &pdev->vars.stats.stats_collect.stats_hw;
    lm_address_t            phys_addr       = stats_hw->bmac_stats_phys_addr;
    void*                   dmae_context    = stats_hw->dmae_sgl_context_non_emac;
    u8_t                    cmd_seq         = 0;

    DbgMessage4(pdev, INFORM, "lm_stats_hw_setup_mstat: block1_start=%x, block1_size=%x, block2_start=%x, block2_size=%x\n",block1_start,block1_size,block2_start, block2_size);

    lm_status = lm_dmae_sgl_setup( pdev, stats_hw->dmae_sgl_context_non_emac ) ;
    if( LM_STATUS_SUCCESS != lm_status )
    {
        DbgBreakIf(1) ;
        return lm_status ;
    }

    DbgMessage3(pdev, INFORM, "lm_stats_hw_setup_non_emac_dmae_ctx: grc_base=%x, cmd_seq=%d, phys_addr=%x\n",grc_base,cmd_seq, phys_addr.as_ptr);
    lm_status = lm_dmae_sgl_set_executer_cmd_read_grc_to_pci( pdev,
                                                              dmae_context,
                                                              cmd_seq++,
                                                              grc_base + block1_start,
                                                              phys_addr,
                                                              block1_size>>2 ) ;
    if( LM_STATUS_SUCCESS != lm_status )
    {
        DbgMessage1(pdev, WARN, "lm_stats_hw_setup_non_emac_dmae_ctx: block1 setup, status=0x%x\n", lm_status );
        return lm_status ;
    }

    LM_INC64( &phys_addr, block1_size ) ;
    DbgMessage2(pdev, WARN, "lm_stats_hw_setup_non_emac_dmae_ctx: cmd_seq=%d, phys_addr=%x\n",cmd_seq, phys_addr.as_ptr);
    lm_status = lm_dmae_sgl_set_executer_cmd_read_grc_to_pci( pdev,
                                                              dmae_context,
                                                              cmd_seq++,
                                                              grc_base + block2_start,
                                                              phys_addr,
                                                              block2_size>>2 ) ;

    if( LM_STATUS_SUCCESS != lm_status )
    {
        DbgMessage1(pdev, WARN, "lm_stats_hw_setup_non_emac_dmae_ctx: block2 setup, status=0x%x\n", lm_status );
        return lm_status ;
    }

    lm_status = lm_stats_hw_setup_nig(pdev, dmae_context, cmd_seq++);
    if( LM_STATUS_SUCCESS != lm_status )
    {
        DbgMessage1(pdev, WARNstat, "lm_stats_hw_setup_non_emac status=0x%x\n", lm_status );
        return lm_status ;
    }

    ASSERT_STATIC(DMAE_SGL_STATS_NUM_OF_MSTAT_COMMANDS == DMAE_SGL_STATS_NUM_OF_BIGMAC_COMMANDS);
    DbgBreakIf( cmd_seq > DMAE_SGL_STATS_NUM_OF_MSTAT_COMMANDS );

    return lm_status;
}

/**lm_stats_hw_setup_bmac
 * Setup the BMAC1/BMAC2 stats DMAE transactions.
 * @see lm_stats_hw_setup_non_emac for more details.
 *
 * @param pdev the device to initialize.
 *
 * @return lm_status_t LM_STATUS_SUCCESS on success, some other
 *         value on failure.
 */
static lm_status_t lm_stats_hw_setup_bmac(lm_device_t* pdev)
{
    const u32_t     port         = PORT_ID(pdev) ;
    u32_t           bmac_base    = 0 ; // bmac: GRCBASE_NIG, bmac_base + reg name
                                       // nig :GRCBASE_NIG, reg name (NIG_XXX)
    u32_t           bmac_tx_start_reg, bmac_rx_start_reg;
    u32_t           bmac_tx_stat_size, bmac_rx_stat_size;
    lm_status_t     lm_status = LM_STATUS_FAILURE;

    DbgBreakIf(HAS_MSTAT(pdev));

    switch( port )
    {
    case 0:
        bmac_base = NIG_REG_INGRESS_BMAC0_MEM ;
        break;

    case 1:
        bmac_base = NIG_REG_INGRESS_BMAC1_MEM;

        if (!CHIP_IS_E1x(pdev)) {
            DbgMessage(pdev, INFORMi, "BMAC stats should never be collected on port 1 of E2!\n");
            bmac_base = NIG_REG_INGRESS_BMAC0_MEM;
        }
        break;

    default:
        DbgBreakIf( port > 1 ) ;
        break;

    }

    if (CHIP_IS_E1x(pdev)) {
        bmac_tx_start_reg = BIGMAC_REGISTER_TX_STAT_GTPKT;
        bmac_rx_start_reg = BIGMAC_REGISTER_RX_STAT_GR64;
        bmac_tx_stat_size = sizeof(pdev->vars.stats.stats_collect.stats_hw.u.s.addr_bmac1_stats_query->stats_tx);
        bmac_rx_stat_size = sizeof(pdev->vars.stats.stats_collect.stats_hw.u.s.addr_bmac1_stats_query->stats_rx);
    } else {
        bmac_tx_start_reg = BIGMAC2_REGISTER_TX_STAT_GTPOK;
        bmac_rx_start_reg = BIGMAC2_REGISTER_RX_STAT_GR64;
        bmac_tx_stat_size = sizeof(pdev->vars.stats.stats_collect.stats_hw.u.s.addr_bmac2_stats_query->stats_tx);
        bmac_rx_stat_size = sizeof(pdev->vars.stats.stats_collect.stats_hw.u.s.addr_bmac2_stats_query->stats_rx);
    }

    lm_status = lm_stats_hw_setup_non_emac(pdev, bmac_base, bmac_tx_start_reg, bmac_tx_stat_size, bmac_rx_start_reg, bmac_rx_stat_size);

    return lm_status;
}

/**lm_stats_hw_setup_mstat
 * Setup the MSTAT stats DMAE transactions.
 * @see lm_stats_hw_setup_non_emac for more details.
 *
 * @param pdev the device to initialize.
 *
 * @return lm_status_t LM_STATUS_SUCCESS on success, some other
 *         value on failure.
 */
static lm_status_t lm_stats_hw_setup_mstat(lm_device_t* pdev)
{
    const u32_t     port         = PORT_ID(pdev) ;
    u32_t           mstat_base   = 0;
    u32_t           mstat_tx_start, mstat_tx_size;
    u32_t           mstat_rx_start, mstat_rx_size;
    lm_status_t     lm_status    = LM_STATUS_FAILURE;
    lm_stats_hw_collect_t* stats_hw = &pdev->vars.stats.stats_collect.stats_hw;

    DbgBreakIf(!HAS_MSTAT(pdev));

    mstat_tx_start = MSTAT_REG_TX_STAT_GTXPOK_LO;
    mstat_tx_size = sizeof(stats_hw->u.addr_mstat_stats_query->stats_tx);

    mstat_rx_start = MSTAT_REG_RX_STAT_GR64_LO;
    mstat_rx_size = sizeof(stats_hw->u.addr_mstat_stats_query->stats_rx);

    DbgMessage4(pdev, INFORM, "lm_stats_hw_setup_mstat: mstat_tx_start=%x, mstat_tx_size=%x, mstat_rx_start=%x, mstat_rx_size=%x\n",mstat_tx_start,mstat_tx_size,mstat_rx_start, mstat_rx_size);

    switch(port)
    {
    case 0:
        mstat_base = GRCBASE_MSTAT0;
        break;
    case 1:
        mstat_base = GRCBASE_MSTAT1;
        break;
    default:
        DbgBreakIf( port > 1 ) ;
        break;
    }

    lm_status = lm_stats_hw_setup_non_emac(pdev, mstat_base, mstat_tx_start, mstat_tx_size, mstat_rx_start, mstat_rx_size);

    return lm_status;
}

/* Description:
*    setups resources regarding hw stats (init fields)
*    set offsets serials of hw reads, either from EMAC & BIGMAC or from MSTAT block
*/
lm_status_t lm_stats_hw_setup(struct _lm_device_t *pdev)
{
    lm_status_t lm_status           = LM_STATUS_SUCCESS ;
    /* enable hw collect with mstat only if it's not fpga and not a 4-domain emulation compile... */
    u8_t        b_enable_collect    = HAS_MSTAT(pdev)? ((CHIP_REV_IS_EMUL(pdev) && (CHIP_BONDING(pdev) == 0)) || CHIP_REV_IS_ASIC(pdev)) : TRUE;

    if(HAS_MSTAT(pdev))
    {
        lm_status = lm_stats_hw_setup_mstat(pdev);
        if(lm_status != LM_STATUS_SUCCESS)
        {
            DbgMessage(NULL, FATAL, "Failed to initialize MSTAT statistics\n");
            return lm_status;
        }
    }
    else
    {
        lm_status = lm_stats_hw_setup_emac(pdev);
        if(lm_status != LM_STATUS_SUCCESS)
        {
            DbgMessage(NULL,FATAL,"Failed to initialize EMAC statistics\n");
            return lm_status;
        }
        lm_status = lm_stats_hw_setup_bmac(pdev);
        if(lm_status != LM_STATUS_SUCCESS)
        {
            DbgMessage(NULL, FATAL, "Failed to initialize BMAC statistics\n");
            return lm_status;
        }
    }

    pdev->vars.stats.stats_collect.stats_hw.b_is_link_up = FALSE;

    pdev->vars.stats.stats_collect.stats_hw.b_collect_enabled = b_enable_collect ; // HW stats are not supported on E3 FPGA.

    return lm_status ;
} /* lm_stats_hw_setup */

/**
 * This function will prepare the statistics ramrod data the way
 * we will only have to increment the statistics counter and
 * send the ramrod each time we have to.
 *
 * @param pdev
 */
static void lm_stats_prep_fw_stats_req(lm_device_t *pdev)
{
    lm_stats_fw_collect_t     *stats_fw        = &pdev->vars.stats.stats_collect.stats_fw;
    struct stats_query_header *stats_hdr       = &stats_fw->fw_stats_req->hdr;
    lm_address_t              cur_data_offset  = {{0}};
    struct stats_query_entry  *cur_query_entry = NULL;

    stats_hdr->cmd_num           = stats_fw->fw_stats_num;
    stats_hdr->drv_stats_counter = 0;

    /* storm_counters struct contains the counters of completed
     * statistics requests per storm which are incremented by FW
     * each time it completes hadning a statistics ramrod. We will
     * check these counters in the timer handler and discard a
     * (statistics) ramrod completion.
     */
    cur_data_offset = stats_fw->fw_stats_data_mapping;
    LM_INC64(&cur_data_offset, OFFSETOF(lm_stats_fw_stats_data_t, storm_counters));

    stats_hdr->stats_counters_addrs.hi = mm_cpu_to_le32(cur_data_offset.as_u32.high);
    stats_hdr->stats_counters_addrs.lo = mm_cpu_to_le32(cur_data_offset.as_u32.low);

    /* prepare to the first stats ramrod (will be completed with
     * the counters equal to zero) - init counters to somethig different.
     */
    mm_memset(&stats_fw->fw_stats_data->storm_counters, 0xff, sizeof(stats_fw->fw_stats_data->storm_counters) );

    /**** Port FW statistics data ****/
    cur_data_offset = stats_fw->fw_stats_data_mapping;
    LM_INC64(&cur_data_offset, OFFSETOF(lm_stats_fw_stats_data_t, port));

    cur_query_entry = &stats_fw->fw_stats_req->query[LM_STATS_PORT_QUERY_IDX];

    cur_query_entry->kind       = STATS_TYPE_PORT;
    /* For port query index is a DONT CARE */
    cur_query_entry->index      = PORT_ID(pdev);
    cur_query_entry->funcID     = mm_cpu_to_le16(FUNC_ID(pdev));;
    cur_query_entry->address.hi = mm_cpu_to_le32(cur_data_offset.as_u32.high);
    cur_query_entry->address.lo = mm_cpu_to_le32(cur_data_offset.as_u32.low);

    /**** PF FW statistics data ****/
    cur_data_offset = stats_fw->fw_stats_data_mapping;
    LM_INC64(&cur_data_offset, OFFSETOF(lm_stats_fw_stats_data_t, pf));

    cur_query_entry = &stats_fw->fw_stats_req->query[LM_STATS_PF_QUERY_IDX];

    cur_query_entry->kind       = STATS_TYPE_PF;
    /* For PF query index is a DONT CARE */
    cur_query_entry->index      = PORT_ID(pdev);
    cur_query_entry->funcID     = mm_cpu_to_le16(FUNC_ID(pdev));
    cur_query_entry->address.hi = mm_cpu_to_le32(cur_data_offset.as_u32.high);
    cur_query_entry->address.lo = mm_cpu_to_le32(cur_data_offset.as_u32.low);

    /**** Toe query  ****/
    cur_data_offset = stats_fw->fw_stats_data_mapping;
    LM_INC64(&cur_data_offset, OFFSETOF(lm_stats_fw_stats_data_t, toe));

    ASSERT_STATIC(LM_STATS_TOE_IDX<ARRSIZE(stats_fw->fw_stats_req->query));
    cur_query_entry = &stats_fw->fw_stats_req->query[LM_STATS_TOE_IDX];

    cur_query_entry->kind       = STATS_TYPE_TOE;
    cur_query_entry->index      = LM_STATS_CNT_ID(pdev);
    cur_query_entry->funcID     = mm_cpu_to_le16(FUNC_ID(pdev));
    cur_query_entry->address.hi = mm_cpu_to_le32(cur_data_offset.as_u32.high);
    cur_query_entry->address.lo = mm_cpu_to_le32(cur_data_offset.as_u32.low);

    /**** Clients' queries ****/
    cur_data_offset = stats_fw->fw_stats_data_mapping;
    LM_INC64(&cur_data_offset, OFFSETOF(lm_stats_fw_stats_data_t, queue_stats));

    ASSERT_STATIC(LM_STATS_FIRST_QUEUE_QUERY_IDX < ARRSIZE(stats_fw->fw_stats_req->query));
    cur_query_entry = &stats_fw->fw_stats_req->query[LM_STATS_FIRST_QUEUE_QUERY_IDX];

    cur_query_entry->kind       = STATS_TYPE_QUEUE;
    cur_query_entry->index      = LM_STATS_CNT_ID(pdev);
    cur_query_entry->funcID     = mm_cpu_to_le16(FUNC_ID(pdev));
    cur_query_entry->address.hi = mm_cpu_to_le32(cur_data_offset.as_u32.high);
    cur_query_entry->address.lo = mm_cpu_to_le32(cur_data_offset.as_u32.low);
    /* TODO : VF! more stats? */
}
/* Description:
*    setups fw statistics parameters
*/
void lm_stats_fw_setup(struct _lm_device_t *pdev)
{
    lm_stats_fw_collect_t * stats_fw = &pdev->vars.stats.stats_collect.stats_fw;
    stats_fw->b_completion_done      = TRUE ; // reset flag to initial value
    stats_fw->b_ramrod_completed     = TRUE ;
    stats_fw->drv_counter            = 0 ;
    stats_fw->b_collect_enabled      = pdev->params.fw_stats_init_value ; // change to TRUE in order to enable fw stats

    pdev->vars.stats.stats_collect.b_last_called  = TRUE ;

    /* Prepare the constatnt slow-path command (For stats we don't allocate a new one each time) */
    lm_sq_post_fill_entry(pdev,
                          &(stats_fw->stats_sp_list_command),
                          0 /* cid: Don't care */,
                          RAMROD_CMD_ID_COMMON_STAT_QUERY,
                          NONE_CONNECTION_TYPE,
                          stats_fw->fw_stats_req_mapping.as_u64,
                          FALSE /* don't release sp mem*/);

    /* Prepare the FW stats ramrod request structure (can do this just once) */
    lm_stats_prep_fw_stats_req(pdev);
}
/*
 *------------------------------------------------------------------------
 * lm_stats_fw_check_update_done -
 *
 * check done flags and update flags
 *
 *------------------------------------------------------------------------
 */
void lm_stats_fw_check_update_done( struct _lm_device_t *pdev, OUT u32_t* ptr_stats_flags_done )
{
    if CHK_NULL( ptr_stats_flags_done )
    {
        DbgBreakIf(!ptr_stats_flags_done) ;
        return;
    }

    // For each storm still wasn't done, we check and if done - set, so next time
    // we won't need to check again

    // eth xstorm
    if( 0 == GET_FLAGS(*ptr_stats_flags_done, LM_STATS_FLAG_XSTORM ) )
    {
        if( LM_STATS_VERIFY_COUNTER( pdev, fw_stats_data->storm_counters.xstats_counter ) )
        {
            SET_FLAGS(*ptr_stats_flags_done,LM_STATS_FLAG_XSTORM ) ;
        }
    }

    // eth tstorm
    if( 0 == GET_FLAGS(*ptr_stats_flags_done, LM_STATS_FLAG_TSTORM ) )
    {
        if( LM_STATS_VERIFY_COUNTER( pdev, fw_stats_data->storm_counters.tstats_counter ) )
        {
            SET_FLAGS(*ptr_stats_flags_done,LM_STATS_FLAG_TSTORM ) ;
        }
    }

    // eth ustorm
    if( 0 == GET_FLAGS(*ptr_stats_flags_done, LM_STATS_FLAG_USTORM ) )
    {
        if( LM_STATS_VERIFY_COUNTER( pdev, fw_stats_data->storm_counters.ustats_counter ) )
        {
            SET_FLAGS(*ptr_stats_flags_done,LM_STATS_FLAG_USTORM ) ;
        }
    }

    // eth cstorm
    if( 0 == GET_FLAGS(*ptr_stats_flags_done, LM_STATS_FLAG_CSTORM ) )
    {
        if( LM_STATS_VERIFY_COUNTER( pdev, fw_stats_data->storm_counters.cstats_counter ) )
        {
            SET_FLAGS(*ptr_stats_flags_done,LM_STATS_FLAG_CSTORM ) ;
        }
    }

}

/**
 * @Desription: Checks if FW completed last statistic update, if
 *            it did it assigns the statistics
 *
 * @param pdev
 *
 * @return lm_status_t LM_STATUS_SUCCESS if FW has completed
 *         LM_STATUS_BUSY if it hasn't yet completed
 */
lm_status_t lm_stats_fw_complete( struct _lm_device_t *pdev  )
{
    u32_t stats_flags_done      = 0 ; // bit wise for storms done flags are on
    u32_t stats_flags_assigned  = 0 ; // bit wise for already assigned values from storms
    lm_status_t lm_status             = LM_STATUS_SUCCESS;

    if CHK_NULL( pdev )
    {
        DbgBreakIf( !pdev ) ;
        return LM_STATUS_INVALID_PARAMETER;
    }

    /* First check if the ramrod has completed, if it hasn't don't bother checking
     * dma completion  yet, we need both of them to complete before sending another
     * ramrod. */
    if (FALSE == pdev->vars.stats.stats_collect.stats_fw.b_ramrod_completed)
    {
        lm_status = LM_STATUS_BUSY;
    } 
    else if (FALSE == pdev->vars.stats.stats_collect.stats_fw.b_completion_done)
    {

        // check done flags and update the falg if there was a change
        lm_stats_fw_check_update_done( pdev, &stats_flags_done ) ;

        // Check if we can assign any of the storms
        if ( LM_STATS_DO_ASSIGN_ANY( stats_flags_done, stats_flags_assigned) )
        {
            // assign stats that are ready
            lm_stats_fw_assign( pdev, stats_flags_done, &stats_flags_assigned ) ;
        }

        // did all storms were assigned
        if ERR_IF( LM_STATS_FLAGS_ALL != stats_flags_assigned  )
        {
            lm_status = LM_STATUS_BUSY;
        }
        else
        {
            ++pdev->vars.stats.stats_collect.stats_fw.drv_counter ;

            // barrier (for IA64) is to assure that the counter will be incremented BEFORE
            // the complation_done flag is set to TRUE.
            // in order to assure correct drv_counter sent to fw in lm_stats_on_timer (CQ48772)

            mm_write_barrier();
            // now we can notify timer that cb is done!
            pdev->vars.stats.stats_collect.stats_fw.b_completion_done = TRUE ;
            lm_status = LM_STATUS_SUCCESS;
        }
    }
    return lm_status;
}

/*
 *------------------------------------------------------------------------
 * lm_stats_fw_assign -
 *
 * assign values from fw shared memory to the lm structs
 *
 *------------------------------------------------------------------------
 */
void lm_stats_fw_assign( struct _lm_device_t *pdev, u32_t stats_flags_done, u32_t* ptr_stats_flags_assigned )
{
    const u8_t cli_id       = LM_CLI_IDX_NDIS ;
    int        arr_cnt      = 0 ;
    u8_t       i            = 0 ;

    if CHK_NULL( ptr_stats_flags_assigned )
    {
        DbgBreakIf(!ptr_stats_flags_assigned) ;
        return;
    }

// assign reg_pair fw collected into fw mirror
#define LM_STATS_FW_ASSIGN_TOE_REGPAIR(field_name) \
        REGPAIR_TO_U64(pdev->vars.stats.stats_mirror.stats_fw.toe_##field_name, \
        pdev->vars.stats.stats_collect.stats_fw.fw_stats_data->toe.field_name ) ;

// assign u32 fw collected into fw mirror + do sign extension
#define LM_STATS_FW_ASSIGN_TOE_U32(field_name) \
        LM_SIGN_EXTEND_VALUE_32( pdev->vars.stats.stats_collect.stats_fw.fw_stats_data->toe.field_name, \
        pdev->vars.stats.stats_mirror.stats_fw.toe_##field_name ) ;


    // eth xstorm
    if( LM_STATS_DO_ASSIGN( stats_flags_done, *ptr_stats_flags_assigned, LM_STATS_FLAG_XSTORM ) )
    {
        // regpairs
        REGPAIR_TO_U64(pdev->vars.stats.stats_mirror.stats_fw.eth_xstorm_common.client_statistics[cli_id].unicast_bytes_sent,
                       pdev->vars.stats.stats_collect.stats_fw.fw_stats_data->queue_stats.xstorm_queue_statistics.ucast_bytes_sent);
        // regpairs
        REGPAIR_TO_U64(pdev->vars.stats.stats_mirror.stats_fw.eth_xstorm_common.client_statistics[cli_id].multicast_bytes_sent,
                       pdev->vars.stats.stats_collect.stats_fw.fw_stats_data->queue_stats.xstorm_queue_statistics.mcast_bytes_sent);

        // regpairs
        REGPAIR_TO_U64(pdev->vars.stats.stats_mirror.stats_fw.eth_xstorm_common.client_statistics[cli_id].broadcast_bytes_sent,
                       pdev->vars.stats.stats_collect.stats_fw.fw_stats_data->queue_stats.xstorm_queue_statistics.bcast_bytes_sent);

        pdev->vars.stats.stats_mirror.stats_fw.eth_xstorm_common.client_statistics[cli_id].total_sent_bytes =
            pdev->vars.stats.stats_mirror.stats_fw.eth_xstorm_common.client_statistics[cli_id].unicast_bytes_sent +
            pdev->vars.stats.stats_mirror.stats_fw.eth_xstorm_common.client_statistics[cli_id].multicast_bytes_sent +
            pdev->vars.stats.stats_mirror.stats_fw.eth_xstorm_common.client_statistics[cli_id].broadcast_bytes_sent;

        // non regpairs
        LM_SIGN_EXTEND_VALUE_32( pdev->vars.stats.stats_collect.stats_fw.fw_stats_data->queue_stats.xstorm_queue_statistics.ucast_pkts_sent,
                                 pdev->vars.stats.stats_mirror.stats_fw.eth_xstorm_common.client_statistics[cli_id].unicast_pkts_sent );

        LM_SIGN_EXTEND_VALUE_32( pdev->vars.stats.stats_collect.stats_fw.fw_stats_data->queue_stats.xstorm_queue_statistics.mcast_pkts_sent,
                                 pdev->vars.stats.stats_mirror.stats_fw.eth_xstorm_common.client_statistics[cli_id].multicast_pkts_sent );

        LM_SIGN_EXTEND_VALUE_32( pdev->vars.stats.stats_collect.stats_fw.fw_stats_data->queue_stats.xstorm_queue_statistics.bcast_pkts_sent,
                                 pdev->vars.stats.stats_mirror.stats_fw.eth_xstorm_common.client_statistics[cli_id].broadcast_pkts_sent );

        LM_SIGN_EXTEND_VALUE_32( pdev->vars.stats.stats_collect.stats_fw.fw_stats_data->queue_stats.xstorm_queue_statistics.error_drop_pkts,
                                 pdev->vars.stats.stats_mirror.stats_fw.eth_xstorm_common.client_statistics[cli_id].error_drop_pkts );

        pdev->vars.stats.stats_mirror.stats_fw.eth_xstorm_common.client_statistics[cli_id].total_sent_pkts =
            pdev->vars.stats.stats_mirror.stats_fw.eth_xstorm_common.client_statistics[cli_id].unicast_pkts_sent+
            pdev->vars.stats.stats_mirror.stats_fw.eth_xstorm_common.client_statistics[cli_id].multicast_pkts_sent +
            pdev->vars.stats.stats_mirror.stats_fw.eth_xstorm_common.client_statistics[cli_id].broadcast_pkts_sent;



        /* TOE Stats for Xstorm */
        arr_cnt = ARRSIZE(pdev->vars.stats.stats_mirror.stats_fw.toe_xstorm_toe.statistics) ;
        for ( i = 0; i < arr_cnt; i++)
        {
            LM_STATS_FW_ASSIGN_TOE_U32(xstorm_toe.statistics[i].tcp_out_segments) ;
            LM_STATS_FW_ASSIGN_TOE_U32(xstorm_toe.statistics[i].tcp_retransmitted_segments) ;
            LM_STATS_FW_ASSIGN_TOE_REGPAIR(xstorm_toe.statistics[i].ip_out_octets ) ;
            LM_STATS_FW_ASSIGN_TOE_U32(xstorm_toe.statistics[i].ip_out_requests) ;
        }

        SET_FLAGS( *ptr_stats_flags_assigned, LM_STATS_FLAG_XSTORM ) ;
    }

    // eth tstorm
    if( LM_STATS_DO_ASSIGN( stats_flags_done, *ptr_stats_flags_assigned, LM_STATS_FLAG_TSTORM ) )
    {
        // regpairs
        REGPAIR_TO_U64(pdev->vars.stats.stats_mirror.stats_fw.eth_tstorm_common.client_statistics[cli_id].rcv_unicast_bytes,
                       pdev->vars.stats.stats_collect.stats_fw.fw_stats_data->queue_stats.tstorm_queue_statistics.rcv_ucast_bytes );

        REGPAIR_TO_U64(pdev->vars.stats.stats_mirror.stats_fw.eth_tstorm_common.client_statistics[cli_id].rcv_broadcast_bytes,
                       pdev->vars.stats.stats_collect.stats_fw.fw_stats_data->queue_stats.tstorm_queue_statistics.rcv_bcast_bytes );

        REGPAIR_TO_U64(pdev->vars.stats.stats_mirror.stats_fw.eth_tstorm_common.client_statistics[cli_id].rcv_multicast_bytes,
                       pdev->vars.stats.stats_collect.stats_fw.fw_stats_data->queue_stats.tstorm_queue_statistics.rcv_mcast_bytes );

        // FIXME REGPAIR_TO_U64(pdev->vars.stats.stats_mirror.stats_fw.eth_tstorm_common.client_statistics[cli_id].rcv_error_bytes,
        //               pdev->vars.stats.stats_collect.stats_fw.addr_eth_stats_query->tstorm_common.client_statistics[cnt_id].rcv_error_bytes );

        // eth tstorm - non regpairs
        LM_SIGN_EXTEND_VALUE_32( pdev->vars.stats.stats_collect.stats_fw.fw_stats_data->queue_stats.tstorm_queue_statistics.checksum_discard,
                                 pdev->vars.stats.stats_mirror.stats_fw.eth_tstorm_common.client_statistics[cli_id].checksum_discard );

        LM_SIGN_EXTEND_VALUE_32( pdev->vars.stats.stats_collect.stats_fw.fw_stats_data->queue_stats.tstorm_queue_statistics.pkts_too_big_discard,
                                 pdev->vars.stats.stats_mirror.stats_fw.eth_tstorm_common.client_statistics[cli_id].packets_too_big_discard );

        LM_SIGN_EXTEND_VALUE_32( pdev->vars.stats.stats_collect.stats_fw.fw_stats_data->queue_stats.tstorm_queue_statistics.rcv_ucast_pkts,
                                 pdev->vars.stats.stats_mirror.stats_fw.eth_tstorm_common.client_statistics[cli_id].rcv_unicast_pkts );

        LM_SIGN_EXTEND_VALUE_32( pdev->vars.stats.stats_collect.stats_fw.fw_stats_data->queue_stats.tstorm_queue_statistics.rcv_bcast_pkts,
                                 pdev->vars.stats.stats_mirror.stats_fw.eth_tstorm_common.client_statistics[cli_id].rcv_broadcast_pkts );

        LM_SIGN_EXTEND_VALUE_32( pdev->vars.stats.stats_collect.stats_fw.fw_stats_data->queue_stats.tstorm_queue_statistics.rcv_mcast_pkts,
                                 pdev->vars.stats.stats_mirror.stats_fw.eth_tstorm_common.client_statistics[cli_id].rcv_multicast_pkts );

        LM_SIGN_EXTEND_VALUE_32( pdev->vars.stats.stats_collect.stats_fw.fw_stats_data->queue_stats.tstorm_queue_statistics.no_buff_discard,
                                 pdev->vars.stats.stats_mirror.stats_fw.eth_tstorm_common.client_statistics[cli_id].no_buff_discard );

        LM_SIGN_EXTEND_VALUE_32( pdev->vars.stats.stats_collect.stats_fw.fw_stats_data->queue_stats.tstorm_queue_statistics.ttl0_discard,
                                 pdev->vars.stats.stats_mirror.stats_fw.eth_tstorm_common.client_statistics[cli_id].ttl0_discard );



        LM_SIGN_EXTEND_VALUE_32( pdev->vars.stats.stats_collect.stats_fw.fw_stats_data->port.tstorm_port_statistics.mf_tag_discard,
                                 pdev->vars.stats.stats_mirror.stats_fw.eth_tstorm_common.client_statistics[cli_id].ttl0_discard );


        /* Port Statistics */
        LM_SIGN_EXTEND_VALUE_32( pdev->vars.stats.stats_collect.stats_fw.fw_stats_data->port.tstorm_port_statistics.mac_filter_discard, \
                                 pdev->vars.stats.stats_mirror.stats_fw.eth_tstorm_common.port_statistics.mac_filter_discard ) ;
        LM_SIGN_EXTEND_VALUE_32( pdev->vars.stats.stats_collect.stats_fw.fw_stats_data->port.tstorm_port_statistics.mf_tag_discard, \
                                  pdev->vars.stats.stats_mirror.stats_fw.eth_tstorm_common.port_statistics.xxoverflow_discard ) ;
        LM_SIGN_EXTEND_VALUE_32( pdev->vars.stats.stats_collect.stats_fw.fw_stats_data->port.tstorm_port_statistics.brb_truncate_discard, \
                                 pdev->vars.stats.stats_mirror.stats_fw.eth_tstorm_common.port_statistics.brb_truncate_discard ) ;
        LM_SIGN_EXTEND_VALUE_32( pdev->vars.stats.stats_collect.stats_fw.fw_stats_data->port.tstorm_port_statistics.mac_discard, \
                                 pdev->vars.stats.stats_mirror.stats_fw.eth_tstorm_common.port_statistics.mac_discard ) ;


        // toe tstorm
        arr_cnt = ARRSIZE(pdev->vars.stats.stats_mirror.stats_fw.toe_tstorm_toe.statistics) ;
        for ( i = 0; i < arr_cnt; i++)
        {
            LM_STATS_FW_ASSIGN_TOE_U32(tstorm_toe.statistics[i].ip_in_receives) ;
            LM_STATS_FW_ASSIGN_TOE_U32(tstorm_toe.statistics[i].ip_in_delivers) ;
            LM_STATS_FW_ASSIGN_TOE_REGPAIR(tstorm_toe.statistics[i].ip_in_octets) ;
            LM_STATS_FW_ASSIGN_TOE_U32(tstorm_toe.statistics[i].tcp_in_errors) ;
            LM_STATS_FW_ASSIGN_TOE_U32(tstorm_toe.statistics[i].ip_in_header_errors) ;
            LM_STATS_FW_ASSIGN_TOE_U32(tstorm_toe.statistics[i].ip_in_discards) ;
            LM_STATS_FW_ASSIGN_TOE_U32(tstorm_toe.statistics[i].ip_in_truncated_packets) ;
        }


        SET_FLAGS( *ptr_stats_flags_assigned, LM_STATS_FLAG_TSTORM ) ;
    }

    // eth ustorm
    if( LM_STATS_DO_ASSIGN( stats_flags_done, *ptr_stats_flags_assigned, LM_STATS_FLAG_USTORM ) )
    {
        // regpairs
        REGPAIR_TO_U64(pdev->vars.stats.stats_mirror.stats_fw.eth_ustorm_common.client_statistics[cli_id].ucast_no_buff_bytes,
                       pdev->vars.stats.stats_collect.stats_fw.fw_stats_data->queue_stats.ustorm_queue_statistics.ucast_no_buff_bytes );

        REGPAIR_TO_U64(pdev->vars.stats.stats_mirror.stats_fw.eth_ustorm_common.client_statistics[cli_id].mcast_no_buff_bytes,
                       pdev->vars.stats.stats_collect.stats_fw.fw_stats_data->queue_stats.ustorm_queue_statistics.mcast_no_buff_bytes );

        REGPAIR_TO_U64(pdev->vars.stats.stats_mirror.stats_fw.eth_ustorm_common.client_statistics[cli_id].bcast_no_buff_bytes,
                       pdev->vars.stats.stats_collect.stats_fw.fw_stats_data->queue_stats.ustorm_queue_statistics.bcast_no_buff_bytes );

        // non regpairs
        LM_SIGN_EXTEND_VALUE_32( pdev->vars.stats.stats_collect.stats_fw.fw_stats_data->queue_stats.ustorm_queue_statistics.ucast_no_buff_pkts,
                                 pdev->vars.stats.stats_mirror.stats_fw.eth_ustorm_common.client_statistics[cli_id].ucast_no_buff_pkts );

        LM_SIGN_EXTEND_VALUE_32( pdev->vars.stats.stats_collect.stats_fw.fw_stats_data->queue_stats.ustorm_queue_statistics.mcast_no_buff_pkts,
                                 pdev->vars.stats.stats_mirror.stats_fw.eth_ustorm_common.client_statistics[cli_id].mcast_no_buff_pkts );

        LM_SIGN_EXTEND_VALUE_32( pdev->vars.stats.stats_collect.stats_fw.fw_stats_data->queue_stats.ustorm_queue_statistics.bcast_no_buff_pkts,
                                 pdev->vars.stats.stats_mirror.stats_fw.eth_ustorm_common.client_statistics[cli_id].bcast_no_buff_pkts );

        SET_FLAGS( *ptr_stats_flags_assigned, LM_STATS_FLAG_USTORM ) ;
    }

    if( LM_STATS_DO_ASSIGN( stats_flags_done, *ptr_stats_flags_assigned, LM_STATS_FLAG_CSTORM ) )
    {
        // toe cstorm

        LM_STATS_FW_ASSIGN_TOE_U32(cstorm_toe.no_tx_cqes) ;
        SET_FLAGS( *ptr_stats_flags_assigned, LM_STATS_FLAG_CSTORM ) ;

    }



}

/**lm_stats_hw_macs_assign
 *
 * THIS FUNCTION MUST BE CALLED INSIDE PHY LOCK
 *
 * The mirrored statistics store 2 copies of the MAC stats:
 * CURRENT and TOTAL. the reason for this is that each PF has
 * it's own MAC and when a PMF change occures,  the new PMF
 * would start with all MAC stats equal to 0. in this case
 * CURRENT would be zeroed on the next collection, but TOTAL
 * would still have the old stats.
 * because of this, TOTAL is updated according to the difference
 * between the old value and the new value.
 *
 * the following function updates a field in the CURRENT block
 * and returns the value to be added to the TOTAL block
 *
 * @param bits the number of data bits in the field
 * @param field_collect_val the value collected from the HW
 * @param field_mirror_val a pointer to the relevant field in
 *                         the CURRENT block
 *
 * @return the difference between the new value and the old
 *         value - this should be added to the relevant field in
 *         the TOTAL block.
 *
 * @see stats_macs_idx_t , lm_stats_hw_t
 */
static u64_t lm_stats_hw_macs_assign(IN lm_device_t* pdev,
                                     IN u8_t bits,
                                     IN u64_t field_collect_val,
                                     IN OUT u64_t *field_mirror_val)
{
    /*MSTAT has no wraparound logic, and it's stat values are zeroed on each read.
      This means that what we read is the difference in the stats since the last read,
      so we should just update the counters and exit.
      EMAC and BMAC stats have wraparound logic and are not zeroed on read, so we handle
      the wraparound if needed and return the difference between the old value and the
      new value.*/
    if(HAS_MSTAT(pdev))
    {
        *field_mirror_val += field_collect_val;
        return field_collect_val;
    }
    else
    {
    u64_t prev = *field_mirror_val;
    *field_mirror_val = lm_update_wraparound_if_needed(bits, field_collect_val, *field_mirror_val,FALSE/*no need to swap bytes on HW stats*/) ;
    return *field_mirror_val - prev;
    }
}

#define LM_STATS_HW_MAC_ASSIGN(field_collect, field_mirror, field_width)\
    if (mac_query->field_collect != 0) DbgMessage4(pdev, INFORM,"assigning %s[=%x] to %s, width %d.\n", #field_collect, mac_query->field_collect, #field_mirror, field_width );\
    macs[STATS_MACS_IDX_TOTAL].field_mirror += lm_stats_hw_macs_assign( pdev, \
                                                                        field_width, \
                                                 mac_query->field_collect, \
                                                 &(macs[STATS_MACS_IDX_CURRENT].field_mirror) ) ;

#define LM_STATS_HW_MAC_ASSIGN_U32( field_collect, field_mirror ) LM_STATS_HW_MAC_ASSIGN(field_collect, field_mirror, 32)

#define LM_STATS_HW_MAC_ASSIGN_U36( field_collect, field_mirror ) LM_STATS_HW_MAC_ASSIGN(field_collect, field_mirror, 36)

#define LM_STATS_HW_MAC_ASSIGN_U42( field_collect, field_mirror ) LM_STATS_HW_MAC_ASSIGN(field_collect, field_mirror, 42)


// assign a block (emac/bmac) uXX hw collected into hw mirror + do sign extension (width is XX)
#define LM_STATS_HW_NIG_ASSIGN_UXX(bits, block_name,field_collect,field_mirror) \
                                   LM_SIGN_EXTEND_VALUE_##bits( pdev->vars.stats.stats_collect.stats_hw.addr_##block_name##_stats_query->field_collect, \
                                   pdev->vars.stats.stats_mirror.stats_hw.nig.field_mirror ) ;

#define LM_STATS_HW_NIG_ASSIGN_U32(block_name,field_collect,field_mirror) LM_STATS_HW_NIG_ASSIGN_UXX(32, block_name,field_collect,field_mirror)


/* The code below is duplicated for bmac1, bmac2 and mstat, the structure mac_query differs between them and therefore
 * needs to be done this way (to avoid duplicating the code) */
#define LM_STATS_NON_EMAC_ASSIGN_CODE(_field_width) \
{\
    /* Maps bmac_query into macs sturct */ \
    /* Spec .1-5 (N/A) */ \
    /* Spec .6 */ \
    if (!IS_MULTI_VNIC(pdev)) { \
        LM_STATS_HW_MAC_ASSIGN( stats_tx.tx_gtgca, stats_tx.tx_stat_ifhcoutucastpkts_bmac_bca, _field_width); \
        LM_STATS_HW_MAC_ASSIGN( stats_tx.tx_gtgca, stats_tx.tx_stat_ifhcoutbroadcastpkts, _field_width); \
        LM_STATS_HW_MAC_ASSIGN( stats_tx.tx_gtpkt, stats_tx.tx_stat_ifhcoutucastpkts_bmac_pkt , _field_width); \
        LM_STATS_HW_MAC_ASSIGN( stats_tx.tx_gtmca, stats_tx.tx_stat_ifhcoutucastpkts_bmac_mca , _field_width); \
        /* Spec .7 */ \
        LM_STATS_HW_MAC_ASSIGN( stats_tx.tx_gtmca, stats_tx.tx_stat_ifhcoutmulticastpkts , _field_width); \
        /* Spec .8  */ \
    } \
    /* Spec .9 */ \
    LM_STATS_HW_MAC_ASSIGN( stats_rx.rx_grfcs, stats_rx.rx_stat_dot3statsfcserrors, _field_width); \
    /* Spec .10-11 (N/A) */ \
    /* Spec .12 */ \
    /* Spec .13 */ \
    LM_STATS_HW_MAC_ASSIGN( stats_rx.rx_grovr, stats_rx.rx_stat_dot3statsframestoolong, _field_width); \
    /* Spec .14 (N/A) */ \
    /* Spec .15 */ \
    LM_STATS_HW_MAC_ASSIGN( stats_rx.rx_grxpf, stats_rx.rx_stat_xoffpauseframesreceived, _field_width); \
    /* Spec .17 */ \
    LM_STATS_HW_MAC_ASSIGN( stats_tx.tx_gtxpf, stats_tx.tx_stat_outxoffsent, _field_width); \
    /* Spec .18-21 (N/A) */ \
    /* Spec .22 */ \
    LM_STATS_HW_MAC_ASSIGN( stats_rx.rx_grxpf, stats_rx.rx_stat_maccontrolframesreceived_bmac_xpf, _field_width); \
    LM_STATS_HW_MAC_ASSIGN( stats_rx.rx_grxcf, stats_rx.rx_stat_maccontrolframesreceived_bmac_xcf, _field_width); \
    /* Spec .23-29 (N/A) */ \
    /* Spec. 30 */ \
    LM_STATS_HW_MAC_ASSIGN( stats_tx.tx_gt64, stats_tx.tx_stat_etherstatspkts64octets, _field_width); \
    /* Spec. 31 */ \
    LM_STATS_HW_MAC_ASSIGN( stats_tx.tx_gt127, stats_tx.tx_stat_etherstatspkts65octetsto127octets, _field_width); \
    /* Spec. 32 */ \
    LM_STATS_HW_MAC_ASSIGN( stats_tx.tx_gt255, stats_tx.tx_stat_etherstatspkts128octetsto255octets, _field_width); \
    /* Spec. 33 */ \
    LM_STATS_HW_MAC_ASSIGN( stats_tx.tx_gt511, stats_tx.tx_stat_etherstatspkts256octetsto511octets, _field_width); \
    /* Spec. 34 */ \
    LM_STATS_HW_MAC_ASSIGN( stats_tx.tx_gt1023, stats_tx.tx_stat_etherstatspkts512octetsto1023octets, _field_width); \
    /* Spec. 35                                                   */ \
    LM_STATS_HW_MAC_ASSIGN( stats_tx.tx_gt1518, stats_tx.tx_stat_etherstatspkts1024octetsto1522octet, _field_width); \
    /* Spec. 36 */ \
    LM_STATS_HW_MAC_ASSIGN( stats_tx.tx_gt2047,  stats_tx.tx_stat_etherstatspktsover1522octets_bmac_2047, _field_width); \
    LM_STATS_HW_MAC_ASSIGN( stats_tx.tx_gt4095,  stats_tx.tx_stat_etherstatspktsover1522octets_bmac_4095, _field_width); \
    LM_STATS_HW_MAC_ASSIGN( stats_tx.tx_gt9216,  stats_tx.tx_stat_etherstatspktsover1522octets_bmac_9216, _field_width); \
    LM_STATS_HW_MAC_ASSIGN( stats_tx.tx_gt16383, stats_tx.tx_stat_etherstatspktsover1522octets_bmac_16383, _field_width);\
    /* Spec. 38 */ \
    /* Spec. 39 */ \
    /* Spec. 40 (N/A) */ \
    /* Spec. 41 */ \
    LM_STATS_HW_MAC_ASSIGN( stats_tx.tx_gterr, stats_tx.tx_stat_dot3statsinternalmactransmiterrors, _field_width); \
    /* Spec. 42 (N/A) */ \
    /* Spec. 43 */ \
    LM_STATS_HW_MAC_ASSIGN( stats_tx.tx_gtxpf, stats_tx.tx_stat_flowcontroldone, _field_width); \
    /* Spec. 44 */ \
    LM_STATS_HW_MAC_ASSIGN( stats_rx.rx_grxpf, stats_rx.rx_stat_xoffstateentered, _field_width); \
    /* Spec. 45 */ \
    /* Spec. 46 (N/A) */ \
    /* Spec. 47 */ \
    LM_STATS_HW_MAC_ASSIGN( stats_tx.tx_gtufl, stats_tx.tx_stat_ifhcoutdiscards, _field_width); \
}

//Assign the registers that do not exist in MSTAT or have a different size and therefore can't
//be a part of LM_STATS_NON_EMAC_ASSIGN_CODE
#define LM_STATS_BMAC_ASSIGN_CODE \
{ \
    LM_STATS_HW_MAC_ASSIGN_U42( stats_rx.rx_grund, stats_rx.rx_stat_etherstatsundersizepkts ) ; \
    LM_STATS_HW_MAC_ASSIGN_U36( stats_rx.rx_grjbr, stats_rx.rx_stat_etherstatsjabbers ) ; \
    LM_STATS_HW_MAC_ASSIGN_U42( stats_rx.rx_grfrg, stats_rx.rx_stat_etherstatsfragments ) ; \
    LM_STATS_HW_MAC_ASSIGN_U42( stats_rx.rx_grerb, stats_rx.rx_stat_ifhcinbadoctets ); \
}

//Assign the registers that do not exist in BMAC1/BMAC2 or have a different size and therefore
//can't be a part of LM_STATS_NON_EMAC_ASSIGN_CODE.
//Also, some fields are read from EMAC stats on devices that have an EMAC block but must be read
//from MSTAT on devices that don't have one.
#define LM_STATS_MSTAT_ASSIGN_CODE \
{ \
    LM_STATS_HW_MAC_ASSIGN( stats_rx.rx_grund, stats_rx.rx_stat_etherstatsundersizepkts, 39) ; \
    LM_STATS_HW_MAC_ASSIGN( stats_rx.rx_grfrg, stats_rx.rx_stat_etherstatsfragments, 39) ; \
    LM_STATS_HW_MAC_ASSIGN( stats_rx.rx_grerb, stats_rx.rx_stat_ifhcinbadoctets, 45); \
    if (!IS_MULTI_VNIC(pdev)) {\
        LM_STATS_HW_MAC_ASSIGN(stats_rx.rx_grbyt, stats_rx.rx_stat_ifhcinoctets, 45);\
        LM_STATS_HW_MAC_ASSIGN(stats_rx.rx_gruca, stats_rx.rx_stat_ifhcinucastpkts, 39)\
        LM_STATS_HW_MAC_ASSIGN(stats_rx.rx_grmca, stats_rx.rx_stat_ifhcinmulticastpkts, 39);\
        LM_STATS_HW_MAC_ASSIGN(stats_rx.rx_grbca, stats_rx.rx_stat_ifhcinbroadcastpkts, 39);\
        LM_STATS_HW_MAC_ASSIGN(stats_rx.rx_gr64, stats_rx.rx_stat_etherstatspkts64octets, 39);\
        LM_STATS_HW_MAC_ASSIGN(stats_rx.rx_gr127, stats_rx.rx_stat_etherstatspkts65octetsto127octets, 39);\
        LM_STATS_HW_MAC_ASSIGN(stats_rx.rx_gr255, stats_rx.rx_stat_etherstatspkts128octetsto255octets, 39);\
        LM_STATS_HW_MAC_ASSIGN(stats_rx.rx_gr511, stats_rx.rx_stat_etherstatspkts256octetsto511octets, 39);\
        LM_STATS_HW_MAC_ASSIGN(stats_rx.rx_gr1023, stats_rx.rx_stat_etherstatspkts512octetsto1023octets, 39);\
        LM_STATS_HW_MAC_ASSIGN(stats_rx.rx_gr1518, stats_rx.rx_stat_etherstatspkts1024octetsto1522octets, 39);\
        LM_STATS_HW_MAC_ASSIGN(stats_rx.rx_gr2047, stats_rx.rx_stat_etherstatspktsover1522octets, 39);\
    }\
}

/**lm_stats_hw_emac_assign
 * Copy the stats data from the BMAC1 stats values to the
 * generic struct used by the driver. This function must be
 * called after lm_stats_hw_collect that copies the data from
 * the hardware registers to the host's memory.
 *
 *
 * @param pdev the device to use.
 */
void lm_stats_hw_bmac1_assign( struct _lm_device_t *pdev)
{
    /* Macros required for macros used in this code */
    stats_macs_t *macs = &pdev->vars.stats.stats_mirror.stats_hw.macs[STATS_MACS_IDX_CURRENT];
    volatile struct _stats_bmac1_query_t *mac_query = pdev->vars.stats.stats_collect.stats_hw.u.s.addr_bmac1_stats_query;

    LM_STATS_NON_EMAC_ASSIGN_CODE(36)
    LM_STATS_BMAC_ASSIGN_CODE
}

/**lm_stats_hw_emac_assign
 * Copy the stats data from the BMAC2 stats values to the
 * generic struct used by the driver. This function must be
 * called after lm_stats_hw_collect that copies the data from
 * the hardware registers to the host's memory.
 *
 *
 * @param pdev the device to use.
 */
void lm_stats_hw_bmac2_assign( struct _lm_device_t *pdev)
{
    stats_macs_t *macs = &pdev->vars.stats.stats_mirror.stats_hw.macs[STATS_MACS_IDX_CURRENT];
    volatile struct _stats_bmac2_query_t *mac_query = pdev->vars.stats.stats_collect.stats_hw.u.s.addr_bmac2_stats_query;

    DbgBreakIf(mac_query == NULL);

    LM_STATS_NON_EMAC_ASSIGN_CODE(36)
    LM_STATS_BMAC_ASSIGN_CODE
}

/**lm_stats_hw_emac_assign
 * Copy the stats data from the MSTAT stats values to the
 * generic struct used by the driver. This function must be
 * called after lm_stats_hw_collect that copies the data from
 * the hardware registers to the host's memory.
 *
 *
 * @param pdev the device to use.
 */
void lm_stats_hw_mstat_assign( lm_device_t* pdev)
{
    stats_macs_t *macs = &pdev->vars.stats.stats_mirror.stats_hw.macs[STATS_MACS_IDX_CURRENT];
    volatile struct _stats_mstat_query_t *mac_query = pdev->vars.stats.stats_collect.stats_hw.u.addr_mstat_stats_query;

    DbgBreakIf(mac_query == NULL);

    DbgMessage1(pdev, INFORM, "lm_stats_hw_mstat_assign: mac_query=%x\n", mac_query);

    LM_STATS_NON_EMAC_ASSIGN_CODE(39)
    LM_STATS_MSTAT_ASSIGN_CODE
}

/**lm_stats_hw_emac_assign
 * Copy the stats data from the EMAC stats values to the generic
 * struct used by the driver. This function must be called after
 * lm_stats_hw_collect that copies the data from the hardware
 * registers to the host's memory.
 *
 *
 * @param pdev the device to use.
 */
void lm_stats_hw_emac_assign( struct _lm_device_t *pdev)
{
    stats_macs_t *macs = &pdev->vars.stats.stats_mirror.stats_hw.macs[STATS_MACS_IDX_CURRENT];
    volatile struct _stats_emac_query_t *mac_query = pdev->vars.stats.stats_collect.stats_hw.u.s.addr_emac_stats_query;

    DbgBreakIf(mac_query == NULL);

    LM_STATS_HW_MAC_ASSIGN_U32(stats_rx.rx_stat_ifhcinbadoctets, stats_rx.rx_stat_ifhcinbadoctets ) ;
    LM_STATS_HW_MAC_ASSIGN_U32(stats_rx.rx_stat_etherstatsfragments, stats_rx.rx_stat_etherstatsfragments ) ;

    if (!IS_MULTI_VNIC(pdev)) {
        LM_STATS_HW_MAC_ASSIGN_U32(stats_rx.rx_stat_ifhcinoctets, stats_rx.rx_stat_ifhcinoctets );
        LM_STATS_HW_MAC_ASSIGN_U32(stats_rx.rx_stat_ifhcinucastpkts, stats_rx.rx_stat_ifhcinucastpkts )
        LM_STATS_HW_MAC_ASSIGN_U32(stats_rx.rx_stat_ifhcinmulticastpkts, stats_rx.rx_stat_ifhcinmulticastpkts );
        LM_STATS_HW_MAC_ASSIGN_U32(stats_rx.rx_stat_ifhcinbroadcastpkts, stats_rx.rx_stat_ifhcinbroadcastpkts );
        LM_STATS_HW_MAC_ASSIGN_U32(stats_rx.rx_stat_etherstatspkts64octets, stats_rx.rx_stat_etherstatspkts64octets );
        LM_STATS_HW_MAC_ASSIGN_U32(stats_rx.rx_stat_etherstatspkts65octetsto127octets, stats_rx.rx_stat_etherstatspkts65octetsto127octets );
        LM_STATS_HW_MAC_ASSIGN_U32(stats_rx.rx_stat_etherstatspkts128octetsto255octets, stats_rx.rx_stat_etherstatspkts128octetsto255octets );
        LM_STATS_HW_MAC_ASSIGN_U32(stats_rx.rx_stat_etherstatspkts256octetsto511octets, stats_rx.rx_stat_etherstatspkts256octetsto511octets );
        LM_STATS_HW_MAC_ASSIGN_U32(stats_rx.rx_stat_etherstatspkts512octetsto1023octets, stats_rx.rx_stat_etherstatspkts512octetsto1023octets);
        LM_STATS_HW_MAC_ASSIGN_U32(stats_rx.rx_stat_etherstatspkts1024octetsto1522octets, stats_rx.rx_stat_etherstatspkts1024octetsto1522octets);
        LM_STATS_HW_MAC_ASSIGN_U32(stats_rx.rx_stat_etherstatspktsover1522octets, stats_rx.rx_stat_etherstatspktsover1522octets);
        LM_STATS_HW_MAC_ASSIGN_U32(stats_tx.tx_stat_ifhcoutoctets, stats_tx.tx_stat_ifhcoutoctets);
        LM_STATS_HW_MAC_ASSIGN_U32(stats_tx.tx_stat_ifhcoutucastpkts, stats_tx.tx_stat_ifhcoutucastpkts);
        LM_STATS_HW_MAC_ASSIGN_U32(stats_tx.tx_stat_ifhcoutmulticastpkts, stats_tx.tx_stat_ifhcoutmulticastpkts);
        LM_STATS_HW_MAC_ASSIGN_U32(stats_tx.tx_stat_ifhcoutbroadcastpkts, stats_tx.tx_stat_ifhcoutbroadcastpkts);
    }

    LM_STATS_HW_MAC_ASSIGN_U32(stats_rx.rx_stat_dot3statsfcserrors, stats_rx.rx_stat_dot3statsfcserrors ) ;
    LM_STATS_HW_MAC_ASSIGN_U32(stats_rx.rx_stat_dot3statsalignmenterrors, stats_rx.rx_stat_dot3statsalignmenterrors ) ;
    LM_STATS_HW_MAC_ASSIGN_U32(stats_rx.rx_stat_dot3statscarriersenseerrors, stats_rx.rx_stat_dot3statscarriersenseerrors ) ;
    LM_STATS_HW_MAC_ASSIGN_U32(stats_rx.rx_stat_xonpauseframesreceived, stats_rx.rx_stat_xonpauseframesreceived ) ;
    LM_STATS_HW_MAC_ASSIGN_U32(stats_rx.rx_stat_xoffpauseframesreceived, stats_rx.rx_stat_xoffpauseframesreceived ) ;
    LM_STATS_HW_MAC_ASSIGN_U32(stats_rx.rx_stat_maccontrolframesreceived, stats_rx.rx_stat_maccontrolframesreceived ) ;
    LM_STATS_HW_MAC_ASSIGN_U32(stats_rx.rx_stat_xoffstateentered, stats_rx.rx_stat_xoffstateentered ) ;
    LM_STATS_HW_MAC_ASSIGN_U32(stats_rx.rx_stat_dot3statsframestoolong, stats_rx.rx_stat_dot3statsframestoolong ) ;
    LM_STATS_HW_MAC_ASSIGN_U32(stats_rx.rx_stat_etherstatsjabbers, stats_rx.rx_stat_etherstatsjabbers ) ;
    LM_STATS_HW_MAC_ASSIGN_U32(stats_rx.rx_stat_etherstatsundersizepkts, stats_rx.rx_stat_etherstatsundersizepkts ) ;


    LM_STATS_HW_MAC_ASSIGN_U32(stats_rx_err.rx_stat_falsecarriererrors, stats_rx_err.rx_stat_falsecarriererrors ) ;



    LM_STATS_HW_MAC_ASSIGN_U32(stats_tx.tx_stat_ifhcoutbadoctets, stats_tx.tx_stat_ifhcoutbadoctets ) ;
    LM_STATS_HW_MAC_ASSIGN_U32(stats_tx.tx_stat_etherstatscollisions, stats_tx.tx_stat_etherstatscollisions ) ;
    LM_STATS_HW_MAC_ASSIGN_U32(stats_tx.tx_stat_outxonsent, stats_tx.tx_stat_outxonsent ) ;
    LM_STATS_HW_MAC_ASSIGN_U32(stats_tx.tx_stat_outxoffsent, stats_tx.tx_stat_outxoffsent ) ;
    LM_STATS_HW_MAC_ASSIGN_U32(stats_tx.tx_stat_flowcontroldone, stats_tx.tx_stat_flowcontroldone ) ;
    LM_STATS_HW_MAC_ASSIGN_U32(stats_tx.tx_stat_dot3statssinglecollisionframes, stats_tx.tx_stat_dot3statssinglecollisionframes ) ;
    LM_STATS_HW_MAC_ASSIGN_U32(stats_tx.tx_stat_dot3statsmultiplecollisionframes, stats_tx.tx_stat_dot3statsmultiplecollisionframes ) ;
    LM_STATS_HW_MAC_ASSIGN_U32(stats_tx.tx_stat_dot3statsdeferredtransmissions, stats_tx.tx_stat_dot3statsdeferredtransmissions ) ;
    LM_STATS_HW_MAC_ASSIGN_U32(stats_tx.tx_stat_dot3statsexcessivecollisions, stats_tx.tx_stat_dot3statsexcessivecollisions ) ;
    LM_STATS_HW_MAC_ASSIGN_U32(stats_tx.tx_stat_dot3statslatecollisions, stats_tx.tx_stat_dot3statslatecollisions ) ;


    LM_STATS_HW_MAC_ASSIGN_U32(stats_tx.tx_stat_etherstatspkts64octets, stats_tx.tx_stat_etherstatspkts64octets ) ;
    LM_STATS_HW_MAC_ASSIGN_U32(stats_tx.tx_stat_etherstatspkts65octetsto127octets, stats_tx.tx_stat_etherstatspkts65octetsto127octets ) ;
    LM_STATS_HW_MAC_ASSIGN_U32(stats_tx.tx_stat_etherstatspkts128octetsto255octets, stats_tx.tx_stat_etherstatspkts128octetsto255octets ) ;
    LM_STATS_HW_MAC_ASSIGN_U32(stats_tx.tx_stat_etherstatspkts256octetsto511octets, stats_tx.tx_stat_etherstatspkts256octetsto511octets ) ;
    LM_STATS_HW_MAC_ASSIGN_U32(stats_tx.tx_stat_etherstatspkts512octetsto1023octets, stats_tx.tx_stat_etherstatspkts512octetsto1023octets ) ;
    LM_STATS_HW_MAC_ASSIGN_U32(stats_tx.tx_stat_etherstatspkts1024octetsto1522octet, stats_tx.tx_stat_etherstatspkts1024octetsto1522octet ) ;
    LM_STATS_HW_MAC_ASSIGN_U32(stats_tx.tx_stat_etherstatspktsover1522octets, stats_tx.tx_stat_etherstatspktsover1522octets ) ;
    LM_STATS_HW_MAC_ASSIGN_U32(stats_tx.tx_stat_dot3statsinternalmactransmiterrors, stats_tx.tx_stat_dot3statsinternalmactransmiterrors ) ;
}

void lm_stats_hw_assign( struct _lm_device_t *pdev )
{
    if(HAS_MSTAT(pdev))
    {
        DbgMessage(pdev, INFORM, "lm_stats_hw_assign: device has MSTAT block.\n");
        lm_stats_hw_mstat_assign(pdev);
    }
    else if (CHIP_IS_E2(pdev) && (pdev->vars.mac_type == MAC_TYPE_BMAC))
    {
        lm_stats_hw_bmac2_assign(pdev);
    }
    else if (pdev->vars.mac_type == MAC_TYPE_BMAC)
    {
        lm_stats_hw_bmac1_assign(pdev);
    }
    else if(pdev->vars.mac_type == MAC_TYPE_EMAC)
    {
        lm_stats_hw_emac_assign(pdev);
    }
    else
    {
        DbgBreakIf((pdev->vars.mac_type != MAC_TYPE_EMAC) && (pdev->vars.mac_type == MAC_TYPE_BMAC) && !HAS_MSTAT(pdev) );
    }

    //nig
    {
       LM_STATS_HW_NIG_ASSIGN_U32(nig, brb_discard,       brb_discard       ) ;
       if (!IS_MULTI_VNIC(pdev))
       {
           LM_STATS_HW_NIG_ASSIGN_U32(nig, brb_packet,        brb_packet        );
           LM_STATS_HW_NIG_ASSIGN_U32(nig, brb_truncate,      brb_truncate      );
           LM_STATS_HW_NIG_ASSIGN_U32(nig, flow_ctrl_discard, flow_ctrl_discard );
           LM_STATS_HW_NIG_ASSIGN_U32(nig, flow_ctrl_octets,  flow_ctrl_octets  );
           LM_STATS_HW_NIG_ASSIGN_U32(nig, flow_ctrl_packet,  flow_ctrl_packet  );
           LM_STATS_HW_NIG_ASSIGN_U32(nig, mng_discard,       mng_discard       );
           LM_STATS_HW_NIG_ASSIGN_U32(nig, mng_octet_inp,     mng_octet_inp     );
           LM_STATS_HW_NIG_ASSIGN_U32(nig, mng_octet_out,     mng_octet_out     );
           LM_STATS_HW_NIG_ASSIGN_U32(nig, mng_packet_inp,    mng_packet_inp    );
           LM_STATS_HW_NIG_ASSIGN_U32(nig, mng_packet_out,    mng_packet_out    );
           LM_STATS_HW_NIG_ASSIGN_U32(nig, pbf_octets,        pbf_octets        );
           LM_STATS_HW_NIG_ASSIGN_U32(nig, pbf_packet,        pbf_packet        );
           LM_STATS_HW_NIG_ASSIGN_U32(nig, safc_inp,          safc_inp          );
       }
       if(HAS_MSTAT(pdev))//E3 has no NIG-ex registers, so we use values from MSTAT instead.
       {
           //Note: this must occur after the other HW stats have been assigned.
           stats_macs_t* assigned_hw_stats = &pdev->vars.stats.stats_mirror.stats_hw.macs[STATS_MACS_IDX_TOTAL];
           struct _stats_nig_ex_t* nig_ex_stats = &pdev->vars.stats.stats_collect.stats_hw.nig_ex_stats_query;
           /*NIG pkt0 counts packets with sizes 1024-1522 bytes. MSTAT has an equivalent register.*/
           nig_ex_stats->egress_mac_pkt0 = assigned_hw_stats->stats_tx.tx_stat_etherstatspkts1024octetsto1522octet;
           /*NIG pkt1 counts packets of size 1523 and up. We sum the required MSTAT values to get the right result.
             Note that the field names are somewhat misleading, since they don't count sizes 1522-XXXX but [1522-2047],[2048-4095],[4096-9216],[9217-14383]
             (see MSTAT low level design document).
             */
           nig_ex_stats->egress_mac_pkt1 =  assigned_hw_stats->stats_tx.tx_stat_etherstatspktsover1522octets_bmac_2047+
                                            assigned_hw_stats->stats_tx.tx_stat_etherstatspktsover1522octets_bmac_4095+
                                            assigned_hw_stats->stats_tx.tx_stat_etherstatspktsover1522octets_bmac_9216+
                                            assigned_hw_stats->stats_tx.tx_stat_etherstatspktsover1522octets_bmac_16383;
       }
       else
       {
       LM_SIGN_EXTEND_VALUE_36( pdev->vars.stats.stats_collect.stats_hw.nig_ex_stats_query.egress_mac_pkt0, pdev->vars.stats.stats_mirror.stats_hw.nig_ex.egress_mac_pkt0 ) ;
       LM_SIGN_EXTEND_VALUE_36( pdev->vars.stats.stats_collect.stats_hw.nig_ex_stats_query.egress_mac_pkt1, pdev->vars.stats.stats_mirror.stats_hw.nig_ex.egress_mac_pkt1 ) ;
    }
    }
}

// resets mirror fw statistics
void lm_stats_fw_reset( struct _lm_device_t* pdev)
{
     if CHK_NULL( pdev )
     {
         DbgBreakIf(!pdev) ;
     }
     mm_memset( &pdev->vars.stats.stats_mirror.stats_fw, 0, sizeof(pdev->vars.stats.stats_mirror.stats_fw) ) ;
}

void lm_stats_get_driver_stats( struct _lm_device_t* pdev, b10_driver_statistics_t *stats )
{
    stats->ver_num            = DRIVER_STATISTISTCS_VER_NUM;
    stats->tx_lso_frames      = pdev->vars.stats.stats_mirror.stats_drv.drv_eth.tx_lso_frames ;
    stats->tx_aborted         = pdev->vars.stats.stats_mirror.stats_drv.drv_eth.tx_aborted ;
    stats->tx_no_bd           = 0 ;
    stats->tx_no_desc         = 0 ;
    stats->tx_no_coalesce_buf = pdev->vars.stats.stats_mirror.stats_drv.drv_eth.tx_no_coalesce_buf ;
    stats->tx_no_map_reg      = 0 ;
    stats->rx_aborted         = pdev->vars.stats.stats_mirror.stats_drv.drv_eth.rx_aborted ;
    stats->rx_err             = 0 ;
    stats->rx_crc             = 0 ;
    stats->rx_phy_err         = 0 ;
    stats->rx_alignment       = 0;
    stats->rx_short_packet    = 0 ;
    stats->rx_giant_packet    = 0 ;
}

void lm_stats_get_l2_driver_stats( struct _lm_device_t* pdev, b10_l2_driver_statistics_t *stats )
{
    stats->ver_num            = L2_DRIVER_STATISTISTCS_VER_NUM;
    stats->RxIPv4FragCount    = pdev->vars.stats.stats_mirror.stats_drv.drv_eth.rx_ipv4_frag_count ;
    stats->RxIpCsErrorCount   = pdev->vars.stats.stats_mirror.stats_drv.drv_eth.rx_ip_cs_error_count ;
    stats->RxTcpCsErrorCount  = pdev->vars.stats.stats_mirror.stats_drv.drv_eth.rx_tcp_cs_error_count ;
    stats->RxLlcSnapCount     = pdev->vars.stats.stats_mirror.stats_drv.drv_eth.rx_llc_snap_count ;
    stats->RxPhyErrorCount    = pdev->vars.stats.stats_mirror.stats_drv.drv_eth.rx_phy_error_count ;
    stats->RxIpv6ExtCount     = pdev->vars.stats.stats_mirror.stats_drv.drv_eth.rx_ipv6_ext_count ;
    stats->TxNoL2Bd           = pdev->vars.stats.stats_mirror.stats_drv.drv_eth.tx_no_l2_bd ;
    stats->TxNoSqWqe          = pdev->vars.stats.stats_mirror.stats_drv.drv_eth.tx_no_sq_wqe ;
    stats->TxL2AssemblyBufUse = pdev->vars.stats.stats_mirror.stats_drv.drv_eth.tx_l2_assembly_buf_use ;
}
void lm_stats_get_l4_driver_stats( struct _lm_device_t* pdev, b10_l4_driver_statistics_t *stats )
{
    u8_t idx = 0 ;

    stats->ver_num                    = L4_DRIVER_STATISTISTCS_VER_NUM;

    idx = STATS_IP_4_IDX ;
    stats->CurrentlyIpv4Established   = pdev->vars.stats.stats_mirror.stats_drv.drv_toe.ipv[idx].currently_established ;
    stats->OutIpv4Resets              = pdev->vars.stats.stats_mirror.stats_drv.drv_toe.ipv[idx].out_resets ;
    stats->OutIpv4Fin                 = pdev->vars.stats.stats_mirror.stats_drv.drv_toe.ipv[idx].out_fin ;
    stats->InIpv4Reset                = pdev->vars.stats.stats_mirror.stats_drv.drv_toe.ipv[idx].in_reset ;
    stats->InIpv4Fin                  = pdev->vars.stats.stats_mirror.stats_drv.drv_toe.ipv[idx].in_fin ;

    idx = STATS_IP_6_IDX ;
    stats->CurrentlyIpv6Established   = pdev->vars.stats.stats_mirror.stats_drv.drv_toe.ipv[idx].currently_established ;
    stats->OutIpv6Resets              = pdev->vars.stats.stats_mirror.stats_drv.drv_toe.ipv[idx].out_resets ;
    stats->OutIpv6Fin                 = pdev->vars.stats.stats_mirror.stats_drv.drv_toe.ipv[idx].out_fin ;
    stats->InIpv6Reset                = pdev->vars.stats.stats_mirror.stats_drv.drv_toe.ipv[idx].in_reset ;
    stats->InIpv6Fin                  = pdev->vars.stats.stats_mirror.stats_drv.drv_toe.ipv[idx].in_fin ;

    stats->RxIndicateReturnPendingCnt = pdev->vars.stats.stats_mirror.stats_drv.drv_toe.rx_indicate_return_pending_cnt ;
    stats->RxIndicateReturnDoneCnt    = pdev->vars.stats.stats_mirror.stats_drv.drv_toe.rx_indicate_return_done_cnt ;
    stats->RxActiveGenBufCnt          = pdev->vars.stats.stats_mirror.stats_drv.drv_toe.rx_active_gen_buf_cnt ;
    stats->TxNoL4Bd                   = pdev->vars.stats.stats_mirror.stats_drv.drv_toe.tx_no_l4_bd ;
    stats->TxL4AssemblyBufUse         = pdev->vars.stats.stats_mirror.stats_drv.drv_toe.tx_l4_assembly_buf_use ;
}

void lm_stats_get_l2_chip_stats( struct _lm_device_t* pdev, b10_l2_chip_statistics_t *stats )
{
    u32_t idx = LM_CLI_IDX_NDIS ;

    stats->ver_num                                = L2_CHIP_STATISTISTCS_VER_NUM ;

    // TODO - change IOCTL structure to be per client

    stats->IfHCInOctets                           = pdev->vars.stats.stats_mirror.stats_fw.eth_tstorm_common.client_statistics[idx].rcv_broadcast_bytes +
                                                    pdev->vars.stats.stats_mirror.stats_fw.eth_tstorm_common.client_statistics[idx].rcv_multicast_bytes +
                                                    pdev->vars.stats.stats_mirror.stats_fw.eth_tstorm_common.client_statistics[idx].rcv_unicast_bytes ;
    stats->IfHCInBadOctets                        = (LM_STATS_HW_GET_MACS_U64( pdev, stats_rx.rx_stat_ifhcinbadoctets ) );
    stats->IfHCOutOctets                          = pdev->vars.stats.stats_mirror.stats_fw.eth_xstorm_common.client_statistics[idx].total_sent_bytes ;
    stats->IfHCOutBadOctets                       = (LM_STATS_HW_GET_MACS_U64( pdev, stats_tx.tx_stat_ifhcoutbadoctets ) );
    stats->IfHCInUcastPkts                        = (pdev->vars.stats.stats_mirror.stats_fw.eth_tstorm_common.client_statistics[idx].rcv_unicast_pkts ) ;
    stats->IfHCInMulticastPkts                    = (pdev->vars.stats.stats_mirror.stats_fw.eth_tstorm_common.client_statistics[idx].rcv_multicast_pkts ) ;
    stats->IfHCInBroadcastPkts                    = (pdev->vars.stats.stats_mirror.stats_fw.eth_tstorm_common.client_statistics[idx].rcv_broadcast_pkts ) ;
    stats->IfHCInUcastOctets                      = (pdev->vars.stats.stats_mirror.stats_fw.eth_tstorm_common.client_statistics[idx].rcv_unicast_bytes ) ;
    stats->IfHCInMulticastOctets                  = (pdev->vars.stats.stats_mirror.stats_fw.eth_tstorm_common.client_statistics[idx].rcv_multicast_bytes ) ;
    stats->IfHCInBroadcastOctets                  = (pdev->vars.stats.stats_mirror.stats_fw.eth_tstorm_common.client_statistics[idx].rcv_broadcast_bytes ) ;

    stats->IfHCOutUcastOctets                     = (pdev->vars.stats.stats_mirror.stats_fw.eth_xstorm_common.client_statistics[idx].unicast_bytes_sent ) ;
    stats->IfHCOutMulticastOctets                 = (pdev->vars.stats.stats_mirror.stats_fw.eth_xstorm_common.client_statistics[idx].multicast_bytes_sent ) ;
    stats->IfHCOutBroadcastOctets                 = (pdev->vars.stats.stats_mirror.stats_fw.eth_xstorm_common.client_statistics[idx].broadcast_bytes_sent ) ;
    stats->IfHCOutPkts                            = (pdev->vars.stats.stats_mirror.stats_fw.eth_xstorm_common.client_statistics[idx].total_sent_pkts ) ;


    lm_get_stats( pdev,  LM_STATS_UNICAST_FRAMES_XMIT, &stats->IfHCOutUcastPkts ) ;
    lm_get_stats( pdev,  LM_STATS_MULTICAST_FRAMES_XMIT, &stats->IfHCOutMulticastPkts ) ;
    lm_get_stats( pdev,  LM_STATS_BROADCAST_FRAMES_XMIT, &stats->IfHCOutBroadcastPkts ) ;

    stats->IfHCInPkts                             = pdev->vars.stats.stats_mirror.stats_fw.eth_tstorm_common.client_statistics[idx].rcv_broadcast_pkts +
                                                    pdev->vars.stats.stats_mirror.stats_fw.eth_tstorm_common.client_statistics[idx].rcv_multicast_pkts +
                                                    pdev->vars.stats.stats_mirror.stats_fw.eth_tstorm_common.client_statistics[idx].rcv_unicast_pkts ;

    stats->IfHCOutDiscards                        = (LM_STATS_HW_GET_MACS_U64( pdev, stats_tx.tx_stat_ifhcoutdiscards ) );
    stats->IfHCInFalseCarrierErrors               = (LM_STATS_HW_GET_MACS_U64( pdev, stats_rx_err.rx_stat_falsecarriererrors ) );

    stats->Dot3StatsInternalMacTransmitErrors     = (LM_STATS_HW_GET_MACS_U64( pdev, stats_tx.tx_stat_dot3statsinternalmactransmiterrors )) ;
    stats->Dot3StatsCarrierSenseErrors            = (LM_STATS_HW_GET_MACS_U64( pdev, stats_rx.rx_stat_dot3statscarriersenseerrors )) ;
    stats->Dot3StatsFCSErrors                     = (LM_STATS_HW_GET_MACS_U64( pdev, stats_rx.rx_stat_dot3statsfcserrors )) ;
    stats->Dot3StatsAlignmentErrors               = (LM_STATS_HW_GET_MACS_U64( pdev, stats_rx.rx_stat_dot3statsalignmenterrors )) ;
    stats->Dot3StatsSingleCollisionFrames         = (LM_STATS_HW_GET_MACS_U64( pdev, stats_tx.tx_stat_dot3statssinglecollisionframes )) ;
    stats->Dot3StatsMultipleCollisionFrames       = (LM_STATS_HW_GET_MACS_U64( pdev, stats_tx.tx_stat_dot3statsmultiplecollisionframes )) ;
    stats->Dot3StatsDeferredTransmissions         = (LM_STATS_HW_GET_MACS_U64( pdev, stats_tx.tx_stat_dot3statsdeferredtransmissions )) ;
    stats->Dot3StatsExcessiveCollisions           = (LM_STATS_HW_GET_MACS_U64( pdev, stats_tx.tx_stat_dot3statsexcessivecollisions )) ;
    stats->Dot3StatsLateCollisions                = (LM_STATS_HW_GET_MACS_U64( pdev, stats_tx.tx_stat_dot3statslatecollisions )) ;
    stats->EtherStatsCollisions                   = (LM_STATS_HW_GET_MACS_U64( pdev, stats_tx.tx_stat_etherstatscollisions )) ;
    stats->EtherStatsFragments                    = (LM_STATS_HW_GET_MACS_U64( pdev, stats_rx.rx_stat_etherstatsfragments )) ;
    stats->EtherStatsJabbers                      = (LM_STATS_HW_GET_MACS_U64( pdev, stats_rx.rx_stat_etherstatsjabbers )) ;


    stats->EtherStatsUndersizePkts                = (LM_STATS_HW_GET_MACS_U64( pdev, stats_rx.rx_stat_etherstatsundersizepkts )) ;
    stats->EtherStatsOverrsizePkts                = (LM_STATS_HW_GET_MACS_U64( pdev, stats_rx.rx_stat_dot3statsframestoolong )) ;

    stats->EtherStatsPktsTx64Octets               = (LM_STATS_HW_GET_MACS_U64( pdev, stats_tx.tx_stat_etherstatspkts64octets )) ;
    stats->EtherStatsPktsTx65Octetsto127Octets    = (LM_STATS_HW_GET_MACS_U64( pdev, stats_tx.tx_stat_etherstatspkts65octetsto127octets )) ;
    stats->EtherStatsPktsTx128Octetsto255Octets   = (LM_STATS_HW_GET_MACS_U64( pdev, stats_tx.tx_stat_etherstatspkts128octetsto255octets )) ;
    stats->EtherStatsPktsTx256Octetsto511Octets   = (LM_STATS_HW_GET_MACS_U64( pdev, stats_tx.tx_stat_etherstatspkts256octetsto511octets )) ;
    stats->EtherStatsPktsTx512Octetsto1023Octets  = (LM_STATS_HW_GET_MACS_U64( pdev, stats_tx.tx_stat_etherstatspkts512octetsto1023octets)) ;
    stats->EtherStatsPktsTx1024Octetsto1522Octets = (pdev->vars.stats.stats_mirror.stats_hw.nig_ex.egress_mac_pkt0) ;
    stats->EtherStatsPktsTxOver1522Octets         = (pdev->vars.stats.stats_mirror.stats_hw.nig_ex.egress_mac_pkt1) ;

    stats->XonPauseFramesReceived                 = (LM_STATS_HW_GET_MACS_U64( pdev, stats_rx.rx_stat_xonpauseframesreceived )) ;
    stats->XoffPauseFramesReceived                = (LM_STATS_HW_GET_MACS_U64( pdev, stats_rx.rx_stat_xoffpauseframesreceived )) ;
    stats->OutXonSent                             = (LM_STATS_HW_GET_MACS_U64( pdev, stats_tx.tx_stat_outxonsent )) ;

    stats->OutXoffSent                            = (LM_STATS_HW_GET_MACS_U64( pdev, stats_tx.tx_stat_outxoffsent )) ;

    stats->FlowControlDone                        = (LM_STATS_HW_GET_MACS_U64( pdev, stats_tx.tx_stat_flowcontroldone )) ;

    stats->MacControlFramesReceived               = (LM_STATS_HW_GET_MACS_U64( pdev, stats_rx.rx_stat_maccontrolframesreceived )) ;
    stats->MacControlFramesReceived              += (LM_STATS_HW_GET_MACS_U64( pdev, stats_rx.rx_stat_maccontrolframesreceived_bmac_xcf )) ;

    stats->XoffStateEntered                       = (LM_STATS_HW_GET_MACS_U64( pdev, stats_rx.rx_stat_xoffstateentered )) ;
    lm_get_stats( pdev, LM_STATS_ERRORED_RECEIVE_CNT, &stats->IfInErrors ) ;
    // TBD - IfInErrorsOctets - naming and support
    stats->IfInNoBrbBuffer                        = (pdev->vars.stats.stats_mirror.stats_hw.nig.brb_discard) ;
    stats->IfInFramesL2FilterDiscards             = (pdev->vars.stats.stats_mirror.stats_fw.eth_tstorm_common.port_statistics.mac_filter_discard) ;
    stats->IfInTTL0Discards                       = (pdev->vars.stats.stats_mirror.stats_fw.eth_tstorm_common.client_statistics[idx].ttl0_discard) ;
    stats->IfInxxOverflowDiscards                 = (pdev->vars.stats.stats_mirror.stats_fw.eth_tstorm_common.port_statistics.xxoverflow_discard) ;

    stats->IfInMBUFDiscards                       = (pdev->vars.stats.stats_mirror.stats_fw.eth_tstorm_common.client_statistics[idx].no_buff_discard );
    stats->IfInMBUFDiscards                      += (pdev->vars.stats.stats_mirror.stats_fw.eth_ustorm_common.client_statistics[idx].ucast_no_buff_pkts );
    stats->IfInMBUFDiscards                      += (pdev->vars.stats.stats_mirror.stats_fw.eth_ustorm_common.client_statistics[idx].mcast_no_buff_pkts );
    stats->IfInMBUFDiscards                      += (pdev->vars.stats.stats_mirror.stats_fw.eth_ustorm_common.client_statistics[idx].bcast_no_buff_pkts );

    stats->Nig_brb_packet                         = (pdev->vars.stats.stats_mirror.stats_hw.nig.brb_packet) ;
    stats->Nig_brb_truncate                       = (pdev->vars.stats.stats_mirror.stats_hw.nig.brb_truncate) ;
    stats->Nig_flow_ctrl_discard                  = (pdev->vars.stats.stats_mirror.stats_hw.nig.flow_ctrl_discard) ;
    stats->Nig_flow_ctrl_octets                   = (pdev->vars.stats.stats_mirror.stats_hw.nig.flow_ctrl_octets) ;
    stats->Nig_flow_ctrl_packet                   = (pdev->vars.stats.stats_mirror.stats_hw.nig.flow_ctrl_packet) ;
    stats->Nig_mng_discard                        = (pdev->vars.stats.stats_mirror.stats_hw.nig.mng_discard) ;
    stats->Nig_mng_octet_inp                      = (pdev->vars.stats.stats_mirror.stats_hw.nig.mng_octet_inp) ;
    stats->Nig_mng_octet_out                      = (pdev->vars.stats.stats_mirror.stats_hw.nig.mng_octet_out) ;
    stats->Nig_mng_packet_inp                     = (pdev->vars.stats.stats_mirror.stats_hw.nig.mng_packet_inp) ;
    stats->Nig_mng_packet_out                     = (pdev->vars.stats.stats_mirror.stats_hw.nig.mng_packet_out) ;
    stats->Nig_pbf_octets                         = (pdev->vars.stats.stats_mirror.stats_hw.nig.pbf_octets) ;
    stats->Nig_pbf_packet                         = (pdev->vars.stats.stats_mirror.stats_hw.nig.pbf_packet) ;
    stats->Nig_safc_inp                           = (pdev->vars.stats.stats_mirror.stats_hw.nig.safc_inp) ;
}

void lm_stats_get_l4_chip_stats( struct _lm_device_t* pdev, b10_l4_chip_statistics_t *stats )
{
    u8_t idx = 0 ;

    stats->ver_num                     = L4_CHIP_STATISTISTCS_VER_NUM ;

    stats->NoTxCqes                    = pdev->vars.stats.stats_mirror.stats_fw.toe_cstorm_toe.no_tx_cqes ;

    // IP4
    idx = STATS_IP_4_IDX ;

    stats->InTCP4Segments              = pdev->vars.stats.stats_mirror.stats_fw.toe_tstorm_toe.statistics[idx].ip_in_receives ;
    stats->OutTCP4Segments             = pdev->vars.stats.stats_mirror.stats_fw.toe_xstorm_toe.statistics[idx].tcp_out_segments ;
    stats->RetransmittedTCP4Segments   = pdev->vars.stats.stats_mirror.stats_fw.toe_xstorm_toe.statistics[idx].tcp_retransmitted_segments ;
    stats->InTCP4Errors                = pdev->vars.stats.stats_mirror.stats_fw.toe_tstorm_toe.statistics[idx].tcp_in_errors ;
    stats->InIP4Receives               = pdev->vars.stats.stats_mirror.stats_fw.toe_tstorm_toe.statistics[idx].ip_in_receives ;
    stats->InIP4HeaderErrors           = pdev->vars.stats.stats_mirror.stats_fw.toe_tstorm_toe.statistics[idx].ip_in_header_errors ;
    stats->InIP4Discards               = pdev->vars.stats.stats_mirror.stats_fw.toe_tstorm_toe.statistics[idx].ip_in_discards ;
    stats->InIP4Delivers               = pdev->vars.stats.stats_mirror.stats_fw.toe_tstorm_toe.statistics[idx].ip_in_delivers ;
    stats->InIP4Octets                 = pdev->vars.stats.stats_mirror.stats_fw.toe_tstorm_toe.statistics[idx].ip_in_octets ;
    stats->OutIP4Octets                = pdev->vars.stats.stats_mirror.stats_fw.toe_xstorm_toe.statistics[idx].ip_out_octets ;
    stats->InIP4TruncatedPackets       = pdev->vars.stats.stats_mirror.stats_fw.toe_tstorm_toe.statistics[idx].ip_in_truncated_packets ;

    // IP6
    idx = STATS_IP_6_IDX ;

    stats->InTCP6Segments              = pdev->vars.stats.stats_mirror.stats_fw.toe_tstorm_toe.statistics[idx].ip_in_receives ;
    stats->OutTCP6Segments             = pdev->vars.stats.stats_mirror.stats_fw.toe_xstorm_toe.statistics[idx].tcp_out_segments ;
    stats->RetransmittedTCP6Segments   = pdev->vars.stats.stats_mirror.stats_fw.toe_xstorm_toe.statistics[idx].tcp_retransmitted_segments ;
    stats->InTCP6Errors                = pdev->vars.stats.stats_mirror.stats_fw.toe_tstorm_toe.statistics[idx].tcp_in_errors ;
    stats->InIP6Receives               = pdev->vars.stats.stats_mirror.stats_fw.toe_tstorm_toe.statistics[idx].ip_in_receives ;
    stats->InIP6HeaderErrors           = pdev->vars.stats.stats_mirror.stats_fw.toe_tstorm_toe.statistics[idx].ip_in_header_errors ;
    stats->InIP6Discards               = pdev->vars.stats.stats_mirror.stats_fw.toe_tstorm_toe.statistics[idx].ip_in_discards ;
    stats->InIP6Delivers               = pdev->vars.stats.stats_mirror.stats_fw.toe_tstorm_toe.statistics[idx].ip_in_delivers ;
    stats->InIP6Octets                 = pdev->vars.stats.stats_mirror.stats_fw.toe_tstorm_toe.statistics[idx].ip_in_octets ;
    stats->OutIP6Octets                = pdev->vars.stats.stats_mirror.stats_fw.toe_xstorm_toe.statistics[idx].ip_out_octets ;
    stats->InIP6TruncatedPackets       = pdev->vars.stats.stats_mirror.stats_fw.toe_tstorm_toe.statistics[idx].ip_in_truncated_packets ;
}

void lm_stats_hw_config_stats( struct _lm_device_t* pdev, u8_t b_enabled )
{
    DbgMessage2(pdev, WARNstat, "lm_stats_hw_config_stats: b_collect_enabled %s-->%s\n",
                pdev->vars.stats.stats_collect.stats_hw.b_collect_enabled ? "TRUE":"FALSE",
                b_enabled ? "TRUE":"FALSE" );

    if (IS_PFDEV(pdev)) {
    pdev->vars.stats.stats_collect.stats_hw.b_collect_enabled = b_enabled ;
    }
}

void lm_stats_fw_config_stats( struct _lm_device_t* pdev, u8_t b_enabled )
{
    DbgMessage2(pdev, VERBOSEstat, "lm_stats_fw_config_stats: b_collect_enabled %s-->%s\n",
            pdev->vars.stats.stats_collect.stats_fw.b_collect_enabled ? "TRUE":"FALSE",
            b_enabled ? "TRUE":"FALSE" );
    if (IS_PFDEV(pdev)) {
        pdev->vars.stats.stats_collect.stats_fw.b_collect_enabled = b_enabled ;
    }
}

/*
 *------------------------------------------------------------------------
 * lm_stats_mgmt_assign_func
 *
 * assign values from different 'mirror' structures into host_func_stats_t structure
 * that will be sent later to mgmt
 * NOTE: function must be called under PHY_LOCK (since it uses REG_WR_DMAE interface)
 *------------------------------------------------------------------------
 */
STATIC void lm_stats_mgmt_assign_func( IN struct _lm_device_t* pdev )
{
    u64_t              val           = 0 ;
    u64_t              val_base      = 0 ;
    lm_status_t        lm_status     = LM_STATUS_SUCCESS ;
    lm_stats_t         stats_type    = 0 ;
    host_func_stats_t* mcp_func      = NULL ;
    host_func_stats_t* mcp_func_base = NULL ;

    if CHK_NULL(pdev)
    {
        return;
    }

    if ( GET_FLAGS(pdev->params.test_mode, TEST_MODE_NO_MCP ) )
    {
        return;
    }

    mcp_func      = &pdev->vars.stats.stats_mirror.stats_mcp_func ;
    mcp_func_base = &pdev->vars.stats.stats_mirror.stats_mcp_func_base ;

    stats_type = LM_STATS_BYTES_RCV ;
    lm_status = lm_get_stats( pdev, stats_type, &val ) ;
    if ERR_IF( LM_STATUS_SUCCESS != lm_status )
    {
        DbgMessage2(pdev, WARNstat, "lm_stats_mcp_assign: lm_get_stats type=0x%X failed. lm_status=0x%X", stats_type, lm_status ) ;
    }
    else
    {
        // calculate 'total' rcv (total+discards)
        val += (pdev->vars.stats.stats_mirror.stats_fw.eth_tstorm_common.client_statistics[LM_CLI_IDX_NDIS].rcv_error_bytes) ;

        val+= LM_STATS_HI_LO_TO_64(mcp_func_base->total_bytes_received, val_base);
        mcp_func->total_bytes_received_hi                = (u32_t)U64_HI( val ) ;
        mcp_func->total_bytes_received_lo                = (u32_t)U64_LO( val ) ;
    }

    stats_type = LM_STATS_BYTES_XMIT ;
    lm_status  = lm_get_stats( pdev, stats_type, &val ) ;
    if ERR_IF( LM_STATUS_SUCCESS != lm_status )
    {
        DbgMessage2(pdev, WARNstat, "lm_stats_mcp_assign: lm_get_stats type=0x%X failed. lm_status=0x%X", stats_type, lm_status ) ;
    }
    else
    {
        val+= LM_STATS_HI_LO_TO_64(mcp_func_base->total_bytes_transmitted, val_base);
        mcp_func->total_bytes_transmitted_hi             = (u32_t)U64_HI( val ) ;
        mcp_func->total_bytes_transmitted_lo             = (u32_t)U64_LO( val ) ;
    }

    stats_type = LM_STATS_UNICAST_FRAMES_RCV ;
    lm_status  = lm_get_stats( pdev, stats_type, &val ) ;
    if ERR_IF( LM_STATUS_SUCCESS != lm_status )
    {
        DbgMessage2(pdev, WARNstat, "lm_stats_mcp_assign: lm_get_stats type=0x%X failed. lm_status=0x%X", stats_type, lm_status ) ;
    }
    else
    {
        val+= LM_STATS_HI_LO_TO_64(mcp_func_base->total_unicast_packets_received, val_base);
        mcp_func->total_unicast_packets_received_hi      = (u32_t)U64_HI( val ) ;
        mcp_func->total_unicast_packets_received_lo      = (u32_t)U64_LO( val ) ;
    }

    stats_type = LM_STATS_MULTICAST_FRAMES_RCV ;
    lm_status  = lm_get_stats( pdev, stats_type, &val ) ;
    if ERR_IF( LM_STATUS_SUCCESS != lm_status )
    {
        DbgMessage2(pdev, WARNstat, "lm_stats_mcp_assign: lm_get_stats type=0x%X failed. lm_status=0x%X", stats_type, lm_status ) ;
    }
    else
    {
        val+= LM_STATS_HI_LO_TO_64(mcp_func_base->total_multicast_packets_received, val_base);
        mcp_func->total_multicast_packets_received_hi    = (u32_t)U64_HI( val ) ;
        mcp_func->total_multicast_packets_received_lo    = (u32_t)U64_LO( val ) ;
    }

    stats_type = LM_STATS_BROADCAST_FRAMES_RCV ;
    lm_status = lm_get_stats( pdev, stats_type, &val ) ;
    if ERR_IF( LM_STATUS_SUCCESS != lm_status )
    {
        DbgMessage2(pdev, WARNstat, "lm_stats_mcp_assign: lm_get_stats type=0x%X failed. lm_status=0x%X", stats_type, lm_status ) ;
    }
    else
    {
        val+= LM_STATS_HI_LO_TO_64(mcp_func_base->total_broadcast_packets_received, val_base);
        mcp_func->total_broadcast_packets_received_hi    = (u32_t)U64_HI( val ) ;
        mcp_func->total_broadcast_packets_received_lo    = (u32_t)U64_LO( val ) ;
    }

    stats_type = LM_STATS_UNICAST_FRAMES_XMIT ;
    lm_status  = lm_get_stats( pdev, stats_type, &val ) ;
    if ERR_IF( LM_STATUS_SUCCESS != lm_status )
    {
        DbgMessage2(pdev, WARNstat, "lm_stats_mcp_assign: lm_get_stats type=0x%X failed. lm_status=0x%X", stats_type, lm_status ) ;
    }
    else
    {
        val+= LM_STATS_HI_LO_TO_64(mcp_func_base->total_unicast_packets_transmitted, val_base);
        mcp_func->total_unicast_packets_transmitted_hi   = (u32_t)U64_HI( val ) ;
        mcp_func->total_unicast_packets_transmitted_lo   = (u32_t)U64_LO( val ) ;
    }

    stats_type = LM_STATS_MULTICAST_FRAMES_XMIT ;
    lm_status  = lm_get_stats( pdev, stats_type, &val ) ;
    if ERR_IF( LM_STATUS_SUCCESS != lm_status )
    {
        DbgMessage2(pdev, WARNstat, "lm_stats_mcp_assign: lm_get_stats type=0x%X failed. lm_status=0x%X", stats_type, lm_status ) ;
    }
    else
    {
        val+= LM_STATS_HI_LO_TO_64(mcp_func_base->total_multicast_packets_transmitted, val_base);
        mcp_func->total_multicast_packets_transmitted_hi = (u32_t)U64_HI( val ) ;
        mcp_func->total_multicast_packets_transmitted_lo = (u32_t)U64_LO( val ) ;
    }

    stats_type = LM_STATS_BROADCAST_FRAMES_XMIT ;
    lm_status  = lm_get_stats( pdev, stats_type, &val ) ;
    if ERR_IF( LM_STATUS_SUCCESS != lm_status )
    {
        DbgMessage2(pdev, WARNstat, "lm_stats_mcp_assign: lm_get_stats type=0x%X failed. lm_status=0x%X", stats_type, lm_status ) ;
    }
    else
    {
        val+= LM_STATS_HI_LO_TO_64(mcp_func_base->total_broadcast_packets_transmitted, val_base);
        mcp_func->total_broadcast_packets_transmitted_hi = (u32_t)U64_HI( val ) ;
        mcp_func->total_broadcast_packets_transmitted_lo = (u32_t)U64_LO( val ) ;
    }

    // Calculate the size to be written through DMAE
    val = sizeof(pdev->vars.stats.stats_mirror.stats_mcp_func) ;
    val = val/sizeof(u32_t) ;
    mcp_func->host_func_stats_end = ++mcp_func->host_func_stats_start ;

    // This code section must be under phy lock!
    REG_WR_DMAE_LEN(pdev,
                    pdev->vars.fw_func_stats_ptr,
                    mcp_func,
                    (u16_t)val ) ;

} // lm_stats_mgmt_assign

/*
 *------------------------------------------------------------------------
 * lm_stats_mgmt_read_base -
 *
 * read values from mgmt structures into host_func_stats_t base structure
 * this is as a basic value that will be added when function report statistics
 * NOTE: function must be called under PHY_LOCK (since it uses REG_RD_DMAE interface)
 *------------------------------------------------------------------------
 */
static void lm_stats_mgmt_read_func_base( IN struct _lm_device_t* pdev )
{
    u64_t              val           = 0 ;
    host_func_stats_t* mcp_func_base = NULL ;

    if CHK_NULL(pdev)
    {
        return;
    }

    if( 0 == pdev->vars.fw_func_stats_ptr )
    {
        return;
    }

    if (GET_FLAGS(pdev->params.test_mode, TEST_MODE_NO_MCP ))
    {
        return;
    }

    mcp_func_base = &pdev->vars.stats.stats_mirror.stats_mcp_func_base ;

    val = sizeof(pdev->vars.stats.stats_mirror.stats_mcp_func_base) ;
    val = val/sizeof(u32_t) ;

    // This code section must be under phy lock!
    REG_RD_DMAE_LEN(pdev,
                    pdev->vars.fw_func_stats_ptr,
                    mcp_func_base,
                    (u16_t)val ) ;

} // lm_stats_mgmt_read_base


/*
 *------------------------------------------------------------------------
 * lm_stats_mgmt_clear_all_func -
 *
 * clear mgmt statistics for all function
 * should be called on init port part. first function should clear all other functions mail box
 * NOTE: function must be called under PHY_LOCK (since it uses REG_WR_DMAE interface)
 *------------------------------------------------------------------------
 */
static void lm_stats_mgmt_clear_all_func( IN struct _lm_device_t* pdev )
{
    u64_t              val               = 0 ;
    u8_t               func              = 0;
    u32_t              fw_func_stats_ptr = 0;

    // use current pdev stats_mcp_func for all function - (zeroed buffer)
    val = sizeof(pdev->vars.stats.stats_mirror.stats_mcp_func);
    mm_mem_zero(&pdev->vars.stats.stats_mirror.stats_mcp_func, (u32_t)val );

    val = val/sizeof(u32_t) ;

    LM_FOREACH_FUNC_MAILBOX_IN_PORT(pdev,func)
    {
        lm_setup_read_mgmt_stats_ptr(pdev, func, NULL, &fw_func_stats_ptr );

        if( 0 != fw_func_stats_ptr )
        {

            // This code section must be under phy lock!
            // writes zero
            REG_WR_DMAE_LEN(pdev,
                            fw_func_stats_ptr,
                            &pdev->vars.stats.stats_mirror.stats_mcp_func,
                            (u16_t)val ) ;
        }
        if(CHIP_IS_E1(pdev) || (!CHIP_IS_E1x(pdev) && (CHIP_PORT_MODE(pdev) == LM_CHIP_PORT_MODE_4)))
        {
            // only one iteration functionand one  for E1 !
            break;
        }
    }
} // lm_stats_mgmt_clear_all_func

/*
 *Function Name:lm_stats_port_to_from
 *
 *Parameters:
 *  b_is_to - determine is it operation to/from MCP
 *  b_is_to TRUE  - to MCP
 *  b_is_to FLASE - from MCP
 *Description:
 *  Helper function in order to set stats to/from mcp to driver host when swithcing PMF's
 *
 *Returns:
 *
 */
void lm_stats_port_to_from( IN OUT struct _lm_device_t* pdev, u8_t b_is_to )
{
    host_port_stats_t* mcp_port      = NULL ;
    lm_stats_hw_t*    stats_hw       = NULL ;
    stats_macs_idx_t  stats_macs_idx = STATS_MACS_IDX_MAX ;
    u8_t              i              = 0 ;

    if CHK_NULL(pdev)
    {
        return;
    }

    mcp_port = &pdev->vars.stats.stats_mirror.stats_mcp_port ;
    stats_hw = &pdev->vars.stats.stats_mirror.stats_hw ;

    ASSERT_STATIC( STATS_MACS_IDX_MAX == MAC_STX_IDX_MAX );
    ASSERT_STATIC( STATS_MACS_IDX_CURRENT < STATS_MACS_IDX_TOTAL );


    // B/EMAC is up:
    //   OLD PMF:
    //   copy all EMAC 'reset' to 'total'
    //
    //   NEW PMF:
    //   copy all EMAC 'total' to 'reset'
    //
    // NONE is up:
    //   copy only 'reset' to 'total'

    switch( pdev->vars.mac_type )
    {
    case MAC_TYPE_EMAC:
    case MAC_TYPE_BMAC:
    case MAC_TYPE_UMAC:
    case MAC_TYPE_XMAC:
        stats_macs_idx  = STATS_MACS_IDX_CURRENT ;
        break;

    case MAC_TYPE_NONE:
        stats_macs_idx  = STATS_MACS_IDX_TOTAL ;
        break;

    default:
        DbgBreakMsg( "mac_type not acceptable" ) ;
        return;
    }

#define LM_STATS_PMF_TO_FROM( _mcp_field, _hw_field, _b_is_to ) \
                             if( _b_is_to )\
                             {             \
                                LM_STATS_64_TO_HI_LO( stats_hw->macs[i]._hw_field, mcp_port->mac_stx[i]._mcp_field );\
                             }             \
                             else          \
                             {             \
                                 LM_STATS_HI_LO_TO_64( mcp_port->mac_stx[i]._mcp_field, stats_hw->macs[i]._hw_field ) ;\
                             }


    for( i = stats_macs_idx; i < STATS_MACS_IDX_MAX; i++ )
    {
       LM_STATS_PMF_TO_FROM( rx_stat_dot3statsfcserrors,                   stats_rx.rx_stat_dot3statsfcserrors,                   b_is_to ) ;
       LM_STATS_PMF_TO_FROM( rx_stat_dot3statsalignmenterrors,             stats_rx.rx_stat_dot3statsalignmenterrors,             b_is_to ) ; // BMAC 0
       LM_STATS_PMF_TO_FROM( rx_stat_dot3statscarriersenseerrors,          stats_rx.rx_stat_dot3statscarriersenseerrors,          b_is_to ) ; // BMAC 0
       LM_STATS_PMF_TO_FROM( rx_stat_etherstatsundersizepkts,              stats_rx.rx_stat_etherstatsundersizepkts,              b_is_to ) ;

       // Exception - don't migrate this parameter (mandatory NDIS parameter)
       //LM_STATS_PMF_TO_FROM( rx_stat_dot3statsframestoolong,               stats_rx.rx_stat_dot3statsframestoolong,             b_is_to ) ;

       LM_STATS_PMF_TO_FROM( rx_stat_xonpauseframesreceived,               stats_rx.rx_stat_xonpauseframesreceived,               b_is_to ) ; // BMAC 0
       LM_STATS_PMF_TO_FROM( rx_stat_xoffpauseframesreceived,              stats_rx.rx_stat_xoffpauseframesreceived,              b_is_to ) ;
       LM_STATS_PMF_TO_FROM( tx_stat_outxonsent,                           stats_tx.tx_stat_outxonsent,                           b_is_to ) ; // BMAC 0
       LM_STATS_PMF_TO_FROM( tx_stat_outxoffsent,                          stats_tx.tx_stat_outxoffsent,                          b_is_to ) ;
       LM_STATS_PMF_TO_FROM( tx_stat_dot3statssinglecollisionframes,       stats_tx.tx_stat_dot3statssinglecollisionframes,       b_is_to ) ; // BMAC 0
       LM_STATS_PMF_TO_FROM( tx_stat_dot3statsmultiplecollisionframes,     stats_tx.tx_stat_dot3statsmultiplecollisionframes,     b_is_to ) ; // BMAC 0
       LM_STATS_PMF_TO_FROM( tx_stat_dot3statslatecollisions,              stats_tx.tx_stat_dot3statslatecollisions,              b_is_to ) ; // BMAC 0
       LM_STATS_PMF_TO_FROM( tx_stat_dot3statsexcessivecollisions,         stats_tx.tx_stat_dot3statsexcessivecollisions,         b_is_to ) ; // BMAC 0
       LM_STATS_PMF_TO_FROM( rx_stat_maccontrolframesreceived,             stats_rx.rx_stat_maccontrolframesreceived,             b_is_to ) ;

       LM_STATS_PMF_TO_FROM( rx_stat_mac_xpf,                             stats_rx.rx_stat_maccontrolframesreceived_bmac_xpf,    b_is_to ) ; // EMAC 0 BMAC only
       LM_STATS_PMF_TO_FROM( rx_stat_mac_xcf,                             stats_rx.rx_stat_maccontrolframesreceived_bmac_xcf,    b_is_to ) ; // EMAC 0 BMAC only

       LM_STATS_PMF_TO_FROM( tx_stat_etherstatspkts64octets,               stats_tx.tx_stat_etherstatspkts64octets,               b_is_to ) ;
       LM_STATS_PMF_TO_FROM( tx_stat_etherstatspkts65octetsto127octets,    stats_tx.tx_stat_etherstatspkts65octetsto127octets,    b_is_to ) ;
       LM_STATS_PMF_TO_FROM( tx_stat_etherstatspkts128octetsto255octets,   stats_tx.tx_stat_etherstatspkts128octetsto255octets,   b_is_to ) ;
       LM_STATS_PMF_TO_FROM( tx_stat_etherstatspkts256octetsto511octets,   stats_tx.tx_stat_etherstatspkts256octetsto511octets,   b_is_to ) ;
       LM_STATS_PMF_TO_FROM( tx_stat_etherstatspkts512octetsto1023octets,  stats_tx.tx_stat_etherstatspkts512octetsto1023octets,  b_is_to ) ;
       LM_STATS_PMF_TO_FROM( tx_stat_etherstatspkts1024octetsto1522octets, stats_tx.tx_stat_etherstatspkts1024octetsto1522octet,  b_is_to ) ;
       LM_STATS_PMF_TO_FROM( tx_stat_etherstatspktsover1522octets,         stats_tx.tx_stat_etherstatspktsover1522octets,         b_is_to ) ;


       LM_STATS_PMF_TO_FROM( tx_stat_mac_2047,                            stats_tx.tx_stat_etherstatspktsover1522octets_bmac_2047, b_is_to ) ; // EMAC 0 BMAC only
       LM_STATS_PMF_TO_FROM( tx_stat_mac_4095,                            stats_tx.tx_stat_etherstatspktsover1522octets_bmac_4095, b_is_to ) ; // EMAC 0 BMAC only
       LM_STATS_PMF_TO_FROM( tx_stat_mac_9216,                            stats_tx.tx_stat_etherstatspktsover1522octets_bmac_9216, b_is_to ) ; // EMAC 0 BMAC only
       LM_STATS_PMF_TO_FROM( tx_stat_mac_16383,                           stats_tx.tx_stat_etherstatspktsover1522octets_bmac_16383, b_is_to ) ; // EMAC 0 BMAC only

       LM_STATS_PMF_TO_FROM( rx_stat_etherstatsfragments,                  stats_rx.rx_stat_etherstatsfragments,                  b_is_to ) ;
       LM_STATS_PMF_TO_FROM( rx_stat_etherstatsjabbers,                    stats_rx.rx_stat_etherstatsjabbers,                    b_is_to ) ;
       LM_STATS_PMF_TO_FROM( tx_stat_dot3statsdeferredtransmissions,       stats_tx.tx_stat_dot3statsdeferredtransmissions,       b_is_to ) ; // BMAC 0
       LM_STATS_PMF_TO_FROM( tx_stat_dot3statsinternalmactransmiterrors,   stats_tx.tx_stat_dot3statsinternalmactransmiterrors,   b_is_to ) ;
       LM_STATS_PMF_TO_FROM( tx_stat_etherstatscollisions,                 stats_tx.tx_stat_etherstatscollisions,                 b_is_to ) ; // BMAC 0
       LM_STATS_PMF_TO_FROM( tx_stat_flowcontroldone,                      stats_tx.tx_stat_flowcontroldone,                      b_is_to ) ;
       LM_STATS_PMF_TO_FROM( rx_stat_xoffstateentered,                     stats_rx.rx_stat_xoffstateentered,                     b_is_to ) ;
       LM_STATS_PMF_TO_FROM( rx_stat_ifhcinbadoctets,                      stats_rx.rx_stat_ifhcinbadoctets,                      b_is_to ) ;
       LM_STATS_PMF_TO_FROM( tx_stat_ifhcoutbadoctets,                     stats_tx.tx_stat_ifhcoutbadoctets,                     b_is_to ) ; // BMAC 0
       LM_STATS_PMF_TO_FROM( tx_stat_mac_ufl,                             stats_tx.tx_stat_ifhcoutdiscards,                      b_is_to ) ; // EMAC 0
       LM_STATS_PMF_TO_FROM( rx_stat_dot3statscarriersenseerrors,          stats_rx.rx_stat_dot3statscarriersenseerrors,          b_is_to ) ; // BMAC 0

    }

    // NIG
    if( b_is_to)
    {
        LM_STATS_64_TO_HI_LO( stats_hw->nig.brb_discard, mcp_port->brb_drop ) ;
    }
    else
    {
        LM_STATS_HI_LO_TO_64( mcp_port->brb_drop, stats_hw->nig.brb_discard ) ;
    }
}

/*
 *Function Name:lm_stats_port_zero
 *
 *Parameters:
 *
 *Description:
 *  This function should be called by first function on port (PMF) - zeros MCP scatrch pad
 *Returns:
 *
 */
lm_status_t lm_stats_port_zero( IN struct _lm_device_t* pdev )
{
    u64_t            val        = 0 ;
    lm_status_t      lm_status  = LM_STATUS_SUCCESS ;

    if CHK_NULL(pdev)
    {
        return LM_STATUS_INVALID_PARAMETER;
    }

    if( 0 == pdev->vars.fw_port_stats_ptr )
    {
        /* This could happen and therefore is not considered an error */
        return LM_STATUS_SUCCESS;
    }

    // Calculate the size to be written through DMAE
    val = sizeof(pdev->vars.stats.stats_mirror.stats_mcp_port) ;
    val = val/sizeof(u32_t) ;

    // This code section must be under phy lock!
    REG_WR_DMAE_LEN_ZERO(pdev,
                         pdev->vars.fw_port_stats_ptr,
                         (u16_t)val ) ;

    return lm_status ;
}

/*
 *Function Name:lm_stats_port_save
 *
 *Parameters:
 *
 *Description:
 *  This function should be called before PMF is unloaded in order to preserve statitiscs for the next PMF
 *  ASSUMPTION: function must be called under PHY_LOCK (since it uses REG_WR_DMAE interface)
 *  ASSUMPTION: link can not change at this point and until PMF is down
 *Returns:
 *
 */
lm_status_t lm_stats_port_save( IN struct _lm_device_t* pdev )
{
    u64_t              val        = 0 ;
    lm_status_t        lm_status  = LM_STATUS_SUCCESS ;
    host_port_stats_t* mcp_port   = NULL ;

    if CHK_NULL(pdev)
    {
        return LM_STATUS_INVALID_PARAMETER;
    }

    if( 0 == pdev->vars.fw_port_stats_ptr )
    {
        /* This could happen and therefore is not considered an error */
        return LM_STATUS_SUCCESS;
    }

    mcp_port = &pdev->vars.stats.stats_mirror.stats_mcp_port ;

    lm_stats_port_to_from( pdev, TRUE ) ;

    // Calculate the size to be written through DMAE
    val = sizeof(pdev->vars.stats.stats_mirror.stats_mcp_port) ;
    val = val/sizeof(u32_t) ;
    pdev->vars.stats.stats_mirror.stats_mcp_port.host_port_stats_end = ++pdev->vars.stats.stats_mirror.stats_mcp_port.host_port_stats_start ;

    // This code section must be under phy lock!
    REG_WR_DMAE_LEN(pdev,
                    pdev->vars.fw_port_stats_ptr,
                    mcp_port,
                    (u16_t)val ) ;

    return lm_status ;
}

/*
 *Function Name:lm_stats_port_load
 *
 *Parameters:
 *
 *Description:
 *  This function should be called before a new PMF is loaded in order to restore statitiscs from the previous PMF
 *  vars.is_pmf should be set to TRUE only after this function completed!
 *  ASSUMPTION: function must be called under PHY_LOCK (since it uses REG_RD_DMAE interface)
 *  ASSUMPTION: link can not change at this point and until PMF is up
 *Returns:
 *
 */
lm_status_t lm_stats_port_load( IN struct _lm_device_t* pdev )
{
    u64_t              val        = 0 ;
    lm_status_t        lm_status  = LM_STATUS_SUCCESS ;
    host_port_stats_t* mcp_port   = NULL ;

    if CHK_NULL(pdev)
    {
        return LM_STATUS_INVALID_PARAMETER;
    }

    if( 0 == pdev->vars.fw_port_stats_ptr )
    {
        /* This could happen and therefore is not considered an error */
        return LM_STATUS_SUCCESS;
    }

    mcp_port = &pdev->vars.stats.stats_mirror.stats_mcp_port ;

    // Calculate the size to be written through DMAE
    val = sizeof(pdev->vars.stats.stats_mirror.stats_mcp_port) ;
    val = val/sizeof(u32_t) ;

    pdev->vars.stats.stats_mirror.stats_mcp_port.host_port_stats_end = ++pdev->vars.stats.stats_mirror.stats_mcp_port.host_port_stats_start ;

    // This code section must be under phy lock!
    REG_RD_DMAE_LEN(pdev,
                    pdev->vars.fw_port_stats_ptr,
                    mcp_port,
                    (u16_t)val ) ;

    lm_stats_port_to_from( pdev, FALSE ) ;

    return lm_status ;
}

/*
 *------------------------------------------------------------------------
 * lm_stats_mgmt_assign
 *
 * write values from mgmt structures into func and port  base structure
 * NOTE: function must be called under PHY_LOCK (since it uses REG_RD_DMAE interface)
 *------------------------------------------------------------------------
 */
void lm_stats_mgmt_assign( IN struct _lm_device_t* pdev )
{
    if CHK_NULL(pdev)
    {
        return;
    }

    if ( GET_FLAGS(pdev->params.test_mode, TEST_MODE_NO_MCP ) )
    {
        return;
    }

    if( pdev->vars.fw_func_stats_ptr )
    {
        lm_stats_mgmt_assign_func(pdev);
    }
    if( pdev->vars.fw_port_stats_ptr )
    {
        // only PMF should assign port statistics
        if( IS_PMF(pdev) )
        {
            lm_stats_port_save(pdev);
        }
    }
}

/*
 *Function Name:lm_stats_on_pmf_update
 *
 *Parameters:
 *  b_on:
 *  TRUE  - the device is beocming now a PMF
 *  FALSE - the device is now going down and transfering PMF to another device
 *Description:
 *  the function should be called under PHY LOCK.
 *  TRUE when a device becoming a PMF and before the link status changed from last state when previous PMF was down after call for mcp driver load
 *  FALSE when a device going down and after the link status saved and can not be changed (interrupts are disabled) before call for mcp driver unload
 *Returns:
 *
 */
lm_status_t lm_stats_on_pmf_update( struct _lm_device_t* pdev, IN u8_t b_on )
{
    lm_status_t lm_status  = LM_STATUS_SUCCESS ;

    if CHK_NULL(pdev)
    {
        return LM_STATUS_INVALID_PARAMETER;
    }

    if( b_on )
    {
        lm_status = lm_stats_port_load( pdev );
    }
    else
    {
        lm_status = lm_stats_on_update_state(pdev);

        // check for success, but link down is a valid situation!
        DbgBreakIf( ( LM_STATUS_SUCCESS != lm_status ) && ( LM_STATUS_LINK_DOWN != lm_status ) );

        // we need to save port stats only if link is down
        // if link is up, it was already made on call to lm_stats_on_update_state.
        if( LM_STATUS_LINK_DOWN == lm_status )
        {
            lm_status = lm_stats_port_save( pdev );
        }
    }
    return lm_status ;
}
/*
 *Function Name:lm_stats_on_pmf_init
 *
 *Parameters:
 *
 *Description:
 *  call this function under PHY LOCK when FIRST ever PMF is on
 *Returns:
 *
 */
lm_status_t lm_stats_on_pmf_init( struct _lm_device_t* pdev )
{
    lm_status_t lm_status  = LM_STATUS_SUCCESS ;
    if CHK_NULL(pdev)
    {
        return LM_STATUS_INVALID_PARAMETER;
    }

    lm_status = lm_stats_port_zero( pdev ) ;

    return lm_status ;

}

lm_status_t lm_stats_hw_collect( struct _lm_device_t* pdev )
{
    lm_status_t lm_status = LM_STATUS_SUCCESS;
    const u32_t pkt0      = PORT_ID(pdev) ? NIG_REG_STAT1_EGRESS_MAC_PKT0 : NIG_REG_STAT0_EGRESS_MAC_PKT0  ;
    const u32_t pkt1      = PORT_ID(pdev) ? NIG_REG_STAT1_EGRESS_MAC_PKT1 : NIG_REG_STAT0_EGRESS_MAC_PKT1  ;

    // call the dmae commands sequance
    lm_status = lm_stats_dmae( pdev ) ;
    if( LM_STATUS_SUCCESS != lm_status )
    {
        return lm_status;
    }

    // read two more NIG registers in the regular way - on E3 these do not exist!!!
    if (!CHIP_IS_E3(pdev))
    {
        REG_RD_DMAE( pdev,  pkt0, &pdev->vars.stats.stats_collect.stats_hw.nig_ex_stats_query.egress_mac_pkt0 );
        REG_RD_DMAE( pdev,  pkt1, &pdev->vars.stats.stats_collect.stats_hw.nig_ex_stats_query.egress_mac_pkt1 );
    }

    return lm_status ;
}

/*
 *Function Name:lm_stats_init_port_part
 *
 *Parameters:
 *
 *Description:
 *  call this function under PHY LOCK on port init
 *Returns:
 *
 */
void lm_stats_init_port_part( struct _lm_device_t* pdev )
{
    lm_stats_mgmt_clear_all_func(pdev);
}

/*
 *Function Name:lm_stats_init_port_part
 *
 *Parameters:
 *
 *Description:
 *  call this function under PHY LOCK on function init
 *Returns:
 *
 */
void lm_stats_init_func_part( struct _lm_device_t* pdev )
{
    if (IS_PMF(pdev) && IS_MULTI_VNIC(pdev))
    {
        lm_stats_on_pmf_init(pdev);
    }
    lm_stats_mgmt_read_func_base(pdev);
}
