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

/*      Copyright (c) 1990, 1991 UNIX System Laboratories, Inc. */
/*      Copyright (c) 1984, 1986, 1987, 1988, 1989, 1990 AT&T   */
/*        All Rights Reserved   */

/*      Copyright (c) 1987, 1988 Microsoft Corporation  */
/*        All Rights Reserved   */

/*
 * Copyright (c) 2009, Intel Corporation.
 * All rights reserved.
 */

#include <sys/asm_linkage.h>
#include <sys/asm_misc.h>
#include <sys/regset.h>
#include <sys/privregs.h>
#include <sys/x86_archext.h>

#if defined(__lint)
#include <sys/types.h>
#include <sys/fp.h>
#else
#include "assym.h"
#endif

#if defined(__lint)
 
uint_t
fpu_initial_probe(void)
{ return (0); }

#else	/* __lint */

	/*
	 * Returns zero if x87 "chip" is present(!)
	 */
	ENTRY_NP(fpu_initial_probe)
	CLTS
	fninit
	fnstsw	%ax
	movzbl	%al, %eax
	ret
	SET_SIZE(fpu_initial_probe)

#endif	/* __lint */

#if defined(__lint)

/*ARGSUSED*/
void
fxsave_insn(struct fxsave_state *fx)
{}

#else	/* __lint */

	ENTRY_NP(fxsave_insn)
	FXSAVEQ	((%rdi))
	ret
	SET_SIZE(fxsave_insn)

#endif	/* __lint */

#if defined(__lint)

void
patch_xsave(void)
{}

void
patch_xsaveopt(void)
{}

#else	/* __lint */

	/*
	 * Patch lazy fp restore instructions in the trap handler
	 * to use xrstor instead of fxrstorq
	 */
	ENTRY_NP(patch_xsave)
	_HOT_PATCH_PROLOG
	/
	/	FXRSTORQ (%rbx);	-> xrstor (%rbx)
	/
	_HOT_PATCH(_xrstor_rbx_insn, _patch_xrstorq_rbx, 4)
	_HOT_PATCH_EPILOG
	ret
_xrstor_rbx_insn:			/ see ndptrap_frstor()
	#rex.W=1 (.byte 0x48)
	#xrstor (%rbx)
	.byte	0x48, 0x0f, 0xae, 0x2b
	SET_SIZE(patch_xsave)


	/*
	 * Patch xsave in context switch to use xsaveopt
	 */
	ENTRY_NP(patch_xsaveopt)
	_HOT_PATCH_PROLOG
	/
	/	xsave (%rsi)	->	xsaveopt (%rsi)
	/
	_HOT_PATCH(_xsaveopt_rsi_insn, _patch_xsave_rsi, 3)
	_HOT_PATCH_EPILOG
	ret
_xsaveopt_rsi_insn:			/ see xsave_ctxt()
	#xsaveopt	(%rsi)
 	.byte	0x0f, 0xae, 0x36
	SET_SIZE(patch_xsaveopt)

#endif	/* __lint */

/*
 * One of these routines is called from any lwp with floating
 * point context as part of the prolog of a context switch.
 */

#if defined(__lint)

/*ARGSUSED*/
void
xsave_ctxt(void *arg)
{}

/*ARGSUSED*/
void
fpxsave_ctxt(void *arg)
{}

/*ARGSUSED*/
void
fpnsave_ctxt(void *arg)
{}

#else	/* __lint */

	ENTRY_NP(fpxsave_ctxt)
	cmpl	$FPU_EN, FPU_CTX_FPU_FLAGS(%rdi)
	jne	1f

	movl	$_CONST(FPU_VALID|FPU_EN), FPU_CTX_FPU_FLAGS(%rdi)
	FXSAVEQ	(FPU_CTX_FPU_REGS(%rdi))

	/*
	 * On certain AMD processors, the "exception pointers" i.e. the last
	 * instruction pointer, last data pointer, and last opcode
	 * are saved by the fxsave instruction ONLY if the exception summary
	 * bit is set.
	 *
	 * To ensure that we don't leak these values into the next context
	 * on the cpu, we could just issue an fninit here, but that's
	 * rather slow and so we issue an instruction sequence that
	 * clears them more quickly, if a little obscurely.
	 */
	btw	$7, FXSAVE_STATE_FSW(%rdi)	/* Test saved ES bit */
	jnc	0f				/* jump if ES = 0 */
	fnclex		/* clear pending x87 exceptions */
0:	ffree	%st(7)	/* clear tag bit to remove possible stack overflow */
	fildl	.fpzero_const(%rip)
			/* dummy load changes all exception pointers */
	STTS(%rsi)	/* trap on next fpu touch */
1:	rep;	ret	/* use 2 byte return instruction when branch target */
			/* AMD Software Optimization Guide - Section 6.2 */
	SET_SIZE(fpxsave_ctxt)

	ENTRY_NP(xsave_ctxt)
	cmpl	$FPU_EN, FPU_CTX_FPU_FLAGS(%rdi)
	jne	1f
	movl	$_CONST(FPU_VALID|FPU_EN), FPU_CTX_FPU_FLAGS(%rdi)
	/*
	 * Setup xsave flags in EDX:EAX
	 */
	movl	FPU_CTX_FPU_XSAVE_MASK(%rdi), %eax
	movl	FPU_CTX_FPU_XSAVE_MASK+4(%rdi), %edx
	leaq	FPU_CTX_FPU_REGS(%rdi), %rsi
	ALTENTRY(xsave_ctxt_xsaveopt)
	.globl	_patch_xsave_rsi
_patch_xsave_rsi:
	#xsave	(%rsi)
	.byte	0x0f, 0xae, 0x26
	
	/*
	 * (see notes above about "exception pointers")
	 * TODO: does it apply to any machine that uses xsave?
	 */
	btw	$7, FXSAVE_STATE_FSW(%rdi)	/* Test saved ES bit */
	jnc	0f				/* jump if ES = 0 */
	fnclex		/* clear pending x87 exceptions */
0:	ffree	%st(7)	/* clear tag bit to remove possible stack overflow */
	fildl	.fpzero_const(%rip)
			/* dummy load changes all exception pointers */
	STTS(%rsi)	/* trap on next fpu touch */
1:	ret
	SET_SIZE(xsave_ctxt_xsaveopt)
	SET_SIZE(xsave_ctxt)

	.align	8
.fpzero_const:
	.4byte	0x0
	.4byte	0x0

#endif	/* __lint */


#if defined(__lint)

/*ARGSUSED*/
void
fpsave(struct fnsave_state *f)
{}

/*ARGSUSED*/
void
fpxsave(struct fxsave_state *f)
{}

/*ARGSUSED*/
void
xsave(struct xsave_state *f, uint64_t m)
{}

#else	/* __lint */

	ENTRY_NP(fpxsave)
	CLTS
	FXSAVEQ	((%rdi))
	fninit				/* clear exceptions, init x87 tags */
	STTS(%rdi)			/* set TS bit in %cr0 (disable FPU) */
	ret
	SET_SIZE(fpxsave)

	ENTRY_NP(xsave)
	CLTS
	movl	%esi, %eax		/* bv mask */
	movq	%rsi, %rdx
	shrq	$32, %rdx
	#xsave	(%rdi)
	.byte	0x0f, 0xae, 0x27
	
	fninit				/* clear exceptions, init x87 tags */
	STTS(%rdi)			/* set TS bit in %cr0 (disable FPU) */
	ret
	SET_SIZE(xsave)

#endif	/* __lint */

#if defined(__lint)

/*ARGSUSED*/
void
fprestore(struct fnsave_state *f)
{}

/*ARGSUSED*/
void
fpxrestore(struct fxsave_state *f)
{}

/*ARGSUSED*/
void
xrestore(struct xsave_state *f, uint64_t m)
{}

#else	/* __lint */

	ENTRY_NP(fpxrestore)
	CLTS
	FXRSTORQ	((%rdi))
	ret
	SET_SIZE(fpxrestore)

	ENTRY_NP(xrestore)
	CLTS
	movl	%esi, %eax		/* bv mask */
	movq	%rsi, %rdx
	shrq	$32, %rdx
	#xrstor	(%rdi)
	.byte	0x0f, 0xae, 0x2f
	ret
	SET_SIZE(xrestore)

#endif	/* __lint */

/*
 * Disable the floating point unit.
 */

#if defined(__lint)

void
fpdisable(void)
{}

#else	/* __lint */

	ENTRY_NP(fpdisable)
	STTS(%rdi)			/* set TS bit in %cr0 (disable FPU) */ 
	ret
	SET_SIZE(fpdisable)

#endif	/* __lint */

/*
 * Initialize the fpu hardware.
 */

#if defined(__lint)

void
fpinit(void)
{}

#else	/* __lint */

	ENTRY_NP(fpinit)
	CLTS
	cmpl	$FP_XSAVE, fp_save_mech
	je	1f

	/* fxsave */
	leaq	sse_initial(%rip), %rax
	FXRSTORQ	((%rax))		/* load clean initial state */
	ret

1:	/* xsave */
	leaq	avx_initial(%rip), %rcx
	xorl	%edx, %edx
	movl	$XFEATURE_AVX, %eax
	btq	$X86FSET_AVX, x86_featureset(%rip)
	cmovael	%edx, %eax
	orl	$(XFEATURE_LEGACY_FP | XFEATURE_SSE), %eax
	/* xrstor (%rcx) */
	.byte	0x0f, 0xae, 0x29		/* load clean initial state */
	ret
	SET_SIZE(fpinit)

#endif	/* __lint */

/*
 * Clears FPU exception state.
 * Returns the FP status word.
 */

#if defined(__lint)

uint32_t
fperr_reset(void)
{ return (0); }

uint32_t
fpxerr_reset(void)
{ return (0); }

#else	/* __lint */

	ENTRY_NP(fperr_reset)
	CLTS
	xorl	%eax, %eax
	fnstsw	%ax
	fnclex
	ret
	SET_SIZE(fperr_reset)

	ENTRY_NP(fpxerr_reset)
	pushq	%rbp
	movq	%rsp, %rbp
	subq	$0x10, %rsp		/* make some temporary space */
	CLTS
	stmxcsr	(%rsp)
	movl	(%rsp), %eax
	andl	$_BITNOT(SSE_MXCSR_EFLAGS), (%rsp)
	ldmxcsr	(%rsp)			/* clear processor exceptions */
	leave
	ret
	SET_SIZE(fpxerr_reset)

#endif	/* __lint */

#if defined(__lint)

uint32_t
fpgetcwsw(void)
{
	return (0);
}

#else   /* __lint */

	ENTRY_NP(fpgetcwsw)
	pushq	%rbp
	movq	%rsp, %rbp
	subq	$0x10, %rsp		/* make some temporary space	*/
	CLTS
	fnstsw	(%rsp)			/* store the status word	*/
	fnstcw	2(%rsp)			/* store the control word	*/
	movl	(%rsp), %eax		/* put both in %eax		*/
	leave
	ret
	SET_SIZE(fpgetcwsw)

#endif  /* __lint */

/*
 * Returns the MXCSR register.
 */

#if defined(__lint)

uint32_t
fpgetmxcsr(void)
{
	return (0);
}

#else   /* __lint */

	ENTRY_NP(fpgetmxcsr)
	pushq	%rbp
	movq	%rsp, %rbp
	subq	$0x10, %rsp		/* make some temporary space */
	CLTS
	stmxcsr	(%rsp)
	movl	(%rsp), %eax
	leave
	ret
	SET_SIZE(fpgetmxcsr)

#endif  /* __lint */
