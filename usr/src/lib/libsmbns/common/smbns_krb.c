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
 * Copyright 1990 by the Massachusetts Institute of Technology.
 * All Rights Reserved.
 *
 * Export of this software from the United States of America may
 *   require a specific license from the United States Government.
 *   It is the responsibility of any person or organization contemplating
 *   export to obtain such a license before exporting.
 *
 * WITHIN THAT CONSTRAINT, permission to use, copy, modify, and
 * distribute this software and its documentation for any purpose and
 * without fee is hereby granted, provided that the above copyright
 * notice appear in all copies and that both that copyright notice and
 * this permission notice appear in supporting documentation, and that
 * the name of M.I.T. not be used in advertising or publicity pertaining
 * to distribution of the software without specific, written prior
 * permission.  Furthermore if you modify this software you must label
 * your software as modified software and not distribute it in such a
 * fashion that it might be confused with the original M.I.T. software.
 * M.I.T. makes no representations about the suitability of
 * this software for any purpose.  It is provided "as is" without express
 * or implied warranty.
 *
 *
 * Initialize a credentials cache.
 */
#include <kerberosv5/krb5.h>
#include <kerberosv5/com_err.h>
#include <assert.h>
#include <stdio.h>
#include <syslog.h>
#include <strings.h>
#include <errno.h>
#include <libintl.h>

#include <smbsrv/libsmbns.h>
#include <smbns_krb.h>

#define	SMB_KRB5_CFG_DREALM		"default_realm"
#define	SMB_KRB5_CFG_KPASSWD_SRV	"kpasswd_server"
#define	SMB_KRB5_CFG_ADMIN_SRV		"admin_server"
#define	SMB_KRB5_CFG_KDC		"kdc"
#define	SMB_KRB5_CFG_VALID_RETRIES	20
#define	SMB_KRB5_LOGSIZE		2048
#define	SMB_KRB5_LOGNAME		"smbns_krb"

static smb_log_hdl_t loghd;
static void smb_krb5_cfg_free_list(char **);
static boolean_t smb_krb5_cfg_values_match(char **, char **);
static errcode_t smb_krb5_cfg_update_master(smb_krb5_cfg_t *, char *, size_t);

errcode_t
smb_krb5_cfg_init(smb_krb5_cfg_t *cfg, const char *fqdn,
    const char *master_kdc, char **kdcs)
{
	errcode_t rc;

	if (cfg == NULL || fqdn == NULL || kdcs == NULL)
		return (EINVAL);

	bzero(cfg, sizeof (smb_krb5_cfg_t));

	if ((rc = krb5_init_context(&cfg->kc_ctx)) != 0)
		return (rc);

	cfg->kc_path = smb_krb5_cfg_getpath();
	cfg->kc_exist = (access(cfg->kc_path, F_OK) == 0) ? B_TRUE : B_FALSE;

	rc = k5_profile_init(cfg->kc_path, &cfg->kc_profile);
	if (rc != 0) {
		(void) smb_krb5_cfg_fini(cfg, B_FALSE);
		return (rc);
	}

	if (cfg->kc_exist) {
		(void) k5_profile_get_default_realm(cfg->kc_profile,
		    &cfg->kc_orig_drealm);
	}

	cfg->kc_fqdomain = strdup(fqdn);
	cfg->kc_realm = smb_krb5_domain2realm(fqdn);
	cfg->kc_master_kdc = strdup(master_kdc);
	cfg->kc_kdcs = kdcs;

	if (cfg->kc_fqdomain == NULL || cfg->kc_realm == NULL ||
	    cfg->kc_master_kdc == NULL) {
		(void) smb_krb5_cfg_fini(cfg, B_FALSE);
		return (ENOMEM);
	}

	return (0);
}

/*
 * Commit changes to krb5.conf upon request and release any allocated
 * memory.
 */
errcode_t
smb_krb5_cfg_fini(smb_krb5_cfg_t *cfg, boolean_t commit)
{
	int i;
	errcode_t rc;

	if (cfg == NULL)
		return (0);

	if (commit) {
		if (cfg->kc_profile == NULL)
			return (EINVAL);

		if ((rc = k5_profile_release(cfg->kc_profile)) != 0)
			return (rc);
	} else {
		if (cfg->kc_profile != NULL)
			k5_profile_abandon(cfg->kc_profile);
	}

	if (cfg->kc_ctx != NULL)
		krb5_free_context(cfg->kc_ctx);

	free(cfg->kc_realm);
	free(cfg->kc_fqdomain);
	free(cfg->kc_master_kdc);

	if (cfg->kc_kdcs != NULL) {
		for (i = 0; cfg->kc_kdcs[i] != NULL; i++)
			free(cfg->kc_kdcs[i]);

		free(cfg->kc_kdcs);
	}

	return (0);
}

/*
 * If the existing krb5.conf doesn't contain any KDC entries, it probably
 * means that admins would like Solaris Kerberos to find KDCs all by itself
 * via DNS SRV RR lookups. In that case, no updates will be performed.
 * Otherwise, the set of KDC entries of the joined domain in krb5.conf will be
 * overwritten if a change is detected.
 */
void
smb_krb5_cfg_update_kdcs(void)
{
	profile_t	profile;
	char		fqdomain[MAXHOSTNAMELEN];
	char		*path, *realm;
	char		**kdcs1 = NULL, **kdcs2 = NULL;
	errcode_t 	rc;

	path = smb_krb5_cfg_getpath();

	if ((rc = k5_profile_init(path, &profile)) != 0) {
		smb_krb5_log_errmsg(NULL, rc,
		    "smbns_krb: failed to read %s", path);
		return;
	}

	if (smb_getdomainname_ad(fqdomain, MAXHOSTNAMELEN) != 0) {
		k5_profile_abandon(profile);
		return;
	}

	if ((realm = smb_krb5_domain2realm(fqdomain)) == NULL) {
		k5_profile_abandon(profile);
		return;
	}

	rc = k5_profile_get_realm_entry(profile, realm, "kdc", &kdcs1);
	if (rc != 0) {
		smb_krb5_log_errmsg(NULL, rc,
		    "smbns_krb: failed to obtain KDC entries from %s",
		    path);
		k5_profile_abandon(profile);
		free(realm);
		return;
	}

	/* No updates if KDCs are not defined. */
	if (kdcs1 == NULL) {
		k5_profile_abandon(profile);
		free(realm);
		return;
	}

	kdcs2 = smb_ads_get_kdcs(fqdomain);
	if (kdcs2 == NULL) {
		syslog(LOG_NOTICE, "smb_krb: failed to discover KDCs");
		k5_profile_abandon(profile);
		smb_krb5_cfg_free_list(kdcs1);
		free(realm);
		return;
	}

	if (smb_krb5_cfg_values_match(kdcs1, kdcs2) == B_TRUE) {
		k5_profile_abandon(profile);
		smb_krb5_cfg_free_list(kdcs1);
		smb_krb5_cfg_free_list(kdcs2);
		free(realm);
		return;
	}

	smb_krb5_cfg_free_list(kdcs1);

	rc = k5_profile_add_realm_entry(profile, realm, "kdc", kdcs2);
	smb_krb5_cfg_free_list(kdcs2);
	free(realm);

	if (rc != 0) {
		smb_krb5_log_errmsg(NULL, rc,
		    "smbns_krb: failed to update KDC entries in %s", path);
		k5_profile_abandon(profile);
		return;
	}

	if ((rc = k5_profile_release(profile)) != 0) {
		smb_krb5_log_errmsg(NULL, rc,
		    "smbns_krb: failed to commit KDC changes to %s", path);
		k5_profile_abandon(profile);
		return;
	}
}

/*
 * Configure default realm.
 */
errcode_t
smb_krb5_cfg_set_drealm(const char *drealm)
{
	profile_t	profile;
	char		*path;
	errcode_t 	rc;

	path = smb_krb5_cfg_getpath();

	if ((rc = k5_profile_init(path, &profile)) != 0)
		return (rc);

	if ((rc = k5_profile_set_libdefaults(profile, (char *)drealm)) != 0) {
		k5_profile_abandon(profile);
		return (rc);
	}

	if ((rc = k5_profile_release(profile)) != 0) {
		k5_profile_abandon(profile);
		return (rc);
	}

	return (0);
}

/*
 * Compares the elements in the 2 null-terminated string arrays.
 * If the elements in both arrays match, return B_TRUE. Otherwise, returns
 * B_FALSE.
 */
static boolean_t
smb_krb5_cfg_values_match(char *array1[], char *array2[])
{
	int i, j;
	int num1, num2;
	boolean_t found;

	if (array1 == NULL || array2 == NULL)
		return (B_FALSE);

	for (i = 0; array1[i] != NULL; i++) {}
	num1 = i;


	for (i = 0; array2[i] != NULL; i++) {}
	num2 = i;

	if (num1 != num2)
		return (B_FALSE);

	for (i = 0; array1[i] != NULL; i++) {
		for (found = B_FALSE, j = 0; array2[j] != NULL; j++) {
			if (strcasecmp(array1[i], array2[j]) == 0) {
				found = B_TRUE;
				break;
			}
		}

		if (!found)
			return (B_FALSE);
	}

	return (B_TRUE);
}

errcode_t
smb_krb5_cfg_add(smb_krb5_cfg_t *cfg, char *errmsg, size_t len)
{
	char		relation[MAXHOSTNAMELEN];
	char		*errval;
	int		verrcode;
	errcode_t	rc;

	if (cfg == NULL || errmsg == NULL)
		return (EINVAL);

	bzero(errmsg, len);

	if ((rc = k5_profile_add_realm(cfg->kc_profile,
	    cfg->kc_realm, cfg->kc_master_kdc,
	    cfg->kc_kdcs, B_TRUE, cfg->kc_default)) != 0) {
		(void) snprintf(errmsg, len, "failed to add %s realm to %s",
		    cfg->kc_realm, cfg->kc_path);
		return (rc);
	}

	/*
	 * Populates the following relation-value pair in the
	 * [domain_realm] section of krb5.conf.
	 *
	 * .<AD domain> = <REALM>
	 * .<local DNS suffix> = <REALM>
	 */
	(void) snprintf(relation, sizeof (relation), ".%s",
	    cfg->kc_fqdomain);

	if ((rc = k5_profile_add_domain_mapping(
	    cfg->kc_profile, relation, cfg->kc_realm)) != 0) {
		(void) snprintf(errmsg, len,
		    "failed to add domain-realm mappings for %s",
		    cfg->kc_realm);
			return (rc);
	}

	if ((rc = k5_profile_validate(cfg->kc_profile, cfg->kc_realm,
	    &verrcode, &errval)) != 0) {
		(void) snprintf(errmsg, len, "failed to validate %s",
		    cfg->kc_path);
		free(errval);
		return (rc);
	}

	if (verrcode != K5_PROFILE_VAL_SUCCESS)
		return (smb_krb5_cfg_update(cfg, errmsg, len));

	return (0);
}

/*
 * If the existing krb5.conf is valid then update the kpasswd/admin server and
 * the list of KDC entries.
 * If not then try to auto-recover from the misconfiguration. Retries for at
 * most SMB_KRB5_CFG_VALID_RETRIES number of times.
 */
errcode_t
smb_krb5_cfg_update(smb_krb5_cfg_t *cfg, char *errmsg, size_t len)
{
	int		i, verrcode;
	char		lval[MAXHOSTNAMELEN];
	char		*errval = NULL;
	char		*krbmsg = NULL;
	char		*drealm = NULL;
	boolean_t	update_kdcs = B_FALSE, update_master = B_FALSE;
	boolean_t	done = B_FALSE;
	errcode_t	rc;

	if (cfg == NULL || errmsg == NULL)
		return (EINVAL);

	bzero(errmsg, len);
	for (i = 0; i < SMB_KRB5_CFG_VALID_RETRIES; i++) {
		if ((rc = k5_profile_validate(cfg->kc_profile,
		    cfg->kc_realm, &verrcode, &errval)) != 0) {
			(void) snprintf(errmsg, len, "failed to validate %s",
			    cfg->kc_path);
			return (rc);
		}

		switch (verrcode) {
		case K5_PROFILE_VAL_SUCCESS:

			if (done) {
				free(errval);
				return (0);
			}

			if (!update_kdcs) {
				if ((rc = k5_profile_add_realm_entry(
				    cfg->kc_profile, cfg->kc_realm,
				    SMB_KRB5_CFG_KDC, cfg->kc_kdcs)) != 0) {
					(void) snprintf(errmsg, len,
					    "failed to add KDC entries for %s",
					    cfg->kc_realm);
					free(errval);
					return (rc);
				}
			}

			if (!update_master) {
				if ((rc = smb_krb5_cfg_update_master(cfg,
				    errmsg, len)) != 0) {
					free(errval);
					return (rc);
				}
			}

			done = B_TRUE;
			if (!cfg->kc_default)
				break;

			(void) k5_profile_get_default_realm(cfg->kc_profile,
			    &drealm);
			if (strcmp(drealm, cfg->kc_realm) == 0)
				break;

			/*FALLTHROUGH*/

		case K5_PROFILE_VAL_NO_DEF_IN_REALM:
		case K5_PROFILE_VAL_NO_DEF_REALM:
		case K5_PROFILE_VAL_DEF_REALM_CASE:
			if ((rc = k5_profile_set_libdefaults(
			    cfg->kc_profile, cfg->kc_realm)) != 0) {
				if (strcmp(errval, cfg->kc_realm)) {
					(void) snprintf(errmsg, len,
					    "failed to change default realm "
					    "from %s to %s",
					    errval, cfg->kc_realm);
				} else {
					(void) snprintf(errmsg, len,
					    "failed to set default realm to %s",
					    cfg->kc_realm);

				}
				free(errval);
				return (rc);
			}
			break;


		case K5_PROFILE_VAL_NO_REALM:
		case K5_PROFILE_VAL_REALM_CASE:
		case K5_PROFILE_VAL_NULL_REALM:
			rc = k5_profile_remove_realm(cfg->kc_profile, errval);

			if (rc != 0) {
				(void) snprintf(errmsg, len,
				    "failed to remove %s realm from %s",
				    errval, cfg->kc_path);
				free(errval);
				return (rc);
			}

			if ((rc = k5_profile_add_realm(cfg->kc_profile,
			    cfg->kc_realm, cfg->kc_master_kdc,
			    cfg->kc_kdcs, B_TRUE, cfg->kc_default)) != 0) {
				(void) snprintf(errmsg, len,
				    "failed to add %s realm to %s",
				    cfg->kc_realm, cfg->kc_path);
				free(errval);
				return (rc);
			}
			break;

		case K5_PROFILE_VAL_NO_DOM_REALM_MAP:
			/*
			 * Populates the following relation-value pair in the
			 * [domain_realm] section of krb5.conf.
			 *
			 * .<fqdn> = <REALM>
			 */
			(void) snprintf(lval, sizeof (lval), ".%s",
			    cfg->kc_fqdomain);

			if ((rc = k5_profile_add_domain_mapping(
			    cfg->kc_profile, lval, cfg->kc_realm)) != 0) {
				(void) snprintf(errmsg, len,
				    "failed to add domain_realm entries for %s",
				    cfg->kc_realm);
				free(errval);
				return (rc);
			}
			break;

		case K5_PROFILE_VAL_KDC_NO_REALM:
			if ((rc = k5_profile_add_realm_entry(
			    cfg->kc_profile, cfg->kc_realm, SMB_KRB5_CFG_KDC,
			    cfg->kc_kdcs)) != 0) {
				(void) snprintf(errmsg, len,
				    "failed to add KDC entries for %s",
				    cfg->kc_realm);
				free(errval);
				return (rc);
			}

			update_kdcs = B_TRUE;
			break;

		case K5_PROFILE_VAL_ADMIN_NO_REALM:
			if ((rc = smb_krb5_cfg_update_master(cfg, errmsg,
			    len)) != 0) {
				free(errval);
				return (rc);
			}

			update_master = B_TRUE;
			break;

		case K5_PROFILE_VAL_DOM_REALM_CASE:
			if ((rc = k5_profile_add_domain_mapping(
			    cfg->kc_profile, errval, cfg->kc_realm)) != 0) {
				(void) snprintf(errmsg, len,
				    "failed to add domain_realm entries for %s",
				    cfg->kc_realm);
				free(errval);
				return (rc);
			}
			break;
		}

		/*
		 * An error message will be generated outside the loop only if
		 * max retries is reached. Hence, the memory allocated
		 * for errval in the last iteration should not be released here.
		 */
		if (i + 1 != SMB_KRB5_CFG_VALID_RETRIES) {
			free(errval);
			errval = NULL;
		}
	}

	if (k5_profile_validate_get_error_msg(cfg->kc_profile, verrcode,
	    errval, &krbmsg) == 0) {
		(void) strlcpy(errmsg, krbmsg, len);
		free(krbmsg);
	} else {
		(void) snprintf(errmsg, len, "validation error code: %d",
		    verrcode);
	}

	/* deallocate the memory allocated for errval from the last iteration */
	free(errval);
	return (EINVAL);
}

/*
 * Overwrite the value of kpasswd server (only if configured) and
 * the value of admin server in krb5.conf.
 */
static errcode_t
smb_krb5_cfg_update_master(smb_krb5_cfg_t *cfg, char *errmsg, size_t len)
{
	char		**list = NULL;
	char		*masters[] = {cfg->kc_master_kdc, NULL};
	errcode_t	rc;

	if (cfg == NULL || errmsg == NULL)
		return (EINVAL);

	bzero(errmsg, len);
	if (k5_profile_get_realm_entry(cfg->kc_profile, cfg->kc_realm,
	    SMB_KRB5_CFG_KPASSWD_SRV, &list) == 0 && list != NULL)
		(void) k5_profile_add_realm_entry(cfg->kc_profile,
		    cfg->kc_realm, SMB_KRB5_CFG_KPASSWD_SRV, masters);

	if ((rc = k5_profile_add_realm_entry(cfg->kc_profile,
	    cfg->kc_realm, SMB_KRB5_CFG_ADMIN_SRV, masters)) != 0) {
		(void) snprintf(errmsg, len,
		    "failed to add admin_server for %s", cfg->kc_realm);
		return (rc);
	}

	return (0);
}

char *
smb_krb5_cfg_getpath(void)
{
	char *path;

	path = getenv("KRB5_CONFIG");
	if (path == NULL || *path == '\0')
		path = SMB_KRB5_CFG_FILE;

	return (path);
}

static void
smb_krb5_cfg_free_list(char **list)
{
	int i;

	if (list == NULL)
		return;

	for (i = 0; list[i] != NULL; i++)
		free(list[i]);

	free(list);
}

/*
 * Obtain and cache an initial TGT ticket for the specified principal.
 * TGT ticket can be obtained given the username and password or service
 * principal name with a NULL password. In the latter case, keys for the
 * specified service principal will be looked up from the local keytab
 * prior to performing the KRB-AS exchange.
 *
 * For kinit to work in multiple realms environment, the principal name must be
 * in this format: <username>@<REALM>
 *
 * Returns 0 on success. Otherwise, returns -1.
 */
int
smb_kinit(char *principal_name, char *principal_passwd)
{
	krb5_context ctx = NULL;
	krb5_ccache cc = NULL;
	krb5_principal me = NULL;
	krb5_creds my_creds;
	krb5_error_code rc;
	const char *errmsg = NULL;
	const char *action = "smbns_krb";

	assert(principal_name != NULL);
	(void) memset(&my_creds, 0, sizeof (my_creds));

	/*
	 * From this point on, we can goto cleanup because the key variables
	 * are initialized.
	 */

	rc = krb5_init_context(&ctx);
	if (rc) {
		action = "smbns_krb: initializing context";
		goto cleanup;
	}

	rc = krb5_cc_default(ctx, &cc);
	if (rc != 0) {
		action = "smbns_krb: resolve default credentials cache";
		goto cleanup;
	}

	/* Use specified name */
	rc = krb5_parse_name(ctx, principal_name, &me);
	if (rc != 0) {
		action = "smbns_krb: parsing principal name";
		goto cleanup;
	}

	if (principal_passwd != NULL) {
		action = "smbns_krb: getting initial credentials";
		rc = krb5_get_init_creds_password(ctx, &my_creds, me,
		    principal_passwd, NULL, 0, (krb5_deltat)0,
		    NULL, NULL);
	} else {
		krb5_keytab keytab;

		action = "smbns_krb: getting initial credentials via keytab";
		if ((rc = krb5_kt_default(ctx, &keytab)) != 0)
			goto cleanup;

		rc = krb5_get_init_creds_keytab(ctx, &my_creds, me,
		    keytab, (krb5_deltat)0, NULL, NULL);

		(void) krb5_kt_close(ctx, keytab);
	}

	if (rc != 0) {
		if (rc == KRB5KRB_AP_ERR_BAD_INTEGRITY)
			errmsg = "Password incorrect or key out of sync";

		goto cleanup;
	}

	rc = krb5_cc_initialize(ctx, cc, me);
	if (rc != 0) {
		action = "smbns_krb: initializing cache";
		goto cleanup;
	}

	rc = krb5_cc_store_cred(ctx, cc, &my_creds);
	if (rc != 0) {
		action = "smbns_krb: storing credentials";
		goto cleanup;
	}

	/* SUCCESS! */

cleanup:
	if (rc != 0) {
		if (errmsg == NULL)
			smb_krb5_log_errmsg(ctx, rc, action);
		else
			syslog(LOG_ERR, "%s (%s)", action, errmsg);
	}

	if (my_creds.client == me) {
		my_creds.client = NULL;
	}
	krb5_free_cred_contents(ctx, &my_creds);

	if (me)
		krb5_free_principal(ctx, me);
	if (cc)
		(void) krb5_cc_close(ctx, cc);
	if (ctx)
		krb5_free_context(ctx);

	return ((rc == 0) ? 0 : -1);
}

/*
 * Invoke krb5_get_error_message() to generate a richer error message if
 * a Kerberos context is established. Otherwise, a generic error message is
 * logged.
 */
void
smb_krb5_log_errmsg(krb5_context ctx, krb5_error_code code,
    const char *fmt, ...)
{
	va_list		ap;
	const char	*krbmsg;
	char		buf[SMB_LOG_LINE_SZ];

	assert(fmt != NULL);

	if (loghd == 0)
		loghd = smb_log_create(SMB_KRB5_LOGSIZE, SMB_KRB5_LOGNAME);

	va_start(ap, fmt);
	(void) vsnprintf(buf, SMB_LOG_LINE_SZ, fmt, ap);
	va_end(ap);

	if (ctx != NULL)
		krbmsg = krb5_get_error_message(ctx, code);
	else
		krbmsg = error_message(code);

	smb_log(loghd, LOG_ERR, "%s (%s)", buf, krbmsg);

	if (ctx != NULL)
		krb5_free_error_message(ctx, krbmsg);
}

char *
smb_krb5_domain2realm(const char *fqdn)
{
	char *realm;

	if ((realm = strdup(fqdn)) == NULL)
		return (NULL);

	(void) smb_strupr(realm);
	return (realm);
}
