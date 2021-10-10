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
 * SMB specific functions
 */

#include <locale.h>
#include <errno.h>
#include <zone.h>
#include <note.h>

#include <smbsrv/libsmb.h>
#include <smbsrv/smb_share.h>

#include <libshare.h>
#include <libshare_impl.h>

#define	SCFTYPE_LEN		32
#define	CHUNKSIZE		256

/*
 * defined options types.
 */
#define	OPT_TYPE_ANY		0
#define	OPT_TYPE_STRING		1
#define	OPT_TYPE_BOOLEAN	2
#define	OPT_TYPE_NUMBER		3
#define	OPT_TYPE_PATH		4
#define	OPT_TYPE_PROTOCOL	5
#define	OPT_TYPE_ACCLIST	6
#define	OPT_TYPE_CSC		7

/*
 * protocol property management routines
 */

static void sa_smb_free_proto_proplist(void);
static int sa_smb_init_proto_proplist(void);

/*
 * protocol plugin op routines
 */
static int sa_smb_init(void);
static void sa_smb_fini(void);
static int sa_smb_share_parse(const char *, int, nvlist_t **, char *, size_t);
static int sa_smb_share_merge(nvlist_t *, nvlist_t *, int, char *, size_t);
static int sa_smb_share_set_def_proto(nvlist_t *);
static int sa_smb_share_validate_name(const char *, boolean_t, char *, size_t);
static int sa_smb_share_validate(nvlist_t *, boolean_t, char *, size_t);
static int sa_smb_share_publish(nvlist_t *, int);
static int sa_smb_share_unpublish(nvlist_t *, int);
static int sa_smb_share_unpublish_byname(const char *, const char *, int);
static int sa_smb_share_publish_admin(const char *);
static int sa_smb_fs_publish(nvlist_t *, int);
static int sa_smb_fs_unpublish(nvlist_t *, int);
static int sa_smb_format_props(nvlist_t *, char **);

/* share property check routines */
static int sa_smb_check_boolean(const char *, const char *, char *, size_t);
static int sa_smb_check_ad(const char *, const char *, char *, size_t);
static int sa_smb_check_csc(const char *, const char *, char *, size_t);
static int sa_smb_check_acclist(const char *, const char *, char *, size_t);

/* protocol property op routines */
static int sa_smb_proto_get_features(uint64_t *);
static int sa_smb_proto_get_proplist(nvlist_t **);
static int sa_smb_proto_get_status(char **);
static int sa_smb_proto_get_property(const char *, const char *, char **);
static int sa_smb_proto_set_property(const char *, const char *, const char *);

sa_proto_ops_t sa_plugin_ops = {
	.sap_hdr = {
		.pi_ptype = SA_PLUGIN_PROTO,
		.pi_type = SA_PROT_SMB,
		.pi_name = "smb",
		.pi_version = SA_LIBSHARE_VERSION,
		.pi_flags = 0,
		.pi_init = sa_smb_init,
		.pi_fini = sa_smb_fini
	},
	.sap_share_parse = sa_smb_share_parse,
	.sap_share_merge = sa_smb_share_merge,
	.sap_share_set_def_proto = sa_smb_share_set_def_proto,
	.sap_share_validate_name = sa_smb_share_validate_name,
	.sap_share_validate = sa_smb_share_validate,
	.sap_share_publish = sa_smb_share_publish,
	.sap_share_unpublish = sa_smb_share_unpublish,
	.sap_share_unpublish_byname = sa_smb_share_unpublish_byname,
	.sap_share_publish_admin = sa_smb_share_publish_admin,
	.sap_fs_publish = sa_smb_fs_publish,
	.sap_fs_unpublish = sa_smb_fs_unpublish,
	.sap_share_prop_format = sa_smb_format_props,

	.sap_proto_get_features = sa_smb_proto_get_features,
	.sap_proto_get_proplist = sa_smb_proto_get_proplist,
	.sap_proto_get_status = sa_smb_proto_get_status,
	.sap_proto_get_property = sa_smb_proto_get_property,
	.sap_proto_set_property = sa_smb_proto_set_property,
	.sap_proto_rem_section = NULL
};

static smb_cfg_id_t smb_proto_options[] = {
	SMB_CI_SYS_CMNT,
	SMB_CI_MAX_WORKERS,
	SMB_CI_MAX_CONNECTIONS,
	SMB_CI_NBSCOPE,
	SMB_CI_CLNT_LM_LEVEL,
	SMB_CI_SVR_LM_LEVEL,
	SMB_CI_KEEPALIVE,
	SMB_CI_WINS_SRV1,
	SMB_CI_WINS_SRV2,
	SMB_CI_WINS_EXCL,
	SMB_CI_CLNT_SIGNING_REQD,
	SMB_CI_SVR_SIGNING_ENABLE,
	SMB_CI_SVR_SIGNING_REQD,
	SMB_CI_RESTRICT_ANON,
	SMB_CI_DOMAIN_SRV,
	SMB_CI_ADS_SITE,
	SMB_CI_DYNDNS_ENABLE,
	SMB_CI_DNS_SUFFIX,
	SMB_CI_AUTOHOME_MAP,
	SMB_CI_IPV6_ENABLE,
	SMB_CI_PRINT_ENABLE,
	SMB_CI_MAP,
	SMB_CI_UNMAP,
	SMB_CI_DISPOSITION
};

#define	SMB_PROTO_OPTNUM \
	(sizeof (smb_proto_options) / sizeof (smb_proto_options[0]))

typedef struct sa_smb_share_opt {
	char *tag;
	int type;
	int (*check)(const char *, const char *, char *, size_t);
} sa_smb_share_opt_t;

static sa_smb_share_opt_t smb_share_options[] = {
	{ SHOPT_ABE,		OPT_TYPE_BOOLEAN,	sa_smb_check_boolean },
	{ SHOPT_CATIA,		OPT_TYPE_BOOLEAN,	sa_smb_check_boolean },
	{ SHOPT_GUEST,		OPT_TYPE_BOOLEAN,	sa_smb_check_boolean },
	{ SHOPT_DFSROOT,	OPT_TYPE_BOOLEAN,	sa_smb_check_boolean },
	{ SHOPT_CSC,		OPT_TYPE_CSC,		sa_smb_check_csc },
	{ SHOPT_AD_CONTAINER,	OPT_TYPE_STRING,	sa_smb_check_ad },
	{ SHOPT_RO,		OPT_TYPE_ACCLIST,	sa_smb_check_acclist },
	{ SHOPT_RW,		OPT_TYPE_ACCLIST,	sa_smb_check_acclist },
	{ SHOPT_NONE,		OPT_TYPE_ACCLIST,	sa_smb_check_acclist },
	{ NULL, 0, NULL }
};

static nvlist_t *smb_proto_proplist = NULL;

static int
sa_smb_init(void)
{
	return (SA_OK);
}

static void
sa_smb_fini(void)
{
	sa_smb_free_proto_proplist();
}

/*
 * sa_smb_share_findopt(name)
 *
 * Lookup option "name" in the option table and return ptr
 * to table entry.
 */
static sa_smb_share_opt_t *
sa_smb_share_findopt(const char *name)
{
	sa_smb_share_opt_t *opt;

	if (name != NULL) {
		for (opt = smb_share_options; opt->tag != NULL; opt++) {
			if (strcmp(opt->tag, name) == 0)
				return (opt);
		}
	}

	return (NULL);
}

static int
sa_smb_share_findopt_type(const char *name)
{
	sa_smb_share_opt_t *opt;

	if (name != NULL) {
		for (opt = smb_share_options; opt->tag != NULL; opt++) {
			if (strcmp(opt->tag, name) == 0)
				return (opt->type);
		}
	}

	return (-1);
}

static boolean_t
sa_smb_acccmp(char *prop1, char *prop2)
{
	if (prop1 != NULL && prop2 != NULL &&
	    strcmp(prop1, prop2) == 0)
		return (B_TRUE);
	else
		return (B_FALSE);
}

/*
 * Validate access list values. Should eventually validate that all
 * the values make sense. Also, ro and rw may have cross value conflicts.
 * These are the current applicable rules:
 *
 *   ro != rw
 *   ro != none
 *   rw != none
 */
static int
sa_smb_validate_acclist(nvlist_t *optlist, char *errbuf, size_t buflen)
{
	char *ro, *rw, *none;
	char *prop1, *prop2;
	int rc;

	ro = sa_share_get_prop(optlist, SHOPT_RO);
	rw = sa_share_get_prop(optlist, SHOPT_RW);
	none = sa_share_get_prop(optlist, SHOPT_NONE);

	if (sa_smb_acccmp(ro, rw)) {
		prop1 = SHOPT_RO;
		prop2 = SHOPT_RW;
		goto invalid;
	}

	if (sa_smb_acccmp(ro, none)) {
		prop1 = SHOPT_RO;
		prop2 = SHOPT_NONE;
		goto invalid;
	}

	if (sa_smb_acccmp(rw, none)) {
		prop1 = SHOPT_RW;
		prop2 = SHOPT_NONE;
		goto invalid;
	}

	return (SA_OK);

invalid:
	rc = SA_INVALID_ACCLIST_PROP_VAL;
	(void) snprintf(errbuf, buflen,
	    dgettext(TEXT_DOMAIN, "%s: %s and %s cannot have the same value"),
	    sa_strerror(rc), prop1, prop2);
	return (rc);
}

static int
sa_smb_check_boolean(const char *prop, const char *value, char *errbuf,
    size_t buflen)
{
	int rc = SA_OK;

	if (value == NULL)
		return (SA_INVALID_PROP_VAL);

	if ((strcasecmp(value, "true") != 0) &&
	    (strcasecmp(value, "false") != 0)) {
		rc = SA_INVALID_PROP_VAL;
		(void) snprintf(errbuf, buflen,
		    dgettext(TEXT_DOMAIN, "%s: must be true|false: %s=%s"),
		    sa_strerror(rc), prop, value);
	}

	return (rc);
}

static int
sa_smb_check_csc(const char *prop, const char *value, char *errbuf,
    size_t buflen)
{
	int rc = SA_OK;

	if (!smb_share_csc_valid(value)) {
		rc = SA_INVALID_PROP_VAL;
		(void) snprintf(errbuf, buflen, "%s: %s=%s",
		    sa_strerror(rc), prop, value);
	}

	return (rc);
}

/*
 * sa_smb_check_ad
 *
 * check syntax of ad-container value.
 * This will only make sure that each name has a value.
 * There is no checking for validity of DN/RDN values.
 */
static int
sa_smb_check_ad(const char *prop, const char *value, char *errbuf,
    size_t buflen)
{
	int rc;
	char *strprop;
	char *namep;
	char *valp;
	char *nextp;

	if ((strprop = strdup(value)) == NULL) {
		rc = SA_NO_MEMORY;
		(void) snprintf(errbuf, buflen, "%s", sa_strerror(rc));
		return (rc);
	}

	/*
	 * make sure each name has a value
	 */
	for (namep = strprop;
	    namep != NULL;
	    namep = nextp) {
		nextp = strchr(namep, ',');
		if (nextp != NULL) {
			*nextp = '\0';
			if (nextp[1] == '\0')
				nextp = NULL;
			else
				nextp++;
		}
		valp = strchr(namep, '=');
		if (valp == NULL || valp[1] == '\0') {
			rc = SA_SYNTAX_ERR;
			(void) snprintf(errbuf, buflen,
			    dgettext(TEXT_DOMAIN, "syntax error: %s: "
			    "missing value for '%s' attribute"), prop, namep);
			free(strprop);
			return (rc);
		} else {
			*valp = '\0';
			valp++;
		}
	}
	free(strprop);

	return (SA_OK);
}

/*
 * sa_smb_check_acclist
 *
 * check syntax of access list value
 * This will only check multi-entry values.
 * Cannot have '*' with other values
 */
static int
sa_smb_check_acclist(const char *prop, const char *value, char *errbuf,
    size_t buflen)
{
	char *savp, *propval;
	char *token;
	char *lasts;
	int tok_cnt = 0;
	int found_wildcard = 0;
	int rc = SA_OK;

	if ((propval = strdup(value)) == NULL)
		return (SA_NO_MEMORY);
	savp = propval;

	while ((token = strtok_r(propval, ":", &lasts)) != NULL) {
		if (strcmp(token, "*") == 0)
			found_wildcard = 1;
		propval = NULL;
		tok_cnt++;
	}

	if (tok_cnt > 1 && found_wildcard) {
		rc = SA_INVALID_PROP_VAL;
		(void) snprintf(errbuf, buflen,
		    dgettext(TEXT_DOMAIN,
		    "%s: cannot mix \"*\" with other values: %s=%s"),
		    sa_strerror(rc), prop, value);
	}

	free(savp);

	return (rc);
}

/*
 * parse the protocol property string into a nvlist of name=value pairs.
 *
 * INPUT:
 *   prot_props : protocol property string
 *   unset: set if -c option was specified, remove
 */
static int
sa_smb_share_parse(const char *prot_props, int unset, nvlist_t **prot_nvl,
    char *errbuf, size_t buflen)
{
	char *strprop = NULL;
	char *namep;
	char *valp;
	char *nextp;
	char *tmpp;
	char *delimp;
	nvlist_t *nvl = NULL;
	char *propval;
	char *newval;
	int ptype;
	int rc;

	if (prot_nvl == NULL) {
		(void) snprintf(errbuf, buflen,
		    dgettext(TEXT_DOMAIN, "Internal error: prot_nvl==NULL"));
		return (SA_INTERNAL_ERR);
	}

	/*
	 * allocate a new protocol nvlist
	 */
	if (nvlist_alloc(&nvl, NV_UNIQUE_NAME, 0) != 0) {
		rc = SA_NO_MEMORY;
		(void) snprintf(errbuf, buflen, "%s", sa_strerror(rc));
		goto err_out;
	}

	/*
	 * If no property string, then return return
	 * empty proplist nvlist
	 */
	if (prot_props == NULL) {
		*prot_nvl = nvl;
		return (SA_OK);
	}

	strprop = strdup(prot_props);
	if (strprop == NULL) {
		rc = SA_NO_MEMORY;
		(void) snprintf(errbuf, buflen, "%s", sa_strerror(rc));
		goto err_out;
	}

	for (namep = strprop;
	    namep != NULL;
	    namep = nextp) {
		nextp = strchr(namep, ',');
		if (nextp != NULL) {
			*nextp = '\0';
			if (nextp[1] == '\0')
				nextp = NULL;
			else
				nextp++;
		}
		valp = strchr(namep, '=');
		if (valp == NULL) {
			/* equal sign not found */
			if (unset) {
				/* not needed for unset */
				valp = "";
			} else {
				/*
				 * if property type is boolean
				 * then default to 'true'
				 * if property type is access list
				 * then default to '*'
				 */
				ptype = sa_smb_share_findopt_type(namep);
				if (ptype == OPT_TYPE_BOOLEAN)
					valp = "true";
				else if (ptype == OPT_TYPE_ACCLIST)
					valp = "*";
				else {
					rc = SA_SYNTAX_ERR;
					(void) snprintf(errbuf, buflen,
					    dgettext(TEXT_DOMAIN,
					    "syntax error: "
					    "no value for '%s' property"),
					    namep);
					goto err_out;
				}
			}
		} else if (valp[1] == '\0') {
			if (unset) {
				*valp = '\0';
				valp = "";
			} else {
				rc = SA_SYNTAX_ERR;
				(void) snprintf(errbuf, buflen,
				    dgettext(TEXT_DOMAIN,
				    "syntax error: no value for '%s' property"),
				    namep);
				goto err_out;
			}
		} else {
			*valp = '\0';
			valp++;
		}

		/*
		 * special processing for ad-container
		 * the value for ad-container is a comma separated
		 * list of name/value pairs. Therefore search forward
		 * in option string for the next valid smb property.
		 * ie ad-container=dc=com,dc=oracle,ou=sales,cn=users
		 *
		 * This does not validate the ad-container value
		 */
		if (strcmp(namep, SHOPT_AD_CONTAINER) == 0 &&
		    nextp != NULL) {
			/* save location of ',' */
			delimp = nextp - 1;
			/* keep track of name location */
			tmpp = nextp;
			while (nextp != NULL) {
				/* find end of current name */
				nextp = strchr(tmpp, '=');
				if (nextp == NULL) {
					nextp = tmpp;
					break;
				}
				/* replace '=' with '\0' */
				*nextp = '\0';
				if (sa_smb_share_findopt(tmpp) != NULL) {
					/*
					 * found valid option
					 * restore and exit
					 */
					*nextp = '=';
					nextp = tmpp;
					break;
				} else {
					/*
					 * not a valid smb option
					 * restore '=' and ',' and look
					 * for next name/value pair
					 */
					*nextp = '=';
					*delimp = ',';
					nextp = strchr(tmpp, ',');
					if (nextp != NULL) {
						/*
						 * replace ',' with '\0'
						 * and setup for next pair
						 */
						*nextp = '\0';
						if (nextp[1] == '\0')
							nextp = NULL;
						else {
							delimp = nextp;
							nextp++;
							tmpp = nextp;
						}
					}
				}
			}
		}

		newval = NULL;
		if ((propval = sa_share_get_prop(nvl, namep)) != NULL) {
			/*
			 * append access list value for this property
			 */
			ptype = sa_smb_share_findopt_type(namep);
			if (ptype == OPT_TYPE_ACCLIST) {
				(void) asprintf(&newval, "%s:%s",
				    propval, valp);
				valp = newval;
			}
			/*
			 * all other property types will use last
			 * value defined.
			 */
		}

		rc = sa_share_set_prop(nvl, namep, valp);
		free(newval);
		if (rc != SA_OK) {
			(void) snprintf(errbuf, buflen, "%s", sa_strerror(rc));
			goto err_out;
		}
	}

	free(strprop);
	*prot_nvl = nvl;
	return (SA_OK);

err_out:
	if (nvl != NULL)
		nvlist_free(nvl);
	if (strprop != NULL)
		free(strprop);
	*prot_nvl = NULL;
	return (rc);
}

static int
sa_smb_share_merge(nvlist_t *dst_prot_nvl, nvlist_t *src_prot_nvl, int unset,
    char *errbuf, size_t buflen)
{
	nvpair_t *nvp;
	int rc;

	if (unset) {
		for (nvp = nvlist_next_nvpair(src_prot_nvl, NULL);
		    nvp != NULL;
		    nvp = nvlist_next_nvpair(src_prot_nvl, nvp)) {
			if (nvlist_remove(dst_prot_nvl, nvpair_name(nvp),
			    DATA_TYPE_STRING) != 0) {
				rc = SA_NO_SUCH_PROP;
				(void) snprintf(errbuf, buflen, "%s: %s",
				    sa_strerror(rc), nvpair_name(nvp));
				return (rc);
			}
		}
	} else {
		if (nvlist_merge(dst_prot_nvl, src_prot_nvl, 0) != 0) {
			rc = SA_NO_MEMORY;
			(void) snprintf(errbuf, buflen, "%s", sa_strerror(rc));
			return (rc);
		}
	}

	return (SA_OK);
}

/*
 * Set the default protocol properties on the share.
 *
 * No special properties required. Just set an empty
 * protocol list with "smb" as the name.
 */
static int
sa_smb_share_set_def_proto(nvlist_t *share)
{
	int rc;
	nvlist_t *prot_nvl;

	if (nvlist_alloc(&prot_nvl, NV_UNIQUE_NAME, 0) != 0) {
		salog_error(SA_NO_MEMORY, "smb_share_set_def_proto");
		return (SA_NO_MEMORY);
	}

	if ((rc = sa_share_set_proto(share, SA_PROT_SMB, prot_nvl)) != SA_OK)
		salog_error(rc, "smb_share_set_def_proto");
	nvlist_free(prot_nvl);
	return (rc);
}

static int
sa_smb_share_validate_name(const char *sh_name, boolean_t new,
    char *errbuf, size_t buflen)
{
	NOTE(ARGUNUSED(new))
	int rc;

	/*
	 * Make sure no invalid characters
	 */
	if (smb_name_validate_share(sh_name) != ERROR_SUCCESS) {
		rc = SA_INVALID_SHARE_NAME;
		(void) snprintf(errbuf, buflen, "%s: %s",
		    sa_strerror(rc), sh_name);
		return (rc);
	}

	return (SA_OK);
}

static int
sa_smb_share_validate(nvlist_t *share, boolean_t new, char *errbuf,
    size_t buflen)
{
	NOTE(ARGUNUSED(new))
	int rc = SA_OK;
	char *sh_name;
	char *prop_name;
	char *prop_val;
	nvlist_t *prot_nvl;
	nvpair_t *nvp;
	boolean_t skip_check = B_FALSE;
	sa_smb_share_opt_t *opt;

	/*
	 * do not allow a share in a non-global zone
	 */
	if (getzoneid() != GLOBAL_ZONEID) {
		rc = SA_INVALID_ZONE;
		(void) snprintf(errbuf, buflen, "SMB: %s", sa_strerror(rc));
		return (rc);
	}

	sh_name = sa_share_get_name(share);
	if (sh_name == NULL) {
		rc = SA_NO_SHARE_NAME;
		(void) snprintf(errbuf, buflen, "%s", sa_strerror(rc));
		return (rc);
	}

	if (smb_name_validate_share(sh_name) != ERROR_SUCCESS) {
		rc = SA_INVALID_SHARE_NAME;
		(void) snprintf(errbuf, buflen, "%s: %s",
		    sa_strerror(rc), sh_name);
		return (rc);
	}

	/*
	 * get protocol property list from share
	 */
	prot_nvl = sa_share_get_proto(share, SA_PROT_SMB);
	if (prot_nvl == NULL) {
		rc = SA_NO_SHARE_PROTO;
		(void) snprintf(errbuf, buflen, "%s", sa_strerror(rc));
		return (rc);
	}

	/*
	 * validate each property in the list
	 */
	for (nvp = nvlist_next_nvpair(prot_nvl, NULL);
	    nvp != NULL; nvp = nvlist_next_nvpair(prot_nvl, nvp)) {

		prop_name = nvpair_name(nvp);
		if (nvpair_value_string(nvp, &prop_val) != 0) {
			rc = SA_INTERNAL_ERR;
			(void) snprintf(errbuf, buflen, "%s: %s",
			    sa_strerror(rc), prop_name);
			break;
		}

		if ((opt = sa_smb_share_findopt(prop_name)) == NULL) {
			rc = SA_INVALID_SMB_PROP;
			(void) snprintf(errbuf, buflen, "%s: %s",
			    sa_strerror(rc), prop_name);
			break;
		}

		/*
		 * first basic type checking
		 */
		if (opt->check != NULL) {
			rc = opt->check(prop_name, prop_val, errbuf, buflen);
			if (rc != SA_OK)
				break;
		}

		if (!skip_check && (opt->type == OPT_TYPE_ACCLIST)) {
			rc = sa_smb_validate_acclist(prot_nvl, errbuf, buflen);
			if (rc != SA_OK)
				break;
			skip_check = B_TRUE;
		}
	}

	return (rc);
}

#ifdef ENABLE_SERVICE
/*
 * sa_smb_isonline()
 *
 * Determine if the SMF service instance is in the online state or
 * not. A number of operations depend on this state.
 */
static boolean_t
sa_smb_isonline(void)
{
	char *str;
	boolean_t rc = B_FALSE;

	if ((str = smf_get_state(SMBD_DEFAULT_INSTANCE_FMRI)) != NULL) {
		rc = (strcmp(str, SCF_STATE_STRING_ONLINE) == 0);
		free(str);
	}
	return (rc);
}

/*
 * sa_smb_isdisabled()
 *
 * Determine if the SMF service instance is in the disabled state or
 * not. A number of operations depend on this state.
 */
static boolean_t
sa_smb_isdisabled(void)
{
	char *str;
	boolean_t rc = B_FALSE;

	if ((str = smf_get_state(SMBD_DEFAULT_INSTANCE_FMRI)) != NULL) {
		rc = (strcmp(str, SCF_STATE_STRING_DISABLED) == 0);
		free(str);
	}
	return (rc);
}

/*
 * sa_smb_isautoenable()
 *
 * Determine if the SMF service instance auto_enabled set or not. A
 * number of operations depend on this state.  The property not being
 * set or being set to true means autoenable.  Only being set to false
 * is not autoenabled.
 */
static boolean_t
sa_smb_isautoenable(void)
{
	boolean_t rc = B_TRUE;
	scf_simple_prop_t *prop;
	uint8_t *retstr;

	prop = scf_simple_prop_get(NULL, SMBD_DEFAULT_INSTANCE_FMRI,
	    "application", "auto_enable");
	if (prop != NULL) {
		retstr = scf_simple_prop_next_boolean(prop);
		rc = *retstr != 0;
		scf_simple_prop_free(prop);
	}
	return (rc);
}

/*
 * sa_smb_ismaint()
 *
 * Determine if the SMF service instance is in the disabled state or
 * not. A number of operations depend on this state.
 */
static boolean_t
sa_smb_ismaint(void)
{
	char *str;
	boolean_t rc = B_FALSE;

	if ((str = smf_get_state(SMBD_DEFAULT_INSTANCE_FMRI)) != NULL) {
		rc = (strcmp(str, SCF_STATE_STRING_MAINT) == 0);
		free(str);
	}
	return (rc);
}

/*
 * sa_smb_enable_dependencies()
 *
 * SMBD_DEFAULT_INSTANCE_FMRI may have some dependencies that aren't
 * enabled. This will attempt to enable all of them.
 */
static void
sa_smb_enable_dependencies(const char *fmri)
{
	scf_handle_t *handle;
	scf_service_t *service;
	scf_instance_t *inst = NULL;
	scf_iter_t *iter;
	scf_property_t *prop;
	scf_value_t *value;
	scf_propertygroup_t *pg;
	scf_scope_t *scope;
	char type[SCFTYPE_LEN];
	char *dependency;
	char *servname;
	int maxlen;

	/*
	 * Get all required handles and storage.
	 */
	handle = scf_handle_create(SCF_VERSION);
	if (handle == NULL)
		return;

	if (scf_handle_bind(handle) != 0) {
		scf_handle_destroy(handle);
		return;
	}

	maxlen = scf_limit(SCF_LIMIT_MAX_VALUE_LENGTH);
	if (maxlen == (ssize_t)-1)
		maxlen = MAXPATHLEN;

	dependency = malloc(maxlen);

	service = scf_service_create(handle);
	iter = scf_iter_create(handle);
	pg = scf_pg_create(handle);
	prop = scf_property_create(handle);
	value = scf_value_create(handle);
	scope = scf_scope_create(handle);

	if (service == NULL || iter == NULL || pg == NULL || prop == NULL ||
	    value == NULL || scope == NULL || dependency == NULL)
		goto done;

	/*
	 *  We passed in the FMRI for the default instance but for
	 *  some things we need the simple form so construct it. Since
	 *  we reuse the storage that dependency points to, we need to
	 *  use the servname early.
	 */
	(void) snprintf(dependency, maxlen, "%s", fmri + sizeof ("svc:"));
	servname = strrchr(dependency, ':');
	if (servname == NULL)
		goto done;
	*servname = '\0';
	servname = dependency;

	/*
	 * Setup to iterate over the service property groups, only
	 * looking at those that are "dependency" types. The "entity"
	 * property will have the FMRI of the service we are dependent
	 * on.
	 */
	if (scf_handle_get_scope(handle, SCF_SCOPE_LOCAL, scope) != 0)
		goto done;

	if (scf_scope_get_service(scope, servname, service) != 0)
		goto done;

	if (scf_iter_service_pgs(iter, service) != 0)
		goto done;

	while (scf_iter_next_pg(iter, pg) > 0) {
		char *services[2];
		/*
		 * Have a property group for the service. See if it is
		 * a dependency pg and only do operations on those.
		 */
		if (scf_pg_get_type(pg, type, SCFTYPE_LEN) <= 0)
			continue;

		if (strncmp(type, SCF_GROUP_DEPENDENCY, SCFTYPE_LEN) != 0)
			continue;
		/*
		 * Have a dependency.  Attempt to enable it.
		 */
		if (scf_pg_get_property(pg, SCF_PROPERTY_ENTITIES, prop) != 0)
			continue;

		if (scf_property_get_value(prop, value) != 0)
			continue;

		services[1] = NULL;

		if (scf_value_get_as_string(value, dependency, maxlen) > 0) {
			services[0] = dependency;
			_check_services(services);
		}
	}

done:
	if (dependency != NULL)
		free(dependency);
	if (value != NULL)
		scf_value_destroy(value);
	if (prop != NULL)
		scf_property_destroy(prop);
	if (pg != NULL)
		scf_pg_destroy(pg);
	if (iter != NULL)
		scf_iter_destroy(iter);
	if (scope != NULL)
		scf_scope_destroy(scope);
	if (inst != NULL)
		scf_instance_destroy(inst);
	if (service != NULL)
		scf_service_destroy(service);

	(void) scf_handle_unbind(handle);
	scf_handle_destroy(handle);
}

/*
 * How long to wait for service to come online
 */
#define	WAIT_FOR_SERVICE	15

/*
 * sa_smb_enable_service()
 *
 */
static int
sa_smb_enable_service(void)
{
	int i;
	int rc = SA_OK;
	char *service[] = { SMBD_DEFAULT_INSTANCE_FMRI, NULL };

	if (!sa_smb_isonline()) {
		/*
		 * Attempt to start the idmap, and other dependent
		 * services, first.  If it fails, the SMB service will
		 * ultimately fail so we use that as the error.  If we
		 * don't try to enable idmap, smb won't start the
		 * first time unless the admin has done it
		 * manually. The service could be administratively
		 * disabled so we won't always get started.
		 */
		sa_smb_enable_dependencies(SMBD_DEFAULT_INSTANCE_FMRI);
		_check_services(service);

		/* Wait for service to come online */
		for (i = 0; i < WAIT_FOR_SERVICE; i++) {
			if (sa_smb_isonline()) {
				rc =  SA_OK;
				break;
			} else if (sa_smb_ismaint()) {
				/* maintenance requires help */
				rc = SA_SYSTEM_ERR;
				break;
			} else if (sa_smb_isdisabled()) {
				/* disabled is ok */
				rc = SA_OK;
				break;
			} else {
				/* try another time */
				rc = SA_BUSY;
				(void) sleep(1);
			}
		}
	}
	return (rc);
}
#endif /* ENABLE_SERVICE */

static int
sa_smb_sprintf_option(char **bufpp, size_t *buf_len,
    size_t chunksize, char *propname, char *value, char *sep)
{
	char *bufp = *bufpp;
	char *buf_endp;
	size_t strsize;
	size_t curlen;
	size_t buf_rem;

	if (bufp == NULL)
		return (SA_INTERNAL_ERR);

	/*
	 * A future RFE would be to replace this with more
	 * generic code and to possibly handle more types.
	 */

	strsize = strlen(propname) + strlen(sep) + strlen(value);

	curlen = strlen(bufp);
	buf_rem = *buf_len - curlen;

	while (strsize >= buf_rem) {
		bufp = realloc(bufp, (*buf_len) + chunksize);
		if (bufp != NULL) {
			*bufpp = bufp;
			*buf_len += chunksize;
			buf_rem += chunksize;
		} else {
			*buf_len = 0;
			return (SA_NO_MEMORY);
		}
	}

	buf_endp = bufp + curlen;

	(void) snprintf(buf_endp, buf_rem, "%s%s=%s", sep,
	    propname, value);

	return (SA_OK);
}

static int
sa_smb_format_props(nvlist_t *props, char **retbuf)
{
	char *bufp;
	size_t buflen;
	char *propname;
	char *propvalue;
	char *sep = "";
	nvpair_t *prop;

	if ((bufp = calloc(CHUNKSIZE, sizeof (char))) == NULL)
		return (SA_NO_MEMORY);

	buflen = CHUNKSIZE;

	for (prop = nvlist_next_nvpair(props, NULL);
	    prop != NULL; prop = nvlist_next_nvpair(props, prop)) {
		if (nvpair_type(prop) != DATA_TYPE_STRING)
			continue;
		propname = nvpair_name(prop);
		(void) nvpair_value_string(prop, &propvalue);
		if (sa_smb_sprintf_option(&bufp, &buflen, CHUNKSIZE,
		    propname, propvalue, sep) == SA_OK)
			sep = ",";
		else
			return (SA_NO_MEMORY);
	}
	*retbuf = bufp;
	return (SA_OK);
}

static int
sa_smb_sharefs(sharefs_op_t op, nvlist_t *shrlist)
{
	char *buf = NULL;
	size_t buflen;
	int err, rc;

	if (nvlist_pack(shrlist, &buf, &buflen, NV_ENCODE_XDR, 0) != 0)
		return (SA_NO_MEMORY);

	err = sharefs(SHAREFS_SMB, op, buf, buflen);
	free(buf);

	if (err < 0) {
		switch (errno) {
		case EEXIST:
			rc = SA_DUPLICATE_NAME;
			break;
		case ENOENT:
			rc = SA_SHARE_NOT_FOUND;
			break;
		case EPERM:
			rc = SA_NO_PERMISSION;
			break;
		case ENOTSUP:
			rc = SA_INVALID_ZONE;
			break;
		default:
			rc = SA_INTERNAL_ERR;
			break;
		}
	} else {
		rc = SA_OK;
	}

	return (rc);
}

static int
sa_smb_share_publish(nvlist_t *share, int wait)
{
	NOTE(ARGUNUSED(wait))
	char *sh_name;
	char *sh_path;
	nvlist_t *sh_list = NULL;
	int rc = SA_OK;
#ifdef ENABLE_SERVICE
	priv_set_t *priv_effective;
	boolean_t privileged;
	boolean_t online;
#endif /* ENABLE_SERVICE */

	if ((sh_name = sa_share_get_name(share)) == NULL) {
		salog_error(SA_NO_SHARE_NAME,
		    "sa_smb_share_publish");
		return (SA_NO_SHARE_NAME);
	}

	if ((sh_path = sa_share_get_path(share)) == NULL) {
		salog_error(SA_NO_SHARE_PATH,
		    "sa_smb_share_publish");
		return (SA_NO_SHARE_PATH);
	}

	sa_tracef("smb_share_publish_start: %s:%s:%d",
	    sh_name, sh_path, wait);

#ifdef ENABLE_SERVICE
	priv_effective = priv_allocset();
	(void) getppriv(PRIV_EFFECTIVE, priv_effective);
	privileged = priv_isfullset(priv_effective);
	priv_freeset(priv_effective);

	/*
	 * If administratively disabled, don't try to start anything.
	 */
	online = sa_smb_isonline();
	if (!online && !sa_smb_isautoenable() && sa_smb_isdisabled()) {
		salog_debug(0, "SMB: %s:%s: cannot share: "
		    "SMB service is disabled",
		    sh_name, sh_path);
		goto out;
	}

	if (!privileged && !online) {
		rc = SA_NO_PERMISSION;
		salog_error(rc,
		    "SMB: %s:%s: cannot enable service",
		    sh_name, sh_path);
		goto out;
	}

	if (privileged && !online) {
		rc = sa_smb_enable_service();
		if (rc != SA_OK) {
			salog_debug(0, "SMB: %s:%s: "
			    "error enabling SMB service",
			    sh_name, sh_path, rc);
			if (rc == SA_BUSY || rc == SA_SYSTEM_ERR)
				rc = SA_OK;
		} else {
			/*
			 * wait for service to go online
			 * This is only temporary. Remove once
			 * changes to SMB are complete.
			 */
			int cnt = 0;
			while (!sa_smb_isonline() && cnt < 5) {
				(void) sleep(1);
				++cnt;
			}
			if (cnt < 5)
				online = B_TRUE;
		}
	}

	if (!online)
		goto out;
#endif /* ENABLE_SERVICE */

	/*
	 * SMB expects a list of shares, so allocate a list and add the share
	 */
	if (((sh_list = sa_share_alloc(NULL, NULL)) == NULL) ||
	    (nvlist_add_nvlist(sh_list, sh_name, share) != 0)) {
		rc = SA_NO_MEMORY;
		salog_error(rc, "sa_smb_share_publish");
		goto out;
	}

	if ((rc = sa_smb_sharefs(SHAREFS_PUBLISH, sh_list)) != SA_OK) {
		if (rc == SA_DUPLICATE_NAME) {
			/*
			 * do not report duplicate name as an error
			 */
			salog_debug(rc, "SMB: %s:%s: "
			    "publishing duplicate share name",
			    sh_name, sh_path);
			rc = SA_OK;
		} else {
			salog_error(rc, "SMB: %s:%s: "
			    "error publishing share",
			    sh_name, sh_path);
		}
	}

out:
	sa_share_free(sh_list);
	sa_tracef("smb_share_publish_stop: %s: %d: %s",
	    sh_name, rc, sa_strerror(rc));

	return (rc);
}

static int
sa_smb_share_unpublish(nvlist_t *share, int wait)
{
	nvlist_t *sh_list;
	char *sh_name;
	char *sh_path;
	int rc = SA_OK;

	if ((sh_name = sa_share_get_name(share)) == NULL) {
		salog_error(SA_NO_SHARE_NAME,
		    "sa_smb_share_unpublish");
		return (SA_NO_SHARE_NAME);
	}

	sh_path = sa_share_get_path(share);

	sa_tracef("smb_share_unpublish_start: %s:%s:%d",
	    sh_name, (sh_path != NULL ? sh_path : ""), wait);

	/*
	 * SMB expects a list of shares, so allocate a list and add the share
	 */
	if (((sh_list = sa_share_alloc(NULL, NULL)) == NULL) ||
	    (nvlist_add_nvlist(sh_list, sh_name, share) != 0)) {
		rc = SA_NO_MEMORY;
		salog_error(rc, "sa_smb_share_publish");
		goto out;
	}

	if ((rc = sa_smb_sharefs(SHAREFS_UNPUBLISH, sh_list)) != SA_OK) {
		if (rc == SA_SHARE_NOT_FOUND) {
			rc = SA_OK;
		} else {
			salog_error(rc, "SMB: %s:%s: "
			    "error unpublishing share",
			    sh_name, sh_path != NULL ? sh_path : "");
		}
	}
out:
	sa_share_free(sh_list);
	sa_tracef("smb_share_unpublish_stop: %s: %s",
	    sh_name, sa_strerror(rc));

	return (rc);
}

static int
sa_smb_share_unpublish_byname(const char *sh_name, const char *sh_path,
    int wait)
{
	NOTE(ARGUNUSED(wait))
	nvlist_t *share;
	int rc;

	if ((share = sa_share_alloc(sh_name, sh_path)) == NULL) {
		salog_error(SA_NO_MEMORY, "SMB: %s:%s: "
		    "error unpublishing share",
		    sh_name != NULL ? sh_name : "",
		    sh_path != NULL ? sh_path : "");
		return (SA_NO_MEMORY);
	}

	rc = sa_smb_share_unpublish(share, wait);
	sa_share_free(share);
	return (rc);
}

static int
sa_smb_share_publish_admin(const char *mntpnt)
{
	uint32_t status;

	status = smb_share_publish_admin(mntpnt);
	if (status != 0)
		return (SA_SYSTEM_ERR);
	else
		return (SA_OK);
}

static int
sa_smb_fs_publish(nvlist_t *sh_list, int wait)
{
	NOTE(ARGUNUSED(wait))
	int rc = SA_OK;

	sa_tracef("smb_fs_publish_start: %d", wait);

	if ((rc = sa_smb_sharefs(SHAREFS_PUBLISH, sh_list)) != SA_OK) {
		if (rc == SA_DUPLICATE_NAME) {
			/*
			 * do not report duplicate name as an error
			 */
			rc = SA_OK;
		} else {
			salog_error(rc, "SMB: error publishing shares");
		}
	}

	(void) nvlist_free(sh_list);
	sa_tracef("smb_fs_publish_stop: %s", sa_strerror(rc));

	return (rc);
}

static int
sa_smb_fs_unpublish(nvlist_t *sh_list, int wait)
{
	NOTE(ARGUNUSED(wait))
	int rc = SA_OK;

	sa_tracef("smb_fs_unpublish_start: %d", wait);

	if ((rc = sa_smb_sharefs(SHAREFS_UNPUBLISH, sh_list)) != SA_OK)
		salog_error(rc, "SMB: error unpublishing shares");

	(void) nvlist_free(sh_list);
	sa_tracef("smb_fs_unpublish_stop: %s", sa_strerror(rc));

	return (rc);
}

/*
 * Functions that follow are used by sharectl
 */

static void
sa_smb_free_proto_proplist(void)
{
	if (smb_proto_proplist != NULL) {
		nvlist_free(smb_proto_proplist);
		smb_proto_proplist = NULL;
	}
}

static int
sa_smb_init_proto_proplist(void)
{
	int rc;
	int index;
	char *name;
	char value[MAX_VALUE_BUFLEN];

	sa_smb_free_proto_proplist();

	if (nvlist_alloc(&smb_proto_proplist, NV_UNIQUE_NAME, 0) != 0)
		return (SA_NO_MEMORY);

	for (index = 0; index < SMB_PROTO_OPTNUM; index++) {
		rc = smb_config_get(smb_proto_options[index],
		    value, sizeof (value));
		if (rc != SMBD_SMF_OK)
			continue;
		name = smb_config_getname(smb_proto_options[index]);
		if (nvlist_add_string(smb_proto_proplist, name, value) != 0) {
			nvlist_free(smb_proto_proplist);
			smb_proto_proplist = NULL;
			return (SA_NO_MEMORY);
		}
	}

	return (SA_OK);
}

/*
 * sa_smb_get_features
 *
 * return supported features
 */
static int
sa_smb_proto_get_features(uint64_t *features)
{
	*features = (SA_FEATURE_RESOURCE | SA_FEATURE_ALLOWSUBDIRS |
	    SA_FEATURE_ALLOWPARDIRS | SA_FEATURE_SERVER |
	    SA_FEATURE_MULT_RESOURCES);
	return (SA_OK);
}

static int
sa_smb_proto_get_proplist(nvlist_t **proplist)
{
	if (smb_proto_proplist == NULL) {
		if (sa_smb_init_proto_proplist() != SA_OK) {
			*proplist = NULL;
			return (SA_NO_MEMORY);
		}
	}

	if (nvlist_dup(smb_proto_proplist, proplist, 0) == 0)
		return (SA_OK);
	else
		return (SA_NO_MEMORY);
}

/*
 * sa_smb_proto_get_status()
 *
 * What is the current status of the smbd? We use the SMF state here.
 * Caller must free the returned value.
 */
static int
sa_smb_proto_get_status(char **status_str)
{
	*status_str = smf_get_state(SMBD_DEFAULT_INSTANCE_FMRI);
	if (*status_str != NULL)
		return (SA_OK);
	else
		return (SA_NO_MEMORY);
}

static int
sa_smb_proto_get_property(const char *sectname, const char *propname,
    char **propval)
{
	NOTE(ARGUNUSED(sectname))
	char *val;

	if (smb_proto_proplist == NULL) {
		if (sa_smb_init_proto_proplist() != SA_OK) {
			*propval = NULL;
			return (SA_NO_MEMORY);
		}
	}

	if (nvlist_lookup_string(smb_proto_proplist, propname, &val) == 0) {
		if ((*propval = strdup(val)) != NULL)
			return (SA_OK);
		else
			return (SA_NO_MEMORY);
	} else {
		return (SA_NO_SUCH_PROP);
	}
}

/*
 * Saves the specified protocol property with the given value to SMF
 */
static int
sa_smb_proto_set_property(const char *sectname, const char *propname,
    const char *propval)
{
	NOTE(ARGUNUSED(sectname))
	int rc;

	rc = smb_config_set(propname, propval);

	switch (rc) {
	case SMBD_SMF_OK:
		(void) sa_smb_init_proto_proplist();
		rc = SA_OK;
		break;
	case SMBD_SMF_INVALID_VALUE:
		rc = SA_INVALID_PROP_VAL;
		break;
	case SMBD_SMF_INVALID_ARG:
		rc = SA_INVALID_PROP;
		break;
	default:
		rc = SA_INTERNAL_ERR;
		break;
	}

	return (rc);
}
