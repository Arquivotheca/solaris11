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
 * Server Service RPC (SRVSVC) server-side interface definition.
 * The server service provides a remote administration interface.
 *
 * This service uses NERR/Win32 error codes rather than NT status
 * values.
 */

#include <sys/errno.h>
#include <sys/tzfile.h>
#include <unistd.h>
#include <netdb.h>
#include <strings.h>
#include <time.h>
#include <thread.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <libshare.h>
#include <libnvpair.h>
#include <sys/idmap.h>
#include <pwd.h>
#include <nss_dbdefs.h>
#include <smbsrv/libsmb.h>
#include <smbsrv/libntsvcs.h>
#include <smb/nmpipes.h>
#include <smb/smb.h>
#include <smbsrv/netrauth.h>
#include <smbsrv/ndl/srvsvc.ndl>
#include "ntsvcs.h"

#define	SMB_SRVSVC_MAXBUFLEN		(8 * 1024 * 1024)
#define	SMB_SRVSVC_MAXPREFLEN		((uint32_t)(-1))

typedef struct srvsvc_sd {
	uint8_t *sd_buf;
	uint32_t sd_size;
} srvsvc_sd_t;

typedef struct srvsvc_netshare_setinfo {
	char *nss_netname;
	char *nss_comment;
	char *nss_path;
	uint32_t nss_type;
	srvsvc_sd_t nss_sd;
} srvsvc_netshare_setinfo_t;

typedef union srvsvc_netshare_getinfo {
	struct mslm_NetShareInfo_0 nsg_info0;
	struct mslm_NetShareInfo_1 nsg_info1;
	struct mslm_NetShareInfo_2 nsg_info2;
	struct mslm_NetShareInfo_501 nsg_info501;
	struct mslm_NetShareInfo_502 nsg_info502;
	struct mslm_NetShareInfo_503 nsg_info503;
	struct mslm_NetShareInfo_1004 nsg_info1004;
	struct mslm_NetShareInfo_1005 nsg_info1005;
	struct mslm_NetShareInfo_1006 nsg_info1006;
	struct mslm_NetShareInfo_1501 nsg_info1501;
} srvsvc_netshare_getinfo_t;

typedef struct mslm_infonres srvsvc_infonres_t;
typedef struct mslm_NetConnectEnum srvsvc_NetConnectEnum_t;

static uint32_t srvsvc_netconnectenum_level0(ndr_xa_t *, smb_svcenum_t *,
    srvsvc_NetConnectEnum_t *);
static uint32_t srvsvc_netconnectenum_level1(ndr_xa_t *, smb_svcenum_t *,
    srvsvc_NetConnectEnum_t *);
static uint32_t srvsvc_netconnectenum_common(ndr_xa_t *,
    srvsvc_NetConnectInfo_t *, smb_netsvc_t *, smb_svcenum_t *);

static DWORD srvsvc_NetFileEnum2(ndr_xa_t *, struct mslm_NetFileEnum *,
    smb_svcenum_t *se);
static DWORD srvsvc_NetFileEnum3(ndr_xa_t *, struct mslm_NetFileEnum *,
    smb_svcenum_t *se);

static uint32_t srvsvc_NetSessionEnumCommon(ndr_xa_t *, srvsvc_infonres_t *,
    smb_netsvc_t *, smb_svcenum_t *);

static DWORD mlsvc_NetShareEnumCommon(ndr_xa_t *, smb_svcenum_t *,
    smb_share_t *, void *);
static uint32_t srvsvc_share_getsd(ndr_xa_t *, smb_share_t *, srvsvc_sd_t *);
static int srvsvc_share_enum(void *, ndr_xa_t *, boolean_t);
static smb_netsvc_t *srvsvc_shareenum_init(smb_svcenum_t *, smb_netuserinfo_t *,
    boolean_t);

static int srvsvc_netconnect_qualifier(smb_svcenum_t *, const char *);
static void srvsvc_estimate_limit(smb_svcenum_t *, uint32_t);
static uint32_t srvsvc_get_realpath(const char *, char *, int);
static uint32_t srvsvc_open_sessions(void);
static uint32_t srvsvc_open_connections(void);
static uint32_t srvsvc_open_files(void);

static uint32_t srvsvc_modify_share(smb_share_t *,
    srvsvc_netshare_setinfo_t *);
static uint32_t srvsvc_modify_transient_share(smb_share_t *,
    srvsvc_netshare_setinfo_t *);
static uint32_t srvsvc_update_share_flags(smb_share_t *, uint32_t);
static uint32_t srvsvc_get_share_flags(smb_share_t *);

static boolean_t srvsvc_share_is_restricted(const char *);
static boolean_t srvsvc_share_is_admin(const char *);

static uint32_t srvsvc_sa_add(char *, char *, char *);
static uint32_t srvsvc_sa_delete(const char *, const char *);
static uint32_t srvsvc_sa_modify(smb_share_t *, srvsvc_netshare_setinfo_t *);
static uint32_t srvsvc_sa_setprop(smb_share_t *, nvlist_t *);

static ndr_stub_table_t srvsvc_stub_table[];

static ndr_service_t srvsvc_service = {
	"SRVSVC",			/* name */
	"Server services",		/* desc */
	"\\srvsvc",			/* endpoint */
	PIPE_NTSVCS,			/* sec_addr_port */
	"4b324fc8-1670-01d3-1278-5a47bf6ee188", 3,	/* abstract */
	NDR_TRANSFER_SYNTAX_UUID,		2,	/* transfer */
	0,				/* no bind_instance_size */
	0,				/* no bind_req() */
	0,				/* no unbind_and_close() */
	0,				/* use generic_call_stub() */
	&TYPEINFO(srvsvc_interface),	/* interface ti */
	srvsvc_stub_table		/* stub_table */
};

/*
 * srvsvc_initialize
 *
 * This function registers the SRVSVC RPC interface with the RPC runtime
 * library. It must be called in order to use either the client side
 * or the server side functions.
 */
void
srvsvc_initialize(void)
{
	(void) ndr_svc_register(&srvsvc_service);
}

/*
 * Turn "dfsroot" property on/off for the specified
 * share and save it.
 *
 * If the requested value is the same as what is already
 * set then no change is required and the function returns.
 */
uint32_t
srvsvc_shr_setdfsroot(smb_share_t *si, boolean_t on)
{
	char *dfs = NULL;
	nvlist_t *nvl;
	uint32_t nerr;

	if (on && ((si->shr_flags & SMB_SHRF_DFSROOT) == 0)) {
		si->shr_flags |= SMB_SHRF_DFSROOT;
		dfs = "true";
	} else if (!on && (si->shr_flags & SMB_SHRF_DFSROOT)) {
		si->shr_flags &= ~SMB_SHRF_DFSROOT;
		dfs = "false";
	}

	if (dfs == NULL)
		return (ERROR_SUCCESS);

	if (nvlist_alloc(&nvl, NV_UNIQUE_NAME, 0) != 0)
		return (NERR_InternalError);

	if (nvlist_add_string(nvl, SHOPT_DFSROOT, dfs) != 0) {
		nvlist_free(nvl);
		return (NERR_InternalError);
	}

	nerr = srvsvc_sa_setprop(si, nvl);
	nvlist_free(nvl);

	return (nerr);
}

/*
 * srvsvc_s_NetConnectEnum
 *
 * List tree connections made to a share on this server or all tree
 * connections established from a specific client.  Administrator,
 * Server Operator, Print Operator or Power User group membership
 * is required to use this interface.
 *
 * There are three information levels:  0, 1, and 50.  We don't support
 * level 50, which is only used by Windows 9x clients.
 *
 * It seems Server Manger (srvmgr) only sends workstation as the qualifier
 * and the Computer Management Interface on Windows 2000 doesn't request
 * a list of connections.
 *
 * Return Values:
 * ERROR_SUCCESS            Success
 * ERROR_ACCESS_DENIED      Caller does not have access to this call.
 * ERROR_INVALID_PARAMETER  One of the parameters is invalid.
 * ERROR_INVALID_LEVEL      Unknown information level specified.
 * ERROR_MORE_DATA          Partial date returned, more entries available.
 * ERROR_NOT_ENOUGH_MEMORY  Insufficient memory is available.
 * NERR_NetNameNotFound     The share qualifier cannot be found.
 * NERR_BufTooSmall         The supplied buffer is too small.
 */
static int
srvsvc_s_NetConnectEnum(void *arg, ndr_xa_t *mxa)
{
	srvsvc_NetConnectEnum_t		*param = arg;
	smb_netsvc_t			*ns;
	smb_svcenum_t			se;
	char				*qualifier;
	DWORD				status = ERROR_SUCCESS;

	if (!ndr_is_poweruser(mxa)) {
		status = ERROR_ACCESS_DENIED;
		goto srvsvc_netconnectenum_error;
	}

	param->total_entries = srvsvc_open_connections();
	if (param->total_entries == 0) {
		bzero(param, sizeof (srvsvc_NetConnectEnum_t));
		param->status = ERROR_SUCCESS;
		return (NDR_DRC_OK);
	}

	bzero(&se, sizeof (smb_svcenum_t));
	se.se_type = SMB_SVCENUM_TYPE_TREE;
	se.se_level = param->info.level;
	se.se_ntotal = param->total_entries;
	se.se_nlimit = se.se_ntotal;

	if (param->pref_max_len == SMB_SRVSVC_MAXPREFLEN ||
	    param->pref_max_len > SMB_SRVSVC_MAXBUFLEN)
		se.se_prefmaxlen = SMB_SRVSVC_MAXBUFLEN;
	else
		se.se_prefmaxlen = param->pref_max_len;

	if (param->resume_handle) {
		se.se_resume = *param->resume_handle;
		se.se_nskip = se.se_resume;
		*param->resume_handle = 0;
	}

	switch (param->info.level) {
	case 0:
		status = srvsvc_netconnectenum_level0(mxa, &se, param);
		break;
	case 1:
		status = srvsvc_netconnectenum_level1(mxa, &se, param);
		break;
	case 50:
		status = ERROR_NOT_SUPPORTED;
		break;
	default:
		status = ERROR_INVALID_LEVEL;
		break;
	}

	if (status != ERROR_SUCCESS)
		goto srvsvc_netconnectenum_error;

	qualifier = (char *)param->qualifier;
	if (srvsvc_netconnect_qualifier(&se, qualifier) != 0) {
		status = NERR_NetNameNotFound;
		goto srvsvc_netconnectenum_error;
	}

	if ((ns = smb_kmod_enum_init(&se)) == NULL) {
		status = ERROR_NOT_ENOUGH_MEMORY;
		goto srvsvc_netconnectenum_error;
	}

	status = srvsvc_netconnectenum_common(mxa, &param->info, ns, &se);
	smb_kmod_enum_fini(ns);

	if (status != ERROR_SUCCESS)
		goto srvsvc_netconnectenum_error;

	if (param->resume_handle &&
	    param->pref_max_len != SMB_SRVSVC_MAXPREFLEN) {
		if (se.se_resume < param->total_entries) {
			*param->resume_handle = se.se_resume;
			status = ERROR_MORE_DATA;
		}
	}

	param->status = status;
	return (NDR_DRC_OK);

srvsvc_netconnectenum_error:
	bzero(param, sizeof (srvsvc_NetConnectEnum_t));
	param->status = status;
	return (NDR_DRC_OK);
}

/*
 * Allocate memory and estimate the number of objects that can
 * be returned for NetConnectEnum level 0.
 */
static uint32_t
srvsvc_netconnectenum_level0(ndr_xa_t *mxa, smb_svcenum_t *se,
    srvsvc_NetConnectEnum_t *param)
{
	srvsvc_NetConnectInfo0_t	*info0;
	srvsvc_NetConnectInfoBuf0_t	*ci0;

	if ((info0 = NDR_NEW(mxa, srvsvc_NetConnectInfo0_t)) == NULL)
		return (ERROR_NOT_ENOUGH_MEMORY);

	bzero(info0, sizeof (srvsvc_NetConnectInfo0_t));
	param->info.ru.info0 = info0;

	srvsvc_estimate_limit(se, sizeof (srvsvc_NetConnectInfoBuf0_t));
	if (se->se_nlimit == 0)
		return (NERR_BufTooSmall);

	do {
		ci0 = NDR_NEWN(mxa, srvsvc_NetConnectInfoBuf0_t, se->se_nlimit);
		if (ci0 == NULL)
			se->se_nlimit >>= 1;
	} while ((se->se_nlimit > 0) && (ci0 == NULL));

	if (ci0 == NULL)
		return (ERROR_NOT_ENOUGH_MEMORY);

	info0->ci0 = ci0;
	info0->entries_read = 0;
	return (ERROR_SUCCESS);
}

/*
 * Allocate memory and estimate the number of objects that can
 * be returned for NetConnectEnum level 1.
 */
static uint32_t
srvsvc_netconnectenum_level1(ndr_xa_t *mxa, smb_svcenum_t *se,
    srvsvc_NetConnectEnum_t *param)
{
	srvsvc_NetConnectInfo1_t	*info1;
	srvsvc_NetConnectInfoBuf1_t	*ci1;

	if ((info1 = NDR_NEW(mxa, srvsvc_NetConnectInfo1_t)) == NULL)
		return (ERROR_NOT_ENOUGH_MEMORY);

	bzero(info1, sizeof (srvsvc_NetConnectInfo1_t));
	param->info.ru.info1 = info1;

	srvsvc_estimate_limit(se,
	    sizeof (srvsvc_NetConnectInfoBuf1_t) + MAXNAMELEN);
	if (se->se_nlimit == 0)
		return (NERR_BufTooSmall);

	do {
		ci1 = NDR_NEWN(mxa, srvsvc_NetConnectInfoBuf1_t, se->se_nlimit);
		if (ci1 == NULL)
			se->se_nlimit >>= 1;
	} while ((se->se_nlimit > 0) && (ci1 == NULL));

	if (ci1 == NULL)
		return (ERROR_NOT_ENOUGH_MEMORY);

	info1->ci1 = ci1;
	info1->entries_read = 0;
	return (ERROR_SUCCESS);
}

/*
 * Request a list of connections from the kernel and set up
 * the connection information to be returned to the client.
 */
static uint32_t
srvsvc_netconnectenum_common(ndr_xa_t *mxa, srvsvc_NetConnectInfo_t *info,
    smb_netsvc_t *ns, smb_svcenum_t *se)
{
	srvsvc_NetConnectInfo0_t	*info0;
	srvsvc_NetConnectInfo1_t	*info1;
	srvsvc_NetConnectInfoBuf0_t	*ci0;
	srvsvc_NetConnectInfoBuf1_t	*ci1;
	smb_netsvcitem_t		*item;
	smb_netconnectinfo_t		*tree;

	if (smb_kmod_enum(ns) != 0)
		return (ERROR_INTERNAL_ERROR);

	info0 = info->ru.info0;
	ci0 = info0->ci0;

	info1 = info->ru.info1;
	ci1 = info1->ci1;

	item = list_head(&ns->ns_list);
	while (item != NULL) {
		tree = &item->nsi_un.nsi_tree;

		switch (se->se_level) {
		case 0:
			ci0->coni0_id = tree->ci_id;
			++ci0;
			++info0->entries_read;
			break;
		case 1:
			ci1->coni1_id = tree->ci_id;
			ci1->coni1_type = tree->ci_type;
			ci1->coni1_num_opens = tree->ci_numopens;
			ci1->coni1_num_users = tree->ci_numusers;
			ci1->coni1_time = tree->ci_time;
			ci1->coni1_username = (uint8_t *)
			    NDR_STRDUP(mxa, tree->ci_username);
			ci1->coni1_netname = (uint8_t *)
			    NDR_STRDUP(mxa, tree->ci_share);
			++ci1;
			++info1->entries_read;
			break;
		default:
			return (ERROR_INVALID_LEVEL);
		}

		++se->se_resume;
		item = list_next(&ns->ns_list, item);
	}

	return (ERROR_SUCCESS);
}

/*
 * srvsvc_netconnect_qualifier
 *
 * The qualifier is a string that specifies a share name or computer name
 * for the connections of interest.  If it is a share name then all the
 * connections made to that share name are listed.  If it is a computer
 * name (it starts with two backslash characters), then NetConnectEnum
 * lists all connections made from that computer to the specified server.
 */
static int
srvsvc_netconnect_qualifier(smb_svcenum_t *se, const char *qualifier)
{
	if (qualifier == NULL || *qualifier == '\0')
		return (-1);

	if (strlen(qualifier) > MAXHOSTNAMELEN)
		return (-1);

	(void) strlcpy(se->se_qualifier.seq_qualstr, qualifier,
	    sizeof (se->se_qualifier.seq_qualstr));
	if (qualifier[0] == '\\' && qualifier[1] == '\\') {
		se->se_qualifier.seq_mode = SMB_SVCENUM_CONNECT_WKSTN;
	} else {
		if (!smb_share_exists((char *)qualifier))
			return (-1);

		se->se_qualifier.seq_mode = SMB_SVCENUM_CONNECT_SHARE;
	}

	return (0);
}

static uint32_t
srvsvc_open_sessions(void)
{
	smb_opennum_t	opennum;

	bzero(&opennum, sizeof (smb_opennum_t));
	if (smb_kmod_get_open_num(&opennum) != 0)
		return (0);

	return (opennum.open_users);
}

static uint32_t
srvsvc_open_connections(void)
{
	smb_opennum_t	opennum;

	bzero(&opennum, sizeof (smb_opennum_t));
	if (smb_kmod_get_open_num(&opennum) != 0)
		return (0);

	return (opennum.open_trees);
}

static uint32_t
srvsvc_open_files(void)
{
	smb_opennum_t	opennum;

	bzero(&opennum, sizeof (smb_opennum_t));
	if (smb_kmod_get_open_num(&opennum) != 0)
		return (0);

	return (opennum.open_files);
}

/*
 * srvsvc_s_NetFileEnum
 *
 * Return information on open files or named pipes. Only members of the
 * Administrators or Server Operators local groups are allowed to make
 * this call. Currently, we only support Administrators.
 *
 * If basepath is null, all open resources are enumerated. If basepath
 * is non-null, only resources that have basepath as a prefix should
 * be returned.
 *
 * If username is specified (non-null), only files opened by username
 * should be returned.
 *
 * Notes:
 * 1. We don't validate the servername because we would have to check
 * all primary IPs and the ROI seems unlikely to be worth it.
 * 2. Both basepath and username are currently ignored because both
 * Server Manger (NT 4.0) and CMI (Windows 2000) always set them to null.
 *
 * The level of information requested may be one of:
 *
 *  2   Return the file identification number.
 *      This level is not supported on Windows Me/98/95.
 *
 *  3   Return information about the file.
 *      This level is not supported on Windows Me/98/95.
 *
 *  50  Windows Me/98/95:  Return information about the file.
 *
 * Note:
 * If pref_max_len is unlimited and resume_handle is null, the client
 * expects to receive all data in a single call.
 * If we are unable to do fit all data in a single response, we would
 * normally return ERROR_MORE_DATA with a partial list.
 *
 * Unfortunately, when both of these conditions occur, Server Manager
 * pops up an error box with the message "more data available" and
 * doesn't display any of the returned data. In this case, it is
 * probably better to return ERROR_SUCCESS with the partial list.
 * Windows 2000 doesn't have this problem because it always sends a
 * non-null resume_handle.
 *
 * Return Values:
 * ERROR_SUCCESS            Success
 * ERROR_ACCESS_DENIED      Caller does not have access to this call.
 * ERROR_INVALID_PARAMETER  One of the parameters is invalid.
 * ERROR_INVALID_LEVEL      Unknown information level specified.
 * ERROR_MORE_DATA          Partial date returned, more entries available.
 * ERROR_NOT_ENOUGH_MEMORY  Insufficient memory is available.
 * NERR_BufTooSmall         The supplied buffer is too small.
 */
static int
srvsvc_s_NetFileEnum(void *arg, ndr_xa_t *mxa)
{
	struct mslm_NetFileEnum	*param = arg;
	smb_svcenum_t		se;
	DWORD			status;

	if (!ndr_is_admin(mxa)) {
		bzero(param, sizeof (struct mslm_NetFileEnum));
		param->status = ERROR_ACCESS_DENIED;
		return (NDR_DRC_OK);
	}

	if ((param->total_entries = srvsvc_open_files()) == 0) {
		bzero(param, sizeof (struct mslm_NetFileEnum));
		param->status = ERROR_SUCCESS;
		return (NDR_DRC_OK);
	}

	bzero(&se, sizeof (smb_svcenum_t));
	se.se_type = SMB_SVCENUM_TYPE_FILE;
	se.se_level = param->info.switch_value;
	se.se_ntotal = param->total_entries;
	se.se_nlimit = se.se_ntotal;

	if (param->pref_max_len == SMB_SRVSVC_MAXPREFLEN ||
	    param->pref_max_len > SMB_SRVSVC_MAXBUFLEN)
		se.se_prefmaxlen = SMB_SRVSVC_MAXBUFLEN;
	else
		se.se_prefmaxlen = param->pref_max_len;

	if (param->resume_handle) {
		se.se_resume = *param->resume_handle;
		se.se_nskip = se.se_resume;
		*param->resume_handle = 0;
	}

	switch (param->info.switch_value) {
	case 2:
		status = srvsvc_NetFileEnum2(mxa, param, &se);
		break;

	case 3:
		status = srvsvc_NetFileEnum3(mxa, param, &se);
		break;

	case 50:
		status = ERROR_NOT_SUPPORTED;
		break;

	default:
		status = ERROR_INVALID_LEVEL;
		break;
	}

	if (status != ERROR_SUCCESS) {
		bzero(param, sizeof (struct mslm_NetFileEnum));
		param->status = status;
		return (NDR_DRC_OK);
	}

	if (param->resume_handle &&
	    param->pref_max_len != SMB_SRVSVC_MAXPREFLEN) {
		if (se.se_resume < param->total_entries) {
			*param->resume_handle = se.se_resume;
			status = ERROR_MORE_DATA;
		}
	}

	param->status = status;
	return (NDR_DRC_OK);
}

/*
 * Build level 2 file information.
 *
 * SMB fids are 16-bit values but this interface expects 32-bit file ids.
 * So we use the uniqid here.
 *
 * On success, the caller expects that the info2, fi2 and entries_read
 * fields have been set up.
 */
static DWORD
srvsvc_NetFileEnum2(ndr_xa_t *mxa, struct mslm_NetFileEnum *param,
    smb_svcenum_t *se)
{
	struct mslm_NetFileInfoBuf2	*fi2;
	smb_netsvc_t			*ns;
	smb_netsvcitem_t		*item;
	smb_netfileinfo_t		*ofile;
	uint32_t			entries_read = 0;

	param->info.ru.info2 = NDR_NEW(mxa, struct mslm_NetFileInfo2);
	if (param->info.ru.info2 == NULL)
		return (ERROR_NOT_ENOUGH_MEMORY);

	srvsvc_estimate_limit(se, sizeof (struct mslm_NetFileInfoBuf2));
	if (se->se_nlimit == 0)
		return (NERR_BufTooSmall);

	do {
		fi2 = NDR_NEWN(mxa, struct mslm_NetFileInfoBuf2, se->se_nlimit);
		if (fi2 == NULL)
			se->se_nlimit >>= 1;
	} while ((se->se_nlimit > 0) && (fi2 == NULL));

	if (fi2 == NULL)
		return (ERROR_NOT_ENOUGH_MEMORY);

	param->info.ru.info2->fi2 = fi2;

	if ((ns = smb_kmod_enum_init(se)) == NULL)
		return (ERROR_NOT_ENOUGH_MEMORY);

	if (smb_kmod_enum(ns) != 0) {
		smb_kmod_enum_fini(ns);
		return (ERROR_INTERNAL_ERROR);
	}

	item = list_head(&ns->ns_list);
	while (item != NULL) {
		ofile = &item->nsi_un.nsi_ofile;
		fi2->fi2_id = ofile->fi_uniqid;

		++entries_read;
		++fi2;
		item = list_next(&ns->ns_list, item);
	}

	se->se_resume += entries_read;
	param->info.ru.info2->entries_read = entries_read;
	smb_kmod_enum_fini(ns);
	return (ERROR_SUCCESS);
}

/*
 * Build level 3 file information.
 *
 * SMB fids are 16-bit values but this interface expects 32-bit file ids.
 * So we use the uniqid here.
 *
 * On success, the caller expects that the info3, fi3 and entries_read
 * fields have been set up.
 */
static DWORD
srvsvc_NetFileEnum3(ndr_xa_t *mxa, struct mslm_NetFileEnum *param,
    smb_svcenum_t *se)
{
	struct mslm_NetFileInfoBuf3	*fi3;
	smb_netsvc_t			*ns;
	smb_netsvcitem_t		*item;
	smb_netfileinfo_t		*ofile;
	uint32_t			entries_read = 0;

	param->info.ru.info3 = NDR_NEW(mxa, struct mslm_NetFileInfo3);
	if (param->info.ru.info3 == NULL)
		return (ERROR_NOT_ENOUGH_MEMORY);

	srvsvc_estimate_limit(se,
	    sizeof (struct mslm_NetFileInfoBuf3) + MAXNAMELEN);
	if (se->se_nlimit == 0)
		return (NERR_BufTooSmall);

	do {
		fi3 = NDR_NEWN(mxa, struct mslm_NetFileInfoBuf3, se->se_nlimit);
		if (fi3 == NULL)
			se->se_nlimit >>= 1;
	} while ((se->se_nlimit > 0) && (fi3 == NULL));

	if (fi3 == NULL)
		return (ERROR_NOT_ENOUGH_MEMORY);

	param->info.ru.info3->fi3 = fi3;

	if ((ns = smb_kmod_enum_init(se)) == NULL)
		return (ERROR_NOT_ENOUGH_MEMORY);

	if (smb_kmod_enum(ns) != 0) {
		smb_kmod_enum_fini(ns);
		return (ERROR_INTERNAL_ERROR);
	}

	item = list_head(&ns->ns_list);
	while (item != NULL) {
		ofile = &item->nsi_un.nsi_ofile;
		fi3->fi3_id = ofile->fi_uniqid;
		fi3->fi3_permissions = ofile->fi_permissions;
		fi3->fi3_num_locks = ofile->fi_numlocks;
		fi3->fi3_pathname = (uint8_t *)
		    NDR_STRDUP(mxa, ofile->fi_path);
		fi3->fi3_username = (uint8_t *)
		    NDR_STRDUP(mxa, ofile->fi_username);

		++entries_read;
		++fi3;
		item = list_next(&ns->ns_list, item);
	}

	se->se_resume += entries_read;
	param->info.ru.info3->entries_read = entries_read;
	param->total_entries = entries_read;
	smb_kmod_enum_fini(ns);
	return (ERROR_SUCCESS);
}

/*
 * srvsvc_s_NetFileClose
 *
 * NetFileClose forces a file to close. This function can be used when
 * an error prevents closure by other means.  Use NetFileClose with
 * caution because it does not flush data, cached on a client, to the
 * file before closing the file.
 *
 * SMB fids are 16-bit values but this interface expects 32-bit file ids.
 * So we use the uniqid here.
 *
 * Return Values
 * ERROR_SUCCESS            Operation succeeded.
 * ERROR_ACCESS_DENIED      Operation denied.
 * NERR_FileIdNotFound      No open file with the specified id.
 *
 * Note: MSDN suggests ERROR_FILE_NOT_FOUND for NetFileClose but network
 * captures using NT show NERR_FileIdNotFound, which is consistent with
 * the NetFileClose2 page on MSDN.
 */
static int
srvsvc_s_NetFileClose(void *arg, ndr_xa_t *mxa)
{
	static struct {
		int errnum;
		int nerr;
	} errmap[] = {
		0,	ERROR_SUCCESS,
		EACCES,	ERROR_ACCESS_DENIED,
		EPERM,	ERROR_ACCESS_DENIED,
		EINVAL,	ERROR_INVALID_PARAMETER,
		ENOMEM,	ERROR_NOT_ENOUGH_MEMORY,
		ENOENT,	NERR_FileIdNotFound
	};

	struct mslm_NetFileClose *param = arg;
	int		i;
	int		rc;

	if (!ndr_is_admin(mxa)) {
		param->status = ERROR_ACCESS_DENIED;
		return (NDR_DRC_OK);
	}

	rc = smb_kmod_file_close(param->file_id);

	for (i = 0; i < (sizeof (errmap) / sizeof (errmap[0])); ++i) {
		if (rc == errmap[i].errnum) {
			param->status = errmap[i].nerr;
			return (NDR_DRC_OK);
		}
	}

	param->status = ERROR_INTERNAL_ERROR;
	return (NDR_DRC_OK);
}

/*
 * srvsvc_s_NetShareGetInfo
 *
 * Returns Win32 error codes.
 */
static int
srvsvc_s_NetShareGetInfo(void *arg, ndr_xa_t *mxa)
{
	struct mlsm_NetShareGetInfo *param = arg;
	struct mslm_NetShareInfo_0 *info0;
	struct mslm_NetShareInfo_1 *info1;
	struct mslm_NetShareInfo_2 *info2;
	struct mslm_NetShareInfo_501 *info501;
	struct mslm_NetShareInfo_502 *info502;
	struct mslm_NetShareInfo_503 *info503;
	struct mslm_NetShareInfo_1004 *info1004;
	struct mslm_NetShareInfo_1005 *info1005;
	struct mslm_NetShareInfo_1006 *info1006;
	struct mslm_NetShareInfo_1501 *info1501;
	srvsvc_netshare_getinfo_t *info;
	uint8_t *netname;
	uint8_t *comment;
	uint8_t *path;
	smb_share_t si;
	srvsvc_sd_t sd;
	DWORD status;

	status = smb_share_lookup((char *)param->netname, &si);
	if (status != NERR_Success) {
		bzero(param, sizeof (struct mlsm_NetShareGetInfo));
		param->status = status;
		return (NDR_DRC_OK);
	}

	netname = (uint8_t *)NDR_STRDUP(mxa, si.shr_name);
	comment = (uint8_t *)NDR_STRDUP(mxa, si.shr_cmnt);
	path = (uint8_t *)NDR_STRDUP(mxa, si.shr_winpath);
	info = NDR_NEW(mxa, srvsvc_netshare_getinfo_t);
	bzero(info, sizeof (srvsvc_netshare_getinfo_t));

	if (netname == NULL || comment == NULL || info == NULL) {
		bzero(param, sizeof (struct mlsm_NetShareGetInfo));
		param->status = ERROR_NOT_ENOUGH_MEMORY;
		smb_share_free(&si);
		return (NDR_DRC_OK);
	}

	switch (param->level) {
	case 0:
		info0 = &info->nsg_info0;
		info0->shi0_netname = netname;
		param->result.ru.info0 = info0;
		break;

	case 1:
		info1 = &info->nsg_info1;
		info1->shi1_netname = netname;
		info1->shi1_comment = comment;
		info1->shi1_type = si.shr_type;
		param->result.ru.info1 = info1;
		break;

	case 2:
		info2 = &info->nsg_info2;
		param->result.ru.info2 = info2;

		if (!ndr_is_poweruser(mxa)) {
			status = ERROR_ACCESS_DENIED;
			break;
		}

		info2->shi2_netname = netname;
		info2->shi2_comment = comment;
		info2->shi2_path = path;
		info2->shi2_passwd = 0;
		info2->shi2_type = si.shr_type;
		info2->shi2_permissions = 0;
		info2->shi2_max_uses = SHI_USES_UNLIMITED;
		info2->shi2_current_uses = 0;
		break;

	case 501:
		info501 = &info->nsg_info501;
		info501->shi501_netname = netname;
		info501->shi501_comment = comment;
		info501->shi501_type = si.shr_type;
		info501->shi501_flags = srvsvc_get_share_flags(&si);
		param->result.ru.info501 = info501;
		break;

	case 502:
		info502 = &info->nsg_info502;
		param->result.ru.info502 = info502;

		if (!ndr_is_poweruser(mxa)) {
			status = ERROR_ACCESS_DENIED;
			break;
		}

		info502->shi502_netname = netname;
		info502->shi502_comment = comment;
		info502->shi502_path = path;
		info502->shi502_passwd = 0;
		info502->shi502_type = si.shr_type;
		info502->shi502_permissions = 0;
		info502->shi502_max_uses = SHI_USES_UNLIMITED;
		info502->shi502_current_uses = 0;

		status = srvsvc_share_getsd(mxa, &si, &sd);
		if (status == ERROR_SUCCESS) {
			info502->shi502_reserved = sd.sd_size;
			info502->shi502_security_descriptor = sd.sd_buf;
		} else {
			info502->shi502_reserved = 0;
			info502->shi502_security_descriptor = NULL;
		}
		break;

	case 503:
		info503 = &info->nsg_info503;
		param->result.ru.info503 = info503;

		if (!ndr_is_poweruser(mxa)) {
			status = ERROR_ACCESS_DENIED;
			break;
		}

		info503->shi503_netname = netname;
		info503->shi503_comment = comment;
		info503->shi503_path = path;
		info503->shi503_passwd = NULL;
		info503->shi503_type = si.shr_type;
		info503->shi503_permissions = 0;
		info503->shi503_max_uses = SHI_USES_UNLIMITED;
		info503->shi503_current_uses = 0;
		info503->shi503_servername = NULL;

		status = srvsvc_share_getsd(mxa, &si, &sd);
		if (status == ERROR_SUCCESS) {
			info503->shi503_reserved = sd.sd_size;
			info503->shi503_security_descriptor = sd.sd_buf;
		} else {
			info503->shi503_reserved = 0;
			info503->shi503_security_descriptor = NULL;
		}
		break;

	case 1004:
		info1004 = &info->nsg_info1004;
		info1004->shi1004_comment = comment;
		param->result.ru.info1004 = info1004;
		break;

	case 1005:
		info1005 = &info->nsg_info1005;
		info1005->shi1005_flags = srvsvc_get_share_flags(&si);
		param->result.ru.info1005 = info1005;
		break;

	case 1006:
		info1006 = &info->nsg_info1006;
		info1006->shi1006_max_uses = SHI_USES_UNLIMITED;
		param->result.ru.info1006 = info1006;
		break;

	case 1501:
		info1501 = &info->nsg_info1501;

		status = srvsvc_share_getsd(mxa, &si, &sd);
		if (status == ERROR_SUCCESS) {
			info503->shi503_reserved = sd.sd_size;
			info503->shi503_security_descriptor = sd.sd_buf;
		} else {
			info503->shi503_reserved = 0;
			info503->shi503_security_descriptor = NULL;
		}

		param->result.ru.info1501 = info1501;
		break;

	default:
		status = ERROR_ACCESS_DENIED;
		break;
	}

	smb_share_free(&si);

	param->result.switch_value = param->level;
	param->status = status;
	return (NDR_DRC_OK);
}

static uint32_t
srvsvc_share_getsd(ndr_xa_t *mxa, smb_share_t *si, srvsvc_sd_t *sd)
{
	uint32_t status;

	status = srvsvc_sd_get(si, NULL, &sd->sd_size);
	if (status != ERROR_SUCCESS) {
		if (status == ERROR_PATH_NOT_FOUND) {
			bzero(sd, sizeof (srvsvc_sd_t));
			status = ERROR_SUCCESS;
		}

		return (status);
	}

	if ((sd->sd_buf = NDR_MALLOC(mxa, sd->sd_size)) == NULL)
		return (ERROR_NOT_ENOUGH_MEMORY);

	status = srvsvc_sd_get(si, sd->sd_buf, NULL);
	if (status == ERROR_PATH_NOT_FOUND) {
		bzero(sd, sizeof (srvsvc_sd_t));
		status = ERROR_SUCCESS;
	}

	return (status);
}

/*
 * srvsvc_s_NetShareSetInfo
 *
 * This call is made by SrvMgr to set share information.
 * Only power users groups can manage shares.
 *
 * To avoid misleading errors, we don't report an error
 * when a FS doesn't support ACLs on shares.
 *
 * Returns Win32 error codes.
 */
static int
srvsvc_s_NetShareSetInfo(void *arg, ndr_xa_t *mxa)
{
	struct mlsm_NetShareSetInfo *param = arg;
	struct mslm_NetShareInfo_1 *info1;
	struct mslm_NetShareInfo_2 *info2;
	struct mslm_NetShareInfo_501 *info501;
	struct mslm_NetShareInfo_502 *info502;
	struct mslm_NetShareInfo_503 *info503;
	struct mslm_NetShareInfo_1004 *info1004;
	struct mslm_NetShareInfo_1005 *info1005;
	struct mslm_NetShareInfo_1501 *info1501;
	static DWORD parm_err = 0;
	srvsvc_netshare_setinfo_t info;
	smb_share_t si;
	uint8_t *sdbuf;
	int32_t native_os;
	DWORD status;

	native_os = ndr_native_os(mxa);

	bzero(&si, sizeof (smb_share_t));

	if (!ndr_is_poweruser(mxa)) {
		status = ERROR_ACCESS_DENIED;
		goto netsharesetinfo_exit;
	}

	if (param->result.ru.nullptr == NULL) {
		status = ERROR_INVALID_PARAMETER;
		goto netsharesetinfo_exit;
	}

	if (smb_share_lookup((char *)param->netname, &si) != NERR_Success) {
		status = ERROR_INVALID_NETNAME;
		goto netsharesetinfo_exit;
	}

	bzero(&info, sizeof (srvsvc_netshare_setinfo_t));

	switch (param->level) {
	case 0:
		status = ERROR_INVALID_LEVEL;
		break;

	case 1:
		info1 = (struct mslm_NetShareInfo_1 *)param->result.ru.info1;
		info.nss_netname = (char *)info1->shi1_netname;
		info.nss_comment = (char *)info1->shi1_comment;
		info.nss_type = info1->shi1_type;
		status = srvsvc_modify_share(&si, &info);
		break;

	case 2:
		info2 = (struct mslm_NetShareInfo_2 *)param->result.ru.info2;
		info.nss_netname = (char *)info2->shi2_netname;
		info.nss_comment = (char *)info2->shi2_comment;
		info.nss_path = (char *)info2->shi2_path;
		info.nss_type = info2->shi2_type;
		status = srvsvc_modify_share(&si, &info);
		break;

	case 501:
		info501 = (struct mslm_NetShareInfo_501 *)
		    param->result.ru.info501;
		info.nss_netname = (char *)info501->shi501_netname;
		info.nss_comment = (char *)info501->shi501_comment;
		info.nss_type = info501->shi501_type;
		status = srvsvc_modify_share(&si, &info);
		if (status == ERROR_SUCCESS)
			status = srvsvc_update_share_flags(&si,
			    info501->shi501_flags);
		break;

	case 502:
		info502 = (struct mslm_NetShareInfo_502 *)
		    param->result.ru.info502;
		info.nss_netname = (char *)info502->shi502_netname;
		info.nss_comment = (char *)info502->shi502_comment;
		info.nss_path = (char *)info502->shi502_path;
		info.nss_type = info502->shi502_type;
		info.nss_sd.sd_buf = info502->shi502_security_descriptor;
		status = srvsvc_modify_share(&si, &info);
		break;

	case 503:
		info503 = (struct mslm_NetShareInfo_503 *)
		    param->result.ru.info503;
		info.nss_netname = (char *)info503->shi503_netname;
		info.nss_comment = (char *)info503->shi503_comment;
		info.nss_path = (char *)info503->shi503_path;
		info.nss_type = info503->shi503_type;
		info.nss_sd.sd_buf = info503->shi503_security_descriptor;
		status = srvsvc_modify_share(&si, &info);
		break;

	case 1004:
		info1004 = (struct mslm_NetShareInfo_1004 *)
		    param->result.ru.info1004;
		info.nss_comment = (char *)info1004->shi1004_comment;
		status = srvsvc_modify_share(&si, &info);
		break;

	case 1005:
		info1005 = (struct mslm_NetShareInfo_1005 *)
		    param->result.ru.info1005;
		status = srvsvc_update_share_flags(&si,
		    info1005->shi1005_flags);
		break;

	case 1006:
		/*
		 * We don't limit the maximum number of concurrent
		 * connections to a share.
		 */
		status = ERROR_SUCCESS;
		break;

	case 1501:
		info1501 = (struct mslm_NetShareInfo_1501 *)
		    param->result.ru.info1501;
		sdbuf = info1501->shi1501_security_descriptor;
		status = ERROR_SUCCESS;

		if (sdbuf != NULL) {
			status = srvsvc_sd_set(&si, sdbuf);
			if (status == ERROR_PATH_NOT_FOUND)
				status = ERROR_SUCCESS;
		}
		break;

	default:
		status = ERROR_ACCESS_DENIED;
		break;
	}

netsharesetinfo_exit:
	smb_share_free(&si);
	if (status != ERROR_SUCCESS)
		bzero(param, sizeof (struct mlsm_NetShareSetInfo));

	param->parm_err = (native_os == NATIVE_OS_WIN95) ? 0 : &parm_err;
	param->status = status;
	return (NDR_DRC_OK);
}

static uint32_t
srvsvc_modify_share(smb_share_t *si, srvsvc_netshare_setinfo_t *info)
{
	uint32_t nerr = NERR_Success;

	if (si->shr_flags & SMB_SHRF_TRANS)
		return (srvsvc_modify_transient_share(si, info));

	if (info->nss_sd.sd_buf != NULL) {
		nerr = srvsvc_sd_set(si, info->nss_sd.sd_buf);
		if (nerr == ERROR_PATH_NOT_FOUND)
			nerr = NERR_Success;
	}

	return (srvsvc_sa_modify(si, info));
}

/*
 * Update transient shares.  This includes autohome shares.
 */
static uint32_t
srvsvc_modify_transient_share(smb_share_t *si, srvsvc_netshare_setinfo_t *info)
{
	uint32_t	nerr = NERR_Success;
	boolean_t	new_cmnt = B_FALSE;
	char		*newname = NULL;
	char		*curname;

	if (info->nss_netname != NULL && info->nss_netname[0] != '\0' &&
	    smb_strcasecmp(info->nss_netname, si->shr_name, 0) != 0) {
		newname = info->nss_netname;
	}

	if (info->nss_comment == NULL) {
		if (si->shr_cmnt != NULL) {
			free(si->shr_cmnt);
			si->shr_cmnt = NULL;
			new_cmnt = B_TRUE;
		}
	} else {
		if ((si->shr_cmnt == NULL) ||
		    strcmp(info->nss_comment, si->shr_cmnt) != 0) {
			free(si->shr_cmnt);
			if ((si->shr_cmnt = strdup(info->nss_comment)) == NULL)
				return (ERROR_NOT_ENOUGH_MEMORY);

			new_cmnt = B_TRUE;
		}
	}

	if (newname != NULL) {
		curname = si->shr_name;
		si->shr_name = newname;
		if ((nerr = smb_share_add(si)) == NERR_Success)
			nerr = smb_share_remove(curname);
		si->shr_name = curname;
	} else if (new_cmnt) {
		nerr = smb_share_add(si);
	}

	return (nerr);
}

/*
 * srvsvc_update_share_flags
 *
 * This function updates flags for shares.
 * Flags for Persistent shares are updated in both libshare and the local cache.
 * Flags for Transient shares are updated only in the local cache.
 */
static uint32_t
srvsvc_update_share_flags(smb_share_t *si, uint32_t shi_flags)
{
	uint32_t nerr = NERR_Success;
	uint32_t flag = 0;
	char *csc_value;
	char *abe_value = "false";
	nvlist_t *nvl;
	int err = 0;

	if (shi_flags & SHI1005_FLAGS_ACCESS_BASED_DIRECTORY_ENUM) {
		flag = SMB_SHRF_ABE;
		abe_value = "true";
	}

	si->shr_flags &= ~SMB_SHRF_ABE;
	si->shr_flags |= flag;

	switch ((shi_flags & CSC_MASK)) {
	case CSC_CACHE_AUTO_REINT:
		flag = SMB_SHRF_CSC_AUTO;
		break;
	case CSC_CACHE_VDO:
		flag = SMB_SHRF_CSC_VDO;
		break;
	case CSC_CACHE_NONE:
		flag = SMB_SHRF_CSC_DISABLED;
		break;
	case CSC_CACHE_MANUAL_REINT:
		flag = SMB_SHRF_CSC_MANUAL;
		break;
	default:
		return (NERR_InternalError);
	}

	si->shr_flags &= ~SMB_SHRF_CSC_MASK;
	si->shr_flags |= flag;

	if ((si->shr_flags & SMB_SHRF_TRANS) == 0) {
		csc_value = smb_share_csc_name(si);

		if (nvlist_alloc(&nvl, NV_UNIQUE_NAME, 0) != 0)
			return (NERR_InternalError);

		err |= nvlist_add_string(nvl, SHOPT_CSC, csc_value);
		err |= nvlist_add_string(nvl, SHOPT_ABE, abe_value);
		if (err) {
			nvlist_free(nvl);
			return (NERR_InternalError);
		}

		nerr = srvsvc_sa_setprop(si, nvl);
		nvlist_free(nvl);
	}

	return (nerr);
}

static uint32_t
srvsvc_get_share_flags(smb_share_t *si)
{
	uint32_t flags = 0;

	switch (si->shr_flags & SMB_SHRF_CSC_MASK) {
	case SMB_SHRF_CSC_DISABLED:
		flags |= CSC_CACHE_NONE;
		break;
	case SMB_SHRF_CSC_AUTO:
		flags |= CSC_CACHE_AUTO_REINT;
		break;
	case SMB_SHRF_CSC_VDO:
		flags |= CSC_CACHE_VDO;
		break;
	case SMB_SHRF_CSC_MANUAL:
	default:
		/*
		 * Default to CSC_CACHE_MANUAL_REINT.
		 */
		break;
	}

	if (si->shr_flags & SMB_SHRF_ABE)
		flags |= SHI1005_FLAGS_ACCESS_BASED_DIRECTORY_ENUM;

	/* if 'smb' zfs property: shortnames=disabled */
	if ((si->shr_flags & SMB_SHRF_SHORTNAME) == 0)
		flags |= SHI1005_FLAGS_ALLOW_NAMESPACE_CACHING;

	return (flags);
}

/*
 * srvsvc_s_NetSessionEnum
 *
 * Level 1 request is made by (Server Manager (srvmgr) on NT Server when
 * the user info icon is selected.
 *
 * On success, the return value is NERR_Success.
 * On error, the return value can be one of the following error codes:
 *
 * ERROR_ACCESS_DENIED      The user does not have access to the requested
 *                          information.
 * ERROR_INVALID_LEVEL      The value specified for the level is invalid.
 * ERROR_INVALID_PARAMETER  The specified parameter is invalid.
 * ERROR_MORE_DATA          More entries are available. Specify a large
 *                          enough buffer to receive all entries.
 * ERROR_NOT_ENOUGH_MEMORY  Insufficient memory is available.
 * NERR_ClientNameNotFound  A session does not exist with the computer name.
 * NERR_InvalidComputer     The computer name is invalid.
 * NERR_UserNotFound        The user name could not be found.
 */
static int
srvsvc_s_NetSessionEnum(void *arg, ndr_xa_t *mxa)
{
	struct mslm_NetSessionEnum	*param = arg;
	srvsvc_infonres_t		*info;
	smb_netsvc_t			*ns;
	smb_svcenum_t			se;
	DWORD				status = ERROR_SUCCESS;

	if (!ndr_is_admin(mxa)) {
		status = ERROR_ACCESS_DENIED;
		goto srvsvc_netsessionenum_error;
	}

	if ((info = NDR_NEW(mxa, srvsvc_infonres_t)) == NULL) {
		status = ERROR_NOT_ENOUGH_MEMORY;
		goto srvsvc_netsessionenum_error;
	}

	info->entriesread = 0;
	info->entries = NULL;
	param->result.level = param->level;
	param->result.bufptr.p = info;

	if ((param->total_entries = srvsvc_open_sessions()) == 0) {
		param->resume_handle = NULL;
		param->status = ERROR_SUCCESS;
		return (NDR_DRC_OK);
	}

	bzero(&se, sizeof (smb_svcenum_t));
	se.se_type = SMB_SVCENUM_TYPE_USER;
	se.se_level = param->level;
	se.se_ntotal = param->total_entries;
	se.se_nlimit = se.se_ntotal;

	if (param->resume_handle) {
		se.se_resume = *param->resume_handle;
		se.se_nskip = se.se_resume;
		*param->resume_handle = 0;
	}

	switch (param->level) {
	case 0:
		info->entries = NDR_NEWN(mxa, struct mslm_SESSION_INFO_0,
		    se.se_nlimit);
		break;
	case 1:
		info->entries = NDR_NEWN(mxa, struct mslm_SESSION_INFO_1,
		    se.se_nlimit);
		break;
	case 2:
		info->entries = NDR_NEWN(mxa, struct mslm_SESSION_INFO_2,
		    se.se_nlimit);
		break;
	case 10:
		info->entries = NDR_NEWN(mxa, struct mslm_SESSION_INFO_10,
		    se.se_nlimit);
		break;
	case 502:
		info->entries = NDR_NEWN(mxa, struct mslm_SESSION_INFO_502,
		    se.se_nlimit);
		break;
	default:
		bzero(param, sizeof (struct mslm_NetSessionEnum));
		param->status = ERROR_INVALID_LEVEL;
		return (NDR_DRC_OK);
	}

	if (info->entries == NULL) {
		status = ERROR_NOT_ENOUGH_MEMORY;
		goto srvsvc_netsessionenum_error;
	}

	if ((ns = smb_kmod_enum_init(&se)) == NULL) {
		status = ERROR_NOT_ENOUGH_MEMORY;
		goto srvsvc_netsessionenum_error;
	}

	status = srvsvc_NetSessionEnumCommon(mxa, info, ns, &se);
	smb_kmod_enum_fini(ns);

	if (status != ERROR_SUCCESS)
		goto srvsvc_netsessionenum_error;

	if (param->resume_handle &&
	    param->pref_max_len != SMB_SRVSVC_MAXPREFLEN) {
		if (se.se_resume < param->total_entries) {
			*param->resume_handle = se.se_resume;
			status = ERROR_MORE_DATA;
		}
	}

	param->total_entries = info->entriesread;
	param->status = status;
	return (NDR_DRC_OK);

srvsvc_netsessionenum_error:
	bzero(param, sizeof (struct mslm_NetSessionEnum));
	param->status = status;
	return (NDR_DRC_OK);
}

static uint32_t
srvsvc_NetSessionEnumCommon(ndr_xa_t *mxa, srvsvc_infonres_t *info,
    smb_netsvc_t *ns, smb_svcenum_t *se)
{
	struct mslm_SESSION_INFO_0	*info0 = info->entries;
	struct mslm_SESSION_INFO_1	*info1 = info->entries;
	struct mslm_SESSION_INFO_2	*info2 = info->entries;
	struct mslm_SESSION_INFO_10	*info10 = info->entries;
	struct mslm_SESSION_INFO_502	*info502 = info->entries;
	smb_netsvcitem_t		*item;
	smb_netuserinfo_t		*user;
	char				*workstation;
	char				account[MAXNAMELEN];
	char				ipaddr_buf[INET6_ADDRSTRLEN];
	uint32_t			logon_time;
	uint32_t			flags;
	uint32_t			entries_read = 0;

	if (smb_kmod_enum(ns) != 0)
		return (ERROR_INTERNAL_ERROR);

	item = list_head(&ns->ns_list);
	while (item != NULL) {
		user = &item->nsi_un.nsi_user;

		workstation = user->ui_workstation;
		if (workstation == NULL || *workstation == '\0') {
			(void) smb_inet_ntop(&user->ui_ipaddr, ipaddr_buf,
			    SMB_IPSTRLEN(user->ui_ipaddr.a_family));
			workstation = ipaddr_buf;
		}

		(void) snprintf(account, MAXNAMELEN, "%s\\%s",
		    user->ui_domain, user->ui_account);

		logon_time = time(0) - user->ui_logon_time;
		flags = (user->ui_flags & SMB_ATF_GUEST) ? SESS_GUEST : 0;

		switch (se->se_level) {
		case 0:
			info0->sesi0_cname = NDR_STRDUP(mxa, workstation);
			if (info0->sesi0_cname == NULL)
				return (ERROR_NOT_ENOUGH_MEMORY);
			++info0;
			break;

		case 1:
			info1->sesi1_cname = NDR_STRDUP(mxa, workstation);
			info1->sesi1_uname = NDR_STRDUP(mxa, account);

			if (info1->sesi1_cname == NULL ||
			    info1->sesi1_uname == NULL)
				return (ERROR_NOT_ENOUGH_MEMORY);

			info1->sesi1_nopens = user->ui_numopens;
			info1->sesi1_time = logon_time;
			info1->sesi1_itime = 0;
			info1->sesi1_uflags = flags;
			++info1;
			break;

		case 2:
			info2->sesi2_cname = NDR_STRDUP(mxa, workstation);
			info2->sesi2_uname = NDR_STRDUP(mxa, account);

			if (info2->sesi2_cname == NULL ||
			    info2->sesi2_uname == NULL)
				return (ERROR_NOT_ENOUGH_MEMORY);

			info2->sesi2_nopens = user->ui_numopens;
			info2->sesi2_time = logon_time;
			info2->sesi2_itime = 0;
			info2->sesi2_uflags = flags;
			info2->sesi2_cltype_name = (uint8_t *)"";
			++info2;
			break;

		case 10:
			info10->sesi10_cname = NDR_STRDUP(mxa, workstation);
			info10->sesi10_uname = NDR_STRDUP(mxa, account);

			if (info10->sesi10_cname == NULL ||
			    info10->sesi10_uname == NULL)
				return (ERROR_NOT_ENOUGH_MEMORY);

			info10->sesi10_time = logon_time;
			info10->sesi10_itime = 0;
			++info10;
			break;

		case 502:
			info502->sesi502_cname = NDR_STRDUP(mxa, workstation);
			info502->sesi502_uname = NDR_STRDUP(mxa, account);

			if (info502->sesi502_cname == NULL ||
			    info502->sesi502_uname == NULL)
				return (ERROR_NOT_ENOUGH_MEMORY);

			info502->sesi502_nopens = user->ui_numopens;
			info502->sesi502_time = logon_time;
			info502->sesi502_itime = 0;
			info502->sesi502_uflags = flags;
			info502->sesi502_cltype_name = (uint8_t *)"";
			info502->sesi502_transport = (uint8_t *)"";
			++info502;
			break;

		default:
			return (ERROR_INVALID_LEVEL);
		}

		++entries_read;
		item = list_next(&ns->ns_list, item);
	}

	info->entriesread = entries_read;
	return (ERROR_SUCCESS);
}

/*
 * srvsvc_s_NetSessionDel
 *
 * Ends a network session between a server and a workstation.
 * On NT only members of the Administrators or Account Operators
 * local groups are permitted to use NetSessionDel.
 *
 * If unc_clientname is NULL, all sessions associated with the
 * specified user will be disconnected.
 *
 * If username is NULL, all sessions from the specified client
 * will be disconnected.
 *
 * Return Values
 * On success, the return value is NERR_Success/ERROR_SUCCESS.
 * On failure, the return value can be one of the following errors:
 *
 * ERROR_ACCESS_DENIED 		The user does not have access to the
 * 				requested information.
 * ERROR_INVALID_PARAMETER	The specified parameter is invalid.
 * ERROR_NOT_ENOUGH_MEMORY	Insufficient memory is available.
 * NERR_ClientNameNotFound	A session does not exist with that
 *				computer name.
 */
static int
srvsvc_s_NetSessionDel(void *arg, ndr_xa_t *mxa)
{
	static struct {
		int errnum;
		int nerr;
	} errmap[] = {
		0,	ERROR_SUCCESS,
		EACCES,	ERROR_ACCESS_DENIED,
		EPERM,	ERROR_ACCESS_DENIED,
		EINVAL,	ERROR_INVALID_PARAMETER,
		ENOMEM,	ERROR_NOT_ENOUGH_MEMORY,
		ENOENT,	NERR_ClientNameNotFound
	};

	struct mslm_NetSessionDel *param = arg;
	int	i;
	int	rc;

	if (!ndr_is_admin(mxa)) {
		param->status = ERROR_ACCESS_DENIED;
		return (NDR_DRC_OK);
	}

	rc = smb_kmod_session_close((char *)param->unc_clientname,
	    (char *)param->username);

	for (i = 0; i < (sizeof (errmap) / sizeof (errmap[0])); ++i) {
		if (rc == errmap[i].errnum) {
			param->status = errmap[i].nerr;
			return (NDR_DRC_OK);
		}
	}

	param->status = ERROR_INTERNAL_ERROR;
	return (NDR_DRC_OK);
}

static int
srvsvc_s_NetServerGetInfo(void *arg, ndr_xa_t *mxa)
{
	struct mslm_NetServerGetInfo *param = arg;
	struct mslm_SERVER_INFO_100 *info100;
	struct mslm_SERVER_INFO_101 *info101;
	struct mslm_SERVER_INFO_102 *info102;
	struct mslm_SERVER_INFO_502 *info502;
	struct mslm_SERVER_INFO_503 *info503;
	char sys_comment[SMB_PI_MAX_COMMENT];
	char hostname[NETBIOS_NAME_SZ];
	smb_version_t version;

	if (smb_getnetbiosname(hostname, sizeof (hostname)) != 0) {
netservergetinfo_no_memory:
		bzero(param, sizeof (struct mslm_NetServerGetInfo));
		return (ERROR_NOT_ENOUGH_MEMORY);
	}

	(void) smb_config_getstr(SMB_CI_SYS_CMNT, sys_comment,
	    sizeof (sys_comment));
	if (*sys_comment == '\0')
		(void) strcpy(sys_comment, " ");

	smb_config_get_version(&version);

	switch (param->level) {
	case 100:
		info100 = NDR_NEW(mxa, struct mslm_SERVER_INFO_100);
		if (info100 == NULL)
			goto netservergetinfo_no_memory;

		bzero(info100, sizeof (struct mslm_SERVER_INFO_100));
		info100->sv100_platform_id = SV_PLATFORM_ID_NT;
		info100->sv100_name = (uint8_t *)NDR_STRDUP(mxa, hostname);
		if (info100->sv100_name == NULL)
			goto netservergetinfo_no_memory;

		param->result.bufptr.bufptr100 = info100;
		break;

	case 101:
		info101 = NDR_NEW(mxa, struct mslm_SERVER_INFO_101);
		if (info101 == NULL)
			goto netservergetinfo_no_memory;

		bzero(info101, sizeof (struct mslm_SERVER_INFO_101));
		info101->sv101_platform_id = SV_PLATFORM_ID_NT;
		info101->sv101_version_major = version.sv_major;
		info101->sv101_version_minor = version.sv_minor;
		info101->sv101_type = SV_TYPE_DEFAULT;
		info101->sv101_name = (uint8_t *)NDR_STRDUP(mxa, hostname);
		info101->sv101_comment
		    = (uint8_t *)NDR_STRDUP(mxa, sys_comment);

		if (info101->sv101_name == NULL ||
		    info101->sv101_comment == NULL)
			goto netservergetinfo_no_memory;

		param->result.bufptr.bufptr101 = info101;
		break;

	case 102:
		info102 = NDR_NEW(mxa, struct mslm_SERVER_INFO_102);
		if (info102 == NULL)
			goto netservergetinfo_no_memory;

		bzero(info102, sizeof (struct mslm_SERVER_INFO_102));
		info102->sv102_platform_id = SV_PLATFORM_ID_NT;
		info102->sv102_version_major = version.sv_major;
		info102->sv102_version_minor = version.sv_minor;
		info102->sv102_type = SV_TYPE_DEFAULT;
		info102->sv102_name = (uint8_t *)NDR_STRDUP(mxa, hostname);
		info102->sv102_comment
		    = (uint8_t *)NDR_STRDUP(mxa, sys_comment);

		/*
		 * The following level 102 fields are defaulted to zero
		 * by virtue of the call to bzero above.
		 *
		 * sv102_users
		 * sv102_disc
		 * sv102_hidden
		 * sv102_announce
		 * sv102_anndelta
		 * sv102_licenses
		 * sv102_userpath
		 */
		if (info102->sv102_name == NULL ||
		    info102->sv102_comment == NULL)
			goto netservergetinfo_no_memory;

		param->result.bufptr.bufptr102 = info102;
		break;

	case 502:
		info502 = NDR_NEW(mxa, struct mslm_SERVER_INFO_502);
		if (info502 == NULL)
			goto netservergetinfo_no_memory;

		bzero(info502, sizeof (struct mslm_SERVER_INFO_502));
		param->result.bufptr.bufptr502 = info502;
#ifdef SRVSVC_SATISFY_SMBTORTURE
		break;
#else
		param->result.level = param->level;
		param->status = ERROR_ACCESS_DENIED;
		return (NDR_DRC_OK);
#endif /* SRVSVC_SATISFY_SMBTORTURE */

	case 503:
		info503 = NDR_NEW(mxa, struct mslm_SERVER_INFO_503);
		if (info503 == NULL)
			goto netservergetinfo_no_memory;

		bzero(info503, sizeof (struct mslm_SERVER_INFO_503));
		param->result.bufptr.bufptr503 = info503;
#ifdef SRVSVC_SATISFY_SMBTORTURE
		break;
#else
		param->result.level = param->level;
		param->status = ERROR_ACCESS_DENIED;
		return (NDR_DRC_OK);
#endif /* SRVSVC_SATISFY_SMBTORTURE */

	default:
		bzero(&param->result,
		    sizeof (struct mslm_NetServerGetInfo_result));
		param->status = ERROR_ACCESS_DENIED;
		return (NDR_DRC_OK);
	}

	param->result.level = param->level;
	param->status = ERROR_SUCCESS;
	return (NDR_DRC_OK);
}

/*
 * NetRemoteTOD
 *
 * Returns information about the time of day on this server.
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
 *	DWORD tod_weekday;   // day of the week since Sunday [0-6]
 * } TIME_OF_DAY_INFO;
 *
 * The time zone of the server is calculated in minutes from Greenwich
 * Mean Time (GMT). For time zones west of Greenwich, the value is
 * positive; for time zones east of Greenwich, the value is negative.
 * A value of -1 indicates that the time zone is undefined.
 *
 * Determine offset from GMT. If daylight saving time use altzone,
 * otherwise use timezone.
 *
 * The clock tick value represents a resolution of one ten-thousandth
 * (0.0001) second.
 */
static int
srvsvc_s_NetRemoteTOD(void *arg, ndr_xa_t *mxa)
{
	struct mslm_NetRemoteTOD *param = arg;
	struct mslm_TIME_OF_DAY_INFO *tod;
	struct timeval		time_val;
	struct tm		tm;
	time_t			gmtoff;


	(void) gettimeofday(&time_val, 0);
	(void) gmtime_r(&time_val.tv_sec, &tm);

	tod = NDR_NEW(mxa, struct mslm_TIME_OF_DAY_INFO);
	if (tod == NULL) {
		bzero(param, sizeof (struct mslm_NetRemoteTOD));
		return (ERROR_NOT_ENOUGH_MEMORY);
	}

	bzero(tod, sizeof (struct mslm_TIME_OF_DAY_INFO));

	tod->tod_elapsedt = time_val.tv_sec;
	tod->tod_msecs = time_val.tv_usec;
	tod->tod_hours = tm.tm_hour;
	tod->tod_mins = tm.tm_min;
	tod->tod_secs = tm.tm_sec;
	tod->tod_hunds = 0;
	tod->tod_tinterval = 1000;
	tod->tod_day = tm.tm_mday;
	tod->tod_month = tm.tm_mon+1;
	tod->tod_year = tm.tm_year+1900;
	tod->tod_weekday = tm.tm_wday;

	(void) localtime_r(&time_val.tv_sec, &tm);
	gmtoff = (tm.tm_isdst) ? altzone : timezone;
	tod->tod_timezone = gmtoff / SECSPERMIN;

	param->bufptr = tod;
	param->status = ERROR_SUCCESS;
	return (NDR_DRC_OK);
}

/*
 * srvsvc_s_NetNameValidate
 *
 * Perform name validation.
 *
 * Returns Win32 error codes.
 */
/*ARGSUSED*/
static int
srvsvc_s_NetNameValidate(void *arg, ndr_xa_t *mxa)
{
	struct mslm_NetNameValidate *param = arg;
	char *name;
	int maxlen;
	int len;

	if ((name = (char *)param->pathname) == NULL) {
		param->status = ERROR_INVALID_PARAMETER;
		return (NDR_DRC_OK);
	}

	switch (param->type) {
	case NAMETYPE_SHARE:
		len = strlen(name);
		maxlen = (param->flags & NAMEFLAG_LM2) ?
		    SMB_SHARE_OEMNAME_MAX : SMB_SHARE_NTNAME_MAX;

		if (len > maxlen) {
			param->status = ERROR_INVALID_NAME;
			return (NDR_DRC_OK);
		}

		param->status = smb_name_validate_share(name);
		break;

	case NAMETYPE_USER:
	case NAMETYPE_GROUP:
		param->status = smb_name_validate_account(name);
		break;

	case NAMETYPE_DOMAIN:	/* NetBIOS domain name */
		param->status = smb_name_validate_nbdomain(name);
		break;

	case NAMETYPE_WORKGROUP:
		param->status = smb_name_validate_workgroup(name);
		break;

	case NAMETYPE_PASSWORD:
	case NAMETYPE_COMPUTER:
	case NAMETYPE_EVENT:
	case NAMETYPE_SERVICE:
	case NAMETYPE_NET:
	case NAMETYPE_MESSAGE:
	case NAMETYPE_MESSAGEDEST:
	case NAMETYPE_SHAREPASSWORD:
		param->status = ERROR_NOT_SUPPORTED;
		break;

	default:
		param->status = ERROR_INVALID_PARAMETER;
		break;
	}

	return (NDR_DRC_OK);
}

/*
 * srvsvc_s_NetShareAdd
 *
 * Add a new share. Only power users groups can manage shares.
 *
 * This interface is used by the rmtshare command from the NT resource
 * kit. Rmtshare allows a client to add or remove shares on a server
 * from the client's command line.
 *
 * Returns Win32 error codes.
 */
static int
srvsvc_s_NetShareAdd(void *arg, ndr_xa_t *mxa)
{
	static DWORD parm_err = 0;
	DWORD parm_stat;
	struct mslm_NetShareAdd *param = arg;
	struct mslm_NetShareInfo_2 *info2;
	struct mslm_NetShareInfo_502 *info502;
	char realpath[MAXPATHLEN];
	int32_t native_os;
	uint8_t *sdbuf = NULL;
	uint32_t status;
	smb_share_t si;

	native_os = ndr_native_os(mxa);

	if (!ndr_is_poweruser(mxa)) {
		bzero(param, sizeof (struct mslm_NetShareAdd));
		param->status = ERROR_ACCESS_DENIED;
		return (NDR_DRC_OK);
	}

	switch (param->level) {
	case 2:
		info2 = (struct mslm_NetShareInfo_2 *)param->info.un.info2;
		break;

	case 502:
		info502 = (struct mslm_NetShareInfo_502 *)
		    param->info.un.info502;
		sdbuf = info502->shi502_security_descriptor;
		info2 = (struct mslm_NetShareInfo_2 *)info502;
		break;

	default:
		bzero(param, sizeof (struct mslm_NetShareAdd));
		param->status = ERROR_ACCESS_DENIED;
		return (NDR_DRC_OK);
	}

	if (info2->shi2_netname == NULL || info2->shi2_path == NULL) {
		bzero(param, sizeof (struct mslm_NetShareAdd));
		param->status = NERR_NetNameNotFound;
		return (NDR_DRC_OK);
	}

	if (srvsvc_share_is_restricted((char *)info2->shi2_netname)) {
		bzero(param, sizeof (struct mslm_NetShareAdd));
		param->status = ERROR_ACCESS_DENIED;
		return (NDR_DRC_OK);
	}

	if (info2->shi2_comment == NULL)
		info2->shi2_comment = (uint8_t *)"";

	/*
	 * Derive the real path which will be stored in the
	 * directory field of the smb_share_t structure
	 * from the path field in this RPC request.
	 */
	parm_stat = srvsvc_get_realpath((const char *)info2->shi2_path,
	    realpath, MAXPATHLEN);

	if (parm_stat != NERR_Success) {
		bzero(param, sizeof (struct mslm_NetShareAdd));
		param->status = parm_stat;
		param->parm_err
		    = (native_os == NATIVE_OS_WIN95) ? 0 : &parm_err;
		return (NDR_DRC_OK);
	}

	param->status = srvsvc_sa_add((char *)info2->shi2_netname, realpath,
	    (char *)info2->shi2_comment);
	if (param->status == NERR_Success) {
		status = smb_share_lookup((char *)info2->shi2_netname, &si);

		if (status == NERR_Success) {
			if (sdbuf != NULL)
				(void) srvsvc_sd_set(&si, sdbuf);
			smb_share_free(&si);
		}
	}
	param->parm_err = (native_os == NATIVE_OS_WIN95) ? 0 : &parm_err;
	return (NDR_DRC_OK);
}

/*
 * srvsvc_get_realpath
 *
 * Derive the real path for a share from the path provided by a client.
 * For instance, the real path of C:\ may be /cvol or the real path of
 * F:\home may be /vol1/home.
 *
 * clntpath - path provided by the Windows client is in the
 *            format of <drive letter>:\<dir>
 * realpath - path that will be stored as the directory field of
 *            the smb_share_t structure of the share.
 * maxlen   - maximum length of the realpath buffer
 *
 * Return LAN Manager network error code.
 */
uint32_t
srvsvc_get_realpath(const char *clntpath, char *realpath, int maxlen)
{
	const char *p;
	int len;

	if ((p = strchr(clntpath, ':')) != NULL)
		++p;
	else
		p = clntpath;

	(void) strlcpy(realpath, p, maxlen);
	(void) strcanon(realpath, "/\\");
	(void) strsubst(realpath, '\\', '/');

	len = strlen(realpath);
	if ((len > 1) && (realpath[len - 1] == '/'))
		realpath[len - 1] = '\0';

	return (NERR_Success);
}

/*
 * srvsvc_estimate_limit
 *
 * Estimate the number of objects that will fit in prefmaxlen.
 * nlimit is adjusted here.
 */
static void
srvsvc_estimate_limit(smb_svcenum_t *se, uint32_t obj_size)
{
	DWORD max_cnt;

	if (obj_size == 0) {
		se->se_nlimit = 0;
		return;
	}

	if ((max_cnt = (se->se_prefmaxlen / obj_size)) == 0) {
		se->se_nlimit = 0;
		return;
	}

	if (se->se_ntotal > max_cnt)
		se->se_nlimit = max_cnt;
	else
		se->se_nlimit = se->se_ntotal;
}

/*
 * Enumerate all shares (see also NetShareEnumSticky).
 *
 * Request for various levels of information about our shares.
 * Level 0: share names.
 * Level 1: share name, share type and comment field.
 * Level 2: everything that we know about the shares.
 * Level 501: level 1 + flags.
 * Level 502: level 2 + security descriptor.
 */
static int
srvsvc_s_NetShareEnum(void *arg, ndr_xa_t *mxa)
{
	return (srvsvc_share_enum(arg, mxa, B_FALSE));
}

/*
 * Enumerate sticky shares: all shares except those marked STYPE_SPECIAL.
 * Except for excluding STYPE_SPECIAL shares, NetShareEnumSticky is the
 * same as NetShareEnum.
 *
 * Request for various levels of information about our shares.
 * Level 0: share names.
 * Level 1: share name, share type and comment field.
 * Level 2: everything that we know about the shares.
 * Level 501: not valid for this request.
 * Level 502: level 2 + security descriptor.
 *
 * We set n_skip to resume_handle, which is used to find the appropriate
 * place to resume.  The resume_handle is similar to the readdir cookie.
 */
static int
srvsvc_s_NetShareEnumSticky(void *arg, ndr_xa_t *mxa)
{
	return (srvsvc_share_enum(arg, mxa, B_TRUE));
}

static int
srvsvc_share_enum(void *arg, ndr_xa_t *mxa, boolean_t sticky)
{
	struct mslm_NetShareEnum *param = arg;
	srvsvc_infonres_t	*infonres;
	smb_netuserinfo_t	*user = &mxa->pipe->np_user;
	smb_svcenum_t		se;
	smb_netsvc_t		*ns;
	smb_netsvcitem_t	*item;
	DWORD			status = ERROR_SUCCESS;
	void			*info = NULL;

	switch (param->level) {
	case 2:
	case 502:
		if (!ndr_is_poweruser(mxa))
			status = ERROR_ACCESS_DENIED;
		break;

	case 501:
		if (sticky)
			status = ERROR_INVALID_LEVEL;
		else if (!ndr_is_poweruser(mxa))
			status = ERROR_ACCESS_DENIED;
		else
			status = ERROR_SUCCESS;
		break;

	default:
		break;
	}

	if (status != ERROR_SUCCESS) {
		bzero(param, sizeof (struct mslm_NetShareEnum));
		param->status = status;
		return (NDR_DRC_OK);
	}

	infonres = NDR_NEW(mxa, srvsvc_infonres_t);
	if (infonres == NULL) {
		bzero(param, sizeof (struct mslm_NetShareEnum));
		param->status = ERROR_NOT_ENOUGH_MEMORY;
		return (NDR_DRC_OK);
	}

	param->totalentries = smb_share_count();
	if (param->totalentries == 0) {
		bzero(param, sizeof (struct mslm_NetShareEnum));
		param->status = ERROR_SUCCESS;
		return (NDR_DRC_OK);
	}

	infonres->entriesread = 0;
	infonres->entries = NULL;
	param->result.level = param->level;
	param->result.bufptr.p = infonres;

	bzero(&se, sizeof (smb_svcenum_t));
	se.se_type = SMB_SVCENUM_TYPE_SHARE;
	se.se_level = param->level;
	se.se_ntotal = param->totalentries;
	se.se_nlimit = se.se_ntotal;

	if (param->prefmaxlen == SMB_SRVSVC_MAXPREFLEN ||
	    param->prefmaxlen > SMB_SRVSVC_MAXBUFLEN)
		se.se_prefmaxlen = SMB_SRVSVC_MAXBUFLEN;
	else
		se.se_prefmaxlen = param->prefmaxlen;

	if (param->resume_handle) {
		se.se_resume = *param->resume_handle;
		se.se_nskip = se.se_resume;
		*param->resume_handle = 0;
	}

	switch (param->level) {
	case 0:
		srvsvc_estimate_limit(&se,
		    sizeof (struct mslm_NetShareInfo_0) + MAXNAMELEN);
		info = NDR_NEWN(mxa, struct mslm_NetShareInfo_0, se.se_nlimit);
		break;

	case 1:
		srvsvc_estimate_limit(&se,
		    sizeof (struct mslm_NetShareInfo_1) + MAXNAMELEN);
		info = NDR_NEWN(mxa, struct mslm_NetShareInfo_1, se.se_nlimit);
		break;

	case 2:
		srvsvc_estimate_limit(&se,
		    sizeof (struct mslm_NetShareInfo_2) + MAXNAMELEN);
		info = NDR_NEWN(mxa, struct mslm_NetShareInfo_2, se.se_nlimit);
		break;

	case 501:
		srvsvc_estimate_limit(&se,
		    sizeof (struct mslm_NetShareInfo_501) + MAXNAMELEN);
		info = NDR_NEWN(mxa, struct mslm_NetShareInfo_501,
		    se.se_nlimit);
		break;

	case 502:
		srvsvc_estimate_limit(&se,
		    sizeof (struct mslm_NetShareInfo_502) + MAXNAMELEN);
		info = NDR_NEWN(mxa, struct mslm_NetShareInfo_502,
		    se.se_nlimit);
		break;

	default:
		status = ERROR_INVALID_LEVEL;
		break;
	}

	if (se.se_nlimit == 0) {
		param->status = ERROR_SUCCESS;
		return (NDR_DRC_OK);
	}

	if (info == NULL)
		status = ERROR_NOT_ENOUGH_MEMORY;

	if (status != ERROR_SUCCESS) {
		bzero(param, sizeof (struct mslm_NetShareEnum));
		param->status = status;
		return (NDR_DRC_OK);
	}

	if ((ns = srvsvc_shareenum_init(&se, user, sticky)) == NULL) {
		bzero(param, sizeof (struct mslm_NetShareEnum));
		param->status = ERROR_NOT_ENOUGH_MEMORY;
		return (NDR_DRC_OK);
	}

	if (smb_kmod_enum(ns) != 0) {
		smb_kmod_enum_fini(ns);
		bzero(param, sizeof (struct mslm_NetShareEnum));
		param->status = ERROR_INTERNAL_ERROR;
		return (NDR_DRC_OK);
	}

	se.se_nitems = 0;
	item = list_head(&ns->ns_list);
	while (item != NULL) {
		status = mlsvc_NetShareEnumCommon(mxa, &se,
		    &item->nsi_un.nsi_share, info);
		if (status != ERROR_SUCCESS)
			break;

		se.se_nitems++;
		item = list_next(&ns->ns_list, item);
	}

	se.se_resume += se.se_nitems;
	infonres->entriesread = se.se_nitems;
	infonres->entries = info;
	smb_kmod_enum_fini(ns);

	if (param->resume_handle &&
	    param->prefmaxlen != SMB_SRVSVC_MAXPREFLEN) {
		if (se.se_resume < se.se_ntotal) {
			*param->resume_handle = se.se_resume;
			status = ERROR_MORE_DATA;
		}
	}

	param->totalentries = se.se_ntotal;
	param->status = status;
	return (NDR_DRC_OK);
}

/*
 * mlsvc_NetShareEnumCommon
 *
 * Build the levels 0, 1, 2, 501 and 502 share information. This function
 * is called by the various NetShareEnum levels for each share. If
 * we cannot build the share data for some reason, we return an error
 * but the actual value of the error is not important to the caller.
 * The caller just needs to know not to include this info in the RPC
 * response.
 *
 * Returns:
 *	ERROR_SUCCESS
 *	ERROR_NOT_ENOUGH_MEMORY
 *	ERROR_INVALID_LEVEL
 */
static DWORD
mlsvc_NetShareEnumCommon(ndr_xa_t *mxa, smb_svcenum_t *se,
    smb_share_t *si, void *infop)
{
	static uint8_t empty_string[1];
	struct mslm_NetShareInfo_0 *info0;
	struct mslm_NetShareInfo_1 *info1;
	struct mslm_NetShareInfo_2 *info2;
	struct mslm_NetShareInfo_501 *info501;
	struct mslm_NetShareInfo_502 *info502;
	srvsvc_sd_t sd;
	uint8_t *netname;
	uint8_t *comment = empty_string;
	uint8_t *passwd = empty_string;
	uint8_t *path;
	int i = se->se_nitems;

	netname = (uint8_t *)NDR_STRDUP(mxa, si->shr_name);
	path = (uint8_t *)NDR_STRDUP(mxa, si->shr_winpath);

	if (si->shr_cmnt != NULL)
		comment = (uint8_t *)NDR_STRDUP(mxa, si->shr_cmnt);

	if (!netname || !comment || !path)
		return (ERROR_NOT_ENOUGH_MEMORY);

	switch (se->se_level) {
	case 0:
		info0 = (struct mslm_NetShareInfo_0 *)infop;
		info0[i].shi0_netname = netname;
		break;

	case 1:
		info1 = (struct mslm_NetShareInfo_1 *)infop;
		info1[i].shi1_netname = netname;
		info1[i].shi1_comment = comment;
		info1[i].shi1_type = si->shr_type;
		break;

	case 2:
		info2 = (struct mslm_NetShareInfo_2 *)infop;
		info2[i].shi2_netname = netname;
		info2[i].shi2_comment = comment;
		info2[i].shi2_path = path;
		info2[i].shi2_type = si->shr_type;
		info2[i].shi2_permissions = 0;
		info2[i].shi2_max_uses = SHI_USES_UNLIMITED;
		info2[i].shi2_current_uses = 0;
		info2[i].shi2_passwd = passwd;
		break;

	case 501:
		info501 = (struct mslm_NetShareInfo_501 *)infop;
		info501[i].shi501_netname = netname;
		info501[i].shi501_comment = comment;
		info501[i].shi501_type = si->shr_type;
		info501[i].shi501_flags = srvsvc_get_share_flags(si);
		break;

	case 502:
		info502 = (struct mslm_NetShareInfo_502 *)infop;
		info502[i].shi502_netname = netname;
		info502[i].shi502_comment = comment;
		info502[i].shi502_path = path;
		info502[i].shi502_type = si->shr_type;
		info502[i].shi502_permissions = 0;
		info502[i].shi502_max_uses = SHI_USES_UNLIMITED;
		info502[i].shi502_current_uses = 0;
		info502[i].shi502_passwd = passwd;

		if (srvsvc_share_getsd(mxa, si, &sd) == ERROR_SUCCESS) {
			info502[i].shi502_reserved = sd.sd_size;
			info502[i].shi502_security_descriptor = sd.sd_buf;
		} else {
			info502[i].shi502_reserved = 0;
			info502[i].shi502_security_descriptor = NULL;
		}

		break;

	default:
		return (ERROR_INVALID_LEVEL);
	}

	return (ERROR_SUCCESS);
}

static smb_netsvc_t *
srvsvc_shareenum_init(smb_svcenum_t *se, smb_netuserinfo_t *user,
    boolean_t sticky)
{
	smb_netsvc_t *ns;
	char *username;

	if ((ns = smb_kmod_enum_init(se)) == NULL)
		return (NULL);

	username = (user->ui_posix_name)
	    ? user->ui_posix_name : user->ui_account;

	(void) strlcpy(se->se_qualifier.seq_qualstr, username, MAXNAMELEN);
	se->se_qualifier.seq_mode = (sticky)
	    ? SMB_SVCENUM_SHARE_PERM : SMB_SVCENUM_SHARE_RPC;

	return (ns);
}

static int /*ARGSUSED*/
srvsvc_s_NetShareCheck(void *arg, ndr_xa_t *mxa)
{
	struct mslm_NetShareCheck *param = arg;
	smb_share_t si;

	if (param->path == NULL) {
		param->stype = STYPE_DISKTREE;
		param->status = NERR_NetNameNotFound;
		return (NDR_DRC_OK);
	}

	(void) strsubst((char *)param->path, '/', '\\');

	if (smb_share_check((const char *)param->path, &si) == 0) {
		param->stype = (si.shr_type & STYPE_MASK);
		param->status = NERR_Success;
		smb_share_free(&si);
		return (NDR_DRC_OK);
	}

	param->stype = STYPE_DISKTREE;
	param->status = NERR_NetNameNotFound;
	return (NDR_DRC_OK);
}

/*
 * Delete a share.  Only members of the Administrators, Server Operators
 * or Power Users local groups are allowed to delete shares.
 *
 * This interface is used by the rmtshare command from the NT resource
 * kit. Rmtshare allows a client to add or remove shares on a server
 * from the client's command line.
 *
 * Returns Win32 error codes.
 */
static int
srvsvc_s_NetShareDel(void *arg, ndr_xa_t *mxa)
{
	struct mslm_NetShareDel *param = arg;
	smb_share_t si;

	if (!ndr_is_poweruser(mxa) ||
	    srvsvc_share_is_restricted((char *)param->netname)) {
		param->status = ERROR_ACCESS_DENIED;
		return (NDR_DRC_OK);
	}

	param->status = smb_share_lookup((char *)param->netname, &si);
	if (param->status == NERR_Success) {
		if ((si.shr_flags & SMB_SHRF_DFSROOT) != 0)
			param->status = NERR_IsDfsShare;
		else
			param->status = srvsvc_sa_delete(si.shr_name,
			    si.shr_path);
		smb_share_free(&si);
	}

	return (NDR_DRC_OK);
}

/*
 * srvsvc_s_NetGetFileSecurity
 *
 * Get security descriptor of the requested file/folder
 *
 * Right now, just returns ERROR_ACCESS_DENIED, because we cannot
 * get the requested SD here in RPC code.
 */
/*ARGSUSED*/
static int
srvsvc_s_NetGetFileSecurity(void *arg, ndr_xa_t *mxa)
{
	struct mslm_NetGetFileSecurity *param = arg;

	param->length = 0;
	param->status = ERROR_ACCESS_DENIED;
	return (NDR_DRC_OK);
}

/*
 * srvsvc_s_NetSetFileSecurity
 *
 * Set the given security descriptor for the requested file/folder
 *
 * Right now, just returns ERROR_ACCESS_DENIED, because we cannot
 * set the requested SD here in RPC code.
 */
/*ARGSUSED*/
static int
srvsvc_s_NetSetFileSecurity(void *arg, ndr_xa_t *mxa)
{
	struct mslm_NetSetFileSecurity *param = arg;

	param->status = ERROR_ACCESS_DENIED;
	return (NDR_DRC_OK);
}

/*
 * Check whether or not there is a restriction on a share. Restricted
 * shares are generally STYPE_SPECIAL, for example, IPC$. All the
 * administration share names are restricted: C$, D$ etc. Returns B_TRUE
 * if the share is restricted. Otherwise B_FALSE is returned to indicate
 * that there are no restrictions.
 */
static boolean_t
srvsvc_share_is_restricted(const char *sharename)
{
	static char *restricted[] = {
		"IPC$"
	};

	int i;

	if (sharename == NULL)
		return (B_FALSE);

	for (i = 0; i < sizeof (restricted)/sizeof (restricted[0]); i++) {
		if (smb_strcasecmp(restricted[i], sharename, 0) == 0)
			return (B_TRUE);
	}

	return (srvsvc_share_is_admin(sharename));
}

/*
 * Check whether or not access to the share should be restricted to
 * administrators. This is a bit of a hack because what we're doing
 * is checking for the default admin shares: C$, D$ etc.. There are
 * other shares that have restrictions: see srvsvc_share_is_restricted().
 *
 * Returns B_TRUE if the shares is an admin share. Otherwise B_FALSE
 * is returned to indicate that there are no restrictions.
 */
static boolean_t
srvsvc_share_is_admin(const char *sharename)
{
	if (sharename == NULL)
		return (B_FALSE);

	if (strlen(sharename) == 2 &&
	    smb_isalpha(sharename[0]) && sharename[1] == '$') {
		return (B_TRUE);
	}

	return (B_FALSE);
}

/*
 * Stores the given share in sharemgr
 */
static uint32_t
srvsvc_sa_add(char *sharename, char *path, char *cmnt)
{
	nvlist_t *share;
	boolean_t new_share = B_FALSE;
	char errbuf[512];
	int rc;

	if (sa_share_read(path, sharename, &share) != SA_OK) {
		new_share = B_TRUE;
		if ((share = sa_share_alloc(sharename, path)) == NULL)
			return (ERROR_NOT_ENOUGH_MEMORY);
	}

	if (cmnt != NULL && *cmnt != '\0' &&
	    ((rc = sa_share_set_desc(share, cmnt)) != SA_OK))
		goto done;

	if (sa_share_get_proto(share, SA_PROT_SMB) == NULL) {
		if ((rc = sa_share_set_def_proto(share, SA_PROT_SMB)) != SA_OK)
			goto done;
	}

	if ((rc = sa_share_validate(share, new_share, errbuf,
	    sizeof (errbuf))) != SA_OK)
		goto done;

	if ((rc = sa_share_write(share)) != SA_OK)
		goto done;

	if ((sa_sharing_enabled(path) & SA_PROT_SMB) == 0)
		rc = sa_sharing_set_prop(path, SA_PROT_SMB, "on");

done:
	sa_share_free(share);
	return (smb_share_lmerr(rc));
}

/*
 * Removes the share via libshare
 *
 * Remove the SMB properties from the share.
 * If there are no other protocols enabled for this share,
 * remove it, otherwise save it.
 */
static uint32_t
srvsvc_sa_delete(const char *sharename, const char *path)
{
	nvlist_t *share;
	int rc;

	rc = sa_share_read(path, sharename, &share);
	if (rc != SA_OK)
		return (smb_share_lmerr(rc));

	(void) sa_share_rem_proto(share, SA_PROT_SMB);
	if (sa_share_proto_count(share) == 0)
		rc = sa_share_remove(sharename, path);
	else
		rc = sa_share_write(share);

	sa_share_free(share);
	return (smb_share_lmerr(rc));
}

/*
 * Update the share information.
 */
static uint32_t
srvsvc_sa_modify(smb_share_t *si, srvsvc_netshare_setinfo_t *info)
{
	nvlist_t *share;
	int rc = SA_OK;
	boolean_t new_cmnt = B_FALSE;
	boolean_t new_name = B_FALSE;

	rc = sa_share_read(si->shr_path, si->shr_name, &share);
	if (rc != SA_OK)
		return (smb_share_lmerr(rc));

	if (info->nss_comment == NULL) {
		/*
		 * remove share description if it exists
		 */
		if (si->shr_cmnt != NULL) {
			(void) sa_share_rem_desc(share);
			new_cmnt = B_TRUE;
		}
	} else {
		if ((si->shr_cmnt == NULL) ||
		    strcmp(info->nss_comment, si->shr_cmnt) != 0) {
			/*
			 * description has been added or modified
			 */
			rc = sa_share_set_desc(share, info->nss_comment);
			if (rc != SA_OK) {
				sa_share_free(share);
				return (smb_share_lmerr(rc));
			}
			new_cmnt = B_TRUE;
		}
	}

	if (info->nss_netname != NULL && info->nss_netname[0] != 0 &&
	    smb_strcasecmp(info->nss_netname, si->shr_name, 0) != 0) {
		/*
		 * info contains new name, update share
		 */
		new_name = B_TRUE;
		rc = sa_share_set_name(share, info->nss_netname);
		if (rc != SA_OK) {
			sa_share_free(share);
			return (smb_share_lmerr(rc));
		}
	}

	if (new_cmnt || new_name) {
		/*
		 * share has been updated, save and publish
		 */
		if ((rc = sa_share_write(share)) != SA_OK) {
			sa_share_free(share);
			return (smb_share_lmerr(rc));
		}

		/*
		 * share was renamed, unpublish and remove old share
		 */
		if (new_name)
			rc = sa_share_remove(si->shr_name, si->shr_path);
	}

	sa_share_free(share);
	return (smb_share_lmerr(rc));

}

/*
 * Sets the share properties.
 *
 * Updates the smb protocol properties of the share.
 * The properties are given as a list of name-value pair.
 * The name argument should be the optionset property name and the value
 * should be a valid value for the specified property.
 */
static uint32_t
srvsvc_sa_setprop(smb_share_t *si, nvlist_t *nvl)
{
	nvlist_t *share;
	nvlist_t *proplist;
	nvpair_t *curprop;
	char *propname;
	char *propval;
	boolean_t modified = B_FALSE;
	int rc;
	uint32_t status = NERR_Success;
	char errbuf[512];

	rc = sa_share_read(si->shr_path, si->shr_name, &share);
	if (rc != SA_OK)
		return (smb_share_lmerr(rc));

	if ((proplist = sa_share_get_proto(share, SA_PROT_SMB)) == NULL) {
		sa_share_free(share);
		return (NERR_InternalError);
	}

	curprop = nvlist_next_nvpair(nvl, NULL);
	while (curprop != NULL) {
		propname = nvpair_name(curprop);
		rc = nvpair_value_string(curprop, &propval);
		if ((rc != 0) || (propname == NULL) || (propval == NULL)) {
			status = NERR_InternalError;
			break;
		}

		if (sa_share_set_prop(proplist, propname, propval) != SA_OK) {
			status = NERR_InternalError;
			break;
		}
		modified = B_TRUE;

		curprop = nvlist_next_nvpair(nvl, curprop);
	}

	if ((status == NERR_Success) && modified) {
		/*
		 * validate and update share
		 */
		rc = sa_share_validate(share, 0, errbuf, sizeof (errbuf));
		if (rc == SA_OK)
			rc = sa_share_write(share);
		status = smb_share_lmerr(rc);
	}

	sa_share_free(share);
	return (status);
}

static ndr_stub_table_t srvsvc_stub_table[] = {
	{ srvsvc_s_NetConnectEnum,	SRVSVC_OPNUM_NetConnectEnum },
	{ srvsvc_s_NetFileEnum,		SRVSVC_OPNUM_NetFileEnum },
	{ srvsvc_s_NetFileClose,	SRVSVC_OPNUM_NetFileClose },
	{ srvsvc_s_NetShareGetInfo,	SRVSVC_OPNUM_NetShareGetInfo },
	{ srvsvc_s_NetShareSetInfo,	SRVSVC_OPNUM_NetShareSetInfo },
	{ srvsvc_s_NetSessionEnum,	SRVSVC_OPNUM_NetSessionEnum },
	{ srvsvc_s_NetSessionDel,	SRVSVC_OPNUM_NetSessionDel },
	{ srvsvc_s_NetServerGetInfo,	SRVSVC_OPNUM_NetServerGetInfo },
	{ srvsvc_s_NetRemoteTOD,	SRVSVC_OPNUM_NetRemoteTOD },
	{ srvsvc_s_NetNameValidate,	SRVSVC_OPNUM_NetNameValidate },
	{ srvsvc_s_NetShareAdd,		SRVSVC_OPNUM_NetShareAdd },
	{ srvsvc_s_NetShareDel,		SRVSVC_OPNUM_NetShareDel },
	{ srvsvc_s_NetShareEnum,	SRVSVC_OPNUM_NetShareEnum },
	{ srvsvc_s_NetShareEnumSticky,	SRVSVC_OPNUM_NetShareEnumSticky },
	{ srvsvc_s_NetShareCheck,	SRVSVC_OPNUM_NetShareCheck },
	{ srvsvc_s_NetGetFileSecurity,	SRVSVC_OPNUM_NetGetFileSecurity },
	{ srvsvc_s_NetSetFileSecurity,	SRVSVC_OPNUM_NetSetFileSecurity },
	{0}
};
