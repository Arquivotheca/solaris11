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
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#if defined(__lint)
#include <setjmp.h>
#endif

#include <sys/asm_linkage.h>

/*
 * This is a copy of the setjmp (and longjmp) code used in libc.  Note that
 * we use sigsetjmp as an alias for setjmp, with a corresponding alias between
 * siglongjmp and longjmp.  We can do this because there aren't any signals
 * in kmdb (with the possible exception of the smoke signals the machine will
 * emit when we break something).  We can also use a sigjmp_buf as a jmp_buf,
 * since the latter is smaller than the former.
 */

#if !defined(__lint)
JB_FLAGS	= (0*8)	! offsets in jmpbuf (see sigsetjmp.c)
JB_SP		= (1*8)	! words 5 through 11 are unused!
JB_PC		= (2*8)
JB_FP		= (3*8)
JB_I7		= (4*8)
#endif

/*
 * setjmp(buf_ptr)
 * buf_ptr points to a twelve word array (jmp_buf)
 */

#if defined(__lint)
/* ARGSUSED */
int 
setjmp(jmp_buf env)
{
	return (0);
}

/* ARGSUSED */
int
sigsetjmp(sigjmp_buf env, int savemask)
{
	return (0);
}
#else	/* __lint */

	ENTRY(setjmp)
	ALTENTRY(sigsetjmp)
	clr	[%o0 + JB_FLAGS]	! clear flags (used by sigsetjmp)
	stx	%sp, [%o0 + JB_SP]	! save caller's sp
	add	%o7, 8, %o1		! compute return pc
	stx	%o1, [%o0 + JB_PC]	! save pc
	stx	%fp, [%o0 + JB_FP]	! save fp
	stx	%i7, [%o0 + JB_I7]	! save %i7
	flushw
	retl
	clr	%o0			! return (0)

	SET_SIZE(setjmp)
#endif	/* __lint */

/*
 * longjmp(buf_ptr, val)
 * buf_ptr points to a jmpbuf which has been initialized by setjmp.
 * val is the value we wish to return to setjmp's caller
 *
 * We flush the register file to the stack by doing a kernel call.
 * This is necessary to ensure that the registers we want to
 * pick up are stored on the stack, and that subsequent restores
 * will function correctly.
 *
 * sp, fp, and %i7, the caller's return address, are all restored
 * to the values they had at the time of the call to setjmp().  All
 * other locals, ins and outs are set to potentially random values
 * (as per the man page).  This is sufficient to permit the correct
 * operation of normal code.
 *
 * Actually, the above description is not quite correct.  If the routine
 * that called setjmp() has not altered the sp value of their frame we
 * will restore the remaining locals and ins to the values these
 * registers had in the this frame at the time of the call to longjmp()
 * (not setjmp()!).  This is intended to help compilers, typically not
 * C compilers, that have some registers assigned to fixed purposes,
 * and that only alter the values of these registers on function entry
 * and exit.
 *
 * Since a C routine could call setjmp() followed by alloca() and thus
 * alter the sp this feature will typically not be helpful for a C
 * compiler.
 *
 * Note also that because the caller of a routine compiled "flat" (without
 * register windows) assumes that their ins and locals are preserved,
 * routines that call setjmp() must not be flat.
 */

#if defined(__lint)
/* ARGSUSED */
void 
longjmp(jmp_buf env, int val)
{
}

/* ARGSUSED */
void 
siglongjmp(sigjmp_buf env, int val)
{
}
#else	/* __lint */

	ENTRY(longjmp)
	ALTENTRY(siglongjmp)

	/* flush all reg windows to the stack. */
	save
	flushw
	restore
	nop

	ldx	[%o0 + JB_SP], %o2	! sp in %o2 until safe to puke there
	ldx	[%o2 + STACK_BIAS], %l0	! restore locals and ins if we can
	ldx	[%o2 + (1*8) + STACK_BIAS], %l1
	ldx	[%o2 + (2*8) + STACK_BIAS], %l2
	ldx	[%o2 + (3*8) + STACK_BIAS], %l3
	ldx	[%o2 + (4*8) + STACK_BIAS], %l4
	ldx	[%o2 + (5*8) + STACK_BIAS], %l5
	ldx	[%o2 + (6*8) + STACK_BIAS], %l6
	ldx	[%o2 + (7*8) + STACK_BIAS], %l7
	ldx	[%o2 + (8*8) + STACK_BIAS], %i0
	ldx	[%o2 + (9*8) + STACK_BIAS], %i1
	ldx	[%o2 + (10*8) + STACK_BIAS], %i2
	ldx	[%o2 + (11*8) + STACK_BIAS], %i3
	ldx	[%o2 + (12*8) + STACK_BIAS], %i4
	ldx	[%o2 + (13*8) + STACK_BIAS], %i5
	ldx	[%o0 + JB_FP], %fp	! restore fp
	mov	%o2, %sp		! restore sp
	ldx	[%o0 + JB_I7], %i7	! restore %i7
	ldx	[%o0 + JB_PC], %o3	! get new return pc
	tst	%o1			! is return value 0?
	bnz	1f			! no - leave it alone
	sub	%o3, 8, %o7		! normalize return (for adb) (dly slot)
	mov	1, %o1			! yes - set it to one
1:
	retl
	mov	%o1, %o0		! return (val)

	SET_SIZE(longjmp)
#endif	/* __lint */
