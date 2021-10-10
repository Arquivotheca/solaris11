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
 * Server Service (srvsvc) client side RPC library interface. The
 * srvsvc interface allows a client to query a server for information
 * on shares, sessions, connections and files on the server. Some
 * functions are available via anonymous IPC while others require
 * administrator privilege. Also, some functions return NT status
 * values while others return Win32 errors codes.
 */

#include <sys/errno.h>
#include <stdio.h>
#include <time.h>
#include <strings.h>

#include <smbsrv/libsmb.h>
#include <smbsrv/libntsvcs.h>
#include <smbsrv/smbinfo.h>
#include <smbsrv/ndl/srvsvc.ndl>

/*
 * Information level for NetShareGetInfo.
 */
DWORD srvsvc_info_level = 1;
static void srvsvc_net_free(srvsvc_info_t *);

/*
 * Bind to the the SRVSVC.
 *
 * If username argument is NULL, an anonymous connection will be established.
 * Otherwise, an authenticated connection will be established.
 */
static int
srvsvc_open(char *server, char *domain, char *username, mlsvc_handle_t *handle)
{
	smb_domainex_t di;

	if (server == NULL || domain == NULL) {
		if (!smb_domain_getinfo(&di))
			return (-1);

		server = di.d_dc;
		domain = di.d_primary.di_nbname;
	}

	if (ndr_rpc_bind(handle, server, domain, username, "SRVSVC") < 0)
		return (-1);

	return (0);
}

/*
 * Unbind the SRVSVC connection.
 */
static void
srvsvc_close(mlsvc_handle_t *handle)
{
	ndr_rpc_unbind(handle);
}

static srvsvc_info_t *
srvsvc_net_enum_add(srvsvc_list_t *sl, uint32_t type)
{
	srvsvc_info_t		*info;

	if ((info = calloc(1, sizeof (srvsvc_info_t))) == NULL)
		return (NULL);

	info->l_type = type;
	list_insert_tail(&sl->sl_list, info);
	++sl->sl_count;
	return (info);
}

static void
srvsvc_net_enum_remove(srvsvc_list_t *sl, srvsvc_info_t *si)
{
	list_remove(&sl->sl_list, si);
	--sl->sl_count;
	srvsvc_net_free(si);
	free(si);
}

static void
srvsvc_net_free(srvsvc_info_t *info)
{
	srvsvc_share_info_t	*si;
	srvsvc_session_info_t	*ss;
	srvsvc_connect_info_t	*sc;
	srvsvc_file_info_t	*sf;

	switch (info->l_type) {
	case SMB_SVCENUM_TYPE_USER:
		ss = &info->l_list.ul_session;
		free(ss->ui_account);
		free(ss->ui_workstation);
		bzero(ss, sizeof (srvsvc_session_info_t));
		break;
	case SMB_SVCENUM_TYPE_TREE:
		sc = &info->l_list.ul_connection;
		free(sc->ci_share);
		free(sc->ci_username);
		bzero(sc, sizeof (srvsvc_connect_info_t));
		break;
	case SMB_SVCENUM_TYPE_FILE:
		sf = &info->l_list.ul_file;
		free(sf->fi_path);
		free(sf->fi_username);
		bzero(sf, sizeof (srvsvc_file_info_t));
		break;
	case SMB_SVCENUM_TYPE_SHARE:
		si = &info->l_list.ul_shr;
		free(si->si_netname);
		free(si->si_comment);
		free(si->si_path);
		free(si->si_servername);
		bzero(si, sizeof (srvsvc_share_info_t));
		break;
	}
}

/*
 * This is a client side routine for NetShareGetInfo enumeration.
 * Only level 1 request is supported.
 */
int
srvsvc_net_share_enum(char *server, char *domain, uint32_t level,
    srvsvc_list_t *sl)
{
	typedef struct mslm_infonres srvsvc_infonres_t;

	struct mslm_NetShareEnum	arg;
	mlsvc_handle_t			handle;
	srvsvc_infonres_t		infonres;
	srvsvc_infonres_t		*result;
	struct mslm_NetShareInfo_1	*info1;
	srvsvc_info_t			*info;
	srvsvc_share_info_t		*si;
	char				user[SMB_USERNAME_MAXLEN];
	char				*comment;
	int				opnum;
	int				rc;
	int				i;

	smb_ipc_get_user(user, SMB_USERNAME_MAXLEN);

	if (srvsvc_open(server, domain, user, &handle) != 0)
		return (-1);

	opnum = SRVSVC_OPNUM_NetShareEnum;
	bzero(&arg, sizeof (struct mslm_NetShareEnum));
	bzero(&infonres, sizeof (srvsvc_infonres_t));

	arg.servername = ndr_rpc_derive_nbhandle(&handle, server);
	if (arg.servername == NULL) {
		srvsvc_close(&handle);
		return (-1);
	}

	arg.level = level;	/* share information level */
	arg.result.level = level;
	arg.result.bufptr.p = &infonres;
	arg.prefmaxlen = (uint32_t)-1;

	rc = ndr_rpc_call(&handle, opnum, &arg);
	if (rc != 0) {
		srvsvc_close(&handle);
		return (-1);
	}

	if ((arg.status != 0) && (arg.status != ERROR_MORE_DATA)) {
		srvsvc_close(&handle);
		return (-1);
	}

	result = (srvsvc_infonres_t *)arg.result.bufptr.p;
	info1 = (struct mslm_NetShareInfo_1 *)result->entries;

	sl->sl_level = level;
	sl->sl_totalentries = arg.totalentries;
	sl->sl_entriesread = result->entriesread;

	for (i = 0; i < result->entriesread && i < arg.totalentries; ++i) {
		if (info1[i].shi1_comment == NULL)
			comment = "";
		else
			comment =  (char *)info1[i].shi1_comment;

		if ((info = srvsvc_net_enum_add(sl,
		    SMB_SVCENUM_TYPE_SHARE)) != NULL) {
			si = &info->l_list.ul_shr;
			si->si_netname = strdup((char *)info1[i].shi1_netname);
			si->si_comment = strdup(comment);
			si->si_type = info1[i].shi1_type;

			if (si->si_netname == NULL || si->si_comment == NULL)
				srvsvc_net_enum_remove(sl, info);
		}
	}

	ndr_rpc_release(&handle);
	srvsvc_close(&handle);
	return (0);
}

/*
 * This is a client side routine for NetShareGetInfo.
 * Levels 0 and 1 work with an anonymous connection but
 * level 2 requires administrator access.
 */
int
srvsvc_net_share_get_info(char *server, char *domain, char *netname,
    uint32_t level, srvsvc_share_info_t *si)
{
	struct mlsm_NetShareGetInfo arg;
	mlsvc_handle_t handle;
	int rc;
	int opnum;
	struct mslm_NetShareInfo_0 *info0;
	struct mslm_NetShareInfo_1 *info1;
	struct mslm_NetShareInfo_2 *info2;
	char user[SMB_USERNAME_MAXLEN];

	if (netname == NULL || si == NULL)
		return (-1);

	smb_ipc_get_user(user, SMB_USERNAME_MAXLEN);

	if (srvsvc_open(server, domain, user, &handle) != 0)
		return (-1);

	opnum = SRVSVC_OPNUM_NetShareGetInfo;
	bzero(&arg, sizeof (struct mlsm_NetShareGetInfo));

	arg.servername = ndr_rpc_derive_nbhandle(&handle, server);
	if (arg.servername == NULL) {
		srvsvc_close(&handle);
		return (-1);
	}

	arg.netname = (LPTSTR)netname;
	arg.level = level; /* share information level */

	rc = ndr_rpc_call(&handle, opnum, &arg);
	if ((rc != 0) || (arg.status != 0)) {
		srvsvc_close(&handle);
		return (-1);
	}

	bzero(si, sizeof (srvsvc_share_info_t));

	switch (arg.result.switch_value) {
	case 0:
		info0 = arg.result.ru.info0;
		si->si_netname = strdup((char *)info0->shi0_netname);
		break;

	case 1:
		info1 = arg.result.ru.info1;
		si->si_netname = strdup((char *)info1->shi1_netname);
		si->si_type = info1->shi1_type;

		if (info1->shi1_comment)
			si->si_comment = strdup((char *)info1->shi1_comment);
		break;

	case 2:
		info2 = arg.result.ru.info2;
		si->si_netname = strdup((char *)info2->shi2_netname);
		si->si_type = info2->shi2_type;
		si->si_permissions = info2->shi2_permissions;
		si->si_max_uses = info2->shi2_max_uses;
		si->si_current_uses = info2->shi2_current_uses;

		if (info2->shi2_comment)
			si->si_comment = strdup((char *)info2->shi2_comment);

		if (info2->shi2_path)
			si->si_path = strdup((char *)info2->shi2_path);

		break;

	default:
		smb_tracef("srvsvc: unknown level");
		break;
	}

	ndr_rpc_release(&handle);
	srvsvc_close(&handle);
	return (0);
}

/*
 * This is a client side routine for NetFilesEnum.
 * NetFilesEnum requires administrator rights.
 * Level 2 and 3 requests are supported.
 *
 * The basepath parameter specifies a qualifier for the returned information.
 * If this parameter is not NULL, the server MUST enumerate only resources that
 * have basepath as a prefix.
 *
 * If username parameter is specified, the server MUST limit the files returned
 * to those that were opened by a session whose user name matches username.
 */
int
srvsvc_net_files_enum(char *server, char *domain, char *basepath,
    char *username, uint32_t level, srvsvc_list_t *sl)
{
	struct mslm_NetFileEnum arg;
	mlsvc_handle_t handle;
	int rc, opnum, i;
	struct mslm_NetFileInfo2 info2;
	struct mslm_NetFileInfo3 info3;
	struct mslm_NetFileInfo2 *result2;
	struct mslm_NetFileInfo3 *result3;
	struct mslm_NetFileInfoBuf2 *fib2;
	struct mslm_NetFileInfoBuf3 *fib3;
	char user[SMB_USERNAME_MAXLEN];
	srvsvc_file_info_t	*sf;
	srvsvc_info_t		*info;

	smb_ipc_get_user(user, SMB_USERNAME_MAXLEN);
	rc = srvsvc_open(server, domain, user, &handle);
	if (rc != 0)
		return (-1);

	opnum = SRVSVC_OPNUM_NetFileEnum;
	bzero(&arg, sizeof (struct mslm_NetFileEnum));
	arg.servername = ndr_rpc_derive_nbhandle(&handle, server);
	if (arg.servername == NULL) {
		srvsvc_close(&handle);
		return (-1);
	}

	if (basepath != NULL)
		arg.basepath = (LPTSTR)basepath;

	if (username != NULL)
		arg.username = (LPTSTR)username;

	arg.info.level = arg.info.switch_value = level;
	arg.resume_handle = NULL;
	arg.pref_max_len = (uint32_t)-1;
	switch (level) {
	case 2:
		bzero(&info2, sizeof (struct mslm_NetFileInfo2));
		arg.info.ru.info2 = &info2;
		break;
	case 3:
		bzero(&info3, sizeof (struct mslm_NetFileInfo3));
		arg.info.ru.info3 = &info3;
		break;
	default:
		srvsvc_close(&handle);
		return (-1);
	}

	rc = ndr_rpc_call(&handle, opnum, &arg);
	if ((rc != 0) || (arg.status != 0)) {
		srvsvc_close(&handle);
		return (-1);
	}

	sl->sl_level = arg.info.switch_value;
	sl->sl_totalentries = arg.total_entries;
	switch (level) {
	case 2:
		result2 = (struct mslm_NetFileInfo2 *)arg.info.ru.info2;
		fib2 = (struct mslm_NetFileInfoBuf2 *)result2->fi2;
		sl->sl_entriesread = result2->entries_read;
		for (i = 0; i < sl->sl_entriesread &&
		    i < sl->sl_totalentries; ++i) {
			if ((info = srvsvc_net_enum_add(sl,
			    SMB_SVCENUM_TYPE_FILE)) != NULL) {
				sf = &info->l_list.ul_file;
				sf->fi_fid = fib2[i].fi2_id;
			}
		}
		break;
	case 3:
		result3 = (struct mslm_NetFileInfo3 *)arg.info.ru.info3;
		fib3 = (struct mslm_NetFileInfoBuf3 *)result3->fi3;
		sl->sl_entriesread = result3->entries_read;
		for (i = 0; i < sl->sl_entriesread &&
		    i < sl->sl_totalentries; ++i) {
			if ((fib3[i].fi3_username == NULL) ||
			    (fib3[i].fi3_pathname == NULL))
				break;

			if ((info = srvsvc_net_enum_add(sl,
			    SMB_SVCENUM_TYPE_FILE)) != NULL) {
				sf = &info->l_list.ul_file;
				sf->fi_username =
				    strdup((char *)fib3[i].fi3_username);
				sf->fi_path =
				    strdup((char *)fib3[i].fi3_pathname);
				sf->fi_fid = fib3[i].fi3_id;
				sf->fi_permissions = fib3[i].fi3_permissions;
				sf->fi_numlocks = fib3[i].fi3_num_locks;

				if (sf->fi_path == NULL ||
				    sf->fi_username == NULL)
					srvsvc_net_enum_remove(sl, info);
			}
		}
		break;

	default:
		rc = -1;
		break;
	}

	ndr_rpc_release(&handle);
	srvsvc_close(&handle);
	return (rc);
}

/*
 * This is a client side routine for NetConnectEnum.
 * NetConnectEnum requires administrator rights.
 * Level 0 and 1 requests are supported.
 *
 * The qualifier parameter specifies a share name or computer name for
 * connections of interest to the client. Qualifiers that begin with \\ are
 * considered server names and anything else is considered a share name.
 * Share names MUST NOT begin with \\.
 *
 * The qualifier parameter must not be NULL.
 */
int
srvsvc_net_connect_enum(char *server, char *domain, char *qualifier,
    uint32_t level, srvsvc_list_t *sl)
{
	struct mslm_NetConnectEnum arg;
	mlsvc_handle_t handle;
	int rc, i, opnum;
	struct mslm_NetConnectInfo0 info0;
	struct mslm_NetConnectInfo1 info1;
	struct mslm_NetConnectInfo0 *result0;
	struct mslm_NetConnectInfo1 *result1;
	struct mslm_NetConnectInfoBuf0 *cib0;
	struct mslm_NetConnectInfoBuf1 *cib1;
	char user[SMB_USERNAME_MAXLEN];
	srvsvc_connect_info_t *sc;
	srvsvc_info_t	*info;

	if (qualifier == NULL)
		return (-1);

	smb_ipc_get_user(user, SMB_USERNAME_MAXLEN);
	rc = srvsvc_open(server, domain, user, &handle);
	if (rc != 0)
		return (-1);

	opnum = SRVSVC_OPNUM_NetConnectEnum;
	bzero(&arg, sizeof (struct mslm_NetConnectEnum));
	arg.servername = ndr_rpc_derive_nbhandle(&handle, server);
	if (arg.servername == NULL) {
		srvsvc_close(&handle);
		return (-1);
	}
	arg.qualifier = (LPTSTR)qualifier;
	arg.resume_handle = NULL;
	arg.pref_max_len = (uint32_t)-1;
	arg.info.level = arg.info.switch_value = level;
	switch (level) {
	case 0:
		bzero(&info0, sizeof (struct mslm_NetConnectInfo0));
		arg.info.ru.info0 = &info0;
		break;
	case 1:
		bzero(&info1, sizeof (struct mslm_NetConnectInfo1));
		arg.info.ru.info1 = &info1;
		break;
	default:
		srvsvc_close(&handle);
		return (-1);
	}

	rc = ndr_rpc_call(&handle, opnum, &arg);
	if ((rc != 0) || (arg.status != 0)) {
		srvsvc_close(&handle);
		return (-1);
	}

	sl->sl_level = arg.info.switch_value;
	sl->sl_totalentries = arg.total_entries;
	switch (level) {
	case 0:
		result0 = (struct mslm_NetConnectInfo0 *)arg.info.ru.info0;
		cib0 = (struct mslm_NetConnectInfoBuf0 *)result0->ci0;
		sl->sl_entriesread = result0->entries_read;
		for (i = 0; i < sl->sl_entriesread &&
		    i < sl->sl_totalentries; ++i) {
			if ((info = srvsvc_net_enum_add(sl,
			    SMB_SVCENUM_TYPE_TREE)) != NULL) {
				sc = &info->l_list.ul_connection;
				sc->ci_id = cib0[i].coni0_id;
			}
		}
		break;
	case 1:
		result1 = (struct mslm_NetConnectInfo1 *)arg.info.ru.info1;
		cib1 = (struct mslm_NetConnectInfoBuf1 *)result1->ci1;
		sl->sl_entriesread = result1->entries_read;
		for (i = 0; i < sl->sl_entriesread &&
		    i < sl->sl_totalentries; ++i) {
			if ((cib1[i].coni1_username == NULL) ||
			    (cib1[i].coni1_netname == NULL))
				break;

			if ((info = srvsvc_net_enum_add(sl,
			    SMB_SVCENUM_TYPE_TREE)) != NULL) {
				sc = &info->l_list.ul_connection;
				sc->ci_username =
				    strdup((char *)cib1[i].coni1_username);
				sc->ci_share =
				    strdup((char *)cib1[i].coni1_netname);
				sc->ci_numopens = cib1[i].coni1_num_opens;
				sc->ci_time = cib1[i].coni1_time;
				sc->ci_numusers = cib1[i].coni1_num_users;

				if (sc->ci_username == NULL ||
				    sc->ci_share == NULL)
					srvsvc_net_enum_remove(sl, info);
			}
		}
		break;

	default:
		rc = -1;
		break;
	}

	ndr_rpc_release(&handle);
	srvsvc_close(&handle);
	return (rc);
}

/*
 * This is a client side routine for NetSessionEnum.
 * NetSessionEnum requires administrator rights.
 * Only level 1 request is supported.
 *
 * The clientname parameter specifies a qualifier for the returned information.
 * If a clientname is specified (i.e. it is not a NULL (zero-length) string),
 * the client computer name MUST match the ClientName for the session to be
 * returned. If a clientname is specified, it MUST start with "\\".
 *
 * The username parameter specifies a qualifier for the returned information.
 * If a username is specified (that is, not a NULL (zero-length) string), the
 * user name MUST match the username parameter for the session to be returned.
 */
int
srvsvc_net_session_enum(char *server, char *domain, char *clientname,
    char *username, uint32_t level, srvsvc_list_t *sl)
{
	struct mslm_NetSessionEnum arg;
	mlsvc_handle_t handle;
	int rc, i, opnum;
	struct mslm_infonres infonres;
	struct mslm_infonres *result;
	struct mslm_SESSION_INFO_1 *nsi1;
	char user[SMB_USERNAME_MAXLEN];
	srvsvc_session_info_t	*ss;
	srvsvc_info_t	*info;

	smb_ipc_get_user(user, SMB_USERNAME_MAXLEN);

	rc = srvsvc_open(server, domain, user, &handle);
	if (rc != 0)
		return (-1);

	opnum = SRVSVC_OPNUM_NetSessionEnum;
	bzero(&arg, sizeof (struct mslm_NetSessionEnum));
	bzero(&infonres, sizeof (struct mslm_infonres));

	arg.servername = ndr_rpc_derive_nbhandle(&handle, server);
	if (arg.servername == NULL) {
		srvsvc_close(&handle);
		return (-1);
	}

	if (clientname != NULL)
		arg.clientname = (LPTSTR)clientname;

	if (username != NULL)
		arg.username = (LPTSTR)username;

	arg.level = level;
	arg.result.level = level;
	arg.result.bufptr.p = &infonres;
	arg.resume_handle = NULL;
	arg.pref_max_len = (uint32_t)-1;
	rc = ndr_rpc_call(&handle, opnum, &arg);
	if ((rc != 0) || (arg.status != 0)) {
		srvsvc_close(&handle);
		return (-1);
	}

	result = (struct mslm_infonres *)arg.result.bufptr.p;
	nsi1 = (struct mslm_SESSION_INFO_1 *)result->entries;

	sl->sl_level = arg.result.level;
	sl->sl_totalentries = arg.total_entries;
	sl->sl_entriesread = result->entriesread;
	for (i = 0; i < sl->sl_entriesread && i < sl->sl_totalentries; ++i) {
		if ((info = srvsvc_net_enum_add(sl,
		    SMB_SVCENUM_TYPE_USER)) != NULL) {
			ss = &info->l_list.ul_session;
			ss->ui_account =
			    strdup((char *)nsi1[i].sesi1_uname);
			ss->ui_workstation =
			    strdup((char *)nsi1[i].sesi1_cname);
			ss->ui_logon_time = nsi1[i].sesi1_time;
			ss->ui_numopens = nsi1[i].sesi1_nopens;
			ss->ui_flags = nsi1[i].sesi1_uflags;

			if (ss->ui_account == NULL ||
			    ss->ui_workstation == NULL)
				srvsvc_net_enum_remove(sl, info);
		}
	}

	ndr_rpc_release(&handle);
	srvsvc_close(&handle);
	return (0);
}

/*
 * Windows 95+ and Windows NT4.0 both report the version as 4.0.
 * Windows 2000+ reports the version as 5.x.
 */
int
srvsvc_net_server_getinfo(char *server, char *domain,
    srvsvc_server_info_t *svinfo)
{
	mlsvc_handle_t handle;
	struct mslm_NetServerGetInfo arg;
	struct mslm_SERVER_INFO_101 *sv101;
	int opnum, rc;
	char user[SMB_USERNAME_MAXLEN];

	smb_ipc_get_user(user, SMB_USERNAME_MAXLEN);

	if (srvsvc_open(server, domain, user, &handle) != 0)
		return (-1);

	opnum = SRVSVC_OPNUM_NetServerGetInfo;
	bzero(&arg, sizeof (arg));

	arg.servername = ndr_rpc_derive_nbhandle(&handle, server);
	if (arg.servername == NULL) {
		srvsvc_close(&handle);
		return (-1);
	}

	arg.level = 101;

	rc = ndr_rpc_call(&handle, opnum, &arg);
	if ((rc != 0) || (arg.status != 0)) {
		srvsvc_close(&handle);
		return (-1);
	}

	sv101 = arg.result.bufptr.bufptr101;

	bzero(svinfo, sizeof (srvsvc_server_info_t));
	svinfo->sv_platform_id = sv101->sv101_platform_id;
	svinfo->sv_version_major = sv101->sv101_version_major;
	svinfo->sv_version_minor = sv101->sv101_version_minor;
	svinfo->sv_type = sv101->sv101_type;
	if (sv101->sv101_name)
		svinfo->sv_name = strdup((char *)sv101->sv101_name);
	if (sv101->sv101_comment)
		svinfo->sv_comment = strdup((char *)sv101->sv101_comment);

	if (svinfo->sv_type & SV_TYPE_WFW)
		svinfo->sv_os = NATIVE_OS_WIN95;
	if (svinfo->sv_type & SV_TYPE_WINDOWS)
		svinfo->sv_os = NATIVE_OS_WIN95;
	if ((svinfo->sv_type & SV_TYPE_NT) ||
	    (svinfo->sv_type & SV_TYPE_SERVER_NT))
		svinfo->sv_os = NATIVE_OS_WINNT;
	if (svinfo->sv_version_major > 4)
		svinfo->sv_os = NATIVE_OS_WIN2000;

	ndr_rpc_release(&handle);
	srvsvc_close(&handle);
	return (0);
}

/*
 * Synchronize the local system clock with the domain controller.
 */
void
srvsvc_timesync(void)
{
	smb_domainex_t di;
	struct timeval tv;
	struct tm tm;
	time_t tsecs;

	if (!smb_domain_getinfo(&di))
		return;

	if (srvsvc_net_remote_tod(di.d_dc, di.d_primary.di_nbname, &tv, &tm)
	    != 0)
		return;

	if (settimeofday(&tv, 0))
		smb_tracef("unable to set system time");

	tsecs = time(0);
	(void) localtime_r(&tsecs, &tm);
	smb_tracef("SrvsvcTimeSync %s", ctime((time_t *)&tv.tv_sec));
}

/*
 * NetRemoteTOD to get the current GMT time from a Windows NT server.
 */
int
srvsvc_gettime(unsigned long *t)
{
	smb_domainex_t di;
	struct timeval tv;
	struct tm tm;

	if (!smb_domain_getinfo(&di))
		return (-1);

	if (srvsvc_net_remote_tod(di.d_dc, di.d_primary.di_nbname, &tv, &tm)
	    != 0)
		return (-1);

	*t = tv.tv_sec;
	return (0);
}

/*
 * This is a client side routine for NetRemoteTOD, which gets the time
 * and date from a remote system. The time information is returned in
 * the timeval and tm.
 *
 * typedef struct _TIME_OF_DAY_INFO {
 *	DWORD tod_elapsedt;  // seconds since 00:00:00 January 1 1970 GMT
 *	DWORD tod_msecs;     // arbitrary milliseconds (since reset)
 *	DWORD tod_hours;     // current hour [0-23]
 *	DWORD tod_mins;      // current minute [0-59]
 *	DWORD tod_secs;      // current second [0-59]
 *	DWORD tod_hunds;     // current hundredth (0.01) second [0-99]
 *	LONG tod_timezone;   // time zone of the server
 *	DWORD tod_tinterval; // clock tick time interval
 *	DWORD tod_day;       // day of the month [1-31]
 *	DWORD tod_month;     // month of the year [1-12]
 *	DWORD tod_year;      // current year
 *	DWORD tod_weekday;   // day of the week since sunday [0-6]
 * } TIME_OF_DAY_INFO;
 *
 * The time zone of the server is calculated in minutes from Greenwich
 * Mean Time (GMT). For time zones west of Greenwich, the value is
 * positive; for time zones east of Greenwich, the value is negative.
 * A value of -1 indicates that the time zone is undefined.
 *
 * The clock tick value represents a resolution of one ten-thousandth
 * (0.0001) second.
 */
int
srvsvc_net_remote_tod(char *server, char *domain, struct timeval *tv,
    struct tm *tm)
{
	struct mslm_NetRemoteTOD	arg;
	struct mslm_TIME_OF_DAY_INFO	*tod;
	mlsvc_handle_t			handle;
	int				rc;
	int				opnum;
	char				user[SMB_USERNAME_MAXLEN];

	smb_ipc_get_user(user, SMB_USERNAME_MAXLEN);

	rc = srvsvc_open(server, domain, user, &handle);
	if (rc != 0)
		return (-1);

	opnum = SRVSVC_OPNUM_NetRemoteTOD;
	bzero(&arg, sizeof (struct mslm_NetRemoteTOD));

	arg.servername = ndr_rpc_derive_nbhandle(&handle, server);
	if (arg.servername == NULL) {
		srvsvc_close(&handle);
		return (-1);
	}

	rc = ndr_rpc_call(&handle, opnum, &arg);
	if ((rc != 0) || (arg.status != 0)) {
		srvsvc_close(&handle);
		return (-1);
	}

	/*
	 * We're assigning milliseconds to microseconds
	 * here but the value's not really relevant.
	 */
	tod = arg.bufptr;

	if (tv) {
		tv->tv_sec = tod->tod_elapsedt;
		tv->tv_usec = tod->tod_msecs;
	}

	if (tm) {
		tm->tm_sec = tod->tod_secs;
		tm->tm_min = tod->tod_mins;
		tm->tm_hour = tod->tod_hours;
		tm->tm_mday = tod->tod_day;
		tm->tm_mon = tod->tod_month - 1;
		tm->tm_year = tod->tod_year - 1900;
		tm->tm_wday = tod->tod_weekday;
	}

	ndr_rpc_release(&handle);
	srvsvc_close(&handle);
	return (0);
}

void
srvsvc_net_enum_init(srvsvc_list_t *sl)
{
	list_create(&sl->sl_list, sizeof (srvsvc_info_t),
	    offsetof(srvsvc_info_t, l_lnd));
	sl->sl_count = 0;
}

void
srvsvc_net_enum_fini(srvsvc_list_t *sl)
{
	list_t			*lst;
	srvsvc_info_t		*info;

	if (sl == NULL)
		return;

	lst = &sl->sl_list;
	while ((info = list_head(lst)) != NULL) {
		list_remove(lst, info);
		srvsvc_net_free(info);
		free(info);
	}
	list_destroy(lst);
	sl->sl_count = 0;
}

void
srvsvc_net_test(char *server, char *domain, char *netname)
{
	smb_domainex_t di;
	srvsvc_server_info_t svinfo;
	srvsvc_share_info_t shinfo;

	(void) smb_tracef("%s %s %s", server, domain, netname);

	if (smb_domain_getinfo(&di)) {
		server = di.d_dc;
		domain = di.d_primary.di_nbname;
	}

	if (srvsvc_net_server_getinfo(server, domain, &svinfo) == 0) {
		smb_tracef("NetServerGetInfo: %s %s (%d.%d) id=%d type=0x%08x",
		    svinfo.sv_name ? svinfo.sv_name : "NULL",
		    svinfo.sv_comment ? svinfo.sv_comment : "NULL",
		    svinfo.sv_version_major, svinfo.sv_version_minor,
		    svinfo.sv_platform_id, svinfo.sv_type);

		free(svinfo.sv_name);
		free(svinfo.sv_comment);
	}

	if (srvsvc_net_share_get_info(server, domain, netname,
	    srvsvc_info_level, &shinfo) == 0) {
		smb_tracef("NetShareGetInfo: %s %s %s (type=%d) %s",
		    shinfo.si_netname ? shinfo.si_netname : "NULL",
		    shinfo.si_path ? shinfo.si_path : "NULL",
		    shinfo.si_servername ? shinfo.si_servername : "NULL",
		    shinfo.si_type,
		    shinfo.si_comment ? shinfo.si_comment : "NULL");
	}
}
