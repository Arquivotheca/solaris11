/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 *	nis/getprotoent.c -- "nis" backend for nsswitch "protocols" database
 *
 *	Copyright (c) 1988-1992 Sun Microsystems Inc
 *	All Rights Reserved.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <stdio.h>
#include "nis_common.h"
#include <synch.h>
#include <netdb.h>
#include <string.h>

static nss_status_t
getbyname(be, a)
	nis_backend_ptr_t	be;
	void			*a;
{
	nss_XbyY_args_t		*argp = (nss_XbyY_args_t *) a;

	return (_nss_nis_lookup(be, argp, 1, "protocols.byname",
		argp->key.name, 0));
}

static nss_status_t
getbynumber(be, a)
	nis_backend_ptr_t	be;
	void			*a;
{
	nss_XbyY_args_t		*argp = (nss_XbyY_args_t *) a;
	char			numstr[12];

	(void) sprintf(numstr, "%d", argp->key.number);
	return (_nss_nis_lookup(be, argp, 1, "protocols.bynumber",
				numstr, 0));
}

static nis_backend_op_t proto_ops[] = {
	_nss_nis_destr,
	_nss_nis_endent,
	_nss_nis_setent,
	_nss_nis_getent_netdb,
	getbyname,
	getbynumber
};

/*ARGSUSED*/
nss_backend_t *
_nss_nis_protocols_constr(dummy1, dummy2, dummy3)
	const char	*dummy1, *dummy2, *dummy3;
{
	return (_nss_nis_constr(proto_ops,
				sizeof (proto_ops) / sizeof (proto_ops[0]),
				"protocols.bynumber"));
}
