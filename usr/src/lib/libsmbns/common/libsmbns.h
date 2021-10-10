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

#ifndef	_LIBSMBNS_H
#define	_LIBSMBNS_H

#include <kerberosv5/krb5.h>
#include <kerberosv5/com_err.h>
#include <ldap.h>
#include <smbsrv/libsmb.h>
#include <profile.h>

#ifdef	__cplusplus
extern "C" {
#endif

/* ADS typedef/data structures and functions */


typedef struct smb_ads_handle {
	char *domain;		/* ADS domain (in lower case) */
	char *domain_dn;	/* domain in Distinquish Name format */
	char *ip_addr;		/* ip addr in string format */
	char *hostname;		/* fully qualified hostname */
	char *site;		/* local ADS site */
	LDAP *ld;		/* LDAP handle */
} smb_ads_handle_t;

typedef struct smb_ads_host_info {
	char domain[MAXHOSTNAMELEN];	/* FQDN of the AD domain */
	char name[MAXHOSTNAMELEN];  /* fully qualified hostname */
	int port;		/* ldap port */
	int priority;		/* DNS SRV record priority */
	int weight;		/* DNS SRV record weight */
	smb_inaddr_t ipaddr;	/* network byte order */
} smb_ads_host_info_t;

/*
 * The possible return status of the adjoin routine.
 */
typedef enum smb_adjoin_status {
	SMB_ADJOIN_SUCCESS = 0,
	SMB_ADJOIN_ERR_GET_HANDLE,
	SMB_ADJOIN_ERR_GEN_PWD,
	SMB_ADJOIN_ERR_GET_DCLEVEL,
	SMB_ADJOIN_ERR_ADD_TRUST_ACCT,
	SMB_ADJOIN_ERR_MOD_TRUST_ACCT,
	SMB_ADJOIN_ERR_DUP_TRUST_ACCT,
	SMB_ADJOIN_ERR_TRUST_ACCT,
	SMB_ADJOIN_ERR_INIT_KRB_CTX,
	SMB_ADJOIN_ERR_GET_SPNS,
	SMB_ADJOIN_ERR_KSETPWD,
	SMB_ADJOIN_ERR_UPDATE_CNTRL_ATTR,
	SMB_ADJOIN_ERR_WRITE_KEYTAB
} smb_adjoin_status_t;

/* ADS functions */
extern void smb_ads_refresh(void);
extern smb_ads_handle_t *smb_ads_open(void);
extern void smb_ads_close(smb_ads_handle_t *);
extern int smb_ads_publish_share(smb_ads_handle_t *, const char *, const char *,
    const char *, const char *);
extern int smb_ads_remove_share(smb_ads_handle_t *, const char *, const char *,
    const char *, const char *);
extern int smb_ads_build_unc_name(char *, int, const char *, const char *);
extern int smb_ads_lookup_share(smb_ads_handle_t *, const char *, const char *,
    char *);
extern int smb_ads_add_share(smb_ads_handle_t *, const char *, const char *,
    const char *);
extern smb_adjoin_status_t smb_ads_join(char *, char *, char *, char *, size_t);
extern void smb_ads_join_errmsg(smb_adjoin_status_t);
extern boolean_t smb_ads_lookup_msdcs(char *, char *, char *, uint32_t);
extern smb_ads_host_info_t *smb_ads_find_host(char *, char *);
extern void *smb_ads_upgrade(void *);
extern char **smb_ads_get_kdcs(char *);

/* DYNDNS functions */
extern int dyndns_update(char *);
extern int dyndns_zone_clear(const char *);
extern int dyndns_zone_update(const char *);

/* NETBIOS Functions */
extern int smb_netbios_start(void);
extern void smb_netbios_stop(void);
extern void smb_netbios_name_reconfig(void);

/* Browser Functions */
extern void smb_browser_reconfig(void);
extern boolean_t smb_browser_netlogon(char *, char *, uint32_t, uint32_t *);

/*
 * Kerberos configuration
 */
#define	SMB_KRB5_CFG_FILE			"/etc/krb5/krb5.conf"
#define	SMB_KRB5_CFG_SUFFIX_BACKUP		".bak"
#define	SMB_KRB5_CFG_SUFFIX_SMB			".smb.fail"

#define	SMB_KRB5_CFG_DEFAULT_REALM_EXIST(cfg)	\
	((cfg)->kc_orig_drealm != NULL)

#define	SMB_KRB5_CFG_IS_DEFAULT_REALM(cfg)	\
	(SMB_KRB5_CFG_DEFAULT_REALM_EXIST(cfg) && \
	strncasecmp((cfg)->kc_orig_drealm, (cfg)->kc_realm, \
	    strlen((cfg)->kc_orig_drealm)) == 0)


/*
 * smb_krb5_cfg_t: used for retrieving and writing Kerberos configuration
 *
 *   kc_path		- the path of Kerberos configuration file
 *   kc_profile		- profile handle
 *   kc_ctx		- a Kerberos context which is needed for
 *			  retrieving/writing profile data and obtaining
 *                        Kerberos error messages
 *   kc_exist		- a boolean that is set when krb5.conf exists
 *   kc_orig_drealm	- original default realm
 *   kc_default		- a boolean that is set when the specified realm will
 *			  be set as default realm
 *   kc_realm		- realm to be configured
 *   kc_fqdomain	- the fully-qualified domain name associated with the
 *			  specified realm.
 *   kc_kdcs		- used for setting the KDC entries in krb5.conf
 *   kc_master_kdc	- used for setting kpasswd_server and/or admin_server
 *			  field in krb5.conf
 */
typedef struct smb_krb5_cfg {
	char			*kc_path;
	profile_t		kc_profile;
	krb5_context		kc_ctx;
	boolean_t		kc_exist;
	char			*kc_orig_drealm;
	boolean_t		kc_default;
	char			*kc_realm;
	char			*kc_fqdomain;
	char			**kc_kdcs;
	char			*kc_master_kdc;
} smb_krb5_cfg_t;

extern char *smb_krb5_cfg_getpath(void);
extern errcode_t smb_krb5_cfg_init(smb_krb5_cfg_t *, const char *, const char *,
    char **);
extern errcode_t smb_krb5_cfg_fini(smb_krb5_cfg_t *, boolean_t);
extern errcode_t smb_krb5_cfg_add(smb_krb5_cfg_t *, char *, size_t);
extern errcode_t smb_krb5_cfg_set_drealm(const char *);
extern errcode_t smb_krb5_cfg_update(smb_krb5_cfg_t *, char *, size_t);
extern void smb_krb5_cfg_update_kdcs(void);

extern int smb_krb5_kt_remove(char *);

extern void smb_krb5_log_errmsg(krb5_context, krb5_error_code,
    const char *, ...);

#ifdef	__cplusplus
}
#endif

#endif	/* _LIBSMBNS_H */
