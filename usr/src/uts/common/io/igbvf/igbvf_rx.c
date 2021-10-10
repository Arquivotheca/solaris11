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
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include "igbvf_sw.h"

/* function prototypes */
static mblk_t *igbvf_rx_bind(igbvf_rx_data_t *, uint32_t, uint32_t);
static mblk_t *igbvf_rx_copy(igbvf_rx_data_t *, uint32_t, uint32_t);
static void igbvf_rx_assoc_hcksum(mblk_t *, uint32_t);
static void igbvf_strip_vlan(mblk_t *);

#ifndef IGBVF_DEBUG
#pragma inline(igbvf_rx_assoc_hcksum)
#pragma inline(igbvf_strip_vlan)
#endif


/*
 * igbvf_rx_recycle - the call-back function to reclaim rx buffer
 *
 * This function is called when an mp is freed by the user thru
 * freeb call (Only for mp constructed through desballoc call).
 * It returns back the freed buffer to the free list.
 */
void
igbvf_rx_recycle(caddr_t arg)
{
	igbvf_t *igbvf;
	igbvf_rx_ring_t *rx_ring;
	igbvf_rx_data_t	*rx_data;
	rx_control_block_t *recycle_rcb;
	uint32_t free_index;
	uint32_t ref_cnt;

	recycle_rcb = (rx_control_block_t *)(uintptr_t)arg;
	rx_data = recycle_rcb->rx_data;
	rx_ring = rx_data->rx_ring;
	igbvf = rx_ring->igbvf;

	if (recycle_rcb->ref_cnt == 0) {
		/*
		 * This case only happens when rx buffers are being freed
		 * in igbvf_stop() and freemsg() is called.
		 */
		return;
	}

	ASSERT(recycle_rcb->mp == NULL);

	/*
	 * Using the recycled data buffer to generate a new mblk
	 */
	recycle_rcb->mp = desballoc((unsigned char *)
	    recycle_rcb->rx_buf.address,
	    recycle_rcb->rx_buf.size,
	    0, &recycle_rcb->free_rtn);

	/*
	 * Put the recycled rx control block into free list
	 */
	mutex_enter(&rx_data->recycle_lock);

	free_index = rx_data->rcb_tail;
	ASSERT(rx_data->free_list[free_index] == NULL);

	rx_data->free_list[free_index] = recycle_rcb;
	rx_data->rcb_tail = NEXT_INDEX(free_index, 1, rx_data->free_list_size);

	mutex_exit(&rx_data->recycle_lock);

	/*
	 * The atomic operation on the number of the available rx control
	 * blocks in the free list is used to make the recycling mutual
	 * exclusive with the receiving.
	 */
	atomic_inc_32(&rx_data->rcb_free);
	ASSERT(rx_data->rcb_free <= rx_data->free_list_size);

	/*
	 * Considering the case that the interface is unplumbed
	 * and there are still some buffers held by the upper layer.
	 * When the buffer is returned back, we need to free it.
	 */
	ref_cnt = atomic_dec_32_nv(&recycle_rcb->ref_cnt);
	if (ref_cnt == 0) {
		if (recycle_rcb->mp != NULL) {
			freemsg(recycle_rcb->mp);
			recycle_rcb->mp = NULL;
		}

		if (IGBVF_IS_SUSPENDED(igbvf) &&
		    IGBVF_IS_SUSPENDED_DMA_FREE(igbvf)) {
			/* Decrease ref_cnt to -1 */
			atomic_dec_32(&recycle_rcb->ref_cnt);
		} else {
			igbvf_free_dma_buffer(&recycle_rcb->rx_buf);
		}

		mutex_enter(&igbvf->rx_pending_lock);
		atomic_dec_32(&rx_data->rcb_pending);
		atomic_dec_32(&igbvf->rcb_pending);

		/*
		 * When there is not any buffer belonging to this rx_data
		 * held by the upper layer, the rx_data can be freed.
		 */
		if ((rx_data->flag & IGBVF_RX_STOPPED) &&
		    (rx_data->rcb_pending == 0)) {
			if (IGBVF_IS_SUSPENDED(igbvf) &&
			    IGBVF_IS_SUSPENDED_DMA_FREE(igbvf)) {
				/* Add the rx data to the pending list */
				rx_data->next = igbvf->pending_rx_data;
				igbvf->pending_rx_data = rx_data;
			} else {
				igbvf_free_rx_ring_data(rx_data);
			}
		}

		mutex_exit(&igbvf->rx_pending_lock);
	}
}

/*
 * igbvf_rx_copy - Use copy to process the received packet
 *
 * This function will use bcopy to process the packet
 * and send the copied packet upstream
 */
static mblk_t *
igbvf_rx_copy(igbvf_rx_data_t *rx_data, uint32_t index, uint32_t pkt_len)
{
	rx_control_block_t *current_rcb;
	mblk_t *mp;
	igbvf_t *igbvf = rx_data->rx_ring->igbvf;

	current_rcb = rx_data->work_list[index];

	DMA_SYNC(&current_rcb->rx_buf, DDI_DMA_SYNC_FORKERNEL);

	if (igbvf_check_dma_handle(
	    current_rcb->rx_buf.dma_handle) != DDI_FM_OK) {
		ddi_fm_service_impact(igbvf->dip, DDI_SERVICE_DEGRADED);
		atomic_or_32(&igbvf->igbvf_state, IGBVF_ERROR);
		return (NULL);
	}

	/*
	 * Allocate buffer to receive this packet
	 */
	mp = allocb(pkt_len + IPHDR_ALIGN_ROOM, 0);
	if (mp == NULL) {
		igbvf_log(igbvf, "igbvf_rx_copy: allocate buffer failed");
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
 * igbvf_rx_bind - Use existing DMA buffer to build mblk for receiving
 *
 * This function will use pre-bound DMA buffer to receive the packet
 * and build mblk that will be sent upstream.
 */
static mblk_t *
igbvf_rx_bind(igbvf_rx_data_t *rx_data, uint32_t index, uint32_t pkt_len)
{
	rx_control_block_t *current_rcb;
	rx_control_block_t *free_rcb;
	uint32_t free_index;
	mblk_t *mp;
	igbvf_t *igbvf = rx_data->rx_ring->igbvf;

	/*
	 * If the free list is empty, we cannot proceed to send
	 * the current DMA buffer upstream. We'll have to return
	 * and use bcopy to process the packet.
	 */
	if (igbvf_atomic_reserve(&rx_data->rcb_free, 1) < 0)
		return (NULL);

	current_rcb = rx_data->work_list[index];
	/*
	 * If the mp of the rx control block is NULL, try to do
	 * desballoc again.
	 */
	if (current_rcb->mp == NULL) {
		current_rcb->mp = desballoc((unsigned char *)
		    current_rcb->rx_buf.address,
		    current_rcb->rx_buf.size,
		    0, &current_rcb->free_rtn);
		/*
		 * If it is failed to built a mblk using the current
		 * DMA buffer, we have to return and use bcopy to
		 * process the packet.
		 */
		if (current_rcb->mp == NULL) {
			atomic_inc_32(&rx_data->rcb_free);
			return (NULL);
		}
	}
	/*
	 * Sync up the data received
	 */
	DMA_SYNC(&current_rcb->rx_buf, DDI_DMA_SYNC_FORKERNEL);

	if (igbvf_check_dma_handle(
	    current_rcb->rx_buf.dma_handle) != DDI_FM_OK) {
		ddi_fm_service_impact(igbvf->dip, DDI_SERVICE_DEGRADED);
		atomic_or_32(&igbvf->igbvf_state, IGBVF_ERROR);
		atomic_inc_32(&rx_data->rcb_free);
		return (NULL);
	}

	mp = current_rcb->mp;
	current_rcb->mp = NULL;
	atomic_inc_32(&current_rcb->ref_cnt);

	mp->b_wptr = mp->b_rptr + pkt_len;
	mp->b_next = mp->b_cont = NULL;

	/*
	 * Strip off one free rx control block from the free list
	 */
	free_index = rx_data->rcb_head;
	free_rcb = rx_data->free_list[free_index];
	ASSERT(free_rcb != NULL);
	rx_data->free_list[free_index] = NULL;
	rx_data->rcb_head = NEXT_INDEX(free_index, 1, rx_data->free_list_size);

	/*
	 * Put the rx control block to the work list
	 */
	rx_data->work_list[index] = free_rcb;

	return (mp);
}

/*
 * igbvf_rx_assoc_hcksum
 *
 * Check the rx hardware checksum status and associate the hcksum flags
 */
static void
igbvf_rx_assoc_hcksum(mblk_t *mp, uint32_t status_error)
{
	uint32_t hcksum_flags = 0;

	/* Ignore Checksum Indication */
	if (status_error & E1000_RXD_STAT_IXSM)
		return;

	/*
	 * Check TCP/UDP checksum
	 */
	if (((status_error & E1000_RXD_STAT_TCPCS) ||
	    (status_error & E1000_RXD_STAT_UDPCS)) &&
	    !(status_error & E1000_RXDEXT_STATERR_TCPE))
		hcksum_flags |= HCK_FULLCKSUM_OK;

	/*
	 * Check IP Checksum
	 */
	if ((status_error & E1000_RXD_STAT_IPCS) &&
	    !(status_error & E1000_RXDEXT_STATERR_IPE))
		hcksum_flags |= HCK_IPV4_HDRCKSUM_OK;

	if (hcksum_flags != 0) {
		mac_hcksum_set(mp, 0, 0, 0, 0, hcksum_flags);
	}
}

mblk_t *
igbvf_rx_ring_poll(void *arg, int bytes, int pkts)
{
	igbvf_rx_ring_t *rx_ring = (igbvf_rx_ring_t *)arg;
	igbvf_t *igbvf = rx_ring->igbvf;
	mblk_t *mp;

	ASSERT(bytes >= 0);

	if (bytes == 0)
		return (NULL);

	mutex_enter(&rx_ring->rx_lock);

	if ((igbvf->igbvf_state & (IGBVF_STARTED | IGBVF_ERROR |
	    IGBVF_SUSPENDED_TX_RX)) != IGBVF_STARTED) {
		mutex_exit(&rx_ring->rx_lock);
		return (NULL);
	}

	mp = igbvf_rx(rx_ring, bytes, pkts);

	mutex_exit(&rx_ring->rx_lock);

	return (mp);
}

/*
 * igbvf_rx - Receive the data of one ring
 *
 * This function goes throught h/w descriptor in one specified rx ring,
 * receives the data if the descriptor status shows the data is ready.
 * It returns a chain of mblks containing the received data, to be
 * passed up to mac_rx().
 */
mblk_t *
igbvf_rx(igbvf_rx_ring_t *rx_ring, int poll_bytes, int poll_pkts)
{
	union e1000_adv_rx_desc *current_rbd;
	rx_control_block_t *current_rcb;
	mblk_t *mp;
	mblk_t *mblk_head;
	mblk_t **mblk_tail;
	uint32_t rx_next;
	uint32_t rx_tail;
	uint32_t pkt_len;
	uint32_t status_error;
	uint32_t pkt_num;
	uint32_t total_bytes;
	igbvf_t *igbvf = rx_ring->igbvf;
	igbvf_rx_data_t *rx_data = rx_ring->rx_data;

	mblk_head = NULL;
	mblk_tail = &mblk_head;

	/*
	 * Sync the receive descriptors before
	 * accepting the packets
	 */
	DMA_SYNC(&rx_data->rbd_area, DDI_DMA_SYNC_FORKERNEL);

	if (igbvf_check_dma_handle(
	    rx_data->rbd_area.dma_handle) != DDI_FM_OK) {
		ddi_fm_service_impact(igbvf->dip, DDI_SERVICE_DEGRADED);
		atomic_or_32(&igbvf->igbvf_state, IGBVF_ERROR);
		return (NULL);
	}

	/*
	 * Get the start point of rx bd ring which should be examined
	 * during this cycle.
	 */
	rx_next = rx_data->rbd_next;

	current_rbd = &rx_data->rbd_ring[rx_next];
	pkt_num = 0;
	total_bytes = 0;
	status_error = current_rbd->wb.upper.status_error;
	while (status_error & E1000_RXD_STAT_DD) {
		/*
		 * If hardware has found the errors, but the error
		 * is hardware checksum error, here does not discard the
		 * packet, and let upper layer compute the checksum;
		 * Otherwise discard the packet.
		 */
		if ((status_error & E1000_RXDEXT_ERR_FRAME_ERR_MASK) ||
		    !(status_error & E1000_RXD_STAT_EOP)) {
			IGBVF_DEBUG_STAT(rx_ring->stat_frame_error);
			goto rx_discard;
		}

		IGBVF_DEBUG_STAT_COND(rx_ring->stat_cksum_error,
		    (status_error & E1000_RXDEXT_STATERR_TCPE) ||
		    (status_error & E1000_RXDEXT_STATERR_IPE));

		pkt_len = current_rbd->wb.upper.length;

		if ((pkt_len + total_bytes) > poll_bytes ||
		    pkt_num >= poll_pkts)
			break;

		IGBVF_DEBUG_STAT(rx_ring->stat_pkt_cnt);
		total_bytes += pkt_len;

		mp = NULL;
		/*
		 * For packets with length more than the copy threshold,
		 * we'll firstly try to use the existed DMA buffer to built
		 * a mblk and send the mblk upstream.
		 *
		 * If the first method fails, or the packet length is less
		 * than the copy threshold, we'll allocate a new mblk and
		 * copy the packet data to the mblk.
		 */
		if (pkt_len > igbvf->rx_copy_thresh)
			mp = igbvf_rx_bind(rx_data, rx_next, pkt_len);

		if (mp == NULL)
			mp = igbvf_rx_copy(rx_data, rx_next, pkt_len);

		if (mp != NULL) {
			/* Check VLAN */
			if (igbvf->transparent_vlan_enable &&
			    IGBVF_VLAN_PACKET(mp->b_rptr)) {
				igbvf_strip_vlan(mp);
			}

			/*
			 * Check h/w checksum offload status
			 */
			if (igbvf->rx_hcksum_enable)
				igbvf_rx_assoc_hcksum(mp, status_error);

			*mblk_tail = mp;
			mblk_tail = &mp->b_next;
		}

		/* Update per-ring rx statistics */
		rx_ring->rx_pkts++;
		rx_ring->rx_bytes += pkt_len;

rx_discard:
		/*
		 * Reset rx descriptor read bits
		 */
		current_rcb = rx_data->work_list[rx_next];
		current_rbd->read.pkt_addr = current_rcb->rx_buf.dma_address;
		current_rbd->read.hdr_addr = 0;

		rx_next = NEXT_INDEX(rx_next, 1, rx_data->ring_size);

		/*
		 * The receive function is in interrupt context, so here
		 * rx_limit_per_intr is used to avoid doing receiving too long
		 * per interrupt.
		 */
		if (++pkt_num > igbvf->rx_limit_per_intr) {
			IGBVF_DEBUG_STAT(rx_ring->stat_exceed_pkt);
			break;
		}

		current_rbd = &rx_data->rbd_ring[rx_next];
		status_error = current_rbd->wb.upper.status_error;
	}

	DMA_SYNC(&rx_data->rbd_area, DDI_DMA_SYNC_FORDEV);

	rx_data->rbd_next = rx_next;

	/*
	 * Update the h/w tail accordingly
	 */
	rx_tail = PREV_INDEX(rx_next, 1, rx_data->ring_size);

	E1000_WRITE_REG(&igbvf->hw, E1000_RDT(rx_ring->queue), rx_tail);

	if (igbvf_check_acc_handle(igbvf->osdep.reg_handle) != DDI_FM_OK) {
		ddi_fm_service_impact(igbvf->dip, DDI_SERVICE_DEGRADED);
		atomic_or_32(&igbvf->igbvf_state, IGBVF_ERROR);
	}

	return (mblk_head);
}

static void
igbvf_strip_vlan(mblk_t *mp)
{
	unsigned char *new;

	new = mp->b_rptr + VLAN_TAGSZ;
	bcopy(mp->b_rptr, new, 2 * ETHERADDRL);
	mp->b_rptr = new;
}
