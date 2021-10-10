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
 * Utility functions used by all of libpower
 */

#include <sys/types.h>
#include <sys/pm.h>
#include <stdlib.h>
#include <strings.h>
#include <ctype.h>
#include <libnvpair.h>
#include <libpower.h>
#include <libpower_impl.h>


static pm_error_t pm_result_update(nvlist_t *, pm_authority_t, data_type_t,
    void *, uint_t);
static pm_error_t pm_result_create(nvlist_t *, pm_authority_t, const char *,
    const char *, data_type_t, void *, uint_t);
static int match_pgname(nvlist_t *, const char *);
static pm_error_t pm_result_merge(nvlist_t **, nvlist_t *, const char *,
    pm_authority_t);


/*
 * Add a property and value to a result set to be returned to a caller of
 * libpower.  The result list has the structure:
 *
 *	Name			Type	Description
 *	-------------------	------	-------------------------------------
 *	property_name		nvlist	The list of available values and
 *					other information about the
 *					property. See below for the structure
 *					of this list.
 *
 *	Name			Type	Description
 *	-------------------	------	-------------------------------------
 *	PM_PROP_PGNAME		string	The name of the SMF property
 *					group to which the property belongs
 *					other information about the
 *					property.
 *
 *	PM_AUTHORITY_SMF_STR	varies	The value of the property from SMF.
 *					Optional.
 *
 *	PM_AUTHORITY_CURRENT_STR
 *				varies	The value of the property in use by
 *					the kernel.
 *					Optional.
 *
 *	PM_AUTHORITY_PLATFORM_STR
 *				varies	The value of the property contained
 *					in the platform.
 *					Optional.
 */
pm_error_t
pm_result_add(nvlist_t **result, pm_authority_t authority, const char *pgname,
    const char *propname, data_type_t proptype, void *propv, uint_t propc)
{
	pm_error_t	err;
	nvlist_t	*rp;
	nvlist_t	*nvl;

	errno = 0;
	if (result == NULL || propname == NULL || propv == NULL) {
		errno = EINVAL;
		return (PM_ERROR_INVALID_ARGUMENT);
	}

	/* Allocate a new result list if necessary */
	rp = *result;
	if (rp == NULL) {
		errno = nvlist_alloc(result, NV_UNIQUE_NAME, 0);
		if (errno != 0) {
			return (PM_ERROR_NVLIST);
		}
		rp = *result;
	}

	/* Get the list of values for this property from the result set */
	err = PM_SUCCESS;
	nvl = NULL;
	errno = nvlist_lookup_nvlist(rp, propname, &nvl);
	if (errno != 0 || nvl == NULL) {
		/*
		 * This property does not exist in the result set. Add it
		 * to the result
		 */
		err = pm_result_create(rp, authority, pgname, propname,
		    proptype, propv, propc);
	} else {
		/*
		 * The property exists. Update it with the data from the new
		 * authority.
		 */
		err = pm_result_update(nvl, authority, proptype, propv, propc);
		if (err == PM_ERROR_MISSING_PROPERTY_VALUE &&
		    authority == PM_AUTHORITY_SMF) {
			uu_dprintf(pm_log, UU_DPRINTF_NOTICE,
			    "%s property %s: %s\n", __FUNCTION__,
			    propname, pm_strerror(err));

			/*
			 * Some properties in SMF may have no value.  This is
			 * valid, but the property is intentionally not added
			 * to the result set.
			 */
			errno = 0;
			err = PM_SUCCESS;
		}
	}

	return (err);
}


/*
 * Create a new property entry in a result set as specified by pm_result_add
 */
static pm_error_t
pm_result_create(nvlist_t *nvl, pm_authority_t authority, const char *pgname,
    const char *propname, data_type_t proptype, void *propv, uint_t propc)
{
	pm_error_t	err;
	nvlist_t	*plist;

	/* Create the new list and add required elements */
	errno = nvlist_alloc(&plist, NV_UNIQUE_NAME, 0);
	if (errno != 0) {
		return (PM_ERROR_NVLIST);
	}

	if (pgname != NULL) {
		errno = nvlist_add_string(plist, PM_PROP_PGNAME, pgname);
		if (errno != 0) {
			nvlist_free(plist);
			return (PM_ERROR_NVLIST);
		}
	}

	/* Add the new value to the list */
	err = pm_result_update(plist, authority, proptype, propv, propc);
	if (err == PM_SUCCESS) {
		/* Add the property to the main list */
		errno = nvlist_add_nvlist(nvl, propname, plist);
		if (errno != 0) {
			nvlist_free(nvl);
			err = PM_ERROR_NVLIST;
		}
	}
	if (plist != NULL) {
		nvlist_free(plist);
	}

	return (err);
}


/*
 * Add a new value to a property value list
 */
static pm_error_t
pm_result_update(nvlist_t *nvl, pm_authority_t authority, data_type_t proptype,
    void *propv, uint_t propc)
{
	pm_error_t	err;
	boolean_t	*bap;
	uint64_t	*uap;
	char		**sap;
	const char	*authname;

	if (propv == NULL || propc == 0) {
		errno = EINVAL;
		return (PM_ERROR_MISSING_PROPERTY_VALUE);
	}

	authname = pm_authority_getname(authority);
	if (authority == PM_AUTHORITY_INVALID) {
		errno = EINVAL;
		return (PM_ERROR_INVALID_AUTHORITY);
	}

	/*
	 * We assume that a property cannot exist in multiple property
	 * groups and simply add the value from the authority.
	 */
	errno = 0;
	err = PM_ERROR_NVLIST;
	switch (proptype) {
	case DATA_TYPE_BOOLEAN_ARRAY:
		bap = (boolean_t *)propv;
		errno = nvlist_add_boolean_array(nvl, authname, bap, propc);
		break;

	case DATA_TYPE_BOOLEAN_VALUE:
		bap = (boolean_t *)propv;
		errno = nvlist_add_boolean_value(nvl, authname, bap[0]);
		break;

	case DATA_TYPE_UINT64_ARRAY:
		uap = (uint64_t *)propv;
		errno = nvlist_add_uint64_array(nvl, authname, uap, propc);
		break;

	case DATA_TYPE_UINT64:
		uap = (uint64_t *)propv;
		errno = nvlist_add_uint64(nvl, authname, uap[0]);
		break;

	case DATA_TYPE_STRING_ARRAY:
		sap = (char **)propv;
		errno = nvlist_add_string_array(nvl, authname, sap, propc);
		break;

	case DATA_TYPE_STRING:
		sap = (char **)propv;
		errno = nvlist_add_string(nvl, authname, sap[0]);
		break;

	default:
		errno = EINVAL;
		err = PM_ERROR_INVALID_TYPE;
		break;
	}
	if (errno == 0) {
		err = PM_SUCCESS;
	}

	return (err);
}


/*
 * Given a result set built by pm_result_add, above, created a new result set
 * with just the requested properties.
 */
pm_error_t
pm_result_filter(nvlist_t **result, nvlist_t *nvl, pm_authority_t authority,
    char **propv, uint_t propc)
{
	pm_error_t	err;
	int		i;
	char		*propname;
	char		*pgname;
	char		tok[BUFSIZ];
	nvlist_t	*el;
	nvpair_t	*nvp;

	if (result == NULL || nvl == NULL) {
		uu_dprintf(pm_log, UU_DPRINTF_DEBUG, "%s invalid argument\n",
		    __FUNCTION__);
		errno = EINVAL;
		return (PM_ERROR_INVALID_ARGUMENT);
	}

	/*
	 * Scan each requested property and find the matching entry in the
	 * result set.
	 */
	err = PM_SUCCESS;
	for (i = 0; i < propc && err == PM_SUCCESS; i++) {
		if (propv[i] == NULL)  {
			/* This property entry is invalid. */
			uu_dprintf(pm_log, UU_DPRINTF_FATAL,
			    "%s null property at index %d\n", __FUNCTION__, i);
			errno = EINVAL;
			err = PM_ERROR_INVALID_PROPERTY_NAME;
			continue;
		}

		/*
		 * Get the property name and potential group name from the
		 * current token
		 */
		(void) strncpy(tok, propv[i], sizeof (tok));
		propname = pm_parse_propname(tok, &pgname);

		/*
		 * Get the unique property name from the list. There can be
		 * only one.
		 */
		errno = nvlist_lookup_nvlist(nvl, propname, &el);
		if (errno != 0) {
			/* The requested property is not found. Error. */
			uu_dprintf(pm_log, UU_DPRINTF_FATAL,
			    "%s property %s not found in result set\n",
			    __FUNCTION__, propname);
			errno = ENOENT;
			err = PM_ERROR_INVALID_PROPERTY_NAME;
			break;
		}

		/* Now match the property group name */
		if (! match_pgname(el, pgname)) {
			/*
			 * This is the only property with this name and the
			 * group does not match.
			 */
			uu_dprintf(pm_log, UU_DPRINTF_NOTICE,
			    "%s property %s/%s not found in result set\n",
			    __FUNCTION__, pgname, propname);
			errno = ENOENT;
			err = PM_ERROR_PROPERTY_NOT_FOUND;
			break;
		}

		/*
		 * Does this property contain an nvpair with the requested
		 * authority?
		 */
		errno = nvlist_lookup_nvpair(el,
		    pm_authority_getname(authority), &nvp);
		if (errno != 0) {
			if (authority == PM_AUTHORITY_ALL) {
				/*
				 * This is the special match-everything
				 * authority. Declare success and add the value
				 */
				errno = 0;
			} else {
				/*
				 * The specified authority does not contain the
				 * requested property name.
				 */
				uu_dprintf(pm_log, UU_DPRINTF_FATAL,
				    "%s authority %s not found in value list "
				    "for property %s\n", __FUNCTION__,
				    pm_authority_getname(authority), propname);
				errno = ENOENT;
				err = PM_ERROR_PROPERTY_NOT_FOUND;
				break;
			}
		}

		/*
		 * Merge the requested nvpair into the results and remove
		 * all the the requested authorities.
		 */
		err = pm_result_merge(result, nvl, propname, authority);
	}
	uu_dprintf(pm_log, UU_DPRINTF_DEBUG, "%s exiting with %d (%s)\n",
	    __FUNCTION__, err, pm_strerror(err));

	return (err);
}


/*
 * Add a specified nvpair to an existing result set
 */
static pm_error_t
pm_result_merge(nvlist_t **result, nvlist_t *nvl, const char *propname,
    pm_authority_t authority)
{
	pm_error_t	err;
	pm_authority_t	auth;
	nvpair_t	*nvp;
	nvlist_t	*vallist;

	err = PM_SUCCESS;
	if (result == NULL || nvl == NULL) {
		uu_dprintf(pm_log, UU_DPRINTF_DEBUG, "%s invalid argument\n",
		    __FUNCTION__);
		errno = EINVAL;
		return (PM_ERROR_INVALID_ARGUMENT);
	}
	if (*result == NULL) {
		errno = nvlist_alloc(result, NV_UNIQUE_NAME, 0);
		if (errno != 0) {
			return (PM_ERROR_NVLIST);
		}
	}

	/* Add the property to the result set and retrieve it's value list */
	if ((errno = nvlist_lookup_nvpair(nvl, propname, &nvp)) != 0 ||
	    (errno = nvlist_add_nvpair(*result, nvp)) != 0 ||
	    (errno = nvpair_value_nvlist(nvp, &vallist)) != 0) {
		uu_dprintf(pm_log, UU_DPRINTF_FATAL,
		    "%s property %s could not be added\n", __FUNCTION__,
		    propname);
		uu_dprintf(pm_log, UU_DPRINTF_DEBUG,
		    "%s nvlist returned %d (%s)\n", __FUNCTION__, errno,
		    strerror(errno));
		return (PM_ERROR_NVLIST);
	}

	/*
	 * Remove any authorities that do not match the requested authority
	 * from the result set.
	 */
	err = PM_SUCCESS;
	for (auth = pm_authority_next(PM_AUTHORITY_INVALID);
	    auth != PM_AUTHORITY_INVALID && err == PM_SUCCESS;
	    auth = pm_authority_next(auth)) {
		uu_dprintf(pm_log, UU_DPRINTF_DEBUG, "%s checking auth %s\n",
		    __FUNCTION__, pm_authority_getname(auth));

		if (authority == PM_AUTHORITY_ALL) {
			/* The request is for all available authorities */
			continue;
		}

		if (authority == auth) {
			/* The request is for this authority. Do not remove */
			continue;
		}

		/*
		 * Remove this authority from the list of authorities if
		 * it exists.
		 */
		if ((errno = nvlist_remove_all(vallist,
		    pm_authority_getname(auth))) != 0 && errno != ENOENT) {
			uu_dprintf(pm_log, UU_DPRINTF_DEBUG,
			    "%s nvlist_remove_all returned %d for %s\n",
			    __FUNCTION__, errno, pm_authority_getname(auth));
			err = PM_ERROR_NVLIST;
		}
	}

	return (err);
}


/*
 * Property name specifiers may also include a property group designation:
 *	property_group_name/property_name
 *
 * Given a buffer with a property name specifier, determine the location of
 * the property name and the property group name.  Modify the given buffer
 * if necessary.
 */
char *
pm_parse_propname(char *buf, char **pgname)
{
	char	*propname;
	char	*sep;

	if (buf == NULL) {
		errno = EINVAL;
		return (NULL);
	}

	/* Find the potential separator in the buffer */
	*pgname = NULL;
	propname = NULL;
	sep = strstr(buf, PM_SEP_STR);
	if (sep == NULL) {
		/* The buffer contains only a potential property name */
		propname = buf;
	} else {
		/*
		 * The buffer contains both a potential property name and a
		 * potential property group name.  Modify the buffer and
		 * returned both.
		 */
		*sep = '\0';
		propname = sep + 1;
		*pgname = buf;
		if (strlen(*pgname) == 0) {
			uu_dprintf(pm_log, UU_DPRINTF_WARNING,
			    "%s property group name has zero length\n",
			    __FUNCTION__);
			*pgname = NULL;
		}
	}

	return (propname);
}

/*
 * Determine if the string in the given buffer is a valid boolean value
 */
pm_error_t
pm_parse_boolean(const char *buf, boolean_t *result)
{
	pm_error_t	err;

	if (buf == NULL) {
		errno = EINVAL;
		return (PM_ERROR_INVALID_BOOLEAN);
	}

	/*
	 * Compare the given buffer with the valid values for a boolean value.
	 */
	err = PM_ERROR_INVALID_BOOLEAN;
	if (strncasecmp(buf, PM_TRUE_STR, strlen(PM_TRUE_STR)) == 0) {
		/* The value is valid and true. */
		*result = B_TRUE;
		err = PM_SUCCESS;
	} else if (strncasecmp(buf, PM_FALSE_STR, strlen(PM_FALSE_STR)) == 0) {
		/* The value is valid and false. */
		*result = B_FALSE;
		err = PM_SUCCESS;
	}

	return (err);
}


/*
 * Determine if the string in the given buffer contains a valid integer value
 */
pm_error_t
pm_parse_integer(const char *buf, int64_t *result)
{
	const char *cp;

	if (buf == NULL) {
		return (PM_ERROR_INVALID_INTEGER);
	}

	/* Make sure that all the characters are numbers */
	for (cp = buf; cp != NULL && *cp != '\0'; cp++) {
		if (! isdigit(*cp)) {
			uu_dprintf(pm_log, UU_DPRINTF_DEBUG,
			    "string \"%s\" is not a valid integer\n", buf);
			errno = EINVAL;
			return (PM_ERROR_INVALID_INTEGER);
		}
	}

	/* Convert the integer to an int64 value */
	if (sscanf(buf, "%lld", result) != 1) {
		uu_dprintf(pm_log, UU_DPRINTF_FATAL,
		    "integer \"%s\" could not be converted\n", buf);
		errno = EINVAL;
		return (PM_ERROR_INVALID_INTEGER);
	}

	return (PM_SUCCESS);
}


/*
 * Determine if the given property group name matches a given property value
 * list by comparing the strings.  An input property group name of null
 * matches all values.
 */
static int
match_pgname(nvlist_t *nvl, const char *pgname)
{
	char		*sp;

	if (pgname == NULL) {
		/* Match. The request wants any possible property group */
		return (1);
	}

	sp = NULL;
	if (nvlist_lookup_string(nvl, PM_PROP_PGNAME, &sp) != 0) {
		/*
		 * No match. The property group name for this property list
		 * does not exist and therefore does not match.
		 */
		uu_dprintf(pm_log, UU_DPRINTF_DEBUG,
		    "%s property group %s not found\n", __FUNCTION__, pgname);

		return (0);
	}
	if (strncmp(pgname, sp, strlen(sp)) != 0) {
		/*
		 * No match. The property group name for this property list
		 * does not match the requested property group name.
		 */
		uu_dprintf(pm_log, UU_DPRINTF_DEBUG,
		    "%s requested property group %s != %s\n", __FUNCTION__,
		    pgname, sp);

		return (0);
	}

	/* Match. */
	return (1);
}
