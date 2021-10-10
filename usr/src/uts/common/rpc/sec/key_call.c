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

/*	Copyright (c) 1983, 1984, 1985, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*
 * Portions of this source code were derived from Berkeley 4.3 BSD
 * under license from the Regents of the University of California.
 */


/*
 * key_call.c, Interface to keyserver
 * key_encryptsession(agent, deskey, cr)-encrypt a session key to talk to agent
 * key_decryptsession(agent, deskey) - decrypt ditto
 * key_gendes(deskey) - generate a secure des key
 * key_getnetname(netname, cr) - get the netname from the keyserv
 * netname2user(...) - get unix credential for given name (kernel only)
 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/pathname.h>
#include <sys/sysmacros.h>
#include <sys/vnode.h>
#include <sys/uio.h>
#include <sys/debug.h>
#include <sys/utsname.h>
#include <sys/cmn_err.h>
#include <sys/atomic.h>

#include <rpc/rpc.h>
#include <rpc/key_prot.h>

#define	KEY_TIMEOUT	30	/* per-try timeout in seconds */
#define	KEY_NRETRY	6	/* number of retries */

struct auth_globals {
	struct knetconfig	auth_config;
	char 			auth_keyname[SYS_NMLN+16];
};

static struct timeval keytrytimeout = { KEY_TIMEOUT, 0 };

static enum clnt_stat key_call(rpcproc_t, xdrproc_t, char *, xdrproc_t, char *,
    cred_t *);

/* ARGSUSED */
void *
auth_zone_init(zoneid_t zoneid)
{
	struct auth_globals *authg;

	authg = kmem_zalloc(sizeof (*authg), KM_SLEEP);
	return (authg);
}

/* ARGSUSED */
void
auth_zone_fini(zoneid_t zoneid, void *data)
{
	struct auth_globals *authg = data;

	kmem_free(authg, sizeof (*authg));
}

enum clnt_stat
key_encryptsession(char *remotename, des_block *deskey, cred_t *cr)
{
	cryptkeyarg arg;
	cryptkeyres res;
	enum clnt_stat stat;

	RPCLOG(8, "key_encryptsession(%s, ", remotename);
	RPCLOG(8, "%x", *(uint32_t *)deskey);
	RPCLOG(8, "%x)\n", *(((uint32_t *)(deskey))+1));

	arg.remotename = remotename;
	arg.deskey = *deskey;
	if ((stat = key_call(KEY_ENCRYPT, xdr_cryptkeyarg, (char *)&arg,
	    xdr_cryptkeyres, (char *)&res, cr)) != RPC_SUCCESS) {
		RPCLOG(1, "key_encryptsession(%d, ", (int)crgetuid(cr));
		RPCLOG(1, "%s): ", remotename);
		RPCLOG(1, "rpc status %d ", stat);
		RPCLOG(1, "(%s)\n", clnt_sperrno(stat));
		return (stat);
	}

	if (res.status != KEY_SUCCESS) {
		RPCLOG(1, "key_encryptsession(%d, ", (int)crgetuid(cr));
		RPCLOG(1, "%s): ", remotename);
		RPCLOG(1, "key status %d\n", res.status);
		return (RPC_FAILED);	/* XXX */
	}
	*deskey = res.cryptkeyres_u.deskey;
	return (RPC_SUCCESS);
}

enum clnt_stat
key_decryptsession(char *remotename, des_block *deskey)
{
	cryptkeyarg arg;
	cryptkeyres res;
	enum clnt_stat stat;

	RPCLOG(8, "key_decryptsession(%s, ", remotename);
	RPCLOG(2, "%x", *(uint32_t *)deskey);
	RPCLOG(2, "%x)\n", *(((uint32_t *)(deskey))+1));

	arg.remotename = remotename;
	arg.deskey = *deskey;
	if ((stat = key_call(KEY_DECRYPT, xdr_cryptkeyarg, (char *)&arg,
	    xdr_cryptkeyres, (char *)&res, kcred)) != RPC_SUCCESS) {
		RPCLOG(1, "key_decryptsession(%s): ", remotename);
		RPCLOG(1, "rpc status %d ", stat);
		RPCLOG(1, "(%s)\n", clnt_sperrno(stat));
		return (stat);
	}

	if (res.status != KEY_SUCCESS) {
		RPCLOG(1, "key_decryptsession(%s): ", remotename);
		RPCLOG(1, "key status %d\n", res.status);
		return (RPC_FAILED);	/* XXX */
	}
	*deskey = res.cryptkeyres_u.deskey;
	return (RPC_SUCCESS);
}

enum clnt_stat
key_gendes(des_block *key)
{

	return (key_call(KEY_GEN, xdr_void, NULL, xdr_des_block, (char *)key,
	    CRED()));
}

/*
 *  Call up to keyserv to get the netname of the client based
 *  on its uid.  The netname is written into the string that "netname"
 *  points to; the caller is responsible for ensuring that sufficient space
 *  is available.
 */
enum clnt_stat
key_getnetname(netname, cr)
	char *netname;
	cred_t *cr;
{
	key_netstres kres;
	enum clnt_stat stat;

	/*
	 * Look up the keyserv interface routines to see if
	 * netname is stored there.
	 */
	kres.key_netstres_u.knet.st_netname = netname;
	if ((stat = key_call((rpcproc_t)KEY_NET_GET, xdr_void, NULL,
	    xdr_key_netstres, (char *)&kres, cr)) != RPC_SUCCESS) {
		RPCLOG(1, "key_getnetname(%d): ", (int)crgetuid(cr));
		RPCLOG(1, "rpc status %d ", stat);
		RPCLOG(1, "(%s)\n", clnt_sperrno(stat));
		return (stat);
	}

	if (kres.status != KEY_SUCCESS) {
		RPCLOG(1, "key_getnetname(%d): ", (int)crgetuid(cr));
		RPCLOG(1, "key status %d\n", kres.status);
		return (RPC_FAILED);
	}

	return (RPC_SUCCESS);
}

enum clnt_stat
knetname2user(char *name, uid_t *uid, gid_t *gid, credgrp_t **cgrpp)
{
	struct getcredres3 res;
	enum clnt_stat stat;
	int len;

	res.getcredres3_u.cred.gids.gids_val = NULL;
	if ((stat = key_call(KEY_GETCRED_3, xdr_netnamestr, (char *)&name,
	    xdr_getcredres3, (char *)&res, CRED())) != RPC_SUCCESS) {
		RPCLOG(1, "knetname2user(%s): ", name);
		RPCLOG(1, "rpc status %d ", stat);
		RPCLOG(1, "(%s)\n", clnt_sperrno(stat));
		return (stat);
	}

	if (res.status != KEY_SUCCESS) {
		RPCLOG(1, "knetname2user(%s): ", name);
		RPCLOG(1, "key status %d\n", res.status);
		return (RPC_FAILED);	/* XXX */
	}
	*uid = res.getcredres3_u.cred.uid;
	*gid = res.getcredres3_u.cred.gid;

	len = res.getcredres3_u.cred.gids.gids_len;
	if (len > 0) {
		gid_t *grps = res.getcredres3_u.cred.gids.gids_val;
		credgrp_t *cgrp = crgrpalloc(len);

		bcopy(grps, crgrpgetgroups(cgrp), sizeof (gid_t) * len);
		crgrpsort(cgrp);

		kmem_free(grps, sizeof (gid_t) * len);

		*cgrpp = cgrp;
	} else {
		*cgrpp = NULL;
	}

	return (RPC_SUCCESS);
}

#define	NC_LOOPBACK		"loopback"	/* XXX */
char loopback_name[] = NC_LOOPBACK;

static enum clnt_stat
key_call(rpcproc_t procn, xdrproc_t xdr_args, caddr_t args,
	xdrproc_t xdr_rslt, caddr_t rslt, cred_t *cr)
{
	struct netbuf netaddr;
	CLIENT *client;
	enum clnt_stat stat;
	vnode_t *vp;
	int error;
	struct auth_globals *authg;
	char *keyname;
	struct knetconfig *configp;
	k_sigset_t smask;

	authg = zone_getspecific(auth_zone_key, curproc->p_zone);
	keyname = authg->auth_keyname;
	configp = &authg->auth_config;

	/*
	 * Using a global here is obviously busted and fraught with danger.
	 */
	(void) strcpy(keyname, uts_nodename());
	netaddr.len = strlen(keyname);
	(void) strcpy(&keyname[netaddr.len], ".keyserv");

	netaddr.buf = keyname;
	/*
	 * 8 = strlen(".keyserv");
	 */
	netaddr.len = netaddr.maxlen = netaddr.len + 8;

	/*
	 * filch a knetconfig structure.
	 */
	if (configp->knc_rdev == 0) {
		if ((error = lookupname("/dev/ticlts", UIO_SYSSPACE,
		    FOLLOW, NULLVPP, &vp)) != 0) {
			RPCLOG(1, "key_call: lookupname: %d\n", error);
			return (RPC_UNKNOWNPROTO);
		}
		configp->knc_rdev = vp->v_rdev;
		configp->knc_protofmly = loopback_name;
		VN_RELE(vp);
	}
	configp->knc_semantics = NC_TPI_CLTS;
	RPCLOG(8, "key_call: procn %d, ", procn);
	RPCLOG(8, "rdev %lx, ", configp->knc_rdev);
	RPCLOG(8, "len %d, ", netaddr.len);
	RPCLOG(8, "maxlen %d, ", netaddr.maxlen);
	RPCLOG(8, "name %p\n", (void *)netaddr.buf);

	/*
	 * now call the proper stuff.
	 */
	error = clnt_tli_kcreate(configp, &netaddr, KEY_PROG, KEY_VERS,
	    0, KEY_NRETRY, cr, &client);

	if (error != 0) {
		RPCLOG(1, "key_call: clnt_tli_kcreate: error %d\n", error);
		switch (error) {
		case EINTR:
			return (RPC_INTR);
		case ETIMEDOUT:
			return (RPC_TIMEDOUT);
		default:
			return (RPC_FAILED);	/* XXX */
		}
	}

	/* Mask out all signals except SIGHUP, SIGQUIT, and SIGTERM. */
	sigintr(&smask, 0);
	stat = clnt_call(client, procn, xdr_args, args, xdr_rslt, rslt,
	    keytrytimeout);
	sigunintr(&smask);

	auth_destroy(client->cl_auth);
	clnt_destroy(client);
	if (stat != RPC_SUCCESS) {
		RPCLOG(1, "key_call: keyserver clnt_call failed: stat %d ",
		    stat);
		RPCLOG(1, "(%s)\n", clnt_sperrno(stat));
		RPCLOG0(1, "\n");
		return (stat);
	}
	RPCLOG(8, "key call: (%d) ok\n", procn);
	return (RPC_SUCCESS);
}
