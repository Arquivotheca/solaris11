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
 * This file converts shares in libshare v1 format to libshare v2 format.
 * The configuration of libshare v1 shares is stored in the
 * svc:/network/shares/group:<groupname> SMF service instances.
 * This configuration is read and converted into nvlist format of
 * libshare v2. Additionally, libshare v1 has the concept of groups, shares and
 * resources. Groups can contain shares and shares can have different resource
 * names. Properties can be defined at each of the 3 levels described above.
 * The group properties are inherited by shares and the share properties are
 * inherited by resources.
 *
 * We start out by parsing out the SMF configuration of the share into a
 * nvlist format. We then merge the group and share properties
 * into the newly defined nvlist share for libshare v2.
 *
 * Example
 * -------
 *
 * Consider a share defined in libshare v1 format. The share belongs to group
 * named "smb". This group has "rw=*" property setting. The shared path is
 * /export/home and it has "none=*" property setting. The share is
 * named with resource name "exp" and has a description of "comment".
 * Additionally the resource has "rw=.CIFSDEVDC, abe=true, csc=manual"
 * properties.
 *
 * smb smb=(rw="*")
 *  /export/home   smb=(none="*")
 *    exp=/export/home  smb=(rw=".CIFSDEVDC" abe="true" csc="manual") "comment"
 *
 * Format of the share in SMF configuration
 * ----------------------------------------
 *
 * Group names start with "optionset" and share and resource names
 * start with "S-". The share (with path "/export/home") with resource name
 * "exp" belongs to the "smb" group.
 *
 * svc:/network/shares/group:smb> listprop
 *	:
 * optionset_smb                                     application
 * optionset_smb/rw                                  astring  *
 * S-d49965d5-a91b-4b40-996d-bff3e535e582            application
 * S-d49965d5-a91b-4b40-996d-bff3e535e582/path       astring  /export/home
 * S-d49965d5-a91b-4b40-996d-bff3e535e582/resource   astring  "1:exp:comment"
 * S-d49965d5-a91b-4b40-996d-bff3e535e582_smb_1      application
 * S-d49965d5-a91b-4b40-996d-bff3e535e582_smb_1/rw   astring  .CIFSDEVDC
 * S-d49965d5-a91b-4b40-996d-bff3e535e582_smb_1/abe  astring  true
 * S-d49965d5-a91b-4b40-996d-bff3e535e582_smb_1/csc  astring  manual
 * S-d49965d5-a91b-4b40-996d-bff3e535e582_smb        application
 * S-d49965d5-a91b-4b40-996d-bff3e535e582_smb/none   astring  *
 *
 * Format of the nvlist that is created from the share in SMF
 * ----------------------------------------------------------
 *
 * The SMF configuration is parsed into a nvlist. Below is the output of the
 * nvlist created by parsing the SMF configuration of the share.
 *
 * sharecfg:
 *   smb:
 *       gname: smb
 *       smb:
 *           rw: *
 *       S-d49965d5-a91b-4b40-996d-bff3e535e582:
 *           id: S-d49965d5-a91b-4b40-996d-bff3e535e582
 *           path: /export/home
 *           1:
 *               name: exp
 *               path: /export/home
 *               desc: comment
 *               smb:
 *                    rw: .CIFSDEVDC
 *                    abe: true
 *                    csc: manual
 *           smb:
 *                none: *
 *
 * Format of libshare v2  nvlist
 * -----------------------------
 *
 * The above nvlist is converted in to libshare v2 nvlist by merging the
 * properties of groups and share into nvlist for libshare2.
 *
 * exp:
 *    name: exp
 *    path: /export/home
 *    desc: comment
 *    smb:
 *        none: *
 *        rw: .CIFSDEVDC
 *        abe: true
 *        csc: manual
 *
 * The above nvlist configuration is written to persistent repository defined
 * by libshare v2.
 */

#include "libshare.h"
#include "libshare_impl.h"
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <uuid/uuid.h>
#include <sys/param.h>
#include <signal.h>
#include <syslog.h>
#include <sys/varargs.h>
#include <sys/time.h>
#include <libintl.h>
#include <strings.h>
#include <libscf.h>

ssize_t scf_max_name_len;

#define	SCH_STATE_UNINIT	0
#define	SCH_STATE_INITIALIZING	1
#define	SCH_STATE_INIT	2

/*
 * Shares are held in a property group with name of the form
 * S-<GUID>.  The total length of the name is 38 characters.
 */
#define	SA_SHARE_PG_PREFIX	"S-"
#define	SA_SHARE_PG_PREFIXLEN	2
#define	SA_SHARE_PG_LEN		38
#define	SA_MAX_NAME_LEN		100

/*
 * service instance related defines
 */
#define	SA_GROUP_SVC_NAME	"network/shares/group"

typedef struct scfutilhandle {
	scf_handle_t		*handle;
	int			scf_state;
	scf_service_t		*service;
	scf_scope_t		*scope;
	scf_transaction_t	*trans;
	scf_transaction_entry_t	*entry;
	scf_propertygroup_t	*pg;
	scf_instance_t		*instance;
} scfutilhandle_t;

/*
 * The SMF facility uses some properties that must exist. We want to
 * skip over these when processing protocol options.
 */
static char *skip_props[] = {
	"modify_authorization",
	"action_authorization",
	"value_authorization",
	NULL
};

static void sa_upgrade_error(int, const char *, ...);

/*
 * sa_upgrade_scf_fini
 *
 * Must be called when done. Called with the handle allocated in
 * sa_upgrade_scf_init(), it cleans up the state and frees any SCF resources
 * still in use.
 */
static void
sa_upgrade_scf_fini(scfutilhandle_t *handle)
{
	int unbind = 0;

	if (handle != NULL) {
		if (handle->scope != NULL) {
			unbind = 1;
			scf_scope_destroy(handle->scope);
		}
		if (handle->instance != NULL)
			scf_instance_destroy(handle->instance);
		if (handle->service != NULL)
			scf_service_destroy(handle->service);
		if (handle->pg != NULL)
			scf_pg_destroy(handle->pg);
		if (handle->handle != NULL) {
			handle->scf_state = SCH_STATE_UNINIT;
			if (unbind)
				(void) scf_handle_unbind(handle->handle);
			scf_handle_destroy(handle->handle);
		}
		free(handle);
	}
}

/*
 * sa_upgrade_scf_init
 *
 * Must be called before using any of the SCF functions. It initializes and
 * returns handle to SCF database.
 */
static scfutilhandle_t *
sa_upgrade_scf_init(void)
{
	scfutilhandle_t *handle;

	scf_max_name_len = scf_limit(SCF_LIMIT_MAX_NAME_LENGTH);
	if (scf_max_name_len <= 0)
		scf_max_name_len = SA_MAX_NAME_LEN + 1;

	handle = calloc(1, sizeof (scfutilhandle_t));
	if (handle == NULL)
		return (handle);

	handle->scf_state = SCH_STATE_INITIALIZING;
	handle->handle = scf_handle_create(SCF_VERSION);
	if (handle->handle == NULL) {
		free(handle);
		handle = NULL;
		sa_upgrade_error(SA_SCF_ERROR,
		    "libshare could not access SMF repository: %s",
		    scf_strerror(scf_error()));
		return (handle);
	}
	if (scf_handle_bind(handle->handle) != 0)
		goto err;

	handle->scope = scf_scope_create(handle->handle);
	handle->service = scf_service_create(handle->handle);
	handle->pg = scf_pg_create(handle->handle);

	handle->instance = scf_instance_create(handle->handle);
	if (handle->scope == NULL || handle->service == NULL ||
	    handle->pg == NULL || handle->instance == NULL)
		goto err;
	if (scf_handle_get_scope(handle->handle,
	    SCF_SCOPE_LOCAL, handle->scope) != 0)
		goto err;
	if (scf_scope_get_service(handle->scope,
	    SA_GROUP_SVC_NAME, handle->service) != 0)
		goto err;

	handle->scf_state = SCH_STATE_INIT;

	return (handle);

err:
	(void) sa_upgrade_scf_fini(handle);
	sa_upgrade_error(SA_SCF_ERROR,
	    "libshare SMF initialization problem: %s",
	    scf_strerror(scf_error()));
	return (NULL);
}

/*
 * sa_upgrade_get_scf_limit
 *
 * Since we use  scf_limit a lot and do the same  check and return the
 * same  value  if  it  fails,   implement  as  a  function  for  code
 * simplification.  Basically, if  name isn't found, return MAXPATHLEN
 * (1024) so we have a reasonable default buffer size.
 */
static ssize_t
sa_upgrade_get_scf_limit(uint32_t name)
{
	ssize_t vallen;

	vallen = scf_limit(name);
	if (vallen == (ssize_t)-1)
		vallen = MAXPATHLEN;
	return (vallen);
}

/*
 * sa_upgrade_skip_property
 *
 * Internal function to check to see if a property is an SMF magic
 * property that needs to be skipped.
 */
static int
sa_upgrade_skip_property(char *name)
{
	int i;

	for (i = 0; skip_props[i] != NULL; i++)
		if (strcmp(name, skip_props[i]) == 0)
			return (1);
	return (0);
}

/*
 * sa_upgrade_dump_list
 *
 * Debug routine to dump nvlist. Called from sa_upgrade_dump method.
 */
static void
sa_upgrade_dump_list(nvlist_t *list, int ident, boolean_t is_log_smf)
{
	nvlist_t	*nvlist_value;
	nvpair_t	*nvp;
	char		*attrname, *str;

	nvp = nvlist_next_nvpair(list, NULL);
	while (nvp != NULL) {
		attrname = nvpair_name(nvp);
		switch (nvpair_type(nvp)) {
		case DATA_TYPE_STRING:
			(void) nvpair_value_string(nvp, &str);
			salog_debug(0, "%*s%s: %s", ident, "", attrname, str);
			if (is_log_smf)
				(void) fprintf(stderr, "%*s%s: %s\n", ident, "",
				    attrname, str);
			break;

		case DATA_TYPE_NVLIST:
			(void) nvpair_value_nvlist(nvp, &nvlist_value);
			salog_debug(0, "%*s%s:", ident, "", nvpair_name(nvp));
			if (is_log_smf)
				(void) fprintf(stderr, "%*s%s:\n", ident, "",
				    nvpair_name(nvp));
			sa_upgrade_dump_list(nvlist_value,
			    ident + 4, is_log_smf);
			break;

		default:
			salog_debug(0, "%s: UNSUPPORTED TYPE\n", attrname);
			if (is_log_smf)
				(void) fprintf(stderr, "%s: UNSUPPORTED TYPE\n",
				    attrname);
			break;
		}
		nvp = nvlist_next_nvpair(list, nvp);
	}
}

/*
 * sa_upgrade_dump
 *
 * Debug routine to dump nvlist. Optionally, an header can be provided before
 * dumping the list. If is_log_smb is set to true, the output is logged to SMF
 * service logs.
 */
static void
sa_upgrade_dump(nvlist_t *list, boolean_t is_log_smf, char *title)
{
	if (title != NULL) {
		salog_debug(0, "%s", title);
		if (is_log_smf)
			(void) fprintf(stderr, "%s\n", title);
	}

	sa_upgrade_dump_list(list, 2, is_log_smf);
}

/*
 * sa_upgrade_get_nvlist
 *
 * Get nvlist associated with the passed "name" parameter.
 */
static nvlist_t *
sa_upgrade_get_nvlist(nvlist_t *list, char *name)
{
	nvlist_t *rlist = NULL;

	if ((list == NULL) || (name == NULL))
		return (NULL);

	if (nvlist_lookup_nvlist(list, name, &rlist) != 0)
		return (NULL);
	return (rlist);
}

/*
 * sa_upgrade_map_nvlist_errcodes
 *
 * This routine maps nvlist API error codes to libshare v2 error codes.
 */
static int
sa_upgrade_map_nvlist_errcodes(int errcode)
{
	int sa_errcode;

	switch (errcode) {
	case 0:
		sa_errcode = SA_OK;
		break;
	case ENOMEM:
		sa_errcode = SA_NO_MEMORY;
		break;
	case EINVAL:
		sa_errcode = SA_INVALID_PROP;
		break;
	default:
		sa_errcode = SA_INTERNAL_ERR;
	}
	return (sa_errcode);
}

/*
 * sa_upgrade_nvlist_merge
 *
 * This method provides a wrapper around nvlist_merge API and returns
 * libshare v2 error codes.
 */
static int
sa_upgrade_nvlist_merge(nvlist_t *dst, nvlist_t *nvl, int flag)
{
	int ret;

	ret = nvlist_merge(dst, nvl, flag);

	return (sa_upgrade_map_nvlist_errcodes(ret));
}

/*
 * sa_upgrade_add_nvlist
 *
 * This method provides a wrapper around nvlist_add_nvlist API and returns
 * libshare v2 error codes.
 */
static int
sa_upgrade_add_nvlist(nvlist_t *list, char *prop, nvlist_t *nlist)
{
	int ret;

	ret = nvlist_add_nvlist(list, prop, nlist);

	return (sa_upgrade_map_nvlist_errcodes(ret));
}

/*
 * sa_upgrade_extract_resource
 *
 * Extract a resource node from the share node. The resource node is
 * stored in "valuestr" whose format is,
 *  "<id1>:<name1>:<description1>" "<id2>:<name2>:<description2>"
 *
 * For example:
 *  S-{uuid}/resource   astring  "1:exp:" "2:exp1:test-descrip"
 */
static int
sa_upgrade_extract_resource(nvlist_t *share, char *path, char *valuestr)
{
	char *idx;
	char *name;
	char *description = NULL;
	nvlist_t *resource = NULL;
	int ret = SA_OK;

	if ((path == NULL) || (valuestr == NULL))
		return (SA_INVALID_PROP);

	idx = valuestr;
	name = strchr(valuestr, ':');
	if (name == NULL) {
		idx = "0";
		name = valuestr;
	} else {
		*name++ = '\0';
		description = strchr(name, ':');
		if (description != NULL)
			*description++ = '\0';
	}

	resource = sa_share_alloc(name, path);
	if (resource == NULL) {
		ret = SA_NO_MEMORY;
	} else {
		if (description != NULL && strlen(description) > 0)
			ret = sa_share_set_prop(resource, "desc", description);
			if (ret == SA_OK)
				ret = sa_upgrade_add_nvlist(share, idx,
				    resource);
		sa_share_free(resource);
	}

	if (ret != SA_OK)
		sa_upgrade_error(ret,
		    "error parsing resource for %s", path);

	return (ret);
}

/*
 * sa_upgrade_extract_share_prop
 *
 * Extract share properties from the SMF property group.
 */
static int
sa_upgrade_extract_share_prop(nvlist_t *group, scfutilhandle_t *handle,
    scf_propertygroup_t *pg, char *id)
{
	scf_iter_t *iter = NULL;
	scf_property_t *prop = NULL;
	scf_value_t *value = NULL;
	ssize_t vallen;
	char *name = NULL;
	char *valuestr = NULL;
	char *sectype = NULL;
	char *proto;
	int ret = SA_OK;
	nvlist_t *node, *pnode, *share, *resource;
	nvlist_t *prot = NULL;
	nvlist_t *sec = NULL;
	boolean_t is_sec = B_FALSE;
	boolean_t is_prot = B_FALSE;

	vallen = strlen(id);
	if (*id != SA_SHARE_PG_PREFIX[0] || vallen <= SA_SHARE_PG_LEN)
		return (SA_OK);

	if (strncmp(id, SA_SHARE_PG_PREFIX, SA_SHARE_PG_PREFIXLEN) == 0) {
		proto = strchr(id, '_');
		if (proto == NULL)
			return (SA_OK);
		*proto++ = '\0';
		if (*proto == '\0')
			return (SA_OK);
		sectype = strchr(proto, '_');
		if (sectype != NULL)
			*sectype++ = '\0';
	}

	share = sa_upgrade_get_nvlist(group, id);
	if (share == NULL)
		return (SA_INVALID_SHARE);
	node = share;

	if (sectype != NULL) {
		/*
		 * If sectype[0] is a digit, then it is an index into
		 * the resource names. We need to find a resource
		 * record and then get the properties into an
		 * optionset. The optionset becomes the "node" and the
		 * rest is hung off of the share.
		 */
		if (isdigit((int)*sectype)) {
			resource = sa_upgrade_get_nvlist(share, sectype);
			if (resource == NULL) {
				ret = SA_INVALID_PROP;
				goto out;
			}
			node = resource;
		} else {
			sec = sa_share_alloc(NULL, NULL);
			if (sec == NULL) {
				ret = SA_NO_MEMORY;
				goto out;
			}
			is_sec = B_TRUE;
		}
	}

	if ((prot = sa_upgrade_get_nvlist(node, proto)) == NULL) {
		prot = sa_share_alloc(NULL, NULL);
		if (prot == NULL) {
			ret = SA_NO_MEMORY;
			goto out;
		}
		is_prot = B_TRUE;
	}
	pnode = (is_sec) ? sec : prot;

	vallen = sa_upgrade_get_scf_limit(SCF_LIMIT_MAX_VALUE_LENGTH);
	iter = scf_iter_create(handle->handle);
	value = scf_value_create(handle->handle);
	prop = scf_property_create(handle->handle);
	name = malloc(scf_max_name_len);
	valuestr = malloc(vallen);
	if (iter == NULL || value == NULL || prop == NULL ||
	    name == NULL || valuestr == NULL) {
		ret = SA_NO_MEMORY;
		goto out;
	}

	if (scf_iter_pg_properties(iter, pg) == 0) {
		while (scf_iter_next_property(iter, prop) > 0) {
			ret = SA_SCF_ERROR;
			if (scf_property_get_name(prop, name,
			    scf_max_name_len) > 0) {
				if (scf_property_get_value(prop, value) == 0) {
					if (scf_value_get_astring(value,
					    valuestr, vallen) >= 0)
						ret = sa_share_set_prop(pnode,
						    name, valuestr);
				}
			}
			if (ret != SA_OK)
				break;
		}
	} else {
		ret = SA_SCF_ERROR;
	}

	if ((ret == SA_OK) && (proto != NULL)) {
		if (is_sec)
			ret = sa_upgrade_add_nvlist(prot, sectype, sec);
		if (ret == SA_OK)
			ret = sa_upgrade_add_nvlist(node, proto, prot);
	}
out:
	if (ret != SA_OK)
		sa_upgrade_error(ret,
		    "error parsing properties for share id %s.", id);
	if (is_prot && (prot != NULL))
		sa_share_free(prot);
	if (is_sec && (sec != NULL))
		sa_share_free(sec);
	if (iter != NULL)
		scf_iter_destroy(iter);
	if (value != NULL)
		scf_value_destroy(value);
	if (prop != NULL)
		scf_property_destroy(prop);
	if (name != NULL)
		free(name);
	if (valuestr != NULL)
		free(valuestr);
	return (ret);
}

/*
 * sa_upgrade_extract_share
 *
 * Extract the share definition from the share property group.
 */
static int
sa_upgrade_extract_share(nvlist_t *group, scfutilhandle_t *handle,
    scf_propertygroup_t *pg, char *id)
{
	scf_iter_t *iter, *viter;
	scf_property_t *prop;
	scf_value_t *value;
	ssize_t vallen;
	char *name, *valuestr, *sh_name;
	int ret = SA_OK;
	boolean_t have_path = B_FALSE;
	boolean_t have_resource = B_FALSE;
	char path[MAXNAMELEN];
	uuid_t uuid;
	nvlist_t *share;

	vallen = strlen(id);
	if (*id == SA_SHARE_PG_PREFIX[0] && vallen == SA_SHARE_PG_LEN) {
		if ((strncmp(id, SA_SHARE_PG_PREFIX,
		    SA_SHARE_PG_PREFIXLEN) != 0) ||
		    (uuid_parse(id + SA_SHARE_PG_PREFIXLEN, uuid) < 0))
			return (SA_INVALID_UID);
	} else {
		return (SA_OK);
	}

	vallen = sa_upgrade_get_scf_limit(SCF_LIMIT_MAX_VALUE_LENGTH);
	iter = scf_iter_create(handle->handle);
	value = scf_value_create(handle->handle);
	prop = scf_property_create(handle->handle);
	name = malloc(scf_max_name_len);
	valuestr = malloc(vallen);
	if (iter == NULL || value == NULL || prop == NULL ||
	    name == NULL || valuestr == NULL) {
		ret = SA_NO_MEMORY;
		goto out;
	}

	if (scf_iter_pg_properties(iter, pg) != 0) {
		ret = SA_SCF_ERROR;
		goto out;
	}

	share = sa_share_alloc(NULL, NULL);
	if (share == NULL) {
		ret = SA_NO_MEMORY;
		goto out;
	}

	ret = sa_share_set_prop(share, "id", id);
	if (ret != SA_OK) {
		sa_share_free(share);
		goto out;
	}

	while (scf_iter_next_property(iter, prop) > 0) {
		ret = SA_SCF_ERROR;
		if (scf_property_get_name(prop, name, scf_max_name_len) > 0) {
			if ((scf_property_get_value(prop, value) == 0) &&
			    (scf_value_get_astring(value,
			    valuestr, vallen) >= 0))
				ret = SA_OK;
			else if (strcmp(name, "resource") == 0)
				ret = SA_OK;
		}
		if (ret != SA_OK)
			break;

		if (strcmp(name, "path") == 0) {
			have_path = B_TRUE;
			(void) strlcpy(path, valuestr, MAXNAMELEN);
			ret = sa_share_set_prop(share, "path", valuestr);
		} else if (strcmp(name, "description") == 0) {
			ret = sa_share_set_prop(share, "desc", valuestr);
		} else if (strcmp(name, "resource") == 0) {
			viter = scf_iter_create(handle->handle);
			have_resource = B_TRUE;
			if (viter != NULL && have_path &&
			    scf_iter_property_values(viter, prop) == 0) {
				while (scf_iter_next_value(viter, value) > 0) {
					if (scf_value_get_ustring(value,
					    valuestr, vallen) >= 0)
						ret =
						    sa_upgrade_extract_resource(
						    share, path, valuestr);
					else if (scf_value_get_astring(value,
					    valuestr, vallen) >= 0)
						ret =
						    sa_upgrade_extract_resource(
						    share, path, valuestr);
					if (ret != SA_OK)
						break;
				}
				scf_iter_destroy(viter);
			}
		}
		if (ret != SA_OK)
			break;
	}

	/*
	 * Some nfs shares do not have resource names. The resource names
	 * for these shares are made up using the share path.
	 */
	if ((ret == SA_OK) && have_path && !have_resource) {
		sh_name = strdup(path);
		if (sh_name == NULL) {
			ret = SA_NO_MEMORY;
			goto out;
		}
		sa_path_to_shr_name(sh_name);
		ret = sa_upgrade_extract_resource(share, path, sh_name);
		free(sh_name);
	}

	if (ret == SA_OK)
		ret = sa_upgrade_add_nvlist(group, id, share);
	sa_share_free(share);
out:
	if (ret != SA_OK)
		sa_upgrade_error(ret, "error parsing share id %s", id);
	if (name != NULL)
		free(name);
	if (valuestr != NULL)
		free(valuestr);
	if (value != NULL)
		scf_value_destroy(value);
	if (iter != NULL)
		scf_iter_destroy(iter);
	if (prop != NULL)
		scf_property_destroy(prop);
	return (ret);
}

/*
 * sa_upgrade_extract_group_prop
 *
 * Extract the name property group and create the specified type of
 * nvlist from the provided group. The nvlist will be of type  "optionset"
 * or "security".
 */
static int
sa_upgrade_extract_group_prop(nvlist_t *group, scfutilhandle_t *handle,
    scf_propertygroup_t *pg, char *proto, char *sectype)
{
	scf_iter_t *iter;
	scf_property_t *prop;
	scf_value_t *value;
	char *name, *valuestr;
	ssize_t vallen;
	nvlist_t *prot = NULL;
	nvlist_t *sec = NULL;
	nvlist_t *pnode = NULL;
	int ret = SA_OK;
	boolean_t is_sec = B_FALSE;
	boolean_t is_prot = B_FALSE;

	if (proto == NULL)
		return (SA_INVALID_PROP_VAL);

	vallen = sa_upgrade_get_scf_limit(SCF_LIMIT_MAX_VALUE_LENGTH);
	iter = scf_iter_create(handle->handle);
	value = scf_value_create(handle->handle);
	prop = scf_property_create(handle->handle);
	name = malloc(scf_max_name_len);
	valuestr = malloc(vallen);
	if (iter == NULL || value == NULL || prop == NULL ||
	    valuestr == NULL || name == NULL) {
		ret = SA_NO_MEMORY;
		goto out;
	}

	if ((prot = sa_upgrade_get_nvlist(group, proto)) == NULL) {
		prot = sa_share_alloc(NULL, NULL);
		if (prot == NULL) {
			ret = SA_NO_MEMORY;
			goto out;
		}
		pnode = prot;
		is_prot = B_TRUE;
	} else if (sectype != NULL) {
		sec = sa_share_alloc(NULL, NULL);
		if (sec == NULL) {
			ret = SA_NO_MEMORY;
			goto out;
		}
		pnode = sec;
		is_sec = B_TRUE;
	}

	if (scf_iter_pg_properties(iter, pg) == 0) {
		while (scf_iter_next_property(iter, prop) > 0) {
			ret = SA_SCF_ERROR;
			if (scf_property_get_name(prop, name,
			    scf_max_name_len) > 0) {
				if (sa_upgrade_skip_property(name))
					continue;

				if (scf_property_get_value(prop, value) == 0) {
					if (scf_value_get_astring(value,
					    valuestr, vallen) >= 0)
						ret = sa_share_set_prop(pnode,
						    name, valuestr);
				}
			}
			if (ret != SA_OK)
				break;
		}
	} else {
		ret = SA_SCF_ERROR;
	}

	if ((ret == SA_OK) && (proto != NULL)) {
		if (sectype != NULL)
			ret = sa_upgrade_add_nvlist(prot, sectype, sec);
		if (ret == SA_OK)
			ret = sa_upgrade_add_nvlist(group, proto, prot);
	}
out:
	if (ret != SA_OK)
		sa_upgrade_error(ret,
		    "error while parsing group properties.");
	if (is_prot && (prot != NULL))
		sa_share_free(prot);
	if (is_sec && (sec != NULL))
		sa_share_free(sec);
	if (value != NULL)
		scf_value_destroy(value);
	if (iter != NULL)
		scf_iter_destroy(iter);
	if (prop != NULL)
		scf_property_destroy(prop);
	if (name != NULL)
		free(name);
	if (valuestr != NULL)
		free(valuestr);
	return (ret);
}

/*
 * sa_upgrade_extract_group
 *
 * Get the config info for the instance of a group and create an
 * nvlist from it.
 */
static int
sa_upgrade_extract_group(nvlist_t *glist, scfutilhandle_t *handle,
    scf_instance_t *instance)
{
	scf_iter_t *iter;
	char *proto, *sectype, *buff;
	boolean_t have_shares = B_FALSE;
	boolean_t have_proto = B_FALSE;
	int ret = SA_OK;
	int err;
	char gname[MAXNAMELEN];
	nvlist_t *group;

	buff = malloc(scf_max_name_len);
	if (buff == NULL)
		return (SA_NO_MEMORY);

	iter = scf_iter_create(handle->handle);
	if (iter == NULL) {
		ret = SA_NO_MEMORY;
		goto out;
	}

	if (scf_instance_get_name(instance, buff, scf_max_name_len) > 0) {
		if (scf_iter_instance_pgs(iter, instance) != 0) {
			ret = SA_SCF_ERROR;
			goto out;
		}

		group = sa_share_alloc(NULL, NULL);
		if (group == NULL) {
			ret = SA_NO_MEMORY;
			goto out;
		}
		ret = sa_share_set_prop(group, "gname", buff);
		if (ret != SA_OK) {
			sa_share_free(group);
			goto out;
		}
		(void) strlcpy(gname, buff, MAXNAMELEN);

		/*
		 * Iterate through all the property groups. Property groups
		 * starting with "optionset" prefixes are for groups. Property
		 * groups starting with "S-" prefix are for shares and
		 * resources.
		 */
		while (scf_iter_next_pg(iter, handle->pg) > 0) {
			err = scf_pg_get_name(handle->pg, buff,
			    scf_max_name_len);
			if (err <= 0)
				continue;

			if (buff[0] == SA_SHARE_PG_PREFIX[0]) {
				ret = sa_upgrade_extract_share(group, handle,
				    handle->pg, buff);
				have_shares = B_TRUE;
			} else if (strncmp(buff, "optionset", 9) == 0) {
				sectype = proto = NULL;
				proto = strchr(buff, '_');
				if (proto != NULL) {
					*proto++ = '\0';
					have_proto = B_TRUE;
					sectype = strchr(proto, '_');
					if (sectype != NULL)
						*sectype++ = '\0';
				} else if (strlen(buff) > 9) {
					continue;
				}

				ret = sa_upgrade_extract_group_prop(group,
				    handle, handle->pg, proto, sectype);
			}
			if (ret != SA_OK)
				break;
		}

		/*
		 * A share group in a libshare configuration must have a
		 * protocol specified. If we cannot get a group protocol,
		 * mark it as an error. If no share and protocol is defined
		 * for a group, then just delete that group.
		 */
		if (!have_proto) {
			if (have_shares) {
				ret = SA_NO_SHARE_PROTO;
				sa_upgrade_dump(group, B_TRUE,
				    "GROUP CONFIGURATION");
			} else {
				sa_share_free(group);
				ret = SA_OK;
				goto out;
			}
		}

		/*
		 * Do a second pass to get share/resource properties. This is
		 * needed as property groups are not sorted in SMF manifest.
		 */
		if ((ret == SA_OK) && have_shares &&
		    scf_iter_instance_pgs(iter, instance) == 0) {
			while (scf_iter_next_pg(iter, handle->pg) > 0) {
				err = scf_pg_get_name(handle->pg, buff,
				    scf_max_name_len);
				if (err  <= 0)
					continue;

				if (buff[0] == SA_SHARE_PG_PREFIX[0]) {
					ret = sa_upgrade_extract_share_prop(
					    group, handle, handle->pg, buff);
					if (ret != SA_OK)
						break;
				}
			}
		}

		if (ret == SA_OK)
			ret = sa_upgrade_add_nvlist(glist, gname, group);
		sa_share_free(group);
	}
out:
	if (ret != SA_OK)
		sa_upgrade_error(ret, "error parsing group %s.", gname);
	if (iter != NULL)
		scf_iter_destroy(iter);
	if (buff != NULL)
		free(buff);
	return (ret);
}

/*
 * sa_upgrade_instance_is_enabled
 *
 * Returns B_TRUE if SMF group instance is in online or offline state.
 * The offline state is required as the start method of the SMF manifest
 * will call the upgrade function.
 */
static boolean_t
sa_upgrade_instance_is_enabled(scf_instance_t *instance)
{
	char *state, *fmri;
	int fmri_len, ret;

	fmri_len = scf_limit(SCF_LIMIT_MAX_FMRI_LENGTH);
	fmri = malloc(fmri_len + 1);
	if (fmri == NULL)
		return (B_FALSE);
	ret = scf_instance_to_fmri(instance, fmri, fmri_len + 1);
	if (ret == -1) {
		free(fmri);
		return (B_FALSE);
	}

	state = smf_get_state(fmri);
	free(fmri);
	if (state == NULL)
		return (B_FALSE);

	if ((strcmp(state, "online") == 0) || (strcmp(state, "offline") == 0))
		return (B_TRUE);

	return (B_FALSE);
}

/*
 * sa_upgrade_get_config
 *
 * This method gets the upgrade libshare v1 configuration from a SMF group
 * instance (/network/shares/group:*) and return it in nvlist format.
 * It skips the "zfs" group instances.
 */
static int
sa_upgrade_get_config(scfutilhandle_t *handle, nvlist_t *root, char *gname,
    boolean_t force_upgrade)
{
	int ret = SA_OK;
	nvlist_t *glist;
	scf_instance_t *instance;

	instance = scf_instance_create(handle->handle);
	if (scf_service_get_instance(handle->service, gname,
	    instance) != SCF_SUCCESS) {
		ret = SA_SCF_ERROR;
		goto out;
	}

	if (!force_upgrade && !sa_upgrade_instance_is_enabled(instance)) {
		ret = SA_SYSTEM_ERR;
		sa_upgrade_error(ret,
		    "svc:/network/shares/group:%s is not enabled", gname);
		return (ret);
	}

	glist = sa_share_alloc(NULL, NULL);
	if (glist == NULL) {
		ret = SA_NO_MEMORY;
		goto out;
	}

	ret = sa_upgrade_extract_group(glist, handle, instance);
	if (ret == SA_OK)
		ret = sa_upgrade_add_nvlist(root, "sharecfg", glist);
	sa_share_free(glist);

out:
	if (ret != SA_OK)
		sa_upgrade_error(ret,
		    "error getting configuration for group %s", gname);
	if (instance != NULL)
		scf_instance_destroy(instance);
	return (ret);
}

/*
 * sa_upgrade_delete_config
 *
 * This method deletes the SMF group instance (/network/shares/group:*),
 * which stores the upgrade libshare v1 configuration.
 */
static int
sa_upgrade_delete_config(scfutilhandle_t *handle, char *gname,
    boolean_t force_upgrade)
{
	int ret = SA_OK;
	scf_instance_t *instance;

	instance = scf_instance_create(handle->handle);
	if (scf_service_get_instance(handle->service, gname,
	    instance) != SCF_SUCCESS) {
		ret = SA_SCF_ERROR;
		goto out;
	}

	if (!force_upgrade && !sa_upgrade_instance_is_enabled(instance)) {
		ret = SA_SYSTEM_ERR;
		sa_upgrade_error(ret,
		    "svc:/network/shares/group:%s is not enabled", gname);
		return (ret);
	}

	ret = scf_instance_delete(instance);
	if (ret != 0)
		ret = SA_SCF_ERROR;

out:
	if (instance != NULL)
		scf_instance_destroy(instance);
	if (ret != SA_OK)
		sa_upgrade_error(ret, "error deleting group %s", gname);
	return (ret);
}

/*
 * sa_upgrade_merge_sec
 *
 * This method merges the "security" properties for a group and/or a share
 * with the "security" properties for a resource.
 */
static int
sa_upgrade_merge_sec(nvlist_t *src_proto, nvlist_t *dest_proto)
{
	nvlist_t *sec, *dsec, *tmp_sec;
	nvpair_t *nvp;
	char *secname;
	int ret = SA_OK;

	sec = dsec = tmp_sec = NULL;
	for (nvp = nvlist_next_nvpair(src_proto, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(src_proto, nvp)) {
		if (nvpair_type(nvp) != DATA_TYPE_NVLIST)
			continue;

		if (nvpair_value_nvlist(nvp, &sec) != 0)
			return (SA_INVALID_PROP_VAL);

		if ((secname = nvpair_name(nvp)) == NULL)
			return (SA_INVALID_PROP);

		tmp_sec = sa_share_alloc(NULL, NULL);
		if (tmp_sec == NULL)
			return (SA_NO_MEMORY);
		ret = sa_upgrade_nvlist_merge(tmp_sec, sec, 0);

		if ((ret == SA_OK) &&
		    (nvlist_lookup_nvlist(dest_proto, secname, &dsec) == 0)) {
			if ((!sa_prop_empty_list(tmp_sec)) &&
			    (!sa_prop_empty_list(dsec))) {
				ret = sa_upgrade_nvlist_merge(tmp_sec,
				    dsec, 0);
				if (ret != SA_OK) {
					sa_share_free(tmp_sec);
					break;
				}
				ret = sa_upgrade_nvlist_merge(dsec,
				    tmp_sec, 0);
			}
		}
		sa_share_free(tmp_sec);
		if (ret != SA_OK)
			break;
	}

	if (ret != SA_OK)
		sa_upgrade_error(ret, "error merging security properties.");

	return (ret);
}

/*
 * sa_upgrade_merge_prot
 *
 * This method merges the "protocol" properties for a group and/or a share
 * with the "protocol" properties for a resource. It also calls the
 * sa_upgrade_merge_sec() method to merge the "security" properties.
 */
static int
sa_upgrade_merge_prot(nvlist_t *src_nvl, nvlist_t *dest_nvl)
{
	nvlist_t *proto, *dproto, *tmp_proto;
	nvpair_t *nvp;
	char *protname;
	int ret = SA_OK;

	proto = dproto = tmp_proto = NULL;
	for (nvp = nvlist_next_nvpair(src_nvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(src_nvl, nvp)) {
		if ((protname = nvpair_name(nvp)) == NULL)
			return (SA_INVALID_PROP);

		if (nvpair_type(nvp) != DATA_TYPE_NVLIST)
			continue;

		if (nvpair_value_nvlist(nvp, &proto) != 0)
			return (SA_INVALID_PROP_VAL);

		if ((strcasecmp(protname, "nfs") == 0) ||
		    (strcasecmp(protname, "smb") == 0)) {
			tmp_proto = sa_share_alloc(NULL, NULL);
			if (tmp_proto == NULL)
				return (SA_NO_MEMORY);

			ret = sa_upgrade_nvlist_merge(tmp_proto, proto, 0);
			if (ret != SA_OK) {
				sa_share_free(tmp_proto);
				break;
			}

			if (nvlist_lookup_nvlist(dest_nvl,
			    protname, &dproto) == 0) {
				ret = sa_upgrade_nvlist_merge(tmp_proto,
				    dproto, 0);
				if (ret == SA_OK)
					ret = sa_upgrade_nvlist_merge(dproto,
					    tmp_proto, 0);
				if (ret != SA_OK) {
					sa_share_free(tmp_proto);
					break;
				}
			} else {
				dproto = sa_share_alloc(NULL, NULL);
				if (dproto == NULL) {
					sa_share_free(tmp_proto);
					return (SA_NO_MEMORY);
				}

				ret = sa_upgrade_nvlist_merge(dproto,
				    tmp_proto, 0);
				if (ret == SA_OK)
					ret = sa_upgrade_add_nvlist(dest_nvl,
					    protname, dproto);
				if (ret != SA_OK) {
					sa_share_free(tmp_proto);
					sa_share_free(dproto);
					break;
				}
				sa_share_free(dproto);
			}
			sa_share_free(tmp_proto);

			if (ret == SA_OK) {
				dproto = NULL;
				if (nvlist_lookup_nvlist(dest_nvl,
				    protname, &dproto) != 0) {
					ret = SA_INVALID_PROTO;
					break;
				}

				ret = sa_upgrade_merge_sec(proto, dproto);
				if (ret != SA_OK)
					break;
			}
		}
	}

	if (ret != SA_OK)
		sa_upgrade_error(ret, "error merging protocol properties.");

	return (ret);
}

/*
 * sa_upgrade_walk_group
 *
 * This method walks a group (and the nested shares and resources) nvlist,
 * and adds the newly created libshare v2 formatted nvlist(s) in "nvl_new"
 */
static int
sa_upgrade_walk_group(nvlist_t *nvl_new, nvlist_t *group)
{
	nvpair_t *gnvp, *snvp, *rnvp;
	nvlist_t *share = NULL;
	nvlist_t *resource = NULL;
	char *propname, *rname;
	int ret = SA_OK;

	for (gnvp = nvlist_next_nvpair(group, NULL); gnvp != NULL;
	    gnvp = nvlist_next_nvpair(group, gnvp)) {
		propname = nvpair_name(gnvp);
		if ((strcasecmp(propname, "nfs") == 0) ||
		    (strcasecmp(propname, "smb") == 0) ||
		    (nvpair_type(gnvp) != DATA_TYPE_NVLIST))
			continue;

		if (nvpair_value_nvlist(gnvp, &share) != 0)
			return (SA_INVALID_PROP_VAL);

		for (snvp = nvlist_next_nvpair(share, NULL); snvp != NULL;
		    snvp = nvlist_next_nvpair(share, snvp)) {
			propname = nvpair_name(snvp);
			if ((strcasecmp(propname, "nfs") == 0) ||
			    (strcasecmp(propname, "smb") == 0) ||
			    (nvpair_type(snvp) != DATA_TYPE_NVLIST))
				continue;

			if (nvpair_value_nvlist(snvp, &resource) != 0)
				return (SA_INVALID_PROP_VAL);

			for (rnvp = nvlist_next_nvpair(resource, NULL);
			    rnvp != NULL;
			    rnvp = nvlist_next_nvpair(resource, rnvp)) {
				ret = sa_upgrade_merge_prot(group, share);
				if (ret != SA_OK)
					break;

				ret = sa_upgrade_merge_prot(share, resource);
				if (ret != SA_OK)
					break;

				ret = nvlist_lookup_string(resource, "name",
				    &rname);
				ret = sa_upgrade_map_nvlist_errcodes(ret);
				if (rname != NULL && (ret == SA_OK))
					ret = sa_upgrade_add_nvlist(nvl_new,
					    rname, resource);
				if (ret != SA_OK)
					break;
			}
		}
	}

	return (ret);
}

/*
 * sa_upgrade_convert_config
 *
 * Converts libshare v1 configuration into libshare v2 configuration.
 * The nvlist configuration for libshare v1 is stored in the nvl_old param.
 * The converted nvlist configuration for libshare v2 is returned in nvl_new
 * param.
 */
static int
sa_upgrade_convert_config(nvlist_t *nvl_new, nvlist_t *nvl_old, char *gname)
{
	nvpair_t *cnvp, *glnvp;
	nvlist_t *glist = NULL;
	nvlist_t *group = NULL;
	int ret = SA_OK;

	for (cnvp = nvlist_next_nvpair(nvl_old, NULL); cnvp != NULL;
	    cnvp = nvlist_next_nvpair(nvl_old, cnvp)) {
		if (nvpair_type(cnvp) != DATA_TYPE_NVLIST)
			continue;

		if (nvpair_value_nvlist(cnvp, &glist) != 0) {
			ret = SA_INVALID_PROP_VAL;
			break;
		}

		for (glnvp = nvlist_next_nvpair(glist, NULL); glnvp != NULL;
		    glnvp = nvlist_next_nvpair(glist, glnvp)) {
			if (nvpair_type(glnvp) != DATA_TYPE_NVLIST)
				continue;

			if (nvpair_value_nvlist(glnvp, &group) != 0) {
				ret = SA_INVALID_PROP_VAL;
				break;
			}

			ret = sa_upgrade_walk_group(nvl_new, group);
			if (ret != SA_OK)
				break;
		}
	}

	if (ret != SA_OK)
		sa_upgrade_error(ret,
		    "error converting group %s to libshare v2 format.", gname);

	return (ret);
}

/*
 * sa_upgrade_sharing_set
 *
 * This method sets the sharesmb/sharenfs property to "on". If the filesystem
 * does not support these properties, then SA_NOT_SUPPORTED error is returned.
 */
static int
sa_upgrade_sharing_set(nvlist_t *share, char *gname)
{
	char *path, *mntpnt;
	sa_proto_t p;
	char *optstr = NULL;
	int ret = SA_OK;

	path = sa_share_get_path(share);
	mntpnt = malloc(MAXPATHLEN);
	if (mntpnt == NULL) {
		ret = SA_NO_MEMORY;
		sa_upgrade_error(ret,
		    "error setting sharesmb property for share "
		    " %s in group %s.", sa_share_get_name(share), gname);
		return (ret);
	}

	ret = sa_get_mntpnt_for_path(path, mntpnt, MAXPATHLEN,
	    NULL, 0, NULL, 0);
	if (ret != SA_OK) {
		free(mntpnt);
		sa_upgrade_error(ret,
		    "error getting mountpoint for share %s in group %s.",
		    sa_share_get_name(share), gname);
		return (ret);
	}

	for (p = sa_proto_first(); p != SA_PROT_NONE; p = sa_proto_next(p)) {
		if (sa_share_get_proto(share, p) == NULL)
			continue;

		if (sa_sharing_get_prop(mntpnt, p, &optstr) == SA_OK &&
		    optstr != NULL) {
			/*
			 * in order to preserve share properties, only
			 * set this if currently set to off.
			 */
			if (strcmp(optstr, "off") == 0) {
				ret = sa_sharing_set_prop(mntpnt, p, "on");
				if ((ret != SA_NOT_SUPPORTED) &&
				    (ret != SA_OK)) {
					free(mntpnt);
					sa_upgrade_error(ret,
					    "error setting share%s property "
					    "for share %s in group %s",
					    sa_proto_to_val(p),
					    sa_share_get_name(share), gname);

					return (ret);
				}
			}
			free(optstr);
		}
	}

	free(mntpnt);
	return (SA_OK);
}

/*
 * sa_upgrade_write_config
 *
 * This method validates the new libshare v2 config and writes shares
 * in the new SMF instance for libshare v2. It also publishes the new share.
 */
static int
sa_upgrade_write_config(nvlist_t *new_nvl, char *gname)
{
	boolean_t new = B_TRUE;
	nvpair_t *snvp;
	nvlist_t *share = NULL;
	char errbuf[512], *sname;
	int ret = SA_OK;
	boolean_t is_error = B_FALSE;

	for (snvp = nvlist_next_nvpair(new_nvl, NULL); snvp != NULL;
	    snvp = nvlist_next_nvpair(new_nvl, snvp)) {
		ret = nvpair_value_nvlist(snvp, &share);
		if (ret != 0) {
			is_error = B_TRUE;
			ret = SA_INVALID_PROP_VAL;
			sa_upgrade_error(ret,
			    "error validating share for group %s", gname);
			continue;
		}

		if ((sname = sa_share_get_name(share)) == NULL)
			sname = "";
		ret = sa_share_validate(share, new, errbuf, sizeof (errbuf));

		/*
		 * Ignore SA_DUPLICATE_PATH & SA_INVALID_ACCLIST_PROP_VAL
		 * errors on upgrade.
		 */
		if (ret == SA_DUPLICATE_PATH ||
		    ret == SA_INVALID_ACCLIST_PROP_VAL) {
			salog_debug(ret, "%s: %s", sname, errbuf);
			ret = SA_OK;
		}

		if (ret != SA_OK) {
			is_error = B_TRUE;
			sa_upgrade_error(ret,
			    "error validating share %s for group "
			    "%s: %s", sname, gname, errbuf);
		}

		if (ret == SA_OK && sa_share_get_mntpnt(share) == NULL) {
			char mntpnt[MAXPATHLEN];
			char *path;

			/* add a mountpoint for legacy */
			path = sa_share_get_path(share);
			ret = sa_get_mntpnt_for_path(path, mntpnt,
			    (size_t)MAXPATHLEN, NULL, 0, NULL, 0);
			if (ret == SA_OK)
				ret = sa_share_set_mntpnt(share, mntpnt);
			if (ret != SA_OK) {
				is_error = B_TRUE;
				sa_upgrade_error(ret, "error setting mountpoint"
				    "on share %s for group %s",
				    sname, gname);
			}
		}
	}

	if (is_error)
		return (SA_INTERNAL_ERR);

	for (snvp = nvlist_next_nvpair(new_nvl, NULL); snvp != NULL;
	    snvp = nvlist_next_nvpair(new_nvl, snvp)) {
		ret = nvpair_value_nvlist(snvp, &share);
		if (ret != 0) {
			is_error = B_TRUE;
			ret = SA_INVALID_PROP_VAL;
			sa_upgrade_error(ret,
			    "error writing share for group %s", gname);
			continue;
		}

		if ((sname = sa_share_get_name(share)) == NULL)
			sname = "";
		ret = sa_share_write(share);
		if (ret != SA_OK) {
			is_error = B_TRUE;
			sa_upgrade_error(ret,
			    "error writing share %s for group %s",
			    sname, gname);
			continue;
		}

		ret = sa_upgrade_sharing_set(share, gname);
		if (ret != SA_OK) {
			is_error = B_TRUE;
			sa_upgrade_error(ret,
			    "error writing share %s for group %s",
			    sname, gname);
		}
	}
	if (is_error)
		return (SA_INTERNAL_ERR);

	return (SA_OK);
}

/*
 * sa_upgrade_error
 *
 * This method will log the error message to both syslog and SMF log of the
 * associated SMF service.
 */
static void
sa_upgrade_error(int err, const char *fmt, ...)
{
	va_list ap;
	char errbuf[1024];

	va_start(ap, fmt);
	(void) vsnprintf(errbuf, sizeof (errbuf), fmt, ap);
	va_end(ap);

	syslog(LOG_ERR, dgettext(TEXT_DOMAIN, "share upgrade: %s: %s"),
	    errbuf, sa_strerror(err));
	(void) fprintf(stderr, dgettext(TEXT_DOMAIN, "share upgrade: %s: %s\n"),
	    errbuf, sa_strerror(err));

	if (err == SA_SCF_ERROR) {
		syslog(LOG_ERR, dgettext(TEXT_DOMAIN, "share upgrade: %s"),
		    scf_strerror(scf_error()));
		(void) fprintf(stderr, dgettext(TEXT_DOMAIN,
		    "share upgrade: %s\n"), scf_strerror(scf_error()));
	}
}

/*
 * sa_upgrade_smf_share_group
 *
 * This method upgrades non-zfs shares from libshare v1 format to
 * libshare v2 format. All shares (and resources) in the passed group
 * name are converted. If force_upgrade is B_TRUE, then the state of
 * legacy group service instances will be ignored.
 */
int
sa_upgrade_smf_share_group(char *gname, boolean_t force_upgrade)
{
	nvlist_t	*new_nvl = NULL;
	nvlist_t	*old_nvl = NULL;
	scfutilhandle_t *handle;
	int ret = SA_OK;
	boolean_t is_log_smf = !force_upgrade;

	handle = sa_upgrade_scf_init();
	if ((gname == NULL) || (handle == NULL))
		return (SA_SCF_ERROR);

	if (strcmp(gname, "zfs") == 0) {
		ret = sa_upgrade_delete_config(handle, gname, force_upgrade);
		sa_upgrade_scf_fini(handle);
		return (SA_OK);
	}

	old_nvl = sa_share_alloc(NULL, NULL);
	if (old_nvl == NULL) {
		sa_upgrade_scf_fini(handle);
		return (SA_NO_MEMORY);
	}

	ret = sa_upgrade_get_config(handle, old_nvl, gname, force_upgrade);
	if (ret != SA_OK) {
		sa_share_free(old_nvl);
		sa_upgrade_scf_fini(handle);
		return (ret);
	}
	sa_upgrade_dump(old_nvl, is_log_smf, "ORIGINAL CONFIGURATION");

	new_nvl = sa_share_alloc(NULL, NULL);
	if (new_nvl  == NULL) {
		sa_share_free(old_nvl);
		sa_upgrade_scf_fini(handle);
		return (SA_NO_MEMORY);
	}

	ret = sa_upgrade_convert_config(new_nvl, old_nvl, gname);
	if (ret != SA_OK) {
		sa_share_free(old_nvl);
		sa_share_free(new_nvl);
		sa_upgrade_scf_fini(handle);
		return (ret);
	}
	sa_upgrade_dump(new_nvl, is_log_smf, "UPGRADED CONFIGURATION");
	sa_share_free(old_nvl);

	ret = sa_upgrade_write_config(new_nvl, gname);
	if (ret != SA_OK) {
		sa_share_free(new_nvl);
		sa_upgrade_scf_fini(handle);
		return (ret);
	}
	sa_share_free(new_nvl);

	ret = sa_upgrade_delete_config(handle, gname, force_upgrade);
	sa_upgrade_scf_fini(handle);

	return (ret);
}
