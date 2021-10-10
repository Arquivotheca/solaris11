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
 * Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#if defined(lint) || defined(DS_DDICT)
#include <sys/types.h>
#include <sys/param.h>
#else
#include "assym.h"	/* Determine value of CPU_THREAD */
#include <sys/asm_linkage.h>
#endif

#ifdef DS_DDICT
#define	uint8_t	uchar_t
#endif


/*
 * Special support routines that can't be done with C
 * x86 variant
 */

/*
 * uint8_t nsc_ldstub(uint8_t *cp)
 *
 * Store 0xFF at the specified location, and return its previous content.
 */

#if defined(lint) || defined(DS_DDICT)
uint8_t
nsc_ldstub(uint8_t *cp)
{
	uint8_t rv;
	rv = *cp;
	*cp = 0xFF;
	return (rv);
}
#else
	ENTRY(nsc_ldstub)
#ifdef	__amd64
	movl    $0xff,%eax 
	lock
	xchgb   %al, (%rdi) 		/* rdi = lock addr */
	ret
#else
#error  "port this routine"
#endif	/* __amd64 */
	SET_SIZE(nsc_ldstub)
#endif

/*
 * nsc_membar_stld(void)
 *
 * On SPARC this is a C callable interface to SPARC asm membar instruction.
 * For x86 we brute force it with a #LOCK instruction.
 */

#if defined(lint) || defined(DS_DDICT)
void
nsc_membar_stld(void)
{}
#else

	ENTRY(nsc_membar_stld)
#ifdef	__amd64
	mfence
	ret
#else
#error	"port this routine"
#endif	/* __amd64 */
	SET_SIZE(nsc_membar_stld)

#endif	/* lint || DS_DDICT */


/*
 * if a() calls b() calls nsc_caller(),
 * nsc_caller() returns return address in a().
 */

#if defined(lint) || defined(DS_DDICT)
caddr_t
nsc_caller(void)
{
	return (0);
}
#else

	ENTRY(nsc_caller)
#ifdef	__amd64
	movq	8(%rbp), %rax		/* b()'s return pc, in a() */
	ret
#else
#error	"port this routine"
#endif	/* __amd64 */
	SET_SIZE(nsc_caller)

#endif  /* lint || DS_DDICT */


/*
 * if a() calls nsc_callee(), nsc_callee() returns the
 * return address in a();
 */

#if defined(lint) || defined(DS_DDICT)
caddr_t
nsc_callee(void)
{
	return (0);
}
#else

	ENTRY(nsc_callee)
#ifdef	__amd64
	movq	(%rsp), %rax		/* callee()'s return pc, in a() */
	ret
#else
#error	"port this routine"
#endif	/* __amd64 */
	SET_SIZE(nsc_callee)

#endif  /* lint || DS_DDICT */


/*
 * nsc_threadp(void)
 *
 * C callable interface to get the current thread pointer.
 */
 
#if defined(lint) || defined(DS_DDICT)
void *
nsc_threadp(void)
{
	return (NULL);
}
#else

	ENTRY(nsc_threadp)
#ifdef	__amd64
	movq    %gs:CPU_THREAD, %rax
	ret
#else
#error	"port this routine"
#endif	/* __amd64 */
	SET_SIZE(nsc_threadp)

#endif /* lint || DS_DDICT */
