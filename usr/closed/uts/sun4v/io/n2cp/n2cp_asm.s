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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <sys/asm_linkage.h>

/*LINTLIBRARY*/

#if defined(lint) || defined(__lint)

#include <sys/n2cp.h>

/*
 * n2cp_delay()
 *
 * use some long latency instructions to delay between polls
 * rd %y was suggested by PAE.
 */
/* ARGSUSED */
void
n2cp_delay(uint64_t count)
{ return; }
	
/*
 * gcm_mul_vis3_64()
 *
 * gcm mode galois-field multiplication using the xmulx[hi] instructions
 * available on Rainbow Falls (a.k.a. KT) processors
 *
 */
/*ARGSUSED*/
aes_block_t
gcm_mul_vis3_64(aes_block_t x, aes_block_t y)
{ return (x); }


#else   /* lint || __lint */
	ENTRY(n2cp_delay)
1:	
	subcc %o0, 1, %o0
	bnz,pn %xcc, 1b
	rd %y, %o1
	retl
	nop
	SET_SIZE(n2cp_delay)


	ENTRY(gcm_mul_vis3_64)

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
	
	retl
	nop

	SET_SIZE(gcm_mul_vis3_64)

#endif  /* lint || __lint */
