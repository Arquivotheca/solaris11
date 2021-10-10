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
 * Copyright (c) 1998, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#if defined(_KERNEL)
#include <sys/time.h>
#include <sys/types.h>
#include <sys/machparam.h>
#endif	/* _KERNEL */
#include <sys/hrt.h>
#include <sys/atomic.h>

/*
 * Common implementation for kernel gethrtime(9F), gettimeofday(9F)
 * and TSCP hwcap libc gethrtime(3C) and gettimeofday(3C).
 *
 * General High Resolution Time algorithm comments are in uts/sun4/sys/clock.h.
 */

extern hrt_t *hrt;

#if !defined(_KERNEL)

extern volatile hrt_t *map_hrt(void);

static volatile boolean_t map_hrt_failed = B_FALSE;

#endif	/* !_KERNEL */

/*
 * This is common code which implements both the i86pc kernel gethrtime(9F)
 * and libc hwcap gethrtime(3C).
 * The kernel gets to tscp_gethrtime_delta via the gethrtimef pointer.
 * Libc uses this implementation for the appropriate hwcap gethrtime(3C).
 */
#if defined(_KERNEL)
hrtime_t
tscp_gethrtime_delta(void)
#else
hrtime_t
gethrtime(void)
#endif
{
	uint32_t cpu_id;
	uint32_t old_hres_lock;
	uint64_t tsc;
	hrtime_t now;

#if !defined(_KERNEL)
	extern hrtime_t _gethrtime(void);

	if (hrt == NULL) {
		if (map_hrt_failed) {
			return (_gethrtime());	/* non-HWCAP version */
		} else if (map_hrt() == NULL) {
			map_hrt_failed = B_TRUE;
			return (_gethrtime());	/* non-HWCAP version */
		}
	}
#endif	/* !_KERNEL */

	do {
		old_hres_lock = hrt->hres_lock;

		/*
		 * The RDTSCP instruction is a memory barrier.
		 * RDTSCP atomically reads the TSC value and the CPUID of
		 * the CPU where tsc was read.
		 */
		tsc = rdtscp(&cpu_id);
		tsc += hrt->tsc_sync_tick_delta[cpu_id];

		if (tsc >= hrt->tsc_last) {
			tsc -= hrt->tsc_last;
		} else if (tsc >= hrt->tsc_last - (2 * hrt->tsc_max_delta)) {
			tsc = 0;
		}

		now = hrt->tsc_hrtime_base;
		TSC_CONVERT_AND_ADD(tsc, now, hrt->nsec_scale, hrt->nsec_shift);
	} while ((old_hres_lock & ~1) != hrt->hres_lock);

	return (now);
}

/*
 * This is common code which implements both the i86pc kernel
 * gettimeofday(9F) and libc hwcap gettimeofday(3C).
 * The kernel gets to tscp_gethrtime_delta via the gethrestimef pointer.
 * Libc uses this implementation for appropriate hwcap gettimeofday(3C).
 */
#if defined(_KERNEL)
void
tscp_gethrestime(timestruc_t *tp)
#else
int
gettimeofday(struct timeval *tp, void *tzp)
#endif	/* _KERNEL */
{
	timestruc_t	ts;
	uint64_t	tsc;		/* current tsc value */
	uint64_t	now;		/* current hrtime */
	int		nslt;		/* nsec since last tick */
	uint32_t	cpu_id;
	int		lock_prev;
	int		adj;		/* amount of adjustment to apply */

#if !defined(_KERNEL)
	extern int _gettimeofday(struct timeval *tp, void *tzp);

	if (hrt == NULL) {
		if (map_hrt_failed) {
			return (_gettimeofday(tp, tzp));	/* non-HWCAP */
		} else if (map_hrt() == NULL) {
			map_hrt_failed = B_TRUE;
			return (_gettimeofday(tp, tzp));	/* non-HWCAP */
		}
	}

	if (tp == NULL)
		return (0);		/* per the spec */
#endif	/* !_KERNEL */

	do {
		/*
		 * first loop is gethrtime() inline:
		 */
		do {
			lock_prev = hrt->hres_lock;

			ts.tv_sec = (time_t)hrt->hrestime.tv_sec;
			ts.tv_nsec = (long)hrt->hrestime.tv_nsec;

			tsc = rdtscp(&cpu_id);
			tsc += hrt->tsc_sync_tick_delta[cpu_id];
			if (tsc >= hrt->tsc_last)
				tsc -= hrt->tsc_last;
			else if (tsc >=
			    hrt->tsc_last - (2 * hrt->tsc_max_delta))
				tsc = 0;
			now = hrt->tsc_hrtime_base;
			TSC_CONVERT_AND_ADD(tsc, now, hrt->nsec_scale,
			    hrt->nsec_shift);

			nslt = (int)(now - hrt->hres_last_tick);

			/*
			 * nslt < 0 means a tick came between computing
			 * gethrtime() and hrt->hres_last_tick.
			 * Restart the loop.
			 */
		} while (nslt < 0);

		ts.tv_nsec += nslt;
		if (hrt->hrestime_adj != 0) {
			if (hrt->hrestime_adj > 0) {
				adj = (nslt >> hrt->adj_shift);
				if (adj > hrt->hrestime_adj)
					adj = (int)hrt->hrestime_adj;
			} else {
				adj = -(nslt >> hrt->adj_shift);
				if (adj < hrt->hrestime_adj)
					adj = (int)hrt->hrestime_adj;
			}
			ts.tv_nsec += adj;
		}
		while ((unsigned long)ts.tv_nsec >= NANOSEC) {
			ts.tv_nsec -= NANOSEC;
			ts.tv_sec++;
		}
	} while ((lock_prev & ~1) != hrt->hres_lock);

	tp->tv_sec = ts.tv_sec;

#if defined(_KERNEL)
	tp->tv_nsec = ts.tv_nsec;
#else
	/*
	 * timestruct_t uses nano seconds; timval uses micro seconds.
	 * See Hacker's Delight pg 162 for this divide by 1000 algorithm.
	 */
	{
		/*
		 * tp->tv_usec = ts.tv_nsec / 1000;
		 */
		uint64_t l = (uint64_t)ts.tv_nsec * 0x10624dd3;
		l >>= 38;
		tp->tv_usec = l;
	}
	return (0);
#endif
}
