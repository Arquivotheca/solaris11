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
 * Determine whether this SPARC machine supports suspend and should have
 * suspend-enable activated.
 */

#include <sys/types.h>
#include <libpower.h>
#include <libpower_impl.h>

/*
 * Determine if the current machine supports suspend.
 */
boolean_t
pm_get_suspendenable(void)
{

	/*
	 * Suspend is disabled by default for all SPARC machines.
	 * MOU3 is not used because sun4u machines have been deprecated
	 * and no newer technique is available.
	 */
	return (B_FALSE);
}
