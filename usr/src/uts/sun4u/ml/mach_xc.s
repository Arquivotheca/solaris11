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

#if defined(lint)
#include <sys/types.h>
#include <sys/cpuvar.h>
#else	/*lint */
#include "assym.h"
#endif	/* lint */

#include <sys/asm_linkage.h>
#include <sys/privregs.h>
#include <sys/x_call.h>
#include <sys/xc_impl.h>

#ifdef TRAPTRACE
#include <sys/traptrace.h>
#endif /* TRAPTRACE */


#if defined(lint)

/* ARGSUSED */
void
self_xcall(struct cpu *cpu, uint64_t arg1, uint64_t arg2, xcfunc_t *func)
{}

#else

/*
 * Entered by the software trap (TT=ST_SELFXCALL, TL>0) thru send_self_xcall().
 * Emulate the mondo handler - vec_interrupt().
 *
 * Global registers are the Alternate Globals.
 * Arguments:
 * 	%o0 - CPU
 * 	ILP32 kernel:
 * 		%o5 - function to call
 * 		%o1, %o2, %o3, %o4  - arguments
 * 	LP64 kernel:
 * 		%o3 - function to call
 * 		%o1, %o2 - arguments
 */
	ENTRY_NP(self_xcall)
	!
	! TL>0 handlers are expected to do "retry"
	! prepare their return PC and nPC now
	!
	rdpr	%tnpc, %g1
	wrpr	%g1, %tpc			!  PC <- TNPC[TL]
 	add	%g1, 4, %g1
	wrpr	%g1, %tnpc			! nPC <- TNPC[TL] + 4

#ifdef TRAPTRACE
	TRACE_PTR(%g4, %g6)
	GET_TRACE_TICK(%g6, %g3)
	stxa	%g6, [%g4 + TRAP_ENT_TICK]%asi
	rdpr	%tl, %g6
	stha	%g6, [%g4 + TRAP_ENT_TL]%asi
	rdpr	%tt, %g6
	stha	%g6, [%g4 + TRAP_ENT_TT]%asi
	stna	%o3, [%g4 + TRAP_ENT_TR]%asi ! pc of the TL>0 handler
	rdpr	%tpc, %g6
	stna	%g6, [%g4 + TRAP_ENT_TPC]%asi
	rdpr	%tstate, %g6
	stxa	%g6, [%g4 + TRAP_ENT_TSTATE]%asi
	stna	%sp, [%g4 + TRAP_ENT_SP]%asi
	stna	%o1, [%g4 + TRAP_ENT_F1]%asi ! arg 1
	stna	%o2, [%g4 + TRAP_ENT_F2]%asi ! arg 2
	stna	%g0, [%g4 + TRAP_ENT_F3]%asi
	stna	%g0, [%g4 + TRAP_ENT_F4]%asi
	TRACE_NEXT(%g4, %g6, %g3)
#endif /* TRAPTRACE */
	!
	! Load the arguments for the fast trap handler.
	!
	mov	%o1, %g1
	jmp	%o3				! call the fast trap handler
	mov	%o2, %g2
	/* Not Reached */
	SET_SIZE(self_xcall)

#endif	/* lint */

#ifdef  TRAPTRACE
#if defined(lint)

/* ARGSUSED */
void
xc_trace(u_int traptype, cpuset_t *cpu_set, xcfunc_t *func,
	uint64_t arg1, uint64_t arg2)
{}

#else	/* lint */
	ENTRY(xc_trace)
	rdpr	%pstate, %g1
	andn	%g1, PSTATE_IE | PSTATE_AM, %g2
	wrpr	%g0, %g2, %pstate			/* disable interrupts */
	TRACE_PTR(%g3, %g4)
	GET_TRACE_TICK(%g6, %g4)
	stxa	%g6, [%g3 + TRAP_ENT_TICK]%asi
	stha	%g0, [%g3 + TRAP_ENT_TL]%asi
	set	TT_XCALL, %g2
	or	%o0, %g2, %g4
	stha	%g4, [%g3 + TRAP_ENT_TT]%asi
	stna	%o7, [%g3 + TRAP_ENT_TPC]%asi
	ldn	[%o1], %g2
	stna	%g2, [%g3 + TRAP_ENT_SP]%asi		/* sp = cpuset */
	stna	%o2, [%g3 + TRAP_ENT_TR]%asi		/* tr = func */
	stna	%o3, [%g3 + TRAP_ENT_F1]%asi		/* f1 = arg1 */
	stna	%o4, [%g3 + TRAP_ENT_F2]%asi		/* f2 = arg2 */
	stna	%g0, [%g3 + TRAP_ENT_F3]%asi		/* f3 = 0 */
	stna	%i7, [%g3 + TRAP_ENT_F4]%asi		/* f4 = xcall caller */
	stxa	%g1, [%g3 + TRAP_ENT_TSTATE]%asi	/* tstate = pstate */
	TRACE_NEXT(%g2, %g3, %g4)
/*
 * In the case of a cpuset of greater size than a long we
 * grab extra trace buffers just to store the cpuset.
 * Seems like a waste but popular opinion opted for this 
 * rather than increase the size of the buffer.
 */
#if CPUSET_SIZE > CLONGSIZE
	add	%o1, CPUSET_SIZE, %g5			/* end of cpuset */
	clr	%o2
1:
	TRACE_PTR(%g3, %g4)
	stha	%g0, [%g3 + TRAP_ENT_TL]%asi
	set	TT_XCALL_CONT, %g2
	or	%g2, %o2, %g2				/* continuation # */
	stha	%g2, [%g3 + TRAP_ENT_TT]%asi
	stxa	%g6, [%g3 + TRAP_ENT_TICK]%asi		/* same tick */
	stna	%g0, [%g3 + TRAP_ENT_TPC]%asi		/* clr unused fields */
	stna	%g0, [%g3 + TRAP_ENT_SP]%asi
	stna	%g0, [%g3 + TRAP_ENT_TR]%asi
	stxa	%g0, [%g3 + TRAP_ENT_TSTATE]%asi
	stna	%g0, [%g3 + TRAP_ENT_F2]%asi
	stna	%g0, [%g3 + TRAP_ENT_F3]%asi
	stna	%g0, [%g3 + TRAP_ENT_F4]%asi
	ldn	[%o1], %g2
	stna	%g2, [%g3 + TRAP_ENT_F1]%asi
	add	%o1, CLONGSIZE, %o1
	cmp	%o1, %g5
	bge	2f
	ldn	[%o1], %g2
	stna	%g2, [%g3 + TRAP_ENT_F2]%asi
	add	%o1, CLONGSIZE, %o1
	cmp	%o1, %g5
	bge	2f
	ldn	[%o1], %g2
	stna	%g2, [%g3 + TRAP_ENT_F3]%asi
	add	%o1, CLONGSIZE, %o1
	cmp	%o1, %g5
	bge	2f
	ldn	[%o1], %g2
	stna	%g2, [%g3 + TRAP_ENT_F4]%asi
	add	%o1, CLONGSIZE, %o1
2:	
	TRACE_NEXT(%g2, %g3, %g4)
	cmp	%o1, %g5
	bl	1b
	inc	%o2
#endif	/* CPUSET_SIZE */
	retl
	wrpr	%g0, %g1, %pstate			/* enable interrupts */
	SET_SIZE(xc_trace)

#endif	/* lint */
#endif	/* TRAPTRACE */

#if defined(lint)

/* ARGSUSED */
void
xt_sync_tl1(uint64_t *cpu_sync_addr)
{}

#else
/*
 * This dummy tl1 function is there to ensure that previously called
 * xtrap handlers have exececuted. The hardware (mondo dispatch
 * mechanism) is such that return from xtrap doesn't guarantee execution
 * of xtrap handler. So, callers can call this xtrap-handler to ensure
 * that the previous one is complete. This is because the hardware only
 * can handle 1 mondo at a time - when this mondo is handled, we are sure
 * that the mondo for the previous xtrap must have been handled.
 */
	ENTRY_NP(xt_sync_tl1)
	retry
	SET_SIZE(xt_sync_tl1)

#endif  /* lint */
