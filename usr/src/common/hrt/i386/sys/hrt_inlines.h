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

#ifndef _HRT_INLINES_H
#define	_HRT_INLINES_H

#ifdef	__cplusplus
extern "C" {
#endif

#ifndef _ASM
#if defined(__GNUC__) && defined(_ASM_INLINES)

extern __inline__ uint64_t
rdtscp(uint32_t *cpu_id)
{
	uint64_t __value;
#if defined(__i386)
	__asm__ __volatile__(
	    "rdtscp\n\t"
	    "movl %%ecx, %1"
	    : "=A" (__value), "=m" (*cpu_id)
	    :: "%ecx");
#elif defined(__amd64)
	__asm__ __volatile__(
	    "rdtscp\n\t"
	    "movl %%ecx, %1\n\t"
	    "shlq $32, %%rdx\n\t"
	    "orq %%rdx, %%rax"
	    : "=a" (__value), "=m" (*cpu_id)
	    :: "%rcx", "%rdx");
#endif	/* __i386 */
	return (__value);
}

#endif	/* __GNUC__ && _ASM_INLINES */
#endif	/* !_ASM */

#ifdef	__cplusplus
}
#endif

#endif	/* _HRT_INLINES_H */
