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

#ifndef _HRT_H
#define	_HRT_H

#ifdef	__cplusplus
extern "C" {
#endif

#ifndef _ASM

#include <sys/types.h>
#include <sys/time.h>
#if defined(__GNUC__)
#include <sys/hrt_inlines.h>
#endif	/* __GNUC__ */

/*
 * Definitions for data used by kernel and libc to generate hrestime.
 * This also defines the libc private interface for mapping the kernel page
 * which contains this data.  This allows libc's gethrtime(3C) and
 * gettimeofday(3C) to execute entirely in userland on supporting hardware.
 * Structures must have same memory layout in 32-bit and 64-bit processes.
 */

/*
 * Timestamp conversion details are commented comment in uts/sun4/clock.h
 * above NATIVE_TIME_TO_NSEC_SCALE.
 */
#define	TSC_CONVERT_AND_ADD(tsc, hrt, scale, nsec_shift) {		\
	unsigned int *_l = (unsigned int *)&(tsc);			\
	(hrt) += (_l[1] * ((uint64_t)scale)) << (nsec_shift);		\
	(hrt) += (_l[0] * ((uint64_t)scale)) >> (32 - (nsec_shift));	\
}

typedef struct hrestime64 {
	int64_t		tv_sec;		/* seconds */
	int64_t		tv_nsec;	/* and nanoseconds */
} hrestime_t;

typedef struct hrt {
	volatile uint32_t	hres_lock;
	uint32_t		pad1;
	volatile hrestime_t	hrestime;

	volatile int64_t	hrestime_adj;
	volatile hrtime_t	hres_last_tick;
	volatile int64_t	timedelta;
	volatile hrtime_t	hrtime_base;

	volatile uint_t		adj_shift;
	uint_t			nsec_scale;
	uint_t			nsec_shift;
	uint_t			nsec_unscale;

	volatile hrtime_t	tsc_max_delta;
	volatile hrtime_t	tsc_last;
	volatile hrtime_t	tsc_hrtime_base;

	/*
	 * NCPU is purposefully hidden from libc and the non-machine-specific
	 * kernel.
	 */
#if defined(_KERNEL) && defined(_SYS_MACHPARAM_H)
	volatile hrtime_t	tsc_sync_tick_delta[NCPU];
#else
	volatile hrtime_t	tsc_sync_tick_delta[1];
#endif
} hrt_t;

#if defined(_KERNEL)

extern hrtime_t tscp_gethrtime_delta(void);
extern void tscp_gethrestime(timestruc_t *tp);
extern void hrt_init(void);
extern caddr_t hrt_map(void);
extern void plat_boot_hrt_switch(hrt_t *hp);

extern hrt_t *hrt;
extern int uhrt_enable;

#endif	/* _KERNEL */

extern uint64_t rdtscp(uint32_t *);

#endif	/* !_ASM */

#ifdef	__cplusplus
}
#endif

#endif	/* _HRT_H */
