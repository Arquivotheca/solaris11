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
 * NFS specific functions
 */

#include <strings.h>
#include <ctype.h>
#include <limits.h>
#include <pwd.h>
#include <libnvpair.h>
#include <unistd.h>
#include <locale.h>
#include <errno.h>
#include <zone.h>
#include <signal.h>
#include <libscf.h>
#include <note.h>
#include <sys/sysmacros.h>
#include <rpcsvc/daemon_utils.h>
#include <sys/mnttab.h>
#include <sys/mntent.h>

#include "nfslog_config.h"
#include "nfslogtab.h"
#include <nfs/export.h>
#include <nfs/nfs.h>
#include <nfs/nfssys.h>

#include <sharefs/share.h>
#include <sharefs/sharetab.h>

#include <libshare.h>
#include <libshare_impl.h>

#include "smfcfg.h"
#include "libshare_nfs.h"

static int debug = 0;
static int nfs_svc_delta = 2;	/* seconds between attempts to start svc */

#define	NFS_CLIENT_SVC	(char *)"svc:/network/nfs/client:default"

/* should really be in some global place */
#define	DEF_WIN		30000
#define	CHUNKSIZE	256

/* used to sort out autofs and lofs */
#define	deveq(dev1, dev2)	((dev1) == (dev2))

static int sa_nfs_add_property(nvlist_t *, char *, char *,
    char *, size_t);
static int sa_nfs_parse_val(char **, char *, int, char *, size_t);
static int sa_nfs_init_sec_list(nvlist_t **, nvlist_t *, nvlist_t **,
char **, char *, char **, char *, size_t);
static int sa_nfs_finalize_sec_list(nvlist_t *, nvlist_t *, char *,
    char *, char *, size_t);
static int sa_nfs_dup_sec_list(nvlist_t *, nvlist_t *, char *,
    char *, char *, size_t);
static int sa_nfs_cleanup_sec_props(nvlist_t *, char *, size_t, int);
static int sa_nfs_set_default_sec_prop(nvlist_t *, char *, size_t);
static void sanfs_free_proto_proplist(void);
static int find_gbl_prop(const char *);
static int find_sec_prop(const char *);
static int find_gbl_prop_type(const char *);
static int find_sec_prop_type(const char *);
static void nfs_check_services(boolean_t);
static int nfslogtab_add(char *, char *, char *);
static int nfslogtab_remove(const char *);


/*
 * plugin ops routines
 */
static int sa_nfs_init(void);
static void sa_nfs_fini(void);
static int sa_nfs_share_parse(const char *, int, nvlist_t **, char *, size_t);
static int sa_nfs_share_merge(nvlist_t *, nvlist_t *, int, char *, size_t);
static int sa_nfs_share_set_def_proto(nvlist_t *);
static int sa_nfs_share_validate_name(const char *, boolean_t, char *, size_t);
static int sa_nfs_share_validate(nvlist_t *, boolean_t, char *, size_t);
static int sa_nfs_share_publish(nvlist_t *, int);
static int sa_nfs_share_unpublish(nvlist_t *, int);
static int sa_nfs_share_unpublish_byname(const char *, const char *, int);
static int sa_nfs_share_publish_admin(const char *);
static int sa_nfs_fs_publish(nvlist_t *, int);
static int sa_nfs_fs_unpublish(nvlist_t *, int);
static int sa_nfs_format_props(nvlist_t *, char **);
/* protocol property routines */
static int sa_nfs_proto_get_features(uint64_t *);
static int sa_nfs_proto_get_proplist(nvlist_t **);
static int sa_nfs_proto_get_status(char **);
static int sa_nfs_proto_get_property(const char *, const char *, char **);
static int sa_nfs_proto_set_property(const char *, const char *, const char *);
static char *nfs_space_alias(char *);

sa_proto_ops_t sa_plugin_ops = {
	.sap_hdr = {
		.pi_ptype = SA_PLUGIN_PROTO,
		.pi_type = SA_PROT_NFS,
		.pi_name = "nfs",
		.pi_version = SA_LIBSHARE_VERSION,
		.pi_flags = 0,
		.pi_init = sa_nfs_init,
		.pi_fini = sa_nfs_fini
	},
	.sap_share_parse = sa_nfs_share_parse,
	.sap_share_merge = sa_nfs_share_merge,
	.sap_share_set_def_proto = sa_nfs_share_set_def_proto,
	.sap_share_validate_name = sa_nfs_share_validate_name,
	.sap_share_validate = sa_nfs_share_validate,
	.sap_share_publish = sa_nfs_share_publish,
	.sap_share_unpublish = sa_nfs_share_unpublish,
	.sap_share_unpublish_byname = sa_nfs_share_unpublish_byname,
	.sap_share_publish_admin = sa_nfs_share_publish_admin,
	.sap_fs_publish = sa_nfs_fs_publish,
	.sap_fs_unpublish = sa_nfs_fs_unpublish,
	.sap_share_prop_format = sa_nfs_format_props,

	.sap_proto_get_features = sa_nfs_proto_get_features,
	.sap_proto_get_proplist = sa_nfs_proto_get_proplist,
	.sap_proto_get_status = sa_nfs_proto_get_status,
	.sap_proto_get_property = sa_nfs_proto_get_property,
	.sap_proto_set_property = sa_nfs_proto_set_property,
	.sap_proto_rem_section = NULL
};

/*
 * option definitions.  Make sure to keep the #define for the option
 * index just before the entry it is the index for. Changing the order
 * can cause breakage.  E.g OPT_RW is index 1 and must precede the
 * line that includes the SHOPT_RW and OPT_RW entries.
 */

struct option_defs gbl_prop_defs[] = {
#define	OPT_ANON	0
	{SHOPT_ANON, OPT_ANON, OPT_TYPE_USER},
#define	OPT_NOSUID	1
	{SHOPT_NOSUID, OPT_NOSUID, OPT_TYPE_BOOLEAN},
#define	OPT_ACLOK	2
	{SHOPT_ACLOK, OPT_ACLOK, OPT_TYPE_BOOLEAN},
#define	OPT_NOSUB	3
	{SHOPT_NOSUB, OPT_NOSUB, OPT_TYPE_BOOLEAN},
#define	OPT_PUBLIC	4
	{SHOPT_PUBLIC, OPT_PUBLIC, OPT_TYPE_BOOLEAN, OPT_SHARE_ONLY},
#define	OPT_INDEX	5
	{SHOPT_INDEX, OPT_INDEX, OPT_TYPE_FILE},
#define	OPT_LOG		6
	{SHOPT_LOG, OPT_LOG, OPT_TYPE_LOGTAG},
#define	OPT_CKSUM	7
	{SHOPT_CKSUM, OPT_CKSUM, OPT_TYPE_STRINGSET},
#define	OPT_SEC		8
	{"", OPT_SEC, OPT_TYPE_SECURITY},
#define	OPT_CHARSET_MAP	9
	{"", OPT_CHARSET_MAP, OPT_TYPE_ACCLIST},
#define	OPT_NOACLFAB	10
	{SHOPT_NOACLFAB, OPT_NOACLFAB, OPT_TYPE_BOOLEAN},
#ifdef VOLATILE_FH_TEST	/* added for testing volatile fh's only */
#define	OPT_VOLFH	11
	{SHOPT_VOLFH, OPT_VOLFH, OPT_TYPE_STRINGSET},
#endif /* VOLATILE_FH_TEST */
	NULL
};

struct option_defs sec_prop_defs[] = {
#define	OPT_RO		0
	{SHOPT_RO, OPT_RO, OPT_TYPE_ACCLIST},
#define	OPT_RW		1
	{SHOPT_RW, OPT_RW, OPT_TYPE_ACCLIST},
#define	OPT_ROOT	2
	{SHOPT_ROOT, OPT_ROOT, OPT_TYPE_ACCLIST},
#define	OPT_WINDOW	3
	{SHOPT_WINDOW, OPT_WINDOW, OPT_TYPE_NUMBER},
#define	OPT_NONE	4
	{SHOPT_NONE, OPT_NONE, OPT_TYPE_ACCLIST},
#define	OPT_ROOT_MAPPING	5
	{SHOPT_ROOT_MAPPING, OPT_ROOT_MAPPING, OPT_TYPE_USER},
	NULL
};

/*
 * List of security flavors.
 * etc/nfssec.conf
 */
static char *sec_flavors[] = {
	"none",
	"sys",
	"dh",
	"krb5",
	"krb5i",
	"krb5p",
	NULL
};

/*
 * Codesets that may need to be converted to UTF-8 for file paths.
 * Add new names here to add new property support. If we ever get a
 * way to query the kernel for character sets, this should become
 * dynamically loaded. Make sure changes here are reflected in
 * cmd/fs.d/nfs/mountd/nfscmd.c
 */

static char *legal_charset[] = {
	"euc-cn",
	"euc-jp",
	"euc-jpms",
	"euc-kr",
	"euc-tw",
	"iso8859-1",
	"iso8859-2",
	"iso8859-5",
	"iso8859-6",
	"iso8859-7",
	"iso8859-8",
	"iso8859-9",
	"iso8859-13",
	"iso8859-15",
	"koi8-r",
	NULL
};

/*
 * list of support services needed
 * defines should come from head/rpcsvc/daemon_utils.h
 */

static char *nfs_service_list_default[] =
	{ STATD, LOCKD, MOUNTD, NFSD, NFSMAPID, RQUOTAD, REPARSED, NULL };
static char *nfs_service_list_logging[] =
	{ STATD, LOCKD, MOUNTD, NFSD, NFSMAPID, RQUOTAD, NFSLOGD, REPARSED,
	    NULL };
static mutex_t nfs_services;

#define	LOCK_NFS_SERVICES()	(void) mutex_lock(&nfs_services)
#define	UNLOCK_NFS_SERVICES()	(void) mutex_unlock(&nfs_services)

/*
 * the proplist holds the defined options so we don't have to read
 * them multiple times
 */
static nvlist_t *nfs_proto_proplist = NULL;

static int
sa_nfs_init(void)
{
	return (SA_OK);
}

static void
sa_nfs_fini(void)
{
	sanfs_free_proto_proplist();
}

static int
sa_nfs_share_parse(const char *prot_props, int unset, nvlist_t **prot_nvl,
    char *errbuf, size_t buflen)
{
	char *strprop = NULL;
	char *namep;
	char *valp;
	char *nextp;
	char *sec_name = NULL;
	char *modep = NULL;
	nvlist_t *nvl = NULL;
	nvlist_t *sec_nvl = NULL;
	nvlist_t *cur_nvl;
	int ret;

	if (prot_nvl == NULL) {
		(void) snprintf(errbuf, buflen,
		    dgettext(TEXT_DOMAIN, "Internal error: prot_nvl==NULL"));
		return (SA_INTERNAL_ERR);
	}
	/*
	 * allocate a new nvlist for nfs properties
	 */
	if (nvlist_alloc(&nvl, NV_UNIQUE_NAME, 0) != 0) {
		ret = SA_NO_MEMORY;
		(void) snprintf(errbuf, buflen,
		    dgettext(TEXT_DOMAIN, "%s"), sa_strerror(ret));
		goto err_out;
	}

	/*
	 * return prot_nvl with no properties.
	 */
	if (prot_props == NULL) {
		*prot_nvl = nvl;
		return (SA_OK);
	}

	strprop = strdup(prot_props);
	if (strprop == NULL) {
		ret = SA_NO_MEMORY;
		(void) snprintf(errbuf, buflen,
		    dgettext(TEXT_DOMAIN, "%s"), sa_strerror(ret));
		goto err_out;
	}

	cur_nvl = nvl;
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

		ret = sa_nfs_parse_val(&valp, namep, unset, errbuf, buflen);
		if (ret != SA_OK)
			goto err_out;

		if (strcasecmp(namep, "sec") == 0) {
			ret = sa_nfs_init_sec_list(&cur_nvl, nvl, &sec_nvl,
			    &sec_name, valp, &modep, errbuf, buflen);
		} else {
			/*
			 * If this is not a valid security or global
			 * property, then return with invalid property
			 * error.
			 */
			if (find_gbl_prop(namep) != -1) {
				ret = sa_nfs_add_property(nvl, namep, valp,
				    errbuf, buflen);
			} else if (find_sec_prop(namep) != -1) {
				/*
				 * this is a security property,
				 * if we have seen a sec= property then
				 * add it to the sec_nvl, otherwise
				 * add it to the global list. It will be
				 * moved to the default security list
				 * in sa_nfs_cleanup_sec_props later.
				 */
				if (sec_nvl != NULL) {
					ret = sa_nfs_add_property(sec_nvl,
					    namep, valp, errbuf, buflen);
				} else {
					ret = sa_nfs_add_property(nvl,
					    namep, valp, errbuf, buflen);
				}
			} else {
				ret = SA_INVALID_NFS_PROP;
				(void) snprintf(errbuf, buflen,
				    dgettext(TEXT_DOMAIN, "%s: %s"),
				    sa_strerror(ret), namep);
				goto err_out;
			}
		}
		if (ret != SA_OK)
			goto err_out;
	}

	/*
	 * done parsing protocol properties,
	 * attach any outstanding security nvlists to prot_nvl
	 */
	if (cur_nvl == sec_nvl) {
		ret = sa_nfs_finalize_sec_list(nvl, sec_nvl, sec_name,
		    modep, errbuf, buflen);
		sec_nvl = NULL;
		if (ret != SA_OK)
			goto err_out;
	}

	ret = sa_nfs_cleanup_sec_props(nvl, errbuf, buflen, unset);
	if (ret != SA_OK)
		goto err_out;

	free(strprop);
	*prot_nvl = nvl;
	return (SA_OK);

err_out:
	if (sec_nvl != NULL)
		nvlist_free(sec_nvl);
	if (nvl != NULL)
		nvlist_free(nvl);
	if (strprop != NULL)
		free(strprop);
	*prot_nvl = NULL;

	return (ret);
}

static int
sa_nfs_add_property(nvlist_t *nvl, char *namep, char *valp,
    char *errbuf, size_t buflen)
{
	int ptype;
	char *propval;
	char *newval = NULL;
	int ret;

	propval = sa_share_get_prop(nvl, namep);
	if (propval != NULL) {
		ptype = find_gbl_prop_type(namep);
		if (ptype == -1)
			ptype = find_sec_prop_type(namep);
		if (ptype == OPT_TYPE_ACCLIST) {
			/*
			 * append access list value for this property
			 */
			(void) asprintf(&newval, "%s:%s", propval, valp);
			valp = newval;
		}
		/*
		 * all other property types will
		 * use the last value defined.
		 */
	}

	/*
	 * save property to list
	 */
	ret = sa_share_set_prop(nvl, namep, valp);
	if (ret != SA_OK) {
		(void) snprintf(errbuf, buflen,
		    dgettext(TEXT_DOMAIN, "%s"),
		    sa_strerror(ret));
	}

	if (newval != NULL)
		free(newval);

	return (ret);
}

/*
 * sa_nfs_parse_val
 *
 */
static int
sa_nfs_parse_val(char **valpp, char *namep, int unset,
    char *errbuf, size_t buflen)
{
	char *valp;
	int ptype;
	int ret = SA_OK;

	valp = strchr(namep, '=');
	if (valp == NULL) {
		/* no equal sign found */
		if (unset) {
			/* not needed for unset */
			*valpp = "";
		} else {
			/* property with no value, set to default */
			ptype = find_gbl_prop_type(namep);
			if (ptype == -1)
				ptype = find_sec_prop_type(namep);

			switch (ptype) {
			case OPT_TYPE_BOOLEAN:
				*valpp = "true";
				break;
			case OPT_TYPE_ACCLIST:
				*valpp = "*";
				break;
			case OPT_TYPE_LOGTAG:
				*valpp = "global";
				break;
			default:
				ret = SA_SYNTAX_ERR;
				(void) snprintf(errbuf, buflen,
				    dgettext(TEXT_DOMAIN,
				    "syntax error: no value for "
				    "'%s' property"), namep);
				break;
			}
		}
	} else if (valp[1] == '\0') {
		/* equal sign found but no value specified */
		if (unset) {
			*valp = '\0';
			*valpp = "";
		} else {
			ret = SA_SYNTAX_ERR;
			(void) snprintf(errbuf, buflen,
			    dgettext(TEXT_DOMAIN,
			    "syntax error: no value for '%s' property"),
			    namep);
		}
	} else {
		*valp++ = '\0';
		*valpp = valp;
	}

	return (ret);
}

static int
sa_nfs_init_sec_list(nvlist_t **cur_nvlp, nvlist_t *prot_nvl,
    nvlist_t **sec_nvlp, char **sec_namep, char *valp, char **modepp,
    char *errbuf, size_t buflen)
{
	char *modep;
	char *sec_name;
	int ret;

	if (*cur_nvlp == *sec_nvlp) {
		ret = sa_nfs_finalize_sec_list(prot_nvl, *sec_nvlp, *sec_namep,
		    *modepp, errbuf, buflen);
		*sec_nvlp = NULL;
		if (ret != SA_OK)
			return (ret);
	}

	/*
	 * is this a multi-mode security property?
	 * ie sec=sys:dh,ro=*,rw=grp1
	 */
	modep = strchr(valp, ':');
	if (modep != NULL) {
		/*
		 * yes, null terminate first mode
		 * and setup for next mode.
		 */
		*modep++ = '\0';
		*modepp = modep;
	} else {
		*modepp = NULL;
	}

	sec_name = strdup(valp);

	if (sec_name == NULL) {
		ret = SA_NO_MEMORY;
		(void) snprintf(errbuf, buflen, "%s", sa_strerror(ret));
		return (ret);
	}

	/*
	 * is this a valid security mode
	 */
	if (strcmp(valp, "default") == 0) {
		char *defval;
		/*
		 * Special case of "default" where we just want to
		 * know that there is a defined default. We don't want
		 * to convert the default name to the current default.
		 */
		defval = nfs_space_alias(valp);
		if (defval == NULL) {
			ret = SA_NO_MEMORY;
			(void) snprintf(errbuf, buflen, "%s", sa_strerror(ret));
			free(sec_name);
			return (ret);
		}
		if (strcmp(defval, "default") == 0) {
			ret = SA_INVALID_SECURITY;
			(void) snprintf(errbuf, buflen,
			    dgettext(TEXT_DOMAIN,
			    "%s: value not defined for sec=default"),
			    sa_strerror(ret));
			free(defval);
			free(sec_name);
			return (ret);
		}

		free(defval);
	} else if (sa_prop_cmp_list(sec_name, sec_flavors) < 0) {
		ret = SA_INVALID_SECURITY;
		(void) snprintf(errbuf, buflen, "%s: %s",
		    sa_strerror(ret), sec_name);
		free(sec_name);
		return (ret);
	}

	/*
	 * allocate a new security nvlist
	 */
	if (nvlist_alloc(sec_nvlp, NV_UNIQUE_NAME, 0) != 0) {
		ret = SA_NO_MEMORY;
		(void) snprintf(errbuf, buflen, "%s", sa_strerror(ret));
		free(sec_name);
		return (ret);
	}

	*sec_namep = sec_name;
	*cur_nvlp = *sec_nvlp;

	return (SA_OK);
}

/*
 * sa_nfs_finalize_sec_list
 *
 * sec_nvl is added to prot_nvl
 * For multi-mode, sa_nfs_dup_sec_list is called to add
 * security property lists for all modes specified in modep
 *
 * cur_nvl is deallocated.
 */
static int
sa_nfs_finalize_sec_list(nvlist_t *prot_nvl, nvlist_t *sec_nvl, char *sec_name,
    char *modep, char *errbuf, size_t buflen)
{
	int ret = SA_OK;

	/*
	 * Are we working on a multi-mode security?
	 */
	if (modep != NULL) {
		/*
		 * save cur sec_nvl and add security
		 * lists for all modes specified in
		 * security property
		 */
		ret = sa_nfs_dup_sec_list(prot_nvl, sec_nvl,
		    sec_name, modep, errbuf, buflen);
	} else {
		/*
		 * add the current sec_nvl
		 */
		if (nvlist_add_nvlist(prot_nvl, sec_name,
		    sec_nvl) != 0) {
			ret = SA_NO_MEMORY;
			(void) snprintf(errbuf, buflen, "%s", sa_strerror(ret));
		}
	}
	nvlist_free(sec_nvl);
	free(sec_name);

	return (ret);
}

/*
 * sa_nfs_dup_sec_list
 *
 * This routine handles the case when multiple security modes
 * are specified with a single sec= property (ie sec=krb5:krb5i,ro=*,rw=grp1)
 *
 * INPUTS:
 *    prot_nvl:  protocol property list
 *    sec_nvl:   security property list for first mode
 *    sec_name:  security mode of sec_nvl
 *    modep:     points to next mode in the string
 *    buflen:    size of errbuf
 *
 * OUTPUTS:
 *    errbuf:    on error, set to error string
 *
 * NOTES:
 *    The security properties have already been parsed and added to sec_nvl.
 *    modep points to the second security mode in the list
 *    sec_nvl will added to prot_nvl, and a copy will be made and added for
 *    each security mode specified in modep.
 *    sec_nvl will not be deallocated by this routine.
 */
static int
sa_nfs_dup_sec_list(nvlist_t *prot_nvl, nvlist_t *sec_nvl, char *sec_name,
    char *modep, char *errbuf, size_t buflen)
{
	char *valp;
	nvlist_t *new_sec_nvl;
	int rc;

	/*
	 * add the current sec_nvl
	 */
	if (nvlist_add_nvlist(prot_nvl, sec_name, sec_nvl) != 0) {
		rc = SA_NO_MEMORY;
		(void) snprintf(errbuf, buflen,
		    dgettext(TEXT_DOMAIN,
		    "%s"), sa_strerror(rc));
		return (rc);
	}

	do {
		valp = modep;
		/*
		 * search for the next security mode
		 * and null terminate the mode name
		 */
		if ((modep = strchr(modep, ':')) != NULL) {
			*modep = '\0';
			modep++;
		}

		if (*valp != '\0') {
			/*
			 * is this a valid security mode?
			 */
			if (strcmp(valp, "default") == 0) {
				/*
				 * Special case of "default" converts
				 * to defined default value. We will
				 * be keeping the default name but do
				 * need to validate that it would map
				 * to something.
				 */
				sec_name = nfs_space_alias(valp);
				if (sec_name == NULL) {
					rc = SA_NO_MEMORY;
					(void) snprintf(errbuf, buflen,
					    "%s: %s", sa_strerror(rc),
					    valp);
					return (rc);
				}
				if (strcmp(sec_name, "default") == 0) {
					rc = SA_INVALID_SECURITY;
					(void) snprintf(errbuf, buflen,
					    dgettext(TEXT_DOMAIN,
					    "%s: value not defined for "
					    "sec=default"), sa_strerror(rc));
					free(sec_name);
					return (rc);
				}
				free(sec_name);
			} else  if (sa_prop_cmp_list(valp, sec_flavors) < 0) {
				rc = SA_INVALID_SECURITY;
				(void) snprintf(errbuf, buflen,
				    "%s: %s", sa_strerror(rc), valp);
				return (rc);
			}

			sec_name = strdup(valp);

			if (sec_name == NULL) {
				rc = SA_NO_MEMORY;
				(void) snprintf(errbuf, buflen,
				    "%s", sa_strerror(rc));
				return (rc);
			}

			/*
			 * dup the original security property list
			 */
			if (nvlist_dup(sec_nvl, &new_sec_nvl, 0) != 0) {
				rc = SA_NO_MEMORY;
				(void) snprintf(errbuf, buflen,
				    dgettext(TEXT_DOMAIN,
				    "%s"), sa_strerror(rc));
				free(sec_name);
				return (rc);
			}

			/*
			 * and add it to the protocol property list
			 * using the next mode name in the list
			 */
			if (nvlist_add_nvlist(prot_nvl, sec_name,
			    new_sec_nvl) != 0) {
				rc = SA_NO_MEMORY;
				(void) snprintf(errbuf, buflen,
				"%s", sa_strerror(rc));
				nvlist_free(new_sec_nvl);
				free(sec_name);
				return (rc);
			}
			nvlist_free(new_sec_nvl);
			free(sec_name);
		}
	} while (modep != NULL);

	return (SA_OK);
}

/*
 * This routine will check for any security properties outside of a
 * security nvlist. If found, move the properties to a new security list.
 * When done, merge sec=sys property list (if exists) into new security list
 * and then save it to the protocol nvlist as sec=sys.
 * Also add rw=* to all empty security lists.
 */
static int
sa_nfs_cleanup_sec_props(nvlist_t *prot_nvl, char *errbuf, size_t buflen,
    int unset)
{
	nvlist_t *sec_nvl = NULL;
	nvlist_t *new_nvl = NULL;
	nvpair_t *curr_nvp;
	nvpair_t *next_nvp;
	char *propname;
	char *propval;
	boolean_t sec_sys_exists = B_FALSE;
	int ret = SA_OK;

	if (nvlist_lookup_nvlist(prot_nvl, "sys", &sec_nvl) == 0)
		sec_sys_exists = B_TRUE;

	curr_nvp = nvlist_next_nvpair(prot_nvl, NULL);
	while (curr_nvp != NULL) {
		next_nvp = nvlist_next_nvpair(prot_nvl, curr_nvp);

		/* skip security nvlists */
		if (nvpair_type(curr_nvp) == DATA_TYPE_NVLIST) {
			curr_nvp = next_nvp;
			continue;
		}

		propname = nvpair_name(curr_nvp);

		/* skip non security properties */
		if (find_sec_prop(propname) == -1) {
			curr_nvp = next_nvp;
			continue;
		}

		/*
		 * found a security property
		 * if sys security list already exists, then
		 * add to the list, otherwise create one first
		 */
		if (new_nvl == NULL) {
			if (nvlist_alloc(&new_nvl, NV_UNIQUE_NAME, 0) != 0) {
				ret = SA_NO_MEMORY;
				(void) snprintf(errbuf, buflen,
				    dgettext(TEXT_DOMAIN, "%s"),
				    sa_strerror(ret));
				return (ret);
			}
		}

		/* Get value from nvpair */
		if (nvpair_value_string(curr_nvp, &propval) != 0) {
			ret = SA_INVALID_PROP_VAL;
			(void) snprintf(errbuf, buflen,
			    dgettext(TEXT_DOMAIN, "%s: %s"),
			    sa_strerror(ret), propname);
			nvlist_free(new_nvl);
			return (ret);
		}

		/* add to security property list */
		ret = sa_share_set_prop(new_nvl, propname, propval);
		if (ret != SA_OK) {
			(void) snprintf(errbuf, buflen,
			    dgettext(TEXT_DOMAIN, "%s"),
			    sa_strerror(ret));
			nvlist_free(new_nvl);
			return (ret);
		}

		/* remove from protocol property list */
		if (nvlist_remove(prot_nvl, propname,
		    nvpair_type(curr_nvp)) != 0) {
			ret = SA_SYNTAX_ERR;
			(void) snprintf(errbuf, buflen,
			    dgettext(TEXT_DOMAIN,
			    "%s: must follow sec property: %s"),
			    sa_strerror(ret), propname);
			nvlist_free(new_nvl);
			return (ret);
		}
		curr_nvp = next_nvp;
	}

	if (new_nvl != NULL) {
		if (sec_sys_exists) {
			/*
			 * move security properties from existing sec=sys
			 * security nvlist to the new_nvl. This will preserve
			 * the order of the properties.
			 */
			if (nvlist_merge(new_nvl, sec_nvl, 0) != 0) {
				ret = SA_NO_MEMORY;
				(void) snprintf(errbuf, buflen,
				    dgettext(TEXT_DOMAIN, "%s"),
				    sa_strerror(ret));
				nvlist_free(new_nvl);
				return (ret);
			}
		}

		/*
		 * save the new security nvlist to the prot_nvl
		 * This will replace any existing sec=sys nvlist
		 */
		if (nvlist_add_nvlist(prot_nvl, "sys", new_nvl) != 0) {
			ret = SA_NO_MEMORY;
			(void) snprintf(errbuf, buflen,
			    dgettext(TEXT_DOMAIN, "%s"),
			    sa_strerror(ret));
			nvlist_free(new_nvl);
			return (ret);
		}
		nvlist_free(new_nvl);
	}

	/*
	 * If this is a unset request then leave the property
	 * lists the way they are. For example, an empty security
	 * list is a request to remove all properties for the specified
	 * security mode. So we only want to add default properties
	 * for update requests.
	 */
	if (unset)
		return (SA_OK);

	/* make sure each security list has at least one property */
	return (sa_nfs_set_default_sec_prop(prot_nvl, errbuf, buflen));
}

/*
 * Go through all security lists and make sure each
 * has at least one property. If not then add
 * default property (rw=*)
 */
static int
sa_nfs_set_default_sec_prop(nvlist_t *prot_nvl, char *errbuf, size_t buflen)
{
	nvpair_t *curr_nvp;
	nvlist_t *sec_nvl;
	int ret;

	for (curr_nvp = nvlist_next_nvpair(prot_nvl, NULL);
	    curr_nvp != NULL;
	    curr_nvp = nvlist_next_nvpair(prot_nvl, curr_nvp)) {
		/* skip non-security properties */
		if (nvpair_type(curr_nvp) != DATA_TYPE_NVLIST)
			continue;

		if (nvpair_value_nvlist(curr_nvp, &sec_nvl) != 0) {
			ret = SA_INVALID_SECURITY;
			(void) snprintf(errbuf, buflen,
			    dgettext(TEXT_DOMAIN, "%s"),
			    sa_strerror(ret));
			return (ret);
		}

		/*
		 * make sure we have at least one security property. If
		 * not, then add an "rw".
		 */
		if (nvlist_next_nvpair(sec_nvl, NULL) == NULL) {
			ret = sa_share_set_prop(sec_nvl, SHOPT_RW, "*");
			if (ret != SA_OK) {
				(void) snprintf(errbuf, buflen, "%s",
				    sa_strerror(ret));
				return (ret);
			}
		}
	}

	return (SA_OK);
}

/*
 * nfs_merge_security
 *
 * INPUTS:
 *     dst_prot_nvl: the destination protocol property list.
 *     src_sec_nvl:  the new security property list to be merged
 *                   into dst_prot_nvl
 *     sec_mode_name: security mode name of src_sec_nvl
 *     unset       : if set, remove sec mode from dst_prot_nvl
 *
 * OUTPUTS:
 *     dst_pro_nvl:  the destination protocol property list is
 *                   updated with new security mode properties,
 *                   or if 'unset', the security mode properties
 *                   are removed from the property list.
 *     errbuf:       if there are any errors, the error message
 *                   is copied to this buffer.
 */
static int
nfs_merge_security(nvlist_t *dst_prot_nvl, nvlist_t *src_sec_nvl,
    const char *sec_mode_name, int unset, char *errbuf, size_t buflen)
{
	nvlist_t *dst_sec_nvl;
	int rc;

	if (unset) {
		/*
		 * If there are no specific security properties
		 * specified, then remove the security nvlist
		 * otherwise remove just the properties given.
		 */
		if (sa_prop_empty_list(src_sec_nvl)) {
			if (nvlist_remove(dst_prot_nvl, sec_mode_name,
			    DATA_TYPE_NVLIST) != 0) {
				rc = SA_NO_SUCH_PROP;
				(void) snprintf(errbuf, buflen,
				    dgettext(TEXT_DOMAIN, "%s: sec=%s"),
				    sa_strerror(rc), sec_mode_name);
				return (rc);
			}
		} else {
			nvpair_t *snvp;

			if (nvlist_lookup_nvlist(dst_prot_nvl, sec_mode_name,
			    &dst_sec_nvl) != 0) {
				rc = SA_NO_SUCH_PROP;
				(void) snprintf(errbuf, buflen,
				    dgettext(TEXT_DOMAIN, "%s: sec=%s"),
				    sa_strerror(rc), sec_mode_name);
				return (rc);
			}

			for (snvp = nvlist_next_nvpair(src_sec_nvl, NULL);
			    snvp != NULL;
			    snvp = nvlist_next_nvpair(src_sec_nvl, snvp)) {
				if (nvlist_remove(dst_sec_nvl,
				    nvpair_name(snvp), DATA_TYPE_STRING) != 0) {
					rc = SA_NO_SUCH_PROP;
					(void) snprintf(errbuf, buflen,
					    dgettext(TEXT_DOMAIN,
					    "%s: sec=%s,%s"), sa_strerror(rc),
					    sec_mode_name, nvpair_name(snvp));
					return (rc);
				}
			}
		}
	} else {
		/*
		 * If there is a matching security nvlist in the
		 * destination protocol nvlist, then merge the two
		 * security lists, otherwise add the source security
		 * nvlist to the destination protocol nvlist.
		 */
		if (nvlist_lookup_nvlist(dst_prot_nvl, sec_mode_name,
		    &dst_sec_nvl) == 0) {
			if (nvlist_merge(dst_sec_nvl, src_sec_nvl, 0) != 0) {
				rc = SA_NO_MEMORY;
				(void) snprintf(errbuf, buflen,
				    dgettext(TEXT_DOMAIN, "%s"),
				    sa_strerror(rc));
				return (rc);
			}
		} else {
			if (nvlist_add_nvlist(dst_prot_nvl, sec_mode_name,
			    src_sec_nvl) != 0) {
				rc = SA_NO_MEMORY;
				(void) snprintf(errbuf, buflen,
				    dgettext(TEXT_DOMAIN, "%s"),
				    sa_strerror(rc));
				return (rc);
			}
		}
	}

	return (SA_OK);
}

/*
 * sa_nfs_share_merge
 *
 * Merge a new nfs protocol property list with an existing property list.
 * Existing properties will be overwritten, new properties will be added.
 * If the 'unset' flag is set, then existing properties will be removed.
 *
 * INPUTS:
 *     dst_prot_nvl: existing protocol property list. Values from src_prot_nvl
 *                   will be merged into this list.
 *     src_prot_nvl: New protocol property list. This is a list of properties
 *                   that will be updated in dst_prot_nvl.
 *     unset:        If set, the properties identified in src_prot_nvl will be
 *                   removed from dst_prot_nvl.
 *
 * OUTPUTS:
 *     dst_prot_nvl: Updated property list
 *     errbuf:       if error, error message is copied here.
 */
static int
sa_nfs_share_merge(nvlist_t *dst_prot_nvl, nvlist_t *src_prot_nvl, int unset,
    char *errbuf, size_t buflen)
{
	nvpair_t *nvp;
	nvlist_t *src_sec_nvl;
	char *propname;
	int rc;

	for (nvp = nvlist_next_nvpair(src_prot_nvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(src_prot_nvl, nvp)) {

		propname = nvpair_name(nvp);
		if (nvpair_type(nvp) == DATA_TYPE_NVLIST) {

			if (nvpair_value_nvlist(nvp, &src_sec_nvl) != 0) {
				rc = SA_NO_MEMORY;
				(void) snprintf(errbuf, buflen,
				    dgettext(TEXT_DOMAIN, "%s"),
				    sa_strerror(rc));
				return (rc);
			}

			rc = nfs_merge_security(dst_prot_nvl, src_sec_nvl,
			    propname, unset, errbuf, buflen);
			if (rc != SA_OK)
				return (rc);
		} else {
			if (unset) {
				if (nvlist_remove(dst_prot_nvl, propname,
				    DATA_TYPE_STRING) != 0) {
					rc = SA_NO_SUCH_PROP;
					(void) snprintf(errbuf, buflen,
					    dgettext(TEXT_DOMAIN, "%s: %s"),
					    sa_strerror(rc), propname);
					return (rc);
				}
			} else {
				/*
				 * Just add the new nvpair to the destination
				 * protocol nvlist. Because the nvlist was
				 * created with the NV_UNIQUE_NAME flag,
				 * existing properties will be overwritten
				 * with the new property.
				 */
				if (nvlist_add_nvpair(dst_prot_nvl, nvp) != 0) {
					rc = SA_NO_MEMORY;
					(void) snprintf(errbuf, buflen,
					    dgettext(TEXT_DOMAIN, "%s"),
					    sa_strerror(rc));
					return (rc);
				}
			}
		}
	}

	return (SA_OK);
}

/*
 * sa_nfs_share_set_def_proto
 *
 * add a default proto property list to the share
 */
static int
sa_nfs_share_set_def_proto(nvlist_t *share)
{
	int rc;
	nvlist_t *prot_nvl;

	if (nvlist_alloc(&prot_nvl, NV_UNIQUE_NAME, 0) != 0) {
		salog_error(SA_NO_MEMORY, "smb_share_set_def_proto");
		return (SA_NO_MEMORY);
	}

	if ((rc = sa_share_set_proto(share, SA_PROT_NFS, prot_nvl)) != SA_OK)
		salog_error(rc, "nfs_share_set_def_proto");

	nvlist_free(prot_nvl);
	return (rc);
}

/*
 * is_a_number(number)
 *
 * is the string a number in one of the forms we want to use?
 */

static int
is_a_number(const char *number)
{
	int ret = 1;
	int hex = 0;

	if (strncmp(number, "0x", 2) == 0) {
		number += 2;
		hex = 1;
	} else if (*number == '-') {
		number++; /* skip the minus */
	}
	while (ret == 1 && *number != '\0') {
		if (hex) {
			ret = isxdigit(*number++);
		} else {
			ret = isdigit(*number++);
		}
	}
	return (ret);
}

static int
validate_user_type(const char *name, const char *val,
    char *errbuf, int buflen)
{
	struct passwd pwd;
	struct passwd *pwdp;
	int pwd_rc;
	char *pwd_bufp = NULL;
	size_t pwd_buflen = 0;
	int rc;

	if (!is_a_number(val)) {
		/*
		 * in this case it would have to be a
		 * user name
		 */
		pwd_buflen = sysconf(_SC_GETPW_R_SIZE_MAX);
		pwd_bufp = malloc(pwd_buflen);
		if (pwd_bufp == NULL) {
			rc = SA_NO_MEMORY;
			(void) snprintf(errbuf, buflen,
			    dgettext(TEXT_DOMAIN, "%s=%s: %s"),
			    name, val, sa_strerror(rc));
			return (rc);
		}

		pwd_rc = getpwnam_r(val, &pwd, pwd_bufp, pwd_buflen, &pwdp);
		endpwent();
		free(pwd_bufp);
		if (pwd_rc != 0 || pwdp == NULL) {
			rc = SA_INVALID_UNAME;
			(void) snprintf(errbuf, buflen,
			    dgettext(TEXT_DOMAIN, "%s=%s: %s"),
			    name, val, sa_strerror(rc));
			return (rc);
		}
	} else {
		uint64_t intval;
		intval = strtoull(val, NULL, 0);
		if (intval > UID_MAX && intval != ~0) {
			rc = SA_INVALID_UID;
			(void) snprintf(errbuf, buflen,
			    dgettext(TEXT_DOMAIN, "%s=%s: %s"),
			    name, val, sa_strerror(rc));
			return (rc);
		}
	}

	return (SA_OK);
}

/*
 * sa_nfs_check_acclist
 *
 * check syntax of access list value
 * This will only check multi-entry values.
 * Cannot have '*' with other values
 *
 * This could be enhanced to check for duplicates
 */
static int
sa_nfs_check_acclist(const char *prop, const char *value, char *errbuf,
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

static boolean_t
sa_nfs_acccmp(char *prop1, char *prop2)
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
sa_nfs_validate_acclist(nvlist_t *optlist, char *errbuf, size_t buflen)
{
	char *ro, *rw, *none;
	char *prop1, *prop2;
	int rc;

	ro = sa_share_get_prop(optlist, SHOPT_RO);
	rw = sa_share_get_prop(optlist, SHOPT_RW);
	none = sa_share_get_prop(optlist, SHOPT_NONE);

	if (sa_nfs_acccmp(ro, rw)) {
		prop1 = SHOPT_RO;
		prop2 = SHOPT_RW;
		goto invalid;
	}

	if (sa_nfs_acccmp(ro, none)) {
		prop1 = SHOPT_RO;
		prop2 = SHOPT_NONE;
		goto invalid;
	}

	if (sa_nfs_acccmp(rw, none)) {
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
find_gbl_prop(const char *propname)
{
	int i;

	if (propname == NULL || *propname == '\0')
		return (-1);

	for (i = 0; gbl_prop_defs[i].tag != NULL; ++i) {
		if (strcmp(propname, gbl_prop_defs[i].tag) == 0)
			return (i);
	}

	if (sa_prop_cmp_list(propname, legal_charset) != -1)
		return (OPT_CHARSET_MAP);

	return (-1);
}

static int
find_sec_prop(const char *propname)
{
	int i;

	if (propname == NULL || *propname == '\0')
		return (-1);

	for (i = 0; sec_prop_defs[i].tag != NULL; ++i) {
		if (strcmp(propname, sec_prop_defs[i].tag) == 0)
			return (i);
	}

	return (-1);
}


static int
find_gbl_prop_type(const char *propname)
{
	int i;

	if (propname == NULL || *propname == '\0')
		return (-1);

	for (i = 0; gbl_prop_defs[i].tag != NULL; ++i) {
		if (strcmp(propname, gbl_prop_defs[i].tag) == 0)
			return (gbl_prop_defs[i].type);
	}

	if (sa_prop_cmp_list(propname, legal_charset) != -1)
		return (gbl_prop_defs[OPT_CHARSET_MAP].type);

	return (-1);
}

static int
find_sec_prop_type(const char *propname)
{
	int i;

	if (propname == NULL || *propname == '\0')
		return (-1);

	for (i = 0; sec_prop_defs[i].tag != NULL; ++i) {
		if (strcmp(propname, sec_prop_defs[i].tag) == 0)
			return (sec_prop_defs[i].type);
	}

	return (-1);
}

/*
 * nfs_space_alias(alias)
 *
 * Lookup the space (security) name. If it is default, convert to the
 * real name. In a multithreaded environment or if nfs_getseconfig is
 * called later, can't trust that name will survive so strdup() it.
 */

static char *
nfs_space_alias(char *space)
{
	char *name = space;
	seconfig_t secconf;

	/*
	 * Only the space named "default" is special. If it is used,
	 * the default needs to be looked up and the real name used.
	 * This is normally "sys" but could be changed.  We always
	 * change default to the real name.
	 */
	if (strcmp(space, "default") == 0 &&
	    nfs_getseconfig_default(&secconf) == 0) {
		if (nfs_getseconfig_bynumber(secconf.sc_nfsnum, &secconf) == 0)
			name = secconf.sc_name;
	}
	return (strdup(name));
}

static int
nfs_sec_validate(nvlist_t *sec_nvl, char *errbuf, int buflen)
{
	nvpair_t *nvp;
	int prop;
	int proptype;
	char *propname;
	char *propval;
	boolean_t acclist_validated = B_FALSE;
	int rc;

	for (nvp = nvlist_next_nvpair(sec_nvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(sec_nvl, nvp)) {

		propname = nvpair_name(nvp);
		prop = find_sec_prop(propname);

		if (prop == -1) {
			rc = SA_INVALID_NFS_PROP;
			(void) snprintf(errbuf, buflen,
			    dgettext(TEXT_DOMAIN, "%s: %s"),
			    sa_strerror(rc), propname);
			return (rc);
		} else {
			proptype = sec_prop_defs[prop].type;
		}

		if (nvpair_value_string(nvp, &propval) != 0) {
			rc = SA_INVALID_PROP_VAL;
			(void) snprintf(errbuf, buflen,
			    dgettext(TEXT_DOMAIN, "%s: %s"),
			    sa_strerror(rc), propname);
			return (rc);
		}

		switch (proptype) {
		case OPT_TYPE_NUMBER:
			/* check that the value is all digits */
			if (!is_a_number(propval)) {
				rc = SA_INVALID_PROP_VAL;
				(void) snprintf(errbuf, buflen,
				    dgettext(TEXT_DOMAIN, "%s: %s=%s"),
				    sa_strerror(rc), propname, propval);
				return (rc);
			}
			break;

		case OPT_TYPE_ACCLIST:
			rc = sa_nfs_check_acclist(propname, propval,
			    errbuf, buflen);
			if (rc != SA_OK)
				return (rc);

			if (!acclist_validated) {
				rc = sa_nfs_validate_acclist(sec_nvl,
				    errbuf, buflen);
				if (rc != SA_OK)
					return (rc);
				acclist_validated = B_TRUE;
			}
			break;

		case OPT_TYPE_USER:
			rc = validate_user_type(propname, propval,
			    errbuf, buflen);
			if (rc != SA_OK) {
				return (rc);
			}
			break;
		}

	}

	return (SA_OK);
}

/*
 * validate that this path has not been shared (for NFS) or
 * no other NFS share exists in path (ancestor/decendents)
 */
static int
nfs_validate_path(const char *sh_name, const char *sh_path,
    char *errbuf, size_t buflen)
{
	int rc;
	char mntpnt[MAXPATHLEN];
	void *hdl;
	nvlist_t *share;
	size_t sh_pathlen;
	struct stat st_mntpnt;
	struct stat st_path;

	sh_pathlen = strlen(sh_path);

	if ((rc = sa_get_mntpnt_for_path(sh_path, mntpnt, sizeof (mntpnt),
	    NULL, 0, NULL, 0)) != SA_OK) {
		salog_error(rc, "nfs_validate_path: "
		    "error obtaining mountpoint for %s", sh_path);
		(void) snprintf(errbuf, buflen, dgettext(TEXT_DOMAIN,
		    "path=%s: %s"), sh_path, sa_strerror(rc));
		return (rc);
	}

	/*
	 * iterate through all shares on dataset
	 */
	rc = SA_OK;
	if (sa_share_read_init(mntpnt, &hdl) == SA_OK) {
		while (sa_share_read_next(hdl, &share) == SA_OK) {
			char *p;
			char *name;
			char *path;
			size_t pathlen;

			/*
			 * ignore if this is not an NFS share or has the
			 * same share name.
			 */
			if ((sa_share_get_proto(share, SA_PROT_NFS) == NULL) ||
			    ((path = sa_share_get_path(share)) == NULL) ||
			    ((name = sa_share_get_name(share)) == NULL) ||
			    (strcmp(sh_name, name) == 0)) {
				sa_share_free(share);
				continue;
			}

			pathlen = strlen(path);

			if (strncmp(sh_path, path, MIN(sh_pathlen, pathlen))
			    == 0) {
				if (sh_pathlen == pathlen)
					rc = SA_DUPLICATE_PATH;
				else if (sh_pathlen < pathlen) {
					p = path + sh_pathlen;
					if (*p == '/' ||
					    strcmp(sh_path, "/") == 0)
						rc = SA_DESCENDANT_SHARED;
				} else { /* pathlen < sh_pathlen */
					p = (char *)sh_path + pathlen;
					if (*p == '/' ||
					    strcmp(path, "/") == 0)
						rc = SA_ANCESTOR_SHARED;
				}
				if (rc != SA_OK) {
					(void) snprintf(errbuf, buflen,
					    dgettext(TEXT_DOMAIN,
					    "NFS: %s: %s in %s"),
					    sa_strerror(rc), path, name);
					sa_share_free(share);
					break;
				}
			}

			sa_share_free(share);
		}

		sa_share_read_fini(hdl);
	}


	/*
	 * Even if it passes the above tests, it could be a LOFS file
	 * system inside the autofs. In this case, if the st_rdev's
	 * are the same we reject it. The code also catches the auto
	 * mount not being there.
	 */
	if (rc == SA_OK && stat(mntpnt, &st_mntpnt) >= 0 &&
	    strcmp(st_mntpnt.st_fstype, MNTTYPE_LOFS) == 0) {
		if (stat(sh_path, &st_path) < 0)
			rc = SA_INVALID_SHARE_PATH;
		else if (deveq(st_mntpnt.st_rdev, st_path.st_rdev))
			rc = SA_INVALID_SHARE;
		if (rc != SA_OK) {
			(void) snprintf(errbuf, buflen,
			    dgettext(TEXT_DOMAIN, "NFS: Invalid lofs or autofs"
			    " share: %s in %s"), sh_path, sh_name);
		}
	}

	return (rc);
}

/*
 * nfs_public_exists
 *
 * search permanent storage for an existing share with
 * the public option.
 */
static boolean_t
nfs_public_exists(char *path, char *errbuf, size_t buflen)
{
	int found = B_FALSE;
	void *hdl;
	nvlist_t *nfs;
	char *sh_path;
	char *sh_name;
	nvlist_t *share;

	if (sa_share_read_init(NULL, &hdl) != SA_OK)
		return (B_FALSE);

	while (!found && sa_share_read_next(hdl, &share) == SA_OK) {
		nfs = sa_share_get_proto(share, SA_PROT_NFS);
		if ((nfs != NULL) &&
		    ((sh_path = sa_share_get_path(share)) != NULL) &&
		    (strcmp(path, sh_path) != 0) &&
		    (sa_share_get_prop(nfs, SHOPT_PUBLIC) != NULL)) {
			found = B_TRUE;
			sh_name = sa_share_get_name(share);
			(void) snprintf(errbuf, buflen,
			    dgettext(TEXT_DOMAIN,
			    "%s: only one instance of \"public\" allowed: "
			    "%s:%s"), sa_strerror(SA_INVALID_NFS_PROP),
			    sh_name ? sh_name : "", sh_path);
		}
		sa_share_free(share);
	}
	sa_share_read_fini(hdl);

	return (found);
}

/*
 * nfs_public_active
 *
 * search active shares for a share with public option
 */
static boolean_t
nfs_public_active(const char *path)
{
	int found = B_FALSE;
	void *hdl;
	nvlist_t *nfs;
	char *sh_path;
	nvlist_t *share;

	if (sa_share_find_init(NULL, SA_PROT_NFS, &hdl) != SA_OK)
		return (B_FALSE);

	while (!found && sa_share_find_next(hdl, &share) == SA_OK) {
		if (sa_share_get_status(share) & SA_PROT_NFS) {
			nfs = sa_share_get_proto(share, SA_PROT_NFS);
			if ((nfs != NULL) &&
			    ((sh_path = sa_share_get_path(share)) != NULL) &&
			    (strcmp(path, sh_path) != 0) &&
			    (sa_share_get_prop(nfs, SHOPT_PUBLIC) != NULL)) {
				found = B_TRUE;
			}
		}
		sa_share_free(share);
	}
	sa_share_find_fini(hdl);

	return (found);
}

static int
sa_nfs_share_validate_name(const char *sh_name, boolean_t new,
    char *errbuf, size_t buflen)
{
	NOTE(ARGUNUSED(new))

	int rc = SA_OK;

	if (sh_name == NULL) {
		rc = SA_NO_SHARE_NAME;
		(void) snprintf(errbuf, buflen,
		    dgettext(TEXT_DOMAIN, "%s"),
		    sa_strerror(rc));
		return (rc);
	}

	return (rc);
}

static int
sa_nfs_share_validate(nvlist_t *share, boolean_t new, char *errbuf,
    size_t buflen)
{
	NOTE(ARGUNUSED(new))

	int rc;
	int prop;
	char *sh_name;
	char *sh_path;
	char *propname;
	char *propval;
	nvlist_t *prot_nvl;
	nvlist_t *sec_nvl;
	nvpair_t *nvp;
	nfsl_config_t *configlist = NULL;

	sh_name = sa_share_get_name(share);
	if (sh_name == NULL) {
		rc = SA_NO_SHARE_NAME;
		(void) snprintf(errbuf, buflen,
		    dgettext(TEXT_DOMAIN, "%s"),
		    sa_strerror(rc));
		return (rc);
	}

	sh_path = sa_share_get_path(share);
	if (sh_path == NULL) {
		rc = SA_NO_SHARE_PATH;
		(void) snprintf(errbuf, buflen,
		    dgettext(TEXT_DOMAIN, "%s"),
		    sa_strerror(rc));
		return (rc);
	}

	if ((rc = nfs_validate_path(sh_name, sh_path, errbuf, buflen)) != SA_OK)
		return (rc);

	/*
	 * MUST have a nfs protocol property list
	 * It is ok if list is empty
	 */
	prot_nvl = sa_share_get_proto(share, SA_PROT_NFS);
	if (prot_nvl == NULL) {
		rc = SA_NO_SUCH_PROTO;
		(void) snprintf(errbuf, buflen,
		    dgettext(TEXT_DOMAIN, "%s: nfs"),
		    sa_strerror(rc));
		return (rc);
	}

	for (nvp = nvlist_next_nvpair(prot_nvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(prot_nvl, nvp)) {

		/*
		 * if nvpair type == nvlist, then security list
		 */
		if (nvpair_type(nvp) == DATA_TYPE_NVLIST) {
			propname = nvpair_name(nvp);
			if (strcmp(propname, "default") != 0 &&
			    sa_prop_cmp_list(propname, sec_flavors) < 0) {
				rc = SA_INVALID_SECURITY;
				(void) snprintf(errbuf, buflen,
				    dgettext(TEXT_DOMAIN, "%s: %s"),
				    sa_strerror(rc), propname);
				return (rc);
			}

			if (nvpair_value_nvlist(nvp, &sec_nvl) != 0) {
				rc = SA_NO_MEMORY;
				(void) snprintf(errbuf, buflen,
				    dgettext(TEXT_DOMAIN, "%s"),
				    sa_strerror(rc));
				return (rc);
			}

			rc = nfs_sec_validate(sec_nvl, errbuf, buflen);
			if (rc != SA_OK)
				return (rc);
			continue;
		}

		propname = nvpair_name(nvp);
		prop = find_gbl_prop(propname);
		if (prop == -1) {
			if (find_sec_prop(propname) != -1) {
				rc = SA_SYNTAX_ERR;
				(void) snprintf(errbuf, buflen,
				    dgettext(TEXT_DOMAIN,
				    "%s: must follow sec property: %s"),
				    sa_strerror(rc), propname);
				return (rc);
			} else {
				rc = SA_INVALID_NFS_PROP;
				(void) snprintf(errbuf, buflen,
				    dgettext(TEXT_DOMAIN, "%s: %s"),
				    sa_strerror(rc), propname);
				return (rc);
			}
		}

		if (prop == OPT_PUBLIC &&
		    nfs_public_exists(sh_path, errbuf, buflen)) {
			/*
			 * Public is special in that only one instance can
			 * be in the repository at the same time.
			 */
			return (SA_INVALID_NFS_PROP);
		}

		if (nvpair_value_string(nvp, &propval) != 0) {
			rc = SA_INVALID_PROP_VAL;
			(void) snprintf(errbuf, buflen,
			    dgettext(TEXT_DOMAIN, "%s: %s"),
			    sa_strerror(rc), propname);
			return (rc);
		}

		switch (gbl_prop_defs[prop].type) {

		case OPT_TYPE_NUMBER:
			/* check that the value is all digits */
			if (!is_a_number(propval)) {
				rc = SA_INVALID_PROP_VAL;
				(void) snprintf(errbuf, buflen,
				    dgettext(TEXT_DOMAIN, "%s: %s=%s"),
				    sa_strerror(rc), propname, propval);
				return (rc);
			}
			break;

		case OPT_TYPE_BOOLEAN:
			if (strcasecmp(propval, "true") != 0 &&
			    strcmp(propval, "1") != 0 &&
			    strcasecmp(propval, "false") != 0 &&
			    strcmp(propval, "0") != 0) {
				rc = SA_INVALID_PROP_VAL;
				(void) snprintf(errbuf, buflen,
				    dgettext(TEXT_DOMAIN, "%s: %s=%s"),
				    sa_strerror(rc), propname, propval);
				return (rc);
			}
			break;

		case OPT_TYPE_USER:
			rc = validate_user_type(propname, propval,
			    errbuf, buflen);
			if (rc != SA_OK) {
				return (rc);
			}
			break;

		case OPT_TYPE_FILE:
			if (strcmp(propval, "..") == 0 ||
			    strchr(propval, '/') != NULL) {
				rc = SA_INVALID_FNAME;
				(void) snprintf(errbuf, buflen,
				    dgettext(TEXT_DOMAIN, "%s: %s=%s"),
				    sa_strerror(rc), propname, propval);
				return (rc);
			}
			break;

		case OPT_TYPE_ACCLIST:
			rc = sa_nfs_check_acclist(propname, propval,
			    errbuf, buflen);
			if (rc != SA_OK)
				return (rc);
			break;

		case OPT_TYPE_LOGTAG:
			if (nfsl_getconfig_list(&configlist) == 0) {
				int error;
				if (propval == NULL ||
				    strlen(propval) == 0)
					propval = "global";

				if (propval != NULL &&
				    nfsl_findconfig(configlist, propval,
				    &error) == NULL)
					rc = SA_INVALID_PROP_VAL;
				/* Must always free when done */
				nfsl_freeconfig_list(&configlist);
			} else {
				rc = SA_CONFIG_ERR;
			}
			if (rc != SA_OK) {
				(void) snprintf(errbuf, buflen,
				    dgettext(TEXT_DOMAIN, "%s: %s=%s"),
				    sa_strerror(rc), propname, propval);
				return (rc);
			}
			break;

		case OPT_TYPE_STRING:
			if (*propval == '\0') {
				rc = SA_INVALID_PROP_VAL;
				(void) snprintf(errbuf, buflen,
				    dgettext(TEXT_DOMAIN, "%s: %s=%s"),
				    sa_strerror(rc), propname, propval);
				return (rc);
			}
			break;

		default:
			break;
		}
	}


	return (SA_OK);
}

/*
 * Look for the specified tag in the configuration file. If it is found,
 * enable logging and set the logging configuration information for exp.
 */
static int
configlog(struct exportdata *exp, char *tag)
{
	nfsl_config_t *configlist = NULL, *configp;
	int error = 0;
	char globaltag[] = DEFAULTTAG;

	/*
	 * Sends config errors to stderr
	 */
	nfsl_errs_to_syslog = B_FALSE;

	/*
	 * get the list of configuration settings
	 */
	error = nfsl_getconfig_list(&configlist);
	if (error) {
		salog_error(0, "NFS: Cannot get log configuration: %s",
		    strerror(error));
		error = SA_SYSTEM_ERR;
		goto err;
	}

	if (tag == NULL)
		tag = globaltag;
	if ((configp = nfsl_findconfig(configlist, tag, &error)) == NULL) {
		nfsl_freeconfig_list(&configlist);
		salog_error(0, "NFS: No tags matching \"%s\"", tag);
		/* bad configuration */
		error = SA_INVALID_PROP_VAL;
		goto err;
	}

	if ((exp->ex_tag = strdup(tag)) == NULL) {
		error = SA_NO_MEMORY;
		goto out;
	}
	if ((exp->ex_log_buffer = strdup(configp->nc_bufferpath)) == NULL) {
		error = SA_NO_MEMORY;
		goto out;
	}
	exp->ex_flags |= EX_LOG;
	if (configp->nc_rpclogpath != NULL)
		exp->ex_flags |= EX_LOG_ALLOPS;
out:
	if (configlist != NULL)
		nfsl_freeconfig_list(&configlist);

err:
	if (error != 0) {
		if (exp->ex_flags != NULL)
			free(exp->ex_tag);
		if (exp->ex_log_buffer != NULL)
			free(exp->ex_log_buffer);
		salog_error(0, "NFS: Cannot set log configuration: %s",
		    strerror(error));
	}
	return (error);
}

static int
sa_nfs_count_security(nvlist_t *proplist)
{
	int count = 0;
	nvpair_t *nvpair;

	for (nvpair = nvlist_next_nvpair(proplist, NULL);
	    nvpair != NULL;
	    nvpair = nvlist_next_nvpair(proplist, nvpair)) {
		if (nvpair_type(nvpair) == DATA_TYPE_NVLIST) {
			count++;
		}
	}
	return (count);
}

/*
 * Given a seconfig entry and a colon-separated
 * list of names, allocate an array big enough
 * to hold the root list, then convert each name to
 * a principal name according to the security
 * info and assign it to an array element.
 * Return the array and its size.
 */
static caddr_t *
get_rootnames(seconfig_t *sec, char *list, int *count)
{
	caddr_t *a;
	int c, i;
	char *host, *p;
	char *lasts;

	/*
	 * Count the number of strings in the list.
	 * This is the number of colon separators + 1.
	 */
	c = 1;
	for (p = list; *p; p++)
		if (*p == ':')
			c++;
	*count = c;

	a = (caddr_t *)malloc(c * sizeof (char *));
	if (a == NULL) {
		salog_error(SA_NO_MEMORY, "NFS get_rootnames");
	} else {
		for (i = 0; i < c; i++) {
			host = strtok_r(list, ":", &lasts);
			if (!nfs_get_root_principal(sec, host, &a[i])) {
				free(a);
				a = NULL;
				break;
			}
			list = NULL;
		}
	}

	return (a);
}

static int
sa_nfs_fill_security(nvlist_t *seclist, const char *sh_path,
    const char *sectype, struct secinfo *secinfo)
{
	nvpair_t *secopt;
	boolean_t longform;
	char *name;
	char *value;
	int type;
	struct passwd pwd;
	struct passwd *pwdp;
	int pwd_rc;
	char *pwd_bufp = NULL;
	size_t pwd_buflen = 0;
	int err = SA_OK;
	int val;

	if (sectype != NULL) {
		/* named security type needs secinfo to be filled in */
		err = nfs_getseconfig_byname((char *)sectype,
		    &secinfo->s_secinfo);
	} else {
		/* default case */
		err = nfs_getseconfig_default(&secinfo->s_secinfo);
	}

	if (err != SC_NOERROR) {
		switch (err) {
		case SC_NOMEM:
			err = SA_NO_MEMORY;
			break;
		case SC_OPENFAIL:
			err = SA_INTERNAL_ERR;
			break;
		case SC_NOTFOUND:
		default:
			err = SA_NO_SUCH_SECURITY;
			break;
		}
		salog_error(err,
		    "NFS: %s: error obtaining security config for %s",
		    sh_path, sectype != NULL ? sectype : "default");
		return (err);
	}

	for (secopt = nvlist_next_nvpair(seclist, NULL);
	    secopt != NULL;
	    secopt = nvlist_next_nvpair(seclist, secopt)) {

		if (nvpair_type(secopt) != DATA_TYPE_STRING)
			continue;

		name = nvpair_name(secopt);
		type = find_sec_prop(name);
		if (nvpair_value_string(secopt, &value) != 0) {
			salog_error(0,
			    "NFS: %s: invalid security property: %s:%s",
			    sh_path,
			    sectype != NULL ? sectype : "default",
			    name);
			return (SA_INVALID_PROP_VAL);
		}

		longform = ((value != NULL) && (strcmp(value, "*") != 0));

		switch (type) {
		case OPT_RO:
			secinfo->s_flags |= longform ? M_ROL : M_RO;
			break;
		case OPT_RW:
			secinfo->s_flags |= longform ? M_RWL : M_RW;
			break;
		case OPT_ROOT:
			secinfo->s_flags |= M_ROOT;
			/*
			 * if we are using AUTH_UNIX, handle like other things
			 * such as RO/RW
			 */
			if (secinfo->s_secinfo.sc_rpcnum == AUTH_UNIX)
				continue;
			/* not AUTH_UNIX */
			if (value != NULL) {
				char *root_val = strdup(value);
				if (root_val == NULL) {
					err = SA_NO_MEMORY;
					salog_error(err,
					    "NFS: %s: error processing "
					    "security properties", sh_path);
					return (err);
				}

				secinfo->s_rootnames =
				    get_rootnames(&secinfo->s_secinfo,
				    root_val, &secinfo->s_rootcnt);
				free(root_val);
				if (secinfo->s_rootnames == NULL) {
					err = SA_INVALID_PROP_VAL;
					salog_error(err,
					    "NFS: %s: invalid root list: %s=%s",
					    sh_path, name, value);
					return (err);
				}
			}
			break;
		case OPT_WINDOW:
			if (value != NULL) {
				secinfo->s_window = atoi(value);
				/* just in case */
				if (secinfo->s_window < 0)
					secinfo->s_window = DEF_WIN;
			}
			break;
		case OPT_NONE:
			secinfo->s_flags |= M_NONE;
			break;
		case OPT_ROOT_MAPPING:
			if (value != NULL && is_a_number(value)) {
				val = strtoul(value, NULL, 0);
			} else {
				if (pwd_buflen == 0) {
					pwd_buflen = sysconf(
					    _SC_GETPW_R_SIZE_MAX);
					pwd_bufp = malloc(pwd_buflen);
					if (pwd_bufp == NULL) {
						salog_error(SA_NO_MEMORY,
						    "NFS: %s: error processing "
						    "security properties",
						    sh_path);
						return (SA_NO_MEMORY);
					}
				}

				pwd_rc = getpwnam_r(value != NULL ? value :
				    "nobody", &pwd, pwd_bufp, pwd_buflen,
				    &pwdp);
				if (pwd_rc != 0 || pwdp == NULL)
					val = UID_NOBODY;
				else
					val = pwdp->pw_uid;
				endpwent();
			}
			secinfo->s_rootid = val;
			break;
		}
	}

	if (pwd_bufp != NULL)
		free(pwd_bufp);
	return (SA_OK);
}

/*
 * cleanup_export(export)
 *
 * Cleanup the allocated areas so we don't leak memory
 * This frees added structure pointers.
 */
static void
cleanup_export(struct exportdata *export)
{
	int i, j;
	struct secinfo *sp;

	if (export->ex_index != NULL) {
		free(export->ex_index);
		export->ex_index = NULL;
	}

	if (export->ex_secinfo != NULL) {
		for (i = 0; i < export->ex_seccnt; i++) {
			sp = &export->ex_secinfo[i];
			if (sp->s_rootnames != NULL) {
				for (j = 0; j < sp->s_rootcnt; j++)
					if (sp->s_rootnames[j] != NULL)
						free(sp->s_rootnames[j]);
				free(sp->s_rootnames);
			}
		}
		free(export->ex_secinfo);
		export->ex_secinfo = NULL;
	}
}

#ifdef	VOLATILE_FH_TEST
/*
 * Set the ex_flags to indicate which fh expire type.
 * Return 0 for success, error otherwise.
 */
static int
nfs4_set_volfh_flags(struct exportdata *exp, char *volfhtypes)
{
	char	*voltype, *next;
	int	err = 0;

	for (voltype = volfhtypes; !err && voltype != NULL; voltype = next) {
		while (*voltype == ':') voltype++;
		next = strchr(voltype, ':');
		if (next != NULL)
			*next = '\0';
		if (strcmp(voltype, "any") == 0)
			exp->ex_flags |= EX_VOLFH;
		else if (strcmp(voltype, "rnm") == 0)
			exp->ex_flags |= EX_VOLRNM;
		else if (strcmp(voltype, "mig") == 0)
			exp->ex_flags |= EX_VOLMIG;
		else if (strcmp(voltype, "noexpopen") == 0)
			exp->ex_flags |= EX_NOEXPOPEN;
		else {
			err = EINVAL; /* invalid arg */
		}
		if (next != NULL)
			*next = ':';
	}
	return (err);
}
#endif	/* VOLATILE_FH_TEST */

static int
sa_nfs_fill_export(const char *sh_path, nvlist_t *proplist,
    struct exportdata *export)
{
	nvpair_t *nvp;
	nvpair_t *next_nvp;
	nvlist_t *seclist;
	char *name;
	int type;
	char *value;
	struct passwd pwd;
	struct passwd *pwdp;
	int pwd_rc;
	char *pwd_bufp = NULL;
	size_t pwd_buflen = 0;
	uint32_t val;
	int rc = SA_OK;
	int sec_err = SA_OK;
	int err;
	int numsec;
	int whichsec = 0;
	struct secinfo *sp;

	numsec = sa_nfs_count_security(proplist);
	/*
	 * must allocate for default security options
	 */
	if (numsec == 0)
		numsec = 1;
	sp = calloc(numsec, sizeof (struct secinfo));
	if (sp == NULL) {
		salog_error(SA_NO_MEMORY, "sa_nfs_fill_export: %s", sh_path);
		return (SA_NO_MEMORY);
	}

	/*
	 * since we must have one security option defined, we
	 * init to the default and then override as we find
	 * defined security options. This handles the case
	 * where we have no defined options but we need to set
	 * up one.
	 */
	sp[0].s_window = DEF_WIN;
	sp[0].s_rootnames = NULL;
	/* setup a default in case no properties defined */
	if ((err = nfs_getseconfig_default(&sp[0].s_secinfo)) != SC_NOERROR) {
		salog_error(0,
		    "NFS: %s: failed to get default security mode: %d",
		    sh_path, err);
		if (err == SC_NOMEM)
			rc = SA_NO_MEMORY;
		else if (err == SC_OPENFAIL)
			rc = SA_INTERNAL_ERR;
		else
			rc = SA_NO_SUCH_SECURITY;
		free(sp);
		return (rc);
	}

	export->ex_secinfo = sp;
	export->ex_seccnt = numsec;

	for (nvp = nvlist_next_nvpair(proplist, NULL);
	    nvp != NULL; nvp = next_nvp) {

		next_nvp = nvlist_next_nvpair(proplist, nvp);
		name = nvpair_name(nvp);

		if (nvpair_type(nvp) == DATA_TYPE_NVLIST) {
			/*
			 * Security options are in a sub-nvlist and
			 * are filled separately.
			 */
			if (nvpair_value_nvlist(nvp, &seclist) != 0) {
				rc = SA_NO_MEMORY;
				salog_error(rc, "NFS: %s: error processing "
				    "'%s' security properties", sh_path, name);
				cleanup_export(export);
				return (rc);
			}

			rc = sa_nfs_fill_security(seclist, sh_path, name,
			    sp + whichsec);
			if (rc != SA_OK) {
				if (rc != SA_NO_SUCH_SECURITY) {
					/* error is logged in fill_security */
					cleanup_export(export);
					return (rc);
				} else {
					/*
					 * bad security mode, remove
					 * from property list
					 */
					if (nvlist_remove(proplist, name,
					    DATA_TYPE_NVLIST) != 0) {
						rc = SA_INVALID_SECURITY;
						salog_error(rc,
						    "NFS: %s: error processing"
						    " '%s' security properties",
						    sh_path, name);
						cleanup_export(export);
						return (rc);
					} else {
						sec_err = rc;
					}
				}
			} else {
				whichsec++;
			}
			continue;
		}

		if (nvpair_value_string(nvp, &value) != 0) {
			salog_error(0, "sa_nfs_fill_export: "
			    "invalid value for %s", name);
			cleanup_export(export);
			return (SA_INTERNAL_ERR);
		}

		type = find_gbl_prop(name);
		switch (type) {
		case OPT_ANON:
			if (value != NULL && is_a_number(value)) {
				val = strtoul(value, NULL, 0);
			} else {
				pwd_buflen = sysconf(_SC_GETPW_R_SIZE_MAX);
				pwd_bufp = malloc(pwd_buflen);
				if (pwd_bufp == NULL) {
					salog_error(SA_NO_MEMORY,
					    "sa_nfs_fill_export");
					cleanup_export(export);
					return (SA_NO_MEMORY);
				}

				pwd_rc = getpwnam_r(value != NULL ? value :
				    "nobody", &pwd, pwd_bufp, pwd_buflen,
				    &pwdp);
				if (pwd_rc != 0 || pwdp == NULL)
					val = UID_NOBODY;
				else
					val = pwdp->pw_uid;
				endpwent();
				free(pwd_bufp);
			}
			export->ex_anon = val;
			break;
		case OPT_NOSUID:
			if (value != NULL && (strcasecmp(value, "true") == 0 ||
			    strcmp(value, "1") == 0))
				export->ex_flags |= EX_NOSUID;
			else
				export->ex_flags &= ~EX_NOSUID;
			break;
		case OPT_ACLOK:
			if (value != NULL && (strcasecmp(value, "true") == 0 ||
			    strcmp(value, "1") == 0))
				export->ex_flags |= EX_ACLOK;
			else
				export->ex_flags &= ~EX_ACLOK;
			break;
		case OPT_NOSUB:
			if (value != NULL && (strcasecmp(value, "true") == 0 ||
			    strcmp(value, "1") == 0))
				export->ex_flags |= EX_NOSUB;
			else
				export->ex_flags &= ~EX_NOSUB;
			break;
		case OPT_PUBLIC:
			if (nfs_public_active(sh_path)) {
				salog_error(0, "NFS:: "
				    "public share exists");
				cleanup_export(export);
				return (SA_INVALID_NFS_PROP);
			}

			if (value != NULL && (strcasecmp(value, "true") == 0 ||
			    strcmp(value, "1") == 0))
				export->ex_flags |= EX_PUBLIC;
			else
				export->ex_flags &= ~EX_PUBLIC;
			break;
		case OPT_INDEX:
			if (value != NULL && (strcmp(value, "..") == 0 ||
			    strchr(value, '/') != NULL)) {
				/* this is an error */
				salog_error(0, "NFS: index=\"%s\" not valid: "
				    "must be a filename.", value);
				cleanup_export(export);
				return (SA_INVALID_PROP_VAL);
			}
			if (value != NULL && *value != '\0' &&
			    strcmp(value, ".") != 0) {
				/* valid index file string */
				if (export->ex_index != NULL) {
					/* left over from "default" */
					free(export->ex_index);
				}
				/* remember to free */
				export->ex_index = strdup(value);
				if (export->ex_index == NULL) {
					salog_error(SA_NO_MEMORY,
					    "NFS: error setting index "
					    "property");
					cleanup_export(export);
					return (SA_NO_MEMORY);
				}
				export->ex_flags |= EX_INDEX;
			}
			break;
		case OPT_LOG:
			if (value == NULL)
				value = strdup("global");
			if (value != NULL) {
				rc = configlog(export,
				    strlen(value) ? value : "global");
				if (rc != SA_OK) {
					cleanup_export(export);
					return (rc);
				}
			}
			break;
		case OPT_CKSUM:
			break;
		case OPT_CHARSET_MAP:
			/*
			 * Set EX_CHARMAP when there is at least one
			 * charmap conversion property. This will get
			 * checked by the nfs server when it needs to.
			 */
			export->ex_flags |= EX_CHARMAP;
			break;
		case OPT_NOACLFAB:
			if (value != NULL && (strcasecmp(value, "true") == 0 ||
			    strcmp(value, "1") == 0))
				export->ex_flags |= EX_NOACLFAB;
			else
				export->ex_flags &= ~EX_NOACLFAB;
			break;
#ifdef	VOLATILE_FH_TEST
		case OPT_VOLFH:
			/* volatile filehandles - expire on share */
			if (value == NULL)
				(void) printf(gettext(
				    "missing volatile fh types\n"));
			if (nfs4_set_volfh_flags(export, value))
				(void) printf(gettext(
				    "invalid volatile fh types\n"));
			break;
#endif	/* VOLATILE_FH_TEST */
		default:
			/* have a syntactic error */
			salog_error(SA_INVALID_PROP,
			    "sa_nfs_fill_export: %s=%s",
			    name != NULL ? name : "",
			    value != NULL ? value : "");
			break;
		}
	}

	/*
	 * Make sure at least one of the security
	 * modes defined is valid.
	 */
	if (sec_err != SA_OK && whichsec == 0) {
		/* error is logged in fill_security */
		cleanup_export(export);
		return (sec_err);
	}

	return (SA_OK);
}

static int
nfs_sprint_option(char **bufpp, size_t *buf_len,
    size_t chunksize, char *propname, char *value, char **sep)
{
	char *bufp = *bufpp;
	char *buf_endp;
	size_t strsize;
	size_t curlen;
	size_t buf_rem;
	int ptype;

	if (bufp == NULL)
		return (SA_INTERNAL_ERR);

	/*
	 * A future RFE would be to replace this with more
	 * generic code and to possibly handle more types.
	 */

	strsize = strlen(propname) + strlen(*sep);

	ptype = find_gbl_prop_type(propname);
	if (ptype == -1)
		ptype = find_sec_prop_type(propname);

	switch (ptype) {
	case OPT_TYPE_BOOLEAN:
		/*
		 * For NFS, boolean value of FALSE means it
		 * doesn't show up in the option list at all.
		 */
		if (value != NULL && strcasecmp(value, "false") == 0)
			return (SA_OK);
		if (value != NULL) {
			value = NULL;
		}
		break;
	case OPT_TYPE_ACCLIST:
		if (value != NULL && strcmp(value, "*") == 0) {
			value = NULL;
		} else {
			if (value != NULL)
				strsize += 1 + strlen(value);
		}
		break;
	case OPT_TYPE_LOGTAG:
		if (value != NULL && strlen(value) == 0) {
			value = NULL;
		} else {
			if (value != NULL)
				strsize += 1 + strlen(value);
		}
		break;
	default:
		if (value != NULL)
			strsize += 1 + strlen(value);
		break;
	}

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
	if (value == NULL)
		(void) snprintf(buf_endp, buf_rem, "%s%s", *sep,
		    propname);
	else
		(void) snprintf(buf_endp, buf_rem, "%s%s=%s", *sep,
		    propname, value);

	*sep = ",";

	return (SA_OK);
}

static int
sa_nfs_format_props_impl(nvlist_t *props, char **retbuf, boolean_t sharetab)
{
	char *bufp;
	size_t buflen;
	nvpair_t *prop;
	nvlist_t *sec_props;
	nvpair_t *sec_prop;
	char *propname;
	char *propval;
	int propid;
	char *sep = "";
	boolean_t sec_prop_found = B_FALSE;

	if ((bufp = calloc(CHUNKSIZE, sizeof (char))) == NULL)
		return (SA_NO_MEMORY);
	buflen = CHUNKSIZE;

	/*
	 * First, add all global NFS properties
	 */
	for (prop = nvlist_next_nvpair(props, NULL);
	    prop != NULL;
	    prop = nvlist_next_nvpair(props, prop)) {
		/*
		 * security properties are contained in an embedded
		 * nvlist, ignore them for now.
		 */
		if (nvpair_type(prop) != DATA_TYPE_STRING)
			continue;

		propname = nvpair_name(prop);
		propid = find_gbl_prop(propname);
		switch (propid) {
		case -1:
			continue;
		default:
			if (nvpair_value_string(prop, &propval) != 0) {
				free(bufp);
				return (SA_INVALID_PROP);
			}

			if (nfs_sprint_option(&bufp, &buflen, CHUNKSIZE,
			    propname, propval, &sep) != SA_OK) {
				free(bufp);
				return (SA_NO_MEMORY);
			}
			break;
		}
	}

	/*
	 * now add all security mode properties
	 */
	for (prop = nvlist_next_nvpair(props, NULL);
	    prop != NULL;
	    prop = nvlist_next_nvpair(props, prop)) {
		boolean_t prop_found = B_FALSE;
		char *freeprop = NULL;
		/*
		 * looking for sec property nvlists
		 */
		if (nvpair_type(prop) != DATA_TYPE_NVLIST)
			continue;

		propname = nvpair_name(prop);
		/*
		 * add sec=mode. If we are working on the sharetab
		 * entry, then convert sec=default into the correct
		 * form based on the defined default value.
		 */
		if (sharetab && strcmp(propname, "default") == 0) {
			propname = freeprop = nfs_space_alias(propname);
			if (freeprop == NULL) {
				free(bufp);
				return (SA_NO_MEMORY);
			}
			if (strcmp(freeprop, "default") == 0) {
				free(bufp);
				free(freeprop);
				return (SA_INVALID_SECURITY);
			}
		}
		if (nfs_sprint_option(&bufp, &buflen, CHUNKSIZE,
		    "sec", propname, &sep) != SA_OK) {
			free(bufp);
			free(freeprop);
			return (SA_NO_MEMORY);
		}
		free(freeprop);

		/*
		 * retrieve the security property list
		 */
		if (nvpair_value_nvlist(prop, &sec_props) != 0) {
			free(bufp);
			return (SA_INVALID_PROP);
		}

		/*
		 * now add all properties for this security mode
		 */
		for (sec_prop = nvlist_next_nvpair(sec_props, NULL);
		    sec_prop != NULL;
		    sec_prop = nvlist_next_nvpair(sec_props, sec_prop)) {

			if (nvpair_type(sec_prop) != DATA_TYPE_STRING)
				continue;

			propname = nvpair_name(sec_prop);
			if (find_sec_prop(propname) == -1)
				continue;

			if (nvpair_value_string(sec_prop, &propval) != 0) {
				free(bufp);
				return (SA_INVALID_PROP);
			}

			if (nfs_sprint_option(&bufp, &buflen, CHUNKSIZE,
			    propname, propval, &sep) != SA_OK) {
				free(bufp);
				return (SA_NO_MEMORY);
			}
			prop_found = B_TRUE;
		}

		if (!prop_found) {
			/* add 'rw' to security options */
			if (nfs_sprint_option(&bufp, &buflen, CHUNKSIZE,
			    SHOPT_RW, NULL, &sep) != SA_OK) {
				free(bufp);
				return (SA_NO_MEMORY);
			}
		}
		sec_prop_found = B_TRUE;
	}

	if (!sec_prop_found) {
		/* add defined default security option */
		if (nfs_sprint_option(&bufp, &buflen, CHUNKSIZE,
		    NFS_DEFAULT_OPTS, NULL, &sep) != SA_OK) {
			free(bufp);
			return (SA_NO_MEMORY);
		}
	}

	*retbuf = bufp;
	return (SA_OK);
}

static int
sa_nfs_format_props(nvlist_t *props, char **retbuf)
{
	return (sa_nfs_format_props_impl(props, retbuf, B_FALSE));
}

static void
sa_emptyshare(struct exp_share *sh)
{
	if (sh != NULL) {
		if (sh->name != NULL)
			free(sh->name);
		if (sh->opt_str != NULL)
			free(sh->opt_str);
		if (sh->shr_buf != NULL)
			free(sh->shr_buf);
	}
}

static int
sa_fillshare(nvlist_t *share, struct exp_share *sh, boolean_t publish)
{
	char *sh_name;
	nvlist_t *props;
	int ret;

	(void) memset(sh, 0, sizeof (struct exp_share));

	if ((sh_name = sa_share_get_name(share)) == NULL) {
		ret = SA_INVALID_SHARE;
		goto out;
	}

	if ((sh->name = strdup(sh_name)) == NULL) {
		ret = SA_NO_MEMORY;
		goto out;
	}
	sh->name_len = strlen(sh_name);

	if (publish) {
		if ((props = sa_share_get_proto(share, SA_PROT_NFS)) == NULL) {
			ret = SA_NO_SUCH_PROTO;
			goto out;
		}

		if ((ret = sa_nfs_format_props_impl(
		    props, &sh->opt_str, B_TRUE)) != SA_OK)
			goto out;

		sh->opt_len = strlen(sh->opt_str);

		if (nvlist_pack(share, &sh->shr_buf, &sh->shr_len,
		    NV_ENCODE_XDR, 0) != 0) {
			ret = SA_NO_MEMORY;
			goto out;
		}
	}

	return (SA_OK);

out:
	sa_emptyshare(sh);
	return (ret);
}

/*
 * This is for testing only
 * It displays the export structure that
 * goes into the kernel.
 */
static void
printarg(char *path, struct exportdata *ep)
{
	int i, j;
	struct secinfo *sp;

	if (debug == 0)
		return;

	(void) printf("%s:\n", path);
	(void) printf("\tex_version = %d\n", ep->ex_version);
	(void) printf("\tex_path = %s\n", ep->ex_path);
	(void) printf("\tex_pathlen = %ld\n", (ulong_t)ep->ex_pathlen);
	(void) printf("\tex_flags: (0x%02x) ", ep->ex_flags);
	if (ep->ex_flags & EX_NOSUID)
		(void) printf("NOSUID ");
	if (ep->ex_flags & EX_ACLOK)
		(void) printf("ACLOK ");
	if (ep->ex_flags & EX_PUBLIC)
		(void) printf("PUBLIC ");
	if (ep->ex_flags & EX_NOSUB)
		(void) printf("NOSUB ");
	if (ep->ex_flags & EX_LOG)
		(void) printf("LOG ");
	if (ep->ex_flags & EX_CHARMAP)
		(void) printf("CHARMAP ");
	if (ep->ex_flags & EX_LOG_ALLOPS)
		(void) printf("LOG_ALLOPS ");
	if (ep->ex_flags & EX_NOACLFAB)
		(void) printf("NOACLFAB ");
	if (ep->ex_flags == 0)
		(void) printf("(none)");
	(void) printf("\n");
	if (ep->ex_flags & EX_LOG) {
		(void) printf("\tex_log_buffer = %s\n",
		    (ep->ex_log_buffer ? ep->ex_log_buffer : "(NULL)"));
		(void) printf("\tex_tag = %s\n",
		    (ep->ex_tag ? ep->ex_tag : "(NULL)"));
	}
	(void) printf("\tex_anon = %d\n", ep->ex_anon);
	(void) printf("\tex_seccnt = %d\n", ep->ex_seccnt);
	(void) printf("\n");
	for (i = 0; i < ep->ex_seccnt; i++) {
		sp = &ep->ex_secinfo[i];
		(void) printf("\t\ts_secinfo = %s\n", sp->s_secinfo.sc_name);
		(void) printf("\t\ts_flags: (0x%02x) ", sp->s_flags);
		if (sp->s_flags & M_ROOT) (void) printf("M_ROOT ");
		if (sp->s_flags & M_RO) (void) printf("M_RO ");
		if (sp->s_flags & M_ROL) (void) printf("M_ROL ");
		if (sp->s_flags & M_RW) (void) printf("M_RW ");
		if (sp->s_flags & M_RWL) (void) printf("M_RWL ");
		if (sp->s_flags & M_NONE) (void) printf("M_NONE ");
		if (sp->s_flags == 0) (void) printf("(none)");
		(void) printf("\n");
		(void) printf("\t\ts_window = %d\n", sp->s_window);
		(void) printf("\t\ts_rootid = %d\n", sp->s_rootid);
		(void) printf("\t\ts_rootcnt = %d ", sp->s_rootcnt);
		(void) fflush(stdout);
		for (j = 0; j < sp->s_rootcnt; j++)
			(void) printf("%s ", sp->s_rootnames[j] ?
			    sp->s_rootnames[j] : "<null>");
		(void) printf("\n\n");
	}
}

static int
nfs_build_exportdata(nvlist_t *share, struct exportdata *exportp)
{
	char *sh_path;
	nvlist_t *props;
	int ret;

	sh_path = sa_share_get_path(share);
	if (sh_path == NULL) {
		salog_error(SA_NO_SHARE_PATH,
		    "sa_nfs_share_publish");
		return (SA_NO_SHARE_PATH);
	}

	props = sa_share_get_proto(share, SA_PROT_NFS);
	if (props == NULL) {
		salog_error(SA_NO_SHARE_PROTO,
		    "sa_nfs_share_publish");
		return (SA_NO_SHARE_PROTO);
	}

	/*
	 * walk through the options and fill in the structure
	 * appropriately.
	 */

	(void) memset(exportp, '\0', sizeof (struct exportdata));

	/*
	 * do non-security options first since there is only one after
	 * the derived group is constructed.
	 */
	exportp->ex_version = EX_CURRENT_VERSION;
	exportp->ex_anon = UID_NOBODY; /* this is our default value */
	exportp->ex_index = NULL;
	exportp->ex_path = sh_path;
	exportp->ex_pathlen = strlen(sh_path) + 1;

	ret = sa_nfs_fill_export(sh_path, props, exportp);
	printarg(sh_path, exportp);

	return (ret);
}

/*
 * nfs_sharefs_one
 *
 * Send a single share to sharefs system call.
 *
 * Returns 0 on success, libshare_error on failure
 *
 * sharefs returns -1 on failure with return value set in errno.
 * If sharefs returns success (0), then return value will be
 * obtained from share_status, which will either be 0 (success) or
 * an errno value.
 */
static int
nfs_sharefs_one(char *path, struct exportdata *ep, struct exp_share *sh)
{
	struct exportfs_args ea;
	int err, rc;
	int share_status;

	(void) memset(&ea, 0, sizeof (ea));
	ea.dname = path;
	ea.uex = ep;
	ea.shareinfo = sh;
	ea.ea_statusp = &share_status;
	ea.next_eargs = NULL;

	rc = sharefs(SHAREFS_NFS,
	    (ep == NULL ? SHAREFS_UNPUBLISH : SHAREFS_PUBLISH),
	    &ea, EXPORTFS_DEPENDENT);

	if (rc == 0)
		err = share_status;
	else
		err = errno;

	switch (err) {
	case 0:
		return (SA_OK);
	case EBUSY:
		return (SA_SHARE_OTHERZONE);
	case EEXIST:
		return (SA_DUPLICATE_NAME);
	case ENOENT:
		return (SA_PATH_NOT_FOUND);
	case EPERM:
		return (SA_NO_PERMISSION);
	case EINVAL:
		return (SA_OK);
	case ENOMEM:
		return (SA_NO_MEMORY);
	default:
		return (SA_SYSTEM_ERR);
	}
}

/*
 * sa_nfs_share_publish(share, wait)
 *
 * Convert the share nvlist into an exportdata structure then issue
 * the exportfs call.
 */
static int
sa_nfs_share_publish(nvlist_t *share, int wait)
{
	NOTE(ARGUNUSED(wait))
	struct exportdata export;
	char *sh_name;
	char *sh_path;
	struct exp_share sh;
	int rc;
	boolean_t logging = B_FALSE;

	sh_name = sa_share_get_name(share);
	if (sh_name == NULL) {
		rc = SA_NO_SHARE_NAME;
		salog_error(rc, "sa_nfs_share_publish");
		return (rc);
	}

	sh_path = sa_share_get_path(share);
	if (sh_path == NULL) {
		rc = SA_NO_SHARE_PATH;
		salog_error(rc, "sa_nfs_share_publish");
		return (rc);
	}

	sa_tracef("nfs_share_publish_start: %s:%s:%d",
	    sh_name, sh_path, wait);

	if ((rc = nfs_build_exportdata(share, &export)) != SA_OK) {
		sa_tracef("nfs_share_publish_stop: %s: %s",
		    sh_name, sa_strerror(rc));
		return (rc);
	}

	/*
	 * Fill in share structure and send to the kernel.
	 */
	if ((rc = sa_fillshare(share, &sh, B_TRUE)) != SA_OK) {
		salog_error(rc, "sa_nfs_share_publish");
		goto out;
	}

	if (export.ex_flags & EX_LOG)
		logging = B_TRUE;

	rc = nfs_sharefs_one(sh_path, &export, &sh);
	sa_emptyshare(&sh);

	if (rc != SA_OK) {
		salog_error(rc, "NFS: %s:%s: "
		    "error sharing filesystem",
		    sh_name, sh_path);
	} else {
		/*
		 * check to see if logging and other services need to
		 * be triggered, but only if there wasn't an
		 * error.
		 */
		if (export.ex_flags & EX_LOG) {
			/* enable logging */
			if (nfslogtab_add(sh_path, export.ex_log_buffer,
			    export.ex_tag) != 0) {
				salog_error(SA_SYSTEM_ERR, dgettext(TEXT_DOMAIN,
				    "Could not enable logging for %s\n"),
				    sh_path);
			}
		} else {
			/*
			 * don't have logging so remove it from file. It might
			 * not be there, but that doesn't matter.
			 */
			(void) nfslogtab_remove(sh_path);
		}
		nfs_check_services(logging);
	}

out:
	cleanup_export(&export);

	sa_tracef("nfs_share_publish_stop: %s: %s",
	    sh_name, sa_strerror(rc));

	return (rc);
}

static int
nfs_share_unpub_common(const char *sh_name, const char *sh_path, int wait)
{
	int rc;
	struct exp_share sh;

	sa_tracef("nfs_share_unpublish_start: %s:%s:%d",
	    sh_name, sh_path, wait);

	(void) memset(&sh, '\0', sizeof (sh));
	sh.name = (char *)sh_name;
	sh.name_len = strlen(sh_name);

	rc = nfs_sharefs_one((char *)sh_path, NULL, &sh);
	if (rc != SA_OK) {
		salog_error(rc, "NFS: %s:%s: "
		    "error unsharing filesystem",
		    sh_name, sh_path);
	} else {
		/* just in case it was logged */
		(void) nfslogtab_remove(sh_path);
	}

	sa_tracef("nfs_share_unpublish_stop: %s: %s",
	    sh_name, sa_strerror(rc));

	return (rc);
}

static int
sa_nfs_share_unpublish(nvlist_t *share, int wait)
{
	NOTE(ARGUNUSED(wait))
	char *sh_path;
	char *sh_name;

	sh_name = sa_share_get_name(share);
	if (sh_name == NULL) {
		salog_error(SA_NO_SHARE_NAME,
		    "sa_nfs_share_unpublish");
		return (SA_NO_SHARE_NAME);
	}

	if ((sh_path = sa_share_get_path(share)) == NULL) {
		salog_error(SA_NO_SHARE_PATH,
		    "sa_nfs_share_unpublish: %s", sh_name);
		return (SA_NO_SHARE_PATH);
	}

	return (nfs_share_unpub_common(sh_name, sh_path, wait));

}

static int
sa_nfs_share_unpublish_byname(const char *sh_name, const char *sh_path,
    int wait)
{
	return (nfs_share_unpub_common(sh_name, sh_path, wait));
}

static void
cleanup_export_arg(struct exportfs_args *eap)
{
	free(eap->ea_statusp);
	if (eap->uex != NULL) {
		cleanup_export(eap->uex);
		free(eap->uex);
	}
	if (eap->shareinfo != NULL) {
		sa_emptyshare(eap->shareinfo);
		free(eap->shareinfo);
	}
	free(eap);
}

static void
cleanup_export_list(struct exportfs_args *arglist)
{
	struct exportfs_args *next;

	while (arglist != NULL) {
		next = arglist->next_eargs;
		cleanup_export_arg(arglist);
		arglist = next;
	}
}

static int
create_export_list(nvlist_t *sh_list, struct exportfs_args **list,
    boolean_t publish, boolean_t *logging)
{
	nvpair_t *nvp;
	nvlist_t *share;
	char *sh_name;
	char *sh_path;
	int ret = SA_OK;
	struct exportfs_args *cureap = NULL, *preveap = NULL;

	*list = NULL;
	*logging = B_FALSE;

	for (nvp = nvlist_next_nvpair(sh_list, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(sh_list, nvp)) {

		if (nvpair_type(nvp) != DATA_TYPE_NVLIST)
			continue;

		/*
		 * retrieve the share nvlist from nvpair
		 */
		if (nvpair_value_nvlist(nvp, &share) != 0) {
			ret = SA_NO_MEMORY;
			goto err;
		}

		cureap = calloc(1, sizeof (struct exportfs_args));
		if (cureap == NULL) {
			ret = SA_NO_MEMORY;
			goto err;
		}

		cureap->ea_statusp = calloc(1, sizeof (int));
		if (cureap->ea_statusp == NULL) {
			cleanup_export_arg(cureap);
			ret = SA_NO_MEMORY;
			goto err;
		}

		cureap->dname = sa_share_get_path(share);

		cureap->shareinfo = (struct exp_share *)
		    calloc(1, sizeof (struct exp_share));
		if (cureap->shareinfo == NULL) {
			cleanup_export_arg(cureap);
			ret = SA_NO_MEMORY;
			goto err;
		}

		if (publish) {
			cureap->uex = calloc(1, sizeof (struct exportdata));
			if (cureap->uex == NULL) {
				cleanup_export_arg(cureap);
				ret = SA_NO_MEMORY;
				goto err;
			}

			ret = nfs_build_exportdata(share, cureap->uex);
			if (ret != SA_OK) {
				cleanup_export_arg(cureap);
				if (ret == SA_NO_MEMORY) {
					goto err;
				} else {
					sh_path = sa_share_get_path(share);
					sh_name = sa_share_get_name(share);
					salog_error(ret,
					    "NFS: share not published: %s:%s",
					    sh_name ? sh_name : "",
					    sh_path ? sh_path : "");
					ret = SA_OK;
					continue;
				}
			}
			if (cureap->uex->ex_flags & EX_LOG)
				*logging = B_TRUE;
		}

		ret = sa_fillshare(share, cureap->shareinfo, publish);
		if (ret != SA_OK) {
			cleanup_export_arg(cureap);
			if (ret == SA_NO_MEMORY) {
				goto err;
			} else {
				sh_path = sa_share_get_path(share);
				sh_name = sa_share_get_name(share);
				salog_error(ret,
				    "NFS: share not published: %s:%s",
				    sh_name ? sh_name : "",
				    sh_path ? sh_path : "");
				ret = SA_OK;
				continue;
			}
		}

		/*
		 * add to list
		 */
		if (*list == NULL)
			*list = cureap;

		if (preveap != NULL)
			preveap->next_eargs = cureap;
		preveap = cureap;

	}
	return (ret);

err:
	cleanup_export_list(*list);
	*list = NULL;
	return (ret);
}

/*
 * nfs_check_shares()
 *
 * if any one share succeeded, return true
 */
static boolean_t
nfs_check_shares(struct exportfs_args *list)
{
	while (list != NULL) {
		if (*list->ea_statusp == 0)
			return (B_TRUE);
		list = list->next_eargs;
	}
	return (B_FALSE);
}

/*
 * nfs_check_logging
 *
 * Looks for EX_LOG being set and enables logging. Since this could be
 * called as a result of changing the properties which are then
 * inherited, we have to assume that logging was on for any share that
 * doesn't have EX_LOG set and disable it.
 */
static void
nfs_check_logging(struct exportfs_args *list)
{
	while (list != NULL) {
		if (list->uex->ex_flags & EX_LOG)
			(void) nfslogtab_add(list->dname,
			    list->uex->ex_log_buffer, list->uex->ex_tag);
		else
			(void) nfslogtab_remove(list->dname);
		list = list->next_eargs;
	}
}

static int
sa_nfs_share_publish_admin(const char *mntpnt)
{
	NOTE(ARGUNUSED(mntpnt))

	return (SA_OK);
}

static int
sa_nfs_fs_publish(nvlist_t *sh_list, int wait)
{
	int ret = SA_OK;
	struct exportfs_args *list;
	boolean_t logging;

	sa_tracef("nfs_fs_publish_start: %d", wait);

	ret = create_export_list(sh_list, &list, B_TRUE, &logging);
	if (ret != 0)
		goto err;

	ret = sharefs(SHAREFS_NFS, SHAREFS_PUBLISH, list, EXPORTFS_DEPENDENT);
	if (ret != 0) {
		switch (errno) {
		case EBUSY:
			ret = SA_SHARE_OTHERZONE;
			break;
		case EPERM:
			ret = SA_NO_PERMISSION;
			break;
		default:
			ret = SA_SYSTEM_ERR;
			break;
		}
	}
	nfs_check_logging(list);
	if (nfs_check_shares(list))
		nfs_check_services(logging);


err:
	sa_tracef("nfs_fs_publish_stop: %s", sa_strerror(ret));
	nvlist_free(sh_list);
	cleanup_export_list(list);
	if (ret != SA_OK)
		salog_error(ret, "NFS: error sharing filesystem");

	return (ret);
}

static int
sa_nfs_fs_unpublish(nvlist_t *sh_list, int wait)
{
	int ret;
	struct exportfs_args *list;
	boolean_t logging = B_FALSE;

	sa_tracef("nfs_fs_unpublish_start: %d", wait);

	ret = create_export_list(sh_list, &list, B_FALSE, &logging);
	if (ret != 0)
		goto done;

	ret = sharefs(SHAREFS_NFS, SHAREFS_UNPUBLISH, list,
	    EXPORTFS_DEPENDENT);

	if (ret != 0) {
		switch (errno) {
		case EEXIST:
			ret = SA_DUPLICATE_NAME;
			break;
		case ENOENT:
			ret = SA_SHARE_NOT_FOUND;
			break;
		case EPERM:
			ret = SA_NO_PERMISSION;
			break;
		default:
			ret = SA_SYSTEM_ERR;
			break;
		}
	} else {
		ret = SA_OK;
	}

	cleanup_export_list(list);
done:
	nvlist_free(sh_list);
	sa_tracef("nfs_fs_unpublish_stop: %s", sa_strerror(ret));

	if (ret != SA_OK)
		salog_error(ret, "NFS: error unsharing filesystem");

	return (ret);
}

/*
 * Protocol management functions
 *
 * Properties defined in the default files are defined in
 * proto_option_defs for parsing and validation. If "other" and
 * "compare" are set, then the value for this property should be
 * compared against the property specified in "other" using the
 * "compare" check (either <= or >=) in order to ensure that the
 * values are in the correct range.  E.g. setting server_versmin
 * higher than server_versmax should not be allowed.
 */

struct proto_option_defs {
	char *tag;
	char *name;	/* display name -- remove protocol identifier */
	int index;
	int type;
	union {
	    int intval;
	    char *string;
	} defvalue;
	uint32_t svcs;
	int32_t minval;
	int32_t maxval;
	char *other;
	int compare;
#define	OPT_CMP_GE	0
#define	OPT_CMP_LE	1
	int (*check)(char *);
} proto_options[] = {
#define	PROTO_OPT_NFSD_SERVERS			0
	{"nfsd_servers",
	    "servers", PROTO_OPT_NFSD_SERVERS, OPT_TYPE_NUMBER, 16, SVC_NFSD,
	    1, INT32_MAX },
#define	PROTO_OPT_LOCKD_LISTEN_BACKLOG		1
	{"lockd_listen_backlog",
	    "lockd_listen_backlog", PROTO_OPT_LOCKD_LISTEN_BACKLOG,
	    OPT_TYPE_NUMBER, 32, SVC_LOCKD, 32, INT32_MAX },
#define	PROTO_OPT_LOCKD_SERVERS			2
	{"lockd_servers",
	    "lockd_servers", PROTO_OPT_LOCKD_SERVERS, OPT_TYPE_NUMBER, 20,
	    SVC_LOCKD, 1, INT32_MAX },
#define	PROTO_OPT_LOCKD_RETRANSMIT_TIMEOUT	3
	{"lockd_retransmit_timeout",
	    "lockd_retransmit_timeout", PROTO_OPT_LOCKD_RETRANSMIT_TIMEOUT,
	    OPT_TYPE_NUMBER, 5, SVC_LOCKD, 0, INT32_MAX },
#define	PROTO_OPT_GRACE_PERIOD			4
	{"grace_period",
	    "grace_period", PROTO_OPT_GRACE_PERIOD, OPT_TYPE_NUMBER, 90,
	    SVC_LOCKD, 0, INT32_MAX },
#define	PROTO_OPT_NFS_SERVER_VERSMIN		5
	{"nfs_server_versmin",
	    "server_versmin", PROTO_OPT_NFS_SERVER_VERSMIN, OPT_TYPE_NUMBER,
	    (int)NFS_VERSMIN_DEFAULT, SVC_NFSD|SVC_MOUNTD, NFS_VERSMIN,
	    NFS_VERSMAX, "server_versmax", OPT_CMP_LE},
#define	PROTO_OPT_NFS_SERVER_VERSMAX		6
	{"nfs_server_versmax",
	    "server_versmax", PROTO_OPT_NFS_SERVER_VERSMAX, OPT_TYPE_NUMBER,
	    (int)NFS_VERSMAX_DEFAULT, SVC_NFSD|SVC_MOUNTD, NFS_VERSMIN,
	    NFS_VERSMAX, "server_versmin", OPT_CMP_GE},
#define	PROTO_OPT_NFS_CLIENT_VERSMIN		7
	{"nfs_client_versmin",
	    "client_versmin", PROTO_OPT_NFS_CLIENT_VERSMIN, OPT_TYPE_NUMBER,
	    (int)NFS_VERSMIN_DEFAULT, SVC_CLIENT, NFS_VERSMIN, NFS_VERSMAX,
	    "client_versmax", OPT_CMP_LE},
#define	PROTO_OPT_NFS_CLIENT_VERSMAX		8
	{"nfs_client_versmax",
	    "client_versmax", PROTO_OPT_NFS_CLIENT_VERSMAX, OPT_TYPE_NUMBER,
	    (int)NFS_VERSMAX_DEFAULT, SVC_CLIENT, NFS_VERSMIN, NFS_VERSMAX,
	    "client_versmin", OPT_CMP_GE},
#define	PROTO_OPT_NFS_SERVER_DELEGATION		9
	{"nfs_server_delegation",
	    "server_delegation", PROTO_OPT_NFS_SERVER_DELEGATION,
	    OPT_TYPE_ONOFF, NFS_SERVER_DELEGATION_DEFAULT, SVC_NFSD, 0, 0 },
#define	PROTO_OPT_NFSMAPID_DOMAIN		10
	{"nfsmapid_domain",
	    "nfsmapid_domain", PROTO_OPT_NFSMAPID_DOMAIN, OPT_TYPE_DOMAIN,
	    NULL, SVC_NFSMAPID, 0, 0 },
#define	PROTO_OPT_NFSD_MAX_CONNECTIONS		11
	{"nfsd_max_connections",
	    "max_connections", PROTO_OPT_NFSD_MAX_CONNECTIONS,
	    OPT_TYPE_NUMBER, -1, SVC_NFSD, -1, INT32_MAX },
#define	PROTO_OPT_NFSD_PROTOCOL			12
	{"nfsd_protocol",
	    "protocol", PROTO_OPT_NFSD_PROTOCOL, OPT_TYPE_PROTOCOL, 0,
	    SVC_NFSD, 0, 0 },
#define	PROTO_OPT_NFSD_LISTEN_BACKLOG		13
	{"nfsd_listen_backlog",
	    "listen_backlog", PROTO_OPT_NFSD_LISTEN_BACKLOG,
	    OPT_TYPE_NUMBER, 0,
	    SVC_NFSD, 0, INT32_MAX },
#define	PROTO_OPT_NFSD_DEVICE			14
	{"nfsd_device",
	    "device", PROTO_OPT_NFSD_DEVICE,
	    OPT_TYPE_STRING, NULL, SVC_NFSD, 0, 0},
	{NULL}
};

#define	NFS_OPT_MAX	PROTO_OPT_NFSD_DEVICE

typedef struct def_file {
	struct def_file *next;
	char *line;
} def_file_t;

/*
 * service_in_state(service, chkstate)
 *
 * Want to know if the specified service is in the desired state
 * (chkstate) or not. Return true (1) if it is and false (0) if it
 * isn't.
 */
static boolean_t
service_in_state(char *service, const char *chkstate)
{
	char *state;
	int ret = B_FALSE;

	state = smf_get_state(service);
	if (state != NULL) {
		/* got the state so get the equality for the return value */
		ret = strcmp(state, chkstate) == 0 ? B_TRUE : B_FALSE;
		free(state);
	}
	return (ret);
}

/*
 * restart_service(svcs)
 *
 * Walk through the bit mask of services that need to be restarted in
 * order to use the new property values. Some properties affect
 * multiple daemons. Should only restart a service if it is currently
 * enabled (online).
 */
static void
restart_service(uint32_t svcs)
{
	uint32_t mask;
	int ret;
	char *service;

	for (mask = 1; svcs != 0; mask <<= 1) {
		switch (svcs & mask) {
		case SVC_LOCKD:
			service = LOCKD;
			break;
		case SVC_STATD:
			service = STATD;
			break;
		case SVC_NFSD:
			service = NFSD;
			break;
		case SVC_MOUNTD:
			service = MOUNTD;
			break;
		case SVC_NFS4CBD:
			service = NFS4CBD;
			break;
		case SVC_NFSMAPID:
			service = NFSMAPID;
			break;
		case SVC_RQUOTAD:
			service = RQUOTAD;
			break;
		case SVC_NFSLOGD:
			service = NFSLOGD;
			break;
		case SVC_REPARSED:
			service = REPARSED;
			break;
		case SVC_CLIENT:
			service = NFS_CLIENT_SVC;
			break;
		default:
			continue;
		}

		/*
		 * Only attempt to restart the service if it is
		 * currently running. In the future, it may be
		 * desirable to use smf_refresh_instance if the NFS
		 * services ever implement the refresh method.
		 */
		if (service_in_state(service, SCF_STATE_STRING_ONLINE)) {
			ret = smf_restart_instance(service);
			/*
			 * There are only a few SMF errors at this point, but
			 * it is also possible that a bad value may have put
			 * the service into maintenance if there wasn't an
			 * SMF level error.
			 */
			if (ret != 0) {
				(void) fprintf(stderr,
				    dgettext(TEXT_DOMAIN,
				    "%s failed to restart: %s\n"),
				    service, scf_strerror(scf_error()));
			} else {
				/*
				 * Check whether it has gone to "maintenance"
				 * mode or not. Maintenance implies something
				 * went wrong.
				 */
				if (service_in_state(service,
				    SCF_STATE_STRING_MAINT)) {
					(void) fprintf(stderr,
					    dgettext(TEXT_DOMAIN,
					    "%s failed to restart\n"),
					    service);
				}
			}
		}
		svcs &= ~mask;
	}
}

/*
 * nfs_minmax_check(name, value)
 *
 * Verify that the value for the property specified by index is valid
 * relative to the opposite value in the case of a min/max variable.
 * Currently, server_minvers/server_maxvers and
 * client_minvers/client_maxvers are the only ones to check.
 */
static boolean_t
nfs_minmax_check(int index, int value)
{
	char *propval;
	int val;
	int ret = B_TRUE;

	if (proto_options[index].other != NULL) {
		/* have a property to compare against */
		if (sa_nfs_proto_get_property(NULL, proto_options[index].other,
		    &propval) == SA_OK) {
			val = strtoul(propval, NULL, 0);
			if (proto_options[index].compare == OPT_CMP_LE) {
				ret = value <= val ? B_TRUE : B_FALSE;
			} else if (proto_options[index].compare == OPT_CMP_GE) {
				ret = value >= val ? B_TRUE : B_FALSE;
			}
			free(propval);
		}
	}

	return (ret);
}

static int
findprotoopt(const char *name, int whichname)
{
	int i;
	for (i = 0; proto_options[i].tag != NULL; i++) {
		if (whichname == 1) {
			if (strcasecmp(proto_options[i].name, name) == 0)
			return (i);
		} else {
			if (strcasecmp(proto_options[i].tag, name) == 0)
				return (i);
		}
	}
	return (-1);
}

/*
 * fixcaselower(str)
 *
 * convert a string to lower case (inplace).
 */
static void
fixcaselower(char *str)
{
	while (*str) {
		*str = tolower(*str);
		str++;
	}
}

/*
 * skipwhitespace(str)
 *
 * Skip leading white space. It is assumed that it is called with a
 * valid pointer.
 */
static char *
skipwhitespace(char *str)
{
	while (*str && isspace(*str))
		str++;

	return (str);
}

/*
 * nfs_validate_proto_prop(index, name, value)
 *
 * Verify that the property specified by name can take the new
 * value. This is a sanity check to prevent bad values getting into
 * the default files. All values need to be checked against what is
 * allowed by their defined type. If a type isn't explicitly defined
 * here, it is treated as a string.
 *
 * Note that OPT_TYPE_NUMBER will additionally check that the value is
 * within the range specified and potentially against another property
 * value as well as specified in the proto_options members other and
 * compare.
 */
static int
sanfs_validate_proto_prop(int index, const char *value)
{
	int ret = SA_OK;
	char *cp;

	switch (proto_options[index].type) {
	case OPT_TYPE_NUMBER:
		if (!is_a_number(value)) {
			ret = SA_INVALID_PROP_VAL;
		} else {
			int val;
			val = strtoul(value, NULL, 0);
			if (val < proto_options[index].minval ||
			    val > proto_options[index].maxval)
				ret = SA_INVALID_PROP_VAL;
			/*
			 * For server_versmin/server_versmax and
			 * client_versmin/client_versmax, the
			 * value of the min(max) should be checked
			 * to be correct relative to the current
			 * max(min).
			 */
			if (!nfs_minmax_check(index, val))
				ret = SA_INVALID_PROP_VAL;
		}
		break;

	case OPT_TYPE_DOMAIN:
		/*
		 * needs to be a qualified domain so will have at
		 * least one period and other characters on either
		 * side of it.  A zero length string is also allowed
		 * and is the way to turn off the override.
		 */
		if (strlen(value) == 0)
			break;
		cp = strchr(value, '.');
		if (cp == NULL || cp == value || strchr(value, '@') != NULL)
			ret = SA_INVALID_PROP_VAL;
		break;

	case OPT_TYPE_BOOLEAN:
		if (strlen(value) == 0 ||
		    strcasecmp(value, "true") == 0 ||
		    strcmp(value, "1") == 0 ||
		    strcasecmp(value, "false") == 0 ||
		    strcmp(value, "0") == 0) {
			ret = SA_OK;
		} else {
			ret = SA_INVALID_PROP_VAL;
		}
		break;

	case OPT_TYPE_ONOFF:
		if (strcasecmp(value, "on") != 0 &&
		    strcasecmp(value, "off") != 0) {
			ret = SA_INVALID_PROP_VAL;
		}
		break;

	case OPT_TYPE_PROTOCOL:
		if (strlen(value) != 0 &&
		    strcasecmp(value, "all") != 0 &&
		    strcasecmp(value, "tcp") != 0 &&
		    strcasecmp(value, "udp") != 0)
			ret = SA_INVALID_PROP_VAL;
		break;

	default:
		/* treat as a string */
		break;
	}
	return (ret);
}

/*
 * extractprop()
 *
 * Extract the property and value out of the line and add
 * the name/value pair to the proplist (nfs_proto_proplist)
 */
static int
extractprop(char *name, char *value)
{
	int index;
	int ret = SA_OK;

	/*
	 * Remove any leading white spaces.
	 */
	name = skipwhitespace(name);

	index = findprotoopt(name, 1);
	if (index >= 0) {
		fixcaselower(name);
		if (nvlist_add_string(nfs_proto_proplist,
		    proto_options[index].name, value) != 0)
			ret = SA_NO_MEMORY;
	}
	return (ret);
}

static scf_type_t
getscftype(int type)
{
	scf_type_t ret;

	switch (type) {
	case OPT_TYPE_NUMBER:
		ret = SCF_TYPE_INTEGER;
	break;
	case OPT_TYPE_ONOFF:
	case OPT_TYPE_BOOLEAN:
		ret = SCF_TYPE_BOOLEAN;
	break;
	default:
		ret = SCF_TYPE_ASTRING;
	}
	return (ret);
}

static char *
getsvcname(uint32_t svcs)
{
	char *service;
	switch (svcs) {
		case SVC_LOCKD:
			service = LOCKD;
			break;
		case SVC_STATD:
			service = STATD;
			break;
		case SVC_NFSD:
			service = NFSD;
			break;
		case SVC_CLIENT:
			service = NFS_CLIENT_SVC;
			break;
		case SVC_NFS4CBD:
			service = NFS4CBD;
			break;
		case SVC_NFSMAPID:
			service = NFSMAPID;
			break;
		case SVC_RQUOTAD:
			service = RQUOTAD;
			break;
		case SVC_NFSLOGD:
			service = NFSLOGD;
			break;
		case SVC_REPARSED:
			service = REPARSED;
			break;
		default:
			service = NFSD;
	}
	return (service);
}

/*
 * add_default_proto_prop
 *
 * Add the default values for any property not defined in the parsing
 * of the default files. Values are set according to their defined
 * types.
 */
static int
add_default_proto_prop(int index)
{
	char value[MAXDIGITS];
	int ret;

	if (index < 0 || index > NFS_OPT_MAX)
		return (SA_INVALID_PROP);

	switch (proto_options[index].type) {
	case OPT_TYPE_NUMBER:
		(void) snprintf(value, sizeof (value), "%d",
		    proto_options[index].defvalue.intval);
		ret = nvlist_add_string(nfs_proto_proplist,
		    proto_options[index].name, value);
		break;

	case OPT_TYPE_ONOFF:
	case OPT_TYPE_BOOLEAN:
		ret = nvlist_add_string(nfs_proto_proplist,
		    proto_options[index].name,
		    proto_options[index].defvalue.intval ?
		    "on" : "off");
		break;

	default:
		/* treat as strings of zero length */
		ret = nvlist_add_string(nfs_proto_proplist,
		    proto_options[index].name, "");
		break;
	}

	return ((ret == 0) ? SA_OK : SA_NO_MEMORY);
}

static void
sanfs_free_proto_proplist(void)
{
	if (nfs_proto_proplist != NULL) {
		nvlist_free(nfs_proto_proplist);
		nfs_proto_proplist = NULL;
	}
}

/*
 * sanfs_init_proto_proplist
 *
 * Read NFS SMF properties and add the defined values to the
 * proplist.  Note that default values are known from the built in
 * table in case SMF doesn't have a definition. Not having
 * SMF properties is OK since we have builtin default
 * values.
 */
static int
sanfs_init_proto_proplist(void)
{
	int ret = SA_OK;
	char name[PATH_MAX];
	char value[PATH_MAX];
	scf_type_t sctype;
	char *svc_name;
	int bufsz = 0, i;

	sanfs_free_proto_proplist();

	if (nvlist_alloc(&nfs_proto_proplist, NV_UNIQUE_NAME, 0) != 0)
		return (SA_NO_MEMORY);

	for (i = 0; proto_options[i].tag != NULL; i++) {
		bzero(value, PATH_MAX);
		(void) strncpy(name, proto_options[i].name, PATH_MAX);
		/* Replace NULL with the correct instance */
		sctype = getscftype(proto_options[i].type);
		svc_name = getsvcname(proto_options[i].svcs);
		bufsz = PATH_MAX;
		ret = nfs_smf_get_prop(name, value,
		    (char *)DEFAULT_INSTANCE, sctype,
		    svc_name, &bufsz);

		if (ret == 0) {
			/*
			 * Special case for server_delegation as the
			 * values it takes are "on/off" and boolean
			 * translates to true/false. Convert true/false
			 * to on/off values here
			 */
			if (strcasecmp(name, "server_delegation") == 0) {
				if (value != NULL &&
				    (strcasecmp(value, "true") == 0))
					(void) strcpy(value, "on");
				else
					(void) strcpy(value, "off");
			}
			/* add property to list */
			ret = extractprop(name, value);
		} else {
			/* add default value to list */
			ret = add_default_proto_prop(i);
		}

		if (ret != SA_OK) {
			sanfs_free_proto_proplist();
			break;
		}
	}

	return (ret);
}

/*
 * sa_nfs_get_proto_features
 *
 * Return a mask of the features required.
 */
static int
sa_nfs_proto_get_features(uint64_t *features)
{
	*features = (SA_FEATURE_DFSTAB | SA_FEATURE_SERVER);
	return (SA_OK);
}

static int
sa_nfs_proto_get_proplist(nvlist_t **proplist)
{
	if (nfs_proto_proplist == NULL) {
		if (sanfs_init_proto_proplist() != SA_OK) {
			*proplist = NULL;
			return (SA_NO_MEMORY);
		}
	}

	if (nvlist_dup(nfs_proto_proplist, proplist, 0) == 0)
		return (SA_OK);
	else
		return (SA_NO_MEMORY);
}

/*
 * sa_nfs_proto_get_status()
 *
 * What is the current status of the nfsd? We use the SMF state here.
 * Caller must free the returned value.
 */
static int
sa_nfs_proto_get_status(char **status_str)
{
	*status_str = smf_get_state(NFSD);
	if (*status_str != NULL)
		return (SA_OK);
	else
		return (SA_NO_MEMORY);
}

static int
sa_nfs_proto_get_property(const char *sectname, const char *propname,
    char **propval)
{
	NOTE(ARGUNUSED(sectname))
	char *val;

	if (nfs_proto_proplist == NULL) {
		if (sanfs_init_proto_proplist() != SA_OK) {
			*propval = NULL;
			return (SA_NO_MEMORY);
		}
	}

	if (nvlist_lookup_string(nfs_proto_proplist, propname, &val) == 0) {
		if ((*propval = strdup(val)) != NULL)
			return (SA_OK);
		else
			return (SA_NO_MEMORY);
	} else {
		return (SA_NO_SUCH_PROP);
	}
}

static int
sa_nfs_proto_set_property(const char *sectname, const char *propname,
    const char *propval)
{
	NOTE(ARGUNUSED(sectname))
	int index;
	int ret;
	scf_type_t sctype;
	char *svc_name;
	char *value = (char *)propval;

	if (propname == NULL)
		return (SA_INVALID_PROP);

	if (propval == NULL)
		return (SA_INVALID_PROP_VAL);

	if ((index = findprotoopt(propname, 1)) < 0)
		return (SA_INVALID_PROP);

	ret = sanfs_validate_proto_prop(index, propval);
	if (ret == SA_OK) {
		sctype = getscftype(proto_options[index].type);
		svc_name = getsvcname(proto_options[index].svcs);
		if (sctype == SCF_TYPE_BOOLEAN)
			value = (string_to_boolean(propval)) ? "1" : "0";
		ret = nfs_smf_set_prop((char *)propname, value,
		    NULL, sctype, svc_name);

		if (ret == 0) {
			restart_service(proto_options[index].svcs);
			(void) sanfs_init_proto_proplist();
		} else {
			ret = SA_SCF_ERROR;
		}
	}

	return (ret);
}

/*
 * nfs_check_services
 *
 * check to see if the NFS services need to be started. If we've
 * already done this in nfs_svc_delta seconds, don't do it again until
 * either the service list changes or the delta has expired.
 */
static void
nfs_check_services(boolean_t logging)
{
	static char **last_list = NULL;
	time_t last_time = 0;
	char **service_list = NULL;

	service_list = logging ?
	    nfs_service_list_logging : nfs_service_list_default;

	LOCK_NFS_SERVICES();
	if ((char **)service_list != last_list ||
	    (last_time + nfs_svc_delta) < time(NULL)) {
		_check_services(service_list);
		last_list = service_list;
		last_time = time(NULL);
	}
	UNLOCK_NFS_SERVICES();
}

/*
 * Append an entry to the nfslogtab file
 */
static int
nfslogtab_add(char *dir, char *buffer, char *tag)
{
	FILE *f;
	struct logtab_ent lep;
	int error = SA_OK;

	/*
	 * Open the file for update and create it if necessary.
	 * This may leave the I/O offset at the end of the file,
	 * so rewind back to the beginning of the file.
	 */
	f = fopen(NFSLOGTAB, "a+");
	if (f == NULL) {
		if (errno == EPERM)
			error = SA_NO_PERMISSION;
		else
			error = SA_SYSTEM_ERR;
		goto out;
	}
	rewind(f);

	if (lockf(fileno(f), F_LOCK, 0L) < 0) {
		salog_error(SA_SYSTEM_ERR,
		    "nfslog: failed to lock %s "
		    "for update: %s\n", NFSLOGTAB, strerror(errno));
		error = SA_SYSTEM_ERR;
		goto out;
	}

	if (logtab_deactivate_after_boot(f) == -1) {
		salog_error(SA_SYSTEM_ERR,
		    "nfslog: could not deactivate "
		    "entries in %s\n", NFSLOGTAB);
		error = -1;
		goto out;
	}

	/*
	 * Remove entries matching buffer and sharepoint since we're
	 * going to replace it with perhaps an entry with a new tag.
	 */
	if (logtab_rement(f, buffer, dir, NULL, -1)) {
		salog_error(SA_SYSTEM_ERR,
		    "nfslog: could not remove matching "
		    "entries in %s\n", NFSLOGTAB);
		error = SA_SYSTEM_ERR;
		goto out;
	}

	/*
	 * Deactivate all active entries matching this sharepoint
	 */
	if (logtab_deactivate(f, NULL, dir, NULL)) {
		salog_error(SA_SYSTEM_ERR,
		    "nfslog: could not deactivate matching "
		    "entries in %s\n", NFSLOGTAB);
		error = SA_SYSTEM_ERR;
		goto out;
	}

	lep.le_buffer = buffer;
	lep.le_path = dir;
	lep.le_tag = tag;
	lep.le_state = LES_ACTIVE;

	/*
	 * Add new sharepoint / buffer location to nfslogtab
	 */
	if (logtab_putent(f, &lep) < 0) {
		salog_error(SA_SYSTEM_ERR,
		    "nfslog: could not add %s to %s\n",
		    dir, NFSLOGTAB);
		error = SA_SYSTEM_ERR;
	}

out:
	if (f != NULL)
		(void) fclose(f);

	return (error);
}

/*
 * Deactivate an entry from the nfslogtab file
 */
static int
nfslogtab_remove(const char *path)
{
	FILE *f;
	int error = SA_OK;

	f = fopen(NFSLOGTAB, "r+");
	if (f == NULL) {
		if (errno == EPERM)
			error = SA_NO_PERMISSION;
		goto out;
	}
	if (lockf(fileno(f), F_LOCK, 0L) < 0) {
		salog_error(SA_SYSTEM_ERR,
		    "nfslog: could not lock %s for "
		    "update: %s\n", NFSLOGTAB, strerror(error));
		error = SA_SYSTEM_ERR;
		goto out;
	}
	if (logtab_deactivate(f, NULL, (char *)path, NULL) == -1) {
		error = SA_SYSTEM_ERR;
		salog_error(SA_SYSTEM_ERR,
		    "nfslog: could not deactivate %s in %s\n", path, NFSLOGTAB);
	}

out:
	if (f != NULL)
		(void) fclose(f);

	return (error);
}
