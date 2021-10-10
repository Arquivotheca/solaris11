/*
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include "ixgb.h"

#undef	IXGB_DBG
#define	IXGB_DBG	IXGB_DBG_RECV	/* debug flag for this code	*/

/*
 * Callback code invoked from STREAMs when the recv data buffer is free
 * for recycling.
 */
void
ixgb_rx_recycle(caddr_t arg)
{
	ixgb_t *ixgbp;
	buff_ring_t *brp;
	dma_area_t *rx_buf;
	sw_rbd_t *free_srbdp;
	uint32_t slot_recy;

	rx_buf = (dma_area_t *)arg;
	ixgbp = (ixgb_t *)rx_buf->private;
	brp = ixgbp->buff;

	/*
	 * If ixgb_fini_buff_ring() or ixgb_fini_reve_ring() is called,
	 * this callback function will also be called when we try to free
	 * the mp in such situation, we needn't to do below desballoc(),
	 * otherwise, there'll be memory leak.
	 */
	if (ixgbp->ixgb_mac_state == IXGB_MAC_UNATTACHED)
		return;

	/*
	 * Recycle the data buffer again
	 * and fill them in free ring
	 */
	rx_buf->mp = desballoc(DMA_VPTR(*rx_buf),
	    ixgbp->buf_size, 0, &rx_buf->rx_recycle);
	if (rx_buf->mp == NULL) {
		ixgb_problem(ixgbp, "ixgb_rx_recycle: desballoc() failed!");
		return;
	}

	mutex_enter(brp->rc_lock);
	slot_recy = brp->rc_next;
	free_srbdp = &brp->free_srbds[slot_recy];
	ASSERT(free_srbdp->bufp == NULL);

	free_srbdp->bufp = rx_buf;
	brp->rc_next = NEXT(slot_recy, IXGB_RECV_SLOTS_BUFF);
	ixgb_atomic_renounce(&brp->rx_free, 1);
	if (brp->rx_bcopy && brp->rx_free == IXGB_RECV_SLOTS_BUFF)
		brp->rx_bcopy = B_FALSE;
	ASSERT(brp->rx_free <= IXGB_RECV_SLOTS_BUFF);

	mutex_exit(brp->rc_lock);
}

/*
 * Judge whether the status of hardware checksum is correct
 * Note: intel's 82597x only report the status of ip/tcp/udp hw sum,
 * do not report the value to the driver. So it can work well for
 * solaris full checksum. If support partial checksum, have to analyse
 * tcp/ip header by driver.
 */
static void ixgb_rx_checksum(mblk_t *mp, uint8_t hw_flags, uint8_t errs);
#pragma	inline(ixgb_rx_checksum)

static void
ixgb_rx_checksum(mblk_t *mp, uint8_t hw_flags, uint8_t errs)
{
	uint32_t pflags = 0;

	/*
	 * We can provide the following to the stack:
	 * a flag to say that the TCP/UDP checksum is correct
	 * a flag to say that the IPhdr checksum is correct
	 * the value of the TCP/UDP (pseudo-header+payload) checksum
	 * the value of the IPhdr checksum
	 * The currect HCK interface doesn't accommodate the last of
	 * these, but all the rest can be passed up via mac_hcksum_set().
	 *
	 * We can't in general distinguish between "a packet of a type
	 * recognised by the chip that had a bad checksum", and "a packet
	 * of a type not recognised by the chip".
	 * We only indicate that full checksum was performed by the chip
	 * sucessfully, not report the value to the upper.
	 */
	if (!(hw_flags & IXGB_RSTATUS_IXSM)) {
		if ((hw_flags & IXGB_RSTATUS_TCPCS) && !(errs & IXGB_RERR_TCPE))
			pflags |= HCK_FULLCKSUM_OK;
		if ((hw_flags & IXGB_RSTATUS_IPCS) && !(errs & IXGB_RERR_IPE))
			pflags |= HCK_IPV4_HDRCKSUM_OK;
		if (pflags != 0)
			mac_hcksum_set(mp, 0, 0, 0, 0, pflags);
	}
}

/*
 * Statistic the rx's error
 * and generate a log msg for these.
 * Note:
 * RXE, Parity Error, Symbo error, CRC error
 * have been recored by intel's 82507ex hardware
 * statistics part (ixgb_statistics). So it is uncessary to record them by
 * driver in this place.
 */
static boolean_t ixgb_rxerr_log(ixgb_t *ixgbp, uint8_t status, uint8_t err);
#pragma	inline(ixgb_rxerr_log)

static boolean_t
ixgb_rxerr_log(ixgb_t *ixgbp, uint8_t status, uint8_t err)
{
	ixgb_sw_statistics_t *sw_stp;

	sw_stp = &ixgbp->statistics.sw_statistics;

	if (!(status & IXGB_RSTATUS_IXSM)) {
		if ((status & IXGB_RSTATUS_IPCS) && (err & IXGB_RERR_IPE))
			sw_stp->ip_hwsum_err++;
		if ((status & IXGB_RSTATUS_TCPCS) && (err & IXGB_RERR_TCPE))
			sw_stp->tcp_hwsum_err++;
	}

	if (err & IXGB_RERR_MASK) {
		sw_stp->rcv_err++;
		IXGB_DEBUG(("ixgb receive err = %x", err));
		return (B_TRUE);
	}

	return (B_FALSE);
}

static boolean_t ixgb_rx_refill(recv_ring_t *rrp, buff_ring_t *brp,
    uint32_t slot);
#pragma	inline(ixgb_rx_refill)

static boolean_t
ixgb_rx_refill(recv_ring_t *rrp, buff_ring_t *brp, uint32_t slot)
{
	dma_area_t *free_buf;
	ixgb_rbd_t *hw_rbd_p;
	sw_rbd_t *srbdp;
	uint32_t free_slot;

	srbdp = &rrp->sw_rbds[slot];
	hw_rbd_p = &rrp->rx_ring[slot];
	free_slot = brp->rfree_next;
	free_buf = brp->free_srbds[free_slot].bufp;
	if (free_buf != NULL) {
		srbdp->bufp = free_buf;
		brp->free_srbds[free_slot].bufp = NULL;
		hw_rbd_p->host_buf_addr = IXGB_HEADROOM +
		    free_buf->cookie.dmac_laddress;
		brp->rfree_next = NEXT(free_slot, IXGB_RECV_SLOTS_BUFF);
		return (B_TRUE);
	} else {
		/*
		 * This situation shouldn't happen
		 */
		brp->rx_bcopy = B_TRUE;
		return (B_FALSE);
	}
}

static mblk_t *ixgb_receive_bind(ixgb_t *ixgbp, uint32_t slot,
    uint32_t packet_len);
#pragma	inline(ixgb_receive_bind)

static mblk_t *
ixgb_receive_bind(ixgb_t *ixgbp, uint32_t slot,
    uint32_t packet_len)
{
	recv_ring_t *rrp;
	buff_ring_t *brp;
	sw_rbd_t *srbdp;
	mblk_t *mp;

	rrp = ixgbp->recv;
	brp = ixgbp->buff;
	srbdp = &rrp->sw_rbds[slot];

	DMA_SYNC(*srbdp->bufp, DDI_DMA_SYNC_FORKERNEL);
	mp = srbdp->bufp->mp;
	mp->b_rptr += IXGB_HEADROOM;
	mp->b_wptr = mp->b_rptr + packet_len;
	mp->b_next = mp->b_cont = NULL;

	/*
	 * Refill the current receive bd buffer
	 * if fails, will just keep the mp.
	 */
	if (!ixgb_rx_refill(rrp, brp, slot)) {
		ixgb_atomic_renounce(&brp->rx_free, 1);
		ixgb_problem(ixgbp, "ixgb_receive_bind: slot refill failed!");
		return (NULL);
	}

	return (mp);
}

static mblk_t *ixgb_receive_copy(ixgb_t *ixgbp, uint32_t slot,
    uint32_t packet_len);
#pragma	inline(ixgb_receive_copy)

static mblk_t *
ixgb_receive_copy(ixgb_t *ixgbp, uint32_t slot, uint32_t packet_len)
{
	recv_ring_t *rrp;
	sw_rbd_t *srbdp;
	uchar_t *dp;
	mblk_t *mp;
	uint8_t *rx_ptr;

	rrp = ixgbp->recv;
	srbdp = &rrp->sw_rbds[slot];
	DMA_SYNC(*srbdp->bufp, DDI_DMA_SYNC_FORKERNEL);

	/*
	 * Allocate buffer to receive this good packet
	 */
	mp = allocb(packet_len + IXGB_HEADROOM, 0);
	if (mp == NULL) {
		IXGB_DEBUG(("ixgb_receive_copy: allocate buffer fail"));
		return (NULL);
	}

	/*
	 * Copy the data found into the new cluster
	 */
	rx_ptr = DMA_VPTR(*srbdp->bufp);
	dp = mp->b_rptr = mp->b_rptr + IXGB_HEADROOM;
	bcopy(rx_ptr + IXGB_HEADROOM, dp, packet_len);
	mp->b_wptr = dp + packet_len;

	return (mp);
}

/*
 * Accept the packets received in rx ring.
 *
 * Returns a chain of mblks containing the received data, to be
 * passed up to mac_rx().
 * The routine returns only when a complete scan has been performed
 * without finding any packets to receive.
 * This function must SET the OWN bit of BD to indicate the packets
 * it has accepted from the ring.
 */
static mblk_t *
ixgb_receive_ring(ixgb_t *ixgbp, recv_ring_t *rrp)
{
	uint32_t slot;
	uint32_t slot_tail;
	uint32_t rx_head;
	buff_ring_t *brp;
	ixgb_rbd_t *hw_rbd_p;
	mblk_t *head;
	mblk_t **tail;
	mblk_t *mp;
	uint32_t packet_len;
	uint32_t minsize;
	uint32_t maxsize;
	uint8_t status;
	uint8_t err;
	boolean_t flag_hw_err;
	boolean_t flag_len_err;

	ASSERT(mutex_owned(rrp->rx_lock));
	brp = ixgbp->buff;
	minsize = ETHERMIN;
	maxsize = ixgbp->max_frame;
	flag_hw_err = B_FALSE;
	flag_len_err = B_FALSE;

	head = NULL;
	tail = &head;
	rx_head = ixgb_reg_get32(ixgbp, IXGB_RDH);
	/*
	 * Sync (all) the receive ring descriptors
	 * before accepting the packets they describe
	 */
	DMA_SYNC(rrp->desc, DDI_DMA_SYNC_FORKERNEL);
	slot = rrp->rx_next;
	slot_tail = rrp->rx_tail;
	hw_rbd_p = &rrp->rx_ring[slot];
	while (hw_rbd_p->status & IXGB_RSTATUS_DD) {
		/*
		 * If hardware has found the errors, but the error
		 * is tcp checksum error, or IP checksum error,
		 * does not decard the packet, and let upper recompute
		 * the checksum again.
		 * Or discard the packet.
		 */
		status = hw_rbd_p->status;
		err = hw_rbd_p->errors;
		if (err != 0)
			flag_hw_err = ixgb_rxerr_log(ixgbp, status, err);

		packet_len = hw_rbd_p->length;
		flag_len_err = (packet_len > maxsize) || (packet_len < minsize);

		if (flag_hw_err || flag_len_err ||
		    !(hw_rbd_p->status & IXGB_RSTATUS_EOP)) {
			IXGB_DEBUG(("ixgb_receive_ring: receive fail"));
			ixgbp->ixgb_chip_state = IXGB_CHIP_ERROR;
			goto rx_err;
		}

		if (packet_len <= IXGB_COPY_SIZE || brp->rx_bcopy ||
		    !ixgb_atomic_reserve(&brp->rx_free, 1))
			mp = ixgb_receive_copy(ixgbp, slot, packet_len);
		else
			mp = ixgb_receive_bind(ixgbp, slot, packet_len);
		if (mp != NULL) {
			/* Check h/w checksum offload status */
			if (ixgbp->rx_hw_chksum)
				ixgb_rx_checksum(mp, status, err);
			*tail = mp;
			tail = &mp->b_next;
		}

rx_err:
		/*
		 * Clear RBD flags
		 */
		hw_rbd_p->status = 0;
		hw_rbd_p->errors = 0;
		hw_rbd_p->special = 0;
		hw_rbd_p->length = 0;

		slot = NEXT(slot, rrp->desc.nslots);
		slot_tail = NEXT(slot_tail, rrp->desc.nslots);
		if (slot == rx_head ||
		    ixgbp->ixgb_chip_state != IXGB_CHIP_RUNNING)
			break;
		hw_rbd_p = &rrp->rx_ring[slot];
	}
	rrp->rx_next = slot;
	rrp->rx_tail = slot_tail;
	DMA_SYNC(rrp->desc, DDI_DMA_SYNC_FORDEV);
	ixgb_reg_put32(ixgbp, IXGB_RDT, rrp->rx_tail);
	return (head);
}


/*
 * Receive all ready packets.
 */
void
ixgb_receive(ixgb_t *ixgbp)
{
	mblk_t *mp;
	recv_ring_t *rrp;

	rrp = ixgbp->recv;
	mutex_enter(rrp->rx_lock);
	mp = ixgb_receive_ring(ixgbp, rrp);
	mutex_exit(rrp->rx_lock);

	if (mp != NULL)
		mac_rx(ixgbp->mh, NULL, mp);
}
