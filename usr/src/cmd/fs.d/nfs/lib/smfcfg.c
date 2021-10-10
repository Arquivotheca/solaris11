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
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <stdarg.h>
#include <zone.h>
#include "smfcfg.h"

struct properties autofs_nfs_tab_prop[] = {
	{ "AUTOMOUNT_TIMEOUT=", "timeout", AUTOFS_SMF,
    SCF_TYPE_INTEGER, AUTOMOUNTD },
	{ "AUTOMOUNT_VERBOSE=", "automount_verbose", AUTOFS_SMF,
    SCF_TYPE_BOOLEAN, AUTOMOUNTD},
	{ "AUTOMOUNTD_ENV=", "environment", AUTOFS_SMF,
    SCF_TYPE_ASTRING, AUTOMOUNTD },
	{ "AUTOMOUNTD_NOBROWSE=", "nobrowse", AUTOFS_SMF,
    SCF_TYPE_BOOLEAN, AUTOMOUNTD },
	{ "AUTOMOUNTD_TRACE=", "trace", AUTOFS_SMF,
    SCF_TYPE_INTEGER, AUTOMOUNTD},
	{ "AUTOMOUNTD_VERBOSE=", "automountd_verbose", AUTOFS_SMF,
    SCF_TYPE_BOOLEAN, AUTOMOUNTD },
	{ "NFS_CLIENT_VERSMIN=", "client_versmin", NFS_SMF,
    SCF_TYPE_INTEGER, SVC_NFS_CLIENT },
	{ "NFS_CLIENT_VERSMAX=", "client_versmax", NFS_SMF,
    SCF_TYPE_INTEGER, SVC_NFS_CLIENT },
	{ "NFS_SERVER_VERSMIN=", "server_versmin", NFS_SMF,
    SCF_TYPE_INTEGER, NFSD  },
	{ "NFS_SERVER_VERSMAX=", "server_versmax", NFS_SMF,
    SCF_TYPE_INTEGER, NFSD }
};

static void fs_smf_fini(fs_smfhandle_t *);

static fs_smfhandle_t *
fs_smf_init(char *fmri, char *instance)
{
	fs_smfhandle_t *handle = NULL;
	char *svcname, srv[MAXPATHLEN];

	/*
	 * svc name is of the form svc://network/fs/server:instance1
	 * FMRI portion is /network/fs/server
	 */
	snprintf(srv, MAXPATHLEN, "%s", fmri + strlen("svc:/"));
	svcname = strrchr(srv, ':');
	if (svcname != NULL)
		*svcname = '\0';
	svcname = srv;

	handle = calloc(1, sizeof (fs_smfhandle_t));
	if (handle != NULL) {
		handle->fs_handle = scf_handle_create(SCF_VERSION);
		if (handle->fs_handle == NULL)
			goto out;
		if (scf_handle_bind(handle->fs_handle) != 0)
			goto out;
		handle->fs_service =
		    scf_service_create(handle->fs_handle);
		handle->fs_scope =
		    scf_scope_create(handle->fs_handle);
		if (scf_handle_get_local_scope(handle->fs_handle,
		    handle->fs_scope) != 0)
			goto out;
		if (scf_scope_get_service(handle->fs_scope,
		    svcname, handle->fs_service)  != SCF_SUCCESS) {
			goto out;
		}
		handle->fs_pg =
		    scf_pg_create(handle->fs_handle);
		handle->fs_instance =
		    scf_instance_create(handle->fs_handle);
		handle->fs_property =
		    scf_property_create(handle->fs_handle);
		handle->fs_value =
		    scf_value_create(handle->fs_handle);
	} else {
		syslog(LOG_ERR,
		    "Cannot access SMF repository: %s\n", fmri);
	}
	return (handle);

out:
	fs_smf_fini(handle);
	return (NULL);
}


static void
fs_smf_fini(fs_smfhandle_t *handle)
{
	if (handle != NULL) {
		scf_scope_destroy(handle->fs_scope);
		scf_instance_destroy(handle->fs_instance);
		scf_service_destroy(handle->fs_service);
		scf_pg_destroy(handle->fs_pg);
		scf_property_destroy(handle->fs_property);
		scf_value_destroy(handle->fs_value);
		if (handle->fs_handle != NULL) {
			scf_handle_unbind(handle->fs_handle);
			scf_handle_destroy(handle->fs_handle);
		}
		free(handle);
	}
}

int
fs_smf_set_prop(smf_fstype_t fstype, char *prop_name, char *valbuf,
    char *instance, scf_type_t sctype, char *fmri)
{
	fs_smfhandle_t *phandle;
	scf_handle_t *handle;
	scf_propertygroup_t *pg;
	scf_property_t *prop;
	scf_transaction_t *tran;
	scf_transaction_entry_t *entry;
	scf_instance_t *inst;
	scf_value_t *val;
	int valint;
	int index = 0;
	int ret = 0;
	char *p = NULL;
	char *svcname, srv[MAXPATHLEN];
	const char *pgname;

	/*
	 * The SVC names we are using currently are already
	 * appended by default. Fix this for instances project.
	 */
	snprintf(srv, MAXPATHLEN, "%s", fmri);
	p = strstr(fmri, ":default");
	if (p == NULL) {
		strcat(srv, ":");
		if (instance == NULL)
			instance = "default";
		if (strlen(srv) + strlen(instance) > MAXPATHLEN)
			goto out;
		strncat(srv, instance, strlen(instance));
	}
	svcname = srv;
	phandle = fs_smf_init(fmri, instance);
	if (phandle == NULL) {
		return (SMF_SYSTEM_ERR);
	}
	handle = phandle->fs_handle;
	pg = phandle->fs_pg;
	prop = phandle->fs_property;
	inst = phandle->fs_instance;
	val = phandle->fs_value;
	tran = scf_transaction_create(handle);
	entry = scf_entry_create(handle);

	if (handle == NULL || pg == NULL || prop == NULL ||
	    val == NULL|| tran == NULL || entry == NULL || inst == NULL) {
		ret = SMF_SYSTEM_ERR;
		goto out;
	}

	if (scf_handle_decode_fmri(handle, svcname, phandle->fs_scope,
	    phandle->fs_service, inst, NULL, NULL, 0) != 0) {
		ret = scf_error();
		goto out;
	}
	if (fstype == AUTOFS_SMF)
		pgname = AUTOFS_PROPS_PGNAME;
	else
		pgname = NFS_PROPS_PGNAME;

	if (scf_instance_get_pg(inst, pgname,
	    pg) != -1) {
		uint8_t	vint;
		if (scf_transaction_start(tran, pg) == -1) {
			ret = scf_error();
			goto out;
		}
		switch (sctype) {
		case SCF_TYPE_INTEGER:
			errno = 0;
			valint = strtoul(valbuf, NULL, 0);
			if (errno != 0) {
				ret = SMF_SYSTEM_ERR;
				goto out;
			}
			if (scf_transaction_property_change(tran,
			    entry, prop_name, SCF_TYPE_INTEGER) == 0) {
				scf_value_set_integer(val, valint);
				if (scf_entry_add_value(entry, val) < 0) {
					ret = scf_error();
					goto out;
				}
			}
			break;
		case SCF_TYPE_ASTRING:
			if (scf_transaction_property_change(tran, entry,
			    prop_name, SCF_TYPE_ASTRING) == 0) {
				if (scf_value_set_astring(val,
				    valbuf) == 0) {
					if (scf_entry_add_value(entry,
					    val) != 0) {
						ret = scf_error();
						goto out;
					}
				} else
					ret = SMF_SYSTEM_ERR;
			} else
				ret = SMF_SYSTEM_ERR;
			break;
		case SCF_TYPE_BOOLEAN:
			if (strcmp(valbuf, "1") == 0) {
				vint = 1;
			} else if (strcmp(valbuf, "0") == 0) {
				vint = 0;
			} else  {
				ret = SMF_SYSTEM_ERR;
				break;
			}
			if (scf_transaction_property_change(tran, entry,
			    prop_name, SCF_TYPE_BOOLEAN) == 0) {
				scf_value_set_boolean(val, (uint8_t)vint);
				if (scf_entry_add_value(entry, val) != 0) {
					ret = scf_error();
					goto out;
				}
			} else {
				ret = SMF_SYSTEM_ERR;
			}
			break;
		}
		if (ret != SMF_SYSTEM_ERR)
			scf_transaction_commit(tran);
	}
out:
	if (tran != NULL)
		scf_transaction_destroy(tran);
	if (entry != NULL)
		scf_entry_destroy(entry);
	fs_smf_fini(phandle);
	return (ret);
}

int
fs_smf_get_prop(smf_fstype_t fstype, char *prop_name, char *cbuf,
    char *instance, scf_type_t sctype, char *fmri, int *bufsz)
{
	fs_smfhandle_t *phandle;
	scf_handle_t *handle;
	scf_propertygroup_t *pg;
	scf_property_t *prop;
	scf_value_t *val;
	scf_instance_t *inst;
	int ret = 0, len = 0, length;
	int64_t valint = 0;
	char srv[MAXPATHLEN], *p, *svcname;
	const char *pgname;
	uint8_t bval;

	/*
	 * The SVC names we are using currently are already
	 * appended by default. Fix this for instances project.
	 */
	snprintf(srv, MAXPATHLEN, "%s", fmri);
	p = strstr(fmri, ":default");
	if (p == NULL) {
		strcat(srv, ":");
		if (instance == NULL)
			instance = "default";
		if (strlen(srv) + strlen(instance) > MAXPATHLEN)
			goto out;
		strncat(srv, instance, strlen(instance));
	}
	svcname = srv;
	phandle = fs_smf_init(fmri, instance);
	if (phandle == NULL)
		return (SMF_SYSTEM_ERR);
	handle = phandle->fs_handle;
	pg = phandle->fs_pg;
	inst = phandle->fs_instance;
	prop = phandle->fs_property;
	val = phandle->fs_value;

	if (handle == NULL || pg == NULL || prop == NULL || val == NULL ||
	    inst == NULL)  {
		return (SMF_SYSTEM_ERR);
	}


	if (scf_handle_decode_fmri(handle, svcname, phandle->fs_scope,
	    phandle->fs_service, inst, NULL, NULL, 0) != 0) {
		ret = scf_error();
		goto out;
	}

	if (fstype == AUTOFS_SMF)
		pgname = AUTOFS_PROPS_PGNAME;
	else
		pgname = NFS_PROPS_PGNAME;

	if (scf_instance_get_pg(inst, pgname, pg) != -1) {
		if (scf_pg_get_property(pg, prop_name,
		    prop) != SCF_SUCCESS) {
			ret = scf_error();
			goto out;
		}
		if (scf_property_get_value(prop, val) != SCF_SUCCESS) {
			ret = scf_error();
			goto out;
		}
		switch (sctype) {
		case SCF_TYPE_ASTRING:
			len = scf_value_get_astring(val, cbuf, *bufsz);
			if (len < 0 || len > *bufsz) {
				ret = scf_error();
				goto out;
			}
			ret = 0;
			*bufsz = len;
		break;
		case SCF_TYPE_INTEGER:
			if (scf_value_get_integer(val, &valint) != 0) {
				ret = scf_error();
				goto out;
			}
			length =  snprintf(cbuf, *bufsz, "%lld", valint);
			if (length < 0 || length > *bufsz) {
				ret = SCF_ERROR_INVALID_ARGUMENT;
				goto out;
			}
			ret = 0;
		break;
		case SCF_TYPE_BOOLEAN:
			if (scf_value_get_boolean(val, &bval) != 0) {
				ret = scf_error();
				goto out;
			}
			if (bval == 1) {
				length = snprintf(cbuf, *bufsz, "%s", "true");
			} else {
				length = snprintf(cbuf, *bufsz, "%s", "false");
			}
			if (length < 0 || length > *bufsz) {
				ret = SCF_ERROR_INVALID_ARGUMENT;
				goto out;
			}
		break;
		}
	} else {
		ret = scf_error();
	}
	if ((ret != 0) && scf_error() != SCF_ERROR_NONE)
		fprintf(stdout, gettext("%s\n"), scf_strerror(ret));
out:
	fs_smf_fini(phandle);
	return (ret);
}


int
nfs_smf_get_prop(char *prop_name, char *propbuf, char *instance,
    scf_type_t sctype, char *svc_name, int *bufsz)
{
	return (fs_smf_get_prop(NFS_SMF, prop_name, propbuf,
	    instance, sctype, svc_name, bufsz));
}

int
nfs_smf_set_prop(char *prop_name, char *value, char *instance,
    scf_type_t type, char *svc_name)
{
	return (fs_smf_set_prop(NFS_SMF, prop_name, value, instance,
	    type, svc_name));
}

int
autofs_smf_set_prop(char *prop_name, char *value, char *instance,
    scf_type_t type, char *svc_name)
{
	return (fs_smf_set_prop(AUTOFS_SMF, prop_name, value, instance,
	    type, svc_name));
}

int
autofs_smf_get_prop(char *prop_name, char *propbuf, char *instance,
    scf_type_t sctype, char *svc_name, int *bufsz)
{
	return (fs_smf_get_prop(AUTOFS_SMF, prop_name, propbuf,
	    instance, sctype, svc_name, bufsz));
}

boolean_t
string_to_boolean(const char *str)
{
	if (strcasecmp(str, "true") == 0 || atoi(str) == 1 ||
	    strcasecmp(str, "on") == 0 || strcasecmp(str, "yes") == 0) {
		return (B_TRUE);
	} else
		return (B_FALSE);
}

boolean_t
check_for_solaris10_brand(zoneid_t zid)
{
	char brand[MAXNAMELEN];

	if (zone_getattr(zid, ZONE_ATTR_BRAND, brand, sizeof (brand)) < 0)
		return (B_FALSE);

	return (strcmp(brand, S10_BRANDNAME) == 0);
}

int
wrapped_get_prop(enum autofs_nfs_prop_idx prop_idx, char *cbuf,
    char *instance,  int  bufsz)
{
	int retval = 0;
	int fileopen = -1;
	char *defval_s10;
	int local_bufsz = bufsz;

	if (check_for_solaris10_brand(getzoneid())) {
	/*
	 * for S10 branded zone, open NFS_ADMIN or AUTOFS_ADMIN
	 * files depending on the type.
	 */
		if (autofs_nfs_tab_prop[prop_idx].smf_fst == NFS_SMF)
				fileopen = defopen(NFSADMIN);
		else if (autofs_nfs_tab_prop[prop_idx].smf_fst == AUTOFS_SMF)
				fileopen = defopen(AUTOFSADMIN);

		if (fileopen == 0) {
			if ((defval_s10 = defread(
			    autofs_nfs_tab_prop[prop_idx].s10_name)) != NULL) {
				strncpy(cbuf, defval_s10, local_bufsz);
				return (0);
			} else
				return (-1);
		}
	} else {
	/* non-S10 branded zone stuff */
		retval = fs_smf_get_prop(autofs_nfs_tab_prop[prop_idx].smf_fst,
		    autofs_nfs_tab_prop[prop_idx].nv_name,
		    cbuf, instance, autofs_nfs_tab_prop[prop_idx].scftype,
		    autofs_nfs_tab_prop[prop_idx].fmri, &local_bufsz);
		return (retval);
	}
	return (-1);
}

void
wrapped_put_automountd_env(enum autofs_nfs_prop_idx prop_idx)
{

	char defval[PATH_MAX], *p, *a, *c, *defval_s10;
	int ret = 0, bufsz = PATH_MAX, defflags;

	/* This is non s10 branded zone */
	ret = wrapped_get_prop(prop_idx, defval,
	    DEFAULT_INSTANCE, sizeof (defval));

	if (ret == 0) {
		if (check_for_solaris10_brand(getzoneid())) {
		/* S10 branded zone */
			(void) putenv(strdup(defval));
			defflags = defcntl(DC_GETFLAGS, 0);
			TURNON(defflags, DC_NOREWIND);
			defflags = defcntl(DC_SETFLAGS, defflags);
			while ((defval_s10 =
			    defread(autofs_nfs_tab_prop[prop_idx].s10_name)) !=
			    NULL)
				(void) putenv(strdup(defval_s10));
			(void) defcntl(DC_SETFLAGS, defflags);
			defopen(NULL);
		} else {
			/* non-S10 branded zone */
			a = c = defval;
			if (*a == NULL)
				return;

/*
 * Environment variables can have more than one value
 * separated by a comma and there can be multiple
 * environment variables. * a=b\,c,d=e. For multiple
 * valued environment variable, values are separated
 * with an escape character.
 */

			while ((p = strchr(c, ',')) != NULL) {
				if (*(p - 1) == '\\') {
					c = p + 1;
					continue;
				}
				*p = '\0';
				if ((c = strchr(a, '=')) != NULL)
					putenv(strdup(a));
				a = c = p + 1;
			}
			if (*a != NULL) {
				if ((c = strchr(a, '=')) != NULL)
					putenv(strdup(a));
			}
		}
	}
}
