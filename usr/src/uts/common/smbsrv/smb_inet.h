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
 * Copyright (c) 2009, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _SMB_INET_H
#define	_SMB_INET_H

#ifdef	__cplusplus
extern "C" {
#endif

#ifndef _KERNEL
#include <inet/tcp.h>
#include <arpa/inet.h>
#endif /* !_KERNEL */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

typedef struct smb_inaddr {
	union {
		in_addr_t au_ipv4;
		in6_addr_t au_ipv6;
		in6_addr_t au_ip;
	} au_addr;
	int a_family;
} smb_inaddr_t;

#define	a_ipv4 au_addr.au_ipv4
#define	a_ipv6 au_addr.au_ipv6
#define	a_ip au_addr.au_ip

#define	SMB_IPSTRLEN(family) \
(((family) == AF_INET) ? INET_ADDRSTRLEN : INET6_ADDRSTRLEN)

boolean_t smb_inet_equal(smb_inaddr_t *, smb_inaddr_t *);
boolean_t smb_inet_same_subnet(smb_inaddr_t *, smb_inaddr_t *, uint32_t);
boolean_t smb_inet_iszero(smb_inaddr_t *);
const char *smb_inet_ntop(const smb_inaddr_t *, char *, int);

#ifdef	__cplusplus
}
#endif

#endif /* _SMB_INET_H */
