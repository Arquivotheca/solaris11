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
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*LINTLIBRARY*/

#if defined(lint) || defined(__lint)

#include <sys/types.h>
#include <sys/sha2.h>

/*ARGSUSED*/
void yf_sha256(SHA2_CTX *ctx, const uint8_t *blk)
{ return; }

/*ARGSUSED*/
void yf_sha512(SHA2_CTX *ctx, const uint8_t *blk)
{ return; }

#else	/* lint || __lint */

#include<sys/asm_linkage.h>

	ENTRY(yf_sha256)

	add	%o0, 0x8, %o0		!skip over first field in ctx

!load result from previous digest (stored in ctx)
	ld	[%o0], %f0
	ld	[%o0 + 0x4], %f1
	ld	[%o0 + 0x8], %f2
	ld	[%o0 + 0xc], %f3
	ld	[%o0 + 0x10], %f4
	ld	[%o0 + 0x14], %f5
	ld	[%o0 + 0x18], %f6
	ld	[%o0 + 0x1c], %f7

!load 64 bytes of data
	ld	[%o1], %f8		!load 4 bytes of data
	ld	[%o1 + 0x4], %f9	!load 4 bytes of data
	ld	[%o1 + 0x8], %f10	!load 4 bytes of data
	ld	[%o1 + 0xc], %f11	!load 4 bytes of data
	ld	[%o1 + 0x10], %f12	!load 4 bytes of data
	ld	[%o1 + 0x14], %f13	!load 4 bytes of data
	ld	[%o1 + 0x18], %f14	!load 4 bytes of data
	ld	[%o1 + 0x1c], %f15	!load 4 bytes of data
	ld	[%o1 + 0x20], %f16	!load 4 bytes of data
	ld	[%o1 + 0x24], %f17	!load 4 bytes of data
	ld	[%o1 + 0x28], %f18	!load 4 bytes of data
	ld	[%o1 + 0x2c], %f19	!load 4 bytes of data
	ld	[%o1 + 0x30], %f20	!load 4 bytes of data
	ld	[%o1 + 0x34], %f21	!load 4 bytes of data
	ld	[%o1 + 0x38], %f22	!load 4 bytes of data
	ld	[%o1 + 0x3c], %f23	!load 4 bytes of data

!perform crypto instruction here
	sha256

!copy digest back into ctx
	st	%f0, [%o0]
	st	%f1, [%o0 + 0x4]
	st	%f2, [%o0 + 0x8]
	st	%f3, [%o0 + 0xc]
	st	%f4, [%o0 + 0x10]
	st	%f5, [%o0 + 0x14]
	st	%f6, [%o0 + 0x18]
	retl
	st	%f7, [%o0 + 0x1c]

	SET_SIZE(yf_sha256)


	ENTRY(yf_sha256_multiblock)

	add	%o0, 0x8, %o0		!skip over first field in ctx

!load result from previous digest (stored in ctx)
	ld	[%o0], %f0
	ld	[%o0 + 0x4], %f1
	ld	[%o0 + 0x8], %f2
	ld	[%o0 + 0xc], %f3
	ld	[%o0 + 0x10], %f4
	ld	[%o0 + 0x14], %f5
	ld	[%o0 + 0x18], %f6
	ld	[%o0 + 0x1c], %f7

	and	%o1, 7, %o3
	brnz	%o3, sha256_unaligned_input
	nop

sha256_loop:	

!load 64 bytes of data
	ldd	[%o1], %f8		!load 8 bytes of data
	ldd	[%o1 + 0x8], %f10	!load 8 bytes of data
	ldd	[%o1 + 0x10], %f12	!load 8 bytes of data
	ldd	[%o1 + 0x18], %f14	!load 8 bytes of data
	ldd	[%o1 + 0x20], %f16	!load 8 bytes of data
	ldd	[%o1 + 0x28], %f18	!load 8 bytes of data
	ldd	[%o1 + 0x30], %f20	!load 8 bytes of data
	ldd	[%o1 + 0x38], %f22	!load 8 bytes of data

!perform crypto instruction here
	sha256

	dec	%o2
	brnz	%o2, sha256_loop
	add	%o1, 0x40, %o1

!copy digest back into ctx
	st	%f0, [%o0]
	st	%f1, [%o0 + 0x4]
	st	%f2, [%o0 + 0x8]
	st	%f3, [%o0 + 0xc]
	st	%f4, [%o0 + 0x10]
	st	%f5, [%o0 + 0x14]
	st	%f6, [%o0 + 0x18]
	retl
	st	%f7, [%o0 + 0x1c]

sha256_unaligned_input:
	alignaddr %o1, %g0, %g0		! generate %gsr
	andn	%o1, 7, %o1

sha256_unaligned_input_loop:
	ldd	[%o1], %f8		!load 8 bytes of data
	ldd	[%o1 + 0x8], %f10	!load 8 bytes of data
	ldd	[%o1 + 0x10], %f12	!load 8 bytes of data
	ldd	[%o1 + 0x18], %f14	!load 8 bytes of data
	ldd	[%o1 + 0x20], %f16	!load 8 bytes of data
	ldd	[%o1 + 0x28], %f18	!load 8 bytes of data
	ldd	[%o1 + 0x30], %f20	!load 8 bytes of data
	ldd	[%o1 + 0x38], %f22	!load 8 bytes of data
	ldd	[%o1 + 0x40], %f24	!load 8 bytes of data
	faligndata %f8, %f10, %f8
	faligndata %f10, %f12, %f10
	faligndata %f12, %f14, %f12
	faligndata %f14, %f16, %f14
	faligndata %f16, %f18, %f16
	faligndata %f18, %f20, %f18
	faligndata %f20, %f22, %f20
	faligndata %f22, %f24, %f22

!perform crypto instruction here
	sha256

	dec	%o2
	brnz	%o2, sha256_unaligned_input_loop
	add	%o1, 0x40, %o1

!copy digest back into ctx
	st	%f0, [%o0]
	st	%f1, [%o0 + 0x4]
	st	%f2, [%o0 + 0x8]
	st	%f3, [%o0 + 0xc]
	st	%f4, [%o0 + 0x10]
	st	%f5, [%o0 + 0x14]
	st	%f6, [%o0 + 0x18]
	retl
	st	%f7, [%o0 + 0x1c]

	SET_SIZE(yf_sha256_multiblock)


	ENTRY(yf_sha512)

	add	%o0, 0x8, %o0		!skip over first field in ctx

!load result from previous digest (stored in ctx)
	ld	[%o0], %f0
	ld	[%o0 + 0x4], %f1
	ld	[%o0 + 0x8], %f2
	ld	[%o0 + 0xc], %f3
	ld	[%o0 + 0x10], %f4
	ld	[%o0 + 0x14], %f5
	ld	[%o0 + 0x18], %f6
	ld	[%o0 + 0x1c], %f7
	ld	[%o0 + 0x20], %f8
	ld	[%o0 + 0x24], %f9
	ld	[%o0 + 0x28], %f10
	ld	[%o0 + 0x2c], %f11
	ld	[%o0 + 0x30], %f12
	ld	[%o0 + 0x34], %f13
	ld	[%o0 + 0x38], %f14
	ld	[%o0 + 0x3c], %f15

!load 128 bytes of data
	ld	[%o1], %f16		!load 4 bytes of data
	ld	[%o1 + 0x4], %f17	!load 4 bytes of data
	ld	[%o1 + 0x8], %f18	!load 4 bytes of data
	ld	[%o1 + 0xc], %f19	!load 4 bytes of data
	ld	[%o1 + 0x10], %f20	!load 4 bytes of data
	ld	[%o1 + 0x14], %f21	!load 4 bytes of data
	ld	[%o1 + 0x18], %f22	!load 4 bytes of data
	ld	[%o1 + 0x1c], %f23	!load 4 bytes of data
	ld	[%o1 + 0x20], %f24	!load 4 bytes of data
	ld	[%o1 + 0x24], %f25	!load 4 bytes of data
	ld	[%o1 + 0x28], %f26	!load 4 bytes of data
	ld	[%o1 + 0x2c], %f27	!load 4 bytes of data
	ld	[%o1 + 0x30], %f28	!load 4 bytes of data
	ld	[%o1 + 0x34], %f29	!load 4 bytes of data

	ld	[%o1 + 0x40], %f30	!using %f30 as tmp
	ld	[%o1 + 0x44], %f31	!using %f31 as tmp
	fmovd	%f30, %f32

	ld	[%o1 + 0x48], %f30	!using %f30 as tmp
	ld	[%o1 + 0x4c], %f31	!using %f31 as tmp
	fmovd	%f30, %f34

	ld	[%o1 + 0x50], %f30	!using %f30 as tmp
	ld	[%o1 + 0x54], %f31	!using %f31 as tmp
	fmovd	%f30, %f36

	ld	[%o1 + 0x58], %f30	!using %f30 as tmp
	ld	[%o1 + 0x5c], %f31	!using %f31 as tmp
	fmovd	%f30, %f38

	ld	[%o1 + 0x60], %f30	!using %f30 as tmp
	ld	[%o1 + 0x64], %f31	!using %f31 as tmp
	fmovd	%f30, %f40

	ld	[%o1 + 0x68], %f30	!using %f30 as tmp
	ld	[%o1 + 0x6c], %f31	!using %f31 as tmp
	fmovd	%f30, %f42

	ld	[%o1 + 0x70], %f30	!using %f30 as tmp
	ld	[%o1 + 0x74], %f31	!using %f31 as tmp
	fmovd	%f30, %f44

	ld	[%o1 + 0x78], %f30	!using %f30 as tmp
	ld	[%o1 + 0x7c], %f31	!using %f31 as tmp
	fmovd	%f30, %f46

	ld      [%o1 + 0x38], %f30      !finally setting %f30 to proper value
	ld      [%o1 + 0x3c], %f31      !finally setting %f31 to proper value

!perform crypto instruction here
	sha512

!copy digest back into ctx
	st	%f0, [%o0]
	st	%f1, [%o0+ 0x4]
	st	%f2, [%o0+ 0x8]
	st	%f3, [%o0+ 0xc]
	st	%f4, [%o0+ 0x10]
	st	%f5, [%o0+ 0x14]
	st	%f6, [%o0+ 0x18]
	st	%f7, [%o0+ 0x1c]
	st	%f8, [%o0+ 0x20]
	st	%f9, [%o0+ 0x24]
	st	%f10, [%o0+ 0x28]
	st	%f11, [%o0+ 0x2c]
	st	%f12, [%o0+ 0x30]
	st	%f13, [%o0+ 0x34]
	st	%f14, [%o0+ 0x38]
	retl
	st	%f15, [%o0+ 0x3c]

	SET_SIZE(yf_sha512)


	ENTRY(yf_sha512_multiblock)

	add	%o0, 0x8, %o0		!skip over first field in ctx

!load result from previous digest (stored in ctx)
	ld	[%o0], %f0
	ld	[%o0 + 0x4], %f1
	ld	[%o0 + 0x8], %f2
	ld	[%o0 + 0xc], %f3
	ld	[%o0 + 0x10], %f4
	ld	[%o0 + 0x14], %f5
	ld	[%o0 + 0x18], %f6
	ld	[%o0 + 0x1c], %f7
	ld	[%o0 + 0x20], %f8
	ld	[%o0 + 0x24], %f9
	ld	[%o0 + 0x28], %f10
	ld	[%o0 + 0x2c], %f11
	ld	[%o0 + 0x30], %f12
	ld	[%o0 + 0x34], %f13
	ld	[%o0 + 0x38], %f14
	ld	[%o0 + 0x3c], %f15

	and	%o1, 7, %o3
	brnz	%o3, sha512_unaligned_input
	nop

sha512_loop:	

!load 128 bytes of data
	ldd	[%o1], %f16		!load 8 bytes of data
	ldd	[%o1 + 0x8], %f18	!load 8 bytes of data
	ldd	[%o1 + 0x10], %f20	!load 8 bytes of data
	ldd	[%o1 + 0x18], %f22	!load 8 bytes of data
	ldd	[%o1 + 0x20], %f24	!load 8 bytes of data
	ldd	[%o1 + 0x28], %f26	!load 8 bytes of data
	ldd	[%o1 + 0x30], %f28	!load 8 bytes of data
	ldd	[%o1 + 0x38], %f30	!load 8 bytes of data
	ldd	[%o1 + 0x40], %f32	!load 8 bytes of data
	ldd	[%o1 + 0x48], %f34	!load 8 bytes of data
	ldd	[%o1 + 0x50], %f36	!load 8 bytes of data
	ldd	[%o1 + 0x58], %f38	!load 8 bytes of data
	ldd	[%o1 + 0x60], %f40	!load 8 bytes of data
	ldd	[%o1 + 0x68], %f42	!load 8 bytes of data
	ldd	[%o1 + 0x70], %f44	!load 8 bytes of data
	ldd	[%o1 + 0x78], %f46	!load 8 bytes of data

!perform crypto instruction here
	sha512

	dec	%o2
	brnz	%o2, sha512_loop
	add	%o1, 0x80, %o1

!copy digest back into ctx
	st	%f0, [%o0]
	st	%f1, [%o0 + 0x4]
	st	%f2, [%o0 + 0x8]
	st	%f3, [%o0 + 0xc]
	st	%f4, [%o0 + 0x10]
	st	%f5, [%o0 + 0x14]
	st	%f6, [%o0 + 0x18]
	st	%f7, [%o0+ 0x1c]
	st	%f8, [%o0+ 0x20]
	st	%f9, [%o0+ 0x24]
	st	%f10, [%o0+ 0x28]
	st	%f11, [%o0+ 0x2c]
	st	%f12, [%o0+ 0x30]
	st	%f13, [%o0+ 0x34]
	st	%f14, [%o0+ 0x38]
	retl
	st	%f15, [%o0+ 0x3c]

sha512_unaligned_input:
	alignaddr %o1, %g0, %g0		! generate %gsr
	andn	%o1, 7, %o1

sha512_unaligned_input_loop:
	ldd	[%o1], %f16		!load 8 bytes of data
	ldd	[%o1 + 0x8], %f18	!load 8 bytes of data
	ldd	[%o1 + 0x10], %f20	!load 8 bytes of data
	ldd	[%o1 + 0x18], %f22	!load 8 bytes of data
	ldd	[%o1 + 0x20], %f24	!load 8 bytes of data
	ldd	[%o1 + 0x28], %f26	!load 8 bytes of data
	ldd	[%o1 + 0x30], %f28	!load 8 bytes of data
	ldd	[%o1 + 0x38], %f30	!load 8 bytes of data
	ldd	[%o1 + 0x40], %f32	!load 8 bytes of data
	ldd	[%o1 + 0x48], %f34	!load 8 bytes of data
	ldd	[%o1 + 0x50], %f36	!load 8 bytes of data
	ldd	[%o1 + 0x58], %f38	!load 8 bytes of data
	ldd	[%o1 + 0x60], %f40	!load 8 bytes of data
	ldd	[%o1 + 0x68], %f42	!load 8 bytes of data
	ldd	[%o1 + 0x70], %f44	!load 8 bytes of data
	ldd	[%o1 + 0x78], %f46	!load 8 bytes of data
	ldd	[%o1 + 0x80], %f48	!load 8 bytes of data
	faligndata %f16, %f18, %f16
	faligndata %f18, %f20, %f18
	faligndata %f20, %f22, %f20
	faligndata %f22, %f24, %f22
	faligndata %f24, %f26, %f24
	faligndata %f26, %f28, %f26
	faligndata %f28, %f30, %f28
	faligndata %f30, %f32, %f30
	faligndata %f32, %f34, %f32
	faligndata %f34, %f36, %f34
	faligndata %f36, %f38, %f36
	faligndata %f38, %f40, %f38
	faligndata %f40, %f42, %f40
	faligndata %f42, %f44, %f42
	faligndata %f44, %f46, %f44
	faligndata %f46, %f48, %f46

!perform crypto instruction here
	sha512

	dec	%o2
	brnz	%o2, sha512_unaligned_input_loop
	add	%o1, 0x80, %o1

!copy digest back into ctx
	st	%f0, [%o0]
	st	%f1, [%o0 + 0x4]
	st	%f2, [%o0 + 0x8]
	st	%f3, [%o0 + 0xc]
	st	%f4, [%o0 + 0x10]
	st	%f5, [%o0 + 0x14]
	st	%f6, [%o0 + 0x18]
	st	%f7, [%o0+ 0x1c]
	st	%f8, [%o0+ 0x20]
	st	%f9, [%o0+ 0x24]
	st	%f10, [%o0+ 0x28]
	st	%f11, [%o0+ 0x2c]
	st	%f12, [%o0+ 0x30]
	st	%f13, [%o0+ 0x34]
	st	%f14, [%o0+ 0x38]
	retl
	st	%f15, [%o0+ 0x3c]

	SET_SIZE(yf_sha512_multiblock)

#endif  /* lint || __lint */
