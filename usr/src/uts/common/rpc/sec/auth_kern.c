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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*	Copyright (c) 1983, 1984, 1985, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*
 * Portions of this source code were derived from Berkeley 4.3 BSD
 * under license from the Regents of the University of California.
 */

/*
 * auth_kern.c, implements UNIX style authentication parameters in the kernel.
 * Interfaces with svc_auth_unix on the server.  See auth_unix.c for the user
 * level implementation of unix auth.
 *
 */

#include <sys/param.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/cred.h>
#include <sys/kmem.h>
#include <sys/sysmacros.h>
#include <sys/cmn_err.h>
#include <sys/utsname.h>

#include <rpc/types.h>
#include <rpc/xdr.h>
#include <rpc/auth.h>
#include <rpc/auth_unix.h>
#include <rpc/clnt.h>
#include <rpc/rpc_msg.h>

/*
 * Unix authenticator operations vector
 */
static void	authkern_nextverf(AUTH *);
static bool_t	authkern_marshal(AUTH *, XDR *, struct cred *);
static bool_t	authkern_validate(AUTH *, struct opaque_auth *);
static bool_t	authkern_refresh(AUTH *, struct rpc_msg *, cred_t *);
static void	authkern_destroy(AUTH *);

static struct auth_ops auth_kern_ops = {
	authkern_nextverf,
	authkern_marshal,
	authkern_validate,
	authkern_refresh,
	authkern_destroy,
	authany_wrap,
	authany_unwrap
};

/*
 * Create a kernel unix style authenticator.
 * Returns an auth handle.
 */
AUTH *
authkern_create(void)
{
	/*
	 * Allocate and set up auth handle
	 */
	return (kmem_cache_alloc(authkern_cache, KM_SLEEP));
}

/*
 *  The constructor of the authkern_cache.
 */
/* ARGSUSED */
int
authkern_init(void *buf, void *cdrarg, int kmflags)
{
	AUTH *auth = (AUTH *)buf;

	auth->ah_ops = &auth_kern_ops;
	auth->ah_cred.oa_flavor = AUTH_UNIX;
	auth->ah_verf = _null_auth;

	return (0);
}

/*
 * authkern operations
 */
/* ARGSUSED */
static void
authkern_nextverf(AUTH *auth)
{
	/* no action necessary */
}

static bool_t
authkern_marshal(AUTH *auth, XDR *xdrs, struct cred *cr)
{
	char *sercred;
	XDR xdrm;
	struct opaque_auth *cred;
	bool_t ret = FALSE;
	const gid_t *gp, *gpend;
	int gidlen, credsize, namelen, rounded_namelen;
	int32_t *ptr;
	char *nodename = uts_nodename();

	/*
	 * First we try a fast path to get through
	 * this very common operation.
	 */
	gp = crgetgroups(cr);
	gidlen = crgetngroups(cr);
	if (gidlen > NGRPS)
		gidlen = NGRPS;
	gpend = &gp[gidlen-1];

	namelen = (int)strlen(nodename);
	rounded_namelen = RNDUP(namelen);
	credsize = 4 + 4 + rounded_namelen + 4 + 4 + 4 + gidlen * 4;
	ptr = XDR_INLINE(xdrs, 4 + 4 + credsize + 4 + 4);
	if (ptr) {
		/*
		 * We can do the fast path.
		 */
		IXDR_PUT_INT32(ptr, AUTH_UNIX);	/* cred flavor */
		IXDR_PUT_INT32(ptr, credsize);	/* cred len */
		IXDR_PUT_INT32(ptr, gethrestime_sec());
		IXDR_PUT_INT32(ptr, namelen);
		bcopy(nodename, (caddr_t)ptr, namelen);
		if (rounded_namelen - namelen)
			bzero(((caddr_t)ptr) + namelen,
			    rounded_namelen - namelen);
		ptr += rounded_namelen / BYTES_PER_XDR_UNIT;
		IXDR_PUT_INT32(ptr, crgetuid(cr));
		IXDR_PUT_INT32(ptr, crgetgid(cr));
		IXDR_PUT_INT32(ptr, gidlen);
		while (gp <= gpend) {
			IXDR_PUT_INT32(ptr, *gp++);
		}
		IXDR_PUT_INT32(ptr, AUTH_NULL);	/* verf flavor */
		IXDR_PUT_INT32(ptr, 0);	/* verf len */
		return (TRUE);
	}
	sercred = kmem_alloc(MAX_AUTH_BYTES, KM_SLEEP);
	/*
	 * serialize u struct stuff into sercred
	 */
	xdrmem_create(&xdrm, sercred, MAX_AUTH_BYTES, XDR_ENCODE);
	if (!xdr_authkern(&xdrm)) {
		printf("authkern_marshal: xdr_authkern failed\n");
		ret = FALSE;
		goto done;
	}

	/*
	 * Make opaque auth credentials that point at serialized u struct
	 */
	cred = &(auth->ah_cred);
	cred->oa_length = XDR_GETPOS(&xdrm);
	cred->oa_base = sercred;

	/*
	 * serialize credentials and verifiers (null)
	 */
	if ((xdr_opaque_auth(xdrs, &(auth->ah_cred))) &&
	    (xdr_opaque_auth(xdrs, &(auth->ah_verf))))
		ret = TRUE;
	else
		ret = FALSE;
done:
	kmem_free(sercred, MAX_AUTH_BYTES);
	return (ret);
}

/* ARGSUSED */
static bool_t
authkern_validate(AUTH *auth, struct opaque_auth *verf)
{
	return (TRUE);
}

/* ARGSUSED */
static bool_t
authkern_refresh(AUTH *auth, struct rpc_msg *msg, cred_t *cr)
{
	return (FALSE);
}

static void
authkern_destroy(AUTH *auth)
{
	kmem_cache_free(authkern_cache, auth);
}
