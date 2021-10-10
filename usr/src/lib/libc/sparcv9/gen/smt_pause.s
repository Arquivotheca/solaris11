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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */

	.file	"smt_pause.s"

#include <sys/asm_linkage.h>
#include <../assym.h>

/*
 * void smt_pause(void)
 *
 * Do nothing efficiently.
 */

#ifdef lint
void
smt_pause(void)
{
}
#else
#define	BANKS	(4 * 64)
	ENTRY(smt_pause)
	save %sp, -SA(MINFRAME+BANKS), %sp
	ld      [%g7 + UL_LWPID], %i3   ! curthread->ul_lwpid
	add	%fp, STACK_BIAS-BANKS, %i0
	and     %i3, 0x3, %i2           ! save last 2 bits
	sllx    %i2, 0x6, %i1           ! pick a slot
	add     %i1, %i0, %o0
	casx    [%o0], %g0, %g0
	casx    [%o0], %g0, %g0
	ret
	restore
	SET_SIZE(smt_pause)
#endif
