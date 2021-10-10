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
 * Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/brand.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <sys/zone.h>

/*
 * brand(2) system call.
 */
int64_t
brandsys(int cmd, uintptr_t arg1, uintptr_t arg2, uintptr_t arg3,
    uintptr_t arg4, uintptr_t arg5, uintptr_t arg6)
{
	struct proc *p = curthread->t_procp;
	int64_t rval = 0;
	int err;

	/*
	 * The brandsys system call can only be executed by a process within
	 * a branded zone.  Note that the process itself may or may not
	 * have be branded-- it could be a natively "escaped" process inside
	 * of the branded zone.  It's up to the brand to decide whether
	 * that's OK, or not, typically on a per-subcode basis.
	 */
	if (INGLOBALZONE(p) || !ZONE_HAS_BRANDOPS(p->p_zone))
		return (set_errno(ENOSYS));

	if ((err = ZBROP(p->p_zone)->b_brandsys(cmd, &rval, arg1, arg2, arg3,
	    arg4, arg5, arg6)) != 0)
		return (set_errno(err));

	return (rval);
}
