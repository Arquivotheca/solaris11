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
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/types.h>
#include <sys/kmem.h>
#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/ksynch.h>
#include <sys/pattr.h>			/* HCK_* */
#include <inet/ip.h>			/* ipha_t */
#include <inet/tcp.h>			/* tcph_t */
#include <sys/strsun.h>			/* MBLKL */

#include <sys/ib/clients/eoib/eib_impl.h>

/*
 * Declarations private to this file
 */
static int eib_data_setup_cqs(eib_t *, eib_vnic_t *);
static int eib_data_setup_ud_channel(eib_t *, eib_vnic_t *);
static int eib_data_update_encap_cksum(uint32_t *,
    eib_nw_hdrinfo_t *);
static void eib_data_parse_nw_hdrs(mblk_t *, eib_ether_hdr_t *,
    eib_nw_hdrinfo_t *);
static void eib_data_setup_lso(eib_wqe_t *, mblk_t *, uint32_t,
    eib_nw_hdrinfo_t *);
static int eib_data_prepare_sgl(eib_vnic_t *, eib_wqe_t *, mblk_t *);
static int eib_data_is_mcast_pkt_ok(eib_vnic_t *, uint8_t *, uint64_t *,
    uint64_t *);
static void eib_data_rx_comp_intr(ibt_cq_hdl_t, void *);
static void eib_data_tx_comp_intr(ibt_cq_hdl_t, void *);
static mblk_t *eib_data_rx_comp(eib_vnic_t *, eib_wqe_t *, ibt_wc_t *);
static void eib_data_tx_comp(eib_vnic_t *, eib_wqe_t *);
static void eib_data_err_comp(eib_vnic_t *, eib_wqe_t *, ibt_wc_t *);
static void eib_rb_data_setup_cqs(eib_t *, eib_vnic_t *);
static void eib_rb_data_setup_ud_channel(eib_t *, eib_vnic_t *);

int
eib_data_create_qp(eib_t *ss, eib_vnic_t *vnic, int *err)
{
	eib_chan_t *chan = NULL;

	/*
	 * Allocate a eib_chan_t to store stuff about this vnic's data qp
	 * and initialize it with default admin qp pkey parameters. We'll
	 * re-associate this with the pkey we receive from the gw once we
	 * receive the login ack.
	 */
	vnic->vn_data_chan = eib_chan_init();

	chan = vnic->vn_data_chan;
	chan->ch_pkey = ss->ei_admin_chan->ch_pkey;
	chan->ch_pkey_ix = ss->ei_admin_chan->ch_pkey_ix;

	/*
	 * Setup tx/rx CQs and completion handlers
	 */
	if (eib_data_setup_cqs(ss, vnic) != EIB_E_SUCCESS) {
		EIB_DPRINTF_ERR(ss->ei_instance, "eib_data_create_qp: "
		    "eib_data_setup_cqs(vn_inst=0x%x) failed",
		    vnic->vn_instance);
		*err = ENOMEM;
		goto data_create_qp_fail;
	}

	/*
	 * Setup UD channel
	 */
	if (eib_data_setup_ud_channel(ss, vnic) != EIB_E_SUCCESS) {
		EIB_DPRINTF_ERR(ss->ei_instance, "eib_data_create_qp: "
		    "eib_data_setup_ud_channel(vn_inst=0x%x) failed",
		    vnic->vn_instance);
		*err = ENOMEM;
		goto data_create_qp_fail;
	}

	return (EIB_E_SUCCESS);

data_create_qp_fail:
	eib_rb_data_create_qp(ss, vnic);
	return (EIB_E_FAILURE);
}

/*ARGSUSED*/
uint_t
eib_data_rx_comp_handler(caddr_t arg1, caddr_t arg2)
{
	eib_vnic_t *vnic = (eib_vnic_t *)(void *)arg1;
	eib_t *ss = vnic->vn_ss;
	eib_chan_t *chan = vnic->vn_data_chan;
	eib_stats_t *stats = ss->ei_stats;
	eib_rx_ring_t *rx_ring;
	ibt_wc_t *wc;
	eib_wqe_t *wqe;
	mblk_t *mp = NULL;
	mblk_t *head = NULL;
	mblk_t *tail = NULL;
	mblk_t *mblks_in_ring = NULL;
	ibt_status_t ret;
	uint_t polled;
	uint_t rbytes;
	uint_t ipkts;
	uint_t wqes_with_nw = 0;
	uint_t num_wc;
	uint_t rr_polling;
	int i;

	/*
	 * Re-arm the rx notification callback before we start polling
	 * the completion queue.  There's nothing much we can do if the
	 * enable_cq_notify fails - we issue a warning and move on.
	 */
	ret = ibt_enable_cq_notify(chan->ch_rcv_cq_hdl, IBT_NEXT_COMPLETION);
	if (ret != IBT_SUCCESS) {
		EIB_DPRINTF_WARN(ss->ei_instance, "eib_data_rx_comp_handler: "
		    "ibt_enable_cq_notify() failed, ret=%d", ret);
	}

	/*
	 * We don't want to be stuck in receive processing for too long without
	 * giving others a chance.
	 */
	num_wc = (chan->ch_rcv_cq_sz < EIB_MAX_RX_PKTS_ONINTR) ?
	    chan->ch_rcv_cq_sz : EIB_MAX_RX_PKTS_ONINTR;

	rbytes = ipkts = 0;
	head = tail = NULL;
	wqes_with_nw = 0;

	/*
	 * Handle rx completions
	 */
	while ((ret = ibt_poll_cq(chan->ch_rcv_cq_hdl, chan->ch_rcv_wc,
	    num_wc, &polled)) == IBT_SUCCESS) {

		for (wc = chan->ch_rcv_wc, i = 0; i < polled; i++, wc++) {
			wqe = (eib_wqe_t *)(uintptr_t)wc->wc_id;

			ASSERT(EIB_WQE_TYPE(wqe->qe_info) == EIB_WQE_RX);

			if (wc->wc_status != IBT_WC_SUCCESS) {
				EIB_INCR_COUNTER(&stats->st_ierrors);
				eib_data_err_comp(vnic, wqe, wc);
				continue;
			}
			rbytes += wc->wc_bytes_xfer;

			ipkts++;
			if ((mp = eib_data_rx_comp(vnic, wqe, wc)) == NULL)
				continue;

			if (wqe->qe_info & EIB_WQE_FLG_WITH_NW)
				wqes_with_nw++;

			if (head)
				tail->b_next = mp;
			else
				head = mp;
			tail = mp;
		}

		num_wc -= polled;

		/*
		 * If we have processed too many packets in one attempt, we'll
		 * have to come back here later.
		 */
		if ((ipkts >= EIB_MAX_RX_PKTS_ONINTR) || (num_wc == 0)) {
			(void) ddi_intr_trigger_softint(vnic->vn_data_rx_si_hdl,
			    NULL);
			break;
		}
	}

	/*
	 * We reduce the number of atomic updates to key statistics
	 * by pooling them here, once per ibt_poll_cq().  The accuracy
	 * and consistency of the published statistics within a cq
	 * polling cycle will be compromised a little bit, but that
	 * should be ok, given that we probably gain a little bit by
	 * not having to do these atomic operations per packet.
	 */
	EIB_UPDT_COUNTER(&stats->st_rbytes, rbytes);
	EIB_UPDT_COUNTER(&stats->st_ipkts, ipkts);

	/*
	 * If we are in poll-mode, queue up whatever we've collected
	 * in this call into the rx ring.  Otherwise send up to the
	 * mac layer whatever is there in the rx ring already, along
	 * with whatever we've collected in this call.  In both cases,
	 * treat it as if the ones we've collected in this call are
	 * now with the nw layer (i.e. update "num_with_nw" count).
	 */
	EIB_UPDT_COUNTER_32(&ss->ei_rx->wp_num_with_nw, wqes_with_nw);

	rx_ring = ss->ei_rx_ring[0];

	mutex_enter(&rx_ring->rr_lock);
	if ((rr_polling = rx_ring->rr_polling) == 1) {
		if (rx_ring->rr_mp)
			rx_ring->rr_mp_tail->b_next = head;
		else
			rx_ring->rr_mp = head;
		rx_ring->rr_mp_tail = tail;
	} else {
		mblks_in_ring = rx_ring->rr_mp;
		rx_ring->rr_mp = NULL;
		rx_ring->rr_mp_tail = NULL;
	}
	mutex_exit(&rx_ring->rr_lock);

	if (rr_polling == 0) {
		if (mblks_in_ring)
			mac_rx(ss->ei_mac_hdl, NULL, mblks_in_ring);
		if (head)
			mac_rx(ss->ei_mac_hdl, NULL, head);
	}

	if ((chan->ch_tear_down) && (ret == IBT_CQ_EMPTY)) {
		mutex_enter(&chan->ch_rcqstate_lock);
		if (chan->ch_rcqstate_wait) {
			chan->ch_rcqstate_empty = B_TRUE;
			cv_signal(&chan->ch_rcqstate_cv);
		}
		mutex_exit(&chan->ch_rcqstate_lock);
	}

	return (DDI_INTR_CLAIMED);
}

/*ARGSUSED*/
uint_t
eib_data_tx_comp_handler(caddr_t arg1, caddr_t arg2)
{
	eib_vnic_t *vnic = (eib_vnic_t *)(void *)arg1;
	eib_t *ss = vnic->vn_ss;
	eib_chan_t *chan = vnic->vn_data_chan;
	eib_stats_t *stats = ss->ei_stats;
	ibt_wc_t *wc;
	eib_wqe_t *wqe;
	ibt_status_t ret;
	uint_t polled;
	int i;

	/*
	 * Re-arm the tx notification callback before we start polling
	 * the completion queue.  There's nothing much we can do if the
	 * enable_cq_notify fails - we issue a warning and move on.
	 */
	ret = ibt_enable_cq_notify(chan->ch_cq_hdl, IBT_NEXT_COMPLETION);
	if (ret != IBT_SUCCESS) {
		EIB_DPRINTF_WARN(ss->ei_instance, "eib_data_tx_comp_handler: "
		    "ibt_enable_cq_notify() failed, ret=%d", ret);
	}

	/*
	 * Handle tx completions
	 */
	while ((ret = ibt_poll_cq(chan->ch_cq_hdl, chan->ch_wc, chan->ch_cq_sz,
	    &polled)) == IBT_SUCCESS) {
		for (wc = chan->ch_wc, i = 0; i < polled; i++, wc++) {
			wqe = (eib_wqe_t *)(uintptr_t)wc->wc_id;

			ASSERT(EIB_WQE_TYPE(wqe->qe_info) == EIB_WQE_TX);

			if (wc->wc_status != IBT_WC_SUCCESS) {
				EIB_INCR_COUNTER(&stats->st_oerrors);
				eib_data_err_comp(vnic, wqe, wc);
			} else {
				eib_data_tx_comp(vnic, wqe);
			}
		}
	}

	if ((chan->ch_tear_down) && (ret == IBT_CQ_EMPTY)) {
		mutex_enter(&chan->ch_cqstate_lock);
		if (chan->ch_cqstate_wait) {
			chan->ch_cqstate_empty = B_TRUE;
			cv_signal(&chan->ch_cqstate_cv);
		}
		mutex_exit(&chan->ch_cqstate_lock);
	}

	return (DDI_INTR_CLAIMED);
}

void
eib_data_rx_recycle(caddr_t arg)
{
	eib_wqe_t *rwqe = (eib_wqe_t *)(void *)arg;
	eib_t *ss = rwqe->qe_pool->wp_ss;
	uint_t qe_info;

	qe_info = rwqe->qe_info;
	rwqe->qe_info &= (~EIB_WQEFLGS_MASK);
	rwqe->qe_mp = NULL;

	/*
	 * We come here from four places via the freemsg() callback:
	 *
	 * (a)	from the nw layer if the rx mblk we handed to
	 *	it has been done with
	 * (b)	from eib_data_rx_comp() if the rx completion
	 *	processing discovers that the received EoIB
	 *	packet has a problem
	 * (c)	from eib_data_err_comp() if we're tearing down
	 *	this channel and
	 * (d)	from eib_rb_setup_srqs() when we're unplumbing
	 *	the eoib instance
	 *
	 * For all cases, we repost the wqe to the SRQ unless the
	 * EIB_WQE_FLG_RET_TO_POOL flag is set.  If the wqe is
	 * being returned by the network layer (case (a) above),
	 * we also decrement the number of wqes now with the
	 * network layer.
	 */
	if (qe_info & EIB_WQE_FLG_RET_TO_POOL)
		eib_rsrc_return_rwqe(ss, rwqe);
	else {
		eib_chan_post_rwqe(ss, ss->ei_data_srq, rwqe);
		if (qe_info & EIB_WQE_FLG_WITH_NW)
			EIB_DECR_COUNTER_32(&rwqe->qe_pool->wp_num_with_nw);
	}
}

void
eib_data_post_tx(eib_vnic_t *vnic, eib_wqe_t *swqe)
{
	eib_chan_t *chan = vnic->vn_data_chan;
	eib_t *ss = vnic->vn_ss;
	eib_stats_t *stats = vnic->vn_ss->ei_stats;
	eib_chan_txq_t *txq;
	ibt_send_wr_t wrs[EIB_MAX_POST_MULTIPLE];
	eib_wqe_t *wqes[EIB_MAX_POST_MULTIPLE];
	eib_wqe_t *elem;
	ibt_status_t ret;
	uint_t n_wrs;
	uint_t n_posted;
	uint_t total_failed = 0;
	uint_t n_failed = 0;
	uint_t i;

	/*
	 * Hash into the tx queue for this connection and add it to the
	 * list of tx wrs to post.
	 */
	ASSERT(swqe->qe_hash < EIB_NUM_TX_QUEUES);
	txq = &chan->ch_txq[swqe->qe_hash];

	mutex_enter(&txq->tx_lock);

	swqe->qe_nxt_post = NULL;
	if (txq->tx) {
		txq->tx_tail->qe_nxt_post = swqe;
	} else {
		txq->tx = swqe;
	}
	txq->tx_tail = swqe;

	/*
	 * If someone's already posting tx wqes for this queue, let
	 * them post ours as well.
	 */
	if (txq->tx_busy == B_TRUE) {
		mutex_exit(&txq->tx_lock);
		return;
	}
	txq->tx_busy = B_TRUE;

	while (txq->tx) {
		/*
		 * Post EIB_MAX_POST_MULTIPLE wrs at a time
		 */
		for (n_wrs = 0, elem = txq->tx;
		    (elem) && (n_wrs < EIB_MAX_POST_MULTIPLE);
		    elem = elem->qe_nxt_post, n_wrs++) {
			wqes[n_wrs] = elem;
			wrs[n_wrs] = (elem->qe_wr).send;
		}
		txq->tx = elem;
		if (elem == NULL) {
			txq->tx_tail = NULL;
		}
		mutex_exit(&txq->tx_lock);

		ASSERT(n_wrs != 0);

		/*
		 * If multiple wrs posting fails for some reason, we'll try
		 * posting the unposted ones one by one.  If even that fails,
		 * we'll release any mappings/buffers/mblks associated with
		 * this wqe and return it to the pool.
		 */
		n_posted = n_failed = 0;
		ret = ibt_post_send(chan->ch_chan, wrs, n_wrs, &n_posted);
		if (ret != IBT_SUCCESS) {
			EIB_DPRINTF_WARN(ss->ei_instance, "eib_data_post_tx: "
			    "ibt_post_send(n_wrs=0x%lx, n_posted=0x%lx) "
			    "failed, ret=%d", n_wrs, n_posted, ret);

			for (i = n_posted; i < n_wrs; i++) {
				ret = ibt_post_send(chan->ch_chan, &wrs[i],
				    1, NULL);
				if (ret != IBT_SUCCESS) {
					n_failed++;
					eib_data_tx_comp(vnic, wqes[i]);

					EIB_DPRINTF_WARN(ss->ei_instance,
					    "eib_data_post_tx: "
					    "ibt_post_send(n_wrs=1) failed, "
					    "ret=%d", ret);
				}
			}
		}

		total_failed += n_failed;
		mutex_enter(&txq->tx_lock);
	}

	txq->tx_busy = B_FALSE;
	mutex_exit(&txq->tx_lock);

	/*
	 * If we failed to post something, update error stats
	 */
	if (total_failed) {
		EIB_UPDT_COUNTER(&stats->st_oerrors, total_failed);
	}
}

void
eib_data_parse_ether_hdr(mblk_t *mp, eib_ether_hdr_t *evh)
{
	struct ether_vlan_header *vl_hdr;
	struct ether_header *hdr;

	/*
	 * Assume that the ether header (with or without vlan tag) is
	 * contained in one fragment
	 */
	hdr = (struct ether_header *)(void *)mp->b_rptr;
	vl_hdr = (struct ether_vlan_header *)(void *)mp->b_rptr;

	evh->eh_ether_type = ntohs(hdr->ether_type);
	if (evh->eh_ether_type != ETHERTYPE_VLAN) {
		evh->eh_tagless = 1;
		evh->eh_vlan = 0;
		ether_copy((void *)hdr->ether_dhost.ether_addr_octet,
		    (void *)evh->eh_dmac);
		ether_copy((void *)hdr->ether_shost.ether_addr_octet,
		    (void *)evh->eh_smac);
	} else {
		evh->eh_ether_type = ntohs(vl_hdr->ether_type);
		evh->eh_tagless = 0;
		evh->eh_vlan = VLAN_ID(ntohs(vl_hdr->ether_tci));
		ether_copy((void *)vl_hdr->ether_dhost.ether_addr_octet,
		    (void *)evh->eh_dmac);
		ether_copy((void *)vl_hdr->ether_shost.ether_addr_octet,
		    (void *)evh->eh_smac);
	}
}

int
eib_data_lookup_vnic(eib_t *ss, uint8_t *mac, uint16_t vlan, eib_vnic_t **vnicp,
    boolean_t *failed)
{
	eib_vnic_t *vnic;
	eib_vnic_req_t *vrq;
	uint8_t *vn_mac;
	uint16_t vn_vlan;
	uint64_t av;
	int inst = 0;

	if (mac == NULL)
		return (EIB_E_FAILURE);

	/*
	 * For now, a simple search (but only what we've allocated). Note that
	 * if we're in the process of creating a vnic, the instance might've
	 * been allocated, but the vnic entry would be NULL.
	 */
	rw_enter(&ss->ei_vnic_lock, RW_READER);
	av = ss->ei_active_vnics;
	while ((inst = EIB_FIND_LSB_SET(av)) != -1) {
		if ((vnic = ss->ei_vnic[inst]) != NULL) {
			vn_mac = vnic->vn_login_data.ld_assigned_mac;
			vn_vlan = vnic->vn_login_data.ld_assigned_vlan;

			if ((vn_vlan == vlan) &&
			    (bcmp(vn_mac, mac, ETHERADDRL) == 0)) {
				if (vnicp) {
					*vnicp = vnic;
				}
				rw_exit(&ss->ei_vnic_lock);
				return (EIB_E_SUCCESS);
			}
		}

		av &= (~((uint64_t)1 << inst));
	}
	rw_exit(&ss->ei_vnic_lock);

	/*
	 * If we haven't been able to locate a vnic for this {mac,vlan} tuple,
	 * see if we've already failed a creation request for this vnic, and
	 * return that information.
	 */
	if (failed) {
		mutex_enter(&ss->ei_vnic_req_lock);
		*failed = B_FALSE;
		for (vrq = ss->ei_failed_vnic_req; vrq; vrq = vrq->vr_next) {
			if ((vrq->vr_vlan == vlan) &&
			    (bcmp(vrq->vr_mac, mac, ETHERADDRL) == 0)) {
				*failed = B_TRUE;
			}
		}
		mutex_exit(&ss->ei_vnic_req_lock);
	}

	return (EIB_E_FAILURE);
}

int
eib_data_prepare_frame(eib_vnic_t *vnic, eib_wqe_t *swqe, mblk_t *mp,
    eib_ether_hdr_t *evh)
{
	ipha_t *ipha;
	uint32_t *encap_hdr;
	uint32_t mss;
	uint32_t lsoflags;
	uint32_t hckflags;
	uint32_t nds;
	uint8_t ip_hdr_offset;
	uint8_t tcp_udp_hdr_offset;
	uint8_t tcp_udp_flag;
	eib_nw_hdrinfo_t nwh;

	/*
	 * Parse the network headers and compute the ethernet, ip,
	 * tcp/udp header lengths
	 */
	eib_data_parse_nw_hdrs(mp, evh, &nwh);
	ipha = nwh.nh_ipha;

	/*
	 * Setup flags for hardware checksumming if necessary.
	 *
	 * Note that even though we look for the "HCK_FULLCKSUM"
	 * flag, currently ConnectX/ConnectX-2 supports only partial
	 * checksumming, and not true full-checksumming the way it
	 * supports it for IPoIB.
	 *
	 * However, even the partial checksumming supported by
	 * ConnectX-2 is different from the support that Solaris
	 * provides via the HCK_PARTIALCKSUM flag, so we pretend
	 * (to gldv3 framework) that we support true full-checksumming
	 * and within the eoib driver prepare and pass the pseudo
	 * checksum the way the ConnectX-2 fw/hw expects it.
	 */
	mac_hcksum_get(mp, NULL, NULL, NULL, NULL, &hckflags);
	if ((hckflags & HCK_FULLCKSUM) == HCK_FULLCKSUM) {
		encap_hdr = (uint32_t *)(void *)swqe->qe_payload_hdr;
		if (eib_data_update_encap_cksum(encap_hdr, &nwh) ==
		    EIB_E_SUCCESS) {
			swqe->qe_wr.send.wr_flags |= IBT_WR_EXTENDED_FLAGS;
			swqe->qe_wr.send.wr_ext_flags |=
			    IBT_WR_SEND_PARTIAL_CKSUM;
		}
	}

	/*
	 * The swqe defaults are set to use the regular ud work request
	 * member and the IBT_WRC_SEND opcode, so we don't need to do
	 * anything here if this isn't an LSO packet.
	 */
	mac_lso_get(mp, &mss, &lsoflags);
	if ((lsoflags & HW_LSO) == HW_LSO)
		eib_data_setup_lso(swqe, mp, mss, &nwh);

	/*
	 * Prepare the SGL
	 */
	if (eib_data_prepare_sgl(vnic, swqe, mp) != 0)
		return (EIB_E_FAILURE);

	/*
	 * Now that the work request in the swqe has been prepared,
	 * update the wr_nds field to pass the ip/tcp_udp offsets to
	 * the HCA driver if necessary.
	 */
	if ((swqe->qe_wr.send.wr_flags & IBT_WR_EXTENDED_FLAGS) &&
	    (swqe->qe_wr.send.wr_ext_flags & IBT_WR_SEND_PARTIAL_CKSUM)) {

		ip_hdr_offset = (EIB_ENCAP_HDR_SZ + nwh.nh_eth_hdr_len) >> 1;
		tcp_udp_hdr_offset = nwh.nh_ip_hdr_len >> 2;
		tcp_udp_flag = (ipha->ipha_protocol == IPPROTO_TCP) ?
		    IBT_EIB_TCP : IBT_EIB_UDP;

		nds = swqe->qe_wr.send.wr_nds;
		ASSERT(nds <= EIB_MAX_NDS);

		nds &= IBT_EIB_NDS_VAL_MASK;
		nds |= (tcp_udp_flag << IBT_EIB_TCPUDP_FLAG_SHIFT);
		nds |= (tcp_udp_hdr_offset << IBT_EIB_TCPUDP_OFFSET_SHIFT);
		nds |= (ip_hdr_offset << IBT_EIB_IP_OFFSET_SHIFT);

		swqe->qe_wr.send.wr_nds = nds;
	}

	swqe->qe_hash = nwh.nh_hash;
	swqe->qe_mp = mp;

	return (EIB_E_SUCCESS);
}

void
eib_rb_data_create_qp(eib_t *ss, eib_vnic_t *vnic)
{
	eib_rb_data_setup_ud_channel(ss, vnic);

	eib_rb_data_setup_cqs(ss, vnic);

	eib_chan_fini(vnic->vn_data_chan);
	vnic->vn_data_chan = NULL;
}

static int
eib_data_setup_cqs(eib_t *ss, eib_vnic_t *vnic)
{
	eib_chan_t *chan = vnic->vn_data_chan;
	ibt_cq_attr_t cq_attr;
	ibt_status_t ret;
	uint_t snd_sz;
	uint_t rcv_sz;
	int rv;

	/*
	 * Allocate send completion queue. Note that we've already verified
	 * that cp_max_swqe and cp_max_rwqe meet the max cq size requirements
	 * of the hca.
	 */
	cq_attr.cq_sched = NULL;
	cq_attr.cq_flags = IBT_CQ_NO_FLAGS;
	cq_attr.cq_size = ss->ei_caps->cp_max_swqe + 1;

	ret = ibt_alloc_cq(ss->ei_hca_hdl, &cq_attr, &chan->ch_cq_hdl, &snd_sz);
	if (ret != IBT_SUCCESS) {
		EIB_DPRINTF_ERR(ss->ei_instance, "eib_data_setup_cqs: "
		    "ibt_alloc_cq(snd_cq_sz=0x%lx) failed, ret=%d",
		    cq_attr.cq_size, ret);
		goto setup_data_cqs_fail;
	}
	ret = ibt_modify_cq(chan->ch_cq_hdl, EIB_TX_COMP_COUNT,
	    EIB_TX_COMP_USEC, 0);
	if (ret != IBT_SUCCESS) {
		EIB_DPRINTF_WARN(ss->ei_instance, "eib_data_setup_cqs: "
		    "ibt_modify_cq(snd_comp_count=0x%lx, snd_comp_usec=0x%lx) "
		    "failed, ret=%d",
		    EIB_TX_COMP_COUNT, EIB_TX_COMP_USEC, ret);
	}

	/*
	 * Allocate receive completion queue
	 */
	cq_attr.cq_sched = NULL;
	cq_attr.cq_flags = IBT_CQ_NO_FLAGS;
	cq_attr.cq_size = ss->ei_caps->cp_max_rwqe + 1;

	ret = ibt_alloc_cq(ss->ei_hca_hdl, &cq_attr, &chan->ch_rcv_cq_hdl,
	    &rcv_sz);
	if (ret != IBT_SUCCESS) {
		EIB_DPRINTF_ERR(ss->ei_instance, "eib_data_setup_cqs: "
		    "ibt_alloc_cq(rcv_cq_sz=0x%lx) failed, ret=%d",
		    cq_attr.cq_size, ret);
		goto setup_data_cqs_fail;
	}
	ret = ibt_modify_cq(chan->ch_rcv_cq_hdl, EIB_RX_COMP_COUNT,
	    EIB_RX_COMP_USEC, 0);
	if (ret != IBT_SUCCESS) {
		EIB_DPRINTF_WARN(ss->ei_instance, "eib_data_setup_cqs: "
		    "ibt_modify_cq(rcv_comp_count=0x%lx, rcv_comp_usec=0x%lx) "
		    "failed, ret=%d",
		    EIB_RX_COMP_COUNT, EIB_RX_COMP_USEC, ret);
	}

	/*
	 * Set up parameters for collecting tx and rx completion information
	 */
	chan->ch_cq_sz = snd_sz;
	chan->ch_wc = kmem_zalloc(sizeof (ibt_wc_t) * snd_sz, KM_SLEEP);
	chan->ch_rcv_cq_sz = rcv_sz;
	chan->ch_rcv_wc = kmem_zalloc(sizeof (ibt_wc_t) * rcv_sz, KM_SLEEP);

	/*
	 * Set up the vnic's data tx completion queue handler and allocate
	 * a softint for it as well.
	 */
	if ((rv = ddi_intr_add_softint(ss->ei_dip, &vnic->vn_data_tx_si_hdl,
	    EIB_SOFTPRI_DATA, eib_data_tx_comp_handler, vnic)) != DDI_SUCCESS) {
		EIB_DPRINTF_ERR(ss->ei_instance, "eib_data_setup_cqs: "
		    "ddi_intr_add_softint() failed for data tx qp, ret=%d", rv);
		goto setup_data_cqs_fail;
	}
	ibt_set_cq_handler(chan->ch_cq_hdl, eib_data_tx_comp_intr, vnic);
	ret = ibt_enable_cq_notify(chan->ch_cq_hdl, IBT_NEXT_COMPLETION);
	if (ret != IBT_SUCCESS) {
		EIB_DPRINTF_ERR(ss->ei_instance, "eib_data_setup_cqs: "
		    "ibt_enable_cq_notify() failed for tx cq, ret=%d", ret);
		goto setup_data_cqs_fail;
	}

	/*
	 * And then the data rx completion queue handler
	 */
	if ((rv = ddi_intr_add_softint(ss->ei_dip, &vnic->vn_data_rx_si_hdl,
	    EIB_SOFTPRI_DATA, eib_data_rx_comp_handler, vnic)) != DDI_SUCCESS) {
		EIB_DPRINTF_ERR(ss->ei_instance, "eib_data_setup_cqs: "
		    "ddi_intr_add_softint() failed for data rx qp, ret=%d", rv);
		goto setup_data_cqs_fail;
	}
	ibt_set_cq_handler(chan->ch_rcv_cq_hdl, eib_data_rx_comp_intr, vnic);
	ret = ibt_enable_cq_notify(chan->ch_rcv_cq_hdl, IBT_NEXT_COMPLETION);
	if (ret != IBT_SUCCESS) {
		EIB_DPRINTF_ERR(ss->ei_instance, "eib_data_setup_cqs: "
		    "ibt_enable_cq_notify() failed for rx cq, ret=%d", ret);
		goto setup_data_cqs_fail;
	}

	return (EIB_E_SUCCESS);

setup_data_cqs_fail:
	eib_rb_data_setup_cqs(ss, vnic);
	return (EIB_E_FAILURE);
}

static int
eib_data_setup_ud_channel(eib_t *ss, eib_vnic_t *vnic)
{
	eib_chan_t *chan = vnic->vn_data_chan;
	ibt_ud_chan_alloc_args_t alloc_attr;
	ibt_ud_chan_query_attr_t query_attr;
	ibt_status_t ret;

	bzero(&alloc_attr, sizeof (ibt_ud_chan_alloc_args_t));
	bzero(&query_attr, sizeof (ibt_ud_chan_query_attr_t));

	alloc_attr.ud_flags = IBT_ALL_SIGNALED;
	if (ss->ei_caps->cp_resv_lkey_capab)
		alloc_attr.ud_flags |= IBT_FAST_REG_RES_LKEY;
	if (ss->ei_caps->cp_lso_maxlen)
		alloc_attr.ud_flags |= IBT_USES_LSO;

	alloc_attr.ud_hca_port_num = ss->ei_props->ep_port_num;
	alloc_attr.ud_pkey_ix = chan->ch_pkey_ix;
	alloc_attr.ud_sizes.cs_sq = ss->ei_caps->cp_max_swqe;
	alloc_attr.ud_sizes.cs_rq = 0;		/* ignored for SRQ use */
	alloc_attr.ud_sizes.cs_sq_sgl = ss->ei_caps->cp_max_sgl;
	alloc_attr.ud_sizes.cs_rq_sgl = 1;	/* ignored for SRQ use */
	alloc_attr.ud_sizes.cs_inline = 0;

	alloc_attr.ud_qkey = EIB_DATA_QKEY;
	alloc_attr.ud_scq = chan->ch_cq_hdl;
	alloc_attr.ud_rcq = chan->ch_rcv_cq_hdl;
	alloc_attr.ud_pd = ss->ei_pd_hdl;
	alloc_attr.ud_srq = ss->ei_data_srq->sr_srq_hdl;

	ret = ibt_alloc_ud_channel(ss->ei_hca_hdl, IBT_ACHAN_USES_SRQ,
	    &alloc_attr, &chan->ch_chan, NULL);
	if (ret != IBT_SUCCESS) {
		EIB_DPRINTF_ERR(ss->ei_instance, "eib_data_setup_ud_channel: "
		    "ibt_alloc_ud_channel(port=0x%x, pkey_ix=0x%x, "
		    "cs_sq=0x%lx, cs_rq=0x%lx, sq_sgl=0x%lx) failed, ret=%d",
		    alloc_attr.ud_hca_port_num, chan->ch_pkey_ix,
		    alloc_attr.ud_sizes.cs_sq, alloc_attr.ud_sizes.cs_rq,
		    alloc_attr.ud_sizes.cs_sq_sgl, ret);

		goto setup_data_ud_channel_fail;
	}

	ret = ibt_query_ud_channel(chan->ch_chan, &query_attr);
	if (ret != IBT_SUCCESS) {
		EIB_DPRINTF_ERR(ss->ei_instance, "eib_data_setup_ud_channel: "
		    "ibt_query_ud_channel() failed, ret=%d", ret);
		goto setup_data_ud_channel_fail;
	}

	chan->ch_qpn = query_attr.ud_qpn;
	chan->ch_tear_down = B_FALSE;

	return (EIB_E_SUCCESS);

setup_data_ud_channel_fail:
	eib_rb_data_setup_ud_channel(ss, vnic);
	return (EIB_E_FAILURE);
}

/*
 * This routine is called only when the IP layer has told us that the
 * outgoing packet needs hardware checksumming. Given that we said we
 * had the capablity to do only IPv4 full checksumming via the
 * MAC_CAPAB_HCKSUM interface earlier, this packet must necessarily
 * be either TCP/IP or UDP/IP.
 */
static int
eib_data_update_encap_cksum(uint32_t *encap_hdr, eib_nw_hdrinfo_t *nhi)
{
	ipha_t *ipha = nhi->nh_ipha;
	uint32_t ip_src;
	uint32_t ip_dst;
	uint32_t cksum = 0;
	uint16_t *cksump;

	if (ipha) {
		ip_src = ntohl(ipha->ipha_src);
		ip_dst = ntohl(ipha->ipha_dst);

		cksum += (ip_dst >> 16) + (ip_dst & 0xffff) +
		    (ip_src >> 16) + (ip_src & 0xffff);
		cksum += ipha->ipha_protocol;

		while (cksum >> 16)
			cksum = (cksum & 0xffff) + (cksum >> 16);

		if (ipha->ipha_protocol == IPPROTO_TCP) {
			*encap_hdr = htonl(EIB_TX_ENCAP_TCPIP_CKSUM);

			/* LINTED: improper alignment cast */
			cksump = EIB_TCPH_CHECKSUMP(ipha, nhi->nh_ip_hdr_len);
			*cksump = htons((uint16_t)cksum);

		} else if (ipha->ipha_protocol == IPPROTO_UDP) {
			*encap_hdr = htonl(EIB_TX_ENCAP_UDPIP_CKSUM);

			/* LINTED: improper alignment cast */
			cksump = EIB_UDPH_CHECKSUMP(ipha, nhi->nh_ip_hdr_len);
			*cksump = htons((uint16_t)cksum);
		}

		return (EIB_E_SUCCESS);
	}

	return (EIB_E_FAILURE);
}

static void
eib_data_parse_nw_hdrs(mblk_t *mp, eib_ether_hdr_t *evh, eib_nw_hdrinfo_t *nhi)
{
	mblk_t *nmp;
	uintptr_t ip_start;
	uintptr_t tcpudp_start;

	uint8_t *ip_src;
	uint8_t *ip_dst;
	uint8_t *ports;
	uint64_t hash = 0;

	nhi->nh_eth_hdr_len = (evh->eh_tagless) ?
	    (sizeof (struct ether_header)) :
	    (sizeof (struct ether_vlan_header));
	nhi->nh_ip_hdr_len = 0;
	nhi->nh_tcp_hdr_len = 0;
	nhi->nh_hash = 0;	/* make sure this is set */

	if (evh->eh_ether_type != ETHERTYPE_IP)
		return;

	/*
	 * The only assumption we make here is that each of the Ethernet,
	 * IP and TCP headers will be contained in a single mblk fragment;
	 * together, the headers may span multiple mblk fragments.
	 */
	nmp = mp;
	ip_start = (uintptr_t)(nmp->b_rptr) + nhi->nh_eth_hdr_len;
	if (ip_start >= (uintptr_t)(nmp->b_wptr)) {
		ip_start = (uintptr_t)nmp->b_cont->b_rptr
		    + (ip_start - (uintptr_t)(nmp->b_wptr));
		nmp = nmp->b_cont;
	}

	nhi->nh_ip_hdr_len = IPH_HDR_LENGTH((ipha_t *)ip_start);
	nhi->nh_ipha = (ipha_t *)ip_start;

	ip_src = (uint8_t *)&(nhi->nh_ipha->ipha_src);
	ip_dst = (uint8_t *)&(nhi->nh_ipha->ipha_dst);
	hash ^= (EIB_PKT_HASH_4B(ip_src) ^ EIB_PKT_HASH_4B(ip_dst));

	if (nhi->nh_ipha->ipha_protocol == IPPROTO_TCP ||
	    nhi->nh_ipha->ipha_protocol == IPPROTO_UDP) {
		tcpudp_start = ip_start + nhi->nh_ip_hdr_len;
		if (tcpudp_start >= (uintptr_t)(nmp->b_wptr)) {
			tcpudp_start = (uintptr_t)nmp->b_cont->b_rptr
			    + (tcpudp_start - (uintptr_t)(nmp->b_wptr));
			nmp = nmp->b_cont;
		}

		if (nhi->nh_ipha->ipha_protocol == IPPROTO_TCP) {
			nhi->nh_tcp_hdr_len =
			    TCP_HDR_LENGTH((tcph_t *)tcpudp_start);
		}

		ports = (uint8_t *)(void *)tcpudp_start;
		hash ^= EIB_PKT_HASH_4B(ports);
	}

	nhi->nh_hash = hash % EIB_NUM_TX_QUEUES;
}

static void
eib_data_setup_lso(eib_wqe_t *swqe, mblk_t *mp, uint32_t mss,
    eib_nw_hdrinfo_t *nhi)
{
	ibt_wr_lso_t *lso;
	mblk_t  *nmp;
	uint8_t *dst;
	uint_t pending;
	uint_t mblen;

	ASSERT(nhi->nh_tcp_hdr_len != 0);

	/*
	 * When the swqe was grabbed, it would've had its wr_opcode and
	 * wr.ud.udwr_dest set to default values. Since we're now going
	 * to use LSO, we need to change these.
	 */
	swqe->qe_wr.send.wr_opcode = IBT_WRC_SEND_LSO;
	lso = &(swqe->qe_wr.send.wr.ud_lso);
	lso->lso_ud_dest = swqe->qe_dest;

	/*
	 * Set mss, lso header size.  Note that since the EoIB encapsulation
	 * header is not part of the message block we receive, we'll need to
	 * account space for inserting it later. Also, since the passed down
	 * mp fragment never contains the EoIB encapsulation header, we always
	 * have to copy the LSO header. Sigh.
	 */
	lso->lso_mss = mss;
	lso->lso_hdr = swqe->qe_payload_hdr;
	lso->lso_hdr_sz = EIB_ENCAP_HDR_SZ + nhi->nh_eth_hdr_len +
	    nhi->nh_ip_hdr_len + nhi->nh_tcp_hdr_len;

	/*
	 * We already have the EoIB encapsulation header written at the
	 * start of wqe->qe_payload_hdr during swqe acquisition.  Only
	 * copy the remaining headers.
	 */
	dst = lso->lso_hdr + EIB_ENCAP_HDR_SZ;
	pending = lso->lso_hdr_sz - EIB_ENCAP_HDR_SZ;

	for (nmp = mp; nmp && pending; nmp = nmp->b_cont) {
		mblen = MBLKL(nmp);
		if (pending > mblen) {
			bcopy(nmp->b_rptr, dst, mblen);
			dst += mblen;
			pending -= mblen;
		} else {
			bcopy(nmp->b_rptr, dst, pending);
			break;
		}
	}
}

static int
eib_data_prepare_sgl(eib_vnic_t *vnic, eib_wqe_t *swqe, mblk_t *mp)
{
	eib_t *ss = vnic->vn_ss;
	eib_stats_t *stats = vnic->vn_ss->ei_stats;
	ibt_iov_t iov_arr[EIB_MAX_SGL];
	ibt_iov_attr_t iov_attr;
	ibt_wr_ds_t *sgl;
	ibt_status_t ret;
	mblk_t *nmp;
	mblk_t *data_mp;
	uchar_t *bufp;
	size_t blksize;
	size_t skip;
	size_t avail;
	uint_t lsohdr_sz;
	uint_t pktsz;
	ptrdiff_t frag_len;
	uint_t pending_hdr;
	uint_t nblks;
	uint_t i;

	/*
	 * Let's skip ahead to the TCP data if this is LSO.  Note that while
	 * the lso header size in the swqe includes the EoIB encapsulation
	 * header size, that encapsulation header itself won't be found in
	 * the mblk.
	 */
	lsohdr_sz = (swqe->qe_wr.send.wr_opcode == IBT_WRC_SEND) ? 0 :
	    swqe->qe_wr.send.wr.ud_lso.lso_hdr_sz;

	data_mp = mp;
	pending_hdr = 0;
	if (lsohdr_sz) {
		pending_hdr = lsohdr_sz - EIB_ENCAP_HDR_SZ;
		for (nmp = mp; nmp; nmp = nmp->b_cont) {
			frag_len =
			    (uintptr_t)nmp->b_wptr - (uintptr_t)nmp->b_rptr;
			if (frag_len > pending_hdr)
				break;
			pending_hdr -= frag_len;
		}
		data_mp = nmp;  /* start of data past lso header */
		ASSERT(data_mp != NULL);
	}

	/*
	 * If this is an LSO packet, we want pktsz to hold the size of the
	 * data following the eoib/ethernet/tcp/ip headers.  If this is a
	 * non-LSO packet, we want pktsz to refer to the size of the entire
	 * packet with all the headers, and nblks to hold the number of
	 * mappings we'll need to iov map this (for reserved lkey request).
	 */
	if (lsohdr_sz == 0) {
		nblks = 1;
		pktsz = EIB_ENCAP_HDR_SZ;
	} else {
		nblks = 0;
		pktsz = 0;
	}
	for (nmp = data_mp; nmp != NULL; nmp = nmp->b_cont) {
		pktsz += MBLKL(nmp);
		nblks++;
	}
	pktsz -= pending_hdr;

	EIB_UPDT_COUNTER(&stats->st_obytes, pktsz);
	EIB_INCR_COUNTER(&stats->st_opkts);

	/*
	 * We only do ibt_map_mem_iov() if the pktsz is above the tx copy
	 * threshold and if the number of mp fragments is less than the
	 * maximum acceptable.
	 */
	if ((ss->ei_caps->cp_resv_lkey_capab) && (pktsz > EIB_TX_COPY_THRESH) &&
	    (nblks < ss->ei_caps->cp_hiwm_sgl)) {

		iov_attr.iov_as = NULL;
		iov_attr.iov = iov_arr;
		iov_attr.iov_buf = NULL;
		iov_attr.iov_list_len = nblks;
		iov_attr.iov_wr_nds = ss->ei_caps->cp_max_sgl;
		iov_attr.iov_lso_hdr_sz = lsohdr_sz;
		iov_attr.iov_flags = IBT_IOV_SLEEP;

		i = 0;
		if (lsohdr_sz == 0) {
			iov_arr[i].iov_addr = (caddr_t)swqe->qe_payload_hdr;
			iov_arr[i].iov_len = EIB_ENCAP_HDR_SZ;
			i++;
		}
		for (nmp = data_mp; i < nblks; i++, nmp = nmp->b_cont) {
			iov_arr[i].iov_addr = (caddr_t)(void *)nmp->b_rptr;
			iov_arr[i].iov_len = MBLKL(nmp);
			if (nmp == data_mp) {
				iov_arr[i].iov_addr += pending_hdr;
				iov_arr[i].iov_len -= pending_hdr;
			}
		}
		swqe->qe_wr.send.wr_sgl = swqe->qe_big_sgl;

		ret = ibt_map_mem_iov(ss->ei_hca_hdl, &iov_attr,
		    &swqe->qe_wr, &swqe->qe_iov_hdl);
		if (ret != IBT_SUCCESS) {
			EIB_DPRINTF_WARN(ss->ei_instance,
			"eib_data_prepare_sgl: "
			"ibt_map_mem_iov(nblks=0x%lx) failed, ret=%d "
			"attempting to use copy path", nblks, ret);
			goto prepare_sgl_copy_path;
		}
		swqe->qe_info |= EIB_WQE_FLG_BUFTYPE_MAPPED;

		return (EIB_E_SUCCESS);
	}

prepare_sgl_copy_path:
	if (pktsz <= swqe->qe_bufsz) {
		swqe->qe_wr.send.wr_nds = 1;
		swqe->qe_wr.send.wr_sgl = &swqe->qe_sgl;
		swqe->qe_sgl.ds_len = pktsz;

		/*
		 * Even though this is the copy path for transfers less than
		 * qe_bufsz, it could still be an LSO packet.  If so, we only
		 * have to write the data following all the headers into the
		 * work request buffer, since we'll be sending the lso header
		 * itself separately. If this is not an LSO send (but pkt size
		 * greater than mtu, say for a jumbo frame), then we need
		 * to write all the headers including EoIB encapsulation,
		 * into the work request buffer.
		 *
		 * Note that even if this isn't an LSO, we may still need to
		 * use the correct EoIB encapsulation header we set up during
		 * the checksum-offload checks earlier.
		 */
		bufp = (uchar_t *)(uintptr_t)swqe->qe_sgl.ds_va;
		if (lsohdr_sz == 0) {
			*(uint32_t *)((void *)bufp) =
			    *(uint32_t *)(void *)swqe->qe_payload_hdr;
			bufp += EIB_ENCAP_HDR_SZ;
		}
		for (nmp = data_mp; nmp != NULL; nmp = nmp->b_cont) {
			blksize = MBLKL(nmp) - pending_hdr;
			bcopy(nmp->b_rptr + pending_hdr, bufp, blksize);
			bufp += blksize;
			pending_hdr = 0;
		}

		/*
		 * If the ethernet frame we're going to send is less than
		 * ETHERMIN, pad up the buffer to ETHERMIN (with zeros)
		 */
		if ((pktsz + lsohdr_sz) < (ETHERMIN + EIB_ENCAP_HDR_SZ)) {
			bzero(bufp, (ETHERMIN + EIB_ENCAP_HDR_SZ) -
			    (pktsz + lsohdr_sz));
			swqe->qe_sgl.ds_len = ETHERMIN + EIB_ENCAP_HDR_SZ;
		}
		return (EIB_E_SUCCESS);
	}

	/*
	 * Copy path for transfers greater than swqe->qe_bufsz
	 */
	swqe->qe_wr.send.wr_sgl = swqe->qe_big_sgl;
	if (eib_rsrc_grab_lsobufs(ss, pktsz, swqe->qe_wr.send.wr_sgl,
	    &(swqe->qe_wr.send.wr_nds)) != EIB_E_SUCCESS) {
		EIB_DPRINTF_ERR(ss->ei_instance, "eib_data_prepare_sgl: "
		    "eib_rsrc_grab_lsobufs() failed");
		return (EIB_E_FAILURE);
	}
	swqe->qe_info |= EIB_WQE_FLG_BUFTYPE_LSO;

	/*
	 * Copy the larger-than-qe_buf_sz packet into a set of fixed-sized,
	 * pre-mapped LSO buffers. Note that we might need to skip part of
	 * the LSO header in the first fragment as before.
	 */
	nmp = data_mp;
	skip = pending_hdr;
	for (i = 0; i < swqe->qe_wr.send.wr_nds; i++) {
		sgl = swqe->qe_wr.send.wr_sgl + i;
		bufp = (uchar_t *)(uintptr_t)sgl->ds_va;
		avail = EIB_LSO_BUFSZ;

		/*
		 * If this is a non-LSO packet (perhaps a jumbo frame?)
		 * we may still need to prefix the EoIB header in the
		 * wr buffer.
		 */
		if ((i == 0) && (lsohdr_sz == 0)) {
			*(uint32_t *)((void *)bufp) =
			    *(uint32_t *)(void *)swqe->qe_payload_hdr;
			bufp += EIB_ENCAP_HDR_SZ;
			avail -= EIB_ENCAP_HDR_SZ;
		}

		while (nmp && avail) {
			blksize = MBLKL(nmp) - skip;
			if (blksize > avail) {
				bcopy(nmp->b_rptr + skip, bufp, avail);
				skip += avail;
				avail = 0;
			} else {
				bcopy(nmp->b_rptr + skip, bufp, blksize);
				skip = 0;
				bufp += blksize;
				avail -= blksize;
				nmp = nmp->b_cont;
			}
		}
	}

	return (EIB_E_SUCCESS);
}

/*ARGSUSED*/
static int
eib_data_is_mcast_pkt_ok(eib_vnic_t *vnic, uint8_t *macaddr, uint64_t *brdcst,
    uint64_t *multicst)
{
	/*
	 * If the dmac is a broadcast packet, let it through.  Otherwise, either
	 * we should be in promiscuous mode or the dmac should be in our list of
	 * joined multicast addresses. Currently we only update the stat
	 * counters and always let things through.
	 */
	if (bcmp(macaddr, eib_broadcast_mac, ETHERADDRL) == 0)
		EIB_INCR_COUNTER(brdcst);
	else
		EIB_INCR_COUNTER(multicst);

	return (1);
}

static void
eib_data_rx_comp_intr(ibt_cq_hdl_t cq_hdl, void *arg)
{
	eib_vnic_t *vnic = arg;
	eib_chan_t *chan = vnic->vn_data_chan;
	eib_t *ss = vnic->vn_ss;

	if (cq_hdl != chan->ch_rcv_cq_hdl) {
		EIB_DPRINTF_DEBUG(ss->ei_instance, "eib_data_rx_comp_intr: "
		    "cq_hdl(0x%llx) != chan->ch_cq_hdl(0x%llx), "
		    "ignoring completion", cq_hdl, chan->ch_cq_hdl);
		return;
	}

	ASSERT(vnic->vn_data_rx_si_hdl != NULL);

	(void) ddi_intr_trigger_softint(vnic->vn_data_rx_si_hdl, NULL);
}

static void
eib_data_tx_comp_intr(ibt_cq_hdl_t cq_hdl, void *arg)
{
	eib_vnic_t *vnic = arg;
	eib_chan_t *chan = vnic->vn_data_chan;
	eib_t *ss = vnic->vn_ss;

	if (cq_hdl != chan->ch_cq_hdl) {
		EIB_DPRINTF_DEBUG(ss->ei_instance, "eib_data_tx_comp_intr: "
		    "cq_hdl(0x%llx) != chan->ch_cq_hdl(0x%llx), "
		    "ignoring completion", cq_hdl, chan->ch_cq_hdl);
		return;
	}

	ASSERT(vnic->vn_data_tx_si_hdl != NULL);

	(void) ddi_intr_trigger_softint(vnic->vn_data_tx_si_hdl, NULL);
}

static mblk_t *
eib_data_rx_comp(eib_vnic_t *vnic, eib_wqe_t *wqe, ibt_wc_t *wc)
{
	eib_t *ss = vnic->vn_ss;
	eib_chan_t *chan = vnic->vn_data_chan;
	eib_login_data_t *ld = &vnic->vn_login_data;
	eib_stats_t *stats = ss->ei_stats;
	eib_ether_hdr_t evh;
	mblk_t *mp;
	boolean_t allocd_mp = B_FALSE;
	uint_t ec_hdr;
	uint_t ec_sign;
	uint_t ec_ver;
	uint_t ec_tu_cs;
	uint_t ec_ip_cs;

	/*
	 * If the number of receive wqes with the network layer is
	 * greater than the threshold (any snapshot of the counter
	 * is good enough) allocate a new mblk, copy the received data
	 * into it and send it up (and repost the current rwqe by
	 * calling freemsg() on the original mblk).
	 */
	if (wqe->qe_pool->wp_num_with_nw < EIB_NUM_WITH_NW_LIMIT) {
		mp = wqe->qe_mp;
	} else {
		if ((mp = allocb(wc->wc_bytes_xfer + EIB_IPHDR_ALIGN_ROOM,
		    BPRI_HI)) != NULL) {
			mp->b_rptr += EIB_IP_HDR_ALIGN;
			bcopy(wqe->qe_mp->b_rptr, mp->b_rptr,
			    wc->wc_bytes_xfer);
			freemsg(wqe->qe_mp);
			allocd_mp = B_TRUE;
		} else {
			EIB_DPRINTF_WARN(ss->ei_instance,
			    "eib_data_rx_comp: too many pkts with nw and "
			    "no memory, dropping rx pkt");
			EIB_INCR_COUNTER(&stats->st_norcvbuf);
			freemsg(wqe->qe_mp);
			return (NULL);
		}
	}

	/*
	 * Adjust write pointer depending on how much data came in. Note that
	 * since the nw layer will expect us to hand over the mp with the
	 * ethernet header starting at mp->b_rptr, update the b_rptr as well.
	 */
	mp->b_wptr = mp->b_rptr + wc->wc_bytes_xfer;

	/*
	 * We have a problem if this really happens!
	 */
	if (mp->b_next != NULL) {
		EIB_DPRINTF_WARN(ss->ei_instance, "eib_data_rx_comp: "
		    "received packet's b_next not NULL, possible dup from cq");
		mp->b_next = NULL;
	}

	/*
	 * Drop loopback packets ?
	 */
	if ((wc->wc_slid == ss->ei_props->ep_blid) &&
	    (wc->wc_qpn == chan->ch_qpn)) {
		goto data_rx_comp_fail;
	}

	mp->b_rptr += EIB_GRH_SZ;

	/*
	 * Since the recv buffer has been aligned for IP header to start on
	 * a word boundary, it is safe to say that the EoIB and ethernet
	 * headers won't start on a word boundary.
	 */
	bcopy(mp->b_rptr, &ec_hdr, EIB_ENCAP_HDR_SZ);

	/*
	 * Check EoIB signature and version
	 */
	ec_hdr = ntohl(ec_hdr);

	ec_sign = (ec_hdr >> EIB_ENCAP_SIGN_SHIFT) & EIB_ENCAP_SIGN_MASK;
	if (ec_sign != EIB_EH_SIGNATURE) {
		EIB_DPRINTF_WARN(ss->ei_instance, "eib_data_rx_comp: "
		    "EoIB encapsulation header signature (0x%lx) unknown",
		    ec_sign);
		goto data_rx_comp_fail;
	}

	ec_ver = (ec_hdr >> EIB_ENCAP_VER_SHIFT) & EIB_ENCAP_VER_MASK;
	if (ec_ver != EIB_EH_VERSION) {
		EIB_DPRINTF_WARN(ss->ei_instance, "eib_data_rx_comp: "
		    "EoIB encapsulation header version (0x%lx) unknown",
		    ec_ver);
		goto data_rx_comp_fail;
	}

	/*
	 * Check TCP/UDP and IP checksum
	 */
	ec_tu_cs = (ec_hdr >> EIB_ENCAP_TCPCHK_SHIFT) & EIB_ENCAP_TCPCHK_MASK;
	ec_ip_cs = (ec_hdr >> EIB_ENCAP_IPCHK_SHIFT) & EIB_ENCAP_IPCHK_MASK;

	if ((ec_tu_cs == EIB_EH_UDPCSUM_OK || ec_tu_cs == EIB_EH_TCPCSUM_OK) &&
	    (ec_ip_cs == EIB_EH_IPCSUM_OK)) {
		mac_hcksum_set(mp, 0, 0, 0, 0,
		    HCK_IPV4_HDRCKSUM_OK | HCK_FULLCKSUM_OK);

	} else if (ec_tu_cs == EIB_EH_CSUM_BAD || ec_ip_cs == EIB_EH_CSUM_BAD) {
		EIB_DPRINTF_WARN(ss->ei_instance, "eib_data_rx_comp: "
		    "EoIB encapsulation header tcp/udp checksum (0x%lx) or"
		    "ip checksum (0x%lx) is bad", ec_tu_cs, ec_ip_cs);
	}

	/*
	 * Update the message block's b_rptr to the start of ethernet header
	 * and parse the header information
	 */
	mp->b_rptr += EIB_ENCAP_HDR_SZ;
	eib_data_parse_ether_hdr(mp, &evh);

	/*
	 * If the incoming packet is vlan-tagged, but the tag doesn't match
	 * this vnic's vlan, drop it.
	 */
	if ((evh.eh_tagless == 0) && (evh.eh_vlan != ld->ld_assigned_vlan)) {
		EIB_DPRINTF_WARN(ss->ei_instance, "eib_data_rx_comp: "
		    "received packet's vlan unknown, expected=0x%x, got=0x%x",
		    ld->ld_assigned_vlan, evh.eh_vlan);
		goto data_rx_comp_fail;
	}

	/*
	 * Final checks to see if the unicast destination is indeed correct
	 * and to see if the multicast address is ok for us.
	 */
	if (EIB_UNICAST_MAC(evh.eh_dmac)) {
		if (bcmp(evh.eh_dmac, ld->ld_assigned_mac, ETHERADDRL) != 0) {
			uint8_t *exp;
			uint8_t *got;

			exp = ld->ld_assigned_mac;
			got = evh.eh_dmac;

			EIB_DPRINTF_WARN(ss->ei_instance, "eib_data_rx_comp: "
			    "received packet's macaddr mismatch, "
			    "expected=%x:%x:%x:%x:%x:%x, got=%x:%x:%x:%x:%x:%x",
			    exp[0], exp[1], exp[2], exp[3], exp[4], exp[5],
			    got[0], got[1], got[2], got[3], got[4], got[5]);

			goto data_rx_comp_fail;
		}
	} else {
		if (!eib_data_is_mcast_pkt_ok(vnic, evh.eh_dmac,
		    &stats->st_brdcstrcv, &stats->st_multircv)) {
			EIB_DPRINTF_WARN(ss->ei_instance, "eib_data_rx_comp: "
			    "multicast packet not ok");
			goto data_rx_comp_fail;
		}
	}

	/*
	 * Strip ethernet FCS if present in the packet.  ConnectX-2 doesn't
	 * support ethernet FCS, so this shouldn't happen anyway.
	 */
	if ((ec_hdr >> EIB_ENCAP_FCS_B_SHIFT) & 0x1) {
		EIB_DPRINTF_WARN(ss->ei_instance, "eib_data_rx_comp: "
		    "ethernet FCS present (ec_hdr=0%lx), ignoring",
		    ec_hdr);

		mp->b_wptr -= ETHERFCSL;
	}

	/*
	 * If this is the same mp as was in the original rwqe (i.e. we didn't
	 * do any allocb()), then mark the rwqe flag so we know that its mblk
	 * is with the network layer.
	 */
	if (!allocd_mp) {
		wqe->qe_info |= EIB_WQE_FLG_WITH_NW;
	}

	return (mp);

data_rx_comp_fail:
	freemsg(mp);
	return (NULL);
}

static void
eib_data_tx_comp(eib_vnic_t *vnic, eib_wqe_t *wqe)
{
	eib_t *ss = vnic->vn_ss;
	ibt_status_t ret;

	if (wqe->qe_mp) {
		if (wqe->qe_info & EIB_WQE_FLG_BUFTYPE_MAPPED) {
			ret = ibt_unmap_mem_iov(ss->ei_hca_hdl,
			    wqe->qe_iov_hdl);
			if (ret != IBT_SUCCESS) {
				EIB_DPRINTF_WARN(ss->ei_instance,
				    "eib_data_tx_comp: "
				    "ibt_unmap_mem_iov() failed, ret=%d", ret);
			}
			wqe->qe_iov_hdl = NULL;
			wqe->qe_info &= (~EIB_WQE_FLG_BUFTYPE_MAPPED);
		} else if (wqe->qe_info & EIB_WQE_FLG_BUFTYPE_LSO) {
			eib_rsrc_return_lsobufs(ss, wqe->qe_big_sgl,
			    IBT_EIB_NDS_VAL(wqe->qe_wr.send.wr_nds));
			wqe->qe_info &= (~EIB_WQE_FLG_BUFTYPE_LSO);
		}
		freemsg(wqe->qe_mp);
		wqe->qe_mp = NULL;
	}

	eib_rsrc_return_swqe(ss, wqe);
}

static void
eib_data_err_comp(eib_vnic_t *vnic, eib_wqe_t *wqe, ibt_wc_t *wc)
{
	eib_t *ss = vnic->vn_ss;

	/*
	 * Currently, all we do is report
	 */
	switch (wc->wc_status) {
	case IBT_WC_WR_FLUSHED_ERR:
		break;

	case IBT_WC_LOCAL_CHAN_OP_ERR:
		EIB_DPRINTF_ERR(ss->ei_instance, "eib_data_err_comp: "
		    "IBT_WC_LOCAL_CHAN_OP_ERR seen, wqe_info=0x%lx ",
		    wqe->qe_info);
		break;

	case IBT_WC_LOCAL_PROTECT_ERR:
		EIB_DPRINTF_ERR(ss->ei_instance, "eib_data_err_comp: "
		    "IBT_WC_LOCAL_PROTECT_ERR seen, wqe_info=0x%lx ",
		    wqe->qe_info);
		break;
	}

	/*
	 * We need to return the wqe to the pool. For rwqes, attempting
	 * to free the mblk in the wqe invokes the eib_data_rx_recycle()
	 * callback.  For tx wqes, error handling is the same as
	 * successful completion handling - we still need to unmap iov/
	 * free lsobufs/free mblk and then return the swqe to the pool.
	 */
	if (EIB_WQE_TYPE(wqe->qe_info) == EIB_WQE_RX) {
		ASSERT(wqe->qe_mp != NULL);
		wqe->qe_info |= EIB_WQE_FLG_RET_TO_POOL;
		freemsg(wqe->qe_mp);
	} else {
		eib_data_tx_comp(vnic, wqe);
	}
}

/*ARGSUSED*/
static void
eib_rb_data_setup_cqs(eib_t *ss, eib_vnic_t *vnic)
{
	eib_chan_t *chan = vnic->vn_data_chan;
	ibt_status_t ret;

	if (chan == NULL)
		return;

	/*
	 * Reset any completion handlers we may have set up
	 */
	if (chan->ch_rcv_cq_hdl) {
		ibt_set_cq_handler(chan->ch_rcv_cq_hdl, NULL, NULL);
	}
	if (chan->ch_cq_hdl) {
		ibt_set_cq_handler(chan->ch_cq_hdl, NULL, NULL);
	}

	/*
	 * Remove any softints that were added
	 */
	if (vnic->vn_data_rx_si_hdl) {
		(void) ddi_intr_remove_softint(vnic->vn_data_rx_si_hdl);
		vnic->vn_data_rx_si_hdl = NULL;
	}
	if (vnic->vn_data_tx_si_hdl) {
		(void) ddi_intr_remove_softint(vnic->vn_data_tx_si_hdl);
		vnic->vn_data_tx_si_hdl = NULL;
	}

	/*
	 * Release any work completion buffers we may have allocated
	 */
	if (chan->ch_rcv_wc && chan->ch_rcv_cq_sz) {
		kmem_free(chan->ch_rcv_wc,
		    sizeof (ibt_wc_t) * chan->ch_rcv_cq_sz);
	}
	chan->ch_rcv_cq_sz = 0;
	chan->ch_rcv_wc = NULL;

	if (chan->ch_wc && chan->ch_cq_sz) {
		kmem_free(chan->ch_wc, sizeof (ibt_wc_t) * chan->ch_cq_sz);
	}
	chan->ch_cq_sz = 0;
	chan->ch_wc = NULL;

	/*
	 * Free any completion queues we may have allocated
	 */
	if (chan->ch_rcv_cq_hdl) {
		ret = ibt_free_cq(chan->ch_rcv_cq_hdl);
		if (ret != IBT_SUCCESS) {
			EIB_DPRINTF_WARN(ss->ei_instance,
			    "eib_rb_data_setup_cqs: "
			    "ibt_free_cq(rcv_cq) failed, ret=%d", ret);
		}
		chan->ch_rcv_cq_hdl = NULL;
	}
	if (chan->ch_cq_hdl) {
		ret = ibt_free_cq(chan->ch_cq_hdl);
		if (ret != IBT_SUCCESS) {
			EIB_DPRINTF_WARN(ss->ei_instance,
			    "eib_rb_data_setup_cqs: "
			    "ibt_free_cq(snd_cq) failed, ret=%d", ret);
		}
		chan->ch_cq_hdl = NULL;
	}
}

/*ARGSUSED*/
static void
eib_rb_data_setup_ud_channel(eib_t *ss, eib_vnic_t *vnic)
{
	eib_chan_t *chan = vnic->vn_data_chan;
	ibt_status_t ret;

	if (chan == NULL)
		return;

	if (chan->ch_chan) {
		/*
		 * Mark the channel as being torn down and flush the channel
		 */
		chan->ch_tear_down = B_TRUE;
		if ((ret = ibt_flush_channel(chan->ch_chan)) != IBT_SUCCESS) {
			EIB_DPRINTF_WARN(ss->ei_instance,
			    "eib_rb_data_setup_ud_channel: "
			    "ibt_flush_channel() failed, ret=%d", ret);
		}

		/*
		 * The channel is now in the error state. We'll now wait for
		 * all the CQEs to be generated and the channel to get to
		 * the "last wqe reached" state.
		 */
		mutex_enter(&chan->ch_emptychan_lock);
		while (!chan->ch_emptychan) {
			cv_wait(&chan->ch_emptychan_cv,
			    &chan->ch_emptychan_lock);
		}
		mutex_exit(&chan->ch_emptychan_lock);

		/*
		 * We now mark the tx cqstate wait flag so that any send
		 * completion handler running after this point will know
		 * that we will be waiting for the cq to be drained and
		 * therefore do the appropiate thing.  Of course, we also
		 * need to make sure at least one more pass of the tx cq
		 * handler is invoked to ensure the cq is drained after
		 * the channel was flushed.
		 */
		mutex_enter(&chan->ch_cqstate_lock);
		chan->ch_cqstate_wait = B_TRUE;
		(void) ddi_intr_trigger_softint(vnic->vn_data_tx_si_hdl, NULL);
		while (!chan->ch_cqstate_empty)
			cv_wait(&chan->ch_cqstate_cv, &chan->ch_cqstate_lock);
		mutex_exit(&chan->ch_cqstate_lock);

		/*
		 * Do a similar thing for the rx cq as well.
		 */
		mutex_enter(&chan->ch_rcqstate_lock);
		chan->ch_rcqstate_wait = B_TRUE;
		(void) ddi_intr_trigger_softint(vnic->vn_data_rx_si_hdl, NULL);
		while (!chan->ch_rcqstate_empty)
			cv_wait(&chan->ch_rcqstate_cv, &chan->ch_rcqstate_lock);
		mutex_exit(&chan->ch_rcqstate_lock);

		/*
		 * Now we're ready to free this channel
		 */
		if ((ret = ibt_free_channel(chan->ch_chan)) != IBT_SUCCESS) {
			EIB_DPRINTF_WARN(ss->ei_instance,
			    "eib_rb_data_setup_ud_channel: "
			    "ibt_free_channel() failed, ret=%d", ret);
		}

		chan->ch_qpn = 0;
		chan->ch_chan = NULL;
	}
}
