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
 * This file implements the transaction processing logic common to send
 * and receive transactions in IBMF.
 */

#include <sys/ib/mgt/ibmf/ibmf_impl.h>

extern int ibmf_trace_level;

/*
 * ibmf_i_terminate_transaction():
 *	Do transaction termination processing.
 */

/* ARGSUSED */
void
ibmf_i_terminate_transaction(ibmf_client_t *clientp, ibmf_msg_impl_t *msgimplp,
    uint32_t status)
{


	ASSERT(MUTEX_HELD(&msgimplp->im_mutex));

	msgimplp->im_msg_status = status;

	/*
	 * Cancel the transaction timer. timer is probably only active if status
	 * was not success and this is a recv operation, but unset_timer() will
	 * check.
	 */
	ibmf_i_unset_timer(msgimplp, IBMF_TRANS_TIMER);

	/*
	 * For unsolicited messages, do not notify the client
	 * if an error was encontered in the transfer.
	 * For solicited messages, call the transaction callback
	 * provided by the client in the message context.
	 */
	if (msgimplp->im_unsolicited == B_TRUE) {

		msgimplp->im_trans_state_flags |= IBMF_TRANS_STATE_FLAG_DONE;

	} else {


		/* mark as recv_compl happened */
		msgimplp->im_trans_state_flags |=
		    IBMF_TRANS_STATE_FLAG_RECV_DONE;

		/*
		 * Check if last send is done before marking as done.
		 * We should get here for sequenced transactions and
		 * non-sequenced send RMPP transaction.
		 */
		if (msgimplp->im_trans_state_flags &
		    IBMF_TRANS_STATE_FLAG_SEND_DONE) {
			msgimplp->im_trans_state_flags |=
			    IBMF_TRANS_STATE_FLAG_DONE;
		}
	}

}

/*
 * ibmf_i_notify_client():
 * 	If the transaction is done, call the appropriate callback
 */
void
ibmf_i_notify_client(ibmf_msg_impl_t *msgimplp)
{
	ibmf_client_t	*clientp;
	ibmf_msg_cb_t	async_cb;
	void		*async_cb_arg;


	clientp = msgimplp->im_client;

	/*
	 * message is removed so no more threads will find message;
	 * wait for any current clients to finish
	 */
	mutex_enter(&msgimplp->im_mutex);

	ASSERT(msgimplp->im_trans_state_flags & IBMF_TRANS_STATE_FLAG_DONE);

	/*
	 * If the message reference count is not zero, then some duplicate
	 * MAD has arrived for this message. The thread processing the MAD
	 * found the message on the client's list before this thread was able
	 * to remove the message from the list. Since, we should not notify
	 * the client of the transaction completion until all the threads
	 * working on this message have completed (we don't want the client
	 * to free the message while a thread is working on it), we let one
	 * of the other threads notify the client of the completion once
	 * the message reference count is zero.
	 */
	if (msgimplp->im_ref_count != 0) {
		mutex_exit(&msgimplp->im_mutex);
		return;
	}

	mutex_exit(&msgimplp->im_mutex);

	/*
	 * Free up the UD dest resource so it is not tied down by
	 * the message in case the message is not freed immediately.
	 * Clean up the UD dest list as well so that excess UD dest
	 * resources are returned to the CI.
	 */
	if (msgimplp->im_ibmf_ud_dest != NULL) {
		ibmf_i_free_ud_dest(clientp, msgimplp);
		ibmf_i_clean_ud_dest_list(clientp->ic_myci, B_FALSE);
	}

	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(*msgimplp))

	if (msgimplp->im_unsolicited == B_TRUE) {

		/*
		 * Do nothing if error status
		 */
		if (msgimplp->im_msg_status != IBMF_SUCCESS) {

			if (msgimplp->im_qp_hdl == IBMF_QP_HANDLE_DEFAULT) {
				mutex_enter(&clientp->ic_mutex);
				IBMF_RECV_CB_CLEANUP(clientp);
				mutex_exit(&clientp->ic_mutex);
			} else {
				ibmf_alt_qp_t *qpp =
				    (ibmf_alt_qp_t *)msgimplp->im_qp_hdl;
				mutex_enter(&qpp->isq_mutex);
				IBMF_ALT_RECV_CB_CLEANUP(qpp);
				mutex_exit(&qpp->isq_mutex);
			}


			ibmf_i_free_msg(msgimplp);


			return;
		}

		/*
		 * Check to see if
		 * a callback has been registered with the client
		 * for this unsolicited message.
		 * If one has been registered, up the recvs active
		 * count to get the teardown routine to wait until
		 * this callback is complete.
		 */
		if (msgimplp->im_qp_hdl == IBMF_QP_HANDLE_DEFAULT) {

			mutex_enter(&clientp->ic_mutex);

			if ((clientp->ic_recv_cb == NULL) ||
			    (clientp->ic_flags & IBMF_CLIENT_TEAR_DOWN_CB)) {
				IBMF_RECV_CB_CLEANUP(clientp);
				mutex_exit(&clientp->ic_mutex);
				ibmf_i_free_msg(msgimplp);
				return;
			}

			clientp->ic_msgs_alloced++;
			mutex_enter(&clientp->ic_kstat_mutex);
			IBMF_ADD32_KSTATS(clientp, msgs_alloced, 1);
			mutex_exit(&clientp->ic_kstat_mutex);

			async_cb = clientp->ic_recv_cb;
			async_cb_arg = clientp->ic_recv_cb_arg;

			mutex_exit(&clientp->ic_mutex);

			async_cb((ibmf_handle_t)clientp, (ibmf_msg_t *)msgimplp,
			    async_cb_arg);

			mutex_enter(&clientp->ic_mutex);
			IBMF_RECV_CB_CLEANUP(clientp);
			mutex_exit(&clientp->ic_mutex);

		} else {
			ibmf_alt_qp_t *qpp =
			    (ibmf_alt_qp_t *)msgimplp->im_qp_hdl;

			mutex_enter(&qpp->isq_mutex);

			if ((qpp->isq_recv_cb == NULL) ||
			    (qpp->isq_flags & IBMF_CLIENT_TEAR_DOWN_CB)) {
				IBMF_ALT_RECV_CB_CLEANUP(qpp);
				mutex_exit(&qpp->isq_mutex);
				ibmf_i_free_msg(msgimplp);
				return;
			}

			async_cb = qpp->isq_recv_cb;
			async_cb_arg = qpp->isq_recv_cb_arg;

			mutex_exit(&qpp->isq_mutex);

			mutex_enter(&clientp->ic_mutex);

			clientp->ic_msgs_alloced++;

			mutex_exit(&clientp->ic_mutex);

			mutex_enter(&clientp->ic_kstat_mutex);
			IBMF_ADD32_KSTATS(clientp, msgs_alloced, 1);
			mutex_exit(&clientp->ic_kstat_mutex);

			async_cb((ibmf_handle_t)clientp, (ibmf_msg_t *)msgimplp,
			    async_cb_arg);

			mutex_enter(&qpp->isq_mutex);
			IBMF_ALT_RECV_CB_CLEANUP(qpp);
			mutex_exit(&qpp->isq_mutex);
		}
	} else {

		/* Solicited transaction processing */

		if (msgimplp->im_trans_cb == NULL) {

			/* Processing for a blocking transaction */

			mutex_enter(&msgimplp->im_mutex);

			if (msgimplp->im_trans_state_flags &
			    IBMF_TRANS_STATE_FLAG_WAIT) {


				cv_signal(&msgimplp->im_trans_cv);
			}

			msgimplp->im_trans_state_flags |=
			    IBMF_TRANS_STATE_FLAG_SIGNALED;

			mutex_exit(&msgimplp->im_mutex);

		} else {

			/* Processing for a non-blocking transaction */

			mutex_enter(&msgimplp->im_mutex);
			msgimplp->im_flags &= ~IBMF_MSG_FLAGS_BUSY;
			mutex_exit(&msgimplp->im_mutex);


			if (msgimplp->im_trans_cb != NULL) {
				msgimplp->im_trans_cb(
				    (ibmf_handle_t)clientp,
				    (ibmf_msg_t *)msgimplp,
				    msgimplp->im_trans_cb_arg);
			}
		}
	}

}

/*
 * ibmf_i_notify_sequence()
 *	Checks for the need to create a termination context before
 *	notifying the client.
 */
void
ibmf_i_notify_sequence(ibmf_client_t *clientp, ibmf_msg_impl_t *msgimplp,
    int msg_flags)
{
	int status;


	if (msg_flags & IBMF_MSG_FLAGS_TERMINATION) {

		return;
	}

	if (msg_flags & IBMF_MSG_FLAGS_SET_TERMINATION) {

		/*
		 * In some cases, we need to check if the termination context
		 * needs to be set up for early termination of non-double-sided
		 * RMPP receiver transactions. In these cases we set up the
		 * termination context, and then notify the client.
		 * If the set up of the termination context fails, attempt to
		 * reverse state to the regular context, and set the response
		 * timer for the termination timeout and exit without notifying
		 * the client in this failure case. If the setting of the
		 * response timer fails, simply notify the client without
		 * going through the process of timing out in the response
		 * timer.
		 */
		status = ibmf_setup_term_ctx(clientp, msgimplp);
		if (status != IBMF_SUCCESS) {


			mutex_enter(&msgimplp->im_mutex);

			ibmf_i_set_timer(ibmf_i_recv_timeout, msgimplp,
			    IBMF_RESP_TIMER);

			/*
			 * Set the flags cleared in
			 * ibmf_i_terminate_transaction()
			 */
			msgimplp->im_trans_state_flags &=
			    ~IBMF_TRANS_STATE_FLAG_DONE;
			msgimplp->im_trans_state_flags &=
			    ~IBMF_TRANS_STATE_FLAG_RECV_DONE;

			mutex_exit(&msgimplp->im_mutex);

			/* Re-add the message to the list */
			ibmf_i_client_add_msg(clientp, msgimplp);
		} else {
			/*
			 * The termination context has been
			 * set up. Notify the client that the
			 * regular message is done.
			 */
			ibmf_i_notify_client(msgimplp);
		}
	} else {
		ibmf_i_notify_client(msgimplp);
	}

}
