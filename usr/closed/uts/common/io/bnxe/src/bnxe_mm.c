
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

#define BNXE_DEF_TX_BD_PAGE_CNT  12
#define BNXE_DEF_TX_COAL_BUF_CNT 10

typedef struct
{
    int bufCnt;
    int txBdPageCnt;
    int txCoalBufCnt;
} BnxeHwPageConfig;

static BnxeHwPageConfig bnxeHwPageConfigs[] =
{
    /* Buffers   TX BD Pages   TX Coalesce Bufs */
    {  1000,     4,            10 },
    {  1500,     6,            10 },
    {  3000,     12,           10 },
    {  0,        0,            0 }
};

#if 0
#define MEM_LOG BnxeLogInfo
#else
#define MEM_LOG
#endif

ddi_device_acc_attr_t bnxeAccessAttribBAR =
{
    DDI_DEVICE_ATTR_V0,   /* devacc_attr_version */
    DDI_STRUCTURE_LE_ACC, /* devacc_attr_endian_flags */
    DDI_STRICTORDER_ACC,  /* devacc_attr_dataorder */
    DDI_DEFAULT_ACC       /* devacc_attr_access */
};

ddi_device_acc_attr_t bnxeAccessAttribBUF =
{
    DDI_DEVICE_ATTR_V0,   /* devacc_attr_version */
    DDI_NEVERSWAP_ACC,    /* devacc_attr_endian_flags */
    DDI_STRICTORDER_ACC,  /* devacc_attr_dataorder */
    DDI_DEFAULT_ACC       /* devacc_attr_access */
};

ddi_dma_attr_t bnxeDmaPageAttrib =
{
    DMA_ATTR_V0,         /* dma_attr_version */
    0,                   /* dma_attr_addr_lo */
    0xffffffffffffffff,  /* dma_attr_addr_hi */
    0xffffffffffffffff,  /* dma_attr_count_max */
    0,                   /* dma_attr_align */
    0xffffffff,          /* dma_attr_burstsizes */
    1,                   /* dma_attr_minxfer */
    0xffffffffffffffff,  /* dma_attr_maxxfer */
    0xffffffffffffffff,  /* dma_attr_seg */
    1,                   /* dma_attr_sgllen */
    1,                   /* dma_attr_granular */
    0,                   /* dma_attr_flags */
};


void mm_wait(lm_device_t * pDev,
             u32_t         delayUs)
{
    (void)pDev;
    drv_usecwait(delayUs);
}


lm_status_t mm_read_pci(lm_device_t * pDev,
                        u32_t         pciReg,
                        u32_t *       pRegValue)
{
    um_device_t * pUM = (um_device_t *)pDev;

    *pRegValue = pci_config_get32(pUM->pPciCfg, (off_t)pciReg);

    return LM_STATUS_SUCCESS;
}


lm_status_t mm_write_pci(lm_device_t * pDev,
                         u32_t         pciReg,
                         u32_t         regValue)
{
    um_device_t * pUM = (um_device_t *)pDev;

    pci_config_put32(pUM->pPciCfg, (off_t)pciReg, regValue);

    return LM_STATUS_SUCCESS;
}


void BnxeInitBdCnts(um_device_t * pUM,
                    int           cli_idx)
{
    lm_device_t *      pLM = (lm_device_t *)pUM;
    BnxeHwPageConfig * pPageCfg;

    pLM->params.l2_tx_bd_page_cnt[cli_idx]  = BNXE_DEF_TX_BD_PAGE_CNT;
    pLM->params.l2_tx_coal_buf_cnt[cli_idx] = BNXE_DEF_TX_COAL_BUF_CNT;

    pPageCfg = &bnxeHwPageConfigs[0];
    while (pPageCfg->bufCnt)
    {
        if (pLM->params.l2_rx_desc_cnt[cli_idx] <= pPageCfg->bufCnt)
        {
            pLM->params.l2_tx_bd_page_cnt[cli_idx]  = pPageCfg->txBdPageCnt;
            pLM->params.l2_tx_coal_buf_cnt[cli_idx] = pPageCfg->txCoalBufCnt;
            break;
        }

        pPageCfg++;
    }
}


extern u32_t LOG2(u32_t v);
unsigned long log2_align(unsigned long n);

lm_status_t mm_get_user_config(lm_device_t * pLM)
{
    um_device_t * pUM = (um_device_t *)pLM;
    u32_t         total_size;
    u32_t         required_page_size;

    BnxeCfgInit(pUM);

    pLM->params.ofld_cap = (LM_OFFLOAD_TX_IP_CKSUM   |
                            LM_OFFLOAD_RX_IP_CKSUM   |
                            LM_OFFLOAD_TX_TCP_CKSUM  |
                            LM_OFFLOAD_RX_TCP_CKSUM  |
                            LM_OFFLOAD_TX_TCP6_CKSUM |
                            LM_OFFLOAD_RX_TCP6_CKSUM |
                            LM_OFFLOAD_TX_UDP_CKSUM  |
                            LM_OFFLOAD_RX_UDP_CKSUM  |
                            LM_OFFLOAD_TX_UDP6_CKSUM |
                            LM_OFFLOAD_RX_UDP6_CKSUM);

    /* XXX Wake on LAN? */
    //pLM->params.wol_cap = (LM_WAKE_UP_MODE_MAGIC_PACKET | LM_WAKE_UP_MODE_NWUF);

    /* keep the VLAN tag in the mac header when receiving */
    pLM->params.keep_vlan_tag = 1;

    /* set in BnxeIntrInit based on the allocated number of MSIX interrupts */
    //pLM->params.rss_chain_cnt = pUM->devParams.numRings;
    //pLM->params.tss_chain_cnt = pUM->devParams.numRings;

    pLM->params.l2_rx_desc_cnt[LM_CLI_IDX_NDIS] = pUM->devParams.numRxDesc[LM_CLI_IDX_NDIS];
    pLM->params.l2_tx_bd_page_cnt[LM_CLI_IDX_NDIS] = 0;
    pLM->params.l2_tx_coal_buf_cnt[LM_CLI_IDX_NDIS] = 0;

    BnxeInitBdCnts(pUM, LM_CLI_IDX_NDIS);

    pLM->params.l2_rx_desc_cnt[LM_CLI_IDX_FWD] = 0;
    pLM->params.l2_tx_bd_page_cnt[LM_CLI_IDX_FWD] = 0;
    pLM->params.l2_tx_coal_buf_cnt[LM_CLI_IDX_FWD] = 0;

    pLM->params.l2_rx_desc_cnt[LM_CLI_IDX_ISCSI] = 0;
    pLM->params.l2_tx_bd_page_cnt[LM_CLI_IDX_ISCSI] = 0;
    pLM->params.l2_tx_coal_buf_cnt[LM_CLI_IDX_ISCSI] = 0;

    pLM->params.l2_rx_desc_cnt[LM_CLI_IDX_RDMA] = 0;
    pLM->params.l2_tx_bd_page_cnt[LM_CLI_IDX_RDMA] = 0;
    pLM->params.l2_tx_coal_buf_cnt[LM_CLI_IDX_RDMA] = 0;

    pLM->params.l2_rx_desc_cnt[LM_CLI_IDX_FCOE] = 0;
    pLM->params.l2_tx_bd_page_cnt[LM_CLI_IDX_FCOE] = 0;
    pLM->params.l2_tx_coal_buf_cnt[LM_CLI_IDX_FCOE] = 0;

    pLM->params.max_func_toe_cons    = 0;
    pLM->params.max_func_iscsi_cons  = 0;
    pLM->params.max_func_rdma_cons   = 0;
    pLM->params.max_func_fcoe_cons   = pUM->lm_dev.hw_info.max_port_fcoe_conn;
    pLM->params.max_func_connections =
        log2_align(pLM->params.max_func_toe_cons +
                   pLM->params.max_func_rdma_cons +
                   pLM->params.max_func_iscsi_cons +
                   pLM->params.max_func_fcoe_cons +
                   MAX_ETH_CONS);

    /* determine: 1. itl_client_page_size, #context in page*/

    /* based on PCIe block INIT document */

    /* We now need to calculate the page size based on the maximum number of
     * connections supported. Since this property is identical to all ports, and
     * is configured in COMMON registers, we need to use the maximum number of
     * connections in all ports. */

    /* The L2P table is used to map logical addresses to physical ones. There
     * are four clients that use this table.  We want to use only the ILT
     * (Internal), we need to calculate the total size required for all clients,
     * divide it by the number of entries in the ILT table and that will give us
     * the page size we want. The following table describes the needs of each of
     * these clients:
     *
     *  HW block(L2P client)    Area name       Size [B]
     *  Searcher                T1              ROUNDUP(LOG2(N)) * 64
     *  Timers                  Linear Array    N * 8
     *  QM                      Queues          N * 32 * 4
     *  CDU                     Context         N * S + W * ROUNDUP (N/m)  (W=0)
     *
     * N: Number of connections
     * S: Context Size
     * W: Block Waste (not really interesting) we configure the context size to
     *    be a power of 2.
     * m: Number of cids in a block (not really interesting, since W will always
     *    be 0)
     */
    total_size = (pLM->hw_info.max_common_conns *
                  (SEARCHER_TOTAL_MEM_REQUIRED_PER_CON +
                   TIMERS_TOTAL_MEM_REQUIRED_PER_CON +
                   QM_TOTAL_MEM_REQUIRED_PER_CON +
                   pLM->params.context_line_size));

    required_page_size = (total_size / ILT_NUM_PAGE_ENTRIES_PER_FUNC);
    required_page_size = (2 << LOG2(required_page_size));

    if (required_page_size < LM_PAGE_SIZE)
    {
        required_page_size = LM_PAGE_SIZE;
    }

    pLM->params.ilt_client_page_size = required_page_size;
    pLM->params.num_context_in_page  = (pLM->params.ilt_client_page_size /
                                        pLM->params.context_line_size);

    if (pUM->devParams.intrCoalesce)
    {
        pLM->params.int_coalesing_mode = LM_INT_COAL_PERIODIC_SYNC;
        pLM->params.int_per_sec_rx_override = pUM->devParams.intrRxPerSec;
        pLM->params.int_per_sec_tx_override = pUM->devParams.intrTxPerSec;
    }
    else
    {
        pLM->params.int_coalesing_mode = LM_INT_COAL_NONE;
    }

    pLM->params.enable_dynamic_hc[0] = 0;
    pLM->params.enable_dynamic_hc[1] = 0;
    pLM->params.enable_dynamic_hc[2] = 0;
    pLM->params.enable_dynamic_hc[3] = 0;

    pLM->params.l2_fw_flow_ctrl = (pUM->devParams.l2_fw_flow_ctrl) ? 1 : 0;

    pLM->params.rcv_buffer_offset = BNXE_DMA_RX_OFFSET; 

    pLM->params.debug_cap_flags = DEFAULT_DEBUG_CAP_FLAGS_VAL;

    pLM->params.max_fcoe_task = lm_fc_max_fcoe_task_sup(pLM);

    pLM->params.validate_sq_complete = 1;

    return LM_STATUS_SUCCESS;
}


static boolean_t BnxeIsBarUsed(um_device_t * pUM,
                               int           regNumber,
                               offset_t      offset,
                               u32_t         size)
{
    BnxeMemRegion * pMem;

    BNXE_LOCK_ENTER_MEM(pUM);

    pMem = (BnxeMemRegion *)d_list_peek_head(&pUM->memRegionList);

    while (pMem)
    {
        if ((pMem->regNumber == regNumber) &&
            (pMem->offset == offset) &&
            (pMem->size == size))
        {
            BNXE_LOCK_EXIT_MEM(pUM);
            return B_TRUE;
        }

        pMem = (BnxeMemRegion *)d_list_next_entry(D_LINK_CAST(pMem));
    }

    BNXE_LOCK_EXIT_MEM(pUM);
    return B_FALSE;
}


void * mm_map_io_base(lm_device_t * pLM,
                      lm_address_t  baseAddr,
                      u32_t         size,
                      u8_t          bar)
{
    um_device_t *   pUM = (um_device_t *)pLM;
    BnxeMemRegion * pMem;
    //int             numRegs;
    off_t           regSize;
    int rc;

    /*
     * Solaris identifies:
     *   BAR 0 - size 0 (pci config regs?)
     *   BAR 1 - size 0x800000  (Everest 1/2 LM BAR 0)
     *   BAR 2 - size 0x4000000 (Everest 1   LM BAR 1)
     *                0x800000  (Everest 2   LM BAR 1)
     *   BAR 3 - size 0x10000   (Everest 2   LM BAR 2)
     */
    bar++;

    //ddi_dev_nregs(pUM->pDev, &numRegs);

    ddi_dev_regsize(pUM->pDev, bar, &regSize);

    if ((size > regSize) || BnxeIsBarUsed(pUM, bar, 0, size))
    {
        BnxeLogWarn(pUM, "BAR %d at offset %d and size %d is already being used!",
                    bar, 0, (int)regSize);
        return NULL;
    }

    if ((pMem = kmem_zalloc(sizeof(BnxeMemRegion), KM_NOSLEEP)) == NULL)
    {
        BnxeLogWarn(pUM, "Memory allocation for BAR %d at offset %d and size %d failed!",
                    bar, 0, (int)regSize);
        return NULL;
    }

    if ((rc = ddi_regs_map_setup(pUM->pDev,
                                 bar, // bar number
                                 &pMem->pRegAddr,
                                 0, // region map offset,
                                 size, // region memory window size (0=all)
                                 &bnxeAccessAttribBAR,
                                 &pMem->regAccess)) != DDI_SUCCESS)
    {
        BnxeLogWarn(pUM, "Failed to memory map device (BAR=%d, offset=%d, size=%d) (%d)",
                    bar, 0, size, rc);
        kmem_free(pMem, sizeof(BnxeMemRegion));
        return NULL;
    }

    pMem->baseAddr  = baseAddr;
    pMem->regNumber = bar;
    pMem->offset    = 0;
    pMem->size      = size;

    BNXE_LOCK_ENTER_MEM(pUM);
    d_list_push_head(&pUM->memRegionList, D_LINK_CAST(pMem));
    BNXE_LOCK_EXIT_MEM(pUM);

    bar--;
    pLM->vars.reg_handle[bar] = pMem->regAccess;

    return pMem->pRegAddr;
}


void * mm_map_io_space_solaris(lm_device_t *      pLM,
                               lm_address_t       physAddr,
                               u8_t               bar,
                               u32_t              offset,
                               u32_t              size,
                               ddi_acc_handle_t * pRegAccHandle)
{
    um_device_t *   pUM = (um_device_t *)pLM;
    BnxeMemRegion * pMem;
    off_t           regSize;
    int rc;

    /* see bar mapping described in mm_map_io_base above */
    bar++;

    ddi_dev_regsize(pUM->pDev, bar, &regSize);

    if ((size > regSize) || BnxeIsBarUsed(pUM, bar, offset, size))
    {
        BnxeLogWarn(pUM, "BAR %d at offset %d and size %d is already being used!",
                    bar, offset, (int)regSize);
        return NULL;
    }

    if ((pMem = kmem_zalloc(sizeof(BnxeMemRegion), KM_NOSLEEP)) == NULL)
    {
        BnxeLogWarn(pUM, "Memory allocation for BAR %d at offset %d and size %d failed!",
                    bar, offset, (int)regSize);
        return NULL;
    }

    if ((rc = ddi_regs_map_setup(pUM->pDev,
                                 bar, // bar number
                                 &pMem->pRegAddr,
                                 offset, // region map offset,
                                 size, // region memory window size (0=all)
                                 &bnxeAccessAttribBAR,
                                 pRegAccHandle)) != DDI_SUCCESS)
    {
        BnxeLogWarn(pUM, "Failed to memory map device (BAR=%d, offset=%d, size=%d) (%d)",
                    bar, offset, size, rc);
        kmem_free(pMem, sizeof(BnxeMemRegion));
        return NULL;
    }

    pMem->baseAddr  = physAddr;
    pMem->regNumber = bar;
    pMem->offset    = offset;
    pMem->size      = size;
    pMem->regAccess = *pRegAccHandle;

    BNXE_LOCK_ENTER_MEM(pUM);
    d_list_push_head(&pUM->memRegionList, D_LINK_CAST(pMem));
    BNXE_LOCK_EXIT_MEM(pUM);

    return pMem->pRegAddr;
}


void mm_unmap_io_space(lm_device_t * pLM,
                       void *        pVirtAddr,
                       u32_t         size)
{
    um_device_t *   pUM = (um_device_t *)pLM;
    BnxeMemRegion * pMemRegion;

    BNXE_LOCK_ENTER_MEM(pUM);

    pMemRegion = (BnxeMemRegion *)d_list_peek_head(&pUM->memRegionList);

    while (pMemRegion)
    {
        if ((pMemRegion->pRegAddr == pVirtAddr) &&
            (pMemRegion->size == size))
        {
            d_list_remove_entry(&pUM->memRegionList, D_LINK_CAST(pMemRegion));
            ddi_regs_map_free(&pMemRegion->regAccess);
            kmem_free(pMemRegion, sizeof(BnxeMemRegion));
            break;
        }

        pMemRegion = (BnxeMemRegion *)d_list_next_entry(D_LINK_CAST(pMemRegion));
    }

    BNXE_LOCK_EXIT_MEM(pUM);
}


void * mm_alloc_mem(lm_device_t * pLM,
                    u32_t         memSize,
                    u8_t          cli_idx)
{
    um_device_t *  pUM = (um_device_t *)pLM;
    BnxeMemBlock * pMem;
    void *         pBuf;
    u32_t *        pTmp;
    int i;

    (void)cli_idx;

    if ((pMem = kmem_zalloc(sizeof(BnxeMemBlock), KM_NOSLEEP)) == NULL)
    {
        return NULL;
    }

    /* allocated space for header/trailer checks */
    memSize += (BNXE_MEM_CHECK_LEN * 2);

    MEM_LOG(pUM, "*** MEM: %8u", memSize);

    if ((pBuf = kmem_zalloc(memSize, KM_NOSLEEP)) == NULL)
    {
        BnxeLogWarn(pUM, "Failed to allocate memory");
        kmem_free(pMem, sizeof(BnxeMemBlock));
        return NULL;
    }

    /* fill in the header check */
    for (i = 0, pTmp = (u32_t *)pBuf;
         i < BNXE_MEM_CHECK_LEN;
         i += 4, pTmp++)
    {
        *pTmp = BNXE_MAGIC;
    }

    /* fill in the trailer check */
    for (i = 0, pTmp = (u32_t *)((char *)pBuf + memSize - BNXE_MEM_CHECK_LEN);
         i < BNXE_MEM_CHECK_LEN;
         i += 4, pTmp++)
    {
        *pTmp = BNXE_MAGIC;
    }

    pMem->size = memSize;
    pMem->pBuf = pBuf;

    BNXE_LOCK_ENTER_MEM(pUM);
    d_list_push_head(&pUM->memBlockList, D_LINK_CAST(pMem));
    BNXE_LOCK_EXIT_MEM(pUM);

    MEM_LOG(pUM, "Allocated %d byte block virt:%p",
            memSize, ((char *)pBuf + BNXE_MEM_CHECK_LEN));

    return ((char *)pBuf + BNXE_MEM_CHECK_LEN);
}


void * mm_alloc_phys_mem_align(lm_device_t *  pLM,
                               u32_t          memSize,
                               lm_address_t * pPhysAddr,
                               u32_t          alignment,
                               u8_t           memType,
                               u8_t           cli_idx)
{
    um_device_t * pUM = (um_device_t *)pLM;
    int rc;
    caddr_t pBuf;
    size_t length;
    unsigned int count;
    ddi_dma_attr_t     dmaAttrib;
    ddi_dma_handle_t * pDmaHandle;
    ddi_acc_handle_t * pDmaAccHandle;
    ddi_dma_cookie_t cookie;
    BnxeMemDma * pMem;
    size_t size;

    (void)memType;
    (void)cli_idx;

    if (memSize == 0)
    {
        return NULL;
    }

    if ((pMem = kmem_zalloc(sizeof(BnxeMemDma), KM_NOSLEEP)) == NULL)
    {
        return NULL;
    }

    dmaAttrib                = bnxeDmaPageAttrib;
    dmaAttrib.dma_attr_align = alignment;

    pDmaHandle    = &pMem->dmaHandle;
    pDmaAccHandle = &pMem->dmaAccHandle;

    size  = memSize;
    size += (alignment - 1);
    size &= ~((u32_t)(alignment - 1));

    MEM_LOG(pUM, "*** DMA: %8u (%4d) - %8u", memSize, alignment, size);

    if ((rc = ddi_dma_alloc_handle(pUM->pDev,
                                   &dmaAttrib,
                                   DDI_DMA_DONTWAIT,
                                   (void *)0,
                                   pDmaHandle)) != DDI_SUCCESS)
    {
        BnxeLogWarn(pUM, "Failed to alloc DMA handle");
        kmem_free(pMem, sizeof(BnxeMemDma));
        return NULL;
    }

    if ((rc = ddi_dma_mem_alloc(*pDmaHandle,
                                size,
                                &bnxeAccessAttribBUF,
                                DDI_DMA_CONSISTENT,
                                DDI_DMA_DONTWAIT,
                                (void *)0,
                                &pBuf,
                                &length,
                                pDmaAccHandle)) != DDI_SUCCESS)
    {
        BnxeLogWarn(pUM, "Failed to alloc DMA memory");
        ddi_dma_free_handle(pDmaHandle);
        kmem_free(pMem, sizeof(BnxeMemDma));
        return NULL;
    }

    if ((rc = ddi_dma_addr_bind_handle(*pDmaHandle,
                                       (struct as *)0,
                                       pBuf,
                                       length,
                                       DDI_DMA_RDWR | DDI_DMA_CONSISTENT,
                                       DDI_DMA_DONTWAIT,
                                       (void *)0,
                                       &cookie,
                                       &count)) != DDI_DMA_MAPPED)
    {
        BnxeLogWarn(pUM, "Failed to bind DMA address");
        ddi_dma_mem_free(pDmaAccHandle);
        ddi_dma_free_handle(pDmaHandle);
        kmem_free(pMem, sizeof(BnxeMemDma));
        return NULL;
    }

    pPhysAddr->as_u64 = cookie.dmac_laddress;

    /* save the virtual memory address so we can get the dma_handle later */
    pMem->size     = memSize;
    pMem->pDmaVirt = pBuf;
    pMem->physAddr = *pPhysAddr;

#if 0
    MEM_LOG(pUM, "*** DMA: virt %p / phys 0x%0llx (%d/%d)",
            pBuf, pPhysAddr->as_u64,
            (!((u32_t)pBuf % (u32_t)alignment)) ? 1 : 0,
            (!((u32_t)pPhysAddr->as_ptr % (u32_t)alignment) ? 1 : 0));
#endif

    BNXE_LOCK_ENTER_MEM(pUM);
    d_list_push_head(&pUM->memDmaList, D_LINK_CAST(pMem));
    BNXE_LOCK_EXIT_MEM(pUM);

    MEM_LOG(pUM, "Allocated %d sized DMA block phys:%p virt:%p",
            memSize, pMem->physAddr.as_ptr, pMem->pDmaVirt);

    /* Zero memory! */
    bzero(pBuf, length);

    /* make sure the new contents are flushed back to main memory */
    ddi_dma_sync(*pDmaHandle, 0, length, DDI_DMA_SYNC_FORDEV);

    return pBuf;
}


void * mm_alloc_phys_mem(lm_device_t *  pLM,
                         u32_t          memSize,
                         lm_address_t * pPhysAddr,
                         u8_t           memType,
                         u8_t           cli_idx)
{
    return mm_alloc_phys_mem_align(pLM, memSize, pPhysAddr, BNXE_DMA_ALIGNMENT, memType, cli_idx);
}


void * mm_rt_alloc_mem(lm_device_t * pDev,
                       u32_t         memSize,
                       u8_t          cli_idx)
{
    return mm_alloc_mem(pDev, memSize, cli_idx);
}


void * mm_rt_alloc_phys_mem(lm_device_t *  pDev,
                            u32_t          memSize,
                            lm_address_t * pPhysAddr,
                            u8_t           flushType,
                            u8_t           cli_idx)
{
    return mm_alloc_phys_mem(pDev, memSize, pPhysAddr, flushType, cli_idx);
}


u64_t mm_get_current_time(lm_device_t * pDev)
{
    um_device_t * pUM = (um_device_t *)pDev;
    BnxeDbgBreakMsg(pUM, "MM_GET_CURRENT_TIME");
    return 0;
}


void mm_rt_free_mem(lm_device_t * pDev,
                    void *        pBuf,
                    u32_t         memSize,
                    u8_t          cli_idx)
{
    um_device_t *  pUM = (um_device_t *)pDev;
    BnxeMemBlock * pMem;
    u32_t *        pTmp;
    int i;

    (void)cli_idx;

    BNXE_LOCK_ENTER_MEM(pUM);

    pMem = (BnxeMemBlock *)d_list_peek_head(&pUM->memBlockList);

    /* adjuest for header/trailer checks */
    pBuf = ((char *)pBuf - BNXE_MEM_CHECK_LEN);
    memSize += (BNXE_MEM_CHECK_LEN * 2);

    /* verify header check */
    for (i = 0, pTmp = (u32_t *)pBuf;
         i < BNXE_MEM_CHECK_LEN;
         i += 4, pTmp++)
    {
        if (*pTmp != BNXE_MAGIC)
        {
            BnxeLogWarn(pUM, "Header overflow! (%p/%u)", pBuf, memSize);
            BnxeDbgBreak(pUM);
        }
    }

    /* verify trailer check */
    for (i = 0, pTmp = (u32_t *)((char *)pBuf + memSize - BNXE_MEM_CHECK_LEN);
         i < BNXE_MEM_CHECK_LEN;
         i += 4, pTmp++)
    {
        if (*pTmp != BNXE_MAGIC)
        {
            BnxeLogWarn(pUM, "Trailer overflow! (%p/%u)", pBuf, memSize);
            BnxeDbgBreak(pUM);
        }
    }

    while (pMem)
    {
        if (pBuf == pMem->pBuf)
        {
            if (memSize != pMem->size)
            {
                /* Uh-Oh! */
                BnxeLogWarn(pUM, "Attempt to free memory block with invalid size (%d/%d)",
                            memSize, pMem->size);
                BnxeDbgBreak(pUM);

                BNXE_LOCK_EXIT_MEM(pUM);
                return;
            }

            d_list_remove_entry(&pUM->memBlockList, D_LINK_CAST(pMem));

            kmem_free(pBuf, memSize);
            kmem_free(pMem, sizeof(BnxeMemBlock));

            BNXE_LOCK_EXIT_MEM(pUM);
            return;
        }

        pMem = (BnxeMemBlock *)d_list_next_entry(D_LINK_CAST(pMem));
    }

    BNXE_LOCK_EXIT_MEM(pUM);
}


void mm_rt_free_phys_mem(lm_device_t * pDev,
                         u32_t         memSize,
                         void *        pBuf,
                         lm_address_t  pPhysAddr,
                         u8_t          cli_idx)
{
    um_device_t * pUM = (um_device_t *)pDev;
    BnxeMemDma * pMem;

    (void)pPhysAddr;
    (void)cli_idx;

    BNXE_LOCK_ENTER_MEM(pUM);

    pMem = (BnxeMemDma *)d_list_peek_head(&pUM->memDmaList);

    while (pMem)
    {
        if (pBuf == pMem->pDmaVirt)
        {
            if (memSize != pMem->size)
            {
                /* Uh-Oh! */
                BnxeLogWarn(pUM, "Attempt to free DMA memory with invalid size (%d/%d)",
                            memSize, pMem->size);
                BnxeDbgBreak(pUM);

                BNXE_LOCK_EXIT_MEM(pUM);
                return;
            }

            d_list_remove_entry(&pUM->memDmaList, D_LINK_CAST(pMem));

            ddi_dma_unbind_handle(pMem->dmaHandle);
            ddi_dma_mem_free(&pMem->dmaAccHandle);
            ddi_dma_free_handle(&pMem->dmaHandle);
            kmem_free(pMem, sizeof(BnxeMemDma));

            BNXE_LOCK_EXIT_MEM(pUM);
            return;
        }

        pMem = (BnxeMemDma *)d_list_next_entry(D_LINK_CAST(pMem));
    }

    BNXE_LOCK_EXIT_MEM(pUM);
}


void mm_memset(void * pBuf,
               u8_t   val,
               u32_t  memSize)
{
    memset(pBuf, val, memSize);
}


void mm_memcpy(void *       pDest,
               const void * pSrc,
               u32_t        memSize)
{
    memcpy(pDest, pSrc, memSize);
}


u8_t mm_memcmp(void * pBuf1,
               void * pBuf2,
               u32_t  count)
{
    return (memcmp(pBuf1, pBuf2, count) == 0) ? 1 : 0;
}


void mm_indicate_tx(lm_device_t * pLM,
                    u32_t         idx,
                    s_list_t *    packet_list)
{
    BnxeTxPktsReclaim((um_device_t *)pLM, idx, packet_list);
}


void mm_set_done(lm_device_t * pDev,
                 u32_t         cid,
                 void *        cookie)
{
#if 0
    um_device_t * pUM = (um_device_t *)pDev;
    BnxeLogInfo(pUM, "RAMROD on cid %d cmd is done", cid);
#else
    (void)pDev;
    (void)cid;
#endif
}


void mm_return_sq_pending_command(lm_device_t *               pDev,
                                  struct sq_pending_command * pPending)
{
    /* XXX probably need a memory pool to pull from... */
    mm_rt_free_mem(pDev, pPending, sizeof(struct sq_pending_command),
                   LM_CLI_IDX_NDIS);
}


struct sq_pending_command * mm_get_sq_pending_command(lm_device_t * pDev)
{
    /* XXX probably need a memory pool to pull from... */
    return mm_rt_alloc_mem(pDev, sizeof(struct sq_pending_command),
                           LM_CLI_IDX_NDIS);
}


u32_t mm_copy_packet_buf(lm_device_t * pDev,
                         lm_packet_t * pLMPkt,
                         u8_t *        pMemBuf,
                         u32_t         size)
{
    //um_device_t *   pUM = (um_device_t *)pDev;
    um_txpacket_t * pTxPkt = (um_txpacket_t *)pLMPkt;
    mblk_t *        pMblk;
    u32_t           copied;
    u32_t           mblkDataLen;
    u32_t           toCopy;

    pMblk  = pTxPkt->pMblk;
    copied = 0;

    while (size && pMblk)
    {
        mblkDataLen = (pMblk->b_wptr - pMblk->b_rptr);
        toCopy = (mblkDataLen <= size) ? mblkDataLen : size;

        bcopy(pMblk->b_rptr, pMemBuf, toCopy);

        pMemBuf += toCopy;
        copied  += toCopy;
        size    -= toCopy;

        pMblk = pMblk->b_cont;
    }

    return copied;
}


lm_status_t mm_fan_failure(lm_device_t * pDev)
{
    um_device_t * pUM = (um_device_t *)pDev;
    BnxeLogWarn(pUM, "FAN FAILURE!");
    return LM_STATUS_SUCCESS;
}


static void BnxeLinkStatus(um_device_t * pUM,
                           lm_status_t   link,
                           lm_medium_t   medium)
{
    #define TBUF_SIZE 64
    char tbuf[TBUF_SIZE];
    char * pDuplex;
    char * pRxFlow;
    char * pTxFlow;
    char * pSpeed;

    if (link != LM_STATUS_LINK_ACTIVE)
    {
        /* reset the link status */
        pUM->props.link_speed   = 0;
        pUM->props.link_duplex  = B_FALSE;
        pUM->props.link_txpause = B_FALSE;
        pUM->props.link_rxpause = B_FALSE;
        pUM->props.uptime       = 0;

        /* reset the link partner status */
        pUM->remote.link_autoneg   = B_FALSE;
        pUM->remote.param_10000fdx = B_FALSE;
        pUM->remote.param_2500fdx  = B_FALSE;
        pUM->remote.param_1000fdx  = B_FALSE;
        pUM->remote.param_100fdx   = B_FALSE;
        pUM->remote.param_100hdx   = B_FALSE;
        pUM->remote.param_10fdx    = B_FALSE;
        pUM->remote.param_10hdx    = B_FALSE;
        pUM->remote.param_txpause  = B_FALSE;
        pUM->remote.param_rxpause  = B_FALSE;

        BnxeLogInfo(pUM, "Link Down");
        return;
    }

    pUM->props.uptime = ddi_get_time();

    if (GET_MEDIUM_DUPLEX(medium) == LM_MEDIUM_HALF_DUPLEX)
    {
        pDuplex = "Half";
        pUM->props.link_duplex = B_FALSE;
    }
    else
    {
        pDuplex = "Full";
        pUM->props.link_duplex = B_TRUE;
    }

    if (pUM->lm_dev.vars.flow_control & LM_FLOW_CONTROL_RECEIVE_PAUSE)
    {
        pRxFlow = "ON";
        pUM->props.link_rxpause = B_TRUE;
    }
    else
    {
        pRxFlow = "OFF";
        pUM->props.link_rxpause = B_FALSE;
    }

    if (pUM->lm_dev.vars.flow_control & LM_FLOW_CONTROL_TRANSMIT_PAUSE)
    {
        pTxFlow = "ON";
        pUM->props.link_txpause = B_TRUE;
    }
    else
    {
        pTxFlow = "OFF";
        pUM->props.link_txpause = B_FALSE;
    }

#if 0
    if (pUM->curcfg.lnkcfg.link_autoneg == B_TRUE)
    {
        BnxeUpdateLpCap(pUM);
    }
#endif

    switch (GET_MEDIUM_SPEED(medium))
    {
    case LM_MEDIUM_SPEED_10MBPS:

        pUM->props.link_speed = 10;
        pSpeed = "10Mb";
        break;

    case LM_MEDIUM_SPEED_100MBPS:

        pUM->props.link_speed = 100;
        pSpeed = "100Mb";
        break;

    case LM_MEDIUM_SPEED_1000MBPS:

        pUM->props.link_speed = 1000;
        pSpeed = "1Gb";
        break;

    case LM_MEDIUM_SPEED_2500MBPS:

        pUM->props.link_speed = 2500;
        pSpeed = "2.5Gb";
        break;

    case LM_MEDIUM_SPEED_10GBPS:

        pUM->props.link_speed = 10000;
        pSpeed = "10Gb";
        break;

    case LM_MEDIUM_SPEED_12GBPS:

        pUM->props.link_speed = 12000;
        pSpeed = "12Gb";
        break;

    case LM_MEDIUM_SPEED_12_5GBPS:

        pUM->props.link_speed = 12500;
        pSpeed = "12.5Gb";
        break;

    case LM_MEDIUM_SPEED_13GBPS:

        pUM->props.link_speed = 13000;
        pSpeed = "13Gb";
        break;

    case LM_MEDIUM_SPEED_15GBPS:

        pUM->props.link_speed = 15000;
        pSpeed = "15Gb";
        break;

    case LM_MEDIUM_SPEED_16GBPS:

        pUM->props.link_speed = 16000;
        pSpeed = "16Gb";
        break;

    default:

        if ((GET_MEDIUM_SPEED(medium) >= LM_MEDIUM_SPEED_SEQ_START) &&
            (GET_MEDIUM_SPEED(medium) <= LM_MEDIUM_SPEED_SEQ_END))
        {
            pUM->props.link_speed = (((GET_MEDIUM_SPEED(medium) >> 8) -
                                      (LM_MEDIUM_SPEED_SEQ_START >> 8) +
                                      1) * 100);
            snprintf(tbuf, TBUF_SIZE, "%u", pUM->props.link_speed);
            pSpeed = tbuf;
            break;
        }

        pUM->props.link_speed = 0;
        pSpeed = "";

        break;
    }

    if (*pSpeed == 0)
    {
        BnxeLogInfo(pUM, "%s Duplex Rx Flow %s Tx Flow %s Link Up",
                    pDuplex, pRxFlow, pTxFlow);
    }
    else
    {
        BnxeLogInfo(pUM, "%s %s Duplex Rx Flow %s Tx Flow %s Link Up",
                    pSpeed, pDuplex, pRxFlow, pTxFlow);
    }
}


void mm_indicate_link(lm_device_t * pLM,
                      lm_status_t   link,
                      lm_medium_t   medium)
{
    um_device_t * pUM = (um_device_t *)pLM;

    /* ignore link status if it has not changed since the last indicate */
    if ((pUM->devParams.lastIndLink == link) &&
        (pUM->devParams.lastIndMedium == medium))
    {
        return;
    }

    pUM->devParams.lastIndLink   = link;
    pUM->devParams.lastIndMedium = medium;

    BnxeLinkStatus(pUM, link, medium);

    if (CLIENT_BOUND(pUM, LM_CLI_IDX_NDIS))
    {
        BnxeGldLink(pUM, (link == LM_STATUS_LINK_ACTIVE) ?
                             LINK_STATE_UP : LINK_STATE_DOWN);
    }

    if (CLIENT_BOUND(pUM, LM_CLI_IDX_FCOE))
    {
        if (pUM->fcoe.pDev == NULL)
        {
            BnxeLogWarn(pUM, "FCoE Client bound and pDev is NULL (LINK STATUS failed!) %s@%s",
                        BNXEF_NAME, ddi_get_name_addr(pUM->pDev));
        }
        else if (pUM->fcoe.bind.cliCtl == NULL)
        {
            BnxeLogWarn(pUM, "FCoE Client bound and cliCtl is NULL (LINK STATUS failed!) %s@%s",
                        BNXEF_NAME, ddi_get_name_addr(pUM->pDev));
        }
        else
        {
            pUM->fcoe.bind.cliCtl(pUM->fcoe.pDev,
                                  (link == LM_STATUS_LINK_ACTIVE) ?
                                      CLI_CTL_LINK_UP : CLI_CTL_LINK_DOWN,
                                  NULL,
                                  0);
        }
    }
}


lm_status_t mm_schedule_task(lm_device_t * pDev,
                             u32_t         delay_ms,
                             lm_task_cb_t  task,
                             void *        param)
{
    um_device_t * pUM = (um_device_t *)pDev;

    BnxeWorkQueueAddDelayNoCopy(pUM, (void (*)(um_device_t *, void *))task, param, delay_ms);

    return LM_STATUS_SUCCESS;
}


lm_status_t mm_register_lpme(lm_device_t *                  pDev,
                             lm_generic_workitem_function * func,
                             u8_t                           b_fw_access,
                             u8_t                           b_queue_for_fw)
{
    um_device_t * pUM = (um_device_t *)pDev;

    (void)b_fw_access;
    (void)b_queue_for_fw;

    BnxeWorkQueueAddGeneric(pUM, (void (*)(um_device_t *))func);

    return LM_STATUS_SUCCESS;
}


void MM_ACQUIRE_SPQ_LOCK(lm_device_t * pDev)
{
    BNXE_LOCK_ENTER_SPQ((um_device_t *)pDev);
}


void MM_RELEASE_SPQ_LOCK(lm_device_t * pDev)
{
    BNXE_LOCK_EXIT_SPQ((um_device_t *)pDev);
}


void MM_ACQUIRE_SPQ_LOCK_DPC(lm_device_t * pDev)
{
    BNXE_LOCK_ENTER_SPQ((um_device_t *)pDev);
}


void MM_RELEASE_SPQ_LOCK_DPC(lm_device_t * pDev)
{
    BNXE_LOCK_EXIT_SPQ((um_device_t *)pDev);
}


void MM_ACQUIRE_CID_LOCK(lm_device_t * pDev)
{
    BNXE_LOCK_ENTER_CID((um_device_t *)pDev);
}


void MM_RELEASE_CID_LOCK(lm_device_t * pDev)
{
    BNXE_LOCK_EXIT_CID((um_device_t *)pDev);
}


void MM_ACQUIRE_REQUEST_LOCK(lm_device_t * pDev)
{
    BNXE_LOCK_ENTER_RRREQ((um_device_t *)pDev);
}


void MM_RELEASE_REQUEST_LOCK(lm_device_t * pDev)
{
    BNXE_LOCK_EXIT_RRREQ((um_device_t *)pDev);
}


void MM_ACQUIRE_PHY_LOCK(lm_device_t * pDev)
{
    BNXE_LOCK_ENTER_PHY((um_device_t *)pDev);
}


void MM_RELEASE_PHY_LOCK(lm_device_t * pDev)
{
    BNXE_LOCK_EXIT_PHY((um_device_t *)pDev);
}


void MM_ACQUIRE_PHY_LOCK_DPC(lm_device_t * pDev)
{
    BNXE_LOCK_ENTER_PHY((um_device_t *)pDev);
}


void MM_RELEASE_PHY_LOCK_DPC(lm_device_t * pDev)
{
    BNXE_LOCK_EXIT_PHY((um_device_t *)pDev);
}


void MM_ACQUIRE_DMAE_STATS_LOCK(lm_device_t * pDev)
{
    BNXE_LOCK_ENTER_DMAE_STATS((um_device_t *)pDev);
}


void MM_RELEASE_DMAE_STATS_LOCK(lm_device_t * pDev)
{
    BNXE_LOCK_EXIT_DMAE_STATS((um_device_t *)pDev);
}


void MM_ACQUIRE_DMAE_MISC_LOCK(lm_device_t * pDev)
{
    BNXE_LOCK_ENTER_DMAE_MISC((um_device_t *)pDev);
}


void MM_RELEASE_DMAE_MISC_LOCK(lm_device_t * pDev)
{
    BNXE_LOCK_EXIT_DMAE_MISC((um_device_t *)pDev);
}


void MM_ACQUIRE_IND_REG_LOCK(lm_device_t * pDev)
{
    BNXE_LOCK_ENTER_IND((um_device_t *)pDev);
}


void MM_RELEASE_IND_REG_LOCK(lm_device_t * pDev)
{
    BNXE_LOCK_EXIT_IND((um_device_t *)pDev);
}


void MM_ACQUIRE_LOADER_LOCK()
{
    mutex_enter(&bnxeLoaderMutex);
}


void MM_RELEASE_LOADER_LOCK()
{
    mutex_exit(&bnxeLoaderMutex);
}


void MM_ACQUIRE_SP_REQ_MGR_LOCK(lm_device_t * pDev)
{
    BNXE_LOCK_ENTER_SPREQ((um_device_t *)pDev);
}


void MM_RELEASE_SP_REQ_MGR_LOCK(lm_device_t * pDev)
{
    BNXE_LOCK_EXIT_SPREQ((um_device_t *)pDev);
}


void MM_ACQUIRE_ISLES_CONTROL_LOCK(lm_device_t * pDev)
{
    BNXE_LOCK_ENTER_ISLES_CONTROL((um_device_t *)pDev);
}


void MM_RELEASE_ISLES_CONTROL_LOCK(lm_device_t * pDev)
{
    BNXE_LOCK_EXIT_ISLES_CONTROL((um_device_t *)pDev);
}


void MM_ACQUIRE_ISLES_CONTROL_LOCK_DPC(lm_device_t * pDev)
{
    BNXE_LOCK_ENTER_ISLES_CONTROL((um_device_t *)pDev);
}


void MM_RELEASE_ISLES_CONTROL_LOCK_DPC(lm_device_t * pDev)
{
    BNXE_LOCK_EXIT_ISLES_CONTROL((um_device_t *)pDev);
}


void MM_ACQUIRE_SB_LOCK(lm_device_t * pDev, u8_t sb_idx)
{
    BNXE_LOCK_ENTER_SB((um_device_t *)pDev, sb_idx);
}


void MM_RELEASE_SB_LOCK(lm_device_t * pDev, u8_t sb_idx)
{
    BNXE_LOCK_EXIT_SB((um_device_t *)pDev, sb_idx);
}


void MM_ACQUIRE_MCP_LOCK(lm_device_t * pDev)
{
    BNXE_LOCK_ENTER_MCP((um_device_t *)pDev);
}


void MM_RELEASE_MCP_LOCK(lm_device_t * pDev)
{
    BNXE_LOCK_EXIT_MCP((um_device_t *)pDev);
}


unsigned int mm_crc32(unsigned char * address,
                      unsigned int    size,
                      unsigned int    crc)
{
    return 0;
}


unsigned short mm_crc16(unsigned char * address,
                        unsigned int    size,
                        unsigned short  crc)
{
    return 0;
}


lm_status_t mm_event_log_generic_arg_fwd(lm_device_t *     pDev,
                                         const lm_log_id_t lm_log_id,
                                         va_list           argp)
{
    um_device_t * pUM = (um_device_t *)pDev;
    u8_t          port = 0 ;
    char *        sz_vendor_name = NULL;
    char *        sz_vendor_pn = NULL;

    switch (lm_log_id)
    {
    case LM_LOG_ID_FAN_FAILURE: // fan failure detected

        BnxeLogWarn(pUM, "FAN FAILURE!");
        break;

    case LM_LOG_ID_UNQUAL_IO_MODULE: // SFP+ unqualified io module
        /*
         * expected parameters:
         * u8 port, const char * vendor_name, const char * vendor_pn
         */
        port           = va_arg(argp, int);
        sz_vendor_name = va_arg(argp, char*);
        sz_vendor_pn   = va_arg(argp, char*);

        BnxeLogInfo(pUM, "Unqualified IO Module: %s %s (port=%d)",
                    sz_vendor_name, sz_vendor_pn, port);
        break;

    case LM_LOG_ID_OVER_CURRENT: // SFP+ over current power
        /*
         * expected parametrs:
         * u8 port
         */ 
        port = va_arg(argp, int);

        BnxeLogWarn(pUM, "SFP+ over current, power failure! (port=%d)", port);
        break;

    case LM_LOG_ID_NO_10G_SUPPORT: // 10g speed is requested but not supported
        /*
         * expected parametrs:
         * u8 port
         */
        port = va_arg(argp, int);

        BnxeLogWarn(pUM, "10Gb speed not supported! (port=%d)", port);
        break;

    case LM_LOG_ID_PHY_UNINITIALIZED:
        /*
         * expected parametrs:
         * u8 port
         */
        port = va_arg(argp, int);

        BnxeLogWarn(pUM, "PHY uninitialized! (port=%d)", port);
        break;

    case LM_LOG_ID_MDIO_ACCESS_TIMEOUT:

#define MM_PORT_NUM(pdev)                               \
        (CHIP_PORT_MODE(pdev) == LM_CHIP_PORT_MODE_4) ? \
            (PATH_ID(pdev) + (2 * PORT_ID(pdev)))     : \
            (PATH_ID(pdev) + PORT_ID(pdev))

        port = MM_PORT_NUM(&pUM->lm_dev);

        BnxeLogWarn(pUM, "MDIO access timeout! (port=%d)", port);
        break;

    default:

        BnxeLogWarn(pUM, "Unknown MM event log! (type=%d)", lm_log_id);
        break;
    }

    return LM_STATUS_SUCCESS;
}


lm_status_t mm_event_log_generic(lm_device_t *     pDev,
                                 const lm_log_id_t lm_log_id,
                                 ...)
{
    lm_status_t lm_status = LM_STATUS_SUCCESS;
    va_list argp;

    va_start(argp, lm_log_id);
    lm_status = mm_event_log_generic_arg_fwd(pDev, lm_log_id, argp);
    va_end(argp);

    return lm_status;
}

