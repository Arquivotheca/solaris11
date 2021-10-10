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

#ifndef _ZONESTAT_IMPL_H
#define	_ZONESTAT_IMPL_H

#include <zonestat.h>
#include <sys/list.h>
#include <sys/priv_const.h>
#include <paths.h>
#include <libdllink.h>
#include <net/if.h>

#ifdef __cplusplus
extern "C" {

#endif

/*
 * INTERFACES DEFINED IN THIS FILE DO NOT CONSTITUTE A PUBLIC INTERFACE.
 *
 * Do not consume these interfaces; your program will break in the future
 * (even in a patch) if you do.
 */

/*
 * This file defines the private interface used between zonestatd and
 * libzonestat.
 */
#define	ZS_VERSION	1

#define	ZS_PSET_DEFAULT		PS_NONE
#define	ZS_PSET_MULTI		PS_MYID
#define	ZS_PSET_ERROR		PS_QUERY

#define	ZS_DOOR_PATH		_PATH_SYSVOL "/zonestat_door"

#define	ZSD_CMD_READ		1
#define	ZSD_CMD_CONNECT		2
#define	ZSD_CMD_NEW_ZONE	3

/* The following read commands are unimplemented */
#define	ZSD_CMD_READ_TIME	3
#define	ZSD_CMD_READ_SET	4
#define	ZSD_CMD_READ_SET_TIME	5

#define	ZSD_STATUS_OK			0
#define	ZSD_STATUS_VERSION_MISMATCH	1
#define	ZSD_STATUS_PERMISSION		2
#define	ZSD_STATUS_INTERNAL_ERROR	3

#define	TIMESTRUC_ADD_NANOSEC(ts, nsec)				\
	{							\
		(ts).tv_sec += (time_t)((nsec) / NANOSEC);	\
		(ts).tv_nsec += (long)((nsec) % NANOSEC);	\
		if ((ts).tv_nsec > NANOSEC) {			\
			(ts).tv_sec += (ts).tv_nsec / NANOSEC;	\
			(ts).tv_nsec = (ts).tv_nsec % NANOSEC;	\
		}						\
	}

#define	TIMESTRUC_ADD_TIMESTRUC(ts, add)			\
	{							\
		(ts).tv_sec += (add).tv_sec;			\
		(ts).tv_nsec += (add).tv_nsec;			\
		if ((ts).tv_nsec > NANOSEC) {			\
			(ts).tv_sec += (ts).tv_nsec / NANOSEC;	\
			(ts).tv_nsec = (ts).tv_nsec % NANOSEC;	\
		}						\
	}

#define	TIMESTRUC_DELTA(delta, new, old)			\
	{							\
		(delta).tv_sec = (new).tv_sec - (old).tv_sec;	\
		(delta).tv_nsec = (new).tv_nsec - (old).tv_nsec;\
		if ((delta).tv_nsec < 0) {			\
			delta.tv_nsec += NANOSEC;		\
			delta.tv_sec -= 1;			\
		}						\
		if ((delta).tv_sec < 0) {			\
			delta.tv_sec = 0;			\
			delta.tv_nsec = 0;			\
		}						\
	}

struct zs_property {
	data_type_t zsp_type;
	union zsp_value_union {
		char *zsv_string;
		double zsv_double;
		uint64_t zsv_uint64;
		int64_t zsv_int64;
		uint_t zsv_uint;
		int zsv_int;
	} zsp_v;
};

struct zs_system {

	uint64_t zss_ram_total;
	uint64_t zss_ram_kern;
	uint64_t zss_ram_zones;

	uint64_t zss_locked_kern;
	uint64_t zss_locked_zones;

	uint64_t zss_vm_total;
	uint64_t zss_vm_kern;
	uint64_t zss_vm_zones;

	uint64_t zss_swap_total;
	uint64_t zss_swap_used;

	timestruc_t zss_cpu_total_time;
	timestruc_t zss_cpu_usage_kern;
	timestruc_t zss_cpu_usage_zones;

	uint64_t zss_processes_max;
	uint64_t zss_lwps_max;
	uint64_t zss_shm_max;
	uint64_t zss_shmids_max;
	uint64_t zss_semids_max;
	uint64_t zss_msgids_max;
	uint64_t zss_lofi_max;

	uint64_t zss_processes;
	uint64_t zss_lwps;
	uint64_t zss_shm;
	uint64_t zss_shmids;
	uint64_t zss_semids;
	uint64_t zss_msgids;
	uint64_t zss_lofi;

	uint64_t zss_ncpus;
	uint64_t zss_ncpus_online;
	double	 zss_load_avg[3];

	uint64_t zss_net_speed;
	uint64_t zss_net_bytes;
	uint64_t zss_net_rbytes;
	uint64_t zss_net_obytes;
	uint64_t zss_net_pbytes;
	uint64_t zss_net_prbytes;
	uint64_t zss_net_pobytes;
	uint_t zss_net_pused;

	/* The following provide space for the given properties */

	/* ZS_RESOURCE_PROP_CPU_TOTAL */
	struct zs_property zss_prop_cpu_total;

	/* ZS_RESOURCE_PROP_CPU_ONLINE */
	struct zs_property zss_prop_cpu_online;

	/* ZS_RESOURCE_PROP_CPU_LOAD_1MIN */
	struct zs_property zss_prop_cpu_1min;

	/* ZS_RESOURCE_PROP_CPU_LOAD_5MIN */
	struct zs_property zss_prop_cpu_5min;

	/* ZS_RESOURCE_PROP_CPU_LOAD_15MIN */
	struct zs_property zss_prop_cpu_15min;
};

struct zs_pset;
struct zs_zone;

struct zs_pset_zone {

	list_node_t	zspz_next;
	struct zs_pset	*zspz_pset;
	struct zs_zone	*zspz_zone;
	zoneid_t	zspz_zoneid;
	time_t		zspz_start;
	hrtime_t	zspz_hrstart;
	uint_t		zspz_intervals;

	uint64_t	zspz_cpu_shares;
	uint_t		zspz_scheds;

	timestruc_t	zspz_cpu_usage;

	/* The following provide space for the given properties */

	/* ZS_PZ_PROP_SCHEDULERS */
	struct zs_property zspz_prop_schedulers;

	/* ZS_PZ_PROP_CPU_SHARES */
	struct zs_property zspz_prop_cpushares;

	/* ZS_PZ_PROP_CPU_CAP */
	struct zs_property zspz_prop_cpucap;

};

struct zs_link_zone {
	list_node_t	zlz_next;
	char		zlz_name[ZONENAME_MAX];
	uint64_t	zlz_total_bw;
	uint64_t	zlz_total_bytes;
	uint64_t	zlz_total_rbytes;
	uint64_t	zlz_total_obytes;
	uint64_t	zlz_total_prbytes;
	uint64_t	zlz_total_pobytes;
	uint_t		zlz_partial_bw;

	/* The following provide space for the given properties */

	/* ZLZ_PROP_NAME */
	struct zs_property zlz_prop_name;

	/* ZLZ_PROP_BW */
	struct zs_property zlz_prop_bw;

	/* ZLZ_PROP_BYTES */
	struct zs_property zlz_prop_bytes;

	/* ZLZ_PROP_RBYTES */
	struct zs_property zlz_prop_rbytes;

	/* ZLZ_PROP_OBYTES */
	struct zs_property zlz_prop_obytes;

	/* ZLZ_PROP_PRBYTES */
	struct zs_property zlz_prop_prbytes;

	/* ZLZ_PROP_POBYTES */
	struct zs_property zlz_prop_pobytes;

	/* ZLZ_PROP_PARTBW */
	struct zs_property zlz_prop_partbw;
};

struct zs_datalink {
	list_node_t	zsl_next;
	char		zsl_linkname[MAXLINKNAMELEN];
	char		zsl_devname[MAXLINKNAMELEN];
	char		zsl_zonename[ZONENAME_MAX];
	char		zsl_state[10];	/* "up", "down", or "unknown" */
	list_t		zsl_vlink_list;
	list_t		zsl_zone_list;
	uint64_t	zsl_rbytes;
	uint64_t	zsl_obytes;
	uint64_t	zsl_prbytes;
	uint64_t	zsl_pobytes;
	uint64_t	zsl_speed;
	uint64_t	zsl_maxbw;
	datalink_id_t	zsl_linkid;
	datalink_class_t zsl_class;
	zoneid_t	zsl_zoneid;
	uint_t		zsl_nclients;
	uint_t		zsl_nlinkzones;
	uint_t		zsl_intervals;
	hrtime_t	zsl_hrtime;
	uint64_t	zsl_total_rbytes;
	uint64_t	zsl_total_obytes;
	uint64_t	zsl_total_prbytes;
	uint64_t	zsl_total_pobytes;

	/* The following provide space for the given properties */

	/* ZSL_PROP_LINKNAME */
	struct zs_property zsl_prop_linkname;

	/* ZSL_PROP_DEVNAME */
	struct zs_property zsl_prop_devname;

	/* ZSL_PROP_ZONENAME */
	struct zs_property zsl_prop_zonename;

	/* ZSL_PROP_STATE */
	struct zs_property zsl_prop_state;

	/* ZSL_PROP_CLASS */
	struct zs_property zsl_prop_class;

	/* ZSL_PROP_RBYTES */
	struct zs_property zsl_prop_rbytes;

	/* ZSL_PROP_OBYTES */
	struct zs_property zsl_prop_obytes;

	/* ZSL_PROP_PRBYTES */
	struct zs_property zsl_prop_prbytes;

	/* ZSL_PROP_POBYTES */
	struct zs_property zsl_prop_pobytes;

	/* ZSL_PROP_SPEED */
	struct zs_property zsl_prop_speed;

	/* ZSL_PROP_TOT_BYTES */
	struct zs_property zsl_prop_tot_bytes;

	/* ZSL_PROP_TOT_RBYTES */
	struct zs_property zsl_prop_tot_rbytes;

	/* ZSL_PROP_TOT_OBYTES */
	struct zs_property zsl_prop_tot_obytes;

	/* ZSL_PROP_TOT_PRBYTES */
	struct zs_property zsl_prop_tot_prbytes;

	/* ZSL_PROP_TOT_POBYTES */
	struct zs_property zsl_prop_tot_pobytes;

	/* ZSL_PROP_MAXBW */
	struct zs_property zsl_prop_maxbw;
};

struct zs_ctl {
	int	 zsctl_door;
	uint64_t zsctl_gen;
	struct zs_usage *zsctl_start;
};

struct zs_zone {
	list_node_t	zsz_next;
	struct zs_system *zsz_system;
	char		zsz_name[ZS_ZONENAME_MAX];
	char		zsz_pool[ZS_POOLNAME_MAX];
	char		zsz_pset[ZS_PSETNAME_MAX];
	zoneid_t	zsz_id;
	int		zsz_default_sched;
	uint_t		zsz_cputype;
	uint_t		zsz_iptype;
	time_t		zsz_start;
	hrtime_t	zsz_hrstart;
	uint_t		zsz_intervals;

	uint_t		zsz_scheds;
	uint64_t	zsz_cpu_shares;
	uint64_t	zsz_cpu_cap;
	uint64_t	zsz_ram_cap;
	uint64_t	zsz_vm_cap;
	uint64_t	zsz_locked_cap;

	uint64_t	zsz_cpus_online;
	timestruc_t	zsz_cpu_usage;
	timestruc_t	zsz_pset_time;
	timestruc_t	zsz_cap_time;
	timestruc_t	zsz_share_time;

	uint64_t	zsz_usage_ram;
	uint64_t	zsz_usage_locked;
	uint64_t	zsz_usage_vm;

	uint64_t	zsz_processes_cap;
	uint64_t	zsz_lwps_cap;
	uint64_t	zsz_shm_cap;
	uint64_t	zsz_shmids_cap;
	uint64_t	zsz_semids_cap;
	uint64_t	zsz_msgids_cap;
	uint64_t	zsz_lofi_cap;

	uint64_t	zsz_processes;
	uint64_t	zsz_lwps;
	uint64_t	zsz_shm;
	uint64_t	zsz_shmids;
	uint64_t	zsz_semids;
	uint64_t	zsz_msgids;
	uint64_t	zsz_lofi;

	uint64_t	zsz_tot_bytes;
	uint64_t	zsz_tot_pbytes;
	uint64_t	zsz_rbytes;
	uint64_t	zsz_obytes;
	uint64_t	zsz_prbytes;
	uint64_t	zsz_pobytes;
	uint64_t	zsz_speed;
	uint_t		zsz_pused;

	/* The following provide space for the given properties */

	/* ZS_ZONE_PROP_NAME */
	struct zs_property zsz_prop_name;

	/* ZS_ZONE_PROP_ID */
	struct zs_property zsz_prop_id;

	/* ZS_ZONE_PROP_IPTYPE */
	struct zs_property zsz_prop_iptype;

	/* ZS_ZONE_PROP_CPUTYPE */
	struct zs_property zsz_prop_cputype;

	/* ZS_ZONE_PROP_DEFAULT_SCHED */
	struct zs_property zsz_prop_defsched;

	/* ZS_ZONE_PROP_SCHEDULERS */
	struct zs_property zsz_prop_schedulers;

	/* ZS_ZONE_PROP_CPU_SHARES */
	struct zs_property zsz_prop_cpushares;

	/* ZS_ZONE_PROP_POOLNAME */
	struct zs_property zsz_prop_poolname;

	/* ZS_ZONE_PROP_PSETNAME */
	struct zs_property zsz_prop_psetname;
};

struct zs_pset {
	list_node_t	zsp_next;
	char		zsp_name[ZS_PSETNAME_MAX];
	psetid_t	zsp_id;
	uint_t		zsp_cputype;
	time_t		zsp_start;
	hrtime_t	zsp_hrstart;
	uint_t		zsp_intervals;

	uint64_t	zsp_online;
	uint64_t	zsp_size;
	uint64_t	zsp_min;
	uint64_t	zsp_max;
	int64_t		zsp_importance;
	double		zsp_load_avg[3];
	uint_t		zsp_scheds;
	uint64_t	zsp_cpu_shares;
	timestruc_t	zsp_total_time;
	timestruc_t	zsp_usage_kern;
	timestruc_t	zsp_usage_zones;

	uint_t		zsp_nusage;
	list_t		zsp_usage_list;

	/* The following provide space for the given propeties */

	/* ZS_PSET_PROP_NAME */
	struct zs_property zsp_prop_name;

	/* ZS_PSET_PROP_ID */
	struct zs_property zsp_prop_id;

	/* ZS_PSET_PROP_CPUTYPE */
	struct zs_property zsp_prop_cputype;

	/* ZS_PSET_PROP_SIZE */
	struct zs_property zsp_prop_size;

	/* ZS_PSET_PROP_ONLINE */
	struct zs_property zsp_prop_online;

	/* ZS_PSET_PROP_MIN */
	struct zs_property zsp_prop_min;

	/* ZS_PSET_PROP_MAX */
	struct zs_property zsp_prop_max;

	/* ZS_PSET_PROP_CPU_SHARES */
	struct zs_property zsp_prop_cpushares;

	/* ZS_PSET_PROP_SCHEDULERS */
	struct zs_property zsp_prop_schedulers;

	/* ZS_PSET_PROP_LOAD_1MIN */
	struct zs_property zsp_prop_1min;

	/* ZS_PSET_PROP_LOAD_5MIN */
	struct zs_property zsp_prop_5min;

	/* ZS_PSET_PROP_LOAD_15MIN */
	struct zs_property zsp_prop_15min;
};

struct zs_usage {
	time_t		zsu_start;
	hrtime_t	zsu_hrstart;
	time_t		zsu_time;
	hrtime_t	zsu_hrtime;
	uint64_t	zsu_size;
	uint_t		zsu_intervals;
	hrtime_t	zsu_hrintervaltime;
	uint64_t	zsu_gen;
	boolean_t	zsu_mmap;
	uint_t		zsu_nzones;
	uint_t		zsu_npsets;
	uint_t		zsu_ndatalinks;
	uint_t		zsu_nvlinks;
	struct zs_system *zsu_system;
	list_t		zsu_zone_list;
	list_t		zsu_pset_list;
	list_t		zsu_datalink_list;
};

struct zs_usage_set {
	struct zs_usage *zsus_total;
	struct zs_usage *zsus_avg;
	struct zs_usage *zsus_high;
	uint_t		zsus_count;
};

struct zs_usage_cache {
	int zsuc_ref;
	uint_t zsuc_size;
	uint64_t zsuc_gen;
	struct zs_usage *zsuc_usage;
};


#ifdef __cplusplus
}
#endif

#endif	/* _ZONESTAT_IMPL_H */
