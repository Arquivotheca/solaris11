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
 * Copyright 2010 QLogic Corporation. All rights reserved.
 */
/*
 * Error codes for HAL - NIC interface.
 *
 */

#ifndef _QLCNIC_ERRORCODE_H_
#define	_QLCNIC_ERRORCODE_H_

#ifdef __cplusplus
extern "C" {
#endif

/*
 *        Common Error Codes
 */

#define	QLCNIC_RCODE_SUCCESS			0
/* Insuff. mem resource on host */
#define	QLCNIC_RCODE_NO_HOST_MEM		1
/* Insuff. misc. resources on host */
#define	QLCNIC_RCODE_NO_HOST_RESOURCE	2
/* Insuff. crb resources on card */
#define	QLCNIC_RCODE_NO_CARD_CRB		3
/* Insuff. mem resources on card */
#define	QLCNIC_RCODE_NO_CARD_MEM		4
/* Insuff. misc. resources on card */
#define	QLCNIC_RCODE_NO_CARD_RESOURCE	5
/* One or more args to routine were out-of-range */
#define	QLCNIC_RCODE_INVALID_ARGS		6
/* Requested action is invalid / in error */
#define	QLCNIC_RCODE_INVALID_ACTION		7
/* Requested RX/TX has invalid state */
#define	QLCNIC_RCODE_INVALID_STATE		8
/* Requested action is not supported */
#define	QLCNIC_RCODE_NOT_SUPPORTED		9
/* Requested action is not allowed */
#define	QLCNIC_RCODE_NOT_PERMITTED		10
/* System not ready for action */
#define	QLCNIC_RCODE_NOT_READY			11
/* Target of requested action does not exist */
#define	QLCNIC_RCODE_DOES_NOT_EXIST		2
/* Requested action already performed/complete */
#define	QLCNIC_RCODE_ALREADY_EXISTSi	13
/* Invalid signature provided */
#define	QLCNIC_RCODE_BAD_SIGNATURE		14
/* Valid command, not implemented */
#define	QLCNIC_RCODE_CMD_NOT_IMPLi		15
/* Invalid/Unknown command */
#define	QLCNIC_RCODE_CMD_INVALID		16
/* Timeout on polling rsp status  */
#define	QLCNIC_RCODE_TIMEOUT			17
#define	QLCNIC_RCODE_CMD_FAILED			18
#define	QLCNIC_RCODE_MAX_EXCEEDED		19
#define	QLCNIC_RCODE_MAX				20

/*
 *       Macros
 */
#define	QLCNIC_IS_RCODE_VALID(ERR)		(ERR >= QLCNIC_RCODE_MAX)

#ifdef __cplusplus
}
#endif

#endif /* _QLCNIC_ERRORCODE_H_ */
