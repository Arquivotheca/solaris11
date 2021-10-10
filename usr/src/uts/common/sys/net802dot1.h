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

#ifndef	_SYS_NET802DOT1_H
#define	_SYS_NET802DOT1_H

#ifdef	__cplusplus
extern "C" {
#endif

#define	GVRP_DEST_ADDR	{ 0x01, 0x80, 0xC2, 0x00, 0x00, 0x21 }

#define	GVRP_PROTID_LEN	3	/* protid + version */
#define	GVRP_PROTID		{ 0x00, 0x01, 0x01 }

/* GARP PDU format as given in 802.1D section 12.10 */
#define	GARP_ATTR_LIST_ENDMARK  0x00

#define	GARP_LEAVE_ALL		0
#define	GARP_JOIN_EMPTY		1
#define	GARP_JOIN_IN		2
#define	GARP_LEAVE_EMPTY	3
#define	GARP_LEAVE_IN		4
#define	GARP_EMPTY		5

typedef struct gvrp_attr
{
	uint8_t len;
	uint8_t event;
	uint16_t value;
} gvrp_attr_t;

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_NET802DOT1_H */
