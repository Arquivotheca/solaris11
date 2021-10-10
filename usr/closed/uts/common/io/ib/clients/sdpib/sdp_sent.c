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
 * specific MID handler functions. (SEND)
 */
void
sdp_rsrv_disconnect(void *arg)
{
	sdp_conn_t *conn = (sdp_conn_t *)arg;

	if (conn == NULL) {
		return;
	}
	dprint(SDP_DBG, ("sdp_rsrv_discon: conn:%p state:%x", (void *)conn,
	    conn->state));
	SDP_CONN_LOCK(conn);
	/*
	 * begin IB/CM disconnect
	 */
	sdp_cm_disconnect(conn);

	SDP_CONN_UNLOCK(conn);
}



/*
 * sdp_event_send_disconnect --
 */
/* ARGSUSED */
static int32_t
sdp_event_send_disconnect(sdp_conn_t *conn, sdp_buff_t *buff)
{
	SDP_CHECK_NULL(conn, -EINVAL);
	SDP_CHECK_NULL(buff, -EINVAL);


	dprint(SDP_DBG,  ("sdp_event_send_disconn: conn:%p state:%x ",
	    (void *)conn, conn->state));
	switch (conn->state) {
		case SDP_CONN_ST_TIME_WAIT_2:

			/*
			 * nothing to do, CM disconnects have been exchanged.
			 */
			break;
		case SDP_CONN_ST_DIS_SEND_1:

			/*
			 * active disconnect message send completed
			 */
			SDP_CONN_ST_SET(conn, SDP_CONN_ST_DIS_SENT_1);

			break;
		case SDP_CONN_ST_DIS_SEND_2:
		case SDP_CONN_ST_DIS_RECV_R:

			/*
			 * simultaneous disconnect. received a disconnect,
			 * after we initiated one.
			 * This needs to be handled as the active
			 * stream interface close that it is.
			 */
			SDP_CONN_ST_SET(conn, SDP_CONN_ST_TIME_WAIT_1);

			/*
			 * If we are here in the send part due shutdown(WR)
			 * then we send a disconnect to the framework.
			 */
			if (conn->sdp_ulpd) {
				dprint(SDP_DBG,  ("sdp_Send_discon to sockfs"));
				conn->sdp_ulp_disconnected(conn->sdp_ulpd, 0);
			}

			/*
			 * begin IB/CM disconnect
			 */
			sdp_cm_disconnect(conn);

			break;
		case SDP_CONN_ST_ERROR:
			break;
		default:

			return (-EFAULT);
	}

	return (0);
}

/* ========================================================================= */

/*
 * sdp_event_send_abort_conn --
 */
/* ARGSUSED */
static int32_t
sdp_event_send_abort_conn(sdp_conn_t *conn, sdp_buff_t *buff)
{
	SDP_CHECK_NULL(conn, -EINVAL);
	SDP_CHECK_NULL(buff, -EINVAL);

	/*
	 * the gateway interface should be in error state, initiate CM
	 * disconnect.
	 */
	SDP_CONN_ST_SET(conn, SDP_CONN_ST_CLOSED);

	sdp_cm_disconnect(conn);
	return (0);
}   /* sdp_event_send_abort_conn */

/* ========================================================================= */

/*
 * sdp_event_send_send_sm --
 */
/* ARGSUSED */
static int32_t
sdp_event_send_send_sm(sdp_conn_t *conn, sdp_buff_t *buff)
{
	SDP_CHECK_NULL(conn, -EINVAL);
	SDP_CHECK_NULL(buff, -EINVAL);

	return (0);
}   /* sdp_event_send_send_sm */

/* ========================================================================= */

/*
 * sdp_event_send_rdma_write_comp --
 */
/* ARGSUSED */
static int32_t
sdp_event_send_rdma_write_comp(sdp_conn_t *conn, sdp_buff_t *buff)
{
	/* LINTED */
	sdp_msg_rwch_t *rwch = (sdp_msg_rwch_t *)buff->sdp_buff_data;

	SDP_CHECK_NULL(conn, -EINVAL);
	SDP_CHECK_NULL(buff, -EINVAL);

	buff->sdp_buff_data =
	    (char *)buff->sdp_buff_data + sizeof (sdp_msg_rwch_t);

	return (0);
}   /* sdp_event_send_rdma_write_comp */

/* ========================================================================= */

/*
 * sdp_event_send_rdma_read_comp --
 */
/* ARGSUSED */
static int32_t
sdp_event_send_rdma_read_comp(sdp_conn_t *conn, sdp_buff_t *buff)
{
	/* LINTED */
	sdp_msg_rrch_t *rrch = (sdp_msg_rrch_t *)buff->sdp_buff_data;

	SDP_CHECK_NULL(conn, -EINVAL);
	SDP_CHECK_NULL(buff, -EINVAL);

	buff->sdp_buff_data =
	    (char *)buff->sdp_buff_data + sizeof (sdp_msg_rrch_t);

	return (0);
}   /* sdp_event_send_rdma_read_comp */

/* ========================================================================= */

/*
 * sdp_event_send_mode_change --
 */
/* ARGSUSED */
static int32_t
sdp_event_send_mode_change(sdp_conn_t *conn, sdp_buff_t *buff)
{
	/* LINTED */
	sdp_msg_mch_t *mch = (sdp_msg_mch_t *)buff->sdp_buff_data;

	SDP_CHECK_NULL(conn, -EINVAL);
	SDP_CHECK_NULL(buff, -EINVAL);

	buff->sdp_buff_data =
	    (char *)buff->sdp_buff_data + sizeof (sdp_msg_mch_t);

	return (0);
}   /* sdp_event_send_mode_change */

/* ========================================================================= */

/*
 * sdp_event_send_src_cancel --
 */
/* ARGSUSED */
static int32_t
sdp_event_send_src_cancel(sdp_conn_t *conn, sdp_buff_t *buff)
{
	SDP_CHECK_NULL(conn, -EINVAL);
	SDP_CHECK_NULL(buff, -EINVAL);

	return (0);
}   /* sdp_event_send_src_cancel */

/* ========================================================================= */

/*
 * sdp_event_send_sink_cancel --
 */
/* ARGSUSED */
static int32_t
sdp_event_send_sink_cancel(sdp_conn_t *conn, sdp_buff_t *buff)
{
	SDP_CHECK_NULL(conn, -EINVAL);
	SDP_CHECK_NULL(buff, -EINVAL);

	return (0);
}   /* sdp_event_send_sink_cancel */

/* ========================================================================= */

/*
 * sdp_event_send_sink_cancel_ack --
 */
/* ARGSUSED */
static int32_t
sdp_event_send_sink_cancel_ack(sdp_conn_t *conn, sdp_buff_t *buff)
{
	SDP_CHECK_NULL(conn, -EINVAL);
	SDP_CHECK_NULL(buff, -EINVAL);

	return (0);
}   /* sdp_event_send_sink_cancel_ack */

/* ========================================================================= */

/*
 * sdp_event_send_change_rcv_buf_ack --
 */
/* ARGSUSED */
static int32_t
sdp_event_send_change_rcv_buf_ack(sdp_conn_t *conn, sdp_buff_t *buff)
{
	/* LINTED */
	sdp_msg_crbah_t *crbah = (sdp_msg_crbah_t *)buff->sdp_buff_data;

	SDP_CHECK_NULL(conn, -EINVAL);
	SDP_CHECK_NULL(buff, -EINVAL);

	buff->sdp_buff_data =
	    (char *)buff->sdp_buff_data + sizeof (sdp_msg_crbah_t);

	return (0);
}   /* sdp_event_send_change_rcv_buf_ack */

/* ========================================================================= */

/*
 * sdp_event_send_suspend --
 */
/* ARGSUSED */
static int32_t
sdp_event_send_suspend(sdp_conn_t *conn, sdp_buff_t *buff)
{
	/* LINTED */
	sdp_msg_sch_t *sch = (sdp_msg_sch_t *)buff->sdp_buff_data;

	SDP_CHECK_NULL(conn, -EINVAL);
	SDP_CHECK_NULL(buff, -EINVAL);

	buff->sdp_buff_data =
	    (char *)buff->sdp_buff_data + sizeof (sdp_msg_sch_t);

	return (0);
}   /* sdp_event_send_suspend */

/* ========================================================================= */

/*
 * sdp_event_send_suspend_ack --
 */
/* ARGSUSED */
static int32_t
sdp_event_send_suspend_ack(sdp_conn_t *conn, sdp_buff_t *buff)
{
	SDP_CHECK_NULL(conn, -EINVAL);
	SDP_CHECK_NULL(buff, -EINVAL);

	return (0);
}   /* sdp_event_send_suspend_ack */

/* ========================================================================= */

/*
 * sdp_event_send_snk_avail --
 */
/* ARGSUSED */
static int32_t
sdp_event_send_snk_avail(sdp_conn_t *conn, sdp_buff_t *buff)
{
	/* LINTED */
	sdp_msg_snkah_t *snkah = (sdp_msg_snkah_t *)buff->sdp_buff_data;

	SDP_CHECK_NULL(conn, -EINVAL);
	SDP_CHECK_NULL(buff, -EINVAL);

	buff->sdp_buff_data =
	    (char *)buff->sdp_buff_data + sizeof (sdp_msg_snkah_t);

	return (0);
}   /* sdp_event_send_snk_avail */

/* ========================================================================= */

/*
 * sdp_event_send_src_avail --
 */
/* ARGSUSED */
static int32_t
sdp_event_send_src_avail(sdp_conn_t *conn, sdp_buff_t *buff)
{
	/* LINTED */
	sdp_msg_srcah_t *srcah = (sdp_msg_srcah_t *)buff->sdp_buff_data;

	SDP_CHECK_NULL(conn, -EINVAL);
	SDP_CHECK_NULL(buff, -EINVAL);

	buff->sdp_buff_data =
	    (char *)buff->sdp_buff_data + sizeof (sdp_msg_srcah_t);

	return (0);
}   /* sdp_event_send_src_avail */

/* ========================================================================= */

/*
 * sdp_event_send_data -- SDP data message event received
 */
static int32_t
sdp_event_send_data(sdp_conn_t *conn, sdp_buff_t *buff)
{
	int32_t result = 0;

	SDP_CHECK_NULL(conn, -EINVAL);

	SDP_CHECK_NULL(buff, -EINVAL);

	/*
	 * This is the first buff of a new message being cleanup
	 */
	conn->sdpc_tx_bytes_queued -= buff->sdp_buff_data_size;

	dprint(SDP_CONT, ("comp1 :id:%d qud:%d sz:%d",
	    buff->u_id, conn->sdpc_tx_bytes_queued, conn->s_wq_size));

	if ((conn->sdp_ulpd != NULL) &&
	    (conn->state == SDP_CONN_ST_ESTABLISHED))
		conn->sdp_ulp_xmitted(conn->sdp_ulpd,
		    sdp_inet_writable(conn));

	return (result);
}   /* sdp_event_send_data */

/* ========================================================================= */

/*
 * sdp_event_send_unsupported -- valid messages we're not sending
 */
/* ARGSUSED */
static int32_t
sdp_event_send_unsupported(sdp_conn_t *conn, sdp_buff_t *buff)
{
	SDP_CHECK_NULL(conn, -EINVAL);
	SDP_CHECK_NULL(buff, -EINVAL);

	/*
	 * since the gateway only initates RDMA's but is never a target, and
	 * for a few other reasons, there are certain valid SDP messages
	 * which we never send.
	 */

	return (0);
}   /* sdp_event_send_unsupported */

/*
 * event dispatch table. for performance a dispatch table is used to avoid a
 * giant case statment for every single SDP event. this is a bit more
 * confusing, relies on the layout of the message ids, and is less flexable.
 * however, it is faster.
 *
 * sparse table, the full table would be 16x16, where the low 4 bits, of the
 * MID byte, are one dimension, and the high 4 bits are the other dimension.
 * since all rows, except for the first and last, are empty, only those are
 * represented in the table.
 */
static sdp_event_cb_func_t send_event_funcs[SDP_MSG_EVENT_TABLE_SIZE] = {
	NULL,	/* SDP_MID_HELLO	0x00 */
	NULL,	/* SDP_MID_HELLO_ACK	0x01 */
	sdp_event_send_disconnect,	/* SDP_MID_DISCONNECT	0x02 */
	sdp_event_send_abort_conn,	/* SDP_MID_ABORT_CONN	0x03 */
	sdp_event_send_send_sm,	/* SDP_MID_SEND_SM		0x04 */
	sdp_event_send_rdma_write_comp, /* SDP_MID_RDMA_WR_COMP 0x05 */
	sdp_event_send_rdma_read_comp, /* SDP_MID_RDMA_RD_COMP	0x06 */
	sdp_event_send_mode_change,	/* SDP_MID_MODE_CHANGE	0x07 */
	sdp_event_send_src_cancel, /* SDP_MID_SRC_CANCEL	0x08 */
	sdp_event_send_sink_cancel,	/* SDP_MID_SNK_CANCEL	0x09 */
	sdp_event_send_sink_cancel_ack, /* SDP_MID_SNK_CANCEL_ACK 0x0a */
	sdp_event_send_unsupported,	/* SDP_MID_CH_RECV_BUF	0x0b */
	sdp_event_send_change_rcv_buf_ack,
	/* SDP_MID_CH_RECV_BUF_ACK  0x0c */
	sdp_event_send_suspend, /* SDP_MID_SUSPEND 0x0d */
	sdp_event_send_suspend_ack,	/* SDP_MID_SUSPEND_ACK 0x0e */
	NULL,	/* reserved 0x0f */
	NULL,	/* reserved 0x_f0 */
	NULL,	/* reserved 0x_f1 */
	NULL,	/* reserved 0x_f2 */
	NULL,	/* reserved 0x_f3 */
	NULL,	/* reserved 0x_f4 */
	NULL,	/* reserved 0x_f5 */
	NULL,	/* reserved 0x_f6 */
	NULL,	/* reserved 0x_f7 */
	NULL,	/* reserved 0x_f8 */
	NULL,	/* reserved 0x_f9 */
	NULL,	/* reserved 0x_fa */
	NULL,	/* reserved 0x_fb */
	NULL,	/* reserved 0x_fc */
	sdp_event_send_snk_avail,	/* SDP_MID_SNK_AVAIL 0x_fd */
	sdp_event_send_src_avail,	/* SDP_MID_SRC_AVAIL 0x_fe */
	sdp_event_send_data	/* SDP_MID_DATA 0x_ff */
};  /* send_event_funcs */

/* ========================================================================= */

/*
 * sdp_event_send -- send event handler.
 */
int32_t
sdp_event_send(sdp_conn_t *conn, ibt_wc_t *wc)
{
	sdp_event_cb_func_t dispatch_func;
	int32_t free_count = 0;
	int32_t offset;
	int32_t result;
	sdp_buff_t *buff;
	ibt_wrid_t wrid;

	SDP_CHECK_NULL(conn, -EINVAL);

	/*
	 * error handling
	 */
	if (wc->wc_status != IBT_WC_SUCCESS) {
		switch (wc->wc_status) {
			case IBT_WC_WR_FLUSHED_ERR:
				/*
				 * clear posted buffers from error'd queue
				 */
				sdp_buff_pool_clear(&conn->send_post);
				result = 0;
				break;
			default:
				dprint(SDP_CONT,
				    ("sdp_event_send: wc->wc_status %x\n",
				    wc->wc_status));
				result = -EIO;
		}	/* switch */
		goto done;
	}

	/*
	 * get buffer.
	 */
	while ((buff = buff_pool_get_head(&conn->send_post)) != NULL) {

		/*
		 * sanity checks
		 */
		if (buff->bsdh_hdr == NULL) {
			dprint(SDP_CONT, ("sdp_event_send: bsdh_hdr is NULL"));
			result = -ENODATA;
			goto drop;
		}
		if (SDP_WRID_LT(wc->wc_id, buff->sdp_buff_ib_wrid)) {
			dprint(SDP_CONT, ("sdp_event_send: "
			    "buff id is less that wc id"));

			/*
			 * error
			 */
			result = -EINVAL;
			goto drop;
		}

		/*
		 * execute the send dispatch function, for specific actions.
		 * data fast path we collapse the next level dispatch
		 * function. for all other buffers we go the slow path.
		 */
		if (buff->bsdh_hdr->mid == SDP_MSG_MID_DATA) {
			conn->sdpc_tx_bytes_queued -= buff->sdp_buff_data_size;
			if ((conn->sdp_ulpd != NULL) &&
			    (conn->state == SDP_CONN_ST_ESTABLISHED)) {
				conn->sdp_ulp_xmitted(conn->sdp_ulpd,
				    sdp_inet_writable(conn));
			}

		} else {
			offset = buff->bsdh_hdr->mid & 0x1f;
			dprint(SDP_CONT, ("comp1 :id:%d qud:%d sz:%d",
			    buff->u_id, conn->sdpc_tx_bytes_queued,
			    conn->s_wq_size));

			if (!(offset < SDP_MSG_EVENT_TABLE_SIZE) ||
			    send_event_funcs[offset] == NULL) {

				dprint(SDP_CONT, ("sdp_event_send: "
				    "offset < SDP_MSG_EVENT_TABLE_SIZE\n"));
				result = -EINVAL;
				goto drop;
			} else {
				SDP_CONN_STAT_SEND_MID_INC(conn, offset);
				dispatch_func = send_event_funcs[offset];
				result = dispatch_func(conn, buff);
				if (result < 0) {
					dprint(SDP_CONT, ("sdp_event_send: "
					    "dispatch_func %p result%x\n",
					    (void *)dispatch_func, result));

					goto drop;
				}	/* if */
			}	/* else */

		}	/* else */

		if (SDP_BUFF_F_GET_UNSIG(buff)) {
			conn->send_usig--;
		}

		wrid = buff->sdp_buff_ib_wrid;
		sdp_buff_free(buff);

		/*
		 * send queue size reduced by one.
		 */
		conn->s_wq_size--;

		free_count++;
		if (wc->wc_id == wrid) {
			break;
		}	/* if */
	}	/* while */

	if (!(free_count > 0) || conn->send_usig < 0) {
		dprint(SDP_CONT,
		    ("sdp_event_send: free_count %d send_usig %d\n",
		    free_count, conn->send_usig));
		result = -EINVAL;
		goto done;
	}

	/*
	 * * flush queued send data into the post queue if there is room.
	 */
	sdp_send_flush(conn);
	return (0);
drop:
	sdp_buff_free(buff);
done:
	return (result);
}   /* sdp_event_send */

/*
 * CQ handler.
 */
void
sdp_scq_handler(ibt_cq_hdl_t cq_hdl, void *arg)
{
	sdp_conn_t *conn = arg;
	ibt_status_t result = 0;
	ibt_wc_t *wc;
	ibt_wc_t *wcs;
	uint_t	numwc;
	uint_t	i;

	if (conn == NULL) {
		return;
	}
	SDP_CONN_TABLE_VERIFY(conn);

	/*
	 * enable the callback
	 */
	(void) ibt_enable_cq_notify(cq_hdl, IBT_NEXT_COMPLETION);
	/*
	 * Poll the cq and process work requests until the cq is empty.
	 * Drop back to the outer loop and re-enable the cq handler and
	 * poll again, pulling work requests off the queue.  Do this in a
	 * loop until the cq handler has been re-enabled at least once and
	 * the queue is empty.
	 */
	wcs = conn->ib_swc;
	while (ibt_poll_cq(cq_hdl, wcs, SDP_MAX_CQ_WC, &numwc) == IBT_SUCCESS) {
		/*
		 * While there is still something in the completion queue
		 */
		for (i = 0, wc = wcs; i < numwc; i++, wc++) {
			DTRACE_PROBE2(sdp_scq_handler, sdp_conn_t, conn,
			    ibt_wc_t, wc);
			if (wc->wc_status != IBT_WC_SUCCESS) {
				result = 1;
				dprint(SDP_CONT, ("sdp_scq_handler"
				    " wc->wc_status %x\n", wc->wc_status));
				goto done;
			}

			if (conn->state & SDP_ST_MASK_CLOSED) {
				sdp_print_warn(conn, "sdp_scq_handler: "
				    "Conn state invalid <%04x>",
				    conn->state);
				/*
				 * Ignore events in error state: connection
				 * s being terminated; connection cleanup
				 * will take care of freeing posted buffers.
				 */
				result = 0;
				goto done;
			}	/* if */

			/*
			 * If we already closed the channel, just clear out
			 * the cq.  This can happen, for example, when we've
			 * received a disconnect and turn around and send
			 * a disconnect back.  Receives can still occur after
			 * the channel has been closed, but before the refcnt
			 * has dropped to 0 (cm hasn't finished its protocol).
			 */
			switch (wc->wc_type) {
			case IBT_WRC_SEND:
				result = sdp_event_send(conn, wc);
				break;
			case IBT_WRC_RDMAW:
				result = sdp_event_write(conn, wc);
				break;
			case IBT_WRC_RDMAR:
				result = sdp_event_read(conn, wc);
				break;
			default:
				goto done;
			} /* switch */

			if (result != 0)
				goto done;

		}	/* inner while */
	}
done:

	/*
	 * release socket before error processing.
	 */
	if (result != 0) {
		dprint(SDP_DBG, ("sdp_scq_handler: result!=0: %d", result));
		sdp_abort(conn);
	}
	SDP_CONN_UNLOCK(conn);
}
