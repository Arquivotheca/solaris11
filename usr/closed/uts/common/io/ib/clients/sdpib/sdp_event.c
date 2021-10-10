/*
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
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
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
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

/*
 * sdp_event_hello_validate -- validate the hello header
 */
static int32_t
sdp_event_hello_validate(sdp_msg_hello_t *msg_hello, int32_t size)
{

	if (size != sizeof (sdp_msg_hello_t)) {
		dprint(SDP_WARN, ("hello msg: size mismatch"));
		return (-EINVAL);
	}

	/*
	 * endian swap
	 */
	(void) sdp_msg_wire_to_host_bsdh(&msg_hello->bsdh);
	(void) sdp_msg_wire_to_host_hh(&msg_hello->hh);

	/*
	 * validation and consistency checks
	 */
	if (msg_hello->bsdh.size != sizeof (sdp_msg_hello_t)) {
		dprint(SDP_WARN, ("hello msg: bsdh size mismatch"));
		return (-EINVAL);
	}
	if (msg_hello->bsdh.mid != SDP_MSG_MID_HELLO) {
		dprint(SDP_WARN, ("hello msg: Mid Hello not supported"));
		return (-EINVAL);
	}
	if (!(msg_hello->hh.max_adv > 0)) {
		dprint(SDP_WARN, ("hello msg: max advt less that zero"));
		return (-EINVAL);
	}
	if (((0xf0 & msg_hello->hh.version) != (0xf0 & SDP_MSG_VERSION)) &&
	    ((0xf0 & msg_hello->hh.version) != (0xf0 & SDP_MSG_VERSION_OLD))) {
		dprint(SDP_WARN, ("hello msg: version mismatch"));
		return (-EINVAL);
	}

	return (0);
}

/*
 * sdp_event_hello_ack_validate -- validate the hello ack header
 */
static int32_t
sdp_event_hello_ack_validate(sdp_msg_hello_ack_t *hello_ack, int32_t size)
{

	if (size != sizeof (sdp_msg_hello_ack_t)) {
		dprint(SDP_WARN, ("Hello ack: size mismatch"));
		return (EINVAL);
	}

	/*
	 * endian swap
	 */
	(void) sdp_msg_wire_to_host_bsdh(&hello_ack->bsdh);
	(void) sdp_msg_wire_to_host_hah(&hello_ack->hah);

	/*
	 * validation and consistency checks
	 */
	if (hello_ack->bsdh.size != sizeof (sdp_msg_hello_ack_t)) {
		dprint(SDP_WARN, ("Hello ack: BSDH size mismatch"));
		return (EINVAL);
	}

	if (hello_ack->bsdh.mid != SDP_MSG_MID_HELLO_ACK) {
		dprint(SDP_WARN, ("Hello ack: Mid hello ack"));
		return (EINVAL);
	}

	if (!(hello_ack->hah.max_adv > 0)) {
		dprint(SDP_WARN, ("Hello ack: adv less than zero"));
		return (EINVAL);
	}

	if (((0xf0 & hello_ack->hah.version) != (0xf0 & SDP_MSG_VERSION)) &&
	    ((0xf0 & hello_ack->hah.version) != (0xf0 & SDP_MSG_VERSION_OLD))) {
		dprint(SDP_WARN, ("Hello ack: version mismatch"));
		return (EINVAL);
	}

	return (0);
}

/*
 * state specific connection managment callback functions
 */

/*
 * sdp_cm_req_recv -- handler for passive connection open completion
 */
/* ARGSUSED */
static ibt_cm_status_t
sdp_cm_req_recv(void *arg, ibt_cm_event_t *ibt_cm_event,
    ibt_cm_return_args_t *ret_args, void *ret_priv_data,
    ibt_priv_data_len_t ret_len_max)
{
	sdp_conn_t *conn;
	sdp_buff_t *buff;
	sdp_msg_hello_t *msg_hello;
	uint16_t port;
	netstack_t *ns;
	sdp_stack_t *sdps;

	ns = netstack_find_by_stackid(GLOBAL_NETSTACKID);
	sdps = ns->netstack_sdp;
	/*
	 * We can safely release the reference since it is the global zone.
	 */
	netstack_rele(ns);

	/*
	 * create a connection for this request.
	 */
	msg_hello = (sdp_msg_hello_t *)ibt_cm_event->cm_priv_data;
	if (sdp_event_hello_validate(msg_hello,
	    ibt_cm_event->cm_priv_data_len) < 0) {
		SDP_STAT_INC(sdps, CmFails);
		return (IBT_CM_REJECT);
	}

	conn = sdp_conn_allocate(B_TRUE);
	if (conn == NULL)
		return (IBT_CM_REJECT);

	sdp_print_ctrl(conn, "recv <CM REQ> recvd: conn:%p", (void *)conn);
	SDP_CONN_LOCK(conn);

	switch (msg_hello->hh.ip_ver) {
		case SDP_MSG_IPV4:
			dprint(SDP_DBG, ("sdp_cm_req_recv: conn:%p src addr:%x"
			    "dst addr:%x", (void *)conn,
			    msg_hello->hh.src.ipv4.addr,
			    msg_hello->hh.dst.ipv4.addr));

			conn->sdp_ipversion = IPV4_VERSION;
			conn->sdp_family = AF_INET;

			msg_hello->hh.dst.ipv4.addr =
			    htonl(msg_hello->hh.dst.ipv4.addr);
			msg_hello->hh.src.ipv4.addr =
			    htonl(msg_hello->hh.src.ipv4.addr);

			IN6_IPADDR_TO_V4MAPPED(msg_hello->hh.dst.ipv4.addr,
			    &conn->conn_srcv6);
			IN6_IPADDR_TO_V4MAPPED(msg_hello->hh.src.ipv4.addr,
			    &conn->conn_remv6);

			break;
		case SDP_MSG_IPV6:
			conn->sdp_ipversion = IPV6_VERSION;
			conn->sdp_family = AF_INET6;
			bcopy(&msg_hello->hh.dst.ipv6, &conn->conn_srcv6,
			    sizeof (in6_addr_t));
			bcopy(&msg_hello->hh.src.ipv6, &conn->conn_remv6,
			    sizeof (in6_addr_t));

			break;
		default:
			goto error;

	}

	port =
	    SDP_MSG_SID_TO_PORT(ibt_cm_event->cm_event.req.req_service_id);
	conn->src_port = htons(port);

	conn->dst_port = htons(msg_hello->hh.port);
	dprint(SDP_DBG, ("sdp_cm_req_recv: conn:%p ports:<%u:%u>",
	    (void *)conn, port, conn->dst_port));

	SDP_CONN_ST_SET(conn, SDP_CONN_ST_REQ_RECV);
	bcopy(&ibt_cm_event->cm_event.req.req_prim_addr.av_sgid,
	    &conn->s_gid, sizeof (conn->s_gid));
	bcopy(&ibt_cm_event->cm_event.req.req_prim_addr.av_dgid,
	    &conn->d_gid, sizeof (conn->d_gid));
	bcopy(&ibt_cm_event->cm_event.req.req_alt_addr.av_sgid,
	    &conn->s_alt_gid, sizeof (conn->s_alt_gid));
	bcopy(&ibt_cm_event->cm_event.req.req_alt_addr.av_dgid,
	    &conn->d_alt_gid, sizeof (conn->d_alt_gid));

	conn->d_qpn = ibt_cm_event->cm_event.req.req_remote_qpn;

	/*
	 * read remote information
	 */
	conn->sdpc_remote_buff_size = msg_hello->hh.l_rcv_size;
	conn->r_max_adv = msg_hello->hh.max_adv;
	conn->r_recv_bf = msg_hello->bsdh.recv_bufs;
	conn->recv_seq = msg_hello->bsdh.seq_num;
	conn->advt_seq = msg_hello->bsdh.seq_num;
	conn->sdp_msg_version = msg_hello->hh.version &0xff;

	/*
	 * initiate a connection to the stream interface.
	 */
	if (sdp_sock_connect(conn, ibt_cm_event) != 0) {
		sdp_print_warn(conn, "sdp_sock_connect: fail: <%d>",
		    conn->error);
		goto error;
	}
	ret_args->cm_ret.rep.cm_channel = conn->channel_hdl;
	ret_args->cm_ret.rep.cm_rdma_ra_out = 1;
	ret_args->cm_ret.rep.cm_rdma_ra_in = 1;
	ret_args->cm_ret.rep.cm_rnr_retry_cnt = sdp_path_rnr_retry_cnt;
	ret_args->cm_ret_len = sizeof (sdp_msg_hello_ack_t);

	/*
	 * we stored the message in the conn send_queue back in
	 * post_msg_hello_ack.  we need to get it from the queue.
	 */
	buff = buff_pool_get_head(&conn->send_post);
	ASSERT(buff != NULL);
	bcopy(buff->sdp_buff_data, ret_priv_data, sizeof (sdp_msg_hello_ack_t));
	sdp_buff_free(buff);

	/*
	 * we can get another interrupt before releasing the lock,
	 * and we need to avoid deadlock with the conn table lock
	 */
	SDP_CONN_UNLOCK(conn);
	return (IBT_CM_ACCEPT);
error:
	sdp_print_dbg(conn, "sdp_cm_req_recv: fail <%d> to estab connection",
	    conn->error);
	SDP_CONN_ST_SET(conn, SDP_CONN_ST_CLOSED);
	SDP_CONN_UNLOCK(conn);
	SDP_CONN_PUT(conn);	/* CM sk reference */
	SDP_STAT_INC(sdps, AttemptFails);
	return (IBT_CM_REJECT);

}   /* sdp_cm_req_recv */

/* ========================================================================= */

/*
 * sdp_cm_rep_recv -- handler for active connection open completion
 */
/* ARGSUSED */
static ibt_cm_status_t
sdp_cm_rep_recv(sdp_conn_t *conn,
		    ibt_cm_event_t *ibt_cm_event,
		    ibt_cm_return_args_t *ret_args,
		    void *priv_data, ibt_priv_data_len_t ret_len)
{
	sdp_buff_t *buff;
	int32_t error = 0;
	sdp_msg_hello_ack_t *hello_ack;
	sdp_stack_t *sdps = conn->sdp_sdps;

	SDP_CHECK_NULL(ibt_cm_event->cm_priv_data, IBT_CM_REJECT);

	hello_ack = (sdp_msg_hello_ack_t *)ibt_cm_event->cm_priv_data;
	sdp_print_ctrl(conn, "sdp_cm_rep_recv: <CM REP> recvd, state:%04x",
	    conn->state);

	if ((conn->state != SDP_CONN_ST_REQ_SENT) &&
	    (conn->state != SDP_CONN_ST_REQ_PATH)) {
		error = ECONNRESET;
		goto reject;
	}

	/*
	 * check hello header ack, to determine if we want the
	 * connection.
	 */
	error = sdp_event_hello_ack_validate(hello_ack,
	    ibt_cm_event->cm_priv_data_len);
	if (error != 0) {
		sdp_print_warn(conn, "sdp_cm_rep_recv: fail <%d> in hello ack",
		    error);
		if (conn->inet_ops.debug) {
			(void) strlog(SDP_MODULE_ID, 0, 1,
			    SL_TRACE | SL_ERROR, "sdp_cm_rep_recv: "
			    "hello_ack_validate failed");
		}
		SDP_STAT_INC(sdps, AttemptFails);
		goto reject;
	}	/* if */
	SDP_CONN_ST_SET(conn, SDP_CONN_ST_REP_RECV);

	/*
	 * read remote information
	 */
	conn->sdpc_remote_buff_size = hello_ack->hah.l_rcv_size;
	conn->r_max_adv = hello_ack->hah.max_adv;
	conn->r_recv_bf = hello_ack->bsdh.recv_bufs;
	conn->recv_seq = hello_ack->bsdh.seq_num;
	conn->advt_seq = hello_ack->bsdh.seq_num;

	/*
	 * pop the hello message that was sent
	 */
	buff = buff_pool_get_head(&conn->send_post);
	sdp_buff_free(buff);

	error = sdp_actv_connect(conn);
	if (error != 0) {
		goto reject;
	}	/* if */
	ASSERT(mutex_owned(&conn->conn_lock));
	return (IBT_CM_ACCEPT);

reject:
	sdp_print_dbg(conn, "sdp_cm_rep_recv: fail <%d>", error);
	(void) sdp_cm_error(conn, error);
	return (IBT_CM_REJECT);
}   /* sdp_cm_rep_recv */

/* ========================================================================= */

/*
 * sdp_cm_idle -- handler for connection idle completion
 */
/* ARGSUSED */
static int32_t
sdp_cm_idle(sdp_conn_t *conn, ibt_cm_event_t *ibt_cm_event)
{
	/* LINTED E_FUNC_SET_NOT_USED */
	int32_t result;

	SDP_CHECK_NULL(conn, IBT_CM_REJECT);

	ASSERT(mutex_owned(&conn->conn_lock));
	sdp_print_ctrl(conn, "sdp_cm_idle: <CM Idle> state:<%04x>",
	    conn->state);

	/*
	 * IDLE should only be called after some other action on the
	 * connection, which means the callback argument will be a SDP conn,
	 * since the only time it is not a SDP conn is the first callback in
	 * a passive open.
	 */

	/*
	 * check state
	 */
	switch (conn->state) {
		case SDP_CONN_ST_REQ_PATH:
			/*
			 * cancel address resolution
			 */
			result = sdp_pr_lookup_cancel(conn->plid);
			SDP_EXPECT(!(0 > result));
			(void) sdp_cm_error(conn, ECONNREFUSED);

			/*
			 * If there is failure in REQ_PATH/SENT state, we have
			 * not the refcnt yet in sdp_post_path_complete. So do
			 * not decrement the refcnt here, just exit. Besides
			 * if we are here in REQ_PATH/SENT state, we have some
			 * error either while sending req, receiving rep/path
			 * resolution incomplete.
			 */

			break;
		case SDP_CONN_ST_REQ_SENT:
			(void) sdp_cm_error(conn, ECONNREFUSED);
			break;
		case SDP_CONN_ST_REQ_RECV:
		case SDP_CONN_ST_ESTABLISHED:
			sdp_conn_report_error(conn, -ECONNREFUSED);
			break;
		case SDP_CONN_ST_TIME_WAIT_1:
			sdp_print_note(conn, "sdp_cm_idle: unexpected conn"
			    "state <%x>", conn->state);
			    /*FALLTHRU*/
		case SDP_CONN_ST_CLOSED:
		case SDP_CONN_ST_ERROR:
		case SDP_CONN_ST_TIME_WAIT_2:
			/*
			 * Connection is finally dead. Drop the CM reference
			 */
			break;
		default:
			sdp_print_note(conn, "sdp_cm_idle: unknown conn"
			    "state <%x>", conn->state);
			break;
	}

done:
	return (0);
}



/* ========================================================================= */

/*
 * sdp_cm_established -- handler for connection established completion
 */
/* ARGSUSED */
static ibt_cm_status_t
sdp_cm_established(sdp_conn_t *conn, ibt_cm_event_t *ibt_cm_event,
    ibt_cm_return_args_t *ret_args, void *priv_data,
    ibt_priv_data_len_t ret_len_max)
{
	sdp_stack_t *sdps = conn->sdp_sdps;
	int32_t result = 0;

	sdp_print_ctrl(conn, "sdp_cm_established: <CM Estab> state:<%04x>",
	    conn->state);
	/*
	 * check state
	 */
	switch (conn->state) {
		/*
		 * active open, Now RTU sent
		 */
		case SDP_CONN_ST_RTU_SENT:
			SDP_CONN_ST_SET(conn, SDP_CONN_ST_ESTABLISHED);
			conn->sdp_active_open = B_TRUE;
			/*
			 * release disconnects.
			 */
			sdp_send_flush(conn);
			conn->sdp_ulp_connected(conn->sdp_ulpd);
			SDP_STAT_INC(sdps, ActiveOpens);
			SDP_STAT_INC(sdps, CurrEstab);
			result = ibt_enable_cq_notify(conn->rcq_hdl,
			    IBT_NEXT_COMPLETION);
			if (result != IBT_SUCCESS) {
				sdp_print_warn(conn, "sdp_cm_established: "
				    "ibt_enable_cq_notify(rcq) "
				    "failed: status %d", result);
				goto error;
			}
			break;
		case SDP_CONN_ST_DIS_SEND_1:
		case SDP_CONN_ST_DIS_RECV_R:
		case SDP_CONN_ST_DIS_SEND_2:
			break;

			/* passive open: REP sent: */
		case SDP_CONN_ST_REQ_RECV:
			/*
			 *  send the T_CON_IND message to sockfs
			 */
			result = sdp_pass_establish(conn);
			if (result != 0) {
				sdp_print_warn(conn, "sdp_cm_established:"
				    " sdp_pass_estab: fail: <%d>", result);
				goto error;
			}	/* if */
			SDP_CONN_ST_SET(conn, SDP_CONN_ST_ESTABLISHED);
			result = ibt_enable_cq_notify(conn->rcq_hdl,
			    IBT_NEXT_COMPLETION);
			if (result != IBT_SUCCESS) {
				sdp_print_warn(conn, "sdp_cm_established: "
				    "ibt_enable_cq_notify(rcq) "
				    "failed: status %d", result);
				goto error;

			}
			break;

		case SDP_CONN_ST_CLOSED:
		case SDP_CONN_ST_ERROR:
			/*
			 * Sockets has released reference, begin abortive
			 * disconnect. Leave state unchanged, time_wait and
			 * idle will handle the existing state correctly.
			 */
			sdp_cm_disconnect(conn);
			break;
		case SDP_CONN_ST_ESTABLISHED:
			break;
		default:
			result = -EINVAL;
			goto error;
	}	/* switch */
	return (IBT_CM_ACCEPT);
error:
	sdp_cm_to_error(conn);
	conn->error = result;
	return (IBT_CM_REJECT);
}   /* sdp_cm_established */


/* ========================================================================= */

/*
 * sdp_cm_time_wait -- handler for connection time wait completion
 */
/* ARGSUSED */
static int32_t
sdp_cm_time_wait(sdp_conn_t *conn, ibt_cm_event_t *ibt_cm_event)
{
	int32_t result = 0;

	/*
	 * clear out posted receives now, vs after IDLE timeout, which
	 * consumes too many buffers when lots of connections are being
	 * established and torn down. here is a good spot since we know that
	 * the QP has gone to reset, and pretty much all take downs end up
	 * here.
	 */
	(void) sdp_buff_pool_clear(&conn->recv_post);

	sdp_print_ctrl(conn, "sdp_cm_timewait: <CM Timewait> state:<%04x>",
	    conn->state);
	/*
	 * check state
	 */
	switch (conn->state) {
		case SDP_CONN_ST_CLOSED:
		case SDP_CONN_ST_ERROR:
			/*
			 * error on stream intf, no more call to/from those
			 * interfaces.
			 */
			break;
		case SDP_CONN_ST_DIS_RECV_R:
		case SDP_CONN_ST_DIS_SEND_2:
		case SDP_CONN_ST_TIME_WAIT_1:
			/*
			 * SDP disconnect messages have been exchanged, and
			 * DREQ/DREP received, wait for idle timer.
			 */
			SDP_CONN_ST_SET(conn, SDP_CONN_ST_TIME_WAIT_2);
			break;
		case SDP_CONN_ST_DIS_SEND_1:
		case SDP_CONN_ST_DIS_SENT_1:
		case SDP_CONN_ST_DIS_RECV_1:
			/*
			 * connection is being closed without a discon msg,
			 * abortive close.
			 */
		case SDP_CONN_ST_ESTABLISHED:
			/*
			 * Change state, so we only need to wait for the abort
			 * callback, and idle. Call the abort callback.
			 */
			SDP_CONN_ST_SET(conn, SDP_CONN_ST_TIME_WAIT_2);
			sdp_conn_abort(conn);
			break;
		default:
			sdp_print_warn(conn, "sdp_cm_timewait: unexpected "
			    "conn state <%04x>", conn->state);
			sdp_cm_to_error(conn);
			result = -EINVAL;
			break;
	}

	return (result);
}


/*
 * primary connection managment callback function
 */

/* ========================================================================= */

/*
 * cm_event_handler -- handler for CM state transitions request
 */
ibt_cm_status_t
cm_event_handler(void *arg,
		ibt_cm_event_t *ibt_cm_event,
		ibt_cm_return_args_t *ret_args,
		void *ret_priv_data, ibt_priv_data_len_t ret_len_max)
{
	sdp_conn_t *conn = NULL;
	ibt_cm_status_t result = 0;
	sdp_stack_t *sdps;

	if (ibt_cm_event->cm_type == IBT_CM_EVENT_REQ_RCV) {
		result = sdp_cm_req_recv(arg, ibt_cm_event, ret_args,
		    ret_priv_data, ret_len_max);
	} else if (ibt_cm_event->cm_channel != 0) {
		conn = (sdp_conn_t *)(uintptr_t)ibt_get_chan_private(
		    ibt_cm_event->cm_channel);
		if (conn == NULL) {
			return (IBT_CM_REJECT);
		}
		SDP_CONN_TABLE_VERIFY(conn);
		DTRACE_PROBE2(sdp_cm_event_handler, sdp_conn_t *, conn,
		    ibt_cm_event_t *, ibt_cm_event);

		sdps = conn->sdp_sdps;
		switch (ibt_cm_event->cm_type) {
			case IBT_CM_EVENT_CONN_EST:
				result = sdp_cm_established(conn,
				    ibt_cm_event, ret_args, ret_priv_data,
				    ret_len_max);
				break;
			case IBT_CM_EVENT_REP_RCV:
				result = sdp_cm_rep_recv(conn, ibt_cm_event,
				    ret_args, ret_priv_data,
				    ret_args->cm_ret_len);
				break;
			case IBT_CM_EVENT_CONN_CLOSED:
				result = sdp_cm_time_wait(conn, ibt_cm_event);
				result = sdp_cm_idle(conn, ibt_cm_event);
				SDP_CONN_UNLOCK(conn);
				SDP_CONN_PUT(conn); /* CM reference */
				SDP_STAT_DEC(sdps, CurrEstab);
				conn = NULL;
				break;

			case IBT_CM_EVENT_FAILURE:
				result = sdp_cm_idle(conn, ibt_cm_event);
				SDP_CONN_UNLOCK(conn);
				SDP_CONN_PUT(conn); /* CM reference */
				conn = NULL;
				SDP_STAT_INC(sdps, AttemptFails);
				SDP_STAT_INC(sdps, CmFails);
				break;
			case IBT_CM_EVENT_LAP_RCV:
			case IBT_CM_EVENT_APR_RCV:
			case IBT_CM_EVENT_MRA_RCV:
				break;
			default:
				SDP_STAT_INC(sdps, CmFails);
				result = IBT_CM_REJECT;
		}

	} else {
		SDP_STAT_INC(sdps, CmFails);
		result = IBT_CM_REJECT;
	}

	if (conn != NULL) {
		SDP_CONN_UNLOCK(conn);
	}

	return (result);

}   /* cm_event_handler */
