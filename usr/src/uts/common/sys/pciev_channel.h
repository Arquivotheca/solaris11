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
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */
#ifndef	__PCIEV_CHANNEL_H__
#define	__PCIEV_CHANNEL_H__

/* PCIV packet header type definitions */
typedef enum {
	PCIV_PKT_CTRL = 0x0,
	PCIV_PKT_DRV,
	PCIV_PKT_FABERR,
	PCIV_PKT_TYPE_MAX
} pciv_pkt_type_t;

/* Definitions for Root Complex address */
#define	PCIV_INVAL_RC_ADDR 0xffffffff	/* Invalid RC address */

/* Packet header definitions */
#pragma pack(1)
typedef struct pciv_pkt_hdr {
	uint32_t	type;		/* packet type */
	uint32_t	rc_addr;	/* RC address */
	uint32_t	src_addr;	/* source address */
	uint32_t	dst_addr;	/* dest address */
	uint32_t	size;		/* size of payload */
	uint32_t	resv2;		/* Reserved */
} pciv_pkt_hdr_t;
#pragma pack()

/* Framwork control command definitions */
#define	PCIV_CTRL_NOTIFY_READY		0x1
#define	PCIV_CTRL_NOTIFY_NOT_READY	0x2

/* Control packet payload definitions */
#pragma pack(1)
typedef struct pciv_ctrl_op {
	uint16_t	cmd;		/* command */
	uint16_t	status;		/* return value */
	uint32_t	resv[3];	/* reserved */
} pciv_ctrl_op_t;
#pragma pack()

#endif	/* __PCIEV_CHANNEL_H__ */
