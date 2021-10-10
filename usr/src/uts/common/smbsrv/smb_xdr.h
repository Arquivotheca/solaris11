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

#ifndef	_SMBSRV_SMB_XDR_H
#define	_SMBSRV_SMB_XDR_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <rpc/xdr.h>
#include <sys/param.h>
#include <sys/avl.h>
#ifndef _KERNEL
#include <netdb.h>		/* for MAXHOSTNAMELEN */
#endif
#include <smb/wintypes.h>
#include <smb/smb_sid.h>
#include <smbsrv/smbinfo.h>
#include <smbsrv/smb_ioctl.h>
#include <smbsrv/smb_share.h>
#include <smbsrv/smb_dfs.h>

#ifdef _KERNEL
#define	xdr_int8_t	xdr_char
#define	xdr_uint8_t	xdr_u_char
#define	xdr_int16_t	xdr_short
#define	xdr_uint16_t	xdr_u_short

/* value should be the same as in netdb.h */
#define	MAXHOSTNAMELEN	256
#endif /* _KERNEL */

/* null-terminated string */
typedef struct smb_string {
	char *buf;
} smb_string_t;

/* 16-bit opaque buffer (non-null terminated strings) */
typedef struct smb_buf16 {
	uint16_t	len;
	uint8_t		*val;
} smb_buf16_t;

/* 32-bit opaque buffer (non-null terminated strings) */
typedef struct smb_buf32 {
	uint32_t	len;
	uint8_t		*val;
} smb_buf32_t;

#define	SMB_OPIPE_HDR_MAGIC	0x4F484452	/* OHDR */
#define	SMB_OPIPE_DOOR_BUFSIZE	(30 * 1024)

/*
 * Door operations for opipes.
 */
typedef enum {
	SMB_OPIPE_NULL = 0,
	SMB_OPIPE_LOOKUP,
	SMB_OPIPE_OPEN,
	SMB_OPIPE_CLOSE,
	SMB_OPIPE_READ,
	SMB_OPIPE_WRITE,
	SMB_OPIPE_EXEC
} smb_opipe_op_t;

#define	SMB_DOOR_HDR_MAGIC	0x444F4F52	/* DOOR */

/*
 * Door header flags.
 */
#define	SMB_DF_ASYNC		0x00000001	/* Asynchronous call */
#define	SMB_DF_SYSSPACE		0x00000002	/* Called from the kernel */
#define	SMB_DF_USERSPACE	0x00000004	/* Called from user space */

/*
 * Header for door calls.  The op codes and return codes are defined
 * in smb_door.h.  The header is here to make it available to XDR.
 *
 * fid		For opipe: the pipe identifier.
 * op		The door operation being invoked.
 * txid		Unique transaction id for the current door call.
 * datalen	Bytes of data following the header (excludes the header).
 * resid	For opipe: the number of bytes remaining in the server.
 * door_rc	Return code provided by the door server.
 */
typedef struct smb_doorhdr {
	uint32_t dh_magic;
	uint32_t dh_flags;
	uint32_t dh_fid;
	uint32_t dh_op;
	uint32_t dh_txid;
	uint32_t dh_datalen;
	uint32_t dh_resid;
	uint32_t dh_door_rc;
} smb_doorhdr_t;

typedef struct smb_netuserinfo {
	uint64_t	ui_session_id;
	uint16_t	ui_smb_uid;
	char		*ui_domain;
	char		*ui_account;
	char		*ui_posix_name;
	char		*ui_workstation;
	smb_inaddr_t	ui_ipaddr;
	int32_t		ui_native_os;
	int64_t		ui_logon_time;
	uint32_t	ui_numopens;
	uint32_t	ui_flags;
} smb_netuserinfo_t;

typedef struct smb_opennum {
	uint32_t	open_users;
	uint32_t	open_trees;
	uint32_t	open_files;
	uint32_t	qualtype;
	char		qualifier[MAXNAMELEN];
} smb_opennum_t;

typedef struct smb_netconnectinfo {
	uint32_t	ci_id;
	uint32_t	ci_type;
	uint32_t	ci_numopens;
	uint32_t	ci_numusers;
	uint32_t	ci_time;
	uint32_t	ci_namelen;
	uint32_t	ci_sharelen;
	char		*ci_username;
	char		*ci_share;
} smb_netconnectinfo_t;

typedef struct smb_netfileinfo {
	uint16_t	fi_fid;
	uint32_t	fi_uniqid;
	uint32_t	fi_permissions;
	uint32_t	fi_numlocks;
	uint32_t	fi_pathlen;
	uint32_t	fi_namelen;
	char		*fi_path;
	char		*fi_username;
} smb_netfileinfo_t;

typedef struct smb_netsvcitem {
	list_node_t	nsi_lnd;
	union {
		smb_netuserinfo_t	nsi_user;
		smb_netconnectinfo_t	nsi_tree;
		smb_netfileinfo_t	nsi_ofile;
		smb_share_t		nsi_share;
	} nsi_un;
} smb_netsvcitem_t;

typedef struct smb_netsvc {
	list_t			ns_list;
	smb_netsvcitem_t	*ns_items;
	smb_ioc_svcenum_t	*ns_ioc;
	uint32_t		ns_ioclen;
} smb_netsvc_t;


bool_t smb_buf16_xdr(XDR *, smb_buf16_t *);
bool_t smb_buf32_xdr(XDR *, smb_buf32_t *);
bool_t smb_string_xdr(XDR *, smb_string_t *);
bool_t smb_inaddr_xdr(XDR *, smb_inaddr_t *);

#define	smb_session_key_t	smb_buf16_t
#define	smb_session_key_xdr	smb_buf16_xdr
boolean_t smb_session_key_valid(smb_session_key_t *);

const char *smb_doorhdr_opname(uint32_t);
int smb_doorhdr_encode(smb_doorhdr_t *, uint8_t *, uint32_t);
int smb_doorhdr_decode(smb_doorhdr_t *, uint8_t *, uint32_t);
bool_t smb_doorhdr_xdr(XDR *xdrs, smb_doorhdr_t *objp);
int smb_netuserinfo_encode(smb_netuserinfo_t *, uint8_t *, uint32_t, uint_t *);
int smb_netuserinfo_decode(smb_netuserinfo_t *, uint8_t *, uint32_t, uint_t *);
bool_t smb_netuserinfo_xdr(XDR *, smb_netuserinfo_t *);
int smb_netconnectinfo_encode(smb_netconnectinfo_t *, uint8_t *, uint32_t,
    uint_t *);
int smb_netconnectinfo_decode(smb_netconnectinfo_t *, uint8_t *, uint32_t,
    uint_t *);
bool_t smb_netconnectinfo_xdr(XDR *, smb_netconnectinfo_t *);
int smb_netfileinfo_encode(smb_netfileinfo_t *, uint8_t *, uint32_t, uint_t *);
int smb_netfileinfo_decode(smb_netfileinfo_t *, uint8_t *, uint32_t, uint_t *);
bool_t smb_netfileinfo_xdr(XDR *, smb_netfileinfo_t *);

typedef uint16_t sid_type_t;

typedef struct lsa_account {
	ntstatus_t	a_status;
	sid_type_t	a_sidtype;
	char		a_domain[MAXNAMELEN];
	char		a_name[MAXNAMELEN];
	char		a_sid[SMB_SID_STRSZ];
} lsa_account_t;

int lsa_account_encode(lsa_account_t *, uint8_t *, uint32_t);
int lsa_account_decode(lsa_account_t *, uint8_t *, uint32_t);
bool_t lsa_account_xdr(XDR *, lsa_account_t *);

/*
 * VSS Door Structures
 */
#define	SMB_VSS_GMT_SIZE sizeof ("@GMT-yyyy.mm.dd-hh.mm.ss")

typedef struct smb_gmttoken_query {
	uint32_t	gtq_count;
	char		*gtq_path;
} smb_gmttoken_query_t;

typedef char *smb_gmttoken_t;

typedef struct smb_gmttoken_response {
	uint32_t gtr_count;
	struct {
		uint_t		gtr_gmttokens_len;
		smb_gmttoken_t	*gtr_gmttokens_val;
	} gtr_gmttokens;
} smb_gmttoken_response_t;

typedef struct smb_gmttoken_snapname {
	char	*gts_path;
	char	*gts_gmttoken;
} smb_gmttoken_snapname_t;

bool_t smb_gmttoken_query_xdr(XDR *, smb_gmttoken_query_t *);
bool_t smb_gmttoken_response_xdr(XDR *, smb_gmttoken_response_t *);
bool_t smb_gmttoken_snapname_xdr(XDR *, smb_gmttoken_snapname_t *);

/*
 * User and Group Quotas
 *
 * SMB User and Group quota values of SMB_QUOTA_UNLIMITED mean
 * No Limit. This maps to 0 (none) on ZFS.
 */
#define	SMB_QUOTA_UNLIMITED		0xFFFFFFFFFFFFFFFF

typedef struct smb_quota {
	list_node_t q_list_node;
	char q_sidstr[SMB_SID_STRSZ];
	uint32_t q_sidtype;
	uint64_t q_used;
	uint64_t q_thresh;
	uint64_t q_limit;
	avl_node_t q_avl_node;
} smb_quota_t;


typedef enum {
	SMB_QUOTA_SID,
	SMB_QUOTA_UID,
	SMB_QUOTA_GID
} smb_quota_idtype_t;

typedef struct smb_quota_sid {
	list_node_t qs_list_node;
	uint32_t qs_idtype;	/* smb_quota_idtype_t */
	uint32_t qs_id;
	char qs_sidstr[SMB_SID_STRSZ];
} smb_quota_sid_t;

typedef enum {
	SMB_QUOTA_QUERY_INVALID_OP,
	SMB_QUOTA_QUERY_SIDLIST,
	SMB_QUOTA_QUERY_STARTSID,
	SMB_QUOTA_QUERY_ALL,
	SMB_QUOTA_QUERY_USER
} smb_quota_query_op_t;

typedef struct smb_quota_query {
	char *qq_root_path;
	uint32_t qq_query_op;	/* smb_quota_query_op_t */
	bool_t qq_single;
	bool_t qq_restart;
	uint32_t qq_max_quota;
	list_t qq_sid_list;	/* list of smb_quota_sid_t */
} smb_quota_query_t;

typedef struct smb_quota_response {
	uint32_t qr_status;
	list_t qr_quota_list;	/* list of smb_quota_t */
} smb_quota_response_t;

typedef struct smb_quota_set {
	char *qs_root_path;
	list_t qs_quota_list;	/* list of smb_quota_t */
} smb_quota_set_t;

bool_t smb_quota_query_xdr(XDR *, smb_quota_query_t *);
bool_t smb_quota_response_xdr(XDR *, smb_quota_response_t *);
bool_t smb_quota_set_xdr(XDR *, smb_quota_set_t *);

typedef struct dfs_referral_query {
	dfs_reftype_t	rq_type;
	char 		*rq_path;
} dfs_referral_query_t;

typedef struct dfs_referral_response {
	dfs_info_t	rp_referrals;
	uint32_t	rp_status;
} dfs_referral_response_t;

bool_t dfs_referral_query_xdr(XDR *, dfs_referral_query_t *);
bool_t dfs_referral_response_xdr(XDR *, dfs_referral_response_t *);

typedef struct smb_shr_hostaccess_query {
	char		*shq_none;
	char		*shq_ro;
	char		*shq_rw;
	uint32_t	shq_flag;
	smb_inaddr_t	shq_ipaddr;
} smb_shr_hostaccess_query_t;

bool_t smb_shr_hostaccess_query_xdr(XDR *, smb_shr_hostaccess_query_t *);
bool_t smb_shr_execinfo_xdr(XDR *, smb_shr_execinfo_t *);
bool_t smb_shr_notify_xdr(XDR *, smb_shr_notify_t *);
bool_t smb_share_xdr(XDR *, smb_share_t *);
int smb_share_encode(smb_share_t *, uint8_t *, uint32_t, uint_t *);
int smb_share_decode(smb_share_t *, uint8_t *, uint32_t, uint_t *);

typedef struct smb_spooldoc {
	smb_inaddr_t	sd_ipaddr;
	char		sd_username[MAXNAMELEN];
	char		sd_docname[MAXNAMELEN];
	char		sd_path[MAXPATHLEN];
	char		sd_printer[MAXNAMELEN];
} smb_spooldoc_t;

bool_t smb_spooldoc_xdr(XDR *, smb_spooldoc_t *);

typedef struct smb_domain_info {
	list_node_t	i_lnd;
	uint32_t	i_type;
	char		i_nbname[NETBIOS_NAME_SZ];
	char		i_sid[SMB_SID_STRSZ];
} smb_domain_info_t;

typedef struct smb_domains_info {
	uint32_t	d_status;
	char		d_dc_name[MAXHOSTNAMELEN];
	list_t		d_domain_list;	/* list of smb_domain_info_t */
} smb_domains_info_t;

bool_t smb_domains_info_xdr(XDR *, smb_domains_info_t *);

#ifdef	__cplusplus
}
#endif

#endif	/* _SMBSRV_SMB_XDR_H */
