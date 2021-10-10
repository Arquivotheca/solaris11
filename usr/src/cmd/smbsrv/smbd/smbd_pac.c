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
 * The Privilege Attribute Certificate (PAC) is a structure that conveys
 * authorization-related information provided by domain controllers.
 *
 * The AuthorizationData element AD-IF-RELEVANT ([RFC 4120] section 5.2.6)
 * is the outermost wrapper. It encapsulates another AuthorizationData
 * element of type AD-WIN2K-PAC ([RFC4120] section 7.5.4). Inside this
 * structure is the PACTYPE structure, which serves as a header for the
 * actual PAC elements. Immediately following the PACTYPE header is a
 * series of PAC_INFO_BUFFER structures. These PAC_INFO_BUFFER structures
 * serve as pointers into the contents of the PAC that follows this header.
 */

#include <errno.h>
#include <strings.h>
#include <gssapi_krb5.h>
#include <kerberosv5/krb5.h>
#include <smbsrv/libsmb.h>
#include <smbsrv/libsmbns.h>
#include <smbsrv/libntsvcs.h>
#include <smbsrv/ndl/netlogon.ndl>
#include "smbd.h"

/*
 * PAC_INFO_BUFFER structure defines the type and byte offset to a
 * buffer of the PAC. kerb_validation_info_t, which is NDR encoded,
 * is placed in a buffer, at the offset specified in the offset
 * field of the PAC_INFO_BUFFER of type SMB_PAC_ULTYPE_LOGON.
 *
 * Possible types of data present in the PAC_INFO_BUFFER are the
 * following:
 */
#define	SMB_PAC_ULTYPE_LOGON		0x00000001
#define	SMB_PAC_ULTYPE_CRED		0x00000002
#define	SMB_PAC_ULTYPE_SRV_CHECKSUM	0x00000006
#define	SMB_PAC_ULTYPE_KDC_CHECKSUM	0x00000007
#define	SMB_PAC_ULTYPE_CLIENT_TIX	0x0000000A
#define	SMB_PAC_ULTYPE_DELEGTATION	0x0000000B
#define	SMB_PAC_ULTYPE_UPN_DNS		0x0000000C

#define	SMB_KRB_AUTH_PREFIX		"Kerberos user authentication"

static uint32_t smbd_pac_set(smb_authreq_t *);
static uint32_t smbd_pac_verify(smb_authreq_t *);
static uint32_t smbd_pac_extract_ssnkey(smb_authreq_t *,
    smb_session_key_t *);
static uint32_t smbd_pac_decode_logon_info(smb_authreq_t *,
    ndr_buf_t *, kerb_validation_info_t *);
static uint32_t smbd_pac_verify_srv_chksum(smb_authreq_t *);
static uint32_t smbd_pac_verify_info_bufs(smb_authreq_t *);

uint32_t
smbd_pac_get_userinfo(smb_authreq_t *authreq, ndr_buf_t *ndrbuf,
    smb_session_key_t *skey, kerb_validation_info_t *krb_logon)
{
	uint32_t	rc;
	krb5_error_code	krb5err;
	krb5_context	krb5ctx;

	if ((krb5err = krb5_init_context(&krb5ctx)) != 0) {
		smb_krb5_log_errmsg(authreq->au_krb5ctx, krb5err,
		    "PAC initialization failed: krb5_init_context");
		return (NULL);
	}

	authreq->au_krb5ctx = krb5ctx;

	if ((rc = smbd_pac_set(authreq)) != NT_STATUS_SUCCESS)
		return (NT_STATUS_UNSUCCESSFUL);

	if ((rc = smbd_pac_verify(authreq)) != NT_STATUS_SUCCESS)
		return (NT_STATUS_UNSUCCESSFUL);

	rc = smbd_pac_extract_ssnkey(authreq, skey);
	if (rc != NT_STATUS_SUCCESS)
		return (NT_STATUS_UNSUCCESSFUL);

	rc = smbd_pac_decode_logon_info(authreq, ndrbuf, krb_logon);
	return (rc);
}

/*
 * The PAC comes in an AD-IF-RELEVANT container, which in turn
 * contains another sequence of authorization data elements.
 * This is described in RFC4120.
 *
 * One of the elements in the AD-IF-RELEVANT container will be
 * of KRB5_AUTHDATA_WIN2K_PAC type.  See krb5_pac_parse() for
 * the rest.
 *
 * The PAC field of the smb_authreq_t will be set.
 */
static uint32_t
smbd_pac_set(smb_authreq_t *authreq)
{
	OM_uint32	major_stat, minor_stat;
	gss_buffer_desc	ad_buf;
	krb5_error_code	krb5err;
	krb5_authdata	ad_if_relevant_container;
	krb5_authdata	**ad_win2k_pac_authdata = NULL;
	krb5_pac	pac;
	int		i;
	boolean_t	found;
	uint32_t	rc = NT_STATUS_UNSUCCESSFUL;

	major_stat = gsskrb5_extract_authz_data_from_sec_context(&minor_stat,
	    authreq->au_user->u_gss_ctx, KRB5_AUTHDATA_IF_RELEVANT, &ad_buf);

	if (GSS_ERROR(major_stat)) {
		smbd_krb5_gsslog("Kerberos user authentication failed"
		    " while extracting authorization data", authreq,
		    GSS_C_NULL_OID, major_stat, minor_stat);
		return (rc);
	}

	bzero(&ad_if_relevant_container, sizeof (krb5_authdata));
	ad_if_relevant_container.ad_type = KRB5_AUTHDATA_IF_RELEVANT;
	ad_if_relevant_container.length = ad_buf.length;
	ad_if_relevant_container.contents = ad_buf.value;


	krb5err = krb5_decode_authdata_container(authreq->au_krb5ctx,
	    KRB5_AUTHDATA_IF_RELEVANT, &ad_if_relevant_container,
	    &ad_win2k_pac_authdata);

	if (krb5err) {
		smb_krb5_log_errmsg(authreq->au_krb5ctx, krb5err,
		    "%s failed while decoding KRB5_AUTHDATA_IF_RELEVANT",
		    SMB_KRB_AUTH_PREFIX);
		(void) gss_release_buffer(&minor_stat, &ad_buf);
		return (rc);
	}

	for (i = 0, found = B_FALSE; ad_win2k_pac_authdata[i] != NULL; i++) {
		if (ad_win2k_pac_authdata[i]->ad_type ==
		    KRB5_AUTHDATA_WIN2K_PAC) {
			found = B_TRUE;
			break;
		}
	}

	if (!found) {
		syslog(LOG_ERR, "%s failed due to an unexpected PAC: "
		    "missing KRB5_AUTHDATA_WIN2k_PAC", SMB_KRB_AUTH_PREFIX);
		goto cleanup;
	}

	krb5err = krb5_pac_parse(authreq->au_krb5ctx,
	    ad_win2k_pac_authdata[i]->contents,
	    ad_win2k_pac_authdata[i]->length, &pac);

	if (krb5err) {
		smb_krb5_log_errmsg(authreq->au_krb5ctx, krb5err,
		    "%s failed while parsing PAC", SMB_KRB_AUTH_PREFIX);
		goto cleanup;
	}

	authreq->au_pac = pac;
	rc = NT_STATUS_SUCCESS;

cleanup:
	(void) gss_release_buffer(&minor_stat, &ad_buf);
	krb5_free_authdata(authreq->au_krb5ctx, ad_win2k_pac_authdata);

	return (rc);
}

/*
 * Verify Server Signature
 *
 * The PAC bears "signatures" (keyed checksums).  One will be in the
 * ticket's target principal's long-term key.  Sort of like
 * AD-KDC-ISSUED, but not quite.
 */
static uint32_t
smbd_pac_verify_srv_chksum(smb_authreq_t *authreq)
{
	OM_uint32 major_stat, minor_stat;
	gss_name_t target = NULL;
	gss_buffer_desc namebuf;
	gss_OID nametype = GSS_C_NULL_OID;
	krb5_principal princ = NULL;
	krb5_keyblock *srv_key = NULL;
	int i;
	boolean_t found;
	uint32_t rc = NT_STATUS_UNSUCCESSFUL;
	krb5_error_code krb5err;

	/* MS-PAC: signature types */
	krb5_enctype enctypes[] = {
	    ENCTYPE_ARCFOUR_HMAC,
	    ENCTYPE_AES128_CTS_HMAC_SHA1_96,
	    ENCTYPE_AES256_CTS_HMAC_SHA1_96
	};

	major_stat = gss_inquire_context(&minor_stat,
	    authreq->au_user->u_gss_ctx, NULL, &target, NULL, NULL, NULL, NULL,
	    NULL);

	if (target == NULL) {
		if (GSS_ERROR(major_stat))
			smbd_krb5_gsslog("PAC verification: unknown SPN",
			    authreq, GSS_C_NULL_OID, major_stat, minor_stat);
		return (rc);
	}

	major_stat = gss_display_name(&minor_stat, target, &namebuf, &nametype);
	if (GSS_ERROR(major_stat)) {
		smbd_krb5_gsslog("PAC verification: unable to display SPN",
		    authreq, GSS_C_NULL_OID, major_stat, minor_stat);
		(void) gss_release_name(&minor_stat, &target);
		return (rc);
	}

	/*
	 * We've used the GSS-API, but here we need a krb5_principal,
	 * not a gss_name_t, because we're going to look for a matching
	 * keytab entry, and the GSS-API doesn't give us direct access
	 * to the keytab.
	 */
	if (krb5_parse_name(authreq->au_krb5ctx, namebuf.value, &princ) != 0) {
		(void) gss_release_buffer(&minor_stat, &namebuf);
		(void) gss_release_name(&minor_stat, &target);
		return (rc);
	}

	for (i = 0, found = B_FALSE;
	    i < (sizeof (enctypes) / sizeof (krb5_enctype));
	    i++) {
		krb5err = krb5_kt_read_service_key(authreq->au_krb5ctx, NULL,
		    princ, 0, enctypes[i], &srv_key);
		if (krb5err) {
			smb_krb5_log_errmsg(authreq->au_krb5ctx, krb5err,
			    "PAC verification: unable to read service keys");
			continue;
		}

		if ((krb5err = krb5_pac_verify(authreq->au_krb5ctx,
		    authreq->au_pac, NULL, NULL, srv_key, NULL)) == 0) {
			found = B_TRUE;
			krb5_free_keyblock(authreq->au_krb5ctx, srv_key);
			break;
		}

		krb5_free_keyblock(authreq->au_krb5ctx, srv_key);
		srv_key = NULL;
	}

	if (!found) {
		smb_krb5_log_errmsg(authreq->au_krb5ctx, krb5err,
		    "PAC verification: unable to verify server's signature");
	} else {
		rc = NT_STATUS_SUCCESS;
	}

	krb5_free_principal(authreq->au_krb5ctx, princ);
	(void) gss_release_buffer(&minor_stat, &namebuf);
	(void) gss_release_name(&minor_stat, &target);

	return (rc);
}

/*
 * Verify the existence of the required PAC_BUFFER_INFOs
 */
static uint32_t
smbd_pac_verify_info_bufs(smb_authreq_t *authreq)
{
	krb5_ui_4 *buf_types = NULL;
	size_t buf_cnt;
	krb5_error_code krb5err;
	int i, j;
	boolean_t found;

	const uint32_t required_types[] = {
	    SMB_PAC_ULTYPE_LOGON, SMB_PAC_ULTYPE_SRV_CHECKSUM,
	    SMB_PAC_ULTYPE_KDC_CHECKSUM, SMB_PAC_ULTYPE_CLIENT_TIX
	};

	if ((krb5err = krb5_pac_get_types(authreq->au_krb5ctx, authreq->au_pac,
	    &buf_cnt, &buf_types)) != 0) {
		smb_krb5_log_errmsg(authreq->au_krb5ctx, krb5err,
		    "PAC verification: unable to obtain buffer types");
		return (NT_STATUS_UNSUCCESSFUL);
	}

	for (i = 0; i < sizeof (required_types) / sizeof (uint32_t); i++) {
		for (found = B_FALSE, j = 0; j < buf_cnt; j++) {
			if (buf_types[j] == required_types[i]) {
				found = B_TRUE;
				break;
			}
		}

		if (!found) {
			free(buf_types);
			syslog(LOG_ERR, "PAC verification: "
			    "missing the required PAC_INFO_BUFFER (type: %d)",
			    required_types[i]);
			return (NT_STATUS_UNSUCCESSFUL);
		}
	}

	free(buf_types);
	return (NT_STATUS_SUCCESS);
}

/*
 * Verify PAC
 */
static uint32_t
smbd_pac_verify(smb_authreq_t *authreq)
{
	uint32_t rc;

	/* verify server checksum */
	if ((rc = smbd_pac_verify_srv_chksum(authreq)) != NT_STATUS_SUCCESS)
		return (rc);

	/* verify the MUST-HAVE PAC_BUFFER_INFO */
	return (smbd_pac_verify_info_bufs(authreq));
}

/*
 * The extracted session key is the MAC key that will be used for verifying
 * the SIGNATURE of the incoming SMB messages and signing the outgoing SMB
 * messages.  The associated encryption type is skipped here because it is
 * not needed. For the session key that is used for signing when Kerberos is
 * used, see [MS-KILE] Cryptographic Material section. For more info on
 * SMB signing, see [MS-SMB] Message Signing Example section.
 */
static uint32_t
smbd_pac_extract_ssnkey(smb_authreq_t *authreq, smb_session_key_t *session_key)
{
	OM_uint32 major_status, minor_status;
	gss_buffer_set_t data_set = GSS_C_NO_BUFFER_SET;
	uint32_t rc = NT_STATUS_UNSUCCESSFUL;

	if (session_key == NULL)
		return (NT_STATUS_INVALID_PARAMETER);

	bzero(session_key, sizeof (smb_session_key_t));
	major_status = gss_inquire_sec_context_by_oid(&minor_status,
	    authreq->au_user->u_gss_ctx, GSS_C_INQ_SSPI_SESSION_KEY, &data_set);

	if (GSS_ERROR(major_status)) {
		smbd_krb5_gsslog("Kerberos user authentication: "
		    "unable to obtain session key", authreq, GSS_C_NULL_OID,
		    major_status, minor_status);
		return (rc);
	}

	/*
	 * The data set returned by the GSS inquiry function is composed
	 * of two elements:
	 * 1st element: the actual session key value and length
	 * 2nd element: the associated key encryption type by OID.
	 */
	if (data_set == GSS_C_NO_BUFFER_SET ||
	    data_set->count == 0 ||
	    data_set->elements[0].length == 0 ||
	    data_set->elements[0].value == NULL) {
		syslog(LOG_ERR, "%s: no session key", SMB_KRB_AUTH_PREFIX);
		return (NT_STATUS_UNSUCCESSFUL);
	}

	if ((rc = smb_session_key_create(session_key,
	    data_set->elements[0].value, data_set->elements[0].length)) !=
	    NT_STATUS_SUCCESS)
		syslog(LOG_ERR, "%s: unable to create session key: %m",
		    SMB_KRB_AUTH_PREFIX);

	(void) gss_release_buffer_set(&minor_status, &data_set);
	return (rc);
}

static uint32_t
smbd_pac_decode_logon_info(smb_authreq_t *authreq, ndr_buf_t *ndrbuf,
    kerb_validation_info_t *info)
{
	krb5_data pac_logon_info;
	krb5_error_code krb5err;
	uint32_t rc = NT_STATUS_UNSUCCESSFUL;

	bzero(&pac_logon_info, sizeof (krb5_data));
	krb5err = krb5_pac_get_buffer(authreq->au_krb5ctx, authreq->au_pac,
	    SMB_PAC_ULTYPE_LOGON, &pac_logon_info);

	if (krb5err) {
		smb_krb5_log_errmsg(authreq->au_krb5ctx, krb5err,
		    "PAC decoding: unable to extract logon info from PAC");
		return (rc);
	}


	if (pac_logon_info.length == 0 || pac_logon_info.data == NULL) {
		syslog(LOG_ERR, "PAC decoding: no logon info");
		return (rc);
	}

	rc = ndr_buf_decode(ndrbuf, NDR_PTYPE_PAC,
	    KERB_OPNUM_VALIDATION_INFO, pac_logon_info.data,
	    pac_logon_info.length, info);

	if (rc != NDR_DRC_OK) {
		syslog(LOG_ERR,
		    "PAC decoding: unable to extract kerb_validation_info: %d",
		    rc);
		rc = NT_STATUS_UNSUCCESSFUL;
	} else {
		rc = NT_STATUS_SUCCESS;
	}

cleanup:
	free(pac_logon_info.data);
	return (rc);
}
