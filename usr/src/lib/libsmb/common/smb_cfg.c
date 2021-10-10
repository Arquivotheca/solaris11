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
 * Configuration management library
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <synch.h>
#include <string.h>
#include <strings.h>
#include <syslog.h>
#include <netdb.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/idmap.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <libscf.h>
#include <assert.h>
#include <libdlpi.h>
#include <uuid/uuid.h>
#include <smbsrv/libsmb.h>
#include <smb/ntdsutil.h>

typedef struct smb_cfg_param {
	smb_cfg_id_t	sc_id;
	char		*sc_name;
	int		sc_type;
	int32_t		sc_minval;
	int32_t		sc_maxval;
	uint32_t	sc_flags;
	boolean_t	sc_refresh;
	boolean_t	(*sc_chk)(struct smb_cfg_param *, const char *);
} smb_cfg_param_t;

typedef struct smb_hostifs_walker {
	const char	*hiw_ifname;
	boolean_t	hiw_matchfound;
} smb_hostifs_walker_t;

/*
 * config parameter flags
 */
#define	SMB_CF_PROTECTED	0x01
#define	SMB_CF_EXEC		0x02

/* idmap SMF fmri and Property Group */
#define	MACHINE_SID			"machine_sid"
#define	IDMAP_DOMAIN			"domain_name"

#define	SMB_SECMODE_WORKGRP_STR 	"workgroup"
#define	SMB_SECMODE_DOMAIN_STR  	"domain"

#define	SMB_ENC_LEN	1024
#define	SMB_DEC_LEN	256

#define	SMB_VALID_SUB_CHRS	"UDhMLmIiSPu"	/* substitution characters */

static char *b64_data =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static boolean_t smb_config_chk_string(smb_cfg_param_t *, const char *);
static boolean_t smb_config_chk_range(smb_cfg_param_t *, const char *);
static boolean_t smb_config_chk_range_zero(smb_cfg_param_t *, const char *);
static boolean_t smb_config_chk_hostname(smb_cfg_param_t *, const char *);
static boolean_t smb_config_chk_interface(smb_cfg_param_t *, const char *);
static boolean_t smb_config_chk_boolean(smb_cfg_param_t *, const char *);
static boolean_t smb_config_chk_path(smb_cfg_param_t *, const char *);
static boolean_t smb_config_chk_cmd(smb_cfg_param_t *, const char *);
static boolean_t smb_config_chk_disposition(smb_cfg_param_t *, const char *);

static smb_cfg_param_t smb_cfg_table[] =
{
	{ SMB_CI_VERSION, "sv_version", SCF_TYPE_ASTRING,
		0, 0, 0, B_FALSE, NULL },

	/* Oplock configuration, Kernel Only */
	{ SMB_CI_OPLOCK_ENABLE, "oplock_enable", SCF_TYPE_BOOLEAN,
		0, 0, 0, B_FALSE, smb_config_chk_boolean },

	/* Autohome configuration */
	{ SMB_CI_AUTOHOME_MAP, "autohome_map", SCF_TYPE_ASTRING,
		0, 0, 0, B_FALSE, smb_config_chk_path },

	/* Domain/PDC configuration */
	{ SMB_CI_DOMAIN_SID, "domain_sid", SCF_TYPE_ASTRING,
		0, 0, 0, B_FALSE, NULL },

	{ SMB_CI_DOMAIN_NB, "nb_domain", SCF_TYPE_ASTRING,
		0, NETBIOS_NAME_SZ, 0, B_FALSE, smb_config_chk_string },
	{ SMB_CI_DOMAIN_AD, "ad_domain", SCF_TYPE_ASTRING,
		0, MAXHOSTNAMELEN, 0, B_FALSE, smb_config_chk_string },
	{ SMB_CI_DOMAIN_FOREST, "forest", SCF_TYPE_ASTRING,
		0, MAXHOSTNAMELEN, 0, B_FALSE, smb_config_chk_string },
	{ SMB_CI_DOMAIN_GUID, "domain_guid", SCF_TYPE_ASTRING,
		0, UUID_PRINTABLE_STRING_LENGTH, 0, B_FALSE,
		smb_config_chk_string },
	{ SMB_CI_DOMAIN_SRV, "pdc", SCF_TYPE_ASTRING,
		0, 0, 0, B_TRUE, smb_config_chk_hostname },
	{ SMB_CI_DC_SELECTED, "selected_dc", SCF_TYPE_ASTRING,
		0, 0, 0, B_FALSE, smb_config_chk_hostname },
	{ SMB_CI_DC_LEVEL, "dc_level", SCF_TYPE_INTEGER,
		0, 0, 0, B_FALSE, NULL },
	{ SMB_CI_DOMAIN_LEVEL, "domain_level", SCF_TYPE_INTEGER,
		0, 0, 0, B_FALSE, NULL },
	{ SMB_CI_FOREST_LEVEL, "forest_level", SCF_TYPE_INTEGER,
		0, 0, 0, B_FALSE, NULL },

	/* WINS configuration */
	{ SMB_CI_WINS_SRV1, "wins_server_1", SCF_TYPE_ASTRING,
		0, 0, 0, B_TRUE, smb_config_chk_hostname },
	{ SMB_CI_WINS_SRV2, "wins_server_2", SCF_TYPE_ASTRING,
		0, 0, 0, B_TRUE, smb_config_chk_hostname },
	{ SMB_CI_WINS_EXCL, "wins_exclude", SCF_TYPE_ASTRING,
		0, 0, 0, B_TRUE, smb_config_chk_interface },

	/* Kmod specific configuration */
	{ SMB_CI_MAX_WORKERS, "max_workers", SCF_TYPE_INTEGER,
		SMB_PI_MAX_WORKERS_MIN, SMB_PI_MAX_WORKERS_MAX,
		0, B_TRUE, smb_config_chk_range },
	{ SMB_CI_MAX_CONNECTIONS, "max_connections", SCF_TYPE_INTEGER,
		0, SMB_PI_MAX_CONNECTIONS_MAX, 0, B_TRUE,
		smb_config_chk_range },
	{ SMB_CI_KEEPALIVE, "keep_alive", SCF_TYPE_INTEGER,
		20, 5400, 0, B_TRUE, smb_config_chk_range_zero },
	{ SMB_CI_RESTRICT_ANON, "restrict_anonymous", SCF_TYPE_BOOLEAN,
		0, 0, 0, B_TRUE, smb_config_chk_boolean },
	{ SMB_CI_ENFORCE_VCZERO, "enforce_vczero", SCF_TYPE_BOOLEAN,
		0, 0, 0, B_TRUE, smb_config_chk_boolean },

	{SMB_CI_CLNT_SIGNING_REQD, "client_signing_required", SCF_TYPE_BOOLEAN,
		0, 0, 0, B_TRUE, smb_config_chk_boolean },
	{SMB_CI_SVR_SIGNING_ENABLE, "server_signing_enabled", SCF_TYPE_BOOLEAN,
		0, 0, 0, B_TRUE, smb_config_chk_boolean },
	{SMB_CI_SVR_SIGNING_REQD, "server_signing_required", SCF_TYPE_BOOLEAN,
		0, 0, 0, B_TRUE, smb_config_chk_boolean },

	/* Kmod tuning configuration */
	{ SMB_CI_SYNC_ENABLE, "sync_enable", SCF_TYPE_BOOLEAN,
		0, 0, 0, B_FALSE, smb_config_chk_boolean },

	/* SMBd configuration */
	{ SMB_CI_SECURITY, "security", SCF_TYPE_ASTRING,
		0, 0, 0, B_FALSE, NULL },
	{ SMB_CI_NBSCOPE, "netbios_scope", SCF_TYPE_ASTRING,
		0, MAX_VALUE_BUFLEN, 0, B_FALSE, NULL },
	{ SMB_CI_SYS_CMNT, "system_comment", SCF_TYPE_ASTRING,
		0, MAX_VALUE_BUFLEN, 0, B_TRUE, NULL },
	{ SMB_CI_CLNT_LM_LEVEL, "client_lmauth_level", SCF_TYPE_INTEGER,
		1, 5, 0, B_FALSE, smb_config_chk_range },
	{ SMB_CI_SVR_LM_LEVEL, "server_lmauth_level", SCF_TYPE_INTEGER,
		2, 5, 0, B_FALSE, smb_config_chk_range },
	{ SMB_CI_CLNT_EXTSEC, "client_extsec", SCF_TYPE_BOOLEAN,
		0, 0, 0, B_FALSE, smb_config_chk_boolean },
	{ SMB_CI_SVR_EXTSEC, "server_extsec", SCF_TYPE_BOOLEAN,
		0, 0, 0, B_FALSE, smb_config_chk_boolean },
	{ SMB_CI_CLNT_REV, "client_rev", SCF_TYPE_ASTRING,
		0, 0, 0, B_FALSE, NULL },
	{ SMB_CI_SVR_REV, "server_rev", SCF_TYPE_ASTRING,
		0, 0, 0, B_FALSE, NULL },

	/* ADS Configuration */
	{ SMB_CI_ADS_SITE, "ads_site", SCF_TYPE_ASTRING,
		0, MAX_VALUE_BUFLEN, 0, B_TRUE, NULL },

	/* Dynamic DNS */
	{ SMB_CI_DYNDNS_ENABLE, "ddns_enable", SCF_TYPE_BOOLEAN,
		0, 0, 0, B_FALSE, smb_config_chk_boolean },

	/*
	 * Primary DNS suffix of the local system is used in name resolution
	 * and name registration. This should be set only when the primary DNS
	 * domain name of the local system doesn't match with the fully
	 * qualified name of the AD domain.
	 */
	{ SMB_CI_DNS_SUFFIX, "dns_suffix", SCF_TYPE_ASTRING,
		0, MAXHOSTNAMELEN, 0, B_FALSE, smb_config_chk_string },

	{ SMB_CI_MACHINE_PASSWD, "machine_passwd", SCF_TYPE_ASTRING,
		0, MAX_VALUE_BUFLEN, SMB_CF_PROTECTED, B_FALSE, NULL },
	{ SMB_CI_KPASSWD_SRV, "kpasswd_server", SCF_TYPE_ASTRING,
		0, MAX_VALUE_BUFLEN, 0, B_FALSE, NULL },
	{ SMB_CI_KPASSWD_DOMAIN, "kpasswd_domain", SCF_TYPE_ASTRING,
		0, MAX_VALUE_BUFLEN, 0, B_FALSE, NULL },
	{ SMB_CI_KPASSWD_SEQNUM, "kpasswd_seqnum", SCF_TYPE_INTEGER,
		0, 0, 0, B_FALSE, NULL },
	{ SMB_CI_NETLOGON_SEQNUM, "netlogon_seqnum", SCF_TYPE_INTEGER,
		0, 0, 0, B_FALSE, NULL },
	{ SMB_CI_IPV6_ENABLE, "ipv6_enable", SCF_TYPE_BOOLEAN,
		0, 0, 0, B_TRUE, smb_config_chk_boolean },
	{ SMB_CI_PRINT_ENABLE, "print_enable", SCF_TYPE_BOOLEAN,
		0, 0, 0, B_TRUE, smb_config_chk_boolean },
	{ SMB_CI_MAP, "map", SCF_TYPE_ASTRING,
		0, 0, SMB_CF_EXEC, B_TRUE, smb_config_chk_cmd },
	{ SMB_CI_UNMAP, "unmap", SCF_TYPE_ASTRING,
		0, MAX_VALUE_BUFLEN, SMB_CF_EXEC, B_TRUE, smb_config_chk_cmd },
	{ SMB_CI_DISPOSITION, "disposition", SCF_TYPE_ASTRING,
		0, 0, SMB_CF_EXEC, B_TRUE, smb_config_chk_disposition },

	{ SMB_CI_DFS_STDROOT_NUM, "dfs_stdroot_num", SCF_TYPE_INTEGER,
		0, 0, 0, B_FALSE, NULL },
	{ SMB_CI_MACHINE_GUID, "machine_guid", SCF_TYPE_ASTRING,
		0, UUID_PRINTABLE_STRING_LENGTH, 0, B_FALSE,
		smb_config_chk_string },
	{ SMB_CI_NBNS_BCAST_MAX, "nbns_bcast_max", SCF_TYPE_INTEGER,
		0, 0, 0, B_FALSE, NULL },

	/* obsolete */
	{ SMB_CI_DOMAIN_NAME, "domain_name", SCF_TYPE_ASTRING,
		0, MAXHOSTNAMELEN, 0, B_FALSE, smb_config_chk_string },
	{ SMB_CI_DOMAIN_FQDN, "fqdn", SCF_TYPE_ASTRING,
		0, MAXHOSTNAMELEN, 0, B_FALSE, smb_config_chk_string },
	{ SMB_CI_SIGNING_ENABLE, "signing_enabled", SCF_TYPE_BOOLEAN,
		0, 0, 0, B_TRUE, smb_config_chk_boolean },
	{ SMB_CI_SIGNING_REQD, "signing_required", SCF_TYPE_BOOLEAN,
		0, 0, 0, B_TRUE, smb_config_chk_boolean },
	{ SMB_CI_LM_LEVEL, "lmauth_level", SCF_TYPE_INTEGER,
		2, 5, 0, B_FALSE, smb_config_chk_range }

	/* SMB_CI_MAX */
};

static struct {
	smb_cfg_id_t	id;
	const char	*attr;
} smb_cfg_dsattr[] = {
	{ SMB_CI_DC_LEVEL,	DS_ATTR_DCLEVEL },
	{ SMB_CI_DOMAIN_LEVEL,	DS_ATTR_DOMAINLEVEL },
	{ SMB_CI_FOREST_LEVEL,	DS_ATTR_FORESTLEVEL }
};

#define	SMB_CFG_DSATTRNUM (sizeof (smb_cfg_dsattr)/sizeof (smb_cfg_dsattr[0]))

static smb_cfg_param_t *smb_config_getent(smb_cfg_id_t);

static boolean_t smb_is_base64(unsigned char c);
static char *smb_base64_encode(char *str_to_encode);
static char *smb_base64_decode(char *encoded_str);

char *
smb_config_getname(smb_cfg_id_t id)
{
	smb_cfg_param_t *cfg;
	cfg = smb_config_getent(id);
	return (cfg->sc_name);
}

static boolean_t
smb_is_base64(unsigned char c)
{
	return (isalnum(c) || (c == '+') || (c == '/'));
}

/*
 * smb_base64_encode
 *
 * Encode a string using base64 algorithm.
 * Caller should free the returned buffer when done.
 */
static char *
smb_base64_encode(char *str_to_encode)
{
	int ret_cnt = 0;
	int i = 0, j = 0;
	char arr_3[3], arr_4[4];
	int len = strlen(str_to_encode);
	char *ret = malloc(SMB_ENC_LEN);

	if (ret == NULL) {
		return (NULL);
	}

	while (len--) {
		arr_3[i++] = *(str_to_encode++);
		if (i == 3) {
			arr_4[0] = (arr_3[0] & 0xfc) >> 2;
			arr_4[1] = ((arr_3[0] & 0x03) << 4) +
			    ((arr_3[1] & 0xf0) >> 4);
			arr_4[2] = ((arr_3[1] & 0x0f) << 2) +
			    ((arr_3[2] & 0xc0) >> 6);
			arr_4[3] = arr_3[2] & 0x3f;

			for (i = 0; i < 4; i++)
				ret[ret_cnt++] = b64_data[arr_4[i]];
			i = 0;
		}
	}

	if (i) {
		for (j = i; j < 3; j++)
			arr_3[j] = '\0';

		arr_4[0] = (arr_3[0] & 0xfc) >> 2;
		arr_4[1] = ((arr_3[0] & 0x03) << 4) +
		    ((arr_3[1] & 0xf0) >> 4);
		arr_4[2] = ((arr_3[1] & 0x0f) << 2) +
		    ((arr_3[2] & 0xc0) >> 6);
		arr_4[3] = arr_3[2] & 0x3f;

		for (j = 0; j < (i + 1); j++)
			ret[ret_cnt++] = b64_data[arr_4[j]];

		while (i++ < 3)
			ret[ret_cnt++] = '=';
	}

	ret[ret_cnt++] = '\0';
	return (ret);
}

/*
 * smb_base64_decode
 *
 * Decode using base64 algorithm.
 * Caller should free the returned buffer when done.
 */
static char *
smb_base64_decode(char *encoded_str)
{
	int len = strlen(encoded_str);
	int i = 0, j = 0;
	int en_ind = 0;
	char arr_4[4], arr_3[3];
	int ret_cnt = 0;
	char *ret;
	char *p;

	if ((ret = calloc(1, SMB_DEC_LEN)) == NULL)
		return (NULL);

	while (len-- && (encoded_str[en_ind] != '=') &&
	    smb_is_base64(encoded_str[en_ind])) {
		arr_4[i++] = encoded_str[en_ind];
		en_ind++;
		if (i == 4) {
			for (i = 0; i < 4; i++) {
				if ((p = strchr(b64_data, arr_4[i])) == NULL) {
					free(ret);
					return (NULL);
				}

				arr_4[i] = (int)(p - b64_data);
			}

			arr_3[0] = (arr_4[0] << 2) +
			    ((arr_4[1] & 0x30) >> 4);
			arr_3[1] = ((arr_4[1] & 0xf) << 4) +
			    ((arr_4[2] & 0x3c) >> 2);
			arr_3[2] = ((arr_4[2] & 0x3) << 6) +
			    arr_4[3];

			for (i = 0; i < 3; i++)
				ret[ret_cnt++] = arr_3[i];

			i = 0;
		}
	}

	if (i) {
		for (j = i; j < 4; j++)
			arr_4[j] = 0;

		for (j = 0; j < 4; j++) {
			if ((p = strchr(b64_data, arr_4[j])) == NULL) {
				free(ret);
				return (NULL);
			}

			arr_4[j] = (int)(p - b64_data);
		}
		arr_3[0] = (arr_4[0] << 2) +
		    ((arr_4[1] & 0x30) >> 4);
		arr_3[1] = ((arr_4[1] & 0xf) << 4) +
		    ((arr_4[2] & 0x3c) >> 2);
		arr_3[2] = ((arr_4[2] & 0x3) << 6) +
		    arr_4[3];
		for (j = 0; j < (i - 1); j++)
			ret[ret_cnt++] = arr_3[j];
	}

	ret[ret_cnt++] = '\0';
	return (ret);
}

static char *
smb_config_getenv_generic(char *name, char *svc_fmri_prefix, char *svc_propgrp)
{
	smb_scfhandle_t *handle;
	char *value;

	if ((value = malloc(MAX_VALUE_BUFLEN * sizeof (char))) == NULL)
		return (NULL);

	handle = smb_smf_scf_init(svc_fmri_prefix);
	if (handle == NULL) {
		free(value);
		return (NULL);
	}

	(void) smb_smf_create_service_pgroup(handle, svc_propgrp);

	if (smb_smf_get_string_property(handle, name, value,
	    sizeof (char) * MAX_VALUE_BUFLEN) != 0) {
		smb_smf_scf_fini(handle);
		free(value);
		return (NULL);
	}

	smb_smf_scf_fini(handle);
	return (value);

}

static int
smb_config_setenv_generic(char *svc_fmri_prefix, char *svc_propgrp,
    char *name, char *value)
{
	smb_scfhandle_t *handle = NULL;
	int rc = 0;


	handle = smb_smf_scf_init(svc_fmri_prefix);
	if (handle == NULL) {
		return (1);
	}

	(void) smb_smf_create_service_pgroup(handle, svc_propgrp);

	if (smb_smf_start_transaction(handle) != SMBD_SMF_OK) {
		smb_smf_scf_fini(handle);
		return (1);
	}

	if (smb_smf_set_string_property(handle, name, value) != SMBD_SMF_OK)
		rc = 1;

	if (smb_smf_end_transaction(handle) != SMBD_SMF_OK)
		rc = 1;

	smb_smf_scf_fini(handle);
	return (rc);
}

/*
 * smb_config_getstr
 *
 * Fetch the specified string configuration item from SMF
 */
int
smb_config_getstr(smb_cfg_id_t id, char *cbuf, int bufsz)
{
	smb_scfhandle_t *handle;
	smb_cfg_param_t *cfg;
	int rc = SMBD_SMF_OK;
	char *pg;
	char protbuf[SMB_ENC_LEN];
	char *tmp;

	*cbuf = '\0';
	cfg = smb_config_getent(id);
	assert(cfg->sc_type == SCF_TYPE_ASTRING);

	if (cfg->sc_flags & SMB_CF_PROTECTED) {
		handle = smb_smf_scf_init(SMBD_FMRI_PREFIX);
		if (handle == NULL)
			return (SMBD_SMF_SYSTEM_ERR);

		if ((rc = smb_smf_create_service_pgroup(handle,
		    SMBD_PROTECTED_PG_NAME)) != SMBD_SMF_OK)
			goto error;

		if ((rc = smb_smf_get_string_property(handle, cfg->sc_name,
		    protbuf, sizeof (protbuf))) != SMBD_SMF_OK)
			goto error;

		if (*protbuf != '\0') {
			tmp = smb_base64_decode(protbuf);
			(void) strlcpy(cbuf, tmp, bufsz);
			free(tmp);
		}
	} else {
		handle = smb_smf_scf_init(SMB_FMRI_PREFIX);
		if (handle == NULL)
			return (SMBD_SMF_SYSTEM_ERR);

		pg = (cfg->sc_flags & SMB_CF_EXEC) ? SMBD_EXEC_PG_NAME :
		    SMB_PG_NAME;
		rc = smb_smf_create_service_pgroup(handle, pg);
		if (rc == SMBD_SMF_OK)
			rc = smb_smf_get_string_property(handle, cfg->sc_name,
			    cbuf, bufsz);
	}

error:
	smb_smf_scf_fini(handle);
	return (rc);
}

/*
 * Translate the value of an astring SMF property into a binary
 * IP address. If the value is neither a valid IPv4 nor IPv6
 * address, attempt to look it up as a hostname using the
 * configured address type.
 */
int
smb_config_getip(smb_cfg_id_t sc_id, smb_inaddr_t *ipaddr)
{
	int rc, error;
	int a_family;
	char ipstr[MAXHOSTNAMELEN];
	struct hostent *h;
	smb_cfg_param_t *cfg;

	if (ipaddr == NULL)
		return (SMBD_SMF_INVALID_ARG);

	bzero(ipaddr, sizeof (smb_inaddr_t));
	rc = smb_config_getstr(sc_id, ipstr, sizeof (ipstr));
	if (rc == SMBD_SMF_OK) {
		if (*ipstr == '\0')
			return (SMBD_SMF_INVALID_ARG);

		if (inet_pton(AF_INET, ipstr, &ipaddr->a_ipv4) == 1) {
			ipaddr->a_family = AF_INET;
			return (SMBD_SMF_OK);
		}

		if (inet_pton(AF_INET6, ipstr, &ipaddr->a_ipv6) == 1) {
			ipaddr->a_family = AF_INET6;
			return (SMBD_SMF_OK);
		}

		/*
		 * The value is neither an IPv4 nor IPv6 address;
		 * so check if it's a hostname.
		 */
		a_family = smb_config_getbool(SMB_CI_IPV6_ENABLE) ?
		    AF_INET6 : AF_INET;
		h = getipnodebyname(ipstr, a_family, AI_DEFAULT,
		    &error);
		if (h != NULL) {
			bcopy(*(h->h_addr_list), &ipaddr->a_ip,
			    h->h_length);
			ipaddr->a_family = a_family;
			freehostent(h);
			rc = SMBD_SMF_OK;
		} else {
			cfg = smb_config_getent(sc_id);
			syslog(LOG_ERR, "smbd/%s: %s unable to get %s "
			    "address: %d", cfg->sc_name, ipstr,
			    a_family == AF_INET ?  "IPv4" : "IPv6", error);
			rc = SMBD_SMF_INVALID_ARG;
		}
	}

	return (rc);
}

/*
 * smb_config_getnum
 *
 * Returns the value of a numeric config param.
 */
int
smb_config_getnum(smb_cfg_id_t id, int64_t *cint)
{
	smb_scfhandle_t *handle;
	smb_cfg_param_t *cfg;
	int rc = SMBD_SMF_OK;
	int64_t val = 0;

	*cint = 0;
	cfg = smb_config_getent(id);
	assert(cfg->sc_type == SCF_TYPE_INTEGER);

	handle = smb_smf_scf_init(SMB_FMRI_PREFIX);
	if (handle == NULL)
		return (SMBD_SMF_SYSTEM_ERR);

	rc = smb_smf_create_service_pgroup(handle, SMB_PG_NAME);
	if (rc == SMBD_SMF_OK)
		rc = smb_smf_get_integer_property(handle, cfg->sc_name, &val);
	smb_smf_scf_fini(handle);

	if (rc == SMBD_SMF_OK)
		*cint = val;

	return (rc);
}

/*
 * smb_config_getbool
 *
 * Returns the value of a boolean config param.
 */
boolean_t
smb_config_getbool(smb_cfg_id_t id)
{
	smb_scfhandle_t *handle;
	smb_cfg_param_t *cfg;
	int rc = SMBD_SMF_OK;
	uint8_t vbool;

	cfg = smb_config_getent(id);
	assert(cfg->sc_type == SCF_TYPE_BOOLEAN);

	handle = smb_smf_scf_init(SMB_FMRI_PREFIX);
	if (handle == NULL)
		return (B_FALSE);

	rc = smb_smf_create_service_pgroup(handle, SMB_PG_NAME);
	if (rc == SMBD_SMF_OK)
		rc = smb_smf_get_boolean_property(handle, cfg->sc_name, &vbool);
	smb_smf_scf_fini(handle);

	return ((rc == SMBD_SMF_OK) ? (vbool == 1) : B_FALSE);
}

/*
 * smb_config_get
 *
 * This function returns the value of the requested config
 * iterm regardless of its type in string format. This should
 * be used when the config item type is not known by the caller.
 */
int
smb_config_get(smb_cfg_id_t id, char *cbuf, int bufsz)
{
	smb_cfg_param_t *cfg;
	int64_t cint;
	int rc;

	cfg = smb_config_getent(id);
	switch (cfg->sc_type) {
	case SCF_TYPE_ASTRING:
		return (smb_config_getstr(id, cbuf, bufsz));

	case SCF_TYPE_INTEGER:
		rc = smb_config_getnum(id, &cint);
		if (rc == SMBD_SMF_OK)
			(void) snprintf(cbuf, bufsz, "%lld", cint);
		return (rc);

	case SCF_TYPE_BOOLEAN:
		if (smb_config_getbool(id))
			(void) strlcpy(cbuf, "true", bufsz);
		else
			(void) strlcpy(cbuf, "false", bufsz);
		return (SMBD_SMF_OK);
	}

	return (SMBD_SMF_INVALID_ARG);
}

/*
 * smb_config_setstr
 *
 * Set the specified config param with the given
 * value.
 */
int
smb_config_setstr(smb_cfg_id_t id, char *value)
{
	smb_scfhandle_t *handle;
	smb_cfg_param_t *cfg;
	int rc = SMBD_SMF_OK;
	boolean_t protected;
	char *tmp = NULL;
	char *pg;

	cfg = smb_config_getent(id);
	assert(cfg->sc_type == SCF_TYPE_ASTRING);

	protected = B_FALSE;

	switch (cfg->sc_flags) {
	case SMB_CF_PROTECTED:
		handle = smb_smf_scf_init(SMBD_FMRI_PREFIX);
		protected = B_TRUE;
		pg = SMBD_PROTECTED_PG_NAME;
		break;
	case SMB_CF_EXEC:
		handle = smb_smf_scf_init(SMB_FMRI_PREFIX);
		pg = SMBD_EXEC_PG_NAME;
		break;
	default:
		handle = smb_smf_scf_init(SMB_FMRI_PREFIX);
		pg = SMB_PG_NAME;
		break;
	}

	if (handle == NULL)
		return (SMBD_SMF_SYSTEM_ERR);

	rc = smb_smf_create_service_pgroup(handle, pg);
	if (rc == SMBD_SMF_OK)
		rc = smb_smf_start_transaction(handle);

	if (rc != SMBD_SMF_OK) {
		smb_smf_scf_fini(handle);
		return (rc);
	}

	if (protected && value && (*value != '\0')) {
		if ((tmp = smb_base64_encode(value)) == NULL) {
			(void) smb_smf_end_transaction(handle);
			smb_smf_scf_fini(handle);
			return (SMBD_SMF_NO_MEMORY);
		}

		value = tmp;
	}

	rc = smb_smf_set_string_property(handle, cfg->sc_name, value);

	free(tmp);
	(void) smb_smf_end_transaction(handle);
	smb_smf_scf_fini(handle);
	return (rc);
}

/*
 * smb_config_setnum
 *
 * Sets a numeric configuration iterm
 */
int
smb_config_setnum(smb_cfg_id_t id, int64_t value)
{
	smb_scfhandle_t *handle;
	smb_cfg_param_t *cfg;
	int rc = SMBD_SMF_OK;

	cfg = smb_config_getent(id);
	assert(cfg->sc_type == SCF_TYPE_INTEGER);

	handle = smb_smf_scf_init(SMB_FMRI_PREFIX);
	if (handle == NULL)
		return (SMBD_SMF_SYSTEM_ERR);

	rc = smb_smf_create_service_pgroup(handle, SMB_PG_NAME);
	if (rc == SMBD_SMF_OK)
		rc = smb_smf_start_transaction(handle);

	if (rc != SMBD_SMF_OK) {
		smb_smf_scf_fini(handle);
		return (rc);
	}

	rc = smb_smf_set_integer_property(handle, cfg->sc_name, value);

	(void) smb_smf_end_transaction(handle);
	smb_smf_scf_fini(handle);
	return (rc);
}

/*
 * smb_config_setbool
 *
 * Sets a boolean configuration iterm
 */
int
smb_config_setbool(smb_cfg_id_t id, boolean_t value)
{
	smb_scfhandle_t *handle;
	smb_cfg_param_t *cfg;
	int rc = SMBD_SMF_OK;

	cfg = smb_config_getent(id);
	assert(cfg->sc_type == SCF_TYPE_BOOLEAN);

	handle = smb_smf_scf_init(SMB_FMRI_PREFIX);
	if (handle == NULL)
		return (SMBD_SMF_SYSTEM_ERR);

	rc = smb_smf_create_service_pgroup(handle, SMB_PG_NAME);
	if (rc == SMBD_SMF_OK)
		rc = smb_smf_start_transaction(handle);

	if (rc != SMBD_SMF_OK) {
		smb_smf_scf_fini(handle);
		return (rc);
	}

	rc = smb_smf_set_boolean_property(handle, cfg->sc_name, value);

	(void) smb_smf_end_transaction(handle);
	smb_smf_scf_fini(handle);
	return (rc);
}

/*
 * smb_config_set
 *
 * Sets the given value for the specified configuration item.
 * The property is specified by its name and the value is in
 * string format regardless of the property type.
 *
 * The value of property is validated before setting and an
 * error will be returned if it is not valid.
 *
 * If setting the value is successful smb/server maybe refreshed
 * depends on the property settings in smb_cfg_table[] above.
 */
int
smb_config_set(const char *propname, const char *propval)
{
	smb_cfg_param_t *cfg = NULL;
	int64_t cint;
	int rc, i;

	if ((propname == NULL) || (*propname == '\0'))
		return (SMBD_SMF_INVALID_ARG);

	if (propval == NULL)
		return (SMBD_SMF_INVALID_VALUE);

	for (i = 0; i < SMB_CI_MAX; i++) {
		if (strcasecmp(propname, smb_cfg_table[i].sc_name) == 0) {
			cfg = &smb_cfg_table[i];
			break;
		}
	}

	if (cfg == NULL)
		return (SMBD_SMF_INVALID_ARG);

	if ((cfg->sc_chk != NULL) && !cfg->sc_chk(cfg, propval))
		return (SMBD_SMF_INVALID_VALUE);

	switch (cfg->sc_type) {
	case SCF_TYPE_ASTRING:
		rc = smb_config_setstr(cfg->sc_id, (char *)propval);
		break;

	case SCF_TYPE_INTEGER:
		cint = atoi(propval);
		rc = smb_config_setnum(cfg->sc_id, cint);
		break;

	case SCF_TYPE_BOOLEAN:
		rc = smb_config_setbool(cfg->sc_id,
		    strcasecmp(propval, "true") == 0);
		break;

	default:
		rc = SMBD_SMF_INVALID_ARG;
		break;
	}

	if ((rc == SMBD_SMF_OK) && cfg->sc_refresh)
		(void) smf_refresh_instance(SMBD_DEFAULT_INSTANCE_FMRI);

	return (rc);
}

/*
 * smb_config_get_localsid
 *
 * Returns value of the "config/machine_sid" parameter
 * from the IDMAP SMF configuration repository.
 *
 */
char *
smb_config_get_localsid(void)
{
	return (smb_config_getenv_generic(MACHINE_SID, IDMAP_FMRI_PREFIX,
	    IDMAP_CONFIG_PG));
}

/*
 * smb_config_set_idmap_domain
 *
 * Set the "config/domain_name" parameter from IDMAP SMF repository.
 */
int
smb_config_set_idmap_domain(char *value)
{
	return (smb_config_setenv_generic(IDMAP_FMRI_PREFIX, IDMAP_CONFIG_PG,
	    IDMAP_DOMAIN, value));
}

int
smb_config_secmode_fromstr(char *secmode)
{
	if (secmode == NULL)
		return (SMB_SECMODE_WORKGRP);

	if (strcasecmp(secmode, SMB_SECMODE_DOMAIN_STR) == 0)
		return (SMB_SECMODE_DOMAIN);

	return (SMB_SECMODE_WORKGRP);
}

char *
smb_config_secmode_tostr(int secmode)
{
	if (secmode == SMB_SECMODE_DOMAIN)
		return (SMB_SECMODE_DOMAIN_STR);

	return (SMB_SECMODE_WORKGRP_STR);
}

int
smb_config_get_secmode()
{
	char p[16];

	(void) smb_config_getstr(SMB_CI_SECURITY, p, sizeof (p));
	return (smb_config_secmode_fromstr(p));
}

int
smb_config_set_secmode(int secmode)
{
	char *p;

	p = smb_config_secmode_tostr(secmode);
	return (smb_config_setstr(SMB_CI_SECURITY, p));
}

void
smb_config_getdomaininfo(char *nb_domain, char *ad_domain, char *sid,
    char *forest, char *guid)
{
	if (nb_domain)
		(void) smb_config_getstr(SMB_CI_DOMAIN_NB, nb_domain,
		    NETBIOS_NAME_SZ);

	if (ad_domain)
		(void) smb_config_getstr(SMB_CI_DOMAIN_AD, ad_domain,
		    MAXHOSTNAMELEN);

	if (sid)
		(void) smb_config_getstr(SMB_CI_DOMAIN_SID, sid,
		    SMB_SID_STRSZ);

	if (forest)
		(void) smb_config_getstr(SMB_CI_DOMAIN_FOREST, forest,
		    MAXHOSTNAMELEN);

	if (guid)
		(void) smb_config_getstr(SMB_CI_DOMAIN_GUID, guid,
		    UUID_PRINTABLE_STRING_LENGTH);
}

void
smb_config_setdomaininfo(char *nb_domain, char *ad_domain, char *sid,
    char *forest, char *guid)
{
	if (nb_domain)
		(void) smb_config_setstr(SMB_CI_DOMAIN_NB, nb_domain);

	if (ad_domain)
		(void) smb_config_setstr(SMB_CI_DOMAIN_AD, ad_domain);

	if (sid)
		(void) smb_config_setstr(SMB_CI_DOMAIN_SID, sid);
	if (forest)
		(void) smb_config_setstr(SMB_CI_DOMAIN_FOREST, forest);
	if (guid)
		(void) smb_config_setstr(SMB_CI_DOMAIN_GUID, guid);
}

void
smb_config_getdc(char *buf, size_t buflen)
{
	(void) smb_config_getstr(SMB_CI_DC_SELECTED, buf, buflen);
}

void
smb_config_setdc(char *selected_dc)
{
	(void) smb_config_setstr(SMB_CI_DC_SELECTED, selected_dc);
}

int
smb_config_getdsattr(const char *ds_attr, int64_t *level)
{
	int	i;

	for (i = 0; i < SMB_CFG_DSATTRNUM; ++i) {
		if (strcmp(ds_attr, smb_cfg_dsattr[i].attr) == 0)
			return (smb_config_getnum(smb_cfg_dsattr[i].id, level));
	}

	return (SMBD_SMF_SYSTEM_ERR);
}

void
smb_config_setdsattr(const char *ds_attr, int64_t level)
{
	int	i;
	int	rc;

	for (i = 0; i < SMB_CFG_DSATTRNUM; ++i) {
		if (strcmp(ds_attr, smb_cfg_dsattr[i].attr) == 0) {
			rc = smb_config_setnum(smb_cfg_dsattr[i].id, level);
			if (rc != 0)
				syslog(LOG_DEBUG, "smb_config_setdsattr %s: %d",
				    ds_attr, rc);
			return;
		}
	}
}

/*
 * The version stored in SMF in string format as N.N where
 * N is a number defined by Microsoft. The first number represents
 * the major version and the second number is the minor version.
 * Current defined values can be found here in 'ver_table'.
 *
 * This function reads the SMF string value and converts it to
 * two numbers returned in the given 'version' structure.
 * Current default version number is 5.0 which is for Windows 2000.
 */
void
smb_config_get_version(smb_version_t *version)
{
	smb_version_t tmpver;
	char verstr[SMB_VERSTR_LEN];
	char *p;
	int rc, i;
	static smb_version_t ver_table [] = {
		{ 0, SMB_MAJOR_NT,	SMB_MINOR_NT,		1381,	0 },
		{ 0, SMB_MAJOR_2000,	SMB_MINOR_2000,		2195,	0 },
		{ 0, SMB_MAJOR_XP,	SMB_MINOR_XP,		2196,	0 },
		{ 0, SMB_MAJOR_2003,	SMB_MINOR_2003,		2196,	0 },
		{ 0, SMB_MAJOR_VISTA,	SMB_MINOR_VISTA,	6000,	0 },
		{ 0, SMB_MAJOR_2008,	SMB_MINOR_2008,		6000,	0 },
		{ 0, SMB_MAJOR_2008R2,	SMB_MINOR_2008R2,	7007,	0 },
		{ 0, SMB_MAJOR_7,	SMB_MINOR_7,		7007,	0 }
	};

	*version = ver_table[1];
	version->sv_size = sizeof (smb_version_t);

	rc = smb_config_getstr(SMB_CI_VERSION, verstr, sizeof (verstr));
	if (rc != SMBD_SMF_OK)
		return;

	if ((p = strchr(verstr, '.')) == NULL)
		return;

	*p = '\0';
	tmpver.sv_major = (uint8_t)atoi(verstr);
	tmpver.sv_minor = (uint8_t)atoi(p + 1);

	for (i = 0; i < sizeof (ver_table)/sizeof (ver_table[0]); ++i) {
		if ((tmpver.sv_major == ver_table[i].sv_major) &&
		    (tmpver.sv_minor == ver_table[i].sv_minor)) {
			*version = ver_table[i];
			version->sv_size = sizeof (smb_version_t);
			break;
		}
	}
}

/*
 * Reads share exec script properties
 */
uint32_t
smb_config_get_execinfo(char *map, char *unmap, size_t bufsz)
{
	char buf[MAXPATHLEN];
	uint32_t flags = 0;

	if (map == NULL) {
		map = buf;
		bufsz = MAXPATHLEN;
	}

	*map = '\0';
	(void) smb_config_getstr(SMB_CI_MAP, map, bufsz);
	if (*map != '\0')
		flags |= SMB_EXEC_MAP;

	if (unmap == NULL) {
		unmap = buf;
		bufsz = MAXPATHLEN;
	}

	*unmap = '\0';
	(void) smb_config_getstr(SMB_CI_UNMAP, unmap, bufsz);
	if (*unmap != '\0')
		flags |= SMB_EXEC_UNMAP;

	*buf = '\0';
	(void) smb_config_getstr(SMB_CI_DISPOSITION, buf, sizeof (buf));
	if (*buf != '\0')
		if (strcasecmp(buf, SMB_EXEC_DISP_TERMINATE) == 0)
			flags |= SMB_EXEC_TERM;

	return (flags);
}

boolean_t
smb_config_isexec(smb_cfg_id_t id)
{
	smb_cfg_param_t *cfg;
	cfg = smb_config_getent(id);
	return ((cfg->sc_flags & SMB_CF_EXEC) == SMB_CF_EXEC);
}

static smb_cfg_param_t *
smb_config_getent(smb_cfg_id_t id)
{
	int i;

	for (i = 0; i < SMB_CI_MAX; i++)
		if (smb_cfg_table[i].sc_id == id)
			return (&smb_cfg_table[id]);

	assert(0);
	return (NULL);
}

/*
 * Check the length of the string
 */
static boolean_t
smb_config_chk_string(smb_cfg_param_t *cfg, const char *value)
{
	if (value == NULL)
		return (B_FALSE);

	if (strlen(value) > cfg->sc_maxval)
		return (B_FALSE);

	return (B_TRUE);
}

/*
 * Check the numerical range of given value
 */
static boolean_t
smb_config_chk_range(smb_cfg_param_t *cfg, const char *value)
{
	int val;

	errno = 0;
	val = strtoul(value, NULL, 0);
	if (errno != 0)
		return (B_FALSE);

	return ((val >= cfg->sc_minval) && (val <= cfg->sc_maxval));
}

/*
 * Check the numerical range of given value where zero is
 * also a valid value.
 */
static boolean_t
smb_config_chk_range_zero(smb_cfg_param_t *cfg, const char *value)
{
	int val;

	errno = 0;
	val = strtoul(value, NULL, 0);
	if (errno != 0)
		return (B_FALSE);

	return ((val == 0) ||
	    ((val >= cfg->sc_minval) && (val <= cfg->sc_maxval)));
}

/*
 * Check that the specified name is an IP address (v4 or v6) or a hostname.
 * Per RFC 1035 and 1123, names may contain alphanumeric characters, hyphens
 * and dots.  The first and last character of a label must be alphanumeric.
 * Interior characters may be alphanumeric or hypens.
 *
 * Domain names should not contain underscores but we allow them because
 * Windows names are often in non-compliance with this rule.
 */
/*ARGSUSED*/
static boolean_t
smb_config_chk_hostname(smb_cfg_param_t *cfg, const char *value)
{
	char sbytes[INET6_ADDRSTRLEN];
	boolean_t new_label = B_TRUE;
	char *p;
	char label_terminator;
	int len;

	if (value == NULL)
		return (B_TRUE);

	if ((len = strlen(value)) == 0)
		return (B_TRUE);

	if (inet_pton(AF_INET, value, (void *)sbytes) == 1)
		return (B_TRUE);

	if (inet_pton(AF_INET6, value, (void *)sbytes) == 1)
		return (B_TRUE);

	if (len >= MAXHOSTNAMELEN)
		return (B_FALSE);

	if (strspn(value, "0123456789.") == len)
		return (B_FALSE);

	label_terminator = *value;

	for (p = (char *)value; *p != '\0'; ++p) {
		if (new_label) {
			if (!isalnum(*p))
				return (B_FALSE);
			new_label = B_FALSE;
			label_terminator = *p;
			continue;
		}

		if (*p == '.') {
			if (!isalnum(label_terminator))
				return (B_FALSE);
			new_label = B_TRUE;
			label_terminator = *p;
			continue;
		}

		label_terminator = *p;

		if (isalnum(*p) || *p == '-' || *p == '_')
			continue;

		return (B_FALSE);
	}

	if (!isalnum(label_terminator))
		return (B_FALSE);

	return (B_TRUE);
}

/*
 * Call back function for dlpi_walk.
 * Returns TRUE if interface name exists on the host.
 */
static boolean_t
smb_config_ifcmp(const char *ifname, void *arg)
{
	smb_hostifs_walker_t *iterp = arg;

	iterp->hiw_matchfound = (strcmp(ifname, iterp->hiw_ifname) == 0);

	return (iterp->hiw_matchfound);
}

/*
 * Checks to see if the input interface exists on the host.
 * Returns B_TRUE if the match is found, B_FALSE otherwise.
 */
static boolean_t
smb_config_ifexists(const char *ifname)
{
	smb_hostifs_walker_t	iter;

	if ((ifname == NULL) || (*ifname == '\0'))
		return (B_FALSE);

	iter.hiw_ifname = ifname;
	iter.hiw_matchfound = B_FALSE;
	dlpi_walk(smb_config_ifcmp, &iter, 0);

	return (iter.hiw_matchfound);
}

/*
 * Check valid interfaces. Interface names value can be NULL or empty.
 * Returns B_FALSE if interface cannot be found on the host.
 */
/*ARGSUSED*/
static boolean_t
smb_config_chk_interface(smb_cfg_param_t *cfg, const char *value)
{
	char buf[16];
	int valid = B_TRUE;
	char *ifname, *tmp, *p;

	if (value == NULL || *value == '\0')
		return (valid);

	if (strlen(value) > MAX_VALUE_BUFLEN)
		return (B_FALSE);

	if ((p = strdup(value)) == NULL)
		return (B_FALSE);

	tmp = p;
	while ((ifname = strsep(&tmp, ",")) != NULL) {
		if (*ifname == '\0') {
			valid = B_FALSE;
			break;
		}

		if (!smb_config_ifexists(ifname)) {
			if (inet_pton(AF_INET, ifname, (void *)buf) == 0) {
				valid = B_FALSE;
				break;
			}
		}
	}

	free(p);
	return (valid);
}

/*
 * Check true/false
 */
/*ARGSUSED*/
static boolean_t
smb_config_chk_boolean(smb_cfg_param_t *cfg, const char *value)
{
	if (value == NULL)
		return (B_FALSE);

	return ((strcasecmp(value, "true") == 0) ||
	    (strcasecmp(value, "false") == 0));
}

/*
 * Check path
 */
/*ARGSUSED*/
static boolean_t
smb_config_chk_path(smb_cfg_param_t *cfg, const char *path)
{
	struct stat buffer;
	int fd, status;

	if (path == NULL)
		return (B_FALSE);

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return (B_FALSE);

	status = fstat(fd, &buffer);
	(void) close(fd);

	if (status < 0)
		return (B_FALSE);

	return ((buffer.st_mode & S_IFDIR) == S_IFDIR);
}

/*
 * Checks to see if the command args are the supported substitution specifier.
 * i.e. <cmd> %U %S
 */
/*ARGSUSED*/
static boolean_t
smb_config_chk_cmd(smb_cfg_param_t *cfg, const char *value)
{
	char cmd[MAXPATHLEN];
	char *ptr, *v;
	boolean_t skip_cmdname;

	if (*value == '\0')
		return (B_TRUE);

	(void) strlcpy(cmd, value, sizeof (cmd));

	ptr = cmd;
	skip_cmdname = B_TRUE;
	do {
		if ((v = strsep(&ptr, " ")) == NULL)
			break;

		if (*v != '\0') {

			if (skip_cmdname) {
				skip_cmdname = B_FALSE;
				continue;
			}

			if ((strlen(v) != 2) || *v != '%')
				return (B_FALSE);

			if (strpbrk(v, SMB_VALID_SUB_CHRS) == NULL)
				return (B_FALSE);
		}

	} while (v != NULL);

	/*
	 * If skip_cmdname is still true then the string contains
	 * only spaces.  Don't allow such a string.
	 */
	if (skip_cmdname)
		return (B_FALSE);

	return (B_TRUE);
}

/*ARGSUSED*/
static boolean_t
smb_config_chk_disposition(smb_cfg_param_t *cfg, const char *value)
{
	if (value == NULL)
		return (B_FALSE);

	if (*value == '\0')
		return (B_TRUE);

	return ((strcasecmp(value, SMB_EXEC_DISP_CONTINUE) == 0) ||
	    (strcasecmp(value, SMB_EXEC_DISP_TERMINATE) == 0));
}
