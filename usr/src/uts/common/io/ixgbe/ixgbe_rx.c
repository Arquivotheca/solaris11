/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright(c) 2007-2011 Intel Corporation. All rights reserved.
 */

/*
 * Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include "ixgbe_sw.h"
#include <sys/sdt.h>

/*
 * Function prototypes
 */
static mblk_t *ixgbe_rx_copy(ixgbe_rx_data_t *, uint32_t, uint32_t);
static void ixgbe_rx_assoc_hcksum(mblk_t *, uint32_t);
static boolean_t ixgbe_rx_is_fcoe(union ixgbe_adv_rx_desc *);
static void ixgbe_rx_assoc_fcoe_rxcrc(mblk_t *, uint32_t);
static mblk_t *ixgbe_lro_bind(ixgbe_rx_data_t *, uint32_t, uint32_t, uint32_t);
static mblk_t *ixgbe_lro_copy(ixgbe_rx_data_t *, uint32_t, uint32_t);
static int ixgbe_lro_get_start(ixgbe_rx_data_t *, uint32_t);
static uint32_t ixgbe_lro_get_first(ixgbe_rx_data_t *, uint32_t);
static mblk_t *ixgbe_rx_loan(ixgbe_rx_data_t *rx_data, uint32_t index,
    uint32_t pkt_len);
static boolean_t ixgbe_rcb_sync(ixgbe_t *ixgbe, rx_control_block_t *rcb);
static void ixgbe_rcb_update(ixgbe_t *ixgbe, rx_control_block_t *rcb,
    mblk_t *mp);

/*
 * Inline function declarations.
 */
#ifndef IXGBE_DEBUG
#pragma inline(ixgbe_rx_is_fcoe)
#pragma inline(ixgbe_rcb_sync)
#pragma inline(ixgbe_rcb_update)
#pragma inline(ixgbe_rx_loan)
#pragma inline(ixgbe_rx_assoc_hcksum)
#pragma inline(ixgbe_rx_assoc_fcoe_rxcrc)
#pragma inline(ixgbe_lro_get_start)
#pragma inline(ixgbe_lro_get_first)
#endif

/*
 * ixgbe_ddp_buf_recycle - The call-back function to reclaim ddp rx buffer.
 *
 * This function is called when an mp is freed by the user thru
 * freeb call (Only for mp constructed through desballoc call).
 * It returns back the freed buffer to the free list.
 */
void
ixgbe_ddp_buf_recycle(caddr_t arg)
{
	ixgbe_t *ixgbe;
	ixgbe_rx_fcoe_t *rx_fcoe;
	ixgbe_fcoe_ddp_buf_t *recycle_ddp_buf;
	uint32_t free_index;
	uint32_t ref_cnt;
	uint32_t i;
	uint32_t ubd_count;

	recycle_ddp_buf = (ixgbe_fcoe_ddp_buf_t *)(uintptr_t)arg;
	rx_fcoe = recycle_ddp_buf->rx_fcoe;
	ixgbe = rx_fcoe->ixgbe;

	if (recycle_ddp_buf->ref_cnt == 0) {
		/*
		 * This case only happens when rx buffers are being freed
		 * in ixgbe_stop() and freemsg() is called.
		 */
		return;
	}

	atomic_inc_32(&recycle_ddp_buf->recycled_ubd_count);
	ubd_count = recycle_ddp_buf->used_ubd_count;
	if (recycle_ddp_buf->recycled_ubd_count < ubd_count) {
		return;
	}

	/*
	 * Using the recycled data buffer to generate a new mblk
	 */
	for (i = 0; i < ubd_count; i++) {
		ASSERT(recycle_ddp_buf->mp[i] == NULL);
		recycle_ddp_buf->mp[i] = desballoc((unsigned char *)
		    recycle_ddp_buf->rx_buf[i].address,
		    recycle_ddp_buf->rx_buf[i].size,
		    0, &recycle_ddp_buf->free_rtn);
	}
	recycle_ddp_buf->used_ubd_count = 0;
	recycle_ddp_buf->recycled_ubd_count = 0;

	/*
	 * Put the recycled ddp buf into free list
	 */
	mutex_enter(&rx_fcoe->recycle_lock);

	free_index = rx_fcoe->ddp_buf_tail;
	ASSERT(rx_fcoe->free_list[free_index] == NULL);

	rx_fcoe->free_list[free_index] = recycle_ddp_buf;
	rx_fcoe->ddp_buf_tail = NEXT_INDEX(free_index, 1,
	    rx_fcoe->free_list_size);

	mutex_exit(&rx_fcoe->recycle_lock);

	/*
	 * The atomic operation on the number of the available ddp
	 * in the free list is used to make the recycling mutual
	 * exclusive with the receiving.
	 */
	atomic_inc_32(&rx_fcoe->ddp_buf_free);
	ASSERT(rx_fcoe->ddp_buf_free <= rx_fcoe->free_list_size);

	/*
	 * Considering the case that the interface is unplumbed
	 * and there are still some buffers held by the upper layer.
	 * When the buffer is returned back, we need to free it.
	 */
	ref_cnt = atomic_dec_32_nv(&recycle_ddp_buf->ref_cnt);
	if (ref_cnt == 0) {
		for (i = 0; i < IXGBE_DDP_UBD_COUNT; i++) {
			if (recycle_ddp_buf->mp[i] != NULL) {
				freemsg(recycle_ddp_buf->mp[i]);
				recycle_ddp_buf->mp[i] = NULL;
			}
			ixgbe_free_dma_buffer(&recycle_ddp_buf->rx_buf[i]);
		}

		atomic_dec_32(&rx_fcoe->ddp_buf_pending);
		atomic_dec_32(&ixgbe->ddp_buf_pending);

		mutex_enter(&ixgbe->rx_pending_lock);
		/*
		 * When there is not any buffer belonging to the fcoe
		 * held by the upper layer, the fcoe data can be freed.
		 */
		if ((rx_fcoe->flag & IXGBE_RX_STOPPED) &&
		    (rx_fcoe->ddp_buf_pending == 0))
			ixgbe_free_rx_fcoe_data(rx_fcoe);
		mutex_exit(&ixgbe->rx_pending_lock);
	}
}

/*
 * Local definition.
 */
#define	IXGBE_RX_MBLKS	32

static boolean_t
ixgbe_rcb_sync(ixgbe_t *ixgbe, rx_control_block_t *rcb)
{
	ASSERT(ixgbe != NULL);
	ASSERT(ixgbe->mac_hdl != NULL);
	ASSERT(rcb != NULL);
	ASSERT(rcb->mp != NULL);
	ASSERT(rcb->rx_data != NULL);

	/*
	 * Sanity check for the interface being unplumbed.
	 */
	ASSERT(!mac_mblk_unbound(ixgbe->mac_hdl,
	    rcb->rx_data->rx_ring->ring_handle, rcb->mp));

	/*
	 * Sync the data received for the CPU.
	 */
	DMA_SYNC(&rcb->rx_buf, DDI_DMA_SYNC_FORKERNEL);

	if (ixgbe_check_dma_handle(rcb->rx_buf.dma_handle) != DDI_FM_OK) {
		ddi_fm_service_impact(ixgbe->dip, DDI_SERVICE_DEGRADED);
		atomic_or_32(&ixgbe->ixgbe_state, IXGBE_ERROR);
		return (B_FALSE);
	}

	return (B_TRUE);
}

static void
ixgbe_rcb_update(ixgbe_t *ixgbe, rx_control_block_t *rcb, mblk_t *mp)
{
	dma_buffer_t *rx_buf;

	ASSERT(ixgbe != NULL);
	ASSERT(ixgbe->mac_hdl != NULL);
	ASSERT(rcb != NULL);
	ASSERT(mp != NULL);
	ASSERT(mp->b_rptr != NULL);

	rx_buf = &rcb->rx_buf;
	rx_buf->address = (caddr_t)mp->b_rptr;
	mac_mblk_info_get(ixgbe->mac_hdl, mp, &rx_buf->dma_handle,
	    &rx_buf->dma_address, &rx_buf->size);
	if (ixgbe->lro_enable)
		rx_buf->size = ixgbe->rx_buf_size;
	rcb->mp = mp;
}

/*
 * ixgbe_rx_copy_fcoe_hdr - Use copy to process the received fcoe header.
 *
 * This function will use bcopy to process the packet
 * and send the copied packet upstream.
 */
static mblk_t *
ixgbe_rx_copy_fcoe_hdr(ixgbe_rx_data_t *rx_data, uint32_t index,
    uint32_t pkt_len)
{
	ixgbe_t *ixgbe;
	rx_control_block_t *current_rcb;
	mblk_t *mp;

	ixgbe = rx_data->rx_ring->ixgbe;
	current_rcb = rx_data->work_list[index];

	DMA_SYNC(&current_rcb->rx_buf, DDI_DMA_SYNC_FORKERNEL);

	if (ixgbe_check_dma_handle(current_rcb->rx_buf.dma_handle) !=
	    DDI_FM_OK) {
		ddi_fm_service_impact(ixgbe->dip, DDI_SERVICE_DEGRADED);
		atomic_or_32(&ixgbe->ixgbe_state, IXGBE_ERROR);
		return (NULL);
	}

	/*
	 * Allocate buffer to receive this packet
	 *
	 * For FCoE LRO, only the header will be received through legacy
	 * rx ring. Since hardware will NOT put the 8 bytes tail into
	 * header so here to fill it manually.
	 */
	mp = allocb(pkt_len + IXGBE_FC_TRAILER_LEN, 0);
	if (mp == NULL) {
		ixgbe->alloc_mp_fail++;
		return (NULL);
	}

	/*
	 * Copy the data received into the new cluster and refill the trailer
	 */
	bcopy(current_rcb->rx_buf.address, mp->b_rptr, pkt_len);
	*(uint8_t *)(mp->b_rptr + pkt_len + IXGBE_FC_TRAILER_EOF_OFFSET) =
	    IXGBE_FC_EOF_T;
	mp->b_wptr = mp->b_rptr + pkt_len + IXGBE_FC_TRAILER_LEN;

	return (mp);
}

/*
 * static mblk_t *
 * ixgbe_rx_loan() -- Prepare to loan out the current packet to the protocol
 *	stack. If a replacement is not available, try to replenish from
 *	the mac layer packet cache. If no replacement is still available,
 *	return failure, so that the caller falls back to copy.
 */
static mblk_t *
ixgbe_rx_loan(ixgbe_rx_data_t *rx_data, uint32_t index, uint32_t pkt_len)
{
	rx_control_block_t	*current_rcb;
	mblk_t			*nmp, *mp = NULL;
	ixgbe_t			*ixgbe = rx_data->rx_ring->ixgbe;
	mblk_t	*tail;
	int cnt = 32;

	ASSERT(rx_data != NULL);
	ASSERT(pkt_len != 0);

	/*
	 * Grab packets from the MAC layer for the packet
	 * cache.
	 */
	nmp = rx_data->mblk_head;
	if (nmp == NULL) {
		nmp = mac_mblk_get(ixgbe->mac_hdl,
		    rx_data->rx_ring->ring_handle, &tail, &cnt);
		if (cnt == 0)
			return (NULL);
		rx_data->mblk_head = nmp;
		rx_data->mblk_tail = tail;
		rx_data->mblk_cnt = cnt;
	}

	rx_data->mblk_head = nmp->b_next;
	rx_data->mblk_cnt--;
	if (rx_data->mblk_cnt == 0) {
		ASSERT(rx_data->mblk_head == NULL);
		rx_data->mblk_tail = NULL;
	}
	nmp->b_next = NULL;

	/*
	 * Get the current control block
	 * available.
	 */
	current_rcb = rx_data->work_list[index];

	/*
	 * Sync the packet for the CPU.
	 */
	if (ixgbe_rcb_sync(ixgbe, current_rcb)) {
		/*
		 * Prepare the existing packet to be loaned up
		 * to the software stack.
		 */
		mp = current_rcb->mp;
		mp->b_wptr = mp->b_rptr + pkt_len;
		ASSERT(mp->b_next == NULL);
		ASSERT(mp->b_cont == NULL);

		/*
		 * Transition the control block to the new
		 * packet.
		 */
		ixgbe_rcb_update(ixgbe, current_rcb, nmp);
	} else {
		freemsg(nmp);
		return (NULL);
	}

	return (mp);
}

/*
 * ixgbe_rx_copy - Use copy to process the received packet.
 *
 * This function will use bcopy to process the packet
 * and send the copied packet upstream.
 */
static mblk_t *
ixgbe_rx_copy(ixgbe_rx_data_t *rx_data, uint32_t index, uint32_t pkt_len)
{
	ixgbe_t *ixgbe;
	rx_control_block_t *current_rcb;
	mblk_t *mp;

	ASSERT(rx_data != NULL);
	ASSERT(pkt_len != 0);

	ixgbe = rx_data->rx_ring->ixgbe;
	current_rcb = rx_data->work_list[index];

	ASSERT(ixgbe != NULL);
	ASSERT(current_rcb != NULL);

	if (!ixgbe_rcb_sync(ixgbe, current_rcb))
		return (NULL);

	/*
	 * Allocate buffer to receive this packet
	 */
	if ((mp = allocb(pkt_len + IPHDR_ALIGN_ROOM, 0)) == NULL) {
		ixgbe->alloc_mp_fail++;
		return (NULL);
	}

	/*
	 * Copy the data received into the new cluster
	 */
	mp->b_rptr += IPHDR_ALIGN_ROOM;
	bcopy(current_rcb->rx_buf.address, mp->b_rptr, pkt_len);
	mp->b_wptr = mp->b_rptr + pkt_len;

	return (mp);
}

/*
 * ixgbe_lro_bind - Use existing DMA buffer to build LRO mblk for receiving.
 *
 * This function will use pre-bound DMA buffers to receive the packet
 * and build LRO mblk that will be sent upstream.
 */
static mblk_t *
ixgbe_lro_bind(ixgbe_rx_data_t *rx_data, uint32_t lro_start, uint32_t lro_num,
    uint32_t pkt_len)
{
	rx_control_block_t	*current_rcb;
	union ixgbe_adv_rx_desc	*current_rbd;
	int			cnt, lro_next;
	mblk_t			*mp = NULL, *mplist = NULL, *nmplist = NULL;
	mblk_t			*mblk_head = NULL, *tail = NULL;
	mblk_t			**mblk_tail;
	ixgbe_t			*ixgbe;

	ASSERT(rx_data != NULL);
	ASSERT(rx_data->rx_ring != NULL);
	ASSERT(rx_data->rx_ring->ixgbe != NULL);

	ixgbe = rx_data->rx_ring->ixgbe;

	/*
	 * If packets are not available to replace the packet buffers,
	 * then force bcopy of the LRO packets by returning NULL
	 */
	if (rx_data->mblk_cnt < lro_num) {
		cnt = lro_num;
		mplist =  mac_mblk_get(ixgbe->mac_hdl,
		    rx_data->rx_ring->ring_handle, &tail, &cnt);
		if (cnt != 0) {
			if (rx_data->mblk_head == NULL)
				rx_data->mblk_head = mplist;
			else
				rx_data->mblk_tail->b_next = mplist;
			rx_data->mblk_tail = tail;
			rx_data->mblk_cnt += cnt;
		}

		if (rx_data->mblk_cnt < lro_num)
			return (NULL);
	}

	/*
	 * If any one of the rx data blocks can not support
	 * lro bind  operation,  We'll have to return and use
	 * bcopy to process the lro  packet.
	 */
	lro_next = lro_start;
	ASSERT(lro_next != -1);

	while (lro_next != -1) {
		current_rcb = rx_data->work_list[lro_next];

		/*
		 * Sync up the data received
		 */
		if (!ixgbe_rcb_sync(ixgbe, current_rcb)) {
			/*
			 * Return failure since DMA sync failed.
			 */
			return (NULL);
		}

		/*
		 * Next packet.
		 */
		lro_next = current_rcb->lro_next;
	}

	mplist = rx_data->mblk_head;
	mblk_head = NULL;
	mblk_tail = &mblk_head;
	lro_next = lro_start;

	while (lro_next != -1) {
		current_rcb = rx_data->work_list[lro_next];
		current_rbd = &rx_data->rbd_ring[lro_next];

		/*
		 * Update the received mblk.
		 */
		mp = current_rcb->mp;
		if (pkt_len < ixgbe->rx_buf_size)
			mp->b_wptr = mp->b_rptr + pkt_len;
		else
			mp->b_wptr = mp->b_rptr + ixgbe->rx_buf_size;
		mp->b_next = mp->b_cont = NULL;

		/*
		 * Add it to the list.
		 */
		*mblk_tail = mp;
		mblk_tail = &mp->b_cont;

		/*
		 * Update control block.
		 */
		nmplist = mplist->b_next;
		mplist->b_next = NULL;
		ixgbe_rcb_update(ixgbe, current_rcb, mplist);
		mplist = nmplist;

		/*
		 * Update the descriptor and the control block.
		 */
		lro_next = current_rcb->lro_next;
		current_rcb->lro_next = -1;
		current_rcb->lro_prev = -1;
		current_rcb->lro_pkt = B_FALSE;
		current_rbd->read.pkt_addr = current_rcb->rx_buf.dma_address;
		current_rbd->read.hdr_addr = 0;

		pkt_len -= ixgbe->rx_buf_size;
	}

	rx_data->mblk_head = mplist;
	rx_data->mblk_cnt -= lro_num;
	if (rx_data->mblk_cnt == 0) {
		ASSERT(rx_data->mblk_head == NULL);
		rx_data->mblk_tail = NULL;
	} else {
		ASSERT(rx_data->mblk_head != NULL);
	}

	ASSERT(mblk_head != NULL);
	return (mblk_head);
}

/*
 * ixgbe_lro_copy - Use copy to process the received LRO packet.
 *
 * This function will use bcopy to process the LRO  packet
 * and send the copied packet upstream.
 */
static mblk_t *
ixgbe_lro_copy(ixgbe_rx_data_t *rx_data, uint32_t lro_start, uint32_t pkt_len)
{
	ixgbe_t			*ixgbe;
	rx_control_block_t	*current_rcb;
	union ixgbe_adv_rx_desc	*current_rbd;
	mblk_t			*mp;
	int			lro_next;

	ASSERT(rx_data != NULL);
	ASSERT(rx_data->rx_ring != NULL);
	ASSERT(rx_data->rx_ring->ixgbe != NULL);

	ixgbe = rx_data->rx_ring->ixgbe;

	/*
	 * Allocate buffer to receive this LRO packet
	 */
	if ((mp = allocb(pkt_len + IPHDR_ALIGN_ROOM, 0)) == NULL) {
		ixgbe->alloc_mp_fail++;
		return (NULL);
	}


	/*
	 * Sync up the LRO packet data received
	 */
	lro_next = lro_start;
	ASSERT(lro_next != -1);

	while (lro_next != -1) {
		current_rcb = rx_data->work_list[lro_next];

		if (!ixgbe_rcb_sync(ixgbe, current_rcb)) {
			freemsg(mp);
			return (NULL);
		}

		lro_next = current_rcb->lro_next;
	}

	/*
	 * Copy the data received into the new cluster
	 */
	lro_next = lro_start;
	mp->b_rptr += IPHDR_ALIGN_ROOM;
	mp->b_wptr += IPHDR_ALIGN_ROOM;

	while (lro_next != -1) {
		current_rcb = rx_data->work_list[lro_next];
		current_rbd = &rx_data->rbd_ring[lro_next];

		if (pkt_len > ixgbe->rx_buf_size) {
			bcopy(current_rcb->rx_buf.address, mp->b_wptr,
			    current_rcb->rx_buf.size);
			mp->b_wptr += current_rcb->rx_buf.size;
		} else {
			bcopy(current_rcb->rx_buf.address, mp->b_wptr,
			    pkt_len);
			mp->b_wptr += pkt_len;
		}

		/*
		 * Update the control block.
		 */
		lro_next = current_rcb->lro_next;
		current_rcb->lro_next = -1;
		current_rcb->lro_prev = -1;
		current_rcb->lro_pkt = B_FALSE;
		current_rbd->read.pkt_addr = current_rcb->rx_buf.dma_address;
		current_rbd->read.hdr_addr = 0;

		pkt_len -= ixgbe->rx_buf_size;
	}

	return (mp);
}

/*
 * ixgbe_lro_get_start - get the start rcb index in one LRO packet
 */
static int
ixgbe_lro_get_start(ixgbe_rx_data_t *rx_data, uint32_t rx_next)
{
	int lro_prev;
	int lro_start;
	uint32_t lro_num = 1;
	rx_control_block_t *prev_rcb;
	rx_control_block_t *current_rcb = rx_data->work_list[rx_next];
	lro_prev = current_rcb->lro_prev;

	while (lro_prev != -1) {
		lro_num ++;
		prev_rcb = rx_data->work_list[lro_prev];
		lro_start = lro_prev;
		lro_prev = prev_rcb->lro_prev;
	}
	rx_data->lro_num = lro_num;
	return (lro_start);
}

/*
 * ixgbe_lro_get_first - get the first LRO rcb index
 */
static uint32_t
ixgbe_lro_get_first(ixgbe_rx_data_t *rx_data, uint32_t rx_next)
{
	rx_control_block_t *current_rcb;
	uint32_t lro_first;
	lro_first = rx_data->lro_first;
	current_rcb = rx_data->work_list[lro_first];
	while ((!current_rcb->lro_pkt) && (lro_first != rx_next)) {
		lro_first =  NEXT_INDEX(lro_first, 1, rx_data->ring_size);
		current_rcb = rx_data->work_list[lro_first];
	}
	rx_data->lro_first = lro_first;
	return (lro_first);
}

/*
 * ixgbe_rx_assoc_hcksum - Check the rx hardware checksum status and associate
 * the hcksum flags.
 */
static void
ixgbe_rx_assoc_hcksum(mblk_t *mp, uint32_t status_error)
{
	uint32_t hcksum_flags = 0;

	/*
	 * Check TCP/UDP checksum
	 */
	if ((status_error & IXGBE_RXD_STAT_L4CS) &&
	    !(status_error & IXGBE_RXDADV_ERR_TCPE))
		hcksum_flags |= HCK_FULLCKSUM_OK;

	/*
	 * Check IP Checksum
	 */
	if ((status_error & IXGBE_RXD_STAT_IPCS) &&
	    !(status_error & IXGBE_RXDADV_ERR_IPE))
		hcksum_flags |= HCK_IPV4_HDRCKSUM_OK;

	if (hcksum_flags != 0) {
		mac_hcksum_set(mp, 0, 0, 0, 0, hcksum_flags);
	}
}

/*
 * To improve the latency, limit the number of packets returned in a single
 * chain. Seems to make more of a difference on sparc.
 */
#if defined(__sparc)
uint_t ixgbe_max_poll_pkts = 64;
#else
uint_t ixgbe_max_poll_pkts = UINT_MAX;
#endif

/*
 * ixgbe_rx_is_fcoe - Check whether the received packet is FCoE frame.
 */
static boolean_t
ixgbe_rx_is_fcoe(union ixgbe_adv_rx_desc *rbd)
{
	uint16_t pkt_info;

	pkt_info = rbd->wb.lower.lo_dword.hs_rss.pkt_info;
	if (pkt_info & IXGBE_RXDADV_PKTTYPE_ETQF) {
		pkt_info &= IXGBE_RXDADV_PKTTYPE_ETQF_MASK;
		pkt_info >>= IXGBE_RXDADV_PKTTYPE_ETQF_SHIFT;
		return (pkt_info == IXGBE_ETQF_FILTER_FCOE);
	}

	return (B_FALSE);
}

/*
 * ixgbe_rx_fcoe_lro - Handle the FCoE LRO/DDP packet.
 */
static mblk_t *
ixgbe_rx_fcoe_lro(ixgbe_rx_data_t *rx_data, uint32_t index,
    uint32_t status_error, uint32_t hdr_len)
{
	ixgbe_t *ixgbe;
	ixgbe_rx_ring_t *rx_ring;
	union ixgbe_adv_rx_desc *current_rbd;
	mblk_t *mp = NULL;
	mblk_t *mblk_tail = NULL;
	mblk_t *ddp_mp = NULL;
	unsigned char *pos;
	ushort_t etype;
	uint32_t eth_hdr_len;
	uint16_t xchg_id;
	ixgbe_rx_fcoe_t *rx_fcoe;
	ixgbe_fcoe_ddp_t *ddp;
	ixgbe_fcoe_ddp_buf_t *ddp_buf;
	uint32_t ddp_len;
	int32_t i;


	rx_ring = rx_data->rx_ring;
	ixgbe = rx_ring->ixgbe;
	rx_fcoe = ixgbe->rx_fcoe;

	current_rbd = &rx_data->rbd_ring[index];

	/*
	 * Copy out the header (ether_hdr + fcoe_hdr + fc_hdr).
	 */
	hdr_len = (uint32_t)((current_rbd->wb.lower.lo_dword.hs_rss.hdr_info)
	    >> IXGBE_RXDADV_HDRBUFLEN_SHIFT);
	mp = ixgbe_rx_copy_fcoe_hdr(rx_data, index, hdr_len);
	if (mp == NULL) {
		return (NULL);
	}

	/*
	 * Check out the xchg_id from header
	 */
	pos = mp->b_rptr + offsetof(struct ether_header, ether_type);
	etype = ntohs(*(uint16_t *)(uintptr_t)pos);
	if (etype == ETHERTYPE_VLAN) {
		eth_hdr_len = sizeof (struct ether_vlan_header);
	} else {
		eth_hdr_len = sizeof (struct ether_header);
	}
	if (rx_fcoe->on_target) {
		/* rx_id will be used */
		pos = mp->b_rptr + eth_hdr_len + IXGBE_FCOE_HDR_LEN +
		    IXGBE_FC_RXID_OFFSET;
	} else {
		/* ox_id will be used */
		pos = mp->b_rptr + eth_hdr_len + IXGBE_FCOE_HDR_LEN +
		    IXGBE_FC_OXID_OFFSET;
	}
	xchg_id = ntohs(*(uint16_t *)(uintptr_t)pos);

	mutex_enter(&ixgbe->rx_fcoe_lock);

	ddp = &rx_fcoe->ddp_ring[xchg_id % IXGBE_DDP_RING_SIZE];
	ddp_buf = ddp->ddp_buf;

	if ((ddp->xchg_id != xchg_id) || (ddp_buf == NULL)) {
		mutex_exit(&ixgbe->rx_fcoe_lock);
		freemsg(mp);
		return (NULL);
	}

	/*
	 * If the mp of the ddp buf is NULL, try to do
	 * desballoc again.
	 */
	for (i = 0; i < ddp_buf->used_ubd_count; i++) {
		if (ddp_buf->mp[i] == NULL) {
			ddp_buf->mp[i] = desballoc((unsigned char *)
			    ddp_buf->rx_buf[i].address,
			    ddp_buf->rx_buf[i].size,
			    0, &ddp_buf->free_rtn);

			/*
			 * If it is failed to built a mblk using the current
			 * DMA buffer, we have to return and use bcopy to
			 * process the packet.
			 */
			if (ddp_buf->mp[i] == NULL) {
				mutex_exit(&ixgbe->rx_fcoe_lock);
				freemsg(mp);
				return (NULL);
			}
		}
	}

	switch (status_error & IXGBE_RXDADV_STAT_FCSTAT) {
	case IXGBE_RXDADV_STAT_FCSTAT_DDP:
	case IXGBE_RXDADV_STAT_FCSTAT_FCPRSP:
	case IXGBE_RXDADV_STAT_FCSTAT_NODDP:
		ddp_len = current_rbd->wb.lower.hi_dword.rss;
		/*
		 * Sync up the data received
		 */
		if (ddp_len != ddp_buf->used_buf_size) {
			mutex_exit(&ixgbe->rx_fcoe_lock);
			freemsg(mp);
			return (NULL);
		}

		mblk_tail = mp;
		for (i = 0; i < ddp_buf->used_ubd_count; i++) {
			DMA_SYNC(&ddp_buf->rx_buf[i], DDI_DMA_SYNC_FORKERNEL);
			ddp_mp = ddp_buf->mp[i];
			ddp_buf->mp[i] = NULL;
			if (i < (ddp_buf->used_ubd_count -1)) {
				ddp_mp->b_wptr = ddp_mp->b_rptr +
				    IXGBE_DDP_BUF_SIZE;
			} else {
				ddp_mp->b_wptr = ddp_mp->b_rptr +
				    ddp_buf->last_ub_len;
			}
			ddp_mp->b_next = ddp_mp->b_cont = NULL;
			mblk_tail->b_cont = ddp_mp;
			mblk_tail = ddp_mp;
		}

		atomic_inc_32(&ddp_buf->ref_cnt);
		ddp->ddp_buf = NULL;
		ddp->xchg_id = 0;

		break;
	case IXGBE_RXDADV_STAT_FCSTAT_NOMTCH:
	default:
		freemsg(mp);
		mp = NULL;
		break;
	}

	mutex_exit(&ixgbe->rx_fcoe_lock);
	return (mp);
}

static void
ixgbe_rx_assoc_fcoe_rxcrc(mblk_t *mp, uint32_t status_error)
{
	if ((status_error & IXGBE_RXDADV_ERR_FCERR) != IXGBE_FCERR_BADCRC) {
		mac_hcksum_set(mp, 0, 0, 0, 0, HCK_FCOE_FC_CRC_OK);
	}
}

/*
 * ixgbe_ring_rx - Receive the data of one ring.
 *
 * This function goes throught h/w descriptor in one specified rx ring,
 * receives the data if the descriptor status shows the data is ready.
 * It returns a chain of mblks containing the received data, to be
 * passed up to mac.
 */
mblk_t *
ixgbe_ring_rx(ixgbe_rx_ring_t *rx_ring, int poll_bytes, int poll_pkts)
{
	union ixgbe_adv_rx_desc *current_rbd;
	rx_control_block_t *current_rcb;
	mblk_t *mp, *headp = NULL, *tailp = NULL;
	uint32_t rx_next;
	uint32_t rx_tail;
	uint32_t pkt_len;
	uint32_t hdr_len;
	uint32_t status_error;
	uint32_t pkt_num;
	uint32_t rsc_cnt;
	uint32_t lro_first;
	uint32_t lro_start;
	uint32_t lro_next;
	boolean_t lro_eop;
	uint32_t received_bytes;
	ixgbe_t *ixgbe = rx_ring->ixgbe;
	ixgbe_rx_data_t *rx_data;
	boolean_t is_fcoe;
	uint32_t ddp_error;

	if (!rx_ring->started ||
	    (ixgbe->ixgbe_state & IXGBE_SUSPENDED) ||
	    (ixgbe->ixgbe_state & IXGBE_ERROR) ||
	    (ixgbe->ixgbe_state & IXGBE_OVERTEMP) ||
	    !(ixgbe->ixgbe_state & IXGBE_STARTED))
		return (NULL);

	rx_data = rx_ring->rx_data;
	lro_eop = B_FALSE;

	/*
	 * Sync the receive descriptors before accepting the packets
	 */
	DMA_SYNC(&rx_data->rbd_area, DDI_DMA_SYNC_FORKERNEL);

	if (ixgbe_check_dma_handle(rx_data->rbd_area.dma_handle) != DDI_FM_OK) {
		ddi_fm_service_impact(ixgbe->dip, DDI_SERVICE_DEGRADED);
		atomic_or_32(&ixgbe->ixgbe_state, IXGBE_ERROR);
		return (NULL);
	}

	/*
	 * Get the start point of rx bd ring which should be examined
	 * during this cycle.
	 */
	rx_next = rx_data->rbd_next;
	current_rbd = &rx_data->rbd_ring[rx_next];
	received_bytes = 0;
	poll_pkts = min(poll_pkts, ixgbe_max_poll_pkts);
	pkt_num = 0;
	status_error = current_rbd->wb.upper.status_error;

	while (status_error & IXGBE_RXD_STAT_DD) {
		/*
		 * If adapter has found errors, but the error
		 * is hardware checksum error, this does not discard the
		 * packet: let upper layer compute the checksum;
		 * Otherwise discard the packet.
		 */
		if ((status_error & IXGBE_RXDADV_ERR_FRAME_ERR_MASK) ||
		    ((!ixgbe->lro_enable) &&
		    (!(status_error & IXGBE_RXD_STAT_EOP)))) {
			IXGBE_DEBUG_STAT(rx_ring->stat_frame_error);
			goto rx_discard;
		}

		IXGBE_DEBUG_STAT_COND(rx_ring->stat_cksum_error,
		    (status_error & IXGBE_RXDADV_ERR_TCPE) ||
		    (status_error & IXGBE_RXDADV_ERR_IPE));

		is_fcoe = ixgbe_rx_is_fcoe(current_rbd);
		if (is_fcoe && ixgbe->fcoe_lro_enable) {
			ddp_error = status_error &
			    (IXGBE_RXDADV_ERR_FCEOFE | IXGBE_RXDADV_ERR_FCERR);
			pkt_len = current_rbd->wb.upper.length;
			hdr_len = (uint32_t)(
			    (current_rbd->wb.lower.lo_dword.hs_rss.hdr_info)
			    >> IXGBE_RXDADV_HDRBUFLEN_SHIFT);
			if (ddp_error || (pkt_len != hdr_len)) {
				goto rx_continue;
			}

			mp = ixgbe_rx_fcoe_lro(rx_data, rx_next,
			    status_error, hdr_len);

			if (mp != NULL) {
				goto rx_success;
			} else {
				goto rx_discard;
			}
		}
rx_continue:
		if (ixgbe->lro_enable) {
			rsc_cnt =  (current_rbd->wb.lower.lo_dword.data &
			    IXGBE_RXDADV_RSCCNT_MASK) >>
			    IXGBE_RXDADV_RSCCNT_SHIFT;
			if (rsc_cnt != 0) {
				if (status_error & IXGBE_RXD_STAT_EOP) {
					pkt_len = current_rbd->wb.upper.length;
					if (rx_data->work_list[rx_next]->
					    lro_prev != -1) {
						lro_start =
						    ixgbe_lro_get_start(rx_data,
						    rx_next);
						ixgbe->lro_pkt_count++;
						pkt_len +=
						    (rx_data->lro_num  - 1) *
						    ixgbe->rx_buf_size;
						lro_eop = B_TRUE;
					}
				} else {
					lro_next = (status_error &
					    IXGBE_RXDADV_NEXTP_MASK) >>
					    IXGBE_RXDADV_NEXTP_SHIFT;
					rx_data->work_list[lro_next]->lro_prev
					    = rx_next;
					rx_data->work_list[rx_next]->lro_next =
					    lro_next;
					rx_data->work_list[rx_next]->lro_pkt =
					    B_TRUE;
					goto rx_discard;
				}

			} else {
				pkt_len = current_rbd->wb.upper.length;
			}
		} else {
			pkt_len = current_rbd->wb.upper.length;
		}

		if (((received_bytes + pkt_len) > poll_bytes ||
		    (pkt_num > poll_pkts)))
			break;

		received_bytes += pkt_len;
		mp = NULL;

		/*
		 * For packets with length more than the copy threshold,
		 * we'll first try to use the existing DMA buffer to build
		 * an mblk and send the mblk upstream.
		 *
		 * If the first method fails, or the packet length is less
		 * than the copy threshold, we'll allocate a new mblk and
		 * copy the packet data to the new mblk.
		 */
		if (lro_eop) {
			mp = ixgbe_lro_bind(rx_data, lro_start,
			    rx_data->lro_num, pkt_len);
			if (mp == NULL) {
				mp = ixgbe_lro_copy(rx_data, lro_start,
				    pkt_len);
			}
			lro_eop = B_FALSE;
			rx_data->lro_num = 0;
		} else {
			if (pkt_len > ixgbe->rx_copy_thresh)
				mp = ixgbe_rx_loan(rx_data, rx_next, pkt_len);
			if (mp == NULL)
				mp = ixgbe_rx_copy(rx_data, rx_next, pkt_len);
		}
rx_success:
		if (mp != NULL) {
			int16_t rss_type;
			uint16_t rss;

			/*
			 * Check h/w checksum offload status
			 */
			if (ixgbe->rx_hcksum_enable)
				ixgbe_rx_assoc_hcksum(mp, status_error);

			/*
			 * Check h/w FCoE CRC offload status
			 */
			if (is_fcoe && ixgbe->fcoe_rxcrc_enable) {
				ixgbe_rx_assoc_fcoe_rxcrc(mp, status_error);
			}

			rss_type = (int16_t)(current_rbd->
			    wb.lower.lo_dword.hs_rss.pkt_info) &
			    IXGBE_RXDADV_RSSTYPE_MASK;

			if ((rss_type >= IXGBE_RXDADV_RSSTYPE_IPV4_TCP) &&
			    (rss_type <= IXGBE_RXDADV_RSSTYPE_IPV6_UDP_EX)) {
				rss = (uint16_t)
				    current_rbd->wb.lower.hi_dword.rss;
				DTRACE_PROBE1(ixgbe__rss, uint16_t, rss);
				if (rss != 0) {
					DB_CKSUMFLAGS(mp) |= HW_HASH;
					DB_HASH(mp) = rss;
				}
			}

			if (headp == NULL)
				headp = mp;
			else
				tailp->b_next = mp;
			tailp = mp;
		}

rx_discard:
		/*
		 * Reset rx descriptor read bits
		 */
		current_rcb = rx_data->work_list[rx_next];
		if (!current_rcb->lro_pkt) {
			current_rbd->read.pkt_addr =
			    current_rcb->rx_buf.dma_address;
			current_rbd->read.hdr_addr = 0;
		}

		rx_next = NEXT_INDEX(rx_next, 1, rx_data->ring_size);

		/*
		 * The receive function is in interrupt context, so here
		 * rx_limit_per_intr is used to avoid doing receiving too long
		 * per interrupt.
		 */
		if (++pkt_num > ixgbe->rx_limit_per_intr) {
			IXGBE_DEBUG_STAT(rx_ring->stat_exceed_pkt);
			break;
		}

		current_rbd = &rx_data->rbd_ring[rx_next];
		status_error = current_rbd->wb.upper.status_error;
	}

	/* the stat for fcoe lro is bypassed */

	rx_ring->stat_rbytes += received_bytes;
	rx_ring->stat_ipackets += pkt_num;

	DMA_SYNC(&rx_data->rbd_area, DDI_DMA_SYNC_FORDEV);

	rx_data->rbd_next = rx_next;

	/*
	 * Update the h/w tail accordingly
	 */
	if (ixgbe->lro_enable) {
		lro_first = ixgbe_lro_get_first(rx_data, rx_next);
		rx_tail = PREV_INDEX(lro_first, 1, rx_data->ring_size);
	} else
		rx_tail = PREV_INDEX(rx_next, 1, rx_data->ring_size);

	IXGBE_WRITE_REG(&ixgbe->hw, IXGBE_RDT(rx_ring->hw_index), rx_tail);

	if (ixgbe_check_acc_handle(ixgbe->osdep.reg_handle) != DDI_FM_OK) {
		ddi_fm_service_impact(ixgbe->dip, DDI_SERVICE_DEGRADED);
		atomic_or_32(&ixgbe->ixgbe_state, IXGBE_ERROR);
	}

	return (headp);
}

mblk_t *
ixgbe_ring_rx_poll(void *arg, int n_bytes, int n_pkts)
{
	ixgbe_rx_ring_t *rx_ring = (ixgbe_rx_ring_t *)arg;
	mblk_t *mp = NULL;

	ASSERT(n_bytes >= 0);

	if (n_bytes == 0)
		return (NULL);

	mutex_enter(&rx_ring->rx_lock);
	mp = ixgbe_ring_rx(rx_ring, n_bytes, n_pkts);
	mutex_exit(&rx_ring->rx_lock);

	return (mp);
}
