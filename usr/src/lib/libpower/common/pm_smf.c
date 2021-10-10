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
 * Implement Operations for Active Power Management Properties
 */

#include <sys/types.h>
#include <stdio.h>
#include <sys/pm.h>
#include <errno.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <libnvpair.h>
#include <libscf.h>
#include <libscf_priv.h>
#include <libpower.h>
#include <libpower_impl.h>

typedef	pm_error_t (*pm_smf_validate_f)(scf_simple_prop_t *, const char *,
    void **);

struct pm_smf_validate_s {
	const char		*v_name;
	pm_smf_validate_f	v_func;
};
typedef struct pm_smf_validate_s pm_smf_validate_t;

static const char *exclude_props[] = {
	PM_PROP_VALUE_AUTH,
	NULL
};


static void *pm_smf_tonvpair(scf_simple_prop_t *, data_type_t *, uint_t *);
static pm_error_t pm_smf_write(const char *, const char *, scf_propvec_t *);

/* Property value validation routines for use by setprop */
static pm_error_t	pm_smf_validate(scf_simple_prop_t *, const char *,
    void **);
static pm_error_t	pm_smf_validate_authority(scf_simple_prop_t *,
    const char *, void **);
static pm_error_t	pm_smf_validate_boolean(scf_simple_prop_t *,
    const char *, void **);
static pm_error_t	pm_smf_validate_integer(scf_simple_prop_t *,
    const char *, void **);

static const pm_smf_validate_t validate_authority = {
    PM_PROP_AUTHORITY, pm_smf_validate_authority};
static const pm_smf_validate_t validate_suspend_enable = {
    PM_PROP_SUSPEND_ENABLE, pm_smf_validate_boolean};
static const pm_smf_validate_t validate_ttfc = {
    PM_PROP_TTFC, pm_smf_validate_integer};
static const pm_smf_validate_t validate_ttmr = {
    PM_PROP_TTMR, pm_smf_validate_integer};
static const pm_smf_validate_t *pm_smf_validators[] = {
	&validate_authority,
	&validate_suspend_enable,
	&validate_ttfc,
	&validate_ttmr,
	NULL
};


pm_error_t
pm_smf_listprop(nvlist_t **result, const char *instance)
{
	pm_error_t		err;
	uint_t			propc;
	const char		**pp;
	const char		*propname;
	const char		*pgname;
	scf_simple_prop_t	*prop;
	scf_simple_app_props_t	*props;
	data_type_t		proptype;
	void			*propval;

	props = scf_simple_app_props_get(NULL, instance);
	if (props == NULL) {
		/*
		 * An error occurred in the SMF layer. Return that information
		 * to the caller for processing.
		 */
		uu_dprintf(pm_log, UU_DPRINTF_DEBUG, "%s SCF error %d (%s)\n",
		    __FUNCTION__, scf_error(), scf_strerror(scf_error()));

		return (PM_ERROR_SCF);
	}

	err = PM_SUCCESS;
	prop = (scf_simple_prop_t *)scf_simple_app_props_next(props, NULL);
	while (err == PM_SUCCESS && prop != NULL) {
		propname = scf_simple_prop_name(prop);
		pgname = scf_simple_prop_pgname(prop);
		propval = pm_smf_tonvpair(prop, &proptype, &propc);

		/*
		 * Some properties that are returned by the libscf simple
		 * interface are actually internal to the SMF implementation.
		 * To avoid the user changing their values here, where it is
		 * inappropriate, those properties are specifically excluded
		 * from the result set.
		 */
		for (pp = exclude_props; *pp != NULL; pp++) {
			if (strncmp(propname, *pp, strlen(*pp)) == 0) {
				break;
			}
		}
		if (*pp == NULL) {
			/* Add the value, if any, to the result list */
			err = pm_result_add(result, PM_AUTHORITY_SMF,
			    pgname, propname, proptype, propval, propc);
			if (err == PM_ERROR_INVALID_ARGUMENT) {
				uu_dprintf(pm_log, UU_DPRINTF_DEBUG,
				    "%s property %s has no value. Skipping\n",
				    __FUNCTION__, propname);

				/* Not having a value is OK */
				errno = 0;
				err = PM_SUCCESS;
			}
		} else {
			uu_dprintf(pm_log, UU_DPRINTF_DEBUG,
			    "%s property %s is in the exclude list\n",
			    __FUNCTION__, propname);
		}
		if (propval != NULL) {
			switch (proptype) {
			case DATA_TYPE_STRING:
			case DATA_TYPE_STRING_ARRAY: {
				int	i;
				char	**sap = (char **)propval;
				for (i = 0; i < propc; i++) {
					if (sap[i] != NULL) {
						free(sap[i]);
					}
				}
				free(propval);
			}
				break;

			case DATA_TYPE_BOOLEAN:
			case DATA_TYPE_BOOLEAN_ARRAY:
			case DATA_TYPE_UINT64:
			case DATA_TYPE_UINT64_ARRAY:
			default:
				free(propval);
				break;

			}
		}

		/* Check the next property */
		prop = (scf_simple_prop_t *)scf_simple_app_props_next(props,
		    prop);
	}
	scf_simple_app_props_free(props);

	return (err);
}


pm_error_t
pm_smf_setprop(nvpair_t *nvp, const char *instance)
{
	pm_error_t	err;
	char		*val;
	char		*pgname;
	char		*propname;

	scf_simple_prop_t	*prop;
	scf_simple_prop_t	*el;
	scf_simple_app_props_t	*props;
	scf_propvec_t		propvec[2];

	errno = 0;

	/* Retrieve all of the SMF properties for property information */
	props = scf_simple_app_props_get(NULL, instance);
	if (props == NULL) {
		/*
		 * An error occurred in the SMF layer. Return that information
		 * to the caller for processing.
		 */
		uu_dprintf(pm_log, UU_DPRINTF_DEBUG, "%s SCF error %d (%s)\n",
		    __FUNCTION__, scf_error(), scf_strerror(scf_error()));

		return (PM_ERROR_SCF);
	}

	/* Extract the property name from the incoming nvpair */
	prop = NULL;
	val = strdup(nvpair_name(nvp));
	propname = pm_parse_propname(val, &pgname);

	/*
	 * Find the SMF property that matches this property name. Note that
	 * the power service uses multiple property groups, but the property
	 * namespace is unique (otherwise this would not be valid)
	 */
	el = (scf_simple_prop_t *)scf_simple_app_props_next(props, NULL);
	while (el != NULL && prop == NULL) {
		char	*np;
		size_t	nlen;

		np = scf_simple_prop_name(el);
		nlen = strlen(np);

		if (strncmp(propname, np, nlen) == 0) {
			prop = el;
		}

		/* Check the next property */
		el = (scf_simple_prop_t *)scf_simple_app_props_next(props, el);
	}
	if (prop == NULL) {
		errno = ENOENT;
		uu_dprintf(pm_log, UU_DPRINTF_DEBUG,
		    "%s could not find SMF property %s instance %s\n",
		    __FUNCTION__, propname, instance);
		free(val);

		return (PM_ERROR_PROPERTY_NOT_FOUND);
	}
	free(val);

	/* The new value comes in as a string value */
	if (nvpair_value_string(nvp, &val) != 0) {
		errno = EINVAL;
		return (PM_ERROR_MISSING_PROPERTY_VALUE);
	}

	/*
	 * We now have the property from SCF, which is authoritative.
	 * Initialize the property vector using this information
	 */
	bzero(&propvec, sizeof (propvec));
	propvec[1].pv_prop = NULL;
	propvec[0].pv_prop = scf_simple_prop_name(prop);
	propvec[0].pv_type = scf_simple_prop_type(prop);
	propvec[0].pv_mval = B_FALSE;
	err = pm_smf_validate(prop, val, &(propvec[0].pv_ptr));
	if (err != PM_SUCCESS) {
		return (err);
	}

	/* Write the property vector to SMF */
	err = pm_smf_write(instance, scf_simple_prop_pgname(prop), propvec);

	/* Clean up allocated memory and return to the caller */
	if (propvec[0].pv_ptr != NULL) {
		/* The value was allocated by the property validator */
		free(propvec[0].pv_ptr);
	}

	return (err);
}


pm_error_t
pm_smf_write(const char *instance, const char *pgname, scf_propvec_t *propvec)
{
	int		err;
	scf_propvec_t	*errprop = NULL;

	/* Write the property vector to the SMF database */
	err = scf_write_propvec(instance, pgname, propvec, &errprop);
	if (err == SCF_FAILED) {
		uu_dprintf(pm_log, UU_DPRINTF_DEBUG,
		    "%s scf_write_propvec prop %s failed %d (%s)\n",
		    __FUNCTION__, propvec[0].pv_prop, scf_error(),
		    scf_strerror(scf_error()));
		return (PM_ERROR_SCF);
	}

	err = smf_refresh_instance(instance);
	if (err != 0) {
		uu_dprintf(pm_log, UU_DPRINTF_DEBUG,
		    "%s scf_refresh_instance failed %d (%s)\n",
		    __FUNCTION__, scf_error(), scf_strerror(scf_error()));
		return (PM_ERROR_SCF);
	}

	return (err);
}


static void *
pm_smf_tonvpair(scf_simple_prop_t *prop, data_type_t *valtype, uint_t *propc)
{
	int		i;
	size_t		nval;
	boolean_t	*bap;
	char		**sap;
	uint64_t	*uap;
	void		*result;
	char		*propname;
	scf_type_t	proptype;

	nval = *propc = scf_simple_prop_numvalues(prop);
	propname = scf_simple_prop_name(prop);
	if (nval == 0) {
		uu_dprintf(pm_log, UU_DPRINTF_DEBUG,
		    "%s property %s has no values\n", __FUNCTION__,
		    propname);

		return (NULL);
	}

	result = NULL;
	proptype = scf_simple_prop_type(prop);
	uu_dprintf(pm_log, UU_DPRINTF_DEBUG,
	    "%s property %s type %d number of values %d\n",
	    __FUNCTION__, propname, proptype, nval);
	switch (proptype) {
	case SCF_TYPE_BOOLEAN:
		*valtype = (nval == 1 ? DATA_TYPE_BOOLEAN_VALUE :
		    DATA_TYPE_BOOLEAN_ARRAY);
		bap = (boolean_t *)calloc(nval, sizeof (boolean_t));
		if (bap != NULL) {
			for (i = 0; i < nval; i++) {
				uint8_t	*b;
				b = scf_simple_prop_next_boolean(prop);
				bap[i] = (*b != 0 ? B_TRUE : B_FALSE);
			}
			result = (void *)bap;
		}
		break;

	case SCF_TYPE_INTEGER:
		*valtype = (nval == 1 ? DATA_TYPE_UINT64 :
		    DATA_TYPE_UINT64_ARRAY);
		uap = (uint64_t *)calloc(nval, sizeof (uint64_t));
		if (uap != NULL) {
			for (i = 0; i < nval; i++) {
				int64_t *val =
				    scf_simple_prop_next_integer(prop);
				uap[i] = *val;
			}
			result = (void *)uap;
		}
		break;

	case SCF_TYPE_ASTRING:
		*valtype = (*propc == 1 ? DATA_TYPE_STRING :
		    DATA_TYPE_STRING_ARRAY);
		sap = (char **)calloc(nval, sizeof (char *));
		if (sap != NULL) {
			for (i = 0; i < nval; i++) {
				char *s;
				s = strdup(scf_simple_prop_next_astring(prop));
				sap[i] = s;
			}
			result = (void *)sap;
		}
		break;

	default:
		*valtype = DATA_TYPE_UNKNOWN;
		errno = ENOENT;
		return (NULL);
		break;
	}

	return (result);
}


pm_error_t
pm_smf_add_pgname(nvlist_t *result, const char *instance)
{
	pm_error_t		err;
	char			*propname;
	char			*pgname;
	nvpair_t		*nvp;
	nvlist_t		*nvl;
	scf_simple_prop_t	*prop;
	scf_simple_app_props_t	*props;

	/* Retrieve all of the properties from SMF */
	props = scf_simple_app_props_get(NULL, instance);
	if (props == NULL) {
		/*
		 * An error occurred in the SMF layer. Return that information
		 * to the caller for processing.
		 */
		uu_dprintf(pm_log, UU_DPRINTF_DEBUG, "%s SCF error %d (%s)\n",
		    __FUNCTION__, scf_error(), scf_strerror(scf_error()));

		return (PM_ERROR_SCF);
	}

	err = PM_SUCCESS;
	prop = (scf_simple_prop_t *)scf_simple_app_props_next(props, NULL);
	while (err == PM_SUCCESS && prop != NULL) {
		propname = scf_simple_prop_name(prop);
		pgname = scf_simple_prop_pgname(prop);

		/* Find the property in the result list */
		nvl = NULL;
		if (nvlist_lookup_nvlist(result, propname, &nvl) == 0 &&
		    nvl != NULL) {
			/*
			 * The property exists in the result list. Check
			 * for a property group name.
			 */
			nvp = NULL;
			if (nvlist_lookup_nvpair(nvl, PM_PROP_PGNAME, &nvp)) {
				/*
				 * Could not find the property group name for
				 * this property.  Add it now.
				 */
				uu_dprintf(pm_log, UU_DPRINTF_DEBUG,
				    "%s adding property group name for "
				    "property %s\n", __FUNCTION__, propname);

				errno = nvlist_add_string(nvl, PM_PROP_PGNAME,
				    pgname);
				if (errno != 0) {
					err = PM_ERROR_NVLIST;
				}
			}
		} else {
			uu_dprintf(pm_log, UU_DPRINTF_DEBUG,
			    "%s could not find property %s in result set\n",
			    __FUNCTION__, propname);
		}

		/* Check the next property */
		prop = (scf_simple_prop_t *)scf_simple_app_props_next(props,
		    prop);
	}
	scf_simple_app_props_free(props);

	return (err);
}


pm_error_t
pm_smf_validate(scf_simple_prop_t *prop, const char *val, void **result)
{
	int			err;
	size_t			len;
	char			*propname;
	const pm_smf_validate_t	*ep, **epp;

	propname = scf_simple_prop_name(prop);
	len = strlen(propname);
	for (epp = pm_smf_validators; *epp != NULL; epp++) {
		ep = *epp;
		if (strncmp(propname, ep->v_name, len) == 0)
			break;
	}
	if (*epp == NULL) {
		return (PM_ERROR_INVALID_PROPERTY_VALUE);
	}
	ep = *epp;

	/* Call the validation routine for this property */
	err = (ep->v_func)(prop, val, result);

	return (err);
}


pm_error_t
pm_smf_validate_authority(scf_simple_prop_t *prop, const char *val,
    void **result)
{
	pm_authority_t	authority;

	/* Validate that this property takes a string */
	if (scf_simple_prop_type(prop) != SCF_TYPE_ASTRING) {
		uu_dprintf(pm_log, UU_DPRINTF_DEBUG,
		    "%s property %s has unexpected type %d\n", __FUNCTION__,
		    scf_simple_prop_name(prop), scf_simple_prop_type(prop));
		return (PM_ERROR_INVALID_AUTHORITY);
	}

	/* Validate that the given string is a valid authority */
	authority = pm_authority_get(val);
	if (authority == PM_AUTHORITY_INVALID) {
		return (PM_ERROR_INVALID_AUTHORITY);
	}

	/* The authority is valid. Make a copy and return to the caller */
	*result = (void *)strdup(val);

	return (PM_SUCCESS);
}


pm_error_t
pm_smf_validate_boolean(scf_simple_prop_t *prop, const char *val,
    void **result)
{
	pm_error_t	err;
	boolean_t	b;
	boolean_t	*r;

	/* Validate that this property takes a boolean */
	if (scf_simple_prop_type(prop) != SCF_TYPE_BOOLEAN) {
		uu_dprintf(pm_log, UU_DPRINTF_DEBUG,
		    "%s property %s has unexpected type %d\n", __FUNCTION__,
		    scf_simple_prop_name(prop), scf_simple_prop_type(prop));

		return (PM_ERROR_INVALID_BOOLEAN);
	}

	/* Validate that the given string is a valid boolean */
	err = pm_parse_boolean(val, &b);
	if (err != PM_SUCCESS) {
		uu_dprintf(pm_log, UU_DPRINTF_DEBUG,
		    "%s invalid boolean \"%s\" for property %s\n",
		    __FUNCTION__, val, scf_simple_prop_name(prop));

		return (PM_ERROR_INVALID_BOOLEAN);
	}

	/* The boolean is valid. Make a copy and return to the caller */
	r = malloc(sizeof (boolean_t));
	if (r == NULL) {
		return (PM_ERROR_SYSTEM);
	}
	*r = b;
	*result = r;

	return (PM_SUCCESS);
}


pm_error_t
pm_smf_validate_integer(scf_simple_prop_t *prop, const char *val,
    void **result)
{
	pm_error_t	err;
	int64_t		i;
	int64_t		*r;

	/* Validate that this property takes an integer */
	if (scf_simple_prop_type(prop) != SCF_TYPE_INTEGER) {
		uu_dprintf(pm_log, UU_DPRINTF_DEBUG,
		    "%s property %s has unexpected type %d\n", __FUNCTION__,
		    scf_simple_prop_name(prop), scf_simple_prop_type(prop));

		return (PM_ERROR_INVALID_INTEGER);
	}

	/*
	 * Validate that the given string is a valid integer for the power
	 * service (they are all non-negative)
	 */
	err = pm_parse_integer(val, &i);
	if (err != PM_SUCCESS || i < 0) {
		uu_dprintf(pm_log, UU_DPRINTF_DEBUG,
		    "%s invalid integer \"%s\" for property %s\n",
		    __FUNCTION__, val, scf_simple_prop_name(prop));

		return (PM_ERROR_INVALID_INTEGER);
	}

	/* The boolean is valid. Make a copy and return to the caller */
	r = malloc(sizeof (int64_t));
	if (r == NULL) {
		return (PM_ERROR_SYSTEM);
	}
	*r = i;
	*result = r;

	return (PM_SUCCESS);
}
