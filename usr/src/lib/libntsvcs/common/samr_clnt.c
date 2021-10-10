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
 * Security Accounts Manager RPC (SAMR) client-side interface.
 *
 * The SAM is a hierarchical database:
 * - If you want to talk to the SAM you need a SAM handle.
 * - If you want to work with a domain, use the SAM handle.
 *   to obtain a domain handle.
 * - Use domain handles to obtain user handles etc.
 *
 * Be careful about returning null handles to the application.  Use of a
 * null handle may crash the domain controller if you attempt to use it.
 */

#include <stdio.h>
#include <strings.h>
#include <stdlib.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/param.h>

#include <smbsrv/libsmb.h>
#include <smbsrv/libntsvcs.h>
#include <smbsrv/smbinfo.h>
#include <smb/smb.h>
#include <smb/smb_sid.h>
#include <samlib.h>

/*LINTED E_STATIC_UNUSED*/
static DWORD samr_connect1(char *, char *, char *, DWORD, mlsvc_handle_t *);
static DWORD samr_connect2(char *, char *, char *, DWORD, mlsvc_handle_t *);
static DWORD samr_connect4(char *, char *, char *, DWORD, mlsvc_handle_t *);
static DWORD samr_connect5(char *, char *, char *, DWORD, mlsvc_handle_t *);

typedef DWORD (*samr_connop_t)(char *, char *, char *, DWORD,
    mlsvc_handle_t *);

static int samr_setup_user_info(WORD, struct samr_QueryUserInfo *,
    union samr_user_info *);
static void samr_set_user_unknowns(struct samr_SetUserInfo23 *);
static void samr_set_user_logon_hours(struct samr_SetUserInfo *);
static int samr_set_user_password(unsigned char *, BYTE *);

/*
 * samr_open
 *
 * Wrapper round samr_connect to ensure that we connect using the server
 * and domain.  We default to the resource domain if the caller doesn't
 * supply a server name and a domain name.
 *
 * If username argument is NULL, an anonymous connection will be established.
 * Otherwise, an authenticated connection will be established.
 *
 * On success 0 is returned. Otherwise a -ve error code.
 */
int
samr_open(char *server, char *domain, char *username, DWORD access_mask,
    mlsvc_handle_t *samr_handle)
{
	smb_domainex_t di;
	int rc;

	if (server == NULL || domain == NULL) {
		if (!smb_domain_getinfo(&di))
			return (-1);

		server = di.d_dc;
		domain = di.d_primary.di_nbname;
	}

	rc = samr_connect(server, domain, username, access_mask, samr_handle);
	return (rc);
}


/*
 * samr_connect
 *
 * Connect to the SAMR service on the specified server (domain controller).
 * New SAM connect calls have been added to Windows over time:
 *
 *	Windows NT3.x:	SamrConnect
 *	Windows NT4.0:	SamrConnect2
 *	Windows 2000:	SamrConnect4
 *	Windows XP:	SamrConnect5
 *
 * Try the calls from most recent to oldest until the server responds with
 * something other than an RPC protocol error.  We don't use the original
 * connect call because all supported servers should support SamrConnect2.
 */
int
samr_connect(char *server, char *domain, char *username, DWORD access_mask,
    mlsvc_handle_t *samr_handle)
{
	static samr_connop_t samr_connop[] = {
		samr_connect5,
		samr_connect4,
		samr_connect2
	};

	int	n_op = (sizeof (samr_connop) / sizeof (samr_connop[0]));
	DWORD	status;
	int	i;

	if (ndr_rpc_bind(samr_handle, server, domain, username, "SAMR") < 0)
		return (-1);

	for (i = 0; i < n_op; ++i) {
		status = (*samr_connop[i])(server, domain, username,
		    access_mask, samr_handle);

		if (status == NT_STATUS_SUCCESS)
			return (0);
	}

	ndr_rpc_unbind(samr_handle);
	return (-1);
}

/*
 * samr_connect1
 *
 * Original SAMR connect call; probably used on Windows NT 3.51.
 * Windows 95 uses this call with the srvmgr tools update.
 * Servername appears to be a dword rather than a string.
 * The first word contains '\' and the second word contains 0x001,
 * (which is probably uninitialized junk: 0x0001005c.
 */
/*ARGSUSED*/
static DWORD
samr_connect1(char *server, char *domain, char *username, DWORD access_mask,
    mlsvc_handle_t *samr_handle)
{
	struct samr_Connect arg;
	int opnum;
	DWORD status;

	bzero(&arg, sizeof (struct samr_Connect));
	opnum = SAMR_OPNUM_Connect;
	status = NT_STATUS_SUCCESS;

	arg.servername = ndr_rpc_malloc(samr_handle, sizeof (DWORD));
	*(arg.servername) = 0x0001005c;
	arg.access_mask = access_mask;

	if (ndr_rpc_call(samr_handle, opnum, &arg) != 0) {
		status = NT_STATUS_UNSUCCESSFUL;
	} else if (arg.status != 0) {
		status = NT_SC_VALUE(arg.status);
	} else {
		(void) memcpy(&samr_handle->handle, &arg.handle,
		    sizeof (ndr_hdid_t));

		if (ndr_is_null_handle(samr_handle))
			status = NT_STATUS_INVALID_HANDLE;
	}

	ndr_rpc_release(samr_handle);
	return (status);
}

/*
 * samr_connect2
 *
 * Connect to the SAM on a Windows NT 4.0 server (domain controller).
 * We need the domain controller name and, if everything works, we
 * return a handle.  This function adds the double backslash prefx to
 * make it easy for applications.
 *
 * Returns 0 on success. Otherwise returns a -ve error code.
 */
/*ARGSUSED*/
static DWORD
samr_connect2(char *server, char *domain, char *username, DWORD access_mask,
    mlsvc_handle_t *samr_handle)
{
	struct samr_Connect2 arg;
	int opnum;
	DWORD status;

	bzero(&arg, sizeof (struct samr_Connect2));
	opnum = SAMR_OPNUM_Connect2;
	status = NT_STATUS_SUCCESS;

	arg.servername = ndr_rpc_derive_nbhandle(samr_handle, server);
	arg.access_mask = access_mask;

	if (ndr_rpc_call(samr_handle, opnum, &arg) != 0) {
		status = NT_STATUS_UNSUCCESSFUL;
	} else if (arg.status != 0) {
		status = NT_SC_VALUE(arg.status);
	} else {
		(void) memcpy(&samr_handle->handle, &arg.handle,
		    sizeof (ndr_hdid_t));

		if (ndr_is_null_handle(samr_handle))
			status = NT_STATUS_INVALID_HANDLE;
	}

	ndr_rpc_release(samr_handle);
	return (status);
}

/*
 * samr_connect4
 *
 * Connect to the SAM on a Windows 2000 domain controller.
 */
/*ARGSUSED*/
static DWORD
samr_connect4(char *server, char *domain, char *username, DWORD access_mask,
    mlsvc_handle_t *samr_handle)
{
	struct samr_Connect4 arg;
	int opnum;
	DWORD status;

	bzero(&arg, sizeof (struct samr_Connect4));
	opnum = SAMR_OPNUM_Connect4;
	status = NT_STATUS_SUCCESS;

	arg.servername = ndr_rpc_derive_nbhandle(samr_handle, server);
	arg.revision = SAMR_REVISION_2;
	arg.access_mask = access_mask;

	if (ndr_rpc_call(samr_handle, opnum, &arg) != 0) {
		status = NT_STATUS_UNSUCCESSFUL;
	} else if (arg.status != 0) {
		status = NT_SC_VALUE(arg.status);
	} else {
		(void) memcpy(&samr_handle->handle, &arg.handle,
		    sizeof (ndr_hdid_t));

		if (ndr_is_null_handle(samr_handle))
			status = NT_STATUS_INVALID_HANDLE;
	}

	ndr_rpc_release(samr_handle);
	return (status);
}

/*
 * samr_connect5
 *
 * Connect to the SAM on a Windows XP domain controller.  On Windows
 * XP, the server should be the fully qualified DNS domain name with
 * a double backslash prefix.  At this point, it is assumed that we
 * need to add the prefix and the DNS domain name here.
 *
 * If this call succeeds, a SAMR handle is placed in samr_handle and
 * zero is returned. Otherwise, a -ve error code is returned.
 */
/*ARGSUSED*/
static DWORD
samr_connect5(char *server, char *domain, char *username, DWORD access_mask,
    mlsvc_handle_t *samr_handle)
{
	struct samr_Connect5 arg;
	int opnum;
	DWORD status;
	smb_domainex_t dinfo;

	bzero(&arg, sizeof (struct samr_Connect5));
	opnum = SAMR_OPNUM_Connect5;
	status = NT_STATUS_SUCCESS;

	if (!smb_domain_getinfo(&dinfo))
		return (NT_STATUS_CANT_ACCESS_DOMAIN_INFO);

	arg.servername = ndr_rpc_derive_nbhandle(samr_handle, server);
	arg.access_mask = SAM_ENUM_LOCAL_DOMAIN;
	arg.unknown2_00000001 = 0x00000001;
	arg.unknown3_00000001 = 0x00000001;
	arg.unknown4_00000003 = 0x00000003;
	arg.unknown5_00000000 = 0x00000000;

	if (ndr_rpc_call(samr_handle, opnum, &arg) != 0) {
		status = NT_STATUS_UNSUCCESSFUL;
	} else if (arg.status != 0) {
		status = NT_SC_VALUE(arg.status);
	} else {

		(void) memcpy(&samr_handle->handle, &arg.handle,
		    sizeof (ndr_hdid_t));

		if (ndr_is_null_handle(samr_handle))
			status = NT_STATUS_INVALID_HANDLE;
	}

	ndr_rpc_release(samr_handle);
	return (status);
}


/*
 * samr_close_handle
 *
 * This is function closes any valid handle, i.e. sam, domain, user etc.
 * If the handle being closed is the top level connect handle, we unbind.
 * Then we zero out the handle to invalidate it.
 */
int
samr_close_handle(mlsvc_handle_t *samr_handle)
{
	struct samr_CloseHandle arg;
	int opnum;

	if (ndr_is_null_handle(samr_handle))
		return (-1);

	opnum = SAMR_OPNUM_CloseHandle;
	bzero(&arg, sizeof (struct samr_CloseHandle));
	(void) memcpy(&arg.handle, &samr_handle->handle, sizeof (ndr_hdid_t));

	(void) ndr_rpc_call(samr_handle, opnum, &arg);
	ndr_rpc_release(samr_handle);

	if (ndr_is_bind_handle(samr_handle))
		ndr_rpc_unbind(samr_handle);

	bzero(samr_handle, sizeof (mlsvc_handle_t));
	return (0);
}

/*
 * samr_open_domain
 *
 * We use a SAM handle to obtain a handle for a domain, specified by
 * the SID. The SID can be obtain via the LSA interface. A handle for
 * the domain is returned in domain_handle.
 */
DWORD
samr_open_domain(mlsvc_handle_t *samr_handle, DWORD access_mask,
    struct samr_sid *sid, mlsvc_handle_t *domain_handle)
{
	struct samr_OpenDomain arg;
	int opnum;
	DWORD status;

	if (ndr_is_null_handle(samr_handle) ||
	    sid == NULL || domain_handle == NULL) {
		return (NT_STATUS_INVALID_PARAMETER);
	}

	opnum = SAMR_OPNUM_OpenDomain;
	bzero(&arg, sizeof (struct samr_OpenDomain));
	(void) memcpy(&arg.handle, &samr_handle->handle, sizeof (ndr_hdid_t));

	arg.access_mask = access_mask;
	arg.sid = sid;

	if (ndr_rpc_call(samr_handle, opnum, &arg) != 0) {
		status = NT_STATUS_UNSUCCESSFUL;
	} else if (arg.status != 0) {
		status = arg.status;
	} else {
		status = NT_STATUS_SUCCESS;
		ndr_inherit_handle(domain_handle, samr_handle);

		(void) memcpy(&domain_handle->handle, &arg.domain_handle,
		    sizeof (ndr_hdid_t));

		if (ndr_is_null_handle(domain_handle))
			status = NT_STATUS_INVALID_HANDLE;
	}

	if (status != NT_STATUS_SUCCESS)
		ndr_rpc_status(samr_handle, opnum, status);

	ndr_rpc_release(samr_handle);
	return (status);
}

/*
 * samr_open_user
 *
 * Use a domain handle to obtain a handle for a user, specified by the
 * user RID. A user RID (effectively a uid) can be obtained via the
 * LSA interface. A handle for the user is returned in user_handle.
 * Once you have a user handle it should be possible to query the SAM
 * for information on that user.
 */
DWORD
samr_open_user(mlsvc_handle_t *domain_handle, DWORD access_mask, DWORD rid,
    mlsvc_handle_t *user_handle)
{
	struct samr_OpenUser arg;
	int opnum;
	DWORD status = NT_STATUS_SUCCESS;

	if (ndr_is_null_handle(domain_handle) || user_handle == NULL)
		return (NT_STATUS_INVALID_PARAMETER);

	opnum = SAMR_OPNUM_OpenUser;
	bzero(&arg, sizeof (struct samr_OpenUser));
	(void) memcpy(&arg.handle, &domain_handle->handle,
	    sizeof (ndr_hdid_t));
	arg.access_mask = access_mask;
	arg.rid = rid;

	if (ndr_rpc_call(domain_handle, opnum, &arg) != 0) {
		status = NT_STATUS_UNSUCCESSFUL;
	} else if (arg.status != 0) {
		ndr_rpc_status(domain_handle, opnum, arg.status);
		status = NT_SC_VALUE(arg.status);
	} else {
		ndr_inherit_handle(user_handle, domain_handle);

		(void) memcpy(&user_handle->handle, &arg.user_handle,
		    sizeof (ndr_hdid_t));

		if (ndr_is_null_handle(user_handle))
			status = NT_STATUS_INVALID_HANDLE;
	}

	ndr_rpc_release(domain_handle);
	return (status);
}

/*
 * samr_delete_user
 *
 * Delete the user specified by the user_handle.
 */
DWORD
samr_delete_user(mlsvc_handle_t *user_handle)
{
	struct samr_DeleteUser arg;
	int opnum;
	DWORD status;

	if (ndr_is_null_handle(user_handle))
		return (NT_STATUS_INVALID_PARAMETER);

	opnum = SAMR_OPNUM_DeleteUser;
	bzero(&arg, sizeof (struct samr_DeleteUser));
	(void) memcpy(&arg.user_handle, &user_handle->handle,
	    sizeof (ndr_hdid_t));

	if (ndr_rpc_call(user_handle, opnum, &arg) != 0) {
		status = NT_STATUS_INVALID_PARAMETER;
	} else if (arg.status != 0) {
		ndr_rpc_status(user_handle, opnum, arg.status);
		status = NT_SC_VALUE(arg.status);
	} else {
		status = 0;
	}

	ndr_rpc_release(user_handle);
	return (status);
}

/*
 * samr_open_group
 *
 * Use a domain handle to obtain a handle for a group, specified by the
 * group RID. A group RID (effectively a gid) can be obtained via the
 * LSA interface. A handle for the group is returned in group_handle.
 * Once you have a group handle it should be possible to query the SAM
 * for information on that group.
 */
int
samr_open_group(
	mlsvc_handle_t *domain_handle,
	DWORD rid,
	mlsvc_handle_t *group_handle)
{
	struct samr_OpenGroup arg;
	int opnum;
	int rc;

	if (ndr_is_null_handle(domain_handle) || group_handle == NULL)
		return (-1);

	opnum = SAMR_OPNUM_OpenGroup;
	bzero(&arg, sizeof (struct samr_OpenUser));
	(void) memcpy(&arg.handle, &domain_handle->handle,
	    sizeof (ndr_hdid_t));
	arg.access_mask = SAM_LOOKUP_INFORMATION | SAM_ACCESS_USER_READ;
	arg.rid = rid;

	if ((rc = ndr_rpc_call(domain_handle, opnum, &arg)) != 0)
		return (-1);

	if (arg.status != 0) {
		ndr_rpc_status(domain_handle, opnum, arg.status);
		rc = -1;
	} else {
		ndr_inherit_handle(group_handle, domain_handle);

		(void) memcpy(&group_handle->handle, &arg.group_handle,
		    sizeof (ndr_hdid_t));

		if (ndr_is_null_handle(group_handle))
			rc = -1;
	}

	ndr_rpc_release(domain_handle);
	return (rc);
}

/*
 * samr_create_user
 *
 * Create a user in the domain specified by the domain handle. If this
 * call is successful, the server will return the RID for the user and
 * a user handle, which may be used to set or query the SAM.
 *
 * Observed status codes:
 *	NT_STATUS_INVALID_PARAMETER
 *	NT_STATUS_INVALID_ACCOUNT_NAME
 *	NT_STATUS_ACCESS_DENIED
 *	NT_STATUS_USER_EXISTS
 *
 * Returns 0 on success. Otherwise returns an NT status code.
 */
DWORD
samr_create_user(mlsvc_handle_t *domain_handle, char *username,
    DWORD account_flags, DWORD *rid, mlsvc_handle_t *user_handle)
{
	struct samr_CreateUser arg;
	ndr_heap_t *heap;
	int opnum;
	int rc;
	DWORD status = 0;

	if (ndr_is_null_handle(domain_handle) ||
	    username == NULL || rid == NULL) {
		return (NT_STATUS_INVALID_PARAMETER);
	}

	opnum = SAMR_OPNUM_CreateUser;

	bzero(&arg, sizeof (struct samr_CreateUser));
	(void) memcpy(&arg.handle, &domain_handle->handle,
	    sizeof (ndr_hdid_t));

	heap = ndr_rpc_get_heap(domain_handle);
	ndr_heap_mkvcs(heap, username, (ndr_vcstr_t *)&arg.username);

	arg.account_flags = account_flags;
	arg.desired_access = 0xE00500B0;

	rc = ndr_rpc_call(domain_handle, opnum, &arg);
	if (rc != 0) {
		status = NT_STATUS_INVALID_PARAMETER;
	} else if (arg.status != 0) {
		status = NT_SC_VALUE(arg.status);

		if (status != NT_STATUS_USER_EXISTS) {
			smb_tracef("SamrCreateUser[%s]: %s", username,
			    xlate_nt_status(status));
		}
	} else {
		ndr_inherit_handle(user_handle, domain_handle);

		(void) memcpy(&user_handle->handle, &arg.user_handle,
		    sizeof (ndr_hdid_t));

		*rid = arg.rid;

		if (ndr_is_null_handle(user_handle))
			status = NT_STATUS_INVALID_HANDLE;
		else
			status = 0;
	}

	ndr_rpc_release(domain_handle);
	return (status);
}

/*
 * samr_lookup_domain
 *
 * Lookup up the domain SID for the specified domain name. The handle
 * should be one returned from samr_connect. The allocated memory for
 * the returned SID must be freed by caller.
 */
smb_sid_t *
samr_lookup_domain(mlsvc_handle_t *samr_handle, char *domain_name)
{
	struct samr_LookupDomain	arg;
	smb_sid_t	*domsid = NULL;
	int		opnum;
	size_t		length;

	if (ndr_is_null_handle(samr_handle) || domain_name == NULL)
		return (NULL);

	opnum = SAMR_OPNUM_LookupDomain;
	bzero(&arg, sizeof (struct samr_LookupDomain));

	(void) memcpy(&arg.handle, &samr_handle->handle,
	    sizeof (samr_handle_t));

	length = smb_wcequiv_strlen(domain_name);
	if (ndr_rpc_server_os(samr_handle) == NATIVE_OS_WIN2000)
		length += sizeof (smb_wchar_t);

	arg.domain_name.length = length;
	arg.domain_name.allosize = length;
	arg.domain_name.str = (unsigned char *)domain_name;

	if (ndr_rpc_call(samr_handle, opnum, &arg) == 0)
		domsid = smb_sid_dup((smb_sid_t *)arg.sid);

	ndr_rpc_release(samr_handle);
	return (domsid);
}

/*
 * samr_enum_local_domains
 *
 * Get the list of local domains supported by a server.
 *
 * Returns NT status codes.
 */
DWORD
samr_enum_local_domains(mlsvc_handle_t *samr_handle)
{
	struct samr_EnumLocalDomain	arg;
	int	opnum;
	DWORD	status;

	if (ndr_is_null_handle(samr_handle))
		return (NT_STATUS_INVALID_PARAMETER);

	opnum = SAMR_OPNUM_EnumLocalDomains;
	bzero(&arg, sizeof (struct samr_EnumLocalDomain));

	(void) memcpy(&arg.handle, &samr_handle->handle,
	    sizeof (samr_handle_t));
	arg.enum_context = 0;
	arg.max_length = 0x00002000;	/* Value used by NT */

	if (ndr_rpc_call(samr_handle, opnum, &arg) != 0) {
		status = NT_STATUS_INVALID_PARAMETER;
	} else {
		status = NT_SC_VALUE(arg.status);

		/*
		 * Handle none-mapped status quietly.
		 */
		if (status != NT_STATUS_NONE_MAPPED)
			ndr_rpc_status(samr_handle, opnum, arg.status);
	}

	ndr_rpc_release(samr_handle);
	return (status);
}

/*
 * samr_lookup_domain_names
 *
 * Lookup up the given name in the domain specified by domain_handle.
 * Upon a successful lookup the information is returned in the account
 * arg and caller must free allocated memories by calling smb_account_free().
 *
 * Returns NT status codes.
 */
uint32_t
samr_lookup_domain_names(mlsvc_handle_t *domain_handle, char *name,
    smb_account_t *account)
{
	struct samr_LookupNames	arg;
	int			opnum;
	uint32_t		status;
	size_t			length;

	if (ndr_is_null_handle(domain_handle) ||
	    name == NULL || account == NULL) {
		return (NT_STATUS_INVALID_PARAMETER);
	}

	bzero(account, sizeof (smb_account_t));
	opnum = SAMR_OPNUM_LookupNames;
	bzero(&arg, sizeof (struct samr_LookupNames));

	(void) memcpy(&arg.handle, &domain_handle->handle,
	    sizeof (samr_handle_t));
	arg.n_entry = 1;
	arg.max_n_entry = 1000;
	arg.index = 0;
	arg.total = 1;

	length = smb_wcequiv_strlen(name);
	if (ndr_rpc_server_os(domain_handle) == NATIVE_OS_WIN2000)
		length += sizeof (smb_wchar_t);

	arg.name.length = length;
	arg.name.allosize = length;
	arg.name.str = (unsigned char *)name;

	if (ndr_rpc_call(domain_handle, opnum, &arg) != 0) {
		status = NT_STATUS_INVALID_PARAMETER;
	} else if (arg.status != NT_STATUS_SUCCESS) {
		status = NT_SC_VALUE(arg.status);

		/*
		 * Handle none-mapped status quietly.
		 */
		if (status != NT_STATUS_NONE_MAPPED)
			ndr_rpc_status(domain_handle, opnum, arg.status);
	} else {
		account->a_type = arg.rid_types.rid_type[0];
		account->a_rid = arg.rids.rid[0];
		status = NT_STATUS_SUCCESS;
	}

	ndr_rpc_release(domain_handle);
	return (status);
}

/*
 * samr_query_user_info
 *
 * Query information on a specific user. The handle must be a valid
 * user handle obtained via samr_open_user.
 *
 * Returns 0 on success, otherwise returns -ve error code.
 */
int
samr_query_user_info(mlsvc_handle_t *user_handle, WORD switch_value,
    union samr_user_info *user_info)
{
	struct samr_QueryUserInfo	arg;
	int	opnum;
	int	rc;

	if (ndr_is_null_handle(user_handle) || user_info == 0)
		return (-1);

	opnum = SAMR_OPNUM_QueryUserInfo;
	bzero(&arg, sizeof (struct samr_QueryUserInfo));

	(void) memcpy(&arg.user_handle, &user_handle->handle,
	    sizeof (samr_handle_t));
	arg.switch_value = switch_value;

	if (ndr_rpc_call(user_handle, opnum, &arg) != 0) {
		ndr_rpc_release(user_handle);
		return (-1);
	}

	if (arg.status != 0)
		rc = -1;
	else
		rc = samr_setup_user_info(switch_value, &arg, user_info);

	ndr_rpc_release(user_handle);
	return (rc);
}

/*
 * samr_setup_user_info
 *
 * Private function to set up the samr_user_info data. Dependent on
 * the switch value this function may use strdup which will malloc
 * memory. The caller is responsible for deallocating this memory.
 *
 * Returns 0 on success, otherwise returns -1.
 */
static int
samr_setup_user_info(WORD switch_value,
    struct samr_QueryUserInfo *arg, union samr_user_info *user_info)
{
	struct samr_QueryUserInfo1	*info1;
	struct samr_QueryUserInfo6	*info6;

	switch (switch_value) {
	case 1:
		info1 = &arg->ru.info1;
		user_info->info1.username = strdup(
		    (char const *)info1->username.str);
		user_info->info1.fullname = strdup(
		    (char const *)info1->fullname.str);
		user_info->info1.description = strdup(
		    (char const *)info1->description.str);
		user_info->info1.unknown = 0;
		user_info->info1.group_rid = info1->group_rid;
		return (0);

	case 6:
		info6 = &arg->ru.info6;
		user_info->info6.username = strdup(
		    (char const *)info6->username.str);
		user_info->info6.fullname = strdup(
		    (char const *)info6->fullname.str);
		return (0);

	case 7:
		user_info->info7.username = strdup(
		    (char const *)arg->ru.info7.username.str);
		return (0);

	case 8:
		user_info->info8.fullname = strdup(
		    (char const *)arg->ru.info8.fullname.str);
		return (0);

	case 9:
		user_info->info9.group_rid = arg->ru.info9.group_rid;
		return (0);

	case 16:
		return (0);

	default:
		break;
	};

	return (-1);
}

/*
 * samr_query_user_groups
 *
 * Query the groups for a specific user. The handle must be a valid
 * user handle obtained via samr_open_user. The list of groups is
 * returned in group_info. Note that group_info->groups is allocated
 * using malloc. The caller is responsible for deallocating this
 * memory when it is no longer required. If group_info->n_entry is 0
 * then no memory was allocated.
 *
 * Returns 0 on success, otherwise returns -1.
 */
int
samr_query_user_groups(mlsvc_handle_t *user_handle, int *n_groups,
    struct samr_UserGroups **groups)
{
	struct samr_QueryUserGroups arg;
	int	opnum;
	int	rc;
	int	nbytes;

	if (ndr_is_null_handle(user_handle))
		return (-1);

	opnum = SAMR_OPNUM_QueryUserGroups;
	bzero(&arg, sizeof (struct samr_QueryUserGroups));

	(void) memcpy(&arg.user_handle, &user_handle->handle,
	    sizeof (samr_handle_t));

	rc = ndr_rpc_call(user_handle, opnum, &arg);
	if (rc == 0) {
		if (arg.info == 0) {
			rc = -1;
		} else {
			nbytes = arg.info->n_entry *
			    sizeof (struct samr_UserGroups);

			if ((*groups = malloc(nbytes)) == NULL) {
				*n_groups = 0;
				rc = -1;
			} else {
				*n_groups = arg.info->n_entry;
				bcopy(arg.info->groups, *groups, nbytes);
			}
		}
	}

	ndr_rpc_release(user_handle);
	return (rc);
}

/*
 * samr_get_user_pwinfo
 *
 * Get some user password info. I'm not sure what this is yet but it is
 * part of the create user sequence. The handle must be a valid user
 * handle. Since I don't know what this is returning, I haven't provided
 * any return data yet.
 *
 * Returns 0 on success. Otherwise returns an NT status code.
 */
DWORD
samr_get_user_pwinfo(mlsvc_handle_t *user_handle)
{
	struct samr_GetUserPwInfo arg;
	int	opnum;
	DWORD	status;

	if (ndr_is_null_handle(user_handle))
		return (NT_STATUS_INVALID_PARAMETER);

	opnum = SAMR_OPNUM_GetUserPwInfo;
	bzero(&arg, sizeof (struct samr_GetUserPwInfo));
	(void) memcpy(&arg.user_handle, &user_handle->handle,
	    sizeof (samr_handle_t));

	if (ndr_rpc_call(user_handle, opnum, &arg) != 0) {
		status = NT_STATUS_INVALID_PARAMETER;
	} else if (arg.status != 0) {
		ndr_rpc_status(user_handle, opnum, arg.status);
		status = NT_SC_VALUE(arg.status);
	} else {
		status = 0;
	}

	ndr_rpc_release(user_handle);
	return (status);
}

/*
 * samr_set_user_info
 *
 * Returns 0 on success. Otherwise returns an NT status code.
 * NT status codes observed so far:
 *	NT_STATUS_WRONG_PASSWORD
 */
/*ARGSUSED*/
DWORD
samr_set_user_info(mlsvc_handle_t *user_handle)
{
	unsigned char ssn_key[SMBAUTH_SESSION_KEY_SZ];
	struct samr_SetUserInfo arg;
	int opnum;
	DWORD status = 0;

	if (ndr_is_null_handle(user_handle))
		return (NT_STATUS_INVALID_PARAMETER);

	if (ndr_rpc_get_ssnkey(user_handle, ssn_key, sizeof (ssn_key)))
		return (NT_STATUS_INVALID_PARAMETER);

	opnum = SAMR_OPNUM_SetUserInfo;
	bzero(&arg, sizeof (struct samr_SetUserInfo));
	(void) memcpy(&arg.user_handle, &user_handle->handle,
	    sizeof (samr_handle_t));

	arg.info.index = SAMR_SET_USER_INFO_23;
	arg.info.switch_value = SAMR_SET_USER_INFO_23;

	samr_set_user_unknowns(&arg.info.ru.info23);
	samr_set_user_logon_hours(&arg);

	if (samr_set_user_password(ssn_key, arg.info.ru.info23.password) < 0)
		status = NT_STATUS_INTERNAL_ERROR;

	if (ndr_rpc_call(user_handle, opnum, &arg) != 0) {
		status = NT_STATUS_INVALID_PARAMETER;
	} else if (arg.status != 0) {
		ndr_rpc_status(user_handle, opnum, arg.status);
		status = NT_SC_VALUE(arg.status);
	}

	ndr_rpc_release(user_handle);
	return (status);
}

static void
samr_set_user_unknowns(struct samr_SetUserInfo23 *info)
{
	bzero(info, sizeof (struct samr_SetUserInfo23));

	info->sd.length = 0;
	info->sd.data = 0;
	info->user_rid = 0;
	info->group_rid = DOMAIN_GROUP_RID_USERS;

	/*
	 * The trust account value used here should probably
	 * match the one used to create the trust account.
	 */
	info->acct_info = SAMR_AF_WORKSTATION_TRUST_ACCOUNT;
	info->flags = 0x09F827FA;
}

/*
 * samr_set_user_logon_hours
 *
 * SamrSetUserInfo appears to contain some logon hours information, which
 * looks like a varying, conformant array. The top level contains a value
 * (units), which probably indicates the how to interpret the array. The
 * array definition looks like it contains a maximum size, an initial
 * offset and a bit length (units/8), followed by the bitmap.
 *
 *  (info)
 * +-------+
 * | units |
 * +-------+    (hours)
 * | hours |-->+-----------+
 * +-------+   | max_is    |
 *             +-----------+
 *             | first_is  |
 *             +-----------+
 *             | length_is |
 *             +------------------------+
 *             | bitmap[length_is]      |
 *             +---------+--------------+
 *
 * 10080 minutes/week => 10080/8 = 1260 (0x04EC) bytes.
 * 168 hours/week => 168/8 = 21 (0xA8) bytes.
 * In the netmon examples seen so far, all bits are set to 1, i.e.
 * an array containing 0xff. This is probably the default setting.
 *
 * ndrgen has a problem with complex [size_is] statements (length/8).
 * So, for now, we fake it using two separate components.
 */
static void
samr_set_user_logon_hours(struct samr_SetUserInfo *sui)
{
	sui->logon_hours.size = SAMR_HOURS_MAX_SIZE;
	sui->logon_hours.first = 0;
	sui->logon_hours.length = SAMR_SET_USER_HOURS_SZ;
	(void) memset(sui->logon_hours.bitmap, 0xFF, SAMR_SET_USER_HOURS_SZ);

	sui->info.ru.info23.logon_info.units = SAMR_HOURS_PER_WEEK;
	sui->info.ru.info23.logon_info.hours =
	    (DWORD)(uintptr_t)sui->logon_hours.bitmap;
}

/*
 * samr_set_user_password
 *
 * Set the initial password for the user.
 *
 * Returns 0 if everything goes well, -1 if there is trouble generating a
 * key.
 */
static int
samr_set_user_password(unsigned char *nt_key, BYTE *oem_password)
{
	char hostname[NETBIOS_NAME_SZ];

	randomize((char *)oem_password, SAMR_SET_USER_DATA_SZ);

	/*
	 * The new password is going to be the NetBIOS name of the system
	 * in lower case.
	 */
	if (smb_getnetbiosname(hostname, sizeof (hostname)) != 0)
		return (-1);

	(void) smb_strlwr(hostname);

	/*
	 * Generate the OEM password from the hostname and the user session
	 * key(nt_key).
	 */
	/*LINTED E_BAD_PTR_CAST_ALIGN*/
	(void) sam_oem_password((oem_password_t *)oem_password,
	    (unsigned char *)hostname, nt_key);
	return (0);
}
