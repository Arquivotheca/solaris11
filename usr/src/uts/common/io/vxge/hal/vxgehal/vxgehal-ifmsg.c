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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */
/*
 * Copyright Exar 2010. Copyright (c) 2002-2010 Neterion, Inc.
 * All right Reserved.
 *
 * FileName :   vxgehal-ifmsg.c
 *
 * Description:  Routines for communication between the privileged and
 *              non privileged drivers.
 *
 * Created:       06 October 2008
 */

#include "vxgehal.h"

/*
 * __vxge_hal_ifmsg_srpcim_to_vpath_wmsg_process - Process the
 * srpcim to vpath wmsg
 * @vpath: vpath
 * @wmsg: wsmsg
 *
 * Processes the wmsg and invokes appropriate action
 */
void
__vxge_hal_ifmsg_srpcim_to_vpath_wmsg_process(
			__vxge_hal_virtualpath_t *vpath,
			u64 wmsg)
{
	u32 opcode;
	__vxge_hal_device_t *hldev = vpath->hldev;

	vxge_assert(vpath);

	vxge_hal_trace_log_vpath(hldev, vpath->vp_id,
	    "==>  %s:%s:%d", __FILE__, __func__, __LINE__);

	vxge_hal_trace_log_vpath(hldev, vpath->vp_id,
	    "vpath = 0x"VXGE_OS_STXFMT", wmsg = 0x%8x", (ptr_t)vpath, wmsg);

	opcode = (u32)VXGE_HAL_IFMSG_GET_OPCODE(wmsg);

	switch (opcode) {
		default:
		case VXGE_HAL_IFMSG_MSV_OPCODE_UNKNOWN:
			break;
		case VXGE_HAL_IFMSG_MSV_OPCODE_RESET_BEGIN:
			__vxge_hal_device_handle_error(hldev,
			    vpath->vp_id,
			    VXGE_HAL_EVENT_RESET_START);
			break;
		case VXGE_HAL_IFMSG_MSV_OPCODE_RESET_END:
			vpath->hldev->manager_up = TRUE;
			__vxge_hal_device_handle_error(hldev,
			    vpath->vp_id,
			    VXGE_HAL_EVENT_RESET_COMPLETE);
			break;
		case VXGE_HAL_IFMSG_MSV_OPCODE_UP:
			vpath->hldev->manager_up = TRUE;
			break;
		case VXGE_HAL_IFMSG_MSV_OPCODE_DOWN:
			vpath->hldev->manager_up = FALSE;
			break;
		case VXGE_HAL_IFMSG_MSV_OPCODE_ACK:
			break;
	}

	vxge_hal_trace_log_vpath(hldev, vpath->vp_id,
	    "<==  %s:%s:%d Result = 0", __FILE__, __func__, __LINE__);
}

/*
 * __vxge_hal_ifmsg_srpcim_to_vpath_reset_end_poll - Polls for the
 *			     srpcim to vpath reset end
 * @hldev: HAL Device
 * @vp_id: Vpath id
 *
 * Polls for the srpcim to vpath reset end
 */
vxge_hal_status_e
__vxge_hal_ifmsg_srpcim_to_vpath_reset_end_poll(
			__vxge_hal_device_t *hldev,
			u32 vp_id)
{
	vxge_hal_status_e status;

	vxge_assert(hldev);

	vxge_hal_trace_log_mrpcim(hldev, NULL_VPID,
	    "==>  %s:%s:%d", __FILE__, __func__, __LINE__);

	vxge_hal_trace_log_mrpcim(hldev, NULL_VPID,
	    "hldev = 0x"VXGE_OS_STXFMT", vp_id = %d", (ptr_t)hldev, vp_id);

	status = __vxge_hal_device_register_poll(
	    hldev->header.pdev,
	    hldev->header.regh0,
	    &hldev->vpmgmt_reg[vp_id]->srpcim_to_vpath_wmsg, 0,
	    ~((u64)VXGE_HAL_IFMSG_MSV_OPCODE_RESET_END),
	    WAIT_FACTOR * hldev->header.config.device_poll_millis);

	vxge_hal_trace_log_mrpcim(hldev, NULL_VPID,
	    "<==  %s:%s:%d Result = 0", __FILE__, __func__, __LINE__);

	return (status);

}

/*
 * __vxge_hal_ifmsg_srpcim_to_vpath_req_post - Posts the srpcim to vpath req
 * @hldev: HAL Device
 * @vp_id: Vpath id, 0xffffffff for all vpaths
 * @wmsg: wsmsg
 *
 * Posts the req
 */
void
__vxge_hal_ifmsg_srpcim_to_vpath_req_post(
			__vxge_hal_device_t *hldev,
			u32 vp_id,
			u64 wmsg)
{
	vxge_assert(hldev);

	vxge_hal_trace_log_srpcim(hldev, NULL_VPID,
	    "==>  %s:%s:%d", __FILE__, __func__, __LINE__);

	vxge_hal_trace_log_srpcim(hldev, NULL_VPID,
	    "hldev = 0x"VXGE_OS_STXFMT", vp_id = %d, wmsg = 0x%8x",
	    (ptr_t)hldev, vp_id, wmsg);

	if (vp_id == 0xffffffff) {
		u32 i;
		for (i = 0; i < VXGE_HAL_MAX_VIRTUAL_PATHS; i++) {
			wmsg |= VXGE_HAL_IFMSG_VPATH_ID(i) |
			    VXGE_HAL_IFMSG_SEQ_NUM(
			    hldev->srpcim->vpath_state[i].ifmsg_up_seqno++);
			vxge_os_pio_mem_write64(hldev->header.pdev,
			    hldev->header.regh0,
			    wmsg,
			    &hldev->vpmgmt_reg[i]->srpcim_to_vpath_wmsg);
			vxge_os_pio_mem_write64(hldev->header.pdev,
			    hldev->header.regh0,
			    VXGE_HAL_SRPCIM_TO_VPATH_WMSG_TRIG_TRIG,
			    &hldev->vpmgmt_reg[i]->srpcim_to_vpath_wmsg_trig);
		}
	} else {
		wmsg |= VXGE_HAL_IFMSG_VPATH_ID(vp_id) |
		    VXGE_HAL_IFMSG_SEQ_NUM(
		    hldev->srpcim->vpath_state[vp_id].ifmsg_up_seqno++);
		vxge_os_pio_mem_write64(hldev->header.pdev,
		    hldev->header.regh0,
		    wmsg,
		    &hldev->vpmgmt_reg[vp_id]->srpcim_to_vpath_wmsg);
		vxge_os_pio_mem_write64(hldev->header.pdev,
		    hldev->header.regh0,
		    VXGE_HAL_SRPCIM_TO_VPATH_WMSG_TRIG_TRIG,
		    &hldev->vpmgmt_reg[vp_id]->srpcim_to_vpath_wmsg_trig);
	}

	vxge_hal_trace_log_srpcim(hldev, NULL_VPID,
	    "<==  %s:%s:%d Result = 0", __FILE__, __func__, __LINE__);
}

/*
 * __vxge_hal_ifmsg_vpath_to_srpcim_wmsg_process - Process the vpath
 * to srpcim wmsg
 * @hldev: hal device
 * @srpcim_id: SRPCIM Id
 * @vp_id: vpath id
 *
 * Processes the wmsg and invokes appropriate action
 */
void
__vxge_hal_ifmsg_vpath_to_srpcim_wmsg_process(
			__vxge_hal_device_t *hldev,
			u32 srpcim_id,
			u32 vp_id)
{
	u64 wmsg;
	u32 opcode;

	vxge_assert(hldev);

	vxge_hal_trace_log_srpcim(hldev, vp_id,
	    "==>  %s:%s:%d", __FILE__, __func__, __LINE__);

	vxge_hal_trace_log_srpcim(hldev, vp_id,
	    "hldev = 0x"VXGE_OS_STXFMT", vp_id = 0x%8x", (ptr_t)hldev, vp_id);

	vxge_os_pio_mem_write64(hldev->header.pdev,
	    hldev->header.regh0,
	    VXGE_HAL_VPATH_TO_SRPCIM_RMSG_SEL_SEL(vp_id),
	    &hldev->srpcim_reg[srpcim_id]->vpath_to_srpcim_rmsg_sel);

	vxge_os_wmb();

	wmsg = vxge_os_pio_mem_read64(hldev->header.pdev,
	    hldev->header.regh0,
	    &hldev->srpcim_reg[srpcim_id]->vpath_to_srpcim_rmsg);

	opcode = (u32)VXGE_HAL_IFMSG_GET_OPCODE(wmsg);

	switch (opcode) {
		default:
		case VXGE_HAL_IFMSG_VSM_OPCODE_UNKNOWN:
			break;
		case VXGE_HAL_IFMSG_VSM_OPCODE_REGISTER:
			break;
		case VXGE_HAL_IFMSG_VSM_OPCODE_RESET_BEGIN:
			break;
		case VXGE_HAL_IFMSG_VSM_OPCODE_ACK:
			break;
	}

	vxge_hal_trace_log_srpcim(hldev, vp_id,
	    "<==  %s:%s:%d Result = 0", __FILE__, __func__, __LINE__);
}
u32 __vxge_hal_ifmsg_is_manager_up(u64 wmsg) {
	return (((u32)VXGE_HAL_IFMSG_GET_OPCODE(wmsg) ==
	    VXGE_HAL_IFMSG_MSV_OPCODE_UP) ||
	    ((u32)VXGE_HAL_IFMSG_GET_OPCODE(wmsg) ==
	    VXGE_HAL_IFMSG_MSV_OPCODE_RESET_END));
}
