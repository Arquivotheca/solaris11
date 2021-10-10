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
 * Copyright (c) 1999, 2001, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_V4_SUM_IMPL_H
#define	_V4_SUM_IMPL_H

#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/udp.h>

/*
 * Common functions for various IP/UDP module checksum implementations.
 */

#ifdef	__cplusplus
extern "C" {
#endif

#define	BIT_WRAP		(uint_t)0x10000	/* checksum wrap */

extern uint16_t ipv4cksum(uint16_t *, uint16_t);
extern uint16_t udp_chksum(struct udphdr *, const struct in_addr *,
    const struct in_addr *, uint8_t);

#ifdef	__cplusplus
}
#endif

#endif	/* _V4_SUM_IMPL_H */
