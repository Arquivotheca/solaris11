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
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/* Copyright (c) 1983, 1984, 1985, 1986, 1987, 1988, 1989 AT&T */
/* All Rights Reserved */
/*
 * University Copyright- Copyright (c) 1982, 1986, 1988
 * The Regents of the University of California
 * All Rights Reserved
 *
 * University Acknowledgment- Portions of this document are derived from
 * software developed by the University of California, Berkeley, and its
 * contributors.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * pmap_svc.c
 * The server procedure for the version 2 portmaper.
 * All the portmapper related interface from the portmap side.
 */

#ifdef PORTMAP
#include <stdio.h>
#include <alloca.h>
#include <ucred.h>
#include <rpc/rpc.h>
#include <rpc/pmap_prot.h>
#include <rpc/rpcb_prot.h>
#include "rpcbind.h"

#ifdef RPCBIND_DEBUG
#include <netdir.h>
#endif

static PMAPLIST *find_service_pmap();
static bool_t pmapproc_change();
static bool_t pmapproc_getport();
static bool_t pmapproc_dump();

/*
 * Called for all the version 2 inquiries.
 */
void
pmap_service(rqstp, xprt)
	register struct svc_req *rqstp;
	register SVCXPRT *xprt;
{
	rpcbs_procinfo(RPCBVERS_2_STAT, rqstp->rq_proc);
	switch (rqstp->rq_proc) {
	case PMAPPROC_NULL:
		/*
		 * Null proc call
		 */
#ifdef RPCBIND_DEBUG
		fprintf(stderr, "PMAPPROC_NULL\n");
#endif
		PMAP_CHECK(xprt, rqstp->rq_proc);

		if ((!svc_sendreply(xprt, (xdrproc_t)xdr_void, NULL)) &&
			debugging) {
			if (doabort) {
				rpcbind_abort();
			}
		}
		break;

	case PMAPPROC_SET:
		/*
		 * Set a program, version to port mapping
		 */
		pmapproc_change(rqstp, xprt, rqstp->rq_proc);
		break;

	case PMAPPROC_UNSET:
		/*
		 * Remove a program, version to port mapping.
		 */
		pmapproc_change(rqstp, xprt, rqstp->rq_proc);
		break;

	case PMAPPROC_GETPORT:
		/*
		 * Lookup the mapping for a program, version and return its
		 * port number.
		 */
		pmapproc_getport(rqstp, xprt);
		break;

	case PMAPPROC_DUMP:
		/*
		 * Return the current set of mapped program, version
		 */
#ifdef RPCBIND_DEBUG
		fprintf(stderr, "PMAPPROC_DUMP\n");
#endif
		PMAP_CHECK(xprt, rqstp->rq_proc);
		pmapproc_dump(rqstp, xprt);
		break;

	case PMAPPROC_CALLIT:
		/*
		 * Calls a procedure on the local machine. If the requested
		 * procedure is not registered this procedure does not return
		 * error information!!
		 * This procedure is only supported on rpc/udp and calls via
		 * rpc/udp. It passes null authentication parameters.
		 */
		rpcbproc_callit_com(rqstp, xprt, PMAPPROC_CALLIT, PMAPVERS);
		break;

	default:
		PMAP_CHECK(xprt, rqstp->rq_proc);
		svcerr_noproc(xprt);
		break;
	}
}

/*
 * returns the item with the given program, version number. If that version
 * number is not found, it returns the item with that program number, so that
 * the port number is now returned to the caller. The caller when makes a
 * call to this program, version number, the call will fail and it will
 * return with PROGVERS_MISMATCH. The user can then determine the highest
 * and the lowest version number for this program using clnt_geterr() and
 * use those program version numbers.
 */
static PMAPLIST *
find_service_pmap(prog, vers, prot)
	ulong_t prog;
	ulong_t vers;
	ulong_t prot;
{
	register PMAPLIST *hit = NULL;
	register PMAPLIST *pml;

	for (pml = list_pml; pml != NULL; pml = pml->pml_next) {
		if ((pml->pml_map.pm_prog != prog) ||
			(pml->pml_map.pm_prot != prot))
			continue;
		hit = pml;
		if (pml->pml_map.pm_vers == vers)
			break;
	}
	return (hit);
}

extern char *getowner(SVCXPRT *xprt, char *);

/*ARGSUSED*/
static bool_t
pmapproc_change(rqstp, xprt, op)
	struct svc_req *rqstp;
	register SVCXPRT *xprt;
	unsigned long op;
{
	PMAP reg;
	RPCB rpcbreg;
	int ans;
	struct sockaddr_in *who;
	extern bool_t map_set(), map_unset();
	char owner[64];

	if (!svc_getargs(xprt, (xdrproc_t)xdr_pmap, (char *)&reg)) {
		svcerr_decode(xprt);
		return (FALSE);
	}
	who = svc_getcaller(xprt);

#ifdef RPCBIND_DEBUG
	fprintf(stderr, "%s request for (%lu, %lu) : ",
		op == PMAPPROC_SET ? "PMAP_SET" : "PMAP_UNSET",
		reg.pm_prog, reg.pm_vers);
#endif

	/* Don't allow unset/set from remote. */
	if (!localxprt(xprt, B_TRUE)) {
		ans = FALSE;
		goto done_change;
	}

	rpcbreg.r_owner = getowner(xprt, owner);

	if ((op == PMAPPROC_SET) && (reg.pm_port < IPPORT_RESERVED) &&
	    (ntohs(who->sin_port) >= IPPORT_RESERVED)) {
		ans = FALSE;
		goto done_change;
	}
	rpcbreg.r_prog = reg.pm_prog;
	rpcbreg.r_vers = reg.pm_vers;

	if (op == PMAPPROC_SET) {
		char buf[32];

		sprintf(buf, "0.0.0.0.%d.%d", (reg.pm_port >> 8) & 0xff,
			reg.pm_port & 0xff);
		rpcbreg.r_addr = buf;
		if (reg.pm_prot == IPPROTO_UDP) {
			rpcbreg.r_netid = udptrans;
		} else if (reg.pm_prot == IPPROTO_TCP) {
			rpcbreg.r_netid = tcptrans;
		} else {
			ans = FALSE;
			goto done_change;
		}
		ans = map_set(&rpcbreg, rpcbreg.r_owner);
	} else if (op == PMAPPROC_UNSET) {
		bool_t ans1, ans2;

		rpcbreg.r_addr = NULL;
		rpcbreg.r_netid = tcptrans;
		ans1 = map_unset(&rpcbreg, rpcbreg.r_owner);
		rpcbreg.r_netid = udptrans;
		ans2 = map_unset(&rpcbreg, rpcbreg.r_owner);
		ans = ans1 || ans2;
	} else {
		ans = FALSE;
	}
done_change:
	PMAP_LOG(ans, xprt, op, reg.pm_prog);

	if ((!svc_sendreply(xprt, (xdrproc_t)xdr_long, (caddr_t)&ans)) &&
	    debugging) {
		fprintf(stderr, "portmap: svc_sendreply\n");
		if (doabort) {
			rpcbind_abort();
		}
	}
#ifdef RPCBIND_DEBUG
	fprintf(stderr, "%s\n", ans == TRUE ? "succeeded" : "failed");
#endif
	if (op == PMAPPROC_SET)
		rpcbs_set(RPCBVERS_2_STAT, ans);
	else
		rpcbs_unset(RPCBVERS_2_STAT, ans);
	return (TRUE);
}

/* ARGSUSED */
static bool_t
pmapproc_getport(rqstp, xprt)
	struct svc_req *rqstp;
	register SVCXPRT *xprt;
{
	PMAP reg;
	int port = 0;
	PMAPLIST *fnd;
#ifdef RPCBIND_DEBUG
	char *uaddr;
#endif

	if (!svc_getargs(xprt, (xdrproc_t)xdr_pmap, (char *)&reg)) {
		svcerr_decode(xprt);
		return (FALSE);
	}
	PMAP_CHECK_RET(xprt, rqstp->rq_proc, FALSE);

#ifdef RPCBIND_DEBUG
	uaddr =  taddr2uaddr(rpcbind_get_conf(xprt->xp_netid),
			    svc_getrpccaller(xprt));
	fprintf(stderr, "PMAP_GETPORT request for (%lu, %lu, %s) from %s :",
		reg.pm_prog, reg.pm_vers,
		reg.pm_prot == IPPROTO_UDP ? "udp" : "tcp", uaddr);
	free(uaddr);
#endif
	fnd = find_service_pmap(reg.pm_prog, reg.pm_vers, reg.pm_prot);
	if (fnd) {
		char serveuaddr[32], *ua;
		int h1, h2, h3, h4, p1, p2;
		char *netid;

		if (reg.pm_prot == IPPROTO_UDP) {
			ua = udp_uaddr;
			netid = udptrans;
		} else {
			ua = tcp_uaddr; /* To get the len */
			netid = tcptrans;
		}
		if (ua == NULL) {
			goto sendreply;
		}
		if (sscanf(ua, "%d.%d.%d.%d.%d.%d", &h1, &h2, &h3,
				&h4, &p1, &p2) == 6) {
			p1 = (fnd->pml_map.pm_port >> 8) & 0xff;
			p2 = (fnd->pml_map.pm_port) & 0xff;
			sprintf(serveuaddr, "%d.%d.%d.%d.%d.%d",
				h1, h2, h3, h4, p1, p2);
			if (is_bound(netid, serveuaddr)) {
				port = fnd->pml_map.pm_port;
			} else { /* this service is dead; delete it */
				delete_prog(reg.pm_prog);
			}
		}
	}
sendreply:
	if ((!svc_sendreply(xprt, (xdrproc_t)xdr_long, (caddr_t)&port)) &&
			debugging) {
		(void) fprintf(stderr, "portmap: svc_sendreply\n");
		if (doabort) {
			rpcbind_abort();
		}
	}
#ifdef RPCBIND_DEBUG
	fprintf(stderr, "port = %d\n", port);
#endif
	rpcbs_getaddr(RPCBVERS_2_STAT, reg.pm_prog, reg.pm_vers,
		reg.pm_prot == IPPROTO_UDP ? udptrans : tcptrans,
		port ? udptrans : "");

	return (TRUE);
}

/* ARGSUSED */
static bool_t
pmapproc_dump(rqstp, xprt)
	struct svc_req *rqstp;
	register SVCXPRT *xprt;
{
	if (!svc_getargs(xprt, (xdrproc_t)xdr_void, NULL)) {
		svcerr_decode(xprt);
		return (FALSE);
	}
	if ((!svc_sendreply(xprt, (xdrproc_t)xdr_pmaplist_ptr,
			(caddr_t)&list_pml)) && debugging) {
		(void) fprintf(stderr, "portmap: svc_sendreply\n");
		if (doabort) {
			rpcbind_abort();
		}
	}
	return (TRUE);
}

/*
 * Is the transport local?  The original rpcbind code tried to
 * figure out all the network interfaces but there can be a nearly
 * infinite number of network interfaces.  And the number of interfaces can
 * vary over time.
 *
 * Note that when we get here, we've already establised that we're
 * dealing with a TCP/IP endpoint.
 */
boolean_t
localxprt(SVCXPRT *transp, boolean_t forceipv4)
{
	struct sockaddr_gen *sgen = svc_getgencaller(transp);

	switch (SGFAM(sgen)) {
	case AF_INET:
		break;
	case AF_INET6:
		if (forceipv4)
			return (B_FALSE);
		break;
	default:
		return (B_FALSE);
	}

	/*
	 * Get the peer's uid; if it is known it is sufficiently
	 * authenticated and considered local.  The magic behind this
	 * call is all in libnsl.
	 */
	return (rpcb_caller_uid(transp) != -1);
}
#endif /* PORTMAP */
