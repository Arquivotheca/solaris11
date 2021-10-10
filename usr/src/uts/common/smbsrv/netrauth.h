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

#ifndef _SMBSRV_NETRAUTH_H
#define	_SMBSRV_NETRAUTH_H

/*
 * NETR remote authentication and logon services.
 */

#include <sys/types.h>
#include <smb/wintypes.h>
#include <smbsrv/netbios.h>
#include <smbsrv/smbinfo.h>
#include <smbsrv/smb_xdr.h>
#include <smbsrv/ndl/netlogon.ndl>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * See also netlogon.ndl.
 */
#define	NETR_WKSTA_TRUST_ACCOUNT_TYPE		0x02
#define	NETR_DOMAIN_TRUST_ACCOUNT_TYPE		0x04

/*
 * ParameterControl flags for NETLOGON validation processing. For more info,
 * see NETLOGON_LOGON_IDENTITY_INFO section of MS-NRPC document.
 *
 * If NETR_LOGON_FLG_SERVER_TRUST_ACCT flag is set, it allows smb/server to log
 * on with the SAM account of a domain controller.
 *
 * If NETR_LOGON_FLG_WKST_TRUST_ACCT flag is set, it allows smb/server to log
 * on with the SAM account of a domain client.
 */
#define	NETR_LOGON_FLG_SERVER_TRUST_ACCT	0x00000020
#define	NETR_LOGON_FLG_WKST_TRUST_ACCT		0x00000800

/*
 * Negotiation flags for challenge/response authentication.
 */
#define	NETR_NEGOTIATE_BASE_FLAGS		0x000001FF
#define	NETR_NEGOTIATE_STRONGKEY_FLAG		0x00004000

#define	NETR_SESSKEY64_SZ			8
#define	NETR_SESSKEY128_SZ			16
#define	NETR_SESSKEY_MAXSZ			NETR_SESSKEY128_SZ
#define	NETR_CRED_DATA_SZ			8
#define	NETR_OWF_PASSWORD_SZ			16

/*
 * SAM logon levels: interactive and network.
 */
#define	NETR_INTERACTIVE_LOGON			0x01
#define	NETR_NETWORK_LOGON			0x02

/*
 * SAM logon validation levels.
 */
#define	NETR_VALIDATION_LEVEL3			0x03

/*
 * This is a duplicate of the netr_credential
 * from netlogon.ndl.
 */
typedef struct netr_cred {
	BYTE data[NETR_CRED_DATA_SZ];
} netr_cred_t;

typedef struct netr_session_key {
	BYTE key[NETR_SESSKEY_MAXSZ];
	short len;
} netr_session_key_t;

#define	NETR_FLG_NULL		0x00000001
#define	NETR_FLG_VALID		0x00000001
#define	NETR_FLG_INIT		0x00000002

/*
 * 120-byte machine account password (null-terminated)
 */
#define	NETR_MACHINE_ACCT_PASSWD_MAX	120 + 1

typedef struct netr_info {
	DWORD flags;
	char server[NETBIOS_NAME_SZ * 2];
	char hostname[NETBIOS_NAME_SZ * 2];
	netr_cred_t client_challenge;
	netr_cred_t server_challenge;
	netr_cred_t client_credential;
	netr_cred_t server_credential;
	netr_session_key_t session_key;
	BYTE password[NETR_MACHINE_ACCT_PASSWD_MAX];
	time_t timestamp;
} netr_info_t;

/*
 * smb_authinfo_t contains the fields from [netr|kerb]_validation_info
 * required by consumers to build an access token. Note the a_usersessionkey is
 * the user sesssion key in clear form, not the obfuscated one found in
 * netr_validation_info3.
 *
 * From the Kerberos point of view, the SMB server is a resource server.
 * The domain of which the SMB server is a member is the resource domain.
 * The groups defined in the resource domain are considered to be resource
 * groups. The resource group related fields are not used by Windows KDC
 * but they are reserved for use by other implementations of Kerberos that
 * support PAC.
 */
typedef struct smb_authinfo {
	char *a_usrname;				/* EffectiveName */
	DWORD a_usrrid;					/* UserId */
	DWORD a_grprid;					/* PrimaryGroupId */
	DWORD a_grpcnt;					/* GroupCount */
	struct netr_group_membership *a_grps; 		/* GroupIds */
	smb_session_key_t a_usersesskey;		/* UserSessionKey */
	char *a_domainname;				/* LogonDomainName */
	struct netr_sid *a_domainsid;			/* LogonDomainId */
	DWORD a_sidcnt;					/* SidCount */
	struct netr_sid_and_attributes *a_extra_sids;	/* ExtraSids */
	/* Kerberos only - resource groups */
	struct netr_sid *a_resgrp_domainsid;		/* ResGrpDomainSid */
	DWORD a_resgrpcnt;				/* ResGrpCount */
	struct netr_group_membership *a_resgrps;	/* ResGrpIds */
} smb_authinfo_t;

#define	NETR_A2H(c) (isdigit(c)) ? ((c) - '0') : ((c) - 'A' + 10)

#ifdef __cplusplus
}
#endif

#endif /* _SMBSRV_NETRAUTH_H */
