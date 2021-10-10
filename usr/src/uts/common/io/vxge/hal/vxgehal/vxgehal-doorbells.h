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
 * FileName :   vxgehal-doorbells.h
 *
 * Description:  HAL HW doorbells structures
 *
 * Created:       07 Dec 2005
 */

#ifndef	VXGE_HAL_DOOR_BELLS_H
#define	VXGE_HAL_DOOR_BELLS_H

__EXTERN_BEGIN_DECLS

/*
 * struct __vxge_hal_non_offload_db_wrapper_t - Non-offload Doorbell Wrapper
 * @control_0: Bits 0 to 7 - Doorbell type.
 *	       Bits 8 to 31 - Reserved.
 *	       Bits 32 to 39 - The highest TxD in this TxDL.
 *	       Bits 40 to 47 - Reserved.
 *	       Bits 48 to 55 - Reserved.
 *	       Bits 56 to 63 - No snoop flags.
 * @txdl_ptr:  The starting location of the TxDL in host memory.
 *
 * Created by the host and written to the adapter via PIO to a Kernel Doorbell
 * FIFO. All non-offload doorbell wrapper fields must be written by the host as
 * part of a doorbell write. Consumed by the adapter but is not written by the
 * adapter.
 */
typedef __vxge_os_attr_cacheline_aligned
	struct __vxge_hal_non_offload_db_wrapper_t {
	u64		control_0;
#define	VXGE_HAL_NODBW_GET_TYPE(ctrl0)				bVAL8(ctrl0, 0)
#define	VXGE_HAL_NODBW_TYPE(val)				vBIT(val, 0, 8)
#define	VXGE_HAL_NODBW_TYPE_NODBW				0

#define	VXGE_HAL_NODBW_GET_LAST_TXD_NUMBER(ctrl0)		bVAL8(ctrl0, 32)
#define	VXGE_HAL_NODBW_LAST_TXD_NUMBER(val)			vBIT(val, 32, 8)

#define	VXGE_HAL_NODBW_GET_NO_SNOOP(ctrl0)			bVAL8(ctrl0, 56)
#define	VXGE_HAL_NODBW_LIST_NO_SNOOP(val)			vBIT(val, 56, 8)
#define	VXGE_HAL_NODBW_LIST_NO_SNOOP_TXD_READ_TXD0_WRITE	0x2
#define	VXGE_HAL_NODBW_LIST_NO_SNOOP_TX_FRAME_DATA_READ		0x1

	u64		txdl_ptr;
} __vxge_hal_non_offload_db_wrapper_t;

/*
 * struct __vxge_hal_offload_db_wrapper_t - Tx-Offload Doorbell Wrapper
 * @control_0: Bits 0 to 7 - Doorbell type.
 *	       Bits 8 to 31 - Identifies the session to which this Tx
 *		offload doorbell applies.
 *	       Bits 32 to 40 - Identifies the incarnation of this Session
 *		Number. The adapter assigns a Session Instance
 *		Number of 0 to a session when that Session Number
 *		is first used. Each subsequent assignment of that
 *		Session Number from the free pool causes this
 *		number to be incremented, with wrap eventually
 *		occurring from 255 back to 0.
 *	       Bits 40 to 63 - Identifies the end of the TOWI list for
 *		this session to the adapter.
 * @control_1: Bits 0 to 7 - Identifies what is included in this doorbell entry.
 *	       Bits 8 to 15 - The number of Immediate data bytes included in
 *		this doorbell.
 *	       Bits 16 to 63 - Reserved.
 *
 * Created by the host and written to the adapter via PIO to a Kernel Doorbell
 * FIFO.  All Tx Offload doorbell wrapper fields must be written by the host as
 * part of a doorbell write. Consumed by the adapter but is never written by the
 * adapter.
 */
typedef __vxge_os_attr_cacheline_aligned
	struct __vxge_hal_offload_db_wrapper_t {
	u64		control_0;
#define	VXGE_HAL_ODBW_GET_TYPE(ctrl0)			bVAL8(ctrl0, 0)
#define	VXGE_HAL_ODBW_TYPE(val)				vBIT(val, 0, 8)
#define	VXGE_HAL_ODBW_TYPE_ODBW				1

#define	VXGE_HAL_ODBW_GET_SESSION_NUMBER(ctrl0)		bVAL24(ctrl0, 8)
#define	VXGE_HAL_ODBW_SESSION_NUMBER(val)		vBIT(val, 8, 24)

#define	VXGE_HAL_ODBW_GET_SESSION_INST_NUMBER(ctrl0)	bVAL8(ctrl0, 32)
#define	VXGE_HAL_ODBW_SESSION_INST_NUMBER(val)		vBIT(val, 32, 8)

#define	VXGE_HAL_ODBW_GET_HIGH_TOWI_NUMBER(ctrl0)	bVAL24(ctrl0, 40)
#define	VXGE_HAL_ODBW_HIGH_TOWI_NUMBER(val)		vBIT(val, 40, 24)

	u64		control_1;
#define	VXGE_HAL_ODBW_GET_ENTRY_TYPE(ctrl1)		bVAL8(ctrl1, 0)
#define	VXGE_HAL_ODBW_ENTRY_TYPE(val)			vBIT(val, 0, 8)
#define	VXGE_HAL_ODBW_ENTRY_TYPE_WRAPPER_ONLY		0x0
#define	VXGE_HAL_ODBW_ENTRY_TYPE_WRAPPER_TOWI		0x1
#define	VXGE_HAL_ODBW_ENTRY_TYPE_WRAPPER_TOWI_DATA	0x2

#define	VXGE_HAL_ODBW_GET_IMMEDIATE_BYTE_COUNT(ctrl1)	bVAL8(ctrl1, 8)
#define	VXGE_HAL_ODBW_IMMEDIATE_BYTE_COUNT(val)		vBIT(val, 8, 8)

} __vxge_hal_offload_db_wrapper_t;

/*
 * struct __vxge_hal_offload_atomic_db_wrapper_t - Atomic Tx-Offload Doorbell
 *						 Wrapper
 * @control_0:	Bits 0 to 7 - Doorbell type.
 *	       Bits 8 to 31 - Identifies the session to which this Tx
 *		offload doorbell applies.
 *	       Bits 32 to 40 - Identifies the incarnation of this Session
 *		Number. The adapter assigns a Session Instance
 *		Number of 0 to a session when that Session Number
 *		is first used. Each subsequent assignment of that
 *		Session Number from the free pool causes this
 *		number to be incremented, with wrap eventually
 *		occurring from 255 back to 0.
 *	       Bits 40 to 63 - Identifies the end of the TOWI list for
 *		this session to the adapter.
 *
 * Created by the host and written to the adapter via PIO to a Kernel Doorbell
 * FIFO.  All Tx Offload doorbell wrapper fields must be written by the host as
 * part of a doorbell write. Consumed by the adapter but is never written by the
 * adapter.
 */
typedef __vxge_os_attr_cacheline_aligned
struct __vxge_hal_offload_atomic_db_wrapper_t {
	u64		control_0;
#define	VXGE_HAL_ODBW_GET_TYPE(ctrl0)			bVAL8(ctrl0, 0)
#define	VXGE_HAL_ODBW_TYPE(val)				vBIT(val, 0, 8)
#define	VXGE_HAL_ODBW_TYPE_ATOMIC			2

#define	VXGE_HAL_ODBW_GET_SESSION_NUMBER(ctrl0)		bVAL24(ctrl0, 8)
#define	VXGE_HAL_ODBW_SESSION_NUMBER(val)		vBIT(val, 8, 24)

#define	VXGE_HAL_ODBW_GET_SESSION_INST_NUMBER(ctrl0)	bVAL8(ctrl0, 32)
#define	VXGE_HAL_ODBW_SESSION_INST_NUMBER(val)		vBIT(val, 32, 8)

#define	VXGE_HAL_ODBW_GET_HIGH_TOWI_NUMBER(ctrl0)	bVAL24(ctrl0, 40)
#define	VXGE_HAL_ODBW_HIGH_TOWI_NUMBER(val)		vBIT(val, 40, 24)

} __vxge_hal_offload_atomic_db_wrapper_t;



/*
 * struct __vxge_hal_messaging_db_wrapper_t - Messaging Doorbell Wrapper
 * @control_0:	Bits 0 to 7 - Doorbell type.
 *	       Bits 8 to 31 - Reserved.
 *	       Bits 32 to 63 - The number of new message bytes made available
 *		by this doorbell entry.
 * @control_1:	Bits 0 to 7 - Reserved.
 *	       Bits 8 to 15 - The number of Immediate messaging bytes included
 *		in this doorbell.
 *	       Bits 16 to 63 - Reserved.
 *
 * Created by the host and written to the adapter via PIO to a Kernel Doorbell
 * FIFO. All message doorbell wrapper fields must be written by the host as
 * part of a doorbell write. Consumed by the adapter but not written by adapter.
 */
typedef __vxge_os_attr_cacheline_aligned
	struct __vxge_hal_messaging_db_wrapper_t {
	u64		control_0;
#define	VXGE_HAL_MDBW_GET_TYPE(ctrl0)			bVAL8(ctrl0, 0)
#define	VXGE_HAL_MDBW_TYPE(val)				vBIT(val, 0, 8)
#define	VXGE_HAL_MDBW_TYPE_MDBW				3

#define	VXGE_HAL_MDBW_GET_MESSAGE_BYTE_COUNT(ctrl0)	bVAL32(ctrl0, 32)
#define	VXGE_HAL_MDBW_MESSAGE_BYTE_COUNT(val)		vBIT(val, 32, 32)

	u64		control_1;
#define	VXGE_HAL_MDBW_GET_IMMEDIATE_BYTE_COUNT(ctrl1)	bVAL8(ctrl1, 8)
#define	VXGE_HAL_MDBW_IMMEDIATE_BYTE_COUNT(val)		vBIT(val, 8, 8)

} __vxge_hal_messaging_db_wrapper_t;

u64
__vxge_hal_offload_db_ptr_get(vxge_hal_sq_h sqh,
			u32 sess_id);

u64
__vxge_hal_wqe_db_ptr_get(vxge_hal_srq_h srqh,
			u32 sess_id);

u64
__vxge_hal_cq_arm_db_ptr_get(vxge_hal_cqrq_h cqrqh,
			u32 sess_id);

void
__vxge_hal_non_offload_db_post(vxge_hal_vpath_h vpath_handle,
			    u64 txdl_ptr,
			    u32 num_txds,
			    u32 no_snoop);

void
__vxge_hal_rxd_db_post(vxge_hal_vpath_h vpath_handle,
			    u32 num_bytes);

vxge_hal_status_e
__vxge_hal_non_offload_db_reset(vxge_hal_vpath_h vpath_handle);

vxge_hal_status_e
__vxge_hal_offload_db_post(vxge_hal_sq_h sqh,
			    u32 sid,
			    u32 sin,
			    u32 high_towi_num);

void
__vxge_hal_message_db_post(vxge_hal_vpath_h vpath_handle,
			    u32 num_msg_bytes,
			    u8 *immed_msg,
			    u32 immed_msg_len);

vxge_hal_status_e
__vxge_hal_message_db_reset(vxge_hal_vpath_h vpath_handle);

__EXTERN_END_DECLS

#endif /* VXGE_HAL_DOOR_BELLS_H */
