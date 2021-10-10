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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

	.file	"abs.s"

#include "SYS.h"

/*
 * int abs(int arg);
 */
	ENTRY(abs)
	cmp	%o0, 0
	bneg,a	%icc, 1f
	neg %o0
1:
	retl
	nop
	SET_SIZE(abs)

/*
 * long labs(long arg);
 */
	ENTRY(labs)
	brlz,a	%o0, 1f
	neg %o0
1:
	retl
	nop
	SET_SIZE(labs)

/*
 * long long llabs(long long arg);
 */
	ENTRY(llabs)
	brlz,a	%o0, 1f
	neg %o0
1:
	retl
	nop
	SET_SIZE(llabs)
