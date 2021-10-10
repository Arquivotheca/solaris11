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

/*
 * RDMA read processing functions
 */

/* --------------------------------------------------------------------- */
/*
 * sdp_event_read_advt -- RDMA read event handler for source advertisments.
 */
static int
sdp_event_read_advt(sdp_conn_t *conn, ibt_wc_t *comp)
{
	sdp_advt_t *advt;
	int result;

	SDP_CHECK_NULL(conn, -EINVAL);
	SDP_CHECK_NULL(comp, -EINVAL);
	/*
	 * if this was the last RDMA read for an advertisment, post a notice.
	 * Might want to post multiple RDMA read completion messages per
	 * advertisment, to open up a sending window? Have to test to see
	 * what MS does... (Either choice is correct)
	 */
	advt = sdp_conn_advt_table_look(&conn->src_actv);
	if ((advt != NULL) && (advt->wrid == comp->wc_id)) {

		advt = sdp_conn_advt_table_get(&conn->src_actv);
		SDP_EXPECT((advt != NULL));

		conn->src_recv--;

		result = sdp_send_ctrl_rdma_rd_comp(conn, advt->post);
		SDP_EXPECT(!(result < 0));

		result = sdp_conn_advt_destroy(advt);
		SDP_EXPECT(!(result < 0));
		/*
		 * If a SrcAvailCancel was received, and all RDMA reads
		 * have been flushed, perform tail processing
		 */
		if ((conn->flags & SDP_CONN_F_SRC_CANCEL_R) > 0 &&
		    conn->src_recv == 0) {

			conn->flags &= ~SDP_CONN_F_SRC_CANCEL_R;
			conn->advt_seq = conn->recv_seq;
			/*
			 * If any data was canceled, post a SendSm, also
			 */
			if ((conn->flags & SDP_CONN_F_SRC_CANCEL_C) > 0) {

				result = sdp_send_ctrl_send_sm(conn);
				if (result) {
					goto error;
				} /* if */

				conn->flags &= ~SDP_CONN_F_SRC_CANCEL_C;
			} /* if */
		} /* if */
	} else {

		advt = sdp_conn_advt_table_look(&conn->src_pend);
		if (advt != NULL && advt->wrid == comp->wc_id) {
			advt->flag &= ~SDP_ADVT_F_READ;
		} /* if */
	} /* else */

	return (0);
error:
	return (result);
} /* sdp_event_read_advt */

/*
 * RDMA read QP Event Handler
 */

/* --------------------------------------------------------------------- */
/*
 * sdp_event_read -- RDMA read event handler.
 */
int sdp_read_errs = 0;
int
sdp_event_read(sdp_conn_t *conn, ibt_wc_t *comp)
{
	sdp_buff_t *buff;
	int result;
	int type;

	SDP_CHECK_NULL(conn, -EINVAL);
	SDP_CHECK_NULL(comp, -EINVAL);
	/*
	 * error handling
	 */
	if (comp->wc_status != IBT_WC_SUCCESS) {
		switch (comp->wc_status) {
			case IBT_WC_WR_FLUSHED_ERR:
				/*
				 * clear posted buffers from error'd queue
				 */
				sdp_generic_table_clear(&conn->r_src);
				result = 0;
				break;
			default:
				result = -EIO;
		} /* switch */

		goto done;
	} /* if */

	/*
	 * update queue depth
	 */
	conn->s_wq_size--;
	/*
	 * Four basic scenarios:
	 *
	 * 1) BUFF at the head of the active read table is completed by this
	 *    read event completion
	 * 2) IOCB at the head of the active read table is completed by this
	 *    read event completion
	 * 3) IOCB at the head of the active read table is not associated
	 *    with this event, meaning a later event in flight will complete
	 *    it, no IOCB is completed by this event.
	 * 4) No IOCBs are in the active table, the head of the read pending
	 *    table, matches the work request ID of the event and the recv
	 *    low water mark has been satisfied.
	 */
	/*
	 * check type at head of queue
	 */

	type = sdp_generic_table_type_head(&conn->r_src);
	switch (type) {
		case SDP_GENERIC_TYPE_BUFF:

			buff = (sdp_buff_t *)sdp_generic_table_get_head(
			    &conn->r_src);
			SDP_EXPECT((NULL != buff));

			if (comp->wc_id != buff->sdp_buff_ib_wrid) {
				sdp_buff_free(buff);
				result = -EPROTO;
				goto done;
			} /* if */
			/*
			 * post data to the stream interface
			 */
			sdp_sock_buff_recv(conn, buff);
			break;
		default:
			result = -EPROTO;
			goto done;
	}

	/*
	 * The advertisment which generated this READ needs to be checked.
	 */
	result = sdp_event_read_advt(conn, comp);
	if (result < 0) {

		goto done;
	} /* if */

	/*
	 * It's possible that the "send" queue was opened up by the completion
	 * of some RDMAs
	 */

	sdp_send_flush(conn);

	/*
	 * The completion of the RDMA read may allow us to post additional RDMA
	 * reads.
	 */
	result = sdp_recv_post(conn);
	if (result < 0) {
		goto done;
	} /* if */

	return (result);
done:
	sdp_read_errs++;
	return (result);
} /* sdp_event_read */
