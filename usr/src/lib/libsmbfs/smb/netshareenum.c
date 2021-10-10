/*
 * Copyright (c) 2004 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 *
 * "Portions Copyright (c) 1999 Apple Computer, Inc.  All Rights
 * Reserved.  This file contains Original Code and/or Modifications of
 * Original Code as defined in and that are subject to the Apple Public
 * Source License Version 1.0 (the 'License').  You may not use this file
 * except in compliance with the License.  Please obtain a copy of the
 * License at http://www.apple.com/publicsource and read it before using
 * this file.
 *
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE OR NON-INFRINGEMENT.  Please see the
 * License for the specific language governing rights and limitations
 * under the License."
 *
 * @APPLE_LICENSE_HEADER_END@
 */

/* BEGIN CSTYLED */
/*
 *      @(#)ui.c      *
 *      (c) 2004   Apple Computer, Inc.  All Rights Reserved
 *
 *
 *      netshareenum.c -- Routines for getting a list of share information
 *			  from a server.
 *
 *      MODIFICATION HISTORY:
 *       27-Nov-2004     Guy Harris	New today
 */
/* END CSTYLED */

/*
 * Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include <netsmb/mchain.h>
#include <smb/smb.h>
#include <netsmb/smb_lib.h>
#include <netsmb/smb_rap.h>
#include <netsmb/smb_netshareenum.h>
#include <smb/charsets.h>

/*
 * Enumerate shares using RAP
 */
struct smb_share_info_1 {
	char		shi1_netname[13];
	char		shi1_pad;
	uint16_t	shi1_type;
	uint32_t	shi1_remark;		/* char * */
};

static int
smb_rap_NetShareEnum(struct smb_ctx *ctx, int sLevel, void *pbBuffer,
	int *cbBuffer, int *pcEntriesRead, int *pcTotalAvail)
{
	struct smb_rap *rap;
	long lval = -1;
	int error;

	error = smb_rap_create(0, "WrLeh", "B13BWz", &rap);
	if (error)
		return (error);
	(void) smb_rap_setNparam(rap, sLevel);		/* W - sLevel */
	(void) smb_rap_setPparam(rap, pbBuffer);	/* r - pbBuffer */
	(void) smb_rap_setNparam(rap, *cbBuffer);	/* L - cbBuffer */
	error = smb_rap_request(rap, ctx);
	if (error == 0) {
		*pcEntriesRead = rap->r_entries;
		error = smb_rap_getNparam(rap, &lval);
		*pcTotalAvail = lval;
		/* Copy the data length into the IN/OUT variable. */
		*cbBuffer = rap->r_rcvbuflen;
	}
	error = smb_rap_error(rap, error);
	smb_rap_done(rap);
	return (error);
}

static int
rap_netshareenum(struct smb_ctx *ctx, int *entriesp, int *totalp,
    struct share_info **entries_listp)
{
	int error, bufsize, i, entries, total, nreturned;
	struct smb_share_info_1 *rpbuf, *ep;
	struct share_info *entry_list, *elp;
	char *cp;
	int lbound, rbound;

	bufsize = 0xffe0;	/* samba notes win2k bug for 65535 */
	rpbuf = malloc(bufsize);
	if (rpbuf == NULL)
		return (errno);

	error = smb_rap_NetShareEnum(ctx, 1, rpbuf, &bufsize, &entries, &total);
	if (error &&
	    error != (ERROR_MORE_DATA | SMB_RAP_ERROR)) {
		free(rpbuf);
		return (error);
	}
	entry_list = malloc(entries * sizeof (struct share_info));
	if (entry_list == NULL) {
		error = errno;
		free(rpbuf);
		return (error);
	}
	lbound = entries * (sizeof (struct smb_share_info_1));
	rbound = bufsize;
	for (ep = rpbuf, elp = entry_list, i = 0, nreturned = 0; i < entries;
	    i++, ep++) {
		elp->type = letohs(ep->shi1_type);
		ep->shi1_pad = '\0'; /* ensure null termination */
		elp->netname = convert_wincs_to_utf8(ep->shi1_netname);
		if (elp->netname == NULL)
			continue;	/* punt on this entry */
		/*
		 * Check for validity of offset.
		 */
		if (ep->shi1_remark >= lbound && ep->shi1_remark < rbound) {
			cp = (char *)rpbuf + ep->shi1_remark;
			elp->remark = convert_wincs_to_utf8(cp);
		} else
			elp->remark = NULL;
		elp++;
		nreturned++;
	}
	*entriesp = nreturned;
	*totalp = total;
	*entries_listp = entry_list;
	free(rpbuf);
	return (0);
}

/*
 * First we try the RPC-based NetrShareEnum, and, if that fails, we fall
 * back on the RAP-based NetShareEnum.
 */
int
smb_netshareenum(struct smb_ctx *ctx, int *entriesp, int *totalp,
    struct share_info **entry_listp)
{
	int error;

#ifdef NOTYETDEFINED
	/*
	 * Try getting a list of shares with the SRVSVC RPC service.
	 */
	error = rpc_netshareenum(ctx, entriesp, totalp, entry_listp);
	if (error == 0)
		return (0);
#endif

	/*
	 * OK, that didn't work - try RAP.
	 * XXX - do so only if it failed because we couldn't open
	 * the pipe?
	 */
	error = rap_netshareenum(ctx, entriesp, totalp, entry_listp);
	return (error);
}
