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
 * FileName :   vxgehal-ifmsg.h
 *
 * Description:  API and data structures used to communicate between privileged
 *              and non privileged drivers
 *
 * Created:       06 October 2008
 */

#ifndef	VXGE_HAL_IFMSG_H
#define	VXGE_HAL_IFMSG_H

__EXTERN_BEGIN_DECLS

/*
 * wmsg between the MRPCIM, SRPCIM and VPATHS is 64 bit long
 * The structure of the WMSG is
 *
 * Bits  0 to  7: Operation Code
 *	   8 to 12: vpath id
 *	  13 to 15: Sequence number
 *	  16 to 63: Data whose format depends on the operation code
 */
#define	VXGE_HAL_IFMSG_GET_OPCODE(wmsg)				bVAL8(wmsg, 0)
#define	VXGE_HAL_IFMSG_OPCODE(op)				vBIT(op, 0, 8)

#define	VXGE_HAL_IFMSG_GET_VPATH_ID(wmsg)			bVAL5(wmsg, 8)
#define	VXGE_HAL_IFMSG_VPATH_ID(id)				vBIT(id, 8, 5)

#define	VXGE_HAL_IFMSG_GET_SEQ_NUM(wmsg)			bVAL3(wmsg, 13)
#define	VXGE_HAL_IFMSG_SEQ_NUM(seq)				vBIT(seq, 13, 3)


/* Opcodes from MRPCIM/SRPCIM to VPATH */
#define	VXGE_HAL_IFMSG_MSV_OPCODE_UNKNOWN			 0
#define	VXGE_HAL_IFMSG_MSV_OPCODE_RESET_BEGIN			 1
#define	VXGE_HAL_IFMSG_MSV_OPCODE_RESET_END			 2
#define	VXGE_HAL_IFMSG_MSV_OPCODE_UP				 3
#define	VXGE_HAL_IFMSG_MSV_OPCODE_DOWN				 4

#define	VXGE_HAL_IFMSG_MSV_OPCODE_ACK				255



#define	VXGE_HAL_IFMSG_MSV_ACK_DATA_GET_OPCODE(wmsg)		bVAL8(wmsg, 16)
#define	VXGE_HAL_IFMSG_MSV_ACK_DATA_OPCODE(op)			vBIT(op, 16, 8)
#define	VXGE_HAL_IFMSG_MSV_ACK_DATA_GET_VPATH_ID(wmsg)		bVAL5(wmsg, 24)
#define	VXGE_HAL_IFMSG_MSV_ACK_DATA_VPATH_ID(id)		vBIT(id, 24, 3)
#define	VXGE_HAL_IFMSG_MSV_ACK_DATA_GET_SEQ_NUM(wmsg)		bVAL3(wmsg, 29)
#define	VXGE_HAL_IFMSG_MSV_ACK_DATA_SEQ_NUM(seq)		vBIT(seq, 29, 3)

#define	VXGE_HAL_IFMSG_MSV_UP_MSG					\
	VXGE_HAL_IFMSG_OPCODE(VXGE_HAL_IFMSG_MSV_OPCODE_UP)

#define	VXGE_HAL_IFMSG_MSV_DOWN_MSG					\
	VXGE_HAL_IFMSG_OPCODE(VXGE_HAL_IFMSG_MSV_OPCODE_DOWN)

#define	VXGE_HAL_IFMSG_MSV_RESET_BEGIN_MSG				\
	VXGE_HAL_IFMSG_OPCODE(VXGE_HAL_IFMSG_MSV_OPCODE_RESET_BEGIN)

#define	VXGE_HAL_IFMSG_MSV_RESET_END_MSG				\
	VXGE_HAL_IFMSG_OPCODE(VXGE_HAL_IFMSG_MSV_OPCODE_RESET_END)



/* Opcodes from VPATH to MRPCIM/SRPCIM */
#define	VXGE_HAL_IFMSG_VSM_OPCODE_UNKNOWN			 0
#define	VXGE_HAL_IFMSG_VSM_OPCODE_REGISTER			 2
#define	VXGE_HAL_IFMSG_VSM_OPCODE_RESET_BEGIN			 1

#define	VXGE_HAL_IFMSG_VSM_OPCODE_ACK				255



#define	VXGE_HAL_IFMSG_VSM_ACK_DATA_GET_OPCODE(wmsg)		bVAL8(wmsg, 16)
#define	VXGE_HAL_IFMSG_VSM_ACK_DATA_OPCODE(op)			vBIT(op, 16, 8)
#define	VXGE_HAL_IFMSG_VSM_ACK_DATA_GET_VPATH_ID(wmsg)		bVAL5(wmsg, 24)
#define	VXGE_HAL_IFMSG_VSM_ACK_DATA_VPATH_ID(id)		vBIT(id, 24, 3)
#define	VXGE_HAL_IFMSG_VSM_ACK_DATA_GET_SEQ_NUM(wmsg)		bVAL3(wmsg, 29)
#define	VXGE_HAL_IFMSG_VSM_ACK_DATA_SEQ_NUM(seq)		vBIT(seq, 29, 3)


u32 __vxge_hal_ifmsg_is_manager_up(u64 wmsg);
#pragma inline(__vxge_hal_ifmsg_is_manager_up)

vxge_hal_status_e
__vxge_hal_ifmsg_srpcim_to_vpath_reset_end_poll(
			__vxge_hal_device_t *hldev,
			u32 vp_id);

void
__vxge_hal_ifmsg_srpcim_to_vpath_wmsg_process(
			__vxge_hal_virtualpath_t *vpath,
			u64 wmsg);

void
__vxge_hal_ifmsg_srpcim_to_vpath_req_post(
			__vxge_hal_device_t *hldev,
			u32 vp_id,
			u64 wmsg);

void
__vxge_hal_ifmsg_vpath_to_srpcim_wmsg_process(
			__vxge_hal_device_t *hldev,
			u32 srpcim_id,
			u32 vp_id);

__EXTERN_END_DECLS

#endif /* VXGE_HAL_IFMSG_H */
