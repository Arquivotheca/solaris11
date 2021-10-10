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

#ifndef _SYS_SUN4V_PCBE_H
#define	_SYS_SUN4V_PCBE_H

/*
 * sun4v Performance Counter Back-End header file
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _pcbe_config {
	uint_t		pcbe_picno;	/* pic id number */
	uint32_t	pcbe_evsel;	/* %pcr event code unshifted */
	uint32_t	pcbe_flags;	/* hpriv/user/system/priv */
	uint32_t	pcbe_pic;	/* unshifted raw %pic value */
} pcbe_config_t;

typedef struct _pcbe_event {
	const char	*name;
	const uint32_t	emask;
	const uint32_t	emask_valid;	/* Mask of unreserved MASK bits */
} pcbe_event_t;

typedef struct _pcbe_generic_event {
	char *name;
	char *event;
} pcbe_generic_event_t;

#define	PCBE_EV_END {NULL, 0, 0}
#define	PCBE_GEN_EV_END {NULL, NULL}

typedef struct _cpu_pcbe_data {
	char	*cpu_ref_url;
	char	*cpu_impl_name;
	char	*cpu_pcbe_ref;
	uint64_t	allstop;
	uint_t	ncounters;
	char	*pcbe_list_attrs;
	char	*pcbe_list_attrs_nohv;
} cpu_pcbe_data_t;

extern char		*pcbe_evlist;
extern size_t		pcbe_evlist_sz;

extern pcbe_event_t		*pcbe_eventsp;
extern pcbe_generic_event_t	*pcbe_generic_eventsp;

extern int hv_getperf_func_num;
extern int hv_setperf_func_num;

extern uint64_t hv_setperf(uint64_t regnum, uint64_t val);
extern uint64_t hv_getperf(uint64_t regnum, uint64_t *val);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_SUN4V_PCBE_H */
