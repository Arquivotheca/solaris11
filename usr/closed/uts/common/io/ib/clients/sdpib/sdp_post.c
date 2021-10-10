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


#include <sys/ib/clients/sdpib/sdp_main.h>
#include <sys/ib/clients/sdpib/sdp_trace_util.h>

/*
 * connection establishment functions
 */


/* ========================================================================= */

/*
 * sdp_post_path_complete-- path lookup complete, initiate SDP connection
 */
/* ARGSUSED */
static int
sdp_post_path_complete(sdp_lookup_id_t plid,
	uint32_t status,
	sdp_ipx_addr_t *src_addr,
	sdp_ipx_addr_t *dst_addr,
	ibt_path_info_t *path_info,
	ibt_path_attr_t *path_attr,
	uint8_t hw_port, uint16_t pkey, ibt_hca_hdl_t *hca_hdl, void *arg)
{
	sdp_conn_t *conn = (sdp_conn_t *)(uintptr_t)arg;
	sdp_msg_hello_t *hello_msg;
	int32_t result = 0;
	int32_t error = 0;
	sdp_buff_t *buff;
	ib_guid_t path_local_guid;
	ib_gid_t src_gid, dest_gid, alt_src_gid, alt_dest_gid;
	uint16_t pkey_ix;

	sdp_print_ctrl(conn, "sdp_post_path_complete: status :<%d>",
	    status);
	SDP_CONN_LOCK(conn);
	/*
	 * On a sparc system, at times after sending a cm_req request, a short
	 * lived socket connection, closes both the socket and the ib side
	 * connection even before completing through this function. This leads
	 * to connection getting freed up in sdp_ibt_alloc_rc_chan() function
	 * which should not be happening. Hence increment the ref count now
	 * and decrement it at the exit of the fucntion guaranteeing that conn
	 * stays alive throughout this function.
	 */
	ASSERT(conn->sdp_ib_refcnt > 0);
	SDP_CONN_HOLD(conn);

	/*
	 * path lookup is complete
	 */
	if (plid != conn->plid) {
		goto done;
	}
	if (SDP_CONN_ST_REQ_PATH != conn->state) {
		goto done;
	}
	conn->plid = 0;

	if (status != 0) {
		sdp_print_warn(conn, "sdp_post_path_comp: failure: %d",
		    status);
		conn->error = status;
		SDP_STAT_INC(conn->sdp_sdps, AttemptFails);
		SDP_STAT_INC(conn->sdp_sdps, PrFails);
		goto failed;
	}
	conn->hw_port = hw_port;
	path_local_guid = path_info->pi_hca_guid;

	src_gid = path_info->pi_prim_cep_path.cep_adds_vect.av_sgid;
	dest_gid = path_info->pi_prim_cep_path.cep_adds_vect.av_dgid;
	alt_src_gid = path_info->pi_alt_cep_path.cep_adds_vect.av_sgid;
	alt_dest_gid = path_info->pi_alt_cep_path.cep_adds_vect.av_dgid;

	/*
	 * TODO:  make sure we can retry initializing HCA, port
	 */
	ASSERT(path_info->pi_hca_guid != (ib_guid_t)NULL);

	/*
	 * allocate IB resources.
	 */
	result = sdp_conn_allocate_ib(conn, &path_local_guid, hw_port,
	    B_FALSE);
	if (result != 0) {
		sdp_print_warn(conn, "sdp_post_path: fail to alloc_ib:<%d>",
		    result);
		goto failed;
	}

	/*
	 * create the hello message . (don't need to worry about header
	 * space reservation)
	 */
	buff = sdp_buff_get(conn);
	if (buff == NULL) {
		dprint(SDP_WARN, ("post_path: Fail to get buff"));
		goto failed;
	}
	hello_msg = (sdp_msg_hello_t *)buff->sdp_buff_data;
	buff->sdp_buff_tail = (char *)buff->sdp_buff_data +
	    sizeof (sdp_msg_hello_t);

	bzero(hello_msg, sizeof (sdp_msg_hello_t));

	conn->l_advt_bf = min(conn->sdpc_max_rbuffs, conn->rcq_size);

	conn->l_max_adv = SDP_MSG_MAX_ADVS;

	hello_msg->bsdh.recv_bufs = conn->l_advt_bf;
	hello_msg->bsdh.flags = SDP_MSG_FLAG_NON_FLAG;
	hello_msg->bsdh.mid = SDP_MSG_MID_HELLO;
	hello_msg->bsdh.size = sizeof (sdp_msg_hello_t);
	hello_msg->bsdh.seq_num = conn->send_seq;

	hello_msg->bsdh.seq_ack = conn->advt_seq;

	hello_msg->hh.max_adv = conn->l_max_adv;
	hello_msg->hh.ip_ver = (conn->sdp_ipversion == IPV4_VERSION) ?
	    SDP_MSG_IPV4 : SDP_MSG_IPV6;
	hello_msg->hh.version = conn->sdp_msg_version;
	hello_msg->hh.r_rcv_size = conn->sdpc_local_buff_size;
	hello_msg->hh.l_rcv_size = conn->sdpc_local_buff_size;
	hello_msg->hh.port = ntohs(conn->src_port);

	if (conn->sdp_ipversion == IPV6_VERSION) {
		bcopy(&src_addr->ip6addr, &conn->conn_srcv6,
		    sizeof (in6_addr_t));
		bcopy(&dst_addr->ip6addr, &conn->conn_remv6,
		    sizeof (in6_addr_t));

		bcopy(&conn->conn_srcv6, &hello_msg->hh.src.ipv6,
		    sizeof (in6_addr_t));
		bcopy(&conn->conn_remv6, &hello_msg->hh.dst.ipv6,
		    sizeof (in6_addr_t));

		dprint(SDP_DBG, ("sdp_post_path: IP6 "
		    "src_addr:%x %x %x %x \n"
		    "dst_addr:%x %x %x %xsport:%d",
		    conn->conn_srcv6.s6_addr32[0],
		    conn->conn_srcv6.s6_addr32[1],
		    conn->conn_srcv6.s6_addr32[2],
		    conn->conn_srcv6.s6_addr32[3],
		    conn->conn_remv6.s6_addr32[0],
		    conn->conn_remv6.s6_addr32[1],
		    conn->conn_remv6.s6_addr32[2],
		    conn->conn_remv6.s6_addr32[3],
		    hello_msg->hh.port));
	} else {
		if (conn->sdp_ipversion != IPV4_VERSION) {
			sdp_print_warn(conn, "sdp_post_path: IP vers <%d> not"
			    " supported ", conn->sdp_ipversion);
			conn->error = EAFNOSUPPORT;
			goto failed;
		}
		hello_msg->hh.src.ipv4.addr = ntohl(src_addr->ip4addr);
		hello_msg->hh.dst.ipv4.addr = ntohl(dst_addr->ip4addr);
		dprint(SDP_DBG, ("post_path:Hello: conn:%p src_addr:%x "
		    "dst_addr:%x sport:%d", (void *)conn,
		    hello_msg->hh.src.ipv4.addr, hello_msg->hh.dst.ipv4.addr,
		    hello_msg->hh.port));

		IN6_IPADDR_TO_V4MAPPED(src_addr->ip4addr, &conn->conn_srcv6);
		IN6_IPADDR_TO_V4MAPPED(dst_addr->ip4addr, &conn->conn_remv6);
	}

	bcopy(&src_gid, &conn->s_gid, sizeof (conn->s_gid));
	bcopy(&dest_gid, &conn->d_gid, sizeof (conn->d_gid));
	bcopy(&alt_src_gid, &conn->s_alt_gid, sizeof (conn->s_alt_gid));
	bcopy(&alt_dest_gid, &conn->d_alt_gid, sizeof (conn->d_alt_gid));

	/*
	 * * endian swap
	 */
	(void) sdp_msg_host_to_wire_bsdh(&hello_msg->bsdh);
	(void) sdp_msg_host_to_wire_hh(&hello_msg->hh);
	/*
	 * save message
	 */
	buff_pool_put(&conn->send_post, buff);

	result = ibt_pkey2index(conn->hca_hdl, conn->hw_port, pkey, &pkey_ix);
	if (result != 0) {
		sdp_print_warn(conn, "sdp_post_path: fail in pkey2index:<%d>",
		    result);
		goto failed;
	}
	path_info->pi_prim_cep_path.cep_pkey_ix = pkey_ix;
	SDP_CONN_ST_SET(conn, SDP_CONN_ST_REQ_SENT);

	/*
	 * initiate connection; Note that conn_lock can be temporarily released
	 * during the xxx_open_rc_channel() call.  CM reference
	 */
	SDP_CONN_HOLD(conn);
	result = sdp_ibt_alloc_open_rc_channel(conn, path_info, hello_msg);
	if (result != 0) {
		conn->error = ECONNREFUSED;
		sdp_print_warn(conn, "sdp_post_path: fail in ibt_alloc:<%d>",
		    result);
		goto failed;
	}

	goto done;

failed:
	sdp_print_dbg(conn, "sdp_post_path: fail to open IB conn:<%d>",
	    result);

	(void) sdp_cm_error(conn, conn->error);
	/*
	 * If we are in this state in failed section, we are doing
	 * delayed cleanup. Reset the error to 0, so that pr_callback
	 * cleans up wqnp, cm_disconnect in cm_actv_error will lead
	 * to dropping the ref for this conn.
	 */
	if (conn->state == SDP_CONN_ST_TIME_WAIT_1) {
		error = 0;
	}
done:
	SDP_CONN_UNLOCK(conn);
	SDP_CONN_PUT(conn); /* Drop the refcnt held earlier in this function */
	SDP_CONN_PUT(conn); /* Drop the refcnt held before sdp_pr_lookup() */
	return (error);
}

/*
 * sdp_post_msg_hello -- initiate a SDP connection with a hello message.
 */
int32_t
sdp_post_msg_hello(sdp_conn_t *conn)
{
	int32_t result;

	SDP_CHECK_NULL(conn, -EINVAL);

	ASSERT(conn->sdp_global_state->hca_list != NULL);

	SDP_CONN_ST_SET(conn, SDP_CONN_ST_REQ_PATH);

	/*
	 * lookup the remote address
	 * increment refcnt for lookup
	 */
	SDP_CONN_HOLD(conn);
	SDP_CONN_UNLOCK(conn);

	result = sdp_pr_lookup(conn, conn->inet_ops.localroute, 0,
	    sdp_post_path_complete, (void *)(uintptr_t)conn, &conn->plid);

	SDP_CONN_LOCK(conn);
	/*
	 * drop the lookup reference.
	 */
	if (result != 0) {
		sdp_print_warn(conn, "sdp_post_msg_hello: fail in lookup:"
		    " <%d>", result);
		/*
		 * callback dosn't have this socket.
		 */
		conn->error = result;
		/*
		 * The connection will not be freed by this PUT, since this
		 * function is called by connect() and there is at least
		 * a sock refcnt held.
		 */
		SDP_CONN_PUT(conn);
	}	/* if */
	return (result);
}	/* sdp_post_msg_hello */

/* ========================================================================= */

/*
 * sdp_post_msg_hello_ack -- respond to a connection attempt with an ack
 */
int32_t
sdp_post_msg_hello_ack(sdp_conn_t *conn)
{
	sdp_msg_hello_ack_t *hello_ack;
	int32_t result;
	sdp_buff_t *buff;

	SDP_CHECK_NULL(conn, -EINVAL);

	/*
	 * build listen response headers
	 */
	if (sizeof (sdp_msg_hello_ack_t) > conn->sdpc_local_buff_size) {
		result = -ENOBUFS;
		goto error;
	}
	/*
	 * get a buffer, in which we will create the hello header ack.
	 * (don't need to worry about header space reservation on sends)
	 */
	buff = sdp_buff_get(conn);
	if (buff == NULL) {
		result = -ENOMEM;
		goto error;
	}
	hello_ack = (sdp_msg_hello_ack_t *)buff->sdp_buff_data;
	buff->sdp_buff_tail = (char *)buff->sdp_buff_data +
	    sizeof (sdp_msg_hello_ack_t);

	/*
	 * create the message
	 */
	bzero(hello_ack, sizeof (sdp_msg_hello_ack_t));

	conn->l_advt_bf = min(conn->sdpc_max_rbuffs, conn->rcq_size);

	conn->l_max_adv = SDP_MSG_MAX_ADVS;

	hello_ack->bsdh.recv_bufs = conn->l_advt_bf;
	hello_ack->bsdh.flags = SDP_MSG_FLAG_NON_FLAG;
	hello_ack->bsdh.mid = SDP_MSG_MID_HELLO_ACK;
	hello_ack->bsdh.size = sizeof (sdp_msg_hello_ack_t);
	hello_ack->bsdh.seq_num = conn->send_seq;
	hello_ack->bsdh.seq_ack = conn->advt_seq;

	hello_ack->hah.max_adv = conn->l_max_adv;
	hello_ack->hah.version = conn->sdp_msg_version;
	hello_ack->hah.l_rcv_size = conn->sdpc_local_buff_size;

	/*
	 * endian swap
	 */
	(void) sdp_msg_host_to_wire_bsdh(&hello_ack->bsdh);
	(void) sdp_msg_host_to_wire_hah(&hello_ack->hah);
	/*
	 * save message
	 */
	buff_pool_put(&conn->send_post, buff);

	/*
	 * post receive buffers for this connection
	 */
	result = sdp_recv_post(conn);
	if (result < 0) {
		sdp_print_warn(conn, "post_msg_hello_ack: error <%d> posting "
		    "receive buffers.", result);
		if (conn->inet_ops.debug) {
			(void) strlog(SDP_MODULE_ID, 0, 1,
			    SL_TRACE | SL_ERROR,
			    "post_msg_hello_ack: error posting <%d> "
			    "receive buffers. <%d>",
			    conn->sdpc_max_rbuffs, result);
		}
		goto error;
	}

	return (0);
error:
	return (result);
}	/* sdp_post_msg_hello_ack */

/* ========================================================================= */


/*
 * sdp_cm_disconnect -- initiate a disconnect request using the CM
 */
void
sdp_cm_disconnect(sdp_conn_t *conn)
{
	ASSERT(conn != NULL);
	/*
	 * Close the channel here
	 * The channel is freed in the destructor.
	 */
	(void) ibt_close_rc_channel(conn->channel_hdl,
	    IBT_NONBLOCKING, NULL, 0, NULL, NULL, 0);
	/*
	 * Make sure any sleeping threads are woken up
	 */
	cv_signal(&conn->ss_txdata_cv);
	cv_signal(&conn->ss_rxdata_cv);
}
