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
 * Copyright (c) 2003, 2011, Oracle and/or its affiliates. All rights reserved.
 */


#include <sys/asm_linkage.h>
#include <sys/regset.h>

#if defined(lint)
#include <sys/dtrace_impl.h>
#else
#include "assym.h"
#endif

#if defined(lint) || defined(__lint)

greg_t
dtrace_getfp(void)
{ return (0); }

#else	/* lint */

	ENTRY_NP(dtrace_getfp)
	movq	%rbp, %rax
	ret
	SET_SIZE(dtrace_getfp)

#endif	/* lint */


#if defined(lint) || defined(__lint)

uint32_t
dtrace_cas32(uint32_t *target, uint32_t cmp, uint32_t new)
{
	uint32_t old;

	if ((old = *target) == cmp)
		*target = new;
	return (old);
}

void *
dtrace_casptr(void *target, void *cmp, void *new)
{
	void *old;

	if ((old = *(void **)target) == cmp)
		*(void **)target = new;
	return (old);
}

#else	/* lint */

	ENTRY(dtrace_cas32)
	movl	%esi, %eax
	lock
	cmpxchgl %edx, (%rdi)
	ret
	SET_SIZE(dtrace_cas32)

	ENTRY(dtrace_casptr)
	movq	%rsi, %rax
	lock
	cmpxchgq %rdx, (%rdi)
	ret
	SET_SIZE(dtrace_casptr)

#endif	/* lint */

#if defined(lint)

/*ARGSUSED*/
uintptr_t
dtrace_caller(int aframes)
{
	return (0);
}

#else	/* lint */

	ENTRY(dtrace_caller)
	movq	$-1, %rax
	ret
	SET_SIZE(dtrace_caller)

#endif	/* lint */

#if defined(lint)

/*ARGSUSED*/
void
dtrace_copy(uintptr_t src, uintptr_t dest, size_t size)
{}

#else

	ENTRY(dtrace_copy)
	pushq	%rbp
	movq	%rsp, %rbp

	xchgq	%rdi, %rsi		/* make %rsi source, %rdi dest */
	movq	%rdx, %rcx		/* load count */
	repz				/* repeat for count ... */
	smovb				/*   move from %ds:rsi to %ed:rdi */
	leave
	ret
	SET_SIZE(dtrace_copy)

#endif

#if defined(lint)

/*ARGSUSED*/
void
dtrace_copystr(uintptr_t uaddr, uintptr_t kaddr, size_t size,
    volatile uint16_t *flags)
{}

#else

	ENTRY(dtrace_copystr)
	pushq	%rbp
	movq	%rsp, %rbp

0:
	movb	(%rdi), %al		/* load from source */
	movb	%al, (%rsi)		/* store to destination */
	addq	$1, %rdi		/* increment source pointer */
	addq	$1, %rsi		/* increment destination pointer */
	subq	$1, %rdx		/* decrement remaining count */
	cmpb	$0, %al
	je	2f
	testq	$0xfff, %rdx		/* test if count is 4k-aligned */
	jnz	1f			/* if not, continue with copying */
	testq	$CPU_DTRACE_BADADDR, (%rcx) /* load and test dtrace flags */
	jnz	2f
1:
	cmpq	$0, %rdx
	jne	0b
2:
	leave
	ret

	SET_SIZE(dtrace_copystr)

#endif

#if defined(lint)

/*ARGSUSED*/
uintptr_t
dtrace_fulword(void *addr)
{ return (0); }

#else

	ENTRY(dtrace_fulword)
	movq	(%rdi), %rax
	ret
	SET_SIZE(dtrace_fulword)

#endif

#if defined(lint)

/*ARGSUSED*/
uint8_t
dtrace_fuword8_nocheck(void *addr)
{ return (0); }

#else

	ENTRY(dtrace_fuword8_nocheck)
	xorq	%rax, %rax
	movb	(%rdi), %al
	ret
	SET_SIZE(dtrace_fuword8_nocheck)

#endif

#if defined(lint)

/*ARGSUSED*/
uint16_t
dtrace_fuword16_nocheck(void *addr)
{ return (0); }

#else

	ENTRY(dtrace_fuword16_nocheck)
	xorq	%rax, %rax
	movw	(%rdi), %ax
	ret
	SET_SIZE(dtrace_fuword16_nocheck)

#endif

#if defined(lint)

/*ARGSUSED*/
uint32_t
dtrace_fuword32_nocheck(void *addr)
{ return (0); }

#else

	ENTRY(dtrace_fuword32_nocheck)
	xorq	%rax, %rax
	movl	(%rdi), %eax
	ret
	SET_SIZE(dtrace_fuword32_nocheck)

#endif

#if defined(lint)

/*ARGSUSED*/
uint64_t
dtrace_fuword64_nocheck(void *addr)
{ return (0); }

#else

	ENTRY(dtrace_fuword64_nocheck)
	movq	(%rdi), %rax
	ret
	SET_SIZE(dtrace_fuword64_nocheck)

#endif

#if defined(lint) || defined(__lint)

/*ARGSUSED*/
void
dtrace_probe_error(dtrace_state_t *state, dtrace_epid_t epid, int which,
    int fault, int fltoffs, uintptr_t illval)
{}

#else	/* lint */

	ENTRY(dtrace_probe_error)
	pushq	%rbp
	movq	%rsp, %rbp
	subq	$0x8, %rsp
	movq	%r9, (%rsp)
	movq	%r8, %r9
	movq	%rcx, %r8
	movq	%rdx, %rcx
	movq	%rsi, %rdx
	movq	%rdi, %rsi
	movl	dtrace_probeid_error(%rip), %edi
	call	dtrace_probe
	addq	$0x8, %rsp
	leave
	ret
	SET_SIZE(dtrace_probe_error)
	
#endif
