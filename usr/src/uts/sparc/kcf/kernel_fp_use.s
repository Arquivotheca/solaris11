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
#include <sys/types.h>
#else
#include <kernel_fp_use_offs.h>
#endif
	
#include <sys/stack.h>
#include <sys/fsr.h>
#include <sys/asm_linkage.h>

#if defined(lint)

#include "kernel_fp_use.h"

/* LINTED E_FUNC_ARG_UNUSED */
void start_kernel_fp_use(fp_save_t *fp_save_buf)
{}

/* LINTED E_FUNC_ARG_UNUSED */
void end_kernel_fp_use(fp_save_t *fp_save_buf)
{}

#else	/* lint */
	
#define	SAVED_GSR_OFFSET	0
#define	SAVED_FPRS_OFFSET	8
#define	SAVED_FPREGS_OFFSET	12

#define	VIS_BLOCKSIZE		64
#define	THREAD_REG		%g7
#define	ASI_BLK_P		0xF0


	.section	".text",#alloc,#execinstr
	.align	8
	.skip	16


	ENTRY(start_kernel_fp_use)
	save	%sp, -SA(MINFRAME), %sp
	/* we are preventing the thread from migrating */
	ldn	[THREAD_REG + T_LWP], %o0
	brz,a,pn %o0, 1f			! jump if kernel thread
	  ldsb	[THREAD_REG + T_PREEMPT], %o1
	call	thread_nomigrate
	  nop
	ba	2f
	  nop
1:
	inc	%o1			! kpreempt_disable()
	stb	%o1, [THREAD_REG + T_PREEMPT]
2:
	rd	%fprs, %o2		! check for unused fp
	st	%o2, [%i0 + SAVED_FPRS_OFFSET] ! save orig %fprs
	btst	FPRS_FEF, %o2
	bz,a,pt	%icc, 3f
	  wr	%g0, FPRS_FEF, %fprs

	/* save fp registers into the fp_save_t area provided by the caller */
	membar #Sync
	add	%i0, SAVED_FPREGS_OFFSET + VIS_BLOCKSIZE, %o0
	and	%o0, -VIS_BLOCKSIZE, %o0	! block align
	stda	%f0, [%o0]ASI_BLK_P
	add	%o0, VIS_BLOCKSIZE, %o0
	stda	%f16, [%o0]ASI_BLK_P
	add	%o0, VIS_BLOCKSIZE, %o0
	stda	%f32, [%o0]ASI_BLK_P
	add	%o0, VIS_BLOCKSIZE, %o0
	stda	%f48, [%o0]ASI_BLK_P	
	membar	#Sync

3:
	rd	%gsr, %o2
	stx	%o2, [%i0 + SAVED_GSR_OFFSET]	! save gsr

	ret
	restore
	SET_SIZE(start_kernel_fp_use)


	ENTRY(end_kernel_fp_use)
	save	%sp, -SA(MINFRAME), %sp

	ldx	[%i0 + SAVED_GSR_OFFSET], %o0	! restore gsr
	wr	%o0, 0, %gsr

	ld	[%i0 + SAVED_FPRS_OFFSET], %o1
	btst	FPRS_FEF, %o1
	bz,pt	%icc, 1f
	  nop

	/* restore fp registers from the fp_save_t area */
	membar #Sync
	add	%i0, SAVED_FPREGS_OFFSET + VIS_BLOCKSIZE, %o0
	and	%o0, -VIS_BLOCKSIZE, %o0	! block align
	ldda	[%o0]ASI_BLK_P, %f0
	add	%o0, VIS_BLOCKSIZE, %o0
	ldda	[%o0]ASI_BLK_P, %f16
	add	%o0, VIS_BLOCKSIZE, %o0
	ldda	[%o0]ASI_BLK_P, %f32
	add	%o0, VIS_BLOCKSIZE, %o0
	ldda	[%o0]ASI_BLK_P, %f48
	membar	#Sync			! sync error barrier

	ba,pt	%ncc, 2f	
	  wr	%o1, 0, %fprs		! restore fprs
1:
	/* clean fp registers */
	fzero	%f0
	fzero	%f2
	faddd	%f0, %f2, %f4
	fmuld	%f0, %f2, %f6
	faddd	%f0, %f2, %f8
	fmuld	%f0, %f2, %f10
	faddd	%f0, %f2, %f12
	fmuld	%f0, %f2, %f14
	faddd	%f0, %f2, %f16
	fmuld	%f0, %f2, %f18
	faddd	%f0, %f2, %f20
	fmuld	%f0, %f2, %f22
	faddd	%f0, %f2, %f24
	fmuld	%f0, %f2, %f26
	faddd	%f0, %f2, %f28
	fmuld	%f0, %f2, %f30
	faddd	%f0, %f2, %f32
	fmuld	%f0, %f2, %f34
	faddd	%f0, %f2, %f36
	fmuld	%f0, %f2, %f38
	faddd	%f0, %f2, %f40
	fmuld	%f0, %f2, %f42
	faddd	%f0, %f2, %f44
	fmuld	%f0, %f2, %f46
	faddd	%f0, %f2, %f48
	fmuld	%f0, %f2, %f50
	faddd	%f0, %f2, %f52
	fmuld	%f0, %f2, %f54
	faddd	%f0, %f2, %f56
	fmuld	%f0, %f2, %f58
	faddd	%f0, %f2, %f60
	fmuld	%f0, %f2, %f62

	wr	%o1, 0, %fprs		! restore fprs
2:

	ldn	[THREAD_REG + T_LWP], %o0
	brz,a,pn %o0, 3f
	  ldsb	[THREAD_REG + T_PREEMPT], %o1
	call thread_allowmigrate
	  nop
	ret
	restore
3:	
	dec	%o1
	brnz,pn	%o1, 4f
	  stb	%o1, [THREAD_REG + T_PREEMPT]
	ldn	[THREAD_REG + T_CPU], %o0
	ldub	[%o0 + CPU_KPRUNRUN], %o0
	brz,pt	%o0, 4f
	  nop
	call	kpreempt
	  rdpr	%pil, %o0
4:
	ret
	restore
	SET_SIZE(end_kernel_fp_use)
#endif /* lint */
