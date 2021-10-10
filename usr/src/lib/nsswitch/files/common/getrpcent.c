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
 * Copyright (c) 1991, 2011, Oracle and/or its affiliates. All rights reserved.
 *
 * files/getrpcent.c -- "files" backend for nsswitch "rpc" database
 */

#include <rpc/rpcent.h>
#include "files_common.h"
#include <strings.h>
#include <ctype.h>
#include <stdlib.h>

static nss_status_t
getbyname(be, a)
	files_backend_ptr_t	be;
	void			*a;
{
	nss_XbyY_args_t		*argp = (nss_XbyY_args_t *)a;

	return (_nss_files_XY_all(be, argp, 1, argp->key.name,
			_nss_files_check_name_aliases));
}

static int
check_rpcnum(nss_XbyY_args_t *argp, const char *line, int linelen)
{
	int		r_number;
	const char	*limit, *linep;

	linep = line;
	limit = line + linelen;

	/* skip name */
	while (linep < limit && !isspace(*linep))
		linep++;
	/* skip the delimiting spaces */
	while (linep < limit && isspace(*linep))
		linep++;
	if (linep == limit)
		return (0);
	r_number = (int)strtol(linep, NULL, 10);
	return (r_number == argp->key.number);
}


static nss_status_t
getbynumber(be, a)
	files_backend_ptr_t	be;
	void			*a;
{
	nss_XbyY_args_t		*argp = (nss_XbyY_args_t *)a;
	char			numstr[12];

	(void) snprintf(numstr, 12, "%d", argp->key.number);
	return (_nss_files_XY_all(be, argp, 1, numstr, check_rpcnum));
}

static files_backend_op_t rpc_ops[] = {
	_nss_files_destr,
	_nss_files_endent,
	_nss_files_setent,
	_nss_files_getent_netdb,
	getbyname,
	getbynumber
};

/*ARGSUSED*/
nss_backend_t *
_nss_files_rpc_constr(dummy1, dummy2, dummy3)
	const char	*dummy1, *dummy2, *dummy3;
{
	return (_nss_files_constr(rpc_ops,
				sizeof (rpc_ops) / sizeof (rpc_ops[0]),
				"/etc/rpc",
				NSS_LINELEN_RPC,
				NULL, 0));
}
