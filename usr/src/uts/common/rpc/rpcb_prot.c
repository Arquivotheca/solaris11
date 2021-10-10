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
 * Copyright 1997 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*	Copyright (c) 1983, 1984, 1985, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*
 * Portions of this source code were derived from Berkeley 4.3 BSD
 * under license from the Regents of the University of California.
 */

/*
 * rpcb_prot.c
 * XDR routines for the rpcbinder version 3.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#if !defined(lint) && defined(SCCSIDS)
static char sccsid[] = "@(#)rpcb_prot.c 1.9 89/04/21 Copyr 1984 Sun Micro";
#endif


#include <rpc/rpc.h>
#include <rpc/types.h>
#include <rpc/xdr.h>
#include <rpc/rpcb_prot.h>


bool_t
xdr_rpcb(XDR *xdrs, RPCB *objp)
{
	if (!xdr_rpcprog(xdrs, &objp->r_prog))
		return (FALSE);
	if (!xdr_rpcvers(xdrs, &objp->r_vers))
		return (FALSE);
	if (!xdr_string(xdrs, &objp->r_netid, ~0))
		return (FALSE);
	if (!xdr_string(xdrs, &objp->r_addr, ~0))
		return (FALSE);
	if (!xdr_string(xdrs, &objp->r_owner, ~0))
		return (FALSE);
	return (TRUE);
}

/*
 * XDR remote call arguments
 * written for XDR_ENCODE direction only
 */
bool_t
xdr_rpcb_rmtcallargs(XDR *xdrs, struct rpcb_rmtcallargs *objp)
{
	u_int lenposition, argposition, position;

	if (!xdr_rpcprog(xdrs, &objp->prog))
		return (FALSE);
	if (!xdr_rpcvers(xdrs, &objp->vers))
		return (FALSE);
	if (!xdr_rpcproc(xdrs, &objp->proc))
		return (FALSE);
	/*
	 * All the jugglery for just getting the size of the arguments
	 */
	lenposition = XDR_GETPOS(xdrs);
	if (!xdr_u_int(xdrs, &(objp->arglen)))
	    return (FALSE);
	argposition = XDR_GETPOS(xdrs);
	if (!(*(objp->xdr_args))(xdrs, objp->args_ptr))
	    return (FALSE);
	position = XDR_GETPOS(xdrs);
	objp->arglen = (u_int)position - (u_int)argposition;
	XDR_SETPOS(xdrs, lenposition);
	if (!xdr_u_int(xdrs, &(objp->arglen)))
	    return (FALSE);
	XDR_SETPOS(xdrs, position);
	return (TRUE);
}

/*
 * XDR remote call results
 * written for XDR_DECODE direction only
 */
bool_t
xdr_rpcb_rmtcallres(XDR *xdrs, struct rpcb_rmtcallres *objp)
{
	if (!xdr_string(xdrs, &objp->addr_ptr, ~0))
		return (FALSE);
	if (!xdr_u_int(xdrs, &objp->resultslen))
		return (FALSE);
	return ((*(objp->xdr_results))(xdrs, objp->results_ptr));
}

bool_t
xdr_netbuf(XDR *xdrs, struct netbuf *objp)
{
	/*
	 * If we're decoding and the caller has already allocated a buffer,
	 * throw away maxlen, since it doesn't apply to the caller's
	 * buffer.  xdr_bytes will return an error if the buffer isn't big
	 * enough.
	 */
	if (xdrs->x_op == XDR_DECODE && objp->buf != NULL) {
		u_int maxlen;

		if (!xdr_u_int(xdrs, &maxlen))
			return (FALSE);
	} else {
		if (!xdr_u_int(xdrs, (u_int *)&objp->maxlen))
			return (FALSE);
	}
	return (xdr_bytes(xdrs, (char **)&(objp->buf),
	    (u_int *)&(objp->len), objp->maxlen));
}
