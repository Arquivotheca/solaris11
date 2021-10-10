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
 * Copyright (c) 2004, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <assert.h>
#include <libuutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zone.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "startd.h"

/*
 * This file contains functions for setting the environment for
 * processes started by svc.startd.
 */

#define	MAXCMDL		512
#define	DEF_PATH	"PATH=/usr/sbin:/usr/bin"

/* Lock to protect the global environment array */
static pthread_rwlock_t glob_envp_rwlock = PTHREAD_RWLOCK_INITIALIZER;

/* Protected by glob_envp_rwlock */
static char **glob_envp;	/* Array of environment strings */
static int glob_env_n;		/* Number of environment slots allocated. */

static char zonename[ZONENAME_MAX];

/*
 * Loads environment array from SMF service.
 * Returns the environment array and updates
 * the array length in env_n.
 */
static char **
load_env_from_smf(int *env_n)
{
	int	i = 1, val = 0;
	char	**newp, **envp;
	scf_handle_t *scf_handle_p = NULL;
	scf_property_t *scf_prop_p = NULL;
	scf_instance_t *inst = NULL;
	scf_propertygroup_t *pg = NULL;
	scf_iter_t *iter = NULL;
	scf_value_t	*scf_value = NULL;
	scf_snapshot_t *scf_snapshot_p = NULL;

	*env_n = 16;

	envp = startd_zalloc(sizeof (*envp) * (*env_n));

	envp[0] = startd_alloc((unsigned)(strlen(DEF_PATH)+ 1));
	(void) strcpy(envp[0], DEF_PATH);
	envp[1] = NULL;

	scf_handle_p = scf_handle_create(SCF_VERSION);

	if (scf_handle_p == NULL) {
		uu_warn("Cannot create scf_handle %s.\n",
		    scf_strerror(scf_error()));
		return (envp);
	}

	if (scf_handle_bind(scf_handle_p) == -1) {
		uu_warn("Cannot bind scf_handle %s.\n",
		    scf_strerror(scf_error()));
		goto cleanup;
	}

	inst = scf_instance_create(scf_handle_p);
	pg = scf_pg_create(scf_handle_p);
	scf_prop_p = scf_property_create(scf_handle_p);

	if ((inst == NULL) || (pg == NULL) ||
	    (scf_prop_p == NULL)) {
		uu_warn("Unable to create SCF "
		    "instance/property group/property.\n");
		goto cleanup;
	}

	val = scf_handle_decode_fmri(scf_handle_p, SCF_INSTANCE_ENV,
	    NULL, NULL, inst, pg, scf_prop_p, 0);

	if (val != 0) {
		uu_warn("Cannot read svc:/system/environment:init. "
		    "Environment not initialized.\n");
		goto cleanup;
	}

	scf_snapshot_p = scf_snapshot_create(scf_handle_p);

	if (scf_snapshot_p == NULL) {
		uu_warn("Unable to create SCF snapshot: %s\n",
		    scf_strerror(scf_error()));
		goto cleanup;
	}

	val = scf_instance_get_snapshot(inst,
	    "running", scf_snapshot_p);

	if (val == -1) {
		uu_warn("Unable to get snapshot from instance: %s\n",
		    scf_strerror(scf_error()));
		goto cleanup;
	}

	/*
	 * Reading Environment from SMF service
	 */

	if ((iter = scf_iter_create(scf_handle_p)) == NULL) {
		uu_warn("Unable to create SCF iterator: %s\n",
		    scf_strerror(scf_error()));
		goto cleanup;
	}

	val = scf_instance_get_pg_composed(inst, scf_snapshot_p,
	    "environment", pg);
	if (val != 0) {
		uu_warn("Instance properties could "
		    "not be obtained: %s\n",
		    scf_strerror(scf_error()));
		goto cleanup;
	}

	if (scf_iter_pg_properties(iter, pg) != SCF_SUCCESS) {
		uu_warn("Iterator properties could not obtained: %s\n",
		    scf_strerror(scf_error()));
		goto cleanup;
	}

	i = 1;

	while ((scf_iter_next_property(iter, scf_prop_p) == 1)) {
		char valintbuf[MAXCMDL] = "";
		char value[MAXCMDL] = "";
		char key[MAXCMDL] = "";
		size_t length = 0;
		int64_t valint = 0;
		uint8_t valbool = 0;
		scf_type_t type;

		scf_value = scf_value_create(scf_handle_p);
		if (scf_value == NULL)
			continue;

		if ((scf_property_get_value(scf_prop_p,
		    scf_value)) != 0)
			continue;

		(void) scf_property_get_name(scf_prop_p, key, MAXCMDL);

		/*
		 * init already started us with this umask, and we
		 * handled it in startd.c, so just skip CMASK and
		 * properties starting with SMF_
		 */
		if (key == NULL ||
		    strncmp(key, "SMF_", 4) == 0 ||
		    strcmp(key, "value_authorization") == 0 ||
		    strcmp(key, "CMASK") == 0)
			continue;


		type = scf_value_type(scf_value);
		switch (type) {
		case SCF_TYPE_ASTRING:
			if (scf_value_get_astring(scf_value,
			    value, MAXCMDL) == -1)
				continue;
			break;
		case SCF_TYPE_INTEGER:
			if (scf_value_get_integer(scf_value,
			    &valint) != SCF_SUCCESS)
				continue;
			(void) strcpy(value, lltostr(valint,
			    &valintbuf[MAXCMDL - 1]));
			break;
		case SCF_TYPE_BOOLEAN:
			if (scf_value_get_boolean(scf_value,
			    &valbool) != SCF_SUCCESS)
				continue;
			(void) strncpy(value, valbool ? "1" : "0", 2);
			break;
		default:
			continue;
		}

		length = strlen(key) + strlen(value) + 2; /* +2: '=' and NUL */
		envp[i] = startd_alloc(length);
		(void) snprintf(envp[i++], length, "%s=%s", key, value);

		/*
		 * Double the environment size whenever it is
		 * full.
		 */
		if (i == (*env_n)) {
			(*env_n) *= 2;
			newp = startd_zalloc(sizeof (*envp) *
			    (*env_n));
			(void) memcpy(newp, envp,
			    sizeof (*envp) * (*env_n) / 2);
			startd_free(envp,
			    sizeof (*envp) * (*env_n) / 2);
			envp = newp;
		}
	}

	/* Append a null pointer to the environment array to mark its end. */
	envp[i] = NULL;

cleanup:
	if (scf_handle_p) {
		(void) scf_handle_unbind(scf_handle_p);
		scf_handle_destroy(scf_handle_p);
	}
	scf_instance_destroy(inst);
	scf_pg_destroy(pg);
	scf_property_destroy(scf_prop_p);
	scf_value_destroy(scf_value);
	scf_snapshot_destroy(scf_snapshot_p);
	scf_iter_destroy(iter);

	return (envp);
}

/*
 * Initialize the environment from SMF service
 */
void
init_env()
{
	/* Load global environment array from SMF service */
	glob_envp = load_env_from_smf(&glob_env_n);

	/*
	 * Get the zonename once; it is used to set SMF_ZONENAME for methods.
	 */
	(void) getzonenamebyid(getzoneid(), zonename, sizeof (zonename));
}

/*
 * Reload the environment.
 *
 * The dgraph_lock might be held by this thread on entry.
 */
void
reload_env()
{
	char **new_envp = NULL, **tmp_envp = NULL;
	int i = 0, new_env_n = 0, tmp_env_n = 0;

	/* Load glob_envp structure from SMF service */
	new_envp = load_env_from_smf(&new_env_n);

	/* Acquire a write-lock for readers to wait */
	(void) pthread_rwlock_wrlock(&glob_envp_rwlock);

	tmp_envp = glob_envp;
	tmp_env_n = glob_env_n;

	/*
	 * Swap the new environment array with the
	 * global environment array.
	 */
	glob_env_n = new_env_n;
	glob_envp = new_envp;

	/* Release the write-lock */
	(void) pthread_rwlock_unlock(&glob_envp_rwlock);

	/* Free the old environment array */
	if (tmp_envp != NULL) {
		while (tmp_envp[i] != NULL) {
			startd_free(tmp_envp[i], strlen(tmp_envp[i]) + 1);
			i++;
		}
		startd_free(tmp_envp,
		    sizeof (*tmp_envp) * tmp_env_n);
	}
}

static int
valid_env_var(const char *var, const restarter_inst_t *inst, const char *path)
{

	char *cp = strchr(var, '=');

	if (cp == NULL || cp == var) {
		if (inst != NULL)
			log_instance(inst, B_FALSE, "Invalid environment "
			    "variable \"%s\".", var);
		return (0);
	} else if (strncmp(var, "SMF_", 4) == 0) {
		if (inst != NULL)
			log_instance(inst, B_FALSE, "Invalid environment "
			    "variable \"%s\"; \"SMF_\" prefix is reserved.",
			    var);
		return (0);
	} else if (path != NULL && strncmp(var, "PATH=", 5) == 0) {
		return (0);
	}

	return (1);
}

static char **
find_dup(const char *var, char **env, const restarter_inst_t *inst)
{
	char **p;
	char *tmp;

	for (p = env; *p != NULL; p++) {
		assert((tmp = strchr(*p, '=')) != NULL);
		tmp++;
		if (strncmp(*p, var, tmp - *p) == 0)
			break;
	}

	if (*p == NULL)
		return (NULL);

	/*
	 * The first entry in the array can be ignored when it is the
	 * default path.
	 */
	if (inst != NULL && p != env &&
	    strncmp(*p, DEF_PATH, strlen(DEF_PATH)) != 0) {
		log_instance(inst, B_FALSE, "Ignoring duplicate "
		    "environment variable \"%s\".", *p);
	}

	return (p);
}

/*
 * Create an environment which is appropriate for spawning an SMF
 * aware process. The new environment will consist of the values from
 * the global environment as modified by the supplied (local) environment.
 *
 * In order to preserve the correctness of the new environment,
 * various checks are performed on the local environment (init_env()
 * is relied upon to ensure the global environment is correct):
 *
 * - All SMF_ entries are ignored. All SMF_ entries should be provided
 *   by this function.
 * - Duplicates in the entry are eliminated.
 * - Malformed entries are eliminated.
 *
 * Detected errors are logged as warnings to the appropriate instance
 * logfile, since a single bad entry should not be enough to prevent
 * an SMF_ functional environment from being created. The faulty entry
 * is then ignored when building the environment.
 *
 * If env is NULL, then the return is an environment which contains
 * all default values.
 *
 * If "path" is non-NULL, it will silently over-ride any previous
 * PATH environment variable.
 *
 * NB: The returned env and strings are allocated using startd_alloc().
 *
 * The dgraph_lock must never be taken by the thread when entering this
 * function. It might cause a deadlock.
 *
 * So, dgraph_lock and glob_envp_rwlock (read lock) should never be held
 * by any thread at the same time.
 */
char **
set_smf_env(char **env, size_t env_sz, const char *path,
    const restarter_inst_t *inst, const char *method)
{
	char **nenv;
	char **p, **np;
	size_t nenv_size;
	size_t sz;

	(void) pthread_rwlock_rdlock(&glob_envp_rwlock);

	/*
	 * Max. of glob_env, env, four SMF_ variables,
	 * path, and terminating NULL.
	 */
	nenv_size = glob_env_n + env_sz + 4 + 1 + 1;

	nenv = startd_zalloc(sizeof (char *) * nenv_size);

	np = nenv;

	if (path != NULL) {
		sz = strlen(path) + 1;
		*np = startd_alloc(sz);
		(void) strlcpy(*np, path, sz);
		np++;
	}

	if (inst) {
		sz = sizeof ("SMF_FMRI=") + strlen(inst->ri_i.i_fmri);
		*np = startd_alloc(sz);
		(void) strlcpy(*np, "SMF_FMRI=", sz);
		(void) strlcat(*np, inst->ri_i.i_fmri, sz);
		np++;
	}

	if (method) {
		sz = sizeof ("SMF_METHOD=") + strlen(method);
		*np = startd_alloc(sz);
		(void) strlcpy(*np, "SMF_METHOD=", sz);
		(void) strlcat(*np, method, sz);
		np++;
	}

	sz = sizeof ("SMF_RESTARTER=") + strlen(SCF_SERVICE_STARTD);
	*np = startd_alloc(sz);
	(void) strlcpy(*np, "SMF_RESTARTER=", sz);
	(void) strlcat(*np, SCF_SERVICE_STARTD, sz);
	np++;

	sz = sizeof ("SMF_ZONENAME=") + strlen(zonename);
	*np = startd_alloc(sz);
	(void) strlcpy(*np, "SMF_ZONENAME=", sz);
	(void) strlcat(*np, zonename, sz);
	np++;

	for (p = glob_envp; *p != NULL; p++) {
		if (valid_env_var(*p, inst, path)) {
			sz = strlen(*p) + 1;
			*np = startd_alloc(sz);
			(void) strlcpy(*np, *p, sz);
			np++;
		}
	}

	(void) pthread_rwlock_unlock(&glob_envp_rwlock);

	if (env) {
		for (p = env; *p != NULL; p++) {
			char **dup_pos;

			if (!valid_env_var(*p, inst, path))
				continue;

			if ((dup_pos = find_dup(*p, nenv, inst)) != NULL) {
				startd_free(*dup_pos, strlen(*dup_pos) + 1);
				sz = strlen(*p) + 1;
				*dup_pos = startd_alloc(sz);
				(void) strlcpy(*dup_pos, *p, sz);
			} else {
				sz = strlen(*p) + 1;
				*np = startd_alloc(sz);
				(void) strlcpy(*np, *p, sz);
				np++;
			}
		}
	}
	*np = NULL;

	return (nenv);
}
