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
 * Copyright (c) 2004, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _ASM_CPUVAR_H
#define	_ASM_CPUVAR_H

#include <sys/types.h>

#ifdef	__cplusplus
extern "C" {
#endif

#if !defined(__lint) && defined(__GNUC__)

struct cpu;

extern __inline__ struct cpu *curcpup(void)
{
	struct cpu *__value;

#if defined(__amd64)
	__asm__ __volatile__(
	    "movq %%gs:0x10,%0"		/* CPU_SELF */
	    : "=r" (__value));
#elif defined(__i386)
	__asm__ __volatile__(
	    "movl %%gs:0xc,%0"		/* CPU_SELF */
	    : "=r" (__value));
#else
#error	"port me"
#endif
	return (__value);
}

#endif	/* !__lint && __GNUC__ */

#ifdef	__cplusplus
}
#endif

#endif	/* _ASM_CPUVAR_H */
