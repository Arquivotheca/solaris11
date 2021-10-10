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
 * Copyright (c) 1998, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _MTMALLOC_H
#define	_MTMALLOC_H


/*
 * Public interface for multi-threadead malloc user land library
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>

/* commands for mallocctl(int cmd, long value) */

#define	MTDOUBLEFREE	1	/* core dumps on double free */
#define	MTDEBUGPATTERN	2	/* write misaligned data after free. */
#define	MTINITBUFFER	4	/* write misaligned data at allocation */
#define	MTEXCLUSIVE	5	/* Use exclusive BINs for low numbered thrs */
#define	MTREALFREE	6	/* Use madvise to free large allocations */
#define	MTCHUNKSIZE	32	/* How much to alloc when backfilling caches. */

void mallocctl(int, long);

#ifdef __cplusplus
}
#endif

#endif /* _MTMALLOC_H */
