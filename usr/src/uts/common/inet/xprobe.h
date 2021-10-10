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
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_XPROBE_H
#define	_XPROBE_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/time.h>
#include <sys/ethernet.h>

/*
 * L2 payload of ETHERTYPE_ORCL packets MUST start with a 16 bit protocol
 * number. IPMP Transitive probes use a protocol value of ORCL_XPROBE_PROTO
 * and the payload  is a xprobe_data_t structure
 */
#define	ORCL_XPROBE_PROTO	1

typedef struct xprobe_data_s {
	ether_orcl_1_t	xp_orcl_1_proto;
#define	xp_proto	xp_orcl_1_proto.ether_orcl_1_proto
	uint8_t		xp_type;
	uint8_t		_xp_pad1;
	uint16_t	xp_seq;
	hrtime_t	xp_ts;
	/* the target and sender L2 addresses follow as  {len, value} pairs */
} xprobe_data_t;

#define	XP_REQUEST	0
#define	XP_RESPONSE	1

#ifdef	__cplusplus
}
#endif
#endif	/* _XPROBE_H */
