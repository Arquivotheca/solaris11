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

#include "mpmul_vt.h"

	
#else /* lint */

#include <sys/asm_linkage.h>
#include "montmul_offsets.h"

	.section	".text",#alloc,#execinstr
	.align	8
	.skip	16


	/*
	 * The [load,store]_z_xxx_yy routines load(store) the arguments of the 
	 * montmul or montsqr instructions
	 * these instruction come in a few flavors according to
	 * the sizes of the operands (192-2048 bits - currently the sizes
	 * that are implemented are those that are the most important
	 * in Elliptic curve cryptography and RSA/DSA/DH)
	 * and the bignum representation (32 or 64-bit chunks)
	 *
	 * %g1 contains the address of the corresponding bignum's value field
	 *
	 * load_z_xxx_yy may be used to load an argument that is less
	 * than xxx bits long, provided that %g1 points to a buffer
	 * that is allocated to hold at least xxx/8 bytes 
	 *
	 */
	
load_m1_2048:
	ldd	[%g1 + 248], %f22
load_m1_1984:
	ldd	[%g1 + 240], %f20
load_m1_1920:
	ldd	[%g1 + 232], %f18
load_m1_1856:
	ldd	[%g1 + 224], %f16
load_m1_1792:
	ldd	[%g1 + 216], %f14
load_m1_1728:
	ldd	[%g1 + 208], %f12
load_m1_1664:
	ldd	[%g1 + 200], %f10
load_m1_1600:
	ldd	[%g1 + 192], %f8
load_m1_1536:
	ldd	[%g1 + 184], %f6
load_m1_1472:
	ldd	[%g1 + 176], %f4
load_m1_1408:
	ldx	[%g1 + 168], %i5
load_m1_1344:
	ldx	[%g1 + 160], %i4
load_m1_1280:
	ldx	[%g1 + 152], %i3
load_m1_1216:
	ldx	[%g1 + 144], %i2
load_m1_1152:
	ldx	[%g1 + 136], %i1
load_m1_1088:
	ldx	[%g1 + 128], %i0
load_m1_1024:
	ldx	[%g1 + 120], %l7
load_m1_960:
	ldx	[%g1 + 112], %l6
load_m1_896:
	ldx	[%g1 + 104], %l5
load_m1_832:
	ldx	[%g1 + 96], %l4
load_m1_768:
	ldx	[%g1 + 88], %l3
load_m1_704:
	ldx	[%g1 + 80], %l2
load_m1_640:
	ldx	[%g1 + 72], %l1
load_m1_576:
	ldx	[%g1 + 64], %l0
load_m1_512:
	ldd	[%g1 + 56], %f2
load_m1_448:
	ldd	[%g1 + 48], %f0
load_m1_384:
	ldx	[%g1 + 40], %o5
load_m1_320:	
	ldx	[%g1 + 32], %o4
load_m1_256:
	ldx	[%g1 + 24], %o3
load_m1_192:
	ldx	[%g1 + 16], %o2
load_m1_128:
	ldx	[%g1 + 8], %o1
load_m1_64:
	ldx	[%g1], %o0
	save	%sp, -336, %sp	! window (i-5)
	ret
	nop

	
load_m2_2048:
	ldd	[%g1 + 248], %f58
load_m2_1984:
	ldd	[%g1 + 240], %f56
load_m2_1920:
	ldd	[%g1 + 232], %f54
load_m2_1856:
	ldd	[%g1 + 224], %f52
load_m2_1792:
	ldd	[%g1 + 216], %f50
load_m2_1728:
	ldd	[%g1 + 208], %f48
load_m2_1664:
	ldd	[%g1 + 200], %f46
load_m2_1600:
	ldd	[%g1 + 192], %f44
load_m2_1536:
	ldd	[%g1 + 184], %f42
load_m2_1472:
	ldd	[%g1 + 176], %f40
load_m2_1408:
	ldd	[%g1 + 168], %f38
load_m2_1344:
	ldd	[%g1 + 160], %f36
load_m2_1280:
	ldd	[%g1 + 152], %f34
load_m2_1216:
	ldd	[%g1 + 144], %f32
load_m2_1152:
	ldd	[%g1 + 136], %f30
load_m2_1088:
	ldd	[%g1 + 128], %f28
load_m2_1024:
	ldd	[%g1 + 120], %f26
load_m2_960:
	ldd	[%g1 + 112], %f24
load_m2_896:
	ldx	[%g1 + 104], %o5
load_m2_832:
	ldx	[%g1 + 96], %o4
load_m2_768:
	ldx	[%g1 + 88], %o3
load_m2_704:
	ldx	[%g1 + 80], %o2
load_m2_640:
	ldx	[%g1 + 72], %o1
load_m2_576:
	ldx	[%g1 + 64], %o0
load_m2_512:
	ldx	[%g1 + 56], %l7
load_m2_448:
	ldx	[%g1 + 48], %l6
load_m2_384:
	ldx	[%g1 + 40], %l5
load_m2_320:
	ldx	[%g1 + 32], %l4
load_m2_256:
	ldx	[%g1 + 24], %l3
load_m2_192:
	ldx	[%g1 + 16], %l2
load_m2_128:
	ldx	[%g1 + 8], %l1
load_m2_64:
	ldx	[%g1], %l0
	save	%sp, -336, %sp	! window (i-4)
	mov	%i7, %o7
	save	%sp, -336, %sp	! window (i-3)
	mov	%i7, %o7
	save	%sp, -336, %sp	! window (i-2)
	mov	%i7, %o7
	save	%sp, -336, %sp	! window (i-1)
	mov	%i7, %o7
	save	%sp, -336, %sp	! window (i)
	ret
	nop


st_res_2048:
	stx	%l7, [%g1 + 504]
	stx	%l6, [%g1 + 496]
st_res_1984:
	stx	%l5, [%g1 + 488]
	stx	%l4, [%g1 + 480]
st_res_1920:
	stx	%l3, [%g1 + 472]
	stx	%l2, [%g1 + 464]
st_res_1856:
	stx	%l1, [%g1 + 456]
	stx	%l0, [%g1 + 448]
	restore %o7, %g0, %o7
	
st_res_1792:
	stx	%o5, [%g1 + 440]
	stx	%o4, [%g1 + 432]
st_res_1728:
	stx	%o3, [%g1 + 424]
	stx	%o2, [%g1 + 416]
st_res_1664:
	stx	%o1, [%g1 + 408]
	stx	%o0, [%g1 + 400]
st_res_1600:
	stx	%l7, [%g1 + 392]
	stx	%l6, [%g1 + 384]
st_res_1536:
	stx	%l5, [%g1 + 376]
	stx	%l4, [%g1 + 368]
st_res_1472:
	stx	%l3, [%g1 + 360]
	stx	%l2, [%g1 + 352]
st_res_1408:
	stx	%l1, [%g1 + 344]
	stx	%l0, [%g1 + 336]
	restore %o7, %g0, %o7

st_res_1344:
	stx	%o5, [%g1 + 328]
	stx	%o4, [%g1 + 320]
st_res_1280:
	stx	%o3, [%g1 + 312]
	stx	%o2, [%g1 + 304]
st_res_1216:
	stx	%o1, [%g1 + 296]
	stx	%o0, [%g1 + 288]
st_res_1152:
	stx	%l7, [%g1 + 280]
	stx	%l6, [%g1 + 272]
st_res_1088:
	stx	%l5, [%g1 + 264]
	stx	%l4, [%g1 + 256]
st_res_1024:
	stx	%l3, [%g1 + 248]
	stx	%l2, [%g1 + 240]
st_res_960:
	stx	%l1, [%g1 + 232]
	stx	%l0, [%g1 + 224]
	restore %o7, %g0, %o7

st_res_896:
	stx	%o5, [%g1 + 216]
	stx	%o4, [%g1 + 208]
st_res_832:
	stx	%o3, [%g1 + 200]
	stx	%o2, [%g1 + 192]
st_res_768:
	stx	%o1, [%g1 + 184]
	stx	%o0, [%g1 + 176]
st_res_704:
	stx	%l7, [%g1 + 168]
	stx	%l6, [%g1 + 160]
st_res_640:
	stx	%l5, [%g1 + 152]
	stx	%l4, [%g1 + 144]
st_res_576:
	stx	%l3, [%g1 + 136]
	stx	%l2, [%g1 + 128]
st_res_512:
	stx	%l1, [%g1 + 120]
	stx	%l0, [%g1 + 112]
	restore %o7, %g0, %o7

st_res_448:
	stx	%o5, [%g1 + 104]
	stx	%o4, [%g1 + 96]
st_res_384:
	stx	%o3, [%g1 + 88]
	stx	%o2, [%g1 + 80]
st_res_320:
	stx	%o1, [%g1 + 72]
	stx	%o0, [%g1 + 64]
st_res_256:
	stx	%l7, [%g1 + 56]
	stx	%l6, [%g1 + 48]
st_res_192:
	stx	%l5, [%g1 + 40]
	stx	%l4, [%g1 + 32]
st_res_128:
	stx	%l3, [%g1 + 24]
	stx	%l2, [%g1 + 16]
st_res_64:
	stx	%l1, [%g1 + 8]
	stx	%l0, [%g1]
	retl
	mov	0, %o0

	
store_res_2048:
	ba	st_res_2048
	nop
store_res_1984:
	ba	st_res_1984
	nop
store_res_1920:
	ba	st_res_1920
	nop
store_res_1856:
	ba	st_res_1856
	nop

store_res_1792:
	ba	st_res_1792
	restore %o7, %g0, %o7
store_res_1728:
	ba	st_res_1728
	restore %o7, %g0, %o7
store_res_1664:
	ba	st_res_1664
	restore %o7, %g0, %o7
store_res_1600:
	ba	st_res_1600
	restore %o7, %g0, %o7
store_res_1536:
	ba	st_res_1536
	restore %o7, %g0, %o7
store_res_1472:
	ba	st_res_1472
	restore %o7, %g0, %o7
store_res_1408:
	ba	st_res_1408
	restore %o7, %g0, %o7

store_res_1344:
	restore %o7, %g0, %o7
	ba	st_res_1344
	restore %o7, %g0, %o7
store_res_1280:
	restore %o7, %g0, %o7
	ba	st_res_1280
	restore %o7, %g0, %o7
store_res_1216:
	restore %o7, %g0, %o7
	ba	st_res_1216
	restore %o7, %g0, %o7
store_res_1152:
	restore %o7, %g0, %o7
	ba	st_res_1152
	restore %o7, %g0, %o7
store_res_1088:
	restore %o7, %g0, %o7
	ba	st_res_1088
	restore %o7, %g0, %o7
store_res_1024:
	restore %o7, %g0, %o7
	ba	st_res_1024
	restore %o7, %g0, %o7
store_res_960:
	restore %o7, %g0, %o7
	ba	st_res_960
	restore %o7, %g0, %o7

store_res_896:
	restore %o7, %g0, %o7
	restore %o7, %g0, %o7
	ba	st_res_896
	restore %o7, %g0, %o7
store_res_832:
	restore %o7, %g0, %o7
	restore %o7, %g0, %o7
	ba	st_res_832
	restore %o7, %g0, %o7
store_res_768:
	restore %o7, %g0, %o7
	restore %o7, %g0, %o7
	ba	st_res_768
	restore %o7, %g0, %o7
store_res_704:
	restore %o7, %g0, %o7
	restore %o7, %g0, %o7
	ba	st_res_704
	restore %o7, %g0, %o7
store_res_640:
	restore %o7, %g0, %o7
	restore %o7, %g0, %o7
	ba	st_res_640
	restore %o7, %g0, %o7
store_res_576:
	restore %o7, %g0, %o7
	restore %o7, %g0, %o7
	ba	st_res_576
	restore %o7, %g0, %o7
store_res_512:
	restore %o7, %g0, %o7
	restore %o7, %g0, %o7
	ba	st_res_512
	restore %o7, %g0, %o7

store_res_448:
	restore %o7, %g0, %o7
	restore %o7, %g0, %o7
	restore %o7, %g0, %o7
	ba	st_res_448
	restore %o7, %g0, %o7
store_res_384:
	restore %o7, %g0, %o7
	restore %o7, %g0, %o7
	restore %o7, %g0, %o7
	ba	st_res_384
	restore %o7, %g0, %o7
store_res_320:
	restore %o7, %g0, %o7
	restore %o7, %g0, %o7
	restore %o7, %g0, %o7
	ba	st_res_320
	restore %o7, %g0, %o7
store_res_256:
	restore %o7, %g0, %o7
	restore %o7, %g0, %o7
	restore %o7, %g0, %o7
	ba	st_res_256
	restore %o7, %g0, %o7
store_res_192:
	restore %o7, %g0, %o7
	restore %o7, %g0, %o7
	restore %o7, %g0, %o7
	ba	st_res_192
	restore %o7, %g0, %o7
store_res_128:
	restore %o7, %g0, %o7
	restore %o7, %g0, %o7
	restore %o7, %g0, %o7
	ba	st_res_128
	restore %o7, %g0, %o7
store_res_64:
	restore %o7, %g0, %o7
	restore %o7, %g0, %o7
	restore %o7, %g0, %o7
	ba	st_res_64
	restore %o7, %g0, %o7



	/*
	 * XXX - this function should check for errors that are
	 * normally checked in the internal_processor_error
	 * trap handler (in PFSR) and also for possible ECC errors
	 * encountered during its execution (in FSR.fcc3)
	 */
mpmul_check_errors:
	retl
	nop
mpmul_error_found:
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


	ENTRY(mpm_yf_mpmul)			! window (i-7)
	mov	%o0, %g5
#ifndef _sparcv9
	mov	-1, %o0
	sllx	%o0, 32, %o0
	add	%o0, %sp, %sp
#endif
	save	%sp, -336, %sp			! window (i-6)
	ldn	[%g5 + MPM_YF_PARS_LOAD_M1_OFFS], %g1
	call	%g1				! window (i-5)
	ldn	[%g5 + MPM_YF_PARS_M1_OFFS], %g1

	ldn	[%g5 + MPM_YF_PARS_LOAD_M2_OFFS], %g1
	call	%g1				! window (i)
	ldn	[%g5 + MPM_YF_PARS_M2_OFFS], %g1

	ldn	[%g5 + MPM_YF_PARS_MPMUL_OFFS], %g1
	call	%g1
	nop

	ldn	[%g5 + MPM_YF_PARS_STORE_RES_OFFS], %g1
	call	%g1				! window (i-4)
	ldn	[%g5 + MPM_YF_PARS_RES_OFFS], %g1

#ifndef _sparcv9
	restore				! window (i-5)
	restore				! window (i-6)
	restore	%sp, %g0, %o0		! window (i-7)
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
	SET_SIZE(mpm_yf_mpmul)


#include "mpmul_tables.h"

#endif /* lint */
