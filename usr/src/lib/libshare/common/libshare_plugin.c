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

#include <sys/types.h>
#include <sys/param.h>
#include <dlfcn.h>
#include <link.h>
#include <dirent.h>
#include <libintl.h>
#include <sys/systeminfo.h>
#include <synch.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <atomic.h>

#include "libshare.h"
#include "libshare_impl.h"

#define	SAPLUGIN_LOADED(type)	(plugins_initialized & (1 << (type)))

static sa_plugin_t *sa_fs_list;
static rwlock_t sa_fs_lock;

static sa_plugin_t *sa_proto_list;
static rwlock_t sa_proto_lock;

static sa_plugin_t *sa_cache_list;
static rwlock_t sa_cache_lock;

static uint32_t plugins_initialized; /* mask of plugins that have been loaded */

/*
 * load_and_check(path, libname, type)
 *
 * dlopen the library and check to see if a valid libshare v2
 * library of the correct type. If it is, load it and link it together
 * as appropriate.
 *
 * The pi_init() routine is also called to give the plugin a chance
 * to do any required initialization.
 */
static void
load_and_check(char *root, struct dirent64 *dp, sa_plugin_type_t type)
{
	int rc = SA_OK;
	size_t len;
	size_t dir_len;
	sa_plugin_t *plugin;
	sa_plugin_t **pi_list;
	rwlock_t *pi_lock;
	sa_plugin_ops_t *pi_ops;
	char isa[MAXISALEN];
	char path[MAXPATHLEN];
	struct stat st;
	void *dlhandle;

#if defined(_LP64)
	if (sysinfo(SI_ARCHITECTURE_64, isa, MAXISALEN) == -1)
		isa[0] = '\0';
#else
	isa[0] = '\0';
#endif
	len = strlen(root);
	dir_len = strlen(dp->d_name);
	if ((len + dir_len + MAXISALEN) > MAXPATHLEN) {
		salog_error(SA_INVALID_PLUGIN_NAME,
		    "Error loading libshare plugin %s/%s",
		    root, dp->d_name);
		return;
	}

	(void) snprintf(path, MAXPATHLEN,
	    "%s/%s/%s", root, isa, dp->d_name);

	/*
	 * If file doesn't exist, don't try to map it
	 */
	if (stat(path, &st) < 0) {
		salog_error(0, "libshare plugin library not found: %s", path);
		return;
	}

	if ((dlhandle = dlopen(path, RTLD_FIRST|RTLD_LAZY)) == NULL) {
		salog_error(0, "Error loading libshare plugin: %s: %s",
		    path, dlerror());
		return;
	}

	pi_ops = (sa_plugin_ops_t *)dlsym(dlhandle, "sa_plugin_ops");
	if (pi_ops == NULL) {
		salog_error(0, "Error loading libshare plugin: %s: %s",
		    path, dlerror());
		(void) dlclose(dlhandle);
		return;
	}

	if (pi_ops->pi_ptype != type) {
		salog_error(SA_INVALID_PLUGIN_TYPE,
		    "Error loading libshare plugin: %s", path);
		(void) dlclose(dlhandle);
		return;
	}

	plugin = (sa_plugin_t *)calloc(1, sizeof (sa_plugin_t));

	if (plugin == NULL) {
		(void) dlclose(dlhandle);
		salog_error(SA_NO_MEMORY,
		    "Error loading libshare plugin: %s", path);
		return;
	}

	switch (type) {
	case SA_PLUGIN_FS:
		pi_lock = &sa_fs_lock;
		pi_list = &sa_fs_list;
		break;
	case SA_PLUGIN_PROTO:
		pi_lock = &sa_proto_lock;
		pi_list = &sa_proto_list;
		break;
	case SA_PLUGIN_CACHE:
		pi_lock = &sa_cache_lock;
		pi_list = &sa_cache_list;
		break;
	default:
		salog_error(SA_INVALID_PLUGIN_TYPE,
		    "Error loading libshare plugin: %s", path);
		(void) dlclose(dlhandle);
		free(plugin);
		return;
	}

	if ((pi_ops->pi_init != NULL) &&
	    ((rc = pi_ops->pi_init()) != SA_OK)) {
		salog_error(rc,
		    "Error initializing libshare plugin: %s", path);
		(void) dlclose(dlhandle);
		free(plugin);
		return;
	}

	/*
	 * add plugin to list
	 */
	(void) rw_wrlock(pi_lock);
	plugin->pi_next = *pi_list;
	plugin->pi_ops = pi_ops;
	plugin->pi_hdl = dlhandle;
	*pi_list = plugin;
	(void) rw_unlock(pi_lock);
}

/*
 * plugin_load(type)
 *
 * find (and load) all modules of given type
 */
static void
saplugin_load(sa_plugin_type_t type)
{
	DIR *dirp;
	struct dirent64 *dp;
	char *root;

	switch (type) {
	case SA_PLUGIN_FS:
		root = SA_PLUGIN_ROOT_FS;
		break;
	case SA_PLUGIN_PROTO:
		root = SA_PLUGIN_ROOT_PROTO;
		break;
	case SA_PLUGIN_CACHE:
		root = SA_PLUGIN_ROOT_CACHE;
		break;
	default:
		return;
	}

	dirp = opendir(root);
	if (dirp == NULL) {
		salog_debug(SA_NO_PLUGIN_DIR,
		    "saplugin_load: %s", root);
		return;
	}

	while ((dp = readdir64(dirp)) != NULL) {
		if (strncmp(dp->d_name, PLUGIN_LIB_PREFIX,
		    sizeof (PLUGIN_LIB_PREFIX) - 1) == 0) {
			(void) load_and_check(root, dp, type);
		}
	}
	(void) closedir(dirp);

	/*
	 * mark plugin type as initialized regardless of success.
	 * This will prevent continuous attempts to load
	 * bad or non-existent plugins.
	 */
	atomic_or_32(&plugins_initialized, 1 << type);
}

/*
 * saplugin_unload
 *
 * Unload all plugin modules of the given type.
 */
static void
saplugin_unload(sa_plugin_type_t pi_type)
{
	rwlock_t *pi_lock;
	sa_plugin_t **pi_list;
	sa_plugin_t *plugin, *next; /* use fs_plugin as generic type */

	switch (pi_type) {
	case SA_PLUGIN_FS:
		pi_lock = &sa_fs_lock;
		pi_list = &sa_fs_list;
		break;

	case SA_PLUGIN_PROTO:
		pi_lock = &sa_proto_lock;
		pi_list = &sa_proto_list;
		break;

	case SA_PLUGIN_CACHE:
		pi_lock = &sa_cache_lock;
		pi_list = &sa_cache_list;
		break;
	default:
		return;
	}

	(void) rw_wrlock(pi_lock);
	plugin = *pi_list;
	while (plugin != NULL) {
		next = plugin->pi_next;
		/*
		 * call pi_fini() routine to give
		 * plugin a chance to cleanup.
		 */
		if (plugin->pi_ops->pi_fini != NULL)
			(void) plugin->pi_ops->pi_fini();

		if (plugin->pi_hdl != NULL) {
			(void) dlclose(plugin->pi_hdl);
		}

		free(plugin);
		plugin = next;
	}

	atomic_and_32(&plugins_initialized, ~(1 << pi_type));
	(void) rw_unlock(pi_lock);
}

/*
 * saplugin_unload_all
 *
 * Unload all loaded plugin modules.
 * Called from _sa_fini() when libshare.so is unloaded.
 */
void
saplugin_unload_all(void)
{
	saplugin_unload(SA_PLUGIN_FS);
	saplugin_unload(SA_PLUGIN_PROTO);
	saplugin_unload(SA_PLUGIN_CACHE);
}

/*
 * saplugin_find_ops
 *
 * return ops table for specified plugin
 */
sa_plugin_ops_t *
saplugin_find_ops(sa_plugin_type_t type, uint32_t ops_type)
{
	sa_plugin_t *plugin;
	sa_plugin_t **pi_list;
	rwlock_t *pi_lock;

	/*
	 * load the plugin if not yet loaded
	 */
	if (!SAPLUGIN_LOADED(type))
		saplugin_load(type);

	switch (type) {
	case SA_PLUGIN_FS:
		pi_lock = &sa_fs_lock;
		pi_list = &sa_fs_list;
		break;
	case SA_PLUGIN_PROTO:
		pi_lock = &sa_proto_lock;
		pi_list = &sa_proto_list;
		break;
	case SA_PLUGIN_CACHE:
		pi_lock = &sa_cache_lock;
		pi_list = &sa_cache_list;
		break;
	default:
		return (NULL);
	}

	(void) rw_rdlock(pi_lock);
	for (plugin = *pi_list; plugin != NULL; plugin = plugin->pi_next) {
		if (plugin->pi_ops != NULL &&
		    plugin->pi_ops->pi_type == ops_type)
			break;
	}
	(void) rw_unlock(pi_lock);

	if (plugin != NULL)
		return (plugin->pi_ops);
	else
		return (NULL);
}

int
saplugin_get_protos(sa_proto_t **protos)
{
	int i;
	int plugin_cnt = 0;
	sa_plugin_t *plugin;

	if (protos == NULL)
		return (0);

	if (!SAPLUGIN_LOADED(SA_PLUGIN_PROTO))
		saplugin_load(SA_PLUGIN_PROTO);

	(void) rw_rdlock(&sa_proto_lock);
	for (plugin = sa_proto_list; plugin != NULL; plugin = plugin->pi_next) {
		if (plugin->pi_ops != NULL)
			plugin_cnt++;
	}

	*protos = calloc(plugin_cnt, sizeof (sa_proto_t));
	if (*protos == NULL) {
		(void) rw_unlock(&sa_proto_lock);
		return (0);
	}

	for (i = 0, plugin = sa_proto_list;
	    plugin != NULL;
	    ++i, plugin = plugin->pi_next) {
		if (plugin->pi_ops != NULL)
			(*protos)[i] = plugin->pi_ops->pi_type;
	}

	(void) rw_unlock(&sa_proto_lock);

	return (plugin_cnt);
}

/*
 * saplugin_next_ops
 *
 * Return pointer to next plugin in the list of type specified
 * If ops == NULL return first plugin in the list.
 */
sa_plugin_ops_t *
saplugin_next_ops(sa_plugin_type_t type, sa_plugin_ops_t *ops)
{
	sa_plugin_t **pi_list;
	rwlock_t *pi_lock;
	sa_plugin_t *plugin;

	/*
	 * load the plugin if not yet loaded
	 */
	if (!SAPLUGIN_LOADED(type))
		saplugin_load(type);

	switch (type) {
	case SA_PLUGIN_FS:
		pi_lock = &sa_fs_lock;
		pi_list = &sa_fs_list;
		break;
	case SA_PLUGIN_PROTO:
		pi_lock = &sa_proto_lock;
		pi_list = &sa_proto_list;
		break;
	case SA_PLUGIN_CACHE:
		pi_lock = &sa_cache_lock;
		pi_list = &sa_cache_list;
		break;
	default:
		return (NULL);
	}

	(void) rw_rdlock(pi_lock);
	if (ops == NULL) {
		plugin = *pi_list;
	} else {
		for (plugin = *pi_list; plugin != NULL;
		    plugin = plugin->pi_next) {
			if (plugin->pi_ops == ops) {
				plugin = plugin->pi_next;
				break;
			}
		}
	}
	(void) rw_unlock(pi_lock);

	if (plugin != NULL)
		return (plugin->pi_ops);
	else
		return (NULL);
}
