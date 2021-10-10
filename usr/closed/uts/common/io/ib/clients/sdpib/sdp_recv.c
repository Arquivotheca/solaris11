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
#include <sys/sdt.h>

void sdp_esb_buff_free(char *arg);
static int32_t sdp_recv_post_rdma(sdp_conn_t *conn);

/*
 * Post a single buffer for data recv.
 */
int32_t
sdp_recv_post_buff(sdp_conn_t *conn, sdp_buff_t *buff)
{
	ibt_status_t result;

	SDP_CHECK_NULL(conn, -EINVAL);

	/*
	 * The data pointer is set according to what the network interface
	 * peer advertised to us minus the required header. This way the data
	 * we end up passing to the interface will always be within the
	 * correct range.
	 */
	buff->sdp_buff_tail = buff->sdp_buff_end;
	buff->sdp_buff_data =
	    (char *)buff->sdp_buff_tail - conn->sdpc_local_buff_size;

	buff->sdp_buff_ib_wrid = ++conn->recv_wrid;
#ifdef SDP_RD_ESBALLOC
	buff->frtn.free_func = sdp_esb_buff_free;
	buff->frtn.free_arg = (char *)buff->frtn_arg;
	buff->frtn_arg[0] = (uintptr_t)conn;
	buff->frtn_arg[1] = (uintptr_t)buff;
#endif

	conn->l_recv_bf++;

	/*
	 * Save the buffer for the recv cq handler.
	 */
	buff_pool_put_tail(&conn->recv_post, buff);

	buff->sdp_rwr.wr_id = (ibt_wrid_t)conn->recv_wrid;

	/*
	 * Post the recv work request.
	 */
	ASSERT(MUTEX_HELD(&conn->conn_lock));
	result = ibt_post_recv(conn->channel_hdl, &buff->sdp_rwr, 1, NULL);
	if (result != IBT_SUCCESS) {
		sdp_print_warn(conn, "sdp_recv_post_buff: fail in "
		    "ibt_post_recv():<%d>", result);
		(void) buff_pool_get_tail(&conn->recv_post);
		result = -EINVAL;
		goto drop;
	}	/* if */

	/*
	 * Next the connection should consume RDMA Source advertisments or
	 * create RDMA Sink advertisments, either way setup for RDMA's for
	 * data flowing from the remote connection peer to the local
	 * connection peer.
	 */
	result = sdp_recv_post_rdma(conn);
	if (result < 0)
		goto error;

	/*
	 * Gratuitous increase of remote send credits. Independant of posting
	 * receive buffers, it may be neccessary to notify the remote client
	 * of how many buffers are available. For small numbers, advertise
	 * more often than for large numbers. Always advertise when we add
	 * the first two buffers.
	 *
	 * 1) Fewer advertised buffers than actual posted buffers.
	 *
	 * 2) Less than three buffers advertised. (OR'd with the next
	 * two because we can have lots of sinks, but still need to send
	 * since those sinks may never get used.
	 *
	 * 3) The difference between posted and advertised is greater
	 * than three.
	 *
	 * 4) the peer has no source or sink advertisements pending.
	 * In process advertisements generate completions -- that's why no ack.
	 */

	if ((conn->l_advt_bf < SDP_MIN_BUFF_COUNT &&
	    conn->l_recv_bf > conn->l_advt_bf) ||
	    ((conn->l_recv_bf - conn->l_advt_bf) >
	    SDP_CONN_RECV_POST_ACK_LIM)) {
		result = sdp_send_ctrl_ack(conn);
		if (result < 0) {
			goto error;
		}	/* if */
	}	/* if */
	return (0);
drop:
	sdp_buff_free(buff);
	conn->l_recv_bf--;
error:
	return (result);
}   /* sdp_recv_post_recv_buff */


void
sdp_esb_buff_free(char *arg)
{
	/* LINTED */
	uintptr_t	*ptr = (uintptr_t *)arg;
	sdp_conn_t	*conn = (sdp_conn_t *)ptr[0];
	sdp_buff_t	*buff = (sdp_buff_t *)ptr[1];

	if ((conn) && (buff)) {
		SDP_CONN_LOCK(conn);
		(void) sdp_recv_post_buff(conn, buff);
		SDP_CONN_UNLOCK(conn);
	}
}

/*
 * Post a single buffer for data recv.
 */
static int32_t
sdp_recv_post_recv_buff(sdp_conn_t *conn)
{
	ibt_status_t ibt_error;
	int	error;
	sdp_buff_t *buff;

	SDP_CHECK_NULL(conn, -EINVAL);

	/*
	 * Get a buffer from the hca's main pool.
	 */
	buff = sdp_buff_get(conn);
	if (buff == NULL) {
		dprint(SDP_WARN, ("sdp_recv_post_recv_buff: "
		    "Failure to alloc buff"));
		error = ENOMEM;
		goto on_error;
	}

	/*
	 * The data pointer is set according to what the network interface
	 * peer advertised to us minus the required header. This way the data
	 * we end up passing to the interface will always be within the
	 * correct range.
	 */
	buff->sdp_buff_tail = buff->sdp_buff_end;
	buff->sdp_buff_data =
	    (char *)buff->sdp_buff_tail - conn->sdpc_local_buff_size;
	buff->sdp_buff_ib_wrid = ++conn->recv_wrid;
#ifdef SDP_RD_ESBALLOC
	buff->frtn.free_func = sdp_esb_buff_free;
	buff->frtn.free_arg = (char *)buff->frtn_arg;
	buff->frtn_arg[0] = (uintptr_t)conn;
	buff->frtn_arg[1] = (uintptr_t)buff;
#endif
	conn->l_recv_bf++;

	/*
	 * Save the buffer for the recv cq handler.
	 */
	buff_pool_put_tail(&conn->recv_post, buff);

	buff->sdp_rwr.wr_id = (ibt_wrid_t)conn->recv_wrid;

	/*
	 * Post the recv work request.
	 */
	ASSERT(MUTEX_HELD(&conn->conn_lock));
	ibt_error = ibt_post_recv(conn->channel_hdl, &buff->sdp_rwr, 1, NULL);
	if (ibt_error != IBT_SUCCESS) {
		sdp_print_warn(conn, "sdp_recv_post_recv_buff: fail "
		    "post recv <%d>", error);
		error = EINVAL;
		goto drop;
	}
	return (0);
drop:
	sdp_buff_free(buff);
	conn->l_recv_bf--;
on_error:
	return (error);
}   /* sdp_recv_post_recv_buff */

/*
 * Post a chain of buffers
 */
int32_t
sdp_recv_post_buff_chain(sdp_conn_t *conn, sdp_buff_t *head)
{
	ibt_status_t result;
	ibt_recv_wr_t recv_wr[SDP_MAX_CQ_WC];
	sdp_buff_t	*buff;
	sdp_buff_t	*nbuff;
	int		numreq = 0;

	SDP_CHECK_NULL(conn, -EINVAL);

	for (buff = head; buff != NULL; numreq++) {

		buff->sdp_buff_tail = buff->sdp_buff_end;
		buff->sdp_buff_data =
		    (char *)buff->sdp_buff_tail - conn->sdpc_local_buff_size;

		buff->sdp_buff_ib_wrid = ++conn->recv_wrid;
		conn->l_recv_bf++;

		/*
		 * Save the buffer for the recv cq handler.
		 */
		nbuff = buff->next;
		buff->next = NULL;
		buff_pool_put_tail(&conn->recv_post, buff);

		recv_wr[numreq] = buff->sdp_rwr;
		recv_wr[numreq].wr_id = (ibt_wrid_t)conn->recv_wrid;
		buff = nbuff;
	}

	/*
	 * Post the recv work request.
	 */
	ASSERT(MUTEX_HELD(&conn->conn_lock));
	result = ibt_post_recv(conn->channel_hdl, recv_wr, numreq, NULL);
	if (result != IBT_SUCCESS) {
		sdp_print_warn(conn, "sdp_recv_post_buff_chain: fail in "
		    "ibt_post_recv: res <%d>", result);
		(void) buff_pool_get_tail(&conn->recv_post);
		result = -EINVAL;
		goto drop;
	}	/* if */

	/*
	 * Next the connection should consume RDMA Source advertisments or
	 * create RDMA Sink advertisments, either way setup for RDMA's for
	 * data flowing from the remote connection peer to the local
	 * connection peer.
	 */
	result = sdp_recv_post_rdma(conn);
	if (result < 0)
		goto error;

	/*
	 * Gratuitous increase of remote send credits. Independant of posting
	 * receive buffers, it may be neccessary to notify the remote client
	 * of how many buffers are available. For small numbers, advertise
	 * more often than for large numbers. Always advertise when we add
	 * the first two buffers.
	 *
	 * 1) Fewer advertised buffers than actual posted buffers.
	 *
	 * 2) Less than three buffers advertised. (OR'd with the next
	 * two because we can have lots of sinks, but still need to send
	 * since those sinks may never get used.
	 *
	 * 3) The difference between posted and advertised is greater
	 * than three.
	 *
	 * 4) the peer has no source or sink advertisements pending.
	 * In process advertisements generate completions -- that's why no ack.
	 */

	if ((conn->l_advt_bf < SDP_MIN_BUFF_COUNT &&
	    conn->l_recv_bf > conn->l_advt_bf) ||
	    ((conn->l_recv_bf - conn->l_advt_bf) >
	    SDP_CONN_RECV_POST_ACK_LIM)) {
		result = sdp_send_ctrl_ack(conn);
		if (result < 0) {
			goto error;
		}	/* if */
	}	/* if */
	return (0);
drop:
	while (buff != NULL) {
		nbuff = buff->next;
		buff->next = NULL;
		sdp_buff_free(buff);
		buff = nbuff;
	}

	conn->l_recv_bf--;
error:
	sdp_print_dbg(conn, "sdp_recv_post_buff_chain :error :result %d",
	    result);
	return (result);
}   /* sdp_recv_post_buff_chain */

/* ========================================================================= */
/*
 * sdp_recv_post_rdma_buff -- post a single buffers for rdma read on a conn
 */
static int32_t
sdp_recv_post_rdma_buff(sdp_conn_t *conn)
{
	ibt_send_wr_t send_wr;
	ibt_wr_ds_t scat_gat_list;
	sdp_advt_t *advt;
	int32_t result;
	uint32_t len;
	sdp_buff_t *buff;

	/*
	 * check queue depth
	 */
	if (!(conn->s_wq_size < conn->scq_size)) {
		result = -ENODEV;
		goto done;
	} /* if */

	/*
	 * get a reference to the first SrcAvail advertisment.
	 */
	advt = sdp_conn_advt_table_look(&conn->src_pend);
	if (advt == NULL) {
		result = -ENODEV;
		goto done;
	} /* if */

	/*
	 * get a buffer
	 */
	buff = sdp_buff_get(conn);
	if (NULL == buff) {
		result = -ENOMEM;
		goto error;
	}

	/*
	 * The data pointer is backed up based on what the stream interface
	 * peer advertised to us plus the required header. This way the
	 * data we end up passing to the interface will always be within
	 * the correct range.
	 */
	buff->sdp_buff_tail = buff->sdp_buff_end;
	buff->sdp_buff_data = (char *)buff->sdp_buff_tail -
	    min((int32_t)conn->sdpc_local_buff_size, advt->size);

	buff->sdp_buff_ib_wrid = conn->recv_wrid++;

	send_wr.wr_id 		= buff->sdp_buff_ib_wrid;
	send_wr.wr_opcode	= IBT_WRC_RDMAR;
	send_wr.wr_flags	= IBT_WR_SEND_SIGNAL;
	send_wr.wr_trans	= IBT_RC_SRV;
	send_wr.wr_nds		= 1;
	send_wr.wr_sgl		= &scat_gat_list;
	send_wr.wr.rc.rcwr.rdma.rdma_raddr = advt->addr;
	send_wr.wr.rc.rcwr.rdma.rdma_rkey = advt->rkey;
	scat_gat_list.ds_va	= (ib_vaddr_t)(uintptr_t)buff->sdp_buff_data;
	scat_gat_list.ds_key	= buff->sdp_buff_lkey;
	len = (uintptr_t)buff->sdp_buff_tail - (uintptr_t)buff->sdp_buff_data;
	scat_gat_list.ds_len	= len;

	advt->wrid  = buff->sdp_buff_ib_wrid;
	advt->size -= len;
	advt->addr += len;
	advt->post += len;
	advt->flag |= SDP_ADVT_F_READ;

	/*
	 * If there is no more advertised space move the advertisment to the
	 * active list, and match the WRID.
	 */
	if (!(advt->size > 0)) {
		advt = sdp_conn_advt_table_get(&conn->src_pend);
		if (advt == NULL) {
			result = -ENODEV;
			goto drop;
		}

		result = sdp_conn_advt_table_put(&conn->src_actv, advt);
		if (result != 0) {
			(void) sdp_conn_advt_destroy(advt);
			goto drop;
		}
	}

	/*
	 * save the buffer for the event handler. Make sure it's before actually
	 * posting the thing. Completion event can happen before post function
	 * returns.
	 */
	sdp_generic_table_put_tail(&conn->r_src, (sdp_generic_t *)buff);

	/*
	 * post rdma
	 */
	result = ibt_post_send(conn->channel_hdl, &send_wr, 1, NULL);
	if (result != IBT_SUCCESS) {
		(void) sdp_generic_table_get_tail(&conn->r_src);
		result = -EINVAL;
		goto drop;
	}

	/*
	 * update send queue depth
	 */
	conn->s_wq_size++;

	return (0);
drop:
	sdp_buff_free(buff);
error:
done:
	return (result);
} /* _tsSdpRecvPostRdmaBuff */


/* ========================================================================= */
/*
 * sdp_recv_post_rdma  -- post a rdma based requests for a connection
 */
static int32_t
sdp_recv_post_rdma(sdp_conn_t *conn)
{
	int32_t result;

	/*
	 * Since RDMA Reads rely on posting to the Send WQ, stop if
	 * we're not in an appropriate state. It's possible to queue
	 * the sink advertisment, something to explore, but SrcAvail
	 * slow start might make that unneccessart?
	 */
	if ((conn->state & SDP_ST_MASK_SEND_OK) == 0) {
		return (0);
	}

	/*
	 * loop flushing IOCB RDMAs. Read sources, otherwise post sinks.
	 */
	if (conn_advt_table_size(&conn->src_pend) > 0) {

		while (conn_advt_table_size(&conn->src_pend) > 0 &&
		    (buff_pool_size(&conn->recv_pool) <
		    conn->sdpc_max_rbuffs)) {

			if ((result =  sdp_recv_post_rdma_buff(conn)) != 0) {
				if (result < 0) {
					goto done;
				} else {
					/*
					 * No more posts allowed.
					 */
					break;
				}
			}
		} /* while */
	}

	result = 0;
done:
	return (result);
} /* sdp_recv_post_rdma  */

/*
 * Post a certain number of receive buffers on a connection.
 * Calculate the number of buffers we're able to post, and call function
 * in a loop, to post each one.
 */
int32_t
sdp_recv_post(sdp_conn_t *conn)
{
	int32_t result = 0;
	int32_t counter = 0;
	uint32_t pool_size;

	SDP_CHECK_NULL(conn, -EINVAL);

	/*
	 * Verify that the connection is in a posting state
	 */
	if ((SDP_ST_MASK_RCV_POST & conn->state) == 0) {
		dprint(3, ("sdp_recv_post: Conn not in posting state"));
		return (EINVAL);
	}

	/*
	 * 1) Calculate available space in the receive queue. Take the
	 * smaller of (a) bytes available for buffering and (b) maximum number
	 * of buffers allowed in the queue. (This prevents a flood of small
	 * buffers.)
	 *
	 * 2) Subtract buffers already posted.
	 *
	 * 3) Take the smaller of (a) buffers needed to fill the buffered
	 * receive/receive posted queue, and (b) the maximum number which are
	 * allowed to be posted at a given time.
	 *
	 * Loop posting receive buffers onto the queue.
	 *
	 * Note that sdpc_max_rbufs could be smaller than the numbers of buffers
	 * that have been posted, since sdpc_max_rbufs may be decreased by
	 * setting the SO_RCVBUF option
	 */
	ASSERT(conn->sdpc_max_rbuffs != 0);
	pool_size = buff_pool_size(&conn->recv_pool);
	if (conn->sdpc_max_rbuffs > pool_size + conn->l_recv_bf) {
		counter = (int32_t)(conn->sdpc_max_rbuffs - pool_size);
		counter -= conn->l_recv_bf;
		counter = min(counter, (conn->rcq_size - conn->l_recv_bf));
		DTRACE_PROBE3(sdp_recv_post, sdp_conn_t *, conn,
		    int32_t, counter, uint32_t, pool_size);
		while (counter-- > 0) {
			if ((result = sdp_recv_post_recv_buff(conn)) != 0) {
				if (result == EINVAL)
					goto done;
				else
					break;
			}
		}
	} else {
		DTRACE_PROBE3(sdp_recv_post, sdp_conn_t *, conn, int32_t, 0,
		    uint32_t, pool_size);
	}

	/*
	 * Next the connection should consume RDMA Source advertisments or
	 * create RDMA Sink advertisments, either way setup for RDMA's for
	 * data flowing from the remote connection peer to the local
	 * connection peer.
	 */
	result = sdp_recv_post_rdma(conn);
	if (result < 0)
		goto done;

	if ((conn->state & SDP_ST_MASK_CTRL_OK) == 0) {
		/*
		 * sdp_recv_post() can be called when the connection
		 * is being setup to post buffers for example from
		 * sdp_actv_connect(). In that case we can not send an
		 * update
		 */
		goto done;
	}

	/*
	 * Gratuitous increase of remote send credits. Independant of posting
	 * receive buffers, it may be neccessary to notify the remote client
	 * of how many buffers are available. For small numbers, advertise
	 * more often than for large numbers. Always advertise when we add
	 * the first two buffers.
	 *
	 * 1) Fewer advertised buffers than actual posted buffers.
	 *
	 * 2) Less than three buffers advertised. (OR'd with the next
	 * two because we can have lots of sinks, but still need to send
	 * since those sinks may never get used.
	 *
	 * 3) The difference between posted and advertised is greater
	 * than three.
	 *
	 * 4) the peer has no source or sink advertisements pending.
	 * In process advertisements generate completions -- that's why no ack.
	 */

	if ((conn->l_advt_bf < SDP_MIN_BUFF_COUNT &&
	    conn->l_recv_bf > conn->l_advt_bf) ||
	    ((conn->l_recv_bf - conn->l_advt_bf) >
	    SDP_CONN_RECV_POST_ACK_LIM)) {
		ASSERT(conn->r_recv_bf > 0);
		result = sdp_send_ctrl_ack(conn);
	}
done:
	return (result);
}   /* sdp_recv_post */

/*
 * Receive a data buffer in interrupt mode (from the receive
 * cq handler).
 *
 * Check if it contains OOB data.
 *
 * If we can putnext to sockfs, copy the data from the SDP buffer
 * into an mblock and send it upstream.  Otherwise, queue it to
 * the conn->recv_pool and let the read-side service routine do the
 * copy and putnext.
 *
 * Return the number of bytes buffered (that's 0 if we putnext).
 */
void
sdp_sock_buff_recv(sdp_conn_t *conn, sdp_buff_t *buff)
{
	int datalen;
	sdp_stack_t *sdps = conn->sdp_sdps;

	/*
	 * We can get messages for eager conn before we've
	 * received the acceptor open and/or sent the conn_res.  We need
	 * to queue it if we're not in DATA_XFER mode yet.  Then we can
	 * flush the queue when we're in handle_sdp_conn_res().
	 */
	/*
	 * To emulate RFC 1122 (page 88) a connection should be reset/aborted
	 * if data is received and the receive half of the connection has been
	 * closed. This notifies the peer that the data was not received.
	 */
	datalen = PTRDIFF(buff->sdp_buff_tail, buff->sdp_buff_data);
	if (datalen == 0) {
		sdp_buff_free(buff);
		return;
	}

	if (SDP_SHUTDOWN_RECV & conn->shutdown) {
		sdp_print_warn(conn, "Buff Receive data path closed."
		    "<%02x>", conn->shutdown);
		/* abort connection (send reset) */
		sdp_conn_abort(conn);
		/* drop packet */
		sdp_buff_free(buff);
		return;
	}

	if (SDP_MSG_HDR_GET_OOB_PEND(buff->bsdh_hdr) &&
	    (conn->sdp_recv_oob_state != SDP_RECV_OOB_PEND)) {
		conn->sdp_recv_oob_state = SDP_RECV_OOB_PEND;
		conn->sdp_ulp_urgdata(conn->sdp_ulpd);
	}
	if (SDP_MSG_HDR_GET_OOB_PRES(buff->bsdh_hdr)) {
		/*
		 * If SDP_RECV_OOB_PEND is set, sigurg has already been sent
		 */
		if (conn->sdp_recv_oob_state != SDP_RECV_OOB_PEND)
			conn->sdp_ulp_urgdata(conn->sdp_ulpd);
		conn->sdp_recv_oob_state = SDP_RECV_OOB_PRESENT;
		/*
		 * SDP does not provide offset of the
		 * OOB byte. It is always the last byte of the
		 * message containing the OOB byte.
		 */
		if (!conn->inet_ops.urginline) {
			conn->sdp_recv_oob_msg =
			    *((uint8_t *)buff->sdp_buff_tail - 1);
			buff->sdp_buff_tail =
			    (uint8_t *)buff->sdp_buff_tail - 1;
		}
		conn->sdp_recv_oob_offset =
		    conn->sdp_recv_byte_strm + datalen - 1;
		if (conn->sdp_recv_oob_offset == 0) {
			conn->sdp_recv_oob_state |= SDP_RECV_OOB_ATMARK;
		}
	}

	if (PTRDIFF(buff->sdp_buff_tail, buff->sdp_buff_data) > 0) {
		buff_pool_put_tail(&conn->recv_pool, buff);
		conn->sdp_recv_byte_strm +=
		    PTRDIFF(buff->sdp_buff_tail, buff->sdp_buff_data);
	} else {
		sdp_buff_free(buff);
	}
	SDP_STAT_UPDATE(sdps, InDataBytes, datalen);
}
