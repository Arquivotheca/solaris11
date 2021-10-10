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
 * 8. For aes*_cbc_decrypt_asm(), modified the code so the input and output
 *    can be the same.  Save the old input (feedback) in a %xmm register
 *    before overwriting with the output.
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
aes128_cbc_encrypt_asm(unsigned char *ks, unsigned char *p, unsigned char *c,
    size_t length, unsigned char *IV)
{
}

/* ARGSUSED */
void
aes192_cbc_encrypt_asm(unsigned char *ks, unsigned char *p, unsigned char *c,
    size_t length, unsigned char *IV)
{
}

/* ARGSUSED */
void
aes256_cbc_encrypt_asm(unsigned char *ks, unsigned char *p, unsigned char *c,
    size_t length, unsigned char *IV)
{
}

/* ARGSUSED */
void
aes128_cbc_decrypt_asm(unsigned char *ks, unsigned char *p, unsigned char *c,
    size_t length, unsigned char *IV, unsigned char *feedback)
{
}

/* ARGSUSED */
void
aes192_cbc_decrypt_asm(unsigned char *ks, unsigned char *p, unsigned char *c,
    size_t length, unsigned char *IV, unsigned char *feedback)
{
}

/* ARGSUSED */
void
aes256_cbc_decrypt_asm(unsigned char *ks, unsigned char *p, unsigned char *c,
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
 * void aes128_cbc_encrypt_asm(unsigned char *ks, unsigned char *p,
 *	unsigned char *c, size_t length, unsigned char *IV)
 *
 * Parameters:
 * p1 rcx holds the pointer to the key schedule
 * p2 rdx holds the pointer to the plaintext
 * p3 r8  holds the pointer to the ciphertext
 * p4 r9  holds the length in bytes
 * p5 r11 holds the pointer to the IV
 */
ENTRY_NP(aes128_cbc_encrypt_asm)
	// Setup
	mov    %rcx,%r9		// P4: 64-bit length, in bytes
	mov    %r8,%r11		// P5: IV
	mov    %rdx,%r8		// P3: ciphertext, c
	mov    %rdi,%rcx	// P1: key schedule, ks
	mov    %rsi,%rdx	// P2: plaintext, p
	CLEAR_TS_OR_PUSH_XMM_REGISTERS(%rdi)


	movdqu (%r11),%xmm0	// IV move
	shr    $4,%r9		// Convert from bytes to 16-byte blocks

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

	cmp    $0,%r9
	je     2f	// .move_on_1

1:	// .single_block_loop:
	// the cbc XOR
	movdqu (%rdx),%xmm1	// plaintext may be unaligned
	pxor   %xmm1,%xmm0

	// first we xor with the first round key
	pxor   %xmm5,%xmm0
	// rounds to follow
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

	// we save the data
	movdqu %xmm0,(%r8)
	// we update the data pointers and counter
	add    $0x10,%rdx
	add    $0x10,%r8
	dec    %r9
	jne    1b	// .single_block_loop

2:	// .move_on_1:
	SET_TS_OR_POP_XMM_REGISTERS(%rdi)
	retq
	SET_SIZE(aes128_cbc_encrypt_asm)


/*
 * void aes192_cbc_encrypt_asm(unsigned char *ks, unsigned char *p,
 *	unsigned char *c, size_t length, unsigned char *IV)
 *
 * Parameters:
 * p1 rcx holds the pointer to the key schedule
 * p2 rdx holds the pointer to the plaintext
 * p3 r8  holds the pointer to the ciphertext
 * p4 r9  holds the length in bytes
 * p5 r11 holds the pointer to the IV
 */
ENTRY_NP(aes192_cbc_encrypt_asm)
	// Setup
	mov    %rcx,%r9         // P4: 64-bit length, in bytes
	mov    %r8,%r11         // P5: IV
	mov    %rdx,%r8         // P3: ciphertext, c
	mov    %rdi,%rcx        // P1: key schedule, ks
	mov    %rsi,%rdx        // P2: plaintext, p
	CLEAR_TS_OR_PUSH_XMM_REGISTERS(%rdi)

	movdqu (%r11),%xmm0	// IV move
	shr    $4,%r9		// Convert from bytes to 16-byte blocks

	// we load the key schedule from memory
	movdqu (%rcx),%xmm3
	movdqu 0x10(%rcx),%xmm4
	movdqu 0x20(%rcx),%xmm5
	movdqu 0x30(%rcx),%xmm6
	movdqu 0x40(%rcx),%xmm7
	movdqu 0x50(%rcx),%xmm8
	movdqu 0x60(%rcx),%xmm9
	movdqu 0x70(%rcx),%xmm10
	movdqu 0x80(%rcx),%xmm11
	movdqu 0x90(%rcx),%xmm12
	movdqu 0xa0(%rcx),%xmm13
	movdqu 0xb0(%rcx),%xmm14
	movdqu 0xc0(%rcx),%xmm15

	cmp    $0,%r9
	je     2f	// .move_on_1

1:	// .single_block_loop:
	// the cbc XOR
	movdqu (%rdx),%xmm1	// plaintext may be unaligned
	pxor   %xmm1,%xmm0
	// first we xor with the first round key
	pxor   %xmm3,%xmm0

	// 11 rounds to follow
	aesenc %xmm4,%xmm0
	aesenc %xmm5,%xmm0
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

	// we save the data
	movdqu %xmm0,(%r8)

	// We update the data pointers and counter
	add    $0x10,%rdx
	add    $0x10,%r8
	dec    %r9
	jne    1b	// .single_block_loop

2:	// .move_on_1:
	SET_TS_OR_POP_XMM_REGISTERS(%rdi)
	retq
	SET_SIZE(aes192_cbc_encrypt_asm)


/*
 * void aes256_cbc_encrypt_asm(unsigned char *ks, unsigned char *p,
 *	unsigned char *c, size_t length, unsigned char *IV)
 *
 * Parameters:
 * p1 rcx holds the pointer to the key schedule
 * p2 rdx holds the pointer to the plaintext
 * p3 r8  holds the pointer to the ciphertext
 * p4 r9  holds the length in bytes
 * p5 r11 holds the pointer to the IV
 */
ENTRY_NP(aes256_cbc_encrypt_asm)
	// Setup
	mov    %rcx,%r9         // P4: 64-bit length, in bytes
	mov    %r8,%r11         // P5: IV
	mov    %rdx,%r8         // P3: ciphertext, c
	mov    %rdi,%rcx        // P1: key schedule, ks
	mov    %rsi,%rdx        // P2: plaintext, p
	CLEAR_TS_OR_PUSH_XMM_REGISTERS(%rdi)

	movdqu (%r11),%xmm0	// IV move
	shr    $4,%r9		// Convert from bytes to 16-byte blocks

	// We load the key schedule from memory
	movdqu (%rcx),%xmm1
	movdqu 0x10(%rcx),%xmm2
	movdqu 0x20(%rcx),%xmm3
	movdqu 0x30(%rcx),%xmm4
	movdqu 0x40(%rcx),%xmm5
	movdqu 0x50(%rcx),%xmm6
	movdqu 0x60(%rcx),%xmm7
	movdqu 0x70(%rcx),%xmm8
	movdqu 0x80(%rcx),%xmm9
	movdqu 0x90(%rcx),%xmm10
	movdqu 0xa0(%rcx),%xmm11
	movdqu 0xb0(%rcx),%xmm12
	movdqu 0xc0(%rcx),%xmm13
	movdqu 0xd0(%rcx),%xmm14
	movdqu 0xe0(%rcx),%xmm15

	cmp    $0,%r9
	je     2f	// .move_on_1

1:	// .single_block_loop:
	// the cbc XOR
	movdqu   (%rdx),%xmm15		// plaintext is unaligned
	pxor   %xmm15,%xmm0
	// first we xor with the first round key
	pxor   %xmm1,%xmm0
	movdqu 0xe0(%rcx),%xmm15	// restore clobbered %xmm15

	// 13 rounds to follow
	aesenc %xmm2,%xmm0
	aesenc %xmm3,%xmm0
	aesenc %xmm4,%xmm0
	aesenc %xmm5,%xmm0
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
	// we save the data
	movdqu %xmm0,(%r8)
	// we update the data pointers and counter
	add    $0x10,%rdx
	add    $0x10,%r8
	dec    %r9
	jne    1b	// .single_block_loop

2:	// .move_on_1:
	SET_TS_OR_POP_XMM_REGISTERS(%rdi)
	retq
	SET_SIZE(aes256_cbc_encrypt_asm)


/*
 * void aes128_cbc_decrypt_asm(unsigned char *ks, unsigned char *p,
 *	unsigned char *c, size_t length, unsigned char *IV,
 *	unsigned char *feedback)
 *
 * Parameters:
 * p1 rcx holds the pointer to the key schedule
 * p2 rdx holds the pointer to the plaintext
 * p3 r8  holds the pointer to the ciphertext
 * p4 r9  holds the length in bytes
 * p5 r12 holds the pointer to the IV
 * p6 r13 holds the pointer to the feedback; aligned 0 mod 16
 */
ENTRY_NP(aes128_cbc_decrypt_asm)
	// Setup
	push   %r12
	push   %r13
	mov    %r8,%r12		// P5: IV
	mov    %r9,%r13		// P6: feedback
	mov    %rdx,%r8		// P3: ciphertext, c
	mov    %rcx,%r9		// P4: 64-bit length, in bytes
	mov    %rsi,%rdx	// P2: plaintext, p
	mov    %rdi,%rcx	// P1: key schedule, ks
	CLEAR_TS_OR_PUSH_XMM_REGISTERS(%rdi)

	movdqu (%r12),%xmm0	// IV move
	movdqu %xmm0,0(%r13)
	// we compute the number of four block units
	mov    %r9,%r11
	shr    $6,%r11
	// we compute the number of remaining complete blocks
	and    $0x3f,%r9
	mov    %r9,%r10
	shr    $4,%r10		// Convert from bytes to 16-byte blocks
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

	cmp    $0,%r11
	je     2f	// .move_on_1

1:	// .four_block_loop:
	// we load the data
	movdqu (%r8),%xmm0
	movdqu 0x10(%r8),%xmm1
	movdqu 0x20(%r8),%xmm2
	movdqu 0x30(%r8),%xmm3

	// first we xor with the first round key
	pxor   %xmm5,%xmm0
	pxor   %xmm5,%xmm1
	pxor   %xmm5,%xmm2
	pxor   %xmm5,%xmm3
	// round 1
	aesdec %xmm6,%xmm0
	aesdec %xmm6,%xmm1
	aesdec %xmm6,%xmm2
	aesdec %xmm6,%xmm3
	// round 2
	aesdec %xmm7,%xmm0
	aesdec %xmm7,%xmm1
	aesdec %xmm7,%xmm2
	aesdec %xmm7,%xmm3
	// round 3
	aesdec %xmm8,%xmm0
	aesdec %xmm8,%xmm1
	aesdec %xmm8,%xmm2
	aesdec %xmm8,%xmm3
	// round 4
	aesdec %xmm9,%xmm0
	aesdec %xmm9,%xmm1
	aesdec %xmm9,%xmm2
	aesdec %xmm9,%xmm3
	// round 5
	aesdec %xmm10,%xmm0
	aesdec %xmm10,%xmm1
	aesdec %xmm10,%xmm2
	aesdec %xmm10,%xmm3
	// round 6
	aesdec %xmm11,%xmm0
	aesdec %xmm11,%xmm1
	aesdec %xmm11,%xmm2
	aesdec %xmm11,%xmm3
	// round 7
	aesdec %xmm12,%xmm0
	aesdec %xmm12,%xmm1
	aesdec %xmm12,%xmm2
	aesdec %xmm12,%xmm3
	// round 8
	aesdec %xmm13,%xmm0
	aesdec %xmm13,%xmm1
	aesdec %xmm13,%xmm2
	aesdec %xmm13,%xmm3
	// round 9
	aesdec %xmm14,%xmm0
	aesdec %xmm14,%xmm1
	aesdec %xmm14,%xmm2
	aesdec %xmm14,%xmm3
	// round 10
	aesdeclast %xmm15,%xmm0
	aesdeclast %xmm15,%xmm1
	aesdeclast %xmm15,%xmm2
	aesdeclast %xmm15,%xmm3

	// the CBC XORs
	pxor   0(%r13),%xmm0	// feedback is aligned
	movdqu (%r8),%xmm4	// cipher is unaligned
	pxor   %xmm4,%xmm1
	movdqu 0x10(%r8),%xmm4	// cipher is unaligned
	pxor   %xmm4,%xmm2
	movdqu 0x20(%r8),%xmm4	// cipher is unaligned
	pxor   %xmm4,%xmm3

	// we save the old input data for feedback to the next block
	// (before it may be overwritten when input buffer == output buffer)
	movdqu 0x30(%r8),%xmm4

	// we save the output data
	movdqu %xmm0,(%rdx)
	movdqu %xmm1,0x10(%rdx)
	movdqu %xmm2,0x20(%rdx)
	movdqu %xmm3,0x30(%rdx)

	// we update the (saved) feedback to the next block
	movdqu %xmm4,0(%r13)

	// we update the data pointers and counter
	add    $0x40,%r8
	add    $0x40,%rdx
	dec    %r11
	jne    1b	// .four_block_loop

2:	// .move_on_1:
	cmp    $0,%r10
	je     4f	// .move_on_2

3:	// .single_block_loop:
	// we load the data
	movdqu (%r8),%xmm0

	// first we xor with the first round key
	pxor   %xmm5,%xmm0
	// 9 rounds to follow
	aesdec %xmm6,%xmm0
	aesdec %xmm7,%xmm0
	aesdec %xmm8,%xmm0
	aesdec %xmm9,%xmm0
	aesdec %xmm10,%xmm0
	aesdec %xmm11,%xmm0
	aesdec %xmm12,%xmm0
	aesdec %xmm13,%xmm0
	aesdec %xmm14,%xmm0
	// last round
	aesdeclast %xmm15,%xmm0

	// the CBC XOR
	// pxor   .feedback,%xmm0
	pxor   0(%r13),%xmm0		// feedback is aligned

	// we save the old input data for feedback to the next block
	// (before it may be overwritten when input buffer == output buffer)
	movdqu (%r8),%xmm4

	// we save the output data
	movdqu %xmm0,(%rdx)

	// we update the (saved) feedback to the next block
	movdqu %xmm4,0(%r13)

	// we update the data pointers and counter
	add    $0x10,%r8
	add    $0x10,%rdx
	dec    %r10
	jne    3b	// .single_block_loop

4:	// .move_on_2:
	SET_TS_OR_POP_XMM_REGISTERS(%rdi)
	pop    %r13
	pop    %r12
	retq
	SET_SIZE(aes128_cbc_decrypt_asm)


/*
 * void aes192_cbc_decrypt_asm(unsigned char *ks, unsigned char *p,
 *	unsigned char *c, size_t length, unsigned char *IV,
 *	unsigned char *feedback)
 *
 * p1 rcx holds the pointer to the key schedule
 * p2 rdx holds the pointer to the plaintext
 * p3 r8  holds the pointer to the ciphertext
 * p4 r9  holds the length in bytes
 * p5 r12 holds the pointer to the IV
 * p6 r13 holds the pointer to the feedback; aligned 0 mod 16
 */
ENTRY_NP(aes192_cbc_decrypt_asm)
	// Setup
	push   %r12
	push   %r13
	mov    %r8,%r12         // P5: IV
	mov    %r9,%r13         // P6: feedback
	mov    %rdx,%r8         // P3: ciphertext, c
	mov    %rcx,%r9         // P4: 64-bit length, in bytes
	mov    %rsi,%rdx        // P2: plaintext, p
	mov    %rdi,%rcx        // P1: key schedule, ks
	CLEAR_TS_OR_PUSH_XMM_REGISTERS(%rdi)

	movdqu (%r12),%xmm0	// IV move
	movdqu %xmm0,0(%r13)
	// we compute the number of four block units
	mov    %r9,%r11
	shr    $6,%r11
	// we compute the number of remaining complete blocks
	and    $0x3f,%r9
	mov    %r9,%r10
	shr    $4,%r10		// Convert from bytes to 16-byte blocks
	// we load eleven keys of the key schedule fro memory
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

	cmp    $0,%r11
	je     2f	// .move_on_1

1:	// .four_block_loop:
	// we load the data
	movdqu (%r8),%xmm0
	movdqu 0x10(%r8),%xmm1
	movdqu 0x20(%r8),%xmm2
	movdqu 0x30(%r8),%xmm3

	// first we xor with the first round key
	pxor   %xmm5,%xmm0
	pxor   %xmm5,%xmm1
	pxor   %xmm5,%xmm2
	pxor   %xmm5,%xmm3
	// round 1
	aesdec %xmm6,%xmm0
	aesdec %xmm6,%xmm1
	aesdec %xmm6,%xmm2
	aesdec %xmm6,%xmm3

	movdqu 0x40(%rcx),%xmm4
	// round 2
	aesdec %xmm7,%xmm0
	aesdec %xmm7,%xmm1
	aesdec %xmm7,%xmm2
	aesdec %xmm7,%xmm3
	// round 3
	aesdec %xmm8,%xmm0
	aesdec %xmm8,%xmm1
	aesdec %xmm8,%xmm2
	aesdec %xmm8,%xmm3
	// round 4
	aesdec %xmm4,%xmm0
	aesdec %xmm4,%xmm1
	aesdec %xmm4,%xmm2
	aesdec %xmm4,%xmm3
	// round 5
	aesdec %xmm9,%xmm0
	aesdec %xmm9,%xmm1
	aesdec %xmm9,%xmm2
	aesdec %xmm9,%xmm3
	// round 6
	aesdec %xmm10,%xmm0
	aesdec %xmm10,%xmm1
	aesdec %xmm10,%xmm2
	aesdec %xmm10,%xmm3

	movdqu 0x90(%rcx),%xmm4
	// round 7
	aesdec %xmm11,%xmm0
	aesdec %xmm11,%xmm1
	aesdec %xmm11,%xmm2
	aesdec %xmm11,%xmm3
	// round 8
	aesdec %xmm12,%xmm0
	aesdec %xmm12,%xmm1
	aesdec %xmm12,%xmm2
	aesdec %xmm12,%xmm3
	// round 9
	aesdec %xmm4,%xmm0
	aesdec %xmm4,%xmm1
	aesdec %xmm4,%xmm2
	aesdec %xmm4,%xmm3
	// round 10
	aesdec %xmm13,%xmm0
	aesdec %xmm13,%xmm1
	aesdec %xmm13,%xmm2
	aesdec %xmm13,%xmm3
	// round 11
	aesdec %xmm14,%xmm0
	aesdec %xmm14,%xmm1
	aesdec %xmm14,%xmm2
	aesdec %xmm14,%xmm3
	// round 12
	aesdeclast %xmm15,%xmm0
	aesdeclast %xmm15,%xmm1
	aesdeclast %xmm15,%xmm2
	aesdeclast %xmm15,%xmm3

	// the CBC XORs
	pxor   0(%r13),%xmm0	// feedback is aligned
	movdqu (%r8),%xmm4	// cipher is unaligned
	pxor   %xmm4,%xmm1
	movdqu 0x10(%r8),%xmm4	// cipher is unaligned
	pxor   %xmm4,%xmm2
	movdqu 0x20(%r8),%xmm4	// cipher is unaligned
	pxor   %xmm4,%xmm3

	// we save the old input data for feedback for the next block
	// (before it may be overwritten when input buffer == output buffer)
	movdqu 0x30(%r8),%xmm4

	// we save the output data
	movdqu %xmm0,(%rdx)
	movdqu %xmm1,0x10(%rdx)
	movdqu %xmm2,0x20(%rdx)
	movdqu %xmm3,0x30(%rdx)

	// we update the (saved) feedback to the next block
	movdqu %xmm4,0(%r13)

	// we update the data pointers and counter
	add    $0x40,%r8
	add    $0x40,%rdx
	dec    %r11
	jne    1b	// .four_block_loop

2:	// .move_on_1:
	cmp    $0,%r10
	je     4f	// .move_on_2

3:	// .single_block_loop:
	// now we have enough register space to store all round keys
	movdqu 0x40(%rcx),%xmm3
	movdqu 0x90(%rcx),%xmm4

	// we load the data
	movdqu (%r8),%xmm0

	// first we xor with the first round key
	pxor   %xmm5,%xmm0
	// 11 rounds to follow
	aesdec %xmm6,%xmm0
	aesdec %xmm7,%xmm0
	aesdec %xmm8,%xmm0
	aesdec %xmm3,%xmm0
	aesdec %xmm9,%xmm0
	aesdec %xmm10,%xmm0
	aesdec %xmm11,%xmm0
	aesdec %xmm12,%xmm0
	aesdec %xmm4,%xmm0
	aesdec %xmm13,%xmm0
	aesdec %xmm14,%xmm0
	// last round
	aesdeclast %xmm15,%xmm0

	// the CBC XOR
	pxor   0(%r13),%xmm0	// feedback is aligned

	// we save the old input data for feedback to the next block
	// (before it may be overwritten when input buffer == output buffer)
	movdqu (%r8),%xmm4

	// we save the output data
	movdqu %xmm0,(%rdx)

	// we update the (saved) feedback to the next block
	movdqu %xmm4,0(%r13)

	// we update the data pointers and counter
	add    $0x10,%r8
	add    $0x10,%rdx
	dec    %r10
	jne    3b	// .single_block_loop

4:	// .move_on_2:
	SET_TS_OR_POP_XMM_REGISTERS(%rdi)
	pop    %r13
	pop    %r12
	retq
	SET_SIZE(aes192_cbc_decrypt_asm)


/*
 * void aes256_cbc_decrypt_asm(unsigned char *ks, unsigned char *p,
 *	unsigned char *c, size_t length, unsigned char *IV,
 *	unsigned char *feedback)
 *
 * Parameters:
 * p1 rcx holds the pointer to the key schedule
 * p2 rdx holds the pointer to the plaintext
 * p3 r8  holds the pointer to the ciphertext
 * p4 r9  holds the length in bytes
 * p5 rax holds the pointer to the IV
 * p6 r13 holds the pointer to the feedback; aligned 0 mod 16
 */
ENTRY_NP(aes256_cbc_decrypt_asm)
	// Setup
	push   %r12
	push   %r13
	mov    %r8,%r12         // P5: IV
	mov    %r9,%r13         // P6: feedback
	mov    %rdx,%r8         // P3: ciphertext, c
	mov    %rcx,%r9         // P4: 64-bit length, in bytes
	mov    %rsi,%rdx        // P2: plaintext, p
	mov    %rdi,%rcx        // P1: key schedule, ks
	CLEAR_TS_OR_PUSH_XMM_REGISTERS(%rdi)

	movdqu (%r12),%xmm0	// IV move
	movdqu %xmm0,0(%r13)
	// we compute the number of four block units
	mov    %r9,%r11
	shr    $6,%r11
	// we compute the number of remaining complete blocks
	and    $0x3f,%r9
	mov    %r9,%r10
	shr    $4,%r10		// Convert from bytes to 16-byte blocks
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

	cmp    $0,%r11
	je     2f	// .move_on_1

1:	// .four_block_loop:
	// we load the data
	movdqu (%r8),%xmm0
	movdqu 0x10(%r8),%xmm1
	movdqu 0x20(%r8),%xmm2
	movdqu 0x30(%r8),%xmm3

	// first we xor with the first round key
	pxor   %xmm5,%xmm0
	pxor   %xmm5,%xmm1
	pxor   %xmm5,%xmm2
	pxor   %xmm5,%xmm3
	movdqu 0x20(%rcx),%xmm4
	// round 1
	aesdec %xmm6,%xmm0
	aesdec %xmm6,%xmm1
	aesdec %xmm6,%xmm2
	aesdec %xmm6,%xmm3
	// round 2
	aesdec %xmm4,%xmm0
	aesdec %xmm4,%xmm1
	aesdec %xmm4,%xmm2
	aesdec %xmm4,%xmm3
	// round 3
	aesdec %xmm7,%xmm0
	aesdec %xmm7,%xmm1
	aesdec %xmm7,%xmm2
	aesdec %xmm7,%xmm3

	movdqu 0x50(%rcx),%xmm4
	// round 4
	aesdec %xmm8,%xmm0
	aesdec %xmm8,%xmm1
	aesdec %xmm8,%xmm2
	aesdec %xmm8,%xmm3
	// round 5
	aesdec %xmm4,%xmm0
	aesdec %xmm4,%xmm1
	aesdec %xmm4,%xmm2
	aesdec %xmm4,%xmm3
	// round 6
	aesdec %xmm9,%xmm0
	aesdec %xmm9,%xmm1
	aesdec %xmm9,%xmm2
	aesdec %xmm9,%xmm3

	movdqu 0x80(%rcx),%xmm4
	// round 7
	aesdec %xmm10,%xmm0
	aesdec %xmm10,%xmm1
	aesdec %xmm10,%xmm2
	aesdec %xmm10,%xmm3
	// round 8
	aesdec %xmm4,%xmm0
	aesdec %xmm4,%xmm1
	aesdec %xmm4,%xmm2
	aesdec %xmm4,%xmm3
	// round 9
	aesdec %xmm11,%xmm0
	aesdec %xmm11,%xmm1
	aesdec %xmm11,%xmm2
	aesdec %xmm11,%xmm3

	movdqu 0xb0(%rcx),%xmm4
	// round 10
	aesdec %xmm12,%xmm0
	aesdec %xmm12,%xmm1
	aesdec %xmm12,%xmm2
	aesdec %xmm12,%xmm3
	// round 11
	aesdec %xmm4,%xmm0
	aesdec %xmm4,%xmm1
	aesdec %xmm4,%xmm2
	aesdec %xmm4,%xmm3
	// round 12
	aesdec %xmm13,%xmm0
	aesdec %xmm13,%xmm1
	aesdec %xmm13,%xmm2
	aesdec %xmm13,%xmm3
	// round 13
	aesdec %xmm14,%xmm0
	aesdec %xmm14,%xmm1
	aesdec %xmm14,%xmm2
	aesdec %xmm14,%xmm3
	// round 14
	aesdeclast %xmm15,%xmm0
	aesdeclast %xmm15,%xmm1
	aesdeclast %xmm15,%xmm2
	aesdeclast %xmm15,%xmm3

	// the CBC XORs
	pxor   0(%r13),%xmm0	// feedback is aligned
	movdqu (%r8),%xmm4	// cipher is unaligned
	pxor   %xmm4,%xmm1
	movdqu 0x10(%r8),%xmm4	// cipher is unaligned
	pxor   %xmm4,%xmm2
	movdqu 0x20(%r8),%xmm4	// cipher is unaligned
	pxor   %xmm4,%xmm3

	// we save the old input data for feedback for the next block
	// (before it may be overwritten when input buffer == output buffer)
	movdqu 0x30(%r8),%xmm4

	// we save the output data
	movdqu %xmm0,(%rdx)
	movdqu %xmm1,0x10(%rdx)
	movdqu %xmm2,0x20(%rdx)
	movdqu %xmm3,0x30(%rdx)

	// we update the (saved) feedback to the next block
	movdqu %xmm4,0(%r13)

	// we update the data pointers and counter
	add    $0x40,%r8
	add    $0x40,%rdx
	dec    %r11
	jne    1b	// .four_block_loop

2:	// .move_on_1:
	cmp    $0,%r10
	je     4f	// .move_on_2

3:	// .single_block_loop:
	// now we have enough register space to store all round keys
	movdqu 0x20(%rcx),%xmm1
	movdqu 0x50(%rcx),%xmm2
	movdqu 0x80(%rcx),%xmm3
	movdqu 0xb0(%rcx),%xmm4

	// we load the data
	movdqu (%r8),%xmm0

	// first we xor with the first round key
	pxor   %xmm5,%xmm0
	// 13 rounds to follow
	aesdec %xmm6,%xmm0
	aesdec %xmm1,%xmm0
	aesdec %xmm7,%xmm0
	aesdec %xmm8,%xmm0
	aesdec %xmm2,%xmm0
	aesdec %xmm9,%xmm0
	aesdec %xmm10,%xmm0
	aesdec %xmm3,%xmm0
	aesdec %xmm11,%xmm0
	aesdec %xmm12,%xmm0
	aesdec %xmm4,%xmm0
	aesdec %xmm13,%xmm0
	aesdec %xmm14,%xmm0
	// last round
	aesdeclast %xmm15,%xmm0

	// the CBC XOR
	pxor   0(%r13),%xmm0	// feedback is aligned

	// we save the old input data for feedback to the next block
	// (before it may be overwritten when input buffer == output buffer)
	movdqu (%r8),%xmm4

	// we save the output data
	movdqu %xmm0,(%rdx)

	// we update the (saved) feedback to the next block
	movdqu %xmm4,0(%r13)

	// we update the data pointers and counter
	add    $0x10,%r8
	add    $0x10,%rdx
	dec    %r10
	jne    3b	// .single_block_loop

4:	// .move_on_2:
	SET_TS_OR_POP_XMM_REGISTERS(%rdi)
	pop    %r13
	pop    %r12
	retq
	SET_SIZE(aes256_cbc_decrypt_asm)

#endif  /* lint || __lint */
