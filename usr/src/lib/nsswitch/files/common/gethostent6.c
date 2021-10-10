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
 * Copyright (c) 1999, 2011, Oracle and/or its affiliates. All rights reserved.
 *
 * files/gethostent6.c -- "files" backend for nsswitch "hosts" database
 */

#include <netdb.h>
#include "files_common.h"
#include <string.h>
#include <strings.h>
#include <stddef.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/nameser.h>
#include <ctype.h>
#include <nss.h>

extern nss_status_t __nss_files_XY_hostbyname();
extern int __nss_files_check_addr(int, nss_XbyY_args_t *, const char *, int);
extern int convert_ip(int af, const char *line, int linelen,
    char *h_addrp, int *h_length);
extern uint_t hash_hostname(nss_XbyY_args_t *argp, int keyhash,
    const char *line, int linelen);
extern uint_t *hash_arr_hostname(nss_XbyY_args_t *argp, int keyhash,
    const char *line, int linelen);

extern int check_name_host(nss_XbyY_args_t *, const char *, int,
    int, const char **, int *, void *, int *);

static uint_t
hash_ip(nss_XbyY_args_t *argp, int keyhash, const char *line,
    int linelen)
{
	const uint_t	*value;
	uint_t		hash = 0;
	int		h_length;
	struct in6_addr	addr_ipv6;
	char		*h_addrp;

	if (keyhash) {
		(void) memcpy(&addr_ipv6, argp->key.hostaddr.addr,
		    sizeof (addr_ipv6));
	} else {
		h_addrp = (char *)&addr_ipv6;
		if (convert_ip(AF_INET6, line, linelen, h_addrp,
		    &h_length) == 0)
			return (0);
	}

	/* Take 4 bytes which are shared between IPv4 and IPv6. */
	value = (const uint_t *)&addr_ipv6.s6_addr[12];
	hash = *value;

	return (hash);
}

/*
 * hash_ipnodes functions are used for single-value attribute
 * hash_arr_ipnodes are used for multi-value attribute (line in local file
 *     where hostnames can be canonical + aliases)
 */
static files_hash_func hash_ipnodes[2] = { hash_ip, hash_hostname };
static files_h_ar_func hash_arr_ipnodes[2] = { NULL, hash_arr_hostname };
static files_hash_t hashinfo = {
	DEFAULTMUTEX,
	sizeof (struct hostent),
	NSS_BUFLEN_IPNODES,
	2,
	hash_ipnodes,
	hash_arr_ipnodes
};

static int
check_name_hash(nss_XbyY_args_t *argp, const char *line, int linelen)
{
	const char	*namep;
	int		namelen;
	struct in6_addr	addr_ipv6;
	int		i;

	return (check_name_host(argp, line, linelen, AF_INET6, &namep, &namelen,
	    &addr_ipv6, &i));
}

static nss_status_t
getbyname(files_backend_ptr_t be, void *a)
{
	nss_XbyY_args_t		*argp = (nss_XbyY_args_t *)a;
	nss_status_t		res;

	be->flags = FC_FLAG_HOSTNAME;
	/*
	 * For systems without nscd and for libnsl API there is non-hash
	 * function with different output buffer.
	 */
	if (argp->buf.result != NULL)
		res = __nss_files_XY_hostbyname(be, argp, argp->key.ipnode.name,
		    AF_INET6);
	else
		res = _nss_files_XY_hash(be, argp, 1, &hashinfo, 1,
		    check_name_hash);
	if (res != NSS_SUCCESS)
		argp->h_errno = __nss2herrno(res);

	return (res);
}

static int
check_addr(nss_XbyY_args_t *argp, const char *line, int linelen)
{
	return (__nss_files_check_addr(AF_INET6, argp, line, linelen));
}

static nss_status_t
getbyaddr(files_backend_ptr_t be, void *a)
{
	nss_XbyY_args_t		*argp	= (nss_XbyY_args_t *)a;
	nss_status_t		res;

	be->flags = FC_FLAG_IP;
	res = _nss_files_XY_hash(be, argp, 1, &hashinfo, 0, check_addr);
	if (res != NSS_SUCCESS)
		argp->h_errno = __nss2herrno(res);
	return (res);
}

static files_backend_op_t ipnodes_ops[] = {
	_nss_files_destr,
	_nss_files_endent,
	_nss_files_setent,
	_nss_files_getent_netdb,
	getbyname,
	getbyaddr,
};

/*ARGSUSED*/
nss_backend_t *
_nss_files_ipnodes_constr(dummy1, dummy2, dummy3)
	const char	*dummy1, *dummy2, *dummy3;
{
	return (_nss_files_constr(ipnodes_ops,
	    sizeof (ipnodes_ops) / sizeof (ipnodes_ops[0]),
	    _PATH_IPNODES, NSS_LINELEN_HOSTS, &hashinfo, FC_FLAG_HOSTNAME));
}
