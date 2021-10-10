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

#include <synch.h>
#include <strings.h>
#include <syslog.h>
#include <bsm/adt.h>
#include <bsm/adt_event.h>
#include <bsm/audit_uevents.h>
#include <pwd.h>
#include <gssapi/gssapi.h>
#include <smb/spnego.h>
#include <smbsrv/smb_avl.h>
#include "smbd.h"

/*
 * Windows clients tend to give up after 4 unsuccessful security exchange
 * phases. So, smb/server imposes the same rule to ensure non Windows
 * SMB clients that behave differently from Windows will not reinitiate
 * the security exchange after SMBD_AUTH_MAX_PHASE is reached.
 */
#define	SMBD_AUTH_MAX_PHASE	4

/*
 * An audit session is established at user logon and terminated at user
 * logoff.
 *
 * SMB audit handles are allocated when users logon (SmbSessionSetupX)
 * and deallocted when a user logs off (SmbLogoffX).  Each time an SMB
 * audit handle is allocated it is added to a global list.
 */
typedef struct smb_audit {
	struct smb_audit	*sa_next;
	adt_session_data_t	*sa_handle;
	uid_t			sa_uid;
	gid_t			sa_gid;
	uint32_t		sa_audit_sid;
	char			*sa_domain;
	char			*sa_username;
} smb_audit_t;

static smb_audit_t *smbd_audit_list;
static mutex_t smbd_audit_lock;

/*
 * Unique identifier for audit sessions in the audit list.
 * Used to lookup an audit session on logoff.
 */
static uint32_t smbd_audit_sid;

static int smbd_user_audit(smb_authreq_t *, smb_token_t *);
static void smbd_audit_link(smb_audit_t *);
static smb_audit_t *smbd_audit_unlink(uint32_t);

/*
 * The following macros should be removed once the following  RFE is available:
 *
 * 6878600 gssapi/generic Need well-defined symbols for mechanism OIDs
 */
#define	SPNEGO_OID "\053\006\001\005\005\002"
#define	SPNEGO_OID_LENGTH 6
#define	KRB5_OID "\052\206\110\206\367\022\001\002\002"
#define	KRB5_OID_LENGTH 9
#define	MS_KRB5_OID "\052\206\110\202\367\022\001\002\002"
#define	MS_KRB5_OID_LENGTH	KRB5_OID_LENGTH

static gss_OID_desc spnego_oid_desc = {SPNEGO_OID_LENGTH, SPNEGO_OID};


/*
 * ASN.1 DER encodings
 * ======================
 * Each element is distinguished by a leading byte that indicates the makeup of
 * the subsequent data.
 *
 * The following tags/identifiers are typically seen when parsing the DER
 * encoded GSS-API blob.
 *
 * 0x06 Object Identifier (aka OID)
 * 0x60 Application Constructed Object
 * 0x82	Length Tag, indicates the following 2 bytes represent the length
 */
#define	DER_APP		0x60
#define	DER_LEN		0x82
#define	DER_OID		0x06

/*
 * In order to keep track of pending security exchange context, smbd
 * maintains a collection of session objects where each session object
 * maintains a collection of user objects.
 *
 * A session object represents a TCP connection established between a
 * client and Solaris SMB server. A session object is created and added to the
 * session AVL tree during the SmbNegotiate exchange on a connection. A session
 * object can be looked up given its SMB session ID. The session object is
 * removed from the AVL tree upon session teardown.
 *
 * A user object is added to the user AVL tree of a specified session upon the
 * start of a user logon and is removed from the tree upon user logoff.
 * If a user object is created for the first SmbSessionSetup exchange of a TCP
 * connection, it will be assigned the GSS context handle of the associated
 * session.
 *
 * User authentication might takes multiple rounds of SmbSessionSetup exchanges.
 * The pending security exchange information is stored in the user object
 * for use in the subsequent phases of the security exchange (if any).
 * A user object can be looked up given both a reference of the SMB session and
 * the corresponding SMB UID.
 */

/*
 * smbd session AVL tree
 */
static smb_avl_t	*smbd_session_avl = NULL;

/*
 * smbd_session
 */
#define	SMBD_SESSION_MAGIC	0x73657373 /* 'sess' */
#define	SMBD_SESSION_VALID(s)	\
    assert(((s) != NULL) && ((s)->s_magic == SMBD_SESSION_MAGIC))

static int smbd_session_cmp(const void *, const void *);
static int smbd_session_add(const void *);
static void smbd_session_remove(void *);
static void smbd_session_hold(const void *);
static void smbd_session_rele(const void *);
static smbd_session_t *smbd_session_alloc(uint64_t, gss_ctx_id_t);
static void smbd_session_destroy(void *);

static smbd_session_t *smbd_auth_session_lookup(uint64_t);
static void smbd_auth_session_release(smbd_session_t *);
static boolean_t smbd_auth_set_secblob(gss_ctx_id_t *, smb_buf16_t *);

static smb_avl_nops_t	smbd_session_ops = {
	smbd_session_cmp,	/* avln_cmp */
	smbd_session_add,	/* avln_add */
	smbd_session_remove,	/* avln_remove */
	smbd_session_hold,	/* avln_hold */
	smbd_session_rele,	/* avln_rele */
	smbd_session_remove	/* avln_flush */
};

/*
 * smbd_user
 */
#define	SMBD_USER_MAGIC		0x75736572 /* 'user' */
#define	SMBD_USER_VALID(u)	\
    assert(((u) != NULL) && ((u)->u_magic == SMBD_USER_MAGIC))

static int smbd_user_cmp(const void *, const void *);
static int smbd_user_add(const void *);
static void smbd_user_remove(void *);
static void smbd_user_hold(const void *);
static void smbd_user_rele(const void *);
static smbd_user_t *smbd_user_alloc(uint16_t, gss_ctx_id_t);
static void smbd_user_destroy(void *);

static smbd_user_t *smbd_auth_user_create(smbd_session_t *, uint16_t);
static void smbd_auth_user_remove(smbd_session_t *, uint16_t);
static smbd_user_t *smbd_auth_user_lookup(smbd_session_t *, uint16_t);
static void smbd_auth_user_release(smbd_session_t *, smbd_user_t *);

static smb_avl_nops_t	smbd_user_ops = {
	smbd_user_cmp,		/* avln_cmp */
	smbd_user_add,		/* avln_add */
	smbd_user_remove,	/* avln_remove */
	smbd_user_hold,		/* avln_hold */
	smbd_user_rele,		/* avln_rele */
	smbd_user_remove	/* avln_flush */
};

typedef struct smbd_mech_table {
	rwlock_t		t_lock;
	uint32_t		t_num;
	smbd_mech_t		*t_mechs[SMBD_MECH_MAX];
} smbd_mech_table_t;

/* Table of all the available authentication mechanisms */
static smbd_mech_table_t	smbd_mech_table;

static int32_t smbd_mech_gettype(smb_authreq_t *);
static smbd_mech_t *smbd_mech_lookup(int32_t);
static boolean_t smbd_is_krbreq(smb_buf16_t *);

static uint32_t smbd_pre_auth(smbd_mech_t *, smb_authreq_t *);
static void smbd_auth(smbd_mech_t *, smb_authreq_t *, smb_authrsp_t *);
static uint32_t smbd_post_auth(smbd_mech_t *, smb_authreq_t *,
    smb_authrsp_t *);


/*
 * =================================
 * GSS acceptor synchronization
 * =================================
 *
 * The current MIT's Kerberos replay cache implementation does not
 * provide any locking to account for multi-threaded GSS acceptors.
 * We need to provide serialization until 6957103 has been
 * implemented.
 */
static mutex_t smbd_gsslock;

static void smbd_gss_build_msg(OM_uint32, int, gss_OID, char *, size_t);

/*
 * Acquire the gss lock prior to making a GSS acceptor call.
 */
void
smbd_gss_lock(void)
{
	(void) mutex_lock(&smbd_gsslock);
}

/*
 * Release the gss lock upon completion of the GSS acceptor call.
 */
void
smbd_gss_unlock(void)
{
	(void) mutex_unlock(&smbd_gsslock);
}

/*
 * Log GSS acceptor error messages.
 * Filter out messages that don't make sense to end-users.
 * For example, in GSS_S_CONTINUE_NEEDED case:
 * "The routine must be called again to complete its function"
 */
void
smbd_gss_log(char *msg_prefix, gss_OID mech_type, OM_uint32 major,
    OM_uint32 minor)
{
	char major_errmsg[512], minor_errmsg[512];

	if (major != GSS_S_CONTINUE_NEEDED) {
		smbd_gss_build_msg(major, GSS_C_GSS_CODE, GSS_C_NULL_OID,
		    major_errmsg, sizeof (major_errmsg));
		syslog(LOG_NOTICE, "%s (GSS major error): %s", msg_prefix,
		    major_errmsg);
	}

	smbd_gss_build_msg(minor, GSS_C_MECH_CODE, mech_type,
	    minor_errmsg, sizeof (minor_errmsg));
	syslog(LOG_NOTICE, "%s (GSS minor error): %s", msg_prefix,
	    minor_errmsg);
}

static void
smbd_gss_build_msg(OM_uint32 status, int status_type, gss_OID mech_type,
    char *buf, size_t bufsz)
{
	OM_uint32 major, minor, more;
	gss_buffer_desc msg;
	size_t left = bufsz;	/* number of bytes left in the buffer */
	size_t bytes;		/* number of bytes to be copied */
	char *bufp = buf;

	more = 0;

	do {
		if (left == 0)
			break;

		bzero(&msg, sizeof (gss_buffer_desc));
		major = gss_display_status(&minor, status, status_type,
		    mech_type, &more, &msg);

		if ((major != GSS_S_COMPLETE) &&
		    (major != GSS_S_CONTINUE_NEEDED))
			break;

		if (msg.value == NULL)
			break;

		if (left >  msg.length) {
			bytes = msg.length;
			left -= msg.length;
		} else {
			/* message will be truncated. */
			bytes = left - 1;
			left = 0;
		}

		bcopy(msg.value, bufp, bytes);
		bufp += bytes;
		*bufp = '\0';

		(void) gss_release_buffer(&minor, &msg);
	} while (more != 0);
}

/*
 * Obtain mechanism token by stripping off the GSS-API and SPNEGO wrapper
 * of the given security blob.
 */
uint32_t
smbd_mech_secblob2token(const smb_buf16_t *spnego_token,
    smb_buf16_t *mech_token)
{
	SPNEGO_TOKEN_HANDLE stok_in;
	int rc;
	unsigned long len;

	bzero(mech_token, sizeof (smb_buf16_t));
	bzero(&stok_in, sizeof (SPNEGO_TOKEN_HANDLE));
	rc = spnegoInitFromBinary(spnego_token->val,
	    spnego_token->len, &stok_in);

	if (rc != SPNEGO_E_SUCCESS) {
		syslog(LOG_ERR, "failed to decode "
		    "incoming SPNEGO message (%d)", rc);
		return (NT_STATUS_INTERNAL_ERROR);

	}

	rc = spnegoGetMechToken(stok_in, mech_token->val, &len);

	/*
	 * Now get the payload.  Two calls:
	 * first gets the size, 2nd the data.
	 *
	 * Expect SPNEGO_E_BUFFER_TOO_SMALL here,
	 * but if the payload is missing, we'll
	 * get SPNEGO_E_ELEMENT_UNAVAILABLE.
	 */
	switch (rc) {
	case SPNEGO_E_BUFFER_TOO_SMALL:
		break;

	case SPNEGO_E_ELEMENT_UNAVAILABLE:
	default:
		spnegoFreeData(stok_in);
		return (NT_STATUS_INTERNAL_ERROR);
	}

	if (len == 0) {
		spnegoFreeData(stok_in);
		return (NT_STATUS_INTERNAL_ERROR);
	}

	if ((mech_token->val = calloc(1, len)) == NULL) {
		spnegoFreeData(stok_in);
		return (NT_STATUS_INTERNAL_ERROR);
	}

	rc = spnegoGetMechToken(stok_in, mech_token->val, &len);

	mech_token->len = len;

	if (rc != SPNEGO_E_SUCCESS) {
		syslog(LOG_DEBUG, "failed to get mech token (%d)", rc);
		spnegoFreeData(stok_in);
		free(mech_token->val);
		mech_token->val = NULL;
		return (NT_STATUS_INTERNAL_ERROR);
	}

	spnegoFreeData(stok_in);

	return (NT_STATUS_SUCCESS);

}

/*
 * Invoked at user logon due to SmbSessionSetupX.
 * Dispatch the authentication request to the appropriate Security Support
 * Provider (SSP).
 */
void
smbd_user_auth(smb_authreq_t *authreq, smb_authrsp_t *authrsp)
{
	smbd_mech_t	*mech;
	int32_t		mechtype;
	uint32_t	status;

	mechtype = smbd_mech_gettype(authreq);
	if (mechtype == -1) {
		authrsp->ar_status = NT_STATUS_INVALID_PARAMETER;
		return;
	}

	mech = smbd_mech_lookup(mechtype);
	assert(mech != NULL);

	status = smbd_pre_auth(mech, authreq);
	if (status != NT_STATUS_SUCCESS) {
		syslog(LOG_ERR, "pre-auth operation failed: %s",
		    xlate_nt_status(status));
		authrsp->ar_status = NT_STATUS_LOGON_FAILURE;
		return;
	}

	/* sets authrsp->ar_status */
	smbd_auth(mech, authreq, authrsp);

	status = smbd_post_auth(mech, authreq, authrsp);
	if (status != NT_STATUS_SUCCESS) {
		syslog(LOG_ERR, "post-auth operation failed: %s",
		    xlate_nt_status(status));
		authrsp->ar_status = NT_STATUS_LOGON_FAILURE;
		return;
	}
}

/*
 * Start an audit session and audit the user authentication event
 */
static int
smbd_user_audit(smb_authreq_t *authreq, smb_token_t *token)
{
	smb_audit_t		*audit;
	adt_session_data_t	*ah;
	adt_event_data_t	*event;
	au_tid_addr_t		termid;
	char			sidbuf[SMB_SID_STRSZ];
	char			*username, *domain, *sid;
	uid_t			uid;
	gid_t			gid;
	int			status, retval;

	if (token == NULL) {
		uid = ADT_NO_ATTRIB;
		gid = ADT_NO_ATTRIB;
		sid = NT_NULL_SIDSTR;
		username = authreq->au_eusername;
		domain = authreq->au_edomain;
		status = ADT_FAILURE;
		retval = ADT_FAIL_VALUE_AUTH;
	} else {
		uid = token->tkn_user.i_id;
		gid = token->tkn_primary_grp.i_id;
		if (IDMAP_ID_IS_EPHEMERAL(uid) || getpwuid(uid) == NULL) {
			uid = ADT_NO_ATTRIB;
			gid = ADT_NO_ATTRIB;
		}
		smb_sid_tostr(token->tkn_user.i_sid, sidbuf);
		sid = sidbuf;
		username = token->tkn_account_name;
		domain = token->tkn_domain_name;
		status = ADT_SUCCESS;
		retval = ADT_SUCCESS;
	}

	if (adt_start_session(&ah, NULL, 0)) {
		syslog(LOG_AUTH | LOG_ALERT, "adt_start_session: %m");
		return (-1);
	}

	if ((event = adt_alloc_event(ah, ADT_smbd_session)) == NULL) {
		syslog(LOG_AUTH | LOG_ALERT,
		    "adt_alloc_event(ADT_smbd_session): %m");
		(void) adt_end_session(ah);
		return (-1);
	}

	(void) memset(&termid, 0, sizeof (au_tid_addr_t));
	termid.at_port = authreq->au_local_port;

	if (authreq->au_clnt_ipaddr.a_family == AF_INET) {
		termid.at_addr[0] = authreq->au_clnt_ipaddr.a_ipv4;
		termid.at_type = AU_IPv4;
	} else {
		bcopy(&authreq->au_clnt_ipaddr.a_ip, termid.at_addr,
		    IPV6_ADDR_LEN);
		termid.at_type = AU_IPv6;
	}
	adt_set_termid(ah, &termid);

	if (adt_set_user(ah, uid, gid, uid, gid, NULL, ADT_NEW)) {
		syslog(LOG_AUTH | LOG_ALERT, "adt_set_user: %m");
		adt_free_event(event);
		(void) adt_end_session(ah);
		return (-1);
	}

	event->adt_smbd_session.domain = domain;
	event->adt_smbd_session.username = username;
	event->adt_smbd_session.sid = sid;

	if (adt_put_event(event, status, retval))
		syslog(LOG_AUTH | LOG_ALERT, "adt_put_event: %m");

	adt_free_event(event);

	if (token != NULL) {
		if ((audit = malloc(sizeof (smb_audit_t))) == NULL) {
			syslog(LOG_ERR, "smbd_user_audit: %m");
			(void) adt_end_session(ah);
			return (-1);
		}

		audit->sa_handle = ah;
		audit->sa_uid = uid;
		audit->sa_gid = gid;
		audit->sa_username = strdup(username);
		audit->sa_domain = strdup(domain);

		smbd_audit_link(audit);
		(void) mutex_lock(&authreq->au_user->u_mtx);
		authreq->au_user->u_audit_sid = audit->sa_audit_sid;
		(void) mutex_unlock(&authreq->au_user->u_mtx);
	} else {
		(void) adt_end_session(ah);
	}

	return (0);
}

/*
 * Invoked at user logoff. Remove the user from the AVL tree
 * of his session given both the user and session IDs.
 *
 * If this is the final logoff for this user on the session, audit the event
 * and terminate the audit session.
 */
void
smbd_user_logoff(smb_logoff_t *logoff)
{
	smb_audit_t		*audit;
	adt_session_data_t	*ah;
	adt_event_data_t	*event;
	smbd_session_t		*session;
	smbd_user_t		*user;

	if ((session = smbd_auth_session_lookup(logoff->lo_session_id)) == NULL)
		return;

	user = smbd_auth_user_lookup(session, logoff->lo_user_id);
	if (user == NULL) {
		smbd_auth_session_release(session);
		return;
	}

	(void) mutex_lock(&user->u_mtx);
	audit = smbd_audit_unlink(user->u_audit_sid);
	(void) mutex_unlock(&user->u_mtx);

	smbd_auth_user_remove(session, logoff->lo_user_id);
	smbd_auth_user_release(session, user);
	smbd_auth_session_release(session);

	if (audit == NULL)
		return;

	ah = audit->sa_handle;

	if ((event = adt_alloc_event(ah, ADT_smbd_logoff)) == NULL) {
		syslog(LOG_AUTH | LOG_ALERT,
		    "adt_alloc_event(ADT_smbd_logoff): %m");
	} else {
		event->adt_smbd_logoff.domain = audit->sa_domain;
		event->adt_smbd_logoff.username = audit->sa_username;

		if (adt_put_event(event, ADT_SUCCESS, ADT_SUCCESS))
			syslog(LOG_AUTH | LOG_ALERT, "adt_put_event: %m");

		adt_free_event(event);
	}

	(void) adt_end_session(ah);

	free(audit->sa_username);
	free(audit->sa_domain);
	free(audit);
}

/*
 * Allocate an id and link an audit handle onto the global list.
 */
static void
smbd_audit_link(smb_audit_t *audit)
{
	(void) mutex_lock(&smbd_audit_lock);

	do {
		++smbd_audit_sid;
	} while ((smbd_audit_sid == 0) || (smbd_audit_sid == (uint32_t)-1));

	audit->sa_audit_sid = smbd_audit_sid;
	audit->sa_next = smbd_audit_list;
	smbd_audit_list = audit;

	(void) mutex_unlock(&smbd_audit_lock);
}

/*
 * Unlink an audit handle.  If the audit matching audit_sid is found,
 * remove it from the list and return it.
 */
static smb_audit_t *
smbd_audit_unlink(uint32_t audit_sid)
{
	smb_audit_t *audit;
	smb_audit_t **ppe;

	(void) mutex_lock(&smbd_audit_lock);
	ppe = &smbd_audit_list;

	while (*ppe) {
		audit = *ppe;

		if (audit->sa_audit_sid == audit_sid) {
			*ppe = audit->sa_next;
			(void) mutex_unlock(&smbd_audit_lock);
			return (audit);
		}

		ppe = &(*ppe)->sa_next;
	}

	(void) mutex_unlock(&smbd_audit_lock);
	return (NULL);
}

void
smbd_mech_register(smbd_mech_t *mech)
{
	int i;

	assert(mech != NULL);
	assert(mech->m_pre_auth != NULL);
	assert(mech->m_auth != NULL);
	assert(mech->m_post_auth != NULL);

	(void) rw_wrlock(&smbd_mech_table.t_lock);
	assert(smbd_mech_table.t_num < SMBD_MECH_MAX);

	for (i = 0; i < smbd_mech_table.t_num; i++) {
		if (mech->m_type == smbd_mech_table.t_mechs[i]->m_type) {
			(void) rw_unlock(&smbd_mech_table.t_lock);
			return;
		}
	}
	smbd_mech_table.t_mechs[smbd_mech_table.t_num++] = mech;
	(void) rw_unlock(&smbd_mech_table.t_lock);
}

static smbd_mech_t *
smbd_mech_lookup(int32_t mechtype)
{
	int i;
	smbd_mech_t *mech = NULL;

	(void) rw_rdlock(&smbd_mech_table.t_lock);
	for (i = 0; i < smbd_mech_table.t_num; i++) {
		if (mechtype == smbd_mech_table.t_mechs[i]->m_type) {
			mech = smbd_mech_table.t_mechs[i];
			break;
		}
	}

	(void) rw_unlock(&smbd_mech_table.t_lock);

	return (mech);
}

/*
 * Determine the authentication type by examining the au_flags of the
 * incoming 'authreq'. If the au_flags indicates no security blob is encoded
 * in the authreq, NTLM authentication (w/o extended security) will be
 * performed. Otherwise, extended security authentication will be performed.
 *
 * If the contents of the incoming security blob starts with a NTLMSSP
 * signature, raw NTLMSSP authentication will be performed. Then,
 * if Kerberos data blob is not embedded in SPNEGO message but prepended by
 * GSS-API header, Kerberos authentication will be performed. Otherwise,
 * the GSS wrapper will be stripped and the actual mechansim token
 * will be examined to determine whether NTLMSSP or Kerberos authentication
 * should be performed.
 *
 * Returns mechanism type on success. Otherwise, returns -1.
 */
static int32_t
smbd_mech_gettype(smb_authreq_t *authreq)
{
	smb_buf16_t mech_token;
	smbd_mech_type_t type;

	if (!(authreq->au_flags & SMB_AUTH_SECBLOB))
		return (SMBD_MECH_NTLM);

	assert(authreq->au_secblob.val != NULL);

	if (bcmp(authreq->au_secblob.val, NTLMSSP_SIGNATURE,
	    strlen(NTLMSSP_SIGNATURE)) == 0)
		return (SMBD_MECH_RAW_NTLMSSP);

	if (smbd_is_krbreq(&authreq->au_secblob))
		return (SMBD_MECH_KRB);

	if (smbd_mech_secblob2token(&authreq->au_secblob, &mech_token)
	    != NT_STATUS_SUCCESS)
		return (-1);

	if (bcmp(mech_token.val, NTLMSSP_SIGNATURE, strlen(NTLMSSP_SIGNATURE)))
		type = SMBD_MECH_KRB;
	else
		type = SMBD_MECH_NTLMSSP;

	free(mech_token.val);
	return (type);
}

/*
 * Determine whether the Kerberos AP-REQ is embedded in a SPNEGO message.
 * This function returns B_TRUE if the following byte sequence is received:
 *
 * 0x60 (Application Contructed Obj), 0x82 (length identifier), 2 bytes (the
 * actual length), 0x06 (OID tag), 0x09 (length of the KRB5/MS KRB5 OID),
 * byte representation for KRB5 or MS KRB OID.
 *
 * NOTE: The expected Windows/Mac client's behavior is to always send
 * SmbSessionSetup request with the Kerberos message embedded in SPNEGO message
 * when the server returns an optimistic token in the extended SmbNegotiate
 * reply.
 */
static boolean_t
smbd_is_krbreq(smb_buf16_t *secblob)
{
	uint8_t *ptr;

	assert(secblob != NULL);
	assert(secblob->val != NULL);

	ptr = secblob->val;

	if (*ptr != DER_APP)
		return (B_FALSE);

	if (*(ptr + 1) != DER_LEN)
		return (B_FALSE);

	if (*(ptr + 4) != DER_OID)
		return (B_FALSE);

	if (*(ptr + 5) != KRB5_OID_LENGTH)
		return (B_FALSE);

	if (bcmp(ptr + 6, KRB5_OID, KRB5_OID_LENGTH) == 0)
		return (B_TRUE);

	if (bcmp(ptr + 6, MS_KRB5_OID, MS_KRB5_OID_LENGTH) == 0)
		return (B_TRUE);

	return (B_FALSE);
}


/*
 * This function should be used by a mech plugin to indicate a NULL pre-auth
 * operation.
 */
/*ARGSUSED*/
uint32_t
smbd_mech_preauth_noop(smb_authreq_t *authreq)
{
	return (NT_STATUS_SUCCESS);
}

/*
 * This function should be used by a mech plugin to indicate a NULL post-auth
 * operation.
 */
/*ARGSUSED*/
uint32_t
smbd_mech_postauth_noop(smb_authreq_t *authreq, smb_authrsp_t *authrsp)
{
	return (NT_STATUS_SUCCESS);
}

/*
 * Execute mechanism specific pre-auth operation.
 *
 * In preparation for user authentication, both the associated session and
 * user objects will be looked up by SMB session and user IDs in the authreq,
 * respectively.
 *
 * If this is a new authentication request but a user structure with the
 * same UID is found it will be removed as a stale structure before continuing
 * processing of the new request.
 *
 * If the user lookup fails, a new user object will be added to the user AVL
 * tree of the associated session object before the authentication process.
 *
 * On success, a hold will be taken on both session and user objects.
 */
static uint32_t
smbd_pre_auth(smbd_mech_t *mech, smb_authreq_t *authreq)
{
	smbd_session_t	*session;
	uint32_t	rc;

	authreq->au_session = smbd_auth_session_lookup(authreq->au_session_id);
	if (authreq->au_session == NULL)
		return (NT_STATUS_INTERNAL_ERROR);

	session = authreq->au_session;
	authreq->au_user = smbd_auth_user_lookup(session, authreq->au_user_id);
	if ((authreq->au_flags & SMB_AUTH_INIT) &&
	    (authreq->au_user != NULL)) {
		smbd_auth_user_remove(session, authreq->au_user_id);
		smbd_auth_user_release(session, authreq->au_user);
		authreq->au_user = NULL;
		syslog(LOG_DEBUG, "Stale user (uid=%u) removed",
		    authreq->au_user_id);
	}

	if (authreq->au_user == NULL)
		authreq->au_user = smbd_auth_user_create(session,
		    authreq->au_user_id);

	if (authreq->au_user == NULL) {
		smbd_auth_session_release(session);
		return (NT_STATUS_NO_MEMORY);
	}

	(void) mutex_lock(&authreq->au_user->u_mtx);
	authreq->au_user->u_phase_cnt++;
	(void) mutex_unlock(&authreq->au_user->u_mtx);

	/* invoke mechanism specific pre-auth op */
	rc = mech->m_pre_auth(authreq);
	if (rc != NT_STATUS_SUCCESS) {
		smbd_auth_user_remove(session, authreq->au_user_id);
		smbd_auth_user_release(session, authreq->au_user);
		smbd_auth_session_release(session);
		return (rc);
	}

	return (NT_STATUS_SUCCESS);
}

/*
 * Entry point for authenticating a local/domain user logon using the specified
 * mechanism. NT_STATUS_MORE_PROCESSING_REQUIRED is returned by mechanism
 * specific authentication routine to indicate subsequent extended security
 * exchange phases are required to complete the authentication.
 * NT_STATUS_LOGON_FAILURE is returned after a maximum number of phases is
 * reached.
 *
 * Parameter(s):
 *  authreq   - the information needed for user authentication.
 *
 * Return(s): - SMB authrsp (access token, security blob, authentication status)
 *
 * Note that
 *               both access token and the security blob can be NULL.
 *               If extended security is used, both the username and domain
 *               related fields of the passed 'authreq' will be set
 *               accordingly.
 */
static void
smbd_auth(smbd_mech_t *mech, smb_authreq_t *authreq, smb_authrsp_t *authrsp)
{
	authrsp->ar_status = mech->m_auth(authreq, authrsp);

	switch (authrsp->ar_status) {
	case NT_STATUS_MORE_PROCESSING_REQUIRED:
		(void) mutex_lock(&authreq->au_user->u_mtx);
		if (authreq->au_user->u_phase_cnt == SMBD_AUTH_MAX_PHASE)
			authrsp->ar_status = NT_STATUS_LOGON_FAILURE;
		(void) mutex_unlock(&authreq->au_user->u_mtx);

		/* authrsp->ar_secblob is set */
		return;

	case NT_STATUS_SUCCESS:
		if (smbd_token_setup_common(authrsp->ar_token))
			return;

		authrsp->ar_status = NT_STATUS_LOGON_FAILURE;
		break;

	case NT_STATUS_INVALID_PARAMETER:
		break;

	default:
		/*
		 * limit the NT status to be returned to the client.
		 * So far, I've only seen Windows server returns the following
		 * status on error:
		 *
		 * NT_STATUS_MORE_PROCESSING_REQUIRED
		 * NT_STATUS_INVALID_PARAMETER or
		 * NT_STATUS_LOGON_FAIURE
		 */
		authrsp->ar_status = NT_STATUS_LOGON_FAILURE;
		break;
	}
}

/*
 * Execute mechanism specific post-auth operation.
 * Start an audit session and audit the event.
 * Start an audit session and audit the event if this is the last phase of the
 * security exchange.
 * Add authome share if configured.
 * Remove the user object from the AVL if authentication fails.
 * Release the hold of both session and user objects. Free any memory
 * allocated in the pre-auth operation.
 */
static uint32_t
smbd_post_auth(smbd_mech_t *mech, smb_authreq_t *authreq,
    smb_authrsp_t *authrsp)
{
	uint32_t	rc = NT_STATUS_SUCCESS;
	char		*domain;
	char		*user;


	/* invoke mechanism specific post-auth op */
	rc = mech->m_post_auth(authreq, authrsp);

	if (authrsp->ar_status != NT_STATUS_MORE_PROCESSING_REQUIRED) {
		if (smbd_user_audit(authreq, authrsp->ar_token) != 0) {
			if (authrsp->ar_token != NULL) {
				domain = authrsp->ar_token->tkn_domain_name;
				user = authrsp->ar_token->tkn_account_name;
			} else {
				domain = authreq->au_edomain;
				user = authreq->au_eusername;

			}

			syslog(LOG_ERR, "[%s\\%s]: %s", domain, user,
			    xlate_nt_status(NT_STATUS_AUDIT_FAILED));
		}
	}

	if (authrsp->ar_token)
		smb_autohome_add(authrsp->ar_token);

	if ((authrsp->ar_status != NT_STATUS_SUCCESS) &&
	    (authrsp->ar_status != NT_STATUS_MORE_PROCESSING_REQUIRED)) {
		smbd_auth_user_remove(authreq->au_session, authreq->au_user_id);
	}

	smbd_auth_user_release(authreq->au_session, authreq->au_user);
	smbd_auth_session_release(authreq->au_session);
	return (rc);
}

/*
 * =========================
 * smbd session API (public)
 * =========================
 */

/*
 * Create session AVL tree. The memory allocated for the session AVL tree and
 * its nodes will be free'd by process exit.
 */
int
smbd_session_avl_init(void)
{
	smbd_session_avl = smb_avl_create(sizeof (smbd_session_t),
	    offsetof(smbd_session_t, s_node), &smbd_session_ops);

	if (smbd_session_avl == NULL)
		return (-1);

	return (0);
}

/*
 * When a new session is established, a session object is created and added to
 * the session AVL tree. A negotiation security blob will be setup only if
 * extended security is negotiated.
 *
 * NOTE: any stale session object with the same session ID will be removed
 * to account for ID recycle. The stale session should have been removed but
 * adding a check here anyway in case something goes wrong.
 */
void
smbd_session_connect(smb_sessionreq_t *request, smb_sessionrsp_t *response)
{
	gss_ctx_id_t	gssctx = GSS_C_NO_CONTEXT;
	smbd_session_t	key, *session;
	uint32_t	minor;

	bzero(&response->s_secblob, sizeof (smb_buf16_t));
	errno = 0;
	key.s_id = request->s_session_id;

	session = smb_avl_lookup(smbd_session_avl, &key);
	if (session != NULL) {
		smb_avl_remove(smbd_session_avl, &key);
		smb_avl_release(smbd_session_avl, session);
		syslog(LOG_DEBUG, "Stale session (id=%llu) removed",
		    request->s_session_id);
	}

	if ((request->s_extsec) &&
	    (smb_config_get_secmode() == SMB_SECMODE_DOMAIN))
		(void) smbd_auth_set_secblob(&gssctx, &response->s_secblob);

	session = smbd_session_alloc(request->s_session_id, gssctx);
	if (session == NULL) {
		if (gssctx != GSS_C_NO_CONTEXT)
			(void) gss_delete_sec_context(&minor, &gssctx,
			    GSS_C_NO_BUFFER);
		response->s_status = NT_STATUS_NO_MEMORY;
		return;
	}

	if (smb_avl_add(smbd_session_avl, session) == 0)
		response->s_status = NT_STATUS_SUCCESS;
	else
		response->s_status = NT_STATUS_UNSUCCESSFUL;
}

/*
 * Upon session teardown, the specified session will be removed from session
 * AVL tree.
 */
void
smbd_session_disconnect(uint64_t session_id)
{
	smbd_session_t key;

	key.s_id = session_id;
	smb_avl_remove(smbd_session_avl, &key);
}

void
smbd_user_cleanup(uint64_t session_id, uint16_t user_id)
{
	smbd_session_t	*session;
	smbd_user_t	*user;

	if ((session = smbd_auth_session_lookup(session_id)) == NULL)
		return;

	user = smbd_auth_user_lookup(session, user_id);
	if (user == NULL) {
		smbd_auth_session_release(session);
		return;
	}

	smbd_auth_user_remove(session, user_id);
	smbd_auth_user_release(session, user);
	smbd_auth_session_release(session);
}

/*
 * ==================================
 * smbd session AVL node operations
 * ==================================
 */

static int
smbd_session_cmp(const void *p1, const void *p2)
{
	smbd_session_t *s1 = (smbd_session_t *)p1;
	smbd_session_t *s2 = (smbd_session_t *)p2;
	int rc;

	assert(s1 != NULL);
	assert(s2 != NULL);
	rc = s1->s_id - s2->s_id;
	if (rc < 0)
		return (-1);

	if (rc > 0)
		return (1);

	return (0);
}

static int
smbd_session_add(const void *p)
{
	smbd_session_hold(p);
	return (0);
}

static void
smbd_session_remove(void *p)
{
	smbd_session_rele(p);
}

static void
smbd_session_hold(const void *p)
{
	smbd_session_t *s = (smbd_session_t *)p;

	SMBD_SESSION_VALID(s);
	(void) mutex_lock(&s->s_mtx);
	s->s_refcnt++;
	(void) mutex_unlock(&s->s_mtx);

}

static void
smbd_session_rele(const void *p)
{
	smbd_session_t	*s = (smbd_session_t *)p;
	boolean_t	destroy;

	SMBD_SESSION_VALID(s);
	(void) mutex_lock(&s->s_mtx);
	assert(s->s_refcnt > 0);
	s->s_refcnt--;
	destroy = (s->s_refcnt == 0);
	(void) mutex_unlock(&s->s_mtx);

	if (destroy)
		smbd_session_destroy(s);
}

/*
 * ==================================
 * smbd session functions
 * ==================================
 */
static smbd_session_t *
smbd_session_alloc(uint64_t id, gss_ctx_id_t gss_ctx)
{
	smbd_session_t *session;

	session = calloc(1, sizeof (smbd_session_t));
	if (session == NULL)
		return (NULL);

	session->s_magic = SMBD_SESSION_MAGIC;
	session->s_refcnt = 0;
	session->s_id = id;
	session->s_gss_ctx = gss_ctx;

	session->s_user_avl = smb_avl_create(sizeof (smbd_user_t),
	    offsetof(smbd_user_t, u_node), &smbd_user_ops);

	if (session->s_user_avl == NULL) {
		free(session);
		return (NULL);
	}

	return (session);
}

static void
smbd_session_destroy(void *p)
{
	smbd_session_t	*session = (smbd_session_t *)p;
	uint32_t	minor;

	SMBD_SESSION_VALID(session);
	assert(session->s_refcnt == 0);
	if (session->s_gss_ctx != GSS_C_NO_CONTEXT)
		(void) gss_delete_sec_context(&minor, &session->s_gss_ctx,
		    GSS_C_NO_BUFFER);

	smb_avl_flush(session->s_user_avl);
	smb_avl_destroy(session->s_user_avl);
	free(session);
}

/*
 * Look up a session given a session ID.
 */
static smbd_session_t *
smbd_auth_session_lookup(uint64_t session_id)
{
	smbd_session_t	skey;

	skey.s_id = session_id;
	return (smb_avl_lookup(smbd_session_avl, &skey));
}

/*
 * Release a reference on a session.
 */
static void
smbd_auth_session_release(smbd_session_t *session)
{
	SMBD_SESSION_VALID(session);
	smb_avl_release(smbd_session_avl, session);
}

/*
 * Generates an initial SPNEGO negotiation token which will be encoded as
 * security blob of the SmbNegotiate reply. This initial negotiation token
 * created here is a variation of the Negotiation Token specified in
 * RFC 4178: SPNEGO.
 *
 * For more info, please refer to "NegTokenInit2 Variation for
 * Server-Initiation" section of the MS-SPNG document.
 */
static boolean_t
smbd_auth_set_secblob(gss_ctx_id_t *gssctx, smb_buf16_t *blob)
{
	OM_uint32 major_stat, minor_stat;
	OM_uint32 ret_flags;
	gss_buffer_desc gss_itoken, gss_otoken;
	gss_OID mech_type = &spnego_oid_desc;

	bzero(&gss_itoken, sizeof (gss_buffer_desc));
	bzero(&gss_otoken, sizeof (gss_buffer_desc));

	smbd_gss_lock();
	major_stat = gss_accept_sec_context(&minor_stat, gssctx,
	    GSS_C_NO_CREDENTIAL, &gss_itoken, GSS_C_NO_CHANNEL_BINDINGS,
	    NULL, &mech_type, &gss_otoken, &ret_flags, NULL, NULL);
	smbd_gss_unlock();

	switch (major_stat) {
	/*
	 * Upon success, the NegTokenInit2 always returns
	 * GSS_S_CONTINUE_NEEDED as opposed to GSS_S_COMPLETE.
	 */
	case GSS_S_CONTINUE_NEEDED:
		if (gss_otoken.length > 0) {
			blob->val = gss_otoken.value;
			blob->len = (uint16_t)gss_otoken.length;
			return (B_TRUE);
		}
		break;

	default:
		smbd_gss_log("Unable to generate negotiation blob",
		    mech_type, major_stat, minor_stat);
		break;
	}

	return (B_FALSE);
}

/*
 * ==================================
 * smbd user AVL node operations
 * ==================================
 */

static int
smbd_user_cmp(const void *p1, const void *p2)
{
	smbd_user_t *user1 = (smbd_user_t *)p1;
	smbd_user_t *user2 = (smbd_user_t *)p2;
	int rc;

	assert(user1 != NULL);
	assert(user2 != NULL);
	rc = user1->u_id - user2->u_id;
	if (rc < 0)
		return (-1);

	if (rc > 0)
		return (1);

	return (0);
}

static int
smbd_user_add(const void *p)
{
	smbd_user_hold(p);
	return (0);
}

static void
smbd_user_remove(void *p)
{
	smbd_user_rele(p);
}

static void
smbd_user_hold(const void *p)
{
	smbd_user_t *user = (smbd_user_t *)p;

	SMBD_USER_VALID(user);
	(void) mutex_lock(&user->u_mtx);
	user->u_refcnt++;
	(void) mutex_unlock(&user->u_mtx);

}

static void
smbd_user_rele(const void *p)
{
	smbd_user_t	*user = (smbd_user_t *)p;
	boolean_t	destroy;

	SMBD_USER_VALID(user);

	(void) mutex_lock(&user->u_mtx);
	assert(user->u_refcnt > 0);
	user->u_refcnt--;
	destroy = (user->u_refcnt == 0);
	(void) mutex_unlock(&user->u_mtx);

	if (destroy)
		smbd_user_destroy(user);
}

/*
 * ==================================
 * smbd user functions
 * ==================================
 */
static smbd_user_t *
smbd_user_alloc(uint16_t uid, gss_ctx_id_t gss_ctx)
{
	smbd_user_t *user;

	user = calloc(1, sizeof (smbd_user_t));
	if (user == NULL)
		return (NULL);

	user->u_magic = SMBD_USER_MAGIC;
	user->u_refcnt = 0;
	user->u_phase_cnt = 0;
	user->u_id = uid;
	user->u_gss_ctx = gss_ctx;
	return (user);
}

static void
smbd_user_destroy(void *p)
{
	smbd_user_t	*user = (smbd_user_t *)p;
	uint32_t	minor;

	SMBD_USER_VALID(user);
	assert(user->u_refcnt == 0);

	if (user->u_gss_ctx != GSS_C_NO_CONTEXT)
		(void) gss_delete_sec_context(&minor, &user->u_gss_ctx,
		    GSS_C_NO_BUFFER);
	free(user);
}

/*
 * Create a user object given a reference to the associated session and user
 * ID. Assign the GSS context kept in the session to the first user who
 * attempts to log in.
 */
static smbd_user_t *
smbd_auth_user_create(smbd_session_t *session, uint16_t user_id)
{
	smbd_user_t	*user, ukey;
	gss_ctx_id_t	gss_ctx = GSS_C_NO_CONTEXT;
	uint32_t	minor;

	(void) mutex_lock(&session->s_mtx);
	if (session->s_gss_ctx != GSS_C_NO_CONTEXT) {
		gss_ctx = session->s_gss_ctx;
		session->s_gss_ctx = GSS_C_NO_CONTEXT;
	}

	(void) mutex_unlock(&session->s_mtx);

	if ((user = smbd_user_alloc(user_id, gss_ctx)) == NULL) {
		if (gss_ctx != GSS_C_NO_CONTEXT)
			(void) gss_delete_sec_context(&minor, &gss_ctx,
			    GSS_C_NO_BUFFER);
		return (NULL);
	}

	if (smb_avl_add(session->s_user_avl, user) != 0) {
		if (gss_ctx != GSS_C_NO_CONTEXT)
			(void) gss_delete_sec_context(&minor, &gss_ctx,
			    GSS_C_NO_BUFFER);
		free(user);
		return (NULL);
	}

	ukey.u_id = user_id;

	user = smb_avl_lookup(session->s_user_avl, &ukey);
	assert(user != NULL);
	return (user);
}

static void
smbd_auth_user_remove(smbd_session_t *session, uint16_t user_id)
{
	smbd_user_t	ukey;

	SMBD_SESSION_VALID(session);
	ukey.u_id = user_id;
	smb_avl_remove(session->s_user_avl, &ukey);
}

/*
 * Look up a user given a reference to the associated session and a user ID.
 */
static smbd_user_t *
smbd_auth_user_lookup(smbd_session_t *session, uint16_t user_id)
{
	smbd_user_t	ukey;

	SMBD_SESSION_VALID(session);
	ukey.u_id = user_id;

	return (smb_avl_lookup(session->s_user_avl, &ukey));
}

/*
 * Release a reference on a user.
 */
static void
smbd_auth_user_release(smbd_session_t *session, smbd_user_t *user)
{
	SMBD_SESSION_VALID(session);
	SMBD_USER_VALID(user);
	smb_avl_release(session->s_user_avl, user);
}
