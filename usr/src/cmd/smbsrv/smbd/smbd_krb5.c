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
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Kerberos Security Support Provider provides Kerberos authentication for AD
 * users.
 */

#include <syslog.h>
#include <strings.h>
#include <kerberosv5/krb5.h>
#include <smbsrv/libsmb.h>
#include <smbsrv/libntsvcs.h>
#include "smbd.h"

static uint32_t smbd_krb5_auth(smb_authreq_t *, smb_authrsp_t *);

smbd_mech_t krb5 = {
	SMBD_MECH_KRB,		/* m_type */
	smbd_mech_preauth_noop,	/* m_pre_auth */
	smbd_krb5_auth,		/* m_auth */
	smbd_mech_postauth_noop	/* m_post_auth */
};

static uint32_t smbd_krb5_accept_gssctx(smb_authreq_t *, smb_authrsp_t *);
static char *smbd_krb5_gendiag(smb_authreq_t *, char *);

/*
 * Windows Kerberos application servers don't comply with RFC 4120. We've
 * seen cases where Windows servers return KRB_AP_ERR_MODIFIED where RFC 4120
 * says otherwise. The smb/server needs to return KRB_AP_ERR_MODIFIED
 * accordingly for MS interoperability.
 *
 * Set a private environment variable named "MS_INTEROP" to instruct the
 * mech_krb5 to return KRB_AP_ERR_MODIFIED as opposed to the krb5 error code
 * specified in the spec. Failing to set the above environment variable will
 * result in no error code mappings and no Kerberos error blob being sent to
 * the client. As a result, Windows clients cannot auto recover from an error
 * and end-users are required to resolve the reported error manually.
 */
void
smbd_krb5_init(void)
{
	smbd_mech_register(&krb5);
	if (putenv("MS_INTEROP=1") != 0)
		syslog(LOG_NOTICE,
		    "krbssp: MS_INTEROP environment variable not defined");
}

/*
 * The GSS error messages will be prefixed with either [domain\user] or
 * [client IP address] for diagnostic purposes.
 *
 * This is a wrapper around smbd_gss_log() to provide additional guidance
 * to end users in some of the common Kerberos error scenarios.
 */
void
smbd_krb5_gsslog(char *msg_prefix, smb_authreq_t *authreq, gss_OID mech_type,
    OM_uint32 major, OM_uint32 minor)
{
	krb5_error_code err = minor;
	char *prefix = smbd_krb5_gendiag(authreq, msg_prefix);

	smbd_gss_log(prefix, mech_type, major, minor);
	free(prefix);

	switch (err) {
	case KRB5KRB_AP_ERR_SKEW:
		syslog(LOG_NOTICE, "Please make sure the client system clock "
		    "is in sync with the SMB server if the user "
		    " authentication issue persists.");
		break;

	case KRB5KRB_AP_ERR_TKT_NYV:
		syslog(LOG_NOTICE, "Please make sure the system clock is in "
		    "sync with the domain controllers if the user "
		    "authentication issue persists.");
		break;

	case KRB5KRB_AP_ERR_BAD_INTEGRITY:
	case KRB5KRB_AP_ERR_NOKEY:
	case KRB5KRB_AP_ERR_BADKEYVER:
		syslog(LOG_NOTICE, "Please make sure the client doesn't use"
		    " an old service ticket while connecting to the SMB "
		    "server.");
		syslog(LOG_NOTICE, "Please log off from the client and log in"
		    " again to remove any stale ticket that might have cached"
		    " on the client if the authentication issue persists.");
		break;
	}
}

/*
 * The authreq->au_user will be locked for the duration of the Kerberos
 * authentication process where the GSS context held in the au_user will be
 * updated.
 */
uint32_t
smbd_krb5_auth(smb_authreq_t *authreq, smb_authrsp_t *authrsp)
{
	uint32_t		rc;
	kerb_validation_info_t	krb_logon;
	ndr_buf_t		*ndrbuf;
	smb_session_key_t	ssnkey = {NULL, 0};
	smb_token_t		*token;
	smb_authinfo_t		authinfo;

	assert(authreq->au_session != NULL);
	assert(authreq->au_user != NULL);

	bzero(&krb_logon, sizeof (kerb_validation_info_t));
	bzero(&authinfo, sizeof (smb_authinfo_t));

	if ((token = smbd_token_alloc()) == NULL)
		return (NT_STATUS_NO_MEMORY);

	if ((ndrbuf = netr_ndrbuf_init()) == NULL) {
		syslog(LOG_DEBUG, "krbssp: ndr_buf_init failed");
		rc =  NT_STATUS_UNSUCCESSFUL;
		smbd_token_free(token);
		return (rc);
	}

	(void) mutex_lock(&authreq->au_user->u_mtx);
	rc = smbd_krb5_accept_gssctx(authreq, authrsp);
	if (rc != NT_STATUS_SUCCESS) {
		smbd_token_free(token);
		netr_ndrbuf_fini(ndrbuf);
		(void) mutex_unlock(&authreq->au_user->u_mtx);
		return (rc);
	}

	rc = smbd_pac_get_userinfo(authreq, ndrbuf, &ssnkey, &krb_logon);
	if (rc != NT_STATUS_SUCCESS) {
		smbd_token_free(token);
		netr_ndrbuf_fini(ndrbuf);
		smb_session_key_destroy(&ssnkey);
		(void) mutex_unlock(&authreq->au_user->u_mtx);
		return (rc);
	}

	(void) mutex_unlock(&authreq->au_user->u_mtx);

	if (smb_authinfo_setup_krb(&krb_logon, &ssnkey, authreq, &authinfo)) {
		rc = smbd_token_setup_domain(token, &authinfo);
		smb_authinfo_free(&authinfo);
	} else {
		rc = NT_STATUS_NO_MEMORY;
	}

	netr_ndrbuf_fini(ndrbuf);

	if (rc == NT_STATUS_SUCCESS)
		authrsp->ar_token = token;
	else
		smbd_token_free(token);


	return (rc);
}

/*
 * Accept a GSS security context initiated by clients.
 *
 * Upon success, the following will be set/updated:
 * 1) outgoing security blob (i.e. the GSS context token) of the authrsp
 * 2) username, effective username, domain, and effective domain of the authreq
 * 3) GSS context of the associated smbd_user
 */
static uint32_t
smbd_krb5_accept_gssctx(smb_authreq_t *authreq, smb_authrsp_t *authrsp)
{
	OM_uint32	major, minor, minor2, ret_flags;
	gss_OID		mech_type;
	gss_buffer_desc	namebuf;
	gss_name_t	srcname = NULL;
	gss_OID		nametype = GSS_C_NULL_OID;
	gss_buffer_desc	gss_itoken, gss_otoken;
	char		*user, *domain, *p;

	/* validate parameters */
	if ((authreq->au_secblob.len == 0) ||
	    (authreq->au_secblob.val == NULL) ||
	    (*authreq->au_secblob.val == '\0'))
		return (NT_STATUS_INVALID_PARAMETER);

	mech_type = GSS_C_NULL_OID;
	gss_itoken.length = authreq->au_secblob.len;
	gss_itoken.value = authreq->au_secblob.val;

	bzero(&gss_otoken, sizeof (gss_buffer_desc));
	bzero(&namebuf, sizeof (gss_buffer_desc));

	smbd_gss_lock();
	major = gss_accept_sec_context(&minor, &authreq->au_user->u_gss_ctx,
	    GSS_C_NO_CREDENTIAL, &gss_itoken, GSS_C_NO_CHANNEL_BINDINGS,
	    &srcname, &mech_type, &gss_otoken, &ret_flags, NULL, NULL);
	smbd_gss_unlock();

	authrsp->ar_secblob.len = (uint16_t)gss_otoken.length;
	authrsp->ar_secblob.val = (uint8_t *)gss_otoken.value;

	if (GSS_ERROR(major))
		smbd_krb5_gsslog("krbssp: user authentication failed",
		    authreq, mech_type, major, minor);

	if ((srcname != NULL) &&
	    (gss_display_name(&minor2, srcname, &namebuf, &nametype) ==
	    GSS_S_COMPLETE)) {
		/* The namebuf contains string in user@fqdn format. */
		if ((user = strdup(namebuf.value)) == NULL) {
			(void) gss_release_name(&minor2, &srcname);
			return (NT_STATUS_NO_MEMORY);
		}

		free(authreq->au_eusername);
		authreq->au_eusername = user;

		if ((p = strchr(authreq->au_eusername, '@')) != NULL) {
			*p = '\0';

			if ((domain = strdup(p + 1)) == NULL) {
				(void) gss_release_buffer(&minor2, &namebuf);
				(void) gss_release_name(&minor2, &srcname);
				return (NT_STATUS_NO_MEMORY);
			}

			free(authreq->au_edomain);
			authreq->au_edomain = domain;
		}

		if (authreq->au_eusername != NULL) {
			free(authreq->au_username);
			authreq->au_username = strdup(authreq->au_eusername);
		}

		if (authreq->au_edomain != NULL) {
			free(authreq->au_domain);
			authreq->au_domain = strdup(authreq->au_edomain);
		}

		(void) gss_release_buffer(&minor2, &namebuf);
		(void) gss_release_name(&minor2, &srcname);
	}

	switch (major) {
	case GSS_S_COMPLETE:
		return (NT_STATUS_SUCCESS);

	case GSS_S_CONTINUE_NEEDED:
		smbd_krb5_gsslog("krbssp: authentication error detected",
		    authreq, mech_type, major, minor);

		if (gss_otoken.length > 0)
			return (NT_STATUS_MORE_PROCESSING_REQUIRED);
		else
			return (NT_STATUS_WRONG_PASSWORD);

	default:
			return (NT_STATUS_WRONG_PASSWORD);
	}
}

/*
 * Return diagnostic information such as domain name and username if they are
 * available.  Otherwise, returns the IP address of the client from which
 * the user attempts to log on.
 *
 * The memory for the returned diagnostic info must be freed by the caller.
 */
static char *
smbd_krb5_gendiag(smb_authreq_t *authreq, char *msg_prefix)
{
	char *info = NULL;
	smb_inaddr_t *ipaddr;
	char ipstr[INET6_ADDRSTRLEN];

	if ((authreq->au_eusername != NULL) &&
	    (*authreq->au_eusername != '\0') &&
	    (authreq->au_edomain != NULL) &&
	    (*authreq->au_edomain != '\0')) {
		(void) asprintf(&info, "[%s\\%s] %s", authreq->au_edomain,
		    authreq->au_eusername, msg_prefix);
		return (info);
	}

	ipaddr = &authreq->au_clnt_ipaddr;
	if (!smb_inet_iszero(ipaddr)) {
		(void) smb_inet_ntop(ipaddr, ipstr,
		    SMB_IPSTRLEN(ipaddr->a_family));
		(void) asprintf(&info, "[%s] %s", ipstr, msg_prefix);
	} else {
		(void) asprintf(&info, "%s", msg_prefix);
	}

	return (info);
}
