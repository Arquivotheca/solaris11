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
 * Copyright (c) 2011, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _SYS_TICK_AC_H
#define	_SYS_TICK_AC_H

#include <sys/cpuvar.h>
#include <sys/sysmacros.h>

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef _KERNEL

typedef struct proc_ac_data {
	uint64_t	pd_cpu_time;	/* process CPU time */
	uint64_t	pd_threshold;	/* process CPU time threshold */
	uint64_t	pd_rss;		/* accumulated RSS */
	size_t		pd_rss_max;	/* peak RSS */
	ulong_t		pd_rss_samples;	/* number of RSS samples */
} proc_ac_data_t;

#define	PROC_AC_PAD	\
	P2NPHASE(sizeof (proc_ac_data_t), CPU_CACHE_COHERENCE_SIZE)

typedef struct proc_ac {
	proc_ac_data_t		pac_data;
	char			pac_pad[PROC_AC_PAD];
} proc_ac_t;

#define	pac_cpu_time		pac_data.pd_cpu_time
#define	pac_threshold		pac_data.pd_threshold
#define	pac_rss			pac_data.pd_rss
#define	pac_rss_max		pac_data.pd_rss_max
#define	pac_rss_samples		pac_data.pd_rss_samples

typedef struct task_cpu_time_data {
	uint64_t	td_cpu_time;	/* task CPU time */
	uint64_t	td_threshold;	/* task CPU time threshold */
} task_cpu_time_data_t;

#define	TASK_CPU_TIME_PAD	\
	P2NPHASE(sizeof (task_cpu_time_data_t), CPU_CACHE_COHERENCE_SIZE)

typedef struct task_cpu_time {
	task_cpu_time_data_t	tc_data;
	char			tc_pad[TASK_CPU_TIME_PAD];
} task_cpu_time_t;

#define	tc_cpu_time	tc_data.td_cpu_time
#define	tc_threshold	tc_data.td_threshold

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_TICK_AC_H */
