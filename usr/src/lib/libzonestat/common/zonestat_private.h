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

#ifndef _ZONESTAT_PRIVATE_H
#define	_ZONESTAT_PRIVATE_H

#include <zonestat.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Private libzonestat interfaces used by the zonestat command.
 *
 * INTERFACES DEFINED IN THIS FILE DO NOT CONSTITUTE A PUBLIC INTERFACE.
 *
 * Do not consume these interfaces; your program will break in the future
 * (even in a patch) if you do.
 */

/*
 * The usage set is for computations on multiple usage structures to describe
 * a range of time.
 */

typedef enum zs_compute_enum {
    ZS_COMPUTE_USAGE_INTERVAL = 1,
    ZS_COMPUTE_USAGE_TOTAL,
    ZS_COMPUTE_USAGE_AVERAGE,
    ZS_COMPUTE_USAGE_HIGH
} zs_compute_t;

typedef enum zs_compute_set_enum {
    ZS_COMPUTE_SET_TOTAL = 1,
    ZS_COMPUTE_SET_AVERAGE,
    ZS_COMPUTE_SET_HIGH
} zs_compute_set_t;

typedef struct zs_usage_set *zs_usage_set_t;
typedef struct zs_datalink *zs_datalink_t;
typedef struct zs_link_zone *zs_link_zone_t;

zs_usage_t zs_usage_compute(zs_usage_t, zs_usage_t, zs_usage_t, zs_compute_t);

/* functions for manipulating sets of usage data: zs_usage_set */
zs_usage_set_t zs_usage_set_alloc();
void zs_usage_set_free(zs_usage_set_t);
int zs_usage_set_add(zs_usage_set_t, zs_usage_t);
int zs_usage_set_count(zs_usage_set_t);
zs_usage_t zs_usage_set_compute(zs_usage_set_t, zs_compute_set_t);

/* functions for a datalink's per-link usage: zs_datalink */
int zs_datalink_list(zs_usage_t, zs_datalink_t *, int);
zs_property_t zs_link_property(zs_datalink_t, zs_datalink_property_t);
zs_property_t zs_link_zone_property(zs_link_zone_t, zs_lz_property_t);



#ifdef __cplusplus
}
#endif

#endif	/* _ZONESTAT_PRIVATE_H */
