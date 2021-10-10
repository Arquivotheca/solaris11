/*******************************************************************************
 * The information contained in this file is confidential and proprietary to
 * Broadcom Corporation.  No part of this file may be reproduced or
 * distributed, in any form or by any means for any purpose, without the
 * express written permission of Broadcom Corporation.
 *
 * (c) COPYRIGHT 2001-2011 Broadcom Corporation, ALL RIGHTS RESERVED.
 *
 *
 * Module Description:
 *
 *
 *
 ******************************************************************************/

#include "lm5710.h"
#include "577xx_int_offsets.h"
#include "license.h"
#include "mcp_shmem.h"

#if !defined(__LINUX) && !defined(__SunOS)
// disable warning C4127 (conditional expression is constant)
// for this file (relevant when compiling with W4 warning level)
#pragma warning( disable : 4127 )
#endif /* __LINUX */

#if !defined(__LINUX) && !defined(__SunOS)
#pragma warning( default : 4127 )
#endif

#include "mm.h"

#include "aeu_inputs.h"
#include "ecore_init.h"
#include "ecore_init_ops.h"

#include "context.h"
#include "command.h"
#include "general_atten_bits.h"

#include "bd_chain.h"
#include "ecore_common.h"
#include "ecore_sp_verbs.h"

#if defined(DOS) && !defined(UEFI)
#include <dos.h>
#endif
#ifdef UEFI
extern int sleep(int);
#endif

#ifdef _VBD_CMD_
extern volatile u32_t *g_everest_sim_flags_ptr;
#define EVEREST_SIM_RAMROD 0x01
#endif


void lm_int_igu_ack_sb(lm_device_t *pdev, u8_t rss_id, u8_t storm_id, u16_t sb_index, u8_t int_op, u8_t is_update_idx);
static void lm_int_igu_sb_cleanup(lm_device_t *pdev, u8 igu_sb_id);
void lm_reset_common_part(struct _lm_device_t *pdev);
void lm_reset_path( IN struct _lm_device_t *pdev,
                     IN const  u8_t          b_with_nig );

u32_t reg_wait_verify_val(struct _lm_device_t * pdev, u32_t reg_offset, u32_t excpected_val, u32_t total_wait_time_ms )
{
    u32_t val            = 0 ;
    u32_t wait_cnt       = 0 ;
    u32_t wait_cnt_limit = total_wait_time_ms/DEFAULT_WAIT_INTERVAL_MICSEC ;
    if( wait_cnt_limit == 0 )
    {
        wait_cnt_limit = 1;
    }
    val=REG_RD(pdev,reg_offset);
    while( (val != excpected_val) && (wait_cnt++ != wait_cnt_limit) )
    {
        mm_wait(pdev, DEFAULT_WAIT_INTERVAL_MICSEC) ;
        val=REG_RD(pdev,reg_offset);
    }
    DbgBreakIf(val != excpected_val);
    return wait_cnt;
}

#define MASK_01010101 (((unsigned int)(-1))/3)
#define MASK_00110011 (((unsigned int)(-1))/5)
#define MASK_00001111 (((unsigned int)(-1))/17)


#define ECORE_INIT_COMN(_pdev, _block) \
    ecore_init_block(_pdev, _block##_BLOCK, COMMON_STAGE)

#define ECORE_INIT_PORT(_pdev, _block) \
     ecore_init_block(_pdev, _block##_BLOCK, PORT0_STAGE + PORT_ID(_pdev))

#define ECORE_INIT_FUNC(_pdev, _block) \
    ecore_init_block(_pdev, _block##_BLOCK, FUNC0_STAGE + FUNC_ID(_pdev))


static int ecore_gunzip(struct _lm_device_t *pdev, const u8 *zbuf, int len)
{
    /* TODO : Implement... */
    UNREFERENCED_PARAMETER_(pdev);
    UNREFERENCED_PARAMETER_(zbuf);
    UNREFERENCED_PARAMETER_(len);
    DbgBreakMsg("ECORE_GUNZIP NOT IMPLEMENTED\n");
    return FALSE;
}

u32_t count_bits(u32_t n)
{
    n = (n & MASK_01010101) + ((n >> 1) & MASK_01010101) ;
    n = (n & MASK_00110011) + ((n >> 2) & MASK_00110011) ;
    n = (n & MASK_00001111) + ((n >> 4) & MASK_00001111) ;

    return n % 255 ;
}

unsigned long log2_align(unsigned long n)
{
    unsigned long ret = n ? 1 : 0;
    unsigned long _n  = n >> 1;

    while (_n)
    {
        _n >>= 1;
        ret <<= 1;
    }

    if (ret < n)
        ret <<= 1;

    return ret;
}
/**
 * @description
 * Should be moved to a common file.
 * Calculates the lower align of power 2.
 * Values lower than 0 are returned directly.
 * @param n
 *
 * @return unsigned long
 * lower align of power 2.
 */
unsigned long power2_lower_align(unsigned long n)
{
    unsigned long ret = 0;
    if(0 == n)
    {
        return 0;
    }

    if(TRUE == POWER_OF_2(n))
    {
        // The number is already a power of 2.
        return n;
    }

    //Calculates the lower align of power 2.
    ret = log2_align(n);
    DbgBreakIf(FALSE == POWER_OF_2(ret));
    ret >>= 1;

    return ret;
}
/*
Log2
this function calculates rounded LOG2 of a certain number
e.g.: LOG2(1080) = 10 (2^10=1024)
*/
u32_t LOG2(u32_t v){
    u32_t r=0;
    while (v >>= 1) {
        r++;
    }
    return r;
}

void lm_nullify_intmem(struct _lm_device_t *pdev)
{
    u32_t storms[4] = { /* GRC addr of storm intmem per storm */
        0x1a0000, /*T*/
        0x220000, /*C*/
        0x2a0000, /*X*/
        0x320000  /*U*/
    };
    u32_t       i                  = 0;
    u32_t       offset             = 0;
    const u32_t write_size_dw_ram0 = STORM_INTMEM_SIZE(pdev);
    const u32_t write_size_dw_ram1 = SEM_FAST_REG_INT_RAM1_SIZE;

    ASSERT_STATIC(ARRSIZE(pdev->vars.zero_buffer) >= DMAE_MAX_RW_SIZE_STATIC );    

    for (i = 0; i < ARRSIZE(storms); i++)
    {
        offset = storms[i];
        REG_WR_DMAE_LEN_ZERO(pdev, offset, write_size_dw_ram0) ;

        // In E2 we want to nullify also RAM1 which is RAM0 offset + SEM_FAST_REG_INT_RAM1
        if CHIP_IS_E2(pdev)
        {
            offset += SEM_FAST_REG_INT_RAM1; // This value is correct (and needed) for E2 only
            REG_WR_DMAE_LEN_ZERO(pdev, offset , write_size_dw_ram1 );
        }
    }
}

 /* In E2 there is a bug in the timers block that can cause function 6 / 7 (i.e. vnic3) to start
 * even if it is marked as "scan-off". This occurs when a different function (func2,3) is being marked
 * as "scan-off". Real-life scenario for example: if a driver is being load-unloaded while func6,7 are down.
 * this will cause the timer to access the ilt, translate to a logical address and send a request to read/write.
 * since the ilt for the function that is down is not valid, this will cause a translation error which is unrecoverable.
 * The Workaround is intended to make sure that when this happens nothing fatal will occur. The workaround:
1.  First PF driver which loads on a path will:
    a.  After taking the chip out of reset, by using pretend, it will write "0" to the following registers of the other vnics.
        REG_WR(pdev, PGLUE_B_REG_INTERNAL_PFID_ENABLE_MASTER, 0);
        REG_WR(pdev, CFC_REG_WEAK_ENABLE_PF,0);
        REG_WR(pdev, CFC_REG_STRONG_ENABLE_PF,0);
        And for itself it will write '1' to PGLUE_B_REG_INTERNAL_PFID_ENABLE_MASTER to enable dmae-operations (writing to pram for example.)
        note: can be done for only function 6,7 but cleaner this way.
    b.  Write zero+valid to the entire ILT.
    c.  Init the first_timers_ilt_entry, last_timers_ilt_entry of VNIC3 (of that port). The range allocated will be the entire ILT. This is needed to prevent  ILT range error.
2.  Any PF driver load flow:
    a.  ILT update with the physical addresses of the allocated logical pages.
    b.  Wait 20msec. - note that this timeout is needed to make sure there are no requests in one of the PXP internal queues with "old" ILT addresses.
    c.  PF enable in the PGLC.
    d.  Clear the was_error of the PF in the PGLC. (could have occured while driver was down)
    e.  PF enable in the CFC (WEAK + STRONG)
    f.  Timers scan enable
3.  PF driver unload flow:
    a.  Clear the Timers scan_en.
    b.  Polling for scan_on=0 for that PF.
    c.  Clear the PF enable bit in the PXP.
    d.  Clear the PF enable in the CFC (WEAK + STRONG)
    e.  Write zero+valid to all ILT entries (The valid bit must stay set)
    f.  If this is VNIC 3 of a port then also init first_timers_ilt_entry to zero and last_timers_ilt_entry to the last enrty in the ILT.

Notes:
"   Currently the PF error in the PGLC is non recoverable. In the future the there will be a recovery routine for this error. Currently attention is masked.
"   Having an MCP lock on the load/unload process does not guarantee that there is no Timer disable during Func6/7 enable. This is because the Timers scan is currently being cleared by the MCP on FLR.
"   Step 2.d can be done only for PF6/7 and the driver can also check if there is error before clearing it. But the flow above is simpler and more general.
"   All ILT entries are written by zero+valid and not just PF6/7 ILT entries since in the future the ILT entries allocation for PF-s might be dynamic.
*/



// for PRS BRB mem setup
static void init_nig_pkt(struct _lm_device_t *pdev)
{
    u32 wb_write[3] = {0} ;

    wb_write[0] = 0x55555555 ;
    wb_write[1] = 0x55555555 ;
    wb_write[2] = 0x20 ;

    // TBD: consider use DMAE to these writes

    // Ethernet source and destination addresses
    REG_WR_IND(pdev,NIG_REG_DEBUG_PACKET_LB,  wb_write[0]);
    REG_WR_IND(pdev,NIG_REG_DEBUG_PACKET_LB+4,wb_write[1]);
    // #SOP
    REG_WR_IND(pdev,NIG_REG_DEBUG_PACKET_LB+8,wb_write[2]);

    wb_write[0] = 0x09000000 ;
    wb_write[1] = 0x55555555 ;
    wb_write[2] = 0x10 ;

    // NON-IP protocol
    REG_WR_IND(pdev,NIG_REG_DEBUG_PACKET_LB,  wb_write[0]);
    REG_WR_IND(pdev,NIG_REG_DEBUG_PACKET_LB+4,wb_write[1]);
    // EOP, eop_bvalid = 0
    REG_WR_IND(pdev,NIG_REG_DEBUG_PACKET_LB+8,wb_write[2]);
}

static void lm_setup_fan_failure_detection(struct _lm_device_t *pdev)
{
    u32_t             val = 0;
    lm_status_t lm_status = LM_STATUS_SUCCESS;
    u8_t             port = 0;
    u8_t      is_required = FALSE;
    u32            offset = 0;

    offset = OFFSETOF(shmem_region_t, dev_info.shared_hw_config.config2) ;

    LM_SHMEM_READ(pdev, offset, &val);

    val &= SHARED_HW_CFG_FAN_FAILURE_MASK;

    switch(val)
    {
    case SHARED_HW_CFG_FAN_FAILURE_PHY_TYPE:
        {
            /*
             * The fan failure mechanism is usually related to the PHY type since
             * the power consumption of the board is effected by the PHY. Currently,
             * fan is required for most designs with SFX7101, BCM8727 and BCM8481.
             */
            for (port = PORT_0; port < PORT_MAX; port++)
            {
                is_required |= elink_fan_failure_det_req(pdev, pdev->hw_info.shmem_base, pdev->hw_info.shmem_base2, port);
            }
        }
        break;

    case SHARED_HW_CFG_FAN_FAILURE_ENABLED:
        is_required = TRUE;
        break;

    case SHARED_HW_CFG_FAN_FAILURE_DISABLED:
    default:
        break;
    }

    DbgMessage2(pdev, WARN, "lm_setup_fan_failure_detection: cfg=0x%x is_required=%d\n", val, is_required );

    if (!is_required)
    {
        return;
    }

    // read spio5 in order to make it input collaterally - we don't care of the returned value
    // MCP does the same
    lm_status = lm_spio_read( pdev, 5, &val ) ;
    if( LM_STATUS_SUCCESS != lm_status )
    {
        DbgBreakIf(1) ;
    }

    // We write here since value changes from 1 to 0
    val = REG_RD(pdev,MISC_REG_SPIO_INT) ;
    val |= (1<<(16+5)) ;
    REG_WR(pdev,MISC_REG_SPIO_INT, val ) ;

    // enable the SPIO_INT 5 to signal the IGU
    val = REG_RD(pdev,MISC_REG_SPIO_EVENT_EN) ;
    val |= (1<<5) ;
    REG_WR(pdev,MISC_REG_SPIO_EVENT_EN, val ) ;
}

/* This function clears the pf enable bit in the pglue-b and cfc, to make sure that if any requests
 * are made on this function they will be dropped before they can cause any fatal errors. */
static void clear_pf_enable(lm_device_t *pdev)
{
    REG_WR(pdev, PGLUE_B_REG_INTERNAL_PFID_ENABLE_MASTER, 0);
    REG_WR(pdev, CFC_REG_WEAK_ENABLE_PF,0);
    //REG_WR(pdev, CFC_REG_STRONG_ENABLE_PF,0);
}

static void init_misc_common(lm_device_t *pdev)
{
    u32_t reset_reg_1_val = 0xffffffff;
    u32_t reset_reg_2_val = 0xfffc;

    if(ERR_IF(!pdev)) {
        return;
    }

    /* Take Chip Blocks out of Reset */
    if (CHIP_IS_E3(pdev))
    {
        // New blocks that need to be taken out of reset
        // Mstat0 - bit 24 of RESET_REG_2
        // Mstat1 - bit 25 of RESET_REG_2
        reset_reg_2_val |= ((1<<25) | (1<<24)) ;
    }

    REG_WR(pdev,GRCBASE_MISC+MISC_REGISTERS_RESET_REG_1_SET,reset_reg_1_val);
    // BMAC is not out of reset
    REG_WR(pdev,GRCBASE_MISC+MISC_REGISTERS_RESET_REG_2_SET,reset_reg_2_val);

    ECORE_INIT_COMN(pdev, MISC);

    if (!CHIP_IS_E1(pdev)) /* multi-function not supported in E1 */
    {
        // init multifunction_mode reg. For E3 - this is done in the port-phase, and can differ between ports...
        if (CHIP_IS_E2(pdev) || CHIP_IS_E1H(pdev))
        {
            REG_WR(pdev,MISC_REG_E1HMF_MODE , (pdev->params.multi_vnics_mode ? 1 : 0));
        }
        // TBD: E1H, consider disabling grc timeout enable
    }

    /* Chip is out of reset */

    /* Timers bug workaround. The chip has just been taken out of reset. We need to make sure that all the functions (except this one)
     * are marked as disabled in the PGLC + CFC to avoid timer bug to occur */
    if (!CHIP_IS_E1x(pdev)) {
        u8_t abs_func_id;

        /* 4-port mode or 2-port mode we need to turn of master-enable for everyone, after that, turn it back on for self.
         * so, we disregard multi-function or not, and always disable for all functions on the given path, this means 0,2,4,6 for
         * path 0 and 1,3,5,7 for path 1 */
        for (abs_func_id = PATH_ID(pdev); abs_func_id  < E2_FUNC_MAX*2; abs_func_id+=2) {
            if (abs_func_id == ABS_FUNC_ID(pdev)) {
                REG_WR(pdev, PGLUE_B_REG_INTERNAL_PFID_ENABLE_MASTER, 1);
                continue;
            }
            lm_pretend_func(pdev, abs_func_id);

            clear_pf_enable(pdev);

            lm_pretend_func(pdev, ABS_FUNC_ID(pdev));
        }
    }

}

static void init_misc_port(lm_device_t *pdev)
{
    ECORE_INIT_PORT(pdev, MISC);

    // init multifunction_mode reg. For E3 - this is done in the port-phase, and can differ between ports... E2 and before is done in common phase
    if (CHIP_IS_E3(pdev))
    {
        REG_WR(pdev, (PORT_ID(pdev)? MISC_REG_E1HMF_MODE_P1 : MISC_REG_E1HMF_MODE_P0), (pdev->params.multi_vnics_mode ? 1 : 0));
    }


}

static void init_aeu_port(lm_device_t *pdev)
{
    u32_t offset = 0;
    u32_t val    = 0;

    if(ERR_IF(!pdev)) {
        return;
    }

    ECORE_INIT_PORT(pdev, MISC_AEU);

    // init aeu_mask_attn_func_0/1:
    // - SF mode: bits 3-7 are masked. only bits 0-2 are in use
    // - MF mode: bit 3 is masked. bits 0-2 are in use as in SF.
    //            bits 4-7 are used for "per vnic group attention"
    val = (pdev->params.multi_vnics_mode ? 0xF7 : 0x7);
    if(IS_DCB_SUPPORTED(pdev))//TODO:Shayh 17/12/09: In the future this if will be for Everest and wil change to !IS_E1
    {
        // For DCBX we need to enable group 4 even in SF.
        val |= 0x10;
    }
    REG_WR(pdev, (PORT_ID(pdev) ? MISC_REG_AEU_MASK_ATTN_FUNC_1 : MISC_REG_AEU_MASK_ATTN_FUNC_0), val);

    // If SPIO5 is set to generate interrupts, enable it for this port
    val = REG_RD(pdev, MISC_REG_SPIO_EVENT_EN);
    if (val & (1 << MISC_REGISTERS_SPIO_5)) {
        // fan failure handling
        offset = (PORT_ID(pdev) ? MISC_REG_AEU_ENABLE1_FUNC_1_OUT_0 : MISC_REG_AEU_ENABLE1_FUNC_0_OUT_0) ;
        val=REG_RD(pdev, offset );
        // add SPIO5 to group
        SET_FLAGS(val, AEU_INPUTS_ATTN_BITS_SPIO5 ) ;
        REG_WR(pdev, offset, val ) ;
    }

    if (pdev->params.enable_error_recovery && CHIP_IS_E2(pdev))
    {
        /* Under error recovery we use general attention 20 (bit 18) therefore
         * we need to enable it*/
        offset = (PORT_ID(pdev) ? MISC_REG_AEU_ENABLE4_FUNC_1_OUT_0 : MISC_REG_AEU_ENABLE4_FUNC_0_OUT_0) ;
        val = REG_RD(pdev, offset);
        val |= AEU_INPUTS_ATTN_BITS_GRC_MAPPED_GENERAL_ATTN20;
        REG_WR(pdev, offset, val);
    }
}

static void init_pxp_common(lm_device_t *pdev)
{
    if(ERR_IF(!pdev)) {
        return;
    }

    ECORE_INIT_COMN(pdev, PXP);
    if( CHIP_NUM(pdev) <= CHIP_NUM_5710 )
    {
        // enable hw interrupt from PXP on usdm overflow bit 16 on INT_MASK_0
        REG_WR(pdev,PXP_REG_PXP_INT_MASK_0,0);
    }
}


static void init_pxp2_common(lm_device_t *pdev)
{
    u32_t wait_ms = ((CHIP_REV(pdev) != CHIP_REV_EMUL) && (CHIP_REV(pdev) != CHIP_REV_FPGA)) ? 200 : 200000;
    u32_t i;

    if(ERR_IF(!pdev)) {
        return;
    }

    // static init
    ECORE_INIT_COMN(pdev, PXP2);

    // runtime init
#ifdef __BIG_ENDIAN
    REG_WR(pdev,  PXP2_REG_RQ_QM_ENDIAN_M, 1);
    REG_WR(pdev,  PXP2_REG_RQ_TM_ENDIAN_M, 1);
    REG_WR(pdev,  PXP2_REG_RQ_SRC_ENDIAN_M, 1);
    REG_WR(pdev,  PXP2_REG_RQ_CDU_ENDIAN_M, 1);
    REG_WR(pdev,  PXP2_REG_RQ_DBG_ENDIAN_M, 1);

    REG_WR(pdev,  PXP2_REG_RD_QM_SWAP_MODE, 1);
    REG_WR(pdev,  PXP2_REG_RD_TM_SWAP_MODE, 1);
    REG_WR(pdev,  PXP2_REG_RD_SRC_SWAP_MODE, 1);
    REG_WR(pdev,  PXP2_REG_RD_CDURD_SWAP_MODE, 1);
#endif
    ecore_init_pxp_arb(pdev, pdev->hw_info.max_read_req_size, pdev->hw_info.max_payload_size);

    REG_WR(pdev,PXP2_REG_RQ_CDU_P_SIZE,LOG2(pdev->params.ilt_client_page_size/LM_PAGE_SIZE));
    REG_WR(pdev,PXP2_REG_RQ_TM_P_SIZE,LOG2(pdev->params.ilt_client_page_size/LM_PAGE_SIZE));
    REG_WR(pdev,PXP2_REG_RQ_QM_P_SIZE,LOG2(pdev->params.ilt_client_page_size/LM_PAGE_SIZE));
    REG_WR(pdev,PXP2_REG_RQ_SRC_P_SIZE,LOG2(pdev->params.ilt_client_page_size/LM_PAGE_SIZE));

    // on E 1.5 fpga set number of max pcie tag number to 5
    if ((CHIP_REV(pdev) == CHIP_REV_FPGA ) && CHIP_IS_E1H(pdev)) {
        REG_WR(pdev,PXP2_REG_PGL_TAGS_LIMIT,0x1);
    }

    // verify PXP init finished (we want to use the DMAE)
    REG_WAIT_VERIFY_VAL(pdev,PXP2_REG_RQ_CFG_DONE, 1, wait_ms);
    REG_WAIT_VERIFY_VAL(pdev,PXP2_REG_RD_INIT_DONE,1, wait_ms);

    REG_WR(pdev,PXP2_REG_RQ_DISABLE_INPUTS,0);
    REG_WR(pdev,PXP2_REG_RD_DISABLE_INPUTS,0);


    // the phys address is shifted right 12 bits and has an added 1=valid bit added to the 53rd bit
    // then since this is a wide register(TM) we split it into two 32 bit writes
    #define ONCHIP_ADDR1(x)   ((u32_t)( x>>12 & 0xFFFFFFFF ))
    #define ONCHIP_ADDR2(x)   ((u32_t)( 1<<20 | x>>44 ))
    #define ONCHIP_ADDR0_VALID() ((u32_t)( 1<<20 )) /* Address valued 0 with valid bit on. */

    #define PXP2_SET_FIRST_LAST_ILT(pdev, blk, first, last) \
    do { \
        if (CHIP_IS_E1(pdev)) { \
            REG_WR(pdev,(PORT_ID(pdev) ? PXP2_REG_PSWRQ_##blk##1_L2P: PXP2_REG_PSWRQ_##blk##0_L2P),((last)<<10 | (first))); \
        } else { \
            REG_WR(pdev,PXP2_REG_RQ_##blk##_FIRST_ILT,(first)); \
            REG_WR(pdev,PXP2_REG_RQ_##blk##_LAST_ILT,(last)); \
        } \
    } while(0)

    /* Timers bug workaround E2 only. We need to set the entire ILT to have entries with value "0" and valid bit on.
     * This needs to be done by the first PF that is loaded in a path (i.e. common phase)
     */
    if (!CHIP_IS_E1x(pdev)) {
        /* Step 1: set zeroes to all ilt page entries with valid bit on */
        for (i=0; i < ILT_NUM_PAGE_ENTRIES; i++){
            REG_WR(pdev,PXP2_REG_RQ_ONCHIP_AT_B0+i*8,  0);
            REG_WR(pdev,PXP2_REG_RQ_ONCHIP_AT_B0+i*8+4,ONCHIP_ADDR0_VALID());
        }
        /* Step 2: set the timers first/last ilt entry to point to the entire range to prevent ILT range error */
        if (pdev->params.multi_vnics_mode) {
            lm_pretend_func(pdev, (PATH_ID(pdev) + 6));
            PXP2_SET_FIRST_LAST_ILT(pdev, TM,  0, ILT_NUM_PAGE_ENTRIES - 1);
            lm_pretend_func(pdev, ABS_FUNC_ID(pdev));
        }

        /* set E2 HW for 64B cache line alignment */
        /* TODO: align according to runtime cache line size */
        REG_WR(pdev,PXP2_REG_RQ_DRAM_ALIGN,1); /* for 128B cache line value should be 2 */
        REG_WR(pdev,PXP2_REG_RQ_DRAM_ALIGN_RD,1); /* for 128B cache line value should be 2 */
        REG_WR(pdev,PXP2_REG_RQ_DRAM_ALIGN_SEL,1);
    }
}

static void init_pglue_b_common(lm_device_t *pdev)
{
    ECORE_INIT_COMN(pdev, PGLUE_B);
}

static void init_atc_common(lm_device_t *pdev)
{
    u32_t wait_ms = ((CHIP_REV(pdev) != CHIP_REV_EMUL) && (CHIP_REV(pdev) != CHIP_REV_FPGA)) ? 200 : 200000;
    if (!CHIP_IS_E1x(pdev)) {
        ECORE_INIT_COMN(pdev, ATC);

        REG_WAIT_VERIFY_VAL(pdev, ATC_REG_ATC_INIT_DONE ,1,wait_ms );
    }
}

static void init_pxp2_func(lm_device_t *pdev)
{
    #define PXP2_NUM_TABLES 4
    lm_address_t * addr_table[PXP2_NUM_TABLES];
    u32_t           num_pages[PXP2_NUM_TABLES];
    u32_t           first_ilt[PXP2_NUM_TABLES];
    u32_t           last_ilt[PXP2_NUM_TABLES];
    u32_t rq_onchip_at_reg;
    u32_t i,j,k,temp;

    ECORE_INIT_FUNC(pdev, PXP2);

    addr_table[0] = pdev->vars.context_cdu_phys_addr_table;
    addr_table[1] = pdev->vars.timers_linear_phys_addr_table;
    addr_table[2] = pdev->vars.qm_queues_phys_addr_table;
    addr_table[3] = pdev->vars.searcher_t1_phys_addr_table;
    num_pages[0] = pdev->vars.context_cdu_num_pages;
    num_pages[1] = pdev->vars.timers_linear_num_pages;
    num_pages[2] = pdev->vars.qm_queues_num_pages;
    num_pages[3] = pdev->vars.searcher_t1_num_pages;

    temp = FUNC_ID(pdev) * ILT_NUM_PAGE_ENTRIES_PER_FUNC;
    rq_onchip_at_reg = CHIP_IS_E1(pdev) ? PXP2_REG_RQ_ONCHIP_AT : PXP2_REG_RQ_ONCHIP_AT_B0;

    for (k=0;k<PXP2_NUM_TABLES;k++) {
        // j is the first table entry line for this block temp is the number of the last written entry (each entry is 8 octets long)
        j=temp;
        for (i=0; i<num_pages[k]; temp++, i++){
            REG_WR_IND(pdev,rq_onchip_at_reg+temp*8,ONCHIP_ADDR1(addr_table[k][i].as_u64));
            REG_WR_IND(pdev,rq_onchip_at_reg+temp*8+4,ONCHIP_ADDR2(addr_table[k][i].as_u64));
        }
        first_ilt[k] = j;
        last_ilt[k] = (temp - 1);
    }
    DbgBreakIf(!(temp<((u32_t)ILT_NUM_PAGE_ENTRIES_PER_FUNC*(FUNC_ID(pdev)+1))));

    PXP2_SET_FIRST_LAST_ILT(pdev, CDU, first_ilt[0], last_ilt[0]);
    PXP2_SET_FIRST_LAST_ILT(pdev, TM,  first_ilt[1], last_ilt[1]);
    PXP2_SET_FIRST_LAST_ILT(pdev, QM,  first_ilt[2], last_ilt[2]);
    PXP2_SET_FIRST_LAST_ILT(pdev, SRC, first_ilt[3], last_ilt[3]);

    if (!CHIP_IS_E1x(pdev)) {
        /* Timers workaround bug: function init part. Need to wait 20msec after initializing ILT,
         * needed to make sure there are no requests in one of the PXP internal queues with "old" ILT addresses */
        mm_wait(pdev, 20000);
    }

}


static void uninit_pxp2_blk(lm_device_t *pdev)
{
    u32_t rq_onchip_at_reg, on_chip_addr2_val;
    u32_t k, temp;

    if(ERR_IF(!pdev)) {
        return;
    }


    /* clean ILT table
     * before doing that we must promise that all the ILT clients (CDU/TM/QM/SRC) of the
     * disabled function are not going to access the table anymore:
     * - TM: already disabled in "reset function part"
     * - CDU/QM: all L2/L4/L5 connections are already closed
     * - SRC: In order to make sure SRC request is not initiated:
     *    - in MF mode, we clean the ILT table in the per func phase, after LLH was already disabled
     *    - in SF mode, we clean the ILT table in the per port phase, after port link was already reset */

    temp = FUNC_ID(pdev) * ILT_NUM_PAGE_ENTRIES_PER_FUNC;
    rq_onchip_at_reg = CHIP_IS_E1(pdev) ? PXP2_REG_RQ_ONCHIP_AT : PXP2_REG_RQ_ONCHIP_AT_B0;
    on_chip_addr2_val = CHIP_IS_E1x(pdev)? 0 : ONCHIP_ADDR0_VALID();

    for (k=0;k<ILT_NUM_PAGE_ENTRIES_PER_FUNC;temp++,k++) {
        REG_WR_IND(pdev,rq_onchip_at_reg+temp*8,0);
        REG_WR_IND(pdev,rq_onchip_at_reg+temp*8+4,on_chip_addr2_val);
    }

    PXP2_SET_FIRST_LAST_ILT(pdev, CDU, 0, 0);
    PXP2_SET_FIRST_LAST_ILT(pdev, QM,  0, 0);
    PXP2_SET_FIRST_LAST_ILT(pdev, SRC, 0, 0);

    /* Timers workaround bug for E2 phase3: if this is vnic-3, we need to set the entire ilt range for this timers. */
    if (!CHIP_IS_E1x(pdev) && VNIC_ID(pdev) == 3) {
        PXP2_SET_FIRST_LAST_ILT(pdev, TM,  0, ILT_NUM_PAGE_ENTRIES - 1);
    } else {
        PXP2_SET_FIRST_LAST_ILT(pdev, TM,  0, 0);
    }
}

static void init_dmae_common(lm_device_t *pdev)
{
    if(ERR_IF(!pdev)) {
        return;
    }

    ECORE_INIT_COMN( pdev, DMAE);

    // write arbitrary buffer to DMAE, hw memory setup phase

    
    REG_WR_DMAE_LEN_ZERO(pdev,  TSEM_REG_PRAM, 8);
    pdev->vars.b_is_dmae_ready = TRUE ;
}

static void init_qm_common(lm_device_t *pdev)
{
    u8_t i, func;
    if(ERR_IF(!pdev)) {
        return;
    }

    ECORE_INIT_COMN( pdev, QM);
    if (CHIP_PORT_MODE(pdev) == LM_CHIP_PORT_MODE_4) {
        DbgMessage(pdev, FATAL, "Initializing 4-port QM-common\n");
        ECORE_INIT_COMN( pdev, QM_4PORT);
    }

    /* nullify PTRTBL */
    for (i=0; i<64; i++){
        REG_WR_IND(pdev,QM_REG_PTRTBL +8*i ,0);
        REG_WR_IND(pdev,QM_REG_PTRTBL +8*i +4 ,0);
    }

    /* nullify extended PTRTBL (E1H only) */
    if (CHIP_IS_E1H(pdev)) {
        for (i=0; i<64; i++){
            REG_WR_IND(pdev,QM_REG_PTRTBL_EXT_A +8*i ,0);
            REG_WR_IND(pdev,QM_REG_PTRTBL_EXT_A +8*i +4 ,0);
        }
    }

    /* softrest pulse */
    REG_WR(pdev,QM_REG_SOFT_RESET,1);
    REG_WR(pdev,QM_REG_SOFT_RESET,0);

    /* We initialize the QM with max_common_conns, this way, the value is identical for all queues and it saves
     * the driver the need for knowing the mapping of the physical queses to functions.
     * Since we assume  writing the same value to all queue entries, we can do this in the common phase and just initialize
     * all queues the same */
    /* physical queues mapping :
     *  E1 queues:
     *  - q[0-63].
     *  - initialized via QM_REG_BASEADDR and QM_REG_PTRTBL REG
     *  - port0 uses q[0-15], port1 uses q[32-47], q[16-31,48-63] are not used
     *
     *  E1.5 queues:
     *  - _ON TOP OF_ E1 queues !
     *  - q[64-127]
     **/

    /* Initialize QM Queues */
    #define QM_QUEUES_PER_FUNC 16

    /* To eliminate the need of the driver knowing the exact function --> queue mapping, we simply initialize all queues, even for E1
     * we initialize all 64 queues (as if we had 4 functions). For E1H we initialize the extension as well. */
    for (func = 0; func < 4; func++) {
        for (i = 0; i < QM_QUEUES_PER_FUNC; i++){
            REG_WR(pdev,QM_REG_BASEADDR +4*(func*QM_QUEUES_PER_FUNC+i) , pdev->hw_info.max_common_conns * 4*i);
        }
    }

    if (CHIP_IS_E1H(pdev)) {
        for (func = 0; func < 4; func++) {
            for (i=0; i<QM_QUEUES_PER_FUNC; i++){
                REG_WR(pdev,QM_REG_BASEADDR_EXT_A +4*(func*QM_QUEUES_PER_FUNC+i) , pdev->hw_info.max_common_conns * 4*i);
            }
        }
    }
}

static void init_qm_func(lm_device_t *pdev)
{
    ECORE_INIT_FUNC( pdev, QM);
    if (CHIP_PORT_MODE(pdev) == LM_CHIP_PORT_MODE_4) {
        DbgMessage(pdev, FATAL, "Initializing 4-port QM func\n");
        ECORE_INIT_FUNC( pdev, QM_4PORT);
    }

    if (!CHIP_IS_E1x(pdev)) {
        /* Array of PF Enable bits, each pf needs to set its own,
         * is set to 'zero' by MCP on PF FLR */
        REG_WR(pdev, QM_REG_PF_EN, 1);
    }
}

static void init_qm_port(lm_device_t *pdev)
{
    if(ERR_IF(!pdev)) {
        return;
    }

    ECORE_INIT_PORT(pdev, QM);

    if (CHIP_PORT_MODE(pdev) == LM_CHIP_PORT_MODE_4) {
        DbgMessage(pdev, FATAL, "Initializing 4-port QM PORT\n");
        ECORE_INIT_PORT( pdev, QM_4PORT);
    }

    /* The same for all functions on port, therefore we use the max_port_connections */
    REG_WR(pdev, (PORT_ID(pdev) ? QM_REG_CONNNUM_1 : QM_REG_CONNNUM_0), pdev->hw_info.max_common_conns/16 -1);
}

static void init_tm_port(lm_device_t *pdev)
{
    if(ERR_IF(!pdev)) {
        return;
    }

    ECORE_INIT_PORT(pdev, TIMERS);

    /* when more then 64K connections per _port_ are supported, we need to change the init value for LIN0/1_SCAN_TIME */
    REG_WR(pdev,(PORT_ID(pdev) ? TM_REG_LIN1_SCAN_TIME : TM_REG_LIN0_SCAN_TIME), 20);
    /* The same for all functions on port, therefore we need to use the max_port_connections */
    REG_WR(pdev,(PORT_ID(pdev) ? TM_REG_LIN1_MAX_ACTIVE_CID : TM_REG_LIN0_MAX_ACTIVE_CID), (pdev->hw_info.max_port_conns/32)-1);

}

static void init_dq_common(lm_device_t *pdev)
{
    if(ERR_IF(!pdev)) {
        return;
    }

    ECORE_INIT_COMN(pdev, DQ);

    /* Here there is a difference between E2 and E3... */
    if (CHIP_IS_E2(pdev))
    {
        REG_WR(pdev, DORQ_REG_OUTST_REQ, 0x4);
    }
    else if (CHIP_IS_E3(pdev))
    {
        REG_WR(pdev, DORQ_REG_OUTST_REQ, 0x8);
    }

    // TBD: consider setting to the OS page size
    REG_WR(pdev,DORQ_REG_DPM_CID_OFST,LM_DQ_CID_BITS);
    if (!(CHIP_REV(pdev) == CHIP_REV_FPGA || CHIP_REV(pdev) == CHIP_REV_EMUL))
    {
        // enable hw interrupt from doorbell Q
        REG_WR(pdev,DORQ_REG_DORQ_INT_MASK,0);
    }
}

static void init_dq_func(lm_device_t *pdev)
{
    ECORE_INIT_FUNC(pdev, DQ);
#ifdef VF_INVOLVED
    if (!CHIP_IS_E1x(pdev) && IS_BASIC_VIRT_MODE_MASTER_PFDEV(pdev)) {
        REG_WR(pdev, DORQ_REG_MAX_RVFID_SIZE, 6);       // As long as we want to use absolute VF-id number
        REG_WR(pdev, DORQ_REG_VF_NORM_VF_BASE, 0);      //(a VF-id that is unique within the port), like the id
                                                        //that is used by all HW blocks and FW

        REG_WR(pdev, DORQ_REG_VF_NORM_CID_BASE, LM_VF_CID_BASE);  /*64 for single connection.
                                                                    PF connections in the beginning (L2 connections),
                                                                    then VF connections, and then the rest of PF connections */

        REG_WR(pdev, DORQ_REG_VF_NORM_CID_WND_SIZE, LM_VF_CID_WND_SIZE); /* should reflect the maximal number of connections in a VF.
                                                                           0 for single connection  */

        ASSERT_STATIC(LM_DQ_CID_BITS >=  3);
        REG_WR(pdev, DORQ_REG_VF_NORM_CID_OFST, LM_DQ_CID_BITS - 3);    /*means the number of bits in a VF doorbell.
                                                                         For 8B doorbells it should be 0, 128B should be 4 */

        /*In addition, in order to configure the way that the DQ builds the CID,
          the driver should also configure the DQ security checks for the VFs,
          thresholds for VF-doorbells, VF CID range. In the first step it's possible
          to configure all these checks in a way that disables validation checks:
            DQ security checks for VFs - configure single rule (out of 16) with mask = 0x1 and value = 0x0.
            CID range - 0 to 0x1ffff
            VF doorbell thresholds - according to the DQ size. */

        REG_WR(pdev, DORQ_REG_VF_TYPE_MASK_0, 1);
        REG_WR(pdev, DORQ_REG_VF_TYPE_VALUE_0, 0);
        REG_WR(pdev, DORQ_REG_VF_TYPE_MIN_MCID_0, 0);
        REG_WR(pdev, DORQ_REG_VF_TYPE_MAX_MCID_0, 0x1ffff);


        REG_WR(pdev, DORQ_REG_VF_NORM_MAX_CID_COUNT, 0x20000);
        REG_WR(pdev, DORQ_REG_VF_USAGE_CT_LIMIT, 4);
    }
#endif
}

static void init_brb1_common(lm_device_t *pdev)
{
    u32_t i = 0;

    ECORE_INIT_COMN(pdev, BRB1);

    /* The following brb initializations are done by driver runtime for e2/e3 since the same init-values are used for e2/e3 and they
     * differ only in small location - such as this one... */
    if (!CHIP_IS_E1x(pdev))
    {
        REG_WR(pdev, BRB1_REG_SOFT_RESET, 0x1);
        if (CHIP_IS_E2(pdev))
        {

            for (i = 0; i < 511; i++ )
            {
                REG_WR(pdev, BRB1_REG_LL_RAM + 4*i, ((i + 1) << 13));
            }
            REG_WR(pdev, BRB1_REG_LL_RAM + 4*i, (i << 13));

            REG_WR(pdev, BRB1_REG_FREE_LIST_PRS_CRDT , 0);
            REG_WR(pdev, BRB1_REG_FREE_LIST_PRS_CRDT + 4, 511);
            REG_WR(pdev, BRB1_REG_FREE_LIST_PRS_CRDT + 8, 512);


        }
        else /* E3 HW*/
        {
            for (i = 0; i < 1023; i++ )
            {
                REG_WR(pdev, BRB1_REG_LL_RAM + 4*i, ((i + 1) << 13));
            }
            REG_WR(pdev, BRB1_REG_LL_RAM + 4*i, (i << 13));

            REG_WR(pdev, BRB1_REG_FREE_LIST_PRS_CRDT , 0);
            REG_WR(pdev, BRB1_REG_FREE_LIST_PRS_CRDT + 4, 1023);
            REG_WR(pdev, BRB1_REG_FREE_LIST_PRS_CRDT + 8, 1024);

        }
        REG_WR(pdev, BRB1_REG_SOFT_RESET, 0x0);
    }


    if (CHIP_PORT_MODE(pdev) == LM_CHIP_PORT_MODE_4) {
        REG_WR(pdev, BRB1_REG_FULL_LB_XOFF_THRESHOLD, 248);
        REG_WR(pdev, BRB1_REG_FULL_LB_XON_THRESHOLD, 328);
    }
}

static void init_pbf_common(lm_device_t *pdev)
{
    ECORE_INIT_COMN(pdev, PBF);

    if (!CHIP_IS_E1x(pdev))
    {
        if (IS_MF_NIV_MODE(pdev))
        {
            REG_WR(pdev, PBF_REG_HDRS_AFTER_BASIC, 0xE);
            REG_WR(pdev, PBF_REG_MUST_HAVE_HDRS, 0xA);
            REG_WR(pdev, PBF_REG_HDRS_AFTER_TAG_0, 0x6);
            REG_WR(pdev, PBF_REG_TAG_ETHERTYPE_0, 0x8926);
            REG_WR(pdev, PBF_REG_TAG_LEN_0, 0x4);
        }
        else
        {
            /* Ovlan exists only if we are in path multi-function + switch-dependent mode, in switch-independent there is no ovlan headers */
            REG_WR(pdev, PBF_REG_HDRS_AFTER_BASIC, (pdev->params.path_has_ovlan ? 7 : 6)); //Bit-map indicating which L2 hdrs may appear after the basic Ethernet header.
        }
    }
}

static void init_pbf_func(lm_device_t *pdev)
{
    ECORE_INIT_FUNC(pdev, PBF);
    if (!CHIP_IS_E1x(pdev))
    {
        REG_WR(pdev,PBF_REG_DISABLE_PF,0);
    }
}

static void init_brb_port(lm_device_t *pdev)
{
    u32_t low  = 0;
    u32_t high = 0;
    u8_t  port = 0;

    if(ERR_IF(!pdev)) {
        return;
    }

    port=PORT_ID(pdev);

    ECORE_INIT_PORT( pdev, BRB1);

    if (CHIP_IS_E1x(pdev))
    {
        // on E1H we do support enable pause
        if ((CHIP_REV(pdev) == CHIP_REV_EMUL) || ((CHIP_REV(pdev) == CHIP_REV_FPGA) && CHIP_IS_E1(pdev)))
        {
            // special emulation and FPGA values for pause no pause
            high = 513;
            low = 0;
        }
        else
        {
            if (IS_MULTI_VNIC(pdev))
            {
                // A - 24KB + MTU(in K) *4
                // A - 24*4 + 150; (9600*4)/256 - (mtu = jumbo = 9600)
                low = 246;
            }
            else
            {
                if (pdev->params.mtu_max <= 4096)
                {
                    // A - 40KB low = 40*4
                    low = 160;
                }
                else
                {
                    // A - 24KB + MTU(in K) *4
                    low = 96 + (pdev->params.mtu_max*4)/256;
                }
            }
            // B - 14KB High = low+14*4
            high = low + 56;
        }

        REG_WR(pdev,BRB1_REG_PAUSE_LOW_THRESHOLD_0+port*4,low);
        REG_WR(pdev,BRB1_REG_PAUSE_HIGH_THRESHOLD_0+port*4,high);
    }

    if (CHIP_PORT_MODE(pdev) == LM_CHIP_PORT_MODE_4) {

        REG_WR(pdev, BRB1_REG_PAUSE_0_XOFF_THRESHOLD_0 + PORT_ID(pdev)*8, 248);
        REG_WR(pdev, BRB1_REG_PAUSE_0_XON_THRESHOLD_0 + PORT_ID(pdev)*8, 328);
        REG_WR(pdev, (PORT_ID(pdev)?  BRB1_REG_MAC_GUARANTIED_1 : BRB1_REG_MAC_GUARANTIED_0), 40);

    }

}


static void init_prs_common(lm_device_t *pdev)
{
    if(ERR_IF(!pdev))
    {
        return;
    }

    ECORE_INIT_COMN( pdev, PRS);

    if (!CHIP_IS_E1(pdev))
    {
        REG_WR(pdev,PRS_REG_E1HOV_MODE, (pdev->params.path_has_ovlan ? 1 : 0));
    }

    if (!CHIP_IS_E1x(pdev))
    {
        if (IS_MF_NIV_MODE(pdev))
        {
            REG_WR(pdev, PRS_REG_HDRS_AFTER_BASIC, 0xE);
            REG_WR(pdev, PRS_REG_MUST_HAVE_HDRS, 0xA);
            REG_WR(pdev, PRS_REG_HDRS_AFTER_TAG_0, 0x6);
            REG_WR(pdev, PRS_REG_TAG_ETHERTYPE_0, 0x8926);
            REG_WR(pdev, PRS_REG_TAG_LEN_0, 0x4);
        }
        else
        {
            /* Ovlan exists only if we are in multi-function + switch-dependent mode, in switch-independent there is no ovlan headers */
            REG_WR(pdev, PRS_REG_HDRS_AFTER_BASIC, (pdev->params.path_has_ovlan ? 7 : 6)); //Bit-map indicating which L2 hdrs may appear after the basic Ethernet header.
        }
    }

}

static void init_prs_func(lm_device_t *pdev)
{
    if(ERR_IF(!pdev))
    {
        return;
    }

    ECORE_INIT_FUNC( pdev, PRS);
}


static void init_semi_common(lm_device_t *pdev)
{

    if (!CHIP_IS_E1x(pdev)) {
        /* reset VFC memories - relevant only for E2, has to be done before initialing semi blocks which also
         * initialize VFC blocks.  */
        REG_WR(pdev, TSEM_REG_FAST_MEMORY + VFC_REG_MEMORIES_RST,
               VFC_MEMORIES_RST_REG_CAM_RST |
               VFC_MEMORIES_RST_REG_RAM_RST);
        REG_WR(pdev, XSEM_REG_FAST_MEMORY + VFC_REG_MEMORIES_RST,
               VFC_MEMORIES_RST_REG_CAM_RST |
               VFC_MEMORIES_RST_REG_RAM_RST);
    }


    ECORE_INIT_COMN(pdev, TSEM);
    ECORE_INIT_COMN(pdev, CSEM);
    ECORE_INIT_COMN(pdev, USEM);
    ECORE_INIT_COMN(pdev, XSEM);
    if (CHIP_PORT_MODE(pdev) == LM_CHIP_PORT_MODE_4) {
        DbgMessage(pdev, FATAL, "Initializing 4-port XSEM common\n");
        ECORE_INIT_COMN(pdev, XSEM_4PORT);
    }
}

static void init_semi_port(lm_device_t *pdev)
{
    ECORE_INIT_PORT(pdev, TSEM);
    ECORE_INIT_PORT(pdev, USEM);
    ECORE_INIT_PORT(pdev, CSEM);
    ECORE_INIT_PORT(pdev, XSEM);
    if (CHIP_PORT_MODE(pdev) == LM_CHIP_PORT_MODE_4) {
        DbgMessage(pdev, FATAL, "Initializing 4-port XSEM port\n");
        ECORE_INIT_PORT(pdev, XSEM_4PORT);
    }

    // passive buffer REG setup
    /* TBD - E1H: consider moving to SEM init, if so, then maybe should be in "common" phase, if not, can it be removed for E1H ? */
    {
        u32_t kuku = 0;
        kuku= REG_RD(pdev,  XSEM_REG_PASSIVE_BUFFER);
        kuku = REG_RD(pdev,  XSEM_REG_PASSIVE_BUFFER + 4);
        kuku = REG_RD(pdev,  XSEM_REG_PASSIVE_BUFFER + 8);

        kuku = REG_RD(pdev,  CSEM_REG_PASSIVE_BUFFER );
        kuku = REG_RD(pdev,  CSEM_REG_PASSIVE_BUFFER + 4);
        kuku = REG_RD(pdev,  CSEM_REG_PASSIVE_BUFFER + 8);

        kuku = REG_RD(pdev,  TSEM_REG_PASSIVE_BUFFER );
        kuku = REG_RD(pdev,  TSEM_REG_PASSIVE_BUFFER + 4);
        kuku = REG_RD(pdev,  TSEM_REG_PASSIVE_BUFFER + 8);

        kuku = REG_RD(pdev,  USEM_REG_PASSIVE_BUFFER );
        kuku = REG_RD(pdev,  USEM_REG_PASSIVE_BUFFER + 4);
        kuku = REG_RD(pdev,  USEM_REG_PASSIVE_BUFFER + 8);
    }
}

static void init_semi_func(lm_device_t *pdev)
{
    ECORE_INIT_FUNC(pdev, TSEM);
    ECORE_INIT_FUNC(pdev, USEM);
    ECORE_INIT_FUNC(pdev, CSEM);
    ECORE_INIT_FUNC(pdev, XSEM);
    if (CHIP_PORT_MODE(pdev) == LM_CHIP_PORT_MODE_4) {
        DbgMessage(pdev, FATAL, "Initializing 4-port QM func\n");
        ECORE_INIT_FUNC(pdev, XSEM_4PORT);
    }

    if (!CHIP_IS_E1x(pdev)) {
        REG_WR(pdev,TSEM_REG_VFPF_ERR_NUM, (FUNC_ID(pdev) + E2_MAX_NUM_OF_VFS));
        REG_WR(pdev,USEM_REG_VFPF_ERR_NUM, (FUNC_ID(pdev) + E2_MAX_NUM_OF_VFS));
        REG_WR(pdev,CSEM_REG_VFPF_ERR_NUM, (FUNC_ID(pdev) + E2_MAX_NUM_OF_VFS));
        REG_WR(pdev,XSEM_REG_VFPF_ERR_NUM, (FUNC_ID(pdev) + E2_MAX_NUM_OF_VFS));
    }

}


static void init_pbf_port(lm_device_t *pdev)
{
    if(ERR_IF(!pdev)) {
        return;
    }

    ECORE_INIT_PORT(pdev, PBF);

    // configure PBF to work without PAUSE mtu 9600 - bug in E1/E1H
    if (CHIP_IS_E1x(pdev)) {
        REG_WR(pdev,(PORT_ID(pdev) ? PBF_REG_P1_PAUSE_ENABLE : PBF_REG_P0_PAUSE_ENABLE),0);
        //  update threshold
        REG_WR(pdev,(PORT_ID(pdev) ? PBF_REG_P1_ARB_THRSH : PBF_REG_P0_ARB_THRSH),(MAXIMUM_PACKET_SIZE/16));
        //  update init credit
        REG_WR(pdev,(PORT_ID(pdev) ? PBF_REG_P1_INIT_CRD : PBF_REG_P0_INIT_CRD),(MAXIMUM_PACKET_SIZE/16) + 553 -22);
        // probe changes
        REG_WR(pdev,(PORT_ID(pdev) ? PBF_REG_INIT_P1 : PBF_REG_INIT_P0),1);
        mm_wait(pdev,5);
        REG_WR(pdev,(PORT_ID(pdev) ? PBF_REG_INIT_P1 : PBF_REG_INIT_P0),0);
    }

}

static void init_src_common(lm_device_t *pdev)
{
    if(ERR_IF(!pdev)) {
        return;
    }

    REG_WR(pdev,SRC_REG_SOFT_RST,1);

    ECORE_INIT_COMN(pdev, SRCH);

    REG_WR(pdev,SRC_REG_KEYSEARCH_0,*(u32_t *)(&pdev->context_info->searcher_hash.searcher_key[0]));
    REG_WR(pdev,SRC_REG_KEYSEARCH_1,*(u32_t *)(&pdev->context_info->searcher_hash.searcher_key[4]));
    REG_WR(pdev,SRC_REG_KEYSEARCH_2,*(u32_t *)(&pdev->context_info->searcher_hash.searcher_key[8]));
    REG_WR(pdev,SRC_REG_KEYSEARCH_3,*(u32_t *)(&pdev->context_info->searcher_hash.searcher_key[12]));
    REG_WR(pdev,SRC_REG_KEYSEARCH_4,*(u32_t *)(&pdev->context_info->searcher_hash.searcher_key[16]));
    REG_WR(pdev,SRC_REG_KEYSEARCH_5,*(u32_t *)(&pdev->context_info->searcher_hash.searcher_key[20]));
    REG_WR(pdev,SRC_REG_KEYSEARCH_6,*(u32_t *)(&pdev->context_info->searcher_hash.searcher_key[24]));
    REG_WR(pdev,SRC_REG_KEYSEARCH_7,*(u32_t *)(&pdev->context_info->searcher_hash.searcher_key[28]));
    REG_WR(pdev,SRC_REG_KEYSEARCH_8,*(u32_t *)(&pdev->context_info->searcher_hash.searcher_key[32]));
    REG_WR(pdev,SRC_REG_KEYSEARCH_9,*(u32_t *)(&pdev->context_info->searcher_hash.searcher_key[36]));

    REG_WR(pdev,SRC_REG_SOFT_RST,0);
}

static void init_src_func(lm_device_t *pdev)
{
    lm_address_t src_addr;

    ECORE_INIT_FUNC(pdev, SRCH);
    // tell the searcher where the T2 table is
    REG_WR(pdev,  (PORT_ID(pdev) ? SRC_REG_COUNTFREE1 : SRC_REG_COUNTFREE0) ,pdev->vars.searcher_t2_num_pages * pdev->params.ilt_client_page_size/64);
    REG_WR_IND(pdev,  (PORT_ID(pdev) ? SRC_REG_FIRSTFREE1 : SRC_REG_FIRSTFREE0),pdev->vars.searcher_t2_phys_addr_table[0].as_u32.low);
    REG_WR_IND(pdev,  (PORT_ID(pdev) ? SRC_REG_FIRSTFREE1 : SRC_REG_FIRSTFREE0)+4,pdev->vars.searcher_t2_phys_addr_table[0].as_u32.high);
    src_addr.as_u64 = pdev->vars.searcher_t2_phys_addr_table[pdev->vars.searcher_t2_num_pages-1].as_u64
        + pdev->params.ilt_client_page_size - 64 ;
    REG_WR_IND(pdev,  (PORT_ID(pdev) ? SRC_REG_LASTFREE1 : SRC_REG_LASTFREE0),src_addr.as_u32.low);
    REG_WR_IND(pdev,  (PORT_ID(pdev) ? SRC_REG_LASTFREE1 : SRC_REG_LASTFREE0)+4,src_addr.as_u32.high);
    REG_WR(pdev,  (PORT_ID(pdev) ? SRC_REG_NUMBER_HASH_BITS1 : SRC_REG_NUMBER_HASH_BITS0),pdev->context_info->searcher_hash.num_hash_bits);
}

static void init_cdu_common(lm_device_t *pdev)
{
    u32_t val = 0;

    if(ERR_IF(!pdev)) {
        return;
    }
    // static initialization only for Common part
    ECORE_INIT_COMN(pdev, CDU);

    val = (pdev->params.num_context_in_page<<24) +
        (pdev->params.context_waste_size<<12)  +
        pdev->params.context_line_size;
    REG_WR(pdev,CDU_REG_CDU_GLOBAL_PARAMS,val);
    /* configure cdu to work with cdu-validation. TODO: Move init to hw init tool */
    REG_WR(pdev,CDU_REG_CDU_CONTROL0,0X1UL);
    REG_WR(pdev,CDU_REG_CDU_CHK_MASK0,0X0003d000UL); /* enable region 2 */
    REG_WR(pdev,CDU_REG_CDU_CHK_MASK1,0X0000003dUL); /* enable region 4 */

}


static void init_cfc_common(lm_device_t *pdev)
{
    u32_t cfc_init_reg = 0;
    if(ERR_IF(!pdev)) {
        return;
    }

    ECORE_INIT_COMN(pdev, CFC);
    /* init cfc with user configurable number of connections in cfc */

    cfc_init_reg |= (1 << CFC_INIT_REG_REG_AC_INIT_SIZE);
    cfc_init_reg |= (pdev->params.cfc_last_lcid << CFC_INIT_REG_REG_LL_INIT_LAST_LCID_SIZE);
    cfc_init_reg |= (1 << CFC_INIT_REG_REG_LL_INIT_SIZE);
    cfc_init_reg |= (1 << CFC_INIT_REG_REG_CAM_INIT_SIZE);
    REG_WR(pdev,  CFC_REG_INIT_REG, cfc_init_reg);

    // enable context validation interrupt from CFC
    #ifdef VF_INVOLVED
    if (!CHIP_IS_E1x(pdev) && IS_BASIC_VIRT_MODE_MASTER_PFDEV(pdev)) {
        /* with vfs - due to flr.. we don't want cfc to give attention on error from pxp,
         * in regular environemt - we want this error bit5:
         * The CDU responded with an error bit #0 (PCIe error) DORQ client has separate control
         * for this exec error
         */
        REG_WR(pdev, CFC_REG_DISABLE_ON_ERROR, 0xffdf);
        REG_WR(pdev, CFC_REG_CFC_INT_MASK, 0x2);
    }
    else
    {
        REG_WR(pdev,CFC_REG_CFC_INT_MASK ,0);
    }
    #else
    REG_WR(pdev,CFC_REG_CFC_INT_MASK ,0);
    #endif



    // configure CFC/CDU. TODO: Move CFC init to hw init tool */
    REG_WR(pdev,CFC_REG_DEBUG0 ,0x20020000);
    REG_WR(pdev,CFC_REG_INTERFACES ,0x280000);
    REG_WR(pdev,CFC_REG_INTERFACES ,0);

}



static void init_hc_port(lm_device_t *pdev)
{
    if(ERR_IF(!pdev)) {
        return;
    }

    if(CHIP_IS_E1(pdev))
    {
        REG_WR(pdev,  (PORT_ID(pdev) ? HC_REG_LEADING_EDGE_1 : HC_REG_LEADING_EDGE_0), 0);
        REG_WR(pdev,  (PORT_ID(pdev) ? HC_REG_TRAILING_EDGE_1 : HC_REG_TRAILING_EDGE_0), 0);
    }

    ECORE_INIT_PORT(pdev, HC);
}

static void init_hc_func(lm_device_t *pdev)
{
    const u8_t func = FUNC_ID(pdev);

    if(ERR_IF(!pdev)) {
        return;
    }

    if(CHIP_IS_E1H(pdev)) {
        REG_WR(pdev, MISC_REG_AEU_GENERAL_ATTN_12 + 4*func,0x0);
        REG_WR(pdev,  (PORT_ID(pdev) ? HC_REG_LEADING_EDGE_1 : HC_REG_LEADING_EDGE_0), 0);
        REG_WR(pdev,  (PORT_ID(pdev) ? HC_REG_TRAILING_EDGE_1 : HC_REG_TRAILING_EDGE_0), 0);
    }

    ECORE_INIT_FUNC(pdev, HC);
}

static void init_igu_func(lm_device_t *pdev)
{
    u32_t prod_idx,i,val;
    u8_t num_segs;
    u8_t base_prod;
    u8_t sb_id;
    u8_t dsb_idx;
    u8_t igu_func_id;

    if(ERR_IF(!pdev)) {
        return;
    }

    if (INTR_BLK_TYPE(pdev) == INTR_BLK_IGU) {
        /* E2 TODO: make sure that misc is updated accordingly and that three lines below are not required */
        REG_WR(pdev, MISC_REG_AEU_GENERAL_ATTN_12 + 4*FUNC_ID(pdev),0x0);
        REG_WR(pdev,  IGU_REG_LEADING_EDGE_LATCH, 0);
        REG_WR(pdev,  IGU_REG_TRAILING_EDGE_LATCH, 0);

        ECORE_INIT_FUNC(pdev, IGU);

        /* Let's enable the function in the IGU - this is to enable consumer updates */
        val=REG_RD(pdev, IGU_REG_PF_CONFIGURATION);
        SET_FLAGS(val, IGU_PF_CONF_FUNC_EN);
        REG_WR(pdev,  IGU_REG_PF_CONFIGURATION, val);

        /* Producer memory:
         * E2 mode: address 0-135 match to the mapping memory;
         * 136 - PF0 default prod; 137 PF1 default prod; 138 - PF2 default prod;  139 PF3 default prod;
         * 140 - PF0 - ATTN prod; 141 - PF1 - ATTN prod; 142 - PF2 - ATTN prod; 143 - PF3 - ATTN prod;
         * 144-147 reserved.
         * E1.5 mode - In backward compatible mode; for non default SB; each even line in the memory
         * holds the U producer and each odd line hold the C producer. The first 128 producer are for
         * NDSB (PF0 - 0-31; PF1 - 32-63 and so on).
         * The last 20 producers are for the DSB for each PF. each PF has five segments
         * (the order inside each segment is PF0; PF1; PF2; PF3) - 128-131 U prods; 132-135 C prods; 136-139 X prods; 140-143 T prods; 144-147 ATTN prods;
         */
        /* non-default-status-blocks*/
        num_segs = IGU_NORM_NDSB_NUM_SEGS;
        for (sb_id = 0; sb_id < LM_IGU_SB_CNT(pdev); sb_id++) {
            prod_idx = (IGU_BASE_NDSB(pdev) + sb_id)*num_segs; /* bc-assumption consecutive pfs, norm-no assumption */
            for (i = 0; i < num_segs;i++) {
                REG_WR(pdev, IGU_REG_PROD_CONS_MEMORY + (prod_idx + i)*4, 0);
            }
            /* Give Consumer updates with value '0' */
            lm_int_ack_sb_enable(pdev, sb_id);

            /* Send cleanup command */
            lm_int_igu_sb_cleanup(pdev, IGU_BASE_NDSB(pdev) + sb_id);
        }

        /* default-status-blocks */
        if (CHIP_PORT_MODE(pdev) == LM_CHIP_PORT_MODE_4) {
            dsb_idx = FUNC_ID(pdev);
        } else {
            dsb_idx = VNIC_ID(pdev);
        }
        num_segs = (INTR_BLK_MODE(pdev) == INTR_BLK_MODE_BC)? IGU_BC_DSB_NUM_SEGS : IGU_NORM_DSB_NUM_SEGS;
        base_prod = (INTR_BLK_MODE(pdev) == INTR_BLK_MODE_BC) ? (IGU_BC_BASE_DSB_PROD + dsb_idx) : (IGU_NORM_BASE_DSB_PROD + dsb_idx);
        for (i = 0; i < num_segs; i++) {
            REG_WR(pdev, IGU_REG_PROD_CONS_MEMORY + (base_prod + i*MAX_VNIC_NUM)*4, 0);
        }

        lm_int_ack_def_sb_enable(pdev);

        /* Send cleanup command */
        lm_int_igu_sb_cleanup(pdev, IGU_DSB_ID(pdev));

        /* Reset statistics msix / attn */
        igu_func_id = (CHIP_PORT_MODE(pdev) == LM_CHIP_PORT_MODE_4)? FUNC_ID(pdev) : VNIC_ID(pdev);
        igu_func_id |= (1 << IGU_FID_ENCODE_IS_PF_SHIFT);

        REG_WR(pdev, IGU_REG_STATISTIC_NUM_MESSAGE_SENT + igu_func_id*4, 0);
        REG_WR(pdev, IGU_REG_STATISTIC_NUM_MESSAGE_SENT + (igu_func_id + MAX_VNIC_NUM)*4, 0);

        /* E2 TODO: these should become driver const once rf-tool supports split-68 const. */
        REG_WR(pdev, IGU_REG_SB_INT_BEFORE_MASK_LSB, 0);
        REG_WR(pdev, IGU_REG_SB_INT_BEFORE_MASK_MSB, 0);
        REG_WR(pdev, IGU_REG_SB_MASK_LSB, 0);
        REG_WR(pdev, IGU_REG_SB_MASK_MSB, 0);
        REG_WR(pdev, IGU_REG_PBA_STATUS_LSB, 0);
        REG_WR(pdev, IGU_REG_PBA_STATUS_MSB, 0);
    }
}

void init_nig_common_llh(lm_device_t *pdev)
{
    if ( CHIP_IS_E1(pdev) )
    {
        return;
    }

    if (CHIP_IS_E2(pdev) || CHIP_IS_E1H(pdev)) /* E3 supports this per port - and is therefore done in the port phase */
    {
        REG_WR(pdev, NIG_REG_LLH_MF_MODE, IS_MULTI_VNIC(pdev) ? 1 : 0);
    }

    /* E1HOV mode was removed in E2 and is replaced with hdrs-after-basic... */
    if ( CHIP_IS_E1H(pdev) )
    {
        REG_WR(pdev,NIG_REG_LLH_E1HOV_MODE, IS_MF_SD_MODE(pdev) ? 1 : 0);
    }
}

static void init_nig_common(lm_device_t *pdev)
{
    ECORE_INIT_COMN( pdev, NIG);
    init_nig_common_llh(pdev);
}

static void init_nig_port(lm_device_t *pdev)
{
    ECORE_INIT_PORT( pdev, NIG);

    if (!CHIP_IS_E3(pdev))
    {
        REG_WR(pdev,(PORT_ID(pdev) ? NIG_REG_XGXS_SERDES1_MODE_SEL : NIG_REG_XGXS_SERDES0_MODE_SEL),1);
    }

    if (!CHIP_IS_E1x(pdev))
    {
        if (IS_MF_NIV_MODE(pdev))
        {
            REG_WR(pdev, (PORT_ID(pdev)? NIG_REG_P1_HDRS_AFTER_BASIC : NIG_REG_P0_HDRS_AFTER_BASIC), 0xE); // Bit-map indicating which L2 hdrs may appear after the basic Ethernet header.
        }
        else if (IS_MF_SD_MODE(pdev))
        {
            REG_WR(pdev, (PORT_ID(pdev)? NIG_REG_P1_HDRS_AFTER_BASIC : NIG_REG_P0_HDRS_AFTER_BASIC), 0x7 ); // Bit-map indicating which L2 hdrs may appear after the basic Ethernet header.
        }
        else
        {
            REG_WR(pdev, (PORT_ID(pdev)? NIG_REG_P1_HDRS_AFTER_BASIC : NIG_REG_P0_HDRS_AFTER_BASIC), 0x6 ); // Bit-map indicating which L2 hdrs may appear after the basic Ethernet header.
        }

        /* MF-mode can be set separately per port in E3, and therefore is done here... for E2 and before it is done in the common phase */
        if (CHIP_IS_E3(pdev))
        {
            REG_WR(pdev,(PORT_ID(pdev)?  NIG_REG_LLH1_MF_MODE: NIG_REG_LLH_MF_MODE), IS_MULTI_VNIC(pdev) ? 1 : 0);
        }
    }

    if (!CHIP_IS_E1(pdev))
    {
        /*   LLH0/1_BRB1_DRV_MASK_MF        MF      SF
              mask_no_outer_vlan            0       1
              mask_outer_vlan               1       0*/
        u32_t mask_mf_reg = PORT_ID(pdev) ? NIG_REG_LLH1_BRB1_DRV_MASK_MF : NIG_REG_LLH0_BRB1_DRV_MASK_MF;
        u32_t val = IS_MF_SD_MODE(pdev) ? NIG_LLH0_BRB1_DRV_MASK_MF_REG_LLH0_BRB1_DRV_MASK_OUTER_VLAN : NIG_LLH0_BRB1_DRV_MASK_MF_REG_LLH0_BRB1_DRV_MASK_NO_OUTER_VLAN;

        ASSERT_STATIC(NIG_LLH0_BRB1_DRV_MASK_MF_REG_LLH0_BRB1_DRV_MASK_OUTER_VLAN    == NIG_LLH1_BRB1_DRV_MASK_MF_REG_LLH1_BRB1_DRV_MASK_OUTER_VLAN);
        ASSERT_STATIC(NIG_LLH0_BRB1_DRV_MASK_MF_REG_LLH0_BRB1_DRV_MASK_NO_OUTER_VLAN == NIG_LLH1_BRB1_DRV_MASK_MF_REG_LLH1_BRB1_DRV_MASK_NO_OUTER_VLAN);
        REG_WR(pdev, mask_mf_reg, val);

        if (!CHIP_IS_E1x(pdev))
        {
           if ( IS_MF_SI_MODE(pdev))
            {
                REG_WR(pdev, (PORT_ID(pdev) ? NIG_REG_LLH1_CLS_TYPE : NIG_REG_LLH0_CLS_TYPE), 2);
            }
            else
            {
                REG_WR(pdev, (PORT_ID(pdev) ? NIG_REG_LLH1_CLS_TYPE : NIG_REG_LLH0_CLS_TYPE), 1);
            }
        }
    }

}

void init_nig_func(lm_device_t *pdev)
{
    const u8_t  mf     = pdev->params.multi_vnics_mode;
    const u8_t  port   = PORT_ID(pdev);
    u32_t       offset = 0;
    u32_t       val    = 0;

    ECORE_INIT_FUNC(pdev, NIG);

    pdev->hw_info.is_dcc_active = FALSE ;

    if (mf)
    {
        offset = ( port ? NIG_REG_LLH1_FUNC_EN : NIG_REG_LLH0_FUNC_EN );
        REG_WR(pdev, offset , 1);

        offset = ( port ? NIG_REG_LLH1_FUNC_VLAN_ID : NIG_REG_LLH0_FUNC_VLAN_ID );
        REG_WR(pdev, offset , pdev->params.ovlan);

        offset = ( port ? NIG_REG_LLH1_DEST_MAC_3_0 : NIG_REG_LLH0_DEST_MAC_3_0 ) ;
        val = REG_RD(pdev, offset) ;
        if( 0x09000005 == val )
        {
            pdev->hw_info.is_dcc_active = TRUE ;
        }
    }
}

static void init_pxpcs_common(lm_device_t *pdev)
{
    if(ERR_IF(!pdev)) {
        return;
    }

    ECORE_INIT_COMN(pdev, PXPCS);

    /* Reset pciex errors */
    REG_WR(pdev,0x2814,0xffffffff);
    REG_WR(pdev,0x3820,0xffffffff);

    if (!CHIP_IS_E1x(pdev)) {
        REG_WR(pdev,PCICFG_OFFSET + PXPCS_TL_CONTROL_5,  (PXPCS_TL_CONTROL_5_ERR_UNSPPORT1 | PXPCS_TL_CONTROL_5_ERR_UNSPPORT));
        REG_WR(pdev,PCICFG_OFFSET + PXPCS_TL_FUNC345_STAT,
               (PXPCS_TL_FUNC345_STAT_ERR_UNSPPORT4 | PXPCS_TL_FUNC345_STAT_ERR_UNSPPORT3 | PXPCS_TL_FUNC345_STAT_ERR_UNSPPORT2));
        REG_WR(pdev,PCICFG_OFFSET + PXPCS_TL_FUNC678_STAT,
               (PXPCS_TL_FUNC678_STAT_ERR_UNSPPORT7 | PXPCS_TL_FUNC678_STAT_ERR_UNSPPORT6 | PXPCS_TL_FUNC678_STAT_ERR_UNSPPORT5));
    }
}

static void init_pxpcs_func(lm_device_t *pdev)
{
    if(ERR_IF(!pdev)) {
        return;
    }

    ECORE_INIT_FUNC(pdev, PXPCS);
    /* Reset pciex errors */
    REG_WR(pdev,0x2114,0xffffffff);
    REG_WR(pdev,0x2120,0xffffffff);
}

static void init_pglue_b_port(lm_device_t *pdev)
{
    ECORE_INIT_PORT(pdev, PGLUE_B);
    /* Timers bug workaround: disables the pf_master bit in pglue at common phase, we need to enable it here before
     * any dmae access are attempted. Therefore we manually added the enable-master to the port phase (it also happens
     * in the function phase) */
    if (!CHIP_IS_E1x(pdev)) {
        REG_WR(pdev, PGLUE_B_REG_INTERNAL_PFID_ENABLE_MASTER, 1);
    }
}

static void init_pglue_b_func(lm_device_t *pdev)
{
    ECORE_INIT_FUNC(pdev, PGLUE_B);

    if (!CHIP_IS_E1x(pdev)) {
        /* 1. Timers bug workaround. There may be an error here. do this only if func_id=6, otherwise
         * an error isn't expected
         * 2. May be an error due to FLR.
         */
        REG_WR(pdev,PGLUE_B_REG_WAS_ERROR_PF_7_0_CLR, FUNC_ID(pdev));
    }

}

static void init_cfc_func(lm_device_t *pdev)
{
    ECORE_INIT_FUNC(pdev, CFC);
    if (!CHIP_IS_E1x(pdev)) {
        REG_WR(pdev, CFC_REG_WEAK_ENABLE_PF,1);
        //REG_WR(pdev, CFC_REG_STRONG_ENABLE_PF,1);
    }
}

static void init_tcm_common(lm_device_t *pdev)
{
    u32_t xx_msg_num  = 0;
    u32_t xx_init_crd = 0;
    u32_t val         = 0;
    u32_t i           = 0;

    ECORE_INIT_COMN(pdev, TCM);
    if (!CHIP_IS_E1x(pdev))
    {
        if (CHIP_IS_E2(pdev))
        {
            xx_init_crd = 0x1c;
            xx_msg_num = 0x20;
        }
        else
        {
            xx_init_crd = 0x1e;
            xx_msg_num = 0x1d;
        }

        REG_WR(pdev, TCM_REG_XX_INIT_CRD, xx_init_crd);
        REG_WR(pdev, TCM_REG_XX_MSG_NUM, xx_msg_num);

        for (i = 0; i < xx_msg_num; i++ )
        {
            val = (((i + 1) % xx_msg_num) << 16) + ((i * xx_init_crd) << 6);
            DbgMessage1(pdev, INFORM, "val=%x\n", val);
            REG_WR(pdev, TCM_REG_XX_DESCR_TABLE + 4*i, val);
        }

        if (CHIP_IS_E2(pdev))
        {
            /* Nullify last entries only in E2, in E3 the table is smaller...*/
            for (i = xx_msg_num; i < 32; i++ )
            {
                REG_WR(pdev, TCM_REG_XX_DESCR_TABLE + 4*i, 0);
            }
        }
    }
}

/**
 * UCM runtime initializations are done in E2 for E2_HW only.
 * The reason is that the initialization of the table itself is
 * done inthe init-tool and is identical to both E2 + E3, the
 * only difference is the table size, which is larger in E2 and
 * therefore needs to be nullified in the end.
 *
 * @param pdev
 */
static void init_ucm_common(lm_device_t *pdev)
{
    u8_t i;
    #define UCM_XX_MSG_NUM_E2 27
    #define UCM_XX_DESCR_TABLE_NUM_ENTRIES_E2 32

    ECORE_INIT_COMN(pdev, UCM);
    if (CHIP_IS_E2(pdev))
    {
        /* Nullify last entries only in E2, in E3 the table is smaller...*/
        for (i = UCM_XX_MSG_NUM_E2; i < UCM_XX_DESCR_TABLE_NUM_ENTRIES_E2; i++ )
        {
            REG_WR(pdev, UCM_REG_XX_DESCR_TABLE + 4*i, 0);
        }
    }
}

/**
 * CCM runtime initializations are done in E2 for E2_HW only.
 * The reason is that the initialization of the table itself is
 * done inthe init-tool and is identical to both E2 + E3, the
 * only difference is the table size, which is larger in E2 and
 * therefore needs to be nullified in the end.
 *
 * @param pdev
 */
static void init_ccm_common(lm_device_t *pdev)
{
    u8_t i;
    #define CCM_XX_MSG_NUM_E2 24
    #define CCM_XX_DESCR_TABLE_NUM_ENTRIES_E2 36

    ECORE_INIT_COMN(pdev, CCM);
    if (CHIP_IS_E2(pdev))
    {
        /* Nullify last entries only in E2, in E3 the table is smaller...*/
        for (i = CCM_XX_MSG_NUM_E2; i < CCM_XX_DESCR_TABLE_NUM_ENTRIES_E2; i++ )
        {
            REG_WR(pdev, CCM_REG_XX_DESCR_TABLE + 4*i, 0);
        }
    }
}

static void init_aeu_common(lm_device_t * pdev)
{
    ECORE_INIT_COMN(pdev, MISC_AEU);

    /* Error Recovery : attach some attentions to close-the-g8 NIG + PXP2 */
    lm_er_config_close_the_g8(pdev);
}


#define init_xcm_common( pdev)     ECORE_INIT_COMN(pdev, XCM)
#define init_tsdm_common(pdev)     ECORE_INIT_COMN(pdev, TSDM)
#define init_csdm_common(pdev)     ECORE_INIT_COMN(pdev, CSDM)
#define init_usdm_common(pdev)     ECORE_INIT_COMN(pdev, USDM)
#define init_xsdm_common(pdev)     ECORE_INIT_COMN(pdev, XSDM)
#define init_tm_common(  pdev)     ECORE_INIT_COMN(pdev, TIMERS)
#define init_upb_common( pdev)     ECORE_INIT_COMN(pdev, UPB)
#define init_xpb_common( pdev)     ECORE_INIT_COMN(pdev, XPB)
#define init_hc_common(  pdev)     ECORE_INIT_COMN(pdev, HC)
#define init_igu_common( pdev)     ECORE_INIT_COMN(pdev, IGU)
#define init_emac0_common(pdev)    ECORE_INIT_COMN(pdev, EMAC0)
#define init_emac1_common(pdev)    ECORE_INIT_COMN(pdev, EMAC1)
#define init_dbu_common(pdev)      ECORE_INIT_COMN(pdev, DBU)
#define init_dbg_common(pdev)      ECORE_INIT_COMN(pdev, DBG)
#define init_mcp_common(pdev)      ECORE_INIT_COMN(pdev, MCP)

#define init_pxp_port(pdev)        ECORE_INIT_PORT(pdev, PXP)
#define init_pxp2_port(pdev)       ECORE_INIT_PORT(pdev, PXP2)
#define init_atc_port(pdev)        ECORE_INIT_PORT(pdev, ATC)
#define init_tcm_port( pdev)       ECORE_INIT_PORT(pdev, TCM)
#define init_ucm_port( pdev)       ECORE_INIT_PORT(pdev, UCM)
#define init_ccm_port( pdev)       ECORE_INIT_PORT(pdev, CCM)
#define init_xcm_port( pdev)       ECORE_INIT_PORT(pdev, XCM)
#define init_dq_port(pdev)         ECORE_INIT_PORT(pdev, DQ)
#define init_prs_port( pdev)       ECORE_INIT_PORT(pdev, PRS)
#define init_tsdm_port( pdev)      ECORE_INIT_PORT(pdev, TSDM)
#define init_csdm_port( pdev)      ECORE_INIT_PORT(pdev, CSDM)
#define init_usdm_port( pdev)      ECORE_INIT_PORT(pdev, USDM)
#define init_xsdm_port( pdev)      ECORE_INIT_PORT(pdev, XSDM)
#define init_upb_port(pdev)        ECORE_INIT_PORT(pdev, UPB)
#define init_xpb_port(pdev)        ECORE_INIT_PORT(pdev, XPB)
#define init_src_port(pdev)        ECORE_INIT_PORT(pdev, SRCH)
#define init_cdu_port(pdev)        ECORE_INIT_PORT(pdev, CDU)
#define init_cfc_port(pdev)        ECORE_INIT_PORT(pdev, CFC)

#define init_igu_port( pdev)       ECORE_INIT_PORT(pdev, IGU)
#define init_pxpcs_port(pdev)      ECORE_INIT_PORT(pdev, PXPCS)
#define init_emac0_port(pdev)      ECORE_INIT_PORT(pdev, EMAC0)
#define init_emac1_port(pdev)      ECORE_INIT_PORT(pdev, EMAC1)
#define init_dbu_port(pdev)        ECORE_INIT_PORT(pdev, DBU)
#define init_dbg_port(pdev)        ECORE_INIT_PORT(pdev, DBG)
#define init_mcp_port(pdev)        ECORE_INIT_PORT(pdev, MCP)
#define init_dmae_port(pdev)       ECORE_INIT_PORT(pdev, DMAE)

#define init_misc_func(pdev)       ECORE_INIT_FUNC(pdev, MISC)
#define init_pxp_func(pdev)        ECORE_INIT_FUNC(pdev, PXP)
#define init_atc_func(pdev)        ECORE_INIT_FUNC(pdev, ATC)
#define init_tcm_func(pdev)        ECORE_INIT_FUNC(pdev, TCM)
#define init_ucm_func(pdev)        ECORE_INIT_FUNC(pdev, UCM)
#define init_ccm_func(pdev)        ECORE_INIT_FUNC(pdev, CCM)
#define init_xcm_func(pdev)        ECORE_INIT_FUNC(pdev, XCM)
#define init_tm_func(pdev)         ECORE_INIT_FUNC(pdev, TIMERS)
#define init_brb_func(pdev)        ECORE_INIT_FUNC(pdev, BRB1)
#define init_tsdm_func(pdev)       ECORE_INIT_FUNC(pdev, TSDM)
#define init_csdm_func(pdev)       ECORE_INIT_FUNC(pdev, CSDM)
#define init_usdm_func(pdev)       ECORE_INIT_FUNC(pdev, USDM)
#define init_xsdm_func(pdev)       ECORE_INIT_FUNC(pdev, XSDM)
#define init_upb_func(pdev)        ECORE_INIT_FUNC(pdev, UPB)
#define init_xpb_func(pdev)        ECORE_INIT_FUNC(pdev, XPB)
#define init_cdu_func(pdev)        ECORE_INIT_FUNC(pdev, CDU)
#define init_aeu_func(pdev)        ECORE_INIT_FUNC(pdev, MISC_AEU)
#define init_emac0_func(pdev)      ECORE_INIT_FUNC(pdev, EMAC0)
#define init_emac1_func(pdev)      ECORE_INIT_FUNC(pdev, EMAC1)
#define init_dbu_func(pdev)        ECORE_INIT_FUNC(pdev, DBU)
#define init_dbg_func(pdev)        ECORE_INIT_FUNC(pdev, DBG)
#define init_mcp_func(pdev)        ECORE_INIT_FUNC(pdev, MCP)
#define init_dmae_func(pdev)       ECORE_INIT_FUNC(pdev, DMAE)


//The bug is that the RBC doesn't get out of reset after we reset the RBC.
static void rbc_reset_workaround(lm_device_t *pdev)
{
    u32_t val=0;
#if defined(_VBD_CMD_) //This function is not needed in vbd_cmd env.
    return;
#endif

    if (CHIP_IS_E1x(pdev))
    {
        //a.Wait 60 microseconds only for verifying the ~64 cycles have passed.
        mm_wait(pdev, (DEFAULT_WAIT_INTERVAL_MICSEC *2));

        val = REG_RD(pdev,MISC_REG_RESET_REG_1) ;
        if(0 == (val & MISC_REGISTERS_RESET_REG_1_RST_RBCP))
        {
            //If bit 28 is '0' - This means RBCP block is in reset.(one out of reset)
            // Take RBC out of reset.
            REG_WR(pdev,(GRCBASE_MISC + MISC_REGISTERS_RESET_REG_1_SET),MISC_REGISTERS_RESET_REG_1_RST_RBCP);

            mm_wait(pdev, (DEFAULT_WAIT_INTERVAL_MICSEC *2));

            val = REG_RD(pdev,MISC_REG_RESET_REG_1) ;

            DbgMessage1(pdev,WARN,"rbc_reset_workaround: MISC_REG_RESET_REG_1 after set= 0x%x\n",val);
            DbgBreakIf(0 == (val & MISC_REGISTERS_RESET_REG_1_RST_RBCP));
        }
    }
}

// PRS BRB dual port ram memory setup
static void prs_brb_mem_setup (struct _lm_device_t *pdev)
{
    u32_t val    = 0;
    u32_t trash  = 0;
    u32_t cnt    = 0;
    u8_t  i      = 0;

#ifdef _VBD_CMD_
    return;
#endif
    DbgBreakIf(!pdev->vars.clk_factor);

    DbgMessage(pdev,WARN,"mem_wrk start part1\n");
    //First part
    // Disable inputs of parser neighbor blocks
    REG_WR(pdev,TSDM_REG_ENABLE_IN1,0x0);
    REG_WR(pdev,TCM_REG_PRS_IFEN,0x0);
    REG_WR(pdev,CFC_REG_DEBUG0,0x1);
    REG_WR(pdev,NIG_REG_PRS_REQ_IN_EN,0x0);

    // Write 0 to parser credits for CFC search request
    REG_WR(pdev,PRS_REG_CFC_SEARCH_INITIAL_CREDIT,0x0);

    // send Ethernet packet
    init_nig_pkt(pdev);

    // TODO: Reset NIG statistic
    // Wait until NIG register shows 1 packet of size 0x10
    cnt = 1000;
    while (cnt) {
        val=REG_RD(pdev,NIG_REG_STAT2_BRB_OCTET);
        trash=REG_RD(pdev,NIG_REG_STAT2_BRB_OCTET+4);

        if (val == 0x10) {
            break;
        }
        mm_wait(pdev,10 * pdev->vars.clk_factor);
        cnt--;
    }
    if (val != 0x10) {
        DbgMessage1(pdev,FATAL,"mem_wrk: part1 NIG timeout val = 0x%x\n",val);
        DbgBreakIfAll(1);
    }

    // Wait until PRS register shows 1 packet
    cnt = 1000;
    while (cnt) {
        val=REG_RD(pdev,PRS_REG_NUM_OF_PACKETS);

        if (val == 0x1) {
            break;
        }
        mm_wait(pdev,10 * pdev->vars.clk_factor);
        cnt--;
    }
    if (val != 0x1) {
        DbgMessage1(pdev,FATAL,"mem_wrk: part1 PRS timeout val = 0x%x\n",val);
        DbgBreakIfAll(1);
    }
    // End of part 1

    // #Reset and init BRB,PRS
    REG_WR(pdev,GRCBASE_MISC+MISC_REGISTERS_RESET_REG_1_CLEAR,0x3);
    mm_wait(pdev,50);
    REG_WR(pdev,GRCBASE_MISC+MISC_REGISTERS_RESET_REG_1_SET,0x3);
    mm_wait(pdev,50);
    init_brb1_common( pdev );
    init_prs_common(pdev);

    DbgMessage(pdev,WARN,"mem_wrk start part2\n");
    // "Start of part 2"

    // Disable inputs of parser neighbor blocks
    REG_WR(pdev,TSDM_REG_ENABLE_IN1,0x0);
    REG_WR(pdev,TCM_REG_PRS_IFEN,0x0);
    REG_WR(pdev,CFC_REG_DEBUG0,0x1);
    REG_WR(pdev,NIG_REG_PRS_REQ_IN_EN,0x0);

    // Write 0 to parser credits for CFC search request
    REG_WR(pdev,PRS_REG_CFC_SEARCH_INITIAL_CREDIT,0x0);

    // send 10 Ethernet packets
    for (i=0;i<10;i++) {
        init_nig_pkt(pdev);
    }

    // Wait until NIG register shows 10+1 packets of size 11*0x10 = 0xb0
    cnt = 1000;
    while (cnt) {

        val=REG_RD(pdev,NIG_REG_STAT2_BRB_OCTET);
        trash=REG_RD(pdev,NIG_REG_STAT2_BRB_OCTET+4);

        if (val == 0xb0) {
            break;
        }
        mm_wait(pdev,10 * pdev->vars.clk_factor );
        cnt--;
    }
    if (val != 0xb0) {
        DbgMessage1(pdev,FATAL,"mem_wrk: part2 NIG timeout val = 0x%x\n",val);
        DbgBreakIfAll(1);
    }

    // Wait until PRS register shows 2 packet
    val=REG_RD(pdev,PRS_REG_NUM_OF_PACKETS);

    if (val != 0x2) {
        DbgMessage1(pdev,FATAL,"mem_wrk: part2 PRS wait for 2 timeout val = 0x%x\n",val);
        DbgBreakIfAll(1);
    }

    // Write 1 to parser credits for CFC search request
    REG_WR(pdev,PRS_REG_CFC_SEARCH_INITIAL_CREDIT,0x1);

    // Wait until PRS register shows 3 packet
    mm_wait(pdev,100 * pdev->vars.clk_factor);
    // Wait until NIG register shows 1 packet of size 0x10
    val=REG_RD(pdev,PRS_REG_NUM_OF_PACKETS);

    if (val != 0x3) {
        DbgMessage1(pdev,FATAL,"mem_wrk: part2 PRS wait for 3 timeout val = 0x%x\n",val);
        DbgBreakIfAll(1);
    }

     // clear NIG EOP FIFO
    for (i=0;i<11;i++) {
        trash=REG_RD(pdev,NIG_REG_INGRESS_EOP_LB_FIFO);
    }
    val=REG_RD(pdev,NIG_REG_INGRESS_EOP_LB_EMPTY);
    DbgBreakIfAll(val != 1);

    // #Reset and init BRB,PRS
    REG_WR(pdev,GRCBASE_MISC+MISC_REGISTERS_RESET_REG_1_CLEAR,0x03);
    mm_wait(pdev,50);
    REG_WR(pdev,GRCBASE_MISC+MISC_REGISTERS_RESET_REG_1_SET,0x03);
    mm_wait(pdev,50);
    init_brb1_common( pdev );
    init_prs_common(pdev);
    // everest_init_part( pdev, BLCNUM_NIG  ,COMMON, hw);

    // Enable inputs of parser neighbor blocks
    REG_WR(pdev,TSDM_REG_ENABLE_IN1,0x7fffffff);
    REG_WR(pdev,TCM_REG_PRS_IFEN,0x1);
    REG_WR(pdev,CFC_REG_DEBUG0,0x0);
    REG_WR(pdev,NIG_REG_PRS_REQ_IN_EN,0x1);

    DbgMessage(pdev,WARN,"mem_wrk: Finish start part2\n");

}

static void enable_blocks_attention(struct _lm_device_t *pdev)
{
    REG_WR(pdev,PXP_REG_PXP_INT_MASK_0,0);
    if (!CHIP_IS_E1x(pdev)) {
        REG_WR(pdev,PXP_REG_PXP_INT_MASK_1, (PXP_PXP_INT_MASK_1_REG_HST_INCORRECT_ACCESS
                                             | PXP_PXP_INT_MASK_1_REG_HST_VF_DISABLED_ACCESS /*Temporary solution*/));
    }
    REG_WR(pdev,DORQ_REG_DORQ_INT_MASK,0);
    /* CFC_REG_CFC_INT_MASK see in init_cfc_common */


    //mask read length error interrupts in brb for parser (parsing unit and 'checksum and crc' unit)
    //these errors are legal (PU reads fixe length and CAC can cause read length error on truncated packets)
    REG_WR(pdev,BRB1_REG_BRB1_INT_MASK ,0xFC00);

    REG_WR(pdev,QM_REG_QM_INT_MASK ,0);
    REG_WR(pdev,TM_REG_TM_INT_MASK ,0);
    REG_WR(pdev,XSDM_REG_XSDM_INT_MASK_0 ,0);
    REG_WR(pdev,XSDM_REG_XSDM_INT_MASK_1 ,0);
    REG_WR(pdev,XCM_REG_XCM_INT_MASK ,0);
    //REG_WR(pdev,XSEM_REG_XSEM_INT_MASK_0 ,0);
    //REG_WR(pdev,XSEM_REG_XSEM_INT_MASK_1 ,0);
    REG_WR(pdev,USDM_REG_USDM_INT_MASK_0 ,0);
    REG_WR(pdev,USDM_REG_USDM_INT_MASK_1 ,0);
    REG_WR(pdev,UCM_REG_UCM_INT_MASK ,0);
    //REG_WR(pdev,USEM_REG_USEM_INT_MASK_0 ,0);
    //REG_WR(pdev,USEM_REG_USEM_INT_MASK_1 ,0);
    REG_WR(pdev,GRCBASE_UPB+PB_REG_PB_INT_MASK ,0);
    REG_WR(pdev,CSDM_REG_CSDM_INT_MASK_0 ,0);
    REG_WR(pdev,CSDM_REG_CSDM_INT_MASK_1 ,0);
    REG_WR(pdev,CCM_REG_CCM_INT_MASK ,0);
    //REG_WR(pdev,CSEM_REG_CSEM_INT_MASK_0 ,0);
    //REG_WR(pdev,CSEM_REG_CSEM_INT_MASK_1 ,0);
    if (!CHIP_IS_E1x(pdev)) {
        REG_WR(pdev,PXP2_REG_PXP2_INT_MASK_0,(PXP2_PXP2_INT_MASK_0_REG_PGL_CPL_OF
                                                | PXP2_PXP2_INT_MASK_0_REG_PGL_CPL_AFT
                                                | PXP2_PXP2_INT_MASK_0_REG_PGL_PCIE_ATTN
                                                | PXP2_PXP2_INT_MASK_0_REG_PGL_READ_BLOCKED
                                                | PXP2_PXP2_INT_MASK_0_REG_PGL_WRITE_BLOCKED));
    } else {
        REG_WR(pdev,PXP2_REG_PXP2_INT_MASK_0, 0x00580000);
    }

    REG_WR(pdev,TSDM_REG_TSDM_INT_MASK_0 ,0);
    REG_WR(pdev,TSDM_REG_TSDM_INT_MASK_1 ,0);
    REG_WR(pdev,TCM_REG_TCM_INT_MASK ,0);
    //REG_WR(pdev,TSEM_REG_TSEM_INT_MASK_0 ,0);
    //REG_WR(pdev,TSEM_REG_TSEM_INT_MASK_1 ,0);
    REG_WR(pdev,CDU_REG_CDU_INT_MASK ,0);
    REG_WR(pdev,DMAE_REG_DMAE_INT_MASK ,0);
    //REG_WR(pdev,GRCBASE_MISC+MISC_REGISTERS_MISC_INT_MASK ,0);
    //MASK BIT 3,4
    REG_WR(pdev,PBF_REG_PBF_INT_MASK ,0X18);

}

static  void init_common_part(struct _lm_device_t *pdev)
{
    u32_t temp = 0,val = 0,trash = 0;
    u8_t        rc                       = 0;
    const u32_t wait_ms                  = 200*pdev->vars.clk_factor ;
    u32_t       shmem_base[MAX_PATH_NUM] = {0}, shmem_base2[MAX_PATH_NUM] = {0};

    DbgMessage(pdev, INFORMi, "init_common_part\n");


    /* shutdown bug - clear the shutdown inprogress flag*/
    /* Must be done before DMAE */
    lm_reset_clear_inprogress(pdev);

    DbgBreakIf( !pdev->vars.clk_factor );

    init_misc_common( pdev );
    init_pxp_common ( pdev );
    init_pxp2_common( pdev );
    init_pglue_b_common(pdev);
    init_atc_common ( pdev );
    init_dmae_common( pdev );
    init_tcm_common ( pdev );
    init_ucm_common ( pdev );
    init_ccm_common ( pdev );
    init_xcm_common ( pdev );
    init_qm_common  ( pdev );
    init_tm_common  ( pdev );
    init_dq_common  ( pdev );
    init_brb1_common( pdev );
    init_prs_common( pdev);
    init_tsdm_common( pdev );
    init_csdm_common( pdev );
    init_usdm_common( pdev );
    init_xsdm_common( pdev );

    lm_nullify_intmem(pdev);

    init_semi_common(pdev);

    // syncronize rtc of the semi's
    REG_WR(pdev,GRCBASE_MISC+MISC_REGISTERS_RESET_REG_1_CLEAR,0x80000000);
    REG_WR(pdev,GRCBASE_MISC+MISC_REGISTERS_RESET_REG_1_SET,0x80000000);

    init_upb_common( pdev );
    init_xpb_common( pdev );
    init_pbf_common( pdev );

    init_src_common(pdev);
    init_cdu_common(pdev);
    init_cfc_common(pdev);
    init_hc_common(pdev);
    if (!CHIP_IS_E1x(pdev) && GET_FLAGS( pdev->params.test_mode, TEST_MODE_NO_MCP)) {
        /* don't zeroize msix memory - this overrides windows OS initialization */
        REG_WR(pdev,IGU_REG_RESET_MEMORIES,0x36);
    }
    init_igu_common(pdev);
    init_aeu_common(pdev);

    init_pxpcs_common(pdev);
    init_emac0_common(pdev);
    init_emac1_common(pdev);
    init_dbu_common(pdev);
    init_dbg_common(pdev);

    init_mcp_common(pdev);
    init_nig_common(pdev);

    // TBD: E1H - determine whether to move from here, or have "wait for blks done" function
    //finish CFC init
    REG_WAIT_VERIFY_VAL(pdev, CFC_REG_LL_INIT_DONE,1,wait_ms );

    REG_WAIT_VERIFY_VAL(pdev, CFC_REG_AC_INIT_DONE,1,wait_ms);
    // moved here because of timing problem
    REG_WAIT_VERIFY_VAL(pdev, CFC_REG_CAM_INIT_DONE,1,wait_ms);
    // we need to enable inputs here.
    REG_WR(pdev,CFC_REG_DEBUG0,0);

    if (CHIP_IS_E1(pdev))
    {
        // read NIG statistic
        val   = REG_RD(pdev,NIG_REG_STAT2_BRB_OCTET);
        trash = REG_RD(pdev,NIG_REG_STAT2_BRB_OCTET+4);

        // PRS BRB memory setup only after full power cycle
        if(val == 0)
        {
            prs_brb_mem_setup(pdev);
        }
    }

    lm_setup_fan_failure_detection(pdev);

    /* One time initialization of the phy:
    in 2-port-mode - only for the first device on a chip!
    in 4-port-mode - always */

    if ((pdev->vars.load_code == LM_LOADER_RESPONSE_LOAD_COMMON_CHIP) ||
        CHIP_IS_E1x(pdev))
    {
        shmem_base[0] = pdev->hw_info.shmem_base;
        shmem_base2[0] = pdev->hw_info.shmem_base2;

        if (!CHIP_IS_E1x(pdev))
        {
            LM_SHMEM2_READ(pdev, OFFSETOF(shmem2_region_t,other_shmem_base_addr), &shmem_base[1]);
            LM_SHMEM2_READ(pdev, OFFSETOF(shmem2_region_t,other_shmem2_base_addr), &shmem_base2[1]);
        }

        rc = elink_common_init_phy(pdev, shmem_base, shmem_base2, CHIP_ID(pdev));
        DbgBreakIf( ELINK_STATUS_OK != rc );
    }

    //clear PXP2 attentions
    temp = REG_RD(pdev,PXP2_REG_PXP2_INT_STS_CLR_0);

    // set dcc_support in case active
    if(pdev->hw_info.shmem_base2)
    {
        val = (SHMEM_DCC_SUPPORT_DISABLE_ENABLE_PF_TLV | SHMEM_DCC_SUPPORT_BANDWIDTH_ALLOCATION_TLV) ;
        temp = OFFSETOF( shmem2_region_t, dcc_support);
        LM_SHMEM2_WRITE(pdev, temp, val );
    }

    ///Write driver NIV support
    if (IS_MF_NIV_MODE(pdev))
    {
        DbgBreakIf(!pdev->hw_info.shmem_base2);
        LM_SHMEM2_WRITE(pdev,   OFFSETOF( shmem2_region_t, vntag_driver_niv_support),
                                SHMEM_NIV_SUPPORTED_VERSION_ONE );
    }


    enable_blocks_attention(pdev);

    /* For now we enable parity only on E2 as we can recover in E2. */
    if (CHIP_IS_E2(pdev))
    {
        DbgMessage(pdev, INFORM, "Enabling parity errors\n");
        ecore_enable_blocks_parity(pdev);
    }
}
/**
 * @description
 * Calculates BW according to current linespeed and MF
 * configuration  of the function in Mbps.
 * @param pdev
 * @param link_speed - Port rate in Mbps.
 * @param vnic
 *
 * @return u16
 * Return the max BW of the function in Mbps.
 */
u16_t
lm_get_max_bw(IN const lm_device_t  *pdev,
              IN const u32_t        link_speed,
              IN const u8_t         vnic)
{
    u16_t  max_bw   = 0;

    DbgBreakIf(0 == IS_MULTI_VNIC(pdev));

    //global vnic counter
    if(IS_MF_SD_MODE(pdev) || IS_MF_NIV_MODE(pdev))
    {
        // SD max BW in 100Mbps
        max_bw = pdev->params.max_bw[vnic]*100;
    }
    else
    {
        // SI max BW in percentage from the link speed.
        DbgBreakIf(FALSE == IS_MF_SI_MODE(pdev));
        max_bw = (link_speed * pdev->params.max_bw[vnic])/100;
    }
    return max_bw;
}
// initialize congestion managmet params
void lm_cmng_init(struct _lm_device_t *pdev, u32_t port_rate)
{
    u32_t  fair_periodic_timeout_usec = 0;
    u32_t  r_param                    = 0;
    u32_t  vnicWeightSum              = 0;
    u32_t  t_fair                     = 0;
    u8_t   port                       = 0;
    u8_t   vnic                       = 0;
    u32_t  i                          = 0;
    u32_t* buf                        = NULL;
    u8_t   all_zero                   = 0;

    /* CMNG constants, as derived from system spec calculations */
    static const u8_t  DEF_MIN_RATE             = 1      ; /* default MIN rate in case VNIC min rate is configured to zero- 100Mbps */
    static const u32_t RS_PERIODIC_TIMEOUT_USEC = 400    ; /* resolution of the rate shaping timer - 400 usec */
    static const u32_t QM_ARB_BYTES             = 160000 ; /* number of bytes in single QM arbitration cycle - coefficient for calculating the fairness timer */
    static const u32_t MIN_RES                  = 100    ; /* resolution of Min algorithm 1:100 */
    static const u32_t MIN_ABOVE_THRESH         = 32768  ; /* how many bytes above threshold for the minimal credit of Min algorithm*/
    static const u32_t FAIR_MEM                 = 2      ; /* Memory of fairness algorithm - 2 cycles */
    const        u32_t T_FAIR_COEF              = ((MIN_ABOVE_THRESH +  QM_ARB_BYTES) * 8 * MIN_RES); /* Fairness algorithm integration time coefficient- for calculating the actual Tfair */

    // cmng structs
    struct rate_shaping_vars_per_port   cmng_port_rs_vars;
    struct fairness_vars_per_port       cmng_port_fair_vars;
    struct cmng_flags_per_port          cmng_port_flags;

    struct rate_shaping_vars_per_vn     cmng_rs_vnic[MAX_VNIC_NUM];
    struct fairness_vars_per_vn         cmng_fair_vnic[MAX_VNIC_NUM];

    mm_mem_zero(&cmng_port_rs_vars,        sizeof(cmng_port_rs_vars));
    mm_mem_zero(&cmng_port_fair_vars,      sizeof(cmng_port_fair_vars));
    mm_mem_zero(&cmng_port_flags,          sizeof(cmng_port_flags));

    mm_mem_zero(cmng_rs_vnic,    sizeof(cmng_rs_vnic));
    mm_mem_zero(cmng_fair_vnic,  sizeof(cmng_fair_vnic));

    if (CHK_NULL(pdev))
    {
        DbgBreakIf(1);
        return;
    }
    // TBD: E1H - cmng params are currently per port, may change to be per function
    port = PORT_ID(pdev);

    if(IS_MULTI_VNIC(pdev) && pdev->params.cmng_enable)
    {
        //set global values for the algorithms
        r_param                    = port_rate/8;              // currently support 10G only
        fair_periodic_timeout_usec = QM_ARB_BYTES/r_param;     //this is the resolution of the rate shaping timer - 32 usec

        /************************************************************************/
        /* RATE SHAPING - maximum bandwidth for protocol/vnic                   */
        /************************************************************************/
        SET_FLAGS(cmng_port_flags.cmng_enables,CMNG_FLAGS_PER_PORT_RATE_SHAPING_VN);
        //rate shaping per-port variables
        cmng_port_rs_vars.rs_periodic_timeout = RS_PERIODIC_TIMEOUT_USEC / 4;                           //100 micro seconds in SDM ticks = 25 since each tick is 4 microSeconds
        cmng_port_rs_vars.rs_threshold = (u32_t)((5 * RS_PERIODIC_TIMEOUT_USEC * r_param)/4);   //this is the threshold below which no timer arming will occur. 1.25 coefficient is for the threshold to be a little bigger then the real time- to compensate for timer in-accuracy

        //rate shaping per-vnic variables
        for (vnic = 0; vnic < MAX_VNIC_NUM; vnic++)
        {
            //global vnic counter

            //maximal Mbps for this vnic
            cmng_rs_vnic[vnic].vn_counter.rate = lm_get_max_bw(pdev,
                                                               port_rate,
                                                               vnic);

            cmng_rs_vnic[vnic].vn_counter.quota = (u32_t) ((RS_PERIODIC_TIMEOUT_USEC * cmng_rs_vnic[vnic].vn_counter.rate)/8);  //the quota in each timer period - number of bytes transmitted in this period
        }

        /************************************************************************/
        /* FAIRNESS - minimal guaranteed bandwidth for protocol/vnic            */
        /************************************************************************/

        //fairness per-port variables
        t_fair = T_FAIR_COEF / port_rate;                                           //for 10G it is 1000usec. for 1G it is 10000usec.
        cmng_port_fair_vars.fair_threshold   = QM_ARB_BYTES;                        //this is the threshold below which we won't arm the timer anymore
        cmng_port_fair_vars.upper_bound      = (u32_t)(r_param * t_fair * FAIR_MEM);// we multiply by 1e3/8 to get bytes/msec. We don't want the credits to pass a credit of the T_FAIR*FAIR_MEM (algorithm resolution)
        cmng_port_fair_vars.fairness_timeout = fair_periodic_timeout_usec / 4;

        //calculate sum of weights
        all_zero = TRUE;
        vnicWeightSum = 0;
        for (vnic = 0; vnic < MAX_VNIC_NUM; vnic++)
        {
            if (pdev->hw_info.mf_info.min_bw[vnic] == 0)
            {
                if (!GET_FLAGS(pdev->hw_info.mf_info.func_mf_cfg , FUNC_MF_CFG_FUNC_HIDE))
                {
                    pdev->params.min_bw[vnic] = DEF_MIN_RATE;
                }
            }
            else
            {
                all_zero = FALSE;
            }

            vnicWeightSum += pdev->params.min_bw[vnic]*100;
        }
        //fairness per-vnic variables
        //global vnic counter
        if (vnicWeightSum > 0)
        {
            for (vnic = 0; vnic < MAX_VNIC_NUM; vnic++)
            {
                //this is the credit for each period of the fairness algorithm- number of bytes in T_FAIR (this vnic share of the port rate)
                cmng_fair_vnic[vnic].vn_credit_delta = max((u32_t)(pdev->params.min_bw[vnic] * 100 * (T_FAIR_COEF / (8 * vnicWeightSum))),
                                                           (u32_t)(cmng_port_fair_vars.fair_threshold + MIN_ABOVE_THRESH));
                    }
        }
        // IS_DCB_ENABLED isn't updated when this function is called from lm_init_intmem_port
        // but it is called each time the link is up.
        if (( !all_zero )&&(!LM_DCBX_ETS_IS_ENABLED(pdev)))
        {
            SET_FLAGS(cmng_port_flags.cmng_enables,CMNG_FLAGS_PER_PORT_FAIRNESS_VN);
        }
    }

    // fill the data in the internal ram
    // Store per port struct to internal memory
    buf = (u32_t *)&cmng_port_rs_vars;
    ASSERT_STATIC(0 == (sizeof(struct rate_shaping_vars_per_port) % 4)) ;
    for (i = 0; i < sizeof(struct rate_shaping_vars_per_port)/4; i++)
    {
       LM_INTMEM_WRITE32(pdev,(XSTORM_CMNG_PER_PORT_VARS_OFFSET(PORT_ID(pdev)) + OFFSETOF(struct cmng_struct_per_port , rs_vars) + i * 4),
                          buf[i], BAR_XSTRORM_INTMEM);
    }

    buf = (u32_t *)&cmng_port_fair_vars;
    ASSERT_STATIC(0 == (sizeof(struct fairness_vars_per_port) % 4)) ;
    for (i = 0; i < sizeof(struct fairness_vars_per_port)/4; i++)
    {
       LM_INTMEM_WRITE32(pdev,(XSTORM_CMNG_PER_PORT_VARS_OFFSET(PORT_ID(pdev)) + OFFSETOF(struct cmng_struct_per_port , fair_vars) + i * 4),
                          buf[i], BAR_XSTRORM_INTMEM);
    }

    buf = (u32_t *)&cmng_port_flags;
    ASSERT_STATIC(0 == (sizeof(struct cmng_flags_per_port) % 4)) ;
    for (i = 0; i < sizeof(struct cmng_flags_per_port)/4; i++)
    {
       LM_INTMEM_WRITE32(pdev,(XSTORM_CMNG_PER_PORT_VARS_OFFSET(PORT_ID(pdev)) + OFFSETOF(struct cmng_struct_per_port , flags) + i * 4),
                          buf[i], BAR_XSTRORM_INTMEM);
    }

    // store per vnic struct to internal memory rs
    for (vnic = 0; vnic < MAX_VNIC_NUM; vnic++)
    {
        buf = (u32_t *)&cmng_rs_vnic[vnic];
        for (i = 0; i < sizeof(struct rate_shaping_vars_per_vn)/4; i++)
        {
            LM_INTMEM_WRITE32(pdev,XSTORM_RATE_SHAPING_PER_VN_VARS_OFFSET((port+2*vnic))+i*4,
                              buf[i], BAR_XSTRORM_INTMEM);
        }
    }
    // store per vnic struct to internal memory fair
    for (vnic = 0; vnic < MAX_VNIC_NUM; vnic++)
    {
        buf = (u32_t *)&cmng_fair_vnic[vnic];
        for (i = 0; i < sizeof(struct fairness_vars_per_vn)/4; i++)
        {
            LM_INTMEM_WRITE32(pdev,XSTORM_FAIRNESS_PER_VN_VARS_OFFSET((port+2*vnic))+i*4,
                              buf[i], BAR_XSTRORM_INTMEM);
        }
    }
} /* lm_cmng_init */



static void lm_init_intmem_common(struct _lm_device_t *pdev)
{
    /* ip_id_mask (determines how the ip id (ipv4) rolls over, (init value currently constant: 'half')) */
    /* TODO need to add constant in common constant */
    LM_INTMEM_WRITE16(pdev, XSTORM_COMMON_IP_ID_MASK_OFFSET, 0x8000, BAR_XSTRORM_INTMEM);

    LM_INTMEM_WRITE16(pdev, USTORM_ETH_DYNAMIC_HC_PARAM_OFFSET, (u16_t)pdev->params.l2_dynamic_hc_min_bytes_per_packet, BAR_USTRORM_INTMEM);
    DbgBreakIf(USTORM_ETH_DYNAMIC_HC_PARAM_SIZE != sizeof(u16_t));

    if (!CHIP_IS_E1x(pdev)) {
        if (pdev->params.e2_integ_testing_enabled) {
            LM_INTMEM_WRITE8(pdev, XSTORM_E2_INTEG_RAM_OFFSET, 1, BAR_XSTRORM_INTMEM);
            LM_INTMEM_WRITE8(pdev, TSTORM_E2_INTEG_RAM_OFFSET, 1, BAR_TSTRORM_INTMEM);
            DbgMessage(pdev, FATAL, "E2_Integ_Testing_Enabled!!\n");
        }

        DbgBreakIf(CSTORM_IGU_MODE_SIZE != 1);
        if (INTR_BLK_MODE(pdev) == INTR_BLK_MODE_NORM) {
            LM_INTMEM_WRITE8(pdev, CSTORM_IGU_MODE_OFFSET, HC_IGU_NBC_MODE, BAR_CSTRORM_INTMEM);
        } else {
            LM_INTMEM_WRITE8(pdev, CSTORM_IGU_MODE_OFFSET, HC_IGU_BC_MODE, BAR_CSTRORM_INTMEM);
        }
    }
}


static void lm_init_intmem_port(struct _lm_device_t *pdev)
{
    u8_t             func   = 0;

    /* Licensing with no MCP workaround. */
    if (GET_FLAGS( pdev->params.test_mode, TEST_MODE_NO_MCP)) {
        /* If there is no MCP then there is no shmem_base, therefore we write to an absolute address. port 1 is 28 bytes away.  */
        #define SHMEM_ABSOLUTE_LICENSE_ADDRESS 0xaff3c
        DbgMessage1(pdev, WARN, "writing reg: %p\n", SHMEM_ABSOLUTE_LICENSE_ADDRESS + (PORT_ID(pdev) * 0x1c));
        LM_SHMEM_WRITE(pdev, SHMEM_ABSOLUTE_LICENSE_ADDRESS + (PORT_ID(pdev) * 0x1c), 0xffff);
    }

    DbgBreakIf(!pdev->vars.clk_factor);
    if(CHIP_IS_E1H(pdev))
    {
        /* in a non-mf-aware chip, we don't need to take care of all the other functions */
        LM_FOREACH_FUNC_IN_PORT(pdev, func)
        {
            /* Set all mac filter drop flags to '0' to make sure we don't accept packets for vnics that aren't up yet... do this for each vnic! */
            LM_INTMEM_WRITE32(pdev,TSTORM_MAC_FILTER_CONFIG_OFFSET(func) + OFFSETOF(struct tstorm_eth_mac_filter_config, ucast_drop_all), 0, BAR_TSTRORM_INTMEM);
            LM_INTMEM_WRITE32(pdev,TSTORM_MAC_FILTER_CONFIG_OFFSET(func) + OFFSETOF(struct tstorm_eth_mac_filter_config, ucast_accept_all), 0, BAR_TSTRORM_INTMEM);
            LM_INTMEM_WRITE32(pdev,TSTORM_MAC_FILTER_CONFIG_OFFSET(func) + OFFSETOF(struct tstorm_eth_mac_filter_config, mcast_drop_all), 0, BAR_TSTRORM_INTMEM);
            LM_INTMEM_WRITE32(pdev,TSTORM_MAC_FILTER_CONFIG_OFFSET(func) + OFFSETOF(struct tstorm_eth_mac_filter_config, mcast_accept_all), 0, BAR_TSTRORM_INTMEM);
            LM_INTMEM_WRITE32(pdev,TSTORM_MAC_FILTER_CONFIG_OFFSET(func) + OFFSETOF(struct tstorm_eth_mac_filter_config, bcast_accept_all), 0, BAR_TSTRORM_INTMEM);
        }
    }
    // for now only in multi vnic mode for min max cmng
    if (IS_MULTI_VNIC(pdev))
    {
        // first time always use 10000 for 10G
        lm_cmng_init(pdev,10000);
    }

    /* Tx switching is only enabled if in MF SI mode and npar_vm_switching is enabled...*/
    if (IS_MF_SI_MODE(pdev) && pdev->params.npar_vm_switching_enable)
    {
        //In switch independent mode, driver must enable TCP TX switching using XSTORM_TCP_TX_SWITCHING_EN_OFFSET.
        LM_INTMEM_WRITE32(pdev,XSTORM_TCP_TX_SWITCHING_EN_OFFSET(PORT_ID(pdev)), 1, BAR_XSTRORM_INTMEM);
    }
}

static void lm_init_intmem_eq(struct _lm_device_t * pdev)
{
    struct event_ring_data eq_data = {{0}};
    u32_t  addr  = CSTORM_EVENT_RING_DATA_OFFSET(FUNC_ID(pdev));
    u32_t  index = 0;

    eq_data.base_addr.hi = lm_bd_chain_phys_addr(&pdev->eq_info.eq_chain.bd_chain, 0).as_u32.high;
    eq_data.base_addr.lo = lm_bd_chain_phys_addr(&pdev->eq_info.eq_chain.bd_chain, 0).as_u32.low;
    eq_data.producer     = lm_bd_chain_prod_idx(&pdev->eq_info.eq_chain.bd_chain);
    eq_data.index_id     = HC_SP_INDEX_EQ_CONS;
    eq_data.sb_id        = DEF_STATUS_BLOCK_INDEX;

    for (index = 0; index < sizeof(struct event_ring_data) / sizeof(u32_t); index++)
    {
        LM_INTMEM_WRITE32(pdev, addr + (sizeof(u32_t) * index), *((u32 *)&eq_data + index), BAR_CSTRORM_INTMEM);
    }
}

static void lm_init_intmem_function(struct _lm_device_t *pdev)
{
    u8_t const      func                                    = FUNC_ID(pdev);

    /* status blocks are done in init_status_blocks() */    /* need to be write using GRC don't generate interrupt spq prod init WB */
    REG_WR(pdev,XSEM_REG_FAST_MEMORY + (XSTORM_SPQ_PAGE_BASE_OFFSET(func)),pdev->sq_info.sq_chain.bd_chain_phy.as_u32.low);
    REG_WR(pdev,XSEM_REG_FAST_MEMORY + (XSTORM_SPQ_PAGE_BASE_OFFSET(func)) + 4,pdev->sq_info.sq_chain.bd_chain_phy.as_u32.high);
    REG_WR(pdev,XSEM_REG_FAST_MEMORY + (XSTORM_SPQ_PROD_OFFSET(func)),pdev->sq_info.sq_chain.prod_idx);

    /* Initialize the event-queue */
    lm_init_intmem_eq(pdev);

    /* Todo: Init indirection table */

    if(CHIP_IS_E1(pdev))
    {
        // Should run only for E1 (begining fw 6.4.10). In earlier versions (e.g. 6.2) the workaorund is relevant for E1.5 as well.
        /* add for PXP dual port memory setup */
        DbgBreakIf(lm_bd_chain_phys_addr(&pdev->eq_info.eq_chain.bd_chain, 0).as_u64 == 0);
        LM_INTMEM_WRITE32(pdev,USTORM_MEM_WORKAROUND_ADDRESS_OFFSET(func),lm_bd_chain_phys_addr(&pdev->eq_info.eq_chain.bd_chain, 0).as_u32.low, BAR_USTRORM_INTMEM); /* need to check */
        LM_INTMEM_WRITE32(pdev,USTORM_MEM_WORKAROUND_ADDRESS_OFFSET(func)+4,lm_bd_chain_phys_addr(&pdev->eq_info.eq_chain.bd_chain, 0).as_u32.high, BAR_USTRORM_INTMEM); /* need to check */
    }


    ASSERT_STATIC( 3 == ARRSIZE(pdev->vars.int_coal.eth_dynamic_hc_cfg.sm_config[0].threshold) ) ;

    //init dynamic hc
    LM_INTMEM_WRITE32(pdev,CSTORM_DYNAMIC_HC_CONFIG_OFFSET(func), pdev->vars.int_coal.eth_dynamic_hc_cfg.sm_config[SM_RX_ID].threshold[0], BAR_CSTRORM_INTMEM);
    LM_INTMEM_WRITE32(pdev,CSTORM_DYNAMIC_HC_CONFIG_OFFSET(func)+4, pdev->vars.int_coal.eth_dynamic_hc_cfg.sm_config[SM_RX_ID].threshold[1], BAR_CSTRORM_INTMEM);
    LM_INTMEM_WRITE32(pdev,CSTORM_DYNAMIC_HC_CONFIG_OFFSET(func)+8, pdev->vars.int_coal.eth_dynamic_hc_cfg.sm_config[SM_RX_ID].threshold[2], BAR_CSTRORM_INTMEM);

    /*Set DHC scaling factor for L4*/
    LM_INTMEM_WRITE8(pdev,CSTORM_DYNAMIC_HC_CONFIG_OFFSET(func)+12, (16 - (u8_t)pdev->params.l4_hc_scaling_factor), BAR_CSTRORM_INTMEM);

    /*Reset DHC scaling factors for rest of protocols*/
    LM_INTMEM_WRITE8(pdev,CSTORM_DYNAMIC_HC_CONFIG_OFFSET(func)+13, 0, BAR_CSTRORM_INTMEM);
    LM_INTMEM_WRITE8(pdev,CSTORM_DYNAMIC_HC_CONFIG_OFFSET(func)+14, 0, BAR_CSTRORM_INTMEM);
    LM_INTMEM_WRITE8(pdev,CSTORM_DYNAMIC_HC_CONFIG_OFFSET(func)+15, 0, BAR_CSTRORM_INTMEM);

    ASSERT_STATIC( 4 == ARRSIZE(pdev->vars.int_coal.eth_dynamic_hc_cfg.sm_config[SM_RX_ID].hc_timeout0) ) ;
    ASSERT_STATIC( 4 == ARRSIZE(pdev->vars.int_coal.eth_dynamic_hc_cfg.sm_config[SM_RX_ID].hc_timeout1) ) ;
    ASSERT_STATIC( 4 == ARRSIZE(pdev->vars.int_coal.eth_dynamic_hc_cfg.sm_config[SM_RX_ID].hc_timeout2) ) ;
    ASSERT_STATIC( 4 == ARRSIZE(pdev->vars.int_coal.eth_dynamic_hc_cfg.sm_config[SM_RX_ID].hc_timeout3) ) ;

    /*Set DHC timeout 0 for all protocols*/
    LM_INTMEM_WRITE8(pdev,CSTORM_DYNAMIC_HC_CONFIG_OFFSET(func)+16, pdev->vars.int_coal.eth_dynamic_hc_cfg.sm_config[SM_RX_ID].hc_timeout0[0], BAR_CSTRORM_INTMEM);
    LM_INTMEM_WRITE8(pdev,CSTORM_DYNAMIC_HC_CONFIG_OFFSET(func)+17, pdev->vars.int_coal.eth_dynamic_hc_cfg.sm_config[SM_RX_ID].hc_timeout0[1], BAR_CSTRORM_INTMEM);
    LM_INTMEM_WRITE8(pdev,CSTORM_DYNAMIC_HC_CONFIG_OFFSET(func)+18, pdev->vars.int_coal.eth_dynamic_hc_cfg.sm_config[SM_RX_ID].hc_timeout0[2], BAR_CSTRORM_INTMEM);
    LM_INTMEM_WRITE8(pdev,CSTORM_DYNAMIC_HC_CONFIG_OFFSET(func)+19, pdev->vars.int_coal.eth_dynamic_hc_cfg.sm_config[SM_RX_ID].hc_timeout0[3], BAR_CSTRORM_INTMEM);

    /*Set DHC timeout 1 for all protocols*/
    LM_INTMEM_WRITE8(pdev,CSTORM_DYNAMIC_HC_CONFIG_OFFSET(func)+20, pdev->vars.int_coal.eth_dynamic_hc_cfg.sm_config[SM_RX_ID].hc_timeout1[0], BAR_CSTRORM_INTMEM);
    LM_INTMEM_WRITE8(pdev,CSTORM_DYNAMIC_HC_CONFIG_OFFSET(func)+21, pdev->vars.int_coal.eth_dynamic_hc_cfg.sm_config[SM_RX_ID].hc_timeout1[1], BAR_CSTRORM_INTMEM);
    LM_INTMEM_WRITE8(pdev,CSTORM_DYNAMIC_HC_CONFIG_OFFSET(func)+22, pdev->vars.int_coal.eth_dynamic_hc_cfg.sm_config[SM_RX_ID].hc_timeout1[2], BAR_CSTRORM_INTMEM);
    LM_INTMEM_WRITE8(pdev,CSTORM_DYNAMIC_HC_CONFIG_OFFSET(func)+23, pdev->vars.int_coal.eth_dynamic_hc_cfg.sm_config[SM_RX_ID].hc_timeout1[3], BAR_CSTRORM_INTMEM);

    /*Set DHC timeout 2 for all protocols*/
    LM_INTMEM_WRITE8(pdev,CSTORM_DYNAMIC_HC_CONFIG_OFFSET(func)+24, pdev->vars.int_coal.eth_dynamic_hc_cfg.sm_config[SM_RX_ID].hc_timeout2[0], BAR_CSTRORM_INTMEM);
    LM_INTMEM_WRITE8(pdev,CSTORM_DYNAMIC_HC_CONFIG_OFFSET(func)+25, pdev->vars.int_coal.eth_dynamic_hc_cfg.sm_config[SM_RX_ID].hc_timeout2[1], BAR_CSTRORM_INTMEM);
    LM_INTMEM_WRITE8(pdev,CSTORM_DYNAMIC_HC_CONFIG_OFFSET(func)+26, pdev->vars.int_coal.eth_dynamic_hc_cfg.sm_config[SM_RX_ID].hc_timeout2[2], BAR_CSTRORM_INTMEM);
    LM_INTMEM_WRITE8(pdev,CSTORM_DYNAMIC_HC_CONFIG_OFFSET(func)+27, pdev->vars.int_coal.eth_dynamic_hc_cfg.sm_config[SM_RX_ID].hc_timeout2[3], BAR_CSTRORM_INTMEM);

    /*Set DHC timeout 3 for all protocols*/
    LM_INTMEM_WRITE8(pdev,CSTORM_DYNAMIC_HC_CONFIG_OFFSET(func)+28, pdev->vars.int_coal.eth_dynamic_hc_cfg.sm_config[SM_RX_ID].hc_timeout3[0], BAR_CSTRORM_INTMEM);
    LM_INTMEM_WRITE8(pdev,CSTORM_DYNAMIC_HC_CONFIG_OFFSET(func)+29, pdev->vars.int_coal.eth_dynamic_hc_cfg.sm_config[SM_RX_ID].hc_timeout3[1], BAR_CSTRORM_INTMEM);
    LM_INTMEM_WRITE8(pdev,CSTORM_DYNAMIC_HC_CONFIG_OFFSET(func)+30, pdev->vars.int_coal.eth_dynamic_hc_cfg.sm_config[SM_RX_ID].hc_timeout3[2], BAR_CSTRORM_INTMEM);
    LM_INTMEM_WRITE8(pdev,CSTORM_DYNAMIC_HC_CONFIG_OFFSET(func)+31, pdev->vars.int_coal.eth_dynamic_hc_cfg.sm_config[SM_RX_ID].hc_timeout3[3], BAR_CSTRORM_INTMEM);

#define TX_DHC_OFFSET   32
    LM_INTMEM_WRITE32(pdev,CSTORM_DYNAMIC_HC_CONFIG_OFFSET(func)+TX_DHC_OFFSET, pdev->vars.int_coal.eth_dynamic_hc_cfg.sm_config[SM_TX_ID].threshold[0], BAR_CSTRORM_INTMEM);
    LM_INTMEM_WRITE32(pdev,CSTORM_DYNAMIC_HC_CONFIG_OFFSET(func)+TX_DHC_OFFSET+4, pdev->vars.int_coal.eth_dynamic_hc_cfg.sm_config[SM_TX_ID].threshold[1], BAR_CSTRORM_INTMEM);
    LM_INTMEM_WRITE32(pdev,CSTORM_DYNAMIC_HC_CONFIG_OFFSET(func)+TX_DHC_OFFSET+8, pdev->vars.int_coal.eth_dynamic_hc_cfg.sm_config[SM_TX_ID].threshold[2], BAR_CSTRORM_INTMEM);


    /*Reset DHC scaling factors for all protocols*/
    LM_INTMEM_WRITE8(pdev,CSTORM_DYNAMIC_HC_CONFIG_OFFSET(func)+TX_DHC_OFFSET+12, 0, BAR_CSTRORM_INTMEM);
    LM_INTMEM_WRITE8(pdev,CSTORM_DYNAMIC_HC_CONFIG_OFFSET(func)+TX_DHC_OFFSET+13, 0, BAR_CSTRORM_INTMEM);
    LM_INTMEM_WRITE8(pdev,CSTORM_DYNAMIC_HC_CONFIG_OFFSET(func)+TX_DHC_OFFSET+14, 0, BAR_CSTRORM_INTMEM);
    LM_INTMEM_WRITE8(pdev,CSTORM_DYNAMIC_HC_CONFIG_OFFSET(func)+TX_DHC_OFFSET+15, 0, BAR_CSTRORM_INTMEM);

    /*Set DHC timeout 0 for all protocols*/
    LM_INTMEM_WRITE8(pdev,CSTORM_DYNAMIC_HC_CONFIG_OFFSET(func)+TX_DHC_OFFSET+16, pdev->vars.int_coal.eth_dynamic_hc_cfg.sm_config[SM_TX_ID].hc_timeout0[0], BAR_CSTRORM_INTMEM);
    LM_INTMEM_WRITE8(pdev,CSTORM_DYNAMIC_HC_CONFIG_OFFSET(func)+TX_DHC_OFFSET+17, pdev->vars.int_coal.eth_dynamic_hc_cfg.sm_config[SM_TX_ID].hc_timeout0[1], BAR_CSTRORM_INTMEM);
    LM_INTMEM_WRITE8(pdev,CSTORM_DYNAMIC_HC_CONFIG_OFFSET(func)+TX_DHC_OFFSET+18, pdev->vars.int_coal.eth_dynamic_hc_cfg.sm_config[SM_TX_ID].hc_timeout0[2], BAR_CSTRORM_INTMEM);
    LM_INTMEM_WRITE8(pdev,CSTORM_DYNAMIC_HC_CONFIG_OFFSET(func)+TX_DHC_OFFSET+19, pdev->vars.int_coal.eth_dynamic_hc_cfg.sm_config[SM_TX_ID].hc_timeout0[3], BAR_CSTRORM_INTMEM);

    /*Set DHC timeout 1 for all protocols*/
    LM_INTMEM_WRITE8(pdev,CSTORM_DYNAMIC_HC_CONFIG_OFFSET(func)+TX_DHC_OFFSET+20, pdev->vars.int_coal.eth_dynamic_hc_cfg.sm_config[SM_TX_ID].hc_timeout1[0], BAR_CSTRORM_INTMEM);
    LM_INTMEM_WRITE8(pdev,CSTORM_DYNAMIC_HC_CONFIG_OFFSET(func)+TX_DHC_OFFSET+21, pdev->vars.int_coal.eth_dynamic_hc_cfg.sm_config[SM_TX_ID].hc_timeout1[1], BAR_CSTRORM_INTMEM);
    LM_INTMEM_WRITE8(pdev,CSTORM_DYNAMIC_HC_CONFIG_OFFSET(func)+TX_DHC_OFFSET+22, pdev->vars.int_coal.eth_dynamic_hc_cfg.sm_config[SM_TX_ID].hc_timeout1[2], BAR_CSTRORM_INTMEM);
    LM_INTMEM_WRITE8(pdev,CSTORM_DYNAMIC_HC_CONFIG_OFFSET(func)+TX_DHC_OFFSET+23, pdev->vars.int_coal.eth_dynamic_hc_cfg.sm_config[SM_TX_ID].hc_timeout1[3], BAR_CSTRORM_INTMEM);

    /*Set DHC timeout 2 for all protocols*/
    LM_INTMEM_WRITE8(pdev,CSTORM_DYNAMIC_HC_CONFIG_OFFSET(func)+TX_DHC_OFFSET+24, pdev->vars.int_coal.eth_dynamic_hc_cfg.sm_config[SM_TX_ID].hc_timeout2[0], BAR_CSTRORM_INTMEM);
    LM_INTMEM_WRITE8(pdev,CSTORM_DYNAMIC_HC_CONFIG_OFFSET(func)+TX_DHC_OFFSET+25, pdev->vars.int_coal.eth_dynamic_hc_cfg.sm_config[SM_TX_ID].hc_timeout2[1], BAR_CSTRORM_INTMEM);
    LM_INTMEM_WRITE8(pdev,CSTORM_DYNAMIC_HC_CONFIG_OFFSET(func)+TX_DHC_OFFSET+26, pdev->vars.int_coal.eth_dynamic_hc_cfg.sm_config[SM_TX_ID].hc_timeout2[2], BAR_CSTRORM_INTMEM);
    LM_INTMEM_WRITE8(pdev,CSTORM_DYNAMIC_HC_CONFIG_OFFSET(func)+TX_DHC_OFFSET+27, pdev->vars.int_coal.eth_dynamic_hc_cfg.sm_config[SM_TX_ID].hc_timeout2[3], BAR_CSTRORM_INTMEM);

    /*Set DHC timeout 3 for all protocols*/
    LM_INTMEM_WRITE8(pdev,CSTORM_DYNAMIC_HC_CONFIG_OFFSET(func)+TX_DHC_OFFSET+28, pdev->vars.int_coal.eth_dynamic_hc_cfg.sm_config[SM_TX_ID].hc_timeout3[0], BAR_CSTRORM_INTMEM);
    LM_INTMEM_WRITE8(pdev,CSTORM_DYNAMIC_HC_CONFIG_OFFSET(func)+TX_DHC_OFFSET+29, pdev->vars.int_coal.eth_dynamic_hc_cfg.sm_config[SM_TX_ID].hc_timeout3[1], BAR_CSTRORM_INTMEM);
    LM_INTMEM_WRITE8(pdev,CSTORM_DYNAMIC_HC_CONFIG_OFFSET(func)+TX_DHC_OFFSET+30, pdev->vars.int_coal.eth_dynamic_hc_cfg.sm_config[SM_TX_ID].hc_timeout3[2], BAR_CSTRORM_INTMEM);
    LM_INTMEM_WRITE8(pdev,CSTORM_DYNAMIC_HC_CONFIG_OFFSET(func)+TX_DHC_OFFSET+31, pdev->vars.int_coal.eth_dynamic_hc_cfg.sm_config[SM_TX_ID].hc_timeout3[3], BAR_CSTRORM_INTMEM);

    /* E1H specific init */
    if (pdev->params.disable_patent_using) {
        DbgMessage(pdev, WARN,"Patent is disabled\n");
        LM_INTMEM_WRITE8(pdev, TSTORM_TCP_GLOBAL_PARAMS_OFFSET, 0, BAR_TSTRORM_INTMEM);
    }

    /* Enable the function in STORMs */
    LM_INTMEM_WRITE8(pdev, XSTORM_VF_TO_PF_OFFSET(FUNC_ID(pdev)), FUNC_ID(pdev), BAR_XSTRORM_INTMEM);
    LM_INTMEM_WRITE8(pdev, CSTORM_VF_TO_PF_OFFSET(FUNC_ID(pdev)), FUNC_ID(pdev), BAR_CSTRORM_INTMEM);
    LM_INTMEM_WRITE8(pdev, TSTORM_VF_TO_PF_OFFSET(FUNC_ID(pdev)), FUNC_ID(pdev), BAR_TSTRORM_INTMEM);
    LM_INTMEM_WRITE8(pdev, USTORM_VF_TO_PF_OFFSET(FUNC_ID(pdev)), FUNC_ID(pdev), BAR_USTRORM_INTMEM);

    LM_INTMEM_WRITE8(pdev, XSTORM_FUNC_EN_OFFSET(FUNC_ID(pdev)), 1, BAR_XSTRORM_INTMEM);
    LM_INTMEM_WRITE8(pdev, CSTORM_FUNC_EN_OFFSET(FUNC_ID(pdev)), 1, BAR_CSTRORM_INTMEM);
    LM_INTMEM_WRITE8(pdev, TSTORM_FUNC_EN_OFFSET(FUNC_ID(pdev)), 1, BAR_TSTRORM_INTMEM);
    LM_INTMEM_WRITE8(pdev, USTORM_FUNC_EN_OFFSET(FUNC_ID(pdev)), 1, BAR_USTRORM_INTMEM);
}

/* set interrupt coalesing parameters.
   - these settings are derived from user configured interrupt coalesing mode and tx/rx interrupts rate (lm params).
   - these settings are used for status blocks initialization */
static void lm_set_int_coal_info(struct _lm_device_t *pdev)
{
    lm_int_coalesing_info* ic           = &pdev->vars.int_coal;
    u32_t                  rx_coal_usec[HC_USTORM_SB_NUM_INDICES];
    u32_t                  tx_coal_usec[HC_CSTORM_SB_NUM_INDICES];
    u32_t                  i            = 0;

    mm_mem_zero( ic, sizeof(lm_int_coalesing_info) );

    for (i = 0; i < HC_USTORM_SB_NUM_INDICES; i++) {
        rx_coal_usec[i] = 0;
    }

    for (i = 0; i < HC_CSTORM_SB_NUM_INDICES; i++) {
        tx_coal_usec[i] = 0;
    }

    switch (pdev->params.int_coalesing_mode)
    {
    case LM_INT_COAL_PERIODIC_SYNC: /* static periodic sync */
        for (i = 0; i < HC_USTORM_SB_NUM_INDICES; i++) {
            if (pdev->params.int_per_sec_rx_override)
                pdev->params.int_per_sec_rx[i] = pdev->params.int_per_sec_rx_override;

            DbgMessage2(pdev, WARN, "##lm_set_int_coal_info: int_per_sec_rx[%d] = %d\n",i,pdev->params.int_per_sec_rx[i]);
            if (pdev->params.int_per_sec_rx[i])
            {
                rx_coal_usec[i] = 1000000 / pdev->params.int_per_sec_rx[i];
            }
            if(rx_coal_usec[i] > 0x3ff)
            {
                rx_coal_usec[i] = 0x3ff; /* min 1k us, i.e. 1k int per sec */
            }
        }

        for (i = 0; i < HC_CSTORM_SB_NUM_INDICES; i++) {
            if (pdev->params.int_per_sec_tx_override)
                pdev->params.int_per_sec_tx[i] = pdev->params.int_per_sec_tx_override;

            DbgMessage2(pdev, WARN, "##lm_set_int_coal_info: int_per_sec_tx[%d] = %d\n",i,pdev->params.int_per_sec_tx[i]);

            if (pdev->params.int_per_sec_tx[i])
            {
                tx_coal_usec[i] = 1000000 / pdev->params.int_per_sec_tx[i];
            }
            if(tx_coal_usec[i] > 0x3ff)
            {
                tx_coal_usec[i] = 0x3ff; /* min 1k us, i.e. 1k int per sec */
            }
        }
        break;

    case LM_INT_COAL_NONE: /* this is the default */
    default:
        break;
    }

    /* set hc period for c sb for all indices */
    for (i = 0; i < HC_CSTORM_SB_NUM_INDICES; i++) {
        ic->hc_usec_c_sb[i] = tx_coal_usec[i];
    }
    /* set hc period for u sb for all indices */
    for (i = 0; i < HC_USTORM_SB_NUM_INDICES; i++) {
        ic->hc_usec_u_sb[i] = rx_coal_usec[i];
    }

#if 0
    if (pdev->params.l4_fw_dca_enabled) {
        /* set TOE HC to minimum possible for ustorm */
        ic->hc_usec_u_sb[HC_INDEX_U_TOE_RX_CQ_CONS] = pdev->params.l4_hc_ustorm_thresh;  /* 12usec */
    }
#endif

    /* by default set hc period for x/t/c/u defualt sb to NONE.
      (that was already implicitly done by memset 0 above) */


    /* set dynamic hc params */
    for (i = 0; i < HC_USTORM_SB_NUM_INDICES; i++) {
        ic->eth_dynamic_hc_cfg.sm_config[SM_RX_ID].hc_timeout0[i] = (u8_t)pdev->params.hc_timeout0[SM_RX_ID][i];
        ic->eth_dynamic_hc_cfg.sm_config[SM_RX_ID].hc_timeout1[i] = (u8_t)pdev->params.hc_timeout1[SM_RX_ID][i];
        ic->eth_dynamic_hc_cfg.sm_config[SM_RX_ID].hc_timeout2[i] = (u8_t)pdev->params.hc_timeout2[SM_RX_ID][i];
        ic->eth_dynamic_hc_cfg.sm_config[SM_RX_ID].hc_timeout3[i] = (u8_t)pdev->params.hc_timeout3[SM_RX_ID][i];
    }
    ic->eth_dynamic_hc_cfg.sm_config[SM_RX_ID].threshold[0] = pdev->params.hc_threshold0[SM_RX_ID];
    ic->eth_dynamic_hc_cfg.sm_config[SM_RX_ID].threshold[1] = pdev->params.hc_threshold1[SM_RX_ID];
    ic->eth_dynamic_hc_cfg.sm_config[SM_RX_ID].threshold[2] = pdev->params.hc_threshold2[SM_RX_ID];

    for (i = 0; i < HC_CSTORM_SB_NUM_INDICES; i++) {
        ic->eth_dynamic_hc_cfg.sm_config[SM_TX_ID].hc_timeout0[i] = (u8_t)pdev->params.hc_timeout0[SM_TX_ID][i];
        ic->eth_dynamic_hc_cfg.sm_config[SM_TX_ID].hc_timeout1[i] = (u8_t)pdev->params.hc_timeout1[SM_TX_ID][i];
        ic->eth_dynamic_hc_cfg.sm_config[SM_TX_ID].hc_timeout2[i] = (u8_t)pdev->params.hc_timeout2[SM_TX_ID][i];
        ic->eth_dynamic_hc_cfg.sm_config[SM_TX_ID].hc_timeout3[i] = (u8_t)pdev->params.hc_timeout3[SM_TX_ID][i];
    }
    ic->eth_dynamic_hc_cfg.sm_config[SM_TX_ID].threshold[0] = pdev->params.hc_threshold0[SM_TX_ID];
    ic->eth_dynamic_hc_cfg.sm_config[SM_TX_ID].threshold[1] = pdev->params.hc_threshold1[SM_TX_ID];
    ic->eth_dynamic_hc_cfg.sm_config[SM_TX_ID].threshold[2] = pdev->params.hc_threshold2[SM_TX_ID];
}


static void init_hc_attn_status_block(struct _lm_device_t *pdev,
                              u8_t  sb_id,
                              lm_address_t *host_sb_addr)
{
    volatile struct atten_sp_status_block * attention_sb = NULL;
    //give the IGU the status block number(ID) of attention bits section.
    DbgBreakIf(!pdev);

    DbgMessage2(pdev, INFORMi, "init_status_block: host_sb_addr_low:0x%x; host_sb_addr_low:0x%x\n",
                    host_sb_addr->as_u32.low, host_sb_addr->as_u32.high);
    attention_sb = lm_get_attention_status_block(pdev);
    attention_sb->status_block_id = sb_id;
    //write to IGU the physical address where the attention bits lie
    REG_WR(pdev,  HC_REG_ATTN_MSG0_ADDR_L + 8*PORT_ID(pdev), host_sb_addr->as_u32.low);
    REG_WR(pdev,  HC_REG_ATTN_MSG0_ADDR_H + 8*PORT_ID(pdev), host_sb_addr->as_u32.high);
}

static void init_igu_attn_status_block(
    struct _lm_device_t *pdev,
    lm_address_t *host_sb_addr)
{

    //write to IGU the physical address where the attention bits lie
    REG_WR(pdev,  IGU_REG_ATTN_MSG_ADDR_L, host_sb_addr->as_u32.low);
    REG_WR(pdev,  IGU_REG_ATTN_MSG_ADDR_H, host_sb_addr->as_u32.high);

    DbgMessage2(pdev, INFORMi, "init_attn_igu_status_block: host_sb_addr_low:0x%x; host_sb_addr_low:0x%x\n",
                host_sb_addr->as_u32.low, host_sb_addr->as_u32.high);


}

static void init_attn_status_block(struct _lm_device_t *pdev,
                              u8_t  sb_id,
                              lm_address_t *host_sb_addr)
{
    if (INTR_BLK_TYPE(pdev) == INTR_BLK_HC) {
        init_hc_attn_status_block(pdev,sb_id,host_sb_addr);
    } else {
        init_igu_attn_status_block(pdev, host_sb_addr);
    }
}

static void lm_init_sp_status_block(struct _lm_device_t *pdev)
{
    lm_address_t    sb_phy_addr;
    u8_t igu_sp_sb_index; /* igu Status Block constant identifier (0-135) */
    u8_t igu_seg_id;
    u8_t func;
    u8_t i;

    DbgBreakIf(!pdev);
    DbgBreakIf(IS_VFDEV(pdev));

    DbgBreakIf((CSTORM_SP_STATUS_BLOCK_SIZE % 4) != 0);
    DbgBreakIf((CSTORM_SP_STATUS_BLOCK_DATA_SIZE % 4) != 0);
    DbgBreakIf((CSTORM_SP_SYNC_BLOCK_SIZE % 4) != 0);
    func = FUNC_ID(pdev);

    if ((INTR_BLK_TYPE(pdev) == INTR_BLK_IGU) && (INTR_BLK_MODE(pdev) == INTR_BLK_MODE_NORM) ) {
        igu_sp_sb_index = IGU_DSB_ID(pdev);
        igu_seg_id = IGU_SEG_ACCESS_DEF;
    } else {
        igu_sp_sb_index = DEF_STATUS_BLOCK_IGU_INDEX;
        igu_seg_id = HC_SEG_ACCESS_DEF;
    }

    sb_phy_addr = pdev->vars.gen_sp_status_block.blk_phy_address;

    init_attn_status_block(pdev, igu_sp_sb_index, &sb_phy_addr);

    LM_INC64(&sb_phy_addr, OFFSETOF(struct host_sp_status_block, sp_sb));

    /* CQ#46240: Disable the function in the status-block data before nullifying sync-line + status-block */
    LM_INTMEM_WRITE8(PFDEV(pdev), (CSTORM_SP_STATUS_BLOCK_DATA_OFFSET(func) + OFFSETOF(struct hc_sp_status_block_data, p_func.pf_id)), 0xff, BAR_CSTRORM_INTMEM);
    LM_INTMEM_WRITE8(PFDEV(pdev), (CSTORM_SP_STATUS_BLOCK_DATA_OFFSET(func) + OFFSETOF(struct hc_sp_status_block_data, p_func.vf_id)), 0xff, BAR_CSTRORM_INTMEM);
    LM_INTMEM_WRITE8(PFDEV(pdev), (CSTORM_SP_STATUS_BLOCK_DATA_OFFSET(func) + OFFSETOF(struct hc_sp_status_block_data, p_func.vf_valid)), FALSE, BAR_CSTRORM_INTMEM);

    REG_WR_DMAE_LEN_ZERO(pdev, CSEM_REG_FAST_MEMORY + CSTORM_SP_SYNC_BLOCK_OFFSET(func), CSTORM_SP_SYNC_BLOCK_SIZE/4);
    REG_WR_DMAE_LEN_ZERO(pdev, CSEM_REG_FAST_MEMORY + CSTORM_SP_STATUS_BLOCK_OFFSET(func), CSTORM_SP_STATUS_BLOCK_SIZE/4);
    REG_WR_DMAE_LEN_ZERO(pdev, CSEM_REG_FAST_MEMORY + CSTORM_SP_STATUS_BLOCK_DATA_OFFSET(func), CSTORM_SP_STATUS_BLOCK_DATA_SIZE/4);


    pdev->vars.gen_sp_status_block.sb_data.host_sb_addr.lo = sb_phy_addr.as_u32.low;
    pdev->vars.gen_sp_status_block.sb_data.host_sb_addr.hi = sb_phy_addr.as_u32.high;
    pdev->vars.gen_sp_status_block.sb_data.igu_sb_id = igu_sp_sb_index;
    pdev->vars.gen_sp_status_block.sb_data.igu_seg_id = igu_seg_id;
    pdev->vars.gen_sp_status_block.sb_data.p_func.pf_id = func;
    pdev->vars.gen_sp_status_block.sb_data.p_func.vnic_id = VNIC_ID(pdev);
    pdev->vars.gen_sp_status_block.sb_data.p_func.vf_id = 0xff;
    pdev->vars.gen_sp_status_block.sb_data.p_func.vf_valid = FALSE;

    for (i = 0; i < sizeof(struct hc_sp_status_block_data)/sizeof(u32_t); i++) {
        LM_INTMEM_WRITE32(PFDEV(pdev), CSTORM_SP_STATUS_BLOCK_DATA_OFFSET(func) + i*sizeof(u32_t), *((u32_t*)&pdev->vars.gen_sp_status_block.sb_data + i), BAR_CSTRORM_INTMEM);
    }


}


void lm_setup_ndsb_index(struct _lm_device_t *pdev, u8_t sb_id, u8_t idx, u8_t sm_idx, u8_t timeout, u8_t dhc_enable)
{
    struct hc_index_data * hc_index_entry;
    if (CHIP_IS_E1x(pdev)) {
        hc_index_entry = pdev->vars.status_blocks_arr[sb_id].hc_status_block_data.e1x_sb_data.index_data + idx;
    } else {
        hc_index_entry = pdev->vars.status_blocks_arr[sb_id].hc_status_block_data.e2_sb_data.index_data + idx;
    }
    hc_index_entry->timeout = timeout;
    hc_index_entry->flags = (sm_idx << HC_INDEX_DATA_SM_ID_SHIFT) & HC_INDEX_DATA_SM_ID;
    if (timeout) {
        hc_index_entry->flags |= HC_INDEX_DATA_HC_ENABLED;
    }
    if (dhc_enable) {
        hc_index_entry->flags |= HC_INDEX_DATA_DYNAMIC_HC_ENABLED;
    }
}

void lm_setup_ndsb_state_machine(struct _lm_device_t *pdev, u8_t sb_id, u8_t sm_id, u8_t igu_sb_id, u8_t igu_seg_id)
{
    struct hc_status_block_sm  * hc_state_machine;
    if (CHIP_IS_E1x(pdev)) {
        hc_state_machine = pdev->vars.status_blocks_arr[sb_id].hc_status_block_data.e1x_sb_data.common.state_machine + sm_id;
    } else {
        hc_state_machine = pdev->vars.status_blocks_arr[sb_id].hc_status_block_data.e2_sb_data.common.state_machine + sm_id;
    }

    hc_state_machine->igu_sb_id = igu_sb_id;
    hc_state_machine->igu_seg_id = igu_seg_id;
    hc_state_machine->timer_value = 0xFF;
    hc_state_machine->time_to_expire = 0xFFFFFFFF;
}

/**
 * This function disables the function in the non-default status
 * block data. The need came with CQ#46240 - to avoid a race in
 * FW where sync-line was older than status-block, and overrid
 * valid value. This function needs to be called before
 * nullifying sync-line + status-block when the entire status
 * block is nullified
 *
 * @param pdev
 * @param fw_sb_id
 */
static void lm_ndsb_func_disable(struct _lm_device_t *pdev, u8_t fw_sb_id)
{
    u32_t sb_data_pfunc_offset = 0;

    /* CQ#46240: Disable the function in the status-block data before nullifying sync-line + status-block */
    if (CHIP_IS_E1x(pdev))
    {
        sb_data_pfunc_offset = CSTORM_STATUS_BLOCK_DATA_OFFSET(fw_sb_id) + OFFSETOF(struct hc_status_block_data_e1x, common.p_func);
    }
    else
    {
        sb_data_pfunc_offset = CSTORM_STATUS_BLOCK_DATA_OFFSET(fw_sb_id) + OFFSETOF(struct hc_status_block_data_e2, common.p_func);
    }
    LM_INTMEM_WRITE8(PFDEV(pdev), (sb_data_pfunc_offset + OFFSETOF(struct pci_entity, pf_id)), 0xff, BAR_CSTRORM_INTMEM);
    LM_INTMEM_WRITE8(PFDEV(pdev), (sb_data_pfunc_offset + OFFSETOF(struct pci_entity, vf_id)), 0xff, BAR_CSTRORM_INTMEM);
    LM_INTMEM_WRITE8(PFDEV(pdev), (sb_data_pfunc_offset + OFFSETOF(struct pci_entity, vf_valid)), FALSE, BAR_CSTRORM_INTMEM);
}

void lm_init_non_def_status_block(struct _lm_device_t *pdev,
                              u8_t  sb_id,
                              u8_t  port)
{
    lm_int_coalesing_info *ic  = &pdev->vars.int_coal;
    u8_t index                 = 0;
    const u8_t fw_sb_id        = LM_FW_SB_ID(pdev, sb_id);
    const u8_t dhc_qzone_id    = LM_FW_DHC_QZONE_ID(pdev, sb_id);
    const u8_t byte_counter_id = CHIP_IS_E1x(pdev)? fw_sb_id : dhc_qzone_id;
    u8_t igu_sb_id = 0;
    u8_t igu_seg_id = 0;
    u8_t timeout = 0;
    u8_t dhc_enable = FALSE;
    u8_t sm_idx;
    u8_t hc_sb_max_indices;

    DbgBreakIf(!pdev);

    /* CQ#46240: Disable the function in the status-block data before nullifying sync-line + status-block */
    lm_ndsb_func_disable(pdev, fw_sb_id);

    /* nullify the status block */
    DbgBreakIf((CSTORM_STATUS_BLOCK_SIZE % 4) != 0);
    DbgBreakIf((CSTORM_STATUS_BLOCK_DATA_SIZE % 4) != 0);
    DbgBreakIf((CSTORM_SYNC_BLOCK_SIZE % 4) != 0);
    if (IS_PFDEV(pdev)) {
        REG_WR_DMAE_LEN_ZERO(PFDEV(pdev), CSEM_REG_FAST_MEMORY + CSTORM_SYNC_BLOCK_OFFSET(fw_sb_id), CSTORM_SYNC_BLOCK_SIZE / 4);
        REG_WR_DMAE_LEN_ZERO(PFDEV(pdev), CSEM_REG_FAST_MEMORY + CSTORM_STATUS_BLOCK_OFFSET(fw_sb_id), CSTORM_STATUS_BLOCK_SIZE / 4);
        REG_WR_DMAE_LEN_ZERO(PFDEV(pdev), CSEM_REG_FAST_MEMORY + CSTORM_STATUS_BLOCK_DATA_OFFSET(fw_sb_id), CSTORM_STATUS_BLOCK_DATA_SIZE / 4);
    } else {
        for (index = 0; index < CSTORM_SYNC_BLOCK_SIZE / sizeof(u32_t); index++) {
            LM_INTMEM_WRITE32(PFDEV(pdev), CSTORM_SYNC_BLOCK_OFFSET(fw_sb_id) + 4*index, 0, BAR_CSTRORM_INTMEM);
        }
        for (index = 0; index < CSTORM_STATUS_BLOCK_SIZE / sizeof(u32_t); index++) {
            LM_INTMEM_WRITE32(PFDEV(pdev), CSTORM_STATUS_BLOCK_OFFSET(fw_sb_id) + 4*index, 0, BAR_CSTRORM_INTMEM);
        }
        for (index = 0; index < CSTORM_STATUS_BLOCK_DATA_SIZE / sizeof(u32_t); index++) {
            LM_INTMEM_WRITE32(PFDEV(pdev), CSTORM_STATUS_BLOCK_DATA_OFFSET(fw_sb_id) + 4*index, 0, BAR_CSTRORM_INTMEM);
        }
    }

    /* Initialize cstorm_status_block_data structure */
    if (CHIP_IS_E1x(pdev)) {

        pdev->vars.status_blocks_arr[sb_id].hc_status_block_data.e1x_sb_data.common.p_func.pf_id = FUNC_ID(pdev);
        pdev->vars.status_blocks_arr[sb_id].hc_status_block_data.e1x_sb_data.common.p_func.vf_id = 0xff;
        pdev->vars.status_blocks_arr[sb_id].hc_status_block_data.e1x_sb_data.common.p_func.vf_valid = FALSE;
        pdev->vars.status_blocks_arr[sb_id].hc_status_block_data.e1x_sb_data.common.p_func.vnic_id = VNIC_ID(pdev);

        if (pdev->params.ndsb_type == LM_DOUBLE_SM_SINGLE_IGU) {
            pdev->vars.status_blocks_arr[sb_id].hc_status_block_data.e1x_sb_data.common.same_igu_sb_1b = TRUE;
        } else {
            pdev->vars.status_blocks_arr[sb_id].hc_status_block_data.e1x_sb_data.common.same_igu_sb_1b = FALSE;
        }

    } else {

        pdev->vars.status_blocks_arr[sb_id].hc_status_block_data.e2_sb_data.common.p_func.pf_id = FUNC_ID(pdev);
        if (IS_PFDEV(pdev)) {
            pdev->vars.status_blocks_arr[sb_id].hc_status_block_data.e2_sb_data.common.p_func.vf_id = 0xff;
            pdev->vars.status_blocks_arr[sb_id].hc_status_block_data.e2_sb_data.common.p_func.vf_valid = FALSE;
        } else {
            pdev->vars.status_blocks_arr[sb_id].hc_status_block_data.e2_sb_data.common.p_func.vf_id = ABS_VFID(pdev);
            pdev->vars.status_blocks_arr[sb_id].hc_status_block_data.e2_sb_data.common.p_func.vf_valid = TRUE;
        }
        pdev->vars.status_blocks_arr[sb_id].hc_status_block_data.e2_sb_data.common.p_func.vnic_id = VNIC_ID(pdev);
        if (pdev->params.ndsb_type == LM_DOUBLE_SM_SINGLE_IGU) {
            pdev->vars.status_blocks_arr[sb_id].hc_status_block_data.e2_sb_data.common.same_igu_sb_1b = TRUE;
        } else {
            pdev->vars.status_blocks_arr[sb_id].hc_status_block_data.e2_sb_data.common.same_igu_sb_1b = FALSE;
        }
        pdev->vars.status_blocks_arr[sb_id].hc_status_block_data.e2_sb_data.common.dhc_qzone_id = dhc_qzone_id;

    }

    if ((INTR_BLK_TYPE(pdev) == INTR_BLK_IGU) && (INTR_BLK_MODE(pdev) == INTR_BLK_MODE_NORM) ) {
        igu_sb_id = IGU_BASE_NDSB(pdev) + /*IGU_U_NDSB_OFFSET(pdev)*/ + sb_id;
        igu_seg_id = IGU_SEG_ACCESS_NORM;
    } else {
        igu_sb_id = sb_id;
        igu_seg_id = HC_SEG_ACCESS_NORM;
    }

    lm_setup_ndsb_state_machine(pdev, sb_id, SM_RX_ID, igu_sb_id + IGU_U_NDSB_OFFSET(pdev), igu_seg_id);
    if (pdev->params.ndsb_type != LM_SINGLE_SM) {
        lm_setup_ndsb_state_machine(pdev, sb_id, SM_TX_ID, igu_sb_id,igu_seg_id);
    }

    //init host coalescing params - supported dymanicHC indices
    if (CHIP_IS_E1x(pdev)) {
        hc_sb_max_indices = HC_SB_MAX_INDICES_E1X;
    } else {
        hc_sb_max_indices = HC_SB_MAX_INDICES_E2;
    }
    for (index = 0; index < hc_sb_max_indices; index++) {
        if (index < HC_DHC_SB_NUM_INDICES) {
            dhc_enable = (pdev->params.enable_dynamic_hc[index] != 0);
            REG_WR(PFDEV(pdev), CSEM_REG_FAST_MEMORY + CSTORM_BYTE_COUNTER_OFFSET(byte_counter_id, index), 0);
        } else {
            dhc_enable = FALSE;
        }
        switch (index) {
        case HC_INDEX_TOE_RX_CQ_CONS:
        case HC_INDEX_ETH_RX_CQ_CONS:
        case HC_INDEX_FCOE_EQ_CONS:
            sm_idx = SM_RX_ID;
            if (dhc_enable && ic->hc_usec_u_sb[index]) {
                timeout = (u8_t)pdev->params.hc_timeout0[SM_RX_ID][index];
            } else {
                timeout = (u8_t)(ic->hc_usec_u_sb[index] / HC_TIMEOUT_RESOLUTION_IN_US);
            }
            break;
        case HC_INDEX_TOE_TX_CQ_CONS:
            if (pdev->params.ndsb_type != LM_SINGLE_SM) {
                sm_idx = SM_TX_ID;
            } else {
                sm_idx = SM_RX_ID;
            }
            if (dhc_enable && ic->hc_usec_c_sb[0]) {
                if (pdev->params.ndsb_type != LM_SINGLE_SM) {
                    timeout = (u8_t)pdev->params.hc_timeout0[SM_TX_ID][index];
                } else {
                    timeout = (u8_t)pdev->params.hc_timeout0[SM_RX_ID][index];
                }
            } else {
                timeout = (u8_t)(ic->hc_usec_c_sb[0] / HC_TIMEOUT_RESOLUTION_IN_US);
            }
            break;
        case HC_INDEX_ISCSI_OOO_CONS:
            sm_idx = SM_RX_ID;
            timeout = (u8_t)(ic->hc_usec_u_sb[2] / HC_TIMEOUT_RESOLUTION_IN_US);
            break;
        case HC_INDEX_ETH_TX_CQ_CONS:
        case HC_INDEX_ISCSI_EQ_CONS:
            if (pdev->params.ndsb_type != LM_SINGLE_SM) {
                sm_idx = SM_TX_ID;
            } else {
                sm_idx = SM_RX_ID;
            }
            timeout = (u8_t)(ic->hc_usec_c_sb[index - HC_DHC_SB_NUM_INDICES] / HC_TIMEOUT_RESOLUTION_IN_US);
            break;
        default:
            if (pdev->params.ndsb_type != LM_SINGLE_SM) {
                sm_idx = SM_TX_ID;
            } else {
                sm_idx = SM_RX_ID;
            }
            timeout = (u8_t)(ic->hc_usec_c_sb[3] / HC_TIMEOUT_RESOLUTION_IN_US);
            dhc_enable = FALSE;
            break;
        }
        lm_setup_ndsb_index(pdev, sb_id, index, sm_idx, timeout, dhc_enable);
    }
    if (CHIP_IS_E1x(pdev)) {
        for (index = 0; index < sizeof(struct hc_status_block_data_e1x)/sizeof(u32_t); index++) {
            LM_INTMEM_WRITE32(PFDEV(pdev), CSTORM_STATUS_BLOCK_DATA_OFFSET(fw_sb_id) + sizeof(u32_t)*index,
                              *((u32_t*)(&pdev->vars.status_blocks_arr[sb_id].hc_status_block_data.e1x_sb_data) + index), BAR_CSTRORM_INTMEM);
        }
    } else {
        for (index = 0; index < sizeof(struct hc_status_block_data_e2)/sizeof(u32_t); index++) {
            LM_INTMEM_WRITE32(PFDEV(pdev), CSTORM_STATUS_BLOCK_DATA_OFFSET(fw_sb_id) + sizeof(u32_t)*index,
                              *((u32_t*)(&pdev->vars.status_blocks_arr[sb_id].hc_status_block_data.e2_sb_data) + index), BAR_CSTRORM_INTMEM);
        }
    }
}

void lm_int_hc_ack_sb(lm_device_t *pdev, u8_t rss_id, u8_t storm_id, u16_t sb_index, u8_t int_op, u8_t is_update_idx)
{
    struct igu_ack_register hc_data;

    //this is the result which should be communicated to the driver!
    u32_t result = 0;



    //don't forget this
    hc_data.sb_id_and_flags    = 0;
    hc_data.status_block_index = 0;

    DbgMessage4(pdev, INFORMi, "lm_int_ack_sb() inside! rss_id:%d, sb_index:%d, func_num:%d is_update:%d\n", rss_id, sb_index, FUNC_ID(pdev), is_update_idx);

    hc_data.sb_id_and_flags   |= (0xffffffff & (int_op << IGU_ACK_REGISTER_INTERRUPT_MODE_SHIFT));
    hc_data.sb_id_and_flags   |= (0xffffffff & (rss_id << IGU_ACK_REGISTER_STATUS_BLOCK_ID_SHIFT));
    hc_data.sb_id_and_flags   |= (0xffffffff & (storm_id << IGU_ACK_REGISTER_STORM_ID_SHIFT));
    hc_data.sb_id_and_flags   |= (0xffffffff & (is_update_idx << IGU_ACK_REGISTER_UPDATE_INDEX_SHIFT));
    hc_data.status_block_index = sb_index;

    DbgMessage2(pdev, INFORMi, "lm_int_ack_sb() inside! data:0x%x; status_block_index:%d\n", hc_data.sb_id_and_flags, hc_data.status_block_index);

    result = ((u32_t)hc_data.sb_id_and_flags) << 16 | hc_data.status_block_index;

    DbgMessage1(pdev, INFORMi, "lm_int_ack_sb() result:0x%x\n", result);

    // interrupt ack
    REG_WR(pdev,  HC_REG_COMMAND_REG + PORT_ID(pdev)*32 + COMMAND_REG_INT_ACK, result);

}



void lm_int_igu_ack_sb(lm_device_t *pdev, u8_t igu_sb_id, u8_t segment_access, u16_t sb_index, u8_t int_op, u8_t is_update_idx)
{
    struct igu_regular cmd_data;
    struct igu_ctrl_reg cmd_ctrl;
    u32_t cmd_addr;

    //DbgMessage1(pdev, FATAL, "int-igu-ack segment_access=%d\n", segment_access);
    DbgBreakIf(sb_index & ~IGU_REGULAR_SB_INDEX);

    cmd_data.sb_id_and_flags =
        ((sb_index << IGU_REGULAR_SB_INDEX_SHIFT) |
         (segment_access << IGU_REGULAR_SEGMENT_ACCESS_SHIFT) |
         (is_update_idx << IGU_REGULAR_BUPDATE_SHIFT) |
         (int_op << IGU_REGULAR_ENABLE_INT_SHIFT));

    cmd_addr = IGU_CMD_INT_ACK_BASE + igu_sb_id;

    if (INTR_BLK_ACCESS(pdev) == INTR_BLK_ACCESS_IGUMEM) {
        if (IS_PFDEV(pdev)) {
            REG_WR(pdev, BAR_IGU_INTMEM + cmd_addr*8, cmd_data.sb_id_and_flags);
        } else {
            VF_REG_WR(pdev, VF_BAR0_IGU_OFFSET + cmd_addr*8, cmd_data.sb_id_and_flags);
        }
    } else {
        u8_t igu_func_id = 0;

        /* GRC ACCESS: */
        DbgBreakIf(IS_VFDEV(pdev));
        /* Write the Data, then the control */
        /* [18:12] - FID (if VF - [18] = 0; [17:12] = VF number; if PF - [18] = 1; [17:14] = 0; [13:12] = PF number) */
        igu_func_id = IGU_FUNC_ID(pdev);
        cmd_ctrl.ctrl_data =
            ((cmd_addr << IGU_CTRL_REG_ADDRESS_SHIFT) |
             (igu_func_id << IGU_CTRL_REG_FID_SHIFT) |
             (IGU_CTRL_CMD_TYPE_WR << IGU_CTRL_REG_TYPE_SHIFT));

        REG_WR(pdev, IGU_REG_COMMAND_REG_32LSB_DATA, cmd_data.sb_id_and_flags);
        REG_WR(pdev, IGU_REG_COMMAND_REG_CTRL, cmd_ctrl.ctrl_data);
    }
}

static void lm_int_igu_sb_cleanup(lm_device_t *pdev, u8 igu_sb_id)
{
    struct igu_regular  cmd_data = {0};
    struct igu_ctrl_reg cmd_ctrl = {0};
    u32_t igu_addr_ack           = IGU_REG_CSTORM_TYPE_0_SB_CLEANUP + (igu_sb_id/32)*4;
    u32_t sb_bit                 =  1 << (igu_sb_id%32);
    u32_t cnt                    = 100;

#ifdef _VBD_CMD_
    return;
#endif

    /* Not supported in backward compatible mode! */
    if (INTR_BLK_MODE(pdev) == INTR_BLK_MODE_BC)
    {
        return;
    }

    /* Cleanup can be done only via GRC access using the producer update command */
    cmd_data.sb_id_and_flags =
        ((IGU_USE_REGISTER_cstorm_type_0_sb_cleanup << IGU_REGULAR_CLEANUP_TYPE_SHIFT) |
          IGU_REGULAR_CLEANUP_SET |
          IGU_REGULAR_BCLEANUP);

    cmd_ctrl.ctrl_data =
        (((IGU_CMD_E2_PROD_UPD_BASE + igu_sb_id) << IGU_CTRL_REG_ADDRESS_SHIFT) |
         (IGU_FUNC_ID(pdev) << IGU_CTRL_REG_FID_SHIFT) |
         (IGU_CTRL_CMD_TYPE_WR << IGU_CTRL_REG_TYPE_SHIFT));

    REG_WR(pdev, IGU_REG_COMMAND_REG_32LSB_DATA, cmd_data.sb_id_and_flags);
    REG_WR(pdev, IGU_REG_COMMAND_REG_CTRL, cmd_ctrl.ctrl_data);

    /* wait for clean up to finish */
    while (!(REG_RD(pdev, igu_addr_ack) & sb_bit) && --cnt)
    {
        mm_wait(pdev, 10);
    }

    if (!(REG_RD(pdev, igu_addr_ack) & sb_bit))
    {
        DbgMessage4(pdev, FATAL, "Unable to finish IGU cleanup - set: igu_sb_id %d offset %d bit %d (cnt %d)\n",
                    igu_sb_id, igu_sb_id/32, igu_sb_id%32, cnt);
    }

    /* Now we clear the cleanup-bit... same command without cleanup_set... */
    cmd_data.sb_id_and_flags =
        ((IGU_USE_REGISTER_cstorm_type_0_sb_cleanup << IGU_REGULAR_CLEANUP_TYPE_SHIFT) |
          IGU_REGULAR_BCLEANUP);


    REG_WR(pdev, IGU_REG_COMMAND_REG_32LSB_DATA, cmd_data.sb_id_and_flags);
    REG_WR(pdev, IGU_REG_COMMAND_REG_CTRL, cmd_ctrl.ctrl_data);

    /* wait for clean up to finish */
    while ((REG_RD(pdev, igu_addr_ack) & sb_bit) && --cnt)
    {
        mm_wait(pdev, 10);
    }

    if ((REG_RD(pdev, igu_addr_ack) & sb_bit))
    {
        DbgMessage4(pdev, FATAL, "Unable to finish IGU cleanup - clear: igu_sb_id %d offset %d bit %d (cnt %d)\n",
                    igu_sb_id, igu_sb_id/32, igu_sb_id%32, cnt);
    }
}


void lm_int_ack_def_sb_disable(lm_device_t *pdev)
{
    pdev->debug_info.ack_def_dis++;
    if (INTR_BLK_TYPE(pdev) == INTR_BLK_HC) {
        lm_int_hc_ack_sb(pdev, DEF_STATUS_BLOCK_IGU_INDEX, HC_SEG_ACCESS_DEF, DEF_SB_INDEX(pdev), IGU_INT_DISABLE, 0); //DEF_STATUS_BLOCK_INDEX
    } else {
        if (INTR_BLK_MODE(pdev) == INTR_BLK_MODE_BC) {
            lm_int_igu_ack_sb(pdev, IGU_DSB_ID(pdev), HC_SEG_ACCESS_DEF, DEF_SB_INDEX(pdev), IGU_INT_DISABLE, 0);
        } else {
            lm_int_igu_ack_sb(pdev, IGU_DSB_ID(pdev), IGU_SEG_ACCESS_DEF, DEF_SB_INDEX(pdev), IGU_INT_DISABLE, 1);
        }
    }
}

/* Assumptions: Called when acking a status-block and enabling interrupts */
void lm_int_ack_def_sb_enable(lm_device_t *pdev)
{
    pdev->debug_info.ack_def_en++;
    if (INTR_BLK_TYPE(pdev) == INTR_BLK_HC) {
        lm_int_hc_ack_sb(pdev, DEF_STATUS_BLOCK_IGU_INDEX, HC_SEG_ACCESS_ATTN, DEF_SB_INDEX_OF_ATTN(pdev), IGU_INT_NOP, 1); //DEF_STATUS_BLOCK_INDEX
        lm_int_hc_ack_sb(pdev, DEF_STATUS_BLOCK_IGU_INDEX, HC_SEG_ACCESS_DEF, DEF_SB_INDEX(pdev), IGU_INT_ENABLE, 1); //DEF_STATUS_BLOCK_INDEX
    } else {
        if (INTR_BLK_MODE(pdev) == INTR_BLK_MODE_BC) {
            lm_int_igu_ack_sb(pdev, IGU_DSB_ID(pdev), HC_SEG_ACCESS_ATTN, DEF_SB_INDEX_OF_ATTN(pdev), IGU_INT_NOP, 1);
            lm_int_igu_ack_sb(pdev, IGU_DSB_ID(pdev), HC_SEG_ACCESS_DEF, DEF_SB_INDEX(pdev), IGU_INT_ENABLE, 1);
        } else {
            lm_int_igu_ack_sb(pdev, IGU_DSB_ID(pdev), IGU_SEG_ACCESS_ATTN, DEF_SB_INDEX_OF_ATTN(pdev), IGU_INT_NOP, 1);
            lm_int_igu_ack_sb(pdev, IGU_DSB_ID(pdev), IGU_SEG_ACCESS_DEF, DEF_SB_INDEX(pdev), IGU_INT_ENABLE, 1);
        }
    }
}

void lm_int_ack_sb_disable(lm_device_t *pdev, u8_t rss_id)
{
    if (INTR_BLK_TYPE(pdev) == INTR_BLK_HC) {
        lm_int_hc_ack_sb(pdev, rss_id , HC_SEG_ACCESS_NORM, 0, IGU_INT_DISABLE, 0);
        pdev->debug_info.ack_dis[rss_id]++;
    } else {
        if (INTR_BLK_MODE(pdev) == INTR_BLK_MODE_BC) {
            lm_int_igu_ack_sb(pdev, rss_id  + IGU_BASE_NDSB(pdev) , HC_SEG_ACCESS_NORM, 0, IGU_INT_DISABLE, 0);
            pdev->debug_info.ack_dis[rss_id]++;
        } else {
            if (pdev->debug_info.ack_dis[rss_id] == pdev->debug_info.ack_en[rss_id]) {
                //DbgMessage(pdev, WARN, "********lm_int_ack_sb_disable() during DPC\n");
//                REG_WR(PFDEV(pdev), IGU_REG_ECO_RESERVED, 8);
//                DbgBreak();
            }
            lm_int_igu_ack_sb(pdev, rss_id  + IGU_BASE_NDSB(pdev), IGU_SEG_ACCESS_NORM, 0, IGU_INT_DISABLE, 0);
            pdev->debug_info.ack_dis[rss_id]++;
        }
    }
}

void lm_int_ack_sb_enable(lm_device_t *pdev, u8_t rss_id)
{
    if (INTR_BLK_TYPE(pdev) == INTR_BLK_HC) {
        lm_int_hc_ack_sb(pdev, rss_id , HC_SEG_ACCESS_NORM, SB_RX_INDEX(pdev,rss_id), IGU_INT_ENABLE, 1);
        pdev->debug_info.ack_en[rss_id]++;
    } else {
        if (INTR_BLK_MODE(pdev) == INTR_BLK_MODE_BC) {
            lm_int_igu_ack_sb(pdev, rss_id  + IGU_BASE_NDSB(pdev) , HC_SEG_ACCESS_NORM, SB_RX_INDEX(pdev,rss_id), IGU_INT_ENABLE, 1);
            pdev->debug_info.ack_en[rss_id]++;
        } else {
            if (rss_id >= IGU_U_NDSB_OFFSET(pdev)) {
                lm_int_igu_ack_sb(pdev, rss_id  + IGU_BASE_NDSB(pdev), IGU_SEG_ACCESS_NORM, SB_RX_INDEX(pdev,rss_id), IGU_INT_ENABLE, 1);
                pdev->debug_info.ack_en[rss_id]++;
            } else {
                lm_int_igu_ack_sb(pdev, rss_id  + IGU_BASE_NDSB(pdev), IGU_SEG_ACCESS_NORM, SB_TX_INDEX(pdev,rss_id), IGU_INT_ENABLE, 1);
            }
         }
    }
}

void lm_enable_hc_int(struct _lm_device_t *pdev)
{
    u32_t val;
    u32_t reg_name;

    DbgBreakIf(!pdev);

    reg_name = PORT_ID(pdev) ? HC_REG_CONFIG_1 : HC_REG_CONFIG_0;

    DbgMessage(pdev, INFORMnv, "### lm_enable_int\n");

    val = 0x1000;

    SET_FLAGS(val, (PORT_ID(pdev)?  HC_CONFIG_1_REG_ATTN_BIT_EN_1 : HC_CONFIG_0_REG_ATTN_BIT_EN_0));

    switch (pdev->params.interrupt_mode) {
    case LM_INT_MODE_INTA:
        SET_FLAGS(val, (HC_CONFIG_0_REG_INT_LINE_EN_0 |
                        HC_CONFIG_0_REG_SINGLE_ISR_EN_0));

        /* we trust that if we're in inta... the os will take care of the configuration space...and therefore
         * that will determine whether we are in inta or msix and not this configuration, we can't take down msix
         * due to a hw bug */
        if (CHIP_IS_E1(pdev))
        {
            SET_FLAGS(val, HC_CONFIG_0_REG_MSI_MSIX_INT_EN_0);
        }
        break;
    case LM_INT_MODE_SIMD:
        SET_FLAGS(val, (HC_CONFIG_0_REG_SINGLE_ISR_EN_0 | HC_CONFIG_0_REG_MSI_MSIX_INT_EN_0) );
        RESET_FLAGS(val, HC_CONFIG_0_REG_INT_LINE_EN_0);
        break;
    case LM_INT_MODE_MIMD:
        SET_FLAGS(val, HC_CONFIG_0_REG_MSI_MSIX_INT_EN_0);
        RESET_FLAGS(val, (HC_CONFIG_0_REG_INT_LINE_EN_0 |
                          HC_CONFIG_0_REG_SINGLE_ISR_EN_0));
        break;
    default:
        DbgBreakMsg("Wrong Interrupt Mode\n");
        return;
    }

    if (CHIP_IS_E1(pdev))
    {
        REG_WR(pdev, HC_REG_INT_MASK + PORT_ID(pdev)*4, 0x1FFFF);
    }

    REG_WR(pdev,  reg_name, val);

    if(!CHIP_IS_E1(pdev))
    {
        /* init leading/trailing edge */
        if(IS_MULTI_VNIC(pdev))
        {
            /* in mf mode:
             *  - Set only VNIC bit out of the "per vnic group attentions" (bits[4-7]) */
            val = (0xee0f | (1 << (VNIC_ID(pdev) + 4)));
            /* Connect to PMF to NIG attention bit 8 */
            if (IS_PMF(pdev)) {
                val |= 0x1100;
            }
        } else
        {
            val = 0xffff;
        }
        REG_WR(pdev,  (PORT_ID(pdev) ? HC_REG_TRAILING_EDGE_1 : HC_REG_TRAILING_EDGE_0), val);
        REG_WR(pdev,  (PORT_ID(pdev) ? HC_REG_LEADING_EDGE_1 : HC_REG_LEADING_EDGE_0), val);
    }

    pdev->vars.enable_intr = 1;
}

lm_status_t lm_enable_igu_int(struct _lm_device_t *pdev)
{
    u32_t val = 0;

    if(ERR_IF(!pdev)) {
        return LM_STATUS_INVALID_PARAMETER;
    }

#ifdef VF_INVOLVED
    if (IS_VFDEV(pdev)) {
        lm_status_t lm_status;
        lm_status =  lm_vf_enable_igu_int(pdev);
        if (lm_status != LM_STATUS_SUCCESS) {
            DbgMessage(pdev, FATAL, "VF can't enable igu interrupt\n");
            return lm_status;
        }
        pdev->vars.enable_intr = 1;
        return lm_status;

    }
#endif

    DbgMessage(pdev, INFORMnv, "### lm_enable_int\n");

    val=REG_RD(pdev, IGU_REG_PF_CONFIGURATION);

    SET_FLAGS(val, IGU_PF_CONF_FUNC_EN);
    SET_FLAGS(val, IGU_PF_CONF_ATTN_BIT_EN);

    switch (pdev->params.interrupt_mode) {
    case LM_INT_MODE_INTA:
        SET_FLAGS(val, (IGU_PF_CONF_INT_LINE_EN | IGU_PF_CONF_SINGLE_ISR_EN));
        RESET_FLAGS(val, IGU_PF_CONF_MSI_MSIX_EN);
        break;
    case LM_INT_MODE_SIMD:
        SET_FLAGS(val, (IGU_PF_CONF_SINGLE_ISR_EN | IGU_PF_CONF_MSI_MSIX_EN) );
        RESET_FLAGS(val, IGU_PF_CONF_INT_LINE_EN);
        break;
    case LM_INT_MODE_MIMD:
        SET_FLAGS(val, IGU_PF_CONF_MSI_MSIX_EN);
        RESET_FLAGS(val, (IGU_PF_CONF_INT_LINE_EN | IGU_PF_CONF_SINGLE_ISR_EN));
        break;
    default:
        DbgBreakMsg("Wrong Interrupt Mode\n");
        return LM_STATUS_FAILURE;
    }

    REG_WR(pdev,  IGU_REG_PF_CONFIGURATION, val);

    if(!CHIP_IS_E1(pdev))
    {
        /* init leading/trailing edge */
        if(IS_MULTI_VNIC(pdev))
        {
            /* in mf mode:
             *  - Do not set the link attention (bit 11) (will be set by MCP for the PMF)
             *  - Set only VNIC bit out of the "per vnic group attentions" (bits[4-7]) */
            val = (0xee0f | (1 << (VNIC_ID(pdev) + 4)));
            /* Connect to PMF to NIG attention bit 8 */
            if (IS_PMF(pdev)) {
                val |= 0x1100;
            }
        } else
        {
            val = 0xffff;
        }
        REG_WR(pdev,  IGU_REG_TRAILING_EDGE_LATCH, val);
        REG_WR(pdev,  IGU_REG_LEADING_EDGE_LATCH, val);
    }

    pdev->vars.enable_intr = 1;

    return LM_STATUS_SUCCESS;
}

void lm_enable_int(struct _lm_device_t *pdev)
{
    if (INTR_BLK_TYPE(pdev) == INTR_BLK_HC) {
        lm_enable_hc_int(pdev);
    } else {
        lm_enable_igu_int(pdev);
    }
}


void lm_disable_hc_int(struct _lm_device_t *pdev)
{
    u32_t val;
    u32_t reg_name;

    DbgBreakIf(!pdev);

    reg_name = PORT_ID(pdev) ? HC_REG_CONFIG_1 : HC_REG_CONFIG_0;

    DbgMessage(pdev, INFORMnv, "### lm_disable_int\n");

    val=REG_RD(pdev, reg_name);

    /* disable both bits, for INTA, MSI and MSI-X. */
    RESET_FLAGS(val, (HC_CONFIG_0_REG_MSI_MSIX_INT_EN_0 |
                      HC_CONFIG_0_REG_INT_LINE_EN_0 |
                      HC_CONFIG_0_REG_ATTN_BIT_EN_0 |
                      HC_CONFIG_0_REG_SINGLE_ISR_EN_0));

    if (CHIP_IS_E1(pdev))
    {
        REG_WR(pdev, HC_REG_INT_MASK + PORT_ID(pdev)*4, 0);

        /* E1 Errate: can't ever take msix bit down */
        SET_FLAGS(val,HC_CONFIG_0_REG_MSI_MSIX_INT_EN_0);
    }

    REG_WR(pdev,  reg_name, val);

    pdev->vars.enable_intr = 0;
}

void lm_disable_igu_int(struct _lm_device_t *pdev)
{
    u32_t val;

    DbgBreakIf(!pdev);

    DbgMessage(pdev, INFORMnv, "### lm_disable_int\n");

#ifdef VF_INVOLVED
    if (IS_VFDEV(pdev)) {
        lm_vf_disable_igu_int(pdev);
        pdev->vars.enable_intr = 0;
        return;
    }
#endif

    val = REG_RD(pdev, IGU_REG_PF_CONFIGURATION);

    /* disable both bits, for INTA, MSI and MSI-X. */
    RESET_FLAGS(val, (IGU_PF_CONF_MSI_MSIX_EN |
                      IGU_PF_CONF_INT_LINE_EN |
                      IGU_PF_CONF_ATTN_BIT_EN |
                      IGU_PF_CONF_SINGLE_ISR_EN |
                      IGU_PF_CONF_FUNC_EN));

    REG_WR(pdev,  IGU_REG_PF_CONFIGURATION, val);

    pdev->vars.enable_intr = 0;
}

void lm_disable_int(struct _lm_device_t *pdev)
{
    if (INTR_BLK_TYPE(pdev) == INTR_BLK_HC) {
        lm_disable_hc_int(pdev);
    } else {
        lm_disable_igu_int(pdev);
    }
}

volatile struct host_hc_status_block_e2 * lm_get_e2_status_block(lm_device_t *pdev, u8_t rss_id)
{
    return pdev->vars.status_blocks_arr[rss_id].host_hc_status_block.e2_sb;
}

volatile struct host_hc_status_block_e1x * lm_get_e1x_status_block(lm_device_t *pdev, u8_t rss_id)
{
    return pdev->vars.status_blocks_arr[rss_id].host_hc_status_block.e1x_sb;
}

volatile struct hc_sp_status_block * lm_get_default_status_block(lm_device_t *pdev)
{
    return &pdev->vars.gen_sp_status_block.hc_sp_status_blk->sp_sb;
}

volatile struct atten_sp_status_block * lm_get_attention_status_block(lm_device_t *pdev)
{
    return &pdev->vars.gen_sp_status_block.hc_sp_status_blk->atten_status_block;
}


void print_sb_info(lm_device_t *pdev)
{
#if 0
    u8_t index                                    = 0;
    volatile struct host_status_block *rss_sb     = NULL;

    DbgBreakIf(!pdev);
    DbgMessage(pdev, INFORMi, "print_sb_info() inside!\n");
    //print info of all non-default status blocks
    for(index=0; index < MAX_RSS_CHAINS; index++)
    {
        rss_sb = lm_get_status_block(pdev, index);

        DbgBreakIf(!rss_sb);
        DbgBreakIf(*(LM_RCQ(pdev, index).
             hw_con_idx_ptr) != rss_sb->u_status_block.index_values[HC_INDEX_U_ETH_RX_CQ_CONS]);
        DbgBreakIf(*(LM_TXQ(pdev, index).hw_con_idx_ptr) != rss_sb->c_status_block.index_values[HC_INDEX_C_ETH_TX_CQ_CONS]);

        DbgMessage7(pdev, INFORMi, "rss sb #%d: u_new_cons:%d, c_new_cons:%d, c_status idx:%d, c_sbID:%d, u_status idx:%d, u_sbID:%d\n",
            index,
            rss_sb->u_status_block.index_values[HC_INDEX_U_ETH_RX_CQ_CONS],
            rss_sb->c_status_block.index_values[HC_INDEX_C_ETH_TX_CQ_CONS],
            rss_sb->c_status_block.status_block_index,
            rss_sb->c_status_block.status_block_id,
            rss_sb->u_status_block.status_block_index,
            rss_sb->u_status_block.status_block_id);

        DbgMessage(pdev, INFORMi, "____________________________________________________________\n");
    }
    //print info of the default status block
    DbgBreakIf(pdev->vars.gen_sp_status_block.hc_sp_status_blk == NULL);

    DbgMessage2(pdev, INFORMi, "sp sb: c_status idx:%d, c_sbID:%d\n",
        pdev->vars.gen_sp_status_block.hc_sp_status_blk->sp_sb.running_index, pdev->vars.gen_sp_status_block.sb_data.igu_sb_id);

    DbgMessage(pdev, INFORMi, "____________________________________________________________\n");
#endif
}

/**
 * This function sets all the status-block ack values back to
 * zero. Must be called BEFORE initializing the igu + before
 * initializing status-blocks.
 *
 * @param pdev
 */
static void lm_reset_sb_ack_values(struct _lm_device_t *pdev)
{
    //re-initialize all the local copy indices of sbs for load/unload scenarios
    pdev->vars.hc_def_ack = 0;

    //init attn state
    pdev->vars.attn_state = 0;

    pdev->vars.attn_def_ack = 0;

    mm_memset(pdev->vars.c_hc_ack, 0, sizeof(pdev->vars.c_hc_ack));
    mm_memset(pdev->vars.u_hc_ack, 0, sizeof(pdev->vars.u_hc_ack));
}

/* Initalize the whole status blocks per port - overall: 1 defalt sb, 16 non-default sbs
 *
 * Parameters:
 * pdev - the LM device which holds the sbs
 * port - the port number
 */
static void init_status_blocks(struct _lm_device_t *pdev)
{
    u8_t                                    sb_id        = 0;
    u8_t                                    port         = PORT_ID(pdev);
    u8_t                                    group_idx;
    DbgMessage1(pdev, INFORMi, "init_status_blocks() inside! func:%d\n",FUNC_ID(pdev));
    DbgBreakIf(!pdev);

    pdev->vars.num_attn_sig_regs =
        (CHIP_IS_E1x(pdev))? NUM_ATTN_REGS_E1X : NUM_ATTN_REGS_E2;

    //Read routing configuration for attn signal output of groups. Currently, only group 0,1,2 are wired.
    for (group_idx = 0; group_idx < MAX_DYNAMIC_ATTN_GRPS; group_idx++)
    {

        //group index
        pdev->vars.attn_groups_output[group_idx].attn_sig_dword[0] =
            REG_RD(pdev, (PORT_ID(pdev) ? MISC_REG_AEU_ENABLE1_FUNC_1_OUT_0 : MISC_REG_AEU_ENABLE1_FUNC_0_OUT_0) + group_idx*16);
        pdev->vars.attn_groups_output[group_idx].attn_sig_dword[1] =
            REG_RD(pdev, (PORT_ID(pdev) ? MISC_REG_AEU_ENABLE2_FUNC_1_OUT_0 : MISC_REG_AEU_ENABLE2_FUNC_0_OUT_0) + group_idx*16);
        pdev->vars.attn_groups_output[group_idx].attn_sig_dword[2] =
            REG_RD(pdev, (PORT_ID(pdev) ? MISC_REG_AEU_ENABLE3_FUNC_1_OUT_0 : MISC_REG_AEU_ENABLE3_FUNC_0_OUT_0) + group_idx*16);
        pdev->vars.attn_groups_output[group_idx].attn_sig_dword[3] =
            REG_RD(pdev, (PORT_ID(pdev) ? MISC_REG_AEU_ENABLE4_FUNC_1_OUT_0 : MISC_REG_AEU_ENABLE4_FUNC_0_OUT_0) + group_idx*16);
        if (pdev->vars.num_attn_sig_regs == 5) {
            /* enable5 is separate from the rest of the registers, and therefore the address skip is 4 and not 16 between the different groups */
            pdev->vars.attn_groups_output[group_idx].attn_sig_dword[4] =
                REG_RD(pdev, (PORT_ID(pdev) ? MISC_REG_AEU_ENABLE5_FUNC_1_OUT_0 : MISC_REG_AEU_ENABLE5_FUNC_0_OUT_0) + group_idx*4);
        } else {
            pdev->vars.attn_groups_output[group_idx].attn_sig_dword[4] = 0;
        }

        DbgMessage6(pdev, INFORMi, "lm_handle_deassertion_processing: group %d mask1:0x%x, mask2:0x%x, mask3:0x%x, mask4:0x%x, mask5:0x%x\n",
                       group_idx,
                       pdev->vars.attn_groups_output[group_idx].attn_sig_dword[0],
                       pdev->vars.attn_groups_output[group_idx].attn_sig_dword[1],
                       pdev->vars.attn_groups_output[group_idx].attn_sig_dword[2],
                       pdev->vars.attn_groups_output[group_idx].attn_sig_dword[3],
                       pdev->vars.attn_groups_output[group_idx].attn_sig_dword[4]);

    }
    pdev->vars.attn_sig_af_inv_reg_addr[0] =
        PORT_ID(pdev) ? MISC_REG_AEU_AFTER_INVERT_1_FUNC_1 : MISC_REG_AEU_AFTER_INVERT_1_FUNC_0;
    pdev->vars.attn_sig_af_inv_reg_addr[1] =
        PORT_ID(pdev) ? MISC_REG_AEU_AFTER_INVERT_2_FUNC_1 : MISC_REG_AEU_AFTER_INVERT_2_FUNC_0;
    pdev->vars.attn_sig_af_inv_reg_addr[2] =
        PORT_ID(pdev) ? MISC_REG_AEU_AFTER_INVERT_3_FUNC_1 : MISC_REG_AEU_AFTER_INVERT_3_FUNC_0;
    pdev->vars.attn_sig_af_inv_reg_addr[3] =
        PORT_ID(pdev) ? MISC_REG_AEU_AFTER_INVERT_4_FUNC_1 : MISC_REG_AEU_AFTER_INVERT_4_FUNC_0;
    pdev->vars.attn_sig_af_inv_reg_addr[4] =
        PORT_ID(pdev) ? MISC_REG_AEU_AFTER_INVERT_5_FUNC_1 : MISC_REG_AEU_AFTER_INVERT_5_FUNC_0;

    // init the non-default status blocks
    LM_FOREACH_SB_ID(pdev, sb_id)
    {
        lm_init_non_def_status_block(pdev, sb_id, port);
    }

    if (pdev->params.int_coalesing_mode_disabled_by_ndis) {
        lm_set_interrupt_moderation(pdev, FALSE);
    }
    // init the default status block  - composed of 5 parts per storm: Attention bits, Ustorm, Cstorm, Xstorm, Tstorm

    //Init the attention bits part of the default status block
    lm_init_sp_status_block(pdev);
}

//acquire split MCP access lock register
static lm_status_t
acquire_split_alr(
    lm_device_t *pdev)
{
    lm_status_t lm_status;
    u32_t j, cnt;
    u32_t val_wr, val_rd;

    DbgMessage1(pdev, INFORM, "acquire_split_alr() - %d START!\n", FUNC_ID(pdev) );

    //Adjust timeout for our emulation needs
    cnt = 30000 * 100;
    val_wr = 1UL << 31;
    val_rd = 0;

    //acquire lock using mcpr_access_lock SPLIT register

    for(j = 0; j < cnt*10; j++)
    {
        REG_WR(pdev,  GRCBASE_MCP + 0x9c, val_wr);
        val_rd = REG_RD(pdev,  GRCBASE_MCP + 0x9c);
        if (val_rd & (1UL << 31))
        {
            break;
        }

        mm_wait(pdev, 5);
    }

    if(val_rd & (1UL << 31))
    {
        lm_status = LM_STATUS_SUCCESS;
    }
    else
    {
        DbgBreakMsg("Cannot get access to nvram interface.\n");

        lm_status = LM_STATUS_BUSY;
    }

    DbgMessage1(pdev, INFORM, "acquire_split_alr() - %d END!\n", FUNC_ID(pdev) );

    return lm_status;
}

//Release split MCP access lock register
static void
release_split_alr(
    lm_device_t *pdev)
{
    u32_t val = 0;

    DbgMessage1(pdev, INFORM, "release_split_alr() - %d START!\n", FUNC_ID(pdev) );

    //This is only a sanity check, can remove later in free build.
    val= REG_RD(pdev, GRCBASE_MCP + 0x9c);
    DbgBreakIf(!(val & (1L << 31)));

    val = 0;

    //release mcpr_access_lock SPLIT register
    REG_WR(pdev,  GRCBASE_MCP + 0x9c, val);
    DbgMessage1(pdev, INFORM, "release_split_alr() - %d END!\n", FUNC_ID(pdev) );
} /* release_nvram_lock */

static void lm_nig_processing(lm_device_t *pdev)
{
    u32_t nig_status_port          = 0;
    u32_t unicore_val              = 0;
    u32_t is_unicore_intr_asserted = 0;
    // save nig interrupt mask and set it back later
    lm_link_update(pdev);
    if (PORT_ID(pdev) == 0)
    {
        //read the status interrupt of the NIG for the appropriate port (will do read-modify-write)
        nig_status_port = REG_RD(pdev,  NIG_REG_STATUS_INTERRUPT_PORT0);

        //pass over each of the 24 NIG REG to find out why the NIG attention was asserted.
        //every unicore interrupt read, in case it differs from the corresponding bit in the
        //NIG_REG_STATUS_INTERRUPT_PORT0, then we need to assign the value read into the apporpriate bit
        // in NIG_REG_STATUS_INTERRUPT_PORT0 register.

        //HANDLE_UNICORE_INT_ASSERTED(pdev, NIG_REG_EMAC0_STATUS_MISC_MI_INT, &unicore_val, NIG_STATUS_INTERRUPT_PORT0_REG_STATUS_EMAC0_MISC_MI_INT, &nig_status_port, &is_unicore_intr_asserted, NIG_STATUS_INTERRUPT_PORT0_REG_STATUS_EMAC0_MISC_MI_INT_SIZE);
        HANDLE_UNICORE_INT_ASSERTED(pdev, NIG_REG_EMAC0_STATUS_MISC_MI_COMPLETE, &unicore_val, NIG_STATUS_INTERRUPT_PORT0_REG_STATUS_EMAC0_MISC_MI_COMPLETE, &nig_status_port, &is_unicore_intr_asserted, NIG_STATUS_INTERRUPT_PORT0_REG_STATUS_EMAC0_MISC_MI_COMPLETE_SIZE);
        HANDLE_UNICORE_INT_ASSERTED(pdev, NIG_REG_EMAC0_STATUS_MISC_CFG_CHANGE, &unicore_val, NIG_STATUS_INTERRUPT_PORT0_REG_STATUS_EMAC0_MISC_CFG_CHANGE, &nig_status_port, &is_unicore_intr_asserted, NIG_STATUS_INTERRUPT_PORT0_REG_STATUS_EMAC0_MISC_CFG_CHANGE_SIZE);
        HANDLE_UNICORE_INT_ASSERTED(pdev, NIG_REG_EMAC0_STATUS_MISC_LINK_STATUS, &unicore_val, NIG_STATUS_INTERRUPT_PORT0_REG_STATUS_EMAC0_MISC_LINK_STATUS, &nig_status_port, &is_unicore_intr_asserted, NIG_STATUS_INTERRUPT_PORT0_REG_STATUS_EMAC0_MISC_LINK_STATUS_SIZE);
        HANDLE_UNICORE_INT_ASSERTED(pdev, NIG_REG_EMAC0_STATUS_MISC_LINK_CHANGE, &unicore_val, NIG_STATUS_INTERRUPT_PORT0_REG_STATUS_EMAC0_MISC_LINK_CHANGE, &nig_status_port, &is_unicore_intr_asserted, NIG_STATUS_INTERRUPT_PORT0_REG_STATUS_EMAC0_MISC_LINK_CHANGE_SIZE);
        HANDLE_UNICORE_INT_ASSERTED(pdev, NIG_REG_EMAC0_STATUS_MISC_ATTN, &unicore_val, NIG_STATUS_INTERRUPT_PORT0_REG_STATUS_EMAC0_MISC_ATTN, &nig_status_port, &is_unicore_intr_asserted, NIG_STATUS_INTERRUPT_PORT0_REG_STATUS_EMAC0_MISC_ATTN_SIZE);
        HANDLE_UNICORE_INT_ASSERTED(pdev, NIG_REG_SERDES0_STATUS_MAC_CRS, &unicore_val, NIG_STATUS_INTERRUPT_PORT0_REG_STATUS_SERDES0_MAC_CRS, &nig_status_port, &is_unicore_intr_asserted, NIG_STATUS_INTERRUPT_PORT0_REG_STATUS_SERDES0_MAC_CRS_SIZE);
        HANDLE_UNICORE_INT_ASSERTED(pdev, NIG_REG_SERDES0_STATUS_AUTONEG_COMPLETE, &unicore_val, NIG_STATUS_INTERRUPT_PORT0_REG_STATUS_SERDES0_AUTONEG_COMPLETE, &nig_status_port, &is_unicore_intr_asserted, NIG_STATUS_INTERRUPT_PORT0_REG_STATUS_SERDES0_AUTONEG_COMPLETE_SIZE);
        HANDLE_UNICORE_INT_ASSERTED(pdev, NIG_REG_SERDES0_STATUS_FIBER_RXACT, &unicore_val, NIG_STATUS_INTERRUPT_PORT0_REG_STATUS_SERDES0_FIBER_RXACT, &nig_status_port, &is_unicore_intr_asserted, NIG_STATUS_INTERRUPT_PORT0_REG_STATUS_SERDES0_FIBER_RXACT_SIZE);
        //HANDLE_UNICORE_INT_ASSERTED(pdev, NIG_REG_SERDES0_STATUS_LINK_STATUS, &unicore_val, NIG_STATUS_INTERRUPT_PORT0_REG_STATUS_SERDES0_LINK_STATUS, &nig_status_port, &is_unicore_intr_asserted, NIG_STATUS_INTERRUPT_PORT0_REG_STATUS_SERDES0_LINK_STATUS_SIZE);
        HANDLE_UNICORE_INT_ASSERTED(pdev, NIG_REG_SERDES0_STATUS_MR_PAGE_RX, &unicore_val, NIG_STATUS_INTERRUPT_PORT0_REG_STATUS_SERDES0_MR_PAGE_RX, &nig_status_port, &is_unicore_intr_asserted, NIG_STATUS_INTERRUPT_PORT0_REG_STATUS_SERDES0_MR_PAGE_RX_SIZE);
        HANDLE_UNICORE_INT_ASSERTED(pdev, NIG_REG_SERDES0_STATUS_CL73_AN_COMPLETE, &unicore_val, NIG_STATUS_INTERRUPT_PORT0_REG_STATUS_SERDES0_CL73_AN_COMPLETE, &nig_status_port, &is_unicore_intr_asserted, NIG_STATUS_INTERRUPT_PORT0_REG_STATUS_SERDES0_CL73_AN_COMPLETE_SIZE);
        HANDLE_UNICORE_INT_ASSERTED(pdev, NIG_REG_SERDES0_STATUS_CL73_MR_PAGE_RX, &unicore_val, NIG_STATUS_INTERRUPT_PORT0_REG_STATUS_SERDES0_CL73_MR_PAGE_RX, &nig_status_port, &is_unicore_intr_asserted, NIG_STATUS_INTERRUPT_PORT0_REG_STATUS_SERDES0_CL73_MR_PAGE_RX_SIZE);
        HANDLE_UNICORE_INT_ASSERTED(pdev, NIG_REG_SERDES0_STATUS_RX_SIGDET, &unicore_val, NIG_STATUS_INTERRUPT_PORT0_REG_STATUS_SERDES0_RX_SIGDET, &nig_status_port, &is_unicore_intr_asserted, NIG_STATUS_INTERRUPT_PORT0_REG_STATUS_SERDES0_RX_SIGDET_SIZE);
        HANDLE_UNICORE_INT_ASSERTED(pdev, NIG_REG_XGXS0_STATUS_REMOTEMDIOREQ, &unicore_val, NIG_STATUS_INTERRUPT_PORT0_REG_STATUS_XGXS0_REMOTEMDIOREQ, &nig_status_port, &is_unicore_intr_asserted, NIG_STATUS_INTERRUPT_PORT0_REG_STATUS_XGXS0_REMOTEMDIOREQ_SIZE);
        //HANDLE_UNICORE_INT_ASSERTED(pdev, NIG_REG_XGXS0_STATUS_LINK10G, &unicore_val, NIG_STATUS_INTERRUPT_PORT0_REG_STATUS_XGXS0_LINK10G, &nig_status_port, &is_unicore_intr_asserted, NIG_STATUS_INTERRUPT_PORT0_REG_STATUS_XGXS0_LINK10G_SIZE);
        HANDLE_UNICORE_INT_ASSERTED(pdev, NIG_REG_XGXS0_STATUS_AUTONEG_COMPLETE, &unicore_val, NIG_STATUS_INTERRUPT_PORT0_REG_STATUS_XGXS0_AUTONEG_COMPLETE, &nig_status_port, &is_unicore_intr_asserted, NIG_STATUS_INTERRUPT_PORT0_REG_STATUS_XGXS0_AUTONEG_COMPLETE_SIZE);
        HANDLE_UNICORE_INT_ASSERTED(pdev, NIG_REG_XGXS0_STATUS_FIBER_RXACT, &unicore_val, NIG_STATUS_INTERRUPT_PORT0_REG_STATUS_XGXS0_FIBER_RXACT, &nig_status_port, &is_unicore_intr_asserted, NIG_STATUS_INTERRUPT_PORT0_REG_STATUS_XGXS0_FIBER_RXACT_SIZE);
        //HANDLE_UNICORE_INT_ASSERTED(pdev, NIG_REG_XGXS0_STATUS_LINK_STATUS, &unicore_val, NIG_STATUS_INTERRUPT_PORT0_REG_STATUS_XGXS0_LINK_STATUS, &nig_status_port, &is_unicore_intr_asserted, NIG_STATUS_INTERRUPT_PORT0_REG_STATUS_XGXS0_LINK_STATUS_SIZE);
        HANDLE_UNICORE_INT_ASSERTED(pdev, NIG_REG_XGXS0_STATUS_MR_PAGE_RX, &unicore_val, NIG_STATUS_INTERRUPT_PORT0_REG_STATUS_XGXS0_MR_PAGE_RX, &nig_status_port, &is_unicore_intr_asserted, NIG_STATUS_INTERRUPT_PORT0_REG_STATUS_XGXS0_MR_PAGE_RX_SIZE);
        HANDLE_UNICORE_INT_ASSERTED(pdev, NIG_REG_XGXS0_STATUS_CL73_AN_COMPLETE, &unicore_val, NIG_STATUS_INTERRUPT_PORT0_REG_STATUS_XGXS0_CL73_AN_COMPLETE, &nig_status_port, &is_unicore_intr_asserted, NIG_STATUS_INTERRUPT_PORT0_REG_STATUS_XGXS0_CL73_AN_COMPLETE_SIZE);
        HANDLE_UNICORE_INT_ASSERTED(pdev, NIG_REG_XGXS0_STATUS_CL73_MR_PAGE_RX, &unicore_val, NIG_STATUS_INTERRUPT_PORT0_REG_STATUS_XGXS0_CL73_MR_PAGE_RX, &nig_status_port, &is_unicore_intr_asserted, NIG_STATUS_INTERRUPT_PORT0_REG_STATUS_XGXS0_CL73_MR_PAGE_RX_SIZE);
        HANDLE_UNICORE_INT_ASSERTED(pdev, NIG_REG_XGXS0_STATUS_RX_SIGDET, &unicore_val, NIG_STATUS_INTERRUPT_PORT0_REG_STATUS_XGXS0_RX_SIGDET, &nig_status_port, &is_unicore_intr_asserted, NIG_STATUS_INTERRUPT_PORT0_REG_STATUS_XGXS0_RX_SIGDET_SIZE);
        HANDLE_UNICORE_INT_ASSERTED(pdev, NIG_REG_XGXS0_STATUS_MAC_CRS, &unicore_val, NIG_STATUS_INTERRUPT_PORT0_REG_STATUS_XGXS0_MAC_CRS, &nig_status_port, &is_unicore_intr_asserted, NIG_STATUS_INTERRUPT_PORT0_REG_STATUS_XGXS0_MAC_CRS_SIZE);

        //write back the updated status interrupt of the NIG for the appropriate port.
        REG_WR(pdev,  NIG_REG_STATUS_INTERRUPT_PORT0, nig_status_port);
    }
    else
    {
        DbgBreakIf(PORT_ID(pdev) != 1);
        nig_status_port = REG_RD(pdev,  NIG_REG_STATUS_INTERRUPT_PORT1);

        //HANDLE_UNICORE_INT_ASSERTED(pdev, NIG_REG_EMAC1_STATUS_MISC_MI_INT, &unicore_val, NIG_STATUS_INTERRUPT_PORT1_REG_STATUS_EMAC1_MISC_MI_INT, &nig_status_port, &is_unicore_intr_asserted, NIG_STATUS_INTERRUPT_PORT1_REG_STATUS_EMAC1_MISC_MI_INT_SIZE);
        HANDLE_UNICORE_INT_ASSERTED(pdev, NIG_REG_EMAC1_STATUS_MISC_MI_COMPLETE, &unicore_val, NIG_STATUS_INTERRUPT_PORT1_REG_STATUS_EMAC1_MISC_MI_COMPLETE, &nig_status_port, &is_unicore_intr_asserted, NIG_STATUS_INTERRUPT_PORT1_REG_STATUS_EMAC1_MISC_MI_COMPLETE_SIZE);
        HANDLE_UNICORE_INT_ASSERTED(pdev, NIG_REG_EMAC1_STATUS_MISC_CFG_CHANGE, &unicore_val, NIG_STATUS_INTERRUPT_PORT1_REG_STATUS_EMAC1_MISC_CFG_CHANGE, &nig_status_port, &is_unicore_intr_asserted, NIG_STATUS_INTERRUPT_PORT1_REG_STATUS_EMAC1_MISC_CFG_CHANGE_SIZE);
        HANDLE_UNICORE_INT_ASSERTED(pdev, NIG_REG_EMAC1_STATUS_MISC_LINK_STATUS, &unicore_val, NIG_STATUS_INTERRUPT_PORT1_REG_STATUS_EMAC1_MISC_LINK_STATUS, &nig_status_port, &is_unicore_intr_asserted, NIG_STATUS_INTERRUPT_PORT1_REG_STATUS_EMAC1_MISC_LINK_STATUS_SIZE);
        HANDLE_UNICORE_INT_ASSERTED(pdev, NIG_REG_EMAC1_STATUS_MISC_LINK_CHANGE, &unicore_val, NIG_STATUS_INTERRUPT_PORT1_REG_STATUS_EMAC1_MISC_LINK_CHANGE, &nig_status_port, &is_unicore_intr_asserted, NIG_STATUS_INTERRUPT_PORT1_REG_STATUS_EMAC1_MISC_LINK_CHANGE_SIZE);
        HANDLE_UNICORE_INT_ASSERTED(pdev, NIG_REG_EMAC1_STATUS_MISC_ATTN, &unicore_val, NIG_STATUS_INTERRUPT_PORT1_REG_STATUS_EMAC1_MISC_ATTN, &nig_status_port, &is_unicore_intr_asserted, NIG_STATUS_INTERRUPT_PORT1_REG_STATUS_EMAC1_MISC_ATTN_SIZE);
        HANDLE_UNICORE_INT_ASSERTED(pdev, NIG_REG_SERDES1_STATUS_MAC_CRS, &unicore_val, NIG_STATUS_INTERRUPT_PORT1_REG_STATUS_SERDES1_MAC_CRS, &nig_status_port, &is_unicore_intr_asserted, NIG_STATUS_INTERRUPT_PORT1_REG_STATUS_SERDES1_MAC_CRS_SIZE);
        HANDLE_UNICORE_INT_ASSERTED(pdev, NIG_REG_SERDES1_STATUS_AUTONEG_COMPLETE, &unicore_val, NIG_STATUS_INTERRUPT_PORT1_REG_STATUS_SERDES1_AUTONEG_COMPLETE, &nig_status_port, &is_unicore_intr_asserted, NIG_STATUS_INTERRUPT_PORT1_REG_STATUS_SERDES1_AUTONEG_COMPLETE_SIZE);
        HANDLE_UNICORE_INT_ASSERTED(pdev, NIG_REG_SERDES1_STATUS_FIBER_RXACT, &unicore_val, NIG_STATUS_INTERRUPT_PORT1_REG_STATUS_SERDES1_FIBER_RXACT, &nig_status_port, &is_unicore_intr_asserted, NIG_STATUS_INTERRUPT_PORT1_REG_STATUS_SERDES1_FIBER_RXACT_SIZE);
        //HANDLE_UNICORE_INT_ASSERTED(pdev, NIG_REG_SERDES1_STATUS_LINK_STATUS, &unicore_val, NIG_STATUS_INTERRUPT_PORT1_REG_STATUS_SERDES1_LINK_STATUS, &nig_status_port, &is_unicore_intr_asserted, NIG_STATUS_INTERRUPT_PORT1_REG_STATUS_SERDES1_LINK_STATUS_SIZE);
        HANDLE_UNICORE_INT_ASSERTED(pdev, NIG_REG_SERDES1_STATUS_MR_PAGE_RX, &unicore_val, NIG_STATUS_INTERRUPT_PORT1_REG_STATUS_SERDES1_MR_PAGE_RX, &nig_status_port, &is_unicore_intr_asserted, NIG_STATUS_INTERRUPT_PORT1_REG_STATUS_SERDES1_MR_PAGE_RX_SIZE);
        HANDLE_UNICORE_INT_ASSERTED(pdev, NIG_REG_SERDES1_STATUS_CL73_AN_COMPLETE, &unicore_val, NIG_STATUS_INTERRUPT_PORT1_REG_STATUS_SERDES1_CL73_AN_COMPLETE, &nig_status_port, &is_unicore_intr_asserted, NIG_STATUS_INTERRUPT_PORT1_REG_STATUS_SERDES1_CL73_AN_COMPLETE_SIZE);
        HANDLE_UNICORE_INT_ASSERTED(pdev, NIG_REG_SERDES1_STATUS_CL73_MR_PAGE_RX, &unicore_val, NIG_STATUS_INTERRUPT_PORT1_REG_STATUS_SERDES1_CL73_MR_PAGE_RX, &nig_status_port, &is_unicore_intr_asserted, NIG_STATUS_INTERRUPT_PORT1_REG_STATUS_SERDES1_CL73_MR_PAGE_RX_SIZE);
        HANDLE_UNICORE_INT_ASSERTED(pdev, NIG_REG_SERDES1_STATUS_RX_SIGDET, &unicore_val, NIG_STATUS_INTERRUPT_PORT1_REG_STATUS_SERDES1_RX_SIGDET, &nig_status_port, &is_unicore_intr_asserted, NIG_STATUS_INTERRUPT_PORT1_REG_STATUS_SERDES1_RX_SIGDET_SIZE);
        HANDLE_UNICORE_INT_ASSERTED(pdev, NIG_REG_XGXS1_STATUS_REMOTEMDIOREQ, &unicore_val, NIG_STATUS_INTERRUPT_PORT1_REG_STATUS_XGXS1_REMOTEMDIOREQ, &nig_status_port, &is_unicore_intr_asserted, NIG_STATUS_INTERRUPT_PORT1_REG_STATUS_XGXS1_REMOTEMDIOREQ_SIZE);
        //HANDLE_UNICORE_INT_ASSERTED(pdev, NIG_REG_XGXS1_STATUS_LINK10G, &unicore_val, NIG_STATUS_INTERRUPT_PORT1_REG_STATUS_XGXS1_LINK10G, &nig_status_port, &is_unicore_intr_asserted, NIG_STATUS_INTERRUPT_PORT1_REG_STATUS_XGXS1_LINK10G_SIZE);
        HANDLE_UNICORE_INT_ASSERTED(pdev, NIG_REG_XGXS1_STATUS_AUTONEG_COMPLETE, &unicore_val, NIG_STATUS_INTERRUPT_PORT1_REG_STATUS_XGXS1_AUTONEG_COMPLETE, &nig_status_port, &is_unicore_intr_asserted, NIG_STATUS_INTERRUPT_PORT1_REG_STATUS_XGXS1_AUTONEG_COMPLETE_SIZE);
        HANDLE_UNICORE_INT_ASSERTED(pdev, NIG_REG_XGXS1_STATUS_FIBER_RXACT, &unicore_val, NIG_STATUS_INTERRUPT_PORT1_REG_STATUS_XGXS1_FIBER_RXACT, &nig_status_port, &is_unicore_intr_asserted, NIG_STATUS_INTERRUPT_PORT1_REG_STATUS_XGXS1_FIBER_RXACT_SIZE);
        //HANDLE_UNICORE_INT_ASSERTED(pdev, NIG_REG_XGXS1_STATUS_LINK_STATUS, &unicore_val, NIG_STATUS_INTERRUPT_PORT1_REG_STATUS_XGXS1_LINK_STATUS, &nig_status_port, &is_unicore_intr_asserted, NIG_STATUS_INTERRUPT_PORT1_REG_STATUS_XGXS1_LINK_STATUS_SIZE);
        HANDLE_UNICORE_INT_ASSERTED(pdev, NIG_REG_XGXS1_STATUS_MR_PAGE_RX, &unicore_val, NIG_STATUS_INTERRUPT_PORT1_REG_STATUS_XGXS1_MR_PAGE_RX, &nig_status_port, &is_unicore_intr_asserted, NIG_STATUS_INTERRUPT_PORT1_REG_STATUS_XGXS1_MR_PAGE_RX_SIZE);
        HANDLE_UNICORE_INT_ASSERTED(pdev, NIG_REG_XGXS1_STATUS_CL73_AN_COMPLETE, &unicore_val, NIG_STATUS_INTERRUPT_PORT1_REG_STATUS_XGXS1_CL73_AN_COMPLETE, &nig_status_port, &is_unicore_intr_asserted, NIG_STATUS_INTERRUPT_PORT1_REG_STATUS_XGXS1_CL73_AN_COMPLETE_SIZE);
        HANDLE_UNICORE_INT_ASSERTED(pdev, NIG_REG_XGXS1_STATUS_CL73_MR_PAGE_RX, &unicore_val, NIG_STATUS_INTERRUPT_PORT1_REG_STATUS_XGXS1_CL73_MR_PAGE_RX, &nig_status_port, &is_unicore_intr_asserted, NIG_STATUS_INTERRUPT_PORT1_REG_STATUS_XGXS1_CL73_MR_PAGE_RX_SIZE);
        HANDLE_UNICORE_INT_ASSERTED(pdev, NIG_REG_XGXS1_STATUS_RX_SIGDET, &unicore_val, NIG_STATUS_INTERRUPT_PORT1_REG_STATUS_XGXS1_RX_SIGDET, &nig_status_port, &is_unicore_intr_asserted, NIG_STATUS_INTERRUPT_PORT1_REG_STATUS_XGXS1_RX_SIGDET_SIZE);
        HANDLE_UNICORE_INT_ASSERTED(pdev, NIG_REG_XGXS1_STATUS_MAC_CRS, &unicore_val, NIG_STATUS_INTERRUPT_PORT1_REG_STATUS_XGXS1_MAC_CRS, &nig_status_port, &is_unicore_intr_asserted, NIG_STATUS_INTERRUPT_PORT1_REG_STATUS_XGXS1_MAC_CRS_SIZE);

        REG_WR(pdev,  NIG_REG_STATUS_INTERRUPT_PORT1, nig_status_port);

    }
}

// Change PMF or link change
static void lm_pmf_or_link_event(lm_device_t *pdev, u32_t drv_status)
{
    u32_t val = 0;
    // PMF sent link updates to all func (but himself) OR I become a PMF from MCP notification
    DbgBreakIf(IS_PMF(pdev));

    DbgMessage1(pdev,WARN,"lm_pmf_or_link_event: sync general attention received!!! for func%d\n",FUNC_ID(pdev));

    // sync with link
    MM_ACQUIRE_PHY_LOCK(pdev);
    elink_link_status_update(&pdev->params.link,&pdev->vars.link);
    lm_link_report(pdev);
    MM_RELEASE_PHY_LOCK(pdev);

    if (GET_FLAGS(drv_status,DRV_STATUS_PMF))
    {
        //pmf migration
        pdev->vars.is_pmf = PMF_MIGRATION;
        // load stat from MCP
        MM_ACQUIRE_PHY_LOCK(pdev);
        lm_stats_on_pmf_update(pdev,TRUE);
        MM_RELEASE_PHY_LOCK(pdev);

        // Connect to NIG attentions
        val = (0xff0f | (1 << (VNIC_ID(pdev) + 4)));
        if (INTR_BLK_TYPE(pdev) == INTR_BLK_HC)
        {
            REG_WR(pdev,  (PORT_ID(pdev) ? HC_REG_TRAILING_EDGE_1 : HC_REG_TRAILING_EDGE_0), val);
            REG_WR(pdev,  (PORT_ID(pdev) ? HC_REG_LEADING_EDGE_1  : HC_REG_LEADING_EDGE_0) , val);
        }
        else
        {
            REG_WR(pdev,  IGU_REG_TRAILING_EDGE_LATCH, val);
            REG_WR(pdev,  IGU_REG_LEADING_EDGE_LATCH, val);
        }

        if(TRUE == IS_DCB_ENABLED(pdev))
        {
            lm_dcbx_pmf_migration(pdev);
        }
    }
}

static void lm_dcc_event(lm_device_t *pdev, u32_t dcc_event)
{
    u32_t       val               = 0;
    u32_t       event_val_current = 0;
    u32_t       fw_resp           = 0 ;
    lm_status_t lm_status         = LM_STATUS_FAILURE ;

    DbgMessage1(pdev,WARN,"lm_dcc_event: dcc_event=0x%x\n",dcc_event);

    if( !IS_MULTI_VNIC(pdev) )
    {
        DbgBreakIf(1);
        return;
    }

    // read shemem

    // Read new mf config from shemem
    LM_MFCFG_READ(pdev, OFFSETOF(mf_cfg_t, func_mf_config[ABS_FUNC_ID(pdev)].config), &val);

    pdev->hw_info.mf_info.func_mf_cfg = val ;

    // is it enable/disable
    event_val_current = DRV_STATUS_DCC_DISABLE_ENABLE_PF ;

    if GET_FLAGS( dcc_event, event_val_current )
    {
        if( GET_FLAGS( pdev->hw_info.mf_info.func_mf_cfg, FUNC_MF_CFG_FUNC_DISABLED ) )
        {
            DbgMessage1(pdev,WARN,"lm_dcc_event: mf_cfg function disabled val=0x%x\n",val);

            // TODO - receive packets fronm another machine when link is down - expected - miniport drop packets
            // TBD - disable RX & TX
        }
        else
        {
            DbgMessage1(pdev,WARN,"lm_dcc_event: mf_cfg function enabled val=0x%x\n",val);
            // TBD - enable RX & TX
        }
        lm_status = LM_STATUS_SUCCESS ;
        RESET_FLAGS( dcc_event, event_val_current );
    }

    event_val_current = DRV_STATUS_DCC_BANDWIDTH_ALLOCATION ;

    if GET_FLAGS(dcc_event, event_val_current)
    {
        if( !IS_PMF(pdev) )
        {
            DbgBreakIf(1);
            return;
        }
        lm_status = LM_STATUS_SUCCESS ;
        RESET_FLAGS( dcc_event, event_val_current );
    }

    /* Report results to MCP */
    if (dcc_event)
    {
        // unknown event
        lm_status = lm_mcp_cmd_send_recieve( pdev, lm_mcp_mb_header, DRV_MSG_CODE_DCC_FAILURE, 0, MCP_CMD_DEFAULT_TIMEOUT, &fw_resp ) ;
    }
    else
    {
        // we are done
        if( LM_STATUS_SUCCESS == lm_status )
        {
            // sync with link --> update min max/link for all function
            MM_ACQUIRE_PHY_LOCK(pdev);
            elink_link_status_update(&pdev->params.link,&pdev->vars.link);
            lm_link_report(pdev);
            MM_RELEASE_PHY_LOCK(pdev);
        }
        lm_status = lm_mcp_cmd_send_recieve( pdev, lm_mcp_mb_header, DRV_MSG_CODE_DCC_OK, 0, MCP_CMD_DEFAULT_TIMEOUT, &fw_resp ) ;
        //bnx2x_fw_command(bp, DRV_MSG_CODE_DCC_OK);
    }
    DbgBreakIf( lm_status != LM_STATUS_SUCCESS );
}

static lm_status_t lm_set_bandwidth_event(lm_device_t *pdev)
{
    u32_t       mcp_resp    = 0;
    lm_status_t lm_status   = LM_STATUS_SUCCESS;

    DbgBreakIf(!IS_MULTI_VNIC(pdev) || !pdev->vars.is_pmf);

    MM_ACQUIRE_PHY_LOCK(pdev);

    //update CMNG data from SHMEM
    lm_reload_link_and_cmng(pdev);

    //acknoledge the MCP event
    lm_mcp_cmd_send_recieve(pdev,lm_mcp_mb_header, DRV_MSG_CODE_SET_MF_BW_ACK, 0, MCP_CMD_DEFAULT_TIMEOUT, &mcp_resp);

    if ( mcp_resp != FW_MSG_CODE_SET_MF_BW_DONE)
    {
        DbgBreakIf(mcp_resp != FW_MSG_CODE_SET_MF_BW_DONE);
        lm_status = LM_STATUS_FAILURE;
        goto _exit;
    }

    //indicate link change to OS, since sync_link_status does not generate a link event for the PMF.
    mm_indicate_link(pdev, pdev->vars.link_status, pdev->vars.medium);

    //notify all functions
    sync_link_status(pdev);

_exit:
    MM_RELEASE_PHY_LOCK(pdev);
    return lm_status;
}


static void lm_generic_event(lm_device_t *pdev)
{
    u32_t val    = 0;
    u32_t offset = 0; // for debugging convenient

    offset = MISC_REG_AEU_GENERAL_ATTN_12 + 4*FUNC_ID(pdev) ;

    // reset attention
    REG_WR(pdev, offset ,0x0);

    offset = OFFSETOF(shmem_region_t, func_mb[FUNC_MAILBOX_ID(pdev)].drv_status) ;

    // drv_status
    LM_SHMEM_READ(pdev,
                  offset,
                  &val);

    // E1H NIG status sync attention mapped to group 4-7

    if (GET_FLAGS( val, DRV_STATUS_VF_DISABLED))
    {
        u32_t mcp_vf_disabled[E2_VF_MAX / 32];
        u32_t i, fw_resp = 0;
        // Read VFs
        for (i = 0; i < E2_VF_MAX / 32; i++) {
            LM_SHMEM2_READ(pdev, OFFSETOF(shmem2_region_t,mcp_vf_disabled[i]), &mcp_vf_disabled[i]);
        }
        DbgMessage2(pdev,FATAL,"lm_generic_event: DRV_STATUS_VF_DISABLED received for vfs bitmap %x %x!!!\n", mcp_vf_disabled[0], mcp_vf_disabled[1]);

        // SHMULIK, PLACE YOUR CODE HERE ( Handle only VFs of this PF )

        // Acknoledge the VFs you handled ( This array is per PF driver on path )
        for (i = 0; i < E2_VF_MAX / 32; i++) {
            LM_SHMEM2_WRITE(pdev, OFFSETOF(shmem2_region_t,drv_ack_vf_disabled[FUNC_MAILBOX_ID(pdev)][i]), mcp_vf_disabled[i]);
        }
        lm_mcp_cmd_send_recieve( pdev,
                     lm_mcp_mb_header,
                     DRV_MSG_CODE_VF_DISABLED_DONE,
                     0,
                     MCP_CMD_DEFAULT_TIMEOUT,
                     &fw_resp);
        return; // YANIV - DEBUG @@@!!!
    }
    if(IS_MULTI_VNIC(pdev))
    {
        if( GET_FLAGS( val, DRV_STATUS_DCC_EVENT_MASK ) )
        {
            lm_dcc_event(pdev, (DRV_STATUS_DCC_EVENT_MASK & val) );
        }
        if (GET_FLAGS(val, DRV_STATUS_SET_MF_BW ))
        {
            lm_set_bandwidth_event(pdev);
        }
        ///if val has any NIV event flags, call lm_niv_event
        if ( GET_FLAGS(val, DRV_STATUS_VNTAG_EVENT_MASK) )
        {
            lm_niv_event(pdev, GET_FLAGS(val, DRV_STATUS_VNTAG_EVENT_MASK) );
        }
        if(!pdev->vars.is_pmf)
        {
            lm_pmf_or_link_event(pdev, val);
        }
    }

        lm_dcbx_event(pdev,val);

}

static void lm_gen_attn_everest_processing(lm_device_t *pdev, u32_t sig_word_aft_inv)
{
    u32_t offset = 0; // for debugging convenient
    u32_t       val        = 0;

    //pass over all attention generals which are wired to a dynamic group of the lower 8 bits
    offset = GENERAL_ATTEN_OFFSET(TSTORM_FATAL_ASSERT_ATTENTION_BIT) ;
    if ( offset & sig_word_aft_inv)
    {
        REG_WR(pdev,MISC_REG_AEU_GENERAL_ATTN_7,0x0);
        DbgMessage(pdev,FATAL,"lm_gen_attn_everest_processing: TSTORM_FATAL_ASSERT_ATTENTION_BIT received!!!\n");
        DbgBreakIfAll(1);
    }
    offset = GENERAL_ATTEN_OFFSET(USTORM_FATAL_ASSERT_ATTENTION_BIT);
    if ( offset & sig_word_aft_inv)
    {
        REG_WR(pdev,MISC_REG_AEU_GENERAL_ATTN_8,0x0);
        DbgMessage(pdev,FATAL,"lm_gen_attn_everest_processing: USTORM_FATAL_ASSERT_ATTENTION_BIT received!!!\n");
        DbgBreakIfAll(1);
    }
    offset = GENERAL_ATTEN_OFFSET(CSTORM_FATAL_ASSERT_ATTENTION_BIT);
    if ( offset & sig_word_aft_inv)
    {
        REG_WR(pdev,MISC_REG_AEU_GENERAL_ATTN_9,0x0);
        DbgMessage(pdev,FATAL,"lm_gen_attn_everest_processing: CSTORM_FATAL_ASSERT_ATTENTION_BIT received!!!\n");
        DbgBreakIfAll(1);
    }
    offset = GENERAL_ATTEN_OFFSET(XSTORM_FATAL_ASSERT_ATTENTION_BIT);
    if ( offset & sig_word_aft_inv)
    {
        REG_WR(pdev,MISC_REG_AEU_GENERAL_ATTN_10,0x0);
        DbgMessage(pdev,FATAL,"lm_gen_attn_everest_processing: XSTORM_FATAL_ASSERT_ATTENTION_BIT received!!!\n");
        DbgBreakIfAll(1);
    }
    offset = GENERAL_ATTEN_OFFSET(MCP_FATAL_ASSERT_ATTENTION_BIT);
    if ( offset & sig_word_aft_inv)
    {
        REG_WR(pdev,MISC_REG_AEU_GENERAL_ATTN_11,0x0);
        val = lm_mcp_check(pdev);
        DbgMessage1(pdev,FATAL,"lm_gen_attn_everest_processing: MCP_FATAL_ASSERT_ATTENTION_BIT received mcp_check=0x%x!!!\n" , val);
        DbgBreakIfAll(1);
    }
     // E1H NIG status sync attention mapped to group 4-7
    if (!CHIP_IS_E1(pdev))
    {
        // PMF change or link update
        offset = (AEU_INPUTS_ATTN_BITS_GRC_MAPPED_GENERAL_ATTN12 << FUNC_ID(pdev));
        if ( offset & sig_word_aft_inv)
        {
           lm_generic_event(pdev);
        }
    }
}

static void lm_dq_attn_everest_processing(lm_device_t *pdev)
{
    u32_t val;
    val=REG_RD(pdev,DORQ_REG_DORQ_INT_STS);
    // TODO add defines here
    DbgMessage1(pdev,FATAL,"DB hw attention 0x%x\n",val);
    // DORQ discard attention
    if (val & 0x2)
    {
        DbgBreakIfAll(1);
    }

}

static void lm_cfc_attn_everest_processing(lm_device_t *pdev)
{
    u32_t val;
    val=REG_RD(pdev,CFC_REG_CFC_INT_STS);
    // TODO add defines here
    DbgMessage1(pdev,FATAL,"CFC hw attention 0x%x\n",val);
    // CFC error attention
    if (val & 0x2)
    {
        DbgBreakIfAll(1);
    }
}
static void lm_pxp_attn_everest_processing(lm_device_t *pdev)
{
    u32_t val = REG_RD(pdev,PXP_REG_PXP_INT_STS_0);

    // TODO add defines here
    DbgMessage1(pdev,FATAL,"PXP hw attention 0x%x\n",val);
    // RQ_USDMDP_FIFO_OVERFLOW attention
    if (val & 0x18000)
    {
        DbgBreakIfAll(1);
    }

}
/*
 *Function Name:lm_spio5_attn_everest_processing
 *
 *Parameters:
 *
 *Description:
 *  Indicates fan failure on specific external_phy_config (PORT_HW_CFG_XGXS_EXT_PHY_TYPE_SFX7101)
 *Returns:
 *
 */
static void lm_spio5_attn_everest_processing(lm_device_t *pdev)
{
    u32_t val           = 0;
    u32_t offset        = 0;
    u32_t ext_phy_config = 0;
   // Special fan failure handling for boards with external PHY SFX7101 (which include fan)
    PHY_HW_LOCK(pdev);
    elink_hw_reset_phy(&pdev->params.link);
    PHY_HW_UNLOCK(pdev);

    offset = ( 0 == PORT_ID(pdev) ) ? MISC_REG_AEU_ENABLE1_FUNC_0_OUT_0 : MISC_REG_AEU_ENABLE1_FUNC_1_OUT_0 ;

    val=REG_RD(pdev, offset );

    DbgMessage1(pdev,FATAL,"SPIO5 hw attention 0x%x\n",val);

    // mask flags so we won't get this attention anymore
    RESET_FLAGS(val, AEU_INPUTS_ATTN_BITS_SPIO5 ) ;
    REG_WR(pdev, offset, val ) ;

    // change phy_type to type failure (under phy lock)
    MM_ACQUIRE_PHY_LOCK(pdev);
    LM_SHMEM_READ(pdev,
                   OFFSETOF(shmem_region_t,dev_info.port_hw_config[PORT_ID(pdev)].external_phy_config),
                   &ext_phy_config);
    RESET_FLAGS(ext_phy_config, PORT_HW_CFG_XGXS_EXT_PHY_TYPE_MASK ) ;
    SET_FLAGS(ext_phy_config, PORT_HW_CFG_XGXS_EXT_PHY_TYPE_FAILURE ) ;
    // Set external phy type to failure for MCP to know about the failure
    LM_SHMEM_WRITE(pdev,
                   OFFSETOF(shmem_region_t,dev_info.port_hw_config[PORT_ID(pdev)].external_phy_config),
                   ext_phy_config);
    DbgMessage1(pdev, WARN, "lm_spio5_attn_everest_processing: external_phy_type 0x%x\n",ext_phy_config);
    MM_RELEASE_PHY_LOCK(pdev);

    // write to the event log!
    mm_event_log_generic( pdev, LM_LOG_ID_FAN_FAILURE );
}

/*
 *------------------------------------------------------------------------
 * FLR in progress handling -
 *-------------------------------------------------------------------------
 */
void lm_fl_reset_set_inprogress(struct _lm_device_t *pdev)
{
    if (IS_PFDEV(pdev))
    {
        pdev->params.is_flr = TRUE;
        DbgMessage1(pdev, FATAL, "PF[%d] is under FLR\n",FUNC_ID(pdev));
    }
#ifdef VF_INVOLVED
    else
    {
        lm_vf_fl_reset_set_inprogress(pdev);
    }
#endif
    return;
}

void lm_fl_reset_clear_inprogress(struct _lm_device_t *pdev)
{
    if (IS_PFDEV(pdev))
    {
        pdev->params.is_flr = FALSE;
        DbgMessage1(pdev, FATAL, "PF[%d] is not under FLR\n",FUNC_ID(pdev));
    }
#ifdef VF_INVOLVED
    else
    {
        lm_vf_fl_reset_clear_inprogress(pdev);
    }
#endif
    return;
}

u8_t lm_fl_reset_is_inprogress(struct _lm_device_t *pdev)
{
    u8_t flr_in_progress = FALSE;
    if (IS_PFDEV(pdev))
    {
        flr_in_progress = pdev->params.is_flr;
    }
#ifdef VF_INVOLVED
    else
    {
        flr_in_progress = lm_vf_fl_reset_is_inprogress(pdev);
    }
#endif
    return  flr_in_progress;
}

/*
 *------------------------------------------------------------------------
 * shutdown in progress handling -
 *
 *
 *
 *------------------------------------------------------------------------
 */

typedef struct _lm_chip_global_t
{
    u8_t  flags;
    u32_t cnt_grc_timeout_ignored;
    u32_t grc_timeout_val[E1H_FUNC_MAX*2]; // we give each function 2 grc timeouts before we ASSERT...

} lm_chip_global_t;

static lm_chip_global_t g_lm_chip_global[MAX_PCI_BUS_NUM] = {{0}};

#define LM_CHIP_GLOBAL_FLAG_RESET_IN_PROGRESS 0x1 // The flag indicates whether

#define LM_CHIP_GLOBAL_FLAG_NIG_RESET_CALLED  0x2 // the flag will be set when lm_reset_path() will do nig reset
                                                  // the flag will be reset after grc timeout occured and the cause is NIG access OR after another "no nig" reset


#define LM_GRC_TIMEOUT_MAX_IGNORE ARRSIZE(g_lm_chip_global[0].grc_timeout_val)



u32_t lm_inc_cnt_grc_timeout_ignore(struct _lm_device_t *pdev, u32_t val)
{
    const        u8_t bus_num  = INST_ID_TO_BUS_NUM(PFDEV(pdev)->vars.inst_id) ;
    static const u8_t arr_size = ARRSIZE(g_lm_chip_global[0].grc_timeout_val);
    const        u8_t idx      = g_lm_chip_global[bus_num].cnt_grc_timeout_ignored % arr_size ;

    g_lm_chip_global[bus_num].grc_timeout_val[idx] = val;

    return ++g_lm_chip_global[bus_num].cnt_grc_timeout_ignored;
}

void lm_set_nig_reset_called(struct _lm_device_t *pdev)
{
    const u8_t bus_num = INST_ID_TO_BUS_NUM(PFDEV(pdev)->vars.inst_id) ;
    const u8_t flags   = LM_CHIP_GLOBAL_FLAG_NIG_RESET_CALLED;

    SET_FLAGS( g_lm_chip_global[bus_num].flags, flags) ;
}

void lm_clear_nig_reset_called(struct _lm_device_t *pdev)
{
    const u8_t bus_num = INST_ID_TO_BUS_NUM(PFDEV(pdev)->vars.inst_id) ;
    const u8_t flags   = LM_CHIP_GLOBAL_FLAG_NIG_RESET_CALLED;

    RESET_FLAGS( g_lm_chip_global[bus_num].flags, flags) ;
}

u8_t lm_is_nig_reset_called(struct _lm_device_t *pdev)
{
    const u8_t bus_num = INST_ID_TO_BUS_NUM(PFDEV(pdev)->vars.inst_id) ;
    const u8_t flags   = LM_CHIP_GLOBAL_FLAG_NIG_RESET_CALLED;

    return ( 0 != GET_FLAGS( g_lm_chip_global[bus_num].flags, flags ) );
}


void lm_reset_set_inprogress(struct _lm_device_t *pdev)
{
    const u8_t bus_num = INST_ID_TO_BUS_NUM(PFDEV(pdev)->vars.inst_id) ;
    const u8_t flags   = LM_CHIP_GLOBAL_FLAG_RESET_IN_PROGRESS;

    SET_FLAGS( g_lm_chip_global[bus_num].flags, flags) ;
}

void lm_reset_clear_inprogress(struct _lm_device_t *pdev)
{
    const u8_t bus_num = INST_ID_TO_BUS_NUM(PFDEV(pdev)->vars.inst_id) ;
    const u8_t flags   = LM_CHIP_GLOBAL_FLAG_RESET_IN_PROGRESS;

    RESET_FLAGS( g_lm_chip_global[bus_num].flags, flags) ;
}

u8_t lm_pm_reset_is_inprogress(struct _lm_device_t *pdev)
{
    const u8_t bus_num = INST_ID_TO_BUS_NUM(PFDEV(pdev)->vars.inst_id) ;
    const u8_t flags   = LM_CHIP_GLOBAL_FLAG_RESET_IN_PROGRESS;

    return ( 0 != GET_FLAGS(g_lm_chip_global[bus_num].flags, flags ) );
}

void lm_read_attn_regs(lm_device_t *pdev, u32_t * attn_sig_af_inv_arr, u32_t arr_size);
u8_t lm_recoverable_error(lm_device_t *pdev, u32_t * attn_sig, u32_t arr_size);

/**
 * @Description
 *      This function checks if there is optionally a attention
 *      pending that is recoverable. If it is, then we won't
 *      assert in the locations that call reset_is_inprogress,
 *      because there's a high probability we'll overcome the
 *      error with recovery
 * @param pdev
 *
 * @return u8_t
 */
u8_t lm_er_handling_pending(struct _lm_device_t *pdev)
{
    u32_t  attn_sig_af_inv_arr[MAX_ATTN_REGS] = {0};

    if (!pdev->params.enable_error_recovery)
    {
        return FALSE;
    }

    lm_read_attn_regs(pdev, attn_sig_af_inv_arr, ARRSIZE(attn_sig_af_inv_arr));

    return lm_recoverable_error(pdev, attn_sig_af_inv_arr, ARRSIZE(attn_sig_af_inv_arr));
}

u8_t lm_reset_is_inprogress(struct _lm_device_t *pdev)
{
    u8_t reset_in_progress =
        lm_pm_reset_is_inprogress(pdev) ||
        lm_er_handling_pending(pdev) ||
        lm_fl_reset_is_inprogress(PFDEV(pdev)) ||
        (IS_VFDEV(pdev) ? lm_fl_reset_is_inprogress(pdev) : FALSE);

    return reset_in_progress;
}

void lm_reset_mask_attn(struct _lm_device_t *pdev)
{
    // mask the pxp attentions
    REG_WR(pdev,PXP_REG_PXP_INT_MASK_0,0xffffffff); // 32 bits
    if (CHIP_IS_E1x(pdev))
    {
        REG_WR(pdev,PXP_REG_PXP_INT_MASK_1,0x1f); // 5 bits
    }
    else
    {
        REG_WR(pdev,PXP_REG_PXP_INT_MASK_1,0xff); // 8 bits
    }
    REG_WR(pdev,PXP2_REG_PXP2_INT_MASK_0,0xffffffff); // 32 bits

    /* We never unmask this register so no need to re-mask it*/
    //REG_WR(pdev,PXP2_REG_PXP2_INT_MASK_1,0x3f); // 32 bits
}

static void lm_latch_attn_everest_processing(lm_device_t *pdev, u32_t sig_word_aft_inv)
{
    u32_t latch_bit_to_clr = 0;
    u32_t val = 0 ;

    //pass over all latched attentions
    if (GENERAL_ATTEN_OFFSET(LATCHED_ATTN_RBCR) & sig_word_aft_inv)
    {
        latch_bit_to_clr = 0x1;
        REG_WR(pdev, MISC_REG_AEU_CLR_LATCH_SIGNAL, latch_bit_to_clr);
        DbgMessage(pdev,FATAL,"lm_latch_attn_everest_processing: LATCHED_ATTN_RBCR received!!!\n");
        DbgBreakIfAll(1);
    }
    if (GENERAL_ATTEN_OFFSET(LATCHED_ATTN_RBCT) & sig_word_aft_inv)
    {
        latch_bit_to_clr = 0x2;
        REG_WR(pdev, MISC_REG_AEU_CLR_LATCH_SIGNAL, latch_bit_to_clr);
        DbgMessage(pdev,FATAL,"lm_latch_attn_everest_processing: LATCHED_ATTN_RBCT received!!!\n");
        DbgBreakIfAll(1);
    }
    if (GENERAL_ATTEN_OFFSET(LATCHED_ATTN_RBCN) & sig_word_aft_inv)
    {
        latch_bit_to_clr = 0x4;
        REG_WR(pdev, MISC_REG_AEU_CLR_LATCH_SIGNAL, latch_bit_to_clr);
        DbgMessage(pdev,FATAL,"lm_latch_attn_everest_processing: LATCHED_ATTN_RBCN received!!!\n");
        DbgBreakIfAll(1);
    }
    if (GENERAL_ATTEN_OFFSET(LATCHED_ATTN_RBCU) & sig_word_aft_inv)
    {
        latch_bit_to_clr = 0x8;
        REG_WR(pdev, MISC_REG_AEU_CLR_LATCH_SIGNAL, latch_bit_to_clr);
        DbgMessage(pdev,FATAL,"lm_latch_attn_everest_processing: LATCHED_ATTN_RBCU received!!!\n");
        DbgBreakIfAll(1);
    }
    if (GENERAL_ATTEN_OFFSET(LATCHED_ATTN_RBCP) & sig_word_aft_inv)
    {
        latch_bit_to_clr = 0x10;
        REG_WR(pdev, MISC_REG_AEU_CLR_LATCH_SIGNAL, latch_bit_to_clr);
        DbgMessage(pdev,FATAL,"lm_latch_attn_everest_processing: LATCHED_ATTN_RBCP received!!! \n");
        DbgBreakIfAll(1);
    }
    if (GENERAL_ATTEN_OFFSET(LATCHED_ATTN_TIMEOUT_GRC) & sig_word_aft_inv)
    {
#define GRC_TIMEOUT_MASK_ADDRESS(_val)  ( (_val)     & ((1<<19)-1)) // 0x000fffff
#define GRC_TIMEOUT_MASK_FUNCTION(_val) ( (_val>>20) & ((1<<3)-1))  // 0x00700000
#define GRC_TIMEOUT_MASK_MASTER(_val)   ( (_val>>24) & ((1<<4)-1))  // 0x0f000000

        u32_t       addr                            = 0;
        u32_t       func                            = 0;
        u32_t       master                          = 0;
        u32_t       grc_timeout_cnt                 = 0;
        u8_t        b_assert                        = TRUE;
        u8_t        b_nig_reset_called              = lm_is_nig_reset_called(pdev);
        const u32_t grc_timeout_max_ignore          = pdev->params.grc_timeout_max_ignore;

        latch_bit_to_clr = 0x20;

        // we check if nig reset was done
        if( b_nig_reset_called )
        {
            b_assert = FALSE;
        }

        if (!CHIP_IS_E1(pdev))
        {
            val    = REG_RD(pdev, MISC_REG_GRC_TIMEOUT_ATTN);
            addr   = GRC_TIMEOUT_MASK_ADDRESS(val);
            func   = GRC_TIMEOUT_MASK_FUNCTION(val);
            master = GRC_TIMEOUT_MASK_MASTER(val);

            // in non E1 we can verify it is mcp cause (due to nig probably)
            if( 2 != master ) // 2 is mcp cause
            {
                b_assert = TRUE;
            }
        }

        REG_WR(pdev, MISC_REG_AEU_CLR_LATCH_SIGNAL, latch_bit_to_clr);
        DbgMessage5(pdev,FATAL,"lm_latch_attn_everest_processing: LATCHED_ATTN_TIMEOUT_GRC received!!! val=0x%08x master=0x%x func=0x%x addr=0x%xx4=0x%X)\n"
                               ,val, master, func, addr, addr*4 );

        // NOTE: we ignore b_nig_reset_called and ASSERT only according to grc_timeout_max_ignore value (default is 0x10)

        grc_timeout_cnt = lm_inc_cnt_grc_timeout_ignore(pdev, val);
        // if we are here it means we ignore the ASSERT inc counter
        if( grc_timeout_cnt >= grc_timeout_max_ignore )
        {
            b_assert = TRUE;
        }
        else
        {
            b_assert = FALSE;
        }

        if( b_assert )
        {
            DbgBreakIf(1);
        }

        if( b_nig_reset_called )
        {
            // we reset the flag (we "allow" one timeout after nig reset)
            lm_clear_nig_reset_called(pdev);
        }
    }
    if (GENERAL_ATTEN_OFFSET(LATCHED_ATTN_RSVD_GRC) & sig_word_aft_inv)
    {
        latch_bit_to_clr = 0x40;
        REG_WR(pdev, MISC_REG_AEU_CLR_LATCH_SIGNAL, latch_bit_to_clr);
        DbgMessage(pdev,FATAL,"lm_latch_attn_everest_processing: LATCHED_ATTN_RSVD_GRC received!!!\n");
        DbgBreakIfAll(1);
    }
    if (GENERAL_ATTEN_OFFSET(LATCHED_ATTN_ROM_PARITY_MCP) & sig_word_aft_inv)
    {
        latch_bit_to_clr = 0x80;
        REG_WR(pdev, MISC_REG_AEU_CLR_LATCH_SIGNAL, latch_bit_to_clr);
        DbgMessage(pdev,FATAL,"lm_latch_attn_everest_processing: LATCHED_ATTN_ROM_PARITY_MCP received!!!\n");
        /* For E2, at the time this code was written (e2-bringup ) the parity is (somehow) expected */
        if (CHIP_IS_E1x(pdev))
        {
            DbgBreakIfAll(1);
        }
        else
        {
            DbgBreakIf(1);
        }
    }
    if (GENERAL_ATTEN_OFFSET(LATCHED_ATTN_UM_RX_PARITY_MCP) & sig_word_aft_inv)
    {
        latch_bit_to_clr = 0x100;
        REG_WR(pdev, MISC_REG_AEU_CLR_LATCH_SIGNAL, latch_bit_to_clr);
        DbgMessage(pdev,FATAL,"lm_latch_attn_everest_processing: LATCHED_ATTN_UM_RX_PARITY_MCP received!!!\n");
        DbgBreakIfAll(1);
    }
    if (GENERAL_ATTEN_OFFSET(LATCHED_ATTN_UM_TX_PARITY_MCP) & sig_word_aft_inv)
    {
        latch_bit_to_clr = 0x200;
        REG_WR(pdev, MISC_REG_AEU_CLR_LATCH_SIGNAL, latch_bit_to_clr);
        DbgMessage(pdev,FATAL,"lm_latch_attn_everest_processing: LATCHED_ATTN_UM_TX_PARITY_MCP received!!!\n");
        DbgBreakIfAll(1);
    }
    if (GENERAL_ATTEN_OFFSET(LATCHED_ATTN_SCPAD_PARITY_MCP) & sig_word_aft_inv)
    {
        latch_bit_to_clr = 0x400;
        REG_WR(pdev, MISC_REG_AEU_CLR_LATCH_SIGNAL, latch_bit_to_clr);
        DbgMessage(pdev,FATAL,"lm_latch_attn_everest_processing: LATCHED_ATTN_SCPAD_PARITY_MCP received!!!\n");
        /* For E2, at the time this code was written (e2-bringup ) the parity is expected */
        if (CHIP_IS_E1x(pdev))
        {
            DbgBreakIfAll(1);
        }
    }
}

static void lm_hard_wired_processing(lm_device_t *pdev, u16_t assertion_proc_flgs)
{
    /* processing of highest 8-15 bits of 8 "hard-wired" attention signals toward IGU.
       Excluding NIG & PXP "close the gates"

       ! No need to lock here since this is an uncommon group whether there is a recovery procedure or not.

       Signal name         Bit position    SOURCE       Type        Required Destination
       -----------------------------------------------------------------------------
       NIG attention for port0  D8         NIG          Event       MCP/Driver0(PHY)
       SW timer#4 port0         D9         MISC         Event       MCP -> Ignore!
       GPIO#2 port0             D10        MISC         Event       MCP
       GPIO#3 port0             D11        MISC         Event       MCP
       GPIO#4 port0             D12        MISC         Event       MCP
       General attn1            D13        GRC mapped   Attention   MCP/Driver0/Driver1 -> ASSERT!
       General attn2            D14        GRC mapped   Attention   MCP/Driver0/Driver1 -> ASSERT!
       General attn3            D15        GRC mapped   Attention   MCP/Driver0/Driver1 -> ASSERT!
    */
    //TODO: for the required attn signals, need to "clean the hw block" (INT_STS_CLR..)
    if (PORT_ID(pdev) == 0)
    {
        if (assertion_proc_flgs & ATTN_SW_TIMER_4_FUNC)
        {
            DbgMessage(pdev, FATAL, "lm_hard_wired_processing: ATTN_SW_TIMER_4_FUNC!\n");
            //to deal with this signal, add dispatch func call here
        }
        if (assertion_proc_flgs & GPIO_2_FUNC)
        {
            DbgMessage(pdev, WARN, "lm_hard_wired_processing: GPIO_1_FUNC!\n");
            //to deal with this signal, add dispatch func call here
        }
        if (assertion_proc_flgs & GPIO_3_FUNC)
        {
            DbgMessage(pdev, WARN, "lm_hard_wired_processing: GPIO_2_FUNC!\n");
            //to deal with this signal, add dispatch func call here
        }
        if (assertion_proc_flgs & GPIO_4_FUNC)
        {
        DbgMessage(pdev, WARN, "lm_hard_wired_processing: GPIO_3_FUNC0!\n");
        // Will be handled in deassertion
        }
        if (assertion_proc_flgs & ATTN_GENERAL_ATTN_1)
        {
            DbgMessage(pdev, FATAL, "lm_hard_wired_processing: ATTN_GENERAL_ATTN_1! and clean it!!!\n");
            REG_WR(pdev,MISC_REG_AEU_GENERAL_ATTN_1,0x0);
        }
        if (assertion_proc_flgs & ATTN_GENERAL_ATTN_2)
        {
            DbgMessage(pdev, FATAL, "lm_hard_wired_processing: ATTN_GENERAL_ATTN_2! and clean it!!!\n");
            REG_WR(pdev,MISC_REG_AEU_GENERAL_ATTN_2,0x0);
        }
        if (assertion_proc_flgs & ATTN_GENERAL_ATTN_3)
        {
            DbgMessage(pdev, FATAL, "lm_hard_wired_processing: ATTN_GENERAL_ATTN_3! and clean it!!!\n");
            REG_WR(pdev,MISC_REG_AEU_GENERAL_ATTN_3,0x0);
        }
    }
    else
    {
        DbgBreakIf(PORT_ID(pdev) != 1);

        if (assertion_proc_flgs & ATTN_SW_TIMER_4_FUNC1)
        {
            DbgMessage(pdev, FATAL, "lm_hard_wired_processing: ATTN_SW_TIMER_4_FUNC1!\n");
            //to deal with this signal, add dispatch func call here
        }
        if (assertion_proc_flgs & GPIO_2_FUNC1)
        {
            DbgMessage(pdev, WARN, "lm_hard_wired_processing: GPIO_1_FUNC1!\n");
            //to deal with this signal, add dispatch func call here
        }
        if (assertion_proc_flgs & GPIO_3_FUNC1)
        {
            DbgMessage(pdev, WARN, "lm_hard_wired_processing: GPIO_2_FUNC1!\n");
            //to deal with this signal, add dispatch func call here
        }
        if (assertion_proc_flgs & GPIO_4_FUNC1)
        {
            DbgMessage(pdev, WARN, "lm_hard_wired_processing: GPIO_3_FUNC1!\n");
            // Will be handled in deassertion
        }
        if (assertion_proc_flgs & ATTN_GENERAL_ATTN_4)
        {
            DbgMessage(pdev, FATAL, "lm_hard_wired_processing: ATTN_GENERAL_ATTN_4! and clean it!!!\n");
            REG_WR(pdev,MISC_REG_AEU_GENERAL_ATTN_4,0x0);
        }
        if (assertion_proc_flgs & ATTN_GENERAL_ATTN_5)
        {
            DbgMessage(pdev, FATAL, "lm_hard_wired_processing: ATTN_GENERAL_ATTN_5! and clean it!!!\n");
            REG_WR(pdev,MISC_REG_AEU_GENERAL_ATTN_5,0x0);
        }
        if (assertion_proc_flgs & ATTN_GENERAL_ATTN_6)
        {
            DbgMessage(pdev, FATAL, "lm_hard_wired_processing: ATTN_GENERAL_ATTN_6! and clean it!!!\n");
            REG_WR(pdev,MISC_REG_AEU_GENERAL_ATTN_6,0x0);
        }
    }
}

static INLINE void lm_inc_er_debug_idx(lm_device_t * pdev)
{
    pdev->debug_info.curr_er_debug_idx++;
    if (pdev->debug_info.curr_er_debug_idx == MAX_ER_DEBUG_ENTRIES)
    {
        pdev->debug_info.curr_er_debug_idx=0;
    }
}

/**
 * @description
 *      called from attention handling routines, checks if the
 *      attention received is an error which is recoverable via
 *      process kill. If error recovery is disabled this
 *      function always returns FALSE;
 *
 * @param pdev
 * @param attn_sig : values of the after_invert registers read
 *                 in the misc that indicate which attention
 *                 occured
 *
 *
 * @return u8_t TRUE: attention requires process_kill. FALSE o/w
 */
u8_t lm_recoverable_error(lm_device_t *pdev, u32_t * attn_sig, u32_t arr_size)
{
    lm_er_debug_info_t * debug_info = NULL;
    u32_t                i;

    if (!pdev->params.enable_error_recovery)
    {
        return FALSE;
    }

    ASSERT_STATIC(ARRSIZE(debug_info->attn_sig) >= MAX_ATTN_REGS);
    DbgBreakIf(arr_size < MAX_ATTN_REGS);

    if ((attn_sig[0] & HW_PRTY_ASSERT_SET_0) || (attn_sig[1] & HW_PRTY_ASSERT_SET_1) ||
        (attn_sig[2] & HW_PRTY_ASSERT_SET_2) || (attn_sig[3] & HW_PRTY_ASSERT_SET_3))
    {
        /* Parity Error... Assuming we only enable parities we can deal with
         * this is a recoverable error...
         */
        debug_info = &((pdev)->debug_info.er_debug_info[pdev->debug_info.curr_er_debug_idx]);
        for (i = 0; i < arr_size; i++)
        {
            debug_info->attn_sig[i] = attn_sig[i];
        }
        lm_inc_er_debug_idx(pdev);

        /* TODO: maybe get GRCDump here in the future... */
        DbgMessage5(pdev, FATAL, "lm_recoverable_error: funcid:%d, 0:0x%x, 0:0x%x, 0:0x%x, 0:0x%x\n",
                   ABS_FUNC_ID(pdev), attn_sig[0], attn_sig[1], attn_sig[2], attn_sig[3]);

        return TRUE;
    }

    /* HW Attentions (other than parity ) */
    if (attn_sig[1] & HW_INTERRUT_ASSERT_SET_1)
    {
        /* QM Interrupt is recoverable */
        if (attn_sig[1] & AEU_INPUTS_ATTN_BITS_QM_HW_INTERRUPT)
        {
            debug_info = &((pdev)->debug_info.er_debug_info[pdev->debug_info.curr_er_debug_idx]);
            for (i = 0; i < arr_size; i++)
            {
                debug_info->attn_sig[i] = attn_sig[i];
            }
            lm_inc_er_debug_idx(pdev);
            
            DbgMessage5(pdev, FATAL, "lm_recoverable_error: funcid:%d, 0:0x%x, 0:0x%x, 0:0x%x, 0:0x%x\n",
                   ABS_FUNC_ID(pdev), attn_sig[0], attn_sig[1], attn_sig[2], attn_sig[3]);
            return TRUE;
        }
        
    }

    if (attn_sig[3] & EVEREST_GEN_ATTN_IN_USE_MASK)
    {
        if ( GENERAL_ATTEN_OFFSET(ERROR_RECOVERY_ATTENTION_BIT) & attn_sig[3])
        {
            debug_info = &((pdev)->debug_info.er_debug_info[pdev->debug_info.curr_er_debug_idx]);
            for (i = 0; i < arr_size; i++)
            {
                debug_info->attn_sig[i] = attn_sig[i];
            }
            lm_inc_er_debug_idx(pdev);
            
            DbgMessage5(pdev, FATAL, "lm_recoverable_error: funcid:%d, 0:0x%x, 0:0x%x, 0:0x%x, 0:0x%x\n",
                   ABS_FUNC_ID(pdev), attn_sig[0], attn_sig[1], attn_sig[2], attn_sig[3]);
            return TRUE;
        }
    }

    return FALSE;
}

void lm_handle_assertion_processing(lm_device_t *pdev, u16_t assertion_proc_flgs)
{
    u32_t       val           = 0;
    u32_t       port_reg_name = 0;
    u32_t       mask_val      = 0;
    u32_t       nig_mask      = 0;

    DbgMessage1(pdev, INFORM, "lm_handle_assertion_processing: assertion_proc_flgs:%d\n", assertion_proc_flgs);

    //mask only appropriate attention output signals from configured routing and unifier logic toward IGU.
    //This is for driver/chip sync to eventually return to '00' monitored state
    //in both leading & trailing latch.
    //mask non-hard-wired dynamic groups only

    DbgBreakIf(pdev->vars.attn_state & assertion_proc_flgs);

    //mask relevant AEU attn lines
    //             mask  assert_flgs  new mask
    //legal:        0       0       ->    0
    //              1       0       ->    1
    //              1       1       ->    0
    //ASSERT:       0       1 -> this won't change us thanks to & ~

    ASSERT_STATIC( HW_LOCK_RESOURCE_PORT0_ATT_MASK +1 == HW_LOCK_RESOURCE_PORT1_ATT_MASK );
    ASSERT_STATIC( NIG_REG_MASK_INTERRUPT_PORT0 + 4   == NIG_REG_MASK_INTERRUPT_PORT1 );

    lm_hw_lock(pdev, HW_LOCK_RESOURCE_PORT0_ATT_MASK + PORT_ID(pdev), TRUE);
    port_reg_name = PORT_ID(pdev) ? MISC_REG_AEU_MASK_ATTN_FUNC_1 : MISC_REG_AEU_MASK_ATTN_FUNC_0;
    // read the hw current mask value
    mask_val=REG_RD(pdev, port_reg_name);
    //changed rrom XOR to & ~
    pdev->vars.aeu_mask_attn_func = mask_val & 0xff;
    DbgMessage1(pdev, INFORM, "lm_handle_assertion_processing: BEFORE: aeu_mask_attn_func:0x%x\n", pdev->vars.aeu_mask_attn_func);
    //changed rrom XOR to & ~
    pdev->vars.aeu_mask_attn_func &= ~(assertion_proc_flgs & 0xff);
    REG_WR(pdev, port_reg_name, pdev->vars.aeu_mask_attn_func);
    DbgMessage1(pdev, INFORM, "lm_handle_assertion_processing: AFTER : aeu_mask_attn_func:0x%x\n", pdev->vars.aeu_mask_attn_func);
    lm_hw_unlock(pdev, HW_LOCK_RESOURCE_PORT0_ATT_MASK + PORT_ID(pdev));
    //update the bits states

    //        state  assert_flgs  new state
    //legal:    0       0         -> 0
    //          0       1         -> 1
    //          1       0         -> 1
    //error:    1       1 -> this won't change us thanks to |
    DbgMessage1(pdev, INFORM, "lm_handle_assertion_processing: BEFORE: attn_state:0x%x\n", pdev->vars.attn_state);
    //changed from XOR to OR for safety
    pdev->vars.attn_state |= assertion_proc_flgs;

    DbgMessage1(pdev, INFORM, "lm_handle_assertion_processing: AFTER : attn_state:0x%x\n", pdev->vars.attn_state);
    //process only hard-wired lines in case any got up
    if (assertion_proc_flgs & ATTN_HARD_WIRED_MASK)
    {
        lm_hard_wired_processing(pdev, assertion_proc_flgs);
    }

    // now handle nig
    if (assertion_proc_flgs & ATTN_NIG_FOR_FUNC)
    {
        MM_ACQUIRE_PHY_LOCK(pdev);
         // save nig interrupt mask and set it back later
        nig_mask = REG_RD(pdev,  NIG_REG_MASK_INTERRUPT_PORT0 + 4*PORT_ID(pdev));
        REG_WR(pdev,  NIG_REG_MASK_INTERRUPT_PORT0 + 4*PORT_ID(pdev), 0);

        // we'll handle the attention only if mask is not 0
        // if mask is 0, it means that "old" and irrelevant is sent
        // and we should not hnalde it (e.g. CQ48990 - got link down event after loopback mode was set).
        if( nig_mask )
        {
            lm_nig_processing(pdev);
        }
        else
        {
            DbgMessage(pdev, WARN, "lm_handle_deassertion_processing: got attention when nig_mask is 0\n" );
        }
    }

    //parallel write to IGU to set the attn_ack for _all asserted_ lines.
    val = assertion_proc_flgs;

    // attntion bits set
    if (INTR_BLK_TYPE(pdev) == INTR_BLK_HC)
    {
        REG_WR(pdev,  HC_REG_COMMAND_REG + PORT_ID(pdev)*32 + COMMAND_REG_ATTN_BITS_SET,val);
    }
    else
    {
        u32_t cmd_addr = IGU_CMD_ATTN_BIT_SET_UPPER;
        if (INTR_BLK_ACCESS(pdev) == INTR_BLK_ACCESS_IGUMEM)
        {
            REG_WR(pdev, BAR_IGU_INTMEM + cmd_addr*8, val);
        }
        else
        {
            struct igu_ctrl_reg cmd_ctrl;
            u8_t                igu_func_id = 0;
            /* GRC ACCESS: */
            /* Write the Data, then the control */
             /* [18:12] - FID (if VF - [18] = 0; [17:12] = VF number; if PF - [18] = 1; [17:14] = 0; [13:12] = PF number) */
            igu_func_id = IGU_FUNC_ID(pdev);
            cmd_ctrl.ctrl_data =
                ((cmd_addr << IGU_CTRL_REG_ADDRESS_SHIFT) |
                 (igu_func_id << IGU_CTRL_REG_FID_SHIFT) |
                 (IGU_CTRL_CMD_TYPE_WR << IGU_CTRL_REG_TYPE_SHIFT));

            REG_WR(pdev, IGU_REG_COMMAND_REG_32LSB_DATA, val);
            REG_WR(pdev, IGU_REG_COMMAND_REG_CTRL, cmd_ctrl.ctrl_data);
        }
    }

    // now set back the mask
    if (assertion_proc_flgs & ATTN_NIG_FOR_FUNC)
    {
        REG_WR(pdev,  NIG_REG_MASK_INTERRUPT_PORT0 + 4*PORT_ID(pdev), nig_mask);
        MM_RELEASE_PHY_LOCK(pdev);
    }
}

void lm_read_attn_regs(lm_device_t *pdev, u32_t * attn_sig_af_inv_arr, u32_t arr_size)
{
    u8_t i;
    DbgBreakIf( pdev->vars.num_attn_sig_regs > arr_size );
    DbgBreakIf( pdev->vars.num_attn_sig_regs > ARRSIZE(pdev->vars.attn_sig_af_inv_reg_addr) );

    //Read the 128 attn signals bits after inverter
    for (i = 0; i < pdev->vars.num_attn_sig_regs; i++)
    {
        attn_sig_af_inv_arr[i] = REG_RD(pdev, pdev->vars.attn_sig_af_inv_reg_addr[i]);
    }

    DbgMessage5(pdev, INFORM, "lm_handle_deassertion_processing: attn_sig_aft_invert_1:0x%x; attn_sig_aft_invert_2:0x%x; attn_sig_aft_invert_3:0x%x; attn_sig_aft_invert_4:0x%x,attn_sig_aft_invert_5:0x%x\n",
                attn_sig_af_inv_arr[0],
                attn_sig_af_inv_arr[1],
                attn_sig_af_inv_arr[2],
                attn_sig_af_inv_arr[3],
                attn_sig_af_inv_arr[4]);
}

void lm_handle_deassertion_processing(lm_device_t *pdev, u16_t deassertion_proc_flgs)
{
    lm_status_t lm_status                     = LM_STATUS_SUCCESS;
    u32_t  val                                = 0;
    u32_t  port_reg_name                      = 0;
    u8_t   index                              = 0;
    u8_t   i                                  = 0;
    u32_t  mask_val                           = 0;
    u32_t  attn_sig_af_inv_arr[MAX_ATTN_REGS] = {0};
    u32_t  group_mask_arr[MAX_ATTN_REGS]      = {0};
    u32_t  mask_arr_val[MAX_ATTN_REGS]        = {0};

    DbgBreakIf(!pdev);
    DbgMessage1(pdev, INFORM, "lm_handle_deassertion_processing: deassertion_proc_flgs:%d\n", deassertion_proc_flgs);


    //acquire split lock for attention signals handling
    acquire_split_alr(pdev);

    lm_read_attn_regs(pdev, attn_sig_af_inv_arr, ARRSIZE(attn_sig_af_inv_arr));

    if (lm_recoverable_error(pdev, attn_sig_af_inv_arr,ARRSIZE(attn_sig_af_inv_arr)))
    {
        DbgMessage(pdev, WARNer, "Starting lm recover flow ");
        lm_status = mm_er_initiate_recovery(pdev);
        if (lm_status == LM_STATUS_SUCCESS)
        {
            /* Continue only on success... */
            /* Disable HW interrupts */
            lm_disable_int(pdev);

            release_split_alr(pdev);
            /* In case of recoverable error don't handle attention so that
            * other functions get this parity as well.
            */
            return;
        }
        DbgMessage1(pdev, WARNer, "mm_er_initiate_recovery returned status %d ", lm_status);

        /* Recovery failed... we'll keep going, and eventually hit
         * the attnetion and assert...
         */
    }

    //For all deasserted groups, pass over entire attn_bits after inverter and if they
    // are members of that particular gruop, treat each one of them accordingly.
    for (index = 0; index < ARRSIZE(pdev->vars.attn_groups_output); index++)
    {
        if (deassertion_proc_flgs & (1 << index))
        {
            for (i = 0; i < ARRSIZE(group_mask_arr); i++)
            {
                group_mask_arr[i] = pdev->vars.attn_groups_output[index].attn_sig_dword[i];
            }

            DbgMessage1(pdev, WARN, "lm_handle_deassertion_processing: group #%d got attention on it!\n", index);
            DbgMessage5(pdev, WARN, "lm_handle_deassertion_processing: mask1:0x%x, mask2:0x%x, mask3:0x%x, mask4:0x%x,mask5:0x%x\n",
                       group_mask_arr[0],
                       group_mask_arr[1],
                       group_mask_arr[2],
                       group_mask_arr[3],
                       group_mask_arr[4]);
            DbgMessage5(pdev, WARN, "lm_handle_deassertion_processing: attn1:0x%x, attn2:0x%x, attn3:0x%x, attn4:0x%x,attn5:0x%x\n",
                       attn_sig_af_inv_arr[0],
                       attn_sig_af_inv_arr[1],
                       attn_sig_af_inv_arr[2],
                       attn_sig_af_inv_arr[3],
                       attn_sig_af_inv_arr[4]);

            if (attn_sig_af_inv_arr[3] & EVEREST_GEN_ATTN_IN_USE_MASK & group_mask_arr[3])
            {
                lm_gen_attn_everest_processing(pdev, attn_sig_af_inv_arr[3]);
            }

            // DQ attn
            if (attn_sig_af_inv_arr[1] & AEU_INPUTS_ATTN_BITS_DOORBELLQ_HW_INTERRUPT & group_mask_arr[1])
            {
                lm_dq_attn_everest_processing(pdev);
            }
            // CFC attn
            if (attn_sig_af_inv_arr[2] & AEU_INPUTS_ATTN_BITS_CFC_HW_INTERRUPT & group_mask_arr[2])
            {
                lm_cfc_attn_everest_processing(pdev);
            }
            // PXP attn
            if (attn_sig_af_inv_arr[2] & AEU_INPUTS_ATTN_BITS_PXP_HW_INTERRUPT & group_mask_arr[2])
            {
                lm_pxp_attn_everest_processing(pdev);
            }
            // SPIO 5 bit in register 0
            if (attn_sig_af_inv_arr[0] & AEU_INPUTS_ATTN_BITS_SPIO5 & group_mask_arr[0])
            {
                lm_spio5_attn_everest_processing(pdev);
            }

            // GPIO3 bits in register 0
            if (attn_sig_af_inv_arr[0] & pdev->vars.link.aeu_int_mask & group_mask_arr[0])
            {
                // Handle it only for PMF
                if (IS_PMF(pdev)) {
			MM_ACQUIRE_PHY_LOCK(pdev);
			PHY_HW_LOCK(pdev);
			elink_handle_module_detect_int(&pdev->params.link);
			PHY_HW_UNLOCK(pdev);
			MM_RELEASE_PHY_LOCK(pdev);
		}
            }

            //TODO: attribute each attention signal arrived and which is a member of a group and give it its own
            // specific treatment. later, for each attn, do "clean the hw block" via the INT_STS_CLR.

            //Check for lattched attn signals
            if (attn_sig_af_inv_arr[3] & EVEREST_LATCHED_ATTN_IN_USE_MASK & group_mask_arr[3])
            {
                lm_latch_attn_everest_processing(pdev, attn_sig_af_inv_arr[3]);
            }

            // general hw block attention
            i = 0;
            mask_arr_val[i] = attn_sig_af_inv_arr[i] & HW_INTERRUT_ASSERT_SET_0 & group_mask_arr[i];
            i = 1;
            mask_arr_val[i] = attn_sig_af_inv_arr[i] & HW_INTERRUT_ASSERT_SET_1 & group_mask_arr[i];
            i = 2;
            mask_arr_val[i] = attn_sig_af_inv_arr[i] & HW_INTERRUT_ASSERT_SET_2 & group_mask_arr[i];
            i = 4;
            mask_arr_val[i] = attn_sig_af_inv_arr[i] & HW_INTERRUT_ASSERT_SET_4 & group_mask_arr[i];

            if ( (mask_arr_val[0]) ||
                 (mask_arr_val[1]) ||
                 (mask_arr_val[2]) ||
                 (mask_arr_val[4]) )
            {
                DbgMessage(pdev,FATAL,"hw block attention:\n");
                DbgMessage1(pdev,FATAL,"0: 0x%08x\n", mask_arr_val[0]);
                DbgMessage1(pdev,FATAL,"1: 0x%08x\n", mask_arr_val[1]);
                DbgMessage1(pdev,FATAL,"2: 0x%08x\n", mask_arr_val[2]);
                DbgMessage1(pdev,FATAL,"4: 0x%08x\n", mask_arr_val[4]);
                DbgBreakIfAll(1);
            }
            // general hw block mem prty
            i = 0;
            mask_arr_val[i] = attn_sig_af_inv_arr[i] & HW_PRTY_ASSERT_SET_0 & group_mask_arr[i];
            i = 1;
            mask_arr_val[i] = attn_sig_af_inv_arr[i] & HW_PRTY_ASSERT_SET_1 & group_mask_arr[i];
            i = 2;
            mask_arr_val[i] = attn_sig_af_inv_arr[i] & HW_PRTY_ASSERT_SET_2 & group_mask_arr[i];
            i = 4;
            mask_arr_val[i] = attn_sig_af_inv_arr[i] & HW_PRTY_ASSERT_SET_4 & group_mask_arr[i];

            if ( (mask_arr_val[0]) ||
                 (mask_arr_val[1]) ||
                 (mask_arr_val[2]) ||
                 (mask_arr_val[4]) )
            {
                DbgMessage(pdev,FATAL,"hw block parity attention\n");
                DbgMessage1(pdev,FATAL,"0: 0x%08x\n", mask_arr_val[0]);
                DbgMessage1(pdev,FATAL,"1: 0x%08x\n", mask_arr_val[1]);
                DbgMessage1(pdev,FATAL,"2: 0x%08x\n", mask_arr_val[2]);
                DbgMessage1(pdev,FATAL,"4: 0x%08x\n", mask_arr_val[4]);
                DbgBreakIfAll(1);
            }
        }
    }

    //release split lock
    release_split_alr(pdev);

    //TODO: the attn_ack bits to clear must be passed with '0'
    //val = deassertion_proc_flgs;
    val = ~deassertion_proc_flgs;
    // attntion bits clear
    if (INTR_BLK_TYPE(pdev) == INTR_BLK_HC)
    {
        REG_WR(pdev,  HC_REG_COMMAND_REG + PORT_ID(pdev)*32 + COMMAND_REG_ATTN_BITS_CLR,val);
    }
    else
    {
        u32_t cmd_addr = IGU_CMD_ATTN_BIT_CLR_UPPER;

        if (INTR_BLK_ACCESS(pdev) == INTR_BLK_ACCESS_IGUMEM)
        {
            REG_WR(pdev, BAR_IGU_INTMEM + cmd_addr*8, val);
        }
        else
        {
            struct igu_ctrl_reg cmd_ctrl;
            u8_t igu_func_id = 0;

            /* GRC ACCESS: */
            /* Write the Data, then the control */
             /* [18:12] - FID (if VF - [18] = 0; [17:12] = VF number; if PF - [18] = 1; [17:14] = 0; [13:12] = PF number) */
            igu_func_id = IGU_FUNC_ID(pdev);
            cmd_ctrl.ctrl_data =
                ((cmd_addr << IGU_CTRL_REG_ADDRESS_SHIFT) |
                 (igu_func_id << IGU_CTRL_REG_FID_SHIFT) |
                 (IGU_CTRL_CMD_TYPE_WR << IGU_CTRL_REG_TYPE_SHIFT));

            REG_WR(pdev, IGU_REG_COMMAND_REG_32LSB_DATA, val);
            REG_WR(pdev, IGU_REG_COMMAND_REG_CTRL, cmd_ctrl.ctrl_data);
        }
    }

    //unmask only appropriate attention output signals from configured routing and unifier logic toward IGU.
    //This is for driver/chip sync to eventually return to '00' monitored state
    //in both leading & trailing latch.
    //unmask non-hard-wired dynamic groups only

    DbgBreakIf(~pdev->vars.attn_state & deassertion_proc_flgs);

    //unmask relevant AEU attn lines
    //             mask  deassert_flgs  new mask
    //legal:        0       0       ->    0
    //              0       1       ->    1
    //              1       0       ->    1
    //ASSERT:       1       1 -> this won't change us thanks to the |

    port_reg_name = PORT_ID(pdev) ? MISC_REG_AEU_MASK_ATTN_FUNC_1 : MISC_REG_AEU_MASK_ATTN_FUNC_0;

    lm_hw_lock(pdev, HW_LOCK_RESOURCE_PORT0_ATT_MASK + PORT_ID(pdev), TRUE);

    mask_val = REG_RD(pdev, port_reg_name);

    pdev->vars.aeu_mask_attn_func = mask_val & 0xff;

    DbgMessage1(pdev, INFORM, "lm_handle_deassertion_processing: BEFORE: aeu_mask_attn_func:0x%x\n", pdev->vars.aeu_mask_attn_func);
    //changed from XOR to OR for safely
    pdev->vars.aeu_mask_attn_func |= (deassertion_proc_flgs & 0xff);

    DbgMessage1(pdev, INFORM, "lm_handle_deassertion_processing: AFTER : aeu_mask_attn_func:0x%x\n", pdev->vars.aeu_mask_attn_func);

    REG_WR(pdev, port_reg_name, pdev->vars.aeu_mask_attn_func);
    lm_hw_unlock(pdev, HW_LOCK_RESOURCE_PORT0_ATT_MASK + PORT_ID(pdev));
    //update the attn bits states
    //            state  deassert_flgs  new state
    //legal:        0       0       ->    0
    //              1       0       ->    1
    //              1       1       ->    0
    //ASSERT:       0       1 -> this won't change our state thanks to & ~ !
    DbgMessage1(pdev, INFORM, "lm_handle_deassertion_processing: BEFORE: attn_state:0x%x\n", pdev->vars.attn_state);

    //changed from XOR to : AND ~ for safety
    pdev->vars.attn_state &= ~deassertion_proc_flgs;

    DbgMessage1(pdev, INFORM, "lm_handle_deassertion_processing: AFTER : attn_state:0x%x\n", pdev->vars.attn_state);
}

void lm_get_attn_info(lm_device_t *pdev, u16_t *attn_bits, u16_t *attn_ack)
{
    volatile struct atten_sp_status_block *       attention_sb = NULL;
    u16_t                                   lcl_attn_sb_index = 0;

    DbgBreakIf(!(pdev && attn_bits && attn_ack));

    attention_sb = lm_get_attention_status_block(pdev);

    //guard against dynamic change of attn lines - 15 interations max
    //the main idea here is to assure that we work on synchronized snapshots of the attn_bits and
    //attn_ack and avoid a faulty scenario where attn_ack we read in sanpshot #2 corresponds to attn_bits
    //of snapshot #1 which occured on different time frames.
    do
    {
        lcl_attn_sb_index = mm_le16_to_cpu(attention_sb->attn_bits_index);
        *attn_bits = (u16_t)mm_le32_to_cpu(attention_sb->attn_bits);
        *attn_ack  = (u16_t)mm_le32_to_cpu(attention_sb->attn_bits_ack);

    } while (lcl_attn_sb_index != mm_le16_to_cpu(attention_sb->attn_bits_index));
    //the lcl_attn_sb_index differs from the real local attn_index in the pdev since in this while loop it could
    //have been changed, we don't save it locally, and thus we will definitely receive an interrupt in case the
    //while condition is met.

    DbgMessage4(pdev, INFORMi, "lm_get_attn_info: def_sb->attn_bits:0x%x, def_sb->attn_ack:0x%x, attn_bits:0x%x, attn_ack:0x%x\n",
               mm_le32_to_cpu(attention_sb->attn_bits),
               mm_le32_to_cpu(attention_sb->attn_bits_ack),
               *attn_bits,
               *attn_ack);
}

void lm_update_def_hc_indices(lm_device_t *pdev, u8_t dummy_sb_id, u32_t *activity_flg)
{
    volatile struct hc_sp_status_block * sp_sb          = NULL;
    volatile struct atten_sp_status_block * attn_sb = NULL;
    u16_t                             atomic_index   = 0;

    *activity_flg = 0;

    DbgBreakIf(!pdev);


    //It's a default status block

        DbgMessage2(pdev, INFORMi, "BEFORE update: hc_def_ack:%d, attn_def_ack:%d\n",
            pdev->vars.hc_def_ack,
            pdev->vars.attn_def_ack);

    sp_sb = lm_get_default_status_block(pdev);

    atomic_index = mm_le16_to_cpu(sp_sb->running_index);
    if (atomic_index != pdev->vars.hc_def_ack)
    {
        pdev->vars.hc_def_ack = atomic_index;
        (*activity_flg) |= LM_SP_ACTIVE;
    }


    attn_sb = lm_get_attention_status_block(pdev);

    atomic_index = mm_le16_to_cpu(attn_sb->attn_bits_index);
    if (atomic_index != pdev->vars.attn_def_ack)
    {
        pdev->vars.attn_def_ack = atomic_index;
        (*activity_flg) |= LM_DEF_ATTN_ACTIVE;
    }

    DbgMessage2(pdev, INFORMi, "AFTER update: hc_def_ack:%d, attn_def_ack:%d\n",
        pdev->vars.hc_def_ack,
        pdev->vars.attn_def_ack);
}

void lm_update_fp_hc_indices(lm_device_t *pdev, u8_t igu_sb_id, u32_t *activity_flg, u8_t *drv_rss_id)
{
    u16_t                                   atomic_index   = 0;
    u8_t flags;
    u8_t drv_sb_id;

    *activity_flg = 0;
    drv_sb_id = igu_sb_id;

    DbgBreakIf(!(pdev && (drv_sb_id <= ARRSIZE(pdev->vars.status_blocks_arr))));
    DbgMessage1(pdev, INFORMi, "lm_update_hc_indices: inside with sb_idx:%d\n", drv_sb_id);

    DbgBreakIf(!LM_SB_ID_VALID(pdev, drv_sb_id));


    flags = lm_query_storm_intr(pdev, igu_sb_id, &drv_sb_id);

    DbgMessage1(pdev, INFORMi, "BEFORE update: c_hc_ack:%d\n", pdev->vars.c_hc_ack[drv_sb_id]);
    DbgMessage1(pdev, INFORMi, "BEFORE update: u_hc_ack:%d\n", pdev->vars.u_hc_ack[drv_sb_id]);

    if (GET_FLAGS(flags, CSTORM_INTR_FLAG)) {
        atomic_index = lm_get_sb_running_index(pdev, drv_sb_id, SM_TX_ID);

        if (atomic_index != pdev->vars.c_hc_ack[drv_sb_id])
        {
            pdev->vars.c_hc_ack[drv_sb_id] = atomic_index;
            (*activity_flg) |= LM_NON_DEF_CSTORM_ACTIVE;
        }
    }

    if (GET_FLAGS(flags, USTORM_INTR_FLAG)) {
        atomic_index = lm_get_sb_running_index(pdev, drv_sb_id, SM_RX_ID);

        if (atomic_index != pdev->vars.u_hc_ack[drv_sb_id])
        {
            pdev->vars.u_hc_ack[drv_sb_id] = atomic_index;
            (*activity_flg) |= LM_NON_DEF_USTORM_ACTIVE;
            if ((pdev->params.ndsb_type == LM_SINGLE_SM) || (pdev->params.ndsb_type == LM_DOUBLE_SM_SINGLE_IGU)) {
                (*activity_flg) |= LM_NON_DEF_CSTORM_ACTIVE;
            }
        }
    }


    DbgMessage1(pdev, INFORMi, "AFTER update: c_hc_ack:%d\n", pdev->vars.c_hc_ack[drv_sb_id]);
    DbgMessage1(pdev, INFORMi, "AFTER update: u_hc_ack:%d\n", pdev->vars.u_hc_ack[drv_sb_id]);

    /* Fixme - doesn't have to be... */
    *drv_rss_id = drv_sb_id;
}

u8_t lm_is_rx_completion(lm_device_t *pdev, u8_t chain_idx)
{
    u8_t result               = FALSE;
    lm_rcq_chain_t *rcq_chain = &LM_RCQ(pdev, chain_idx);

    DbgBreakIf(!(pdev && rcq_chain));

    //the hw_con_idx_ptr of the rcq_chain points directly to the Rx index in the USTORM part of the non-default status block
    if (rcq_chain->hw_con_idx_ptr &&
        (mm_le16_to_cpu(*rcq_chain->hw_con_idx_ptr) !=
        lm_bd_chain_cons_idx(&rcq_chain->bd_chain)))
    {
        result = TRUE;
    }
    DbgMessage1(pdev, INFORMi, "lm_is_rx_completion: result is:%s\n", result? "TRUE" : "FALSE");

    return result;
}

u8_t lm_is_tx_completion(lm_device_t *pdev, u8_t chain_idx)
{
    u8_t result             = FALSE;
    lm_tx_chain_t *tx_chain = &LM_TXQ(pdev, chain_idx);

    DbgBreakIf(!(pdev && tx_chain));

    //the hw_con_idx_ptr of the rcq_chain points directly to the Rx index in the USTORM part of the non-default status block
    //changed from *tx_chain->hw_con_idx_ptr != tx_chain->cons_idx
    if ( tx_chain->hw_con_idx_ptr && (mm_le16_to_cpu(*tx_chain->hw_con_idx_ptr) != tx_chain->pkt_idx))
    {
        result = TRUE;
    }
    DbgMessage1(pdev, INFORMi, "lm_is_tx_completion: result is:%s\n", result? "TRUE" : "FALSE");

    return result;
}

u8_t lm_is_eq_completion(lm_device_t *pdev)
{
    lm_eq_chain_t * eq_chain = NULL;
    u8_t result                                    = FALSE;

    DbgBreakIf(!pdev);
    if (!pdev || IS_VFDEV(pdev))
    {
        return FALSE;
    }

    eq_chain = &pdev->eq_info.eq_chain;
    if ( eq_chain->hw_con_idx_ptr && (mm_le16_to_cpu(*eq_chain->hw_con_idx_ptr) != lm_bd_chain_cons_idx(&eq_chain->bd_chain)))
    {
        result = TRUE;
    }

    DbgMessage1(pdev, INFORMeq, "lm_is_eq_completion: result is:%s\n", result? "TRUE" : "FALSE");

    return result;
}

u8_t lm_is_def_sb_updated(lm_device_t *pdev)
{
    volatile struct hc_sp_status_block * sp_sb                = NULL;
    volatile struct atten_sp_status_block * attn_sb           = NULL;
    u8_t result                                            = FALSE;
    u16_t hw_sb_idx                                        = 0;

    DbgBreakIfFastPath(!pdev);
    if (!pdev || IS_VFDEV(pdev))
    {
        return FALSE;
    }

    DbgMessage(pdev, INFORMi, "lm_is_def_sb_updated() inside!\n");

    sp_sb = lm_get_default_status_block(pdev);
    //it is legit that only a subgroup of the storms may change between our local copy.
    //at least one storm index change implies that we have work to do on this sb
    hw_sb_idx = mm_le16_to_cpu(sp_sb->running_index);
    if (hw_sb_idx != pdev->vars.hc_def_ack)
    {
        DbgMessage2(pdev, INFORMi, "lm_is_sb_updated: sp running_index:%d, hc_def_ack:%d\n",
                    hw_sb_idx, pdev->vars.hc_def_ack);

        result     = TRUE;
    }

    attn_sb = lm_get_attention_status_block(pdev);
    hw_sb_idx = mm_le16_to_cpu(attn_sb->attn_bits_index);
    if (hw_sb_idx != pdev->vars.attn_def_ack)
    {
        DbgMessage2(pdev, INFORMi, "lm_is_sb_updated: def.attn_bits_index:%d attn_def_ack:%d\n",
                    hw_sb_idx, pdev->vars.attn_def_ack);

        result = TRUE;
    }

    DbgMessage1(pdev, INFORMi, "lm_is_def_sb_updated:  result:%s\n", result? "TRUE" : "FALSE");

    return result;
}




u8_t lm_handle_igu_sb_id(lm_device_t *pdev, u8_t igu_sb_id, u8_t *rx_rss_id, u8_t *tx_rss_id)
{
    u16_t atomic_index = 0;
    u8_t  drv_sb_id = 0;
    u8_t  flags = 0;
    u8_t  drv_rss_id = 0;

    drv_sb_id = igu_sb_id;

    if ((INTR_BLK_TYPE(pdev) == INTR_BLK_HC) || (IGU_U_NDSB_OFFSET(pdev) == 0)) {
        /* One Segment Per u/c */
        SET_FLAGS(flags, USTORM_INTR_FLAG);
        SET_FLAGS(flags, CSTORM_INTR_FLAG);
    } else {
        if (drv_sb_id >= IGU_U_NDSB_OFFSET(pdev)) {
            drv_sb_id -= IGU_U_NDSB_OFFSET(pdev);
            SET_FLAGS(flags, USTORM_INTR_FLAG);
            //DbgMessage1(pdev, FATAL, "Ustorm drv_sb_id=%d\n", drv_sb_id);
        } else {
            SET_FLAGS(flags, CSTORM_INTR_FLAG);
            //DbgMessage1(pdev, FATAL, "Cstorm drv_sb_id=%d\n", drv_sb_id);
        }
    }

    if (GET_FLAGS(flags, USTORM_INTR_FLAG)) {
        atomic_index = lm_get_sb_running_index(pdev, drv_sb_id, SM_RX_ID);

        if (atomic_index != pdev->vars.u_hc_ack[drv_sb_id]) {
            pdev->vars.u_hc_ack[drv_sb_id] = atomic_index;
        }

        drv_rss_id = drv_sb_id; /* FIXME: doesn't have to be... */
        //Check for Rx completions
        if (lm_is_rx_completion(pdev, drv_rss_id))
        {
            //DbgMessage1(pdev, FATAL, "RX_completion=%d\n", drv_rss_id);
            SET_FLAGS(flags, SERV_RX_INTR_FLAG);
        }

#ifdef INCLUDE_L4_SUPPORT
        //Check for L4 Rx completions
        if (lm_toe_is_rx_completion(pdev, drv_rss_id))
        {
            lm_toe_service_rx_intr(pdev, drv_rss_id);
        }
#endif
    }
    if (GET_FLAGS(flags, CSTORM_INTR_FLAG)) {
        if (IGU_U_NDSB_OFFSET(pdev)) {
            atomic_index = lm_get_sb_running_index(pdev, drv_sb_id, SM_TX_ID);

            if (atomic_index != pdev->vars.c_hc_ack[drv_sb_id]) {
                pdev->vars.c_hc_ack[drv_sb_id] = atomic_index;
            }
        }
        drv_rss_id = drv_sb_id; /* FIXME: doesn't have to be... */
        //Check for Tx completions
        if (lm_is_tx_completion(pdev, drv_rss_id))
        {
            //DbgMessage1(pdev, FATAL, "TX_completion=%d\n", drv_rss_id);
            SET_FLAGS(flags, SERV_TX_INTR_FLAG);
        }


#ifdef INCLUDE_L4_SUPPORT
        //Check for L4 Tx completions
        if (lm_toe_is_tx_completion(pdev, drv_rss_id))
        {
            lm_toe_service_tx_intr(pdev, drv_rss_id);
        }
#endif
    }
    *rx_rss_id = drv_rss_id;
    *tx_rss_id = drv_rss_id;

    return flags;
}




void init_port_part(struct _lm_device_t *pdev)
{
    u32_t val = 0;

    /* Probe phys on board - must happen before lm_reset_link*/
    elink_phy_probe(&pdev->params.link);

    MM_ACQUIRE_PHY_LOCK(pdev);
    lm_reset_link(pdev);
    MM_RELEASE_PHY_LOCK(pdev);

    REG_WR(pdev,(PORT_ID(pdev) ? NIG_REG_MASK_INTERRUPT_PORT1 : NIG_REG_MASK_INTERRUPT_PORT0), 0);
    init_misc_port(pdev);
    init_pxp_port(pdev);
    init_pxp2_port(pdev);
    init_pglue_b_port(pdev);
    init_atc_port(pdev);
    init_tcm_port( pdev);
    init_ucm_port( pdev);
    init_ccm_port( pdev);
    init_xcm_port( pdev);
    init_qm_port ( pdev);
    init_tm_port ( pdev);
    init_dq_port ( pdev);
    init_brb_port( pdev);
    init_prs_port( pdev);
    init_tsdm_port( pdev);
    init_csdm_port( pdev);
    init_usdm_port( pdev);
    init_xsdm_port( pdev);

    init_semi_port(pdev);
    init_upb_port(pdev);
    init_xpb_port(pdev);
    init_pbf_port( pdev );
    init_src_port(pdev);
    init_cdu_port(pdev);
    init_cfc_port(pdev);
    init_hc_port( pdev);
    init_igu_port( pdev);
    init_aeu_port( pdev);
    init_pxpcs_port(pdev);
    init_emac0_port(pdev);
    init_emac1_port(pdev);
    init_dbu_port(pdev);
    init_dbg_port(pdev);

    init_nig_port( pdev);
    init_mcp_port(pdev);
    init_dmae_port(pdev);


    MM_ACQUIRE_PHY_LOCK(pdev);
    lm_stats_init_port_part(pdev);
    elink_init_mod_abs_int(pdev, &pdev->vars.link, CHIP_ID(pdev), pdev->hw_info.shmem_base, pdev->hw_info.shmem_base2, PORT_ID(pdev));
    MM_RELEASE_PHY_LOCK(pdev);


    // iSCSI FW expect bit 28 to be set
    if (!GET_FLAGS( pdev->params.test_mode, TEST_MODE_NO_MCP)) {
        LM_SHMEM_READ(pdev,  OFFSETOF(shmem_region_t,dev_info.port_feature_config[PORT_ID(pdev)].config), &val );
        SET_FLAGS(val, (1 << 28)) ;
        LM_SHMEM_WRITE(pdev, OFFSETOF(shmem_region_t,dev_info.port_feature_config[PORT_ID(pdev)].config), val );
    }
    // Clear the shared port bit of the DCBX completion
    lm_dcbx_set_comp_recv_on_port_bit(pdev, FALSE);
}

void init_function_part(struct _lm_device_t *pdev)
{
    const u8_t func = FUNC_ID(pdev);

    DbgMessage1(pdev, INFORMi, "init_function_part, func=%d\n", func);

    init_pxp_func(pdev);
    init_pxp2_func( pdev );
    init_pglue_b_func(pdev);
    init_atc_func(pdev);
    init_misc_func(pdev);
    init_tcm_func(pdev);
    init_ucm_func(pdev);
    init_ccm_func(pdev);
    init_xcm_func(pdev);
    init_semi_func(pdev);
    init_qm_func(pdev);
    init_tm_func(pdev);
    init_dq_func(pdev);
    init_brb_func(pdev);
    init_prs_func(pdev);
    init_tsdm_func(pdev);
    init_csdm_func(pdev);
    init_usdm_func(pdev);
    init_xsdm_func(pdev);
    init_upb_func(pdev);
    init_xpb_func(pdev);

    init_pbf_func(pdev);
    init_src_func(pdev);
    init_cdu_func(pdev);
    init_cfc_func(pdev);
    init_hc_func(pdev);
    init_igu_func(pdev);
    init_aeu_func(pdev);
    init_pxpcs_func(pdev);
    init_emac0_func(pdev);
    init_emac1_func(pdev);
    init_dbu_func(pdev);
    init_dbg_func(pdev);
    init_nig_func( pdev);
    init_mcp_func(pdev);
    init_dmae_func(pdev);


    /* Probe phys on board */
    elink_phy_probe(&pdev->params.link);
    if (IS_PMF(pdev) && IS_MULTI_VNIC(pdev))
    {
        DbgMessage1(pdev, WARN, "init_function_part: Func %d is the PMF\n", func );
    }

    MM_ACQUIRE_PHY_LOCK(pdev);
    lm_stats_init_func_part(pdev);
    MM_RELEASE_PHY_LOCK(pdev);
}

lm_status_t lm_insert_ramrod_req(lm_device_t *pdev,
                                 u8_t         request_type,
                                 u8_t         to_indicate,
                                 u32_t        cid,
                                 u8_t         ramrod_cmd,
                                 u8_t         ramrod_priority,
                                 u16_t        con_type,
                                 u64_t        data_addr)
{
    lm_request_sp *new_req = NULL;
    lm_status_t    lm_status = LM_STATUS_SUCCESS ;

    MM_ACQUIRE_REQUEST_LOCK(pdev);

    DbgMessage5(pdev, VERBOSEl2sp, "lm_insert_ramrod_req[%d]: is_req_pending:%d;req_outstanding:%d,ins_idx:%d;ext idx:%d\n",cid,
               pdev->client_info[cid].ramrod_info.is_req_pending,
               pdev->client_info[cid].ramrod_info.num_req_outstanding,
               pdev->client_info[cid].ramrod_info.insert_idx,
               pdev->client_info[cid].ramrod_info.extract_idx);

/*
  TODO RAMRODEMU remove the DBG_BREAK_ON when ramrod emulation is in place!
*/
    DbgBreakIf((!DBG_BREAK_ON(UNDER_TEST))&&(pdev->client_info[cid].ramrod_info.num_req_outstanding >= MAX_RAMRODS_OUTSTANDING));

    new_req                                                 = &pdev->client_info[cid].ramrod_info.ramrod_reqs[pdev->client_info[cid].ramrod_info.insert_idx];
    new_req->req_type                                       = request_type;
    new_req->ok_to_indicate                                 = to_indicate;
    new_req->ramrod_priority                                = ramrod_priority;
    lm_sq_post_fill_entry(pdev,&(new_req->sp_list_command),VF_TO_PF_CID(pdev,cid),ramrod_cmd,con_type,data_addr,FALSE);

    pdev->client_info[cid].ramrod_info.insert_idx = (pdev->client_info[cid].ramrod_info.insert_idx + 1) % MAX_RAMRODS_OUTSTANDING;

    pdev->client_info[cid].ramrod_info.num_req_outstanding++;

    if (!pdev->client_info[cid].ramrod_info.is_req_pending)
    {
        pdev->client_info[cid].ramrod_info.is_req_pending = TRUE;
        DbgMessage(pdev, VERBOSEl2sp, "lm_insert_ramrod_req: posting ramrod\n");
        lm_status = lm_sq_post_entry(pdev,&(new_req->sp_list_command),new_req->ramrod_priority);
    } else {
        DbgMessage(pdev, VERBOSEl2sp, "lm_insert_ramrod_req: pending ramrod\n");
    }

    MM_RELEASE_REQUEST_LOCK(pdev);

    return lm_status ;
}

lm_status_t lm_extract_ramrod_req(lm_device_t *pdev, u32_t cid)
{
    lm_request_sp* comp_req        = NULL;
    lm_request_sp* pend_req        = NULL;
    u8_t           request_type    = 0;
    u8_t           to_indicate     = 0;
    u8_t           ramrod_cmd      = 0;
    u8_t           ramrod_priority = 0;
    u16_t          con_type        = 0;
    lm_status_t    lm_status       = LM_STATUS_SUCCESS ;
#ifdef __LINUX
    MM_ACQUIRE_REQUEST_LOCK_DPC(pdev);
#else
    MM_ACQUIRE_REQUEST_LOCK(pdev);
#endif

    cid = PF_TO_VF_CID(pdev,cid);

    DbgMessage1(pdev, VERBOSEl2sp, "lm_extract_ramrod_req: inside! cid:%d\n",cid);
    DbgMessage4(pdev, VERBOSEl2sp, "lm_extract_ramrod_req: is_req_pending:%d;req_outstanding:%d,ins_idx:%d;ext idx:%d\n",
               pdev->client_info[cid].ramrod_info.is_req_pending,
               pdev->client_info[cid].ramrod_info.num_req_outstanding,
               pdev->client_info[cid].ramrod_info.insert_idx,
               pdev->client_info[cid].ramrod_info.extract_idx);

    DbgBreakIf((pdev->client_info[cid].ramrod_info.num_req_outstanding == 0) ||
               (pdev->client_info[cid].ramrod_info.num_req_outstanding > MAX_RAMRODS_OUTSTANDING));

    comp_req      = &pdev->client_info[cid].ramrod_info.ramrod_reqs[pdev->client_info[cid].ramrod_info.extract_idx];
    request_type  = comp_req->req_type;
    to_indicate   = comp_req->ok_to_indicate;
    ramrod_cmd    = (u8_t)((mm_le32_to_cpu(comp_req->sp_list_command.command.hdr.conn_and_cmd_data) >> (u32_t)SPE_HDR_T_CMD_ID_SHIFT )& SPE_HDR_T_CMD_ID);
    ramrod_priority = comp_req->ramrod_priority ;
    con_type      = mm_le16_to_cpu(comp_req->sp_list_command.command.hdr.type);

    DbgMessage4(pdev, VERBOSEl2sp, "lm_extract_ramrod_req: request extracted!req_type:%d;to_indicate:%d,ramrod_cmd:%d ramrod_priority=%d\n",
               request_type,
               to_indicate,
               ramrod_cmd,
               ramrod_priority);

    pdev->client_info[cid].ramrod_info.extract_idx = (pdev->client_info[cid].ramrod_info.extract_idx + 1) % MAX_RAMRODS_OUTSTANDING;

    if (--pdev->client_info[cid].ramrod_info.num_req_outstanding)
    {
        pend_req = &pdev->client_info[cid].ramrod_info.ramrod_reqs[pdev->client_info[cid].ramrod_info.extract_idx];
        DbgMessage(pdev, WARNl2sp, "lm_extract_ramrod_req: posting pending ramrod\n");
        lm_status = lm_sq_post_entry(pdev,&(pend_req->sp_list_command),pend_req->ramrod_priority);
    }
    else
    {
        pdev->client_info[cid].ramrod_info.is_req_pending = FALSE;
    }
#ifdef __LINUX
    MM_RELEASE_REQUEST_LOCK_DPC(pdev);
#else
    MM_RELEASE_REQUEST_LOCK(pdev);
#endif

    if (request_type == REQ_SET_INFORMATION)
    {
        if (to_indicate)
        {
            DbgMessage(pdev, WARNl2sp, "lm_extract_ramrod_req: REQ_SET_INFORMATION - indicating back to NDIS!\n");
            mm_set_done(pdev, cid, NULL); // FIXME
        }
    }
    else if (request_type == REQ_QUERY_INFORMATION)
    {
        //TODO: determine use of this request, completion for query op. probably stats. handle appropriately
        DbgMessage(pdev, VERBOSEl2sp, "lm_extract_ramrod_req: REQ_QUERY_INFORMATION - indicating back to NDIS!\n");
    }
    else
    {
        DbgMessage(pdev,FATAL,"lm_extract_ramrod_req: unknown request type!\n");
    }
    return lm_status;
}

/* The dest MAC address of GVRP is 01-80-C2-00-00-21 */
#define IS_GVRP_ADDR(_addr) \
    (((_addr)[0] == 0x01) && ((_addr)[1] == 0x80) && ((_addr)[2] == 0xC2) && ((_addr)[3] == 0x00) && ((_addr)[4] == 0x00) && ((_addr)[5] == 0x21))


/* The dest MAC address of LACP is 01-80-C2-00-00-02 */
#define IS_LACP_ADDR(_addr) \
    (((_addr)[0] == 0x01) && ((_addr)[1] == 0x80) && ((_addr)[2] == 0xC2) && ((_addr)[3] == 0x00) && ((_addr)[4] == 0x00) && ((_addr)[5] == 0x02))


/*****************************************************************************
 ******************************* Classification: Set  Mac ********************
 *****************************************************************************/

/*The NIG mirror is only used in VMChimney in MF/SI mode.
  In this mode, we assume that the driver in the host OS loads
  first, and allocates offset 0 in the NIG for it's own MAC address,
  so we don't use it. Also, iSCSI has a reserved entry in the NIG, so
  we don't use that either.
  */
#define IS_VALID_NIG_IDX(_idx) ((_idx != 0) && (_idx != ISCSI_OFFSET_IN_NIG))
#define INVALID_NIG_OFFSET ((u8_t)-1)

/**initialize_nig_entry
 * Initialize a NIG mirror entry to a given MAC address. Note -
 * the entrie's reference count remains 0.
 *
 * @param pdev
 * @param offset the index of the NIG entry
 * @param addr the MAC address to use
 *
 * @return lm_status_t LM_STATUS_SUCCESS on success, some other
 *         failure code on failure.
 */
static lm_status_t lm_initialize_nig_entry(
    lm_device_t *pdev,
    u8_t         offset,
    u8_t        *addr)
{
    lm_nig_mirror_entry_t* entry = &pdev->vars.nig_mirror.entries[offset];
    DbgBreakIf(entry->refcnt != 0);
    mm_memcpy(entry->addr, addr, ARRSIZE(entry->addr));
    return LM_STATUS_SUCCESS;
}

/**get_available_nig_entry
 * Find a NIG entry that's not in use. Entry 0 and 15 are never
 * considered available, since they are used by iSCSI and by the
 * L2 client.
 *
 * @param pdev
 *
 * @return an index to a usable NIG entry, or INVALID_NIG_OFFSET
 *         if there aren't any available entries.
 */
static u8_t lm_get_available_nig_entry(lm_device_t *pdev)
{
    u8_t i;
    lm_nig_mirror_t *nig_mirror = &pdev->vars.nig_mirror;

    for (i=0; i<ARRSIZE(nig_mirror->entries); ++i)
    {
        if (IS_VALID_NIG_IDX(i) &&
            (nig_mirror->entries[i].refcnt == 0))
        {
            return i;
        }
    }
    return INVALID_NIG_OFFSET;
}

/**find_nig_entry_for_addr
 * Find the entry for a given MAC address in the nig.
 *
 * @param pdev
 * @param addr the MAC address to look for
 *
 * @return u8_t the index of the NIG entry that contains the
 *         given MAC address, or INVALID_NIG_OFFSET if no such
 *         entry exists.
 */
static u8_t lm_find_nig_entry_for_addr(
    lm_device_t *pdev,
    u8_t        *addr)
{
    u8_t i;
    lm_nig_mirror_t *nig_mirror = &pdev->vars.nig_mirror;
    lm_nig_mirror_entry_t* cur_entry = NULL;

    for (i=0; i<ARRSIZE(nig_mirror->entries); ++i)
    {
        cur_entry = &nig_mirror->entries[i];
        if ( (cur_entry->refcnt > 0) &&
             (mm_memcmp(cur_entry->addr, addr, ARRSIZE(cur_entry->addr))) )
        {
            return i;
        }
    }
    return INVALID_NIG_OFFSET;
}

lm_status_t lm_insert_nig_entry(
    lm_device_t *pdev,
    u8_t        *addr)
{
    u8_t offset = 0;
    lm_status_t lm_status = LM_STATUS_SUCCESS;

    offset = lm_find_nig_entry_for_addr(pdev, addr);

    if (offset == INVALID_NIG_OFFSET)
    {
        /*If there was no entry for this MAC, insert it to an available slot and call lm_set_mac_in_nig.*/
        offset = lm_get_available_nig_entry(pdev);
        if (offset == INVALID_NIG_OFFSET)
        {
            return LM_STATUS_RESOURCE; //no available NIG entry.
        }

        lm_status = lm_initialize_nig_entry(pdev, offset, addr);
        DbgBreakIf (lm_status != LM_STATUS_SUCCESS);

        lm_status = lm_set_mac_in_nig(pdev, addr, LM_CLI_IDX_NDIS, offset);
        if (lm_status != LM_STATUS_SUCCESS)
        {
            return lm_status;
        }
    }

    NIG_ENTRY_INC_REFCNT(&pdev->vars.nig_mirror.entries[offset]);

    return lm_status;
}

/**remove_nig_entry
 * Dereference the entry for a given MAC address. If this was
 * the last reference the MAC address is removed from the NIG.
 *
 * @param pdev
 * @param addr the MAC address
 *
 * @return lm_status_t LM_STATUS_SUCCESS on success,
 *         LM_STATUS_FAILURE if the given MAC is not in the NIG,
 *         other failure codes on other errors.
 */
lm_status_t lm_remove_nig_entry(
    lm_device_t *pdev,
    u8_t        *addr)
{
    u8_t offset = 0;
    lm_status_t lm_status = LM_STATUS_SUCCESS;
    lm_nig_mirror_entry_t* entry = NULL;

    offset = lm_find_nig_entry_for_addr(pdev, addr);
    if (offset == INVALID_NIG_OFFSET)
    {
        DbgBreakIf(offset == INVALID_NIG_OFFSET); //trying to remove an address that isn't in the NIG.
        return LM_STATUS_FAILURE;
    }

    entry = &pdev->vars.nig_mirror.entries[offset];

    NIG_ENTRY_DEC_REFCNT(entry);

    if (entry->refcnt == 0)
    {
        lm_status = lm_set_mac_in_nig(pdev, NULL, LM_CLI_IDX_NDIS, offset);
        if (lm_status != LM_STATUS_SUCCESS)
        {
            return lm_status;
        }
        mm_mem_zero(entry->addr, sizeof(entry->addr));
    }

    return lm_status;
}


lm_status_t lm_set_mac_in_nig(lm_device_t * pdev, u8_t * mac_addr, lm_cli_idx_t lm_cli_idx, u8_t offset)
{
    u32_t reg_offset = 0;
    u32_t wb_data[2] = {0};
    u8_t  enable_mac = 0;

    #define MAX_OFFSET_IN_MEM_1   8

    if (lm_cli_idx == LM_CLI_IDX_ISCSI)
    {
        offset = ISCSI_OFFSET_IN_NIG;
    }
    else if (offset == ISCSI_OFFSET_IN_NIG)
    {
        offset = MAX_MAC_OFFSET_IN_NIG; /* Invalidate offset if not iscsi and its in iscsi place */
    }

    /* We set the macs in the nig llh only for E2 SI/NIV mode and for NDIS only (first 16 entries) */
    if (CHIP_IS_E1x(pdev) || !IS_MULTI_VNIC(pdev) || IS_MF_SD_MODE(pdev) || offset >= MAX_MAC_OFFSET_IN_NIG)
    {
        return LM_STATUS_SUCCESS;
    }

    /* in switch-independt mode we need to configure the NIG LLH with the appropriate mac addresses, we use the
     * cam mapping 1--1 for all indices smaller than 16 */
    if (mac_addr)
    {
        DbgMessage7(pdev, WARN, "Setting mac in nig to offset: %d mac_addr[%d]:[%d]:[%d]:[%d]:[%d]:[%d]:", offset,
                   mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
        DbgMessage2(pdev, WARN, "[%d]:[%d]\n",  mac_addr[6], mac_addr[7]);

        if (offset < MAX_OFFSET_IN_MEM_1)
        {
            reg_offset = (PORT_ID(pdev)? NIG_REG_LLH1_FUNC_MEM: NIG_REG_LLH0_FUNC_MEM) + 8*offset;
        }
        else
        {
            reg_offset = (PORT_ID(pdev)? NIG_REG_P1_LLH_FUNC_MEM2: NIG_REG_P0_LLH_FUNC_MEM2) + 8*(offset - MAX_OFFSET_IN_MEM_1);
        }

        wb_data[0] = ((mac_addr[2] << 24) | (mac_addr[3] << 16) | (mac_addr[4] << 8) | mac_addr[5]);
        wb_data[1] = ((mac_addr[0] << 8)  | mac_addr[1]);

        REG_WR_DMAE_LEN(pdev, reg_offset, wb_data, ARRSIZE(wb_data));

        enable_mac = 1;
    }

    DbgMessage1(pdev, WARN, "Enable_mac: %d\n", enable_mac);

    if (offset < MAX_OFFSET_IN_MEM_1)
    {
        reg_offset = (PORT_ID(pdev)? NIG_REG_LLH1_FUNC_MEM_ENABLE : NIG_REG_LLH0_FUNC_MEM_ENABLE) + 4*offset;
    }
    else
    {
        reg_offset = (PORT_ID(pdev)? NIG_REG_P1_LLH_FUNC_MEM2_ENABLE : NIG_REG_P0_LLH_FUNC_MEM2_ENABLE) + 4*(offset - MAX_OFFSET_IN_MEM_1);
    }
    REG_WR(pdev, reg_offset, enable_mac);

    return LM_STATUS_SUCCESS;
}

/**
 * General function that waits for a certain state to change,
 * not protocol specific. It takes into account vbd-commander
 * and reset-is-in-progress
 *
 * @param pdev
 * @param curr_state -> what to poll on
 * @param new_state -> what we're waiting for
 *
 * @return lm_status_t TIMEOUT if state didn't change, SUCCESS
 *         otherwise
 */
static lm_status_t lm_wait_state_change(struct _lm_device_t *pdev, volatile u32_t * curr_state, u32_t new_state)
{
    u32_t              delay_us         = 0;
    u32_t             to_cnt   = 10000 + 2360; // We'll wait 10,000 times 100us (1 second) + 2360 times 25000us (59sec) = total 60 sec
                                                                    // (Winodws only note) the 25000 wait will cause wait to be without CPU stall (look in win_util.c)


#ifdef _VBD_CMD_
    if (!GET_FLAGS(*g_everest_sim_flags_ptr, EVEREST_SIM_RAMROD))
    {
        *curr_state = new_state;
        return LM_STATUS_SUCCESS;
    }
#endif


    /* wait for state change */
    while ((*curr_state != new_state) && to_cnt--)
    {
        delay_us = (to_cnt >= 2360) ? 100 : 25000 ;
        mm_wait(pdev, delay_us);

        #ifdef DOS
            sleep(0); // rescheduling threads, since we don't have a memory barrier.
        #elif defined(__LINUX)
            mm_read_barrier(); // synchronize on eth_con->con_state
        #endif

        // in case reset in progress
        // we won't get completion so no need to wait
        if( lm_reset_is_inprogress(pdev) )
        {
            break;
        }
    }

    if ( *curr_state != new_state)
    {
        DbgMessage2(pdev,FATAL,
                    "lm_wait_state_change: state change timeout, curr state=%d, expected new state=%d!\n",
                    *curr_state, new_state);
        if (!lm_reset_is_inprogress(pdev)) {
            #if defined(_VBD_)
            DbgBreak();
            #endif
            return LM_STATUS_TIMEOUT;
        }
    }

    return LM_STATUS_SUCCESS;
}

static lm_status_t lm_eth_wait_state_change(struct _lm_device_t *pdev, u32_t new_state, u8_t cid)
{
    u8_t  const       pf_cid   = VF_TO_PF_CID(pdev,cid);
    connection_info * eth_con  = &(PFDEV(pdev)->vars.connections[pf_cid]);

    /* check args */
    if(CHK_NULL(pdev))
    {
        return LM_STATUS_FAILURE;
    }

    return lm_wait_state_change(pdev, &eth_con->con_state, new_state);

} /* lm_eth_wait_state_change */


lm_status_t lm_eth_init_client_init_data(lm_device_t *pdev, u8_t cid, u8_t sb_id)
{
    struct client_init_ramrod_data * client_init_data_virt = NULL;
    lm_bd_chain_t * rx_chain_sge  = NULL;
    lm_bd_chain_t * rx_chain_bd   = NULL;
    u8_t            stats_cnt_id  = LM_STATS_CNT_ID(pdev);
    u8_t            is_pfdev      = IS_PFDEV(pdev);

    client_init_data_virt = pdev->client_info[cid].client_init_data_virt;

    if CHK_NULL(client_init_data_virt)
    {
        return LM_STATUS_FAILURE;
    }

    mm_mem_zero((void *) client_init_data_virt , sizeof(struct client_init_ramrod_data));

    /* General Structure */
    client_init_data_virt->general.activate_flg          = 1;
    client_init_data_virt->general.client_id             = LM_FW_CLI_ID(pdev, cid);
    client_init_data_virt->general.is_fcoe_flg           = (cid == FCOE_CID(pdev))? TRUE : FALSE;
    client_init_data_virt->general.statistics_counter_id = stats_cnt_id;
    client_init_data_virt->general.statistics_en_flg     = (is_pfdev || (stats_cnt_id != 0xFF))? TRUE : FALSE;
    client_init_data_virt->general.sp_client_id          = LM_FW_CLI_ID(pdev, cid);
    client_init_data_virt->general.mtu                   = mm_cpu_to_le16((u16_t)pdev->params.l2_cli_con_params[cid].mtu);
    client_init_data_virt->general.func_id               = FUNC_ID(pdev); /* FIXME: VFID needs to be given here for VFs... */

    /* Rx Data */
    rx_chain_sge = LM_RXQ_SGE_PTR_IF_VALID(pdev, cid);
    rx_chain_bd  = &LM_RXQ_CHAIN_BD(pdev, cid);

    client_init_data_virt->rx.status_block_id               = LM_FW_SB_ID(pdev, sb_id);
    client_init_data_virt->rx.client_qzone_id               = LM_FW_QZONE_ID(pdev, cid);
    client_init_data_virt->rx.max_agg_size                  = mm_cpu_to_le16(0); /* TPA related only  */;
    client_init_data_virt->rx.extra_data_over_sgl_en_flg    = (cid == OOO_CID(pdev))? TRUE : FALSE;
    client_init_data_virt->rx.cache_line_alignment_log_size = (u8_t)LOG2(CACHE_LINE_SIZE_MASK + 1/* TODO mm_get_cache_line_alignment()*/);
    client_init_data_virt->rx.enable_dynamic_hc             = (u8_t)pdev->params.enable_dynamic_hc[HC_INDEX_ETH_RX_CQ_CONS];

    client_init_data_virt->rx.outer_vlan_removal_enable_flg = IS_MULTI_VNIC(pdev)? TRUE: FALSE;
    if(OOO_CID(pdev) == cid)
    {
        client_init_data_virt->rx.inner_vlan_removal_enable_flg = 0;
    }
    else
    {
        client_init_data_virt->rx.inner_vlan_removal_enable_flg = !pdev->params.keep_vlan_tag;
    }

    /* Forward connection doesn't have a rx ring or rcq ring... it is tx only */
    if (cid != FWD_CID(pdev))
    {
        client_init_data_virt->rx.bd_page_base.lo= mm_cpu_to_le32(lm_bd_chain_phys_addr(rx_chain_bd, 0).as_u32.low);
        client_init_data_virt->rx.bd_page_base.hi= mm_cpu_to_le32(lm_bd_chain_phys_addr(rx_chain_bd, 0).as_u32.high);

        client_init_data_virt->rx.cqe_page_base.lo = mm_cpu_to_le32(lm_bd_chain_phys_addr(&pdev->rx_info.rcq_chain[cid].bd_chain, 0).as_u32.low);
        client_init_data_virt->rx.cqe_page_base.hi = mm_cpu_to_le32(lm_bd_chain_phys_addr(&pdev->rx_info.rcq_chain[cid].bd_chain, 0).as_u32.high);
    }


    if (cid == LM_SW_LEADING_RSS_CID(pdev))
    {
        /* TODO: for now... doesn't have to be leading cid, anyone can get the approx mcast... */
        client_init_data_virt->rx.is_leading_rss = TRUE;
        client_init_data_virt->rx.is_approx_mcast = TRUE;
    }

    client_init_data_virt->rx.approx_mcast_engine_id = FUNC_ID(pdev); /* FIMXE (MichalS) */
    client_init_data_virt->rx.rss_engine_id          = FUNC_ID(pdev); /* FIMXE (MichalS) */

    if(rx_chain_sge)
    {
        /* override bd_buff_size if we are in LAH enabled mode */
        client_init_data_virt->rx.max_bytes_on_bd     = mm_cpu_to_le16((u16_t)pdev->params.l2_cli_con_params[cid].lah_size);
        client_init_data_virt->rx.vmqueue_mode_en_flg = TRUE;
        client_init_data_virt->rx.max_sges_for_packet = LM_MAX_SGES_FOR_PACKET;
        client_init_data_virt->rx.sge_buff_size       = mm_cpu_to_le16(MAX_L2_CLI_BUFFER_SIZE(pdev, cid) - (u16_t)pdev->params.l2_cli_con_params[cid].lah_size - (u16_t)pdev->params.rcv_buffer_offset);

        client_init_data_virt->rx.sge_page_base.hi    = mm_cpu_to_le32(lm_bd_chain_phys_addr(rx_chain_sge, 0).as_u32.high);
        client_init_data_virt->rx.sge_page_base.lo    = mm_cpu_to_le32(lm_bd_chain_phys_addr(rx_chain_sge, 0).as_u32.low);
    }
    else
    {
        client_init_data_virt->rx.max_bytes_on_bd     = mm_cpu_to_le16(MAX_L2_CLI_BUFFER_SIZE(pdev, cid) - (u16_t)pdev->params.rcv_buffer_offset);
        client_init_data_virt->rx.vmqueue_mode_en_flg = FALSE;
        client_init_data_virt->rx.max_sges_for_packet = 0;
        client_init_data_virt->rx.sge_buff_size       = 0;

        client_init_data_virt->rx.sge_page_base.hi    = 0;
        client_init_data_virt->rx.sge_page_base.lo    = 0;
    }


    /* Status block index init we do for Rx + Tx together so that we ask which cid we are only once */
    if (cid == FWD_CID(pdev))
    {
        client_init_data_virt->rx.rx_sb_index_number = 0; /* D/C */
        client_init_data_virt->tx.tx_sb_index_number = HC_SP_INDEX_ETH_FW_TX_CQ_CONS;
    }
    else if (cid == OOO_CID(pdev))
    {
        client_init_data_virt->rx.rx_sb_index_number = HC_INDEX_ISCSI_OOO_CONS;
        client_init_data_virt->tx.tx_sb_index_number = 7; /* D/C */
    }
    else if (cid == ISCSI_CID(pdev))
    {
        client_init_data_virt->rx.rx_sb_index_number = HC_SP_INDEX_ETH_ISCSI_RX_CQ_CONS;
        client_init_data_virt->tx.tx_sb_index_number = HC_SP_INDEX_ETH_ISCSI_CQ_CONS;
    }
    else if (cid == FCOE_CID(pdev))
    {
        client_init_data_virt->rx.rx_sb_index_number = HC_SP_INDEX_ETH_FCOE_RX_CQ_CONS;
        client_init_data_virt->tx.tx_sb_index_number = HC_SP_INDEX_ETH_FCOE_CQ_CONS;
    }
    else if (cid < MAX_ETH_CONS)
    {
        client_init_data_virt->rx.rx_sb_index_number = HC_INDEX_ETH_RX_CQ_CONS;
        client_init_data_virt->tx.tx_sb_index_number = HC_INDEX_ETH_TX_CQ_CONS;
    }
    else
    {
        DbgMessage1(NULL, FATAL, "Invalid cid 0x%x.\n", cid);
        DbgBreakIf(1);
    }

    /* TX Data (remaining , sb index above...)  */
    /* ooo cid doesn't have a tx chain... */
    if (cid != OOO_CID(pdev))
    {
        client_init_data_virt->tx.tx_bd_page_base.hi = mm_cpu_to_le32(lm_bd_chain_phys_addr(&pdev->tx_info.chain[cid].bd_chain, 0).as_u32.high);
        client_init_data_virt->tx.tx_bd_page_base.lo = mm_cpu_to_le32(lm_bd_chain_phys_addr(&pdev->tx_info.chain[cid].bd_chain, 0).as_u32.low);
    }
    client_init_data_virt->tx.tx_status_block_id = LM_FW_SB_ID(pdev, sb_id);
    client_init_data_virt->tx.enforce_security_flg = FALSE; /* TBD: turn on for KVM VF? */

    /* Tx Switching... */
    if (IS_MF_SI_MODE(pdev) && pdev->params.npar_vm_switching_enable &&
        (cid != FWD_CID(pdev)) && (cid != FCOE_CID(pdev)) && (cid != ISCSI_CID(pdev)))
    {
        client_init_data_virt->tx.tx_switching_flg = TRUE;
    }
    else
    {
        client_init_data_virt->tx.tx_switching_flg = FALSE;
    }

    client_init_data_virt->tx.tss_leading_client_id = LM_FW_CLI_ID(pdev, LM_SW_LEADING_RSS_CID(pdev));

    /* FC */
    if (pdev->params.l2_fw_flow_ctrl)
    {
        u16_t low_thresh  = mm_cpu_to_le16(min(250, ((u16_t)(LM_RXQ(pdev, cid).desc_cnt))/4));
        u16_t high_thresh = mm_cpu_to_le16(min(350, ((u16_t)(LM_RXQ(pdev, cid).desc_cnt))/2));

        client_init_data_virt->fc.cqe_pause_thr_low  = low_thresh;
        client_init_data_virt->fc.bd_pause_thr_low   = low_thresh;
        client_init_data_virt->fc.sge_pause_thr_low  = 0;
        client_init_data_virt->fc.rx_cos_mask        = 1;
        client_init_data_virt->fc.cqe_pause_thr_high = high_thresh;
        client_init_data_virt->fc.bd_pause_thr_high  = high_thresh;
        client_init_data_virt->fc.sge_pause_thr_high = 0;
    }

    client_init_data_virt->fc.safc_group_num                  = 0 ;
    client_init_data_virt->fc.safc_group_en_flg               = FALSE;
    client_init_data_virt->fc.traffic_type                    = (cid == FCOE_CID(pdev))? LLFC_TRAFFIC_TYPE_FCOE :
                                                                ((cid == ISCSI_CID(pdev))? LLFC_TRAFFIC_TYPE_ISCSI :
                                                                 LLFC_TRAFFIC_TYPE_NW);

    return LM_STATUS_SUCCESS;
}

lm_status_t lm_establish_eth_con(struct _lm_device_t *pdev, u8_t const cid, u8_t sb_id)
{
    lm_status_t     lm_status     = LM_STATUS_SUCCESS;
    u8_t            cmd_id        = 0;
    u8_t            type          = 0;
    lm_rcq_chain_t* rcq_chain     = NULL;

    DbgMessage1(pdev, INFORMi|INFORMl2sp, "#lm_establish_eth_con, cid=%d\n",cid);

    SET_CON_STATE(pdev, cid, LM_CON_STATE_CLOSE);
    SET_CON_CID(pdev, cid);

    if (IS_PFDEV(pdev) || IS_BASIC_VFDEV(pdev))
    {
        /* TODO: VF??? */
        lm_eth_init_client_init_data(pdev, cid, sb_id);
        lm_init_connection_context(pdev, cid, sb_id);
    }

    /* When we setup the RCQ ring we should advance the CQ cons by MAX_NUM_RAMRODS - the FWD CID is the only connection without an RCQ
     * therefore we skip this operation for forward */
    if (cid != FWD_CID(pdev))
    {
        cmd_id = RAMROD_CMD_ID_ETH_CLIENT_SETUP;
        rcq_chain = &LM_RCQ(pdev, cid);
        lm_bd_chain_bds_produced(&rcq_chain->bd_chain, MAX_NUM_SPE);
    }
    else
    {
        cmd_id = RAMROD_CMD_ID_ETH_FORWARD_SETUP;
    }

    if (IS_PFDEV(pdev) || IS_BASIC_VFDEV(pdev))
    {
        if (IS_VFDEV(pdev))
        {
            type = (ETH_CONNECTION_TYPE | ((8 + ABS_VFID(pdev)) << SPE_HDR_T_FUNCTION_ID_SHIFT));
        }
        else
        {
            type = ETH_CONNECTION_TYPE;
        }
        lm_status = lm_insert_ramrod_req(pdev,
                             REQ_SET_INFORMATION,
                             FALSE,
                             cid,
                             cmd_id,
                             CMD_PRIORITY_MEDIUM,
                             type,
                             pdev->client_info[cid].client_init_data_phys.as_u64);
    }
#ifdef VF_INVOLVED
    else
    {
        lm_status = lm_vf_queue_init(pdev, cid);
    }
#endif
    if (lm_status != LM_STATUS_SUCCESS)
    {
        return lm_status;
    }

    lm_status = lm_eth_wait_state_change(pdev, LM_CON_STATE_OPEN, cid);

    return lm_status;
} /* lm_establish_eth_con */

/**
 * This function is a general eq ramrod fuanction that waits
 * synchroniously for it's completion.
 *
 * @param pdev
 * cmd_id -The ramrod command ID
 * data -ramrod data
 * curr_state - what to poll on
 * curr_state Current state.
 * new_state - what we're waiting for.
 * @return lm_status_t SUCCESS / TIMEOUT on waiting for
 *         completion
 */
lm_status_t
lm_eq_ramrod_post_sync( IN struct _lm_device_t  *pdev,
                        IN u8_t                 cmd_id,
                        IN u64_t                data,
                        IN u8_t                 ramrod_priority,
                        IN volatile u32_t       *p_curr_state,
                        IN u32_t                curr_state,
                        IN u32_t                new_state)
{

    lm_status_t lm_status = LM_STATUS_SUCCESS;

    DbgMessage(pdev, INFORMeq|INFORMl2sp, "#lm_eq_ramrod\n");

    *p_curr_state = curr_state;

    lm_status = lm_sq_post(pdev,
                           0, //Don't care
                           cmd_id,
                           ramrod_priority,
                           NONE_CONNECTION_TYPE,
                           data );

    if (lm_status != LM_STATUS_SUCCESS)
    {
        return lm_status;
    }

    lm_status = lm_wait_state_change(pdev,
                                     p_curr_state,
                                     new_state);

    return lm_status;
} /* lm_eq_ramrod_post_sync */

/**
 * This function sends the "function-start" ramrod and waits
 * synchroniously for it's completion. Called from the
 * chip-start flow.
 *
 * @param pdev
 *
 * @return lm_status_t SUCCESS / TIMEOUT on waiting for
 *         completion
 */
lm_status_t lm_function_start(struct _lm_device_t *pdev)
{
    struct function_start_data * func_start_data = NULL;
    lm_status_t lm_status = LM_STATUS_SUCCESS;


    DbgMessage(pdev, INFORMeq|INFORMl2sp, "#lm_function_start\n");

    pdev->eq_info.function_state = FUNCTION_START_POSTED;

    if (CHK_NULL(pdev) || CHK_NULL(pdev->slowpath_info.slowpath_data.func_start_data))
    {
        return LM_STATUS_INVALID_PARAMETER;
    }

    func_start_data = pdev->slowpath_info.slowpath_data.func_start_data;

    if (pdev->params.multi_vnics_mode)
    {
        DbgBreakIf(pdev->params.mf_mode >= MAX_MF_MODE);
        func_start_data->function_mode = (u16_t)pdev->params.mf_mode;
    }
    else
    {
        func_start_data->function_mode = SINGLE_FUNCTION;
    }

    func_start_data->sd_vlan_tag = pdev->params.ovlan;
    /* NIV_TODO: func_start_data->vif_id = ?? */

    func_start_data->path_id = PATH_ID(pdev);

    lm_status = lm_sq_post(pdev,
                           0,
                           RAMROD_CMD_ID_COMMON_FUNCTION_START,
                           CMD_PRIORITY_NORMAL,
                           NONE_CONNECTION_TYPE,
                           LM_SLOWPATH_PHYS(pdev, func_start_data).as_u64 );

    if (lm_status != LM_STATUS_SUCCESS)
    {
        return lm_status;
    }

    lm_status = lm_wait_state_change(pdev, &pdev->eq_info.function_state, FUNCTION_START_COMPLETED);

    return lm_status;
} /* lm_function_start */

/**
 * This function sends the function-stop ramrod and waits
 * synchroniously for its completion
 *
 * @param pdev
 *
 * @return lm_status_t SUCCESS / TIMEOUT on waiting for
 *         completion
 */
lm_status_t lm_function_stop(struct _lm_device_t *pdev)
{

    lm_status_t lm_status = LM_STATUS_SUCCESS;


    DbgMessage(pdev, INFORMeq|INFORMl2sp, "#lm_function_stop\n");


    pdev->eq_info.function_state = FUNCTION_STOP_POSTED;

    lm_status = lm_sq_post(pdev,
                           0,
                           RAMROD_CMD_ID_COMMON_FUNCTION_STOP,
                           CMD_PRIORITY_NORMAL,
                           NONE_CONNECTION_TYPE,
                           0 );

    if (lm_status != LM_STATUS_SUCCESS)
    {
        return lm_status;
    }

    lm_status = lm_wait_state_change(pdev, &pdev->eq_info.function_state, FUNCTION_STOP_COMPLETED);

    return lm_status;
} /* lm_function_stop */


static lm_status_t
lm_halt_eth_con(struct _lm_device_t *pdev, u8_t cid)
{
    union eth_specific_data ramrod_data = {{0}};
    lm_status_t               lm_status = LM_STATUS_SUCCESS  ;
    u8_t const              pf_cid      = VF_TO_PF_CID(pdev,cid) ;

    ASSERT_STATIC(sizeof(ramrod_data) == sizeof(u64_t));

    DbgMessage2(pdev, INFORMi|INFORMl2sp, "#lm_halt_eth_con cid=%d pf_cid=%d\n",cid, pf_cid);


    if (ERR_IF(PFDEV(pdev)->vars.connections[pf_cid].con_state != LM_CON_STATE_OPEN))
    {
        DbgBreakIf(!DBG_BREAK_ON(UNDER_TEST));
        return LM_STATUS_FAILURE;
    }

    ramrod_data.halt_ramrod_data.client_id = LM_FW_CLI_ID(pdev, cid);

    lm_status = lm_insert_ramrod_req(pdev,
                         REQ_SET_INFORMATION,
                         FALSE,
                         cid,
                         RAMROD_CMD_ID_ETH_HALT,
                         CMD_PRIORITY_MEDIUM,
                         ETH_CONNECTION_TYPE,
                         *(u64_t *)&ramrod_data);
    if (lm_status != LM_STATUS_SUCCESS)
    {
        return lm_status;
    }

    lm_status = lm_eth_wait_state_change(pdev, LM_CON_STATE_HALT, cid);

    return lm_status;
} /* lm_halt_eth_con */

lm_status_t lm_terminate_eth_con(struct _lm_device_t *pdev,
                                      u8_t const          cid)
{
    lm_status_t lm_status    = LM_STATUS_SUCCESS;
    u8_t const  pf_cid       = VF_TO_PF_CID(pdev,cid);


    DbgMessage2(pdev, INFORMi|INFORMl2sp, "#lm_terminate_eth_con, cid=%d pf_cid=%d\n",cid, pf_cid);

    if (ERR_IF(PFDEV(pdev)->vars.connections[pf_cid].con_state != LM_CON_STATE_HALT))
    {
        DbgBreak();
        return LM_STATUS_FAILURE;
    }

    if (IS_VFDEV(pdev))
    {
        PFDEV(pdev)->vars.connections[pf_cid].con_state = LM_CON_STATE_TERMINATE;
        return LM_STATUS_SUCCESS; /* Not supported for VFs */
    }

    lm_status = lm_insert_ramrod_req(PFDEV(pdev),
                         REQ_SET_INFORMATION,
                         FALSE,
                         pf_cid,
                         RAMROD_CMD_ID_ETH_TERMINATE,
                         CMD_PRIORITY_MEDIUM,
                         ETH_CONNECTION_TYPE,
                         0);
    if (lm_status != LM_STATUS_SUCCESS)
    {
        return lm_status;
    }

    lm_status = lm_eth_wait_state_change(pdev, LM_CON_STATE_TERMINATE, pf_cid);

    return lm_status;
}

static lm_status_t lm_cfc_del_eth_con(struct _lm_device_t *pdev,
                                      u8_t const          cid)
{
/* VIA PF!!!!!!*/
    lm_status_t lm_status    = LM_STATUS_SUCCESS;
    u8_t const  pf_cid       = VF_TO_PF_CID(pdev,cid);


    DbgMessage2(pdev, INFORMi|INFORMl2sp, "#lm_cfc_del_eth_con, cid=%d pf_cid=%d\n",cid, pf_cid);

    if (ERR_IF(PFDEV(pdev)->vars.connections[pf_cid].con_state != LM_CON_STATE_TERMINATE))
    {

        DbgBreak();
        return LM_STATUS_FAILURE;
    }

    lm_status = lm_insert_ramrod_req(PFDEV(pdev),
                         REQ_SET_INFORMATION,
                         FALSE,
                         VF_TO_PF_CID(pdev,cid),
                         RAMROD_CMD_ID_COMMON_CFC_DEL,
                         CMD_PRIORITY_MEDIUM,
                         NONE_CONNECTION_TYPE,
                         0);
    if (lm_status != LM_STATUS_SUCCESS)
    {
        return lm_status;
    }

    lm_status = lm_eth_wait_state_change(PFDEV(pdev), LM_CON_STATE_CLOSE, pf_cid);

    return lm_status;
} /* lm_cfc_del_eth_con */

lm_status_t
lm_empty_ramrod_eth(IN struct _lm_device_t *pdev,
                    IN const u32_t          cid,
                    IN u32_t                data_cid,
                    IN volatile u32_t       *curr_state,
                    IN u32_t                new_state)
{
    union eth_specific_data ramrod_data = {{0}};
    lm_status_t lm_status    = LM_STATUS_SUCCESS;

    DbgMessage1(pdev, INFORMi|INFORMl2sp, "#lm_empty_ramrod_eth_conn, curr_state=%d\n",curr_state);

    ASSERT_STATIC(sizeof(ramrod_data) == sizeof(u64_t));

    //Prepare ramrod data
    ramrod_data.update_data_addr.lo = data_cid;
    ramrod_data.update_data_addr.hi = 0 ;
    // Send Empty ramrod.
    lm_status = lm_insert_ramrod_req(pdev,
                                     REQ_SET_INFORMATION,
                                     FALSE,
                                     cid,
                                     RAMROD_CMD_ID_ETH_EMPTY,
                                     CMD_PRIORITY_MEDIUM,/* Called from WI must be done ASAP*/
                                     ETH_CONNECTION_TYPE,
                                     *(u64_t *)&ramrod_data);

    if (lm_status != LM_STATUS_SUCCESS)
    {
        return lm_status;
    }

    lm_status = lm_wait_state_change(pdev,
                                     curr_state,
                                     new_state);

    if (lm_status != LM_STATUS_SUCCESS)
    {
        DbgBreakMsg("lm_empty_ramrod_eth: lm_wait_state_change failed");
    }
    return lm_status;
} /* lm_establish_eth_con */

lm_status_t lm_establish_forward_con(struct _lm_device_t *pdev)
{
    lm_status_t lm_status = LM_STATUS_SUCCESS;
    u8_t const  fwd_cid   = FWD_CID(pdev);

    DbgMessage(pdev,INFORMi | INFORMl2sp,"lm_establish_forward_con\n");
    lm_status = lm_establish_eth_con(pdev, fwd_cid, DEF_STATUS_BLOCK_INDEX);
    if (lm_status != LM_STATUS_SUCCESS) {
        DbgMessage(pdev,FATAL,"lm_establish_forward_con failed\n");
        return lm_status;
    }

    DbgMessage(pdev,INFORMi | INFORMl2sp, "Establish forward connection ramrod completed\n");

    return LM_STATUS_SUCCESS;
}

lm_status_t lm_close_forward_con(struct _lm_device_t *pdev)
{
    lm_status_t lm_status = LM_STATUS_SUCCESS;
    u8_t const  fwd_cid   = FWD_CID(pdev);

    /* halt and terminate ramrods (lm_{halt,terminate}_eth_con) are not sent for the forward channel connection.
       therefore we just change the state from OPEN to TERMINATE, and send the cfc del ramrod */
    DbgBreakIf(pdev->vars.connections[fwd_cid].con_state != LM_CON_STATE_OPEN);
    pdev->vars.connections[fwd_cid].con_state = LM_CON_STATE_TERMINATE;

    lm_status = lm_cfc_del_eth_con(pdev,fwd_cid);
    if (lm_status != LM_STATUS_SUCCESS) {
        return lm_status;
    }

    DbgMessage(pdev,INFORMi | INFORMl2sp,"lm_close_forward_con completed\n");

    return LM_STATUS_SUCCESS;
}

lm_status_t lm_close_eth_con(struct _lm_device_t *pdev,
                             u8_t    const cid)
{
    lm_status_t lm_status;

    if (lm_fl_reset_is_inprogress(pdev)) {
        pdev->vars.connections[cid].con_state = LM_CON_STATE_CLOSE;
        DbgMessage1(pdev, FATAL, "lm_chip_stop: Under FLR: \"close\" cid=%d.\n", cid);
        return LM_STATUS_SUCCESS;
    }

#ifdef VF_INVOLVED
    if (IS_CHANNEL_VFDEV(pdev)) {
        lm_status = lm_vf_queue_close(pdev, cid);
        return lm_status;
    }
#endif
    lm_status = lm_halt_eth_con(pdev,cid);
    if (lm_status != LM_STATUS_SUCCESS)
    {
        return lm_status;
    }

    lm_status = lm_terminate_eth_con(pdev,cid);
    if (lm_status != LM_STATUS_SUCCESS)
    {
        return lm_status;
    }

    lm_status = lm_cfc_del_eth_con(pdev,cid);
    if (lm_status != LM_STATUS_SUCCESS)
    {
        return lm_status;
    }

    lm_status = lm_clear_eth_con_resc(pdev,cid);
    if (lm_status != LM_STATUS_SUCCESS)
    {
        return lm_status;
    }

    DbgMessage1(pdev,INFORMi | INFORMl2sp,"lm_close_eth_con completed for cid=%d\n", cid);

    return LM_STATUS_SUCCESS;
}

lm_status_t lm_chip_start(struct _lm_device_t *pdev)
{
    lm_status_t lm_status = LM_STATUS_SUCCESS ;
    u8_t min_bw = (u8_t)pdev->params.bandwidth_min;
    u8_t max_bw = (u8_t)pdev->params.bandwidth_max;

    DbgMessage(pdev,INFORMi,"lm_chip_start\n");

    if (IS_VFDEV(pdev))
    {
        return LM_STATUS_SUCCESS; //lm_vf_chip_start(pdev);
    }

    if ( max_bw != 0 )
    {//we assume that if one of the BW registry parameters is not 0, then so is the other one.
        DbgBreakIf(min_bw == 0);
        lm_status = lm_mcp_set_mf_bw(pdev, min_bw, max_bw);
        if (LM_STATUS_SUCCESS != lm_status)
        {
            return lm_status;
        }
    }

    /* Chip is initialized. We are now about to send first ramrod we can open slow-path-queue */
    lm_sq_change_state(pdev, SQ_STATE_NORMAL);

    lm_status = lm_function_start(pdev);
    if ( LM_STATUS_SUCCESS != lm_status )
    {
        return lm_status;
    }

    // start timer scan after leading connection ramrod.
    REG_WR(pdev, TM_REG_EN_LINEAR0_TIMER + 4*PORT_ID(pdev),1);

    lm_status = lm_establish_forward_con(pdev);
    if ( LM_STATUS_SUCCESS != lm_status )
    {
        goto on_err ;
    }

on_err:
    if( LM_STATUS_SUCCESS != lm_status )
    {
        DbgMessage(pdev,FATAL,"lm_chip_start on_err:\n");
        lm_function_stop(pdev);
        REG_WR(pdev, TM_REG_EN_LINEAR0_TIMER + 4*PORT_ID(pdev),0);
    }

    return lm_status;
}

/*
 *Function Name:lm_read_fw_stats_ptr
 *
 *Parameters:
 *
 *Description: read stats_ptr ( port and func) from shmem
 *
 *Assumption: stats scratch pad address from MCP can not change on run time (bc upgrade is not valid)
 *            in case bc upgraded - need to stop statistics and read addresses again
 *Returns:
 *
 */
void lm_setup_read_mgmt_stats_ptr( struct _lm_device_t *pdev, IN const u32_t mailbox_num, OUT u32_t* OPTIONAL fw_port_stats_ptr, OUT u32_t* OPTIONAL fw_func_stats_ptr )
{
    if (GET_FLAGS( pdev->params.test_mode, TEST_MODE_NO_MCP)) {
        // E2 TODO: move this to lm_main and get info at get_shmem_info...
        #define NO_MCP_WA_FW_FUNC_STATS_PTR       (0xAF900)
        #define NO_MCP_WA_FW_PORT_STATS_PTR       (0xAFA00)
        if ( 0 != fw_func_stats_ptr)
        {
            *fw_func_stats_ptr = NO_MCP_WA_FW_FUNC_STATS_PTR;
        }

        if ( 0 != fw_port_stats_ptr)
        {
            *fw_port_stats_ptr = NO_MCP_WA_FW_PORT_STATS_PTR;
        }
        return;
    }

    if ( NULL != fw_func_stats_ptr )
    {
        // read func_stats address
        LM_SHMEM_READ(pdev,
                      OFFSETOF(shmem_region_t,
                      func_mb[mailbox_num].fw_mb_param),
                      fw_func_stats_ptr);

        // Backward compatibility adjustments for Bootcode v4.0.8 and below
        if( 0xf80a0000 == *fw_func_stats_ptr )
        {
            DbgMessage1(pdev, FATAL , "lm_read_fw_stats_ptr: boot code earlier than v4.0.8 fw_mb=%p-->NULL\n", *fw_func_stats_ptr );
            *fw_func_stats_ptr = 0;//NULL
        }
        DbgMessage1(pdev, WARN , "lm_read_fw_stats_ptr: pdev->vars.fw_func_stats_ptr=%p\n", *fw_func_stats_ptr );
    }

    if ( NULL != fw_port_stats_ptr )
    {
        // read port_stats address
        LM_SHMEM_READ(pdev,
                      OFFSETOF(shmem_region_t,
                      port_mb[PORT_ID(pdev)].port_stx),
                      fw_port_stats_ptr);

        DbgMessage1(pdev, WARN, "lm_read_fw_stats_ptr: pdev->vars.fw_port_stats_ptr=%p\n", *fw_port_stats_ptr );
    }
}


/* Description:
 *    The main function of this routine is to initialize the
 *    hardware. it configues all hw blocks in several phases acording to mcp response:
 *    1. common blocks
 *    2. per function blocks
 */
lm_status_t
lm_chip_init( struct _lm_device_t *pdev)
{

    const lm_loader_opcode opcode    = LM_LOADER_OPCODE_LOAD;
    lm_loader_response     resp      = 0;
    lm_status_t            lm_status = LM_STATUS_SUCCESS;

    DbgMessage1(pdev, INFORMi , "### lm_chip_init %x\n",CHIP_NUM(pdev));

#ifdef VF_INVOLVED
    if (IS_VFDEV(pdev)) {
        return lm_vf_chip_init(pdev);
    }
#endif
    // init mcp sequences
    lm_status = lm_mcp_cmd_init(pdev);

    if( LM_STATUS_SUCCESS != lm_status )
    {
        DbgMessage1(pdev,FATAL,"lm_chip_init: mcp_cmd_init failed. lm_status=0x%x\n", lm_status);
        DbgBreakMsg("lm_mcp_cmd_init failed!\n");
        return lm_status ;
    }

    resp = lm_loader_lock(pdev, opcode );

    /* Save the load response */
    pdev->vars.load_code = resp;

    if( LM_LOADER_RESPONSE_INVALID != resp )
    {
        // We need to call it here since init_funciton_part use these pointers
        lm_setup_read_mgmt_stats_ptr(pdev, FUNC_MAILBOX_ID(pdev), &pdev->vars.fw_port_stats_ptr, &pdev->vars.fw_func_stats_ptr );
    }

    // This should be first call after load request since we must complete
    // these settings in 5 seconds (MCP keepalive timeout)
    lm_driver_pulse_always_alive(pdev);

    // update mps and mrrs from pcicfg
    lm_status = lm_get_pcicfg_mps_mrrs(pdev);

    switch (resp)
    {
    case LM_LOADER_RESPONSE_LOAD_COMMON_CHIP:
    case LM_LOADER_RESPONSE_LOAD_COMMON:
        lm_reset_path( pdev, FALSE ); /* Give a chip-reset (path) before initializing driver*/
        init_common_part(pdev);

        lm_init_intmem_common(pdev);
        // going to the port part no break
        //Check if there is dbus work
        mm_dbus_start_if_enable(pdev);
    case LM_LOADER_RESPONSE_LOAD_PORT:
        // If we are here, DMAE is ready (from common part init) - set it for TRUE for non-first devices
        pdev->vars.b_is_dmae_ready = TRUE;

        // set device as pmf
        pdev->vars.is_pmf = PMF_ORIGINAL;

        init_port_part(pdev);
        lm_init_intmem_port(pdev);

        //The PFC initialization must be done after all port initialization. (PFC runs over some port initialization)
        if(CHIP_IS_E1H(pdev))
        {
            lm_pfc_handle_pfc(pdev);
        }
        // going to the function part - fall through
    case LM_LOADER_RESPONSE_LOAD_FUNCTION:
        // If we are here, DMAE is ready (from port part init) - set it for TRUE for non-first devices
        pdev->vars.b_is_dmae_ready = TRUE;
        init_function_part(pdev);
        init_status_blocks(pdev);
        lm_init_intmem_function(pdev);
#ifndef __BIG_ENDIAN
        lm_tcp_init_chip_common(pdev);
#endif
        break;

    default:
        DbgMessage(pdev, WARN, "wrong loader response\n");
        DbgBreakIfAll(1);
    }

    resp = lm_loader_unlock( pdev, opcode ) ;

    if (resp != LM_LOADER_RESPONSE_LOAD_DONE)
    {
        DbgMessage(pdev, WARN, "wrong loader response\n");
        DbgBreakIfAll(1);
    }

    // TBD link training

    return LM_STATUS_SUCCESS;
}

lm_resource_idx_t cid_to_resource(lm_device_t *pdev, u32_t cid)
{
    lm_resource_idx_t resource;

    if (cid < LM_MAX_RSS_CHAINS(pdev))
    {
        resource = LM_RESOURCE_NDIS;
    }
    else if (cid == RDMA_CID(pdev))
    {
        resource = LM_RESOURCE_RDMA;
    }
    else if (cid == ISCSI_CID(pdev))
    {
        resource = LM_RESOURCE_ISCSI;
    }
    else if (cid == FCOE_CID(pdev))
    {
        resource = LM_RESOURCE_FCOE;
    }
    else if (cid == FWD_CID(pdev))
    {
        resource = LM_RESOURCE_FWD;
    }
    else if (cid == OOO_CID(pdev))
    {
        resource = LM_RESOURCE_OOO;
    }
    else
    {
        resource = LM_RESOURCE_COMMON;
    }

    return resource;
}
/**
 * @Description:
 *   allocate given num of coalesce buffers, and queue them in the txq chain.
 *   1 buffer is allocated for LSO packets, and the rest are allocated with
 *   MTU size.
 * @Return:
 *   lm_status
*/
static lm_status_t
lm_allocate_coalesce_buffers(
    lm_device_t     *pdev,
    lm_tx_chain_t   *txq,
    u32_t           coalesce_buf_cnt,
    u32_t           cid)
{
    lm_coalesce_buffer_t *last_coalesce_buf = NULL;
    lm_coalesce_buffer_t *coalesce_buf      = NULL;
    lm_address_t         mem_phy            = {{0}};
    u8_t *               mem_virt           = NULL;
    u32_t                mem_left           = 0;
    u32_t                mem_size           = 0;
    u32_t                buf_size           = 0;
    u32_t                cnt                = 0;
    u8_t                 mm_cli_idx         = 0;

    /* check arguments */
    if(CHK_NULL(pdev) || CHK_NULL(txq))
    {
        return LM_STATUS_FAILURE;
    }

    DbgMessage1(pdev, VERBOSEi | VERBOSEl2sp,
                "#lm_allocate_coalesce_buffers, coalesce_buf_cnt=%d\n",
                coalesce_buf_cnt);

    mm_cli_idx = cid_to_resource(pdev, cid); //!!DP mm_cli_idx_to_um_idx(LM_CHAIN_IDX_CLI(pdev, idx));

    if(coalesce_buf_cnt == 0)
    {
        return LM_STATUS_SUCCESS;
    }

    buf_size = MAX_L2_CLI_BUFFER_SIZE(pdev, cid);

    mem_size = coalesce_buf_cnt * sizeof(lm_coalesce_buffer_t);
    mem_virt = mm_alloc_mem(pdev,mem_size, mm_cli_idx);
    if(ERR_IF(!mem_virt))
    {
        DbgBreakIf(DBG_BREAK_ON(MEMORY_ALLOCATION_FAILURE));
        return LM_STATUS_RESOURCE;
    }
    mm_memset((void *) (mem_virt), 0, mem_size);

    /* Create a list of frame buffer descriptors. */
    coalesce_buf = (lm_coalesce_buffer_t *) mem_virt;
    for(cnt = 0; cnt < coalesce_buf_cnt; cnt++)
    {
        coalesce_buf->frags.cnt = 1;
        coalesce_buf->frags.size = 0; /* not in use */
        coalesce_buf->buf_size = buf_size;

        s_list_push_tail(
            &txq->coalesce_buf_list,
            &coalesce_buf->link);

        coalesce_buf++;
    }

    /* Have at least one coalesce buffer large enough to copy
     * an LSO frame. */
    coalesce_buf = (lm_coalesce_buffer_t *) s_list_peek_head(
        &txq->coalesce_buf_list);
    coalesce_buf->buf_size = 0x10000; /* TBD: consider apply change here for GSO */

    /* Determine the total memory for the coalesce buffers. */
    mem_left = 0;

    coalesce_buf = (lm_coalesce_buffer_t *) s_list_peek_head(
        &txq->coalesce_buf_list);
    while(coalesce_buf)
    {
        mem_left += coalesce_buf->buf_size;
        coalesce_buf = (lm_coalesce_buffer_t *) s_list_next_entry(
            &coalesce_buf->link);
    }

    mem_size = 0;

    /* Initialize all the descriptors to point to a buffer. */
    coalesce_buf = (lm_coalesce_buffer_t *) s_list_peek_head(
        &txq->coalesce_buf_list);
    while(coalesce_buf)
    {
        #define MAX_CONTIGUOUS_BLOCK            (64*1024)

        /* Allocate a small block of memory at a time. */
        if(mem_size == 0)
        {
            last_coalesce_buf = coalesce_buf;

            while(coalesce_buf)
            {
                mem_size += coalesce_buf->buf_size;
                if(mem_size >= MAX_CONTIGUOUS_BLOCK) /* TBD: consider apply change here for GSO */
                {
                    break;
                }

                coalesce_buf = (lm_coalesce_buffer_t *) s_list_next_entry(
                    &coalesce_buf->link);
            }

            mem_left -= mem_size;

            mem_virt = mm_alloc_phys_mem( pdev, mem_size, &mem_phy, 0, mm_cli_idx);
            if(ERR_IF(!mem_virt))
            {
                DbgBreakIf(DBG_BREAK_ON(MEMORY_ALLOCATION_FAILURE));
                return LM_STATUS_RESOURCE;
            }
            mm_memset((void *) (mem_virt), 0, mem_size);

            coalesce_buf = last_coalesce_buf;
        }

        coalesce_buf->mem_virt = mem_virt;
        coalesce_buf->frags.frag_arr[0].addr = mem_phy;
        coalesce_buf->frags.frag_arr[0].size = 0; /* to be set later according to actual packet size */
        mem_size -= coalesce_buf->buf_size;

        /* Go to the next packet buffer. */
        mem_virt += coalesce_buf->buf_size;
        LM_INC64(&mem_phy, coalesce_buf->buf_size);

        coalesce_buf = (lm_coalesce_buffer_t *) s_list_next_entry(
            &coalesce_buf->link);
    }

    if(mem_left || mem_size)
    {
        DbgBreakMsg("Memory allocation out of sync\n");

        return LM_STATUS_FAILURE;
    }

    return LM_STATUS_SUCCESS;
} /* lm_allocate_coalesce_buffers */

lm_status_t
lm_alloc_txq(
    IN struct _lm_device_t *pdev,
    IN u32_t const          cid, /* chain id */
    IN u16_t const          page_cnt,
    IN u16_t const          coalesce_buf_cnt)
{
    lm_tx_chain_t *tx_chain   = NULL  ;
    u32_t const    mem_size   = page_cnt * LM_PAGE_SIZE;
    u8_t  mm_cli_idx      = 0 ;

    DbgMessage2(pdev, INFORMi | INFORMl2sp, "#lm_alloc_txq, cid=%d, page_cnt=%d\n", cid, page_cnt);

    /* check arguments */
    if(CHK_NULL(pdev) ||
       ERR_IF((ARRSIZE(pdev->tx_info.chain) <= cid) || !page_cnt))
    {
        return LM_STATUS_FAILURE;
    }

    tx_chain = &LM_TXQ(pdev, cid);

    mm_cli_idx = cid_to_resource(pdev, cid);

    /* alloc the chain */

    tx_chain->bd_chain.bd_chain_virt =
        mm_alloc_phys_mem( pdev, mem_size, &tx_chain->bd_chain.bd_chain_phy, 0, mm_cli_idx);
    if(ERR_IF(!tx_chain->bd_chain.bd_chain_virt))
    {
        DbgBreakIf(DBG_BREAK_ON(MEMORY_ALLOCATION_FAILURE));
        return LM_STATUS_RESOURCE;
    }
    mm_mem_zero((void *) (tx_chain->bd_chain.bd_chain_virt), mem_size);

    tx_chain->bd_chain.page_cnt = page_cnt;

    s_list_init(&tx_chain->active_descq, NULL, NULL, 0);
    s_list_init(&tx_chain->coalesce_buf_list, NULL, NULL, 0);
    tx_chain->idx              = cid;
    tx_chain->coalesce_buf_cnt = coalesce_buf_cnt;

    return lm_allocate_coalesce_buffers(
        pdev,
        &LM_TXQ(pdev, cid),
        coalesce_buf_cnt,
        cid);

} /* lm_alloc_txq */

lm_status_t
lm_alloc_rxq(
    IN struct _lm_device_t *pdev,
    IN u32_t const          cid,
    IN u16_t const          page_cnt,
    IN u32_t const          desc_cnt)
{
    lm_rx_chain_t*     rxq_chain        = NULL;
    lm_bd_chain_t *    bd_chain         = NULL;
    lm_rxq_chain_idx_t rx_chain_idx_max = LM_RXQ_CHAIN_IDX_MAX;
    lm_rxq_chain_idx_t rx_chain_idx_cur = 0;
    u32_t const        mem_size         = page_cnt * LM_PAGE_SIZE;
    u8_t               mm_cli_idx       = 0 ;

    /* check arguments */
    if(CHK_NULL(pdev) ||
       ERR_IF((ARRSIZE(pdev->rx_info.rxq_chain) <= cid) || !page_cnt))
    {
        return LM_STATUS_FAILURE;
    }

    rxq_chain = &LM_RXQ(pdev, cid);

    DbgMessage3(pdev, INFORMi, "#lm_alloc_rxq, cid=%d, page_cnt=%d, desc_cnt=%d\n",
                cid, page_cnt, desc_cnt);

    mm_cli_idx = cid_to_resource(pdev, cid);//!!DP mm_cli_idx_to_um_idx(LM_CHAIN_IDX_CLI(pdev, idx));

    s_list_init(&rxq_chain->free_descq, NULL, NULL, 0);
    s_list_init(&rxq_chain->active_descq, NULL, NULL, 0);
    rxq_chain->idx      = cid;
    rxq_chain->desc_cnt = desc_cnt;

    /* alloc the chain(s) */
    rx_chain_idx_max = LM_RXQ_IS_CHAIN_SGE_VALID( pdev, cid ) ? LM_RXQ_CHAIN_IDX_SGE : LM_RXQ_CHAIN_IDX_BD;

    for( rx_chain_idx_cur = 0; rx_chain_idx_cur <= rx_chain_idx_max; rx_chain_idx_cur++ )
    {
        bd_chain = &LM_RXQ_CHAIN( pdev, cid, rx_chain_idx_cur );

        bd_chain->bd_chain_virt =  mm_alloc_phys_mem( pdev, mem_size, &bd_chain->bd_chain_phy, 0, mm_cli_idx);
        if(ERR_IF(!bd_chain->bd_chain_virt))
        {
            DbgBreakIf(DBG_BREAK_ON(MEMORY_ALLOCATION_FAILURE));
            return LM_STATUS_RESOURCE;
        }
        mm_mem_zero((void *) (bd_chain->bd_chain_virt) , mem_size);

        bd_chain->page_cnt = page_cnt;
    }

    return LM_STATUS_SUCCESS;
} /* lm_alloc_rxq */

lm_status_t
lm_alloc_rcq(
    IN struct _lm_device_t *pdev,
    IN u32_t const          cid,
    IN u16_t const          page_cnt)
{
    lm_rcq_chain_t *rcq_chain = NULL;
    u32_t const mem_size      = page_cnt * LM_PAGE_SIZE;
    u8_t  mm_cli_idx      = 0 ;

    /* check arguments */
    if(CHK_NULL(pdev) ||
       ERR_IF((ARRSIZE(pdev->rx_info.rcq_chain) <= cid) || !page_cnt))
    {
        return LM_STATUS_FAILURE;
    }

    ASSERT_STATIC(sizeof(struct eth_rx_bd)*LM_RX_BD_CQ_SIZE_RATIO == sizeof(union eth_rx_cqe));
    ASSERT_STATIC(sizeof(struct eth_rx_bd) == sizeof(struct eth_rx_sge) );

    rcq_chain = &pdev->rx_info.rcq_chain[cid];

    DbgMessage2(pdev, INFORMi | INFORMl2sp,
                "#lm_alloc_rcq, idx=%d, page_cnt=%d\n",
                cid, page_cnt);

    mm_cli_idx = cid_to_resource(pdev, cid);//!!DP mm_cli_idx_to_um_idx(LM_CHAIN_IDX_CLI(pdev, idx));

    /* alloc the chain */
    rcq_chain->bd_chain.bd_chain_virt =
        mm_alloc_phys_mem( pdev, mem_size, &rcq_chain->bd_chain.bd_chain_phy, 0, mm_cli_idx);

    if(ERR_IF(!rcq_chain->bd_chain.bd_chain_virt))
    {
        DbgBreakIf(DBG_BREAK_ON(MEMORY_ALLOCATION_FAILURE));
        return LM_STATUS_RESOURCE;
    }

    mm_mem_zero((void *) (rcq_chain->bd_chain.bd_chain_virt) , mem_size);
    rcq_chain->bd_chain.page_cnt = page_cnt;

    return LM_STATUS_SUCCESS;
} /* lm_alloc_rcq */

lm_status_t lm_alloc_sq(struct _lm_device_t *pdev)
{
    lm_sq_info_t * sq_info = &pdev->sq_info;

    sq_info->sq_chain.sq_chain_virt = mm_alloc_phys_mem( pdev,
                                                         LM_PAGE_SIZE,
                                                         (lm_address_t*)&(sq_info->sq_chain.bd_chain_phy),
                                                         0,
                                                         LM_CLI_IDX_MAX);

    if CHK_NULL(sq_info->sq_chain.sq_chain_virt)
    {
        DbgBreakIf(DBG_BREAK_ON(MEMORY_ALLOCATION_FAILURE));
        return LM_STATUS_RESOURCE ;
    }

    mm_mem_zero((void *) (sq_info->sq_chain.sq_chain_virt), LM_PAGE_SIZE);

    return LM_STATUS_SUCCESS;
}

lm_status_t lm_alloc_eq(struct _lm_device_t *pdev)
{
    lm_eq_chain_t *eq_chain = NULL;
    u32_t          mem_size = 0;
    u8_t  const    page_cnt = 1;


    /* check arguments */
    if(CHK_NULL(pdev))
    {
        return LM_STATUS_FAILURE;
    }

    DbgMessage(pdev, INFORMi | INFORMl2sp, "#lm_alloc_eq\n");

    mem_size = page_cnt * LM_PAGE_SIZE;
    eq_chain = &pdev->eq_info.eq_chain;


    /* alloc the chain */
    eq_chain->bd_chain.bd_chain_virt =
        mm_alloc_phys_mem( pdev, mem_size, &eq_chain->bd_chain.bd_chain_phy, 0, LM_CLI_IDX_MAX);

    if(ERR_IF(!eq_chain->bd_chain.bd_chain_virt))
    {
        DbgBreakIf(DBG_BREAK_ON(MEMORY_ALLOCATION_FAILURE));
        return LM_STATUS_RESOURCE;
    }

    mm_mem_zero((void *) (eq_chain->bd_chain.bd_chain_virt), mem_size);
    eq_chain->bd_chain.page_cnt = page_cnt;

    return LM_STATUS_SUCCESS;

}

lm_status_t lm_alloc_client_info(struct _lm_device_t *pdev)
{
    struct client_init_ramrod_data * client_init_data_virt = NULL;
    const u32_t mem_size = sizeof(struct client_init_ramrod_data);
    u8_t i = 0;

    for (i = 0; i < ARRSIZE(pdev->client_info); i++)
    {
        client_init_data_virt = mm_alloc_phys_mem(pdev, mem_size, &pdev->client_info[i].client_init_data_phys, 0, LM_RESOURCE_COMMON);
        if CHK_NULL(client_init_data_virt)
        {
            DbgBreakIf(DBG_BREAK_ON(MEMORY_ALLOCATION_FAILURE));
            return LM_STATUS_RESOURCE ;
        }

        mm_mem_zero((void *)client_init_data_virt, mem_size);

        pdev->client_info[i].client_init_data_virt = client_init_data_virt;
    }

    return LM_STATUS_SUCCESS;
}

lm_status_t lm_setup_client_info(struct _lm_device_t *pdev)
{
    struct client_init_ramrod_data * client_init_data_virt = NULL;
    const u32_t mem_size = sizeof(struct client_init_ramrod_data);

    u8_t i = 0;

    for (i = 0; i < ARRSIZE(pdev->client_info); i++)
    {
        client_init_data_virt = pdev->client_info[i].client_init_data_virt;
        if CHK_NULL(client_init_data_virt)
        {
            DbgMessage(pdev, FATAL, "client-init-data at this point is not expected to be null... \n");
            return LM_STATUS_FAILURE ;
        }
        mm_mem_zero((void *)client_init_data_virt, mem_size);
    }

    return LM_STATUS_SUCCESS;
}

lm_status_t
lm_setup_txq(
    IN struct _lm_device_t *pdev,
    IN u32_t                cid)
{
    lm_bd_chain_t *                      bd_chain = NULL;
    volatile struct hc_sp_status_block * sp_sb = NULL;
    u16_t volatile *                     sb_indexes = NULL;

    /* check arguments */
    if(CHK_NULL(pdev) ||
       ERR_IF((ARRSIZE(pdev->tx_info.chain) <= cid)))
    {
        return LM_STATUS_FAILURE;
    }
    DbgMessage1(pdev, INFORMi|INFORMl2sp, "#lm_setup_txq, cid=%d\n",cid);

    sp_sb = lm_get_default_status_block(pdev);

    LM_TXQ(pdev, cid).prod_bseq = 0;
    LM_TXQ(pdev, cid).pkt_idx = 0;
    LM_TXQ(pdev, cid).coalesce_buf_used = 0;
    LM_TXQ(pdev, cid).lso_split_used = 0;

    bd_chain = &LM_TXQ(pdev, cid).bd_chain;
    lm_bd_chain_setup(pdev, bd_chain, bd_chain->bd_chain_virt, bd_chain->bd_chain_phy, bd_chain->page_cnt, sizeof(struct eth_tx_bd), /* is full? */0, TRUE);

    DbgMessage3(pdev, INFORMi, "txq %d, bd_chain %p, bd_left %d\n",
        cid,
        LM_TXQ(pdev, cid).bd_chain.next_bd,
        LM_TXQ(pdev, cid).bd_chain.bd_left);

    DbgMessage2(pdev, INFORMi, "   bd_chain_phy 0x%x%08x\n",
        LM_TXQ(pdev, cid).bd_chain.bd_chain_phy.as_u32.high,
        LM_TXQ(pdev, cid).bd_chain.bd_chain_phy.as_u32.low);

    mm_memset((void *)(&LM_TXQ(pdev, cid).eth_tx_prods.packets_prod), 0, sizeof(eth_tx_prod_t));

    if (cid == FWD_CID(pdev))
    {
        sp_sb->index_values[HC_SP_INDEX_ETH_FW_TX_CQ_CONS] = 0;
        LM_TXQ(pdev, cid).hw_con_idx_ptr =
            &(sp_sb->index_values[HC_SP_INDEX_ETH_FW_TX_CQ_CONS]);
        LM_TXQ(pdev, cid).hc_sb_info.hc_sb = STATUS_BLOCK_SP_SL_TYPE; // STATUS_BLOCK_SP_TYPE;
        LM_TXQ(pdev, cid).hc_sb_info.hc_index_value = HC_SP_INDEX_ETH_FW_TX_CQ_CONS;
        /* iro_dhc_offste not initialized on purpose --> not expected for FWD channel */
    }
    else if (cid == ISCSI_CID(pdev))
    {
        sp_sb->index_values[HC_SP_INDEX_ETH_ISCSI_CQ_CONS] = 0;
        LM_TXQ(pdev, cid).hw_con_idx_ptr = &(sp_sb->index_values[HC_SP_INDEX_ETH_ISCSI_CQ_CONS]);
        LM_TXQ(pdev, cid).hc_sb_info.hc_sb = STATUS_BLOCK_SP_SL_TYPE; //STATUS_BLOCK_SP_TYPE;
        LM_TXQ(pdev, cid).hc_sb_info.hc_index_value = HC_SP_INDEX_ETH_ISCSI_CQ_CONS;
        /* iro_dhc_offste not initialized on purpose --> not expected for FWD channel */
    }
    else if (cid == FCOE_CID(pdev))
    {
        sp_sb->index_values[HC_SP_INDEX_ETH_FCOE_CQ_CONS] = 0;
        LM_TXQ(pdev, cid).hw_con_idx_ptr =
            &(sp_sb->index_values[HC_SP_INDEX_ETH_FCOE_CQ_CONS]);
        LM_TXQ(pdev, cid).hc_sb_info.hc_sb = STATUS_BLOCK_SP_SL_TYPE; //STATUS_BLOCK_SP_TYPE;
        LM_TXQ(pdev, cid).hc_sb_info.hc_index_value = HC_SP_INDEX_ETH_FCOE_CQ_CONS;
        /* iro_dhc_offste not initialized on purpose --> not expected for FWD channel */
    }
    else if(cid == OOO_CID(pdev))
    {
        DbgBreakMsg("OOO doesn't have a txq");
        return LM_STATUS_FAILURE;
    }
    else
    {
        u32_t sb_id = RSS_ID_TO_SB_ID(CID_TO_RSS_ID(cid));
        const u8_t byte_counter_id = CHIP_IS_E1x(pdev)? LM_FW_SB_ID(pdev, sb_id) : LM_FW_DHC_QZONE_ID(pdev, sb_id);

        // Assign the TX chain consumer pointer to the consumer index in the status block. TBD: rename HC_INDEX_C_ETH_TX_CQ_CONS as its inappropriate
        if( sb_id >= ARRSIZE(pdev->vars.status_blocks_arr) )
        {
            DbgBreakIfAll( sb_id >= ARRSIZE(pdev->vars.status_blocks_arr) ) ;
            return LM_STATUS_FAILURE ;
        }

        sb_indexes = lm_get_sb_indexes(pdev, (u8_t)sb_id);
        sb_indexes[HC_INDEX_ETH_TX_CQ_CONS] = 0;
        LM_TXQ(pdev, cid).hw_con_idx_ptr = sb_indexes + HC_INDEX_ETH_TX_CQ_CONS;
        LM_TXQ(pdev, cid).hc_sb_info.hc_sb = STATUS_BLOCK_NORMAL_TYPE;
        LM_TXQ(pdev, cid).hc_sb_info.hc_index_value = HC_INDEX_ETH_TX_CQ_CONS;
        if (IS_PFDEV(pdev))
        {
            LM_TXQ(pdev, cid).hc_sb_info.iro_dhc_offset = CSTORM_BYTE_COUNTER_OFFSET(byte_counter_id, HC_INDEX_ETH_TX_CQ_CONS);
        }
        else
        {
            DbgMessage(pdev, FATAL, "Dhc not implemented for VF yet\n");
        }
    }

    return LM_STATUS_SUCCESS;
} /* lm_setup_txq */


lm_status_t lm_setup_rxq( IN struct _lm_device_t *pdev,
                          IN u32_t const          cid)
{
    lm_bd_chain_t * bd_chain = NULL;
    lm_rx_chain_t *    rxq_chain                             = NULL;
    lm_rxq_chain_idx_t rx_chain_idx_max                      = LM_RXQ_CHAIN_IDX_MAX;
    lm_rxq_chain_idx_t rx_chain_idx_cur                      = 0;
    static u8_t const  eth_rx_size_arr[LM_RXQ_CHAIN_IDX_MAX] = {sizeof(struct eth_rx_bd), sizeof(struct eth_rx_sge)};
    u32_t              sb_id                                 = RSS_ID_TO_SB_ID(CID_TO_RSS_ID(cid));
    const u8_t         byte_counter_id                       = CHIP_IS_E1x(pdev)? LM_FW_SB_ID(pdev, sb_id) : LM_FW_DHC_QZONE_ID(pdev, sb_id);

    /* check arguments */
    if(CHK_NULL(pdev) ||
       ERR_IF((ARRSIZE(pdev->rx_info.rxq_chain) <= cid)))
    {
        return LM_STATUS_FAILURE;
    }

    DbgMessage1(pdev, INFORMi|INFORMl2sp, "#lm_setup_rxq, cid=%d\n",cid);

    rxq_chain = &LM_RXQ(pdev, cid);

    rxq_chain->prod_bseq                = 0;
    rxq_chain->ret_bytes                = 0;
    rxq_chain->ret_bytes_last_fw_update = 0;
    rxq_chain->bd_prod_without_next     = 0;

    rx_chain_idx_max = LM_RXQ_IS_CHAIN_SGE_VALID( pdev, cid ) ? LM_RXQ_CHAIN_IDX_SGE : LM_RXQ_CHAIN_IDX_BD;

    for( rx_chain_idx_cur = 0; rx_chain_idx_cur <= rx_chain_idx_max; rx_chain_idx_cur++ )
    {
        bd_chain = &LM_RXQ_CHAIN( pdev, cid, rx_chain_idx_cur );

        lm_bd_chain_setup(pdev, bd_chain, bd_chain->bd_chain_virt, bd_chain->bd_chain_phy,bd_chain->page_cnt, eth_rx_size_arr[rx_chain_idx_cur], /* is full? */0, TRUE);

        DbgMessage4(pdev, INFORMi, "rxq[%d] bd_chain[%d] %p, bd_left %d\n", cid,
                                                                            rx_chain_idx_cur,
                                                                            bd_chain->next_bd,
                                                                            bd_chain->bd_left);

        DbgMessage3(pdev, INFORMi, "   bd_chain_phy[%d] 0x%x%08x\n", rx_chain_idx_cur,
                                                                     bd_chain->bd_chain_phy.as_u32.high,
                                                                     bd_chain->bd_chain_phy.as_u32.low);
    }

    /* We initilize the hc_sb_info here for completeness. The fw updates are actually done by rcq-chain, but the dynamic-host-coalescing based on rx-chain */
    rxq_chain->hc_sb_info.hc_sb = STATUS_BLOCK_NORMAL_SL_TYPE;
    rxq_chain->hc_sb_info.hc_index_value = HC_INDEX_ETH_RX_CQ_CONS;
    if (IS_PFDEV(pdev))
    {
        rxq_chain->hc_sb_info.iro_dhc_offset = CSTORM_BYTE_COUNTER_OFFSET(byte_counter_id, HC_INDEX_ETH_RX_CQ_CONS);
    }
    else
    {
        DbgMessage(pdev, FATAL, "Dhc not implemented for VF yet\n");
    }

    return LM_STATUS_SUCCESS;
} /* lm_setup_rxq */


lm_status_t
lm_setup_rcq( IN struct _lm_device_t *pdev,
              IN u32_t  const         cid)
{
    lm_bd_chain_t *                      bd_chain   = NULL;
    lm_rcq_chain_t *                     rcq_chain  = NULL;
    lm_rx_chain_t *                      rxq_chain  = NULL;
    volatile struct hc_sp_status_block * sp_sb      = NULL;
    u16_t volatile *                     sb_indexes = NULL;

    /* check arguments */
    if(CHK_NULL(pdev) ||
       ERR_IF((ARRSIZE(pdev->rx_info.rcq_chain) <= cid)))
    {
        return LM_STATUS_FAILURE;
    }

    rcq_chain = &LM_RCQ(pdev, cid);
    rxq_chain = &LM_RXQ(pdev, cid);

    DbgMessage1(pdev, INFORMi|INFORMl2sp, "#lm_setup_rcq, cid=%d\n",cid);

    sp_sb = lm_get_default_status_block(pdev);

    rcq_chain->prod_bseq = 0;
    if (CHIP_IS_E1x(pdev))
    {
        rcq_chain->iro_prod_offset = USTORM_RX_PRODS_E1X_OFFSET(PORT_ID(pdev), LM_FW_CLI_ID(pdev, cid));
    }
    else
    {
        if (IS_PFDEV(pdev) || IS_BASIC_VFDEV(pdev))
        {
            rcq_chain->iro_prod_offset = USTORM_RX_PRODS_E2_OFFSET(LM_FW_QZONE_ID(pdev, cid));
        }
        else
        {
            rcq_chain->iro_prod_offset = LM_FW_QZONE_ID(pdev, cid)*sizeof(struct ustorm_queue_zone_data);
            DbgMessage1(pdev, FATAL, "iro_prod_offset for vf = %x...\n", rcq_chain->iro_prod_offset);
        }
    }

    //if(pdev->params.l2_rx_desc_cnt[0]) /* if removed. was not required */
    bd_chain = &rcq_chain->bd_chain;

    lm_bd_chain_setup(pdev, bd_chain, bd_chain->bd_chain_virt, bd_chain->bd_chain_phy,bd_chain->page_cnt, sizeof(union eth_rx_cqe), /* is full? */0, TRUE);

    //number of Bds left in the RCQ must be at least the same with its corresponding Rx chain.
    DbgBreakIf(lm_bd_chain_avail_bds(&rxq_chain->chain_arr[LM_RXQ_CHAIN_IDX_BD]) <= lm_bd_chain_avail_bds(&rcq_chain->bd_chain));

    if( LM_RXQ_IS_CHAIN_SGE_VALID(pdev, cid ) )
    {
        DbgBreakIf( !lm_bd_chains_are_consistent( &rxq_chain->chain_arr[LM_RXQ_CHAIN_IDX_BD], &rxq_chain->chain_arr[LM_RXQ_CHAIN_IDX_SGE]) );
    }

    DbgMessage3(pdev, INFORMi, "rcq %d, bd_chain %p, bd_left %d\n", cid,
                                                                    rcq_chain->bd_chain.next_bd,
                                                                    rcq_chain->bd_chain.bd_left);
    DbgMessage2(pdev, INFORMi, "   bd_chain_phy 0x%x%08x\n", rcq_chain->bd_chain.bd_chain_phy.as_u32.high,
                                                             rcq_chain->bd_chain.bd_chain_phy.as_u32.low);

    // Assign the RCQ chain consumer pointer to the consumer index in the status block.
    if (cid == ISCSI_CID(pdev))
    {
        sp_sb->index_values[HC_SP_INDEX_ETH_ISCSI_RX_CQ_CONS] = 0;
        rcq_chain->hw_con_idx_ptr                             = &(sp_sb->index_values[HC_SP_INDEX_ETH_ISCSI_RX_CQ_CONS]);
        rcq_chain->hc_sb_info.hc_sb                           = STATUS_BLOCK_SP_SL_TYPE;
        rcq_chain->hc_sb_info.hc_index_value                  = HC_SP_INDEX_ETH_ISCSI_RX_CQ_CONS;
    }
    else if (cid == FCOE_CID(pdev))
    {
        sp_sb->index_values[HC_SP_INDEX_ETH_FCOE_RX_CQ_CONS] = 0;
        rcq_chain->hw_con_idx_ptr                             = &(sp_sb->index_values[HC_SP_INDEX_ETH_FCOE_RX_CQ_CONS]);
        rcq_chain->hc_sb_info.hc_sb                           = STATUS_BLOCK_SP_SL_TYPE;
        rcq_chain->hc_sb_info.hc_index_value                  = HC_SP_INDEX_ETH_FCOE_RX_CQ_CONS;
    }
    else if (cid == OOO_CID(pdev))
    {
        // Any SB that isn't RSS share the same SB.
        // basically we will want the ISCSI OOO to work on the same SB that ISCSI works.(This does happen see the line above)
        // Even if we want to count on ISCSI and make sure we will work on the same SB:
        // 1.There is no promise on the order the ISCSI nminiport will call
        // ISCSI_KWQE_OPCODE_INIT1 (lm_sc_init inits pdev->iscsi_info.l5_eq_base_chain_idx) or
        // 2.OOO is general code that doesn't depend on a protocol (ISCSI).

        //TODO_OOO Ask Michal regarding E2 if we need LM_FW_SB_ID
        u32_t sb_id                                 = LM_OOO_SB(pdev);
        sb_indexes                                  = lm_get_sb_indexes(pdev, (u8_t)sb_id);
        sb_indexes[HC_INDEX_ISCSI_OOO_CONS]         = 0;
        rcq_chain->hw_con_idx_ptr                   = sb_indexes + HC_INDEX_ISCSI_OOO_CONS;
        rcq_chain->hc_sb_info.hc_sb                 = STATUS_BLOCK_NORMAL_SL_TYPE;
        rcq_chain->hc_sb_info.hc_index_value        = HC_INDEX_ISCSI_OOO_CONS;
    }
    else /* NDIS */
    {
        u32_t sb_id = RSS_ID_TO_SB_ID(CID_TO_RSS_ID(cid));
        const u8_t byte_counter_id = CHIP_IS_E1x(pdev)? LM_FW_SB_ID(pdev, sb_id) : LM_FW_DHC_QZONE_ID(pdev, sb_id);

        if( sb_id >= ARRSIZE(pdev->vars.status_blocks_arr) )
        {
            DbgBreakIfAll( sb_id >= ARRSIZE(pdev->vars.status_blocks_arr) ) ;
            return LM_STATUS_FAILURE ;
        }

        sb_indexes = lm_get_sb_indexes(pdev, (u8_t)sb_id);
        sb_indexes[HC_INDEX_ETH_RX_CQ_CONS] = 0;
        rcq_chain->hw_con_idx_ptr = sb_indexes + HC_INDEX_ETH_RX_CQ_CONS;
        rcq_chain->hc_sb_info.hc_sb = STATUS_BLOCK_NORMAL_SL_TYPE;
        rcq_chain->hc_sb_info.hc_index_value = HC_INDEX_ETH_RX_CQ_CONS;
        if (IS_PFDEV(pdev))
        {
            rcq_chain->hc_sb_info.iro_dhc_offset = CSTORM_BYTE_COUNTER_OFFSET(byte_counter_id, HC_INDEX_ETH_RX_CQ_CONS);
        }
        else
        {
            DbgMessage(pdev, FATAL, "Dhc not implemented for VF yet\n");
        }
    }

    return LM_STATUS_SUCCESS;
} /* lm_setup_rcq */

lm_status_t lm_setup_sq(struct _lm_device_t *pdev)
{
    lm_sq_info_t * sq_info = &pdev->sq_info;

    mm_mem_zero((void *) (sq_info->sq_chain.sq_chain_virt), LM_PAGE_SIZE);

    pdev->sq_info.num_pending_normal = MAX_NORMAL_PRIORITY_SPE;
    pdev->sq_info.num_pending_high = MAX_HIGH_PRIORITY_SPE;

    d_list_init(&pdev->sq_info.pending_normal, 0,0,0);
    d_list_init(&pdev->sq_info.pending_high, 0,0,0);
    d_list_init(&pdev->sq_info.pending_complete, 0,0,0);


    /* The spq dont have next bd */
    pdev->sq_info.sq_chain.bd_left =  USABLE_BDS_PER_PAGE(sizeof(struct slow_path_element), TRUE); /* prod == cons means empty chain */
    pdev->sq_info.sq_chain.con_idx = 0;

    pdev->sq_info.sq_chain.prod_bd = pdev->sq_info.sq_chain.sq_chain_virt;
    pdev->sq_info.sq_chain.last_bd = pdev->sq_info.sq_chain.prod_bd + pdev->sq_info.sq_chain.bd_left ;
    pdev->sq_info.sq_chain.prod_idx = 0;

    return LM_STATUS_SUCCESS;

}

lm_status_t lm_setup_eq(struct _lm_device_t *pdev)
{
    lm_bd_chain_t * bd_chain = NULL;
    lm_eq_chain_t * eq_chain = NULL;
    volatile struct hc_sp_status_block * sp_sb = NULL;


    /* check arguments */
    if(CHK_NULL(pdev))
    {
        return LM_STATUS_FAILURE;
    }

    DbgMessage(pdev, INFORMeq, "#lm_setup_eq\n");

    eq_chain = &pdev->eq_info.eq_chain;
    bd_chain = &eq_chain->bd_chain;

    lm_bd_chain_setup(pdev, bd_chain, bd_chain->bd_chain_virt, bd_chain->bd_chain_phy, bd_chain->page_cnt, sizeof(union event_ring_elem), /* is full? */TRUE, TRUE);

    sp_sb = lm_get_default_status_block(pdev);

    sp_sb->index_values[HC_SP_INDEX_EQ_CONS] = 0;

    eq_chain->hw_con_idx_ptr = &sp_sb->index_values[HC_SP_INDEX_EQ_CONS];
    eq_chain->hc_sb_info.hc_sb = STATUS_BLOCK_NORMAL_SL_TYPE;
    eq_chain->hc_sb_info.hc_index_value = HC_SP_INDEX_EQ_CONS;
    eq_chain->iro_prod_offset = CSTORM_EVENT_RING_PROD_OFFSET(FUNC_ID(pdev));

    return LM_STATUS_SUCCESS;
}

lm_status_t lm_init_sp_objs(struct _lm_device_t *pdev)
{
    u32_t lm_cli_idx = LM_CLI_IDX_MAX;

    ecore_init_mac_credit_pool(pdev, &pdev->slowpath_info.macs_pool, FUNC_ID(pdev), pdev->params.vnics_per_port);
    ecore_init_vlan_credit_pool(pdev, &pdev->slowpath_info.vlans_pool, FUNC_ID(pdev), pdev->params.vnics_per_port);
    ecore_init_rx_mode_obj(pdev, &pdev->slowpath_info.rx_mode_obj);

    for (lm_cli_idx=0; lm_cli_idx < ARRSIZE(pdev->slowpath_info.mcast_obj); lm_cli_idx++)
    {
        ecore_init_mcast_obj(pdev,
                             &pdev->slowpath_info.mcast_obj[lm_cli_idx],
                             LM_FW_CLI_ID(pdev, pdev->params.map_client_to_cid[lm_cli_idx]),
                             pdev->params.map_client_to_cid[lm_cli_idx],
                             FUNC_ID(pdev),
                             FUNC_ID(pdev),
                             LM_SLOWPATH(pdev, mcast_rdata)[lm_cli_idx],
                             LM_SLOWPATH_PHYS(pdev, mcast_rdata)[lm_cli_idx],
                             ECORE_FILTER_MCAST_PENDING,
                             (unsigned long *)&pdev->slowpath_info.sp_mcast_state[lm_cli_idx],
                             ECORE_OBJ_TYPE_RX_TX);
    }

    ecore_init_rss_config_obj(pdev,
                              &pdev->slowpath_info.rss_conf_obj,
                              LM_FW_CLI_ID(pdev, LM_SW_LEADING_RSS_CID(pdev)),
                              LM_SW_LEADING_RSS_CID(pdev),
                              FUNC_ID(pdev),
                              FUNC_ID(pdev),
                              LM_SLOWPATH(pdev, rss_rdata),
                              LM_SLOWPATH_PHYS(pdev, rss_rdata),
                              ECORE_FILTER_RSS_CONF_PENDING,
                              (unsigned long *)&pdev->slowpath_info.sp_rss_state,
                              ECORE_OBJ_TYPE_RX);

    return LM_STATUS_SUCCESS;
}

/**
 * Description:
 *   allocate slowpath resources
 */
static lm_status_t
lm_alloc_setup_slowpath_resc(struct _lm_device_t *pdev , u8_t b_alloc)
{
    lm_slowpath_data_t *slowpath_data = &pdev->slowpath_info.slowpath_data;
    u8_t                i             = 0;

    ASSERT_STATIC(ARRSIZE(slowpath_data->mac_rdata) == ARRSIZE(slowpath_data->rx_mode_rdata));
    ASSERT_STATIC(ARRSIZE(slowpath_data->mac_rdata) == ARRSIZE(slowpath_data->mcast_rdata));

    for (i = 0; i < ARRSIZE(slowpath_data->mac_rdata); i++ )
    {
        if (b_alloc)
    {
            slowpath_data->mac_rdata[i] =
                mm_alloc_phys_mem(pdev,
                                  sizeof(*slowpath_data->mac_rdata[i]),
                                  &slowpath_data->mac_rdata_phys[i],
                                  0,
                                  LM_RESOURCE_COMMON);

            slowpath_data->rx_mode_rdata[i] =
                mm_alloc_phys_mem(pdev,
                                  sizeof(*slowpath_data->rx_mode_rdata[i]),
                                  &slowpath_data->rx_mode_rdata_phys[i],
                                  0,
                                  LM_RESOURCE_COMMON);

            slowpath_data->mcast_rdata[i] =
                mm_alloc_phys_mem(pdev,
                                  sizeof(*slowpath_data->mcast_rdata[i]),
                                  &slowpath_data->mcast_rdata_phys[i],
                                  0,
                                  LM_RESOURCE_COMMON);


    }

        if (CHK_NULL(slowpath_data->mac_rdata[i]) ||
            CHK_NULL(slowpath_data->rx_mode_rdata[i]) ||
            CHK_NULL(slowpath_data->mcast_rdata[i]))

        {
            DbgBreakIf(DBG_BREAK_ON(MEMORY_ALLOCATION_FAILURE));
            return LM_STATUS_RESOURCE ;
        }

        mm_mem_zero(slowpath_data->mac_rdata[i], sizeof(*slowpath_data->mac_rdata[i]));
        mm_mem_zero(slowpath_data->rx_mode_rdata[i], sizeof(*slowpath_data->rx_mode_rdata[i]));
        mm_mem_zero(slowpath_data->mcast_rdata[i], sizeof(*slowpath_data->mcast_rdata[i]));
    }

    if (b_alloc)
    {
        slowpath_data->rss_rdata  = mm_alloc_phys_mem(pdev, sizeof(*slowpath_data->rss_rdata), &slowpath_data->rss_rdata_phys, 0, LM_RESOURCE_COMMON);
    }

    if CHK_NULL(slowpath_data->rss_rdata)
    {
        DbgBreakIf(DBG_BREAK_ON(MEMORY_ALLOCATION_FAILURE));
        return LM_STATUS_RESOURCE ;
    }

    mm_mem_zero(slowpath_data->rss_rdata, sizeof(*slowpath_data->rss_rdata));

    if (b_alloc)
    {
        slowpath_data->func_start_data  = mm_alloc_phys_mem(pdev, sizeof(*slowpath_data->func_start_data), &slowpath_data->func_start_data_phys, 0, LM_RESOURCE_COMMON);
    }

    if CHK_NULL(slowpath_data->func_start_data)
    {
        DbgBreakIf(DBG_BREAK_ON(MEMORY_ALLOCATION_FAILURE));
        return LM_STATUS_RESOURCE ;
    }

    mm_mem_zero(slowpath_data->func_start_data, sizeof(*slowpath_data->func_start_data));

    if (b_alloc)
    {
        slowpath_data->niv_function_update_data = mm_alloc_phys_mem(pdev, sizeof(*slowpath_data->niv_function_update_data), &slowpath_data->niv_function_update_data_phys, 0, LM_RESOURCE_COMMON);
    }
    if CHK_NULL(slowpath_data->niv_function_update_data)
    {
        DbgBreakIf(DBG_BREAK_ON(MEMORY_ALLOCATION_FAILURE));
        return LM_STATUS_RESOURCE ;
    }
    mm_mem_zero(slowpath_data->niv_function_update_data, sizeof(*slowpath_data->niv_function_update_data));

    pdev->slowpath_info.niv_ramrod_state = NIV_RAMROD_NOT_POSTED;

    return LM_STATUS_SUCCESS ;
}


static void * lm_setup_allocate_ilt_client_page( struct _lm_device_t *pdev,
    lm_address_t        *phys_mem,
                                                 u8_t const          cli_idx )
{
    void* ilt_client_page_virt_address = NULL;

    if (!CHIP_IS_E1(pdev))
    {
        ilt_client_page_virt_address = mm_alloc_phys_mem_align( pdev,
                                                                   pdev->params.ilt_client_page_size,
                                                                   phys_mem,
                                                                   LM_ILT_ALIGNMENT,
                                                                   0,
                                                                   cli_idx);
    }
    else
    {
        ilt_client_page_virt_address = mm_alloc_phys_mem_align(pdev,
                                                                   pdev->params.ilt_client_page_size,
                                                                   phys_mem,
                                                                   pdev->params.ilt_client_page_size,
                                                                   0,
                                                                   cli_idx);
    }

    return ilt_client_page_virt_address;
}

/* Description:
*    This routine contain common code for alloc/setup distinguish by flag
*/
lm_status_t lm_common_setup_alloc_resc(struct _lm_device_t *pdev, u8_t const b_is_alloc )
{
    lm_params_t*    params     = NULL ;
    lm_variables_t* vars       = NULL ;
//    lm_sq_info_t*   sq_info    = NULL ;
    lm_status_t     lm_status;
    u32_t           alloc_size = 0 ;
    u32_t           alloc_num  = 0 ;
    u32_t           i          = 0 ;
    u32_t           mem_size   = 0 ;
    u8_t            sb_id      = 0 ;
    u8_t            mm_cli_idx = 0 ;
    lm_address_t    sb_phy_address;

    if CHK_NULL( pdev )
    {
        return LM_STATUS_INVALID_PARAMETER ;
    }

    DbgMessage1(pdev, INFORMi , "### lm_common_setup_alloc_resc b_is_alloc=%s\n", b_is_alloc ? "TRUE" : "FALSE" );

    params     = &pdev->params ;
    vars       = &(pdev->vars) ;

    //       Status blocks allocation. We allocate mem both for the default and non-default status blocks
    //       there is 1 def sb and 16 non-def sb per port.
    //       non-default sb: index 0-15, default sb: index 16.
    if (CHIP_IS_E1x(pdev))
    {
        mem_size = E1X_STATUS_BLOCK_BUFFER_SIZE;
    }
    else
    {
        mem_size = E2_STATUS_BLOCK_BUFFER_SIZE;
    }

    mm_cli_idx = LM_RESOURCE_COMMON;//!!DP mm_cli_idx_to_um_idx(LM_CLI_IDX_MAX);

    LM_FOREACH_SB_ID(pdev, sb_id)
    {
        if( b_is_alloc )
        {
            vars->status_blocks_arr[sb_id].host_hc_status_block.e1x_sb = mm_alloc_phys_mem(pdev, mem_size, &sb_phy_address, 0, mm_cli_idx);
            if (CHIP_IS_E1x(pdev))
            {
                vars->status_blocks_arr[sb_id].hc_status_block_data.e1x_sb_data.common.host_sb_addr.lo = sb_phy_address.as_u32.low;
                vars->status_blocks_arr[sb_id].hc_status_block_data.e1x_sb_data.common.host_sb_addr.hi = sb_phy_address.as_u32.high;
            }
            else
            {
                vars->status_blocks_arr[sb_id].hc_status_block_data.e2_sb_data.common.host_sb_addr.lo = sb_phy_address.as_u32.low;
                vars->status_blocks_arr[sb_id].hc_status_block_data.e2_sb_data.common.host_sb_addr.hi = sb_phy_address.as_u32.high;
            }
        }
        if CHK_NULL(vars->status_blocks_arr[sb_id].host_hc_status_block.e1x_sb)
        {
            DbgBreakIf(DBG_BREAK_ON(MEMORY_ALLOCATION_FAILURE));
            return LM_STATUS_RESOURCE ;
        }
        mm_mem_zero((void *)(vars->status_blocks_arr[sb_id].host_hc_status_block.e1x_sb), mem_size);
    }

    mem_size = DEF_STATUS_BLOCK_BUFFER_SIZE;


    if( b_is_alloc )
    {
        vars->gen_sp_status_block.hc_sp_status_blk = mm_alloc_phys_mem(pdev,
                                                    mem_size,
                                                    &(vars->gen_sp_status_block.blk_phy_address),
                                                    0,
                                                    mm_cli_idx);
    }

    if CHK_NULL(vars->gen_sp_status_block.hc_sp_status_blk)
    {
        DbgBreakIf(DBG_BREAK_ON(MEMORY_ALLOCATION_FAILURE));
        return LM_STATUS_RESOURCE ;
    }

    mm_mem_zero((void *)(vars->gen_sp_status_block.hc_sp_status_blk), mem_size);

    /* Now reset the status-block ack values back to zero. */
    lm_reset_sb_ack_values(pdev);

    /* Register common and ethernet connection types completion callback. */
    lm_sq_comp_cb_register(pdev, ETH_CONNECTION_TYPE, lm_eth_comp_cb);
    lm_sq_comp_cb_register(pdev, NONE_CONNECTION_TYPE, lm_eq_comp_cb);

    /* SlowPath Info */
    lm_status = lm_alloc_setup_slowpath_resc(pdev, b_is_alloc);
    if (lm_status != LM_STATUS_SUCCESS)
    {
        DbgMessage1(pdev, FATAL, "lm_alloc_client_info failed lm-status = %d\n", lm_status);
        return lm_status;
    }


    /* Client Info */
    if( b_is_alloc )
    {
        lm_status = lm_alloc_client_info(pdev);
        if (lm_status != LM_STATUS_SUCCESS)
        {
            DbgMessage1(pdev, FATAL, "lm_alloc_client_info failed lm-status = %d\n", lm_status);
            return lm_status;
        }
    }

    lm_status = lm_setup_client_info(pdev);
    if (lm_status != LM_STATUS_SUCCESS)
    {
        DbgMessage1(pdev, FATAL, "lm_setup_client_info failed lm-status = %d\n", lm_status);
        return lm_status;
    }

    //  Context (roundup ( MAX_CONN / CONN_PER_PAGE) We may configure the CDU to have more than max_func_connections, specifically, we will
    // configure the CDU to have max_port_connections since it is a per-port register and not per-func, but it is OK to allocate
    // less for the cdu, and allocate only what will be used in practice - which is what is configured in max_func_connectinos.
    alloc_num = vars->context_cdu_num_pages = (params->max_func_connections / params->num_context_in_page) +
        ((params->max_func_connections % params->num_context_in_page)? 1:0);

    //TODO: optimize the roundup
    //TODO: assert that we did not go over the limit

    // allocate buffer pointers
    if( b_is_alloc )
    {
        mem_size = alloc_num * sizeof(void *) ;
        vars->context_cdu_virt_addr_table = (void **) mm_alloc_mem( pdev, mem_size, mm_cli_idx );
    }
    if CHK_NULL( vars->context_cdu_virt_addr_table )
    {
        DbgBreakIf(DBG_BREAK_ON(MEMORY_ALLOCATION_FAILURE));
        return LM_STATUS_RESOURCE ;
    }
    else if ( b_is_alloc )
    {
        mm_mem_zero( vars->context_cdu_virt_addr_table, mem_size ) ;
    }

    if( b_is_alloc )
    {
        mem_size = alloc_num * sizeof(lm_address_t) ;
        vars->context_cdu_phys_addr_table = mm_alloc_mem(pdev, mem_size, mm_cli_idx );
    }

    if CHK_NULL( vars->context_cdu_phys_addr_table )
    {
        DbgBreakIf(DBG_BREAK_ON(MEMORY_ALLOCATION_FAILURE));
        return LM_STATUS_RESOURCE ;
    }
    else if ( b_is_alloc )
    {
        mm_mem_zero(vars->context_cdu_phys_addr_table, mem_size );
    }

    /* TBD: for starters, we'll just allocate each page seperatly, to save space in the future, we may want */
    for( i = 0  ;i < alloc_num; i++)
    {
        if( b_is_alloc )
        {
            vars->context_cdu_virt_addr_table[i] = lm_setup_allocate_ilt_client_page(pdev,
                                                                                     (lm_address_t*)&vars->context_cdu_phys_addr_table[i],
                                                         mm_cli_idx);
        }
        if CHK_NULL( vars->context_cdu_virt_addr_table[i] )
        {
            DbgBreakIf(DBG_BREAK_ON(MEMORY_ALLOCATION_FAILURE));
            return LM_STATUS_RESOURCE ;
        }
        mm_mem_zero( vars->context_cdu_virt_addr_table[i], params->ilt_client_page_size ) ;
    }


    //  Searcher T1  (roundup to log2 of 64*MAX_CONN), T2 is 1/4 of T1. The searcher has a 'per-function' register we configure
    // with the number of max connections, therefore, we use the max_func_connections. It can be different per function and independent
    // from what we configure for qm/timers/cdu.
    alloc_size = (log2_align(max(params->max_func_connections,(u32_t)1000))*64);
    alloc_num = vars->searcher_t1_num_pages = max((alloc_size / params->ilt_client_page_size),(u32_t)1);
    mem_size = alloc_num * sizeof(void *) ;

    if( b_is_alloc )
    {
        vars->searcher_t1_virt_addr_table = (void **) mm_alloc_mem(pdev, mem_size, mm_cli_idx);
    }
    if CHK_NULL(vars->searcher_t1_virt_addr_table)
    {
        DbgBreakIf(DBG_BREAK_ON(MEMORY_ALLOCATION_FAILURE));
        return LM_STATUS_RESOURCE ;
    }
    else if ( b_is_alloc )
    {
        mm_mem_zero( vars->searcher_t1_virt_addr_table, mem_size ) ;
    }

    mem_size = alloc_num * sizeof(lm_address_t) ;

    if( b_is_alloc )
    {
        vars->searcher_t1_phys_addr_table = mm_alloc_mem(pdev, mem_size, mm_cli_idx );
    }
    if CHK_NULL(vars->searcher_t1_phys_addr_table)
    {
        DbgBreakIf(DBG_BREAK_ON(MEMORY_ALLOCATION_FAILURE));
        return LM_STATUS_RESOURCE ;
    }
    else if ( b_is_alloc )
    {
        mm_mem_zero( vars->searcher_t1_phys_addr_table, mem_size ) ;
    }

    for( i = 0  ; i < alloc_num; i++ )
    {
        if( b_is_alloc )
        {
            vars->searcher_t1_virt_addr_table[i] = lm_setup_allocate_ilt_client_page(pdev,
                                                         (lm_address_t*)&(vars->searcher_t1_phys_addr_table[i]),
                                                         mm_cli_idx);
        }
        if CHK_NULL( vars->searcher_t1_virt_addr_table[i] )
        {
            DbgBreakIf(DBG_BREAK_ON(MEMORY_ALLOCATION_FAILURE));
            return LM_STATUS_RESOURCE ;
        }
        mm_mem_zero( vars->searcher_t1_virt_addr_table[i], params->ilt_client_page_size ) ;
    }

    // allocate searcher T2 table
    // T2 does not entered into the ILT)
    alloc_size = (params->max_func_connections + 4)*64;
    alloc_num = vars->searcher_t2_num_pages = alloc_size / params->ilt_client_page_size +
        ((alloc_size % params->ilt_client_page_size)? 1:0) ;
    mem_size = alloc_num * sizeof(void *) ;

    if ( b_is_alloc )
    {
        vars->searcher_t2_virt_addr_table = (void **) mm_alloc_mem(pdev, mem_size, mm_cli_idx) ;
    }
    if CHK_NULL(vars->searcher_t2_virt_addr_table)
    {
        DbgBreakIf(DBG_BREAK_ON(MEMORY_ALLOCATION_FAILURE));
        return LM_STATUS_RESOURCE ;
    }
    else if (b_is_alloc)
    {
        mm_mem_zero( vars->searcher_t2_virt_addr_table, mem_size ) ;
    }

    mem_size = alloc_num * sizeof(lm_address_t) ;
    if (b_is_alloc)
    {
        vars->searcher_t2_phys_addr_table = mm_alloc_mem(pdev, mem_size, mm_cli_idx );
    }
    if CHK_NULL(vars->searcher_t2_phys_addr_table)
    {
        DbgBreakIf(DBG_BREAK_ON(MEMORY_ALLOCATION_FAILURE));
        return LM_STATUS_RESOURCE ;
    }

    for( i = 0  ; i < alloc_num; i++)
    {
        if (b_is_alloc )
        {
            vars->searcher_t2_virt_addr_table[i] = lm_setup_allocate_ilt_client_page(pdev,
                                                         (lm_address_t*)&(vars->searcher_t2_phys_addr_table[i]),
                                                         mm_cli_idx);
        }
        if CHK_NULL(vars->searcher_t2_virt_addr_table[i])
        {
            DbgBreakIf(DBG_BREAK_ON(MEMORY_ALLOCATION_FAILURE));
            return LM_STATUS_RESOURCE ;
        }
        mm_mem_zero( vars->searcher_t2_virt_addr_table[i],params->ilt_client_page_size ) ;
    }

    //  Timer block array (MAX_CONN*8) phys uncached. Timer block has a per-port register that defines it's size, and the amount of
    // memory we allocate MUST match this number, therefore we have to allocate the amount of max_port_connections.
    alloc_size = ( 8 * pdev->hw_info.max_port_conns);
    alloc_num = vars->timers_linear_num_pages = alloc_size / params->ilt_client_page_size +
        ((alloc_size % params->ilt_client_page_size)? 1:0) ;
    mem_size = alloc_num * sizeof(void *) ;

    if( b_is_alloc )
    {
        vars->timers_linear_virt_addr_table = (void **) mm_alloc_mem(pdev, mem_size, mm_cli_idx );
    }
    if CHK_NULL(vars->timers_linear_virt_addr_table)
    {
        DbgBreakIf(DBG_BREAK_ON(MEMORY_ALLOCATION_FAILURE));
        return LM_STATUS_RESOURCE ;
    }
    else if ( b_is_alloc )
    {
        mm_mem_zero( vars->timers_linear_virt_addr_table, mem_size ) ;
    }

    mem_size = alloc_num * sizeof(lm_address_t) ;

    if ( b_is_alloc )
    {
        vars->timers_linear_phys_addr_table = mm_alloc_mem(pdev, mem_size, mm_cli_idx );
    }
    if CHK_NULL(vars->timers_linear_phys_addr_table)
    {
        DbgBreakIf(DBG_BREAK_ON(MEMORY_ALLOCATION_FAILURE));
        return LM_STATUS_RESOURCE ;
    }
    else if ( b_is_alloc )
    {
        mm_mem_zero( vars->timers_linear_phys_addr_table, mem_size ) ;
    }

    for( i = 0  ;i < alloc_num; i++)
    {
        if( b_is_alloc )
        {
            vars->timers_linear_virt_addr_table[i] = lm_setup_allocate_ilt_client_page(pdev,
                                                           (lm_address_t*)&(vars->timers_linear_phys_addr_table[i]),
                                                           mm_cli_idx);
        }
        if CHK_NULL(vars->timers_linear_virt_addr_table[i])
        {
            DbgBreakIf(DBG_BREAK_ON(MEMORY_ALLOCATION_FAILURE));
            return LM_STATUS_RESOURCE ;
        }
        mm_mem_zero( vars->timers_linear_virt_addr_table[i], params->ilt_client_page_size ) ;
    }

    //  QM queues (128*MAX_CONN) QM has a per-port register that defines it's size, and the amount of
    // memory we allocate MUST match this number, therefore we have to allocate the amount of max_port_connections.
    alloc_size = ( 128 * pdev->hw_info.max_common_conns);
    alloc_num = vars->qm_queues_num_pages = alloc_size / params->ilt_client_page_size +
        ((alloc_size % params->ilt_client_page_size)? 1:0) ;
    mem_size = alloc_num * sizeof(void *) ;

    if( b_is_alloc )
    {
        vars->qm_queues_virt_addr_table = (void **) mm_alloc_mem(pdev, mem_size, mm_cli_idx );
    }
    if CHK_NULL(vars->qm_queues_virt_addr_table)
    {
        DbgBreakIf(DBG_BREAK_ON(MEMORY_ALLOCATION_FAILURE));
        return LM_STATUS_RESOURCE ;
    }
    else if (b_is_alloc)
    {
        mm_mem_zero( vars->qm_queues_virt_addr_table, mem_size ) ;
    }

    mem_size = alloc_num * sizeof(lm_address_t) ;

    if( b_is_alloc )
    {
        vars->qm_queues_phys_addr_table = mm_alloc_mem(pdev, mem_size, mm_cli_idx );
    }
    if CHK_NULL(vars->qm_queues_phys_addr_table)
    {
        DbgBreakIf(DBG_BREAK_ON(MEMORY_ALLOCATION_FAILURE));
        return LM_STATUS_RESOURCE ;
    }
    else if (b_is_alloc)
    {
        mm_mem_zero( vars->qm_queues_phys_addr_table, mem_size ) ;
    }

    for( i=0  ;i < alloc_num; i++)
    {
        if (b_is_alloc)
        {
            vars->qm_queues_virt_addr_table[i] = lm_setup_allocate_ilt_client_page(pdev,
                                                       (lm_address_t*)&(vars->qm_queues_phys_addr_table[i]),
                                                       mm_cli_idx);
        }
        if CHK_NULL( vars->qm_queues_virt_addr_table[i] )
        {
            DbgBreakIf(DBG_BREAK_ON(MEMORY_ALLOCATION_FAILURE));
            return LM_STATUS_RESOURCE ;
        }
        mm_mem_zero( vars->qm_queues_virt_addr_table[i],params->ilt_client_page_size ) ;
    }

    // common scratchpad buffer for dmae copies of less than 4 bytes
    if( b_is_alloc )
    {
        void *virt = mm_alloc_phys_mem(pdev,
                          8,
                          &params->dmae_copy_scratchpad_phys,
                          0,
                          mm_cli_idx);
        if CHK_NULL(virt)
        {
            DbgBreakIf(DBG_BREAK_ON(MEMORY_ALLOCATION_FAILURE));
            return LM_STATUS_RESOURCE ;
        }
    }

    return LM_STATUS_SUCCESS ;
}

lm_status_t ecore_resc_alloc(struct _lm_device_t * pdev);

/* Description:
*    This routine is called during driver initialization.  It is responsible
*    for allocating memory resources needed by the driver for common init.
*    This routine calls the following mm routines:
*    mm_alloc_mem, mm_alloc_phys_mem, and mm_init_packet_desc. */
lm_status_t lm_alloc_resc(struct _lm_device_t *pdev)
{
    lm_params_t*    params     = NULL ;
    lm_variables_t* vars       = NULL ;
    lm_status_t     lm_status  = LM_STATUS_SUCCESS ;
    u8_t            mm_cli_idx = 0;
    if CHK_NULL( pdev )
    {
        return LM_STATUS_INVALID_PARAMETER ;
    }
    DbgMessage(pdev, INFORMi , "### lm_alloc_resc\n");

#ifdef VF_INVOLVED
    if (IS_VFDEV(pdev)) {
        lm_status = lm_vf_init_dev_info(pdev);
        if (LM_STATUS_SUCCESS != lm_status)
            return lm_status;
    }
#endif

    params     = &pdev->params ;
    vars       = &(pdev->vars) ;

    mm_cli_idx = LM_CLI_IDX_MAX;//!!DP mm_cli_idx_to_um_idx(LM_CLI_IDX_MAX);

    // Cleaning after driver unload
    pdev->context_info = NULL;
    mm_mem_zero((void *) &pdev->cid_recycled_callbacks, sizeof(pdev->cid_recycled_callbacks));
    mm_mem_zero((void *) &pdev->toe_info, sizeof(pdev->toe_info));

    lm_status = lm_alloc_sq(pdev);
    if(LM_STATUS_SUCCESS != lm_status)
    {
        return lm_status;
    }

    /* alloc forward chain */
    pdev->tx_info.catchup_chain_idx = FWD_CID(pdev);
    if (IS_PFDEV(pdev))
    {
        /* Allocate Event-Queue: only the pf has an event queue */
        lm_status = lm_alloc_eq(pdev);
        if(LM_STATUS_SUCCESS != lm_status)
        {
            return lm_status;
        }

        pdev->tx_info.catchup_chain_idx = FWD_CID(pdev);

        lm_status = lm_alloc_txq(pdev, pdev->tx_info.catchup_chain_idx,
                                 (u16_t)params->l2_tx_bd_page_cnt[LM_CLI_IDX_FWD],
                                 (u16_t)params->l2_tx_coal_buf_cnt[LM_CLI_IDX_FWD]);
        if(LM_STATUS_SUCCESS != lm_status)
        {
            return lm_status;
        }
    }

    if (IS_PFDEV(pdev))
    {
        lm_status = lm_common_setup_alloc_resc(pdev, TRUE ) ;
    }
#ifdef VF_INVOLVED
    else
    {
        lm_status = lm_vf_setup_alloc_resc(pdev, TRUE);
    }
#endif

    if(LM_STATUS_SUCCESS != lm_status)
    {
        return lm_status;
    }

    if (IS_PFDEV(pdev)) {
        lm_status = lm_stats_alloc_resc( pdev ) ;
        if( LM_STATUS_SUCCESS != lm_status )
        {
            return lm_status ;
        }

        lm_status = lm_dmae_command_alloc(pdev,
                                          &pdev->vars.lm_dmae_ctx_arr[LM_DMAE_CMD_WB_ACCESS],
                                          DMAE_WB_ACCESS_FUNCTION_CMD(FUNC_ID(pdev)),
                                          DMAE_MAX_RW_SIZE(pdev) ) ;

        if( LM_STATUS_SUCCESS != lm_status )
        {
            return lm_status ;
        }

        lm_status = lm_dmae_command_alloc(pdev,
                                          &pdev->vars.lm_dmae_ctx_arr[LM_DMAE_CMD_PCI2PCI_COPY],
                                          DMAE_COPY_PCI_PCI_PORT_0_CMD + PORT_ID(pdev),
                                          DMAE_MAX_RW_SIZE(pdev) ) ;

        if( LM_STATUS_SUCCESS != lm_status )
        {
            return lm_status ;
        }

        // Init context allocation system
        lm_status = lm_alloc_context_pool(pdev);
        if( LM_STATUS_SUCCESS != lm_status )
        {
            DbgBreakIf( LM_STATUS_SUCCESS != lm_status ) ;
            return lm_status ;
        }
        //  CAM mirror?

        /* alloc for ecore */
        lm_status = ecore_resc_alloc(pdev);
        if( LM_STATUS_SUCCESS != lm_status )
        {
            DbgBreakIf( LM_STATUS_SUCCESS != lm_status ) ;
            return lm_status ;
        }
    }
    else if (IS_CHANNEL_VFDEV(pdev))
    {
        // Init context allocation system
        lm_status = lm_alloc_context_pool(pdev);
        if( LM_STATUS_SUCCESS != lm_status )
        {
            DbgBreakIf( LM_STATUS_SUCCESS != lm_status ) ;
            return lm_status ;
        }
    }
    DbgMessage(pdev, INFORMi , "### exit lm_alloc_resc\n");

    /* FIXME: (MichalS : should be called by um, but this requires lm-um api, so should rethink...) */
    lm_status = lm_init_sp_objs(pdev);
    if( LM_STATUS_SUCCESS != lm_status )
    {
        DbgBreakIf( LM_STATUS_SUCCESS != lm_status ) ;
        return lm_status ;
    }

    return lm_setup_resc(pdev);
}

/* Description:
*    This routine is called during driver initialization.  It is responsible
*    for initilazing  memory resources needed by the driver for common init.
*    This routine calls the following mm routines:
*    mm_alloc_mem, mm_alloc_phys_mem, and mm_init_packet_desc. */
lm_status_t lm_setup_resc(struct _lm_device_t *pdev)
{
    volatile struct hc_sp_status_block * sp_sb = NULL;
    lm_params_t *    params     = NULL ;
    lm_variables_t*  vars       = NULL ;
    lm_tx_info_t *   tx_info    = NULL ;
    lm_rx_info_t *   rx_info    = NULL ;
    u32_t            i          = 0 ;
    u32_t            j          = 0 ;
    lm_status_t      lm_status  = LM_STATUS_SUCCESS ;

    if CHK_NULL( pdev )
    {
        return LM_STATUS_INVALID_PARAMETER ;
    }

    params    = &pdev->params;
    vars      = &(pdev->vars);
    tx_info   = &pdev->tx_info;
    rx_info   = &pdev->rx_info;
    sp_sb     = lm_get_default_status_block(pdev);

    mm_mem_zero((void *) &pdev->cid_recycled_callbacks, sizeof(pdev->cid_recycled_callbacks));
    mm_mem_zero(rx_info->appr_mc.mcast_add_hash_bit_array, sizeof(rx_info->appr_mc.mcast_add_hash_bit_array));

    mm_mem_zero(&pdev->vars.nig_mirror, sizeof(lm_nig_mirror_t));

    pdev->vars.b_is_dmae_ready = FALSE ;

    if (IS_PFDEV(pdev)) {
        // adjust the FWD Tx ring consumer - default sb
        lm_status = lm_setup_txq(pdev, pdev->tx_info.catchup_chain_idx);
        if(LM_STATUS_SUCCESS != lm_status)
        {
            return lm_status;
        }
    }

    if (IS_PFDEV(pdev)) {
        /* setup mac flitering to drop all for all clients */
        // lm_status = lm_setup_tstorm_mac_filter(pdev); FIXME - necessary??
        if(LM_STATUS_SUCCESS != lm_status)
        {
            return lm_status;
        }
    }

    if (IS_PFDEV(pdev)) {
        lm_status = lm_common_setup_alloc_resc(pdev, FALSE ) ;
    }
#ifdef VF_INVOLVED
    else {
        lm_status = lm_vf_setup_alloc_resc(pdev, FALSE);
    }
#endif
    if(LM_STATUS_SUCCESS != lm_status)
    {
        return lm_status;
    }

    lm_status = lm_setup_sq(pdev);
    if(LM_STATUS_SUCCESS != lm_status)
    {
        return lm_status;
    }

    /* Only pfdev has an event-queue */
    if (IS_PFDEV(pdev))
    {
        lm_status = lm_setup_eq(pdev);
        if(LM_STATUS_SUCCESS != lm_status)
        {
            return lm_status;
        }
    }

    // Initialize T1
    if (IS_PFDEV(pdev)) {
        for( i = 0 ; i < vars->searcher_t1_num_pages ; i ++)
        {
            mm_mem_zero( vars->searcher_t1_virt_addr_table[i], params->ilt_client_page_size ) ;
        }

        // Initialize T2 first we make each next filed point to its address +1 then we fixup the edges
        for(i=0 ; i < vars->searcher_t2_num_pages ; i ++)
        {
            for (j=0; j < params->ilt_client_page_size; j+=64)
            {
                *(u64_t*)((char*)vars->searcher_t2_virt_addr_table[i]+j+56) = vars->searcher_t2_phys_addr_table[i].as_u64+j+64; //64bit pointer
            }
            // now fix up the last line in the block to point to the next block
            j = params->ilt_client_page_size - 8;

            if (i < vars->searcher_t2_num_pages -1)
            {
                // this is not the last block
                *(u64_t*)((char*)vars->searcher_t2_virt_addr_table[i]+j) = vars->searcher_t2_phys_addr_table[i+1].as_u64; //64bit pointer
            }
        }

        for( i=0  ;i < vars->timers_linear_num_pages; i++)
        {
            mm_mem_zero(vars->timers_linear_virt_addr_table[i],params->ilt_client_page_size);
        }

#if defined(EMULATION_DOORBELL_FULL_WORKAROUND)
        mm_atomic_set(&vars->doorbells_cnt, DOORBELL_CHECK_FREQUENCY);
#endif

        lm_status = lm_stats_hw_setup(pdev);
        if(lm_status != LM_STATUS_SUCCESS)
        {
            DbgMessage(pdev, WARN, "lm_stats_hw_setup failed.\n");
            return lm_status;
        }

        lm_stats_fw_setup(pdev);

        // init_context
        lm_status = lm_setup_context_pool(pdev) ;
        if(lm_status != LM_STATUS_SUCCESS)
        {
            DbgMessage(pdev, WARN, "lm_setup_context_pool failed.\n");
            return lm_status;
        }
    }
    else if (IS_CHANNEL_VFDEV(pdev))
    {
        lm_status = lm_setup_context_pool(pdev) ;
        if(lm_status != LM_STATUS_SUCCESS)
        {
            DbgMessage(pdev, WARN, "lm_setup_context_pool failed.\n");
            return lm_status;
        }
    }


    pdev->vars.mac_type = MAC_TYPE_NONE;
    pdev->vars.is_pmf = NOT_PMF;

    lm_set_int_coal_info(pdev);

    mm_mem_zero(&pdev->vars.nig_mirror, sizeof(pdev->vars.nig_mirror));

    return lm_status;
}

lm_status_t lm_chip_stop(struct _lm_device_t *pdev)
{
    lm_status_t lm_status   = LM_STATUS_SUCCESS;
    u32_t nig_entry_idx     = 0;
    const u32_t fwd_cid                     = FWD_CID(pdev);
    const u32_t MAX_OFFSET_IN_NIG_MEM1      = 8;
    const u32_t MAX_OFFSET_IN_NIG_MEM2      = MAX_MAC_OFFSET_IN_NIG - MAX_OFFSET_IN_NIG_MEM1;
    const u32_t nig_mem_enable_base_offset  = (PORT_ID(pdev) ? NIG_REG_LLH1_FUNC_MEM_ENABLE : NIG_REG_LLH0_FUNC_MEM_ENABLE);
    const u32_t nig_mem2_enable_base_offset = (PORT_ID(pdev) ? NIG_REG_P1_LLH_FUNC_MEM2_ENABLE : NIG_REG_P0_LLH_FUNC_MEM2_ENABLE);

#ifdef VF_INVOLVED
    if (IS_VFDEV(pdev)) {
        return lm_status;
    }
#endif
    if (lm_fl_reset_is_inprogress(pdev)) {
        pdev->vars.connections[fwd_cid].con_state = LM_CON_STATE_CLOSE;
        DbgMessage(pdev, FATAL, "lm_chip_stop: Under FLR: \"close\" leading and FWD conns.\n");
        return LM_STATUS_SUCCESS;
    }
    if ((lm_status = lm_close_forward_con(pdev)) != LM_STATUS_SUCCESS)
    {
        DbgMessage(pdev, FATAL, "lm_chip_stop: ERROR closing FWD connection!!!\n");
    }

    if (pdev->params.multi_vnics_mode)
    {
        /* before closing leading con (to avoid races), disable function llh in nig -
         for SD mode, clearing NIG_REG_LLH1_FUNC_EN is enough, but for NPAR mode we clear
         every NIG LLH entry by clearing NIG_REG_LLH1_FUNC_MEM_ENABLE for every entry in both
         nig mem1 and mem2.
        */
        REG_WR(pdev, (PORT_ID(pdev) ? NIG_REG_LLH1_FUNC_EN : NIG_REG_LLH0_FUNC_EN), 0);
        if (IS_MF_SI_MODE(pdev))
        {
            for (nig_entry_idx = 0; nig_entry_idx < MAX_OFFSET_IN_NIG_MEM1; ++nig_entry_idx)
            {
                REG_WR(pdev, nig_mem_enable_base_offset + nig_entry_idx*sizeof(u32_t), 0);
            }
            for (nig_entry_idx = 0; nig_entry_idx < MAX_OFFSET_IN_NIG_MEM2; ++nig_entry_idx)
            {
                REG_WR(pdev, nig_mem2_enable_base_offset + nig_entry_idx*sizeof(u32_t), 0);
            }
        }
    }

    if ((lm_status = lm_function_stop(pdev)) != LM_STATUS_SUCCESS)
    {
        DbgMessage(pdev, FATAL, "lm_chip_stop: ERROR closing function!!!\n");
        DbgBreak();
    }

    /* Function stop has been sent, we should now block slowpath commands  */
    lm_sq_change_state(pdev, SQ_STATE_BLOCKED);

    return lm_status;
}

/**
 * Function takes care of resetting everything related to the
 * function stage
 *
 * @param pdev
 * @param cleanup - this indicates whether we are in the last
 *                "Reset" function to be called, if so we need
 *                to do some cleanups here, otherwise they'll be
 *                done in later stages
 *
 * @return lm_status_t
 */
lm_status_t lm_reset_function_part(struct _lm_device_t *pdev, u8_t cleanup)
{
    /*It assumed that all protocols are down all unload ramrod already completed*/
    u32_t cnt = 0;
    u32_t val = 0;
    u8_t port = PORT_ID(pdev);

    if (IS_MULTI_VNIC(pdev) && IS_PMF(pdev)) {
        DbgMessage1(pdev, WARN,
                        "lm_reset_function_part: Func %d is no longer PMF \n", FUNC_ID(pdev));
        // disconnect from NIG attention
        if (INTR_BLK_TYPE(pdev) == INTR_BLK_HC) {
            REG_WR(pdev,  (PORT_ID(pdev) ? HC_REG_LEADING_EDGE_1 : HC_REG_LEADING_EDGE_0), 0);
            REG_WR(pdev,  (PORT_ID(pdev) ? HC_REG_TRAILING_EDGE_1 : HC_REG_TRAILING_EDGE_0), 0);
        } else {
            REG_WR(pdev,  IGU_REG_TRAILING_EDGE_LATCH, 0);
            REG_WR(pdev,  IGU_REG_LEADING_EDGE_LATCH, 0);
        }
        MM_ACQUIRE_PHY_LOCK(pdev);
        lm_stats_on_pmf_update(pdev,FALSE);
        MM_RELEASE_PHY_LOCK(pdev);
    }

    /*  Configure IGU */
    if (INTR_BLK_TYPE(pdev) == INTR_BLK_HC) {
        REG_WR(pdev,HC_REG_CONFIG_0+(4*port),0x1000);
    }

    /*  Timer stop scan.*/
    REG_WR(pdev,TM_REG_EN_LINEAR0_TIMER + (4*port),0);
    for(cnt = 0; cnt < LM_TIMERS_SCAN_POLL; cnt++)
    {
        mm_wait(pdev, LM_TIMERS_SCAN_TIME); /* 1m */

        val=REG_RD(pdev,TM_REG_LIN0_SCAN_ON+(4*port));
        if (!val)
        {
            break;
        }

        // in case reset in progress
        // we won't get completion so no need to wait
        if(CHIP_IS_E1x(pdev) && lm_reset_is_inprogress(pdev) )
        {
            break;
        }
    }
    /*timeout*/
    DbgMessage1(pdev,INFORMi,"timer status on %d \n",val);

    /* shutdown bug - in case of shutdown it's quite possible that the timer blocks hangs the scan never ends */
    if (!lm_reset_is_inprogress(pdev))
    {
        DbgBreakIf(cnt == LM_TIMERS_SCAN_POLL);
    }

    // reset the fw statistics (so next time client is up data will be correct)
    // if we don't call it here - we'll see in statistics 4GB+real
    lm_stats_fw_reset(pdev) ;

    /* Timers workaround bug: before cleaning the ilt we need to disable the pf-enable bit in the pglc + cfc */
    if (cleanup) { /* pdev->params.multi_vnics_mode, function that gets response "port/common" does this in the lm_reset_port_part  */
        if (!CHIP_IS_E1x(pdev)) {
            clear_pf_enable(pdev);
            pdev->vars.b_is_dmae_ready = FALSE; /* Can't access dmae since bus-master is disabled */
        }
        uninit_pxp2_blk(pdev);
    }
/*
Disable the function ibn STORMs
*/
    if (!lm_reset_is_inprogress(pdev)) {
        LM_INTMEM_WRITE8(pdev, XSTORM_FUNC_EN_OFFSET(FUNC_ID(pdev)), 0, BAR_XSTRORM_INTMEM);
        LM_INTMEM_WRITE8(pdev, CSTORM_FUNC_EN_OFFSET(FUNC_ID(pdev)), 0, BAR_CSTRORM_INTMEM);
        LM_INTMEM_WRITE8(pdev, TSTORM_FUNC_EN_OFFSET(FUNC_ID(pdev)), 0, BAR_TSTRORM_INTMEM);
        LM_INTMEM_WRITE8(pdev, USTORM_FUNC_EN_OFFSET(FUNC_ID(pdev)), 0, BAR_USTRORM_INTMEM);
    }

    return LM_STATUS_SUCCESS;
}

lm_status_t lm_reset_port_part(struct _lm_device_t *pdev)
{
    /*It assumed that all protocols are down all unload ramrod already completed*/
    u32_t val = 0;
    u8_t port = PORT_ID(pdev);
    /*  TODO Configure ACPI pattern if required. */
    /*  TODO Close the NIG port (also include congestion management toward XCM).*/
    // disable attention from nig
    REG_WR(pdev, NIG_REG_MASK_INTERRUPT_PORT0 + 4*port,0x0);

    // Do not rcv packets to BRB
    REG_WR(pdev, NIG_REG_LLH0_BRB1_DRV_MASK + 4*port,0x0);

    // Do not direct rcv packets that are not for MCP to the brb
    REG_WR(pdev, NIG_REG_LLH0_BRB1_NOT_MCP  + 4*32*port,0x0);

    // If DCBX is enabled we always want to go back to ETS disabled.
    // NIG is not reset
    if(IS_DCB_ENABLED(pdev))
    {
        elink_ets_disabled(&pdev->params.link);
    }

    // reset external phy to cause link partner to see link down
    MM_ACQUIRE_PHY_LOCK(pdev);
    lm_reset_link(pdev);
    MM_RELEASE_PHY_LOCK(pdev);
    /*  Configure AEU.*/
    REG_WR(pdev,MISC_REG_AEU_MASK_ATTN_FUNC_0+(4*port),0);

    /* shutdown bug - in case of shutdown don't bother with clearing the BRB or the ILT */
    if (!lm_reset_is_inprogress(pdev))
    {
        /*  Wait a timeout (100msec).*/
        mm_wait(pdev,LM_UNLOAD_TIME);
        /*  Check for BRB port occupancy. If BRB is not empty driver starts the ChipErrorRecovery routine.*/
        val=REG_RD(pdev,BRB1_REG_PORT_NUM_OCC_BLOCKS_0+(4*port));
        /* brb1 not empty */
        if (val)
        {
            DbgMessage1(pdev,INFORMi,"lm_reset_function_part BRB1 is not empty %d blooks are occupied\n",val);
            return LM_STATUS_TIMEOUT;
        }


        if (!CHIP_IS_E1x(pdev)) {
            clear_pf_enable(pdev);
            pdev->vars.b_is_dmae_ready = FALSE; /* Can't access dmae since bus-master is disabled */
        }
        /* link is closed and BRB is empty, can safely delete SRC ILT table: */
        uninit_pxp2_blk(pdev);

    }

    return LM_STATUS_SUCCESS;
}

// This function should be called only if we are on MCP lock
// This function should be called only on E1.5 or on E2 (width of PXP2_REG_PGL_PRETEND_FUNC_xx reg is 16bit)
lm_status_t lm_pretend_func( struct _lm_device_t *pdev, u16_t pretend_func_num )
{
    u32_t offset = 0;

    if (CHIP_IS_E1(pdev))
    {
        return LM_STATUS_FAILURE;
    }

    if(CHIP_IS_E1H(pdev) && (pretend_func_num >= E1H_FUNC_MAX))
    {
        return LM_STATUS_INVALID_PARAMETER;
    }

    switch (ABS_FUNC_ID(pdev))
    {
    case 0:
        offset = PXP2_REG_PGL_PRETEND_FUNC_F0;
        break;

    case 1:
        offset = PXP2_REG_PGL_PRETEND_FUNC_F1;
        break;

    case 2:
        offset = PXP2_REG_PGL_PRETEND_FUNC_F2;
        break;

    case 3:
        offset = PXP2_REG_PGL_PRETEND_FUNC_F3;
        break;

    case 4:
        offset = PXP2_REG_PGL_PRETEND_FUNC_F4;
        break;

    case 5:
        offset = PXP2_REG_PGL_PRETEND_FUNC_F5;
        break;

    case 6:
        offset = PXP2_REG_PGL_PRETEND_FUNC_F6;
        break;

    case 7:
        offset = PXP2_REG_PGL_PRETEND_FUNC_F7;
        break;

    default:
        break;
    }

    if( 0 == offset )
    {
        return LM_STATUS_INVALID_PARAMETER;
    }

    if(offset)
    {
        REG_WR(pdev, offset, pretend_func_num );
        REG_WAIT_VERIFY_VAL(pdev, offset, pretend_func_num, 200);
    }

    return LM_STATUS_SUCCESS;
}

#define NIG_REG_PORT_0_OFFSETS_VALUES { /* only for E1.5+ */ NIG_REG_LLH0_FUNC_EN,        \
                                        /* only for E1.5+ */ NIG_REG_LLH0_FUNC_VLAN_ID,   \
                                                             NIG_REG_LLH0_ACPI_ENABLE,    \
                                                             NIG_REG_LLH0_ACPI_PAT_0_LEN, \
                                                             NIG_REG_LLH0_ACPI_PAT_1_LEN, \
                                                             NIG_REG_LLH0_ACPI_PAT_2_LEN, \
                                                             NIG_REG_LLH0_ACPI_PAT_3_LEN, \
                                                             NIG_REG_LLH0_ACPI_PAT_4_LEN, \
                                                             NIG_REG_LLH0_ACPI_PAT_5_LEN, \
                                                             NIG_REG_LLH0_ACPI_PAT_6_LEN, \
                                                             NIG_REG_LLH0_ACPI_PAT_7_LEN, \
                                                             NIG_REG_LLH0_ACPI_PAT_0_CRC, \
                                                             NIG_REG_LLH0_ACPI_PAT_1_CRC, \
                                                             NIG_REG_LLH0_ACPI_PAT_2_CRC, \
                                                             NIG_REG_LLH0_ACPI_PAT_3_CRC, \
                                                             NIG_REG_LLH0_ACPI_PAT_4_CRC, \
                                                             NIG_REG_LLH0_ACPI_PAT_5_CRC, \
                                                             NIG_REG_LLH0_ACPI_PAT_6_CRC, \
                                                             NIG_REG_LLH0_ACPI_PAT_7_CRC }

#define NIG_REG_PORT_1_OFFSETS_VALUES { /* only for E1.5+ */ NIG_REG_LLH1_FUNC_EN,        \
                                        /* only for E1.5+ */ NIG_REG_LLH1_FUNC_VLAN_ID,   \
                                                             NIG_REG_LLH1_ACPI_ENABLE,    \
                                                             NIG_REG_LLH1_ACPI_PAT_0_LEN, \
                                                             NIG_REG_LLH1_ACPI_PAT_1_LEN, \
                                                             NIG_REG_LLH1_ACPI_PAT_2_LEN, \
                                                             NIG_REG_LLH1_ACPI_PAT_3_LEN, \
                                                             NIG_REG_LLH1_ACPI_PAT_4_LEN, \
                                                             NIG_REG_LLH1_ACPI_PAT_5_LEN, \
                                                             NIG_REG_LLH1_ACPI_PAT_6_LEN, \
                                                             NIG_REG_LLH1_ACPI_PAT_7_LEN, \
                                                             NIG_REG_LLH1_ACPI_PAT_0_CRC, \
                                                             NIG_REG_LLH1_ACPI_PAT_1_CRC, \
                                                             NIG_REG_LLH1_ACPI_PAT_2_CRC, \
                                                             NIG_REG_LLH1_ACPI_PAT_3_CRC, \
                                                             NIG_REG_LLH1_ACPI_PAT_4_CRC, \
                                                             NIG_REG_LLH1_ACPI_PAT_5_CRC, \
                                                             NIG_REG_LLH1_ACPI_PAT_6_CRC, \
                                                             NIG_REG_LLH1_ACPI_PAT_7_CRC }

typedef enum {
    LM_RESET_NIG_OP_SAVE      = 0,
    LM_RESET_NIG_OP_RESTORE   = 1,
    LM_RESET_NIG_OP_MAX       = 2
} lm_reset_nig_op_t;

static void lm_reset_nig_values_for_func_save_restore( IN struct _lm_device_t       *pdev,
                                                       IN  lm_reset_nig_op_t  const save_or_restore,
                                                       IN  u8_t               const pretend_func_id,
                                                       IN  u32_t              const reg_offsets_port[],
                                                       OUT u32_t                    reg_port_arr[],
                                                       IN  u32_t              const reg_port_arr_size,
                                                       IN  u8_t               const reg_port_start_idx,
                                                       IN  u32_t              const reg_port_wb_offset_base,
                                                       OUT u64_t                    reg_port_wb_arr[],
                                                       IN  u32_t              const reg_port_wb_arr_size )
{
    u32_t   offset      = 0;
    u32_t   val_32[2]   = {0};
    u32_t   idx         = 0;
    u8_t    abs_func_id = ABS_FUNC_ID(pdev);
    u8_t    b_save      = FALSE;

    switch(save_or_restore)
    {
    case LM_RESET_NIG_OP_SAVE:
        b_save = TRUE;
        break;

    case LM_RESET_NIG_OP_RESTORE:
        b_save = FALSE;
        break;

    default:
        DbgBreakIf(TRUE);
        break;
    }

    if( pretend_func_id != abs_func_id  )
    {
        lm_pretend_func( pdev, pretend_func_id );
    }

    for( idx = reg_port_start_idx; idx < reg_port_arr_size ; idx++ )
    {
        offset = reg_offsets_port[idx];
        if( b_save )
        {
            reg_port_arr[idx] = REG_RD(pdev, offset );
        }
        else
        {
            REG_WR(pdev, offset, reg_port_arr[idx] );
        }
    }

    for( idx = 0; idx < reg_port_wb_arr_size; idx++)
    {
        offset = reg_port_wb_offset_base + 8*idx;

        if( b_save)
        {
            REG_RD_IND( pdev,  offset,   &val_32[0] );
            REG_RD_IND( pdev,  offset+4, &val_32[1] );
            reg_port_wb_arr[idx] = HILO_U64( val_32[1], val_32[0] );
        }
        else
        {
            val_32[0] = U64_LO(reg_port_wb_arr[idx]);
            val_32[1] = U64_HI(reg_port_wb_arr[idx]);

            REG_WR_IND( pdev,  offset,   val_32[0] );
            REG_WR_IND( pdev,  offset+4, val_32[1] );
        }
    }

    if( pretend_func_id != abs_func_id  )
    {
        lm_pretend_func( pdev, abs_func_id );
    }
}
/*
   1. save known essential NIG values (port swap, WOL nwuf for all funcs)
   2. Pretend to relevant func - for split register as well
   3. Resets the device and the NIG.
   4. Restore known essential NIG values (port swap and WOL nwuf).
*/

void
lm_reset_device_with_nig(struct _lm_device_t *pdev)
{
    u8_t                          idx                                        = 0;
    u8_t                          idx_port                                   = 0;
    u8_t                          abs_func_vector                            = 0;
    u8_t                          abs_func_id                                = ABS_FUNC_ID(pdev); // for debugging only
    const u8_t                    port_id                                    = PORT_ID(pdev);
    const u8_t                    idx_max                                    = MAX_FUNC_NUM;
    const u8_t                    path_id                                    = PATH_ID(pdev);
    const u8_t                    reg_port_start_idx                         = CHIP_IS_E1(pdev) ? 2 : 0 ; // For e1.5 and above we need to save/restore NIG_REG_LLHX_FUNC_EN as well
    const u32_t                   chip_num                                   = CHIP_NUM(pdev);
    const lm_chip_port_mode_t     chip_port_mode                             = CHIP_PORT_MODE(pdev);
    static const u32_t            offset_base_wb[PORT_MAX]                   = { NIG_REG_LLH0_ACPI_BE_MEM_DATA, NIG_REG_LLH1_ACPI_BE_MEM_DATA };
    static const u32_t            reg_offsets_port0[]                        = NIG_REG_PORT_0_OFFSETS_VALUES;
    static const u32_t            reg_offsets_port1[]                        = NIG_REG_PORT_1_OFFSETS_VALUES;
    lm_reset_nig_op_t             lm_reset_nig_op                            = LM_RESET_NIG_OP_SAVE;
    u32_t                         non_split_offsets[3]                       = { NIG_REG_PORT_SWAP,
                                                                                 NIG_REG_STRAP_OVERRIDE,// List of registers that are "global" for all funcitons in path
                                                                                 NIG_REG_P0_ACPI_MF_GLOBAL_EN };
    u32_t                         non_split_vals[ARRSIZE(non_split_offsets)] = {0};
    static u64_t                  reg_nig_port_restore_wb[MAX_FUNC_NUM][NIG_REG_LLH0_ACPI_BE_MEM_DATA_SIZE/2] = {{0}} ; // the nwuf data
    static u32_t                  reg_nig_port_restore[MAX_FUNC_NUM][ARRSIZE(reg_offsets_port0)]              = {{0}};

    UNREFERENCED_PARAMETER_( abs_func_id );

    if( 1 == port_id )
    {
        // Can't init on decleration due to dos compiler error :-(
        non_split_offsets[2] = NIG_REG_P1_ACPI_MF_GLOBAL_EN; 
    }

    // Note:
    // Due to kernel stack limitation we use reg_nig_port_restore(_wb) as static variables.
    // At first glance, it doesn't look good BUT avoiding multiple access to the values is assured:
    //    mcp locking mechanism LOAD_COMMON etc

    // Currently we work with max 8 PF, in case of a change - need to verify code is still valid
    ASSERT_STATIC( 8 == MAX_FUNC_NUM );
    ASSERT_STATIC( 2 == PORT_MAX );

    // verify enum values
    ASSERT_STATIC( LM_RESET_NIG_OP_SAVE < LM_RESET_NIG_OP_RESTORE )
    ASSERT_STATIC( 2 == LM_RESET_NIG_OP_MAX );

    // verify that save/restores are same size as offsets range
    ASSERT_STATIC( ARRSIZE(reg_nig_port_restore[0]) == ARRSIZE(reg_offsets_port0) );
    ASSERT_STATIC( ARRSIZE(reg_nig_port_restore[1]) == ARRSIZE(reg_offsets_port1) );
    ASSERT_STATIC( NIG_REG_LLH0_ACPI_BE_MEM_DATA_SIZE == NIG_REG_LLH1_ACPI_BE_MEM_DATA_SIZE );

    abs_func_vector = lm_get_abs_func_vector( chip_num, chip_port_mode, IS_MULTI_VNIC(pdev), path_id );

    // start the "save/restore" operation
    for( lm_reset_nig_op = LM_RESET_NIG_OP_SAVE; lm_reset_nig_op <= LM_RESET_NIG_OP_RESTORE; lm_reset_nig_op++ )
    {
        for( idx = 0; idx < idx_max; idx++ )
        {
            // we skip non-marked functions
            if( 0 == GET_BIT( abs_func_vector, idx ) )
            {
                continue;
            }

            // choose the correct idx_port
            idx_port = PORT_ID_PARAM_FUNC_ABS( chip_num, chip_port_mode, idx );

            DbgBreakIf( idx_port >= PORT_MAX );

            // save for 1st iteariton
            // restore for 2nd iteration
            lm_reset_nig_values_for_func_save_restore( pdev,
                                                       lm_reset_nig_op,
                                                       idx,
                                                       idx_port ? reg_offsets_port1 : reg_offsets_port0,
                                                       reg_nig_port_restore[idx],
                                                       ARRSIZE(reg_nig_port_restore[idx]),
                                                       reg_port_start_idx,
                                                       offset_base_wb[idx_port],
                                                       reg_nig_port_restore_wb[idx],
                                                       ARRSIZE(reg_nig_port_restore_wb[idx]) );
        } // for func iterations

        // This code section should be done once and anyway!
        if ( LM_RESET_NIG_OP_SAVE == lm_reset_nig_op)
        {
            u8_t non_split_idx       = 0;
            u8_t non_split_idx_start = CHIP_IS_E1x(pdev) ? 0 : 2; // E1x 0,1 E2+ 2
            u8_t non_split_idx_max   = CHIP_IS_E1x(pdev) ? 1 : 2; // E1x 1   E2+ 2

            for( non_split_idx = non_split_idx_start; non_split_idx <= non_split_idx_max; non_split_idx++ )
            {
                non_split_vals[non_split_idx] = REG_RD( pdev, non_split_offsets[non_split_idx] );
            }

            //reset chip with NIG!!
            lm_reset_path( pdev, TRUE );

            // initalize relevant nig regiters so WoL will work
            init_nig_common_llh(pdev);

            init_nig_func(pdev);

            // save nig swap register and global acpi enable before NIG reset
            for( non_split_idx = non_split_idx_start; non_split_idx <= non_split_idx_max; non_split_idx++ )
            {
                REG_WR(pdev, non_split_offsets[non_split_idx], non_split_vals[non_split_idx]);
            }

        } // save iteartion only code

    } // for save/restore loop

} // lm_reset_device_with_nig

void
lm_reset_common_part(struct _lm_device_t *pdev)
{
    /* Reset the HW blocks that are listed in section 4.13.18.*/
    if (lm_pm_reset_is_inprogress(pdev))
    {
        /* In case of shutdown we reset the NIG as well */
        lm_reset_device_with_nig(pdev);
        return;
    }

    lm_reset_path( pdev, FALSE );
}

void lm_chip_reset(struct _lm_device_t *pdev, lm_reason_t reason)
{
    lm_loader_opcode       opcode = 0;
    lm_loader_response     resp   = 0;
    u32_t                  val    = 0;

#if defined(_NTDDK_)
    u32_t                  enabled_wols = mm_get_wol_flags(pdev);
#else
    u32_t                  enabled_wols = 0;
#endif // _NTDDK_

    DbgMessage(pdev, INFORMi , "### lm_chip_reset\n");

#ifdef VF_INVOLVED
    if (IS_VFDEV(pdev))
    {
        lm_status_t lm_status = lm_vf_chip_reset(pdev,reason);
        if (lm_status != LM_STATUS_SUCCESS)
        {
            DbgMessage(pdev, FATAL, "lm_chip_reset: ERROR resetting VF!!!\n");
            DbgBreakIfAll(1);
        }
        return;
    }
#endif

    // depends on reason, send relevant message to MCP
    switch( reason )
    {
    case LM_REASON_WOL_SUSPEND:
        opcode = LM_LOADER_OPCODE_UNLOAD_WOL_EN;
        break ;

    case LM_REASON_NO_WOL_SUSPEND:
        opcode = LM_LOADER_OPCODE_UNLOAD_WOL_DIS;
        break ;

    case LM_REASON_DRIVER_UNLOAD:
    case LM_REASON_DRIVER_UNLOAD_POWER_DOWN:
    case LM_REASON_DRIVER_SHUTDOWN:
        enabled_wols = LM_WAKE_UP_MODE_NONE; // in S5 default is by nvm cfg 19
        // in case we do support wol_cap, we ignore OS configuration and
        // we decide upon nvm settings (CQ49516 - S5 WOL functionality to always look at NVRAM WOL Setting)
        if( GET_FLAGS( pdev->hw_info.port_feature_config, PORT_FEATURE_WOL_ENABLED ) )
        {
            opcode = LM_LOADER_OPCODE_UNLOAD_WOL_EN ;
            // enabled_wols so the mac address will be written by lm_set_d3_mpkt()
            SET_FLAGS( enabled_wols, LM_WAKE_UP_MODE_MAGIC_PACKET );
        }
        else
        {
            opcode = LM_LOADER_OPCODE_UNLOAD_WOL_DIS ;
        }
        break;

    default:
        break;
    }

    if ( !CHIP_IS_E1(pdev) )
    {
          if (CHIP_IS_E2(pdev) || CHIP_IS_E1H(pdev))
          {
                val = REG_RD( pdev, MISC_REG_E1HMF_MODE);
          }
          else
          {
                ASSERT_STATIC(MISC_REG_E1HMF_MODE_P1 == (MISC_REG_E1HMF_MODE_P0 + 4));
                val = REG_RD( pdev, MISC_REG_E1HMF_MODE_P0 + PORT_ID(pdev)*4);
          }

        // We do expect that register value will be consistent with multi_vnics_mode.
        DbgBreakIf( pdev->params.multi_vnics_mode ^ val );
    }

    if (lm_fl_reset_is_inprogress(pdev))
    {
        if (TEST_MODE_NO_MCP == GET_FLAGS(pdev->params.test_mode, TEST_MODE_NO_MCP))
        {
            DbgMessage(pdev, FATAL,"lm_chip_reset under FLR: NO MCP\n");
            lm_loader_lock(pdev, opcode);
            lm_loader_unlock(pdev, opcode);
        }
        DbgMessage(pdev, FATAL,"lm_chip_reset under FLR: return\n");
        return;
    }

    // magic packet should be programmed before unload request send to MCP
    lm_set_d3_mpkt(pdev, enabled_wols) ;

    resp = lm_loader_lock(pdev, opcode ) ;

    // nwuf is programmed before chip reset since if we reset the NIG we resotre all function anyway
    lm_set_d3_nwuf(pdev, enabled_wols) ;

    switch (resp)
    {
    case LM_LOADER_RESPONSE_UNLOAD_FUNCTION:
        lm_reset_function_part(pdev, TRUE /* cleanup*/);
        break;
    case LM_LOADER_RESPONSE_UNLOAD_PORT:
        lm_reset_function_part(pdev, FALSE /* cleanup */ );
        lm_reset_port_part(pdev);
        break;
    case LM_LOADER_RESPONSE_UNLOAD_COMMON:
        lm_reset_function_part(pdev, FALSE /* cleanup */);
        lm_reset_port_part(pdev);
        //Check if there is dbus work
        mm_dbus_stop_if_started(pdev);
        lm_reset_common_part(pdev);
        break;
    default:
        DbgMessage1(pdev, WARN, "wrong loader response=0x%x\n", resp);
        DbgBreakIfAll(1);
    }

    pdev->vars.b_is_dmae_ready = FALSE ;

    // unset pmf flag needed for D3 state
    pdev->vars.is_pmf = NOT_PMF;

    resp = lm_loader_unlock(pdev, opcode ) ;

    if (resp != LM_LOADER_RESPONSE_UNLOAD_DONE )
    {
        DbgMessage1(pdev, WARN, "wrong loader response=0x%x\n", resp);
        DbgBreakIfAll(1);
    }
}

/*
 *------------------------------------------------------------------------
 * lm_gpio_read -
 *
 * Read the value of the requested GPIO pin (with pin_num)
 *
 *------------------------------------------------------------------------
 */
lm_status_t lm_gpio_read(struct _lm_device_t *pdev, u32_t pin_num, u32_t* value_ptr, u8_t port)
{
    u32_t reg_val       = 0;
    u32_t gpio_port     = 0;
    u32_t mask          = 0;
    u32_t swap_val      = 0;
    u32_t swap_override = 0;

    if ( CHK_NULL(pdev) || CHK_NULL(value_ptr) )
    {
        DbgBreakIf(!pdev);
        DbgBreakIf(!value_ptr);
        return LM_STATUS_INVALID_PARAMETER ;
    }

    if (pin_num > MISC_REGISTERS_GPIO_3)
    {
        DbgMessage1(pdev, FATAL , "Invalid pin_num GPIO %d\n", pin_num);
        return LM_STATUS_INVALID_PARAMETER;
    }

    /* The GPIO should be swapped if the swap register is set and active */
    swap_val      = REG_RD(pdev,  NIG_REG_PORT_SWAP);
    swap_override = REG_RD(pdev,  NIG_REG_STRAP_OVERRIDE);


    // define port upon swap
    gpio_port = (swap_val && swap_override) ^ port;

    // Get the current port number (0 or 1)
    if (gpio_port > 1)
    {
        return LM_STATUS_FAILURE;
    }

    // Calculate the value with relevent OE set to 1 (for input).
    // Calulate the mask for the read value.
    if (gpio_port == 0)
    {
        switch (pin_num)
        {
            case 0:
                mask = GRC_MISC_REGISTERS_GPIO_PORT0_VAL0;
                break;
            case 1:
                mask = GRC_MISC_REGISTERS_GPIO_PORT0_VAL1;
                break;
            case 2:
                mask = GRC_MISC_REGISTERS_GPIO_PORT0_VAL2;
                break;
            case 3:
                mask = GRC_MISC_REGISTERS_GPIO_PORT0_VAL3;
                break;
            default:
                break;
        }
    }
    // Calculate the value with relevent OE set to 1 (for input).
    // Calulate the mask for the read value.
    if (gpio_port == 1)
    {
        switch (pin_num)
        {
            case 0:
                mask = GRC_MISC_REGISTERS_GPIO_PORT1_VAL0;
                break;
            case 1:
                mask = GRC_MISC_REGISTERS_GPIO_PORT1_VAL1;
                break;
            case 2:
                mask = GRC_MISC_REGISTERS_GPIO_PORT1_VAL2;
                break;
            case 3:
                mask = GRC_MISC_REGISTERS_GPIO_PORT1_VAL3;
                break;
            default:
                break;
        }
    }

    // Read from MISC block the GPIO register
    reg_val = REG_RD(pdev, MISC_REG_GPIO);
    DbgMessage2(NULL, INFORM, "lm_gpio_read: MISC_REG_GPIO value 0x%x mask 0x%x\n", reg_val, mask);

    // Get the requested pin value by masking the val with mask
    if ((reg_val & mask) == mask)
    {
        *value_ptr = 1;
    }
    else
    {
        *value_ptr = 0;
    }
    DbgMessage2(NULL, INFORM, "lm_gpio_read: pin %d value is %x\n", pin_num, *value_ptr);

    return LM_STATUS_SUCCESS;
}

/*
 *------------------------------------------------------------------------
 * lm_gpio_write -
 *
 * Write a value to the requested GPIO pin (with pin_num)
 *
 *------------------------------------------------------------------------
 */
lm_status_t lm_gpio_write(struct _lm_device_t *pdev, u32_t pin_num, u32_t mode, u8_t port)
{
    u32_t gpio_port     = 0;
    u32_t gpio_shift    = 0;
    u32_t gpio_mask     = 0;
    u32_t gpio_reg      = 0;
    u32_t swap_val      = 0;
    u32_t swap_override = 0;

    if( CHK_NULL(pdev) )
    {
        DbgBreakIf(!pdev);
        return LM_STATUS_INVALID_PARAMETER ;
    }
    if (pin_num > MISC_REGISTERS_GPIO_3)
    {
	DbgMessage1(pdev, FATAL , "lm_gpio_write: Invalid pin_num GPIO %d\n", pin_num);
	return LM_STATUS_INVALID_PARAMETER;
    }

    /* The GPIO should be swapped if the swap register is set and active */
    swap_val      = REG_RD(pdev,  NIG_REG_PORT_SWAP);
    swap_override = REG_RD(pdev,  NIG_REG_STRAP_OVERRIDE);

    // define port upon swap
    gpio_port = (swap_val && swap_override) ^ port;

    // Get the current port number (0 or 1)
    if (gpio_port > 1) {
	return LM_STATUS_FAILURE;
    }

    gpio_shift = pin_num +
    		(gpio_port ? MISC_REGISTERS_GPIO_PORT_SHIFT : 0);

    gpio_mask = (1 << gpio_shift);
     
    // lock before read
    lm_hw_lock(pdev, HW_LOCK_RESOURCE_GPIO, TRUE);

    /* read GPIO and mask except the float bits */
    gpio_reg = (REG_RD(pdev, MISC_REG_GPIO) & MISC_REGISTERS_GPIO_FLOAT);
    
    switch (mode) {
    case MISC_REGISTERS_GPIO_OUTPUT_LOW:
    	DbgMessage2(NULL, WARN, "Set GPIO %d (shift %d) -> output low\n", pin_num, gpio_shift);
    	/* clear FLOAT and set CLR */
    	gpio_reg &= ~(gpio_mask << MISC_REGISTERS_GPIO_FLOAT_POS);
    	gpio_reg |=  (gpio_mask << MISC_REGISTERS_GPIO_CLR_POS);
    	break;
    
    case MISC_REGISTERS_GPIO_OUTPUT_HIGH:
    	DbgMessage2(NULL, WARN, "Set GPIO %d (shift %d) -> output high\n", pin_num, gpio_shift);
    	/* clear FLOAT and set SET */
    	gpio_reg &= ~(gpio_mask << MISC_REGISTERS_GPIO_FLOAT_POS);
    	gpio_reg |=  (gpio_mask << MISC_REGISTERS_GPIO_SET_POS);
    	break;
    
    case MISC_REGISTERS_GPIO_INPUT_HI_Z:
    	DbgMessage2(NULL, WARN, "Set GPIO %d (shift %d) -> input\n", pin_num, gpio_shift);
    	/* set FLOAT */
    	gpio_reg |= (gpio_mask << MISC_REGISTERS_GPIO_FLOAT_POS);
    	break;
    
    default:
    	break;
    }
    
    REG_WR(pdev, MISC_REG_GPIO, gpio_reg);
    lm_hw_unlock(pdev, HW_LOCK_RESOURCE_GPIO);

    return LM_STATUS_SUCCESS;
    

}

/*
 *------------------------------------------------------------------------
 * lm_gpio_int_write -
 *
 * Set or clear the requested GPIO pin (with pin_num)
 *
 *------------------------------------------------------------------------
 */

lm_status_t lm_gpio_int_write(struct _lm_device_t *pdev, u32_t pin_num, u32_t mode, u8_t port)
{
    /* The GPIO should be swapped if swap register is set and active */
    u32_t gpio_port;
    u32_t gpio_shift ;
    u32_t gpio_mask;
    u32_t gpio_reg;
    u32_t swap_val      = 0;
    u32_t swap_override = 0;

    swap_val      = REG_RD(pdev,  NIG_REG_PORT_SWAP);
    swap_override = REG_RD(pdev,  NIG_REG_STRAP_OVERRIDE);
    gpio_port     = (swap_val && swap_override ) ^ port;
    gpio_shift    = pin_num + (gpio_port ? MISC_REGISTERS_GPIO_PORT_SHIFT : 0);
    gpio_mask     = (1 << gpio_shift);

    if (pin_num > MISC_REGISTERS_GPIO_3)
    {
        DbgMessage1(pdev, FATAL , "lm_gpio_write: Invalid pin_num GPIO %d\n", pin_num);
        return LM_STATUS_INVALID_PARAMETER;
    }

    // lock before read
    lm_hw_lock(pdev, HW_LOCK_RESOURCE_GPIO, TRUE);

    /* read GPIO int */
    gpio_reg = REG_RD(pdev, MISC_REG_GPIO_INT);

    switch (mode)
    {
    case MISC_REGISTERS_GPIO_INT_OUTPUT_CLR:
        DbgMessage2(pdev, INFORM, "Clear GPIO INT %d (shift %d) -> output low\n",
           pin_num, gpio_shift);
        // clear SET and set CLR
        gpio_reg &= ~(gpio_mask << MISC_REGISTERS_GPIO_INT_SET_POS);
        gpio_reg |=  (gpio_mask << MISC_REGISTERS_GPIO_INT_CLR_POS);
        break;

    case MISC_REGISTERS_GPIO_INT_OUTPUT_SET:
        DbgMessage2(pdev, INFORM, "Set GPIO INT %d (shift %d) -> output high\n",
           pin_num, gpio_shift);
        // clear CLR and set SET
        gpio_reg &= ~(gpio_mask << MISC_REGISTERS_GPIO_INT_CLR_POS);
        gpio_reg |=  (gpio_mask << MISC_REGISTERS_GPIO_INT_SET_POS);
        break;

    default:
        break;
    }

    REG_WR(pdev, MISC_REG_GPIO_INT, gpio_reg);
    // unlock after write
    DbgMessage2(pdev, INFORM, "lm_gpio_int_write: pin %d value is %x\n",
       pin_num, gpio_reg);
    lm_hw_unlock(pdev, HW_LOCK_RESOURCE_GPIO);

    return 0;
}

/*
 *------------------------------------------------------------------------
 * lm_spio_read -
 *
 * Read the value of the requested SPIO pin (with pin_num)
 *
 *------------------------------------------------------------------------
 */
lm_status_t lm_spio_read(struct _lm_device_t *pdev, u32_t pin_num, u32_t* value_ptr)
{
    u32_t reg_val = 0, mask = 0;

    // Read from MISC block the SPIO register
    reg_val = REG_RD(pdev, MISC_REG_SPIO);

    DbgMessage1(pdev, INFORM, "lm_spio_read: MISC_REGISTERS_SPIO value is 0x%x\n", reg_val);

    // Calculate the value with relevent OE set to 1 (for input).
    // Calulate the mask for the read value.
    switch (pin_num) {
        case 0:        // SPIO pins 0-2 do not have OE pins
            mask = GRC_MISC_REGISTERS_SPIO_VAL0;
            break;
        case 1:
            mask = GRC_MISC_REGISTERS_SPIO_VAL1;
            break;
        case 2:
            mask = GRC_MISC_REGISTERS_SPIO_VAL2;
            break;
        case 3:         // SPIO pin 3 is not connected
            return LM_STATUS_FAILURE;
        case 4:        // SPIO pins 4-7 have OE pins
            reg_val |= GRC_MISC_REGISTERS_SPIO_OE4;
            mask = GRC_MISC_REGISTERS_SPIO_VAL4;
            break;
        case 5:
            reg_val |= GRC_MISC_REGISTERS_SPIO_OE5;
            mask = GRC_MISC_REGISTERS_SPIO_VAL5;
            break;
        case 6:
            reg_val |= GRC_MISC_REGISTERS_SPIO_OE6;
            mask = GRC_MISC_REGISTERS_SPIO_VAL6;
            break;
        case 7:
            reg_val |= GRC_MISC_REGISTERS_SPIO_OE7;
            mask = GRC_MISC_REGISTERS_SPIO_VAL7;
            break;
        default:
            return LM_STATUS_FAILURE;
    }

    // Write to SPIO register the value with the relevant OE set to 1
    REG_WR(pdev, MISC_REG_SPIO, reg_val);
    DbgMessage1(NULL, INFORM, "lm_spio_read: writing MISC_REGISTERS_SPIO 0x%x\n", reg_val);

    // Read from MISC block the SPIO register
    reg_val = REG_RD(pdev, MISC_REG_SPIO);
    DbgMessage1(NULL, INFORM, "lm_spio_read: MISC_REGISTERS_SPIO value 0x%x\n", reg_val);

    // Get the requested pin value by masking the val with mask
    if ((reg_val & mask) == mask)
    {
        *value_ptr = 1;
    }
    else
    {
        *value_ptr = 0;
    }
    DbgMessage2(NULL, INFORM, "lm_spio_read: pin %d value is 0x%x\n", pin_num, *value_ptr);

    return LM_STATUS_SUCCESS;
}

/*
 *------------------------------------------------------------------------
 * lm_spio_write -
 *
 * Write a value to the requested SPIO pin (with pin_num)
 *
 *------------------------------------------------------------------------
 */
lm_status_t lm_spio_write(struct _lm_device_t *pdev, u32_t pin_num, u32_t value)
{
    u32_t       reg_val   = 0;
    lm_status_t lm_status = LM_STATUS_SUCCESS ;

    if CHK_NULL(pdev)
    {
        DbgBreakIf(!pdev);
        return LM_STATUS_INVALID_PARAMETER ;
    }

    // lock before read
    lm_hw_lock(pdev, HW_LOCK_RESOURCE_GPIO, TRUE); // The GPIO lock is used for SPIO as well!

    // Read from MISC block the SPIO register
    reg_val = REG_RD(pdev, MISC_REG_SPIO);
    DbgMessage1(NULL, INFORM, "lm_gpio_write: MISC_REGISTERS_SPIO value is 0x%x\n", reg_val);

    // Turn the requested SPIO pin to output by setting its OE bit to 0 and
    // If value is 1 set the relevant SET bit to 1, otherwise set the CLR bit to 1.
    switch (pin_num) {
        case 0:
            // Clear the pins CLR and SET bits
            reg_val &= ~GRC_MISC_REGISTERS_SPIO_SET0 & ~GRC_MISC_REGISTERS_SPIO_CLR0;
            // If value is 1 set the SET bit of this pin, otherwise set the CLR bit.
            reg_val |= (value==1) ? GRC_MISC_REGISTERS_SPIO_SET0 : GRC_MISC_REGISTERS_SPIO_CLR0;
            break;
        case 1:
            // Clear the pins CLR and SET bits
            reg_val &= ~GRC_MISC_REGISTERS_SPIO_SET1 & ~GRC_MISC_REGISTERS_SPIO_CLR1;
            // If value is 1 set the SET bit of this pin, otherwise set the CLR bit.
            reg_val |= (value==1) ? GRC_MISC_REGISTERS_SPIO_SET1 : GRC_MISC_REGISTERS_SPIO_CLR1;
            break;
        case 2:
            // Clear the pins CLR and SET bits
            reg_val &= ~GRC_MISC_REGISTERS_SPIO_SET2 & ~GRC_MISC_REGISTERS_SPIO_CLR2;
            // If value is 1 set the SET bit of this pin, otherwise set the CLR bit.
            reg_val |= (value==1) ? GRC_MISC_REGISTERS_SPIO_SET2 : GRC_MISC_REGISTERS_SPIO_CLR2;
            break;
        case 3:
            // SPIO pin 3 is not connected
            lm_status = LM_STATUS_FAILURE;
        case 4:
            // Set pin as OUTPUT
            reg_val &= ~GRC_MISC_REGISTERS_SPIO_OE4;
            // Clear the pins CLR and SET bits
            reg_val &= ~GRC_MISC_REGISTERS_SPIO_SET4 & ~GRC_MISC_REGISTERS_SPIO_CLR4;
            // If value is 1 set the SET bit of this pin, otherwise set the CLR bit.
            reg_val |= (value==1) ? GRC_MISC_REGISTERS_SPIO_SET4 : GRC_MISC_REGISTERS_SPIO_CLR4;
            break;
        case 5:
            // Set pin as OUTPUT
            reg_val &= ~GRC_MISC_REGISTERS_SPIO_OE5;
            // Clear the pins CLR and SET bits
            reg_val &= ~GRC_MISC_REGISTERS_SPIO_SET5 & ~GRC_MISC_REGISTERS_SPIO_CLR5;
            // If value is 1 set the SET bit of this pin, otherwise set the CLR bit.
            reg_val |= (value==1) ? GRC_MISC_REGISTERS_SPIO_SET5 : GRC_MISC_REGISTERS_SPIO_CLR5;
            break;
        case 6:
            // Set pin as OUTPUT
            reg_val &= ~GRC_MISC_REGISTERS_SPIO_OE6;
            // Clear the pins CLR and SET bits
            reg_val &= ~GRC_MISC_REGISTERS_SPIO_SET6 & ~GRC_MISC_REGISTERS_SPIO_CLR6;
            // If value is 1 set the SET bit of this pin, otherwise set the CLR bit.
            reg_val |= (value==1) ? GRC_MISC_REGISTERS_SPIO_SET6 : GRC_MISC_REGISTERS_SPIO_CLR6;
            break;
        case 7:
            // Set pin as OUTPUT
            reg_val &= ~GRC_MISC_REGISTERS_SPIO_OE7;
            // Clear the pins CLR and SET bits
            reg_val &= ~GRC_MISC_REGISTERS_SPIO_SET7 & ~GRC_MISC_REGISTERS_SPIO_CLR7;
            // If value is 1 set the SET bit of this pin, otherwise set the CLR bit.
            reg_val |= (value==1) ? GRC_MISC_REGISTERS_SPIO_SET7: GRC_MISC_REGISTERS_SPIO_CLR7;
            break;
        default:
            lm_status = LM_STATUS_FAILURE;
            break;

    }

    if( LM_STATUS_SUCCESS == lm_status )
    {
        // Write to SPIO register the value with the relevant OE set to 1 and
        // If value is 1, set the relevant SET bit to 1, otherwise set the CLR bit to 1.
        REG_WR(pdev, MISC_REG_SPIO, reg_val);
        DbgMessage1(NULL, INFORM, "lm_spio_write: writing MISC_REGISTERS_SPIO 0x%x\n", reg_val);
    }

    // unlock
    lm_hw_unlock(pdev, HW_LOCK_RESOURCE_GPIO);

    return lm_status ;
}


/*
 *------------------------------------------------------------------------
 * lm_set_led_mode -
 *
 * Set the led mode of the requested port
 *
 *------------------------------------------------------------------------
 */
lm_status_t lm_set_led_mode(struct _lm_device_t *pdev, u32_t port_idx, u32_t mode_idx)
{

    DbgBreakIf(!pdev);

    // Write to relevant NIG register LED_MODE (P0 or P1) the mode index (0-15)
    switch (port_idx) {
        case 0:
            REG_WR(pdev,  NIG_REG_LED_MODE_P0, mode_idx);
            break;
        case 1:
            REG_WR(pdev,  NIG_REG_LED_MODE_P1, mode_idx);
            break;
        default:
            DbgMessage1(NULL, FATAL, "lm_set_led_mode() unknown port index %d\n", port_idx);
            return LM_STATUS_FAILURE;
    }

    DbgMessage2(NULL, INFORM, "lm_set_led_mode() wrote to NIG_REG_LED_MODE (port %d) 0x%x\n", port_idx, mode_idx);
    return LM_STATUS_SUCCESS;
}

/*
 *------------------------------------------------------------------------
 * lm_get_led_mode -
 *
 * Get the led mode of the requested port
 *
 *------------------------------------------------------------------------
 */
lm_status_t lm_get_led_mode(struct _lm_device_t *pdev, u32_t port_idx, u32_t* mode_idx_ptr)
{

    DbgBreakIf(!pdev);

    // Read from the relevant NIG register LED_MODE (P0 or P1) the mode index (0-15)
    switch (port_idx) {
        case 0:
            *mode_idx_ptr = REG_RD(pdev,  NIG_REG_LED_MODE_P0);
            break;
        case 1:
            *mode_idx_ptr = REG_RD(pdev,  NIG_REG_LED_MODE_P1);
            break;
        default:
            DbgMessage1(NULL, FATAL, "lm_get_led_mode() unknown port index %d\n", port_idx);
            return LM_STATUS_FAILURE;
    }

    DbgMessage2(NULL, INFORM, "lm_get_led_mode() read from NIG_REG_LED_MODE (port %d) 0x%x\n", port_idx, *mode_idx_ptr);

    return LM_STATUS_SUCCESS;
}

/*
 *------------------------------------------------------------------------
 * lm_override_led_value -
 *
 * Override the led value of the requsted led
 *
 *------------------------------------------------------------------------
 */
lm_status_t lm_override_led_value(struct _lm_device_t *pdev, u32_t port_idx, u32_t led_idx, u32_t value)
{
    u32_t reg_val   = 0;

    // If port 0 then use EMAC0, else use EMAC1
    u32_t emac_base = (port_idx) ? GRCBASE_EMAC1 : GRCBASE_EMAC0;

    DbgBreakIf(!pdev);

    DbgMessage3(NULL, INFORM, "lm_override_led_value() port %d led_idx %d value %d\n", port_idx, led_idx, value);

    switch (led_idx) {
        case 0: //10MB led
            // Read the current value of the LED register in the EMAC block
            reg_val = REG_RD(pdev, emac_base + EMAC_REG_EMAC_LED);
            // Set the OVERRIDE bit to 1
            reg_val |= EMAC_LED_OVERRIDE;
            // If value is 1, set the 10M_OVERRIDE bit, otherwise reset it.
            reg_val = (value==1) ? (reg_val | EMAC_LED_10MB_OVERRIDE) : (reg_val & ~EMAC_LED_10MB_OVERRIDE);
            REG_WR(pdev, emac_base+ EMAC_REG_EMAC_LED, reg_val);
            break;
        case 1: //100MB led
            // Read the current value of the LED register in the EMAC block
            reg_val = REG_RD(pdev, emac_base + EMAC_REG_EMAC_LED);
            // Set the OVERRIDE bit to 1
            reg_val |= EMAC_LED_OVERRIDE;
            // If value is 1, set the 100M_OVERRIDE bit, otherwise reset it.
            reg_val = (value==1) ? (reg_val | EMAC_LED_100MB_OVERRIDE) : (reg_val & ~EMAC_LED_100MB_OVERRIDE);
            REG_WR(pdev, emac_base+ EMAC_REG_EMAC_LED, reg_val);
            break;
        case 2: //1000MB led
            // Read the current value of the LED register in the EMAC block
            reg_val = REG_RD(pdev, emac_base + EMAC_REG_EMAC_LED);
            // Set the OVERRIDE bit to 1
            reg_val |= EMAC_LED_OVERRIDE;
            // If value is 1, set the 1000M_OVERRIDE bit, otherwise reset it.
            reg_val = (value==1) ? (reg_val | EMAC_LED_1000MB_OVERRIDE) : (reg_val & ~EMAC_LED_1000MB_OVERRIDE);
            REG_WR(pdev, emac_base+ EMAC_REG_EMAC_LED, reg_val);
            break;
        case 3: //2500MB led
            // Read the current value of the LED register in the EMAC block
            reg_val = REG_RD(pdev, emac_base + EMAC_REG_EMAC_LED);
            // Set the OVERRIDE bit to 1
            reg_val |= EMAC_LED_OVERRIDE;
            // If value is 1, set the 2500M_OVERRIDE bit, otherwise reset it.
            reg_val = (value==1) ? (reg_val | EMAC_LED_2500MB_OVERRIDE) : (reg_val & ~EMAC_LED_2500MB_OVERRIDE);
            REG_WR(pdev, emac_base+ EMAC_REG_EMAC_LED, reg_val);
            break;
        case 4: //10G led
            if (port_idx == 0) {
                REG_WR(pdev,  NIG_REG_LED_10G_P0, value);
            } else {
                REG_WR(pdev,  NIG_REG_LED_10G_P1, value);
            }
            break;
        case 5: //TRAFFIC led

            // Find if the traffic control is via BMAC or EMAC
            if (port_idx == 0) {
                reg_val = REG_RD(pdev,  NIG_REG_NIG_EMAC0_EN);
            } else {
                reg_val = REG_RD(pdev,  NIG_REG_NIG_EMAC1_EN);
            }

            // Override the traffic led in the EMAC:
            if (reg_val == 1) {
                // Read the current value of the LED register in the EMAC block
                reg_val = REG_RD(pdev, emac_base + EMAC_REG_EMAC_LED);
                // Set the TRAFFIC_OVERRIDE bit to 1
                reg_val |= EMAC_LED_OVERRIDE;
                // If value is 1, set the TRAFFIC bit, otherwise reset it.
                reg_val = (value==1) ? (reg_val | EMAC_LED_TRAFFIC) : (reg_val & ~EMAC_LED_TRAFFIC);
                REG_WR(pdev, emac_base+ EMAC_REG_EMAC_LED, reg_val);
            } else {    // Override the traffic led in the BMAC:
                if (port_idx == 0) {
                    REG_WR(pdev,  NIG_REG_LED_CONTROL_OVERRIDE_TRAFFIC_P0, 1);
                    REG_WR(pdev,  NIG_REG_LED_CONTROL_TRAFFIC_P0, value);
                } else {
                    REG_WR(pdev,  NIG_REG_LED_CONTROL_OVERRIDE_TRAFFIC_P1, 1);
                    REG_WR(pdev,  NIG_REG_LED_CONTROL_TRAFFIC_P1, value);
                }
            }
            break;
        default:
            DbgMessage1(NULL, FATAL, "lm_override_led_value() unknown led index %d (should be 0-5)\n", led_idx);
            return LM_STATUS_FAILURE;
    }

    return LM_STATUS_SUCCESS;
}

/*
 *------------------------------------------------------------------------
 * lm_blink_traffic_led -
 *
 * Blink the traffic led with the requsted rate
 *
 *------------------------------------------------------------------------
 */
lm_status_t lm_blink_traffic_led(struct _lm_device_t *pdev, u32_t port_idx, u32_t rate)
{
    u32_t reg_val   = 0;
    // If port 0 then use EMAC0, else use EMAC1
    u32_t emac_base = (port_idx) ? GRCBASE_EMAC1 : GRCBASE_EMAC0;

    DbgBreakIf(!pdev);

    // Find if the traffic control is via BMAC or EMAC
    if (port_idx == 0) {
        reg_val = REG_RD(pdev,  NIG_REG_NIG_EMAC0_EN);
    } else {
        reg_val = REG_RD(pdev,  NIG_REG_NIG_EMAC1_EN);
    }

    // Blink the traffic led using EMAC control:
    if (reg_val == 1) {
        // Read the current value of the LED register in the EMAC block
        reg_val = REG_RD(pdev, emac_base + EMAC_REG_EMAC_LED);

        // Set the TRAFFIC_OVERRIDE, TRAFFIC and BLNK_TRAFFIC to 1
        reg_val |= EMAC_LED_OVERRIDE;
        reg_val |= EMAC_LED_TRAFFIC;
        reg_val |= EMAC_LED_BLNK_TRAFFIC;

        // If rate field was entered then set the BLNK_RATE_ENA bit and the BLNK_RATE field,
        // Otherwise the blink rate will be about 16Hz
        if (rate != 0) {
            reg_val |= EMAC_LED_BLNK_RATE_ENA;
            reg_val |= (rate << EMAC_LED_BLNK_RATE_BITSHIFT);
        }
        REG_WR(pdev, emac_base+ EMAC_REG_EMAC_LED, reg_val);
        DbgMessage2(NULL, INFORM, "lm_blink_traffic_led() port %d write to EMAC_REG_EMAC_LED the value 0x%x\n", port_idx, reg_val);

    } else { // Blink the traffic led in the BMAC:
        // Set the CONTROL_OVERRIDE_TRAFFIC and the CONTROL_BLINK_TRAFFIC to 1.
        if (port_idx == 0) {
            REG_WR(pdev,  NIG_REG_LED_CONTROL_OVERRIDE_TRAFFIC_P0, 1);
            REG_WR(pdev,  NIG_REG_LED_CONTROL_TRAFFIC_P0, 1);
            REG_WR(pdev,  NIG_REG_LED_CONTROL_BLINK_TRAFFIC_P0, 1);
            DbgMessage(NULL, INFORM, "lm_blink_traffic_led() set BLINK_TRAFFIC_P0 to 1\n");
            // If the rate field was entered, update the BLINK_RATE register accordingly
            if (rate != 0) {
                REG_WR(pdev,  NIG_REG_LED_CONTROL_BLINK_RATE_ENA_P0, 1);
                REG_WR(pdev,  NIG_REG_LED_CONTROL_BLINK_RATE_P0, rate);
                DbgMessage2(NULL, INFORM, "lm_blink_traffic_led() port %d write to NIG_REG_LED_CONTROL_BLINK_RATE_P0 %x\n", port_idx, rate);
            }
        } else {
            REG_WR(pdev,  NIG_REG_LED_CONTROL_OVERRIDE_TRAFFIC_P1, 1);
            REG_WR(pdev,  NIG_REG_LED_CONTROL_TRAFFIC_P1, 1);
            REG_WR(pdev,  NIG_REG_LED_CONTROL_BLINK_TRAFFIC_P1, 1);
            DbgMessage(NULL, INFORM, "lm_blink_traffic_led() set BLINK_TRAFFIC_P1 to 1\n");
            // If the rate field was entered, update the BLINK_RATE register accordingly
            if (rate != 0) {
                REG_WR(pdev,  NIG_REG_LED_CONTROL_BLINK_RATE_ENA_P1, 1);
                REG_WR(pdev,  NIG_REG_LED_CONTROL_BLINK_RATE_P1, rate);
                DbgMessage2(NULL, INFORM, "lm_blink_traffic_led() port %d write to NIG_REG_LED_CONTROL_BLINK_RATE_P1 0x%x\n", port_idx, rate);
            }
        }
    }
    return LM_STATUS_SUCCESS;
}

/*
 *------------------------------------------------------------------------
 * lm_get_led_status -
 *
 * Get the led status of the requsted led, on the requested port
 *
 *------------------------------------------------------------------------
 */
lm_status_t lm_get_led_status(struct _lm_device_t *pdev, u32_t port_idx, u32_t led_idx, u32_t* value_ptr)
{
    u32_t reg_val   = 0;

    // If port 0 then use EMAC0, else use EMAC1
    u32_t emac_base = (port_idx) ? GRCBASE_EMAC1 : GRCBASE_EMAC0;

    DbgBreakIf(!pdev);

    switch (led_idx) {
        case 0: //10MB LED
            // Read the current value of the LED register in the EMAC block
            reg_val = REG_RD(pdev, emac_base + EMAC_REG_EMAC_LED);
            // Check the 10MB bit status
            *value_ptr = ((reg_val & EMAC_LED_10MB) == EMAC_LED_10MB) ? 1 : 0;
            break;
        case 1: //100MB LED
            // Read the current value of the LED register in the EMAC block
            reg_val = REG_RD(pdev, emac_base + EMAC_REG_EMAC_LED);
            // Check the 100MB bit status
            *value_ptr = ((reg_val & EMAC_LED_100MB) == EMAC_LED_100MB) ? 1 : 0;
            break;
        case 2: //1000MB LED
            // Read the current value of the LED register in the EMAC block
            reg_val = REG_RD(pdev, emac_base + EMAC_REG_EMAC_LED);
            // Check the 1000MB bit status
            *value_ptr = ((reg_val & EMAC_LED_1000MB) == EMAC_LED_1000MB) ? 1 : 0;
            break;
        case 3: //2500MB LED
            // Read the current value of the LED register in the EMAC block
            reg_val = REG_RD(pdev, emac_base + EMAC_REG_EMAC_LED);
            // Check the 2500MB bit status
            *value_ptr = ((reg_val & EMAC_LED_2500MB) == EMAC_LED_2500MB) ? 1 : 0;
            break;
        case 4: //10G LED
            if (port_idx == 0) {
                *value_ptr = REG_RD(pdev,  NIG_REG_LED_10G_P0);
            } else {
                *value_ptr = REG_RD(pdev,  NIG_REG_LED_10G_P1);
            }
            break;
        case 5: //TRAFFIC LED
            // Read the traffic led from the EMAC block
            reg_val = REG_RD(pdev, emac_base + EMAC_REG_EMAC_LED);
            // Check the TRAFFIC_STAT bit status
            *value_ptr = ((reg_val & EMAC_LED_TRAFFIC_STAT) == EMAC_LED_TRAFFIC_STAT) ? 1 : 0;

            // Read the traffic led from the BMAC block
            if (port_idx == 0) {
                *value_ptr = REG_RD(pdev,  NIG_REG_LED_STATUS_ACTIVE_P0);
            } else {
                *value_ptr = REG_RD(pdev,  NIG_REG_LED_STATUS_ACTIVE_P1);
            }
            break;
        default:
            DbgMessage1(NULL, FATAL, "lm_get_led_status() unknown led index %d (should be 0-5)\n", led_idx);
            return LM_STATUS_FAILURE;
    }

    DbgMessage3(NULL, INFORM, "lm_get_led_status() port %d led_idx %d value %d\n", port_idx, led_idx, *value_ptr);

    return LM_STATUS_SUCCESS;

}

/*
 *------------------------------------------------------------------------
 * lm_set_led -
 *
 * Sets the LEDs to operational mode after establishing link
 *
 *------------------------------------------------------------------------
 */
void
lm_set_led(struct _lm_device_t *pdev, lm_medium_t speed)
{
    u8_t port = 0;

    if CHK_NULL( pdev )
    {
        DbgBreakIf(!pdev) ;
        return;
    }
    port = PORT_ID(pdev);

    //ACCESS:RW  DataWidth:0x4  Description: led mode for port0: 0 MAC;
    // 1-3 PHY1; 4 MAC2; 5-7 PHY4; 8-MAC3; 9-11PHY7; 12 MAC4; 13-15 PHY10;
    REG_WR(pdev,  NIG_REG_LED_MODE_P0 + port*4,
           (pdev->hw_info.nvm_hw_config & SHARED_HW_CFG_LED_MODE_MASK) >> SHARED_HW_CFG_LED_MODE_SHIFT);

    REG_WR(pdev,  NIG_REG_LED_CONTROL_OVERRIDE_TRAFFIC_P0 + port*4, 0);

    /* A value of 480 (dec) will provide 15.9Hz blinking rate */
    REG_WR(pdev,  NIG_REG_LED_CONTROL_BLINK_RATE_P0 + port*4, LED_BLINK_RATE_VAL);
    REG_WR(pdev,  NIG_REG_LED_CONTROL_BLINK_RATE_ENA_P0 + port*4, 1);

    /* On Ax chip versions, the less than 10G LED scheme is different */
    if (CHIP_REV(pdev) == CHIP_REV_Ax &&
        GET_MEDIUM_SPEED(speed) < LM_MEDIUM_SPEED_10GBPS) {
            REG_WR(pdev,  NIG_REG_LED_CONTROL_OVERRIDE_TRAFFIC_P0 + port*4, 1);
            REG_WR(pdev,  NIG_REG_LED_CONTROL_TRAFFIC_P0 + port*4, 0);
            REG_WR(pdev,  NIG_REG_LED_CONTROL_BLINK_TRAFFIC_P0 + port*4, 1);
    }
}

/*
*------------------------------------------------------------------------
* lm_reset_led -
*
* Sets the LEDs to operational mode after establishing link
*
*------------------------------------------------------------------------
*/
void
lm_reset_led(struct _lm_device_t *pdev)
{
    //u32_t val;
    u8_t port = 0;

    if (CHK_NULL(pdev)){
        DbgBreakIf(!pdev);
        return;
    }
    port = PORT_ID(pdev);

    REG_WR(pdev,  NIG_REG_LED_10G_P0 + port*4, 0);
    REG_WR(pdev,  NIG_REG_LED_MODE_P0 + port*4,SHARED_HW_CFG_LED_MAC1);
}


void lm_reset_device_if_undi_active(struct _lm_device_t *pdev)
{
    u32_t                         val                        = 0;
    u8_t                          func                       = 0;
    u8_t                          abs_func                   = 0;
    u8_t                          port                       = 0;
    u8_t                          opcode_idx                 = 0; // 0 = load, 1 = unload
    lm_loader_response            resp                       = 0;
    u32_t                         swap_val                   = 0;
    u32_t                         swap_en                    = 0;
    u32_t                         cnt                        = 0;
    u8_t                          port_max                   = 0;
    static const u32_t            UNDI_ACTIVE_INDICATION_VAL = 7;
    static const lm_loader_opcode opcode_arr[]               = {LM_LOADER_OPCODE_LOAD, LM_LOADER_OPCODE_UNLOAD_WOL_DIS} ;
    const lm_chip_port_mode_t     port_mode                  = CHIP_PORT_MODE(pdev);

    val = REG_RD(pdev,MISC_REG_UNPREPARED);

    DbgMessage1(pdev,WARN,"lm_reset_device_if_undi_active: MISC_REG_UNPREPARED val = 0x%x\n", val);

    if( 0x1 != val )
    {
        // chip/path is already in reset, undi is not active, nothing to do.
        return;
    }

    val = 0 ;
    /*
    * Check if device is active and was previously initialized by
    * UNDI driver.  UNDI driver initializes CID offset for normal bell
    * to 0x7.
    */
    if( LM_STATUS_SUCCESS == lm_hw_lock(pdev, HW_LOCK_RESOURCE_UNDI, TRUE) )
    {
        val = REG_RD(pdev,DORQ_REG_NORM_CID_OFST);
        DbgMessage1(pdev,WARN,"lm_reset_device_if_undi_active: DORQ_REG_NORM_CID_OFST val = 0x%x\n",val);
        if( UNDI_ACTIVE_INDICATION_VAL == val )
        {
            REG_WR( pdev, DORQ_REG_NORM_CID_OFST ,0 );
            lm_hw_unlock(pdev, HW_LOCK_RESOURCE_UNDI);
        }
        else
        {
            lm_hw_unlock(pdev, HW_LOCK_RESOURCE_UNDI);
            // undi is not active, nothing to do.
            return;
        }
    }
    else
    {
        // lock is already taken by other func we have nothing to do
        return;
    }

    DbgMessage(pdev,WARN, "lm_reset_device_if_undi_active: UNDI is active! need to reset device\n");

    if (GET_FLAGS( pdev->params.test_mode, TEST_MODE_NO_MCP))
    {
        /* TBD: E1H - when MCP is not present, determine if possible to get here */
        DbgBreakMsg("lm_reset_device_if_undi_active: reading from shmem when MCP is not present\n");
    }

    // Store original pdev func
    func     = FUNC_ID(pdev) ;
    abs_func = ABS_FUNC_ID(pdev);

    switch( port_mode )
    {
    case LM_CHIP_PORT_MODE_NONE: // E1.0/E1.5: we enter this if() one time  - for one of the functions, and and mailbox func numbers are 0 and 1
    case LM_CHIP_PORT_MODE_4:    // E2
        port_max = PORT_MAX;
        break;

    case LM_CHIP_PORT_MODE_2:
        port_max = 1; // E2: we enter this if() maximum twice - once for each path, and mailbox func number is 0 for both times
        break;

    default:
        DbgBreakMsg("we should not reach this line!");
        break;
    }

    ASSERT_STATIC( 2 == ARRSIZE(opcode_arr) );
    DbgBreakIf( LM_LOADER_OPCODE_LOAD != opcode_arr[0] );
    DbgBreakIf( LM_LOADER_OPCODE_LOAD == opcode_arr[1] );

    // We do here two opcode iterations, each one of them for all ports...
    // 1. first iteration(s) will "tell" the mcp that all ports are loaded (MCP accepts LOAD requests for ports that are already loaded.)
    //    This way we cann assure that driver is the "owner" of the hardware (includes NIG)
    //    So we can reset the nig.
    //
    // 2. second iteration(s) will "tell" the mcp that all ports are unloaded so we can "come clean" for regular driver load flow
    for( opcode_idx = 0; opcode_idx < ARRSIZE(opcode_arr); opcode_idx++ )
    {
        for( port = 0; port < port_max; port++ )
        {
            // NOTE: it seems that these two line are redundant after we have the new FUNC_MAILBOX_ID macro
            //       keep it for now
            pdev->params.pfunc_rel = port; // MF - it doesn't matter if 0,2,4,6 or 1,3,5,7 the port is what counts dervied from func
            pdev->params.pfunc_abs = port;

            // get fw_wr_seq for the func
            lm_mcp_cmd_init(pdev);

            resp = lm_loader_lock(pdev, opcode_arr[opcode_idx] );

            if( LM_LOADER_RESPONSE_UNLOAD_COMMON == resp )
            {
                DbgBreakIf( LM_LOADER_OPCODE_LOAD == opcode_arr[opcode_idx] );
            }

            if ( LM_LOADER_OPCODE_LOAD == opcode_arr[opcode_idx] )
            {
                // clean HC config (only if exists  E1.0/E1.5)
                // INTR_BLK_TYPE is not valid since we don't have this information at this phase yet.
                if ( CHIP_IS_E1x(pdev) )
                {
                    REG_WR(pdev,HC_REG_CONFIG_0+(4*port),0x1000);
                }

                //close traffic toward the NIG
                REG_WR(pdev, NIG_REG_LLH0_BRB1_DRV_MASK + 4*port,   0x0);
                REG_WR(pdev, NIG_REG_LLH0_BRB1_NOT_MCP  + 4*32*port,0x0);
                // mask AEU signal
                REG_WR(pdev,MISC_REG_AEU_MASK_ATTN_FUNC_0+(4*port),0);

                //wait 10 msec
                for (cnt = 0; cnt < 200; cnt ++ )
                {
                    mm_wait(pdev,50);
                }

                if( port_max == (port + 1) )
                {
                    // TODO: Reset take into account mstat - dealed better in main branch where reset chip issue is tidier,
                    // leaving this for integrate...

                    // save nig swap register before NIG reset
                    swap_val = REG_RD(pdev,NIG_REG_PORT_SWAP);
                    swap_en  = REG_RD(pdev,NIG_REG_STRAP_OVERRIDE);

                    // reset the chip with nig
                    lm_reset_path( pdev, TRUE );

                    // restore nig swap register
                    REG_WR(pdev,NIG_REG_PORT_SWAP,swap_val);
                    REG_WR(pdev,NIG_REG_STRAP_OVERRIDE,swap_en);
                }
            }
            lm_loader_unlock(pdev, opcode_arr[opcode_idx] ) ;
        } // for loop

    } // opcode loop

    // We expect that last reposne will be LM_LOADER_RESPONSE_UNLOAD_COMMON
    if( LM_LOADER_RESPONSE_UNLOAD_COMMON != resp )
    {
        DbgBreakIf( LM_LOADER_RESPONSE_UNLOAD_COMMON != resp );
    }

    // restore original function number
    pdev->params.pfunc_rel = func ;
    pdev->params.pfunc_abs = abs_func;

    // after the unlock the chip/path is in reset for sure, then second port won't see 7 in the DORQ_REG_NORM_CID_OFST

} // lm_reset_device_if_undi_active

static u8_t lm_is_57710A0_dbg_intr( struct _lm_device_t * pdev )
{
    u32_t val = 0;

    /* if during MSI/MSI-X mode then take no action (different problem) */
    if(pdev->params.interrupt_mode != LM_INT_MODE_INTA)
    {
        DbgMessage(pdev,WARN,"MSI/MSI-X enabled - debugging INTA/B failed\n");
        return 0;
    }

    /* read status from PCIE core */
    val = REG_RD(pdev, 0x2004);

    /* if interrupt line value from PCIE core is not asserted then take no action (different problem) */
    #define PCIE_CORE_INT_PENDING_BIT 0X00080000 /* when this bit is set, interrupt is asserted (pending) */
    if(!GET_FLAGS(val, PCIE_CORE_INT_PENDING_BIT))
    {
        DbgMessage(pdev,WARN,"PCIE core int line not asserted - debugging INTA/B failed\n");
        return 0;
    }

    /* if interrupt line from PCIE core is not enabled then take no action (different problem) */
    #define PCIE_CORE_INT_DISABLE_BIT 0X00000400 /* when this bit is set, interrupt is disabled */
    if(GET_FLAGS(val, PCIE_CORE_INT_DISABLE_BIT))
    {
        DbgMessage(pdev,WARN,"PCIE core int line not enabled - debugging INTA/B failed\n");
        return 0;
    }

    /* read interrupt mask from IGU */
    val = REG_RD(pdev,  HC_REG_INT_MASK + 4*PORT_ID(pdev) );

    /* if not 1FFFF then write warning to log (suspected as different problem) and continue to following step */
    if(val != 0x0001ffff)
    {
        DbgMessage(pdev,WARN,"IGU int mask != 0x1ffff - might not be related to debugging INTA/B issue\n");
    }

    /* verify that int_line_en_0/1 is 1. If bit is clear then no action  write warning to log and return. */
    // We skip this check.

    return 1;
}

/** lm_57710A0_dbg_intr
 *
 * Description:
 * 1. some sanity checks that the case we have is indeed the
 * interrupt debugging mode.
 * 2. Apply special handling, that is to disable and enable
 * INTA/B in IGU
 */
void lm_57710A0_dbg_intr( struct _lm_device_t * pdev )
{
    if(IS_CHIP_REV_A0(pdev) && lm_is_57710A0_dbg_intr(pdev))
    {
        lm_disable_int(pdev);
        lm_enable_int(pdev);
    }
}



lm_status_t
lm_setup_client_con_resc(
    IN struct _lm_device_t *pdev,
    IN u32_t cid
    )
{
    lm_status_t lm_status = LM_STATUS_SUCCESS;

    if CHK_NULL(pdev)
    {
        return LM_STATUS_INVALID_PARAMETER;
    }

    if(GET_FLAGS(pdev->params.l2_cli_con_params[cid].attributes,LM_CLIENT_ATTRIBUTES_TX))
    {
        lm_status = lm_setup_txq(pdev, cid);
        if (lm_status != LM_STATUS_SUCCESS)
        {
            return lm_status;
        }
    }


    if(GET_FLAGS(pdev->params.l2_cli_con_params[cid].attributes,LM_CLIENT_ATTRIBUTES_RX))
    {
        lm_status = lm_setup_rxq(pdev, cid);
        if (lm_status != LM_STATUS_SUCCESS)
        {
            return lm_status;
        }

        lm_status = lm_setup_rcq(pdev, cid);
        if (lm_status != LM_STATUS_SUCCESS)
        {
            return lm_status;
        }
    }

    pdev->client_info[cid].last_set_rx_mask = 0;

    return LM_STATUS_SUCCESS;
}


lm_status_t
lm_clear_chain_sb_cons_idx(
    IN struct _lm_device_t *pdev,
    IN u8_t sb_id,
    IN struct _lm_hc_sb_info_t *hc_sb_info,
    IN volatile u16_t ** hw_con_idx_ptr
    )
{
    u8_t  port       = 0;
    u8_t  func       = 0;
    u16_t rd_val     = 0xFFFF;
    u32_t rd_val_32  = 0xFFFFFFFF;
    u8_t  fw_sb_id   = 0;
    u8_t  sb_lock_id = 0;

    if (CHK_NULL(pdev) || CHK_NULL(hc_sb_info) || CHK_NULL(hw_con_idx_ptr))
    {
        return LM_STATUS_INVALID_PARAMETER;
    }

    if (IS_VFDEV(pdev))
    {
        return LM_STATUS_SUCCESS;
    }

    sb_lock_id = lm_sb_id_from_chain(pdev, sb_id);
    if (sb_lock_id == DEF_STATUS_BLOCK_INDEX)
    {
        sb_lock_id = DEF_STATUS_BLOCK_IGU_INDEX;
    }

    /* make sure that the sb is not during processing while we
     * clear the pointer */
    MM_ACQUIRE_SB_LOCK(pdev, sb_lock_id);

    *hw_con_idx_ptr = NULL;

    MM_RELEASE_SB_LOCK(pdev, sb_lock_id);

    if (lm_reset_is_inprogress(pdev))
    {
        return LM_STATUS_SUCCESS;
    }

    port = PORT_ID(pdev);
    func = FUNC_ID(pdev);
    fw_sb_id = LM_FW_SB_ID(pdev, sb_id);

    switch (hc_sb_info->hc_sb) {
    case STATUS_BLOCK_SP_SL_TYPE:
        LM_INTMEM_WRITE16(pdev, CSTORM_SP_HC_SYNC_LINE_INDEX_OFFSET(hc_sb_info->hc_index_value,func), 0, BAR_CSTRORM_INTMEM);
        LM_INTMEM_READ16(pdev, CSTORM_SP_HC_SYNC_LINE_INDEX_OFFSET(hc_sb_info->hc_index_value,func),  &rd_val, BAR_CSTRORM_INTMEM);
        DbgBreakIfAll(rd_val != 0);

        LM_INTMEM_WRITE16(pdev, (CSTORM_SP_STATUS_BLOCK_OFFSET(func) + OFFSETOF(struct hc_sp_status_block, index_values) + (hc_sb_info->hc_index_value * sizeof(u16_t))), 0, BAR_CSTRORM_INTMEM);
        LM_INTMEM_READ16 (pdev, (CSTORM_SP_STATUS_BLOCK_OFFSET(func) + OFFSETOF(struct hc_sp_status_block, index_values) + (hc_sb_info->hc_index_value * sizeof(u16_t))), &rd_val, BAR_CSTRORM_INTMEM);
        DbgBreakIfAll(rd_val != 0);
        break;
    case STATUS_BLOCK_NORMAL_SL_TYPE:
        if (!LM_SB_ID_VALID(pdev, sb_id))
        {
            return LM_STATUS_INVALID_PARAMETER;
        }
        LM_INTMEM_WRITE32(PFDEV(pdev), CSTORM_HC_SYNC_LINE_DHC_OFFSET(hc_sb_info->hc_index_value, fw_sb_id), 0, BAR_CSTRORM_INTMEM);
        LM_INTMEM_READ32(PFDEV(pdev), CSTORM_HC_SYNC_LINE_DHC_OFFSET(hc_sb_info->hc_index_value, fw_sb_id), &rd_val_32, BAR_CSTRORM_INTMEM);
        DbgBreakIfAll(rd_val_32 != 0);
        //Go to zeroing index value without break
    case STATUS_BLOCK_NORMAL_TYPE:
        if (CHIP_IS_E1x(PFDEV(pdev))) {
            LM_INTMEM_WRITE16(PFDEV(pdev), CSTORM_HC_SYNC_LINE_INDEX_E1X_OFFSET(hc_sb_info->hc_index_value, fw_sb_id), 0, BAR_CSTRORM_INTMEM);
            LM_INTMEM_READ16(PFDEV(pdev), CSTORM_HC_SYNC_LINE_INDEX_E1X_OFFSET(hc_sb_info->hc_index_value, fw_sb_id), &rd_val, BAR_CSTRORM_INTMEM);
        } else {
            LM_INTMEM_WRITE16(PFDEV(pdev), CSTORM_HC_SYNC_LINE_INDEX_E2_OFFSET(hc_sb_info->hc_index_value, fw_sb_id), 0, BAR_CSTRORM_INTMEM);
            LM_INTMEM_READ16(PFDEV(pdev), CSTORM_HC_SYNC_LINE_INDEX_E2_OFFSET(hc_sb_info->hc_index_value, fw_sb_id), &rd_val, BAR_CSTRORM_INTMEM);
        }
        DbgBreakIfAll(rd_val != 0);
        if (CHIP_IS_E1x(pdev)) {
            LM_INTMEM_WRITE16(PFDEV(pdev), (CSTORM_STATUS_BLOCK_OFFSET(fw_sb_id) + OFFSETOF(struct hc_status_block_e1x, index_values) + (hc_sb_info->hc_index_value * sizeof(u16_t))), 0, BAR_CSTRORM_INTMEM);
            LM_INTMEM_READ16 (PFDEV(pdev), (CSTORM_STATUS_BLOCK_OFFSET(fw_sb_id) + OFFSETOF(struct hc_status_block_e1x, index_values) + (hc_sb_info->hc_index_value * sizeof(u16_t))), &rd_val, BAR_CSTRORM_INTMEM);
        } else {
            LM_INTMEM_WRITE16(PFDEV(pdev), (CSTORM_STATUS_BLOCK_OFFSET(fw_sb_id) + OFFSETOF(struct hc_status_block_e2, index_values) + (hc_sb_info->hc_index_value * sizeof(u16_t))), 0, BAR_CSTRORM_INTMEM);
            LM_INTMEM_READ16 (PFDEV(pdev), (CSTORM_STATUS_BLOCK_OFFSET(fw_sb_id) + OFFSETOF(struct hc_status_block_e2, index_values) + (hc_sb_info->hc_index_value * sizeof(u16_t))), &rd_val, BAR_CSTRORM_INTMEM);

        }
        break;
    default:
        DbgMessage1(NULL, FATAL, "Invalid hc_sb value: 0x%x.\n", hc_sb_info->hc_sb);
        DbgBreakIf(1);
    }
    /* We read from the same memory and verify that it's 0 to make sure that the value was written to the grc and was not delayed in the pci */
    DbgBreakIfAll(rd_val != 0);

    return LM_STATUS_SUCCESS;
}

/*
 * reset txq, rxq, rcq counters for L2 client connection
 *
 * assumption: the cid equals the chain idx
 */
lm_status_t lm_clear_eth_con_resc( IN struct _lm_device_t *pdev,
                                   IN u8_t const          cid )
{
    if CHK_NULL(pdev)
    {
        return LM_STATUS_INVALID_PARAMETER;
    }
    if (cid >= MAX_ETH_CONS)
    {
        return LM_STATUS_INVALID_PARAMETER;
    }
    if (cid >= MAX_RX_CHAIN)
    {
        return LM_STATUS_INVALID_PARAMETER;
    }

    if (cid >= MAX_TX_CHAIN)
    {
        return LM_STATUS_INVALID_PARAMETER;
    }

    /* Set hw consumer index pointers to null, so we won't get rx/tx completion */
    /* for this connection, next time we'll load it                             */

    // Regardless the attributes we "clean' the TX status block

    if(GET_FLAGS(pdev->params.l2_cli_con_params[cid].attributes,LM_CLIENT_ATTRIBUTES_TX))
    {
        /* first set the hw consumer index pointers to null, and only then clear the pkt_idx value
         * to avoid a race when servicing interrupt at the same time */
        lm_clear_chain_sb_cons_idx(pdev, cid, &LM_TXQ(pdev, cid).hc_sb_info, &LM_TXQ(pdev, cid).hw_con_idx_ptr);
        LM_TXQ(pdev, cid).pkt_idx = 0;
    }

    if(GET_FLAGS(pdev->params.l2_cli_con_params[cid].attributes,LM_CLIENT_ATTRIBUTES_RX))
    {
        lm_clear_chain_sb_cons_idx(pdev, cid, &LM_RCQ(pdev, cid).hc_sb_info, &LM_RCQ(pdev, cid).hw_con_idx_ptr);
    }
    //s_list_init(&LM_RXQ(pdev, cid).active_descq, NULL, NULL, 0);
    //s_list_init(&LM_RXQ(pdev, cid).free_descq, NULL, NULL, 0);

    return LM_STATUS_SUCCESS;
}

// should be same as ceil (math.h) doesn't support u64_t
#define _ceil( _x_32, _divisor_32 ) ((_x_32 / _divisor_32) + ( (_x_32%_divisor_32) ? 1 : 0))

lm_status_t
lm_alloc_client_con_resc(
    IN struct _lm_device_t *pdev,
    IN u32_t        const   cid,
    IN lm_cli_idx_t const   lm_cli_idx
    )
{
    lm_status_t  lm_status = LM_STATUS_SUCCESS;
    u16_t   l2_rx_bd_page_cnt = 0;

    if CHK_NULL(pdev)
    {
        return LM_STATUS_INVALID_PARAMETER;
    }

    if (cid >= MAX_RX_CHAIN)
    {
        return LM_STATUS_INVALID_PARAMETER;
    }

    if(GET_FLAGS(pdev->params.l2_cli_con_params[cid].attributes,LM_CLIENT_ATTRIBUTES_TX))
    {
        lm_status = lm_alloc_txq(pdev,
                                 cid,
                                 (u16_t)pdev->params.l2_tx_bd_page_cnt[lm_cli_idx],
                                 (u16_t)pdev->params.l2_tx_coal_buf_cnt[lm_cli_idx]);
        if (lm_status != LM_STATUS_SUCCESS)
        {
            return lm_status;
        }
    }

    if(GET_FLAGS(pdev->params.l2_cli_con_params[cid].attributes,LM_CLIENT_ATTRIBUTES_RX))
    {
        l2_rx_bd_page_cnt =_ceil( pdev->params.l2_cli_con_params[cid].num_rx_desc, 500 );
        lm_status = lm_alloc_rxq(pdev,
                                 cid,
                                 l2_rx_bd_page_cnt,
                                 pdev->params.l2_cli_con_params[cid].num_rx_desc);

        if (lm_status != LM_STATUS_SUCCESS)
        {
            return lm_status;
        }

        lm_status = lm_alloc_rcq(pdev,
                                 cid,
                                 (u16_t)l2_rx_bd_page_cnt * LM_RX_BD_CQ_SIZE_RATIO);
        if (lm_status != LM_STATUS_SUCCESS)
        {
            return lm_status;
        }
    }
    return LM_STATUS_SUCCESS;
}


lm_status_t
lm_setup_client_con_params( IN struct _lm_device_t            *pdev,
                            IN u8_t const                      chain_idx,
                            IN struct _lm_client_con_params_t *cli_params )
{
    lm_rx_chain_t* rxq_chain = NULL;

    if (CHK_NULL(pdev) ||
        CHK_NULL(cli_params) ||
        ERR_IF((ARRSIZE(pdev->params.l2_cli_con_params) <= chain_idx) ||
               (CHIP_IS_E1H(pdev) && (chain_idx >= ETH_MAX_RX_CLIENTS_E1H)) || /* TODO E2 add IS_E2*/
               (CHIP_IS_E1(pdev) && (chain_idx >= ETH_MAX_RX_CLIENTS_E1)) ))
    {
        return LM_STATUS_INVALID_PARAMETER;
    }

    mm_memcpy(&pdev->params.l2_cli_con_params[chain_idx], cli_params, sizeof(struct _lm_client_con_params_t));;

    // update rxq_chain strucutre
    rxq_chain           = &LM_RXQ(pdev, chain_idx);
    rxq_chain->lah_size = pdev->params.l2_cli_con_params[chain_idx].lah_size;

    return LM_STATUS_SUCCESS;
}

lm_status_t
lm_init_client_con( IN struct _lm_device_t *pdev,
                    IN u8_t const          chain_idx,
                    IN u8_t const          b_alloc )
{
    lm_status_t  lm_status  = LM_STATUS_SUCCESS;
    u8_t         lm_cli_idx = LM_CHAIN_IDX_CLI(pdev, chain_idx); // FIXME!!!

    if CHK_NULL(pdev)
    {
        return LM_STATUS_INVALID_PARAMETER;
    }

    if (b_alloc)
    {
        lm_status = lm_alloc_client_con_resc(pdev, chain_idx, LM_CHAIN_IDX_CLI(pdev, chain_idx));

        if (lm_status != LM_STATUS_SUCCESS)
        {
            return lm_status;
        }

        /* On allocation, init the clients objects... do this only on allocation, on setup, we'll need
         * the info to reconfigure... */
        ecore_init_mac_obj(pdev,
                       &pdev->client_info[chain_idx].mac_obj,
                       LM_FW_CLI_ID(pdev, chain_idx),
                       chain_idx,
                       FUNC_ID(pdev),
                       LM_SLOWPATH(pdev, mac_rdata)[lm_cli_idx],
                       LM_SLOWPATH_PHYS(pdev, mac_rdata)[lm_cli_idx],
                       ECORE_FILTER_MAC_PENDING,
                       (unsigned long *)&pdev->client_info[chain_idx].sp_mac_state,
                       ECORE_OBJ_TYPE_RX_TX,
                       &pdev->slowpath_info.macs_pool);


        if (!CHIP_IS_E1(pdev))
        {
            ecore_init_vlan_mac_obj(pdev,
                               &pdev->client_info[chain_idx].mac_vlan_obj,
                               LM_FW_CLI_ID(pdev, chain_idx),
                               chain_idx,
                               FUNC_ID(pdev),
                               LM_SLOWPATH(pdev, mac_rdata)[lm_cli_idx],
                               LM_SLOWPATH_PHYS(pdev, mac_rdata)[lm_cli_idx],
                               ECORE_FILTER_VLAN_MAC_PENDING,
                               (unsigned long *)&pdev->client_info[chain_idx].sp_mac_state,
                               ECORE_OBJ_TYPE_RX_TX,
                               &pdev->slowpath_info.macs_pool,
                               &pdev->slowpath_info.vlans_pool);

        }
    }


    lm_status = lm_setup_client_con_resc(pdev, chain_idx);

    return lm_status;
}

/* This function reset a path (e2) or a chip (e1/e1.5)
 * includeing or excluding the nig (b_with_nig)
 */
void lm_reset_path( IN struct _lm_device_t *pdev,
                    IN const  u8_t          b_with_nig )
{
    const u32_t reg_1_clear = b_with_nig ? 0xd3ffffff : 0xd3ffff7f ;
    u32_t reg_2_clear = 0x1400;

    DbgMessage1(pdev,WARN,"lm_reset_path:%sreset [begin]\n", b_with_nig ? " (with NIG) " : " ");

    /* reset device */
    REG_WR(pdev, GRCBASE_MISC+ MISC_REGISTERS_RESET_REG_1_CLEAR, reg_1_clear );

    if (CHIP_IS_E3(pdev))
    {
        // New blocks that need to be taken out of reset
        // Mstat0 - bit 24 of RESET_REG_2
        // Mstat1 - bit 25 of RESET_REG_2
        reg_2_clear |= ((1<<25) | (1<<24)) ;;
    }

    REG_WR(pdev, GRCBASE_MISC+ MISC_REGISTERS_RESET_REG_2_CLEAR, reg_2_clear);

    if( b_with_nig  )
    {
        lm_set_nig_reset_called(pdev);
        /* take the NIG out of reset */
        REG_WR(pdev, GRCBASE_MISC+ MISC_REGISTERS_RESET_REG_1_SET, MISC_REGISTERS_RESET_REG_1_RST_NIG);
    }

    pdev->vars.b_is_dmae_ready = FALSE;

    DbgMessage1(pdev,WARN,"lm_reset_path:%sreset [end]\n", b_with_nig ? " (with NIG) ": " ");

    // rbc_reset_workaround() should be called AFTER nig is out of reset
    // otherwise the probability that nig will be accessed by bootcode while
    // it is in reset is very high (this will cause GRC_TIMEOUT)

    // TODO - we still need to deal with CQ45947 (calling rbc_reset_workaround before nig is out of reset will
    //        cause the grc_timeout to happen
    DbgMessage1(pdev,WARN,"lm_reset_path:%sreset rbcp wait [begin]\n", b_with_nig ? " (with NIG) ": " ");
    rbc_reset_workaround(pdev);
    DbgMessage1(pdev,WARN,"lm_reset_path:%sreset rbcp wait [end]\n", b_with_nig ? " (with NIG) ": " ");
}

#define MCP_EMUL_TIMEOUT 200000    /* 200 ms (in us) */
#define MCP_TIMEOUT      5000000   /* 5 seconds (in us) */
#define MCP_ONE_TIMEOUT  100000    /* 100 ms (in us) */

/**
 * Waits for MCP_ONE_TIMEOUT or MCP_ONE_TIMEOUT*10,
 * depending on the HW type.
 *
 * @param pdev
 */
static __inline void lm_mcp_wait_one (
    IN  struct _lm_device_t * pdev
    )
{
    /* special handling for emulation and FPGA,
       wait 10 times longer */
    if ((CHIP_REV(pdev) == CHIP_REV_EMUL) ||
        (CHIP_REV(pdev) == CHIP_REV_FPGA)) {
        mm_wait(pdev, MCP_ONE_TIMEOUT*10);
    } else {
        mm_wait(pdev, MCP_ONE_TIMEOUT);
    }
}


#if !defined(b710)

/**
 * Prepare CLP to MCP reset.
 *
 * @param pdev Device handle
 * @param magic_val Old value of `magic' bit.
 */
void lm_clp_reset_prep(
    IN  struct _lm_device_t * pdev,
    OUT u32_t               * magic_val
    )
{
    u32_t val = 0;
    u32_t offset;

#define SHARED_MF_CLP_MAGIC  0x80000000 /* `magic' bit */

    ASSERT_STATIC(sizeof(struct mf_cfg) % sizeof(u32_t) == 0);

    /* Do some magic... */
    offset = OFFSETOF(mf_cfg_t, shared_mf_config.clp_mb);
    LM_MFCFG_READ(pdev, offset, &val);
    *magic_val = val & SHARED_MF_CLP_MAGIC;
    LM_MFCFG_WRITE(pdev, offset, val | SHARED_MF_CLP_MAGIC);
}

/**
 * Restore the value of the `magic' bit.
 *
 * @param pdev Device handle.
 * @param magic_val Old value of the `magic' bit.
 */
void lm_clp_reset_done(
    IN  struct _lm_device_t * pdev,
    IN  u32_t                 magic_val
    )
{
    u32_t val = 0;
    u32_t offset;

    /* Restore the `magic' bit value... */
    offset = OFFSETOF(mf_cfg_t, shared_mf_config.clp_mb);
    LM_MFCFG_READ(pdev, offset, &val);
    LM_MFCFG_WRITE(pdev, offset, (val & (~SHARED_MF_CLP_MAGIC)) | magic_val);
}

#endif // !b710

u8_t lm_is_mcp_detected(
    IN struct _lm_device_t *pdev
    )
{
    return pdev->hw_info.mcp_detected;
}

/**
 * @Description
 *      Prepares for MCP reset: takes care of CLP configurations
 *      (saves it aside to resotre later) .
 *
 * @param pdev
 * @param magic_val Old value of 'magic' bit.
 */
lm_status_t lm_reset_mcp_prep(lm_device_t *pdev, u32_t * magic_val)
{
    u32_t shmem;
    u32_t validity_offset;

    /* Set `magic' bit in order to save MF config */
    if (!CHIP_IS_E1(pdev))
    {
        lm_clp_reset_prep(pdev, magic_val);
    }

    /* Get shmem offset */
    shmem = REG_RD(pdev, MISC_REG_SHARED_MEM_ADDR);
    validity_offset = OFFSETOF(shmem_region_t, validity_map[0]);

    /* Clear validity map flags */
    if( shmem > 0 )
    {
        REG_WR(pdev, shmem + validity_offset, 0);
    }

    return LM_STATUS_SUCCESS;
}

lm_status_t lm_reset_mcp_comp(lm_device_t *pdev, u32_t magic_val)
{
    lm_status_t lm_status         = LM_STATUS_SUCCESS;
    u32_t       shmem_sig_timeout = 0;
    u32_t       validity_offset   = 0;
    u32_t       shmem             = 0;
    u32_t       val               = 0;
    u32_t       cnt               = 0;

#ifdef _VBD_CMD_
    return LM_STATUS_SUCCESS;
#endif

    /* Get shmem offset */
    shmem = REG_RD(pdev, MISC_REG_SHARED_MEM_ADDR);
    if( shmem == 0 ) {
        DbgMessage(pdev, FATAL, "Shmem 0 return failure\n");
        lm_status = LM_STATUS_FAILURE;
        goto exit_lbl;
    }

    ASSERT_STATIC(0 != MCP_ONE_TIMEOUT);

    if (CHIP_REV(pdev) == CHIP_REV_EMUL)
        shmem_sig_timeout = MCP_EMUL_TIMEOUT / MCP_ONE_TIMEOUT; // 200ms
    else
        shmem_sig_timeout = MCP_TIMEOUT / MCP_ONE_TIMEOUT; // 5sec

    validity_offset = OFFSETOF(shmem_region_t, validity_map[0]);

    /* Wait for MCP to come up */
    for(cnt = 0; cnt < shmem_sig_timeout; cnt++)
    {
        /* TBD: its best to check validity map of last port. currently checks on port 0. */
        val = REG_RD(pdev, shmem + validity_offset);
        DbgMessage3(pdev, INFORM, "shmem 0x%x validity map(0x%x)=0x%x\n", shmem, shmem + validity_offset, val);

        /* check that shared memory is valid. */
        if((val & (SHR_MEM_VALIDITY_DEV_INFO | SHR_MEM_VALIDITY_MB)) ==
           (SHR_MEM_VALIDITY_DEV_INFO|SHR_MEM_VALIDITY_MB)) {
            break;
        }

        lm_mcp_wait_one(pdev);
    }

    DbgMessage2(pdev, INFORM ,"Cnt=%d Shmem validity map 0x%x\n",cnt, val);

    /* Check that shared memory is valid. This indicates that MCP is up. */
    if((val & (SHR_MEM_VALIDITY_DEV_INFO | SHR_MEM_VALIDITY_MB)) !=
       (SHR_MEM_VALIDITY_DEV_INFO | SHR_MEM_VALIDITY_MB))
    {
        DbgMessage(pdev, FATAL, "Shmem signature not present. MCP is not up !!\n");
        lm_status = LM_STATUS_FAILURE;
        goto exit_lbl;
    }

exit_lbl:

    if (!CHIP_IS_E1(pdev))
    {
        /* Restore `magic' bit value */
        lm_clp_reset_done(pdev, magic_val);
    }

    return lm_status;
}

lm_status_t lm_reset_mcp(
    IN struct _lm_device_t *pdev
    )
{

    u32_t magic_val = 0;

    lm_status_t lm_status = LM_STATUS_SUCCESS;

    DbgMessage(pdev, VERBOSE, "Entered lm_reset_mcp\n");

    lm_reset_mcp_prep(pdev, &magic_val);

    /* Reset the MCP */
    REG_WR(pdev, GRCBASE_MISC+ MISC_REGISTERS_RESET_REG_2_CLEAR,
         MISC_REGISTERS_RESET_REG_2_RST_MCP_N_RESET_REG_HARD_CORE  |
         MISC_REGISTERS_RESET_REG_2_RST_MCP_N_HARD_CORE_RST_B      |
         MISC_REGISTERS_RESET_REG_2_RST_MCP_N_RESET_CMN_CPU        |
         MISC_REGISTERS_RESET_REG_2_RST_MCP_N_RESET_CMN_CORE);
    mm_wait(pdev, 100000);

    lm_status = lm_reset_mcp_comp(pdev, magic_val);

    return lm_status;
}

void disable_blocks_attention(struct _lm_device_t *pdev)
{
#define MASK_VALUE_GENERATE(_val) ((u32_t)((((u64_t)0x1)<<_val)-1))
    typedef struct _block_mask_info_t
    {
        u32_t reg_offset;    /* the register offset */
        u32_t mask_value[3]; /* the mask value per hw (e1 =0 /e1.5 = 1/e2 = 2)*/
    } block_mask_info_t;

    u8_t  chip_idx   = 0;
    u32_t mask_idx   = 0;
    u32_t val        = 0;
    u32_t offset     = 0;
    u32_t mask_value = 0;

    static const block_mask_info_t init_mask_values_arr[] =
    {
        { ATC_REG_ATC_INT_MASK,           { 0,
                                            0,
                                            6 } },

        { BRB1_REG_BRB1_INT_MASK,         { 19,
                                            19,
                                            19} },

        { CCM_REG_CCM_INT_MASK,           { 11,
                                            11,
                                            11 } },

        { CDU_REG_CDU_INT_MASK,           { 7,
                                            7,
                                            7 } },

        { CFC_REG_CFC_INT_MASK,           { 2,
                                            2,
                                            2  } },

        { CSDM_REG_CSDM_INT_MASK_0,       { 32,
                                            32,
                                            32 } },

        { CSDM_REG_CSDM_INT_MASK_1,       { 10,
                                            10,
                                            11 } },

#if 0
        { CSEM_REG_CSEM_INT_MASK_0,       { 32,
                                            32,
                                            32 } },

        { CSEM_REG_CSEM_INT_MASK_1,       { 10,
                                            11,
                                            11} },

        { DBG_REG_DBG_INT_MASK,           { 2,
                                            2,
                                            2 } },
#endif //0

        { DMAE_REG_DMAE_INT_MASK,         { 2,
                                            2,
                                            2 } },

        { DORQ_REG_DORQ_INT_MASK,         { 5,
                                            5,
                                            6 } },
#if 0
        { HC_REG_HC_INT_MASK,             { 7,
                                            7,
                                            7 } },
#endif //0

        { IGU_REG_IGU_INT_MASK,           { 0,
                                            0,
                                            11 } },
#if 0
        { MISC_REGISTERS_MISC_INT_MASK,   { 4,
                                            4,
                                            8 } },

        { NIG_REGISTERS_NIG_INT_MASK_0,   { 32,
                                            32,
                                            32 } },

        { NIG_REGISTERS_NIG_INT_MASK_1,   { 2,
                                            4,
                                            14 } },

        { PB_REGISTERS_PB_INT_MASK,       { 2,
                                            2,
                                            2} },
#endif // 0

        { PBF_REG_PBF_INT_MASK,           { 5,
                                            5,
                                            7 } },

        { PGLUE_B_REG_PGLUE_B_INT_MASK,   { 0,
                                            0,
                                            9 } },
#if 0
        { PRS_REG_PRS_INT_MASK,           { 1,
                                            1,
                                            1 } },
#endif // 0

        { PXP2_REG_PXP2_INT_MASK_0,       { 25,
                                            32,
                                            32 } },

#if 0
        { PXP2_REG_PXP2_INT_MASK_1,       { 0,
                                            6,
                                            16} },
#endif //0

        { PXP_REG_PXP_INT_MASK_0,         { 32,
                                            32,
                                            32 } },

        { PXP_REG_PXP_INT_MASK_1,         { 5,
                                            5,
                                            8 } },

        { QM_REG_QM_INT_MASK,             { 2,
                                            2,
                                            14 } },
#if 0
        { SEM_FAST_REG_SEM_FAST_INT_MASK, { 1, // This offset is actually 4 different registers (per SEM)
                                            1,
                                            1} },

        { SRC_REG_SRC_INT_MASK,           { 1,
                                            3,
                                            3 } },
#endif //0

        { TCM_REG_TCM_INT_MASK,           { 11,
                                            11,
                                            11 } },

        { TM_REG_TM_INT_MASK,             { 1,
                                            1,
                                            1} },

        { TSDM_REG_TSDM_INT_MASK_0,       { 32,
                                            32,
                                            32 } },

        { TSDM_REG_TSDM_INT_MASK_1,       { 10,
                                            10,
                                            11 } },
#if 0
        { TSEM_REG_TSEM_INT_MASK_0,       { 32,
                                            32,
                                            32 } },

        { TSEM_REG_TSEM_INT_MASK_1,       { 10,
                                            11,
                                            13 } },
#endif // 0

        { UCM_REG_UCM_INT_MASK,           { 11,
                                            11,
                                            11} },

        { USDM_REG_USDM_INT_MASK_0,       { 32,
                                            32,
                                            32 } },

        { USDM_REG_USDM_INT_MASK_1,       { 10,
                                            10,
                                            11 } },
#if 0
        { USEM_REG_USEM_INT_MASK_0,       { 32,
                                            32,
                                            32 } },

        { USEM_REG_USEM_INT_MASK_1,       { 10,
                                            11,
                                            11 } },
#endif //0

        { VFC_REG_VFC_INT_MASK,           { 0,
                                            0,
                                            1 } },

        { XCM_REG_XCM_INT_MASK,           { 14,
                                            14,
                                            14 } },

        { XSDM_REG_XSDM_INT_MASK_0,       { 32,
                                            32,
                                            32 } },

        { XSDM_REG_XSDM_INT_MASK_1,       { 10,
                                            10,
                                            11} },
#if 0
        { XSEM_REG_XSEM_INT_MASK_0,      { 32,
                                           32,
                                           32 } },

        { XSEM_REG_XSEM_INT_MASK_1,      { 10,
                                           11,
                                           13 } } ,
#endif // 0
    }; // init_mask_values_arr

    if CHIP_IS_E1( pdev )
    {
        chip_idx = 0; // E1.0
    }
    else if CHIP_IS_E1H(pdev)
    {
        chip_idx = 1; // E1.5
    }
    else if CHIP_IS_E2E3(pdev)
    {
        chip_idx = 2; // E2
    }
    else
    {
        // New chip!!!
        DbgBreakIf(1); // E??
    }

    DbgBreakIf( chip_idx >= ARRSIZE( init_mask_values_arr[0].mask_value ) );

    for( mask_idx = 0; mask_idx < ARRSIZE(init_mask_values_arr);  mask_idx++ )
    {
        mask_value = init_mask_values_arr[mask_idx].mask_value[chip_idx] ;

        if( mask_value )
        {
            val        = MASK_VALUE_GENERATE(mask_value);
            offset     = init_mask_values_arr[mask_idx].reg_offset;
            REG_WR(pdev, offset, val );
        }
    }
    /*

    REG_WR(pdev,PXP_REG_PXP_INT_MASK_0,0xffffffff);
    if (IS_E2(pdev)) {
        REG_WR(pdev,PXP_REG_PXP_INT_MASK_1,0xff);
    } else {
    REG_WR(pdev,PXP_REG_PXP_INT_MASK_1,0x1f);
    }
    REG_WR(pdev,DORQ_REG_DORQ_INT_MASK,0x1f);
    REG_WR(pdev,CFC_REG_CFC_INT_MASK ,0x3);
    REG_WR(pdev,QM_REG_QM_INT_MASK ,0x3);
    REG_WR(pdev,TM_REG_TM_INT_MASK ,0x1);
    REG_WR(pdev,XSDM_REG_XSDM_INT_MASK_0 ,0xffffffff);
    REG_WR(pdev,XSDM_REG_XSDM_INT_MASK_1 ,0x3ff);
    REG_WR(pdev,XCM_REG_XCM_INT_MASK,0x3fff);
    //REG_WR(pdev,XSEM_REG_XSEM_INT_MASK_0 ,0);
    //REG_WR(pdev,XSEM_REG_XSEM_INT_MASK_1 ,0);
    REG_WR(pdev,USDM_REG_USDM_INT_MASK_0 ,0xffffffff);
    REG_WR(pdev,USDM_REG_USDM_INT_MASK_1 ,0x3ff);
    REG_WR(pdev,UCM_REG_UCM_INT_MASK ,0x7ff);
    //REG_WR(pdev,USEM_REG_USEM_INT_MASK_0 ,0);
    //REG_WR(pdev,USEM_REG_USEM_INT_MASK_1 ,0);
    REG_WR(pdev,GRCBASE_UPB+PB_REG_PB_INT_MASK ,0x3);
    REG_WR(pdev,CSDM_REG_CSDM_INT_MASK_0 ,0xffffffff);
    REG_WR(pdev,CSDM_REG_CSDM_INT_MASK_1 ,0x3ff);
    REG_WR(pdev,CCM_REG_CCM_INT_MASK ,0x7ff);
    //REG_WR(pdev,CSEM_REG_CSEM_INT_MASK_0 ,0);
    //REG_WR(pdev,CSEM_REG_CSEM_INT_MASK_1 ,0);

    REG_WR(pdev,PXP2_REG_PXP2_INT_MASK_0,0xffffffff);

    REG_WR(pdev,TSDM_REG_TSDM_INT_MASK_0 ,0xffffffff);
    REG_WR(pdev,TSDM_REG_TSDM_INT_MASK_1 ,0x3ff);
    REG_WR(pdev,TCM_REG_TCM_INT_MASK ,0x7ff);
    //REG_WR(pdev,TSEM_REG_TSEM_INT_MASK_0 ,0);
    //REG_WR(pdev,TSEM_REG_TSEM_INT_MASK_1 ,0);
    REG_WR(pdev,CDU_REG_CDU_INT_MASK ,0x7f);
    REG_WR(pdev,DMAE_REG_DMAE_INT_MASK ,0x3);
    //REG_WR(pdev,GRCBASE_MISC+MISC_REGISTERS_MISC_INT_MASK ,0);
    //MASK BIT 3,4
    REG_WR(pdev,PBF_REG_PBF_INT_MASK ,0x1f);
    */

    // disable MCP's attentions
    REG_WR(pdev,MISC_REG_AEU_ENABLE4_FUNC_0_OUT_0,0);
    REG_WR(pdev,MISC_REG_AEU_ENABLE4_FUNC_0_OUT_1,0);
    REG_WR(pdev,MISC_REG_AEU_ENABLE4_FUNC_0_OUT_2,0);
    REG_WR(pdev,MISC_REG_AEU_ENABLE4_FUNC_0_OUT_3,0);
    REG_WR(pdev,MISC_REG_AEU_ENABLE4_FUNC_1_OUT_0,0);
    REG_WR(pdev,MISC_REG_AEU_ENABLE4_FUNC_1_OUT_1,0);
    REG_WR(pdev,MISC_REG_AEU_ENABLE4_FUNC_1_OUT_2,0);
    REG_WR(pdev,MISC_REG_AEU_ENABLE4_FUNC_1_OUT_3,0);
    REG_WR(pdev,MISC_REG_AEU_ENABLE4_FUNC_0_OUT_4,0);
    REG_WR(pdev,MISC_REG_AEU_ENABLE4_FUNC_0_OUT_5,0);
    REG_WR(pdev,MISC_REG_AEU_ENABLE4_FUNC_0_OUT_6,0);
    REG_WR(pdev,MISC_REG_AEU_ENABLE4_FUNC_0_OUT_7,0);
    REG_WR(pdev,MISC_REG_AEU_ENABLE4_FUNC_1_OUT_4,0);
    REG_WR(pdev,MISC_REG_AEU_ENABLE4_FUNC_1_OUT_5,0);
    REG_WR(pdev,MISC_REG_AEU_ENABLE4_FUNC_1_OUT_6,0);
    REG_WR(pdev,MISC_REG_AEU_ENABLE4_FUNC_1_OUT_7,0);
}


void lm_clear_non_def_status_block(struct _lm_device_t *pdev, u8_t  sb_id, u8_t port)
{
    u32_t index = 0;
    u8_t func = 0;
    u8_t fw_sb_id = LM_FW_SB_ID(pdev, sb_id);


    DbgBreakIf(!pdev);
    DbgMessage2(pdev, INFORMi, "clear_status_block: sb_id:%d, port:%d,\n",
        sb_id, port);

    func = FUNC_ID(pdev);

    /* nullify the status block */
    DbgBreakIf((CSTORM_STATUS_BLOCK_SIZE % 4) != 0);
    DbgBreakIf((CSTORM_STATUS_BLOCK_DATA_SIZE % 4) != 0);
    DbgBreakIf((CSTORM_SYNC_BLOCK_SIZE % 4) != 0);

    lm_ndsb_func_disable(pdev, fw_sb_id);

    if (IS_PFDEV(pdev)) {
        REG_WR_DMAE_LEN_ZERO(PFDEV(pdev), CSEM_REG_FAST_MEMORY + CSTORM_SYNC_BLOCK_OFFSET(fw_sb_id), CSTORM_SYNC_BLOCK_SIZE / 4);
        REG_WR_DMAE_LEN_ZERO(PFDEV(pdev), CSEM_REG_FAST_MEMORY + CSTORM_STATUS_BLOCK_OFFSET(fw_sb_id), CSTORM_STATUS_BLOCK_SIZE / 4);
        REG_WR_DMAE_LEN_ZERO(PFDEV(pdev), CSEM_REG_FAST_MEMORY + CSTORM_STATUS_BLOCK_DATA_OFFSET(fw_sb_id), CSTORM_STATUS_BLOCK_DATA_SIZE / 4);
    } else {
        for (index = 0; index < CSTORM_SYNC_BLOCK_SIZE / sizeof(u32_t); index++) {
            LM_INTMEM_WRITE32(PFDEV(pdev), CSTORM_SYNC_BLOCK_OFFSET(fw_sb_id) + 4*index, 0, BAR_CSTRORM_INTMEM);
        }
        for (index = 0; index < CSTORM_STATUS_BLOCK_SIZE / sizeof(u32_t); index++) {
            LM_INTMEM_WRITE32(PFDEV(pdev), CSTORM_STATUS_BLOCK_OFFSET(fw_sb_id) + 4*index, 0, BAR_CSTRORM_INTMEM);
        }
        for (index = 0; index < CSTORM_STATUS_BLOCK_DATA_SIZE / sizeof(u32_t); index++) {
            LM_INTMEM_WRITE32(PFDEV(pdev), CSTORM_STATUS_BLOCK_DATA_OFFSET(fw_sb_id) + 4*index, 0, BAR_CSTRORM_INTMEM);
        }
    }

    if (CHIP_IS_E1x(pdev)) {
        LM_INTMEM_WRITE8(PFDEV(pdev), ((CSTORM_STATUS_BLOCK_DATA_OFFSET(fw_sb_id)) +
                                       (OFFSETOF(struct hc_status_block_data_e1x, common)) +
                                       (OFFSETOF(struct hc_sb_data, p_func)) +
                                       (OFFSETOF(struct pci_entity, pf_id))), 0xff, BAR_CSTRORM_INTMEM);
    } else {
        LM_INTMEM_WRITE8(PFDEV(pdev), ((CSTORM_STATUS_BLOCK_DATA_OFFSET(fw_sb_id)) +
                                       (OFFSETOF(struct hc_status_block_data_e2, common)) +
                                       (OFFSETOF(struct hc_sb_data, p_func)) +
                                       (OFFSETOF(struct pci_entity, pf_id))), 0xff, BAR_CSTRORM_INTMEM);
    }
}

// Check current fan failure state - report in case signaled.
void lm_check_fan_failure(struct _lm_device_t *pdev)
{
    u32_t val = 0;

    if (IS_VFDEV(pdev))
    {
        return;
    }

    val = REG_RD(pdev, MISC_REG_AEU_AFTER_INVERT_1_FUNC_0 + PORT_ID(pdev)*4);

    if( GET_FLAGS(val, AEU_INPUTS_ATTN_BITS_SPIO5))
    {
        lm_spio5_attn_everest_processing(pdev);
    }
}

/**
 * @param pdev
 *
 * @return 0 if device is ASIC.
 */
int lm_chip_is_slow(struct _lm_device_t *pdev)
{
    u32_t val = 0;

    lm_reg_rd_ind(pdev, MISC_REG_CHIP_REV, &val);

    val = (val & 0xf) << 12;

    if (val > CHIP_REV_Cx) {
        DbgMessage(pdev, VERBOSEi, "Chip is slow\n");
        return 1;
    } else {
        return 0;
    }
}

void lm_set_igu_tmode(struct _lm_device_t *pdev, u8_t tmode)
{
    pdev->vars.is_igu_test_mode = tmode;
}

u8_t lm_get_igu_tmode(struct _lm_device_t *pdev)
{
    return pdev->vars.is_igu_test_mode;
}

void lm_set_interrupt_mode(struct _lm_device_t *pdev, u32_t mode)
{
    DbgBreakIf(mode > LM_INT_MODE_MIMD);
    pdev->params.interrupt_mode = mode;
}

u32_t lm_get_interrupt_mode(struct _lm_device_t *pdev)
{
    return pdev->params.interrupt_mode;
}

u8_t lm_get_num_fp_msix_messages(struct _lm_device_t *pdev)
{
    if (INTR_BLK_TYPE(pdev) == INTR_BLK_IGU) {
        if (pdev->vars.is_igu_test_mode) {
            DbgMessage1(pdev, FATAL, "IGU test mode: returned %d fp-messages\n", pdev->hw_info.intr_blk_info.igu_info.igu_test_sb_cnt + pdev->hw_info.intr_blk_info.igu_info.igu_sb_cnt);
            return (pdev->hw_info.intr_blk_info.igu_info.igu_test_sb_cnt + pdev->hw_info.intr_blk_info.igu_info.igu_sb_cnt);
        }
        return pdev->hw_info.intr_blk_info.igu_info.igu_sb_cnt;
    } else {
        return pdev->params.sb_cnt;
    }
}

u8_t lm_get_base_msix_msg(struct _lm_device_t *pdev)
{
    if (IS_PFDEV(pdev)) {
        return 1;
    } else {
        return 0;
    }
}

u8_t lm_has_sp_msix_vector(struct _lm_device_t *pdev)
{
    if (IS_PFDEV(pdev)) {
        return TRUE;
    } else {
        return FALSE;
    }
}

u8_t lm_is_function_after_flr(struct _lm_device_t * pdev)
{
    u8_t is_after_flr = FALSE;
    if (IS_PFDEV(pdev)) {
        is_after_flr = pdev->params.is_flr;
        if (is_after_flr) {
            DbgMessage1(pdev, FATAL, "PF[%d] was FLRed\n",FUNC_ID(pdev));
        }
#ifdef VF_INVOLVED
    } else {
        is_after_flr = lm_vf_is_function_after_flr(pdev);
#endif
    }
    return is_after_flr;
}

lm_status_t lm_cleanup_after_flr(struct _lm_device_t * pdev)
{
    lm_status_t lm_status  = LM_STATUS_SUCCESS;
    u32_t wait_ms          = 10000;
    u16_t pretend_value    = 0;
    u32_t factor           = 0;
    u32_t cleanup_complete = 0;
#ifdef __LINUX
    u32_t pcie_caps_offset = 0;
#endif

    u8_t  function_for_clean_up = 0;
    u8_t  idx                   = 0;

    struct sdm_op_gen final_cleanup;

    // TODO - use here pdev->vars.clk_factor
    switch (CHIP_REV(pdev))
    {
        case CHIP_REV_EMUL:
            factor = LM_EMUL_FACTOR;
            break;
        case CHIP_REV_FPGA:
            factor = LM_FPGA_FACTOR;
            break;
        default:
            factor = 1;
            break;
    }

    wait_ms *= factor;
    pdev->flr_stats.default_wait_interval_ms = DEFAULT_WAIT_INTERVAL_MICSEC;
    if (IS_PFDEV(pdev)) {
        DbgMessage1(pdev, FATAL, "lm_cleanup_after_flr PF[%d] >>>\n",FUNC_ID(pdev));
        pdev->flr_stats.is_pf = TRUE;
        /* Re-enable target PF read access */
        REG_WR(pdev, PGLUE_B_REG_INTERNAL_PFID_ENABLE_TARGET_READ, 1);

        /*Poll on CFC per-pf usage-counter until its 0*/
        pdev->flr_stats.cfc_usage_counter = REG_WAIT_VERIFY_VAL(pdev, CFC_REG_NUM_LCIDS_INSIDE_PF, 0, wait_ms);
        DbgMessage2(pdev, FATAL, "%d*%dms waiting for CFC per pf usage counter\n",pdev->flr_stats.cfc_usage_counter,DEFAULT_WAIT_INTERVAL_MICSEC);

        /* Poll on DQ per-pf usage-counter (until full dq-cleanup is implemented) until its 0*/
        pdev->flr_stats.dq_usage_counter = REG_WAIT_VERIFY_VAL(pdev, DORQ_REG_PF_USAGE_CNT, 0, wait_ms);
        DbgMessage2(pdev, FATAL, "%d*%dms waiting for DQ per pf usage counter\n", pdev->flr_stats.dq_usage_counter, DEFAULT_WAIT_INTERVAL_MICSEC);

        /* Poll on QM per-pf usage-counter until its 0*/
        pdev->flr_stats.qm_usage_counter = REG_WAIT_VERIFY_VAL(pdev, QM_REG_PF_USG_CNT_0 + 4*FUNC_ID(pdev),0, wait_ms);
        DbgMessage2(pdev, FATAL, "%d*%dms waiting for QM per pf usage counter\n", pdev->flr_stats.qm_usage_counter, DEFAULT_WAIT_INTERVAL_MICSEC);

        /* Poll on TM per-pf-usage-counter until its 0 */

        pdev->flr_stats.tm_vnic_usage_counter = REG_WAIT_VERIFY_VAL(pdev, TM_REG_LIN0_VNIC_UC + 4*PORT_ID(pdev),0, wait_ms);
        DbgMessage3(pdev, FATAL, "%d*%dms waiting for TM%d(VNIC) per pf usage counter\n",
                    pdev->flr_stats.tm_vnic_usage_counter, DEFAULT_WAIT_INTERVAL_MICSEC, PORT_ID(pdev));

        pdev->flr_stats.tm_num_scans_usage_counter = REG_WAIT_VERIFY_VAL(pdev, TM_REG_LIN0_NUM_SCANS + 4*PORT_ID(pdev),0, wait_ms);
        DbgMessage3(pdev, FATAL, "%d*%dms waiting for TM%d(NUM_SCANS) per pf usage counter\n",
                    pdev->flr_stats.tm_num_scans_usage_counter, DEFAULT_WAIT_INTERVAL_MICSEC, PORT_ID(pdev));

        pdev->flr_stats.dmae_cx = REG_WAIT_VERIFY_VAL(pdev, lm_dmae_idx_to_go_cmd(DMAE_WB_ACCESS_FUNCTION_CMD(FUNC_ID(pdev))), 0, wait_ms);
        DbgMessage3(pdev, FATAL, "%d*%dms waiting for DMAE_REG_GO_C%d \n",
                    pdev->flr_stats.tm_num_scans_usage_counter, DEFAULT_WAIT_INTERVAL_MICSEC, DMAE_WB_ACCESS_FUNCTION_CMD(FUNC_ID(pdev)));
    } else {
        DbgMessage1(pdev, FATAL, "lm_cleanup_after_flr VF[%d] >>>\n",ABS_VFID(pdev));

/*
    VF FLR only part
a.  Wait until there are no pending ramrods for this VFid in the PF DB. - No pending VF's pending ramrod. It's based on "FLR not during driver load/unload".
    What about set MAC?

b.  Send the new "L2 connection terminate" ramrod for each L2 CID that was used by the VF,
    including sending the doorbell with the "terminate" flag. - Will be implemented in FW later

c.  Send CFC delete ramrod on all L2 connections of that VF (set the CDU-validation field to "invalid"). - part of FW cleanup. VF_TO_PF_CID must initialized in
    PF CID array*/

/*  3.  Poll on the DQ per-function usage-counter until it's 0. */
        pretend_value = ABS_FUNC_ID(pdev) | (1<<3) | (ABS_VFID(pdev) << 4);
        lm_status = lm_pretend_func(PFDEV(pdev), pretend_value);
        if (lm_status == LM_STATUS_SUCCESS) {
            pdev->flr_stats.dq_usage_counter = REG_WAIT_VERIFY_VAL(PFDEV(pdev), DORQ_REG_VF_USAGE_CNT, 0, wait_ms);
            lm_pretend_func(PFDEV(pdev), ABS_FUNC_ID(pdev));
            DbgMessage2(pdev, FATAL, "%d*%dms waiting for DQ per vf usage counter\n", pdev->flr_stats.dq_usage_counter, DEFAULT_WAIT_INTERVAL_MICSEC);
        } else {
            DbgMessage2(pdev, FATAL, "lm_pretend_func(%x) returns %d\n",pretend_value,lm_status);
            DbgMessage1(pdev, FATAL, "VF[%d]: could not read DORQ_REG_VF_USAGE_CNT\n", ABS_VFID(pdev));
            return lm_status;
        }
    }

/*  4.  Activate the FW cleanup process by activating AggInt in the FW with GRC. Set the bit of the relevant function in the AggInt bitmask,
        to indicate to the FW which function is being cleaned. Wait for the per-function completion indication in the Cstorm RAM
*/
    function_for_clean_up = IS_VFDEV(pdev) ? FW_VFID(pdev) : FUNC_ID(pdev);
    cleanup_complete = 0xFFFFFFFF;
    LM_INTMEM_READ32(PFDEV(pdev),CSTORM_FINAL_CLEANUP_COMPLETE_OFFSET(function_for_clean_up),&cleanup_complete, BAR_CSTRORM_INTMEM);
    if (cleanup_complete) {
        DbgMessage1(pdev, FATAL, "CSTORM_FINAL_CLEANUP_COMPLETE_OFFSET is %x",cleanup_complete);
        DbgBreak();
    }

    final_cleanup.command = (XSTORM_AGG_INT_FINAL_CLEANUP_INDEX << SDM_OP_GEN_COMP_PARAM_SHIFT) & SDM_OP_GEN_COMP_PARAM;
    final_cleanup.command |= (XSTORM_AGG_INT_FINAL_CLEANUP_COMP_TYPE << SDM_OP_GEN_COMP_TYPE_SHIFT) & SDM_OP_GEN_COMP_TYPE;
    final_cleanup.command |= 1 << SDM_OP_GEN_AGG_VECT_IDX_VALID_SHIFT;
    final_cleanup.command |= (function_for_clean_up << SDM_OP_GEN_AGG_VECT_IDX_SHIFT) & SDM_OP_GEN_AGG_VECT_IDX;

    DbgMessage(pdev, FATAL, "Final cleanup\n");
    REG_WR(PFDEV(pdev),XSDM_REG_OPERATION_GEN, final_cleanup.command);
    pdev->flr_stats.final_cleanup_complete = REG_WAIT_VERIFY_VAL(PFDEV(pdev), BAR_CSTRORM_INTMEM + CSTORM_FINAL_CLEANUP_COMPLETE_OFFSET(function_for_clean_up), 1, wait_ms);
    DbgMessage2(pdev, FATAL, "%d*%dms waiting for final cleanup compete\n", pdev->flr_stats.final_cleanup_complete, DEFAULT_WAIT_INTERVAL_MICSEC);
    /* Lets cleanup for next FLR final-cleanup... */
    LM_INTMEM_WRITE32(PFDEV(pdev),CSTORM_FINAL_CLEANUP_COMPLETE_OFFSET(function_for_clean_up),0, BAR_CSTRORM_INTMEM);


/*  5.  ATC cleanup. This process will include the following steps (note that ATC will not be available for phase2 of the
        integration and the following should be added only in phase3):
    a.  Optionally, wait 2 ms. This is not a must. The driver can start polling (next steps) immediately,
        but take into account that it may take time till the done indications will be set.
    b.  Wait until INVALIDATION_DONE[function] = 1
    c.  Write-clear INVALIDATION_DONE[function] */


/*  6.  Verify PBF cleanup. Do the following for all PBF queues (queues 0,1,4, that will be indicated below with N):
    a.  Make sure PBF command-queue is flushed: Read pN_tq_occupancy. Let's say that the value is X.
        This number indicates the number of occupied transmission-queue lines.
        Poll on pN_tq_occupancy and pN_tq_lines_freed_cnt until one of the following:
            i.  pN_tq_occupancy is 0 (queue is empty). OR
            ii. pN_tq_lines_freed_cnt equals has advanced (cyclically) by X (all lines that were in the queue were processed). */

    for (idx = 0; idx < 3; idx++) {
        u32_t tq_to_free;
        u32_t tq_freed_cnt_start;
        u32_t tq_occ;
        u32_t tq_freed_cnt_last;
        u32_t pbf_reg_pN_tq_occupancy = 0;
        u32_t pbf_reg_pN_tq_lines_freed_cnt = 0;

        switch (idx) {
        case 0:
            pbf_reg_pN_tq_occupancy = PBF_REG_P0_TQ_OCCUPANCY;
            pbf_reg_pN_tq_lines_freed_cnt = PBF_REG_P0_TQ_LINES_FREED_CNT;
            break;
        case 1:
            pbf_reg_pN_tq_occupancy = PBF_REG_P1_TQ_OCCUPANCY;
            pbf_reg_pN_tq_lines_freed_cnt = PBF_REG_P1_TQ_LINES_FREED_CNT;
            break;
        case 2:
            pbf_reg_pN_tq_occupancy = PBF_REG_P4_TQ_OCCUPANCY;
            pbf_reg_pN_tq_lines_freed_cnt = PBF_REG_P4_TQ_LINES_FREED_CNT;
            break;
        }
        pdev->flr_stats.pbf_queue[idx] = 0;
        tq_freed_cnt_last = tq_freed_cnt_start = REG_RD(PFDEV(pdev), pbf_reg_pN_tq_lines_freed_cnt);
        tq_occ = tq_to_free = REG_RD(PFDEV(pdev), pbf_reg_pN_tq_occupancy);
        DbgMessage2(pdev, FATAL, "TQ_OCCUPANCY[%d]      : s:%x\n", (idx == 2) ? 4 : idx, tq_to_free);
        DbgMessage2(pdev, FATAL, "TQ_LINES_FREED_CNT[%d]: s:%x\n", (idx == 2) ? 4 : idx, tq_freed_cnt_start);
        while(tq_occ && ((u32_t)S32_SUB(tq_freed_cnt_last, tq_freed_cnt_start) < tq_to_free)) {
            if (pdev->flr_stats.pbf_queue[idx]++ < wait_ms/DEFAULT_WAIT_INTERVAL_MICSEC) {
                mm_wait(PFDEV(pdev), DEFAULT_WAIT_INTERVAL_MICSEC);
                tq_occ = REG_RD(PFDEV(pdev), pbf_reg_pN_tq_occupancy);
                tq_freed_cnt_last = REG_RD(PFDEV(pdev), pbf_reg_pN_tq_lines_freed_cnt);
            } else {
                DbgMessage2(pdev, FATAL, "TQ_OCCUPANCY[%d]      : c:%x\n", (idx == 2) ? 4 : idx, tq_occ);
                DbgMessage2(pdev, FATAL, "TQ_LINES_FREED_CNT[%d]: c:%x\n", (idx == 2) ? 4 : idx, tq_freed_cnt_last);
                DbgBreak();
                break;
            }
        }
        DbgMessage3(pdev, FATAL, "%d*%dms waiting for PBF command queue[%d] is flushed\n",
                    pdev->flr_stats.pbf_queue[idx], DEFAULT_WAIT_INTERVAL_MICSEC, (idx == 2) ? 4 : idx);
    }

/*  b.  Make sure PBF transmission buffer is flushed: read pN_init_crd once and keep it in variable Y.
        Read pN_credit and keep it in X. Poll on pN_credit and pN_internal_crd_freed until one of the following:
            i.  (Y - pN_credit) is 0 (transmission buffer is empty). OR
            ii. pN_internal_crd_freed_cnt has advanced (cyclically) by Y-X (all transmission buffer lines that were occupied were freed).*/

    for (idx = 0; idx < 3; idx++) {
        u32_t init_crd;
        u32_t credit_last,credit_start;
        u32_t inernal_freed_crd_start;
        u32_t inernal_freed_crd_last = 0;
        u32_t pbf_reg_pN_init_crd = 0;
        u32_t pbf_reg_pN_credit = 0;
        u32_t pbf_reg_pN_internal_crd_freed = 0;
        switch (idx) {
        case 0:
            pbf_reg_pN_init_crd = PBF_REG_P0_INIT_CRD;
            pbf_reg_pN_credit = PBF_REG_P0_CREDIT;
            pbf_reg_pN_internal_crd_freed = PBF_REG_P0_INTERNAL_CRD_FREED_CNT;
            break;
        case 1:
            pbf_reg_pN_init_crd = PBF_REG_P1_INIT_CRD;
            pbf_reg_pN_credit = PBF_REG_P1_CREDIT;
            pbf_reg_pN_internal_crd_freed = PBF_REG_P1_INTERNAL_CRD_FREED_CNT;
            break;
        case 2:
            pbf_reg_pN_init_crd = PBF_REG_P4_INIT_CRD;
            pbf_reg_pN_credit = PBF_REG_P4_CREDIT;
            pbf_reg_pN_internal_crd_freed = PBF_REG_P4_INTERNAL_CRD_FREED_CNT;
            break;
        }
        pdev->flr_stats.pbf_transmit_buffer[idx] = 0;
        inernal_freed_crd_last = inernal_freed_crd_start = REG_RD(PFDEV(pdev), pbf_reg_pN_internal_crd_freed);
        credit_last = credit_start = REG_RD(PFDEV(pdev), pbf_reg_pN_credit);
        init_crd = REG_RD(PFDEV(pdev), pbf_reg_pN_init_crd);
        DbgMessage2(pdev, FATAL, "INIT CREDIT[%d]       : %x\n", (idx == 2) ? 4 : idx, init_crd);
        DbgMessage2(pdev, FATAL, "CREDIT[%d]            : s:%x\n", (idx == 2) ? 4 : idx, credit_start);
        DbgMessage2(pdev, FATAL, "INTERNAL_CRD_FREED[%d]: s:%x\n", (idx == 2) ? 4 : idx, inernal_freed_crd_start);
        while ((credit_last != init_crd)
               && (u32_t)S32_SUB(inernal_freed_crd_last, inernal_freed_crd_start) < (init_crd - credit_start)) {
            if (pdev->flr_stats.pbf_transmit_buffer[idx]++ < wait_ms/DEFAULT_WAIT_INTERVAL_MICSEC) {
                mm_wait(PFDEV(pdev), DEFAULT_WAIT_INTERVAL_MICSEC);
                credit_last = REG_RD(PFDEV(pdev), pbf_reg_pN_credit);
                inernal_freed_crd_last = REG_RD(PFDEV(pdev), pbf_reg_pN_internal_crd_freed);
            } else {
                DbgMessage2(pdev, FATAL, "CREDIT[%d]            : c:%x\n", (idx == 2) ? 4 : idx, credit_last);
                DbgMessage2(pdev, FATAL, "INTERNAL_CRD_FREED[%d]: c:%x\n", (idx == 2) ? 4 : idx, inernal_freed_crd_last);
                DbgBreak();
                break;
            }
        }
        DbgMessage3(pdev, FATAL, "%d*%dms waiting for PBF transmission buffer[%d] is flushed\n",
                    pdev->flr_stats.pbf_transmit_buffer[idx], DEFAULT_WAIT_INTERVAL_MICSEC, (idx == 2) ? 4 : idx);
    }

/*  7.  Wait for 100ms in order to make sure that the chip is clean, including all PCI related paths
        (in Emulation the driver can wait for 10ms*EmulationFactor, i.e.: 20s). This is especially required if FW doesn't implement
        the flows in Optional Operations (future enhancements).) */
    mm_wait(pdev, 10000*factor);

/*  8.  Verify that the transaction-pending bit of each of the function in the Device Status Register in the PCIe is cleared. */

#ifdef __LINUX
    pcie_caps_offset = mm_get_cap_offset(pdev, PCI_CAP_PCIE);
    if (pcie_caps_offset != 0 && pcie_caps_offset != 0xFFFFFFFF) {
        u32_t dev_control_and_status = 0xFFFFFFFF;
        mm_read_pci(pdev, pcie_caps_offset + PCIE_DEV_CTRL, &dev_control_and_status);
        DbgMessage1(pdev,FATAL,"Device Control&Status of PCIe caps is %x\n",dev_control_and_status);
        if (dev_control_and_status & (PCIE_DEV_STATUS_PENDING_TRANSACTION << 16)) {
            DbgBreak();
        }
    }
#else
    DbgMessage(pdev, FATAL, "Function mm_get_cap_offset is not implemented yet\n");
    DbgBreak();
#endif
/*  9.  Initialize the function as usual this should include also re-enabling the function in all the HW blocks and Storms that
    were disabled by the MCP and cleaning relevant per-function information in the chip (internal RAM related information, IGU memory etc.).
        a.  In case of VF, PF resources that were allocated for previous VF can be re-used by the new VF. If there are resources
            that are not needed by the new VF then they should be cleared.
        b.  Note that as long as slow-path prod/cons update to Xstorm is not atomic, they must be cleared by the driver before setting
            the function to "enable" in the Xstorm.
        c.  Don't forget to enable the VF in the PXP or the DMA operation for PF in the PXP. */

    if (IS_PFDEV(pdev)) {
        u32_t m_en;
        u32_t tmp = REG_RD(pdev,CFC_REG_WEAK_ENABLE_PF);
        DbgMessage1(pdev, FATAL, "CFC_REG_WEAK_ENABLE_PF is 0x%x\n",tmp);
//        REG_WR(pdev,CFC_REG_WEAK_ENABLE_PF,1);
        tmp = REG_RD(pdev,PBF_REG_DISABLE_PF);
        DbgMessage1(pdev, FATAL, "PBF_REG_DISABLE_PF is 0x%x\n",tmp);
//        REG_WR(pdev,PBF_REG_DISABLE_PF,0);

        tmp = REG_RD(pdev,IGU_REG_PCI_PF_MSI_EN);
        DbgMessage1(pdev, FATAL, "IGU_REG_PCI_PF_MSI_EN is 0x%x\n",tmp);

        tmp = REG_RD(pdev,IGU_REG_PCI_PF_MSIX_EN);
        DbgMessage1(pdev, FATAL, "IGU_REG_PCI_PF_MSIX_EN is 0x%x\n",tmp);

        tmp = REG_RD(pdev,IGU_REG_PCI_PF_MSIX_FUNC_MASK);
        DbgMessage1(pdev, FATAL, "IGU_REG_PCI_PF_MSIX_FUNC_MASK is 0x%x\n",tmp);

        tmp = REG_RD(pdev,PGLUE_B_REG_SHADOW_BME_PF_7_0_CLR);
        DbgMessage1(pdev, FATAL, "PGLUE_B_REG_SHADOW_BME_PF_7_0_CLR is 0x%x\n",tmp);

        tmp = REG_RD(pdev,PGLUE_B_REG_FLR_REQUEST_PF_7_0_CLR);
        DbgMessage1(pdev, FATAL, "PGLUE_B_REG_FLR_REQUEST_PF_7_0_CLR is 0x%x\n",tmp);

        REG_WR(pdev, PGLUE_B_REG_INTERNAL_PFID_ENABLE_MASTER, 1);
        mm_wait(pdev,999999);
        m_en = REG_RD(pdev, PGLUE_B_REG_INTERNAL_PFID_ENABLE_MASTER);
        DbgMessage1(pdev, FATAL, "M:0x%x\n",m_en);
    }

    if (IS_VFDEV(pdev))
    {
#ifdef VF_INVOLVED
//        lm_vf_enable_vf(pdev);
        lm_status = lm_vf_recycle_resc_in_pf(pdev);
        PFDEV(pdev)->vars.connections[VF_TO_PF_CID(pdev,LM_SW_LEADING_RSS_CID(pdev))].con_state = LM_CON_STATE_CLOSE;
#endif
    }

    lm_fl_reset_clear_inprogress(pdev);

    return lm_status;
}

lm_status_t lm_set_hc_flag(struct _lm_device_t *pdev, u8_t sb_id, u8_t idx, u8_t is_enable)
{
    lm_status_t lm_status = LM_STATUS_SUCCESS;
    struct hc_index_data * hc_index_entry;
    u8_t fw_sb_id;
    u8_t notify_fw = FALSE;

    if (CHIP_IS_E1x(pdev)) {
        hc_index_entry = pdev->vars.status_blocks_arr[sb_id].hc_status_block_data.e1x_sb_data.index_data + idx;
    } else {
        hc_index_entry = pdev->vars.status_blocks_arr[sb_id].hc_status_block_data.e2_sb_data.index_data + idx;
    }
    if (pdev->params.int_coalesing_mode == LM_INT_COAL_PERIODIC_SYNC) {
        if (is_enable) {
            if (!(hc_index_entry->flags & HC_INDEX_DATA_HC_ENABLED) && hc_index_entry->timeout) {
                hc_index_entry->flags |= HC_INDEX_DATA_HC_ENABLED;
                notify_fw = TRUE;
            }
        } else {
            if (hc_index_entry->flags & HC_INDEX_DATA_HC_ENABLED) {
                hc_index_entry->flags &= ~HC_INDEX_DATA_HC_ENABLED;
                notify_fw = TRUE;
            }
        }
    }
    if (notify_fw) {
        fw_sb_id = LM_FW_SB_ID(pdev, sb_id);
        if (CHIP_IS_E1x(pdev)) {
            LM_INTMEM_WRITE8(PFDEV(pdev), (CSTORM_STATUS_BLOCK_DATA_OFFSET(fw_sb_id)
                                           + OFFSETOF(struct hc_status_block_data_e1x, index_data)
                                           + sizeof(struct hc_index_data)*idx
                                           + OFFSETOF(struct hc_index_data,flags)),
                                           hc_index_entry->flags, BAR_CSTRORM_INTMEM);
        } else {
            LM_INTMEM_WRITE8(PFDEV(pdev), (CSTORM_STATUS_BLOCK_DATA_OFFSET(fw_sb_id)
                                           + OFFSETOF(struct hc_status_block_data_e2, index_data)
                                           + sizeof(struct hc_index_data)*idx
                                           + OFFSETOF(struct hc_index_data,flags)),
                                           hc_index_entry->flags, BAR_CSTRORM_INTMEM);

        }
        DbgMessage3(pdev,WARN,"HC set to %d for SB%d(index%d)\n",is_enable,sb_id,idx);
    } else {
        DbgMessage3(pdev,WARN,"HC already set to %d for SB%d(index%d)\n",is_enable,sb_id,idx);
    }

    return lm_status;
}

lm_status_t lm_set_interrupt_moderation(struct _lm_device_t *pdev, u8_t is_enable)
{
    lm_status_t lm_status = LM_STATUS_SUCCESS;
    u8_t            sb_id      = 0 ;

    pdev->params.int_coalesing_mode_disabled_by_ndis = !is_enable;
    if (pdev->params.int_coalesing_mode == LM_INT_COAL_NONE) {
        DbgMessage(pdev,WARN,"HC is not supported (disabled) in driver\n");
        return LM_STATUS_SUCCESS;
    }

    LM_FOREACH_SB_ID(pdev, sb_id)
    {
        if ((lm_status = lm_set_hc_flag(pdev, sb_id, HC_INDEX_TOE_RX_CQ_CONS, is_enable)) != LM_STATUS_SUCCESS)
            break;
        if ((lm_status = lm_set_hc_flag(pdev, sb_id, HC_INDEX_TOE_TX_CQ_CONS, is_enable)) != LM_STATUS_SUCCESS)
            break;
        if ((lm_status = lm_set_hc_flag(pdev, sb_id, HC_INDEX_ETH_RX_CQ_CONS, is_enable)) != LM_STATUS_SUCCESS)
            break;
        if ((lm_status = lm_set_hc_flag(pdev, sb_id, HC_INDEX_ETH_TX_CQ_CONS, is_enable)) != LM_STATUS_SUCCESS)
            break;

    }

    return lm_status;
}
