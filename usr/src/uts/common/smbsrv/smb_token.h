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

#ifndef _SMB_TOKEN_H
#define	_SMB_TOKEN_H

#ifdef _KERNEL
#include <sys/ksynch.h>
#else /* _KERNEL */
#include <synch.h>
#endif

#include <gssapi/gssapi.h>
#include <smbsrv/smb_avl.h>
#include <smbsrv/netrauth.h>
#include <smbsrv/smb_privilege.h>
#include <smb/smb_sid.h>
#include <smbsrv/smb_xdr.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Access Token
 *
 * An access token identifies a user, the user's privileges and the
 * list of groups of which the user is a member. This information is
 * used when access is requested to an object by comparing this
 * information with the DACL in the object's security descriptor.
 *
 * There should be one unique token per user per session per client.
 *
 * Access Token Flags
 *
 * SMB_ATF_GUEST	Token belongs to guest user
 * SMB_ATF_ANON		Token belongs to anonymous user
 * 			and it's only good for IPC Connection.
 * SMB_ATF_POWERUSER	Token belongs to a Power User member
 * SMB_ATF_BACKUPOP	Token belongs to a Power User member
 * SMB_ATF_ADMIN	Token belongs to a Domain Admins member
 */
#define	SMB_ATF_GUEST		0x00000001
#define	SMB_ATF_ANON		0x00000002
#define	SMB_ATF_POWERUSER	0x00000004
#define	SMB_ATF_BACKUPOP	0x00000008
#define	SMB_ATF_ADMIN		0x00000010

#define	SMB_POSIX_GRPS_SIZE(n) \
	(sizeof (smb_posix_grps_t) + (n - 1) * sizeof (gid_t))
/*
 * It consists of the primary and supplementary POSIX groups.
 */
typedef struct smb_posix_grps {
	uint32_t	pg_ngrps;
	gid_t		pg_grps[ANY_SIZE_ARRAY];
} smb_posix_grps_t;

typedef struct smb_token {
	smb_id_t		tkn_user;
	smb_id_t		tkn_owner;
	smb_id_t		tkn_primary_grp;
	smb_ids_t		tkn_win_grps;
	smb_privset_t		*tkn_privileges;
	char			*tkn_account_name;
	char			*tkn_domain_name;
	char			*tkn_posix_name;
	uint32_t		tkn_flags;
	smb_session_key_t	tkn_session_key;
	smb_posix_grps_t	*tkn_posix_grps;
} smb_token_t;

#ifndef _KERNEL
typedef struct smbd_session {
	uint32_t	s_magic;
	avl_node_t	s_node;
	mutex_t		s_mtx;
	int		s_refcnt;
	uint64_t	s_id;
	gss_ctx_id_t	s_gss_ctx;
	smb_avl_t	*s_user_avl; /* user AVL tree */
} smbd_session_t;

typedef struct smbd_user {
	uint32_t	u_magic;
	avl_node_t	u_node;
	mutex_t		u_mtx;
	int		u_refcnt;
	uint16_t	u_id;
	gss_ctx_id_t	u_gss_ctx;
	uint32_t	u_audit_sid;
	uint16_t	u_phase_cnt;
} smbd_user_t;
#endif /* _KERNEL */

#ifdef _KERNEL
#define	SMB_AUTH_SESSION_T	void
#define	SMB_AUTH_USER_T		void
#else /* _KERNEL */
#define	SMB_AUTH_SESSION_T	smbd_session_t
#define	SMB_AUTH_USER_T		smbd_user_t
#endif /* _KERNEL */

/*
 * smb_authreq_t au_flags values
 * SMB_AUTH_INIT - identifies a new authentication request; as against
 * the continuation of an in-progress multi-phase authentication.
 */
#define	SMB_AUTH_LM_PASSWD	0x01
#define	SMB_AUTH_NTLM_PASSWD	0x02
#define	SMB_AUTH_SECBLOB	0x04
#define	SMB_AUTH_ANON		0x10
#define	SMB_AUTH_INIT		0x20

/*
 * Details required to authenticate a user.
 */
typedef struct smb_authreq {
	uint64_t		au_session_id;
	uint16_t		au_user_id;
	uint16_t		au_level;
	char			*au_username;	/* requested username */
	char			*au_domain;	/* requested domain */
	char			*au_eusername;	/* effective username */
	char			*au_edomain;	/* effective domain */
	char			*au_workstation;
	smb_inaddr_t		au_clnt_ipaddr;
	smb_inaddr_t		au_local_ipaddr;
	uint16_t		au_local_port;
	smb_buf32_t		au_challenge_key;
	smb_buf16_t		au_ntpasswd;
	smb_buf16_t		au_lmpasswd;
	smb_buf16_t		au_secblob;
	int			au_native_os;
	int			au_native_lm;
	uint32_t		au_flags;

	/* included primarily for dtrace */
	uint16_t		au_vcnumber;
	uint16_t		au_maxmpxcount;
	uint16_t		au_maxbufsize;
	uint16_t		au_guest;
	uint32_t		au_capabilities;
	uint32_t		au_sesskey;

	/* used only in user space */
	uint32_t		au_logon_id;
	uint32_t		au_domain_type;
	uint32_t		au_secmode;
	uint32_t		au_ntlm_msgtype;
	SMB_AUTH_SESSION_T	*au_session;
	SMB_AUTH_USER_T		*au_user;
	void			*au_krb5ctx;
	void			*au_pac;
} smb_authreq_t;

typedef struct smb_authrsp {
	smb_token_t	*ar_token;
	smb_buf16_t	ar_secblob;
	uint32_t	ar_status;
} smb_authrsp_t;

typedef struct smb_logoff {
	uint64_t	lo_session_id;
	uint16_t	lo_user_id;
} smb_logoff_t;

typedef struct smb_sessionreq {
	uint64_t	s_session_id;
	bool_t		s_extsec;
} smb_sessionreq_t;

typedef struct smb_sessionrsp {
	smb_buf16_t	s_secblob;
	uint32_t	s_status;
} smb_sessionrsp_t;


bool_t smb_authreq_xdr();
bool_t smb_authrsp_xdr();
bool_t smb_logoff_xdr();
bool_t smb_sessionreq_xdr();
bool_t smb_sessionrsp_xdr();
boolean_t smb_token_valid(smb_token_t *);

#ifdef __cplusplus
}
#endif


#endif /* _SMB_TOKEN_H */
