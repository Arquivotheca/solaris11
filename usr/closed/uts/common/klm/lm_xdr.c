/*
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Copyright 1991 NCR Corporation - Dayton, Ohio, USA
 */

/*	Copyright (c) 1990 UNIX System Laboratories, Inc.	*/
/*	Copyright (c) 1983, 1984, 1985, 1986, 1987, 1988, 1989, 1990 AT&T */
/*	  All Rights Reserved  	*/

/*
 * Portions of this source code were derived from Berkeley 4.3 BSD
 * under license from the Regents of the University of California.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * The procedures in this file was generated using rpcgen.
 * Do not edit the file.
 */
#include <rpc/rpc.h>
#include <rpc/pmap_prot.h>
#include <nfs/lm_nlm.h>
#include <rpcsvc/sm_inter.h>
#include <rpcsvc/nsm_addr.h>

bool_t
xdr_nlm_stats(xdrs, objp)
	XDR *xdrs;
	nlm_stats *objp;
{
	if (!xdr_enum(xdrs, (enum_t *)objp)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_nlm_holder(xdrs, objp)
	XDR *xdrs;
	nlm_holder *objp;
{
	if (!xdr_bool(xdrs, &objp->exclusive)) {
		return (FALSE);
	}
	if (!xdr_int(xdrs, &objp->svid)) {
		return (FALSE);
	}
	if (!xdr_netobj(xdrs, &objp->oh)) {
		return (FALSE);
	}
	if (!xdr_u_int(xdrs, &objp->l_offset)) {
		return (FALSE);
	}
	if (!xdr_u_int(xdrs, &objp->l_len)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_nlm_testrply(xdrs, objp)
	XDR *xdrs;
	nlm_testrply *objp;
{
	if (!xdr_nlm_stats(xdrs, &objp->stat)) {
		return (FALSE);
	}
	switch (objp->stat) {
	case nlm_denied:
		if (!xdr_nlm_holder(xdrs, &objp->nlm_testrply_u.holder)) {
			return (FALSE);
		}
		break;
	default:
		break;
	}
	return (TRUE);
}

bool_t
xdr_nlm_stat(xdrs, objp)
	XDR *xdrs;
	nlm_stat *objp;
{
	if (!xdr_nlm_stats(xdrs, &objp->stat)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_nlm_res(xdrs, objp)
	XDR *xdrs;
	nlm_res *objp;
{
	if (!xdr_netobj(xdrs, &objp->cookie)) {
		return (FALSE);
	}
	if (!xdr_nlm_stat(xdrs, &objp->stat)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_nlm_testres(xdrs, objp)
	XDR *xdrs;
	nlm_testres *objp;
{
	if (!xdr_netobj(xdrs, &objp->cookie)) {
		return (FALSE);
	}
	if (!xdr_nlm_testrply(xdrs, &objp->stat)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_nlm_lock(xdrs, objp)
	XDR *xdrs;
	nlm_lock *objp;
{
	if (!xdr_string(xdrs, &objp->caller_name, LM_MAXSTRLEN)) {
		return (FALSE);
	}
	if (!xdr_netobj(xdrs, &objp->fh)) {
		return (FALSE);
	}
	if (!xdr_netobj(xdrs, &objp->oh)) {
		return (FALSE);
	}
	if (!xdr_int(xdrs, &objp->svid)) {
		return (FALSE);
	}
	if (!xdr_u_int(xdrs, &objp->l_offset)) {
		return (FALSE);
	}
	if (!xdr_u_int(xdrs, &objp->l_len)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_nlm_lockargs(xdrs, objp)
	XDR *xdrs;
	nlm_lockargs *objp;
{
	if (!xdr_netobj(xdrs, &objp->cookie)) {
		return (FALSE);
	}
	if (!xdr_bool(xdrs, &objp->block)) {
		return (FALSE);
	}
	if (!xdr_bool(xdrs, &objp->exclusive)) {
		return (FALSE);
	}
	if (!xdr_nlm_lock(xdrs, &objp->alock)) {
		return (FALSE);
	}
	if (!xdr_bool(xdrs, &objp->reclaim)) {
		return (FALSE);
	}
	if (!xdr_int(xdrs, &objp->state)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_nlm_cancargs(xdrs, objp)
	XDR *xdrs;
	nlm_cancargs *objp;
{
	if (!xdr_netobj(xdrs, &objp->cookie)) {
		return (FALSE);
	}
	if (!xdr_bool(xdrs, &objp->block)) {
		return (FALSE);
	}
	if (!xdr_bool(xdrs, &objp->exclusive)) {
		return (FALSE);
	}
	if (!xdr_nlm_lock(xdrs, &objp->alock)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_nlm_testargs(xdrs, objp)
	XDR *xdrs;
	nlm_testargs *objp;
{
	if (!xdr_netobj(xdrs, &objp->cookie)) {
		return (FALSE);
	}
	if (!xdr_bool(xdrs, &objp->exclusive)) {
		return (FALSE);
	}
	if (!xdr_nlm_lock(xdrs, &objp->alock)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_nlm_unlockargs(xdrs, objp)
	XDR *xdrs;
	nlm_unlockargs *objp;
{
	if (!xdr_netobj(xdrs, &objp->cookie)) {
		return (FALSE);
	}
	if (!xdr_nlm_lock(xdrs, &objp->alock)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_fsh_mode(xdrs, objp)
	XDR *xdrs;
	fsh_mode *objp;
{
	if (!xdr_enum(xdrs, (enum_t *)objp)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_fsh_access(xdrs, objp)
	XDR *xdrs;
	fsh_access *objp;
{
	if (!xdr_enum(xdrs, (enum_t *)objp)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_nlm_share(xdrs, objp)
	XDR *xdrs;
	nlm_share *objp;
{
	if (!xdr_string(xdrs, &objp->caller_name, LM_MAXSTRLEN)) {
		return (FALSE);
	}
	if (!xdr_netobj(xdrs, &objp->fh)) {
		return (FALSE);
	}
	if (!xdr_netobj(xdrs, &objp->oh)) {
		return (FALSE);
	}
	if (!xdr_fsh_mode(xdrs, &objp->mode)) {
		return (FALSE);
	}
	if (!xdr_fsh_access(xdrs, &objp->access)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_nlm_shareargs(xdrs, objp)
	XDR *xdrs;
	nlm_shareargs *objp;
{
	if (!xdr_netobj(xdrs, &objp->cookie)) {
		return (FALSE);
	}
	if (!xdr_nlm_share(xdrs, &objp->share)) {
		return (FALSE);
	}
	if (!xdr_bool(xdrs, &objp->reclaim)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_nlm_shareres(xdrs, objp)
	XDR *xdrs;
	nlm_shareres *objp;
{
	if (!xdr_netobj(xdrs, &objp->cookie)) {
		return (FALSE);
	}
	if (!xdr_nlm_stats(xdrs, &objp->stat)) {
		return (FALSE);
	}
	if (!xdr_int(xdrs, &objp->sequence)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_nlm_notify(xdrs, objp)
	XDR *xdrs;
	nlm_notify *objp;
{
	if (!xdr_string(xdrs, &objp->name, MAXXDRHOSTNAMELEN)) {
		return (FALSE);
	}
	if (!xdr_int(xdrs, &objp->state)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_sm_name(xdrs, objp)
	XDR *xdrs;
	sm_name *objp;
{
	if (!xdr_string(xdrs, &objp->mon_name, SM_MAXSTRLEN)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_res(xdrs, objp)
	XDR *xdrs;
	res *objp;
{
	if (!xdr_enum(xdrs, (enum_t *)objp)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_sm_stat_res(xdrs, objp)
	XDR *xdrs;
	sm_stat_res *objp;
{
	if (!xdr_res(xdrs, &objp->res_stat)) {
		return (FALSE);
	}
	if (!xdr_int(xdrs, &objp->state)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_sm_stat(xdrs, objp)
	XDR *xdrs;
	sm_stat *objp;
{
	if (!xdr_int(xdrs, &objp->state)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_my_id(xdrs, objp)
	XDR *xdrs;
	my_id *objp;
{
	if (!xdr_string(xdrs, &objp->my_name, SM_MAXSTRLEN)) {
		return (FALSE);
	}
	if (!xdr_int(xdrs, &objp->my_prog)) {
		return (FALSE);
	}
	if (!xdr_int(xdrs, &objp->my_vers)) {
		return (FALSE);
	}
	if (!xdr_int(xdrs, &objp->my_proc)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_mon_id(xdrs, objp)
	XDR *xdrs;
	mon_id *objp;
{
	if (!xdr_string(xdrs, &objp->mon_name, SM_MAXSTRLEN)) {
		return (FALSE);
	}
	if (!xdr_my_id(xdrs, &objp->my_id)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_mon(xdrs, objp)
	XDR *xdrs;
	mon *objp;
{
	if (!xdr_mon_id(xdrs, &objp->mon_id)) {
		return (FALSE);
	}
	if (!xdr_opaque(xdrs, objp->priv, 16)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_status(xdrs, objp)
	XDR *xdrs;
	status *objp;
{
	if (!xdr_string(xdrs, &objp->mon_name, SM_MAXSTRLEN)) {
		return (FALSE);
	}
	if (!xdr_int(xdrs, &objp->state)) {
		return (FALSE);
	}
	if (!xdr_opaque(xdrs, objp->priv, 16)) {
		return (FALSE);
	}
	return (TRUE);
}

/*
 * xdr_pmap
 *
 * Taken from libnsl/rpc/pmap_prot.c
 */
bool_t
xdr_pmap(xdrs, regs)
	XDR *xdrs;
	struct pmap *regs;
{
	if (xdr_rpcprog(xdrs, &regs->pm_prog) &&
	    xdr_rpcvers(xdrs, &regs->pm_vers) &&
	    xdr_rpcprot(xdrs, &regs->pm_prot))
		return (xdr_rpcport(xdrs, &regs->pm_port));
	return (FALSE);
}

/* NLM4 support */

bool_t
xdr_nlm4_stats(xdrs, objp)
	XDR *xdrs;
	nlm4_stats *objp;
{
	if (!xdr_enum(xdrs, (enum_t *)objp)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_nlm4_holder(xdrs, objp)
	XDR *xdrs;
	nlm4_holder *objp;
{
	if (!xdr_bool(xdrs, &objp->exclusive)) {
		return (FALSE);
	}
	if (!xdr_int(xdrs, &objp->svid)) {
		return (FALSE);
	}
	if (!xdr_netobj(xdrs, &objp->oh)) {
		return (FALSE);
	}
	if (!xdr_u_longlong_t(xdrs, &objp->l_offset)) {
		return (FALSE);
	}
	if (!xdr_u_longlong_t(xdrs, &objp->l_len)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_nlm4_testrply(xdrs, objp)
	XDR *xdrs;
	nlm4_testrply *objp;
{
	if (!xdr_nlm4_stats(xdrs, &objp->stat)) {
		return (FALSE);
	}
	switch (objp->stat) {
	case NLM4_DENIED:
		if (!xdr_nlm4_holder(xdrs, &objp->nlm4_testrply_u.holder)) {
			return (FALSE);
		}
		break;
	default:
		break;
	}
	return (TRUE);
}

bool_t
xdr_nlm4_stat(xdrs, objp)
	XDR *xdrs;
	nlm4_stat *objp;
{
	if (!xdr_nlm4_stats(xdrs, &objp->stat)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_nlm4_res(xdrs, objp)
	XDR *xdrs;
	nlm4_res *objp;
{
	if (!xdr_netobj(xdrs, &objp->cookie)) {
		return (FALSE);
	}
	if (!xdr_nlm4_stat(xdrs, &objp->stat)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_nlm4_testres(xdrs, objp)
	XDR *xdrs;
	nlm4_testres *objp;
{
	if (!xdr_netobj(xdrs, &objp->cookie)) {
		return (FALSE);
	}
	if (!xdr_nlm4_testrply(xdrs, &objp->stat)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_nlm4_lock(xdrs, objp)
	XDR *xdrs;
	nlm4_lock *objp;
{
	if (!xdr_string(xdrs, &objp->caller_name, LM_MAXSTRLEN)) {
		return (FALSE);
	}
	if (!xdr_netobj(xdrs, &objp->fh)) {
		return (FALSE);
	}
	if (!xdr_netobj(xdrs, &objp->oh)) {
		return (FALSE);
	}
	if (!xdr_int(xdrs, &objp->svid)) {
		return (FALSE);
	}
	if (!xdr_u_longlong_t(xdrs, &objp->l_offset)) {
		return (FALSE);
	}
	if (!xdr_u_longlong_t(xdrs, &objp->l_len)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_nlm4_lockargs(xdrs, objp)
	XDR *xdrs;
	nlm4_lockargs *objp;
{
	if (!xdr_netobj(xdrs, &objp->cookie)) {
		return (FALSE);
	}
	if (!xdr_bool(xdrs, &objp->block)) {
		return (FALSE);
	}
	if (!xdr_bool(xdrs, &objp->exclusive)) {
		return (FALSE);
	}
	if (!xdr_nlm4_lock(xdrs, &objp->alock)) {
		return (FALSE);
	}
	if (!xdr_bool(xdrs, &objp->reclaim)) {
		return (FALSE);
	}
	if (!xdr_int(xdrs, &objp->state)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_nlm4_cancargs(xdrs, objp)
	XDR *xdrs;
	nlm4_cancargs *objp;
{
	if (!xdr_netobj(xdrs, &objp->cookie)) {
		return (FALSE);
	}
	if (!xdr_bool(xdrs, &objp->block)) {
		return (FALSE);
	}
	if (!xdr_bool(xdrs, &objp->exclusive)) {
		return (FALSE);
	}
	if (!xdr_nlm4_lock(xdrs, &objp->alock)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_nlm4_testargs(xdrs, objp)
	XDR *xdrs;
	nlm4_testargs *objp;
{
	if (!xdr_netobj(xdrs, &objp->cookie)) {
		return (FALSE);
	}
	if (!xdr_bool(xdrs, &objp->exclusive)) {
		return (FALSE);
	}
	if (!xdr_nlm4_lock(xdrs, &objp->alock)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_nlm4_unlockargs(xdrs, objp)
	XDR *xdrs;
	nlm4_unlockargs *objp;
{
	if (!xdr_netobj(xdrs, &objp->cookie)) {
		return (FALSE);
	}
	if (!xdr_nlm4_lock(xdrs, &objp->alock)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_fsh4_mode(xdrs, objp)
	XDR *xdrs;
	fsh4_mode *objp;
{
	if (!xdr_enum(xdrs, (enum_t *)objp)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_fsh4_access(xdrs, objp)
	XDR *xdrs;
	fsh4_access *objp;
{
	if (!xdr_enum(xdrs, (enum_t *)objp)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_nlm4_share(xdrs, objp)
	XDR *xdrs;
	nlm4_share *objp;
{
	if (!xdr_string(xdrs, &objp->caller_name, LM_MAXSTRLEN)) {
		return (FALSE);
	}
	if (!xdr_netobj(xdrs, &objp->fh)) {
		return (FALSE);
	}
	if (!xdr_netobj(xdrs, &objp->oh)) {
		return (FALSE);
	}
	if (!xdr_fsh4_mode(xdrs, &objp->mode)) {
		return (FALSE);
	}
	if (!xdr_fsh4_access(xdrs, &objp->access)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_nlm4_shareargs(xdrs, objp)
	XDR *xdrs;
	nlm4_shareargs *objp;
{
	if (!xdr_netobj(xdrs, &objp->cookie)) {
		return (FALSE);
	}
	if (!xdr_nlm4_share(xdrs, &objp->share)) {
		return (FALSE);
	}
	if (!xdr_bool(xdrs, &objp->reclaim)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_nlm4_shareres(xdrs, objp)
	XDR *xdrs;
	nlm4_shareres *objp;
{
	if (!xdr_netobj(xdrs, &objp->cookie)) {
		return (FALSE);
	}
	if (!xdr_nlm4_stats(xdrs, &objp->stat)) {
		return (FALSE);
	}
	if (!xdr_int(xdrs, &objp->sequence)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_nlm4_notify(xdrs, objp)
	XDR *xdrs;
	nlm4_notify *objp;
{
	if (!xdr_string(xdrs, &objp->name, MAXXDRHOSTNAMELEN)) {
		return (FALSE);
	}
	if (!xdr_int(xdrs, &objp->state)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_nsm_addr_res(xdrs, objp)
	XDR *xdrs;
	nsm_addr_res *objp;
{
	if (!xdr_enum(xdrs, (enum_t *)objp)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_reg1args(xdrs, objp)
	XDR *xdrs;
	reg1args *objp;
{
	if (!xdr_u_int(xdrs, &objp->family)) {
		return (FALSE);
	}
	if (!xdr_string(xdrs, &objp->name, 1024)) {
		return (FALSE);
	}
	if (!xdr_netobj(xdrs, &objp->address)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_reg1res(xdrs, objp)
	XDR *xdrs;
	reg1res *objp;
{
	if (!xdr_nsm_addr_res(xdrs, &objp->status)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_unreg1args(xdrs, objp)
	XDR *xdrs;
	unreg1args *objp;
{
	if (!xdr_u_int(xdrs, &objp->family)) {
		return (FALSE);
	}
	if (!xdr_string(xdrs, &objp->name, 1024)) {
		return (FALSE);
	}
	if (!xdr_netobj(xdrs, &objp->address)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_unreg1res(xdrs, objp)
	XDR *xdrs;
	unreg1res *objp;
{
	if (!xdr_nsm_addr_res(xdrs, &objp->status)) {
		return (FALSE);
	}
	return (TRUE);
}
