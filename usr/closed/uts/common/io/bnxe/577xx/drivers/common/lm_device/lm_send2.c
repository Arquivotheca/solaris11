/*******************************************************************************
 * The information contained in this file is confidential and proprietary to
 * Broadcom Corporation.  No part of this file may be reproduced or
 * distributed, in any form or by any means for any purpose, without the
 * express written permission of Broadcom Corporation.
 *
 * (c) COPYRIGHT 2001-2006 Broadcom Corporation, ALL RIGHTS RESERVED.
 *
 *
 * Module Description:
 *
 *
 * History:
 *    11/15/01 Hav Khauv        Inception.
 *    4/4/06  Eliezer           begin modifying for everest 
 ******************************************************************************/

#include "lm5710.h"
#include "microcode_constants.h"
#include "eth_constants.h"
#include "bd_chain.h"
#include "ecore_common.h"

static void lm_handle_lso_split(IN  lm_address_t frag_addr_data_offset,
                                IN  u16_t data_part_size,
                                IN  lm_tx_chain_t *tx_chain,
                                IN  struct eth_tx_start_bd *start_bd,
                                IN  struct eth_tx_bd *generic_bd
                                )
{
    struct eth_tx_bd *prod_bd;
    u16_t old_nbd = mm_le16_to_cpu(start_bd->nbd);
    u16_t old_nbytes = mm_le16_to_cpu(generic_bd->nbytes);

    ASSERT_STATIC(OFFSETOF(struct eth_tx_bd, nbytes) == OFFSETOF(struct eth_tx_start_bd, nbytes)) ;
    DbgBreakIfFastPath(!(start_bd && generic_bd));

    //increase nbd on account of the split BD
    start_bd->nbd = mm_cpu_to_le16(old_nbd + 1);

    //fix the num of bytes of the BD which has the headers+data to correspond only to the headers part
    generic_bd->nbytes = mm_cpu_to_le16(old_nbytes - data_part_size);
    //this is phys addr which points to the start of the data part right after the end of the headers
    LM_INC64(&frag_addr_data_offset, mm_le16_to_cpu(generic_bd->nbytes));

    //Advance to the next BD.
    prod_bd = (struct eth_tx_bd *)lm_bd_chain_produce_bd(&tx_chain->bd_chain);
    
    //fill the fields of the new additional BD which holds _only_ data
    prod_bd->addr_lo              = mm_cpu_to_le32(frag_addr_data_offset.as_u32.low);
    prod_bd->addr_hi              = mm_cpu_to_le32(frag_addr_data_offset.as_u32.high);
    prod_bd->nbytes               = mm_cpu_to_le16(data_part_size);

    tx_chain->lso_split_used++;

    DbgMessage2(NULL, WARNl2tx, "#lm_handle_lso_split: after split: original bd nbytes=0x%x,new bd nbytes=0x%x\n",
               mm_le16_to_cpu(generic_bd->nbytes), mm_le16_to_cpu(prod_bd->nbytes));
}

static void lm_pre_process_lso_packet(
    IN  lm_device_t     *pdev,
    IN  lm_packet_t     *packet,
    IN  lm_frag_list_t  *frags,
    OUT u8_t            *split_required,
    IN  u16_t            total_hlen_bytes
    )
{
    /* find headers nbds, for that calc eth_hlen and total_hlen_bytes,
       and take the opportunity to decide if header data separation is required */
    u32_t cnt;
    u16_t sum_frag_size = 0;
    u8_t  hdr_nbds      = 0;

    *split_required = FALSE;

    for(cnt = 0; cnt < frags->cnt; cnt++)
    {
        hdr_nbds++;
        sum_frag_size += (u16_t)frags->frag_arr[cnt].size;
        if (total_hlen_bytes <= sum_frag_size) 
        {
            if (total_hlen_bytes < sum_frag_size)
            {
                *split_required = TRUE;
            }
            break;
        }
    }
    DbgBreakIfFastPath(total_hlen_bytes > sum_frag_size);
    packet->u1.tx.hdr_nbds = hdr_nbds;
}

static void lm_process_lso_packet(IN  lm_packet_t *packet,
                                  IN  lm_device_t *pdev,
                                  IN  lm_tx_chain_t *tx_chain,
                                  IN  lm_frag_list_t *frags,
                                  IN  void *parse_bd,
                                  IN  struct eth_tx_start_bd *start_bd,
                                  OUT lm_frag_t **frag,
                                  IN  u16_t total_hlen_bytes,
                                  IN  u8_t split_required)
{    
    struct eth_tx_bd *prod_bd = NULL;
    u32_t cnt                 = 0;
    u16_t hlen_reminder       = total_hlen_bytes;

    /* Sanity check.  Maximum total length for IP and TCP headers
     * is 120 bytes. */
    DbgBreakIfFastPath(packet->l2pkt_tx_info->lso_ip_hdr_len + packet->l2pkt_tx_info->lso_tcp_hdr_len > 120);

    start_bd->bd_flags.as_bitfield |= ETH_TX_BD_FLAGS_SW_LSO;

    if (CHIP_IS_E1x(pdev)) 
    {
        struct eth_tx_parse_bd_e1x *parse_bd_e1x = (struct eth_tx_parse_bd_e1x *)parse_bd;
        parse_bd_e1x->lso_mss          = mm_cpu_to_le16(packet->l2pkt_tx_info->lso_mss);
        parse_bd_e1x->ip_id            = mm_cpu_to_le16(packet->l2pkt_tx_info->lso_ipid);
        parse_bd_e1x->tcp_send_seq     = mm_cpu_to_le32(packet->l2pkt_tx_info->lso_tcp_send_seq);
        parse_bd_e1x->tcp_flags        = packet->l2pkt_tx_info->lso_tcp_flags; // no endianity since it is u8_t
    

        //in case of LSO it is required according to fw to toggle the ETH_TX_PARSE_BD_PSEUDO_CS_WITHOUT_LEN flag since the TCP seg len is 0
        parse_bd_e1x->global_data |= ETH_TX_PARSE_BD_E1X_PSEUDO_CS_WITHOUT_LEN;

        if GET_FLAGS(packet->l2pkt_tx_info->flags, LM_TX_FLAG_TCP_LSO_SNAP_FRAME)
        {
            parse_bd_e1x->global_data |= ETH_TX_PARSE_BD_E1X_LLC_SNAP_EN;
        }
    }
    else 
    {
        struct eth_tx_parse_bd_e2 *parse_bd_e2 = (struct eth_tx_parse_bd_e2 *)parse_bd;
        parse_bd_e2->parsing_data |= ETH_TX_PARSE_BD_E2_LSO_MSS & (packet->l2pkt_tx_info->lso_mss << ETH_TX_PARSE_BD_E2_LSO_MSS_SHIFT);
    }
    

    //enforce this due to miniport design in case of LSO and CSUM
    SET_FLAGS(packet->l2pkt_tx_info->flags, LM_TX_FLAG_COMPUTE_TCP_UDP_CKSUM);

    if (! GET_FLAGS(packet->l2pkt_tx_info->flags, LM_TX_FLAG_IPV6_PACKET))
    {
        SET_FLAGS(packet->l2pkt_tx_info->flags, LM_TX_FLAG_COMPUTE_IP_CKSUM);
    }


    //required only in case of LSO - num of bds the headers occupy all together
    RESET_FLAGS(start_bd->general_data, ETH_TX_START_BD_HDR_NBDS);
    start_bd->general_data |= ((packet->u1.tx.hdr_nbds & ETH_TX_START_BD_HDR_NBDS) << ETH_TX_START_BD_HDR_NBDS_SHIFT);
    
    //check for split in START BD
    if (split_required)
    { 
        if ((start_bd->general_data & ETH_TX_START_BD_HDR_NBDS) == 1)
        {
            lm_handle_lso_split(frags->frag_arr[0].addr,
                                mm_le16_to_cpu(start_bd->nbytes) - hlen_reminder,
                                tx_chain,
                                start_bd,
                                (struct eth_tx_bd *)start_bd ); 
            split_required = FALSE;
        }
        else 
        {
            u16_t start_bd_nbytes = mm_le16_to_cpu(start_bd->nbytes);

            DbgBreakIfFastPath(hlen_reminder <= start_bd_nbytes);
            hlen_reminder -= start_bd_nbytes;
        }
    }

    for(cnt = 1; cnt < frags->cnt; cnt++)
    {
        DbgBreakIfFastPath((*frag)->size >= 0x10000 || (*frag)->size == 0);

        //Advance to the next BD.
        prod_bd = (struct eth_tx_bd *)lm_bd_chain_produce_bd(&tx_chain->bd_chain);

        prod_bd->addr_lo              = mm_cpu_to_le32((*frag)->addr.as_u32.low);
        prod_bd->addr_hi              = mm_cpu_to_le32((*frag)->addr.as_u32.high);
        prod_bd->nbytes               = mm_cpu_to_le16((u16_t) (*frag)->size);

        //if there is a split condition and we are on the exact BD, do it! we don't enter here if there was a split already!
        if (split_required)
        {
            if (cnt == ((start_bd->general_data & ETH_TX_START_BD_HDR_NBDS) - 1))
            {
                lm_handle_lso_split((*frag)->addr,
                                    mm_le16_to_cpu(prod_bd->nbytes) - hlen_reminder,
                                    tx_chain,
                                    start_bd,
                                    prod_bd
                                    );
                split_required = FALSE;
            }
            else 
            {
                u16_t prod_bd_nbytes = mm_le16_to_cpu(prod_bd->nbytes);

                DbgBreakIfFastPath(hlen_reminder <= prod_bd_nbytes);
                hlen_reminder -= prod_bd_nbytes;
            }
        }

        packet->size += (*frag)->size;
        (*frag)++;
    }

    

    //statistics
    //since this is fast path, we do not use ATOMIC INC.
    //therefore the statistic might not be completely accurate
    //possible fix (FUTURE, if required): count the statistic item per RSS/TSS
    LM_COMMON_DRV_STATS_INC_ETH(pdev, tx_lso_frames);
}

/**
 * @Description: 
 *   returns coalesce buffer of size >= buf_size, or NULL if none available
 * @Assumptions:
 *   txq lock is taken by the caller
*/
lm_coalesce_buffer_t *
lm_get_coalesce_buffer(
    IN lm_device_t      *pdev,
    IN lm_tx_chain_t    *txq,
    IN u32_t            buf_size)
{
    lm_coalesce_buffer_t *coalesce_buf = NULL;
    u32_t coalesce_buf_cnt, cnt;

    if (ERR_IF(CHK_NULL(pdev) || CHK_NULL(txq) || !buf_size)) {
        DbgBreakFastPath();
        return NULL;
    }

    coalesce_buf_cnt = s_list_entry_cnt(&txq->coalesce_buf_list);
    for(cnt = 0; cnt < coalesce_buf_cnt; cnt++)
    {
        coalesce_buf = (lm_coalesce_buffer_t *) s_list_pop_head(
            &txq->coalesce_buf_list);

        DbgBreakIfFastPath(coalesce_buf == NULL);
        if(NULL == coalesce_buf)
        {
            //this case were coalesce buffer in the list is equal to null shouldn't happen.
            DbgMessage(pdev, FATAL, "lm_get_coalesce_buffer:coalesce buffer was null\n");
            break;
        }
        if(coalesce_buf->buf_size >= buf_size)
        {
            txq->coalesce_buf_used++;
            break;
        }

        s_list_push_tail(&txq->coalesce_buf_list, &coalesce_buf->link);

        coalesce_buf = NULL;
    }

    return coalesce_buf;
} /* lm_get_coalesce_buffer */

/**
 * @Description: 
 *   returns coalesce_buf into txq list
 * @Assumptions:
 *   txq lock is taken by the caller
*/
void
lm_put_coalesce_buffer(
    IN lm_device_t          *pdev,
    IN lm_tx_chain_t        *txq,
    IN lm_coalesce_buffer_t *coalesce_buf)
{
    if (ERR_IF(CHK_NULL(pdev) || CHK_NULL(txq) || CHK_NULL(coalesce_buf))) {
        DbgBreakFastPath();
        return;
    }

    s_list_push_tail(&txq->coalesce_buf_list, &coalesce_buf->link);

    return;
} /* lm_put_coalesce_buffer */

/**
 * @Description:
 *   copy given packet into available coalesce buffer of given txq
 * @Assumptions:
 *   txq lock is taken by the caller
 * @Returns:
 *   - SUCCESS - 
 *      - The OUT parameter coal_buf will be set to point the allocated 
 *        coalesce buffer
 *      - The coalesce buffer frag size will be set to the given packet size
 *   - RESOURCE - no available coalecse buffer for given packet 
 *                (according to packet size) 
 */ 
static lm_status_t
lm_copy_packet_to_coalesce_buffer(
    IN  lm_device_t             *pdev,
    IN  lm_tx_chain_t           *txq,
    IN  lm_packet_t             *lmpkt,
    IN  lm_frag_list_t          *frags,
    OUT lm_coalesce_buffer_t    **coal_buf
    )
{
    lm_coalesce_buffer_t *coalesce_buf;
    lm_frag_t*            frag;
    u32_t                 pkt_size      = 0;
    u32_t                 copied_bytes;
    u32_t                 cnt;

    if (ERR_IF(CHK_NULL(pdev) || CHK_NULL(txq) || 
               CHK_NULL(lmpkt) || CHK_NULL(frags)))
    {
        DbgBreakFastPath();
        return LM_STATUS_FAILURE;
    }

    /* Determine packet size. */    
    frag = &frags->frag_arr[0];
    for (cnt = 0; cnt < frags->cnt; cnt++, frag++) {
        pkt_size += frag->size;
    }
    
    /* Find a buffer large enough for copying this packet.  In the case
     * of an LSO frame, we should have at least one 64k coalesce buffer. */
    coalesce_buf = lm_get_coalesce_buffer(pdev, txq, pkt_size);
    if(coalesce_buf == NULL)
    {
        DbgMessage2(pdev, INFORMl2tx,
                    "#copy to coalesce buffer FAILED, (lmpkt=0x%p,pkt_size=%d)\n",
                    lmpkt, pkt_size);
        LM_COMMON_DRV_STATS_ATOMIC_INC_ETH(pdev, tx_no_coalesce_buf);
        return LM_STATUS_RESOURCE;
    }

    /* copy the packet into the coalesce buffer */
    copied_bytes = mm_copy_packet_buf(
        pdev, lmpkt, coalesce_buf->mem_virt, pkt_size);
    if (ERR_IF(copied_bytes != pkt_size)) {
        DbgBreakFastPath();
        lm_put_coalesce_buffer(pdev, txq, coalesce_buf);
        return LM_STATUS_FAILURE;
    }

    /* adjust frag size in coalesce buf */
    coalesce_buf->frags.frag_arr[0].size = pkt_size;

    *coal_buf = coalesce_buf;
    return LM_STATUS_SUCCESS;
} /* lm_copy_packet_to_coalesce_buffer */

/**
 * @Description:
 *   check if packet requires copying to coalesce buf (packet too fregmented)
 * @Returns: 
 *   TRUE or FALSE
*/
static u8_t
lm_is_packet_coalescing_required(
    IN     lm_device_t *pdev,
    IN     lm_packet_t *lmpkt,
    IN     lm_frag_list_t *frags
    )
{
    u8_t to_copy = FALSE; 
    static u32_t const MAX_FETCH_BD = 13;  /* HW max bds per packet capabitily */

    if (frags->cnt >= MAX_FETCH_BD) 
    {
        if GET_FLAGS(lmpkt->l2pkt_tx_info->flags, LM_TX_FLAG_TCP_LSO_FRAME)
        {
            /* Too fragmented LSO packet, check if it needs to be copied: */
            u8_t wnd_size  = MAX_FETCH_BD - lmpkt->u1.tx.hdr_nbds - 2;
            u8_t num_frags = (u8_t)frags->cnt;
            u8_t wnd_idx   = 0;
            u8_t frag_idx  = 0;
            u32_t wnd_sum  = 0;

            for (wnd_idx = lmpkt->u1.tx.hdr_nbds; wnd_idx <= (num_frags - wnd_size); wnd_idx++)
            {
                for (frag_idx = 0; frag_idx < wnd_size; frag_idx++)
                {
                    wnd_sum += frags->frag_arr[wnd_idx + frag_idx].size;
                }

                if (wnd_sum < lmpkt->l2pkt_tx_info->lso_mss)
                {
                    DbgMessage2(pdev, WARNl2tx,
                                "#copy to coalesce buffer IS REQUIRED for LSO packet, (lmpkt=0x%p,num_frags=%d)\n",
                                lmpkt, num_frags);
                    to_copy = TRUE;
                    break;
                }
                wnd_sum = 0;
            }
        }
        else
        {
            /* in non LSO, too fragmented packet should always 
               be copied to coalesce buffer */
            DbgMessage2(pdev, INFORMl2tx,
                        "#copy to coalesce buffer IS REQUIRED for NON LSO packet, (lmpkt=0x%p,num_frags=%d)\n",
                        lmpkt, frags->cnt);
            to_copy = TRUE;
        }
    }

    return to_copy;
} /* lm_is_packet_coalescing_required */
                 
/*******************************************************************************
 * Description:
 *
 * Return:
 ******************************************************************************/
lm_status_t
lm_send_packet(
    lm_device_t *pdev,
    u32_t chain_idx,
    lm_packet_t *packet,
    lm_frag_list_t *frags)
{
    lm_tx_chain_t *tx_chain          = NULL;
    struct eth_tx_start_bd *start_bd = NULL;
    struct eth_tx_bd *prod_bd        = NULL;
    lm_frag_t *frag                  = NULL;
    u16_t old_prod_idx               = 0;
    u32_t flags                      = 0;
    u32_t cnt                        = 0;
#if defined(__BIG_ENDIAN)
    struct doorbell_set_prod  dq_msg = {0, 0, {0}};
#elif defined(__LITTLE_ENDIAN)
    struct doorbell_set_prod  dq_msg = {{0}, 0, 0};
#endif
    
    u8_t eth_hlen                    = ETHERNET_PACKET_HEADER_SIZE;
    u8_t split_required              = FALSE;
    u8_t eth_addr_type               = UNKNOWN_ADDRESS;
    u16_t total_hlen_bytes           = 0;
    u16_t start_bd_nbd               = 0;
    const u8_t  traffic_type         = LM_CHAIN_IDX_TRAFFIC_TYPE(pdev, chain_idx);
    u16_t vlan_tag = 0;
    void * parse_bd_ptr;

    /* Assert on received Parameters */
    DbgBreakIfAll( LLFC_DRIVER_TRAFFIC_TYPE_MAX == traffic_type);
    //DbgBreakIfFastPath(chain_idx >= pdev->params.rss_chain_cnt);

    DbgMessage(pdev, VERBOSEl2tx | VERBOSEl4tx, "### lm_send_packet\n");

    tx_chain = &LM_TXQ(pdev, chain_idx);
    old_prod_idx = lm_bd_chain_prod_idx(&tx_chain->bd_chain);

    /* Compute Ethernet Header Len */
    if GET_FLAGS(packet->l2pkt_tx_info->flags, LM_TX_FLAG_VLAN_TAG_EXISTS)
    {
        eth_hlen += ETHERNET_VLAN_TAG_SIZE;
    }

    if GET_FLAGS(packet->l2pkt_tx_info->flags, LM_TX_FLAG_TCP_LSO_SNAP_FRAME) 
    {
        eth_hlen += ETHERNET_LLC_SNAP_SIZE;
    }

    //calculate the total sum of ETH + IP + TCP headers in term of bytes
    total_hlen_bytes = packet->l2pkt_tx_info->lso_ip_hdr_len + packet->l2pkt_tx_info->lso_tcp_hdr_len + eth_hlen;

    if GET_FLAGS(packet->l2pkt_tx_info->flags, LM_TX_FLAG_TCP_LSO_FRAME)
    {
        lm_pre_process_lso_packet( pdev, packet, frags, &split_required, total_hlen_bytes);
    }

    /* handle packet coalescing - if required, copy the too fregmented packet 
       into a pre-allocated coalesce buffer */
    if (lm_is_packet_coalescing_required(pdev, packet, frags))
    {
        lm_coalesce_buffer_t *coalesce_buf = NULL;
        lm_status_t lm_status;

        if (ERR_IF(packet->u1.tx.coalesce_buf != NULL))
        {
            /* pkt coal buf can't already be set */
            DbgBreakFastPath();
            return LM_STATUS_FAILURE;
        }

        lm_status = lm_copy_packet_to_coalesce_buffer(
            pdev, tx_chain, packet, frags, &coalesce_buf);

        if (lm_status == LM_STATUS_SUCCESS)
        {
            LM_COMMON_DRV_STATS_ATOMIC_INC_ETH(pdev, tx_l2_assembly_buf_use);

            packet->u1.tx.coalesce_buf = coalesce_buf; /* saved to be freed upon completion */            

            packet->u1.tx.hdr_nbds = 1;
            split_required = 1;

            /* from here on, use the coalesce buf frags list 
               instead of the frags list given by the caller */
            frags = &coalesce_buf->frags;
        }
        else
        {
            return lm_status; /* no coalesce buf available, can't continue */
        }
    }

    // stringent heuristic - there might be a parsing bd + a split of hdr & data
    if((frags->cnt + 2) > lm_bd_chain_avail_bds(&tx_chain->bd_chain))
    {
        LM_COMMON_DRV_STATS_ATOMIC_INC_ETH(pdev, tx_no_l2_bd);
        if (packet->u1.tx.coalesce_buf)
        {
            /* TODO: change this to "goto out_err:" */
            lm_put_coalesce_buffer(pdev, tx_chain, packet->u1.tx.coalesce_buf);
            packet->u1.tx.coalesce_buf = NULL;
        }
        return LM_STATUS_RESOURCE;
    }

    packet->size = 0;
    start_bd = (struct eth_tx_start_bd *)lm_bd_chain_produce_bd(&tx_chain->bd_chain);
    start_bd->nbd = 0;
    start_bd->general_data = (UNICAST_ADDRESS << ETH_TX_START_BD_ETH_ADDR_TYPE_SHIFT);
    frag = frags->frag_arr;

    //SNAP
    //parse bd is always present for FW simplicity
    //adjust the parse BD pointer
    /////////////////start parse BD handling ////////////////////////////////////////////
    parse_bd_ptr = lm_bd_chain_produce_bd(&tx_chain->bd_chain);
    mm_mem_zero(parse_bd_ptr, sizeof(union eth_tx_bd_types));

    //parse BD taken into account
    start_bd_nbd++;
    /////////////////end parse BD handling ////////////////////////////////////////////

    //initialize the start BD 
    start_bd->addr_lo              = mm_cpu_to_le32(frag->addr.as_u32.low);
    start_bd->addr_hi              = mm_cpu_to_le32(frag->addr.as_u32.high);
    start_bd->nbytes               = mm_cpu_to_le16((u16_t) frag->size);
    start_bd->bd_flags.as_bitfield = (u8_t) flags | ETH_TX_BD_FLAGS_START_BD;

    packet->size += frag->size;
    frag++;

    if GET_FLAGS(packet->l2pkt_tx_info->flags , LM_TX_FLAG_INSERT_VLAN_TAG)
    {
        DbgMessage1(pdev,INFORMl2,"Outband vlan 0X%x\n",packet->l2pkt_tx_info->vlan_tag);
        start_bd->bd_flags.as_bitfield |= (ETH_TX_BD_FLAGS_VLAN_MODE & (X_ETH_OUTBAND_VLAN << ETH_TX_BD_FLAGS_VLAN_MODE_SHIFT));

        vlan_tag = packet->l2pkt_tx_info->vlan_tag;
    }
    else if GET_FLAGS(packet->l2pkt_tx_info->flags, LM_TX_FLAG_VLAN_TAG_EXISTS)
    {
        DbgMessage1(pdev,INFORMl2,"Inband vlan 0X%x\n",packet->l2pkt_tx_info->vlan_tag);
        start_bd->bd_flags.as_bitfield |= (ETH_TX_BD_FLAGS_VLAN_MODE & (X_ETH_INBAND_VLAN << ETH_TX_BD_FLAGS_VLAN_MODE_SHIFT));

        vlan_tag = packet->l2pkt_tx_info->vlan_tag;
    }
    else
    {
         /* for debug only - to discover driver/fw lack of synchronization */
         vlan_tag = (u16_t)(pdev->tx_info.chain[chain_idx].eth_tx_prods.packets_prod);
    }
    start_bd->vlan_or_ethertype = mm_cpu_to_le16(vlan_tag);

    /* set ipv6 flag in case of ipv6 lso or ipv6 cksum */
    if GET_FLAGS(packet->l2pkt_tx_info->flags, LM_TX_FLAG_IPV6_PACKET)
    {
        start_bd->bd_flags.as_bitfield |= ETH_TX_BD_FLAGS_IPV6;
    }

    //SNAP 
    if GET_FLAGS(packet->l2pkt_tx_info->flags, LM_TX_FLAG_TCP_LSO_FRAME)
    {
        start_bd->nbd = mm_cpu_to_le16(start_bd_nbd);
        lm_process_lso_packet(packet, pdev, tx_chain, frags, parse_bd_ptr, start_bd,
                              &frag, total_hlen_bytes, split_required);
        start_bd_nbd = mm_cpu_to_le16(start_bd->nbd);
    }
    else //This is the regular path in case we're not LSO
    {
        // In non-LSO packets, if there are more than 1 data bds, the second data bd (the one after 
        // the parsing bd) will be of the above type.total_pkt_bytes will hold the total packet length,
        // without outer vlan and without vlan in case there is vlan offload.
        struct eth_tx_bd *total_pkt_bytes_bd        = NULL;

        //pass on all frags except the first one
        for(cnt = 1; cnt < frags->cnt; cnt++)
        {
            DbgMessage4(pdev, VERBOSEl2tx | VERBOSEl4tx,"   frag %d, hi 0x%x, lo 0x%x, size %d\n",
                cnt, frag->addr.as_u32.high, frag->addr.as_u32.low, frag->size);
    
            DbgBreakIfFastPath(frag->size >= 0x10000 || frag->size == 0);
            // TODO: assert/ fixup if to many SGE's per MTU
    
            //Advance to the next BD.
            prod_bd = (struct eth_tx_bd *)lm_bd_chain_produce_bd(&tx_chain->bd_chain);
    
            prod_bd->addr_lo              = mm_cpu_to_le32(frag->addr.as_u32.low);
            prod_bd->addr_hi              = mm_cpu_to_le32(frag->addr.as_u32.high);
            prod_bd->nbytes               = mm_cpu_to_le16((u16_t) frag->size);
            if (NULL == total_pkt_bytes_bd)
            {
                //second data bd saved for updating total_pkt_bytes.
                total_pkt_bytes_bd = prod_bd;
            }
            packet->size += frag->size;
    
            frag++;
        }

        if (NULL != total_pkt_bytes_bd)
        {
            //we have a second data bd
            total_pkt_bytes_bd->total_pkt_bytes = mm_cpu_to_le16((u16_t) packet->size);
        }
    }

    //we might have IP csum, TCP csum, both or none.
    //It is definitely legit for a packet to be csum offloaded with or without LSO!
    //If the packet is LSO, we must enter here!!!!
    if GET_FLAGS(packet->l2pkt_tx_info->flags, (LM_TX_FLAG_COMPUTE_IP_CKSUM | LM_TX_FLAG_COMPUTE_TCP_UDP_CKSUM))
    {
        if GET_FLAGS(packet->l2pkt_tx_info->flags, LM_TX_FLAG_COMPUTE_IP_CKSUM)
        {
            start_bd->bd_flags.as_bitfield |= ETH_TX_BD_FLAGS_IP_CSUM;
        }

        if GET_FLAGS(packet->l2pkt_tx_info->flags, LM_TX_FLAG_COMPUTE_TCP_UDP_CKSUM)
        {
            start_bd->bd_flags.as_bitfield |= ETH_TX_BD_FLAGS_L4_CSUM;
            if(packet->l2pkt_tx_info->cs_any_offset)
            {
                start_bd->bd_flags.as_bitfield |= ETH_TX_BD_FLAGS_IS_UDP;
            }
        }

        if (CHIP_IS_E1x(pdev)) {
            struct eth_tx_parse_bd_e1x *parse_bd_e1x = parse_bd_ptr;

            if CHK_NULL(parse_bd_ptr) {
                DbgBreakIfFastPath( !parse_bd_ptr ) ;
                return LM_STATUS_FAILURE ;
            }

            parse_bd_e1x->ip_hlen_w    = packet->l2pkt_tx_info->lso_ip_hdr_len >> 1;
            parse_bd_e1x->global_data |= (( (eth_hlen) >> 1) << ETH_TX_PARSE_BD_E1X_IP_HDR_START_OFFSET_W_SHIFT);
            parse_bd_e1x->total_hlen_w = mm_cpu_to_le16((packet->l2pkt_tx_info->lso_ip_hdr_len >> 1) + ( (eth_hlen) >> 1));

            if(packet->l2pkt_tx_info->flags & LM_TX_FLAG_TCP_LSO_SNAP_FRAME) {
                parse_bd_e1x->global_data |= ETH_TX_PARSE_BD_E1X_LLC_SNAP_EN;
            }

            if (packet->l2pkt_tx_info->flags & LM_TX_FLAG_COMPUTE_TCP_UDP_CKSUM)
            {
                parse_bd_e1x->tcp_pseudo_csum = mm_cpu_to_le16(packet->l2pkt_tx_info->tcp_pseudo_csum);
                parse_bd_e1x->global_data    |= (packet->l2pkt_tx_info->tcp_nonce_sum_bit << ETH_TX_PARSE_BD_E1X_NS_FLG_SHIFT);
                parse_bd_e1x->total_hlen_w    = mm_cpu_to_le16((total_hlen_bytes) >> 1);
            }

        } else {
            struct eth_tx_parse_bd_e2 *parse_bd_e2 = parse_bd_ptr;
            u32_t val;

            val = ((packet->l2pkt_tx_info->lso_ip_hdr_len + eth_hlen) >> 1);
            parse_bd_e2->parsing_data |= ETH_TX_PARSE_BD_E2_TCP_HDR_START_OFFSET_W & (val << ETH_TX_PARSE_BD_E2_TCP_HDR_START_OFFSET_W_SHIFT);

            val = (packet->l2pkt_tx_info->lso_tcp_hdr_len >> 2);
            parse_bd_e2->parsing_data |= ETH_TX_PARSE_BD_E2_TCP_HDR_LENGTH_DW & (val << ETH_TX_PARSE_BD_E2_TCP_HDR_LENGTH_DW_SHIFT);

            if (packet->l2pkt_tx_info->lso_ip_hdr_len > 40) {
                parse_bd_e2->parsing_data |= ETH_TX_PARSE_BD_E2_IPV6_WITH_EXT_HDR;
            }

            parse_bd_e2->parsing_data = mm_cpu_to_le32(parse_bd_e2->parsing_data);
        }
    }

    if (!CHIP_IS_E1x(pdev))
    {
        struct eth_tx_parse_bd_e2 *parse_bd_e2 = parse_bd_ptr;

        ecore_set_fw_mac_addr(&parse_bd_e2->dst_mac_addr_hi, 
                              &parse_bd_e2->dst_mac_addr_mid, 
                              &parse_bd_e2->dst_mac_addr_lo, 
                              packet->l2pkt_tx_info->dst_mac_addr);

    }

    /* set dst addr type, if different from unicast */
    if ( IS_ETH_MULTICAST(packet->l2pkt_tx_info->dst_mac_addr) )
    {
        if( IS_ETH_BROADCAST(packet->l2pkt_tx_info->dst_mac_addr) )
        {
            eth_addr_type = BROADCAST_ADDRESS;
        }
        else
        {
            eth_addr_type = MULTICAST_ADDRESS;
        }

        RESET_FLAGS(start_bd->general_data, ETH_TX_START_BD_ETH_ADDR_TYPE);
        start_bd->general_data |= (eth_addr_type << ETH_TX_START_BD_ETH_ADDR_TYPE_SHIFT);
    }

    // Save the number of BDs used.  Later we need to add this value back
    // to tx_chain->bd_left when the packet is sent.
    packet->u1.tx.bd_used = start_bd_nbd += (u16_t)frags->cnt;

    packet->u1.tx.next_bd_idx = lm_bd_chain_prod_idx(&tx_chain->bd_chain);
    tx_chain->prod_bseq += packet->size;

    /* There is a PBF limitation on minimum packet size (9B)
     * We assert since we do not expect packet length < 14 */
    DbgBreakIfFastPath(packet->size < ETHERNET_PACKET_HEADER_SIZE);

#if DBG
    for(cnt = 0; cnt < start_bd_nbd; cnt++)
    {
        if (parse_bd_ptr && (cnt == 1))
        {
            if (CHIP_IS_E1x(pdev))
            {
                DbgMessage1(pdev, VERBOSEl2tx,
                            "   parse_bd: global_data 0x%x",
                            ((struct eth_tx_parse_bd_e1x *)(&start_bd[cnt]))->global_data);
            }
            else /* E2 */
            {
                DbgMessage1(pdev, VERBOSEl2tx,
                            "   parse_bd: parsing_data 0x%08x",
                            mm_le32_to_cpu(((struct eth_tx_parse_bd_e2 *)(&start_bd[cnt]))->parsing_data));
            }
        }
        else
        {
            DbgMessage5(pdev, VERBOSEl2tx,
                        "-> frag: %d, bd_flags: %d, nbytes: %d, hi: 0x%x, lo: 0x%x",
                        cnt, start_bd[cnt].bd_flags.as_bitfield, mm_le16_to_cpu(start_bd[cnt].nbytes), 
                        mm_le32_to_cpu(start_bd[cnt].addr_hi), mm_le32_to_cpu(start_bd[cnt].addr_lo));
            if (cnt == 0)
            {
                DbgMessage3(pdev, VERBOSEl2tx,
                            "      start bd info: nbds: %d, vlan: 0x%x, hdr_nbds: %d",
                            start_bd_nbd, mm_le16_to_cpu(start_bd->vlan_or_ethertype),
                            (start_bd->general_data & ETH_TX_START_BD_HDR_NBDS));
            }
        }
    }
#endif

    start_bd->nbd = mm_cpu_to_le16(start_bd_nbd);

    s_list_push_tail(&tx_chain->active_descq, &packet->link);

    //in case of a packet consisting of 1 frag only, but with the use of parsing info BD, 
    //the last_bd will point to the START BD!
    //this is since we need to mark both the START & END on the START BD.
    //Only the start BD can fill the flags and we always have 2 BDs. 

    // Debug message on the parsed_bd
    //DbgMessage4(pdev, INFORM,"lm_send_packet() parse_bd: total_hlen %d ip_hlen %d lso_mss %d tcp_flags 0x%x\n",
    //        parse_bd->total_hlen, parse_bd->ip_hlen, parse_bd->lso_mss, parse_bd->tcp_flags);
    //DbgMessage1(pdev, INFORM,"lm_send_packet() start_bd: bd_flags 0x%x\n",start_bd->bd_flags);
    
    // Make sure that the BD data is updated before updating the producer
    // since FW might read the BD right after the producer is updated.
    // This is only applicable for weak-ordered memory model archs such
    // as IA-64, The following barrier is also mandatory since FW will
    // assumes packets must have BDs
    //order is crucial in case of preemption
    pdev->tx_info.chain[chain_idx].eth_tx_prods.bds_prod = pdev->tx_info.chain[chain_idx].eth_tx_prods.bds_prod +
                                S16_SUB(lm_bd_chain_prod_idx(&tx_chain->bd_chain), old_prod_idx);
    pdev->tx_info.chain[chain_idx].eth_tx_prods.packets_prod = pdev->tx_info.chain[chain_idx].eth_tx_prods.packets_prod + 1;

    //DB
    dq_msg.header.data  = DOORBELL_HDR_T_DB_TYPE; /* tx doorbell normal doorbell type eth */
    dq_msg.zero_fill1   = 0;
    dq_msg.prod         = pdev->tx_info.chain[chain_idx].eth_tx_prods.bds_prod;

    // Make sure that the BD data is updated before updating the producer
    // since FW might read the BD right after the producer is updated.
    // This is only applicable for weak-ordered memory model archs such
    // as IA-64, The following barrier is also mandatory since FW will
    // assumes packets must have BDs
    //order is crucial in case of preemption
    mm_write_barrier();
    DOORBELL(pdev, chain_idx, *((u32_t *)&dq_msg));

    return LM_STATUS_SUCCESS;
} /* lm_send_packet */

/*******************************************************************************
 * Description:
 *
 * Return:
 ******************************************************************************/
u32_t
lm_get_packets_sent( struct _lm_device_t* pdev,
    u32_t chain_idx,
    s_list_t *sent_list)
{
    lm_tx_chain_t* tx_chain = &LM_TXQ(pdev, chain_idx);
    lm_packet_t*   pkt      = 0;
    u32_t          pkt_cnt  = 0;
    u16_t          old_idx  = lm_bd_chain_cons_idx(&tx_chain->bd_chain);

    /* Get the new consumer idx.  The bd's between new_idx and old_idx
     * are bd's that have been consumered by the chip. */
    u16_t new_idx = mm_le16_to_cpu(*(tx_chain->hw_con_idx_ptr));
    u16_t pkt_num = S16_SUB(new_idx,tx_chain->pkt_idx);

    //We work here with packets granularity(pkt_idx) as opposed to Teton which 
    //work in BDs granularity. the cons_idx is not relevant anymore in Tx chain, but
    //we keep it for debugging as the firmware still maintains a BD consumer.



    DbgBreakIfFastPath(pkt_num == 0);

    while(pkt_num > 0)
    {
        pkt = (lm_packet_t *) s_list_peek_head(&tx_chain->active_descq);

        //instead of the assert, lets check the db counter in the hw!
        //DbgBreakIfFastPath(pkt == NULL);
        if (pkt == NULL)
        {
#if !(defined(UEFI) || defined(DOS) || defined(__LINUX))
            lm_idle_chk(PFDEV(pdev));

            lm_get_doorbell_info(PFDEV(pdev));

            lm_get_storms_assert(PFDEV(pdev));
#endif
 
            DbgBreakIfFastPath(pkt == NULL);

            return pkt_cnt;
        }

        // TODO check LSO condition as in teton
        pkt = (lm_packet_t *) s_list_pop_head(&tx_chain->active_descq);

        /* Advance the old_idx to the start bd_idx of the next packet. */
        old_idx = pkt->u1.tx.next_bd_idx;

        pkt->status = LM_STATUS_SUCCESS;

        lm_bd_chain_bds_consumed(&tx_chain->bd_chain, pkt->u1.tx.bd_used);

        if (pkt->u1.tx.coalesce_buf) {
            /* return coalesce buffer to the chain's pool */
            lm_put_coalesce_buffer(pdev, tx_chain, pkt->u1.tx.coalesce_buf);
            pkt->u1.tx.coalesce_buf = NULL;
        }
         
        /* Get an updated new_idx from the status block.  The index may
         * end at the last BD of a page.  This BD is a pointer to the next
         * BD page which we need to skip over. */
        //TODO: need to verify that we have fairness among other protocols since we are also using the 
        //      in_dpc_loop_cnt - so don't starve!
        new_idx = mm_le16_to_cpu(*(tx_chain->hw_con_idx_ptr));
        tx_chain->pkt_idx++;
        pkt_num = S16_SUB(new_idx,tx_chain->pkt_idx);
        pkt_cnt++;
        s_list_push_tail(sent_list, &pkt->link);
    }

    // TODO: currently bd_chain doesn't maintain the cons_idx... 
    tx_chain->bd_chain.cons_idx = old_idx;
        
    DbgMessage4(pdev, INFORMl2tx , "lm_get_packets_sent()- func: %d, txidx: %d, txbd con: %d txbd prod: %d \n",
        FUNC_ID(pdev), chain_idx , lm_bd_chain_cons_idx(&tx_chain->bd_chain), lm_bd_chain_prod_idx(&tx_chain->bd_chain));

    return pkt_cnt;
} /* lm_get_packets_sent */
