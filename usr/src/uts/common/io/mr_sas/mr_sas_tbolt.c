/*
 * mr_sas_tbolt.c: source for mr_sas driver for New Generation.
 * i.e. Thunderbolt
 *
 * MegaRAID device driver for SAS2.0 controllers
 * Copyright (c) 2008-2010, LSI Logic Corporation.
 * All rights reserved.
 *
 * Version:
 * Author:
 *		Swaminathan K S
 *		Arun Chandrashekhar
 *
 */

/*
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/types.h>
#include <sys/file.h>
#include <sys/atomic.h>
#include <sys/scsi/scsi.h>
#include <sys/byteorder.h>
#include "ld_pd_map.h"
#include "mr_sas.h"
#include "fusion.h"

extern ddi_dma_attr_t mrsas_generic_dma_attr;
extern struct ddi_device_acc_attr endian_attr;
extern int 	debug_level_g;
extern unsigned int	enable_fp;

extern void
io_timeout_checker(void *arg);
extern int
mfi_state_transition_to_ready(struct mrsas_instance *instance);
extern volatile int  debug_timeout_g;
volatile int dump_io_wait_time = 90;
extern int 	mrsas_issue_pending_cmds(struct mrsas_instance *);
extern int mrsas_complete_pending_cmds(struct mrsas_instance *instance);

extern void	push_pending_mfi_pkt(struct mrsas_instance *,
			struct mrsas_cmd *);
static volatile int  debug_tbolt_fw_faults_after_ocr_g  = 0;

/*
 * destroy_mfi_mpi_frame_pool
 */
void
destroy_mfi_mpi_frame_pool(struct mrsas_instance *instance)
{
	int	i;

	struct mrsas_cmd	*cmd;

	/* return all mfi frames to pool */
	for (i = 0; i < APP_RESERVE_CMDS; i++) {
		cmd = instance->cmd_list[i];
		if (cmd->frame_dma_obj_status == DMA_OBJ_ALLOCATED)
			(void) mrsas_free_dma_obj(instance,
			    cmd->frame_dma_obj);
		cmd->frame_dma_obj_status  = DMA_OBJ_FREED;
	}
}

/*
 * destroy_mpi2_frame_pool
 */
void
destroy_mpi2_frame_pool(struct mrsas_instance *instance)
{

	if (instance->mpi2_frame_pool_dma_obj.status == DMA_OBJ_ALLOCATED) {
		(void) mrsas_free_dma_obj(instance,
		    instance->mpi2_frame_pool_dma_obj);
		instance->mpi2_frame_pool_dma_obj.status |= DMA_OBJ_FREED;
	}
}


/*
 * mrsas_tbolt_free_additional_dma_buffer
 */
void
mrsas_tbolt_free_additional_dma_buffer(struct mrsas_instance *instance)
{
	int i;
	if (instance->mfi_internal_dma_obj.status == DMA_OBJ_ALLOCATED) {
		(void) mrsas_free_dma_obj(instance,
		    instance->mfi_internal_dma_obj);
		instance->mfi_internal_dma_obj.status = DMA_OBJ_FREED;
	}
	if (instance->mfi_evt_detail_obj.status == DMA_OBJ_ALLOCATED) {
		(void) mrsas_free_dma_obj(instance,
		    instance->mfi_evt_detail_obj);
		instance->mfi_evt_detail_obj.status = DMA_OBJ_FREED;
	}

	for (i = 0; i < 2; i++) {
		if (instance->ld_map_obj[i].status == DMA_OBJ_ALLOCATED) {
			(void) mrsas_free_dma_obj(instance,
			    instance->ld_map_obj[i]);
			instance->ld_map_obj[i].status = DMA_OBJ_FREED;
		}
	}
}


/*
 * free_req_desc_pool
 */
void
free_req_rep_desc_pool(struct mrsas_instance *instance)
{
	if (instance->request_desc_dma_obj.status == DMA_OBJ_ALLOCATED) {
		(void) mrsas_free_dma_obj(instance,
		    instance->request_desc_dma_obj);
		instance->request_desc_dma_obj.status = DMA_OBJ_FREED;
	}

	if (instance->reply_desc_dma_obj.status == DMA_OBJ_ALLOCATED) {
		(void) mrsas_free_dma_obj(instance,
		    instance->reply_desc_dma_obj);
		instance->reply_desc_dma_obj.status = DMA_OBJ_FREED;
	}


}


/*
 * ThunderBolt(TB) Request Message Frame Pool
 */
int
create_mpi2_frame_pool(struct mrsas_instance *instance)
{
	int		i = 0;
	uint16_t	max_cmd;
	uint32_t	sgl_sz;
	uint32_t	raid_msg_size;
	uint32_t	total_size;
	uint32_t  	offset;
	uint32_t 	io_req_base_phys;
	uint8_t 	*io_req_base;
	struct mrsas_cmd	*cmd;

	max_cmd = instance->max_fw_cmds;

	sgl_sz		= 1024;
	raid_msg_size	= MRSAS_THUNDERBOLT_MSG_SIZE;

	/* Allocating additional 256 bytes to accomodate SMID 0 */
	total_size = MRSAS_THUNDERBOLT_MSG_SIZE + (max_cmd * raid_msg_size) +
	    (max_cmd * sgl_sz) + (max_cmd * SENSE_LENGTH);

	con_log(CL_ANN1, (CE_NOTE, "create_mpi2_frame_pool: "
	    "max_cmd %x ", max_cmd));

	con_log(CL_DLEVEL3, (CE_NOTE, "create_mpi2_frame_pool: "
	    "request message frame pool size %x", total_size));

	/*
	 * ThunderBolt(TB) We need to create a single chunk of DMA'ble memory
	 * and then split the memory to 1024 commands. Each command should be
	 * able to contain a RAID MESSAGE FRAME which will embed a MFI_FRAME
	 * within it. Further refer the "alloc_req_rep_desc" function where
	 * we allocate request/reply descriptors queues for a clue.
	 */

	instance->mpi2_frame_pool_dma_obj.size = total_size;
	instance->mpi2_frame_pool_dma_obj.dma_attr = mrsas_generic_dma_attr;
	instance->mpi2_frame_pool_dma_obj.dma_attr.dma_attr_addr_hi =
	    0xFFFFFFFFU;
	instance->mpi2_frame_pool_dma_obj.dma_attr.dma_attr_count_max =
	    0xFFFFFFFFU;
	instance->mpi2_frame_pool_dma_obj.dma_attr.dma_attr_sgllen = 1;
	instance->mpi2_frame_pool_dma_obj.dma_attr.dma_attr_align = 256;

	if (mrsas_alloc_dma_obj(instance, &instance->mpi2_frame_pool_dma_obj,
	    (uchar_t)DDI_STRUCTURE_LE_ACC) != 1) {
		con_log(CL_ANN, (CE_WARN,
		    "mr_sas: could not alloc mpi2 frame pool"));
		return (DDI_FAILURE);
	}

	bzero(instance->mpi2_frame_pool_dma_obj.buffer, total_size);
	instance->mpi2_frame_pool_dma_obj.status |= DMA_OBJ_ALLOCATED;

	instance->io_request_frames =
	    (uint8_t *)instance->mpi2_frame_pool_dma_obj.buffer;
	instance->io_request_frames_phy =
	    (uint32_t)
	    instance->mpi2_frame_pool_dma_obj.dma_cookie[0].dmac_address;

	con_log(CL_DLEVEL3, (CE_NOTE,
	    "io_request_frames %p",
	    (void *)instance->io_request_frames));

	con_log(CL_DLEVEL3, (CE_NOTE,
	    "io_request_frames_phy 0x%x",
	    instance->io_request_frames_phy));

	io_req_base = (uint8_t *)instance->io_request_frames +
	    MRSAS_THUNDERBOLT_MSG_SIZE;
	io_req_base_phys = instance->io_request_frames_phy +
	    MRSAS_THUNDERBOLT_MSG_SIZE;

	con_log(CL_DLEVEL3, (CE_NOTE,
	    "io req_base_phys 0x%x", io_req_base_phys));

	for (i = 0; i < max_cmd; i++) {
		cmd = instance->cmd_list[i];

		offset = i * MRSAS_THUNDERBOLT_MSG_SIZE;

		cmd->scsi_io_request = (Mpi2RaidSCSIIORequest_t *)
		    ((uint8_t *)io_req_base + offset);
		cmd->scsi_io_request_phys_addr = io_req_base_phys + offset;

		cmd->sgl = (Mpi2SGEIOUnion_t *)
		    ((uint8_t *)io_req_base +
		    (max_cmd * raid_msg_size) +  i * sgl_sz);

		cmd->sgl_phys_addr =
		    (io_req_base_phys +
		    (max_cmd * raid_msg_size) + i * sgl_sz);

		cmd->sense1 = (uint8_t *)
		    ((uint8_t *)io_req_base +
		    (max_cmd * raid_msg_size) + (max_cmd * sgl_sz) +
		    (i * SENSE_LENGTH));

		cmd->sense_phys_addr1 =
		    (io_req_base_phys +
		    (max_cmd * raid_msg_size) + (max_cmd * sgl_sz) +
		    (i * SENSE_LENGTH));


		cmd->SMID = i+1;

		con_log(CL_DLEVEL3, (CE_NOTE,
		    "Frame Pool Addr [%x]%p",
		    cmd->index, (void *)cmd->scsi_io_request));

		con_log(CL_DLEVEL3, (CE_NOTE,
		    "Frame Pool Phys Addr [%x]0x%x",
		    cmd->index, cmd->scsi_io_request_phys_addr));

		con_log(CL_DLEVEL3, (CE_NOTE,
		    "Sense Addr [%x]%p",
		    cmd->index, (void *)cmd->sense1));

		con_log(CL_DLEVEL3, (CE_NOTE,
		    "Sense Addr Phys [%x]0x%x",
		    cmd->index, cmd->sense_phys_addr1));


		con_log(CL_DLEVEL3, (CE_NOTE,
		    "Sgl bufffers [%x]%p",
		    cmd->index, (void *)cmd->sgl));

		con_log(CL_DLEVEL3, (CE_NOTE,
		    "Sgl bufffers phys  [%x]0x%x",
		    cmd->index, cmd->sgl_phys_addr));
	}

	return (DDI_SUCCESS);

}


/*
 * alloc_additional_dma_buffer for AEN
 */
int
mrsas_tbolt_alloc_additional_dma_buffer(struct mrsas_instance *instance)
{
	uint32_t	internal_buf_size = PAGESIZE*2;
	int		i;

	instance->mfi_internal_dma_obj.size =
	    internal_buf_size;
	instance->mfi_internal_dma_obj.dma_attr =
	    mrsas_generic_dma_attr;
	instance->mfi_internal_dma_obj.dma_attr.dma_attr_addr_hi =
	    0xFFFFFFFFU;
	instance->mfi_internal_dma_obj.dma_attr.dma_attr_count_max =
	    0xFFFFFFFFU;
	instance->mfi_internal_dma_obj.dma_attr.dma_attr_sgllen = 1;

	if (mrsas_alloc_dma_obj(instance, &instance->mfi_internal_dma_obj,
	    (uchar_t)DDI_STRUCTURE_LE_ACC) != 1) {
		con_log(CL_ANN, (CE_WARN,
		    "mr_sas: could not alloc reply queue"));
		return (DDI_FAILURE);
	}

	bzero(instance->mfi_internal_dma_obj.buffer, internal_buf_size);

	instance->mfi_internal_dma_obj.status |= DMA_OBJ_ALLOCATED;
	instance->internal_buf = (caddr_t)(((unsigned long)
	    instance->mfi_internal_dma_obj.buffer));
	instance->internal_buf_dmac_add =
	    instance->mfi_internal_dma_obj.dma_cookie[0].dmac_address;

	instance->internal_buf_size = internal_buf_size;

	/* allocate evt_detail */
	instance->mfi_evt_detail_obj.size = sizeof (struct mrsas_evt_detail);
	instance->mfi_evt_detail_obj.dma_attr = mrsas_generic_dma_attr;
	instance->mfi_evt_detail_obj.dma_attr.dma_attr_addr_hi = 0xFFFFFFFFU;
	instance->mfi_evt_detail_obj.dma_attr.dma_attr_count_max = 0xFFFFFFFFU;
	instance->mfi_evt_detail_obj.dma_attr.dma_attr_sgllen = 1;
	instance->mfi_evt_detail_obj.dma_attr.dma_attr_align = 8;

	if (mrsas_alloc_dma_obj(instance, &instance->mfi_evt_detail_obj,
	    (uchar_t)DDI_STRUCTURE_LE_ACC) != 1) {
		con_log(CL_ANN, (CE_WARN,
		    "mrsas_tbolt_alloc_additional_dma_buffer: "
		    "could not allocate data transfer buffer."));
		return (DDI_FAILURE);
	}

	bzero(instance->mfi_evt_detail_obj.buffer,
	    sizeof (struct mrsas_evt_detail));

	instance->mfi_evt_detail_obj.status |= DMA_OBJ_ALLOCATED;

	instance->size_map_info = sizeof (MR_FW_RAID_MAP) +
	    (sizeof (MR_LD_SPAN_MAP) * (MAX_LOGICAL_DRIVES - 1));

	for (i = 0; i < 2; i++) {
		/* allocate the data transfer buffer */
		instance->ld_map_obj[i].size =
		    instance->size_map_info;
		instance->ld_map_obj[i].dma_attr =
		    mrsas_generic_dma_attr;
		instance->ld_map_obj[i].dma_attr.dma_attr_addr_hi =
		    0xFFFFFFFFU;
		instance->ld_map_obj[i].dma_attr.dma_attr_count_max =
		    0xFFFFFFFFU;
		instance->ld_map_obj[i].dma_attr.dma_attr_sgllen = 1;
		instance->ld_map_obj[i].dma_attr.dma_attr_align = 1;

		if (mrsas_alloc_dma_obj(instance, &instance->ld_map_obj[i],
		    (uchar_t)DDI_STRUCTURE_LE_ACC) != 1) {
			con_log(CL_ANN, (CE_WARN,
			    "could not allocate data transfer buffer."));
			return (DDI_FAILURE);
		}

		instance->ld_map_obj[i].status |= DMA_OBJ_ALLOCATED;

		(void) memset(instance->ld_map_obj[i].buffer, 0,
		    instance->size_map_info);

		instance->ld_map[i] =
		    (MR_FW_RAID_MAP_ALL *)instance->ld_map_obj[i].buffer;
		instance->ld_map_phy[i] =
		    (uint32_t)instance->
		    ld_map_obj[i].dma_cookie[0].dmac_address;

		con_log(CL_DLEVEL3, (CE_NOTE,
		    "ld_map Addr Phys 0x%x", instance->ld_map_phy[i]));

		con_log(CL_DLEVEL3, (CE_NOTE,
		    "size_map_info 0x%x", instance->size_map_info));

	}

	return (DDI_SUCCESS);
}

MRSAS_REQUEST_DESCRIPTOR_UNION *
mr_sas_get_request_descriptor(struct mrsas_instance *instance,
    uint16_t index)
{
	MRSAS_REQUEST_DESCRIPTOR_UNION *req_desc;

	if (index > instance->max_fw_cmds) {
		con_log(CL_ANN1, (CE_NOTE,
		    "Invalid SMID 0x%x request for descriptor", index));
		con_log(CL_ANN1, (CE_NOTE,
		    "max_fw_cmds : 0x%x\n", instance->max_fw_cmds));
		return (NULL);
	}

	req_desc = (MRSAS_REQUEST_DESCRIPTOR_UNION *)
	    ((char *)instance->request_message_pool +
	    (sizeof (MRSAS_REQUEST_DESCRIPTOR_UNION) * index));

	con_log(CL_ANN1, (CE_NOTE,
	    "request descriptor : 0x%08lx\n", (unsigned long)req_desc));

	con_log(CL_ANN1, (CE_NOTE,
	    "request descriptor base phy : 0x%08lx\n",
	    (unsigned long)instance->request_message_pool_phy));

	return ((MRSAS_REQUEST_DESCRIPTOR_UNION *)req_desc);
}


/*
 * Allocate Request and Reply  Queue Descriptors.
 */
int
alloc_req_rep_desc(struct mrsas_instance *instance)
{
	uint32_t	request_q_sz, reply_q_sz;
	int 		i, max_reply_q_sz;
	MPI2_REPLY_DESCRIPTORS_UNION *reply_desc;

	/*
	 * ThunderBolt(TB) There's no longer producer consumer mechanism.
	 * Once we have an interrupt we are supposed to scan through the list of
	 * reply descriptors and process them accordingly. We would be needing
	 * to allocate memory for 1024 reply descriptors
	 */

	/* Allocate Reply Descriptors */
	con_log(CL_ANN1, (CE_NOTE, " reply q desc len = %x\n",
	    (uint32_t)sizeof (MPI2_REPLY_DESCRIPTORS_UNION)));

	/* reply queue size should be multiple of 16 */
	max_reply_q_sz = ((instance->max_fw_cmds + 1 + 15)/16)*16;

	reply_q_sz = 8 * max_reply_q_sz;


	con_log(CL_ANN1, (CE_NOTE, " reply q desc len = %x\n",
	    (uint32_t)sizeof (MPI2_REPLY_DESCRIPTORS_UNION)));

	instance->reply_desc_dma_obj.size = reply_q_sz;
	instance->reply_desc_dma_obj.dma_attr = mrsas_generic_dma_attr;
	instance->reply_desc_dma_obj.dma_attr.dma_attr_addr_hi = 0xFFFFFFFFU;
	instance->reply_desc_dma_obj.dma_attr.dma_attr_count_max = 0xFFFFFFFFU;
	instance->reply_desc_dma_obj.dma_attr.dma_attr_sgllen = 1;
	instance->reply_desc_dma_obj.dma_attr.dma_attr_align = 16;

	if (mrsas_alloc_dma_obj(instance, &instance->reply_desc_dma_obj,
	    (uchar_t)DDI_STRUCTURE_LE_ACC) != 1) {
		con_log(CL_ANN, (CE_WARN,
		    "mr_sas: could not alloc reply queue"));
		return (DDI_FAILURE);
	}

	bzero(instance->reply_desc_dma_obj.buffer, reply_q_sz);
	instance->reply_desc_dma_obj.status |= DMA_OBJ_ALLOCATED;

	/* virtual address of  reply queue */
	instance->reply_frame_pool = (MPI2_REPLY_DESCRIPTORS_UNION *)(
	    instance->reply_desc_dma_obj.buffer);

	instance->reply_q_depth = max_reply_q_sz;

	con_log(CL_ANN1, (CE_NOTE, "[reply queue depth]0x%x",
	    instance->reply_q_depth));

	con_log(CL_ANN1, (CE_NOTE, "[reply queue virt addr]%p",
	    (void *)instance->reply_frame_pool));

	/* initializing reply address to 0xFFFFFFFF */
	reply_desc = instance->reply_frame_pool;

	for (i = 0; i < instance->reply_q_depth; i++) {
		ddi_put32(instance->reply_desc_dma_obj.acc_handle,
		    &reply_desc->Words.Low, 0xFFFFFFFF);
		ddi_put32(instance->reply_desc_dma_obj.acc_handle,
		    &reply_desc->Words.High, 0xFFFFFFFF);
		reply_desc++;
	}


	instance->reply_frame_pool_phy =
	    (uint32_t)instance->reply_desc_dma_obj.dma_cookie[0].dmac_address;

	con_log(CL_ANN1, (CE_NOTE,
	    "[reply queue phys addr]0x%x", instance->reply_frame_pool_phy));


	instance->reply_pool_limit_phy = (instance->reply_frame_pool_phy +
	    reply_q_sz);

	con_log(CL_ANN1, (CE_NOTE, "[reply pool limit phys addr]0x%x",
	    instance->reply_pool_limit_phy));


	con_log(CL_ANN1, (CE_NOTE, " request q desc len = %x\n",
	    (uint32_t)sizeof (MRSAS_REQUEST_DESCRIPTOR_UNION)));

	/* Allocate Request Descriptors */
	con_log(CL_ANN1, (CE_NOTE, " request q desc len = %x\n",
	    (uint32_t)sizeof (MRSAS_REQUEST_DESCRIPTOR_UNION)));

	request_q_sz = 8 *
	    (instance->max_fw_cmds);

	instance->request_desc_dma_obj.size = request_q_sz;
	instance->request_desc_dma_obj.dma_attr	= mrsas_generic_dma_attr;
	instance->request_desc_dma_obj.dma_attr.dma_attr_addr_hi = 0xFFFFFFFFU;
	instance->request_desc_dma_obj.dma_attr.dma_attr_count_max =
	    0xFFFFFFFFU;
	instance->request_desc_dma_obj.dma_attr.dma_attr_sgllen	= 1;
	instance->request_desc_dma_obj.dma_attr.dma_attr_align = 16;

	if (mrsas_alloc_dma_obj(instance, &instance->request_desc_dma_obj,
	    (uchar_t)DDI_STRUCTURE_LE_ACC) != 1) {
		con_log(CL_ANN, (CE_WARN,
		    "mr_sas: could not alloc request queue desc"));
		return (DDI_FAILURE);
	}

	bzero(instance->request_desc_dma_obj.buffer, request_q_sz);
	instance->request_desc_dma_obj.status |= DMA_OBJ_ALLOCATED;

	/* virtual address of  request queue desc */
	instance->request_message_pool = (MRSAS_REQUEST_DESCRIPTOR_UNION *)
	    (instance->request_desc_dma_obj.buffer);

	instance->request_message_pool_phy =
	    (uint32_t)instance->request_desc_dma_obj.dma_cookie[0].dmac_address;


	return (DDI_SUCCESS);
}

/*
 * free_space_for_mpi2
 */
void
free_space_for_mpi2(struct mrsas_instance *instance)
{
	int		i;
	uint32_t	max_cmd = instance->max_fw_cmds;

	/* First free the reply desc pool */
	mrsas_tbolt_free_additional_dma_buffer(instance);

	/* Free the request desc pool */
	free_req_rep_desc_pool(instance);

	/*  Free the MPI frame pool */
	destroy_mpi2_frame_pool(instance);

	/* Free the MFI frame pool */
	/* destroy_mfi_mpi_frame_pool(instance); */
	destroy_mfi_frame_pool(instance);


	/* free all the commands in the cmd_list */
	for (i = 0; i < instance->max_fw_cmds; i++) {
		if (instance->cmd_list[i] != NULL) {
			kmem_free(instance->cmd_list[i],
			    sizeof (struct mrsas_cmd));
			instance->cmd_list[i] = NULL;
		}
	}

	/* free the cmd_list buffer itself */
	if (instance->cmd_list != NULL) {
		kmem_free(instance->cmd_list,
		    sizeof (struct mrsas_cmd *) * max_cmd);
		instance->cmd_list = NULL;
	}

	INIT_LIST_HEAD(&instance->cmd_pool_list);
	INIT_LIST_HEAD(&instance->cmd_pend_list);
}


/*
 * ThunderBolt(TB) memory allocations for commands/messages/frames.
 */
int
alloc_space_for_mpi2(struct mrsas_instance *instance)
{
	int		i;
	uint32_t	max_cmd;
	size_t		sz;
	struct mrsas_cmd	*cmd;
	instance->max_fw_cmds = 1008;

	/*
	 * Reduce the max supported cmds by 1. This is to ensure that the
	 * reply_q_sz (1 more than the max cmd that driver may send)
	 * does not exceed max cmds that the FW can support
	 */
	instance->max_fw_cmds = instance->max_fw_cmds-1;

	max_cmd = instance->max_fw_cmds;

	con_log(CL_DLEVEL3, (CE_NOTE, "alloc_space_for_mpi2: "
	    "max_cmd %x", max_cmd));

	sz = sizeof (struct mrsas_cmd *) * max_cmd;

	/*
	 * instance->cmd_list is an array of struct mrsas_cmd pointers.
	 * Allocate the dynamic array first and then allocate individual
	 * commands.
	 */


	instance->cmd_list = kmem_zalloc(sz, KM_SLEEP);
	ASSERT(instance->cmd_list);

	for (i = 0; i < max_cmd; i++) {
		instance->cmd_list[i] = kmem_zalloc(sizeof (struct mrsas_cmd),
		    KM_SLEEP);
		ASSERT(instance->cmd_list[i]);
	}


	INIT_LIST_HEAD(&instance->cmd_app_pool_list);
	INIT_LIST_HEAD(&instance->cmd_pool_list);

	INIT_LIST_HEAD(&instance->cmd_pend_list);
	for (i = 1; i < (APP_RESERVE_CMDS-2); i++) {
		cmd		= instance->cmd_list[i];
		cmd->index	= i;
		mlist_add_tail(&cmd->list, &instance->cmd_app_pool_list);
	}

	/*
	 * reserve slot instance->cmd_list[APP_RESERVE_CMDS-1]
	 * for abort_aen_cmd and
	 * reserve slot instance->cmd_list[APP_RESERVE_CMDS-2]
	 * for abort_sync_cmd
	 */
	for (i = (APP_RESERVE_CMDS-2); i < APP_RESERVE_CMDS; i++) {
		cmd		= instance->cmd_list[i];
		cmd->index	= i;
	}

	con_log(CL_ANN, (CE_NOTE, "alloc_space_for_mpi2: "
	    " Completed App pool init "));

	/* add all the commands to command pool (instance->cmd_pool) */
	for (i = APP_RESERVE_CMDS; i < (max_cmd-1); i++) {
		cmd		= instance->cmd_list[i];
		cmd->index	= i;
		mlist_add_tail(&cmd->list, &instance->cmd_pool_list);
	}

	/* single slot for flush_cache won't be added in command pool */
	cmd = instance->cmd_list[i];
	cmd->index = i;

	if (mrsas_mpi2_init_pool(instance)) {
		con_log(CL_ANN, (CE_NOTE,
		    "mr_sas_tbolt: error creating frame DMA pool"));
		free_space_for_mpi2(instance);
		return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);
}


int
mrsas_mpi2_init_pool(struct mrsas_instance *instance)
{

	/* Initialize single reply size and Message size */
	instance->reply_size = MRSAS_THUNDERBOLT_REPLY_SIZE;
	instance->raid_io_msg_size = MRSAS_THUNDERBOLT_MSG_SIZE;

	instance->max_sge_in_main_msg = (MRSAS_THUNDERBOLT_MSG_SIZE -
	    (sizeof (MPI2_RAID_SCSI_IO_REQUEST) -
	    sizeof (MPI2_SGE_IO_UNION)))/ sizeof (MPI2_SGE_IO_UNION);

	instance->max_sge_in_chain = (MR_COMMAND_SIZE -
	    MRSAS_THUNDERBOLT_MSG_SIZE) / sizeof (MPI2_SGE_IO_UNION);

	/* Reduce SG count by 1 to take care of group cmds feature in FW */
	instance->max_num_sge = (instance->max_sge_in_main_msg +
	    instance->max_sge_in_chain - 2);

	instance->chain_offset_mpt_msg =
	    offsetof(MPI2_RAID_SCSI_IO_REQUEST, SGL) / 16;

	instance->chain_offset_io_req = (MRSAS_THUNDERBOLT_MSG_SIZE -
	    sizeof (MPI2_SGE_IO_UNION)) / 16;

	instance->reply_read_index = 0;

	/* Make sure the buffer is aligned to 8 for req/rep  descriptor Pool */
	if (alloc_req_rep_desc(instance)) {
		return (DDI_FAILURE);
	}

	con_log(CL_ANN1, (CE_NOTE, "[request message pool phys addr]0x%x",
	    instance->request_message_pool_phy));

	/* Create MFI Frame Pool for MPI passthru */

	/* create a frame pool and assign one frame to each cmd */
	if (create_mfi_frame_pool(instance)) {
		con_log(CL_ANN, (CE_NOTE, "error creating frame DMA pool"));
		return (DDI_FAILURE);
	}


	/*
	 * Make sure the buffer is alligned to 256 for raid message packet
	 * create a io request pool and assign one frame to each cmd
	 */

	if (create_mpi2_frame_pool(instance)) {
		return (DDI_FAILURE);
	}

	con_log(CL_ANN1, (CE_NOTE, "[max_sge_in_main_msg]0x%x",
	    instance->max_sge_in_main_msg));

	con_log(CL_ANN1, (CE_NOTE, "[max_sge_in_chain]0x%x",
	    instance->max_sge_in_chain));

	con_log(CL_ANN1, (CE_NOTE,
	    "[max_sge]0x%x", instance->max_num_sge));

	con_log(CL_ANN1, (CE_NOTE, "[chain_offset_mpt_msg]0x%x",
	    instance->chain_offset_mpt_msg));

	con_log(CL_ANN1, (CE_NOTE, "[chain_offset_io_req]0x%x",
	    instance->chain_offset_io_req));

	if (mrsas_tbolt_alloc_additional_dma_buffer(instance)) {
		con_log(CL_ANN, (CE_NOTE, "error creating frame DMA pool"));
		return (DDI_FAILURE);
	}

	return (0);
}


/*
 * init_mpi2
 */
int
mrsas_issue_init_mpi2(struct mrsas_instance *instance)
{
	dma_obj_t init2_dma_obj;
	int ret_val = DDI_SUCCESS;

	/* Send ioc init message */

	/* allocate the data transfer buffer */
	init2_dma_obj.size = sizeof (Mpi2IOCInitRequest_t);
	init2_dma_obj.dma_attr = mrsas_generic_dma_attr;
	init2_dma_obj.dma_attr.dma_attr_addr_hi = 0xFFFFFFFFU;
	init2_dma_obj.dma_attr.dma_attr_count_max = 0xFFFFFFFFU;
	init2_dma_obj.dma_attr.dma_attr_sgllen = 1;
	init2_dma_obj.dma_attr.dma_attr_align = 256;

	/* allocate kernel buffer for DMA */
	if (mrsas_alloc_dma_obj(instance, &init2_dma_obj,
	    (uchar_t)DDI_STRUCTURE_LE_ACC) != 1) {
		con_log(CL_ANN, (CE_WARN, "mr_sas_issue_init_mpi2 "
		    "could not allocate data transfer buffer."));
		return (DDI_FAILURE);
	}

	(void) memset(init2_dma_obj.buffer, 2,
	    sizeof (Mpi2IOCInitRequest_t));
	con_log(CL_ANN, (CE_NOTE,
	    "mrsas_issue_init_mpi2 _phys adr: %x \n",
	    init2_dma_obj.dma_cookie[0].dmac_address));

	ret_val = mrsas_tbolt_ioc_init(instance, &init2_dma_obj);

	if (mrsas_free_dma_obj(instance, init2_dma_obj)
	    != DDI_SUCCESS) {
		con_log(CL_ANN1, (CE_NOTE,
		    "mrsas_issue_init_mpi2: Free Failed\n"));
		return (DDI_FAILURE);
	}

	if (ret_val == DDI_FAILURE) {
		con_log(CL_ANN1, (CE_NOTE,
		    "mrsas_issue_init_mpi2: Failed\n"));
		return (ret_val);
	}

	instance->map_id = 0;
	instance->fp_change = 1;

	if (mrsas_tbolt_check_map_info(instance) == DDI_SUCCESS) {
		(void) mrsas_tbolt_sync_map_info(instance);
		instance->fp_change = 0;
	}

	return (DDI_SUCCESS);
}

int
mrsas_tbolt_ioc_init(struct mrsas_instance *instance, dma_obj_t *mpi2_dma_obj)
{
	int				numbytes;
	int ret				= DDI_SUCCESS;
	uint16_t			flags;
	struct mrsas_init_frame2	*mfiFrameInit2;
	struct mrsas_header		*frame_hdr;
	Mpi2IOCInitRequest_t		*init;
	struct mrsas_cmd		*cmd = NULL;
	struct	mrsas_drv_ver		drv_ver_info;
	MRSAS_REQUEST_DESCRIPTOR_UNION	*req_desc;


	con_log(CL_ANN1, (CE_NOTE, "chkpnt: Entered tbolt_ioc_init:%d",
	    __LINE__));
	con_log(CL_ANN1, (CE_NOTE, " mfiFrameInit2 len = %x\n",
	    (uint32_t)sizeof (*mfiFrameInit2)));
	con_log(CL_ANN1, (CE_NOTE, " MPI len = %x\n",
	    (uint32_t)sizeof (*init)));
	con_log(CL_ANN1, (CE_NOTE, " mfiFrameInit2 len = %x\n",
	    (uint32_t)sizeof (struct mrsas_init_frame2)));
	con_log(CL_ANN1, (CE_NOTE, " MPI len = %x\n",
	    (uint32_t)sizeof (Mpi2IOCInitRequest_t)));

	init = (Mpi2IOCInitRequest_t *)mpi2_dma_obj->buffer;
	numbytes = sizeof (*init);
	bzero(init, numbytes);

	ddi_put8(mpi2_dma_obj->acc_handle, &init->Function,
	    MPI2_FUNCTION_IOC_INIT);

	ddi_put8(mpi2_dma_obj->acc_handle, &init->WhoInit,
	    MPI2_WHOINIT_HOST_DRIVER);

	/* set MsgVersion and HeaderVersion host driver was built with */
	ddi_put16(mpi2_dma_obj->acc_handle, &init->MsgVersion,
	    MPI2_VERSION);

	ddi_put16(mpi2_dma_obj->acc_handle, &init->HeaderVersion,
	    MPI2_HEADER_VERSION);

	ddi_put16(mpi2_dma_obj->acc_handle, &init->SystemRequestFrameSize,
	    instance->raid_io_msg_size / 4);

	ddi_put16(mpi2_dma_obj->acc_handle, &init->ReplyFreeQueueDepth,
	    0);

	ddi_put16(mpi2_dma_obj->acc_handle,
	    &init->ReplyDescriptorPostQueueDepth,
	    instance->reply_q_depth);
	/*
	 * These addresses are set using the DMA cookie addresses from when the
	 * memory was allocated.  Sense buffer hi address should be 0.
	 * ddi_put32(accessp, &init->SenseBufferAddressHigh, 0);
	 */

	ddi_put32(mpi2_dma_obj->acc_handle,
	    &init->SenseBufferAddressHigh, 0);

	ddi_put64(mpi2_dma_obj->acc_handle,
	    (uint64_t *)&init->SystemRequestFrameBaseAddress,
	    instance->io_request_frames_phy);

	ddi_put64(mpi2_dma_obj->acc_handle,
	    &init->ReplyDescriptorPostQueueAddress,
	    instance->reply_frame_pool_phy);

	ddi_put64(mpi2_dma_obj->acc_handle,
	    &init->ReplyFreeQueueAddress, 0);

	cmd = instance->cmd_list[0];

	if (cmd == NULL) {
		return (DDI_FAILURE);
	}
	cmd->retry_count_for_ocr = 0;
	mfiFrameInit2 = (struct mrsas_init_frame2 *)cmd->scsi_io_request;
	con_log(CL_ANN1, (CE_NOTE, "[mfi vaddr]%p", (void *)mfiFrameInit2));

	frame_hdr = &cmd->frame->hdr;

	ddi_put8(cmd->frame_dma_obj.acc_handle, &frame_hdr->cmd_status,
	    MFI_CMD_STATUS_POLL_MODE);

	flags = ddi_get16(cmd->frame_dma_obj.acc_handle, &frame_hdr->flags);

	flags 	|= MFI_FRAME_DONT_POST_IN_REPLY_QUEUE;

	ddi_put16(cmd->frame_dma_obj.acc_handle, &frame_hdr->flags, flags);

	con_log(CL_ANN, (CE_NOTE,
	    "mrsas_tbolt_ioc_init: SMID:%x\n", cmd->SMID));

	/* Init the MFI Header */
	ddi_put8(instance->mpi2_frame_pool_dma_obj.acc_handle,
	    &mfiFrameInit2->cmd, MFI_CMD_OP_INIT);

	con_log(CL_ANN1, (CE_NOTE, "[CMD]%x", mfiFrameInit2->cmd));

	ddi_put8(instance->mpi2_frame_pool_dma_obj.acc_handle,
	    &mfiFrameInit2->cmd_status,
	    MFI_STAT_INVALID_STATUS);

	con_log(CL_ANN1, (CE_NOTE, "[Status]%x", mfiFrameInit2->cmd_status));

	ddi_put32(instance->mpi2_frame_pool_dma_obj.acc_handle,
	    &mfiFrameInit2->queue_info_new_phys_addr_lo,
	    mpi2_dma_obj->dma_cookie[0].dmac_address);

	ddi_put32(instance->mpi2_frame_pool_dma_obj.acc_handle,
	    &mfiFrameInit2->data_xfer_len,
	    sizeof (Mpi2IOCInitRequest_t));

	con_log(CL_ANN1, (CE_NOTE, "[reply q desc addr]%lx",
	    (unsigned long)init->ReplyDescriptorPostQueueAddress));

	/* fill driver version information */
	fill_up_drv_ver(&drv_ver_info);

	/* allocate the driver version data transfer buffer */
	instance->drv_ver_dma_obj.size = sizeof (drv_ver_info.drv_ver);
	instance->drv_ver_dma_obj.dma_attr = mrsas_generic_dma_attr;
	instance->drv_ver_dma_obj.dma_attr.dma_attr_addr_hi = 0xFFFFFFFFU;
	instance->drv_ver_dma_obj.dma_attr.dma_attr_count_max = 0xFFFFFFFFU;
	instance->drv_ver_dma_obj.dma_attr.dma_attr_sgllen = 1;
	instance->drv_ver_dma_obj.dma_attr.dma_attr_align = 1;

	if (mrsas_alloc_dma_obj(instance, &instance->drv_ver_dma_obj,
	    (uchar_t)DDI_STRUCTURE_LE_ACC) != 1) {
		con_log(CL_ANN, (CE_WARN,
		    "fusion init: Could not allocate"
		    " driver version buffer.\n"));
		return (DDI_FAILURE);
	}
	/* copy driver version to dma buffer */
	(void) memset(instance->drv_ver_dma_obj.buffer,
	    0, sizeof (drv_ver_info.drv_ver));
	ddi_rep_put8(cmd->frame_dma_obj.acc_handle,
	    (uint8_t *)drv_ver_info.drv_ver,
	    (uint8_t *)instance->drv_ver_dma_obj.buffer,
	    sizeof (drv_ver_info.drv_ver), DDI_DEV_AUTOINCR);

	/* send driver version physical address to firmware */
	ddi_put64(cmd->frame_dma_obj.acc_handle,
	    &mfiFrameInit2->driverversion,
	    instance->drv_ver_dma_obj.dma_cookie[0].dmac_address);

	con_log(CL_ANN1, (CE_NOTE, "[MPIINIT2 frame Phys addr ]0x%x len = %x",
	    mfiFrameInit2->queue_info_new_phys_addr_lo,
	    (uint32_t)sizeof (Mpi2IOCInitRequest_t)));

	con_log(CL_ANN1, (CE_NOTE, "[Length]%x", mfiFrameInit2->data_xfer_len));

	con_log(CL_ANN1, (CE_NOTE, "[MFI frame Phys Address]%x len = %x",
	    cmd->scsi_io_request_phys_addr,
	    (uint32_t)sizeof (struct mrsas_init_frame2)));

	/* disable interrupts before sending INIT2 frame */
	instance->func_ptr->disable_intr(instance);

	req_desc = (MRSAS_REQUEST_DESCRIPTOR_UNION *)
	    instance->request_message_pool;
	req_desc->Words.Low = cmd->scsi_io_request_phys_addr;
	req_desc->Words.High = 0;
	req_desc->MFAIo.RequestFlags =
	    (MPI2_REQ_DESCRIPT_FLAGS_MFA << MPI2_REQ_DESCRIPT_FLAGS_TYPE_SHIFT);

	cmd->request_desc = req_desc;

	/* issue the init frame */
	instance->func_ptr->issue_cmd_in_poll_mode(instance, cmd);

	con_log(CL_ANN1, (CE_NOTE, "[cmd = %d] ", frame_hdr->cmd));
	con_log(CL_ANN1, (CE_NOTE, "[cmd  Status= %x] ",
	    frame_hdr->cmd_status));

	if (mrsas_free_dma_obj(instance, instance->drv_ver_dma_obj) !=
	    DDI_SUCCESS) {
		return (DDI_FAILURE);
	}

	if (ddi_get8(instance->mpi2_frame_pool_dma_obj.acc_handle,
	    &mfiFrameInit2->cmd_status) == 0) {
		con_log(CL_ANN1, (CE_NOTE, "INIT2 Success"));
		ret = DDI_SUCCESS;
	} else {
		con_log(CL_ANN1, (CE_NOTE, "INIT2 Fail"));
		ret = DDI_FAILURE;
	}

	mrsas_dump_reply_desc(instance);

	if (mrsas_common_check(instance, cmd) != DDI_SUCCESS) {
		return (DDI_FAILURE);
	}

	if (mrsas_check_acc_handle(instance->regmap_handle) != DDI_SUCCESS) {
		ret = DDI_FAILURE;
	}

	return (ret);
}
int
wait_for_outstanding_poll_io(struct mrsas_instance *instance)
{
	int		i;
	uint32_t	wait_time = dump_io_wait_time;

	for (i = 0; i < wait_time; i++) {
		/*
		 * Check For Outstanding poll commands
		 * except ldsync command and aen command
		 */
		if (instance->fw_outstanding <= 2) {
			break;
		}
		drv_usecwait(10 * MILLISEC);
		/* complete commands from reply queue */
		(void) mr_sas_tbolt_process_outstanding_cmd(instance);
	}

	if (instance->fw_outstanding > 2) {
		return (1);
	}
	return (0);
}

/*
 * scsi_pkt handling
 *
 * Visible to the external world via the transport structure.
 */

int
mrsas_tbolt_tran_start(struct scsi_address *ap, struct scsi_pkt *pkt)
{
	struct mrsas_instance	*instance = ADDR2MR(ap);
	struct scsa_cmd		*acmd = PKT2CMD(pkt);
	struct mrsas_cmd	*cmd = NULL;
	uchar_t 		cmd_done = 0;

	if (instance->deadadapter == 1) {
		con_log(CL_ANN1, (CE_WARN,
		    "mrsas_tran_start:TBOLT return TRAN_FATAL_ERROR "
		    "for IO, as the HBA doesnt take any more IOs"));
		if (pkt) {
			pkt->pkt_reason		= CMD_DEV_GONE;
			pkt->pkt_statistics	= STAT_DISCON;
		}
		return (TRAN_FATAL_ERROR);
	}
	if (instance->adapterresetinprogress) {
		con_log(CL_ANN1, (CE_NOTE, "Reset flag set, "
		    "returning mfi_pkt and setting TRAN_BUSY\n"));
		return (TRAN_BUSY);
	}
	(void) mrsas_tbolt_prepare_pkt(acmd);

	cmd = mrsas_tbolt_build_cmd(instance, ap, pkt, &cmd_done);

	/*
	 * Check if the command is already completed by the mrsas_build_cmd()
	 * routine. In which case the busy_flag would be clear and scb will be
	 * NULL and appropriate reason provided in pkt_reason field
	 */
	if (cmd_done) {
		pkt->pkt_reason = CMD_CMPLT;
		pkt->pkt_scbp[0] = STATUS_GOOD;
		pkt->pkt_state |= STATE_GOT_BUS | STATE_GOT_TARGET
		    | STATE_SENT_CMD;
		if (((pkt->pkt_flags & FLAG_NOINTR) == 0) && pkt->pkt_comp) {
			(*pkt->pkt_comp)(pkt);
		}

		return (TRAN_ACCEPT);
	}

	if (cmd == NULL) {
		return (TRAN_BUSY);
	}


	if ((pkt->pkt_flags & FLAG_NOINTR) == 0) {
		if (instance->fw_outstanding > instance->max_fw_cmds) {
			con_log(CL_ANN, (CE_NOTE,
			    "Command Queue Full... Returning \n"));
			return_raid_msg_pkt(instance, cmd);
			return (TRAN_BUSY);
		}

		/* Synchronize the Cmd frame for the controller */
		(void) ddi_dma_sync(cmd->frame_dma_obj.dma_handle, 0, 0,
		    DDI_DMA_SYNC_FORDEV);

		instance->func_ptr->issue_cmd(cmd, instance);

		return (TRAN_ACCEPT);

	} else {
		instance->func_ptr->issue_cmd(cmd, instance);
		(void) wait_for_outstanding_poll_io(instance);

	return (TRAN_ACCEPT);
	}
}

/*
 * prepare the pkt:
 * the pkt may have been resubmitted or just reused so
 * initialize some fields and do some checks.
 */
int
mrsas_tbolt_prepare_pkt(struct scsa_cmd *acmd)
{
	struct scsi_pkt	*pkt = CMD2PKT(acmd);


	/*
	 * Reinitialize some fields that need it; the packet may
	 * have been resubmitted
	 */
	pkt->pkt_reason = CMD_CMPLT;
	pkt->pkt_state = 0;
	pkt->pkt_statistics = 0;
	pkt->pkt_resid = 0;

	/*
	 * zero status byte.
	 */
	*(pkt->pkt_scbp) = 0;

	return (0);
}


int
mr_sas_tbolt_build_sgl(struct mrsas_instance *instance,
    struct scsa_cmd *acmd,
    struct mrsas_cmd *cmd,
    Mpi2RaidSCSIIORequest_t *scsi_raid_io,
    uint32_t *datalen)
{
	uint32_t		MaxSGEs;
	int			sg_to_process;
	uint32_t		i, j;
	uint32_t		numElements, endElement;
	Mpi25IeeeSgeChain64_t	*ieeeChainElement = NULL;
	Mpi25IeeeSgeChain64_t	*scsi_raid_io_sgl_ieee = NULL;

	con_log(CL_ANN1, (CE_NOTE,
	    "chkpnt: Building Chained SGL :%d", __LINE__));
	/* Calulate SGE size in number of Words(32bit) */

	ddi_put8(instance->mpi2_frame_pool_dma_obj.acc_handle,
	    &scsi_raid_io->ChainOffset, 0);

	/* Clear the datalen before updating it. */
	*datalen = 0;

	MaxSGEs = instance->max_sge_in_main_msg;

	ddi_put16(instance->mpi2_frame_pool_dma_obj.acc_handle,
	    &scsi_raid_io->SGLFlags,
	    MPI2_SGE_FLAGS_64_BIT_ADDRESSING);

	/* set data transfer flag */
	if (acmd->cmd_flags & CFLAG_DMASEND) {
		ddi_put32(instance->mpi2_frame_pool_dma_obj.acc_handle,
		    &scsi_raid_io->Control,
		    MPI2_SCSIIO_CONTROL_WRITE);
	} else {
		ddi_put32(instance->mpi2_frame_pool_dma_obj.acc_handle,
		    &scsi_raid_io->Control, MPI2_SCSIIO_CONTROL_READ);
	}

	/* ToDO: Why is this? Where is this assigned */
	numElements = acmd->cmd_cookiecnt;

	con_log(CL_ANN1, (CE_NOTE, "[SGE Count]:%x", numElements));

	if (numElements > instance->max_num_sge) {
		con_log(CL_ANN1, (CE_NOTE,
		    "[Max SGE Count Exceeded]:%x", numElements));
		return (numElements);
	}

	ddi_put8(instance->mpi2_frame_pool_dma_obj.acc_handle,
	    &scsi_raid_io->RaidContext.numSGE, (uint8_t)numElements);

	/* set end element in main message frame */
	endElement = (numElements <= MaxSGEs) ? numElements : (MaxSGEs - 1);

	/* prepare the scatter-gather list for the firmware */
	scsi_raid_io_sgl_ieee =
	    (Mpi25IeeeSgeChain64_t *)&scsi_raid_io->SGL.IeeeChain;

	for (i = 0; i < endElement; i++, scsi_raid_io_sgl_ieee++) {
		ddi_put64(instance->mpi2_frame_pool_dma_obj.acc_handle,
		    &scsi_raid_io_sgl_ieee->Address,
		    acmd->cmd_dmacookies[i].dmac_laddress);

		ddi_put32(instance->mpi2_frame_pool_dma_obj.acc_handle,
		    &scsi_raid_io_sgl_ieee->Length,
		    acmd->cmd_dmacookies[i].dmac_size);

		ddi_put8(instance->mpi2_frame_pool_dma_obj.acc_handle,
		    &scsi_raid_io_sgl_ieee->Flags, 0);

		*datalen += acmd->cmd_dmacookies[i].dmac_size;
		con_log(CL_ANN1, (CE_NOTE, "[SGL Address]:%lx",
		    (unsigned long)scsi_raid_io_sgl_ieee->Address));
		con_log(CL_ANN1, (CE_NOTE, "[SGL Length]:%x",
		    scsi_raid_io_sgl_ieee->Length));
		con_log(CL_ANN1, (CE_NOTE, "[SGL Flags]:%x",
		    scsi_raid_io_sgl_ieee->Flags));
	}

	ddi_put8(instance->mpi2_frame_pool_dma_obj.acc_handle,
	    &scsi_raid_io->ChainOffset, 0);

	/* check if chained SGL required */
	if (i < numElements) {

		con_log(CL_ANN1, (CE_NOTE, "[Chain Element index]:%x", i));
		ddi_put8(instance->mpi2_frame_pool_dma_obj.acc_handle,
		    &scsi_raid_io->ChainOffset,
		    (U8)instance->chain_offset_io_req);

		/* prepare physical chain element */
		ieeeChainElement  = scsi_raid_io_sgl_ieee;

		ddi_put8(instance->mpi2_frame_pool_dma_obj.acc_handle,
		    &ieeeChainElement->NextChainOffset, 0);

		ddi_put8(instance->mpi2_frame_pool_dma_obj.acc_handle,
		    &ieeeChainElement->Flags,
		    (IEEE_SGE_FLAGS_CHAIN_ELEMENT |
		    MPI2_IEEE_SGE_FLAGS_IOCPLBNTA_ADDR));

		ddi_put32(instance->mpi2_frame_pool_dma_obj.acc_handle,
		    &ieeeChainElement->Length,
		    (sizeof (MPI2_SGE_IO_UNION) * (numElements - i)));

		ddi_put64(instance->mpi2_frame_pool_dma_obj.acc_handle,
		    &ieeeChainElement->Address,
		    (U64)cmd->sgl_phys_addr);

		sg_to_process = numElements - i;

		con_log(CL_ANN1, (CE_NOTE,
		    "[Additional SGE Count]:%x", endElement));

		/* point to the chained SGL buffer */
		scsi_raid_io_sgl_ieee = (Mpi25IeeeSgeChain64_t *)cmd->sgl;

		/* build rest of the SGL in chained buffer */
		for (j = 0; j < sg_to_process; j++, scsi_raid_io_sgl_ieee++) {
			con_log(CL_ANN1, (CE_NOTE, "[remaining SGL]:%x", i));

			ddi_put64(instance->mpi2_frame_pool_dma_obj.acc_handle,
			    &scsi_raid_io_sgl_ieee->Address,
			    acmd->cmd_dmacookies[i].dmac_laddress);

			ddi_put32(instance->mpi2_frame_pool_dma_obj.acc_handle,
			    &scsi_raid_io_sgl_ieee->Length,
			    acmd->cmd_dmacookies[i].dmac_size);

			ddi_put8(instance->mpi2_frame_pool_dma_obj.acc_handle,
			    &scsi_raid_io_sgl_ieee->Flags, 0);

			*datalen += acmd->cmd_dmacookies[i].dmac_size;
			con_log(CL_ANN1, (CE_NOTE,
			    "[SGL Address]:%lx",
			    (unsigned long)scsi_raid_io_sgl_ieee->Address));
			con_log(CL_ANN1, (CE_NOTE,
			    "[SGL Length]:%x", scsi_raid_io_sgl_ieee->Length));
			con_log(CL_ANN1, (CE_NOTE,
			    "[SGL Flags]:%x", scsi_raid_io_sgl_ieee->Flags));
			i++;
		}
	}

	return (0);
} /*end of BuildScatterGather */

/*
 * build_cmd
 */
struct mrsas_cmd *
mrsas_tbolt_build_cmd(struct mrsas_instance *instance, struct scsi_address *ap,
    struct scsi_pkt *pkt, uchar_t *cmd_done)
{
	uint8_t		fp_possible = 0;
	uint32_t	index;
	uint32_t	lba_count;
	uint64_t	start_lba_lo;
	struct mrsas_cmd		*cmd = NULL;
	struct scsa_cmd			*acmd = PKT2CMD(pkt);
	MRSAS_REQUEST_DESCRIPTOR_UNION 	*ReqDescUnion;
	Mpi2RaidSCSIIORequest_t 	*scsi_raid_io;
	uint32_t			datalen;
	struct IO_REQUEST_INFO io_info;
	MR_FW_RAID_MAP_ALL *local_map_ptr;

	con_log(CL_ANN1, (CE_NOTE,
	    "chkpnt: Entered mrsas_tbolt_build_cmd:%d", __LINE__));

	/* find out if this is logical or physical drive command.  */
	acmd->islogical = MRDRV_IS_LOGICAL(ap);
	acmd->device_id = MAP_DEVICE_ID(instance, ap);

	*cmd_done = 0;

	/* get the command packet */
	if (!(cmd = get_raid_msg_pkt(instance))) {
		return (NULL);
	}
	cmd->retry_count_for_ocr = 0;

	index = cmd->index;
	ReqDescUnion =  mr_sas_get_request_descriptor(instance, index);
	if (!ReqDescUnion) {
		con_log(CL_ANN1, (CE_NOTE, "[NULL REQDESC]"));
		return_raid_msg_pkt(instance, cmd);
		return (NULL);
	}

	ReqDescUnion->Words.Low = 0;
	ReqDescUnion->Words.High = 0;
	ReqDescUnion->SCSIIO.SMID = cmd->SMID;
	ReqDescUnion->SCSIIO.RequestFlags =
	    (MPI2_REQ_DESCRIPT_FLAGS_LD_IO <<
	    MPI2_REQ_DESCRIPT_FLAGS_TYPE_SHIFT);


	cmd->request_desc = ReqDescUnion;

	cmd->pkt = pkt;
	cmd->cmd = acmd;

	/* lets get the command directions */
	if (acmd->cmd_flags & CFLAG_DMASEND) {
		if (acmd->cmd_flags & CFLAG_CONSISTENT) {
			(void) ddi_dma_sync(acmd->cmd_dmahandle,
			    acmd->cmd_dma_offset, acmd->cmd_dma_len,
			    DDI_DMA_SYNC_FORDEV);
		}
	} else if (acmd->cmd_flags & ~CFLAG_DMASEND) {
		if (acmd->cmd_flags & CFLAG_CONSISTENT) {
			(void) ddi_dma_sync(acmd->cmd_dmahandle,
			    acmd->cmd_dma_offset, acmd->cmd_dma_len,
			    DDI_DMA_SYNC_FORCPU);
		}
	} else {
		con_log(CL_ANN, (CE_NOTE, "NO DMA\n"));
	}

	switch (pkt->pkt_cdbp[0]) {
	case SCMD_READ:
	case SCMD_WRITE:
	case SCMD_READ_G1:
	case SCMD_WRITE_G1:
	if (acmd->islogical) {
		/* Initialize sense Information */
		if (cmd->sense1 == NULL) {
			con_log(CL_ANN, (CE_NOTE,
			    "tbolt_build_cmd NULL Senseptr n"));
		}
		bzero(cmd->sense1, SENSE_LENGTH);
		con_log(CL_ANN, (CE_NOTE,
		    "tbolt_build_cmd CDB[0] = %x\n", pkt->pkt_cdbp[0]));
		if (acmd->cmd_cdblen == CDB_GROUP0) {
			lba_count = (uint16_t)(pkt->pkt_cdbp[4]);
			start_lba_lo =
			    ((uint32_t)(pkt->pkt_cdbp[3]) |
			    ((uint32_t)(pkt->pkt_cdbp[2]) << 8) |
			    ((uint32_t)((pkt->pkt_cdbp[1]) & 0x1F) << 16));
		} else if (acmd->cmd_cdblen == CDB_GROUP1) {
			lba_count =
			    (((uint16_t)(pkt->pkt_cdbp[8])) |
			    ((uint16_t)(pkt->pkt_cdbp[7]) << 8));

			start_lba_lo =
			    (((uint32_t)(pkt->pkt_cdbp[5])) |
			    ((uint32_t)(pkt->pkt_cdbp[4]) << 8) |
			    ((uint32_t)(pkt->pkt_cdbp[3]) << 16) |
			    ((uint32_t)(pkt->pkt_cdbp[2]) << 24));

		} else if (acmd->cmd_cdblen == CDB_GROUP2) {
			lba_count = (
			    ((uint16_t)(pkt->pkt_cdbp[9])) |
			    ((uint16_t)(pkt->pkt_cdbp[8]) << 8) |
			    ((uint16_t)(pkt->pkt_cdbp[7]) << 16) |
			    ((uint16_t)(pkt->pkt_cdbp[6]) << 24));

			start_lba_lo =
			    (((uint32_t)(pkt->pkt_cdbp[5])) |
			    ((uint32_t)(pkt->pkt_cdbp[4]) << 8) |
			    ((uint32_t)(pkt->pkt_cdbp[3]) << 16) |
			    ((uint32_t)(pkt->pkt_cdbp[2]) << 24));

		} else if (acmd->cmd_cdblen == CDB_GROUP3) {
			lba_count = (
			    ((uint16_t)(pkt->pkt_cdbp[13])) |
			    ((uint16_t)(pkt->pkt_cdbp[12]) << 8) |
			    ((uint16_t)(pkt->pkt_cdbp[11]) << 16) |
			    ((uint16_t)(pkt->pkt_cdbp[10]) << 24));

			start_lba_lo = (
			    ((uint32_t)(pkt->pkt_cdbp[9])) |
			    ((uint32_t)(pkt->pkt_cdbp[8]) << 8) |
			    ((uint32_t)(pkt->pkt_cdbp[7]) << 16) |
			    ((uint32_t)(pkt->pkt_cdbp[6]) << 24));

			start_lba_lo = (
			    ((uint32_t)(pkt->pkt_cdbp[5])) |
			    ((uint32_t)(pkt->pkt_cdbp[4]) << 8) |
			    ((uint32_t)(pkt->pkt_cdbp[3]) << 16) |
			    ((uint32_t)(pkt->pkt_cdbp[2]) << 24));
		}

		/* get raid message frame pointer */
		scsi_raid_io = (Mpi2RaidSCSIIORequest_t *)cmd->scsi_io_request;

		/* zero out top part of the raid message frame */

		/*  Initialize raid message frame */

		ddi_rep_put8(instance->mpi2_frame_pool_dma_obj.acc_handle,
		    (uint8_t *)pkt->pkt_cdbp,
		    (uint8_t *)scsi_raid_io->CDB.CDB32,
		    acmd->cmd_cdblen, DDI_DEV_AUTOINCR);

		(void) memset(&io_info, 0, sizeof (struct IO_REQUEST_INFO));
		io_info.ldStartBlock = (U64)start_lba_lo;
		io_info.ldTgtId = acmd->device_id;
		io_info.numBlocks = lba_count;

		if (acmd->cmd_flags & CFLAG_DMASEND)
			io_info.isRead = 0;
		else
			io_info.isRead = 1;

			instance->fp_change = 0;

		local_map_ptr = instance->ld_map[(instance->map_id & 1)];

		if (MR_TargetIdToLdGet(acmd->device_id, local_map_ptr) >=
		    MAX_LOGICAL_DRIVES) {
			fp_possible = 0;
		} else {
			if ((enable_fp == 1) && (instance->fp_change == 0)) {
					if (MR_BuildRaidContext(&io_info,
					    &scsi_raid_io->RaidContext,
					    local_map_ptr)) {
						con_log(CL_ANN1, (CE_NOTE,
						    "Fast Path Code"));
					fp_possible = io_info.fpOkForIo;
			}
			} else {
				con_log(CL_ANN1, (CE_NOTE,
				    "Fast Path Not Possible"
				    "Either FP disabled or Sync required\n"));
				fp_possible = 0;
			}
		}
		if (fp_possible) {
			if (io_info.ldPI == 1) {
				con_log(CL_ANN1, (CE_NOTE,
				    "DIF supported by VD\n"));
			} else {
				con_log(CL_ANN1, (CE_NOTE, "No DIF support\n"));
			}

			/* prepare 32byte CDB for DIF capable  disk */
			if (io_info.ldPI) {
				con_log(CL_ANN1, (CE_NOTE,
				    "Preparing PI for DIF enabled VD\n"));
				(void) mrsas_tbolt_prepare_cdb(
				    scsi_raid_io->CDB.CDB32,
				    &io_info,
				    scsi_raid_io, start_lba_lo);
			} else {
				con_log(CL_ANN1, (CE_NOTE,
				    "DIF NOT enabled for VD\n"));
				mrsas_tbolt_set_pd_lba(scsi_raid_io->CDB.CDB32,
				    acmd->cmd_cdblen, io_info.pdBlock,
				    io_info.numBlocks);
			}

			ddi_put8(instance->mpi2_frame_pool_dma_obj.acc_handle,
			    &scsi_raid_io->Function,
			    MPI2_FUNCTION_SCSI_IO_REQUEST);

			ddi_put16(instance->mpi2_frame_pool_dma_obj.acc_handle,
			    &scsi_raid_io->DevHandle,
			    io_info.devHandle);

			ReqDescUnion->SCSIIO.RequestFlags =
			    (MPI2_REQ_DESCRIPT_FLAGS_HIGH_PRIORITY <<
			    MPI2_REQ_DESCRIPT_FLAGS_TYPE_SHIFT);

			if ((instance->load_balance_info[acmd->
			    device_id].loadBalanceFlag) &&
			    (io_info.isRead)) {
				io_info.devHandle =
				    get_updated_dev_handle(
				    &instance->load_balance_info[acmd->
				    device_id],
				    &io_info);
				cmd->load_balance_flag |=
				    MEGASAS_LOAD_BALANCE_FLAG;
			} else
				cmd->load_balance_flag &=
				    ~MEGASAS_LOAD_BALANCE_FLAG;

			ReqDescUnion->SCSIIO.DevHandle = io_info.devHandle;

		} else {
			ddi_put8(instance->mpi2_frame_pool_dma_obj.acc_handle,
			    &scsi_raid_io->Function,
			    MPI2_FUNCTION_LD_IO_REQUEST);

			ddi_put16(instance->mpi2_frame_pool_dma_obj.acc_handle,
			    &scsi_raid_io->DevHandle, acmd->device_id);

			ReqDescUnion->SCSIIO.RequestFlags =
			    (MPI2_REQ_DESCRIPT_FLAGS_LD_IO <<
			    MPI2_REQ_DESCRIPT_FLAGS_TYPE_SHIFT);
		} /* Not FP */

		ddi_put16(instance->mpi2_frame_pool_dma_obj.acc_handle,
		    &scsi_raid_io->RaidContext.ldTargetId,
		    acmd->device_id);

		ddi_put8(instance->mpi2_frame_pool_dma_obj.acc_handle,
		    &scsi_raid_io->RaidContext.status, 0);


		ddi_put8(instance->mpi2_frame_pool_dma_obj.acc_handle,
		    &scsi_raid_io->RaidContext.extStatus, 0);

		ddi_put16(instance->mpi2_frame_pool_dma_obj.acc_handle,
		    &scsi_raid_io->IoFlags,
		    acmd->cmd_cdblen);

		ddi_put32(instance->mpi2_frame_pool_dma_obj.acc_handle,
		    &scsi_raid_io->SenseBufferLowAddress,
		    cmd->sense_phys_addr1);

		ddi_put8(instance->mpi2_frame_pool_dma_obj.acc_handle,
		    &scsi_raid_io->SenseBufferLength,
		    SENSE_LENGTH);

		ddi_put8(instance->mpi2_frame_pool_dma_obj.acc_handle,
		    &scsi_raid_io->SGLOffset0,
		    offsetof(MPI2_RAID_SCSI_IO_REQUEST, SGL) / 4);

		(void) mr_sas_tbolt_build_sgl(instance, acmd, cmd,
		    scsi_raid_io, &datalen);

		ddi_put32(instance->mpi2_frame_pool_dma_obj.acc_handle,
		    &scsi_raid_io->DataLength, datalen);

		con_log(CL_ANN, (CE_NOTE,
		    "tbolt_build_cmd Read/Write = %x\n",
		    pkt->pkt_cdbp[0]));
		con_log(CL_ANN, (CE_NOTE,
		    "tbolt_build_cmd TargetID = %x\n", acmd->device_id));
		con_log(CL_ANN, (CE_NOTE,
		    "data length = %x\n", scsi_raid_io->DataLength));
		con_log(CL_ANN, (CE_NOTE,
		    "cdb length = %x\n",  acmd->cmd_cdblen));
		}
		break;
		/* fall through For all non-rd/wr cmds */
	default:
		switch (pkt->pkt_cdbp[0]) {
		case 0x35: { /* SCMD_SYNCHRONIZE_CACHE */
			return_raid_msg_pkt(instance, cmd);
			*cmd_done = 1;
			return (NULL);
		}

		case SCMD_MODE_SENSE:
		case SCMD_MODE_SENSE_G1: {
			union scsi_cdb	*cdbp;
			uint16_t	page_code;

			cdbp = (void *)pkt->pkt_cdbp;
			page_code = (uint16_t)cdbp->cdb_un.sg.scsi[0];
			switch (page_code) {
			case 0x3:
			case 0x4:
				(void) mrsas_mode_sense_build(pkt);
				return_raid_msg_pkt(instance, cmd);
				*cmd_done = 1;
				return (NULL);
			}
			break;
		}
		default: {
			/*
			 * Here we need tol handle PASSTHRU for
			 * Logical Devices. Like Inquiry etc.
			 * get raid message frame pointer
			 */
			scsi_raid_io =
			    (Mpi2RaidSCSIIORequest_t *)cmd->scsi_io_request;

			/* zero out top part of the raid message frame */

			/*  Initialize raid message frame */

			ddi_rep_put8
			    (instance->mpi2_frame_pool_dma_obj.acc_handle,
			    (uint8_t *)pkt->pkt_cdbp,
			    (uint8_t *)scsi_raid_io->CDB.CDB32,
			    acmd->cmd_cdblen, DDI_DEV_AUTOINCR);


			ddi_put16(instance->mpi2_frame_pool_dma_obj.acc_handle,
			    &scsi_raid_io->RaidContext.ldTargetId,
			    acmd->device_id);

			ddi_put8(instance->mpi2_frame_pool_dma_obj.acc_handle,
			    &scsi_raid_io->RaidContext.status, 0);


			ddi_put8(instance->mpi2_frame_pool_dma_obj.acc_handle,
			    &scsi_raid_io->RaidContext.extStatus, 0);
			ddi_put8(instance->
			    mpi2_frame_pool_dma_obj.acc_handle,
			    &scsi_raid_io->Function,
			    MPI2_FUNCTION_LD_IO_REQUEST);

			ddi_put8(instance->
			    mpi2_frame_pool_dma_obj.
			    acc_handle,
			    &scsi_raid_io->LUN[0],
			    acmd->lun);
			ReqDescUnion->SCSIIO.RequestFlags =
			    (MPI2_REQ_DESCRIPT_FLAGS_SCSI_IO <<
			    MPI2_REQ_DESCRIPT_FLAGS_TYPE_SHIFT);

			ddi_put16(instance->mpi2_frame_pool_dma_obj.acc_handle,
			    &scsi_raid_io->DevHandle,
			    acmd->device_id);

			ddi_put16(instance->mpi2_frame_pool_dma_obj.acc_handle,
			    &scsi_raid_io->IoFlags, acmd->cmd_cdblen);

			ddi_put32(instance->mpi2_frame_pool_dma_obj.acc_handle,
			    &scsi_raid_io->SenseBufferLowAddress,
			    cmd->sense_phys_addr1);

			ddi_put8(instance->mpi2_frame_pool_dma_obj.acc_handle,
			    &scsi_raid_io->SenseBufferLength, SENSE_LENGTH);

			ddi_put8(instance->mpi2_frame_pool_dma_obj.acc_handle,
			    &scsi_raid_io->SGLOffset0,
			    offsetof(MPI2_RAID_SCSI_IO_REQUEST, SGL) / 4);

			(void) mr_sas_tbolt_build_sgl(instance, acmd, cmd,
			    scsi_raid_io, &datalen);

			ddi_put32(instance->mpi2_frame_pool_dma_obj.acc_handle,
			    &scsi_raid_io->DataLength, datalen);


			con_log(CL_ANN, (CE_NOTE,
			    "tbolt_build_cmd CDB[0] = %x\n",
			    pkt->pkt_cdbp[0]));
			con_log(CL_ANN, (CE_NOTE,
			    "tbolt_build_cmd TargetID = %x\n",
			    acmd->device_id));
			con_log(CL_ANN, (CE_NOTE,
			    "data length = %x\n",
			    scsi_raid_io->DataLength));
			con_log(CL_ANN, (CE_NOTE,
			    "cdb length = %x\n",
			    acmd->cmd_cdblen));
			}
			break;
		}

	}
	return (cmd);
}

/*
 * mrsas_tbolt_tran_init_pkt - allocate & initialize a scsi_pkt structure
 * @ap:
 * @pkt:
 * @bp:
 * @cmdlen:
 * @statuslen:
 * @tgtlen:
 * @flags:
 * @callback:
 *
 * The tran_init_pkt() entry point allocates and initializes a scsi_pkt
 * structure and DMA resources for a target driver request. The
 * tran_init_pkt() entry point is called when the target driver calls the
 * SCSA function scsi_init_pkt(). Each call of the tran_init_pkt() entry point
 * is a request to perform one or more of three possible services:
 *  - allocation and initialization of a scsi_pkt structure
 *  - allocation of DMA resources for data transfer
 *  - reallocation of DMA resources for the next portion of the data transfer
 */
struct scsi_pkt *
mrsas_tbolt_tran_init_pkt(struct scsi_address *ap,
	register struct scsi_pkt *pkt,
	struct buf *bp, int cmdlen, int statuslen, int tgtlen,
	int flags, int (*callback)(), caddr_t arg)
{
	struct scsa_cmd	*acmd;
	struct mrsas_instance	*instance;
	struct scsi_pkt	*new_pkt;

	instance = ADDR2MR(ap);

	/* step #1 : pkt allocation */
	if (pkt == NULL) {
		pkt = scsi_hba_pkt_alloc(instance->dip, ap, cmdlen, statuslen,
		    tgtlen, sizeof (struct scsa_cmd), callback, arg);
		if (pkt == NULL) {
			return (NULL);
		}

		acmd = PKT2CMD(pkt);

		/*
		 * Initialize the new pkt - we redundantly initialize
		 * all the fields for illustrative purposes.
		 */
		acmd->cmd_pkt		= pkt;
		acmd->cmd_flags		= 0;
		acmd->cmd_scblen	= statuslen;
		acmd->cmd_cdblen	= cmdlen;
		acmd->cmd_dmahandle	= NULL;
		acmd->cmd_ncookies	= 0;
		acmd->cmd_cookie	= 0;
		acmd->cmd_cookiecnt	= 0;
		acmd->cmd_nwin		= 0;

		pkt->pkt_address	= *ap;
		pkt->pkt_comp		= (void (*)())NULL;
		pkt->pkt_flags		= 0;
		pkt->pkt_time		= 0;
		pkt->pkt_resid		= 0;
		pkt->pkt_state		= 0;
		pkt->pkt_statistics	= 0;
		pkt->pkt_reason		= 0;
		new_pkt			= pkt;
	} else {
		acmd = PKT2CMD(pkt);
		new_pkt = NULL;
	}

	/* step #2 : dma allocation/move */
	if (bp && bp->b_bcount != 0) {
		if (acmd->cmd_dmahandle == NULL) {
			if (mrsas_dma_alloc(instance, pkt, bp, flags,
			    callback) == DDI_FAILURE) {
				if (new_pkt) {
					scsi_hba_pkt_free(ap, new_pkt);
				}
				return ((struct scsi_pkt *)NULL);
			}
		} else {
			if (mrsas_dma_move(instance, pkt, bp) == DDI_FAILURE) {
				return ((struct scsi_pkt *)NULL);
			}
		}
	}
	return (pkt);
}


int
tbolt_read_fw_status_reg(struct mrsas_instance *instance)
{
	return ((int)RD_OB_SCRATCH_PAD_0(instance));
}

void
tbolt_issue_cmd(struct mrsas_cmd *cmd, struct mrsas_instance *instance)
{
	struct scsi_pkt *pkt;
	MRSAS_REQUEST_DESCRIPTOR_UNION *req_desc = cmd->request_desc;

	atomic_add_16(&instance->fw_outstanding, 1);
	cmd->cmd_issued = MRSAS_TRUE;
	con_log(CL_ANN1, (CE_NOTE,
	    "Issuing command in Normal mode cmd->[SMID]=0x%X", cmd->SMID));

	con_log(CL_ANN, (CE_NOTE, "[SMID]%x", cmd->SMID));
	con_log(CL_ANN1, (CE_NOTE,
	    " [req desc Words.High] %x \n",
	    (unsigned int)req_desc->Words.High));
	con_log(CL_ANN1, (CE_NOTE,
	    " [req desc low part] %x \n", (unsigned int)req_desc->Words.Low));
	pkt = cmd->pkt;

	if (pkt) {
		con_log(CL_ANN1, (CE_CONT, "%llx :TBOLT issue_cmd_ppc:"
		    "ISSUED CMD TO FW : called : cmd:"
		    ": %p instance : %p pkt : %p pkt_time : %x\n",
		    gethrtime(), (void *)cmd, (void *)instance,
		    (void *)pkt, cmd->drv_pkt_time));
		if (instance->adapterresetinprogress) {
			cmd->drv_pkt_time = (unsigned int)debug_timeout_g;
			con_log(CL_ANN1, (CE_NOTE,
			    "TBOLT Reset the scsi_pkt timer"));
		} else {
			push_pending_mfi_pkt(instance, cmd);
		}

	} else {
		con_log(CL_ANN1, (CE_CONT, "%llx :TBOLT issue_cmd_ppc:"
		    "ISSUED CMD TO FW : called : cmd : %p, instance: %p"
		    "(NO PKT)\n", gethrtime(), (void *)cmd, (void *)instance));
	}
	con_log(CL_ANN1, (CE_NOTE,
	    " [req desc high part] %x \n",
	    (unsigned int)(req_desc->Words.High)));

	/* Issue the command to the FW */
	mutex_enter(&instance->reg_write_mtx);
	WR_IB_LOW_QPORT((uint32_t)(req_desc->Words.Low), instance);
	WR_IB_HIGH_QPORT((uint32_t)(req_desc->Words.High), instance);
	mutex_exit(&instance->reg_write_mtx);
	if (mrsas_check_acc_handle(instance->regmap_handle) != DDI_SUCCESS) {
		ddi_fm_service_impact(instance->dip, DDI_SERVICE_UNAFFECTED);
	}
}

/*
 * issue_cmd_in_sync_mode
 */
int
tbolt_issue_cmd_in_sync_mode(struct mrsas_instance *instance,
    struct mrsas_cmd *cmd)
{
	int		i;
	uint32_t	msecs = MFI_POLL_TIMEOUT_SECS * MILLISEC;
	MRSAS_REQUEST_DESCRIPTOR_UNION *req_desc = cmd->request_desc;

	struct mrsas_header	*hdr;
	hdr = (struct mrsas_header *)&cmd->frame->hdr;
	con_log(CL_ANN1, (CE_NOTE, "TBOLT issue_cmd_in_sync_mode_ppc: called"));
	con_log(CL_ANN1, (CE_NOTE,
	    "Issuing command in sync mode cmd->[SMID]=0x%X", cmd->SMID));


	if (instance->adapterresetinprogress) {
		cmd->drv_pkt_time = ddi_get16
		    (cmd->frame_dma_obj.acc_handle, &hdr->timeout);
		if (cmd->drv_pkt_time < debug_timeout_g)
			cmd->drv_pkt_time = (uint16_t)debug_timeout_g;
		con_log(CL_ANN1, (CE_NOTE, "sync_mode_ppc:TBOLT "
		    "issue and return in reset case\n"));
		mutex_enter(&instance->reg_write_mtx);
		WR_IB_LOW_QPORT((uint32_t)(req_desc->Words.Low), instance);
		WR_IB_HIGH_QPORT((uint32_t)(req_desc->Words.High), instance);
		mutex_exit(&instance->reg_write_mtx);
		return (DDI_SUCCESS);
	} else {
		con_log(CL_ANN1, (CE_NOTE, "sync_mode_ppc: pushing the pkt\n"));
		push_pending_mfi_pkt(instance, cmd);
	}
	con_log(CL_ANN1, (CE_NOTE,
	    "HighQport offset :%p",
	    (void *)((uintptr_t)(instance)->regmap + IB_HIGH_QPORT)));

	con_log(CL_ANN1, (CE_NOTE,
	    "LowQport offset :%p",
	    (void *)((uintptr_t)(instance)->regmap + IB_LOW_QPORT)));
	cmd->sync_cmd = MRSAS_TRUE;
	cmd->cmd_status =  ENODATA;
	mutex_enter(&instance->reg_write_mtx);

	WR_IB_LOW_QPORT((uint32_t)(req_desc->Words.Low), instance);
	WR_IB_HIGH_QPORT((uint32_t)(req_desc->Words.High), instance);

	mutex_exit(&instance->reg_write_mtx);

	con_log(CL_ANN1, (CE_NOTE,
	    " req desc high part %lx \n",
	    (unsigned long)(req_desc->Words.High)));
	con_log(CL_ANN, (CE_NOTE,
	    " req desc low part %lx \n", (unsigned long)req_desc->Words.Low));

	mutex_enter(&instance->int_cmd_mtx);

	for (i = 0; i < msecs && (cmd->cmd_status == ENODATA); i++) {
		cv_wait(&instance->int_cmd_cv, &instance->int_cmd_mtx);
	}

	mutex_exit(&instance->int_cmd_mtx);


	if (i < (msecs -1)) {
		return (DDI_SUCCESS);
	} else {
		return (DDI_FAILURE);
	}
}

/*
 * issue_cmd_in_poll_mode
 */
int
tbolt_issue_cmd_in_poll_mode(struct mrsas_instance *instance,
    struct mrsas_cmd *cmd)
{
	int		i;
	uint16_t	flags;
	uint32_t	msecs = MFI_POLL_TIMEOUT_SECS * MILLISEC;
	struct mrsas_header *frame_hdr;
	MRSAS_REQUEST_DESCRIPTOR_UNION *req_desc = cmd->request_desc;

	con_log(CL_ANN1, (CE_NOTE,
	    "Issuing command in poll mode cmd->[SMID]=0x%X",
	    cmd->SMID));

	cmd->poll_cmd = 1;
	frame_hdr = (struct mrsas_header *)&cmd->frame->hdr;

	ddi_put8(cmd->frame_dma_obj.acc_handle, &frame_hdr->cmd_status,
	    MFI_CMD_STATUS_POLL_MODE);

	flags = ddi_get16(cmd->frame_dma_obj.acc_handle, &frame_hdr->flags);

	flags 	|= MFI_FRAME_DONT_POST_IN_REPLY_QUEUE;

	ddi_put16(cmd->frame_dma_obj.acc_handle, &frame_hdr->flags, flags);

	con_log(CL_ANN1, (CE_NOTE,
	    " req desc low part %lx \n", (unsigned long)req_desc->Words.Low));
	con_log(CL_ANN1, (CE_NOTE,
	    " req desc high part %lx \n",
	    (unsigned long)(req_desc->Words.High)));

	/* issue the frame using inbound queue port */
	mutex_enter(&instance->reg_write_mtx);
	WR_IB_LOW_QPORT((uint32_t)(req_desc->Words.Low), instance);
	WR_IB_HIGH_QPORT((uint32_t)(req_desc->Words.High), instance);
	mutex_exit(&instance->reg_write_mtx);

	for (i = 0; i < msecs && (
	    ddi_get8(cmd->frame_dma_obj.acc_handle, &frame_hdr->cmd_status)
	    == MFI_CMD_STATUS_POLL_MODE); i++) {
		/* wait for cmd_status to change from 0xFF */
		drv_usecwait(MILLISEC); /* wait for 1000 usecs */
	}

	if (ddi_get8(cmd->frame_dma_obj.acc_handle,
	    &frame_hdr->cmd_status) == MFI_CMD_STATUS_POLL_MODE) {
		con_log(CL_ANN1, (CE_NOTE,
		    " cmd failed %lx \n",
		    (unsigned long)(req_desc->Words.Low)));
		return (DDI_FAILURE);
	}
	cmd->poll_cmd = 0;
	return (DDI_SUCCESS);
}

void
tbolt_enable_intr(struct mrsas_instance *instance)
{
	uint32_t	mask;

	WR_OB_INTR_MASK(~(MFI_FUSION_ENABLE_INTERRUPT_MASK), instance);

	/* dummy read to force PCI flush */
	mask = RD_OB_INTR_MASK(instance);
	con_log(CL_DLEVEL3, (CE_NOTE,
	    " Interrupt Mask Register Value= %x \n", mask));
}

void
tbolt_disable_intr(struct mrsas_instance *instance)
{
	uint32_t mask = 0xFFFFFFFF;
	uint32_t status;

	WR_OB_INTR_MASK(mask, instance);

	/* Dummy readl to force pci flush */
	status = RD_OB_INTR_MASK(instance);
	con_log(CL_DLEVEL3, (CE_NOTE,
	    " Interrupt Status Register Value= %x \n", status));
}


int
tbolt_intr_ack(struct mrsas_instance *instance)
{
	uint32_t	status;

	/* check if it is our interrupt */
	status = RD_OB_INTR_STATUS(instance);
	con_log(CL_ANN1, (CE_NOTE,
	    "chkpnt: Entered tbolt_intr_ack status = %d \n", status));

	if (!(status & MFI_FUSION_ENABLE_INTERRUPT_MASK)) {
		return (DDI_INTR_UNCLAIMED);
	}

	if ((status & 1) || (status & MFI_FUSION_ENABLE_INTERRUPT_MASK)) {
		/* clear the interrupt by writing back the same value */
		WR_OB_INTR_STATUS(status, instance);
		/* dummy READ */
		(void) RD_OB_INTR_STATUS(instance);
		}
	return (DDI_INTR_CLAIMED);
}

/*
 * get_raid_msg_pkt : Get a command from the free pool
 * After successful allocation, the caller of this routine
 * must clear the frame buffer (memset to zero) before
 * using the packet further.
 *
 * ***** Note *****
 * After clearing the frame buffer the context id of the
 * frame buffer SHOULD be restored back.
 */

struct mrsas_cmd *
get_raid_msg_pkt(struct mrsas_instance *instance)
{
	mlist_t			*head = &instance->cmd_pool_list;
	struct mrsas_cmd	*cmd = NULL;

	mutex_enter(&instance->cmd_pool_mtx);
	ASSERT(mutex_owned(&instance->cmd_pool_mtx));


	if (!mlist_empty(head)) {
		cmd = mlist_entry(head->next, struct mrsas_cmd, list);
		mlist_del_init(head->next);
	}
	if (cmd != NULL) {
		cmd->pkt = NULL;
		cmd->retry_count_for_ocr = 0;
		cmd->drv_pkt_time = 0;
	}
	mutex_exit(&instance->cmd_pool_mtx);

	if (cmd != NULL)
		bzero(cmd->scsi_io_request,
		    sizeof (Mpi2RaidSCSIIORequest_t));
	return (cmd);
}

struct mrsas_cmd *
get_raid_msg_mfi_pkt(struct mrsas_instance *instance)
{
	mlist_t			*head = &instance->cmd_app_pool_list;
	struct mrsas_cmd	*cmd = NULL;

	mutex_enter(&instance->cmd_app_pool_mtx);
	ASSERT(mutex_owned(&instance->cmd_app_pool_mtx));

	if (!mlist_empty(head)) {
		cmd = mlist_entry(head->next, struct mrsas_cmd, list);
		mlist_del_init(head->next);
	}
	if (cmd != NULL) {
		cmd->retry_count_for_ocr = 0;
		cmd->drv_pkt_time = 0;
		cmd->pkt = NULL;
		cmd->request_desc = NULL;
	}

	mutex_exit(&instance->cmd_app_pool_mtx);

	if (cmd != NULL) {
		bzero(cmd->scsi_io_request,
		    sizeof (Mpi2RaidSCSIIORequest_t));
	}

	return (cmd);
}

/*
 * return_raid_msg_pkt : Return a cmd to free command pool
 */
void
return_raid_msg_pkt(struct mrsas_instance *instance, struct mrsas_cmd *cmd)
{
	mutex_enter(&instance->cmd_pool_mtx);
	ASSERT(mutex_owned(&instance->cmd_pool_mtx));


	mlist_add_tail(&cmd->list, &instance->cmd_pool_list);

	mutex_exit(&instance->cmd_pool_mtx);
}

void
return_raid_msg_mfi_pkt(struct mrsas_instance *instance, struct mrsas_cmd *cmd)
{
	mutex_enter(&instance->cmd_app_pool_mtx);
	ASSERT(mutex_owned(&instance->cmd_app_pool_mtx));

	mlist_add_tail(&cmd->list, &instance->cmd_app_pool_list);

	mutex_exit(&instance->cmd_app_pool_mtx);
}


void
mr_sas_tbolt_build_mfi_cmd(struct mrsas_instance *instance,
    struct mrsas_cmd *cmd)
{
	Mpi2RaidSCSIIORequest_t 	*scsi_raid_io;
	Mpi25IeeeSgeChain64_t		*scsi_raid_io_sgl_ieee;
	MRSAS_REQUEST_DESCRIPTOR_UNION 	*ReqDescUnion;
	uint32_t			index;

	if (!instance->tbolt) {
		con_log(CL_ANN, (CE_NOTE, "Not MFA enabled.\n"));
		return;
	}

	index = cmd->index;

	ReqDescUnion =
	    mr_sas_get_request_descriptor(instance, index);

	if (!ReqDescUnion) {
		con_log(CL_ANN1, (CE_NOTE, "[NULL REQDESC]"));
		return;
	}

	con_log(CL_ANN, (CE_NOTE, "[SMID]%x", cmd->SMID));

	ReqDescUnion->Words.Low = 0;
	ReqDescUnion->Words.High = 0;

	ReqDescUnion->SCSIIO.RequestFlags =
	    (MPI2_REQ_DESCRIPT_FLAGS_SCSI_IO <<
	    MPI2_REQ_DESCRIPT_FLAGS_TYPE_SHIFT);

	ReqDescUnion->SCSIIO.SMID = cmd->SMID;

	cmd->request_desc = ReqDescUnion;

	/* get raid message frame pointer */
	scsi_raid_io = (Mpi2RaidSCSIIORequest_t *)cmd->scsi_io_request;

	ddi_put8(instance->mpi2_frame_pool_dma_obj.acc_handle,
	    &scsi_raid_io->Function,
	    MPI2_FUNCTION_PASSTHRU_IO_REQUEST);

	ddi_put8(instance->mpi2_frame_pool_dma_obj.acc_handle,
	    &scsi_raid_io->SGLOffset0,
	    offsetof(MPI2_RAID_SCSI_IO_REQUEST, SGL) / 4);

	ddi_put8(instance->mpi2_frame_pool_dma_obj.acc_handle,
	    &scsi_raid_io->ChainOffset,
	    (U8)offsetof(MPI2_RAID_SCSI_IO_REQUEST, SGL) / 16);

	ddi_put32(instance->mpi2_frame_pool_dma_obj.acc_handle,
	    &scsi_raid_io->SenseBufferLowAddress,
	    cmd->sense_phys_addr1);


	scsi_raid_io_sgl_ieee =
	    (Mpi25IeeeSgeChain64_t *)&scsi_raid_io->SGL.IeeeChain;

	ddi_put64(instance->mpi2_frame_pool_dma_obj.acc_handle,
	    &scsi_raid_io_sgl_ieee->Address,
	    (U64)cmd->frame_phys_addr);

	ddi_put8(instance->mpi2_frame_pool_dma_obj.acc_handle,
	    &scsi_raid_io_sgl_ieee->Flags,
	    (IEEE_SGE_FLAGS_CHAIN_ELEMENT |
	    MPI2_IEEE_SGE_FLAGS_IOCPLBNTA_ADDR));
	ddi_put32(instance->mpi2_frame_pool_dma_obj.acc_handle,
	    &scsi_raid_io_sgl_ieee->Length, 1024);

	con_log(CL_ANN1, (CE_NOTE,
	    "[MFI CMD PHY ADDRESS]:%lx",
	    (unsigned long)scsi_raid_io_sgl_ieee->Address));
	con_log(CL_ANN1, (CE_NOTE,
	    "[SGL Length]:%x", scsi_raid_io_sgl_ieee->Length));
	con_log(CL_ANN1, (CE_NOTE, "[SGL Flags]:%x",
	    scsi_raid_io_sgl_ieee->Flags));
}


void
tbolt_complete_cmd(struct mrsas_instance *instance,
    struct mrsas_cmd *cmd)
{
	uint8_t				status;
	uint8_t				extStatus;
	uint8_t				arm;
	struct scsa_cmd			*acmd;
	struct scsi_pkt			*pkt;
	struct scsi_arq_status		*arqstat;
	Mpi2RaidSCSIIORequest_t		*scsi_raid_io;
	LD_LOAD_BALANCE_INFO 		*lbinfo;

	scsi_raid_io = (Mpi2RaidSCSIIORequest_t *)cmd->scsi_io_request;

	status = ddi_get8(instance->mpi2_frame_pool_dma_obj.acc_handle,
	    &scsi_raid_io->RaidContext.status);

	con_log(CL_DLEVEL3, (CE_NOTE, "status %x", status));

	extStatus = ddi_get8(instance->mpi2_frame_pool_dma_obj.acc_handle,
	    &scsi_raid_io->RaidContext.extStatus);

	con_log(CL_DLEVEL3, (CE_NOTE, "extStatus %x", extStatus));

	if (status != MFI_STAT_OK) {
		con_log(CL_ANN, (CE_WARN,
		    "IO Cmd Failed SMID %x", cmd->SMID));
	} else {
		con_log(CL_ANN, (CE_NOTE,
		    "IO Cmd Success  SMID %x", cmd->SMID));
	}

	/* regular commands */

	switch (ddi_get8(instance->mpi2_frame_pool_dma_obj.acc_handle,
	    &scsi_raid_io->Function)) {

		case MPI2_FUNCTION_SCSI_IO_REQUEST :  /* Fast Path IO. */
		case MPI2_FUNCTION_LD_IO_REQUEST :   { /* Regular Path IO */
			acmd =	(struct scsa_cmd *)cmd->cmd;

			if (ddi_get8(instance->
			    mpi2_frame_pool_dma_obj.acc_handle,
			    &scsi_raid_io->Function) ==
			    MPI2_FUNCTION_SCSI_IO_REQUEST) {
				lbinfo = &instance->
				    load_balance_info[acmd->device_id];
				if (cmd->load_balance_flag &
				    MEGASAS_LOAD_BALANCE_FLAG) {
					arm = lbinfo->raid1DevHandle[0] ==
					    scsi_raid_io->DevHandle
					    ? 0 : 1;
					lbinfo->scsi_pending_cmds[arm]--;
					cmd->load_balance_flag &=
					    ~MEGASAS_LOAD_BALANCE_FLAG;
				}
				con_log(CL_DLEVEL3, (CE_NOTE,
				    "FastPath IO Completion Success "));
			}
			pkt =	(struct scsi_pkt *)CMD2PKT(acmd);

			if (acmd->cmd_flags & CFLAG_DMAVALID) {
				if (acmd->cmd_flags & CFLAG_CONSISTENT) {
					(void) ddi_dma_sync(acmd->cmd_dmahandle,
					    acmd->cmd_dma_offset,
					    acmd->cmd_dma_len,
					    DDI_DMA_SYNC_FORCPU);
				}
			}

			pkt->pkt_reason		= CMD_CMPLT;
			pkt->pkt_statistics	= 0;
			pkt->pkt_state = STATE_GOT_BUS
			    | STATE_GOT_TARGET | STATE_SENT_CMD
			    | STATE_XFERRED_DATA | STATE_GOT_STATUS;

			con_log(CL_ANN1, (CE_CONT,
			    " CDB[0] = %x completed for %s:"
			    " size %lx context %x cmd_status %x",
			    pkt->pkt_cdbp[0],
			    ((acmd->islogical) ? "LD" : "PD"),
			    acmd->cmd_dmacount, cmd->SMID, status));

			if (pkt->pkt_cdbp[0] == SCMD_INQUIRY) {
				struct scsi_inquiry	*inq;

				if (acmd->cmd_dmacount != 0) {
					bp_mapin(acmd->cmd_buf);
					inq = (struct scsi_inquiry *)
					    acmd->cmd_buf->b_un.b_addr;

					/* don't expose physical drives to OS */
					if (acmd->islogical &&
					    (status == MFI_STAT_OK)) {
						display_scsi_inquiry(
						    (caddr_t)inq);
					} else {
						/* for physical disk */
						status =
						    MFI_STAT_DEVICE_NOT_FOUND;
					}
				}
			}

			switch (status) {
			case MFI_STAT_OK:
				pkt->pkt_scbp[0] = STATUS_GOOD;
				break;
			case MFI_STAT_LD_CC_IN_PROGRESS:
			case MFI_STAT_LD_RECON_IN_PROGRESS:
				pkt->pkt_scbp[0] = STATUS_GOOD;
				break;
			case MFI_STAT_LD_INIT_IN_PROGRESS:
				pkt->pkt_reason	= CMD_TRAN_ERR;

				break;
			case MFI_STAT_SCSI_DONE_WITH_ERROR:

				pkt->pkt_reason	= CMD_CMPLT;
				((struct scsi_status *)
				    pkt->pkt_scbp)->sts_chk = 1;

				if (pkt->pkt_cdbp[0] != SCMD_TEST_UNIT_READY) {
					pkt->pkt_state |= STATE_ARQ_DONE;
					arqstat = (void *)(pkt->pkt_scbp);
					arqstat->sts_rqpkt_reason = CMD_CMPLT;
					arqstat->sts_rqpkt_resid = 0;
					arqstat->sts_rqpkt_state |=
					    STATE_GOT_BUS | STATE_GOT_TARGET
					    | STATE_SENT_CMD
					    | STATE_XFERRED_DATA;
					*(uint8_t *)&arqstat->sts_rqpkt_status =
					    STATUS_GOOD;
					con_log(CL_ANN1, (CE_NOTE,
					    "Copying Sense data %x",
					    cmd->SMID));

					ddi_rep_get8(
					    instance->
					    mpi2_frame_pool_dma_obj.acc_handle,
					    (uint8_t *)
					    &(arqstat->sts_sensedata),
					    cmd->sense1,
					    sizeof (struct scsi_extended_sense),
					    DDI_DEV_AUTOINCR);
				}
				break;
			case MFI_STAT_LD_OFFLINE:
			case MFI_STAT_DEVICE_NOT_FOUND:
				pkt->pkt_reason	= CMD_DEV_GONE;
				pkt->pkt_statistics  = STAT_DISCON;
				break;
			case MFI_STAT_LD_LBA_OUT_OF_RANGE:
				pkt->pkt_state |= STATE_ARQ_DONE;
				pkt->pkt_reason	= CMD_CMPLT;
				((struct scsi_status *)
				    pkt->pkt_scbp)->sts_chk = 1;

				arqstat = (void *)(pkt->pkt_scbp);
				arqstat->sts_rqpkt_reason = CMD_CMPLT;
				arqstat->sts_rqpkt_resid = 0;
				arqstat->sts_rqpkt_state |= STATE_GOT_BUS
				    | STATE_GOT_TARGET | STATE_SENT_CMD
				    | STATE_XFERRED_DATA;
				*(uint8_t *)&arqstat->sts_rqpkt_status =
				    STATUS_GOOD;

				arqstat->sts_sensedata.es_valid = 1;
				arqstat->sts_sensedata.es_key =
				    KEY_ILLEGAL_REQUEST;
				arqstat->sts_sensedata.es_class =
				    CLASS_EXTENDED_SENSE;

				/*
				 * LOGICAL BLOCK ADDRESS OUT OF RANGE:
				 * ASC: 0x21h; ASCQ: 0x00h;
				 */
				arqstat->sts_sensedata.es_add_code = 0x21;
				arqstat->sts_sensedata.es_qual_code = 0x00;
				break;

			case MFI_STAT_INVALID_CMD:
			case MFI_STAT_INVALID_DCMD:
			case MFI_STAT_INVALID_PARAMETER:
			case MFI_STAT_INVALID_SEQUENCE_NUMBER:
			case MFI_STAT_SCSI_IO_FAILED:
				/* To do list */
			break;
			}

			atomic_add_16(&instance->fw_outstanding, (-1));

			(void) mrsas_common_check(instance, cmd);

			if (acmd->cmd_dmahandle) {
			if (mrsas_check_dma_handle(
			    acmd->cmd_dmahandle) != DDI_SUCCESS) {
				ddi_fm_service_impact(instance->dip,
				    DDI_SERVICE_UNAFFECTED);
				pkt->pkt_reason = CMD_TRAN_ERR;
				pkt->pkt_statistics = 0;
			}
			}

			/* Call the callback routine */
			if (((pkt->pkt_flags & FLAG_NOINTR) == 0) &&
			    pkt->pkt_comp) {
				(*pkt->pkt_comp)(pkt);
			}

			con_log(CL_ANN1, (CE_NOTE, "Free smid %x", cmd->SMID));

			ddi_put8(instance->mpi2_frame_pool_dma_obj.acc_handle,
			    &scsi_raid_io->RaidContext.status, 0);

			ddi_put8(instance->mpi2_frame_pool_dma_obj.acc_handle,
			    &scsi_raid_io->RaidContext.extStatus, 0);

			return_raid_msg_pkt(instance, cmd);
			break;
		}
	case MPI2_FUNCTION_PASSTHRU_IO_REQUEST:  /* MFA command */

		if (cmd->frame->dcmd.opcode
		    == MR_DCMD_LD_MAP_GET_INFO &&
		    cmd->frame->dcmd.mbox.b[1]
		    == 1) {
			con_log(CL_ANN1, (CE_CONT,
			    "LDMAP sync command  SMID RECEIVED 0x%X",
			    cmd->SMID));
			if (cmd->frame->hdr.cmd_status != 0) {
				con_log(CL_ANN1, (CE_NOTE,
				    "map sync completed\n"));
			} else {
				instance->map_id++;
				con_log(CL_ANN1, (CE_NOTE,
				    "map sync completed map_id = %ld \n",
				    (unsigned long)instance->map_id));
			}
			con_log(CL_ANN1, (CE_NOTE,
			    "sync command completed[SMID]%x", cmd->SMID));

			if (MR_ValidateMapInfo(
			    instance->ld_map[(instance->map_id & 1)],
			    instance->load_balance_info)) {
					instance->fp_change = 0;
			} else {
				instance->fp_change = 1;
			}

			if (instance->map_update_cmd == cmd) {
				return_raid_msg_pkt(instance, cmd);
				atomic_add_16(&instance->fw_outstanding, (-1));
				(void) mrsas_tbolt_sync_map_info(instance);
			}
		break;
		}

		if (cmd->frame->dcmd.opcode == MR_DCMD_CTRL_EVENT_WAIT) {
			con_log(CL_ANN1, (CE_CONT,
			    "AEN command SMID RECEIVED 0x%X",
			    cmd->SMID));
			if ((instance->aen_cmd == cmd) &&
			    (instance->aen_cmd->abort_aen)) {
				con_log(CL_ANN, (CE_WARN,
				    "mrsas_softintr: "
				    "aborted_aen returned"));
			} else {
				atomic_add_16(&instance->fw_outstanding, (-1));
				service_mfi_aen(instance, cmd);
			}
		}

		if (cmd->sync_cmd == MRSAS_TRUE) {
			con_log(CL_ANN1, (CE_CONT,
			    "Sync Command Response SMID RECEIVED 0x%X",
			    cmd->SMID));

			con_log(CL_ANN1, (CE_CONT,
			    "completed for cmd context %x",
			    cmd->SMID));
			tbolt_complete_cmd_in_sync_mode(instance, cmd);
		}
		else
		{
			con_log(CL_ANN1, (CE_CONT,
			    "Wrong SMID RECEIVED 0x%X",
			    cmd->SMID));
		}
		break;
	default:
		/* free message */
		con_log(CL_ANN1, (CE_NOTE, "Unknown Type!!!!!!!!"));
		break;
	}
}
uint_t
mr_sas_tbolt_process_outstanding_cmd(struct mrsas_instance *instance)
{
	uint8_t				replyType;
	Mpi2SCSIIOSuccessReplyDescriptor_t *replyDesc;
	Mpi2ReplyDescriptorsUnion_t 	*desc;
	uint16_t			smid;
	MRU64				d_val;
	struct mrsas_cmd		*cmd;
	struct mrsas_header	*hdr;
	struct scsi_pkt		*pkt;

	(void) ddi_dma_sync(instance->reply_desc_dma_obj.dma_handle,
	    0, 0, DDI_DMA_SYNC_FORDEV);

	(void) ddi_dma_sync(instance->reply_desc_dma_obj.dma_handle,
	    0, 0, DDI_DMA_SYNC_FORCPU);

	desc = instance->reply_frame_pool;
	desc += instance->reply_read_index;

	replyDesc = (MPI2_SCSI_IO_SUCCESS_REPLY_DESCRIPTOR *)desc;
	replyType = replyDesc->ReplyFlags &
	    MPI2_RPY_DESCRIPT_FLAGS_TYPE_MASK;

	if (replyType == MPI2_RPY_DESCRIPT_FLAGS_UNUSED)
		return (DDI_INTR_UNCLAIMED);

	con_log(CL_DLEVEL3, (CE_NOTE, "Reply Desc  = %p  Words.High = %x \n",
	    (void *)(unsigned long)desc, (unsigned int)desc->Words.High));
	con_log(CL_DLEVEL3, (CE_NOTE, "Reply Desc  = %p  Words.Low = %x \n",
	    (void *)(unsigned long)desc, (unsigned int)desc->Words.Low));

	d_val.Low = ddi_get32(instance->reply_desc_dma_obj.acc_handle,
	    &desc->Words.Low);
	d_val.High = ddi_get32(instance->reply_desc_dma_obj.acc_handle,
	    &desc->Words.High);

	/* Read Reply descriptor */
	while ((d_val.Low != 0xffffffff) &&
	    (d_val.High != 0xffffffff)) {

		(void) ddi_dma_sync(instance->reply_desc_dma_obj.dma_handle,
		    0, 0, DDI_DMA_SYNC_FORCPU);

		smid = ddi_get16(instance->reply_desc_dma_obj.acc_handle,
		    &replyDesc->SMID);

		if (!smid || smid > instance->max_fw_cmds + 1) {
			con_log(CL_DLEVEL3, (CE_NOTE,
			    "Reply Desc at Break  = %p  Words.Low = %x \n",
			    (void *)desc, (unsigned int)desc->Words.Low));
			con_log(CL_DLEVEL3, (CE_NOTE,
			    "Reply Desc at Break  = %p  Words.High = %x \n",
			    (void *)desc, (unsigned int)desc->Words.High));
			break;
		}

		cmd = instance->cmd_list[smid - 1];
		if (!cmd) {
			con_log(CL_ANN1, (CE_NOTE,
			    "Invalid command or Poll commad"
			    "Received in completion path\n"));
		} else {
			mutex_enter(&instance->cmd_pend_mtx);
			if (cmd->sync_cmd == MRSAS_TRUE) {
				hdr = (struct mrsas_header *)&cmd->frame->hdr;
				if (hdr) {
					mlist_del_init(&cmd->list);
				}
				} else {
				pkt = cmd->pkt;
				if (pkt) {
					mlist_del_init(&cmd->list);
				}
			}

			mutex_exit(&instance->cmd_pend_mtx);

			tbolt_complete_cmd(instance, cmd);
		}
		/* set it back to all 0xfff */
		ddi_put32(instance->reply_desc_dma_obj.acc_handle,
		    &desc->Words.Low,
		    0xFFFFFFFF);
		ddi_put32(instance->reply_desc_dma_obj.acc_handle,
		    &desc->Words.High,
		    0xFFFFFFFF);

		instance->reply_read_index++;

		if (instance->reply_read_index >= (instance->reply_q_depth)) {
			con_log(CL_DLEVEL3, (CE_NOTE, "wrap around"));
			instance->reply_read_index = 0;
		}

		/* Get the next reply descriptor */
		if (!instance->reply_read_index)
			desc = instance->reply_frame_pool;
		else
			desc++;

		replyDesc = (MPI2_SCSI_IO_SUCCESS_REPLY_DESCRIPTOR *)desc;

		d_val.Low = ddi_get32(instance->reply_desc_dma_obj.acc_handle,
		    &desc->Words.Low);
		d_val.High = ddi_get32(instance->reply_desc_dma_obj.acc_handle,
		    &desc->Words.High);

		con_log(CL_DLEVEL3, (CE_NOTE,
		    "Next Reply Desc  = %p Words.Low = %x\n",
		    (void *)desc, (unsigned int)desc->Words.Low));

		con_log(CL_DLEVEL3, (CE_NOTE,
		    "Next Reply Desc  = %p Words.High = %x\n",
		    (void *)desc, (unsigned int)desc->Words.High));

		replyType = replyDesc->ReplyFlags &
		    MPI2_RPY_DESCRIPT_FLAGS_TYPE_MASK;

		if (replyType == MPI2_RPY_DESCRIPT_FLAGS_UNUSED)
			break;

	} /* End of while loop. */

	/* update replyIndex to FW */
	WR_MPI2_REPLY_POST_INDEX(instance->reply_read_index, instance);
	if (mrsas_check_acc_handle(instance->regmap_handle) != DDI_SUCCESS)
		ddi_fm_service_impact(instance->dip, DDI_SERVICE_UNAFFECTED);

	(void) ddi_dma_sync(instance->reply_desc_dma_obj.dma_handle,
	    0, 0, DDI_DMA_SYNC_FORDEV);

	(void) ddi_dma_sync(instance->reply_desc_dma_obj.dma_handle,
	    0, 0, DDI_DMA_SYNC_FORCPU);

	if (mrsas_check_dma_handle(instance->reply_desc_dma_obj.dma_handle) !=
	    DDI_SUCCESS)
		ddi_fm_service_impact(instance->dip, DDI_SERVICE_UNAFFECTED);
	return (DDI_INTR_CLAIMED);
}




/*
 * complete_cmd_in_sync_mode -	Completes an internal command
 * @instance:			Adapter soft state
 * @cmd:			Command to be completed
 *
 * The issue_cmd_in_sync_mode() function waits for a command to complete
 * after it issues a command. This function wakes up that waiting routine by
 * calling wake_up() on the wait queue.
 */
void
tbolt_complete_cmd_in_sync_mode(struct mrsas_instance *instance,
    struct mrsas_cmd *cmd)
{

	cmd->cmd_status = ddi_get8(cmd->frame_dma_obj.acc_handle,
	    &cmd->frame->io.cmd_status);

	cmd->sync_cmd = MRSAS_FALSE;

	if (cmd->cmd_status == ENODATA) {
		cmd->cmd_status = 0;
	}

	cv_broadcast(&instance->int_cmd_cv);
}

/*
 * mrsas_tbolt_get_ld_map_info -	Returns  ld_map structure
 * instance:				Adapter soft state
 *
 * Issues an internal command (DCMD) to get the FW's controller PD
 * list structure.  This information is mainly used to find out SYSTEM
 * supported by the FW.
 */
int
mrsas_tbolt_get_ld_map_info(struct mrsas_instance *instance)
{
	int ret = 0;
	struct mrsas_cmd	*cmd = NULL;
	struct mrsas_dcmd_frame	*dcmd;
	MR_FW_RAID_MAP_ALL *ci;
	uint32_t ci_h = 0;
	U32 size_map_info;

	cmd = get_raid_msg_pkt(instance);

	if (!cmd) {
		con_log(CL_ANN, (CE_WARN,
		    "Failed to get a cmd for get_ld_map_info"));
		return (DDI_FAILURE);
	}

	dcmd = &cmd->frame->dcmd;

	size_map_info =	sizeof (MR_FW_RAID_MAP) +
	    (sizeof (MR_LD_SPAN_MAP) *
	    (MAX_LOGICAL_DRIVES - 1));


	con_log(CL_ANN, (CE_NOTE,
	    "size_map_info : %x", size_map_info));

	ci = instance->ld_map[(instance->map_id & 1)];
	ci_h = instance->ld_map_phy[(instance->map_id & 1)];

	if (!ci) {
		con_log(CL_ANN, (CE_WARN,
		    "Failed to alloc mem for ld_map_info"));
		return_raid_msg_pkt(instance, cmd);
		return (-1);
	}

	(void) memset(ci, 0, sizeof (*ci));
	(void) memset(dcmd->mbox.b, 0, DCMD_MBOX_SZ);

	dcmd->cmd = MFI_CMD_OP_DCMD;
	dcmd->cmd_status = 0xFF;
	dcmd->sge_count = 1;
	dcmd->flags = MFI_FRAME_DIR_READ;
	dcmd->timeout = 0;
	dcmd->pad_0 = 0;
	dcmd->data_xfer_len = size_map_info;
	dcmd->opcode = MR_DCMD_LD_MAP_GET_INFO;
	dcmd->sgl.sge32[0].phys_addr = ci_h;
	dcmd->sgl.sge32[0].length = size_map_info;

	(void) mr_sas_tbolt_build_mfi_cmd(instance, cmd);

	if (!instance->func_ptr->issue_cmd_in_poll_mode(instance, cmd)) {
		ret = 0;
		con_log(CL_ANN, (CE_NOTE,
		    "Get LD Map Info success\n"));
	} else {
		con_log(CL_ANN, (CE_WARN,
		    "Get LD Map Info failed\n"));
		ret = -1;
	}

	return_raid_msg_pkt(instance, cmd);

	return (ret);
}

void
mrsas_dump_reply_desc(struct mrsas_instance *instance)
{
	uint32_t i;
	MPI2_REPLY_DESCRIPTORS_UNION *reply_desc;
	MRU64 d_val;

	reply_desc = instance->reply_frame_pool;

	for (i = 0; i < instance->reply_q_depth; i++, reply_desc++) {
		d_val.Low = ddi_get32(instance->reply_desc_dma_obj.acc_handle,
		    &reply_desc->Words.Low);
		d_val.High = ddi_get32(instance->reply_desc_dma_obj.acc_handle,
		    &reply_desc->Words.High);
		con_log(CL_DLEVEL3, (CE_NOTE,
		    "i=%d, %x:%x",
		    i, d_val.High, d_val.Low));
	}
}

/*
 * mrsas_tbolt_command_create -	Create command for fast path.
 * @io_info:	MegaRAID IO request packet pointer.
 * @ref_tag:	Reference tag for RD/WRPROTECT
 *
 * Create the command for fast path.
 */
void
mrsas_tbolt_prepare_cdb(U8 cdb[], struct IO_REQUEST_INFO *io_info,
    Mpi2RaidSCSIIORequest_t *scsi_io_request, U32 ref_tag)
{

	/* Prepare 32-byte CDB if DIF is supported on this device */

	if (io_info->ldPI) {

			cdb[0] =  MRSAS_SCSI_VARIABLE_LENGTH_CMD;
			cdb[7] =  MRSAS_SCSI_ADDL_CDB_LEN;

			if (io_info->isRead) {
				cdb[9] = MRSAS_SCSI_SERVICE_ACTION_READ32;
			} else {
				cdb[9] = MRSAS_SCSI_SERVICE_ACTION_WRITE32;
			}

			/* RDPROTECT/WRPROTECT */
			cdb[10] = MRSAS_RD_WR_PROTECT;

			/* LOGICAL BLOCK ADDRESS */
			cdb[12] = (U8)((io_info->pdBlock >> 56) & 0xff);

			cdb[13] = (U8)((io_info->pdBlock >> 48) & 0xff);
			cdb[14] = (U8)((io_info->pdBlock >> 40) & 0xff);
			cdb[15] = (U8)((io_info->pdBlock >> 32) & 0xff);
			cdb[16] = (U8)((io_info->pdBlock >> 24) & 0xff);
			cdb[17] = (U8)((io_info->pdBlock >> 16) & 0xff);
			cdb[18] = (U8)((io_info->pdBlock >> 8) & 0xff);
			cdb[19] = (U8)(io_info->pdBlock & 0xff);

			/* EXPECTED INITIAL LOGICAL BLOCK REFERENCE TAG */
			scsi_io_request->CDB.EEDP32.PrimaryReferenceTag =
			    ref_tag; /* convert to ddi call */

			cdb[28] = (U8)((scsi_io_request->DataLength >>
			    24) & 0xff);	/* TRANSFER LENGTH */
			cdb[29] = (U8)((scsi_io_request->DataLength >>
			    16) & 0xff);
			cdb[30] = (U8)((scsi_io_request->DataLength >>
			    8) & 0xff);
			cdb[31] = (U8)(scsi_io_request->DataLength & 0xff);

			/* set SCSI IO EEDPFlags bits */
			if (io_info->isRead) {
				/*
				 * For READ commands, the EEDPFlags
				 * shall be set to specify to
				 * Increment the Primary Reference Tag,
				 * to Check the Reference
				 * Tag, and to Check and Remove the
				 * Protection Information
				 * fields.
				 */
		scsi_io_request->EEDPFlags =
		    MPI2_SCSIIO_EEDPFLAGS_INC_PRI_REFTAG |
		    MPI2_SCSIIO_EEDPFLAGS_CHECK_REFTAG |
		    MPI2_SCSIIO_EEDPFLAGS_CHECK_REMOVE_OP;
			} else {
				/*
				 * For WRITE commands, the EEDPFlags
				 * shall be set to specify to
				 * Increment the Primary Reference Tag,
				 * and to insert Protection Information fields.
				 */
			scsi_io_request->EEDPFlags =
			    MPI2_SCSIIO_EEDPFLAGS_INC_PRI_REFTAG |
			    MPI2_SCSIIO_EEDPFLAGS_INSERT_OP;
			}
			scsi_io_request->EEDPBlockSize	= MRSAS_EEDPBLOCKSIZE;
	} else {
		/*
		 * Prepare a 10-byte CDB for LBA less than 2TB.
		 * For PD > 2TB in size, prepare 16-byte CDB
		 */
		if (io_info->pdBlock + scsi_io_request->DataLength <=
		    0xFFFFFFFFULL) {

			if (io_info->isRead) {
				cdb[0] = READ_10;
			} else {
				cdb[0] = WRITE_10;
			}

			cdb[2] = (U8)((io_info->pdBlock >> 24) & 0xff);
			cdb[3] = (U8)((io_info->pdBlock >> 16) & 0xff);
			cdb[4] = (U8)((io_info->pdBlock >> 8) & 0xff);
			cdb[5] = (U8)(io_info->pdBlock & 0xff);

			cdb[7] = (U8)((scsi_io_request->DataLength >>
			    8) & 0xff);
			cdb[8] = (U8)(scsi_io_request->DataLength & 0xff);
		} else {
			if (io_info->isRead) {
				cdb[0] = READ_16;
			} else {
				cdb[0] = WRITE_16;
			}

			cdb[2] = (U8)((io_info->pdBlock >> 56) & 0xff);
			cdb[3] = (U8)((io_info->pdBlock >> 48) & 0xff);
			cdb[4] = (U8)((io_info->pdBlock >> 40) & 0xff);
			cdb[5] = (U8)((io_info->pdBlock >> 32) & 0xff);
			cdb[6] = (U8)((io_info->pdBlock >> 24) & 0xff);
			cdb[7] = (U8)((io_info->pdBlock >> 16) & 0xff);
			cdb[8] = (U8)((io_info->pdBlock >> 8) & 0xff);
			cdb[9] = (U8)(io_info->pdBlock & 0xff);

			cdb[10] = (U8)((scsi_io_request->DataLength >>
			    24) & 0xff);
			cdb[11] = (U8)((scsi_io_request->DataLength >>
			    16) & 0xff);
			cdb[12] = (U8)((scsi_io_request->DataLength >>
			    8) & 0xff);
			cdb[13] = (U8)(scsi_io_request->DataLength & 0xff);
		}
	}
}


/*
 * mrsas_tbolt_set_pd_lba -	Sets PD LBA
 * @cdb:		CDB
 * @cdb_len:		cdb length
 * @start_blk:		Start block of IO
 *
 * Used to set the PD LBA in CDB for FP IOs
 */
void
mrsas_tbolt_set_pd_lba(U8 cdb[], U8 cdb_len, U64 start_blk, U32 num_blocks)
{
	U8 flagvals, opcode, groupnum, control;
	/* Some drives don't support 16/12/6 byte CDB's, convert to 10 */
	if (((cdb_len == 6) || (cdb_len == 12) ||
	    (cdb_len == 16)) && (start_blk <= 0xffffffff)) {
		if (cdb_len == 6) {
			con_log(CL_ANN1, (CE_NOTE,
			    "Converting READ6/WRITE6 to READ10\n"));
			opcode = cdb[0] == 0x08 ? 0x28 : 0x2A;
			flagvals = 0;
			groupnum = cdb[14];
			control = cdb[15];
		} else if (cdb_len == 12) {
			con_log(CL_ANN1, (CE_NOTE,
			    "Converting READ/WRITE(12) to READ10\n"));
			opcode = cdb[0] == 0xA8 ? 0x28 : 0x2A;
			flagvals = cdb[1];
			groupnum = cdb[10];
			control = cdb[11];
		} else {
			con_log(CL_ANN1, (CE_NOTE,
			    "Converting READ/WRITE(16) to READ10\n"));
			opcode = cdb[0] == 0x88 ? 0x28 : 0x2A;
			flagvals = cdb[1];
			groupnum = cdb[10];
			control = cdb[11];
		}
		(void) memset(cdb, 0, sizeof (cdb));
		cdb[0] = opcode;
		cdb[1] = flagvals;
		cdb[6] = groupnum;
		cdb[9] = control;
		cdb[8] = (U8)(num_blocks & 0xff);
		cdb[7] = (U8)((num_blocks >> 8) & 0xff);
		cdb_len = 10;
	}
	switch (cdb_len) {
		case 6:
		{
			U8 val = cdb[1] & 0xE0;
			cdb[3] = (U8)(start_blk & 0xff);
			cdb[2] = (U8)((start_blk >> 8) & 0xff);
			cdb[1] = val | ((U8)(start_blk >> 16) & 0x1f);
			break;
		}
		case 10:
			cdb[5] = (U8)(start_blk & 0xff);
			cdb[4] = (U8)((start_blk >> 8) & 0xff);
			cdb[3] = (U8)((start_blk >> 16) & 0xff);
			cdb[2] = (U8)((start_blk >> 24) & 0xff);
			break;
		case 12:
			cdb[5]    = (U8)(start_blk & 0xff);
			cdb[4]    = (U8)((start_blk >> 8) & 0xff);
			cdb[3]    = (U8)((start_blk >> 16) & 0xff);
			cdb[2]    = (U8)((start_blk >> 24) & 0xff);
			break;

		case 16:
			cdb[9]	= (U8)(start_blk & 0xff);
			cdb[8]	= (U8)((start_blk >> 8) & 0xff);
			cdb[7]	= (U8)((start_blk >> 16) & 0xff);
			cdb[6]	= (U8)((start_blk >> 24) & 0xff);
			cdb[5]	= (U8)((start_blk >> 32) & 0xff);
			cdb[4]	= (U8)((start_blk >> 40) & 0xff);
			cdb[3]	= (U8)((start_blk >> 48) & 0xff);
			cdb[2]	= (U8)((start_blk >> 56) & 0xff);
			break;
	}
}

int
mrsas_tbolt_check_map_info(struct mrsas_instance *instance)
{
	MR_FW_RAID_MAP_ALL *ld_map;

	if (!mrsas_tbolt_get_ld_map_info(instance)) {

		ld_map = instance->ld_map[(instance->map_id & 1)];

		con_log(CL_ANN1, (CE_NOTE, "ldCount=%d, map size=%d\n",
		    ld_map->raidMap.ldCount, ld_map->raidMap.totalSize));

		if (MR_ValidateMapInfo(instance->ld_map[(instance->map_id & 1)],
		    instance->load_balance_info)) {
			con_log(CL_ANN, (CE_NOTE, "map info success"));

				instance->fp_change = 0;

			return (DDI_SUCCESS);
		}

	}
		instance->fp_change = 1;
		con_log(CL_ANN, (CE_WARN, "map info failed\n"));
	return (DDI_FAILURE);
}
/*
 * Marks HBA as bad. This will be called either when an
 * IO packet times out even after 3 FW resets
 * or FW is found to be fault even after 3 continuous resets.
 */

int
mrsas_tbolt_kill_adapter(struct mrsas_instance *instance)
{
		if (instance->deadadapter == 1)
			return (DDI_FAILURE);

		con_log(CL_ANN1, (CE_NOTE, "tbolt_kill_adapter: "
		    "Writing to doorbell with MFI_STOP_ADP "));
		mutex_enter(&instance->ocr_flags_mtx);
		instance->deadadapter = 1;
		mutex_exit(&instance->ocr_flags_mtx);
		instance->func_ptr->disable_intr(instance);
		WR_RESERVED0_REGISTER(MFI_STOP_ADP, instance);
		/* Flush */
		(void) RD_RESERVED0_REGISTER(instance);
		(void) mrsas_complete_pending_cmds(instance);
		con_log(CL_ANN1, (CE_WARN, "TBOLT Kill adapter called\n"));

		return (DDI_SUCCESS);
}
int
mrsas_tbolt_reset_ppc(struct mrsas_instance *instance)
{
	uint32_t status = 0x00;
	uint32_t retry = 0;
	uint32_t cur_abs_reg_val;
	uint32_t fw_state;
	int	abs_state;

	con_log(CL_ANN1, (CE_NOTE,
	    "mrsas_tbolt_reset_ppc entered\n "));

	if (instance->deadadapter == 1) {
		con_log(CL_ANN1, (CE_NOTE, "mrsas_tbolt_reset_ppc: "
		    "no more resets as HBA has been marked dead "));
		return (DDI_FAILURE);
	}
	instance->reply_read_index = 0;
	mutex_enter(&instance->ocr_flags_mtx);
	instance->adapterresetinprogress = 1;
	mutex_exit(&instance->ocr_flags_mtx);
	con_log(CL_ANN1, (CE_NOTE, "mrsas_tbolt_reset_ppc:"
	    "adpterresetinprogress "
	    "flag set, time %llx", gethrtime()));
	instance->func_ptr->disable_intr(instance);
retry_reset:
	con_log(CL_ANN1, (CE_NOTE, "mrsas_tbolt_reset_ppc: "
	    ":Resetting PPC "));

	WR_TBOLT_IB_WRITE_SEQ(0xF, instance);
	WR_TBOLT_IB_WRITE_SEQ(4, instance);
	WR_TBOLT_IB_WRITE_SEQ(0xb, instance);
	WR_TBOLT_IB_WRITE_SEQ(2, instance);
	WR_TBOLT_IB_WRITE_SEQ(7, instance);
	WR_TBOLT_IB_WRITE_SEQ(0xd, instance);
	con_log(CL_ANN1, (CE_NOTE,
	    "mrsas_tbolt_reset_ppc: magic number written "
	    "to write sequence register\n"));
	delay(100 * drv_usectohz(MILLISEC));
	status = RD_TBOLT_HOST_DIAG(instance);
	con_log(CL_ANN1, (CE_NOTE,
	    "mrsas_tbolt_reset_ppc: READ HOSTDIAG SUCCESS "
	    "to write sequence register\n"));

	while (status & DIAG_TBOLT_RESET_ADAPTER) {
		delay(100 * drv_usectohz(MILLISEC));
		status = RD_TBOLT_HOST_DIAG(instance);
		if (retry++ == 100) {
			con_log(CL_ANN1, (CE_NOTE,
			    "mrsas_tbolt_reset_ppc:"
			    "resetadapter bit is set already "
			    "check retry count %d\n", retry));
			return (DDI_FAILURE);
		}
	}

	WR_TBOLT_HOST_DIAG(status | DIAG_TBOLT_RESET_ADAPTER, instance);
	delay(100 * drv_usectohz(MILLISEC));

	ddi_rep_get8((instance)->regmap_handle, (uint8_t *)&status,
	    (uint8_t *)((uintptr_t)(instance)->regmap +
	    RESET_TBOLT_STATUS_OFF), 4, DDI_DEV_AUTOINCR);



	while ((status & DIAG_TBOLT_RESET_ADAPTER)) {
		delay(100 * drv_usectohz(MILLISEC));
		ddi_rep_get8((instance)->regmap_handle, (uint8_t *)&status,
		    (uint8_t *)((uintptr_t)(instance)->regmap +
		    RESET_TBOLT_STATUS_OFF), 4, DDI_DEV_AUTOINCR);
		if (retry++ == 100) {
			(void) mrsas_tbolt_kill_adapter(instance);
			return (DDI_FAILURE);
		}
	}

	con_log(CL_ANN1, (CE_NOTE, "mrsas_reset_ppc: Adapter reset complete"));
	con_log(CL_ANN1, (CE_NOTE, "mrsas_reset_ppc: "
	    "Calling mfi_state_transition_to_ready"));

	abs_state = instance->func_ptr->read_fw_status_reg(instance);
	retry = 0;
	while ((abs_state <= MFI_STATE_FW_INIT) && (retry++ < 1000)) {
		delay(100 * drv_usectohz(MILLISEC));
		abs_state = instance->func_ptr->read_fw_status_reg(instance);
	}
	if (abs_state <= MFI_STATE_FW_INIT) {
		con_log(CL_ANN1, (CE_NOTE,
		    "TBOLT:mr_sas firmware state < MFI_STATE_FW_INIT"
		    "state = 0x%x\n", abs_state));
		goto retry_reset;
	}
	/* Mark HBA as bad, if FW is fault after 3 continuous resets */
	if (mfi_state_transition_to_ready(instance) ||
	    debug_tbolt_fw_faults_after_ocr_g == 1) {
		cur_abs_reg_val =
		    instance->func_ptr->read_fw_status_reg(instance);
		fw_state	= cur_abs_reg_val & MFI_STATE_MASK;

		con_log(CL_ANN1, (CE_NOTE,
		    "mrsas_reset_ppc :before fake: FW is not ready "
		    "FW state = 0x%x", fw_state));
		if (debug_tbolt_fw_faults_after_ocr_g == 1)
			fw_state = MFI_STATE_FAULT;

		con_log(CL_ANN1, (CE_NOTE,  "mrsas_reset_ppc : FW is not ready "
		    "FW state = 0x%x", fw_state));

		if (fw_state == MFI_STATE_FAULT) {
			/* increment the count */
			instance->fw_fault_count_after_ocr++;
			if (instance->fw_fault_count_after_ocr
			    < MAX_FW_RESET_COUNT) {
				con_log(CL_ANN1, (CE_WARN, "mrsas_reset_ppc: "
				    "FW is in fault after OCR count %d ",
				    instance->fw_fault_count_after_ocr));
				goto retry_reset;

			} else {
				con_log(CL_ANN1, (CE_WARN, "mrsas_reset_ppc: "
				    "Max Reset Count exceeded "
				    "Mark HBA as bad"));
				(void) mrsas_tbolt_kill_adapter(instance);
				return (DDI_FAILURE);
			}
		}
	}
	/* reset the counter as FW is up after OCR */
	instance->fw_fault_count_after_ocr = 0;

	con_log(CL_ANN1, (CE_NOTE, "mrsas_reset_ppc: "
	    "Calling mrsas_issue_init_mpi2"));
	abs_state = mrsas_issue_init_mpi2(instance);

	if (abs_state == DDI_FAILURE) {
		return (DDI_FAILURE);
	}

	con_log(CL_ANN1, (CE_NOTE, "mrsas_reset_ppc: "
	    "mrsas_issue_init_mpi2 Done"));
	con_log(CL_ANN1, (CE_NOTE, "mrsas_reset_ppc: "
	    "Calling mrsas_print_pending_cmd\n"));
	(void) mrsas_print_pending_cmds(instance);
	con_log(CL_ANN1, (CE_NOTE, "mrsas_reset_ppc: "
	    "mrsas_print_pending_cmd done\n"));
	instance->func_ptr->enable_intr(instance);
	instance->fw_outstanding = 0;
	con_log(CL_ANN1, (CE_NOTE, "mrsas_reset_ppc: "
	    "Calling mrsas_issue_pending_cmds"));
	(void) mrsas_issue_pending_cmds(instance);
	con_log(CL_ANN1, (CE_NOTE, "mrsas_reset_ppc: "
	"Complete"));
	con_log(CL_ANN1, (CE_NOTE, "mrsas_reset_ppc: "
	    "Calling aen registration"));
	instance->func_ptr->issue_cmd(instance->aen_cmd, instance);
	con_log(CL_ANN1, (CE_NOTE, "Unsetting adpresetinprogress flag.\n"));
	mutex_enter(&instance->ocr_flags_mtx);
	instance->adapterresetinprogress = 0;
	mutex_exit(&instance->ocr_flags_mtx);
	con_log(CL_ANN1, (CE_NOTE, "mrsas_reset_ppc: "
	    "adpterresetinprogress flag unset"));
	con_log(CL_ANN1, (CE_NOTE, "mrsas_reset_ppc done\n"));
	return (DDI_SUCCESS);

}


/*
 * mrsas_sync_map_info -	Returns FW's ld_map structure
 * @instance:				Adapter soft state
 *
 * Issues an internal command (DCMD) to get the FW's controller PD
 * list structure.  This information is mainly used to find out SYSTEM
 * supported by the FW.
 */

int
mrsas_tbolt_sync_map_info(struct mrsas_instance *instance)
{
	int 			ret = 0, i;
	struct mrsas_cmd	*cmd = NULL;
	struct mrsas_dcmd_frame	*dcmd;
	uint32_t size_sync_info, num_lds;
	LD_TARGET_SYNC *ci = NULL;
	MR_FW_RAID_MAP_ALL *map;
	MR_LD_RAID  *raid;
	LD_TARGET_SYNC *ld_sync;
	uint32_t ci_h = 0;
	uint32_t size_map_info;

	cmd = get_raid_msg_pkt(instance);

	if (!cmd) {
		con_log(CL_ANN, (CE_WARN,
		    "Failed to get a cmd in mrsas_tbolt_sync_map_info "));
		return (DDI_FAILURE);
	}

	map = instance->ld_map[instance->map_id & 1];

	num_lds = map->raidMap.ldCount;

	dcmd = &cmd->frame->dcmd;

	size_sync_info = sizeof (LD_TARGET_SYNC) * num_lds;

	con_log(CL_ANN, (CE_WARN,
	    "size_sync_info =%X ; ld count = %X \n ",
	    size_sync_info, num_lds));

	ci = (LD_TARGET_SYNC *)instance->ld_map[(instance->map_id - 1) & 1];

	(void) memset(ci, (char)(instance->map_id),
	    sizeof (MR_FW_RAID_MAP_ALL));

	ci_h = instance->ld_map_phy[(instance->map_id - 1) & 1];

	(void) memset(dcmd->mbox.b, 0, DCMD_MBOX_SZ);

	ld_sync = (LD_TARGET_SYNC *)ci;

	for (i = 0; i < num_lds; i++, ld_sync++) {
		raid = MR_LdRaidGet(i, map);

		con_log(CL_ANN1, (CE_NOTE,
		    "i : %x, Seq Num : %x, Sync Reqd : %x\n",
		    i, raid->seqNum, raid->flags.ldSyncRequired));

		ld_sync->ldTargetId = MR_GetLDTgtId(i, map);

		con_log(CL_ANN1, (CE_NOTE, "i : %x, tgt : %x \n",
		    i, ld_sync->ldTargetId));

		ld_sync->seqNum = raid->seqNum;
	}


	size_map_info = sizeof (MR_FW_RAID_MAP) +
	    (sizeof (MR_LD_SPAN_MAP) * (MAX_LOGICAL_DRIVES - 1));

	dcmd->cmd = MFI_CMD_OP_DCMD;
	dcmd->cmd_status = 0xFF;
	dcmd->sge_count = 1;
	dcmd->flags = MFI_FRAME_DIR_WRITE;
	dcmd->timeout = 0;
	dcmd->pad_0 = 0;
	dcmd->data_xfer_len = size_map_info;
	dcmd->mbox.b[0] = (uint8_t)num_lds;
	dcmd->mbox.b[1] = 1; /* Pend */
	dcmd->opcode = MR_DCMD_LD_MAP_GET_INFO;
	dcmd->sgl.sge32[0].phys_addr = ci_h;
	dcmd->sgl.sge32[0].length = size_map_info;

	instance->map_update_cmd = cmd;
	(void) mr_sas_tbolt_build_mfi_cmd(instance, cmd);

	instance->func_ptr->issue_cmd(cmd, instance);

	return (ret);
}

/*
 * abort_sync_cmd
 */
int
abort_sync_cmd(struct mrsas_instance *instance,
    struct mrsas_cmd *cmd_to_abort)
{
	int	ret = 0;

	struct mrsas_cmd		*cmd;
	struct mrsas_abort_frame	*abort_fr;

	con_log(CL_ANN1, (CE_NOTE, "chkpnt: abort_ldsync:%d", __LINE__));

	cmd = instance->cmd_list[APP_RESERVE_CMDS-2];

	if (!cmd) {
		con_log(CL_ANN1, (CE_WARN,
		    "abort_sync_cmd():Failed to"
		    "get a cmd for abort_sync_cmd"));
		return (DDI_FAILURE);
	}
	/* Clear the frame buffer and assign back the context id */
	(void) memset((char *)&cmd->frame[0], 0, sizeof (union mrsas_frame));
	ddi_put32(cmd->frame_dma_obj.acc_handle, &cmd->frame->hdr.context,
	    cmd->index);

	abort_fr = &cmd->frame->abort;

	/* prepare and issue the abort frame */
	ddi_put8(cmd->frame_dma_obj.acc_handle,
	    &abort_fr->cmd, MFI_CMD_OP_ABORT);
	ddi_put8(cmd->frame_dma_obj.acc_handle, &abort_fr->cmd_status,
	    MFI_CMD_STATUS_SYNC_MODE);
	ddi_put16(cmd->frame_dma_obj.acc_handle, &abort_fr->flags, 0);
	ddi_put32(cmd->frame_dma_obj.acc_handle, &abort_fr->abort_context,
	    cmd_to_abort->index);
	ddi_put32(cmd->frame_dma_obj.acc_handle,
	    &abort_fr->abort_mfi_phys_addr_lo, cmd_to_abort->frame_phys_addr);
	ddi_put32(cmd->frame_dma_obj.acc_handle,
	    &abort_fr->abort_mfi_phys_addr_hi, 0);

	cmd->frame_count = 1;

	(void) mr_sas_tbolt_build_mfi_cmd(instance, cmd);

	if (instance->func_ptr->issue_cmd_in_poll_mode(instance, cmd)) {
		con_log(CL_ANN1, (CE_WARN,
		    "abort_ldsync_cmd: issue_cmd_in_poll_mode failed"));
		ret = -1;
	} else {
		ret = 0;
	}

	atomic_add_16(&instance->fw_outstanding, (-1));

	return (ret);
}
