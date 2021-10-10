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
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_SYS_MACHPRIVREGS_H
#define	_SYS_MACHPRIVREGS_H


/*
 * Platform dependent instruction sequences for manipulating
 * privileged state
 */

#ifdef __cplusplus
extern "C" {
#endif

#define	ASSERT_UPCALL_MASK_IS_SET		/* empty */

/*
 * CLI and STI
 */

#define	CLI(r)			\
	cli

#define	STI			\
	sti

/*
 * Used to re-enable interrupts in the body of exception handlers
 */

#define	ENABLE_INTR_FLAGS		\
	pushq	$F_ON;			\
	popfq

/*
 * IRET and SWAPGS
 */
#define	IRET	iretq
#define	SYSRETQ	sysretq
#define	SYSRETL	sysretl
#define	SWAPGS	swapgs
#define	XPV_TRAP_POP	/* empty */
#define	XPV_TRAP_PUSH	/* empty */

#define	CLEAN_CS	/* empty */


/*
 * Macros for saving the original segment registers and restoring them
 * for fast traps.
 */

/*
 * Smaller versions of INTR_PUSH and INTR_POP for fast traps.
 * The following registers have been pushed onto the stack by
 * hardware at this point:
 *
 *	greg_t  r_rip;
 *	greg_t  r_cs;
 *	greg_t  r_rfl;
 *	greg_t  r_rsp;
 *	greg_t  r_ss;
 *
 * This handler is executed both by 32-bit and 64-bit applications.
 * 64-bit applications allow us to treat the set (%rdi, %rsi, %rdx,
 * %rcx, %r8, %r9, %r10, %r11, %rax) as volatile across function calls.
 * However, 32-bit applications only expect (%eax, %edx, %ecx) to be volatile
 * across a function call -- in particular, %esi and %edi MUST be saved!
 *
 * We could do this differently by making a FAST_INTR_PUSH32 for 32-bit
 * programs, and FAST_INTR_PUSH for 64-bit programs, but it doesn't seem
 * particularly worth it.
 */
#define	FAST_INTR_PUSH			\
	INTGATE_INIT_KERNEL_FLAGS;	\
	subq	$REGOFF_RIP, %rsp;	\
	movq	%rsi, REGOFF_RSI(%rsp);	\
	movq	%rdi, REGOFF_RDI(%rsp);	\
	swapgs

#define	FAST_INTR_POP			\
	swapgs;				\
	movq	REGOFF_RSI(%rsp), %rsi;	\
	movq	REGOFF_RDI(%rsp), %rdi;	\
	addq	$REGOFF_RIP, %rsp

#define	FAST_INTR_RETURN	iretq

/*
 * Handling the CR0.TS bit for floating point handling.
 *
 * When the TS bit is *set*, attempts to touch the floating
 * point hardware will result in a #nm trap.
 */
#define	STTS(rtmp)		\
	movq	%cr0, rtmp;	\
	orq	$CR0_TS, rtmp;	\
	movq	rtmp, %cr0

#define	CLTS			\
	clts

#ifdef __cplusplus
}
#endif

#endif	/* _SYS_MACHPRIVREGS_H */
