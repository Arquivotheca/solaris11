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
 * Copyright (c) 2002, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _IPP_IPGPC_BA_TABLE_H
#define	_IPP_IPGPC_BA_TABLE_H

#include <ipp/ipgpc/classifier-objects.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Header file for behavior aggregate (BA) table, which provides
 * classification on the 8 bit DiffServ field of IPv4/IPv6 packets
 */

extern int ba_insert(ba_table_id_t *, int, uint8_t, uint8_t);
extern int ba_retrieve(ba_table_id_t *, uint8_t, ht_match_t *);
extern void ba_remove(ba_table_id_t *, int, uint8_t, uint8_t);

#ifdef	__cplusplus
}
#endif

#endif	/* _IPP_IPGPC_BA_TABLE_H */
