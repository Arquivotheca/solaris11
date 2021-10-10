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
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/asm_linkage.h>

/*LINTLIBRARY*/

#if defined(lint) || defined(__lint)

#include <sys/types.h>
#include <modes/modes.h>

/*
 * gcm_mul_vis3_64()
 *
 * gcm mode galois-field multiplication using the xmulx[hi] instructions
 *
 */
/*ARGSUSED*/
void	
gcm_mul_vis3(uint64_t *x_in, uint64_t *y, uint64_t *res)
	{ return; }

/*ARGSUSED*/
void
ghash_multiblock(gcm_ctx_t *ctx, uint64_t *datap, int len)
	{ return; }

#else	/* lint || __lint */

#include<sys/asm_linkage.h>


	ENTRY(ghash_multiblock_vis3)

	save    %sp, -SA(MINFRAME), %sp
	
	ldx	[%i0], %o0
	ldx	[%i0 + 8], %o1
ghash_loop:
	ldx	[%i2], %o4
	ldx	[%i2 + 8], %o5
	ldx	[%i1], %o2
	ldx	[%i1 + 8], %o3
	xor	%o0, %o4, %o0
	xor	%o1, %o5, %o1

	xmulxhi	%o0, %o3, %g3
	xmulx	%o0, %o2, %o5
	xmulxhi	%o1, %o2, %g4
	xmulxhi	%o1, %o3, %g5
	xmulx	%o0, %o3, %g1
	xmulx	%o1, %o3, %g2
	xmulx	%o1, %o2, %o3
	xmulxhi	%o0, %o2, %o4
	mov	0xe1, %o0
	sllx	%o0, 56, %o0

	xor	%o5, %g3, %o5
	xor	%o5, %g4, %o5
	xor	%g5, %g1, %g1
	xor	%g1, %o3, %g1

	srlx	%g2, 63, %o1
	srlx	%g1, 63, %g3
	sllx	%g2, 63, %o3
	sllx	%g2, 58, %o2
	xor	%o3, %o2, %o2

	sllx	%g1, 1, %g1
	or	%g1, %o1, %g1

	xor	%g1, %o2, %g1

	sllx	%g2, 1, %g2

	xmulxhi	%g1, %o0, %o1
	xmulx	%g1, %o0, %o2
	xmulxhi	%g2, %o0, %o3
	xmulx	%g2, %o0, %g1

	xor	%o4, %o1, %o4
	xor	%o5, %o2, %o5
	xor	%o5, %o3, %o5

	sllx	%o4, 1, %o2
	srlx	%o5, 63, %o3

	or	%o2, %o3, %o0	
	sllx	%o5, 1, %o1
	srlx	%g1, 63, %o2
	or	%o1, %o2, %o1
	xor	%o1, %g3, %o1
	deccc	%i3
	bnz	%icc, ghash_loop
	add	%i2, 16, %i2

	stx	%o0, [%i0]
	stx	%o1, [%i0 + 8]

	ret
	restore

	SET_SIZE(ghash_multiblock_vis3)

	ENTRY(gcm_mul_vis3)

	save    %sp, -SA(MINFRAME), %sp
	ldx	[%i0], %o0
	ldx	[%i0 + 8], %o1
	ldx	[%i1], %o2
	ldx	[%i1 + 8], %o3


	xmulxhi	%o0, %o3, %g3
	xmulx	%o0, %o2, %o5
	xmulxhi	%o1, %o2, %g4
	xmulxhi	%o1, %o3, %g5
	xmulx	%o0, %o3, %g1
	xmulx	%o1, %o3, %g2
	xmulx	%o1, %o2, %o3
	xmulxhi	%o0, %o2, %o4
	mov	0xe1, %o0
	sllx	%o0, 56, %o0

	xor	%o5, %g3, %o5
	xor	%o5, %g4, %o5
	xor	%g5, %g1, %g1
	xor	%g1, %o3, %g1

	srlx	%g2, 63, %o1
	srlx	%g1, 63, %g3
	sllx	%g2, 63, %o3
	sllx	%g2, 58, %o2
	xor	%o3, %o2, %o2

	sllx	%g1, 1, %g1
	or	%g1, %o1, %g1

	xor	%g1, %o2, %g1

	sllx	%g2, 1, %g2

	xmulxhi	%g1, %o0, %o1
	xmulx	%g1, %o0, %o2
	xmulxhi	%g2, %o0, %o3
	xmulx	%g2, %o0, %g1

	xor	%o4, %o1, %o4
	xor	%o5, %o2, %o5
	xor	%o5, %o3, %o5

	sllx	%o4, 1, %o2
	srlx	%o5, 63, %o3

	or	%o2, %o3, %o0	
	stx	%o0, [%i2]

	sllx	%o5, 1, %o1
	srlx	%g1, 63, %o2
	or	%o1, %o2, %o1
	xor	%o1, %g3, %o1
	stx	%o1, [%i2 + 8]

	ret
	restore

	SET_SIZE(gcm_mul_vis3)

#endif  /* lint || __lint */
