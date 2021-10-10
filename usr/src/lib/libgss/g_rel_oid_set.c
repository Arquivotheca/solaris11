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
 * Copyright (c) 1996, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 *  glue routine for gss_release_oid_set
 */

#include <mglueP.h>
#include <stdio.h>
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

OM_uint32
gss_release_oid_set(minor_status, set)

OM_uint32 *			minor_status;
gss_OID_set *			set;
{
	OM_uint32 index;
	gss_OID oid;
	if (minor_status)
		*minor_status = 0;

	if (set == NULL)
		return (GSS_S_COMPLETE);

	if (*set == GSS_C_NULL_OID_SET)
		return (GSS_S_COMPLETE);

	for (index = 0; index < (*set)->count; index++) {
		oid = &(*set)->elements[index];
		free(oid->elements);
	}
	free((*set)->elements);
	free(*set);

	*set = GSS_C_NULL_OID_SET;

	return (GSS_S_COMPLETE);
}
