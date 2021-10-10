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
 *  Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#if defined(lint)

#include "montmul_vt.h"

/* LINTED E_FUNC_ARG_UNUSED */
int mm_yf_montmul(mm_yf_pars_t *params)
{ return (0);}

/* LINTED E_FUNC_ARG_UNUSED */
int mm_yf_montsqr(mm_yf_pars_t *params)
{return (0);}

/* LINTED E_FUNC_ARG_UNUSED */
int mm_yf_execute_slp(void *slp)
{return (0);}

int mm_yf_ret_from_mont_func(void)
{return (0); }

int mm_yf_restore_func(void)
{return (0);}
	
	
#else /* lint */

#include <sys/asm_linkage.h>
#include "montmul_offsets.h"

	.section	".text",#alloc,#execinstr
	.align	8
	.skip	16


	/*
	 * The [load,store]_y_xxx routines load(store) the arguments of the 
	 * montmul or montsqr instructions
	 * These instruction come in a few flavors according to
	 * the sizes of the operands (192-2048 bits - currently the sizes
	 * that are implemented are those that are the most important
	 * in Elliptic curve cryptography and RSA/DSA/DH)
	 *
	 * %g1 contains the address of the corresponding bignum's value field
	 *
	 * [load,store]_y_xxx may be used for an argument that is less
	 * than xxx bits long, provided that %g1 points to a buffer
	 * that is allocated to hold at least xxx/8 bytes. In this case,
	 * the caller has to ignore the extra bytes after store
	 */
	
load_a_192:
	ldx	[%g1], %l0
	ldx	[%g1 + 8], %l1
	ldx	[%g1 + 16], %l2
	save	%sp, -336, %sp
	ret
	nop

load_n_192:
	ldx	[%g1], %l0
	ldx	[%g1 + 8], %l1
	ldx	[%g1 + 16], %l2
	save	%sp, -336, %sp
	mov	%i7, %o7
	save	%sp, -336, %sp
	mov	%i7, %o7
	save	%sp, -336, %sp
	mov	%i7, %o7
	save	%sp, -336, %sp
	ret
	nop

store_a_192:
	restore	%o7, %g0, %o7
	restore	%o7, %g0, %o7
	restore	%o7, %g0, %o7
	restore	%o7, %g0, %o7
	restore	%o7, %g0, %o7
	stx	%l0, [%g1]
	stx	%l1, [%g1 + 8]
	stx	%l2, [%g1 + 16]
	save	%sp, -336, %sp
	ret
	nop

load_b_192:
	restore	%o7, %g0, %o7
	restore	%o7, %g0, %o7
	ldx	[%g1], %o0
	ldx	[%g1 + 8], %o1
	ldx	[%g1 + 16], %o2
	save	%sp, -336, %sp
	mov	%i7, %o7
	save	%sp, -336, %sp
	ret
	nop


load_a_256:
	ldx	[%g1], %l0
	ldx	[%g1 + 8], %l1
	ldx	[%g1 + 16], %l2
	ldx	[%g1 + 24], %l3
	save	%sp, -336, %sp
	ret
	nop
	
load_n_256:
	ldx	[%g1], %l0
	ldx	[%g1 + 8], %l1
	ldx	[%g1 + 16], %l2
	ldx	[%g1 + 24], %l3
	save	%sp, -336, %sp
	mov	%i7, %o7
	save	%sp, -336, %sp
	mov	%i7, %o7
	save	%sp, -336, %sp
	mov	%i7, %o7
	save	%sp, -336, %sp
	ret
	nop
	
store_a_256:
	restore	%o7, %g0, %o7
	restore	%o7, %g0, %o7
	restore	%o7, %g0, %o7
	restore	%o7, %g0, %o7
	restore	%o7, %g0, %o7
	stx	%l0, [%g1]
	stx	%l1, [%g1 + 8]
	stx	%l2, [%g1 + 16]
	stx	%l3, [%g1 + 24]
	save	%sp, -336, %sp
	ret
	nop



load_b_256:
	restore	%o7, %g0, %o7
	restore	%o7, %g0, %o7
	ldx	[%g1], %o0
	ldx	[%g1 + 8], %o1
	ldx	[%g1 + 16], %o2
	ldx	[%g1 + 24], %o3
	save	%sp, -336, %sp
	mov	%i7, %o7
	save	%sp, -336, %sp
	ret
	nop



load_a_384:
	ldx	[%g1], %l0
	ldx	[%g1 + 8], %l1
	ldx	[%g1 + 16], %l2
	ldx	[%g1 + 24], %l3
	ldx	[%g1 + 32], %l4
	ldx	[%g1 + 40], %l5
	save	%sp, -336, %sp
	ret
	nop

load_n_384:
	ldx	[%g1], %l0
	ldx	[%g1 + 8], %l1
	ldx	[%g1 + 16], %l2
	ldx	[%g1 + 24], %l3
	ldx	[%g1 + 32], %l4
	ldx	[%g1 + 40], %l5
	save	%sp, -336, %sp
	mov	%i7, %o7
	save	%sp, -336, %sp
	mov	%i7, %o7
	save	%sp, -336, %sp
	mov	%i7, %o7
	save	%sp, -336, %sp
	ret
	nop

store_a_384:
	restore	%o7, %g0, %o7
	restore	%o7, %g0, %o7
	restore	%o7, %g0, %o7
	restore	%o7, %g0, %o7
	restore	%o7, %g0, %o7
	stx	%l0, [%g1]
	stx	%l1, [%g1 + 8]
	stx	%l2, [%g1 + 16]
	stx	%l3, [%g1 + 24]
	stx	%l4, [%g1 + 32]
	stx	%l5, [%g1 + 40]
	save	%sp, -336, %sp
	ret
	nop

load_b_384:
	restore	%o7, %g0, %o7
	restore	%o7, %g0, %o7
	ldx	[%g1], %o0
	ldx	[%g1 + 8], %o1
	ldx	[%g1 + 16], %o2
	ldx	[%g1 + 24], %o3
	ldx	[%g1 + 32], %o4
	ldx	[%g1 + 40], %o5
	save	%sp, -336, %sp
	mov	%i7, %o7
	save	%sp, -336, %sp
	ret
	nop



load_a_512:
	ldx	[%g1], %l0
	ldx	[%g1 + 8], %l1
	ldx	[%g1 + 16], %l2
	ldx	[%g1 + 24], %l3
	ldx	[%g1 + 32], %l4
	ldx	[%g1 + 40], %l5
	ldx	[%g1 + 48], %l6
	ldx	[%g1 + 56], %l7
	save	%sp, -336, %sp
	ret
	nop

load_n_512:
	ldx	[%g1], %l0
	ldx	[%g1 + 8], %l1
	ldx	[%g1 + 16], %l2
	ldx	[%g1 + 24], %l3
	ldx	[%g1 + 32], %l4
	ldx	[%g1 + 40], %l5
	ldx	[%g1 + 48], %l6
	ldx	[%g1 + 56], %l7
	save	%sp, -336, %sp
	mov	%i7, %o7
	save	%sp, -336, %sp
	mov	%i7, %o7
	save	%sp, -336, %sp
	mov	%i7, %o7
	save	%sp, -336, %sp
	ret
	nop

store_a_512:
	restore	%o7, %g0, %o7
	restore	%o7, %g0, %o7
	restore	%o7, %g0, %o7
	restore	%o7, %g0, %o7
	restore	%o7, %g0, %o7
	stx	%l0, [%g1]
	stx	%l1, [%g1 + 8]
	stx	%l2, [%g1 + 16]
	stx	%l3, [%g1 + 24]
	stx	%l4, [%g1 + 32]
	stx	%l5, [%g1 + 40]
	stx	%l6, [%g1 + 48]
	stx	%l7, [%g1 + 56]
	save	%sp, -336, %sp
	ret
	nop

load_b_512:
	restore	%o7, %g0, %o7
	restore	%o7, %g0, %o7
	ldx	[%g1], %o0
	ldx	[%g1 + 8], %o1
	ldx	[%g1 + 16], %o2
	ldx	[%g1 + 24], %o3
	ldx	[%g1 + 32], %o4
	ldx	[%g1 + 40], %o5
	save	%sp, -336, %sp
	ldx	[%g1 + 48], %l0
	ldx	[%g1 + 56], %l1
	mov	%i7, %o7
	save	%sp, -336, %sp
	ret
	nop



load_a_576:
	ldx	[%g1], %l0
	ldx	[%g1 + 8], %l1
	ldx	[%g1 + 16], %l2
	ldx	[%g1 + 24], %l3
	ldx	[%g1 + 32], %l4
	ldx	[%g1 + 40], %l5
	ldx	[%g1 + 48], %l6
	ldx	[%g1 + 56], %l7
	save	%sp, -336, %sp
	ret
	ldx	[%g1 + 64], %i0		! %o0 of the previous reg. window

load_n_576:
	ldx	[%g1], %l0
	ldx	[%g1 + 8], %l1
	ldx	[%g1 + 16], %l2
	ldx	[%g1 + 24], %l3
	ldx	[%g1 + 32], %l4
	ldx	[%g1 + 40], %l5
	ldx	[%g1 + 48], %l6
	ldx	[%g1 + 56], %l7
	ldx	[%g1 + 64], %o0
	save	%sp, -336, %sp
	mov	%i7, %o7
	save	%sp, -336, %sp
	mov	%i7, %o7
	save	%sp, -336, %sp
	mov	%i7, %o7
	save	%sp, -336, %sp
	ret
	nop

store_a_576:
	restore	%o7, %g0, %o7
	restore	%o7, %g0, %o7
	restore	%o7, %g0, %o7
	restore	%o7, %g0, %o7
	restore	%o7, %g0, %o7
	stx	%l0, [%g1]
	stx	%l1, [%g1 + 8]
	stx	%l2, [%g1 + 16]
	stx	%l3, [%g1 + 24]
	stx	%l4, [%g1 + 32]
	stx	%l5, [%g1 + 40]
	stx	%l6, [%g1 + 48]
	stx	%l7, [%g1 + 56]
	stx	%o0, [%g1 + 64]
	save	%sp, -336, %sp
	ret
	nop

load_b_576:
	restore	%o7, %g0, %o7
	restore	%o7, %g0, %o7
	ldx	[%g1], %o0
	ldx	[%g1 + 8], %o1
	ldx	[%g1 + 16], %o2
	ldx	[%g1 + 24], %o3
	ldx	[%g1 + 32], %o4
	ldx	[%g1 + 40], %o5
	save	%sp, -336, %sp
	ldx	[%g1 + 48], %l0
	ldx	[%g1 + 56], %l1
	ldx	[%g1 + 64], %l2
	mov	%i7, %o7
	save	%sp, -336, %sp
	ret
	nop



load_a_1024:
	ldx	[%g1], %l0
	ldx	[%g1 + 8], %l1
	ldx	[%g1 + 16], %l2
	ldx	[%g1 + 24], %l3
	ldx	[%g1 + 32], %l4
	ldx	[%g1 + 40], %l5
	ldx	[%g1 + 48], %l6
	ldx	[%g1 + 56], %l7

	ldx	[%g1 + 64], %o0
	ldx	[%g1 + 72], %o1
	ldx	[%g1 + 80], %o2
	ldx	[%g1 + 88], %o3
	ldx	[%g1 + 96], %o4
	ldx	[%g1 + 104], %o5
	save	%sp, -336, %sp
	ldd	[%g1 + 112], %f24
	ret
	ldd	[%g1 + 120], %f26

load_n_1024:
	ldx	[%g1], %l0
	ldx	[%g1 + 8], %l1
	ldx	[%g1 + 16], %l2
	ldx	[%g1 + 24], %l3
	ldx	[%g1 + 32], %l4
	ldx	[%g1 + 40], %l5
	ldx	[%g1 + 48], %l6
	ldx	[%g1 + 56], %l7

	ldx	[%g1 + 64], %o0
	ldx	[%g1 + 72], %o1
	ldx	[%g1 + 80], %o2
	ldx	[%g1 + 88], %o3
	ldx	[%g1 + 96], %o4
	ldx	[%g1 + 104], %o5
	save	%sp, -336, %sp
	ldx	[%g1 + 112], %l0
	ldx	[%g1 + 120], %l1
	mov	%i7, %o7
	save	%sp, -336, %sp
	mov	%i7, %o7
	save	%sp, -336, %sp
	mov	%i7, %o7
	save	%sp, -336, %sp
	ret
	nop

store_a_1024:
	restore	%o7, %g0, %o7
	restore	%o7, %g0, %o7
	restore	%o7, %g0, %o7
	restore	%o7, %g0, %o7
	restore	%o7, %g0, %o7
	stx	%l0, [%g1]
	stx	%l1, [%g1 + 8]
	stx	%l2, [%g1 + 16]
	stx	%l3, [%g1 + 24]
	stx	%l4, [%g1 + 32]
	stx	%l5, [%g1 + 40]
	stx	%l6, [%g1 + 48]
	stx	%l7, [%g1 + 56]
	stx	%o0, [%g1 + 64]
	stx	%o1, [%g1 + 72]
	stx	%o2, [%g1 + 80]
	stx	%o3, [%g1 + 88]
	stx	%o4, [%g1 + 96]
	stx	%o5, [%g1 + 104]
	std	%f24, [%g1 + 112]
	std	%f26, [%g1 + 120]
	save	%sp, -336, %sp
	ret
	nop

load_b_1024:
	restore	%o7, %g0, %o7
	restore	%o7, %g0, %o7
	ldx	[%g1], %o0
	ldx	[%g1 + 8], %o1
	ldx	[%g1 + 16], %o2
	ldx	[%g1 + 24], %o3
	ldx	[%g1 + 32], %o4
	ldx	[%g1 + 40], %o5
	save	%sp, -336, %sp
	ldx	[%g1 + 48], %l0
	ldx	[%g1 + 56], %l1

	ldx	[%g1 + 64], %l2
	ldx	[%g1 + 72], %l3
	ldx	[%g1 + 80], %l4
	ldx	[%g1 + 88], %l5
	ldx	[%g1 + 96], %l6
	ldx	[%g1 + 104], %l7
	ldx	[%g1 + 112], %o0
	ldx	[%g1 + 120], %o1
	mov	%i7, %o7
	save	%sp, -336, %sp
	ret
	nop



load_a_1536:
	ldx	[%g1], %l0
	ldx	[%g1 + 8], %l1
	ldx	[%g1 + 16], %l2
	ldx	[%g1 + 24], %l3
	ldx	[%g1 + 32], %l4
	ldx	[%g1 + 40], %l5
	ldx	[%g1 + 48], %l6
	ldx	[%g1 + 56], %l7

	ldx	[%g1 + 64], %o0
	ldx	[%g1 + 72], %o1
	ldx	[%g1 + 80], %o2
	ldx	[%g1 + 88], %o3
	ldx	[%g1 + 96], %o4
	ldx	[%g1 + 104], %o5
	save	%sp, -336, %sp
	ldd	[%g1 + 112], %f24
	ldd	[%g1 + 120], %f26

	ldd	[%g1 + 128], %f28
	ldd	[%g1 + 136], %f30
	ldd	[%g1 + 144], %f32
	ldd	[%g1 + 152], %f34
	ldd	[%g1 + 160], %f36
	ldd	[%g1 + 168], %f38
	ldd	[%g1 + 176], %f40
	ret
	ldd	[%g1 + 184], %f42

load_n_1536:
	ldx	[%g1], %l0
	ldx	[%g1 + 8], %l1
	ldx	[%g1 + 16], %l2
	ldx	[%g1 + 24], %l3
	ldx	[%g1 + 32], %l4
	ldx	[%g1 + 40], %l5
	ldx	[%g1 + 48], %l6
	ldx	[%g1 + 56], %l7

	ldx	[%g1 + 64], %o0
	ldx	[%g1 + 72], %o1
	ldx	[%g1 + 80], %o2
	ldx	[%g1 + 88], %o3
	ldx	[%g1 + 96], %o4
	ldx	[%g1 + 104], %o5
	save	%sp, -336, %sp
	ldx	[%g1 + 112], %l0
	ldx	[%g1 + 120], %l1

	ldx	[%g1 + 128], %l2
	ldx	[%g1 + 136], %l3
	ldx	[%g1 + 144], %l4
	ldx	[%g1 + 152], %l5
	ldx	[%g1 + 160], %l6
	ldx	[%g1 + 168], %l7
	ldx	[%g1 + 176], %o0
	ldx	[%g1 + 184], %o1
	mov	%i7, %o7
	save	%sp, -336, %sp
	mov	%i7, %o7
	save	%sp, -336, %sp
	mov	%i7, %o7
	save	%sp, -336, %sp
	ret
	nop

store_a_1536:
	restore	%o7, %g0, %o7
	restore	%o7, %g0, %o7
	restore	%o7, %g0, %o7
	restore	%o7, %g0, %o7
	restore	%o7, %g0, %o7
	stx	%l0, [%g1]
	stx	%l1, [%g1 + 8]
	stx	%l2, [%g1 + 16]
	stx	%l3, [%g1 + 24]
	stx	%l4, [%g1 + 32]
	stx	%l5, [%g1 + 40]
	stx	%l6, [%g1 + 48]
	stx	%l7, [%g1 + 56]
	stx	%o0, [%g1 + 64]
	stx	%o1, [%g1 + 72]
	stx	%o2, [%g1 + 80]
	stx	%o3, [%g1 + 88]
	stx	%o4, [%g1 + 96]
	stx	%o5, [%g1 + 104]
	std	%f24, [%g1 + 112]
	std	%f26, [%g1 + 120]
	std	%f28, [%g1 + 128]
	std	%f30, [%g1 + 136]
	std	%f32, [%g1 + 144]
	std	%f34, [%g1 + 152]
	std	%f36, [%g1 + 160]
	std	%f38, [%g1 + 168]
	std	%f40, [%g1 + 176]
	std	%f42, [%g1 + 184]
	save	%sp, -336, %sp
	ret
	nop

load_b_1536:
	restore	%o7, %g0, %o7
	restore	%o7, %g0, %o7
	ldx	[%g1], %o0
	ldx	[%g1 + 8], %o1
	ldx	[%g1 + 16], %o2
	ldx	[%g1 + 24], %o3
	ldx	[%g1 + 32], %o4
	ldx	[%g1 + 40], %o5
	save	%sp, -336, %sp
	ldx	[%g1 + 48], %l0
	ldx	[%g1 + 56], %l1

	ldx	[%g1 + 64], %l2
	ldx	[%g1 + 72], %l3
	ldx	[%g1 + 80], %l4
	ldx	[%g1 + 88], %l5
	ldx	[%g1 + 96], %l6
	ldx	[%g1 + 104], %l7
	ldx	[%g1 + 112], %o0
	ldx	[%g1 + 120], %o1

	ldx	[%g1 + 128], %o2
	ldx	[%g1 + 136], %o3
	ldx	[%g1 + 144], %o4
	ldx	[%g1 + 152], %o5
	mov	%i7, %o7
	save	%sp, -336, %sp
	ldx	[%g1 + 160], %l0
	ldx	[%g1 + 168], %l1
	ldx	[%g1 + 176], %l2
	ret
	ldx	[%g1 + 184], %l3



load_a_2048:
	ldx	[%g1], %l0
	ldx	[%g1 + 8], %l1
	ldx	[%g1 + 16], %l2
	ldx	[%g1 + 24], %l3
	ldx	[%g1 + 32], %l4
	ldx	[%g1 + 40], %l5
	ldx	[%g1 + 48], %l6
	ldx	[%g1 + 56], %l7

	ldx	[%g1 + 64], %o0
	ldx	[%g1 + 72], %o1
	ldx	[%g1 + 80], %o2
	ldx	[%g1 + 88], %o3
	ldx	[%g1 + 96], %o4
	ldx	[%g1 + 104], %o5
	save	%sp, -336, %sp
	ldd	[%g1 + 112], %f24
	ldd	[%g1 + 120], %f26

	ldd	[%g1 + 128], %f28
	ldd	[%g1 + 136], %f30
	ldd	[%g1 + 144], %f32
	ldd	[%g1 + 152], %f34
	ldd	[%g1 + 160], %f36
	ldd	[%g1 + 168], %f38
	ldd	[%g1 + 176], %f40
	ldd	[%g1 + 184], %f42

	ldd	[%g1 + 192], %f44
	ldd	[%g1 + 200], %f46
	ldd	[%g1 + 208], %f48
	ldd	[%g1 + 216], %f50
	ldd	[%g1 + 224], %f52
	ldd	[%g1 + 232], %f54
	ldd	[%g1 + 240], %f56
	ret
	ldd	[%g1 + 248], %f58

load_n_2048:
	ldx	[%g1], %l0
	ldx	[%g1 + 8], %l1
	ldx	[%g1 + 16], %l2
	ldx	[%g1 + 24], %l3
	ldx	[%g1 + 32], %l4
	ldx	[%g1 + 40], %l5
	ldx	[%g1 + 48], %l6
	ldx	[%g1 + 56], %l7

	ldx	[%g1 + 64], %o0
	ldx	[%g1 + 72], %o1
	ldx	[%g1 + 80], %o2
	ldx	[%g1 + 88], %o3
	ldx	[%g1 + 96], %o4
	ldx	[%g1 + 104], %o5
	save	%sp, -336, %sp
	ldx	[%g1 + 112], %l0
	ldx	[%g1 + 120], %l1

	ldx	[%g1 + 128], %l2
	ldx	[%g1 + 136], %l3
	ldx	[%g1 + 144], %l4
	ldx	[%g1 + 152], %l5
	ldx	[%g1 + 160], %l6
	ldx	[%g1 + 168], %l7
	ldx	[%g1 + 176], %o0
	ldx	[%g1 + 184], %o1

	ldx	[%g1 + 192], %o2
	ldx	[%g1 + 200], %o3
	ldx	[%g1 + 208], %o4
	ldx	[%g1 + 216], %o5
	mov	%i7, %o7
	save	%sp, -336, %sp
	ldx	[%g1 + 224], %l0
	ldx	[%g1 + 232], %l1
	ldx	[%g1 + 240], %l2
	ldx	[%g1 + 248], %l3
	mov	%i7, %o7
	save	%sp, -336, %sp
	mov	%i7, %o7
	save	%sp, -336, %sp
	ret
	nop


store_a_2048:
	restore	%o7, %g0, %o7
	restore	%o7, %g0, %o7
	restore	%o7, %g0, %o7
	restore	%o7, %g0, %o7
	restore	%o7, %g0, %o7
	stx	%l0, [%g1]
	stx	%l1, [%g1 + 8]
	stx	%l2, [%g1 + 16]
	stx	%l3, [%g1 + 24]
	stx	%l4, [%g1 + 32]
	stx	%l5, [%g1 + 40]
	stx	%l6, [%g1 + 48]
	stx	%l7, [%g1 + 56]
	stx	%o0, [%g1 + 64]
	stx	%o1, [%g1 + 72]
	stx	%o2, [%g1 + 80]
	stx	%o3, [%g1 + 88]
	stx	%o4, [%g1 + 96]
	stx	%o5, [%g1 + 104]
	std	%f24, [%g1 + 112]
	std	%f26, [%g1 + 120]
	std	%f28, [%g1 + 128]
	std	%f30, [%g1 + 136]
	std	%f32, [%g1 + 144]
	std	%f34, [%g1 + 152]
	std	%f36, [%g1 + 160]
	std	%f38, [%g1 + 168]
	std	%f40, [%g1 + 176]
	std	%f42, [%g1 + 184]
	std	%f44, [%g1 + 192]
	std	%f46, [%g1 + 200]
	std	%f48, [%g1 + 208]
	std	%f50, [%g1 + 216]
	std	%f52, [%g1 + 224]
	std	%f54, [%g1 + 232]
	std	%f56, [%g1 + 240]
	std	%f58, [%g1 + 248]
	save	%sp, -336, %sp
	ret
	nop

load_b_2048:
	restore	%o7, %g0, %o7
	restore	%o7, %g0, %o7
	ldx	[%g1], %o0
	ldx	[%g1 + 8], %o1
	ldx	[%g1 + 16], %o2
	ldx	[%g1 + 24], %o3
	ldx	[%g1 + 32], %o4
	ldx	[%g1 + 40], %o5
	save	%sp, -336, %sp
	ldx	[%g1 + 48], %l0
	ldx	[%g1 + 56], %l1

	ldx	[%g1 + 64], %l2
	ldx	[%g1 + 72], %l3
	ldx	[%g1 + 80], %l4
	ldx	[%g1 + 88], %l5
	ldx	[%g1 + 96], %l6
	ldx	[%g1 + 104], %l7
	ldx	[%g1 + 112], %o0
	ldx	[%g1 + 120], %o1

	ldx	[%g1 + 128], %o2
	ldx	[%g1 + 136], %o3
	ldx	[%g1 + 144], %o4
	ldx	[%g1 + 152], %o5
	mov	%i7, %o7
	save	%sp, -336, %sp	
	ldx	[%g1 + 160], %l0
	ldx	[%g1 + 168], %l1
	ldx	[%g1 + 176], %l2
	ldx	[%g1 + 184], %l3

	ldx	[%g1 + 192], %l4
	ldx	[%g1 + 200], %l5
	ldx	[%g1 + 208], %l6
	ldx	[%g1 + 216], %l7
	ldx	[%g1 + 224], %o0
	ldx	[%g1 + 232], %o1
	ldx	[%g1 + 240], %o2
	ret
	ldx	[%g1 + 248], %o3


	/*
	 * XXX - this function should check for errors that are
	 * normally checked in the internal_processor_error
	 * trap handler (in PFSR) and also for possible ECC errors
	 * encountered during its execution (in FSR.fcc3)
	 */
check_errors:
	retl
	nop
montmul_error_found:
	/* the following should be executed on the error path */
	restore			! window (i-1)
	restore			! window (i-2)
	restore			! window (i-3)
	restore			! window (i-4)
	restore			! window (i-5)
	restore			! window (i-6)
	restore			! window (i-7)
	retl
	xor	%g0, %g0, %o0


	/*
	 * Does a Montgomery multiplication using the
	 * parameters from an mm_yf_pars_t structure
	 */

	ENTRY(mm_yf_montmul)
	mov	%o0, %g5
	save	%sp, -336, %sp			! window (i-6)
#ifndef _sparcv9
	mov	-1, %o0
	sllx	%o0, 32, %o0
	add	%o0, %sp, %sp
#endif
	ldn	[%g5 + MM_YF_PARS_NPRIME_OFFS], %g1
	ldd	[%g1], %f60
	save	%sp, -336, %sp			! window (i-5)
	ldn	[%g5 + MM_YF_PARS_LOAD_A_OFFS], %g1
	call	%g1				! window (i-4)
	ldn	[%g5 + MM_YF_PARS_A_OFFS], %g1

	ldn	[%g5 + MM_YF_PARS_LOAD_N_OFFS], %g1
	call	%g1				! window (i)
	ldn	[%g5 + MM_YF_PARS_N_OFFS], %g1

	ldn	[%g5 + MM_YF_PARS_LOAD_B_OFFS], %g1
	call	%g1				! window (i)
	ldn	[%g5 + MM_YF_PARS_B_OFFS], %g1

	ldn	[%g5 + MM_YF_PARS_MONTMUL_OFFS], %g1
	call	%g1
	add	%g5, PTR_SIZE, %g5	! montmul_xxx will subtract the same

	ldn	[%g5 + MM_YF_PARS_STORE_A_OFFS], %g1
	call	%g1
	ldn	[%g5 + MM_YF_PARS_RET_OFFS], %g1

#ifndef _sparcv9
	restore	%i6, %g0, %o0		! window (i-5)
	restore	%o0, %g0, %o0		! window (i-6)
	restore	%o0, %g0, %o0		! window (i-7)
	srax	%o0, 32, %o0
	retl
	add	%o0, 1, %o0
#else
	restore			! window (i-5)
	restore			! window (i-6)
	restore			! window (i-7)
	retl
	mov	0, %o0
#endif
	SET_SIZE(mm_yf_montmul)

	
	/*
	 * Does a Montgomery squaring using the
	 * parameters from an mm_yf_pars_t structure
	 * (ignoring the b parameter)
	 */

	ENTRY(mm_yf_montsqr)
	mov	%o0, %g5
	save	%sp, -336, %sp			! window (i-6)
#ifndef _sparcv9
	mov	-1, %o0
	sllx	%o0, 32, %o0
	add	%o0, %sp, %sp
#endif
	ldn	[%g5 + MM_YF_PARS_NPRIME_OFFS], %g1
	ldd	[%g1], %f60
	save	%sp, -336, %sp			! window (i-5)
	ldn	[%g5 + MM_YF_PARS_LOAD_A_OFFS], %g1
	call	%g1				! window (i-4)
	ldn	[%g5 + MM_YF_PARS_A_OFFS], %g1

	ldn	[%g5 + MM_YF_PARS_LOAD_N_OFFS], %g1
	call	%g1				! window (i)
	ldn	[%g5 + MM_YF_PARS_N_OFFS], %g1

	ldn	[%g5 + MM_YF_PARS_MONTSQR_OFFS], %g1
	call	%g1				! window (i)
	add	%g5, PTR_SIZE, %g5	! montsqr_xxx will subtract the same

	ldn	[%g5 + MM_YF_PARS_STORE_A_OFFS], %g1
	call	%g1				! window (i-4)
	ldn	[%g5 + MM_YF_PARS_RET_OFFS], %g1

#ifndef _sparcv9
	restore	%i6, %g0, %o0		! window (i-5)
	restore	%o0, %g0, %o0		! window (i-6)
	restore	%o0, %g0, %o0		! window (i-7)
	srax	%o0, 32, %o0
	retl
	add	%o0, 1, %o0
#else
	restore			! window (i-5)
	restore			! window (i-6)
	restore			! window (i-7)
	retl
	or	%g0, %g0, %o0
#endif
	SET_SIZE(mm_yf_montsqr)


	ENTRY(mm_yf_restore_func)
	restore	%o7, %g0, %o7
	retl
	sub	%g5, PTR_SIZE, %g5
	SET_SIZE(mm_yf_restore_func)


	/*
	 * This function should be specified as the last function in
	 * the straight-line program passed to mm_yf_execute_slp()
	 * to return to the caller of mm_yf_execute_slp()
	 */

	ENTRY(mm_yf_ret_from_mont_func)
#ifndef _sparcv9
	restore	%i6, %g0, %o0		! window (i-5)
	restore	%o0, %g0, %o0		! window (i-6)
	restore	%o0, %g0, %o0		! window (i-7)
	srax	%o0, 32, %o0
	retl
	add	%o0, 1, %o0
#else
	restore			! window (i-5)
	restore			! window (i-6)
	restore			! window (i-7)
	retl
	or	%g0, %g0, %o0
#endif
	SET_SIZE(mm_yf_ret_from_mont_func)

	
	/*
	 * Executes a straight-line program pointed to by its input
	 * parameter, %o0
	 * The straight-line program consists of function pointers
	 * and data pointers, starting with a pointer to the nprime
	 * parameter of the Montgomery multiplication. It assumes that
	 * a called functioneither uses a pointer parameter supplied
	 * in %g1 or if it doesn't need parameters, subtracts PTR_SIZE
	 * from %g5
	 */

	ENTRY(mm_yf_execute_slp)
	mov	%o0, %g5
	save	%sp, -336, %sp					! window (i-6)
#ifndef _sparcv9
	mov	-1, %o0
	sllx	%o0, 32, %o0
	add	%o0, %sp, %sp
#endif
	ldn	[%g5], %g1
	ldd	[%g1], %f60
	save	%sp, -336, %sp					! window (i-5)
	add	%g5, PTR_SIZE, %g5
slp_loop:
	ldn	[%g5], %g1
	call	%g1
	ldn	[%g5 + PTR_SIZE], %g1

	ba	slp_loop
	add	%g5, 2 * PTR_SIZE, %g5
	SET_SIZE(mm_yf_execute_slp)

#include "montmul_tables.h"

#endif /* lint */
