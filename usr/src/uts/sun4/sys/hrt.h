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

/*
 * Definitions for data used by kernel and libc to generate hrestime.
 * This also defines the libc private interface for mapping the kernel page
 * which contains this data.  This allows libc's gethrtime(3C) and
 * gettimeofday(3C) to execute in userland.  Structures must have same
 * memory layout in 32-bit and 64-bit.
 */

typedef struct hrestime64 {
	int64_t		tv_sec;
	int64_t		tv_nsec;
} hrestime_t;

typedef struct hrt {
	volatile int64_t	timedelta;
	volatile hrtime_t	hres_last_tick;
	volatile hrestime_t	hrestime;
	volatile int64_t	hrestime_adj;
	volatile int		hres_lock;
	uint_t			nsec_scale;
	volatile hrtime_t	hrtime_base;

	/*
	 * -- WARNING --
	 * Initialization of boot_hrt in common_asm.s must be updated
	 * in synch with any changes here in nsec_shift or adj_shift sizes
	 * or relative offsets.
	 */
	uint_t			nsec_shift;
	uint_t			adj_shift;

	uint_t			nsec_unscale;
} hrt_t;

#if defined(_KERNEL)

extern hrt_t *hrt;
extern int uhrt_enable;

#endif  /* _KERNEL */
#endif	/* !_ASM */

#define	HRT_HRESTIME_NSEC	(HRT_HRESTIME + 8)

#ifdef	__cplusplus
}
#endif

#endif	/* _HRT_H */
