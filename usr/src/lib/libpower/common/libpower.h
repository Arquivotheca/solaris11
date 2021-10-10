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

#ifndef _LIBPOWER_H
#define	_LIBPOWER_H

/*
 * Power Management Administration Interfaces
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <errno.h>
#include <sys/pm.h>
#include <libnvpair.h>
#include <libuutil.h>

#define	PM_PROP_QUERY		"pm_property_query"
#define	PM_PROP_PGNAME		"property-group"
#define	PM_TRUE_STR		"true"
#define	PM_FALSE_STR		"false"

enum pm_error_e {
	PM_SUCCESS = 0,
	PM_ERROR_USAGE,
	PM_ERROR_INVALID_ARGUMENT,
	PM_ERROR_INVALID_AUTHORITY,
	PM_ERROR_INVALID_BOOLEAN,
	PM_ERROR_INVALID_INTEGER,
	PM_ERROR_INVALID_PROPERTY_NAME,
	PM_ERROR_INVALID_PROPERTY_VALUE,
	PM_ERROR_INVALID_TYPE,
	PM_ERROR_MISSING_PROPERTY_NAME,
	PM_ERROR_MISSING_PROPERTY_VALUE,
	PM_ERROR_NVLIST,
	PM_ERROR_PROPERTY_NOT_FOUND,
	PM_ERROR_PROPERTY_GROUP_NOT_FOUND,
	PM_ERROR_SCF,
	PM_ERROR_SYSTEM
};
typedef enum pm_error_e pm_error_t;

pm_error_t pm_getprop(nvlist_t **, nvlist_t *);
pm_error_t pm_init_suspend(void);
pm_error_t pm_listprop(nvlist_t **);
pm_error_t pm_setprop(nvlist_t *);
pm_error_t pm_update(void);
int update_cprconfig(uu_dprintf_severity_t);


pm_authority_t	 pm_authority_get(const char *);
const char	*pm_authority_getname(pm_authority_t);
const char	*pm_strerror(pm_error_t);

#ifdef __cplusplus
}
#endif

#endif /* _LIBPOWER_H */
