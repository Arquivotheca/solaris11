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
 * Copyright (c) 2004, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_ZONE_H
#define	_ZONE_H

#include <mdb/mdb_modapi.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	ZONE_WALK_ACTIVE	(void *)(uintptr_t)0
#define	ZONE_WALK_DEATHROW	(void *)(uintptr_t)1

extern int zoneprt(uintptr_t, uint_t, int argc, const mdb_arg_t *);

extern int zone_walk_init(mdb_walk_state_t *);
extern int zone_walk_step(mdb_walk_state_t *);

extern int zsd_walk_init(mdb_walk_state_t *);
extern int zsd_walk_step(mdb_walk_state_t *);

extern int zsd(uintptr_t, uint_t, int, const mdb_arg_t *);

#ifdef	__cplusplus
}
#endif

#endif	/* _ZONE_H */
