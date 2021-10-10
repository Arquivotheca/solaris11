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

#if !defined(lint)
#include "assym.h"
#endif

/*
 * YF processor specific assembly routines
 */

#include <sys/asm_linkage.h>
#include <sys/machasi.h>
#include <sys/machparam.h>
#include <sys/hypervisor_api.h>
#define VT_IMPL
#include <sys/niagara2regs.h>
#include <sys/machasi.h>
#include <sys/niagaraasi.h>
#include <vm/hat_sfmmu.h>

#if defined(lint)
/*ARGSUSED*/
uint64_t
hv_getperf(uint64_t perfreg, uint64_t *datap)
{ return (0); }

/*ARGSUSED*/
uint64_t
hv_setperf(uint64_t perfreg, uint64_t data)
{ return (0); }

#else   /* lint */

	/*
	 * hv_getperf(uint64_t perfreg, uint64_t *datap)
	 */
	ENTRY(hv_getperf)
	mov	%o1, %o4		! save datap
	sethi	%hi(hv_getperf_func_num), %o5
	ld	[%o5 + %lo(hv_getperf_func_num)], %o5
	ta	FAST_TRAP
	brz,a	%o0, 1f
	stx	%o1, [%o4]
1:
	retl
	nop
	SET_SIZE(hv_getperf)

	/*
	 * hv_setperf(uint64_t perfreg, uint64_t data)
	 */
	ENTRY(hv_setperf)
	sethi	%hi(hv_setperf_func_num), %o5
	ld	[%o5 + %lo(hv_setperf_func_num)], %o5
	ta	FAST_TRAP
	retl
	nop
	SET_SIZE(hv_setperf)

#endif /* !lint */

/*
 * On VT the access to PICs is through non-privileged ASIs.
 */

#if defined(lint)
/* ARGSUSED */
void
vt_setpic(uint64_t picno, uint64_t pic)
{ }

#else   /* lint */

	ENTRY(vt_setpic)
	sllx	%o0, VT_PERFREG_SHIFT, %g1
	stxa	%o1, [%g1]ASI_PIC
	retl
	nop
	SET_SIZE(vt_setpic)
#endif  /* lint */

#if defined(lint)
/* ARGSUSED */
uint64_t
vt_getpic(uint64_t picno)
{ return (0); }

#else   /* lint */

	ENTRY(vt_getpic)
	sllx	%o0, VT_PERFREG_SHIFT, %g1
	ldxa	[%g1]ASI_PIC, %o0
	retl
	nop
	SET_SIZE(vt_getpic)
#endif  /* lint */
