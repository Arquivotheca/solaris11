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

#include "lint.h"
#include "thr_uberdata.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <atomic.h>
#include <sys/clock.h>
#include <sys/time.h>
#include <sys/hrt.h>

extern void *gethrt(void);

/*
 * Pointer to data to compute hrtime from TSCP.
 */
volatile hrt_t	*hrt = NULL;

/*
 * Get a pointer to high resolution time data via gethrt() fast trap.
 * This is called the first time a process calls either gethrtime(3C) or
 * gettimeofday(3C).
 */
volatile hrt_t *
map_hrt(void)
{
	hrt_t *new;

	new = (hrt_t *)gethrt();
	if (new == NULL)
		return (NULL);

	/*
	 * hrt must be atomically updated to prevent another thread from
	 * reading a partial update.
	 */
	atomic_swap_ptr((volatile void *)&hrt, (void *)new);

	return (hrt);
}
