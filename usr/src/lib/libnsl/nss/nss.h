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
 * Copyright (c) 2002, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_NSS_H
#define	_NSS_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <nss_common.h>
#include <netdb.h>

#ifdef	__cplusplus
extern "C" {
#endif

extern struct hostent	*_switch_gethostbyname_r(const char *, struct hostent *,
    char *, int, int *);
extern struct hostent	*_switch_gethostbyaddr_r(const char *, int, int,
    struct hostent *, char *, int, int *);
extern struct hostent	*_switch_getipnodebyname_r(const char *,
    struct hostent *, char *, int, int, int, int *);
extern struct hostent	*_switch_getipnodebyaddr_r(const char *, int, int,
    struct hostent *, char *, int, int *);
extern struct hostent	*_door_gethostbyname_r(const char *, struct hostent *,
    char *, int, int *);
extern struct hostent	*_door_gethostbyaddr_r(const char *, int, int,
    struct hostent *, char *, int, int *);
extern struct hostent	*_door_getipnodebyname_r(const char *, struct hostent *,
    char *, int, int, int, int *);
extern struct hostent	*_door_getipnodebyaddr_r(const char *, int, int,
    struct hostent *, char *, int, int *);
extern struct hostent	*__mappedtov4(struct hostent *, int *);
extern int	str2hostent(const char *, int, void *, char *, int);
extern int	str2hostent6(const char *, int, void *, char *, int);
extern int	__str2hostent(int, const char *, int, void *, char *, int);
extern int	str2servent(const char *, int, void *, char *, int);
extern void	_nss_initf_hosts(nss_db_params_t *);
extern void	_nss_initf_ipnodes(nss_db_params_t *);
extern void	order_haddrlist_af(sa_family_t, char **);
extern int	nss_ioctl(int, int, void *);
extern int	__nss2herrno(nss_status_t);

#ifdef	__cplusplus
}
#endif

#endif	/* _NSS_H */
