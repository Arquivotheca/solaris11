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
 * Copyright (c) 2009 Intel Corporation
 * All Rights Reserved.
 */

/*
 * ====================================================================
 * Solaris OS modifications
 *
 * 1. Translated from C with embedded assembly using objdump -d.
 * 2. Added back comments and labels from original C source.
 * 3. Added empty C function definitions for lint.
 * 4. Added Solaris ENTRY_NP() and SET_SIZE() macros from sys/asm_linkage.h.
 * 5. Change length parameter from int (32-bit) to size_t (64-bit unsigned)
 * 6. Added CLEAR_TS_OR_PUSH_XMM_REGISTERS and
 *	SET_TS_OR_POP_XMM_REGISTERS macros to save/restore %xmm* registers
 *	(needed for kernel mode only--not needed for user mode libraries).
 * 7. Replaced code saving/restoring registers in red zone (negative offset
 *	of %rsp) into push and pop.
 *
 * Note: For kernel code, caller is responsible for ensuring
 * kpreempt_disable() has been called before calling functions defined here.
 * This is because %xmm registers are not saved/restored. The
 * CLEAR_TS_OR_PUSH_XMM_REGISTERS and SET_TS_OR_POP_XMM_REGISTERS macros
 * clear and set the CR0.TS bit on entry and exit, respectively, if TS is set
 * on entry.
 * Otherwise, if TS is not set, save and restore %xmm registers on the stack.
 */

#if defined(lint) || defined(__lint)
#include "aesni.h"

/* ARGSUSED */
void
aes128_ctr_asm(unsigned char *ks, unsigned char *p, unsigned char *c,
    size_t length, unsigned char *IV, unsigned char *feedback)
{
}

/* ARGSUSED */
void
aes192_ctr_asm(unsigned char *ks, unsigned char *p, unsigned char *c,
    size_t length, unsigned char *IV, unsigned char *feedback)
{
}

/* ARGSUSED */
void
aes256_ctr_asm(unsigned char *ks, unsigned char *p, unsigned char *c,
    size_t length, unsigned char *IV, unsigned char *feedback)
{
}

#else	/* lint */

#include <sys/asm_linkage.h>
#include <sys/controlregs.h>
#ifdef _KERNEL
#include <sys/machprivregs.h>
#endif

#ifdef _KERNEL
	/*
	 * Note: the CLTS macro clobbers P2 (%rsi) under i86xpv.  That is,
	 * it calls HYPERVISOR_fpu_taskswitch() which modifies %rsi when it
	 * uses it to pass P2 to syscall.
	 * This also occurs with the STTS macro, but we don't care if
	 * P2 (%rsi) is modified just before function exit.
	 * The CLTS and STTS macros push and pop P1 (%rdi) already.
	 */
#ifdef __xpv
#define	PROTECTED_CLTS \
	push	%rsi; \
	CLTS; \
	pop	%rsi
#else
#define	PROTECTED_CLTS \
	CLTS
#endif	/* __xpv */


	/*
	 * If CR0_TS is not set, align stack (with push %rbp) and push
	 * %xmm0 - %xmm10 on stack, otherwise clear CR0_TS
	 */
#define	CLEAR_TS_OR_PUSH_XMM_REGISTERS(tmpreg) \
	push	%rbp; \
	mov	%rsp, %rbp; \
	movq    %cr0, tmpreg; \
	testq	$CR0_TS, tmpreg; \
	jnz	1f; \
	and	$-XMM_ALIGN, %rsp; \
	sub	$[XMM_SIZE * 16], %rsp; \
	movaps	%xmm0, 240(%rsp); \
	movaps	%xmm1, 224(%rsp); \
	movaps	%xmm2, 208(%rsp); \
	movaps	%xmm3, 192(%rsp); \
	movaps	%xmm4, 176(%rsp); \
	movaps	%xmm5, 160(%rsp); \
	movaps	%xmm6, 144(%rsp); \
	movaps	%xmm7, 128(%rsp); \
	movaps	%xmm8, 112(%rsp); \
	movaps	%xmm9, 96(%rsp); \
	movaps	%xmm10, 80(%rsp); \
	movaps	%xmm11, 64(%rsp); \
	movaps	%xmm12, 48(%rsp); \
	movaps	%xmm13, 32(%rsp); \
	movaps	%xmm14, 16(%rsp); \
	movaps	%xmm15, (%rsp); \
	jmp	2f; \
1: \
	PROTECTED_CLTS; \
2:

	/*
	 * If CR0_TS was not set above, pop %xmm0 - %xmm10 off stack,
	 * otherwise set CR0_TS.
	 */
#define	SET_TS_OR_POP_XMM_REGISTERS(tmpreg) \
	testq	$CR0_TS, tmpreg; \
	jnz	1f; \
	movaps	(%rsp), %xmm15; \
	movaps	16(%rsp), %xmm14; \
	movaps	32(%rsp), %xmm13; \
	movaps	48(%rsp), %xmm12; \
	movaps	64(%rsp), %xmm11; \
	movaps	80(%rsp), %xmm10; \
	movaps	96(%rsp), %xmm9; \
	movaps	112(%rsp), %xmm8; \
	movaps	128(%rsp), %xmm7; \
	movaps	144(%rsp), %xmm6; \
	movaps	160(%rsp), %xmm5; \
	movaps	176(%rsp), %xmm4; \
	movaps	192(%rsp), %xmm3; \
	movaps	208(%rsp), %xmm2; \
	movaps	224(%rsp), %xmm1; \
	movaps	240(%rsp), %xmm0; \
	jmp	2f; \
1: \
	STTS(tmpreg); \
2: \
	mov	%rbp, %rsp; \
	pop	%rbp


#else
#define	PROTECTED_CLTS
#define	CLEAR_TS_OR_PUSH_XMM_REGISTERS(tmpreg)
#define	SET_TS_OR_POP_XMM_REGISTERS(tmpreg)
#endif	/* _KERNEL */


/*
 * void aes128_ctr_asm(unsigned char *ks, unsigned char *p, unsigned char *c,
 *	size_t length, unsigned char *IV, unsigned char *feedback)
 *
 * Parameters:
 * p1 rcx	holds the pointer to the key schedule
 * p2 rdx	holds the pointer to the source, p
 * p3 r8	holds the pointer to the destination, c
 * p4 r9	holds the length in bytes
 * p5 rax	holds the pointer to the IV (128-bit little-endian counter)
 * p6 r13	holds the pointer to the feedback (updated with IV-1)
 */
ENTRY_NP(aes128_ctr_asm)
	// Setup
	push   %r13
	push   %r14
	mov    %r9,%r13		// p6 feedback
	mov    %r8,%rax		// p5 IV
	mov    %rcx,%r9		// p4 64-bit length, in bytes
	mov    %rdx,%r8		// p3 destination, c
	mov    %rdi,%rcx	// p1 key schedule
	mov    %rsi,%rdx	// p2 source, p
	lea    .swap_mask(%rip),%r14
	CLEAR_TS_OR_PUSH_XMM_REGISTERS(%rdi)

	// we compute the number of four block units
	mov    %r9,%r11
	shr    $6,%r11

	// we compute the number of remaining complete blocks
	and    $0x3f,%r9
	mov    %r9,%r10
	shr    $4,%r10		// Convert from bytes to 16-byte blocks

	// we also compute the number of remaining bytes
	and    $0xf,%r9

	// we load the key schedule from memory
	movdqu (%rcx),%xmm5
	movdqu 0x10(%rcx),%xmm6
	movdqu 0x20(%rcx),%xmm7
	movdqu 0x30(%rcx),%xmm8
	movdqu 0x40(%rcx),%xmm9
	movdqu 0x50(%rcx),%xmm10
	movdqu 0x60(%rcx),%xmm11
	movdqu 0x70(%rcx),%xmm12
	movdqu 0x80(%rcx),%xmm13
	movdqu 0x90(%rcx),%xmm14
	movdqu 0xa0(%rcx),%xmm15

	// we load the IV-1 into the memory entry tagged "counter"
	movdqu (%rax),%xmm0
	mov    $1,%rax
	movq   %rax,%xmm4
	psubd  %xmm4,%xmm0
	movdqu %xmm0,0(%r13)

	// check if the four block length is zero
	cmp    $0,%r11
	je     2f	// .move_on_1

1:	// .four_block_loop:
	// we prepare the four counters
	movq   %rax,%xmm4
	movdqu 0(%r13),%xmm0
	paddd  %xmm4,%xmm0	// increment by 1
	movdqu %xmm0,%xmm1
	paddd  %xmm4,%xmm1	// increment by 1
	movdqu %xmm1,%xmm2
	paddd  %xmm4,%xmm2	// increment by 1
	movdqu %xmm2,%xmm3
	paddd  %xmm4,%xmm3	// increment by 1
	movdqu %xmm3,0(%r13)

	movdqu (%r14),%xmm4	// .swap_mask
	pshufb %xmm4,%xmm0
	pshufb %xmm4,%xmm1
	pshufb %xmm4,%xmm2
	pshufb %xmm4,%xmm3

	// first we xor with the first round key
	pxor   %xmm5,%xmm0
	pxor   %xmm5,%xmm1
	pxor   %xmm5,%xmm2
	pxor   %xmm5,%xmm3

	// round 1
	aesenc %xmm6,%xmm0
	aesenc %xmm6,%xmm1
	aesenc %xmm6,%xmm2
	aesenc %xmm6,%xmm3
	// round 2
	aesenc %xmm7,%xmm0
	aesenc %xmm7,%xmm1
	aesenc %xmm7,%xmm2
	aesenc %xmm7,%xmm3
	// round 3
	aesenc %xmm8,%xmm0
	aesenc %xmm8,%xmm1
	aesenc %xmm8,%xmm2
	aesenc %xmm8,%xmm3
	// round 4
	aesenc %xmm9,%xmm0
	aesenc %xmm9,%xmm1
	aesenc %xmm9,%xmm2
	aesenc %xmm9,%xmm3
	// round 5
	aesenc %xmm10,%xmm0
	aesenc %xmm10,%xmm1
	aesenc %xmm10,%xmm2
	aesenc %xmm10,%xmm3
	// round 6
	aesenc %xmm11,%xmm0
	aesenc %xmm11,%xmm1
	aesenc %xmm11,%xmm2
	aesenc %xmm11,%xmm3
	// round 7
	aesenc %xmm12,%xmm0
	aesenc %xmm12,%xmm1
	aesenc %xmm12,%xmm2
	aesenc %xmm12,%xmm3
	// round 8
	aesenc %xmm13,%xmm0
	aesenc %xmm13,%xmm1
	aesenc %xmm13,%xmm2
	aesenc %xmm13,%xmm3
	// round 9
	aesenc %xmm14,%xmm0
	aesenc %xmm14,%xmm1
	aesenc %xmm14,%xmm2
	aesenc %xmm14,%xmm3
	// round 10
	aesenclast %xmm15,%xmm0
	aesenclast %xmm15,%xmm1
	aesenclast %xmm15,%xmm2
	aesenclast %xmm15,%xmm3

	// we xor with the data
	movdqu (%rdx),%xmm4		// source is unaligned
	pxor   %xmm4,%xmm0
	movdqu 0x10(%rdx),%xmm4		// source is unaligned
	pxor   %xmm4,%xmm1
	movdqu 0x20(%rdx),%xmm4		// source is unaligned
	pxor   %xmm4,%xmm2
	movdqu 0x30(%rdx),%xmm4		// source is unaligned
	pxor   %xmm4,%xmm3

	// we save the data
	movdqu %xmm0,(%r8)
	movdqu %xmm1,0x10(%r8)
	movdqu %xmm2,0x20(%r8)
	movdqu %xmm3,0x30(%r8)

	// we update the data pointers and counter
	add    $0x40,%rdx
	add    $0x40,%r8
	dec    %r11
	jne    1b	// .four_block_loop

2:	// .move_on_1:
	cmp    $0,%r10
	je     4f	// .move_on_2

3:	// .single_block_loop:
	// we prepare the counter
	movq   %rax,%xmm4
	movdqu 0(%r13),%xmm0
	paddd  %xmm4,%xmm0	// increment by 1
	movdqu %xmm0,0(%r13)

	movdqu (%r14),%xmm4	// .swap_mask
	pshufb %xmm4,%xmm0
	// first we xor with the first round key
	pxor   %xmm5,%xmm0

	// 9 rounds to follow
	aesenc %xmm6,%xmm0
	aesenc %xmm7,%xmm0
	aesenc %xmm8,%xmm0
	aesenc %xmm9,%xmm0
	aesenc %xmm10,%xmm0
	aesenc %xmm11,%xmm0
	aesenc %xmm12,%xmm0
	aesenc %xmm13,%xmm0
	aesenc %xmm14,%xmm0
	// last round
	aesenclast %xmm15,%xmm0

	// we xor with the data
	movdqu (%rdx),%xmm4	// source is unaligned
	pxor   %xmm4,%xmm0

	// we save the data
	movdqu %xmm0,(%r8)

	// we update the data pointers and counter
	add    $0x10,%rdx
	add    $0x10,%r8
	dec    %r10
	jne    3b	// .single_block_loop

4:	// .move_on_2:
	cmp    $0,%r9
	je     7f	// .move_on_3

	xor    %rax,%rax
	pxor   %xmm1,%xmm1
	mov    %r9,%r10
	add    %r9,%rdx
	sub    $1,%rdx

5:	// .load_remaining_bytes:
	mov    (%rdx),%al
	movd   %eax,%xmm2
	pslldq $1,%xmm1
	por    %xmm2,%xmm1
	dec    %rdx
	dec    %r9
	jne    5b	// .load_remaining_bytes

	// we prepare the counter
	mov    $1,%rax
	movq   %rax,%xmm4
	movdqu 0(%r13),%xmm0
	paddd  %xmm4,%xmm0	// increment by 1

	movdqu (%r14),%xmm4	// .swap_mask
	pshufb %xmm4,%xmm0

	// we encrypt for the remaining bytes
	pxor   %xmm5,%xmm0
	aesenc %xmm6,%xmm0
	aesenc %xmm7,%xmm0
	aesenc %xmm8,%xmm0
	aesenc %xmm9,%xmm0
	aesenc %xmm10,%xmm0
	aesenc %xmm11,%xmm0
	aesenc %xmm12,%xmm0
	aesenc %xmm13,%xmm0
	aesenc %xmm14,%xmm0
	aesenclast %xmm15,%xmm0

	// the counter mode xor
	pxor   %xmm1,%xmm0

6:	// .store_remaining_bytes:
	movd   %xmm0,%eax
	mov    %al,(%r8)
	psrldq $1,%xmm0
	inc    %r8
	dec    %r10
	jne    6b	// .store_remaining_bytes

7:	// .move_on_3:
	SET_TS_OR_POP_XMM_REGISTERS(%rdi)
	pop    %r14
	pop    %r13
	retq
	SET_SIZE(aes128_ctr_asm)


/*
 * void aes192_ctr_asm(unsigned char *ks, unsigned char *p, unsigned char *c,
 *	size_t length, unsigned char *IV, unsigned char *feedback)
 *
 * Parameters:
 * p1 rcx	holds the pointer to the key schedule
 * p2 rdx	holds the pointer to the source, p
 * p3 r8	holds the pointer to the destination, c
 * p4 r9	holds the length in bytes
 * p5 rax	holds the pointer to the IV (128-bit little-endian counter)
 * p6 r13	holds the pointer to the feedback (updated with IV-1)
 */
ENTRY_NP(aes192_ctr_asm)
	// Setup
	push   %r13
	push   %r14
	mov    %r9,%r13         // p6 feedback
	mov    %r8,%rax         // p5 IV
	mov    %rcx,%r9         // p4 64-bit length, in bytes
	mov    %rdx,%r8         // p3 destination, c
	mov    %rdi,%rcx        // p1 key schedule
	mov    %rsi,%rdx        // p2 source, p
	lea    .swap_mask(%rip),%r14
	CLEAR_TS_OR_PUSH_XMM_REGISTERS(%rdi)

	// we compute the number of four block units
	mov    %r9,%r11
	shr    $6,%r11

	// we compute the number of remaining complete blocks
	and    $0x3f,%r9
	mov    %r9,%r10
	shr    $4,%r10		// Convert from bytes to 16-byte blocks

	// we also compute the number of remaining bytes
	and    $0xf,%r9

	// we load eleven keys of the key schedule from memory
	movdqu (%rcx),%xmm5
	movdqu 0x10(%rcx),%xmm6
	movdqu 0x20(%rcx),%xmm7
	movdqu 0x30(%rcx),%xmm8
	movdqu 0x50(%rcx),%xmm9
	movdqu 0x60(%rcx),%xmm10
	movdqu 0x70(%rcx),%xmm11
	movdqu 0x80(%rcx),%xmm12
	movdqu 0xa0(%rcx),%xmm13
	movdqu 0xb0(%rcx),%xmm14
	movdqu 0xc0(%rcx),%xmm15

	// we load the IV-1 into the memory entry tagged "counter"
	movdqu (%rax),%xmm0
	mov    $1,%rax
	movq   %rax,%xmm4
	psubd  %xmm4,%xmm0
	movdqu %xmm0,0(%r13)

	// check if the four block length is zero
	cmp    $0,%r11
	je     2f // .move_on_1

1:	// .four_block_loop:
	// we prepare the four counters
	movq   %rax,%xmm4
	movdqu 0(%r13),%xmm0
	paddd  %xmm4,%xmm0	// increment by 1
	movdqu %xmm0,%xmm1
	paddd  %xmm4,%xmm1	// increment by 1
	movdqu %xmm1,%xmm2
	paddd  %xmm4,%xmm2	// increment by 1
	movdqu %xmm2,%xmm3
	paddd  %xmm4,%xmm3	// increment by 1
	movdqu %xmm3,0(%r13)

	movdqu (%r14),%xmm4	// .swap_mask
	pshufb %xmm4,%xmm0
	pshufb %xmm4,%xmm1
	pshufb %xmm4,%xmm2
	pshufb %xmm4,%xmm3

	// first we xor with the first round key
	pxor   %xmm5,%xmm0
	pxor   %xmm5,%xmm1
	pxor   %xmm5,%xmm2
	pxor   %xmm5,%xmm3

	// round 1
	aesenc %xmm6,%xmm0
	aesenc %xmm6,%xmm1
	aesenc %xmm6,%xmm2
	aesenc %xmm6,%xmm3

	movdqu 0x40(%rcx),%xmm4
	// round 2
	aesenc %xmm7,%xmm0
	aesenc %xmm7,%xmm1
	aesenc %xmm7,%xmm2
	aesenc %xmm7,%xmm3
	// round 3
	aesenc %xmm8,%xmm0
	aesenc %xmm8,%xmm1
	aesenc %xmm8,%xmm2
	aesenc %xmm8,%xmm3
	// round 4
	aesenc %xmm4,%xmm0
	aesenc %xmm4,%xmm1
	aesenc %xmm4,%xmm2
	aesenc %xmm4,%xmm3
	// round 5
	aesenc %xmm9,%xmm0
	aesenc %xmm9,%xmm1
	aesenc %xmm9,%xmm2
	aesenc %xmm9,%xmm3
	// round 6
	aesenc %xmm10,%xmm0
	aesenc %xmm10,%xmm1
	aesenc %xmm10,%xmm2
	aesenc %xmm10,%xmm3

	movdqu 0x90(%rcx),%xmm4
	// round 7
	aesenc %xmm11,%xmm0
	aesenc %xmm11,%xmm1
	aesenc %xmm11,%xmm2
	aesenc %xmm11,%xmm3
	// round 8
	aesenc %xmm12,%xmm0
	aesenc %xmm12,%xmm1
	aesenc %xmm12,%xmm2
	aesenc %xmm12,%xmm3
	// round 9
	aesenc %xmm4,%xmm0
	aesenc %xmm4,%xmm1
	aesenc %xmm4,%xmm2
	aesenc %xmm4,%xmm3
	// round 10
	aesenc %xmm13,%xmm0
	aesenc %xmm13,%xmm1
	aesenc %xmm13,%xmm2
	aesenc %xmm13,%xmm3
	// round 11
	aesenc %xmm14,%xmm0
	aesenc %xmm14,%xmm1
	aesenc %xmm14,%xmm2
	aesenc %xmm14,%xmm3
	// round 12
	aesenclast %xmm15,%xmm0
	aesenclast %xmm15,%xmm1
	aesenclast %xmm15,%xmm2
	aesenclast %xmm15,%xmm3

	// we xor with the data
	movdqu (%rdx),%xmm4		// source is unaligned
	pxor   %xmm4,%xmm0
	movdqu 0x10(%rdx),%xmm4		// source is unaligned
	pxor   %xmm4,%xmm1
	movdqu 0x20(%rdx),%xmm4		// source is unaligned
	pxor   %xmm4,%xmm2
	movdqu 0x30(%rdx),%xmm4		// source is unaligned
	pxor   %xmm4,%xmm3

	// we save the data
	movdqu %xmm0,(%r8)
	movdqu %xmm1,0x10(%r8)
	movdqu %xmm2,0x20(%r8)
	movdqu %xmm3,0x30(%r8)

	// we update the data pointers
	add    $0x40,%rdx
	add    $0x40,%r8
	dec    %r11
	jne    1b	// .four_block_loop

2:	// .move_on_1:
	cmp    $0,%r10
	je     4f	// .move_on_2

3:	// .single_block_loop:
	// we prepare the counter
	movq   %rax,%xmm4
	movdqu 0(%r13),%xmm0
	paddd  %xmm4,%xmm0	// increment by 1
	movdqu %xmm0,0(%r13)

	movdqu (%r14),%xmm4	// .swap_mask
	pshufb %xmm4,%xmm0

	// now we have enough register space to store all round keys
	movdqu 0x40(%rcx),%xmm3
	movdqu 0x90(%rcx),%xmm4

	// first we xor with the first round key
	pxor   %xmm5,%xmm0
	// 11 rounds to follow
	aesenc %xmm6,%xmm0
	aesenc %xmm7,%xmm0
	aesenc %xmm8,%xmm0
	aesenc %xmm3,%xmm0
	aesenc %xmm9,%xmm0
	aesenc %xmm10,%xmm0
	aesenc %xmm11,%xmm0
	aesenc %xmm12,%xmm0
	aesenc %xmm4,%xmm0
	aesenc %xmm13,%xmm0
	aesenc %xmm14,%xmm0
	// last round
	aesenclast %xmm15,%xmm0

	// we xor with the data
	movdqu (%rdx),%xmm4	// source is unaligned
	pxor   %xmm4,%xmm0

	// we save the data
	movdqu %xmm0,(%r8)

	// we update the data pointers
	add    $0x10,%rdx
	add    $0x10,%r8
	dec    %r10
	jne    3b	// .single_block_loop

4:	// .move_on_2:
	cmp    $0,%r9
	je     7f	// .move_on_3

	xor    %rax,%rax
	pxor   %xmm1,%xmm1
	mov    %r9,%r10
	add    %r9,%rdx
	sub    $1,%rdx

5:	// .load_remaining_bytes:
	mov    (%rdx),%al
	movd   %eax,%xmm2
	pslldq $1,%xmm1
	por    %xmm2,%xmm1
	dec    %rdx
	dec    %r9
	jne    5b	// .load_remaining_bytes

	// we prepare the counter
	mov    $1,%rax
	movq   %rax,%xmm4
	movdqu 0(%r13),%xmm0
	paddd  %xmm4,%xmm0	// increment by 1

	movdqu (%r14),%xmm4	// .swap_mask
	pshufb %xmm4,%xmm0

	// we encrypt the remaining bytes
	movdqu 0x40(%rcx),%xmm3
	movdqu 0x90(%rcx),%xmm4
	pxor   %xmm5,%xmm0
	aesenc %xmm6,%xmm0
	aesenc %xmm7,%xmm0
	aesenc %xmm8,%xmm0
	aesenc %xmm3,%xmm0
	aesenc %xmm9,%xmm0
	aesenc %xmm10,%xmm0
	aesenc %xmm11,%xmm0
	aesenc %xmm12,%xmm0
	aesenc %xmm4,%xmm0
	aesenc %xmm13,%xmm0
	aesenc %xmm14,%xmm0
	aesenclast %xmm15,%xmm0

	// the counter mode xor
	pxor   %xmm1,%xmm0

6:	// .store_remaining_bytes:
	movd   %xmm0,%eax
	mov    %al,(%r8)
	psrldq $1,%xmm0
	inc    %r8
	dec    %r10
	jne    6b	// .store_remaining_bytes

7:	// .move_on_3:
	SET_TS_OR_POP_XMM_REGISTERS(%rdi)
	pop    %r14
	pop    %r13
	retq
	SET_SIZE(aes192_ctr_asm)


/*
 * void aes256_ctr_asm(unsigned char *ks, unsigned char *p, unsigned char *c,
 *	size_t length, unsigned char *IV, unsigned char *feedback)
 *
 * Parameters:
 * p1 rcx	holds the pointer to the key schedule
 * p2 rdx	holds the pointer to the source, p
 * p3 r8	holds the pointer to the destination, c
 * p4 r9	holds the length in bytes
 * p5 rax	holds the pointer to the IV (128-bit little-endian counter)
 * p6 r13	holds the pointer to the feedback (updated with IV-1)
 */
ENTRY_NP(aes256_ctr_asm)
	// Setup
	push   %r13
	push   %r14
	mov    %r9,%r13         // p6 feedback
	mov    %r8,%rax         // p5 IV
	mov    %rcx,%r9         // p4 64-bit length, in bytes
	mov    %rdx,%r8         // p3 destination, c
	mov    %rdi,%rcx        // p1 key schedule
	mov    %rsi,%rdx        // p2 source, p
	lea    .swap_mask(%rip),%r14
	CLEAR_TS_OR_PUSH_XMM_REGISTERS(%rdi)

	// we compute the number of four block units
	mov    %r9,%r11
	shr    $6,%r11

	// we compute the number of remaining complete blocks
	and    $0x3f,%r9
	mov    %r9,%r10
	shr    $4,%r10		// Convert from bytes to 16-byte blocks

	// we also compute the number of remaining bytes
	and    $0xf,%r9

	// we load eleven keys of the key schedule from memory
	movdqu (%rcx),%xmm5
	movdqu 0x10(%rcx),%xmm6
	movdqu 0x30(%rcx),%xmm7
	movdqu 0x40(%rcx),%xmm8
	movdqu 0x60(%rcx),%xmm9
	movdqu 0x70(%rcx),%xmm10
	movdqu 0x90(%rcx),%xmm11
	movdqu 0xa0(%rcx),%xmm12
	movdqu 0xc0(%rcx),%xmm13
	movdqu 0xd0(%rcx),%xmm14
	movdqu 0xe0(%rcx),%xmm15

	// we load the IV-1 into the memory entry tagged "counter"
	movdqu (%rax),%xmm0
	mov    $1,%rax
	movq   %rax,%xmm4
	psubd  %xmm4,%xmm0
	movdqu %xmm0,0(%r13)

	// check if the four block length is zero
	cmp    $0,%r11
	je     2f	// .move_on_1

1:	// .four_block_loop:
	// we prepare the four counters
	movq   %rax,%xmm4
	movdqu 0(%r13),%xmm0
	paddd  %xmm4,%xmm0	// increment by 1
	movdqu %xmm0,%xmm1
	paddd  %xmm4,%xmm1	// increment by 1
	movdqu %xmm1,%xmm2
	paddd  %xmm4,%xmm2	// increment by 1
	movdqu %xmm2,%xmm3
	paddd  %xmm4,%xmm3	// increment by 1
	movdqu %xmm3,0(%r13)

	movdqu (%r14),%xmm4	// .swap_mask
	pshufb %xmm4,%xmm0
	pshufb %xmm4,%xmm1
	pshufb %xmm4,%xmm2
	pshufb %xmm4,%xmm3

	// first we xor with the first round key
	pxor   %xmm5,%xmm0
	pxor   %xmm5,%xmm1
	pxor   %xmm5,%xmm2
	pxor   %xmm5,%xmm3

	movdqu 0x20(%rcx),%xmm4
	// round 1
	aesenc %xmm6,%xmm0
	aesenc %xmm6,%xmm1
	aesenc %xmm6,%xmm2
	aesenc %xmm6,%xmm3
	// round 2
	aesenc %xmm4,%xmm0
	aesenc %xmm4,%xmm1
	aesenc %xmm4,%xmm2
	aesenc %xmm4,%xmm3

	// round 3
	aesenc %xmm7,%xmm0
	aesenc %xmm7,%xmm1
	aesenc %xmm7,%xmm2
	aesenc %xmm7,%xmm3

	movdqu 0x50(%rcx),%xmm4
	// round 4
	aesenc %xmm8,%xmm0
	aesenc %xmm8,%xmm1
	aesenc %xmm8,%xmm2
	aesenc %xmm8,%xmm3
	// round 5
	aesenc %xmm4,%xmm0
	aesenc %xmm4,%xmm1
	aesenc %xmm4,%xmm2
	aesenc %xmm4,%xmm3
	// round 6
	aesenc %xmm9,%xmm0
	aesenc %xmm9,%xmm1
	aesenc %xmm9,%xmm2
	aesenc %xmm9,%xmm3

	movdqu 0x80(%rcx),%xmm4
	// round 7
	aesenc %xmm10,%xmm0
	aesenc %xmm10,%xmm1
	aesenc %xmm10,%xmm2
	aesenc %xmm10,%xmm3
	// round 8
	aesenc %xmm4,%xmm0
	aesenc %xmm4,%xmm1
	aesenc %xmm4,%xmm2
	aesenc %xmm4,%xmm3
	// round 9
	aesenc %xmm11,%xmm0
	aesenc %xmm11,%xmm1
	aesenc %xmm11,%xmm2
	aesenc %xmm11,%xmm3

	movdqu 0xb0(%rcx),%xmm4
	// round 10
	aesenc %xmm12,%xmm0
	aesenc %xmm12,%xmm1
	aesenc %xmm12,%xmm2
	aesenc %xmm12,%xmm3
	// round 11
	aesenc %xmm4,%xmm0
	aesenc %xmm4,%xmm1
	aesenc %xmm4,%xmm2
	aesenc %xmm4,%xmm3
	// round 12
	aesenc %xmm13,%xmm0
	aesenc %xmm13,%xmm1
	aesenc %xmm13,%xmm2
	aesenc %xmm13,%xmm3
	// round 13
	aesenc %xmm14,%xmm0
	aesenc %xmm14,%xmm1
	aesenc %xmm14,%xmm2
	aesenc %xmm14,%xmm3
	// round 14
	aesenclast %xmm15,%xmm0
	aesenclast %xmm15,%xmm1
	aesenclast %xmm15,%xmm2
	aesenclast %xmm15,%xmm3

	// we xor with the data
	movdqu (%rdx),%xmm4		// source is unaligned
	pxor   %xmm4,%xmm0
	movdqu 0x10(%rdx),%xmm4		// source is unaligned
	pxor   %xmm4,%xmm1
	movdqu 0x20(%rdx),%xmm4		// source is unaligned
	pxor   %xmm4,%xmm2
	movdqu 0x30(%rdx),%xmm4		// source is unaligned
	pxor   %xmm4,%xmm3

	// we save the data
	movdqu %xmm0,(%r8)
	movdqu %xmm1,0x10(%r8)
	movdqu %xmm2,0x20(%r8)
	movdqu %xmm3,0x30(%r8)

	// we update the data pointers
	add    $0x40,%rdx
	add    $0x40,%r8
	dec    %r11
	jne    1b	// .four_block_loop:

2:	// .move_on_1:
	cmp    $0,%r10
	je     4f	// .move_on_2

3:	// .single_block_loop:
	// we prepare the counter
	movq   %rax,%xmm4
	movdqu 0(%r13),%xmm0
	paddd  %xmm4,%xmm0	// increment by 1
	movdqu %xmm0,0(%r13)

	movdqu (%r14),%xmm4	// .swap_mask
	pshufb %xmm4,%xmm0

	// now we have enough register space to store all rounds keys
	movdqu 0x20(%rcx),%xmm1
	movdqu 0x50(%rcx),%xmm2
	movdqu 0x80(%rcx),%xmm3
	movdqu 0xb0(%rcx),%xmm4

	// first we xor with the first round key
	pxor   %xmm5,%xmm0
	// 13 rounds to follow
	aesenc %xmm6,%xmm0
	aesenc %xmm1,%xmm0
	aesenc %xmm7,%xmm0
	aesenc %xmm8,%xmm0
	aesenc %xmm2,%xmm0
	aesenc %xmm9,%xmm0
	aesenc %xmm10,%xmm0
	aesenc %xmm3,%xmm0
	aesenc %xmm11,%xmm0
	aesenc %xmm12,%xmm0
	aesenc %xmm4,%xmm0
	aesenc %xmm13,%xmm0
	aesenc %xmm14,%xmm0
	// last round
	aesenclast %xmm15,%xmm0

	// we xor with the data
	movdqu (%rdx),%xmm4	// source is unaligned
	pxor   %xmm4,%xmm0

	// we save the data
	movdqu %xmm0,(%r8)

	// we update the data pointers
	add    $0x10,%rdx
	add    $0x10,%r8
	dec    %r10
	jne    3b	// .single_block_loop

4:	// .move_on_2:
	cmp    $0,%r9
	je     7f	// .move_on_3

	xor    %rax,%rax
	pxor   %xmm1,%xmm1
	mov    %r9,%r10
	add    %r9,%rdx
	sub    $1,%rdx

5:	// .load_remaining_bytes:
	mov    (%rdx),%al
	movd   %eax,%xmm2
	pslldq $1,%xmm1
	por    %xmm2,%xmm1
	dec    %rdx
	dec    %r9
	jne    5b	// .load_remaining_bytes

	// we prepare the counter
	mov    $1,%rax
	movq   %rax,%xmm4
	movdqu 0(%r13),%xmm0
	paddd  %xmm4,%xmm0	// increment by 1

	movdqu (%r14),%xmm4	// .swap_mask
	pshufb %xmm4,%xmm0

	// we encrypt the remaining byte
	movdqu 0x50(%rcx),%xmm2
	movdqu 0x80(%rcx),%xmm3
	movdqu 0xb0(%rcx),%xmm4
	pxor   %xmm5,%xmm0
	aesenc %xmm6,%xmm0
	aesenc 0x20(%rcx),%xmm0
	aesenc %xmm7,%xmm0
	aesenc %xmm8,%xmm0
	aesenc %xmm2,%xmm0
	aesenc %xmm9,%xmm0
	aesenc %xmm10,%xmm0
	aesenc %xmm3,%xmm0
	aesenc %xmm11,%xmm0
	aesenc %xmm12,%xmm0
	aesenc %xmm4,%xmm0
	aesenc %xmm13,%xmm0
	aesenc %xmm14,%xmm0
	aesenclast %xmm15,%xmm0

	// the counter mode xor
	pxor   %xmm1,%xmm0

6:	// .store_remaining_bytes:
	movd   %xmm0,%eax
	mov    %al,(%r8)
	psrldq $1,%xmm0
	inc    %r8
	dec    %r10
	jne    6b	// .store_remaining_bytes

7:	// .move_on_3
	SET_TS_OR_POP_XMM_REGISTERS(%rdi)
	pop    %r14
	pop    %r13
	retq
	SET_SIZE(aes256_ctr_asm)


/*
 * Use this mask to byte-swap a 16-byte integer with the pshufb instruction.
 * static uint8_t swap_mask[] = {
 *	 15, 14, 13, 12, 11, 10, 9, 8, 7, 6 ,5, 4, 3, 2, 1, 0};
 */
.text
.align XMM_ALIGN
.type	.swap_mask, @object
.swap_mask:
	.byte	15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0

#endif  /* lint || __lint */
