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

#include <stdio.h>
#include <sys/types.h>
#include <umem.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <alloca.h>
#include <libuvfs_impl.h>

const char *
libuvfs_scf_error(const libuvfs_fs_t *fs, scf_error_t *errnum)
{
	if (errnum != NULL)
		*errnum = fs->fs_scf_error;

	return (scf_strerror(fs->fs_scf_error));
}

static void
libuvfs_set_svc_info(libuvfs_fs_t *fs)
{
	if (fs->fs_scf_handle != NULL)
		return;

	fs->fs_scf_handle = scf_handle_create(SCF_VERSION);
	if (fs->fs_scf_handle == NULL) {
		fs->fs_scf_error = scf_error();
		return;
	}

	if (scf_handle_bind(fs->fs_scf_handle) != 0)
		fs->fs_scf_error = scf_error();

	/*
	 * This should succeed if we're an smf service, and fail otherwise.
	 * If it fails, fs_scf_props will be NULL.  The caller will have to
	 * deal with this if it's expected to have the values.
	 *
	 * We always allocate memory for fs_daemon_fmri, even if we are not
	 * in a daemon process.  That way, the buffer can be used in other
	 * places, e.g. launching an instance.
	 */
	fs->fs_daemon_fmri_size = scf_limit(SCF_LIMIT_MAX_FMRI_LENGTH);
	fs->fs_daemon_fmri = umem_alloc(fs->fs_daemon_fmri_size, UMEM_NOFAIL);
	fs->fs_daemon_fmri[0] = '\0';
	if (scf_myname(fs->fs_scf_handle, fs->fs_daemon_fmri,
	    fs->fs_daemon_fmri_size) != -1)
		fs->fs_scf_props = scf_simple_app_props_get(fs->fs_scf_handle,
		    fs->fs_daemon_fmri);

	if (fs->fs_scf_props == NULL)
		fs->fs_scf_error = scf_error();
}

int
libuvfs_is_daemon(void)
{
	int is_daemon = B_FALSE;
	scf_handle_t *scf = NULL;
	int fmri_size;
	char *fmri;

	scf = scf_handle_create(SCF_VERSION);
	if (scf == NULL)
		goto out;
	if (scf_handle_bind(scf) != 0)
		goto out;
	fmri_size = scf_limit(SCF_LIMIT_MAX_FMRI_LENGTH);
	fmri = alloca(fmri_size);
	if (scf_myname(scf, fmri, fmri_size) == -1)
		goto out;

	is_daemon = B_TRUE;

out:
	if (scf != NULL)
		scf_handle_destroy(scf);
	return (is_daemon);
}

const char *
libuvfs_get_daemon_executable(libuvfs_fs_t *fs)
{
	const scf_simple_prop_t *prop;
	char *found_exec;

	libuvfs_set_svc_info(fs);
	if (fs->fs_scf_props == NULL) {
		fs->fs_scf_error = scf_error();
		return (NULL);
	}

	prop = scf_simple_app_props_search(fs->fs_scf_props, "filesys",
	    "daemon");
	if (prop == NULL)
		return (NULL);

	if (scf_simple_prop_numvalues(prop) != 1)
		return (NULL);

	found_exec = scf_simple_prop_next_astring((scf_simple_prop_t *)prop);
	if (found_exec == NULL)
		return (NULL);

	return (libuvfs_strdup(found_exec));
}

void
libuvfs_get_daemon_fsid(libuvfs_fs_t *fs)
{
	const scf_simple_prop_t *prop;
	int64_t *found_fsid;

	libuvfs_set_svc_info(fs);
	if (fs->fs_scf_props == NULL) {
		fs->fs_scf_error = scf_error();
		return;
	}

	prop = scf_simple_app_props_search(fs->fs_scf_props, "filesys", "fsid");
	if (prop == NULL)
		return;

	if (scf_simple_prop_numvalues(prop) != 1)
		return;

	found_fsid = scf_simple_prop_next_integer((scf_simple_prop_t *)prop);
	fs->fs_fsid = *((uint64_t *)found_fsid);
}

static int
libuvfs_trans_set_value(scf_transaction_t *tr, const char *name,
    scf_value_t *scfval)
{
	scf_handle_t *h = scf_transaction_handle(tr);
	scf_transaction_entry_t *ent = scf_entry_create(h);
	int type;

	type = scf_value_type(scfval);

	if ((scf_transaction_property_new(tr, ent, name, type) != 0) &&
	    (scf_transaction_property_change(tr, ent, name, type) != 0)) {
		scf_entry_destroy_children(ent);
		scf_entry_destroy(ent);
		return (-1);
	}

	if (scf_entry_add_value(ent, scfval) != 0) {
		if (ent != NULL)
			scf_entry_destroy(ent);
		if (scfval != NULL)
			scf_value_destroy(scfval);
		return (-1);
	}

	return (0);
}

static int
libuvfs_set_trans_astring(scf_transaction_t *tr, const char *name,
    const char *val)
{
	scf_handle_t *h = scf_transaction_handle(tr);
	scf_value_t *scfval = scf_value_create(h);
	int rc;

	/* protect against passing NULL as meaning "nothing" */
	if (val == NULL)
		val = "";
	rc = scf_value_set_astring(scfval, val);
	if (rc == 0)
		rc = libuvfs_trans_set_value(tr, name, scfval);
	else if (scfval != NULL)
		scf_value_destroy(scfval);

	return (rc);
}

static int
libuvfs_set_trans_integer(scf_transaction_t *tr, const char *name,
    const int64_t val)
{
	scf_handle_t *h = scf_transaction_handle(tr);
	scf_value_t *scfval = scf_value_create(h);
	int rc;

	scf_value_set_integer(scfval, val);
	rc = libuvfs_trans_set_value(tr, name, scfval);

	return (rc);
}

int
libuvfs_daemon_launch(libuvfs_fs_t *fs, const char *special,
    const char *mountpoint, uint64_t fsid, const char *options)
{
	scf_service_t *service = NULL;
	scf_instance_t *instance = NULL;
	scf_propertygroup_t *pg = NULL;
	scf_transaction_t *trans = NULL;
	char *inst_name, fsid_str[17];
	ssize_t inst_name_size;
	int rc;

	inst_name_size = scf_limit(SCF_LIMIT_MAX_NAME_LENGTH);
	inst_name = alloca(inst_name_size);

	libuvfs_fsid_to_str(fsid, fsid_str, sizeof (fsid_str));
	(void) strlcpy(inst_name, "fsid-", inst_name_size);
	if (strlcat(inst_name, fsid_str, inst_name_size) >= inst_name_size) {
		rc = -1;
		goto out;
	}

	libuvfs_set_svc_info(fs);

	service = scf_service_create(fs->fs_scf_handle);
	if (service == NULL) {
		rc = -1;
		goto out;
	}
	instance = scf_instance_create(fs->fs_scf_handle);
	if (instance == NULL) {
		rc = -1;
		goto out;
	}

	rc = scf_handle_decode_fmri(fs->fs_scf_handle, LIBUVFS_SERVER_FMRI,
	    NULL, service, NULL, NULL, NULL, 0);
	if (rc != 0)
		goto out;

	if (scf_service_add_instance(service, inst_name, instance) != NULL) {
		int error = scf_error();

		if (error != SCF_ERROR_EXISTS) {
			rc = -1;
			goto out;
		}
		if (scf_service_get_instance(service, inst_name, instance)
		    != 0) {
			rc = -1;
			goto out;
		}
	}

	pg = scf_pg_create(fs->fs_scf_handle);
	rc = scf_instance_add_pg(instance, "filesys", "application", 0, pg);
	if (rc != 0) {
		int error = scf_error();

		if ((error != SCF_ERROR_EXISTS) ||
		    (scf_instance_get_pg(instance, "filesys", pg)))
			goto out;
	}

	trans = scf_transaction_create(fs->fs_scf_handle);
	rc = scf_transaction_start(trans, pg);
	if (rc != 0)
		goto out;

	rc = libuvfs_set_trans_astring(trans, "daemon", special);
	if (rc != 0)
		goto out;
	rc = libuvfs_set_trans_astring(trans, "special", special);
	if (rc != 0)
		goto out;
	rc = libuvfs_set_trans_astring(trans, "mountpoint", mountpoint);
	if (rc != 0)
		goto out;
	rc = libuvfs_set_trans_integer(trans, "fsid", (int64_t)fsid);
	if (rc != 0)
		goto out;
	rc = libuvfs_set_trans_astring(trans, "options", options);
	if (rc != 0)
		goto out;

	if (scf_transaction_commit(trans) != 1) {
		rc = -1;
		goto out;
	}

	rc = scf_instance_to_fmri(instance, fs->fs_daemon_fmri,
	    fs->fs_daemon_fmri_size);
	if (rc == -1)
		goto out;

	rc = smf_refresh_instance(fs->fs_daemon_fmri);
	if (rc == -1)
		goto out;
	rc = smf_enable_instance(fs->fs_daemon_fmri, 0);

out:
	if (rc != 0)
		fs->fs_scf_error = scf_error();

	if (trans != NULL) {
		scf_transaction_destroy_children(trans);
		scf_transaction_destroy(trans);
	}
	if (pg != NULL)
		scf_pg_destroy(pg);
	if (instance != NULL)
		scf_instance_destroy(instance);
	if (service != NULL)
		scf_service_destroy(service);

	return (rc);
}

void
libuvfs_daemon_exit(libuvfs_fs_t *fs)
{
	libuvfs_set_svc_info(fs);
	if ((fs->fs_daemon_fmri == NULL) || (fs->fs_daemon_fmri[0] == '\0'))
		exit(0);

	(void) smf_disable_instance(fs->fs_daemon_fmri, 0);

	exit(0);
}

void
libuvfs_daemon_atexit(void)
{
	libuvfs_fs_t *fs;

	fs = libuvfs_create_fs(LIBUVFS_VERSION, LIBUVFS_FSID_NONE);
	libuvfs_set_svc_info(fs);
	if (fs->fs_daemon_fmri)
		(void) smf_disable_instance(fs->fs_daemon_fmri, 0);
}

int
libuvfs_remove_disabled_instances(libuvfs_fs_t *fs)
{
	scf_service_t *service = NULL;
	scf_iter_t *iter = NULL;
	scf_instance_t *instance = NULL;
	scf_simple_prop_t *prop = NULL;
	size_t inst_fmri_size;
	char *inst_fmri;
	int rc = 0;

	libuvfs_set_svc_info(fs);
	if (fs->fs_scf_handle == NULL)
		return (-1);

	service = scf_service_create(fs->fs_scf_handle);
	rc = scf_handle_decode_fmri(fs->fs_scf_handle, LIBUVFS_SERVER_FMRI,
	    NULL, service, NULL, NULL, NULL, 0);
	if (rc != 0)
		goto out;

	iter = scf_iter_create(fs->fs_scf_handle);
	rc = scf_iter_service_instances(iter, service);
	if (rc != 0)
		goto out;

	instance = scf_instance_create(fs->fs_scf_handle);
	inst_fmri_size = scf_limit(SCF_LIMIT_MAX_FMRI_LENGTH);
	inst_fmri = alloca(inst_fmri_size);
	for (rc = scf_iter_next_instance(iter, instance); rc == 1;
	    rc = scf_iter_next_instance(iter, instance)) {
		uint8_t *enabled;
		char *state;

		rc = scf_instance_to_fmri(instance, inst_fmri, inst_fmri_size);
		if (rc ==  -1)
			continue;

		prop = scf_simple_prop_get(fs->fs_scf_handle, inst_fmri,
		    "restarter", "state");
		if (prop == NULL)
			continue;
		if (scf_simple_prop_numvalues(prop) != 1)
			continue;
		state = scf_simple_prop_next_astring(prop);
		if (state == NULL)
			continue;
		if (strcmp(state, "maintenance") == 0) {
			(void) scf_instance_delete(instance);
			continue;
		}

		prop = scf_simple_prop_get(fs->fs_scf_handle, inst_fmri,
		    "general", "enabled");
		if (prop == NULL)
			continue;
		if (scf_simple_prop_numvalues(prop) != 1)
			continue;
		enabled = scf_simple_prop_next_boolean(prop);
		if (enabled == NULL)
			continue;
		if (*enabled)
			continue;

		(void) scf_instance_delete(instance);
	}
out:
	if (rc != 0)
		fs->fs_scf_error = scf_error();

	if (prop != NULL)
		scf_simple_prop_free(prop);
	if (instance != NULL)
		scf_instance_destroy(instance);
	if (iter != NULL)
		scf_iter_destroy(iter);
	if (service != NULL)
		scf_service_destroy(service);

	return (rc);
}
