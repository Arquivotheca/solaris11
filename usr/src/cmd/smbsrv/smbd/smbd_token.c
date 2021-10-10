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
 * Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <unistd.h>
#include <strings.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <assert.h>
#include <synch.h>
#include <nss_dbdefs.h>
#include <sys/syslog.h>
#include <kerberosv5/krb5.h>

#include <smbsrv/libsmb.h>
#include <smbsrv/libntsvcs.h>
#include <smbsrv/smbinfo.h>
#include <smbsrv/smb_token.h>
#include <smbsrv/netrauth.h>
#include "smbd.h"

static smb_posix_grps_t *smbd_token_create_pxgrps(uid_t);
static smb_privset_t *smbd_token_create_privs(smb_token_t *);
static void smbd_token_set_owner(smb_token_t *);
static void smbd_token_set_flags(smb_token_t *);
static int smbd_token_sids2ids(smb_token_t *);
static boolean_t smbd_token_is_member(smb_token_t *, smb_sid_t *);
static uint32_t smbd_token_setup_wingrps(smb_token_t *, const smb_authinfo_t *);
static uint32_t smbd_token_setup_local_wingrps(smb_token_t *);
static uint32_t smbd_token_setup_domain_wingrps(smb_ids_t *,
    const smb_authinfo_t *);

/* Consolidation private function from Network Repository */
extern int _getgroupsbymember(const char *, gid_t[], int, int);

/*
 * Decode: flat buffer -> structure
 */
smb_authreq_t *
smbd_authreq_decode(uint8_t *buf, uint32_t len)
{
	smb_authreq_t	*obj;
	XDR		xdrs;

	xdrmem_create(&xdrs, (const caddr_t)buf, len, XDR_DECODE);

	if ((obj = calloc(1, sizeof (smb_authreq_t))) == NULL) {
		syslog(LOG_ERR, "smbd_authreq_decode: %m");
		xdr_destroy(&xdrs);
		return (NULL);
	}

	if (!smb_authreq_xdr(&xdrs, obj)) {
		syslog(LOG_ERR, "smbd_authreq_decode: XDR decode error");
		xdr_free(smb_authreq_xdr, (char *)obj);
		free(obj);
		obj = NULL;
	}

	xdr_destroy(&xdrs);
	return (obj);
}

void
smbd_authreq_free(smb_authreq_t *authreq)
{
	xdr_free(smb_authreq_xdr, (char *)authreq);
	if (authreq->au_krb5ctx != NULL) {
		if (authreq->au_pac != NULL)
			krb5_pac_free(authreq->au_krb5ctx, authreq->au_pac);
		krb5_free_context(authreq->au_krb5ctx);
	}
	free(authreq);
}

void
smbd_authrsp_free(smb_authrsp_t *authrsp)
{
	assert(authrsp != NULL);
	smbd_token_free(authrsp->ar_token);
	free(authrsp->ar_secblob.val);
	bzero(authrsp, sizeof (authrsp));
}

/*
 * Encode: structure -> flat buffer (buffer size)
 * Pre-condition: obj is non-null.
 */
uint8_t *
smbd_authrsp_encode(smb_authrsp_t *obj, uint32_t *len)
{
	uint8_t *buf;
	XDR xdrs;

	if (!obj) {
		syslog(LOG_ERR, "smbd_authrsp_encode: invalid parameter");
		return (NULL);
	}

	*len = xdr_sizeof(smb_authrsp_xdr, obj);
	buf = (uint8_t *)malloc(*len);
	if (!buf) {
		syslog(LOG_ERR, "smbd_authrsp_encode: %m");
		return (NULL);
	}

	xdrmem_create(&xdrs, (const caddr_t)buf, *len, XDR_ENCODE);

	if (!smb_authrsp_xdr(&xdrs, obj)) {
		syslog(LOG_ERR, "smbd_authrsp_encode: XDR encode error");
		*len = 0;
		free(buf);
		buf = NULL;
	}

	xdr_destroy(&xdrs);
	return (buf);
}

smb_token_t *
smbd_token_alloc(void)
{
	return (calloc(1, sizeof (smb_token_t)));
}

void
smbd_token_free(smb_token_t *token)
{
	if (token != NULL) {
		smbd_token_cleanup(token);
		free(token);
	}
}

/*
 * smbd_token_destroy
 *
 * Release all of the memory associated with a token structure. Ensure
 * that the token has been unlinked before calling.
 */
void
smbd_token_cleanup(smb_token_t *token)
{
	if (token != NULL) {
		smb_sid_free(token->tkn_user.i_sid);
		smb_sid_free(token->tkn_owner.i_sid);
		smb_sid_free(token->tkn_primary_grp.i_sid);
		smb_ids_free(&token->tkn_win_grps);
		smb_privset_free(token->tkn_privileges);
		free(token->tkn_posix_grps);
		free(token->tkn_account_name);
		free(token->tkn_domain_name);
		free(token->tkn_posix_name);
		smb_session_key_destroy(&token->tkn_session_key);
		bzero(token, sizeof (smb_token_t));
	}
}

/*
 * Common token setup for both local and domain users.
 * This function must be called after the initial setup
 * has been done.
 *
 * Note that the order of calls in this function are important.
 */
boolean_t
smbd_token_setup_common(smb_token_t *token)
{
	struct passwd	pw;
	char		buf[NSS_LINELEN_PASSWD];
	uid_t		uid;

	smbd_token_set_flags(token);

	smbd_token_set_owner(token);
	if (token->tkn_owner.i_sid == NULL)
		return (B_FALSE);

	/* Privileges */
	token->tkn_privileges = smbd_token_create_privs(token);
	if (token->tkn_privileges == NULL)
		return (B_FALSE);

	if (smbd_token_sids2ids(token) != 0) {
		syslog(LOG_ERR, "%s\\%s: idmap failed",
		    token->tkn_domain_name, token->tkn_account_name);
		return (B_FALSE);
	}

	/* Solaris Groups */
	token->tkn_posix_grps = smbd_token_create_pxgrps(token->tkn_user.i_id);

	uid = token->tkn_user.i_id;

	if (!IDMAP_ID_IS_EPHEMERAL(uid)) {
		if (getpwuid_r(uid, &pw, buf, sizeof (buf)) == NULL) {
			syslog(LOG_WARNING,
			    "failed to get the name of UID=%u: %m", uid);
		}
		token->tkn_posix_name = strdup(pw.pw_name);
	}

	return (smb_token_valid(token));
}

uint32_t
smbd_token_setup_domain(smb_token_t *token, const smb_authinfo_t *authinfo)
{
	smb_sid_t *domsid;
	uint32_t status;

	domsid = (smb_sid_t *)authinfo->a_domainsid;

	token->tkn_user.i_sid = smb_sid_splice(domsid, authinfo->a_usrrid);
	if (token->tkn_user.i_sid == NULL)
		return (NT_STATUS_NO_MEMORY);

	token->tkn_primary_grp.i_sid = smb_sid_splice(domsid,
	    authinfo->a_grprid);
	if (token->tkn_primary_grp.i_sid == NULL)
		return (NT_STATUS_NO_MEMORY);

	token->tkn_account_name = strdup(authinfo->a_usrname);
	token->tkn_domain_name = strdup(authinfo->a_domainname);

	if (token->tkn_account_name == NULL || token->tkn_domain_name == NULL)
		return (NT_STATUS_NO_MEMORY);

	status = smbd_token_setup_wingrps(token, authinfo);
	if (status != NT_STATUS_SUCCESS)
		return (status);

	if ((status = smb_session_key_create(&token->tkn_session_key,
	    authinfo->a_usersesskey.val, authinfo->a_usersesskey.len))
	    != NT_STATUS_SUCCESS)
		return (status);

	return (NT_STATUS_SUCCESS);
}

/*
 * Setup an access token for the specified local user.
 */
uint32_t
smbd_token_setup_local(smb_passwd_t *smbpw, smb_token_t *token)
{
	idmap_stat stat;
	smb_idmap_batch_t sib;
	smb_idmap_t *umap, *gmap;
	struct passwd pw;
	char pwbuf[1024];
	char nbname[NETBIOS_NAME_SZ];

	(void) smb_getnetbiosname(nbname, sizeof (nbname));
	token->tkn_account_name = strdup(smbpw->pw_name);
	token->tkn_domain_name = strdup(nbname);

	if (token->tkn_account_name == NULL ||
	    token->tkn_domain_name == NULL)
		return (NT_STATUS_NO_MEMORY);

	if (getpwuid_r(smbpw->pw_uid, &pw, pwbuf, sizeof (pwbuf)) == NULL)
		return (NT_STATUS_NO_SUCH_USER);

	/* Get the SID for user's uid & gid */
	stat = smb_idmap_batch_create(&sib, 2, SMB_IDMAP_ID2SID);
	if (stat != IDMAP_SUCCESS)
		return (NT_STATUS_INTERNAL_ERROR);

	umap = &sib.sib_maps[0];
	stat = smb_idmap_batch_getsid(sib.sib_idmaph, umap, pw.pw_uid,
	    SMB_IDMAP_USER);

	if (stat != IDMAP_SUCCESS) {
		smb_idmap_batch_destroy(&sib);
		return (NT_STATUS_INTERNAL_ERROR);
	}

	gmap = &sib.sib_maps[1];
	stat = smb_idmap_batch_getsid(sib.sib_idmaph, gmap, pw.pw_gid,
	    SMB_IDMAP_GROUP);

	if (stat != IDMAP_SUCCESS) {
		smb_idmap_batch_destroy(&sib);
		return (NT_STATUS_INTERNAL_ERROR);
	}

	if (smb_idmap_batch_getmappings(&sib) != IDMAP_SUCCESS) {
		smb_idmap_batch_destroy(&sib);
		return (NT_STATUS_INTERNAL_ERROR);
	}

	token->tkn_user.i_sid = smb_sid_dup(umap->sim_sid);
	token->tkn_primary_grp.i_sid = smb_sid_dup(gmap->sim_sid);

	smb_idmap_batch_destroy(&sib);

	if (token->tkn_user.i_sid == NULL ||
	    token->tkn_primary_grp.i_sid == NULL)
		return (NT_STATUS_NO_MEMORY);

	return (smbd_token_setup_local_wingrps(token));
}

/*
 * Setup an access token for a guest connection.
 */
uint32_t
smbd_token_setup_guest(smb_account_t *guest, smb_token_t *token)
{
	static smb_account_t	domain_users;
	static mutex_t		domain_users_mutex;
	uint32_t		status;

	(void) mutex_lock(&domain_users_mutex);
	if (domain_users.a_sid == NULL) {
		status = smb_sam_lookup_name(NULL, "domain users",
		    SidTypeGroup, &domain_users);

		if (status != NT_STATUS_SUCCESS) {
			smb_account_free(&domain_users);
			(void) mutex_unlock(&domain_users_mutex);
			return (NT_STATUS_NO_MEMORY);
		}
	}
	(void) mutex_unlock(&domain_users_mutex);

	token->tkn_account_name = strdup(guest->a_name);
	token->tkn_domain_name = strdup(guest->a_domain);
	token->tkn_user.i_sid = smb_sid_dup(guest->a_sid);
	token->tkn_primary_grp.i_sid = smb_sid_dup(domain_users.a_sid);
	token->tkn_flags = SMB_ATF_GUEST;

	if (token->tkn_account_name == NULL ||
	    token->tkn_domain_name == NULL ||
	    token->tkn_user.i_sid == NULL ||
	    token->tkn_primary_grp.i_sid == NULL)
		return (NT_STATUS_NO_MEMORY);

	return (smbd_token_setup_local_wingrps(token));
}

/*
 * Setup access token for anonymous connections
 */
uint32_t
smbd_token_setup_anon(smb_token_t *token)
{
	smb_sid_t *user_sid;

	token->tkn_account_name = strdup("Anonymous");
	token->tkn_domain_name = strdup("NT Authority");
	user_sid = smb_wka_get_sid("Anonymous");
	token->tkn_user.i_sid = smb_sid_dup(user_sid);
	token->tkn_primary_grp.i_sid = smb_sid_dup(user_sid);
	token->tkn_flags = SMB_ATF_ANON;

	if (token->tkn_account_name == NULL ||
	    token->tkn_domain_name == NULL ||
	    token->tkn_user.i_sid == NULL ||
	    token->tkn_primary_grp.i_sid == NULL)
		return (NT_STATUS_NO_MEMORY);

	return (smbd_token_setup_local_wingrps(token));
}

/*
 * Try both LM hash and NT hashes with user's password(s) to authenticate
 * the user.
 */
uint32_t
smbd_token_auth_local(smb_authreq_t *authreq, smb_token_t *token,
    smb_passwd_t *smbpw)
{
	boolean_t lm_ok, nt_ok;
	uint32_t status = NT_STATUS_SUCCESS;
	uint8_t ssnkey[SMBAUTH_SESSION_KEY_SZ];

	if (smb_pwd_getpwnam(authreq->au_eusername, smbpw) == NULL)
		return (NT_STATUS_NO_SUCH_USER);

	if (smbpw->pw_flags & SMB_PWF_DISABLE)
		return (NT_STATUS_ACCOUNT_DISABLED);

	nt_ok = lm_ok = B_FALSE;
	if ((smbpw->pw_flags & SMB_PWF_LM) &&
	    (authreq->au_lmpasswd.len != 0)) {
		lm_ok = smb_auth_validate_lm(
		    authreq->au_challenge_key.val,
		    authreq->au_challenge_key.len,
		    smbpw,
		    authreq->au_lmpasswd.val,
		    authreq->au_lmpasswd.len,
		    authreq->au_domain,
		    authreq->au_username);
		bzero(&token->tkn_session_key, sizeof (smb_session_key_t));
	}

	if (!lm_ok && (authreq->au_ntpasswd.len != 0)) {
		nt_ok = smb_auth_validate_nt(
		    authreq->au_challenge_key.val,
		    authreq->au_challenge_key.len,
		    smbpw,
		    authreq->au_ntpasswd.val,
		    authreq->au_ntpasswd.len,
		    authreq->au_domain,
		    authreq->au_username,
		    /* client nonce for NTLM2 session security */
		    authreq->au_lmpasswd.val,
		    8,
		    ssnkey);

		if (nt_ok) {
			if ((status = smb_session_key_create(
			    &token->tkn_session_key, ssnkey,
			    SMBAUTH_SESSION_KEY_SZ)) != NT_STATUS_SUCCESS)
				return (status);
		}
	}

	if (!nt_ok && !lm_ok) {
		status = NT_STATUS_LOGON_FAILURE;
		syslog(LOG_NOTICE, "logon[%s\\%s]: %s",
		    authreq->au_edomain, authreq->au_eusername,
		    xlate_nt_status(status));
	}

	return (status);
}

/*
 * smbd_token_log
 *
 * Diagnostic routine to write the contents of a token to the log.
 */
void
smbd_token_log(smb_token_t *token)
{
	smb_ids_t *w_grps;
	smb_id_t *grp;
	smb_posix_grps_t *x_grps;
	char sidstr[SMB_SID_STRSZ];
	int i;

	if (token == NULL)
		return;

	syslog(LOG_DEBUG, "Token for %s\\%s",
	    (token->tkn_domain_name) ? token->tkn_domain_name : "-NULL-",
	    (token->tkn_account_name) ? token->tkn_account_name : "-NULL-");

	syslog(LOG_DEBUG, "   User->Attr: %d", token->tkn_user.i_attrs);
	smb_sid_tostr((smb_sid_t *)token->tkn_user.i_sid, sidstr);
	syslog(LOG_DEBUG, "   User->Sid: %s (id=%u)", sidstr,
	    token->tkn_user.i_id);

	smb_sid_tostr((smb_sid_t *)token->tkn_owner.i_sid, sidstr);
	syslog(LOG_DEBUG, "   Ownr->Sid: %s (id=%u)",
	    sidstr, token->tkn_owner.i_id);

	smb_sid_tostr((smb_sid_t *)token->tkn_primary_grp.i_sid, sidstr);
	syslog(LOG_DEBUG, "   PGrp->Sid: %s (id=%u)",
	    sidstr, token->tkn_primary_grp.i_id);

	w_grps = &token->tkn_win_grps;
	if (w_grps->i_ids) {
		syslog(LOG_DEBUG, "   Windows groups: %d", w_grps->i_cnt);
		grp = w_grps->i_ids;
		for (i = 0; i < w_grps->i_cnt; ++i, grp++) {
			syslog(LOG_DEBUG,
			    "    Grp[%d].Attr:%d", i, grp->i_attrs);
			if (grp->i_sid != NULL) {
				smb_sid_tostr((smb_sid_t *)grp->i_sid, sidstr);
				syslog(LOG_DEBUG,
				    "    Grp[%d].Sid: %s (id=%u)", i, sidstr,
				    grp->i_id);
			}
		}
	} else {
		syslog(LOG_DEBUG, "   No Windows groups");
	}

	x_grps = token->tkn_posix_grps;
	if (x_grps) {
		syslog(LOG_DEBUG, "   Solaris groups: %d", x_grps->pg_ngrps);
		for (i = 0; i < x_grps->pg_ngrps; i++)
			syslog(LOG_DEBUG, "    %u", x_grps->pg_grps[i]);
	} else {
		syslog(LOG_DEBUG, "   No Solaris groups");
	}

	if (token->tkn_privileges)
		smb_privset_log(token->tkn_privileges);
	else
		syslog(LOG_DEBUG, "   No privileges");
}

static idmap_stat
smbd_token_idmap(smb_token_t *token, smb_idmap_batch_t *sib)
{
	idmap_stat stat;
	smb_idmap_t *sim;
	smb_id_t *id;
	int i;

	if (!token || !sib)
		return (IDMAP_ERR_ARG);

	sim = sib->sib_maps;

	if (token->tkn_flags & SMB_ATF_ANON) {
		token->tkn_user.i_id = UID_NOBODY;
		token->tkn_owner.i_id = UID_NOBODY;
	} else {
		/* User SID */
		id = &token->tkn_user;
		sim->sim_id = &id->i_id;
		stat = smb_idmap_batch_getid(sib->sib_idmaph, sim++,
		    id->i_sid, SMB_IDMAP_USER);

		if (stat != IDMAP_SUCCESS)
			return (stat);

		/* Owner SID */
		id = &token->tkn_owner;
		sim->sim_id = &id->i_id;
		stat = smb_idmap_batch_getid(sib->sib_idmaph, sim++,
		    id->i_sid, SMB_IDMAP_USER);

		if (stat != IDMAP_SUCCESS)
			return (stat);
	}

	/* Primary Group SID */
	id = &token->tkn_primary_grp;
	sim->sim_id = &id->i_id;
	stat = smb_idmap_batch_getid(sib->sib_idmaph, sim++, id->i_sid,
	    SMB_IDMAP_GROUP);

	if (stat != IDMAP_SUCCESS)
		return (stat);

	/* Other Windows Group SIDs */
	for (i = 0; i < token->tkn_win_grps.i_cnt; i++, sim++) {
		id = &token->tkn_win_grps.i_ids[i];
		sim->sim_id = &id->i_id;
		stat = smb_idmap_batch_getid(sib->sib_idmaph, sim,
		    id->i_sid, SMB_IDMAP_GROUP);

		if (stat != IDMAP_SUCCESS)
			break;
	}

	return (stat);
}

/*
 * smbd_token_sids2ids
 *
 * This will map all the SIDs of the access token to UIDs/GIDs.
 *
 * Returns 0 upon success.  Otherwise, returns -1.
 */
static int
smbd_token_sids2ids(smb_token_t *token)
{
	idmap_stat stat;
	int nmaps;
	smb_idmap_batch_t sib;

	/*
	 * Number of idmap lookups: user SID, owner SID, primary group SID,
	 * and all Windows group SIDs. Skip user/owner SID for Anonymous.
	 */
	if (token->tkn_flags & SMB_ATF_ANON)
		nmaps = token->tkn_win_grps.i_cnt + 1;
	else
		nmaps = token->tkn_win_grps.i_cnt + 3;

	stat = smb_idmap_batch_create(&sib, nmaps, SMB_IDMAP_SID2ID);
	if (stat != IDMAP_SUCCESS)
		return (-1);

	stat = smbd_token_idmap(token, &sib);
	if (stat != IDMAP_SUCCESS) {
		smb_idmap_batch_destroy(&sib);
		return (-1);
	}

	stat = smb_idmap_batch_getmappings(&sib);
	smb_idmap_batch_destroy(&sib);
	smb_idmap_check("smb_idmap_batch_getmappings", stat);

	return (stat == IDMAP_SUCCESS ? 0 : -1);
}

/*
 * smbd_token_create_pxgrps
 *
 * Setup the POSIX group membership of the access token if the given UID is
 * a POSIX UID (non-ephemeral). Both the user's primary group and
 * supplementary groups will be added to the POSIX group array of the access
 * token.
 */
static smb_posix_grps_t *
smbd_token_create_pxgrps(uid_t uid)
{
	struct passwd *pwd;
	smb_posix_grps_t *pgrps;
	int ngroups_max, num;
	gid_t *gids;

	if ((ngroups_max = sysconf(_SC_NGROUPS_MAX)) < 0) {
		syslog(LOG_ERR, "smb_logon: failed to get _SC_NGROUPS_MAX");
		return (NULL);
	}

	pwd = getpwuid(uid);
	if (pwd == NULL) {
		pgrps = malloc(sizeof (smb_posix_grps_t));
		if (pgrps == NULL)
			return (NULL);

		pgrps->pg_ngrps = 0;
		return (pgrps);
	}

	if (pwd->pw_name == NULL) {
		pgrps = malloc(sizeof (smb_posix_grps_t));
		if (pgrps == NULL)
			return (NULL);

		pgrps->pg_ngrps = 1;
		pgrps->pg_grps[0] = pwd->pw_gid;
		return (pgrps);
	}

	gids = (gid_t *)malloc(ngroups_max * sizeof (gid_t));
	if (gids == NULL) {
		return (NULL);
	}
	bzero(gids, ngroups_max * sizeof (gid_t));

	gids[0] = pwd->pw_gid;

	/*
	 * Setup the groups starting at index 1 (the last arg)
	 * of gids array.
	 */
	num = _getgroupsbymember(pwd->pw_name, gids, ngroups_max, 1);

	if (num == -1) {
		syslog(LOG_ERR, "smb_logon: unable "
		    "to get user's supplementary groups");
		num = 1;
	}

	pgrps = (smb_posix_grps_t *)malloc(SMB_POSIX_GRPS_SIZE(num));
	if (pgrps) {
		pgrps->pg_ngrps = num;
		bcopy(gids, pgrps->pg_grps, num * sizeof (gid_t));
	}

	free(gids);
	return (pgrps);
}

static smb_privset_t *
smbd_token_create_privs(smb_token_t *token)
{
	smb_privset_t *privs;
	smb_giter_t gi;
	smb_group_t grp;
	int rc;

	privs = smb_privset_new();
	if (privs == NULL)
		return (NULL);

	if (smb_lgrp_iteropen(&gi) != SMB_LGRP_SUCCESS) {
		smb_privset_free(privs);
		return (NULL);
	}

	while (smb_lgrp_iterate(&gi, &grp) == SMB_LGRP_SUCCESS) {
		if (smb_lgrp_is_member(&grp, token->tkn_user.i_sid))
			smb_privset_merge(privs, grp.sg_privs);
		smb_lgrp_free(&grp);
	}
	smb_lgrp_iterclose(&gi);

	if (token->tkn_flags & SMB_ATF_ADMIN) {
		rc = smb_lgrp_getbyname("Administrators", &grp);
		if (rc == SMB_LGRP_SUCCESS) {
			smb_privset_merge(privs, grp.sg_privs);
			smb_lgrp_free(&grp);
		}

		/*
		 * This privilege is required to view/edit SACL
		 */
		smb_privset_enable(privs, SE_SECURITY_LUID);
	}

	return (privs);
}

/*
 * Token owner should be set to local Administrators group
 * in two cases:
 *   1. The logged on user is a member of Domain Admins group
 *   2. he/she is a member of local Administrators group
 */
static void
smbd_token_set_owner(smb_token_t *token)
{
#ifdef SMB_SUPPORT_GROUP_OWNER
	smb_sid_t *owner_sid;

	if (token->tkn_flags & SMB_ATF_ADMIN) {
		owner_sid = smb_wka_get_sid("Administrators");
		assert(owner_sid);
	} else {
		owner_sid = token->tkn_user->i_sid;
	}

	token->tkn_owner.i_sid = smb_sid_dup(owner_sid);
#endif
	token->tkn_owner.i_sid = smb_sid_dup(token->tkn_user.i_sid);
}

static void
smbd_token_set_flags(smb_token_t *token)
{
	if (smbd_token_is_member(token, smb_wka_get_sid("Administrators")))
		token->tkn_flags |= SMB_ATF_ADMIN;

	if (smbd_token_is_member(token, smb_wka_get_sid("Power Users")))
		token->tkn_flags |= SMB_ATF_POWERUSER;

	if (smbd_token_is_member(token, smb_wka_get_sid("Backup Operators")))
		token->tkn_flags |= SMB_ATF_BACKUPOP;
}

/*
 * smbd_token_user_sid
 *
 * Return a pointer to the user SID in the specified token. A null
 * pointer indicates an error.
 */
static smb_sid_t *
smbd_token_user_sid(smb_token_t *token)
{
	return ((token) ? token->tkn_user.i_sid : NULL);
}

/*
 * smbd_token_group_sid
 *
 * Return a pointer to the group SID as indicated by the iterator.
 * Setting the iterator to 0 before calling this function will return
 * the first group, which will always be the primary group. The
 * iterator will be incremented before returning the SID so that this
 * function can be used to cycle through the groups. The caller can
 * adjust the iterator as required between calls to obtain any specific
 * group.
 *
 * On success a pointer to the appropriate group SID will be returned.
 * Otherwise a null pointer will be returned.
 */
static smb_sid_t *
smbd_token_group_sid(smb_token_t *token, int *iterator)
{
	int index;

	if (token == NULL || iterator == NULL)
		return (NULL);

	if (token->tkn_win_grps.i_ids == NULL)
		return (NULL);

	index = *iterator;

	if (index < 0 || index >= token->tkn_win_grps.i_cnt)
		return (NULL);

	++(*iterator);
	return (token->tkn_win_grps.i_ids[index].i_sid);
}

/*
 * smbd_token_is_member
 *
 * This function will determine whether or not the specified SID is a
 * member of a token. The user SID and all group SIDs are tested.
 * Returns 1 if the SID is a member of the token. Otherwise returns 0.
 */
static boolean_t
smbd_token_is_member(smb_token_t *token, smb_sid_t *sid)
{
	smb_sid_t *tsid;
	int iterator = 0;

	if (token == NULL || sid == NULL)
		return (B_FALSE);

	tsid = smbd_token_user_sid(token);
	while (tsid) {
		if (smb_sid_cmp(tsid, sid))
			return (B_TRUE);

		tsid = smbd_token_group_sid(token, &iterator);
	}

	return (B_FALSE);
}

/*
 * Sets up domain, local and well-known group membership for the given
 * token. Two assumptions have been made here:
 *
 *   a) token already contains a valid user SID so that group
 *      memberships can be established
 *
 *   b) token belongs to a domain user
 */
static uint32_t
smbd_token_setup_wingrps(smb_token_t *token, const smb_authinfo_t *authinfo)
{
	smb_ids_t tkn_grps;
	uint32_t status;

	tkn_grps.i_cnt = 0;
	tkn_grps.i_ids = NULL;

	status = smbd_token_setup_domain_wingrps(&tkn_grps, authinfo);
	if (status != NT_STATUS_SUCCESS) {
		smb_ids_free(&tkn_grps);
		return (status);
	}

	status = smb_sam_usr_groups(token->tkn_user.i_sid, &tkn_grps);
	if (status != NT_STATUS_SUCCESS) {
		smb_ids_free(&tkn_grps);
		return (status);
	}

	if (netr_isadmin(authinfo))
		token->tkn_flags |= SMB_ATF_ADMIN;

	status = smb_wka_token_groups(token->tkn_flags, &tkn_grps);
	if (status == NT_STATUS_SUCCESS)
		token->tkn_win_grps = tkn_grps;
	else
		smb_ids_free(&tkn_grps);

	return (status);
}

/*
 * Converts groups information in the returned structure by domain controller
 * to an internal representation (gids)
 */
static uint32_t
smbd_token_setup_domain_wingrps(smb_ids_t *gids, const smb_authinfo_t *authinfo)
{
	smb_sid_t *domain_sid;
	smb_id_t *ids;
	int i, total_cnt;

	if ((i = authinfo->a_grpcnt) == 0)
		i++;
	i += authinfo->a_sidcnt;
	i += authinfo->a_resgrpcnt;

	total_cnt = gids->i_cnt + i;

	gids->i_ids = realloc(gids->i_ids, total_cnt * sizeof (smb_id_t));
	if (gids->i_ids == NULL)
		return (NT_STATUS_NO_MEMORY);

	domain_sid = (smb_sid_t *)authinfo->a_domainsid;

	ids = gids->i_ids + gids->i_cnt;
	for (i = 0; i < authinfo->a_grpcnt; i++, gids->i_cnt++, ids++) {
		ids->i_sid = smb_sid_splice(domain_sid,
		    authinfo->a_grps[i].rid);
		if (ids->i_sid == NULL)
			return (NT_STATUS_NO_MEMORY);

		ids->i_attrs = authinfo->a_grps[i].attributes;
	}

	if (authinfo->a_grpcnt == 0) {
		/*
		 * if there's no global group should add the primary group.
		 */
		ids->i_sid = smb_sid_splice(domain_sid, authinfo->a_grprid);
		if (ids->i_sid == NULL)
			return (NT_STATUS_NO_MEMORY);

		ids->i_attrs = 0x7;
		gids->i_cnt++;
		ids++;
	}

	/* Add the extra SIDs */
	for (i = 0; i < authinfo->a_sidcnt; i++, gids->i_cnt++, ids++) {
		ids->i_sid = smb_sid_dup(
		    (smb_sid_t *)authinfo->a_extra_sids[i].sid);
		if (ids->i_sid == NULL)
			return (NT_STATUS_NO_MEMORY);

		ids->i_attrs = authinfo->a_extra_sids[i].attributes;
	}

	/* Add resource groups */
	domain_sid = (smb_sid_t *)authinfo->a_resgrp_domainsid;
	if (domain_sid != NULL) {
		for (i = 0; i < authinfo->a_resgrpcnt;
		    i++, gids->i_cnt++, ids++) {
			ids->i_sid = smb_sid_splice(domain_sid,
			    authinfo->a_resgrps[i].rid);
			if (ids->i_sid == NULL)
				return (NT_STATUS_NO_MEMORY);

			ids->i_attrs = authinfo->a_resgrps[i].attributes;
		}
	}

	return (NT_STATUS_SUCCESS);
}

/*
 * Sets up local and well-known group membership for the given
 * token. Two assumptions have been made here:
 *
 *   a) token already contains a valid user SID so that group
 *      memberships can be established
 *
 *   b) token belongs to a local or anonymous user
 */
static uint32_t
smbd_token_setup_local_wingrps(smb_token_t *token)
{
	smb_ids_t tkn_grps;
	uint32_t status;

	/*
	 * We always want the user's primary group in the list
	 * of groups.
	 */
	tkn_grps.i_cnt = 1;
	if ((tkn_grps.i_ids = malloc(sizeof (smb_id_t))) == NULL)
		return (NT_STATUS_NO_MEMORY);

	tkn_grps.i_ids->i_sid = smb_sid_dup(token->tkn_primary_grp.i_sid);
	tkn_grps.i_ids->i_attrs = token->tkn_primary_grp.i_attrs;
	if (tkn_grps.i_ids->i_sid == NULL) {
		smb_ids_free(&tkn_grps);
		return (NT_STATUS_NO_MEMORY);
	}

	status = smb_sam_usr_groups(token->tkn_user.i_sid, &tkn_grps);
	if (status != NT_STATUS_SUCCESS) {
		smb_ids_free(&tkn_grps);
		return (status);
	}

	status = smb_wka_token_groups(token->tkn_flags, &tkn_grps);
	if (status != NT_STATUS_SUCCESS) {
		smb_ids_free(&tkn_grps);
		return (status);
	}

	token->tkn_win_grps = tkn_grps;
	return (status);
}
