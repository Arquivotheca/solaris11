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
 * Copyright (c) 2003, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * This file implements the timer setup and timeout handling functions.
 */

#include <sys/ib/mgt/ibmf/ibmf_impl.h>

clock_t ibmf_retrans_def_trans_to = IBMF_RETRANS_DEF_TRANS_TO;
extern int ibmf_trace_level;
extern int ibmf_rmpp_default_win_sz;

/*
 * ibmf_i_set_timer():
 *	Set the timer to the response or transaction time interval
 */
void
ibmf_i_set_timer(void (*func)(void *), ibmf_msg_impl_t *msgimplp,
    ibmf_timer_t type)
{
	clock_t		interval;
	ibmf_rmpp_ctx_t	*rmpp_ctx;

	ASSERT(MUTEX_HELD(&msgimplp->im_mutex));


	if (type == IBMF_RESP_TIMER) {

		/*
		 * The response timer interval is the sum of the IBA
		 * defined RespTimeValue (Vol. 1, Section 13.4.6.2.2),
		 * and the round trip time value. Both values are determined
		 * by the IBMF client and passed in the retrans_rtv and
		 * retrans_rttv fields respectively, when calling
		 * ibmf_msg_transport()
		 */
		ASSERT(msgimplp->im_rp_timeout_id == 0);
		interval = msgimplp->im_retrans.retrans_rtv +
		    msgimplp->im_retrans.retrans_rttv;


		msgimplp->im_rp_timeout_id = timeout(func,
		    (void *)msgimplp, drv_usectohz(interval));
	} else if (type == IBMF_TRANS_TIMER) {
		rmpp_ctx = &msgimplp->im_rmpp_ctx;

		ASSERT(msgimplp->im_tr_timeout_id == 0);
		if (rmpp_ctx->rmpp_flags & IBMF_CTX_RMPP_FLAGS_DYN_PYLD) {
			/*
			 * if payload was not specified use IB spec default
			 * of 40 seconds
			 */
			interval = ibmf_retrans_def_trans_to;

		} else {
			/*
			 * if payload was specified, use a variation of IB
			 * spec equation (13.6.3.2) that accounts for average
			 * window size
			 * If specified use the client transaction multiplier.
			 */
			if ((msgimplp->im_retrans.retrans_trans_to > 0) &&
			    (msgimplp->im_retrans.retrans_trans_to <= 10)) {
				interval = ((msgimplp->im_retrans.retrans_rtv +
				    msgimplp->im_retrans.retrans_rttv) /
				    ibmf_rmpp_default_win_sz) * 7 *
				    msgimplp->im_retrans.retrans_trans_to *
				    msgimplp->im_rmpp_ctx.rmpp_num_pkts;
				if (interval > ibmf_retrans_def_trans_to)
					interval = ibmf_retrans_def_trans_to;
			} else {
				interval = ((msgimplp->im_retrans.retrans_rtv +
				    msgimplp->im_retrans.retrans_rttv) /
				    ibmf_rmpp_default_win_sz) * 4 *
				    msgimplp->im_rmpp_ctx.rmpp_num_pkts;
			}

		}



		msgimplp->im_tr_timeout_id = timeout(func,
		    (void *)msgimplp, drv_usectohz(interval));
	}

}

/*
 * ibmf_i_unset_timer():
 *	Unset the timer
 */
void
ibmf_i_unset_timer(ibmf_msg_impl_t *msgimplp, ibmf_timer_t type)
{

	ASSERT(MUTEX_HELD(&msgimplp->im_mutex));

	if (type == IBMF_RESP_TIMER) {
		if (msgimplp->im_rp_timeout_id != 0) {
			msgimplp->im_rp_unset_timeout_id =
			    msgimplp->im_rp_timeout_id;
			msgimplp->im_rp_timeout_id = 0;
		}
	} else if (type == IBMF_TRANS_TIMER) {
		if (msgimplp->im_tr_timeout_id != 0) {
			msgimplp->im_tr_unset_timeout_id =
			    msgimplp->im_tr_timeout_id;
			msgimplp->im_tr_timeout_id = 0;
		}
	}

}

/*
 * ibmf_i_recv_timeout:
 *
 *	Perform "receive" timeout processing for the message.
 *	This timeout handler is only used in RMPP processing.
 */
void
ibmf_i_recv_timeout(void *argp)
{
	ibmf_msg_impl_t *msgimplp = (ibmf_msg_impl_t *)argp;
	ibmf_client_t	*clientp = (ibmf_client_t *)msgimplp->im_client;
	ibmf_rmpp_ctx_t	*rmpp_ctx;
	int		msg_flags;
	uint_t		ref_cnt;
	int		status;


	mutex_enter(&msgimplp->im_mutex);


	/*
	 * If the message has been marked unitialized or done
	 * release the message mutex and return
	 */
	if ((msgimplp->im_trans_state_flags & IBMF_TRANS_STATE_FLAG_UNINIT) ||
	    (msgimplp->im_trans_state_flags & IBMF_TRANS_STATE_FLAG_DONE)) {

		mutex_exit(&msgimplp->im_mutex);


		return;
	}

	/*
	 * Unset the response and trans timers if they haven't fired (unlikely)
	 */
	ibmf_i_unset_timer(msgimplp, IBMF_RESP_TIMER);
	ibmf_i_unset_timer(msgimplp, IBMF_TRANS_TIMER);

	rmpp_ctx = &msgimplp->im_rmpp_ctx;

	/* Perform timeout processing for the RMPP transaction */
	if (rmpp_ctx->rmpp_state == IBMF_RMPP_STATE_RECEVR_ACTIVE) {


		status = ibmf_i_send_rmpp(msgimplp, IBMF_RMPP_TYPE_ABORT,
		    IBMF_RMPP_STATUS_T2L, 0, 0, IBMF_NO_BLOCK);
		if (status != IBMF_SUCCESS) {
			msgimplp->im_trans_state_flags |=
			    IBMF_TRANS_STATE_FLAG_SEND_DONE;
		}

		mutex_enter(&clientp->ic_kstat_mutex);
		IBMF_ADD32_KSTATS(clientp, rmpp_errors, 1);
		mutex_exit(&clientp->ic_kstat_mutex);

		rmpp_ctx->rmpp_state = IBMF_RMPP_STATE_ABORT;


		ibmf_i_terminate_transaction(msgimplp->im_client,
		    msgimplp, IBMF_TRANS_TIMEOUT);

	} else if (rmpp_ctx->rmpp_state == IBMF_RMPP_STATE_RECEVR_TERMINATE) {

		rmpp_ctx->rmpp_state = IBMF_RMPP_STATE_DONE;
		ibmf_i_terminate_transaction(msgimplp->im_client,
		    msgimplp, IBMF_SUCCESS);
	}

	/*
	 * Save the transaction state flags and the timeout IDs
	 * before releasing the mutex as they may be changed after that.
	 */
	msg_flags = msgimplp->im_trans_state_flags;

	mutex_exit(&msgimplp->im_mutex);


	/*
	 * If the transaction flags indicate a completed transaction,
	 * notify the client
	 */
	if (msg_flags & IBMF_TRANS_STATE_FLAG_DONE) {
		/* Remove the message from the client's message list */
		ibmf_i_client_rem_msg(clientp, msgimplp, &ref_cnt);

		/*
		 * Notify the client if the message reference count is zero.
		 * At this point, we know that the transaction is done and
		 * the message has been removed from the client's message list.
		 * So, we only need to make sure the reference count is zero
		 * before notifying the client.
		 */
		if (ref_cnt == 0) {
			_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(*msgimplp))
			if (msgimplp->im_flags & IBMF_MSG_FLAGS_TERMINATION) {

				/*
				 * If the message is a termination message,
				 * free it at this time.
				 */

				/* free up the UD destination resource */
				if (msgimplp->im_ibmf_ud_dest != NULL) {
					ibmf_i_free_ud_dest(clientp, msgimplp);
					ibmf_i_clean_ud_dest_list(
					    clientp->ic_myci, B_FALSE);
				}

				/* Free the receive buffer */
				kmem_free(
				    msgimplp->im_msgbufs_recv.im_bufs_mad_hdr,
				    IBMF_MAD_SIZE);

				/* destroy the message mutex */
				mutex_destroy(&msgimplp->im_mutex);

				/* Free the termination message context */
				kmem_free(msgimplp, sizeof (ibmf_msg_impl_t));

				/*
				 * Decrease the "messages allocated" count
				 * so that an ibmf_unregister() can succeed
				 * for this client.
				 */
				mutex_enter(&clientp->ic_mutex);
				clientp->ic_msgs_alloced--;
				mutex_exit(&clientp->ic_mutex);

			} else {


				ibmf_i_notify_client(msgimplp);
			}
		}
	}

}

/*
 * ibmf_i_send_timeout:
 *
 *	Perform "send" timeout processing for the message.
 *	This timeout handler is used in non-RMPP and RMPP processing.
 */
void
ibmf_i_send_timeout(void *argp)
{
	ibmf_msg_impl_t *msgimplp = (ibmf_msg_impl_t *)argp;
	ibmf_client_t	*clientp = (ibmf_client_t *)msgimplp->im_client;
	ibmf_rmpp_ctx_t	*rmpp_ctx;
	int		msg_flags;
	uint_t		ref_cnt;
	int		status;


	mutex_enter(&msgimplp->im_mutex);

	/*
	 * If the message has been marked uninitialized or done, release the
	 * message mutex and return
	 */
	if ((msgimplp->im_trans_state_flags & IBMF_TRANS_STATE_FLAG_UNINIT) ||
	    (msgimplp->im_trans_state_flags & IBMF_TRANS_STATE_FLAG_DONE)) {

		mutex_exit(&msgimplp->im_mutex);


		return;
	}


	/*
	 * If the timer fired, but the corresponding MAD was received before
	 * we got to this point in the timeout code, then do nothing in the
	 * timeout handler and return
	 */
	if ((msgimplp->im_flags & IBMF_MSG_FLAGS_RECV_RMPP) &&
	    (msgimplp->im_rp_timeout_id == 0)) {

		mutex_exit(&msgimplp->im_mutex);


		return;
	}

	/* Clear the response timer */
	if (msgimplp->im_rp_timeout_id != 0)
		ibmf_i_unset_timer(msgimplp, IBMF_RESP_TIMER);

	rmpp_ctx = &msgimplp->im_rmpp_ctx;

	/*
	 * Non-RMPP send transaction timeout processing
	 */
	if ((msgimplp->im_flags & IBMF_MSG_FLAGS_SEND_RMPP) == 0) {

		/*
		 * We use the RMPP context to store the retry count even if
		 * the response does not use RMPP
		 */
		if (rmpp_ctx->rmpp_retry_cnt <
		    msgimplp->im_retrans.retrans_retries) {


			rmpp_ctx->rmpp_retry_cnt++;

			status = ibmf_i_send_single_pkt(msgimplp->im_client,
			    msgimplp->im_qp_hdl, msgimplp, IBMF_NO_BLOCK);
			if (status == IBMF_SUCCESS) {

				mutex_exit(&msgimplp->im_mutex);


				return;
			}


		}

		/*
		 * If we are in receive RMPP mode, then an ABORT should
		 * be sent after the required number of retries.
		 */
		if (msgimplp->im_flags & IBMF_MSG_FLAGS_RECV_RMPP) {
			status = ibmf_i_send_rmpp(msgimplp,
			    IBMF_RMPP_TYPE_ABORT, IBMF_RMPP_STATUS_TMR, 0, 0,
			    IBMF_NO_BLOCK);
			if (status != IBMF_SUCCESS) {
				msgimplp->im_trans_state_flags |=
				    IBMF_TRANS_STATE_FLAG_SEND_DONE;
			}
			rmpp_ctx->rmpp_state = IBMF_RMPP_STATE_ABORT;
		}

		ibmf_i_terminate_transaction(msgimplp->im_client, msgimplp,
		    IBMF_TRANS_TIMEOUT);

		msg_flags = msgimplp->im_trans_state_flags;

		mutex_exit(&msgimplp->im_mutex);

		/* Notify the client if the transaction is done */
		if (msg_flags & IBMF_TRANS_STATE_FLAG_DONE) {

			/* Remove the message from the client's message list */
			ibmf_i_client_rem_msg(clientp, msgimplp, &ref_cnt);
			/*
			 * Notify the client if the message reference count is
			 * zero. At this point, we know that the transaction is
			 * done and the message has been removed from the
			 * client's message list. So, we need to be sure the
			 * reference count is zero before notifying the client.
			 */
			if (ref_cnt == 0) {
				ibmf_i_notify_client(msgimplp);
			}
		}


		return;
	}


	/* RMPP send transaction timeout processing */
	if (rmpp_ctx->rmpp_retry_cnt == msgimplp->im_retrans.retrans_retries) {


		status = ibmf_i_send_rmpp(msgimplp, IBMF_RMPP_TYPE_ABORT,
		    IBMF_RMPP_STATUS_TMR, 0, 0, IBMF_NO_BLOCK);
		if (status != IBMF_SUCCESS) {
			msgimplp->im_trans_state_flags |=
			    IBMF_TRANS_STATE_FLAG_SEND_DONE;
		}

		rmpp_ctx->rmpp_state = IBMF_RMPP_STATE_ABORT;

		mutex_enter(&clientp->ic_kstat_mutex);
		IBMF_ADD32_KSTATS(clientp, rmpp_errors, 1);
		mutex_exit(&clientp->ic_kstat_mutex);


		ibmf_i_terminate_transaction(msgimplp->im_client,
		    msgimplp, IBMF_TRANS_TIMEOUT);

	} else {

		if (rmpp_ctx->rmpp_state == IBMF_RMPP_STATE_SENDER_ACTIVE) {


			/*
			 * resend the window
			 */
			rmpp_ctx->rmpp_ns = rmpp_ctx->rmpp_wf;

			ibmf_i_send_rmpp_window(msgimplp, IBMF_NO_BLOCK);
		} else if (rmpp_ctx->rmpp_state ==
		    IBMF_RMPP_STATE_SENDER_SWITCH) {


			/* send ACK */
			(void) ibmf_i_send_rmpp(msgimplp, IBMF_RMPP_TYPE_ACK,
			    IBMF_RMPP_STATUS_NORMAL, 0, 1, IBMF_NO_BLOCK);


			/* set response timer */
			ibmf_i_set_timer(ibmf_i_send_timeout, msgimplp,
			    IBMF_RESP_TIMER);
		}

		rmpp_ctx->rmpp_retry_cnt++;

	}

	msg_flags = msgimplp->im_trans_state_flags;

	mutex_exit(&msgimplp->im_mutex);

	clientp = (ibmf_client_t *)msgimplp->im_client;


	if (msg_flags & IBMF_TRANS_STATE_FLAG_DONE) {
		/* Remove the message from the client's message list */
		ibmf_i_client_rem_msg(clientp, msgimplp, &ref_cnt);
		/*
		 * Notify the client if the message reference count is zero.
		 * At this point, we know that the transaction is done and
		 * the message has been removed from the client's message list.
		 * So, we only need to make sure the reference count is zero
		 * before notifying the client.
		 */
		if (ref_cnt == 0) {
			ibmf_i_notify_client(msgimplp);
		}
	}

}

void
ibmf_i_err_terminate_timeout(void *argp)
{
	ibmf_msg_impl_t *msgimplp = (ibmf_msg_impl_t *)argp;
	ibmf_client_t	*clientp = (ibmf_client_t *)msgimplp->im_client;
	int		msg_flags;
	uint_t		ref_cnt;


	mutex_enter(&msgimplp->im_mutex);

	/*
	 * If the message has been marked uninitialized or done, release the
	 * message mutex and return
	 */
	if ((msgimplp->im_trans_state_flags & IBMF_TRANS_STATE_FLAG_UNINIT) ||
	    (msgimplp->im_trans_state_flags & IBMF_TRANS_STATE_FLAG_DONE)) {

		mutex_exit(&msgimplp->im_mutex);


		return;
	}


	/* Clear the response timer */
	if (msgimplp->im_rp_timeout_id != 0)
		msgimplp->im_rp_timeout_id = 0;

	/* Mark the transaction as terminated */
	ibmf_i_terminate_transaction(msgimplp->im_client, msgimplp,
	    IBMF_TRANS_FAILURE);

	msg_flags = msgimplp->im_trans_state_flags;

	mutex_exit(&msgimplp->im_mutex);

	clientp = (ibmf_client_t *)msgimplp->im_client;


	if (msg_flags & IBMF_TRANS_STATE_FLAG_DONE) {
		/* Remove the message from the client's message list */
		ibmf_i_client_rem_msg(clientp, msgimplp, &ref_cnt);
		/*
		 * Notify the client if the message reference count is zero.
		 * At this point, we know that the transaction is done and
		 * the message has been removed from the client's message list.
		 * So, we only need to make sure the reference count is zero
		 * before notifying the client.
		 */
		if (ref_cnt == 0) {
			_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(*msgimplp))
			if (msgimplp->im_flags & IBMF_MSG_FLAGS_TERMINATION) {

				/*
				 * If the message is a termination message,
				 * free it at this time.
				 */


				/* free up the UD destination resource */
				if (msgimplp->im_ibmf_ud_dest != NULL) {
					ibmf_i_free_ud_dest(clientp, msgimplp);
					ibmf_i_clean_ud_dest_list(
					    clientp->ic_myci, B_FALSE);
				}

				/* Free the receive buffer */
				kmem_free(
				    msgimplp->im_msgbufs_recv.im_bufs_mad_hdr,
				    IBMF_MAD_SIZE);

				/* destroy the message mutex */
				mutex_destroy(&msgimplp->im_mutex);

				/* Free the termination message context */
				kmem_free(msgimplp, sizeof (ibmf_msg_impl_t));

				/*
				 * Decrease the "messages allocated" count
				 * so that an ibmf_unregister() can succeed
				 * for this client.
				 */
				mutex_enter(&clientp->ic_mutex);
				clientp->ic_msgs_alloced--;
				mutex_exit(&clientp->ic_mutex);

			} else {


				ibmf_i_notify_client(msgimplp);
			}
		}
	}

}
