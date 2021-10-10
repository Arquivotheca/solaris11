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
 * Copyright (c) 1992, 2011, Oracle and/or its affiliates. All rights reserved.
 */

	.file	"gettimeofday.s"

#include "SYS.h"

	ANSI_PRAGMA_WEAK(gettimeofday,function)

/
/  implements int gettimeofday(struct timeval *tp, void *tzp)
/
/	note that tzp is always ignored
/
/	gettimeofday is implemented as an alternate capability symbol
/	on x86 systems which support the rdtscp instruction.
/
	ENTRY(gettimeofday)
/
/	use long long gethrestime()
/
	SYSFASTTRAP(GETHRESTIME)
/
/	gethrestime trap returns seconds in %eax, nsecs in %edx
/	need to convert nsecs to usecs & store into area pointed
/	to by struct timeval * argument.
/
	movl 4(%esp), %ecx	/ put ptr to timeval in %ecx
	jecxz 1f		/ bail if we get a null pointer
	movl %eax, (%ecx)	/ store seconds into timeval ptr	
	movl $274877907, %eax	/ divide by 1000 as impl. by gcc
	imull %edx		/ See Hacker's Delight pg 162
	sarl $6, %edx		/ simplified by 0 <= nsec <= 1e9
	movl %edx, 4(%ecx)	/ store usecs into timeval ptr + 4.
1:
	RETC			/ return 0
	SET_SIZE(gettimeofday)
