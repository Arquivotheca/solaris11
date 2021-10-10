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
 * Define and access error strings for the libpower library
 */

#include <sys/types.h>
#include <errno.h>
#include <string.h>
#include <libintl.h>
#include <locale.h>
#include <libscf.h>
#include <libpower.h>

static struct pm_error_info {
	pm_error_t	e_code;
	const char	*e_desc;
} pm_errors[] = {
	{PM_SUCCESS, "no error"},
	{PM_ERROR_USAGE, "invalid command line arguments"},
	{PM_ERROR_INVALID_ARGUMENT, "one or more arguments are invalid"},
	{PM_ERROR_INVALID_AUTHORITY, "the specified authority is invalid"},
	{PM_ERROR_INVALID_BOOLEAN,
	    "a string could not be converted to a valid boolean"},
	{PM_ERROR_INVALID_INTEGER,
	    "a string could not be converted to a valid integer"},
	{PM_ERROR_INVALID_PROPERTY_NAME,
	    "one or more property names are invalid"},
	{PM_ERROR_INVALID_PROPERTY_VALUE,
	    "one or more property values are invalid"},
	{PM_ERROR_INVALID_TYPE, "an invalid data type was encountered"},
	{PM_ERROR_MISSING_PROPERTY_NAME, "a property name is required"},
	{PM_ERROR_MISSING_PROPERTY_VALUE, "a property value is required"},
	{PM_ERROR_NVLIST, "an internal nvlist error occurred"},
	{PM_ERROR_PROPERTY_NOT_FOUND, "the requested property was not found"},
	{PM_ERROR_PROPERTY_GROUP_NOT_FOUND,
	    "a property group name was not found"},
	{PM_ERROR_SCF, "an SCF error occurred"},
	{PM_ERROR_SYSTEM, "an OS error occurred"},
};


const char *
pm_strerror(pm_error_t code)
{
	const char	*result = NULL;
	int		i;
	size_t		nel;

	/* Search the above array for the given pm_error_t */
	nel = sizeof (pm_errors) / sizeof (struct pm_error_info);
	i = 0;
	while (i < nel && pm_errors[i].e_code != code) {
		i++;
	}
	if (i < nel) {
		/* Determine what string to return based on the pm_error_t */
		switch (code) {
		case PM_ERROR_SCF:
			/*
			 * The error is in the SMF layer. Get the string from
			 * SMF. These are already localized by libscf.
			 */
			result = scf_strerror(scf_error());
			break;

		case PM_ERROR_NVLIST:
		case PM_ERROR_SYSTEM:
			/*
			 * The error is in the system layer. Get the string
			 * from the system. These are already localized by
			 * the system.
			 */
			result = strerror(errno);
			break;

		default:
			/*
			 * The error is in the libpower layer. Get the
			 * localized string.
			 */
			result = gettext(pm_errors[i].e_desc);
			break;
		}
	}

	return (result);
}
