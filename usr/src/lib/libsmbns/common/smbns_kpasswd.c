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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <syslog.h>
#include <netdb.h>
#include <sys/param.h>
#include <kerberosv5/krb5.h>
#include <kerberosv5/com_err.h>

#include <smbsrv/libsmbns.h>
#include <smbsrv/libsmb.h>
#include <smbns_krb.h>

/*
 * Kerberized services available on the system.
 */
static smb_krb5_pn_t smb_krb5_pn_tab[] = {
	/*
	 * Service keys are salted with the SMB_KRB_PN_ID_ID_SALT prinipal
	 * name.
	 */
	{SMB_KRB5_PN_ID_SALT,		SMB_PN_SVC_HOST,	SMB_PN_SALT},

	/*
	 * Clients use one of the following host/cifs/NetBIOS SPNs
	 * to request the CIFS service.
	 */
	{SMB_KRB5_PN_ID_HOST_FQHN,	SMB_PN_SVC_HOST,
	    SMB_PN_KEYTAB_ENTRY | SMB_PN_SPN_ATTR | SMB_PN_UPN_ATTR |
	    SMB_PN_KERBERIZED_CIFS | SMB_PN_NONKERBERIZED_CIFS},
	{SMB_KRB5_PN_ID_HOST_NETBIOS,	SMB_PN_SVC_HOST,
	    SMB_PN_KEYTAB_ENTRY | SMB_PN_SPN_ATTR | SMB_PN_KERBERIZED_CIFS},
	{SMB_KRB5_PN_ID_CIFS_FQHN,	SMB_PN_SVC_CIFS,
	    SMB_PN_KEYTAB_ENTRY | SMB_PN_SPN_ATTR | SMB_PN_KERBERIZED_CIFS},
	{SMB_KRB5_PN_ID_CIFS_NETBIOS,	SMB_PN_SVC_CIFS,
	    SMB_PN_KEYTAB_ENTRY | SMB_PN_KERBERIZED_CIFS},
	{SMB_KRB5_PN_ID_SAM_ACCT,	NULL,
	    SMB_PN_KEYTAB_ENTRY | SMB_PN_KERBERIZED_CIFS},

	/* NFS */
	{SMB_KRB5_PN_ID_NFS_FQHN,	SMB_PN_SVC_NFS,
	    SMB_PN_KEYTAB_ENTRY | SMB_PN_SPN_ATTR | SMB_PN_NONKERBERIZED_CIFS},

	/* HTTP */
	{SMB_KRB5_PN_ID_HTTP_FQHN,	SMB_PN_SVC_HTTP,
	    SMB_PN_KEYTAB_ENTRY | SMB_PN_SPN_ATTR | SMB_PN_NONKERBERIZED_CIFS},

	/* ROOT */
	{SMB_KRB5_PN_ID_ROOT_FQHN,	SMB_PN_SVC_ROOT,
	    SMB_PN_KEYTAB_ENTRY | SMB_PN_SPN_ATTR | SMB_PN_NONKERBERIZED_CIFS},
};

#define	SMB_KRB5_SPN_TAB_SZ \
	(sizeof (smb_krb5_pn_tab) / sizeof (smb_krb5_pn_tab[0]))

#define	SMB_KRB5_MAX_BUFLEN	1024

static int smb_krb5_spn_count(uint32_t);
static smb_krb5_pn_t *smb_krb5_lookup_pn(smb_krb5_pn_id_t);
static int smb_krb5_get_kprinc(krb5_context, smb_krb5_pn_id_t, uint32_t,
    const char *, krb5_principal *);
static int smb_krb5_kt_spn_validate(krb5_context, const char *, uint32_t,
    smb_krb5_pn_set_t *);
static int smb_krb5_kt_remove_by_fqdn(krb5_context, char *);

/*
 * Generates a null-terminated array of principal names that
 * represents the list of the available Kerberized services
 * of the specified type (SPN attribute, UPN attribute, or
 * keytab entry).
 *
 * Returns the number of principal names returned via the 1st
 * output parameter (i.e. vals).
 *
 * Caller must invoke smb_krb5_free_spns to free the allocated
 * memory when finished.
 */
uint32_t
smb_krb5_get_pn_set(smb_krb5_pn_set_t *set, uint32_t type, char *fqdn)
{
	int cnt, i;
	smb_krb5_pn_t *tabent;

	if (!set || !fqdn)
		return (0);

	(void) memset(set, 0, sizeof (smb_krb5_pn_set_t));
	cnt = smb_krb5_spn_count(type);
	set->s_pns = (char **)calloc(cnt + 1, sizeof (char *));

	if (set->s_pns == NULL)
		return (0);

	for (i = 0, set->s_cnt = 0; i < SMB_KRB5_SPN_TAB_SZ; i++) {
		tabent = &smb_krb5_pn_tab[i];

		if (set->s_cnt == cnt)
			break;

		if ((tabent->p_flags & type) != type)
			continue;

		set->s_pns[set->s_cnt] = smb_krb5_get_pn_by_id(tabent->p_id,
		    type, fqdn);

		if (set->s_pns[set->s_cnt] == NULL) {
			syslog(LOG_ERR,
			    "smbns_kpasswd: failed to obtain principal names:"
			    " possible transient memory shortage");
			smb_krb5_free_pn_set(set);
			return (0);
		}

		set->s_cnt++;
	}

	if (set->s_cnt == 0)
		smb_krb5_free_pn_set(set);

	return (set->s_cnt);
}

void
smb_krb5_free_pn_set(smb_krb5_pn_set_t *set)
{
	int i;

	if (set == NULL || set->s_pns == NULL)
		return;

	for (i = 0; i < set->s_cnt; i++)
		free(set->s_pns[i]);

	free(set->s_pns);
	set->s_pns = NULL;
}

/*
 * The format of SMBNS_KRB5_KEYTAB_ENV is
 * [[<kt type>:]<absolute path of the keytab file>] where <kt type> can be
 * FILE or WRFILE. This function gets the absolute path of the keytab file by
 * stripping off the <kt type>.
 */
char *
smb_krb5_kt_getpath(void)
{
	char	*p, *path;

	path = getenv(SMBNS_KRB5_KEYTAB_ENV);
	if (path == NULL || *path == '\0')
		return (SMBNS_KRB5_KEYTAB);

	if ((p = strchr(path, ':')) != NULL)
		path = ++p;

	return (path);
}

/*
 * Update keytab file during startup if keys that SMB service relies on
 * are missing from the keytab. If keytab file doesn't exist, create one
 * and add keys for all SPNs of the specified domain to the keytab file.
 */
int
smb_krb5_kt_update_startup(char *fqdn, krb5_kvno kvno, uint32_t kt_flags)
{
	int i;
	int rc;
	char pwd[SMB_PASSWD_MAXLEN + 1];
	krb5_error_code kerr;
	krb5_context ctx = NULL;
	smb_krb5_pn_set_t spns;
	char *path;

	rc = smb_config_getstr(SMB_CI_MACHINE_PASSWD, pwd, sizeof (pwd));
	if (rc != SMBD_SMF_OK || *pwd == '\0') {
		syslog(LOG_NOTICE,
		    "smbns_kpasswd: unable to obtain machine password");
		return (-1);
	}

	if ((kerr = krb5_init_context(&ctx)) != 0) {
		smb_krb5_log_errmsg(ctx, kerr,
		    "Kerberos context initialization failed");
		return (-1);
	}

	(void) memset(&spns, 0, sizeof (smb_krb5_pn_set_t));
	path = smb_krb5_kt_getpath();
	if (access(path, F_OK) != 0) {
		if (smb_krb5_get_pn_set(&spns, SMB_PN_KEYTAB_ENTRY, fqdn)
		    == 0) {
			rc = -1;
			goto cleanup;
		}

	} else {
		if ((kerr = smb_krb5_kt_spn_validate(ctx, fqdn, kt_flags,
		    &spns)) != 0) {
			rc = -1;
			goto cleanup;
		}

		if (spns.s_cnt == 0) {
			rc = 0;
			goto cleanup;
		}
	}

	for (i = 0; spns.s_pns[i] != NULL; i++) {
		kerr = k5_kt_remove_by_svcprinc(ctx, spns.s_pns[i]);
		if (kerr != 0 && kerr != ENOENT) {
			smb_krb5_log_errmsg(ctx, kerr,
			    "smbns_kpasswd(%s: %s): key removal failed",
			    smb_krb5_kt_getpath(), spns.s_pns[i]);
			rc = -1;
			goto cleanup;
		}
	}

	if ((kerr = k5_kt_add_ad_entries(ctx, spns.s_pns,
	    fqdn, kvno, kt_flags, pwd)) != 0) {
		smb_krb5_log_errmsg(ctx, kerr,
		    "smbns_kpasswd(%s): key addition failed",
		    smb_krb5_kt_getpath());
		rc = -1;
		goto cleanup;
	}

	rc = 0;

cleanup:
	if (ctx != NULL)
		krb5_free_context(ctx);

	smb_krb5_free_pn_set(&spns);
	(void) memset(pwd, 0, sizeof (pwd));

	return (rc);
}

/*
 * Update keytab file during domain join
 * Prior to adding new key set for the specified AD domain, any old key sets
 * for the specified domain will be removed.
 */
int
smb_krb5_kt_update_adjoin(krb5_context ctx,
    char *fqdn, krb5_kvno kvno, char *passwd, uint32_t kt_flags)

{
	krb5_error_code kerr;
	smb_krb5_pn_set_t spns;
	char *path;
	int rc = 0;
	uint32_t keytype = SMB_PN_KEYTAB_ENTRY;

	if (smb_krb5_kt_remove_by_fqdn(ctx, fqdn) != 0)
		return (-1);

	if (!smb_config_getbool(SMB_CI_SVR_EXTSEC))
		keytype |= SMB_PN_NONKERBERIZED_CIFS;

	if (smb_krb5_get_pn_set(&spns, keytype, fqdn) == 0)
		return (-1);

	if ((kerr = k5_kt_add_ad_entries(ctx, spns.s_pns,
	    fqdn, kvno, kt_flags, passwd)) != 0) {
		path = smb_krb5_kt_getpath();
		smb_krb5_log_errmsg(ctx, kerr,
		    "smbns_kpasswd(%s: %s): key addition failed",
		    path, fqdn);
		rc = -1;
	}

	smb_krb5_free_pn_set(&spns);
	return (rc);
}

/*
 * Wrapper function to call smb_krb5_kt_remove_by_fqdn.
 * The intent is to hide the Kerberos context from the API consumer.
 */
int
smb_krb5_kt_remove(char *fqdn)
{
	krb5_error_code kerr;
	krb5_context ctx = NULL;
	int rc = 0;

	if ((kerr = krb5_init_context(&ctx)) != 0) {
		smb_krb5_log_errmsg(ctx, kerr,
		    "Failed to initiate Kerberos context");
		return (-1);
	}

	if (smb_krb5_kt_remove_by_fqdn(ctx, fqdn) != 0)
		rc = -1;

	krb5_free_context(ctx);
	return (rc);
}

/*
 * Set the workstation trust account password.
 * Returns 0 on success.  Otherwise, returns non-zero value.
 */
int
smb_krb5_setpwd(krb5_context ctx, const char *fqdn, char *passwd)
{
	krb5_error_code code;
	krb5_ccache cc = NULL;
	int result_code = 0;
	krb5_data result_code_string, result_string;
	krb5_principal princ;

	if (smb_krb5_get_kprinc(ctx, SMB_KRB5_PN_ID_HOST_FQHN,
	    SMB_PN_UPN_ATTR, fqdn, &princ) != 0)
		return (-1);

	(void) memset(&result_code_string, 0, sizeof (result_code_string));
	(void) memset(&result_string, 0, sizeof (result_string));

	if ((code = krb5_cc_default(ctx, &cc)) != 0) {
		smb_krb5_log_errmsg(ctx, code,
		    "smbns_kpasswd: failed to find %s", SMB_CCACHE_PATH);
		krb5_free_principal(ctx, princ);
		return (-1);
	}

	code = krb5_set_password_using_ccache(ctx, cc, passwd, princ,
	    &result_code, &result_code_string, &result_string);

	if (code != 0)
		smb_krb5_log_errmsg(ctx, code,
		    "smbns_kpasswd: KPASSWD protocol exchange failed");

	(void) krb5_cc_close(ctx, cc);

	if (result_code != 0)
		syslog(LOG_ERR, "smbns_kpasswd: KPASSWD failed: %.*s (%.*s)",
		    result_code_string.length, result_code_string.data,
		    result_string.length, result_string.data);

	krb5_free_principal(ctx, princ);
	free(result_code_string.data);
	free(result_string.data);
	return (code);
}

/*
 * Derives the Kerberos realm from the specified fqdn, and removes
 * all keytab entries associated with that realm from the keytab file.
 */
static int
smb_krb5_kt_remove_by_fqdn(krb5_context ctx, char *fqdn)
{
	char *realm, *path;
	krb5_error_code kerr;

	if ((fqdn == NULL) || (*fqdn == '\0'))
		return (-1);

	if ((realm = smb_krb5_domain2realm(fqdn)) == NULL)
		return (-1);

	kerr = k5_kt_remove_by_realm(ctx, realm);
	free(realm);

	if (kerr == ENOENT)
		return (0);

	if (kerr != 0) {
		path = smb_krb5_kt_getpath();
		smb_krb5_log_errmsg(ctx, kerr,
		    "smbns_kpasswd(%s: %s): key removal failed",
		    path, fqdn);
		return (-1);
	}

	return (0);
}

/*
 * Returns the set of missing service principals that are required for CIFS
 * Service Kerberos authentication via the output parameter 'set'.
 * Caller must invoke smb_krb5_free_spns to free the allocated memory when
 * finished.
 */
static krb5_error_code
smb_krb5_kt_spn_validate(krb5_context ctx, const char *fqdn, uint32_t flags,
    smb_krb5_pn_set_t *set)
{
	int i;
	smb_krb5_pn_t *tabent;
	krb5_error_code kerr, rc = 0;
	boolean_t valid;
	char *svcprinc;

	if (set == NULL)
		return (EINVAL);

	(void) memset(set, 0, sizeof (smb_krb5_pn_set_t));
	set->s_pns = (char **)calloc(SMB_KRB5_SPN_TAB_SZ + 1, sizeof (char *));
	if (set->s_pns == NULL)
		return (ENOMEM);

	for (i = 0, set->s_cnt = 0; i < SMB_KRB5_SPN_TAB_SZ; i++) {
		tabent = &smb_krb5_pn_tab[i];
		if (!(tabent->p_flags & SMB_PN_KERBERIZED_CIFS))
			continue;

		svcprinc = smb_krb5_get_pn_by_id(tabent->p_id,
		    SMB_PN_KEYTAB_ENTRY, fqdn);

		if (svcprinc == NULL) {
			smb_krb5_log_errmsg(ctx, ENOMEM,
			    "smbns_kpasswd(%d): key verification skipped",
			    tabent->p_id);
			rc = ENOMEM;
			continue;
		}

		if ((kerr = k5_kt_ad_validate(ctx, svcprinc, flags,
		    &valid)) != 0) {
			smb_krb5_log_errmsg(ctx, kerr,
			    "smbns_kpasswd(%s): key verification failed",
			    svcprinc);

			free(svcprinc);
			rc = kerr;
			continue;
		}

		if (valid)
			free(svcprinc);
		else
			set->s_pns[set->s_cnt++] = svcprinc;
	}

	return (rc);
}

boolean_t
smb_krb5_kt_find(smb_krb5_pn_id_t id, const char *fqdn, char *fname)
{
	krb5_context ctx;
	krb5_error_code kerr;
	krb5_keytab kt;
	krb5_keytab_entry entry;
	krb5_principal princ;
	char ktname[MAXPATHLEN];
	boolean_t found = B_FALSE;

	if (!fqdn || !fname)
		return (found);

	if ((kerr = krb5_init_context(&ctx)) != 0) {
		smb_krb5_log_errmsg(ctx, kerr,
		    "smbns_kpasswd: Kerberos context initialization failed");
		return (found);
	}

	if (smb_krb5_get_kprinc(ctx, id, SMB_PN_KEYTAB_ENTRY, fqdn,
	    &princ) != 0) {
		krb5_free_context(ctx);
		return (found);
	}

	(void) snprintf(ktname, MAXPATHLEN, "FILE:%s", fname);
	if (krb5_kt_resolve(ctx, ktname, &kt) == 0) {
		if (krb5_kt_get_entry(ctx, kt, princ, 0, 0, &entry) == 0) {
			found = B_TRUE;
			(void) krb5_kt_free_entry(ctx, &entry);
		}

		(void) krb5_kt_close(ctx, kt);
	}

	krb5_free_principal(ctx, princ);
	krb5_free_context(ctx);
	return (found);
}

static int
smb_krb5_spn_count(uint32_t type)
{
	int i, cnt;

	for (i = 0, cnt = 0; i < SMB_KRB5_SPN_TAB_SZ; i++) {
		if (smb_krb5_pn_tab[i].p_flags & type)
			cnt++;
	}

	return (cnt);
}

/*
 * Generate the Kerberos Principal given a principal name format and the
 * fully qualified domain name. On success, caller must free the allocated
 * memory by calling krb5_free_principal().
 */
static int
smb_krb5_get_kprinc(krb5_context ctx, smb_krb5_pn_id_t id, uint32_t type,
    const char *fqdn, krb5_principal *princ)
{
	char *buf;

	if ((buf = smb_krb5_get_pn_by_id(id, type, fqdn)) == NULL)
		return (-1);

	if (krb5_parse_name(ctx, buf, princ) != 0) {
		free(buf);
		return (-1);
	}

	free(buf);
	return (0);
}

/*
 * Looks up an entry in the principal name table given the ID.
 */
static smb_krb5_pn_t *
smb_krb5_lookup_pn(smb_krb5_pn_id_t id)
{
	int i;
	smb_krb5_pn_t *tabent;

	for (i = 0; i < SMB_KRB5_SPN_TAB_SZ; i++) {
		tabent = &smb_krb5_pn_tab[i];
		if (id == tabent->p_id)
			return (tabent);
	}

	return (NULL);
}

/*
 * Construct the principal name given an ID, the requested type, and the
 * fully-qualified name of the domain of which the principal is a member.
 */
char *
smb_krb5_get_pn_by_id(smb_krb5_pn_id_t id, uint32_t type,
    const char *ad_domain)
{
	char dns_domain[MAXHOSTNAMELEN];
	char nbname[NETBIOS_NAME_SZ];
	char hostname[MAXHOSTNAMELEN];
	char *realm = NULL;
	smb_krb5_pn_t *pn;
	char *buf;

	if (smb_get_dns_suffix(dns_domain, sizeof (dns_domain)) == -1)
		(void) strlcpy(dns_domain, ad_domain, sizeof (dns_domain));

	(void) smb_getnetbiosname(nbname, NETBIOS_NAME_SZ);
	(void) smb_gethostname(hostname, MAXHOSTNAMELEN, SMB_CASE_LOWER);

	pn = smb_krb5_lookup_pn(id);
	if (pn == NULL)
		return (NULL);

	/* detect inconsistent requested format and type */
	if ((type & pn->p_flags) != type)
		return (NULL);

	switch (id) {
	case SMB_KRB5_PN_ID_SALT:
		(void) asprintf(&buf, "%s/%s.%s",
		    pn->p_svc, smb_strlwr(nbname), ad_domain);
		break;

	case SMB_KRB5_PN_ID_HOST_FQHN:
	case SMB_KRB5_PN_ID_CIFS_FQHN:
	case SMB_KRB5_PN_ID_NFS_FQHN:
	case SMB_KRB5_PN_ID_HTTP_FQHN:
	case SMB_KRB5_PN_ID_ROOT_FQHN:
		(void) asprintf(&buf, "%s/%s.%s",
		    pn->p_svc, hostname, dns_domain);
		break;

	case SMB_KRB5_PN_ID_HOST_NETBIOS:
	case SMB_KRB5_PN_ID_CIFS_NETBIOS:
		(void) asprintf(&buf, "%s/%s",
		    pn->p_svc, nbname);
		break;

	case SMB_KRB5_PN_ID_SAM_ACCT:
		(void) asprintf(&buf, "%s$", nbname);
		break;
	}

	/*
	 * If the requested principal is either added to keytab / the machine
	 * account as the UPN attribute or used for key salt generation,
	 * the principal name must have the @<REALM> portion.
	 */
	if (type & (SMB_PN_KEYTAB_ENTRY | SMB_PN_UPN_ATTR | SMB_PN_SALT)) {
		if ((realm = strdup(ad_domain)) == NULL) {
			free(buf);
			return (NULL);
		}

		(void) smb_strupr(realm);
		if (buf != NULL) {
			char *tmp;

			(void) asprintf(&tmp, "%s@%s", buf,
			    realm);
			free(buf);
			buf = tmp;
		}

		free(realm);
	}

	return (buf);
}
