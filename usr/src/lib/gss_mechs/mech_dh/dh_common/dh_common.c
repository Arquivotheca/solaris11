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
 * Copyright (c) 1997, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <stdlib.h>
#include <stdio.h>
#include <dlfcn.h>

#include "dh_gssapi.h"
#include "dh_common.h"

/*
 * __dh_generic_initialize: This routine is called from the mechanism
 * specific gss_mech_initialize routine, which in turn is called from
 * libgss to initialize a mechanism. This routine takes a pointer to
 * a struct gss_config, the OID for the calling mechanism and that mechanisms
 * keyopts. It returns the same gss_mechanism back, but with all fields
 * correctly initialized. This routine in turn opens the common wire
 * protocol moduel mech_dh.so.1 to fill in the common parts of the
 * gss_mechanism. It then associatates the OID and the keyopts with this
 * gss_mechanism. If there is any failure NULL is return instead.
 */
gss_mechanism
__dh_generic_initialize(gss_mechanism dhmech, /* The mechanism to initialize */
			gss_OID_desc mech_type, /* OID of mechanism */
			dh_keyopts_t keyopts /* Key mechanism entry points  */)
{
	gss_mechanism (*mech_init)(gss_mechanism mech);
	gss_mechanism mech;
	void *dlhandle;

	/* Initialize the common parts of the gss_mechanism */
	if ((mech = __dh_gss_initialize(dhmech)) == NULL) {
		return (NULL);
	}

	/* Set the mechanism OID */
	mech->mech_type = mech_type;

	return (mech);
}
