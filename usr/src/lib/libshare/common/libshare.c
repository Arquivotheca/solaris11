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
 * Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <libintl.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <thread.h>
#include <synch.h>
#include <assert.h>
#include <zone.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mnttab.h>
#include <sys/mntent.h>
#include <sys/sdt.h>
#include <sys/param.h>

#include "libshare.h"
#include "libshare_impl.h"

#define	SA_PIDX_INVALID	-1
#define	SA_PIDX_NAME	0
#define	SA_PIDX_PATH	1
#define	SA_PIDX_DESC	2
#define	SA_PIDX_MNTPNT	3
#define	SA_PIDX_TRANS	4

char *sa_global_prop_list[] = {
	SA_PROP_NAME,
	SA_PROP_PATH,
	SA_PROP_DESC,
	SA_PROP_MNTPNT,
	SA_PROP_TRANS,
	NULL
};

/*
 * Define libshare environment properties here.
 */
typedef uint32_t sa_env_flags_t;

#define	SA_ENV_AKD	0x0001

static sa_env_flags_t sa_global_env_flag = 0;
static void sa_set_env_flags(void);

#pragma init(_sa_init)
int
_sa_init(void)
{
	sa_set_env_flags();
	/*
	 * plugins are loaded as they are needed, so no need
	 * to load them here. see libshare_plugin.c
	 */
	return (SA_OK);
}

#pragma fini(_sa_fini)
int
_sa_fini(void)
{
	saplugin_unload_all();
	return (SA_OK);
}

/*
 * sa_set_env_flags
 *
 * Sets the environment flags associated with the share environment.
 */
static void
sa_set_env_flags(void)
{
	struct stat st;

	if (stat("/usr/lib/ak/akd", &st) == 0)
		sa_global_env_flag |= SA_ENV_AKD;
}

/*
 * sa_is_akd_present
 *
 * Returns B_TRUE if akd deamon is present.
 */
boolean_t
sa_is_akd_present(void)
{
	if (sa_global_env_flag & SA_ENV_AKD)
		return (B_TRUE);
	return (B_FALSE);
}

/*
 * sa_share_parse
 *
 * Parse a comma separated list of name=value pairs into an nvlist_t
 * Returns an nvlist of share properties that must be freed by the caller
 *
 * Each share nvlist contains nvpairs for global properties (ie name, path)
 * and an embedded nvlist for each protocol (ie nfs, smb).
 *
 * The protocol plugins are called to parse the protocol specific properties.
 *
 * errors are reported in errbuf.
 */
int
sa_share_parse(const char *propstr, int unset, nvlist_t **share,
    char *errbuf, size_t buflen)
{
	char *share_props = NULL;
	char *prot_props;
	char *namep;
	char *valp;
	char *nextp;
	int prop_idx;
	nvlist_t *share_nvl = NULL;
	nvlist_t *prot_nvl;
	sa_proto_t proto;
	int ret;

	if ((share_nvl = sa_share_alloc(NULL, NULL)) == NULL) {
		ret = SA_NO_MEMORY;
		(void) snprintf(errbuf, buflen, "%s", sa_strerror(ret));
		goto err_out;
	}

	share_props = strdup(propstr);
	if (share_props == NULL) {
		ret = SA_NO_MEMORY;
		(void) snprintf(errbuf, buflen, "%s", sa_strerror(ret));
		goto err_out;
	}

	for (namep = share_props;
	    namep != NULL;
	    namep = nextp) {
		nextp = sa_strchr_escape(namep, ',');
		if (nextp != NULL) {
			*nextp = '\0';
			if (nextp[1] == '\0')
				nextp = NULL;
			else
				nextp++;
		}
		valp = strchr(namep, '=');
		if (valp == NULL) {
			valp = "";
		} else {
			*valp = '\0';
			valp++;
		}

		if (strcasecmp(namep, SA_PROP_PROT) == 0) {

			prot_props = nextp;
			if (prot_props != NULL) {
				if (strncasecmp(prot_props, SA_PROP_PROT"=",
				    strlen(SA_PROP_PROT"=")) == 0) {
					prot_props = NULL;
				} else {
					nextp = strstr(nextp, ","SA_PROP_PROT);
					if (nextp != NULL) {
						*nextp = '\0';
						nextp++;
					}
				}
			}

			proto = sa_val_to_proto(valp);
			if (proto == SA_PROT_NONE) {
				ret = SA_INVALID_PROTO;
				(void) snprintf(errbuf, buflen, "%s: %s",
				    sa_strerror(ret), valp);
				goto err_out;
			}

			ret = saproto_share_parse(proto, prot_props, unset,
			    &prot_nvl, errbuf, buflen);

			if (ret != SA_OK)
				goto err_out;

			/*
			 * only allow for a protocol to be specified once
			 */
			if (sa_share_get_proto(share_nvl, proto) != NULL) {
				ret = SA_DUPLICATE_PROTO;
				(void) snprintf(errbuf, buflen, "%s: %s",
				    sa_strerror(ret), valp);
				goto err_out;
			}

			/*
			 * save the prot_nvl to the share_nvl
			 */
			ret = sa_share_set_proto(share_nvl, proto, prot_nvl);
			if (ret != SA_OK) {
				(void) snprintf(errbuf, buflen, "%s",
				    sa_strerror(ret));
				nvlist_free(prot_nvl);
				goto err_out;
			} else {
				nvlist_free(prot_nvl);
			}
		} else {
			char *stripped;
			char *utf8_val;

			prop_idx = sa_prop_cmp_list(namep, sa_global_prop_list);
			switch (prop_idx) {
			case SA_PIDX_NAME:
			case SA_PIDX_PATH:
			case SA_PIDX_DESC:
				if (valp[0] == '\0') {
					(void) snprintf(errbuf, buflen,
					    dgettext(TEXT_DOMAIN,
					    "syntax error: no value for"
					    " '%s' property"), namep);
					ret = SA_SYNTAX_ERR;
					goto err_out;
				}
				break;
			default:
				(void) snprintf(errbuf, buflen,
				    dgettext(TEXT_DOMAIN,
				    "Invalid global property: %s"),
				    namep);
				ret = SA_INVALID_PROP;
				goto err_out;
			}

			/*
			 * property can only be specified once
			 */
			if (sa_share_get_prop(share_nvl, namep) != NULL) {
				ret = SA_DUPLICATE_PROP;
				(void) snprintf(errbuf, buflen, "%s: %s",
				    sa_strerror(ret), namep);
				goto err_out;
			}

			/*
			 * add the property to the share after
			 * stripping any quotes and converting to utf-8.
			 */
			stripped = sa_strip_escape(valp);
			if (stripped == NULL) {
				(void) snprintf(errbuf, buflen, "%s",
				    sa_strerror(SA_NO_MEMORY));
				goto err_out;

			}

			if (prop_idx == SA_PIDX_NAME ||
			    prop_idx == SA_PIDX_DESC) {
				ret = sa_locale_to_utf8(stripped,
				    &utf8_val);
				if (ret == SA_OK) {
					ret = sa_share_set_prop(share_nvl,
					    namep, utf8_val);
					free(utf8_val);
				}
			} else {
				ret = sa_share_set_prop(share_nvl, namep,
				    stripped);
			}
			free(stripped);

			if (ret != SA_OK) {
				(void) snprintf(errbuf, buflen, "%s",
				    sa_strerror(ret));
				goto err_out;
			}
		}
	}

	free(share_props);
	*share = share_nvl;

	return (SA_OK);

err_out:
	if (share_nvl != NULL)
		sa_share_free(share_nvl);
	if (share_props != NULL)
		free(share_props);
	return (ret);
}

/*
 * sa_prop_emtpy_list
 *
 * return B_TRUE if nvlist has no nvpairs.
 */
boolean_t
sa_prop_empty_list(nvlist_t *nvl)
{
	return (nvlist_empty(nvl));
}

/*
 * sa_share_merge_prot
 *
 * Merge the protocol nvlist (contained in nvp) into the
 * share nvlist.
 *
 * INPUTS:
 *   nvp       - nvpair that contains the protocol nvlist
 *   share_nvl - destination share nvlist
 */
static int
sa_share_merge_prot(nvpair_t *nvp, nvlist_t *share_nvl, int unset,
    char *errbuf, size_t buflen)
{
	int rc;
	char *prot_name;
	sa_proto_t proto;
	nvlist_t *src_prot_nvl;
	nvlist_t *dst_prot_nvl;

	prot_name = nvpair_name(nvp);
	proto = sa_val_to_proto(prot_name);

	/*
	 * retrieve the protocol nvlist from nvpair
	 */
	if (nvpair_value_nvlist(nvp, &src_prot_nvl) != 0) {
		rc = SA_NO_MEMORY;
		(void) snprintf(errbuf, buflen, "%s", sa_strerror(rc));
		return (rc);
	}

	/*
	 * if no properties specified and unset,
	 * remove all properties for this protocol.
	 */
	if (unset && sa_prop_empty_list(src_prot_nvl)) {
		if (sa_share_rem_proto(share_nvl, proto) != SA_OK) {
			rc = SA_NO_SUCH_PROTO;
			(void) snprintf(errbuf, buflen, "%s: %s",
			    sa_strerror(rc), prot_name);
			return (rc);
		} else {
			return (SA_OK);
		}
	}

	/*
	 * If there is a matching protocol nvlist in the destination
	 * share, then merge the two (via the appropriate protocol
	 * plugin routine), otherwise add the new protocol nvlist
	 * to the destination share.
	 */

	if ((dst_prot_nvl = sa_share_get_proto(share_nvl, proto)) != NULL) {
		return (saproto_share_merge(proto, dst_prot_nvl,
		    src_prot_nvl, unset, errbuf, buflen));
	} else {
		if (unset) {
			rc = SA_NO_SUCH_PROTO;
			(void) snprintf(errbuf, buflen, "%s: %s",
			    sa_strerror(rc), prot_name);
			return (rc);
		} else {
			if (sa_share_set_proto(share_nvl, proto,
			    src_prot_nvl) != SA_OK) {
				rc = SA_NO_MEMORY;
				(void) snprintf(errbuf, buflen, "%s",
				    sa_strerror(rc));
				return (rc);
			}
		}
	}

	return (SA_OK);
}

/*
 * sa_share_merge
 *
 * merge share properties from src_share into dst_share
 */
int
sa_share_merge(nvlist_t *dst_share, nvlist_t *src_share, int unset,
    char *errbuf, size_t buflen)
{
	char *propname;
	char *src_name;
	char *dst_name;
	nvpair_t *nvp;
	int rc;

	for (nvp = nvlist_next_nvpair(src_share, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(src_share, nvp)) {

		/*
		 * if this is an nvlist, then it must be a
		 * protocol property list. merge
		 */
		if (nvpair_type(nvp) == DATA_TYPE_NVLIST) {
			if ((rc = sa_share_merge_prot(nvp, dst_share,
			    unset, errbuf, buflen)) != SA_OK)
				return (rc);
		} else {
			propname = nvpair_name(nvp);

			if (strcasecmp(propname, SA_PROP_NAME) == 0) {
				/*
				 * sanity check, names should match
				 */
				if (nvpair_value_string(nvp, &src_name) != 0) {
					rc = SA_NO_SHARE_NAME;
					(void) snprintf(errbuf, buflen,
					    dgettext(TEXT_DOMAIN,
					    "source share: %s"),
					    sa_strerror(rc));
					return (rc);
				}

				dst_name = sa_share_get_name(dst_share);
				if (dst_name == NULL) {
					/*
					 * this should not happen. dst_share
					 * is an existing share read from disk
					 */
					rc = SA_NO_SHARE_NAME;
					(void) snprintf(errbuf, buflen,
					    dgettext(TEXT_DOMAIN,
					    "destination share: %s"),
					    sa_strerror(rc));
					return (rc);
				}

				if (strcmp(dst_name, src_name) != 0) {
					rc = SA_INVALID_SHARE_NAME;
					(void) snprintf(errbuf, buflen,
					    dgettext(TEXT_DOMAIN,
					    "share names do not match: %s"),
					    sa_strerror(rc));
					return (rc);
				}

				continue;
			}

			if (unset) {
				if (nvlist_remove(dst_share, propname,
				    DATA_TYPE_STRING) != 0) {
					rc = SA_NO_SUCH_PROP;
					(void) snprintf(errbuf, buflen,
					    "%s: %s",
					    sa_strerror(rc), propname);
					return (rc);
				}
			} else {
				if (nvlist_add_nvpair(dst_share, nvp) != 0) {
					rc = SA_NO_MEMORY;
					(void) snprintf(errbuf, buflen, "%s",
					    sa_strerror(rc));
					return (rc);
				}
			}
		}
	}

	return (SA_OK);
}

/*
 * sa_share_validate_name
 *
 * validate the share name
 *
 * The protocol plugins will also validate the name.
 */
int
sa_share_validate_name(const char *sh_name, const char *sh_path, boolean_t new,
    sa_proto_t proto, char *errbuf, size_t buflen)
{
	nvlist_t *share;
	sa_proto_t p;
	int rc;

	if (sh_name == NULL) {
		rc = SA_NO_SHARE_NAME;
		(void) snprintf(errbuf, buflen, "%s", sa_strerror(rc));
		return (rc);
	}

	for (p = sa_proto_first(); p != SA_PROT_NONE; p = sa_proto_next(p)) {
		if (p & proto) {
			if ((rc = saproto_share_validate_name(sh_name, p, new,
			    errbuf, buflen)) != SA_OK)
				return (rc);
		}
	}

	if (new) {
		if (sa_share_lookup(sh_name, NULL, SA_PROT_ANY,
		    &share) == SA_OK) {
			/* share name already exists */
			char *path = sa_share_get_path(share);

			if (path == NULL) {
				rc = SA_NO_SHARE_PATH;
				(void) snprintf(errbuf, buflen,
				    dgettext(TEXT_DOMAIN, "%s: name=%s"),
				    sa_strerror(rc), sh_name);
				sa_share_free(share);
				return (rc);
			}

			/*
			 * only a duplicate if paths are not the same
			 */
			if (strcmp(sh_path, path) != 0) {
				rc = SA_DUPLICATE_NAME;
				(void) snprintf(errbuf, buflen,
				    dgettext(TEXT_DOMAIN,
				    "%s: name=%s, path=%s"),
				    sa_strerror(rc), sh_name, path);
				sa_share_free(share);
				return (rc);
			}
			sa_share_free(share);
		}

		/*
		 * validate share name globally
		 */
		rc = sacache_share_validate_name(sh_name, new);
		if (rc != SA_OK &&
		    rc != SA_NOT_SUPPORTED &&
		    rc != SA_NOT_IMPLEMENTED) {
			/* share name already exists */
			rc = SA_DUPLICATE_NAME;
			(void) snprintf(errbuf, buflen, "%s: %s",
			    sa_strerror(rc), sh_name);
			return (rc);
		}
	}

	return (SA_OK);
}

/*
 * sa_share_validate
 *
 * validate the share nvlist.
 * This routine will validate global properties.
 * The protocol plugins will be called to validate protocol properties.
 */
int
sa_share_validate(nvlist_t *share, boolean_t new, char *errbuf, size_t buflen)
{
	nvpair_t *nvp;
	sa_proto_t proto;
	int proto_found = 0;
	char *propname;
	char *propval;
	char *sh_name = NULL;
	char *sh_path = NULL;
	char *sh_desc;
	char resolved_path[PATH_MAX+1];
	struct stat st;
	int prop_idx;
	int rc;

	/*
	 * convert share path to real path
	 */
	if ((sh_path = sa_share_get_path(share)) == NULL) {
		rc = SA_NO_SHARE_PATH;
		(void) snprintf(errbuf, buflen, "%s", sa_strerror(rc));
		return (rc);
	}

	if (realpath(sh_path, resolved_path) == NULL) {
		rc = SA_PATH_NOT_FOUND;
		(void) snprintf(errbuf, buflen, "%s: %s",
		    sa_strerror(rc), sh_path);
		return (rc);
	}

	/* remove any repeating or trailing slashes */
	(void) sa_fixup_path(resolved_path);

	/*
	 * store the realpath in the share. It will be
	 * validated further below and in the plugins.
	 */
	if ((rc = sa_share_set_path(share, resolved_path)) != SA_OK) {
		(void) snprintf(errbuf, buflen, "%s", sa_strerror(rc));
		return (rc);
	}

	for (nvp = nvlist_next_nvpair(share, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(share, nvp)) {

		propname = nvpair_name(nvp);
		prop_idx = sa_prop_cmp_list(propname, sa_global_prop_list);

		switch (prop_idx) {
		case SA_PIDX_NAME: /* name */
			if (nvpair_value_string(nvp, &sh_name) != 0) {
				rc = SA_NO_SHARE_NAME;
				(void) snprintf(errbuf, buflen, "%s",
				    sa_strerror(rc));
				return (rc);
			}

			/*
			 * validate the name. protocol handlers will validate
			 * the name later, so set proto to NONE here.
			 */
			if ((rc = sa_share_validate_name(sh_name, resolved_path,
			    new, SA_PROT_NONE, errbuf, buflen)) != SA_OK)
				return (rc);

			break;

		case SA_PIDX_PATH: /* path */
			if (nvpair_value_string(nvp, &sh_path) != 0) {
				rc = SA_NO_SHARE_PATH;
				(void) snprintf(errbuf, buflen, "%s",
				    sa_strerror(rc));
				return (rc);
			}

			/*
			 * make sure path exists.
			 */
			if (stat(sh_path, &st) < 0) {
				rc = SA_PATH_NOT_FOUND;
				(void) snprintf(errbuf, buflen, "%s: %s",
				    sa_strerror(rc), sh_path);
				return (rc);
			}
			break;

		case SA_PIDX_DESC: /* desc */
			if (nvpair_value_string(nvp, &sh_desc) != 0) {
				rc = SA_NO_SHARE_DESC;
				(void) snprintf(errbuf, buflen, "%s",
				    sa_strerror(rc));
				return (rc);
			}
			break;

		case SA_PIDX_MNTPNT: /* mountpoint */
			/*
			 * mntpnt property is internal use only
			 * user cannot modify, no validation required
			 */
			break;

		case SA_PIDX_TRANS:
			if (nvpair_value_string(nvp, &propval) != 0 ||
			    strcmp(propval, "true") != 0) {
				rc = SA_INVALID_PROP_VAL;
				(void) snprintf(errbuf, buflen, "%s",
				    sa_strerror(rc));
				return (rc);
			}
			break;

		case SA_PIDX_INVALID:
			/*
			 * not a global property.
			 * is it one of the protocol names?
			 */
			proto = sa_val_to_proto(propname);
			if (proto == SA_PROT_NONE) {
				(void) snprintf(errbuf, buflen,
				    dgettext(TEXT_DOMAIN,
				    "Missing protocol or invalid property: %s"),
				    propname);
				return (SA_INVALID_PROP);
			}
			/*
			 * yes, send it to the protocol plugin
			 */
			rc = saproto_share_validate(share, proto,
			    new, errbuf, buflen);
			if (rc != SA_OK)
				return (rc);
			proto_found = 1;
			break;

		default:
			rc = SA_INVALID_PROP;
			(void) snprintf(errbuf, buflen, "%s: %s",
			    sa_strerror(rc), propname);
			return (rc);
		}
	}

	/*
	 * must have a share name
	 */
	if (sh_name == NULL) {
		rc = SA_NO_SHARE_NAME;
		(void) snprintf(errbuf, buflen, "%s", sa_strerror(rc));
		return (rc);
	}

	/*
	 * must have a share path
	 */
	if (sh_path == NULL) {
		rc = SA_NO_SHARE_PATH;
		(void) snprintf(errbuf, buflen, "%s", sa_strerror(rc));
		return (rc);
	}

	/*
	 * must have protocol defined
	 */
	if (!proto_found) {
		rc = SA_NO_SHARE_PROTO;
		(void) snprintf(errbuf, buflen, "%s", sa_strerror(rc));
		return (rc);
	}

	return (SA_OK);
}

/*
 * sa_share_publish
 *
 * publish the share for the specified protocols.
 */
int
sa_share_publish(nvlist_t *share, sa_proto_t proto, int wait)
{
	sa_proto_t p;
	sa_proto_t protos_enabled;
	char *sh_path;
	int rc, ret = SA_OK;

	sa_tracef("share_publish_start: %s:%s:%d:%d",
	    sa_share_get_name(share), sa_share_get_path(share),
	    proto, wait);

	if ((sh_path = sa_share_get_path(share)) == NULL) {
		sa_tracef("share_publish_stop: %s: %s",
		    sa_share_get_name(share), sa_strerror(SA_NO_SHARE_PATH));
		return (SA_NO_SHARE_PATH);
	}

	if (sa_share_is_transient(share))
		protos_enabled = SA_PROT_ALL;
	else
		protos_enabled = safs_sharing_enabled(sh_path);

	for (p = sa_proto_first(); p != SA_PROT_NONE; p = sa_proto_next(p)) {
		if ((p & proto) && (p & protos_enabled)) {
			if (sa_share_get_proto(share, p) == NULL) {
				salog_debug(SA_NOT_SHARED_PROTO,
				    "sa_share_publish: %s:%s:%s",
				    sa_share_get_name(share),
				    sh_path, sa_proto_to_val(p));
				continue;
			}

			/*
			 * send to protocol plugin for publishing
			 */
			rc = saproto_share_publish(share, p, wait);
			if (rc != SA_OK)
				ret = rc;
		}
	}

	sa_tracef("share_publish_stop: %s: %s",
	    sa_share_get_name(share), sa_strerror(ret));
	return (ret);
}

/*
 * sa_share_unpublish
 *
 * unpublish the share for the specified protocols.
 */
int
sa_share_unpublish(nvlist_t *share, sa_proto_t proto, int wait)
{
	sa_proto_t p;
	int rc, ret = SA_OK;

	sa_tracef("share_unpublish_start: %s:%s:%d:%d",
	    sa_share_get_name(share), sa_share_get_path(share),
	    proto, wait);

	for (p = sa_proto_first(); p != SA_PROT_NONE; p = sa_proto_next(p)) {
		if (p & proto) {
			rc = saproto_share_unpublish(share, p, wait);
			if (rc != SA_OK)
				ret = rc;
		}
	}
	sa_tracef("share_unpublish_stop: %s: %s",
	    sa_share_get_name(share), sa_strerror(ret));
	return (ret);
}

/*
 * sa_share_unpublish_byname
 *
 * unpublish the share for the specified protocols.
 */
int
sa_share_unpublish_byname(const char *sh_name, const char *sh_path,
    sa_proto_t proto, int wait)
{
	sa_proto_t p;
	int rc, ret = SA_OK;

	sa_tracef("share_unpublish_start: %s:%s:%d:%d",
	    sh_name, sh_path, proto, wait);

	for (p = sa_proto_first(); p != SA_PROT_NONE; p = sa_proto_next(p)) {
		if (p & proto) {
			rc = saproto_share_unpublish_byname(sh_name, sh_path,
			    p, wait);
			if (rc != SA_OK)
				ret = rc;
		}
	}

	sa_tracef("share_unpublish_stop: %s: %s",
	    sh_name, sa_strerror(ret));

	return (ret);
}

static int
sa_share_publish_admin(const char *mntpnt, sa_proto_t proto)
{
	sa_proto_t p;
	int rc, ret = SA_OK;

	for (p = sa_proto_first(); p != SA_PROT_NONE; p = sa_proto_next(p)) {
		if (p & proto) {
			rc = saproto_share_publish_admin(mntpnt, p);
			if (rc != SA_OK)
				ret = rc;
		}
	}

	return (ret);
}

static void
sa_share_list_free(nvlist_t **share_list, int cnt)
{
	int i;

	if (share_list != NULL) {
		for (i = 0; i < cnt; ++i) {
			if (share_list[i] != NULL)
				nvlist_free(share_list[i]);
		}
		free(share_list);
	}
}

static nvlist_t **
sa_share_list_alloc(sa_proto_t proto, int cnt)
{
	int i;
	sa_proto_t p;
	nvlist_t **share_list;

	if ((share_list = calloc(cnt, sizeof (nvlist_t *))) == NULL)
		return (NULL);

	/*
	 * initialize the protocol share lists
	 */
	for (i = 0; i < cnt; ++i) {
		p = sa_proto_get_type(i);
		if (p & proto) {
			/* allocate an nvlist_t for this protocol */
			if (nvlist_alloc(&share_list[i], NV_UNIQUE_NAME, 0)
			    != 0) {
				sa_share_list_free(share_list, cnt);
				return (NULL);
			}
		}
	}

	return (share_list);
}

static int
sa_share_list_add(nvlist_t **share_list, nvlist_t *share, const char *sh_name,
    int proto_cnt, boolean_t publish)
{
	int i;
	int rc = SA_OK;
	sa_proto_t p;
	sa_proto_t status;

	if (publish)
		status = SA_PROT_ALL;
	else
		status = sa_share_get_status(share);

	for (i = 0; i < proto_cnt; ++i) {
		p = sa_proto_get_type(i);
		if (share_list[i] != NULL && (p & status) &&
		    sa_share_get_proto(share, p) != NULL) {
			if (nvlist_add_nvlist(share_list[i],
			    sh_name, share) != 0) {
				rc = SA_NO_MEMORY;
				break;
			}
		}
	}

	sa_share_free(share);
	return (rc);
}

static int
sa_share_list_publish(nvlist_t **share_list, int proto_cnt, int wait)
{
	int i;
	int err;
	int rc = SA_OK;

	for (i = 0; i < proto_cnt; ++i) {
		if (share_list[i] == NULL)
			continue;

		if (!nvlist_empty(share_list[i])) {
			/*
			 * Now send the share list to the protocol
			 * plugin. The plugin will always free the
			 * share list
			 */
			err = saproto_fs_publish(share_list[i],
			    sa_proto_get_type(i), wait);
			share_list[i] = NULL;
			if (err != SA_OK) {
				switch (err) {
				case SA_NO_MEMORY:
				case SA_NO_PERMISSION:
					rc = err;
					goto err_out;
				default:
					rc = SA_PARTIAL_PUBLISH;
					break;
				}
			}
		} else {
			nvlist_free(share_list[i]);
			share_list[i] = NULL;
		}
	}

	free(share_list);
	return (rc);

err_out:
	sa_share_list_free(share_list, proto_cnt);
	return (rc);
}

/*
 * sa_fs_publish
 *
 * Publish all shares on the dataset for the specified protocols .
 *
 * Retrieve all shares from cache, and collect into protocol lists,
 * then send the lists to protocol plugins.
 */
int
sa_fs_publish(const char *mntpnt, sa_proto_t proto, int wait)
{
	int rc;
	void *hdl;
	char *sh_name;
	char *sh_path;
	char *active_path;
	char *old_name = NULL;
	nvlist_t *share;
	nvlist_t *active_shr;
	nvlist_t **share_list;
	boolean_t share_tossed = B_FALSE;
	int proto_cnt = sa_proto_count();

	sa_tracef("fs_publish_start: %s:%d:%d", mntpnt, proto, wait);

	(void) sa_share_publish_admin(mntpnt, proto);

	/*
	 * Get a list of protocols enabled for this mntpnt
	 */
	if ((proto &= safs_sharing_enabled(mntpnt)) == SA_PROT_NONE) {
		salog_debug(0,
		    "sa_fs_publish: %s: protocols not enabled for sharing",
		    mntpnt);
		return (SA_OK);
	}

	/*
	 * allocate protocol share lists
	 */
	share_list = sa_share_list_alloc(proto, proto_cnt);
	if (share_list == NULL) {
		rc = SA_NO_MEMORY;
		goto out;
	}

	/*
	 * obtain all shares for this file system from disk
	 */
	if ((rc = sa_share_read_init(mntpnt, &hdl)) != SA_OK) {
		if (rc == SA_SHARE_NOT_FOUND)
			rc = SA_OK;
		sa_share_list_free(share_list, proto_cnt);
		goto out;
	}

	while (sa_share_read_next(hdl, &share) == SA_OK) {
		if (((sh_name = sa_share_get_name(share)) == NULL) ||
		    ((sh_path = sa_share_get_path(share)) == NULL)) {
			sa_share_free(share);
			continue;
		}

		if (sa_share_lookup(sh_name, NULL, proto, &active_shr)
		    == SA_OK) {
			/*
			 * share name already exists,
			 * no conflict if for the same path
			 */
			active_path = sa_share_get_path(active_shr);
			if (strcmp(sh_path, active_path) != 0) {
				sa_share_free(active_shr);
				if ((old_name = strdup(sh_name)) == NULL) {
					rc = SA_NO_MEMORY;
					sa_share_free(share);
					sa_share_read_fini(hdl);
					sa_share_list_free(share_list,
					    proto_cnt);
					goto out;
				}

				rc = sa_resolve_share_name_conflict(share,
				    share_list, proto_cnt);
				if (rc != SA_OK) {
					salog_error(0, "%s:%s not shared: "
					    "name conflict: %s",
					    old_name, sh_path, sa_strerror(rc));
					free(old_name);
					sa_share_free(share);
					share_tossed = B_TRUE;
					continue;
				}

				sh_name = sa_share_get_name(share);
				sh_path = sa_share_get_path(share);
				if (sa_share_get_proto(share, SA_PROT_SMB)) {
					/*
					 * log a message if shared for SMB
					 */
					salog_info(0,
					    "SMB: %s:%s: share name conflict, "
					    "name changed to: %s",
					    old_name, sh_path, sh_name);
				}
				free(old_name);
			} else {
				sa_share_free(active_shr);
			}
		}

		/*
		 * Add the share to all protocol lists for which it has been
		 * configured. share will be freed by sa_share_list_add.
		 */
		rc = sa_share_list_add(share_list, share, sh_name,
		    proto_cnt, B_TRUE);
		if (rc != SA_OK) {
			sa_share_read_fini(hdl);
			sa_share_list_free(share_list, proto_cnt);
			goto out;
		}
	}
	sa_share_read_fini(hdl);

	/*
	 * now send the share list to the protocol plugins.
	 * sa_share_list_publish will make sure share_list is freed
	 */
	rc = sa_share_list_publish(share_list, proto_cnt, wait);

out:
	if (rc != SA_OK) {
		salog_error(rc,
		    "sa_fs_publish: %s: could not publish all shares",
		    mntpnt);
	}

	if (share_tossed)
		rc = SA_DUPLICATE_NAME;

	sa_tracef("fs_publish_stop: %s: %s", mntpnt, sa_strerror(rc));
	return (rc);
}

/*
 * sa_ds_publish
 *
 * Publish all shares on the dataset for the specified protocols .
 *
 * Read all shares from share cache, and collect into protocol lists,
 * then send the lists to protocol plugins to be published.
 */
int
sa_ds_publish(const char *dataset, sa_proto_t proto, int wait)
{
	int rc;
	void *hdl;
	char *sh_name;
	nvlist_t *share;
	nvlist_t **share_list;
	int proto_cnt = sa_proto_count();

	sa_tracef("ds_publish_start: %s:%d:%d", dataset, proto, wait);

	share_list = sa_share_list_alloc(proto, proto_cnt);
	if (share_list == NULL) {
		rc = SA_NO_MEMORY;
		goto out;
	}

	/*
	 * read all shares for this dataset from cache
	 */
	if ((rc = sacache_share_ds_find_init(dataset, proto, &hdl)) != SA_OK) {
		if (rc == SA_SHARE_NOT_FOUND)
			rc = SA_OK;
		sa_share_list_free(share_list, proto_cnt);
		goto out;
	}

	while ((rc = sacache_share_ds_find_get(hdl, &share)) == SA_OK) {
		if (((sh_name = sa_share_get_name(share)) == NULL) ||
		    (sa_share_get_path(share) == NULL)) {
			sa_share_free(share);
			continue;
		}

		/*
		 * Add the share to all protocol lists for which it has been
		 * configured. sa_share_list_add will free share.
		 */
		rc = sa_share_list_add(share_list, share, sh_name,
		    proto_cnt, B_TRUE);
		if (rc != SA_OK) {
			(void) sacache_share_ds_find_fini(hdl);
			sa_share_list_free(share_list, proto_cnt);
			goto out;
		}
	}

	(void) sacache_share_ds_find_fini(hdl);

	/*
	 * now send the share list to the protocol plugins.
	 * sa_share_list_publish will make sure share_list is freed
	 */
	rc = sa_share_list_publish(share_list, proto_cnt, wait);

out:
	if (rc != SA_OK) {
		salog_error(rc,
		    "sa_ds_publish: %s: could not publish shares",
		    dataset);
	}
	sa_tracef("ds_publish_stop: %s: %s", dataset, sa_strerror(rc));

	return (rc);
}

/*
 * sa_list_publish
 *
 * Publish all shares in the sh_list for the specified protocol.
 */
int
sa_list_publish(nvlist_t *sh_list, sa_proto_t proto, int wait)
{
	int rc = SA_OK;

	if (!nvlist_empty(sh_list))
		rc = saproto_fs_publish(sh_list, proto, wait);
	else
		nvlist_free(sh_list);

	return (rc);
}

static int
sa_share_list_unpublish(nvlist_t **share_list, int proto_cnt, int wait)
{
	int i;
	int err;
	int rc = SA_OK;

	for (i = 0; i < proto_cnt; ++i) {
		if (share_list[i] == NULL)
			continue;

		if (!nvlist_empty(share_list[i])) {
			/*
			 * Now send the share list to the protocol
			 * plugin. The plugin will always free the
			 * share list.
			 */
			err = saproto_fs_unpublish(share_list[i],
			    sa_proto_get_type(i), wait);
			/* list has been freed by protocol plugin */
			share_list[i] = NULL;
			if (err != SA_OK) {
				switch (err) {
				case SA_NO_MEMORY:
				case SA_NO_PERMISSION:
					rc = err;
					goto err_out;
				default:
					rc = SA_PARTIAL_UNPUBLISH;
					break;
				}
			}
		} else {
			nvlist_free(share_list[i]);
			share_list[i] = NULL;
		}
	}

	free(share_list);
	return (rc);

err_out:
	sa_share_list_free(share_list, proto_cnt);
	return (rc);
}

/*
 * sa_fs_unpublish
 *
 * find all shares from cache, and collect into protocol lists,
 * then send the lists to the protocol plugins for unpublish
 */
int
sa_fs_unpublish(const char *mntpnt, sa_proto_t proto, int wait)
{
	int rc;
	void *hdl;
	char *sh_name;
	nvlist_t *share;
	nvlist_t **share_list;
	int proto_cnt = sa_proto_count();

	sa_tracef("fs_unpublish_start: %s:%d:%d", mntpnt, proto, wait);

	share_list = sa_share_list_alloc(proto, proto_cnt);
	if (share_list == NULL) {
		rc = SA_NO_MEMORY;
		goto out;
	}

	/*
	 * find all shares for this file system, from cache
	 */
	if ((rc = sa_share_find_init(mntpnt, proto, &hdl)) != SA_OK) {
		if (rc == SA_SHARE_NOT_FOUND || rc == SA_NOT_SUPPORTED)
			rc = SA_OK;
		sa_share_list_free(share_list, proto_cnt);
		goto out;
	}

	while (sa_share_find_next(hdl, &share) == SA_OK) {
		if (((sh_name = sa_share_get_name(share)) == NULL) ||
		    (sa_share_get_path(share) == NULL)) {
			sa_share_free(share);
			continue;
		}

		/*
		 * Add the share to all protocol lists for which it
		 * is currently shared. sa_share_list_add will free share.
		 */
		rc = sa_share_list_add(share_list, share, sh_name,
		    proto_cnt, B_FALSE);
		if (rc != SA_OK) {
			sa_share_find_fini(hdl);
			sa_share_list_free(share_list, proto_cnt);
			goto out;
		}

	}
	sa_share_find_fini(hdl);

	/*
	 * now send the share list to the protocol plugins.
	 * sa_share_list_unpublish will make sure share_list is freed
	 */
	rc = sa_share_list_unpublish(share_list, proto_cnt, wait);

out:
	if (rc != SA_OK) {
		salog_error(rc, "sa_fs_unpublish: "
		    "%s: could not unpublish shares", mntpnt);
	}

	sa_tracef("fs_unpublish_stop: %s: %s", mntpnt, sa_strerror(rc));

	return (rc);
}

/*
 * sa_ds_unpublish
 *
 * find all shares from cache, and collect into protocol lists,
 * then send the lists to the protocol plugins for unpublish
 */
int
sa_ds_unpublish(const char *dataset, sa_proto_t proto, int wait)
{
	int rc;
	void *hdl;
	char *sh_name;
	nvlist_t *share;
	nvlist_t **share_list;
	int proto_cnt = sa_proto_count();

	sa_tracef("ds_unpublish_start: %s:%d:%d", dataset, proto, wait);

	share_list = sa_share_list_alloc(proto, proto_cnt);
	if (share_list == NULL) {
		rc = SA_NO_MEMORY;
		goto out;
	}

	/*
	 * find all shares for this dataset, from cache
	 */
	if ((rc = sacache_share_ds_find_init(dataset, proto, &hdl)) != SA_OK) {
		if (rc == SA_SHARE_NOT_FOUND || rc == SA_NOT_SUPPORTED)
			rc = SA_OK;
		sa_share_list_free(share_list, proto_cnt);
		goto out;
	}

	while (sacache_share_ds_find_get(hdl, &share) == SA_OK) {
		if (((sh_name = sa_share_get_name(share)) == NULL) ||
		    (sa_share_get_path(share) == NULL)) {
			sa_share_free(share);
			continue;
		}

		/*
		 * Add the share to all protocol lists for which it has been
		 * configured. sa_share_list_add will free share.
		 */
		rc = sa_share_list_add(share_list, share, sh_name, proto_cnt,
		    B_FALSE);
		if (rc != SA_OK) {
			(void) sacache_share_ds_find_fini(hdl);
			sa_share_list_free(share_list, proto_cnt);
			goto out;
		}
	}

	(void) sacache_share_ds_find_fini(hdl);

	/*
	 * now send the share list to the protocol plugins.
	 * sa_share_list_unpublish will make sure share_list is freed
	 */
	rc = sa_share_list_unpublish(share_list, proto_cnt, wait);
out:
	if (rc != SA_OK) {
		salog_error(rc, "sa_ds_unpublish: "
		    "%s: could not unpublish shares", dataset);
	}
	sa_tracef("ds_unpublish_stop: %s: %s", dataset, sa_strerror(rc));

	return (rc);
}

/*
 * sa_share write
 *
 * write share to repository
 * share must be validated prior to writing
 *
 * After writing the share, republish the share
 * and update the share cache.
 */
int
sa_share_write(nvlist_t *share)
{
	int rc;
	char *sh_name;
	char *sh_path;
	char *old_path;
	nvlist_t *old_share = NULL;
	sa_proto_t p, proto = SA_PROT_NONE;

	if ((sh_name = sa_share_get_name(share)) == NULL)
		return (SA_NO_SHARE_NAME);

	if ((sh_path = sa_share_get_path(share)) == NULL)
		return (SA_NO_SHARE_PATH);

	/*
	 * check which protocols were previously configured in
	 * case a protocol was removed.
	 */
	if (sa_share_read(sh_path, sh_name, &old_share) == SA_OK) {
		for (p = sa_proto_first(); p != SA_PROT_NONE;
		    p = sa_proto_next(p)) {
			if ((sa_share_get_proto(old_share, p) != NULL) &&
			    (sa_share_get_proto(share, p) == NULL))
				proto |= p;
		}

		/*
		 * make sure all protocols are unpublished if
		 * the path is changing
		 */
		old_path = sa_share_get_path(old_share);
		if (strcmp(sh_path, old_path) != 0)
			proto = SA_PROT_ALL;
	}

	if ((rc = safs_share_write(share)) != SA_OK) {
		if (old_share != NULL)
			sa_share_free(old_share);
		return (rc);
	}

	if (old_share != NULL) {
		if (proto != SA_PROT_NONE)
			(void) sa_share_unpublish(old_share, proto, 1);
		sa_share_free(old_share);
	}

	(void) sa_share_publish(share, SA_PROT_ALL, 0);

	return (SA_OK);
}

/*
 * sa_share_create_default
 *
 * Create a default share for the path and protocol specified.
 * The share name is created from the share path.
 */
static int
sa_share_create_default(const char *sh_path, sa_proto_t proto, boolean_t *new,
    nvlist_t **share)
{
	int rc;
	nvlist_t *new_share;

	/*
	 * create share with default name derived from path
	 */
	rc = sa_share_from_path(sh_path, &new_share, new);
	if (rc != SA_OK) {
		salog_error(rc, "sa_share_create_default: %s: "
		    "error creating share name", sh_path);
		return (rc);
	}

	if (sa_share_get_proto(new_share, proto) != NULL) {
		/*
		 * share with default name already
		 * exists for this protocol
		 */
		sa_share_free(new_share);
		salog_debug(0, "sa_share_create_default: "
		    "default share exists");
		return (SA_OK);
	}

	/*
	 * call the protocol plugins to add a protocol
	 * property list with default values.
	 */
	rc = sa_share_set_def_proto(new_share, proto);
	if (rc != SA_OK) {
		sa_share_free(new_share);
		salog_error(rc, "sa_share_create_default: %s: "
		    "error setting default proto", sh_path);
		return (rc);
	}

	*share = new_share;

	return (rc);
}

/*
 * sa_share_create_from_props
 *
 * create a share using the supplied property string.
 * This routine is used during upgrade to create a share
 * from existing property strings (sharenfs/sharesmb).
 *
 */
static int
sa_share_create_from_props(const char *propstr, const char *sh_path,
    sa_proto_t proto, boolean_t *new, nvlist_t **share,
    char *errbuf, size_t buflen)
{
	int rc;
	nvlist_t *proto_props;
	nvlist_t *new_share;
	char *sh_name;

	/*
	 * All properties in the property string should
	 * be protocol specific, (except possibly 'name')
	 * so pass the property string to the protocol parser.
	 * Parser should not worry about validity of
	 * properties, just the format.
	 *
	 */
	if ((rc = saproto_share_parse(proto, propstr, 0, &proto_props, errbuf,
	    buflen)) != SA_OK) {
		return (rc);
	}

	if ((sh_name = sa_share_get_name(proto_props)) != NULL) {
		/*
		 * found "name" property, look for share with name
		 */
		rc = sa_share_read(sh_path, sh_name, &new_share);
		if (rc == SA_OK) {
			/*
			 * share with same name exists, return error
			 */
			sa_share_free(new_share);
			sa_share_free(proto_props);
			return (SA_DUPLICATE_NAME);
		} else {
			/*
			 * share does not exist. create one with name
			 */
			new_share = sa_share_alloc(sh_name, sh_path);
			if (new_share == NULL) {
				sa_share_free(proto_props);
				return (SA_NO_MEMORY);
			}
		}

		/*
		 * remove name property from protocol property list
		 */
		(void) nvlist_remove(proto_props, SA_PROP_NAME,
		    DATA_TYPE_STRING);
	} else {
		/*
		 * name property not specified, create name from path
		 */
		rc = sa_share_from_path(sh_path, &new_share, new);
		if (rc != SA_OK) {
			sa_share_free(proto_props);
			salog_error(rc, "sa_share_create_from_props: "
			    "error creating share for %s", sh_path);
			return (rc);
		}
	}

	/*
	 * We now have a share (new_share) and a
	 * protocol property list (proto_props)
	 * Add the protocol property list to new share
	 */
	rc = sa_share_set_proto(new_share, proto, proto_props);
	sa_share_free(proto_props);
	if (rc != SA_OK) {
		sa_share_free(new_share);
		salog_error(rc, "sa_share_create_from_props: "
		    "error creating share for %s", sh_path);
		return (rc);
	}

	*share = new_share;
	return (rc);
}

int
sa_share_create_defaults(const char *mntpnt, sa_proto_t protos,
    const char *nfsopts, const char *smbopts)
{
	int rc = SA_OK;
	int err = SA_OK;
	char *sh_path;
	boolean_t nfs_new = B_TRUE;
	boolean_t smb_new = B_TRUE;
	nvlist_t *nfs_share = NULL;
	nvlist_t *smb_share = NULL;
	sa_proto_t smb_share_protos = SA_PROT_SMB;
	char *nfs_name;
	char *smb_name;
	nvlist_t *nfs_props;
	char errbuf[512];

	if ((sh_path = strdup(mntpnt)) == NULL) {
		rc = SA_NO_MEMORY;
		salog_error(rc, "sa_share_create_defaults: %s", mntpnt);
		return (rc);
	}

	/* remove any repeating or trailing slashes */
	(void) sa_fixup_path(sh_path);

	/*
	 * create an NFS share based on sharenfs option string
	 */
	if (nfsopts != NULL && strcmp(nfsopts, "off") != 0) {
		if (strcmp(nfsopts, "on") == 0) {
			err = sa_share_create_default(sh_path, SA_PROT_NFS,
			    &nfs_new, &nfs_share);
		} else {
			if (strncmp(nfsopts, "on,", 3) == 0)
				nfsopts += 3;
			/*
			 * convert shareopts to share.
			 */
			err = sa_share_create_from_props(nfsopts, sh_path,
			    SA_PROT_NFS, &nfs_new, &nfs_share,
			    errbuf, sizeof (errbuf));
		}
	} else {
		err = SA_OK;
	}

	if (err != SA_OK)
		rc = err;

	/*
	 * create an SMB share based on sharenfs option string
	 */
	if (smbopts != NULL && strcmp(smbopts, "off") != 0) {
		if (strcmp(smbopts, "on") == 0) {
			err = sa_share_create_default(sh_path, SA_PROT_SMB,
			    &smb_new, &smb_share);
		} else {
			if (strncmp(smbopts, "on,", 3) == 0)
				smbopts += 3;
			/*
			 * convert smbopts to share.
			 */
			err = sa_share_create_from_props(smbopts, sh_path,
			    SA_PROT_SMB, &smb_new, &smb_share,
			    errbuf, sizeof (errbuf));
		}
	} else {
		err = SA_OK;
	}

	if (err != SA_OK)
		rc = err;

	if (nfs_share != NULL && smb_share != NULL) {
		/*
		 * we have two shares, if they have the same name
		 * combine them into a single share, otherwise
		 * use both of them
		 */
		if ((nfs_name = sa_share_get_name(nfs_share)) == NULL ||
		    (smb_name = sa_share_get_name(smb_share)) == NULL) {
			rc = SA_NO_SHARE_NAME;
			goto out;
		}

		if (strcmp(nfs_name, smb_name) == 0) {
			/*
			 * combine the shares
			 * add the nfs properties to smb share
			 */
			nfs_props = sa_share_get_proto(nfs_share, SA_PROT_NFS);
			if (nfs_props != NULL) {
				if (sa_share_set_proto(smb_share, SA_PROT_NFS,
				    nfs_props) != SA_OK) {
					rc = SA_NO_MEMORY;
					goto out;
				}
				smb_share_protos |= SA_PROT_NFS;
			}
			sa_share_free(nfs_share);
			nfs_share = NULL;
		}
	}

	if (nfs_share != NULL) {
		err = sa_share_validate(nfs_share, nfs_new,
		    errbuf, sizeof (errbuf));
		if (err == SA_OK) {
			/*
			 * save the share
			 */
			err = safs_share_write(nfs_share);
			if ((err == SA_READ_ONLY) && (protos & SA_PROT_NFS)) {
				/*
				 * write failed because of a read-only
				 * filesystem, publish the share anyway
				 */
				(void) sa_share_set_mntpnt(nfs_share, sh_path);
				(void) sa_share_publish(nfs_share,
				    SA_PROT_NFS, 0);
			}
		}
		if (err != SA_OK)
			rc = err;
	}

	if (smb_share != NULL) {
		err = sa_share_validate(smb_share, smb_new,
		    errbuf, sizeof (errbuf));
		if (err == SA_OK) {
			/*
			 * now save the share
			 */
			err = safs_share_write(smb_share);
			if ((err == SA_READ_ONLY) &&
			    (protos & smb_share_protos)) {
				/*
				 * if smb_share contains at least one of the
				 * protocols specified in protos, then publish
				 */
				(void) sa_share_set_mntpnt(smb_share, sh_path);
				(void) sa_share_publish(smb_share, protos, 0);
			}
		}
		if (err != SA_OK)
			rc = err;
	}

out:
	free(sh_path);
	if (smb_share != NULL)
		sa_share_free(smb_share);
	if (nfs_share != NULL)
		sa_share_free(nfs_share);

	return (rc);
}

/*
 * sa_share_remove
 *
 * INPUTS:
 *   sh_name  : name of share to remove
 *   sh_path  : share path, may be NULL
 *
 * RETURN VALUES:
 *   SA_OK              : share removed successfully
 *   SA_INVALID_SHARE   : invalid share name (NULL)
 *   SA_SHARE_NOT_FOUND : share does not exist
 *   SA_NOT_SUPPORTED   : no plugin for file system type
 */
int
sa_share_remove(const char *sh_name, const char *sh_path)
{
	int rc;
	nvlist_t *share;
	sa_proto_t p, prot = SA_PROT_NONE;
	struct stat st;

	if (sh_name == NULL) {
		salog_debug(SA_INVALID_SHARE_NAME,
		    "sa_share_remove");
		return (SA_NO_SHARE_NAME);
	}

	if (sa_share_read(sh_path, sh_name, &share) != SA_OK) {
		/*
		 * Could not find share. When sh_path is NULL
		 * all mountpoints were searched so it really
		 * doesn't exist, so exit.
		 */
		if (sh_path == NULL)
			return (SA_SHARE_NOT_FOUND);

		if (stat(sh_path, &st) < 0 && errno == ENOENT) {
			/*
			 * directory does not exist, cannot determine
			 * mountpoint with out path. Try again with
			 * NULL path, to search all mntpnts.
			 */
			rc = sa_share_read(NULL, sh_name, &share);
			if (rc != SA_OK)
				return (SA_SHARE_NOT_FOUND);
			/*
			 * found the share, use mountpoint from
			 * share instead of sh_path for remove
			 */
			sh_path = sa_share_get_mntpnt(share);
			if (sh_path == NULL)
				return (SA_INVALID_SHARE_PATH);
		} else {
			/*
			 * path is ok, share does not exist
			 */
			return (SA_SHARE_NOT_FOUND);
		}
	}

	if (sh_path == NULL) {
		/*
		 * use mount point from share instead
		 */
		sh_path = sa_share_get_mntpnt(share);
		if (sh_path == NULL) {
			sa_share_free(share);
			return (SA_NO_SHARE_PATH);
		}
	}

	for (p = sa_proto_first(); p != SA_PROT_NONE; p = sa_proto_next(p)) {
		if (sa_share_get_proto(share, p) != NULL)
			prot |= p;
	}

	(void) sa_share_unpublish(share, prot, 1);

	rc = safs_share_remove(sh_name, sh_path);

	sa_share_free(share);

	return (rc);
}

/*
 * sa_share_lookup
 *
 * search the share cache for the specified share.
 * sh_name or sh_path can be NULL but not both.
 *
 * If sh_name is non NULL return the share if found and is
 * configured for the specified protocols.
 *
 * If sh_path is non NULL, search specified dataset for sh_name.
 * If sh_name is NULL, return first share found.
 *
 * It is the responsibility of the caller to free the nvlist.
 */
int
sa_share_lookup(const char *sh_name, const char *sh_path, sa_proto_t proto,
    nvlist_t **share)
{
	if (sh_name == NULL)
		return (SA_INVALID_SHARE_NAME);

	return (sacache_share_lookup(sh_name, sh_path, proto, share));
}

/*
 * sa_share_read
 *
 * read a share from the share repository.
 * This is .zfs/shares on zfs datasets.
 * and SMF repository for all others.
 *
 * INPUTS:
 *   mntpnt: if NULL, search all mounted file systems
 *   sh_name: name of share to read.
 *
 * OUTPUT:
 *   share: pointer to nvlist containing share properties
 *          Must be freed by caller.
 */
int
sa_share_read(const char *mntpnt, const char *sh_name, nvlist_t **share)
{
	FILE *fp;
	struct mnttab entry;
	int rc;

	if (sh_name == NULL)
		return (SA_NO_SHARE_NAME);

	if (share == NULL)
		return (SA_INVALID_SHARE);

	if (mntpnt != NULL) {
		return (safs_share_read(mntpnt, sh_name, share));
	} else {
		*share = NULL;

		/*
		 * search mounted filesystems for this share
		 */
		if ((fp = fopen(MNTTAB, "r")) == NULL) {
			salog_error(0, "sa_share_read: "
			    "error opening %s: %s",
			    MNTTAB, strerror(errno));
			return (SA_SYSTEM_ERR);
		}

		while (getmntent(fp, &entry) == 0) {
			char *mntpnt;

			if (sa_mntent_is_shareable(&entry) != SA_OK)
				continue;

			if ((mntpnt = strdup(entry.mnt_mountp)) == NULL) {
				salog_error(SA_NO_MEMORY, "sa_share_read");
				(void) fclose(fp);
				return (SA_NO_MEMORY);
			}

			rc = safs_share_read(mntpnt, sh_name, share);
			if (rc == SA_OK) {
				free(mntpnt);
				(void) fclose(fp);
				return (rc);
			}
			free(mntpnt);
		}

		(void) fclose(fp);
		return (SA_SHARE_NOT_FOUND);
	}
}

/*
 * sa_alloc_read_hdl
 *
 * allocate a sa_read_hdl_t to be used by
 * sa_share_read_[init|get|fini]
 */
static sa_read_hdl_t *
sa_alloc_read_hdl(void)
{
	sa_read_hdl_t *srhp;

	if ((srhp = calloc(1, sizeof (sa_read_hdl_t))) != NULL) {
		srhp->srh_proto = SA_PROT_ALL; /* used by find routines */
	}

	return (srhp);
}

/*
 * sa_free_read_hdl
 *
 * free a sa_read_hdl_t previously allocated
 * by sa_alloc_read_hdl
 */
static void
sa_free_read_hdl(sa_read_hdl_t *srhp)
{
	if (srhp->srh_mnttab_fp != NULL)
		(void) fclose(srhp->srh_mnttab_fp);

	if (srhp->srh_mntpnt != NULL)
		free(srhp->srh_mntpnt);

	free(srhp);
}

/*
 * sa_share_read_init
 *
 * Iterator routines used to read (and cache) all shares for a
 * specified file system from the repository. If mntpnt is NULL,
 * all shares are retrieved.
 * 'hdl' is allocated in sa_share_read_init() and is freed in
 * sa_share_read_fini().
 *
 * if mntpnt is non-NULL, it must be valid mountpoint.
 */
int
sa_share_read_init(const char *mntpnt, void **hdl)
{
	int rc, ret = SA_OK;
	sa_read_hdl_t *srhp;
	struct mnttab entry;

	if ((srhp = sa_alloc_read_hdl()) == NULL) {
		salog_error(SA_NO_MEMORY,
		    "sa_share_read_init");
		return (SA_NO_MEMORY);
	}

	if (mntpnt == NULL) {
		/*
		 * iterate through entries in mnttab looking for
		 * a valid share directory
		 */
		if ((srhp->srh_mnttab_fp = fopen(MNTTAB, "r")) == NULL) {
			sa_free_read_hdl(srhp);
			salog_error(0,
			    "sa_share_read_init: "
			    "error opening %s: %s",
			    MNTTAB, strerror(errno));
			return (SA_SYSTEM_ERR);
		}

		while ((rc = getmntent(srhp->srh_mnttab_fp, &entry)) == 0) {
			if (sa_mntent_is_shareable(&entry) != SA_OK)
				continue;

			srhp->srh_mntpnt = strdup(entry.mnt_mountp);
			if (srhp->srh_mntpnt == NULL) {
				sa_free_read_hdl(srhp);
				salog_error(SA_NO_MEMORY,
				    "sa_share_read_init");
				return (SA_NO_MEMORY);
			}

			if ((rc = safs_share_read_init(srhp)) == SA_OK) {
				*hdl = (void *)srhp;
				return (SA_OK);
			} else {
				/*
				 * error opening share directory
				 */
				free(srhp->srh_mntpnt);
				srhp->srh_mntpnt = NULL;

				/* if memory error, leave now */
				if (rc == SA_NO_MEMORY) {
					salog_error(rc, "sa_share_read_init");
					sa_free_read_hdl(srhp);
					return (rc);
				}
			}
		}

		sa_free_read_hdl(srhp);
		if (rc < 0) {
			/* EOF encountered */
			return (SA_SHARE_NOT_FOUND);
		} else {
			salog_error(0,
			    "sa_share_read_init: "
			    "error reading %s: %d",
			    MNTTAB, rc);
			return (SA_SYSTEM_ERR);
		}
	} else {
		srhp->srh_mnttab_fp = NULL;
		srhp->srh_mntpnt = strdup(mntpnt);
		if (srhp->srh_mntpnt == NULL) {
			sa_free_read_hdl(srhp);
			salog_error(SA_NO_MEMORY,
			    "sa_share_read_init");
			return (SA_NO_MEMORY);
		}

		if ((ret = safs_share_read_init(srhp)) != SA_OK) {
			sa_free_read_hdl(srhp);
			return (ret);
		}
		*hdl = (void *)srhp;
		return (SA_OK);
	}
}

/*
 * sa_share_read_next
 *
 */
int
sa_share_read_next(void *hdl, nvlist_t **share)
{
	int rc;
	struct mnttab entry;
	sa_read_hdl_t *srhp = (sa_read_hdl_t *)hdl;

	/*
	 * call the file system plugin to
	 * read from current dataset
	 */
	if ((rc = safs_share_read_next(srhp, share)) == SA_OK)
		return (SA_OK);

	/*
	 * no share found on current dataset.
	 * check other datasets if needed
	 */
	if (srhp->srh_mnttab_fp != NULL) {

		(void) safs_share_read_fini(srhp);

		if (srhp->srh_mntpnt != NULL) {
			free(srhp->srh_mntpnt);
			srhp->srh_mntpnt = NULL;
		}

		/*
		 * continue going through entries in mnttab looking for
		 * a valid share directory
		 */
		while ((rc = getmntent(srhp->srh_mnttab_fp, &entry)) == 0) {
			if (sa_mntent_is_shareable(&entry) != SA_OK)
				continue;

			srhp->srh_mntpnt = strdup(entry.mnt_mountp);
			if (srhp->srh_mntpnt == NULL) {
				salog_error(SA_NO_MEMORY,
				    "sa_share_read_get");
				return (SA_NO_MEMORY);
			}

			if ((rc = safs_share_read_init(srhp)) == SA_OK &&
			    (rc = safs_share_read_next(srhp, share))
			    == SA_OK) {
				return (SA_OK);
			}

			/* if memory error, leave now */
			if (rc == SA_NO_MEMORY) {
				return (rc);
			}

			(void) safs_share_read_fini(srhp);
			free(srhp->srh_mntpnt);
			srhp->srh_mntpnt = NULL;
		}

		(void) fclose(srhp->srh_mnttab_fp);
		srhp->srh_mnttab_fp = NULL;

		if (rc < 0) {
			/* EOF encountered */
			return (SA_SHARE_NOT_FOUND);
		} else {
			salog_error(0,
			    "sa_share_read_get: "
			    "error reading %s: %d", MNTTAB, rc);
			return (SA_SYSTEM_ERR);
		}

	}

	return (rc);
}

/*
 * sa_share_read_fini
 */
void
sa_share_read_fini(void *hdl)
{
	sa_read_hdl_t *srhp = (sa_read_hdl_t *)hdl;

	(void) safs_share_read_fini(srhp);

	sa_free_read_hdl(srhp);
}

int
sa_share_get_acl(const char *sh_name, const char *sh_path, acl_t **aclp)
{
	if (sh_name == NULL)
		return (SA_INVALID_SHARE_NAME);

	if (sh_path == NULL)
		return (SA_INVALID_SHARE_PATH);

	return (safs_share_get_acl(sh_name, sh_path, aclp));
}

int
sa_share_set_acl(const char *sh_name, const char *sh_path, acl_t *acl)
{
	if (sh_name == NULL)
		return (SA_INVALID_SHARE_NAME);

	if (sh_path == NULL)
		return (SA_INVALID_SHARE_PATH);

	return (safs_share_set_acl(sh_name, sh_path, acl));
}

/*
 * sa_get_mntpnt_for_path
 *
 * call the file system plugin to return the
 * mountpoint of the file system containing sh_path.
 */
int
sa_get_mntpnt_for_path(const char *sh_path, char *mntpnt, size_t mp_len,
    char *dataset, size_t ds_len, char *mntopts, size_t opt_len)
{
	if (sh_path == NULL)
		return (SA_INVALID_SHARE_PATH);

	return (safs_get_mntpnt_for_path(sh_path, mntpnt, mp_len,
	    dataset, ds_len, mntopts, opt_len));
}

void
sa_mnttab_cache(boolean_t enable)
{
	safs_mnttab_cache(enable);
}

/*
 * sa_share_find_init
 *
 * setup to iterate shares in cache
 */
int
sa_share_find_init(const char *mntpnt, sa_proto_t proto, void **hdl)
{
	return (sacache_share_find_init(mntpnt, proto, hdl));
}

/*
 * sa_share_find_next
 */
int
sa_share_find_next(void *hdl, nvlist_t **share)
{
	return (sacache_share_find_next(hdl, share));
}

/*
 * sa_share_find_fini
 */
void
sa_share_find_fini(void *hdl)
{
	(void) sacache_share_find_fini(hdl);
}

/*
 * sa_sharing_enabled
 */
sa_proto_t
sa_sharing_enabled(const char *mntpnt)
{
	return (safs_sharing_enabled(mntpnt));
}

/*
 * sa_sharing_get_prop
 *
 * Return the sharenfs/sharesmb property value from ZFS dataset
 * Non zfs datasets should return "on" or "off"
 * The caller MUST free the returned buffer.
 */
int
sa_sharing_get_prop(const char *mntpnt, sa_proto_t prot, char **sh_props)
{
	return (safs_sharing_get_prop(mntpnt, prot, sh_props));
}

/*
 * sa_sharing_set_prop
 *
 * This method sets the sharesmb/sharenfs property.
 */
int
sa_sharing_set_prop(const char *mntpnt, sa_proto_t proto, char *sh_prop)
{
	return (safs_sharing_set_prop(mntpnt, proto, sh_prop));
}

/*
 * sa_share_set_def_proto
 *
 * Set the default protocol property list for each protocol specified.
 * Call the appropriate protocol plugin to create the property list.
 */
int
sa_share_set_def_proto(nvlist_t *share, sa_proto_t proto)
{
	sa_proto_t p;
	int rc, ret = SA_OK;

	for (p = sa_proto_first(); p != SA_PROT_NONE; p = sa_proto_next(p)) {
		if (p & proto) {
			if (sa_share_get_proto(share, p) == NULL) {
				rc = saproto_share_set_def_proto(p, share);
				if (rc != SA_OK)
					ret = rc;
			}
		}
	}

	return (ret);
}

int
sa_share_format_props(nvlist_t *proplist, sa_proto_t proto, char **retbuf)
{
	return (saproto_share_format_props(proplist, proto, retbuf));
}

/*
 * sa_path_in_current_zone
 *
 * Returns B_TRUE if the path is on a mountpoint
 * that is shareable in the current zone.
 */
boolean_t
sa_path_in_current_zone(const char *path)
{
	int rc;
	char mntpnt[MAXPATHLEN];
	char mntopts[MNT_LINE_MAX];

	if (path == NULL)
		return (B_FALSE);

	rc = sa_get_mntpnt_for_path(path, mntpnt, sizeof (mntpnt), NULL, 0,
	    mntopts, sizeof (mntopts));
	if (rc != SA_OK)
		return (B_FALSE);

	return (sa_mntpnt_in_current_zone(mntpnt, mntopts));
}

/*
 * sa_mntent_is_shareable
 *
 * INPUTS:
 *   pointer to mnttab entry to validate
 *
 * RETURNS:
 *   SA_OK: mntpnt is a shareable fstype and in current zone
 *   SA_INVALID_PROP: invalid mntab entry
 *   SA_INVALID_FSTYPE: mntpnt is not a shareable file system type
 *   SA_SHARE_OTHERZONE: mntpnt is not in current zone
 */
int
sa_mntent_is_shareable(struct mnttab *entry)
{
	if (entry == NULL || entry->mnt_mountp == NULL ||
	    entry->mnt_mntopts == NULL)
		return (SA_INVALID_PROP);

	if (!sa_fstype_is_shareable(entry->mnt_fstype))
		return (SA_INVALID_FSTYPE);

	if (!sa_mntpnt_in_current_zone(entry->mnt_mountp, entry->mnt_mntopts))
		return (SA_SHARE_OTHERZONE);

	return (SA_OK);
}

/*
 * sa_path_is_shareable
 *
 * RETURNS:
 *   SA_OK: path exists, is a shareable fstype and exits in the current zone.
 *   SA_INVALID_SHARE_PATH: invalid input data
 *   SA_NO_PERMISSION: access denied to path
 *   SA_PATH_NOT_FOUND: path does not exist
 *   SA_SYSTEM_ERR: could not stat file
 *   SA_INVALID_FSTYPE: not a shareable file system type
 *   SA_SHARE_OTHERZONE: path is not in current zone
 */
int
sa_path_is_shareable(const char *path)
{
	struct stat st;

	if (path == NULL)
		return (SA_INVALID_SHARE_PATH);

	if (stat(path, &st) != 0) {
		switch (errno) {
		case EACCES:
			return (SA_NO_PERMISSION);
		case ENOENT:
		case ENOLINK:
		case ENOTDIR:
			return (SA_PATH_NOT_FOUND);
		default:
			return (SA_SYSTEM_ERR);
		}
	}

	if (!sa_fstype_is_shareable(st.st_fstype))
		return (SA_INVALID_FSTYPE);

	if (!sa_path_in_current_zone(path))
		return (SA_SHARE_OTHERZONE);

	return (SA_OK);
}
