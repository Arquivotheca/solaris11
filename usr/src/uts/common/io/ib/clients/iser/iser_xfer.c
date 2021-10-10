/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/modctl.h>
#include <sys/byteorder.h>
#include <sys/sdt.h>

#include <sys/ib/clients/iser/iser.h>

/*
 * iser_xfer.c
 */

static iser_status_t
iser_post_send(iser_chan_t *iser_chan, ibt_send_wr_t *swqp, uint32_t nwrs)
{
	uint32_t	posted;
	ibt_status_t	ibt_status;

	/*
	 * Increment this channel's SQ posted count.  If it goes over the limit,
	 * then (for now) fail this I/O without posting it.  Eventually we need
	 * to queue these I/Os and retry them later.
	 */
	mutex_enter(&iser_chan->ic_sq_post_lock);
	if (iser_chan->ic_sq_post_count + nwrs > iser_chan->ic_sizes.cs_sq) {
		ISER_LOG(CE_NOTE, "iser_post_send: flow control dropped: "
		    "(%d %d %d)", iser_chan->ic_sq_post_count, nwrs,
		    iser_chan->ic_sizes.cs_sq);
		DTRACE_PROBE3(iser__post__send__dropped, iser_chan_t *,
		    iser_chan, ibt_send_wr_t *, swqp, uint32_t, nwrs);
		iser_chan->ic_sq_dropped++;
		mutex_exit(&iser_chan->ic_sq_post_lock);
		return (ISER_STATUS_FAIL);
	}
	iser_chan->ic_sq_post_count += nwrs;
	if (iser_chan->ic_sq_post_count > iser_chan->ic_sq_max_post_count)
		iser_chan->ic_sq_max_post_count = iser_chan->ic_sq_post_count;
	mutex_exit(&iser_chan->ic_sq_post_lock);

	ibt_status = ibt_post_send(iser_chan->ic_chanhdl, swqp, nwrs, &posted);
	if (ibt_status != IBT_SUCCESS || posted != nwrs) {
		ISER_LOG(CE_NOTE, "iser_post_send: ibt_post_send "
		    "failure (%d %d %d)", ibt_status, posted, nwrs);
		mutex_enter(&iser_chan->ic_sq_post_lock);
		iser_chan->ic_sq_post_count -= (nwrs - posted);
		mutex_exit(&iser_chan->ic_sq_post_lock);
		if (posted == 0) {
			/* If nothing was queued, then caller must clean up */
			return (ISER_STATUS_FAIL);
		}
	}
	return (ISER_STATUS_SUCCESS);
}




int
iser_xfer_hello_msg(iser_chan_t *chan)
{
	iser_hca_t		*hca;
	iser_wr_t		*iser_wr;
	iser_msg_t		*msg;
	ibt_send_wr_t		wr;
	iser_hello_hdr_t	*hdr;
	int			status;

	ASSERT(chan != NULL);

	hca = (iser_hca_t *)chan->ic_hca;
	if (hca == NULL) {
		ISER_LOG(CE_NOTE, "iser_xfer_hello_msg: no hca handle found");
		return (ISER_STATUS_FAIL);
	}

	msg = iser_msg_get(hca, 1, NULL);

	if (msg == NULL) {
		ISER_LOG(CE_NOTE, "iser_xfer_hello_msg: iser message cache "
		    "alloc failed");
		return (ISER_STATUS_FAIL);
	}

	/* Send iSER Hello Message to declare iSER parameters to the target */
	hdr = (iser_hello_hdr_t *)(uintptr_t)msg->msg_ds.ds_va;

	hdr->opcode	= ISER_OPCODE_HELLO_MSG;
	hdr->rsvd1	= 0;
	hdr->maxver 	= 1;
	hdr->minver 	= 1;
	hdr->iser_ird 	= htons(ISER_IB_DEFAULT_IRD);
	hdr->rsvd2[0] 	= 0;
	hdr->rsvd2[1] 	= 0;

	/* Allocate an iSER WR handle and tuck this msg into it */
	iser_wr = iser_wr_get();
	if (iser_wr == NULL) {
		ISER_LOG(CE_NOTE, "iser_xfer_hello_msg: unable to allocate "
		    "iser wr handle");
		iser_msg_free(msg);
		return (ISER_STATUS_FAIL);
	}
	iser_wr->iw_msg = msg;
	iser_wr->iw_type = ISER_WR_SEND;

	/* Use the address of our generic iser_wr handle as our WRID */
	wr.wr_id	= (ibt_wrid_t)(uintptr_t)iser_wr;

	/* Populate the rest of the work request */
	wr.wr_trans	= IBT_RC_SRV;
	wr.wr_opcode	= IBT_WRC_SEND;
	wr.wr_nds	= 1;
	wr.wr_sgl	= &msg->msg_ds;
	wr.wr_flags	= IBT_WR_SEND_SIGNAL;

	/*
	 * Avoid race condition by incrementing this channel's
	 * SQ posted count prior to calling ibt_post_send
	 */
	mutex_enter(&chan->ic_sq_post_lock);
	chan->ic_sq_post_count++;
	if (chan->ic_sq_post_count > chan->ic_sq_max_post_count)
		chan->ic_sq_max_post_count = chan->ic_sq_post_count;
	mutex_exit(&chan->ic_sq_post_lock);

	status = ibt_post_send(chan->ic_chanhdl, &wr, 1, NULL);
	if (status != IBT_SUCCESS) {
		ISER_LOG(CE_NOTE, "iser_xfer_hello_msg: ibt_post_send "
		    "failure (%d)", status);
		mutex_enter(&chan->ic_sq_post_lock);
		chan->ic_sq_post_count--;
		mutex_exit(&chan->ic_sq_post_lock);
		iser_msg_free(msg);
		iser_wr_free(iser_wr);
		return (ISER_STATUS_FAIL);
	}

	ISER_LOG(CE_NOTE, "Posting iSER Hello message: chan (0x%p): "
	    "IP [%x to %x]", (void *)chan, chan->ic_localip.un.ip4addr,
	    chan->ic_remoteip.un.ip4addr);

	return (ISER_STATUS_SUCCESS);
}

int
iser_xfer_helloreply_msg(iser_chan_t *chan)
{
	iser_hca_t		*hca;
	iser_wr_t		*iser_wr;
	ibt_send_wr_t   	wr;
	iser_msg_t		*msg;
	iser_helloreply_hdr_t	*hdr;
	int			status;

	ASSERT(chan != NULL);

	hca = (iser_hca_t *)chan->ic_hca;
	if (hca == NULL) {
		ISER_LOG(CE_NOTE, "iser_xfer_helloreply_msg: no hca handle "
		    "found");
		return (ISER_STATUS_FAIL);
	}

	msg = iser_msg_get(hca, 1, NULL);

	if (msg == NULL) {
		ISER_LOG(CE_NOTE, "iser_xfer_helloreply_msg: iser message "
		    "cache alloc failed");
		return (ISER_STATUS_FAIL);
	}

	/* Use the iSER Hello Reply Message */
	hdr = (iser_helloreply_hdr_t *)(uintptr_t)msg->msg_ds.ds_va;

	hdr->opcode	= ISER_OPCODE_HELLOREPLY_MSG;
	hdr->rsvd1	= 0;
	hdr->flag	= 0;
	hdr->maxver	= 1;
	hdr->curver	= 1;
	hdr->iser_ord	= htons(ISER_IB_DEFAULT_ORD);
	hdr->rsvd2[0]	= 0;
	hdr->rsvd2[1]	= 0;

	/* Allocate an iSER WR handle and tuck this msg into it */
	iser_wr = iser_wr_get();
	if (iser_wr == NULL) {
		ISER_LOG(CE_NOTE, "iser_xfer_helloreply_msg: unable to "
		    "allocate iser wr handle");
		iser_msg_free(msg);
		return (ISER_STATUS_FAIL);
	}
	iser_wr->iw_msg = msg;
	iser_wr->iw_type = ISER_WR_SEND;

	/* Use the address of our generic iser_wr handle as our WRID */
	wr.wr_id	= (ibt_wrid_t)(uintptr_t)iser_wr;

	/* Populate the rest of the work request */
	wr.wr_trans	= IBT_RC_SRV;
	wr.wr_opcode	= IBT_WRC_SEND;
	wr.wr_nds	= 1;
	wr.wr_sgl	= &msg->msg_ds;
	wr.wr_flags	= IBT_WR_SEND_SIGNAL;

	mutex_enter(&chan->ic_sq_post_lock);
	chan->ic_sq_post_count++;
	if (chan->ic_sq_post_count > chan->ic_sq_max_post_count)
		chan->ic_sq_max_post_count = chan->ic_sq_post_count;

	mutex_exit(&chan->ic_sq_post_lock);

	status = ibt_post_send(chan->ic_chanhdl, &wr, 1, NULL);
	if (status != IBT_SUCCESS) {
		ISER_LOG(CE_NOTE, "iser_xfer_helloreply_msg: ibt_post_send "
		    "failure (%d)", status);
		mutex_enter(&chan->ic_sq_post_lock);
		chan->ic_sq_post_count--;
		mutex_exit(&chan->ic_sq_post_lock);
		iser_msg_free(msg);
		iser_wr_free(iser_wr);
		return (ISER_STATUS_FAIL);
	}
	ISER_LOG(CE_NOTE, "Posting iSER HelloReply message: chan (0x%p): "
	    "IP [%x to %x]", (void *)chan, chan->ic_localip.un.ip4addr,
	    chan->ic_remoteip.un.ip4addr);

	return (ISER_STATUS_SUCCESS);
}

/*
 * iser_xfer_ctrlpdu
 *
 * This is iSER's implementation of the 'Send_control' operational primitive.
 * This iSER layer uses the Send Message type of RCaP to transfer the iSCSI
 * Control-type PDU. A special case is that the transfer of SCSI Data-out PDUs
 * carrying unsolicited data are also treated as iSCSI Control-Type PDUs. The
 * message payload contains an iSER header followed by the iSCSI Control-type
 * the iSCSI Control-type PDU.
 * This function is invoked by an initiator iSCSI layer requesting the transfer
 * of a iSCSI command PDU or a target iSCSI layer requesting the transfer of a
 * iSCSI response PDU.
 */
int
iser_xfer_ctrlpdu(iser_chan_t *chan, idm_pdu_t *pdu)
{
	iser_hca_t	*hca;
	iser_ctrl_hdr_t	*hdr;
	iser_msg_t	*msg;
	iser_wr_t	*iser_wr;
	ibt_send_wr_t   wr;
	int		status;
	iser_mr_t	*mr;
	iscsi_data_hdr_t	*bhs;
	idm_conn_t	*ic;
	idm_task_t	*idt = NULL;
	idm_buf_t	*buf;

	ASSERT(chan != NULL);

	mutex_enter(&chan->ic_conn->ic_lock);
	/* Bail out if the connection is closed */
	if ((chan->ic_conn->ic_stage == ISER_CONN_STAGE_CLOSING) ||
	    (chan->ic_conn->ic_stage == ISER_CONN_STAGE_CLOSED)) {
		mutex_exit(&chan->ic_conn->ic_lock);
		return (ISER_STATUS_FAIL);
	}

	ic = chan->ic_conn->ic_idmc;

	/* Pull the BHS out of the PDU handle */
	bhs = (iscsi_data_hdr_t *)pdu->isp_hdr;

	/*
	 * All SCSI command PDU (except SCSI Read and SCSI Write) and the SCSI
	 * Response PDU are sent to the remote end using the SendSE Message.
	 *
	 * The StatSN may need to be sent (and possibly advanced) at this time
	 * for some PDUs, identified by the IDM_PDU_SET_STATSN flag.
	 */
	if (pdu->isp_flags & IDM_PDU_SET_STATSN) {
		(ic->ic_conn_ops.icb_update_statsn)(NULL, pdu);
	}
	/*
	 * Setup a Send Message for carrying the iSCSI control-type PDU
	 * preceded by an iSER header.
	 */
	hca = (iser_hca_t *)chan->ic_hca;
	if (hca == NULL) {
		ISER_LOG(CE_NOTE, "iser_xfer_ctrlpdu: no hca handle found");
		mutex_exit(&chan->ic_conn->ic_lock);
		return (ISER_STATUS_FAIL);
	}

	msg = iser_msg_get(hca, 1, NULL);
	if (msg == NULL) {
		ISER_LOG(CE_NOTE, "iser_xfer_ctrlpdu: iser message cache "
		    "alloc failed");
		mutex_exit(&chan->ic_conn->ic_lock);
		return (ISER_STATUS_FAIL);
	}

	hdr = (iser_ctrl_hdr_t *)(uintptr_t)msg->msg_ds.ds_va;

	/*
	 * Initialize header assuming no transfers
	 */
	bzero(hdr, sizeof (*hdr));
	hdr->opcode	= ISER_OPCODE_CTRL_TYPE_PDU;

	/*
	 * On the initiator side, the task buffers will be used to identify
	 * if there are any buffers to be advertised
	 */
	if ((ic->ic_conn_type == CONN_TYPE_INI) &&
	    ((bhs->opcode & ISCSI_OPCODE_MASK) == ISCSI_OP_SCSI_CMD) &&
	    ((idt = idm_task_find(ic, bhs->itt, bhs->ttt)) != NULL)) {

		if (!list_is_empty(&idt->idt_inbufv)) {
			buf = idm_buf_find(&idt->idt_inbufv, 0);
			ASSERT(buf != NULL);

			mr = (iser_mr_t *)buf->idb_reg_private;
			ASSERT(mr != NULL);

			hdr->rsv_flag = 1;
			hdr->rstag = htonl(mr->is_mrrkey);
			BE_OUT64(&hdr->rva, mr->is_mrva);
		}

		if (!list_is_empty(&idt->idt_outbufv)) {
			buf = idm_buf_find(&idt->idt_outbufv, 0);
			ASSERT(buf != NULL);

			mr = (iser_mr_t *)buf->idb_reg_private;
			ASSERT(mr != NULL);

			hdr->wsv_flag = 1;
			hdr->wstag = htonl(mr->is_mrrkey);
			BE_OUT64(&hdr->wva, mr->is_mrva);
		}

		/* Release our reference on the task */
		idm_task_rele(idt);
	}

	/* Copy the BHS after the iSER header */
	bcopy(pdu->isp_hdr,
	    (uint8_t *)(uintptr_t)msg->msg_ds.ds_va + ISER_HEADER_LENGTH,
	    pdu->isp_hdrlen);

	if (pdu->isp_datalen > 0) {
		/* Copy the isp_data after the PDU header */
		bcopy(pdu->isp_data,
		    (uint8_t *)(uintptr_t)msg->msg_ds.ds_va +
		    ISER_HEADER_LENGTH + pdu->isp_hdrlen,
		    pdu->isp_datalen);

		/* Set the SGE's ds_len */
		msg->msg_ds.ds_len = ISER_HEADER_LENGTH + pdu->isp_hdrlen +
		    pdu->isp_datalen;
	} else {
		/* No data, so set the SGE's ds_len to the headers length */
		msg->msg_ds.ds_len = ISER_HEADER_LENGTH + pdu->isp_hdrlen;
	}

	/*
	 * Build Work Request to be posted on the Send Queue.
	 */
	bzero(&wr, sizeof (wr));

	/* Allocate an iSER WR handle and tuck the msg and pdu into it */
	iser_wr = iser_wr_get();
	if (iser_wr == NULL) {
		ISER_LOG(CE_NOTE, "iser_xfer_ctrlpdu: unable to allocate "
		    "iser wr handle");
		iser_msg_free(msg);
		mutex_exit(&chan->ic_conn->ic_lock);
		return (ISER_STATUS_FAIL);
	}
	iser_wr->iw_pdu = pdu;
	iser_wr->iw_msg = msg;
	iser_wr->iw_type = ISER_WR_SEND;

	/*
	 * Use the address of our generic iser_wr handle as our WRID
	 * and populate the rest of the work request
	 */
	wr.wr_id	= (ibt_wrid_t)(uintptr_t)iser_wr;
	wr.wr_trans	= IBT_RC_SRV;
	wr.wr_opcode	= IBT_WRC_SEND;
	wr.wr_nds	= 1;
	wr.wr_sgl	= &msg->msg_ds;
	wr.wr_flags	= IBT_WR_SEND_SIGNAL;

	/* Increment this channel's SQ posted count */
	mutex_enter(&chan->ic_sq_post_lock);
	chan->ic_sq_post_count++;
	if (chan->ic_sq_post_count > chan->ic_sq_max_post_count)
		chan->ic_sq_max_post_count = chan->ic_sq_post_count;
	mutex_exit(&chan->ic_sq_post_lock);

	/* Post Send Work Request on the specified channel */
	status = ibt_post_send(chan->ic_chanhdl, &wr, 1, NULL);
	if (status != IBT_SUCCESS) {
		ISER_LOG(CE_NOTE, "iser_xfer_ctrlpdu: ibt_post_send "
		    "failure (%d)", status);
		iser_msg_free(msg);
		iser_wr_free(iser_wr);
		mutex_enter(&chan->ic_sq_post_lock);
		chan->ic_sq_post_count--;
		mutex_exit(&chan->ic_sq_post_lock);
		mutex_exit(&chan->ic_conn->ic_lock);
		return (ISER_STATUS_FAIL);
	}

	mutex_exit(&chan->ic_conn->ic_lock);
	return (ISER_STATUS_SUCCESS);
}

/*
 * iser_xfer_buf_to_ini
 * This is iSER's implementation of the 'Put_Data' operational primitive.
 * The iSCSI layer at the target invokes this function when it is ready to
 * return the SCSI Read Data to the initiator. This function generates and
 * sends an RDMA Write Message containing the read data to the initiator.
 */
int
iser_xfer_buf_to_ini(idm_task_t *idt, idm_buf_t *buf)
{
	iser_conn_t	*iser_conn;
	iser_chan_t	*iser_chan;
	iser_hca_t	*iser_hca;
	iser_buf_t	*iser_buf;
	iser_sgl_t	*iser_sgl;
	iser_wr_t	*iser_wr;
	iser_ctrl_hdr_t	*iser_hdr;
	ibt_send_wr_t	wr, *pwr, *swqp;
	uint64_t	reg_raddr, remote_addr;
	uint32_t	reg_rkey;
	uint32_t	nwrs, j, idx, left, count;
	int		sent;
	ibt_wr_ds_t	*sgl;
	iser_status_t	status;

	/* Grab the iSER resources from the task and buf handles */
	iser_conn = (iser_conn_t *)idt->idt_ic->ic_transport_private;
	iser_chan = iser_conn->ic_chan;
	iser_hca = iser_conn->ic_chan->ic_hca;

	mutex_enter(&iser_chan->ic_conn->ic_lock);
	/* Bail out if the connection is closed */
	if ((iser_chan->ic_conn->ic_stage == ISER_CONN_STAGE_CLOSING) ||
	    (iser_chan->ic_conn->ic_stage == ISER_CONN_STAGE_CLOSED)) {
		mutex_exit(&iser_chan->ic_conn->ic_lock);
		return (ISER_STATUS_FAIL);
	}

	/* Pull the Read STag data out of the iSER header in the task hdl */
	iser_hdr  = (iser_ctrl_hdr_t *)idt->idt_transport_hdr;
	reg_raddr = BE_IN64(&iser_hdr->rva);
	reg_rkey  = (ntohl(iser_hdr->rstag));

	/* Allocate an iSER WR handle and tuck the IDM buf handle into it */
	iser_wr = iser_wr_get();
	if (iser_wr == NULL) {
		ISER_LOG(CE_NOTE, "iser_xfer_buf_to_ini: unable to allocate "
		    "iser wr handle");
		mutex_exit(&iser_chan->ic_conn->ic_lock);
		return (ISER_STATUS_FAIL);
	}
	iser_wr->iw_buf = buf;
	iser_wr->iw_type = ISER_WR_RDMAW;

	if (buf->idb_bufalloc == IDB_ZCOPY) {
		/*
		 * For ZCOPY, the WR was set up by iser_alloc_buf_sgl,
		 * We copy the scatter-gather entries from there into a new
		 * set of work requests.  This spreads out the work enough
		 * so that the IB card (e.g. hermon) can handle it.
		 */
		ASSERT(iser_hca->hca_max_sgl_size > 1);
		iser_sgl = buf->idb_buf_private;
		remote_addr = reg_raddr + buf->idb_bufoffset;
		idx = 0;
		sent = 0;
		left = iser_sgl->buf_wr.wr_nds;
		ASSERT(left > 0);
		sgl = iser_sgl->buf_wr.wr_sgl;
		swqp = &iser_sgl->buf_xmit_wrs[0];
		nwrs = 0;

		do {
			pwr = &swqp[nwrs];
			bzero(pwr, sizeof (*pwr));
			/*
			 * We save the wrid in each send WR, to help with DEBUG.
			 */
			pwr->wr_id = (ibt_wrid_t)((uintptr_t)iser_wr | 1);
			pwr->wr_trans = IBT_RC_SRV;
			pwr->wr_opcode = IBT_WRC_RDMAW;
			pwr->wr.rc.rcwr.rdma.rdma_raddr = remote_addr;
			pwr->wr.rc.rcwr.rdma.rdma_rkey = reg_rkey;

			if (left > iser_hca->hca_max_sgl_size) {
				count = iser_hca->hca_max_sgl_size;
				left -= iser_hca->hca_max_sgl_size;
			} else {
				count = left;
				left = 0;
			}

			/*
			 * Rather than copying the SGEs from the SGList that
			 * is the output of ibt_map_mem_iov, we just point
			 * the new WRs directly into the right offset in the
			 * SGList.  If we change to use multiple calls to
			 * ibt_map_mem_iov, this will have to change as well.
			 */
			pwr->wr_nds = count;
			pwr->wr_sgl = &sgl[idx];
			for (j = 0; j < count; j++) {
				remote_addr += sgl[idx].ds_len;
				sent += sgl[idx].ds_len;
				idx++;
			}
			nwrs++;
		} while (left > 0);

		ASSERT(sent == buf->idb_xfer_len);
		VERIFY(nwrs < ISER_IB_MAX_WRS);

		pwr->wr_flags = IBT_WR_SEND_SIGNAL;
		iser_wr->iw_nwrs = nwrs;
		pwr->wr_id = (ibt_wrid_t)(uintptr_t)iser_wr;

	} else {
		iser_buf = buf->idb_buf_private;
		bzero(&wr, sizeof (ibt_send_wr_t));
		nwrs = 1;
		wr.wr_nds = 1;
		wr.wr_sgl = &iser_buf->buf_ds;
		/* Set the transfer length from the IDM buf handle */
		iser_buf->buf_ds.ds_len = buf->idb_xfer_len;
		swqp = &wr;
		/* Use the address of our generic iser_wr handle as our WRID */
		swqp->wr_id	= (ibt_wrid_t)(uintptr_t)iser_wr;

		/* Populate the rest of the work request */
		swqp->wr.rc.rcwr.rdma.rdma_raddr =
		    reg_raddr + buf->idb_bufoffset;
		swqp->wr.rc.rcwr.rdma.rdma_rkey  = reg_rkey;
		swqp->wr_flags	= IBT_WR_SEND_SIGNAL;
		swqp->wr_trans	= IBT_RC_SRV;
		swqp->wr_opcode	= IBT_WRC_RDMAW;

#ifdef DEBUG
		if (buf->idb_bufalloc != IDB_ZCOPY)
			bcopy(&wr, &iser_buf->buf_wr, sizeof (ibt_send_wr_t));
#endif
	}

	DTRACE_ISCSI_8(xfer__start, idm_conn_t *, idt->idt_ic,
	    uintptr_t, buf->idb_buf, uint32_t, buf->idb_bufoffset,
	    uint64_t, reg_raddr, uint32_t, buf->idb_bufoffset,
	    uint32_t,  reg_rkey,
	    uint32_t, buf->idb_xfer_len, int, XFER_BUF_TX_TO_INI);


	status = iser_post_send(iser_chan, swqp, nwrs);
	if (status != ISER_STATUS_SUCCESS) {
		iser_wr_free(iser_wr);
	}
	mutex_exit(&iser_chan->ic_conn->ic_lock);
	return (status);
}

/*
 * iser_xfer_buf_from_ini
 * This is iSER's implementation of the 'Get_Data' operational primitive.
 * The iSCSI layer at the target invokes this function when it is ready to
 * receive the SCSI Write Data from the initiator. This function generates and
 * sends an RDMA Read Message to get the data from the initiator. No R2T PDUs
 * are generated.
 */
int
iser_xfer_buf_from_ini(idm_task_t *idt, idm_buf_t *buf)
{
	iser_conn_t	*iser_conn;
	iser_chan_t	*iser_chan;
	iser_hca_t	*iser_hca;
	iser_buf_t	*iser_buf;
	iser_sgl_t	*iser_sgl;
	iser_wr_t	*iser_wr;
	iser_ctrl_hdr_t	*iser_hdr;
	ibt_send_wr_t	wr, *pwr, *swqp;
	uint64_t	reg_raddr, remote_addr;
	uint32_t	reg_rkey;
	uint32_t	nwrs, j, idx, left, count;
	int		sent;
	ibt_wr_ds_t	*sgl;
	iser_status_t	status;

	/* Grab the iSER resources from the task and buf handles */
	iser_conn = (iser_conn_t *)idt->idt_ic->ic_transport_private;
	iser_chan = iser_conn->ic_chan;
	iser_hca = iser_conn->ic_chan->ic_hca;

	mutex_enter(&iser_chan->ic_conn->ic_lock);
	/* Bail out if the connection is closed */
	if ((iser_chan->ic_conn->ic_stage == ISER_CONN_STAGE_CLOSING) ||
	    (iser_chan->ic_conn->ic_stage == ISER_CONN_STAGE_CLOSED)) {
		mutex_exit(&iser_chan->ic_conn->ic_lock);
		return (ISER_STATUS_FAIL);
	}

	/* Pull the Write STag data out of the iSER header in the task hdl */
	iser_hdr  = (iser_ctrl_hdr_t *)idt->idt_transport_hdr;
	reg_raddr = BE_IN64(&iser_hdr->wva);
	reg_rkey  = (ntohl(iser_hdr->wstag));

	/* Allocate an iSER WR handle and tuck the IDM buf handle into it */
	iser_wr = iser_wr_get();
	if (iser_wr == NULL) {
		ISER_LOG(CE_NOTE, "iser_xfer_buf_from_ini: unable to allocate "
		    "iser wr handle");
		mutex_exit(&iser_chan->ic_conn->ic_lock);
		return (ISER_STATUS_FAIL);
	}
	iser_wr->iw_buf = buf;
	iser_wr->iw_type = ISER_WR_RDMAR;

	if (buf->idb_bufalloc == IDB_ZCOPY) {
		/*
		 * For ZCOPY, the WR was set up by iser_alloc_buf_sgl,
		 * We copy the scatter-gather entries from there into a new
		 * set of work requests.  This spreads out the work enough
		 * so that the IB card (e.g. hermon) can handle it.
		 */
		ASSERT(iser_hca->hca_max_sgl_size > 1);
		iser_sgl = buf->idb_buf_private;
		remote_addr = reg_raddr + buf->idb_bufoffset;
		idx = 0;
		sent = 0;
		left = iser_sgl->buf_wr.wr_nds;
		ASSERT(left > 0);
		sgl = iser_sgl->buf_wr.wr_sgl;
		swqp = &iser_sgl->buf_xmit_wrs[0];

		nwrs = 0;
		do {
			pwr = &swqp[nwrs];
			bzero(pwr, sizeof (*pwr));
			/*
			 * We save the wrid in each send WR, to help with DEBUG.
			 */
			pwr->wr_id = (ibt_wrid_t)((uintptr_t)iser_wr | 1);
			pwr->wr_trans = IBT_RC_SRV;
			pwr->wr_opcode = IBT_WRC_RDMAR;
			pwr->wr.rc.rcwr.rdma.rdma_raddr = remote_addr;
			pwr->wr.rc.rcwr.rdma.rdma_rkey = reg_rkey;

			if (left > iser_hca->hca_max_sgl_size) {
				count = iser_hca->hca_max_sgl_size;
				left -= iser_hca->hca_max_sgl_size;
			} else {
				count = left;
				left = 0;
			}

			/*
			 * Rather than copying the SGEs from the SGList that
			 * is the output of ibt_map_mem_iov, we just point
			 * the new WRs directly into the right offset in the
			 * SGList.  If we change to use multiple calls to
			 * ibt_map_mem_iov, this will have to change as well.
			 */
			pwr->wr_nds = count;
			pwr->wr_sgl = &sgl[idx];
			for (j = 0; j < count; j++) {
				remote_addr += sgl[idx].ds_len;
				sent += sgl[idx].ds_len;
				idx++;
			}
			nwrs++;
		} while (left > 0);

		ASSERT(sent == buf->idb_xfer_len);
		VERIFY(nwrs < ISER_IB_MAX_WRS);

		pwr->wr_flags = IBT_WR_SEND_SIGNAL;
		iser_wr->iw_nwrs = nwrs;
		pwr->wr_id = (ibt_wrid_t)(uintptr_t)iser_wr;

	} else {
		iser_buf = buf->idb_buf_private;
		bzero(&wr, sizeof (ibt_send_wr_t));
		nwrs = 1;
		wr.wr_nds = 1;
		wr.wr_sgl = &iser_buf->buf_ds;
		/* Set the transfer length from the IDM buf handle */
		iser_buf->buf_ds.ds_len = buf->idb_xfer_len;
		swqp = &wr;
		/* Use the address of our generic iser_wr handle as our WRID */
		swqp->wr_id	= (ibt_wrid_t)(uintptr_t)iser_wr;

		/* Populate the rest of the work request */
		swqp->wr.rc.rcwr.rdma.rdma_raddr =
		    reg_raddr + buf->idb_bufoffset;
		swqp->wr.rc.rcwr.rdma.rdma_rkey  = reg_rkey;
		swqp->wr_flags	= IBT_WR_SEND_SIGNAL;
		swqp->wr_trans	= IBT_RC_SRV;
		swqp->wr_opcode	= IBT_WRC_RDMAR;

#ifdef DEBUG
		if (buf->idb_bufalloc != IDB_ZCOPY)
			bcopy(&wr, &iser_buf->buf_wr, sizeof (ibt_send_wr_t));
#endif
	}

	DTRACE_ISCSI_8(xfer__start, idm_conn_t *, idt->idt_ic,
	    uintptr_t, buf->idb_buf, uint32_t, buf->idb_bufoffset,
	    uint64_t, reg_raddr, uint32_t, buf->idb_bufoffset,
	    uint32_t,  reg_rkey,
	    uint32_t, buf->idb_xfer_len, int, XFER_BUF_RX_FROM_INI);

	status = iser_post_send(iser_chan, swqp, nwrs);
	if (status != ISER_STATUS_SUCCESS) {
		iser_wr_free(iser_wr);
	}
	mutex_exit(&iser_chan->ic_conn->ic_lock);
	return (status);
}
