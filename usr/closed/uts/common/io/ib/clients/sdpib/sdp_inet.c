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


#include <sys/policy.h>
#include <sys/ib/clients/sdpib/sdp_main.h>
#include <sys/socketvar.h>
#include <sys/ib/clients/sdpib/sdp_trace_util.h>

/*
 * During initialization the driver registers SDP_BUFFER_COUNT_MIN buffers
 * and tries to assign them individually to each conenction on need basis.
 * If all these buffers are in flight, then more are assigned upto
 * SDP_BUFFER_COUNT_MAX level.
 */
static int32_t _buff_min = SDP_BUFFER_COUNT_MIN;
static int32_t _buff_max = SDP_BUFFER_COUNT_MAX;
static int32_t _conn_size = SDP_DEV_SK_LIST_SIZE;

/*
 * set it this way.
 */
int32_t sdp_smallest_nonpriv_port = 1024;
extern sdp_state_t *sdp_global_state;
/*
 * internal socket/handle managment functions
 */

/* ========================================================================= */

/*
 * sdp_inet_post_disconnect -- disconnect a connection.
 */
static int32_t
sdp_inet_post_disconnect(sdp_conn_t *conn)
{
	int32_t result = 0;

	SDP_CHECK_NULL(conn, -EINVAL);
	/* CONSTCOND */
	sdp_print_ctrl(conn, "sdp_post_discon: discon conn, state:<%04x>",
	    conn->state);

	conn->sdpc_tx_max_queue_size = 0;
	/*
	 * Generate a Disconnect message, and mark self as disconnecting.
	 */
	switch (conn->state) {
		case SDP_CONN_ST_REQ_PATH:
		case SDP_CONN_ST_REQ_SENT:
			/*
			 * outstanding request. Mark it in error, and
			 * completions needs to complete the closing.
			 */
			SDP_CONN_ST_SET(conn, SDP_CONN_ST_ERROR);
			break;
		case SDP_CONN_ST_REQ_RECV:
		case SDP_CONN_ST_REP_RECV:
		case SDP_CONN_ST_ESTABLISHED:
			/*
			 * Attempt to send a disconnect message
			 */
			SDP_CONN_ST_SET(conn, SDP_CONN_ST_DIS_SEND_1);
			sdp_send_ctrl_disconnect(conn);
			break;
		case SDP_CONN_ST_DIS_RECV_1:
			/*
			 * Change state, and send a disconnect request
			 */
			SDP_CONN_ST_SET(conn, SDP_CONN_ST_DIS_SEND_2);
			sdp_send_ctrl_disconnect(conn);
			break;
		case SDP_CONN_ST_TIME_WAIT_1:
		case SDP_CONN_ST_TIME_WAIT_2:
		case SDP_CONN_ST_ERROR:
		case SDP_CONN_ST_CLOSED:
			break;
		default:
			/* CONSTCOND */
			sdp_print_warn(conn, "sdp_inet_post_discon: "
			    "Incorrect state for disconn: <%x>", conn->state);
			result = -EBADE;
			goto error;
	}
	return (0);
error:
	/*
	 * abortive close.
	 */
	sdp_conn_report_error(conn, -ECONNRESET);
	sdp_cm_disconnect(conn);
	return (result);
}   /* sdp_inet_post_disconnect */

/*
 * Wait till the channel is fully disconnected
 */
int32_t
sdp_do_linger(sdp_conn_t *conn)
{
	clock_t	lend;

	if (conn->closeflags & (FNDELAY|FNONBLOCK)) {
		return (EWOULDBLOCK);
	}

	lend = conn->inet_ops.lingertime;
	if (cv_reltimedwait(&conn->closecv, &conn->conn_lock,
	    lend, TR_CLOCK_TICK) == -1) {
		return (EIO);
	}
	return (0);
}

/*
 * sdp_inet_release:  release/close a socket
 */
int32_t
sdp_inet_release(sdp_conn_t *conn)
{
	int32_t result = 0;

	SDP_CHECK_NULL(conn, -EINVAL);

	/* CONSTCOND */
	sdp_print_ctrl(conn, "sdp_inet_release: Close socket, state:<%04x>",
	    conn->state);

	if (conn != NULL) {

		/*
		 * clear out sock, so we only do this once.
		 */
		conn->shutdown = SDP_SHUTDOWN_MASK;
		if (conn->state == SDP_CONN_ST_LISTEN) {
			/*
			 * stop listening
			 */
			result = sdp_inet_listen_stop(conn);
		} else {
			/*
			 * if there is data in the receive queue,
			 * flush it, and consider this an abort. otherwise
			 * consider this a gracefull close.
			 */
			dprint(SDP_DBG, ("sdp_inet_release: sdpc_max_rwin:%d"
			    "src_recv:%d bytes on recv queue:%d",
			    conn->sdpc_max_rwin, conn->src_recv,
			    conn->sdp_recv_byte_strm));

			if (buff_pool_size(&conn->recv_pool) > 0 ||
			    conn->src_recv > 0 ||
			    (conn->inet_ops.linger > 0 &&
			    conn->inet_ops.lingertime == 0)) {
				sdp_conn_abort(conn);
				/* CONSTCOND */
				sdp_print_dbg(conn, "sdp_inet_release: "
				    "data present: aborting conn: %d",
				    conn->sdp_ib_refcnt);
			} else {
				/*
				 * disconnect. (state dependant)
				 */
				result = sdp_inet_post_disconnect(conn);
			}   /* else */
		}   /* else */
	}   /* if */
done:
	return (result);
}   /* sdp_inet_release */

/* ========================================================================= */

/*
 * sdp_inet_bind4 -- bind a socket to an ipv4 address/interface
 */
int32_t
sdp_inet_bind4(sdp_conn_t *conn, sin_t *addr, int32_t size)
{
	int32_t result;
	uint16_t requested_port;
	sdp_stack_t *sdps = conn->sdp_sdps;

	SDP_CHECK_NULL(addr, EINVAL);
	ASSERT(mutex_owned(&conn->conn_lock));

	if (size < sizeof (struct sockaddr_in))
		return (EINVAL);

	/*
	 * basically we're OK with INADDR_ANY or a local interface
	 */
	requested_port = ntohs(addr->sin_port);
	/* CONSTCOND */
	sdp_print_ctrl(conn, "sdp_bind4: bind on port :<%d>", requested_port);

	if ((requested_port != 0) && (requested_port <
	    sdp_smallest_nonpriv_port ||
	    sdp_is_extra_priv_port(sdps, requested_port))) {
		if (secpolicy_net_privaddr(conn->sdp_credp, requested_port,
		    PROTO_SDP) != 0) {
			/* CONSTCOND */
			sdp_print_warn(conn, "sdp_inet_bind4: no permission "
			    "to bind to port <%u>\n",
			    requested_port);
			if (conn->inet_ops.debug) {
				(void) strlog(SDP_MODULE_ID, 0, 1,
				    SL_TRACE | SL_ERROR,
				    "sdp_inet_bind4: no permission for conn"
				    " <%d> to bind to port <%u>\n",
				    conn->sdp_hashent, requested_port);
			}
			result = EACCES;
			goto done;
		}
	}
	IN6_INADDR_TO_V4MAPPED(&addr->sin_addr, &conn->conn_srcv6);
	result = sdp_inet_port_get(conn, sdp_binding_v4, requested_port);
	if (result != 0) {
		/* CONSTCOND */
		sdp_print_warn(conn, "sdp_inet_bind: error getting free port "
		    "during bind. <%d>", result);
		if (conn->inet_ops.debug) {
			(void) strlog(SDP_MODULE_ID, 0, 1,
			    SL_TRACE | SL_ERROR,
			    "sdp_inet_bind: error getting port "
			    "during bind. <%d>", result);
		}
		goto done;
	}
	conn->sdp_bound_state = sdp_bound_v4;
	result = 0;
done:
	return (result);
}   /* sdp_inet_bind4 */

/*
 * sdp_inet_bind6 -- bind a socket to an ipv6 address/interface
 */
int32_t
sdp_inet_bind6(sdp_conn_t *conn, sin6_t *addr6, int32_t size)
{
	int32_t result;
	in6_addr_t *v6srcp;
	uint16_t requested_port;
	sdp_stack_t *sdps = conn->sdp_sdps;

	SDP_CHECK_NULL(addr6, EINVAL);
	if (size < sizeof (struct sockaddr_in6)) {
		return (EINVAL);
	}

	requested_port = ntohs(addr6->sin6_port);
	/* CONSTCOND */
	sdp_print_ctrl(conn, "sdp_inet_bind6: bind on port : %u",
	    requested_port);

	if ((requested_port != 0) && (requested_port <
	    sdp_smallest_nonpriv_port ||
	    sdp_is_extra_priv_port(sdps, requested_port))) {
		if (secpolicy_net_privaddr(conn->sdp_credp, requested_port,
		    PROTO_SDP) != 0) {
			/* CONSTCOND */
			sdp_print_warn(conn, "sdp_inet_bind6: no permission "
			    "to bind to port <%u>\n",
			    requested_port);
			if (conn->inet_ops.debug) {
				(void) strlog(SDP_MODULE_ID, 0, 1,
				    SL_TRACE | SL_ERROR,
				    "sdp_inet_bind6: no permission for conn"
				    " <%d> to bind to port <%u>\n",
				    conn->sdp_hashent, requested_port);
			}
			result = EACCES;
			goto done;
		}
	}
	v6srcp = &addr6->sin6_addr;
	conn->conn_srcv6 = *v6srcp;
	conn->conn_remv6 = ipv6_all_zeros;

	result = sdp_inet_port_get(conn, sdp_binding_v6, requested_port);
	if (result != 0) {
		/* CONSTCOND */
		sdp_print_warn(conn, "sdp_inet_bind6: error getting port "
		    "during bind. <%d>", result);
		if (conn->inet_ops.debug) {
			(void) strlog(SDP_MODULE_ID, 0, 1,
			    SL_TRACE | SL_ERROR,
			    "sdp_inet_bind6: error getting port "
			    "during bind. <%d>", result);
		}
		goto done;
	}
	conn->sdp_bound_state = sdp_bound_v6;
	result = 0;
done:
	ASSERT(mutex_owned(&conn->conn_lock));
	return (result);
}   /* sdp_inet_bind6 */

/* ========================================================================= */

/*
 * Common code for ipv4 and ipv6.
 */
static int32_t
sdp_inet_connect(sdp_conn_t *conn)
{

	int	result;

	result = sdp_post_msg_hello(conn);
	if (result != 0) {
		/* CONSTCOND */
		sdp_print_warn(conn, "sdp_inet_connect: error <%d> during "
		    "connect()", result);
		if (conn->inet_ops.debug) {
			(void) strlog(SDP_MODULE_ID, 0, 1,
			    SL_TRACE | SL_ERROR,
			    "sdp_inet_connect: error connecting to "
			    " gateway interface. <%d>",
			    result);
		}
	} /* if */
done:
	return (result);
}

/*
 * sdp_inet_connect -- connect to a remote address
 * called from sdp_connect, which has received a connect request
 */
int32_t
sdp_inet_connect4(sdp_conn_t *conn, sin_t *addr, int32_t size)
{
	int32_t result;
	in6_addr_t dstaddr;
	char buf[INET6_ADDRSTRLEN];

	SDP_CHECK_NULL(addr, EINVAL);
	ASSERT(mutex_owned(&conn->conn_lock));
	if (size < sizeof (struct sockaddr_in))
		return (EINVAL);

	IN6_INADDR_TO_V4MAPPED(&addr->sin_addr, &dstaddr);
	(void) inet_ntop(AF_INET6, &dstaddr, buf, sizeof (buf));
	bcopy(&dstaddr, &conn->conn_remv6, sizeof (in6_addr_t));

	conn->error = 0;
	conn->dst_port = addr->sin_port;
	dprint(SDP_CTRL, ("CTRL <%p> sdp_inet_connect4: connecting to <%s:%d>",
	    (void *)conn, buf, ntohs(conn->dst_port)));

	/*
	 * close, allow connection completion notification.
	 */
	result = sdp_inet_connect(conn);
	/* CONSTCOND */
	sdp_print_dbg(conn, "sdp_inet_connect4: result:<%d>", result);

done:
	return (result);
}   /* sdp_inet_connect4 */

int32_t
sdp_inet_connect6(sdp_conn_t *conn, sin6_t *addr6, int32_t size)
{
	in6_addr_t *v6dstp;
	int32_t result;

	SDP_CHECK_NULL(addr6, EINVAL);

	if (size < sizeof (struct sockaddr_in6))
		return (EINVAL);

	conn->error = 0;
	conn->dst_port = addr6->sin6_port;
	v6dstp = &addr6->sin6_addr;
	conn->conn_remv6 = *v6dstp;
	conn->conn_srcv6 = ipv6_all_zeros;
	dprint(SDP_CTRL, ("sdp_inet_connect6: conn:%p, connecting to "
	    " <%x:%x:%x:%x> ", (void *)conn,
	    conn->conn_remv6.s6_addr32[0], conn->conn_remv6.s6_addr32[1],
	    conn->conn_remv6.s6_addr32[2], conn->conn_remv6.s6_addr32[3]));

	/*
	 * close, allow connection completion notification.
	 */
	result = sdp_inet_connect(conn);
	/* CONSTCOND */
	sdp_print_dbg(conn, "sdp_inet_connect6: result:<%d>", result);

done:
	return (result);
}   /* sdp_inet_connect */

/* ========================================================================= */

/*
 * sdp_inet_listen -- listen on a socket for incomming addresses.
 */
/* ARGSUSED */
int32_t
sdp_inet_listen(sdp_conn_t *conn, int32_t backlog)
{
	int32_t result;

	SDP_CHECK_NULL(conn, -EINVAL);
	/* CONSTCOND */
	sdp_print_ctrl(conn, "sdp_inet_listen: backlog:%d", backlog);

	if (conn->state != SDP_CONN_ST_LISTEN) {
		result = sdp_inet_listen_start(conn);
		if (result != 0) {
			/* CONSTCOND */
			sdp_print_warn(conn, "sdp_inet_listen: fail <%d>",
			    result);
			if (conn->inet_ops.debug) {
				(void) strlog(SDP_MODULE_ID, 0, 1,
				    SL_TRACE | SL_ERROR,
				    "sdp_inet_listen: failed to "
				    "start listening. <%d>",
				    result);
			}
			goto done;
		}
	}
	conn->backlog_max = backlog;
	result = 0;
	SDP_STAT_INC(conn->sdp_sdps, PassiveOpens);

done:
	return (result);
}   /* sdp_inet_listen */

/* ========================================================================= */

/*
 * sdp_inet_shutdown -- process shutdown
 */
int32_t
sdp_inet_shutdown(int32_t flag, sdp_conn_t *conn)
{
	int32_t result = 0;

	ASSERT(mutex_owned(&conn->conn_lock));

	/*
	 * flag:
	 * 0 - recv shutdown
	 * 1 - send shutdown
	 * 2 - send/recv shutdown.
	 */
	if (flag < 0 || flag > 2) {
		return (-EINVAL);
	} else {
		flag++;	/* match shutdown mask. */
	}	/* else */

	conn->shutdown |= flag;
	/* CONSTCOND */
	sdp_print_ctrl(conn, "sdp_inet_shutdown: shutdown flag:<%d>", flag);

	switch (conn->state) {
		case SDP_CONN_ST_REQ_PATH:
		case SDP_CONN_ST_REQ_SENT:
			/*
			 * outstanding request. Mark it in error, and
			 * completions needs to complete the closing.
			 */
			SDP_CONN_ST_SET(conn, SDP_CONN_ST_ERROR);
			break;
		case SDP_CONN_ST_LISTEN:
			if (flag & SDP_SHUTDOWN_RECV) {
				result = sdp_inet_listen_stop(conn);
				if (result != 0) {
					/* CONSTCOND */
					sdp_print_warn(conn,
					    "sdp_inet_shutdown: listen stop"
					    " error <%d>", result);
					break;
				}
			}
			break;
		case SDP_CONN_ST_CLOSED:
		case SDP_CONN_ST_ERROR:
			result = -ENOTCONN;
			break;
		default:
			if (!(flag & SDP_SHUTDOWN_RECV)) {
				result = sdp_inet_post_disconnect(conn);
				if (result < 0) {
					/* CONSTCOND */
					sdp_print_warn(conn,
					    "sdp_inet_shutdown: disconn stop"
					    " error <%d>", result);
					break;
				}
			}
			break;
	}
	return (result);
}


int sdp_recv_urg(sdp_conn_t *conn, struct msghdr *msg,
    int flags, struct uio *uiop)
{

	int result = 0;


	if (conn->inet_ops.urginline ||
	    !(conn->sdp_recv_oob_state &
	    (SDP_RECV_OOB_PEND | SDP_RECV_OOB_PRESENT))) {
		return (EINVAL);
	}

	if (!(conn->sdp_recv_oob_state & SDP_RECV_OOB_PRESENT)) {
		return (EWOULDBLOCK);
	}

	msg->msg_flags |= MSG_OOB;
	result = uiomove(&conn->sdp_recv_oob_msg, 1, UIO_READ, uiop);
	if (result) {
		goto done;
	}
	/*
	 * Clear SDP_RECV_OOB_PRESENT flag and mark the byte as being consumed.
	 * All this is necessary to support weird OOB semantics
	 */
	if (!(flags & MSG_PEEK)) {
		conn->sdp_recv_oob_msg = NULL;
		conn->sdp_recv_oob_state &= ~SDP_RECV_OOB_PRESENT;
		conn->sdp_recv_oob_state |= SDP_RECV_OOB_CONSUMED;
	}
done:
	return (result);
}

/*
 * sdp_recv - recv data from the network to user space
 */
int
sdp_recv(sdp_conn_t *conn, struct msghdr *msg, int flags, struct uio *uiop)
{
	int result = 0;
	ssize_t low_water;
	ssize_t count;
	int ack = 0;
	sdp_pool_t peek_queue;
	sdp_buff_t *buff;
	boolean_t sdp_clear_atmark = B_FALSE;
	int peek_count = 0;

	/*
	 * TODO: unhandled.
	 */
	if (flags & MSG_TRUNC)
		return (EOPNOTSUPP);

	if (flags & MSG_PEEK) {
		(void) sdp_buff_pool_init(&peek_queue);
		msg->msg_flags |= MSG_PEEK;
	}

	SDP_CONN_LOCK(conn);

	if (conn->state == SDP_CONN_ST_LISTEN ||
	    conn->state == SDP_CONN_ST_CLOSED ||
	    conn->state == SDP_CONN_ST_ERROR ||
	    conn->state == SDP_CONN_ST_TIME_WAIT_1 ||
	    conn->state == SDP_CONN_ST_TIME_WAIT_2) {
		sdp_print_warn(conn, "sdp_recv: invalid conn state: %04x",
		    conn->state);
		SDP_CONN_UNLOCK(conn);
		return (ENOTCONN);
	}

	/*
	 * process urgent data
	 */
	if (flags & MSG_OOB) {
		result = sdp_recv_urg(conn, msg, flags, uiop);
		SDP_CONN_UNLOCK(conn);
		return (result);
	}

	if (flags & MSG_WAITALL)
		low_water = uiop->uio_resid;
	else
		low_water = min(uiop->uio_resid,
		    conn->sdp_sdps->sdp_sth_rcv_lowat);

	msg->msg_controllen = 0;
	msg->msg_namelen = 0;

	if (low_water == 0)
		low_water = 1;

	if (conn->sdp_recv_oob_state & SDP_RECV_OOB_ATMARK) {
		if (!(flags & MSG_PEEK))
			sdp_clear_atmark = B_TRUE;
	}

	while (uiop->uio_resid > 0 && !(SDP_SHUTDOWN_RECV & conn->shutdown)) {
		if ((buff = buff_pool_get_head(&conn->recv_pool)) != NULL) {
			count = PTRDIFF(buff->sdp_buff_tail,
			    buff->sdp_buff_data);
			ASSERT(count > 0);

			/*
			 * can not read past oob mark
			 * We read:
			 * min(count, oob_offset, resid)
			 */
			if (conn->sdp_recv_oob_offset != 0)
				count = min(conn->sdp_recv_oob_offset, count);
			count = min(uiop->uio_resid, (int)count);
			sdp_print_data(conn, "sdp_recv: copying <%ld> len data",
			    count);
			result = uiomove(buff->sdp_buff_data, count,
			    UIO_READ, uiop);

			dprint(SDP_DATA, ("sdp_recv:uiomove: resid:%ld "
			    "error:%d len:%ld", uiop->uio_resid,
			    result, count));
			if (result != 0) {
				/*
				 * We will re-read this message the next time.
				 */
				sdp_print_warn(conn, "sdp_recv: "
				    "Failed in uiomove: <%d>", result);
				buff_pool_put_head(&conn->recv_pool, buff);
				goto done;
			}

			low_water -= count;
			if (flags & MSG_PEEK) {

				peek_count += count;
				buff_pool_put_head(&peek_queue, buff);
				if ((conn->sdp_recv_oob_offset -
				    peek_count) == 0)
					break;
			} else {
				SDP_CONN_STAT_RECV_INC(conn, count);
				conn->sdp_recv_byte_strm -= count;
				buff->sdp_buff_data =
				    (char *)buff->sdp_buff_data + count;
				if (sdp_clear_atmark) {
					conn->sdp_recv_oob_state &=
					    ~ (SDP_RECV_OOB_ATMARK |
					    SDP_RECV_OOB_PRESENT |
					    SDP_RECV_OOB_PEND |
					    SDP_RECV_OOB_CONSUMED);
					sdp_clear_atmark = B_FALSE;
				}
				if (conn->sdp_recv_oob_offset > 0) {
					ASSERT(conn->sdp_recv_oob_state &
					    SDP_RECV_OOB_PEND |
					    SDP_RECV_OOB_PRESENT);
					conn->sdp_recv_oob_offset -= count;
					if (conn->sdp_recv_oob_offset == 0) {
						conn->sdp_recv_oob_state |=
						    SDP_RECV_OOB_ATMARK;
					}
				}
				if (PTRDIFF(buff->sdp_buff_tail,
				    buff->sdp_buff_data) > 0) {
					buff_pool_put_head
					    (&conn->recv_pool, buff);
				} else {
					sdp_buff_free(buff);
				}
				/*
				 * post additional recv buffers if
				 * needed, but check only every N
				 * buffers...
				 */
				if (SDP_CONN_RECV_POST_ACK_LIM < ++ack) {
					result = sdp_recv_post(conn);
					if (result < 0) {
						/* CONSTCOND */
						sdp_print_warn(conn,
						    "sdp_recv: Failed to post"
						    "buffers: %d", result);
						goto done;
					}
					ack = 0;
				}

				if (conn->sdp_recv_oob_state &
				    SDP_RECV_OOB_ATMARK)
					break;
			}
		}

		if (uiop->uio_resid == 0)
			break;

		if (buff_pool_size(&conn->recv_pool) > 0)
				continue;
		/*
		 * If there are no outstanding remote source advertisements
		 * and enough data has been copied, complete the read
		 */
		if (!conn->src_recv && (low_water <= 0))
			break;
		/*
		 * check connection errors, and then wait for more data.
		 */
		if (SDP_SHUTDOWN_RECV & conn->shutdown) {
			result = 0;
			break;
		}

		if (conn->state == SDP_CONN_ST_ERROR) {
			/* CONSTCOND */
			sdp_print_dbg(conn, "sdp_recv: conn state error: %d",
			    conn->error);
			result = EPROTO;
			break;
		}

		if (flags & MSG_DONTWAIT) {
			result = EWOULDBLOCK;
			break;
		}

		if (!cv_wait_sig(&conn->ss_rxdata_cv, &conn->conn_lock)) {
			result = EINTR;
			break;
		}
	}

done:
	/*
	 * If the SDP shutdown message had been received earlier, but
	 * there may have been data left in the pipeline.  Now there
	 * is not so we mark the socket as unable to receive anything more.
	 */
	if ((SDP_SHUTDOWN_RECV & conn->shutdown) &&
	    (buff_pool_size(&conn->recv_pool) == 0)) {
		if (conn->sdp_ulpd != NULL)
			(*conn->sdp_ulp_ordrel)(conn->sdp_ulpd);
	}

	/*
	 * acknowledge moved data
	 */
	if (ack > 0) {
		int error;

		error = sdp_recv_post(conn);
		if (error < 0) {
			/* CONSTCOND */
			sdp_print_warn(conn, "sdp_recv:error <%d> recv queue",
			    error);
		}
	}

	/*
	 * return any peeked buffers to the recv queue, in the correct order.
	 */
	if (flags & MSG_PEEK) {
		while ((buff = buff_pool_get_head(&peek_queue))) {
			buff_pool_put_head(&conn->recv_pool, buff);
		}
	}

	SDP_CONN_UNLOCK(conn);

	return (result);
}

static sin6_t sin6_zero;
static sin_t sin4_zero;

/*
 * SDP exported kernel interface for geting the primary peer address of
 * a sctp_t.  The parameter addr is assumed to have enough space to hold
 * one socket address.
 */
int
sdp_getpeername(sdp_conn_t *conn, struct sockaddr *addr, socklen_t *addrlen)
{
	int	err = 0;
	sin6_t	*sin6;
	sin_t	*sin4;

	ASSERT(conn != NULL);
	SDP_CONN_LOCK(conn);


	addr->sa_family = conn->sdp_family;
	switch (conn->sdp_family) {
	case AF_INET:
		/* CONSTCOND */
		sdp_print_ctrl(conn, "sdp_getpeername: dst: <%x>",
		    conn->conn_rem);
		/* LINTED */
		sin4 = (struct sockaddr_in *)addr;
		sin4->sin_family = conn->sdp_family_used;
		sin4->sin_addr.s_addr = conn->conn_rem;
		sin4->sin_port = conn->dst_port;
		*addrlen = sizeof (struct sockaddr_in);
		break;
	case AF_INET6:
		/* LINTED */
		sin6 = (sin6_t *)addr;
		*sin6 = sin6_zero;
		sin6->sin6_family = conn->sdp_family_used;
		*addrlen = sizeof (struct sockaddr_in6);
		sin6->sin6_addr = conn->faddr; /* structure copy */
		sin6->sin6_port = conn->dst_port;
		break;
	default:
		err = EPROTONOSUPPORT;
		break;
	}

	SDP_CONN_UNLOCK(conn);
	return (err);
}


/*
 * SDP exported kernel interface for geting the first source address of
 * a sctp_t.  The parameter addr is assumed to have enough space to hold
 * one socket address.
 */
int
sdp_getsockname(sdp_conn_t *conn, struct sockaddr *addr, socklen_t *addrlen)
{
	int	err = 0;
	sin_t	*sin4;
	sin6_t	*sin6;

	ASSERT(conn != NULL);

	/* CONSTCOND */
	sdp_print_ctrl(conn, "sdp_getsockname: family:%d", conn->sdp_family);

	SDP_CONN_LOCK(conn);
	switch (conn->sdp_family) {
	case AF_INET:
		/* LINTED */
		sin4 = (sin_t *)addr;
		*sin4 = sin4_zero;
		sin4->sin_family = conn->sdp_family_used;
		sin4->sin_addr.s_addr = conn->conn_src;
		sin4->sin_port = conn->src_port;
		*addrlen = sizeof (struct sockaddr_in);
		break;
	case AF_INET6:
		/* LINTED */
		sin6 = (sin6_t *)addr;
		*sin6 = sin6_zero;
		sin6->sin6_family = conn->sdp_family_used;
		*addrlen = sizeof (struct sockaddr_in6);
		sin6->sin6_addr = conn->saddr;	/* structure copy */
		sin6->sin6_port = conn->src_port;
		break;
	}
	SDP_CONN_UNLOCK(conn);
	return (err);
}


/*
 * SDP host module load/unload functions
 */

/* ========================================================================= */

/*
 * sdp_init - initialize the sdp host module
 */
int sdp_msg_buff_size = SDP_MSG_BUFF_SIZE;
boolean_t sdp_apm_enabled = B_TRUE;

int
sdp_init(sdp_dev_root_t **global_state)
{
	int32_t result = 0;

	/*
	 * sdp_post_msg_hello_ack has a check
	 * to not accept connections if the buffer size is
	 * < sizeof (sdp_msg_hello_ack_t)
	 */
	if (sdp_msg_buff_size < sizeof (sdp_msg_hello_ack_t)) {
		cmn_err(CE_CONT, "Configured sdp_msg_buff_size of %d Bytes"
		    " is less than minimum allowable size of %d Bytes,"
		    " using default size of %d Bytes\n",
		    sdp_msg_buff_size, (int)sizeof (sdp_msg_hello_ack_t),
		    SDP_MSG_BUFF_SIZE);
		sdp_msg_buff_size = SDP_MSG_BUFF_SIZE;
	}

	/*
	 * Make sure that sdp_msg_buff_size is always a multiple
	 * of the cache line
	 */
	sdp_msg_buff_size = P2ROUNDUP(sdp_msg_buff_size, CACHE_ALIGN_SIZE);
	/*
	 * we don't want global_state to reference anything yet.
	 */
	*global_state = NULL;

	result = sdp_generic_main_init();
	if (result != 0) {
		goto error_generic;
	}
	/*
	 * advertisment table -- create its cache.
	 */
	result = sdp_conn_advt_init();
	if (result != 0) {
		goto error_advt;
	}
	/*
	 * * connection table
	 */
	result = sdp_conn_table_init(AF_INET, _conn_size, _buff_min,
	    _buff_max, global_state);
	if (result < 0) {
		ASSERT(NULL == *global_state);
		goto error_conn;
	}
	ASSERT(*global_state != NULL);

	mutex_init(&(((sdp_state_t *)*global_state)->hcas_mutex), NULL,
	    MUTEX_DRIVER, NULL);
	cv_init(&(((sdp_state_t *)*global_state)->hcas_cv), NULL,
	    CV_DRIVER, NULL);

	(*global_state)->sdp_buff_size = sdp_msg_buff_size;

	(*global_state)->sdp_apm_enabled = sdp_apm_enabled;
	return (0);	/* success */
error_conn:
	(void) sdp_conn_advt_cleanup();
error_advt:
	(void) sdp_generic_main_cleanup();
error_generic:
	return (result);
}   /* sdp_init */

/* ========================================================================= */

/*
 * sdp_exit -- cleanup the sdp host module
 */
void
sdp_exit(void)
{
	sdp_state_t *state = sdp_global_state;

	/*
	 * connection table
	 */
	(void) sdp_conn_table_clear();

	/*
	 * delete advertisement table
	 */
	(void) sdp_conn_advt_cleanup();
	/*
	 * delete generic table
	 */
	(void) sdp_generic_main_cleanup();

	/* Destroy the global mutex */
	mutex_destroy(&state->hcas_mutex);
	cv_destroy(&state->hcas_cv);

}   /* sdp_exit */
