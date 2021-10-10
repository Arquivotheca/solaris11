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
 * Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#if !defined(lint)
#include "assym.h"
#endif

/*
 * Niagara2 processor specific assembly routines
 */

#include <sys/asm_linkage.h>
#include <sys/machasi.h>
#include <sys/machparam.h>
#include <sys/hypervisor_api.h>
#include <sys/niagara2regs.h>
#include <sys/machasi.h>
#include <sys/niagaraasi.h>
#include <vm/hat_sfmmu.h>

#if defined(lint)
/*ARGSUSED*/
uint64_t
hv_niagara_getperf(uint64_t perfreg, uint64_t *datap)
{ return (0); }

/*ARGSUSED*/
uint64_t
hv_niagara_setperf(uint64_t perfreg, uint64_t data)
{ return (0); }

#else   /* lint */

	/*
	 * hv_niagara_getperf(uint64_t perfreg, uint64_t *datap)
	 */
	ENTRY(hv_niagara_getperf)
	mov     %o1, %o4                        ! save datap
#if defined(NIAGARA2_IMPL)
	mov     HV_NIAGARA2_GETPERF, %o5
#elif defined(VFALLS_IMPL)
	mov	HV_VFALLS_GETPERF, %o5
#elif defined(KT_IMPL)
	mov	HV_KT_GETPERF, %o5
#elif defined(VT_IMPL)
	mov	HV_VT_GETPERF, %o5
#endif
	ta      FAST_TRAP
	brz,a   %o0, 1f
	stx     %o1, [%o4]
1:
	retl
	nop
	SET_SIZE(hv_niagara_getperf)

	/*
	 * hv_niagara_setperf(uint64_t perfreg, uint64_t data)
	 */
	ENTRY(hv_niagara_setperf)
#if defined(NIAGARA2_IMPL)
	mov     HV_NIAGARA2_SETPERF, %o5
#elif defined(VFALLS_IMPL)
	mov     HV_VFALLS_SETPERF, %o5
#elif defined(KT_IMPL)
	mov     HV_KT_SETPERF, %o5
#elif defined(VT_IMPL)
	mov     HV_VT_SETPERF, %o5
#endif
	ta      FAST_TRAP
	retl
	nop
	SET_SIZE(hv_niagara_setperf)

#endif /* !lint */

#if defined (lint)
/*
 * Invalidate all of the entries within the TSB, by setting the inv bit
 * in the tte_tag field of each tsbe.
 *
 * We take advantage of the fact that the TSBs are page aligned and a
 * multiple of PAGESIZE to use ASI_BLK_INIT_xxx ASI.
 *
 * See TSB_LOCK_ENTRY and the miss handlers for how this works in practice
 * (in short, we set all bits in the upper word of the tag, and we give the
 * invalid bit precedence over other tag bits in both places).
 */
/*ARGSUSED*/
void
cpu_inv_tsb(caddr_t tsb_base, uint_t tsb_bytes)
{}

#else /* lint */

	ENTRY(cpu_inv_tsb)

	/*
	 * The following code assumes that the tsb_base (%o0) is 256 bytes
	 * aligned and the tsb_bytes count is multiple of 256 bytes.
	 */

	wr	%g0, ASI_BLK_INIT_ST_QUAD_LDD_P, %asi
	set	TSBTAG_INVALID, %o2
	sllx	%o2, 32, %o2		! INV bit in upper 32 bits of the tag
1:
	stxa	%o2, [%o0+0x0]%asi
	stxa	%o2, [%o0+0x40]%asi
	stxa	%o2, [%o0+0x80]%asi
	stxa	%o2, [%o0+0xc0]%asi

	stxa	%o2, [%o0+0x10]%asi
	stxa	%o2, [%o0+0x20]%asi
	stxa	%o2, [%o0+0x30]%asi

	stxa	%o2, [%o0+0x50]%asi
	stxa	%o2, [%o0+0x60]%asi
	stxa	%o2, [%o0+0x70]%asi

	stxa	%o2, [%o0+0x90]%asi
	stxa	%o2, [%o0+0xa0]%asi
	stxa	%o2, [%o0+0xb0]%asi

	stxa	%o2, [%o0+0xd0]%asi
	stxa	%o2, [%o0+0xe0]%asi
	stxa	%o2, [%o0+0xf0]%asi

	subcc	%o1, 0x100, %o1
	bgu,pt	%ncc, 1b
	add	%o0, 0x100, %o0

	membar	#Sync
	retl
	nop

	SET_SIZE(cpu_inv_tsb)
#endif /* lint */

#if defined (lint)
/*
 * This is CPU specific delay routine for atomic backoff. It is used in case
 * of Niagara2 and VF CPUs. The rd instruction uses less resources than casx
 * on these CPUs.
 * For VT we use the wrpause instruction. The pause value needs to be tuned. 
 */
void
cpu_atomic_delay(void)
{}
#else	/* lint */
	ENTRY(cpu_atomic_delay)
#if defined(VT_IMPL)
	wr	%g0, 100, %pause
	retl
	nop
#else
	rd	%ccr, %g0
	rd	%ccr, %g0
	retl
	rd	%ccr, %g0
#endif /* VT_IMPL */
	SET_SIZE(cpu_atomic_delay)
#endif	/* lint */

#if defined(VT_IMPL)
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
{ }

#else   /* lint */

	ENTRY(vt_getpic)
	sllx	%o0, VT_PERFREG_SHIFT, %g1
	ldxa	[%g1]ASI_PIC, %o0
	retl
	nop
	SET_SIZE(vt_getpic)
#endif  /* lint */

#if defined(lint)

/*
 * Use the wrpause instruction for YF processors.  The wrpause instruction
 * does nothing efficiently.  Note: we use wrasr to keep assembler compatible.
 * wrpause is just wrasr 27.  This function is used in mutex delay loops.
 * The value 100 is just a guess: it will need tuning later.
 */
void
wrpause_delay(void)
{}
#else	/* lint */
	ENTRY(wrpause_delay)
	wr	%g0, 100, %pause
	retl
	nop
	SET_SIZE(wrpause_delay)
#endif	/* lint */

#endif /* VT_IMPL */
