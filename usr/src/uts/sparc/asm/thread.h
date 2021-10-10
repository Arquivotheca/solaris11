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
 * Copyright (c) 2005, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _ASM_THREAD_H
#define	_ASM_THREAD_H

#include <sys/types.h>

#ifdef	__cplusplus
extern "C" {
#endif

#if !defined(__lint) && defined(__GNUC__)

struct _kthread;

extern __inline__ struct _kthread *
threadp(void)
{
	void *__value;

#if defined(__sparcv9)
	__asm__ __volatile__(
	    ".register %%g7, #scratch\n\t"
	    "mov %%g7, %0"
	    : "=r" (__value));
#else
#error	"port me"
#endif
	return (__value);
}

extern __inline__ caddr_t
caller(void)
{
	caddr_t __value;

#if defined(__sparcv9)
	__asm__ __volatile__(
	    "mov %%i7, %0"
	    : "=r" (__value));
#else
#error	"port me"
#endif
	return (__value);
}

extern __inline__ caddr_t
callee(void)
{
	caddr_t __value;

#if defined(__sparcv9)
	__asm__ __volatile__(
	    "mov %%o7, %0"
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

#endif	/* _ASM_THREAD_H */
