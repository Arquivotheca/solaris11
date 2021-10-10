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
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _ZONESTAT_H
#define	_ZONESTAT_H


#include <limits.h>
#include <sys/nvpair.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/param.h>
#include <sys/pset.h>
#include <sys/zone.h>

#ifdef __cplusplus
extern "C" {
#endif

#define	ZS_IPTYPE_SHARED	1
#define	ZS_IPTYPE_EXCLUSIVE	2

#define	ZS_CPUTYPE_DEFAULT_PSET	1
#define	ZS_CPUTYPE_POOL_PSET	2
#define	ZS_CPUTYPE_PSRSET_PSET	3
#define	ZS_CPUTYPE_DEDICATED	4

/*
 * Scheds are defined as flags so a list of them can be returned in a single
 * uint_t.
 */
#define	ZS_SCHED_TS			0x1
#define	ZS_SCHED_IA			0x2
#define	ZS_SCHED_RT			0x4
#define	ZS_SCHED_FX			0x8
#define	ZS_SCHED_FX_60			0x10
#define	ZS_SCHED_FSS			0x20
#define	ZS_SCHED_SDC			0x40
#define	ZS_SCHED_SYS			0x80
#define	ZS_SCHED_CONFLICT		0x100

#define	ZS_LIMIT_NONE			(UINT64_MAX)
#define	ZS_PCT_NONE			(UINT_MAX)
#define	ZS_SHARES_UNLIMITED		(UINT16_MAX)

#define	ZS_ZONENAME_MAX			ZONENAME_MAX
#define	ZS_PSETNAME_MAX			(1024 + 1)
#define	ZS_POOLNAME_MAX			(1024 + 1)

typedef enum zs_resource_type_enum {
    ZS_RESOURCE_TYPE_TIME = 1,
    ZS_RESOURCE_TYPE_COUNT,
    ZS_RESOURCE_TYPE_BYTES
} zs_resource_type_t;

typedef enum zs_limit_type_enum {
    ZS_LIMIT_TYPE_TIME = 1,
    ZS_LIMIT_TYPE_COUNT,
    ZS_LIMIT_TYPE_BYTES
} zs_limit_type_t;

typedef enum zs_resource_property_enum {
    ZS_RESOURCE_PROP_CPU_TOTAL = 1,
    ZS_RESOURCE_PROP_CPU_ONLINE,
    ZS_RESOURCE_PROP_CPU_LOAD_1MIN,
    ZS_RESOURCE_PROP_CPU_LOAD_5MIN,
    ZS_RESOURCE_PROP_CPU_LOAD_15MIN
} zs_resource_property_t;

typedef enum zs_resource_enum {
    ZS_RESOURCE_CPU = 1,
    ZS_RESOURCE_RAM_RSS,
    ZS_RESOURCE_RAM_LOCKED,
    ZS_RESOURCE_VM,
    ZS_RESOURCE_DISK_SWAP, /* Only supports ZS_USER_ALL and ZS_USER_FREE */
    ZS_RESOURCE_LWPS,
    ZS_RESOURCE_PROCESSES,
    ZS_RESOURCE_SHM_MEMORY,
    ZS_RESOURCE_SHM_IDS,
    ZS_RESOURCE_SEM_IDS,
    ZS_RESOURCE_MSG_IDS,
    ZS_RESOURCE_LOFI,
    ZS_RESOURCE_NETWORK,
    ZS_RESOURCE_NET_SPEED,
    ZS_RESOURCE_NET_ALL,
    ZS_RESOURCE_NET_IN,
    ZS_RESOURCE_NET_OUT,
    ZS_RESOURCE_NET_PHYS_ALL,
    ZS_RESOURCE_NET_PHYS_IN,
    ZS_RESOURCE_NET_PHYS_OUT
} zs_resource_t;

typedef enum zs_user_enum {
    ZS_USER_ALL = 1,
    ZS_USER_SYSTEM,
    ZS_USER_ZONES,
    ZS_USER_FREE
} zs_user_t;

typedef enum zs_limit_enum {
    ZS_LIMIT_CPU = 1,
    ZS_LIMIT_CPU_SHARES,
    ZS_LIMIT_RAM_RSS,
    ZS_LIMIT_RAM_LOCKED,
    ZS_LIMIT_VM,
    ZS_LIMIT_LWPS,
    ZS_LIMIT_PROCESSES,
    ZS_LIMIT_SHM_MEMORY,
    ZS_LIMIT_SHM_IDS,
    ZS_LIMIT_MSG_IDS,
    ZS_LIMIT_SEM_IDS,
    ZS_LIMIT_LOFI
} zs_limit_t;

typedef enum zs_zone_property_enum {
    ZS_ZONE_PROP_NAME = 1,
    ZS_ZONE_PROP_ID,
    ZS_ZONE_PROP_IPTYPE,
    ZS_ZONE_PROP_CPUTYPE,
    ZS_ZONE_PROP_DEFAULT_SCHED,
    ZS_ZONE_PROP_SCHEDULERS,
    ZS_ZONE_PROP_CPU_SHARES,
    ZS_ZONE_PROP_POOLNAME,
    ZS_ZONE_PROP_PSETNAME
} zs_zone_property_t;

typedef enum zs_pset_property_enum {
    ZS_PSET_PROP_NAME = 1,
    ZS_PSET_PROP_ID,
    ZS_PSET_PROP_CPUTYPE,
    ZS_PSET_PROP_SIZE,
    ZS_PSET_PROP_ONLINE,
    ZS_PSET_PROP_MIN,
    ZS_PSET_PROP_MAX,
    ZS_PSET_PROP_CPU_SHARES,
    ZS_PSET_PROP_SCHEDULERS,
    ZS_PSET_PROP_LOAD_1MIN,
    ZS_PSET_PROP_LOAD_5MIN,
    ZS_PSET_PROP_LOAD_15MIN
} zs_pset_property_t;

typedef enum zs_pz_property_enum {
    ZS_PZ_PROP_SCHEDULERS = 1,
    ZS_PZ_PROP_CPU_SHARES,
    ZS_PZ_PROP_CPU_CAP
} zs_pz_property_t;

typedef enum zs_pz_pct_enum {
    ZS_PZ_PCT_PSET = 1,
    ZS_PZ_PCT_CPU_CAP,
    ZS_PZ_PCT_PSET_SHARES,
    ZS_PZ_PCT_CPU_SHARES
} zs_pz_pct_t;

typedef enum zs_datalink_enum {
	ZS_LINK_PROP_NAME = 1,
	ZS_LINK_PROP_DEVNAME,
	ZS_LINK_PROP_ZONENAME,
	ZS_LINK_PROP_SPEED,
	ZS_LINK_PROP_STATE,
	ZS_LINK_PROP_CLASS,
	ZS_LINK_PROP_RBYTE,
	ZS_LINK_PROP_OBYTE,
	ZS_LINK_PROP_PRBYTES,
	ZS_LINK_PROP_POBYTES,
	ZS_LINK_PROP_TOT_BYTES,
	ZS_LINK_PROP_TOT_RBYTES,
	ZS_LINK_PROP_TOT_OBYTES,
	ZS_LINK_PROP_TOT_PRBYTES,
	ZS_LINK_PROP_TOT_POBYTES,
	ZS_LINK_PROP_MAXBW
} zs_datalink_property_t;

typedef enum zs_link_zone_enum {
	ZS_LINK_ZONE_PROP_NAME = 1,
	ZS_LINK_ZONE_PROP_BYTES,
	ZS_LINK_ZONE_PROP_RBYTES,
	ZS_LINK_ZONE_PROP_OBYTES,
	ZS_LINK_ZONE_PROP_PRBYTES,
	ZS_LINK_ZONE_PROP_POBYTES,
	ZS_LINK_ZONE_PROP_MAXBW,
	ZS_LINK_ZONE_PROP_PARTBW
} zs_lz_property_t;

/* Per-client handle to libzonestat */
typedef struct __zs_ctl *zs_ctl_t;

/*
 * These usage structure contains the system's utilization (overall, zones,
 * psets, memory) at a given point in time.
 */
typedef struct __zs_usage *zs_usage_t;

/*
 * The following structures desribe each zone, pset, and each zone's usage
 * of each pset.  Each usage structure (above) contains lists of these that
 * can be traversed.
 */
typedef struct __zs_zone *zs_zone_t;
typedef struct __zs_pset *zs_pset_t;
typedef struct __zs_pset_zone *zs_pset_zone_t;

/*
 * Opaque structure for properties.
 */
typedef struct __zs_property *zs_property_t;

/* functions for opening/closing a handle for reading current usage */
zs_ctl_t zs_open();
void zs_close(zs_ctl_t);

/* function for reading current resource usage */
zs_usage_t zs_usage_read(zs_ctl_t);

/* functions for manimulating usage data: zs_usage */
zs_usage_t zs_usage_diff(zs_usage_t, zs_usage_t);
void zs_usage_free(zs_usage_t);

/* functions for overall system resources: zs_resource */

boolean_t zs_resource_supported(zs_resource_t);
boolean_t zs_resource_user_supported(zs_resource_t, zs_user_t);
zs_resource_type_t zs_resource_type(zs_resource_t);

boolean_t zs_resource_property_supported(zs_resource_property_t);
zs_property_t zs_resource_property(zs_usage_t, zs_resource_property_t);

uint64_t zs_resource_total_uint64(zs_usage_t, zs_resource_t);
uint64_t zs_resource_used_uint64(zs_usage_t, zs_resource_t, zs_user_t);
uint64_t zs_resource_used_zone_uint64(zs_zone_t, zs_resource_t);
void zs_resource_total_time(zs_usage_t, zs_resource_t, timestruc_t *);
void zs_resource_used_time(zs_usage_t, zs_resource_t, zs_user_t,
    timestruc_t *);
void zs_resource_used_zone_time(zs_zone_t, zs_resource_t, timestruc_t *);
uint_t zs_resource_used_pct(zs_usage_t, zs_resource_t, zs_user_t);
uint_t zs_resource_used_zone_pct(zs_zone_t, zs_resource_t);

/* functions for individual zone usage: zs_zone */
int zs_zone_list(zs_usage_t, zs_zone_t *, int);
zs_zone_t zs_zone_walk(zs_usage_t, zs_zone_t);

boolean_t zs_zone_property_supported(zs_zone_property_t);
zs_property_t zs_zone_property(zs_zone_t, zs_zone_property_t);

boolean_t zs_zone_limit_supported(zs_limit_t);
zs_limit_type_t zs_zone_limit_type(zs_limit_t);
uint64_t zs_zone_limit_uint64(zs_zone_t, zs_limit_t);
uint64_t zs_zone_limit_used_uint64(zs_zone_t, zs_limit_t);
void zs_zone_limit_time(zs_zone_t, zs_limit_t, timestruc_t *);
void zs_zone_limit_used_time(zs_zone_t, zs_limit_t, timestruc_t *);
uint_t zs_zone_limit_used_pct(zs_zone_t, zs_limit_t);

/* functions for individual psets: zs_pset */
int zs_pset_list(zs_usage_t, zs_pset_t *, int);
zs_pset_t zs_pset_walk(zs_usage_t, zs_pset_t);
boolean_t zs_pset_property_supported(zs_pset_property_t);
boolean_t zs_pset_user_supported(zs_user_t);
zs_property_t zs_pset_property(zs_pset_t, zs_pset_property_t);
void zs_pset_total_time(zs_pset_t, timestruc_t *);
uint64_t zs_pset_total_cpus(zs_pset_t);
void zs_pset_used_time(zs_pset_t, zs_user_t, timestruc_t *);
uint64_t zs_pset_used_cpus(zs_pset_t, zs_user_t);
uint_t zs_pset_used_pct(zs_pset_t, zs_user_t);

/* functions for a pset's per-zone usage: zs_pset_zone */
int zs_pset_zone_list(zs_pset_t, zs_pset_zone_t *, int);
zs_pset_zone_t zs_pset_zone_walk(zs_pset_t, zs_pset_zone_t);
zs_zone_t zs_pset_zone_get_zone(zs_pset_zone_t);
zs_pset_t zs_pset_zone_get_pset(zs_pset_zone_t);
boolean_t zs_pset_zone_property_supported(zs_pz_property_t);
zs_property_t zs_pset_zone_property(zs_pset_zone_t, zs_pz_property_t);
void zs_pset_zone_used_time(zs_pset_zone_t, timestruc_t *);
uint64_t zs_pset_zone_used_cpus(zs_pset_zone_t);
boolean_t zs_pset_zone_pct_supported(zs_pz_pct_t);
uint_t zs_pset_zone_used_pct(zs_pset_zone_t, zs_pz_pct_t);

/* functions for accessing properties */
zs_property_t zs_property_alloc();
size_t zs_property_size();
void zs_property_free(zs_property_t);
data_type_t zs_property_type(zs_property_t);
char *zs_property_string(zs_property_t);
double zs_property_double(zs_property_t);
uint64_t zs_property_uint64(zs_property_t);
int64_t zs_property_int64(zs_property_t);
uint_t zs_property_uint(zs_property_t);
int zs_property_int(zs_property_t);

#ifdef __cplusplus
}
#endif

#endif	/* _ZONESTAT_H */
