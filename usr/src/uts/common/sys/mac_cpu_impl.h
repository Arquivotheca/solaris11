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

#ifndef	_MAC_CPU_IMPL_H
#define	_MAC_CPU_IMPL_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/mac_client_impl.h>

typedef enum {
	MAC_CPU_SQUEUE_ADD = 0,
	MAC_CPU_SQUEUE_REMOVE,
	MAC_CPU_INTR,
	MAC_CPU_RING_REMOVE
} mac_cpu_state_t;

extern void	mac_cpu_setup(mac_client_impl_t *);
extern void	mac_cpu_modify(mac_client_impl_t *, mac_cpu_state_t, void *);
extern void	mac_cpu_pool_setup(mac_client_impl_t *);
extern void	mac_cpu_teardown(mac_client_impl_t *);
extern void	mac_cpu_set_effective(mac_client_impl_t *);
extern void	mac_cpu_group_init(mac_client_impl_t *);
extern void	mac_cpu_group_fini(mac_client_impl_t *);

#ifdef	__cplusplus
}
#endif

#endif	/* _MAC_CPU_IMPL_H */
