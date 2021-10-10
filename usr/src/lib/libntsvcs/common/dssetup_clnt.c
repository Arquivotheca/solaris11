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
 * Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Client side for the DSSETUP RPC service.
 */

#include <string.h>
#include <strings.h>
#include <smb/wintypes.h>
#include <smbsrv/libsmb.h>
#include <smbsrv/ndl/dssetup.ndl>
#include <smbsrv/libntsvcs.h>

int
dssetup_get_domain_info(ds_primary_domain_info_t *ds_info)
{
	dssetup_DsRoleGetPrimaryDomainInfo_t arg;
	struct dssetup_DsRolePrimaryDomInfo1 *info;
	smb_domainex_t di;
	mlsvc_handle_t handle;
	char user[SMB_USERNAME_MAXLEN];
	int opnum;
	int rc;

	if (!smb_domain_getinfo(&di))
		return (-1);

	errno = 0;
	rc = ndr_rpc_bind(&handle, di.d_dc, di.d_primary.di_nbname,
	    NULL, "DSSETUP");
	if (rc != 0) {
		switch (errno) {
		case ENETUNREACH:	/* 128 */
		case ENETRESET:		/* 129 */
		case ECONNABORTED:	/* 130 */
		case ECONNRESET:	/* 131 */
		case ETIMEDOUT:		/* 145 */
		case EHOSTDOWN:		/* 147 */
		case EHOSTUNREACH:	/* 148 */
			return (-1);
		}

		/*
		 * Retry with authenticated IPC connection only if the failure
		 * is not caused by network/connection issue.
		 */
		smb_ipc_get_user(user, SMB_USERNAME_MAXLEN);

		rc = ndr_rpc_bind(&handle, di.d_dc, di.d_primary.di_nbname,
		    user, "DSSETUP");
		if (rc != 0)
			return (-1);
	}

	opnum = DSSETUP_OPNUM_DsRoleGetPrimaryDomainInfo;
	bzero(&arg, sizeof (dssetup_DsRoleGetPrimaryDomainInfo_t));
	arg.level = DS_ROLE_BASIC_INFORMATION;

	rc = ndr_rpc_call(&handle, opnum, &arg);
	if ((rc != 0) || (arg.status != 0) || arg.info == NULL) {
		ndr_rpc_unbind(&handle);
		return (-1);
	}

	info = &arg.info->ru.info1;

	if (info->nt_domain == NULL ||
	    info->dns_domain == NULL ||
	    info->forest == NULL) {
		ndr_rpc_unbind(&handle);
		return (-1);
	}

	bcopy(info, ds_info, sizeof (ds_primary_domain_info_t));
	ds_info->nt_domain = (uint8_t *)strdup((char *)info->nt_domain);
	ds_info->dns_domain = (uint8_t *)strdup((char *)info->dns_domain);
	ds_info->forest = (uint8_t *)strdup((char *)info->forest);

	ndr_rpc_unbind(&handle);
	return (0);
}

/*
 * DsRoleGetPrimaryDomainInfo doesn't get the domain SID
 * but we can use an LSA query to get it.
 */
uint32_t
dssetup_query_domain_info(char *server, char *domain, smb_domain_t *dinfo)
{
	dssetup_DsRoleGetPrimaryDomainInfo_t arg;
	struct dssetup_DsRolePrimaryDomInfo1 *info1;
	mlsvc_handle_t	handle;
	char		guid_str[UUID_PRINTABLE_STRING_LENGTH];
	char		sid_str[SMB_SID_STRSZ];
	char		user[SMB_USERNAME_MAXLEN];
	int		opnum;
	int		rc;

	if (lsa_query_primary_domain_info(server, domain, dinfo) == 0)
		(void) strlcpy(sid_str, dinfo->di_sid, SMB_SID_STRSZ);
	else
		sid_str[0] = '\0';

	if (ndr_rpc_bind(&handle, server, domain, NULL, "DSSETUP") != 0) {
		smb_ipc_get_user(user, SMB_USERNAME_MAXLEN);

		rc = ndr_rpc_bind(&handle, server, domain, user, "DSSETUP");
		if (rc != 0)
			return (NT_STATUS_CANT_ACCESS_DOMAIN_INFO);
	}

	opnum = DSSETUP_OPNUM_DsRoleGetPrimaryDomainInfo;
	bzero(&arg, sizeof (dssetup_DsRoleGetPrimaryDomainInfo_t));
	arg.level = DS_ROLE_BASIC_INFORMATION;

	rc = ndr_rpc_call(&handle, opnum, &arg);
	if ((rc != 0) || (arg.status != 0) || arg.info == NULL) {
		ndr_rpc_unbind(&handle);
		return (NT_STATUS_CANT_ACCESS_DOMAIN_INFO);
	}

	info1 = &arg.info->ru.info1;

	if (info1->nt_domain == NULL ||
	    info1->dns_domain == NULL ||
	    info1->forest == NULL) {
		ndr_rpc_unbind(&handle);
		return (NT_STATUS_CANT_ACCESS_DOMAIN_INFO);
	}

	ndr_uuid_unparse((ndr_uuid_t *)&info1->domain_guid, guid_str);
	dinfo->di_type = SMB_DOMAIN_PRIMARY;

	smb_domain_set_dns_info(sid_str,
	    (char *)info1->nt_domain,
	    (char *)info1->dns_domain,
	    (char *)info1->forest,
	    guid_str, dinfo);

	ndr_rpc_unbind(&handle);
	return (NT_STATUS_SUCCESS);
}

int
dssetup_check_service(void)
{
	ds_primary_domain_info_t	ds_info;
	int				rc;

	bzero(&ds_info, sizeof (ds_primary_domain_info_t));

	if ((rc = dssetup_get_domain_info(&ds_info)) == 0) {
		free(ds_info.nt_domain);
		free(ds_info.dns_domain);
		free(ds_info.forest);
	}

	return (rc);
}
