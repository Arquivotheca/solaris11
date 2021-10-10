/*
 * Copyright (c) 2002, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * mpt_impl - This file contains all the basic functions for communicating
 * to MPT based hardware.
 */

#if defined(lint) && !defined(DEBUG)
#define	DEBUG 1
#define	MPT_DEBUG
#endif

/*
 * standard header files
 */
#include <sys/note.h>
#include <sys/scsi/scsi.h>
#include <sys/pci.h>

#include <sys/mpt/mpi.h>
#include <sys/mpt/mpi_ioc.h>
#include <sys/mpt/mpi_init.h>

/*
 * private header files.
 */
#include <sys/scsi/adapters/mptvar.h>
#include <sys/scsi/adapters/mptreg.h>

/*
 * FMA header files.
 */
#include <sys/fm/io/ddi.h>

#if defined(MPT_DEBUG)
extern uint32_t mpt_debug_flags;
#endif

extern uint_t mpt_raid_fw[];
extern uint_t mpt_fw[];

int mpt_disable_firmware_download = 0;
int mpt_force_firmware_download = 0;

extern int mpt_check_acc_handle(ddi_acc_handle_t handle);
extern void mpt_fm_ereport(mpt_t *mpt, char *detail);

/*
 *  queue operation for ioc event command
 */
static void mpt_ioc_event_cmdq_add(mpt_t *mpt, m_event_struct_t *cmd);
static void mpt_ioc_event_cmdq_delete(mpt_t *mpt, m_event_struct_t *cmd);
static m_event_struct_t *
mpt_ioc_event_find_by_cmd(mpt_t *mpt, struct mpt_cmd *cmd);

/*
 * add ioc evnet cmd into the queue
 */
static void
mpt_ioc_event_cmdq_add(mpt_t *mpt, m_event_struct_t *cmd)
{
	if ((cmd->m_event_linkp = mpt->m_ioc_event_cmdq) == NULL) {
		mpt->m_ioc_event_cmdtail = &cmd->m_event_linkp;
		mpt->m_ioc_event_cmdq = cmd;
	} else {
		cmd->m_event_linkp = NULL;
		*(mpt->m_ioc_event_cmdtail) = cmd;
		mpt->m_ioc_event_cmdtail = &cmd->m_event_linkp;
	}
}

/*
 * remove specified cmd from the ioc event queue
 */
static void
mpt_ioc_event_cmdq_delete(mpt_t *mpt, m_event_struct_t *cmd)
{
	m_event_struct_t *prev = mpt->m_ioc_event_cmdq;
	if (prev == cmd) {
		if ((mpt->m_ioc_event_cmdq = cmd->m_event_linkp) == NULL) {
			mpt->m_ioc_event_cmdtail = &mpt->m_ioc_event_cmdq;
		}
		cmd->m_event_linkp = NULL;
		return;
	}
	while (prev != NULL) {
		if (prev->m_event_linkp == cmd) {
			prev->m_event_linkp = cmd->m_event_linkp;
			if (cmd->m_event_linkp == NULL) {
				mpt->m_ioc_event_cmdtail = &prev->m_event_linkp;
			}

			cmd->m_event_linkp = NULL;
			return;
		}
		prev = prev->m_event_linkp;
	}
}

static m_event_struct_t *
mpt_ioc_event_find_by_cmd(mpt_t *mpt, struct mpt_cmd *cmd)
{
	m_event_struct_t *ioc_cmd = NULL;

	ioc_cmd = mpt->m_ioc_event_cmdq;
	while (ioc_cmd != NULL) {
		if (&(ioc_cmd->m_event_cmd) == cmd) {
			return (ioc_cmd);
		}
		ioc_cmd = ioc_cmd->m_event_linkp;
	}
	ioc_cmd = NULL;
	return (ioc_cmd);
}

void
mpt_destroy_ioc_event_cmd(mpt_t *mpt)
{
	m_event_struct_t *ioc_cmd = NULL;
	m_event_struct_t *ioc_cmd_tmp = NULL;
	ioc_cmd = mpt->m_ioc_event_cmdq;

	/*
	 * because the IOC event queue is resource of per instance for driver,
	 * it's not only ACK event commands used it, but also some others used
	 * it. We need destroy all ACK event commands when IOC reset, but can't
	 * disturb others.So we use filter to clear the ACK event cmd in ioc
	 * event queue, and other requests should be reserved, and they would
	 * be free by its owner.
	 */
	while (ioc_cmd != NULL) {
		if (ioc_cmd->m_event_cmd.cmd_flags & CFLAG_CMDACK) {
			NDBG20(("destroy!! remove Ack Flag ioc_cmd\n"));
			if ((mpt->m_ioc_event_cmdq =
			    ioc_cmd->m_event_linkp) == NULL)
				mpt->m_ioc_event_cmdtail =
				    &mpt->m_ioc_event_cmdq;
			ioc_cmd_tmp = ioc_cmd;
			ioc_cmd = ioc_cmd->m_event_linkp;
			kmem_free(ioc_cmd_tmp, M_EVENT_STRUCT_SIZE);
		} else {
			/*
			 * it's not ack cmd, so continue to check next one
			 */

			NDBG20(("destroy!! it's not Ack Flag, continue\n"));
			ioc_cmd = ioc_cmd->m_event_linkp;
		}

	}
}

int
mpt_ioc_wait_for_response(mpt_t *mpt)
{
	int polls = 0;

	while ((ddi_get32(mpt->m_datap,
	    &mpt->m_reg->m_intr_status) & MPI_HIS_IOP_DOORBELL_STATUS)) {
		drv_usecwait(1000);
		if (polls++ > 60000) {
			return (-1);
		}
	}
	return (0);
}

int
mpt_ioc_wait_for_doorbell(mpt_t *mpt)
{
	int polls = 0;

	while ((ddi_get32(mpt->m_datap,
	    &mpt->m_reg->m_intr_status) & MPI_HIM_DIM) == 0) {
		drv_usecwait(1000);
		if (polls++ > 300000) {
			return (-1);
		}
	}
	return (0);
}

int
mpt_send_handshake_msg(mpt_t *mpt, caddr_t memp, int numbytes,
	ddi_acc_handle_t accessp)
{
	int i;

	/*
	 * clean pending doorbells
	 */
	ddi_put32(mpt->m_datap, &mpt->m_reg->m_intr_status, 0);
	ddi_put32(mpt->m_datap, &mpt->m_reg->m_doorbell,
	    ((MPI_FUNCTION_HANDSHAKE << MPI_DOORBELL_FUNCTION_SHIFT) |
	    ((numbytes / 4) << MPI_DOORBELL_ADD_DWORDS_SHIFT)));

	if (mpt_ioc_wait_for_doorbell(mpt)) {
		NDBG19(("mpt_send_handshake failed.  Doorbell not ready\n"));
		return (-1);
	}

	/*
	 * clean pending doorbells again
	 */
	ddi_put32(mpt->m_datap, &mpt->m_reg->m_intr_status, 0);

	if (mpt_ioc_wait_for_response(mpt)) {
		NDBG19(("mpt_send_handshake failed.  Doorbell not cleared\n"));
		return (-1);
	}

	/*
	 * post handshake message
	 */
	for (i = 0; (i < numbytes / 4); i++, memp += 4) {
		ddi_put32(mpt->m_datap, &mpt->m_reg->m_doorbell,
		    ddi_get32(accessp, (uint32_t *)((void *)(memp))));
		if (mpt_ioc_wait_for_response(mpt)) {
			NDBG19(("mpt_send_handshake failed posting message\n"));
			return (-1);
		}
	}

	if (mpt_check_acc_handle(mpt->m_datap) != DDI_SUCCESS) {
		ddi_fm_service_impact(mpt->m_dip, DDI_SERVICE_UNAFFECTED);
		ddi_fm_acc_err_clear(mpt->m_datap, DDI_FME_VER0);
		return (-1);
	}

	return (0);
}

int
mpt_get_handshake_msg(mpt_t *mpt, caddr_t memp, int numbytes,
	ddi_acc_handle_t accessp)
{
	int i, totalbytes, bytesleft;
	uint16_t val;

	/*
	 * wait for doorbell
	 */
	if (mpt_ioc_wait_for_doorbell(mpt)) {
		NDBG19(("mpt_get_handshake failed.  Doorbell not ready\n"));
		return (-1);
	}

	/*
	 * get first 2 bytes of handshake message to determine how much
	 * data we will be getting
	 */
	for (i = 0; i < 2; i++, memp += 2) {
		val = (ddi_get32(mpt->m_datap,
		    &mpt->m_reg->m_doorbell) & MPI_DOORBELL_DATA_MASK);
		ddi_put32(mpt->m_datap, &mpt->m_reg->m_intr_status, 0);
		if (mpt_ioc_wait_for_doorbell(mpt)) {
			NDBG19(("mpt_get_handshake failure getting initial"
			    " data\n"));
			return (-1);
		}
		ddi_put16(accessp, (uint16_t *)((void *)(memp)), val);
		if (i == 1) {
			totalbytes = (val & 0xFF) * 2;
		}
	}

	/*
	 * If we are expecting less bytes than the message wants to send
	 * we simply save as much as we expected and then throw out the rest
	 * later
	 */
	if (totalbytes > (numbytes / 2)) {
		bytesleft = ((numbytes / 2) - 2);
	} else {
		bytesleft = (totalbytes - 2);
	}

	/*
	 * Get the rest of the data
	 */
	for (i = 0; i < bytesleft; i++, memp += 2) {
		val = (ddi_get32(mpt->m_datap,
		    &mpt->m_reg->m_doorbell) & MPI_DOORBELL_DATA_MASK);
		ddi_put32(mpt->m_datap, &mpt->m_reg->m_intr_status, 0);
		if (mpt_ioc_wait_for_doorbell(mpt)) {
			NDBG19(("mpt_get_handshake failure getting"
			    " main data\n"));
			return (-1);
		}
		ddi_put16(accessp, (uint16_t *)((void *)(memp)), val);
	}

	/*
	 * Sometimes the device will send more data than is expected
	 * This data is not used by us but needs to be cleared from
	 * ioc doorbell.  So we just read the values and throw
	 * them out.
	 */
	if (totalbytes > (numbytes / 2)) {
		for (i = (numbytes / 2); i < totalbytes; i++) {
			val = (ddi_get32(mpt->m_datap,
			    &mpt->m_reg->m_doorbell) & MPI_DOORBELL_DATA_MASK);
			ddi_put32(mpt->m_datap, &mpt->m_reg->m_intr_status, 0);
			if (mpt_ioc_wait_for_doorbell(mpt)) {
				NDBG19(("mpt_get_handshake failure getting "
				    "extra garbage data\n"));
				return (-1);
			}
		}
	}

	ddi_put32(mpt->m_datap, &mpt->m_reg->m_intr_status, 0);

	if (mpt_check_acc_handle(mpt->m_datap) != DDI_SUCCESS) {
		ddi_fm_service_impact(mpt->m_dip, DDI_SERVICE_UNAFFECTED);
		ddi_fm_acc_err_clear(mpt->m_datap, DDI_FME_VER0);
		return (-1);
	}

	return (0);
}

int
mpt_ioc_reset(mpt_t *mpt)
{
	int polls = 0;
	uint32_t ioc_state, reset_msg;

	ioc_state = ddi_get32(mpt->m_datap, &mpt->m_reg->m_doorbell);

	if (mpt->m_softstate & MPT_SS_IO_UNIT_RESET) {
		mpt_log(mpt, CE_NOTE, "?Performing IO unit reset");
		reset_msg = MPI_FUNCTION_IO_UNIT_RESET;
		mpt->m_softstate &= ~MPT_SS_IO_UNIT_RESET;
	} else {
		reset_msg = MPI_FUNCTION_IOC_MESSAGE_UNIT_RESET;
	}

	/*
	 * If chip is already in ready state then there is nothing to do.
	 */
	if (ioc_state == MPI_IOC_STATE_READY) {
		return (DDI_SUCCESS);
	}

	/*
	 * If the chip is already operational, we just need to send
	 * it a message unit reset to put it back in the ready state
	 */
	if (ioc_state & MPI_IOC_STATE_OPERATIONAL) {
		ddi_put32(mpt->m_datap, &mpt->m_reg->m_doorbell,
		    (reset_msg << MPI_DOORBELL_FUNCTION_SHIFT));
		if (mpt_ioc_wait_for_response(mpt)) {
			NDBG19(("mpt_ioc_reset failure sending "
			    "message_unit_reset\n"));
			goto hard_reset;
		}

		/*
		 * Wait for chip to become ready
		 */
		while ((ddi_get32(mpt->m_datap, &mpt->m_reg->m_doorbell) &
		    MPI_IOC_STATE_READY) == 0x0) {
			drv_usecwait(1000);
			if (polls++ > 20000) {
				goto hard_reset;
			}
		}
		/*
		 * the message unit reset would do reset operations
		 * clear reply and request queue, so we should clear
		 * ACK event cmd.
		 */
		mpt_destroy_ioc_event_cmd(mpt);
		return (DDI_SUCCESS);
	}

hard_reset:
	/*
	 * Perform hard reset.
	 * Note that 1078IR is different from other mpt HBAs,
	 * we need to write 0x7 to offset base + 0x10FC, didn't define it as
	 * macro because it's special for 1078IR only, and would never change.
	 */
	MPT_ENABLE_DRWE(mpt);
	if (mpt->m_devid == MPT_1078IR) {
		ddi_put32(mpt->m_datap, (uint32_t *)((void *)(
		    (uint8_t *)(mpt->m_reg) + 0x10FC)), 0x7);
	} else {
		ddi_put32(mpt->m_datap, &mpt->m_reg->m_diag,
		    MPI_DIAG_RESET_ADAPTER);
	}
	drv_usecwait(100);
	/*
	 * it's true hardware reset after sent bit
	 * we can clear the ioc ack events queue.
	 * and this position can make sure queue clear
	 */
	mpt_destroy_ioc_event_cmd(mpt);

	/*
	 * Reload the firmware to ensure we have the most up to date version
	 */
	if (mpt_can_download_firmware(mpt)) {
		if (mpt_download_firmware(mpt)) {
			mpt_log(mpt, CE_WARN, "firmware load failed");
			mpt_fm_ereport(mpt, DDI_FM_DEVICE_NO_RESPONSE);
			ddi_fm_service_impact(mpt->m_dip, DDI_SERVICE_LOST);
			return (DDI_FAILURE);
		}
	}

	/*
	 * Wait up to 20 seconds for the IOC to become ready.
	 */
	polls = 0;
	while ((ddi_get32(mpt->m_datap, &mpt->m_reg->m_doorbell) &
	    MPI_IOC_STATE_READY) == 0x0) {
		drv_usecwait(1000);
		if (polls++ > 20000) {
			mpt_fm_ereport(mpt, DDI_FM_DEVICE_NO_RESPONSE);
			ddi_fm_service_impact(mpt->m_dip, DDI_SERVICE_LOST);
			return (DDI_FAILURE);
		}
	}
	return (DDI_SUCCESS);
}

int
mpt_request_from_pool(mpt_t *mpt, mpt_cmd_t **cmd, struct scsi_pkt **pkt)
{
	m_event_struct_t *ioc_cmd = NULL;

	ioc_cmd = kmem_zalloc(M_EVENT_STRUCT_SIZE, KM_SLEEP);
	if (ioc_cmd == NULL) {
		return (DDI_FAILURE);
	}
	ioc_cmd->m_event_linkp = NULL;
	mpt_ioc_event_cmdq_add(mpt, ioc_cmd);
	*cmd = &(ioc_cmd->m_event_cmd);
	*pkt = &(ioc_cmd->m_event_pkt);

	return (DDI_SUCCESS);
}

void
mpt_return_to_pool(mpt_t *mpt, mpt_cmd_t *cmd)
{
	m_event_struct_t *ioc_cmd = NULL;

	ioc_cmd = mpt_ioc_event_find_by_cmd(mpt, cmd);
	if (ioc_cmd == NULL) {
		return;
	}

	mpt_ioc_event_cmdq_delete(mpt, ioc_cmd);
	kmem_free(ioc_cmd, M_EVENT_STRUCT_SIZE);
	ioc_cmd = NULL;
}

int
mpt_ioc_task_management(mpt_t *mpt, int task_type, int target,
	int lun, int taskslot)
{
	_NOTE(ARGUNUSED(taskslot))

	/*
	 * In order to avoid allocating variables on the stack,
	 * we make use of the pre-existing mpt_cmd_t and
	 * scsi_pkt which are included in the mpt_t which
	 * is passed to this routine.
	 */

	msg_scsi_task_mgmt_t *task;
	int numbytes;
	int rval = FALSE;
	mpt_cmd_t *cmd;
	struct scsi_pkt *pkt;
	mpt_slots_t *slots = mpt->m_active;

	/*
	 * Can't start another task management routine.
	 */
	if (slots->m_slot[MPT_PROXY_SLOT(mpt)] != NULL) {
		mpt_log(mpt, CE_WARN, "Can only start 1 task management command"
		    " at a time\n");
		return (FALSE);
	}

	cmd = &(mpt->m_event_task_mgmt.m_event_cmd);
	pkt = &(mpt->m_event_task_mgmt.m_event_pkt);

	/*
	 * disable interrupts
	 */
	MPT_DISABLE_INTR(mpt);

	bzero((caddr_t)cmd, sizeof (*cmd));
	bzero((caddr_t)pkt, scsi_pkt_size());

	pkt->pkt_cdbp		= (opaque_t)&cmd->cmd_cdb[0];
	pkt->pkt_scbp		= (opaque_t)&cmd->cmd_scb;
	pkt->pkt_ha_private	= (opaque_t)cmd;
	pkt->pkt_flags		= (FLAG_NOINTR | FLAG_HEAD);
	pkt->pkt_time		= 60;
	pkt->pkt_address.a_target = (ushort_t)target;
	pkt->pkt_address.a_lun = (uchar_t)lun;
	cmd->cmd_pkt		= pkt;
	cmd->cmd_scblen		= 1;
	cmd->cmd_flags		= CFLAG_CMDPROXY;
	cmd->cmd_slot		= MPT_PROXY_SLOT(mpt);

	slots->m_slot[MPT_PROXY_SLOT(mpt)] = cmd;

	task = (void *)mpt->m_hshk_memp;
	bzero((caddr_t)task, sizeof (*task));

	/*
	 * form message for requested task
	 */
	mpt_init_std_hdr(mpt->m_hshk_acc_hdl, task, BT_TO_TARG(target), lun,
	    BT_TO_BUS(target), 0, MPI_FUNCTION_SCSI_TASK_MGMT);
	mpt_put_msg_MessageContext(mpt->m_hshk_acc_hdl,
	    task, (cmd->cmd_slot << 3));

	switch (task_type) {

	/*
	 * Map abort task and abort task set to target reset
	 */
	case MPI_SCSITASKMGMT_TASKTYPE_ABORT_TASK:
		ddi_put8(mpt->m_hshk_acc_hdl, &task->TaskType,
		    MPI_SCSITASKMGMT_TASKTYPE_ABORT_TASK);
		break;
	case MPI_SCSITASKMGMT_TASKTYPE_ABRT_TASK_SET:
		ddi_put8(mpt->m_hshk_acc_hdl, &task->TaskType,
		    MPI_SCSITASKMGMT_TASKTYPE_ABRT_TASK_SET);
		break;
	case MPI_SCSITASKMGMT_TASKTYPE_TARGET_RESET:
		ddi_put8(mpt->m_hshk_acc_hdl, &task->TaskType,
		    MPI_SCSITASKMGMT_TASKTYPE_TARGET_RESET);
		break;
	case MPI_SCSITASKMGMT_TASKTYPE_RESET_BUS:
		ddi_put8(mpt->m_hshk_acc_hdl, &task->TaskType,
		    MPI_SCSITASKMGMT_TASKTYPE_RESET_BUS);
		break;
	case MPI_SCSITASKMGMT_TASKTYPE_LOGICAL_UNIT_RESET:
		ddi_put8(mpt->m_hshk_acc_hdl, &task->TaskType,
		    MPI_SCSITASKMGMT_TASKTYPE_LOGICAL_UNIT_RESET);
		break;
	default:
		break;
	}

	numbytes = sizeof (struct msg_scsi_task_mgmt);

	/*
	 * Post mesage via handshake
	 */
	if (mpt_send_handshake_msg(mpt, (caddr_t)task, numbytes,
	    mpt->m_hshk_acc_hdl)) {
		mpt_log(mpt, CE_WARN, "mpt_send_handshake_msg task %d failed\n",
		    task_type);
		goto done;
	}

	rval = mpt_poll(mpt, cmd, MPT_POLL_TIME);
	ASSERT((cmd->cmd_flags & CFLAG_CMD_REMOVED) == 0);
	cmd->cmd_flags |= CFLAG_CMD_REMOVED;

	/*
	 * The Doorbell interrupt bit is set when IOC completes the SCSI
	 * task. It should be cleared before the function exits.
	 */
	ddi_put32(mpt->m_datap, &mpt->m_reg->m_intr_status, 0);
done:
	/*
	 * clear the proxy slot, and enable interrupts before returning
	 */
	slots->m_slot[MPT_PROXY_SLOT(mpt)] = NULL;

	/*
	 * If we lost our task management command
	 * we need to reset the ioc
	 */
	if (rval == FALSE) {
		if (mpt_restart_ioc(mpt)) {
			mpt_log(mpt, CE_WARN, "mpt_restart_ioc failed");
			rval = FAILED;
		}
	}

	MPT_ENABLE_INTR(mpt);
	return (rval);
}

int
mpt_send_event_ack(mpt_t *mpt, uint32_t event, uint32_t eventcntx)
{

	/*
	 * In order to avoid allocating variables on the stack,
	 * we make use of the pre-existing mpt_cmd_t and
	 * scsi_pkt which are included in the mpt_t which
	 * is passed to this routine.
	 */

	msg_event_ack_t *eventack;
	caddr_t memp;
	mpt_cmd_t *cmd;
	m_event_struct_t *ioc_event_cmd;
	struct scsi_pkt *pkt;
	uint32_t fma;
	int rvalue;

	NDBG20(("Need to send ack"));
	if ((rvalue = (mpt_request_from_pool(mpt, &cmd, &pkt))) == -1) {
		mpt_log(mpt, CE_WARN, "mpt_send_event_ack(): allocation "
		    "failed. event ack command pool is full\n");
		return (rvalue);
	}

	bzero((caddr_t)cmd, sizeof (*cmd));
	bzero((caddr_t)pkt, scsi_pkt_size());
	cmd->ioc_cmd_slot = (uint32_t)rvalue;

	/*
	 * Form a blank cmd/pkt to store the acknoledgement message
	 * the cmd_flag CFLAG_CMDACK is used for destroy function
	 */
	pkt->pkt_cdbp		= (opaque_t)&cmd->cmd_cdb[0];
	pkt->pkt_scbp		= (opaque_t)&cmd->cmd_scb;
	pkt->pkt_ha_private	= (opaque_t)cmd;
	pkt->pkt_flags		= FLAG_HEAD;
	pkt->pkt_time		= 60;
	cmd->cmd_pkt		= pkt;
	cmd->cmd_scblen		= 1;
	cmd->cmd_flags		= CFLAG_CMDIOC | CFLAG_CMDACK;

	/*
	 * Save the command in a slot. If it failed, we store the
	 * event into the ioc event queue. send it in pending function.
	 */
	if (mpt_save_cmd(mpt, cmd) == FALSE) {
		NDBG20(("send event ack save cmd failed"));
		ioc_event_cmd = mpt_ioc_event_find_by_cmd(mpt, cmd);
		ioc_event_cmd->m_event = event;
		ioc_event_cmd->m_eventcntx = eventcntx;
		return (-1);
	} else {
		/*
		 * It's succeed, don't do anything.
		 */
		NDBG20(("send ack succeed"));
	}
	/*
	 * Store the Event Ack message in memory location corresponding to
	 * our slot number
	 */
	memp = MPT_GET_NEXT_FRAME(mpt, cmd->cmd_slot);
	/*
	 * This is the most efficient way to avoid bzeroing the entire frame.
	 */
	ddi_put32(mpt->m_acc_hdl, (uint32_t *)((void *)(memp)), 0);
	ddi_put32(mpt->m_acc_hdl, (uint32_t *)((void *)(memp + 4)), 0);
	eventack = (void *)memp;
	ddi_put8(mpt->m_acc_hdl, &eventack->Function, MPI_FUNCTION_EVENT_ACK);
	ddi_put32(mpt->m_acc_hdl, &eventack->Event, event);
	ddi_put32(mpt->m_acc_hdl, &eventack->EventContext, eventcntx);
	mpt_put_msg_MessageContext(mpt->m_acc_hdl, eventack,
	    (cmd->cmd_slot << 3));

	fma = (mpt->m_fma + ((mpt->m_req_frame_size * 4 * MPT_FRAME_SIZE(mpt)) *
	    cmd->cmd_slot));

	/*
	 * Write address of our command to the chip to start the command
	 */
	(void) ddi_dma_sync(mpt->m_dma_hdl, 0, 0, DDI_DMA_SYNC_FORDEV);
	MPT_START_CMD(mpt, fma);

	return (0);
}


/*
 * mpt send pending ack, it would send ack event cmd
 * in mpt interrupt process, make sure the ack event
 * cmd can be send.
 */
void
mpt_send_pending_event_ack(mpt_t *mpt)
{
	msg_event_ack_t *eventack;
	caddr_t memp;
	mpt_cmd_t *cmd;
	m_event_struct_t *ioc_cmd = NULL;
	uint32_t fma, event, eventcntx;

	if (mpt->m_ioc_event_cmdtail == &mpt->m_ioc_event_cmdq) {
		return;
	}
	ioc_cmd = mpt->m_ioc_event_cmdq;
	while (ioc_cmd != NULL) {

		if (ioc_cmd->m_eventcntx == NULL) {
			ioc_cmd = ioc_cmd->m_event_linkp;
			continue;
		}
		NDBG20(("Send queued ioc_cmd"));

		cmd = &(ioc_cmd->m_event_cmd);
		event = ioc_cmd->m_event;
		eventcntx = ioc_cmd->m_eventcntx;
		/*
		 * Save the command in a slot, if result is false
		 * wait next interrupt chance send remain cmd
		 */
		if (mpt_save_cmd(mpt, cmd) == FALSE) {
			ioc_cmd = ioc_cmd->m_event_linkp;
			break;
		}

		NDBG20(("Successfully sent queued ioc_cmd"));
		/*
		 * Store the Event Ack message in memory
		 * location corresponding to
		 * our slot number
		 */
		memp = MPT_GET_NEXT_FRAME(mpt, cmd->cmd_slot);
		/*
		 * This is the most efficient way to avoid bzeroing the entire
		 * frame.
		 */
		ddi_put32(mpt->m_acc_hdl, (uint32_t *)((void *)(memp)), 0);
		ddi_put32(mpt->m_acc_hdl, (uint32_t *)((void *)(memp + 4)), 0);

		eventack = (void *)memp;
		ddi_put8(mpt->m_acc_hdl, &eventack->Function,
		    MPI_FUNCTION_EVENT_ACK);
		ddi_put32(mpt->m_acc_hdl, &eventack->Event, event);
		ddi_put32(mpt->m_acc_hdl, &eventack->EventContext, eventcntx);
		mpt_put_msg_MessageContext(mpt->m_acc_hdl, eventack,
		    (cmd->cmd_slot << 3));

		fma = (mpt->m_fma + ((mpt->m_req_frame_size * 4 *
		    MPT_FRAME_SIZE(mpt)) *
		    cmd->cmd_slot));

		/*
		 * Write address of our command to the chip to start the command
		 */
		(void) ddi_dma_sync(mpt->m_dma_hdl, 0, 0, DDI_DMA_SYNC_FORDEV);
		MPT_START_CMD(mpt, fma);
		ioc_cmd->m_eventcntx = NULL;
		ioc_cmd = ioc_cmd->m_event_linkp;
	}
}

/*
 * Returns non-zero if this HBA is compatible with the firmware built into
 * the driver AND if the firmware currently on the HBA is older than
 * the firmware stored in the driver.
 */
int
mpt_can_download_firmware(mpt_t *mpt)
{
	uint32_t *chosen_fw = NULL;
	uint32_t product = mpt->m_productid & MPI_FW_HEADER_PID_PROD_MASK;
	uint32_t raid_fw_prod;
	uint32_t nonraid_fw_prod;
	int downloadable = 0;

	/* Firmware download is only supported on SCSI adapters */
	if (!MPT_IS_SCSI(mpt) || mpt_disable_firmware_download)
		return (0);

	raid_fw_prod = mpt_raid_fw[MPT_FW_PRODUCTID_OFFSET];
	raid_fw_prod &= MPT_FW_PRODUCTID_MASK;
	raid_fw_prod >>= MPT_FW_PRODUCTID_SHIFT;
	raid_fw_prod &= MPI_FW_HEADER_PID_PROD_MASK;

	nonraid_fw_prod = mpt_fw[MPT_FW_PRODUCTID_OFFSET];
	nonraid_fw_prod &= MPT_FW_PRODUCTID_MASK;
	nonraid_fw_prod >>= MPT_FW_PRODUCTID_SHIFT;
	nonraid_fw_prod &= MPI_FW_HEADER_PID_PROD_MASK;

	/*
	 * We currently store two types of mpt firmware in the driver.
	 * If the product type of the stored firmware is not the same as
	 * that of the adapter, firmware download cannot occur.
	 */
	if (product == raid_fw_prod)
		chosen_fw = mpt_raid_fw;
	else if (product == nonraid_fw_prod)
		chosen_fw = mpt_fw;

	if (chosen_fw != NULL) {
		uint32_t family;
		uint32_t fw_fam;

		/*
		 * Ensure that the chosen firmware is compatible with the
		 * current adapter.
		 */
		family = mpt->m_productid & MPI_FW_HEADER_PID_FAMILY_MASK;
		fw_fam = chosen_fw[MPT_FW_PRODUCTID_OFFSET];
		fw_fam &= MPT_FW_PRODUCTID_MASK;
		fw_fam >>= MPT_FW_PRODUCTID_SHIFT;
		fw_fam &= MPI_FW_HEADER_PID_FAMILY_MASK;

		/*
		 * The only time when we should be downloading firmware is
		 * to the family for which the firmware was designed, and
		 * even then, only if the driver's firmware is more recent
		 * (or if forced download is enabled).
		 */
		if ((family == fw_fam) &&
		    (mpt_force_firmware_download ||
		    (mpt->m_fwversion < chosen_fw[MPT_FW_VERSION_OFFSET])))
			downloadable = 1;
	}

	return (downloadable);
}

int
mpt_download_firmware(mpt_t *mpt)
{
	int polls = 0;
	int i;
	int fail = 0;
	msg_fw_header_t *header;
	mpi_ext_image_header_t *extheader;
	uint32_t *image;
	uint8_t *nextimage;
	uint32_t addr, next, data, size;

	/*
	 * Before we start the download process, check for I/O resource
	 * allocation failures. Fail gracefully if no I/O resources are
	 * allocated.
	 */
	ddi_regs_map_free(&mpt->m_datap);
	if (ddi_regs_map_setup(mpt->m_dip, IO_SPACE, (caddr_t *)&mpt->m_reg,
	    0, 0, &mpt->m_reg_acc_attr, &mpt->m_datap) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN, "map setup IO_SPACE failed");
		fail = -1;
	} else {
		if (mpt->m_reg == NULL) {
			mpt_log(mpt, CE_WARN, "IO_SPACE resources not "
			    "allocated");
			fail = -1;
		}
		ddi_regs_map_free(&mpt->m_datap);
	}
	if (ddi_regs_map_setup(mpt->m_dip, MEM_SPACE, (caddr_t *)&mpt->m_reg,
	    0, 0, &mpt->m_reg_acc_attr, &mpt->m_datap) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN, "map setup MEM_SPACE failed");
		return (-1);
	}
	if (fail)
		return (fail);

	MPT_ENABLE_DRWE(mpt);
	ddi_put32(mpt->m_datap, &mpt->m_reg->m_diag, MPI_DIAG_DISABLE_ARM |
	    MPI_DIAG_PREVENT_IOC_BOOT | MPI_DIAG_CLEAR_FLASH_BAD_SIG);

	drv_usecwait(100);
	ddi_put32(mpt->m_datap, &mpt->m_reg->m_diag, MPI_DIAG_DISABLE_ARM |
	    MPI_DIAG_PREVENT_IOC_BOOT | MPI_DIAG_RESET_ADAPTER);

	while ((ddi_get32(mpt->m_datap, &mpt->m_reg->m_diag) &
	    MPI_DIAG_RESET_ADAPTER)) {
		drv_usecwait(1000);
		if (polls++ > 20000) {
			return (-1);
		}
	}

	MPT_ENABLE_DRWE(mpt);
	ddi_put32(mpt->m_datap, &mpt->m_reg->m_diag, MPI_DIAG_DISABLE_ARM |
	    MPI_DIAG_PREVENT_IOC_BOOT | MPI_DIAG_CLEAR_FLASH_BAD_SIG |
	    MPI_DIAG_RW_ENABLE);

	/*
	 * Figure out if we need to load RAID or non-RAID FW
	 */
	if (mpt->m_ssid == MPT_HBA_SUBSYSTEM_ID) {
		header = (msg_fw_header_t *)mpt_fw;
	} else if (mpt->m_productid & MPI_FW_HEADER_PID_PROD_IM_SCSI) {
		header = (msg_fw_header_t *)mpt_raid_fw;
	} else {
		header = (msg_fw_header_t *)mpt_fw;
	}
	image = (uint32_t *)header;

	addr = header->LoadStartAddress;
	size = header->ImageSize;
	next = header->NextImageHeaderOffset;
	nextimage = (uint8_t *)header + next;

	/*
	 * Need to write to diagrw registers using i/o access
	 * Remap them here.
	 */
	ddi_regs_map_free(&mpt->m_datap);
	if (ddi_regs_map_setup(mpt->m_dip, IO_SPACE, (caddr_t *)&mpt->m_reg,
	    0, 0, &mpt->m_reg_acc_attr, &mpt->m_datap) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN, "map setup IO_SPACE failed");
		return (-1);
	}

	/*
	 * Begin downloading firmware to the chip
	 */
	/*CONSTCOND*/
	while (1) {
		ddi_put32(mpt->m_datap, &mpt->m_reg->m_diagrw_addr, addr);
		if (size % 4 == 0) {
			size = size / 4;
		} else {
			size = (size / 4) + 1;
		}

		for (i = 0; i < size; i++) {
			data = *image++;
			ddi_put32(mpt->m_datap, &mpt->m_reg->m_diagrw_data,
			    data);

			/*
			 * Delay needed as workaround due to the inability
			 * of the PCI Express based LSI1030 HBA (Rhea) to
			 * keep up with firmware transmission.  Delay
			 * is fabricated by reading Doorbell register.
			 */
			if (mpt->m_ssid == MPT_RHEA_SUBSYSTEM_ID) {
				(void) ddi_get32(mpt->m_datap,
				    &mpt->m_reg->m_doorbell);
			}
		}

		if (next == 0) {
			break;
		}
		extheader = (void *)nextimage;
		image = (uint32_t *)extheader;
		addr = extheader->LoadStartAddress;
		size = extheader->ImageSize;
		next = extheader->NextImageHeaderOffset;
		nextimage = (uint8_t *)header + next;
	}

	/*
	 * finish up
	 */
	ddi_put32(mpt->m_datap, &mpt->m_reg->m_diagrw_addr,
	    header->IopResetRegAddr);
	ddi_put32(mpt->m_datap, &mpt->m_reg->m_diagrw_data,
	    header->IopResetVectorValue);

	if (MPT_IS_SCSI(mpt)) {
		ddi_put32(mpt->m_datap, &mpt->m_reg->m_diagrw_addr, 0x3F000000);
		data = ddi_get32(mpt->m_datap, &mpt->m_reg->m_diagrw_data);
		data |= 0x40000000;
		ddi_put32(mpt->m_datap, &mpt->m_reg->m_diagrw_addr, 0x3F000000);
		ddi_put32(mpt->m_datap, &mpt->m_reg->m_diagrw_data, data);
	}

	/*
	 * switch back to mem mapped
	 */
	ddi_regs_map_free(&mpt->m_datap);
	if (ddi_regs_map_setup(mpt->m_dip, MEM_SPACE, (caddr_t *)&mpt->m_reg,
	    0, 0, &mpt->m_reg_acc_attr, &mpt->m_datap) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN, "map setup MEM_SPACE failed");
		return (-1);
	}

	ddi_put32(mpt->m_datap, &mpt->m_reg->m_diag,
	    MPI_DIAG_CLEAR_FLASH_BAD_SIG);
	ddi_put32(mpt->m_datap, &mpt->m_reg->m_write_seq, 0xF);

	/*
	 * Wait up to 40 seconds for the chip to become ready
	 * We wait that long because the firmware has the ability
	 * to self-heal a corrupted SEEPROM part that is connected to the
	 * chip.  If it has to self-heal it takes longer for the chip
	 * to become ready since it has to rewrite the prom.
	 */
	polls = 0;
	while ((ddi_get32(mpt->m_datap, &mpt->m_reg->m_doorbell) &
	    MPI_IOC_STATE_READY) == 0x0) {
		drv_usecwait(1000);
		if (polls++ > 40000) {
			return (-1);
		}
	}

	return (0);
}

int
mpt_update_flash(mpt_t *mpt, caddr_t ptrbuffer, uint32_t size, uint8_t type,
	int mode)
{

	/*
	 * In order to avoid allocating variables on the stack,
	 * we make use of the pre-existing mpt_cmd_t and
	 * scsi_pkt which are included in the mpt_t which
	 * is passed to this routine.
	 */

	ddi_dma_attr_t flsh_dma_attrs;
	uint_t flsh_ncookie;
	ddi_dma_cookie_t flsh_cookie;
	ddi_dma_handle_t flsh_dma_handle;
	ddi_acc_handle_t flsh_accessp;
	size_t flsh_alloc_len;
	caddr_t memp, flsh_memp;
	uint32_t flagslength, fma;
	msg_fw_download_t *fwdownload;
	fw_download_tcsge_t *tcsge;
	sge_simple32_t *sge;
	mpt_cmd_t *cmd;
	struct scsi_pkt *pkt;
	int i;
	int rvalue;

	if ((rvalue = (mpt_request_from_pool(mpt, &cmd, &pkt))) == -1) {
		mpt_log(mpt, CE_WARN, "mpt_update_flash(): allocation failed. "
		    "event ack command pool is full\n");
		return (rvalue);
	}

	bzero((caddr_t)cmd, sizeof (*cmd));
	bzero((caddr_t)pkt, scsi_pkt_size());
	cmd->ioc_cmd_slot = (uint32_t)rvalue;

	/*
	 * dynamically create a customized dma attribute structure
	 * that describes the flash file.
	 */
	flsh_dma_attrs = mpt->m_msg_dma_attr;
	flsh_dma_attrs.dma_attr_sgllen = 1;

	if (ddi_dma_alloc_handle(mpt->m_dip, &flsh_dma_attrs,
	    DDI_DMA_SLEEP, NULL, &flsh_dma_handle) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN,
		    "(unable to allocate dma handle.");
		mpt_return_to_pool(mpt, cmd);
		return (-1);
	}

	if (ddi_dma_mem_alloc(flsh_dma_handle, size,
	    &mpt->m_dev_acc_attr, DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, NULL,
	    &flsh_memp, &flsh_alloc_len, &flsh_accessp) != DDI_SUCCESS) {
		ddi_dma_free_handle(&flsh_dma_handle);
		mpt_log(mpt, CE_WARN,
		    "unable to allocate flash structure.");
		mpt_return_to_pool(mpt, cmd);
		return (-1);
	}

	if (ddi_dma_addr_bind_handle(flsh_dma_handle, NULL, flsh_memp,
	    flsh_alloc_len, DDI_DMA_RDWR | DDI_DMA_CONSISTENT, DDI_DMA_SLEEP,
	    NULL, &flsh_cookie, &flsh_ncookie) != DDI_DMA_MAPPED) {
		(void) ddi_dma_mem_free(&flsh_accessp);
		ddi_dma_free_handle(&flsh_dma_handle);
		mpt_log(mpt, CE_WARN, "unable to bind DMA resources.");
		mpt_return_to_pool(mpt, cmd);
		return (-1);
	}
	bzero(flsh_memp, size);

	for (i = 0; i < size; i++) {
		(void) ddi_copyin(ptrbuffer + i, flsh_memp + i, 1, mode);
	}

	/*
	 * form a cmd/pkt to store the fw download message
	 */
	pkt->pkt_cdbp		= (opaque_t)&cmd->cmd_cdb[0];
	pkt->pkt_scbp		= (opaque_t)&cmd->cmd_scb;
	pkt->pkt_ha_private	= (opaque_t)cmd;
	pkt->pkt_flags		= FLAG_HEAD;
	pkt->pkt_time		= 60;
	cmd->cmd_pkt		= pkt;
	cmd->cmd_scblen		= 1;
	cmd->cmd_flags		= CFLAG_CMDIOC;

	/*
	 * Save the command in a slot
	 */
	if (mpt_save_cmd(mpt, cmd) == FALSE) {
		(void) ddi_dma_unbind_handle(flsh_dma_handle);
		(void) ddi_dma_mem_free(&flsh_accessp);
		ddi_dma_free_handle(&flsh_dma_handle);
		mpt_return_to_pool(mpt, cmd);
		return (-1);
	}

	/*
	 * Fill in fw download message
	 */
	memp = MPT_GET_NEXT_FRAME(mpt, cmd->cmd_slot);
	bzero(memp, (mpt->m_req_frame_size << 2));
	fwdownload = (void *)memp;
	ddi_put8(mpt->m_acc_hdl, &fwdownload->Function,
	    MPI_FUNCTION_FW_DOWNLOAD);
	ddi_put8(mpt->m_acc_hdl, &fwdownload->ImageType, type);
	ddi_put8(mpt->m_acc_hdl, &fwdownload->MsgFlags,
	    MPI_FW_DOWNLOAD_MSGFLGS_LAST_SEGMENT);
	mpt_put_msg_MessageContext(mpt->m_acc_hdl, fwdownload,
	    (cmd->cmd_slot << 3));
	tcsge = (struct fw_download_tcsge *)&fwdownload->SGL;
	ddi_put8(mpt->m_acc_hdl, &tcsge->ContextSize, 0);
	ddi_put8(mpt->m_acc_hdl, &tcsge->DetailsLength, 12);
	ddi_put8(mpt->m_acc_hdl, &tcsge->Flags, 0);
	ddi_put32(mpt->m_acc_hdl, &tcsge->ImageOffset, 0);
	ddi_put32(mpt->m_acc_hdl, &tcsge->ImageSize, size);
	sge = (sge_simple32_t *)(tcsge + 1);
	flagslength = size;
	flagslength |= ((uint32_t)(MPI_SGE_FLAGS_LAST_ELEMENT |
	    MPI_SGE_FLAGS_END_OF_BUFFER |
	    MPI_SGE_FLAGS_SIMPLE_ELEMENT |
	    MPI_SGE_FLAGS_SYSTEM_ADDRESS |
	    MPI_SGE_FLAGS_32_BIT_ADDRESSING |
	    MPI_SGE_FLAGS_HOST_TO_IOC |
	    MPI_SGE_FLAGS_END_OF_LIST) << MPI_SGE_FLAGS_SHIFT);
	ddi_put32(mpt->m_acc_hdl, &sge->FlagsLength, flagslength);
	ddi_put32(mpt->m_acc_hdl, &sge->Address, flsh_cookie.dmac_address);

	fma = (mpt->m_fma + ((mpt->m_req_frame_size * 4 * MPT_FRAME_SIZE(mpt)) *
	    cmd->cmd_slot));

	/*
	 * Start command and poll waiting for it to complete
	 */
	MPT_DISABLE_INTR(mpt);
	(void) ddi_dma_sync(flsh_dma_handle, 0, 0, DDI_DMA_SYNC_FORDEV);
	MPT_START_CMD(mpt, fma);
	(void) mpt_poll(mpt, cmd, MPT_POLL_TIME);
	MPT_ENABLE_INTR(mpt);

	(void) ddi_dma_unbind_handle(flsh_dma_handle);
	(void) ddi_dma_mem_free(&flsh_accessp);
	ddi_dma_free_handle(&flsh_dma_handle);

	if (pkt->pkt_reason == CMD_INCOMPLETE) {
		return (-1);
	} else {
		return (0);
	}
}

int
mpt_check_flash(mpt_t *mpt, caddr_t origfile, uint32_t size, uint8_t type,
	int mode)
{

	/*
	 * In order to avoid allocating variables on the stack,
	 * we make use of the pre-existing mpt_cmd_t and
	 * scsi_pkt which are included in the mpt_t which
	 * is passed to this routine.
	 */

	ddi_dma_attr_t flsh_dma_attrs, file_dma_attrs;
	uint_t flsh_ncookie, file_ncookie;
	ddi_dma_cookie_t flsh_cookie, file_cookie;
	ddi_dma_handle_t flsh_dma_handle, file_dma_handle;
	ddi_acc_handle_t flsh_accessp, file_accessp;
	size_t flsh_alloc_len, file_alloc_len;
	caddr_t memp, flsh_memp, file_memp;
	uint32_t flagslength, fma;
	msg_fw_upload_t *fwupload;
	fw_upload_tcsge_t *tcsge;
	sge_simple32_t *sge;
	mpt_cmd_t *cmd;
	struct scsi_pkt *pkt;
	int i, rval = 0;
	uint32_t *orig, *flsh;
	int rvalue;

	if ((rvalue = (mpt_request_from_pool(mpt, &cmd, &pkt))) == -1) {
		mpt_log(mpt, CE_WARN, "mpt_check_flash(): allocation failed. "
		    "event ack command pool is full\n");
		return (rvalue);
	}

	bzero((caddr_t)cmd, sizeof (*cmd));
	bzero((caddr_t)pkt, scsi_pkt_size());
	cmd->ioc_cmd_slot = (uint32_t)rvalue;

	/*
	 * dynamically create a customized dma attribute structure
	 * that describes the flash file.
	 */
	flsh_dma_attrs = mpt->m_msg_dma_attr;
	flsh_dma_attrs.dma_attr_sgllen = 1;

	if (ddi_dma_alloc_handle(mpt->m_dip, &flsh_dma_attrs,
	    DDI_DMA_SLEEP, NULL, &flsh_dma_handle) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN,
		    "(unable to allocate dma handle.");
		mpt_return_to_pool(mpt, cmd);
		return (-1);
	}

	if (ddi_dma_mem_alloc(flsh_dma_handle, size,
	    &mpt->m_dev_acc_attr, DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, NULL,
	    &flsh_memp, &flsh_alloc_len, &flsh_accessp) != DDI_SUCCESS) {
		ddi_dma_free_handle(&flsh_dma_handle);
		mpt_log(mpt, CE_WARN,
		    "unable to allocate flash structure.");
		mpt_return_to_pool(mpt, cmd);
		return (-1);
	}

	if (ddi_dma_addr_bind_handle(flsh_dma_handle, NULL, flsh_memp,
	    flsh_alloc_len, DDI_DMA_RDWR | DDI_DMA_CONSISTENT, DDI_DMA_SLEEP,
	    NULL, &flsh_cookie, &flsh_ncookie) != DDI_DMA_MAPPED) {
		(void) ddi_dma_mem_free(&flsh_accessp);
		ddi_dma_free_handle(&flsh_dma_handle);
		mpt_log(mpt, CE_WARN, "unable to bind DMA resources.");
		mpt_return_to_pool(mpt, cmd);
		return (-1);
	}
	bzero(flsh_memp, size);

	/*
	 * form a cmd/pkt to store the fw upload message
	 */
	pkt->pkt_cdbp		= (opaque_t)&cmd->cmd_cdb[0];
	pkt->pkt_scbp		= (opaque_t)&cmd->cmd_scb;
	pkt->pkt_ha_private	= (opaque_t)cmd;
	pkt->pkt_flags		= FLAG_HEAD;
	pkt->pkt_time		= 60;
	cmd->cmd_pkt		= pkt;
	cmd->cmd_scblen		= 1;
	cmd->cmd_flags		= CFLAG_CMDIOC;

	/*
	 * Save the command in a slot
	 */
	if (mpt_save_cmd(mpt, cmd) == FALSE) {
		(void) ddi_dma_unbind_handle(flsh_dma_handle);
		(void) ddi_dma_mem_free(&flsh_accessp);
		ddi_dma_free_handle(&flsh_dma_handle);
		mpt_return_to_pool(mpt, cmd);
		return (-1);
	}

	/*
	 * Fill in fw upload message
	 */
	memp = MPT_GET_NEXT_FRAME(mpt, cmd->cmd_slot);
	bzero(memp, (mpt->m_req_frame_size << 2));
	fwupload = (void *)memp;
	ddi_put8(mpt->m_acc_hdl, &fwupload->Function,
	    MPI_FUNCTION_FW_UPLOAD);
	ddi_put8(mpt->m_acc_hdl, &fwupload->ImageType, type);
	mpt_put_msg_MessageContext(mpt->m_acc_hdl, fwupload,
	    (cmd->cmd_slot << 3));
	tcsge = (struct fw_upload_tcsge *)&fwupload->SGL;
	ddi_put8(mpt->m_acc_hdl, &tcsge->ContextSize, 0);
	ddi_put8(mpt->m_acc_hdl, &tcsge->DetailsLength, 12);
	ddi_put8(mpt->m_acc_hdl, &tcsge->Flags, 0);
	ddi_put32(mpt->m_acc_hdl, &tcsge->ImageOffset, 0);
	ddi_put32(mpt->m_acc_hdl, &tcsge->ImageSize, size);
	sge = (sge_simple32_t *)(tcsge + 1);
	flagslength = size;
	flagslength |= ((uint32_t)(MPI_SGE_FLAGS_LAST_ELEMENT |
	    MPI_SGE_FLAGS_END_OF_BUFFER |
	    MPI_SGE_FLAGS_SIMPLE_ELEMENT |
	    MPI_SGE_FLAGS_SYSTEM_ADDRESS |
	    MPI_SGE_FLAGS_32_BIT_ADDRESSING |
	    MPI_SGE_FLAGS_IOC_TO_HOST |
	    MPI_SGE_FLAGS_END_OF_LIST) << MPI_SGE_FLAGS_SHIFT);
	ddi_put32(mpt->m_acc_hdl, &sge->FlagsLength, flagslength);
	ddi_put32(mpt->m_acc_hdl, &sge->Address, flsh_cookie.dmac_address);

	fma = (mpt->m_fma + ((mpt->m_req_frame_size * 4 * MPT_FRAME_SIZE(mpt)) *
	    cmd->cmd_slot));

	/*
	 * Start command and poll waiting for it to complete
	 */
	MPT_DISABLE_INTR(mpt);
	(void) ddi_dma_sync(flsh_dma_handle, 0, 0, DDI_DMA_SYNC_FORDEV);
	MPT_START_CMD(mpt, fma);
	(void) mpt_poll(mpt, cmd, MPT_POLL_TIME);
	MPT_ENABLE_INTR(mpt);

	if (pkt->pkt_reason == CMD_INCOMPLETE) {
		(void) ddi_dma_unbind_handle(flsh_dma_handle);
		(void) ddi_dma_mem_free(&flsh_accessp);
		ddi_dma_free_handle(&flsh_dma_handle);
		mpt_return_to_pool(mpt, cmd);
		return (-1);
	}

	/*
	 * dynamically create a customized dma attribute structure
	 * that describes the original flash file.
	 */
	file_dma_attrs = mpt->m_msg_dma_attr;
	file_dma_attrs.dma_attr_sgllen = 1;

	if (ddi_dma_alloc_handle(mpt->m_dip, &file_dma_attrs,
	    DDI_DMA_SLEEP, NULL, &file_dma_handle) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN,
		    "(unable to allocate dma handle.");
		(void) ddi_dma_unbind_handle(flsh_dma_handle);
		(void) ddi_dma_mem_free(&flsh_accessp);
		ddi_dma_free_handle(&flsh_dma_handle);
		mpt_return_to_pool(mpt, cmd);
		return (-1);
	}

	if (ddi_dma_mem_alloc(file_dma_handle, size,
	    &mpt->m_dev_acc_attr, DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, NULL,
	    &file_memp, &file_alloc_len, &file_accessp) != DDI_SUCCESS) {
		ddi_dma_free_handle(&file_dma_handle);
		mpt_log(mpt, CE_WARN,
		    "unable to allocate flash structure.");
		(void) ddi_dma_unbind_handle(flsh_dma_handle);
		(void) ddi_dma_mem_free(&flsh_accessp);
		ddi_dma_free_handle(&flsh_dma_handle);
		mpt_return_to_pool(mpt, cmd);
		return (-1);
	}

	if (ddi_dma_addr_bind_handle(file_dma_handle, NULL, file_memp,
	    file_alloc_len, DDI_DMA_RDWR | DDI_DMA_CONSISTENT, DDI_DMA_SLEEP,
	    NULL, &file_cookie, &file_ncookie) != DDI_DMA_MAPPED) {
		(void) ddi_dma_mem_free(&file_accessp);
		ddi_dma_free_handle(&file_dma_handle);
		mpt_log(mpt, CE_WARN, "unable to bind DMA resources.");
		(void) ddi_dma_unbind_handle(flsh_dma_handle);
		(void) ddi_dma_mem_free(&flsh_accessp);
		ddi_dma_free_handle(&flsh_dma_handle);
		mpt_return_to_pool(mpt, cmd);
		return (-1);
	}
	bzero(file_memp, size);

	for (i = 0; i < size; i++) {
		(void) ddi_copyin(origfile + i, file_memp + i, 1, mode);
	}

	/*
	 * Compare downloaded file with original file
	 */
	orig = (void *)file_memp;
	flsh = (void *)flsh_memp;

	for (i = 0; i < (size / 4); i++) {
		if ((ddi_get32(file_accessp, &orig[i])) !=
		    (ddi_get32(flsh_accessp, &flsh[i]))) {
			mpt_log(mpt, CE_WARN, "Flash compare failed at "
			    "location %d, 0x%x vs 0x%x\n", i,
			    ddi_get32(file_accessp, &orig[i]),
			    ddi_get32(flsh_accessp, &flsh[i]));
			rval = (-1);
		}
	}

	(void) ddi_dma_unbind_handle(flsh_dma_handle);
	(void) ddi_dma_unbind_handle(file_dma_handle);
	(void) ddi_dma_mem_free(&flsh_accessp);
	(void) ddi_dma_mem_free(&file_accessp);
	ddi_dma_free_handle(&flsh_dma_handle);
	ddi_dma_free_handle(&file_dma_handle);

	return (rval);
}
