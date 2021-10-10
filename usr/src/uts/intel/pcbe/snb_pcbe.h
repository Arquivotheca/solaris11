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
 * Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _SYS_SNB_PCBE_H
#define	_SYS_SNB_PCBE_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * See section "Off-core Response Performance Monitoring" of the Intel Vol 3B
 * spec for OFFCORE_RSP_X definition.  Writing to a "reserved" bit seems to
 * cause a general protection fault.  However, the figure and table do not match
 * in the April 2011 spec.  The comment below is taken from the "Tables", not
 * the "Figures", which made more technical sense.
 *
 * Request Type[0:15]
 * - Reserved [12:14]
 * Common [16] - "Any" bit, if set supplier/snoop info is ignored.
 * Supplier Info [17:22]
 * - Reserved [22:30]
 * Snoop Info [31:37]
 */
#define	SNB_OFFCORE_RSP_MASK 0x3F803F8FFF

typedef struct snb_pcbe_events_table {
	ipcbe_events_table_t	event_info;
	uint16_t		msr_offset;
} snb_pcbe_events_table_t;

typedef struct snb_pcbe_config {
	uint16_t	msr_offset;
	uint64_t	msr_value;
} snb_pcbe_config_t;

extern const snb_pcbe_events_table_t snb_gpc_events_tbl[];
extern int snb_pcbe_events_num;

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SNB_PCBE_H */
