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
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * This module provides the high level interface to the LSA RPC functions.
 */

#include <strings.h>
#include <unistd.h>

#include <smbsrv/libsmb.h>
#include <smbsrv/libntsvcs.h>
#include <smbsrv/smbinfo.h>
#include <smbsrv/smb_token.h>

#include <lsalib.h>

static uint32_t lsa_lookup_name_builtin(char *, char *, smb_account_t *);
static uint32_t lsa_lookup_name_domain(char *, smb_account_t *);

static uint32_t lsa_lookup_sid_builtin(smb_sid_t *, smb_account_t *);
static uint32_t lsa_lookup_sid_domain(smb_sid_t *, smb_account_t *);

static int lsa_list_accounts(mlsvc_handle_t *);

/*
 * Lookup the given account and returns the account information
 * in the passed smb_account_t structure.
 *
 * The lookup is performed in the following order:
 *    well known accounts
 *    local accounts
 *    domain accounts
 *
 * If it's established the given account is well know or local
 * but the lookup fails for some reason, the next step(s) won't be
 * performed.
 *
 * If the name is a domain account, it may refer to a user, group or
 * alias. If it is a local account, its type should be specified
 * in the sid_type parameter. In case the account type is unknown
 * sid_type should be set to SidTypeUnknown.
 *
 * account argument could be either [domain\]name or [domain/]name.
 *
 * Return status:
 *
 *   NT_STATUS_SUCCESS		Account is successfully translated
 *   NT_STATUS_NONE_MAPPED	Couldn't translate the account
 */
uint32_t
lsa_lookup_name(char *account, uint16_t type, smb_account_t *info)
{
	char		domain_acct[MAXNAMELEN];
	char		nambuf[MAXNAMELEN];
	char		*domain = NULL;
	char		*name = NULL;
	uint32_t	status;

	(void) strlcpy(nambuf, account, MAXNAMELEN);
	(void) strsubst(nambuf, '/', '\\');
	(void) strcanon(nambuf, "\\");
	(void) strlcpy(domain_acct, nambuf, MAXNAMELEN);

	smb_name_parse(nambuf, &name, &domain);
	if (name == NULL)
		name = account;

	status = lsa_lookup_name_builtin(domain, name, info);
	if (status == NT_STATUS_NOT_FOUND) {
		status = smb_sam_lookup_name(domain, name, type, info);
		if (status == NT_STATUS_SUCCESS)
			return (status);

		if ((domain == NULL) || (status == NT_STATUS_NOT_FOUND))
			status = lsa_lookup_name_domain(domain_acct, info);
	}

	return ((status == NT_STATUS_SUCCESS) ? status : NT_STATUS_NONE_MAPPED);
}

uint32_t
lsa_lookup_sid(smb_sid_t *sid, smb_account_t *info)
{
	uint32_t status;

	if (!smb_sid_isvalid(sid))
		return (NT_STATUS_INVALID_SID);

	status = lsa_lookup_sid_builtin(sid, info);
	if (status == NT_STATUS_NOT_FOUND) {
		status = smb_sam_lookup_sid(sid, info);
		if (status == NT_STATUS_NOT_FOUND)
			status = lsa_lookup_sid_domain(sid, info);
	}

	return ((status == NT_STATUS_SUCCESS) ? status : NT_STATUS_NONE_MAPPED);
}

/*
 * Obtains the primary domain SID and name from the specified server
 * (domain controller).
 *
 * The requested information will be returned via 'info' argument.
 *
 * Returns NT status codes.
 */
DWORD
lsa_query_primary_domain_info(char *server, char *domain,
    smb_domain_t *info)
{
	mlsvc_handle_t domain_handle;
	DWORD status;
	char user[SMB_USERNAME_MAXLEN];

	smb_ipc_get_user(user, SMB_USERNAME_MAXLEN);

	if ((lsar_open(server, domain, user, &domain_handle)) != 0)
		return (NT_STATUS_CANT_ACCESS_DOMAIN_INFO);

	status = lsar_query_info_policy(&domain_handle,
	    MSLSA_POLICY_PRIMARY_DOMAIN_INFO, info);

	(void) lsar_close(&domain_handle);
	return (status);
}

/*
 * Obtains the account domain SID and name from the current server
 * (domain controller).
 *
 * The requested information will be returned via 'info' argument.
 *
 * Returns NT status codes.
 */
DWORD
lsa_query_account_domain_info(char *server, char *domain,
    smb_domain_t *info)
{
	mlsvc_handle_t domain_handle;
	DWORD status;
	char user[SMB_USERNAME_MAXLEN];

	smb_ipc_get_user(user, SMB_USERNAME_MAXLEN);

	if ((lsar_open(server, domain, user, &domain_handle)) != 0)
		return (NT_STATUS_CANT_ACCESS_DOMAIN_INFO);

	status = lsar_query_info_policy(&domain_handle,
	    MSLSA_POLICY_ACCOUNT_DOMAIN_INFO, info);

	(void) lsar_close(&domain_handle);
	return (status);
}

/*
 * lsa_query_dns_domain_info
 *
 * Obtains the DNS domain info from the specified server
 * (domain controller).
 *
 * The requested information will be returned via 'info' argument.
 *
 * Returns NT status codes.
 */
DWORD
lsa_query_dns_domain_info(char *server, char *domain, smb_domain_t *info)
{
	mlsvc_handle_t domain_handle;
	DWORD status;
	char user[SMB_USERNAME_MAXLEN];

	smb_ipc_get_user(user, SMB_USERNAME_MAXLEN);

	if ((lsar_open(server, domain, user, &domain_handle)) != 0)
		return (NT_STATUS_CANT_ACCESS_DOMAIN_INFO);

	status = lsar_query_info_policy(&domain_handle,
	    MSLSA_POLICY_DNS_DOMAIN_INFO, info);

	(void) lsar_close(&domain_handle);
	return (status);
}

/*
 * Enumerate the trusted domains of the primary domain.
 *
 * Try the extended enumaration call, which returns the FQDN and
 * trust information in addition to the trusted domain's NetBIOS
 * name and SID.
 *
 * If the extended call fails, we fall back to the original enum
 * call.
 *
 * The requested information will be returned via the info pointer.
 *
 * Returns NT status codes.
 * STATUS_NO_MORE_ENTRIES indicates that we have all of the available
 * information, which we can translate to NT_STATUS_SUCCESS.
 */
DWORD
lsa_enum_trusted_domains(char *server, char *domain,
    smb_trusted_domains_t *info)
{
	mlsvc_handle_t domain_handle;
	DWORD enum_context;
	DWORD status;
	char user[SMB_USERNAME_MAXLEN];

	smb_ipc_get_user(user, SMB_USERNAME_MAXLEN);

	if ((lsar_open(server, domain, user, &domain_handle)) != 0)
		return (NT_STATUS_CANT_ACCESS_DOMAIN_INFO);

	enum_context = 0;

	status = lsar_enum_trusted_domains_ex(&domain_handle, &enum_context,
	    info);
	if (status == NT_STATUS_NO_MORE_ENTRIES)
		status = NT_STATUS_SUCCESS;

	if (status != NT_STATUS_SUCCESS) {
		enum_context = 0;

		status = lsar_enum_trusted_domains(&domain_handle,
		    &enum_context, info);

		if (status == NT_STATUS_NO_MORE_ENTRIES)
			status = NT_STATUS_SUCCESS;
	}

	(void) lsar_close(&domain_handle);
	return (status);
}

/*
 * Lookup well known accounts table
 *
 * Return status:
 *
 *   NT_STATUS_SUCCESS		Account is translated successfully
 *   NT_STATUS_NOT_FOUND	This is not a well known account
 *   NT_STATUS_NONE_MAPPED	Account is found but domains don't match
 *   NT_STATUS_NO_MEMORY	Memory shortage
 *   NT_STATUS_INTERNAL_ERROR	Internal error/unexpected failure
 */
static uint32_t
lsa_lookup_name_builtin(char *domain, char *name, smb_account_t *info)
{
	smb_wka_t *wka;
	char *wkadom;

	bzero(info, sizeof (smb_account_t));

	if ((wka = smb_wka_lookup_name(name)) == NULL)
		return (NT_STATUS_NOT_FOUND);

	if ((wkadom = smb_wka_get_domain(wka->wka_domidx)) == NULL)
		return (NT_STATUS_INTERNAL_ERROR);

	if ((domain != NULL) && (smb_strcasecmp(domain, wkadom, 0) != 0))
		return (NT_STATUS_NONE_MAPPED);

	info->a_name = strdup(name);
	info->a_sid = smb_sid_dup(wka->wka_binsid);
	info->a_domain = strdup(wkadom);
	info->a_domsid = smb_sid_split(wka->wka_binsid, &info->a_rid);
	info->a_type = wka->wka_type;

	if (!smb_account_validate(info)) {
		smb_account_free(info);
		return (NT_STATUS_NO_MEMORY);
	}

	return (NT_STATUS_SUCCESS);
}

/*
 * Lookup the given account in domain.
 *
 * The information is returned in the user_info structure.
 * The caller is responsible for allocating and releasing
 * this structure.
 */
static uint32_t
lsa_lookup_name_domain(char *account_name, smb_account_t *info)
{
	mlsvc_handle_t domain_handle;
	smb_domainex_t dinfo;
	uint32_t status;
	char user[SMB_USERNAME_MAXLEN];

	smb_ipc_get_user(user, SMB_USERNAME_MAXLEN);

	if (!smb_domain_getinfo(&dinfo))
		return (NT_STATUS_CANT_ACCESS_DOMAIN_INFO);

	if (lsar_open(dinfo.d_dc, dinfo.d_primary.di_nbname, user,
	    &domain_handle) != 0)
		return (NT_STATUS_INVALID_PARAMETER);

	status = lsar_lookup_names(&domain_handle, account_name, info);

	(void) lsar_close(&domain_handle);
	return (status);
}

/*
 * lsa_lookup_privs
 *
 * Request the privileges associated with the specified account. In
 * order to get the privileges, we first have to lookup the name on
 * the specified domain controller and obtain the appropriate SID.
 * The SID can then be used to open the account and obtain the
 * account privileges. The results from both the name lookup and the
 * privileges are returned in the user_info structure. The caller is
 * responsible for allocating and releasing this structure.
 *
 * On success 0 is returned. Otherwise a -ve error code.
 */
/*ARGSUSED*/
int
lsa_lookup_privs(char *account_name, char *target_name, smb_account_t *ainfo)
{
	mlsvc_handle_t domain_handle;
	int rc;
	smb_domainex_t dinfo;
	char user[SMB_USERNAME_MAXLEN];

	smb_ipc_get_user(user, SMB_USERNAME_MAXLEN);

	if (!smb_domain_getinfo(&dinfo))
		return (-1);

	if ((lsar_open(dinfo.d_dc, dinfo.d_primary.di_nbname, user,
	    &domain_handle)) != 0)
		return (-1);

	rc = lsa_list_accounts(&domain_handle);
	(void) lsar_close(&domain_handle);
	return (rc);
}

/*
 * lsa_list_privs
 *
 * List the privileges supported by the specified server.
 * This function is only intended for diagnostics.
 *
 * Returns NT status codes.
 */
DWORD
lsa_list_privs(char *server, char *domain)
{
	static char name[128];
	static struct ms_luid luid;
	mlsvc_handle_t domain_handle;
	int rc;
	int i;
	char user[SMB_USERNAME_MAXLEN];

	smb_ipc_get_user(user, SMB_USERNAME_MAXLEN);

	rc = lsar_open(server, domain, user, &domain_handle);
	if (rc != 0)
		return (NT_STATUS_INVALID_PARAMETER);

	for (i = 0; i < 30; ++i) {
		luid.low_part = i;
		rc = lsar_lookup_priv_name(&domain_handle, &luid, name, 128);
		if (rc != 0)
			continue;

		(void) lsar_lookup_priv_value(&domain_handle, name, &luid);
		(void) lsar_lookup_priv_display_name(&domain_handle, name,
		    name, 128);
	}

	(void) lsar_close(&domain_handle);
	return (NT_STATUS_SUCCESS);
}

/*
 * lsa_list_accounts
 *
 * This function can be used to list the accounts in the specified
 * domain. For now the SIDs are just listed in the system log.
 *
 * On success 0 is returned. Otherwise a -ve error code.
 */
static int
lsa_list_accounts(mlsvc_handle_t *domain_handle)
{
	mlsvc_handle_t account_handle;
	struct mslsa_EnumAccountBuf accounts;
	struct mslsa_sid *sid;
	smb_account_t ainfo;
	DWORD enum_context = 0;
	int rc;
	int i;

	bzero(&accounts, sizeof (struct mslsa_EnumAccountBuf));

	do {
		rc = lsar_enum_accounts(domain_handle, &enum_context,
		    &accounts);
		if (rc != 0)
			return (rc);

		for (i = 0; i < accounts.entries_read; ++i) {
			sid = accounts.info[i].sid;

			if (lsar_open_account(domain_handle, sid,
			    &account_handle) == 0) {
				(void) lsar_enum_privs_account(&account_handle,
				    &ainfo);
				(void) lsar_close(&account_handle);
			}

			free(accounts.info[i].sid);
		}

		if (accounts.info)
			free(accounts.info);
	} while (rc == 0 && accounts.entries_read != 0);

	return (0);
}

/*
 * Lookup well known accounts table for the given SID
 *
 * Return status:
 *
 *   NT_STATUS_SUCCESS		Account is translated successfully
 *   NT_STATUS_NOT_FOUND	This is not a well known account
 *   NT_STATUS_NO_MEMORY	Memory shortage
 *   NT_STATUS_INTERNAL_ERROR	Internal error/unexpected failure
 */
static uint32_t
lsa_lookup_sid_builtin(smb_sid_t *sid, smb_account_t *ainfo)
{
	smb_wka_t *wka;
	char *wkadom;

	bzero(ainfo, sizeof (smb_account_t));

	if ((wka = smb_wka_lookup_sid(sid)) == NULL)
		return (NT_STATUS_NOT_FOUND);

	if ((wkadom = smb_wka_get_domain(wka->wka_domidx)) == NULL)
		return (NT_STATUS_INTERNAL_ERROR);

	ainfo->a_name = strdup(wka->wka_name);
	ainfo->a_sid = smb_sid_dup(wka->wka_binsid);
	ainfo->a_domain = strdup(wkadom);
	ainfo->a_domsid = smb_sid_split(ainfo->a_sid, &ainfo->a_rid);
	ainfo->a_type = wka->wka_type;

	if (!smb_account_validate(ainfo)) {
		smb_account_free(ainfo);
		return (NT_STATUS_NO_MEMORY);
	}

	return (NT_STATUS_SUCCESS);
}

static uint32_t
lsa_lookup_sid_domain(smb_sid_t *sid, smb_account_t *ainfo)
{
	mlsvc_handle_t domain_handle;
	uint32_t status;
	smb_domainex_t dinfo;
	char user[SMB_USERNAME_MAXLEN];

	smb_ipc_get_user(user, SMB_USERNAME_MAXLEN);

	if (!smb_domain_getinfo(&dinfo))
		return (NT_STATUS_CANT_ACCESS_DOMAIN_INFO);

	if (lsar_open(dinfo.d_dc, dinfo.d_primary.di_nbname, user,
	    &domain_handle) != 0)
		return (NT_STATUS_INVALID_PARAMETER);

	status = lsar_lookup_sids(&domain_handle, sid, ainfo);

	(void) lsar_close(&domain_handle);
	return (status);
}
