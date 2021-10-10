/*
 * Copyright (c) 2002, 2010, Oracle and/or its affiliates. All rights reserved.
 */


/*
 * mpt_raid - This file contains all the RAID related functions for the
 * MPT interface.
 */

#if defined(lint) && !defined(DEBUG)
#define	DEBUG 1
#define	MPT_DEBUG
#endif

#define	MPI_RAID_VOL_PAGE_0_PHYSDISK_MAX	2

/*
 * standard header files
 */
#include <sys/note.h>
#include <sys/scsi/scsi.h>
#include <sys/byteorder.h>

#include <sys/mpt/mpi.h>
#include <sys/mpt/mpi_cnfg.h>
#include <sys/mpt/mpi_init.h>
#include <sys/mpt/mpi_ioc.h>
#include <sys/mpt/mpi_raid.h>
#include <sys/raidioctl.h>

/*
 * private header files.
 */
#include <sys/scsi/adapters/mptvar.h>
#include <sys/scsi/adapters/mptreg.h>

static int mpt_create_physdisk(mpt_t *mpt, uint16_t targ);
static int mpt_create_volume(mpt_t *mpt, int vol, uint8_t raid_level,
	uint16_t volid, uint32_t maxlba);
static int mpt_get_raid_wwid(mpt_t *mpt, int vol, uint16_t volid);

extern int mpt_check_dma_handle(ddi_dma_handle_t handle);

int
mpt_send_raid_action(mpt_t *mpt, uint8_t action, uint16_t vol, uint8_t physdisk,
	uint32_t flagslength, uint32_t SGEaddr, uint32_t dataword)
{

	/*
	 * In order to avoid allocating variables on the stack,
	 * we make use of the pre-existing mpt_cmd_t and
	 * scsi_pkt which are included in the mpt_t which
	 * is passed to this routine.
	 */

	msg_raid_action_t *raidaction;
	caddr_t memp;
	mpt_cmd_t *cmd;
	struct scsi_pkt *pkt;
	uint32_t fma;
	int rvalue;

	if ((rvalue = (mpt_request_from_pool(mpt, &cmd, &pkt))) == -1) {
		mpt_log(mpt, CE_WARN, "mpt_send_raid_action(): allocation "
		    "failed. event ack command pool is full\n");
		return (rvalue);
	}


	bzero((caddr_t)cmd, sizeof (*cmd));
	bzero((caddr_t)pkt, scsi_pkt_size());
	cmd->ioc_cmd_slot = (uint32_t)rvalue;

	/*
	 * Form a blank cmd/pkt to store the acknoledgement message
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
		mpt_return_to_pool(mpt, cmd);
		return (-1);
	}
	/*
	 * Store the raid action message in memory location corresponding to
	 * our slot number
	 */
	memp = MPT_GET_NEXT_FRAME(mpt, cmd->cmd_slot);
	bzero(memp, (mpt->m_req_frame_size << 2));
	raidaction = (void *)memp;
	ddi_put8(mpt->m_acc_hdl, &raidaction->Function,
	    MPI_FUNCTION_RAID_ACTION);
	ddi_put8(mpt->m_acc_hdl, &raidaction->Action, action);
	ddi_put8(mpt->m_acc_hdl, &raidaction->VolumeID, BT_TO_TARG(vol));
	ddi_put8(mpt->m_acc_hdl, &raidaction->VolumeBus, BT_TO_BUS(vol));

	if (action != MPI_RAID_ACTION_CREATE_PHYSDISK)
		ddi_put8(mpt->m_acc_hdl, &raidaction->PhysDiskNum, physdisk);

	ddi_put32(mpt->m_acc_hdl,
	    &raidaction->ActionDataSGE.FlagsLength, flagslength);
	ddi_put32(mpt->m_acc_hdl,
	    &raidaction->ActionDataSGE.u1.Address32, SGEaddr);
	ddi_put32(mpt->m_acc_hdl, &raidaction->ActionDataWord,
	    dataword);

	mpt_put_msg_MessageContext(mpt->m_acc_hdl, raidaction,
	    (cmd->cmd_slot << 3));

	fma = (mpt->m_fma + ((mpt->m_req_frame_size * 4 * MPT_FRAME_SIZE(mpt)) *
	    cmd->cmd_slot));

	/*
	 * We must wait till the message has been completed before beginning
	 * the next message so we poll for this one to finish
	 */
	MPT_DISABLE_INTR(mpt);
	(void) ddi_dma_sync(mpt->m_dma_hdl, 0, 0, DDI_DMA_SYNC_FORDEV);
	MPT_START_CMD(mpt, fma);
	(void) mpt_poll(mpt, cmd, MPT_POLL_TIME);
	MPT_ENABLE_INTR(mpt);

	if (pkt->pkt_reason == CMD_INCOMPLETE) {
		return (-1);
	} else {
		return (0);
	}
}

int
mpt_get_raid_info(mpt_t *mpt)
{
	ddi_dma_attr_t recv_dma_attrs, page_dma_attrs;
	uint_t recv_ncookie, page_ncookie;
	ddi_dma_cookie_t recv_cookie, page_cookie;
	ddi_dma_handle_t recv_dma_handle, page_dma_handle;
	ddi_acc_handle_t recv_accessp, page_accessp;
	size_t recv_alloc_len, page_alloc_len;
	msg_config_reply_t *configreply;
	config_page_ioc_2_t *iocpage;
	int recv_numbytes;
	int length, i;
	int rval = (-1);
	caddr_t recv_memp, page_memp;
	uint32_t flagslength;
	uint8_t numvolumes;
	uint16_t volid;
	mpt_slots_t *slots = mpt->m_active;
	int recv_dmastate = 0;
	int page_dmastate = 0;

	if (mpt_send_config_request_msg(mpt, MPI_CONFIG_ACTION_PAGE_HEADER,
	    MPI_CONFIG_PAGETYPE_IOC, 0, 2, 0, 0, 0, 0)) {
		goto done;
	}

	/*
	 * dynamically create a customized dma attribute structure
	 * that describes the MPT's config reply page request structure.
	 */
	recv_dma_attrs = mpt->m_msg_dma_attr;
	recv_dma_attrs.dma_attr_sgllen = 1;
	recv_dma_attrs.dma_attr_granular =
	    (sizeof (struct msg_config_reply));

	if (ddi_dma_alloc_handle(mpt->m_dip, &recv_dma_attrs,
	    DDI_DMA_SLEEP, NULL, &recv_dma_handle) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN,
		    "(unable to allocate dma handle.");
		goto done;
	}
	recv_dmastate |= MPT_DMA_HANDLE_ALLOCD;

	if (ddi_dma_mem_alloc(recv_dma_handle,
	    (sizeof (struct msg_config_reply)),
	    &mpt->m_dev_acc_attr, DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, NULL,
	    &recv_memp, &recv_alloc_len, &recv_accessp) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN,
		    "unable to allocate config_reply structure.");
		goto done;
	}
	recv_dmastate |= MPT_DMA_MEMORY_ALLOCD;

	if (ddi_dma_addr_bind_handle(recv_dma_handle, NULL, recv_memp,
	    recv_alloc_len, DDI_DMA_RDWR | DDI_DMA_CONSISTENT, DDI_DMA_SLEEP,
	    NULL, &recv_cookie, &recv_ncookie) != DDI_DMA_MAPPED) {
		mpt_log(mpt, CE_WARN, "unable to bind DMA resources.");
		goto done;
	}
	recv_dmastate |= MPT_DMA_HANDLE_BOUND;
	bzero(recv_memp, sizeof (*configreply));
	configreply = (void *)recv_memp;
	recv_numbytes = sizeof (*configreply);

	/*
	 * get config reply message
	 */
	if (mpt_get_handshake_msg(mpt, recv_memp, recv_numbytes,
	    recv_accessp)) {
		goto done;
	}

	if (mpt_handle_ioc_status(mpt, recv_accessp, &configreply->IOCStatus,
	    &configreply->IOCLogInfo, "mpt_get_raid_info header", 1)) {
		goto done;
	}

	length = (ddi_get8(recv_accessp, &configreply->Header.PageLength) * 4);

	/*
	 * dynamically create a customized dma attribute structure
	 * that describes the MPT's config page structure.
	 */
	page_dma_attrs = mpt->m_msg_dma_attr;
	page_dma_attrs.dma_attr_sgllen = 1;
	page_dma_attrs.dma_attr_granular = length;

	if (ddi_dma_alloc_handle(mpt->m_dip, &page_dma_attrs,
	    DDI_DMA_SLEEP, NULL, &page_dma_handle) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN,
		    "(unable to allocate dma handle.");
		goto done;
	}
	page_dmastate |= MPT_DMA_HANDLE_ALLOCD;
	if (ddi_dma_mem_alloc(page_dma_handle, length,
	    &mpt->m_dev_acc_attr, DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, NULL,
	    &page_memp, &page_alloc_len, &page_accessp) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN,
		    "unable to allocate config page structure.");
		goto done;
	}
	page_dmastate |= MPT_DMA_MEMORY_ALLOCD;
	if (ddi_dma_addr_bind_handle(page_dma_handle, NULL, page_memp,
	    page_alloc_len, DDI_DMA_RDWR | DDI_DMA_CONSISTENT, DDI_DMA_SLEEP,
	    NULL, &page_cookie, &page_ncookie) != DDI_DMA_MAPPED) {
		mpt_log(mpt, CE_WARN, "unable to bind DMA resources.");
		goto done;
	}
	page_dmastate |= MPT_DMA_HANDLE_BOUND;
	bzero(page_memp, length);
	iocpage = (void *)page_memp;
	flagslength = length;

	/*
	 * set up scatter gather element flags
	 */
	flagslength |= ((uint32_t)(MPI_SGE_FLAGS_LAST_ELEMENT |
	    MPI_SGE_FLAGS_END_OF_BUFFER |
	    MPI_SGE_FLAGS_SIMPLE_ELEMENT |
	    MPI_SGE_FLAGS_SYSTEM_ADDRESS |
	    MPI_SGE_FLAGS_32_BIT_ADDRESSING |
	    MPI_SGE_FLAGS_IOC_TO_HOST |
	    MPI_SGE_FLAGS_END_OF_LIST) << MPI_SGE_FLAGS_SHIFT);

	/*
	 * Give reply address to IOC to store config page in and send
	 * config request out.
	 */

	if (mpt_send_config_request_msg(mpt,
	    MPI_CONFIG_ACTION_PAGE_READ_CURRENT,
	    MPI_CONFIG_PAGETYPE_IOC, 0, 2,
	    ddi_get8(recv_accessp, &configreply->Header.PageVersion),
	    (length / 4), flagslength, page_cookie.dmac_address)) {
		goto done;
	}

	/*
	 * get reply view handshake
	 */
	if (mpt_get_handshake_msg(mpt, recv_memp, recv_numbytes,
	    recv_accessp)) {
		goto done;
	}

	if (mpt_handle_ioc_status(mpt, page_accessp, &configreply->IOCStatus,
	    &configreply->IOCLogInfo, "mpt_get_raid_info config", 1)) {
		goto done;
	}

	numvolumes = ddi_get8(page_accessp, &iocpage->NumActiveVolumes);
	for (i = 0; i < numvolumes; i++) {
		if (MPT_IS_SAS(mpt)) {
			volid = BUSTARG_TO_BT(
			    ddi_get8(page_accessp,
			    &iocpage->RaidVolume[i].VolumeBus),
			    ddi_get8(page_accessp,
			    &iocpage->RaidVolume[i].VolumeID));
		} else {
			volid = ddi_get8(page_accessp,
			    &iocpage->RaidVolume[i].VolumeID);
		}

		slots->m_raidvol[i].m_israid = 1;
		slots->m_raidvol[i].m_raidtarg = volid;

		/*
		 * Get the settings for the raid volume
		 * this includes the real target id's
		 * for the disks making up the raid
		 */
		if (mpt_get_raid_settings(mpt, i, volid))
			goto done;

		/*
		 * Get the WWID of the RAID volume for SAS HBA
		 */
		if (MPT_IS_SAS(mpt) && mpt_get_raid_wwid(mpt, i, volid))
			goto done;
	}

	if ((mpt_check_dma_handle(recv_dma_handle) != DDI_SUCCESS) ||
	    (mpt_check_dma_handle(page_dma_handle) != DDI_SUCCESS)) {
		ddi_fm_service_impact(mpt->m_dip,
		    DDI_SERVICE_UNAFFECTED);
		rval = -1;
	} else {
		rval = 0;
	}
done:
	/*
	 * free up memory
	 */
	if (recv_dmastate & MPT_DMA_HANDLE_BOUND)
		(void) ddi_dma_unbind_handle(recv_dma_handle);
	if (page_dmastate & MPT_DMA_HANDLE_BOUND)
		(void) ddi_dma_unbind_handle(page_dma_handle);
	if (recv_dmastate & MPT_DMA_MEMORY_ALLOCD)
		(void) ddi_dma_mem_free(&recv_accessp);
	if (page_dmastate & MPT_DMA_MEMORY_ALLOCD)
		(void) ddi_dma_mem_free(&page_accessp);
	if (recv_dmastate & MPT_DMA_HANDLE_ALLOCD)
		ddi_dma_free_handle(&recv_dma_handle);
	if (page_dmastate & MPT_DMA_HANDLE_ALLOCD)
		ddi_dma_free_handle(&page_dma_handle);
	return (rval);
}

int
mpt_get_raid_settings(mpt_t *mpt, int vol, uint16_t volid)
{
	ddi_dma_attr_t recv_dma_attrs, page_dma_attrs;
	uint_t recv_ncookie, page_ncookie;
	ddi_dma_cookie_t recv_cookie, page_cookie;
	ddi_dma_handle_t recv_dma_handle, page_dma_handle;
	ddi_acc_handle_t recv_accessp, page_accessp;
	size_t recv_alloc_len, page_alloc_len;
	msg_config_reply_t *configreply;
	config_page_raid_vol_0_t *raidpage;
	int recv_numbytes, i;
	caddr_t recv_memp, page_memp;
	uint32_t flagslength;
	uint8_t numdisks, volflags, volstate, voltype, physdisknum;
	mpt_slots_t *slots = mpt->m_active;
	int recv_dmastate = 0;
	int page_dmastate = 0;
	int rval = (-1);

	if (mpt_send_config_request_msg(mpt, MPI_CONFIG_ACTION_PAGE_HEADER,
	    MPI_CONFIG_PAGETYPE_RAID_VOLUME, volid, 0, 0, 0, 0, 0)) {
		goto done;
	}

	/*
	 * dynamically create a customized dma attribute structure
	 * that describes the MPT's config reply page request structure.
	 */
	recv_dma_attrs = mpt->m_msg_dma_attr;
	recv_dma_attrs.dma_attr_sgllen = 1;
	recv_dma_attrs.dma_attr_granular = (sizeof (struct msg_config_reply));

	if (ddi_dma_alloc_handle(mpt->m_dip, &recv_dma_attrs,
	    DDI_DMA_SLEEP, NULL, &recv_dma_handle) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN,
		    "(unable to allocate dma handle.");
		goto done;
	}
	recv_dmastate |= MPT_DMA_HANDLE_ALLOCD;

	if (ddi_dma_mem_alloc(recv_dma_handle,
	    (sizeof (struct msg_config_reply)),
	    &mpt->m_dev_acc_attr, DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, NULL,
	    &recv_memp, &recv_alloc_len, &recv_accessp) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN,
		    "unable to allocate config_reply structure.");
		goto done;
	}
	recv_dmastate |= MPT_DMA_MEMORY_ALLOCD;

	if (ddi_dma_addr_bind_handle(recv_dma_handle, NULL, recv_memp,
	    recv_alloc_len, DDI_DMA_RDWR | DDI_DMA_CONSISTENT, DDI_DMA_SLEEP,
	    NULL, &recv_cookie, &recv_ncookie) != DDI_DMA_MAPPED) {
		mpt_log(mpt, CE_WARN, "unable to bind DMA resources.");
		goto done;
	}
	recv_dmastate |= MPT_DMA_HANDLE_BOUND;
	bzero(recv_memp, sizeof (*configreply));
	configreply = (void *)recv_memp;
	recv_numbytes = sizeof (*configreply);

	/*
	 * get config reply message
	 */
	if (mpt_get_handshake_msg(mpt, recv_memp, recv_numbytes,
	    recv_accessp)) {
		goto done;
	}

	if (mpt_handle_ioc_status(mpt, recv_accessp, &configreply->IOCStatus,
	    &configreply->IOCLogInfo, "mpt_get_raid_settings header", 1)) {
		goto done;
	}

	/*
	 * dynamically create a customized dma attribute structure
	 * that describes the MPT's config page structure.
	 */
	page_dma_attrs = mpt->m_msg_dma_attr;
	page_dma_attrs.dma_attr_sgllen = 1;
	page_dma_attrs.dma_attr_granular = (sizeof (uint32_t[256]));

	if (ddi_dma_alloc_handle(mpt->m_dip, &page_dma_attrs,
	    DDI_DMA_SLEEP, NULL, &page_dma_handle) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN,
		    "(unable to allocate dma handle.");
		goto done;
	}
	page_dmastate |= MPT_DMA_HANDLE_ALLOCD;

	if (ddi_dma_mem_alloc(page_dma_handle,
	    (sizeof (uint32_t[256])),
	    &mpt->m_dev_acc_attr, DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, NULL,
	    &page_memp, &page_alloc_len, &page_accessp) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN,
		    "unable to allocate config page structure.");
		goto done;
	}
	page_dmastate |= MPT_DMA_MEMORY_ALLOCD;

	if (ddi_dma_addr_bind_handle(page_dma_handle, NULL, page_memp,
	    page_alloc_len, DDI_DMA_RDWR | DDI_DMA_CONSISTENT, DDI_DMA_SLEEP,
	    NULL, &page_cookie, &page_ncookie) != DDI_DMA_MAPPED) {
		mpt_log(mpt, CE_WARN, "unable to bind DMA resources.");
		goto done;
	}
	page_dmastate |= MPT_DMA_HANDLE_BOUND;
	bzero(page_memp, sizeof (uint32_t[256]));
	raidpage = (void *)page_memp;
	flagslength = sizeof (uint32_t[256]);

	/*
	 * set up scatter gather element flags
	 */
	flagslength |= ((uint32_t)(MPI_SGE_FLAGS_LAST_ELEMENT |
	    MPI_SGE_FLAGS_END_OF_BUFFER |
	    MPI_SGE_FLAGS_SIMPLE_ELEMENT |
	    MPI_SGE_FLAGS_SYSTEM_ADDRESS |
	    MPI_SGE_FLAGS_32_BIT_ADDRESSING |
	    MPI_SGE_FLAGS_IOC_TO_HOST |
	    MPI_SGE_FLAGS_END_OF_LIST) << MPI_SGE_FLAGS_SHIFT);

	/*
	 * Give reply address to IOC to store config page in and send
	 * config request out.
	 */

	if (mpt_send_config_request_msg(mpt,
	    MPI_CONFIG_ACTION_PAGE_READ_CURRENT,
	    MPI_CONFIG_PAGETYPE_RAID_VOLUME, volid, 0,
	    ddi_get8(recv_accessp, &configreply->Header.PageVersion),
	    ddi_get8(recv_accessp, &configreply->Header.PageLength),
	    flagslength, page_cookie.dmac_address)) {
		goto done;
	}

	/*
	 * get reply view handshake
	 */
	if (mpt_get_handshake_msg(mpt, recv_memp, recv_numbytes,
	    recv_accessp)) {
		goto done;
	}

	if (mpt_handle_ioc_status(mpt, recv_accessp, &configreply->IOCStatus,
	    &configreply->IOCLogInfo, "mpt_get_raid_settings config", 1)) {
		goto done;
	}


	volstate = ddi_get8(page_accessp, &raidpage->VolumeStatus.State);
	volflags = ddi_get8(page_accessp, &raidpage->VolumeStatus.Flags);
	voltype = ddi_get8(page_accessp, &raidpage->VolumeType);
	slots->m_raidvol[vol].m_state = volstate;
	slots->m_raidvol[vol].m_flags = volflags;
	slots->m_raidvol[vol].m_raidsize = ddi_get32(page_accessp,
	    &raidpage->MaxLBA);

	if (voltype == MPI_RAID_VOL_TYPE_IS) {
		slots->m_raidvol[vol].m_raidlevel = 0;
	} else if (voltype == MPI_RAID_VOL_TYPE_IM) {
		slots->m_raidvol[vol].m_raidlevel = 1;
	}

	if (volflags & MPI_RAIDVOL0_STATUS_FLAG_QUIESCED) {
		mpt_log(mpt, CE_NOTE, "?Volume %d is quiesced\n", volid);
	}

	if (volflags & MPI_RAIDVOL0_STATUS_FLAG_RESYNC_IN_PROGRESS) {
		mpt_log(mpt, CE_NOTE, "?Volume %d is resyncing\n", volid);
	}

	switch (volstate) {
		case MPI_RAIDVOL0_STATUS_STATE_OPTIMAL:
			mpt_log(mpt, CE_NOTE, "?Volume %d is optimal\n", volid);
			break;
		case MPI_RAIDVOL0_STATUS_STATE_DEGRADED:
			if ((volflags &
			    MPI_RAIDVOL0_STATUS_FLAG_RESYNC_IN_PROGRESS) == 0) {
				mpt_log(mpt, CE_WARN, "Volume %d is degraded\n",
				    volid);
			}
			break;
		case MPI_RAIDVOL0_STATUS_STATE_FAILED:
			mpt_log(mpt, CE_WARN, "Volume %d is failed\n", volid);
			break;
		case MPI_RAIDVOL0_STATUS_STATE_MISSING:
			mpt_log(mpt, CE_WARN, "Volume %d is missing\n", volid);
			break;
		default:
			break;
	}
	numdisks = ddi_get8(page_accessp, &raidpage->NumPhysDisks);
	slots->m_raidvol[vol].m_ndisks = numdisks;

	for (i = 0; i < numdisks; i++) {
		physdisknum = ddi_get8(page_accessp,
		    &raidpage->PhysDisk[i].PhysDiskNum);

		slots->m_raidvol[vol].m_disknum[i] = physdisknum;

		if (mpt_get_physdisk_settings(mpt, vol, physdisknum))
			goto done;
	}
	if ((mpt_check_dma_handle(recv_dma_handle) != DDI_SUCCESS) ||
	    (mpt_check_dma_handle(page_dma_handle) != DDI_SUCCESS)) {
		ddi_fm_service_impact(mpt->m_dip,
		    DDI_SERVICE_UNAFFECTED);
		rval = DDI_FAILURE;
	} else {
		rval = 0;
	}
done:
	if (recv_dmastate & MPT_DMA_HANDLE_BOUND)
		(void) ddi_dma_unbind_handle(recv_dma_handle);
	if (page_dmastate & MPT_DMA_HANDLE_BOUND)
		(void) ddi_dma_unbind_handle(page_dma_handle);
	if (recv_dmastate & MPT_DMA_MEMORY_ALLOCD)
		(void) ddi_dma_mem_free(&recv_accessp);
	if (page_dmastate & MPT_DMA_MEMORY_ALLOCD)
		(void) ddi_dma_mem_free(&page_accessp);
	if (recv_dmastate & MPT_DMA_HANDLE_ALLOCD)
		ddi_dma_free_handle(&recv_dma_handle);
	if (page_dmastate & MPT_DMA_HANDLE_ALLOCD)
		ddi_dma_free_handle(&page_dma_handle);
	return (rval);
}

static int
mpt_get_raid_wwid(mpt_t *mpt, int vol, uint16_t volid)
{
	ddi_dma_attr_t recv_dma_attrs, page_dma_attrs;
	uint_t recv_ncookie, page_ncookie;
	ddi_dma_cookie_t recv_cookie, page_cookie;
	ddi_dma_handle_t recv_dma_handle, page_dma_handle;
	ddi_acc_handle_t recv_accessp, page_accessp;
	size_t recv_alloc_len, page_alloc_len;
	msg_config_reply_t *configreply;
	config_page_raid_vol_1_t *raidpage;
	int recv_numbytes, i;
	caddr_t recv_memp, page_memp;
	uint32_t flagslength;
	mpt_slots_t *slots = mpt->m_active;
	int recv_dmastate = 0;
	int page_dmastate = 0;
	int rval = (-1);
	uint8_t *sas_addr = NULL;
	uint8_t tmp_sas_wwn[SAS_WWN_BYTE_SIZE];
	uint64_t *sas_wwn;

	if (mpt_send_config_request_msg(mpt, MPI_CONFIG_ACTION_PAGE_HEADER,
	    MPI_CONFIG_PAGETYPE_RAID_VOLUME, volid, 1, 0, 0, 0, 0)) {
		goto done;
	}

	/*
	 * dynamically create a customized dma attribute structure
	 * that describes the MPT's config reply page request structure.
	 */
	recv_dma_attrs = mpt->m_msg_dma_attr;
	recv_dma_attrs.dma_attr_sgllen = 1;
	recv_dma_attrs.dma_attr_granular = (sizeof (struct msg_config_reply));

	if (ddi_dma_alloc_handle(mpt->m_dip, &recv_dma_attrs,
	    DDI_DMA_SLEEP, NULL, &recv_dma_handle) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN,
		    "(unable to allocate dma handle.");
		goto done;
	}
	recv_dmastate |= MPT_DMA_HANDLE_ALLOCD;

	if (ddi_dma_mem_alloc(recv_dma_handle,
	    (sizeof (struct msg_config_reply)),
	    &mpt->m_dev_acc_attr, DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, NULL,
	    &recv_memp, &recv_alloc_len, &recv_accessp) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN,
		    "unable to allocate config_reply structure.");
		goto done;
	}
	recv_dmastate |= MPT_DMA_MEMORY_ALLOCD;

	if (ddi_dma_addr_bind_handle(recv_dma_handle, NULL, recv_memp,
	    recv_alloc_len, DDI_DMA_RDWR | DDI_DMA_CONSISTENT, DDI_DMA_SLEEP,
	    NULL, &recv_cookie, &recv_ncookie) != DDI_DMA_MAPPED) {
		mpt_log(mpt, CE_WARN, "unable to bind DMA resources.");
		goto done;
	}
	recv_dmastate |= MPT_DMA_HANDLE_BOUND;
	bzero(recv_memp, sizeof (*configreply));
	configreply = (void *)recv_memp;
	recv_numbytes = sizeof (*configreply);

	/*
	 * get config reply message
	 */
	if (mpt_get_handshake_msg(mpt, recv_memp, recv_numbytes,
	    recv_accessp)) {
		goto done;
	}

	if (mpt_handle_ioc_status(mpt, recv_accessp, &configreply->IOCStatus,
	    &configreply->IOCLogInfo, "mpt_get_raid_wwid header", 1)) {
		goto done;
	}

	/*
	 * dynamically create a customized dma attribute structure
	 * that describes the MPT's config page structure.
	 */
	page_dma_attrs = mpt->m_msg_dma_attr;
	page_dma_attrs.dma_attr_sgllen = 1;
	page_dma_attrs.dma_attr_granular = sizeof (config_page_raid_vol_1_t);

	if (ddi_dma_alloc_handle(mpt->m_dip, &page_dma_attrs,
	    DDI_DMA_SLEEP, NULL, &page_dma_handle) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN,
		    "(unable to allocate dma handle.");
		goto done;
	}
	page_dmastate |= MPT_DMA_HANDLE_ALLOCD;

	if (ddi_dma_mem_alloc(page_dma_handle,
	    sizeof (config_page_raid_vol_1_t),
	    &mpt->m_dev_acc_attr, DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, NULL,
	    &page_memp, &page_alloc_len, &page_accessp) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN,
		    "unable to allocate config page structure.");
		goto done;
	}
	page_dmastate |= MPT_DMA_MEMORY_ALLOCD;

	if (ddi_dma_addr_bind_handle(page_dma_handle, NULL, page_memp,
	    page_alloc_len, DDI_DMA_RDWR | DDI_DMA_CONSISTENT, DDI_DMA_SLEEP,
	    NULL, &page_cookie, &page_ncookie) != DDI_DMA_MAPPED) {
		mpt_log(mpt, CE_WARN, "unable to bind DMA resources.");
		goto done;
	}
	page_dmastate |= MPT_DMA_HANDLE_BOUND;
	bzero(page_memp, sizeof (config_page_raid_vol_1_t));
	raidpage = (void *)page_memp;
	flagslength = sizeof (config_page_raid_vol_1_t);

	/*
	 * set up scatter gather element flags
	 */
	flagslength |= ((uint32_t)(MPI_SGE_FLAGS_LAST_ELEMENT |
	    MPI_SGE_FLAGS_END_OF_BUFFER |
	    MPI_SGE_FLAGS_SIMPLE_ELEMENT |
	    MPI_SGE_FLAGS_SYSTEM_ADDRESS |
	    MPI_SGE_FLAGS_32_BIT_ADDRESSING |
	    MPI_SGE_FLAGS_IOC_TO_HOST |
	    MPI_SGE_FLAGS_END_OF_LIST) << MPI_SGE_FLAGS_SHIFT);

	/*
	 * Give reply address to IOC to store config page in and send
	 * config request out.
	 */

	if (mpt_send_config_request_msg(mpt,
	    MPI_CONFIG_ACTION_PAGE_READ_CURRENT,
	    MPI_CONFIG_PAGETYPE_RAID_VOLUME, volid, 1,
	    ddi_get8(recv_accessp, &configreply->Header.PageVersion),
	    ddi_get8(recv_accessp, &configreply->Header.PageLength),
	    flagslength, page_cookie.dmac_address)) {
		goto done;
	}

	/*
	 * get reply view handshake
	 */
	if (mpt_get_handshake_msg(mpt, recv_memp, recv_numbytes,
	    recv_accessp)) {
		goto done;
	}

	if (mpt_handle_ioc_status(mpt, recv_accessp, &configreply->IOCStatus,
	    &configreply->IOCLogInfo, "mpt_get_raid_wwid", 1)) {
		goto done;
	}

	/*
	 * get wwid of RAID volume
	 */
	sas_wwn = (void *)tmp_sas_wwn;
	sas_addr = (uint8_t *)(&raidpage->WWID);
	for (i = 0; i < SAS_WWN_BYTE_SIZE; i++) {
		tmp_sas_wwn[i] = ddi_get8(page_accessp, sas_addr + i);
	}
	/*
	 * replace top nibble of WWID of RAID to '3' for OBP
	 */
	*sas_wwn = MPT_RAID_WWID(LE_64(*sas_wwn));

	slots->m_raidvol[vol].m_raidwwid = *sas_wwn;

	if ((mpt_check_dma_handle(recv_dma_handle) != DDI_SUCCESS) ||
	    (mpt_check_dma_handle(page_dma_handle) != DDI_SUCCESS)) {
		ddi_fm_service_impact(mpt->m_dip,
		    DDI_SERVICE_UNAFFECTED);
		rval = DDI_FAILURE;
	} else {
		rval = 0;
	}
done:
	if (recv_dmastate & MPT_DMA_HANDLE_BOUND)
		(void) ddi_dma_unbind_handle(recv_dma_handle);
	if (page_dmastate & MPT_DMA_HANDLE_BOUND)
		(void) ddi_dma_unbind_handle(page_dma_handle);
	if (recv_dmastate & MPT_DMA_MEMORY_ALLOCD)
		(void) ddi_dma_mem_free(&recv_accessp);
	if (page_dmastate & MPT_DMA_MEMORY_ALLOCD)
		(void) ddi_dma_mem_free(&page_accessp);
	if (recv_dmastate & MPT_DMA_HANDLE_ALLOCD)
		ddi_dma_free_handle(&recv_dma_handle);
	if (page_dmastate & MPT_DMA_HANDLE_ALLOCD)
		ddi_dma_free_handle(&page_dma_handle);
	return (rval);
}

int
mpt_get_physdisk_settings(mpt_t *mpt, int vol, uint8_t physdisknum)
{
	ddi_dma_attr_t recv_dma_attrs, page_dma_attrs;
	uint_t recv_ncookie, page_ncookie;
	ddi_dma_cookie_t recv_cookie, page_cookie;
	ddi_dma_handle_t recv_dma_handle, page_dma_handle;
	ddi_acc_handle_t recv_accessp, page_accessp;
	size_t recv_alloc_len, page_alloc_len;
	msg_config_reply_t *configreply;
	config_page_raid_phys_disk_0_t *diskpage;
	int recv_numbytes, i;
	caddr_t recv_memp, page_memp;
	uint32_t flagslength;
	uint8_t state;
	uint16_t diskid, physdiskid;
	mpt_slots_t *slots = mpt->m_active;
	int recv_dmastate = 0;
	int page_dmastate = 0;
	int rval = (-1);

	if (mpt_send_config_request_msg(mpt, MPI_CONFIG_ACTION_PAGE_HEADER,
	    MPI_CONFIG_PAGETYPE_RAID_PHYSDISK, physdisknum, 0, 0, 0,
	    0, 0)) {
		goto done;
	}

	/*
	 * dynamically create a customized dma attribute structure
	 * that describes the MPT's config reply page request structure.
	 */
	recv_dma_attrs = mpt->m_msg_dma_attr;
	recv_dma_attrs.dma_attr_sgllen = 1;
	recv_dma_attrs.dma_attr_granular =
	    (sizeof (struct msg_config_reply));

	if (ddi_dma_alloc_handle(mpt->m_dip, &recv_dma_attrs,
	    DDI_DMA_SLEEP, NULL, &recv_dma_handle) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN,
		    "(unable to allocate dma handle.");
		goto done;
	}
	recv_dmastate |= MPT_DMA_HANDLE_ALLOCD;

	if (ddi_dma_mem_alloc(recv_dma_handle,
	    (sizeof (struct msg_config_reply)),
	    &mpt->m_dev_acc_attr, DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, NULL,
	    &recv_memp, &recv_alloc_len, &recv_accessp) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN,
		    "unable to allocate config_reply structure.");
		goto done;
	}
	recv_dmastate |= MPT_DMA_MEMORY_ALLOCD;

	if (ddi_dma_addr_bind_handle(recv_dma_handle, NULL, recv_memp,
	    recv_alloc_len, DDI_DMA_RDWR | DDI_DMA_CONSISTENT, DDI_DMA_SLEEP,
	    NULL, &recv_cookie, &recv_ncookie) != DDI_DMA_MAPPED) {
		mpt_log(mpt, CE_WARN, "unable to bind DMA resources.");
		goto done;
	}
	recv_dmastate |= MPT_DMA_HANDLE_BOUND;
	bzero(recv_memp, sizeof (*configreply));
	configreply = (void *)recv_memp;
	recv_numbytes = sizeof (*configreply);

	/*
	 * get config reply message
	 */
	if (mpt_get_handshake_msg(mpt, recv_memp, recv_numbytes,
	    recv_accessp)) {
		goto done;
	}

	if (mpt_handle_ioc_status(mpt, recv_accessp, &configreply->IOCStatus,
	    &configreply->IOCLogInfo, "mpt_get_physdisk_settings header", 1)) {
		goto done;
	}

	/*
	 * dynamically create a customized dma attribute structure
	 * that describes the MPT's config page structure.
	 */
	page_dma_attrs = mpt->m_msg_dma_attr;
	page_dma_attrs.dma_attr_sgllen = 1;
	page_dma_attrs.dma_attr_granular =
	    (sizeof (struct config_page_raid_phys_disk_0));

	if (ddi_dma_alloc_handle(mpt->m_dip, &page_dma_attrs,
	    DDI_DMA_SLEEP, NULL, &page_dma_handle) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN,
		    "(unable to allocate dma handle.");
		goto done;
	}
	page_dmastate |= MPT_DMA_HANDLE_ALLOCD;

	if (ddi_dma_mem_alloc(page_dma_handle,
	    (sizeof (struct config_page_raid_phys_disk_0)),
	    &mpt->m_dev_acc_attr, DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, NULL,
	    &page_memp, &page_alloc_len, &page_accessp) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN,
		    "unable to allocate config page structure.");
		goto done;
	}
	page_dmastate |= MPT_DMA_MEMORY_ALLOCD;

	if (ddi_dma_addr_bind_handle(page_dma_handle, NULL, page_memp,
	    page_alloc_len, DDI_DMA_RDWR | DDI_DMA_CONSISTENT, DDI_DMA_SLEEP,
	    NULL, &page_cookie, &page_ncookie) != DDI_DMA_MAPPED) {
		mpt_log(mpt, CE_WARN, "unable to bind DMA resources.");
		goto done;
	}
	page_dmastate |= MPT_DMA_HANDLE_BOUND;
	bzero(page_memp, sizeof (config_page_raid_phys_disk_0_t));
	diskpage = (void *)page_memp;
	flagslength = sizeof (struct config_page_raid_phys_disk_0);

	/*
	 * set up scatter gather element flags
	 */
	flagslength |= ((uint32_t)(MPI_SGE_FLAGS_LAST_ELEMENT |
	    MPI_SGE_FLAGS_END_OF_BUFFER |
	    MPI_SGE_FLAGS_SIMPLE_ELEMENT |
	    MPI_SGE_FLAGS_SYSTEM_ADDRESS |
	    MPI_SGE_FLAGS_32_BIT_ADDRESSING |
	    MPI_SGE_FLAGS_IOC_TO_HOST |
	    MPI_SGE_FLAGS_END_OF_LIST) << MPI_SGE_FLAGS_SHIFT);

	/*
	 * Give reply address to IOC to store config page in and send
	 * config request out.
	 */

	if (mpt_send_config_request_msg(mpt,
	    MPI_CONFIG_ACTION_PAGE_READ_CURRENT,
	    MPI_CONFIG_PAGETYPE_RAID_PHYSDISK, physdisknum, 0,
	    ddi_get8(recv_accessp, &configreply->Header.PageVersion),
	    ddi_get8(recv_accessp, &configreply->Header.PageLength),
	    flagslength, page_cookie.dmac_address)) {
		goto done;
	}

	/*
	 * get reply view handshake
	 */
	if (mpt_get_handshake_msg(mpt, recv_memp, recv_numbytes,
	    recv_accessp)) {
		goto done;
	}

	if (mpt_handle_ioc_status(mpt, recv_accessp, &configreply->IOCStatus,
	    &configreply->IOCLogInfo, "mpt_get_physdisk_settings config", 1)) {
		goto done;
	}

	/*
	 * get real target id's for physical disk
	 */
	if (MPT_IS_SCSI(mpt)) {
		/* scsi HBA, just read the PhysDiskID */
		diskid = ddi_get8(page_accessp,
		    &diskpage->PhysDiskID);
	} else {
		/*
		 * SAS HBA - this is a bit trickier. the LSI firmware
		 * uses an internal TargetID for disks that are members
		 * of a RAID volume on a SAS HBA, so we need to find out
		 * the id of this disk in another way.  Since we use direct
		 * mapping mode, we can just use the PhyNum for the id
		 */
		physdiskid = BUSTARG_TO_BT(
		    ddi_get8(page_accessp, &diskpage->PhysDiskBus),
		    ddi_get8(page_accessp, &diskpage->PhysDiskID));

		if (physdiskid == 255) {
			/*
			 * disk is missing
			 */
			diskid = physdiskid;
		} else
			diskid = mpt_get_sas_device_phynum(mpt,
			    BT_TO_BUS(physdiskid), BT_TO_TARG(physdiskid));
	}

	state = ddi_get8(page_accessp, &diskpage->PhysDiskStatus.State);

	/*
	 * Store disk information in the appropriate location
	 */
	for (i = 0; i < MPT_MAX_DISKS_IN_RAID; i++) {
		/* find the correct position in the arrays */
		if (slots->m_raidvol[vol].m_disknum[i] == physdisknum)
			break;
	}

	slots->m_raidvol[vol].m_diskid[i] = diskid;

	switch (state) {
		case MPI_PHYSDISK0_STATUS_MISSING:
			slots->m_raidvol[vol].m_diskstatus[i] =
			    RAID_DISKSTATUS_MISSING;
			break;
		case MPI_PHYSDISK0_STATUS_NOT_COMPATIBLE:
		case MPI_PHYSDISK0_STATUS_FAILED:
		case MPI_PHYSDISK0_STATUS_FAILED_REQUESTED:
			slots->m_raidvol[vol].m_diskstatus[i] =
			    RAID_DISKSTATUS_FAILED;
			break;
		case MPI_PHYSDISK0_STATUS_ONLINE:
		default:
			slots->m_raidvol[vol].m_diskstatus[i] =
			    RAID_DISKSTATUS_GOOD;
			break;
	}
	if ((mpt_check_dma_handle(recv_dma_handle) != DDI_SUCCESS) ||
	    (mpt_check_dma_handle(page_dma_handle) != DDI_SUCCESS)) {
		ddi_fm_service_impact(mpt->m_dip,
		    DDI_SERVICE_UNAFFECTED);
		rval = -1;
	} else {
		rval = 0;
	}
done:
	if (recv_dmastate & MPT_DMA_HANDLE_BOUND)
		(void) ddi_dma_unbind_handle(recv_dma_handle);
	if (page_dmastate & MPT_DMA_HANDLE_BOUND)
		(void) ddi_dma_unbind_handle(page_dma_handle);
	if (recv_dmastate & MPT_DMA_MEMORY_ALLOCD)
		(void) ddi_dma_mem_free(&recv_accessp);
	if (page_dmastate & MPT_DMA_MEMORY_ALLOCD)
		(void) ddi_dma_mem_free(&page_accessp);
	if (recv_dmastate & MPT_DMA_HANDLE_ALLOCD)
		ddi_dma_free_handle(&recv_dma_handle);
	if (page_dmastate & MPT_DMA_HANDLE_ALLOCD)
		ddi_dma_free_handle(&page_dma_handle);
	return (rval);
}

int
mpt_create_raid(mpt_t *mpt, mpt_disklist_t *disklist, uint16_t volid,
	int volsize, int raid_level)
{
	int rval = 0;
	int i, vol = (-1);

	mpt_slots_t *slots = mpt->m_active;

	if (MPT_IS_SCSI(mpt)) {
		/*
		 * This HBA is an LSI1030
		 *
		 * LSI1030 firmware requires 33 blocks on disk for
		 * metadata, and only supports a single IM volume
		 */
		if (MPT_RAID_EXISTS(mpt, 0)) {
			mpt_log(mpt, CE_WARN, "RAID volume already exists.");
			rval = (-1);
			goto done;
		}
		volsize = (volsize - 33);
		vol = 0;
	} else {
		/*
		 * This HBA is not a 1030, treat as an LSI1064
		 *
		 * LSI1064 firmware requires 131074 blocks per disk
		 * for metadata, and supports up to 2 IM or IS volumes
		 */
		for (i = 0; i < MPT_MAX_RAIDVOLS; i++) {
			if (MPT_RAID_EXISTS(mpt, i))
				continue;
			vol = i;
			break;
		}
		if (vol < 0) {
			mpt_log(mpt, CE_WARN, "Cannot create additional "
			    "RAID volumes on HBA");
			rval = (-1);
			goto done;
		}
		volsize = (volsize - (131074 * disklist->m_ndisks));
	}

	/*
	 * Indicate that we are currently building a raid volume.
	 * This is to stop an excess of event messages generated
	 * to the console
	 */
	slots->m_raidvol[vol].m_raidbuilding = 1;
	slots->m_raidvol[vol].m_ndisks = disklist->m_ndisks;

	/*
	 * Create physdisk page for each disk
	 */
	for (i = 0; i < disklist->m_ndisks; i++) {
		/*
		 * mark this vol so that mpt_process_intr can determine
		 * the lowest available slot to assign the disknum to
		 * we will use 255
		 */
		slots->m_raidvol[vol].m_disknum[i] = 255;

		if (mpt_create_physdisk(mpt, disklist->m_diskid[i])) {
			mpt_log(mpt, CE_WARN,
			    "unable to create physdisk page.");
			rval = (-1);
			goto done;
		}

		slots->m_raidvol[vol].m_diskid[i] = disklist->m_diskid[i];
	}

	if (mpt_create_volume(mpt, vol, raid_level, volid, volsize)) {
		mpt_log(mpt, CE_WARN, "unable to create raid volume.");
		rval = (-1);
		goto done;
	}

	slots->m_raidvol[vol].m_israid = 1;
	slots->m_raidvol[vol].m_raidtarg = volid;
	slots->m_raidvol[vol].m_raidsize = volsize;

	if (raid_level == MPI_RAID_VOL_TYPE_IS) {
		slots->m_raidvol[vol].m_raidlevel = 0;
	} else if (raid_level == MPI_RAID_VOL_TYPE_IM) {
		slots->m_raidvol[vol].m_raidlevel = 1;
	}

done:
	/*
	 * Indicate that we are done building the mirror
	 */
	slots->m_raidvol[vol].m_raidbuilding = 0;
	return (rval);
}

int
mpt_delete_volume(mpt_t *mpt, uint16_t volid)
{
	int i, vol = (-1);
	mpt_slots_t *slots = mpt->m_active;

	for (i = 0; i < MPT_MAX_RAIDVOLS; i++) {
		if (TGT_IS_RAID(mpt, i, volid)) {
			vol = i;
			break;
		}
	}

	if (vol < 0) {
		mpt_log(mpt, CE_WARN, "raid doesn't exist at"
		    " specified target.");
		return (-1);
	}

	/*
	 * Send raid action message to delete the mirror
	 */
	if (mpt_send_raid_action(mpt, MPI_RAID_ACTION_DELETE_VOLUME,
	    volid, 0, 0, 0, MPI_RAID_ACTION_ADATA_DEL_PHYS_DISKS)) {
		mpt_log(mpt, CE_WARN, "failed to delete volume.");
		return (-1);
	}

	slots->m_raidvol[vol].m_israid = 0;
	slots->m_raidvol[vol].m_ndisks = 0;

	return (0);
}

static int
mpt_create_physdisk(mpt_t *mpt, uint16_t targ)
{
	ddi_dma_attr_t page_dma_attrs;
	uint_t page_ncookie;
	ddi_dma_cookie_t page_cookie;
	ddi_dma_handle_t page_dma_handle;
	ddi_acc_handle_t page_accessp;
	size_t page_alloc_len;
	caddr_t page_memp;
	config_page_raid_phys_disk_0_t *physdiskpage;
	int rval = 0;
	uint32_t flagslength;

	/*
	 * dynamically create a customized dma attribute structure
	 * that describes the MPT's config page structure.
	 */
	page_dma_attrs = mpt->m_msg_dma_attr;
	page_dma_attrs.dma_attr_sgllen = 1;
	page_dma_attrs.dma_attr_granular =
	    (sizeof (struct config_page_raid_phys_disk_0));

	if (ddi_dma_alloc_handle(mpt->m_dip, &page_dma_attrs,
	    DDI_DMA_SLEEP, NULL, &page_dma_handle) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN,
		    "(unable to allocate dma handle.");
		return (-1);
	}

	if (ddi_dma_mem_alloc(page_dma_handle,
	    (sizeof (struct config_page_raid_phys_disk_0)),
	    &mpt->m_dev_acc_attr, DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, NULL,
	    &page_memp, &page_alloc_len, &page_accessp) != DDI_SUCCESS) {
		ddi_dma_free_handle(&page_dma_handle);
		mpt_log(mpt, CE_WARN,
		    "unable to allocate config page structure.");
		return (-1);
	}

	if (ddi_dma_addr_bind_handle(page_dma_handle, NULL, page_memp,
	    page_alloc_len, DDI_DMA_RDWR | DDI_DMA_CONSISTENT, DDI_DMA_SLEEP,
	    NULL, &page_cookie, &page_ncookie) != DDI_DMA_MAPPED) {
		(void) ddi_dma_mem_free(&page_accessp);
		ddi_dma_free_handle(&page_dma_handle);
		mpt_log(mpt, CE_WARN, "unable to bind DMA resources.");
		return (-1);
	}
	bzero(page_memp, sizeof (config_page_raid_phys_disk_0_t));
	physdiskpage = (void *)page_memp;

	/*
	 * Fill in physdisk page
	 */
	ddi_put8(page_accessp, &physdiskpage->Header.PageType,
	    MPI_CONFIG_PAGETYPE_RAID_PHYSDISK);
	ddi_put8(page_accessp, &physdiskpage->Header.PageLength,
	    (sizeof (config_page_raid_phys_disk_0_t)) / 4);
	ddi_put8(page_accessp, &physdiskpage->PhysDiskID, BT_TO_TARG(targ));
	ddi_put8(page_accessp, &physdiskpage->PhysDiskBus, BT_TO_BUS(targ));
	ddi_put8(page_accessp, &physdiskpage->PhysDiskIOC, mpt->m_ioc_num);

	flagslength = sizeof (struct config_page_raid_phys_disk_0);
	flagslength |= ((uint32_t)(MPI_SGE_FLAGS_LAST_ELEMENT |
	    MPI_SGE_FLAGS_END_OF_BUFFER |
	    MPI_SGE_FLAGS_SIMPLE_ELEMENT |
	    MPI_SGE_FLAGS_SYSTEM_ADDRESS |
	    MPI_SGE_FLAGS_32_BIT_ADDRESSING |
	    MPI_SGE_FLAGS_HOST_TO_IOC |
	    MPI_SGE_FLAGS_END_OF_LIST) << MPI_SGE_FLAGS_SHIFT);

	if (mpt_send_raid_action(mpt, MPI_RAID_ACTION_CREATE_PHYSDISK, 0,
	    0, flagslength, page_cookie.dmac_address, 0)) {
		rval = (-1);
	}

	/*
	 * free up memory
	 */
	(void) ddi_dma_unbind_handle(page_dma_handle);
	(void) ddi_dma_mem_free(&page_accessp);
	ddi_dma_free_handle(&page_dma_handle);

	return (rval);
}

static int
mpt_create_volume(mpt_t *mpt, int vol, uint8_t raid_level,
	uint16_t volid, uint32_t maxlba)
{
	ddi_dma_attr_t page_dma_attrs;
	uint_t page_ncookie;
	ddi_dma_cookie_t page_cookie;
	ddi_dma_handle_t page_dma_handle;
	ddi_acc_handle_t page_accessp;
	size_t page_alloc_len;
	caddr_t page_memp;
	config_page_raid_vol_0_t *raidpage;
	int rval = 0;
	int i, length, ndisks;
	uint32_t flagslength;
	mpt_slots_t *slots = mpt->m_active;

	ndisks = slots->m_raidvol[vol].m_ndisks;

	length = sizeof (config_page_raid_vol_0_t);

	/*
	 * config_page_raid_vol_0_t has an array of
	 * sizeof (raid_vol0_phys_disk_t) * MPI_RAID_VOL_PAGE_0_PHYSDISK_MAX
	 * already built in, so just account for more disks than
	 * MPI_RAID_VOL_PAGE_0_PHYSDISK_MAX.
	 */
	if (ndisks > MPI_RAID_VOL_PAGE_0_PHYSDISK_MAX) {
		length += (sizeof (raid_vol0_phys_disk_t) *
		    (ndisks-MPI_RAID_VOL_PAGE_0_PHYSDISK_MAX));
	}

	/*
	 * dynamically create a customized dma attribute structure
	 * that describes the MPT's config page structure.
	 */
	page_dma_attrs = mpt->m_msg_dma_attr;
	page_dma_attrs.dma_attr_sgllen = 1;
	page_dma_attrs.dma_attr_granular = length;

	if (ddi_dma_alloc_handle(mpt->m_dip, &page_dma_attrs,
	    DDI_DMA_SLEEP, NULL, &page_dma_handle) != DDI_SUCCESS) {
		mpt_log(mpt, CE_WARN, "(unable to allocate dma handle.");
		return (-1);
	}

	if (ddi_dma_mem_alloc(page_dma_handle, length,
	    &mpt->m_dev_acc_attr, DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, NULL,
	    &page_memp, &page_alloc_len, &page_accessp) != DDI_SUCCESS) {
		ddi_dma_free_handle(&page_dma_handle);
		mpt_log(mpt, CE_WARN,
		    "unable to allocate config page structure.");
		return (-1);
	}

	if (ddi_dma_addr_bind_handle(page_dma_handle, NULL, page_memp,
	    page_alloc_len, DDI_DMA_RDWR | DDI_DMA_CONSISTENT, DDI_DMA_SLEEP,
	    NULL, &page_cookie, &page_ncookie) != DDI_DMA_MAPPED) {
		(void) ddi_dma_mem_free(&page_accessp);
		ddi_dma_free_handle(&page_dma_handle);
		mpt_log(mpt, CE_WARN, "unable to bind DMA resources.");
		return (-1);
	}

	bzero(page_memp, length);
	raidpage = (void *)page_memp;

	/*
	 * Fill in raid page
	 */
	ddi_put8(page_accessp, &raidpage->Header.PageType,
	    MPI_CONFIG_PAGETYPE_RAID_VOLUME);
	ddi_put8(page_accessp, &raidpage->Header.PageLength, length / 4);
	ddi_put8(page_accessp, &raidpage->VolumeID, BT_TO_TARG(volid));
	ddi_put8(page_accessp, &raidpage->VolumeBus, BT_TO_BUS(volid));
	ddi_put8(page_accessp, &raidpage->VolumeIOC, mpt->m_ioc_num);
	ddi_put8(page_accessp, &raidpage->VolumeType, raid_level);
	ddi_put16(page_accessp, &raidpage->VolumeSettings.Settings,
	    MPI_RAIDVOL0_SETTING_OFFLINE_ON_SMART |
	    MPI_RAIDVOL0_SETTING_AUTO_CONFIGURE |
	    MPI_RAIDVOL0_SETTING_PRIORITY_RESYNC |
	    MPI_RAIDVOL0_SETTING_USE_PRODUCT_ID_SUFFIX);
	ddi_put8(page_accessp, &raidpage->NumPhysDisks, ndisks);

	/* populate the disk array */
	for (i = 0; i < ndisks; i++) {
		ddi_put8(page_accessp, &raidpage->PhysDisk[i].PhysDiskNum,
		    slots->m_raidvol[vol].m_disknum[i]);
	}

	/* set up the disk mapping */
	if (raid_level == MPI_RAID_VOL_TYPE_IM) {
		/*
		 * map which disk is primary, which is secondary
		 */
		ddi_put8(page_accessp, &raidpage->PhysDisk[0].PhysDiskMap,
		    MPI_RAIDVOL0_PHYSDISK_PRIMARY);
		ddi_put8(page_accessp, &raidpage->PhysDisk[1].PhysDiskMap,
		    MPI_RAIDVOL0_PHYSDISK_SECONDARY);
	} else {
		/*
		 * map the stripe columns, which range from 0 to
		 * ndisks-1, so use it's position in PhysDisk
		 */
		for (i = 0; i < ndisks; i++) {
			ddi_put8(page_accessp,
			    &raidpage->PhysDisk[i].PhysDiskMap, i);
		}
	}

	ddi_put32(page_accessp, &raidpage->MaxLBA, maxlba);

	flagslength = length;
	flagslength |= ((uint32_t)(MPI_SGE_FLAGS_LAST_ELEMENT |
	    MPI_SGE_FLAGS_END_OF_BUFFER |
	    MPI_SGE_FLAGS_SIMPLE_ELEMENT |
	    MPI_SGE_FLAGS_SYSTEM_ADDRESS |
	    MPI_SGE_FLAGS_32_BIT_ADDRESSING |
	    MPI_SGE_FLAGS_HOST_TO_IOC |
	    MPI_SGE_FLAGS_END_OF_LIST) << MPI_SGE_FLAGS_SHIFT);

	if (mpt_send_raid_action(mpt, MPI_RAID_ACTION_CREATE_VOLUME, volid,
	    0, flagslength, page_cookie.dmac_address, 0)) {
		rval = (-1);
	}

	/*
	 * Free up memory
	 */
	(void) ddi_dma_unbind_handle(page_dma_handle);
	(void) ddi_dma_mem_free(&page_accessp);
	ddi_dma_free_handle(&page_dma_handle);

	return (rval);
}
