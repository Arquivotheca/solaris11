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
 * Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_SYS_CLOCK_TICK_H
#define	_SYS_CLOCK_TICK_H

#include <sys/types.h>
#include <sys/mutex.h>
#include <sys/cpuvar.h>
#include <sys/cpupart.h>
#include <sys/systm.h>
#include <sys/cyclic.h>
#include <sys/sysmacros.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	CLOCK_TICK_NCPUS	64

/*
 * Per-CPU structure to facilitate multi-threaded tick accounting.
 *
 * ct_lock
 *	Mutex for the structure. Used to lock the structure to pass
 *	arguments to the tick processing softint handler.
 * ct_lbolt
 *	Copy of the lbolt at the time of tick scheduling.
 * ct_pending
 *	Number of ticks to be processed by one invocation of the tick
 *	processing softint.
 * ct_start
 *	First CPU to do tick processing for.
 * ct_end
 *	Last CPU to do tick processing for.
 * ct_scan
 *	Direction in which the CPUs are scanned.
 * ct_set
 *	Set number for an invocation.
 */
typedef struct clock_tick_cpu {
	kmutex_t		ct_lock;
	clock_t			ct_lbolt;
	int			ct_pending;
	int			ct_start;
	int			ct_end;
	int			ct_scan;
	int			ct_set;
} clock_tick_cpu_t;

#define	CLOCK_TICK_CPU_PAD	\
	P2NPHASE(sizeof (clock_tick_cpu_t), CPU_CACHE_COHERENCE_SIZE)

typedef struct clock_tick_cpu_pad {
	clock_tick_cpu_t	ct_data;
	char			ct_pad[CLOCK_TICK_CPU_PAD];
} clock_tick_cpu_pad_t;

/*
 * Per-set structure to facilitate multi-threaded tick accounting.
 * clock_tick_lock protects this.
 *
 * ct_start
 *	First CPU to do tick processing for.
 * ct_end
 *	Last CPU to do tick processing for.
 * ct_scan
 *	CPU to start the tick processing from. Rotated every tick.
 * ct_active
 *	Handler for this set is active.
 * ct_bitset
 *	Bitmap of all the CPUs in this set - indexed by cpu_seqid.
 */
typedef struct clock_tick_set {
	int			ct_start;
	int			ct_end;
	int			ct_scan;
	ulong_t			ct_active;
	bitset_t		ct_bitset;
} clock_tick_set_t;

#define	CLOCK_TICK_SET_PAD	\
	P2NPHASE(sizeof (clock_tick_set_t), CPU_CACHE_COHERENCE_SIZE)

typedef struct clock_tick_set_pad {
	clock_tick_set_t	cs_data;
	char			cs_pad[CLOCK_TICK_SET_PAD];
} clock_tick_set_pad_t;

#define	CLOCK_TICK_CPU_OFFLINE(cp)	\
	(((cp) != cpu_active) && ((cp)->cpu_next_onln == (cp)))

#define	CLOCK_TICK_XCALL_SAFE(cp)	\
		CPU_IN_SET(clock_tick_online_cpuset, cp->cpu_id)

/*
 * PERFORMANCE
 *	The implementation will optimize for performance.
 * POWER
 *	The implementation will optimize for power savings.
 */
#define	CLOCK_TICK_POLICY_PERFORMANCE	0
#define	CLOCK_TICK_POLICY_POWER		1
#define	CLOCK_TICK_MAX_POLICIES		2

#ifdef	_KERNEL
extern ulong_t		create_softint(uint_t, uint_t (*)(caddr_t, caddr_t),
				caddr_t, char *);
extern void		invoke_softint(processorid_t, ulong_t);
extern void		sync_softint(processorid_t);
extern void		clock_tick(int, kthread_t *, int);
extern void		membar_sync(void);
extern void		clock_tick_set_policy(int);
extern void		*clock_tick_alloc(size_t, size_t *);
extern void		clock_tick_free(void *, size_t, size_t);

extern int		hires_tick;
extern int		clock_tick_policy;
extern int		clock_tick_max_sets;
extern int		clock_tick_highest_set;

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_CLOCK_TICK_H */
