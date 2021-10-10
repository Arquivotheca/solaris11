#include "lm5710.h"
#include "command.h"
#include "bd_chain.h"


// TODO - move to common place (.h file - the problem - prevent re-defination)
#ifndef FORCEINLINE
#if defined(_MSC_VER) && (_MSC_VER >= 1200) // Windows
#define FORCEINLINE __forceinline
#else
#define FORCEINLINE __inline
#endif
#endif

#define OOO_CID_USTRORM_PROD_DIFF           (0x4000)
/*******************************************************************************
 * Description:
 *  set both rcq, rx bd and rx sge (if valid) prods
 * Return:
 ******************************************************************************/
static void FORCEINLINE lm_rx_set_prods( lm_device_t     *pdev,
                                         u16_t const     iro_prod_offset,
                                         lm_bd_chain_t   *rcq_chain_bd,
                                         lm_bd_chain_t   *rx_chain_bd,
                                         lm_bd_chain_t   *rx_chain_sge,
                                         const u32_t     chain_idx )
{
    lm_rx_chain_t*  rxq_chain   = &LM_RXQ(pdev, chain_idx);
    u32_t           val32       = 0;
    u64_t           val64       = 0;
    u16_t           val16_lo    = lm_bd_chain_prod_idx(rcq_chain_bd);
    u16_t           val16_hi    = lm_bd_chain_prod_idx(rx_chain_bd);

    if (IS_CHANNEL_VFDEV(pdev))
    {
       LM_INTMEM_WRITE32(PFDEV(pdev),
                         iro_prod_offset,
                         val32,
                         VF_BAR0_USDM_QUEUES_OFFSET);
        return;
    }

    if(OOO_CID(pdev) == chain_idx)
    {
        DbgBreakIfFastPath( NULL != rx_chain_sge );

        LM_INTMEM_WRITE16(PFDEV(pdev),
                          TSTORM_ISCSI_L2_ISCSI_OOO_PROD_OFFSET(FUNC_ID(pdev)),
                          rxq_chain->bd_prod_without_next,
                          BAR_TSTRORM_INTMEM);

        // Ugly FW solution OOO FW wants the
        val16_lo    += OOO_CID_USTRORM_PROD_DIFF;
        val16_hi    += OOO_CID_USTRORM_PROD_DIFF;
    }

    val32       = ((u32_t)(val16_hi << 16) | val16_lo);

    //notify the fw of the prod of the RCQ. No need to do that for the Rx bd chain.
    if( rx_chain_sge )
    {
        val64 = (((u64_t)lm_bd_chain_prod_idx(rx_chain_sge))<<32) | val32 ;
        
        LM_INTMEM_WRITE64(PFDEV(pdev),
                          iro_prod_offset,
                          val64,
                          BAR_USTRORM_INTMEM);

        DbgBreakIfFastPath( !lm_bd_chains_are_consistent( rx_chain_sge, rx_chain_bd ) );
    }
    else
    {
        LM_INTMEM_WRITE32(PFDEV(pdev),
                          iro_prod_offset,
                          val32,
                          BAR_USTRORM_INTMEM);
    }    
}

/*******************************************************************************
 * Description:
 *  rx_chain_bd always valid, rx_chain_sge valid only in case we are LAH enabled in this queue
 *  all if() checking will be always done on rx_chain_bd since he is always valid and sge should be consistent 
 *  We verify it in case sge is valid
 *  all bd_xxx operations will be done on both
 * Return:
 ******************************************************************************/
u32_t
lm_post_buffers(
    lm_device_t *pdev,
    u32_t chain_idx,
    lm_packet_t *packet)    /* optional. */
{
    lm_rx_chain_t*     rxq_chain     = &LM_RXQ(pdev, chain_idx);
    lm_rcq_chain_t*    rcq_chain     = &LM_RCQ(pdev, chain_idx);
    lm_bd_chain_t*     rx_chain_bd   = &LM_RXQ_CHAIN_BD(pdev, chain_idx);
    lm_bd_chain_t*     rx_chain_sge  = LM_RXQ_SGE_PTR_IF_VALID(pdev, chain_idx);
    u32_t              pkt_queued    = 0;
    struct eth_rx_bd*  cur_bd        = NULL;
    struct eth_rx_sge* cur_sge       = NULL;
    u32_t              prod_bseq     = 0;
    u32_t              rcq_prod_bseq = 0;

    DbgMessage(pdev, INFORMl2 , "### lm_post_buffers\n");

    // Verify BD's consistent
    DbgBreakIfFastPath( rx_chain_sge && !lm_bd_chains_are_consistent( rx_chain_sge, rx_chain_bd ) );

    /* Make sure we have a bd left for posting a receive buffer. */
    if(packet)
    {
        DbgBreakIfFastPath(SIG(packet) != L2PACKET_RX_SIG);

        if(lm_bd_chain_is_empty(rx_chain_bd))
        {
            s_list_push_tail(&rxq_chain->free_descq, &packet->link);
            packet = NULL;
        }
    }
    else if(!lm_bd_chain_is_empty(rx_chain_bd))
    {
        packet = (lm_packet_t *) s_list_pop_head(&rxq_chain->free_descq);
    }

    prod_bseq     = rxq_chain->prod_bseq;
    rcq_prod_bseq = rcq_chain->prod_bseq;

    while(packet)
    {
        cur_bd  = lm_bd_chain_produce_bd(rx_chain_bd);
        rxq_chain->bd_prod_without_next++;
        cur_sge = rx_chain_sge ? lm_bd_chain_produce_bd(rx_chain_sge) : NULL;

        prod_bseq += packet->l2pkt_rx_info->mem_size;

        //take care of the RCQ related prod stuff. 
        rcq_prod_bseq += packet->l2pkt_rx_info->mem_size;

        /* These were actually produced before by fw, but we only produce them now to make sure they're synced with the rx-chain */
        lm_bd_chain_bd_produced(&rcq_chain->bd_chain);

        packet->u1.rx.next_bd_idx = lm_bd_chain_prod_idx(rx_chain_bd);
#if L2_RX_BUF_SIG
        /* make sure signitures exist before and after the buffer */
        DbgBreakIfFastPath(SIG(packet->u1.rx.mem_virt - pdev->params.rcv_buffer_offset) != L2PACKET_RX_SIG);
        DbgBreakIfFastPath(END_SIG(packet->u1.rx.mem_virt, MAX_L2_CLI_BUFFER_SIZE(pdev, chain_idx)) != L2PACKET_RX_SIG);
#endif /* L2_RX_BUF_SIG */
        
        cur_bd->addr_lo  = mm_cpu_to_le32(packet->u1.rx.mem_phys[0].as_u32.low);
        cur_bd->addr_hi  = mm_cpu_to_le32(packet->u1.rx.mem_phys[0].as_u32.high);

        if( cur_sge )
        {
            cur_sge->addr_lo = mm_cpu_to_le32(packet->u1.rx.mem_phys[1].as_u32.low);
            cur_sge->addr_hi = mm_cpu_to_le32(packet->u1.rx.mem_phys[1].as_u32.high);
        }

        //DbgMessage3(pdev, VERBOSEl2sp , "pkt %p, idx 0x%x, left %d\n",
        //   packet, lm_bd_chain_prod_idx(&rxq_chain->bd_chain), lm_bd_chain_avail_bds(&rxq_chain->bd_chain));

        s_list_push_tail(&rxq_chain->active_descq, &packet->link);
        pkt_queued++;

        /* the assumption is that the number of cqes is less or equal to the corresponding rx bds,
           therefore if there no cqes left, break */
        if(lm_bd_chain_is_empty(&rcq_chain->bd_chain))
        {
            break;
        }

        packet = (lm_packet_t *) s_list_pop_head(&rxq_chain->free_descq);
    }

    rxq_chain->prod_bseq = prod_bseq;

    //update the prod of the RCQ only AFTER the Rx bd!
    rcq_chain->prod_bseq = rcq_prod_bseq;

    if(pkt_queued)
    {        

#ifdef LITTLE_ENDIAN
#define PROD_OFFSET_CQE 0
#define PROD_OFFSET_BD  2
#define PROD_OFFSET_SGE 4
#else // BIG_ENDIAN
#define PROD_OFFSET_CQE 2
#define PROD_OFFSET_BD  0
#define PROD_OFFSET_SGE 6
#endif // LITTLE_ENDIAN

        ASSERT_STATIC(OFFSETOF(struct ustorm_eth_rx_producers, cqe_prod)  ==  PROD_OFFSET_CQE) ;
        ASSERT_STATIC(OFFSETOF(struct ustorm_eth_rx_producers, bd_prod)   ==  PROD_OFFSET_BD) ;
        ASSERT_STATIC(OFFSETOF(struct ustorm_eth_rx_producers, sge_prod)  ==  PROD_OFFSET_SGE) ;

        //notify the fw of the prod
        lm_rx_set_prods(pdev, rcq_chain->iro_prod_offset, &rcq_chain->bd_chain, rx_chain_bd, rx_chain_sge ,chain_idx);
    }

    DbgMessage2(pdev, INFORMl2 , "lm_post_buffers - bd con: %d bd prod: %d \n",
                lm_bd_chain_cons_idx(rx_chain_bd),lm_bd_chain_prod_idx(rx_chain_bd));
    DbgMessage2(pdev, INFORMl2 , "lm_post_buffers - cq con: %d cq prod: %d \n",
                lm_bd_chain_cons_idx(&rcq_chain->bd_chain) ,lm_bd_chain_prod_idx(&rcq_chain->bd_chain));

    return pkt_queued;
} /* lm_post_buffers */


/*******************************************************************************
 * Description:
 * Here the RCQ chain is the chain coordinated with the status block, that is, 
 * the index in the status block describes the RCQ and NOT the rx_bd chain as in
 * the case of Teton. We run on the delta between the new consumer index of the RCQ
 * which we get from the sb and the old consumer index of the RCQ.
 * In cases of both slow and fast path, the consumer of the RCQ is always incremented.
 *
 * The assumption which we must stick to all the way is: RCQ and Rx bd chain  
 * have the same size at all times! Otherwise, so help us Alan Bertkey!
 *
 * Return:
 ******************************************************************************/
u32_t
lm_get_packets_rcvd( struct _lm_device_t  *pdev,
                     u32_t const          chain_idx,
                     s_list_t             *rcvd_list,
                     struct _sp_cqes_info *sp_cqes)
{
    lm_rx_chain_t*          rxq_chain       = &LM_RXQ(pdev, chain_idx); //get a hold of the matching Rx bd chain according to index
    lm_rcq_chain_t*         rcq_chain       = &LM_RCQ(pdev, chain_idx); //get a hold of the matching RCQ chain according to index 
    lm_bd_chain_t*          rx_chain_bd     = &LM_RXQ_CHAIN_BD(pdev, chain_idx);
    lm_bd_chain_t*          rx_chain_sge    = LM_RXQ_SGE_PTR_IF_VALID(pdev, chain_idx);
    union eth_rx_cqe*       cqe             = NULL;
    lm_packet_t*            pkt             = NULL;
    u32_t                   pkt_cnt         = 0;
    u16_t                   rx_old_idx      = 0;
    u16_t                   cq_new_idx      = 0;
    u16_t                   cq_old_idx      = 0;
    enum eth_rx_cqe_type    cqe_type        = MAX_ETH_RX_CQE_TYPE;

    DbgMessage(pdev, INFORMl2 , "lm_get_packets_rcvd inside!\n");

    /* make sure to zeroize the sp_cqes... */
    mm_mem_zero( sp_cqes, sizeof(struct _sp_cqes_info) );

    /* Get the new consumer idx.  The bd's between rcq_new_idx and rcq_old_idx
     * are bd's containing receive packets.
     */
    cq_new_idx = mm_le16_to_cpu(*(rcq_chain->hw_con_idx_ptr));

    /* The consumer index of the RCQ only, may stop at the end of a page boundary.  In
     * this case, we need to advance the next to the next one.
     * In here we do not increase the cons_bd as well! this is since we're dealing here
     * with the new cons index and not with the actual old one for which, as we progress, we 
     * need to maintain the bd_cons as well.
     */
    if((cq_new_idx & lm_bd_chain_usable_bds_per_page(&rcq_chain->bd_chain)) == lm_bd_chain_usable_bds_per_page(&rcq_chain->bd_chain))
    {
        cq_new_idx+= lm_bd_chain_bds_skip_eop(&rcq_chain->bd_chain);
    }

    DbgBreakIfFastPath( rx_chain_sge && !lm_bd_chains_are_consistent( rx_chain_sge, rx_chain_bd ) );
   
    rx_old_idx = lm_bd_chain_cons_idx(rx_chain_bd);
    cq_old_idx = lm_bd_chain_cons_idx(&rcq_chain->bd_chain);

    //there is no change in the RCQ consumer index so exit!
    if (cq_old_idx == cq_new_idx)
    {
        DbgMessage(pdev, INFORMl2rx , "there is no change in the RCQ consumer index so exit!\n");
        return pkt_cnt;
    }

    while(cq_old_idx != cq_new_idx)
    {
        DbgBreakIfFastPath(S16_SUB(cq_new_idx, cq_old_idx) <= 0);
        //get hold of the cqe, and find out what it's type corresponds to
        cqe = (union eth_rx_cqe *)lm_bd_chain_consume_bd(&rcq_chain->bd_chain);
        DbgBreakIfFastPath(cqe == NULL);

        //update the cons of the RCQ and the bd_prod pointer of the RCQ as well!
        //this holds both for slow and fast path!
        cq_old_idx = lm_bd_chain_cons_idx(&rcq_chain->bd_chain);
             
        cqe_type = GET_FLAGS(cqe->ramrod_cqe.ramrod_type, COMMON_RAMROD_ETH_RX_CQE_TYPE) >> COMMON_RAMROD_ETH_RX_CQE_TYPE_SHIFT;
        //the cqe is a ramrod, so do the ramrod and recycle the cqe.
        //TODO: replace this with the #defines: 1- eth ramrod, 2- toe init ofld ramrod
        switch(cqe_type)
        {
        case RX_ETH_CQE_TYPE_ETH_RAMROD:
            {
                /* 13/08/08 NirV: bugbug, temp workaround for dpc watch dog bug,
                 * ignore toe completions on L2 ring - initiate offload */
                if (cqe->ramrod_cqe.conn_type != TOE_CONNECTION_TYPE)
                {
                    if (ERR_IF(sp_cqes->idx >= MAX_NUM_SPE))
                    {
                        DbgBreakMsgFastPath("too many spe completed\n");
                        /* we shouldn't get here - there is something very wrong if we did... in this case we will risk 
                         * completing the ramrods - even though we're holding a lock!!! */
                        /* bugbug... */
                        DbgBreakIfAll(sp_cqes->idx >= MAX_NUM_SPE);
                        return pkt_cnt;
                    }
                    mm_memcpy((void*)(&(sp_cqes->sp_cqe[sp_cqes->idx++])), (const void*)cqe, sizeof(*cqe));
                }
    
                //update the prod of the RCQ - by this, we recycled the CQE.
                lm_bd_chain_bd_produced(&rcq_chain->bd_chain);
                
    #if 0
                //in case of ramrod, pop out the Rx bd and push it to the free descriptors list
                pkt = (lm_packet_t *) s_list_pop_head(&rxq_chain->active_descq);
                
                DbgBreakIfFastPath(pkt == NULL);
    
                s_list_push_tail( &LM_RXQ(pdev, chain_idx).free_descq,
                                  &pkt->link);
    #endif
                break;
            } 
        case RX_ETH_CQE_TYPE_ETH_FASTPATH: 
            { //enter here in case the cqe is a fast path type (data)
                u16_t parse_flags = mm_le16_to_cpu(cqe->fast_path_cqe.pars_flags.flags);
    
                DbgMessage1(pdev, INFORMl2rx , "lm_get_packets_rcvd- it is fast path, func=%d\n", FUNC_ID(pdev));
                pkt = (lm_packet_t *) s_list_pop_head(&rxq_chain->active_descq);
                
                DbgBreakIfFastPath( NULL == pkt );
    
    #if DBG
                if CHK_NULL( pkt )
                {
                    return 0;
                }
    #endif // DBG
    
                DbgBreakIfFastPath(SIG(pkt) != L2PACKET_RX_SIG);
    
    #if L2_RX_BUF_SIG
                /* make sure signitures exist before and after the buffer */
                DbgBreakIfFastPath(SIG(pkt->u1.rx.mem_virt - pdev->params.rcv_buffer_offset) != L2PACKET_RX_SIG);
                DbgBreakIfFastPath(END_SIG(pkt->u1.rx.mem_virt, MAX_L2_CLI_BUFFER_SIZE(pdev, chain_idx)) != L2PACKET_RX_SIG);
    #endif /* L2_RX_BUF_SIG */
    
                lm_bd_chain_bds_consumed(rx_chain_bd, 1);
                if( rx_chain_sge )
                {
                    lm_bd_chain_bds_consumed(rx_chain_sge, 1);
                }
    #if defined(_NTDDK_)  
    //PreFast 28182 :Prefast reviewed and suppress this situation shouldn't occur.
    #pragma warning (push)
    #pragma warning( disable:28182 ) 
    #endif // !_NTDDK_
                /* Advance the rx_old_idx to the start bd_idx of the next packet. */
                rx_old_idx = pkt->u1.rx.next_bd_idx;
                //cq_old_idx = pkt->u1.rx.next_bd_idx;
    
                pkt->status = LM_STATUS_SUCCESS;
                
                //changed, as we dont have fhdr infrastructure
                pkt->size        = mm_le16_to_cpu(cqe->fast_path_cqe.pkt_len); //- 4; /* CRC32 */
                CLEAR_FLAGS( pkt->l2pkt_rx_info->flags );
    
                DbgMessage1(pdev, VERBOSEl2, "pkt_size: %d\n",pkt->size);
    
                //optimized
                /* make sure packet size if larger than header size and smaller than max packet size of the specific L2 client */
                DbgBreakIfFastPath((pkt->size < MIN_ETHERNET_PACKET_SIZE) || (pkt->size > MAX_CLI_PACKET_SIZE(pdev, chain_idx)));
    
                if(OOO_CID(pdev) == chain_idx)
                {
                    DbgBreakIfFastPath( ETH_FP_CQE_RAW != (GET_FLAGS( cqe->fast_path_cqe.type_error_flags, ETH_FAST_PATH_RX_CQE_SGL_RAW_SEL ) >>
                                                           ETH_FAST_PATH_RX_CQE_SGL_RAW_SEL_SHIFT));
                    
                    //optimized
                    /* make sure packet size if larger than header size and smaller than max packet size of the specific L2 client */
                    // TODO_OOO - check with flag
                    ASSERT_STATIC( sizeof(pkt->u1.rx.sgl_or_raw_data.raw_data) == sizeof(cqe->fast_path_cqe.sgl_or_raw_data.raw_data) );
                    mm_memcpy( pkt->u1.rx.sgl_or_raw_data.raw_data, cqe->fast_path_cqe.sgl_or_raw_data.raw_data, sizeof(pkt->u1.rx.sgl_or_raw_data.raw_data) );                
                }
                else
                {
                    DbgBreakIfFastPath( ETH_FP_CQE_REGULAR != (GET_FLAGS( cqe->fast_path_cqe.type_error_flags, ETH_FAST_PATH_RX_CQE_SGL_RAW_SEL )>>
                                                           ETH_FAST_PATH_RX_CQE_SGL_RAW_SEL_SHIFT) );
                }
    
                if GET_FLAGS(cqe->fast_path_cqe.status_flags, ETH_FAST_PATH_RX_CQE_RSS_HASH_FLG)
                {
                    SET_FLAGS(pkt->l2pkt_rx_info->flags, LM_RX_FLAG_VALID_HASH_VALUE );
                    *pkt->u1.rx.hash_val_ptr = mm_le32_to_cpu(cqe->fast_path_cqe.rss_hash_result);
                }
                
                if(GET_FLAGS(parse_flags,PARSING_FLAGS_INNER_VLAN_EXIST))
                {
                    u16_t vlan_tag = mm_le16_to_cpu(cqe->fast_path_cqe.vlan_tag);
    
                    DbgMessage1(pdev, INFORMl2, "vlan frame recieved: %x\n",vlan_tag);
                      /* fw always set ETH_FAST_PATH_RX_CQE_VLAN_TAG_FLG and pass vlan tag when
                         packet with vlan arrives but it remove the vlan from the packet only when
                         it configured to remove vlan using params.vlan_removal_enable  
                      */
                      if ((!pdev->params.keep_vlan_tag) &&
                          ( OOO_CID(pdev) != chain_idx) &&
                          (!(IS_MF_NIV_MODE(pdev)&&(vlan_tag == NIV_DEFAULT_VLAN(pdev)))))
                      {
                          SET_FLAGS(pkt->l2pkt_rx_info->flags , LM_RX_FLAG_VALID_VLAN_TAG);
                          pkt->l2pkt_rx_info->vlan_tag = vlan_tag;
                          DbgMessage1(pdev, INFORMl2rx, "vlan removed from frame: %x\n",vlan_tag);
                      }
                }
                
                /* check if IP datagram (either IPv4 or IPv6) */
                if(((GET_FLAGS(parse_flags,PARSING_FLAGS_OVER_ETHERNET_PROTOCOL) >> 
                    PARSING_FLAGS_OVER_ETHERNET_PROTOCOL_SHIFT) == PRS_FLAG_OVERETH_IPV4) ||
                   ((GET_FLAGS(parse_flags,PARSING_FLAGS_OVER_ETHERNET_PROTOCOL) >> 
                    PARSING_FLAGS_OVER_ETHERNET_PROTOCOL_SHIFT) == PRS_FLAG_OVERETH_IPV6))
                {
                    pkt->l2pkt_rx_info->flags  |= 
                        (GET_FLAGS(parse_flags,PARSING_FLAGS_OVER_ETHERNET_PROTOCOL) >>
                         PARSING_FLAGS_OVER_ETHERNET_PROTOCOL_SHIFT) == PRS_FLAG_OVERETH_IPV4 ?
                        LM_RX_FLAG_IS_IPV4_DATAGRAM :
                        LM_RX_FLAG_IS_IPV6_DATAGRAM;
                    if(!GET_FLAGS(cqe->fast_path_cqe.status_flags, ETH_FAST_PATH_RX_CQE_IP_XSUM_NO_VALIDATION_FLG))
                    {
                        /* ip cksum validated */
                        if GET_FLAGS(cqe->fast_path_cqe.type_error_flags, ETH_FAST_PATH_RX_CQE_IP_BAD_XSUM_FLG)
                        {
                            /* invalid ip cksum */
                            SET_FLAGS(pkt->l2pkt_rx_info->flags, LM_RX_FLAG_IP_CKSUM_IS_BAD);
    
                            LM_COMMON_DRV_STATS_ATOMIC_INC_ETH(pdev, rx_ip_cs_error_count);
                        }
                        else
                        {
                            /* valid ip cksum */
                            SET_FLAGS(pkt->l2pkt_rx_info->flags, LM_RX_FLAG_IP_CKSUM_IS_GOOD);
                        }
                    }
                }
    
                if(!GET_FLAGS(parse_flags,PARSING_FLAGS_FRAGMENTATION_STATUS))
                {
                    /* check if TCP segment */
                    if((GET_FLAGS(parse_flags,PARSING_FLAGS_OVER_IP_PROTOCOL) >> 
                        PARSING_FLAGS_OVER_IP_PROTOCOL_SHIFT) == PRS_FLAG_OVERIP_TCP)
                    {
                        SET_FLAGS(pkt->l2pkt_rx_info->flags, LM_RX_FLAG_IS_TCP_SEGMENT);
                        DbgMessage(pdev, INFORM, "--- TCP Packet --- \n");
                    }
                    /* check if UDP segment */
                    else if((GET_FLAGS(parse_flags,PARSING_FLAGS_OVER_IP_PROTOCOL) >> 
                             PARSING_FLAGS_OVER_IP_PROTOCOL_SHIFT) == PRS_FLAG_OVERIP_UDP)
                    {
                        SET_FLAGS(pkt->l2pkt_rx_info->flags , LM_RX_FLAG_IS_UDP_DATAGRAM);
                        DbgMessage(pdev, INFORM, "--- UDP Packet --- \n");
                    }
                }
    
                
                /* check if udp/tcp cksum was validated */
                if( GET_FLAGS(pkt->l2pkt_rx_info->flags, (LM_RX_FLAG_IS_TCP_SEGMENT | LM_RX_FLAG_IS_UDP_DATAGRAM)) &&
                   !GET_FLAGS(cqe->fast_path_cqe.status_flags, ETH_FAST_PATH_RX_CQE_L4_XSUM_NO_VALIDATION_FLG))
                {
    #define SHIFT_IS_GOOD  1
    #define SHIFT_IS_BAD   2
                    ASSERT_STATIC(LM_RX_FLAG_UDP_CKSUM_IS_GOOD == LM_RX_FLAG_IS_UDP_DATAGRAM << SHIFT_IS_GOOD);
                    ASSERT_STATIC(LM_RX_FLAG_UDP_CKSUM_IS_BAD  == LM_RX_FLAG_IS_UDP_DATAGRAM << SHIFT_IS_BAD);
                    ASSERT_STATIC(LM_RX_FLAG_TCP_CKSUM_IS_GOOD == LM_RX_FLAG_IS_TCP_SEGMENT  << SHIFT_IS_GOOD);
                    ASSERT_STATIC(LM_RX_FLAG_TCP_CKSUM_IS_BAD  == LM_RX_FLAG_IS_TCP_SEGMENT  << SHIFT_IS_BAD);

                    DbgMessage(pdev, INFORM, "  Checksum validated.\n");
    
                    /* tcp/udp cksum validated */
                    if GET_FLAGS(cqe->fast_path_cqe.type_error_flags, ETH_FAST_PATH_RX_CQE_L4_BAD_XSUM_FLG)
                    {
                        /* invalid tcp/udp cksum */
                        SET_FLAGS(pkt->l2pkt_rx_info->flags , ( GET_FLAGS(pkt->l2pkt_rx_info->flags, (LM_RX_FLAG_IS_TCP_SEGMENT | LM_RX_FLAG_IS_UDP_DATAGRAM)) << SHIFT_IS_BAD ) );
    
                        LM_COMMON_DRV_STATS_ATOMIC_INC_ETH(pdev, rx_tcp_cs_error_count);
                        DbgMessage(pdev, INFORM, "  BAD checksum.\n");
                    }
                    else if (GET_FLAGS(pkt->l2pkt_rx_info->flags , LM_RX_FLAG_IP_CKSUM_IS_BAD))
                    {
                        /* invalid tcp/udp cksum due to invalid ip cksum */
                        SET_FLAGS(pkt->l2pkt_rx_info->flags , ( GET_FLAGS(pkt->l2pkt_rx_info->flags, (LM_RX_FLAG_IS_TCP_SEGMENT | LM_RX_FLAG_IS_UDP_DATAGRAM)) << SHIFT_IS_BAD ) );
                        DbgMessage(pdev, INFORM, "  BAD IP checksum\n");
                    }
                    else
                    {
                        /* valid tcp/udp cksum */
                        SET_FLAGS(pkt->l2pkt_rx_info->flags , ( GET_FLAGS(pkt->l2pkt_rx_info->flags, (LM_RX_FLAG_IS_TCP_SEGMENT | LM_RX_FLAG_IS_UDP_DATAGRAM)) << SHIFT_IS_GOOD ) );
                        DbgMessage(pdev, INFORM, "  GOOD checksum.\n");
                    }
                }
                else
                {
                    DbgMessage(pdev, INFORM, "  Checksum NOT validated.\n");
                    /*Packets with invalid TCP options are reported with L4_XSUM_NO_VALIDATION due to HW limitation. In this case we assume that 
                      their checksum is OK.*/
                    if(GET_FLAGS(pkt->l2pkt_rx_info->flags, (LM_RX_FLAG_IS_TCP_SEGMENT | LM_RX_FLAG_IS_UDP_DATAGRAM)) &&
                       GET_FLAGS(cqe->fast_path_cqe.status_flags, ETH_FAST_PATH_RX_CQE_L4_XSUM_NO_VALIDATION_FLG) &&
                       GET_FLAGS(cqe->fast_path_cqe.pars_flags.flags, PARSING_FLAGS_TCP_OPTIONS_EXIST))
                    {
                        DbgMessage(pdev, INFORM, "  TCP Options exist - forcing return value.\n");
                        if(GET_FLAGS(pkt->l2pkt_rx_info->flags , LM_RX_FLAG_IP_CKSUM_IS_BAD))
                        {
                            DbgMessage(pdev, INFORM, "  IP checksum invalid - reporting BAD checksum.\n");
                            SET_FLAGS(pkt->l2pkt_rx_info->flags , ( GET_FLAGS(pkt->l2pkt_rx_info->flags, (LM_RX_FLAG_IS_TCP_SEGMENT | LM_RX_FLAG_IS_UDP_DATAGRAM)) << SHIFT_IS_BAD ) );
                        }
                        else
                        {
                            DbgMessage(pdev, INFORM, "  IP checksum ok - reporting GOOD checksum.\n");
                            SET_FLAGS(pkt->l2pkt_rx_info->flags , ( GET_FLAGS(pkt->l2pkt_rx_info->flags, (LM_RX_FLAG_IS_TCP_SEGMENT | LM_RX_FLAG_IS_UDP_DATAGRAM)) << SHIFT_IS_GOOD ) );
                        }
                    }
                }
    #if defined(_NTDDK_)  
    #pragma warning (pop)
    #endif // !_NTDDK_
    #if DBG
                if(GET_FLAGS(parse_flags,PARSING_FLAGS_FRAGMENTATION_STATUS))
                {
                    LM_COMMON_DRV_STATS_ATOMIC_INC_ETH(pdev, rx_ipv4_frag_count);
                }
                if(GET_FLAGS(parse_flags,PARSING_FLAGS_LLC_SNAP))
                {
                    LM_COMMON_DRV_STATS_ATOMIC_INC_ETH(pdev, rx_llc_snap_count);
                }
                if(GET_FLAGS(parse_flags,PARSING_FLAGS_IP_OPTIONS) &&
                    GET_FLAGS(pkt->l2pkt_rx_info->flags ,LM_RX_FLAG_IS_IPV6_DATAGRAM))
                {
                    LM_COMMON_DRV_STATS_ATOMIC_INC_ETH(pdev, rx_ipv6_ext_count);
                }
    #endif // DBG
    
                /* We use to assert that if we got the PHY_DECODE_ERROR it was always a result of DROP_MAC_ERR, since we don't configure
                 * DROP_MAC_ERR anymore, we don't expect this flag to ever be on.*/
                DbgBreakIfFastPath( GET_FLAGS(cqe->fast_path_cqe.type_error_flags, ETH_FAST_PATH_RX_CQE_PHY_DECODE_ERR_FLG) );
    
                DbgBreakIfFastPath(cqe->fast_path_cqe.type_error_flags &
                                ~(ETH_FAST_PATH_RX_CQE_PHY_DECODE_ERR_FLG |
                                  ETH_FAST_PATH_RX_CQE_IP_BAD_XSUM_FLG |
                                  ETH_FAST_PATH_RX_CQE_L4_BAD_XSUM_FLG |
                                  ETH_FAST_PATH_RX_CQE_SGL_RAW_SEL));
    
    
                pkt_cnt++;
                s_list_push_tail(rcvd_list, &pkt->link);
                break;
            }
            
        case RX_ETH_CQE_TYPE_ETH_START_AGG:
        case RX_ETH_CQE_TYPE_ETH_STOP_AGG:
        case MAX_ETH_RX_CQE_TYPE:
        default:
            {
                DbgBreakMsg("CQE type not supported");
            }
        }
    }

    // TODO: Move index update to a more suitable place
    rx_chain_bd->cons_idx = rx_old_idx;
    if( rx_chain_sge )
    {
        rx_chain_sge->cons_idx = rx_old_idx;
    }

    //notify the fw of the prod
    lm_rx_set_prods(pdev, rcq_chain->iro_prod_offset, &rcq_chain->bd_chain, rx_chain_bd, rx_chain_sge ,chain_idx);

    DbgMessage2(pdev, INFORMl2rx, "lm_get_packets_rcvd- bd con: %d bd prod: %d \n",
                                lm_bd_chain_cons_idx(rx_chain_bd), lm_bd_chain_prod_idx(rx_chain_bd));
    DbgMessage2(pdev, INFORMl2rx , "lm_get_packets_rcvd- cq con: %d cq prod: %d \n",
                                lm_bd_chain_cons_idx(&rcq_chain->bd_chain), lm_bd_chain_prod_idx(&rcq_chain->bd_chain));
    return pkt_cnt;
} /* lm_get_packets_rcvd */

lm_status_t lm_complete_ramrods(
    struct _lm_device_t *pdev, 
    struct _sp_cqes_info *sp_cqes)
{
    u8_t idx;
    
    for (idx = 0; idx < sp_cqes->idx; idx++) {
        lm_eth_init_command_comp(pdev, &(sp_cqes->sp_cqe[idx].ramrod_cqe));
    }

    return LM_STATUS_SUCCESS;
}

/* called by um whenever packets are returned by client 
   rxq lock is taken by caller */
void
lm_return_packet_bytes( struct _lm_device_t *pdev,
                        u32_t const          qidx,
                        u32_t const          returned_bytes)
{
    lm_rx_chain_t *rxq = &LM_RXQ(pdev, qidx);

    if (IS_CHANNEL_VFDEV(pdev))
    {
        //DbgMessage(pdev, FATAL, "FW-producer update Not implemented for Channel-VF yet\n");
        return;
    }

    rxq->ret_bytes += returned_bytes;

    /* aggregate updates over PCI */

    /* HC_RET_BYTES_TH = min(l2_hc_threshold0 / 2 , 16KB) */
    #define HC_RET_BYTES_TH(pdev) (((pdev)->params.hc_threshold0[SM_RX_ID] < 32768) ? ((pdev)->params.hc_threshold0[SM_RX_ID] >> 1) : 16384)

    /* TODO: Future: Add #updatesTH = 20 */

    /* time to update fw ? */
    if(S32_SUB(rxq->ret_bytes, rxq->ret_bytes_last_fw_update + HC_RET_BYTES_TH(pdev)) >= 0)
    {
        /*
          !!DP
          The test below is to disable dynamic HC for the iSCSI chains
        */
        // TODO: VF dhc
        if (qidx < LM_MAX_RSS_CHAINS(pdev) && IS_PFDEV(pdev)) /* should be fine, if not, you can go for less robust case of != LM_CLI_RX_CHAIN_IDX(pdev, LM_CLI_IDX_ISCSI) */
        {
            /* There are HC_USTORM_SB_NUM_INDICES (4) index values for each SB to set and we're using the corresponding U indexes from the microcode consts */
            LM_INTMEM_WRITE32(PFDEV(pdev), rxq->hc_sb_info.iro_dhc_offset, rxq->ret_bytes, BAR_CSTRORM_INTMEM);
            rxq->ret_bytes_last_fw_update = rxq->ret_bytes;
        }
    }
}

