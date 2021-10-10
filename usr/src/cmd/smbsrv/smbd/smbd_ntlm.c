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
 * NTLMSSP - NTLM Security Support Provider
 * 1) Provides NTLM authentication when extended security is not negotiated.
 * 2) Supports NTLMSSP authentication protocol when extended security is
 * negotiated. NTLMSSP messages are typically embedded in GSS/SPNEGO messages.
 * It's been seen that Windows clients send raw NTLMSSP messages when the
 * contacted server is in workgroup mode. This SSP module handles both raw
 * NTLMSSP messages and NTLMSSP messages that are embedded in SPNEGO messages.
 */

#include <sys/types.h>
#include <errno.h>
#include <synch.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <syslog.h>
#include <fcntl.h>
#include <pwd.h>
#include <nss_dbdefs.h>
#include <sys/idmap.h>
#include <smb/spnego.h>
#include <smb/ntlmssp.h>
#include <smbsrv/msgbuf.h>
#include "smbd.h"

#define	SMBD_NTLM_AVPAIR_NUM		5

static uint32_t smbd_ntlm_auth_dispatch(smb_authreq_t *, smb_authrsp_t *);
static uint32_t smbd_ntlm_msg_dispatch(smb_authreq_t *, smb_authrsp_t *);

/* NTLM mechanism for non extended security authentication */
smbd_mech_t ntlm = {
	SMBD_MECH_NTLM,			/* m_type */
	smbd_mech_preauth_noop,		/* m_pre_auth */
	smbd_ntlm_auth_dispatch,	/* m_auth */
	smbd_mech_postauth_noop		/* m_post_auth */
};

/* Raw NTLMSSP mechanism */
smbd_mech_t raw_ntlmssp = {
	SMBD_MECH_RAW_NTLMSSP,		/* m_type */
	smbd_mech_preauth_noop,		/* m_pre_auth */
	smbd_ntlm_msg_dispatch,		/* m_auth */
	smbd_mech_postauth_noop		/* m_post_auth */
};

/* NTLMSSP mechansim - NTLMSSP with SPNEGO */
smbd_mech_t ntlmssp = {
	SMBD_MECH_NTLMSSP,		/* m_type */
	smbd_ntlm_spng2mech,		/* m_pre_auth */
	smbd_ntlm_msg_dispatch,		/* m_auth */
	smbd_ntlm_mech2spng		/* m_post_auth */
};

typedef struct smbd_ntlm_msg {
	uint32_t msg_type;
	union {
		smbd_ntlm_neg_msg_t	msg_neg;
		smbd_ntlm_challenge_msg_t msg_challenge;
		smbd_ntlm_auth_msg_t	msg_auth;
	} msg_u;
} smbd_ntlm_msg_t;

#define	ntlm_neg		msg_u.msg_neg
#define	ntlm_challenge		msg_u.msg_challenge
#define	ntlm_auth		msg_u.msg_auth

static uint32_t smbd_ntlm_negotiate(const smb_authreq_t *, smbd_ntlm_msg_t *);
static uint32_t smbd_ntlm_challenge(const smb_authreq_t *, const
    smbd_ntlm_msg_t *, smb_authrsp_t *);
static uint32_t smbd_ntlm_authenticate(const smb_authreq_t *,
    smbd_ntlm_msg_t *);
static void smbd_ntlm_msg_free(smbd_ntlm_msg_t *);

static uint32_t smbd_ntlm_challenge_create(smbd_ntlm_msg_t *,
    const smb_buf32_t *, uint32_t);
static int smbd_ntlm_challenge_encode(smbd_ntlm_challenge_msg_t *, uint8_t **);
static int smbd_ntlm_encode_bufinfo(smb_msgbuf_t *,
    const smbd_ntlm_bufinfo_t *);
static int smbd_ntlm_decode_bufinfo(smb_msgbuf_t *, smbd_ntlm_bufinfo_t *);
static int smbd_ntlm_getbuf(smb_msgbuf_t *, const smbd_ntlm_bufinfo_t *,
    uint8_t **, smbd_ntlm_bufconv_t);
static smbd_ntlm_avpair_t *smbd_ntlm_avpairs_create(void);
static void smbd_ntlm_setup_avpair_unicode(smbd_ntlm_avpair_t *, int,
    const char *);
void smbd_ntlm_set_target_info(smbd_ntlm_challenge_msg_t *);
static uint32_t smbd_ntlm_update_userinfo(smb_authreq_t *,
    smbd_ntlm_auth_msg_t *);
static boolean_t smbd_ntlm_is_anon(const smbd_ntlm_auth_msg_t *);

typedef void (*smbd_ntlm_authop_t)(smb_authreq_t *, smb_authrsp_t *);
static void smbd_ntlm_auth_domain(smb_authreq_t *, smb_authrsp_t *);
static void smbd_ntlm_auth_local(smb_authreq_t *, smb_authrsp_t *);
static void smbd_ntlm_auth_guest(smb_authreq_t *, smb_authrsp_t *);
static void smbd_ntlm_auth_anon(smb_authreq_t *, smb_authrsp_t *);

static int smbd_guest_account(smb_account_t *);
static const smb_account_t *smbd_generate_guest_account(void);

/*
 * Initialization for NTLM Security Support Provider
 * Registers NTLM, raw NTLMSSP and NTLMSSP mechanisms.
 */
void
smbd_ntlm_init(void)
{
	smbd_mech_register(&ntlm);
	smbd_mech_register(&raw_ntlmssp);
	smbd_mech_register(&ntlmssp);
}

/*
 * NTLM mechanism token embedded in the SPNEGO message will be
 * extracted here. authreq->au_secblob is modified in place.
 */
uint32_t
smbd_ntlm_spng2mech(smb_authreq_t *authreq)
{
	smb_buf16_t mech_token;

	if (smbd_mech_secblob2token(&authreq->au_secblob,
	    &mech_token) != NT_STATUS_SUCCESS) {
		return (NT_STATUS_INVALID_PARAMETER);
	}

	free(authreq->au_secblob.val);
	authreq->au_secblob.val = mech_token.val;
	authreq->au_secblob.len = mech_token.len;

	return (NT_STATUS_SUCCESS);
}

/*
 * Convert NTLMSSP mechanism token to SPNEGO target token.
 * The ar_secblob of the smb_authrsp_t structure will be updated
 * in place.
 */
uint32_t
smbd_ntlm_mech2spng(smb_authreq_t *authreq, smb_authrsp_t *authrsp)
{
	SPNEGO_TOKEN_HANDLE	stok_out;
	int			rc;
	unsigned long		len;
	smb_buf16_t		mech_token;

	bzero(&stok_out, sizeof (SPNEGO_TOKEN_HANDLE));
	bcopy(&authrsp->ar_secblob, &mech_token, sizeof (smb_buf16_t));
	bzero(&authrsp->ar_secblob, sizeof (smb_buf16_t));

	switch (authreq->au_ntlm_msgtype) {
	case NTLMSSP_MSGTYPE_NEGOTIATE:
		rc = spnegoCreateNegTokenTarg(
		    spnego_mech_oid_NTLMSSP,
		    spnego_negresult_incomplete,
		    mech_token.val,
		    mech_token.len,
		    NULL, 0, &stok_out);
		break;

	case NTLMSSP_MSGTYPE_AUTHENTICATE:
		/*
		 * To conform with Windows, a security blob should not
		 * be returned upon failure.
		 */
		if (authrsp->ar_status != NT_STATUS_SUCCESS) {
			if (mech_token.val != NULL)
				free(mech_token.val);

			return (NT_STATUS_SUCCESS);
		}

		rc = spnegoCreateNegTokenTarg(
		    spnego_mech_oid_NotUsed,
		    spnego_negresult_success,
		    NULL, 0, NULL, 0, &stok_out);

		break;
	}

	if (rc != SPNEGO_E_SUCCESS) {
		if (mech_token.val != NULL)
			free(mech_token.val);
		return (NT_STATUS_INTERNAL_ERROR);
	}

	/*
	 * Copy binary from stok_out to caller_out
	 * Two calls: get the size, get the data.
	 */
	rc = spnegoTokenGetBinary(stok_out, NULL, &len);
	if (rc != SPNEGO_E_BUFFER_TOO_SMALL) {
		if (mech_token.val != NULL)
			free(mech_token.val);
		spnegoFreeData(stok_out);
		return (NT_STATUS_INTERNAL_ERROR);
	}

	authrsp->ar_secblob.val = calloc(1, len);

	if (authrsp->ar_secblob.val == NULL) {
		if (mech_token.val != NULL)
			free(mech_token.val);
		spnegoFreeData(stok_out);
		return (NT_STATUS_INTERNAL_ERROR);
	}

	rc = spnegoTokenGetBinary(stok_out,
	    authrsp->ar_secblob.val, &len);
	if (rc) {
		if (mech_token.val != NULL)
			free(mech_token.val);
		spnegoFreeData(stok_out);
		free(authrsp->ar_secblob.val);
		authrsp->ar_secblob.val = NULL;
		return (NT_STATUS_INTERNAL_ERROR);
	}

	authrsp->ar_secblob.len = len;
	spnegoFreeData(stok_out);

	if (mech_token.val != NULL)
		free(mech_token.val);

	return (NT_STATUS_SUCCESS);
}

static uint32_t
smbd_ntlm_msg_dispatch(smb_authreq_t *authreq, smb_authrsp_t *authrsp)
{
	smbd_ntlm_msg_t	msg;
	uint8_t		*ptr;
	uint32_t	status;

	ptr = authreq->au_secblob.val + strlen(NTLMSSP_SIGNATURE)
	    + 1;
	msg.msg_type = LE_IN32(ptr);
	authreq->au_ntlm_msgtype = msg.msg_type;

	switch (msg.msg_type) {
	case NTLMSSP_MSGTYPE_NEGOTIATE:
		status = smbd_ntlm_negotiate(authreq, &msg);
		if (status == NT_STATUS_SUCCESS)
			status = smbd_ntlm_challenge(authreq, &msg, authrsp);

		smbd_ntlm_msg_free(&msg);
		break;

	case NTLMSSP_MSGTYPE_AUTHENTICATE:
		status = smbd_ntlm_authenticate(authreq, &msg);
		if (status == NT_STATUS_SUCCESS) {
			status =
			    smbd_ntlm_update_userinfo(authreq, &msg.ntlm_auth);
			if (status == NT_STATUS_SUCCESS)
				status = smbd_ntlm_auth_dispatch(authreq,
				    authrsp);
		}

		smbd_ntlm_msg_free(&msg);
		break;

	default:
		syslog(LOG_ERR, "ntlmssp: Unexpected message type (0x%x)",
		    msg.msg_type);
		return (NT_STATUS_INVALID_PARAMETER);
	}

	return (status);
}

/*
 * Decode NTLMSSP Type 1 (aka Negotiate) message.
 */
static uint32_t
smbd_ntlm_negotiate(const smb_authreq_t *authreq, smbd_ntlm_msg_t *msg)
{
	smbd_ntlm_neg_msg_t	*neg_msg;
	smbd_ntlm_neg_hdr_t	*hdr;
	smb_msgbuf_t		*mb;

	assert(msg->msg_type == NTLMSSP_MSGTYPE_NEGOTIATE);
	neg_msg = &msg->ntlm_neg;
	hdr = &neg_msg->nm_hdr;
	mb = &neg_msg->nm_mb;

	bzero(neg_msg, sizeof (smbd_ntlm_neg_msg_t));
	smb_msgbuf_init(mb, authreq->au_secblob.val,
	    authreq->au_secblob.len, 0);

	/*
	 * Decode header.
	 */
	if (smb_msgbuf_decode(mb, "sll", &hdr->nh_signature, &hdr->nh_type,
	    &hdr->nh_negflags) < 0)
		return (NT_STATUS_INTERNAL_ERROR);

	if (smbd_ntlm_decode_bufinfo(mb, &hdr->nh_domain) < 0)
		return (NT_STATUS_INTERNAL_ERROR);

	if (smbd_ntlm_decode_bufinfo(mb, &hdr->nh_wkst) < 0)
		return (NT_STATUS_INTERNAL_ERROR);

	/*
	 * Now go get the payload section for each of the
	 * smbd_ntlm_bufinfo fields in the header.  Note
	 * that the payload parts may appear in any order,
	 * and smbd_ntlm_msg_getbuf() takes care to locate
	 * payload info. using the offsets already parsed.
	 */
	if (smbd_ntlm_getbuf(mb, &hdr->nh_domain,
	    (uint8_t **)&neg_msg->nm_domain, BUFCONV_ASCII_STRING) < 0)
		return (NT_STATUS_INTERNAL_ERROR);

	if (smbd_ntlm_getbuf(mb, &hdr->nh_wkst, (uint8_t **)&neg_msg->nm_wkst,
	    BUFCONV_ASCII_STRING) < 0)
		return (NT_STATUS_INTERNAL_ERROR);

	return (NT_STATUS_SUCCESS);
}

/*
 * Encode NTLMSSP Type 2 (challenge) message.
 */
static uint32_t
smbd_ntlm_challenge(const smb_authreq_t *authreq, const smbd_ntlm_msg_t *in,
    smb_authrsp_t *authrsp)
{
	smbd_ntlm_msg_t	out;
	uint32_t	rc;

	assert(in->msg_type == NTLMSSP_MSGTYPE_NEGOTIATE);

	if ((rc = smbd_ntlm_challenge_create(&out, &authreq->au_challenge_key,
	    in->ntlm_neg.nm_hdr.nh_negflags)) == NT_STATUS_SUCCESS) {
		authrsp->ar_secblob.len = smbd_ntlm_challenge_encode(
		    &out.ntlm_challenge, (uint8_t **)&authrsp->ar_secblob.val);
		rc = NT_STATUS_MORE_PROCESSING_REQUIRED;
	}

	smbd_ntlm_msg_free(&out);
	return (rc);
}

/*
 * Decode NTLMSSP Type 3 (authenticate) message.
 */
static uint32_t
smbd_ntlm_authenticate(const smb_authreq_t *authreq, smbd_ntlm_msg_t *msg)
{
	smbd_ntlm_auth_msg_t	*auth_msg;
	smbd_ntlm_auth_hdr_t	*hdr;
	smb_msgbuf_t		*mb;

	assert(msg->msg_type == NTLMSSP_MSGTYPE_AUTHENTICATE);
	auth_msg = &msg->ntlm_auth;
	hdr = &auth_msg->am_hdr;
	mb = &auth_msg->am_mb;

	bzero(auth_msg, sizeof (smbd_ntlm_auth_msg_t));
	smb_msgbuf_init(mb, authreq->au_secblob.val,
	    authreq->au_secblob.len, 0);

	/*
	 * Decode header.
	 */
	if (smb_msgbuf_decode(mb, "sl", &hdr->ah_signature, &hdr->ah_type) < 0)
		return (NT_STATUS_INTERNAL_ERROR);

	if (smbd_ntlm_decode_bufinfo(mb, &hdr->ah_lm) < 0)
		return (NT_STATUS_INTERNAL_ERROR);

	if (smbd_ntlm_decode_bufinfo(mb, &hdr->ah_ntlm) < 0)
		return (NT_STATUS_INTERNAL_ERROR);

	if (smbd_ntlm_decode_bufinfo(mb, &hdr->ah_domain) < 0)
		return (NT_STATUS_INTERNAL_ERROR);

	if (smbd_ntlm_decode_bufinfo(mb, &hdr->ah_user) < 0)
		return (NT_STATUS_INTERNAL_ERROR);

	if (smbd_ntlm_decode_bufinfo(mb, &hdr->ah_wkst) < 0)
		return (NT_STATUS_INTERNAL_ERROR);

	if (smbd_ntlm_decode_bufinfo(mb, &hdr->ah_ssnkey) < 0)
		return (NT_STATUS_INTERNAL_ERROR);

	if (smb_msgbuf_decode(mb, "l", &hdr->ah_negflags) < 0)
		return (NT_STATUS_INTERNAL_ERROR);

	/*
	 * Now go get the payload section for each of the
	 * smbd_ntlm_bufinfo fields in the header.  Note
	 * that the payload parts may appear in any order,
	 * and smbd_ntlm_msg_getbuf() takes care to locate
	 * payload info. using the offsets already parsed.
	 */
	if (smbd_ntlm_getbuf(mb, &hdr->ah_lm, &auth_msg->am_lm,
	    BUFCONV_NOTERM) < 0)
		return (NT_STATUS_INTERNAL_ERROR);

	if (smbd_ntlm_getbuf(mb, &hdr->ah_ntlm, &auth_msg->am_ntlm,
	    BUFCONV_NOTERM) < 0)
		return (NT_STATUS_INTERNAL_ERROR);

	if (smbd_ntlm_getbuf(mb, &hdr->ah_domain,
	    (uint8_t **)&auth_msg->am_domain, BUFCONV_UNICODE) < 0)
		return (NT_STATUS_INTERNAL_ERROR);

	if (smbd_ntlm_getbuf(mb, &hdr->ah_user,
	    (uint8_t **)&auth_msg->am_user, BUFCONV_UNICODE) < 0)
		return (NT_STATUS_INTERNAL_ERROR);

	if (smbd_ntlm_getbuf(mb, &hdr->ah_wkst,
	    (uint8_t **)&auth_msg->am_wkst, BUFCONV_UNICODE) < 0)
		return (NT_STATUS_INTERNAL_ERROR);

	/*
	 * To accomodate pre-snv_138 Solaris CIFS clients, which don't
	 * send the session key in the format as specified in MS-NLMP.
	 */
	if (smbd_ntlm_getbuf(mb, &hdr->ah_ssnkey, &auth_msg->am_ssnkey,
	    BUFCONV_NOTERM) < 0)
		return (NT_STATUS_INTERNAL_ERROR);

	return (NT_STATUS_SUCCESS);
}

static void
smbd_ntlm_msg_free(smbd_ntlm_msg_t *msg)
{
	switch (msg->msg_type) {
	case NTLMSSP_MSGTYPE_NEGOTIATE:
		smb_msgbuf_term(&msg->ntlm_neg.nm_mb);
		break;

	case NTLMSSP_MSGTYPE_CHALLENGE:
		smb_msgbuf_term(&msg->ntlm_challenge.cm_mb);
		free(msg->ntlm_challenge.cm_avpairs);
		break;

	case NTLMSSP_MSGTYPE_AUTHENTICATE:
		smb_msgbuf_term(&msg->ntlm_auth.am_mb);
		break;

	default:
		break;
	}
}

static uint32_t
smbd_ntlm_challenge_create(smbd_ntlm_msg_t *msg, const smb_buf32_t *challenge,
    uint32_t clnt_negflags)
{
	smbd_ntlm_challenge_msg_t	*challenge_msg;
	smbd_ntlm_challenge_hdr_t	*hdr;
	char				target[NETBIOS_NAME_SZ];
	boolean_t			domain_mode;

	msg->msg_type = NTLMSSP_MSGTYPE_CHALLENGE;
	challenge_msg = &msg->ntlm_challenge;
	hdr = &challenge_msg->cm_hdr;
	(void) strlcpy(hdr->ch_signature, NTLMSSP_SIGNATURE,
	    sizeof (hdr->ch_signature));
	hdr->ch_type = NTLMSSP_MSGTYPE_CHALLENGE;
	smb_msgbuf_init(&challenge_msg->cm_mb, NULL, 0, 0);

	/*
	 * If the system is in domain mode, target name
	 * should be set to the NetBIOS name of the joined
	 * domain. Otherwise, it should be set to NetBIOS
	 * name of the system.
	 */
	*target = '\0';
	domain_mode = (smb_config_get_secmode() == SMB_SECMODE_DOMAIN);

	if (domain_mode)
		(void) smb_getdomainname_nb(target, sizeof (target));

	if (*target == '\0')
		(void) smb_getnetbiosname(target, sizeof (target));

	hdr->ch_target_name.b_len = smb_auth_qnd_unicode(
	    challenge_msg->cm_target_namebuf, target, strlen(target));
	hdr->ch_target_name.b_maxlen = hdr->ch_target_name.b_len;
	hdr->ch_target_name.b_offset = sizeof (smbd_ntlm_challenge_hdr_t);
	hdr->ch_negflags = NTLMSSP_BASIC_NEG_FLAGS;

	if (clnt_negflags & NTLMSSP_NEGOTIATE_56)
		hdr->ch_negflags |= NTLMSSP_NEGOTIATE_56;

	if (clnt_negflags & NTLMSSP_NEGOTIATE_128)
		hdr->ch_negflags |= NTLMSSP_NEGOTIATE_128;

#ifndef NTLM2_SESSION_SECURITY_DOMAIN
	/*
	 * For now, NTLM2 session security is only supported in
	 * workgroup mode.
	 */
	if (!domain_mode &&
	    (clnt_negflags & NTLMSSP_NEGOTIATE_EXTENDED_SESSIONSECURITY))
		hdr->ch_negflags |= NTLMSSP_NEGOTIATE_EXTENDED_SESSIONSECURITY;
#else
	if (clnt_negflags & NTLMSSP_NEGOTIATE_EXTENDED_SESSIONSECURITY)
		hdr->ch_negflags |= NTLMSSP_NEGOTIATE_EXTENDED_SESSIONSECURITY;
#endif

	bcopy(challenge->val, hdr->ch_srv_challenge, challenge->len);

	hdr->ch_reserved = 0;

	challenge_msg->cm_avpairs = smbd_ntlm_avpairs_create();
	if (challenge_msg->cm_avpairs == NULL)
		return (NT_STATUS_NO_MEMORY);

	smbd_ntlm_set_target_info(challenge_msg);
	return (NT_STATUS_SUCCESS);
}

/*
 * On success, the passed buffer will be filled with NTLMSSP challenge message
 * and the number of encoded bytes will be returned. Otherwise, returns 0.
 */
static int
smbd_ntlm_challenge_encode(smbd_ntlm_challenge_msg_t *msg, uint8_t **buf)
{
	int				len;
	int				i;
	smbd_ntlm_challenge_hdr_t	*hdr;
	smbd_ntlm_avpair_t		*avp;
	smb_msgbuf_t			*mb = &msg->cm_mb;
	int				rc;

	/* estimate the size of the buffer. */
	len = sizeof (smbd_ntlm_challenge_hdr_t) +
	    msg->cm_hdr.ch_target_name.b_len +
	    msg->cm_hdr.ch_target_info.b_len;

	*buf = calloc(1, len);
	if (*buf == NULL)
		return (0);

	smb_msgbuf_init(mb, *buf, len, 0);
	hdr = &msg->cm_hdr;
	rc = smb_msgbuf_encode(mb, "sl", hdr->ch_signature, hdr->ch_type);
	if (rc < 0)
		goto cleanup;

	rc = smbd_ntlm_encode_bufinfo(mb, &hdr->ch_target_name);
	if (rc < 0)
		goto cleanup;

	rc = smb_msgbuf_encode(mb, "l#cq", hdr->ch_negflags, 8,
	    hdr->ch_srv_challenge, hdr->ch_reserved);
	if (rc < 0)
		goto cleanup;

	rc = smbd_ntlm_encode_bufinfo(mb, &hdr->ch_target_info);
	if (rc < 0)
		goto cleanup;

	rc = smb_msgbuf_encode(mb, "#c", hdr->ch_target_name.b_len,
	    msg->cm_target_namebuf);
	if (rc < 0)
		goto cleanup;

	for (i = 0; i < SMBD_NTLM_AVPAIR_NUM; i++) {
		avp = &msg->cm_avpairs[i];
		rc = smb_msgbuf_encode(mb, "ww#c", avp->av_id,
		    avp->av_len, avp->av_len, avp->av_value);
		if (rc < 0)
			goto cleanup;
	}

cleanup:
	if (rc < 0) {
		free(*buf);
		*buf = NULL;
		rc = 0;
	} else {
		rc = smb_msgbuf_used(mb);
		if (rc != len)
			syslog(LOG_DEBUG, "ntlmssp (Type 2 message): "
			    "encoded %d bytes (expected: %d bytes)", rc, len);
	}

	return (rc);
}

static int
smbd_ntlm_encode_bufinfo(smb_msgbuf_t *mb, const smbd_ntlm_bufinfo_t *info)
{
	return (smb_msgbuf_encode(mb, "wwl", info->b_len, info->b_maxlen,
	    info->b_offset));
}

static int
smbd_ntlm_decode_bufinfo(smb_msgbuf_t *mb, smbd_ntlm_bufinfo_t *info)
{
	return (smb_msgbuf_decode(mb, "wwl", &info->b_len, &info->b_maxlen,
	    &info->b_offset));
}

/*
 * The Unicode/ASCII strings embedded in the payload of any NTLMSSP messages
 * are not NULL terminated. This function will null-terminated the returned
 * value if the conversion type is either BUFCONV_UNICODE/BUFCONV_ASCII_STRING.
 *
 * Returns 0 on success. Otherwise, -1.
 */
static int
smbd_ntlm_getbuf(smb_msgbuf_t *mb, const smbd_ntlm_bufinfo_t *info,
    uint8_t **val, smbd_ntlm_bufconv_t conversion)
{
	smb_wchar_t *wcs;
	int wcslen = info->b_len / sizeof (smb_wchar_t);

	if (val == NULL)
		return (-1);

	*val = NULL;

	if (info->b_len == 0)
		return (0);

	if (!smb_msgbuf_seek(mb, info->b_offset))
		return (-1);

	switch (conversion) {
	case BUFCONV_UNICODE:
		wcs = (smb_wchar_t *)smb_msgbuf_malloc(mb,
		    info->b_len + sizeof (smb_wchar_t));

		if (wcs == NULL)
			return (-1);

		if (smb_msgbuf_decode(mb, "#w", wcslen, wcs) <= 0)
			return (-1);

		*val = smb_msgbuf_malloc(mb, wcslen + 1);
		if (*val == NULL)
			return (-1);

		(void) smb_wcstombs((char *)*val, wcs, wcslen);
		break;

	case BUFCONV_ASCII_STRING:
		*val = smb_msgbuf_malloc(mb, info->b_len + 1);
		if (*val == NULL)
			return (-1);

		if (smb_msgbuf_decode(mb, "#c", info->b_len, *val) <= 0)
			return (-1);
		break;

	case BUFCONV_NOTERM:
		*val = smb_msgbuf_malloc(mb, info->b_len);
		if (*val == NULL)
			return (-1);

		if (smb_msgbuf_decode(mb, "#c", info->b_len, *val) <= 0)
			return (-1);
		break;
	}

	return (0);
}

/*
 * Caller must free the allocated memory.
 */
static smbd_ntlm_avpair_t *
smbd_ntlm_avpairs_create(void)
{
	int			idx;
	char			ad_domain[MAXHOSTNAMELEN];
	char			fq_host[MAXHOSTNAMELEN];
	char			nb_domain[NETBIOS_NAME_SZ];
	char			nb_host[NETBIOS_NAME_SZ];
	smbd_ntlm_avpair_t	*avpairs;

	(void) smb_getdomainname_nb(nb_domain, sizeof (nb_domain));
	(void) smb_getdomainname_ad(ad_domain, sizeof (ad_domain));
	(void) smb_getnetbiosname(nb_host, sizeof (nb_host));
	(void) smb_getfqhostname(fq_host, sizeof (fq_host));

	avpairs = malloc(SMBD_NTLM_AVPAIR_NUM * sizeof (smbd_ntlm_avpair_t));

	if (avpairs == NULL)
		return (NULL);

	idx = 0;
	/* Domain NetBIOS name (0x02) */
	smbd_ntlm_setup_avpair_unicode(&avpairs[idx++],
	    SMBD_NTLM_AVID_NB_DOMAIN, nb_domain);

	/* Server NetBIOS name (0x01) */
	smbd_ntlm_setup_avpair_unicode(&avpairs[idx++], SMBD_NTLM_AVID_NB_NAME,
	    nb_host);

	/* Domain DNS name (0x04) */
	smbd_ntlm_setup_avpair_unicode(&avpairs[idx++],
	    SMBD_NTLM_AVID_DNS_DOMAIN, ad_domain);

	/* Server DNS name (0x03) */
	smbd_ntlm_setup_avpair_unicode(&avpairs[idx++], SMBD_NTLM_AVID_DNS_NAME,
	    fq_host);

	/* list terminator (0x00) */
	avpairs[idx].av_id = SMBD_NTLM_AVID_EOL;
	avpairs[idx].av_len = 0;
	*avpairs[idx].av_value = '\0';

	return (avpairs);
}

static void
smbd_ntlm_setup_avpair_unicode(smbd_ntlm_avpair_t *avp, int id,
    const char *value)
{
	assert(value != NULL);

	avp->av_id = id;
	avp->av_len = (uint16_t)smb_auth_qnd_unicode(avp->av_value,
	    value, strlen(value));
}

void
smbd_ntlm_set_target_info(smbd_ntlm_challenge_msg_t *msg)
{
	smbd_ntlm_bufinfo_t *tinfo;
	int i;

	assert(msg != NULL);
	tinfo = &msg->cm_hdr.ch_target_info;

	for (i = 0, tinfo->b_len = 0; i < SMBD_NTLM_AVPAIR_NUM; i++) {
		/* target info fields: 2-byte len and 2-byte max len */
		tinfo->b_len += (sizeof (uint16_t) * 2);
		/* target info value */
		tinfo->b_len += msg->cm_avpairs[i].av_len;
	}

	tinfo->b_maxlen = tinfo->b_len;
	tinfo->b_offset = msg->cm_hdr.ch_target_name.b_offset +
	    msg->cm_hdr.ch_target_name.b_len;
}

/*
 * Update the username, domain, and auth info based on the NTLMSSP Type 3
 * message. Memory allocated for the authreq will be freed in
 * smbd_dop_user_auth() regardless of the update status.
 */
static uint32_t
smbd_ntlm_update_userinfo(smb_authreq_t *authreq, smbd_ntlm_auth_msg_t *msg)
{
	smbd_ntlm_auth_hdr_t	*hdr = &msg->am_hdr;
	smb_buf16_t buf;

	if (msg->am_domain != NULL) {
		free(authreq->au_domain);
		free(authreq->au_edomain);
		authreq->au_domain = strdup(msg->am_domain);
		authreq->au_edomain = strdup(msg->am_domain);
	}

	if (msg->am_user != NULL) {
		free(authreq->au_username);
		free(authreq->au_eusername);
		authreq->au_username = strdup(msg->am_user);
		authreq->au_eusername = strdup(msg->am_user);
	} else if (smbd_ntlm_is_anon(msg)) {
		free(authreq->au_eusername);
		authreq->au_eusername = strdup("anonymous");
		authreq->au_flags |= SMB_AUTH_ANON;
	}

	if (authreq->au_domain == NULL || authreq->au_edomain == NULL ||
	    authreq->au_username == NULL || authreq->au_eusername == NULL)
		return (NT_STATUS_NO_MEMORY);

	bzero(&authreq->au_lmpasswd, sizeof (smb_buf16_t));
	bzero(&authreq->au_ntpasswd, sizeof (smb_buf16_t));

	if (hdr->ah_lm.b_len != 0) {
		if ((buf.val = malloc(hdr->ah_lm.b_len)) == NULL)
			return (NT_STATUS_NO_MEMORY);

		buf.len = hdr->ah_lm.b_len;
		bcopy(msg->am_lm, buf.val, buf.len);
		authreq->au_lmpasswd = buf;
		authreq->au_flags |= SMB_AUTH_LM_PASSWD;
	}


	if (hdr->ah_ntlm.b_len != 0) {
		if ((buf.val = malloc(hdr->ah_ntlm.b_len)) == NULL)
			return (NT_STATUS_NO_MEMORY);

		buf.len = hdr->ah_ntlm.b_len;
		bcopy(msg->am_ntlm, buf.val, buf.len);
		authreq->au_ntpasswd = buf;
		authreq->au_flags |= SMB_AUTH_NTLM_PASSWD;
	}

	return (NT_STATUS_SUCCESS);
}

/*
 * Determine whether the authentication request is made by an anonymous user
 * by examining both the LM and NTLM challenge responses.
 *
 * Returns B_TRUE if zero-length NTLM challenge response and 0/1 byte LM
 * challenge response are detected.
 */
static boolean_t
smbd_ntlm_is_anon(const smbd_ntlm_auth_msg_t *msg)
{
	if (msg->am_user == NULL &&
	    msg->am_hdr.ah_ntlm.b_len == 0 &&
	    (msg->am_hdr.ah_lm.b_len == 0 ||
	    (msg->am_hdr.ah_lm.b_len == 1 && *msg->am_lm == '\0')))
		return (B_TRUE);

	return (B_FALSE);
}

/*
 * Perform NTLM authentication.
 *
 * The dispatched functions must only update the authrsp status if they
 * attempt to authenticate the user.
 */
static uint32_t
smbd_ntlm_auth_dispatch(smb_authreq_t *authreq, smb_authrsp_t *authrsp)
{
	static smbd_ntlm_authop_t	ops[] = {
		smbd_ntlm_auth_anon,
		smbd_ntlm_auth_local,
		smbd_ntlm_auth_domain,
		smbd_ntlm_auth_guest
	};
	smb_domain_t			domain;
	int				n_op = (sizeof (ops) / sizeof (ops[0]));
	int				i;

	authreq->au_secmode = smb_config_get_secmode();
	authrsp->ar_status = NT_STATUS_NO_SUCH_USER;

	if (smb_domain_lookup_name(authreq->au_edomain, &domain))
		authreq->au_domain_type = domain.di_type;
	else
		authreq->au_domain_type = SMB_DOMAIN_NULL;

	if ((authrsp->ar_token = smbd_token_alloc()) == NULL) {
		syslog(LOG_ERR, "logon[%s\\%s]: %m",
		    authreq->au_edomain, authreq->au_eusername);
		return (NT_STATUS_NO_MEMORY);
	}

	for (i = 0; i < n_op; ++i) {
		(*ops[i])(authreq, authrsp);
		if (authrsp->ar_status == NT_STATUS_SUCCESS)
			break;

		smbd_token_cleanup(authrsp->ar_token);
		if (authrsp->ar_status == NT_STATUS_NO_MEMORY)
			break;
	}

	if (authrsp->ar_status != NT_STATUS_SUCCESS) {
		smbd_token_free(authrsp->ar_token);
		authrsp->ar_token = NULL;
	}

	return (authrsp->ar_status);
}

/*
 * If we are not going to attempt authentication, this function returns
 * without updating the authrsp.
 */
static void
smbd_ntlm_auth_domain(smb_authreq_t *authreq, smb_authrsp_t *authrsp)
{
	smb_authinfo_t authinfo;

	if ((authreq->au_secmode != SMB_SECMODE_DOMAIN) ||
	    (authreq->au_domain_type == SMB_DOMAIN_LOCAL)) {
		return;
	}

	authrsp->ar_status = netr_logon(authreq, &authinfo);
	if (authrsp->ar_status == NT_STATUS_SUCCESS) {
		authrsp->ar_status =
		    smbd_token_setup_domain(authrsp->ar_token, &authinfo);
		smb_authinfo_free(&authinfo);
	}
}

/*
 * If the user has an entry in the local database, attempt local authentication.
 *
 * In domain mode, we try to exclude domain accounts, which we do by only
 * accepting local or null (blank) domain names here.  Some clients (Mac OS)
 * don't always send the domain name.
 *
 * If we are not going to attempt authentication, this function returns
 * without updating the authrsp.
 */
static void
smbd_ntlm_auth_local(smb_authreq_t *authreq, smb_authrsp_t *authrsp)
{
	char *username = authreq->au_eusername;
	smb_account_t guest;
	smb_passwd_t smbpw;
	uint32_t status;
	smb_token_t *token;
	boolean_t isguest = B_FALSE;

	if (authreq->au_secmode == SMB_SECMODE_DOMAIN) {
		if ((authreq->au_domain_type != SMB_DOMAIN_LOCAL) &&
		    (authreq->au_domain_type != SMB_DOMAIN_NULL))
			return;
	}

	bzero(&guest, sizeof (smb_account_t));
	if (smbd_guest_account(&guest) == 0) {
		if (smb_strcasecmp(guest.a_name, username, 0) == 0)
			isguest = B_TRUE;
	}

	token = authrsp->ar_token;
	status = smbd_token_auth_local(authreq, token, &smbpw);
	if (status == NT_STATUS_SUCCESS) {
		if (isguest)
			status = smbd_token_setup_guest(&guest, token);
		else
			status = smbd_token_setup_local(&smbpw, token);
	}
	authrsp->ar_status = status;

	smb_account_free(&guest);
}

/*
 * Guest authentication.  This may be a local guest account or the guest
 * account may be mapped to a local account.  These accounts are regular
 * accounts with normal password protection.
 *
 * Only proceed with a guest logon if previous logon options have resulted
 * in NT_STATUS_NO_SUCH_USER.
 *
 * If we are not going to attempt authentication, this function returns
 * without updating the authrsp.
 */
static void
smbd_ntlm_auth_guest(smb_authreq_t *authreq, smb_authrsp_t *authrsp)
{
	smb_account_t guest;
	smb_passwd_t smbpw;
	uint32_t status;
	char *temp;

	if (authrsp->ar_status != NT_STATUS_NO_SUCH_USER)
		return;

	if (smbd_guest_account(&guest) != 0)
		return;

	temp = authreq->au_eusername;
	authreq->au_eusername = guest.a_name;

	status = smbd_token_auth_local(authreq, authrsp->ar_token, &smbpw);
	if ((status == NT_STATUS_SUCCESS) ||
	    (status == NT_STATUS_NO_SUCH_USER)) {
		status = smbd_token_setup_guest(&guest, authrsp->ar_token);
	}
	authrsp->ar_status = status;

	authreq->au_eusername = temp;
	smb_account_free(&guest);
}

/*
 * If authreq represents an anonymous user, setup the token in the authrsp.
 * If we are not going to attempt authentication, this function returns
 * without updating the authrsp.
 */
static void
smbd_ntlm_auth_anon(smb_authreq_t *authreq, smb_authrsp_t *authrsp)
{
	if (authreq->au_flags & SMB_AUTH_ANON) {
		authrsp->ar_status =
		    smbd_token_setup_anon(authrsp->ar_token);
	}
}

/*
 * Get a reference to the guest account definition.
 */
static int
smbd_guest_account(smb_account_t *account)
{
	static mutex_t		guest_mutex;
	const smb_account_t	*guest;

	(void) mutex_lock(&guest_mutex);

	if ((guest = smbd_generate_guest_account()) == NULL) {
		(void) mutex_unlock(&guest_mutex);
		return (-1);
	}

	account->a_name = strdup(guest->a_name);
	account->a_domain = strdup(guest->a_domain);
	account->a_type = guest->a_type;
	account->a_sid = smb_sid_dup(guest->a_sid);
	account->a_domsid = smb_sid_dup(guest->a_domsid);
	account->a_rid = guest->a_rid;

	(void) mutex_unlock(&guest_mutex);

	if (!smb_account_validate(account)) {
		smb_account_free(account);
		return (-1);
	}

	return (0);
}

/*
 * By default the guest account name would be "guest" unless there is
 * an idmap name-based rule that maps the guest to a local Solaris user,
 * in which case the name of that user is used instead of "guest".
 */
static const smb_account_t *
smbd_generate_guest_account(void)
{
	static smb_account_t	guest;
	idmap_stat		stat;
	uid_t			guest_uid;
	struct passwd		pw;
	char			pwbuf[1024];
	int			idtype;
	uint32_t		status;

	if (guest.a_name == NULL) {
		status = smb_sam_lookup_name(NULL, "guest", SidTypeUser,
		    &guest);
		if (status != NT_STATUS_SUCCESS) {
			smb_account_free(&guest);
			return (NULL);
		}
	}

	idtype = SMB_IDMAP_USER;
	stat = smb_idmap_getid(guest.a_sid, &guest_uid, &idtype);
	if (stat != IDMAP_SUCCESS)
		return (&guest);

	/* If we get an Ephemeral ID, return the default name */
	if (IDMAP_ID_IS_EPHEMERAL(guest_uid))
		return (&guest);

	if (getpwuid_r(guest_uid, &pw, pwbuf, sizeof (pwbuf)) == NULL)
		return (&guest);

	/*
	 * We have an alternate username for the guest account.
	 */
	free(guest.a_name);
	guest.a_name = strdup(pw.pw_name);

	if (guest.a_name == NULL) {
		smb_account_free(&guest);
		return (NULL);
	}

	return (&guest);
}
