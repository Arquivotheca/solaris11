/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil -*- */
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

#include "k5-int.h"
#include "etypes.h"

/* Solaris kerberos:
 *
 * is_in_keytype(): returns 1 if enctype == one of the enctypes in keytype
 * otherwise 0 is returned.
 */
krb5_boolean KRB5_CALLCONV
is_in_keytype(
    krb5_const krb5_enctype *keytype,
    int numkeytypes,
    krb5_enctype enctype)
{
    int i;

    if (keytype == NULL || numkeytypes <= 0)
	return(0);

    for (i = 0; i < numkeytypes; i++) {
	if (keytype[i] == enctype)
	    return(1);
    }

    return(0);
}
