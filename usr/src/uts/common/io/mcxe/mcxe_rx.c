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
 *  Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Copyright (c) 2010 Mellanox Technologies. All rights reserved.
 */


#include "mcxe.h"


int
mcxe_post_recv(struct mcxe_rxbuf *rxb)
{
	struct mcxe_rx_ring *rx_ring = rxb->rx_ring;
	dma_buffer_t *dma_buf = &rxb->dma_buf;
	ibt_recv_wr_t wr;
	ibt_wr_ds_t sge;
	uint_t posted;
	int ret;

	sge.ds_key = rx_ring->port->mcxnex_state->hs_devlim.rsv_lkey;
	sge.ds_len = dma_buf->size;
	sge.ds_va = dma_buf->dma_address;

	wr.wr_id = (unsigned long)rxb;
	wr.wr_nds = 1;
	wr.wr_sgl = &sge;

	/* post back the rxb to CX2 chip */
	ret = mcxnex_post_recv(rx_ring->port->mcxnex_state, rx_ring->rx_qp,
	    &wr, 1, &posted);
	if (ret != DDI_SUCCESS) {
		if (mcxe_verbose) {
			cmn_err(CE_WARN, "!mcxe%d: mcxe_post_recv() failed to "
			    "post recv wqe index %d (ret %d)\n",
			    rx_ring->port->instance, rxb->index, ret);
		}
		rx_ring->rx_postfail++;
		return (DDI_FAILURE);
	}

	atomic_inc_32(&rx_ring->rx_free);

	return (DDI_SUCCESS);
}

void
mcxe_rxb_free_cb(caddr_t arg)
{
	struct mcxe_rxbuf *rxb = (struct mcxe_rxbuf *)(void *)arg;
	struct mcxe_rx_ring *rx_ring = rxb->rx_ring;
	mcxe_port_t *port = rx_ring->port;

	if (rxb->ref_cnt == 0) {
		/*
		 * This case only happens when rx buffers are being freed
		 * in mcxe_alloc_rxb_table() and freemsg() is called.
		 */
		rxb->mp = NULL;
		return;
	}

	rw_enter(&rx_ring->rc_rwlock, RW_READER);
	if (rxb->flag & MCXE_RXB_STOPPED) {
		rw_exit(&rx_ring->rc_rwlock);
		mcxe_post_rxb_free(rxb);
		return;
	}

	if (!(port->if_state & MCXE_IF_STARTED)) {
		rxb->mp = NULL;
		atomic_dec_32(&rxb->ref_cnt);
		rw_exit(&rx_ring->rc_rwlock);
		return;
	}

	atomic_dec_32(&rxb->ref_cnt);
	rxb->mp = desballoc((unsigned char *)rxb->dma_buf.address,
	    rxb->dma_buf.size, 0, &rxb->rxb_free_rtn);
	(void) mcxe_post_recv(rxb);
	rw_exit(&rx_ring->rc_rwlock);
}

static mblk_t *
mcxe_rx_one_packet(struct mcxe_rx_ring *rx_ring, ibt_wc_t *wcp)
{
	mcxe_port_t *port = rx_ring->port;
	struct mcxe_rxbuf *rxb;
	struct ether_vlan_header *ehp;
	int pflags;
	mblk_t *mp = NULL;
	uint32_t len;
	boolean_t use_bcopy = B_FALSE;

	rxb = (struct mcxe_rxbuf *)(unsigned long)wcp->wc_id;
	len = wcp->wc_bytes_xfer;
	ASSERT((len != 0) && (len <= rxb->dma_buf.size));
	(void) ddi_dma_sync(rxb->dma_buf.dma_handle,
	    0, len, DDI_DMA_SYNC_FORKERNEL);

	/* receive the packet */
	if (rxb->mp == NULL) {
		rxb->mp = desballoc((unsigned char *)rxb->dma_buf.address,
		    rxb->dma_buf.size, 0, &rxb->rxb_free_rtn);
	}
	if (len <= port->rx_copy_thresh ||
	    rx_ring->rx_free < MCXE_RX_SLOTS_MIN) {
		mp = allocb(MCXE_IPHDR_ALIGN_ROOM + len, 0);
		use_bcopy = B_TRUE;
	}

	if (mp != NULL) { /* use bcopy() way */
		mp->b_rptr += MCXE_IPHDR_ALIGN_ROOM;
		bcopy(rxb->dma_buf.address, mp->b_rptr, len);
		(void) mcxe_post_recv(rxb);
		rx_ring->rx_bcopy++;
	} else if (!use_bcopy && (rxb->mp != NULL)) {
		/* use desballoc() way */
		mp = rxb->mp;
		atomic_inc_32(&rxb->ref_cnt);
		rx_ring->rx_bind++;
	} else { /* fail: no receive buffer */
		rx_ring->rx_allocfail++;
		(void) mcxe_post_recv(rxb);
		return (NULL);
	}

	/* TCP/UDP/IP checksum offloading */
	if ((wcp->wc_flags & IBT_WC_CKSUM_OK) &&
	    (wcp->wc_cksum == 0xFFFF)) {
		pflags = HCK_IPV4_HDRCKSUM_OK;
		if (wcp->wc_flags & IBT_WC_CKSUM_TCP_UDP_OK)
			pflags |= HCK_FULLCKSUM_OK;
		mac_hcksum_set(mp, 0, 0, 0, 0, pflags);
	}

	/*
	 * VLAN packet ?
	 */
	if ((wcp->wc_vlanid != 0) &&
	    (!port->mcxnex_state->hs_vlan_strip_off_cap)) {
		/*
		 * As h/w strips the VLAN tag from incoming packet, we need
		 * to insert VLAN tag into the packet manually.
		 */
		(void) memmove(mp->b_rptr - VLAN_TAGSZ, mp->b_rptr,
		    2 * ETHERADDRL);
		mp->b_rptr -= VLAN_TAGSZ;
		len += VLAN_TAGSZ;
		ehp = (struct ether_vlan_header *)(void *)mp->b_rptr;
		ehp->ether_tpid = htons(ETHERTYPE_VLAN);
		ehp->ether_tci = wcp->wc_vlanid;
	}
	mp->b_wptr = mp->b_rptr + len;
	mp->b_next = mp->b_cont = NULL;

	return (mp);
}

static mblk_t *
mcxe_rx_cq_poll(void *arg, int n_bytes)
{
	struct mcxe_rx_ring *rx_ring = (struct mcxe_rx_ring *)arg;
	mcxe_port_t *port = rx_ring->port;
	mblk_t *mp, *mp_head, **mp_tail;
	mcxnex_cqhdl_t cq_hdl;
	ibt_wc_t wc;
	int ret, total_bytes = 0;

	cq_hdl = rx_ring->rx_cq;
	mp_head = NULL;
	mp_tail = &mp_head;

	for (;;) {
		ret = mcxnex_cq_poll(port->mcxnex_state, cq_hdl, &wc, 1, NULL);
		switch (ret) {
		case IBT_CQ_EMPTY:
			(void) mcxnex_cq_notify(port->mcxnex_state,
			    rx_ring->rx_cq, IBT_NEXT_COMPLETION);
			return (mp_head);

		case DDI_SUCCESS:
			atomic_dec_32(&rx_ring->rx_free);
			mp = mcxe_rx_one_packet(rx_ring, &wc);
			if (mp == NULL)
				break;

			*mp_tail = mp;
			mp_tail = &mp->b_next;

			total_bytes += wc.wc_bytes_xfer;
			if ((n_bytes > 0) && (total_bytes >= n_bytes)) {
				/* received enough */
				return (mp_head);
			}

			break;

		default:
			cmn_err(CE_WARN, "!mcxe%d: mcxe_rx_cq_poll failed "
			    "(ret=%d)", port->instance, ret);
			return (NULL);
		}
	}
}

/*ARGSUSED*/
void
mcxe_rx_intr_handler(mcxnex_state_t *mcxnex_state, mcxnex_cqhdl_t cq_hdl,
    void *cb_arg)
{
	struct mcxe_rx_ring *rx_ring = (struct mcxe_rx_ring *)cb_arg;
	mcxe_port_t *port = rx_ring->port;
	mblk_t *mp;

	mutex_enter(&rx_ring->rx_lock);
	if (!(port->if_state & MCXE_IF_STARTED)) {
		mutex_exit(&rx_ring->rx_lock);
		return;
	}
	mp = mcxe_rx_cq_poll(cb_arg, -1);
	mutex_exit(&rx_ring->rx_lock);

	if (mp)
		mac_rx(port->mac_hdl, NULL, mp);
}
