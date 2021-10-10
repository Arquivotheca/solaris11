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
 * Copyright (c) 1989, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*	Copyright (c) 1983, 1984, 1985, 1986, 1987, 1988, 1989 AT&T */
/*	  All Rights Reserved   */

/*
 * Portions of this source code were derived from Berkeley
 * under license from the Regents of the University of
 * California.
 */

#include "mt.h"
#include <rpc/rpc.h>
#include <sys/time.h>
#include <sys/types.h>
#include <string.h>
#include <rpcsvc/yp_b.h>

/* Default timeout can be changed using clnt_control() */
static struct timeval TIMEOUT = { 25, 0 };

void *
ypbindproc_null_3(void *argp, CLIENT *clnt)
{
	static char res;

	res = 0;
	if (clnt_call(clnt, YPBINDPROC_NULL, xdr_void,
	    argp, xdr_void, &res, TIMEOUT) != RPC_SUCCESS)
		return (NULL);
	return ((void *)&res);
}

ypbind_resp *
ypbindproc_domain_3(ypbind_domain *argp, CLIENT *clnt)
{
	static ypbind_resp res;

	(void) memset(&res, 0, sizeof (res));
	if (clnt_call(clnt, YPBINDPROC_DOMAIN,
	    xdr_ypbind_domain, (char *)argp, xdr_ypbind_resp,
	    (char *)&res, TIMEOUT) != RPC_SUCCESS)
		return (NULL);
	return (&res);
}

void *
ypbindproc_setdom_3(ypbind_setdom *argp, CLIENT *clnt)
{
	static char res;

	res = 0;
	if (clnt_call(clnt, YPBINDPROC_SETDOM,
	    xdr_ypbind_setdom, (char *)argp, xdr_void, &res,
	    TIMEOUT) != RPC_SUCCESS)
		return (NULL);
	return ((void *)&res);
}
