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

/*
 * The following notice accompanied the original version of this file:
 *
 * BSD LICENSE
 *
 * Copyright(c) 2007 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * This file defines interfaces between fcoe and fcoet driver.
 */

/*
 * Driver kernel header files
 */
#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/stat.h>
#include <sys/pci.h>
#include <sys/sunddi.h>
#include <sys/modctl.h>
#include <sys/file.h>
#include <sys/cred.h>
#include <sys/byteorder.h>
#include <sys/atomic.h>
#include <sys/scsi/scsi.h>
#include <sys/ethernet.h>
#include <sys/strsun.h>

/*
 * COMSTAR header files
 */
#include <sys/stmf_defines.h>
#include <sys/fct_defines.h>
#include <sys/stmf.h>
#include <sys/portif.h>
#include <sys/fct.h>

/*
 * FCoE header files
 */
#include <sys/fcoe/fcoe_common.h>

/*
 * Driver's own header files
 */
#include "fcoet.h"
#include "fcoet_eth.h"

/*
 * function forward declaration
 */
static fcoet_exchange_t *fcoet_create_unsol_exchange(fcoe_frame_t *frame);
static int fcoet_process_sol_fcp_data(fcoe_frame_t *frm);
static int fcoet_process_unsol_fcp_cmd(fcoe_frame_t *frm);
static int fcoet_process_unsol_els_req(fcoe_frame_t *frm);
static int fcoet_process_sol_els_rsp(fcoe_frame_t *frm);
static int fcoet_process_unsol_abts_req(fcoe_frame_t *frame);
static int fcoet_process_sol_abts_acc(fcoe_frame_t *frame);
static int fcoet_process_sol_abts_rjt(fcoe_frame_t *frame);
static int fcoet_process_unsol_ct_req(fcoe_frame_t *frm);
static int fcoet_process_sol_ct_rsp(fcoe_frame_t *frame);
static int fcoet_process_sol_flogi_rsp(fcoe_frame_t *frame);
static int fcoet_send_sol_fcp_data_done(fcoe_frame_t *frm);
static int fcoet_send_fcp_status_done(fcoe_frame_t *frm);
static int fcoet_send_unsol_els_rsp_done(fcoe_frame_t *frm);
static int fcoet_send_sol_els_req_done(fcoe_frame_t *frm);
static int fcoet_send_unsol_bls_acc_done(fcoe_frame_t *frm);
static int fcoet_send_unsol_bls_rjt_done(fcoe_frame_t *frm);
static int fcoet_send_sol_bls_req_done(fcoe_frame_t *frm);
static int fcoet_send_sol_ct_req_done(fcoe_frame_t *frm);
static int fcoet_process_unsol_flogi_req(fcoet_exchange_t *xch);

/*
 * rx_frame & release_sol_frame
 */

void
fcoet_rx_frame(fcoe_frame_t *frm)
{
	uint8_t rctl = FRM_R_CTL(frm);

	switch (rctl) {
	case 0x01:
		/*
		 * Solicited data
		 */
		if (fcoet_process_sol_fcp_data(frm)) {
			FCOET_LOG("fcoet_rx_frame",
			    "fcoet_process_sol_fcp_data failed");
		}
		break;

	case 0x06:
		/*
		 * Unsolicited fcp_cmnd
		 */
		if (fcoet_process_unsol_fcp_cmd(frm)) {
			FCOET_LOG("fcoet_rx_frame",
			    "fcoet_process_unsol_fcp_cmd failed");
		}
		break;

	case 0x22:
		/*
		 * unsolicited ELS req
		 */
		if (fcoet_process_unsol_els_req(frm)) {
			FCOET_LOG("fcoet_rx_frame",
			    "fcoet_process_unsol_els_req failed");
		}
		break;

	case 0x23:
		/*
		 * solicited ELS rsp
		 */
		if (fcoet_process_sol_els_rsp(frm)) {
			FCOET_LOG("fcoet_rx_frame",
			    "fcoet_process_sol_els_rsp failed");
		}
		break;

	case 0x81:
		/*
		 *  unsolicted ABTS req
		 */
		if (fcoet_process_unsol_abts_req(frm)) {
			FCOET_LOG("fcoet_rx_frame",
			    "fcoet_process_unsol_abts_req failed");
		}
		break;

	case 0x84:
		/*
		 * solicited ABTS acc response
		 */
		if (fcoet_process_sol_abts_acc(frm)) {
			FCOET_LOG("fcoet_rx_frame",
			    "fcoet_process_sol_abts_acc failed");
		}
		break;
	case 0x85:
		/*
		 * solcited ABTS rjt response
		 */
		if (fcoet_process_sol_abts_rjt(frm)) {
			FCOET_LOG("fcoet_rx_frame",
			    "fcoet_process_sol_abts_rjt failed");
		}
		break;

	case 0x02:
		/*
		 * unsolicited CT req
		 */
		if (fcoet_process_unsol_ct_req(frm)) {
			FCOET_LOG("fcoet_rx_frame",
			    "fcoet_process_sol_ct_rsp failed");
		}
		break;

	case 0x03:
		/*
		 * sol ct rsp
		 */
		if (fcoet_process_sol_ct_rsp(frm)) {
			FCOET_LOG("fcoet_rx_frame",
			    "fcoet_process_sol_ct_rsp failed");
		}
		break;

	default:
		/*
		 * Unsupported frame
		 */
		PRT_FRM_HDR("Unsupported unsol frame: ", frm);
		break;
	}

	/*
	 * Release the frame in the end
	 */
	frm->frm_eport->eport_free_netb(frm->frm_netb);
	frm->frm_eport->eport_release_frame(frm);
}

/*
 * For solicited frames, after FCoE has sent it out, it will call this
 * to notify client(FCoEI/FCoET) about its completion.
 */
void
fcoet_release_sol_frame(fcoe_frame_t *frm)
{
	fcoet_exchange_t	*xch = FRM2TFM(frm)->tfm_xch;

	/*
	 * From now, we should not access both frm_hdr and frm_payload. Its
	 * mblk could have been released by MAC driver.
	 */
	switch (FRM2TFM(frm)->tfm_rctl) {
	case 0x01:
		if (xch && xch->xch_flags & XCH_FLAG_FCT_CALLED_ABORT) {
			FCOET_RELE_XCHG(xch);
			break;
		}
		if (fcoet_send_sol_fcp_data_done(frm)) {
			ASSERT(0);
		}
		break;

	case 0x05:
		break;

	case 0x07:
		if (xch && xch->xch_flags & XCH_FLAG_FCT_CALLED_ABORT) {
			FCOET_RELE_XCHG(xch);
			break;
		}

		if (fcoet_send_fcp_status_done(frm)) {
			ASSERT(0);
		}
		break;

	case 0x23:
		if (xch && xch->xch_flags & XCH_FLAG_FCT_CALLED_ABORT) {
			FCOET_RELE_XCHG(xch);
			break;
		}
		if (fcoet_send_unsol_els_rsp_done(frm)) {
			ASSERT(0);
		}
		break;

	case 0x22:
		if (fcoet_send_sol_els_req_done(frm)) {
			ASSERT(0);
		}
		break;

	case 0x84:
		if (fcoet_send_unsol_bls_acc_done(frm)) {
			ASSERT(0);
		}
		break;

	case 0x85:
		if (fcoet_send_unsol_bls_rjt_done(frm)) {
			ASSERT(0);
		}
		break;

	case 0x81:
		if (fcoet_send_sol_bls_req_done(frm)) {
			ASSERT(0);
		}
		break;

	case 0x02:
		if (fcoet_send_sol_ct_req_done(frm)) {
			ASSERT(0);
		}
		break;

	case 0x03:
	default:
		/*
		 * Unsupported frame
		 */
		PRT_FRM_HDR("Unsupported sol frame: ", frm);
		break;
	}

	/*
	 * We should release the frame
	 */
	FRM2SS(frm)->ss_eport->eport_release_frame(frm);
}

void
fcoet_port_event(fcoe_port_t *eport, uint32_t event)
{
	fcoet_soft_state_t *ss = EPORT2SS(eport);
	switch (event) {
	case FCOE_NOTIFY_EPORT_LINK_UP:
		if (eport->eport_mtu >= FCOE_MIN_MTU_SIZE) {
			ss->ss_fcp_data_payload_size =
			    FCOE_DEFAULT_FCP_DATA_PAYLOAD_SIZE;
		} else {
			ss->ss_fcp_data_payload_size =
			    FCOE_MIN_FCP_DATA_PAYLOAD_SIZE;
		}
		FCOET_LOG("fcoet_port_event", "LINK UP notified");
		mutex_enter(&ss->ss_watch_mutex);
		ss->ss_sol_flogi_state = SFS_FLOGI_INIT;
		cv_signal(&ss->ss_watch_cv);
		mutex_exit(&ss->ss_watch_mutex);
		break;
	case FCOE_NOTIFY_EPORT_LINK_DOWN:
		fct_handle_event(ss->ss_port,
		    FCT_EVENT_LINK_DOWN, 0, 0);
		/* Need clear up all other things */
		FCOET_LOG("fcoet_port_event", "LINK DOWN notified");
		ss->ss_sol_flogi_state = SFS_WAIT_LINKUP;
		eport->eport_flags &= ~EPORT_FLAG_FLOGI_DONE;
		break;
	default:
		break;
	}
}

/*
 * For unsolicited exchanges, FCoET is only responsible for allocation of
 * req_payload. FCT will allocate resp_payload after the exchange is
 * passed on.
 */
static fcoet_exchange_t *
fcoet_create_unsol_exchange(fcoe_frame_t *frm)
{
	uint8_t			 r_ctl;
	int			 cdb_size;
	fcoet_exchange_t	*xch;
	fct_cmd_t		*cmd;
	fcoe_fcp_cmnd_t		*ffc;
	uint32_t		task_expected_len = 0;
	uint16_t		xid;
	uint16_t		min_xid;

	r_ctl = FRM_R_CTL(frm);
	switch (r_ctl) {
	case 0x22:
		/*
		 * FCoET's unsolicited ELS
		 */
		cmd = (fct_cmd_t *)fct_alloc(FCT_STRUCT_CMD_RCVD_ELS,
		    GET_STRUCT_SIZE(fcoet_exchange_t) +
		    frm->frm_payload_size, 0);
		if (cmd == NULL) {
			FCOET_EXT_LOG(0, "can't get cmd");
			return (NULL);
		}
		break;

	case 0x06:
		/*
		 * FCoET's unsolicited SCSI cmd
		 */
		cdb_size = 16;	/* need improve later */
		cmd = fct_scsi_task_alloc(FRM2SS(frm)->ss_port, FCT_HANDLE_NONE,
		    FRM_S_ID(frm), frm->frm_payload, cdb_size,
		    STMF_TASK_EXT_NONE);
		if (cmd == NULL) {
			FCOET_EXT_LOG(0, "can't get fcp cmd");
			return (NULL);
		}
		ffc = (fcoe_fcp_cmnd_t *)frm->frm_payload;
		task_expected_len = FCOE_B2V_4(ffc->ffc_fcp_dl);
		break;

	default:
		FCOET_EXT_LOG(0, "unsupported R_CTL: %x", r_ctl);
		return (NULL);
	}

	/*
	 * xch initialization
	 */
	xch = CMD2XCH(cmd);
	xch->xch_oxid = FRM_OXID(frm);
	xch->xch_flags = 0;
	xch->xch_ss = FRM2SS(frm);
	xch->xch_cmd = cmd;
	xch->xch_current_seq = NULL;
	xch->xch_left_data_size = 0;
	if (task_expected_len) {
		bzero(xch->xch_dbufs, sizeof (xch->xch_dbufs));
		xch->xch_buf_map = 0;
	}
	xch->xch_start_time = ddi_get_lbolt();

	min_xid = xch->xch_ss->ss_min_unsol_rxid;
	do {
		xid = atomic_inc_16_nv(&xch->xch_ss->ss_next_unsol_rxid) %
		    (xch->xch_ss->ss_max_unsol_rxid -
		    min_xid + 1) + min_xid;
		/*
		 * An RX_ID of FFFFh indicates that the RX_ID is unassigned
		 */
		if (xid == 0xFFFF) {
			xid = min_xid;
		}
	} while (atomic_cas_ptr(&FRM2SS(frm)->ss_unsol_rxid_table[xid],
	    NULL, xch) != NULL);

	xch->xch_rxid = xid;
	xch->xch_sequence_no = 0;
	xch->xch_ref = 0;

	/*
	 * cmd initialization
	 */
	cmd->cmd_port = FRM2SS(frm)->ss_port;
	cmd->cmd_rp_handle = FCT_HANDLE_NONE;
	cmd->cmd_rportid = FRM_S_ID(frm);
	cmd->cmd_lportid = FRM_D_ID(frm);
	cmd->cmd_oxid = xch->xch_oxid;
	cmd->cmd_rxid = xch->xch_rxid;

	fcoet_init_tfm(frm, xch);

	return (xch);
}

int
fcoet_get_dbuf_idx_by_offset(fcoet_exchange_t *xch, int offset)
{
	/* All fcp data coming is handled in a single thread, no lock here */
	for (int i = 0; i < FCOET_MAX_DBUF_PER_XCHG; i++) {
		if (xch->xch_dbufs[i] != NULL &&
		    (offset >= xch->xch_dbufs[i]->db_relative_offset &&
		    offset < (xch->xch_dbufs[i]->db_relative_offset +
		    xch->xch_dbufs[i]->db_buf_size)))
			return (i);
	}
	return (-1);
}

int
fcoet_clear_unsol_exchange(fcoet_exchange_t *xch)
{
	uint16_t	rxid = xch->xch_rxid;

	if (atomic_cas_ptr(&xch->xch_ss->ss_unsol_rxid_table[rxid], xch, NULL)
	    == xch) {
		return (FCOE_SUCCESS);
	}

	FCOET_LOG(__func__, "xch/%p oxid/%x rxid/%x does not match "
	    "unsol_rxid_table, this should never happen.",
	    xch, xch->xch_oxid, xch->xch_rxid);
	return (FCOE_FAILURE);
}

void
fcoet_clear_sol_exchange(fcoet_exchange_t *xch)
{
	uint16_t	oxid = xch->xch_oxid;

	if (atomic_cas_ptr(&xch->xch_ss->ss_sol_oxid_table[oxid], xch, NULL)
	    == xch) {
		return;
	}

	FCOET_LOG(__func__, "xch/%p oxid/%x rxid/%x does not match "
	    "sol_oxid_table, this should never happen.",
	    xch, xch->xch_oxid, xch->xch_rxid);
}

static int
fcoet_process_sol_fcp_data(fcoe_frame_t *frm)
{
	fcoet_exchange_t	*xch = NULL;
	fct_status_t		 fc_st;
	uint32_t		 iof;
	uint16_t		 unsol_rxid;
	int			 dbuf_idx;
	stmf_data_buf_t		*dbuf;
	int			 data_offset;
	boolean_t		 dbuf_done;
	int			 offset = 0;
	mblk_t			*hdr_mp, *mp, *next;

	unsol_rxid = FRM_RXID(frm);
	xch = FRM2SS(frm)->ss_unsol_rxid_table[unsol_rxid];
	if (xch == NULL) {
		return (FCOE_FAILURE);
	}
	FCOET_BUSY_XCHG(xch);

	hdr_mp = (mblk_t *)frm->frm_netb;
	mp = hdr_mp->b_cont;

	if (SS_LRO_ENABLED(FRM2SS(frm)) && (mp != NULL)) {
		/* offload LRO */
		while (mp != NULL) {
			next = mp->b_cont;

			dbuf_idx = fcoet_get_dbuf_idx_by_offset(xch, offset);
			ASSERT(dbuf_idx != -1);
			dbuf = xch->xch_dbufs[dbuf_idx];
			ASSERT(dbuf);
			FCOET_COPY_TO_DBUF(dbuf, mp->b_rptr, offset, MBLKL(mp));
			offset += MBLKL(mp);
			mp = next;
		}
		if ((dbuf->db_flags & DB_LU_DATA_BUF) == 0) {
			/* We are asserting that all the frames come in order */
			dbuf_done = ((offset + frm->frm_payload_size) >=
			    (dbuf->db_relative_offset + dbuf->db_data_size));
		} else {
			FCOET_DB_SET_LEFT_SIZE(dbuf,
			    FCOET_DB_GET_LEFT_SIZE(dbuf) -
			    frm->frm_payload_size);
			dbuf_done = (FCOET_DB_GET_LEFT_SIZE(dbuf) <= 0);
		}

		xch->xch_left_data_size -= frm->frm_payload_size;
		if (xch->xch_left_data_size <= 0 || dbuf_done) {
			fc_st = FCT_SUCCESS;
			iof = 0;
			dbuf->db_xfer_status = fc_st;
			dbuf->db_flags |= DB_DONT_REUSE;
			xch->xch_dbufs[dbuf_idx] = NULL;
			atomic_and_8(&xch->xch_buf_map, ~(1 << dbuf_idx));
			fct_scsi_data_xfer_done(xch->xch_cmd, dbuf, iof);
		}
	} else {
		/*
		 * we will always have a buf waiting there
		 */
		data_offset = FRM_PARAM(frm);
		dbuf_idx = fcoet_get_dbuf_idx_by_offset(xch, data_offset);
		ASSERT(dbuf_idx != -1);
		dbuf = xch->xch_dbufs[dbuf_idx];
		ASSERT(dbuf);

		FCOET_COPY_TO_DBUF(dbuf, frm->frm_payload,
		    data_offset, frm->frm_payload_size);
		if ((dbuf->db_flags & DB_LU_DATA_BUF) == 0) {
			/* We are asserting that all the frames come in order */
			dbuf_done = ((data_offset + frm->frm_payload_size) >=
			    (dbuf->db_relative_offset + dbuf->db_data_size));
		} else {
			FCOET_DB_SET_LEFT_SIZE(dbuf,
			    FCOET_DB_GET_LEFT_SIZE(dbuf) -
			    frm->frm_payload_size);
			dbuf_done = (FCOET_DB_GET_LEFT_SIZE(dbuf) <= 0);
		}

		xch->xch_left_data_size -= frm->frm_payload_size;
		if (xch->xch_left_data_size <= 0 || dbuf_done) {
			fc_st = FCT_SUCCESS;
			iof = 0;
			dbuf->db_xfer_status = fc_st;
			dbuf->db_flags |= DB_DONT_REUSE;
			xch->xch_dbufs[dbuf_idx] = NULL;
			atomic_and_8(&xch->xch_buf_map, ~(1 << dbuf_idx));
			fct_scsi_data_xfer_done(xch->xch_cmd, dbuf, iof);
		}
	}

	FCOET_RELE_XCHG(xch);
	return (FCOE_SUCCESS);
}

static int
fcoet_process_unsol_fcp_cmd(fcoe_frame_t *frm)
{
	fcoet_exchange_t	*xch;
	fcoe_fcp_cmnd_t		*ffc;
	uint8_t			 tm;
	scsi_task_t		*task;

	xch = fcoet_create_unsol_exchange(frm);
	if (xch == NULL) {
		FCOET_LOG("fcoet_process_unsol_fcp_cmd", "can't get exchange");
		return (FCOE_FAILURE);
	}

	ffc = (fcoe_fcp_cmnd_t *)frm->frm_payload;
	task = XCH2TASK(xch);
	task->task_csn_size = 8;
	task->task_max_nbufs = FCOET_MAX_DBUF_PER_XCHG;
	task->task_cmd_seq_no = FCOE_B2V_1(ffc->ffc_ref_num);
	task->task_flags = FCOE_B2V_1(ffc->ffc_attribute) & 0x07;
	task->task_flags |=
	    (FCOE_B2V_1(ffc->ffc_addlen_rdwr) & 0x03) << 5;
	task->task_expected_xfer_length = FCOE_B2V_4(ffc->ffc_fcp_dl);

	tm = FCOE_B2V_1(ffc->ffc_management_flags);
	if (tm) {
		if (tm & BIT_1) {
			task->task_mgmt_function = TM_ABORT_TASK_SET;
		} else if (tm & BIT_2) {
			task->task_mgmt_function = TM_CLEAR_TASK_SET;
		} else if (tm & BIT_4) {
			task->task_mgmt_function = TM_LUN_RESET;
		} else if (tm & BIT_5) {
			task->task_mgmt_function = TM_TARGET_COLD_RESET;
		} else if (tm & BIT_6) {
			task->task_mgmt_function = TM_CLEAR_ACA;
		} else {
			task->task_mgmt_function = TM_ABORT_TASK;
		}
	}

	bcopy(ffc->ffc_cdb, task->task_cdb, 16);
	fct_post_rcvd_cmd(xch->xch_cmd, NULL);
	return (FCOE_SUCCESS);
}
/*
 * It must be from link
 * req_payload has been allocated when create_unsol_exchange
 */
static int
fcoet_process_unsol_els_req(fcoe_frame_t *frm)
{
	int			ret = FCOE_SUCCESS;
	fcoet_exchange_t	*xch;

	xch = fcoet_create_unsol_exchange(frm);
	ASSERT(xch);
	ASSERT(FRM_IS_LAST_FRAME(frm));

	/*
	 * For the reason of keeping symmetric, we do copy here as in
	 * process_sol_els instead of in create_unsol_exchange.
	 * req_payload depends on how to allocate buf in create_unsol_exchange
	 */
	XCH2ELS(xch)->els_req_alloc_size = 0;
	XCH2ELS(xch)->els_req_size = frm->frm_payload_size;
	XCH2ELS(xch)->els_req_payload =
	    GET_BYTE_OFFSET(xch, GET_STRUCT_SIZE(fcoet_exchange_t));
	bcopy(frm->frm_payload, XCH2ELS(xch)->els_req_payload,
	    XCH2ELS(xch)->els_req_size);
	if (XCH2ELS(xch)->els_req_payload[0] != ELS_OP_FLOGI) {
		/*
		 * Ensure LINK_UP event has been handled, or PLOIG has
		 * been processed by FCT, or else it will be discarded.
		 * It need more consideration later ???
		 */
		if ((XCH2ELS(xch)->els_req_payload[0] == ELS_OP_PLOGI) &&
		    (xch->xch_ss->ss_flags & SS_FLAG_DELAY_PLOGI)) {
			delay(STMF_SEC2TICK(1)/2);
		}

		if ((XCH2ELS(xch)->els_req_payload[0] == ELS_OP_PRLI) &&
		    (xch->xch_ss->ss_flags & SS_FLAG_DELAY_PLOGI)) {
			atomic_and_32(&xch->xch_ss->ss_flags,
			    ~SS_FLAG_DELAY_PLOGI);
			delay(STMF_SEC2TICK(1)/3);
		}
		fct_post_rcvd_cmd(xch->xch_cmd, NULL);
	} else {
		/*
		 * We always handle FLOGI internally
		 * Save dst mac address from FLOGI request to restore later
		 */
		bcopy((char *)frm->frm_hdr-22,
		    frm->frm_eport->eport_efh_dst, ETHERADDRL);
		ret = fcoet_process_unsol_flogi_req(xch);
	}
	return (ret);
}


/*
 * It must be from link, but could be incomplete because of network problems
 */
static int
fcoet_process_sol_els_rsp(fcoe_frame_t *frm)
{
	uint32_t		 actual_size;
	fct_status_t		 fc_st;
	uint32_t		 iof;
	uint16_t		 sol_oxid;
	fcoet_exchange_t	*xch = NULL;
	fct_els_t		*els = NULL;
	int			 ret = FCOE_SUCCESS;

	sol_oxid = FRM_OXID(frm);
	xch = FRM2SS(frm)->ss_sol_oxid_table[sol_oxid];
	if (xch == NULL) {
		return (FCOE_FAILURE);
	}
	FCOET_BUSY_XCHG(xch);

	if (xch != FRM2SS(frm)->ss_sol_flogi) {
		fcoet_clear_sol_exchange(xch);
	}

	fcoet_init_tfm(frm, xch);
	els = CMD2ELS(xch->xch_cmd);
	ASSERT(FRM_IS_LAST_FRAME(frm));
	actual_size = els->els_resp_size;
	if (actual_size > frm->frm_payload_size) {
		actual_size = frm->frm_payload_size;
	}

	els->els_resp_size = (uint16_t)actual_size;
	bcopy(frm->frm_payload, els->els_resp_payload, actual_size);

	if (xch->xch_ss->ss_sol_flogi == xch) {
		/*
		 * We handle FLOGI internally
		 */
		ret = fcoet_process_sol_flogi_rsp(frm);
		FCOET_RELE_XCHG(xch);
	} else {
		fc_st = FCT_SUCCESS;
		iof = FCT_IOF_FCA_DONE;
		FCOET_RELE_XCHG(xch);
		fct_send_cmd_done(xch->xch_cmd, fc_st, iof);
	}
	return (ret);
}

/*
 * It's still in the context of being aborted exchange, but FCT can't support
 * this scheme, so there are two fct_cmd_t that are bound with one exchange.
 */
static int
fcoet_process_unsol_abts_req(fcoe_frame_t *frm)
{
	fct_cmd_t		*cmd;
	fcoet_exchange_t	*xch = NULL;
	uint16_t		unsol_rxid;
	int			i = 0;

	FCOET_LOG("fcoet_process_unsol_abts_req", "ABTS: %x/%x",
	    FRM_OXID(frm), FRM_RXID(frm));
	unsol_rxid = FRM_RXID(frm);
	if (unsol_rxid != 0xffff) {
		xch = FRM2SS(frm)->ss_unsol_rxid_table[unsol_rxid];
	} else {
		for (i = FRM2SS(frm)->ss_min_unsol_rxid;
		    i <= FRM2SS(frm)->ss_max_unsol_rxid;
		    i++) {
			xch = FRM2SS(frm)->ss_unsol_rxid_table[i];
			if (xch == NULL)
				continue;
			if ((xch->xch_oxid == FRM_OXID(frm)) &&
			    (xch->xch_cmd->cmd_rportid == FRM_S_ID(frm))) {
				break;
			}
		}
	}

	if ((xch == NULL) || (i == FCOET_RXID_TABLE_SIZE)) {
		FCOET_LOG("fcoet_process_unsol_abts_req",
		    "can't find aborted exchange");
		return (FCOE_SUCCESS);
	}

	FCOET_BUSY_XCHG(xch);
	fcoet_init_tfm(frm, xch);

	if (!FRM_IS_LAST_FRAME(frm)) {
		FCOET_LOG("fcoet_process_unsol_abts_req",
		    "not supported this kind frame");
		FCOET_RELE_XCHG(xch);
		return (FCOE_FAILURE);
	}

	cmd = (fct_cmd_t *)fct_alloc(FCT_STRUCT_CMD_RCVD_ABTS, 0, 0);
	if (cmd == NULL) {
		FCOET_LOG("fcoet_process_unsol_abts_req",
		    "can't alloc fct_cmd_t");
		FCOET_RELE_XCHG(xch);
		return (FCOE_FAILURE);
	}

	atomic_or_32(&xch->xch_flags, XCH_FLAG_INI_ASKED_ABORT);
	cmd->cmd_fca_private = xch;
	cmd->cmd_port = xch->xch_cmd->cmd_port;
	cmd->cmd_rp_handle = xch->xch_cmd->cmd_rp_handle;
	cmd->cmd_rportid = xch->xch_cmd->cmd_rportid;
	cmd->cmd_lportid = xch->xch_cmd->cmd_lportid;
	cmd->cmd_oxid = xch->xch_cmd->cmd_oxid;
	cmd->cmd_rxid = xch->xch_cmd->cmd_rxid;
	fct_post_rcvd_cmd(cmd, NULL);
	FCOET_LOG("fcoet_process_unsol_abts_req",
	    "abts now: xch/%p, %x/%x, frm/%p, time/%d",
	    xch, xch->xch_oxid, xch->xch_rxid, frm,
	    ddi_get_lbolt() - xch->xch_start_time);

	FCOET_RELE_XCHG(xch);
	return (FCOE_SUCCESS);
}

static int
fcoet_process_sol_abts_acc(fcoe_frame_t *frm)
{
	fcoet_exchange_t	*xch = NULL;
	uint16_t		 sol_oxid;

	sol_oxid = FRM_OXID(frm);
	xch = atomic_swap_ptr(&FRM2SS(frm)->ss_sol_oxid_table[sol_oxid], NULL);
	if (xch == NULL) {
		/*
		 * So far ABTS for FLOGI might be removed from ss_sol_oxid_table
		 * in fcoet_watch_handle_sol_flogi, Will improve it later
		 */
		return (FCOE_SUCCESS);
	}

	if (!FRM_IS_LAST_FRAME(frm)) {
		FCOET_LOG("fcoet_process_sol_abts_acc",
		    "not supported this kind frame");
		FCOET_RELE_XCHG(xch);
		return (FCOE_FAILURE);
	}
	FCOET_LOG("fcoet_process_sol_abts_acc",
	    "ABTS received but there is nothing to do");
	return (FCOE_SUCCESS);
}

static int
fcoet_process_sol_abts_rjt(fcoe_frame_t *frm)
{
	fcoet_exchange_t	*xch	   = NULL;
	uint16_t		 sol_oxid;

	sol_oxid = FRM_OXID(frm);
	xch = atomic_swap_ptr(&FRM2SS(frm)->ss_sol_oxid_table[sol_oxid], NULL);
	if (xch == NULL) {
		/*
		 * So far ABTS for FLOGI might be removed from ss_sol_oxid_table
		 * in fcoet_watch_handle_sol_flogi, Will improve it later
		 */
		return (FCOE_SUCCESS);
	}

	if (!FRM_IS_LAST_FRAME(frm)) {
		FCOET_LOG("fcoet_process_sol_abts_rjt",
		    "not supported this kind frame");
		return (FCOE_FAILURE);
	}

	FCOET_LOG("fcoet_process_sol_abts_rjt",
	    "ABTS_RJT received rjt reason %x but there is nothing to do",
	    frm->frm_payload[1]);
	return (FCOE_SUCCESS);
}

static int
fcoet_process_unsol_ct_req(fcoe_frame_t *frm)
{
	/*
	 * If you want to implement virtual name server, or FC/ETH
	 * gateway, you can do it here
	 */
	if (!FRM_IS_LAST_FRAME(frm)) {
		FCOET_LOG("fcoet_process_unsol_ct_req",
		    "not supported this kind frame");
		return (FCOE_FAILURE);
	}

	FCOET_LOG("fcoet_process_unsol_ct_req",
	    "No support for unsolicited CT request");
	return (FCOE_SUCCESS);
}

static int
fcoet_process_sol_ct_rsp(fcoe_frame_t *frm)
{
	uint32_t		 actual_size;
	fct_status_t		 fc_st;
	uint32_t		 iof;
	fct_sol_ct_t		*ct  = NULL;
	fcoet_exchange_t	*xch = NULL;
	uint16_t		 sol_oxid;

	sol_oxid = FRM_OXID(frm);
	xch = atomic_swap_ptr(&FRM2SS(frm)->ss_sol_oxid_table[sol_oxid], NULL);
	if (xch == NULL) {
		return (FCOE_SUCCESS);
	}

	fcoet_init_tfm(frm, xch);

	ASSERT(FRM_IS_LAST_FRAME(frm));
	actual_size = CMD2ELS(xch->xch_cmd)->els_resp_size;
	if (actual_size > frm->frm_payload_size) {
		actual_size = frm->frm_payload_size;
	}
	ct = CMD2CT(xch->xch_cmd);
	ct->ct_resp_size = (uint16_t)actual_size;

	bcopy(frm->frm_payload,
	    CMD2CT(xch->xch_cmd)->ct_resp_payload, actual_size);

	fc_st = FCT_SUCCESS;
	iof = FCT_IOF_FCA_DONE;
	fct_send_cmd_done(xch->xch_cmd, fc_st, iof);

	return (FCOE_SUCCESS);
}

static int
fcoet_send_sol_fcp_data_done(fcoe_frame_t *frm)
{
	fcoet_exchange_t	*xch = FRM2TFM(frm)->tfm_xch;
	stmf_data_buf_t		*dbuf;
	int			dbuf_index;
	uint32_t		 iof;
	boolean_t		dbuf_done;

	dbuf_index = FRM2TFM(frm)->tfm_buf_idx;
	xch->xch_left_data_size -= frm->frm_payload_size;
	dbuf = xch->xch_dbufs[dbuf_index];
	ASSERT((dbuf) && (dbuf->db_flags & DB_DIRECTION_TO_RPORT));

	if ((dbuf->db_flags & DB_LU_DATA_BUF) == 0) {
		/*
		 * We decrease db_sglist_length only for READ-type commands.
		 * For INQUIRY, resid could be non-zero, then db_sglist_length
		 * will be useful.
		 */
		dbuf->db_sglist_length--;
		dbuf_done = (dbuf->db_sglist_length == 0);
	} else {
		FCOET_DB_SET_LEFT_SIZE(dbuf,
		    FCOET_DB_GET_LEFT_SIZE(dbuf) - frm->frm_payload_size);
		dbuf_done = (FCOET_DB_GET_LEFT_SIZE(dbuf) <= 0);
	}

	if (xch->xch_left_data_size <= 0 || dbuf_done) {
		iof = 0;

		dbuf->db_xfer_status = FCT_SUCCESS;
		dbuf->db_flags |= DB_DONT_REUSE;
		if (dbuf->db_flags & DB_SEND_STATUS_GOOD) {
			atomic_or_32(&xch->xch_flags,
			    XCH_FLAG_STATUS_SENT_BY_FCOET);
			if (fcoet_send_status(xch->xch_cmd) != FCT_SUCCESS) {
				atomic_and_32(&xch->xch_flags,
				    ~XCH_FLAG_STATUS_SENT_BY_FCOET);
				xch->xch_dbufs[dbuf_index] = NULL;
				atomic_and_8(&xch->xch_buf_map,
				    ~(1 << dbuf_index));
				return (FCOE_FAILURE);
			}
		} else {
			xch->xch_dbufs[dbuf_index] = NULL;
			atomic_and_8(&xch->xch_buf_map, ~(1 << dbuf_index));
			fct_scsi_data_xfer_done(xch->xch_cmd, dbuf, iof);
		}
	}
	FCOET_RELE_XCHG(xch);
	return (FCOE_SUCCESS);
}

static int
fcoet_send_fcp_status_done(fcoe_frame_t *frm)
{
	fcoet_exchange_t	*xch = FRM2TFM(frm)->tfm_xch;
	fct_status_t		 fc_st = FCT_SUCCESS;
	uint32_t		 iof = FCT_IOF_FCA_DONE;
	stmf_data_buf_t 	*dbuf = NULL;
	int			i;

	if (xch->xch_flags & XCH_FLAG_FCT_CALLED_ABORT) {
		FCOET_RELE_XCHG(xch);
		return (FCOE_SUCCESS);
	}

	if (xch->xch_flags & XCH_FLAG_STATUS_SENT_BY_FCOET) {
		/* Status sent out for the last dbuf */
		for (i = 0; i < FCOET_MAX_DBUF_PER_XCHG; i++) {
			if (xch->xch_dbufs[i] != NULL &&
			    xch->xch_dbufs[i]->db_flags & DB_SEND_STATUS_GOOD) {
				dbuf = xch->xch_dbufs[i];
				break;
			}
		}
	}

	if (fcoet_clear_unsol_exchange(xch) == FCOE_SUCCESS) {
		FCOET_RELE_XCHG(xch);
		if ((xch->xch_flags & XCH_FLAG_STATUS_SENT_BY_FCOET) == 0) {
			/* pure status xfer */
			fct_send_response_done(xch->xch_cmd, fc_st, iof);
		} else {
			if (dbuf == NULL) {
				FCOET_LOG("fcoet_send_fcp_status_done",
				    "Cannot find the corresponding dbuf with "
				    "DB_SEND_STATUS_GOOD set for exchange %p",
				    xch);
				/* fct will abort this cmd for such error */
				fct_send_response_done(xch->xch_cmd, fc_st, 0);
			} else {
				dbuf->db_flags |= DB_STATUS_GOOD_SENT;
				xch->xch_dbufs[i] = NULL;
				atomic_and_8(&xch->xch_buf_map, ~(1 << i));
				fct_scsi_data_xfer_done(xch->xch_cmd,
				    dbuf, iof);
			}
		}
	} else {
		/* Already cleared from unsol rxid table by abort */
		FCOET_RELE_XCHG(xch);
	}

	return (FCOE_SUCCESS);
}

/*
 * Solicited frames callback area
 */
static int
fcoet_send_unsol_els_rsp_done(fcoe_frame_t *frm)
{
	fcoet_exchange_t	*xch = FRM2TFM(frm)->tfm_xch;
	fct_status_t		 fc_st;
	uint32_t		 iof;

	FCOET_EXT_LOG("fcoet_send_unsol_els_rsp_done",
	    "frm/oxid/els: %p/%x/%x",
	    frm, FRM_OXID(frm), XCH2ELS(xch)->els_req_payload[0]);
	if (xch->xch_flags & XCH_FLAG_FCT_CALLED_ABORT) {
		FCOET_RELE_XCHG(xch);
		return (FCOE_SUCCESS);
	}

	if (fcoet_clear_unsol_exchange(xch) == FCOE_FAILURE) {
		FCOET_RELE_XCHG(xch);
		return (FCOE_SUCCESS);
	}

	FCOET_RELE_XCHG(xch);
	if (XCH2ELS(xch)->els_req_payload[0] != ELS_OP_FLOGI) {
		fc_st = FCT_SUCCESS;
		iof = FCT_IOF_FCA_DONE;
		fct_send_response_done(xch->xch_cmd, fc_st, iof);
	} else {
		/*
		 * We need update ss_link_info and flags.
		 */
		mutex_enter(&xch->xch_ss->ss_watch_mutex);
		xch->xch_ss->ss_link_info.portid =
		    xch->xch_cmd->cmd_lportid;
		xch->xch_ss->ss_link_info.port_topology =
		    PORT_TOPOLOGY_PT_TO_PT;
		if (frm->frm_eport->eport_link_speed == FCOE_PORT_SPEED_1G) {
			xch->xch_ss->ss_link_info.port_speed = PORT_SPEED_1G;
		} else if (frm->frm_eport->eport_link_speed ==
		    FCOE_PORT_SPEED_10G) {
			xch->xch_ss->ss_link_info.port_speed = PORT_SPEED_10G;
		}
		xch->xch_ss->ss_link_info.port_no_fct_flogi = 1;
		xch->xch_ss->ss_link_info.port_fca_flogi_done = 1;
		xch->xch_ss->ss_link_info.port_fct_flogi_done = 0;
		bcopy(XCH2ELS(xch)->els_req_payload + 20,
		    xch->xch_ss->ss_link_info.port_rpwwn, 8);
		bcopy(XCH2ELS(xch)->els_req_payload + 28,
		    xch->xch_ss->ss_link_info.port_rnwwn, 8);
		atomic_or_32(&xch->xch_ss->ss_flags,
		    SS_FLAG_UNSOL_FLOGI_DONE);
		atomic_or_32(&xch->xch_ss->ss_flags,
		    SS_FLAG_REPORT_TO_FCT);

		xch->xch_ss->ss_sol_flogi_state = SFS_FLOGI_ACC;
		mutex_exit(&xch->xch_ss->ss_watch_mutex);

		fct_free(xch->xch_cmd);
	}
	return (FCOE_SUCCESS);
}

/* ARGSUSED */
static int
fcoet_send_sol_els_req_done(fcoe_frame_t *frm)
{
	return (FCOE_SUCCESS);
}

/*
 * FCT have released relevant fct_cmd_t and fcoet_exchange_t now, so it's not
 * needed to notify FCT anything. Just do nothing.
 */
/* ARGSUSED */
static int
fcoet_send_unsol_bls_acc_done(fcoe_frame_t *frm)
{
	FCOET_LOG("fcoet_send_unsol_bls_acc_done",
	    "Unsolicited BA_ACC sent out and released ");

	return (FCOE_SUCCESS);
}

/* ARGSUSED */
static int
fcoet_send_unsol_bls_rjt_done(fcoe_frame_t *frm)
{
	FCOET_LOG("fcoet_send_unsol_bls_rjt_done",
	    "Unsolicited BA_RJT sent out and released");
	return (FCOE_SUCCESS);
}

/* ARGSUSED */
static int
fcoet_send_sol_bls_req_done(fcoe_frame_t *frm)
{
	FCOET_LOG("fcoet_send_sol_bls_req_done",
	    "Soclited ABTS was sent out and released");
	return (FCOE_SUCCESS);
}

/* ARGSUSED */
static int
fcoet_send_sol_ct_req_done(fcoe_frame_t *frm)
{
	FCOET_LOG("fcoet_send_sol_ct_req_done",
	    "CT request was sent out and released");
	return (FCOE_SUCCESS);
}

/*
 * FCoET can only interpret solicited and unsolicited FLOGI, all the other
 * ELS/CT/FCP should be passed up to FCT.
 */
static int
fcoet_process_unsol_flogi_req(fcoet_exchange_t *xch)
{
	fcoe_frame_t *frm;

	atomic_or_32(&xch->xch_ss->ss_flags, SS_FLAG_DELAY_PLOGI);

	/*
	 * In spec, common service parameter should indicate if it's from
	 * N-port or F-port, but the initial intel implementation is not
	 * spec-compliant, so we use eport_flags to workaround the problem
	 */
	if (!(xch->xch_ss->ss_eport->eport_flags & EPORT_FLAG_IS_DIRECT_P2P)) {
		/*
		 * The topology is switch P2P, so there's no need to respond
		 * to this FLOGI
		 */
		FCOET_LOG("fcoet_process_unsol_flogi_req",
		    "skip FLOGI, since we are in switch topology");
		return (FCOE_SUCCESS);
	}

	/*
	 * Send ACC according to the spec.
	 */
	frm = xch->xch_ss->ss_eport->eport_alloc_frame(xch->xch_ss->ss_eport,
	    FLOGI_ACC_PAYLOAD_SIZE + FCFH_SIZE, 0);
	if (frm == NULL) {
		ASSERT(0);
		return (FCOE_FAILURE);
	} else {
		fcoet_init_tfm(frm, xch);
		bzero(frm->frm_payload, frm->frm_payload_size);
	}

	FFM_R_CTL(0x23, frm);
	FRM2TFM(frm)->tfm_rctl = 0x23;
	FFM_TYPE(0x01, frm);
	FFM_F_CTL(0x980000, frm);
	FFM_OXID(xch->xch_oxid, frm);
	FFM_RXID(xch->xch_rxid, frm);
	FFM_S_ID(0xFFFFFE, frm);

	/*
	 * ACC
	 */
	frm->frm_payload[0] = 0x02;

	/*
	 * Common Svc Parameters
	 */
	frm->frm_payload[4] = 0x20;
	frm->frm_payload[5] = 0x20;
	frm->frm_payload[7] = 0x0A;
	frm->frm_payload[10] = 0x05;
	frm->frm_payload[11] = 0xAC;
	bcopy(xch->xch_ss->ss_eport->eport_portwwn, frm->frm_payload + 20, 8);
	bcopy(xch->xch_ss->ss_eport->eport_nodewwn, frm->frm_payload + 28, 8);

	/*
	 * Class3 Svc Parameters
	 */
	frm->frm_payload[68] = 0x88;

	/*
	 * Send FLOGI ACC out
	 * After this, we should never use the exchange, because it could
	 * have been released. Please pay attention to other similiar cases.
	 */
	xch->xch_ss->ss_eport->eport_tx_frame(frm);
	return (FCOE_SUCCESS);
}

static int
fcoet_process_sol_flogi_rsp(fcoe_frame_t *frm)
{
	int ret = FCOE_SUCCESS;
	fcoet_exchange_t	*xch = FRM2TFM(frm)->tfm_xch;
	fct_els_t		*els = CMD2ELS(xch->xch_cmd);
	fcoet_soft_state_t	*ss = FRM2SS(frm);

	if (els->els_resp_payload[0] == ELS_OP_ACC) {
		/*
		 * We need always update ss_link_info and flags for solicited
		 * FLOGI, because someone has assigned address for you. The
		 * initial intel implementation will always assign address for
		 * you even you are in back-to-back mode (direct P2P).
		 */
		mutex_enter(&ss->ss_watch_mutex);

		if ((!(ss->ss_flags & SS_FLAG_PORT_DISABLED)) &&
		    (ss->ss_flags & SS_FLAG_UNSOL_FLOGI_DONE) &&
		    (ss->ss_sol_flogi_state == SFS_FLOGI_ACC)) {
			frm->frm_eport->eport_flags |= EPORT_FLAG_FLOGI_DONE;
			mutex_exit(&ss->ss_watch_mutex);
			FCOET_LOG("fcoet_process_sol_flogi_rsp",
			    "unsolicited FLOGI has finished.");
			return (ret);
		}

		if (ss->ss_flags & SS_FLAG_PORT_DISABLED ||
		    (ss->ss_sol_flogi_state != SFS_FLOGI_INIT &&
		    ss->ss_sol_flogi_state != SFS_FLOGI_CHECK_TIMEOUT &&
		    ss->ss_sol_flogi_state != SFS_ABTS_INIT)) {
			/*
			 * The status is not correct, this response may be
			 * obsolete.
			 */
			mutex_exit(&ss->ss_watch_mutex);
			FCOET_LOG("fcoet_process_sol_flogi_rsp",
			    "FLOGI response is obsolete");
			return (FCOE_FAILURE);
		}
		if (xch->xch_flags & XCH_FLAG_NONFCP_REQ_SENT) {
			xch->xch_cmd->cmd_lportid = FRM_D_ID(frm);
			xch->xch_ss->ss_link_info.portid =
			    xch->xch_cmd->cmd_lportid;
			/*
			 * Check the bit 28 in 3rd word of the payload
			 *  in common service parameters to know the
			 * remote port is F_PORT or N_PORT
			 */
			if (els->els_resp_payload[8] & 0x10) {
				uint8_t src_addr[ETHERADDRL];
				frm->frm_eport->eport_flags &=
				    ~EPORT_FLAG_IS_DIRECT_P2P;
				FCOE_SET_DEFAULT_OUI(src_addr);
				bcopy(frm->frm_hdr->hdr_d_id, src_addr + 3, 3);
				bcopy((char *)frm->frm_hdr-22,
				    frm->frm_eport->eport_efh_dst,
				    ETHERADDRL);
				frm->frm_eport->eport_set_mac_address(
				    frm->frm_eport, src_addr, B_TRUE);
				xch->xch_ss->ss_link_info.port_topology =
				    PORT_TOPOLOGY_FABRIC_PT_TO_PT;
			} else {
				xch->xch_ss->ss_link_info.port_topology =
				    PORT_TOPOLOGY_PT_TO_PT;
			}

			frm->frm_eport->eport_flags |= EPORT_FLAG_FLOGI_DONE;

			xch->xch_ss->ss_link_info.port_speed = PORT_SPEED_10G;
			xch->xch_ss->ss_link_info.port_no_fct_flogi = 1;
			xch->xch_ss->ss_link_info.port_fca_flogi_done = 1;
			xch->xch_ss->ss_link_info.port_fct_flogi_done = 0;
			xch->xch_ss->ss_sol_flogi_state = SFS_FLOGI_ACC;
			cv_signal(&xch->xch_ss->ss_watch_cv);
			FCOET_LOG("fcoet_process_sol_flogi_rsp",
			    "FLOGI is accecpted");
		} else {
			FCOET_LOG("fcoet_process_sol_flogi_rsp",
			    "FLOGI xch_flags/%x", xch->xch_flags);
			ret = FCOE_FAILURE;
		}
		mutex_exit(&ss->ss_watch_mutex);
	} else {
		FCOET_LOG("fcoet_process_sol_flogi_rsp", "FLOGI is rejected");
		ret = FCOE_FAILURE;
	}
	return (ret);
}
