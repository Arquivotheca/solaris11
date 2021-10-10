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
#include "command.h"
#include "license.h"
#include "mcp_shmem.h"
#include "577xx_int_offsets.h"
#include "bd_chain.h"
#include "igu_def.h"
#include "ecore_common.h"

#ifdef VF_INVOLVED
#include "lm_vf.h"
#endif
u32_t calc_crc32( u8_t* crc32_packet, u32_t crc32_length, u32_t crc32_seed, u8_t complement) ;
unsigned long log2_align(unsigned long n);

#define DRV_DUMP_XSTORM_WAITP_ADDRESS (0x2b8a80)
#define DRV_DUMP_TSTORM_WAITP_ADDRESS (0x1b8a80)
#define DRV_DUMP_USTORM_WAITP_ADDRESS (0x338a80)
#define DRV_DUMP_CSTORM_WAITP_ADDRESS (0x238a80)

void lm_set_waitp(lm_device_t *pdev)
{
    REG_WR(pdev,DRV_DUMP_TSTORM_WAITP_ADDRESS,1);
    REG_WR(pdev,DRV_DUMP_XSTORM_WAITP_ADDRESS,1);
    REG_WR(pdev,DRV_DUMP_CSTORM_WAITP_ADDRESS,1);
    REG_WR(pdev,DRV_DUMP_USTORM_WAITP_ADDRESS,1);
}



static u32_t lm_get_shmem_mf_cfg_base(lm_device_t *pdev)
{
    u32_t shmem2_size;
    u32_t offset;
    u32_t mf_cfg_offset_value;
    offset = pdev->hw_info.shmem_base + OFFSETOF(shmem_region_t, func_mb) + E1H_FUNC_MAX * sizeof(struct drv_func_mb);
    if (pdev->hw_info.shmem_base2 != 0) {
        LM_SHMEM2_READ(pdev, OFFSETOF(shmem2_region_t,size), &shmem2_size);
        if (shmem2_size > OFFSETOF(shmem2_region_t,mf_cfg_addr)) {
            LM_SHMEM2_READ(pdev, OFFSETOF(shmem2_region_t,mf_cfg_addr), &mf_cfg_offset_value);
            if (SHMEM_MF_CFG_ADDR_NONE != mf_cfg_offset_value) {
                offset = mf_cfg_offset_value;
            }
        }
    }
    return offset;
}
/*******************************************************************************
 * Description:
 *
 * Return:
 ******************************************************************************/
static char *
val_to_decimal_string(
    char *str_buf,
    u32_t buf_size,
    u32_t val)
{
    u32_t digit;
    if(buf_size == 0)
    {
        return str_buf;
    }
    digit = val % 10;
    val = val / 10;
    if(val)
    {
        buf_size--;
        str_buf = val_to_decimal_string(str_buf, buf_size, val);
    }
    *str_buf = '0' + digit;
    str_buf++;
    return str_buf;
} /* val_to_decimal_string */

/*******************************************************************************
 * Description:
 *
 * Return:
 ******************************************************************************/
static u32_t
build_ver_string(
    char *str_buf,
    u32_t buf_size,
    u8_t major_ver,
    u8_t minor_ver,
    u16_t fix_num,
    u16_t eng_num)
{
    char *p;
    if(buf_size == 0)
    {
        return 0;
    }
    p = str_buf;
    if(buf_size - (p - str_buf) > 1)
    {
        *p = 'v';
        p++;
    }
    if(buf_size - (p - str_buf) > 1)
    {
        p = val_to_decimal_string(
            p,
            buf_size - (u32_t) PTR_SUB(p, str_buf),
            major_ver);
    }
    if(buf_size - (p - str_buf) > 1)
    {
        *p = '.';
        p++;
    }
    if(buf_size - (u32_t) PTR_SUB(p, str_buf) > 1)
    {
        p = val_to_decimal_string(
            p,
            buf_size - (u32_t) PTR_SUB(p, str_buf),
            minor_ver);
    }
    if(buf_size - (u32_t) PTR_SUB(p, str_buf) > 1)
    {
        *p = '.';
        p++;
    }
    if(buf_size - (u32_t) PTR_SUB(p, str_buf) > 1)
    {
        p = val_to_decimal_string(
            p,
            buf_size - (u32_t) PTR_SUB(p, str_buf),
            fix_num);
    }
    if(buf_size - (u32_t) PTR_SUB(p, str_buf) > 1)
    {
        *p = '.';
        p++;
    }
    if(buf_size - (u32_t) PTR_SUB(p, str_buf) > 1)
    {
        p = val_to_decimal_string(
            p,
            buf_size - (u32_t) PTR_SUB(p, str_buf),
            eng_num);
    }
    if(buf_size - (u32_t) PTR_SUB(p, str_buf) > 1)
    {
        *p = '.';
        p++;
    }
    if(buf_size - (u32_t) PTR_SUB(p, str_buf) > 1)
    {
        #if DBG
        *p = 'd';
        #else
        *p = 'r';
        #endif
        p++;
    }
    if(buf_size - (u32_t) PTR_SUB(p, str_buf) > 1)
    {
        #if DBG
        *p = 'b';
        #else
        *p = 't';
        #endif
        p++;
    }
    if(buf_size - (u32_t) PTR_SUB(p, str_buf) > 1)
    {
        #if DBG
        *p = 'g';
        #else
        *p = 'l';
        #endif
        p++;
    }
    *p = 0;
    p++;
    return (u32_t) PTR_SUB(p, str_buf);
} /* build_ver_string */
// These lm_reg_xx_ind_imp() are for blk reading when lock is acquired only once (for the whole block reading)
void
lm_reg_rd_ind_imp(
    lm_device_t *pdev,
    u32_t offset,
    u32_t *ret)
{
    DbgBreakIf(offset & 0x3);
    mm_write_pci(pdev,PCICFG_GRC_ADDRESS,offset);
    mm_read_pci(pdev,PCICFG_GRC_DATA,ret);
} /* lm_reg_rd_ind_imp */
void
lm_reg_wr_ind_imp(
    lm_device_t *pdev,
    u32_t offset,
    u32_t val)
{
    u32_t dummy;
    DbgBreakIf(offset & 0x3);
    mm_write_pci(pdev,PCICFG_GRC_ADDRESS,offset);
    mm_write_pci(pdev,PCICFG_GRC_DATA,val);
    lm_reg_rd_ind_imp(pdev,PCICFG_VENDOR_ID_OFFSET,&dummy);
} /* lm_reg_wr_ind_imp */
/*******************************************************************************
 * Description:
 *
 * Return:
 *    None.
 *
 * Note:
 *    The caller is responsible for synchronizing calls to lm_reg_rd_ind and
 *    lm_reg_wr_ind.
 ******************************************************************************/
void
lm_reg_rd_ind(
    lm_device_t *pdev,
    u32_t offset,
    u32_t *ret)
{
    MM_ACQUIRE_IND_REG_LOCK(pdev);
    lm_reg_rd_ind_imp(pdev,offset,ret);
    MM_RELEASE_IND_REG_LOCK(pdev);
} /* lm_reg_rd_ind */
/*******************************************************************************
 * Description:
 *
 * Return:
 *    None.
 *
 * Note:
 *    The caller is responsible for synchronizing calls to lm_reg_rd_ind and
 *    lm_reg_wr_ind.
 ******************************************************************************/
void
lm_reg_wr_ind(
    lm_device_t *pdev,
    u32_t offset,
    u32_t val)
{
    MM_ACQUIRE_IND_REG_LOCK(pdev);
    lm_reg_wr_ind_imp(pdev,offset,val);
    MM_RELEASE_IND_REG_LOCK(pdev);
} /* lm_reg_wr_ind */
/*******************************************************************************
 * Description:
 *
 * Return:
 ******************************************************************************/
void
lm_reg_rd_blk(
    lm_device_t *pdev,
    u32_t reg_offset,
    u32_t *buf_ptr,
    u32_t u32t_cnt)
{
    u32_t current_offset = 0;
    DbgBreakIf(reg_offset & 0x3);
    while(u32t_cnt)
    {
        *buf_ptr = REG_RD(pdev, reg_offset + current_offset);
        buf_ptr++;
        u32t_cnt--;
        current_offset += 4;
    }
} /* lm_reg_rd_blk */
/*******************************************************************************
 * Description:
 *
 * Return:
 ******************************************************************************/
void
lm_reg_rd_blk_ind(
    lm_device_t *pdev,
    u32_t reg_offset,
    u32_t *buf_ptr,
    u32_t u32t_cnt,
    u8_t acquire_lock_flag)
{
    u32_t current_offset = 0;
    if(acquire_lock_flag)
    {
        MM_ACQUIRE_IND_REG_LOCK(pdev);
    }
    while(u32t_cnt)
    {
        lm_reg_rd_ind_imp(pdev, reg_offset + current_offset, buf_ptr);
        buf_ptr++;
        u32t_cnt--;
        current_offset += 4;
    }
    if(acquire_lock_flag)
    {
        MM_RELEASE_IND_REG_LOCK(pdev);
    }
} /* lm_reg_rd_blk_ind */

/*******************************************************************************
 * Description:
 *
 * Return:
 ******************************************************************************/
void
lm_reg_wr_blk(
    lm_device_t *pdev,
    u32_t reg_offset,
    u32_t *data_ptr,
    u32_t u32t_cnt)
{
    u32_t current_offset = 0;
    DbgBreakIf(reg_offset & 0x3);
    while(u32t_cnt)
    {
        REG_WR(pdev, reg_offset + current_offset, *data_ptr);
        data_ptr++;
        u32t_cnt--;
        current_offset += 4;
    }
} /* lm_reg_wr_blk */

/*******************************************************************************
 * Description:
 *
 * Return:
 ******************************************************************************/
void
lm_reg_wr_blk_ind(
    lm_device_t *pdev,
    u32_t reg_offset,
    u32_t *data_ptr,
    u32_t u32t_cnt)
{
    u32_t current_offset = 0;

    MM_ACQUIRE_IND_REG_LOCK(pdev);
    while(u32t_cnt)
    {
        lm_reg_wr_ind_imp(pdev, reg_offset + current_offset, *data_ptr);
        data_ptr++;
        u32t_cnt--;
        current_offset += 4;
    }
    MM_RELEASE_IND_REG_LOCK(pdev);
} /* lm_reg_wr_blk_ind */

/*******************************************************************************
 * Description:
 *
 * Return:
 ******************************************************************************/
void
lm_abort( IN OUT   lm_device_t*  pdev,
          IN const lm_abort_op_t abort_op,
          IN const u32_t         idx)
{
    lm_packet_t*   pkt          = NULL;
    lm_rx_chain_t* rxq_chain    = NULL;
    lm_bd_chain_t* rx_chain_bd  = NULL;
    lm_bd_chain_t* rx_chain_sge = NULL;
    lm_tx_chain_t* tx_chain     = NULL;
    s_list_t       packet_list  = {0};

    DbgMessage2(pdev, INFORM, "### lm_abort   abort_op=%d idx=%d\n", abort_op, idx);
    switch(abort_op)
    {
        case ABORT_OP_RX_CHAIN:
        case ABORT_OP_INDICATE_RX_CHAIN:
        {
            rxq_chain    = &LM_RXQ(pdev, idx);
            rx_chain_bd  = &LM_RXQ_CHAIN_BD(pdev, idx);
            rx_chain_sge = LM_RXQ_SGE_PTR_IF_VALID(pdev, idx);
            // Verify BD's consistent
            DbgBreakIfFastPath( rx_chain_sge && !lm_bd_chains_are_consistent( rx_chain_sge, rx_chain_bd ) );
            /* indicate packets from the active descriptor list */
            for(; ;)
            {
                pkt = (lm_packet_t *) s_list_pop_head(&rxq_chain->active_descq);
                if(pkt == NULL)
                {
                    break;
                }
                pkt->status = LM_STATUS_ABORTED;
                lm_bd_chain_bds_consumed(rx_chain_bd, 1);
                if( rx_chain_sge )
                {
                    lm_bd_chain_bds_consumed(rx_chain_sge, 1);
                }
                LM_COMMON_DRV_STATS_ATOMIC_INC_ETH(pdev, rx_aborted);
                // if in shutdown flow or not if in d3 flow ?
                if (abort_op == ABORT_OP_INDICATE_RX_CHAIN)
                {
#if (!defined(LINUX) && !defined(__SunOS) && !defined(UEFI) && !defined(DOS))
                    s_list_push_tail(&packet_list, (s_list_entry_t *)pkt);
#endif
                }
                else
                {
                    s_list_push_tail(&rxq_chain->free_descq, &pkt->link);
                }
            }
            if ( ABORT_OP_INDICATE_RX_CHAIN == abort_op )
            {
                /* indicate packets from the free descriptor list */
                for(; ;)
                {
                    pkt = (lm_packet_t *) s_list_pop_head(&rxq_chain->free_descq);
                    if (pkt == NULL)
                    {
                        break;
                    }
                    pkt->status = LM_STATUS_ABORTED;
                    LM_COMMON_DRV_STATS_ATOMIC_INC_ETH(pdev, rx_aborted);
#if (!defined(LINUX) && !defined(__SunOS) && !defined(UEFI) && !defined(DOS))
                    s_list_push_tail(&packet_list, (s_list_entry_t *)pkt);
#endif
                }
                if (!s_list_is_empty(&packet_list))
                {
#if (!defined(LINUX) && !defined(__SunOS) && !defined(UEFI) && !defined(DOS))
                    mm_indicate_rx(pdev, idx, &packet_list);
#endif
                }
            }
        break;
        } // ABORT_OP_INDICATE_RX_CHAIN
        case ABORT_OP_INDICATE_TX_CHAIN:
        {
            tx_chain = &LM_TXQ(pdev, idx);
            for(; ;)
            {
                pkt = (lm_packet_t *) s_list_pop_head(&tx_chain->active_descq);
                if(pkt == NULL)
                {
                    break;
                }
                pkt->status = LM_STATUS_ABORTED;
                LM_COMMON_DRV_STATS_ATOMIC_INC_ETH(pdev, tx_aborted);
                lm_bd_chain_bds_consumed(&tx_chain->bd_chain, pkt->u1.tx.bd_used);
                if (pkt->u1.tx.coalesce_buf) {
                    /* return coalesce buffer to the chain's pool */
                    lm_put_coalesce_buffer(pdev, tx_chain, pkt->u1.tx.coalesce_buf);
                    pkt->u1.tx.coalesce_buf = NULL;
                }
                s_list_push_tail(&packet_list, (s_list_entry_t *)pkt);
            }
            if (!s_list_is_empty(&packet_list))
            {
                mm_indicate_tx(pdev, idx, &packet_list);
            }

            // changed from pdev->params.l2_tx_bd_page_cnt[idx] to pdev->params.l2_tx_bd_page_cnt[0]
            DbgBreakIf(!lm_bd_chain_is_full(&tx_chain->bd_chain));
            DbgBreakIf(s_list_entry_cnt(&tx_chain->coalesce_buf_list) != tx_chain->coalesce_buf_cnt);
            break;
        } // ABORT_OP_INDICATE_TX_CHAIN
        default:
        {
            DbgBreakMsg("unknown abort operation.\n");
            break;
        }
    } //switch
} /* lm_abort */

// reads max_payload_size & max_read_req_size from pci config space
lm_status_t lm_get_pcicfg_mps_mrrs(lm_device_t * pdev)
{
    lm_status_t lm_status = LM_STATUS_SUCCESS;
    u32_t       val       = 0;
    /* get max payload size and max read size we need it for pxp configuration
    in the real chip it should be done by the MCP.*/
    lm_status = mm_read_pci(pdev, PCICFG_DEVICE_CONTROL, &val);
    if (lm_status != LM_STATUS_SUCCESS)
    {
        return lm_status;
    }
    // bit 5-7
    pdev->hw_info.max_payload_size = (val & 0xe0)>>5;
    // bit 12-14
    pdev->hw_info.max_read_req_size = (val & 0x7000)>>12;
    DbgMessage3(pdev, INFORMi, "reg 0xd8 0x%x \n max_payload %d max_read_req %d \n",
                val,pdev->hw_info.max_payload_size,pdev->hw_info.max_read_req_size);
    return lm_status ;
}
static lm_status_t lm_get_pcicfg_info(lm_device_t *pdev)
{
    lm_status_t lm_status;
    u32_t val;
    /* Get PCI device and vendor id. (need to be read from parent */
    if (IS_PFDEV(pdev) || IS_CHANNEL_VFDEV(pdev))
    {
        lm_status = mm_read_pci(pdev, PCICFG_VENDOR_ID_OFFSET, &val);
        if (lm_status != LM_STATUS_SUCCESS)
        {
            return lm_status;
        }
        pdev->hw_info.vid = (u16_t) val;
        DbgMessage1(pdev, INFORMi, "vid 0x%x\n", pdev->hw_info.vid);
        pdev->hw_info.did = (u16_t) (val >> 16);
        DbgMessage1(pdev, INFORMi, "did 0x%x\n", pdev->hw_info.did);
    }
    else if (IS_BASIC_VFDEV(pdev))
    {
        pdev->hw_info.vid = PFDEV(pdev)->hw_info.vid;
        pdev->hw_info.did = PFDEV(pdev)->hw_info.sriov_info.vf_device_id;
    }
    else
    {
        DbgMessage(pdev,WARN,"vid&did for VBD VF will be known later\n"); /*Must be known earlier*/
    }
    /* Get subsystem and subvendor id. */
    lm_status = mm_read_pci(pdev, PCICFG_SUBSYSTEM_VENDOR_ID_OFFSET, &val);
    if (lm_status != LM_STATUS_SUCCESS)
    {
        return lm_status;
    }
    pdev->hw_info.svid = (u16_t) val;
    DbgMessage1(pdev, INFORMi, "svid 0x%x\n", pdev->hw_info.svid);
    pdev->hw_info.ssid = (u16_t) (val >> 16);
    DbgMessage1(pdev, INFORMi, "ssid 0x%x\n", pdev->hw_info.ssid);
    /* Get IRQ, and interrupt pin. */
    lm_status = mm_read_pci(pdev, PCICFG_INT_LINE, &val);
    if (lm_status != LM_STATUS_SUCCESS)
    {
        return lm_status;
    }
    pdev->hw_info.irq = (u8_t) val;
    DbgMessage1(pdev, INFORMi, "IRQ 0x%x\n", pdev->hw_info.irq);
    pdev->hw_info.int_pin = (u8_t) (val >> 8);
    DbgMessage1(pdev, INFORMi, "Int pin 0x%x\n", pdev->hw_info.int_pin);
    /* Get cache line size. */
    lm_status = mm_read_pci(pdev, PCICFG_CACHE_LINE_SIZE, &val);
    if (lm_status != LM_STATUS_SUCCESS)
    {
        return lm_status;
    }
    pdev->hw_info.cache_line_size = (u8_t) val;
    DbgMessage1(pdev, INFORMi, "Cache line size 0x%x\n", (u8_t) val);
    pdev->hw_info.latency_timer = (u8_t) (val >> 8);
    DbgMessage1(pdev, INFORMi, "Latency timer 0x%x\n", (u8_t) (val >> 8));
    /* Get PCI revision id. */
    lm_status = mm_read_pci(pdev, PCICFG_REVESION_ID_OFFSET, &val);
    if (lm_status != LM_STATUS_SUCCESS)
    {
        return lm_status;
    }
    pdev->hw_info.rev_id = (u8_t) val;
    DbgMessage1(pdev, INFORMi, "Revision id 0x%x\n", pdev->hw_info.rev_id);

    /* Get PCI-E speed*/
    /* only for PF */
    if (IS_PFDEV(pdev)) {
        lm_status = mm_read_pci(pdev, PCICFG_LINK_CONTROL, &val);
        if (lm_status != LM_STATUS_SUCCESS)
        {
            return lm_status;
        }
        /* bit 20-25 */
        pdev->hw_info.pcie_lane_width = (val & 0x3f00000) >> 20;
        DbgMessage1(pdev, INFORMi, "pcie_lane_width 0x%x\n", pdev->hw_info.pcie_lane_width);
        /* bit 16 - 19 */
        pdev->hw_info.pcie_lane_speed = (val & 0xf0000) >> 16;
        DbgMessage1(pdev, INFORMi, "pcie_lane_speed 0x%x\n", pdev->hw_info.pcie_lane_speed);

        lm_status = lm_get_pcicfg_mps_mrrs(pdev);
    }
    /* For VF, we also get the vf-id here... since we need it from configuration space */
#ifdef VF_INVOLVED
    if (IS_VFDEV(pdev)) {
        lm_vf_get_vf_id(pdev);
    }
#endif
    return lm_status;
}
/**
 * This function reads bar offset from PCI configuration
 * header.
 *
 * @param _pdev
 * @param bar_num Bar index: BAR_0 or BAR_1 or BAR_2
 * @param bar_addr Output value (bar offset).
 *
 * @return LM_STATUS_SUCCESS if bar offset has been read
 *         successfully.
 */
static __inline lm_status_t lm_get_bar_offset_direct(
    IN   struct _lm_device_t * pdev,
    IN   u8_t                  bar_num,   /* Bar index: BAR_0 or BAR_1 or BAR_2 */
    OUT lm_address_t         * bar_addr
    )
{
    u32_t pci_reg, val;
    lm_status_t lm_status;
    /* Get BARs addresses. */
    switch (bar_num) {
    case BAR_0:
        pci_reg = PCICFG_BAR_1_LOW;
        break;
    case BAR_1:
        pci_reg = PCICFG_BAR_1_LOW + 8;
        break;
    case BAR_2:
        pci_reg = PCICFG_BAR_1_LOW + 16;
        break;
    default:
        DbgMessage1(pdev, FATAL, "Unsupported bar index: %d\n", bar_num);
        DbgBreakIfAll(1);
        return LM_STATUS_INVALID_PARAMETER;
    }
    lm_status = mm_read_pci(pdev, pci_reg, &val);
    if(lm_status != LM_STATUS_SUCCESS) {
        return lm_status;
    }
    bar_addr->as_u32.low = val & 0xfffffff0;;
    DbgMessage2(pdev, INFORMi, "BAR %d low 0x%x\n", bar_num,
                bar_addr->as_u32.low);
    pci_reg += 4; /* sizeof configuration space bar address register */
    lm_status = mm_read_pci(pdev, pci_reg, &val);
    if(lm_status != LM_STATUS_SUCCESS) {
        return lm_status;
    }
    bar_addr->as_u32.high = val;
    DbgMessage2(pdev, INFORMi, "BAR %d high 0x%x\n", bar_num,
                bar_addr->as_u32.high);
    return LM_STATUS_SUCCESS;
}
static __inline lm_status_t lm_get_bar_size_direct (
    IN  lm_device_t *pdev,
    IN  u8_t bar_num,
    OUT  u32_t * val_p)
{
    u32_t bar_address = 0;
    u32_t bar_size;
    switch (bar_num) {
    case BAR_0:
        bar_address = GRC_CONFIG_2_SIZE_REG;
        break;
    case BAR_1:
        bar_address = GRC_BAR2_CONFIG;
        break;
    case BAR_2:
        bar_address = GRC_BAR3_CONFIG;
        break;
    default:
        DbgMessage(pdev, FATAL, "Invalid Bar Num\n");
        return LM_STATUS_INVALID_PARAMETER;
    }
    lm_reg_rd_ind(pdev,PCICFG_OFFSET + bar_address,&bar_size);
    /*extract only bar size*/
    ASSERT_STATIC(PCI_CONFIG_2_BAR1_SIZE == PCI_CONFIG_2_BAR2_SIZE);
    ASSERT_STATIC(PCI_CONFIG_2_BAR2_SIZE == PCI_CONFIG_2_BAR3_SIZE);
    bar_size = (bar_size & PCI_CONFIG_2_BAR1_SIZE);
    if (bar_size == 0) {
        /*bar size disabled*/
        return LM_STATUS_FAILURE;
    } else {
        /*bit 1 stand for 64K each bit multiply it by two */
        *val_p = (0x40 << ((bar_size - 1)))*0x400;
    }
    return LM_STATUS_SUCCESS;
}
/* init pdev->hw_info with data from pcicfg */
static lm_status_t lm_get_bars_info(lm_device_t *pdev)
{
    lm_status_t lm_status;
    u32_t bar_map_size = 0;
    u8_t i;

    /* Get BARs addresses. */
    for (i = 0; i < MAX_NUM_BAR; i++)
    {
        lm_status = mm_get_bar_offset(pdev, i, &pdev->hw_info.mem_base[i]);
        DbgMessage1(pdev, INFORMi, "Bar_Offset=0x%x\n", pdev->hw_info.mem_base[i]);
        if(lm_status != LM_STATUS_SUCCESS)
        {
            return lm_status;
        }
        if(pdev->hw_info.mem_base[i].as_u64 == 0)
        {
            DbgMessage1(pdev, WARNi, "BAR %d IS NOT PRESENT\n", i);
            if(i==0)
            {
                DbgBreakMsg("BAR 0 must be present\n");
            }
        }
    }
    /* TBA: review two intializations done in Teton here (are they needed? are they part of "get_bars_info"):
    - Enable PCI bus master....
    - Configure byte swap and enable write to the reg_window registers
    */
    for (i = 0; i < MAX_NUM_BAR; i++)
    {
        if(pdev->hw_info.mem_base[i].as_u64 == 0) {
            continue;
        }
        /* get bar i size*/
        lm_status = mm_get_bar_size(pdev, i, &(pdev->hw_info.bar_size[i]));
        if ( lm_status != LM_STATUS_SUCCESS ) {
            return lm_status;
        }
        DbgMessage2(pdev, INFORMi, "bar %d size 0x%x\n", i, pdev->hw_info.bar_size[i]);
        /* Change in BAR1
         * The function will map in case of BAR1 only the ETH cid doorbell space to a virtual address.
         * (Map from BAR1 base address, to BAR1 base address plus MAX_ETH_CONS* LM_PAGE_SIZE).
        */
        if (BAR_1 == i )
        {
            if (IS_PFDEV(pdev)) { //TODO Revise it
#ifdef VF_INVOLVED
                bar_map_size = pdev->hw_info.bar_size[i];
#else
                bar_map_size = LM_DQ_CID_SIZE * MAX_ETH_CONS;
#endif
            } else {
                bar_map_size = LM_DQ_CID_SIZE;
            }
#ifndef VF_INVOLVED
            DbgBreakIf(bar_map_size >= pdev->hw_info.bar_size[i]);
#endif
        }
        else
        {
            bar_map_size = pdev->hw_info.bar_size[i];
        }
        /* Map bar i to system address space. If not mapped already. */
        if(lm_is_function_after_flr(pdev) ||
#ifdef VF_INVOLVED
           lm_is_function_after_flr(PFDEV(pdev)) ||
#endif
           (pdev->vars.mapped_bar_addr[i] == NULL))
        {
                pdev->vars.mapped_bar_addr[i] = NULL;
                pdev->vars.mapped_bar_addr[i] = mm_map_io_base(
                        pdev,
                        pdev->hw_info.mem_base[i],
                        bar_map_size,
                        i);
                if(pdev->vars.mapped_bar_addr[i] == NULL)
                {
                        DbgMessage1(pdev, FATAL, "bar %d map io failed\n", i);
                        return LM_STATUS_FAILURE;
                } else {
                    DbgMessage3(pdev, INFORMi, "mem_base[%d]=%p size=0x%x\n", i, pdev->vars.mapped_bar_addr[i], pdev->hw_info.bar_size[i]);
                }
        }
    }
    /* Now that the bars are mapped, we need to enable target read + write and master-enable,
     * we can't do this before bars are mapped, but we need to do this before we start any chip
     * initializations... */
    #if 0 // Michals 10/20/10 (problem: we don't know which chip we are, but we can't know until we write to the register....
          // need to figure out what to do.
    if (!CHIP_IS_E1x(pdev) && IS_PFDEV(pdev)) {
        u32_t m_e,tr_e,tw_e;
        u32_t i_cycles;
        REG_WR(pdev, PGLUE_B_REG_INTERNAL_PFID_ENABLE_TARGET_READ, 1);
        for (i_cycles = 0; i_cycles < 1000; i_cycles++) {
            mm_wait(pdev,999);
        }
        tr_e = REG_RD(pdev, PGLUE_B_REG_INTERNAL_PFID_ENABLE_TARGET_READ);
        tw_e = REG_RD(pdev, PGLUE_B_REG_INTERNAL_PFID_ENABLE_TARGET_WRITE);
        m_e = REG_RD(pdev, PGLUE_B_REG_INTERNAL_PFID_ENABLE_MASTER);
        DbgMessage3(pdev, INFORM, "M:0x%x, TR:0x%x, TW:0x%x\n",m_e,tr_e,tw_e);
        if (tw_e != 0x1) {
            DbgBreakMsg("BAR 0 must be present\n");
            return LM_STATUS_FAILURE;
        }
    }
    #endif
    return LM_STATUS_SUCCESS;
}
static lm_status_t lm_get_chip_id_and_mode(lm_device_t *pdev)
{
    u32_t val;

    /* Get the chip revision id and number. */
    /* chip num:16-31, rev:12-15, metal:4-11, bond_id:0-3 */
    val=REG_RD(PFDEV(pdev),MISC_REG_CHIP_NUM);
    CHIP_NUM_SET(pdev->hw_info.chip_id,val);
    val=REG_RD(PFDEV(pdev),MISC_REG_CHIP_REV);
    // the chip rev is realy ASIC when it < 5
    // when it > 5 odd mean FPGA even EMUL.
    if ((val & 0xf)<=5) {
        pdev->hw_info.chip_id |= (val & 0xf) << 12;
        pdev->vars.clk_factor = 1;
    }
    else if ((val & 0xf) & 0x1) {
        pdev->hw_info.chip_id |= CHIP_REV_FPGA;
        pdev->vars.clk_factor = LM_FPGA_FACTOR;
        pdev->hw_info.max_payload_size = 0;
        DbgMessage1(pdev, INFORMi, "FPGA: forcing MPS from %d to 0.\n", pdev->hw_info.max_payload_size);
    }
    else {
        pdev->hw_info.chip_id |= CHIP_REV_EMUL;
        pdev->vars.clk_factor = LM_EMUL_FACTOR;
    }
    val=REG_RD(PFDEV(pdev),MISC_REG_CHIP_METAL);
    pdev->hw_info.chip_id |= (val & 0xff) << 4;
    val=REG_RD(PFDEV(pdev),MISC_REG_BOND_ID);
    pdev->hw_info.chip_id |= (val & 0xf);
    DbgMessage1(pdev, INFORMi , "chip id 0x%x\n", pdev->hw_info.chip_id);
    /* Read silent revision */
    val=REG_RD(PFDEV(pdev),MISC_REG_CHIP_TEST_REG);
    pdev->hw_info.silent_chip_rev = (val & 0xff);
    DbgMessage1(pdev, INFORMi , "silent chip rev 0x%x\n", pdev->hw_info.silent_chip_rev);
    if (!CHIP_IS_E1x(pdev))
    {
        /* Determine whether we are 2 port or 4 port mode */
        /* read port4mode_en_ovwr[0];
         * b)     if 0 – read port4mode_en (0 – 2-port; 1 – 4-port);
         * c)     if 1 – read port4mode_en_ovwr[1] (0 – 2-port; 1 – 4-port);
         */
        val = REG_RD(PFDEV(pdev), MISC_REG_PORT4MODE_EN_OVWR);
        DbgMessage1(pdev, WARN, "MISC_REG_PORT4MODE_EN_OVWR = %d\n", val);
        if ((val & 1) == 0)
        {
            val = REG_RD(PFDEV(pdev), MISC_REG_PORT4MODE_EN);
        }
        else
        {
            val = (val >> 1) & 1;
        }
        pdev->hw_info.chip_port_mode = val? LM_CHIP_PORT_MODE_4 : LM_CHIP_PORT_MODE_2;
        DbgMessage1(pdev, WARN, "chip_port_mode %s\n", (pdev->hw_info.chip_port_mode == LM_CHIP_PORT_MODE_4 )? "4_PORT" : "2_PORT");
    }
    else
    {
        pdev->hw_info.chip_port_mode = LM_CHIP_PORT_MODE_NONE; /* N/A */
        DbgMessage(pdev, WARN, "chip_port_mode NONE\n");
    }
    return LM_STATUS_SUCCESS;
}
static lm_status_t lm_get_igu_cam_info(lm_device_t *pdev)
{
    lm_intr_blk_info_t *blk_info = &pdev->hw_info.intr_blk_info;
    u8_t igu_test_vectors = FALSE;
    #define IGU_CAM_VFID_MATCH(pdev, igu_fid) (!(igu_fid & IGU_FID_ENCODE_IS_PF) && ((igu_fid & IGU_FID_VF_NUM_MASK) == ABS_VFID(pdev)))
    #define IGU_CAM_PFID_MATCH(pdev, igu_fid) ((igu_fid & IGU_FID_ENCODE_IS_PF) && ((igu_fid & IGU_FID_PF_NUM_MASK) == FUNC_ID(pdev)))
    if (INTR_BLK_MODE(pdev) == INTR_BLK_MODE_BC) {
        blk_info->igu_info.igu_sb_cnt       = MAX_RSS_CHAINS;
        blk_info->igu_info.igu_u_sb_offset  = 0;
        if (CHIP_PORT_MODE(pdev) == LM_CHIP_PORT_MODE_2)
        {
            blk_info->igu_info.igu_base_sb      = VNIC_ID(pdev) * MAX_RSS_CHAINS;
            blk_info->igu_info.igu_dsb_id       = MAX_VNIC_NUM * MAX_RSS_CHAINS + VNIC_ID(pdev);
        }
        else
        {
            blk_info->igu_info.igu_base_sb      = FUNC_ID(pdev) * MAX_RSS_CHAINS;
            blk_info->igu_info.igu_dsb_id       = MAX_VNIC_NUM * MAX_RSS_CHAINS + FUNC_ID(pdev);
        }
    }
    else
    {
        u8_t igu_sb_id;
        u8_t fid;
        u8_t vec;
        u32_t val;
        blk_info->igu_info.igu_sb_cnt = 0;
        blk_info->igu_info.igu_test_sb_cnt = 0;
        blk_info->igu_info.igu_base_sb = 0xff;
        for (igu_sb_id = 0; igu_sb_id < IGU_REG_MAPPING_MEMORY_SIZE; igu_sb_id++ ) {
            // mapping CAM; relevant for E2 operating mode only.
            // [0] - valid.
            // [6:1] - vector number;
            // [13:7] - FID (if VF - [13] = 0; [12:7] = VF number; if PF - [13] = 1; [12:9] = 0; [8:7] = PF number);
            val = REG_RD(PFDEV(pdev), IGU_REG_MAPPING_MEMORY + 4*igu_sb_id);
            DbgMessage3(pdev, INFORMi, "addr:0x%x IGU_CAM[%d]=%x\n",IGU_REG_MAPPING_MEMORY + 4*igu_sb_id, igu_sb_id, val);
            if (!(val & IGU_REG_MAPPING_MEMORY_VALID)) {
                continue;
            }
            fid = (val & IGU_REG_MAPPING_MEMORY_FID_MASK) >> IGU_REG_MAPPING_MEMORY_FID_SHIFT;
            DbgMessage2(pdev, VERBOSEi, "FID[%d]=%d\n", igu_sb_id, fid);
            if ((IS_PFDEV(pdev) && IGU_CAM_PFID_MATCH(pdev, fid)) ||
                (IS_VFDEV(pdev) && IGU_CAM_VFID_MATCH(pdev, fid))) {
                vec = (val & IGU_REG_MAPPING_MEMORY_VECTOR_MASK) >> IGU_REG_MAPPING_MEMORY_VECTOR_SHIFT;
                DbgMessage2(pdev, INFORMi, "VEC[%d]=%d\n", igu_sb_id, vec);
                if (igu_test_vectors) {
                    blk_info->igu_info.igu_test_sb_cnt++;
                } else {
                    if (vec == 0 && IS_PFDEV(pdev)) {
                        /* default status block for default segment + attn segment */
                        blk_info->igu_info.igu_dsb_id = igu_sb_id;
                    } else  {
                        if (blk_info->igu_info.igu_base_sb == 0xff) {
                            blk_info->igu_info.igu_base_sb = igu_sb_id;
                        }
                        /* we don't count the default */
                        blk_info->igu_info.igu_sb_cnt++;
                    }
                }
            } else {
                /* No Match - belongs to someone else, check if breaks consecutiveness, if so, break at this point
                 * driver doesn't support non-consecutive vectors (EXCEPT Def sb...) */
                if (blk_info->igu_info.igu_base_sb != 0xff) {
                    /* We've already found our base... but now we don't match... these are now igu-test-vectors */
                    igu_test_vectors = TRUE;
                }
            }
        }
        // TODO check cam is valid...
        blk_info->igu_info.igu_sb_cnt = min(blk_info->igu_info.igu_sb_cnt, (u8_t)16);
        /* E2 TODO: if we don't want to separate u/c/ producers in IGU, this line needs to
         * be removed, and igu_u_offset needs to be set to 'zero'
        blk_info->igu_info.igu_u_sb_offset = blk_info->igu_info.igu_sb_cnt / 2;*/
        DbgMessage5(pdev, WARN, "igu_sb_cnt=%d igu_dsb_id=%d igu_base_sb = %d igu_us_sb_offset = %d igu_test_cnt=%d\n",
                    blk_info->igu_info.igu_sb_cnt, blk_info->igu_info.igu_dsb_id, blk_info->igu_info.igu_base_sb, blk_info->igu_info.igu_u_sb_offset,
                    blk_info->igu_info.igu_test_sb_cnt);
        if (blk_info->igu_info.igu_sb_cnt < 1) {
            DbgMessage1(pdev, FATAL, "Igu sb cnt is not valid value=%d\n", blk_info->igu_info.igu_sb_cnt);
            return LM_STATUS_FAILURE;
        }
        if (blk_info->igu_info.igu_base_sb == 0xff) {
            DbgMessage1(pdev, FATAL, "Igu base sb is not valid value=%d\n", blk_info->igu_info.igu_base_sb);
            return LM_STATUS_FAILURE;
        }
    }
    return LM_STATUS_SUCCESS;
}
/*
 * Assumptions:
 *  - the following are initialized before call to this function:
 *    chip-id, func-rel,
 */
static lm_status_t lm_get_intr_blk_info(lm_device_t *pdev)
{
    lm_intr_blk_info_t *blk_info = &pdev->hw_info.intr_blk_info;
    lm_status_t lm_status;
    u32_t bar_base;
    u8_t igu_func_id = 0;
    if (CHIP_IS_E1x(pdev)) {
        blk_info->blk_type = INTR_BLK_HC;
        blk_info->access_type = INTR_BLK_ACCESS_GRC;
        blk_info->blk_mode = INTR_BLK_MODE_NORM;
        blk_info->simd_addr_womask = HC_REG_COMMAND_REG + PORT_ID(pdev)*32 + COMMAND_REG_SIMD_NOMASK;
        /* The next part is tricky... and has to do with an emulation work-around for handling interrupts, in which
         * we want to read without mask - always... so we take care of it here, instead of changing different ums to
         * call approriate function */
        if (CHIP_REV(pdev) == CHIP_REV_EMUL) {
            blk_info->simd_addr_wmask = HC_REG_COMMAND_REG + PORT_ID(pdev)*32 + COMMAND_REG_SIMD_NOMASK;
        } else {
            blk_info->simd_addr_wmask = HC_REG_COMMAND_REG + PORT_ID(pdev)*32 + COMMAND_REG_SIMD_MASK;
        }
    } else {
        /* If we have more than 32 status blocks we'll need to read from IGU_REG_SISR_MDPC_WMASK_UPPER */
        ASSERT_STATIC(MAX_RSS_CHAINS <= 32);
        pdev->hw_info.intr_blk_info.blk_type = INTR_BLK_IGU;
        if (REG_RD(PFDEV(pdev), IGU_REG_BLOCK_CONFIGURATION) & IGU_BLOCK_CONFIGURATION_REG_BACKWARD_COMP_EN) {
            DbgMessage(pdev, FATAL, "IGU Backward Compatible Mode\n");
            if (IS_VFDEV(pdev)) {
                DbgBreakMsg("VF Can't work in IGU Backward compatible mode!\n");
                return LM_STATUS_FAILURE;
            }
            blk_info->blk_mode = INTR_BLK_MODE_BC;
        } else {
            DbgMessage(pdev, WARN, "IGU Normal Mode\n");
            blk_info->blk_mode = INTR_BLK_MODE_NORM;
        }
        /* read CAM to get igu info (must be called after we know if we're in backward compatible mode or not )*/
        lm_status = lm_get_igu_cam_info(pdev);
        if (lm_status != LM_STATUS_SUCCESS) {
            return lm_status;
        }
        igu_func_id = (1 << IGU_FID_ENCODE_IS_PF_SHIFT) | FUNC_ID(pdev);
        blk_info->igu_info.igu_func_id = igu_func_id;
        if (pdev->params.igu_access_mode == INTR_BLK_ACCESS_GRC) {
            DbgMessage(pdev, FATAL, "IGU -  GRC\n");
            if (IS_VFDEV(pdev)) {
                DbgBreakMsg("VF Can't work in GRC Access mode!\n");
                return LM_STATUS_FAILURE;
            }
            blk_info->access_type = INTR_BLK_ACCESS_GRC;
            /* [18:12] - FID (if VF - [18] = 0; [17:12] = VF number; if PF - [18] = 1; [17:14] = 0; [13:12] = PF number) */
            blk_info->cmd_ctrl_rd_womask =
            ((IGU_REG_SISR_MDPC_WOMASK_UPPER << IGU_CTRL_REG_ADDRESS_SHIFT) |
             (igu_func_id << IGU_CTRL_REG_FID_SHIFT) |
             (IGU_CTRL_CMD_TYPE_RD << IGU_CTRL_REG_TYPE_SHIFT));
            blk_info->simd_addr_womask = IGU_REG_COMMAND_REG_32LSB_DATA; /* this is where data will be after writing ctrol reg... */
            /* The next part is tricky... and has to do with an emulation work-around for handling interrupts, in which
             * we want to read without mask - always... so we take care of it here, instead of changing different ums to
             * call approriate function */
            if (CHIP_REV(pdev) == CHIP_REV_EMUL) {
                blk_info->cmd_ctrl_rd_wmask =
                ((IGU_REG_SISR_MDPC_WOMASK_UPPER << IGU_CTRL_REG_ADDRESS_SHIFT) |
                 (igu_func_id << IGU_CTRL_REG_FID_SHIFT) |
                 (IGU_CTRL_CMD_TYPE_RD << IGU_CTRL_REG_TYPE_SHIFT));
            } else {
                blk_info->cmd_ctrl_rd_wmask =
                ((IGU_REG_SISR_MDPC_WMASK_LSB_UPPER << IGU_CTRL_REG_ADDRESS_SHIFT) |
                 (igu_func_id << IGU_CTRL_REG_FID_SHIFT) |
                 (IGU_CTRL_CMD_TYPE_RD << IGU_CTRL_REG_TYPE_SHIFT));
            }
            blk_info->simd_addr_wmask = IGU_REG_COMMAND_REG_32LSB_DATA; /* this is where data will be after writing ctrol reg... */
        } else {
            DbgMessage(pdev, WARN, "IGU  - IGUMEM\n");
            blk_info->access_type = INTR_BLK_ACCESS_IGUMEM;
            bar_base = IS_PFDEV(pdev)? BAR_IGU_INTMEM : VF_BAR0_IGU_OFFSET;
            blk_info->simd_addr_womask = bar_base + IGU_REG_SISR_MDPC_WOMASK_UPPER*8;
            /* The next part is tricky... and has to do with an emulation work-around for handling interrupts, in which
             * we want to read without mask - always... so we take care of it here, instead of changing different ums to
             * call approriate function */
            if (CHIP_REV(pdev) == CHIP_REV_EMUL) {
                blk_info->simd_addr_wmask = bar_base + IGU_REG_SISR_MDPC_WOMASK_UPPER*8;
            } else {
                blk_info->simd_addr_wmask = bar_base + IGU_REG_SISR_MDPC_WMASK_LSB_UPPER*8;
            }
        }
    }
    return LM_STATUS_SUCCESS;
}
static lm_status_t lm_get_nvm_info(lm_device_t *pdev)
{
    u32_t val = 0;
    val=REG_RD(pdev,MCP_REG_MCPR_NVM_CFG4);
    pdev->hw_info.flash_spec.total_size = NVRAM_1MB_SIZE << (val & MCPR_NVM_CFG4_FLASH_SIZE);
    pdev->hw_info.flash_spec.page_size = NVRAM_PAGE_SIZE;
    return LM_STATUS_SUCCESS;
}
#if defined(DOS) || defined(__LINUX)
/* for ediag + lediat we don't really care about licensing!... */
#define MAX_CONNECTIONS 2048  /* Max 32K Connections per port / vnic-per-port (rounded  to power2)*/
#define MAX_CONNECTIONS_ISCSI 128
#define MAX_CONNECTIONS_RDMA   10
#define MAX_CONNECTIONS_TOE 1880
#define MAX_CONNECTIONS_FCOE 0
#else
#define MAX_CONNECTIONS (min(16384,(32768 / (log2_align(pdev->hw_info.mf_info.vnics_per_port)))))  /* Max 32K Connections per port / vnic-per-port (rounded  to power2)
                                                                                                    but no more 16K to limit ilt client page size by 64KB*/
#define MAX_CONNECTIONS_ISCSI 128
#define MAX_CONNECTIONS_RDMA   10
#define MAX_CONNECTIONS_FCOE 1024
#define MAX_CONNECTIONS_TOE (MAX_CONNECTIONS - MAX_CONNECTIONS_ISCSI - MAX_CONNECTIONS_RDMA - MAX_CONNECTIONS_FCOE - MAX_ETH_CONS)
#endif
#define MAX_CONNECTIONS_TOE_NO_LICENSE   0
#define MAX_CONNECTIONS_ISCSI_NO_LICENSE 0
#define MAX_CONNECTIONS_RDMA_NO_LICENSE  0
#define MAX_CONNECTIONS_FCOE_NO_LICENSE  128
static u32_t lm_parse_license_info(u32 val, u8_t is_high)
{
    if (is_high)
    {
        val &=0xFFFF0000;
        if(val)
        {
            val ^= FW_ENCODE_32BIT_PATTERN;
        }
        val >>= 16;
    }
    else
    {
        val &= 0xffff;
        if(val)
        {
            val ^= FW_ENCODE_16BIT_PATTERN;
        }
    }
    return val;
}

static u32_t lm_parse_license_info_bounded(u32 val, u32_t max_cons, u8_t is_high)
{
    u32_t license_from_shmem =0;
    license_from_shmem = lm_parse_license_info(val, is_high);

    val = min(license_from_shmem, max_cons);
    return val;
}
/* No special MCP handling for a specific E1H configuration */
/* WARNING: Do Not Change these defines!!! They are used in an external tcl script that assumes their values!!! */
#define NO_MCP_WA_CFG_SET_ADDR            (0xA0000)
#define NO_MCP_WA_CFG_SET_MAGIC           (0x88AA55FF)
#define NO_MCP_WA_MULTI_VNIC_MODE         (0xA0004)
#define NO_MCP_WA_VNICS_PER_PORT(port)    (0xA0008 + 4*(port))
#define NO_MCP_WA_OVLAN(func)             (0xA0010 + 4*(func)) // --> 0xA0030
#define NO_MCP_WA_FORCE_5710              (0xA0030)
#define NO_MCP_WA_VALID_LIC_ADDR          (0xA0040)
#define NO_MCP_WA_VALID_LIC_MAGIC         (0xCCAAFFEE)
#define NO_MCP_WA_TOE_LIC                 (0xA0048)
#define NO_MCP_WA_ISCSI_LIC               (0xA0050)
#define NO_MCP_WA_RDMA_LIC                (0xA0058)
#define NO_MCP_WA_CLC_SHMEM               (0xAF900)

static lm_status_t lm_get_shmem_license_info(lm_device_t *pdev)
{
    u32_t max_toe_cons[PORT_MAX]           = {MAX_CONNECTIONS_TOE_NO_LICENSE,MAX_CONNECTIONS_TOE_NO_LICENSE};
    u32_t max_rdma_cons[PORT_MAX]          = {MAX_CONNECTIONS_RDMA_NO_LICENSE,MAX_CONNECTIONS_RDMA_NO_LICENSE};
    u32_t max_iscsi_cons[PORT_MAX]         = {MAX_CONNECTIONS_ISCSI_NO_LICENSE,MAX_CONNECTIONS_ISCSI_NO_LICENSE};
    u32_t max_fcoe_cons[PORT_MAX]          = {MAX_CONNECTIONS_FCOE_NO_LICENSE,MAX_CONNECTIONS_FCOE_NO_LICENSE};
    u32_t max_bar_supported_cons[PORT_MAX] = {0};
    u32_t max_supported_cons[PORT_MAX]     = {0};
    u32_t val                              = 0;
    u8_t  port                             = 0;
    u32_t offset                           = 0;

    /* Even though only one port actually does the initialization, ALL functions need to know the maximum number of connections
     * because that's how they know what the page-size-is, and based on that do per-function initializations as well. */
    pdev->hw_info.max_common_conns = 0;

    /* get values for relevant ports. */
    for (port = 0; port < PORT_MAX; port++)
    {
        if (pdev->hw_info.mcp_detected == 1)
        {
            LM_SHMEM_READ(pdev, OFFSETOF(shmem_region_t, validity_map[port]),&val);

            // check that licensing is enabled
            if(GET_FLAGS(val, SHR_MEM_VALIDITY_LIC_MANUF_KEY_IN_EFFECT | SHR_MEM_VALIDITY_LIC_UPGRADE_KEY_IN_EFFECT))
            {
                // align to 32 bit
                offset = OFFSETOF(shmem_region_t, drv_lic_key[port].max_toe_conn) & 0xfffffffc;
                LM_SHMEM_READ(pdev, offset, &val);
                max_toe_cons[port] = lm_parse_license_info_bounded(val, MAX_CONNECTIONS_TOE,FALSE);
                DbgMessage2(pdev, INFORMi, "max_toe_conn from shmem %d for port %d\n",val, port);
                /* RDMA */
                offset = OFFSETOF(shmem_region_t, drv_lic_key[port].max_um_rdma_conn) & 0xfffffffc;
                LM_SHMEM_READ(pdev, offset, &val);
                max_rdma_cons[port] = lm_parse_license_info_bounded(val, MAX_CONNECTIONS_RDMA,FALSE);
                DbgMessage2(pdev, INFORMi, "max_rdma_conn from shmem %d for port %d\n",val, port);
                /* ISCSI */
                offset = OFFSETOF(shmem_region_t, drv_lic_key[port].max_iscsi_trgt_conn) & 0xfffffffc;
                LM_SHMEM_READ(pdev, offset, &val);
                max_iscsi_cons[port] = lm_parse_license_info_bounded(val, MAX_CONNECTIONS_ISCSI,TRUE);
                DbgMessage2(pdev, INFORMi, "max_iscsi_conn from shmem %d for port %d\n",val, port);
                /* FCOE */
                offset = OFFSETOF(shmem_region_t, drv_lic_key[port].max_fcoe_init_conn) & 0xfffffffc;
                LM_SHMEM_READ(pdev, offset, &val);
                if(0 == lm_parse_license_info(val,TRUE))
                {
                    max_fcoe_cons[port] = 0;
                }
                else
                {
                    max_fcoe_cons[port] = MAX_CONNECTIONS_FCOE;
                }
                DbgMessage2(pdev, INFORMi, "max_fcoe_conn from shmem %d for port %d\n",val, port);

            }
            /* get the bar size... unless it's current port and then we have it. otherwise, read from shmem W.C which
             * is what the other ports asked for, they could have gotten less, but we're looking into the worst case. */
            if (PORT_ID(pdev) == port)
            {
                max_bar_supported_cons[port] = pdev->hw_info.bar_size[BAR_1] / LM_DQ_CID_SIZE;
            }
            else
            {
                LM_SHMEM_READ(pdev, OFFSETOF(shmem_region_t, dev_info.port_feature_config[port].config), &val);
                val = (val & PORT_FEATURE_BAR2_SIZE_MASK) >> PORT_FEATURE_BAR2_SIZE_SHIFT;
                if (val != 0)
                 {
                    /* bit 1 stand for 64K each bit multiply it by two */
                    val = (0x40 << ((val - 1)))*0x400;
                }
                max_bar_supported_cons[port] = val / LM_DQ_CID_SIZE;
            }
        }
        else
        {
            // MCP_WA
            LM_SHMEM_READ(pdev, NO_MCP_WA_VALID_LIC_ADDR+4*port, &val);

            if (val == NO_MCP_WA_VALID_LIC_MAGIC)
            {
                LM_SHMEM_READ(pdev, NO_MCP_WA_TOE_LIC+4*port, &val);
                max_toe_cons[port] = val;
                LM_SHMEM_READ(pdev, NO_MCP_WA_ISCSI_LIC+4*port, &val);
                max_iscsi_cons[port] = val;
                LM_SHMEM_READ(pdev, NO_MCP_WA_RDMA_LIC+4*port, &val);
                max_rdma_cons[port] = val;
                /* FCOE */
                // Fcoe licencing isn't supported.
                /*
                LM_SHMEM_READ(pdev, NO_MCP_WA_FCOE_LIC+4*port, &val);
                max_fcoe_cons[port] = val;
                */
            }
            else
            {
                #ifdef VF_INVOLVED
                max_toe_cons[port] = 1780;
                #else
                max_toe_cons[port] = 1880;
                #endif
                max_iscsi_cons[port] = 128;
                max_rdma_cons[port] = 10;
                // Need to review this value seems like we take in this case the max value
                max_fcoe_cons[port] = 128;
            }
            /* For MCP - WA, we always assume the same bar size for all ports: makes life simpler... */
            max_bar_supported_cons[port] = pdev->hw_info.bar_size[BAR_1] / LM_DQ_CID_SIZE;
        }
        /* so after all this - what is the maximum number of connections supported for this port? */
        max_supported_cons[port] = log2_align(max_toe_cons[port] + max_rdma_cons[port] + max_iscsi_cons[port] + max_fcoe_cons[port] + MAX_ETH_CONS + MAX_VF_ETH_CONS);
        max_supported_cons[port] = min(max_supported_cons[port], max_bar_supported_cons[port]);

        /* And after all this... in lediag  / ediag... we assume a maximum of 1024 connections */
        #if defined(DOS) || defined(__LINUX)
        max_supported_cons[port] = min(max_supported_cons[port], (u32_t)1024);
        #endif

        if (max_supported_cons[port] > pdev->hw_info.max_common_conns)
        {
            pdev->hw_info.max_common_conns = max_supported_cons[port];
        }
    }
    /* Now, port specific... */
    port = PORT_ID(pdev);
    /* now, there could be a problem where the bar limited us, and the max-connections is smaller than the total above, in this case we need to decrease the
     * numbers relatively... can't touch MAX_ETH_CONS... */
    if (ERR_IF(max_supported_cons[port] <= (MAX_ETH_CONS + MAX_VF_ETH_CONS)))
    {
        return LM_STATUS_INVALID_PARAMETER;
    }
    if ((max_iscsi_cons[port]  + max_rdma_cons[port] +  max_toe_cons[port] + max_fcoe_cons[port] + MAX_ETH_CONS + MAX_VF_ETH_CONS) > max_supported_cons[port])
    {
        /* we first try giving iscsi + rdma what they asked for... */
        if ((max_iscsi_cons[port] + max_rdma_cons[port] + max_fcoe_cons[port] + MAX_ETH_CONS + MAX_VF_ETH_CONS) > max_supported_cons[port])
        {
            u32_t s = max_iscsi_cons[port] + max_rdma_cons[port] +  max_toe_cons[port] + max_fcoe_cons[port]; /* eth out of the game... */
            u32_t t = max_supported_cons[port] - (MAX_ETH_CONS + MAX_VF_ETH_CONS); /* what we want to reach... */
            /* relatively decrease all... (x+y+z=s, actual = t: xt/s+yt/s+zt/s = t) */
            max_iscsi_cons[port] *=t;
            max_iscsi_cons[port] /=s;
            max_rdma_cons[port]  *=t;
            max_rdma_cons[port]  /=s;
            max_toe_cons[port]   *=t;
            max_toe_cons[port]   /=s;
            max_fcoe_cons[port]  *=t;
            max_fcoe_cons[port]  /=s;
        }
        else
         {
            /* just give toe what's left... */
            max_toe_cons[port] = max_supported_cons[port] - (max_iscsi_cons[port] + max_rdma_cons[port]  + max_fcoe_cons[port] + MAX_ETH_CONS + MAX_VF_ETH_CONS);
        }
    }
    if (ERR_IF((max_iscsi_cons[port]  + max_rdma_cons[port] + max_fcoe_cons[port] + max_toe_cons[port] + MAX_ETH_CONS + MAX_VF_ETH_CONS) > max_supported_cons[port]))
    {
        return LM_STATUS_INVALID_PARAMETER;
    }

    /* Now lets save our port-specific variables. By this stage we have the maximum supported connections for our port. */
    pdev->hw_info.max_port_toe_conn = max_toe_cons[port];
    DbgMessage1(pdev, INFORMi, "max_toe_conn from shmem %d\n",pdev->hw_info.max_port_toe_conn);
    /* RDMA */
    pdev->hw_info.max_port_rdma_conn = max_rdma_cons[port];
    DbgMessage1(pdev, INFORMi, "max_rdma_conn from shmem %d\n",pdev->hw_info.max_port_rdma_conn);
    /* ISCSI */
    pdev->hw_info.max_port_iscsi_conn = max_iscsi_cons[port];
    DbgMessage1(pdev, INFORMi, "max_iscsi_conn from shmem %d\n",pdev->hw_info.max_port_iscsi_conn);
    /* FCOE */
    pdev->hw_info.max_port_fcoe_conn = max_fcoe_cons[port];
    DbgMessage1(pdev, INFORMi, "max_fcoe_conn from shmem %d\n",pdev->hw_info.max_port_fcoe_conn);

    pdev->hw_info.max_port_conns = log2_align(pdev->hw_info.max_port_toe_conn +
                                              pdev->hw_info.max_port_rdma_conn + pdev->hw_info.max_port_iscsi_conn
                                              + pdev->hw_info.max_port_fcoe_conn + MAX_ETH_CONS + MAX_VF_ETH_CONS);

    if (ERR_IF(pdev->hw_info.max_port_conns > max_bar_supported_cons[port]))
    {
        /* this would mean an error in the calculations above. */
        return LM_STATUS_INVALID_PARAMETER;
    }

    return LM_STATUS_SUCCESS;
}
static lm_status_t lm_check_valid_mf_cfg(lm_device_t *pdev)
{
    lm_hardware_mf_info_t *mf_info;
    lm_status_t lm_status = LM_STATUS_SUCCESS;
    u8_t i = 0;
    u8_t j = 0;
    u32_t mf_cfg1 = 0;
    u32_t mf_cfg2 = 0;
    u32_t ovlan1 = 0;
    u32_t ovlan2 = 0;

    mf_info = &pdev->hw_info.mf_info;
    /* hard coded offsets in vnic_cfg.tcl. if assertion here fails,
     * need to fix vnic_cfg.tcl script as well. */
//    ASSERT_STATIC(OFFSETOF(shmem_region_t,mf_cfg)            == 0x7e4);
    ASSERT_STATIC(OFFSETOF(mf_cfg_t,shared_mf_config.clp_mb) == 0);
  //ASSERT_STATIC(MCP_CLP_MB_NO_CLP                          == 0x80000000); not yet defined
    ASSERT_STATIC(OFFSETOF(mf_cfg_t,func_mf_config)          == 36);
    ASSERT_STATIC(OFFSETOF(func_mf_cfg_t,config)             == 0);
    ASSERT_STATIC(FUNC_MF_CFG_FUNC_HIDE                      == 0x1);
    ASSERT_STATIC(FUNC_MF_CFG_PROTOCOL_ETHERNET_WITH_RDMA    == 0x4);
    ASSERT_STATIC(FUNC_MF_CFG_FUNC_DISABLED                  == 0x8);
    ASSERT_STATIC(OFFSETOF(func_mf_cfg_t,mac_upper)          == 4);
    ASSERT_STATIC(OFFSETOF(func_mf_cfg_t,mac_lower)          == 8);
    ASSERT_STATIC(FUNC_MF_CFG_UPPERMAC_DEFAULT               == 0x0000ffff);
    ASSERT_STATIC(FUNC_MF_CFG_LOWERMAC_DEFAULT               == 0xffffffff);
    ASSERT_STATIC(OFFSETOF(func_mf_cfg_t,e1hov_tag)          == 12);
    ASSERT_STATIC(FUNC_MF_CFG_E1HOV_TAG_DEFAULT              == 0x0000ffff);
    ASSERT_STATIC(sizeof(func_mf_cfg_t)                      == 24);

    /* trace mf cfg parameters */
    DbgMessage1(pdev, INFORMi, "MF cfg parameters for function %d:\n", FUNC_ID(pdev));
    DbgMessage6(pdev, INFORMi, "\t func_mf_cfg=0x%x\n\t multi_vnics_mode=%d\n\t vnics_per_port=%d\n\t ovlan/vifid=%d\n\t min_bw=%d\n\t max_bw=%d\n",
                mf_info->func_mf_cfg,
                mf_info->vnics_per_port,
                mf_info->multi_vnics_mode,
                mf_info->ext_id,
                mf_info->min_bw,
                mf_info->max_bw);
    DbgMessage6(pdev, INFORMi, "\t mac addr (overiding main and iscsi): %02x %02x %02x %02x %02x %02x\n",
            pdev->hw_info.mac_addr[0],
            pdev->hw_info.mac_addr[1],
            pdev->hw_info.mac_addr[2],
            pdev->hw_info.mac_addr[3],
            pdev->hw_info.mac_addr[4],
            pdev->hw_info.mac_addr[5]);

    /* verify that function is not hidden */
    if (GET_FLAGS(mf_info->func_mf_cfg, FUNC_MF_CFG_FUNC_HIDE))
    {
        DbgMessage1(pdev, FATAL, "Enumerated function %d, is marked as hidden\n", FUNC_ID(pdev));
        lm_status = LM_STATUS_FAILURE;
        goto _end;
    }

    if (mf_info->vnics_per_port > 1 && !mf_info->multi_vnics_mode)
    {
        DbgMessage2(pdev, FATAL, "invalid mf mode configuration: vnics_per_port=%d, multi_vnics_mode=%d\n",
                    mf_info->vnics_per_port,
                    mf_info->multi_vnics_mode);
        lm_status = LM_STATUS_FAILURE;
        //DbgBreakIf(1);
        goto _end;
    }

    /* Sanity checks on outer-vlan for switch_dependent_mode... */
    if (mf_info->mf_mode == MULTI_FUNCTION_SD)
    {

        /* enumerated vnic id > 0 must have valid ovlan if we're in switch-dependet mode */
        if ((VNIC_ID(pdev) > 0) && !VALID_OVLAN(OVLAN(pdev)))
        {
            DbgMessage2(pdev, WARNi, "invalid mf mode configuration: VNICID=%d, Function is enumerated, ovlan (%d) is invalid\n",
                        VNIC_ID(pdev), OVLAN(pdev));
            lm_status = LM_STATUS_FAILURE;
            goto _end;
        }

        /* additional sanity checks */
        if (!VALID_OVLAN(OVLAN(pdev)) && mf_info->multi_vnics_mode)
        {
            DbgMessage2(pdev, FATAL, "invalid mf mode configuration: multi_vnics_mode=%d, ovlan=%d\n",
                        mf_info->multi_vnics_mode,
                        OVLAN(pdev));
            lm_status = LM_STATUS_FAILURE;
            //DbgBreakIf(1);
            goto _end;
        }
        /* verify all functions are either mf mode or sf mode:
         * if we set mode to mf, make sure that all non hidden functions have valid ovlan
         * if we set mode to sf, make sure that all non hidden functions have invalid ovlan */
        LM_FOREACH_ABS_FUNC_IN_PORT(pdev, i)
        {
            LM_MFCFG_READ(pdev, OFFSETOF(mf_cfg_t, func_mf_config[i].config),&mf_cfg1);
            LM_MFCFG_READ(pdev, OFFSETOF(mf_cfg_t, func_mf_config[i].e1hov_tag), &ovlan1);
            if (!GET_FLAGS(mf_cfg1, FUNC_MF_CFG_FUNC_HIDE) &&
                (((mf_info->multi_vnics_mode) && !VALID_OVLAN(ovlan1)) ||
                 ((!mf_info->multi_vnics_mode) && VALID_OVLAN(ovlan1))))
            {
                lm_status = LM_STATUS_FAILURE;
                //DbgBreakIf(1);
                goto _end;
            }
        }
        /* verify different ovlan between funcs on same port */
        LM_FOREACH_ABS_FUNC_IN_PORT(pdev, i)
        {
            LM_MFCFG_READ(pdev, OFFSETOF(mf_cfg_t, func_mf_config[i].config),&mf_cfg1);
            LM_MFCFG_READ(pdev, OFFSETOF(mf_cfg_t, func_mf_config[i].e1hov_tag), &ovlan1);
            /* iterate from the next function in the port till max func */
            for (j = i + 2; j < E1H_FUNC_MAX; j += 2)
            {
                LM_MFCFG_READ(pdev, OFFSETOF(mf_cfg_t, func_mf_config[j].config),&mf_cfg2);
                LM_MFCFG_READ(pdev, OFFSETOF(mf_cfg_t, func_mf_config[j].e1hov_tag), &ovlan2);
                if (!GET_FLAGS(mf_cfg1, FUNC_MF_CFG_FUNC_HIDE) && VALID_OVLAN(ovlan1) &&
                    !GET_FLAGS(mf_cfg2, FUNC_MF_CFG_FUNC_HIDE) && VALID_OVLAN(ovlan2) &&
                    (ovlan1 == ovlan2) )
                {
                    lm_status = LM_STATUS_FAILURE;
                    DbgBreakIf(1);
                    goto _end;
                }
            }
        }
    }
_end:
    return lm_status;
}

void lm_cmng_get_shmem_info( lm_device_t* pdev )
{
    u32_t                  val     = 0;
    u8_t                   i       = 0;
    u8_t                   vnic    = 0;
    lm_hardware_mf_info_t *mf_info = &pdev->hw_info.mf_info;;

    if( !IS_MF_MODE_CAPABLE(pdev) )
    {
        DbgBreakIf(1) ;
        return;
    }

    LM_FOREACH_ABS_FUNC_IN_PORT(pdev, i)
    {
        LM_MFCFG_READ(pdev, OFFSETOF(mf_cfg_t, func_mf_config[i].config),&val);
        /* get min/max bw */
        mf_info->min_bw[vnic] = (GET_FLAGS(val, FUNC_MF_CFG_MIN_BW_MASK) >> FUNC_MF_CFG_MIN_BW_SHIFT);
        mf_info->max_bw[vnic] = (GET_FLAGS(val, FUNC_MF_CFG_MAX_BW_MASK) >> FUNC_MF_CFG_MAX_BW_SHIFT);
        vnic++;
    }
}

/* Get shmem multi function config info for switch dependent mode */
static lm_status_t lm_get_shmem_mf_cfg_info_sd(lm_device_t *pdev)
{
    lm_hardware_mf_info_t *mf_info = &pdev->hw_info.mf_info;
    u32_t ovlan = 0;
    u32_t val   = 0;
    u32_t i     = 0;

    /* get ovlan if we're in switch-dependent mode... */
    LM_MFCFG_READ(pdev, OFFSETOF(mf_cfg_t, func_mf_config[ABS_FUNC_ID(pdev)].e1hov_tag),&val);
    mf_info->ext_id = (u16_t)val;

    /* decide on multi vnics mode */
    mf_info->multi_vnics_mode = VALID_OVLAN(OVLAN(pdev)) ? 1 : 0;
    mf_info->path_has_ovlan = mf_info->multi_vnics_mode;

    /* decide on path multi vnics mode - incase we're not in mf mode...and in 4-port-mode good enough to check vnic-0 of the other port, on the same path */
    if ((CHIP_PORT_MODE(pdev) == LM_CHIP_PORT_MODE_4) &&  !mf_info->multi_vnics_mode)
    {
        u8_t other_port = !PORT_ID(pdev);
        u8_t abs_func_on_other_port = PATH_ID(pdev) + 2*other_port;
        LM_MFCFG_READ(pdev, OFFSETOF(mf_cfg_t, func_mf_config[abs_func_on_other_port].e1hov_tag),&val);

        mf_info->path_has_ovlan = VALID_OVLAN((u16_t)val) ? 1 : 0;
    }

    /* Get capabilities */
    if (GET_FLAGS(mf_info->func_mf_cfg, FUNC_MF_CFG_PROTOCOL_MASK) == FUNC_MF_CFG_PROTOCOL_ISCSI)
    {
        pdev->params.mf_proto_support_flags |= LM_PROTO_SUPPORT_ISCSI;
    }
    else if (GET_FLAGS(mf_info->func_mf_cfg, FUNC_MF_CFG_PROTOCOL_MASK) == FUNC_MF_CFG_PROTOCOL_FCOE)
    {
        pdev->params.mf_proto_support_flags |= LM_PROTO_SUPPORT_FCOE;
    }
    else
    {
        pdev->params.mf_proto_support_flags |= LM_PROTO_SUPPORT_ETHERNET;
    }

    /* get vnics per port - set vnics_per_port according to highest enabled
     * function on the port since we support both switch-dependent + switch-independent mode, we check
     * if a function is valid, based on it's mac address. If nove has valid ovaln, vnics_per_port will remain 1
     */
    LM_FOREACH_ABS_FUNC_IN_PORT(pdev, i)
    {
        LM_MFCFG_READ(pdev, OFFSETOF(mf_cfg_t, func_mf_config[i].config),&val);
        LM_MFCFG_READ(pdev, OFFSETOF(mf_cfg_t, func_mf_config[i].e1hov_tag), &ovlan);
        if (!GET_FLAGS(val, FUNC_MF_CFG_FUNC_HIDE) && VALID_OVLAN(ovlan)) {
            mf_info->vnics_per_port = (i >> 1) + 1;
        }
    }
    /* round up to power of 2 */
    mf_info->vnics_per_port = (u8_t)log2_align(mf_info->vnics_per_port);
    return LM_STATUS_SUCCESS;
}

static void _copy_mac_upper_lower_to_arr(IN u32_t mac_upper, IN u32_t mac_lower, OUT u8_t* mac_addr)
{
    if(mac_addr) 
    {
        mac_addr[0] = (u8_t) (mac_upper >> 8);
        mac_addr[1] = (u8_t) mac_upper;
        mac_addr[2] = (u8_t) (mac_lower >> 24);
        mac_addr[3] = (u8_t) (mac_lower >> 16);
        mac_addr[4] = (u8_t) (mac_lower >> 8);
        mac_addr[5] = (u8_t) mac_lower;
    }
}

static void lm_get_shmem_ext_mac_addresses(lm_device_t *pdev)
{
    u32_t      mac_upper   = 0;
    u32_t      mac_lower   = 0;
    u32_t      offset      = 0;
    const u8_t abs_func_id = ABS_FUNC_ID(pdev);

    /* We have a different mac address per iscsi / fcoe - we'll set it from extended multi function info, but only if it's valid, otherwise
     * we'll leave the same mac as for L2
     */
    offset = OFFSETOF(mf_cfg_t, func_ext_config[abs_func_id].iscsi_mac_addr_upper);
    LM_MFCFG_READ(pdev, offset, &mac_upper);

    offset = OFFSETOF(mf_cfg_t, func_ext_config[abs_func_id].iscsi_mac_addr_lower);
    LM_MFCFG_READ(pdev, offset, &mac_lower);

    _copy_mac_upper_lower_to_arr(mac_upper, mac_lower, pdev->hw_info.iscsi_mac_addr);
    
    offset = OFFSETOF(mf_cfg_t, func_ext_config[abs_func_id].fcoe_mac_addr_upper);
    LM_MFCFG_READ(pdev, offset, &mac_upper);

    offset = OFFSETOF(mf_cfg_t, func_ext_config[abs_func_id].fcoe_mac_addr_lower);
    LM_MFCFG_READ(pdev, offset, &mac_lower);

    _copy_mac_upper_lower_to_arr(mac_upper, mac_lower, pdev->hw_info.fcoe_mac_addr);
    
    offset = OFFSETOF(mf_cfg_t, func_ext_config[abs_func_id].fcoe_wwn_port_name_upper);
    LM_MFCFG_READ(pdev, offset, &mac_upper);

    offset = OFFSETOF(mf_cfg_t, func_ext_config[abs_func_id].fcoe_wwn_port_name_lower);
    LM_MFCFG_READ(pdev, offset, &mac_lower);

    _copy_mac_upper_lower_to_arr(mac_upper, mac_lower, &(pdev->hw_info.fcoe_wwn_port_name[2]));
    pdev->hw_info.fcoe_wwn_port_name[0] = (u8_t) (mac_upper >> 24);
    pdev->hw_info.fcoe_wwn_port_name[1] = (u8_t) (mac_upper >> 16);

    offset = OFFSETOF(mf_cfg_t, func_ext_config[abs_func_id].fcoe_wwn_node_name_upper);
    LM_MFCFG_READ(pdev, offset, &mac_upper);

    offset = OFFSETOF(mf_cfg_t, func_ext_config[abs_func_id].fcoe_wwn_node_name_lower);
    LM_MFCFG_READ(pdev, offset, &mac_lower);

    _copy_mac_upper_lower_to_arr(mac_upper, mac_lower, &(pdev->hw_info.fcoe_wwn_node_name[2]));
    pdev->hw_info.fcoe_wwn_node_name[0] = (u8_t) (mac_upper >> 24);
    pdev->hw_info.fcoe_wwn_node_name[1] = (u8_t) (mac_upper >> 16);
}

/* Get shmem multi function config info for switch independent mode */
static lm_status_t lm_get_shmem_mf_cfg_info_si(lm_device_t *pdev)
{
    lm_hardware_mf_info_t *mf_info = &pdev->hw_info.mf_info;
    u32_t func_cfg  = 0;
    u32_t val       = 0;
    u32_t i         = 0;

   /* No outer-vlan... we're in switch-independent mode, so if the mac is valid - assume multi-function */
    LM_MFCFG_READ(pdev, OFFSETOF(mf_cfg_t, func_ext_config[ABS_FUNC_ID(pdev)].func_cfg),&val);
    val = val & MACP_FUNC_CFG_FLAGS_MASK;
    mf_info->multi_vnics_mode = (val != 0);
    mf_info->path_has_ovlan = FALSE;

    if (GET_FLAGS(val, MACP_FUNC_CFG_FLAGS_ENABLED ))
    {
        if (GET_FLAGS(val, MACP_FUNC_CFG_FLAGS_ETHERNET))
        {
            pdev->params.mf_proto_support_flags |= LM_PROTO_SUPPORT_ETHERNET;
        }
        if (GET_FLAGS(val, MACP_FUNC_CFG_FLAGS_ISCSI_OFFLOAD))
        {
            pdev->params.mf_proto_support_flags |= LM_PROTO_SUPPORT_ISCSI;
        }
        if (GET_FLAGS(val, MACP_FUNC_CFG_FLAGS_FCOE_OFFLOAD))
        {
            pdev->params.mf_proto_support_flags |= LM_PROTO_SUPPORT_FCOE;
        }
    }

    /* get vnics per port - set vnics_per_port according to highest enabled
     * function on the port since we support both switch-dependent + switch-independent mode, we check
     * if a function is valid, based on it's mac address. If nove has valid ovaln, vnics_per_port will remain 1
     */
    LM_FOREACH_ABS_FUNC_IN_PORT(pdev, i)
    {
        LM_MFCFG_READ(pdev, OFFSETOF(mf_cfg_t, func_mf_config[i].config),&val);
        LM_MFCFG_READ(pdev, OFFSETOF(mf_cfg_t, func_ext_config[i].func_cfg), &func_cfg);
        if (!GET_FLAGS(val, FUNC_MF_CFG_FUNC_HIDE) && (func_cfg != 0)) {
            mf_info->vnics_per_port = (i >> 1) + 1;
        }
    }
    /* round up to power of 2 */
    mf_info->vnics_per_port = (u8_t)log2_align(mf_info->vnics_per_port);

    lm_get_shmem_ext_mac_addresses(pdev);

    return LM_STATUS_SUCCESS;

}

lm_status_t lm_get_shmem_mf_cfg_info_niv(lm_device_t *pdev)
{
    lm_hardware_mf_info_t   *mf_info    = &pdev->hw_info.mf_info;
    u32_t                   val         = 0;

    LM_MFCFG_READ(pdev, OFFSETOF(mf_cfg_t, func_mf_config[ABS_FUNC_ID(pdev)].e1hov_tag),&val);
    mf_info->ext_id = (u16_t)(GET_FLAGS(val, FUNC_MF_CFG_E1HOV_TAG_MASK)>>FUNC_MF_CFG_E1HOV_TAG_SHIFT);
    mf_info->default_vlan = (u16_t)(GET_FLAGS(val, FUNC_MF_CFG_NIV_VLAN_MASK)>>FUNC_MF_CFG_NIV_VLAN_SHIFT);

    mf_info->multi_vnics_mode = TRUE;

    /* Get capabilities */
    if (GET_FLAGS(mf_info->func_mf_cfg, FUNC_MF_CFG_PROTOCOL_MASK) == FUNC_MF_CFG_PROTOCOL_ISCSI)
    {
        pdev->params.mf_proto_support_flags |= LM_PROTO_SUPPORT_ISCSI;
    }
    else if (GET_FLAGS(mf_info->func_mf_cfg, FUNC_MF_CFG_PROTOCOL_MASK) == FUNC_MF_CFG_PROTOCOL_FCOE)
    {
        pdev->params.mf_proto_support_flags |= LM_PROTO_SUPPORT_FCOE;
    }
    else
    {
        pdev->params.mf_proto_support_flags |= LM_PROTO_SUPPORT_ETHERNET;
    }

    lm_get_shmem_ext_mac_addresses(pdev);

    ///TODO DEFAULT-COS, COS-FILTER, RATE-LIMIT, BURST-SIZE, PRIORITY

    mf_info->niv_priority = 0;

    return LM_STATUS_SUCCESS;
}

static void lm_fcoe_set_default_wwns(lm_device_t *pdev)
{
    /* create default wwns from fcoe mac adress */
    mm_memcpy(&(pdev->hw_info.fcoe_wwn_port_name[2]), pdev->hw_info.fcoe_mac_addr, 6);
    pdev->hw_info.fcoe_wwn_port_name[0] = 0x20;
    pdev->hw_info.fcoe_wwn_port_name[1] = 0;
    mm_memcpy(&(pdev->hw_info.fcoe_wwn_node_name[2]), pdev->hw_info.fcoe_mac_addr, 6);
    pdev->hw_info.fcoe_wwn_node_name[0] = 0x10;
    pdev->hw_info.fcoe_wwn_node_name[1] = 0;
}
 

static lm_status_t lm_get_shmem_mf_cfg_info(lm_device_t *pdev)
{
    lm_hardware_mf_info_t *mf_info = &pdev->hw_info.mf_info;
    u32_t val       = 0;
    u32_t mac_upper = 0, mac_lower = 0;

    /* Set some mf_info defaults */
    mf_info->vnics_per_port = 1;
    mf_info->multi_vnics_mode = FALSE;
    mf_info->path_has_ovlan = FALSE;
    pdev->params.mf_proto_support_flags = 0;

    /* Get the multi-function-mode value (switch dependent / independent / single-function )  */
    LM_SHMEM_READ(pdev,OFFSETOF(shmem_region_t,dev_info.shared_feature_config.config),&val);
    val &= SHARED_FEAT_CFG_FORCE_SF_MODE_MASK;

    switch (val)
    {
    case SHARED_FEAT_CFG_FORCE_SF_MODE_SWITCH_INDEPT:
        mf_info->mf_mode = MULTI_FUNCTION_SI;
        DbgBreakIf(CHIP_IS_E1x(pdev));
        break;
    case SHARED_FEAT_CFG_FORCE_SF_MODE_MF_ALLOWED:
    case SHARED_FEAT_CFG_FORCE_SF_MODE_SPIO4:
        mf_info->mf_mode = MULTI_FUNCTION_SD;
        break;
    case SHARED_FEAT_CFG_FORCE_SF_MODE_FORCED_SF:
        /* We're not in multi-function mode - return with vnics_per_port=1 & multi_vnics_mode = FALSE*/
        return LM_STATUS_SUCCESS;
    case SHARED_FEAT_CFG_FORCE_SF_MODE_NIV_MODE:
        mf_info->mf_mode = MULTI_FUNCTION_NIV;
        break;
    default:
        DbgBreakMsg(" Unknown mf mode\n");
        return LM_STATUS_FAILURE;
    }

    /* Get the multi-function configuration */
    LM_MFCFG_READ(pdev, OFFSETOF(mf_cfg_t, func_mf_config[ABS_FUNC_ID(pdev)].config),&val);
    mf_info->func_mf_cfg = val;

    /* Get the permanent L2 MAC address. */
    LM_MFCFG_READ(pdev, OFFSETOF(mf_cfg_t, func_mf_config[ABS_FUNC_ID(pdev)].mac_upper),&mac_upper);
    LM_MFCFG_READ(pdev, OFFSETOF(mf_cfg_t, func_mf_config[ABS_FUNC_ID(pdev)].mac_lower),&mac_lower);
    if (mac_upper == FUNC_MF_CFG_UPPERMAC_DEFAULT )
    {   /* mac address in mf cfg is not valid  */
        RESET_FLAGS(mf_info->flags, MF_INFO_VALID_MAC);
        DbgBreakIf(FUNC_MF_CFG_LOWERMAC_DEFAULT != mac_lower );
        if (VNIC_ID(pdev) > 0)
        {
            DbgMessage1(pdev, WARNi, "function %d: Does not have valid mf mac address for vnic id > 0, Using port's default!\n", FUNC_ID(pdev));
        }
    }
    else
    {
        /* mac address is valid */
        SET_FLAGS(mf_info->flags,MF_INFO_VALID_MAC);
        _copy_mac_upper_lower_to_arr(mac_upper, mac_lower, pdev->hw_info.mac_addr);

        /* by default set all iscsi and fcoe mac addresses the same as network. 
           this may be overriden later according to the actual mode of multi function */
        mm_memcpy(pdev->hw_info.iscsi_mac_addr, pdev->hw_info.mac_addr, 6);
        mm_memcpy(pdev->hw_info.fcoe_mac_addr, pdev->hw_info.mac_addr, 6);
        lm_fcoe_set_default_wwns(pdev);        
    }

    switch(mf_info->mf_mode)
    {
        case MULTI_FUNCTION_SD:
        {
            lm_get_shmem_mf_cfg_info_sd(pdev);
        }
        break;
        case MULTI_FUNCTION_SI:
        {
            if (FUNC_MF_CFG_UPPERMAC_DEFAULT != mac_upper)
            {
                // If mac_upper is FUNC_MF_CFG_UPPERMAC_DEFAULT then we are in SF mode.
                lm_get_shmem_mf_cfg_info_si(pdev);
            }
            else
            {
                if (VNIC_ID(pdev) > 0)
                {
                    return LM_STATUS_FAILURE;
                }
            }
        }
        break;
        case MULTI_FUNCTION_NIV:
        {
            lm_get_shmem_mf_cfg_info_niv(pdev);
        }
        break;
        default:
        {
            DbgBreakIfAll(TRUE);
            return LM_STATUS_FAILURE;
        }
    }

    lm_cmng_get_shmem_info(pdev);

    return lm_check_valid_mf_cfg(pdev);
}

lm_status_t lm_verify_validity_map(lm_device_t *pdev)
{
    u64_t        wait_cnt       = 0 ;
    u64_t        wait_cnt_limit = 200000; // 4 seconds (ASIC)
    u32_t        val            = 0;
    lm_status_t  lm_status      = LM_STATUS_FAILURE ;
    if ( CHK_NULL(pdev) )
    {
        return LM_STATUS_INVALID_PARAMETER ;
    }
    wait_cnt_limit*= (u64_t)(pdev->vars.clk_factor) ;
    for(wait_cnt = 0; wait_cnt < wait_cnt_limit; wait_cnt++)
    {
        //it takes MFW ~200ms to initialize validity_map.
        LM_SHMEM_READ(pdev,OFFSETOF(shmem_region_t, validity_map[PORT_ID(pdev)]),&val);
        // check that shared memory is valid.
        if((val & (SHR_MEM_VALIDITY_DEV_INFO | SHR_MEM_VALIDITY_MB)) == (SHR_MEM_VALIDITY_DEV_INFO|SHR_MEM_VALIDITY_MB))
        {
            lm_status = LM_STATUS_SUCCESS ;
            break;
        }
        mm_wait(pdev, 20);
    }
    DbgMessage1(pdev, INFORMi, "lm_verify_validity_map: shmem signature %d\n",val);
    return lm_status ;
}
static lm_status_t lm_get_shmem_info(lm_device_t *pdev)
{
    lm_hardware_mf_info_t *mf_info   = &pdev->hw_info.mf_info;
    u32_t                  val            = 0;
    u32_t                  val2           = 0;
    u32_t                  min_shmem_addr = 0;
    u32_t                  max_shmem_addr = 0;
    u8_t                   pf_abs_id      = 0xff;
    u8_t                   i         = 0;
    lm_status_t            lm_status = LM_STATUS_SUCCESS;

    /* set defaults: */
    mf_info->multi_vnics_mode = 0;
    mf_info->vnics_per_port   = 1;
    mf_info->ext_id            = 0xffff; /* invalid ovlan */ /* TBD - E1H: - what is the right value for Cisco? */
    for (i = 0; i < MAX_VNIC_NUM; i++)
    {
        mf_info->min_bw[i] = 0;
        mf_info->max_bw[i] = 100;
    }
    pdev->hw_info.shmem_base          = 0;
    pdev->hw_info.max_port_toe_conn   = MAX_CONNECTIONS_TOE;
    pdev->hw_info.max_port_rdma_conn  = MAX_CONNECTIONS_RDMA;
    pdev->hw_info.max_port_iscsi_conn = MAX_CONNECTIONS_ISCSI;
    pdev->hw_info.max_port_fcoe_conn  = MAX_CONNECTIONS_FCOE;
    pdev->hw_info.max_port_conns      = MAX_CONNECTIONS;
    pdev->hw_info.max_common_conns    = MAX_CONNECTIONS;

    DbgMessage1(pdev, WARN, "lm_get_shmem_info: FUNC_ID: %d\n", FUNC_ID(pdev));
    if (CHIP_PORT_MODE(pdev) == LM_CHIP_PORT_MODE_4)
    {
        pf_abs_id = PATH_ID(pdev) + 2*FUNC_ID(pdev);
    }
    else
    {
        pf_abs_id = PATH_ID(pdev) + FUNC_ID(pdev);
    }
    DbgMessage1(pdev, WARN, "lm_get_shmem_info: PCI_FUNC_ID: %d\n", pf_abs_id);
    DbgMessage1(pdev, WARN, "lm_get_shmem_info: PORT_ID: %d\n", PORT_ID(pdev));

    if (CHIP_PORT_MODE(pdev) == LM_CHIP_PORT_MODE_4)
    {
        DbgMessage1(pdev, WARN, "lm_get_shmem_info: ETH_PORT_ID: %d\n", PATH_ID(pdev) + 2*PORT_ID(pdev));
    }
    else
    {
        DbgMessage1(pdev, WARN, "lm_get_shmem_info: ETH_PORT_ID: %d\n", PATH_ID(pdev) + PORT_ID(pdev));
    }

    DbgMessage1(pdev, WARN, "lm_get_shmem_info: PATH_ID: %d\n", PATH_ID(pdev));
    DbgMessage1(pdev, WARN, "lm_get_shmem_info: VNIC_ID: %d\n", VNIC_ID(pdev));
    DbgMessage1(pdev, WARN, "lm_get_shmem_info: FUNC_MAILBOX_ID: %d\n", FUNC_MAILBOX_ID(pdev));

    /* Get firmware share memory base address. */
    //TODO should be read from misc block
    val = REG_RD(pdev,MISC_REG_SHARED_MEM_ADDR);

    /* it takes MFW ~20ms to initialize shmem_base (may be relevant for hot plug
       scenario, though problem never seen). Wait 50ms and try again one more time */
    if(0 == val) 
    {
        mm_wait(pdev, 50000);
        val = REG_RD(pdev,MISC_REG_SHARED_MEM_ADDR);
    }

    if (CHIP_IS_E1(pdev))
    {
        min_shmem_addr = 0xa0000;
        max_shmem_addr = 0xb0000;
    }
    else if (CHIP_IS_E1H(pdev))
    {
       min_shmem_addr = 0xa0000;
       max_shmem_addr = 0xc0000;
    }
    else if (CHIP_IS_E2E3(pdev))
    {
        min_shmem_addr = 0x3a0000;
        max_shmem_addr = 0x3c8000;
    }
    else
    {
        u32 pcicfg_chip;
        mm_read_pci(pdev, 0, &pcicfg_chip);
        DbgMessage3(pdev, FATAL , "Unknown chip 0x%x, pcicfg[0]=0x%x, GRC[0x2000]=0x%x\n",
                    CHIP_NUM(pdev), pcicfg_chip, REG_RD(pdev, 0x2000));
        DbgBreakMsg("Unknown chip version");
    }

    // Check shmem availabilty
    if (val < min_shmem_addr || val >= max_shmem_addr)
    {
        // bypass MCP
        DbgMessage(pdev, WARN, "MCP Down Detected\n");
#ifndef _VBD_CMD_
        DbgMessage1(pdev, FATAL, "FW ShMem addr: 0x%x\n", val);
#endif // _VBD_CMD_
        pdev->hw_info.mcp_detected = 0;
        /* should have a magic number written if configuration was set otherwise, use default above */
        LM_SHMEM_READ(pdev, NO_MCP_WA_CFG_SET_ADDR, &val);
        if (val == NO_MCP_WA_CFG_SET_MAGIC)
        {
            LM_SHMEM_READ(pdev, NO_MCP_WA_FORCE_5710, &val);
            LM_SHMEM_READ(pdev, NO_MCP_WA_MULTI_VNIC_MODE, &val);
            mf_info->multi_vnics_mode = (u8_t)val;
            if (mf_info->multi_vnics_mode)
            {
                LM_SHMEM_READ(pdev, NO_MCP_WA_OVLAN(pf_abs_id), &val);
                mf_info->ext_id = (u16_t)val;

                mf_info->multi_vnics_mode = VALID_OVLAN(mf_info->ext_id)? 1 : 0;
                mf_info->path_has_ovlan = mf_info->multi_vnics_mode;

                /* decide on path multi vnics mode - incase we're not in mf mode...and in 4-port-mode good enough to check vnic-0 of the other port, on the same path */
                if ((CHIP_PORT_MODE(pdev) == LM_CHIP_PORT_MODE_4) &&  !mf_info->multi_vnics_mode)
                {
                    u8_t other_port = !PORT_ID(pdev);
                    u8_t abs_func_on_other_port = PATH_ID(pdev) + 2*other_port;
                    LM_SHMEM_READ(pdev, NO_MCP_WA_OVLAN(abs_func_on_other_port), &val);

                    mf_info->path_has_ovlan = VALID_OVLAN((u16_t)val) ? 1 : 0;
                }

                /* For simplicity, we leave vnics_per_port to be 2, for resource splitting issues... */
                if (mf_info->path_has_ovlan)
                {
                    if (CHIP_PORT_MODE(pdev) == LM_CHIP_PORT_MODE_4)
                    {
                        mf_info->vnics_per_port = 2;
                    }
                    else
                    {
                        mf_info->vnics_per_port = 4;
                    }
                }

                /* If we're multi-vnic, we'll set a default mf_mode of switch-dependent, this could be overriden
                 * later on by registry */
                mf_info->mf_mode = MULTI_FUNCTION_SD;

            }
            lm_status = lm_get_shmem_license_info(pdev);
            if (lm_status != LM_STATUS_SUCCESS)
            {
                return lm_status;
            }
        }
        /* sanity checks on vnic params */
        if (mf_info->multi_vnics_mode)
        {
            if (!VALID_OVLAN(mf_info->ext_id))
            {
                DbgMessage2(pdev, FATAL, "Invalid ovlan (0x%x) configured for Func %d. Can't load the function.\n",
                            mf_info->ext_id, pf_abs_id);
                lm_status = LM_STATUS_FAILURE;
            }
        }
        if ((mf_info->vnics_per_port - 1 < VNIC_ID(pdev)) || ( !mf_info->multi_vnics_mode && (VNIC_ID(pdev) > 0)))
        {
            DbgMessage2(pdev, FATAL, "Invalid vnics_per_port (%d) configured for Func %d. Can't load the function.\n",
                        mf_info->vnics_per_port, pf_abs_id);
            lm_status = LM_STATUS_FAILURE;
        }
        return lm_status;
    } // NO MCP

    /* must not read any additional info from shmem and/or any register set by 
       MFW before validty map is verifired. Relevant mainly to hotplug scenario */
    pdev->hw_info.mcp_detected = 1;
    pdev->hw_info.shmem_base   = val;    
    lm_status = lm_verify_validity_map( pdev );    
    if(LM_STATUS_SUCCESS != lm_status )
    {
        DbgMessage(pdev, WARN, "lm_get_shmem_info: Shmem signature not present.\n");
        pdev->hw_info.mcp_detected = 0;
        return LM_STATUS_SUCCESS;
    }

    // read shmem_base2
    pdev->hw_info.shmem_base2 = REG_RD(pdev, PATH_ID(pdev) ? MISC_REG_GENERIC_CR_1 : MISC_REG_GENERIC_CR_0);
    pdev->hw_info.mf_cfg_base = lm_get_shmem_mf_cfg_base(pdev);
    DbgMessage3(pdev, WARN, "MCP Up Detected. shmem_base=0x%x shmem_base2=0x%x mf_cfg_offset=0x%x\n",
                pdev->hw_info.shmem_base, pdev->hw_info.shmem_base2, pdev->hw_info.mf_cfg_base);
    
    /* Get the hw config words. */
    LM_SHMEM_READ(pdev, OFFSETOF(shmem_region_t, dev_info.shared_hw_config.config),&val);
    pdev->hw_info.nvm_hw_config = val;
    pdev->params.link.hw_led_mode = ((pdev->hw_info.nvm_hw_config & SHARED_HW_CFG_LED_MODE_MASK) >> SHARED_HW_CFG_LED_MODE_SHIFT);
    DbgMessage1(pdev, INFORMi, "nvm_hw_config %d\n",val);
    LM_SHMEM_READ(pdev,
        OFFSETOF(shmem_region_t, dev_info.shared_hw_config.config2),&val);
    pdev->hw_info.nvm_hw_config2 = val;
    DbgMessage1(pdev, INFORMi, "nvm_hw_configs %d\n",val);
    //board_sn;
    LM_SHMEM_READ(pdev,
        OFFSETOF(shmem_region_t, dev_info.shared_hw_config.part_num),&val);
    pdev->hw_info.board_num[0] = (u8_t) val;
    pdev->hw_info.board_num[1] = (u8_t) (val >> 8);
    pdev->hw_info.board_num[2] = (u8_t) (val >> 16);
    pdev->hw_info.board_num[3] = (u8_t) (val >> 24);
    LM_SHMEM_READ(pdev,
        OFFSETOF(shmem_region_t, dev_info.shared_hw_config.part_num)+4,&val);
    pdev->hw_info.board_num[4] = (u8_t) val;
    pdev->hw_info.board_num[5] = (u8_t) (val >> 8);
    pdev->hw_info.board_num[6] = (u8_t) (val >> 16);
    pdev->hw_info.board_num[7] = (u8_t) (val >> 24);
    LM_SHMEM_READ(pdev,
        OFFSETOF(shmem_region_t, dev_info.shared_hw_config.part_num)+8,&val);
    pdev->hw_info.board_num[8] = (u8_t) val;
    pdev->hw_info.board_num[9] = (u8_t) (val >> 8);
    pdev->hw_info.board_num[10] =(u8_t) (val >> 16);
    pdev->hw_info.board_num[11] =(u8_t) (val >> 24);
    LM_SHMEM_READ(pdev,
        OFFSETOF(shmem_region_t, dev_info.shared_hw_config.part_num)+12,&val);
    pdev->hw_info.board_num[12] = (u8_t) val;
    pdev->hw_info.board_num[13] = (u8_t) (val >> 8);
    pdev->hw_info.board_num[14] = (u8_t) (val >> 16);
    pdev->hw_info.board_num[15] = (u8_t) (val >> 24);
    DbgMessage(pdev, INFORMi, "board_sn: ");
    for (i = 0 ; i < 16 ; i++ )
    {
        DbgMessage1(pdev, INFORMi, "%02x",pdev->hw_info.board_num[i]);
    }
    DbgMessage(pdev, INFORMi, "\n");
    LM_SHMEM_READ(pdev,
        OFFSETOF(shmem_region_t, dev_info.port_hw_config[PORT_ID(pdev)].mac_upper),&val);    
    LM_SHMEM_READ(pdev,
        OFFSETOF(shmem_region_t, dev_info.port_hw_config[PORT_ID(pdev)].mac_lower),&val2);
    _copy_mac_upper_lower_to_arr(val, val2, pdev->hw_info.mac_addr);
    
    /* Get iSCSI MAC address. */
    LM_SHMEM_READ(pdev,
        OFFSETOF(shmem_region_t,dev_info.port_hw_config[PORT_ID(pdev)].iscsi_mac_upper),&val);    
    LM_SHMEM_READ(pdev,
        OFFSETOF(shmem_region_t,dev_info.port_hw_config[PORT_ID(pdev)].iscsi_mac_lower),&val2);
    _copy_mac_upper_lower_to_arr(val, val2, pdev->hw_info.iscsi_mac_addr);    

    DbgMessage6(pdev, INFORMi, "main mac addr: %02x %02x %02x %02x %02x %02x\n",
        pdev->hw_info.mac_addr[0],
        pdev->hw_info.mac_addr[1],
        pdev->hw_info.mac_addr[2],
        pdev->hw_info.mac_addr[3],
        pdev->hw_info.mac_addr[4],
        pdev->hw_info.mac_addr[5]);
    DbgMessage6(pdev, INFORMi, "iSCSI mac addr: %02x %02x %02x %02x %02x %02x\n",
        pdev->hw_info.iscsi_mac_addr[0],
        pdev->hw_info.iscsi_mac_addr[1],
        pdev->hw_info.iscsi_mac_addr[2],
        pdev->hw_info.iscsi_mac_addr[3],
        pdev->hw_info.iscsi_mac_addr[4],
        pdev->hw_info.iscsi_mac_addr[5]);

     /* Get FCoE MAC addresses. */
    LM_SHMEM_READ(pdev,
        OFFSETOF(shmem_region_t,dev_info.port_hw_config[PORT_ID(pdev)].fcoe_fip_mac_upper),&val);    
    LM_SHMEM_READ(pdev,
        OFFSETOF(shmem_region_t,dev_info.port_hw_config[PORT_ID(pdev)].fcoe_fip_mac_lower),&val2);
    _copy_mac_upper_lower_to_arr(val, val2, pdev->hw_info.fcoe_mac_addr);
       
    LM_SHMEM_READ(pdev,
        OFFSETOF(shmem_region_t,dev_info.port_hw_config[PORT_ID(pdev)].fcoe_wwn_port_name_upper),&val);
    LM_SHMEM_READ(pdev,
        OFFSETOF(shmem_region_t,dev_info.port_hw_config[PORT_ID(pdev)].fcoe_wwn_port_name_lower),&val2);
    _copy_mac_upper_lower_to_arr(val, val2, &(pdev->hw_info.fcoe_wwn_port_name[2]));    
    pdev->hw_info.fcoe_wwn_port_name[0] = (u8_t) (val >> 24);
    pdev->hw_info.fcoe_wwn_port_name[1] = (u8_t) (val >> 16);
    
    LM_SHMEM_READ(pdev,
        OFFSETOF(shmem_region_t,dev_info.port_hw_config[PORT_ID(pdev)].fcoe_wwn_node_name_upper),&val);    
    LM_SHMEM_READ(pdev,
        OFFSETOF(shmem_region_t,dev_info.port_hw_config[PORT_ID(pdev)].fcoe_wwn_node_name_lower),&val2);    
    _copy_mac_upper_lower_to_arr(val, val2, &(pdev->hw_info.fcoe_wwn_node_name[2]));    
    pdev->hw_info.fcoe_wwn_node_name[0] = (u8_t) (val >> 24);
    pdev->hw_info.fcoe_wwn_node_name[1] = (u8_t) (val >> 16);
      
    /* mba features*/
    LM_SHMEM_READ(pdev,
        OFFSETOF(shmem_region_t,dev_info.port_feature_config[PORT_ID(pdev)].mba_config),
        &val);
    pdev->hw_info.mba_features = (val & PORT_FEATURE_MBA_BOOT_AGENT_TYPE_MASK);
    DbgMessage1(pdev, INFORMi, "mba_features %d\n",pdev->hw_info.mba_features);
    /* mba_vlan_cfg */
    LM_SHMEM_READ(pdev,
        OFFSETOF(shmem_region_t,dev_info.port_feature_config[PORT_ID(pdev)].mba_vlan_cfg),
        &val);
    pdev->hw_info.mba_vlan_cfg = val ;
    DbgMessage1(pdev, INFORMi, "mba_vlan_cfg 0x%x\n",pdev->hw_info.mba_vlan_cfg);

    // port_feature_config bits
    LM_SHMEM_READ(pdev,
        OFFSETOF(shmem_region_t,dev_info.port_feature_config[PORT_ID(pdev)].config),
        &val);
    pdev->hw_info.port_feature_config = val;
    DbgMessage1(pdev, INFORMi, "port_feature_config 0x%x\n",pdev->hw_info.port_feature_config);
    /* bc rev */
    LM_SHMEM_READ(pdev,OFFSETOF(shmem_region_t,dev_info.bc_rev),&val);
    pdev->hw_info.bc_rev = val;
    DbgMessage1(pdev, INFORMi, "bc_rev %d\n",val);
    /* clc params*/
    LM_SHMEM_READ(pdev,OFFSETOF(shmem_region_t,dev_info.port_hw_config[PORT_ID(pdev)].speed_capability_mask),&val);
    pdev->params.link.speed_cap_mask[0] = val;
    DbgMessage1(pdev, INFORMi, "speed_cap_mask1 %d\n",val);
    LM_SHMEM_READ(pdev,OFFSETOF(shmem_region_t,dev_info.port_hw_config[PORT_ID(pdev)].speed_capability_mask2),&val);
    pdev->params.link.speed_cap_mask[1] = val;
    DbgMessage1(pdev, INFORMi, "speed_cap_mask2 %d\n",val);
    /* Get lane swap*/
    LM_SHMEM_READ(pdev,OFFSETOF(shmem_region_t,dev_info.port_hw_config[PORT_ID(pdev)].lane_config),&val);
    pdev->params.link.lane_config = val;
    DbgMessage1(pdev, INFORMi, "lane_config %d\n",val);
    /*link config  */
    LM_SHMEM_READ(pdev,OFFSETOF(shmem_region_t,dev_info.port_feature_config[PORT_ID(pdev)].link_config),&val);
    pdev->hw_info.link_config[ELINK_INT_PHY] = val;
    pdev->params.link.switch_cfg = val & PORT_FEATURE_CONNECTED_SWITCH_MASK;
    DbgMessage1(pdev, INFORMi, "link config %d\n",val);
    LM_SHMEM_READ(pdev,OFFSETOF(shmem_region_t,dev_info.port_feature_config[PORT_ID(pdev)].link_config2),&val);
    pdev->hw_info.link_config[ELINK_EXT_PHY1] = val;
    /* Get the override preemphasis flag */
    LM_SHMEM_READ(pdev,OFFSETOF(shmem_region_t,dev_info.shared_feature_config.config),&val);
    if GET_FLAGS(val, SHARED_FEAT_CFG_OVERRIDE_PREEMPHASIS_CFG_ENABLED)
    {
        SET_FLAGS( pdev->params.link.feature_config_flags, ELINK_FEATURE_CONFIG_OVERRIDE_PREEMPHASIS_ENABLED);
    }
    else
    {
        RESET_FLAGS(pdev->params.link.feature_config_flags,ELINK_FEATURE_CONFIG_OVERRIDE_PREEMPHASIS_ENABLED);
    }
     LM_SHMEM_READ(pdev,OFFSETOF(shmem_region_t,dev_info.port_hw_config[PORT_ID(pdev)].multi_phy_config),&val);
     /* set the initial value to the link params */
     pdev->params.link.multi_phy_config = val;
     /* save the initial value if we'll want to restore it later */
    pdev->hw_info.multi_phy_config = val;
    // check if 10g is not supported for this board
    pdev->hw_info.phy_no_10g_support = FALSE ;

    LM_SHMEM_READ(pdev,OFFSETOF(shmem_region_t,dev_info.port_hw_config[PORT_ID(pdev)].default_cfg),&val);
    pdev->hw_info.phy_force_kr_enabler = (val & PORT_HW_CFG_FORCE_KR_ENABLER_MASK) ;

    if( PORT_HW_CFG_FORCE_KR_ENABLER_NOT_FORCED != pdev->hw_info.phy_force_kr_enabler )
    {
        if( FALSE == ( pdev->params.link.speed_cap_mask[0] & PORT_HW_CFG_SPEED_CAPABILITY_D0_10G ))
        {
            pdev->hw_info.phy_no_10g_support = TRUE ;
        }
    }

    /* Check License for toe/rdma/iscsi */
#ifdef _LICENSE_H
    lm_status = lm_get_shmem_license_info(pdev);
    if (lm_status != LM_STATUS_SUCCESS)
    {
        return lm_status;
    }
#endif
    /* get mf config parameters */
    if (IS_MF_MODE_CAPABLE(pdev))
    {
        lm_status = lm_get_shmem_mf_cfg_info(pdev);
        if (lm_status != LM_STATUS_SUCCESS)
        {
            return lm_status;
        }
    }
    else if (FUNC_ID(pdev) != PORT_ID(pdev))
    {
        DbgMessage(pdev, WARNi, "Illegal to load func %d of port %d on non MF mode capable device\n");
        return LM_STATUS_FAILURE;
    }
    return LM_STATUS_SUCCESS;
}
/** lm_init_cam_params
 *  set cam/mac parameters
 *
 *  cam mapping is dynamic, we only set sizes...
 *
 */
static void lm_init_cam_params(lm_device_t *pdev)
{
    /* FIXME: remove once constants are in hsi file */
    #define LM_CAM_SIZE_EMUL                    (5)                                /*5 per vnic also in single function mode (real cam size on emulation is 20 per port) */
    #define LM_MC_TABLE_SIZE_EMUL               (1)
    #define LM_CAM_SIZE_EMUL_E2                 (40)

    u16_t mc_credit;
    u16_t uc_credit;
    u8_t b_is_asic = ((CHIP_REV(pdev) != CHIP_REV_EMUL) && (CHIP_REV(pdev) != CHIP_REV_FPGA));
    u8_t num_ports = 2;

    /* set CAM parameters according to EMUL/FPGA or ASIC + Chip*/
    mm_mem_zero(pdev->params.uc_table_size, sizeof(pdev->params.uc_table_size));
    mm_mem_zero(pdev->params.mc_table_size, sizeof(pdev->params.mc_table_size));

    if (CHIP_IS_E1(pdev))
    {
        pdev->params.cam_size = b_is_asic? MAX_MAC_CREDIT_E1 / num_ports : LM_CAM_SIZE_EMUL;

        mc_credit = b_is_asic? LM_MC_NDIS_TABLE_SIZE : LM_MC_TABLE_SIZE_EMUL;
        uc_credit = pdev->params.cam_size - mc_credit; /* E1 multicast is in CAM */

    /* init unicast table entires */
    pdev->params.uc_table_size[LM_CLI_IDX_ISCSI]    = 1;
        pdev->params.uc_table_size[LM_CLI_IDX_NDIS]  = uc_credit - 1; /* - one for iscsi... */

    /* init multicast table entires */
        pdev->params.mc_table_size[LM_CLI_IDX_NDIS] = mc_credit;

        DbgMessage3(pdev, INFORMi, "uc_table_size[ndis]=%d, uc_table_size[ndis]=%d, mc_table_size[ndis]=%d\n",
                   pdev->params.uc_table_size[LM_CLI_IDX_NDIS], pdev->params.uc_table_size[LM_CLI_IDX_ISCSI],
                   pdev->params.mc_table_size[LM_CLI_IDX_NDIS]);

    }
    else if (CHIP_IS_E1H(pdev))
    {
        pdev->params.cam_size = b_is_asic? MAX_MAC_CREDIT_E1H / num_ports: LM_CAM_SIZE_EMUL;
        pdev->params.cam_size = pdev->params.cam_size / pdev->params.vnics_per_port;
        uc_credit = pdev->params.cam_size;

        /* init unicast table entires */
        pdev->params.uc_table_size[LM_CLI_IDX_ISCSI] = 1;
        pdev->params.uc_table_size[LM_CLI_IDX_NDIS]  = uc_credit - 1; /* - one for iscsi... */

        /* init multicast table entires */
        pdev->params.mc_table_size[LM_CLI_IDX_NDIS] = LM_MC_NDIS_TABLE_SIZE;

        DbgMessage3(pdev, INFORMi, "uc_table_size[ndis]=%d, uc_table_size[ndis]=%d, mc_table_size[ndis]=%d\n",
                   pdev->params.uc_table_size[LM_CLI_IDX_NDIS], pdev->params.uc_table_size[LM_CLI_IDX_ISCSI],
                   pdev->params.mc_table_size[LM_CLI_IDX_NDIS]);
    }
    else if (CHIP_IS_E2E3(pdev))
        {
        num_ports = (CHIP_PORT_MODE(pdev) == LM_CHIP_PORT_MODE_4)? 2 : 1;
        pdev->params.cam_size = b_is_asic? MAX_MAC_CREDIT_E2 / num_ports: LM_CAM_SIZE_EMUL_E2;

        uc_credit = pdev->params.cam_size;

        /* init unicast table entires */
        pdev->params.uc_table_size[LM_CLI_IDX_ISCSI] = 1;
        pdev->params.uc_table_size[LM_CLI_IDX_FCOE]  = 1;
        pdev->params.uc_table_size[LM_CLI_IDX_NDIS]  = uc_credit - 2; /* - the two above... */

        /* init multicast table entires */
        pdev->params.mc_table_size[LM_CLI_IDX_NDIS] = LM_MC_NDIS_TABLE_SIZE;
        pdev->params.mc_table_size[LM_CLI_IDX_FCOE] = LM_MC_FCOE_TABLE_SIZE;

        DbgMessage5(pdev, INFORMi, "uc_table_size[ndis]=%d, uc_table_size[ndis]=%d, uc_table_size[fcoe]=%d, mc_table_size[ndis]=%d, mc_table_size[fcoe]=%d\n",
                    pdev->params.uc_table_size[LM_CLI_IDX_NDIS], pdev->params.uc_table_size[LM_CLI_IDX_ISCSI],
                    pdev->params.uc_table_size[LM_CLI_IDX_FCOE],
                    pdev->params.mc_table_size[LM_CLI_IDX_NDIS], pdev->params.mc_table_size[LM_CLI_IDX_FCOE]);
    }
    else
    {
        DbgBreakIfAll("New Chip?? initialize cam params!\n");
    }

    /* override CAM parameters for chips later than E1 */
    if (IS_PFDEV(pdev))
    {
        pdev->params.base_offset_in_cam_table = ((num_ports == 2)? FUNC_ID(pdev) : VNIC_ID(pdev)) * LM_CAM_SIZE(pdev);
            }
    else if (IS_CHANNEL_VFDEV(pdev))
        {
        pdev->params.base_offset_in_cam_table = 0;
        pdev->params.mc_table_size[LM_CLI_IDX_NDIS] = 0; /* VF TODO: not implemented yet!! */
    }
}

static lm_status_t lm_init_params(lm_device_t *pdev, u8_t validate)
{
    typedef struct _param_entry_t
    {
        /* Ideally, we want to save the address of the parameter here.
        * However, some compiler will not allow us to dynamically
        * initialize the pointer to a parameter in the table below.
        * As an alternative, we will save the offset to the parameter
        * from pdev device structure. */
        u32_t offset;
        /* Parameter default value. */
        u32_t asic_default;
        u32_t fpga_default;
        u32_t emulation_default;
        /* Limit checking is diabled if min and max are zeros. */
        u32_t min;
        u32_t max;
    } param_entry_t;
    #define _OFFSET(_name)          (OFFSETOF(lm_device_t, params._name))
    #define PARAM_VAL(_pdev, _entry) \
        (*((u32_t *) ((u8_t *) (_pdev) + (_entry)->offset)))
    #define SET_PARAM_VAL(_pdev, _entry, _val) \
        *((u32_t *) ((u8_t *) (_pdev) + (_entry)->offset)) = (_val)
    static param_entry_t param_list[] =
    {
        /*                                 asic     fpga     emul
        offset                          default  default  default min     max */
        { _OFFSET(mtu[LM_CLI_IDX_NDIS]),  9216,    9216,    9216,   1500,   9216 },
        { _OFFSET(mtu[LM_CLI_IDX_ISCSI]),  9216,    9216,    9216,   1500,   9216 },
        { _OFFSET(mtu[LM_CLI_IDX_FCOE]),  9216,    9216,    9216,   1500,   9216 },
        { _OFFSET(mtu[LM_CLI_IDX_RDMA]),  LM_MTU_INVALID_VALUE,    LM_MTU_INVALID_VALUE,    LM_MTU_INVALID_VALUE,   LM_MTU_INVALID_VALUE,   LM_MTU_INVALID_VALUE },
        { _OFFSET(mtu[LM_CLI_IDX_OOO]),  9216,    9216,    9216,   1500,   9216 },
        { _OFFSET(mtu[LM_CLI_IDX_FWD]),  9216,    9216,    9216,   1500,   9216 },
        { _OFFSET(mtu_max),  9216,    9216,    9216,   1500,   9216 },
        { _OFFSET(rcv_buffer_offset),      0,       0,       0,      0,   9000 },
        { _OFFSET(l2_rx_desc_cnt[LM_CLI_IDX_NDIS]),      200,     200,     200,    0,      32767 },
        { _OFFSET(l2_rx_desc_cnt[LM_CLI_IDX_FCOE]),      200,     200,     200,    0,      32767 },
        { _OFFSET(l2_rx_desc_cnt[LM_CLI_IDX_OOO]),       500,     500,     500,    0,      32767 },
        /* The maximum page count is chosen to prevent us from having
        * more than 32767 pending entries at any one time. */
        { _OFFSET(l2_tx_bd_page_cnt[LM_CLI_IDX_NDIS]),   2,       2,       2,      1,      127 },
        { _OFFSET(l2_tx_bd_page_cnt[LM_CLI_IDX_FCOE]),   2,       2,       2,      1,      127 },
        { _OFFSET(l2_tx_coal_buf_cnt[LM_CLI_IDX_NDIS]),  0,       0,       0,      0,      20 },
        { _OFFSET(l2_tx_coal_buf_cnt[LM_CLI_IDX_FCOE]),  0,       0,       0,      0,      20 },
        { _OFFSET(l2_tx_bd_page_cnt[LM_CLI_IDX_FWD]) ,   2,       2,       2,      1,      127 },
        /* NirV: still not supported in ediag, being set in the windows mm */
//        { _OFFSET(l2_rx_desc_cnt[LM_CLI_IDX_ISCSI]),      200,     200,     200,    0,      32767 },
//
//        /* The maximum page count is chosen to prevent us from having
//        * more than 32767 pending entries at any one time. */
//        { _OFFSET(l2_tx_bd_page_cnt[LM_CLI_IDX_ISCSI]),   2,       2,       2,      1,      127 },
//        { _OFFSET(l2_tx_coal_buf_cnt[LM_CLI_IDX_ISCSI]),  0,       0,       0,      0,      20 },
//        { _OFFSET(l2_rx_bd_page_cnt[LM_CLI_IDX_ISCSI]),   1,       1,       1,      1,      127 },
        { _OFFSET(test_mode),              0,       0,       0,      0,      0 },
        { _OFFSET(ofld_cap),               0,       0,       0,      0,      0 },
        { _OFFSET(wol_cap),                0,       0,       0,      0,      0 },
        { _OFFSET(flow_ctrl_cap),          0,       0,       0,      0,      0x80000000 },
        { _OFFSET(req_medium),             0xff00,  0x00ff,  0x00ff, 0,   0xfffff },
        { _OFFSET(interrupt_mode),         LM_INT_MODE_INTA, LM_INT_MODE_INTA, LM_INT_MODE_INTA, LM_INT_MODE_INTA, LM_INT_MODE_MIMD},
        { _OFFSET(igu_access_mode),        INTR_BLK_ACCESS_IGUMEM, INTR_BLK_ACCESS_IGUMEM, INTR_BLK_ACCESS_IGUMEM, INTR_BLK_ACCESS_GRC, INTR_BLK_ACCESS_IGUMEM},
        { _OFFSET(sw_config),              4,       4,       4,      0,      4},
        { _OFFSET(selective_autoneg),      0,       0,       0,      0,      0 },
        { _OFFSET(wire_speed),             1,       0,       0,      0,      0 },
        { _OFFSET(phy_int_mode),           2,       2,       2,      0,      0 },
        { _OFFSET(link_chng_mode),         2,       2,       2,      0,      0 },
        // TODO add correct values here
        { _OFFSET(max_func_connections),   1024,    1024,    1024,   0,      500000},
#ifdef VF_INVOLVED
        { _OFFSET(max_func_toe_cons),      650,     650,     650,    0,      500000},
#else
        { _OFFSET(max_func_toe_cons),      750,     750,     750,    0,      500000},
#endif
        { _OFFSET(max_func_rdma_cons),     10,       10,      10,    0,      500000},
        { _OFFSET(max_func_iscsi_cons),    128,     128,     128,    0,      500000},
        { _OFFSET(max_func_fcoe_cons),     64,      64,      20,     0,      500000},
        { _OFFSET(context_line_size),      LM_CONTEXT_SIZE,    LM_CONTEXT_SIZE,    LM_CONTEXT_SIZE,   0,      LM_CONTEXT_SIZE },
        { _OFFSET(context_waste_size),     0,       0,       0,      0,      1024 },
        { _OFFSET(num_context_in_page),    4,       4,       4,      0,       128},
        { _OFFSET(client_page_size),       0x1000, 0x1000, 0x1000,0x1000, 0x20000 },
        { _OFFSET(elt_page_size),          0x1000, 0x1000, 0x1000,0x1000, 0x20000 },
        { _OFFSET(ilt_client_page_size),   0x1000, 0x1000, 0x1000,0x1000, 0x20000 },
        { _OFFSET(cfc_last_lcid),          0xff,   0xff,   0xff,    0x1,     0xff },
        { _OFFSET(override_rss_chain_cnt), 0,      0,      0,       0,       16 },
        // network type and max cwnd
        { _OFFSET(network_type),   LM_NETOWRK_TYPE_WAN, LM_NETOWRK_TYPE_WAN, LM_NETOWRK_TYPE_WAN,LM_NETOWRK_TYPE_LAN, LM_NETOWRK_TYPE_WAN },
        { _OFFSET(max_cwnd_wan),   12500000, 12500000, 12500000,12500000, 12500000 },
        { _OFFSET(max_cwnd_lan),   1250000 , 1250000,  1250000, 1250000,  1250000 },
        // cid allocation mode
        { _OFFSET(cid_allocation_mode),     LM_CID_ALLOC_DELAY , LM_CID_ALLOC_DELAY, LM_CID_ALLOC_DELAY,LM_CID_ALLOC_DELAY, LM_CID_ALLOC_NUM_MODES},
        // interrupt coalesing configuration
        { _OFFSET(int_coalesing_mode),      LM_INT_COAL_PERIODIC_SYNC, LM_INT_COAL_NONE, LM_INT_COAL_NONE, 1, LM_INT_COAL_NUM_MODES },
        { _OFFSET(int_per_sec_rx[0]),          5000,    5000,    5000,  1,      200000 },
        { _OFFSET(int_per_sec_rx[1]),          5000,    5000,    5000,  1,      200000 },
        { _OFFSET(int_per_sec_rx[2]),          5000,    5000,    5000,  1,      200000 },
        { _OFFSET(int_per_sec_rx[3]),          5000,    5000,    5000,  1,      200000 },
        { _OFFSET(int_per_sec_tx[0]),          7500,    7500,    7500,  1,      200000 },
        { _OFFSET(int_per_sec_tx[1]),          3800,    3800,    3800,  1,      200000 },
        { _OFFSET(int_per_sec_tx[2]),          3800,    3800,    3800,  1,      200000 },
        { _OFFSET(int_per_sec_tx[3]),          3800,    3800,    3800,  1,      200000 },
        { _OFFSET(enable_dynamic_hc[0]),    1,       1,       1,     0,      1 },
        { _OFFSET(enable_dynamic_hc[1]),    1,       1,       1,     0,      1 },
        { _OFFSET(enable_dynamic_hc[2]),    1,       1,       1,     0,      1 },
        { _OFFSET(enable_dynamic_hc[3]),    0,       0,       0,     0,      1 },
        { _OFFSET(hc_timeout0[SM_RX_ID][0]),       12,      12,      12,    1,      0xff },   /* (20K int/sec assuming no more btr) */
        { _OFFSET(hc_timeout1[SM_RX_ID][0]),       48,      48,      48,    1,      0xff },   /* (5K int/sec assuming no more btr) */
        { _OFFSET(hc_timeout2[SM_RX_ID][0]),       48,      48,      48,    1,      0xff },   /* (5K int/sec assuming no more btr) */
        { _OFFSET(hc_timeout3[SM_RX_ID][0]),       48,      48,      48,    1,      0xff },   /* (5K int/sec assuming no more btr) */
        { _OFFSET(hc_timeout0[SM_RX_ID][1]),       6,       6,       6,     1,      0xff },   /* (40K int/sec assuming no more btr) */
        { _OFFSET(hc_timeout1[SM_RX_ID][1]),       48,      48,      48,    1,      0xff },   /* (5K int/sec assuming no more btr) */
        { _OFFSET(hc_timeout2[SM_RX_ID][1]),       120,    120,     120,    1,      0xff },   /* (2K int/sec assuming no more btr) */
        { _OFFSET(hc_timeout3[SM_RX_ID][1]),       240,    240,     240,    1,      0xff },   /* (1K int/sec assuming no more btr) */
        { _OFFSET(hc_timeout0[SM_RX_ID][2]),       6,       6,       6,     1,      0xff },   /* (40K int/sec assuming no more btr) */
        { _OFFSET(hc_timeout1[SM_RX_ID][2]),       48,      48,      48,    1,      0xff },   /* (5K int/sec assuming no more btr) */
        { _OFFSET(hc_timeout2[SM_RX_ID][2]),       120,    120,     120,    1,      0xff },   /* (2K int/sec assuming no more btr) */
        { _OFFSET(hc_timeout3[SM_RX_ID][2]),       240,    240,     240,    1,      0xff },   /* (1K int/sec assuming no more btr) */
        { _OFFSET(hc_timeout0[SM_RX_ID][3]),       6,       6,       6,     1,      0xff },   /* (40K int/sec assuming no more btr) */
        { _OFFSET(hc_timeout1[SM_RX_ID][3]),       48,      48,      48,    1,      0xff },   /* (5K int/sec assuming no more btr) */
        { _OFFSET(hc_timeout2[SM_RX_ID][3]),       120,    120,     120,    1,      0xff },   /* (2K int/sec assuming no more btr) */
        { _OFFSET(hc_timeout3[SM_RX_ID][3]),       240,    240,     240,    1,      0xff },   /* (1K int/sec assuming no more btr) */

        { _OFFSET(hc_timeout0[SM_TX_ID][0]),       12,      12,      12,    1,      0xff },   /* (20K int/sec assuming no more btr) */
        { _OFFSET(hc_timeout1[SM_TX_ID][0]),       48,      48,      48,    1,      0xff },   /* (5K int/sec assuming no more btr) */
        { _OFFSET(hc_timeout2[SM_TX_ID][0]),       48,      48,      48,    1,      0xff },   /* (5K int/sec assuming no more btr) */
        { _OFFSET(hc_timeout3[SM_TX_ID][0]),       48,      48,      48,    1,      0xff },   /* (5K int/sec assuming no more btr) */
        { _OFFSET(hc_timeout0[SM_TX_ID][1]),       6,       6,       6,     1,      0xff },   /* (40K int/sec assuming no more btr) */
        { _OFFSET(hc_timeout1[SM_TX_ID][1]),       48,      48,      48,    1,      0xff },   /* (5K int/sec assuming no more btr) */
        { _OFFSET(hc_timeout2[SM_TX_ID][1]),       120,    120,     120,    1,      0xff },   /* (2K int/sec assuming no more btr) */
        { _OFFSET(hc_timeout3[SM_TX_ID][1]),       240,    240,     240,    1,      0xff },   /* (1K int/sec assuming no more btr) */
        { _OFFSET(hc_timeout0[SM_TX_ID][2]),        6,       6,       6,    1,      0xff },   /* (40K int/sec assuming no more btr) */
        { _OFFSET(hc_timeout1[SM_TX_ID][2]),       12,      12,      12,    1,      0xff },   /* (20K int/sec assuming no more btr) */
        { _OFFSET(hc_timeout2[SM_TX_ID][2]),       48,      48,      48,    1,      0xff },   /* (5K int/sec assuming no more btr) */
        { _OFFSET(hc_timeout3[SM_TX_ID][2]),       64,      64,      64,    1,      0xff },   /* (3.75K int/sec assuming no more btr) */
        { _OFFSET(hc_timeout0[SM_TX_ID][3]),       6,       6,       6,     1,      0xff },   /* (40K int/sec assuming no more btr) */
        { _OFFSET(hc_timeout1[SM_TX_ID][3]),       48,      48,      48,    1,      0xff },   /* (5K int/sec assuming no more btr) */
        { _OFFSET(hc_timeout2[SM_TX_ID][3]),       120,    120,     120,    1,      0xff },   /* (2K int/sec assuming no more btr) */
        { _OFFSET(hc_timeout3[SM_TX_ID][3]),       240,    240,     240,    1,      0xff },   /* (1K int/sec assuming no more btr) */

        { _OFFSET(hc_threshold0[SM_RX_ID]),        0x2000,  0x2000,  0x2000,1,     0xffffffff },
        { _OFFSET(hc_threshold1[SM_RX_ID]),        0x10000, 0x10000, 0x10000,1,     0xffffffff },
        { _OFFSET(hc_threshold2[SM_RX_ID]),        0x50000, 0x50000, 0x50000,1,     0xffffffff },

        { _OFFSET(hc_threshold0[SM_TX_ID]),        0x2000,  0x2000,  0x2000,1,     0xffffffff },
        { _OFFSET(hc_threshold1[SM_TX_ID]),        0x10000, 0x10000, 0x10000,1,     0xffffffff },
        { _OFFSET(hc_threshold2[SM_TX_ID]),        0x20000, 0x20000, 0x20000,1,     0xffffffff },

        { _OFFSET(l2_dynamic_hc_min_bytes_per_packet),        0,      0,        0,        0,     0xffff },
//        { _OFFSET(l4_hc_scaling_factor),     12,      12,        12,      0,    16 },
        { _OFFSET(l4_hc_ustorm_thresh),     12,     12,       12,     12,     0xffffffff },  /* 128K */
        // l4 params
        { _OFFSET(l4_scq_page_cnt),         2,       2,       2,     2,      127 }, /* 321 BDs are reserved to FW threshold :-( */
        { _OFFSET(l4_rcq_page_cnt),         3,       3,       3,     3,      127 }, /* 398 BDs are reserved to FW threshold :-(  CQ_XOFF_TH = ((65*6) +  8) = ((maximum pending incoming msgs) * (maximum completions) + (maximum ramrods)) */
        { _OFFSET(l4_grq_page_cnt),         2,       2,       2,     2,      127 }, /* 65  BDs are reserved to FW threshold :-( */
        { _OFFSET(l4_tx_chain_page_cnt),    2,       2,       2,     2,      127 },
        { _OFFSET(l4_rx_chain_page_cnt),    2,       2,       2,     2,      127 },
        { _OFFSET(l4_gen_buf_size),         LM_PAGE_SIZE,LM_PAGE_SIZE,LM_PAGE_SIZE,LM_PAGE_SIZE,16*LM_PAGE_SIZE },
        { _OFFSET(l4_history_cqe_cnt),      20,      20,      20,    1,      20   },
        { _OFFSET(l4_ignore_grq_push_enabled), 0,       0,       0,     0,      1   },
        { _OFFSET(l4cli_flags),             0,       0,       0,     0,      1 },
        { _OFFSET(l4cli_ticks_per_second),  1000,    1000,    1000,  500,    10000 },
        { _OFFSET(l4cli_ack_frequency),     2,       2,       2,     1,      255 }, /* default 2 segments */
        { _OFFSET(l4cli_delayed_ack_ticks), 200,     200,     200,   1,      255 }, /* default 200ms */
        { _OFFSET(l4cli_max_retx),          6,       6,       6,     1,      255 },
        { _OFFSET(l4cli_doubt_reachability_retx),3,  3,       3,     1,      255 },
        { _OFFSET(l4cli_sws_prevention_ticks), 1000, 1000,    1000,  200,    0xffffffff }, /* default 1s */
        { _OFFSET(l4cli_dup_ack_threshold), 3,       3,       3,     1,      255 },
        { _OFFSET(l4cli_push_ticks),        100,     100,     100,   1,      0xffffffff }, /* default 100ms */
        { _OFFSET(l4cli_nce_stale_ticks),   0xffffff,0xffffff,0xffffff, 1,   0xffffffff },
        { _OFFSET(l4cli_starting_ip_id),    0,       0,       0,     0,      0xffff },
        { _OFFSET(keep_vlan_tag),           1 ,      1,       1,     0,      1 },
        //congestion managment parameters
        { _OFFSET(cmng_enable),             0,       0,       0,     0,      1},
        { _OFFSET(cmng_rate_shaping_enable),1,       1,       1,     0,      1},
        { _OFFSET(cmng_fairness_enable),    1,       1,       1,     0,      1},
        // safc
        { _OFFSET(cmng_safc_rate_thresh),   3,       3,       3,     0,      10},
        { _OFFSET(cmng_activate_safc),      0,       0,       0,     0,      1},
        // fairness
        { _OFFSET(cmng_fair_port0_rate),    10,      10,      10,    1,      10},
        { _OFFSET(cmng_eth_weight),         8,       8,       8,     0,      10},
        { _OFFSET(cmng_toe_weight),         8,       8,       8,     0,      10},
        { _OFFSET(cmng_rdma_weight),        8,       8,       8,     0,      10},
        { _OFFSET(cmng_iscsi_weight),       8,       8,       8,     0,      10},
        // rate shaping
        { _OFFSET(cmng_eth_rate),           10,      10,      10,    0,      10},
        { _OFFSET(cmng_toe_rate),           10,      10,      10,    0,      10},
        { _OFFSET(cmng_rdma_rate),          2,       2,       2,     0,      10},
        { _OFFSET(cmng_iscsi_rate),         4,       2,       2,     0,      10},
        // Demo will be removed later
        { _OFFSET(cmng_toe_con_number),     20,      20,      20,    0,      1024},
        { _OFFSET(cmng_rdma_con_number),    2,       2,       2,     0,      1024},
        { _OFFSET(cmng_iscsi_con_number),   40,      40,      40,    0,      1024},
        // iscsi
        { _OFFSET(l5sc_max_pending_tasks),      64,          64,      64,    64,     2048},
        // fcoe
        { _OFFSET(max_fcoe_task),           64,      64,      64,    0,     4096},
#if 0
        { _OFFSET(disable_patent_using),        1,       1,       1,     0,      1},
#else
        { _OFFSET(disable_patent_using),        0,       0,       0,     0,      1},
#endif
        { _OFFSET(l4_grq_filling_threshold_divider),    64,       64,       64,     2,      2048},
        { _OFFSET(l4_free_cid_delay_time),  2000,   10000,      10000,  0,  10000},
        { _OFFSET(preemphasis_enable),      0,       0,       0,     0,      1},
        { _OFFSET(preemphasis_rx_0),        0,       0,       0,     0,      0xffff},
        { _OFFSET(preemphasis_rx_1),        0,       0,       0,     0,      0xffff},
        { _OFFSET(preemphasis_rx_2),        0,       0,       0,     0,      0xffff},
        { _OFFSET(preemphasis_rx_3),        0,       0,       0,     0,      0xffff},
        { _OFFSET(preemphasis_tx_0),        0,       0,       0,     0,      0xffff},
        { _OFFSET(preemphasis_tx_1),        0,       0,       0,     0,      0xffff},
        { _OFFSET(preemphasis_tx_2),        0,       0,       0,     0,      0xffff},
        { _OFFSET(preemphasis_tx_3),        0,       0,       0,     0,      0xffff},
        { _OFFSET(disable_pcie_nfr),        0,       0,       0,     0,      1},
        { _OFFSET(debug_cap_flags),  0xffffffff,       0xffffffff,       0xffffffff,     0,      0xffffffff},
        { _OFFSET(try_not_align_page_multiplied_memory),     1,       1,       1,     0,      1},
        { _OFFSET(e2_integ_testing_enabled),0,       0,       0,     0,      1},
        { _OFFSET(e2_integ_def_cos_setting_0),0,    0,  0,  0,  100},
        { _OFFSET(e2_integ_def_pbfq_setting_0),0,   0,  0,  0,  100},
        { _OFFSET(e2_integ_def_voq_setting_0),0,    0,  0,  0,  100},
        { _OFFSET(e2_integ_def_priority_setting_0),0,   0,  0,  0,  100},
        { _OFFSET(e2_integ_def_flags_0),0,   0,  0,  0,  100},
        { _OFFSET(e2_integ_def_cos_setting_1),0,    0,  0,  0,  100},
        { _OFFSET(e2_integ_def_pbfq_setting_1),0,   0,  0,  0,  100},
        { _OFFSET(e2_integ_def_voq_setting_1),0,    0,  0,  0,  100},
        { _OFFSET(e2_integ_def_priority_setting_1),0,   0,  0,  0,  100},
        { _OFFSET(e2_integ_def_flags_1),0,   0,  0,  0,  100},
        { _OFFSET(e2_integ_vfc_test_en),    0,       0,       0,     0,      1},
        { _OFFSET(l4_limit_isles),          0,       0,       0,     0,      1},
        { _OFFSET(l4_max_rcv_wnd_size),     0x100000,0x100000,0x100000, 0,      0x1000000},
        { _OFFSET(ndsb_type),               1,       1,       1,     0,      2},
        { _OFFSET(l4_dominance_threshold),  10,      10,      10,     0,      0xFF},
        { _OFFSET(l4_max_dominance_value),  20,     20,      20,     0,      0xFF},
        { _OFFSET(l4_data_integrity),       0x0,     0x0,     0x0,   0x0,      0x3},
        { _OFFSET(l4_start_port),           5001,  5001,      5001,  0,      0xFFFFFFFF},
        { _OFFSET(l4_num_of_ports),         50,      50,      50,     0,      0xFFFF},
        { _OFFSET(l4_skip_start_bytes),     4,       4,       4,     0,      0xFFFFFFFF},
        { _OFFSET(phy_priority_mode),       PHY_PRIORITY_MODE_HW_DEF, PHY_PRIORITY_MODE_HW_DEF, PHY_PRIORITY_MODE_HW_DEF, PHY_PRIORITY_MODE_HW_DEF, PHY_PRIORITY_MODE_HW_PIN},
        { _OFFSET(grc_timeout_max_ignore),  0,       0,       0,     0,      0xFFFFFFFF},
        { _OFFSET(enable_error_recovery),   0,       0,       0,     0,      1},
        { _OFFSET(validate_sq_complete),    0,       0,       0,     0,      1},
        { _OFFSET(npar_vm_switching_enable),0,       0,       0,     0,      1},
        { 0,                                0,       0,       0,     0,      0}
    }; // param_list

    param_entry_t *param        = NULL;
    size_t         csize        = 0;
    u32_t          flow_control = 0;
    u8_t           i            = 0;

    DbgMessage(pdev, INFORMi , "### lm_init_param\n");
    if (!validate)
    {
        /* Initialize the default parameters. */
        param = param_list;
        while(param->offset)
        {
            switch (CHIP_REV(pdev))
            {
            case CHIP_REV_FPGA:
                SET_PARAM_VAL(pdev, param, param->fpga_default);
                break;
            case CHIP_REV_EMUL:
                SET_PARAM_VAL(pdev, param, param->emulation_default);
                break;
            default:
                SET_PARAM_VAL(pdev, param, param->asic_default);
                break;
            }
            param++;
        }
        pdev->params.rss_caps = (LM_RSS_CAP_IPV4 | LM_RSS_CAP_IPV6);
        pdev->params.rss_chain_cnt = 1;
        pdev->params.tss_chain_cnt = 1;
        if (IS_PFDEV(pdev))
        {
            pdev->params.sb_cnt = MAX_RSS_CHAINS / pdev->params.vnics_per_port;
            /* base non-default status block idx - 0 in E1. 0, 4, 8 or 12 in E1H */
            if (CHIP_IS_E1x(pdev))
            {
                pdev->params.base_fw_ndsb = FUNC_ID(pdev) * pdev->params.sb_cnt;
            }
            else
            {
                if (CHIP_PORT_MODE(pdev) == LM_CHIP_PORT_MODE_4)
                {
                    pdev->params.base_fw_ndsb = FUNC_ID(pdev) * pdev->params.sb_cnt;
                }
                else
                {
                    pdev->params.base_fw_ndsb = VNIC_ID(pdev) * pdev->params.sb_cnt;
                }



            }
            pdev->params.base_fw_client_id = VNIC_ID(pdev) * (pdev->params.sb_cnt + MAX_NON_RSS_FW_CLIENTS);
            /* For now, base_fw_qzone_id == base_fw_client_id, but this doesn't have to be the case... */
            /* qzone-id is relevant only for E2 and therefore it is ok that we use a */
            /* Todo - change once E2 client is added. */
            pdev->params.base_fw_qzone_id = pdev->params.base_fw_client_id + ETH_MAX_RX_CLIENTS_E1H*PORT_ID(pdev);
            /* E2 TODO: read how many sb each pf has...?? */
        }
        else
        {
            pdev->params.sb_cnt = 1;
        }
        pdev->params.max_rss_chains = ((IS_PFDEV(pdev) && IGU_U_NDSB_OFFSET(pdev)) ? min(IGU_U_NDSB_OFFSET(pdev),LM_SB_CNT(pdev)) : LM_SB_CNT(pdev));
        pdev->params.base_cam_offset = 0;
        /* set the clients cids that will be used by the driver */
        pdev->params.map_client_to_cid[LM_CLI_IDX_NDIS]  = 0;
        pdev->params.map_client_to_cid[LM_CLI_IDX_ISCSI] = i = LM_MAX_RSS_CHAINS(pdev);
        pdev->params.map_client_to_cid[LM_CLI_IDX_OOO]   = ++i;
        pdev->params.map_client_to_cid[LM_CLI_IDX_FCOE]  = ++i;
        pdev->params.map_client_to_cid[LM_CLI_IDX_FWD]   = ++i;
        pdev->params.map_client_to_cid[LM_CLI_IDX_RDMA]  = ++i;
        // FCoE is not supported in E1 and we have only 18 clients in E1
        // so we OOO client gets 'priority' over FCoE
        DbgBreakIf(pdev->params.map_client_to_cid[LM_CLI_IDX_OOO] > pdev->params.map_client_to_cid[LM_CLI_IDX_FCOE]);

        /* L4 RSS */
        pdev->params.l4_rss_chain_cnt = 1;
        pdev->params.l4_tss_chain_cnt = 1;
        /* set l4_rss base chain index to be the first one after l2 */
        pdev->params.l4_rss_base_chain_idx = 0;
        pdev->params.l4_base_fw_rss_id = VNIC_ID(pdev) * pdev->params.sb_cnt ;
        /* master-pfdev needs to keep resources for its vfs, resource allocation is done first between
         * pfs and then each pf leaves itself 1 sb_cnt for enabling vfs. */
        if (IS_BASIC_VIRT_MODE_MASTER_PFDEV(pdev)) {
            pdev->params.sb_cnt = 1;
        }
        pdev->params.eth_align_enable = 0;
        lm_init_cam_params(pdev);

        if(((CHIP_REV(pdev) == CHIP_REV_EMUL) || (CHIP_REV(pdev) == CHIP_REV_FPGA) || IS_VFDEV(pdev)) &&
           (!(GET_FLAGS(pdev->hw_info.mf_info.flags,MF_INFO_VALID_MAC))))
        {
            pdev->params.mac_addr[0] = pdev->hw_info.mac_addr[0] = 0x00;
            pdev->params.mac_addr[1] = pdev->hw_info.mac_addr[1] = 0x50;
            pdev->params.mac_addr[2] = pdev->hw_info.mac_addr[2] = 0xc2;
            pdev->params.mac_addr[3] = pdev->hw_info.mac_addr[3] = 0x2c;
            pdev->params.mac_addr[4] = pdev->hw_info.mac_addr[4] = 0x70 + (IS_PFDEV(pdev) ? 0 : (1 + 64*PATH_ID(pdev) + ABS_VFID(pdev)));
            if (CHIP_IS_E1x(pdev))
            {
                pdev->params.mac_addr[5] = pdev->hw_info.mac_addr[5] = 0x9a + 2 * FUNC_ID(pdev);
            }
            else
            {
                pdev->params.mac_addr[5] = pdev->hw_info.mac_addr[5] = 0x9a + PATH_ID(pdev)*8 + PORT_ID(pdev)*4 + VNIC_ID(pdev)*2;
            }
            
            mm_memcpy(pdev->hw_info.iscsi_mac_addr, pdev->hw_info.mac_addr, 6);
            pdev->hw_info.iscsi_mac_addr[5]++;
            mm_memcpy(pdev->hw_info.fcoe_mac_addr, pdev->hw_info.iscsi_mac_addr, 6);
            pdev->hw_info.fcoe_mac_addr[5]++;
            lm_fcoe_set_default_wwns(pdev);            
        }
        else
        {
            pdev->params.mac_addr[0] = pdev->hw_info.mac_addr[0];
            pdev->params.mac_addr[1] = pdev->hw_info.mac_addr[1];
            pdev->params.mac_addr[2] = pdev->hw_info.mac_addr[2];
            pdev->params.mac_addr[3] = pdev->hw_info.mac_addr[3];
            pdev->params.mac_addr[4] = pdev->hw_info.mac_addr[4];
            pdev->params.mac_addr[5] = pdev->hw_info.mac_addr[5];
        }
        if(CHIP_REV(pdev) == CHIP_REV_EMUL)
        {
            DbgMessage(pdev, INFORMi, "Emulation is detected.\n");
            pdev->params.test_mode |= TEST_MODE_IGNORE_SHMEM_SIGNATURE;
            pdev->params.test_mode |= TEST_MODE_LOG_REG_ACCESS;
            //pdev->params.test_mode |= TEST_MODE_NO_MCP;
            DbgMessage1(pdev, INFORMi , "test mode is 0x%x \n",pdev->params.test_mode);
        }
        else
        {
            DbgMessage(pdev, INFORMi, "ASIC is detected.\n");
        }
        if (!pdev->hw_info.mcp_detected)
        {
            pdev->params.test_mode |= TEST_MODE_NO_MCP;
        }
        flow_control = (pdev->hw_info.link_config[ELINK_INT_PHY] & PORT_FEATURE_FLOW_CONTROL_MASK);

        switch (flow_control)
        {
        case PORT_FEATURE_FLOW_CONTROL_AUTO:
            pdev->params.flow_ctrl_cap = LM_FLOW_CONTROL_AUTO_PAUSE;
        break;
        case PORT_FEATURE_FLOW_CONTROL_TX:
            pdev->params.flow_ctrl_cap = LM_FLOW_CONTROL_TRANSMIT_PAUSE;
        break;
        case PORT_FEATURE_FLOW_CONTROL_RX:
            pdev->params.flow_ctrl_cap = LM_FLOW_CONTROL_RECEIVE_PAUSE;
        break;
        case PORT_FEATURE_FLOW_CONTROL_BOTH:
            pdev->params.flow_ctrl_cap = LM_FLOW_CONTROL_TRANSMIT_PAUSE | LM_FLOW_CONTROL_RECEIVE_PAUSE;
        break;
        case PORT_FEATURE_FLOW_CONTROL_NONE:
            pdev->params.flow_ctrl_cap = LM_FLOW_CONTROL_NONE;
        break;
        default:
            pdev->params.flow_ctrl_cap = LM_FLOW_CONTROL_NONE;
        break;
        }
        /* L2 FW Flow control */
        pdev->params.l2_fw_flow_ctrl = 0;
        pdev->params.l4_fw_flow_ctrl = 0;
        pdev->params.fw_stats_init_value = TRUE;

        pdev->params.mf_mode = pdev->hw_info.mf_info.mf_mode;
    }
    else
    {
        /* Make sure the parameter values are within range. */
        param = param_list;
        while(param->offset)
        {
            if(param->min != 0 || param->max != 0)
            {
                if(PARAM_VAL(pdev, param) < param->min ||
                    PARAM_VAL(pdev, param) > param->max)
                {
                    switch (CHIP_REV(pdev))
                    {
                    case CHIP_REV_FPGA:
                        SET_PARAM_VAL(pdev, param, param->fpga_default);
                        break;
                    case CHIP_REV_EMUL:
                        SET_PARAM_VAL(pdev, param, param->emulation_default);
                        break;
                    default:
                        SET_PARAM_VAL(pdev, param, param->asic_default);
                        break;
                    }
                }
            }
            param++;
        }
        /* calculate context_line_size context_waste_size */
        // TODO calculate number of context lines in alocation page.
            csize = max(sizeof(struct eth_context),sizeof(struct toe_context));
            //csize = max(sizeof(struct rdma_context),csize);
            csize = max(sizeof(struct iscsi_context),csize);
            DbgBreakIf(csize>1024);
        /* Check for a valid mac address. */
        if((pdev->params.mac_addr[0] == 0 &&
            pdev->params.mac_addr[1] == 0 &&
            pdev->params.mac_addr[2] == 0 &&
            pdev->params.mac_addr[3] == 0 &&
            pdev->params.mac_addr[4] == 0 &&
            pdev->params.mac_addr[5] == 0) || (pdev->params.mac_addr[0] & 1))
        {
            DbgMessage(pdev, WARNi, "invalid MAC number.\n");
            pdev->params.mac_addr[0] = pdev->hw_info.mac_addr[0];
            pdev->params.mac_addr[1] = pdev->hw_info.mac_addr[1];
            pdev->params.mac_addr[2] = pdev->hw_info.mac_addr[2];
            pdev->params.mac_addr[3] = pdev->hw_info.mac_addr[3];
            pdev->params.mac_addr[4] = pdev->hw_info.mac_addr[4];
            pdev->params.mac_addr[5] = pdev->hw_info.mac_addr[5];
        }
        if (CHIP_IS_E1(pdev))
        {
            if ((pdev->params.l2_fw_flow_ctrl == 1) || (pdev->params.l4_fw_flow_ctrl == 1))
            {
                DbgMessage(pdev, WARNi, "L2 FW Flow control not supported on E1\n");
                pdev->params.l2_fw_flow_ctrl = 0;
                pdev->params.l4_fw_flow_ctrl = 0;
            }
        }
    }

    /* init l2 client conn param with default mtu values */
    for (i = 0; i < ARRSIZE(pdev->params.l2_cli_con_params); i++)
    {
        lm_cli_idx_t lm_cli_idx = LM_CHAIN_IDX_CLI(pdev, i);

        if( lm_cli_idx >= ARRSIZE(pdev->params.mtu) ||
            lm_cli_idx >= ARRSIZE(pdev->params.l2_rx_desc_cnt) )
        {
            // in case lm_cli_idx is above boundries
            // it means that is should not be used (currently expected in MF mode)
            // we skip the iteration
            continue;
        }
        pdev->params.l2_cli_con_params[i].mtu         = pdev->params.mtu[lm_cli_idx];
        pdev->params.l2_cli_con_params[i].num_rx_desc = pdev->params.l2_rx_desc_cnt[lm_cli_idx];
        pdev->params.l2_cli_con_params[i].attributes        = LM_CLIENT_ATTRIBUTES_RX | LM_CLIENT_ATTRIBUTES_TX;
    }
    return LM_STATUS_SUCCESS;
} /* lm_init_params */

static void init_link_params(lm_device_t *pdev)
{
    u32_t val              = 0;
    u32_t feat_val         = 0;
    pdev->params.link.port = PORT_ID(pdev);

    if (pdev->hw_info.mcp_detected)
    {
        pdev->params.link.shmem_base = pdev->hw_info.shmem_base;
        pdev->params.link.shmem2_base= pdev->hw_info.shmem_base2;
    }
    else
    {
        pdev->params.link.shmem_base = NO_MCP_WA_CLC_SHMEM;
        pdev->params.link.shmem2_base= NO_MCP_WA_CLC_SHMEM;
    }
    pdev->params.link.chip_id = pdev->hw_info.chip_id;
    pdev->params.link.cb      = pdev;

    ///TODO remove - the initialization in lm_mcp_cmd_init should be enough, but BC versions are still in flux.
    if(pdev->hw_info.mf_info.mf_mode == MULTI_FUNCTION_NIV) //we can't use IS_MF_NIV_MODE because params.mf_mode is not initalized yet.
    {
        pdev->params.link.feature_config_flags |= ELINK_FEATURE_CONFIG_BC_SUPPORTS_VNTAG;
    }

    if ((CHIP_REV(pdev) == CHIP_REV_EMUL) || (CHIP_REV(pdev) == CHIP_REV_FPGA))
    {
        val = CHIP_BONDING(pdev);
        DbgMessage1(pdev,WARN,"init_link_params: chip bond id is 0x%x\n",val);

        if (pdev->hw_info.chip_port_mode == LM_CHIP_PORT_MODE_4)
        {
            feat_val |= ELINK_FEATURE_CONFIG_EMUL_DISABLE_BMAC;
        }
        else if (val & 0x4)
        {
            // force to work with emac
            if (CHIP_IS_E3(pdev))
            {
                pdev->params.link.req_line_speed[0] = ELINK_SPEED_1000;
                feat_val |= ELINK_FEATURE_CONFIG_EMUL_DISABLE_XMAC;
            }
            else
            {
                feat_val |= ELINK_FEATURE_CONFIG_EMUL_DISABLE_BMAC;
            }
        }
        else if (val & 0x8)
        {
            if (CHIP_IS_E3(pdev))
            {
                feat_val |= ELINK_FEATURE_CONFIG_EMUL_DISABLE_UMAC;
            }
            else
            {
                feat_val |= ELINK_FEATURE_CONFIG_EMUL_DISABLE_EMAC;
            }
        }
        /* Disable EMAC for E3 and above */
        if (val & 2)
        {
            feat_val |= ELINK_FEATURE_CONFIG_EMUL_DISABLE_EMAC;
        }

        SET_FLAGS(pdev->params.link.feature_config_flags, feat_val);
    }

    pdev->params.phy_hw_lock_required = elink_hw_lock_required(pdev, pdev->hw_info.shmem_base, pdev->hw_info.shmem_base2);
}
/*******************************************************************************
 * Description:
 *
 * Return:
 ******************************************************************************/
lm_status_t
lm_get_function_num(lm_device_t *pdev)
{
    u32_t val = 0;
    /* read the me register to get function number. */
    /* Me register: holds the relative-function num + absolute-function num,
     * absolute-function-num appears only from E2 and above. Before that these bits
     * always contained zero, therefore we can't take as is. */
    val = REG_RD(pdev, BAR_ME_REGISTER);
    pdev->params.pfunc_rel = (u8_t)((val & ME_REG_PF_NUM) >> ME_REG_PF_NUM_SHIFT);
    pdev->params.path_id = (u8_t)((val & ME_REG_ABS_PF_NUM) >> ME_REG_ABS_PF_NUM_SHIFT) & 1;
    if (CHIP_PORT_MODE(pdev) == LM_CHIP_PORT_MODE_4) {
        pdev->params.pfunc_abs = (pdev->params.pfunc_rel << 1) | pdev->params.path_id;
    } else {
        pdev->params.pfunc_abs = pdev->params.pfunc_rel | pdev->params.path_id;
    }
    DbgMessage2(pdev, INFORMi , "relative function %d absolute function %d\n", pdev->params.pfunc_rel, pdev->params.pfunc_abs);
    return LM_STATUS_SUCCESS;
}
/*******************************************************************************
 * Description:
 *
 * Return:
 ******************************************************************************/
void lm_cmng_calc_params(lm_device_t* pdev )
{
    u8_t vnic = 0;
    DbgBreakIf(!IS_MULTI_VNIC(pdev));
    for (vnic = 0; vnic < MAX_VNIC_NUM; vnic++)
    {
        if (GET_FLAGS(pdev->hw_info.mf_info.func_mf_cfg , FUNC_MF_CFG_FUNC_HIDE))
        {
            pdev->params.min_bw[vnic] = 0;
            pdev->params.max_bw[vnic] = 0;
        }
        else
        {
            pdev->params.min_bw[vnic] = pdev->hw_info.mf_info.min_bw[vnic];
            pdev->params.max_bw[vnic] = pdev->hw_info.mf_info.max_bw[vnic];
        }
    }
} /* lm_cmng_calc_params */
lm_status_t
lm_get_sriov_info(lm_device_t *pdev)
{
    lm_status_t rc = LM_STATUS_SUCCESS;
    u32_t val;
    if (!CHIP_IS_E1x(pdev)) {
        /* get bars... */
#ifdef VF_INVOLVED
        rc = mm_get_sriov_info(pdev, &pdev->hw_info.sriov_info);
        if (rc != LM_STATUS_SUCCESS) {
            return rc;
        }
#endif
        /* TODO: Channel-vt for device-type-pf?? */
        lm_set_virt_mode(pdev, DEVICE_TYPE_PF, (pdev->hw_info.sriov_info.total_vfs? VT_BASIC_VF : VT_NONE));
        /* Since registers from 0x000-0x7ff are spilt across functions, each PF will have  the same location for the same 4 bits*/
        val = REG_RD(pdev, PCICFG_OFFSET + GRC_CONFIG_REG_PF_INIT_VF);
        pdev->hw_info.sriov_info.first_vf_in_pf = ((val & GRC_CR_PF_INIT_VF_PF_FIRST_VF_NUM_MASK) * 8) - E2_MAX_NUM_OF_VFS*PATH_ID(pdev);
        DbgMessage1(pdev, WARN, "First VF in PF = %d\n", pdev->hw_info.sriov_info.first_vf_in_pf);
    }
    return rc;
}
#include "57710_int_offsets.h"
#include "57711_int_offsets.h"
#include "57712_int_offsets.h"
void ecore_init_e1_firmware(struct _lm_device_t *pdev);
void ecore_init_e1h_firmware(struct _lm_device_t *pdev);
void ecore_init_e2_firmware(struct _lm_device_t *pdev);
static __inline int
lm_set_init_arrs(lm_device_t *pdev)
{
    u32_t const chip_num = CHIP_NUM(pdev);
    switch(chip_num)
    {
    case CHIP_NUM_5710:
        DbgBreakIf( !CHIP_IS_E1(pdev) );
        ecore_init_e1_firmware(pdev);
        INIT_IRO_ARRAY(pdev) = e1_iro_arr;
        break;
    case CHIP_NUM_5711:
    case CHIP_NUM_5711E:
        DbgBreakIf( !CHIP_IS_E1H(pdev) );
        ecore_init_e1h_firmware(pdev);
        INIT_IRO_ARRAY(pdev) = e1h_iro_arr;
        break;
    case CHIP_NUM_5712:
    case CHIP_NUM_5713:
    case CHIP_NUM_5712E:
    case CHIP_NUM_5713E:
        DbgBreakIf( !CHIP_IS_E2(pdev) );
    case CHIP_NUM_57800:
    case CHIP_NUM_57810:
    case CHIP_NUM_57840:
        DbgBreakIf( !CHIP_IS_E2(pdev) && !CHIP_IS_E3(pdev) );
        ecore_init_e2_firmware(pdev);
        INIT_IRO_ARRAY(pdev) = e2_iro_arr;
        break;
    default:
        DbgMessage1(pdev, FATAL, "chip-id=%x NOT SUPPORTED\n", CHIP_NUM(pdev));
        return -1; /* for now not supported, can't have all three...*/
    }
    return 0;
}
/*******************************************************************************
 * Description:
 *
 * Return:
 ******************************************************************************/
lm_status_t
lm_get_dev_info(
    lm_device_t *pdev)
{
    lm_status_t lm_status = LM_STATUS_SUCCESS;

    DbgMessage(pdev, INFORMi , "### lm_get_dev_info\n");

    lm_status = lm_get_pcicfg_info(pdev);
    if(lm_status != LM_STATUS_SUCCESS)
    {
        return lm_status;
    }
    lm_status = lm_get_bars_info(pdev);
    if(lm_status != LM_STATUS_SUCCESS)
    {
        return lm_status;
    }
    if (lm_is_function_after_flr(pdev))
    {
        lm_status = lm_cleanup_after_flr(pdev);
        if(lm_status != LM_STATUS_SUCCESS)
        {
            return lm_status;
        }
    }
    if (!IS_CHANNEL_VFDEV(pdev)) {
        lm_status = lm_get_chip_id_and_mode(pdev);
        if(lm_status != LM_STATUS_SUCCESS)
        {
            return lm_status;
        }
    }
    if (IS_PFDEV(pdev)) {
        // Get function num using me register
        lm_status = lm_get_function_num(pdev);
        if (lm_status != LM_STATUS_SUCCESS) {
            return lm_status;
        }
        lm_status = lm_get_sriov_info(pdev);
        if (lm_status != LM_STATUS_SUCCESS) {
            return lm_status;
        }
        lm_status = lm_get_nvm_info(pdev);
        if (lm_status != LM_STATUS_SUCCESS) {
            return lm_status;
        }
        lm_status = lm_get_shmem_info(pdev);
        if (lm_status != LM_STATUS_SUCCESS) {
            return lm_status;
        }

        // initialize pointers to init arrays (can only do this after we know which chip we are...)
        // We want to do this here to enable IRO access before driver load (ediag/lediag) this is only done
        // for PFs, VFs use PFDEV to access IRO
        if ( lm_set_init_arrs(pdev) != 0 ) {
            DbgMessage(pdev, FATAL, "Unknown chip revision\n");
            return LM_STATUS_UNKNOWN_ADAPTER;
        }
    } else if (IS_CHANNEL_VFDEV(pdev)) { //TODO check for basic vf
        pdev->hw_info.mf_info.multi_vnics_mode = 0;
        pdev->hw_info.mf_info.vnics_per_port   = 1;
        pdev->hw_info.mf_info.ext_id            = 0xffff; /* invalid ovlan */ /* TBD - E1H: - what is the right value for Cisco? */
        pdev->hw_info.mcp_detected = FALSE;
        pdev->hw_info.chip_id = CHIP_NUM_5712E;
        pdev->hw_info.max_port_conns =  log2_align(MAX_ETH_CONS);
        pdev->debug_info.ack_en[0] = 1;
    }

#ifdef VF_INVOLVED
    if (IS_VFDEV(pdev)) {
        lm_vf_enable_vf(pdev);
    }
#endif
    pdev->ver_num =
        (LM_DRIVER_MAJOR_VER << 24) |
        (LM_DRIVER_MINOR_VER << 16) |
        (LM_DRIVER_FIX_NUM   << 8)  |
         LM_DRIVER_ENG_NUM ;
    build_ver_string(
        (char *)pdev->ver_str,
        sizeof(pdev->ver_str),
        LM_DRIVER_MAJOR_VER,
        LM_DRIVER_MINOR_VER,
        LM_DRIVER_FIX_NUM,
        LM_DRIVER_ENG_NUM);
    // for debugging only (no other use)
    pdev->ver_num_fw = (BCM_5710_FW_MAJOR_VERSION << 24) |
                       (BCM_5710_FW_MINOR_VERSION << 16) |
                       (BCM_5710_FW_REVISION_VERSION<<8) |
                       (BCM_5710_FW_ENGINEERING_VERSION) ;
    /* get vnic parameters */
    pdev->params.vnics_per_port = pdev->hw_info.mf_info.vnics_per_port;
    pdev->params.ovlan = VALID_OVLAN(OVLAN(pdev)) ? OVLAN(pdev) : 0; // TBD: verify it's the right value (with OfirH)
    pdev->params.multi_vnics_mode = pdev->hw_info.mf_info.multi_vnics_mode;
    pdev->params.path_has_ovlan = pdev->hw_info.mf_info.path_has_ovlan;

    if IS_MULTI_VNIC(pdev)
    {
        lm_cmng_calc_params(pdev);
    }
    if (IS_PFDEV(pdev))
    {
        /* Check if we need to reset the device */
        lm_reset_device_if_undi_active(pdev);
        // clc params
        init_link_params(pdev);
    }
    lm_status = lm_init_params(pdev, 0);
    if(lm_status != LM_STATUS_SUCCESS)
    {
        return lm_status;
    }
    if (IS_CHANNEL_VFDEV(pdev))
    {
        pdev->hw_info.intr_blk_info.blk_type = INTR_BLK_IGU;
        pdev->hw_info.intr_blk_info.blk_mode = INTR_BLK_MODE_NORM;
        pdev->hw_info.intr_blk_info.access_type = INTR_BLK_ACCESS_IGUMEM;
    }
    else
    {
        lm_status = lm_get_intr_blk_info(pdev);
        if(lm_status != LM_STATUS_SUCCESS)
        {
            return lm_status;
        }
    }
    lm_status = lm_mcp_cmd_init(pdev);
    if( LM_STATUS_SUCCESS != lm_status )
    {
        // Ediag may want to update the BC version. Don't fail lm_get_dev_info because of lm_mcp_cmd_init
        // in no condition.
        DbgMessage1(pdev,FATAL,"lm_get_shmem_info: mcp_cmd_init failed. lm_status=0x%x\n", lm_status);
    }

    if (CHIP_PORT_MODE(pdev) == LM_CHIP_PORT_MODE_4)
    {
        /* We're a single-function port on a mult-function oath in a 4-port-mode environment... we need to support 1G */
        if (pdev->params.path_has_ovlan && !pdev->params.multi_vnics_mode)
        {
            DbgMessage1(pdev, WARN, "func_id = %d Setting link speed to 1000MBPS\n", ABS_FUNC_ID(pdev));
            SET_MEDIUM_SPEED(pdev->params.req_medium, LM_MEDIUM_SPEED_1000MBPS);
        }
    }


    /* Override the defaults with user configurations. */
    lm_status = mm_get_user_config(pdev);
    if(lm_status != LM_STATUS_SUCCESS)
    {
        return lm_status;
    }
    lm_status = lm_init_params(pdev, 1);
    if(lm_status != LM_STATUS_SUCCESS)
    {
        return lm_status;
    }
    DbgMessage(pdev, INFORMi , "### lm_get_dev_info exit\n");
    return LM_STATUS_SUCCESS;
} /* lm_get_dev_info */


/*******************************************************************************
 * Description:
 *
 * Return:
 ******************************************************************************/
lm_status_t
lm_get_stats( lm_device_t* pdev,
              lm_stats_t   stats_type,
              u64_t*       stats_cnt )
{
    lm_status_t lm_status    = LM_STATUS_SUCCESS;
    lm_u64_t*   stats        = (lm_u64_t *)stats_cnt;
    const u32_t i            = LM_CLI_IDX_NDIS;
    switch(stats_type)
    {
        case LM_STATS_FRAMES_XMITTED_OK:
            stats->as_u64 = pdev->vars.stats.stats_mirror.stats_fw.eth_xstorm_common.client_statistics[i].total_sent_pkts ;
            // ioc IfHCOutPkts
            break;
        case LM_STATS_FRAMES_RECEIVED_OK:
            stats->as_u64 = pdev->vars.stats.stats_mirror.stats_fw.eth_tstorm_common.client_statistics[i].rcv_broadcast_pkts +
                            pdev->vars.stats.stats_mirror.stats_fw.eth_tstorm_common.client_statistics[i].rcv_multicast_pkts +
                            pdev->vars.stats.stats_mirror.stats_fw.eth_tstorm_common.client_statistics[i].rcv_unicast_pkts ;
            stats->as_u64-= pdev->vars.stats.stats_mirror.stats_fw.eth_ustorm_common.client_statistics[i].ucast_no_buff_pkts ;
            stats->as_u64-= pdev->vars.stats.stats_mirror.stats_fw.eth_ustorm_common.client_statistics[i].mcast_no_buff_pkts ;
            stats->as_u64-= pdev->vars.stats.stats_mirror.stats_fw.eth_ustorm_common.client_statistics[i].bcast_no_buff_pkts ;
            // ioc IfHCInPkts
            break;
        case LM_STATS_ERRORED_RECEIVE_CNT:
#define LM_STATS_ERROR_DISCARD_SUM( _pdev, _i )  _pdev->vars.stats.stats_mirror.stats_fw.eth_tstorm_common.client_statistics[_i].checksum_discard + \
                                                 _pdev->vars.stats.stats_mirror.stats_fw.eth_tstorm_common.client_statistics[_i].packets_too_big_discard + \
                                                 _pdev->vars.stats.stats_mirror.stats_fw.eth_tstorm_common.port_statistics.mac_discard + \
                                                 LM_STATS_HW_GET_MACS_U64(_pdev, stats_rx.rx_stat_dot3statsframestoolong )
            stats->as_u64 = LM_STATS_ERROR_DISCARD_SUM( pdev, i ) ;
           break;
        case LM_STATS_RCV_CRC_ERROR:
            // Spec. 9
            stats->as_u64 = LM_STATS_HW_GET_MACS_U64(pdev, stats_rx.rx_stat_dot3statsfcserrors) ;
            // ioc Dot3StatsFCSErrors
            break;
        case LM_STATS_ALIGNMENT_ERROR:
            // Spec. 10
            if( !IS_PMF(pdev))
            {
                stats->as_u64 = 0 ;
            }
            else
            {
                stats->as_u64 =  LM_STATS_HW_GET_MACS_U64(pdev, stats_rx.rx_stat_dot3statsalignmenterrors) ;
            }
            // ioc Dot3StatsAlignmentErrors
            break;
        case LM_STATS_SINGLE_COLLISION_FRAMES:
            // Spec. 18
            if( !IS_PMF(pdev) )
            {
                stats->as_u64 = 0 ;
            }
            else
            {
                stats->as_u64 =  LM_STATS_HW_GET_MACS_U64(pdev, stats_tx.tx_stat_dot3statssinglecollisionframes ) ;
            }
            // ioc Dot3StatsSingleCollisionFrames
            break;
        case LM_STATS_MULTIPLE_COLLISION_FRAMES:
            // Spec. 19
            if( !IS_PMF(pdev) )
            {
                stats->as_u64 = 0 ;
            }
            else
            {
                stats->as_u64 =  LM_STATS_HW_GET_MACS_U64(pdev, stats_tx.tx_stat_dot3statsmultiplecollisionframes ) ;
            }
            // ioc Dot3StatsMultipleCollisionFrame
            break;
        case LM_STATS_FRAMES_DEFERRED:
            // Spec. 40 (not in mini port)
            stats->as_u64 =  LM_STATS_HW_GET_MACS_U64(pdev, stats_tx.tx_stat_dot3statsdeferredtransmissions ) ;
            // ioc Dot3StatsDeferredTransmissions
            break;
        case LM_STATS_MAX_COLLISIONS:
            // Spec. 21
            stats->as_u64 = LM_STATS_HW_GET_MACS_U64(pdev, stats_tx.tx_stat_dot3statsexcessivecollisions ) ;
            // ioc Dot3StatsExcessiveCollisions
            break;
        case LM_STATS_UNICAST_FRAMES_XMIT:
            // Spec. 6
            stats->as_u64 = pdev->vars.stats.stats_mirror.stats_fw.eth_xstorm_common.client_statistics[i].unicast_pkts_sent ;
            break;
        case LM_STATS_MULTICAST_FRAMES_XMIT:
            // Spec. 7
            stats->as_u64 = pdev->vars.stats.stats_mirror.stats_fw.eth_xstorm_common.client_statistics[i].multicast_pkts_sent ;
            break;
        case LM_STATS_BROADCAST_FRAMES_XMIT:
            stats->as_u64 = pdev->vars.stats.stats_mirror.stats_fw.eth_xstorm_common.client_statistics[i].broadcast_pkts_sent ;
            break;
        case LM_STATS_UNICAST_FRAMES_RCV:
            stats->as_u64 = pdev->vars.stats.stats_mirror.stats_fw.eth_tstorm_common.client_statistics[i].rcv_unicast_pkts ;
            break;
        case LM_STATS_MULTICAST_FRAMES_RCV:
            stats->as_u64 = pdev->vars.stats.stats_mirror.stats_fw.eth_tstorm_common.client_statistics[i].rcv_multicast_pkts ;
            break;
        case LM_STATS_BROADCAST_FRAMES_RCV:
            stats->as_u64 = pdev->vars.stats.stats_mirror.stats_fw.eth_tstorm_common.client_statistics[i].rcv_broadcast_pkts ;
            break;
        case LM_STATS_ERRORED_TRANSMIT_CNT:
            if( !IS_PMF(pdev) )
            {
                stats->as_u64 = 0 ;
            }
            else
            {
                stats->as_u64 =  LM_STATS_HW_GET_MACS_U64(pdev, stats_tx.tx_stat_dot3statsinternalmactransmiterrors ) ;
            }
            break;
        case LM_STATS_RCV_OVERRUN:
            stats->as_u64 =  pdev->vars.stats.stats_mirror.stats_hw.nig.brb_discard ;
            stats->as_u64+=  pdev->vars.stats.stats_mirror.stats_hw.nig.brb_truncate ;
            stats->as_u64+=  pdev->vars.stats.stats_mirror.stats_fw.eth_tstorm_common.port_statistics.xxoverflow_discard ;
            break;
        case LM_STATS_XMIT_UNDERRUN:
            //These counters are always zero
            stats->as_u64 = 0;
            break;
        case LM_STATS_RCV_NO_BUFFER_DROP:
            stats->as_u64 = pdev->vars.stats.stats_mirror.stats_fw.eth_tstorm_common.client_statistics[i].no_buff_discard ;
            stats->as_u64+= pdev->vars.stats.stats_mirror.stats_fw.eth_ustorm_common.client_statistics[i].ucast_no_buff_pkts ;
            stats->as_u64+= pdev->vars.stats.stats_mirror.stats_fw.eth_ustorm_common.client_statistics[i].mcast_no_buff_pkts ;
            stats->as_u64+= pdev->vars.stats.stats_mirror.stats_fw.eth_ustorm_common.client_statistics[i].bcast_no_buff_pkts ;
            // ioc IfInMBUFDiscards
            break;
        case LM_STATS_BYTES_RCV:
            stats->as_u64 = pdev->vars.stats.stats_mirror.stats_fw.eth_tstorm_common.client_statistics[i].rcv_broadcast_bytes +
                            pdev->vars.stats.stats_mirror.stats_fw.eth_tstorm_common.client_statistics[i].rcv_multicast_bytes +
                            pdev->vars.stats.stats_mirror.stats_fw.eth_tstorm_common.client_statistics[i].rcv_unicast_bytes ;
            //  ioc IfHCInOctets
            break;
        case LM_STATS_BYTES_XMIT:
            stats->as_u64 = pdev->vars.stats.stats_mirror.stats_fw.eth_xstorm_common.client_statistics[i].total_sent_bytes ;
            // ioc IfHCOutOctets
            break;
        case LM_STATS_IF_IN_DISCARDS:
            stats->as_u64 = LM_STATS_ERROR_DISCARD_SUM( pdev, i ) ;                                                         // LM_STATS_ERRORED_RECEIVE_CNT
            stats->as_u64+= pdev->vars.stats.stats_mirror.stats_fw.eth_tstorm_common.client_statistics[i].no_buff_discard ; // LM_STATS_RCV_NO_BUFFER_DROP
            stats->as_u64+= pdev->vars.stats.stats_mirror.stats_fw.eth_ustorm_common.client_statistics[i].ucast_no_buff_pkts ; // LM_STATS_RCV_NO_BUFFER_DROP
            stats->as_u64+= pdev->vars.stats.stats_mirror.stats_fw.eth_ustorm_common.client_statistics[i].mcast_no_buff_pkts ; // LM_STATS_RCV_NO_BUFFER_DROP
            stats->as_u64+= pdev->vars.stats.stats_mirror.stats_fw.eth_ustorm_common.client_statistics[i].bcast_no_buff_pkts ; // LM_STATS_RCV_NO_BUFFER_DROP
            stats->as_u64+= pdev->vars.stats.stats_mirror.stats_hw.nig.brb_discard ;                                        // LM_STATS_RCV_OVERRUN
            stats->as_u64+= pdev->vars.stats.stats_mirror.stats_hw.nig.brb_truncate ;                                       // LM_STATS_RCV_OVERRUN
            stats->as_u64+= pdev->vars.stats.stats_mirror.stats_fw.eth_tstorm_common.port_statistics.xxoverflow_discard ;   // LM_STATS_RCV_OVERRUN
            break;
        case LM_STATS_MULTICAST_BYTES_RCV:
            stats->as_u64 = pdev->vars.stats.stats_mirror.stats_fw.eth_tstorm_common.client_statistics[i].rcv_multicast_bytes ;
            break;
        case LM_STATS_DIRECTED_BYTES_RCV:
            stats->as_u64 = pdev->vars.stats.stats_mirror.stats_fw.eth_tstorm_common.client_statistics[i].rcv_unicast_bytes ;
            break;
        case LM_STATS_BROADCAST_BYTES_RCV:
            stats->as_u64 = pdev->vars.stats.stats_mirror.stats_fw.eth_tstorm_common.client_statistics[i].rcv_broadcast_bytes ;
            break;
        case LM_STATS_DIRECTED_BYTES_XMIT:
            stats->as_u64 = pdev->vars.stats.stats_mirror.stats_fw.eth_xstorm_common.client_statistics[i].unicast_bytes_sent ;
            break;
        case LM_STATS_MULTICAST_BYTES_XMIT:
            stats->as_u64 = pdev->vars.stats.stats_mirror.stats_fw.eth_xstorm_common.client_statistics[i].multicast_bytes_sent ;
            break;
        case LM_STATS_BROADCAST_BYTES_XMIT:
            stats->as_u64 = pdev->vars.stats.stats_mirror.stats_fw.eth_xstorm_common.client_statistics[i].broadcast_bytes_sent ;
            break;
/*
        case LM_STATS_IF_IN_ERRORS:
        case LM_STATS_IF_OUT_ERRORS:
            stats->as_u32.low = 0;
            stats->as_u32.high = 0;
            break;
*/
        default:
           stats->as_u64 = 0 ;
            lm_status = LM_STATUS_INVALID_PARAMETER;
            break;
    }
    //DbgMessage2(pdev, WARN, "lm_get_stats: stats_type=0x%X val=%d\n", stats_type, stats->as_u64);
    return lm_status;
} /* lm_get_stats */
/*******************************************************************************
 * Description:
 *  Zero the mirror statistics (probably after miniport was down in windows, 'driver unload' on ediag)
 *
 * Return:
 ******************************************************************************/
void lm_stats_reset( struct _lm_device_t* pdev)
{
    DbgMessage(pdev, INFORM, "Zero 'mirror' statistics...\n");
    mm_mem_zero( &pdev->vars.stats.stats_mirror, sizeof(pdev->vars.stats.stats_mirror) ) ;
}
/*******************************************************************************
 * Description:
 *
 * Return:
 ******************************************************************************/
static lm_nwuf_t * find_nwuf( lm_nwuf_list_t* nwuf_list,
                              u32_t           mask_size,
                              u8_t*           byte_mask,
                              u8_t*           pattern )
{
    lm_nwuf_t *nwuf;
    u8_t found;
    u32_t idx;
    u32_t j;
    u32_t k;
    ASSERT_STATIC(LM_MAX_NWUF_CNT==8);
    for(idx = 0; idx < LM_MAX_NWUF_CNT; idx++)
    {
        nwuf = &nwuf_list->nwuf_arr[idx];
        if(nwuf->size != mask_size)
        {
            continue;
        }
        found = TRUE;
        for(j = 0; j < mask_size && found == TRUE; j++)
        {
            if(nwuf->mask[j] != byte_mask[j])
            {
                found = FALSE;
                break;
            }
            for(k = 0; k < 8; k++)
            {
                if((byte_mask[j] & (1 << k)) &&
                    (nwuf->pattern[j*8 + k] != pattern[j*8 + k]))
                {
                    found = FALSE;
                    break;
                }
            }
        }
        if(found)
        {
            return nwuf;
        }
    }
    return NULL;
} /* find_nwuf */
/*******************************************************************************
 * Description:
 *
 * Return:
 ******************************************************************************/
lm_status_t lm_add_nwuf( lm_device_t* pdev,
                         u32_t        mask_size,
                         u8_t*        byte_mask,
                         u8_t*        pattern )
{
    lm_nwuf_t* nwuf        = NULL ;
    u32_t      idx         = 0 ;
    u32_t      j           = 0 ;
    u32_t      k           = 0 ;
    u32_t      zero_serial = 0 ;
    if( ERR_IF(0 == mask_size) || ERR_IF( mask_size > LM_NWUF_PATTERN_MASK_SIZE ) )
    {
        DbgBreakMsg("Invalid byte mask size\n");
        return LM_STATUS_FAILURE;
    }
    /* If this is a duplicate entry, we are done. */
    nwuf = find_nwuf(&pdev->nwuf_list, mask_size, byte_mask, pattern);
    // according to DTM test (WHQL) we should fail duplicate adding
    if( NULL != nwuf )
    {
        DbgMessage(pdev, WARN, "Duplicated nwuf entry.\n");
        return LM_STATUS_EXISTING_OBJECT;
    }
    /* Find an empty slot. */
    nwuf = NULL;
    for(idx = 0; idx < LM_MAX_NWUF_CNT; idx++)
    {
        if(pdev->nwuf_list.nwuf_arr[idx].size == 0)
        {
            nwuf = &pdev->nwuf_list.nwuf_arr[idx] ;
            break;
        }
    }
    if( NULL == nwuf )
    {
        DbgMessage(pdev, WARN, "Cannot add Nwuf, exceeded maximum.\n");
        return LM_STATUS_RESOURCE;
    }
    pdev->nwuf_list.cnt++;
    /* Save nwuf data. */
    nwuf->size         = mask_size;
    // apply the mask on the pattern
    for(j = 0; j < mask_size; j++)
    {
        nwuf->mask[j] = byte_mask[j];
        for(k = 0; k < 8; k++)
        {
            if(byte_mask[j] & (1 << k))
            {
                nwuf->pattern[j*8 + k] = pattern[j*8 + k];
                zero_serial = 0;
            }
            else
            {
                nwuf->pattern[j*8 + k] = 0;
                ++zero_serial;
            }
        }
    }
    // Decrement from pattern size last bits that are not enabled (revresed)
    // TODO: When pattern size will be added to the interface, this calculation (zero_serial) is not needed, and
    //       pattern size would be the original pattern size as recieved from OS
    nwuf->pattern_size = mask_size*8 - zero_serial ;
    j = nwuf->pattern_size/8 ;
    if( nwuf->pattern_size % 8 )
    {
        j++;
    }
    j*= 8;
    // TODO: when patter size will be added to the interface, j should be: mask_size*8
    // calc the CRC using the same NIG algorithem and save it
    nwuf->crc32 = calc_crc32( nwuf->pattern, j, 0xffffffff /*seed*/, 1 /*complement*/ ) ;
#define WOL_DBG_PRINT 0
#if (WOL_DBG_PRINT) // this is to debug wolpattern WHQL test
    {
        DbgPrint("lm_add_nwuf: pattern[%u] mask_size=%03u pattern_size=%03u (%03u) crc calc size=%03u\n",
                 idx,
                 nwuf->size,
                 nwuf->pattern_size,
                 nwuf->size*8,
                 j );
        DbgPrint("pattern[%u] CRC=0x%08x\n",idx, nwuf->crc32 ) ;
        //DbgPrint("Pattern (original) size=%03u\n", nwuf->pattern_size ) ;

        for( idx = 0 ; idx < nwuf->size*8 ; idx++ )
        {
            DbgPrint("%02X", pattern[idx] ) ;
            if( idx != nwuf->size*8-1 )
            {
                DbgPrint("-") ;
            }
            if( ( 0!= idx ) && 0 == ( idx % 32 ) )
            {
                DbgPrint("\n") ;
            }
        }
        DbgPrint("\nPattern (masked):\n");
        for( idx = 0 ; idx < nwuf->size*8 ; idx++ )
        {
            DbgPrint("%02X", nwuf->pattern[idx] ) ;
            if( idx != nwuf->size*8-1 )
            {
                DbgPrint("-") ;
            }
            if( ( 0!= idx ) && 0 == ( idx % 32 ) )
            {
                DbgPrint("\n") ;
            }
        }
        DbgPrint("\nmask (size=%03u)\n", nwuf->size) ;
        for( idx = 0 ; idx < nwuf->size ; idx++ )
        {
            DbgPrint("%02X", byte_mask[idx] ) ;
            if( idx != nwuf->size-1 )
            {
                DbgPrint("-") ;
            }
        }
        DbgPrint("\n") ;
    }
#endif // WOL_DBG_PRINT
    if ERR_IF( 0xffffffff == nwuf->crc32 )
    {
        DbgBreakMsg("Invalid crc32\n") ;
    }
    return LM_STATUS_SUCCESS;
} /* lm_add_nwuf */

/*******************************************************************************
 * Description:
 *
 * Return:
 ******************************************************************************/
lm_status_t lm_del_nwuf( lm_device_t* pdev,
                         u32_t        mask_size,
                         u8_t*        byte_mask,
                         u8_t *       pattern )
{
    lm_nwuf_t *nwuf;
    u32_t k;
    if(mask_size == 0 || mask_size > LM_NWUF_PATTERN_MASK_SIZE)
    {
        DbgBreakMsg("Invalid byte mask size\n");
        return LM_STATUS_FAILURE;
    }
    /* Look for a matching pattern. */
    nwuf = find_nwuf(&pdev->nwuf_list, mask_size, byte_mask, pattern);
    if(nwuf)
    {
        /*
        DbgPrint("lm_del_nwuf: pattern[?] mask_size=%03u(%03u) cnt=%u crc32=0x%08x %02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X....\n",
                 nwuf->size, nwuf->size*8, pdev->nwuf_list.cnt-1, nwuf->crc32,
                 nwuf->pattern[0],  nwuf->pattern[1], nwuf->pattern[2], nwuf->pattern[3],
                 nwuf->pattern[4],  nwuf->pattern[5], nwuf->pattern[6], nwuf->pattern[7],
                 nwuf->pattern[8],  nwuf->pattern[9], nwuf->pattern[10], nwuf->pattern[11],
                 nwuf->pattern[12], nwuf->pattern[13], nwuf->pattern[14], nwuf->pattern[15] ) ;
        */
        nwuf->size = 0;
        nwuf->crc32 = 0 ;
        for(k = 0; k < LM_NWUF_PATTERN_MASK_SIZE; k++)
        {
            nwuf->mask[k] = 0;
        }
        for(k = 0; k < LM_NWUF_PATTERN_SIZE; k++)
        {
            nwuf->pattern[k] = 0xff;
        }
        pdev->nwuf_list.cnt--;
    }
    else
    {
        // according to DTM test (WHQL) we should fail non exists delete
        DbgMessage1(pdev, WARN, "not exists nwuf entry. mask_size=%03d\n", mask_size );
        return LM_STATUS_OBJECT_NOT_FOUND;
    }
    return LM_STATUS_SUCCESS;
} /* lm_del_nwuf */

/*******************************************************************************
 * Description:
 *
 * Return:
 ******************************************************************************/
void
lm_clear_nwuf(
    lm_device_t *pdev)
{
    u32_t j;
    u32_t k;
    for(j = 0; j < LM_MAX_NWUF_CNT; j++)
    {
        pdev->nwuf_list.nwuf_arr[j].size = 0;
        for(k = 0; k < LM_NWUF_PATTERN_MASK_SIZE; k++)
        {
            pdev->nwuf_list.nwuf_arr[j].mask[k] = 0;
        }
        for(k = 0; k < LM_NWUF_PATTERN_SIZE; k++)
        {
            pdev->nwuf_list.nwuf_arr[j].pattern[k] = 0xff;
        }
    }
    pdev->nwuf_list.cnt = 0;
} /* lm_clear_nwuf */

/*******************************************************************************
 * Description:
 *
 * Return:
 ******************************************************************************/
u32_t
init_nwuf_57710(
    lm_device_t *pdev,
    lm_nwuf_list_t *nwuf_list)
{
    lm_nwuf_t* nwuf       = NULL ;
    u32_t      nwuf_cnt   = 0 ;
    u32_t      offset     = 0 ;
    u8_t       mask       = 0 ;
    u32_t      val        = 0 ;
    u64_t      val_64     = 0 ;
    u32_t      val_32[2]  = {0} ;
    u32_t      mod        = 0 ;
    u32_t      idx        = 0 ;
    u32_t      bit        = 0 ;
    u32_t      reg_len    = 0 ;
    u32_t      reg_crc    = 0 ;
    u32_t      reg_be     = 0 ;
    u32_t      reg_offset = 0 ;
    if CHK_NULL(pdev)
    {
        return 0 ;
    }
    ASSERT_STATIC(LM_NWUF_PATTERN_SIZE <= 128 );
    // Write the size + crc32 of the patterns
    for(idx = 0; idx < LM_MAX_NWUF_CNT; idx++)
    {
        nwuf = &nwuf_list->nwuf_arr[idx];
        // find NIG registers names
#define LM_NIG_ACPI_PAT_LEN_IDX(_func,_idx) NIG_REG_LLH##_func##_ACPI_PAT_##_idx##_LEN
#define LM_NIG_ACPI_PAT_CRC_IDX(_func,_idx) NIG_REG_LLH##_func##_ACPI_PAT_##_idx##_CRC
        switch( idx )
        { /* TBD - E1H: currenlty assuming split registers in NIG */
        case 0:
            reg_len = (0 == PORT_ID(pdev)) ? LM_NIG_ACPI_PAT_LEN_IDX(0,0) : LM_NIG_ACPI_PAT_LEN_IDX(1,0) ;
            reg_crc = (0 == PORT_ID(pdev)) ? LM_NIG_ACPI_PAT_CRC_IDX(0,0) : LM_NIG_ACPI_PAT_CRC_IDX(1,0) ;
            break;
        case 1:
            reg_len = (0 == PORT_ID(pdev)) ? LM_NIG_ACPI_PAT_LEN_IDX(0,1) : LM_NIG_ACPI_PAT_LEN_IDX(1,1) ;
            reg_crc = (0 == PORT_ID(pdev)) ? LM_NIG_ACPI_PAT_CRC_IDX(0,1) : LM_NIG_ACPI_PAT_CRC_IDX(1,1) ;
            break;
        case 2:
            reg_len = (0 == PORT_ID(pdev)) ? LM_NIG_ACPI_PAT_LEN_IDX(0,2) : LM_NIG_ACPI_PAT_LEN_IDX(1,2) ;
            reg_crc = (0 == PORT_ID(pdev)) ? LM_NIG_ACPI_PAT_CRC_IDX(0,2) : LM_NIG_ACPI_PAT_CRC_IDX(1,2) ;
            break;
        case 3:
            reg_len = (0 == PORT_ID(pdev)) ? LM_NIG_ACPI_PAT_LEN_IDX(0,3) : LM_NIG_ACPI_PAT_LEN_IDX(1,3) ;
            reg_crc = (0 == PORT_ID(pdev)) ? LM_NIG_ACPI_PAT_CRC_IDX(0,3) : LM_NIG_ACPI_PAT_CRC_IDX(1,3) ;
            break;
        case 4:
            reg_len = (0 == PORT_ID(pdev)) ? LM_NIG_ACPI_PAT_LEN_IDX(0,4) : LM_NIG_ACPI_PAT_LEN_IDX(1,4) ;
            reg_crc = (0 == PORT_ID(pdev)) ? LM_NIG_ACPI_PAT_CRC_IDX(0,4) : LM_NIG_ACPI_PAT_CRC_IDX(1,4) ;
            break;
        case 5:
            reg_len = (0 == PORT_ID(pdev)) ? LM_NIG_ACPI_PAT_LEN_IDX(0,5) : LM_NIG_ACPI_PAT_LEN_IDX(1,5) ;
            reg_crc = (0 == PORT_ID(pdev)) ? LM_NIG_ACPI_PAT_CRC_IDX(0,5) : LM_NIG_ACPI_PAT_CRC_IDX(1,5) ;
            break;
        case 6:
            reg_len = (0 == PORT_ID(pdev)) ? LM_NIG_ACPI_PAT_LEN_IDX(0,6) : LM_NIG_ACPI_PAT_LEN_IDX(1,6) ;
            reg_crc = (0 == PORT_ID(pdev)) ? LM_NIG_ACPI_PAT_CRC_IDX(0,6) : LM_NIG_ACPI_PAT_CRC_IDX(1,6) ;
            break;
        case 7:
            reg_len = (0 == PORT_ID(pdev)) ? LM_NIG_ACPI_PAT_LEN_IDX(0,7) : LM_NIG_ACPI_PAT_LEN_IDX(1,7) ;
            reg_crc = (0 == PORT_ID(pdev)) ? LM_NIG_ACPI_PAT_CRC_IDX(0,7) : LM_NIG_ACPI_PAT_CRC_IDX(1,7) ;
            break;
        default:
            DbgBreakMsg("Invalid index\n") ;
            return 0 ;
        } // switch idx
        // write pattern length
        val = nwuf->pattern_size;
        DbgMessage3(pdev, VERBOSE, "init_nwuf_57710: idx[%d] crc_mask=0x%08x size=%d\n", idx, nwuf->crc32, val  );
        // Init NIG registers
#if !(defined(DOS) || defined(__LINUX))
        if (0)
        {
            val = min( nwuf->size * 8, 64 ) ;
            if( val != nwuf->size * 8 )
            {
                DbgMessage3(pdev, WARN, "init_nwuf_57710: idx[%d] Updated size=%03d-->%03d\n", idx, nwuf->size * 8, val ) ;
            }
        }
#endif
        REG_WR( pdev,  reg_len, val ) ;
        // write crc value
        val = nwuf->crc32 ;
        REG_WR( pdev,  reg_crc, val ) ;
     } // LM_MAX_NWUF_CNT loop
    // byte enable mask
    reg_be = (0 == PORT_ID(pdev)) ? NIG_REG_LLH0_ACPI_BE_MEM_DATA : NIG_REG_LLH1_ACPI_BE_MEM_DATA ;
// create a matrix following LLH_vlsi_spec_rev4.doc document:
//
//        63                                                     56      7                                                        0
//        +------------------------------------------------------------------------------------------------------------------------+
//word 0  |Pattern 7 bit 0 | Pattern 6 bit 0 |....|Pattern 0 bit 0|..... |Pattern 7 bit 7 | Pattern 6 bit 7 |....|Pattern 0 bit 7  |
//        +------------------------------------------------------------------------------------------------------------------------+
//word 1  |Pattern 7 bit 8 | Pattern 6 bit 8 |....|Pattern 0 bit 8|..... |Pattern 7 bit 15| Pattern 6 bit 15|....|Pattern 0 bit 15 |
//        +------------------------------------------------------------------------------------------------------------------------+
//        |                                                      ..........                                                        |
//        +------------------------------------------------------------------------------------------------------------------------+
//        |                                                      ..........                                                        |
//        +------------------------------------------------------------------------------------------------------------------------+
//        |                                                                                                                        |
//        +------------------------------------------------------------------------------------------------------------------------+
//        |                                                      ..........                                                        |
//        +------------------------------------------------------------------------------------------------------------------------+
//word 15 |Pattern 7bit 120| Pattern6 bit120 |....|Pattern0 bit120|..... |Pattern7 bit 127| Pattern6 bit 127|....|Pattern0 bit 127 |
//        +------------------------------------------------------------------------------------------------------------------------+

    for(offset = 0; offset <= LM_NWUF_PATTERN_SIZE; offset++)
    {
        mod = offset%8 ;
        if ( ( 0 == mod ) && ( offset!= 0 ) )
        {
            // write to the registers, WB (write using DMAE)
            reg_offset  = ( offset / 8 ) - 1  ; // 0 - 15
            val = (reg_offset*sizeof(u64_t)) ;
            // For yet to be explained reasons, using WR_DMAE write it to the opposite port.
            // We'll always use indirect writes
            if( 0 )//pdev->vars.b_is_dmae_ready )
            {
                REG_WR_DMAE( pdev,  reg_be+val, &val_64 ) ;
            }
            else
            {
                val_32[0] = U64_LO(val_64);
                val_32[1] = U64_HI(val_64);
                REG_WR_IND( pdev,  reg_be+val,   val_32[0] ) ;
                REG_WR_IND( pdev,  reg_be+val+4, val_32[1] ) ;
            }
            // reset for next 8 iterations
            val_64 = 0 ;
        }
        // after write - nothing to do!
        if( LM_NWUF_PATTERN_SIZE == offset )
        {
            break ;
        }
        for(idx = 0; idx < LM_MAX_NWUF_CNT; idx++)
        {
            nwuf = &nwuf_list->nwuf_arr[idx];
            if(nwuf->size == 0 || offset > nwuf->size * 8)
            {
                continue;
            }
            mask = nwuf->mask[(offset/8)]; // 0-15
            bit = mod ;
            if( mask & (1 << bit) )
            {
                val_64  |= 0x1ULL << idx;
            }
        } // LM_MAX_NWUF_CNT
        if( mod != 7 )
        {
            val_64  = val_64 << 8 ;
        }
    } // LM_NWUF_PATTERN_SIZE
    nwuf_cnt = 0;
    // count total items
    for(idx = 0; idx < LM_MAX_NWUF_CNT; idx++)
    {
        nwuf = &nwuf_list->nwuf_arr[idx];
        if(nwuf->size == 0)
        {
            continue;
        }
        nwuf_cnt++ ;
    }
    return nwuf_cnt;
} /* init_nwuf_57510 */
/*******************************************************************************
 * Description:
 *
 * Return:
 ******************************************************************************/
STATIC void
set_d0_power_state(
    lm_device_t *pdev,
    u8_t set_pci_pm)
{
    u32_t idx = 0;
    UNREFERENCED_PARAMETER_(set_pci_pm);
    DbgMessage(pdev, INFORM, "### set_d0_power_state\n");
#if 0
    u32_t val;
    /* This step should be done by the OS or the caller.  Windows is
     * already doing this. */
    if(set_pci_pm)
    {
        /* Set the device to D0 state.  If a device is already in D3 state,
         * we will not be able to read the PCICFG_PM_CSR register using the
         * PCI memory command, we need to use config access here. */
        mm_read_pci(
            pdev,
            OFFSETOF(reg_space_t, pci_config.pcicfg_pm_csr),
            &val);
        /* Set the device to D0 state.  This may be already done by the OS. */
        val &= ~PCICFG_PM_CSR_STATE;
        val |= PCICFG_PM_CSR_STATE_D0 | PCICFG_PM_CSR_PME_STATUS;
        mm_write_pci(
            pdev,
            OFFSETOF(reg_space_t, pci_config.pcicfg_pm_csr),
            val);
    }
#endif
    /* With 5706_A1, the chip gets a reset coming out of D3.  Wait
     * for the boot to code finish running before we continue.  Without
     * this wait, we could run into lockup or the PHY may not work. */
    if(CHIP_ID(pdev) == CHIP_ID_5706_A1)
    {
        for(idx = 0; idx < 1000; idx++)
        {
            mm_wait(pdev, 15);
        }
    }
#if 0 // PWR_TODO - WOL wait for spec
    /* Clear the ACPI_RCVD and MPKT_RCVD bits and disable magic packet. */
    val = REG_RD(pdev, emac.emac_mode);
    val |= EMAC_MODE_MPKT_RCVD | EMAC_MODE_ACPI_RCVD;
    val &= ~EMAC_MODE_MPKT;
    REG_WR(pdev, emac.emac_mode, val);
    /* Disable interesting packet detection. */
    val = REG_RD(pdev, rpm.rpm_config);
    val &= ~RPM_CONFIG_ACPI_ENA;
    REG_WR(pdev, rpm.rpm_config, val);
#endif // if 0
} /* set_d0_power_state */
/*******************************************************************************
 * Description:
 *
 * Return:
 ******************************************************************************/
#if 0
STATIC void
set_d3_power_state(
    lm_device_t *pdev,
    lm_wake_up_mode_t wake_up_mode,
    u8_t set_pci_pm)
{
    DbgMessage(pdev, INFORM, "### set_d3_power_state\n");
    u32_t gpio_pin;
    u32_t cnt;
    u32_t fw_timed_out;
    u32_t reset_reason;
    u32_t val = 0;
    /* This step should be done by the OS or the caller.  Windows is
     * already doing this. */
    // PWR_TODO - what things we might need to do here:
    // 1. phy_reset
    // 2. mdio_clock
    // 3. gpio
    if(set_pci_pm)
    {
        /* Set the device to D3 state. */
        REG_RD_OFFSET(
            pdev,
            OFFSETOF(reg_space_t, pci_config.pcicfg_pm_csr),
            &val);
        val &= ~PCICFG_PM_CSR_STATE;
        val |= PCICFG_PM_CSR_STATE_D3_HOT;
        REG_WR_OFFSET(
            pdev,
            OFFSETOF(reg_space_t, pci_config.pcicfg_pm_csr),
            val);
    }
} /* set_d3_power_state */
#endif // 0

/*******************************************************************************
 * Description:
 *
 * Return:
 ******************************************************************************/
void
lm_set_power_state(
    lm_device_t*      pdev,
    lm_power_state_t  power_state,
    lm_wake_up_mode_t wake_up_mode,     /* Valid when power_state is D3. */
    u8_t              set_pci_pm )
{
    UNREFERENCED_PARAMETER_(wake_up_mode);
    switch( power_state )
    {
    case LM_POWER_STATE_D0:
        set_d0_power_state(pdev, set_pci_pm);
        break;
    default:
        //set_d3_power_state(pdev, wake_up_mode, set_pci_pm);
        break;
    }
} /* lm_set_power_state */

/*******************************************************************************
 * Description:
 *
 * Return:
 ******************************************************************************/
lm_interrupt_status_t
lm_get_interrupt_status(
    lm_device_t *pdev)
{
    lm_interrupt_status_t intr_status = 0;

    if (INTR_BLK_REQUIRE_CMD_CTRL(pdev)) {
        /* This is IGU GRC Access... need to write ctrl and then read data */
        REG_WR(pdev, IGU_REG_COMMAND_REG_CTRL, INTR_BLK_CMD_CTRL_RD_WMASK(pdev));
    }
    intr_status = REG_RD(pdev, INTR_BLK_SIMD_ADDR_WMASK(pdev));
    /* if above, need to read 64 bits from IGU...and take care of all-ones */
    ASSERT_STATIC(MAX_RSS_CHAINS <= 32);
    return intr_status;
} /* lm_get_interrupt_status */
/* Reads IGU interrupt status register MSB / LSB */
static u32_t lm_read_isr32 (
    lm_device_t *pdev,
    u32_t addr)
{
    u32 offset = IS_PFDEV(pdev) ? BAR_IGU_INTMEM : VF_BAR0_IGU_OFFSET;
    u32_t res = 0;
    u32_t value;
    do {
        /* Read the 32 bit value from BAR */
        LM_BAR_RD32_OFFSET(pdev,BAR_0,offset + addr, &value);
        DbgMessage2(pdev,VERBOSEi,"  ### lm_read_isr32 read address 0x%x value=0x%x\n",addr,value);
        DbgBreakIf(value == 0xffffffff);
        res |= value;
        /* Do one more iteration if we got the value for a legitimate "all ones" */
    } while (value == 0xefffffff);
    return res;
}
/* Reads IGU interrupt status register MSB / LSB */
static u64_t lm_read_isr64(
    lm_device_t *pdev,
    u32_t addr)
{
    u32 offset = IS_PFDEV(pdev) ? BAR_IGU_INTMEM : VF_BAR0_IGU_OFFSET;
    u64_t res = 0;
    u64_t value;
    do {
        /* Read the 32 bit value from BAR */
        LM_BAR_RD64_OFFSET(pdev,BAR_0, offset + addr,&value);
        DbgMessage3(pdev,FATAL,"  ### lm_read_isr64 read address 0x%x value=0x%x 0x%x\n",addr,(u32_t)(value>>32),(u32_t)value);
        DbgBreakIf(value == 0xffffffffffffffffULL);
        res |= value;
        /* Do one more iteration if we got the value for a legitimate "all ones" */
    } while (value == 0xefffffffffffffffULL);
    DbgMessage2(pdev,FATAL,"  ### lm_read_isr64 res=0x%x 0x%x\n",(u32_t)(res>>32),(u32_t)res);
    return res;
}
/*******************************************************************************
 * Description:
 *      Replacement function for lm_get_interrupt_status for dedicated IGU tests
 * Return:
 *      64 bit mask of active interrupts
 ******************************************************************************/
u64_t lm_igutest_get_isr32(struct _lm_device_t *pdev)
{
    u64_t intr_status = 0;
    intr_status = ((u64_t)lm_read_isr32(pdev,8 * IGU_REG_SISR_MDPC_WMASK_MSB_UPPER) << 32) |
        lm_read_isr32(pdev,8 * IGU_REG_SISR_MDPC_WMASK_LSB_UPPER);
    return intr_status;
} /* lm_igutest_get_isr */
u64_t
lm_igutest_get_isr64(struct _lm_device_t *pdev)
{
    return lm_read_isr64(pdev,8 * IGU_REG_SISR_MDPC_WMASK_UPPER);
} /* lm_igutest_get_isr */
lm_interrupt_status_t
lm_get_interrupt_status_wo_mask(
    lm_device_t *pdev)
{
    lm_interrupt_status_t intr_status = 0;
    if (INTR_BLK_REQUIRE_CMD_CTRL(pdev)) {
        /* This is IGU GRC Access... need to write ctrl and then read data */
        REG_WR(pdev, IGU_REG_COMMAND_REG_CTRL, INTR_BLK_CMD_CTRL_RD_WOMASK(pdev));
    }
    intr_status = REG_RD(pdev, INTR_BLK_SIMD_ADDR_WOMASK(pdev));
    /* if above, need to read 64 bits from IGU...and take care of all-ones */
    ASSERT_STATIC(MAX_RSS_CHAINS <= 32);
    return intr_status;
} /* lm_get_interrupt_status */


/*******************************************************************************
 * Description:
 *         Calculates crc 32 on a buffer
 *         Note: crc32_length MUST be aligned to 8
 * Return:
 ******************************************************************************/
u32_t calc_crc32( u8_t* crc32_packet, u32_t crc32_length, u32_t crc32_seed, u8_t complement)
{
   u32_t byte             = 0 ;
   u32_t bit              = 0 ;
   u8_t  msb              = 0 ; // 1
   u32_t temp             = 0 ;
   u32_t shft             = 0 ;
   u8_t  current_byte     = 0 ;
   u32_t crc32_result     = crc32_seed;
   const u32_t CRC32_POLY = 0x1edc6f41;
    if( CHK_NULL( crc32_packet) || ERR_IF( 0 == crc32_length ) || ERR_IF( 0 != ( crc32_length % 8 ) ) )
    {
        return crc32_result ;
    }
    for (byte = 0; byte < crc32_length; byte = byte + 1)
    {
        current_byte = crc32_packet[byte];
        for (bit = 0; bit < 8; bit = bit + 1)
        {
            msb = (u8_t)(crc32_result >> 31) ; // msb = crc32_result[31];
            crc32_result = crc32_result << 1;
            if ( msb != ( 0x1 & (current_byte>>bit)) ) // (msb != current_byte[bit])
            {
               crc32_result = crc32_result ^ CRC32_POLY;
               crc32_result |= 1 ;//crc32_result[0] = 1;
            }
         }
      }
      // Last step is to "mirror" every bit, swap the 4 bytes, and then complement each bit.
      //
      // Mirror:
      temp = crc32_result ;
      shft = sizeof(crc32_result) * 8 -1 ;
      for( crc32_result>>= 1; crc32_result; crc32_result>>= 1 )
      {
        temp <<= 1;
        temp  |= crc32_result & 1;
        shft-- ;
      }
      temp <<= shft ;
      //temp[31-bit] = crc32_result[bit];
      // Swap:
      // crc32_result = {temp[7:0], temp[15:8], temp[23:16], temp[31:24]};
      {
          u32_t t0, t1, t2, t3 ;
          t0 = ( ( 0x000000ff ) & ( temp >> 24 ) ) ; // temp >> 24 ;
          t1 = ( ( 0x0000ff00 ) & ( temp >> 8 ) ) ;
          t2 = ( ( 0x00ff0000 ) & ( temp << 8 ) ) ;
          t3 = ( ( 0xff000000 ) & ( temp << 24 ) ) ;
          crc32_result = t0 | t1 | t2 | t3 ;
      }
      // Complement:
      if (complement)
      {
          crc32_result = ~crc32_result ;
      }
      return crc32_result  ;
}
/*******************************************************************************
 * Description:
 *         Configures nwuf packets.
 *         (for wide bus)
 * Return:
 ******************************************************************************/
void lm_set_d3_nwuf(       lm_device_t*      pdev,
                     const lm_wake_up_mode_t wake_up_mode )
{
    const u8_t port_id     = PORT_ID(pdev);
    u8_t  abs_func_id      = ABS_FUNC_ID(pdev); // for debugging only
    u8_t  nwuf_reg_value   = 0 ;
    u32_t cnt              = 0 ;
    u32_t offset           = 0;

    UNREFERENCED_PARAMETER_( abs_func_id );

    /* Set up interesting packet detection. */
    if ( 0 != GET_FLAGS(wake_up_mode, LM_WAKE_UP_MODE_NWUF) )
    {
        // This comment - from TETON
        /* Also need to be documented in the prm - to prevent a false
         * detection, we need to disable ACP_EN if there is no pattern
         * programmed.  There is no way of preventing false detection
         * by intializing the pattern buffer a certain way. */
        if( (cnt = init_nwuf_57710(pdev, &pdev->nwuf_list)) )
        {
            DbgMessage1(pdev, WARN , "LM_WAKE_UP_MODE_NWUF is ON cnt=%d\n", cnt );
            nwuf_reg_value = 1 ;
        }
        else
        {
            DbgMessage(pdev, WARN , "LM_WAKE_UP_MODE_NWUF is ON cnt=0\n" );
            nwuf_reg_value = 0 ;
        }

        // Enable ACPI register (split)
        offset = (0 == port_id) ? NIG_REG_LLH0_ACPI_ENABLE :NIG_REG_LLH1_ACPI_ENABLE;
        REG_WR( pdev, offset, nwuf_reg_value ) ;

        if( !CHIP_IS_E1(pdev) )
        {
            // Enable per function register
            // make sure outer vlan written correctly
            init_nig_func(pdev);

            // for E2 and above, we need to set also NIG_REG_PX_ACPI_MF_GLOBAL_EN to 1
            // This register is global per port.
            // The "algorithm" will be - if ANY of the vnic is enabled - we enable ACPI for the port (logic OR)
            // The patterns themselves should prevent a "false positive" wake up for a function
            // All the above is relevant for MF SI mode!
            if ( !CHIP_IS_E1x(pdev)   &&
                 nwuf_reg_value       &&
                 ( IS_MF_SI_MODE(pdev) ) )
            {
                // TODO - NIV (T7.0) should be different behaviour!
                DbgBreakIf( CHIP_IS_E1(pdev) ); // if someone will take this if block out of "if( !IS_E1(pdev)"
                DbgBreakIf( !nwuf_reg_value );

                offset = (0 == port_id) ? NIG_REG_P0_ACPI_MF_GLOBAL_EN :NIG_REG_P1_ACPI_MF_GLOBAL_EN;

                REG_WR( pdev, offset, nwuf_reg_value ) ;
            }
        }
        DbgMessage1(pdev, WARN , "ACPI_ENABLE=%d\n", nwuf_reg_value );
    }
    else
    {
        DbgMessage(pdev, WARN , "LM_WAKE_UP_MODE_NWUF is OFF\n" );
    }
} /* lm_set_d3_nwuf */
/*******************************************************************************
 * Description:
 *         Configures magic packets.
 * Return:
 ******************************************************************************/
void lm_set_d3_mpkt( lm_device_t*            pdev,
                     const lm_wake_up_mode_t wake_up_mode )
{
    u32_t       emac_base     = 0 ;
    u32_t       val           = 0 ;
    u32_t       offset        = 0 ;
    u8_t  const b_enable_mpkt = ( 0 != GET_FLAGS(wake_up_mode, LM_WAKE_UP_MODE_MAGIC_PACKET) );
    u8_t*       mac_addr      = &pdev->params.mac_addr[0]; //&pdev->hw_info.mac_addr[0];

    if CHK_NULL(pdev)
    {
        DbgBreakIf(!pdev) ;
        return;
    }
    /* Set up magic packet detection. */
    if( b_enable_mpkt )
    {
        DbgMessage(pdev, WARN , "LM_WAKE_UP_MODE_MAGIC_PACKET is ON\n" );
    }
    else
    {
        DbgMessage(pdev, WARN , "LM_WAKE_UP_MODE_MAGIC_PACKET is OFF\n" );
    }
    emac_base = ( 0 == PORT_ID(pdev) ) ? GRCBASE_EMAC0 : GRCBASE_EMAC1 ;

    /* The mac address is written to entries 1-5 to
       preserve entry 0 which is used by the PMF */
    val = (mac_addr[0] << 8) | mac_addr[1];
    offset = EMAC_REG_EMAC_MAC_MATCH + (VNIC_ID(pdev)+ 1)*8 ;
    REG_WR(pdev, emac_base+ offset , b_enable_mpkt ? val:0);

    val = (mac_addr[2] << 24) | (mac_addr[3] << 16) |
          (mac_addr[4] << 8)  |  mac_addr[5];
    offset+= 4;
    REG_WR(pdev, emac_base+ offset, b_enable_mpkt ? val:0);
}
/*******************************************************************************
 * Description:
 *     reads iscsi_boot info block from shmem
 * Return:
 *     lm_status
 ******************************************************************************/
lm_status_t lm_get_iscsi_boot_info_block( struct _lm_device_t *pdev, struct _iscsi_info_block_hdr_t* iscsi_info_block_hdr_ptr )
{
    u32_t           val                = 0;
    u32_t           offset             = 0;
    const u8_t      func_mb_id         = FUNC_MAILBOX_ID(pdev);

    // dummy variables so we have convenience way to know the shmem offsets
    // This is a pointer so it doesn't load the stack.
    // If we delete these lines we won't have shmem_region_t symbols
    shmem_region_t*    shmem_region_dummy    = NULL;
    shmem2_region_t*   shmem2_region_dummy   = NULL;
    shared_hw_cfg_t*   shared_hw_cfg_dummy   = NULL;
    port_hw_cfg_t*     port_hw_cfg_dummy     = NULL;
    shared_feat_cfg_t* shared_feat_cfg_dummy = NULL;
    port_feat_cfg_t*   port_feat_cfg_dummy   = NULL;
    mf_cfg_t*          mf_cfg_dummy          = NULL;

    UNREFERENCED_PARAMETER_(shmem_region_dummy);
    UNREFERENCED_PARAMETER_(shmem2_region_dummy);
    UNREFERENCED_PARAMETER_(shared_hw_cfg_dummy);
    UNREFERENCED_PARAMETER_(port_hw_cfg_dummy);
    UNREFERENCED_PARAMETER_(shared_feat_cfg_dummy);
    UNREFERENCED_PARAMETER_(port_feat_cfg_dummy);
    UNREFERENCED_PARAMETER_(mf_cfg_dummy);

    if ( CHK_NULL( iscsi_info_block_hdr_ptr ) )
    {
        return LM_STATUS_INVALID_PARAMETER ;
    }

    if (pdev->hw_info.mcp_detected == 1)
    {
        offset = OFFSETOF(shmem_region_t,func_mb[func_mb_id].iscsi_boot_signature);
        LM_SHMEM_READ(pdev, offset, &val );
        iscsi_info_block_hdr_ptr->signature = val ;
        // only for debugging
        offset = OFFSETOF(shmem_region_t,func_mb[func_mb_id].iscsi_boot_block_offset);
        LM_SHMEM_READ(pdev, offset, &val );
    }
    else
    {
        // If mcp is detected the shmenm is not initialized and
        iscsi_info_block_hdr_ptr->signature = 0;
    }
    return LM_STATUS_SUCCESS ;
}

/*******************************************************************************
 * Description:
 *     stop any dma transactions to/from chip
 *     after this function is called, no write to chip is availalbe anymore.
 * Return:
 *     void
 ******************************************************************************/
void lm_disable_pci_dma(struct _lm_device_t *pdev)
{
    u32_t       val   = 0;
    u32_t       idx   = 0;
    const u32_t flags = (PCICFG_DEVICE_STATUS_NO_PEND << 16) ;

    if (IS_PFDEV(pdev))
    {
        if (CHIP_IS_E1x(pdev))
        {
            /* Disable bus_master. */
            val=REG_RD(pdev,GRCBASE_PCICONFIG+PCICFG_COMMAND_OFFSET);
            RESET_FLAGS( val, PCICFG_COMMAND_BUS_MASTER );
            REG_WR(pdev,GRCBASE_PCICONFIG+PCICFG_COMMAND_OFFSET,val);
        }
        else
        {
            /* In E2, there is a cleaner way to disable pci-dma, no need for a pci-configuration
             * transaction */
            REG_WR(pdev, PGLUE_B_REG_INTERNAL_PFID_ENABLE_MASTER, 0);
        }

        /* wait until there is no pending transaction. */
        for(idx = 0; idx < 1000; idx++)
        {
            val=REG_RD(pdev,GRCBASE_PCICONFIG+PCICFG_DEVICE_CONTROL);
            if( (val & flags) == 0)
            {
                break;
            }
            mm_wait(pdev, 5);
        }
    }
}
/*******************************************************************************
 * Description:
 *     enable Bus Master Enable
 * Return:
 *     void
 ******************************************************************************/
void lm_enable_pci_dma(struct _lm_device_t *pdev)
{
    u32_t       val   = 0;
    if (IS_PFDEV(pdev))
    {
        if (CHIP_IS_E1x(pdev))
        {
            /* Enable bus_master. */
            val=REG_RD(pdev,GRCBASE_PCICONFIG+PCICFG_COMMAND_OFFSET);
            if( 0 == GET_FLAGS( val, PCICFG_COMMAND_BUS_MASTER ) )
            {
                SET_FLAGS( val, PCICFG_COMMAND_BUS_MASTER );
                REG_WR(pdev,GRCBASE_PCICONFIG+PCICFG_COMMAND_OFFSET,val);
            }
        }
        else
        {
            /* In E2, there is a cleaner way to disable pci-dma, no need for a pci-configuration
             * transaction */
            REG_WR(pdev, PGLUE_B_REG_INTERNAL_PFID_ENABLE_MASTER, 1);
        }
    }
}
/*******************************************************************************
 * Description:
 *     disable non fatal error pcie reporting
 * Return:
 *     void
 ******************************************************************************/
void lm_set_pcie_nfe_report(lm_device_t *pdev)
{
    if(IS_PFDEV(pdev) && pdev->params.disable_pcie_nfr)
    {
        u32_t pci_devctl = 0 ;
        pci_devctl = REG_RD(pdev,GRCBASE_PCICONFIG + PCICFG_DEVICE_CONTROL);
        RESET_FLAGS( pci_devctl, PCICFG_DEVICE_STATUS_NON_FATAL_ERR_DET );
        REG_WR(pdev,GRCBASE_PCICONFIG + PCICFG_DEVICE_CONTROL,pci_devctl);
    }
}
/*******************************************************************************
 * Description:
 *         Acquiring the HW lock for a specific resource.
 *         The assumption is that only 1 bit is set in the resource parameter
 *         There is a HW attention in case the same function attempts to
 *         acquire the same lock more than once
 *
 * Params:
 *         resource: the HW LOCK Register name
 *         b_block: Try to get lock until succesful, or backout immediately on failure.
 * Return:
 *          Success - got the lock
 *          Fail - Invalid parameter or could not obtain the lock for our 1 sec in block mode
 *          or couldn't obtain lock one-shot in non block mode
 ******************************************************************************/
lm_status_t lm_hw_lock(lm_device_t*      pdev,
                       u32_t             resource,
                       u8_t              b_block)
{
    u32_t cnt                = 0;
    u32_t lock_status        = 0;
    u32_t const resource_bit = (1 << resource);
    u8_t  const func         = FUNC_ID(pdev);
    u32_t hw_lock_cntr_reg   = 0;

    // Validating the resource in within range
    if (resource > HW_LOCK_MAX_RESOURCE_VALUE)
    {
        DbgMessage1(pdev, FATAL , "lm_hw_lock: LM_STATUS_INVALID_PARAMETER resource=0x%x\n", resource);
        DbgBreakMsg("lm_hw_lock: LM_STATUS_INVALID_PARAMETER\n");
        return LM_STATUS_INVALID_PARAMETER;
    }
    if (func <= 5)
    {
        hw_lock_cntr_reg = MISC_REG_DRIVER_CONTROL_1 + (func * 8);
    }
    else
    {
        hw_lock_cntr_reg = MISC_REG_DRIVER_CONTROL_7 + ((func-6) * 8);
    }
    // Validating that the resource is not already taken
    lock_status = REG_RD(pdev, hw_lock_cntr_reg);
    if (lock_status & resource_bit)
    {
        DbgMessage2(pdev, FATAL , "lm_hw_lock: LM_STATUS_EXISTING_OBJECT lock_status=0x%x resource_bit=0x%x\n", lock_status, resource_bit);
        DbgBreakMsg("lm_hw_lock: LM_STATUS_EXISTING_OBJECT\n");
        return LM_STATUS_EXISTING_OBJECT;
    }
    // Try for 16 second every 50us
    for (cnt = 0; cnt < 320000; cnt++)
    {
        // Try to acquire the lock
        REG_WR(pdev, hw_lock_cntr_reg + 4, resource_bit);
        lock_status= REG_RD(pdev, hw_lock_cntr_reg);
        if (lock_status & resource_bit)
        {
            return LM_STATUS_SUCCESS;
        }
        if (!b_block)
        {
            return LM_STATUS_FAILURE;
        }
        mm_wait(pdev, 50);
    }
    DbgMessage(pdev, FATAL , "lm_hw_lock: LM_STATUS_TIMEOUT\n" );
    DbgBreakMsg("lm_hw_lock: FAILED LM_STATUS_TIMEOUT\n");
    return LM_STATUS_TIMEOUT;
}
/*******************************************************************************
 * Description:
 *         Releasing the HW lock for a specific resource.
 *         There is a HW attention in case the a function attempts to release
 *         a lock that it did not acquire
 * Return:
 *          Success - if the parameter is valid, the assumption is that it
 *                    will succeed
 *          Fail - Invalid parameter
 ******************************************************************************/
lm_status_t lm_hw_unlock(lm_device_t*      pdev,
                         u32_t             resource)
{
    u32_t lock_status        = 0;
    u32_t const resource_bit = (1 << resource);
    u8_t  const func         = FUNC_ID(pdev);
    u32_t hw_lock_cntr_reg   = 0;

    // Validating the resource in within range
    if (resource > HW_LOCK_MAX_RESOURCE_VALUE)
    {
        DbgMessage1(pdev, FATAL , "lm_hw_unlock: LM_STATUS_INVALID_PARAMETER resource=0x%x\n", resource);
        DbgBreakMsg("lm_hw_unlock: LM_STATUS_INVALID_PARAMETER\n");
        return LM_STATUS_INVALID_PARAMETER;
    }
    if (func <= 5)
    {
        hw_lock_cntr_reg = MISC_REG_DRIVER_CONTROL_1 + (func * 8);
    }
    else
    {
        hw_lock_cntr_reg = MISC_REG_DRIVER_CONTROL_7 + ((func-6) * 8);
    }
    // Validating that the resource is currently taken
    lock_status = REG_RD(pdev, hw_lock_cntr_reg);
    if (!(lock_status & resource_bit))
    {
        DbgMessage2(pdev, FATAL , "lm_hw_unlock: LM_STATUS_OBJECT_NOT_FOUND lock_status=0x%x resource_bit=0x%x\n", lock_status, resource_bit);
        DbgBreakMsg("lm_hw_unlock: LM_STATUS_OBJECT_NOT_FOUND\n");
        return LM_STATUS_OBJECT_NOT_FOUND;
    }
    REG_WR(pdev, hw_lock_cntr_reg, resource_bit);

    return LM_STATUS_SUCCESS;
}
/*******************************************************************************
 * Description:
 *         sends the mcp a keepalive to known registers
 * Return:
 ******************************************************************************/
lm_status_t lm_send_driver_pulse( lm_device_t* pdev )
{
    u32_t        msg_code   = 0;
    u32_t        drv_pulse  = 0;
    u32_t        mcp_pulse  = 0;
    if CHK_NULL(pdev)
    {
        return LM_STATUS_INVALID_PARAMETER ;
    }
    if GET_FLAGS(pdev->params.test_mode, TEST_MODE_NO_MCP)
    {
        return LM_STATUS_SUCCESS ;
    }
    ++pdev->vars.drv_pulse_wr_seq;
    msg_code = pdev->vars.drv_pulse_wr_seq & DRV_PULSE_SEQ_MASK;
    if GET_FLAGS(pdev->params.test_mode, TEST_MODE_DRIVER_PULSE_ALWAYS_ALIVE)
    {
        SET_FLAGS( msg_code, DRV_PULSE_ALWAYS_ALIVE ) ;
    }
    drv_pulse = msg_code;
    LM_SHMEM_WRITE(pdev,
                   OFFSETOF(shmem_region_t,
                   func_mb[FUNC_MAILBOX_ID(pdev)].drv_pulse_mb),msg_code);
    LM_SHMEM_READ(pdev,
                  OFFSETOF(shmem_region_t,
                  func_mb[FUNC_MAILBOX_ID(pdev)].mcp_pulse_mb),
                  &mcp_pulse);
    mcp_pulse&= MCP_PULSE_SEQ_MASK ;
    /* The delta between driver pulse and mcp response
     * should be 1 (before mcp response) or 0 (after mcp response)
    */
    if ((drv_pulse != mcp_pulse) &&
        (drv_pulse != ((mcp_pulse + 1) & MCP_PULSE_SEQ_MASK)))
    {
        DbgMessage2(pdev, FATAL , "drv_pulse (0x%x) != mcp_pulse (0x%x)\n", drv_pulse, mcp_pulse );
        return LM_STATUS_FAILURE ;
    }
    DbgMessage(pdev, INFORMi , "Sent driver pulse cmd to MCP\n");
    return LM_STATUS_SUCCESS ;
}
/*******************************************************************************
 * Description:
 *         Set driver pulse to MCP to always alive
 * Return:
 ******************************************************************************/
void lm_driver_pulse_always_alive(struct _lm_device_t* pdev)
{
    if CHK_NULL(pdev)
    {
        return;
    }
    if GET_FLAGS(pdev->params.test_mode, TEST_MODE_NO_MCP)
    {
        return ;
    }
    // Reset the MCP pulse to always alive
    LM_SHMEM_WRITE( pdev,
                    OFFSETOF(shmem_region_t,
                    func_mb[FUNC_MAILBOX_ID(pdev)].drv_pulse_mb),
                    DRV_PULSE_ALWAYS_ALIVE );
}
// entry that represents a function in the loader objcet
typedef struct _lm_loader_func_entry_t
{
    u8_t b_loaded ;   // does this function was loaded
} lm_loader_func_entry_t ;
// global object represents MCP - should be one per CHIP (boards)
typedef struct _lm_loader_path_obj_t
{
    u32_t*                   lock_ctx ;               // reserved - lock object context (currently not in use)
    lm_loader_func_entry_t   func_arr[E1H_FUNC_MAX] ; // array of function entries
} lm_loader_path_obj_t ;

typedef struct _lm_loader_obj_t
{
    u8_t                     lock_owner ;             // is a function acquire the lock? (1 based)
    lm_loader_path_obj_t path_arr[MAX_PATH_NUM] ;
} lm_loader_obj_t ;

lm_loader_obj_t g_lm_loader  = {0};

// TRUE if the function is first on the port
#define LM_LOADER_IS_FIRST_ON_PORT(_pdev,_path_idx,_port_idx) \
 ( (FALSE == g_lm_loader.path_arr[_path_idx].func_arr[_port_idx+0].b_loaded) && \
   (FALSE == g_lm_loader.path_arr[_path_idx].func_arr[_port_idx+2].b_loaded) && \
   (FALSE == g_lm_loader.path_arr[_path_idx].func_arr[_port_idx+4].b_loaded) && \
   (FALSE == g_lm_loader.path_arr[_path_idx].func_arr[_port_idx+6].b_loaded) )

// TRUE if the function is last on the port
#define LM_LOADER_IS_LAST_ON_PORT(_pdev,_path_idx,_port_idx) \
  ( ( ( FUNC_ID(_pdev) == (_port_idx+0) ) ? TRUE : (FALSE == g_lm_loader.path_arr[_path_idx].func_arr[(_port_idx+0)].b_loaded) ) && \
    ( ( FUNC_ID(_pdev) == (_port_idx+2) ) ? TRUE : (FALSE == g_lm_loader.path_arr[_path_idx].func_arr[(_port_idx+2)].b_loaded) ) && \
    ( ( FUNC_ID(_pdev) == (_port_idx+4) ) ? TRUE : (FALSE == g_lm_loader.path_arr[_path_idx].func_arr[(_port_idx+4)].b_loaded) ) && \
    ( ( FUNC_ID(_pdev) == (_port_idx+6) ) ? TRUE : (_port_idx == 0)?(FALSE == g_lm_loader.path_arr[_path_idx].func_arr[6].b_loaded):(FALSE == g_lm_loader.path_arr[_path_idx].func_arr[7].b_loaded) ) )


#define LM_LOADER_IS_FIRST_ON_COMMON(_pdev,_path_idx) (LM_LOADER_IS_FIRST_ON_PORT(_pdev,_path_idx,0) && LM_LOADER_IS_FIRST_ON_PORT(_pdev,_path_idx,1))
#define LM_LOADER_IS_LAST_ON_COMMON(_pdev,_path_idx)  (LM_LOADER_IS_LAST_ON_PORT(_pdev,_path_idx,0)  && LM_LOADER_IS_LAST_ON_PORT(_pdev,_path_idx,1))

#define LM_LOADER_IS_FIRST_ON_CHIP(_pdev) (LM_LOADER_IS_FIRST_ON_COMMON(_pdev,0) && LM_LOADER_IS_FIRST_ON_COMMON(_pdev,1))
#define LM_LOADER_IS_LAST_ON_CHIP(_pdev)  (LM_LOADER_IS_LAST_ON_COMMON(_pdev,0)  && LM_LOADER_IS_LAST_ON_COMMON(_pdev,1))

// Accessed only with lock!
// TRUE if any device is currently locked
#define LM_LOADER_IS_LOCKED(_chip_idx) ( (FALSE != g_lm_loader.lock_owner) )

/*
 *Function Name:lm_loader_opcode_to_mcp_msg
 *
 *Parameters:
 *      b_lock - true if it is lock false if unlock
 *Description:
 *      LM_LOADER_OPCODE_XXX-->DRV_MSG_CODE_XXX
 *Returns:
 *
 */
u32_t lm_loader_opcode_to_mcp_msg( lm_loader_opcode opcode, u8_t b_lock )
{
    u32_t mcp_msg = 0xffffffff ;
    switch(opcode)
    {
    case LM_LOADER_OPCODE_LOAD:
        mcp_msg = b_lock ? DRV_MSG_CODE_LOAD_REQ : DRV_MSG_CODE_LOAD_DONE ;
        break;
    case LM_LOADER_OPCODE_UNLOAD_WOL_EN:
        mcp_msg = b_lock ? DRV_MSG_CODE_UNLOAD_REQ_WOL_EN : DRV_MSG_CODE_UNLOAD_DONE ;
        break;
    case LM_LOADER_OPCODE_UNLOAD_WOL_DIS:
        mcp_msg = b_lock ? DRV_MSG_CODE_UNLOAD_REQ_WOL_DIS : DRV_MSG_CODE_UNLOAD_DONE ;
        break;
    case LM_LOADER_OPCODE_UNLOAD_WOL_MCP:
        mcp_msg = b_lock ? DRV_MSG_CODE_UNLOAD_REQ_WOL_MCP : DRV_MSG_CODE_UNLOAD_DONE ;
        break;
    default:
        DbgBreakIf(1) ;
        break;
    }
    return mcp_msg ;
}
/*
 *Function Name:mcp_resp_to_lm_loader_resp
 *
 *Parameters:
 *
 *Description:
 *      Translates mcp response to loader response FW_MSG_CODE_DRV_XXX->LM_LOADER_RESPONSE_XX
 *Returns:
 *
 */
lm_loader_response mcp_resp_to_lm_loader_resp( u32_t mcp_resp )
{
    lm_loader_response resp = LM_LOADER_RESPONSE_INVALID ;
    switch(mcp_resp)
    {
    case FW_MSG_CODE_DRV_LOAD_COMMON:
        resp = LM_LOADER_RESPONSE_LOAD_COMMON ;
        break;
    case FW_MSG_CODE_DRV_LOAD_COMMON_CHIP:
        resp = LM_LOADER_RESPONSE_LOAD_COMMON_CHIP ;
        break;
    case FW_MSG_CODE_DRV_LOAD_PORT:
        resp = LM_LOADER_RESPONSE_LOAD_PORT ;
        break;
    case FW_MSG_CODE_DRV_LOAD_FUNCTION:
        resp = LM_LOADER_RESPONSE_LOAD_FUNCTION ;
        break;
    case FW_MSG_CODE_DRV_UNLOAD_COMMON:
        resp = LM_LOADER_RESPONSE_UNLOAD_COMMON ;
        break;
    case FW_MSG_CODE_DRV_UNLOAD_PORT:
        resp = LM_LOADER_RESPONSE_UNLOAD_PORT ;
        break;
    case FW_MSG_CODE_DRV_UNLOAD_FUNCTION:
        resp = LM_LOADER_RESPONSE_UNLOAD_FUNCTION ;
        break;
    case FW_MSG_CODE_DRV_LOAD_DONE:
        resp = LM_LOADER_RESPONSE_LOAD_DONE ;
        break;
    case FW_MSG_CODE_DRV_UNLOAD_DONE:
        resp = LM_LOADER_RESPONSE_UNLOAD_DONE ;
        break;
    default:
        DbgMessage1(NULL, FATAL, "mcp_resp=0x%x\n", mcp_resp );
        DbgBreakIf(1) ;
        break;
    }
    return resp ;
}
// TBD - should it be the only indication??
#define IS_MCP_ON(_pdev) ( TEST_MODE_NO_MCP != GET_FLAGS(_pdev->params.test_mode, TEST_MODE_NO_MCP ) )

/*
 *Function Name:lm_loader_lock
 *
 *Parameters:
 *
 *Description:
 *     sync loading/unloading of port/funciton
 *Returns:
 *
 */
lm_loader_response lm_loader_lock( lm_device_t* pdev, lm_loader_opcode opcode )
{
    u32_t              mcp_msg        = 0;
    u32_t              fw_resp        = 0 ;
    lm_loader_response resp           = LM_LOADER_RESPONSE_INVALID ;
    lm_status_t        lm_status      = LM_STATUS_SUCCESS ;
    u32_t              wait_cnt       = 0 ;
    u32_t              wait_cnt_limit = 5000 ;
    if CHK_NULL(pdev)
    {
        return resp ;
    }
    if( IS_MCP_ON(pdev) )
    {
        mcp_msg   = lm_loader_opcode_to_mcp_msg( opcode, TRUE ) ;

        //we do this with no locks because acquiring the loader lock may take a long time (e.g in case another function takes a
        //long time to initialize we will only get a response from the MCP when it's done). We don't need a lock because interrupts
        //are disabled at this point and we won't get any IOCTLs.
        lm_status = lm_mcp_cmd_send_recieve_non_atomic( pdev, lm_mcp_mb_header, mcp_msg, 0, MCP_CMD_DEFAULT_TIMEOUT, &fw_resp ) ;
        if ( LM_STATUS_SUCCESS == lm_status )
        {
            resp = mcp_resp_to_lm_loader_resp(  fw_resp ) ;
            pdev->vars.b_in_init_reset_flow = TRUE;
        }
    }
    else // MCP_SIM
    {
        if( ERR_IF(PORT_ID(pdev) > 1) || ERR_IF(( FUNC_ID(pdev)) >= ARRSIZE(g_lm_loader.path_arr[PATH_ID(pdev)].func_arr)) )
        {
            DbgBreakMsg("Invalid PORT_ID/FUNC_ID\n");
            return resp ;
        }
        do
        {
            MM_ACQUIRE_LOADER_LOCK();
            if( LM_LOADER_IS_LOCKED(PATH_ID(pdev)) )
            {
                MM_RELEASE_LOADER_LOCK();
                mm_wait(pdev,20) ;
                DbgBreakIfAll( ++wait_cnt > wait_cnt_limit ) ;
            }
            else
            {
                // we'll release the lock when we are finish the work
                break;
            }
        }while(1) ;
        // Verify no one hold the lock, if so - it's a bug!
        DbgBreakIf( 0 != g_lm_loader.lock_owner ) ;

        // mark our current function id as owner
        g_lm_loader.lock_owner = FUNC_ID(pdev)+1 ;

        switch( opcode )
        {
        case LM_LOADER_OPCODE_LOAD:
            if( LM_LOADER_IS_FIRST_ON_CHIP(pdev) )
            {
                resp = LM_LOADER_RESPONSE_LOAD_COMMON_CHIP;
            }
            else if( LM_LOADER_IS_FIRST_ON_COMMON(pdev,PATH_ID(pdev)) )
            {
                resp = LM_LOADER_RESPONSE_LOAD_COMMON ;
            }
            else if( LM_LOADER_IS_FIRST_ON_PORT( pdev, PATH_ID(pdev), PORT_ID(pdev) ) )
            {
                resp = LM_LOADER_RESPONSE_LOAD_PORT ;
            }
            else
            {
                resp = LM_LOADER_RESPONSE_LOAD_FUNCTION ;
            }
            break;
        case LM_LOADER_OPCODE_UNLOAD_WOL_EN:
        case LM_LOADER_OPCODE_UNLOAD_WOL_DIS:
        case LM_LOADER_OPCODE_UNLOAD_WOL_MCP:
            if( LM_LOADER_IS_LAST_ON_COMMON(pdev,PATH_ID(pdev)) )
            {
                resp = LM_LOADER_RESPONSE_UNLOAD_COMMON ;
            }
            else if( LM_LOADER_IS_LAST_ON_PORT( pdev, PATH_ID(pdev), PORT_ID(pdev) ) )
            {
                resp = LM_LOADER_RESPONSE_UNLOAD_PORT ;
            }
            else
            {
                resp = LM_LOADER_RESPONSE_UNLOAD_FUNCTION ;
            }
            break;
        default:
            DbgBreakIf(1) ;
            break;
        }  // switch
        pdev->vars.b_in_init_reset_flow = TRUE;
        MM_RELEASE_LOADER_LOCK();
    } // MCP_SIM
    return resp ;
}
/*
 *Function Name:lm_loader_unlock
 *
 *Parameters:
 *
 *Description:
 *      sync loading/unloading of port/funciton
 *Returns:
 *
 */
lm_loader_response lm_loader_unlock( struct _lm_device_t *pdev, lm_loader_opcode opcode )
{
    u32_t              mcp_msg     = 0 ;
    lm_loader_response resp        = LM_LOADER_RESPONSE_INVALID ;
    u32_t              fw_resp     = 0 ;
    lm_status_t        lm_status   = LM_STATUS_SUCCESS ;
    u8_t               b_new_state = 0xff ;
    if CHK_NULL(pdev)
    {
        return resp ;
    }
    if( IS_MCP_ON(pdev) )
    {
        mcp_msg   = lm_loader_opcode_to_mcp_msg( opcode, FALSE ) ;
        //we do this with no locks because acquiring the loader lock may take a long time (e.g in case another function takes a
        //long time to initialize we will only get a response from the MCP when it's done). We don't need a lock because interrupts
        //are disabled at this point and we won't get any IOCTLs.
        lm_status = lm_mcp_cmd_send_recieve_non_atomic(pdev, lm_mcp_mb_header, mcp_msg, 0, MCP_CMD_DEFAULT_TIMEOUT, &fw_resp ) ;
        if ( LM_STATUS_SUCCESS == lm_status )
        {
            resp = mcp_resp_to_lm_loader_resp( fw_resp ) ;
            pdev->vars.b_in_init_reset_flow = FALSE;
        }
    }
    else // MCP_SIM
    {
        MM_ACQUIRE_LOADER_LOCK();

        // Verify current function id is the owner
        DbgBreakIf( g_lm_loader.lock_owner != FUNC_ID(pdev)+1 ) ;

        switch( opcode )
        {
        case LM_LOADER_OPCODE_LOAD:
            b_new_state = TRUE ;
            resp        = LM_LOADER_RESPONSE_LOAD_DONE ;
            break;
        case LM_LOADER_OPCODE_UNLOAD_WOL_EN:
        case LM_LOADER_OPCODE_UNLOAD_WOL_DIS:
        case LM_LOADER_OPCODE_UNLOAD_WOL_MCP:
            b_new_state = FALSE  ;
            resp        = LM_LOADER_RESPONSE_UNLOAD_DONE ;
            break;
        default:
            DbgBreakIf(1) ;
            break;
        }  // switch
        // verify new state differs than current
        DbgBreakIf(g_lm_loader.path_arr[PATH_ID(pdev)].func_arr[FUNC_ID(pdev)].b_loaded == b_new_state);

        // assign new state
        g_lm_loader.path_arr[PATH_ID(pdev)].func_arr[FUNC_ID(pdev)].b_loaded = b_new_state ;

        // mark we don't own the lock anymore
        g_lm_loader.lock_owner = FALSE ;

        pdev->vars.b_in_init_reset_flow = FALSE;
        MM_RELEASE_LOADER_LOCK();
    } // MCP_SIM
    return resp ;
}

/* Used for simulating a mcp reset where the mcp no longer knows the state of the uploaded drivers... */
void lm_loader_reset ( struct _lm_device_t *pdev )
{
    mm_memset(&g_lm_loader, 0, sizeof(g_lm_loader));
}

lm_status_t ecore_resc_alloc(struct _lm_device_t * pdev)
{
    pdev->ecore_info.gunzip_buf = mm_alloc_phys_mem(pdev, FW_BUF_SIZE, &pdev->ecore_info.gunzip_phys, PHYS_MEM_TYPE_NONCACHED, LM_RESOURCE_COMMON);
    if CHK_NULL(pdev->ecore_info.gunzip_buf)
    {
        return LM_STATUS_RESOURCE ;
    }
    return LM_STATUS_SUCCESS;
}
u32_t lm_get_max_supported_toe_cons(struct _lm_device_t *pdev)
{
    if ( CHK_NULL(pdev) )
    {
        return 0;
    }
    return pdev->params.max_supported_toe_cons;
}
u8_t lm_get_toe_rss_possibility(struct _lm_device_t *pdev)
{
    if ( CHK_NULL(pdev) )
    {
        return 0;
    }
    return (pdev->params.l4_rss_is_possible != L4_RSS_DISABLED);
}
u64_t lm_get_timestamp_of_recent_cid_recycling(struct _lm_device_t *pdev)
{
    return pdev->vars.last_recycling_timestamp;
}
lm_status_t
lm_set_cam_params(struct _lm_device_t * pdev,
                  u32_t mac_requestors_mask,
                  u32_t base_offset_in_cam_table,
                  u32_t cam_size,
                  u32_t mma_size,
                  u32_t mc_size)
{
    lm_status_t lm_status =  LM_STATUS_SUCCESS;
    if (IS_VFDEV(pdev)) {
        return LM_STATUS_FAILURE;
    }
    if (base_offset_in_cam_table != LM_KEEP_CURRENT_CAM_VALUE) {
        pdev->params.base_offset_in_cam_table = (u8_t)base_offset_in_cam_table;
    }
    if (cam_size != LM_KEEP_CURRENT_CAM_VALUE) {
        pdev->params.cam_size = (u8_t)cam_size;
    }
    if (mc_size != LM_KEEP_CURRENT_CAM_VALUE) {
        if (CHIP_IS_E1(pdev)) {
            pdev->params.mc_table_size[LM_CLI_IDX_NDIS]  =(u8_t) mc_size;
        } else {
            pdev->params.mc_table_size[LM_CLI_IDX_FCOE]  = (u8_t)mc_size;
        }
    }

    return lm_status;
} /* lm_set_cam_params */

/*******************************************************************************
 * Description: Reads the parametrs using elink interface
 *              Must be called under PHY_LOCK
 * Return:
 ******************************************************************************/
lm_status_t
lm_get_transceiver_data(struct _lm_device_t*     pdev,
                        b10_transceiver_data_t*  b10_transceiver_data )
{
    u16_t eeprom_data[][2] = { { ELINK_SFP_EEPROM_VENDOR_NAME_ADDR, ELINK_SFP_EEPROM_VENDOR_NAME_SIZE},
                               { ELINK_SFP_EEPROM_PART_NO_ADDR,     ELINK_SFP_EEPROM_PART_NO_SIZE},
                               { ELINK_SFP_EEPROM_SERIAL_ADDR,      ELINK_SFP_EEPROM_SERIAL_SIZE},
                               { ELINK_SFP_EEPROM_REVISION_ADDR,    ELINK_SFP_EEPROM_REVISION_SIZE},
                               { ELINK_SFP_EEPROM_DATE_ADDR,        ELINK_SFP_EEPROM_DATE_SIZE} } ;

    u8_t        vendor_name  [ELINK_SFP_EEPROM_VENDOR_NAME_SIZE] = {0};
    u8_t        model_num    [ELINK_SFP_EEPROM_PART_NO_SIZE]     = {0};
    u8_t        serial_num   [ELINK_SFP_EEPROM_SERIAL_SIZE]      = {0};
    u8_t        revision_num [ELINK_SFP_EEPROM_REVISION_SIZE]    = {0};
    u8_t        mfg_date     [ELINK_SFP_EEPROM_DATE_SIZE]        = {0};
    u8_t*       ptr_arr[ARRSIZE(eeprom_data)]                    = {0}; // for convinence of coding
    u8_t        idx                                              = 0;
    u8_t        elink_res                                        = ELINK_STATUS_ERROR;
    u8_t        ext_phy_type                                     = 0;
    lm_status_t lm_status                                        = LM_STATUS_SUCCESS;

    // we use local variables (vendor_name, model_num etc...) to protect flows in IA64
    // that upper layer might send us non-aligned to u16_t pointer, in this case a BSOD might occur.
    // using local variables and than memcpy prevent such situation.

    if CHK_NULL( b10_transceiver_data )
    {
        return LM_STATUS_INVALID_PARAMETER;
    }

    ASSERT_STATIC( sizeof(b10_transceiver_data->vendor_name)  == sizeof(vendor_name) ) ;
    ASSERT_STATIC( sizeof(b10_transceiver_data->model_num)    == sizeof(model_num) ) ;
    ASSERT_STATIC( sizeof(b10_transceiver_data->serial_num)   == sizeof(serial_num) ) ;
    ASSERT_STATIC( sizeof(b10_transceiver_data->revision_num) == sizeof(revision_num) ) ;
    ASSERT_STATIC( sizeof(b10_transceiver_data->mfg_date)     == sizeof(mfg_date) ) ;

    mm_mem_zero( b10_transceiver_data, sizeof( b10_transceiver_data_t ) ) ;

    ptr_arr[0] = &vendor_name[0];
    ptr_arr[1] = &model_num[0];
    ptr_arr[2] = &serial_num[0];
    ptr_arr[3] = &revision_num[0];
    ptr_arr[4] = &mfg_date[0];

    if( pdev->params.link.num_phys > ELINK_MAX_PHYS )
    {
        DbgBreakIf(1);
        return LM_STATUS_FAILURE;
    }

    // query from elink all ext_phy types (currently 1 and 2)
    for( ext_phy_type = ELINK_EXT_PHY1; ext_phy_type < pdev->params.link.num_phys; ext_phy_type++ )
    {
        if( ELINK_ETH_PHY_SFP_FIBER == pdev->params.link.phy[ext_phy_type].media_type ||
	    ELINK_ETH_PHY_DA_TWINAX == pdev->params.link.phy[ext_phy_type].media_type)
        {
            // only in case not SFP+ - the elink query is supported
            for( idx = 0; idx < ARRSIZE(eeprom_data) ; idx++ )
            {
                PHY_HW_LOCK(pdev);
                elink_res = elink_read_sfp_module_eeprom( &pdev->params.link.phy[ext_phy_type], // ELINK_INT_PHY, ELINK_EXT_PHY1, ELINK_EXT_PHY2
                                                          &pdev->params.link,
                                                          eeprom_data[idx][0],
                                                          (u8_t)eeprom_data[idx][1],
                                                          ptr_arr[idx] ) ;
                PHY_HW_UNLOCK(pdev);
                if( ELINK_STATUS_OK != elink_res )
                {
                    // We assume that if one of the queries failed - there is an error so we break this loop
                    break;
                }

            } // for "eeprom_data" size

            // only one sfp+ module is expected on board so we exit the ext_phy_type loop
            break;

        } // ELINK_ETH_PHY_SFP_FIBER == media_type

    } // for "ext_phy_type"

    switch(elink_res)
    {
    case ELINK_STATUS_OK:
        {
            b10_transceiver_data->ver_num = TRANSCEIVER_DATA_VER_NUM;

            mm_memcpy( b10_transceiver_data->vendor_name,  &vendor_name[0],  sizeof(vendor_name) );
            mm_memcpy( b10_transceiver_data->model_num,    &model_num[0],    sizeof(model_num) );
            mm_memcpy( b10_transceiver_data->serial_num,   &serial_num[0],   sizeof(serial_num) );
            mm_memcpy( b10_transceiver_data->revision_num, &revision_num[0], sizeof(revision_num) );
            mm_memcpy( b10_transceiver_data->mfg_date,     &mfg_date[0],     sizeof(mfg_date) );
        }
        lm_status = LM_STATUS_SUCCESS;
        break;

    case ELINK_STATUS_TIMEOUT:
        lm_status = LM_STATUS_TIMEOUT;
        break;

    case ELINK_STATUS_ERROR:
    default:
        lm_status = LM_STATUS_FAILURE;
        break;
    }// switch elink_res

    return lm_status;

} /* lm_get_transceiver_data */

/*******************************************************************************
 * Description: turn led on/off/operational mode
 *              Must be called under PHY_LOCK
 * Return:
 ******************************************************************************/
lm_status_t
lm_set_led_wrapper(struct _lm_device_t*     pdev,
                   const   u8_t             led_mode )
{
    u8_t        elink_res = ELINK_STATUS_OK;
    lm_status_t lm_status = LM_STATUS_SUCCESS;

    PHY_HW_LOCK(pdev);
    elink_res = elink_set_led( &pdev->params.link, &pdev->vars.link, led_mode, pdev->vars.link.line_speed );
    PHY_HW_UNLOCK(pdev);

    switch(elink_res)
    {
    case ELINK_STATUS_OK:
        lm_status = LM_STATUS_SUCCESS;
        break;

    case ELINK_STATUS_ERROR:
    default:
        lm_status = LM_STATUS_FAILURE;
        break;
    }// switch elink_res

    return lm_status;
} /* lm_set_led */

u32_t lm_get_num_of_cashed_grq_bds(struct _lm_device_t *pdev)
{
    return USTORM_TOE_GRQ_CACHE_NUM_BDS;
}

/*
 *Function Name: lm_get_port_id_from_func_abs
 *
 *Parameters:
 *
 *Description:
 *  returns the port ID according to the func_abs_id
 * E1/E1.5:
 * Port0: 0,2,4,6
 * Port1: 1,3,5,7
 *
 * E2/E32P
 * Port0: 0,1,2,3,4,5,6,7
 *
 * E34P
 * Port0: 0,1,4,5
 * Port1: 2,3,6,7
 *
 *Returns: u8_t port_id
 *
 */
u8_t lm_get_port_id_from_func_abs( const u32_t chip_num,  const lm_chip_port_mode_t lm_chip_port_mode, const u8_t abs_func )
{
    u8_t port_id     = 0xff;
    u8_t modulus_res = 0;

    do
    {
        if( CHIP_IS_E1x_PARAM( chip_num ) )
        {
            port_id = abs_func % PORT_MAX;
            break;
        }

        switch( lm_chip_port_mode )
        {
        case LM_CHIP_PORT_MODE_2:
            {
                // we expect here only E2 or E3
                DbgBreakIf( CHIP_IS_E1x_PARAM( chip_num ) );
                port_id = 0;
            }
            break;

        case LM_CHIP_PORT_MODE_4:
            {
                modulus_res = abs_func % 4;
                switch (modulus_res)
                {
                case 0:
                case 1:
                    port_id = 0;
                    break;
                case 2:
                case 3:
                    port_id = 1;
                    break;
                default:
                    break;
                }
            }
            break;

        default:
            DbgBreakIf(TRUE);
            break;
        } // switch lm_chip_port_mode
    }while(0);

    return port_id;
} /* lm_get_port_id_from_func_abs */

/*
 *Function Name: lm_get_abs_func_vector
 *
 *Parameters:
 *
 *Description:
 *  returns vector of abs_func id's upon parameters
 *
 *Returns: u32_t abs_func_vector
 *
 */
u8_t lm_get_abs_func_vector( const u32_t chip_num,  const lm_chip_port_mode_t chip_port_mode, const u8_t b_multi_vnics_mode, const u8_t path_id )
{
    u8_t abs_func_vector = 0;

    // TODO VF for T7.0

/*
    The following table is mapping between abs func, ports and paths

    |-----------------------------------------------|
    |[#]| CHIP & Mode | PATH(s) | Port(s) | Func(s) |
    |---|-------------|---------|---------|---------|
    |[1]| E1.0 (SF)   |   (0)   |   0,1   |  (0,1)  |
    |   | E1.5  SF    |         |   0,1   |  (0,1)  | (port is same as func)
    |---|-------------|---------|---------|---------|
    |[2]| E1.5 MF     |   (0)   |   0,1   |   0-7   | 0,1,2,3,4,5,6,7 (port is %2 of func)
    |---|-------------|---------|---------|---------|
    |[3]| E2/E32P SF  |   0,1   |   0     |   --->  | (Path 0) 0        | (Path 1) 1
    |---|-------------|---------|---------|---------|
    |[4]| E2/E32P MF  |   0,1   |   0     |   --->  | (Path 0) 0,2,4,6  | (Path 1) 1,3,5,7
    |---|-------------|---------|---------|---------|
    |[5]| E34P SF     |   0,1   |   0,1   |   --->  | (Path 0) 0:port0 2:port1     | (Path 1) 1:port0 3:port1
    |---|-------------|---------|---------|---------|
    |[6]| E34P MF     |   0,1   |   0,1   |   --->  | (Path 0) 0,4:port0 2,6:port1 | (Path 1) 1,5:port0 3,7:port1
    |---|-------------|---------|---------|---------|
*/
    do
    {
        // [1]
        if( CHIP_IS_E1x_PARAM(chip_num) && !b_multi_vnics_mode )
        {
            SET_BIT( abs_func_vector, 0 );
            SET_BIT( abs_func_vector, 1 );
            break;
        }

        // [2]
        if( CHIP_IS_E1H_PARAM(chip_num) && b_multi_vnics_mode )
        {
            SET_BIT( abs_func_vector, 0 );
            SET_BIT( abs_func_vector, 1 );
            SET_BIT( abs_func_vector, 2 );
            SET_BIT( abs_func_vector, 3 );
            SET_BIT( abs_func_vector, 4 );
            SET_BIT( abs_func_vector, 5 );
            SET_BIT( abs_func_vector, 6 );
            SET_BIT( abs_func_vector, 7 );
            break;
        }

        // If we got here chip should not be ealier than E2
        DbgBreakIf( CHIP_IS_E1x_PARAM(chip_num) );

        // [3] [4] [5] [6]
        switch ( chip_port_mode )
        {
        case LM_CHIP_PORT_MODE_2:
            {
                // we expect here only E2 or E3
                DbgBreakIf( !CHIP_IS_E2_PARAM(chip_num) && !CHIP_IS_E3_PARAM(chip_num) );

                if( b_multi_vnics_mode )
                {
                    // [4]
                    SET_BIT( abs_func_vector, (0 + path_id) );
                    SET_BIT( abs_func_vector, (2 + path_id) );
                    SET_BIT( abs_func_vector, (4 + path_id) );
                    SET_BIT( abs_func_vector, (6 + path_id) );
                    break;
                }
                else
                {
                    // [3]
                    SET_BIT( abs_func_vector, path_id );
                    break;
                }
            } // LM_CHIP_PORT_MODE_2
            break;


        case LM_CHIP_PORT_MODE_4:
            {
                if( b_multi_vnics_mode )
                {
                    // [6]
                    // No operational support
                    DbgBreakIf( TRUE );
                    break;
                }
                else
                {
                    // [5]
                    SET_BIT( abs_func_vector, (0 + path_id) );
                    SET_BIT( abs_func_vector, (2 + path_id) );
                    break;
                }
            } // LM_CHIP_PORT_MODE_4
            break;

        default:
            {
                DbgBreakIf(TRUE);
                break;
            }
        } // CHIP_PORT_MODE
    }while(0);

    return abs_func_vector;
} /* lm_get_abs_func_vector */
