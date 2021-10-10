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

#include <sys/types.h>
#include <sys/sid.h>
#include <sys/priv_names.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <smbsrv/smb_idmap.h>
#include <smbsrv/smb_kproto.h>
#include <smbsrv/smb_token.h>

typedef struct smb_native {
	int sn_value;
	const char *sn_name;
} smb_native_t;

static int smb_session_setup_decode_lm(smb_request_t *);
static int smb_session_setup_decode_ntlm(smb_request_t *);
static int smb_session_setup_decode_extended(smb_request_t *);
static void smb_session_setup_decode_common(smb_request_t *);
static int smb_session_setup_encode(smb_request_t *);
static int smb_session_setup_encode_extended(smb_request_t *);
static int smb_session_setup_init_signing(smb_request_t *);
static int smb_session_setup_native_os(const char *);
static int smb_session_setup_native_lm(const char *);

smb_sdrc_t
smb_pre_session_setup_andx(smb_request_t *sr)
{
	int		rc = 0;
	int		dialect = sr->session->dialect;

	sr->sr_ssetup = smb_srm_zalloc(sr, sizeof (smb_ssetup_t));

	if (dialect < NTLM_0_12)
		rc = smb_session_setup_decode_lm(sr);
	else if (sr->smb_wct == 12)
		rc = smb_session_setup_decode_extended(sr);
	else
		rc = smb_session_setup_decode_ntlm(sr);

	if (rc == 0)
		smb_session_setup_decode_common(sr);

	DTRACE_SMB_2(op__SessionSetupX__start, smb_request_t *, sr,
	    smb_authreq_t, &sr->sr_ssetup->ss_authreq);
	return ((rc == 0) ? SDRC_SUCCESS : SDRC_ERROR);
}

/*
 * Decode a SmbSessionSetupX requests sent by a client that negotiated
 * pre-NTLM_0_12 dialect.
 * For Pre-NTLM 0.12, despite the CIFS/1.0 spec, the user and domain are
 * not always present in the message.  We try to get the account name and
 * the primary domain but we don't care about the the native OS or native
 * LM fields.
 */
static int
smb_session_setup_decode_lm(smb_request_t *sr)
{
	int		rc;
	smb_authreq_t	*authreq = &sr->sr_ssetup->ss_authreq;
	uint8_t		*lmpasswd;
	uint16_t	lmpasswd_len;

	rc = smbsr_decode_vwv(sr, "b.wwwwlw4.",
	    &sr->andx_com,
	    &sr->andx_off,
	    &authreq->au_maxbufsize,
	    &authreq->au_maxmpxcount,
	    &authreq->au_vcnumber,
	    &authreq->au_sesskey,
	    &lmpasswd_len);
	if (rc != 0)
		return (-1);

	lmpasswd = smb_srm_zalloc(sr, lmpasswd_len +1);
	rc = smbsr_decode_data(sr, "%#c", sr, lmpasswd_len, lmpasswd);
	if (rc != 0)
		return (-1);

	lmpasswd[lmpasswd_len] = 0;
	authreq->au_lmpasswd.val = lmpasswd;
	authreq->au_lmpasswd.len = lmpasswd_len;

	if (smbsr_decode_data(sr, "%u", sr, &authreq->au_username) != 0)
		authreq->au_username = smb_srm_strdup(sr, "");

	if (smbsr_decode_data(sr, "%u", sr, &authreq->au_domain) != 0)
		authreq->au_domain = smb_srm_strdup(sr, "");

	authreq->au_native_os = NATIVE_OS_WINNT;
	authreq->au_native_lm = NATIVE_LM_NT;
	authreq->au_flags |= SMB_AUTH_LM_PASSWD;
	return (0);
}

/*
 * Decode a SmbSessionSetupX request sent by a client that negotiated
 * NTLM_0_12 dialect or above.
 *
 * Some clients (Windows NT, Windows Server 2003) insert an extra '\0'
 * after native_os. If the native_lm is decoded as "\0" and there is
 * more data, decode the remaining data as the native_lm.
 */
static int
smb_session_setup_decode_ntlm(smb_request_t *sr)
{
	int		rc;
	char		*native_os = NULL;
	char		*native_lm = NULL;
	uint8_t		*lmpasswd, *ntpasswd;
	uint16_t	lmpasswd_len, ntpasswd_len;
	smb_authreq_t	*authreq = &sr->sr_ssetup->ss_authreq;

	if (sr->smb_wct != 13)
		return (-1);

	rc = smbsr_decode_vwv(sr, "b.wwwwlww4.l",
	    &sr->andx_com,
	    &sr->andx_off,
	    &authreq->au_maxbufsize,
	    &authreq->au_maxmpxcount,
	    &authreq->au_vcnumber,
	    &authreq->au_sesskey,
	    &lmpasswd_len,
	    &ntpasswd_len,
	    &authreq->au_capabilities);
	if (rc != 0)
		return (-1);

	lmpasswd = smb_srm_zalloc(sr, lmpasswd_len + 1);
	ntpasswd = smb_srm_zalloc(sr, ntpasswd_len + 1);
	rc = smbsr_decode_data(sr, "%#c#cuuuu",
	    sr,
	    lmpasswd_len, lmpasswd,
	    ntpasswd_len, ntpasswd,
	    &authreq->au_username,
	    &authreq->au_domain,
	    &native_os,
	    &native_lm);
	if (rc != 0)
		return (-1);

	if (native_lm && (native_lm[0] == '\0') && smbsr_decode_data_avail(sr))
		(void) smbsr_decode_data(sr, "%u", sr, &native_lm);

	lmpasswd[lmpasswd_len] = 0;
	ntpasswd[ntpasswd_len] = 0;

	authreq->au_lmpasswd.val = lmpasswd;
	authreq->au_lmpasswd.len = lmpasswd_len;
	authreq->au_ntpasswd.val = ntpasswd;
	authreq->au_ntpasswd.len = ntpasswd_len;

	authreq->au_native_os = smb_session_setup_native_os(native_os);
	authreq->au_native_lm = smb_session_setup_native_lm(native_lm);
	authreq->au_flags |= (SMB_AUTH_LM_PASSWD | SMB_AUTH_NTLM_PASSWD);

	return (0);
}

/*
 * Decode an extended SmbSessionSetupX request sent by a client that
 * negotiated NTLM_0_12 dialect or above.
 *
 * Some clients (Windows NT, Windows Server 2003) insert an extra '\0'
 * after native_os. If the native_lm is decoded as "\0" and there is
 * more data, decode the remaining data as the native_lm.
 *
 * Extended SmbSessionSetup requests don't have cleartext username
 * and domain, so we set these to be "" in the authreq.
 */
static int
smb_session_setup_decode_extended(smb_request_t *sr)
{
	int	rc = 0;
	char	*native_os = NULL;
	char 	*native_lm = NULL;
	smb_authreq_t	*authreq = &sr->sr_ssetup->ss_authreq;

	if (sr->sr_cfg->skc_extsec_enable == 0) {
		cmn_err(CE_NOTE, "Extended Security disabled: "
		    "Unexpected request");
		return (-1);
	}

	rc = smbsr_decode_vwv(sr, "b.wwwwlw4.l",
	    &sr->andx_com,
	    &sr->andx_off,
	    &authreq->au_maxbufsize,
	    &authreq->au_maxmpxcount,
	    &authreq->au_vcnumber,
	    &authreq->au_sesskey,
	    &authreq->au_secblob.len,
	    &authreq->au_capabilities);
	if ((rc != 0) ||
	    !(authreq->au_capabilities & CAP_EXTENDED_SECURITY))
		return (-1);

	authreq->au_secblob.val = smb_srm_zalloc(sr, authreq->au_secblob.len);
	rc = smbsr_decode_data(sr, "%#cuu", sr,
	    authreq->au_secblob.len, authreq->au_secblob.val,
	    &native_os, &native_lm);
	if (rc != 0)
		return (-1);

	if (native_lm && (native_lm[0] == '\0') && smbsr_decode_data_avail(sr))
		(void) smbsr_decode_data(sr, "%u", sr, &native_lm);

	authreq->au_username = smb_srm_strdup(sr, "");
	authreq->au_domain = smb_srm_strdup(sr, "");
	authreq->au_native_os = smb_session_setup_native_os(native_os);
	authreq->au_native_lm = smb_session_setup_native_lm(native_lm);
	authreq->au_flags |= SMB_AUTH_SECBLOB;
	return (0);
}

/*
 * Common processing of decoded username and domainname.
 */
static void
smb_session_setup_decode_common(smb_request_t *sr)
{
	smb_authreq_t	*authreq = &sr->sr_ssetup->ss_authreq;
	char		*p, *buf = NULL;

	authreq = &sr->sr_ssetup->ss_authreq;
	authreq->au_edomain = authreq->au_domain;

	if (!(authreq->au_flags & SMB_AUTH_SECBLOB) &&
	    (authreq->au_username[0] == '\0') &&
	    (authreq->au_ntpasswd.len == 0) &&
	    (authreq->au_lmpasswd.len == 0 ||
	    (authreq->au_lmpasswd.len == 1 &&
	    authreq->au_lmpasswd.val[0] == '\0'))) {
		authreq->au_eusername = smb_srm_strdup(sr, "anonymous");
		authreq->au_flags |= SMB_AUTH_ANON;
	} else {
		authreq->au_eusername = authreq->au_username;
	}

	/*
	 * Handle user@domain format.  We need to retain the original
	 * data as this is important in some forms of authentication.
	 */
	if (authreq->au_domain[0] == '\0') {
		buf = smb_srm_strdup(sr, authreq->au_username);
		if ((p = strchr(buf, '@')) != NULL) {
			*p = '\0';
			authreq->au_eusername = buf;
				authreq->au_edomain = p + 1;
		}
	}
}

void
smb_post_session_setup_andx(smb_request_t *sr)
{
	smb_authreq_t	*authreq = &sr->sr_ssetup->ss_authreq;

	DTRACE_SMB_2(op__SessionSetupX__done, smb_request_t *, sr,
	    smb_authreq_t, authreq);

	if (authreq->au_lmpasswd.val != NULL)
		bzero(authreq->au_lmpasswd.val,
		    authreq->au_lmpasswd.len + 1);

	if (authreq->au_ntpasswd.val != NULL)
		bzero(authreq->au_ntpasswd.val,
		    authreq->au_ntpasswd.len + 1);
}

/*
 * If the vcnumber is zero, and enforce_vczero is set, discard any other
 * connections associated with this client.
 *
 * If signing has not already been enabled on this session check to see if
 * it should be enabled.  The first authenticated logon provides the MAC
 * key and sequence numbers for signing all subsequent sessions on the same
 * connection.
 */
smb_sdrc_t
smb_com_session_setup_andx(smb_request_t *sr)
{
	int		rc;
	smb_authreq_t	*authreq = &sr->sr_ssetup->ss_authreq;
	smb_authrsp_t	*authrsp = &sr->sr_ssetup->ss_authrsp;
	boolean_t	extsec = (authreq->au_flags & SMB_AUTH_SECBLOB);

	sr->session->native_os = authreq->au_native_os;
	sr->session->native_lm = authreq->au_native_lm;
	sr->session->vcnumber = authreq->au_vcnumber;
	sr->session->smb_msg_size = authreq->au_maxbufsize;

	if ((sr->session->vcnumber == 0) &&
	    (sr->sr_cfg->skc_enforce_vczero != 0))
		smb_server_reconnection_check(sr->sr_server, sr->session);

	if (sr->session->native_lm == NATIVE_LM_WIN2000)
		authreq->au_capabilities |= CAP_LARGE_FILES |
		    CAP_LARGE_READX | CAP_LARGE_WRITEX;

	sr->session->capabilities = authreq->au_capabilities;

	if (!extsec || (sr->sr_uid == 0) || (sr->sr_uid == SMB_UID_RESERVED)) {
		authreq->au_user_id = smb_session_create_user(sr->session);
		if (authreq->au_user_id == 0) {
			cmn_err(CE_NOTE, "SessionSetup: too many users");
			return (SDRC_ERROR);
		}
		authreq->au_flags |= SMB_AUTH_INIT;
	} else {
		/*
		 * If the user is already LOGGED_ON this is a re-authentication
		 * request. Re-authentication is not supported; logoff user and
		 * return error to force client to establish a new user session.
		 */
		if (smb_session_logoff_user(sr->session, sr->sr_uid) == 0) {
			smb_errcode_seterror(NT_STATUS_LOGON_FAILURE,
			    ERRDOS, ERROR_LOGON_FAILURE);
			return (SDRC_ERROR);
		}
		authreq->au_user_id = sr->sr_uid;
	}

	rc = smb_session_authenticate_user(sr, sr->session, authreq, authrsp);
	if (rc != 0)
		return (SDRC_ERROR);

	if (smb_session_setup_init_signing(sr) != 0) {
		smb_session_authrsp_free(authrsp);
		return (SDRC_ERROR);
	}

	if (extsec)
		rc = smb_session_setup_encode_extended(sr);
	else
		rc = smb_session_setup_encode(sr);

	if (rc == 0)
		sr->sr_uid = authreq->au_user_id;

	smb_session_authrsp_free(authrsp);
	return ((rc == 0) ? SDRC_SUCCESS : SDRC_ERROR);
}

/*
 * If the server requires signing but the client doesn't support it,
 * return NT_STATUS_ACCESS_DENIED.
 *
 * Signing will be initialized if either:
 * - signing is enabled on server and the client requests signing OR
 * - the client requires signing
 */
static int
smb_session_setup_init_signing(smb_request_t *sr)
{
	smb_authreq_t	*authreq = &sr->sr_ssetup->ss_authreq;
	smb_authrsp_t	*authrsp = &sr->sr_ssetup->ss_authrsp;
	char		ipaddr_buf[INET6_ADDRSTRLEN];
	boolean_t	sign = B_FALSE;

	if (!(sr->sr_flags2 & SMB_FLAGS2_SMB_SECURITY_SIGNATURE) &&
	    (sr->sr_cfg->skc_signing_required)) {
		(void) smb_inet_ntop(&sr->session->ipaddr, ipaddr_buf,
		    SMB_IPSTRLEN(sr->session->ipaddr.a_family));
		cmn_err(CE_NOTE,
		    "SmbSessionSetupX: client %s does not support signing",
		    ipaddr_buf);
		smb_errcode_seterror(NT_STATUS_ACCESS_DENIED,
		    ERRDOS, ERROR_ACCESS_DENIED);
		return (-1);
	}

	/* signing already initialized, or not supported */
	if ((sr->session->signing.flags & SMB_SIGNING_ENABLED) ||
	    !(sr->session->signing.flags & SMB_SIGNING_SUPPORTED)) {
		return (0);
	}

	if (((sr->session->secmode & NEGOTIATE_SECURITY_SIGNATURES_ENABLED) &&
	    (sr->sr_flags2 & SMB_FLAGS2_SMB_SECURITY_SIGNATURE)) ||
	    (sr->sr_flags2 & SMB_FLAGS2_SMB_SECURITY_SIGNATURE_REQUIRED)) {
		sign = B_TRUE;
	}

	if (sign && (authrsp->ar_token != NULL) &&
	    (smb_session_key_valid(&authrsp->ar_token->tkn_session_key))) {
		smb_sign_init(sr, &authrsp->ar_token->tkn_session_key,
		    (char *)authreq->au_ntpasswd.val, authreq->au_ntpasswd.len);
	}

	return (0);
}

static int
smb_session_setup_encode(smb_request_t *sr)
{
	smb_authreq_t	*authreq = &sr->sr_ssetup->ss_authreq;
	int		rc;

	rc = smbsr_encode_result(sr, 3, VAR_BCC, "bb.www%uuu",
	    3,
	    sr->andx_com,
	    -1,			/* andx_off */
	    authreq->au_guest,
	    VAR_BCC,
	    sr,
	    sr->sr_cfg->skc_native_os,
	    sr->sr_cfg->skc_native_lm,
	    sr->sr_cfg->skc_nbdomain);

	return (rc);
}

static int
smb_session_setup_encode_extended(smb_request_t *sr)
{
	smb_authreq_t	*authreq = &sr->sr_ssetup->ss_authreq;
	smb_buf16_t	*secblob = &sr->sr_ssetup->ss_authrsp.ar_secblob;

	int		rc;

	rc = smbsr_encode_result(sr, 4, VAR_BCC, "bb.wwww#c%uuu",
	    4,
	    sr->andx_com,
	    -1,			/* andx_off */
	    authreq->au_guest ? 1 : 0,
	    secblob->len,
	    VAR_BCC,
	    secblob->len,
	    secblob->val,
	    sr,
	    sr->sr_cfg->skc_native_os,
	    sr->sr_cfg->skc_native_lm,
	    sr->sr_cfg->skc_nbdomain);

	return (rc);
}

/*
 * Return the appropriate native OS value for the specified native OS name.
 * Samba reports UNIX as its Native OS, which we map to NT 4.0.
 * Windows Vista and later send an empty native OS string.
 * Windows 2000 and later are treated as Windows 2000.
 */
static int
smb_session_setup_native_os(const char *native_os)
{
	static smb_native_t os_table[] = {
		{ NATIVE_OS_WINNT,	"Windows NT 4.0"	},
		{ NATIVE_OS_WINNT,	"Windows NT"		},
		{ NATIVE_OS_WIN95,	"Windows 4.0"		},
		{ NATIVE_OS_WINNT,	"UNIX"			},
		{ NATIVE_OS_MACOS,	"MacOS" 		}
	};

	int i;
	int len;
	const char *name;

	if ((native_os == NULL) ||(*native_os == '\0'))
		return (NATIVE_OS_WIN2000);

	for (i = 0; i < sizeof (os_table)/sizeof (os_table[0]); ++i) {
		name = os_table[i].sn_name;
		len = strlen(name);

		if (smb_strcasecmp(name, native_os, len) == 0)
			return (os_table[i].sn_value);
	}

	return (NATIVE_OS_WIN2000);
}

/*
 * Return the appropriate native LanMan value for the specified native
 * LanMan name.
 * Windows Vista and later send an empty native LM string.
 * Windows 2000 and later are treated as Windows 2000.
 */
static int
smb_session_setup_native_lm(const char *native_lm)
{
	static smb_native_t lm_table[] = {
		{ NATIVE_LM_NT,		"NT LAN Manager 4.0"		},
		{ NATIVE_LM_NT,		"Windows NT"			},
		{ NATIVE_LM_NT,		"Windows 4.0"			},
		{ NATIVE_LM_NT,		"DAVE"				}
	};

	int i;
	int len;
	const char *name;

	if ((native_lm == NULL) || (*native_lm == '\0'))
		return (NATIVE_LM_WIN2000);

	for (i = 0; i < sizeof (lm_table)/sizeof (lm_table[0]); ++i) {
		name = lm_table[i].sn_name;
		len = strlen(name);

		if (smb_strcasecmp(name, native_lm, len) == 0)
			return (lm_table[i].sn_value);
	}

	return (NATIVE_LM_WIN2000);
}
