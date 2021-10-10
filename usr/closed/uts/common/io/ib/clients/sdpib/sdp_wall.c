/*
 * Copyright (c) 2004, 2011, Oracle and/or its affiliates. All rights reserved.
 */
/*
 * LEGAL NOTICE
 *
 * This file contains source code that implements the Sockets Direct
 * Protocol (SDP) as defined by the InfiniBand Architecture Specification,
 * Volume 1, Annex A4, Version 1.1.  Due to restrictions in the SDP license,
 * source code contained in this file may not be distributed outside of
 * Sun Microsystems without further legal review to ensure compliance with
 * the license terms.
 *
 * Sun employees and contactors are cautioned not to extract source code
 * from this file and use it for other purposes.  The SDP implementation
 * code in this and other files must be kept separate from all other source
 * code.
 *
 * As required by the license, the following notice is added to the source
 * code:
 *
 * This source code may incorporate intellectual property owned by
 * Microsoft Corporation.  Our provision of this source code does not
 * include any licenses or any other rights to you under any Microsoft
 * intellectual property.  If you would like a license from Microsoft
 * (e.g., to rebrand, redistribute), you need to contact Microsoft
 * directly.
 */

/*
 * Sun elects to include this software in this distribution under the
 * OpenIB.org BSD license
 *
 *
 * Copyright (c) 2005 Topspin Communications.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */


#include <sys/ib/clients/sdpib/sdp_main.h>
#include <sys/ib/clients/sdpib/sdp_trace_util.h>

extern int sdp_ibt_alloc_rc_channel(sdp_conn_t *conn);


/* ========================================================================= */

/*
 * sdp_cm_accept -- Process active open from peer side and reply with
 * Hello Ack.
 */
int32_t
sdp_cm_accept(sdp_conn_t *conn)
{
	int32_t result = 0;

	SDP_CHECK_NULL(conn, EINVAL);

	/*
	 * check to make sure a CM transition didn't put us into an error
	 * state.
	 */
	switch (conn->state) {
		case SDP_CONN_ST_REQ_RECV:
			/*
			 * the following routine merely allocates and formats
			 * the message, then saves it in the conn send queue.
			 * it doesn't actually "post" it.
			 *
			 * when we return to the cm_handler code, we'll get it
			 * from the send queue and copy it to the cm_priv_data
			 * pointer in the event structure (and then save the
			 * message again in the send queue).
			 */
			result = sdp_post_msg_hello_ack(conn);
			break;
		default:
			/*
			 * initiate disconnect, fail to gateway, mark
			 * connection.
			 */
			break;
	}	/* switch */

	return (result);
}   /* sdp_conn_accept */


/* ========================================================================= */


void
sdp_conn_report_error(sdp_conn_t *conn, int error)
{

	sdp_print_warn(conn, "sdp_conn_report_error: <%d>", error);
	/*
	 * the connection has failed, move to error, and notify anyone
	 * waiting of the state change. remove connection from listen
	 * queue if possible.
	 */
	(void) sdp_inet_accept_queue_remove(conn, B_FALSE);

	SDP_CONN_ST_SET(conn, SDP_CONN_ST_ERROR);
	conn->shutdown = SDP_SHUTDOWN_MASK;
	conn->sdpc_tx_max_queue_size = 0;

	if (conn->sdp_ulpd != NULL)
		conn->sdp_ulp_disconnected(conn->sdp_ulpd, -error);

	cv_signal(&conn->ss_rxdata_cv);
	cv_signal(&conn->ss_txdata_cv);
	/* if linger is set wake the thread. */
	if ((conn->inet_ops.linger) && (conn->inet_ops.lingertime > 0)) {
		cv_signal(&conn->closecv);
	}
}


/*
 * sdp_conn_abort -- caller has decided to abort connection.
 */
void
sdp_conn_abort(sdp_conn_t *conn)
{
	int32_t error = -ECONNRESET;

	SDP_CHECK_NULL(conn, -EINVAL);
	sdp_print_dbg(conn, "sdp_conn_abort: state:%x",
	    conn->state);

	switch (conn->state) {
		case SDP_CONN_ST_DIS_SENT_1:
		case SDP_CONN_ST_DIS_SEND_2:
		case SDP_CONN_ST_DIS_SEND_1:
			sdp_generic_table_clear(&conn->send_ctrl);
			sdp_generic_table_clear(&conn->send_queue);
			/*FALLTHRU*/
		case SDP_CONN_ST_DIS_RECV_1:
			error = -EPIPE;
			/*FALLTHRU*/
		case SDP_CONN_ST_DIS_RECV_R:
		case SDP_CONN_ST_ESTABLISHED:
			/*
			 * abortive close.
			 */
			sdp_cm_disconnect(conn);
			break;
		case SDP_CONN_ST_REQ_PATH:
		case SDP_CONN_ST_REQ_SENT:
		case SDP_CONN_ST_REQ_RECV:
			/*
			 * outstanding CM request. Mark it in error, and CM
			 * completion needs to complete the closing.
			 */
			error = -ECONNREFUSED;
			break;
		case SDP_CONN_ST_ERROR:
		case SDP_CONN_ST_CLOSED:
		case SDP_CONN_ST_TIME_WAIT_1:
		case SDP_CONN_ST_TIME_WAIT_2:
			break;

		default:
			sdp_print_warn(conn, "sdp_conn_abort: unexpected state"
			    " <%x>", conn->state);
			break;
	}
	sdp_conn_report_error(conn, error);
}

/*
 * sock_connect -- Received a Hello packet. Begin passive open
 */
int32_t
sdp_sock_connect(sdp_conn_t *accept_conn, ibt_cm_event_t *ibt_cm_event)
{
	struct sdp_inet_ops *listen_ops;
	struct sdp_inet_ops *accept_ops;
	int32_t result;
	sdp_conn_t *listen_conn;

	SDP_CHECK_NULL(accept_conn, EINVAL);

	/*
	 * first find a listening connection
	 */
	sdp_print_dbg(accept_conn, "sdp_sock_conn: sport:%d",
	    ntohs(accept_conn->src_port));
	listen_conn = sdp_inet_listen_lookup(accept_conn, ibt_cm_event);
	if ((listen_conn == NULL) || (listen_conn == accept_conn)) {
		/*
		 * no connection, reject
		 */
		result = ECONNREFUSED;
		dprint(SDP_DBG, ("sdp_sock_connect: listen lookup failed on "
		    "port <%d>", ntohs(accept_conn->src_port)));
		if (accept_conn->inet_ops.debug) {
			(void) strlog(SDP_MODULE_ID, 0, 1,
			    SL_TRACE | SL_ERROR,
			    "sdp_sock_connect: listen lookup failed on "
			    "port <%d>", accept_conn->src_port);
		}
		goto lookup_error;
	}
	SDP_CONN_LOCK(listen_conn);
	dprint(SDP_DBG, ("sdp_sock_connect: lconn:%p:%04x econn:%p",
	    (void *)listen_conn, listen_conn->state, (void *)accept_conn));
	if (listen_conn->state != SDP_CONN_ST_LISTEN) {
		dprint(SDP_DBG, ("sdp_sock_connect: Listen conn state %x"
		    " incorrect", listen_conn->state));
		result = ECONNREFUSED;
		goto done;
	}

	/*
	 * check backlog
	 */
	listen_ops = &listen_conn->inet_ops;
	accept_ops = &accept_conn->inet_ops;

	if (listen_conn->backlog_cnt > listen_conn->backlog_max) {
		result = ECONNREFUSED;
		sdp_print_warn(listen_conn, "sdp_sock_connect: backlog <%d> "
		    "exceeds", listen_conn->backlog_cnt);
		if (listen_conn->inet_ops.debug) {
			(void) strlog(SDP_MODULE_ID, 0, 1,
			    SL_TRACE | SL_ERROR,
			    "sdp_sock_connect: backlog count <%d exceeds "
			    "max <%d>", listen_conn->backlog_cnt,
			    listen_conn->backlog_max);
		}
		goto done;
	}	/* if */
	result = sdp_inet_port_inherit(listen_conn, accept_conn);
	if (result != 0) {
		result = EFAULT;
		goto done;
	}

	/*
	 * insert accept socket into listen sockets list. Needs to be a
	 * FIFO not a LIFO, as is now.
	 */
	/*
	 * relevant options, and others...
	 */
	accept_ops->reuse = listen_ops->reuse;
	accept_ops->debug = listen_ops->debug;
	accept_ops->localroute = listen_ops->localroute;
	accept_ops->broadcast = listen_ops->broadcast;
	accept_ops->sndbuf = listen_ops->sndbuf;
	accept_ops->rcvbuf = listen_ops->rcvbuf;
	accept_ops->urginline = listen_ops->urginline;
	accept_ops->no_check = listen_ops->no_check;
	accept_ops->priority = listen_ops->priority;
	accept_ops->linger = listen_ops->linger;
	accept_ops->lingertime = listen_ops->lingertime;
	accept_ops->bsdism = listen_ops->bsdism;
	accept_ops->rcvtstamp = listen_ops->rcvtstamp;
	accept_ops->rcvlowat = listen_ops->rcvlowat;
	accept_ops->rcvtimeo = listen_ops->rcvtimeo;
	accept_ops->sndtimeo = listen_ops->sndtimeo;
	accept_ops->bound_dev_if = listen_ops->bound_dev_if;
	accept_ops->ipv6_v6only = listen_ops->ipv6_v6only;
	accept_conn->nodelay = listen_conn->nodelay;
	accept_conn->sdpc_tx_max_queue_size =
	    listen_conn->sdpc_tx_max_queue_size;

	if (!sdp_conn_init_cred(accept_conn, listen_conn->sdp_credp)) {
		dprint(SDP_DBG, ("Fail in sdp_conn_init_cred"));
		goto done;
	}

	accept_conn->sdpc_max_rwin = listen_conn->sdpc_max_rwin;

	accept_conn->rcq_size = listen_conn->rcq_size;
	accept_conn->scq_size = listen_conn->scq_size;

	bcopy(&listen_conn->sdp_upcalls, &accept_conn->sdp_upcalls,
	    sizeof (sdp_upcalls_t));
	accept_conn->sdp_family_used = listen_conn->sdp_family_used;

	/*
	 * associate connection with a hca/port, and allocate IB.
	 */
	result = sdp_conn_allocate_ib(accept_conn,
	    &ibt_cm_event->cm_event.req.req_hca_guid,
	    ibt_cm_event->cm_event.req.req_prim_hca_port, B_FALSE);
	if (result != 0) {
		dprint(SDP_DBG, ("Fail in sdp_conn_alloc_ib"));
		goto done;
	}

	result = sdp_ibt_alloc_rc_channel(accept_conn);
	if (result != 0) {
		dprint(SDP_DBG, ("Fail in sdp_ibt_alloc_rc_channel"));
		goto done;
	}

	result = sdp_cm_accept(accept_conn);
	if (result != 0) {
		sdp_print_warn(accept_conn, "sdp_sock_connect: fail <%d>"
		    "during passive accept", result);
		goto done;
	}	/* if */
	result = sdp_inet_accept_queue_put(listen_conn, accept_conn);
	if (result != 0) {
		sdp_print_warn(listen_conn, "sdp_sock_connect: fail <%d>"
		    "during move of accept conn", result);
	}

done:
	SDP_CONN_UNLOCK(listen_conn);
	SDP_CONN_PUT(listen_conn);	/* listen_lookup reference */
lookup_error:
	return (result);
}   /* sdp_sock_connect */

/* ========================================================================= */

/*
 * sock_accept -- accept an active open
 */
int32_t
sdp_actv_connect(sdp_conn_t *conn)
{
	int32_t result;

	SDP_CHECK_NULL(conn, -EINVAL);

	/*
	 * only reason not to confirm is if the connection state has changed
	 * from under us, and the change wasn't followed up with a gateway
	 * abort(), which it should have been.
	 */
	/*
	 * post receive buffers.
	 */
	sdp_conn_set_buff_limits(conn);
	result = sdp_recv_post(conn);
	if (result < 0) {
		result = -result;
		sdp_print_warn(conn, "sdp_actv_connect: fail in recv_post:<%d>",
		    result);
		goto done;
	}
	SDP_CONN_ST_SET(conn, SDP_CONN_ST_RTU_SENT);
done:
	return (result);
}   /* sdp_sock_accept */

/* ========================================================================= */

int32_t
sdp_cm_error(sdp_conn_t *conn, int error)
{
	int result = 0;
	/*
	 * Handle errors within active connections stream.
	 * First generate appropriate response, REJ, DREQ or nothing.
	 * Second the socket must be notified of the error.
	 */

	sdp_print_dbg(conn, "sdp_cm_error: state: <%04x>",
	    conn->state);
	ASSERT(mutex_owned(&conn->conn_lock));
	switch (conn->state) {
	default:
		/*FALLTHRU*/
	case SDP_CONN_ST_REQ_SENT:
	case SDP_CONN_ST_REQ_PATH:
		/*
		 * CM message was never sent.
		 */
		SDP_CONN_ST_SET(conn, SDP_CONN_ST_ERROR);
		/*FALLTHRU*/
	case SDP_CONN_ST_ERROR:
	case SDP_CONN_ST_CLOSED:
		break;
	case SDP_CONN_ST_REP_RECV:
		/*
		 * All four states we have gotten a REP and are now in
		 * one of these states.
		 */
		result = IBT_CM_REJECT;
		SDP_CONN_ST_SET(conn, SDP_CONN_ST_ERROR);
		break;
	case SDP_CONN_ST_TIME_WAIT_1:
		/*FALLTHRU*/
	case SDP_CONN_ST_ESTABLISHED:
		/*
		 * Made it all the way to established, need to initiate a
		 * full disconnect.
		 */
		sdp_cm_disconnect(conn);
		SDP_CONN_ST_SET(conn, SDP_CONN_ST_TIME_WAIT_1);
		break;
	}

	conn->shutdown = SDP_SHUTDOWN_MASK;
	conn->sdpc_tx_max_queue_size = 0;
	conn->error = error;
	if (conn->sdp_ulpd) {
		conn->sdp_ulp_connfailed(conn->sdp_ulpd, error);
	}
	return (result);
}


/* ========================================================================= */

/*
 * sock_abort -- abortive close notification
 */
int32_t
sdp_sock_abort(sdp_conn_t *conn)
{

	SDP_CHECK_NULL(conn, -EINVAL);
	dprint(SDP_DBG, ("sdp_sock_abort: conn:%p state:%x",
	    (void *)conn, conn->state));

	/*
	 * wake the connection in case it's doing anything, and mark it as
	 * closed. leave data in their buffers so the user can blead it dry.
	 */
	conn->sdpc_tx_max_queue_size = 0;
	conn->shutdown = SDP_SHUTDOWN_MASK;

	return (0);
}   /* sdp_sock_abort */

/* ========================================================================= */


void
sdp_cm_to_error(sdp_conn_t *conn)
{
	sdp_conn_report_error(conn, -ECONNRESET);
}

/* --------------------------------------------------------------------- */

/*
 * sdp_abort -- initiate socket dropping.
 */
void
sdp_abort(sdp_conn_t *conn)
{
	ASSERT(mutex_owned(&conn->conn_lock));

	/*
	 * notify both halves of the wall that the connection is being
	 * aborted.
	 */
	(void) sdp_sock_abort(conn);
	sdp_conn_abort(conn);

}
