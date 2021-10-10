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
 * Copyright (c) 2011, Intel Corporation
 * All rights reserved.
 */

/*
 * This is a vectorized SHA-1 implementation, it requires Intel(R) Supplemental
 * SSE3 instruction set extensions, introduced in Intel Core Microarchitecture
 * processors.
 *
 * This work was inspired by vectorized implementation of Dean Gaudet.
 * Additional information on it can be found here:
 * http://arctic.org/~dean/crypto/sha1.html
 *
 * It was improved upon with more efficient vectorization of the message
 * scheduling.  This compiler independent assembly implementation has also been
 * optimized for all current and several future generations of Intel CPUs.
 *
 * Speed of this implementation on 'Nehalem' processors family is 5.8~6.0
 * cycles/byte depending on an input buffer size.
 *
 * Intel CPUs supporting Intel AVX extensions will get additional boost, you
 * may need to update assembler version to support AVX in the case of compile
 * time errors. You can also disable AVX dispatch with
 * -DINTEL_SHA1_NO_AVX_DISPATCH.
 *
 * Source implements both forms of SHA-1 update function: working on multiple
 * or single 64-byte block(s) buffer. Multiple blocks version is pipelined and
 * faster overall, it is a default. Compile with -DINTEL_SHA1_SINGLEBLOCK
 * to select single block version.
 *
 * C/C++ prototypes of implemented functions are below:
 *    #ifndef INTEL_SHA1_SINGLEBLOCK
 *       // Updates 20-byte SHA-1 record in 'hash' for 'num_blocks' consequtive
 *       // 64-byte blocks
 *       extern "C" void sha1_update_intel(int *hash, const char* input,
 *           size_t num_blocks);
 *    #else
 *       // Updates 20-byte SHA-1 record in 'hash' for one 64-byte block
 *       // pointed by 'input'
 *       extern "C" void sha1_update_intel(int *hash, const char* input);
 *    #endif
 *
 * Name sha1_update_intel can be changed in the source below or via
 * compile time option:
 *   -DINTEL_SHA1_UPDATE_FUNCNAME=<name>
 * (e.g. sha1_block_data_order for OpenSSL 0.9.8)
 *
 * It implements both UNIX(default) and Windows ABIs, compile with -DWIN_ABI on
 * Windows.
 *
 * Code incorporating this implementation does not have to implement checking
 * for SSSE3 CPUID feature flag support (CPUID.1.ECX.SSSE3[bit 9]==1), it can
 * rely on efficient dispatch implementation provided in this source. Since in
 * most cases functionality on non-SSSE3 supporting CPUs is required, default
 * (most often the one being replaced) function can be provided for a dispatch
 * on such CPUs, then add the following option to a command line:
 *    -DINTEL_SHA1_UPDATE_DEFAULT_DISPATCH=<default_sha1_update_function_name> 
 * or put default SHA-1 update function name directly into the source right
 * after this comment.
 *
 * Thank you for using this code, your feedback is very welcome.
 *
 * Authors: Maxim.Locktyukhin and Ronen.Zohar at Intel Corp.
 */

/*
 * Source:
 *   http://software.intel.com/en-us/articles/
 *   improving-the-performance-of-the-secure-hash-algorithm-1/
 * Published April 23, 2010 2:42 PM PDT by Max Locktyukhin (Intel).
 *
 * License from the Intel web page above:
 * "We are encouraging all projects utilizing SHA-1 to integrate this new fast
 * implementation and are ready to help if issues or concerns arise (you are
 * welcome to leave a comment or write an email to the authors). It is
 * provided 'as is' and free for either commercial or non-commercial use."
 * ©Intel Corporation
 */

/*
 * Solaris Modifications:
 *
 * 1. Added comments
 *
 * 2. Translate yasm/nasm %define and .macro definitions to cpp(1) #define
 * Made #define names UPPER CASE and labels lower case.
 *
 * 3. Translate yasm/nasm %ifdef/%ifndef to cpp(1) #ifdef
 *
 * 4. Translate Intel/yasm/nasm syntax to ATT/Solaris as(1) syntax
 * (operands reversed, literals prefixed with "$", registers prefixed with "%",
 * and "[register+offset]", addressing changed to "offset(register)",
 * parenthesis in constant expressions "()" changed to square brackets "[]",
 * "." removed from  local (numeric) labels, and other changes.
 * Examples:
 * Intel/yasm/nasm Syntax       ATT/gas/Solaris Syntax
 * section .text		.text
 * align 16			.align 16
 * global foobar		.global foobar
 * multiblock_begin:		.Lmultiblock_begin:  // Local label
 * mov   rax, (4*20h)		mov   $[4*0x20], %rax
 * mov   rax, [ebx+20h]		mov   0x20(%ebx), %rax
 * lea   rax, [ebx+ecx]		lea   (%ebx, %ecx), %rax
 * lea   r8, [k_xmm_ar]		lea   k_xmm_ar(%rip), %r8
 * sub   rax, [ebx +ecx*4 -20h]	sub   -0x20(%ebx, %ecx, 4), %rax
 * cmp   r11, r10		cmp   %r10, %r11
 * db    255, 0			.byte 255, 0
 * dd    012345678h		.long 0x12345678
 *
 * 5. Added Solaris ENTRY_NP/SET_SIZE macros from
 * /usr/include/sys/asm_linkage.h, lint(1B) guards, and dummy C function
 * definitions for lint.
 *
 * 6. Removed MS Windows ABI code (identified by %ifdef WIN_ABI)
 *
 * 7. Remove code to detect if SSSE3 support and to call an alternate
 * "dispatch" function when SSSE3 isn't supported.
 *
 * 8. Moved data to .text segment; made symbols internal (with "."), and moved
 * data definitions towards the end of this file.
 *
 * 9. Added USE_AVX to conditionally select SSSE3 instructions or AVX
 * instructions at compile time.
 *
 * 10. Changed default INTEL_SHA1_UPDATE_FUNCNAME from sha1_update_intel to
 * sha1_1block-data_order_ssse3 and sha1_block_data_order_ssse3 (for SSSE3),
 * and sha1_1block-data_order_ssse3 and sha1_block_data_order_ssse3 (for AVX).
 *
 * 11. Push/pop register %r12 on the stack (used to save/restore %rsp).
 * For the kernel, also push/pop %r13 on the stack (used to save/restore cr0).
 *
 * For background information about this source, see
 * "Improving the Performance of the Secure Hash Algorithm (SHA-1)"
 * by Max Locktyukhin.  Published March 31, 2010:
 * http://software.intel.com/en-us/articles/
 *     improving-the-performance-of-the-secure-hash-algorithm-1/
 */

/* Provide SHA-1 update function name here */
#ifndef INTEL_SHA1_SINGLEBLOCK
#define	MULTIBLOCK 1
#ifndef INTEL_SHA1_UPDATE_FUNCNAME
#ifndef USE_AVX
#define	INTEL_SHA1_UPDATE_FUNCNAME	sha1_block_data_order_ssse3
#else
#define	INTEL_SHA1_UPDATE_FUNCNAME	sha1_block_data_order_avx
#endif
#endif

#else /* single block */
#define	MULTIBLOCK 0
#ifndef INTEL_SHA1_UPDATE_FUNCNAME
#ifndef USE_AVX
#define	INTEL_SHA1_UPDATE_FUNCNAME	sha1_1block_data_order_ssse3
#else
#define	INTEL_SHA1_UPDATE_FUNCNAME	sha1_1block_data_order_avx
#endif
#endif
#endif

#if defined(lint) || defined(__lint)

#include <sys/types.h>
#include <sys/sha1.h>

/* ARGSUSED */
void
#if (MULTIBLOCK == 1)
INTEL_SHA1_UPDATE_FUNCNAME(SHA1_CTX *ctx, const void *inpp, size_t num_blocks)
#else
INTEL_SHA1_UPDATE_FUNCNAME(SHA1_CTX *ctx, const void *inpp)
#endif	/* MULTIBLOCK */
{
}

#else   /* lint */

#include <sys/asm_linkage.h>
#include <sys/controlregs.h>
#ifdef _KERNEL
#include <sys/machprivregs.h>
#endif

#ifdef _KERNEL
	/* Macros to save %xmm* registers in the kernel when necessary. */

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
 * If CR0_TS is not set, push %xmm0 - %xmm10 on stack,
 * otherwise clear CR0_TS.
 * Note: the stack must have been previously aligned 0 mod 16.
 */
#define	CLEAR_TS_OR_PUSH_XMM_REGISTERS(tmpreg) \
	movq	%cr0, tmpreg; \
	testq	$CR0_TS, tmpreg; \
	jnz	1f; \
	sub	$[XMM_SIZE * 11], %rsp; \
	movaps	%xmm0, 160(%rsp); \
	movaps	%xmm1, 144(%rsp); \
	movaps	%xmm2, 128(%rsp); \
	movaps	%xmm3, 112(%rsp); \
	movaps	%xmm4, 96(%rsp); \
	movaps	%xmm5, 80(%rsp); \
	movaps	%xmm6, 64(%rsp); \
	movaps	%xmm7, 48(%rsp); \
	movaps	%xmm8, 32(%rsp); \
	movaps	%xmm9, 16(%rsp); \
	movaps	%xmm10, (%rsp); \
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
	movaps	(%rsp), %xmm10; \
	movaps	16(%rsp), %xmm9; \
	movaps	32(%rsp), %xmm8; \
	movaps	48(%rsp), %xmm7; \
	movaps	64(%rsp), %xmm6; \
	movaps	80(%rsp), %xmm5; \
	movaps	96(%rsp), %xmm4; \
	movaps	112(%rsp), %xmm3; \
	movaps	128(%rsp), %xmm2; \
	movaps	144(%rsp), %xmm1; \
	movaps	160(%rsp), %xmm0; \
	jmp	2f; \
1: \
	STTS(tmpreg); \
2:

#else	/* userland */
#define	PROTECTED_CLTS
#define	CLEAR_TS_OR_PUSH_XMM_REGISTERS(tmpreg)
#define	SET_TS_OR_POP_XMM_REGISTERS(tmpreg)
#endif	/* _KERNEL */


// Function parameters
// UNIX ABI
#define	ARG1		%rdi
#define	ARG2		%rsi
#if (MULTIBLOCK == 1)
#define	ARG3		%rdx
#endif

#define	CTX		ARG1
#define	BUF		ARG2
#if (MULTIBLOCK == 1)
#define	CNT		ARG3
#endif

// Register allocation of temporary variables A, B, C, D, E, T1, T2
#define	A		%ecx
#define	B		%esi
#define	C		%edi
#define	D		%ebp
#define	E		%edx

#define	T1		%eax
#define	T2		%ebx

#define	K_BASE		%r8
#define	HASH_PTR	%r9
#define	BUFFER_PTR	%r10
#if (MULTIBLOCK == 1)
#define	BUFFER_END	%r11
#endif

#define	W_TMP		%xmm0
#define	W_TMP2		%xmm9

#define	W0		%xmm1
#define	W4		%xmm2
#define	W8		%xmm3
#define	W12		%xmm4
#define	W16		%xmm5
#define	W20		%xmm6
#define	W24		%xmm7
#define	W28		%xmm8

#define	XMM_SHUFB_BSWAP	%xmm10

#define	W_PRECALC_AHEAD	16

// We keep a window of 64 w[i]+K pre-calculated values in a circular buffer
// on the stack.
#define	WK(t) (((t) & 15) * 4)

#define	UPDATE_HASH(P1, P2) \
	add	P1, P2; \
	mov	P2, P1

//
// Non-linear function, F, varies according to the round.
//

// F1(X, Y, Z) = (X & Y) | ((~X) & Z) for rounds 0 to 19
#define	F1(P1, P2, P3) \
	mov	P2, T1; \
	xor	P3, T1; \
	and	P1, T1; \
	xor	P3, T1

// F2(X, Y, Z) = X ^ Y ^ Z for rounds 20 to 39
#define	F2(P1, P2, P3) \
	mov	P3, T1; \
	xor	P2, T1; \
	xor	P1, T1

// F3(X, Y, Z) = (X & Y) | (X & Z) | (Y & Z) for rounds 40 to 59
#define	F3(P1, P2, P3) \
	mov	P2, T1; \
	mov	P1, T2; \
	or	P1, T1; \
	and	P2, T2; \
	and	P3, T1; \
	or	T2, T1

// F2(X, Y, Z) = X ^ Y ^ Z for rounds 60 to 79
#define	F4 F2

#ifndef	USE_AVX
//
// SSSE3
//
#define	W_PRECALC_00_15_0MOD4(I)	W_PRECALC_00_15_0MOD4_SSSE3(I)
#define	W_PRECALC_00_15_1MOD4		W_PRECALC_00_15_1MOD4_SSSE3
#define	W_PRECALC_00_15_2MOD4		W_PRECALC_00_15_2MOD4_SSSE3
#define	W_PRECALC_00_15_3MOD4(I)	W_PRECALC_00_15_3MOD4_SSSE3(I)
#define	W_PRECALC_16_31_0MOD4		W_PRECALC_16_31_0MOD4_SSSE3
#define	W_PRECALC_16_31_1MOD4		W_PRECALC_16_31_1MOD4_SSSE3
#define	W_PRECALC_16_31_2MOD4		W_PRECALC_16_31_2MOD4_SSSE3
#define	W_PRECALC_16_31_3MOD4(I, K_XMM)	W_PRECALC_16_31_3MOD4_SSSE3(I, K_XMM)
#define	W_PRECALC_32_79_0MOD4		W_PRECALC_32_79_0MOD4_SSSE3
#define	W_PRECALC_32_79_1MOD4		W_PRECALC_32_79_1MOD4_SSSE3
#define	W_PRECALC_32_79_2MOD4		W_PRECALC_32_79_2MOD4_SSSE3
#define	W_PRECALC_32_79_3MOD4(I, K_XMM)	W_PRECALC_32_79_3MOD4_SSSE3(I, K_XMM)
#define	RR0(A, B, C, D, E, STEP)	RR0_SSSE3(A, B, C, D, E, STEP)
#define	RR1(A, B, C, D, E)		RR1_SSSE3(A, B, C, D, E)

#define	XMM_MOV		movdqa

//
// Macros to pre-calculate rounds 0 - 15
//

// Message scheduling pre-compute for rounds 0-15, round mod 4 == 0.
// Scheduling to interleave with ALUs.
// 1 vector iteration per 4 rounds.
#define	W_PRECALC_00_15_0MOD4_SSSE3(i) \
	movdqu	(i) * 4(BUFFER_PTR), W_TMP

// Message scheduling pre-compute for rounds 0-15, round mod 4 == 1.
#define	W_PRECALC_00_15_1MOD4_SSSE3 \
	pshufb	XMM_SHUFB_BSWAP, W_TMP; \
	movdqa	W_TMP, W

// Message scheduling pre-compute for rounds 0-15, round mod 4 == 2.
#define	W_PRECALC_00_15_2MOD4_SSSE3 \
	paddd	(K_BASE), W_TMP

// Message scheduling pre-compute for rounds 0-15, round mod 4 == 3.
#define	W_PRECALC_00_15_3MOD4_SSSE3(i) \
	movdqa	W_TMP, WK((i) & ~3)(%rsp)

//
// Macros to pre-calculate rounds 16 - 31
//
// Calculating last 32 w[i] values in 8 XMM registers.
// Pre-calculate K+w[i] values and store to memory, for later load by
// ALU add instruction.
//
// Some "heavy-lifting" vectorization for rounds 16-31 due to
// w[i]->w[i-3] dependency, but improves for rounds 32-79.
//
// Scheduling to interleave with ALUs.
// 1 vector iteration per 4 rounds.
// palignr	w[i-14]
// psrldq	w[i-3]
//

// Message scheduling pre-compute for rounds 16-31, round mod 4 == 0
#define	W_PRECALC_16_31_0MOD4_SSSE3 \
	movdqa	W_MINUS_12, W; \
	palignr	$8, W_MINUS_16, W; \
	movdqa	W_MINUS_04, W_TMP; \
	psrldq	$4, W_TMP; \
	pxor	W_MINUS_08, W

// Message scheduling pre-compute for rounds 16-31, round mod 4 == 1
#define	W_PRECALC_16_31_1MOD4_SSSE3 \
	pxor	W_MINUS_16, W_TMP; \
	pxor	W_TMP, W; \
	movdqa	W, W_TMP2; \
	movdqa	W, W_TMP; \
	pslldq	$12, W_TMP2

// Message scheduling pre-compute for rounds 16-31, round mod 4 == 2
#define	W_PRECALC_16_31_2MOD4_SSSE3 \
	psrld	$31, W; \
	pslld	$1, W_TMP; \
	por	W, W_TMP; \
	movdqa	W_TMP2, W; \
	psrld	$30, W_TMP2; \
	pslld	$2, W

// Message scheduling pre-compute for rounds 16-31, round mod 4 == 3
#define	W_PRECALC_16_31_3MOD4_SSSE3(i, K_XMM) \
	pxor	W, W_TMP; \
	pxor	W_TMP2, W_TMP; \
	movdqa	W_TMP, W; \
	paddd	(K_XMM)(K_BASE), W_TMP; \
	movdqa	W_TMP, WK((i) & ~3)(%rsp)

//
// Macros to pre-calculate rounds 32 - 79
//
// In SHA-1 specification: w[i] = (w[i-3] ^ w[i-8]  ^ w[i-14] ^ w[i-16]) rol 1.
// Instead we do equal:    w[i] = (w[i-6] ^ w[i-16] ^ w[i-28] ^ w[i-32]) rol 2.
// Allows more efficient vectorization since w[i]=>w[i-3] dependency is broken.
//
// Scheduling to interleave with ALUs.
// 1 vector iteration per 4 rounds.
// W is W_MINUS_32 before xor.
//

// Message scheduling pre-compute for rounds 32-79, round mod 4 == 0.
#define	W_PRECALC_32_79_0MOD4_SSSE3 \
	movdqa	W_MINUS_04, W_TMP; \
	pxor	W_MINUS_28, W; \
	palignr	$8, W_MINUS_08, W_TMP

// Message scheduling pre-compute for rounds 32-79, round mod 4 == 1.
#define	W_PRECALC_32_79_1MOD4_SSSE3 \
	pxor	W_MINUS_16, W; \
	pxor	W_TMP, W; \
	movdqa	W, W_TMP

// Message scheduling pre-compute for rounds 32-79, round mod 4 == 2.
#define	W_PRECALC_32_79_2MOD4_SSSE3 \
	psrld	$30, W; \
	pslld	$2, W_TMP; \
	por	W, W_TMP

// Message scheduling pre-compute for rounds 32-79, round mod 4 == 3.
#define	W_PRECALC_32_79_3MOD4_SSSE3(i, K_XMM) \
	movdqa	W_TMP, W; \
	paddd	(K_XMM)(K_BASE), W_TMP; \
	movdqa	W_TMP, WK((i) & ~3)(%rsp)

//
// RR0_SSSE3 does the first of two rounds of SHA-1 back to back with
// W pre-calculation.
// One round:
//	TEMP = A
//	A = F( i, B, C, D ) + E + ROTATE_LEFT( A, 5 ) + W[i] + K(i)
//	C = ROTATE_LEFT( B, 30 )
//	D = C
//	E = D
//	B = TEMP
//
// F returns result in T1
// write:  A, B
// rotate: A<=D, B<=E, C<=A, D<=B, E<=C
//
// Parameters:
// A, B		Modified and rotated variables
// C, D, E	Rotated variables
// i		Round # (round 0 - 78)
//
#define	RR0_SSSE3(A, B, C, D, E, STEP) \
	F(B, C, D); \
	add	WK(STEP)(%rsp), E; \
	rol	$30, B; \
	mov	A, T2; \
	add	WK((STEP) + 1)(%rsp), D; \
	rol	$5, T2; \
	add	T1, E


//
// RR1_SSSE3 does the second of two rounds of SHA-1 back to back with
// W pre-calculation.
// One round:
//	TEMP = A
//	A = F( i, B, C, D ) + E + ROTATE_LEFT( A, 5 ) + W[i] + K(i)
//	C = ROTATE_LEFT( B, 30 )
//	D = C
//	E = D
//	B = TEMP
//
// F returns result in T1
// write:  A, B
// rotate: A<=D, B<=E, C<=A, D<=B, E<=C
//
// Parameters:
// A, B		Modified and rotated variables
// C, D, E	Rotated variables
//
#define	RR1_SSSE3(A, B, C, D, E) \
	add	E, T2; \
	mov	T2, E; \
	rol	$5, T2; \
	add	T2, D; \
	F(A, B, C); \
	add	T1, D; \
	rol	$30, A


#else	/* USE_AVX */
//
// AVX
//
#define	W_PRECALC_00_15_0MOD4(I)	W_PRECALC_00_15_0MOD4_AVX(I)
#define	W_PRECALC_00_15_1MOD4		W_PRECALC_00_15_1MOD4_AVX
#define	W_PRECALC_00_15_2MOD4		W_PRECALC_00_15_2MOD4_AVX
#define	W_PRECALC_00_15_3MOD4(I)	W_PRECALC_00_15_3MOD4_AVX(I)
#define	W_PRECALC_16_31_0MOD4		W_PRECALC_16_31_0MOD4_AVX
#define	W_PRECALC_16_31_1MOD4		W_PRECALC_16_31_1MOD4_AVX
#define	W_PRECALC_16_31_2MOD4		W_PRECALC_16_31_2MOD4_AVX
#define	W_PRECALC_16_31_3MOD4(I, K_XMM)	W_PRECALC_16_31_3MOD4_AVX(I, K_XMM)
#define	W_PRECALC_32_79_0MOD4		W_PRECALC_32_79_0MOD4_AVX
#define	W_PRECALC_32_79_1MOD4		W_PRECALC_32_79_1MOD4_AVX
#define	W_PRECALC_32_79_2MOD4		W_PRECALC_32_79_2MOD4_AVX
#define	W_PRECALC_32_79_3MOD4(I, K_XMM)	W_PRECALC_32_79_3MOD4_AVX(I, K_XMM)
#define	RR0(A, B, C, D, E, STEP)	RR0_AVX(A, B, C, D, E, STEP)
#define	RR1(A, B, C, D, E)		RR1_AVX(A, B, C, D, E)

// Same performance as vmovdqa but AVX recommends this to avoid faulting
// load and store forms:
#define	XMM_MOV		vmovdqu

//
// Macros to pre-calculate rounds 0 - 15
//
// Scheduling to interleave with ALUs.
// 1 vector iteration per 4 rounds.

// Message scheduling pre-compute for rounds 0-15, round mod 4 == 0
#define	W_PRECALC_00_15_0MOD4_AVX(i) \
	vmovdqu		(i) * 4(BUFFER_PTR), W_TMP

// Message scheduling pre-compute for rounds 0-15, round mod 4 == 1.
#define	W_PRECALC_00_15_1MOD4_AVX \
	vpshufb		XMM_SHUFB_BSWAP, W_TMP, W

// Message scheduling pre-compute for rounds 0-15, round mod 4 == 2.
#define	W_PRECALC_00_15_2MOD4_AVX \
	vpaddd		(K_BASE), W, W_TMP

// Message scheduling pre-compute for rounds 0-15, round mod 4 == 3.
#define	W_PRECALC_00_15_3MOD4_AVX(i) \
	vmovdqu		W_TMP, WK((i) & ~3)(%rsp)

//
// Macros to pre-calculate rounds 16 - 31
//
// Calculating last 32 w[i] values in 8 XMM registers.
// Pre-calculate K+w[i] values and store to memory, for later load by
// ALU add instruction.
//
// Some "heavy-lifting" vectorization for rounds 16-31 due to
// w[i]->w[i-3] dependency, but improves for rounds 32-79.
//
// Scheduling to interleave with ALUs.
// 1 vector iteration per 4 rounds.
// vpalignr	w[i-14]
// vpsrldq	w[i-3]
//

// Message scheduling pre-compute for rounds 16-31, round mod 4 == 0
#define	W_PRECALC_16_31_0MOD4_AVX \
	vpalignr	$8, W_MINUS_16, W_MINUS_12, W; \
	vpsrldq		$4, W_MINUS_04, W_TMP; \
	vpxor		W_MINUS_08, W, W; \
	vpxor		W_MINUS_16, W_TMP, W_TMP

// Message scheduling pre-compute for rounds 16-31, round mod 4 == 1
#define	W_PRECALC_16_31_1MOD4_AVX \
	vpxor		W_TMP, W, W; \
	vpslldq		$12, W, W_TMP2; \
	vpslld		$1, W, W_TMP

// Message scheduling pre-compute for rounds 16-31, round mod 4 == 2
#define	W_PRECALC_16_31_2MOD4_AVX \
	vpsrld		$31, W, W; \
	vpor		W, W_TMP, W_TMP; \
	vpslld		$2, W_TMP2, W; \
	vpsrld		$30, W_TMP2, W_TMP2

// Message scheduling pre-compute for rounds 16-31, round mod 4 == 3
#define	W_PRECALC_16_31_3MOD4_AVX(i, K_XMM) \
	vpxor		W, W_TMP, W_TMP; \
	vpxor		W_TMP2, W_TMP, W; \
	vpaddd		(K_XMM)(K_BASE), W, W_TMP; \
	vmovdqu		W_TMP, WK((i) & ~3)(%rsp)

//
// Macros to pre-calculate rounds 32 - 79
//
// In SHA-1 specification: w[i] = (w[i-3] ^ w[i-8]  ^ w[i-14] ^ w[i-16]) rol 1.
// Instead we do equal:    w[i] = (w[i-6] ^ w[i-16] ^ w[i-28] ^ w[i-32]) rol 2.
// Allows more efficient vectorization since w[i]=>w[i-3] dependency is broken.
//
// Scheduling to interleave with ALUs.
// 1 vector iteration per 4 rounds.
// W is W_MINUS_32 before xor.
//

// Message scheduling pre-compute for rounds 32-79, round mod 4 == 0.
#define	W_PRECALC_32_79_0MOD4_AVX \
	vpalignr	$8, W_MINUS_08, W_MINUS_04, W_TMP; \
	vpxor		W_MINUS_28, W, W

// Message scheduling pre-compute for rounds 32-79, round mod 4 == 1.
#define	W_PRECALC_32_79_1MOD4_AVX \
	vpxor	W_MINUS_16, W_TMP, W_TMP; \
	vpxor	W_TMP, W, W

// Message scheduling pre-compute for rounds 32-79, round mod 4 == 2.
#define	W_PRECALC_32_79_2MOD4_AVX \
	vpslld		$2, W, W_TMP; \
	vpsrld		$30, W, W; \
	vpor		W, W_TMP, W

// Message scheduling pre-compute for rounds 32-79, round mod 4 == 3.
#define	W_PRECALC_32_79_3MOD4_AVX(i, K_XMM) \
	vpaddd		(K_XMM)(K_BASE), W, W_TMP; \
	vmovdqu		W_TMP, WK((i) & ~3)(%rsp)

//
// RR0_AVX does the first of two rounds of SHA-1 back to back with
// W pre-calculation.
// One round:
//	TEMP = A
//	A = F( i, B, C, D ) + E + ROTATE_LEFT( A, 5 ) + W[i] + K(i)
//	C = ROTATE_LEFT( B, 30 )
//	D = C
//	E = D
//	B = TEMP
//
// F returns result in T1
// write:  A, B
// rotate: A<=D, B<=E, C<=A, D<=B, E<=C
//
// Parameters:
// A, B		Modified and rotated variables
// C, D, E	Rotated variables
// i		Round # (round 0 - 78)
//
#define	RR0_AVX(A, B, C, D, E, STEP) \
	F(B, C, D); \
	add	WK(STEP)(%rsp), E; \
	shld	$30, B, B; \
	mov	A, T2; \
	add	WK((STEP) + 1)(%rsp), D; \
	shld	$5, T2, T2; \
	add	T1, E


//
// RR1_AVX does the second of two rounds of SHA-1 back to back with
// W pre-calculation.
// One round:
//	TEMP = A
//	A = F( i, B, C, D ) + E + ROTATE_LEFT( A, 5 ) + W[i] + K(i)
//	C = ROTATE_LEFT( B, 30 )
//	D = C
//	E = D
//	B = TEMP
//
// F returns result in T1
// write:  A, B
// rotate: A<=D, B<=E, C<=A, D<=B, E<=C
//
// Parameters:
// A, B		Modified and rotated variables
// C, D, E	Rotated variables
//
#define	RR1_AVX(A, B, C, D, E) \
	add	E, T2; \
	mov	T2, E; \
	shld	$5, T2, T2; \
	add	T2, D; \
	F(A, B, C); \
	add	T1, D; \
	shld	$30, A, A

#endif	/* USE_AVX */


// ---------------------------------------------------------------------------
.text
.align 4096

//
// Execution begins here.
//

//
// void sha1_block_data_order_ssse3(SHA1_CTX *ctx, const void *inpp,
//     size_t num_blocks);
// void sha1_1block_data_order_ssse3(SHA1_CTX *ctx, const void *inpp);
//
// Transform a single or several 64-byte blocks of data into the digest,
// updating the digest with the data block.
//
// 3rd function's argument, num_blocks, is a number, greater than 0, of
// 64-byte blocks to use to calculate the hash.
//
// MULTIBLOCK: = 0 - function implements single   64-byte block hash
//             = 1 - function implements multiple 64-byte block hash
//
// Use INTEL_SHA1_UPDATE_FUNCNAME to optionally redefine this function name.
//

.align 4096

ENTRY_NP(INTEL_SHA1_UPDATE_FUNCNAME)
	//
	// Setup and align stack
	//

	// Some registers must be callee-saved before they can be used:
	push	%rbx
	// Solaris additionally pushes %r12 and %r13 to save %rsp and %cr0.
	// We can't save %rsp in %rbp because %ebp is used as a temp.
	push	%r12
#ifdef _KERNEL
	push	%r13
#endif
	push	%rbp

	mov	%rsp, %r12
	and	$-XMM_ALIGN, %rsp

	CLEAR_TS_OR_PUSH_XMM_REGISTERS(%r13)

//
// We need ((16+1)*4) bytes on stack to save 64+4 pre-calculated values
// in a circular buffer.
// Round this amount up 0 mod 16 to keep stack aligned for movdqa instructions.
//
#define	RESERVE_STACK (20 * 4)
	sub     $[RESERVE_STACK], %rsp

	//
	// Copy parameters
	//
	mov	CTX, HASH_PTR	// P1
	mov	BUF, BUFFER_PTR	// P2

#if (MULTIBLOCK == 1)
	shl	$6, CNT		// multiply P3 by 64
	add	BUF, CNT
	mov	CNT, BUFFER_END
#endif

	lea	k_xmm_ar(%rip), K_BASE
	XMM_MOV	bswap_shufb_ctl(%rip), XMM_SHUFB_BSWAP


// -------------------------------------------
// 80 rounds of SHA-1, for one 64-byte block or multiple
// blocks with s/w pipelining
//
	mov	(HASH_PTR),   A
	mov	4(HASH_PTR),  B
	mov	8(HASH_PTR),  C
	mov	12(HASH_PTR), D
	mov	16(HASH_PTR), E

// Initialize window of precalculated values
#define	W		W0
#define	W_MINUS_04	W4
#define	W_MINUS_08	W8
#define	W_MINUS_12	W12
#define	W_MINUS_16	W16
#define	W_MINUS_20	W20
#define	W_MINUS_24	W24
#define	W_MINUS_28	W28
#define	W_MINUS_32	W

//
// Pre-calculate the first 16 rounds before starting block hashing:
//
	W_PRECALC_00_15_0MOD4(0)	// Pre-calculate round 0
	W_PRECALC_00_15_1MOD4		// Pre-calculate round 1
	W_PRECALC_00_15_2MOD4		// Pre-calculate round 2
	W_PRECALC_00_15_3MOD4(3)	// Pre-calculate round 3

// Rotate window of precalculated values
#undef	W_MINUS_32
#define	W_MINUS_32	W28
#undef	W_MINUS_28
#define	W_MINUS_28	W24
#undef	W_MINUS_24
#define	W_MINUS_24	W20
#undef	W_MINUS_20
#define	W_MINUS_20	W16
#undef	W_MINUS_16
#define	W_MINUS_16	W12
#undef	W_MINUS_12
#define	W_MINUS_12	W8
#undef	W_MINUS_08
#define	W_MINUS_08	W4
#undef	W_MINUS_04
#define	W_MINUS_04	W0
#undef	W
#define	W		W28

	W_PRECALC_00_15_0MOD4(4)	// Pre-calculate round 4
	W_PRECALC_00_15_1MOD4		// Pre-calculate round 5
	W_PRECALC_00_15_2MOD4		// Pre-calculate round 6
	W_PRECALC_00_15_3MOD4(7)	// Pre-calculate round 7

// Rotate window of precalculated values
#undef	W_MINUS_32
#define	W_MINUS_32	W24
#undef	W_MINUS_28
#define	W_MINUS_28	W20
#undef	W_MINUS_24
#define	W_MINUS_24	W16
#undef	W_MINUS_20
#define	W_MINUS_20	W12
#undef	W_MINUS_16
#define	W_MINUS_16	W8
#undef	W_MINUS_12
#define	W_MINUS_12	W4
#undef	W_MINUS_08
#define	W_MINUS_08	W0
#undef	W_MINUS_04
#define	W_MINUS_04	W28
#undef	W
#define	W		W24

	W_PRECALC_00_15_0MOD4(8)	// Pre-calculate round 8
	W_PRECALC_00_15_1MOD4		// Pre-calculate round 9
	W_PRECALC_00_15_2MOD4		// Pre-calculate round 10
	W_PRECALC_00_15_3MOD4(11)	// Pre-calculate round 11

// Rotate window of precalculated values
#undef	W_MINUS_32
#define	W_MINUS_32	W20
#undef	W_MINUS_28
#define	W_MINUS_28	W16
#undef	W_MINUS_24
#define	W_MINUS_24	W12
#undef	W_MINUS_20
#define	W_MINUS_20	W8
#undef	W_MINUS_16
#define	W_MINUS_16	W4
#undef	W_MINUS_12
#define	W_MINUS_12	W0
#undef	W_MINUS_08
#define	W_MINUS_08	W28
#undef	W_MINUS_04
#define	W_MINUS_04	W24
#undef	W
#define	W		W20

	W_PRECALC_00_15_0MOD4(12)	// Pre-calculate round 12
	W_PRECALC_00_15_1MOD4		// Pre-calculate round 13
	W_PRECALC_00_15_2MOD4		// Pre-calculate round 14
	W_PRECALC_00_15_3MOD4(15)	// Pre-calculate round 15

// Rotate window of precalculated values
#undef	W_MINUS_32
#define	W_MINUS_32	W16
#undef	W_MINUS_28
#define	W_MINUS_28	W12
#undef	W_MINUS_24
#define	W_MINUS_24	W8
#undef	W_MINUS_20
#define	W_MINUS_20	W4
#undef	W_MINUS_16
#define	W_MINUS_16	W0
#undef	W_MINUS_12
#define	W_MINUS_12	W28
#undef	W_MINUS_08
#define	W_MINUS_08	W24
#undef	W_MINUS_04
#define	W_MINUS_04	W20
#undef	W
#define	W		W16


#define	F F1

#if (MULTIBLOCK == 1) /* code loops through more than one block */
.align 32
.Lmultiblock_loop:
	//
	// Process data in successive 64-byte blocks.
	// Break each 64-byte block into an array of 16 32-bit words.
	//

	// We use K_BASE value as a signal of a last block, which is
	// set below by: cmovae K_BASE, BUFFER_PTR
	cmp	K_BASE, BUFFER_PTR
	jne	.Lmultiblock_begin
.align 32
	jmp	.Lmultiblock_end

.align 32
.Lmultiblock_begin:
#endif
	W_PRECALC_16_31_0MOD4		// Pre-calculate round 16
	RR0(A, B, C, D, E, 0)		// Round 0
	W_PRECALC_16_31_1MOD4		// Pre-calculate round 17
	RR1(A, B, C, D, E)		// Round 1
	W_PRECALC_16_31_2MOD4		// Pre-calculate round 18
	RR0(D, E, A, B, C, 2)		// Round 2
	W_PRECALC_16_31_3MOD4(19, 0)	// Pre-calculate round 19

// Rotate window of precalculated values
#undef	W_MINUS_32
#define	W_MINUS_32	W12
#undef	W_MINUS_28
#define	W_MINUS_28	W8
#undef	W_MINUS_24
#define	W_MINUS_24	W4
#undef	W_MINUS_20
#define	W_MINUS_20	W0
#undef	W_MINUS_16
#define	W_MINUS_16	W28
#undef	W_MINUS_12
#define	W_MINUS_12	W24
#undef	W_MINUS_08
#define	W_MINUS_08	W20
#undef	W_MINUS_04
#define	W_MINUS_04	W16
#undef	W
#define	W		W12

	RR1(D, E, A, B, C)		// Round 3
	W_PRECALC_16_31_0MOD4		// Pre-calculate round 20
	RR0(B, C, D, E, A, 4)		// Round 4
	W_PRECALC_16_31_1MOD4		// Pre-calculate round 21
	RR1(B, C, D, E, A)		// Round 5
	W_PRECALC_16_31_2MOD4		// Pre-calculate round 22
	RR0(E, A, B, C, D, 6)		// Round 6
	W_PRECALC_16_31_3MOD4(23, 16)	// Pre-calculate round 23

// Rotate window of precalculated values
#undef	W_MINUS_32
#define	W_MINUS_32	W8
#undef	W_MINUS_28
#define	W_MINUS_28	W4
#undef	W_MINUS_24
#define	W_MINUS_24	W0
#undef	W_MINUS_20
#define	W_MINUS_20	W28
#undef	W_MINUS_16
#define	W_MINUS_16	W24
#undef	W_MINUS_12
#define	W_MINUS_12	W20
#undef	W_MINUS_08
#define	W_MINUS_08	W16
#undef	W_MINUS_04
#define	W_MINUS_04	W12
#undef	W
#define	W		W8

	RR1(E, A, B, C, D)		// Round 7
	W_PRECALC_16_31_0MOD4		// Pre-calculate round 24
	RR0(C, D, E, A, B, 8)		// Round 8
	W_PRECALC_16_31_1MOD4		// Pre-calculate round 25
	RR1(C, D, E, A, B)		// Round 9
	W_PRECALC_16_31_2MOD4		// Pre-calculate round 26
	RR0(A, B, C, D, E, 10)		// Round 10
	W_PRECALC_16_31_3MOD4(27, 16)	// Pre-calculate round 27

// Rotate window of precalculated values
#undef	W_MINUS_32
#define	W_MINUS_32	W4
#undef	W_MINUS_28
#define	W_MINUS_28	W0
#undef	W_MINUS_24
#define	W_MINUS_24	W28
#undef	W_MINUS_20
#define	W_MINUS_20	W24
#undef	W_MINUS_16
#define	W_MINUS_16	W20
#undef	W_MINUS_12
#define	W_MINUS_12	W16
#undef	W_MINUS_08
#define	W_MINUS_08	W12
#undef	W_MINUS_04
#define	W_MINUS_04	W8
#undef	W
#define	W		W4

	RR1(A, B, C, D, E)		// Round 11
	W_PRECALC_16_31_0MOD4		// Pre-calculate round 28
	RR0(D, E, A, B, C, 12)		// Round 12
	W_PRECALC_16_31_1MOD4		// Pre-calculate round 29
	RR1(D, E, A, B, C)		// Round 13
	W_PRECALC_16_31_2MOD4		// Pre-calculate round 30
	RR0(B, C, D, E, A, 14)		// Round 14
	W_PRECALC_16_31_3MOD4(31, 16)	// Pre-calculate round 31

// Rotate window of precalculated values
#undef	W_MINUS_32
#define	W_MINUS_32	W0
#undef	W_MINUS_28
#define	W_MINUS_28	W28
#undef	W_MINUS_24
#define	W_MINUS_24	W24
#undef	W_MINUS_20
#define	W_MINUS_20	W20
#undef	W_MINUS_16
#define	W_MINUS_16	W16
#undef	W_MINUS_12
#define	W_MINUS_12	W12
#undef	W_MINUS_08
#define	W_MINUS_08	W8
#undef	W_MINUS_04
#define	W_MINUS_04	W4
#undef	W
#define	W		W0

	RR1(B, C, D, E, A)		// Round 15
	W_PRECALC_32_79_0MOD4		// Pre-calculate round 32
	RR0(E, A, B, C, D, 16)		// Round 16
	W_PRECALC_32_79_1MOD4		// Pre-calculate round 33
	RR1(E, A, B, C, D)		// Round 17
	W_PRECALC_32_79_2MOD4		// Pre-calculate round 34
	RR0(C, D, E, A, B, 18)		// Round 18
	W_PRECALC_32_79_3MOD4(35, 16)	// Pre-calculate round 35

// Rotate window of precalculated values
#undef	W_MINUS_32
#define	W_MINUS_32	W28
#undef	W_MINUS_28
#define	W_MINUS_28	W24
#undef	W_MINUS_24
#define	W_MINUS_24	W20
#undef	W_MINUS_20
#define	W_MINUS_20	W16
#undef	W_MINUS_16
#define	W_MINUS_16	W12
#undef	W_MINUS_12
#define	W_MINUS_12	W8
#undef	W_MINUS_08
#define	W_MINUS_08	W4
#undef	W_MINUS_04
#define	W_MINUS_04	W0
#undef	W
#define	W		W28

	RR1(C, D, E, A, B)		// Round 19

#undef	F
#define	F F2

	W_PRECALC_32_79_0MOD4		// Pre-calculate round 36
	RR0(A, B, C, D, E, 20)		// Round 20
	W_PRECALC_32_79_1MOD4		// Pre-calculate round 37
	RR1(A, B, C, D, E)		// Round 21
	W_PRECALC_32_79_2MOD4		// Pre-calculate round 38
	RR0(D, E, A, B, C, 22)		// Round 22
	W_PRECALC_32_79_3MOD4(39, 16)	// Pre-calculate round 39

// Rotate window of precalculated values
#undef	W_MINUS_32
#define	W_MINUS_32	W24
#undef	W_MINUS_28
#define	W_MINUS_28	W20
#undef	W_MINUS_24
#define	W_MINUS_24	W16
#undef	W_MINUS_20
#define	W_MINUS_20	W12
#undef	W_MINUS_16
#define	W_MINUS_16	W8
#undef	W_MINUS_12
#define	W_MINUS_12	W4
#undef	W_MINUS_08
#define	W_MINUS_08	W0
#undef	W_MINUS_04
#define	W_MINUS_04	W28
#undef	W
#define	W		W24

	RR1(D, E, A, B, C)		// Round 23
	W_PRECALC_32_79_0MOD4		// Pre-calculate round 40
	RR0(B, C, D, E, A, 24)		// Round 24
	W_PRECALC_32_79_1MOD4		// Pre-calculate round 41
	RR1(B, C, D, E, A)		// Round 25
	W_PRECALC_32_79_2MOD4		// Pre-calculate round 42
	RR0(E, A, B, C, D, 26)		// Round 26
	W_PRECALC_32_79_3MOD4(43, 32)	// Pre-calculate round 43

// Rotate window of precalculated values
#undef	W_MINUS_32
#define	W_MINUS_32	W20
#undef	W_MINUS_28
#define	W_MINUS_28	W16
#undef	W_MINUS_24
#define	W_MINUS_24	W12
#undef	W_MINUS_20
#define	W_MINUS_20	W8
#undef	W_MINUS_16
#define	W_MINUS_16	W4
#undef	W_MINUS_12
#define	W_MINUS_12	W0
#undef	W_MINUS_08
#define	W_MINUS_08	W28
#undef	W_MINUS_04
#define	W_MINUS_04	W24
#undef	W
#define	W		W20

	RR1(E, A, B, C, D)		// Round 27
	W_PRECALC_32_79_0MOD4		// Pre-calculate round 44
	RR0(C, D, E, A, B, 28)		// Round 28
	W_PRECALC_32_79_1MOD4		// Pre-calculate round 45
	RR1(C, D, E, A, B)		// Round 29

	W_PRECALC_32_79_2MOD4		// Pre-calculate round 46
	RR0(A, B, C, D, E, 30)		// Round 30
	W_PRECALC_32_79_3MOD4(47, 32)	// Pre-calculate round 47

// Rotate window of precalculated values
#undef	W_MINUS_32
#define	W_MINUS_32	W16
#undef	W_MINUS_28
#define	W_MINUS_28	W12
#undef	W_MINUS_24
#define	W_MINUS_24	W8
#undef	W_MINUS_20
#define	W_MINUS_20	W4
#undef	W_MINUS_16
#define	W_MINUS_16	W0
#undef	W_MINUS_12
#define	W_MINUS_12	W28
#undef	W_MINUS_08
#define	W_MINUS_08	W24
#undef	W_MINUS_04
#define	W_MINUS_04	W20
#undef	W
#define	W		W16

	RR1(A, B, C, D, E)		// Round 31
	W_PRECALC_32_79_0MOD4		// Pre-calculate round 48
	RR0(D, E, A, B, C, 32)		// Round 32
	W_PRECALC_32_79_1MOD4		// Pre-calculate round 49
	RR1(D, E, A, B, C)		// Round 33
	W_PRECALC_32_79_2MOD4		// Pre-calculate round 50
	RR0(B, C, D, E, A, 34)		// Round 34
	W_PRECALC_32_79_3MOD4(51, 32)	// Pre-calculate round 51

// Rotate window of precalculated values
#undef	W_MINUS_32
#define	W_MINUS_32	W12
#undef	W_MINUS_28
#define	W_MINUS_28	W8
#undef	W_MINUS_24
#define	W_MINUS_24	W4
#undef	W_MINUS_20
#define	W_MINUS_20	W0
#undef	W_MINUS_16
#define	W_MINUS_16	W28
#undef	W_MINUS_12
#define	W_MINUS_12	W24
#undef	W_MINUS_08
#define	W_MINUS_08	W20
#undef	W_MINUS_04
#define	W_MINUS_04	W16
#undef	W
#define	W		W12

	RR1(B, C, D, E, A)		// Round 35
	W_PRECALC_32_79_0MOD4		// Pre-calculate round 52
	RR0(E, A, B, C, D, 36)		// Round 36
	W_PRECALC_32_79_1MOD4		// Pre-calculate round 53
	RR1(E, A, B, C, D)		// Round 37
	W_PRECALC_32_79_2MOD4		// Pre-calculate round 54
	RR0(C, D, E, A, B, 38)		// Round 38
	W_PRECALC_32_79_3MOD4(55, 32)	// Pre-calculate round 55

// Rotate window of precalculated values
#undef	W_MINUS_32
#define	W_MINUS_32	W8
#undef	W_MINUS_28
#define	W_MINUS_28	W4
#undef	W_MINUS_24
#define	W_MINUS_24	W0
#undef	W_MINUS_20
#define	W_MINUS_20	W28
#undef	W_MINUS_16
#define	W_MINUS_16	W24
#undef	W_MINUS_12
#define	W_MINUS_12	W20
#undef	W_MINUS_08
#define	W_MINUS_08	W16
#undef	W_MINUS_04
#define	W_MINUS_04	W12
#undef	W
#define	W		W8

	RR1(C, D, E, A, B)		// Round 39

#undef	F
#define	F F3

	W_PRECALC_32_79_0MOD4		// Pre-calculate round 56
	RR0(A, B, C, D, E, 40)		// Round 40
	W_PRECALC_32_79_1MOD4		// Pre-calculate round 57
	RR1(A, B, C, D, E)		// Round 41
	W_PRECALC_32_79_2MOD4		// Pre-calculate round 58
	RR0(D, E, A, B, C, 42)		// Round 42
	W_PRECALC_32_79_3MOD4(59, 32)	// Pre-calculate round 59

// Rotate window of precalculated values
#undef	W_MINUS_32
#define	W_MINUS_32	W4
#undef	W_MINUS_28
#define	W_MINUS_28	W0
#undef	W_MINUS_24
#define	W_MINUS_24	W28
#undef	W_MINUS_20
#define	W_MINUS_20	W24
#undef	W_MINUS_16
#define	W_MINUS_16	W20
#undef	W_MINUS_12
#define	W_MINUS_12	W16
#undef	W_MINUS_08
#define	W_MINUS_08	W12
#undef	W_MINUS_04
#define	W_MINUS_04	W8
#undef	W
#define	W		W4

	RR1(D, E, A, B, C)		// Round 43
	W_PRECALC_32_79_0MOD4		// Pre-calculate round 60
	RR0(B, C, D, E, A, 44)		// Round 44
	W_PRECALC_32_79_1MOD4		// Pre-calculate round 61
	RR1(B, C, D, E, A)		// Round 45
	W_PRECALC_32_79_2MOD4		// Pre-calculate round 62
	RR0(E, A, B, C, D, 46)		// Round 46
	W_PRECALC_32_79_3MOD4(63, 48)	// Pre-calculate round 63

// Rotate window of precalculated values
#undef	W_MINUS_32
#define	W_MINUS_32	W0
#undef	W_MINUS_28
#define	W_MINUS_28	W28
#undef	W_MINUS_24
#define	W_MINUS_24	W24
#undef	W_MINUS_20
#define	W_MINUS_20	W20
#undef	W_MINUS_16
#define	W_MINUS_16	W16
#undef	W_MINUS_12
#define	W_MINUS_12	W12
#undef	W_MINUS_08
#define	W_MINUS_08	W8
#undef	W_MINUS_04
#define	W_MINUS_04	W4
#undef	W
#define	W		W0

	RR1(E, A, B, C, D)		// Round 47
	W_PRECALC_32_79_0MOD4		// Pre-calculate round 64
	RR0(C, D, E, A, B, 48)		// Round 48
	W_PRECALC_32_79_1MOD4		// Pre-calculate round 65
	RR1(C, D, E, A, B)		// Round 49

	W_PRECALC_32_79_2MOD4		// Pre-calculate round 66
	RR0(A, B, C, D, E, 50)		// Round 50
	W_PRECALC_32_79_3MOD4(67, 48)	// Pre-calculate round 67

// Rotate window of precalculated values
#undef	W_MINUS_32
#define	W_MINUS_32	W28
#undef	W_MINUS_28
#define	W_MINUS_28	W24
#undef	W_MINUS_24
#define	W_MINUS_24	W20
#undef	W_MINUS_20
#define	W_MINUS_20	W16
#undef	W_MINUS_16
#define	W_MINUS_16	W12
#undef	W_MINUS_12
#define	W_MINUS_12	W8
#undef	W_MINUS_08
#define	W_MINUS_08	W4
#undef	W_MINUS_04
#define	W_MINUS_04	W0
#undef	W
#define	W		W28

	RR1(A, B, C, D, E)		// Round 51
	W_PRECALC_32_79_0MOD4		// Pre-calculate round 68
	RR0(D, E, A, B, C, 52)		// Round 52
	W_PRECALC_32_79_1MOD4		// Pre-calculate round 69
	RR1(D, E, A, B, C)		// Round 53
	W_PRECALC_32_79_2MOD4		// Pre-calculate round 70
	RR0(B, C, D, E, A, 54)		// Round 54
	W_PRECALC_32_79_3MOD4(71, 48)	// Pre-calculate round 71

// Rotate window of precalculated values
#undef	W_MINUS_32
#define	W_MINUS_32	W24
#undef	W_MINUS_28
#define	W_MINUS_28	W20
#undef	W_MINUS_24
#define	W_MINUS_24	W16
#undef	W_MINUS_20
#define	W_MINUS_20	W12
#undef	W_MINUS_16
#define	W_MINUS_16	W8
#undef	W_MINUS_12
#define	W_MINUS_12	W4
#undef	W_MINUS_08
#define	W_MINUS_08	W0
#undef	W_MINUS_04
#define	W_MINUS_04	W28
#undef	W
#define	W		W24

	RR1(B, C, D, E, A)		// Round 55
	W_PRECALC_32_79_0MOD4		// Pre-calculate round 72
	RR0(E, A, B, C, D, 56)		// Round 56
	W_PRECALC_32_79_1MOD4		// Pre-calculate round 73
	RR1(E, A, B, C, D)		// Round 57
	W_PRECALC_32_79_2MOD4		// Pre-calculate round 74
	RR0(C, D, E, A, B, 58)		// Round 58
	W_PRECALC_32_79_3MOD4(75, 48)	// Pre-calculate round 75

// Rotate window of precalculated values
#undef	W_MINUS_32
#define	W_MINUS_32	W20
#undef	W_MINUS_28
#define	W_MINUS_28	W16
#undef	W_MINUS_24
#define	W_MINUS_24	W12
#undef	W_MINUS_20
#define	W_MINUS_20	W8
#undef	W_MINUS_16
#define	W_MINUS_16	W4
#undef	W_MINUS_12
#define	W_MINUS_12	W0
#undef	W_MINUS_08
#define	W_MINUS_08	W28
#undef	W_MINUS_04
#define	W_MINUS_04	W24
#undef	W
#define	W		W20

	RR1(C, D, E, A, B)		// Round 59

#undef	F
#define	F F4

#if (MULTIBLOCK == 1)	/* If code loops through more than one block. */
	add   $64, BUFFER_PTR	     // Move to next 64-byte block.
	cmp   BUFFER_END, BUFFER_PTR // Check if current block is the last one.
	cmovae K_BASE, BUFFER_PTR    // Smart way to signal the last iteration,
				     // dummy next block.
#define	NO_TAIL_PRECALC 0

#else
	// If single block interface, no pre-compute for a next iteration.
#define	NO_TAIL_PRECALC 1
#endif

	W_PRECALC_32_79_0MOD4		// Pre-calculate round 76
	RR0(A, B, C, D, E, 60)		// Round 60
	W_PRECALC_32_79_1MOD4		// Pre-calculate round 77
	RR1(A, B, C, D, E)		// Round 61
	W_PRECALC_32_79_2MOD4		// Pre-calculate round 78
	RR0(D, E, A, B, C, 62)		// Round 62
	W_PRECALC_32_79_3MOD4(79, 48)	// Pre-calculate round 79

// Rotate window of precalculated values
#undef	W_MINUS_32
#define	W_MINUS_32	W16
#undef	W_MINUS_28
#define	W_MINUS_28	W12
#undef	W_MINUS_24
#define	W_MINUS_24	W8
#undef	W_MINUS_20
#define	W_MINUS_20	W4
#undef	W_MINUS_16
#define	W_MINUS_16	W0
#undef	W_MINUS_12
#define	W_MINUS_12	W28
#undef	W_MINUS_08
#define	W_MINUS_08	W24
#undef	W_MINUS_04
#define	W_MINUS_04	W20
#undef	W
#define	W		W16

	RR1(D, E, A, B, C)		// Round 63

#if NO_TAIL_PRECALC == 0
// Reset window of precalculated values
#undef	W
#define	W		W0
#undef	W_MINUS_04
#define	W_MINUS_04	W4
#undef	W_MINUS_08
#define	W_MINUS_08	W8
#undef	W_MINUS_12
#define	W_MINUS_12	W12
#undef	W_MINUS_16
#define	W_MINUS_16	W16
#undef	W_MINUS_20
#define	W_MINUS_20	W20
#undef	W_MINUS_24
#define	W_MINUS_24	W24
#undef	W_MINUS_28
#define	W_MINUS_28	W28
#undef	W_MINUS_32
#define	W_MINUS_32	W
#endif

#if NO_TAIL_PRECALC == 0
	W_PRECALC_00_15_0MOD4(0)	// Pre-calculate round 0
#endif
	RR0(B, C, D, E, A, 64)		// Round 64
#if NO_TAIL_PRECALC == 0
	W_PRECALC_00_15_1MOD4		// Pre-calculate round 1
#endif
	RR1(B, C, D, E, A)		// Round 65
#if NO_TAIL_PRECALC == 0
	W_PRECALC_00_15_2MOD4		// Pre-calculate round 2
#endif
	RR0(E, A, B, C, D, 66)		// Round 66
#if NO_TAIL_PRECALC == 0
	W_PRECALC_00_15_3MOD4(3)	// Pre-calculate round 3

// Rotate window of precalculated values
#undef	W_MINUS_32
#define	W_MINUS_32	W28
#undef	W_MINUS_28
#define	W_MINUS_28	W24
#undef	W_MINUS_24
#define	W_MINUS_24	W20
#undef	W_MINUS_20
#define	W_MINUS_20	W16
#undef	W_MINUS_16
#define	W_MINUS_16	W12
#undef	W_MINUS_12
#define	W_MINUS_12	W8
#undef	W_MINUS_08
#define	W_MINUS_08	W4
#undef	W_MINUS_04
#define	W_MINUS_04	W0
#undef	W
#define	W		W28
#endif
	RR1(E, A, B, C, D)		// Round 67
#if NO_TAIL_PRECALC == 0
	W_PRECALC_00_15_0MOD4(4)	// Pre-calculate round 4
#endif
	RR0(C, D, E, A, B, 68)		// Round 68
#if NO_TAIL_PRECALC == 0
	W_PRECALC_00_15_1MOD4		// Pre-calculate round 5
#endif
	RR1(C, D, E, A, B)		// Round 69

#if NO_TAIL_PRECALC == 0
	W_PRECALC_00_15_2MOD4		// Pre-calculate round 6
#endif
	RR0(A, B, C, D, E, 70)		// Round 70
#if NO_TAIL_PRECALC == 0
	W_PRECALC_00_15_3MOD4(7)	// Pre-calculate round 7

// Rotate window of precalculated values
#undef	W_MINUS_32
#define	W_MINUS_32	W24
#undef	W_MINUS_28
#define	W_MINUS_28	W20
#undef	W_MINUS_24
#define	W_MINUS_24	W16
#undef	W_MINUS_20
#define	W_MINUS_20	W12
#undef	W_MINUS_16
#define	W_MINUS_16	W8
#undef	W_MINUS_12
#define	W_MINUS_12	W4
#undef	W_MINUS_08
#define	W_MINUS_08	W0
#undef	W_MINUS_04
#define	W_MINUS_04	W28
#undef	W
#define	W		W24
#endif
	RR1(A, B, C, D, E)		// Round 71
#if NO_TAIL_PRECALC == 0
	W_PRECALC_00_15_0MOD4(8)	// Pre-calculate round 8
#endif
	RR0(D, E, A, B, C, 72)		// Round 72
#if NO_TAIL_PRECALC == 0
	W_PRECALC_00_15_1MOD4		// Pre-calculate round 9
#endif
	RR1(D, E, A, B, C)		// Round 73
#if NO_TAIL_PRECALC == 0
	W_PRECALC_00_15_2MOD4		// Pre-calculate round 10
#endif
	RR0(B, C, D, E, A, 74)		// Round 74
#if NO_TAIL_PRECALC == 0
	W_PRECALC_00_15_3MOD4(11)	// Pre-calculate round 11

// Rotate window of precalculated values
#undef	W_MINUS_32
#define	W_MINUS_32	W20
#undef	W_MINUS_28
#define	W_MINUS_28	W16
#undef	W_MINUS_24
#define	W_MINUS_24	W12
#undef	W_MINUS_20
#define	W_MINUS_20	W8
#undef	W_MINUS_16
#define	W_MINUS_16	W4
#undef	W_MINUS_12
#define	W_MINUS_12	W0
#undef	W_MINUS_08
#define	W_MINUS_08	W28
#undef	W_MINUS_04
#define	W_MINUS_04	W24
#undef	W
#define	W		W20
#endif
	RR1(B, C, D, E, A)		// Round 75
#if NO_TAIL_PRECALC == 0
	W_PRECALC_00_15_0MOD4(12)	// Pre-calculate round 12
#endif
	RR0(E, A, B, C, D, 76)		// Round 76
#if NO_TAIL_PRECALC == 0
	W_PRECALC_00_15_1MOD4		// Pre-calculate round 13
#endif
	RR1(E, A, B, C, D)		// Round 77
#if NO_TAIL_PRECALC == 0
	W_PRECALC_00_15_2MOD4		// Pre-calculate round 14
#endif
	RR0(C, D, E, A, B, 78)		// Round 78
#if NO_TAIL_PRECALC == 0
	W_PRECALC_00_15_3MOD4(15)	// Pre-calculate round 15

// Rotate window of precalculated values
#undef	W_MINUS_32
#define	W_MINUS_32	W16
#undef	W_MINUS_28
#define	W_MINUS_28	W12
#undef	W_MINUS_24
#define	W_MINUS_24	W8
#undef	W_MINUS_20
#define	W_MINUS_20	W4
#undef	W_MINUS_16
#define	W_MINUS_16	W0
#undef	W_MINUS_12
#define	W_MINUS_12	W28
#undef	W_MINUS_08
#define	W_MINUS_08	W24
#undef	W_MINUS_04
#define	W_MINUS_04	W20
#undef	W
#define	W		W16
#endif
	RR1(C, D, E, A, B)		// Round 79

	// Add this block's hash to the current result
	UPDATE_HASH((HASH_PTR),   A)
	UPDATE_HASH(4(HASH_PTR),  B)
	UPDATE_HASH(8(HASH_PTR),  C)
	UPDATE_HASH(12(HASH_PTR), D)
	UPDATE_HASH(16(HASH_PTR), E)
	
#if (MULTIBLOCK == 1)
	jmp	.Lmultiblock_loop

.align 32
.Lmultiblock_end:
#endif
	//
	// Pop stack and return
	//
#ifdef _KERNEL
	add	$[RESERVE_STACK], %rsp
	SET_TS_OR_POP_XMM_REGISTERS(%r13)
#endif

	mov	%r12, %rsp	// We saved %rsp in %r12 instead of %rbp

	// Restore callee-saved registers before returning:
	pop	%rbp
#ifdef _KERNEL
	pop	%r13
#endif
	pop	%r12
	pop	%rbx

	ret
SET_SIZE(INTEL_SHA1_UPDATE_FUNCNAME)


// ---------------------------------------------------------------------------
//
// Read-only data
//
.text

// These constants are 2^30 * square root of 2, 3, 5, and 10, respectively:
#define	K1	0x5a827999	/* Constant for rounds  0 to 19. */
#define	K2	0x6ed9eba1	/* Constant for rounds 20 to 39. */
#define	K3	0x8f1bbcdc	/* Constant for rounds 40 to 59. */
#define	K4	0xca62c1d6	/* Constant for rounds 60 to 79. */

.align 128
.type	k_xmm_ar, @object
k_xmm_ar:
	.long	K1, K1, K1, K1
	.long	K2, K2, K2, K2
	.long	K3, K3, K3, K3
	.long	K4, K4, K4, K4

.align XMM_ALIGN
.type	bswap_shufb_ctl, @object
bswap_shufb_ctl:	// For byte-swapping 128-bit blocks with pshufb.
	.long	0x00010203
	.long	0x04050607
	.long	0x08090a0b
	.long	0x0c0d0e0f

#endif  /* lint || __lint */
