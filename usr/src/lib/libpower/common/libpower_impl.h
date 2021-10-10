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

#ifndef _LIBPOWER_IMPL_H
#define	_LIBPOWER_IMPL_H

/*
 * Internal definitions and data structures to implement libpower
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <sys/pm.h>
#include <errno.h>
#include <libuutil.h>
#include <libnvpair.h>

#define	PM_DEV_PATH		"/dev/pm"
#define	PM_LOG_DOMAIN		"libpower"
#define	PM_PROP_VALUE_AUTH	"value_authorization"
#define	PM_ENV_DEBUG		"LIBPOWER_DEBUG"

struct pm_authorities_s {
	pm_authority_t	a_auth;
	const char	*a_name;
};

extern	uu_dprintf_t		*pm_log;

char		*pm_parse_propname(char *, char **);
pm_authority_t	 pm_authority_next(pm_authority_t);
boolean_t	 pm_get_suspendenable(void);
pm_error_t	 pm_kernel_listprop(nvlist_t **);
pm_error_t	 pm_kernel_update(nvlist_t *);
pm_error_t	 pm_parse_boolean(const char *, boolean_t *);
pm_error_t	 pm_parse_integer(const char *, int64_t *);
pm_error_t	 pm_result_add(nvlist_t **, pm_authority_t, const char *,
    const char *, data_type_t, void *, uint_t);
pm_error_t	 pm_result_filter(nvlist_t **, nvlist_t *, pm_authority_t,
    char **propv, uint_t propc);
pm_error_t	 pm_smf_add_pgname(nvlist_t *, const char *);
pm_error_t	 pm_smf_listprop(nvlist_t **, const char *);
pm_error_t	 pm_smf_setprop(nvpair_t *, const char *);

#ifdef __cplusplus
}
#endif

#endif /* _LIBPOWER_IMPL_H */
