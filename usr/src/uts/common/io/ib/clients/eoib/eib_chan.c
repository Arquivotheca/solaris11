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

eib_chan_t *
eib_chan_init(void)
{
	eib_chan_t *chan;
	eib_chan_txq_t *txq;
	int i;

	/*
	 * Allocate a eib_chan_t to store stuff about admin qp and
	 * initialize some basic stuff
	 */
	chan = kmem_zalloc(sizeof (eib_chan_t), KM_SLEEP);

	mutex_init(&chan->ch_pkey_lock, NULL, MUTEX_DRIVER, NULL);
	mutex_init(&chan->ch_cep_lock, NULL, MUTEX_DRIVER, NULL);
	mutex_init(&chan->ch_cqstate_lock, NULL, MUTEX_DRIVER, NULL);
	mutex_init(&chan->ch_rcqstate_lock, NULL, MUTEX_DRIVER, NULL);
	mutex_init(&chan->ch_emptychan_lock, NULL, MUTEX_DRIVER, NULL);

	cv_init(&chan->ch_cep_cv, NULL, CV_DEFAULT, NULL);
	cv_init(&chan->ch_cqstate_cv, NULL, CV_DEFAULT, NULL);
	cv_init(&chan->ch_rcqstate_cv, NULL, CV_DEFAULT, NULL);
	cv_init(&chan->ch_emptychan_cv, NULL, CV_DEFAULT, NULL);

	for (i = 0; i < EIB_NUM_TX_QUEUES; i++) {
		txq = &chan->ch_txq[i];
		mutex_init(&txq->tx_lock, NULL, MUTEX_DRIVER, NULL);
		cv_init(&txq->tx_cv, NULL, CV_DEFAULT, NULL);
	}

	return (chan);
}

void
eib_chan_fini(eib_chan_t *chan)
{
	eib_chan_txq_t *txq;
	int i;

	if (chan) {
		for (i = 0; i < EIB_NUM_TX_QUEUES; i++) {
			txq = &chan->ch_txq[i];
			cv_destroy(&txq->tx_cv);
			mutex_destroy(&txq->tx_lock);
		}

		cv_destroy(&chan->ch_emptychan_cv);
		cv_destroy(&chan->ch_rcqstate_cv);
		cv_destroy(&chan->ch_cqstate_cv);
		cv_destroy(&chan->ch_cep_cv);

		mutex_destroy(&chan->ch_emptychan_lock);
		mutex_destroy(&chan->ch_rcqstate_lock);
		mutex_destroy(&chan->ch_cqstate_lock);
		mutex_destroy(&chan->ch_cep_lock);
		mutex_destroy(&chan->ch_pkey_lock);

		kmem_free(chan, sizeof (eib_chan_t));
	}
}

int
eib_chan_fill_srq(eib_t *ss, eib_srq_t *srq)
{
	eib_wqe_t *rwqes[EIB_RWR_CHUNK_SZ];
	uint_t n_got = 0;
	uint_t n_good = 0;
	uint_t room = 0;
	uint_t chunk_sz;
	uint_t n_posted_ok;
	int wndx;
	int i;

	room = srq->sr_srq_wr_sz - 2;
	for (wndx = 0; wndx < room; wndx += chunk_sz) {
		chunk_sz = ((room - wndx) < EIB_RWR_CHUNK_SZ) ?
		    (room - wndx) : EIB_RWR_CHUNK_SZ;

		if (eib_rsrc_grab_rwqes(ss, rwqes, chunk_sz, &n_got,
		    EIB_WPRI_HI) != EIB_E_SUCCESS) {
			break;
		}

		/*
		 * Post work requests from the rwqes we just grabbed
		 */
		(void) eib_chan_post_rwrs(ss, srq, rwqes, n_got, &n_posted_ok);
		for (i = n_posted_ok; i < n_got; i++) {
			if (rwqes[i]->qe_mp) {
				rwqes[i]->qe_info |= EIB_WQE_FLG_RET_TO_POOL;
				freemsg(rwqes[i]->qe_mp);
			} else {
				eib_rsrc_return_rwqe(ss, rwqes[i]);
			}
		}
		n_good += n_posted_ok;

		/*
		 * If we got less rwqes than we asked for during the grab
		 * earlier, we'll stop asking for more and quit now.
		 */
		if (n_got < chunk_sz)
			break;
	}

	/*
	 * If we posted absolutely nothing, we return failure; otherwise
	 * return success.
	 */
	if (n_good == 0)
		return (EIB_E_FAILURE);

	return (EIB_E_SUCCESS);
}

void
eib_chan_post_rwqe(eib_t *ss, eib_srq_t *srq, eib_wqe_t *wqe)
{
	eib_rxq_t *rxq;
	eib_wqe_t *rwqes[EIB_RWR_CHUNK_SZ];
	boolean_t post_now = B_FALSE;
	uint_t n_posted_ok;
	int i;

	/*
	 * Add this rwqe to the queue.  If chunk size is reached,
	 * post it to the SRQ.
	 */
	rxq = &srq->sr_rxq[EIB_RWQE_HASH(srq, wqe)];

	mutex_enter(&rxq->rx_q_lock);

	rxq->rx_q[rxq->rx_q_count] = wqe;
	rxq->rx_q_count++;

	if (rxq->rx_q_count == EIB_RWR_CHUNK_SZ) {
		for (i = 0; i < EIB_RWR_CHUNK_SZ; i++) {
			rwqes[i] = rxq->rx_q[i];
			rxq->rx_q[i] = NULL;
		}
		rxq->rx_q_count = 0;
		post_now = B_TRUE;
	}
	mutex_exit(&rxq->rx_q_lock);

	/*
	 * If we were not able to post the entire chunk successfully, we
	 * may have to freemsg() mblks we allocated during the attempt
	 * to post.  This would end up calling the rx recycle routine,
	 * which should not try to post it back into the rx queue and end
	 * up in a recursive loop, so we set a flag EIB_WQE_FLG_RET_TO_POOL.
	 */
	if (post_now) {
		(void) eib_chan_post_rwrs(ss, srq, rwqes, EIB_RWR_CHUNK_SZ,
		    &n_posted_ok);

		for (i = n_posted_ok; i < EIB_RWR_CHUNK_SZ; i++) {
			if (rwqes[i]->qe_mp) {
				rwqes[i]->qe_info |= EIB_WQE_FLG_RET_TO_POOL;
				freemsg(rwqes[i]->qe_mp);
			} else {
				eib_rsrc_return_rwqe(ss, rwqes[i]);
			}
		}
	}

}

/*ARGSUSED*/
int
eib_chan_post_rwrs(eib_t *ss, eib_srq_t *srq, eib_wqe_t **rwqes,
    uint_t n_rwqes, uint_t *n_posted)
{
	eib_wqe_t *rwqe;
	ibt_recv_wr_t wrs[EIB_RWR_CHUNK_SZ];
	ibt_status_t ret;
	uint_t num_posted;
	uint8_t *mp_base;
	size_t mp_len;
	int i;

	ASSERT(n_rwqes <= EIB_RWR_CHUNK_SZ);

	for (i = 0; i < n_rwqes; i++) {
		rwqe = rwqes[i];

		rwqe->qe_sgl.ds_va = (ib_vaddr_t)(uintptr_t)rwqe->qe_cpbuf;
		rwqe->qe_sgl.ds_len = rwqe->qe_bufsz;

		/*
		 * If this srq has receive buffer alignment restrictions,
		 * make sure the requirements are met
		 */
		if (srq->sr_ip_hdr_align) {
			rwqe->qe_sgl.ds_va += srq->sr_ip_hdr_align;
			rwqe->qe_sgl.ds_len -= srq->sr_ip_hdr_align;
		}

		/*
		 * If the receive buffer for this srq needs to have an
		 * mblk allocated, do it
		 */
		if (srq->sr_alloc_mp) {
			mp_base = (uint8_t *)(uintptr_t)(rwqe->qe_sgl.ds_va);
			mp_len = rwqe->qe_sgl.ds_len;

			rwqe->qe_mp = desballoc(mp_base, mp_len, 0,
			    &rwqe->qe_frp);
			if (rwqe->qe_mp == NULL) {
				EIB_DPRINTF_DEBUG(ss->ei_instance,
				    "eib_chan_post_rwrs: "
				    "desballoc(base=0x%llx, len=0x%llx) "
				    "failed", mp_base, mp_len);
				break;
			}
		} else {
			rwqe->qe_mp = NULL;
		}

		wrs[i] = rwqe->qe_wr.recv;
	}

	/*
	 * Try posting to the SRQ whatever we've desballoc'd
	 */
	ret = ibt_post_srq(srq->sr_srq_hdl, wrs, i, &num_posted);
	if (ret != IBT_SUCCESS) {
		EIB_DPRINTF_DEBUG(ss->ei_instance, "eib_chan_post_rwrs: "
		    "ibt_post_srq() failed, ret=%d", ret);
	}

	*n_posted = num_posted;

	return (EIB_E_SUCCESS);
}
