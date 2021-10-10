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
 * Copyright (c) 2005, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _SYS_MACHCLOCK_H
#define	_SYS_MACHCLOCK_H

#include <sys/intreg.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	MIG_FREQ_SHIFT	4

/*
 * Macro to do stick scaling based on freqscale value.
 * This is modeled after the clever NSEC_SCALE trick. Check out the
 * NSEC_SCALE comments in clock.h
 */
#define	MIG_SCALE_STICK(out, scr1, scr2, shift)				\
	srlx	out, 32, scr2;		/* check high 32 bits */	\
/* CSTYLED */								\
	brz,a,pt scr2, .+40;		/* if clear, 32-bit fast path */\
	mulx	out, scr1, out;		/* delay: 32-bit fast path */	\
	sllx	out, shift, out;	/* clear NPT and pre-scale */	\
	srlx	out, 32, scr2;		/* scr2 = hi32(tick<<4) = H */	\
	mulx	scr2, scr1, scr2;	/* scr2 = (H*F) */		\
	srl	out, 0, out;		/* out = lo32(tick<<4) = L */	\
	mulx	out, scr1, scr1;	/* scr1 = (L*F) */		\
	srlx	scr1, 32, scr1;		/* scr1 = (L*F) >> 32 */	\
	ba	.+12;			/* branch over 32-bit path */	\
	add	scr1, scr2, out;	/* out = (H*F) + ((L*F)>>32) */	\
	srlx	out, 32 - shift, out;


/*
 * Tick/Stick Register Access
 *
 * The following assembly language macros are defined for reading
 * the %tick and %stick registers as well as reading and writing
 * the stick compare register. With the exception of trapstat, reads
 * and writes of these registers all take into account an offset
 * value which is added to the hardware counter. By default, this
 * offset is zero. The offsets can only be modified when CPUs are
 * paused and are only intended to be modified during an OS suspend
 * operation.
 *
 * Since the read of the %tick or %stick is not an atomic operation,
 * it is possible for a migration suspend operation to occur between
 * the read of the hardware register and its offset variable. The
 * macros here take this into account by comparing the value of the
 * offset variable before and after reading the hardware register.
 * Callers that need to read the %tick register and can guarantee
 * they will not be preempted can use the RD_TICK_NO_SUSPEND_CHECK
 * which does not check for native_tick_offset changing.
 *
 * To support migration between machines with different stick freqs,
 * we implement a mechanism to maintain stick freq invariant from
 * the original src machine across migrations. That is, the stick
 * freq from the original boot src machine never change. When we
 * migrate between two machines with differing stick freqs, we
 * first normalize the stick value of the target to be based on
 * the invariant src (original boot) machine stick freq, next we
 * then compute the stick offset from this normalized target stick
 * value. To obtain a stick value based on an invariant stick freq,
 * we read the HW stick, normalized the value to the invariant stick
 * freq by applying a precompute freqscale factor and then add the
 * stick offset.
 *
 * Here is the algorithm to handle the situation where suspension
 * can occur between reading the HW stick counter and the global variables
 * (there are now 2 of them) to do normalization and apply the offset.
 *
 * native_stick_offset is the offset to be applied.
 * mig_stick_normscale is the scaling factor for normalization. Depending
 * on the scaling/migration scenario, it may be set to match the
 * native_stick_offset or to include an encoding of the native_stick_offset
 * in the upper 32 bits. See below.
 *
 * Case 1: Normal running system, not simulating a stick frequency
 *   and never migrated to a machine with a different frequency.
 *   - we will set native_stick_offset == mig_stick_normscale == 0
 *
 * Case 2: Migrated system where the stick freq has never changed.
 *   - we will set mig_stick_normscale == native_stick_offset
 *   - note that native_stick_offset will be non-zero.
 *
 * Case 3: Migrated to a machine with a stick frequency that differs
 *   from the original boot stick frequency OR we have never migrated
 *   but are simulating a stick frequency and have been since boot.
 *   - we will set the mig_stick_normscale's high 32 bits to be
 *     the low-order 32s bit of the native_stick_offset while the
 *     actual normscale factor occupies the low-order 32 bits.
 *
 * Basic algorithm:
 *
 * 1. Read native_stick_offset(o1).
 * 2. Read the HW Counter.
 * 3. Read mig_stick_normscale(n1).
 * 4. If mig_stick_normscale(n1) == native_stick_offset(o1)
 *       Just do the offset, we don't need to normalize freq.
 *       Done.
 *    else
 *       Re-read the mig_stick_normscale(n2) and if no change from
 *       previous(n1), Do the freq normalization. Otherwise go back
 *       to step 1.
 *       Next, reread the native_stick_offset(o2) and if it's low
 *       32 bit value is still the same as the one in the high
 *       32 bit of the original mig_stick_normscale(n1), add the
 *       native_stick_offset and we are done.
 *       Otherwise go back to step 1.
 */
#define	RD_STICK(out, scr1, scr2, scr3, label)			\
.rd_stick.label:						\
	sethi	%hi(native_stick_offset), scr3;			\
	ldx	[scr3 + %lo(native_stick_offset)], scr1;	\
	rd	STICK, out;					\
	ldx	[scr3 + %lo(mig_stick_normscale)], scr2;	\
	sllx	out, 1, out;					\
	srlx	out, 1, out;					\
	cmp	scr1, scr2;					\
	be	%xcc, .rd_stick_normal.label;			\
	nop;							\
	ldx	[scr3 + %lo(mig_stick_normscale)], scr3;	\
	cmp	scr2, scr3;					\
/* CSTYLED */							\
	bne,pn	%xcc, .rd_stick.label;				\
	srl	scr2, 0, scr2;					\
/* CSTYLED */							\
	MIG_SCALE_STICK(out,scr2,scr1,MIG_FREQ_SHIFT)		\
	srlx	scr3, 32, scr3;					\
	sethi	%hi(native_stick_offset), scr1;			\
	ldx	[scr1 + %lo(native_stick_offset)], scr1;	\
	srl	scr1, 0, scr2;					\
	subcc	scr2, scr3, %g0;				\
/* CSTYLED */							\
	bnz,pn	%xcc, .rd_stick.label;				\
.rd_stick_normal.label:						\
	add	out, scr1, out

/*
 * These macros on sun4v read the %stick register, because :
 *
 * For sun4v platforms %tick can change dynamically *without* kernel
 * knowledge, due to SP side power & thermal management cases,
 * which is triggered externally by SP and handled by Hypervisor.
 *
 * The frequency of %tick cannot be relied upon by kernel code,
 * since it changes dynamically without the kernel being aware.
 * So, always use the constant-frequency %stick on sun4v.
 */
#define	RD_CLOCK_TICK(out, scr1, scr2, scr3, label)		\
/* CSTYLED */							\
	RD_STICK(out,scr1,scr2,scr3,label)

#define	RD_STICK_NO_SUSPEND_CHECK(out, scr1, scr2, label)	\
	sethi	%hi(native_stick_offset), scr1;			\
	ldx	[scr1 + %lo(mig_stick_normscale)], scr2;	\
	ldx	[scr1 + %lo(native_stick_offset)], scr1;	\
	rd	STICK, out;					\
	sllx	out, 1, out;					\
	srlx	out, 1, out;					\
	subcc	scr1, scr2, %g0;				\
/* CSTYLED */							\
	bz,pt	%xcc, .rd_stick_nosus.label;			\
	srl	scr2, 0, scr2;					\
/* CSTYLED */							\
	MIG_SCALE_STICK(out,scr2,scr1,MIG_FREQ_SHIFT)		\
	sethi	%hi(native_stick_offset), scr1;			\
	ldx	[scr1 + %lo(native_stick_offset)], scr1;	\
.rd_stick_nosus.label:						\
	add	out, scr1, out

#define	RD_CLOCK_TICK_NO_SUSPEND_CHECK(out, scr1, scr2, label)	\
/* CSTYLED */							\
	RD_STICK_NO_SUSPEND_CHECK(out,scr1,scr2,label)

#ifndef	_ASM
#ifdef	_KERNEL
extern u_longlong_t gettick(void);
#define	CLOCK_TICK_COUNTER()	gettick()	/* returns %stick */
#endif	/* _KERNEL */
#endif	/* _ASM */


#define	RD_TICK(out, scr1, scr2, label)				\
.rd_tick.label:							\
	sethi	%hi(native_tick_offset), scr1;			\
	ldx	[scr1 + %lo(native_tick_offset)], scr2;		\
	rd	%tick, out;					\
	ldx	[scr1 + %lo(native_tick_offset)], scr1;		\
	sub	scr1, scr2, scr2;				\
/* CSTYLED */							\
	brnz,pn	scr2, .rd_tick.label;				\
	sllx	out, 1, out;					\
	srlx	out, 1, out;					\
	add	out, scr1, out

#define	RD_TICK_NO_SUSPEND_CHECK(out, scr1)			\
	sethi	%hi(native_tick_offset), scr1;			\
	ldx	[scr1 + %lo(native_tick_offset)], scr1;		\
	rd	%tick, out;					\
	sllx	out, 1, out;					\
	srlx	out, 1, out;					\
	add	out, scr1, out

/*
 * Read the physical %stick register
 */
#define	RD_STICK_PHYSICAL(out)					\
	rd	%stick, out

/*
 * Read the raw stick value from %stick register
 * Need to mask out the NPT bit here to get just the stick value
 */
#define	RD_STICK_RAW(out)					\
	rd	%stick, out;					\
	sllx	out, 1, out;					\
	srlx	out, 1, out


/*
 * Read the %tick register without taking the native_tick_offset
 * into account. Required to be a single instruction, usable in a
 * delay slot.
 */
#define	RD_TICK_PHYSICAL(out)					\
	rd	%tick, out

/*
 * For traptrace, which requires either the %tick or %stick
 * counter depending on the value of a global variable.
 * If the kernel variable passed in as 'use_stick' is non-zero,
 * read the %stick counter into the 'out' register, otherwise,
 * read the %tick counter. Note the label-less branches.
 * For migration, no offset or freq normalization will be done
 * for %stick as they incur too much overhead which defeat the
 * purpose of finegrain trace timestamping. Users of traptrace
 * need to be aware of a jump in the timestamp (if %stick is
 * used) when a ldom migration happens. Note that the trace
 * buffers get recyled so quickly that one would almost never
 * see the change/jump due to migration.
 */
#define	RD_TICKSTICK_FLAG(out, scr1, use_stick)			\
	sethi	%hi(use_stick), scr1;				\
	lduw	[scr1 + %lo(use_stick)], scr1;			\
/* CSTYLED */							\
	brz,a	scr1, .+20;					\
	rd	%tick, out;					\
	mov	%g0, scr1;					\
	ba	.+16;						\
	rd	STICK, out;					\
	sethi	%hi(native_tick_offset), scr1;			\
	ldx	[scr1 + %lo(native_tick_offset)], scr1;		\
	sllx	out, 1, out;					\
	srlx	out, 1, out;					\
	add	out, scr1, out;

/*
 * Reading and writing of the stick compare register is
 * assumed to be raw or physical. Any normalization if needed
 * is done by the caller before calling these macros
 */
#define	RD_TICKCMPR(out)					\
	rd	STICK_COMPARE, out;

#define	WR_TICKCMPR(in)						\
	wr	in, STICK_COMPARE

#define	GET_NATIVE_TIME(out, scr1, scr2, scr3, label)		\
/* CSTYLED */							\
	RD_STICK(out,scr1,scr2,scr3,label)

/*
 * Sun4v processors come up with NPT cleared and there is no need to
 * clear it again. Also, clearing of the NPT cannot be done atomically
 * on a CMT processor.
 */
#define	CLEARTICKNPT

/*
 * Constants used to convert hi-res timestamps into nanoseconds
 * (see <sys/clock.h> file for more information)
 */

/*
 * At least 62.5 MHz, for faster %tick-based systems.
 */
#define	NSEC_SHIFT	4

/*
 * NOTE: the macros below assume that the various time-related variables
 * in boot_hrt are all stored together on a 64-byte boundary.  The primary
 * motivation is cache performance, but we also take advantage of a convenient
 * side effect: these variables all have the same high 22 address bits, so only
 * one sethi is needed to access them all.
 */

/*
 * GET_HRESTIME() returns the value of hrestime, hrestime_adj and the
 * number of nanoseconds since the last clock tick ('nslt').  It also
 * sets 'nano' to the value NANOSEC (one billion).
 *
 * This macro assumes that all registers are globals or outs so they can
 * safely contain 64-bit data, and that it's safe to use the label "5:".
 * Further, this macro calls the NATIVE_TIME_TO_NSEC_SCALE which in turn
 * uses the labels "6:" and "7:"; labels "5:", "6:" and "7:" must not
 * be used across invocations of this macro.
 */
#define	GET_HRESTIME(hrestsec, hrestnsec, adj, nslt, nano, scr, hrlock, \
    gnt1, gnt2, label) \
5:	sethi	%hi(boot_hrt), scr;					\
						/* load clock lock */	\
	lduw	[scr + %lo(boot_hrt + HRT_HRES_LOCK)], hrlock;		\
	andn	hrlock, 1, hrlock;  	/* see comments above! */	\
	ldx	[scr + %lo(boot_hrt + HRT_HRES_LAST_TICK)], nslt;	\
						/* load hrestime.sec */	\
	ldn	[scr + %lo(boot_hrt + HRT_HRESTIME)], hrestsec;		\
						/* load hrestime.nsec */\
	ldn	[scr + %lo(boot_hrt + HRT_HRESTIME_NSEC)], hrestnsec;	\
/* CSTYLED */ 								\
	GET_NATIVE_TIME(adj,gnt1,gnt2,nano,label); /* get %stick val */	\
	subcc	adj, nslt, nslt; /* nslt = ticks since last clockint */	\
	movneg	%xcc, %g0, nslt; /* ignore neg delta from tick skew */	\
						/* load hrestime_adj */	\
	ldx	[scr + %lo(boot_hrt + HRT_HRESTIME_ADJ)], adj;		\
	/* membar #LoadLoad; (see comment (2) above) */			\
						/* tick-to-ns factor */	\
	lduw	[scr + %lo(boot_hrt + HRT_NSEC_SCALE)], nano;		\
						/* load clock lock */	\
	lduw	[scr + %lo(boot_hrt + HRT_HRES_LOCK)], scr;		\
	NATIVE_TIME_TO_NSEC_SCALE(nslt, nano, gnt1, NSEC_SHIFT);	\
	sethi	%hi(NANOSEC), nano;					\
	xor	hrlock, scr, scr;					\
/* CSTYLED */ 								\
	brnz,pn	scr, 5b;						\
	or	nano, %lo(NANOSEC), nano;

/*
 * Similar to above, but returns current gethrtime() value in 'base'.
 */
#define	GET_HRTIME(base, now, nslt, scale, scr, hrlock, gnt1, gnt2, label) \
5:	sethi	%hi(boot_hrt), scr;					\
						/* load clock lock */	\
	lduw	[scr + %lo(boot_hrt + HRT_HRES_LOCK)], hrlock;		\
	andn	hrlock, 1, hrlock;  	/* see comments above! */	\
	ldx	[scr + %lo(boot_hrt + HRT_HRES_LAST_TICK)], nslt;	\
						/* load hrtime_base */	\
	ldx	[scr + %lo(boot_hrt + HRT_HRTIME_BASE)], base;		\
/* CSTYLED */ 								\
	GET_NATIVE_TIME(now,gnt1,gnt2,scale,label); /* get %stick */	\
	subcc	now, nslt, nslt; /* nslt = ticks since last clockint */	\
	movneg	%xcc, %g0, nslt; /* ignore neg delta from tick skew */	\
	/* membar #LoadLoad; (see comment (2) above) */			\
						/* tick-to-ns factor */	\
	lduw	[scr + %lo(boot_hrt + HRT_NSEC_SCALE)], scale;		\
						/* load clock lock */	\
	ld	[scr + %lo(boot_hrt + HRT_HRES_LOCK)], scr;		\
	NATIVE_TIME_TO_NSEC_SCALE(nslt, scale, gnt1, NSEC_SHIFT);	\
	xor	hrlock, scr, scr;					\
/* CSTYLED */ 								\
	brnz,pn	scr, 5b;						\
	add	base, nslt, base;

#ifdef	__cplusplus
}
#endif

#endif	/* !_SYS_MACHCLOCK_H */
