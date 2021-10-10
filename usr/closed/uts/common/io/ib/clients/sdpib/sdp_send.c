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
#include <sys/ib/clients/sdpib/sdp_misc.h>
#include <sys/ib/clients/sdpib/sdp_trace_util.h>

/*
 * Post a buffer send on a SDP connection.
 * Finish writing out the BSDH header.
 * Prepare the scatter/gather list and the send work request,
 * and post the buffer to the framework.
 */
static int32_t
sdp_send_buff_post(sdp_conn_t *conn, sdp_buff_t *buff)
{
	int32_t result;
	ibt_send_wr_t send_wr;
	ibt_wr_ds_t sgl[SDP_QP_LIMIT_SG_MAX], *buff_sgl;
	uint32_t len;

	SDP_CHECK_NULL(conn, -EINVAL);
	SDP_CHECK_NULL(buff, -EINVAL);
	SDP_CHECK_NULL(buff->bsdh_hdr, -EINVAL);

	ASSERT(MUTEX_HELD(&conn->conn_lock));

	/*
	 * Write header on send buffer.
	 */
	conn->r_recv_bf--;
	conn->s_wq_size++;

	dprint(SDP_DATA, ("DATA: <%p> ibt_post_send: sz:%d queued:%d id:%d "
	    "p:%d r_bf:%d l_bf:%d", (void *)conn, conn->s_wq_size,
	    conn->sdpc_tx_bytes_queued, buff->u_id,
	    conn->sdpc_tx_bytes_unposted,
	    conn->r_recv_bf, conn->l_recv_bf));

	conn->l_advt_bf			= conn->l_recv_bf;
	conn->sdpc_tx_bytes_unposted	-= buff->sdp_buff_data_size;
	conn->oob_offset 	-= (conn->oob_offset > 0) ?
	    buff->sdp_buff_data_size : 0;
	buff->sdp_buff_ib_wrid	= conn->send_wrid++;
	buff->bsdh_hdr->recv_bufs = conn->l_advt_bf;
	buff->bsdh_hdr->size	= (uintptr_t)buff->sdp_buff_tail -
	    (uintptr_t)buff->sdp_buff_data;
	buff->bsdh_hdr->seq_num	= ++conn->send_seq;
	buff->bsdh_hdr->seq_ack	= conn->advt_seq;

	/*
	 * Endian swap
	 */
	(void) sdp_msg_host_to_wire_bsdh(buff->bsdh_hdr);

	/*
	 * Prepare for IBTF post.
	 */
	len = (uintptr_t)buff->sdp_buff_tail - (uintptr_t)buff->sdp_buff_data;

	ASSERT(len > 0);

	send_wr.wr_id = (ibt_wrid_t)buff->sdp_buff_ib_wrid;

#ifdef USE_UNSIGNALLED
	if ((SDP_BUFF_F_GET_UNSIG(buff) > 0) &&
	    (conn->send_cons < SDP_CONN_UNSIG_SEND_MAX)) {
		send_wr.wr_flags = IBT_WR_NO_FLAGS;
		conn->send_usig++;
		conn->send_cons++;
	} else {
		SDP_BUFF_F_CLR_UNSIG(buff);
		send_wr.wr_flags = IBT_WR_SEND_SIGNAL;
		conn->send_cons = 0;
	}
#endif
	SDP_BUFF_F_CLR_UNSIG(buff);
	send_wr.wr_flags = IBT_WR_SEND_SIGNAL;
	conn->send_cons = 0;

	send_wr.wr_opcode	= IBT_WRC_SEND;
	send_wr.wr_trans	= IBT_RC_SRV;
	send_wr.wr_sgl		= sgl;

	/*
	 * Inline requires virtual address, buff_sgl may be physical
	 */
	if (len <= conn->ib_inline_max) {
		send_wr.wr_flags |= IBT_WR_SEND_INLINE;
		send_wr.wr_nds = 1;
		sgl[0].ds_va = (ib_vaddr_t)(uintptr_t)buff->sdp_buff_data;
		sgl[0].ds_len = len;
	} else {
		int segs = 0; /* number of scatter/gather segments */
		int doffset = 0; /* offset to start of data */
		int i;
		uint32_t nds;

		buff_sgl = buff->sdp_rwr.wr_sgl;
		nds = buff->sdp_rwr.wr_nds;
		/*
		 * If the buffer size being used is smaller
		 * than what was allocated (because the peer is
		 * using a smaller buffer) we have to skip
		 * the extra bytes starting from sdp_buff_head
		 * to sdp_buff_data. doffset is the number of bytes to skip.
		 */
		doffset = (uintptr_t)buff->sdp_buff_data -
		    (uintptr_t)buff->sdp_buff_head;
		for (i = 0; i < nds; i++) {
			if (doffset >= buff_sgl[i].ds_len) {
				/*
				 * No data in this segment, skip it
				 */
				doffset = doffset - buff_sgl[i].ds_len;
				continue;
			} else {
				sgl[segs] = buff_sgl[i];
				sgl[segs].ds_len = buff_sgl[i].ds_len - doffset;
				sgl[segs].ds_va = buff_sgl[i].ds_va + doffset;
				if (len <= sgl[segs].ds_len) {
					sgl[segs++].ds_len = len;
					break;
				}
				len -= sgl[segs++].ds_len;
				doffset = 0;
			}
		}
		send_wr.wr_nds = segs;
	}

	/*
	 * Solicit event bit
	 */
	if (SDP_BUFF_F_GET_SE(buff) > 0) {
		send_wr.wr_flags |= IBT_WR_SEND_SOLICIT;
	}

	/*
	 * OOB processing. If there is a single OOB byte in flight then the
	 * pending flag is set as early as possible. IF a second OOB byte
	 * becomes queued then the pending flag for that byte will be in the
	 * buffer which contains the data. Multiple outstanding OOB messages
	 * is not well defined, this way we won't lose any, we'll get early
	 * notification in the normal case, we adhear to the protocol, and we
	 * Don't need to track every message seperatly which would be
	 * expensive.
	 *
	 * If the connections OOB flag is set and the oob counter falls below
	 * 64K we set the pending flag, and clear the the flag. this allows
	 * for at least one pending urgent message to send early
	 * notification.
	 */
	if ((conn->flags & SDP_CONN_F_OOB_SEND) > 0 &&
	    !(conn->oob_offset > 0xffff)) {
		/*
		 * OOB offset is below 64K set the pending flag
		 * and clear the conn OOB flag.
		 */
		SDP_MSG_HDR_SET_OOB_PEND(buff->bsdh_hdr);
		SDP_BUFF_F_SET_SE(buff);
		conn->flags &= ~(SDP_CONN_F_OOB_SEND);
	}

	/*
	 * The only valid oob byte is the last most oob byte.
	 * So if oob_offset is non zero but SDP_BUFF_F_OOB_PRES is set
	 * it means an OOB byte was added later which consumes the current
	 * OOB byte. The current OOB byte will be sent as simple data byte.
	 */
	if ((buff->flags & SDP_BUFF_F_OOB_PRES) && conn->oob_offset == 0) {
		/*
		 * set the oob present flag
		 */
		SDP_MSG_HDR_SET_OOB_PRES(buff->bsdh_hdr);
		SDP_BUFF_F_SET_SE(buff);
	}
	/*
	 * Check queue membership. (First send attempt vs. flush.)
	 */
	if (generic_table_member((sdp_generic_t *)buff) > 0) {
		sdp_generic_table_remove((sdp_generic_t *)buff);
	}

	/*
	 * Save the buffer for the send cq handler.
	 */
	buff_pool_put_tail(&conn->send_post, buff);

	/*
	 * Post the send to the IBTF..
	 */
	result = ibt_post_send(conn->channel_hdl, &send_wr, 1, NULL);
	if (result != IBT_SUCCESS) {
		(void) buff_pool_get_tail(&conn->send_post);
		result = EIO;
		goto done;
	}

	SDP_STAT_INC(conn->sdp_sdps, OutSegs);
	return (0);
done:
	conn->r_recv_bf++;
	conn->send_seq--;
	conn->s_wq_size--;
	return (result);
}   /* sdp_send_buff_post */


/*
 * Post data for buffered transmission.
 */
static int32_t
sdp_send_data_buff_post(sdp_conn_t *conn, sdp_buff_t *buff)
{
	sdp_advt_t *advt;
	int32_t result;

	SDP_CHECK_NULL(conn, -EINVAL);
	SDP_CHECK_NULL(buff, -EINVAL);

	/*
	 * Check sufficient remote buffer advertisements for data transmission
	 */
	if (conn->r_recv_bf < SDP_MIN_BUFF_COUNT) {
		sdp_print_dbg(conn, "sdp_send: Remote advt buff<3:<%d>",
		    conn->r_recv_bf);
		return (ENOBUFS);
	}

	/*
	 * * the rest of the checks can proceed if there is a signalled event
	 * * in the pipe, otherwise we could stall...
	 */
	if (conn->send_usig < buff_pool_size(&conn->send_post) ||
	    sdp_generic_table_size(&conn->w_snk) > 0) {
		if (buff->sdp_buff_tail < buff->sdp_buff_end &&
		    (buff->flags & SDP_BUFF_F_OOB_PRES) == 0 &&
		    conn->nodelay == 0) {

			/*
			 * if the buffer is not full, and there is already
			 * data in the SDP pipe, then hold on to the buffer
			 * to fill it up with more data. if SDP acks clear
			 * the pipe they'll grab this buffer, or send will
			 * flush once it's full, which ever comes first.
			 */
			return (ENOBUFS);
		} /* if */
		/*
		 * Slow start to give sink advertisements a chance for
		 * asymmetric connections. this is desirable to offload the
		 * remote host.
		 */
		if (!(conn->s_wq_cur > conn->s_wq_size)) {

			/*
			 * Slow down the uptake in the send data path to
			 * give the remote side some time to post available
			 * sink advertisements.
			 */
			if (conn->s_wq_cur < conn->scq_size) {
				if (SDP_DEV_SEND_POST_COUNT > conn->s_wq_par) {
					conn->s_wq_par++;
				} else {
					conn->s_wq_cur++;
					conn->s_wq_par = 0;
				}	/* else */
			}	/* if */
			return (ENOBUFS);
		}	/* if */
	}

	/*
	 * Set up header.
	 */
	buff->sdp_buff_data	= (char *)buff->sdp_buff_data -
	    sizeof (sdp_msg_bsdh_t);
	buff->bsdh_hdr		= (sdp_msg_bsdh_t *)buff->sdp_buff_data;
	buff->bsdh_hdr->mid	= SDP_MSG_MID_DATA;
	buff->bsdh_hdr->flags	= SDP_MSG_FLAG_NON_FLAG;

#ifdef USE_UNSIGNALLED
	/*
	 * Signalled? With no delay turned off, data transmission may be
	 * waiting for a send completion.
	 */
	SDP_BUFF_F_SET_UNSIG(buff);
#endif

	/*
	 * Update non-discard counter. Make consideration for a pending sink.
	 * (can be forced by OOB)
	 */
	if (conn_advt_table_size(&conn->snk_pend) > 0) {

		/*
		 * As sink advertisement needs to be discarded, We always
		 * complete an advertisement if there is not enough room for
		 * an entire buffer's worth of data, this allows us to not
		 * need to check how much room is going to be consumed by
		 * this buffer, and only one discard is needed. (Remember the
		 * spec makes sure that the sink is bigger then the buffer.)
		 */
		advt = sdp_conn_advt_table_get(&conn->snk_pend);

		SDP_EXPECT((NULL != advt));

		result = sdp_conn_advt_destroy(advt);
		SDP_EXPECT(!(0 > result));

		/*
		 * Update sink advertisements.
		 */
		conn->snk_recv--;
	} else {
		conn->nond_send++;
	}

	/*
	 * time for transmision.
	 */
	result = sdp_send_buff_post(conn, buff);
	return (result);
}


/*
 * Post data for RDMA transmission
 */
static int32_t
sdp_send_data_buff_sink(sdp_conn_t *conn, sdp_buff_t *buff)
{
	sdp_advt_t *advt;
	int32_t result;
	int32_t zcopy;
	ibt_send_wr_t send_wr;
	ibt_wr_ds_t scat_gat_list;

	SDP_CHECK_NULL(conn, -EINVAL);
	SDP_CHECK_NULL(buff, -EINVAL);

	/*
	 * check state to determine OK to send:
	 *
	 * 1) sufficient send resources.
	 */
	if (!(conn->s_wq_size < conn->scq_size)) {
		return (ENOBUFS);
	}

	/*
	 * Confirm type
	 */
	if (buff->type != SDP_GENERIC_TYPE_BUFF) {
		return (-ENOBUFS);
	}

	/*
	 * Get advertisement.
	 */
	advt = sdp_conn_advt_table_look(&conn->snk_pend);
	if (advt == NULL) {

		return (ENOBUFS);
	}

	/*
	 * Signalled? With no delay turned off, data transmission may be
	 * waiting for a send completion.
	 */
	buff->sdp_buff_ib_wrid = conn->send_wrid++;

	bzero(&send_wr, sizeof (ibt_send_wr_t));
	bzero(&scat_gat_list, sizeof (ibt_wr_ds_t));

	/*
	 * Prepare for IBTF post.
	 */
	scat_gat_list.ds_va = (ib_vaddr_t)(uintptr_t)buff->sdp_buff_data;
	scat_gat_list.ds_key = buff->sdp_buff_lkey;
	scat_gat_list.ds_len = (uintptr_t)buff->sdp_buff_tail -
	    (uintptr_t)buff->sdp_buff_data;

	/*
	 * Send immediately flag?
	 */
	send_wr.wr_flags = IBT_WR_SEND_SIGNAL;
	send_wr.wr_opcode = IBT_WRC_RDMAW;
	send_wr.wr_trans = IBT_RC_SRV;
	send_wr.wr.rc.rcwr.rdma.rdma_raddr = (ib_vaddr_t)advt->addr;
	send_wr.wr.rc.rcwr.rdma.rdma_rkey = (ibt_rkey_t)advt->rkey;
	send_wr.wr_nds = 1;
	send_wr.wr_sgl = &scat_gat_list;

	advt->wrid = buff->sdp_buff_ib_wrid;
	advt->size -= PTRDIFF(buff->sdp_buff_tail, buff->sdp_buff_data);
	advt->addr += PTRDIFF(buff->sdp_buff_tail, buff->sdp_buff_data);
	advt->post += PTRDIFF(buff->sdp_buff_tail, buff->sdp_buff_data);

	/*
	 * Dequeue if needed and the queue buffer
	 */
	if (generic_table_member((sdp_generic_t *)buff) > 0) {

		sdp_generic_table_remove((sdp_generic_t *)buff);
	}

	/*
	 * Update send queue depth
	 */
	conn->s_wq_size++;
	conn->sdpc_tx_bytes_unposted -= buff->sdp_buff_data_size;
	conn->oob_offset -= (conn->oob_offset > 0) ?
	    buff->sdp_buff_data_size : 0;

	sdp_generic_table_put_tail(&conn->w_snk, (sdp_generic_t *)buff);

	/*
	 * Post RDMA
	 */
	result = ibt_post_send(conn->channel_hdl, &send_wr, 1, NULL);

	if (result != 0) {
		result = -EINVAL;
		conn->s_wq_size--;
		goto error;
	}

	/*
	 * If the available space is smaller then send size, complete the
	 * advertisement.
	 */
	if (conn->sdpc_send_buff_size > advt->size) {
		advt = sdp_conn_advt_table_get(&conn->snk_pend);
		SDP_EXPECT((NULL != advt));
		zcopy = advt->post;
		result = sdp_conn_advt_destroy(advt);
		SDP_EXPECT(!(0 > result));

		result = sdp_send_ctrl_rdma_wr_comp(conn, zcopy);
		if (result < 0) {
			result = -ENODEV;
			goto error;
		}

		/*
		 * Update sink advertisements.
		 */
		conn->snk_recv--;
	}	/* if */
	return (0);
error:
	return (result);
}   /* sdp_send_data_buff_sink */

/*
 * Send data buffer if conditions are met.
 */
static int32_t
sdp_send_data_queue_test(sdp_conn_t *conn, sdp_generic_t *element)
{
	int32_t result;

	SDP_CHECK_NULL(conn, -EINVAL);
	SDP_CHECK_NULL(element, -EINVAL);

	if ((SDP_ST_MASK_SEND_OK & conn->state) == 0)
		return (EINVAL);

	if ((SDP_CONN_F_SRC_CANCEL_L & conn->flags) != 0)
		return (ENOBUFS);

	if (element->type == SDP_GENERIC_TYPE_BUFF) {
		if (sdp_conn_advt_table_look(&conn->snk_pend) == 0 ||
		    (SDP_BUFF_F_OOB_PRES & ((sdp_buff_t *)element)->flags)
		    != 0) {
			result = sdp_send_data_buff_post(conn,
			    (sdp_buff_t *)element);
		} else {
			result = sdp_send_data_buff_sink(conn,
			    (sdp_buff_t *)element);
		}
	}

	return (result);
}   /* sdp_send_data_queue_test */

/*
 * Flush data from send queue, to send post.
 */
static void
sdp_send_data_queue_flush(sdp_conn_t *conn)
{
	sdp_generic_t *element;

	SDP_CHECK_NULL(conn, -EINVAL);

	/*
	 * As long as there is data, try to post buffered data, until a
	 * non-zero result is generated. (positive: no space; negative:
	 * error)
	 */
	while (sdp_generic_table_size(&conn->send_queue) > 0) {
		element = sdp_generic_table_look_head(&conn->send_queue);
		SDP_EXPECT((NULL != element));

		if (sdp_send_data_queue_test(conn, element)) {
			/*
			 * Check for dangling element reference,
			 * since called functions can dequeue the
			 * element, and not know how to requeue it.
			 */
			if (sdp_generic_table_member(element) == 0) {
				sdp_generic_table_put_head(
				    &conn->send_queue, element);
			}
			break;
		}
	}
}


/*
 * Send using the data queue if necessary.
 */
static int32_t
sdp_send_data_queue(sdp_conn_t *conn, sdp_generic_t *element)
{
	int32_t result = -1;

	SDP_CHECK_NULL(conn, -EINVAL);
	SDP_CHECK_NULL(element, -EINVAL);

	/*
	 * If data is being buffered, save and return: send/recv completions
	 * will flush the queue. If data is not being buffered, attempt to
	 * send: a positive result requires us to buffer, a negative result
	 * is an error, a return value of zero is a successful transmission
	 */
	if (sdp_generic_table_size(&conn->send_queue) > 0||
	    (result = sdp_send_data_queue_test(conn, element)) != 0) {
		/*
		 * queue packet only if we are out of memory or credits
		 * else propagate error upto send.
		 */
		if ((result != -1) && (result != ENOBUFS) &&
		    (result != ENOMEM)) {
			return (result);
		}
		sdp_generic_table_put_tail(&conn->send_queue, element);
		/*
		 * Potentially request a switch to pipelined mode.
		 */
		if (conn->send_mode == SDP_MODE_COMB &&
		    !(sdp_generic_table_size(&conn->send_queue) <
		    SDP_DEV_SEND_BACKLOG)) {
			(void) sdp_send_ctrl_mode_change(conn,
			    SDP_MSG_MCH_PIPE_RECV);
		}
	}
	return (0);
}


/*
 * Get an appropriate write buffer for send.
 */
static sdp_buff_t *
sdp_send_data_buff_get(sdp_conn_t *conn)
{
	sdp_buff_t *buff;

	SDP_CHECK_NULL(conn, NULL);

	/*
	 * If there is no available buffer with which we can consolidate
	 * data or the buffer contains urgent data, get a new one.
	 */
	buff = (sdp_buff_t *)sdp_generic_table_look_type_tail(
	    &conn->send_queue, SDP_GENERIC_TYPE_BUFF);
	if (buff == NULL || buff->sdp_buff_tail == buff->sdp_buff_end ||
	    (SDP_BUFF_F_OOB_PRES & buff->flags) > 0) {
		buff = sdp_buff_get(conn);
		if (buff != NULL) {
			ASSERT((buff->sdp_buff_head == buff->sdp_buff_data) &&
			    (buff->sdp_buff_data == buff->sdp_buff_tail));
			/*
			 * Restrict the send size to the recv size of
			 * the peer. conn->send_size is set at the time of
			 * connection establishment.
			 */
			buff->sdp_buff_data =
			    (char *)buff->sdp_buff_end -
			    conn->sdpc_send_buff_size;
			/*
			 * account for the header
			 */
			if (buff->sdp_buff_data < buff->sdp_buff_head) {
				buff->sdp_buff_data = buff->sdp_buff_head;
			}
			buff->sdp_buff_tail = buff->sdp_buff_data =
			    (char *)buff->sdp_buff_data +
			    sizeof (sdp_msg_bsdh_t);
			ASSERT(buff->sdp_buff_data <= buff->sdp_buff_end);
			buff->sdp_buff_data_size = 0;
			buff->flags = 0;
		}
	}

	return (buff);
}   /* sdp_send_data_buff_get */

/*
 * Place a buffer into the send queue.
 */
static int32_t
sdp_send_data_buff_put(sdp_conn_t *conn, sdp_buff_t *buff,
    int32_t size, int32_t urg)
{
	int32_t result = 0;

	SDP_CHECK_NULL(conn, -EINVAL);
	SDP_CHECK_NULL(buff, -EINVAL);

	/*
	 * See note on send OOB implementation in send_buff_post.
	 */
	if (urg > 0) {
		buff->flags |= SDP_BUFF_F_OOB_PRES;

		/*
		 * The OOB PEND and PRES flags need to match up as pairs.
		 */
		if (conn->oob_offset < 0) {
			conn->oob_offset = conn->sdpc_tx_bytes_unposted + size;
			conn->flags |= SDP_CONN_F_OOB_SEND;
		}
	}

	/*
	 * If the buffer was queued, then this was a fill of a partial
	 * buffer.
	 */
	if ((buff->flags & SDP_BUFF_F_QUEUEDED) > 0) {
		buff->sdp_buff_data_size += size;
		conn->sdpc_tx_bytes_queued += size;
		conn->sdpc_tx_bytes_unposted += size;
	} else {
		buff->sdp_buff_data_size =
		    PTRDIFF(buff->sdp_buff_tail, buff->sdp_buff_data);
		conn->sdpc_tx_bytes_queued += buff->sdp_buff_data_size;
		conn->sdpc_tx_bytes_unposted += buff->sdp_buff_data_size;

		buff->flags |= SDP_BUFF_F_QUEUEDED;

		/*
		 * Send the data buffer on to the next test...
		 */
		result = sdp_send_data_queue(conn, (sdp_generic_t *)buff);
		if (result != 0) {
			conn->sdpc_tx_bytes_queued -= buff->sdp_buff_data_size;
			conn->sdpc_tx_bytes_unposted -=
			    buff->sdp_buff_data_size;
			sdp_buff_free(buff);
		}
	}
	return (result);
}   /* sdp_send_data_buff_put */

/*
 * Control message handling:
 */

/*
 * Determine if it's OK to post a control msg.
 */
static int32_t
sdp_send_ctrl_buff_test(sdp_conn_t *conn, sdp_buff_t *buff)
{
	int32_t result = 0;

	SDP_CHECK_NULL(conn, -EINVAL);
	SDP_CHECK_NULL(buff, -EINVAL);

	if ((conn->state & SDP_ST_MASK_CTRL_OK) == 0)
		return (EINVAL);

	if (!(conn->s_wq_size < conn->scq_size) || !(conn->r_recv_bf > 0)) {
		return (EAGAIN);
	}

	/*
	 * Post the control buffer.
	 */
	result = sdp_send_buff_post(conn, buff);
	if (result != 0) {
		goto error;
	}
	SDP_STAT_INC(conn->sdp_sdps, OutControl);
	return (0);
error:
	return (result);
}   /* sdp_send_ctrl_buff_test */


/*
 * Flush control buffers, to send post.
 */
static void
sdp_send_ctrl_buff_flush(sdp_conn_t *conn)
{
	sdp_generic_t *element;
	int32_t result = 0;

	SDP_CHECK_NULL(conn, -EINVAL);

	/*
	 * As long as there are buffers, try to post until a non-zero result
	 * is generated. (Positive: no space; negative: error)
	 */
	while (sdp_generic_table_size(&conn->send_ctrl) > 0) {
		element = sdp_generic_table_look_head(&conn->send_ctrl);
		SDP_EXPECT((NULL != element));

		result = sdp_send_ctrl_buff_test(conn, (sdp_buff_t *)element);
		if (result != 0) {
			if (sdp_generic_table_member(element) == 0) {
				sdp_generic_table_put_head(
				    &conn->send_ctrl, element);
			}
			break;
		}
	}
}

/*
 * Send a buffered control message.
 */
static int32_t
sdp_send_ctrl_buff_buffered(sdp_conn_t *conn, sdp_buff_t *buff)
{
	int32_t result = 0;

	SDP_CHECK_NULL(conn, -EINVAL);
	SDP_CHECK_NULL(buff, -EINVAL);

	/*
	 * Either post a send, or buffer the packet in the tx queue
	 */
	if ((sdp_generic_table_size(&conn->send_ctrl) > 0 ||
	    (result = sdp_send_ctrl_buff_test(conn, buff)) != 0)) {
		if (result != EINVAL) {
			/*
			 * Save the buffer for later flushing into the post
			 * queue.
			 */
			sdp_generic_table_put_tail(&conn->send_ctrl,
			    (sdp_generic_t *)buff);
			return (0);
		}
	}
	return (result);
}   /* sdp_send_ctrl_buff_buffered */


/*
 * Create and send a buffered control message.
 */
static int32_t
sdp_send_ctrl_buff(sdp_conn_t *conn, uchar_t mid, int32_t se, int32_t sig)
{
	int32_t result = 0;
	sdp_buff_t *buff;

	SDP_CHECK_NULL(conn, -EINVAL);

	sdp_print_data(conn, "sdp_send_ctrl_buff: sigalled :<%d>", se);
	/*
	 * Create the message, which contains just the BSDH header. (Don't
	 * need to worry about header space reservation)
	 */
	buff = sdp_buff_get(conn);
	if (buff == NULL) {
		result = ENOMEM;
		goto error;
	}

	/*
	 * Set up header.
	 */
	buff->bsdh_hdr		= (sdp_msg_bsdh_t *)buff->sdp_buff_data;
	buff->bsdh_hdr->mid	= mid;
	buff->bsdh_hdr->flags	= SDP_MSG_FLAG_NON_FLAG;
	buff->sdp_buff_tail	= (char *)buff->sdp_buff_data +
	    sizeof (sdp_msg_bsdh_t);
	buff->sdp_buff_data_size = 0;

	/*
	 * Solicit event flag for IB sends.
	 */
	if (se) {
		SDP_BUFF_F_SET_SE(buff);
	}

	/*
	 * Try for unsignalled?
	 */
	if (sig == 0) {
		SDP_BUFF_F_SET_UNSIG(buff);
	}

	/*
	 * Either post a send, or buffer the packet in the tx queue
	 */
	result = sdp_send_ctrl_buff_buffered(conn, buff);
	if (result != 0) {
		sdp_buff_free(buff);
	}
error:
	return (result);
}   /* sdp_send_ctrl_buff */


/*
 * Send a disconnect request.
 */
static int32_t
send_ctrl_disconnect(sdp_conn_t *conn)
{
	sdp_buff_t *buff;

	SDP_CHECK_NULL(conn, -EINVAL);

	/*
	 * The connection may be aborted or marked with error. Do not send
	 * disconnect message in that case.
	 */
	if ((conn->state & SDP_ST_MASK_CTRL_OK) == 0)
		return (EINVAL);

	if (conn->r_recv_bf == 0)
		return (EAGAIN);
	/*
	 * Create the disconnect message, which contains just the BSDH
	 * header. (Don't need to worry about header space reservation)
	 */
	buff = sdp_buff_get(conn);
	if (buff == NULL)
		return (ENOMEM);

	/*
	 * Set up header.
	 */
	buff->bsdh_hdr		= (sdp_msg_bsdh_t *)buff->sdp_buff_data;
	buff->bsdh_hdr->mid	= SDP_MSG_MID_DISCONNECT;
	buff->bsdh_hdr->flags	= SDP_MSG_FLAG_NON_FLAG;
	buff->sdp_buff_tail	= (char *)buff->sdp_buff_data +
	    sizeof (sdp_msg_bsdh_t);
	buff->sdp_buff_data_size = 0;

	SDP_BUFF_F_CLR_SE(buff);
	SDP_BUFF_F_CLR_UNSIG(buff);


	ASSERT(conn->s_wq_size < conn->scq_size);
	ASSERT(conn->r_recv_bf > 0);
	/*
	 * Send
	 */
	return (sdp_send_buff_post(conn, buff));
}   /* sdp_send_ctrl_disconnect */

/*
 * Time to wait for cleanup -- in msec
 */
int sdp_cleanup_wait = 500;

/*
 * The timer is used to monitor the connetion closing process and see whether
 * something wrong with the CM manager or the peer system which causes no
 * progress can be made. If so, abort the connetion. There are two cases:
 *
 * a. If there is disconnection request pending, which means that either
 *    the send_queue is not empty or the send_post is full when the
 *    disconnection request was sent. Check whether the send_queue or
 *    s_wq_size changes since last time, if not, no TX progress is made
 *    and this perhaps means the remote peer hangs for some reason;
 *
 * b. If no disconnection request is pending, check whether the state
 *    changes, if not, it perhaps also means the peer or the CM
 *    manager hangs;
 *
 * In both cases, abort the connection; otherwise, restart the
 * timer.
 */
void
sdp_conn_timeout(void *arg)
{
	sdp_conn_t *conn = (sdp_conn_t *)arg;

	SDP_CONN_LOCK(conn);

	ASSERT(conn->s_wq_size <= conn->scq_size);
	ASSERT(conn->sdp_conn_tid != 0);

	if (conn->state == SDP_CONN_ST_CLOSED ||
	    conn->state == SDP_CONN_ST_ERROR ||
	    conn->state == SDP_CONN_ST_TIME_WAIT_2) {
		conn->sdp_conn_tid = 0;
	} else if (((conn->flags & SDP_CONN_F_DIS_PEND) &&
	    conn->sdp_saved_sendq_size ==
	    sdp_generic_table_size(&conn->send_queue) &&
	    conn->s_wq_size == conn->sdp_saved_s_wq_size) ||
	    (!(conn->flags & SDP_CONN_F_DIS_PEND) &&
	    conn->sdp_saved_state == conn->state)) {
		/* no progress, simply abort the connection. */
		sdp_conn_abort(conn);
		conn->sdp_conn_tid = 0;
	} else {
		/* progress is being made, restart the timer */
		if (conn->flags & SDP_CONN_F_DIS_PEND) {
			conn->sdp_saved_sendq_size =
			    sdp_generic_table_size(&conn->send_queue);
			conn->sdp_saved_s_wq_size = conn->s_wq_size;
		} else {
			conn->sdp_saved_state = conn->state;
		}
		conn->sdp_conn_tid = timeout(sdp_conn_timeout, conn,
		    MSEC_TO_TICK(sdp_cleanup_wait));
	}
	if (conn->sdp_conn_tid == 0) {
		conn->flags &= ~SDP_CONN_F_DIS_PEND;
		SDP_CONN_PUT_ISR(conn);
	}
	SDP_CONN_UNLOCK(conn);
}

/*
 * Potentially send a disconnect request.
 */
void
sdp_send_ctrl_disconnect(sdp_conn_t *conn)
{
	int32_t result = 0;

	SDP_CHECK_NULL(conn, -EINVAL);
	ASSERT(conn->s_wq_size <= conn->scq_size);

	/*
	 * Only create/post the message if there is no data in the data
	 * queue and the send_post queue is not full, and start a
	 * timer which will periodically check the state transition. If
	 * no state transition is made, it means that there is something
	 * wrong either with the CM or the peer, abort the connetion then.
	 * See more in sdp_conn_timeout()
	 */
	if (sdp_generic_table_size(&conn->send_queue) == 0 &&
	    conn->s_wq_size < conn->scq_size) {
		conn->flags &= ~SDP_CONN_F_DIS_PEND;

		result = send_ctrl_disconnect(conn);
		if (result != 0) {
			/*
			 * sdp_conn_abort() sets the state to
			 * SDP_CONN_ST_ERROR. So even this is called from
			 * sdp_send_flush() and the timer is started,
			 * the timeout routine will do nothing.
			 */
			sdp_conn_abort(conn);
			return;
		}

		/*
		 * If a disconnect request was not sent successfully,
		 * a timer was started. See sdp_conn_timeout().
		 */
		if (conn->sdp_conn_tid != 0) {
			/*
			 * Timer was already started, which means that
			 * this is called from sdp_send_flush().
			 * since we cannot untimeout() with the
			 * lock held, we do the trick by setting
			 * saved_state to 0, so that the timeout
			 * routine will always restart the timer
			 */
			conn->sdp_saved_state = 0;
		} else {
			conn->sdp_saved_state = conn->state;
			conn->sdp_conn_tid = timeout(sdp_conn_timeout,
			    conn, MSEC_TO_TICK(sdp_cleanup_wait));
			if (conn->sdp_conn_tid != 0) {
				SDP_CONN_HOLD(conn);
			} else {
				sdp_conn_abort(conn);
			}
		}
		return;
	}

	/*
	 * Otherwise, mark the connection with disconnect request pending,
	 * and this function will be called again when sdp_send_flush() is
	 * done. A timer is also started in case the remote port is down and
	 * no further TX progress can be made. See sdp_conn_timeout().
	 *
	 * If sdp_conn_tid is non-zero, which means this function is called
	 * from sdp_send_flush(), no need to restart the timer since it was
	 * already started.
	 */
	conn->flags |= SDP_CONN_F_DIS_PEND;
	if (conn->sdp_conn_tid == 0) {
		conn->sdp_saved_sendq_size =
		    sdp_generic_table_size(&conn->send_queue);
		conn->sdp_saved_s_wq_size = conn->s_wq_size;
		conn->sdp_conn_tid = timeout(sdp_conn_timeout, conn,
		    MSEC_TO_TICK(sdp_cleanup_wait));
		if (conn->sdp_conn_tid != 0) {
			SDP_CONN_HOLD(conn);
		} else {
			conn->flags &= ~SDP_CONN_F_DIS_PEND;
			sdp_conn_abort(conn);
		}
	}
}

/*
 * Send a gratuitous ack.
 */
int32_t
sdp_send_ctrl_ack(sdp_conn_t *conn)
{
	SDP_CHECK_NULL(conn, -EINVAL);

	/*
	 * The gratuitous ack is not really an ack, but an update of the
	 * number of buffers posted for receive. This is important when
	 * traffic is only moving in one direction.
	 */
	if ((conn->r_recv_bf > SDP_MIN_BUFF_COUNT &&
	    (sdp_generic_table_size(&conn->send_queue) > 0)) ||
	    (sdp_generic_table_size(&conn->send_ctrl) > 0)) {
		/*
		 * We have data and/or ctrl messages queued for transmission
		 * and we also have enough credits to send one of these messages
		 * So there is no need to send a separate ctrl ack to update
		 * the credits. It will piggy back on one of these messages.
		 */
		return (0);
	}

	return (sdp_send_ctrl_buff(conn, SDP_MSG_MID_DATA,
	    B_FALSE, B_FALSE));
}   /* sdp_send_ctrl_ack */


/*
 * Send a request for buffered mode.
 * This routine is currently unused in Solaris.
 */
int32_t
sdp_send_ctrl_send_sm(sdp_conn_t *conn)
{
	int32_t	result;

	result = sdp_send_ctrl_buff(conn, SDP_MSG_MID_SEND_SM, B_TRUE, B_TRUE);
	if (result == 0) {
		SDP_STAT_INC(conn->sdp_sdps, OutSendSm);
	}

	return (result);
}   /* sdp_send_ctrl_send_sm */


/*
 * Send a source cancel
 */
int32_t
sdp_send_ctrl_src_cancel(sdp_conn_t *conn)
{
	int32_t	result;

	result = sdp_send_ctrl_buff(conn, SDP_MSG_MID_SRC_CANCEL,
	    B_TRUE, B_TRUE);
	if (result == 0) {
		SDP_STAT_INC(conn->sdp_sdps, OutSrcCancel);
	}

	return (result);
}   /* sdp_send_ctrl_src_cancel */


/*
 * Send a sink cancel
 */
int32_t
sdp_send_ctrl_snk_cancel(sdp_conn_t *conn)
{
	int32_t	result;

	result = sdp_send_ctrl_buff(conn,
	    SDP_MSG_MID_SNK_CANCEL, B_TRUE, B_TRUE);
	if (result == 0) {
		SDP_STAT_INC(conn->sdp_sdps, OutSinkCancel);
	}

	return (result);
}   /* send_ctrl_snk_cancel */


/*
 * Send an ack for a sink cancel
 */
int32_t
sdp_send_ctrl_snk_cancel_ack(sdp_conn_t *conn)
{
	int32_t	result;

	result =  sdp_send_ctrl_buff(conn,
	    SDP_MSG_MID_SNK_CANCEL_ACK, B_TRUE, B_TRUE);
	if (result == 0) {
		SDP_STAT_INC(conn->sdp_sdps, OutSinkCancelAck);
	}

	return (result);
}   /* sdp_send_ctrl_snk_cancel_ack */


/*
 * Send an abort message.
 */
int32_t
sdp_send_ctrl_abort(sdp_conn_t *conn)
{
	int32_t result;
	SDP_CHECK_NULL(conn, -EINVAL);

	result = sdp_send_ctrl_buff(conn, SDP_MSG_MID_ABORT_CONN,
	    B_TRUE, B_TRUE);
	if (result == 0) {
		SDP_STAT_INC(conn->sdp_sdps, OutAborts);
	}
	return (result);
}   /* sdp_send_ctrl_abort */


/*
 * send_ctrl_resize_buff_ack -- send an ack for a buffer size change
 */
int32_t
sdp_send_ctrl_resize_buff_ack(sdp_conn_t *conn, uint32_t size)
{
	sdp_msg_crbah_t *crbah;
	int32_t result = 0;
	sdp_buff_t *buff;

	SDP_CHECK_NULL(conn, -EINVAL);

	/*
	 * Create the message, which contains just the BSDH header. (Don't
	 * need to worry about header space reservation)
	 */
	buff = sdp_buff_get(conn);
	if (buff == NULL) {
		result = ENOMEM;
		goto error;
	}

	/*
	 * Set up header.
	 */
	buff->sdp_buff_tail	= buff->sdp_buff_data;
	buff->bsdh_hdr		= (sdp_msg_bsdh_t *)buff->sdp_buff_tail;
	buff->bsdh_hdr->mid	= SDP_MSG_MID_CH_RECV_BUF_ACK;
	buff->bsdh_hdr->flags	= SDP_MSG_FLAG_NON_FLAG;
	buff->sdp_buff_tail	=
	    (char *)buff->sdp_buff_tail + sizeof (sdp_msg_bsdh_t);
	crbah			= (sdp_msg_crbah_t *)buff->sdp_buff_tail;
	crbah->size		= size;
	buff->sdp_buff_tail	=
	    (char *)buff->sdp_buff_tail + sizeof (sdp_msg_crbah_t);
	SDP_BUFF_F_CLR_SE(buff);
	SDP_BUFF_F_CLR_UNSIG(buff);

	/*
	 * Endian swap of extended header
	 */
	result = sdp_msg_host_to_wire_crbah(crbah);
	SDP_EXPECT(!(0 > result));

	/*
	 * Either post a send, or buffer the packet in the tx queue
	 */
	result = sdp_send_ctrl_buff_buffered(conn, buff);
	if (result != 0) {
		sdp_buff_free(buff);
	} else {
		SDP_STAT_INC(conn->sdp_sdps, OutResizeAck);
	}

error:
	return (result);
}   /* send_ctrl_resize_buff_ack */


/*
 * sdp_send_ctrl_rdma_rd_comp -- send an rdma read completion
 */
int32_t
sdp_send_ctrl_rdma_rd_comp(sdp_conn_t *conn, int32_t size)
{
	sdp_msg_rrch_t *rrch;
	int32_t result = 0;
	sdp_buff_t *buff;

	SDP_CHECK_NULL(conn, -EINVAL);

	/*
	 * check size
	 */
	if (size < 0) {
		return (-ERANGE);
	}

	/*
	 * Create the message, which contains just the BSDH header. (Don't
	 * need to worry about header space reservation)
	 */
	buff = sdp_buff_get(conn);
	if (buff == NULL) {
		result = -ENOMEM;
		goto error;
	}

	/*
	 * Set up header.
	 */
	buff->sdp_buff_tail	= buff->sdp_buff_data;
	buff->bsdh_hdr		= (sdp_msg_bsdh_t *)buff->sdp_buff_tail;
	buff->bsdh_hdr->mid	= SDP_MSG_MID_RDMA_RD_COMP;
	buff->bsdh_hdr->flags	= SDP_MSG_FLAG_NON_FLAG;
	buff->sdp_buff_tail	= (char *)buff->sdp_buff_tail +
	    sizeof (sdp_msg_bsdh_t);
	rrch			= (sdp_msg_rrch_t *)buff->sdp_buff_tail;
	rrch->size		= (uint32_t)size;
	buff->sdp_buff_tail	= (char *)buff->sdp_buff_tail +
	    sizeof (sdp_msg_rrch_t);
	/*
	 * Solicit event
	 */

	SDP_BUFF_F_SET_SE(buff);

	SDP_BUFF_F_SET_UNSIG(buff);

	/*
	 * Set PIPE bit to request switch into pipeline mode.
	 */
	SDP_MSG_HDR_SET_REQ_PIPE(buff->bsdh_hdr);

	/*
	 * Endian swap of extended header
	 */
	result = sdp_msg_host_to_wire_rrch(rrch);
	SDP_EXPECT(!(0 > result));

	/*
	 * Either post a send, or buffer the packet in the tx queue
	 */
	result = sdp_send_ctrl_buff_buffered(conn, buff);
	if (result != 0) {
		sdp_buff_free(buff);
	} else {
		SDP_STAT_INC(conn->sdp_sdps, OutRdmaRdCompl);
	}

error:
	return (result);
}   /* sdp_send_ctrl_rdma_rd_comp */

/* ========================================================================= */

/*
 * sdp_send_ctrl_rdma_wr_comp -- send an rdma write completion
 */
int32_t
sdp_send_ctrl_rdma_wr_comp(sdp_conn_t *conn, uint32_t size)
{
	sdp_msg_rwch_t *rwch;
	int32_t result = 0;
	sdp_buff_t *buff;

	SDP_CHECK_NULL(conn, -EINVAL);

	/*
	 * Create the message, which contains just the BSDH header. (Don't
	 * need to worry about header space reservation.)
	 */
	buff = sdp_buff_get(conn);
	if (buff == NULL) {
		result = -ENOMEM;
		goto error;
	}

	/*
	 * Set up header.
	 */
	buff->sdp_buff_tail	= buff->sdp_buff_data;
	buff->bsdh_hdr		= (sdp_msg_bsdh_t *)buff->sdp_buff_tail;
	buff->bsdh_hdr->mid	= SDP_MSG_MID_RDMA_WR_COMP;
	buff->bsdh_hdr->flags	= SDP_MSG_FLAG_NON_FLAG;
	buff->sdp_buff_tail	= (char *)buff->sdp_buff_tail +
	    sizeof (sdp_msg_bsdh_t);
	rwch			= (sdp_msg_rwch_t *)buff->sdp_buff_tail;
	rwch->size		= size;
	buff->sdp_buff_tail	= (char *)buff->sdp_buff_tail +
	    sizeof (sdp_msg_rwch_t);
	/*
	 * Solicit event
	 */

	SDP_BUFF_F_SET_SE(buff);

	SDP_BUFF_F_SET_UNSIG(buff);

	/*
	 * Endian swap of extended header
	 */
	result = sdp_msg_host_to_wire_rwch(rwch);
	SDP_EXPECT(!(0 > result));

	/*
	 * Either post a send, or buffer the packet in the tx queue
	 */
	result = sdp_send_ctrl_buff_buffered(conn, buff);
	if (result != 0) {
		sdp_buff_free(buff);
	} else {
		SDP_STAT_INC(conn->sdp_sdps, OutRdmaWrCompl);
	}

error:
	return (result);
}   /* sdp_send_ctrl_rdma_wr_comp */

/* ========================================================================= */

/*
 * sdp_send_ctrl_snk_avail -- send a sink available message
 */
int32_t
sdp_send_ctrl_snk_avail(sdp_conn_t *conn, uint32_t size,
		uint32_t rkey, uint64_t addr)
{
	sdp_msg_snkah_t *snkah;
	int32_t result = 0;
	sdp_buff_t *buff;

	SDP_CHECK_NULL(conn, -EINVAL);

	/*
	 * check mode
	 */
	if (SDP_MODE_PIPE != conn->recv_mode) {
		result = -EPROTO;
		goto error;
	}

	/*
	 * Create the message, which contains just the BSDH header. (Don't
	 * need to worry about header space reservation)
	 */
	buff = sdp_buff_get(conn);
	if (buff == NULL) {
		result = -ENOMEM;
		goto error;
	}

	/*
	 * Set up header.
	 */
	buff->sdp_buff_tail	= buff->sdp_buff_data;
	buff->bsdh_hdr		= (sdp_msg_bsdh_t *)buff->sdp_buff_tail;
	buff->bsdh_hdr->mid	= SDP_MSG_MID_SNK_AVAIL;
	buff->bsdh_hdr->flags	= SDP_MSG_FLAG_NON_FLAG;
	buff->sdp_buff_tail	= (char *)buff->sdp_buff_tail +
	    sizeof (sdp_msg_bsdh_t);
	snkah			= (sdp_msg_snkah_t *)buff->sdp_buff_tail;
	snkah->size		= size;
	snkah->r_key		= rkey;
	snkah->addr		= addr;
	/*
	 * If no IOCB, then this will be 0.
	 */
	snkah->non_disc = conn->nond_recv;
	buff->sdp_buff_tail = (char *)buff->sdp_buff_tail +
	    sizeof (sdp_msg_snkah_t);
	buff->sdp_buff_data_size = 0;

	SDP_BUFF_F_CLR_SE(buff);
	SDP_BUFF_F_SET_UNSIG(buff);

	/*
	 * Endian swap of extended header.
	 */
	result = sdp_msg_host_to_wire_snkah(snkah);
	SDP_EXPECT(!(0 > result));

	/*
	 * Either post a send, or buffer the packet in the tx queue.
	 */
	result = sdp_send_ctrl_buff_buffered(conn, buff);
	if (result != 0) {
		sdp_buff_free(buff);
	} else {
		SDP_STAT_INC(conn->sdp_sdps, OutSinkAvail);
	}

error:
	return (result);
}   /* sdp_send_ctrl_snk_avail */

/* ========================================================================= */

/*
 * sdp_send_ctrl_mode_change -- send a mode change command
 */
int32_t
sdp_send_ctrl_mode_change(sdp_conn_t *conn, uchar_t mode)
{
	sdp_msg_mch_t *mch;
	int32_t result = 0;
	sdp_buff_t *buff;

	SDP_CHECK_NULL(conn, -EINVAL);

	sdp_print_ctrl(conn, "sdp_send_ctrl_mode_change: mode <%d>", mode);
	/*
	 * validate that the requested mode transition is OK.
	 */
	switch (mode) {
		case SDP_MSG_MCH_BUFF_RECV:	/* source to sink */
			conn->send_mode =
			    ((SDP_MODE_COMB ==
			    conn->send_mode) ? SDP_MODE_BUFF : SDP_MODE_ERROR);
			break;
		case SDP_MSG_MCH_COMB_SEND:	/* sink to source */
			conn->recv_mode =
			    ((SDP_MODE_BUFF ==
			    conn->recv_mode) ? SDP_MODE_COMB : SDP_MODE_ERROR);
			break;
		case SDP_MSG_MCH_PIPE_RECV:	/* source to sink */
			conn->send_mode =
			    ((SDP_MODE_COMB ==
			    conn->send_mode) ? SDP_MODE_PIPE : SDP_MODE_ERROR);
			break;
		case SDP_MSG_MCH_COMB_RECV:	/* source to sink */
			conn->send_mode =
			    ((SDP_MODE_PIPE ==
			    conn->send_mode) ? SDP_MODE_COMB : SDP_MODE_ERROR);
			break;
		default:
			result = -EPROTO;
			goto error;
	}	/* switch */

	if (conn->send_mode == SDP_MODE_ERROR ||
	    conn->recv_mode == SDP_MODE_ERROR) {
		result = -EPROTO;
		goto error;
	}

	/*
	 * Create the message, which contains just the BSDH header. (Don't
	 * need to worry about header space reservation)
	 */
	buff = sdp_buff_get(conn);
	if (buff == NULL) {
		result = -ENOMEM;
		goto error;
	}

	/*
	 * Set up header.
	 */
	buff->sdp_buff_tail	= buff->sdp_buff_data;
	buff->bsdh_hdr		= (sdp_msg_bsdh_t *)buff->sdp_buff_tail;
	buff->bsdh_hdr->mid	= SDP_MSG_MID_MODE_CHANGE;
	buff->bsdh_hdr->flags	= SDP_MSG_FLAG_NON_FLAG;
	buff->sdp_buff_tail	=
	    (char *)buff->sdp_buff_tail + sizeof (sdp_msg_bsdh_t);
	mch			= (sdp_msg_mch_t *)buff->sdp_buff_tail;
	buff->sdp_buff_tail	=
	    (char *)buff->sdp_buff_tail + sizeof (sdp_msg_mch_t);
	SDP_BUFF_F_SET_SE(buff);
	SDP_BUFF_F_CLR_UNSIG(buff);
	SDP_MSG_MCH_SET_MODE(mch, mode);

	/*
	 * Endian swap of extended header
	 */
	result = sdp_msg_host_to_wire_mch(mch);
	SDP_EXPECT(!(0 > result));

	/*
	 * either post a send, or buffer the packet in the tx queue
	 */
	result = sdp_send_ctrl_buff_buffered(conn, buff);
	if (result != 0) {
		sdp_buff_free(buff);
	} else {
		SDP_STAT_INC(conn->sdp_sdps, OutModeChange);
	}

error:
	return (result);
}   /* sdp_send_ctrl_mode_change */

/* ========================================================================= */

/*
 * sdp_send_advt_flush -- flush passive sink advertisements
 */
static void
sdp_send_advt_flush(sdp_conn_t *conn)
{
	sdp_advt_t *advt;

	/*
	 * if there is no data in the pending or active send pipes, and a
	 * partially complete sink advertisement is pending, then it needs to
	 * be completed. it might be some time until more data is ready for
	 * transmission, and the remote host needs to be notified of present
	 * data. (rdma ping-pong letency test...)
	 */
	if (sdp_generic_table_size(&conn->send_queue) == 0) {
		/*
		 * might be more aggressive then we want it to be. maybe
		 * check if the active sink queue is empty as well?
		 */
		advt = sdp_conn_advt_table_look(&conn->snk_pend);
		if (advt != NULL && advt->post > 0) {

			advt = sdp_conn_advt_table_get(&conn->snk_pend);
			SDP_EXPECT((NULL != advt));

			(void) sdp_send_ctrl_rdma_wr_comp(conn, advt->post);

			(void) sdp_conn_advt_destroy(advt);

			/*
			 * update sink advertisements.
			 */
			conn->snk_recv--;
		}	/* if */
	}	/* if */
}   /* sdp_send_advt_flush */

/*
 * GENERAL functions
 */

/*
 * sdp_send_flush -- flush buffers from send queue, in to send post.
 */
void
sdp_send_flush(sdp_conn_t *conn)
{
	SDP_CHECK_NULL(conn, -EINVAL);

	/*
	 * keep posting sends as long as there is room for an SDP post.
	 * priority goes to control messages, and we need to follow the send
	 * credit utilization rules.
	 */
	sdp_send_ctrl_buff_flush(conn);

	/*
	 * * data flush
	 */
	sdp_send_data_queue_flush(conn);

	/*
	 * * sink advertiesment flush.
	 */
	if (conn_advt_table_size(&conn->snk_pend) > 0) {
		sdp_send_advt_flush(conn);
	}

	/*
	 * disconnect flush
	 */
	if (conn->flags & SDP_CONN_F_DIS_PEND)
		sdp_send_ctrl_disconnect(conn);

	if ((conn->xmitflags & SDP_WRFULL) && sdp_inet_writable(conn)) {
		dprint(SDP_DATA, ("sdp_send_flush:writable ?:%d xmit_flags:%d",
		    sdp_inet_writable(conn), (conn->xmitflags & SDP_WRFULL)));
		conn->xmitflags &= ~SDP_WRFULL;
		cv_signal(&conn->ss_txdata_cv);
	}
}


/*
 * Send data to the network.  sdp_send() is called directly
 * from sockfs().  The send path today accepts only
 * buffered, not asynchronous, data for transmission.  The protocol
 * implements an ordered byte stream, where data is guaranteed to be
 * delivered in the same order in which it was sent. If there is an
 * error in transmission, that error is reported to both halves
 * of the connection.
 *
 * sdp_send() holds the connection lock throughout kernel-level
 * processing of data passed to it in a single invocation,
 * although some of the data may be buffered (not yet on the wire)
 * when the routine returns and the lock is dropped.  SDP sets the
 * write queue to full before return if network write space
 * isn't large enough to accept another data chunk from the
 * user.  Send interrupt processing is able to clear the
 * write side and push buffered data onto the network when enough
 * network write space has become available. Note that data is
 * buffered for reasons other than lack of write space: for example,
 * to consolidate data in buffers. Much of the protocol complexity
 * is spared, however, in the absence of asynchronous communication.
 * Protocol and other considerations for "stalling" the send pipeline
 * are described in the SDP implementation design document,
 * Section 5.1.1.
 *
 * This entry point from gets a 4KB buffer from its pool
 * of preallocated buffers, and copies up to 4KB of mblock data
 * minus the amount needed for the BSDH header.  It processes this
 * data, eventually either queuing it or posting it to the network via
 * ibt_post_send() in the routine sdp_send_buff_post().  If the
 * data is queued, it is posted to the network later as a result of a
 * flush called from the send interrupt routine.  All data that is
 * sent gets posted via sdp_send_buff_post().
 *
 * The routine loops through data until all data has been
 * copied and queued or sent.
 *
 */
/*ARGSUSED*/
int
sdp_send(sdp_conn_t *conn, struct msghdr *msg, size_t size,
    int flags, struct uio *uiop)
{
	sdp_buff_t	*buff		= NULL;
	int		result		= 0;
	int		copied		= 0;
	int		copy_amt	= 0;
	int		write_space_bytes = 0;
	boolean_t	oob;

	oob = flags & MSG_OOB;
	SDP_CONN_LOCK(conn);

	if (SDP_SHUTDOWN_SEND & conn->shutdown) {
		result = EPIPE;
		SDP_CONN_UNLOCK(conn);
		return (result);
	}

	if (conn->state == SDP_CONN_ST_LISTEN ||
	    conn->state == SDP_CONN_ST_CLOSED ||
	    conn->state == SDP_CONN_ST_ERROR ||
	    conn->state == SDP_CONN_ST_TIME_WAIT_1 ||
	    conn->state == SDP_CONN_ST_TIME_WAIT_2) {
		sdp_print_note(conn, "sdp_send: conn state not valid:<%04x>",
		    conn->state);
		result = ENOTCONN;
		SDP_CONN_UNLOCK(conn);
		return (result);
	}

	while (copied < size) {
		while (sdp_inet_write_space(conn, oob) > 0) {
			buff = sdp_send_data_buff_get(conn);
			if (buff == NULL) {
				result = ENOMEM;
				goto done;
			}	/* if */
			ASSERT((char *)buff->sdp_buff_data >=
			    ((char *)buff->sdp_buff_head +
			    sizeof (sdp_msg_bsdh_t)));
			copy_amt = min(PTRDIFF(buff->sdp_buff_end,
			    buff->sdp_buff_tail), (size - copied));
			copy_amt = min(copy_amt, sdp_inet_write_space(conn,
			    oob));
			result = uiomove(buff->sdp_buff_tail, copy_amt,
			    UIO_WRITE, uiop);
			if (result != 0) {
				sdp_buff_free(buff);
				goto done;
			}
			buff->sdp_buff_tail = (char *)buff->sdp_buff_tail +
			    copy_amt;
			copied += copy_amt;

			result = sdp_send_data_buff_put(conn, buff,
			    copy_amt, ((copied == size) ? oob : 0));
			if (result != 0) {
				goto done;
			}
			SDP_CONN_STAT_SEND_INC(conn, copy_amt);


			dprint(SDP_DATA, ("DATA: <%p> sdp_send: copied:%d "
			    "data out of total:%d", (void *)conn, copied,
			    (int)size));
			if (copied == size)
				goto done;
		}

		if (SDP_SHUTDOWN_SEND & conn->shutdown) {
			result = EPIPE;
			break;
		}

		if (conn->state == SDP_CONN_ST_ERROR) {
			result = EPROTO;
			break;
		}

		if (flags & MSG_DONTWAIT) {
			result = EWOULDBLOCK;
			break;
		}

		if (sdp_inet_write_space(conn, oob) <= 0) {
			conn->xmitflags |= SDP_WRFULL;
			dprint(SDP_DBG, ("DBG <%p> sdp_send: Write side full: "
			    "write:%d snd_max:%d qud:%d size:%d xmit_flag:%d",
			    (void *)conn, write_space_bytes,
			    conn->sdpc_tx_max_queue_size,
			    conn->sdpc_tx_bytes_queued, (int)size,
			    conn->xmitflags));

			if (!cv_wait_sig(&conn->ss_txdata_cv,
			    &conn->conn_lock)) {
				result = EINTR;
				break;
			}

		}
	}

done:
	SDP_STAT_UPDATE(conn->sdp_sdps, OutDataBytes, size);
	SDP_CONN_UNLOCK(conn);

	dprint(SDP_DBG, ("DBG: <%p> sdp_send: bytes send:<%d> size <%d>"
	    "result:%d", (void *)conn, (int)copied, (int)size, result));
	result = (int32_t)((copied == size) ? 0 : result);
	return (result);
}   /* sdp_send */
