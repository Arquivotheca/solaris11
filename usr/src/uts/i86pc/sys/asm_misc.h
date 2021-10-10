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

#ifndef _SYS_ASM_MISC_H
#define	_SYS_ASM_MISC_H

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef _ASM	/* The remainder of this file is only for assembly files */

/* Load reg with pointer to per-CPU structure */
#define	LOADCPU(reg)			\
	movq	%gs:CPU_SELF, reg;

#define	RET_INSTR	0xc3
#define	NOP_INSTR	0x90
#define	STI_INSTR	0xfb
#define	JMP_INSTR	0x00eb

/*
 * While as doesn't support fxsaveq/fxrstorq (fxsave/fxrstor with REX.W = 1)
 * we will use the FXSAVEQ/FXRSTORQ macro
 */

#define	FXSAVEQ(x)	\
	.byte	0x48;	\
	fxsave	x

#define	FXRSTORQ(x)	\
	.byte	0x48;	\
	fxrstor	x

#define	_HOT_PATCH_PROLOG			\
	pushq	%rbp;				\
	movq	%rsp, %rbp;			\
	pushq	%rbx;				\
	pushq	%r14;				\
	pushq	%r15

/* Clobbered: %rbx, %r14, %r15, argument registers (%rsi, %rdi, %rdx) */
#define	_HOT_PATCH(srcaddr, dstaddr, size)	\
	leaq	dstaddr(%rip), %rbx;		\
	leaq	srcaddr(%rip), %r14;		\
	movq	$size, %r15;			\
0:	movq	%rbx, %rdi;			\
	/*CSTYLED*/				\
	movzbq	(%r14), %rsi;			\
	xorq	%rdx, %rdx;			\
	incq	%rdx;				\
	call	hot_patch_kernel_text;		\
	incq	%rbx;				\
	incq	%r14;				\
	decq	%r15;				\
	jnz	0b

#define	_HOT_PATCH_EPILOG			\
	popq	%r15;				\
	popq	%r14;				\
	popq	%rbx;				\
	leaveq

#endif /* _ASM */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_ASM_MISC_H */
