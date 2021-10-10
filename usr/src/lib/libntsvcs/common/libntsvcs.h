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

#ifndef	_LIBNTSVCS_H
#define	_LIBNTSVCS_H

#include <uuid/uuid.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/list.h>
#include <sys/uio.h>
#include <sys/ksynch.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <netdb.h>
#include <libuutil.h>
#include <smb/wintypes.h>
#include <smb/ntifs.h>
#include <smbsrv/smb_token.h>
#include <smbsrv/smb_privilege.h>
#include <smbsrv/smb_share.h>
#include <smbsrv/smb_xdr.h>
#include <smbsrv/smb_dfs.h>
#include <smbsrv/libsmb.h>
#include <smbsrv/libndr.h>
#include <smbsrv/ndl/lsarpc.ndl>

#ifdef	__cplusplus
extern "C" {
#endif

uint32_t lsa_lookup_name(char *, uint16_t, smb_account_t *);
uint32_t lsa_lookup_sid(smb_sid_t *, smb_account_t *);
uint32_t lsa_query_primary_domain_info(char *, char *, smb_domain_t *);
uint32_t lsa_query_account_domain_info(char *, char *, smb_domain_t *);
uint32_t lsa_query_dns_domain_info(char *, char *, smb_domain_t *);
uint32_t lsa_enum_trusted_domains(char *, char *, smb_trusted_domains_t *);

extern int dssetup_check_service(void);
extern void dssetup_clear_domain_info(void);
uint32_t dssetup_query_domain_info(char *, char *, smb_domain_t *);
void ntsvcs_init(void);
void ntsvcs_fini(void);
DWORD netlogon_setup_auth(char *, char *);
boolean_t smb_authinfo_setup_krb(const kerb_validation_info_t *,
    smb_session_key_t *, smb_authreq_t *, smb_authinfo_t *);
boolean_t smb_authinfo_setup_common(const netr_validation_info3_t *,
    smb_session_key_t *, smb_authreq_t *, smb_authinfo_t *);
boolean_t smb_authinfo_validate(smb_authinfo_t *);
void smb_authinfo_free(smb_authinfo_t *);
boolean_t netr_isadmin(const smb_authinfo_t *);
void netr_logon_abort(void);
uint32_t netr_logon(smb_authreq_t *, smb_authinfo_t *);
ndr_buf_t *netr_ndrbuf_init(void);
void netr_ndrbuf_fini(ndr_buf_t *);
DWORD sam_create_trust_account(char *, char *);
uint32_t smb_session_key_create(smb_session_key_t *, uint8_t *, uint32_t);
void smb_session_key_destroy(smb_session_key_t *);

/*
 * The maximum number of domains (NT limit).
 */
#define	MLSVC_DOMAIN_MAX		32

/*
 * Passthrough negotiation and authentication interface.
 *
 * NT supports two forms of password: a Lanman (case-insensitive)
 * password and an NT (case-sensitive) password. If either of the
 * passwords is not available its pointer and length should be set
 * to zero. The session key and vc number are required to validate
 * the encrypted passwords.
 */

void mlsvc_nt_password_hash(char *result, char *password);
int mlsvc_encrypt_nt_password(char *password, char *key, int keylen, char *out,
    int outmax);

#define	SMB_AUTOHOME_FILE	"smbautohome"
#define	SMB_AUTOHOME_PATH	"/etc"

typedef struct smb_autohome {
	struct smb_autohome *ah_next;
	uint32_t ah_hits;
	time_t ah_timestamp;
	char *ah_name;		/* User account name */
	char *ah_path;		/* Home directory path */
	char *ah_container;	/* ADS container distinguished name */
} smb_autohome_t;

extern void smb_autohome_add(const smb_token_t *);

/*
 * A local unique id (LUID) is an opaque id used by servers to identify
 * local resources, such as privileges.  A client will use lookup
 * functions to translate the LUID to a more general, machine independent
 * form; such as a string.
 */
typedef struct ms_luid {
	uint32_t low_part;
	uint32_t high_part;
} ms_luid_t;

typedef struct wksta_info {
	uint32_t	wki_platform_id;
	char		*wki_computername;
	char		*wki_domainname;
	uint32_t	wki_ver_major;
	uint32_t	wki_ver_minor;
} wksta_info_t;

int wkssvc_getinfo(char *, char *, wksta_info_t *);
void wkssvc_freeinfo(wksta_info_t *);

typedef struct srvsvc_share_info {
	char		*si_netname;
	uint32_t	si_type;
	char		*si_comment;
	uint32_t	si_permissions;
	uint32_t	si_max_uses;
	uint32_t	si_current_uses;
	char		*si_path;
	char		*si_servername;
} srvsvc_share_info_t;

typedef struct srvsvc_session_info {
	char		*ui_account;
	char		*ui_workstation;
	int64_t		ui_logon_time;
	uint32_t	ui_numopens;
	uint32_t	ui_flags;
} srvsvc_session_info_t;

typedef struct srvsvc_connect_info {
	uint32_t	ci_id;
	uint32_t	ci_type;
	uint32_t	ci_numopens;
	uint32_t	ci_numusers;
	uint32_t	ci_time;
	char		*ci_username;
	char		*ci_share;
} srvsvc_connect_info_t;

typedef struct srvsvc_file_info {
	uint16_t	fi_fid;
	uint32_t	fi_permissions;
	uint32_t	fi_numlocks;
	char		*fi_path;
	char		*fi_username;
} srvsvc_file_info_t;

typedef struct srvsvc_info {
	list_node_t		l_lnd;
	uint32_t		l_type;
	union {
		srvsvc_session_info_t	ul_session;
		srvsvc_connect_info_t	ul_connection;
		srvsvc_file_info_t	ul_file;
		srvsvc_share_info_t	ul_shr;
	} l_list;
} srvsvc_info_t;

typedef struct srvsvc_list {
	list_t		sl_list;
	uint32_t	sl_count;
	uint32_t	sl_level;
	uint32_t	sl_totalentries;
	uint32_t	sl_entriesread;
} srvsvc_list_t;

void srvsvc_net_enum_init(srvsvc_list_t *);
void srvsvc_net_enum_fini(srvsvc_list_t *);

int srvsvc_net_share_get_info(char *, char *, char *, uint32_t,
    srvsvc_share_info_t *);
int srvsvc_net_share_enum(char *, char *, uint32_t, srvsvc_list_t *);
int srvsvc_net_session_enum(char *, char *, char *, char *, uint32_t,
    srvsvc_list_t *);
int srvsvc_net_connect_enum(char *, char *, char *, uint32_t, srvsvc_list_t *);
int srvsvc_net_files_enum(char *, char *, char *, char *, uint32_t,
    srvsvc_list_t *);

/*
 * Information about a server as reported by NetServerGetInfo.
 * The SV_PLATFORM and SV_TYPE definitions are in srvsvc.ndl.
 */
typedef struct srvsvc_server_info {
	uint32_t	sv_platform_id;
	char		*sv_name;
	uint32_t	sv_version_major;
	uint32_t	sv_version_minor;
	uint32_t	sv_type;
	char		*sv_comment;
	uint32_t	sv_os;
} srvsvc_server_info_t;

int srvsvc_net_server_getinfo(char *, char *, srvsvc_server_info_t *);
int srvsvc_net_remote_tod(char *, char *, struct timeval *, struct tm *);
void srvsvc_timecheck(char *, char *);
uint32_t srvsvc_sd_set_relative(smb_sd_t *, uint8_t *);

/*
 * A client_t is created while binding a client connection to hold the
 * context for calls made using that connection.
 *
 * Handles are RPC call specific and we use an inheritance mechanism to
 * ensure that each handle has a pointer to the client_t.  When the top
 * level (bind) handle is released, we close the connection.
 */
typedef struct mlsvc_handle {
	ndr_hdid_t			handle;
	ndr_client_t			*clnt;
	srvsvc_server_info_t		svinfo;
} mlsvc_handle_t;

/*
 * Minimum buffer size for a NetBIOS name RPC binding handle.
 * The handle is the NetBIOS name prefixed (UNC style) by '\\'.
 */
#define	NDR_BIND_NBNAME_SZ		(NETBIOS_NAME_SZ + 2)

int ndr_rpc_bind(mlsvc_handle_t *, char *, char *, char *, const char *);
void ndr_rpc_unbind(mlsvc_handle_t *);
int ndr_rpc_call(mlsvc_handle_t *, int, void *);
void ndr_rpc_set_nonull(mlsvc_handle_t *);
const srvsvc_server_info_t *ndr_rpc_server_info(mlsvc_handle_t *);
uint32_t ndr_rpc_server_os(mlsvc_handle_t *);
int ndr_rpc_get_ssnkey(mlsvc_handle_t *, unsigned char *, size_t);
void *ndr_rpc_malloc(mlsvc_handle_t *, size_t);
void *ndr_rpc_derive_nbhandle(mlsvc_handle_t *, const char *);
void ndr_rpc_format_nbhandle(const char *, char *, size_t);
ndr_heap_t *ndr_rpc_get_heap(mlsvc_handle_t *);
void ndr_rpc_release(mlsvc_handle_t *);
boolean_t ndr_is_null_handle(mlsvc_handle_t *);
boolean_t ndr_is_bind_handle(mlsvc_handle_t *);
void ndr_inherit_handle(mlsvc_handle_t *, mlsvc_handle_t *);
void ndr_rpc_status(mlsvc_handle_t *, int, uint32_t);

/* SVCCTL service */
/*
 * Calculate the wide-char equivalent string length required to
 * store a string - including the terminating null wide-char.
 */
#define	SVCCTL_WNSTRLEN(S)	((strlen((S)) + 1) * sizeof (smb_wchar_t))

/* An AVL-storable node representing each service in the SCM database. */
typedef struct svcctl_svc_node {
	uu_avl_node_t		sn_node;
	char			*sn_name;	/* Service Name (Key) */
	char			*sn_fmri;	/* Display Name (FMRI) */
	char			*sn_desc;	/* Description */
	char			*sn_state;	/* State */
} svcctl_svc_node_t;

/* This structure provides context for each svcctl_s_OpenManager call. */
typedef struct svcctl_manager_context {
	scf_handle_t		*mc_scf_hdl;	  /* SCF handle */
	scf_propertygroup_t	*mc_scf_gpg;	  /* Property group */
	scf_property_t		*mc_scf_gprop;	  /* Property */
	scf_value_t		*mc_scf_gval;	  /* Value */
	uint32_t		mc_scf_numsvcs;   /* Number of SMF services */
	ssize_t			mc_scf_max_fmri_len;  /* Max FMRI length */
	ssize_t			mc_scf_max_value_len; /* Max Value length */
	uint32_t		mc_bytes_needed;  /* Number of bytes needed */
	uu_avl_pool_t		*mc_svcs_pool;	  /* AVL pool */
	uu_avl_t		*mc_svcs;	  /* AVL tree of SMF services */
} svcctl_manager_context_t;

/* This structure provides context for each svcctl_s_OpenService call. */
typedef struct svcctl_service_context {
	ndr_hdid_t		*sc_mgrid;	/* Manager ID */
	char			*sc_svcname;    /* Service Name */
} svcctl_service_context_t;

typedef enum {
	SVCCTL_MANAGER_CONTEXT = 0,
	SVCCTL_SERVICE_CONTEXT
} svcctl_context_type_t;

/* This structure provides abstraction for service and manager context call. */
typedef struct svcctl_context {
	svcctl_context_type_t	c_type;
	union {
		svcctl_manager_context_t *uc_mgr;
		svcctl_service_context_t *uc_svc;
		void *uc_cp;
	} c_ctx;
} svcctl_context_t;

/* Service Control Manager (SCM) functions */
void svcctl_init(void);
void svcctl_fini(void);
int svcctl_scm_init(svcctl_manager_context_t *);
void svcctl_scm_fini(svcctl_manager_context_t *);
int svcctl_scm_scf_handle_init(svcctl_manager_context_t *);
void svcctl_scm_scf_handle_fini(svcctl_manager_context_t *);
int svcctl_scm_refresh(svcctl_manager_context_t *);
uint32_t svcctl_scm_enum_services(svcctl_manager_context_t *, uint8_t *,
    size_t, uint32_t *, boolean_t);
uint32_t svcctl_scm_validate_service(svcctl_manager_context_t *, char *);
svcctl_svc_node_t *svcctl_scm_find_service(svcctl_manager_context_t *, char *);
uint32_t svcctl_scm_map_status(const char *);

/* LOGR service */
#define	LOGR_APPLICATION_LOG		"Application"
#define	LOGR_SECURITY_LOG		"Security"
#define	LOGR_SYSTEM_LOG			"System"
#define	LOGR_NMSGMASK			1023
#define	LOGR_MAXMSGLEN			800

typedef struct logr_entry {
	struct timeval	le_timestamp;			/* Time of log entry */
	int		le_pri;				/* Message priority */
	char		le_hostname[MAXHOSTNAMELEN];	/* Log hostname */
	char		le_msg[LOGR_MAXMSGLEN];		/* Log message text */
} logr_entry_t;

typedef struct logr_info {
	logr_entry_t	li_entry[LOGR_NMSGMASK+1];	/* Array of log entry */
	int		li_idx;				/* Index */
} logr_info_t;

typedef struct logr_read_data {
	int		rd_tot_recnum;		/* Total no. of record read */
	int		rd_last_sentrec;	/* Last sentence read */
	char		rd_first_read;		/* First sentence read */
	logr_info_t	*rd_log;		/* Log information read */
} logr_read_data_t;

/* This structure provides the context for eventlog calls from clients. */
typedef struct logr_context {
	logr_read_data_t *lc_cached_read_data;
	char *lc_source_name;
} logr_context_t;

void logr_init(void);
void logr_fini(void);
boolean_t logr_is_supported(char *);
int logr_get_snapshot(logr_context_t *);

uint32_t dfs_get_referrals(const char *, dfs_reftype_t, dfs_info_t *);
void dfs_info_free(dfs_info_t *);
void *dfs_ns_export(void *);
void dfs_ns_unexport(const char *);

#ifdef	__cplusplus
}
#endif

#endif	/* _LIBNTSVCS_H */
