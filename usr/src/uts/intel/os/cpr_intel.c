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
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * cpr functions for supported sparc platforms
 */
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/cpr.h>
#include <sys/kmem.h>
#include <sys/errno.h>

/*
 * setup the original and new sets of property names/values
 * XXXX - Not currently used on x86, but has some potential value
 * (such as things that might be neccessary for GRUB).
 */
/*ARGSUSED*/
int
cpr_default_setup(int alloc)
{
	return (0);
}

/*
 * Set boot properties.
 * Currently unused on x64
 */
/*ARGSUSED*/
int
cpr_set_properties(int new)
{
	return (0);
}
