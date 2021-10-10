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
 * This file implements the callback handler logic common to send and receive
 * handling in IBMF.
 */

#include <sys/ib/mgt/ibmf/ibmf_impl.h>

extern int ibmf_trace_level;
extern ibmf_state_t *ibmf_statep;
extern void ibmf_saa_impl_ibt_async_handler(ibt_async_code_t code,
    ibt_async_event_t *event);

static void ibmf_i_process_completion(ibmf_ci_t *cip, ibt_wc_t *wcp);
static void ibmf_i_callback_clients(ib_guid_t hca_guid,
    ibmf_async_event_t evt);

/*
 * ibmf_ibt_async_handler():
 * 	This function handles asynchronous events detected by the
 *	IBT framework.
 */
/* ARGSUSED */
void
ibmf_ibt_async_handler(void *clnt_private, ibt_hca_hdl_t hca_hdl,
    ibt_async_code_t code, ibt_async_event_t *event)
{
	ibmf_ci_t		*cip;


	/*
	 * let ibmf_saa know events first hand
	 */
	ibmf_saa_impl_ibt_async_handler(code, event);

	/*
	 * call client callbacks and then fail if ANY client remains.
	 */
	if (code == IBT_HCA_DETACH_EVENT) {

		ibmf_i_callback_clients(event->ev_hca_guid, IBMF_CI_OFFLINE);

		mutex_enter(&ibmf_statep->ibmf_mutex);
		cip = ibmf_statep->ibmf_ci_list;

		while (cip != NULL) {
			mutex_enter(&cip->ci_mutex);

			if (cip->ci_node_guid == event->ev_hca_guid) {

				mutex_exit(&cip->ci_mutex);
				break;
			}

			mutex_exit(&cip->ci_mutex);
			cip = cip->ci_next;
		}

		mutex_exit(&ibmf_statep->ibmf_mutex);
	} else if (code == IBT_EVENT_SQD) {
		ibmf_ci_t	*cip;
		ibt_qp_hdl_t	qphdl = (ibt_qp_hdl_t)event->ev_chan_hdl;
		ibmf_alt_qp_t	*altqpp;
		boolean_t	found = B_FALSE;

		mutex_enter(&ibmf_statep->ibmf_mutex);

		cip = ibmf_statep->ibmf_ci_list;

		/*
		 * An SQD event is received. We match the QP handle provided
		 * with all the alternate QP handles maintained on the lists
		 * of all the CI contexts. If a match is found, we wake
		 * up the thread waiting in ibmf_modify_qp().
		 */
		while (cip != NULL) {
			mutex_enter(&cip->ci_mutex);
			altqpp = cip->ci_alt_qp_list;
			while (altqpp != NULL) {
				if (altqpp->isq_qp_handle == qphdl) {
					mutex_enter(&altqpp->isq_mutex);
					cv_signal(&altqpp->isq_sqd_cv);
					mutex_exit(&altqpp->isq_mutex);
					found = B_TRUE;
					break;
				}
				altqpp = altqpp->isq_next;
			}
			mutex_exit(&cip->ci_mutex);

			if (found)
				break;
			cip = cip->ci_next;
		}

		mutex_exit(&ibmf_statep->ibmf_mutex);

	}

}

/*
 * ibmf_i_callback_clients():
 *	Finds the ci given in parameter.
 *	Calls the client callbacks with the event given in parameter.
 *	Note that client callbacks are called with all ibmf mutexes unlocked.
 */
static void
ibmf_i_callback_clients(ib_guid_t hca_guid, ibmf_async_event_t evt)
{
	ibmf_ci_t		*cip;
	ibmf_client_t		*clientp;

	int			nclients	= 0;
	ibmf_async_event_cb_t	*cb_array	= NULL;
	void			**cb_args_array	= NULL;
	ibmf_handle_t		*client_array	= NULL;
	int			iclient;


	/* find ci */
	mutex_enter(&ibmf_statep->ibmf_mutex);
	cip = ibmf_statep->ibmf_ci_list;

	while (cip != NULL) {
		mutex_enter(&cip->ci_mutex);

		if (cip->ci_node_guid == hca_guid) {
			mutex_exit(&cip->ci_mutex);
			break;
		}

		mutex_exit(&cip->ci_mutex);
		cip = cip->ci_next;
	}

	if (cip == NULL) {


		mutex_exit(&ibmf_statep->ibmf_mutex);
		goto bail;
	}

	/* found the right ci, count clients */
	mutex_enter(&cip->ci_clients_mutex);

	/* empty counting loop */
	for (clientp = cip->ci_clients, nclients = 0; clientp != NULL;
	    clientp = clientp->ic_next, nclients++)
		;


	/* no clients? bail */
	if (nclients == 0) {

		mutex_exit(&cip->ci_clients_mutex);
		mutex_exit(&ibmf_statep->ibmf_mutex);
		goto bail;
	}

	/* allocate callback, args, and client arrays */

	cb_array = kmem_zalloc(
	    nclients * sizeof (ibmf_async_event_cb_t), KM_NOSLEEP);

	cb_args_array = kmem_zalloc(
	    nclients * sizeof (void*), KM_NOSLEEP);

	client_array = kmem_zalloc(
	    nclients * sizeof (ibmf_handle_t), KM_NOSLEEP);

	if (cb_array == NULL || cb_args_array == NULL ||
	    client_array == NULL) {


		mutex_exit(&cip->ci_clients_mutex);
		mutex_exit(&ibmf_statep->ibmf_mutex);
		goto bail;
	}

	/* build callback list */

	for (clientp = cip->ci_clients, iclient = 0;
	    clientp != NULL;
	    clientp = clientp->ic_next, iclient++) {

		cb_array[iclient]	 = clientp->ic_async_cb;
		cb_args_array[iclient]	 = clientp->ic_async_cb_arg;
		client_array[iclient]	 = (ibmf_handle_t)clientp;
	}

	mutex_exit(&cip->ci_clients_mutex);
	mutex_exit(&ibmf_statep->ibmf_mutex);

	/*
	 * All mutex unlocked, call back clients
	 */
	for (iclient = 0; iclient < nclients; iclient++) {


		if (cb_array[iclient] != NULL)
			cb_array[iclient](client_array[iclient],
			    cb_args_array[iclient], evt);
	}

bail:

	if (cb_array != NULL)
		kmem_free(cb_array, nclients * sizeof (ibmf_async_event_cb_t));

	if (cb_args_array != NULL)
		kmem_free(cb_args_array, nclients * sizeof (void*));

	if (client_array != NULL)
		kmem_free(client_array, nclients * sizeof (ibmf_handle_t));

}

/*
 * ibmf_i_mad_completions():
 *	Check for a completion entry on the specified CQ and process it
 */
void
ibmf_i_mad_completions(ibt_cq_hdl_t cq_handle, void *arg)
{
	ibt_wc_t	cqe;
	ibt_status_t	status;
	ibmf_ci_t	*ibmf_cip;


	ibmf_cip = arg;

	ASSERT(ibmf_cip != NULL);

	/*
	 * Pull a completion and process it
	 */
	for (;;) {
		status = ibt_poll_cq(cq_handle, &cqe, 1, NULL);
		ASSERT(status != IBT_CQ_HDL_INVALID &&
		    status != IBT_HCA_HDL_INVALID);
		/*
		 * Check if the status is IBT_SUCCESS or IBT_CQ_EMPTY
		 * either which can return from ibt_poll_cq(). In other
		 * cases, log the status for the further investigation.
		 */
		if (status != IBT_SUCCESS) {
			if (status != IBT_CQ_EMPTY) {
				cmn_err(CE_NOTE, "!ibmf_i_mad_completions got "
				    "an error status (0x%x) from ibt_poll_cq.",
				    status);
			}
			break;
		}

		/* process the completion */
		ibmf_i_process_completion(ibmf_cip, &cqe);
	}

	(void) ibt_enable_cq_notify(cq_handle, IBT_NEXT_COMPLETION);

	/*
	 * Look for more completions just in case some came in before
	 * we were able to reenable CQ notification
	 */
	for (;;) {
		status = ibt_poll_cq(cq_handle, &cqe, 1, NULL);
		ASSERT(status != IBT_CQ_HDL_INVALID &&
		    status != IBT_HCA_HDL_INVALID);
		/*
		 * Check if the status is IBT_SUCCESS or IBT_CQ_EMPTY
		 * either which can return from ibt_poll_cq(). In other
		 * cases, log the status for the further investigation.
		 */
		if (status != IBT_SUCCESS) {
			if (status != IBT_CQ_EMPTY) {
				cmn_err(CE_NOTE, "!ibmf_i_mad_completions got "
				    "an error status (0x%x) from ibt_poll_cq.",
				    status);
			}
			break;
		}

		/* process the completion */
		ibmf_i_process_completion(ibmf_cip, &cqe);
	}

}

/*
 * ibmf_i_process_completion():
 *	Process the send or receive completion
 */
static void
ibmf_i_process_completion(ibmf_ci_t *cip, ibt_wc_t *wcp)
{

	if (IBMF_IS_RECV_WR_ID(wcp->wc_id) == B_TRUE) {
		/* completion from a receive queue */
		ibmf_i_handle_recv_completion(cip, wcp);
	} else {
		/* completion from a send queue */
		ibmf_i_handle_send_completion(cip, wcp);
	}

}

#ifdef DEBUG
static int ibmf_i_dump_mad_size = 0x40;
static int ibmf_i_dump_wcp_enable = 0;

/* ARGSUSED */
void
ibmf_i_dump_wcp(ibmf_ci_t *cip, ibt_wc_t *wcp, ibmf_recv_wqe_t	*recv_wqep)
{
	uchar_t *ptr;
	char buf[256], *sptr;
	int i, j;

	if (ibmf_i_dump_wcp_enable == 0)
		return;

	printf("wcp: sender lid %x port num %x path bits %x qp %x sl %x\n",
	    wcp->wc_slid, recv_wqep->recv_port_num, wcp->wc_path_bits,
	    wcp->wc_qpn, wcp->wc_sl);

	ptr = (uchar_t *)((uintptr_t)recv_wqep->recv_mem +
	    sizeof (ib_grh_t));

	printf("mad:\n");
	/* first print multiples of 16bytes */
	for (i = ibmf_i_dump_mad_size; i >= 16; i -= 16) {
		for (sptr = buf, j = 0; j < 16; j++) {
			(void) sprintf(sptr, "%02x ", *ptr++);
			sptr += 3; /* 2 digits + space */
		}
		printf("%s\n", buf);
	}
	/* print the rest */
	if (i < 16) {
		for (sptr = buf, j = 0; j < i; j++) {
			(void) sprintf(sptr, "%02x ", *ptr++);
			sptr += 3; /* 2 digits + space */
		}
		printf("%s\n", buf);
	}
}
#endif /* DEBUG */
