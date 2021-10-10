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
 * Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Create a new VC given a list of addresses.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <unistd.h>
#include <netdb.h>
#include <libintl.h>
#include <xti.h>
#include <assert.h>

#include <sys/types.h>
#include <sys/time.h>
#include <sys/byteorder.h>
#include <sys/socket.h>
#include <sys/fcntl.h>

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include <smb/smb.h>
#include <netsmb/smb_lib.h>
#include <netsmb/netbios.h>
#include <netsmb/nb_lib.h>
#include <netsmb/smb_dev.h>

#include "charsets.h"
#include "private.h"

/*
 * Ask the IOD to create a VC with this IP address.
 */
static int
newvc(struct smb_ctx *ctx, struct addrinfo *ai)
{
	smbioc_ossn_t *ssn = &ctx->ct_ssn;

	/*
	 * Copy the passed address into ssn_srvaddr,
	 * but first sanity-check lengths.  Also,
	 * zero it first to avoid trailing junk.
	 */
	if (ai->ai_addrlen > sizeof (ssn->ssn_srvaddr))
		return (EINVAL);
	bzero(&ssn->ssn_srvaddr, sizeof (ssn->ssn_srvaddr));
	bcopy(ai->ai_addr, &ssn->ssn_srvaddr, ai->ai_addrlen);

	return (smb_iod_cl_newvc(ctx));
}

/*
 * Setup a new VC via the IOD.
 * Similar to findvc.c
 */
int
smb_ctx_newvc(struct smb_ctx *ctx)
{
	struct addrinfo *ai;
	int err;

	/* Should already have the address list. */
	if ((ctx->ct_flags & SMBCF_RESOLVED) == 0)
		return (EINVAL);

	/*
	 * Get a door handle to the smbiod if we
	 * don't have one already.  This also
	 * starts the smbiod if necessary.
	 */
	if (ctx->ct_door_fd < 0) {
		err = smb_iod_start(ctx);
		if (err != 0)
			return (err);
	}

	err = EPROTONOSUPPORT;  /* in case no AF match */
	for (ai = ctx->ct_addrinfo; ai; ai = ai->ai_next) {

		switch (ai->ai_family) {

		case AF_INET:
		case AF_INET6:
		case AF_NETBIOS:
			err = newvc(ctx, ai);
			if (err == 0)
				goto OK;
			break;

		default:
			DPRINT("skipped family %d", ai->ai_family);
			break;
		}
	}

	/*
	 * In the error case, the caller may try again
	 * with new auth. info, so keep the door open.
	 * Error return will close in smb_ctx_done.
	 */
	return (err);

OK:
	/* Done with the door handle. */
	close(ctx->ct_door_fd);
	ctx->ct_door_fd = -1;
	return (0);
}
