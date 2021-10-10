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
 * Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <alloca.h>
#include <libnvpair.h>
#include <libhotplug.h>
#include <libhotplug_impl.h>
#include <sys/types.h>
#include <sys/sunddi.h>
#include <sys/ddi_hp.h>
#include <sys/modctl.h>
#include "hotplugd_impl.h"

/*
 * All operations affecting kernel state are serialized.
 */
static pthread_mutex_t	hotplug_lock = PTHREAD_MUTEX_INITIALIZER;

/*
 * Local functions.
 */
static boolean_t check_rcm_required(hp_node_t, int);
static int pack_string_common(const char *, char **, size_t *);
static void unpack_string_common(char *, size_t, char **,
    int (*)(const char *));

static int property_skip_func(const char *);
static void free_properties(ddi_hp_property_t *);

static int pack_state(ddi_hp_cn_state_t *, ddi_hp_cn_state_code_t,
    const char *);
static void free_state(ddi_hp_cn_state_t *);

/*
 * changestate()
 *
 *	Perform a state change operation.
 *
 *	NOTE: all operations are serialized, using a global lock.
 */
int
changestate(const char *path, const char *connection, int state_code,
    const char *state_priv_string, uint_t flags, int *old_statep,
    hp_node_t *resultsp)
{
	hp_node_t		root = NULL;
	char			**rsrcs = NULL;
	boolean_t		use_rcm = B_FALSE;
	ddi_hp_cn_state_t	state;
	int			rv;

	dprintf("changestate(path=%s, connection=%s, state=0x%x,"
	    " state_priv=%s, flags=0x%x)\n", path, connection, state_code,
	    state_priv_string ? state_priv_string : "NULL", flags);

	/* Initialize results */
	*resultsp = NULL;
	*old_statep = -1;

	/* Lock hotplug */
	(void) pthread_mutex_lock(&hotplug_lock);

	/* Get an information snapshot, without usage details */
	if ((rv = getinfo(path, connection, 0, &root)) != 0) {
		(void) pthread_mutex_unlock(&hotplug_lock);
		dprintf("changestate: getinfo() failed (%s)\n", strerror(rv));
		return (rv);
	}

	/* Record current state (used in hotplugd_door.c for auditing) */
	*old_statep = hp_state(root);

	/* Check if a device is present */
	if (*old_statep == DDI_HP_CN_STATE_EMPTY) {
		(void) pthread_mutex_unlock(&hotplug_lock);
		dprintf("changestate: no device is present.\n");
		hp_fini(root);
		return (ENODEV);
	}

	/* Construct the target state argument */
	if ((rv = pack_state(&state, state_code, state_priv_string)) != 0) {
		(void) pthread_mutex_unlock(&hotplug_lock);
		dprintf("changestate: failed to pack state argument.\n");
		hp_fini(root);
		return (rv);
	}

	/* Check if RCM interactions are required */
	use_rcm = check_rcm_required(root, state_code);

	/* If RCM is required, perform RCM offline */
	if (use_rcm) {

		dprintf("changestate: RCM offline is required.\n");

		/* Get RCM resources */
		if ((rv = rcm_resources(root, &rsrcs)) != 0) {
			dprintf("changestate: rcm_resources() failed.\n");
			(void) pthread_mutex_unlock(&hotplug_lock);
			hp_fini(root);
			free_state(&state);
			return (rv);
		}

		/* Request RCM offline */
		if ((rsrcs != NULL) &&
		    ((rv = rcm_offline(rsrcs, flags, root)) != 0)) {
			dprintf("changestate: rcm_offline() failed.\n");
			rcm_online(rsrcs);
			(void) pthread_mutex_unlock(&hotplug_lock);
			free_rcm_resources(rsrcs);
			free_state(&state);
			*resultsp = root;
			return (rv);
		}
	}

	/* The information snapshot is no longer needed */
	hp_fini(root);

	/* Stop now if QUERY flag was specified */
	if (flags & HPQUERY) {
		dprintf("changestate: operation was QUERY only.\n");
		rcm_online(rsrcs);
		(void) pthread_mutex_unlock(&hotplug_lock);
		free_rcm_resources(rsrcs);
		free_state(&state);
		return (0);
	}

	/* Do state change in kernel */
	rv = 0;
	if (modctl(MODHPOPS, MODHPOPS_CHANGE_STATE, path, connection, &state))
		rv = errno;
	dprintf("changestate: modctl(MODHPOPS_CHANGE_STATE) = %d.\n", rv);

	/*
	 * If RCM is required, then perform an RCM online or RCM remove
	 * operation.  Which depends upon if modctl succeeded or failed.
	 */
	if (use_rcm && (rsrcs != NULL)) {

		/* RCM online if failure, or RCM remove if successful */
		if (rv == 0)
			rcm_remove(rsrcs);
		else
			rcm_online(rsrcs);

		/* RCM resources no longer required */
		free_rcm_resources(rsrcs);
	}

	/* Unlock hotplug */
	(void) pthread_mutex_unlock(&hotplug_lock);

	free_state(&state);

	*resultsp = NULL;
	return (rv);
}

/*
 * private_options()
 *
 *	Implement set/get of bus private options.
 *
 *	NOTE: all operations are serialized, using a global lock.
 */
int
private_options(const char *path, const char *connection, hp_cmd_t cmd,
    const char *options, char **resultsp)
{
	ddi_hp_property_t	prop;
	ddi_hp_property_t	results;
	char			*values = NULL;
	int			rv;

	dprintf("private_options(path=%s, connection=%s, options='%s')\n",
	    path, connection, options);

	/* Initialize property arguments */
	if ((rv = pack_string_common(options, &prop.nvlist_buf,
	    &prop.buf_size)) != 0) {
		dprintf("private_options: failed to pack properties.\n");
		return (rv);
	}

	/* Initialize results */
	(void) memset(&results, 0, sizeof (ddi_hp_property_t));
	results.buf_size = HP_PRIVATE_BUF_SZ;
	results.nvlist_buf = (char *)calloc(1, HP_PRIVATE_BUF_SZ);
	if (results.nvlist_buf == NULL) {
		dprintf("private_options: failed to allocate buffer.\n");
		free_properties(&prop);
		return (ENOMEM);
	}

	/* Lock hotplug */
	(void) pthread_mutex_lock(&hotplug_lock);

	/* Perform the command */
	rv = 0;
	if (cmd == HP_CMD_GETPRIVATE) {
		if (modctl(MODHPOPS, MODHPOPS_BUS_GET, path, connection,
		    &prop, &results))
			rv = errno;
		dprintf("private_options: modctl(MODHPOPS_BUS_GET) = %d\n", rv);
	} else {
		if (modctl(MODHPOPS, MODHPOPS_BUS_SET, path, connection,
		    &prop, &results))
			rv = errno;
		dprintf("private_options: modctl(MODHPOPS_BUS_SET) = %d\n", rv);
	}

	/* Unlock hotplug */
	(void) pthread_mutex_unlock(&hotplug_lock);

	/* Parse results */
	if (rv == 0) {
		unpack_string_common(results.nvlist_buf, results.buf_size,
		    &values, property_skip_func);
		*resultsp = values;
	}

	/* Cleanup */
	free_properties(&prop);
	free_properties(&results);

	return (rv);
}

/*
 * install()
 *
 *	Implement installation of a hotplug connection's dependents.
 *
 *	NOTE: all operations are serialized, using a global lock.
 */
int
install(const char *path, const char *cn, uint_t flags, hp_node_t *resultsp)
{
	hp_node_t		root = NULL;
	ddi_hp_cn_state_t	state;
	int			rv;

	dprintf("install(path=%s, cn=%s, flags=0x%x)\n", path, cn, flags);

	/* Initialize results */
	*resultsp = NULL;

	/* Lock hotplug */
	(void) pthread_mutex_lock(&hotplug_lock);

	/* Get information snapshot */
	if ((rv = getinfo(path, cn, 0, &root)) != 0) {
		(void) pthread_mutex_unlock(&hotplug_lock);
		dprintf("install: getinfo() failed (%s)\n", strerror(rv));
		return (rv);
	}

	/* Cannot install dependents unless root is installed */
	if (!HP_IS_ONLINE(root->hp_state)) {

		dprintf("install: upgrading port state.\n");

		/* Construct the target state argument */
		rv = pack_state(&state, DDI_HP_CN_STATE_ONLINE, NULL);
		if (rv != 0) {
			(void) pthread_mutex_unlock(&hotplug_lock);
			dprintf("install: failed to pack state argument.\n");
			hp_fini(root);
			return (rv);
		}

		/* Online the connection */
		if (modctl(MODHPOPS, MODHPOPS_CHANGE_STATE, path, cn,
		    &state) != 0)
			rv = errno;
		dprintf("install: modctl(MODHPOPS_CHANGE_STATE) = %d.\n", rv);

		free_state(&state);

		if (rv != 0) {
			(void) pthread_mutex_unlock(&hotplug_lock);
			hp_fini(root);
			return (rv);
		}
	}

	/* Do the install */
	rv = 0;
	if (modctl(MODHPOPS, MODHPOPS_INSTALL, path, cn))
		rv = errno;
	dprintf("install: modctl(MODHPOPS_INSTALL) = %d.\n", rv);

	/* Unlock hotplug */
	(void) pthread_mutex_unlock(&hotplug_lock);

	/* The information snapshot is no longer needed */
	hp_fini(root);

	return (rv);
}

/*
 * uninstall()
 *
 *	Implement un-installation of a hotplug connection's dependents.
 *
 *	An RCM offline on the dependent device paths must succeed
 *	before the dependents can be uninstalled in the kernel.
 *
 *	NOTE: all operations are serialized, using a global lock.
 */
int
uninstall(const char *path, const char *cn, uint_t flags, hp_node_t *resultsp)
{
	hp_node_t	root = NULL;
	char		**rsrcs = NULL;
	int		rv;

	dprintf("uninstall(path=%s, cn=%s, flags=0x%x)\n", path, cn, flags);

	/* Initialize results */
	*resultsp = NULL;

	/* Lock hotplug */
	(void) pthread_mutex_lock(&hotplug_lock);

	/* Get information snapshot */
	if ((rv = getinfo(path, cn, 0, &root)) != 0) {
		(void) pthread_mutex_unlock(&hotplug_lock);
		dprintf("uninstall: getinfo() failed (%s)\n", strerror(rv));
		return (rv);
	}

	/* Cannot uninstall dependents unless root is installed */
	if (!HP_IS_ONLINE(root->hp_state)) {
		(void) pthread_mutex_unlock(&hotplug_lock);
		dprintf("uninstall: invalid origin state.\n");
		hp_fini(root);
		return (ENOTSUP);
	}

	/* Get RCM resources */
	if ((rv = rcm_resources_depends(root, &rsrcs)) != 0) {
		(void) pthread_mutex_unlock(&hotplug_lock);
		dprintf("uninstall: RCM resources failed (%s)\n", strerror(rv));
		hp_fini(root);
		return (rv);
	}

	/* Use RCM to offline dependent resources */
	if ((rsrcs != NULL) && ((rv = rcm_offline(rsrcs, flags, root)) != 0)) {
		dprintf("uninstall: rcm_offline() failed (%s).\n",
		    strerror(rv));
		rcm_online(rsrcs);
		(void) pthread_mutex_unlock(&hotplug_lock);
		free_rcm_resources(rsrcs);
		*resultsp = root;
		return (rv);
	}

	/* Information snapshot is no longer required */
	hp_fini(root);

	/* Stop now if QUERY flag was specified */
	if (flags & HPQUERY) {
		dprintf("uninstall: operation was QUERY only.\n");
		if (rsrcs != NULL) {
			rcm_online(rsrcs);
			free_rcm_resources(rsrcs);
		}
		(void) pthread_mutex_unlock(&hotplug_lock);
		return (0);
	}

	/* Do the un-install */
	rv = 0;
	if (modctl(MODHPOPS, MODHPOPS_UNINSTALL, path, cn))
		rv = errno;
	dprintf("uninstall: modctl(MODHPOPS_UNINSTALL) = %d.\n", rv);

	/* Finalize RCM transactions */
	if (rsrcs != NULL) {
		if (rv == 0)
			rcm_remove(rsrcs);
		else
			rcm_online(rsrcs);
		free_rcm_resources(rsrcs);
	}

	/* Unlock hotplug */
	(void) pthread_mutex_unlock(&hotplug_lock);

	return (rv);
}

/*
 * unpack_state_priv()
 *
 *	Given a packed nvlist of state private info, unpack all name/value
 *	pairs into a string in getsubopt(3C) format.
 */
char *
unpack_state_priv(char *nvlist_buf, size_t buf_len)
{
	char *state_priv_string;

	if (nvlist_buf == NULL || buf_len == 0)
		return (NULL);

	/* unpack_string_common() will init state_priv_string to NULL */
	unpack_string_common(nvlist_buf, buf_len, &state_priv_string, NULL);

	return (state_priv_string);
}

/*
 * check_rcm_required()
 *
 *	Given the root of a changestate operation and the target
 *	state, determine if RCM interactions will be required.
 */
static boolean_t
check_rcm_required(hp_node_t root, int target_state)
{
	/*
	 * RCM is required when transitioning an ENABLED
	 * connector to a non-ENABLED state.
	 */
	if ((root->hp_type == HP_NODE_CONNECTOR) &&
	    HP_IS_ENABLED(root->hp_state) && !HP_IS_ENABLED(target_state))
		return (B_TRUE);

	/*
	 * RCM is required when transitioning an OPERATIONAL
	 * port to a non-OPERATIONAL state.
	 */
	if ((root->hp_type == HP_NODE_PORT) &&
	    HP_IS_ONLINE(root->hp_state) && HP_IS_OFFLINE(target_state))
		return (B_TRUE);

	/* RCM is not required in other cases */
	return (B_FALSE);
}

/*
 * pack_string_common()
 *
 *	Given a string of options in getsubopt(3C) format, construct
 *	a packed nvlist.  All entries in the nvlist will be strings.
 *	If an option was only specified as a name, with no assigned
 *	value, then the strings value in the nvlist will be an empty
 *	string.
 */
static int
pack_string_common(const char *options, char **pbuf, size_t *plen)
{
	nvlist_t	*nvl;
	char		*buf, *tmp, *name, *value, *next;
	size_t		len;

	/* Initialize results */
	*pbuf = NULL;
	*plen = 0;

	if (options == NULL)
		return (0);

	/* Do nothing if options string is empty */
	if ((len = strlen(options)) == 0) {
		dprintf("pack_properties: options string is empty.\n");
		return (ENOENT);
	}

	/* Avoid modifying the input string by using a copy on the stack */
	if ((tmp = (char *)alloca(len + 1)) == NULL) {
		log_err("Failed to allocate buffer for private options.\n");
		return (ENOMEM);
	}
	(void) strlcpy(tmp, options, len + 1);

	/* Allocate the nvlist */
	if (nvlist_alloc(&nvl, NV_UNIQUE_NAME, 0) != 0) {
		log_err("Failed to allocate private options nvlist.\n");
		return (ENOMEM);
	}

	/* Add each option from the string */
	for (name = tmp; name != NULL; name = next) {

		/* Isolate current name/value, and locate the next */
		if ((next = strchr(name, ',')) != NULL) {
			*next = '\0';
			next++;
		}

		/* Split current name/value pair */
		if ((value = strchr(name, '=')) != NULL) {
			*value = '\0';
			value++;
		} else {
			value = "";
		}

		/* Add the option to the nvlist */
		if (nvlist_add_string(nvl, name, value) != 0) {
			log_err("Failed to add private option to nvlist.\n");
			nvlist_free(nvl);
			return (EFAULT);
		}
	}

	/* Pack the nvlist */
	len = 0;
	buf = NULL;
	if (nvlist_pack(nvl, &buf, &len, NV_ENCODE_NATIVE, 0) != 0) {
		log_err("Failed to pack private options nvlist.\n");
		nvlist_free(nvl);
		return (EFAULT);
	}

	/* Save results */
	*pbuf = buf;
	*plen = len;

	/* The nvlist is no longer needed */
	nvlist_free(nvl);

	return (0);
}

/*
 * unpack_string_common()
 *
 *	Given a packed nvlist, unpack all string values stored
 *	within the nvlist into a string of name/value pairs in
 *	getsubopt(3C) format.  An optional 'skip_func' can be
 *	supplied to filter certain name/value pairs from being
 *	included in the resulting string.
 */
static void
unpack_string_common(char *nvlist_buf, size_t buf_size, char **optionsp,
    int (*skip_func)(const char *name))
{
	nvlist_t	*nvl = NULL;
	nvpair_t	*nvp;
	boolean_t	first_flag;
	char		*name, *value, *options;
	size_t		len;

	/* Initialize results */
	*optionsp = NULL;

	/* Do nothing if nvlist does not exist */
	if ((nvlist_buf == NULL) || (buf_size == 0)) {
		dprintf("unpack_string_common: no nvlist exists.\n");
		return;
	}

	/* Unpack the nvlist */
	if (nvlist_unpack(nvlist_buf, buf_size, &nvl, 0) != 0) {
		log_err("Failed to unpack string nvlist.\n");
		return;
	}

	/* Compute the size of the options string */
	for (len = 0, nvp = NULL; nvp = nvlist_next_nvpair(nvl, nvp); ) {

		name = nvpair_name(nvp);

		/* Skip this one, and anything not a string */
		if ((skip_func && skip_func(name)) ||
		    (nvpair_type(nvp) != DATA_TYPE_STRING))
			continue;

		(void) nvpair_value_string(nvp, &value);

		/* Account for '=' signs, commas, and terminating NULL */
		len += (strlen(name) + strlen(value) + 2);
	}

	/* Allocate the resulting options string */
	if ((options = (char *)calloc(len, sizeof (char))) == NULL) {
		log_err("Failed to allocate options string.\n");
		nvlist_free(nvl);
		return;
	}

	/* Copy name/value pairs into the options string */
	first_flag = B_TRUE;
	for (nvp = NULL; nvp = nvlist_next_nvpair(nvl, nvp); ) {

		name = nvpair_name(nvp);

		/* Skip this one, and anything not a string */
		if ((skip_func && skip_func(name)) ||
		    (nvpair_type(nvp) != DATA_TYPE_STRING))
			continue;

		if (!first_flag)
			(void) strlcat(options, ",", len);

		(void) strlcat(options, name, len);

		(void) nvpair_value_string(nvp, &value);

		if (strlen(value) > 0) {
			(void) strlcat(options, "=", len);
			(void) strlcat(options, value, len);
		}

		first_flag = B_FALSE;
	}

	/* The unpacked nvlist is no longer needed */
	nvlist_free(nvl);

	/* Save results */
	*optionsp = options;
}

/*
 * property_skip_func()
 *
 *	Filter function for unpack_string_common(), used to
 *	exclude the "cmd=set" or "cmd=get" that may occur in
 *	private options strings.
 */
static int
property_skip_func(const char *name)
{
	return (strcmp(name, "cmd") == 0);
}

/*
 * free_properties()
 *
 *	Destroy a structure containing a packed nvlist of bus
 *	private properties.
 */
static void
free_properties(ddi_hp_property_t *prop)
{
	if (prop) {
		if (prop->nvlist_buf)
			free(prop->nvlist_buf);
		(void) memset(prop, 0, sizeof (ddi_hp_property_t));
	}
}

/*
 * pack_state()
 *
 *	Given a state code and a string of state private info, fill a state
 *	structure.  'state_priv_string' must be in getsubopt(3C) format.
 *
 *	free_state() must be called later to destroy the state, since memory
 *	may be allocated for the state private info in the returned state
 *	structure that 'statep' points to.
 */
static int
pack_state(ddi_hp_cn_state_t *statep, ddi_hp_cn_state_code_t state_code,
    const char *state_priv_string)
{
	ddi_hp_state_priv_t *state_priv_p;
	int rv;

	statep->state_code = state_code;

	/* Quick path for no state priv info case */
	if (state_priv_string == NULL) {
		statep->state_priv = NULL;
		return (0);
	}

	/* Allocate state private info nvlist buffer */
	state_priv_p
	    = (ddi_hp_state_priv_t *)calloc(1, sizeof (ddi_hp_state_priv_t));
	if (state_priv_p == NULL) {
		dprintf("pack_state: can't alloc state priv info.\n");
		return (ENOMEM);
	}

	/* Pack the state private info string into nvlist */
	if ((rv = pack_string_common(state_priv_string,
	    &state_priv_p->nvlist_buf, &state_priv_p->buf_size)) != 0) {
		dprintf("pack_state: failed to pack state priv info.\n");
		return (rv);
	}

	if (state_priv_p->buf_size == 0) {
		/* no actual state priv info, set to NULL */
		statep->state_priv = NULL;
		free(state_priv_p);
	} else {
		statep->state_priv = (void *)state_priv_p;
	}

	return (0);
}

/*
 * free_state()
 *
 *	Destroy the packed nvlist of state private info contained
 *	in a state structure.
 */
static void
free_state(ddi_hp_cn_state_t *statep)
{
	ddi_hp_state_priv_t *state_priv_p;

	if (statep) {
		state_priv_p = (ddi_hp_state_priv_t *)statep->state_priv;
		if (state_priv_p) {
			if (state_priv_p->nvlist_buf)
				free(state_priv_p->nvlist_buf);
			free(state_priv_p);
		}

		(void) memset(statep, 0, sizeof (ddi_hp_cn_state_t));
	}
}
