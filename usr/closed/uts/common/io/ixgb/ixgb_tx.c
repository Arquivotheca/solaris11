/*
 * Copyright (c) 2004, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#include "ixgb.h"

#undef	IXGB_DBG
#define	IXGB_DBG		IXGB_DBG_SEND

/*
 * Recycle a buffer that a message was mapped into
 */
static void ixgb_tx_recycle_mapped(send_ring_t *srp, sw_sbd_t *ssbdp);
#pragma	inline(ixgb_tx_recycle_mapped)

static void
ixgb_tx_recycle_mapped(send_ring_t *srp, sw_sbd_t *ssbdp)
{
	uint32_t frag_no;
	ixgb_queue_item_t *hdl_item;
	ixgb_queue_t *txhdl_queue;
	ddi_dma_handle_t *mblk_hdl;

	/*
	 * Check if we need exchange the tx dma handle push and pop queue
	 */
	if (srp->txhdl_pop_queue->count < IXGB_TX_HANDLES_LO) {
		txhdl_queue = srp->txhdl_pop_queue;
		srp->txhdl_pop_queue = srp->txhdl_push_queue;
		srp->txhdl_push_queue = txhdl_queue;
	}
	txhdl_queue = srp->txhdl_push_queue;

	/*
	 * This slot refers to the original mblk passed to ixgb_send_mapped()
	 * So we should unbind the mblk here ...
	 */
	if (ssbdp->mp != NULL) {
		for (frag_no = 0; frag_no < ssbdp->frags; ++frag_no) {
			hdl_item = ssbdp->mblk_hdl[frag_no];
			mblk_hdl = hdl_item->item;
			if (mblk_hdl != NULL) {
				(void) ddi_dma_unbind_handle(*mblk_hdl);
				IXGB_QUEUE_PUSH(txhdl_queue, hdl_item);
				ssbdp->mblk_hdl[frag_no] = NULL;
			}
		}
		freemsg(ssbdp->mp);
		ssbdp->mp = NULL;
	}
}

void
ixgb_tx_recycle_all(ixgb_t *ixgbp)
{
	send_ring_t *srp;
	sw_sbd_t *ssbdp;
	uint32_t slot;
	uint32_t frag_no;
	ddi_dma_handle_t *mblk_hdl;

	srp = ixgbp->send;
	ssbdp = srp->sw_sbds;
	for (slot = 0; slot < IXGB_SEND_SLOTS_USED; ++slot) {
		if (ssbdp->mp != NULL) {
			for (frag_no = 0; frag_no < ssbdp->frags; ++frag_no) {
				mblk_hdl = ssbdp->mblk_hdl[frag_no]->item;
				if (mblk_hdl != NULL) {
					(void) ddi_dma_unbind_handle(*mblk_hdl);
					ssbdp->mblk_hdl[frag_no] = NULL;
				}
			}
			freemsg(ssbdp->mp);
			ssbdp->mp = NULL;
		}
		ssbdp++;
	}
}

/*
 * Reclaim the resource after tx's completion
 */
boolean_t
ixgb_tx_recycle(ixgb_t *ixgbp)
{
	send_ring_t *srp;
	uint32_t tx_head;
	uint32_t slot;
	uint32_t count;
	sw_sbd_t *ssbdp;
	ixgb_sbd_t *hw_sbd_p;
	boolean_t tx_done = B_FALSE;

	/*
	 * Sync (all) the send ring descriptors
	 * before recycling the buffers they describe
	 */
	srp = ixgbp->send;
	DMA_SYNC(srp->desc, DDI_DMA_SYNC_FORKERNEL);

	/*
	 * Look through the send ring by tx's head pointer
	 * to find all the bds which has been transmitted sucessfully
	 * then reclaim all resouces associated with these bds
	 */
	mutex_enter(srp->tc_lock);
	count = 0;
	tx_head = ixgb_reg_get32(ixgbp, IXGB_TDH);
	slot = srp->tc_next;
	ssbdp = &srp->sw_sbds[slot];
	hw_sbd_p = DMA_VPTR(ssbdp->desc);
	while (hw_sbd_p->status & IXGB_RSTATUS_DD) {
		if (ssbdp->tx_recycle) {
			(*ssbdp->tx_recycle)(srp, ssbdp);
			ssbdp->tx_recycle = NULL;
		}
		DMA_ZERO(ssbdp->desc);
		count++;
		slot = NEXT(slot, srp->desc.nslots);
		if (slot == tx_head)
			break;
		ssbdp = &srp->sw_sbds[slot];
		hw_sbd_p = DMA_VPTR(ssbdp->desc);
	}
	if (count != 0) {
		ixgbp->watchdog = 1;
		tx_done = B_TRUE;
		srp->tc_next = slot;
		ixgb_atomic_renounce(&srp->tx_free, count);
		DMA_SYNC(srp->desc, DDI_DMA_SYNC_FORDEV);
		/*
		 * up to this place, we have reclaimed some resource, if there
		 * is a requirement to report to MAC layer, report this.
		 */
		if (ixgbp->resched_needed && !ixgbp->resched_running) {
			ixgbp->resched_running = B_TRUE;
			ddi_trigger_softintr(ixgbp->resched_id);
		}
	}
	if (srp->tx_free == IXGB_SEND_SLOTS_USED)
		ixgbp->watchdog = 0;
	mutex_exit(srp->tc_lock);

	/*
	 * We're about to release one or more places :-)
	 * These ASSERTions check that our invariants still hold:
	 * there must always be at least one free place
	 * at this point, there must be at least one place NOT free
	 * we're not about to free more places than were claimed!
	 */
	ASSERT(srp->tx_free <= srp->desc.nslots);

	return (tx_done);
}

/*
 * CLAIM an already-reserved place on the next train
 *
 * This is the point of no return!
 */
static void ixgb_send_claim(send_ring_t *srp,
    uint32_t bd_nums, uint32_t *start_index, uint32_t *end_index);
#pragma	inline(ixgb_send_claim)

static void
ixgb_send_claim(send_ring_t *srp,
    uint32_t bd_nums, uint32_t *start_index, uint32_t *end_index)
{
	uint32_t slot;
	uint32_t next;

	ASSERT(mutex_owned(srp->tx_lock));
	slot = srp->tx_next;
	ASSERT(slot < srp->desc.nslots);

	*start_index = slot;
	next = NEXT_N(slot, bd_nums, srp->desc.nslots);
	srp->tx_next = next;
	*end_index = LAST(next, srp->desc.nslots);
	srp->tx_flow++;
}

/*
 * Retrieve the start_offset and Stuff offset from the transmitting messages
 * to support intel's tx's partial checksum.
 */
static int ixgb_tx_lso_chksum(send_ring_t *srp, mblk_t *mp, uint32_t sum_flags,
    uint32_t lso_flags, uint32_t mss, uint32_t start_offset,
    uint32_t stuff_offset);
#pragma inline(ixgb_tx_lso_chksum)

static int
ixgb_tx_lso_chksum(send_ring_t *srp, mblk_t *mp, uint32_t sum_flags,
    uint32_t lso_flags, uint32_t mss, uint32_t start_offset,
    uint32_t stuff_offset)
{
	boolean_t vlan;
	uint8_t ether_header_size;
	uint32_t hdr_len;
	ipha_t *lso_ip;
	tcph_t *lso_tcp;

	/*
	 * If needed, readjust checksum start/offset
	 * and this requires to load another tx's checksum context again.
	 * This section code must be protected by lock tx_lock
	 */
	ASSERT(mutex_owned(srp->tx_lock));
	if (sum_flags) {
		vlan = IS_VLAN_PACKET(mp->b_rptr);
		if (vlan)
			ether_header_size = sizeof (struct ether_vlan_header);
		else
			ether_header_size = sizeof (struct ether_header);
		if (lso_flags & HW_LSO) {
			lso_ip = (ipha_t *)((uchar_t *)(mp->b_rptr)
			    + ether_header_size);
			lso_tcp = (tcph_t *)((uchar_t *)lso_ip
			    + IPH_HDR_LENGTH(lso_ip));
			hdr_len = IPH_HDR_LENGTH(lso_ip)
			    + TCP_HDR_LENGTH(lso_tcp) + ether_header_size;
			lso_ip->ipha_length = 0;
			lso_ip->ipha_hdr_checksum = 0;
			srp->sum_flags = sum_flags;
			srp->lso_flags = lso_flags;
			srp->ether_header_size = ether_header_size;
			srp->start_offset = start_offset;
			srp->stuff_offset = stuff_offset;
			srp->mss = mss;
			srp->hdr_len = hdr_len;
			return (LOAD_CONT);
		}
		if (srp->sum_flags != sum_flags ||
		    srp->lso_flags != lso_flags ||
		    srp->ether_header_size != ether_header_size ||
		    srp->start_offset != start_offset ||
		    srp->stuff_offset != stuff_offset) {
			srp->sum_flags = sum_flags;
			srp->lso_flags = lso_flags;
			srp->ether_header_size = ether_header_size;
			srp->start_offset = start_offset;
			srp->stuff_offset = stuff_offset;
			return (LOAD_CONT);
		}
	}
	return (LOAD_NONE);
}

/*
 * Filling the contents of tx's context BD
 */
static void ixgb_set_hw_cbd_flags(ixgb_t *ixgbp, sw_sbd_t *ssbdp,
    uint32_t mblen);
#pragma inline(ixgb_set_hw_cbd_flags)

static void
ixgb_set_hw_cbd_flags(ixgb_t *ixgbp, sw_sbd_t *ssbdp, uint32_t mblen)
{
	send_ring_t *srp;
	ixgb_cbd_t *hw_cbd_p;
	uint8_t ether_header_size;

	srp = ixgbp->send;
	ether_header_size = srp->ether_header_size;
	hw_cbd_p = (ixgb_cbd_t *)DMA_VPTR(ssbdp->desc);
	ixgbp->statistics.sw_statistics.load_context++;

	/* loading a new context descriptor */
	if (srp->sum_flags & HCK_IPV4_HDRCKSUM) {
		hw_cbd_p->ip_csum_part.ip_csum_fields.ipcss =
		    ether_header_size;
		hw_cbd_p->ip_csum_part.ip_csum_fields.ipcso =
		    ether_header_size + offsetof(struct ip, ip_sum);
		hw_cbd_p->ip_csum_part.ip_csum_fields.ipcse =
		    ether_header_size + srp->start_offset - 1;
	} else
		hw_cbd_p->ip_csum_part.ip_csum = 0;

	if (srp->sum_flags & HCK_PARTIALCKSUM) {
		hw_cbd_p->tcp_csum_part.tcp_csum_fields.tucss
		    = srp->start_offset + ether_header_size;
		hw_cbd_p->tcp_csum_part.tcp_csum_fields.tucso
		    = srp->stuff_offset + ether_header_size;
		hw_cbd_p->tcp_csum_part.tcp_csum_fields.tucse = 0;
	} else
		hw_cbd_p->tcp_csum_part.tcp_csum = 0;

	if (srp->lso_flags & HW_LSO) {
		hw_cbd_p->tcp_seg_part.tcp_seg_fileds.mss = srp->mss;
		hw_cbd_p->tcp_seg_part.tcp_seg_fileds.hdr_len =
		    srp->hdr_len;
		hw_cbd_p->cmd_type_len = IXGB_CBD_TSE | IXGB_CBD_IP
		    | IXGB_CBD_RS | IXGB_CBD_TYPE
		    | IXGB_CBD_TCP | (mblen - srp->hdr_len);
	} else {
		hw_cbd_p->cmd_type_len = (IXGB_CBD_TYPE | IXGB_CBD_RS);
	}

	DMA_SYNC(ssbdp->desc, DDI_DMA_SYNC_FORDEV);
}

/*
 * Filling the contents of Tx's data descriptor
 * before transmitting.
 */
static void ixgb_set_hw_sbd_flags(ixgb_sbd_t *hw_sbd_p,
    uint32_t sum_flags, uint32_t lso_flags, boolean_t end);
#pragma inline(ixgb_set_hw_sbd_flags)

static void
ixgb_set_hw_sbd_flags(ixgb_sbd_t *hw_sbd_p, uint32_t sum_flags,
    uint32_t lso_flags, boolean_t end)
{
	/* setting tcp checksum */
	if (sum_flags & HCK_PARTIALCKSUM)
		hw_sbd_p->popts |= IXGB_TBD_POPTS_TXSM;

	/* setting ip checksum */
	if (sum_flags & HCK_IPV4_HDRCKSUM)
		hw_sbd_p->popts |= IXGB_TBD_POPTS_IXSM;

	hw_sbd_p->len_cmd |= IXGB_TBD_TYPE;	/* data bd type */
	hw_sbd_p->len_cmd |= IXGB_TBD_RS;	/* report status to driver */
	if (lso_flags & HW_LSO)
		hw_sbd_p->len_cmd |= IXGB_TBD_TSE;

	/* indicating the end of BDs */
	if (end)
		hw_sbd_p->len_cmd |= IXGB_TBD_EOP;
}

static enum send_status
ixgb_send_copy(ixgb_t *ixgbp, mblk_t *mp, uint32_t totlen, uint32_t rsv_bds)
{
	send_ring_t *srp = ixgbp->send;
	mblk_t *bp;
	uint32_t mblen;
	uint32_t sum_flags;
	uint32_t lso_flags;
	uint32_t mss;
	uint32_t start_offset;
	uint32_t stuff_offset;
	boolean_t tcp_csum_load_cont;
	uint32_t bds;
	uint32_t start_index;
	uint32_t end_index;
	ixgb_sbd_t *hw_sbd_p;
	sw_sbd_t *ssbdp;
	char *txbuf;
	int ret;

	IXGB_TRACE(("ixgb_send_copy($%p, $%p, %d)",
	    (void *)ixgbp, (void *)mp, totlen));

	/*
	 * intel's 10G chipset uses a separate tx's BD called
	 * a context descriptor to set up support
	 * for partial checksumming w/o LSO.
	 * These change everytime the start_offset or stuff_offset
	 * changes, or when non checksummed packets arrive.
	 * For performance reasons, whenever possible we minimize
	 * sending out this setup context descriptor prior to the
	 * data descriptors.
	 *
	 *
	 * when the tx's context bd need to be loaded again, the driver
	 * must guarantee:
	 * 1.its context bd and its data bd must be conitunuous
	 * 2.the context bd can not been loaded
	 * until the previous data bd has been send out.
	 * Thus, the order should be like this:
	 * tcp context bd + tcp data bd + update tx's tail pointer
	 * + udp context bd + udp data bd + update tx's tail pointer.
	 *
	 *
	 * the following scenarios should not occur:
	 * 1. tcp context bd + udp context bd + tcp data bd + udp data bd +
	 * update tx's tail pointer.
	 * this will cause tx's tcp/udp checksum error.
	 *
	 * 2. tcp context bd + tcp data bd + udp context bd +
	 * update tx's tail pointer + udp data bd
	 * + update tx's tail pointer.
	 * this will cause tx's state machine enter the state of out of order
	 * and can not sent out packets.
	 */
	bds = 1;
	sum_flags = 0;
	lso_flags = 0;
	mss = 0;

	/* check lso information */
	if (ixgbp->lso_enable)
		mac_lso_get(mp, &mss, &lso_flags);
	/* retrieve checksum information */
	if (ixgbp->tx_hw_chksum)
		mac_hcksum_get(mp, &start_offset, &stuff_offset, NULL, NULL,
		    &sum_flags);
	if (lso_flags & HW_LSO) {
		/*
		 * ixgb LSO is depend on the h/w cksum, so the LSO packet
		 * should be dropped if the h/w cksum is not supported.
		 */
		if (!(sum_flags & HCK_PARTIALCKSUM)) {
			ixgb_atomic_renounce(&srp->tx_free, rsv_bds);
			freemsg(mp);
			mutex_enter(srp->tx_lock);
			srp->tx_flow++;
			mutex_exit(srp->tx_lock);
			return (SEND_COPY_SUCCESS);
		}
		sum_flags |= HCK_IPV4_HDRCKSUM;
	}

	tcp_csum_load_cont = B_FALSE;
	mutex_enter(srp->tx_lock);
	if (ixgbp->tx_hw_chksum) {
		ret = ixgb_tx_lso_chksum(srp, mp, sum_flags, lso_flags, mss,
		    start_offset, stuff_offset);
		if (ret == LOAD_CONT) {
			tcp_csum_load_cont = B_TRUE;
			bds += 1;
		}
	}

	/*
	 * Return extra reserved bds
	 */
	if (bds < rsv_bds)
		ixgb_atomic_renounce(&srp->tx_free, rsv_bds-bds);

	/*
	 * go straight to claiming our already-reserved places
	 * on the train!
	 */
	ixgb_send_claim(srp, bds, &start_index, &end_index);

	/*
	 * if there is no requiment to load a new checksum context,
	 * it is unnecessary to protect the following codes
	 */
	if (tcp_csum_load_cont)
		ixgb_set_hw_cbd_flags(ixgbp,
		    &srp->sw_sbds[start_index], totlen);

	mutex_exit(srp->tx_lock);

	ssbdp = &srp->sw_sbds[end_index];
	hw_sbd_p = DMA_VPTR(ssbdp->desc);
	txbuf = DMA_VPTR(ssbdp->pbuf);
	for (bp = mp; bp != NULL; bp = bp->b_cont) {
		mblen = MBLKL(bp);
		bcopy(bp->b_rptr, txbuf, mblen);
		txbuf += mblen;
	}

	DMA_SYNC(ssbdp->pbuf, DDI_DMA_SYNC_FORDEV);
	hw_sbd_p->host_buf_addr = ssbdp->pbuf.cookie.dmac_laddress;
	hw_sbd_p->len_cmd = totlen & IXGB_TBD_LEN_MASK;

	/*
	 * update flags in the send buffer descriptor for this message.
	 */
	ixgb_set_hw_sbd_flags(hw_sbd_p, sum_flags, lso_flags, B_TRUE);

	/*
	 * The message can be freed right away,
	 * as we've already copied the contents ...
	 */
	freemsg(mp);

	return (SEND_COPY_SUCCESS);
}

static enum send_status
ixgb_send_mapped(ixgb_t *ixgbp, mblk_t *mp, uint32_t totlen, uint32_t rsv_bds)
{
	send_ring_t *srp = ixgbp->send;
	mblk_t *bp;
	uint32_t mblen;
	ddi_dma_handle_t *mblk_hdl;
	ixgb_queue_t *txhdl_queue;
	ixgb_queue_item_t *hdl_item[IXGB_MAP_FRAGS];
	ixgb_cookie_t ixgb_cookie[IXGB_MAP_FRAGS];
	ddi_dma_cookie_t cookie;
	uint32_t icookie;
	uint32_t ncookies;
	uint32_t cookie_len;
	uint64_t cookie_addr;
	uint32_t slot;
	uint32_t frag_no;
	uint32_t frags;
	uint32_t start_index;
	uint32_t end_index;
	uint64_t bds;
	uint32_t dma_flag;
	uint32_t sum_flags;
	uint32_t lso_flags;
	uint32_t mss;
	uint32_t start_offset;
	uint32_t stuff_offset;
	boolean_t tcp_csum_load_cont;
	sw_sbd_t *ssbdp;
	ixgb_sbd_t *hw_sbd_p;
	char *txbuf;
	uint32_t ret;

	IXGB_TRACE(("ixgb_send_mapped($%p, $%p, %d)",
	    (void *)ixgbp, (void *)mp, totlen));

	/*
	 * Pre-scan the message chain, noting the total number of bytes,
	 * the number of fragments by pre-doing dma addr bind
	 * if the fragment is larger than IXGB_COPY_SIZE.
	 * This way has the following advantages:
	 * 1. Acquire the detailed information of resouce
	 *	need to send the message
	 *
	 * 2. If can not pre-apply enough resouce, fails  at once
	 *	and the driver will chose copy way to send out the
	 *	message
	 */
	frag_no = 0;
	frags = 0;
	bds = 0;
	sum_flags = 0;
	lso_flags = 0;
	mss = 0;

	/* check lso information */
	if (ixgbp->lso_enable)
		mac_lso_get(mp, &mss, &lso_flags);
	/* retrieve checksum information */
	if (ixgbp->tx_hw_chksum)
		mac_hcksum_get(mp, &start_offset, &stuff_offset, NULL, NULL,
		    &sum_flags);
	if (lso_flags & HW_LSO) {
		/*
		 * ixgb LSO is depend on the h/w cksum, so the LSO packet
		 * should be dropped if the h/w cksum is not supported.
		 */
		if (!(sum_flags & HCK_PARTIALCKSUM)) {
			ixgb_atomic_renounce(&srp->tx_free, rsv_bds);
			freemsg(mp);
			mutex_enter(srp->tx_lock);
			srp->tx_flow++;
			mutex_exit(srp->tx_lock);
			return (SEND_MAP_SUCCESS);
		}
		sum_flags |= HCK_IPV4_HDRCKSUM;
	}
	for (bp = mp; bp != NULL; bp = bp->b_cont) {
		if ((mblen = MBLKL(bp)) == 0)
			continue;
		/*
		 * If the frag size is less than IXGB_COPY_SIZE,
		 * use bcopy way.
		 */
		if (mblen <= IXGB_COPY_SIZE) {
			bds++;
			continue;
		}
		/*
		 * Now we're tring to do dma bind way.
		 */
		txhdl_queue = srp->txhdl_pop_queue;
		IXGB_QUEUE_POP(txhdl_queue, &hdl_item[frag_no]);
		if (hdl_item[frag_no] == NULL) {
			txhdl_queue = srp->txhdl_push_queue;
			IXGB_QUEUE_POP(txhdl_queue, &hdl_item[frag_no]);
			if (hdl_item[frag_no] == NULL) {
				IXGB_DEBUG(("ixgb_send_mapped: no tx hdl"));
				goto map_fail;
			}
		}
		mblk_hdl = hdl_item[frag_no]->item;
		/*
		 * Workaround for errata 27: LSO
		 * use a second bd to transimte the last 4 bytes
		 */
		if ((lso_flags & HW_LSO) && (bp->b_cont == NULL)) {
			mblen -= 4;
			bds++;
		}
		dma_flag = DMA_FLAG_STR | DDI_DMA_WRITE;
		ret = ddi_dma_addr_bind_handle(*mblk_hdl,
		    NULL, (caddr_t)bp->b_rptr, mblen,
		    dma_flag, DDI_DMA_DONTWAIT, NULL, &cookie,
		    &ncookies);
		/*
		 * If there can not map successfully, it is unnecessary
		 * sending the message by map way. Sending the message
		 * by copy way.
		 */
		if (ret != DDI_DMA_MAPPED ||
		    ncookies > IXGB_MAP_COOKIES) {
				IXGB_DEBUG(("ixgb_send_mapped: map tx hdl "
				    "failed: ret(%x) cookie(%x), ncookies(%x)",
				    ret, cookie.dmac_laddress, ncookies));
				goto map_fail;
		}

		/*
		 * Recode the necessary information for the next step
		 */
		for (icookie = 0; icookie < ncookies; icookie++) {
			cookie_len = (uint32_t)cookie.dmac_size;
			cookie_addr = cookie.dmac_laddress;
			ixgb_cookie[frag_no].cookie_len[icookie] = cookie_len;
			ixgb_cookie[frag_no].cookie_addr[icookie] = cookie_addr;
			if (icookie != ncookies - 1)
				ddi_dma_nextcookie(*mblk_hdl, &cookie);
		}
		bds += ncookies;
		ixgb_cookie[frag_no].ncookies = ncookies;
		frag_no++;
		frags++;
	}

	/*
	 * intel's 10G chipset uses a separate tx's BD called
	 * a context descriptor to set up support
	 * for partial checksumming w/o LSO.
	 * These change everytime the start_offset or stuff_offset
	 * changes, or when non checksummed packets arrive.
	 * For performance reasons, whenever possible we minimize
	 * sending out this setup context descriptor prior to the
	 * data descriptors.
	 *
	 *
	 * when the tx's context bd need to be loaded again, the driver
	 * must guarantee:
	 * 1.its context bd and its data bd must be conitunuous
	 * 2.the context bd can not been loaded
	 * until the previous data bd has been send out.
	 * Thus, the order should be like this:
	 * tcp context bd + tcp data bd + update tx's tail pointer
	 * + udp context bd + udp data bd + update tx's tail pointer.
	 *
	 *
	 * the following scenarios should not occur:
	 * 1. tcp context bd + udp context bd + tcp data bd + udp data bd +
	 * update tx's tail pointer.
	 * this will cause tx's tcp/udp checksum error.
	 *
	 * 2. tcp context bd + tcp data bd + udp context bd +
	 * update tx's tail pointer + udp data bd
	 * + update tx's tail pointer.
	 * this will cause tx's state machine enter the state of out of order
	 * and can not sent out packets.
	 */
	tcp_csum_load_cont = B_FALSE;
	mutex_enter(srp->tx_lock);
	if (ixgbp->tx_hw_chksum) {
		ret = ixgb_tx_lso_chksum(srp, mp, sum_flags, lso_flags, mss,
		    start_offset, stuff_offset);
		if (ret == LOAD_CONT) {
			tcp_csum_load_cont = B_TRUE;
			bds += 1;
		}
	}
	ASSERT(bds <= rsv_bds);

	/*
	 * Return extra reserved bds
	 */
	if (bds < rsv_bds)
		ixgb_atomic_renounce(&srp->tx_free, rsv_bds-bds);

	/*
	 * go straight to claiming our already-reserved places
	 * on the train!
	 */
	ixgb_send_claim(srp, bds, &start_index, &end_index);

	/*
	 * if there is no requiment to load a new checksum context,
	 * it is unnecessary to protect the following codes
	 */
	if (tcp_csum_load_cont) {
		ixgb_set_hw_cbd_flags(ixgbp,
		    &srp->sw_sbds[start_index], totlen);
		start_index = NEXT(start_index, srp->desc.nslots);
	}
	mutex_exit(srp->tx_lock);

	slot = start_index;
	frag_no = 0;
	for (bp = mp; bp != NULL; bp = bp->b_cont) {
		if ((mblen = MBLKL(bp)) == 0)
			continue;
		/*
		 * bcopy the less than IXGB_COPY_SIZE fragment
		 */
		if (mblen <= IXGB_COPY_SIZE) {
			ssbdp = &srp->sw_sbds[slot];
			hw_sbd_p = DMA_VPTR(ssbdp->desc);
			txbuf = DMA_VPTR(ssbdp->pbuf);
			bcopy(bp->b_rptr, txbuf, mblen);
			DMA_SYNC(ssbdp->pbuf, DDI_DMA_SYNC_FORDEV);
			hw_sbd_p->host_buf_addr =
			    ssbdp->pbuf.cookie.dmac_laddress;
			hw_sbd_p->len_cmd = mblen & IXGB_TBD_LEN_MASK;

			/*
			 * update flags in the send buffer descriptor
			 * for this frag.
			 */
			ixgb_set_hw_sbd_flags(hw_sbd_p, sum_flags,
			    lso_flags, B_FALSE);

			/*
			 * Hardware tcp/ip checksum flag is only valid
			 * in first data descriptor or LSO context
			 */
			if ((!(lso_flags & HW_LSO)) && (sum_flags != 0)) {
				sum_flags = 0;
			}
			slot = NEXT(slot, srp->desc.nslots);
			continue;
		}

		ncookies = ixgb_cookie[frag_no].ncookies;
		mblk_hdl = hdl_item[frag_no]->item;
		/*
		 * Workaround for errata 27: LSO
		 * use a second bd to transimte the last 4 bytes
		 */
		if ((lso_flags & HW_LSO) && (bp->b_cont == NULL)) {
			ASSERT(mblen > 4);
			mblen -= 4;
		}
		(void) ddi_dma_sync(*mblk_hdl, 0,
		    mblen, DDI_DMA_SYNC_FORDEV);

		/*
		 * Look through the cookie link list
		 * Send out each cookie.
		 */
		for (icookie = 0; icookie < ncookies; icookie++) {
			ssbdp = &srp->sw_sbds[slot];
			hw_sbd_p = DMA_VPTR(ssbdp->desc);
			cookie_addr =
			    ixgb_cookie[frag_no].cookie_addr[icookie];
			cookie_len =
			    ixgb_cookie[frag_no].cookie_len[icookie];
			hw_sbd_p->host_buf_addr = cookie_addr;
			hw_sbd_p->len_cmd = cookie_len & IXGB_TBD_LEN_MASK;
			/*
			 * we still have not reach the end
			 * of the chain. But we still update
			 * the hardware send buffer descriptor,
			 * except not setting the end flag.
			 */
			ixgb_set_hw_sbd_flags(hw_sbd_p, sum_flags,
			    lso_flags, B_FALSE);

			/*
			 * Hardware tcp/ip checksum flag is only valid
			 * in first data descriptor or LSO context
			 */
			if ((!(lso_flags & HW_LSO)) && (sum_flags != 0)) {
				sum_flags = 0;
			}
			slot = NEXT(slot, srp->desc.nslots);
		}
		frag_no++;
		/*
		 * Workaround for errata 27: LSO
		 * use a second bd to transimte the last 4 bytes
		 */
		if ((lso_flags & HW_LSO) && (bp->b_cont == NULL)) {
			ssbdp = &srp->sw_sbds[slot];
			hw_sbd_p = DMA_VPTR(ssbdp->desc);
			txbuf = DMA_VPTR(ssbdp->pbuf);
			bcopy((bp->b_rptr + mblen), txbuf, 4);
			DMA_SYNC(ssbdp->pbuf, DDI_DMA_SYNC_FORDEV);
			hw_sbd_p->host_buf_addr =
			    ssbdp->pbuf.cookie.dmac_laddress;
			hw_sbd_p->len_cmd = 4 & IXGB_TBD_LEN_MASK;

			/*
			 * update flags in the send buffer descriptor
			 * for this frag.
			 */
			ixgb_set_hw_sbd_flags(hw_sbd_p, sum_flags,
			    lso_flags, B_FALSE);
		}
	}

	/*
	 * We've reached the end of the chain;
	 * and we should set the end flag
	 * for the messages.
	 */
	hw_sbd_p->len_cmd |= IXGB_TBD_EOP;

	/*
	 * We should record the resouce for later freed
	 */
	if (frags != 0) {
		for (frag_no = 0; frag_no < frags; ++frag_no)
			ssbdp->mblk_hdl[frag_no] = hdl_item[frag_no];
		ssbdp->tx_recycle = ixgb_tx_recycle_mapped;
		ssbdp->mp = mp;
		ssbdp->frags = frags;
		/*
		 * The return status indicates that the message can not
		 * be freed right away, until we can make assure the message
		 * has been sent out sucessfully.
		 */
		return (SEND_MAP_SUCCESS);
	} else {
		/*
		 * We did bcopy on all the fragments, so just free this mp
		 */
		freemsg(mp);
		return (SEND_COPY_SUCCESS);
	}

map_fail:
	for (frag_no = 0; frag_no < frags; ++frag_no) {
		mblk_hdl = hdl_item[frag_no]->item;
		if (mblk_hdl != NULL) {
			(void) ddi_dma_unbind_handle(*mblk_hdl);
			IXGB_QUEUE_PUSH(txhdl_queue, hdl_item[frag_no]);
		}
	}
	/*
	 * We'll try copy way....
	 * But It's invalid to send large LSO packet through copy way
	 */
	if (totlen > ixgbp->max_frame) {
		ixgb_atomic_renounce(&srp->tx_free, rsv_bds);
		return (SEND_MAP_FAIL);
	}

	return (ixgb_send_copy(ixgbp, mp, totlen, rsv_bds));
}

/*
 * ixgb_send() - send one packet
 */
static boolean_t
ixgb_send(ixgb_t *ixgbp, mblk_t *mp)
{
	uint32_t fraglen;
	uint32_t mblen;
	uint32_t frags;
	uint32_t map_rsv_bds;
	uint32_t copy_rsv_bds;
	mblk_t *bp;
	mblk_t *nmp;
	send_ring_t *srp;
	enum send_status status;

	ASSERT(mp->b_next == NULL);
	srp = ixgbp->send;

	/*
	 * Check the number of the fragments and length of
	 * the message. If the message length is larger than
	 * IXGB_COPY_SIZE, choose the map way.
	 * Otherwise, choose the copy way.
	 */
	mblen = 0;
	frags = 0;
	map_rsv_bds = 1;
	/*
	 * Workaround for errata 27: LSO
	 * use a second bd to transimte the last 4 bytes
	 */
	if (ixgbp->lso_enable)
		map_rsv_bds++;

	for (bp = mp; bp != NULL; bp = bp->b_cont) {
		if ((fraglen = MBLKL(bp)) == 0)
			continue;
		frags++;
		mblen += fraglen;
		map_rsv_bds += (fraglen / (uint32_t)ixgbp->pagesize) + 2;
		if ((frags == (IXGB_MAP_FRAGS - 1)) &&
		    (bp->b_cont != NULL) &&
		    (bp->b_cont->b_cont != NULL)) {
			if ((nmp = msgpullup(bp->b_cont, -1)) != NULL) {
				freemsg(bp->b_cont);
				bp->b_cont = nmp;
			} else {
				IXGB_DEBUG(("ixgb_send: too many frags!"));
				ixgbp->statistics.sw_statistics.xmt_err++;
				freemsg(mp);
				return (B_TRUE);
			}
		}
	}

	if ((ixgbp->lso_enable == 0) && (mblen > ixgbp->max_frame)) {
		ixgbp->statistics.sw_statistics.xmt_err++;
		freemsg(mp);
		return (B_TRUE);
	}
	if ((srp->tx_free <= 16) || (srp->tx_free < map_rsv_bds))
		(void) ixgb_tx_recycle(ixgbp);

	copy_rsv_bds = 2;
	if ((mblen > IXGB_COPY_SIZE) &&
	    ixgb_atomic_reserve(&srp->tx_free, map_rsv_bds)) {
		status = ixgb_send_mapped(ixgbp, mp, mblen,
		    map_rsv_bds);
	} else if ((mblen <= ixgbp->max_frame) &&
	    ixgb_atomic_reserve(&srp->tx_free, copy_rsv_bds)) {
		status = ixgb_send_copy(ixgbp, mp, mblen, copy_rsv_bds);
	} else {
		ixgbp->resched_needed = B_TRUE;
		return (B_FALSE);
	}

	if (status == SEND_MAP_FAIL || status == SEND_COPY_FAIL) {
		/*
		 * The send routine failed :(  So report to MAC and
		 * we also have to ensure that mac_tx_update() is called
		 * sometime to resend this packet.
		 */
		ixgbp->resched_needed = B_TRUE;
		return (B_FALSE);
	}

	/*
	 * Update chip h/w transmit tail pointer...
	 */
	mutex_enter(srp->tx_lock);
	if (--srp->tx_flow == 0) {
		/*
		 * Bump the watchdog counter, thus guaranteeing
		 * that it's nonzero (watchdog activated).
		 */
		ixgbp->watchdog++;
		DMA_SYNC(srp->desc, DDI_DMA_SYNC_FORDEV);
		ixgb_reg_put32(ixgbp, IXGB_TDT, srp->tx_next);
	}
	mutex_exit(srp->tx_lock);

	return (B_TRUE);
}


/*
 * ixgb_m_tx() - send a chain of packets
 */
mblk_t *
ixgb_m_tx(void *arg, mblk_t *mp)
{
	ixgb_t *ixgbp = arg;
	mblk_t *next;

	IXGB_TRACE(("ixgb_m_tx($%p, $%p)", arg, (void *)mp));
	ASSERT(mp != NULL);

	if (ixgbp->ixgb_chip_state != IXGB_CHIP_RUNNING) {
		IXGB_DEBUG(("ixgb_m_tx: chip not running"));
		ixgbp->resched_needed = B_TRUE;
		return (mp);
	}

	rw_enter(ixgbp->errlock, RW_READER);
	while (mp != NULL) {
		next = mp->b_next;
		mp->b_next = NULL;

		if (!ixgb_send(ixgbp, mp)) {
			mp->b_next = next;
			break;
		}

		mp = next;
	}
	rw_exit(ixgbp->errlock);

	return (mp);
}

uint_t
ixgb_reschedule(caddr_t arg)
{
	ixgb_t *ixgbp;

	ixgbp = (ixgb_t *)arg;
	IXGB_TRACE(("ixgb_reschedule($%p)", (void *)ixgbp));

	/*
	 * when softintr is trigged, checking whether this
	 * is caused by our expected interrupt
	 */
	if (ixgbp->resched_needed) {
		ixgbp->resched_needed = B_FALSE;
		if (ixgbp->ixgb_mac_state == IXGB_MAC_STARTED)
			mac_tx_update(ixgbp->mh);
		ixgbp->resched_running = B_FALSE;
	}
	return (DDI_INTR_CLAIMED);
}
