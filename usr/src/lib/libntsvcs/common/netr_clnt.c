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
 * NETR SamLogon and SamLogoff RPC client functions.
 * NETR challenge/response client functions.
 *
 * NT_STATUS_INVALID_PARAMETER
 * NT_STATUS_NO_TRUST_SAM_ACCOUNT
 * NT_STATUS_ACCESS_DENIED
 */

#include <stdio.h>
#include <strings.h>
#include <stdlib.h>
#include <time.h>
#include <alloca.h>
#include <unistd.h>
#include <netdb.h>
#include <thread.h>
#include <ctype.h>
#include <security/cryptoki.h>
#include <security/pkcs11.h>
#include <smbsrv/libsmb.h>
#include <smbsrv/libntsvcs.h>
#include <smbsrv/netrauth.h>
#include <smbsrv/smbinfo.h>
#include <ntsvcs.h>

#define	NETLOGON_ATTEMPTS		2
#define	NETR_SESSKEY_ZEROBUF_SZ		4
/* The DES algorithm uses a 56-bit encryption key. */
#define	NETR_DESKEY_LEN			7

netr_info_t netr_global_info;

static mutex_t netlogon_mutex;
static cond_t netlogon_cv;
static boolean_t netlogon_busy = B_FALSE;
static boolean_t netlogon_abort = B_FALSE;

static int netr_open(char *, char *, mlsvc_handle_t *);
static int netr_close(mlsvc_handle_t *);
static DWORD netlogon_auth(char *, mlsvc_handle_t *, DWORD);
static int netr_server_req_challenge(mlsvc_handle_t *, netr_info_t *);
static int netr_server_authenticate2(mlsvc_handle_t *, netr_info_t *);
static int netr_gen_skey128(netr_info_t *);
static int netr_gen_skey64(netr_info_t *);
static int netr_gen_credentials(BYTE *, netr_cred_t *, DWORD,
    netr_cred_t *);
static int netr_gen_password(BYTE *, BYTE *, BYTE *);
static int netr_setup_authenticator(netr_info_t *,
    struct netr_authenticator *, struct netr_authenticator *);
static uint32_t netr_validate_chain(netr_info_t *, struct netr_authenticator *);
static uint32_t netlogon_logon(smb_authreq_t *, smb_authinfo_t *);
static uint32_t netr_server_samlogon(mlsvc_handle_t *, netr_info_t *, char *,
    smb_authreq_t *, smb_authinfo_t *);
static void netr_interactive_samlogon(netr_info_t *, smb_authreq_t *,
    struct netr_logon_info1 *);
static uint32_t netr_network_samlogon(ndr_heap_t *, netr_info_t *,
    smb_authreq_t *, struct netr_logon_info2 *);
static void netr_invalidate_chain(void);
static void netr_setup_identity(ndr_heap_t *, smb_authreq_t *,
    netr_logon_id_t *);


/*
 * Wrapper for the netlogon challenge-response authentication protocol
 * to set up the credential chain.
 */
DWORD
netlogon_setup_auth(char *server, char *domain)
{
	mlsvc_handle_t netr_handle;
	DWORD status;

	if (netr_open(server, domain, &netr_handle) != 0)
		return (NT_STATUS_OPEN_FAILED);

	status = netlogon_auth(server, &netr_handle, NETR_FLG_INIT);
	if (status != NT_STATUS_SUCCESS)
		syslog(LOG_NOTICE, "NETLOGON credential chain setup failed");

	(void) netr_close(&netr_handle);
	return (status);
}

boolean_t
smb_authinfo_setup_common(const netr_validation_info3_t *info3,
    smb_session_key_t *sessionkey, smb_authreq_t *authreq,
    smb_authinfo_t *authinfo)
{
	boolean_t valid;
	char nbdomain[NETBIOS_NAME_SZ];
	size_t size;
	int i;

	bzero(authinfo, sizeof (smb_authinfo_t));

	if (info3->EffectiveName.str != NULL)
		authinfo->a_usrname = strdup(
		    (char *)info3->EffectiveName.str);

	if (authinfo->a_usrname == NULL)
		authinfo->a_usrname = strdup(
		    authreq->au_eusername);

	authinfo->a_usrrid = info3->UserId;
	authinfo->a_grprid = info3->PrimaryGroupId;
	authinfo->a_grpcnt = info3->GroupCount;

	if (authinfo->a_grpcnt > 0) {
		size = sizeof (struct netr_group_membership) *
		    authinfo->a_grpcnt;

		if ((authinfo->a_grps = calloc(1, size)) != NULL)
			bcopy(info3->GroupIds, authinfo->a_grps, size);
	}

	authinfo->a_usersesskey = *sessionkey;

	if (info3->LogonDomainName.str != NULL)
		authinfo->a_domainname = strdup(
		    (char *)info3->LogonDomainName.str);

	if (authinfo->a_domainname == NULL) {
		if (*authreq->au_edomain != '\0') {
			authinfo->a_domainname = strdup(authreq->au_edomain);
		} else {
			(void) smb_getdomainname_nb(nbdomain,
			    sizeof (nbdomain));
			authinfo->a_domainname = strdup(nbdomain);
		}
	}

	authinfo->a_domainsid = (struct netr_sid *)smb_sid_dup(
	    (smb_sid_t *)info3->LogonDomainId);
	authinfo->a_sidcnt = info3->SidCount;

	if (authinfo->a_sidcnt > 0) {
		if ((authinfo->a_extra_sids = calloc(authinfo->a_sidcnt,
		    sizeof (struct netr_sid_and_attributes))) == NULL)
			goto validate;

		for (i = 0; i < authinfo->a_sidcnt; i++) {
			authinfo->a_extra_sids[i].sid =
			    (struct netr_sid *)smb_sid_dup(
			    (smb_sid_t *)info3->ExtraSids[i].sid);

			authinfo->a_extra_sids[i].attributes =
			    info3->ExtraSids[i].attributes;
		}
	}

validate:
	valid = smb_authinfo_validate(authinfo);
	if (!valid)
		smb_authinfo_free(authinfo);

	return (valid);
}

boolean_t
smb_authinfo_setup_krb(const kerb_validation_info_t *kerb_info,
    smb_session_key_t *ssnkey, smb_authreq_t *authreq, smb_authinfo_t *authinfo)
{
	size_t		size;
	boolean_t	valid;

	bzero(authinfo, sizeof (smb_authinfo_t));

	if (!smb_authinfo_setup_common(&kerb_info->BasicVinfo, ssnkey, authreq,
	    authinfo))
		return (B_FALSE);

	/* ResourceGroupDomainSid can be NULL */
	if (kerb_info->ResourceGroupDomainSid != NULL)
		authinfo->a_resgrp_domainsid = (struct netr_sid *)smb_sid_dup(
		    (smb_sid_t *)kerb_info->ResourceGroupDomainSid);

	authinfo->a_resgrpcnt = kerb_info->ResourceGroupCount;

	if (authinfo->a_resgrpcnt > 0) {
		size = sizeof (struct netr_group_membership) *
		    authinfo->a_resgrpcnt;

		if ((authinfo->a_resgrps = calloc(1, size)) != NULL)
			bcopy(kerb_info->ResourceGroupIds, authinfo->a_resgrps,
			    size);
	}

	valid = smb_authinfo_validate(authinfo);
	if (!valid)
		smb_authinfo_free(authinfo);

	return (valid);
}

boolean_t
smb_authinfo_validate(smb_authinfo_t *authinfo)
{
	int i;

	if ((authinfo->a_usrname == NULL) ||
	    (authinfo->a_domainname == NULL) ||
	    (authinfo->a_domainsid == NULL))
		return (B_FALSE);

	if ((authinfo->a_grpcnt != 0) &&
	    (authinfo->a_grps == NULL))
		return (B_FALSE);

	if (authinfo->a_sidcnt == 0)
		return (B_TRUE);

	if (authinfo->a_extra_sids == NULL)
		return (B_FALSE);

	for (i = 0; i < authinfo->a_sidcnt; i++) {
		if (authinfo->a_extra_sids[i].sid == NULL)
			return (B_FALSE);
	}

	if (!smb_session_key_valid(&authinfo->a_usersesskey))
		return (B_FALSE);

	if ((authinfo->a_resgrpcnt != 0) &&
	    (authinfo->a_resgrps == NULL))
		return (B_FALSE);

	return (B_TRUE);
}

void
smb_authinfo_free(smb_authinfo_t *authinfo)
{
	int i;

	if (authinfo == NULL)
		return;

	free(authinfo->a_usrname);
	free(authinfo->a_grps);
	smb_session_key_destroy(&authinfo->a_usersesskey);
	free(authinfo->a_domainname);
	free(authinfo->a_domainsid);

	for (i = 0; i < authinfo->a_sidcnt; i++)
		free(authinfo->a_extra_sids[i].sid);

	free(authinfo->a_extra_sids);
	free(authinfo->a_resgrp_domainsid);
	free(authinfo->a_resgrps);
	bzero(authinfo, sizeof (smb_authinfo_t));

}

/*
 * Determines if the given user is the domain Administrator or a
 * member of Domain Admins
 */
boolean_t
netr_isadmin(const smb_authinfo_t *authinfo)
{
	smb_domain_t di;
	int i;

	if (!smb_domain_lookup_sid((smb_sid_t *)authinfo->a_domainsid, &di))
		return (B_FALSE);

	if (di.di_type != SMB_DOMAIN_PRIMARY)
		return (B_FALSE);

	if ((authinfo->a_usrrid == DOMAIN_USER_RID_ADMIN) ||
	    (authinfo->a_grprid == DOMAIN_GROUP_RID_ADMINS))
		return (B_TRUE);

	for (i = 0; i < authinfo->a_grpcnt; i++)
		if (authinfo->a_grps[i].rid == DOMAIN_GROUP_RID_ADMINS)
			return (B_TRUE);

	return (B_FALSE);
}

/*
 * Abort impending domain logon requests.
 */
void
netr_logon_abort(void)
{
	(void) mutex_lock(&netlogon_mutex);
	if (netlogon_busy && !netlogon_abort)
		syslog(LOG_DEBUG, "logon abort");
	netlogon_abort = B_TRUE;
	(void) cond_broadcast(&netlogon_cv);
	(void) mutex_unlock(&netlogon_mutex);
}

/*
 * This is the entry point for authenticating domain users.
 * It is a wrapper function providing serialization and retries for
 * netlogon_logon().
 */
uint32_t
netr_logon(smb_authreq_t *authreq, smb_authinfo_t *authinfo)
{
	uint32_t	status;
	int		i;

	if ((authreq->au_secmode != SMB_SECMODE_DOMAIN) ||
	    (authreq->au_domain_type == SMB_DOMAIN_LOCAL)) {
		return (NT_STATUS_UNSUCCESSFUL);
	}

	for (i = 0; i < NETLOGON_ATTEMPTS; ++i) {
		(void) mutex_lock(&netlogon_mutex);
		while (netlogon_busy && !netlogon_abort)
			(void) cond_wait(&netlogon_cv, &netlogon_mutex);

		if (netlogon_abort) {
			(void) mutex_unlock(&netlogon_mutex);
			return (NT_STATUS_REQUEST_ABORTED);
		}

		netlogon_busy = B_TRUE;
		(void) mutex_unlock(&netlogon_mutex);

		status = netlogon_logon(authreq, authinfo);

		(void) mutex_lock(&netlogon_mutex);
		netlogon_busy = B_FALSE;
		if (netlogon_abort)
			status = NT_STATUS_REQUEST_ABORTED;
		(void) cond_signal(&netlogon_cv);
		(void) mutex_unlock(&netlogon_mutex);

		if (status != NT_STATUS_CANT_ACCESS_DOMAIN_INFO)
			break;
	}

	if (status != NT_STATUS_SUCCESS)
		syslog(LOG_INFO, "logon[%s\\%s]: %s", authreq->au_edomain,
		    authreq->au_eusername, xlate_nt_status(status));

	return (status);
}

/*
 * netr_open
 *
 * Open an authenticated session to the NETLOGON pipe on a domain controller
 * and bind to the NETR RPC interface.
 *
 * We store the remote server information, which is used to drive Windows
 * version specific behavior.
 */
static int
netr_open(char *server, char *domain, mlsvc_handle_t *netr_handle)
{
	char user[SMB_USERNAME_MAXLEN];

	smb_ipc_get_user(user, SMB_USERNAME_MAXLEN);

	if (ndr_rpc_bind(netr_handle, server, domain, user, "NETR") < 0)
		return (-1);

	return (0);
}

/*
 * netr_close
 *
 * Close a NETLOGON pipe and free the RPC context.
 */
static int
netr_close(mlsvc_handle_t *netr_handle)
{
	ndr_rpc_unbind(netr_handle);
	return (0);
}

/*
 * netlogon_auth
 *
 * This is the core of the NETLOGON authentication protocol.
 * Do the challenge response authentication.
 *
 * Prior to calling this function, an authenticated session to the NETLOGON
 * pipe on a domain controller(server) should have already been opened.
 *
 * Upon a successful NETLOGON credential chain establishment, the
 * netlogon sequence number will be set to match the kpasswd sequence
 * number.
 *
 */
static DWORD
netlogon_auth(char *server, mlsvc_handle_t *netr_handle, DWORD flags)
{
	netr_info_t *netr_info;
	int rc;
	DWORD leout_rc[2];

	netr_info = &netr_global_info;
	bzero(netr_info, sizeof (netr_info_t));

	netr_info->flags |= flags;

	rc = smb_getnetbiosname(netr_info->hostname, NETBIOS_NAME_SZ);
	if (rc != 0)
		return (NT_STATUS_UNSUCCESSFUL);

	ndr_rpc_format_nbhandle(server, netr_info->server,
	    sizeof (netr_info->server));

	LE_OUT32(&leout_rc[0], random());
	LE_OUT32(&leout_rc[1], random());
	(void) memcpy(&netr_info->client_challenge, leout_rc,
	    sizeof (struct netr_credential));

	if ((rc = netr_server_req_challenge(netr_handle, netr_info)) == 0) {
		rc = netr_server_authenticate2(netr_handle, netr_info);
		if (rc == 0) {
			smb_update_netlogon_seqnum();
			netr_info->flags |= NETR_FLG_VALID;
		}
	}

	return ((rc) ? NT_STATUS_UNSUCCESSFUL : NT_STATUS_SUCCESS);
}

/*
 * netr_server_req_challenge
 */
static int
netr_server_req_challenge(mlsvc_handle_t *netr_handle, netr_info_t *netr_info)
{
	struct netr_ServerReqChallenge arg;
	int opnum;

	bzero(&arg, sizeof (struct netr_ServerReqChallenge));
	opnum = NETR_OPNUM_ServerReqChallenge;

	arg.servername = (unsigned char *)netr_info->server;
	arg.hostname = (unsigned char *)netr_info->hostname;

	(void) memcpy(&arg.client_challenge, &netr_info->client_challenge,
	    sizeof (struct netr_credential));

	if (ndr_rpc_call(netr_handle, opnum, &arg) != 0)
		return (-1);

	if (arg.status != 0) {
		ndr_rpc_status(netr_handle, opnum, arg.status);
		ndr_rpc_release(netr_handle);
		return (-1);
	}

	(void) memcpy(&netr_info->server_challenge, &arg.server_challenge,
	    sizeof (struct netr_credential));

	ndr_rpc_release(netr_handle);
	return (0);
}

/*
 * netr_server_authenticate2
 */
static int
netr_server_authenticate2(mlsvc_handle_t *netr_handle, netr_info_t *netr_info)
{
	struct netr_ServerAuthenticate2 arg;
	int opnum;
	int rc;
	char account_name[NETBIOS_NAME_SZ * 2];

	bzero(&arg, sizeof (struct netr_ServerAuthenticate2));
	opnum = NETR_OPNUM_ServerAuthenticate2;

	(void) snprintf(account_name, sizeof (account_name), "%s$",
	    netr_info->hostname);

	smb_tracef("server=[%s] account_name=[%s] hostname=[%s]\n",
	    netr_info->server, account_name, netr_info->hostname);

	arg.servername = (unsigned char *)netr_info->server;
	arg.account_name = (unsigned char *)account_name;
	arg.account_type = NETR_WKSTA_TRUST_ACCOUNT_TYPE;
	arg.hostname = (unsigned char *)netr_info->hostname;
	arg.negotiate_flags = NETR_NEGOTIATE_BASE_FLAGS;

	if (ndr_rpc_server_os(netr_handle) == NATIVE_OS_WIN2000) {
		arg.negotiate_flags |= NETR_NEGOTIATE_STRONGKEY_FLAG;
		if (netr_gen_skey128(netr_info) != SMBAUTH_SUCCESS)
			return (-1);
	} else {
		if (netr_gen_skey64(netr_info) != SMBAUTH_SUCCESS)
			return (-1);
	}

	if (netr_gen_credentials(netr_info->session_key.key,
	    &netr_info->client_challenge, 0,
	    &netr_info->client_credential) != SMBAUTH_SUCCESS) {
		return (-1);
	}

	if (netr_gen_credentials(netr_info->session_key.key,
	    &netr_info->server_challenge, 0,
	    &netr_info->server_credential) != SMBAUTH_SUCCESS) {
		return (-1);
	}

	(void) memcpy(&arg.client_credential, &netr_info->client_credential,
	    sizeof (struct netr_credential));

	if (ndr_rpc_call(netr_handle, opnum, &arg) != 0)
		return (-1);

	if (arg.status != 0) {
		ndr_rpc_status(netr_handle, opnum, arg.status);
		ndr_rpc_release(netr_handle);
		return (-1);
	}

	rc = memcmp(&netr_info->server_credential, &arg.server_credential,
	    sizeof (struct netr_credential));

	ndr_rpc_release(netr_handle);
	return (rc);
}

/*
 * netr_gen_skey128
 *
 * Generate a 128-bit session key from the client and server challenges.
 */
static int
netr_gen_skey128(netr_info_t *netr_info)
{
	unsigned char ntlmhash[SMBAUTH_HASH_SZ];
	int rc = SMBAUTH_FAILURE;
	CK_RV rv;
	CK_MECHANISM mechanism;
	CK_SESSION_HANDLE hSession;
	CK_ULONG diglen = MD_DIGEST_LEN;
	unsigned char md5digest[MD_DIGEST_LEN];
	unsigned char zerobuf[NETR_SESSKEY_ZEROBUF_SZ];

	bzero(ntlmhash, SMBAUTH_HASH_SZ);
	/*
	 * We should check (netr_info->flags & NETR_FLG_INIT) and use
	 * the appropriate password but it isn't working yet.  So we
	 * always use the default one for now.
	 */
	bzero(netr_info->password, sizeof (netr_info->password));
	rc = smb_config_getstr(SMB_CI_MACHINE_PASSWD,
	    (char *)netr_info->password, sizeof (netr_info->password));

	if ((rc != SMBD_SMF_OK) || *netr_info->password == '\0') {
		return (SMBAUTH_FAILURE);
	}

	rc = smb_auth_ntlm_hash((char *)netr_info->password, ntlmhash);
	if (rc != SMBAUTH_SUCCESS)
		return (SMBAUTH_FAILURE);

	bzero(zerobuf, NETR_SESSKEY_ZEROBUF_SZ);

	mechanism.mechanism = CKM_MD5;
	mechanism.pParameter = 0;
	mechanism.ulParameterLen = 0;

	rv = SUNW_C_GetMechSession(mechanism.mechanism, &hSession);
	if (rv != CKR_OK)
		return (SMBAUTH_FAILURE);

	rv = C_DigestInit(hSession, &mechanism);
	if (rv != CKR_OK)
		goto cleanup;

	rv = C_DigestUpdate(hSession, (CK_BYTE_PTR)zerobuf,
	    NETR_SESSKEY_ZEROBUF_SZ);
	if (rv != CKR_OK)
		goto cleanup;

	rv = C_DigestUpdate(hSession,
	    (CK_BYTE_PTR)netr_info->client_challenge.data, NETR_CRED_DATA_SZ);
	if (rv != CKR_OK)
		goto cleanup;

	rv = C_DigestUpdate(hSession,
	    (CK_BYTE_PTR)netr_info->server_challenge.data, NETR_CRED_DATA_SZ);
	if (rv != CKR_OK)
		goto cleanup;

	rv = C_DigestFinal(hSession, (CK_BYTE_PTR)md5digest, &diglen);
	if (rv != CKR_OK)
		goto cleanup;

	rc = smb_auth_hmac_md5(md5digest, diglen, ntlmhash, SMBAUTH_HASH_SZ,
	    netr_info->session_key.key);

	netr_info->session_key.len = NETR_SESSKEY128_SZ;
cleanup:
	(void) C_CloseSession(hSession);
	return (rc);

}

/*
 * netr_gen_skey64
 *
 * Generate a 64-bit session key from the client and server challenges.
 * See "Session-Key Computation" section of MS-NRPC document.
 *
 * The algorithm is a two stage hash. For the first hash, the input is
 * the combination of the client and server challenges, the key is
 * the first 7 bytes of the password. The initial password is formed
 * using the NT password hash on the local hostname in lower case.
 * The result is stored in a temporary buffer.
 *
 *		input:	challenge
 *		key:	passwd lower 7 bytes
 *		output:	intermediate result
 *
 * For the second hash, the input is the result of the first hash and
 * the key is the last 7 bytes of the password.
 *
 *		input:	result of first hash
 *		key:	passwd upper 7 bytes
 *		output:	session_key
 *
 * The final output should be the session key.
 *
 *		FYI: smb_auth_DES(output, key, input)
 *
 * If any difficulties occur using the cryptographic framework, the
 * function returns SMBAUTH_FAILURE.  Otherwise SMBAUTH_SUCCESS is
 * returned.
 */
static int
netr_gen_skey64(netr_info_t *netr_info)
{
	unsigned char md4hash[32];
	unsigned char buffer[8];
	DWORD data[2];
	DWORD *client_challenge;
	DWORD *server_challenge;
	int rc;
	DWORD le_data[2];

	client_challenge = (DWORD *)(uintptr_t)&netr_info->client_challenge;
	server_challenge = (DWORD *)(uintptr_t)&netr_info->server_challenge;
	bzero(md4hash, 32);

	/*
	 * We should check (netr_info->flags & NETR_FLG_INIT) and use
	 * the appropriate password but it isn't working yet.  So we
	 * always use the default one for now.
	 */
	bzero(netr_info->password, sizeof (netr_info->password));
	rc = smb_config_getstr(SMB_CI_MACHINE_PASSWD,
	    (char *)netr_info->password, sizeof (netr_info->password));

	if ((rc != SMBD_SMF_OK) || *netr_info->password == '\0') {
		return (SMBAUTH_FAILURE);
	}

	rc = smb_auth_ntlm_hash((char *)netr_info->password, md4hash);

	if (rc != SMBAUTH_SUCCESS)
		return (SMBAUTH_FAILURE);

	data[0] = LE_IN32(&client_challenge[0]) + LE_IN32(&server_challenge[0]);
	data[1] = LE_IN32(&client_challenge[1]) + LE_IN32(&server_challenge[1]);
	LE_OUT32(&le_data[0], data[0]);
	LE_OUT32(&le_data[1], data[1]);
	rc = smb_auth_DES(buffer, 8, md4hash, NETR_DESKEY_LEN,
	    (unsigned char *)le_data, 8);

	if (rc != SMBAUTH_SUCCESS)
		return (rc);

	netr_info->session_key.len = NETR_SESSKEY64_SZ;
	rc = smb_auth_DES(netr_info->session_key.key,
	    netr_info->session_key.len, &md4hash[9], NETR_DESKEY_LEN, buffer,
	    8);

	return (rc);
}

/*
 * netr_gen_credentials
 *
 * Generate a set of credentials from a challenge and a session key.
 * The algorithm is a two stage hash. For the first hash, the
 * timestamp is added to the challenge and the result is stored in a
 * temporary buffer:
 *
 *		input:	challenge (including timestamp)
 *		key:	session_key
 *		output:	intermediate result
 *
 * For the second hash, the input is the result of the first hash and
 * a strange partial key is used:
 *
 *		input:	result of first hash
 *		key:	funny partial key
 *		output:	credentiails
 *
 * The final output should be an encrypted set of credentials.
 *
 *		FYI: smb_auth_DES(output, key, input)
 *
 * If any difficulties occur using the cryptographic framework, the
 * function returns SMBAUTH_FAILURE.  Otherwise SMBAUTH_SUCCESS is
 * returned.
 */
static int
netr_gen_credentials(BYTE *session_key, netr_cred_t *challenge,
    DWORD timestamp, netr_cred_t *out_cred)
{
	unsigned char buffer[8];
	DWORD data[2];
	DWORD le_data[2];
	DWORD *p;
	int rc;

	p = (DWORD *)(uintptr_t)challenge;
	data[0] = LE_IN32(&p[0]) + timestamp;
	data[1] = LE_IN32(&p[1]);

	LE_OUT32(&le_data[0], data[0]);
	LE_OUT32(&le_data[1], data[1]);

	if (smb_auth_DES(buffer, 8, session_key, NETR_DESKEY_LEN,
	    (unsigned char *)le_data, 8) != SMBAUTH_SUCCESS)
		return (SMBAUTH_FAILURE);

	rc = smb_auth_DES(out_cred->data, 8, &session_key[NETR_DESKEY_LEN],
	    NETR_DESKEY_LEN, buffer, 8);

	return (rc);
}

/*
 * netr_gen_password
 *
 * Generate a new pasword from the old password  and the session key.
 * The algorithm is a two stage hash. The session key is used in the
 * first hash but only part of the session key is used in the second
 * hash.
 *
 * If any difficulties occur using the cryptographic framework, the
 * function returns SMBAUTH_FAILURE.  Otherwise SMBAUTH_SUCCESS is
 * returned.
 */
static int
netr_gen_password(BYTE *session_key, BYTE *old_password, BYTE *new_password)
{
	int rv;

	rv = smb_auth_DES(new_password, 8, session_key, NETR_DESKEY_LEN,
	    old_password, 8);
	if (rv != SMBAUTH_SUCCESS)
		return (rv);

	rv = smb_auth_DES(&new_password[8], 8, &session_key[NETR_DESKEY_LEN],
	    NETR_DESKEY_LEN, &old_password[8], 8);
	return (rv);
}

/*
 * netr_setup_authenticator
 *
 * Set up the request and return authenticators. A new credential is
 * generated from the session key, the current client credential and
 * the current time, i.e.
 *
 *		NewCredential = Cred(SessionKey, OldCredential, time);
 *
 * The timestamp, which is used as a random seed, is stored in both
 * the request and return authenticators.
 *
 * If any difficulties occur using the cryptographic framework, the
 * function returns SMBAUTH_FAILURE.  Otherwise SMBAUTH_SUCCESS is
 * returned.
 */
static int
netr_setup_authenticator(netr_info_t *netr_info,
    struct netr_authenticator *auth, struct netr_authenticator *ret_auth)
{
	bzero(auth, sizeof (struct netr_authenticator));

	netr_info->timestamp = time(0);
	auth->timestamp = netr_info->timestamp;

	if (netr_gen_credentials(netr_info->session_key.key,
	    &netr_info->client_credential,
	    netr_info->timestamp,
	    (netr_cred_t *)&auth->credential) != SMBAUTH_SUCCESS)
		return (SMBAUTH_FAILURE);

	if (ret_auth) {
		bzero(ret_auth, sizeof (struct netr_authenticator));
		ret_auth->timestamp = netr_info->timestamp;
	}

	return (SMBAUTH_SUCCESS);
}

/*
 * Validate the returned credentials and update the credential chain.
 * The server returns an updated client credential rather than a new
 * server credential.  The server uses (timestamp + 1) when generating
 * the credential.
 *
 * Generate the new seed for the credential chain. The new seed is
 * formed by adding (timestamp + 1) to the current client credential.
 * The only quirk is the uint32_t style addition.
 *
 * Returns NT_STATUS_INSUFFICIENT_LOGON_INFO if auth->credential is a
 * NULL pointer. The Authenticator field of the SamLogon response packet
 * sent by the Samba 3 PDC always return NULL pointer if the received
 * SamLogon request is not immediately followed by the ServerReqChallenge
 * and ServerAuthenticate2 requests.
 *
 * The size of client_credential is NETR_CRED_DATA_SZ (8 bytes). If the
 * credential chain is valid, the new client_credential is generated by
 * adding netr_info->timestamp to the least significant 4 bytes of the
 * current 8 byte client credential.
 *
 * Returns NT_STATUS_SUCCESS if the server returned a valid credential.
 * Otherwise we retirm NT_STATUS_UNSUCCESSFUL.
 */
static uint32_t
netr_validate_chain(netr_info_t *netr_info, struct netr_authenticator *auth)
{
	netr_cred_t cred;
	uint32_t result = NT_STATUS_SUCCESS;
	uint32_t *le_dwp;
	uint32_t dwp[2];
	int rc;

	++netr_info->timestamp;

	if (netr_gen_credentials(netr_info->session_key.key,
	    &netr_info->client_credential,
	    netr_info->timestamp, &cred) != SMBAUTH_SUCCESS)
		return (NT_STATUS_INTERNAL_ERROR);

	if (&auth->credential == 0) {
		/*
		 * If the validation fails, destroy the credential chain.
		 * This should trigger a new authentication chain.
		 */
		bzero(netr_info, sizeof (netr_info_t));
		return (NT_STATUS_INSUFFICIENT_LOGON_INFO);
	}

	rc = memcmp(&cred, &auth->credential, sizeof (netr_cred_t));
	if (rc != 0) {
		/*
		 * If the validation fails, destroy the credential chain.
		 * This should trigger a new authentication chain.
		 */
		bzero(netr_info, sizeof (netr_info_t));
		result = NT_STATUS_UNSUCCESSFUL;
	} else {
		/*
		 * Otherwise generate the next step in the chain.
		 */
		le_dwp = (uint32_t *)(uintptr_t)&netr_info->client_credential;
		dwp[0] = LE_IN32(&le_dwp[0]) + netr_info->timestamp;
		dwp[1] = LE_IN32(&le_dwp[1]);

		LE_OUT32(&le_dwp[0], dwp[0]);
		LE_OUT32(&le_dwp[1], dwp[1]);

		netr_info->flags |= NETR_FLG_VALID;
	}

	return (result);
}

/*
 * Establish NETLOGON credential chain with the bound DC if necessary.
 * Once the credential chain is validated, perform pass-through authentication
 * by sending network logon requests.
 * If the user is successfully authenticated, the output parameter 'authinfo'
 * will be populated in preparation for generating an access token.
 */
static uint32_t
netlogon_logon(smb_authreq_t *authreq, smb_authinfo_t *authinfo)
{
	char resource_domain[SMB_PI_MAX_DOMAIN];
	char nbname[NDR_BIND_NBNAME_SZ];
	mlsvc_handle_t netr_handle;
	smb_domainex_t di;
	uint32_t status;
	int retries = 0;

	(void) smb_getdomainname_nb(resource_domain, SMB_PI_MAX_DOMAIN);

	if (!smb_domain_getinfo(&di)) {
		netr_invalidate_chain();
		return (NT_STATUS_CANT_ACCESS_DOMAIN_INFO);
	}

	ndr_rpc_format_nbhandle(di.d_dc, nbname, sizeof (nbname));

	do {
		if (netr_open(di.d_dc, di.d_primary.di_nbname, &netr_handle)
		    != 0)
			return (NT_STATUS_OPEN_FAILED);

		if (*netr_global_info.server != '\0') {
			if (strncasecmp(netr_global_info.server,
			    nbname, strlen(nbname)) != 0)
				netr_invalidate_chain();
		}

		if ((netr_global_info.flags & NETR_FLG_VALID) == 0 ||
		    !smb_match_netlogon_seqnum()) {
			status = netlogon_auth(di.d_dc, &netr_handle,
			    NETR_FLG_NULL);

			if (status != 0) {
				(void) netr_close(&netr_handle);
				return (NT_STATUS_LOGON_FAILURE);
			}

			netr_global_info.flags |= NETR_FLG_VALID;
		}

		status = netr_server_samlogon(&netr_handle,
		    &netr_global_info, di.d_dc, authreq, authinfo);

		(void) netr_close(&netr_handle);
	} while (status == NT_STATUS_INSUFFICIENT_LOGON_INFO && retries++ < 3);

	if (retries >= 3)
		status = NT_STATUS_LOGON_FAILURE;

	return (status);
}

/*
 * netr_server_samlogon
 *
 * NetrServerSamLogon RPC: interactive or network. It is assumed that
 * we have already authenticated with the PDC. If everything works,
 * a smb_authinfo_t structure, which contains the information
 * that will be required for building an access token, will be returned.
 *
 * Returns an NT status. There are numerous possibilities here.
 * For example:
 *	NT_STATUS_INVALID_INFO_CLASS
 *	NT_STATUS_INVALID_PARAMETER
 *	NT_STATUS_ACCESS_DENIED
 *	NT_STATUS_PASSWORD_MUST_CHANGE
 *	NT_STATUS_NO_SUCH_USER
 *	NT_STATUS_WRONG_PASSWORD
 *	NT_STATUS_LOGON_FAILURE
 *	NT_STATUS_ACCOUNT_RESTRICTION
 *	NT_STATUS_INVALID_LOGON_HOURS
 *	NT_STATUS_INVALID_WORKSTATION
 *	NT_STATUS_INTERNAL_ERROR
 *	NT_STATUS_PASSWORD_EXPIRED
 *	NT_STATUS_ACCOUNT_DISABLED
 */
static uint32_t
netr_server_samlogon(mlsvc_handle_t *netr_handle, netr_info_t *netr_info,
    char *server, smb_authreq_t *authreq, smb_authinfo_t *authinfo)
{
	struct netr_SamLogon arg;
	struct netr_authenticator auth;
	struct netr_authenticator ret_auth;
	struct netr_logon_info1 info1;
	struct netr_logon_info2 info2;
	struct netr_validation_info3 *info3;
	ndr_heap_t *heap;
	int opnum;
	int rc;
	uint32_t status;
	smb_session_key_t ssnkey;
	unsigned char rc4key[NETR_SESSKEY_MAXSZ];

	if (authinfo == NULL) {
		ndr_rpc_release(netr_handle);
		return (NT_STATUS_INVALID_PARAMETER);
	}

	bzero(&arg, sizeof (struct netr_SamLogon));
	opnum = NETR_OPNUM_SamLogon;

	/*
	 * Should we get the server and hostname from netr_info?
	 */
	arg.servername = ndr_rpc_derive_nbhandle(netr_handle, server);
	arg.hostname = ndr_rpc_malloc(netr_handle, NETBIOS_NAME_SZ);
	if (arg.servername == NULL || arg.hostname == NULL) {
		ndr_rpc_release(netr_handle);
		return (NT_STATUS_INTERNAL_ERROR);
	}

	if (smb_getnetbiosname((char *)arg.hostname, NETBIOS_NAME_SZ) != 0) {
		ndr_rpc_release(netr_handle);
		return (NT_STATUS_INTERNAL_ERROR);
	}

	rc = netr_setup_authenticator(netr_info, &auth, &ret_auth);
	if (rc != SMBAUTH_SUCCESS) {
		ndr_rpc_release(netr_handle);
		return (NT_STATUS_INTERNAL_ERROR);
	}

	arg.auth = &auth;
	arg.ret_auth = &ret_auth;
	arg.validation_level = NETR_VALIDATION_LEVEL3;
	arg.logon_info.logon_level = authreq->au_level;
	arg.logon_info.switch_value = authreq->au_level;

	heap = ndr_rpc_get_heap(netr_handle);

	switch (authreq->au_level) {
	case NETR_INTERACTIVE_LOGON:
		netr_setup_identity(heap, authreq, &info1.identity);
		netr_interactive_samlogon(netr_info, authreq, &info1);
		arg.logon_info.ru.info1 = &info1;
		break;

	case NETR_NETWORK_LOGON:
		netr_setup_identity(heap, authreq, &info2.identity);
		status = netr_network_samlogon(heap, netr_info, authreq,
		    &info2);
		if (status != NT_STATUS_SUCCESS) {
			ndr_rpc_release(netr_handle);
			return (status);
		}
		arg.logon_info.ru.info2 = &info2;
		break;

	default:
		ndr_rpc_release(netr_handle);
		return (NT_STATUS_INVALID_PARAMETER);
	}

	rc = ndr_rpc_call(netr_handle, opnum, &arg);
	if (rc != 0) {
		bzero(netr_info, sizeof (netr_info_t));
		status = NT_STATUS_INVALID_PARAMETER;
	} else if (arg.status != 0) {
		status = NT_SC_VALUE(arg.status);

		/*
		 * We need to validate the chain even though we have
		 * a non-zero status. If the status is ACCESS_DENIED
		 * this will trigger a new credential chain. However,
		 * a valid credential is returned with some status
		 * codes; for example, WRONG_PASSWORD.
		 */
		(void) netr_validate_chain(netr_info, arg.ret_auth);
	} else {
		status = netr_validate_chain(netr_info, arg.ret_auth);
		if (status == NT_STATUS_INSUFFICIENT_LOGON_INFO) {
			ndr_rpc_release(netr_handle);
			return (status);
		}

		info3 = arg.ru.info3;
#ifndef NTLM2_SESSION_SECURITY_DOMAIN
		/*
		 * The UserSessionKey in NetrSamLogon RPC is obfuscated
		 * using the session key obtained in the NETLOGON credential
		 * chain. An 8 byte session key is zero extended to 16 bytes.
		 * This 16 byte key is the key to the RC4 algorithm. The RC4
		 * byte stream is exclusively ored with the 16 byte
		 * UserSessionKey to recover the the clear form.
		 */
		bzero(rc4key, sizeof (rc4key));
		bcopy(netr_info->session_key.key, rc4key,
		    netr_info->session_key.len);

		if ((status = smb_session_key_create(&ssnkey,
		    (uint8_t *)info3->UserSessionKey.data,
		    SMBAUTH_SESSION_KEY_SZ)) != NT_STATUS_SUCCESS) {
			ndr_rpc_release(netr_handle);
			return (status);
		}

		rand_hash(ssnkey.val, ssnkey.len, rc4key, sizeof (rc4key));
#else
		/*
		 * TODO: generate NTLM2 session response user session key.
		 */
#endif
		if (!smb_authinfo_setup_common(info3, &ssnkey, authreq,
		    authinfo))
			status = NT_STATUS_NO_MEMORY;
	}

	ndr_rpc_release(netr_handle);
	return (status);
}

/*
 * netr_interactive_samlogon
 *
 * Set things up for an interactive SamLogon. Copy the NT and LM
 * passwords to the logon structure and hash them with the session
 * key.
 */
static void
netr_interactive_samlogon(netr_info_t *netr_info, smb_authreq_t *authreq,
    struct netr_logon_info1 *info1)
{
	BYTE key[NETR_OWF_PASSWORD_SZ];

	(void) memcpy(&info1->lm_owf_password,
	    authreq->au_lmpasswd.val, sizeof (netr_owf_password_t));

	(void) memcpy(&info1->nt_owf_password,
	    authreq->au_ntpasswd.val, sizeof (netr_owf_password_t));

	(void) memset(key, 0, NETR_OWF_PASSWORD_SZ);
	(void) memcpy(key, netr_info->session_key.key,
	    netr_info->session_key.len);

	rand_hash((unsigned char *)&info1->lm_owf_password,
	    NETR_OWF_PASSWORD_SZ, key, NETR_OWF_PASSWORD_SZ);

	rand_hash((unsigned char *)&info1->nt_owf_password,
	    NETR_OWF_PASSWORD_SZ, key, NETR_OWF_PASSWORD_SZ);
}

/*
 * netr_network_samlogon
 *
 * Set things up for a network SamLogon.  We provide a copy of the random
 * challenge, that we sent to the client, to the domain controller.  This
 * is the key that the client will have used to encrypt the NT and LM
 * passwords.  Note that Windows 9x clients may not provide both passwords.
 */
/*ARGSUSED*/
static uint32_t
netr_network_samlogon(ndr_heap_t *heap, netr_info_t *netr_info,
    smb_authreq_t *authreq, struct netr_logon_info2 *info2)
{
	uint32_t len;
#ifdef NTLM2_SESSION_SECURITY_DOMAIN
	smb_buf16_t md5input;
	uint8_t md5digest[MD_DIGEST_LEN];

	/*
	 * TODO: NTLM2 session security - if negFlags sets to
	 * NTLM2_EXTENDED_SECURITY and LM response only has the first 8
	 * bytes set and followed by 16 NULL chars.
	 */
	md5input.len = authreq->au_challenge_key.len + 8;
	md5input.val = calloc(1, md5input.len);
	if (md5input.val == NULL)
		return (NT_STATUS_NO_MEMORY);

	bcopy(authreq->au_challenge_key.val, md5input.val,
	    authreq->au_challenge_key.len);
	bcopy(authreq->au_lmpasswd.val,
	    md5input.val + authreq->au_challenge_key.len, 8);
	if (smb_auth_md5(md5digest, md5input.val, md5input.len)
	    != SMBAUTH_SUCCESS) {
		free(md5input.val);
		return (NT_STATUS_INTERNAL_ERROR);
	}

	bcopy(md5digest, info2->lm_challenge.data, 8);
	free(md5input.val);
#else
	bcopy(authreq->au_challenge_key.val, info2->lm_challenge.data, 8);
#endif

	if ((len = authreq->au_ntpasswd.len) != 0) {
		ndr_heap_mkvcb(heap, authreq->au_ntpasswd.val, len,
		    (ndr_vcbuf_t *)&info2->nt_response);
	} else {
		bzero(&info2->nt_response, sizeof (netr_vcbuf_t));
	}

	if ((len = authreq->au_lmpasswd.len) != 0) {
		ndr_heap_mkvcb(heap, authreq->au_lmpasswd.val, len,
		    (ndr_vcbuf_t *)&info2->lm_response);
	} else {
		bzero(&info2->lm_response, sizeof (netr_vcbuf_t));
	}

	return (NT_STATUS_SUCCESS);
}

/*
 * netr_invalidate_chain
 *
 * Mark the credential chain as invalid so that it will be recreated
 * on the next attempt.
 */
static void
netr_invalidate_chain(void)
{
	netr_global_info.flags &= ~NETR_FLG_VALID;
}

/*
 * netr_setup_identity
 *
 * Set up the client identity information. All of this information is
 * specifically related to the client user and workstation attempting
 * to access this system. It may not be in our primary domain.
 *
 * I don't know what logon_id is, it seems to be a unique identifier.
 * Increment it before each use.
 */
static void
netr_setup_identity(ndr_heap_t *heap, smb_authreq_t *authreq,
    netr_logon_id_t *identity)
{
	static mutex_t logon_id_mutex;
	static uint32_t logon_id;

	(void) mutex_lock(&logon_id_mutex);

	if (logon_id == 0)
		logon_id = 0xDCD0;

	++logon_id;
	authreq->au_logon_id = logon_id;

	(void) mutex_unlock(&logon_id_mutex);

	identity->parameter_control = (NETR_LOGON_FLG_SERVER_TRUST_ACCT |
	    NETR_LOGON_FLG_WKST_TRUST_ACCT);
	identity->logon_id.LowPart = logon_id;
	identity->logon_id.HighPart = 0;

	ndr_heap_mkvcs(heap, authreq->au_domain,
	    (ndr_vcstr_t *)&identity->domain_name);

	ndr_heap_mkvcs(heap, authreq->au_username,
	    (ndr_vcstr_t *)&identity->username);

	/*
	 * Some systems prefix the client workstation name with \\.
	 * It doesn't seem to make any difference whether it's there
	 * or not.
	 */
	ndr_heap_mkvcs(heap, authreq->au_workstation,
	    (ndr_vcstr_t *)&identity->workstation);
}


/*
 * netr_server_password_set
 *
 * Attempt to change the trust account password for this system.
 *
 * Note that this call may legitimately fail if the registry on the
 * domain controller has been setup to deny attempts to change the
 * trust account password. In this case we should just continue to
 * use the original password.
 *
 * Possible status values:
 *	NT_STATUS_ACCESS_DENIED
 */
int
netr_server_password_set(mlsvc_handle_t *netr_handle, netr_info_t *netr_info)
{
	struct netr_PasswordSet  arg;
	int opnum;
	BYTE new_password[NETR_OWF_PASSWORD_SZ];
	char account_name[NETBIOS_NAME_SZ * 2];

	bzero(&arg, sizeof (struct netr_PasswordSet));
	opnum = NETR_OPNUM_ServerPasswordSet;

	(void) snprintf(account_name, sizeof (account_name), "%s$",
	    netr_info->hostname);

	arg.servername = (unsigned char *)netr_info->server;
	arg.account_name = (unsigned char *)account_name;
	arg.account_type = NETR_WKSTA_TRUST_ACCOUNT_TYPE;
	arg.hostname = (unsigned char *)netr_info->hostname;

	/*
	 * Set up the client side authenticator.
	 */
	if (netr_setup_authenticator(netr_info, &arg.auth, 0) !=
	    SMBAUTH_SUCCESS) {
		return (-1);
	}

	/*
	 * Generate a new password from the old password.
	 */
	if (netr_gen_password(netr_info->session_key.key,
	    netr_info->password, new_password) == SMBAUTH_FAILURE) {
		return (-1);
	}

	(void) memcpy(&arg.uas_new_password, &new_password,
	    NETR_OWF_PASSWORD_SZ);

	if (ndr_rpc_call(netr_handle, opnum, &arg) != 0)
		return (-1);

	if (arg.status != 0) {
		ndr_rpc_status(netr_handle, opnum, arg.status);
		ndr_rpc_release(netr_handle);
		return (-1);
	}

	/*
	 * Check the returned credentials.  The server returns the new
	 * client credential rather than the new server credentiali,
	 * as documented elsewhere.
	 *
	 * Generate the new seed for the credential chain.  Increment
	 * the timestamp and add it to the client challenge.  Then we
	 * need to copy the challenge to the credential field in
	 * preparation for the next cycle.
	 */
	if (netr_validate_chain(netr_info, &arg.auth) == 0) {
		/*
		 * Save the new password.
		 */
		(void) memcpy(netr_info->password, new_password,
		    NETR_OWF_PASSWORD_SZ);
	}

	ndr_rpc_release(netr_handle);
	return (0);
}

uint32_t
smb_session_key_create(smb_session_key_t *ssnkey, uint8_t *buf, uint32_t len)
{
	assert(ssnkey != NULL);
	assert(buf != NULL);

	if ((ssnkey->val = calloc(1, len)) == NULL)
		return (NT_STATUS_NO_MEMORY);

	ssnkey->len = len;
	bcopy(buf, ssnkey->val, ssnkey->len);

	return (NT_STATUS_SUCCESS);
}

void
smb_session_key_destroy(smb_session_key_t *ssnkey)
{
	if (smb_session_key_valid(ssnkey)) {
		free(ssnkey->val);
		bzero(ssnkey, sizeof (smb_session_key_t));
	}
}

ndr_buf_t *
netr_ndrbuf_init(void)
{
	return (ndr_buf_init(&TYPEINFO(netr_interface)));
}

void
netr_ndrbuf_fini(ndr_buf_t *ndrbuf)
{
	if (ndrbuf != NULL)
		ndr_buf_fini(ndrbuf);
}
