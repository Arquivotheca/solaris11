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
 * Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <strings.h>
#include <smbsrv/libsmb.h>

extern int smb_pwd_num(void);
extern int smb_lgrp_numbydomain(smb_domain_type_t, int *);

static uint32_t smb_sam_lookup_user(char *, smb_sid_t **);
static uint32_t smb_sam_lookup_group(char *, smb_sid_t **);

/*
 * Local well-known accounts data structure table and prototypes
 */
typedef struct smb_lwka {
	uint32_t	lwka_rid;
	char		*lwka_name;
	uint16_t	lwka_type;
} smb_lwka_t;

static smb_lwka_t lwka_tbl[] = {
	{ 500, "Administrator", SidTypeUser },
	{ 501, "Guest", SidTypeUser },
	{ 502, "KRBTGT", SidTypeUser },
	{ 512, "Domain Admins", SidTypeGroup },
	{ 513, "Domain Users", SidTypeGroup },
	{ 514, "Domain Guests", SidTypeGroup },
	{ 516, "Domain Controllers", SidTypeGroup },
	{ 517, "Cert Publishers", SidTypeGroup },
	{ 518, "Schema Admins", SidTypeGroup },
	{ 519, "Enterprise Admins", SidTypeGroup },
	{ 520, "Global Policy Creator Owners", SidTypeGroup },
	{ 533, "RAS and IAS Servers", SidTypeGroup }
};

#define	SMB_LWKA_NUM	(sizeof (lwka_tbl)/sizeof (lwka_tbl[0]))

static smb_lwka_t *smb_lwka_lookup_name(char *);
static smb_lwka_t *smb_lwka_lookup_sid(smb_sid_t *);

/*
 * Looks up the given name in local account databases:
 *
 * SMB Local users are looked up in /var/smb/smbpasswd
 * SMB Local groups are looked up in /var/smb/smbgroup.db
 *
 * If the account is found, its information is populated
 * in the passed smb_account_t structure. Caller must free
 * allocated memories by calling smb_account_free() upon
 * successful return.
 *
 * The type of account is specified by 'type', which can be user,
 * alias (local group) or unknown. If the caller doesn't know
 * whether the name is a user or group name then SidTypeUnknown
 * should be passed.
 *
 * If a local user and group have the same name, the user will
 * always be picked. Note that this situation cannot happen on
 * Windows systems.
 *
 * If a SMB local user/group is found but it turns out that
 * it'll be mapped to a domain user/group the lookup is considered
 * failed and NT_STATUS_NONE_MAPPED is returned.
 *
 * Return status:
 *
 *   NT_STATUS_NOT_FOUND	This is not a local account
 *   NT_STATUS_NONE_MAPPED	It's a local account but cannot be
 *   				translated.
 *   other error status codes.
 */
uint32_t
smb_sam_lookup_name(char *domain, char *name, uint16_t type,
    smb_account_t *account)
{
	smb_domain_t di;
	smb_sid_t *sid;
	uint32_t status;
	smb_lwka_t *lwka;

	bzero(account, sizeof (smb_account_t));

	if (domain != NULL) {
		if (!smb_domain_lookup_name(domain, &di) ||
		    (di.di_type != SMB_DOMAIN_LOCAL))
			return (NT_STATUS_NOT_FOUND);

		/* Only Netbios hostname is accepted */
		if ((smb_strcasecmp(domain, di.di_nbname, 0) != 0) &&
		    (smb_strcasecmp(domain, di.di_fqname, 0) != 0))
			return (NT_STATUS_NONE_MAPPED);
	} else {
		if (!smb_domain_lookup_type(SMB_DOMAIN_LOCAL, &di))
			return (NT_STATUS_CANT_ACCESS_DOMAIN_INFO);
	}

	if (smb_strcasecmp(name, di.di_nbname, 0) == 0) {
		/* This is the local domain name */
		account->a_type = SidTypeDomain;
		account->a_name = strdup("");
		account->a_domain = strdup(di.di_nbname);
		account->a_sid = smb_sid_dup(di.di_binsid);
		account->a_domsid = smb_sid_dup(di.di_binsid);
		account->a_rid = (uint32_t)-1;

		if (!smb_account_validate(account)) {
			smb_account_free(account);
			return (NT_STATUS_NO_MEMORY);
		}

		return (NT_STATUS_SUCCESS);
	}

	if ((lwka = smb_lwka_lookup_name(name)) != NULL) {
		sid = smb_sid_splice(di.di_binsid, lwka->lwka_rid);
		type = lwka->lwka_type;
	} else {
		switch (type) {
		case SidTypeUser:
			status = smb_sam_lookup_user(name, &sid);
			if (status != NT_STATUS_SUCCESS)
				return (status);
			break;

		case SidTypeAlias:
			status = smb_sam_lookup_group(name, &sid);
			if (status != NT_STATUS_SUCCESS)
				return (status);
			break;

		case SidTypeUnknown:
			type = SidTypeUser;
			status = smb_sam_lookup_user(name, &sid);
			if (status == NT_STATUS_SUCCESS)
				break;

			if (status == NT_STATUS_NONE_MAPPED)
				return (status);

			type = SidTypeAlias;
			status = smb_sam_lookup_group(name, &sid);
			if (status != NT_STATUS_SUCCESS)
				return (status);
			break;

		default:
			return (NT_STATUS_INVALID_PARAMETER);
		}
	}

	account->a_name = strdup(name);
	account->a_sid = sid;
	account->a_domain = strdup(di.di_nbname);
	account->a_domsid = smb_sid_split(sid, &account->a_rid);
	account->a_type = type;

	if (!smb_account_validate(account)) {
		smb_account_free(account);
		return (NT_STATUS_NO_MEMORY);
	}

	return (NT_STATUS_SUCCESS);
}

/*
 * Looks up the given SID in local account databases:
 *
 * SMB Local users are looked up in /var/smb/smbpasswd
 * SMB Local groups are looked up in /var/smb/smbgroup.db
 *
 * If the account is found, its information is populated
 * in the passed smb_account_t structure. Caller must free
 * allocated memories by calling smb_account_free() upon
 * successful return.
 *
 * Return status:
 *
 *   NT_STATUS_NOT_FOUND	This is not a local account
 *   NT_STATUS_NONE_MAPPED	It's a local account but cannot be
 *   				translated.
 *   other error status codes.
 */
uint32_t
smb_sam_lookup_sid(smb_sid_t *sid, smb_account_t *account)
{
	char hostname[MAXHOSTNAMELEN];
	smb_passwd_t smbpw;
	smb_group_t grp;
	smb_lwka_t *lwka;
	smb_domain_t di;
	uint32_t rid;
	uid_t id;
	int id_type;
	int rc;

	bzero(account, sizeof (smb_account_t));

	if (!smb_domain_lookup_type(SMB_DOMAIN_LOCAL, &di))
		return (NT_STATUS_CANT_ACCESS_DOMAIN_INFO);

	if (smb_sid_cmp(sid, di.di_binsid)) {
		/* This is the local domain SID */
		account->a_type = SidTypeDomain;
		account->a_name = strdup("");
		account->a_domain = strdup(di.di_nbname);
		account->a_sid = smb_sid_dup(sid);
		account->a_domsid = smb_sid_dup(sid);
		account->a_rid = (uint32_t)-1;

		if (!smb_account_validate(account)) {
			smb_account_free(account);
			return (NT_STATUS_NO_MEMORY);
		}

		return (NT_STATUS_SUCCESS);
	}

	if (!smb_sid_indomain(di.di_binsid, sid)) {
		/* This is not a local SID */
		return (NT_STATUS_NOT_FOUND);
	}

	if ((lwka = smb_lwka_lookup_sid(sid)) != NULL) {
		account->a_type = lwka->lwka_type;
		account->a_name = strdup(lwka->lwka_name);
	} else {
		id_type = SMB_IDMAP_UNKNOWN;
		if (smb_idmap_getid(sid, &id, &id_type) != IDMAP_SUCCESS)
			return (NT_STATUS_NONE_MAPPED);

		switch (id_type) {
		case SMB_IDMAP_USER:
			account->a_type = SidTypeUser;
			if (smb_pwd_getpwuid(id, &smbpw) == NULL)
				return (NT_STATUS_NO_SUCH_USER);

			account->a_name = strdup(smbpw.pw_name);
			break;

		case SMB_IDMAP_GROUP:
			account->a_type = SidTypeAlias;
			(void) smb_sid_getrid(sid, &rid);
			rc = smb_lgrp_getbyrid(rid, SMB_DOMAIN_LOCAL, &grp);
			if (rc != SMB_LGRP_SUCCESS)
				return (NT_STATUS_NO_SUCH_ALIAS);

			account->a_name = strdup(grp.sg_name);
			smb_lgrp_free(&grp);
			break;

		default:
			return (NT_STATUS_NONE_MAPPED);
		}
	}

	if (smb_getnetbiosname(hostname, MAXHOSTNAMELEN) == 0)
		account->a_domain = strdup(hostname);
	account->a_sid = smb_sid_dup(sid);
	account->a_domsid = smb_sid_split(sid, &account->a_rid);

	if (!smb_account_validate(account)) {
		smb_account_free(account);
		return (NT_STATUS_NO_MEMORY);
	}

	return (NT_STATUS_SUCCESS);
}

/*
 * Returns number of SMB users, i.e. users who have entry
 * in /var/smb/smbpasswd
 */
int
smb_sam_usr_cnt(void)
{
	return (smb_pwd_num());
}

/*
 * Returns a list of local groups which the given user is
 * their member. A pointer to an array of smb_ids_t
 * structure is returned which must be freed by caller.
 */
uint32_t
smb_sam_usr_groups(smb_sid_t *user_sid, smb_ids_t *gids)
{
	smb_id_t *ids;
	smb_giter_t gi;
	smb_group_t lgrp;
	int total_cnt, gcnt;

	gcnt = 0;
	if (smb_lgrp_iteropen(&gi) != SMB_LGRP_SUCCESS)
		return (NT_STATUS_INTERNAL_ERROR);

	while (smb_lgrp_iterate(&gi, &lgrp) == SMB_LGRP_SUCCESS) {
		if (smb_lgrp_is_member(&lgrp, user_sid))
			gcnt++;
		smb_lgrp_free(&lgrp);
	}
	smb_lgrp_iterclose(&gi);

	if (gcnt == 0)
		return (NT_STATUS_SUCCESS);

	total_cnt = gids->i_cnt + gcnt;
	gids->i_ids = realloc(gids->i_ids, total_cnt * sizeof (smb_id_t));
	if (gids->i_ids == NULL)
		return (NT_STATUS_NO_MEMORY);

	if (smb_lgrp_iteropen(&gi) != SMB_LGRP_SUCCESS)
		return (NT_STATUS_INTERNAL_ERROR);

	ids = gids->i_ids + gids->i_cnt;
	while (smb_lgrp_iterate(&gi, &lgrp) == SMB_LGRP_SUCCESS) {
		if (gcnt == 0) {
			smb_lgrp_free(&lgrp);
			break;
		}
		if (smb_lgrp_is_member(&lgrp, user_sid)) {
			ids->i_sid = smb_sid_dup(lgrp.sg_id.gs_sid);
			if (ids->i_sid == NULL) {
				smb_lgrp_free(&lgrp);
				return (NT_STATUS_NO_MEMORY);
			}
			ids->i_attrs = lgrp.sg_attr;
			gids->i_cnt++;
			gcnt--;
			ids++;
		}
		smb_lgrp_free(&lgrp);
	}
	smb_lgrp_iterclose(&gi);

	return (NT_STATUS_SUCCESS);
}

/*
 * Returns the number of built-in or local groups stored
 * in /var/smb/smbgroup.db
 */
int
smb_sam_grp_cnt(smb_domain_type_t dtype)
{
	int grpcnt;
	int rc;

	switch (dtype) {
	case SMB_DOMAIN_BUILTIN:
		rc = smb_lgrp_numbydomain(SMB_DOMAIN_BUILTIN, &grpcnt);
		break;

	case SMB_DOMAIN_LOCAL:
		rc = smb_lgrp_numbydomain(SMB_DOMAIN_LOCAL, &grpcnt);
		break;

	default:
		rc = SMB_LGRP_INVALID_ARG;
	}

	return ((rc == SMB_LGRP_SUCCESS) ? grpcnt : 0);
}

/*
 * Determines whether the given SID is a member of the group
 * specified by gname.
 */
boolean_t
smb_sam_grp_ismember(const char *gname, smb_sid_t *sid)
{
	smb_group_t grp;
	boolean_t ismember = B_FALSE;

	if (smb_lgrp_getbyname((char *)gname, &grp) == SMB_LGRP_SUCCESS) {
		ismember = smb_lgrp_is_member(&grp, sid);
		smb_lgrp_free(&grp);
	}

	return (ismember);
}

/*
 * Frees memories allocated for the passed account fields.
 */
void
smb_account_free(smb_account_t *account)
{
	free(account->a_name);
	free(account->a_domain);
	smb_sid_free(account->a_sid);
	smb_sid_free(account->a_domsid);
	bzero(account, sizeof (smb_account_t));
}

/*
 * Validates the given account.
 */
boolean_t
smb_account_validate(smb_account_t *account)
{
	return ((account->a_name != NULL) && (account->a_sid != NULL) &&
	    (account->a_domain != NULL) && (account->a_domsid != NULL));
}

/*
 * Lookup local SMB user account database (/var/smb/smbpasswd)
 * if there's a match query its SID from idmap service and make
 * sure the SID is a local SID.
 *
 * The memory for the returned SID must be freed by the caller.
 */
static uint32_t
smb_sam_lookup_user(char *name, smb_sid_t **sid)
{
	smb_passwd_t smbpw;

	if (smb_pwd_getpwnam(name, &smbpw) == NULL)
		return (NT_STATUS_NO_SUCH_USER);

	if (smbpw.pw_flags & SMB_PWF_DISABLE)
		return (NT_STATUS_ACCOUNT_DISABLED);

	if (smb_idmap_getsid(smbpw.pw_uid, SMB_IDMAP_USER, sid)
	    != IDMAP_SUCCESS)
		return (NT_STATUS_NONE_MAPPED);

	if (!smb_sid_islocal(*sid)) {
		smb_sid_free(*sid);
		return (NT_STATUS_NONE_MAPPED);
	}

	return (NT_STATUS_SUCCESS);
}

/*
 * Lookup local SMB group account database (/var/smb/smbgroup.db)
 * The memory for the returned SID must be freed by the caller.
 */
static uint32_t
smb_sam_lookup_group(char *name, smb_sid_t **sid)
{
	smb_group_t grp;

	if (smb_lgrp_getbyname(name, &grp) != SMB_LGRP_SUCCESS)
		return (NT_STATUS_NO_SUCH_ALIAS);

	*sid = smb_sid_dup(grp.sg_id.gs_sid);
	smb_lgrp_free(&grp);

	return ((*sid == NULL) ? NT_STATUS_NO_MEMORY : NT_STATUS_SUCCESS);
}

static smb_lwka_t *
smb_lwka_lookup_name(char *name)
{
	int i;

	for (i = 0; i < SMB_LWKA_NUM; i++) {
		if (smb_strcasecmp(name, lwka_tbl[i].lwka_name, 0) == 0)
			return (&lwka_tbl[i]);
	}

	return (NULL);
}

static smb_lwka_t *
smb_lwka_lookup_sid(smb_sid_t *sid)
{
	uint32_t rid;
	int i;

	(void) smb_sid_getrid(sid, &rid);
	if (rid > 999)
		return (NULL);

	for (i = 0; i < SMB_LWKA_NUM; i++) {
		if (rid == lwka_tbl[i].lwka_rid)
			return (&lwka_tbl[i]);
	}

	return (NULL);
}
