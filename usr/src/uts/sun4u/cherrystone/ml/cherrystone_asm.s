/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#if defined(lint)
#include <sys/types.h>
#else
#include "assym.h"
#endif /* lint */

#include <sys/asm_linkage.h>
#include <sys/param.h>
#include <sys/privregs.h>
#include <sys/machasi.h>
#include <sys/mmu.h>
#include <sys/machthread.h>
#include <sys/pte.h>
#include <sys/stack.h>
#include <sys/vis.h>
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/vtrace.h>
#include <sys/clock.h>
#include <sys/asi.h>
#include <sys/fsr.h>
#include <sys/cheetahregs.h>

/*
 * NOTE: This file is identical to the one found under sun4u/daktari/ml.
 * If its found that either of these files will not change it might
 * be a good idea to move this up to sun4u/ml.
 */

#if defined(lint)

/* ARGSUSED */
uint64_t
lddmcdecode(uint64_t physaddr)
{
	return (0x0ull);
}

/* ARGSUSED */
uint64_t
lddsafaddr(uint64_t physaddr)
{
	return (0x0ull);
}

#else /* !lint */

!
! Load the safari address for a specific cpu
!
!
	ENTRY(lddsafaddr)
#ifndef __sparcv9
	sllx	%o0, 32, %o0	! shift upper 32 bits
	srl	%o1, 0, %o1	! clear upper 32 bits
	or	%o0, %o1, %o0	! form 64 bit physaddr in %o0 using (%o0,%o1)
#endif
	rdpr	%pstate, %o4
	andn	%o4, PSTATE_IE | PSTATE_AM, %o5
	wrpr	%o5, 0, %pstate	! clear IE, AM bits
#ifdef __sparcv9
	ldxa	[%o0]ASI_SAFARI_CONFIG, %o0
#else
	ldxa	[%o0]ASI_SAFARI_CONFIG, %g1
	srlx	%g1, 32, %o0	! put the high 32 bits in low part of o0
	srl	%g1, 0, %o1	! put lower 32 bits in o1, clear upper 32 bits
#endif
	retl
	wrpr	%g0, %o4, %pstate	! restore earlier pstate register value
	SET_SIZE(lddsafaddr)

!
! Load the mc_decode reg for this cpu.
!
!
	ENTRY(lddmcdecode)
#ifndef __sparcv9
	sllx	%o0, 32, %o0	! shift upper 32 bits
	srl	%o1, 0, %o1	! clear upper 32 bits
	or	%o0, %o1, %o0	! form 64 bit physaddr in %o0 using (%o0,%o1)
#endif
	rdpr	%pstate, %o4
	andn	%o4, PSTATE_IE | PSTATE_AM, %o5
	wrpr	%o5, 0, %pstate	! clear IE, AM bits
#ifdef __sparcv9
	ldxa	[%o0]ASI_MC_DECODE, %o0
#else
	ldxa	[%o0]ASI_MC_DECODE, %g1
	srlx	%g1, 32, %o0	! put the high 32 bits in low part of o0
	srl	%g1, 0, %o1	! put lower 32 bits in o1, clear upper 32 bits
#endif
	retl
	wrpr	%g0, %o4, %pstate	! restore earlier pstate register value
	SET_SIZE(lddmcdecode)

#endif /* lint */
