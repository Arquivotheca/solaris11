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
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <smbsrv/smb_kproto.h>
#include <sys/sdt.h>

/*
 * File Change Notification
 *
 * smb_fcn_req_list - list of pending notify change requests
 * smb_fcn_rsp_list - list of requests for which a change has
 *                    occurred (or request has been canceled)
 *                    and a response should now be sent.
 *                    Processed by smb_fcn_monitor(), which runs
 *                    in a separate thread - smb_fcn_thread.
 *
 * When a request is received from the client, if there are pending
 * changes a response is sent immediately. Otherwise, it is added
 * to the smb_fcn_req_list. When a change occurs that the client
 * should be notified of, a request is canceled, or the associated
 * ofile is closed or session torn down, the client's request is
 * moved from the smb_fcn_req_list to the smb_fcn_rsp_list for
 * response processing by the monitor thread.
 */

static boolean_t	smb_fcn_initialized = B_FALSE;
static smb_slist_t	smb_fcn_req_list;
static smb_slist_t	smb_fcn_rsp_list;
static smb_thread_t	smb_fcn_thread;

static void smb_fcn_reply(smb_request_t *);
static void smb_fcn_monitor(smb_thread_t *, void *);

/*
 * smb_fcn_init
 *
 * This function is not multi-thread safe. The caller must make sure
 * only one thread makes the call.
 */
int
smb_fcn_init(void)
{
	int	rc;

	if (smb_fcn_initialized)
		return (0);

	smb_slist_constructor(&smb_fcn_req_list, sizeof (smb_request_t),
	    offsetof(smb_request_t, sr_fcn_lnd));

	smb_slist_constructor(&smb_fcn_rsp_list, sizeof (smb_request_t),
	    offsetof(smb_request_t, sr_fcn_lnd));

	smb_thread_init(&smb_fcn_thread, "smb_fcn_monitor",
	    smb_fcn_monitor, NULL);

	rc = smb_thread_start(&smb_fcn_thread);
	if (rc) {
		smb_thread_destroy(&smb_fcn_thread);
		smb_slist_destructor(&smb_fcn_req_list);
		smb_slist_destructor(&smb_fcn_rsp_list);
		return (rc);
	}

	smb_fcn_initialized = B_TRUE;

	return (0);
}

/*
 * smb_fcn_fini
 *
 * This function is not multi-thread safe. The caller must make sure
 * only one thread makes the call.
 */
void
smb_fcn_fini(void)
{
	if (!smb_fcn_initialized)
		return;

	smb_thread_stop(&smb_fcn_thread);
	smb_thread_destroy(&smb_fcn_thread);
	smb_slist_destructor(&smb_fcn_req_list);
	smb_slist_destructor(&smb_fcn_rsp_list);
	smb_fcn_initialized = B_FALSE;
}

/*
 * smb_nt_transact_notify_change
 *
 * This function is responsible for handling NOTIFY CHANGE requests.
 *
 * The client's request will specify a filter for the events that
 * the client wishes to be notified of and a watchtree flag to
 * specify whether to notify the client of changes to nodes in sub-
 * directories of the watched node. These values are currently unused.
 * All relevant events on a node, or nodes in its sub-directories,
 * cause a notification response to be sent to the client.
 *
 * When a notify change request is received the ofile is notified
 * that it should start monitoring for changes on the associated node.
 * Even when a request is replied to, or canceled, the ofile will
 * continue to monitor for changes, buffering them until the next
 * request is received. If a request is received and there are
 * buffered changes on the ofile, smb_ofile_fcn_start() will return
 * B_TRUE and a response will be sent to the client immediately.
 * If there are no buffered changes, smb_ofile_fcn_start() will
 * return B_FALSE and the request will be added to smb_fcn_req_list
 * to await changes.
 */
smb_sdrc_t
smb_nt_transact_notify_change(struct smb_request *sr, struct smb_xa *xa)
{
	uint32_t		CompletionFilter;
	uint8_t			WatchTree;
	smb_sdrc_t		sdrc = SDRC_SUCCESS;

	if (smb_mbc_decodef(&xa->req_setup_mb, "lwb",
	    &CompletionFilter, &sr->smb_fid, &WatchTree) != 0)
		return (SDRC_NOT_IMPLEMENTED);

	smbsr_lookup_file(sr);
	if (sr->fid_ofile == NULL) {
		smb_errcode_seterror(NT_STATUS_INVALID_HANDLE,
		    ERRDOS, ERRbadfid);
		return (SDRC_ERROR);
	}

	/*
	 * Notify change requests are only valid on directories.
	 */
	if (!smb_node_is_dir(sr->fid_ofile->f_node)) {
		smb_errcode_seterror(NT_STATUS_INVALID_PARAMETER, 0, 0);
		return (SDRC_ERROR);
	}

	mutex_enter(&sr->sr_mutex);
	switch (sr->sr_state) {
	case SMB_REQ_STATE_ACTIVE:
		if (smb_ofile_fcn_start(sr->fid_ofile)) {
			/* pending changes - return immediately */
			sdrc = SDRC_SUCCESS;
		} else {
			sr->sr_keep = B_TRUE;
			sr->sr_state = SMB_REQ_STATE_WAITING_EVENT;
			smb_slist_insert_tail(&smb_fcn_req_list, sr);
			sdrc = SDRC_SR_KEPT;
		}
		break;

	case SMB_REQ_STATE_CANCELED:
		/*
		 * Observed Windows behavior - even a canceled request
		 * will cause the ofile to start buffering changes
		 */
		(void) smb_ofile_fcn_start(sr->fid_ofile);
		smb_errcode_seterror(NT_STATUS_CANCELLED, 0, 0);
		sdrc = SDRC_ERROR;
		break;

	default:
		ASSERT(0);
		sdrc = SDRC_SUCCESS;
		break;
	}

	mutex_exit(&sr->sr_mutex);
	return (sdrc);
}

/*
 * smb_fcn_reply
 *
 * This function sends the appropriate response to a NOTIFY CHANGE
 * request.
 * - If the node has changed, a normal reply is sent.
 *   Although the protocol supports listing details of each event
 *   that has occurred on the node, we currently respond with no
 *   data, implying simply that "something" has changed.
 * - If the ofile is closed, a tree is disconnected, or session
 *   is dropped, a normal reply is sent with no data.
 * - If the client cancels the request an NT_STATUS_CANCELED
 *   is sent in reply.
 */
static void
smb_fcn_reply(smb_request_t *sr)
{
	smb_srqueue_t	*srq;
	int		total_bytes, n_setup, n_param, n_data;
	int		param_off, param_pad, data_off, data_pad;
	struct		smb_xa *xa;

	SMB_REQ_VALID(sr);
	srq = sr->session->s_srqueue;
	smb_srqueue_waitq_to_runq(srq);

	xa = sr->sr_xa;

	mutex_enter(&sr->sr_mutex);
	switch (sr->sr_state) {

	case SMB_REQ_STATE_EVENT_OCCURRED:
		sr->sr_state = SMB_REQ_STATE_ACTIVE;

		/* something changed */
		(void) smb_mbc_encodef(&xa->rep_data_mb, "l", 0L);

		/* setup the NT transact reply */
		n_setup = MBC_LENGTH(&xa->rep_setup_mb);
		n_param = MBC_LENGTH(&xa->rep_param_mb);
		n_data  = MBC_LENGTH(&xa->rep_data_mb);

		n_setup = (n_setup + 1) / 2; /* Convert to setup words */
		param_pad = 1; /* must be one */
		param_off = param_pad + 32 + 37 + (n_setup << 1) + 2;
		/* Pad to 4 bytes */
		data_pad = (4 - ((param_off + n_param) & 3)) % 4;
		/* Param off from hdr */
		data_off = param_off + n_param + data_pad;
		total_bytes = param_pad + n_param + data_pad + n_data;

		(void) smbsr_encode_result(sr, 18+n_setup, total_bytes,
		    "b3.llllllllbCw#.C#.C",
		    18 + n_setup,	/* wct */
		    n_param,		/* Total Parameter Bytes */
		    n_data,		/* Total Data Bytes */
		    n_param,		/* Total Parameter Bytes this buffer */
		    param_off,		/* Param offset from header start */
		    0,			/* Param displacement */
		    n_data,		/* Total Data Bytes this buffer */
		    data_off,		/* Data offset from header start */
		    0,			/* Data displacement */
		    n_setup,		/* suwcnt */
		    &xa->rep_setup_mb,	/* setup[] */
		    total_bytes,	/* Total data bytes */
		    param_pad,
		    &xa->rep_param_mb,
		    data_pad,
		    &xa->rep_data_mb);
		break;

	case SMB_REQ_STATE_CANCELED:
		smb_errcode_set(NT_STATUS_CANCELLED, ERRDOS,
		    ERROR_OPERATION_ABORTED);
		(void) smb_mbc_encodef(&sr->reply, "bwbw",
		    (short)0, 0L, (short)0, 0L);
		sr->smb_wct = 0;
		sr->smb_bcc = 0;
		break;
	default:
		ASSERT(0);
	}
	mutex_exit(&sr->sr_mutex);

	/* send the reply */
	DTRACE_PROBE1(ncr__reply, struct smb_request *, sr)
	smbsr_send_reply(sr);
	smbsr_cleanup(sr);

	mutex_enter(&sr->sr_mutex);
	sr->sr_state = SMB_REQ_STATE_COMPLETED;
	mutex_exit(&sr->sr_mutex);
	smb_srqueue_runq_exit(srq);
	smb_request_free(sr);
}

/*
 * smb_fcn_notify_session
 *
 * This function searches the smb_fcn_req_list for requests related
 * to the specified session, and moves them to the smb_fcn_rsp_list
 * for response processing by the monitor thread.
 */
void
smb_fcn_notify_session(
    smb_session_t	*session,
    smb_tree_t		*tree)
{
	smb_request_t	*sr;
	smb_request_t	*tmp;
	boolean_t	sig = B_FALSE;

	smb_slist_enter(&smb_fcn_req_list);
	smb_slist_enter(&smb_fcn_rsp_list);
	sr = smb_slist_head(&smb_fcn_req_list);
	while (sr) {
		ASSERT(sr->sr_magic == SMB_REQ_MAGIC);
		tmp = smb_slist_next(&smb_fcn_req_list, sr);
		if ((sr->session == session) &&
		    (tree == NULL || sr->tid_tree == tree)) {
			mutex_enter(&sr->sr_mutex);
			switch (sr->sr_state) {
			case SMB_REQ_STATE_WAITING_EVENT:
				smb_slist_obj_move(&smb_fcn_rsp_list,
				    &smb_fcn_req_list, sr);
				smb_srqueue_waitq_enter(
				    sr->session->s_srqueue);
				sr->sr_state = SMB_REQ_STATE_EVENT_OCCURRED;
				sig = B_TRUE;
				break;
			default:
				ASSERT(0);
				break;
			}
			mutex_exit(&sr->sr_mutex);
		}
		sr = tmp;
	}
	smb_slist_exit(&smb_fcn_req_list);
	smb_slist_exit(&smb_fcn_rsp_list);
	if (sig)
		smb_thread_signal(&smb_fcn_thread);
}

/*
 * smb_fcn_notify_ofile
 *
 * This function searches the smb_fcn_req_list for request related
 * to the specified ofile, and moves them to the smb_fcn_rsp_list
 * for response processing by the monitor thread.
 *
 * Returns: B_TRUE - associated request(s) found
 *          B_FALSE - no associated requests
 */
boolean_t
smb_fcn_notify_ofile(struct smb_ofile *of)
{
	smb_request_t	*sr;
	smb_request_t	*tmp;
	boolean_t	sig = B_FALSE;

	smb_slist_enter(&smb_fcn_req_list);
	smb_slist_enter(&smb_fcn_rsp_list);
	sr = smb_slist_head(&smb_fcn_req_list);
	while (sr) {
		ASSERT(sr->sr_magic == SMB_REQ_MAGIC);
		tmp = smb_slist_next(&smb_fcn_req_list, sr);
		if (sr->fid_ofile == of) {
			mutex_enter(&sr->sr_mutex);
			switch (sr->sr_state) {
			case SMB_REQ_STATE_WAITING_EVENT:
				smb_slist_obj_move(&smb_fcn_rsp_list,
				    &smb_fcn_req_list, sr);
				smb_srqueue_waitq_enter(
				    sr->session->s_srqueue);
				sr->sr_state = SMB_REQ_STATE_EVENT_OCCURRED;
				sig = B_TRUE;
				break;
			default:
				ASSERT(0);
				break;
			}
			mutex_exit(&sr->sr_mutex);
		}
		sr = tmp;
	}
	smb_slist_exit(&smb_fcn_req_list);
	smb_slist_exit(&smb_fcn_rsp_list);
	if (sig)
		smb_thread_signal(&smb_fcn_thread);

	return (sig);
}

/*
 * smb_fcn_cancel
 *
 * This function searches smb_fcn_req_list for a specific request.
 * If found, the request is marked as canceled and moved to the
 * smb_fcn_rsp_list for response processing by the monitor thread.
 */
void
smb_fcn_cancel(struct smb_request *zsr)
{
	smb_request_t	*sr;
	smb_request_t	*tmp;
	boolean_t	sig = B_FALSE;

	smb_slist_enter(&smb_fcn_req_list);
	smb_slist_enter(&smb_fcn_rsp_list);
	sr = smb_slist_head(&smb_fcn_req_list);
	while (sr) {
		ASSERT(sr->sr_magic == SMB_REQ_MAGIC);
		tmp = smb_slist_next(&smb_fcn_req_list, sr);
		if ((sr->session == zsr->session) &&
		    (sr->sr_uid == zsr->sr_uid) &&
		    (sr->sr_pid == zsr->sr_pid) &&
		    (sr->sr_tid == zsr->sr_tid) &&
		    (sr->sr_mid == zsr->sr_mid)) {
			mutex_enter(&sr->sr_mutex);
			switch (sr->sr_state) {
			case SMB_REQ_STATE_WAITING_EVENT:
				smb_slist_obj_move(&smb_fcn_rsp_list,
				    &smb_fcn_req_list, sr);
				smb_srqueue_waitq_enter(
				    sr->session->s_srqueue);
				sr->sr_state = SMB_REQ_STATE_CANCELED;
				sig = B_TRUE;
				break;
			default:
				ASSERT(0);
				break;
			}
			mutex_exit(&sr->sr_mutex);
		}
		sr = tmp;
	}
	smb_slist_exit(&smb_fcn_req_list);
	smb_slist_exit(&smb_fcn_rsp_list);
	if (sig)
		smb_thread_signal(&smb_fcn_thread);
}

/*
 * smb_fcn_monitor
 */
static void
smb_fcn_monitor(smb_thread_t *thread, void *arg)
{
	_NOTE(ARGUNUSED(arg))

	smb_request_t	*sr;
	list_t		sr_list;

	list_create(&sr_list, sizeof (smb_request_t),
	    offsetof(smb_request_t, sr_fcn_lnd));

	while (smb_thread_continue(thread)) {
		while (smb_slist_move_tail(&sr_list, &smb_fcn_rsp_list)) {
			while ((sr = list_head(&sr_list)) != NULL) {
				ASSERT(sr->sr_magic == SMB_REQ_MAGIC);
				list_remove(&sr_list, sr);
				smb_errcode_reset();
				smb_fcn_reply(sr);
			}
		}
	}
	list_destroy(&sr_list);
}
