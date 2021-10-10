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

#include <sys/sunddi.h>
#ifndef _KERNEL
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stddef.h>
#endif /* _KERNEL */
#include <smbsrv/smb_door.h>
#include <smbsrv/alloc.h>
#include <sys/socket.h>
#include <sys/sysmacros.h>
#include <smb/wintypes.h>
#include <smb/smb_sid.h>
#include <smbsrv/smb_xdr.h>
#include <smbsrv/smb_token.h>

#define	SMB_XDRMAX16_SZ		0xFFFF
#define	SMB_XDRMAX32_SZ		0xFFFFFFFF

static bool_t smb_list_xdr(XDR *, list_t *,  const size_t, const size_t,
    const xdrproc_t);
static bool_t smb_privset_xdr(XDR *, smb_privset_t *);
static bool_t smb_sid_xdr(XDR *, smb_sid_t *);
static bool_t smb_token_xdr(XDR *, smb_token_t *);

bool_t
smb_buf16_xdr(XDR *xdrs, smb_buf16_t *objp)
{
	uint32_t maxsize = SMB_XDRMAX16_SZ;
	uint32_t size;

	if (xdrs->x_op != XDR_DECODE)
		maxsize = size = (uint16_t)objp->len;

	if (xdr_bytes(xdrs, (char **)&objp->val, &size, maxsize) == TRUE) {
		if (xdrs->x_op == XDR_DECODE)
			objp->len = (uint16_t)size;
		return (TRUE);
	}

	return (FALSE);
}

bool_t
smb_buf32_xdr(XDR *xdrs, smb_buf32_t *objp)
{
	uint_t	maxsize = SMB_XDRMAX32_SZ;
	uint_t	size;

	if (xdrs->x_op != XDR_DECODE)
		maxsize = size = (uint_t)objp->len;

	if (xdr_bytes(xdrs, (char **)&objp->val, &size, maxsize)) {
		if (xdrs->x_op == XDR_DECODE)
			objp->len = (uint32_t)size;
		return (TRUE);
	}

	return (FALSE);
}

/*
 * When decoding into a string, ensure that objp->buf is NULL or
 * is pointing at a buffer large enough to receive the string.
 * Don't leave it as an uninitialized pointer.
 *
 * If objp->buf is NULL, xdr_string will allocate memory for the
 * string.  Otherwise it will copy into the available buffer.
 */
bool_t
smb_string_xdr(XDR *xdrs, smb_string_t *objp)
{
	if (!xdr_string(xdrs, &objp->buf, ~0))
		return (FALSE);
	return (TRUE);
}

const char *
smb_doorhdr_opname(uint32_t op)
{
	struct {
		uint32_t	op;
		const char	*name;
	} ops[] = {
		{ SMB_DR_NULL,			"null" },
		{ SMB_DR_ASYNC_RESPONSE,	"async_response" },
		{ SMB_DR_USER_AUTH,		"user_auth" },
		{ SMB_DR_USER_LOGOFF,		"user_logoff" },
		{ SMB_DR_LOOKUP_SID,		"lookup_sid" },
		{ SMB_DR_LOOKUP_NAME,		"lookup_name" },
		{ SMB_DR_LOCATE_DC,		"locate_dc" },
		{ SMB_DR_JOIN,			"join" },
		{ SMB_DR_GET_DCINFO,		"get_dcinfo" },
		{ SMB_DR_GET_FQDN,		"get_fqdn" },
		{ SMB_DR_VSS_GET_COUNT,		"vss_get_count" },
		{ SMB_DR_VSS_GET_SNAPSHOTS,	"vss_get_snapshots" },
		{ SMB_DR_VSS_MAP_GMTTOKEN,	"vss_map_gmttoken" },
		{ SMB_DR_ADS_FIND_HOST,		"ads_find_host" },
		{ SMB_DR_QUOTA_QUERY,		"quota_query" },
		{ SMB_DR_QUOTA_SET,		"quota_set" },
		{ SMB_DR_DFS_GET_REFERRALS,	"dfs_get_referrals" },
		{ SMB_DR_SHR_HOSTACCESS,	"share_hostaccess" },
		{ SMB_DR_SHR_EXEC,		"share_exec" },
		{ SMB_DR_SHR_NOTIFY,		"share_notify" },
		{ SMB_DR_SHR_PUBLISH_ADMIN,	"share_publish_admin" },
		{ SMB_DR_SPOOLDOC,		"spooldoc" },
		{ SMB_DR_SESSION_CREATE,	"session_create" },
		{ SMB_DR_SESSION_DESTROY,	"session_destroy" },
		{ SMB_DR_GET_DOMAINS_INFO,	"get_domains_info" },
	};
	int	i;

	for (i = 0; i < (sizeof (ops) / sizeof (ops[0])); ++i) {
		if (ops[i].op == op)
			return (ops[i].name);
	}

	return ("unknown");
}

/*
 * Encode a door header structure into an XDR buffer.
 */
int
smb_doorhdr_encode(smb_doorhdr_t *hdr, uint8_t *buf, uint32_t buflen)
{
	XDR xdrs;
	int rc = 0;

	xdrmem_create(&xdrs, (const caddr_t)buf, buflen, XDR_ENCODE);

	if (!smb_doorhdr_xdr(&xdrs, hdr))
		rc = -1;

	xdr_destroy(&xdrs);
	return (rc);
}

/*
 * Decode an XDR buffer into a door header structure.
 */
int
smb_doorhdr_decode(smb_doorhdr_t *hdr, uint8_t *buf, uint32_t buflen)
{
	XDR xdrs;
	int rc = 0;

	bzero(hdr, sizeof (smb_doorhdr_t));
	xdrmem_create(&xdrs, (const caddr_t)buf, buflen, XDR_DECODE);

	if (!smb_doorhdr_xdr(&xdrs, hdr)) {
		xdr_free(smb_doorhdr_xdr, (char *)hdr);
		rc = -1;
	}

	xdr_destroy(&xdrs);
	return (rc);
}

bool_t
smb_doorhdr_xdr(XDR *xdrs, smb_doorhdr_t *objp)
{
	if (!xdr_uint32_t(xdrs, &objp->dh_magic))
		return (FALSE);
	if (!xdr_uint32_t(xdrs, &objp->dh_flags))
		return (FALSE);
	if (!xdr_uint32_t(xdrs, &objp->dh_fid))
		return (FALSE);
	if (!xdr_uint32_t(xdrs, &objp->dh_op))
		return (FALSE);
	if (!xdr_uint32_t(xdrs, &objp->dh_txid))
		return (FALSE);
	if (!xdr_uint32_t(xdrs, &objp->dh_datalen))
		return (FALSE);
	if (!xdr_uint32_t(xdrs, &objp->dh_resid))
		return (FALSE);
	if (!xdr_uint32_t(xdrs, &objp->dh_door_rc))
		return (FALSE);
	return (TRUE);
}

/*
 * Encode an smb_netuserinfo_t into a buffer.
 */
int
smb_netuserinfo_encode(smb_netuserinfo_t *info, uint8_t *buf,
    uint32_t buflen, uint_t *nbytes)
{
	XDR xdrs;
	int rc = 0;

	xdrmem_create(&xdrs, (const caddr_t)buf, buflen, XDR_ENCODE);

	if (!smb_netuserinfo_xdr(&xdrs, info))
		rc = -1;

	if (nbytes != NULL)
		*nbytes = xdr_getpos(&xdrs);
	xdr_destroy(&xdrs);
	return (rc);
}

/*
 * Decode an XDR buffer into an smb_netuserinfo_t.
 */
int
smb_netuserinfo_decode(smb_netuserinfo_t *info, uint8_t *buf,
    uint32_t buflen, uint_t *nbytes)
{
	XDR xdrs;
	int rc = 0;

	xdrmem_create(&xdrs, (const caddr_t)buf, buflen, XDR_DECODE);

	bzero(info, sizeof (smb_netuserinfo_t));
	if (!smb_netuserinfo_xdr(&xdrs, info)) {
		xdr_free(smb_netuserinfo_xdr, (char *)info);
		rc = -1;
	}

	if (nbytes != NULL)
		*nbytes = xdr_getpos(&xdrs);
	xdr_destroy(&xdrs);
	return (rc);
}

bool_t
smb_inaddr_xdr(XDR *xdrs, smb_inaddr_t *objp)
{
	if (!xdr_int32_t(xdrs, &objp->a_family))
		return (FALSE);
	if (objp->a_family == AF_INET) {
		if (!xdr_uint32_t(xdrs, (in_addr_t *)&objp->a_ipv4))
			return (FALSE);
	} else {
		if (!xdr_vector(xdrs, (char *)&objp->a_ipv6,
		    sizeof (objp->a_ipv6), sizeof (char), (xdrproc_t)xdr_char))
			return (FALSE);
	}
	return (TRUE);
}

/*
 * XDR encode/decode for smb_netuserinfo_t.
 */
bool_t
smb_netuserinfo_xdr(XDR *xdrs, smb_netuserinfo_t *objp)
{
	if (!xdr_uint64_t(xdrs, &objp->ui_session_id))
		return (FALSE);
	if (!xdr_uint16_t(xdrs, &objp->ui_smb_uid))
		return (FALSE);
	if (!xdr_string(xdrs, &objp->ui_domain, ~0))
		return (FALSE);
	if (!xdr_string(xdrs, &objp->ui_account, ~0))
		return (FALSE);
	if (!xdr_string(xdrs, &objp->ui_posix_name, ~0))
		return (FALSE);
	if (!xdr_string(xdrs, &objp->ui_workstation, ~0))
		return (FALSE);
	if (!smb_inaddr_xdr(xdrs, &objp->ui_ipaddr))
		return (FALSE);
	if (!xdr_int32_t(xdrs, &objp->ui_native_os))
		return (FALSE);
	if (!xdr_int64_t(xdrs, &objp->ui_logon_time))
		return (FALSE);
	if (!xdr_uint32_t(xdrs, &objp->ui_numopens))
		return (FALSE);
	if (!xdr_uint32_t(xdrs, &objp->ui_flags))
		return (FALSE);
	return (TRUE);
}

/*
 * Encode an smb_netconnectinfo_t into a buffer.
 */
int
smb_netconnectinfo_encode(smb_netconnectinfo_t *info, uint8_t *buf,
    uint32_t buflen, uint_t *nbytes)
{
	XDR xdrs;
	int rc = 0;

	xdrmem_create(&xdrs, (const caddr_t)buf, buflen, XDR_ENCODE);

	if (!smb_netconnectinfo_xdr(&xdrs, info))
		rc = -1;

	if (nbytes != NULL)
		*nbytes = xdr_getpos(&xdrs);
	xdr_destroy(&xdrs);
	return (rc);
}

/*
 * Decode an XDR buffer into an smb_netconnectinfo_t.
 */
int
smb_netconnectinfo_decode(smb_netconnectinfo_t *info, uint8_t *buf,
    uint32_t buflen, uint_t *nbytes)
{
	XDR xdrs;
	int rc = 0;

	xdrmem_create(&xdrs, (const caddr_t)buf, buflen, XDR_DECODE);

	bzero(info, sizeof (smb_netconnectinfo_t));
	if (!smb_netconnectinfo_xdr(&xdrs, info)) {
		xdr_free(smb_netconnectinfo_xdr, (char *)info);
		rc = -1;
	}

	if (nbytes != NULL)
		*nbytes = xdr_getpos(&xdrs);
	xdr_destroy(&xdrs);
	return (rc);
}

/*
 * XDR encode/decode for smb_netconnectinfo_t.
 */
bool_t
smb_netconnectinfo_xdr(XDR *xdrs, smb_netconnectinfo_t *objp)
{
	if (!xdr_uint32_t(xdrs, &objp->ci_id))
		return (FALSE);
	if (!xdr_uint32_t(xdrs, &objp->ci_type))
		return (FALSE);
	if (!xdr_uint32_t(xdrs, &objp->ci_numopens))
		return (FALSE);
	if (!xdr_uint32_t(xdrs, &objp->ci_numusers))
		return (FALSE);
	if (!xdr_uint32_t(xdrs, &objp->ci_time))
		return (FALSE);
	if (!xdr_uint32_t(xdrs, &objp->ci_namelen))
		return (FALSE);
	if (!xdr_uint32_t(xdrs, &objp->ci_sharelen))
		return (FALSE);
	if (!xdr_string(xdrs, &objp->ci_username, MAXNAMELEN))
		return (FALSE);
	if (!xdr_string(xdrs, &objp->ci_share, MAXNAMELEN))
		return (FALSE);
	return (TRUE);
}

/*
 * Encode an smb_netfileinfo_t into a buffer.
 */
int
smb_netfileinfo_encode(smb_netfileinfo_t *info, uint8_t *buf,
    uint32_t buflen, uint_t *nbytes)
{
	XDR xdrs;
	int rc = 0;

	xdrmem_create(&xdrs, (const caddr_t)buf, buflen, XDR_ENCODE);

	if (!smb_netfileinfo_xdr(&xdrs, info))
		rc = -1;

	if (nbytes != NULL)
		*nbytes = xdr_getpos(&xdrs);
	xdr_destroy(&xdrs);
	return (rc);
}

/*
 * Decode an XDR buffer into an smb_netfileinfo_t.
 */
int
smb_netfileinfo_decode(smb_netfileinfo_t *info, uint8_t *buf,
    uint32_t buflen, uint_t *nbytes)
{
	XDR xdrs;
	int rc = 0;

	xdrmem_create(&xdrs, (const caddr_t)buf, buflen, XDR_DECODE);

	bzero(info, sizeof (smb_netfileinfo_t));
	if (!smb_netfileinfo_xdr(&xdrs, info)) {
		xdr_free(smb_netfileinfo_xdr, (char *)info);
		rc = -1;
	}

	if (nbytes != NULL)
		*nbytes = xdr_getpos(&xdrs);
	xdr_destroy(&xdrs);
	return (rc);
}

/*
 * XDR encode/decode for smb_netfileinfo_t.
 */
bool_t
smb_netfileinfo_xdr(XDR *xdrs, smb_netfileinfo_t *objp)
{
	if (!xdr_uint16_t(xdrs, &objp->fi_fid))
		return (FALSE);
	if (!xdr_uint32_t(xdrs, &objp->fi_uniqid))
		return (FALSE);
	if (!xdr_uint32_t(xdrs, &objp->fi_permissions))
		return (FALSE);
	if (!xdr_uint32_t(xdrs, &objp->fi_numlocks))
		return (FALSE);
	if (!xdr_uint32_t(xdrs, &objp->fi_pathlen))
		return (FALSE);
	if (!xdr_uint32_t(xdrs, &objp->fi_namelen))
		return (FALSE);
	if (!xdr_string(xdrs, &objp->fi_path, MAXPATHLEN))
		return (FALSE);
	if (!xdr_string(xdrs, &objp->fi_username, MAXNAMELEN))
		return (FALSE);
	return (TRUE);
}

bool_t
smb_gmttoken_query_xdr(XDR *xdrs, smb_gmttoken_query_t *objp)
{
	if (!xdr_uint32_t(xdrs, &objp->gtq_count)) {
		return (FALSE);
	}
	if (!xdr_string(xdrs, &objp->gtq_path, ~0)) {
		return (FALSE);
	}
	return (TRUE);
}

static bool_t
smb_gmttoken_xdr(XDR *xdrs, smb_gmttoken_t *objp)
{
	if (!xdr_string(xdrs, objp, SMB_VSS_GMT_SIZE)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
smb_gmttoken_response_xdr(XDR *xdrs, smb_gmttoken_response_t *objp)
{
	if (!xdr_uint32_t(xdrs, &objp->gtr_count)) {
		return (FALSE);
	}
	if (!xdr_array(xdrs, (char **)&objp->gtr_gmttokens.gtr_gmttokens_val,
	    (uint_t *)&objp->gtr_gmttokens.gtr_gmttokens_len, ~0,
	    sizeof (smb_gmttoken_t), (xdrproc_t)smb_gmttoken_xdr)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
smb_gmttoken_snapname_xdr(XDR *xdrs, smb_gmttoken_snapname_t *objp)
{
	if (!xdr_string(xdrs, &objp->gts_path, MAXPATHLEN)) {
		return (FALSE);
	}
	if (!xdr_string(xdrs, &objp->gts_gmttoken, SMB_VSS_GMT_SIZE)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
smb_quota_xdr(XDR *xdrs, smb_quota_t *objp)
{
	if (!xdr_vector(xdrs, (char *)objp->q_sidstr, SMB_SID_STRSZ,
	    sizeof (char), (xdrproc_t)xdr_char))
		return (FALSE);
	if (!xdr_uint32_t(xdrs, &objp->q_sidtype))
		return (FALSE);
	if (!xdr_uint64_t(xdrs, &objp->q_used))
		return (FALSE);
	if (!xdr_uint64_t(xdrs, &objp->q_thresh))
		return (FALSE);
	if (!xdr_uint64_t(xdrs, &objp->q_limit))
		return (FALSE);

	return (TRUE);
}

bool_t
smb_quota_sid_xdr(XDR *xdrs, smb_quota_sid_t *objp)
{
	if (!xdr_uint32_t(xdrs, &objp->qs_idtype))
		return (FALSE);
	if (!xdr_uint32_t(xdrs, &objp->qs_id))
		return (FALSE);
	if (!xdr_vector(xdrs, (char *)objp->qs_sidstr, SMB_SID_STRSZ,
	    sizeof (char), (xdrproc_t)xdr_char))
		return (FALSE);
	return (TRUE);
}

bool_t
smb_quota_query_xdr(XDR *xdrs, smb_quota_query_t *objp)
{
	if (!xdr_string(xdrs, &objp->qq_root_path, ~0))
		return (FALSE);
	if (!xdr_uint32_t(xdrs, &objp->qq_query_op))
		return (FALSE);
	if (!xdr_bool(xdrs, &objp->qq_single))
		return (FALSE);
	if (!xdr_bool(xdrs, &objp->qq_restart))
		return (FALSE);
	if (!xdr_uint32_t(xdrs, &objp->qq_max_quota))
		return (FALSE);
	if (!smb_list_xdr(xdrs, &objp->qq_sid_list,
	    offsetof(smb_quota_sid_t, qs_list_node),
	    sizeof (smb_quota_sid_t), (xdrproc_t)smb_quota_sid_xdr))
		return (FALSE);

	return (TRUE);
}

bool_t
smb_quota_response_xdr(XDR *xdrs, smb_quota_response_t *objp)
{
	if (!xdr_uint32_t(xdrs, &objp->qr_status))
		return (FALSE);
	if (!smb_list_xdr(xdrs, &objp->qr_quota_list,
	    offsetof(smb_quota_t, q_list_node),
	    sizeof (smb_quota_t), (xdrproc_t)smb_quota_xdr))
		return (FALSE);
	return (TRUE);
}

bool_t
smb_quota_set_xdr(XDR *xdrs, smb_quota_set_t *objp)
{
	if (!xdr_string(xdrs, &objp->qs_root_path, ~0))
		return (FALSE);
	if (!smb_list_xdr(xdrs, &objp->qs_quota_list,
	    offsetof(smb_quota_t, q_list_node),
	    sizeof (smb_quota_t), (xdrproc_t)smb_quota_xdr))
		return (FALSE);
	return (TRUE);
}

/*
 * XDR a list_t list of elements
 * offset - offset of list_node_t in list element
 * elsize - size of list element
 * elproc - XDR function for the list element
 */
bool_t
smb_list_xdr(XDR *xdrs, list_t *list,  const size_t offset,
    const size_t elsize, const xdrproc_t elproc)
{
	void *node;
	uint32_t count = 0;

	switch (xdrs->x_op) {
	case XDR_ENCODE:
		node = list_head(list);
		while (node) {
			++count;
			node = list_next(list, node);
		}
		if (!xdr_uint32_t(xdrs, &count))
			return (FALSE);

		node = list_head(list);
		while (node) {
			if (!elproc(xdrs, node))
				return (FALSE);
			node = list_next(list, node);
		}
		return (TRUE);

	case XDR_DECODE:
		if (!xdr_uint32_t(xdrs, &count))
			return (FALSE);
		list_create(list, elsize, offset);
		while (count) {
			node = MEM_MALLOC("xdr", elsize);
			if (node == NULL)
				return (FALSE);
			if (!elproc(xdrs, node))
				return (FALSE);
			list_insert_tail(list, node);
			--count;
		}
		return (TRUE);

	case XDR_FREE:
		if (list->list_size == 0)
			return (TRUE);

		while ((node = list_head(list)) != NULL) {
			list_remove(list, node);
			(void) elproc(xdrs, node);
			MEM_FREE("xdr", node);
		}
		list_destroy(list);
		bzero(list, sizeof (list_t));
		return (TRUE);
	}

	return (FALSE);
}

bool_t
dfs_target_pclass_xdr(XDR *xdrs, dfs_target_pclass_t *objp)
{
	return (xdr_enum(xdrs, (enum_t *)objp));
}

bool_t
dfs_target_priority_xdr(XDR *xdrs, dfs_target_priority_t *objp)
{
	if (!dfs_target_pclass_xdr(xdrs, &objp->p_class))
		return (FALSE);

	if (!xdr_uint16_t(xdrs, &objp->p_rank))
		return (FALSE);

	return (TRUE);
}

bool_t
dfs_target_xdr(XDR *xdrs, dfs_target_t *objp)
{
	if (!xdr_vector(xdrs, (char *)objp->t_server, DFS_SRVNAME_MAX,
	    sizeof (char), (xdrproc_t)xdr_char))
		return (FALSE);

	if (!xdr_vector(xdrs, (char *)objp->t_share, DFS_NAME_MAX,
	    sizeof (char), (xdrproc_t)xdr_char))
		return (FALSE);

	if (!xdr_uint32_t(xdrs, &objp->t_state))
		return (FALSE);

	if (!dfs_target_priority_xdr(xdrs, &objp->t_priority))
		return (FALSE);

	return (TRUE);
}

bool_t
dfs_reftype_xdr(XDR *xdrs, dfs_reftype_t *objp)
{
	return (xdr_enum(xdrs, (enum_t *)objp));
}

bool_t
dfs_info_xdr(XDR *xdrs, dfs_info_t *objp)
{
	if (!xdr_vector(xdrs, (char *)objp->i_uncpath, DFS_PATH_MAX,
	    sizeof (char), (xdrproc_t)xdr_char))
		return (FALSE);

	if (!xdr_vector(xdrs, (char *)objp->i_comment, DFS_COMMENT_MAX,
	    sizeof (char), (xdrproc_t)xdr_char))
		return (FALSE);

	if (!xdr_vector(xdrs, (char *)objp->i_guid,
	    UUID_PRINTABLE_STRING_LENGTH, sizeof (char), (xdrproc_t)xdr_char))
		return (FALSE);

	if (!xdr_uint32_t(xdrs, &objp->i_state))
		return (FALSE);

	if (!xdr_uint32_t(xdrs, &objp->i_timeout))
		return (FALSE);

	if (!xdr_uint32_t(xdrs, &objp->i_propflags))
		return (FALSE);

	if (!xdr_uint32_t(xdrs, &objp->i_type))
		return (FALSE);

	if (!xdr_array(xdrs, (char **)&objp->i_targets,
	    (uint32_t *)&objp->i_ntargets, ~0, sizeof (dfs_target_t),
	    (xdrproc_t)dfs_target_xdr))
		return (FALSE);

	return (TRUE);
}

bool_t
dfs_referral_query_xdr(XDR *xdrs, dfs_referral_query_t *objp)
{
	if (!dfs_reftype_xdr(xdrs, &objp->rq_type))
		return (FALSE);

	if (!xdr_string(xdrs, &objp->rq_path, ~0))
		return (FALSE);

	return (TRUE);
}

bool_t
dfs_referral_response_xdr(XDR *xdrs, dfs_referral_response_t *objp)
{
	if (!dfs_info_xdr(xdrs, &objp->rp_referrals))
		return (FALSE);

	if (!xdr_uint32_t(xdrs, &objp->rp_status))
		return (FALSE);

	return (TRUE);
}

bool_t
smb_shr_hostaccess_query_xdr(XDR *xdrs, smb_shr_hostaccess_query_t *objp)
{
	if (!xdr_string(xdrs, &objp->shq_none, ~0))
		return (FALSE);

	if (!xdr_string(xdrs, &objp->shq_ro, ~0))
		return (FALSE);

	if (!xdr_string(xdrs, &objp->shq_rw, ~0))
		return (FALSE);

	if (!xdr_uint32_t(xdrs, &objp->shq_flag))
		return (FALSE);

	if (!smb_inaddr_xdr(xdrs, &objp->shq_ipaddr))
		return (FALSE);

	return (TRUE);
}

bool_t
smb_shr_execinfo_xdr(XDR *xdrs, smb_shr_execinfo_t *objp)
{
	if (!xdr_string(xdrs, &objp->e_sharename, ~0))
		return (FALSE);

	if (!xdr_string(xdrs, &objp->e_sharepath, ~0))
		return (FALSE);

	if (!xdr_string(xdrs, &objp->e_winname, ~0))
		return (FALSE);

	if (!xdr_string(xdrs, &objp->e_userdom, ~0))
		return (FALSE);

	if (!smb_inaddr_xdr(xdrs, &objp->e_srv_ipaddr))
		return (FALSE);

	if (!smb_inaddr_xdr(xdrs, &objp->e_cli_ipaddr))
		return (FALSE);

	if (!xdr_string(xdrs, &objp->e_cli_netbiosname, ~0))
		return (FALSE);

	if (!xdr_u_int(xdrs, &objp->e_uid))
		return (FALSE);

	if (!xdr_int(xdrs, &objp->e_type))
		return (FALSE);

	return (TRUE);
}

bool_t
smb_share_xdr(XDR *xdrs, smb_share_t *objp)
{
	if (!xdr_string(xdrs, &objp->shr_name, ~0))
		return (FALSE);
	if (!xdr_string(xdrs, &objp->shr_path, ~0))
		return (FALSE);
	if (!xdr_string(xdrs, &objp->shr_cmnt, ~0))
		return (FALSE);
	if (!xdr_string(xdrs, &objp->shr_winpath, ~0))
		return (FALSE);
	if (!xdr_string(xdrs, &objp->shr_container, ~0))
		return (FALSE);
	if (!xdr_string(xdrs, &objp->shr_access_none, ~0))
		return (FALSE);
	if (!xdr_string(xdrs, &objp->shr_access_ro, ~0))
		return (FALSE);
	if (!xdr_string(xdrs, &objp->shr_access_rw, ~0))
		return (FALSE);
	if (!xdr_uint32_t(xdrs, &objp->shr_flags))
		return (FALSE);
	if (!xdr_uint32_t(xdrs, &objp->shr_type))
		return (FALSE);
	if (!xdr_uint32_t(xdrs, &objp->shr_uid))
		return (FALSE);
	if (!xdr_uint32_t(xdrs, &objp->shr_gid))
		return (FALSE);
	if (!xdr_u_char(xdrs, &objp->shr_drive))
		return (FALSE);
	return (TRUE);
}

bool_t
smb_shr_notify_xdr(XDR *xdrs, smb_shr_notify_t *objp)
{
	if (!xdr_uint32_t(xdrs, &objp->sn_op))
		return (FALSE);
	if (!xdr_uint32_t(xdrs, &objp->sn_dfsroot))
		return (FALSE);
	if (!xdr_string(xdrs, &objp->sn_name, ~0))
		return (FALSE);
	if (!xdr_string(xdrs, &objp->sn_path, ~0))
		return (FALSE);
	if (!xdr_string(xdrs, &objp->sn_container, ~0))
		return (FALSE);
	if (!xdr_string(xdrs, &objp->sn_newcontainer, ~0))
		return (FALSE);
	return (TRUE);
}

/*
 * Encode an smb_share_t into a buffer.
 */
int
smb_share_encode(smb_share_t *info, uint8_t *buf,
    uint32_t buflen, uint_t *nbytes)
{
	XDR xdrs;
	int rc = 0;

	xdrmem_create(&xdrs, (const caddr_t)buf, buflen, XDR_ENCODE);

	if (!smb_share_xdr(&xdrs, info))
		rc = -1;

	if (nbytes != NULL)
		*nbytes = xdr_getpos(&xdrs);
	xdr_destroy(&xdrs);
	return (rc);
}

/*
 * Decode an XDR buffer into an smb_share_t.
 */
int
smb_share_decode(smb_share_t *info, uint8_t *buf,
    uint32_t buflen, uint_t *nbytes)
{
	XDR xdrs;
	int rc = 0;

	xdrmem_create(&xdrs, (const caddr_t)buf, buflen, XDR_DECODE);

	bzero(info, sizeof (smb_share_t));
	if (!smb_share_xdr(&xdrs, info)) {
		xdr_free(smb_share_xdr, (char *)info);
		rc = -1;
	}

	if (nbytes != NULL)
		*nbytes = xdr_getpos(&xdrs);
	xdr_destroy(&xdrs);
	return (rc);
}

/*
 * XDR encode/decode for smb_spooldoc_t.
 */
bool_t
smb_spooldoc_xdr(XDR *xdrs, smb_spooldoc_t *objp)
{
	if (!smb_inaddr_xdr(xdrs, &objp->sd_ipaddr))
		return (FALSE);
	if (!xdr_vector(xdrs, (char *)objp->sd_username, MAXNAMELEN,
	    sizeof (char), (xdrproc_t)xdr_char))
		return (FALSE);
	if (!xdr_vector(xdrs, (char *)objp->sd_docname, MAXNAMELEN,
	    sizeof (char), (xdrproc_t)xdr_char))
		return (FALSE);
	if (!xdr_vector(xdrs, (char *)objp->sd_path, MAXPATHLEN,
	    sizeof (char), (xdrproc_t)xdr_char))
		return (FALSE);
	if (!xdr_vector(xdrs, (char *)objp->sd_printer, MAXNAMELEN,
	    sizeof (char), (xdrproc_t)xdr_char))
		return (FALSE);
	return (TRUE);
}

static bool_t
smb_privset_helper_xdr(XDR *xdrs, char **privs)
{
	uint32_t pos, len;
	uint32_t cnt;
	bool_t rc;
	smb_privset_t *p;

	if (xdrs->x_op == XDR_DECODE) {
		pos = xdr_getpos(xdrs);

		if (!xdr_bool(xdrs, &rc))
			return (FALSE);

		if (!xdr_uint32_t(xdrs, &cnt))
			return (FALSE);

		rc = xdr_setpos(xdrs, pos);

		if (rc == FALSE)
			return (FALSE);
	} else {
		if (*privs == NULL)
			return (FALSE);

		p = (smb_privset_t *)(uintptr_t)*privs;
		cnt = p->priv_cnt;
	}

	len = sizeof (smb_privset_t)
	    - sizeof (smb_luid_attrs_t)
	    + (cnt * sizeof (smb_luid_attrs_t));

	if (!xdr_pointer(xdrs, privs, len, (xdrproc_t)smb_privset_xdr))
		return (FALSE);

	return (TRUE);
}

static bool_t
smb_id_xdr(XDR *xdrs, smb_id_t *objp)
{
	uint8_t len;

	if ((xdrs->x_op == XDR_ENCODE) || (xdrs->x_op == XDR_FREE))
		len = smb_sid_len(objp->i_sid);

	if (!xdr_uint32_t(xdrs, &objp->i_attrs))
		return (FALSE);

	if (!xdr_uint8_t(xdrs, &len))
		return (FALSE);

	if (!xdr_pointer(xdrs, (char **)&objp->i_sid, len,
	    (xdrproc_t)smb_sid_xdr))
		return (FALSE);

	if (!xdr_uint32_t(xdrs, (uint32_t *)&objp->i_id))
		return (FALSE);

	return (TRUE);
}

static bool_t
smb_ids_xdr(XDR *xdrs, smb_ids_t *objp)
{
	if (!xdr_array(xdrs, (char **)&objp->i_ids, (uint32_t *)&objp->i_cnt,
	    ~0, sizeof (smb_id_t), (xdrproc_t)smb_id_xdr))
		return (FALSE);

	return (TRUE);
}

static bool_t
smb_posix_grps_xdr(XDR *xdrs, smb_posix_grps_t *objp)
{
	if (!xdr_uint32_t(xdrs, &objp->pg_ngrps))
		return (FALSE);
	if (!xdr_vector(xdrs, (char *)objp->pg_grps, objp->pg_ngrps,
	    sizeof (uint32_t), (xdrproc_t)xdr_uint32_t))
		return (FALSE);
	return (TRUE);
}

static bool_t
smb_posix_grps_helper_xdr(XDR *xdrs, char **identity)
{
	uint32_t pos, len;
	uint32_t cnt;
	bool_t rc;

	if (xdrs->x_op == XDR_DECODE) {
		pos = xdr_getpos(xdrs);

		if (!xdr_bool(xdrs, &rc))
			return (FALSE);

		if (!xdr_uint32_t(xdrs, &cnt))
			return (FALSE);

		rc = xdr_setpos(xdrs, pos);
		if (rc == FALSE)
			return (FALSE);
	} else {
		if (*identity == NULL)
			return (FALSE);
		cnt = ((smb_posix_grps_t *)(uintptr_t)*identity)->pg_ngrps;
	}

	len = SMB_POSIX_GRPS_SIZE(cnt);

	if (!xdr_pointer(xdrs, identity, len, (xdrproc_t)smb_posix_grps_xdr))
		return (FALSE);
	return (TRUE);
}

/*
 * All the fields in smb_authreq_t that are used only in userland are skipped
 * from encoding/decoding.
 */
bool_t
smb_authreq_xdr(XDR *xdrs, smb_authreq_t *objp)
{
	if (!xdr_uint64_t(xdrs, &objp->au_session_id))
		return (FALSE);
	if (!xdr_uint16_t(xdrs, &objp->au_user_id))
		return (FALSE);
	if (!xdr_uint16_t(xdrs, &objp->au_level))
		return (FALSE);
	if (!xdr_string(xdrs, &objp->au_username, ~0))
		return (FALSE);
	if (!xdr_string(xdrs, &objp->au_domain, ~0))
		return (FALSE);
	if (!xdr_string(xdrs, &objp->au_eusername, ~0))
		return (FALSE);
	if (!xdr_string(xdrs, &objp->au_edomain, ~0))
		return (FALSE);
	if (!xdr_string(xdrs, &objp->au_workstation, ~0))
		return (FALSE);
	if (!smb_inaddr_xdr(xdrs, &objp->au_clnt_ipaddr))
		return (FALSE);
	if (!smb_inaddr_xdr(xdrs, &objp->au_local_ipaddr))
		return (FALSE);
	if (!xdr_uint16_t(xdrs, &objp->au_local_port))
		return (FALSE);
	if (!smb_buf32_xdr(xdrs, &objp->au_challenge_key))
		return (FALSE);
	if (!smb_buf16_xdr(xdrs, &objp->au_ntpasswd))
		return (FALSE);
	if (!smb_buf16_xdr(xdrs, &objp->au_lmpasswd))
		return (FALSE);
	if (!smb_buf16_xdr(xdrs, &objp->au_secblob))
		return (FALSE);
	if (!xdr_int(xdrs, &objp->au_native_os))
		return (FALSE);
	if (!xdr_int(xdrs, &objp->au_native_lm))
		return (FALSE);
	if (!xdr_uint32_t(xdrs, &objp->au_flags))
		return (FALSE);
	if (!xdr_uint16_t(xdrs, &objp->au_vcnumber))
		return (FALSE);
	if (!xdr_uint16_t(xdrs, &objp->au_maxmpxcount))
		return (FALSE);
	if (!xdr_uint16_t(xdrs, &objp->au_maxbufsize))
		return (FALSE);
	if (!xdr_uint16_t(xdrs, &objp->au_guest))
		return (FALSE);
	if (!xdr_uint32_t(xdrs, &objp->au_capabilities))
		return (FALSE);
	if (!xdr_uint32_t(xdrs, &objp->au_sesskey))
		return (FALSE);

	return (TRUE);
}

bool_t
smb_authrsp_xdr(XDR *xdrs, smb_authrsp_t *objp)
{
	if (!xdr_pointer(xdrs, (char **)&objp->ar_token,
	    sizeof (smb_token_t), (xdrproc_t)smb_token_xdr))
		return (FALSE);

	if (!smb_buf16_xdr(xdrs, &objp->ar_secblob))
		return (FALSE);

	if (!xdr_uint32_t(xdrs, &objp->ar_status))
		return (FALSE);

	return (TRUE);
}

bool_t
smb_logoff_xdr(XDR *xdrs, smb_logoff_t *objp)
{
	if (!xdr_uint64_t(xdrs, &objp->lo_session_id))
		return (FALSE);
	if (!xdr_uint16_t(xdrs, &objp->lo_user_id))
		return (FALSE);

	return (TRUE);
}

bool_t
smb_sessionreq_xdr(XDR *xdrs, smb_sessionreq_t *objp)
{
	if (!xdr_uint64_t(xdrs, &objp->s_session_id))
		return (FALSE);
	if (!xdr_bool(xdrs, &objp->s_extsec))
		return (FALSE);

	return (TRUE);
}

bool_t
smb_sessionrsp_xdr(XDR *xdrs, smb_sessionrsp_t *objp)
{
	if (!smb_buf16_xdr(xdrs, &objp->s_secblob))
		return (FALSE);

	if (!xdr_uint32_t(xdrs, &objp->s_status))
		return (FALSE);

	return (TRUE);
}

static bool_t
smb_sid_xdr(XDR *xdrs, smb_sid_t *objp)
{
	if (!xdr_uint8_t(xdrs, &objp->sid_revision))
		return (FALSE);
	if (!xdr_uint8_t(xdrs, &objp->sid_subauthcnt))
		return (FALSE);
	if (!xdr_vector(xdrs, (char *)objp->sid_authority, NT_SID_AUTH_MAX,
	    sizeof (uint8_t), (xdrproc_t)xdr_uint8_t))
		return (FALSE);
	if (!xdr_vector(xdrs, (char *)objp->sid_subauth, objp->sid_subauthcnt,
	    sizeof (uint32_t), (xdrproc_t)xdr_uint32_t))
		return (FALSE);
	return (TRUE);
}

static bool_t
smb_luid_xdr(XDR *xdrs, smb_luid_t *objp)
{
	if (!xdr_uint32_t(xdrs, &objp->lo_part))
		return (FALSE);
	if (!xdr_uint32_t(xdrs, &objp->hi_part))
		return (FALSE);
	return (TRUE);
}

static bool_t
smb_luid_attrs_xdr(XDR *xdrs, smb_luid_attrs_t *objp)
{
	if (!smb_luid_xdr(xdrs, &objp->luid))
		return (FALSE);
	if (!xdr_uint32_t(xdrs, &objp->attrs))
		return (FALSE);
	return (TRUE);
}

static bool_t
smb_privset_xdr(XDR *xdrs, smb_privset_t *objp)
{
	if (!xdr_uint32_t(xdrs, &objp->priv_cnt))
		return (FALSE);
	if (!xdr_uint32_t(xdrs, &objp->control))
		return (FALSE);
	if (!xdr_vector(xdrs, (char *)objp->priv, objp->priv_cnt,
	    sizeof (smb_luid_attrs_t),
	    (xdrproc_t)smb_luid_attrs_xdr))
		return (FALSE);
	return (TRUE);
}

static bool_t
smb_token_xdr(XDR *xdrs, smb_token_t *objp)
{
	if (!smb_id_xdr(xdrs, &objp->tkn_user))
		return (FALSE);
	if (!smb_id_xdr(xdrs, &objp->tkn_owner))
		return (FALSE);
	if (!smb_id_xdr(xdrs, &objp->tkn_primary_grp))
		return (FALSE);
	if (!smb_ids_xdr(xdrs, &objp->tkn_win_grps))
		return (FALSE);
	if (!smb_privset_helper_xdr(xdrs, (char **)&objp->tkn_privileges))
		return (FALSE);
	if (!xdr_string(xdrs, &objp->tkn_account_name, ~0))
		return (FALSE);
	if (!xdr_string(xdrs, &objp->tkn_domain_name, ~0))
		return (FALSE);
	if (!xdr_string(xdrs, &objp->tkn_posix_name, ~0))
		return (FALSE);
	if (!xdr_uint32_t(xdrs, &objp->tkn_flags))
		return (FALSE);
	if (!smb_session_key_xdr(xdrs, &objp->tkn_session_key))
		return (FALSE);
	if (!smb_posix_grps_helper_xdr(xdrs, (char **)&objp->tkn_posix_grps))
		return (FALSE);
	return (TRUE);
}

/*
 * Basic sanity check on a token.
 */
boolean_t
smb_token_valid(smb_token_t *token)
{
	if (token == NULL)
		return (B_FALSE);

	if ((token->tkn_user.i_sid == NULL) ||
	    (token->tkn_owner.i_sid == NULL) ||
	    (token->tkn_primary_grp.i_sid == NULL) ||
	    (token->tkn_account_name == NULL) ||
	    (token->tkn_domain_name == NULL) ||
	    (token->tkn_posix_grps == NULL))
		return (B_FALSE);

	if ((token->tkn_win_grps.i_cnt != 0) &&
	    (token->tkn_win_grps.i_ids == NULL))
		return (B_FALSE);

	return (B_TRUE);
}

boolean_t
smb_session_key_valid(smb_session_key_t *ssnkey)
{
	if (ssnkey == NULL)
		return (B_FALSE);

	if ((ssnkey->val != NULL) && (ssnkey->len != 0))
		return (B_TRUE);

	return (B_FALSE);
}

bool_t
smb_domain_info_xdr(XDR *xdrs, smb_domain_info_t *objp)
{
	if (!xdr_uint32_t(xdrs, &objp->i_type))
		return (FALSE);
	if (!xdr_vector(xdrs, (char *)objp->i_nbname, NETBIOS_NAME_SZ,
	    sizeof (char), (xdrproc_t)xdr_char))
		return (FALSE);
	if (!xdr_vector(xdrs, (char *)objp->i_sid, SMB_SID_STRSZ,
	    sizeof (char), (xdrproc_t)xdr_char))
		return (FALSE);

	return (TRUE);
}

bool_t
smb_domains_info_xdr(XDR *xdrs, smb_domains_info_t *objp)
{
	if (!xdr_uint32_t(xdrs, &objp->d_status))
		return (FALSE);
	if (!xdr_vector(xdrs, (char *)objp->d_dc_name, MAXHOSTNAMELEN,
	    sizeof (char), (xdrproc_t)xdr_char))
		return (FALSE);
	if (!smb_list_xdr(xdrs, &objp->d_domain_list,
	    offsetof(smb_domain_info_t, i_lnd),
	    sizeof (smb_domain_info_t), (xdrproc_t)smb_domain_info_xdr))
		return (FALSE);

	return (TRUE);
}
