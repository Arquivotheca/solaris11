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
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <strings.h>
#include <smbsrv/libsmb.h>
#include <smbsrv/ndl/srvsvc.ndl>
#include <smbsrv/libntsvcs.h>

int
wkssvc_getinfo(char *server, char *domain, wksta_info_t *info)
{
	struct mslm_NetWkstaGetInfo	arg;
	struct mslm_WKSTA_INFO_100	*info100;
	mlsvc_handle_t			handle;
	char				user[SMB_USERNAME_MAXLEN];
	int				opnum;
	int				rc;

	if (server == NULL || domain == NULL || info == NULL)
		return (-1);

	smb_ipc_get_user(user, SMB_USERNAME_MAXLEN);

	if (ndr_rpc_bind(&handle, server, domain, user, "Workstation") != 0)
		return (-1);

	opnum = WKSSVC_OPNUM_NetWkstaGetInfo;
	bzero(&arg, sizeof (struct mslm_NetWkstaGetInfo));
	arg.level = 100;

	rc = ndr_rpc_call(&handle, opnum, &arg);

	/* LINTED E_BAD_PTR_CAST_ALIGN */
	info100 = (struct mslm_WKSTA_INFO_100 *)arg.result.bufptr.nullptr;

	if (rc != 0 || arg.status != 0 || info100 == NULL) {
		ndr_rpc_unbind(&handle);
		return (-1);
	}

	info->wki_platform_id = info100->wki100_platform_id;
	info->wki_computername = strdup((char *)info100->wki100_computername);
	info->wki_domainname = strdup((char *)info100->wki100_langroup);
	info->wki_ver_major = info100->wki100_ver_major;
	info->wki_ver_minor = info100->wki100_ver_minor;

	ndr_rpc_unbind(&handle);

	if (info->wki_computername == NULL || info->wki_domainname == NULL) {
		wkssvc_freeinfo(info);
		return (-1);
	}

	return (0);
}

void
wkssvc_freeinfo(wksta_info_t *info)
{
	free(info->wki_computername);
	free(info->wki_domainname);
	bzero(info, sizeof (wksta_info_t));
}
