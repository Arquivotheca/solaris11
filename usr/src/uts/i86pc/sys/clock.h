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
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _SYS_CLOCK_H
#define	_SYS_CLOCK_H

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef	_KERNEL

#ifndef	_ASM

#include <sys/psw.h>
#include <sys/time.h>
#include <sys/processor.h>
#if defined(__GNUC__) && defined(_ASM_INLINES)
#include <asm/clock.h>
#endif
#include <sys/machclock.h>

extern time_t ggmtl(void);
extern void sgmtl(time_t);
extern void rtcsync(void);

extern void hres_tick(void);
extern void (*hrtime_tick)(void);

#ifndef __xpv
extern void tsc_hrtimeinit(uint64_t cpu_freq_hz);
extern void tsc_sync_master(processorid_t);
extern void tsc_sync_slave(void);
#endif

/*
 * Careful: this can always return zero on some systems.  Use the system hrtime
 * routines if you want a meaningful time.
 */
extern hrtime_t tsc_read(void);

extern hrtime_t __rdtsc_insn(void);

extern int tsc_gethrtime_enable;
extern int tscp_enable;

#define	ADJ_SHIFT	4		/* used in get_hrestime */
#define	NSEC_SHIFT	5
/* Hack value just to allow clock to be kicked */
#define	NSEC_PER_CLOCK_TICK 	NANOSEC / 100

#define	YRBASE		00	/* 1900 - what year 0 in chip represents */

#endif	/* !_ASM */

#define	CBE_HIGH_PIL	14
#define	CBE_LOCK_PIL	LOCK_LEVEL
#define	CBE_LOW_PIL	2

/*
 * CLOCK_LOCK() sets the LSB (byte 0) of the hrt->hres_lock. The rest of the
 * 24bits are used as the counter. This lock is acquired
 * around hrt members "hrestime" and "timedelta". This lock is acquired to
 * make sure that level-14 accounts for changes to this variable in that
 * interrupt itself. The level-14 interrupt code also acquires this
 * lock.
 * (Note: It is assumed that the lock_set_spl() uses only byte 0 of the lock.)
 *
 * CLOCK_UNLOCK() increments the lower bytes straight, thus clearing the
 * lock and also incrementing the counter. This way gethrtime()
 * can figure out if the value in the lock got changed or not.
 */
#define	HRES_LOCK_OFFSET 0	/* byte 0 has the lock bit(bit 0 in the byte) */

#define	unlock_hres_lock()	atomic_inc_32(&hrt->hres_lock);

#define	CLOCK_LOCK(oldsplp)	\
	lock_set_spl((lock_t *)&hrt->hres_lock + HRES_LOCK_OFFSET, 	\
		ipltospl(XC_HI_PIL), oldsplp)

#define	CLOCK_UNLOCK(spl)		\
	unlock_hres_lock();		\
	splx(spl);			\
	LOCKSTAT_RECORD0(LS_CLOCK_UNLOCK_RELEASE,	\
		(lock_t *)&hrt->hres_lock + HRES_LOCK_OFFSET);

/*
 * You want CLOCK_LOCK and CLOCK_UNLOCK above.
 * These versions of the above macros are for use only by the clock interrupt
 * which is already at CY_LOCK_LEVEL.
 */
#define	CLOCK_SPIN_LOCK()	hres_spin_lock()
#define	CLOCK_SPIN_UNLOCK()	hres_spin_unlock()

#endif	/* KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_CLOCK_H */
