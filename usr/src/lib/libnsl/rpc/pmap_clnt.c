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
 * Copyright (c) 1989, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/* Copyright (c) 1983, 1984, 1985, 1986, 1987, 1988, 1989 AT&T */
/* All Rights Reserved */
/*
 * Portions of this source code were derived from Berkeley
 * 4.3 BSD under license from the Regents of the University of
 * California.
 */

#ifdef PORTMAP

/*
 * interface to pmap rpc service.
 */
#include "mt.h"
#include "rpc_mt.h"
#include <rpc/rpc.h>
#include <rpc/nettype.h>
#include <netdir.h>
#include <rpc/pmap_prot.h>
#include <rpc/pmap_clnt.h>
#include <rpc/pmap_rmt.h>
#include <string.h>
#include <syslog.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

int use_portmapper = 0;
static const struct timeval timeout = { 5, 0 };
static const struct timeval tottimeout = { 60, 0 };
static const struct timeval rmttimeout = { 3, 0 };

/*
 * Set a mapping between program, version and port.
 * Calls the pmap service remotely to do the mapping.
 */
bool_t
pmap_set(rpcprog_t program, rpcvers_t version, rpcprot_t protocol,
								ushort_t port)
{
	bool_t rslt;
	struct netbuf *na;
	struct netconfig *nconf;
	char buf[32];

	if ((protocol != IPPROTO_UDP) && (protocol != IPPROTO_TCP))
		return (FALSE);
	nconf = __rpc_getconfip(protocol == IPPROTO_UDP ? "udp" : "tcp");
	if (!nconf)
		return (FALSE);
	(void) sprintf(buf, "0.0.0.0.%d.%d", port >> 8 & 0xff, port & 0xff);
	na = uaddr2taddr(nconf, buf);
	if (!na) {
		freenetconfigent(nconf);
		return (FALSE);
	}
	rslt = rpcb_set(program, version, nconf, na);

	netdir_free((char *)na, ND_ADDR);
	freenetconfigent(nconf);
	return (rslt);
}

/*
 * Remove the mapping between program, version and port.
 * Calls the pmap service remotely to do the un-mapping.
 */
bool_t
pmap_unset(rpcprog_t program, rpcvers_t version)
{
	struct netconfig *nconf;
	bool_t udp_rslt = FALSE;
	bool_t tcp_rslt = FALSE;

	nconf = __rpc_getconfip("udp");
	if (nconf) {
		udp_rslt = rpcb_unset(program, version, nconf);
		freenetconfigent(nconf);
	}
	nconf = __rpc_getconfip("tcp");
	if (nconf) {
		tcp_rslt = rpcb_unset(program, version, nconf);
		freenetconfigent(nconf);
	}
	/*
	 * XXX: The call may still succeed even if only one of the
	 * calls succeeded.  This was the best that could be
	 * done for backward compatibility.
	 */
	return (tcp_rslt || udp_rslt);
}

/*
 * Find the mapped port for program, version.
 * Calls the pmap service remotely to do the lookup.
 * Returns 0 if no map exists.
 *
 * XXX: It talks only to the portmapper and not to the rpcbind
 * service.  There may be implementations out there which do not
 * run portmapper as a part of rpcbind.
 */
ushort_t
pmap_getport(struct sockaddr_in *address, rpcprog_t program,
					rpcvers_t version, rpcprot_t protocol)
{
	ushort_t port = 0;
	int fd = RPC_ANYFD;
	CLIENT *client;
	struct pmap parms;

	address->sin_port = htons(PMAPPORT);
	client = clntudp_bufcreate(address, PMAPPROG, PMAPVERS, timeout,
				&fd, RPCSMALLMSGSIZE, RPCSMALLMSGSIZE);
	if (client != NULL) {
		parms.pm_prog = program;
		parms.pm_vers = version;
		parms.pm_prot = protocol;
		parms.pm_port = 0;	/* not needed or used */
		if (CLNT_CALL(client, PMAPPROC_GETPORT, (xdrproc_t)xdr_pmap,
			    (caddr_t)&parms, (xdrproc_t)xdr_u_short,
			    (caddr_t)&port, tottimeout) != RPC_SUCCESS) {
			rpc_createerr.cf_stat = RPC_PMAPFAILURE;
			clnt_geterr(client, &rpc_createerr.cf_error);
		} else if (port == 0) {
			rpc_createerr.cf_stat = RPC_PROGNOTREGISTERED;
		}
		CLNT_DESTROY(client);
	}
	address->sin_port = 0;
	return (port);
}

/*
 * Get a copy of the current port maps.
 * Calls the pmap service remotely to do get the maps.
 */
struct pmaplist *
pmap_getmaps(struct sockaddr_in *address)
{
	pmaplist_ptr head = NULL;
	int fd = RPC_ANYFD;
	struct timeval minutetimeout;
	CLIENT *client;

	minutetimeout.tv_sec = 60;
	minutetimeout.tv_usec = 0;
	address->sin_port = htons(PMAPPORT);
	client = clnttcp_create(address, PMAPPROG, PMAPVERS, &fd, 50, 500);
	if (client != NULL) {
		if (CLNT_CALL(client, PMAPPROC_DUMP, (xdrproc_t)xdr_void,
			    NULL, (xdrproc_t)xdr_pmaplist_ptr,
			    (caddr_t)&head, minutetimeout) != RPC_SUCCESS) {
			(void) syslog(LOG_ERR, "%s",
			clnt_sperror(client, "pmap_getmaps rpc problem"));
		}
		CLNT_DESTROY(client);
	}
	address->sin_port = 0;
	return ((struct pmaplist *)head);
}

/*
 * pmapper remote-call-service interface.
 * This routine is used to call the pmapper remote call service
 * which will look up a service program in the port maps, and then
 * remotely call that routine with the given parameters. This allows
 * programs to do a lookup and call in one step.
 */
enum clnt_stat
pmap_rmtcall(struct sockaddr_in *addr, rpcprog_t prog, rpcvers_t vers,
	rpcproc_t proc, xdrproc_t xdrargs, caddr_t argsp, xdrproc_t xdrres,
	caddr_t resp, struct timeval tout, rpcport_t *port_ptr)
{
	int fd = RPC_ANYFD;
	CLIENT *client;
	struct p_rmtcallargs a;
	struct p_rmtcallres r;
	enum clnt_stat stat;
	short tmp = addr->sin_port;

	addr->sin_port = htons(PMAPPORT);
	client = clntudp_create(addr, PMAPPROG, PMAPVERS, rmttimeout, &fd);
	if (client != NULL) {
		a.prog = prog;
		a.vers = vers;
		a.proc = proc;
		a.args.args_val = argsp;
		a.xdr_args = xdrargs;
		r.res.res_val = resp;
		r.xdr_res = xdrres;
		stat = CLNT_CALL(client, PMAPPROC_CALLIT,
				(xdrproc_t)xdr_rmtcallargs,
				(caddr_t)&a, (xdrproc_t)xdr_rmtcallres,
				(caddr_t)&r, tout);
		CLNT_DESTROY(client);
	} else {
		stat = RPC_FAILED;
	}
	addr->sin_port = tmp;
	*port_ptr = r.port;
	return (stat);
}

#endif /* PORTMAP */
