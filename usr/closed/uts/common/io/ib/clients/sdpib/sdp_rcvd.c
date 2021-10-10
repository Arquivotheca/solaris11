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
 * Copyright (c) 2005, 2011, Oracle and/or its affiliates. All rights reserved.
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
 * specific MID handler functions. (RECV)
 */

/*
 * sdp_event_recv_disconnect --  process a discon event from the peer.
 */
/* ARGSUSED */
static int32_t
sdp_event_recv_disconnect(sdp_conn_t *conn, sdp_buff_t *buff)
{
	int32_t result = 0;

	SDP_CHECK_NULL(conn, -EINVAL);
	SDP_CHECK_NULL(buff, -EINVAL);

	sdp_print_ctrl(conn, "sdp_event_recv_discon: state:%x",
	    conn->state);

	switch (conn->state) {
		case SDP_CONN_ST_ESTABLISHED:
			SDP_CONN_ST_SET(conn, SDP_CONN_ST_DIS_RECV_1);
			cv_signal(&conn->ss_rxdata_cv);
			if ((conn->sdp_ulpd != NULL) &&
			    (buff_pool_size(&conn->recv_pool) == 0)) {
				conn->sdp_ulp_ordrel(conn->sdp_ulpd);
			}
			break;
		case SDP_CONN_ST_DIS_SEND_1:
			SDP_CONN_ST_SET(conn, SDP_CONN_ST_DIS_RECV_R);
			break;
		case SDP_CONN_ST_DIS_SENT_1:
			SDP_CONN_ST_SET(conn, SDP_CONN_ST_TIME_WAIT_1);
			cv_signal(&conn->ss_rxdata_cv);
			if ((conn->sdp_ulpd != NULL) &&
			    (buff_pool_size(&conn->recv_pool) == 0)) {
				conn->sdp_ulp_disconnected(conn->sdp_ulpd, 0);
			}

			/*
			 * Close the channel here
			 */
			result = ibt_close_rc_channel(conn->channel_hdl,
			    IBT_NONBLOCKING, NULL, 0, NULL, NULL, 0);
			if (result != 0) {
				result = -EPROTO;
			}
			break;
		default:
			sdp_print_warn(conn, "sdp_recv_discon: unexpected "
			    "state <%04x>", conn->state);
			result = -EPROTO;
			break;
	}

	conn->shutdown |= SDP_SHUTDOWN_RECV;
	sdp_buff_free(buff);
	return (result);

}

/* ========================================================================= */

/*
 * sdp_event_recv_abort_conn -- Process abort from peer side.
 */
/* ARGSUSED */
static int32_t
sdp_event_recv_abort_conn(sdp_conn_t *conn, sdp_buff_t *buff)
{
	int32_t result = 0;

	SDP_CHECK_NULL(conn, -EINVAL);
	SDP_CHECK_NULL(buff, -EINVAL);

	SDP_STAT_INC(conn->sdp_sdps, InAborts);

	sdp_print_ctrl(conn, "sdp_event_recv_abort: state:%x", conn->state);

	/*
	 * connection should be in some post discon recveived state. Notify
	 * gateway interface about abort
	 */
	switch (conn->state) {
		case SDP_CONN_ST_DIS_RECV_1:
		case SDP_CONN_ST_DIS_RECV_R:
		case SDP_CONN_ST_DIS_SEND_2:
			sdp_conn_abort(conn);
			break;
		case SDP_CONN_ST_ERROR:
			sdp_cm_disconnect(conn);
			break;
		default:
			result = -EPROTO;
	}
	sdp_buff_free(buff);
	return (result);
}


/*
 * sdp_event_recv_send_sm --
 */
/* ARGSUSED */
static int32_t
sdp_event_recv_send_sm(sdp_conn_t *conn, sdp_buff_t *buff)
{
	SDP_CHECK_NULL(conn, -EINVAL);
	SDP_CHECK_NULL(buff, -EINVAL);

	SDP_STAT_INC(conn->sdp_sdps, InSendSm);

	sdp_buff_free(buff);
	return (0);
}   /* sdp_event_recv_send_sm */

/* ========================================================================= */

/*
 * sdp_event_recv_rdma_write_comp --
 */
/* ARGSUSED */
static int32_t
sdp_event_recv_rdma_write_comp(sdp_conn_t *conn, sdp_buff_t *buff)
{
	int32_t result = 0;

	SDP_STAT_INC(conn->sdp_sdps, InRdmaWrCompl);

	sdp_buff_free(buff);
	return (result);
}   /* sdp_event_recv_rdma_write_comp */

/* ========================================================================= */

/*
 * sdp_event_recv_rdma_read_comp --
 */
/* ARGSUSED */
static int32_t
sdp_event_recv_rdma_read_comp(sdp_conn_t *conn, sdp_buff_t *buff)
{
	int32_t result = 0;

	SDP_STAT_INC(conn->sdp_sdps, InRdmaRdCompl);

	sdp_buff_free(buff);
	return (result);
}   /* sdp_event_recv_rdma_read_comp */

/* ========================================================================= */

/*
 * sdp_event_recv_mode_change --
 */
static int32_t
sdp_event_recv_mode_change(sdp_conn_t *conn, sdp_buff_t *buff)
{
	sdp_msg_mch_t *mch;
	int32_t result = 0;

	SDP_CHECK_NULL(conn, -EINVAL);
	SDP_CHECK_NULL(buff, -EINVAL);

	SDP_STAT_INC(conn->sdp_sdps, InModeChange);

	mch = (sdp_msg_mch_t *)buff->sdp_buff_data;
	buff->sdp_buff_data =
	    (char *)buff->sdp_buff_data + sizeof (sdp_msg_mch_t);

	result = sdp_msg_wire_to_host_mch(mch);
	SDP_EXPECT(!(0 > result));

	/*
	 * check if the mode change is to the same mode.
	 */
	if (((SDP_MSG_MCH_GET_MODE(mch) & 0x7) ==
	    ((0 < (SDP_MSG_MCH_GET_MODE(mch) & 0x8)) ?
	    conn->send_mode : conn->recv_mode))) {
		result = -EPROTO;
		goto done;
	}

	/*
	 * process mode change requests based on
	 * which state we're in
	 */
	switch (SDP_MSG_MCH_GET_MODE(mch)) {
		case SDP_MSG_MCH_BUFF_RECV:	/* source to sink */

			if (SDP_MODE_COMB != conn->recv_mode) {
				result = -EPROTO;
				goto done;
			}
			if (conn->src_recv > 0) {
				result = -EPROTO;
				goto done;
			}
			break;
		case SDP_MSG_MCH_COMB_SEND:	/* sink to source */

			if (SDP_MODE_BUFF != conn->send_mode) {
				result = -EPROTO;
				goto done;
			}
			break;
		case SDP_MSG_MCH_PIPE_RECV:	/* source to sink */

			if (SDP_MODE_COMB != conn->recv_mode) {
				result = -EPROTO;
				goto done;
			}
			break;
		case SDP_MSG_MCH_COMB_RECV:	/* source to sink */

			if (SDP_MODE_PIPE != conn->recv_mode) {
				result = -EPROTO;
				goto done;
			}

			/*
			 * drop all src_avail message: they will be
			 * reissued, with combined mode constraints.
			 * no snk_avails outstanding on this half of
			 * the connection. how do I know which src_avail
			 * RDMA's completed?
			 */
			break;
		default:

			result = -EPROTO;
			goto done;
	}

	/*
	 * assign new mode
	 */
	if ((SDP_MSG_MCH_GET_MODE(mch) & 0x8) > 0) {
		conn->send_mode = SDP_MSG_MCH_GET_MODE(mch) & 0x7;
	} else {
		conn->recv_mode = SDP_MSG_MCH_GET_MODE(mch) & 0x7;
	}	/* else */

done:
	sdp_buff_free(buff);
	return (result);
}

/* ========================================================================= */

/*
 * sdp_event_recv_src_cancel --
 */
/* ARGSUSED */
static int32_t
sdp_event_recv_src_cancel(sdp_conn_t *conn, sdp_buff_t *buff)
{
	sdp_advt_t *advt;
	int32_t result = 0;

	SDP_CHECK_NULL(conn, -EINVAL);
	SDP_CHECK_NULL(buff, -EINVAL);

	SDP_STAT_INC(conn->sdp_sdps, InSrcCancel);

	/*
	 * if there are no outstanding advertisements,
	 * then there is nothing to do.
	 */
	if (!(conn->src_recv > 0)) {
		result = 0;
		goto done;
	}

	/*
	 * * drop the pending advertisement queue.
	 */
	while ((advt = sdp_conn_advt_table_get(&conn->src_pend)) != NULL) {
		conn->flags |= SDP_CONN_F_SRC_CANCEL_C;

		conn->src_recv--;

		result = sdp_conn_advt_destroy(advt);
		SDP_EXPECT(!(0 > result));
	}	/* while */

	/*
	 * if there are active reads, mark the connection
	 * as being in source
	 * cancel. otherwise
	 */
	if (conn_advt_table_size(&conn->src_actv) > 0) {

		/*
		 * set flag. adjust sequence number ack.
		 * (spec dosn't want
		 * the seq ack in subsequent messages
		 * updated until the
		 * cancel has been processed. all would
		 * be simpler with an
		 * explicit cancel ack, but...)
		 */
		conn->flags |= SDP_CONN_F_SRC_CANCEL_R;
		conn->advt_seq--;
	} else {

		/*
		 * if a source was dropped, generate an ack.
		 */
		if ((SDP_CONN_F_SRC_CANCEL_C & conn->flags) > 0) {

			result = sdp_send_ctrl_send_sm(conn);
			if (result < 0) {
				goto done;
			}	/* if */
			conn->flags &= ~SDP_CONN_F_SRC_CANCEL_C;
		}	/* if */
	}

done:
	sdp_buff_free(buff);
	return (result);
}

/* ========================================================================= */

/*
 * sdp_event_recv_sink_cancel --
 */
/* ARGSUSED */
static int32_t
sdp_event_recv_sink_cancel(sdp_conn_t *conn, sdp_buff_t *buff)
{
	sdp_advt_t *advt;
	int32_t counter;
	int32_t result = 0;

	SDP_CHECK_NULL(conn, -EINVAL);
	SDP_CHECK_NULL(buff, -EINVAL);

	SDP_STAT_INC(conn->sdp_sdps, InSinkCancel);

	/*
	 * if there are no outstanding advertisements,
	 * they we've completed
	 * since the message was sent, and there is nothing to do.
	 */
	if (!(conn->snk_recv > 0)) {
		result = 0;
		goto done;
	}

	/*
	 * get the oldest advertisement, and complete it
	 * if it's partially consumed. throw away all
	 * unprocessed advertisements, and ack the
	 * cancel. since all the active writes and sends
	 * are fenced, it's possible to handle the entire
	 * cancel here.
	 */
	advt = sdp_conn_advt_table_look(&conn->snk_pend);
	if (advt != NULL && advt->post > 0) {

		/*
		 * generate completion
		 */
		result = sdp_send_ctrl_rdma_wr_comp(conn, advt->post);
		if (result < 0) {
			goto done;
		}

		/*
		 * reduce cancel counter
		 */
		counter = -1;
	} else {

		/*
		 * cancel count.
		 */
		counter = 0;
	}	/* else */

	/*
	 * drain the advertisements which have yet to be processed.
	 */
	while ((advt = sdp_conn_advt_table_get(&conn->snk_pend)) != NULL) {
		counter++;
		conn->snk_recv--;

		result = sdp_conn_advt_destroy(advt);
		SDP_EXPECT(!(0 > result));
	}

	/*
	 * A cancel ack is sent only if we cancelled an advertisement without
	 * sending a completion
	 */
	if (counter > 0) {
		result = sdp_send_ctrl_snk_cancel_ack(conn);
	}
done:
	sdp_buff_free(buff);
	return (result);
}

/* ========================================================================= */

/*
 * sdp_event_recv_sink_cancel_ack -- sink cancel confirmantion
 */
/* ARGSUSED */
static int32_t
sdp_event_recv_sink_cancel_ack(sdp_conn_t *conn, sdp_buff_t *buff)
{
	int32_t result = 0;

	SDP_CHECK_NULL(conn, -EINVAL);
	SDP_CHECK_NULL(buff, -EINVAL);

	SDP_STAT_INC(conn->sdp_sdps, InSinkCancelAck);

	sdp_buff_free(buff);
	return (result);
}   /* sdp_event_recv_sink_cancel_ack */

/* ========================================================================= */

/* ..sdp_event_recv_change_rcv_buf --  buffer size change request */
static int32_t
sdp_event_recv_change_rcv_buf(sdp_conn_t *conn, sdp_buff_t *buff)
{
	int32_t result = 0;

	SDP_CHECK_NULL(conn, -EINVAL);
	SDP_CHECK_NULL(buff, -EINVAL);

	SDP_STAT_INC(conn->sdp_sdps, InResize);

	result = sdp_msg_wire_to_host_crbh(
	    (sdp_msg_crbh_t *)buff->sdp_buff_data);

	SDP_EXPECT(!(0 > result));

	buff->sdp_buff_data = (char *)buff->sdp_buff_data +
	    sizeof (sdp_msg_crbh_t);
	/*
	 * request to change our recv buffer size, we're pretty much locked
	 * into the size we're using, once the connection is set up, so we
	 * reject the request.
	 */
	result =
	    sdp_send_ctrl_resize_buff_ack(conn, conn->sdpc_local_buff_size);
	sdp_buff_free(buff);
	return (result);
}

/* ========================================================================= */

/*
 * sdp_event_recv_suspend --
 */
/* ARGSUSED */
static int32_t
sdp_event_recv_suspend(sdp_conn_t *conn, sdp_buff_t *buff)
{
	/* LINTED */
	sdp_msg_sch_t *sch = (sdp_msg_sch_t *)buff->sdp_buff_data;

	SDP_CHECK_NULL(conn, -EINVAL);
	SDP_CHECK_NULL(buff, -EINVAL);

	buff->sdp_buff_data =
	    (char *)buff->sdp_buff_data + sizeof (sdp_msg_sch_t);

	SDP_STAT_INC(conn->sdp_sdps, InSuspend);

	sdp_buff_free(buff);

	return (0);
}

/* ========================================================================= */

/*
 * sdp_event_recv_suspend_ack --
 */
/* ARGSUSED */
static int32_t
sdp_event_recv_suspend_ack(sdp_conn_t *conn, sdp_buff_t *buff)
{
	SDP_CHECK_NULL(conn, -EINVAL);
	SDP_CHECK_NULL(buff, -EINVAL);
	SDP_STAT_INC(conn->sdp_sdps, InSuspendAck);

	sdp_buff_free(buff);

	return (0);
}

/* ========================================================================= */

/*
 * sdp_event_recv_sink_avail --
 */
/* ARGSUSED */
static int32_t
sdp_event_recv_sink_avail(sdp_conn_t *conn, sdp_buff_t *buff)
{
	int32_t result = 0;
	sdp_msg_snkah_t *snkah;
	sdp_advt_t *advt;

	SDP_CHECK_NULL(conn, -EINVAL);
	SDP_CHECK_NULL(buff, -EINVAL);

	SDP_STAT_INC(conn->sdp_sdps, InSinkAvail);

	snkah = (sdp_msg_snkah_t *)buff->sdp_buff_data;
	buff->sdp_buff_data =
	    (char *)buff->sdp_buff_data + sizeof (sdp_msg_snkah_t);

	result = sdp_msg_wire_to_host_snkah(snkah);
	SDP_EXPECT(!(0 > result));

	/*
	 * check our send mode, and make sure parameters are within reason.
	 */
	if (conn->send_mode != SDP_MODE_PIPE) {
		result = -EPROTO;
		goto error;
	}
	if ((conn->src_recv + conn->snk_recv) == SDP_MSG_MAX_ADVS) {
		result = -EPROTO;
		goto error;
	}

	/*
	 * Save the advertisement if it's not stale.
	 */
	if (conn->nond_send == snkah->non_disc) {

		/*
		 * check advertisement size.
		 */
		if (snkah->size < conn->sdpc_send_buff_size) {
			result = -EPROTO;
			goto error;
		}

		/*
		 * create and queue new advertisement
		 */
		advt = sdp_conn_advt_create();
		if (advt == NULL) {
			result = -ENOMEM;
			goto error;
		}
		advt->post = 0;
		advt->size = snkah->size;
		advt->addr = snkah->addr;
		advt->rkey = snkah->r_key;

		conn->snk_recv++;
		conn->sink_actv++;

		conn->s_cur_adv = 1;
		conn->s_par_adv = 0;

		result = sdp_conn_advt_table_put(&conn->snk_pend, advt);
		if (result < 0) {
			(void) sdp_conn_advt_destroy(advt);
			goto error;
		}
	} else {
		conn->nond_send--;

	}

	conn->s_wq_cur = SDP_DEV_SEND_POST_SLOW;
	conn->s_wq_par = 0;

	/*
	 * consume any data in the advertisement for the other direction.
	 */
	if ((intptr_t)PTRDIFF(buff->sdp_buff_tail, buff->sdp_buff_data) > 0) {
		sdp_sock_buff_recv(conn, buff);
		goto done;
	}

error:
	sdp_buff_free(buff);
done:
	/*
	 * sdp_post_recv will take care of consuming this advertisement,
	 * based on result.
	 */
	return (result);
}

/* ========================================================================= */

/*
 * sdp_event_recv_src_avail --
 */
static int32_t
sdp_event_recv_src_avail(sdp_conn_t *conn, sdp_buff_t *buff)
{
	int32_t result = 0;
	sdp_msg_srcah_t *srcah;
	sdp_advt_t *advt;
	intptr_t size;

	SDP_CHECK_NULL(conn, -EINVAL);
	SDP_CHECK_NULL(buff, -EINVAL);

	SDP_STAT_INC(conn->sdp_sdps, InSrcAvail);

	srcah = (sdp_msg_srcah_t *)buff->sdp_buff_data;
	buff->sdp_buff_data =
	    (char *)buff->sdp_buff_data + sizeof (sdp_msg_srcah_t);

	result = sdp_msg_wire_to_host_srcah(srcah);
	SDP_EXPECT(!(0 > result));

	size = (intptr_t)PTRDIFF(buff->sdp_buff_tail, buff->sdp_buff_data);

	if ((SDP_CONN_F_SRC_CANCEL_R & conn->flags) > 0) {
		result = -EFAULT;
		goto done;
	}

	/*
	 * To emulate RFC 1122 (page 88) a connection should be reset/aborted
	 * if data is received and the receive half of the connection has been
	 * closed. This notifies the peer that the data was not received.
	 */
	if ((conn->shutdown & SDP_SHUTDOWN_RECV) > 0) {
		sdp_print_dbg(conn, "src avail, receive path closed <%02x>",
		    conn->shutdown);
		/* abort connection (send reset) */
		sdp_abort(conn);

		/* drop packet */
		result = 0;
		goto done;
	}

	/*
	 * save the advertisement
	 */
	advt = sdp_conn_advt_create();
	if (advt == NULL) {
		result = -ENOMEM;
		goto done;
	}

	/*
	 * consume the advertisement, if it's allowed, first check the recv
	 * path mode to determine if all is cool for the advertisement.
	 */
	switch (conn->recv_mode) {

		case SDP_MODE_BUFF:

			result = -EPROTO;
			goto advt_error;

		case SDP_MODE_COMB:

			if (conn->src_recv > 0 ||
			    !(size > 0) || !(srcah->size > size)) {

				result = -EPROTO;
				goto advt_error;
			}	/* if */
			advt->rkey = srcah->r_key;
			advt->post = 0 - ((size < SDP_SRC_AVAIL_THRESHOLD) ?
			    size : 0);
			advt->size = srcah->size -
			    ((SDP_SRC_AVAIL_THRESHOLD > size) ?  0 : size);
			advt->addr = srcah->addr +
			    ((size < SDP_SRC_AVAIL_THRESHOLD) ? 0 : size);

			break;
		case SDP_MODE_PIPE:

			if ((conn->src_recv + conn->snk_recv) ==
			    SDP_MSG_MAX_ADVS || size != 0) {
				result = -EPROTO;
				goto advt_error;
			}	/* if */
			advt->post = 0;
			advt->size = srcah->size;
			advt->addr = srcah->addr;
			advt->rkey = srcah->r_key;

			break;
		default:

			result = -EPROTO;
			goto advt_error;
	}	/* switch */

	/*
	 * save advertisement
	 */
	conn->src_recv++;

	result = sdp_conn_advt_table_put(&conn->src_pend, advt);
	if (result) {
		goto advt_error;
	}

	/*
	 * process the ULP data in the message
	 */
	if (size > 0) {

		conn->nond_recv++;
		/*
		 * update non-discard for sink advertisement management
		 */
		if (!(size < SDP_SRC_AVAIL_THRESHOLD)) {
			sdp_sock_buff_recv(conn, buff);
			return (0);
		}
		result = 0;
	}

	sdp_buff_free(buff);
	/*
	 * sdp_post_recv will take care of consuming this advertisement.
	 */
	return (result);

advt_error:
	(void) sdp_conn_advt_destroy(advt);
done:
	sdp_buff_free(buff);
	return (result);
}

/* ========================================================================= */

/*
 * sdp_event_recv_data -- SDP data message event received
 */
static int32_t
sdp_event_recv_data(sdp_conn_t *conn, sdp_buff_t *buff)
{
	int32_t result = 0;

	SDP_CHECK_NULL(conn, -EINVAL);
	SDP_CHECK_NULL(buff, -EINVAL);


	/*
	 * if we are processing a src_avail, there should be no
	 * buffered data
	 */
	if (conn->src_recv > 0) {
		result = -EPROTO;
		sdp_buff_free(buff);
		goto done;
	}

	/*
	 * check for out-of-band data, and mark the buffer if there is a
	 * pending urgent message. If the OOB data is in this buffer,
	 * pull it out.
	 */
	if (SDP_MSG_HDR_GET_OOB_PEND(buff->bsdh_hdr)) {
		buff->flags |= SDP_BUFF_F_OOB_PEND;
	}

	if (SDP_MSG_HDR_GET_OOB_PRES(buff->bsdh_hdr)) {
		buff->flags |= SDP_BUFF_F_OOB_PRES;
	}

	/*
	 * update non-discard for sink advertisment management
	 */
	conn->nond_recv++;

	/*
	 * send the data to sockfs.
	 */
	sdp_sock_buff_recv(conn, buff);
done:
	return (result);
}

/*
 * =================================================================== ======
 */

/*
 * sdp_event_recv_unsupported -- valid messages we're not expecting
 */
/* ARGSUSED */
static int32_t
sdp_event_recv_unsupported(sdp_conn_t *conn, sdp_buff_t *buff)
{
	SDP_CHECK_NULL(conn, -EINVAL);
	SDP_CHECK_NULL(buff, -EINVAL);

	/*
	 * since the gateway only initates RDMA's but is never a target, and
	 * for a few other reasons, there are certain valid SDP messages
	 * which we never expect to see.
	 */
	SDP_STAT_INC(conn->sdp_sdps, InResizeAck);
	sdp_buff_free(buff);

	return (0);
}   /* sdp_event_recv_unsupported */

/*
 * event dispatch table. for performance a dispatch table is used to avoid a
 * giant case statment for every single SDP event. this is a bit more
 * confusing, relies on the layout of the message ids, and is less flexable.
 * however, it is faster.
 *
 * sparse table, the full table would be 16x16, where the low 4 bits of the MID
 * byte, are one dimension, and the high 4 bits are the other dimension.
 * since all rows, except for the first and last, are empty, only those are
 * represented in the table.
 */
static sdp_event_cb_func_t recv_event_funcs[SDP_MSG_EVENT_TABLE_SIZE] = {
	NULL,	/* SDP_MID_HELLO 	0x00 */
	NULL,	/* SDP_MID_HELLO_ACK	0x01 */
	sdp_event_recv_disconnect,	/* SDP_MID_DISCONNECT 0x02 */
	sdp_event_recv_abort_conn,	/* SDP_MID_ABORT_CONN 0x03 */
	sdp_event_recv_send_sm,	/* SDP_MID_SEND_SM 0x04 */
	sdp_event_recv_rdma_write_comp, /* SDP_MID_RDMA_WR_COMP 0x05 */
	sdp_event_recv_rdma_read_comp, /* SDP_MID_RDMA_RD_COMP 0x06 */
	sdp_event_recv_mode_change,	/* SDP_MID_MODE_CHANGE 0x07 */
	sdp_event_recv_src_cancel,	/* SDP_MID_SRC_CANCEL 0x08 */
	sdp_event_recv_sink_cancel,	/* SDP_MID_SNK_CANCEL 0x09 */
	sdp_event_recv_sink_cancel_ack, /* SDP_MID_SNK_CANCEL_ACK 0x0a */
	sdp_event_recv_change_rcv_buf, /* SDP_MID_CH_RECV_BUF 0x0b */
	sdp_event_recv_unsupported,	/* SDP_MID_CH_RECV_BUF_ACK 0x0c */
	sdp_event_recv_suspend,	/* SDP_MID_SUSPEND 0x0d */
	sdp_event_recv_suspend_ack,	/* SDP_MID_SUSPEND_ACK 0x0e */
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
	sdp_event_recv_sink_avail,	/* SDP_MID_SNK_AVAIL 0x_fd */
	sdp_event_recv_src_avail,	/* SDP_MID_SRC_AVAIL 0x_fe */
	sdp_event_recv_data	/* SDP_MID_DATA 0x_ff */
};  /* recv_event_funcs */

/*
 * Receive CQ handler.
 * Chain data buffers and send it up
 */
/* ARGSUSED */
void
sdp_rcq_handler(ibt_cq_hdl_t cq_hdl, void *arg)
{
	sdp_conn_t *conn = arg;
	ibt_status_t ibt_status;
	int32_t result;
	ibt_wc_t *wc;
	ibt_wc_t *wcs;
	int	numwc;
	sdp_buff_t	*buff;
	uint32_t offset;
	sdp_event_cb_func_t dispatch_func;

	if (conn == NULL) {
		return;
	}

	SDP_CONN_TABLE_VERIFY(conn);

	buff = NULL;
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
	wcs = conn->ib_rwc;
	for (;;) {
		numwc = 0;
		ibt_status = ibt_poll_cq(cq_hdl, wcs, SDP_MAX_CQ_WC,
		    (uint_t *)&numwc);
		/*
		 * Check if the status is IBT_SUCCESS or IBT_CQ_EMPTY
		 * either which can return from ibt_poll_cq(). In other
		 * cases, log the status for the further investigation.
		 */
		if (ibt_status != IBT_SUCCESS) {
			if (ibt_status != IBT_CQ_EMPTY) {
				cmn_err(CE_NOTE, "!sdp_rcq_handler got "
				    "an error status (0x%x) from ibt_poll_cq.",
				    ibt_status);
			}
			break;
		}

		wc = wcs;
		for (; numwc > 0; numwc--, wc++) {
			DTRACE_PROBE2(sdp_rcq_handler, sdp_conn_t *, conn,
			    ibt_wc_t *, wc);
			if (wc->wc_status != IBT_WC_SUCCESS) {
				if (wc->wc_status == IBT_WC_WR_FLUSHED_ERR) {
					/*
					 * clear posted buffers from
					 * error'd queue
					 */
					sdp_buff_pool_clear(&conn->recv_post);
					sdp_print_warn(conn, "sdp_rcq_handler"
					    ": flush error: state <%x>",
					    conn->state);

				} else {
					sdp_print_warn(conn, "sdp_rcq_handler"
					    " : Error <%d>", wc->wc_status);
				}
				goto abort;
			}

			if (wc->wc_type != IBT_WRC_RECV) {
				continue;
			}

			buff = buff_pool_get_head(&conn->recv_post);
			if (buff == NULL) {
				dprint(SDP_WARN, ("sdp_rcq_handler:conn:%p"
				    " buff is NULL", (void *)conn));
				goto abort;
			}
			if (wc->wc_id != buff->sdp_buff_ib_wrid) {
				dprint(SDP_WARN, ("sdp_rcq_handler: wc id"
				    "are not same: id1: %16llx wrid:%16llx",
				    (u_longlong_t)wc->wc_id,
				    (u_longlong_t)buff->sdp_buff_ib_wrid));
				goto drop;
			}

			conn->l_recv_bf--;
			conn->l_advt_bf--;
			buff->bsdh_hdr = (sdp_msg_bsdh_t *)buff->sdp_buff_data;
			(void) sdp_msg_wire_to_host_bsdh(buff->bsdh_hdr);
			if (wc->wc_bytes_xfer != buff->bsdh_hdr->size) {
				sdp_print_warn(conn, "sdp_rcq_handler:"
				    " Bytes <%d> not same",
				    wc->wc_bytes_xfer);
				goto drop;
			}
			buff->sdp_buff_tail = (char *)buff->sdp_buff_data +
			    buff->bsdh_hdr->size;
			buff->sdp_buff_data = (char *)buff->sdp_buff_data +
			    sizeof (sdp_msg_bsdh_t);

			/*
			 * Make sure messages are received in right sequence.
			 * Assert in the debug kernel to catch unexpected
			 * sender behavior.
			 */
			ASSERT(conn->recv_seq + 1 == buff->bsdh_hdr->seq_num);

			if (conn->recv_seq + 1 != buff->bsdh_hdr->seq_num) {
				char local[INET6_ADDRSTRLEN];
				char remote[INET6_ADDRSTRLEN];

				(void) inet_ntop(AF_INET6, &conn->saddr,
				    local, sizeof (local));
				(void) inet_ntop(AF_INET6, &conn->faddr,
				    remote, sizeof (remote));
				cmn_err(CE_NOTE, "!sdp connetion "
				    "[%s.%u, %s.%u]: received unexpected "
				    "sequence number <%d>, expect <%d>",
				    local, ntohs(conn->src_port),
				    remote, ntohs(conn->dst_port),
				    buff->bsdh_hdr->seq_num,
				    conn->recv_seq + 1);
				goto drop;
			}

			/*
			 * do not update the advertised sequence number,
			 * until the src_avail_cancel message
			 * has been processed.
			 */
			conn->recv_seq = buff->bsdh_hdr->seq_num;
			conn->advt_seq =
			    (((SDP_CONN_F_SRC_CANCEL_R & conn->flags) > 0) ?
			    conn->advt_seq : conn->recv_seq);

			/*
			 * buffers advertised minus the difference in
			 * buffer count between
			 * the number we've sent and the remote
			 * host has received.
			 */
			conn->r_recv_bf = (buff->bsdh_hdr->recv_bufs -
			    ABS((int32_t)conn->send_seq -
			    (int32_t)buff->bsdh_hdr->seq_ack));

			if (buff->bsdh_hdr->mid == SDP_MSG_MID_DATA) {
				result = sdp_event_recv_data(conn, buff);
				if (result < 0) {
					sdp_print_warn(conn, "sdp_rcq_handler:"
					    "fail in sdp_recv_data: <%d>",
					    result);
					goto abort;
				}
			} else {
				/* post back the recv buff */
				offset = buff->bsdh_hdr->mid & 0x1f;
				if (!(offset < SDP_MSG_EVENT_TABLE_SIZE) ||
				    recv_event_funcs[offset] == NULL) {
					sdp_print_warn(conn, "sdp_rcq_handler:"
					    " offset <%d> fail", offset);
					goto drop;
				}
				SDP_CONN_STAT_RECV_MID_INC(conn, offset);
				/* Frees the buff */
				dispatch_func = recv_event_funcs[offset];
				/*
				 * These functions are a mess
				 * till we fix them igonre return
				 */
				(void) dispatch_func(conn, buff);
				buff = NULL;
				SDP_STAT_INC(conn->sdp_sdps, InControl);
			}
			SDP_STAT_INC(conn->sdp_sdps, InSegs);
		}
		(void) sdp_recv_post(conn);
	}

	/*
	 * If a receiver is doing poll, this will wakeup the receiver.
	 */
	if (conn->sdp_ulpd) {
		conn->sdp_ulp_recv(conn->sdp_ulpd, NULL, 0);
	}

	if (buff_pool_size(&conn->recv_pool) > 0)
		cv_signal(&conn->ss_rxdata_cv);
	/*
	 * it's possible that a new recv buffer advertisement opened up the
	 * recv window and we can flush buffered send data
	 */
	sdp_send_flush(conn);
done:
	SDP_CONN_UNLOCK(conn);
	return;
drop:
	sdp_buff_free(buff);
	(void) sdp_recv_post(conn);
abort:
	/*
	 * release socket before error processing.
	 */
	sdp_print_dbg(conn, "sdp_rcq_handler: Abort path: result <%d>",
	    result);
	sdp_conn_abort(conn);
	SDP_CONN_UNLOCK(conn);
}
