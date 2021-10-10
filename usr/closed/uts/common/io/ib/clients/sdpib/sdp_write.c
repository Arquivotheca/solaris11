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

#include <sys/socket.h>
#include <sys/types.h>	/* for size_t */
#include <sys/stream.h>
#include <sys/strsubr.h>
#include <sys/ib/clients/sdpib/sdp_main.h>

/*
 * RDMA read QP Event Handler
 */

/* --------------------------------------------------------------------- */

/* ..sdp_event_write -- RDMA write event handler. */
int
sdp_event_write(sdp_conn_t *conn, ibt_wc_t *comp)
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
				sdp_generic_table_clear(&conn->w_snk);

				result = 0;
				break;
			default:
				result = -EIO;
		} /* switch */
		goto error;
	} /* if */

	/*
	 * Four basic scenarios:
	 *
	 * 1) IOCB at the head of the active sink table is completed by this
	 *    write event completion
	 * 2) BUFF at the head of the active sink table is completed by this
	 *    write event completion
	 * 2) IOCB at the head of the active sink table is not associated
	 *    with this event, meaning a later event in flight will be the
	 *    write to complete it, no IOCB is completed by this event.
	 * 3) No IOCBs are in the active table, the head of the send pending
	 *    table, matches the work request ID of the event.
	 */
	type = sdp_generic_table_type_head(&conn->w_snk);
	switch (type) {
		case SDP_GENERIC_TYPE_BUFF:

			buff = (sdp_buff_t *)sdp_generic_table_get_head(
			    &conn->w_snk);
			SDP_EXPECT((NULL != buff));

			conn->sdpc_tx_bytes_queued -= buff->sdp_buff_data_size;

			sdp_buff_free(buff);
			break;
		default:

			result = -EPROTO;

			goto error;
	} /* switch */

	/*
	 * update queue depth
	 */
	conn->s_wq_size--;
	conn->sink_actv--;

	/*
	 * It's possible that the "send" queue was opened up by the completion
	 * of some more sends.
	 */
	sdp_send_flush(conn);

	/*
	 * The completion of the RDMA read may allow us to post additional RDMA
	 * reads.
	 */
	result = sdp_recv_post(conn);
	if (result) {

		goto error;
	} /* if */

	return (0);
error:
	return (result);
} /* sdp_event_write */
