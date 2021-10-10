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
 * FileName :   vxgehal-doorbells.c
 *
 * Description:  HAL HW doorbell routines
 *
 * Created:       13 Nov 2006
 */
#include "vxgehal.h"

#ifdef	RNIC_LOOPBACK
void vxge_hal_sq_db_post(void);
void vxge_hal_dmq_db_post(void);
#endif

/*
 * __vxge_hal_offload_db_ptr_get - Returns the offload doorbell ptr for
 *			   the session
 *
 * @sqh: Send Queue handle
 * @session_id: Session Id
 *
 * This function returns the pointer to offload doorbell FIFO of the
 * given session.
 *
 */
/*ARGSUSED*/
u64
__vxge_hal_offload_db_ptr_get(vxge_hal_sq_h sqh,
			u32 sess_id)
{

	return (0);
}

/*
 * __vxge_hal_wqe_db_ptr_get - Returns the WQE doorbell ptr for
 *			   the session
 *
 * @srqh: SRQ
 * @session_id: Session Id
 *
 * This function returns the pointer to WQE doorbell FIFO of the
 * given session.
 *
 */
/*ARGSUSED*/
u64
__vxge_hal_wqe_db_ptr_get(vxge_hal_srq_h srqh,
			u32 sess_id)
{

	return (0);
}

/*
 * __vxge_hal_cq_arm_db_ptr_get - Returns the CQ ARM doorbell ptr for
 *			   the session
 *
 * @cqrqh: CQRQ
 * @session_id: Session Id
 *
 * This function returns the pointer to CQ ARM doorbell FIFO of the
 * given session.
 *
 */
/*ARGSUSED*/
u64
__vxge_hal_cq_arm_db_ptr_get(vxge_hal_cqrq_h cqrqh,
			u32 sess_id)
{

	return (0);
}

/*
 * __vxge_hal_non_offload_db_post - Post non offload doorbell
 *
 * @vpath_handle: vpath handle
 * @txdl_ptr: The starting location of the TxDL in host memory
 * @num_txds: The highest TxD in this TxDL (0 to 255 means 1 to 256)
 * @no_snoop: No snoop flags
 *
 * This function posts a non-offload doorbell to doorbell FIFO
 *
 */
void
__vxge_hal_non_offload_db_post(vxge_hal_vpath_h vpath_handle,
			    u64 txdl_ptr,
			    u32 num_txds,
			    u32 no_snoop)
{
	u64 *db_ptr;
	/*LINTED*/
	__vxge_hal_device_t *hldev;
	__vxge_hal_vpath_handle_t *vp =
	    (__vxge_hal_vpath_handle_t *)vpath_handle;

	vxge_assert((vpath_handle != NULL) && (txdl_ptr != 0));

	hldev = (__vxge_hal_device_t *)vp->vpath->hldev;

	vxge_hal_trace_log_fifo(hldev, vp->vpath->vp_id,
	    "==>  %s:%s:%d", __FILE__, __func__, __LINE__);

	vxge_hal_trace_log_fifo(hldev, vp->vpath->vp_id,
	    "vpath_handle = 0x"VXGE_OS_STXFMT", txdl_ptr = 0x"VXGE_OS_STXFMT
	    ", num_txds = %d, no_snoop = %d", (ptr_t)vpath_handle,
	    (ptr_t)txdl_ptr, num_txds, no_snoop);

	db_ptr = &vp->vpath->nofl_db->control_0;

	vxge_os_pio_mem_write64(vp->vpath->hldev->header.pdev,
	    vp->vpath->hldev->header.regh0,
	    VXGE_HAL_NODBW_TYPE(VXGE_HAL_NODBW_TYPE_NODBW) |
	    VXGE_HAL_NODBW_LAST_TXD_NUMBER(num_txds) |
	    VXGE_HAL_NODBW_GET_NO_SNOOP(no_snoop),
	    db_ptr++);

	vxge_os_pio_mem_write64(vp->vpath->hldev->header.pdev,
	    vp->vpath->hldev->header.regh0,
	    txdl_ptr,
	    db_ptr);

	vxge_hal_trace_log_fifo(hldev, vp->vpath->vp_id,
	    "<==  %s:%s:%d  Result: 0", __FILE__, __func__, __LINE__);
}

/*
 * __vxge_hal_non_offload_db_reset - Reset non offload doorbell fifo
 *
 * @vpath_handle: vpath handle
 *
 * This function resets non-offload doorbell FIFO
 *
 */
vxge_hal_status_e
__vxge_hal_non_offload_db_reset(vxge_hal_vpath_h vpath_handle)
{
	vxge_hal_status_e status;
	/*LINTED*/
	__vxge_hal_device_t *hldev;
	__vxge_hal_vpath_handle_t *vp =
	    (__vxge_hal_vpath_handle_t *)vpath_handle;

	vxge_assert(vpath_handle != NULL);

	hldev = (__vxge_hal_device_t *)vp->vpath->hldev;

	vxge_hal_trace_log_fifo(hldev, vp->vpath->vp_id,
	    "==>  %s:%s:%d", __FILE__, __func__, __LINE__);

	vxge_hal_trace_log_fifo(hldev, vp->vpath->vp_id,
	    "vpath_handle = 0x"VXGE_OS_STXFMT, (ptr_t)vpath_handle);

	vxge_os_pio_mem_write64(vp->vpath->hldev->header.pdev,
	    vp->vpath->hldev->header.regh0,
	    VXGE_HAL_CMN_RSTHDLR_CFG2_SW_RESET_FIFO0(
	    1 << (16 - vp->vpath->vp_id)),
	    &vp->vpath->hldev->common_reg->cmn_rsthdlr_cfg2);

	vxge_os_wmb();

	status = __vxge_hal_device_register_poll(vp->vpath->hldev->header.pdev,
	    vp->vpath->hldev->header.regh0,
	    &vp->vpath->hldev->common_reg->cmn_rsthdlr_cfg2, 0,
	    (u64)VXGE_HAL_CMN_RSTHDLR_CFG2_SW_RESET_FIFO0(
	    1 << (16 - vp->vpath->vp_id)),
	    VXGE_HAL_DEF_DEVICE_POLL_MILLIS);

	vxge_hal_trace_log_fifo(hldev, vp->vpath->vp_id,
	    "<==  %s:%s:%d  Result: 0", __FILE__, __func__, __LINE__);

	return (status);
}

/*
 * __vxge_hal_rxd_db_post - Post rxd doorbell
 *
 * @vpath_handle: vpath handle
 * @num_bytes: The number of bytes
 *
 * This function posts a rxd doorbell
 *
 */
void
__vxge_hal_rxd_db_post(vxge_hal_vpath_h vpath_handle,
		    u32 num_bytes)
{
	/*LINTED*/
	__vxge_hal_device_t *hldev;
	__vxge_hal_vpath_handle_t *vp =
	    (__vxge_hal_vpath_handle_t *)vpath_handle;

	vxge_assert(vpath_handle != NULL);

	hldev = (__vxge_hal_device_t *)vp->vpath->hldev;

	vxge_hal_trace_log_ring(hldev, vp->vpath->vp_id,
	    "==>  %s:%s:%d", __FILE__, __func__, __LINE__);

	vxge_hal_trace_log_fifo(hldev, vp->vpath->vp_id,
	    "vpath_handle = 0x"VXGE_OS_STXFMT", num_bytes = %d",
	    (ptr_t)vpath_handle, num_bytes);

	vxge_os_pio_mem_write64(vp->vpath->hldev->header.pdev,
	    vp->vpath->hldev->header.regh0,
	    VXGE_HAL_PRC_RXD_DOORBELL_NEW_QW_CNT((num_bytes>>3)),
	    &vp->vpath->vp_reg->prc_rxd_doorbell);

	vxge_hal_trace_log_ring(hldev, vp->vpath->vp_id,
	    "<==  %s:%s:%d  Result: 0", __FILE__, __func__, __LINE__);
}

/*
 * __vxge_hal_offload_db_post - Post offload doorbell
 *
 * @sqh: Send Queue handle
 * @sid: Identifies the session to which this Tx offload doorbell applies.
 * @sin: Identifies the incarnation of this Session Number.
 *
 * This function posts a offload doorbell to doorbell FIFO
 *
 */
/*ARGSUSED*/
vxge_hal_status_e
__vxge_hal_offload_db_post(vxge_hal_sq_h sqh,
			    u32 sid,
			    u32 sin,
			    u32 high_towi_num)
{

	return (VXGE_HAL_OK);
}

/*
 * __vxge_hal_message_db_post - Post message doorbell
 *
 * @vpath_handle: VPATH handle
 * @num_msg_bytes: The number of new message bytes made available
 *		by this doorbell entry.
 * @immed_msg: Immediate message to be sent
 * @immed_msg_len: Immediate message length
 *
 * This function posts a message doorbell to doorbell FIFO
 *
 */
void
__vxge_hal_message_db_post(vxge_hal_vpath_h vpath_handle,
			    u32 num_msg_bytes,
			    u8 *immed_msg,
			    u32 immed_msg_len)
{
	u32 i;
	u64 *ptr_64;
	u64 *db_ptr;
	/*LINTED*/
	__vxge_hal_device_t *hldev;
	__vxge_hal_vpath_handle_t *vp =
	    (__vxge_hal_vpath_handle_t *)vpath_handle;

	vxge_assert((vpath_handle != NULL) && (num_msg_bytes != 0));

	hldev = (__vxge_hal_device_t *)vp->vpath->hldev;

	vxge_hal_trace_log_dmq(hldev, vp->vpath->vp_id,
	    "==>  %s:%s:%d", __FILE__, __func__, __LINE__);

	vxge_hal_trace_log_dmq(hldev, vp->vpath->vp_id,
	    "vpath_handle = 0x"VXGE_OS_STXFMT", "
	    "num_msg_bytes = %d, immed_msg = 0x"VXGE_OS_STXFMT", "
	    "immed_msg_len = %d", (ptr_t)vpath_handle, num_msg_bytes,
	    (ptr_t)immed_msg, immed_msg_len);

	db_ptr = &vp->vpath->msg_db->control_0;

	vxge_os_pio_mem_write64(vp->vpath->hldev->header.pdev,
	    vp->vpath->hldev->header.regh0,
	    VXGE_HAL_MDBW_TYPE(VXGE_HAL_MDBW_TYPE_MDBW) |
	    VXGE_HAL_MDBW_MESSAGE_BYTE_COUNT(num_msg_bytes),
	    db_ptr++);

	vxge_os_pio_mem_write64(vp->vpath->hldev->header.pdev,
	    vp->vpath->hldev->header.regh0,
	    VXGE_HAL_MDBW_IMMEDIATE_BYTE_COUNT(immed_msg_len),
	    db_ptr++);

	for (i = 0; i < immed_msg_len/8; i++) {
		ptr_64 = (void *)&immed_msg[i*8];
		vxge_os_pio_mem_write64(vp->vpath->hldev->header.pdev,
		    vp->vpath->hldev->header.regh0,
		    *(ptr_64),
		    db_ptr++);
	}

	vxge_hal_trace_log_dmq(hldev, vp->vpath->vp_id,
	    "<==  %s:%s:%d  Result: 0", __FILE__, __func__, __LINE__);
}

/*
 * __vxge_hal_message_db_reset - Reset message doorbell fifo
 *
 * @vpath_handle: vpath handle
 *
 * This function resets message doorbell FIFO
 *
 */
vxge_hal_status_e
__vxge_hal_message_db_reset(vxge_hal_vpath_h vpath_handle)
{
	vxge_hal_status_e status;
	/*LINTED*/
	__vxge_hal_device_t *hldev;
	__vxge_hal_vpath_handle_t *vp =
	    (__vxge_hal_vpath_handle_t *)vpath_handle;

	vxge_assert(vpath_handle != NULL);

	hldev = (__vxge_hal_device_t *)vp->vpath->hldev;

	vxge_hal_trace_log_dmq(hldev, vp->vpath->vp_id,
	    "==>  %s:%s:%d", __FILE__, __func__, __LINE__);

	vxge_hal_trace_log_dmq(hldev, vp->vpath->vp_id,
	    "vpath_handle = 0x"VXGE_OS_STXFMT, (ptr_t)vpath_handle);

	vxge_os_pio_mem_write64(vp->vpath->hldev->header.pdev,
	    vp->vpath->hldev->header.regh0,
	    VXGE_HAL_CMN_RSTHDLR_CFG3_SW_RESET_FIFO1(
	    1 << (16 - vp->vpath->vp_id)),
	    &vp->vpath->hldev->common_reg->cmn_rsthdlr_cfg3);

	vxge_os_wmb();

	status = __vxge_hal_device_register_poll(vp->vpath->hldev->header.pdev,
	    vp->vpath->hldev->header.regh0,
	    &vp->vpath->hldev->common_reg->cmn_rsthdlr_cfg3, 0,
	    (u64)VXGE_HAL_CMN_RSTHDLR_CFG3_SW_RESET_FIFO1(
	    1 << (16 - vp->vpath->vp_id)),
	    VXGE_HAL_DEF_DEVICE_POLL_MILLIS);

	vxge_hal_trace_log_dmq(hldev, vp->vpath->vp_id,
	    "<==  %s:%s:%d  Result: 0", __FILE__, __func__, __LINE__);

	return (status);
}
