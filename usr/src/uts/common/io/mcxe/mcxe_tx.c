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

#include "mcxe.h"


#define	NEXT(index, limit)	((index)+1 < (limit) ? (index)+1 : 0)

static void
mcxe_unbind_txb(struct mcxe_txbuf *txb)
{
	int i;

	for (i = 0; i < txb->num_dma_seg; i++)
		(void) ddi_dma_unbind_handle(txb->dma_handle_table[i]);

	txb->num_dma_seg = 0;
	txb->num_sge = 0;
}

static void
mcxe_tx_recycle(struct mcxe_tx_ring *tx_ring, mcxe_queue_item_t *txbuf_item)
{
	struct mcxe_txbuf *txb;
	mcxe_queue_t *txbuf_queue;

	ASSERT(mutex_owned(&tx_ring->tc_lock));

	txb = txbuf_item->item;
	if (txb->copy_len != 0) {
		txb->copy_len = 0;
		txb->num_sge = 0;
	}
	if (txb->num_dma_seg != 0)
		mcxe_unbind_txb(txb);
	if (txb->mp) {
		freemsg(txb->mp);
		txb->mp = NULL;
	}

	/*
	 * Return tx buffers to buffer push queue
	 */
	txbuf_queue = tx_ring->txbuf_push_queue;
	mutex_enter(txbuf_queue->lock);
	txbuf_item->next = txbuf_queue->head;
	txbuf_queue->head = txbuf_item;
	txbuf_queue->count++;
	mutex_exit(txbuf_queue->lock);
	atomic_inc_32(&tx_ring->tx_free);

	/*
	 * Check if we need exchange the tx buffer push and pop queue
	 */
	if ((tx_ring->txbuf_pop_queue->count < tx_ring->tx_buffers_low) &&
	    (tx_ring->txbuf_pop_queue->count < txbuf_queue->count)) {
		tx_ring->txbuf_push_queue = tx_ring->txbuf_pop_queue;
		tx_ring->txbuf_pop_queue = txbuf_queue;
	}
}

void
mcxe_tx_intr_handler(mcxnex_state_t *mcxnex_state, mcxnex_cqhdl_t cq_hdl,
    void *cb_arg)
{
	struct mcxe_tx_ring *tx_ring = cb_arg;
	ibt_wc_t wc;
	int ret;
	mcxe_queue_item_t *txbuf_item;

	mutex_enter(&tx_ring->tc_lock);

	for (;;) {
		ret = mcxnex_cq_poll(mcxnex_state, cq_hdl, &wc, 1, NULL);
		switch (ret) {
		case IBT_CQ_EMPTY:
			(void) mcxnex_cq_notify(mcxnex_state, cq_hdl,
			    IBT_NEXT_COMPLETION);
			/* Tx reschedule */
			if (tx_ring->tx_resched_needed &&
			    (tx_ring->tx_free > tx_ring->tx_buffers_low)) {
				tx_ring->tx_resched_needed = 0;
				mac_tx_update(tx_ring->port->mac_hdl);
				tx_ring->tx_resched++;
			}
			mutex_exit(&tx_ring->tc_lock);
			return;

		case DDI_SUCCESS:
			if (!(tx_ring->port->if_state & MCXE_IF_STARTED))
				break;
			txbuf_item =
			    (mcxe_queue_item_t *)(unsigned long)wc.wc_id;
			mcxe_tx_recycle(tx_ring, txbuf_item);
			break;

		default:
			cmn_err(CE_WARN, "!mcxe%d: tx cqe error (ret=%x)",
			    tx_ring->port->instance, ret);
			mutex_exit(&tx_ring->tc_lock);
			return;
		}
	}
}

static mcxe_queue_item_t *
mcxe_get_txbuf(struct mcxe_tx_ring *tx_ring)
{
	mcxe_queue_item_t *txbuf_item;
	mcxe_queue_t *txbuf_queue;

	txbuf_queue = tx_ring->txbuf_pop_queue;
	mutex_enter(txbuf_queue->lock);
	if (txbuf_queue->count == 0) {
		mutex_exit(txbuf_queue->lock);
		txbuf_queue = tx_ring->txbuf_push_queue;
		mutex_enter(txbuf_queue->lock);
		if (txbuf_queue->count == 0) {
			mutex_exit(txbuf_queue->lock);
			return (NULL);
		}
	}
	txbuf_item = txbuf_queue->head;
	txbuf_queue->head = (mcxe_queue_item_t *)txbuf_item->next;
	txbuf_queue->count--;
	txbuf_item->next = NULL;
	mutex_exit(txbuf_queue->lock);

	return (txbuf_item);
}

static void
mcxe_tx_copy(struct mcxe_tx_ring *tx_ring, struct mcxe_txbuf *txb,
    mblk_t *mp, uint32_t copy_limit)
{
	mcxe_port_t *port = tx_ring->port;
	mblk_t *bp;
	uint32_t mblen;
	char *pbuf;

	ASSERT(txb->num_sge == 0);
	ASSERT(txb->copy_len == 0);

	pbuf = txb->dma_buf.address;
	for (bp = mp; bp != NULL; bp = bp->b_cont) {
		if ((mblen = MBLKL(bp)) == 0)
			continue;
		ASSERT(txb->copy_len + mblen <= port->tx_buff_size);
		bcopy(bp->b_rptr, pbuf, mblen);
		pbuf += mblen;
		txb->copy_len += mblen;
		if (copy_limit && (txb->copy_len >= copy_limit)) {
			ASSERT(txb->copy_len == copy_limit);
			break;
		}
	}
	(void) ddi_dma_sync(txb->dma_buf.dma_handle, 0,
	    txb->copy_len, DDI_DMA_SYNC_FORDEV);

	txb->dma_frags[txb->num_sge].ds_len = txb->copy_len;
	txb->dma_frags[txb->num_sge].ds_key =
	    port->mcxnex_state->hs_devlim.rsv_lkey;
	txb->dma_frags[txb->num_sge].ds_va = txb->dma_buf.dma_address;
	txb->num_sge++;
}

static int
mcxe_tx_bind_frag(struct mcxe_tx_ring *tx_ring, struct mcxe_txbuf *txb,
    mblk_t *mp, uint32_t len)
{
	mcxe_port_t *port = tx_ring->port;
	ddi_dma_handle_t dma_hdl;
	ddi_dma_cookie_t dma_cookie;
	uint_t ncookies;
	int ret, i;

	dma_hdl = txb->dma_handle_table[txb->num_dma_seg];

	/*
	 * Use DMA binding to process the mblk fragment
	 */
	ret = ddi_dma_addr_bind_handle(dma_hdl,
	    NULL, (caddr_t)mp->b_rptr, len,
	    DDI_DMA_WRITE | DDI_DMA_STREAMING, DDI_DMA_DONTWAIT,
	    0, &dma_cookie, &ncookies);
	if (ret != DDI_DMA_MAPPED) {
		tx_ring->tx_bindfail++;
		mcxe_unbind_txb(txb);
		return (DDI_FAILURE);
	}
	txb->num_dma_seg++;

	/*
	 * Each fragment can span several cookies.
	 */
	for (i = 0; i < ncookies; i++) {
		txb->dma_frags[txb->num_sge].ds_len =
		    (uint32_t)dma_cookie.dmac_size;
		txb->dma_frags[txb->num_sge].ds_key =
		    port->mcxnex_state->hs_devlim.rsv_lkey;
		txb->dma_frags[txb->num_sge].ds_va = dma_cookie.dmac_laddress;
		txb->num_sge++;
		if (txb->num_sge == port->max_tx_frags) {
			tx_ring->tx_bindexceed++;
			mcxe_unbind_txb(txb);
			return (DDI_FAILURE);
		}
		ddi_dma_nextcookie(dma_hdl, &dma_cookie);
	}

	return (DDI_SUCCESS);
}

static int
mcxe_tx_bind(struct mcxe_tx_ring *tx_ring, struct mcxe_txbuf *txb, mblk_t *mp)
{
	mblk_t *bp;
	uint32_t mblen;
	int ret;

	ASSERT(txb->num_dma_seg == 0);
	for (bp = mp; bp != NULL; bp = bp->b_cont) {
		if ((mblen = MBLKL(bp)) == 0)
			continue;
		ret = mcxe_tx_bind_frag(tx_ring, txb, bp, mblen);
		if (ret != DDI_SUCCESS)
			return (ret);
	}

	return (DDI_SUCCESS);
}


static mblk_t *
mcxe_cal_lso_hdrsz(mblk_t *mp, uint32_t *lso_hdrsz, uint32_t *lso_hdrfragsz)
{
	mblk_t *nmp;
	struct ether_vlan_header *ethp;
	int ethh_len, iph_len, tcph_len, mblen;
	uintptr_t ip_start, tcp_start;

	/*
	 * Calculate the LSO header size.
	 * Note that the only assumption we make is that each of the Ethernet,
	 * IP and TCP headers will be contained in a single mblk fragment;
	 * together, the headers may span multiple mblk fragments.
	 */
	ASSERT(MBLKL(mp) >= sizeof (struct ether_header));
	ethp = (struct ether_vlan_header *)(void *)mp->b_rptr;
	if (ethp->ether_tpid == htons(ETHERTYPE_VLAN))
		ethh_len = sizeof (struct ether_vlan_header);
	else
		ethh_len = sizeof (struct ether_header);

	nmp = mp;
	mblen = MBLKL(nmp);
	ip_start = (uintptr_t)(nmp->b_rptr) + ethh_len;
	if (ip_start >= (uintptr_t)(nmp->b_wptr)) {
		ip_start = (uintptr_t)nmp->b_cont->b_rptr
		    + (ip_start - (uintptr_t)(nmp->b_wptr));
		nmp = nmp->b_cont;
		mblen += MBLKL(nmp);

	}
	iph_len = IPH_HDR_LENGTH((ipha_t *)ip_start);

	tcp_start = ip_start + iph_len;
	if (tcp_start >= (uintptr_t)(nmp->b_wptr)) {
		tcp_start = (uintptr_t)nmp->b_cont->b_rptr
		    + (tcp_start - (uintptr_t)(nmp->b_wptr));
		nmp = nmp->b_cont;
		mblen += MBLKL(nmp);
	}
	tcph_len = TCP_HDR_LENGTH((tcph_t *)tcp_start);

	*lso_hdrsz = ethh_len + iph_len + tcph_len;
	*lso_hdrfragsz = MBLKL(nmp) - (mblen - *lso_hdrsz);

	return (nmp);
}

mblk_t *
mcxe_tx(void *arg, mblk_t *mp)
{
	mcxe_port_t *port = arg;
	struct mcxe_tx_ring *tx_ring;
	uint32_t chksum_flags, lso_flags, mss;
	uint32_t mblen, pktsz = 0, bind_mblks = 0, copy_len = 0;
	uint32_t lso_hdrsz = 0, lso_hdrfragsz = 0;
	boolean_t force_bcopy = B_FALSE;
	mblk_t *bp, *bind_bp, *pull_mp = NULL;
	ibt_send_wr_t wr;
	mcxe_queue_item_t *txbuf_item;
	struct mcxe_txbuf *txbuf;
	uint_t posted;
	int ret;

	ASSERT(mp->b_next == NULL);

	/* sanity port status check */
	if (!(port->if_state & MCXE_IF_STARTED) ||
	    (port->link_state != LINK_STATE_UP)) {
		freemsg(mp);
		return (NULL);
	}

	/* choose a tx ring */
	tx_ring = &port->tx_rings[0]; /* currently only one tx ring */

	/* get packet offloading flags */
	mac_hcksum_get(mp, NULL, NULL, NULL, NULL, &chksum_flags);
	mac_lso_get(mp, &mss, &lso_flags);

	/* count the packet size and mblks */
	for (bp = bind_bp = mp; bp != NULL; bp = bp->b_cont) {
		if ((mblen = MBLKL(bp)) == 0)
			continue;
		bind_mblks++;
		/* first mblk */
		if (pktsz == 0 && mblen <= port->tx_copy_thresh) {
			copy_len += mblen;
			bind_mblks--;
			bind_bp = bp->b_cont;
		}
		/* continous mblk(s) with size less than tx_copy_thresh */
		if ((copy_len == pktsz) && (mblen <= port->tx_copy_thresh) &&
		    (copy_len + mblen) <= port->tx_buff_size) {
			copy_len += mblen;
			bind_mblks--;
			bind_bp = bp->b_cont;
		}
		pktsz += mblen;
	}
	if ((lso_flags != HW_LSO) && (pktsz > port->tx_buff_size)) {
		/* wrong size packet */
		tx_ring->tx_drop++;
		freemsg(mp);
		return (NULL);
	}
	if (bind_mblks >= (port->max_tx_frags - 1)) {
		if (pktsz <= port->tx_buff_size) {
			force_bcopy = B_TRUE;
		} else {
			pull_mp = msgpullup(mp, -1);
			if (pull_mp != NULL) {
				tx_ring->tx_pullup++;
				freemsg(mp);
				mp = bind_bp = pull_mp;
				copy_len = 0;
			} else {
				tx_ring->tx_drop++;
				freemsg(mp);
				return (NULL);
			}
		}
	}
	/* get a tx buffer */
	txbuf_item = mcxe_get_txbuf(tx_ring);
	if (txbuf_item == NULL) {
		/* no tx buffer available */
		tx_ring->tx_nobuf++;
		tx_ring->tx_resched_needed = 1;
		return (mp);
	}
	txbuf = txbuf_item->item;
	atomic_dec_32(&tx_ring->tx_free);

tx_retry:
	bp = mp;
	if (lso_flags == HW_LSO) {	/* calculate LSO header size */
		bp = mcxe_cal_lso_hdrsz(mp, &lso_hdrsz, &lso_hdrfragsz);
		if (copy_len > lso_hdrsz)
			copy_len -= lso_hdrsz;
		else {
			copy_len = 0;
			bind_bp = bp;
		}
	}
	bp->b_rptr += lso_hdrfragsz;	/* skip the LSO header fragment */

	/* get the tx buffer ready for h/w Tx */
	if (pktsz <= port->tx_copy_thresh || force_bcopy) {
		mcxe_tx_copy(tx_ring, txbuf, bp, 0);
		tx_ring->tx_bcopy++;
	} else {
		if (copy_len)
			mcxe_tx_copy(tx_ring, txbuf, bp, copy_len);
		if (bind_bp != NULL) {
			ret = mcxe_tx_bind(tx_ring, txbuf, bind_bp);
			if (ret != DDI_SUCCESS) {
				if (copy_len)
					txbuf->copy_len = 0;
				if (pktsz > port->tx_buff_size) {
					bp->b_rptr -= lso_hdrfragsz;
					/* mp was pulled up before */
					if (pull_mp != NULL)
						goto tx_fail;
					/* pull up the mblks */
					pull_mp = msgpullup(mp, -1);
					if (pull_mp != NULL) {
						tx_ring->tx_pullup++;
						freemsg(mp);
						mp = bind_bp = pull_mp;
						copy_len = 0;
						goto tx_retry;
					}
					goto tx_fail;
				} else
					mcxe_tx_copy(tx_ring, txbuf, bp, 0);
			}
		}
	}
	bp->b_rptr -= lso_hdrfragsz;

	/* post the tx buffer to h/w */
	wr.wr_opcode = IBT_WRC_SEND;
	if (lso_flags == HW_LSO) {
		wr.wr_opcode = IBT_WRC_SEND_LSO;
		wr.wr.ud_lso.lso_hdr = (uint8_t *)mp;
		wr.wr.ud_lso.lso_hdr_sz = lso_hdrsz;
		wr.wr.ud_lso.lso_mss = mss;
	}
	wr.wr_flags = IBT_WR_SEND_SIGNAL | IBT_WR_SEND_SOLICIT;
	if (chksum_flags & (HCKSUM_INET_FULL_V4 | HCKSUM_IPHDRCKSUM)) {
		/* UDP/TCP/IP checksum offloading */
		wr.wr_flags |= IBT_WR_SEND_CKSUM;
	}
	wr.wr_nds = txbuf->num_sge;
	wr.wr_sgl = txbuf->dma_frags;
	wr.wr_id = (unsigned long)(unsigned long *)txbuf_item;
	ASSERT(txbuf->mp == NULL);
	txbuf->mp = mp;

	/* post the packet into tx QP for real h/w transmission */
	ret = mcxnex_post_send(port->mcxnex_state, tx_ring->tx_qp, &wr,
	    1, &posted);
	if (ret) {
		if (mcxe_verbose) {
			cmn_err(CE_WARN, "!mcxe%d: post send failed (ret=%d)",
			    port->instance, ret);
		}
		tx_ring->tx_nobd++;
		txbuf->mp = NULL;
		goto tx_fail;
	}

	return (NULL);

tx_fail:
	mutex_enter(&tx_ring->tc_lock);
	/* put back the tx buffer to buffer queue */
	mcxe_tx_recycle(tx_ring, txbuf_item);
	tx_ring->tx_resched_needed = 1;
	mutex_exit(&tx_ring->tc_lock);

	return (mp);
}
