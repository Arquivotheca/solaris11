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

/*
 * Copyright (c) 2009, Intel Corporation
 * All rights reserved.
 */

/*       Copyright (c) 1990, 1991 UNIX System Laboratories, Inc.	*/
/*       Copyright (c) 1984, 1986, 1987, 1988, 1989, 1990 AT&T		*/
/*         All Rights Reserved						*/

/*       Copyright (c) 1987, 1988 Microsoft Corporation			*/
/*         All Rights Reserved						*/

#include <sys/errno.h>
#include <sys/asm_linkage.h>

#if defined(__lint)
#include <sys/types.h>
#include <sys/systm.h>
#else	/* __lint */
#include "assym.h"
#endif	/* __lint */

#define	KCOPY_MIN_SIZE	128	/* Must be >= 16 bytes */
#define	XCOPY_MIN_SIZE	128	/* Must be >= 16 bytes */
/*
 * Non-temopral access (NTA) alignment requirement
 */
#define	NTA_ALIGN_SIZE	4	/* Must be at least 4-byte aligned */
#define	NTA_ALIGN_MASK	_CONST(NTA_ALIGN_SIZE-1)
#define	COUNT_ALIGN_SIZE	16	/* Must be at least 16-byte aligned */
#define	COUNT_ALIGN_MASK	_CONST(COUNT_ALIGN_SIZE-1)

/*
 * The optimal 64-bit bcopy and kcopy for modern x86 processors uses
 * "rep smovq" for large sizes. Performance data shows that many calls to
 * bcopy/kcopy/bzero/kzero operate on small buffers. For best performance for
 * these small sizes unrolled code is used. For medium sizes loops writing
 * 64-bytes per loop are used. Transition points were determined experimentally.
 */ 
#define BZERO_USE_REP	(1024)
#define BCOPY_DFLT_REP	(128)
#define	BCOPY_NHM_REP	(768)

/*
 * Copy a block of storage, returning an error code if `from' or
 * `to' takes a kernel pagefault which cannot be resolved.
 * Returns errno value on pagefault error, 0 if all ok
 */

#if defined(__lint)

/* ARGSUSED */
int
kcopy(const void *from, void *to, size_t count)
{ return (0); }

#else	/* __lint */

	.globl	kernelbase
	.globl	postbootkernelbase

	ENTRY(kcopy)
	pushq	%rbp
	movq	%rsp, %rbp
#ifdef DEBUG
	cmpq	postbootkernelbase(%rip), %rdi 		/* %rdi = from */
	jb	0f
	cmpq	postbootkernelbase(%rip), %rsi		/* %rsi = to */
	jnb	1f
0:	leaq	.kcopy_panic_msg(%rip), %rdi
	xorl	%eax, %eax
	call	panic
1:
#endif
	/*
	 * pass lofault value as 4th argument to do_copy_fault
	 */
	leaq	_kcopy_copyerr(%rip), %rcx
	movq	%gs:CPU_THREAD, %r9	/* %r9 = thread addr */

do_copy_fault:
	movq	T_LOFAULT(%r9), %r11	/* save the current lofault */
	movq	%rcx, T_LOFAULT(%r9)	/* new lofault */
	call	bcopy_altentry
	xorl	%eax, %eax		/* return 0 (success) */

	/*
	 * A fault during do_copy_fault is indicated through an errno value
	 * in %rax and we iretq from the trap handler to here.
	 */
_kcopy_copyerr:
	movq	%r11, T_LOFAULT(%r9)	/* restore original lofault */
	leave
	ret
	SET_SIZE(kcopy)

#endif	/* __lint */

#if defined(__lint)

/*
 * Copy a block of storage.  Similar to kcopy but uses non-temporal
 * instructions.
 */

/* ARGSUSED */
int
kcopy_nta(const void *from, void *to, size_t count, int copy_cached)
{ return (0); }

#else	/* __lint */

#define	COPY_LOOP_INIT(src, dst, cnt)	\
	addq	cnt, src;			\
	addq	cnt, dst;			\
	shrq	$3, cnt;			\
	neg	cnt

	/* Copy 16 bytes per loop.  Uses %rax and %r8 */
#define	COPY_LOOP_BODY(src, dst, cnt)	\
	prefetchnta	0x100(src, cnt, 8);	\
	movq	(src, cnt, 8), %rax;		\
	movq	0x8(src, cnt, 8), %r8;		\
	movnti	%rax, (dst, cnt, 8);		\
	movnti	%r8, 0x8(dst, cnt, 8);		\
	addq	$2, cnt

	ENTRY(kcopy_nta)
	pushq	%rbp
	movq	%rsp, %rbp
#ifdef DEBUG
	cmpq	postbootkernelbase(%rip), %rdi 		/* %rdi = from */
	jb	0f
	cmpq	postbootkernelbase(%rip), %rsi		/* %rsi = to */
	jnb	1f
0:	leaq	.kcopy_panic_msg(%rip), %rdi
	xorl	%eax, %eax
	call	panic
1:
#endif

	movq	%gs:CPU_THREAD, %r9
	cmpq	$0, %rcx		/* No non-temporal access? */
	/*
	 * pass lofault value as 4th argument to do_copy_fault
	 */
	leaq	_kcopy_nta_copyerr(%rip), %rcx	/* doesn't set rflags */
	jnz	do_copy_fault		/* use regular access */
	/*
	 * Make sure cnt is >= KCOPY_MIN_SIZE
	 */
	cmpq	$KCOPY_MIN_SIZE, %rdx
	jb	do_copy_fault

	/*
	 * Make sure src and dst are NTA_ALIGN_SIZE aligned,
	 * count is COUNT_ALIGN_SIZE aligned.
	 */
	movq	%rdi, %r10
	orq	%rsi, %r10
	andq	$NTA_ALIGN_MASK, %r10
	orq	%rdx, %r10
	andq	$COUNT_ALIGN_MASK, %r10
	jnz	do_copy_fault

	ALTENTRY(do_copy_fault_nta)
	movq    %gs:CPU_THREAD, %r9     /* %r9 = thread addr */
	movq    T_LOFAULT(%r9), %r11    /* save the current lofault */
	movq    %rcx, T_LOFAULT(%r9)    /* new lofault */

	/*
	 * COPY_LOOP_BODY uses %rax and %r8
	 */
	COPY_LOOP_INIT(%rdi, %rsi, %rdx)
2:	COPY_LOOP_BODY(%rdi, %rsi, %rdx)
	jnz	2b

	mfence
	xorl	%eax, %eax		/* return 0 (success) */

_kcopy_nta_copyerr:
	movq	%r11, T_LOFAULT(%r9)    /* restore original lofault */
	leave
	ret
	SET_SIZE(do_copy_fault_nta)
	SET_SIZE(kcopy_nta)

#endif	/* __lint */

#if defined(__lint)

/* ARGSUSED */
void
bcopy(const void *from, void *to, size_t count)
{}

#else	/* __lint */

	ENTRY(bcopy)
#ifdef DEBUG
	orq	%rdx, %rdx		/* %rdx = count */
	jz	1f
	cmpq	postbootkernelbase(%rip), %rdi		/* %rdi = from */
	jb	0f
	cmpq	postbootkernelbase(%rip), %rsi		/* %rsi = to */		
	jnb	1f
0:	leaq	.bcopy_panic_msg(%rip), %rdi
	jmp	call_panic		/* setup stack and call panic */
1:
#endif
	/*
	 * bcopy_altentry() is called from kcopy, i.e., do_copy_fault.
	 * kcopy assumes that bcopy doesn't touch %r9 and %r11. If bcopy
	 * uses these registers in future they must be saved and restored.
	 */
	ALTENTRY(bcopy_altentry)
do_copy:
#define	L(s) .bcopy/**/s
	cmpq	$0x50, %rdx		/* 80 */
	jge	bcopy_ck_size

	/*
	 * Performance data shows many caller's copy small buffers. So for
	 * best perf for these sizes unrolled code is used. Store data without
	 * worrying about alignment.
	 */
	leaq	L(fwdPxQx)(%rip), %r10
	addq	%rdx, %rdi
	addq	%rdx, %rsi
	movslq	(%r10,%rdx,4), %rcx
	leaq	(%rcx,%r10,1), %r10
	jmpq	*%r10

	.p2align 4
L(fwdPxQx):
	.int       L(P0Q0)-L(fwdPxQx)	/* 0 */
	.int       L(P1Q0)-L(fwdPxQx)
	.int       L(P2Q0)-L(fwdPxQx)
	.int       L(P3Q0)-L(fwdPxQx)
	.int       L(P4Q0)-L(fwdPxQx)
	.int       L(P5Q0)-L(fwdPxQx)
	.int       L(P6Q0)-L(fwdPxQx)
	.int       L(P7Q0)-L(fwdPxQx) 

	.int       L(P0Q1)-L(fwdPxQx)	/* 8 */
	.int       L(P1Q1)-L(fwdPxQx)
	.int       L(P2Q1)-L(fwdPxQx)
	.int       L(P3Q1)-L(fwdPxQx)
	.int       L(P4Q1)-L(fwdPxQx)
	.int       L(P5Q1)-L(fwdPxQx)
	.int       L(P6Q1)-L(fwdPxQx)
	.int       L(P7Q1)-L(fwdPxQx) 

	.int       L(P0Q2)-L(fwdPxQx)	/* 16 */
	.int       L(P1Q2)-L(fwdPxQx)
	.int       L(P2Q2)-L(fwdPxQx)
	.int       L(P3Q2)-L(fwdPxQx)
	.int       L(P4Q2)-L(fwdPxQx)
	.int       L(P5Q2)-L(fwdPxQx)
	.int       L(P6Q2)-L(fwdPxQx)
	.int       L(P7Q2)-L(fwdPxQx) 

	.int       L(P0Q3)-L(fwdPxQx)	/* 24 */
	.int       L(P1Q3)-L(fwdPxQx)
	.int       L(P2Q3)-L(fwdPxQx)
	.int       L(P3Q3)-L(fwdPxQx)
	.int       L(P4Q3)-L(fwdPxQx)
	.int       L(P5Q3)-L(fwdPxQx)
	.int       L(P6Q3)-L(fwdPxQx)
	.int       L(P7Q3)-L(fwdPxQx) 

	.int       L(P0Q4)-L(fwdPxQx)	/* 32 */
	.int       L(P1Q4)-L(fwdPxQx)
	.int       L(P2Q4)-L(fwdPxQx)
	.int       L(P3Q4)-L(fwdPxQx)
	.int       L(P4Q4)-L(fwdPxQx)
	.int       L(P5Q4)-L(fwdPxQx)
	.int       L(P6Q4)-L(fwdPxQx)
	.int       L(P7Q4)-L(fwdPxQx) 

	.int       L(P0Q5)-L(fwdPxQx)	/* 40 */
	.int       L(P1Q5)-L(fwdPxQx)
	.int       L(P2Q5)-L(fwdPxQx)
	.int       L(P3Q5)-L(fwdPxQx)
	.int       L(P4Q5)-L(fwdPxQx)
	.int       L(P5Q5)-L(fwdPxQx)
	.int       L(P6Q5)-L(fwdPxQx)
	.int       L(P7Q5)-L(fwdPxQx) 

	.int       L(P0Q6)-L(fwdPxQx)	/* 48 */
	.int       L(P1Q6)-L(fwdPxQx)
	.int       L(P2Q6)-L(fwdPxQx)
	.int       L(P3Q6)-L(fwdPxQx)
	.int       L(P4Q6)-L(fwdPxQx)
	.int       L(P5Q6)-L(fwdPxQx)
	.int       L(P6Q6)-L(fwdPxQx)
	.int       L(P7Q6)-L(fwdPxQx) 

	.int       L(P0Q7)-L(fwdPxQx)	/* 56 */
	.int       L(P1Q7)-L(fwdPxQx)
	.int       L(P2Q7)-L(fwdPxQx)
	.int       L(P3Q7)-L(fwdPxQx)
	.int       L(P4Q7)-L(fwdPxQx)
	.int       L(P5Q7)-L(fwdPxQx)
	.int       L(P6Q7)-L(fwdPxQx)
	.int       L(P7Q7)-L(fwdPxQx) 

	.int       L(P0Q8)-L(fwdPxQx)	/* 64 */
	.int       L(P1Q8)-L(fwdPxQx)
	.int       L(P2Q8)-L(fwdPxQx)
	.int       L(P3Q8)-L(fwdPxQx)
	.int       L(P4Q8)-L(fwdPxQx)
	.int       L(P5Q8)-L(fwdPxQx)
	.int       L(P6Q8)-L(fwdPxQx)
	.int       L(P7Q8)-L(fwdPxQx)

	.int       L(P0Q9)-L(fwdPxQx)	/* 72 */
	.int       L(P1Q9)-L(fwdPxQx)
	.int       L(P2Q9)-L(fwdPxQx)
	.int       L(P3Q9)-L(fwdPxQx)
	.int       L(P4Q9)-L(fwdPxQx)
	.int       L(P5Q9)-L(fwdPxQx)
	.int       L(P6Q9)-L(fwdPxQx)
	.int       L(P7Q9)-L(fwdPxQx)	/* 79 */

	.p2align 4
L(P0Q9):
	mov    -0x48(%rdi), %rcx
	mov    %rcx, -0x48(%rsi)
L(P0Q8):
	mov    -0x40(%rdi), %r10
	mov    %r10, -0x40(%rsi)
L(P0Q7):
	mov    -0x38(%rdi), %r8
	mov    %r8, -0x38(%rsi)
L(P0Q6):
	mov    -0x30(%rdi), %rcx
	mov    %rcx, -0x30(%rsi)
L(P0Q5):
	mov    -0x28(%rdi), %r10
	mov    %r10, -0x28(%rsi)
L(P0Q4):
	mov    -0x20(%rdi), %r8
	mov    %r8, -0x20(%rsi)
L(P0Q3):
	mov    -0x18(%rdi), %rcx
	mov    %rcx, -0x18(%rsi)
L(P0Q2):
	mov    -0x10(%rdi), %r10
	mov    %r10, -0x10(%rsi)
L(P0Q1):
	mov    -0x8(%rdi), %r8
	mov    %r8, -0x8(%rsi)
L(P0Q0):                                   
	ret   

	.p2align 4
L(P1Q9):
	mov    -0x49(%rdi), %r8
	mov    %r8, -0x49(%rsi)
L(P1Q8):
	mov    -0x41(%rdi), %rcx
	mov    %rcx, -0x41(%rsi)
L(P1Q7):
	mov    -0x39(%rdi), %r10
	mov    %r10, -0x39(%rsi)
L(P1Q6):
	mov    -0x31(%rdi), %r8
	mov    %r8, -0x31(%rsi)
L(P1Q5):
	mov    -0x29(%rdi), %rcx
	mov    %rcx, -0x29(%rsi)
L(P1Q4):
	mov    -0x21(%rdi), %r10
	mov    %r10, -0x21(%rsi)
L(P1Q3):
	mov    -0x19(%rdi), %r8
	mov    %r8, -0x19(%rsi)
L(P1Q2):
	mov    -0x11(%rdi), %rcx
	mov    %rcx, -0x11(%rsi)
L(P1Q1):
	mov    -0x9(%rdi), %r10
	mov    %r10, -0x9(%rsi)
L(P1Q0):
	movzbq -0x1(%rdi), %r8
	mov    %r8b, -0x1(%rsi)
	ret   

	.p2align 4
L(P2Q9):
	mov    -0x4a(%rdi), %r8
	mov    %r8, -0x4a(%rsi)
L(P2Q8):
	mov    -0x42(%rdi), %rcx
	mov    %rcx, -0x42(%rsi)
L(P2Q7):
	mov    -0x3a(%rdi), %r10
	mov    %r10, -0x3a(%rsi)
L(P2Q6):
	mov    -0x32(%rdi), %r8
	mov    %r8, -0x32(%rsi)
L(P2Q5):
	mov    -0x2a(%rdi), %rcx
	mov    %rcx, -0x2a(%rsi)
L(P2Q4):
	mov    -0x22(%rdi), %r10
	mov    %r10, -0x22(%rsi)
L(P2Q3):
	mov    -0x1a(%rdi), %r8
	mov    %r8, -0x1a(%rsi)
L(P2Q2):
	mov    -0x12(%rdi), %rcx
	mov    %rcx, -0x12(%rsi)
L(P2Q1):
	mov    -0xa(%rdi), %r10
	mov    %r10, -0xa(%rsi)
L(P2Q0):
	movzwq -0x2(%rdi), %r8
	mov    %r8w, -0x2(%rsi)
	ret   

	.p2align 4
L(P3Q9):
	mov    -0x4b(%rdi), %r8
	mov    %r8, -0x4b(%rsi)
L(P3Q8):
	mov    -0x43(%rdi), %rcx
	mov    %rcx, -0x43(%rsi)
L(P3Q7):
	mov    -0x3b(%rdi), %r10
	mov    %r10, -0x3b(%rsi)
L(P3Q6):
	mov    -0x33(%rdi), %r8
	mov    %r8, -0x33(%rsi)
L(P3Q5):
	mov    -0x2b(%rdi), %rcx
	mov    %rcx, -0x2b(%rsi)
L(P3Q4):
	mov    -0x23(%rdi), %r10
	mov    %r10, -0x23(%rsi)
L(P3Q3):
	mov    -0x1b(%rdi), %r8
	mov    %r8, -0x1b(%rsi)
L(P3Q2):
	mov    -0x13(%rdi), %rcx
	mov    %rcx, -0x13(%rsi)
L(P3Q1):
	mov    -0xb(%rdi), %r10
	mov    %r10, -0xb(%rsi)
	/*
	 * These trailing loads/stores have to do all their loads 1st, 
	 * then do the stores.
	 */
L(P3Q0):
	movzwq -0x3(%rdi), %r8
	movzbq -0x1(%rdi), %r10
	mov    %r8w, -0x3(%rsi)
	mov    %r10b, -0x1(%rsi)
	ret   

	.p2align 4
L(P4Q9):
	mov    -0x4c(%rdi), %r8
	mov    %r8, -0x4c(%rsi)
L(P4Q8):
	mov    -0x44(%rdi), %rcx
	mov    %rcx, -0x44(%rsi)
L(P4Q7):
	mov    -0x3c(%rdi), %r10
	mov    %r10, -0x3c(%rsi)
L(P4Q6):
	mov    -0x34(%rdi), %r8
	mov    %r8, -0x34(%rsi)
L(P4Q5):
	mov    -0x2c(%rdi), %rcx
	mov    %rcx, -0x2c(%rsi)
L(P4Q4):
	mov    -0x24(%rdi), %r10
	mov    %r10, -0x24(%rsi)
L(P4Q3):
	mov    -0x1c(%rdi), %r8
	mov    %r8, -0x1c(%rsi)
L(P4Q2):
	mov    -0x14(%rdi), %rcx
	mov    %rcx, -0x14(%rsi)
L(P4Q1):
	mov    -0xc(%rdi), %r10
	mov    %r10, -0xc(%rsi)
L(P4Q0):
	mov    -0x4(%rdi), %r8d
	mov    %r8d, -0x4(%rsi)
	ret   

	.p2align 4
L(P5Q9):
	mov    -0x4d(%rdi), %r8
	mov    %r8, -0x4d(%rsi)
L(P5Q8):
	mov    -0x45(%rdi), %rcx
	mov    %rcx, -0x45(%rsi)
L(P5Q7):
	mov    -0x3d(%rdi), %r10
	mov    %r10, -0x3d(%rsi)
L(P5Q6):
	mov    -0x35(%rdi), %r8
	mov    %r8, -0x35(%rsi)
L(P5Q5):
	mov    -0x2d(%rdi), %rcx
	mov    %rcx, -0x2d(%rsi)
L(P5Q4):
	mov    -0x25(%rdi), %r10
	mov    %r10, -0x25(%rsi)
L(P5Q3):
	mov    -0x1d(%rdi), %r8
	mov    %r8, -0x1d(%rsi)
L(P5Q2):
	mov    -0x15(%rdi), %rcx
	mov    %rcx, -0x15(%rsi)
L(P5Q1):
	mov    -0xd(%rdi), %r10
	mov    %r10, -0xd(%rsi)
L(P5Q0):
	mov    -0x5(%rdi), %r8d
	movzbq -0x1(%rdi), %r10
	mov    %r8d, -0x5(%rsi)
	mov    %r10b, -0x1(%rsi)
	ret   

	.p2align 4
L(P6Q9):
	mov    -0x4e(%rdi), %r8
	mov    %r8, -0x4e(%rsi)
L(P6Q8):
	mov    -0x46(%rdi), %rcx
	mov    %rcx, -0x46(%rsi)
L(P6Q7):
	mov    -0x3e(%rdi), %r10
	mov    %r10, -0x3e(%rsi)
L(P6Q6):
	mov    -0x36(%rdi), %r8
	mov    %r8, -0x36(%rsi)
L(P6Q5):
	mov    -0x2e(%rdi), %rcx
	mov    %rcx, -0x2e(%rsi)
L(P6Q4):
	mov    -0x26(%rdi), %r10
	mov    %r10, -0x26(%rsi)
L(P6Q3):
	mov    -0x1e(%rdi), %r8
	mov    %r8, -0x1e(%rsi)
L(P6Q2):
	mov    -0x16(%rdi), %rcx
	mov    %rcx, -0x16(%rsi)
L(P6Q1):
	mov    -0xe(%rdi), %r10
	mov    %r10, -0xe(%rsi)
L(P6Q0):
	mov    -0x6(%rdi), %r8d
	movzwq -0x2(%rdi), %r10
	mov    %r8d, -0x6(%rsi)
	mov    %r10w, -0x2(%rsi)
	ret   

	.p2align 4
L(P7Q9):
	mov    -0x4f(%rdi), %r8
	mov    %r8, -0x4f(%rsi)
L(P7Q8):
	mov    -0x47(%rdi), %rcx
	mov    %rcx, -0x47(%rsi)
L(P7Q7):
	mov    -0x3f(%rdi), %r10
	mov    %r10, -0x3f(%rsi)
L(P7Q6):
	mov    -0x37(%rdi), %r8
	mov    %r8, -0x37(%rsi)
L(P7Q5):
	mov    -0x2f(%rdi), %rcx
	mov    %rcx, -0x2f(%rsi)
L(P7Q4):
	mov    -0x27(%rdi), %r10
	mov    %r10, -0x27(%rsi)
L(P7Q3):
	mov    -0x1f(%rdi), %r8
	mov    %r8, -0x1f(%rsi)
L(P7Q2):
	mov    -0x17(%rdi), %rcx
	mov    %rcx, -0x17(%rsi)
L(P7Q1):
	mov    -0xf(%rdi), %r10
	mov    %r10, -0xf(%rsi)
L(P7Q0):
	mov    -0x7(%rdi), %r8d
	movzwq -0x3(%rdi), %r10
	movzbq -0x1(%rdi), %rcx
	mov    %r8d, -0x7(%rsi)
	mov    %r10w, -0x3(%rsi)
	mov    %cl, -0x1(%rsi)
	ret   

	/*
	 * For large sizes rep smovq is fastest.
	 * Transition point determined experimentally as measured on
	 * Intel Xeon processors (incl. Nehalem and previous generations) and
	 * AMD Opteron. The transition value is patched at boot time to avoid
	 * memory reference hit.
	 */
	.globl bcopy_patch_start
bcopy_patch_start:
	cmpq	$BCOPY_NHM_REP, %rdx
	.globl bcopy_patch_end
bcopy_patch_end:

	.p2align 4
	.globl bcopy_ck_size
bcopy_ck_size:
	cmpq	$BCOPY_DFLT_REP, %rdx
	jge	L(use_rep)

	/*
	 * Align to a 8-byte boundary. Avoids penalties from unaligned stores
	 * as well as from stores spanning cachelines.
	 */
	test	$0x7, %rsi
	jz	L(aligned_loop)
	test	$0x1, %rsi
	jz	2f
	movzbq	(%rdi), %r8
	dec	%rdx
	inc	%rdi
	mov	%r8b, (%rsi)
	inc	%rsi
2:
	test	$0x2, %rsi
	jz	4f
	movzwq	(%rdi), %r8
	sub	$0x2, %rdx
	add	$0x2, %rdi
	mov	%r8w, (%rsi)
	add	$0x2, %rsi
4:
	test	$0x4, %rsi
	jz	L(aligned_loop)
	mov	(%rdi), %r8d
	sub	$0x4, %rdx
	add	$0x4, %rdi
	mov	%r8d, (%rsi)
	add	$0x4, %rsi

	/*
	 * Copy 64-bytes per loop
	 */
	.p2align 4
L(aligned_loop):
	mov	(%rdi), %r8
	mov	0x8(%rdi), %r10
	lea	-0x40(%rdx), %rdx
	mov	%r8, (%rsi)
	mov	%r10, 0x8(%rsi)
	mov	0x10(%rdi), %rcx
	mov	0x18(%rdi), %r8
	mov	%rcx, 0x10(%rsi)
	mov	%r8, 0x18(%rsi)

	cmp	$0x40, %rdx
	mov	0x20(%rdi), %r10
	mov	0x28(%rdi), %rcx
	mov	%r10, 0x20(%rsi)
	mov	%rcx, 0x28(%rsi)
	mov	0x30(%rdi), %r8
	mov	0x38(%rdi), %r10
	lea	0x40(%rdi), %rdi
	mov	%r8, 0x30(%rsi)
	mov	%r10, 0x38(%rsi)
	lea	0x40(%rsi), %rsi
	jge	L(aligned_loop)

	/*
	 * Copy remaining bytes (0-63)
	 */
L(do_remainder):
	leaq	L(fwdPxQx)(%rip), %r10
	addq	%rdx, %rdi
	addq	%rdx, %rsi
	movslq	(%r10,%rdx,4), %rcx
	leaq	(%rcx,%r10,1), %r10
	jmpq	*%r10

	/*
	 * Use rep smovq. Clear remainder via unrolled code
	 */
	.p2align 4
L(use_rep):
	xchgq	%rdi, %rsi		/* %rsi = source, %rdi = destination */
	movq	%rdx, %rcx		/* %rcx = count */
	shrq	$3, %rcx		/* 8-byte word count */
	rep
	  smovq

	xchgq	%rsi, %rdi		/* %rdi = src, %rsi = destination */
	andq	$7, %rdx		/* remainder */
	jnz	L(do_remainder)
	ret
#undef	L

#ifdef DEBUG
	/*
	 * Setup frame on the run-time stack. The end of the input argument
	 * area must be aligned on a 16 byte boundary. The stack pointer %rsp,
	 * always points to the end of the latest allocated stack frame.
	 * panic(const char *format, ...) is a varargs function. When a
	 * function taking variable arguments is called, %rax must be set
	 * to eight times the number of floating point parameters passed
	 * to the function in SSE registers.
	 */
call_panic:
	pushq	%rbp			/* align stack properly */
	movq	%rsp, %rbp
	xorl	%eax, %eax		/* no variable arguments */
	call	panic			/* %rdi = format string */
#endif
	SET_SIZE(bcopy_altentry)
	SET_SIZE(bcopy)

#endif	/* __lint */


/*
 * Zero a block of storage, returning an error code if we
 * take a kernel pagefault which cannot be resolved.
 * Returns errno value on pagefault error, 0 if all ok
 */

#if defined(__lint)

/* ARGSUSED */
int
kzero(void *addr, size_t count)
{ return (0); }

#else	/* __lint */

	ENTRY(kzero)
#ifdef DEBUG
        cmpq	postbootkernelbase(%rip), %rdi	/* %rdi = addr */
        jnb	0f
        leaq	.kzero_panic_msg(%rip), %rdi
	jmp	call_panic		/* setup stack and call panic */
0:
#endif
	/*
	 * pass lofault value as 3rd argument for fault return 
	 */
	leaq	_kzeroerr(%rip), %rdx

	movq	%gs:CPU_THREAD, %r9	/* %r9 = thread addr */
	movq	T_LOFAULT(%r9), %r11	/* save the current lofault */
	movq	%rdx, T_LOFAULT(%r9)	/* new lofault */
	call	bzero_altentry
	xorl	%eax, %eax
	movq	%r11, T_LOFAULT(%r9)	/* restore the original lofault */
	ret
	/*
	 * A fault during bzero is indicated through an errno value
	 * in %rax when we iretq to here.
	 */
_kzeroerr:
	addq	$8, %rsp		/* pop bzero_altentry call ret addr */
	movq	%r11, T_LOFAULT(%r9)	/* restore the original lofault */
	ret
	SET_SIZE(kzero)

#endif	/* __lint */

/*
 * Zero a block of storage.
 */

#if defined(__lint)

/* ARGSUSED */
void
bzero(void *addr, size_t count)
{}

#else	/* __lint */

	ENTRY(bzero)
#ifdef DEBUG
	cmpq	postbootkernelbase(%rip), %rdi	/* %rdi = addr */
	jnb	0f
	leaq	.bzero_panic_msg(%rip), %rdi
	jmp	call_panic		/* setup stack and call panic */
0:
#endif
	ALTENTRY(bzero_altentry)
do_zero:
#define	L(s) .bzero/**/s
	xorl	%eax, %eax

	cmpq	$0x50, %rsi		/* 80 */
	jge	L(ck_align)

	/*
	 * Performance data shows many caller's are zeroing small buffers. So
	 * for best perf for these sizes unrolled code is used. Store zeros
	 * without worrying about alignment.
	 */
	leaq	L(setPxQx)(%rip), %r10
	addq	%rsi, %rdi
	movslq	(%r10,%rsi,4), %rcx
	leaq	(%rcx,%r10,1), %r10
	jmpq	*%r10

	.p2align 4
L(setPxQx):
	.int       L(P0Q0)-L(setPxQx)	/* 0 */
	.int       L(P1Q0)-L(setPxQx)
	.int       L(P2Q0)-L(setPxQx)
	.int       L(P3Q0)-L(setPxQx)
	.int       L(P4Q0)-L(setPxQx)
	.int       L(P5Q0)-L(setPxQx)
	.int       L(P6Q0)-L(setPxQx)
	.int       L(P7Q0)-L(setPxQx) 

	.int       L(P0Q1)-L(setPxQx)	/* 8 */
	.int       L(P1Q1)-L(setPxQx)
	.int       L(P2Q1)-L(setPxQx)
	.int       L(P3Q1)-L(setPxQx)
	.int       L(P4Q1)-L(setPxQx)
	.int       L(P5Q1)-L(setPxQx)
	.int       L(P6Q1)-L(setPxQx)
	.int       L(P7Q1)-L(setPxQx) 

	.int       L(P0Q2)-L(setPxQx)	/* 16 */
	.int       L(P1Q2)-L(setPxQx)
	.int       L(P2Q2)-L(setPxQx)
	.int       L(P3Q2)-L(setPxQx)
	.int       L(P4Q2)-L(setPxQx)
	.int       L(P5Q2)-L(setPxQx)
	.int       L(P6Q2)-L(setPxQx)
	.int       L(P7Q2)-L(setPxQx) 

	.int       L(P0Q3)-L(setPxQx)	/* 24 */
	.int       L(P1Q3)-L(setPxQx)
	.int       L(P2Q3)-L(setPxQx)
	.int       L(P3Q3)-L(setPxQx)
	.int       L(P4Q3)-L(setPxQx)
	.int       L(P5Q3)-L(setPxQx)
	.int       L(P6Q3)-L(setPxQx)
	.int       L(P7Q3)-L(setPxQx) 

	.int       L(P0Q4)-L(setPxQx)	/* 32 */
	.int       L(P1Q4)-L(setPxQx)
	.int       L(P2Q4)-L(setPxQx)
	.int       L(P3Q4)-L(setPxQx)
	.int       L(P4Q4)-L(setPxQx)
	.int       L(P5Q4)-L(setPxQx)
	.int       L(P6Q4)-L(setPxQx)
	.int       L(P7Q4)-L(setPxQx) 

	.int       L(P0Q5)-L(setPxQx)	/* 40 */
	.int       L(P1Q5)-L(setPxQx)
	.int       L(P2Q5)-L(setPxQx)
	.int       L(P3Q5)-L(setPxQx)
	.int       L(P4Q5)-L(setPxQx)
	.int       L(P5Q5)-L(setPxQx)
	.int       L(P6Q5)-L(setPxQx)
	.int       L(P7Q5)-L(setPxQx) 

	.int       L(P0Q6)-L(setPxQx)	/* 48 */
	.int       L(P1Q6)-L(setPxQx)
	.int       L(P2Q6)-L(setPxQx)
	.int       L(P3Q6)-L(setPxQx)
	.int       L(P4Q6)-L(setPxQx)
	.int       L(P5Q6)-L(setPxQx)
	.int       L(P6Q6)-L(setPxQx)
	.int       L(P7Q6)-L(setPxQx) 

	.int       L(P0Q7)-L(setPxQx)	/* 56 */
	.int       L(P1Q7)-L(setPxQx)
	.int       L(P2Q7)-L(setPxQx)
	.int       L(P3Q7)-L(setPxQx)
	.int       L(P4Q7)-L(setPxQx)
	.int       L(P5Q7)-L(setPxQx)
	.int       L(P6Q7)-L(setPxQx)
	.int       L(P7Q7)-L(setPxQx) 

	.int       L(P0Q8)-L(setPxQx)	/* 64 */
	.int       L(P1Q8)-L(setPxQx)
	.int       L(P2Q8)-L(setPxQx)
	.int       L(P3Q8)-L(setPxQx)
	.int       L(P4Q8)-L(setPxQx)
	.int       L(P5Q8)-L(setPxQx)
	.int       L(P6Q8)-L(setPxQx)
	.int       L(P7Q8)-L(setPxQx)

	.int       L(P0Q9)-L(setPxQx)	/* 72 */
	.int       L(P1Q9)-L(setPxQx)
	.int       L(P2Q9)-L(setPxQx)
	.int       L(P3Q9)-L(setPxQx)
	.int       L(P4Q9)-L(setPxQx)
	.int       L(P5Q9)-L(setPxQx)
	.int       L(P6Q9)-L(setPxQx)
	.int       L(P7Q9)-L(setPxQx)	/* 79 */

	.p2align 4
L(P0Q9): mov    %rax, -0x48(%rdi)
L(P0Q8): mov    %rax, -0x40(%rdi)
L(P0Q7): mov    %rax, -0x38(%rdi)
L(P0Q6): mov    %rax, -0x30(%rdi)
L(P0Q5): mov    %rax, -0x28(%rdi)
L(P0Q4): mov    %rax, -0x20(%rdi)
L(P0Q3): mov    %rax, -0x18(%rdi)
L(P0Q2): mov    %rax, -0x10(%rdi)
L(P0Q1): mov    %rax, -0x8(%rdi)
L(P0Q0): 
	 ret

	.p2align 4
L(P1Q9): mov    %rax, -0x49(%rdi)
L(P1Q8): mov    %rax, -0x41(%rdi)
L(P1Q7): mov    %rax, -0x39(%rdi)
L(P1Q6): mov    %rax, -0x31(%rdi)
L(P1Q5): mov    %rax, -0x29(%rdi)
L(P1Q4): mov    %rax, -0x21(%rdi)
L(P1Q3): mov    %rax, -0x19(%rdi)
L(P1Q2): mov    %rax, -0x11(%rdi)
L(P1Q1): mov    %rax, -0x9(%rdi)
L(P1Q0): mov    %al, -0x1(%rdi)
	 ret

	.p2align 4
L(P2Q9): mov    %rax, -0x4a(%rdi)
L(P2Q8): mov    %rax, -0x42(%rdi)
L(P2Q7): mov    %rax, -0x3a(%rdi)
L(P2Q6): mov    %rax, -0x32(%rdi)
L(P2Q5): mov    %rax, -0x2a(%rdi)
L(P2Q4): mov    %rax, -0x22(%rdi)
L(P2Q3): mov    %rax, -0x1a(%rdi)
L(P2Q2): mov    %rax, -0x12(%rdi)
L(P2Q1): mov    %rax, -0xa(%rdi)
L(P2Q0): mov    %ax, -0x2(%rdi)
	 ret

	.p2align 4
L(P3Q9): mov    %rax, -0x4b(%rdi)
L(P3Q8): mov    %rax, -0x43(%rdi)
L(P3Q7): mov    %rax, -0x3b(%rdi)
L(P3Q6): mov    %rax, -0x33(%rdi)
L(P3Q5): mov    %rax, -0x2b(%rdi)
L(P3Q4): mov    %rax, -0x23(%rdi)
L(P3Q3): mov    %rax, -0x1b(%rdi)
L(P3Q2): mov    %rax, -0x13(%rdi)
L(P3Q1): mov    %rax, -0xb(%rdi)
L(P3Q0): mov    %ax, -0x3(%rdi)
	 mov    %al, -0x1(%rdi)
	 ret

	.p2align 4
L(P4Q9): mov    %rax, -0x4c(%rdi)
L(P4Q8): mov    %rax, -0x44(%rdi)
L(P4Q7): mov    %rax, -0x3c(%rdi)
L(P4Q6): mov    %rax, -0x34(%rdi)
L(P4Q5): mov    %rax, -0x2c(%rdi)
L(P4Q4): mov    %rax, -0x24(%rdi)
L(P4Q3): mov    %rax, -0x1c(%rdi)
L(P4Q2): mov    %rax, -0x14(%rdi)
L(P4Q1): mov    %rax, -0xc(%rdi)
L(P4Q0): mov    %eax, -0x4(%rdi)
	 ret

	.p2align 4
L(P5Q9): mov    %rax, -0x4d(%rdi)
L(P5Q8): mov    %rax, -0x45(%rdi)
L(P5Q7): mov    %rax, -0x3d(%rdi)
L(P5Q6): mov    %rax, -0x35(%rdi)
L(P5Q5): mov    %rax, -0x2d(%rdi)
L(P5Q4): mov    %rax, -0x25(%rdi)
L(P5Q3): mov    %rax, -0x1d(%rdi)
L(P5Q2): mov    %rax, -0x15(%rdi)
L(P5Q1): mov    %rax, -0xd(%rdi)
L(P5Q0): mov    %eax, -0x5(%rdi)
	 mov    %al, -0x1(%rdi)
	 ret

	.p2align 4
L(P6Q9): mov    %rax, -0x4e(%rdi)
L(P6Q8): mov    %rax, -0x46(%rdi)
L(P6Q7): mov    %rax, -0x3e(%rdi)
L(P6Q6): mov    %rax, -0x36(%rdi)
L(P6Q5): mov    %rax, -0x2e(%rdi)
L(P6Q4): mov    %rax, -0x26(%rdi)
L(P6Q3): mov    %rax, -0x1e(%rdi)
L(P6Q2): mov    %rax, -0x16(%rdi)
L(P6Q1): mov    %rax, -0xe(%rdi)
L(P6Q0): mov    %eax, -0x6(%rdi)
	 mov    %ax, -0x2(%rdi)
	 ret

	.p2align 4
L(P7Q9): mov    %rax, -0x4f(%rdi)
L(P7Q8): mov    %rax, -0x47(%rdi)
L(P7Q7): mov    %rax, -0x3f(%rdi)
L(P7Q6): mov    %rax, -0x37(%rdi)
L(P7Q5): mov    %rax, -0x2f(%rdi)
L(P7Q4): mov    %rax, -0x27(%rdi)
L(P7Q3): mov    %rax, -0x1f(%rdi)
L(P7Q2): mov    %rax, -0x17(%rdi)
L(P7Q1): mov    %rax, -0xf(%rdi)
L(P7Q0): mov    %eax, -0x7(%rdi)
	 mov    %ax, -0x3(%rdi)
	 mov    %al, -0x1(%rdi)
	 ret

	/*
	 * Align to a 16-byte boundary. Avoids penalties from unaligned stores
	 * as well as from stores spanning cachelines. Note 16-byte alignment
	 * is better in case where rep sstosq is used.
	 */
	.p2align 4
L(ck_align):
	test	$0xf, %rdi
	jz	L(aligned_now)
	test	$1, %rdi
	jz	2f
	mov	%al, (%rdi)
	dec	%rsi
	lea	1(%rdi),%rdi
2:
	test	$2, %rdi
	jz	4f
	mov	%ax, (%rdi)
	sub	$2, %rsi
	lea	2(%rdi),%rdi
4:
	test	$4, %rdi
	jz	8f
	mov	%eax, (%rdi)
	sub	$4, %rsi
	lea	4(%rdi),%rdi
8:
	test	$8, %rdi
	jz	L(aligned_now)
	mov	%rax, (%rdi)
	sub	$8, %rsi
	lea	8(%rdi),%rdi

	/*
	 * For large sizes rep sstoq is fastest.
	 * Transition point determined experimentally as measured on
	 * Intel Xeon processors (incl. Nehalem) and AMD Opteron.
	 */
L(aligned_now):
	cmp	$BZERO_USE_REP, %rsi
	jg	L(use_rep)

	/*
	 * zero 64-bytes per loop
	 */
	.p2align 4
L(bzero_loop):
	leaq	-0x40(%rsi), %rsi
	cmpq	$0x40, %rsi
	movq	%rax, (%rdi) 
	movq	%rax, 0x8(%rdi) 
	movq	%rax, 0x10(%rdi) 
	movq	%rax, 0x18(%rdi) 
	movq	%rax, 0x20(%rdi) 
	movq	%rax, 0x28(%rdi) 
	movq	%rax, 0x30(%rdi) 
	movq	%rax, 0x38(%rdi) 
	leaq	0x40(%rdi), %rdi
	jge	L(bzero_loop)

	/*
	 * Clear any remaining bytes..
	 */
9:
	leaq	L(setPxQx)(%rip), %r10
	addq	%rsi, %rdi
	movslq	(%r10,%rsi,4), %rcx
	leaq	(%rcx,%r10,1), %r10
	jmpq	*%r10

	/*
	 * Use rep sstoq. Clear any remainder via unrolled code
	 */
	.p2align 4
L(use_rep):
	movq	%rsi, %rcx		/* get size in bytes */
	shrq	$3, %rcx		/* count of 8-byte words to zero */
	rep
	  sstoq				/* %rcx = words to clear (%rax=0) */
	andq	$7, %rsi		/* remaining bytes */
	jnz	9b
	ret
#undef	L
	SET_SIZE(bzero_altentry)
	SET_SIZE(bzero)

#endif	/* __lint */

/*
 * Transfer data to and from user space -
 * Note that these routines can cause faults
 * It is assumed that the kernel has nothing at
 * less than KERNELBASE in the virtual address space.
 *
 * Note that copyin(9F) and copyout(9F) are part of the
 * DDI/DKI which specifies that they return '-1' on "errors."
 *
 * Sigh.
 *
 * So there's two extremely similar routines - xcopyin_nta() and
 * xcopyout_nta() which return the errno that we've faithfully computed.
 * This allows other callers (e.g. uiomove(9F)) to work correctly.
 * Given that these are used pretty heavily, we expand the calling
 * sequences inline for all flavours (rather than making wrappers).
 */

/*
 * Copy user data to kernel space.
 */

#if defined(__lint)

/* ARGSUSED */
int
copyin(const void *uaddr, void *kaddr, size_t count)
{ return (0); }

#else	/* lint */

	ENTRY(copyin)
	pushq	%rbp
	movq	%rsp, %rbp
	subq	$24, %rsp

	/*
	 * save args in case we trap and need to rerun as a copyop
	 */
	movq	%rdi, (%rsp)
	movq	%rsi, 0x8(%rsp)
	movq	%rdx, 0x10(%rsp)

	movq	kernelbase(%rip), %rax
#ifdef DEBUG
	cmpq	%rax, %rsi		/* %rsi = kaddr */
	jnb	1f
	leaq	.copyin_panic_msg(%rip), %rdi
	xorl	%eax, %eax
	call	panic
1:
#endif
	/*
	 * pass lofault value as 4th argument to do_copy_fault
	 */
	leaq	_copyin_err(%rip), %rcx

	movq	%gs:CPU_THREAD, %r9
	cmpq	%rax, %rdi		/* test uaddr < kernelbase */
	jb	do_copy_fault
	jmp	3f

_copyin_err:
	movq	%r11, T_LOFAULT(%r9)	/* restore original lofault */	
	addq	$8, %rsp		/* pop bcopy_altentry call ret addr */
3:
	movq	T_COPYOPS(%r9), %rax
	cmpq	$0, %rax
	jz	2f
	/*
	 * reload args for the copyop
	 */
	movq	(%rsp), %rdi
	movq	0x8(%rsp), %rsi
	movq	0x10(%rsp), %rdx
	leave
	jmp	*CP_COPYIN(%rax)

2:	movl	$-1, %eax	
	leave
	ret
	SET_SIZE(copyin)

#endif	/* __lint */

#if defined(__lint)

/* ARGSUSED */
int
xcopyin_nta(const void *uaddr, void *kaddr, size_t count, int copy_cached)
{ return (0); }

#else	/* __lint */

	ENTRY(xcopyin_nta)
	pushq	%rbp
	movq	%rsp, %rbp
	subq	$24, %rsp

	/*
	 * save args in case we trap and need to rerun as a copyop
	 * %rcx is consumed in this routine so we don't need to save
	 * it.
	 */
	movq	%rdi, (%rsp)
	movq	%rsi, 0x8(%rsp)
	movq	%rdx, 0x10(%rsp)

	movq	kernelbase(%rip), %rax
#ifdef DEBUG
	cmpq	%rax, %rsi		/* %rsi = kaddr */
	jnb	1f
	leaq	.xcopyin_panic_msg(%rip), %rdi
	xorl	%eax, %eax
	call	panic
1:
#endif
	movq	%gs:CPU_THREAD, %r9
	cmpq	%rax, %rdi		/* test uaddr < kernelbase */
	jae	4f
	cmpq	$0, %rcx		/* No non-temporal access? */
	/*
	 * pass lofault value as 4th argument to do_copy_fault
	 */
	leaq	_xcopyin_err(%rip), %rcx	/* doesn't set rflags */
	jnz	do_copy_fault		/* use regular access */
	/*
	 * Make sure cnt is >= XCOPY_MIN_SIZE bytes
	 */
	cmpq	$XCOPY_MIN_SIZE, %rdx
	jb	do_copy_fault
	
	/*
	 * Make sure src and dst are NTA_ALIGN_SIZE aligned,
	 * count is COUNT_ALIGN_SIZE aligned.
	 */
	movq	%rdi, %r10
	orq	%rsi, %r10
	andq	$NTA_ALIGN_MASK, %r10
	orq	%rdx, %r10
	andq	$COUNT_ALIGN_MASK, %r10
	jnz	do_copy_fault
	leaq	_xcopyin_nta_err(%rip), %rcx	/* doesn't set rflags */
	jmp	do_copy_fault_nta	/* use non-temporal access */
	
4:
	movl	$EFAULT, %eax
	jmp	3f

	/*
	 * A fault during do_copy_fault or do_copy_fault_nta is
	 * indicated through an errno value in %rax and we iret from the
	 * trap handler to here.
	 */
_xcopyin_err:
	addq	$8, %rsp		/* pop bcopy_altentry call ret addr */
_xcopyin_nta_err:
	movq	%r11, T_LOFAULT(%r9)	/* restore original lofault */
3:
	movq	T_COPYOPS(%r9), %r8
	cmpq	$0, %r8
	jz	2f

	/*
	 * reload args for the copyop
	 */
	movq	(%rsp), %rdi
	movq	0x8(%rsp), %rsi
	movq	0x10(%rsp), %rdx
	leave
	jmp	*CP_XCOPYIN(%r8)

2:	leave
	ret
	SET_SIZE(xcopyin_nta)

#endif	/* __lint */

/*
 * Copy kernel data to user space.
 */

#if defined(__lint)

/* ARGSUSED */
int
copyout(const void *kaddr, void *uaddr, size_t count)
{ return (0); }

#else	/* __lint */

	ENTRY(copyout)
	pushq	%rbp
	movq	%rsp, %rbp
	subq	$24, %rsp

	/*
	 * save args in case we trap and need to rerun as a copyop
	 */
	movq	%rdi, (%rsp)
	movq	%rsi, 0x8(%rsp)
	movq	%rdx, 0x10(%rsp)

	movq	kernelbase(%rip), %rax
#ifdef DEBUG
	cmpq	%rax, %rdi		/* %rdi = kaddr */
	jnb	1f
	leaq	.copyout_panic_msg(%rip), %rdi
	xorl	%eax, %eax
	call	panic
1:
#endif
	/*
	 * pass lofault value as 4th argument to do_copy_fault
	 */
	leaq	_copyout_err(%rip), %rcx

	movq	%gs:CPU_THREAD, %r9
	cmpq	%rax, %rsi		/* test uaddr < kernelbase */
	jb	do_copy_fault
	jmp	3f

_copyout_err:
	movq	%r11, T_LOFAULT(%r9)	/* restore original lofault */
	addq	$8, %rsp		/* pop bcopy_altentry call ret addr */
3:
	movq	T_COPYOPS(%r9), %rax
	cmpq	$0, %rax
	jz	2f

	/*
	 * reload args for the copyop
	 */
	movq	(%rsp), %rdi
	movq	0x8(%rsp), %rsi
	movq	0x10(%rsp), %rdx
	leave
	jmp	*CP_COPYOUT(%rax)

2:	movl	$-1, %eax
	leave
	ret
	SET_SIZE(copyout)

#endif	/* __lint */

#if defined(__lint)

/* ARGSUSED */
int
xcopyout_nta(const void *kaddr, void *uaddr, size_t count, int copy_cached)
{ return (0); }

#else	/* __lint */

	ENTRY(xcopyout_nta)
	pushq	%rbp
	movq	%rsp, %rbp
	subq	$24, %rsp

	/*
	 * save args in case we trap and need to rerun as a copyop
	 */
	movq	%rdi, (%rsp)
	movq	%rsi, 0x8(%rsp)
	movq	%rdx, 0x10(%rsp)

	movq	kernelbase(%rip), %rax
#ifdef DEBUG
	cmpq	%rax, %rdi		/* %rdi = kaddr */
	jnb	1f
	leaq	.xcopyout_panic_msg(%rip), %rdi
	xorl	%eax, %eax
	call	panic
1:
#endif
	movq	%gs:CPU_THREAD, %r9
	cmpq	%rax, %rsi		/* test uaddr < kernelbase */
	jae	4f

	cmpq	$0, %rcx		/* No non-temporal access? */
	/*
	 * pass lofault value as 4th argument to do_copy_fault
	 */
	leaq	_xcopyout_err(%rip), %rcx
	jnz	do_copy_fault
	/*
	 * Make sure cnt is >= XCOPY_MIN_SIZE bytes
	 */
	cmpq	$XCOPY_MIN_SIZE, %rdx
	jb	do_copy_fault
	
	/*
	 * Make sure src and dst are NTA_ALIGN_SIZE aligned,
	 * count is COUNT_ALIGN_SIZE aligned.
	 */
	movq	%rdi, %r10
	orq	%rsi, %r10
	andq	$NTA_ALIGN_MASK, %r10
	orq	%rdx, %r10
	andq	$COUNT_ALIGN_MASK, %r10
	jnz	do_copy_fault
	leaq	_xcopyout_nta_err(%rip), %rcx
	jmp	do_copy_fault_nta

4:
	movl	$EFAULT, %eax
	jmp	3f

	/*
	 * A fault during do_copy_fault or do_copy_fault_nta is
	 * indicated through an errno value in %rax and we iret from the
	 * trap handler to here.
	 */
_xcopyout_err:
	addq	$8, %rsp		/* pop bcopy_altentry call ret addr */
_xcopyout_nta_err:
	movq	%r11, T_LOFAULT(%r9)	/* restore original lofault */
3:
	movq	T_COPYOPS(%r9), %r8
	cmpq	$0, %r8
	jz	2f

	/*
	 * reload args for the copyop
	 */
	movq	(%rsp), %rdi
	movq	0x8(%rsp), %rsi
	movq	0x10(%rsp), %rdx
	leave
	jmp	*CP_XCOPYOUT(%r8)

2:	leave
	ret
	SET_SIZE(xcopyout_nta)

#endif	/* __lint */

/*
 * Copy a null terminated string from one point to another in
 * the kernel address space.
 */

#if defined(__lint)

/* ARGSUSED */
int
copystr(const char *from, char *to, size_t maxlength, size_t *lencopied)
{ return (0); }

#else	/* __lint */

	ENTRY(copystr)
	pushq	%rbp
	movq	%rsp, %rbp
#ifdef DEBUG
	movq	kernelbase(%rip), %rax
	cmpq	%rax, %rdi		/* %rdi = from */
	jb	0f
	cmpq	%rax, %rsi		/* %rsi = to */
	jnb	1f
0:	leaq	.copystr_panic_msg(%rip), %rdi
	xorl	%eax, %eax
	call	panic
1:
#endif
	movq	%gs:CPU_THREAD, %r9
	movq	T_LOFAULT(%r9), %r8	/* pass current lofault value as */
					/* 5th argument to do_copystr */
do_copystr:
	movq	%gs:CPU_THREAD, %r9	/* %r9 = thread addr */
	movq    T_LOFAULT(%r9), %r11	/* save the current lofault */
	movq	%r8, T_LOFAULT(%r9)	/* new lofault */

	movq	%rdx, %r8		/* save maxlength */

	cmpq	$0, %rdx		/* %rdx = maxlength */
	je	copystr_enametoolong	/* maxlength == 0 */

copystr_loop:
	decq	%r8
	movb	(%rdi), %al
	incq	%rdi
	movb	%al, (%rsi)
	incq	%rsi
	cmpb	$0, %al
	je	copystr_null		/* null char */
	cmpq	$0, %r8
	jne	copystr_loop

copystr_enametoolong:
	movl	$ENAMETOOLONG, %eax
	jmp	copystr_out

copystr_null:
	xorl	%eax, %eax		/* no error */

copystr_out:
	cmpq	$0, %rcx		/* want length? */
	je	copystr_done		/* no */
	subq	%r8, %rdx		/* compute length and store it */
	movq	%rdx, (%rcx)

copystr_done:
	movq	%r11, T_LOFAULT(%r9)	/* restore the original lofault */
	leave
	ret
	SET_SIZE(copystr)

#endif	/* __lint */

/*
 * Copy a null terminated string from the user address space into
 * the kernel address space.
 */

#if defined(__lint)

/* ARGSUSED */
int
copyinstr(const char *uaddr, char *kaddr, size_t maxlength,
    size_t *lencopied)
{ return (0); }

#else	/* __lint */

	ENTRY(copyinstr)
	pushq	%rbp
	movq	%rsp, %rbp
	subq	$32, %rsp

	/*
	 * save args in case we trap and need to rerun as a copyop
	 */
	movq	%rdi, (%rsp)
	movq	%rsi, 0x8(%rsp)
	movq	%rdx, 0x10(%rsp)
	movq	%rcx, 0x18(%rsp)

	movq	kernelbase(%rip), %rax
#ifdef DEBUG
	cmpq	%rax, %rsi		/* %rsi = kaddr */
	jnb	1f
	leaq	.copyinstr_panic_msg(%rip), %rdi
	xorl	%eax, %eax
	call	panic
1:
#endif
	/*
	 * pass lofault value as 5th argument to do_copystr
	 */
	leaq	_copyinstr_error(%rip), %r8

	cmpq	%rax, %rdi		/* test uaddr < kernelbase */
	jb	do_copystr
	movq	%gs:CPU_THREAD, %r9
	jmp	3f

_copyinstr_error:
	movq	%r11, T_LOFAULT(%r9)	/* restore original lofault */
3:
	movq	T_COPYOPS(%r9), %rax
	cmpq	$0, %rax
	jz	2f

	/*
	 * reload args for the copyop
	 */
	movq	(%rsp), %rdi
	movq	0x8(%rsp), %rsi
	movq	0x10(%rsp), %rdx
	movq	0x18(%rsp), %rcx
	leave
	jmp	*CP_COPYINSTR(%rax)
	
2:	movl	$EFAULT, %eax		/* return EFAULT */
	leave
	ret
	SET_SIZE(copyinstr)

#endif	/* __lint */

/*
 * Copy a null terminated string from the kernel
 * address space to the user address space.
 */

#if defined(__lint)

/* ARGSUSED */
int
copyoutstr(const char *kaddr, char *uaddr, size_t maxlength,
    size_t *lencopied)
{ return (0); }

#else	/* __lint */

	ENTRY(copyoutstr)
	pushq	%rbp
	movq	%rsp, %rbp
	subq	$32, %rsp

	/*
	 * save args in case we trap and need to rerun as a copyop
	 */
	movq	%rdi, (%rsp)
	movq	%rsi, 0x8(%rsp)
	movq	%rdx, 0x10(%rsp)
	movq	%rcx, 0x18(%rsp)

	movq	kernelbase(%rip), %rax
#ifdef DEBUG
	cmpq	%rax, %rdi		/* %rdi = kaddr */
	jnb	1f
	leaq	.copyoutstr_panic_msg(%rip), %rdi
	jmp	call_panic		/* setup stack and call panic */
1:
#endif
	/*
	 * pass lofault value as 5th argument to do_copystr
	 */
	leaq	_copyoutstr_error(%rip), %r8

	cmpq	%rax, %rsi		/* test uaddr < kernelbase */
	jb	do_copystr
	movq	%gs:CPU_THREAD, %r9
	jmp	3f

_copyoutstr_error:
	movq	%r11, T_LOFAULT(%r9)	/* restore the original lofault */
3:
	movq	T_COPYOPS(%r9), %rax
	cmpq	$0, %rax
	jz	2f

	/*
	 * reload args for the copyop
	 */
	movq	(%rsp), %rdi
	movq	0x8(%rsp), %rsi
	movq	0x10(%rsp), %rdx
	movq	0x18(%rsp), %rcx
	leave
	jmp	*CP_COPYOUTSTR(%rax)
	
2:	movl	$EFAULT, %eax		/* return EFAULT */
	leave
	ret
	SET_SIZE(copyoutstr)	
	
#endif	/* __lint */

/*
 * Since all of the fuword() variants are so similar, we have a macro to spit
 * them out.  This allows us to create DTrace-unobservable functions easily.
 */
	
#if defined(__lint)

/* ARGSUSED */
int
fuword64(const void *addr, uint64_t *dst)
{ return (0); }

/* ARGSUSED */
int
fuword32(const void *addr, uint32_t *dst)
{ return (0); }

/* ARGSUSED */
int
fuword16(const void *addr, uint16_t *dst)
{ return (0); }

/* ARGSUSED */
int
fuword8(const void *addr, uint8_t *dst)
{ return (0); }

#else	/* __lint */

/*
 * (Note that we don't save and reload the arguments here
 * because their values are not altered in the copy path)
 */

#define	FUWORD(NAME, INSTR, REG, COPYOP)	\
	ENTRY(NAME)				\
	movq	%gs:CPU_THREAD, %r9;		\
	cmpq	kernelbase(%rip), %rdi;		\
	jae	1f;				\
	leaq	_flt_/**/NAME, %rdx;		\
	movq	%rdx, T_LOFAULT(%r9);		\
	INSTR	(%rdi), REG;			\
	movq	$0, T_LOFAULT(%r9);		\
	INSTR	REG, (%rsi);			\
	xorl	%eax, %eax;			\
	ret;					\
_flt_/**/NAME:					\
	movq	$0, T_LOFAULT(%r9);		\
1:						\
	movq	T_COPYOPS(%r9), %rax;		\
	cmpq	$0, %rax;			\
	jz	2f;				\
	jmp	*COPYOP(%rax);			\
2:						\
	movl	$-1, %eax;			\
	ret;					\
	SET_SIZE(NAME)
	
	FUWORD(fuword64, movq, %rax, CP_FUWORD64)
	FUWORD(fuword32, movl, %eax, CP_FUWORD32)
	FUWORD(fuword16, movw, %ax, CP_FUWORD16)
	FUWORD(fuword8, movb, %al, CP_FUWORD8)

#undef	FUWORD

#endif	/* __lint */

/*
 * Set user word.
 */

#if defined(__lint)

/* ARGSUSED */
int
suword64(void *addr, uint64_t value)
{ return (0); }

/* ARGSUSED */
int
suword32(void *addr, uint32_t value)
{ return (0); }

/* ARGSUSED */
int
suword16(void *addr, uint16_t value)
{ return (0); }

/* ARGSUSED */
int
suword8(void *addr, uint8_t value)
{ return (0); }

#else	/* lint */

/*
 * (Note that we don't save and reload the arguments here
 * because their values are not altered in the copy path)
 */

#define	SUWORD(NAME, INSTR, REG, COPYOP)	\
	ENTRY(NAME)				\
	movq	%gs:CPU_THREAD, %r9;		\
	cmpq	kernelbase(%rip), %rdi;		\
	jae	1f;				\
	leaq	_flt_/**/NAME, %rdx;		\
	movq	%rdx, T_LOFAULT(%r9);		\
	INSTR	REG, (%rdi);			\
	movq	$0, T_LOFAULT(%r9);		\
	xorl	%eax, %eax;			\
	ret;					\
_flt_/**/NAME:					\
	movq	$0, T_LOFAULT(%r9);		\
1:						\
	movq	T_COPYOPS(%r9), %rax;		\
	cmpq	$0, %rax;			\
	jz	3f;				\
	jmp	*COPYOP(%rax);			\
3:						\
	movl	$-1, %eax;			\
	ret;					\
	SET_SIZE(NAME)

	SUWORD(suword64, movq, %rsi, CP_SUWORD64)
	SUWORD(suword32, movl, %esi, CP_SUWORD32)
	SUWORD(suword16, movw, %si, CP_SUWORD16)
	SUWORD(suword8, movb, %sil, CP_SUWORD8)

#undef	SUWORD

#endif	/* __lint */

#if defined(__lint)

/*ARGSUSED*/
void
fuword64_noerr(const void *addr, uint64_t *dst)
{}

/*ARGSUSED*/
void
fuword32_noerr(const void *addr, uint32_t *dst)
{}

/*ARGSUSED*/
void
fuword8_noerr(const void *addr, uint8_t *dst)
{}

/*ARGSUSED*/
void
fuword16_noerr(const void *addr, uint16_t *dst)
{}

#else   /* __lint */

#define	FUWORD_NOERR(NAME, INSTR, REG)		\
	ENTRY(NAME)				\
	cmpq	kernelbase(%rip), %rdi;		\
	cmovnbq	kernelbase(%rip), %rdi;		\
	INSTR	(%rdi), REG;			\
	INSTR	REG, (%rsi);			\
	ret;					\
	SET_SIZE(NAME)

	FUWORD_NOERR(fuword64_noerr, movq, %rax)
	FUWORD_NOERR(fuword32_noerr, movl, %eax)
	FUWORD_NOERR(fuword16_noerr, movw, %ax)
	FUWORD_NOERR(fuword8_noerr, movb, %al)

#undef	FUWORD_NOERR

#endif	/* __lint */

#if defined(__lint)

/*ARGSUSED*/
void
suword64_noerr(void *addr, uint64_t value)
{}

/*ARGSUSED*/
void
suword32_noerr(void *addr, uint32_t value)
{}

/*ARGSUSED*/
void
suword16_noerr(void *addr, uint16_t value)
{}

/*ARGSUSED*/
void
suword8_noerr(void *addr, uint8_t value)
{}

#else	/* lint */

#define	SUWORD_NOERR(NAME, INSTR, REG)		\
	ENTRY(NAME)				\
	cmpq	kernelbase(%rip), %rdi;		\
	cmovnbq	kernelbase(%rip), %rdi;		\
	INSTR	REG, (%rdi);			\
	ret;					\
	SET_SIZE(NAME)

	SUWORD_NOERR(suword64_noerr, movq, %rsi)
	SUWORD_NOERR(suword32_noerr, movl, %esi)
	SUWORD_NOERR(suword16_noerr, movw, %si)
	SUWORD_NOERR(suword8_noerr, movb, %sil)

#undef	SUWORD_NOERR

#endif	/* lint */

#if defined(__lint)

/*ARGSUSED*/
int
subyte(void *addr, uchar_t value)
{ return (0); }

/*ARGSUSED*/
void
subyte_noerr(void *addr, uchar_t value)
{}

/*ARGSUSED*/
int
fulword(const void *addr, ulong_t *valuep)
{ return (0); }

/*ARGSUSED*/
void
fulword_noerr(const void *addr, ulong_t *valuep)
{}

/*ARGSUSED*/
int
sulword(void *addr, ulong_t valuep)
{ return (0); }

/*ARGSUSED*/
void
sulword_noerr(void *addr, ulong_t valuep)
{}

#else

	.weak	subyte
	subyte=suword8
	.weak	subyte_noerr
	subyte_noerr=suword8_noerr

	.weak	fulword
	fulword=fuword64
	.weak	fulword_noerr
	fulword_noerr=fuword64_noerr
	.weak	sulword
	sulword=suword64
	.weak	sulword_noerr
	sulword_noerr=suword64_noerr

#endif /* __lint */

#if defined(__lint)

/*
 * Copy a block of storage - must not overlap (from + len <= to).
 * No fault handler installed (to be called under on_fault())
 */

/* ARGSUSED */
void
copyout_noerr(const void *kfrom, void *uto, size_t count)
{}

/* ARGSUSED */
void
copyin_noerr(const void *ufrom, void *kto, size_t count)
{}

/*
 * Zero a block of storage in user space
 */

/* ARGSUSED */
void
uzero(void *addr, size_t count)
{}

/*
 * copy a block of storage in user space
 */

/* ARGSUSED */
void
ucopy(const void *ufrom, void *uto, size_t ulength)
{}

/*
 * copy a string in user space
 */

/* ARGSUSED */
void
ucopystr(const char *ufrom, char *uto, size_t umaxlength, size_t *lencopied)
{}

#else /* __lint */

	ENTRY(copyin_noerr)
	movq	kernelbase(%rip), %rax
#ifdef DEBUG
	cmpq	%rax, %rsi		/* %rsi = kto */
	jae	1f
	leaq	.cpyin_ne_pmsg(%rip), %rdi
	jmp	call_panic		/* setup stack and call panic */
1:
#endif
	cmpq	%rax, %rdi		/* ufrom < kernelbase */
	jb	do_copy
	movq	%rax, %rdi		/* force fault at kernelbase */
	jmp	do_copy
	SET_SIZE(copyin_noerr)

	ENTRY(copyout_noerr)
	movq	kernelbase(%rip), %rax
#ifdef DEBUG
	cmpq	%rax, %rdi		/* %rdi = kfrom */
	jae	1f
	leaq	.cpyout_ne_pmsg(%rip), %rdi
	jmp	call_panic		/* setup stack and call panic */
1:
#endif
	cmpq	%rax, %rsi		/* uto < kernelbase */
	jb	do_copy
	movq	%rax, %rsi		/* force fault at kernelbase */
	jmp	do_copy
	SET_SIZE(copyout_noerr)

	ENTRY(uzero)
	movq	kernelbase(%rip), %rax
	cmpq	%rax, %rdi
	jb	do_zero
	movq	%rax, %rdi	/* force fault at kernelbase */
	jmp	do_zero
	SET_SIZE(uzero)

	ENTRY(ucopy)
	movq	kernelbase(%rip), %rax
	cmpq	%rax, %rdi
	cmovaeq	%rax, %rdi	/* force fault at kernelbase */
	cmpq	%rax, %rsi
	cmovaeq	%rax, %rsi	/* force fault at kernelbase */
	jmp	do_copy
	SET_SIZE(ucopy)

	ENTRY(ucopystr)
	movq	kernelbase(%rip), %rax
	cmpq	%rax, %rdi
	cmovaeq	%rax, %rdi	/* force fault at kernelbase */
	cmpq	%rax, %rsi
	cmovaeq	%rax, %rsi	/* force fault at kernelbase */
	/* do_copystr expects lofault address in %r8 */
	movq	%gs:CPU_THREAD, %r8
	movq	T_LOFAULT(%r8), %r8
	jmp	do_copystr
	SET_SIZE(ucopystr)

#ifdef DEBUG
	.data
.kcopy_panic_msg:
	.string "kcopy: arguments below kernelbase"
.bcopy_panic_msg:
	.string "bcopy: arguments below kernelbase"
.kzero_panic_msg:
        .string "kzero: arguments below kernelbase"
.bzero_panic_msg:
	.string	"bzero: arguments below kernelbase"
.copyin_panic_msg:
	.string "copyin: kaddr argument below kernelbase"
.xcopyin_panic_msg:
	.string	"xcopyin: kaddr argument below kernelbase"
.copyout_panic_msg:
	.string "copyout: kaddr argument below kernelbase"
.xcopyout_panic_msg:
	.string	"xcopyout: kaddr argument below kernelbase"
.copystr_panic_msg:
	.string	"copystr: arguments in user space"
.copyinstr_panic_msg:
	.string	"copyinstr: kaddr argument not in kernel address space"
.copyoutstr_panic_msg:
	.string	"copyoutstr: kaddr argument not in kernel address space"
.cpyin_ne_pmsg:
	.string "copyin_noerr: argument not in kernel address space"
.cpyout_ne_pmsg:
	.string "copyout_noerr: argument not in kernel address space"
#endif

#endif	/* __lint */
