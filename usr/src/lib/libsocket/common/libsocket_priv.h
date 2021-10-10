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
#ifndef _LIBSOCKET_PRIV_H
#define	_LIBSOCKET_PRIV_H

#include <net/if.h>
#include <ifaddrs.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Possible values of flags passed to getallifaddrs.
 */
#define	IA_UP_ADDRS_ONLY	0x00000001

extern int	getallifaddrs(sa_family_t, struct ifaddrs **, int64_t,
		    uint32_t);
extern int	getallifs(int, sa_family_t, struct lifreq **, int *, int64_t);
extern int	getnetmaskbyaddr(const struct in_addr, struct in_addr *);

#ifdef	__cplusplus
}
#endif

#endif	/* _LIBSOCKET_PRIV_H */
