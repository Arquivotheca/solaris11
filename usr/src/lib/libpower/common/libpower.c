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
 * Consolidation Private libpower APIs
 */

#include <sys/types.h>
#include <stdio.h>
#include <errno.h>
#include <libintl.h>
#include <locale.h>
#include <unistd.h>
#include <strings.h>
#include <libuutil.h>
#include <libnvpair.h>
#include <libscf.h>
#include <libpower.h>
#include <libpower_impl.h>

static uu_dprintf_severity_t	pm_severity = UU_DPRINTF_SILENT;
uu_dprintf_t			*pm_log;

static struct pm_authorities_s pm_authorities[] = {
	{ PM_AUTHORITY_ALL,		PM_AUTHORITY_ALL_STR },
	{ PM_AUTHORITY_CURRENT,		PM_AUTHORITY_CURRENT_STR },
	{ PM_AUTHORITY_NONE,		PM_AUTHORITY_NONE_STR },
	{ PM_AUTHORITY_PLATFORM,	PM_AUTHORITY_PLATFORM_STR },
	{ PM_AUTHORITY_SMF,		PM_AUTHORITY_SMF_STR },
	{ PM_AUTHORITY_INVALID,		PM_AUTHORITY_INVALID_STR },
};

static void pm_fini_internal(void);

#pragma	init(pm_init_internal)
static void
pm_init_internal()
{
	const char *debug;

	if (!issetugid() &&
	    (debug = getenv(PM_ENV_DEBUG)) != NULL && debug[0] != 0 &&
	    uu_strtoint(debug, &pm_severity, sizeof (pm_severity),
	    0, 0, 0) == -1) {
		(void) fprintf(stderr, "LIBPOWER: $%s (%s): %s",
		    PM_ENV_DEBUG, debug, uu_strerror(uu_error()));
		pm_severity = UU_DPRINTF_SILENT;
	}

	pm_log = uu_dprintf_create(PM_LOG_DOMAIN, pm_severity, 0);
	uu_dprintf(pm_log, UU_DPRINTF_DEBUG, "debug log created\n");

	(void) atexit(pm_fini_internal);
}

static void
pm_fini_internal()
{
	if (pm_log != NULL) {
		uu_dprintf_destroy(pm_log);
	}
}


/*
 * pm_getprop allows the caller to retrieve a specified list of
 * property values from the kernel. The query may include an
 * optional authority, which filters on the values stored from
 * SMF, the PLATFORM, or the CURRENT values.
 *
 * A valid query list consists of 2 name value pairs:
 *
 * Name			Description
 * ------------------	---------------------------------------
 * PM_PROP_AUTHORITY	The data source to query as a string.
 *			May include PM_AUTHORITY_ANY to query
 *			all available data sources.
 * PM_PROP_QUERY	An nvlist string array where each element
 *			in the array is the property to query.
 *			See below.
 *
 * A property may have the form:
 *	[property_group_name/]property_name
 *
 * where a property_group_name, if specified, indicates a particular
 * set of managed properties.  If the property_group_name is specified,
 * only data sources within that managed group of properties that match
 * the authority specifier will be queried.
 *
 * If the property_group_name is not specified, all specified data
 * sources for all managed groups will be queried.
 */
pm_error_t
pm_getprop(nvlist_t **result, nvlist_t *query)
{
	pm_error_t	err;
	uint_t		propc;
	char		**propv;
	char		*authname;
	nvlist_t	*proplist;

	pm_authority_t	authority;

	err = PM_SUCCESS;
	errno = 0;
	if (result == NULL ||
	    nvlist_lookup_string(query, PM_PROP_AUTHORITY, &authname) != 0 ||
	    nvlist_lookup_string_array(query, PM_PROP_QUERY, &propv,
	    &propc) != 0) {
		/* One or more of the required arguments is invalid */
		uu_dprintf(pm_log, UU_DPRINTF_DEBUG, "%s invalid argument\n",
		    __FUNCTION__);

		errno = ENOENT;
		return (PM_ERROR_INVALID_ARGUMENT);
	}

	/* Validate the authority given in the query */
	proplist = NULL;
	authority = pm_authority_get(authname);
	if (authority == PM_AUTHORITY_INVALID) {
		uu_dprintf(pm_log, UU_DPRINTF_DEBUG,
		    "%s invalid authority %s in query\n", __FUNCTION__,
		    authname);
		errno = EINVAL;
		return (PM_ERROR_INVALID_AUTHORITY);
	}

	/*
	 * Retrieve all of the properties from SMF, which includes property
	 * group names, and add in any new properties from the kernel and
	 * decorate the existing ones with current and platform values.
	 */
	err = pm_smf_listprop(&proplist, PM_SVC_POWER);
	if (err == PM_SUCCESS) {
		err = pm_kernel_listprop(&proplist);
		if (err == PM_SUCCESS) {
			/*
			 * Filter the results and pick out just what was
			 * requested
			 */
			err = pm_result_filter(result, proplist, authority,
			    propv, propc);
		}
	}

	return (err);
}


pm_error_t
pm_setprop(nvlist_t *new)
{
	pm_error_t	err;
	scf_error_t	serr;
	nvpair_t	*nvp;

	errno = 0;
	if (new == NULL) {
		uu_dprintf(pm_log, UU_DPRINTF_DEBUG, "%s invalid argument\n",
		    __FUNCTION__);
		errno = EINVAL;
		return (PM_ERROR_INVALID_ARGUMENT);
	}

	err = PM_SUCCESS;
	nvp = nvlist_next_nvpair(new, NULL);
	while (err == PM_SUCCESS && nvp != NULL) {
		/*
		 * Attempt to write this property to SMF.  Each property
		 * is written in it's own transaction.  This means that
		 * if a property fails to write, the properties that came
		 * before it will succeed.
		 *
		 * This is less than ideal. However, property writes using
		 * the interface we are using requires all the properties to
		 * be in the same property group; we have multiple groups
		 * and this opens up the possibility of partial success
		 * anyway.
		 */
		err = pm_smf_setprop(nvp, PM_SVC_POWER);

		/* prepare for the next iteration */
		nvp = nvlist_next_nvpair(new, nvp);
	}
	if (err == PM_SUCCESS) {
		/*
		 * Request an SMF refresh to sync the updated values with
		 * the kernel.  This will happen asynchronously, and if it
		 * fails the service will be put in to maintenance.
		 */
		uu_dprintf(pm_log, UU_DPRINTF_DEBUG, "%s refreshing %s\n",
		    __FUNCTION__, PM_SVC_POWER);
		serr = smf_refresh_instance(PM_SVC_POWER);
		switch (serr) {
		case SCF_SUCCESS:
			/* DO NOTHING */
			break;

		default:
			/*
			 * Indicate to the caller than an SMF error has
			 * occurred.
			 */
			uu_dprintf(pm_log, UU_DPRINTF_DEBUG,
			    "%s unable to refresh %s: %s\n",
			    __FUNCTION__, PM_SVC_POWER,
			    scf_strerror(scf_error()));
			err = PM_ERROR_SCF;
			break;
		}
	}
	uu_dprintf(pm_log, UU_DPRINTF_DEBUG, "%s returning %d errno %d\n",
	    __FUNCTION__, err, errno);

	return (err);
}


pm_error_t
pm_listprop(nvlist_t **result)
{
	pm_error_t	err;

	errno = 0;
	if (result == NULL) {
		errno = EINVAL;
		return (PM_ERROR_INVALID_ARGUMENT);
	}

	*result = NULL;

	/*
	 * Retrieve all properties from SMF. This will populate the result
	 * list with property group names automatically.
	 */
	err = pm_smf_listprop(result, PM_SVC_POWER);
	if (err == PM_SUCCESS) {
		/*
		 * Add any new properties from the kernel and decorate existing
		 * properties with current values.
		 */
		err = pm_kernel_listprop(result);
	}

	return (err);
}


pm_error_t
pm_update()
{
	pm_error_t	err;
	nvlist_t	*proplist;

	/* Retrieve all of the SMF properties for possible sync */
	errno = 0;
	proplist = NULL;
	err = pm_smf_listprop(&proplist, PM_SVC_POWER);
	if (err != PM_SUCCESS) {
		/*
		 * An error occurred reading from SMF. Pass the error up
		 * to the caller to process.
		 */
		return (err);
	}

	/* Attempt to write the SMF properties to the kernel */
	errno = 0;
	err = pm_kernel_update(proplist);

	/* Clean up */
	nvlist_free(proplist);

	/*
	 * Return an error that indicates that the proper exit code is
	 * stored in the error data so that the caller, assumed to be
	 * executed by SMF, can return the proper exit code.
	 */
	return (err);
}


pm_authority_t
pm_authority_get(const char *authname)
{
	int		i;
	int		nauths;

	if (authname == NULL) {
		/* No authority name was given. This is invalid */
		return (PM_AUTHORITY_INVALID);
	}

	/* Scan the list of authorities for a matching entry */
	nauths = sizeof (pm_authorities) / sizeof (struct pm_authorities_s);
	for (i = 0; i < nauths; i++) {
		const char	*np;
		size_t		nlen;

		np = pm_authorities[i].a_name;
		if (np == NULL) {
			continue;
		}
		nlen = strlen(np);
		if (authname != NULL && strncmp(authname, np, nlen) == 0) {
			/* Match. Return the current authority */
			return (pm_authorities[i].a_auth);
		}
	}

	return (PM_AUTHORITY_INVALID);
}


const char *
pm_authority_getname(pm_authority_t authority)
{
	int		i;
	int		nauths;
	const char	*result = NULL;

	/* Scan the list of all authorities for the given value */
	nauths = sizeof (pm_authorities) / sizeof (struct pm_authorities_s);
	for (i = 0; i < nauths && result == NULL &&
	    pm_authorities[i].a_auth != PM_AUTHORITY_INVALID; i++) {
		if (authority == pm_authorities[i].a_auth) {
			/* We have a match.  Return the authority value. */
			result = pm_authorities[i].a_name;
			break;
		}
	}

	return (result);
}


pm_authority_t
pm_authority_next(pm_authority_t start)
{
	int		i;
	int		nauths;
	pm_authority_t	result;

	/* Use PM_AUTHORITY_INVALID to start a scan of authorities */
	if (start == PM_AUTHORITY_INVALID) {
		result = pm_authorities[0].a_auth;
		return (result);
	}

	i = 0;
	result = PM_AUTHORITY_INVALID;
	nauths = sizeof (pm_authorities) / sizeof (struct pm_authorities_s);
	while (i < nauths) {
		if (start == pm_authorities[i].a_auth) {
			i++;
			break;
		}

		/* Check the next authority */
		i++;
	}
	if (i < nauths) {
		/* Get the authority to return */
		result = pm_authorities[i].a_auth;
	}

	/*
	 * Return the next authority. If the result is PM_AUTHORITY_INVALID,
	 * the end of the list has been encountered.
	 */
	return (result);
}
