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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/types.h>
#include <sys/kmem.h>
#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/ksynch.h>

#include <sys/ib/clients/eoib/eib_impl.h>

/*
 * Declarations private to this file
 */
static int eib_ctl_setup_cq(eib_t *, eib_vnic_t *);
static int eib_ctl_setup_ud_channel(eib_t *, eib_vnic_t *);
static void eib_ctl_comp_intr(ibt_cq_hdl_t, void *);
static void eib_ctl_rx_comp(eib_vnic_t *, eib_wqe_t *);
static void eib_ctl_tx_comp(eib_vnic_t *, eib_wqe_t *);
static void eib_ctl_err_comp(eib_vnic_t *, eib_wqe_t *, ibt_wc_t *);
static void eib_rb_ctl_setup_cq(eib_t *, eib_vnic_t *);
static void eib_rb_ctl_setup_ud_channel(eib_t *, eib_vnic_t *);

int
eib_ctl_create_qp(eib_t *ss, eib_vnic_t *vnic, int *err)
{
	eib_chan_t *chan = NULL;

	/*
	 * Allocate a eib_chan_t to store stuff about this vnic's ctl qp
	 * and initialize it with default admin qp pkey parameters. We'll
	 * re-associate this with the pkey we receive from the gw once we
	 * receive the login ack.
	 */
	vnic->vn_ctl_chan = eib_chan_init();

	chan = vnic->vn_ctl_chan;
	chan->ch_pkey = ss->ei_admin_chan->ch_pkey;
	chan->ch_pkey_ix = ss->ei_admin_chan->ch_pkey_ix;

	/*
	 * Setup a combined CQ and completion handler
	 */
	if (eib_ctl_setup_cq(ss, vnic) != EIB_E_SUCCESS) {
		EIB_DPRINTF_ERR(ss->ei_instance, "eib_ctl_create_qp: "
		    "eib_ctl_setup_cq() failed");
		*err = ENOMEM;
		goto ctl_create_qp_fail;
	}

	/*
	 * Setup UD channel
	 */
	if (eib_ctl_setup_ud_channel(ss, vnic) != EIB_E_SUCCESS) {
		EIB_DPRINTF_ERR(ss->ei_instance, "eib_ctl_create_qp: "
		    "eib_ctl_setup_ud_channel() failed");
		*err = ENOMEM;
		goto ctl_create_qp_fail;
	}

	return (EIB_E_SUCCESS);

ctl_create_qp_fail:
	eib_rb_ctl_create_qp(ss, vnic);
	return (EIB_E_FAILURE);
}

/*ARGSUSED*/
uint_t
eib_ctl_comp_handler(caddr_t arg1, caddr_t arg2)
{
	eib_vnic_t *vnic = (eib_vnic_t *)(void *)arg1;
	eib_chan_t *chan = vnic->vn_ctl_chan;
	eib_t *ss = vnic->vn_ss;
	ibt_wc_t *wc;
	eib_wqe_t *wqe;
	ibt_status_t ret;
	uint_t polled;
	int i;

	/*
	 * Re-arm the notification callback before we start polling
	 * the completion queue.  There's nothing much we can do if the
	 * enable_cq_notify fails - we issue a warning and move on.
	 */
	ret = ibt_enable_cq_notify(chan->ch_cq_hdl, IBT_NEXT_COMPLETION);
	if (ret != IBT_SUCCESS) {
		EIB_DPRINTF_WARN(ss->ei_instance, "eib_ctl_comp_handler: "
		    "ibt_enable_cq_notify() failed, ret=%d", ret);
	}

	/*
	 * Handle tx and rx completions
	 */
	while ((ret = ibt_poll_cq(chan->ch_cq_hdl, chan->ch_wc, chan->ch_cq_sz,
	    &polled)) == IBT_SUCCESS) {
		for (wc = chan->ch_wc, i = 0; i < polled; i++, wc++) {
			wqe = (eib_wqe_t *)(uintptr_t)wc->wc_id;
			if (wc->wc_status != IBT_WC_SUCCESS) {
				eib_ctl_err_comp(vnic, wqe, wc);
			} else if (EIB_WQE_TYPE(wqe->qe_info) == EIB_WQE_RX) {
				eib_ctl_rx_comp(vnic, wqe);
			} else {
				eib_ctl_tx_comp(vnic, wqe);
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
eib_rb_ctl_create_qp(eib_t *ss, eib_vnic_t *vnic)
{
	eib_rb_ctl_setup_ud_channel(ss, vnic);

	eib_rb_ctl_setup_cq(ss, vnic);

	eib_chan_fini(vnic->vn_ctl_chan);
	vnic->vn_ctl_chan = NULL;
}

static int
eib_ctl_setup_cq(eib_t *ss, eib_vnic_t *vnic)
{
	eib_chan_t *chan = vnic->vn_ctl_chan;
	ibt_cq_attr_t cq_attr;
	ibt_status_t ret;
	uint_t sz;
	int rv;

	/*
	 * Allocate a completion queue for sending vhub table request
	 * and vhub-update/vnic-alive messages and responses from the
	 * gateway
	 */
	cq_attr.cq_sched = NULL;
	cq_attr.cq_flags = IBT_CQ_NO_FLAGS;
	if (ss->ei_hca_attrs->hca_max_cq_sz < EIB_CTL_CQ_SIZE)
		cq_attr.cq_size = ss->ei_hca_attrs->hca_max_cq_sz;
	else
		cq_attr.cq_size = EIB_CTL_CQ_SIZE;

	ret = ibt_alloc_cq(ss->ei_hca_hdl, &cq_attr, &chan->ch_cq_hdl, &sz);
	if (ret != IBT_SUCCESS) {
		EIB_DPRINTF_ERR(ss->ei_instance, "eib_ctl_setup_cq: "
		    "ibt_alloc_cq(cq_sz=0x%lx) failed, ret=%d",
		    cq_attr.cq_size, ret);
		goto ctl_setup_cq_fail;
	}

	/*
	 * Set up other parameters for collecting completion information
	 */
	chan->ch_cq_sz = sz;
	chan->ch_wc = kmem_zalloc(sizeof (ibt_wc_t) * sz, KM_SLEEP);

	/*
	 * Allocate soft interrupt for this vnic's control channel cq
	 * handler and set up the IBTL cq handler.
	 */
	if ((rv = ddi_intr_add_softint(ss->ei_dip, &vnic->vn_ctl_si_hdl,
	    EIB_SOFTPRI_CTL, eib_ctl_comp_handler, vnic)) != DDI_SUCCESS) {
		EIB_DPRINTF_ERR(ss->ei_instance, "eib_ctl_setup_cq: "
		    "ddi_intr_add_softint() failed for vnic %d ctl qp, ret=%d",
		    vnic->vn_instance, rv);
		goto ctl_setup_cq_fail;
	}

	/*
	 * Now, set up this vnic's control channel completion queue handler
	 */
	ibt_set_cq_handler(chan->ch_cq_hdl, eib_ctl_comp_intr, vnic);

	ret = ibt_enable_cq_notify(chan->ch_cq_hdl, IBT_NEXT_COMPLETION);
	if (ret != IBT_SUCCESS) {
		EIB_DPRINTF_ERR(ss->ei_instance, "eib_ctl_setup_cq: "
		    "ibt_enable_cq_notify() failed, ret=%d", ret);
		goto ctl_setup_cq_fail;
	}

	return (EIB_E_SUCCESS);

ctl_setup_cq_fail:
	eib_rb_ctl_setup_cq(ss, vnic);
	return (EIB_E_FAILURE);
}

static int
eib_ctl_setup_ud_channel(eib_t *ss, eib_vnic_t *vnic)
{
	eib_chan_t *chan = vnic->vn_ctl_chan;
	ibt_ud_chan_alloc_args_t alloc_attr;
	ibt_ud_chan_query_attr_t query_attr;
	ibt_status_t ret;

	bzero(&alloc_attr, sizeof (ibt_ud_chan_alloc_args_t));
	bzero(&query_attr, sizeof (ibt_ud_chan_query_attr_t));

	alloc_attr.ud_flags = IBT_ALL_SIGNALED;
	alloc_attr.ud_hca_port_num = ss->ei_props->ep_port_num;
	alloc_attr.ud_pkey_ix = chan->ch_pkey_ix;
	alloc_attr.ud_sizes.cs_sq = EIB_CTL_MAX_SWQE;
	alloc_attr.ud_sizes.cs_rq = 0;		/* ignored for SRQ use */
	alloc_attr.ud_sizes.cs_sq_sgl = 1;
	alloc_attr.ud_sizes.cs_rq_sgl = 1;	/* ignored for SRQ use */
	alloc_attr.ud_sizes.cs_inline = 0;

	alloc_attr.ud_qkey = EIB_FIP_QKEY;
	alloc_attr.ud_scq = chan->ch_cq_hdl;
	alloc_attr.ud_rcq = chan->ch_cq_hdl;
	alloc_attr.ud_pd = ss->ei_pd_hdl;
	alloc_attr.ud_srq = ss->ei_ctladm_srq->sr_srq_hdl;

	ret = ibt_alloc_ud_channel(ss->ei_hca_hdl, IBT_ACHAN_USES_SRQ,
	    &alloc_attr, &chan->ch_chan, NULL);
	if (ret != IBT_SUCCESS) {
		EIB_DPRINTF_ERR(ss->ei_instance, "eib_ctl_setup_ud_channel: "
		    "ibt_alloc_ud_channel(port=0x%x, pkey_ix=0x%x) "
		    "failed, ret=%d", alloc_attr.ud_hca_port_num,
		    chan->ch_pkey_ix, ret);
		goto ctl_setup_ud_channel_fail;
	}

	ret = ibt_query_ud_channel(chan->ch_chan, &query_attr);
	if (ret != IBT_SUCCESS) {
		EIB_DPRINTF_ERR(ss->ei_instance, "eib_ctl_setup_ud_channel: "
		    "ibt_query_ud_channel() failed, ret=%d", ret);
		goto ctl_setup_ud_channel_fail;
	}

	chan->ch_qpn = query_attr.ud_qpn;
	chan->ch_tear_down = B_FALSE;

	return (EIB_E_SUCCESS);

ctl_setup_ud_channel_fail:
	eib_rb_ctl_setup_ud_channel(ss, vnic);
	return (EIB_E_FAILURE);
}

static void
eib_ctl_comp_intr(ibt_cq_hdl_t cq_hdl, void *arg)
{
	eib_vnic_t *vnic = arg;
	eib_t *ss = vnic->vn_ss;
	eib_chan_t *chan = vnic->vn_ctl_chan;

	if (cq_hdl != chan->ch_cq_hdl) {
		EIB_DPRINTF_DEBUG(ss->ei_instance, "eib_ctl_comp_intr: "
		    "cq_hdl(0x%llx) != chan->ch_cq_hdl(0x%llx), "
		    "ignoring completion", cq_hdl, chan->ch_cq_hdl);
		return;
	}

	ASSERT(vnic->vn_ctl_si_hdl != NULL);

	(void) ddi_intr_trigger_softint(vnic->vn_ctl_si_hdl, NULL);
}

static void
eib_ctl_rx_comp(eib_vnic_t *vnic, eib_wqe_t *wqe)
{
	eib_t *ss = vnic->vn_ss;
	uint8_t *pkt = (uint8_t *)(uintptr_t)(wqe->qe_sgl.ds_va);

	/*
	 * Skip the GRH and parse the message in the packet
	 */
	(void) eib_fip_parse_ctl_pkt(pkt + EIB_GRH_SZ, vnic);

	/*
	 * Repost the rwqe to the SRQ (or queue it to post to SRQ a
	 * litter later)
	 */
	eib_chan_post_rwqe(ss, ss->ei_ctladm_srq, wqe);
}

static void
eib_ctl_tx_comp(eib_vnic_t *vnic, eib_wqe_t *wqe)
{
	eib_rsrc_return_swqe(vnic->vn_ss, wqe);
}

static void
eib_ctl_err_comp(eib_vnic_t *vnic, eib_wqe_t *wqe, ibt_wc_t *wc)
{
	eib_t *ss = vnic->vn_ss;

	/*
	 * Currently, all we do is report
	 */
	switch (wc->wc_status) {
	case IBT_WC_WR_FLUSHED_ERR:
		break;

	case IBT_WC_LOCAL_CHAN_OP_ERR:
		EIB_DPRINTF_ERR(ss->ei_instance, "eib_ctl_err_comp: "
		    "IBT_WC_LOCAL_CHAN_OP_ERR seen, wqe_info=0x%lx ",
		    wqe->qe_info);
		break;

	case IBT_WC_LOCAL_PROTECT_ERR:
		EIB_DPRINTF_ERR(ss->ei_instance, "eib_ctl_err_comp: "
		    "IBT_WC_LOCAL_PROTECT_ERR seen, wqe_info=0x%lx ",
		    wqe->qe_info);
		break;
	}

	/*
	 * When a wc indicates error, it is most likely that we're
	 * tearing down the channel.  In this case, we do not attempt
	 * to repost but simply return it to the wqe pool.
	 */
	if (EIB_WQE_TYPE(wqe->qe_info) == EIB_WQE_RX)
		eib_rsrc_return_rwqe(ss, wqe);
	else
		eib_rsrc_return_swqe(ss, wqe);
}

/*ARGSUSED*/
static void
eib_rb_ctl_setup_cq(eib_t *ss, eib_vnic_t *vnic)
{
	eib_chan_t *chan = vnic->vn_ctl_chan;
	ibt_status_t ret;

	if (chan == NULL)
		return;

	/*
	 * Reset any completion handler we may have set up
	 */
	if (chan->ch_cq_hdl)
		ibt_set_cq_handler(chan->ch_cq_hdl, NULL, NULL);

	/*
	 * Remove any softint we may have allocated for this cq
	 */
	if (vnic->vn_ctl_si_hdl) {
		(void) ddi_intr_remove_softint(vnic->vn_ctl_si_hdl);
		vnic->vn_ctl_si_hdl = NULL;
	}

	/*
	 * Release any work completion buffers we may have allocated
	 */
	if (chan->ch_wc && chan->ch_cq_sz)
		kmem_free(chan->ch_wc, sizeof (ibt_wc_t) * chan->ch_cq_sz);

	chan->ch_cq_sz = 0;
	chan->ch_wc = NULL;

	/*
	 * Free any completion queue we may have allocated
	 */
	if (chan->ch_cq_hdl) {
		ret = ibt_free_cq(chan->ch_cq_hdl);
		if (ret != IBT_SUCCESS) {
			EIB_DPRINTF_WARN(ss->ei_instance,
			    "eib_rb_ctl_setup_cq: "
			    "ibt_free_cq() failed, ret=%d", ret);
		}
		chan->ch_cq_hdl = NULL;
	}
}

/*ARGSUSED*/
static void
eib_rb_ctl_setup_ud_channel(eib_t *ss, eib_vnic_t *vnic)
{
	eib_chan_t *chan = vnic->vn_ctl_chan;
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
			    "eib_rb_ctl_setup_ud_channel: "
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
		 * We now mark the wait flag so that any completion handler
		 * running after this point will know that we could be waiting
		 * for the cq to be drained.  Of course, we also need to make
		 * sure at least one more pass of the cq handler is invoked
		 * to ensure draining the cq after the channel is flushed.
		 */
		mutex_enter(&chan->ch_cqstate_lock);
		chan->ch_cqstate_wait = B_TRUE;
		(void) ddi_intr_trigger_softint(vnic->vn_ctl_si_hdl, NULL);
		while (!chan->ch_cqstate_empty)
			cv_wait(&chan->ch_cqstate_cv, &chan->ch_cqstate_lock);
		mutex_exit(&chan->ch_cqstate_lock);

		/*
		 * Now we're ready to free this channel
		 */
		if ((ret = ibt_free_channel(chan->ch_chan)) != IBT_SUCCESS) {
			EIB_DPRINTF_WARN(ss->ei_instance,
			    "eib_rb_ctl_setup_ud_channel: "
			    "ibt_free_channel() failed, ret=%d", ret);
		}

		chan->ch_qpn = 0;
		chan->ch_chan = NULL;
	}
}
