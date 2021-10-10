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
 * This file defines interfaces between fcoe and fct driver.
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

/*
 * COMSTAR header files
 */
#include <sys/stmf_defines.h>
#include <sys/fct_defines.h>
#include <sys/stmf.h>
#include <sys/portif.h>
#include <sys/fct.h>

/*
 * FCoE hader files
 */
#include <sys/fcoe/fcoe_common.h>

/*
 * Driver's own header files
 */
#include "fcoet.h"
#include "fcoet_fc.h"
#include "fcoet_eth.h"

/*
 * function forward declaration
 */
static fct_status_t fcoet_fill_plogi_req(fct_local_port_t *port,
    fct_remote_port_t *rp, fct_cmd_t *login);
static fct_status_t fcoet_fill_plogi_resp(fct_local_port_t *port,
    fct_remote_port_t *rp, fct_cmd_t *login);
static fct_status_t fcoet_send_sol_els(fct_cmd_t *cmd);
static fct_status_t fcoet_send_sol_ct(fct_cmd_t *cmd);
static fct_status_t fcoet_send_good_status(fct_cmd_t *cmd);
static fct_status_t fcoet_send_els_response(fct_cmd_t *cmd);
static fct_status_t fcoet_send_abts_response(fct_cmd_t *cmd, uint32_t flags);
static fct_status_t fcoet_logo_fabric(fcoet_soft_state_t *ss);

/* This should be synchronous with FCOET_MAX_DBUF_PER_XCHG */
static uint8_t fcoet_first_zero[] =  {0, 0xff};

/*
 * Return the lower link information
 */
fct_status_t
fcoet_get_link_info(fct_local_port_t *port, fct_link_info_t *li)
{
	bcopy(&PORT2SS(port)->ss_link_info, li, sizeof (fct_link_info_t));
	return (FCT_SUCCESS);
}

/*
 * FCT will call this, when it wants to send PLOGI or has received PLOGI.
 */
fct_status_t
fcoet_register_remote_port(fct_local_port_t *port, fct_remote_port_t *rp,
    fct_cmd_t *login)
{
	uint16_t	handle;
	fct_status_t	ret;

	switch (rp->rp_id) {
	case 0xFFFFFC:
		handle = 0x7FC;
		break;

	case 0xFFFFFD:
		handle = 0x7FD;
		break;

	case 0xFFFFFE:
		handle = 0x7FE;
		break;

	case 0xFFFFFF:
		handle = 0x7FF;
		break;

	default:
		/*
		 * For not well-known address, we let FCT to select one.
		 */
		handle = FCT_HANDLE_NONE;
		break;
	}

	rp->rp_handle = handle;
	if (login->cmd_type == FCT_CMD_SOL_ELS) {
		ret = fcoet_fill_plogi_req(port, rp, login);
	} else {
		ret = fcoet_fill_plogi_resp(port, rp, login);
	}

	return (ret);
}

/*
 * FCT will call this to say "FCoET can release resources with this RP now."
 */
/* ARGSUSED */
fct_status_t
fcoet_deregister_remote_port(fct_local_port_t *port, fct_remote_port_t *rp)
{
	fcoet_soft_state_t	*this_ss = PORT2SS(port);

	this_ss->ss_rport_dereg_state = 0;
	this_ss->ss_rportid_in_dereg = 0;
	return (FCT_SUCCESS);
}

fct_status_t
fcoet_send_cmd(fct_cmd_t *cmd)
{
	if (cmd->cmd_type == FCT_CMD_SOL_ELS) {
		return (fcoet_send_sol_els(cmd));
	} else if (cmd->cmd_type == FCT_CMD_SOL_CT) {
		return (fcoet_send_sol_ct(cmd));
	}

	return (FCT_FAILURE);
}

/*
 * SCSI response phase
 * ELS_ACC/ELS_RJT
 */
fct_status_t
fcoet_send_cmd_response(fct_cmd_t *cmd, uint32_t ioflags)
{
	char	info[FCT_INFO_LEN];

	if (cmd->cmd_type == FCT_CMD_FCP_XCHG) {
		if (ioflags & FCT_IOF_FORCE_FCA_DONE) {
			goto send_cmd_rsp_error;
		} else {
			return (fcoet_send_status(cmd));
		}
	}

	if (cmd->cmd_type == FCT_CMD_RCVD_ELS) {
		if (ioflags & FCT_IOF_FORCE_FCA_DONE) {
			goto send_cmd_rsp_error;
		} else {
			return (fcoet_send_els_response(cmd));
		}
	}

	if (ioflags & FCT_IOF_FORCE_FCA_DONE) {
		cmd->cmd_handle = 0;
	}

	if (cmd->cmd_type == FCT_CMD_RCVD_ABTS) {
		return (fcoet_send_abts_response(cmd, 0));
	} else {
		ASSERT(0);
		return (FCT_FAILURE);
	}

send_cmd_rsp_error:
	(void) snprintf(info, sizeof (info), "fcoet_send_cmd_response: can not "
	    "handle FCT_IOF_FORCE_FCA_DONE for cmd %p, ioflags-%x", (void *)cmd,
	    ioflags);
	(void) fct_port_shutdown(CMD2SS(cmd)->ss_port,
	    STMF_RFLAG_FATAL_ERROR | STMF_RFLAG_RESET, info);
	return (FCT_FAILURE);
}

/*
 * It's for read/write (xfer_rdy)
 */
/* ARGSUSED */
fct_status_t
fcoet_xfer_scsi_data(fct_cmd_t *cmd, stmf_data_buf_t *dbuf, uint32_t ioflags)
{
	fcoe_frame_t	*frm;
	int		 idx;
	int		 dbuf_idx;
	int		 frm_num;
	int		 data_size;
	int		 left_size;
	int		 offset;
	fcoet_exchange_t *xch = CMD2XCH(cmd);
	boolean_t	 b_last_dbuf = B_FALSE;
	uint8_t		 buf_map;
	boolean_t	lso_enabled = B_FALSE;
	uint32_t	fcp_payload_size;
	uint32_t	max_lso_size;

	buf_map = xch->xch_buf_map;
	dbuf_idx = fcoet_first_zero[buf_map];
	if (dbuf_idx == 0xff) {
		return (FCT_FAILURE);
	}
	atomic_or_8(&xch->xch_buf_map, 1 << dbuf_idx);

	xch->xch_dbufs[dbuf_idx] = dbuf;

	left_size = (int)dbuf->db_data_size;
	if (dbuf->db_relative_offset == 0)
		xch->xch_left_data_size =
		    XCH2TASK(xch)->task_expected_xfer_length;

	fcp_payload_size = CMD2SS(cmd)->ss_fcp_data_payload_size;

	if (dbuf->db_flags & DB_DIRECTION_FROM_RPORT) {
		/*
		 * If it's write type command, we need send xfer_rdy now
		 * We may need to consider bidirectional command later
		 */
		frm = CMD2SS(cmd)->ss_eport->eport_alloc_frame(
		    CMD2SS(cmd)->ss_eport, sizeof (fcoe_fcp_xfer_rdy_t) +
		    FCFH_SIZE, NULL);
		if (frm == NULL) {
			ASSERT(0);
			return (FCT_FAILURE);
		} else {
			fcoet_init_tfm(frm, CMD2XCH(cmd));
			bzero(frm->frm_payload, frm->frm_payload_size);
		}

		if (SS_LRO_ENABLED(CMD2SS(cmd)) &&
		    (dbuf->db_data_size > fcp_payload_size)) {
			if (CMD2SS(cmd)->ss_eport->eport_fcoe_setup_lro(
			    CMD2SS(cmd)->ss_eport,
			    MAC_FCOE_XCHG_ON_TARGET,
			    xch->xch_rxid,
			    dbuf->db_data_size) == -1) {
				/*
				 * Fall back to normal Write,
				 * no need to change back the RX_ID
				 */
				FCOET_LOG("fcoet_xfer_scsi_data",
				    "setup_lro failed cmd/%p, xch/%p, %x/%x",
				    cmd, xch, xch->xch_oxid, xch->xch_rxid);
			} else {
				atomic_or_32(&xch->xch_flags, XCH_FLAG_LRO);
			}
		}

		FFM_R_CTL(0x05, frm);
		FRM2TFM(frm)->tfm_rctl = 0x05;
		FFM_TYPE(0x08, frm);
		FFM_F_CTL(0x890000, frm);
		FFM_OXID(cmd->cmd_oxid, frm);
		FFM_RXID(cmd->cmd_rxid, frm);
		FFM_S_ID(cmd->cmd_lportid, frm);
		FFM_D_ID(cmd->cmd_rportid, frm);
		FCOE_V2B_4(dbuf->db_relative_offset, frm->frm_payload);
		FCOE_V2B_4(dbuf->db_data_size, frm->frm_payload + 4);
		CMD2SS(cmd)->ss_eport->eport_tx_frame(frm);

		return (FCT_SUCCESS);
	}

	if (dbuf->db_flags & DB_SEND_STATUS_GOOD) {
		b_last_dbuf = B_TRUE;
	} else if (dbuf->db_flags & DB_LU_DATA_BUF) {
		b_last_dbuf =
		    (dbuf->db_relative_offset + dbuf->db_data_size >=
		    XCH2TASK(xch)->task_expected_xfer_length);
	} else {
		/* Use data size does not always get the correct result */
		b_last_dbuf = (dbuf->db_relative_offset/FCOET_MAX_DBUF_LEN ==
		    (XCH2TASK(xch)->task_expected_xfer_length +
		    FCOET_MAX_DBUF_LEN - 1)/FCOET_MAX_DBUF_LEN - 1);
	}
	/*
	 * It's time to transfer READ data to remote side
	 */
	/*
	 * offload
	 */
	if (SS_LSO_ENABLED(CMD2SS(cmd)) &&
	    dbuf->db_data_size > fcp_payload_size &&
	    (dbuf->db_flags & DB_LU_DATA_BUF)) {
		lso_enabled = B_TRUE;
	}

	max_lso_size = CMD2SS(cmd)->ss_eport->offload_capab.fcoe_max_lso_size;
	if (lso_enabled == B_TRUE) {
		frm_num = (dbuf->db_data_size + max_lso_size -
		    1) / max_lso_size;
	} else {
		frm_num = (dbuf->db_data_size + fcp_payload_size -
		    1) / fcp_payload_size;
	}

	offset = dbuf->db_relative_offset;
	for (idx = 0; idx < frm_num; idx++) {
		if (idx == (frm_num -1)) {
			data_size = P2ROUNDUP(left_size, 4);
		} else if (lso_enabled == B_TRUE) {
			data_size = max_lso_size;
		} else {
			data_size = fcp_payload_size;
		}

		if (dbuf->db_flags & DB_LU_DATA_BUF) {
			frm = CMD2SS(cmd)->ss_eport->eport_alloc_frame(
			    CMD2SS(cmd)->ss_eport, data_size + FCFH_SIZE,
			    NULL);
			if (frm != NULL) {
				FCOET_COPY_FROM_DBUF(dbuf,
				    frm->frm_payload, offset, data_size);
			}
		} else {
			frm = CMD2SS(cmd)->ss_eport->eport_alloc_frame(
			    CMD2SS(cmd)->ss_eport, data_size + FCFH_SIZE,
			    FCOET_GET_NETB(dbuf, idx));
		}

		if (frm == NULL) {
			ASSERT(0);
			return (FCT_FAILURE);
		} else {
			fcoet_init_tfm(frm, CMD2XCH(cmd));
			/*
			 * lock the xchg to avoid being released (by abort)
			 * after sent out and before release
			 */
			FCOET_BUSY_XCHG(CMD2XCH(cmd));
		}

		FFM_R_CTL(0x01, frm);
		FRM2TFM(frm)->tfm_rctl = 0x01;
		FRM2TFM(frm)->tfm_buf_idx = (uint8_t)dbuf_idx;

		FFM_TYPE(0x08, frm);
		if (idx != frm_num - 1 || !b_last_dbuf) {
			FFM_F_CTL(0x800008, frm);
		} else {
			FFM_F_CTL(0x880008 | (data_size - left_size), frm);
		}

		FFM_OXID(cmd->cmd_oxid, frm);
		FFM_RXID(cmd->cmd_rxid, frm);
		FFM_S_ID(cmd->cmd_lportid, frm);
		FFM_D_ID(cmd->cmd_rportid, frm);
		FFM_SEQ_CNT(xch->xch_sequence_no, frm);
		atomic_add_16(&xch->xch_sequence_no,
		    (data_size + fcp_payload_size - 1) / fcp_payload_size);
		FFM_PARAM(offset, frm);
		offset += data_size;
		left_size -= data_size;

		/*
		 * Disassociate netbs which will be freed by NIC driver
		 */
		if ((dbuf->db_flags & DB_LU_DATA_BUF) == 0) {
			FCOET_SET_NETB(dbuf, idx, NULL);
		}

		CMD2SS(cmd)->ss_eport->eport_tx_frame(frm);
	}

	return (FCT_SUCCESS);
}

void
fcoet_fill_abts_acc(fct_cmd_t *cmd)
{
	fct_rcvd_abts_t *abts = (fct_rcvd_abts_t *)cmd->cmd_specific;
	uint8_t *p;

	abts->abts_resp_rctl = BLS_OP_BA_ACC;
	p = abts->abts_resp_payload;
	bzero(p, 12);
	FCOE_V2B_2(cmd->cmd_oxid, p+4);
	FCOE_V2B_2(cmd->cmd_rxid, p+6);
	p[10] = p[11] = 0xff;
}

fct_status_t
fcoet_abort_cmd(struct fct_local_port *port, fct_cmd_t *cmd, uint32_t flags)
{
	fcoet_soft_state_t	*this_ss = PORT2SS(port);
	fct_status_t		 fct_ret = FCT_SUCCESS;

	FCOET_LOG(__func__, "cmd=%p, xch=%p, %x/%x, cmd_specific=%p, time/%d",
	    cmd, cmd->cmd_fca_private, cmd->cmd_oxid, cmd->cmd_rxid,
	    cmd->cmd_specific, ddi_get_lbolt() - CMD2XCH(cmd)->xch_start_time);
	switch (cmd->cmd_type) {
	case FCT_CMD_RCVD_ABTS:
		fcoet_fill_abts_acc(cmd);
		fct_ret = fcoet_send_abts_response(cmd, 1);
		break;
	case FCT_CMD_FCP_XCHG:
	case FCT_CMD_RCVD_ELS:
		if (CMD2XCH(cmd)->xch_flags & XCH_FLAG_FCT_CALLED_ABORT) {
			break;
		}

		atomic_or_32(&CMD2XCH(cmd)->xch_flags,
		    XCH_FLAG_FCT_CALLED_ABORT);
		(void) fcoet_clear_unsol_exchange(CMD2XCH(cmd));
		if (!(flags & FCT_IOF_FORCE_FCA_DONE)) {
			mutex_enter(&this_ss->ss_watch_mutex);
			CMD2XCH(cmd)->xch_start_time = ddi_get_lbolt();
			list_insert_tail(&this_ss->ss_abort_xchg_list,
			    CMD2XCH(cmd));
			cv_signal(&this_ss->ss_watch_cv);
			mutex_exit(&this_ss->ss_watch_mutex);
		}
		break;

	case FCT_CMD_SOL_ELS:
	case FCT_CMD_SOL_CT:
		if (CMD2XCH(cmd)->xch_flags & XCH_FLAG_FCT_CALLED_ABORT) {
			break;
		}

		atomic_or_32(&CMD2XCH(cmd)->xch_flags,
		    XCH_FLAG_FCT_CALLED_ABORT);
		fcoet_clear_sol_exchange(CMD2XCH(cmd));

		if (!(flags & FCT_IOF_FORCE_FCA_DONE)) {
			mutex_enter(&this_ss->ss_watch_mutex);
			CMD2XCH(cmd)->xch_start_time = ddi_get_lbolt();
			cv_signal(&this_ss->ss_watch_cv);
			list_insert_tail(&this_ss->ss_abort_xchg_list,
			    CMD2XCH(cmd));
			mutex_exit(&this_ss->ss_watch_mutex);
		}

		break;

	default:
		ASSERT(0);
		break;
	}

	if ((flags & FCT_IOF_FORCE_FCA_DONE) &&
	    (cmd->cmd_type != FCT_CMD_FCP_XCHG)) {
		cmd->cmd_handle = 0;
	}

	return (fct_ret);
}

/* ARGSUSED */
fct_status_t
fcoet_do_flogi(fct_local_port_t *port, fct_flogi_xchg_t *fx)
{
	cmn_err(CE_WARN, "FLOGI requested (not supported)");
	return (FCT_FAILURE);
}

void
fcoet_send_sol_flogi(fcoet_soft_state_t *ss)
{
	fcoet_exchange_t	*xch;
	fct_cmd_t		*cmd;
	fct_els_t		*els;
	fcoe_frame_t		*frm;
	uint16_t		xid;

	/*
	 * FCT will initialize fct_cmd_t
	 * Initialize fcoet_exchange
	 */
	cmd = (fct_cmd_t *)fct_alloc(FCT_STRUCT_CMD_SOL_ELS,
	    sizeof (fcoet_exchange_t), 0);
	xch = CMD2XCH(cmd);
	els = CMD2ELS(cmd);

	do {
		xid = atomic_inc_16_nv(&ss->ss_next_sol_oxid) %
		    FCOET_OXID_TABLE_SIZE;
	} while (atomic_cas_ptr(&ss->ss_sol_oxid_table[xid], NULL, xch)
	    != NULL);

	xch->xch_oxid = xid;
	xch->xch_rxid = 0xFFFF;
	xch->xch_flags = 0;
	xch->xch_ss = ss;
	xch->xch_cmd = cmd;
	xch->xch_current_seq = NULL;
	xch->xch_start_time = ddi_get_lbolt();

	/*
	 * Keep it to compare with response
	 */
	ss->ss_sol_flogi = xch;
	els->els_resp_alloc_size = 116;
	els->els_resp_size = 116;
	els->els_resp_payload = (uint8_t *)
	    kmem_zalloc(els->els_resp_size, KM_SLEEP);
	atomic_or_32(&ss->ss_flags, SS_FLAG_DELAY_PLOGI);

	/*
	 * FCoE will initialize fcoe_frame_t
	 */
	frm = ss->ss_eport->eport_alloc_frame(ss->ss_eport,
	    FLOGI_REQ_PAYLOAD_SIZE + FCFH_SIZE, NULL);
	if (frm == NULL) {
		ASSERT(0);
		return;
	} else {
		fcoet_init_tfm(frm, xch);
		bzero(frm->frm_payload, frm->frm_payload_size);
	}

	FFM_R_CTL(0x22, frm);
	FRM2TFM(frm)->tfm_rctl = 0x22;
	FFM_TYPE(0x01, frm);
	FFM_F_CTL(0x290000, frm);
	FFM_OXID(xch->xch_oxid, frm);
	FFM_RXID(xch->xch_rxid, frm);
	FFM_D_ID(0xfffffe, frm);
	frm->frm_payload[0] = ELS_OP_FLOGI;
	/* Common Service Parameters */
	frm->frm_payload[4] = 0x20;
	frm->frm_payload[5] = 0x08;
	frm->frm_payload[6] = 0x0;
	frm->frm_payload[7] = 0x03;
	/* N_PORT */
	frm->frm_payload[8] = 0x88;
	frm->frm_payload[9] = 0x00;
	frm->frm_payload[10] = 0x08;
	frm->frm_payload[11] = 0x0;
	frm->frm_payload[12] = 0x0;
	frm->frm_payload[13] = 0xff;
	frm->frm_payload[14] = 0x0;
	frm->frm_payload[15] = 0x03;
	frm->frm_payload[16] = 0x0;
	frm->frm_payload[17] = 0x0;
	frm->frm_payload[18] = 0x07;
	frm->frm_payload[19] = 0xd0;
	/* PWWN and NWWN */
	frm->frm_payload[20] = 0x0;
	bcopy(ss->ss_eport->eport_portwwn, frm->frm_payload+20, 8);
	bcopy(ss->ss_eport->eport_nodewwn, frm->frm_payload+28, 8);
	/* Class 3 Service Parameters */
	frm->frm_payload[68] = 0x88;
	frm->frm_payload[74] = 0x08;
	frm->frm_payload[77] = 0xff;

	ss->ss_eport->eport_tx_frame(frm);
	xch->xch_flags |= XCH_FLAG_NONFCP_REQ_SENT;
}

/*
 * This is for solicited FLOGI only
 */
void
fcoet_send_sol_abts(fcoet_exchange_t *xch)
{
	fcoe_frame_t		*frm;
	fcoet_soft_state_t	*ss = xch->xch_ss;

	/*
	 * FCoE will initialize fcoe_frame_t
	 * ABTS has no payload
	 */
	frm = ss->ss_eport->eport_alloc_frame(ss->ss_eport,
	    FCFH_SIZE, NULL);
	if (frm == NULL) {
		ASSERT(0);
		return;
	} else {
		fcoet_init_tfm(frm, xch);
		frm->frm_payload = NULL;
	}

	FFM_R_CTL(0x81, frm);
	FRM2TFM(frm)->tfm_rctl = 0x81;
	FFM_F_CTL(0x090000, frm);
	FFM_OXID(xch->xch_oxid, frm);
	FFM_RXID(xch->xch_rxid, frm);
	FFM_D_ID(0xfffffe, frm);
	FFM_SEQ_CNT(xch->xch_sequence_no, frm);
	xch->xch_start_time = ddi_get_lbolt();

	ss->ss_eport->eport_tx_frame(frm);
}

void
fcoet_ctl(struct fct_local_port *port, int cmd, void *arg)
{
	stmf_change_status_t		 st;
	stmf_state_change_info_t	*ssci = (stmf_state_change_info_t *)arg;
	fcoet_soft_state_t		*this_ss = PORT2SS(port);

	st.st_completion_status = FCT_SUCCESS;
	st.st_additional_info = NULL;

	switch (cmd) {
	case FCT_CMD_PORT_ONLINE:
		if (this_ss->ss_state == FCT_STATE_ONLINE)
			st.st_completion_status = STMF_ALREADY;
		else if (this_ss->ss_state != FCT_STATE_OFFLINE)
			st.st_completion_status = FCT_FAILURE;
		if (st.st_completion_status == FCT_SUCCESS) {
			this_ss->ss_state = FCT_STATE_ONLINING;
			this_ss->ss_state_not_acked = 1;
			st.st_completion_status = fcoet_enable_port(this_ss);
			if (st.st_completion_status != STMF_SUCCESS) {
				this_ss->ss_state = FCT_STATE_OFFLINE;
				this_ss->ss_state_not_acked = 0;
			} else {
				this_ss->ss_state = FCT_STATE_ONLINE;
			}
		}
		fct_ctl(port->port_lport, FCT_CMD_PORT_ONLINE_COMPLETE, &st);
		this_ss->ss_change_state_flags = 0;
		break;

	case FCT_CMD_PORT_OFFLINE:
		if (this_ss->ss_state == FCT_STATE_OFFLINE) {
			st.st_completion_status = STMF_ALREADY;
		} else if (this_ss->ss_state != FCT_STATE_ONLINE) {
			st.st_completion_status = FCT_FAILURE;
		}
		if (st.st_completion_status == FCT_SUCCESS) {
			this_ss->ss_state = FCT_STATE_OFFLINING;
			this_ss->ss_state_not_acked = 1;
			this_ss->ss_change_state_flags = ssci->st_rflags;
			st.st_completion_status = fcoet_disable_port(this_ss);
			if (st.st_completion_status != STMF_SUCCESS) {
				this_ss->ss_state = FCT_STATE_ONLINE;
				this_ss->ss_state_not_acked = 0;
			} else {
				this_ss->ss_eport->eport_flags &=
				    ~EPORT_FLAG_FLOGI_DONE;
				this_ss->ss_state = FCT_STATE_OFFLINE;
			}
		}
		/*
		 * Notify the watchdog to do clear work
		 */
		mutex_enter(&this_ss->ss_watch_mutex);
		cv_signal(&this_ss->ss_watch_cv);
		mutex_exit(&this_ss->ss_watch_mutex);
		fct_ctl(port->port_lport, FCT_CMD_PORT_OFFLINE_COMPLETE, &st);
		break;

	case FCT_ACK_PORT_ONLINE_COMPLETE:
		this_ss->ss_state_not_acked = 0;
		break;

	case FCT_ACK_PORT_OFFLINE_COMPLETE:
		this_ss->ss_state_not_acked = 0;
		if (this_ss->ss_change_state_flags & STMF_RFLAG_RESET) {
			if (fct_port_initialize(port,
			    this_ss->ss_change_state_flags,
			    "fcoet_ctl FCT_ACK_PORT_OFFLINE_COMPLETE "
			    "with RLFLAG_RESET") != FCT_SUCCESS) {
				cmn_err(CE_WARN, "fcoet_ctl: "
				    "fct_port_initialize %s failed",
				    this_ss->ss_alias);
				FCOET_LOG("fcoet_ctl: fct_port_initialize "
				    "%s failed", this_ss->ss_alias);
			}
		}
		break;
	default:
		FCOET_LOG("fcoet_ctl", "Unsupported cmd %x", cmd);
		break;
	}
}

/*
 * Filling the hba attributes
 */
/* ARGSUSED */
void
fcoet_populate_hba_fru_details(struct fct_local_port *port,
    struct fct_port_attrs *port_attrs)
{
	(void) snprintf(port_attrs->manufacturer, FCHBA_MANUFACTURER_LEN,
	    "Sun Microsystems, Inc.");
	(void) snprintf(port_attrs->driver_name, FCHBA_DRIVER_NAME_LEN,
	    "%s", FCOET_NAME);
	(void) snprintf(port_attrs->driver_version, FCHBA_DRIVER_VERSION_LEN,
	    "%s", FCOET_VERSION);
	(void) strcpy(port_attrs->serial_number, "N/A");
	(void) strcpy(port_attrs->hardware_version, "N/A");
	(void) strcpy(port_attrs->model, "FCoE Virtual FC HBA");
	(void) strcpy(port_attrs->model_description, "N/A");
	(void) strcpy(port_attrs->firmware_version, "N/A");
	(void) strcpy(port_attrs->option_rom_version, "N/A");

	port_attrs->vendor_specific_id = 0xFC0E;
	port_attrs->max_frame_size = 2136;
	port_attrs->supported_cos = 0x10000000;
	/* Specified a fix speed here, need to change it in the future */
	port_attrs->supported_speed = PORT_SPEED_1G | PORT_SPEED_10G;
}


static fct_status_t
fcoet_send_sol_els(fct_cmd_t *cmd)
{
	fcoe_frame_t	 *frm;
	fcoet_exchange_t *xch = NULL;
	uint16_t		xid;

	frm = CMD2SS(cmd)->ss_eport->eport_alloc_frame(CMD2SS(cmd)->ss_eport,
	    CMD2ELS(cmd)->els_req_size + FCFH_SIZE, NULL);
	if (frm == NULL) {
		ASSERT(0);
		return (FCT_FAILURE);
	}

	xch = CMD2XCH(cmd);
	xch->xch_flags = 0;
	xch->xch_ss = CMD2SS(cmd);
	xch->xch_cmd = cmd;
	xch->xch_current_seq = NULL;
	xch->xch_left_data_size = 0;
	xch->xch_sequence_no = 0;
	xch->xch_start_time = ddi_get_lbolt();
	xch->xch_rxid = 0xFFFF;

	do {
		xid = atomic_inc_16_nv(&CMD2SS(cmd)->ss_next_sol_oxid) %
		    FCOET_OXID_TABLE_SIZE;
	} while (atomic_cas_ptr(&CMD2SS(cmd)->ss_sol_oxid_table[xid], NULL, xch)
	    != NULL);
	xch->xch_oxid = xid;

	fcoet_init_tfm(frm, CMD2XCH(cmd));
	bcopy(CMD2ELS(cmd)->els_req_payload, frm->frm_payload,
	    frm->frm_payload_size);
	FFM_R_CTL(0x22, frm);
	FRM2TFM(frm)->tfm_rctl = 0x22;
	FFM_TYPE(0x01, frm);
	FFM_F_CTL(0x290000, frm);
	FFM_OXID(xch->xch_oxid, frm);
	FFM_RXID(xch->xch_rxid, frm);
	FFM_S_ID(cmd->cmd_lportid, frm);
	FFM_D_ID(cmd->cmd_rportid, frm);
	CMD2SS(cmd)->ss_eport->eport_tx_frame(frm);

	return (FCT_SUCCESS);
}

static fct_status_t
fcoet_send_sol_ct(fct_cmd_t *cmd)
{
	fcoe_frame_t	 *frm;
	fcoet_exchange_t *xch;
	uint16_t		xid;

	frm = CMD2SS(cmd)->ss_eport->eport_alloc_frame(CMD2SS(cmd)->ss_eport,
	    CMD2ELS(cmd)->els_req_size + FCFH_SIZE, NULL);
	if (frm == NULL) {
		ASSERT(0);
		return (FCT_FAILURE);
	}

	xch = CMD2XCH(cmd);
	xch->xch_flags = 0;
	xch->xch_ss = CMD2SS(cmd);
	xch->xch_cmd = cmd;
	xch->xch_current_seq = NULL;
	xch->xch_left_data_size = 0;
	xch->xch_sequence_no = 0;
	xch->xch_start_time = ddi_get_lbolt();
	xch->xch_rxid = 0xFFFF;

	do {
		xid = atomic_inc_16_nv(&CMD2SS(cmd)->ss_next_sol_oxid) %
		    FCOET_OXID_TABLE_SIZE;
	} while (atomic_cas_ptr(&CMD2SS(cmd)->ss_sol_oxid_table[xid], NULL, xch)
	    != NULL);
	xch->xch_oxid = xid;

	fcoet_init_tfm(frm, CMD2XCH(cmd));
	bcopy(CMD2ELS(cmd)->els_req_payload, frm->frm_payload,
	    frm->frm_payload_size);
	FFM_R_CTL(0x2, frm);
	FRM2TFM(frm)->tfm_rctl = 0x2;
	FFM_TYPE(0x20, frm);
	FFM_F_CTL(0x290000, frm);
	FFM_OXID(xch->xch_oxid, frm);
	FFM_RXID(xch->xch_rxid, frm);
	FFM_S_ID(cmd->cmd_lportid, frm);
	FFM_D_ID(cmd->cmd_rportid, frm);
	CMD2SS(cmd)->ss_eport->eport_tx_frame(frm);

	return (FCT_SUCCESS);
}

fct_status_t
fcoet_send_status(fct_cmd_t *cmd)
{
	fcoe_frame_t	*frm;
	scsi_task_t	*task = CMD2TASK(cmd);
	fcoe_fcp_rsp_t	*ffr;
	int		 raw_frame_size;

	/*
	 * Fast channel for good status phase
	 */
	if (task->task_scsi_status == STATUS_GOOD && !task->task_resid) {
		return (fcoet_send_good_status(cmd));
	}

	raw_frame_size = FCFH_SIZE + sizeof (fcoe_fcp_rsp_t);
	if (task->task_scsi_status == STATUS_CHECK) {
		raw_frame_size += task->task_sense_length;
	}
	raw_frame_size = P2ROUNDUP(raw_frame_size, 4);

	frm = CMD2SS(cmd)->ss_eport->eport_alloc_frame(CMD2SS(cmd)->ss_eport,
	    raw_frame_size, NULL);
	if (frm == NULL) {
		ASSERT(0);
		return (FCT_FAILURE);
	} else {
		fcoet_init_tfm(frm, CMD2XCH(cmd));
		bzero(frm->frm_payload, frm->frm_payload_size);
		/*
		 * lock the xchg to avoid being released (by abort)
		 * after sent out and before release
		 */
		FCOET_BUSY_XCHG(CMD2XCH(cmd));
	}

	/*
	 * If there's sense data, copy it first
	 */
	if ((task->task_scsi_status == STATUS_CHECK) &&
	    task->task_sense_length) {
		bcopy(task->task_sense_data, frm->frm_payload +
		    sizeof (fcoe_fcp_rsp_t), task->task_sense_length);
	}

	/*
	 * Fill fcp_rsp
	 */
	ffr = (fcoe_fcp_rsp_t *)frm->frm_payload;
	FCOE_V2B_2(0, ffr->ffr_retry_delay_timer);
	FCOE_V2B_1(0, ffr->ffr_flags);
	if (task->task_scsi_status == STATUS_CHECK || task->task_resid) {
		if (task->task_scsi_status == STATUS_CHECK) {
			ffr->ffr_flags[0] |= BIT_1;
		}
		if (task->task_status_ctrl == TASK_SCTRL_OVER) {
			ffr->ffr_flags[0] |= BIT_2;
		} else if (task->task_status_ctrl == TASK_SCTRL_UNDER) {
			ffr->ffr_flags[0] |= BIT_3;
		}
	}
	FCOE_V2B_1(task->task_scsi_status, ffr->ffr_scsi_status);
	FCOE_V2B_4(task->task_resid, ffr->ffr_resid);
	FCOE_V2B_4(task->task_sense_length, ffr->ffr_sns_len);
	FCOE_V2B_4(0, ffr->ffr_rsp_len);

	/*
	 * Fill fc frame header
	 */
	FFM_R_CTL(0x07, frm);
	FRM2TFM(frm)->tfm_rctl = 0x07;
	FFM_TYPE(0x08, frm);
	FFM_F_CTL(0x990000, frm);
	FFM_OXID(cmd->cmd_oxid, frm);
	FFM_RXID(cmd->cmd_rxid, frm);
	FFM_S_ID(cmd->cmd_lportid, frm);
	FFM_D_ID(cmd->cmd_rportid, frm);
	FFM_SEQ_ID(0x01, frm);
	CMD2SS(cmd)->ss_eport->eport_tx_frame(frm);

	return (FCT_SUCCESS);
}

static fct_status_t
fcoet_send_els_response(fct_cmd_t *cmd)
{
	fcoe_frame_t *frm;

	frm = CMD2SS(cmd)->ss_eport->eport_alloc_frame(CMD2SS(cmd)->ss_eport,
	    CMD2ELS(cmd)->els_resp_size + FCFH_SIZE, NULL);
	if (frm == NULL) {
		ASSERT(0);
		return (FCT_FAILURE);
	} else {
		fcoet_init_tfm(frm, CMD2XCH(cmd));
		bzero(frm->frm_payload, frm->frm_payload_size);
		/*
		 * lock the xchg to avoid being released (by abort)
		 * after sent out and before release
		 */
		FCOET_BUSY_XCHG(CMD2XCH(cmd));
	}

	bcopy(CMD2ELS(cmd)->els_resp_payload, frm->frm_payload,
	    frm->frm_payload_size);
	FFM_R_CTL(0x23, frm);
	FRM2TFM(frm)->tfm_rctl = 0x23;
	FFM_TYPE(0x01, frm);
	FFM_F_CTL(0x980000, frm);
	FFM_OXID(cmd->cmd_oxid, frm);
	FFM_RXID(cmd->cmd_rxid, frm);
	FFM_S_ID(cmd->cmd_lportid, frm);
	FFM_D_ID(cmd->cmd_rportid, frm);
	CMD2SS(cmd)->ss_eport->eport_tx_frame(frm);

	return (FCT_SUCCESS);
}

/* ARGSUSED */
static fct_status_t
fcoet_send_abts_response(fct_cmd_t *cmd, uint32_t flags)
{
	fcoe_frame_t	*frm;
	fct_rcvd_abts_t *abts = (fct_rcvd_abts_t *)cmd->cmd_specific;

	FCOET_LOG("fcoet_send_abts_response", "cmd/%p, xch/%p, %x/%x",
	    cmd, CMD2XCH(cmd), cmd->cmd_oxid, cmd->cmd_rxid);

	/*
	 * The relevant fcoet_exchange has been released
	 */
	cmd->cmd_fca_private = NULL;
	frm = CMD2SS(cmd)->ss_eport->eport_alloc_frame(CMD2SS(cmd)->ss_eport,
	    12 + FCFH_SIZE, NULL);
	if (frm == NULL) {
		ASSERT(0);
		return (FCT_FAILURE);
	} else {
		fcoet_init_tfm(frm, NULL);
	}

	bcopy(abts->abts_resp_payload, frm->frm_payload,
	    frm->frm_payload_size);
	FFM_R_CTL(abts->abts_resp_rctl, frm);
	FRM2TFM(frm)->tfm_rctl = abts->abts_resp_rctl;
	FFM_TYPE(0x00, frm);
	FFM_F_CTL(0x980000, frm);
	FFM_OXID(cmd->cmd_oxid, frm);
	FFM_RXID(cmd->cmd_rxid, frm);
	FFM_S_ID(cmd->cmd_lportid, frm);
	FFM_D_ID(cmd->cmd_rportid, frm);
	CMD2SS(cmd)->ss_eport->eport_tx_frame(frm);

	return (FCT_SUCCESS);
}

/*
 * enable/disable port is simple compared to physical FC HBAs
 */
fct_status_t
fcoet_enable_port(fcoet_soft_state_t *ss)
{
	FCOET_EXT_LOG(ss->ss_alias, "port is being enabled-%p", ss);
	/* Call fcoe function to online the port */
	if (ss->ss_eport->eport_ctl(ss->ss_eport, FCOE_CMD_PORT_ONLINE, 0) ==
	    FCOE_FAILURE) {
		return (FCT_FAILURE);
	}

	if ((ss->ss_flags & SS_FLAG_PORT_DISABLED) == SS_FLAG_PORT_DISABLED) {
		atomic_and_32(&ss->ss_flags, ~SS_FLAG_PORT_DISABLED);
	}

	return (FCT_SUCCESS);
}

fct_status_t
fcoet_disable_port(fcoet_soft_state_t *ss)
{
	fct_status_t	status;

	FCOET_EXT_LOG(ss->ss_alias, "port is being disabled-%p", ss);
	/* Call fcoe function to offline the port */
	status = fcoet_logo_fabric(ss);
	ss->ss_eport->eport_ctl(ss->ss_eport, FCOE_CMD_PORT_OFFLINE, 0);
	atomic_or_32(&ss->ss_flags, SS_FLAG_PORT_DISABLED);
	return (status);
}

static fct_status_t
fcoet_logo_fabric(fcoet_soft_state_t *ss)
{
	fcoe_frame_t	*frm;
	uint32_t	req_payload_size = 16;
	uint16_t	xch_oxid, xch_rxid = 0xFFFF;

	frm = ss->ss_eport->eport_alloc_frame(ss->ss_eport,
	    req_payload_size + FCFH_SIZE, NULL);
	if (frm == NULL) {
		ASSERT(0);
		return (FCT_FAILURE);
	} else {
		fcoet_init_tfm(frm, NULL);
		bzero(frm->frm_payload, frm->frm_payload_size);
	}
	xch_oxid = atomic_add_16_nv(&ss->ss_next_sol_oxid, 1);
	if (xch_oxid == 0xFFFF) {
		xch_oxid = atomic_add_16_nv(&ss->ss_next_sol_oxid, 1);
	}
	FFM_R_CTL(0x22, frm);
	FRM2TFM(frm)->tfm_rctl = 0x22;
	FFM_TYPE(0x01, frm);
	FFM_F_CTL(0x290000, frm);
	FFM_OXID(xch_oxid, frm);
	FFM_RXID(xch_rxid, frm);
	FFM_S_ID(ss->ss_link_info.portid, frm);
	FFM_D_ID(0xfffffe, frm);

	FCOE_V2B_1(0x5, frm->frm_payload);
	FCOE_V2B_3(ss->ss_link_info.portid, frm->frm_payload + 5);
	bcopy(ss->ss_eport->eport_portwwn, frm->frm_payload + 8, 8);
	ss->ss_eport->eport_tx_frame(frm);

	return (FCT_SUCCESS);

}

/*
 * Called by: fcoet_register_remote_port
 */
/* ARGSUSED */
static fct_status_t
fcoet_fill_plogi_req(fct_local_port_t *port, fct_remote_port_t *rp,
    fct_cmd_t *login)
{
	uint8_t *p;

	p = ((fct_els_t *)login->cmd_specific)->els_req_payload;
	p[0] = ELS_OP_PLOGI;
	p[4] = 0x20;
	p[5] = 0x20;
	p[7] = 3;
	p[8] = 0x88;
	p[10] = 8;
	p[13] = 0xff; p[15] = 0x1f;
	p[18] = 7; p[19] = 0xd0;

	bcopy(port->port_pwwn, p + 20, 8);
	bcopy(port->port_nwwn, p + 28, 8);

	p[68] = 0x80;
	p[74] = 8;
	p[77] = 0xff;
	p[81] = 1;

	return (FCT_SUCCESS);
}

/*
 * Called by: fcoet_register_remote_port
 */
/* ARGSUSED */
static fct_status_t
fcoet_fill_plogi_resp(fct_local_port_t *port, fct_remote_port_t *rp,
    fct_cmd_t *login)
{
	uint8_t *p;
	/*
	 * ACC
	 */
	p = ((fct_els_t *)login->cmd_specific)->els_req_payload;
	p[0] = ELS_OP_ACC;
	p[4] = 0x20;
	p[5] = 0x20;
	p[7] = 0x0A;
	p[10] = 0x05;
	p[11] = 0xAC;

	bcopy(port->port_pwwn, p + 20, 8);
	bcopy(port->port_nwwn, p + 28, 8);

	p[68] = 0x88;
	return (FCT_SUCCESS);
}

static fct_status_t
fcoet_send_good_status(fct_cmd_t *cmd)
{
	fcoe_frame_t	*frm;
	int		 raw_frame_size;

	raw_frame_size = FCFH_SIZE + sizeof (fcoe_fcp_rsp_t);
	frm = CMD2SS(cmd)->ss_eport->eport_alloc_frame(CMD2SS(cmd)->ss_eport,
	    raw_frame_size, NULL);
	if (frm == NULL) {
		ASSERT(0);
		return (FCT_FAILURE);
	} else {
		fcoet_init_tfm(frm, CMD2XCH(cmd));
		bzero(frm->frm_payload, frm->frm_payload_size);
		/*
		 * lock the xchg to avoid being released (by abort)
		 * after sent out and before release
		 */
		FCOET_BUSY_XCHG(CMD2XCH(cmd));
	}

	/*
	 * Fill fc frame header
	 */
	FFM_R_CTL(0x07, frm);
	FRM2TFM(frm)->tfm_rctl = 0x07;
	FFM_TYPE(0x08, frm);
	FFM_F_CTL(0x990000, frm);
	FFM_OXID(cmd->cmd_oxid, frm);
	FFM_RXID(cmd->cmd_rxid, frm);
	FFM_S_ID(cmd->cmd_lportid, frm);
	FFM_D_ID(cmd->cmd_rportid, frm);
	FFM_SEQ_ID(0x01, frm);

	CMD2SS(cmd)->ss_eport->eport_tx_frame(frm);

	return (FCT_SUCCESS);
}
