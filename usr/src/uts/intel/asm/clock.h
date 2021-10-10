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
 * Copyright (c) 2004, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _ASM_CLOCK_H
#define	_ASM_CLOCK_H

#include <sys/types.h>
#include <sys/time.h>

#ifdef	__cplusplus
extern "C" {
#endif

#if !defined(__lint) && defined(__GNUC__)

#include <sys/machlock.h>

#if defined(__xpv)

extern __inline__ hrtime_t __rdtsc_insn(void)
{
#if defined(__amd64)
	uint32_t lobits, hibits;

	__asm__ __volatile__(
	    "rdtsc"
	    : "=a" (lobits), "=d" (hibits));
	return (lobits | ((hrtime_t)hibits << 32));
#elif defined(__i386)
	hrtime_t __value;

	__asm__ __volatile__(
	    "rdtsc"
	    : "=A" (__value));
	return (__value);
#else
#error	"port me"
#endif
}

#endif /* __xpv */

#endif	/* !__lint && __GNUC__ */

#ifdef	__cplusplus
}
#endif

#endif	/* _ASM_CLOCK_H */
