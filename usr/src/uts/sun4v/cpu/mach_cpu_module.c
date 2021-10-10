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
 * Copyright (c) 2005, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/cpu_module.h>
#include <vm/page.h>
#include <vm/seg_map.h>

/*ARGSUSED*/
void
cpu_fiximp(struct cpu_node *cpunode)
{}

/*ARGSUSED*/
void
cpu_map_exec_units(struct cpu *cp)
{}

void
cpu_flush_ecache(void)
{}

/*ARGSUSED*/
void
cpu_faulted_enter(struct cpu *cp)
{}

/*ARGSUSED*/
void
cpu_faulted_exit(struct cpu *cp)
{}

/*
 * Ecache scrub operations
 */
void
cpu_init_cache_scrub(void)
{}

void
kdi_flush_caches(void)
{}

/*ARGSUSED*/
int
kzero(void *addr, size_t count)
{ return (0); }

/*ARGSUSED*/
void
uzero(void *addr, size_t count)
{}

/*ARGSUSED*/
void
bzero(void *addr, size_t count)
{}

/*ARGSUSED*/
void
cpu_inv_tsb(caddr_t tsb_base, uint_t tsb_bytes)
{}

/* ARGSUSED */
uint64_t
migration_tickscale(uint64_t tick, uint64_t scale)
{ return (0); }

uint64_t
getstick_raw(void)
{ return (0); }
