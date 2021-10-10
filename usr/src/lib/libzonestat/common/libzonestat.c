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

#include <alloca.h>
#include <assert.h>
#include <door.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <strings.h>
#include <sys/loadavg.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <zonestat.h>
#include <zonestat_private.h>
#include <zonestat_impl.h>

#define	ZSD_PCT_INT	10000
#define	ZSD_PCT_DOUBLE	10000.0

#define	ZSD_ONE_CPU	100

#ifndef	MIN
#define	MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef	MAX
#define	MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif

#define	ZS_MAXTS(a, b) ((b).tv_sec > (a).tv_sec || \
	((b).tv_sec == (a).tv_sec && (b).tv_nsec > (a).tv_nsec) ? (b) : (a))


/* Compute max, treating ZS_LIMIT_NONE as zero */
#define	ZS_MAXOF(a, b) { \
	if ((b) != ZS_LIMIT_NONE) { \
		if ((a) == ZS_LIMIT_NONE) \
			(a) = (b); \
		else if ((b) > (a)) \
		(b) = (a); \
	} \
	}

/* Add two caps together, treating ZS_LIMIT_NONE as zero */
#define	ZS_ADD_CAP(a, b) { \
	if ((b) != ZS_LIMIT_NONE) { \
		if ((a) == ZS_LIMIT_NONE) \
			(a) = (b); \
		else \
		(a) += (b); \
	} \
	}

#define	ZS_MAXOFTS(a, b) { \
    if ((b).tv_sec > (a).tv_sec) (a) = (b); \
    else if ((b).tv_nsec > (a).tv_nsec) (a) = (b); }


static uint_t zs_uint64_used_pct(uint64_t, uint64_t, boolean_t);

/*
 * Functions for reading and manipulating resource usage.
 */
static int
zs_connect_zonestatd()
{
	int fd;

	fd = open(ZS_DOOR_PATH, O_RDONLY);
	return (fd);
}

static struct zs_zone *
zs_lookup_zone_byid(struct zs_usage *u, zoneid_t zid)
{
	struct zs_zone *zone;

	for (zone = list_head(&u->zsu_zone_list); zone != NULL;
	    zone = list_next(&u->zsu_zone_list, zone)) {
		if (zone->zsz_id == zid)
			return (zone);
	}
	return (NULL);
}

static struct zs_zone *
zs_lookup_zone_byname(struct zs_usage *u, char *name)
{
	struct zs_zone *zone;

	for (zone = list_head(&u->zsu_zone_list); zone != NULL;
	    zone = list_next(&u->zsu_zone_list, zone)) {
		if (strcmp(zone->zsz_name, name) == 0)
			return (zone);
	}
	return (NULL);
}

static struct zs_usage *
zs_usage_alloc()
{
	struct zs_usage *u;
	struct zs_system *s;

	u = (struct zs_usage *)calloc(1, sizeof (struct zs_usage));
	if (u == NULL)
		return (NULL);

	s = (struct zs_system *)calloc(1, sizeof (struct zs_system));
	if (s == NULL) {
		free(u);
		return (NULL);
	}

	u->zsu_mmap = B_FALSE;
	u->zsu_system = s;
	list_create(&u->zsu_zone_list, sizeof (struct zs_zone),
	    offsetof(struct zs_zone, zsz_next));
	list_create(&u->zsu_pset_list, sizeof (struct zs_pset),
	    offsetof(struct zs_pset, zsp_next));
	list_create(&u->zsu_datalink_list, sizeof (struct zs_datalink),
	    offsetof(struct zs_datalink, zsl_next));

	return (u);
}

static void
zs_zone_add_usage(struct zs_zone *old, struct zs_zone *new, zs_compute_t func)
{
	uint64_t	pused;
	uint64_t	bytes, pbytes;

	bytes = new->zsz_tot_bytes - old->zsz_tot_bytes;
	pbytes = new->zsz_tot_pbytes - old->zsz_tot_pbytes;
	pused = zs_uint64_used_pct(new->zsz_speed, pbytes, B_FALSE);

	if (func == ZS_COMPUTE_USAGE_HIGH) {

		/* Compute max of caps */
		ZS_MAXOF(old->zsz_cpu_cap, new->zsz_cpu_cap);
		ZS_MAXOF(old->zsz_cpu_shares, new->zsz_cpu_shares);
		ZS_MAXOF(old->zsz_ram_cap, new->zsz_ram_cap);
		ZS_MAXOF(old->zsz_locked_cap, new->zsz_locked_cap);
		ZS_MAXOF(old->zsz_vm_cap, new->zsz_vm_cap);
		ZS_MAXOF(old->zsz_processes_cap, new->zsz_processes_cap);
		ZS_MAXOF(old->zsz_lwps_cap, new->zsz_lwps_cap);
		ZS_MAXOF(old->zsz_shm_cap, new->zsz_shm_cap);
		ZS_MAXOF(old->zsz_shmids_cap, new->zsz_shmids_cap);
		ZS_MAXOF(old->zsz_semids_cap, new->zsz_semids_cap);
		ZS_MAXOF(old->zsz_msgids_cap, new->zsz_msgids_cap);
		ZS_MAXOF(old->zsz_lofi_cap, new->zsz_lofi_cap);

		/* Compute max memory and limit usages */
		ZS_MAXOF(old->zsz_usage_ram, new->zsz_usage_ram);
		ZS_MAXOF(old->zsz_usage_locked, new->zsz_usage_locked);
		ZS_MAXOF(old->zsz_usage_ram, new->zsz_usage_ram);

		ZS_MAXOF(old->zsz_processes, new->zsz_processes);
		ZS_MAXOF(old->zsz_lwps, new->zsz_lwps);
		ZS_MAXOF(old->zsz_shm, new->zsz_shm);
		ZS_MAXOF(old->zsz_shmids, new->zsz_shmids);
		ZS_MAXOF(old->zsz_semids, new->zsz_semids);
		ZS_MAXOF(old->zsz_msgids, new->zsz_msgids);
		ZS_MAXOF(old->zsz_lofi, new->zsz_lofi);

		ZS_MAXOF(old->zsz_cpus_online, new->zsz_cpus_online);

		ZS_MAXOF(old->zsz_tot_bytes, new->zsz_tot_bytes);
		ZS_MAXOF(old->zsz_tot_pbytes, new->zsz_tot_pbytes);
		ZS_MAXOF(old->zsz_speed, new->zsz_speed);
		old->zsz_pused = MAX(old->zsz_pused, pused);

		ZS_MAXOFTS(old->zsz_cpu_usage, new->zsz_cpu_usage);
		ZS_MAXOFTS(old->zsz_pset_time, new->zsz_pset_time);
		ZS_MAXOFTS(old->zsz_cap_time, new->zsz_cap_time);
		ZS_MAXOFTS(old->zsz_share_time, new->zsz_share_time);
		return;
	}

	ZS_ADD_CAP(old->zsz_cpu_cap, new->zsz_cpu_cap);
	ZS_ADD_CAP(old->zsz_ram_cap, new->zsz_ram_cap);
	ZS_ADD_CAP(old->zsz_locked_cap, new->zsz_locked_cap);
	ZS_ADD_CAP(old->zsz_vm_cap, new->zsz_vm_cap);
	ZS_ADD_CAP(old->zsz_processes_cap, new->zsz_processes_cap);
	ZS_ADD_CAP(old->zsz_lwps_cap, new->zsz_lwps_cap);
	ZS_ADD_CAP(old->zsz_shm_cap, new->zsz_shm_cap);
	ZS_ADD_CAP(old->zsz_shmids_cap, new->zsz_shmids_cap);
	ZS_ADD_CAP(old->zsz_semids_cap, new->zsz_semids_cap);
	ZS_ADD_CAP(old->zsz_msgids_cap, new->zsz_msgids_cap);
	ZS_ADD_CAP(old->zsz_lofi_cap, new->zsz_lofi_cap);

	/* Add in memory and limit usages */
	old->zsz_usage_ram += new->zsz_usage_ram;
	old->zsz_usage_locked += new->zsz_usage_locked;
	old->zsz_usage_vm += new->zsz_usage_vm;

	old->zsz_processes += new->zsz_processes;
	old->zsz_lwps += new->zsz_lwps;
	old->zsz_shm += new->zsz_shm;
	old->zsz_shmids += new->zsz_shmids;
	old->zsz_semids += new->zsz_semids;
	old->zsz_msgids += new->zsz_msgids;
	old->zsz_lofi += new->zsz_lofi;

	old->zsz_cpus_online += new->zsz_cpus_online;
	old->zsz_cpu_shares += new->zsz_cpu_shares;

	old->zsz_tot_bytes += bytes;
	old->zsz_tot_pbytes += pbytes;
	old->zsz_speed += new->zsz_speed;
	old->zsz_pused += new->zsz_pused;

	TIMESTRUC_ADD_TIMESTRUC(old->zsz_cpu_usage, new->zsz_cpu_usage);
	TIMESTRUC_ADD_TIMESTRUC(old->zsz_pset_time, new->zsz_pset_time);
	TIMESTRUC_ADD_TIMESTRUC(old->zsz_cap_time, new->zsz_cap_time);
	TIMESTRUC_ADD_TIMESTRUC(old->zsz_share_time, new->zsz_share_time);
}

static int
zs_usage_compute_zones(struct zs_usage *ures, struct zs_usage *uold,
    struct zs_usage *unew, zs_compute_t func)
{
	struct zs_system *sres;
	struct zs_zone *zold, *znew, *zres;

	sres = ures->zsu_system;
	/*
	 * Walk zones, assume lists are always sorted the same.  Include
	 * all zones that exist in the new usage.
	 */
	zold = list_head(&uold->zsu_zone_list);
	znew = list_head(&unew->zsu_zone_list);

	while (zold != NULL && znew != NULL) {

		int cmp;

		cmp = strcmp(zold->zsz_name, znew->zsz_name);
		if (cmp > 0) {
			/*
			 * Old interval does not contain zone in new
			 * interval.  Zone is new.  Add zone to result.
			 */
			if (ures != unew) {
				zres = (struct zs_zone *)
				    calloc(1, sizeof (struct zs_zone));
				if (zres == NULL)
					return (-1);
				*zres = *znew;

				zres->zsz_system = sres;
				list_link_init(&zres->zsz_next);
				zres->zsz_intervals = 0;
				if (ures == uold)
					list_insert_before(&uold->zsu_zone_list,
					    zold, zres);
				else
					list_insert_tail(&ures->zsu_zone_list,
					    zres);

			} else {
				zres = znew;
			}

			if (func == ZS_COMPUTE_USAGE_AVERAGE)
				zres->zsz_intervals++;

			znew = list_next(&unew->zsu_zone_list, znew);
			continue;

		} else if (cmp < 0) {
			/*
			 * Start interval contains zones that is not in the
			 * end interval.  This zone is gone.  Leave zone in
			 * old usage, but do not add it to result usage
			 */
			zold = list_next(&uold->zsu_zone_list, zold);
			continue;
		}

		/* Zone is in both start and end interval.  Compute interval */
		if (ures == uold) {
			zres = zold;
		} else if (ures == unew) {
			zres = znew;
		} else {
			/* add zone to new usage */
			zres = (struct zs_zone *)
			    calloc(1, sizeof (struct zs_zone));
			if (zres == NULL)
				return (-1);
			*zres = *znew;
			zres->zsz_system = sres;
			list_insert_tail(&ures->zsu_zone_list, zres);
		}
		if (func == ZS_COMPUTE_USAGE_AVERAGE)
			zres->zsz_intervals++;
		if (func == ZS_COMPUTE_USAGE_INTERVAL) {
			/*
			 * If zone is in the old interval, but has been
			 * rebooted, don't subtract its old interval usage
			 */
			if (zres->zsz_hrstart > uold->zsu_hrtime) {
				znew = list_next(&unew->zsu_zone_list, znew);
				zold = list_next(&uold->zsu_zone_list, zold);
				continue;
			}
			TIMESTRUC_DELTA(zres->zsz_cpu_usage,
			    znew->zsz_cpu_usage, zold->zsz_cpu_usage);
			TIMESTRUC_DELTA(zres->zsz_cap_time, znew->zsz_cap_time,
			    zold->zsz_cap_time);
			TIMESTRUC_DELTA(zres->zsz_share_time,
			    znew->zsz_share_time, zold->zsz_share_time);
			TIMESTRUC_DELTA(zres->zsz_pset_time,
			    znew->zsz_pset_time, zold->zsz_pset_time);

			zres->zsz_tot_bytes = znew->zsz_tot_bytes -
			    zold->zsz_tot_bytes;
			zres->zsz_tot_pbytes = znew->zsz_tot_pbytes -
			    zold->zsz_tot_pbytes;
			zres->zsz_speed = znew->zsz_speed;
			zres->zsz_pused = zs_uint64_used_pct(
			    znew->zsz_speed, zres->zsz_tot_pbytes,
			    B_FALSE);
		} else {
			zs_zone_add_usage(zres, znew, func);
		}
		znew = list_next(&unew->zsu_zone_list, znew);
		zold = list_next(&uold->zsu_zone_list, zold);
	}

	if (ures == unew)
		return (0);

	/* Add in any remaining zones in the new interval */
	while (znew != NULL) {
		zres = (struct zs_zone *)calloc(1, sizeof (struct zs_zone));
		if (zres == NULL)
			return (-1);
		*zres = *znew;
		zres->zsz_system = sres;
		if (func == ZS_COMPUTE_USAGE_AVERAGE)
			zres->zsz_intervals++;
		if (ures == uold)
			list_insert_tail(&uold->zsu_zone_list, zres);
		else
			list_insert_tail(&ures->zsu_zone_list, zres);

		znew = list_next(&unew->zsu_zone_list, znew);
	}
	return (0);
}

static void
zs_pset_zone_add_usage(struct zs_pset_zone *old, struct zs_pset_zone *new,
    zs_compute_t func)
{
	if (func == ZS_COMPUTE_USAGE_HIGH) {
		ZS_MAXOF(old->zspz_cpu_shares, new->zspz_cpu_shares);
		ZS_MAXOFTS(old->zspz_cpu_usage, new->zspz_cpu_usage);
		return;
	}
	old->zspz_cpu_shares += new->zspz_cpu_shares;
	TIMESTRUC_ADD_TIMESTRUC(old->zspz_cpu_usage, new->zspz_cpu_usage);
}

static int
zs_usage_compute_pset_usage(struct zs_usage *uold, struct zs_usage *ures,
    struct zs_pset *pres, struct zs_pset *pold, struct zs_pset *pnew,
    zs_compute_t func)
{
	struct zs_pset_zone *puold, *punew, *pures;

	/*
	 * Walk psets usages, assume lists are always sorted the same.  Include
	 * all pset usages that exist in the new pset.
	 */
	if (pold == NULL)
		puold = NULL;
	else
		puold = list_head(&pold->zsp_usage_list);
	punew = list_head(&pnew->zsp_usage_list);

	while (puold != NULL && punew != NULL) {

		int cmp;

		cmp = strcmp(puold->zspz_zone->zsz_name,
		    punew->zspz_zone->zsz_name);
		if (cmp > 0) {
			/*
			 * Old interval does not contain usage new
			 * interval.  Usage is new.
			 */
			if (pres != pnew) {
				pures = (struct zs_pset_zone *)malloc(
				    sizeof (struct zs_pset_zone));
				if (pures == NULL)
					return (-1);
				*pures = *punew;

				pures->zspz_pset = pres;
				pures->zspz_zone = zs_lookup_zone_byname(ures,
				    punew->zspz_zone->zsz_name);
				assert(pures->zspz_zone != NULL);
				pures->zspz_intervals = 0;
				if (pres == pold)
					list_insert_before(
					    &pold->zsp_usage_list, puold,
					    pures);
				else
					list_insert_tail(&pres->zsp_usage_list,
					    pures);
			} else {
				pures = punew;
			}
			if (func == ZS_COMPUTE_USAGE_AVERAGE)
				pures->zspz_intervals++;
			else if (func == ZS_COMPUTE_USAGE_TOTAL) {
				/* Add pset's time so far to the zone usage */
				TIMESTRUC_ADD_TIMESTRUC(
				    pures->zspz_zone->zsz_pset_time,
				    pres->zsp_total_time);
				pures->zspz_zone->zsz_cpus_online +=
				    pres->zsp_online;
			}

			punew = list_next(&pnew->zsp_usage_list, punew);
			continue;
		} else if (cmp < 0) {

			/*
			 * Old interval contains pset_zone that is not in the
			 * new interval.  This zone is no longer using the
			 * pset.  Leave pset_zone in old interval, but do not
			 * add it to result usage.
			 *
			 * For total utilization, add pset time to zone that
			 * has run in this pset before.
			 */
			if (func == ZS_COMPUTE_USAGE_TOTAL) {
				/* Add new pset time to the zone usage */
				TIMESTRUC_ADD_TIMESTRUC(
				    puold->zspz_zone->zsz_pset_time,
				    pnew->zsp_total_time);
				puold->zspz_zone->zsz_cpus_online +=
				    pnew->zsp_online;
			}
			puold = list_next(&pold->zsp_usage_list, puold);
			continue;
		}
		/*
		 * Zone is using pset in both start and end interval.  Compute
		 * interval
		 */
		if (pres == pold) {
			pures = puold;
		} else if (pres == pnew) {
			pures = punew;
		} else {
			pures = (struct zs_pset_zone *)malloc(
			    sizeof (struct zs_pset_zone));
			if (pures == NULL)
				return (-1);
			*pures = *punew;
			pures->zspz_pset = pres;
			pures->zspz_zone = zs_lookup_zone_byname(ures,
			    punew->zspz_zone->zsz_name);
			assert(pures->zspz_zone != NULL);
			list_insert_tail(&pres->zsp_usage_list, pures);
		}
		if (func == ZS_COMPUTE_USAGE_AVERAGE)
			pures->zspz_intervals++;

		if (func == ZS_COMPUTE_USAGE_INTERVAL) {
			/*
			 * If pset usage has been destroyed and re-created
			 * since start interval, don't subtract the start
			 * interval.
			 */
			if (punew->zspz_hrstart > uold->zsu_hrtime) {
				punew = list_next(&pnew->zsp_usage_list, punew);
				puold = list_next(&pold->zsp_usage_list, puold);
				continue;
			}
			TIMESTRUC_DELTA(pures->zspz_cpu_usage,
			    punew->zspz_cpu_usage, puold->zspz_cpu_usage);
		} else {
			zs_pset_zone_add_usage(pures, punew, func);
		}
		punew = list_next(&pnew->zsp_usage_list, punew);
		puold = list_next(&pold->zsp_usage_list, puold);
	}
	if (func == ZS_COMPUTE_USAGE_TOTAL) {
		while (puold != NULL) {
			TIMESTRUC_ADD_TIMESTRUC(
			    puold->zspz_zone->zsz_pset_time,
			    pnew->zsp_total_time);
			puold->zspz_zone->zsz_cpus_online +=
			    pnew->zsp_online;
			puold = list_next(&pold->zsp_usage_list, puold);
		}
	}

	/* No need to add new pset zone usages if result pset is new pset */
	if (pres == pnew)
		return (0);

	/* Add in any remaining new psets in the new interval */
	while (punew != NULL) {
		pures = (struct zs_pset_zone *)
		    calloc(1, sizeof (struct zs_pset_zone));
		if (pures == NULL)
			return (-1);
		*pures = *punew;
		pures->zspz_pset = pres;
		pures->zspz_zone = zs_lookup_zone_byname(ures,
		    punew->zspz_zone->zsz_name);
		assert(pures->zspz_zone  != NULL);
		if (func == ZS_COMPUTE_USAGE_AVERAGE)
			pures->zspz_intervals++;
		if (pres == pold)
			list_insert_tail(&pold->zsp_usage_list, pures);
		else
			list_insert_tail(&pres->zsp_usage_list, pures);

		punew = list_next(&pnew->zsp_usage_list, punew);
	}
	return (0);
}

static void
zs_pset_add_usage(struct zs_pset *old, struct zs_pset *new, zs_compute_t func)
{

	if (func == ZS_COMPUTE_USAGE_HIGH) {
		ZS_MAXOF(old->zsp_online, new->zsp_online);
		ZS_MAXOF(old->zsp_size, new->zsp_size);
		ZS_MAXOF(old->zsp_min, new->zsp_min);
		ZS_MAXOF(old->zsp_max, new->zsp_max);
		ZS_MAXOF(old->zsp_importance, new->zsp_importance);
		ZS_MAXOF(old->zsp_cpu_shares, new->zsp_cpu_shares);
		ZS_MAXOFTS(old->zsp_total_time, new->zsp_total_time);
		ZS_MAXOFTS(old->zsp_usage_kern, new->zsp_usage_kern);
		ZS_MAXOFTS(old->zsp_usage_zones, new->zsp_usage_zones);
		return;
	}
	old->zsp_online += new->zsp_online;
	old->zsp_size += new->zsp_size;
	old->zsp_min += new->zsp_min;
	old->zsp_max += new->zsp_max;
	old->zsp_importance += new->zsp_importance;
	old->zsp_cpu_shares += new->zsp_cpu_shares;
	TIMESTRUC_ADD_TIMESTRUC(old->zsp_total_time, new->zsp_total_time);
	TIMESTRUC_ADD_TIMESTRUC(old->zsp_usage_kern, new->zsp_usage_kern);
	TIMESTRUC_ADD_TIMESTRUC(old->zsp_usage_zones, new->zsp_usage_zones);
}

static int
zs_usage_compute_psets(struct zs_usage *ures, struct zs_usage *uold,
    struct zs_usage *unew, zs_compute_t func)
{
	struct zs_pset *pold, *pnew, *pres;

	/*
	 * Walk psets, assume lists are always sorted the same.  Include
	 * all psets that exist at the end of the interval.
	 */
	pold = list_head(&uold->zsu_pset_list);
	pnew = list_head(&unew->zsu_pset_list);

	while (pold != NULL && pnew != NULL) {

		int cmp;

		cmp = strcmp(pold->zsp_name, pnew->zsp_name);
		if (cmp > 0) {
			/*
			 * Old interval does not contain pset in new
			 * interval.  Pset is new.
			 */
			if (ures != unew) {
				pres = (struct zs_pset *)
				    malloc(sizeof (struct zs_pset));
				if (pres == NULL)
					return (-1);
				*pres = *pnew;
				pres->zsp_intervals = 0;
				list_create(&pres->zsp_usage_list,
				    sizeof (struct zs_pset_zone),
				    offsetof(struct zs_pset_zone, zspz_next));

				if (ures == uold)
					list_insert_before(&uold->zsu_pset_list,
					    pold, pres);
				else
					list_insert_tail(&ures->zsu_pset_list,
					    pres);

			} else {
				pres = pnew;
			}
			if (zs_usage_compute_pset_usage(uold, ures, pres,
			    NULL, pnew, func) != 0)
				return (-1);

			if (func == ZS_COMPUTE_USAGE_AVERAGE ||
			    func == ZS_COMPUTE_USAGE_TOTAL)
				pres->zsp_intervals++;
			pnew = list_next(&unew->zsu_pset_list, pnew);
			continue;

		} else if (cmp < 0) {
			/*
			 * Start interval contains psets that is not in the
			 * end interval.  This pset is gone.  Leave pset in
			 * old usage, but do not add it to result usage.
			 */
			pold = list_next(&uold->zsu_pset_list, pold);
			continue;
		}

		/* Pset is in both start and end interval.  Compute interval */
		if (ures == uold) {
			pres = pold;
		} else if (ures == unew) {
			pres = pnew;
		} else {
			pres = (struct zs_pset *)
			    calloc(1, sizeof (struct zs_pset));
			if (pres == NULL)
				return (-1);

			*pres = *pnew;
			list_create(&pres->zsp_usage_list,
			    sizeof (struct zs_pset_zone),
			    offsetof(struct zs_pset_zone, zspz_next));
			list_insert_tail(&ures->zsu_pset_list, pres);
		}
		if (func == ZS_COMPUTE_USAGE_AVERAGE ||
		    func == ZS_COMPUTE_USAGE_TOTAL)
			pres->zsp_intervals++;
		if (func == ZS_COMPUTE_USAGE_INTERVAL) {
			/*
			 * If pset as been destroyed and re-created since start
			 * interval, don't subtract the start interval.
			 */
			if (pnew->zsp_hrstart > uold->zsu_hrtime) {
				goto usages;
			}
			TIMESTRUC_DELTA(pres->zsp_total_time,
			    pnew->zsp_total_time, pold->zsp_total_time);

			TIMESTRUC_DELTA(pres->zsp_usage_kern,
			    pnew->zsp_usage_kern, pold->zsp_usage_kern);
			TIMESTRUC_DELTA(pres->zsp_usage_zones,
			    pnew->zsp_usage_zones, pold->zsp_usage_zones);
		} else {
			zs_pset_add_usage(pres, pnew, func);
		}
usages:
		if (zs_usage_compute_pset_usage(uold, ures, pres, pold,
		    pnew, func) != 0)
			return (-1);

		pnew = list_next(&unew->zsu_pset_list, pnew);
		pold = list_next(&uold->zsu_pset_list, pold);
	}

	if (ures == unew)
		return (0);

	/* Add in any remaining psets in the new interval */
	while (pnew != NULL) {
		pres = (struct zs_pset *)calloc(1, sizeof (struct zs_pset));
		if (pres == NULL)
			return (-1);
		*pres = *pnew;
		list_create(&pres->zsp_usage_list,
		    sizeof (struct zs_pset_zone),
		    offsetof(struct zs_pset_zone, zspz_next));
		if (func == ZS_COMPUTE_USAGE_AVERAGE ||
		    func == ZS_COMPUTE_USAGE_TOTAL)
			pres->zsp_intervals++;
		if (ures == uold)
			list_insert_tail(&uold->zsu_pset_list, pres);
		else
			list_insert_tail(&ures->zsu_pset_list, pres);

		if (zs_usage_compute_pset_usage(uold, ures, pres, NULL,
		    pnew, func) != 0)
			return (-1);

		pnew = list_next(&unew->zsu_pset_list, pnew);
	}
	return (0);
}

static int
zs_update_link_zone(struct zs_datalink *dlink, struct zs_datalink *vlink,
    boolean_t is_vlink)
{
	zs_link_zone_t zone;

	for (zone = list_head(&dlink->zsl_zone_list); zone != NULL;
	    zone = list_next(&dlink->zsl_zone_list, zone)) {
		if (strcmp(zone->zlz_name, vlink->zsl_zonename) == 0)
			break;
	}
	if (zone == NULL) {
		zone = (zs_link_zone_t)
		    calloc(1, sizeof (struct zs_link_zone));
		if (zone == NULL)
			return (-1);
		list_link_init(&zone->zlz_next);
		list_insert_tail(&dlink->zsl_zone_list, zone);
		(void) strlcpy(zone->zlz_name, vlink->zsl_zonename,
		    sizeof (zone->zlz_name));
	}
	zone->zlz_total_rbytes += vlink->zsl_rbytes;
	zone->zlz_total_obytes += vlink->zsl_obytes;
	zone->zlz_total_bytes += (vlink->zsl_rbytes + vlink->zsl_obytes);
	zone->zlz_total_prbytes += vlink->zsl_prbytes;
	zone->zlz_total_pobytes += vlink->zsl_pobytes;
	if (is_vlink) {
		if (vlink->zsl_maxbw == 0)
			zone->zlz_partial_bw = 1;
		else
			zone->zlz_total_bw += vlink->zsl_maxbw;
	}
	return (0);
}

static void
zs_link_add_usage(struct zs_datalink *dres, struct zs_datalink *dold,
    struct zs_datalink *dnew, zs_compute_t func)
{
	switch (func) {
	case ZS_COMPUTE_USAGE_HIGH:
		dres->zsl_rbytes = MAX(dold->zsl_rbytes, dnew->zsl_rbytes);
		dres->zsl_obytes = MAX(dold->zsl_obytes, dnew->zsl_obytes);
		dres->zsl_prbytes = MAX(dold->zsl_prbytes, dnew->zsl_prbytes);
		dres->zsl_pobytes = MAX(dold->zsl_pobytes, dnew->zsl_pobytes);
		dres->zsl_speed = MAX(dold->zsl_speed, dnew->zsl_speed);
		break;

	case ZS_COMPUTE_USAGE_AVERAGE:
		/* fall through */
	case ZS_COMPUTE_USAGE_TOTAL:
		dres->zsl_rbytes += dnew->zsl_rbytes;
		dres->zsl_obytes += dnew->zsl_obytes;
		dres->zsl_prbytes += dnew->zsl_prbytes;
		dres->zsl_pobytes += dnew->zsl_pobytes;
		dres->zsl_hrtime += dnew->zsl_hrtime;
		break;

	default:
		break;
	}
}

static int
zs_usage_compute_vlink_usage(struct zs_datalink *dres, struct zs_datalink *dold,
    struct zs_datalink *dnew, zs_compute_t func)
{
	struct zs_datalink *duold, *dunew, *dures;

	/*
	 * Walk vlinks, assume lists are always sorted the same.  Include
	 * all vlink usages that exist in the new datalink.
	 */
	if (dold == NULL)
		duold = NULL;
	else
		duold = list_head(&dold->zsl_vlink_list);
	dunew = list_head(&dnew->zsl_vlink_list);

	while (duold != NULL && dunew != NULL) {
		int cmp;

		cmp = strcmp(duold->zsl_linkname, dunew->zsl_linkname);
		if (cmp > 0) {
			/*
			 * Old interval does not contain the vlink in new
			 * interval.  This vlink is new or renamed. Put it
			 * in result usage but don't add it to totals. We
			 * under port the vlink usage by up to one interval.
			 */
			if (dres != dnew) {
				dures = (struct zs_datalink *)malloc(
				    sizeof (struct zs_datalink));
				if (dures == NULL)
					return (-1);
				*dures = *dunew;
				dures->zsl_intervals = 0;
				if (dres == dold)
					list_insert_before(
					    &dold->zsl_vlink_list, duold,
					    dures);
				else
					list_insert_tail(&dres->zsl_vlink_list,
					    dures);
				if (zs_update_link_zone(dres, dures,
				    B_TRUE) != 0)
					return (-1);
			} else {
				dures = dunew;
			}
			/* vlink is always up */
			if (func == ZS_COMPUTE_USAGE_AVERAGE)
				dures->zsl_intervals++;

			dunew = list_next(&dnew->zsl_vlink_list, dunew);
			continue;
		} else if (cmp < 0) {
			/*
			 * Old interval contains vlink that is not in the
			 * new interval.  This vlink is no longer present.
			 * Leave the vlink in old interval, but do not
			 * add it to result usage.
			 */
			duold = list_next(&dold->zsl_vlink_list, duold);
			continue;
		}
		/*
		 * vlink is in both start and end interval.  Compute interval
		 */
		if (dres == dold) {
			dures = duold;
		} else if (dres == dnew) {
			dures = dunew;
		} else {
			dures = (struct zs_datalink *)malloc(
			    sizeof (struct zs_datalink));
			if (dures == NULL)
				return (-1);
			*dures = *dunew;
			list_insert_tail(&dres->zsl_vlink_list, dures);
		}
		if (func == ZS_COMPUTE_USAGE_AVERAGE ||
		    func == ZS_COMPUTE_USAGE_TOTAL)
			dures->zsl_intervals++;

		if (func == ZS_COMPUTE_USAGE_INTERVAL) {
			dures->zsl_rbytes = dunew->zsl_rbytes -
			    duold->zsl_rbytes;
			dures->zsl_obytes = dunew->zsl_obytes -
			    duold->zsl_obytes;
			dures->zsl_prbytes = dunew->zsl_prbytes -
			    duold->zsl_prbytes;
			dures->zsl_pobytes = dunew->zsl_pobytes -
			    duold->zsl_pobytes;
			dures->zsl_hrtime = dunew->zsl_hrtime -
			    duold->zsl_hrtime;
			/*
			 * add byte counts to link total
			 */
			dres->zsl_total_rbytes += dures->zsl_rbytes;
			dres->zsl_total_obytes += dures->zsl_obytes;
			dres->zsl_total_prbytes += dures->zsl_prbytes;
			dres->zsl_total_pobytes += dures->zsl_pobytes;

			if (zs_update_link_zone(dres, dures, B_TRUE) != 0)
				return (-1);
		} else
			zs_link_add_usage(dures, duold, dunew, func);

		dunew = list_next(&dnew->zsl_vlink_list, dunew);
		duold = list_next(&dold->zsl_vlink_list, duold);
	}

	/* Add in any remaining new datalink in the new interval */
	while (dunew != NULL) {
		if (dres != dnew) {
			dures = (struct zs_datalink *)calloc(1,
			    sizeof (struct zs_datalink));
			if (dures == NULL)
				return (-1);
			*dures = *dunew;
			list_insert_tail(&dres->zsl_vlink_list, dures);
		}
		/* vlink is always up */
		if (func == ZS_COMPUTE_USAGE_AVERAGE)
			dures->zsl_intervals++;

		dres->zsl_total_rbytes += dunew->zsl_rbytes;
		dres->zsl_total_obytes += dunew->zsl_obytes;
		dres->zsl_total_prbytes += dunew->zsl_prbytes;
		dres->zsl_total_pobytes += dunew->zsl_pobytes;

		if (zs_update_link_zone(dres, dures, B_TRUE) != 0)
			return (-1);

		dunew = list_next(&dnew->zsl_vlink_list, dunew);
	}
	return (0);
}

static int
zs_usage_compute_datalinks(struct zs_usage *ures, struct zs_usage *uold,
    struct zs_usage *unew, zs_compute_t func)
{
	struct zs_datalink *dold, *dnew, *dres;

	/*
	 * Walk datalinks, assume lists are always sorted the same.  Include
	 * all datalinks that exist at the end of the interval.
	 */
	dold = list_head(&uold->zsu_datalink_list);
	dnew = list_head(&unew->zsu_datalink_list);

	while (dold != NULL && dnew != NULL) {

		int cmp;

		cmp = strcmp(dold->zsl_linkname, dnew->zsl_linkname);
		if (cmp > 0) {
			/*
			 * Old interval does not contain the link in new
			 * interval.  Link is new.  Add it to result. We
			 * don't have sufficient information on this new
			 * datalink to know whether it is new, renamed, or
			 * used to be an aggr port device. Don't add stats
			 * to link/system total. We may be under reporting
			 * here.
			 */
			if (ures != unew) {
				dres = (struct zs_datalink *)calloc(1,
				    sizeof (struct zs_datalink));
				if (dres == NULL)
					return (-1);
				*dres = *dnew;
				dres->zsl_intervals = 0;
				list_create(&dres->zsl_vlink_list,
				    sizeof (struct zs_datalink),
				    offsetof(struct zs_datalink, zsl_next));
				list_create(&dres->zsl_zone_list,
				    sizeof (struct zs_link_zone),
				    offsetof(struct zs_link_zone, zlz_next));
				if (ures == uold)
					list_insert_before(
					    &uold->zsu_datalink_list,
					    dold, dres);
				else
					list_insert_tail(
					    &ures->zsu_datalink_list, dres);
			} else {
				dres = dnew;
			}

			/*
			 * increment interval count for up phys link
			 * or etherstub
			 */
			if (func == ZS_COMPUTE_USAGE_AVERAGE &&
			    (strcmp(dres->zsl_state, "up") == 0 ||
			    strcmp(dres->zsl_state, "n/a") == 0))
				dres->zsl_intervals++;
			if (zs_usage_compute_vlink_usage(dres, dold, dnew,
			    func) != 0)
				return (-1);
			dnew = list_next(&unew->zsu_datalink_list, dnew);
			continue;
		} else if (cmp < 0) {
			/*
			 * Start interval contains link that is not in the
			 * end interval.  This link is gone.  Leave link in
			 * old usage, but do not add it to result usage
			 */
			dold = list_next(&uold->zsu_datalink_list, dold);
			continue;
		}

		/* Link is in both start and end interval.  Compute interval */
		if (ures == uold) {
			dres = dold;
		} else if (ures == unew) {
			dres = dnew;
		} else {
			dres = (struct zs_datalink *)calloc(1,
			    sizeof (struct zs_datalink));
			if (dres == NULL)
				return (-1);
			*dres = *dnew;
			list_create(&dres->zsl_vlink_list,
			    sizeof (struct zs_datalink),
			    offsetof(struct zs_datalink, zsl_next));
			list_create(&dres->zsl_zone_list,
			    sizeof (struct zs_link_zone),
			    offsetof(struct zs_link_zone, zlz_next));
			list_insert_tail(&ures->zsu_datalink_list, dres);
		}

		/* increment interval count for "up" phys link or etherstub */
		if ((func == ZS_COMPUTE_USAGE_AVERAGE ||
		    func == ZS_COMPUTE_USAGE_HIGH) &&
		    (strcmp(dres->zsl_state, "up") == 0 ||
		    strcmp(dres->zsl_state, "n/a") == 0))
			dres->zsl_intervals++;
		if (func == ZS_COMPUTE_USAGE_INTERVAL) {
			/*
			 * reset total_* counters as they are supposed
			 * to count this interval only
			 */
			dres->zsl_total_rbytes = 0;
			dres->zsl_total_obytes = 0;
			dres->zsl_total_prbytes = 0;
			dres->zsl_total_pobytes = 0;

			dres->zsl_rbytes = dnew->zsl_rbytes - dold->zsl_rbytes;
			dres->zsl_obytes = dnew->zsl_obytes - dold->zsl_obytes;
			dres->zsl_prbytes = dnew->zsl_prbytes -
			    dold->zsl_prbytes;
			dres->zsl_pobytes = dnew->zsl_pobytes -
			    dold->zsl_pobytes;
			dres->zsl_hrtime = dnew->zsl_hrtime -
			    dold->zsl_hrtime;

			/* add to link total */
			dres->zsl_total_rbytes += dres->zsl_rbytes;
			dres->zsl_total_obytes += dres->zsl_obytes;
			dres->zsl_total_prbytes += dres->zsl_prbytes;
			dres->zsl_total_pobytes += dres->zsl_pobytes;

			if (zs_update_link_zone(dres, dres, B_FALSE) != 0)
				return (-1);
		} else
			zs_link_add_usage(dres, dold, dnew, func);

		if (zs_usage_compute_vlink_usage(dres, dold, dnew, func)
		    != 0)
			return (-1);
		dnew = list_next(&unew->zsu_datalink_list, dnew);
		dold = list_next(&uold->zsu_datalink_list, dold);
	}

	/* Add in any remaining links in the new interval */
	while (dnew != NULL) {
		if (ures != unew) {
			dres = (struct zs_datalink *)calloc(1,
			    sizeof (struct zs_datalink));
			if (dres == NULL)
				return (-1);
			*dres = *dnew;
			list_create(&dres->zsl_vlink_list,
			    sizeof (struct zs_datalink),
			    offsetof(struct zs_datalink, zsl_next));
			list_create(&dres->zsl_zone_list,
			    sizeof (struct zs_link_zone),
			    offsetof(struct zs_link_zone, zlz_next));
			/*
			 * increment interval count for up phys link
			 * or etherstub
			 */
			if (func == ZS_COMPUTE_USAGE_AVERAGE &&
			    (strcmp(dres->zsl_state, "up") == 0 ||
			    strcmp(dres->zsl_state, "n/a") == 0))
				dres->zsl_intervals++;
			list_insert_tail(&ures->zsu_datalink_list, dres);
		}
		dres->zsl_total_rbytes += dnew->zsl_rbytes;
		dres->zsl_total_obytes += dnew->zsl_obytes;
		dres->zsl_total_prbytes += dnew->zsl_prbytes;
		dres->zsl_total_pobytes += dnew->zsl_pobytes;

		if (zs_usage_compute_vlink_usage(dres, NULL, dnew, func)
		    != 0)
			return (-1);

		if (zs_update_link_zone(dres, dres, B_FALSE) != 0)
			return (-1);
		dnew = list_next(&unew->zsu_datalink_list, dnew);
	}

	return (0);
}

char *
zs_zone_name(struct zs_zone *zone)
{
	return (zone->zsz_name);
}

static zoneid_t
zs_zone_id(struct zs_zone *zone)
{
	return (zone->zsz_id);
}

static uint_t
zs_zone_iptype(struct zs_zone *zone)
{
	return (zone->zsz_iptype);
}

static uint_t
zs_zone_cputype(struct zs_zone *zone)
{
	return (zone->zsz_cputype);
}

char *
zs_zone_poolname(struct zs_zone *zone)
{
	return (zone->zsz_pool);
}

char *
zs_zone_psetname(struct zs_zone *zone)
{
	return (zone->zsz_pset);
}

static uint_t
zs_zone_schedulers(struct zs_zone *zone)
{
	return (zone->zsz_scheds);
}

static uint_t
zs_zone_default_sched(struct zs_zone *zone)
{
	return (zone->zsz_default_sched);
}

static uint64_t
zs_ts_used_scale(timestruc_t *total, timestruc_t *used, uint64_t scale,
    boolean_t cap_at_100)
{
	double dtotal, dused, pct, dscale;

	/* If no time yet, treat as zero */
	if (total->tv_sec == 0 && total->tv_nsec == 0)
		return (0);

	dtotal = (double)total->tv_sec +
	    ((double)total->tv_nsec / (double)NANOSEC);
	dused = (double)used->tv_sec +
	    ((double)used->tv_nsec / (double)NANOSEC);

	dscale = (double)scale;
	pct = dused / dtotal * dscale;
	if (cap_at_100 && pct > dscale)
		pct = dscale;

	return ((uint_t)pct);
}

/*
 * Convert total and used time into percent used.
 */
static uint_t
zs_ts_used_pct(timestruc_t *total, timestruc_t *used, boolean_t cap_at_100)
{
	uint_t res;

	res = (uint_t)zs_ts_used_scale(total, used, ZSD_PCT_INT, cap_at_100);
	return (res);
}

/*
 * Convert total and used time, plus number of cpus, into number of cpus
 * used, where 100 equals 1 cpu used.
 */
static uint64_t
zs_ts_used_cpus(timestruc_t *total, timestruc_t *used, uint_t ncpus,
    boolean_t cap_at_100)
{
	return (zs_ts_used_scale(total, used, ncpus * ZSD_ONE_CPU, cap_at_100));
}

static uint64_t
zs_zone_cpu_shares(struct zs_zone *zone)
{
	/* No processes found in FSS */
	if ((zone->zsz_scheds & ZS_SCHED_FSS) == 0)
		return (ZS_LIMIT_NONE);

	return (zone->zsz_cpu_shares);
}

static uint64_t
zs_zone_cpu_cap(struct zs_zone *zone)
{
	return (zone->zsz_cpu_cap);
}

static uint64_t
zs_zone_cpu_cap_used(struct zs_zone *zone)
{
	if (zone->zsz_cpu_cap == ZS_LIMIT_NONE)
		return (ZS_LIMIT_NONE);

	return (zs_ts_used_cpus(&zone->zsz_cap_time, &zone->zsz_cpu_usage,
	    zone->zsz_cpus_online, B_TRUE));
}

static uint64_t
zs_zone_cpu_shares_used(struct zs_zone *zone)
{
	if (zone->zsz_cpu_shares == ZS_LIMIT_NONE)
		return (ZS_LIMIT_NONE);

	if (zone->zsz_cpu_shares == ZS_SHARES_UNLIMITED)
		return (ZS_LIMIT_NONE);

	if ((zone->zsz_scheds & ZS_SCHED_FSS) == 0)
		return (ZS_LIMIT_NONE);

	return (zs_ts_used_scale(&zone->zsz_share_time, &zone->zsz_cpu_usage,
	    zone->zsz_cpu_shares, B_FALSE));
}

static void
zs_zone_cpu_cap_time(struct zs_zone *zone, timestruc_t *ts)
{
	*ts = zone->zsz_cap_time;
}

static void
zs_zone_cpu_share_time(struct zs_zone *zone, timestruc_t *ts)
{
	*ts = zone->zsz_share_time;
}

static void
zs_zone_cpu_cap_time_used(struct zs_zone *zone, timestruc_t *ts)
{
	*ts = zone->zsz_cpu_usage;
}

static void
zs_zone_cpu_share_time_used(struct zs_zone *zone, timestruc_t *ts)
{
	*ts = zone->zsz_cpu_usage;
}


static uint64_t
zs_uint64_used_scale(uint64_t total, uint64_t used, uint64_t scale,
    boolean_t cap_at_100)
{
	double dtotal, dused, pct, dscale;

	/* If no time yet, treat as zero */
	if (total == 0)
		return (0);

	dtotal = (double)total;
	dused = (double)used;

	dscale = (double)scale;
	pct = dused / dtotal * dscale;
	if (cap_at_100 && pct > dscale)
		pct = dscale;

	return ((uint64_t)pct);
}

/*
 * Convert a total and used value into a percent used.
 */
static uint_t
zs_uint64_used_pct(uint64_t total, uint64_t used, boolean_t cap_at_100)
{
	uint_t res;

	res = (uint_t)zs_uint64_used_scale(total, used, ZSD_PCT_INT,
	    cap_at_100);
	return (res);
}

static uint_t
zs_zone_cpu_cap_pct(struct zs_zone *zone)
{
	if (zone->zsz_cpu_cap == ZS_LIMIT_NONE)
		return (ZS_PCT_NONE);

	return (zs_ts_used_pct(&zone->zsz_cap_time, &zone->zsz_cpu_usage,
	    B_TRUE));
}

static uint_t
zs_zone_cpu_shares_pct(struct zs_zone *zone)
{
	if (zone->zsz_cpu_shares == ZS_LIMIT_NONE)
		return (ZS_PCT_NONE);

	if (zone->zsz_cpu_shares == ZS_SHARES_UNLIMITED)
		return (ZS_PCT_NONE);

	if ((zone->zsz_scheds & ZS_SCHED_FSS) == 0)
		return (ZS_PCT_NONE);

	return (zs_ts_used_pct(&zone->zsz_share_time, &zone->zsz_cpu_usage,
	    B_FALSE));
}

static uint64_t
zs_zone_physical_memory_cap(struct zs_zone *zone)
{
	return (zone->zsz_ram_cap);
}

static uint64_t
zs_zone_virtual_memory_cap(struct zs_zone *zone)
{
	return (zone->zsz_vm_cap);
}

static uint64_t
zs_zone_locked_memory_cap(struct zs_zone *zone)
{
	return (zone->zsz_locked_cap);
}

static uint64_t
zs_zone_physical_memory_cap_used(struct zs_zone *zone)
{
	if (zone->zsz_ram_cap == ZS_LIMIT_NONE)
		return (ZS_LIMIT_NONE);

	return (zone->zsz_usage_ram);
}

static uint64_t
zs_zone_virtual_memory_cap_used(struct zs_zone *zone)
{
	if (zone->zsz_vm_cap == ZS_LIMIT_NONE)
		return (ZS_LIMIT_NONE);

	return (zone->zsz_usage_vm);
}

static uint64_t
zs_zone_locked_memory_cap_used(struct zs_zone *zone)
{
	if (zone->zsz_locked_cap == ZS_LIMIT_NONE)
		return (ZS_LIMIT_NONE);

	return (zone->zsz_usage_locked);
}

char *
zs_pset_name(struct zs_pset *pset)
{
	return (pset->zsp_name);
}

static psetid_t
zs_pset_id(struct zs_pset *pset)
{
	return (pset->zsp_id);
}

static uint64_t
zs_pset_size(struct zs_pset *pset)
{
	return (pset->zsp_size);
}

static uint64_t
zs_pset_online(struct zs_pset *pset)
{
	return (pset->zsp_online);
}

uint64_t
zs_pset_min(struct zs_pset *pset)
{
	return (pset->zsp_min);
}

uint64_t
zs_pset_max(struct zs_pset *pset)
{
	return (pset->zsp_max);
}

static uint_t
zs_pset_schedulers(struct zs_pset *pset)
{
	return (pset->zsp_scheds);
}

static uint_t
zs_pset_zone_schedulers(struct zs_pset_zone *pz)
{
	return (pz->zspz_scheds);
}

static double
zs_pset_load(struct zs_pset *pset, int load)
{
	return (pset->zsp_load_avg[load]);
}

static uint64_t
zs_pset_cpu_shares(struct zs_pset *pset)
{
	if (!(pset->zsp_scheds & ZS_SCHED_FSS))
		return (ZS_LIMIT_NONE);

	return (pset->zsp_cpu_shares);
}

static uint64_t
zs_pset_zone_cpu_shares(struct zs_pset_zone *pz)
{
	if (!(pz->zspz_scheds & ZS_SCHED_FSS))
		return (ZS_LIMIT_NONE);

	return (pz->zspz_cpu_shares);
}

static uint_t
zs_pset_cputype(struct zs_pset *pset)
{
	return (pset->zsp_cputype);
}

static void
zs_pset_usage_all(struct zs_pset *pset, timestruc_t *ts)
{
	timestruc_t tot;

	tot = pset->zsp_usage_kern;
	TIMESTRUC_ADD_TIMESTRUC(tot, pset->zsp_usage_zones);
	*ts = tot;
}

static void
zs_pset_usage_idle(struct zs_pset *pset, timestruc_t *ts)
{
	timestruc_t tot, time, idle;

	tot = pset->zsp_usage_kern;
	TIMESTRUC_ADD_TIMESTRUC(tot, pset->zsp_usage_zones);
	time = pset->zsp_total_time;
	TIMESTRUC_DELTA(idle, time, tot);
	*ts = idle;
}

static void
zs_pset_usage_kernel(struct zs_pset *pset, timestruc_t *ts)
{
	*ts = pset->zsp_usage_kern;
}

static void
zs_pset_usage_zones(struct zs_pset *pset, timestruc_t *ts)
{
	*ts = pset->zsp_usage_zones;
}

static uint_t
zs_pset_usage_all_pct(struct zs_pset *pset)
{
	timestruc_t tot;

	tot = pset->zsp_usage_kern;
	TIMESTRUC_ADD_TIMESTRUC(tot, pset->zsp_usage_zones);

	return (zs_ts_used_pct(&pset->zsp_total_time, &tot, B_TRUE));
}

static uint_t
zs_pset_usage_idle_pct(struct zs_pset *pset)
{
	timestruc_t tot, idle;

	tot = pset->zsp_usage_kern;
	TIMESTRUC_ADD_TIMESTRUC(tot, pset->zsp_usage_zones);
	TIMESTRUC_DELTA(idle, pset->zsp_total_time, tot);

	return (zs_ts_used_pct(&pset->zsp_total_time, &idle, B_TRUE));
}

static uint_t
zs_pset_usage_kernel_pct(struct zs_pset *pset)
{
	return (zs_ts_used_pct(&pset->zsp_total_time, &pset->zsp_usage_kern,
	    B_TRUE));
}

static uint_t
zs_pset_usage_zones_pct(struct zs_pset *pset)
{
	return (zs_ts_used_pct(&pset->zsp_total_time, &pset->zsp_usage_zones,
	    B_TRUE));
}

static uint_t
zs_pset_usage_all_cpus(struct zs_pset *pset)
{
	timestruc_t tot;

	tot = pset->zsp_usage_kern;
	TIMESTRUC_ADD_TIMESTRUC(tot, pset->zsp_usage_zones);
	return (zs_ts_used_cpus(&pset->zsp_total_time, &tot, pset->zsp_online,
	    B_TRUE));
}

static uint_t
zs_pset_usage_idle_cpus(struct zs_pset *pset)
{
	timestruc_t tot, idle;

	tot = pset->zsp_usage_kern;
	TIMESTRUC_ADD_TIMESTRUC(tot, pset->zsp_usage_zones);
	TIMESTRUC_DELTA(idle, pset->zsp_total_time, tot);

	return (zs_ts_used_cpus(&pset->zsp_total_time, &tot, pset->zsp_online,
	    B_TRUE));
}

static uint_t
zs_pset_usage_kernel_cpus(struct zs_pset *pset)
{
	return (zs_ts_used_cpus(&pset->zsp_total_time, &pset->zsp_usage_kern,
	    pset->zsp_online, B_TRUE));
}

static uint64_t
zs_pset_usage_zones_cpus(struct zs_pset *pset)
{
	return (zs_ts_used_cpus(&pset->zsp_total_time, &pset->zsp_usage_zones,
	    pset->zsp_online, B_TRUE));
}

static void
zs_pset_zone_usage_time(struct zs_pset_zone *pz, timestruc_t *t)
{
	*t = pz->zspz_cpu_usage;
}

static uint_t
zs_pset_zone_usage_cpus(struct zs_pset_zone *pz)
{
	return (zs_ts_used_cpus(&pz->zspz_pset->zsp_total_time,
	    &pz->zspz_cpu_usage, pz->zspz_pset->zsp_online, B_TRUE));
}

static uint_t
zs_pset_zone_usage_pct_pset(struct zs_pset_zone *pz)
{
	return (zs_ts_used_pct(&pz->zspz_pset->zsp_total_time,
	    &pz->zspz_cpu_usage, B_TRUE));
}

static uint64_t
zs_pset_zone_cpu_cap(struct zs_pset_zone *pz)
{
	return (pz->zspz_zone->zsz_cpu_cap);
}

static uint_t
zs_pset_zone_usage_pct_cpu_cap(struct zs_pset_zone *pz)
{
	struct zs_zone *zone = pz->zspz_zone;

	if (zone->zsz_cpu_cap == ZS_LIMIT_NONE) {
		return (ZS_PCT_NONE);
	}
	return (zs_ts_used_pct(&zone->zsz_cap_time,
	    &pz->zspz_cpu_usage, B_TRUE));
}

/*
 * Return the fraction of total shares for a pset allocated to the zone.
 */
static uint_t
zs_pset_zone_usage_pct_pset_shares(struct zs_pset_zone *pz)
{
	struct zs_pset *pset = pz->zspz_pset;

	if (!(pz->zspz_scheds & ZS_SCHED_FSS))
		return (ZS_PCT_NONE);

	if (pz->zspz_cpu_shares == ZS_LIMIT_NONE)
		return (ZS_PCT_NONE);

	if (pz->zspz_cpu_shares == ZS_SHARES_UNLIMITED)
		return (ZS_PCT_NONE);

	if (pz->zspz_pset->zsp_cpu_shares == 0)
		return (0);

	if (pz->zspz_cpu_shares == 0)
		return (0);

	return (zs_uint64_used_pct(pset->zsp_cpu_shares, pz->zspz_cpu_shares,
	    B_TRUE));
}

/*
 * Of a zones shares, what percent of cpu time is it using.  For instance,
 * if a zone has 50% of shares, and is using 50% of the cpu time, then it is
 * using 100% of its share.
 */
static uint_t
zs_pset_zone_usage_pct_cpu_shares(struct zs_pset_zone *pz)
{
	timestruc_t tot, time;
	double sharefactor;
	double total;
	double used;
	double pct;

	if (!(pz->zspz_scheds & ZS_SCHED_FSS))
		return (ZS_PCT_NONE);

	if (pz->zspz_cpu_shares == ZS_LIMIT_NONE)
		return (ZS_PCT_NONE);

	if (pz->zspz_cpu_shares == ZS_SHARES_UNLIMITED)
		return (ZS_PCT_NONE);

	if (pz->zspz_cpu_shares == 0)
		return (ZS_PCT_NONE);

	sharefactor = (double)zs_pset_zone_usage_pct_pset_shares(pz);

	/* Common scaling function won't do sharefactor. */
	time = pz->zspz_pset->zsp_total_time;
	tot = pz->zspz_cpu_usage;

	total = (double)time.tv_sec +
	    ((double)time.tv_nsec / (double)NANOSEC);
	total = total * (sharefactor / ZSD_PCT_DOUBLE);
	used = (double)tot.tv_sec +
	    ((double)tot.tv_nsec / (double)NANOSEC);

	pct = used / total * ZSD_PCT_DOUBLE;
	/* Allow percent of share used to exceed 100% */
	return ((uint_t)pct);
}

static void
zs_cpu_total_time(struct zs_usage *usage, timestruc_t *ts)
{
	*ts = usage->zsu_system->zss_cpu_total_time;
}

static void
zs_cpu_usage_all(struct zs_usage *usage, timestruc_t *ts)
{
	timestruc_t tot;

	tot.tv_sec = 0;
	tot.tv_nsec = 0;
	TIMESTRUC_ADD_TIMESTRUC(tot, usage->zsu_system->zss_cpu_usage_kern);
	TIMESTRUC_ADD_TIMESTRUC(tot, usage->zsu_system->zss_cpu_usage_zones);
	*ts = tot;
}

static void
zs_cpu_usage_idle(struct zs_usage *usage, timestruc_t *ts)
{
	timestruc_t tot, time, idle;

	tot.tv_sec = 0;
	tot.tv_nsec = 0;
	tot = usage->zsu_system->zss_cpu_usage_kern;
	TIMESTRUC_ADD_TIMESTRUC(tot, usage->zsu_system->zss_cpu_usage_zones);
	time = usage->zsu_system->zss_cpu_total_time;
	TIMESTRUC_DELTA(idle, time, tot);
	*ts = idle;
}

static uint_t
zs_cpu_usage_all_pct(struct zs_usage *usage)
{
	timestruc_t tot;

	tot = usage->zsu_system->zss_cpu_usage_kern;
	TIMESTRUC_ADD_TIMESTRUC(tot, usage->zsu_system->zss_cpu_usage_zones);

	return (zs_ts_used_pct(&usage->zsu_system->zss_cpu_total_time,
	    &tot, B_TRUE));
}


static uint_t
zs_cpu_usage_idle_pct(struct zs_usage *usage)
{
	timestruc_t tot, idle;

	tot = usage->zsu_system->zss_cpu_usage_kern;
	TIMESTRUC_ADD_TIMESTRUC(tot, usage->zsu_system->zss_cpu_usage_zones);
	TIMESTRUC_DELTA(idle, usage->zsu_system->zss_cpu_total_time, tot);

	return (zs_ts_used_pct(&usage->zsu_system->zss_cpu_total_time,
	    &idle, B_TRUE));
}

static void
zs_cpu_usage_kernel(struct zs_usage *usage, timestruc_t *ts)
{
	*ts = usage->zsu_system->zss_cpu_usage_kern;
}

static uint_t
zs_cpu_usage_kernel_pct(struct zs_usage *usage)
{
	return (zs_ts_used_pct(&usage->zsu_system->zss_cpu_total_time,
	    &usage->zsu_system->zss_cpu_usage_kern, B_TRUE));
}

static void
zs_cpu_usage_zones(struct zs_usage *usage, timestruc_t *ts)
{
	*ts = usage->zsu_system->zss_cpu_usage_zones;
}


static uint_t
zs_cpu_usage_zones_pct(struct zs_usage *usage)
{
	return (zs_ts_used_pct(&usage->zsu_system->zss_cpu_total_time,
	    &usage->zsu_system->zss_cpu_usage_zones, B_TRUE));
}


static void
zs_cpu_usage_zone(struct zs_zone *zone, timestruc_t *ts)
{
	*ts = zone->zsz_cpu_usage;
}

static uint64_t
zs_cpu_total_cpu(struct zs_usage *usage)
{
	return (usage->zsu_system->zss_ncpus_online * ZSD_ONE_CPU);
}

static uint64_t
zs_cpu_usage_all_cpu(struct zs_usage *usage)
{
	timestruc_t tot;

	tot = usage->zsu_system->zss_cpu_usage_kern;
	TIMESTRUC_ADD_TIMESTRUC(tot, usage->zsu_system->zss_cpu_usage_zones);

	return (zs_ts_used_cpus(&usage->zsu_system->zss_cpu_total_time,
	    &tot, usage->zsu_system->zss_ncpus_online, B_TRUE));
}

static uint64_t
zs_cpu_usage_idle_cpu(struct zs_usage *usage)
{
	timestruc_t tot, idle;

	tot = usage->zsu_system->zss_cpu_usage_kern;
	TIMESTRUC_ADD_TIMESTRUC(tot, usage->zsu_system->zss_cpu_usage_zones);
	TIMESTRUC_DELTA(idle, usage->zsu_system->zss_cpu_total_time, tot);

	return (zs_ts_used_cpus(&usage->zsu_system->zss_cpu_total_time,
	    &idle, usage->zsu_system->zss_ncpus_online, B_TRUE));
}

static uint64_t
zs_cpu_usage_kernel_cpu(struct zs_usage *usage)
{
	return (zs_ts_used_cpus(&usage->zsu_system->zss_cpu_total_time,
	    &usage->zsu_system->zss_cpu_usage_kern,
	    usage->zsu_system->zss_ncpus_online, B_TRUE));
}

static uint64_t
zs_cpu_usage_zones_cpu(struct zs_usage *usage)
{
	return (zs_ts_used_cpus(&usage->zsu_system->zss_cpu_total_time,
	    &usage->zsu_system->zss_cpu_usage_kern,
	    usage->zsu_system->zss_ncpus_online, B_TRUE));
}

static uint64_t
zs_cpu_usage_zone_cpu(struct zs_zone *zone)
{
	return (zs_ts_used_cpus(&zone->zsz_pset_time, &zone->zsz_cpu_usage,
	    zone->zsz_cpus_online, B_TRUE));
}

static uint_t
zs_cpu_usage_zone_pct(struct zs_zone *zone)
{
	return (zs_ts_used_pct(&zone->zsz_pset_time, &zone->zsz_cpu_usage,
	    B_TRUE));
}

static uint64_t
zs_physical_memory_total(struct zs_usage *usage)
{
	return (usage->zsu_system->zss_ram_total);
}


static uint64_t
zs_physical_memory_usage_all(struct zs_usage *usage)
{
	return (usage->zsu_system->zss_ram_kern +
	    usage->zsu_system->zss_ram_zones);
}

static uint_t
zs_physical_memory_usage_all_pct(struct zs_usage *usage)
{
	struct zs_system *system = usage->zsu_system;

	return (zs_uint64_used_pct(system->zss_ram_total,
	    (system->zss_ram_kern + system->zss_ram_zones), B_TRUE));
}

static uint64_t
zs_physical_memory_usage_free(struct zs_usage *usage)
{
	return (usage->zsu_system->zss_ram_total -
	    (usage->zsu_system->zss_ram_kern +
	    usage->zsu_system->zss_ram_zones));
}

static uint_t
zs_physical_memory_usage_free_pct(struct zs_usage *usage)
{
	return (ZSD_PCT_INT - zs_physical_memory_usage_all_pct(usage));
}

static uint64_t
zs_physical_memory_usage_kernel(struct zs_usage *usage)
{
	return (usage->zsu_system->zss_ram_kern);
}

static uint_t
zs_physical_memory_usage_kernel_pct(struct zs_usage *usage)
{
	struct zs_system *system = usage->zsu_system;

	return (zs_uint64_used_pct(system->zss_ram_total,
	    system->zss_ram_kern, B_TRUE));
}

static uint64_t
zs_physical_memory_usage_zones(struct zs_usage *usage)
{
	return (usage->zsu_system->zss_ram_zones);
}

static uint_t
zs_physical_memory_usage_zones_pct(struct zs_usage *usage)
{
	struct zs_system *system = usage->zsu_system;

	return (zs_uint64_used_pct(system->zss_ram_total,
	    system->zss_ram_zones, B_TRUE));
}

static uint64_t
zs_physical_memory_usage_zone(struct zs_zone *zone)
{
	return (zone->zsz_usage_ram);
}

static uint_t
zs_physical_memory_usage_zone_pct(struct zs_zone *zone)
{
	struct zs_system *system = zone->zsz_system;

	return (zs_uint64_used_pct(system->zss_ram_total,
	    zone->zsz_usage_ram, B_TRUE));
}

static uint_t
zs_zone_physical_memory_cap_pct(struct zs_zone *zone)
{
	if (zone->zsz_ram_cap == ZS_LIMIT_NONE)
		return (ZS_PCT_NONE);

	if (zone->zsz_ram_cap == 0) {
		return (0);
	}

	/* Allow ram cap to exeed 100% */
	return (zs_uint64_used_pct(zone->zsz_ram_cap,
	    zone->zsz_usage_ram, B_FALSE));
}
static uint64_t
zs_virtual_memory_total(struct zs_usage *usage)
{
	return (usage->zsu_system->zss_vm_total);
}

static uint64_t
zs_virtual_memory_usage_all(struct zs_usage *usage)
{
	return (usage->zsu_system->zss_vm_kern +
	    usage->zsu_system->zss_vm_zones);
}
static uint64_t
zs_virtual_memory_usage_free(struct zs_usage *usage)
{
	return (usage->zsu_system->zss_vm_total -
	    (usage->zsu_system->zss_vm_kern +
	    usage->zsu_system->zss_vm_zones));
}
static uint_t
zs_virtual_memory_usage_all_pct(struct zs_usage *usage)
{
	struct zs_system *system = usage->zsu_system;

	return (zs_uint64_used_pct(system->zss_vm_total,
	    (system->zss_vm_kern + system->zss_vm_zones), B_TRUE));

}

static uint_t
zs_virtual_memory_usage_free_pct(struct zs_usage *usage)
{
	return (ZSD_PCT_INT - zs_virtual_memory_usage_all_pct(usage));

}
static uint64_t
zs_virtual_memory_usage_kernel(struct zs_usage *usage)
{
	return (usage->zsu_system->zss_vm_kern);
}

static uint_t
zs_virtual_memory_usage_kernel_pct(struct zs_usage *usage)
{
	struct zs_system *system = usage->zsu_system;

	return (zs_uint64_used_pct(system->zss_vm_total,
	    system->zss_vm_kern, B_TRUE));
}

static uint64_t
zs_virtual_memory_usage_zones(struct zs_usage *usage)
{
	return (usage->zsu_system->zss_vm_zones);
}

static uint_t
zs_virtual_memory_usage_zones_pct(struct zs_usage *usage)
{
	struct zs_system *system = usage->zsu_system;

	return (zs_uint64_used_pct(system->zss_vm_total,
	    system->zss_vm_zones, B_TRUE));
}

static uint64_t
zs_virtual_memory_usage_zone(struct zs_zone *zone)
{
	return (zone->zsz_usage_vm);
}

static uint_t
zs_virtual_memory_usage_zone_pct(struct zs_zone *zone)
{
	struct zs_system *system = zone->zsz_system;

	return (zs_uint64_used_pct(system->zss_vm_total,
	    zone->zsz_usage_vm, B_TRUE));

}

static uint_t
zs_zone_virtual_memory_cap_pct(struct zs_zone *zone)
{
	if (zone->zsz_vm_cap == ZS_LIMIT_NONE)
		return (ZS_PCT_NONE);

	if (zone->zsz_vm_cap == 0)
		return (0);

	return (zs_uint64_used_pct(zone->zsz_vm_cap,
	    zone->zsz_usage_vm, B_TRUE));
}

static uint64_t
zs_locked_memory_total(struct zs_usage *usage)
{
	return (usage->zsu_system->zss_ram_total);
}

static uint64_t
zs_locked_memory_usage_all(struct zs_usage *usage)
{
	return (usage->zsu_system->zss_locked_kern +
	    usage->zsu_system->zss_locked_zones);
}
static uint64_t
zs_locked_memory_usage_free(struct zs_usage *usage)
{
	return (usage->zsu_system->zss_ram_total -
	    (usage->zsu_system->zss_locked_kern +
	    usage->zsu_system->zss_locked_zones));
}

static uint_t
zs_locked_memory_usage_all_pct(struct zs_usage *usage)
{
	struct zs_system *system = usage->zsu_system;

	return (zs_uint64_used_pct(system->zss_ram_total,
	    (system->zss_locked_kern + system->zss_locked_zones), B_TRUE));
}

static uint_t
zs_locked_memory_usage_free_pct(struct zs_usage *usage)
{
	return (ZSD_PCT_INT - zs_locked_memory_usage_all_pct(usage));

}

static uint64_t
zs_locked_memory_usage_kernel(struct zs_usage *usage)
{
	return (usage->zsu_system->zss_locked_kern);
}

static uint_t
zs_locked_memory_usage_kernel_pct(struct zs_usage *usage)
{
	struct zs_system *system = usage->zsu_system;

	return (zs_uint64_used_pct(system->zss_ram_total,
	    system->zss_locked_kern, B_TRUE));
}

static uint64_t
zs_locked_memory_usage_zones(struct zs_usage *usage)
{
	return (usage->zsu_system->zss_locked_zones);
}

static uint_t
zs_locked_memory_usage_zones_pct(struct zs_usage *usage)
{
	struct zs_system *system = usage->zsu_system;

	return (zs_uint64_used_pct(system->zss_ram_total,
	    system->zss_locked_zones, B_TRUE));
}

static uint64_t
zs_locked_memory_usage_zone(struct zs_zone *zone)
{
	return (zone->zsz_usage_locked);
}

static uint_t
zs_locked_memory_usage_zone_pct(struct zs_zone *zone)
{
	struct zs_system *system = zone->zsz_system;

	return (zs_uint64_used_pct(system->zss_ram_total,
	    zone->zsz_usage_locked, B_TRUE));
}

static uint_t
zs_zone_locked_memory_cap_pct(struct zs_zone *zone)
{
	if (zone->zsz_locked_cap == ZS_LIMIT_NONE)
		return (ZS_PCT_NONE);

	if (zone->zsz_locked_cap == 0)
		return (0);

	return (zs_uint64_used_pct(zone->zsz_locked_cap,
	    zone->zsz_usage_locked, B_TRUE));

}
static uint64_t
zs_disk_swap_total(struct zs_usage *usage)
{
	return (usage->zsu_system->zss_swap_total);
}

static uint64_t
zs_disk_swap_usage_all(struct zs_usage *usage)
{
	return (usage->zsu_system->zss_swap_used);
}

static uint_t
zs_disk_swap_usage_all_pct(struct zs_usage *usage)
{
	return (zs_uint64_used_pct(usage->zsu_system->zss_swap_total,
	    usage->zsu_system->zss_swap_used, B_TRUE));
}

static uint64_t
zs_disk_swap_usage_free(struct zs_usage *usage)
{
	return (usage->zsu_system->zss_swap_total -
	    usage->zsu_system->zss_swap_used);
}

static uint_t
zs_disk_swap_usage_free_pct(struct zs_usage *usage)
{
	return (ZSD_PCT_INT - zs_disk_swap_usage_all_pct(usage));
}

static uint64_t
zs_processes_total(struct zs_usage *usage)
{
	return (usage->zsu_system->zss_processes_max);
}

static uint64_t
zs_lwps_total(struct zs_usage *usage)
{
	return (usage->zsu_system->zss_lwps_max);
}

static uint64_t
zs_shm_total(struct zs_usage *usage)
{
	return (usage->zsu_system->zss_shm_max);
}

static uint64_t
zs_shmids_total(struct zs_usage *usage)
{
	return (usage->zsu_system->zss_shmids_max);
}

static uint64_t
zs_semids_total(struct zs_usage *usage)
{
	return (usage->zsu_system->zss_semids_max);
}

static uint64_t
zs_msgids_total(struct zs_usage *usage)
{
	return (usage->zsu_system->zss_msgids_max);
}

static uint64_t
zs_lofi_total(struct zs_usage *usage)
{
	return (usage->zsu_system->zss_lofi_max);
}

static uint64_t
zs_net_total(struct zs_usage *usage)
{
	return (usage->zsu_system->zss_net_speed);
}

static uint64_t
zs_net_rbytes(struct zs_usage *usage)
{
	return (usage->zsu_system->zss_net_rbytes);
}

static uint64_t
zs_net_obytes(struct zs_usage *usage)
{
	return (usage->zsu_system->zss_net_obytes);
}

static uint64_t
zs_net_prbytes(struct zs_usage *usage)
{
	return (usage->zsu_system->zss_net_prbytes);
}

static uint64_t
zs_net_pobytes(struct zs_usage *usage)
{
	return (usage->zsu_system->zss_net_pobytes);
}

static uint64_t
zs_net_bytes(struct zs_usage *usage)
{
	return (usage->zsu_system->zss_net_bytes);
}

static uint64_t
zs_net_pbytes(struct zs_usage *usage)
{
	return (usage->zsu_system->zss_net_pbytes);
}

static uint64_t
zs_processes_usage_all(struct zs_usage *usage)
{
	return (usage->zsu_system->zss_processes);
}

static uint64_t
zs_lwps_usage_all(struct zs_usage *usage)
{
	return (usage->zsu_system->zss_lwps);
}

static uint64_t
zs_shm_usage_all(struct zs_usage *usage)
{
	return (usage->zsu_system->zss_shm);
}

static uint64_t
zs_shmids_usage_all(struct zs_usage *usage)
{
	return (usage->zsu_system->zss_shmids);
}

static uint64_t
zs_semids_usage_all(struct zs_usage *usage)
{
	return (usage->zsu_system->zss_semids);
}

static uint64_t
zs_msgids_usage_all(struct zs_usage *usage)
{
	return (usage->zsu_system->zss_msgids);
}

static uint64_t
zs_lofi_usage_all(struct zs_usage *usage)
{
	return (usage->zsu_system->zss_lofi);
}
static uint64_t
zs_processes_usage_all_pct(struct zs_usage *usage)
{
	struct zs_system *system = usage->zsu_system;

	return (zs_uint64_used_pct(system->zss_processes_max,
	    system->zss_processes, B_TRUE));
}

static uint_t
zs_lwps_usage_all_pct(struct zs_usage *usage)
{
	struct zs_system *system = usage->zsu_system;

	return (zs_uint64_used_pct(system->zss_lwps_max,
	    system->zss_lwps, B_TRUE));
}

static uint_t
zs_shm_usage_all_pct(struct zs_usage *usage)
{
	struct zs_system *system = usage->zsu_system;

	return (zs_uint64_used_pct(system->zss_shm_max,
	    system->zss_shm, B_TRUE));
}

static uint_t
zs_shmids_usage_all_pct(struct zs_usage *usage)
{
	struct zs_system *system = usage->zsu_system;

	return (zs_uint64_used_pct(system->zss_shmids_max,
	    system->zss_shmids, B_TRUE));
}

static uint64_t
zs_semids_usage_all_pct(struct zs_usage *usage)
{
	struct zs_system *system = usage->zsu_system;

	return (zs_uint64_used_pct(system->zss_semids_max,
	    system->zss_semids, B_TRUE));
}

static uint64_t
zs_msgids_usage_all_pct(struct zs_usage *usage)
{
	struct zs_system *system = usage->zsu_system;

	return (zs_uint64_used_pct(system->zss_msgids_max,
	    system->zss_msgids, B_TRUE));
}

static uint64_t
zs_lofi_usage_all_pct(struct zs_usage *usage)
{
	struct zs_system *system = usage->zsu_system;

	return (zs_uint64_used_pct(system->zss_lofi_max,
	    system->zss_lofi, B_TRUE));
}

static uint64_t
zs_pbytes_usage_all_pct(struct zs_usage *usage)
{
	struct zs_system *system = usage->zsu_system;

	return (zs_uint64_used_pct(system->zss_net_speed,
	    system->zss_net_pbytes, B_TRUE));
}

static uint64_t
zs_processes_usage_zone(struct zs_zone *zone)
{
	return (zone->zsz_processes);
}

static uint64_t
zs_lwps_usage_zone(struct zs_zone *zone)
{
	return (zone->zsz_lwps);
}

static uint64_t
zs_shm_usage_zone(struct zs_zone *zone)
{
	return (zone->zsz_shm);
}

static uint64_t
zs_shmids_usage_zone(struct zs_zone *zone)
{
	return (zone->zsz_shmids);
}

static uint64_t
zs_semids_usage_zone(struct zs_zone *zone)
{
	return (zone->zsz_semids);
}

static uint64_t
zs_msgids_usage_zone(struct zs_zone *zone)
{
	return (zone->zsz_msgids);
}

static uint64_t
zs_lofi_usage_zone(struct zs_zone *zone)
{
	return (zone->zsz_lofi);
}

static uint64_t
zs_pbytes_usage_zone(struct zs_zone *zone)
{
	return (zone->zsz_tot_pbytes);
}

static uint_t
zs_processes_usage_zone_pct(struct zs_zone *zone)
{
	struct zs_system *system = zone->zsz_system;

	return (zs_uint64_used_pct(system->zss_processes_max,
	    zone->zsz_processes, B_TRUE));
}

static uint_t
zs_lwps_usage_zone_pct(struct zs_zone *zone)
{
	struct zs_system *system = zone->zsz_system;

	return (zs_uint64_used_pct(system->zss_lwps_max,
	    zone->zsz_lwps, B_TRUE));
}

static uint_t
zs_shm_usage_zone_pct(struct zs_zone *zone)
{
	struct zs_system *system = zone->zsz_system;

	return (zs_uint64_used_pct(system->zss_shm_max,
	    zone->zsz_shm, B_TRUE));
}

static uint_t
zs_shmids_usage_zone_pct(struct zs_zone *zone)
{
	struct zs_system *system = zone->zsz_system;

	return (zs_uint64_used_pct(system->zss_shmids_max,
	    zone->zsz_shmids, B_TRUE));
}

static uint_t
zs_semids_usage_zone_pct(struct zs_zone *zone)
{
	struct zs_system *system = zone->zsz_system;

	return (zs_uint64_used_pct(system->zss_semids_max,
	    zone->zsz_semids, B_TRUE));
}

static uint_t
zs_msgids_usage_zone_pct(struct zs_zone *zone)
{
	struct zs_system *system = zone->zsz_system;

	return (zs_uint64_used_pct(system->zss_msgids_max,
	    zone->zsz_msgids, B_TRUE));
}

static uint_t
zs_lofi_usage_zone_pct(struct zs_zone *zone)
{
	struct zs_system *system = zone->zsz_system;

	return (zs_uint64_used_pct(system->zss_lofi_max,
	    zone->zsz_lofi, B_TRUE));
}

static uint_t
zs_pbytes_usage_zone_pct(struct zs_zone *zone)
{
	struct zs_system *system = zone->zsz_system;

	return (zs_uint64_used_pct(system->zss_net_speed,
	    zone->zsz_tot_pbytes, B_TRUE));
}

static uint_t
zs_processes_zone_cap_pct(struct zs_zone *zone)
{
	if (zone->zsz_processes_cap == ZS_LIMIT_NONE)
		return (ZS_PCT_NONE);

	if (zone->zsz_processes_cap == 0)
		return (0);

	return (zs_uint64_used_pct(zone->zsz_processes_cap,
	    zone->zsz_processes, B_TRUE));
}

static uint_t
zs_lwps_zone_cap_pct(struct zs_zone *zone)
{
	if (zone->zsz_lwps_cap == ZS_LIMIT_NONE)
		return (ZS_PCT_NONE);

	if (zone->zsz_lwps_cap == 0)
		return (0);

	return (zs_uint64_used_pct(zone->zsz_lwps_cap, zone->zsz_lwps, B_TRUE));
}

static uint_t
zs_shm_zone_cap_pct(struct zs_zone *zone)
{
	if (zone->zsz_shm_cap == ZS_LIMIT_NONE)
		return (ZS_PCT_NONE);

	if (zone->zsz_shm_cap == 0)
		return (0);

	return (zs_uint64_used_pct(zone->zsz_shm_cap, zone->zsz_shm, B_TRUE));
}

static uint_t
zs_shmids_zone_cap_pct(struct zs_zone *zone)
{
	if (zone->zsz_shmids_cap == ZS_LIMIT_NONE)
		return (ZS_PCT_NONE);

	if (zone->zsz_shmids_cap == 0)
		return (0);

	return (zs_uint64_used_pct(zone->zsz_shmids_cap, zone->zsz_shmids,
	    B_TRUE));
}

static uint_t
zs_semids_zone_cap_pct(struct zs_zone *zone)
{
	if (zone->zsz_semids_cap == ZS_LIMIT_NONE)
		return (ZS_PCT_NONE);

	if (zone->zsz_semids_cap == 0)
		return (0);

	return (zs_uint64_used_pct(zone->zsz_semids_cap, zone->zsz_semids,
	    B_TRUE));
}

static uint_t
zs_msgids_zone_cap_pct(struct zs_zone *zone)
{
	if (zone->zsz_msgids_cap == ZS_LIMIT_NONE)
		return (ZS_PCT_NONE);

	if (zone->zsz_msgids_cap == 0)
		return (0);

	return (zs_uint64_used_pct(zone->zsz_msgids_cap, zone->zsz_msgids,
	    B_TRUE));
}

static uint_t
zs_lofi_zone_cap_pct(struct zs_zone *zone)
{
	if (zone->zsz_lofi_cap == ZS_LIMIT_NONE)
		return (ZS_PCT_NONE);

	if (zone->zsz_lofi_cap == 0)
		return (0);

	return (zs_uint64_used_pct(zone->zsz_lofi_cap, zone->zsz_lofi,
	    B_TRUE));
}

/* All funcs above this line should be static */

void
zs_close(zs_ctl_t ctlin)
{
	struct zs_ctl *ctl = (struct zs_ctl *)ctlin;

	(void) close(ctl->zsctl_door);
	zs_usage_free((zs_usage_t)ctl->zsctl_start);
	free(ctl);
}

/*
 * ERRORS
 *
 *	EINTR   signal received, process forked, or zonestatd exited
 *      ESRCH	zonestatd not responding
 */
static zs_usage_t
zs_usage_read_internal(struct zs_ctl *ctl, int init)
{
	int fd = -1;
	uint_t i, j;
	struct zs_usage *usage;
	struct zs_zone *zone = NULL;
	struct zs_pset *pset = NULL;
	struct zs_pset_zone *pz;
	struct zs_datalink *link = NULL;
	struct zs_datalink *vl;
	char *next;
	uint64_t cmd[2];
	door_arg_t params;

	fd = ctl->zsctl_door;
	cmd[0] = ZSD_CMD_READ;
	cmd[1] = ctl->zsctl_gen;
	params.data_ptr = (char *)cmd;
	params.data_size = sizeof (cmd);
	params.desc_ptr = NULL;
	params.desc_num = 0;
	params.rbuf = NULL;
	params.rsize = 0;

	if (door_call(fd, &params) != 0) {
		switch (errno) {
		case EINTR:
		case EAGAIN:
		case ENOMEM:
			break;
		case EMFILE:
		case EOVERFLOW:
			errno = EAGAIN;
			break;
		default:
			errno = EINTR;
		}
		return (NULL);
	}

	if (params.rbuf == NULL) {
		errno = ESRCH;
		return (NULL);
	}
	/* LINTED */
	usage = (struct zs_usage *)params.data_ptr;
	ctl->zsctl_gen = usage->zsu_gen;
	usage->zsu_mmap = B_TRUE;
	usage->zsu_intervals = 0;

	list_create(&usage->zsu_zone_list, sizeof (struct zs_zone),
	    offsetof(struct zs_zone, zsz_next));
	list_create(&usage->zsu_pset_list, sizeof (struct zs_pset),
	    offsetof(struct zs_pset, zsp_next));
	list_create(&usage->zsu_datalink_list, sizeof (struct zs_datalink),
	    offsetof(struct zs_datalink, zsl_next));

	/* Fix up next pointers inside usage_t */
	next = (char *)usage;
	next += sizeof (struct zs_usage);

	/* LINTED */
	usage->zsu_system = (struct zs_system *)next;
	next += sizeof (struct zs_system);

	for (i = 0; i < usage->zsu_nzones; i++) {
		/* LINTED */
		zone = (struct zs_zone *)next;
		list_insert_tail(&usage->zsu_zone_list, zone);
		next += sizeof (struct zs_zone);
		zone->zsz_system = usage->zsu_system;
		zone->zsz_intervals = 0;
	}

	for (i = 0; i < usage->zsu_npsets; i++) {
		/* LINTED */
		pset = (struct zs_pset *)next;
		list_insert_tail(&usage->zsu_pset_list, pset);
		next += sizeof (struct zs_pset);
		list_create(&pset->zsp_usage_list, sizeof (struct zs_pset_zone),
		    offsetof(struct zs_pset_zone, zspz_next));
		for (j = 0; j < pset->zsp_nusage; j++) {
			/* LINTED */
			pz = (struct zs_pset_zone *)next;
			list_insert_tail(&pset->zsp_usage_list, pz);
			next += sizeof (struct zs_pset_zone);
			pz->zspz_pset = pset;
			pz->zspz_zone =
			    zs_lookup_zone_byid(usage, pz->zspz_zoneid);
			assert(pz->zspz_zone != NULL);
			pz->zspz_intervals = 0;
		}
		pset->zsp_intervals = 0;
	}

	for (i = 0; i < usage->zsu_ndatalinks; i++) {
		/* LINTED */
		link = (struct zs_datalink *)next;
		list_insert_tail(&usage->zsu_datalink_list, link);
		next += sizeof (struct zs_datalink);
		list_create(&link->zsl_vlink_list, sizeof (struct zs_datalink),
		    offsetof(struct zs_datalink, zsl_next));
		list_create(&link->zsl_zone_list, sizeof (struct zs_link_zone),
		    offsetof(struct zs_link_zone, zlz_next));
		for (j = 0; j < link->zsl_nclients; j++) {
			/* LINTED */
			vl = (struct zs_datalink *)next;
			list_insert_tail(&link->zsl_vlink_list, vl);
			next += sizeof (struct zs_datalink);
			vl->zsl_intervals = 0;
		}
	}
	if (init)
		return ((zs_usage_t)usage);

	/*
	 * If current usage tracking started after start usage, then
	 * no need to subtract start usage.  This really can't happen,
	 * as zonestatd should never start over while this client is
	 * connected.
	 */
	if (usage->zsu_hrstart > ctl->zsctl_start->zsu_hrtime) {
		return ((zs_usage_t)usage);
	}

	/*
	 * Compute usage relative to first open.  Usage returned by
	 * zonestatd starts at an arbitrary point in the past.
	 *
	 */

	(void) zs_usage_compute((zs_usage_t)usage,
	    (zs_usage_t)ctl->zsctl_start, (zs_usage_t)usage,
	    ZS_COMPUTE_USAGE_INTERVAL);

	return ((zs_usage_t)usage);
}

zs_usage_t
zs_usage_read(zs_ctl_t ctlin)
{
	struct zs_ctl *ctl = (struct zs_ctl *)ctlin;

	return ((zs_usage_t)zs_usage_read_internal(ctl, B_FALSE));
}

/*
 * Open connection to zonestatd.  NULL of failure, with errno set:
 *
 *  EPERM:  Insufficent privilege (no PRIV_PROC_INFO)
 *  ESRCH:  Zones monitoring service not available or responding
 *  ENOTSUP: Incompatiable zones monitoring service version.
 *  EINTR: Server exited or client forked.
 *  ENOMEM: as malloc(3c)
 *  EAGAIN: asl malloc(3c)
 *
 */
zs_ctl_t
zs_open()
{
	struct zs_ctl *ctl;
	int cmd[2];
	int *res;
	int fd;
	door_arg_t params;
	door_desc_t *door;
	int errno_save;

	ctl = calloc(1, sizeof (struct zs_ctl));
	if (ctl == NULL)
		return (NULL);

	fd = zs_connect_zonestatd();
	if (fd < 0) {
		free(ctl);
		errno = ESRCH;
		return (NULL);
	}

	cmd[0] = ZSD_CMD_CONNECT;
	cmd[1] = ZS_VERSION;
	params.data_ptr = (char *)cmd;
	params.data_size = sizeof (cmd);
	params.desc_ptr = NULL;
	params.desc_num = 0;
	params.rbuf = NULL;
	params.rsize = 0;
	if (door_call(fd, &params) != 0) {
		errno_save = errno;
		free(ctl);
		(void) close(fd);
		switch (errno_save) {
		case ENOMEM:
		case EAGAIN:
			break;
		case EINTR:
			errno = EINTR;
			break;
		case EMFILE:
		case EOVERFLOW:
			errno = EAGAIN;
			break;
		default:
			errno = EINTR;
		}
		return (NULL);
	}
	(void) close(fd);
	/* LINTED */
	res = (int *)params.data_ptr;
	if (res[1] == ZSD_STATUS_VERSION_MISMATCH) {
		free(ctl);
		errno = ENOTSUP;
		return (NULL);
	}
	if (res[1] == ZSD_STATUS_PERMISSION) {
		free(ctl);
		errno = EPERM;
		return (NULL);
	}
	if (res[1] != ZSD_STATUS_OK) {
		free(ctl);
		errno = ESRCH;
		return (NULL);
	}

	door = params.desc_ptr;
	if (door == NULL) {
		free(ctl);
		return (NULL);
	}
	ctl->zsctl_door = door->d_data.d_desc.d_descriptor;

	if (params.data_ptr != (char *)cmd)
		(void) munmap(params.data_ptr, params.data_size);


	/*
	 * Get the initial usage from zonestatd.  This creates a
	 * zero-point on which to base future usages returned by
	 * zs_read().
	 */
	ctl->zsctl_start = (struct zs_usage *)
	    zs_usage_read_internal(ctl, B_TRUE);
	if (ctl->zsctl_start == NULL) {
		errno_save = errno;
		(void) close(ctl->zsctl_door);
		free(ctl);
		if (errno_save == EINTR)
			errno = EINTR;
		else
			errno = ESRCH;
		return (NULL);
	}
	return ((zs_ctl_t)ctl);
}

/*
 * Public api for interval diff
 */
zs_usage_t
zs_usage_diff(zs_usage_t uoldin, zs_usage_t unewin)
{
	struct zs_usage *uold = (struct zs_usage *)uoldin;
	struct zs_usage *unew = (struct zs_usage *)unewin;

	struct zs_usage *udiff = zs_usage_alloc();
	if (udiff == NULL)
		return (NULL);

	return ((zs_usage_t)zs_usage_compute((zs_usage_t)udiff,
	    (zs_usage_t)uold, (zs_usage_t)unew, ZS_COMPUTE_USAGE_INTERVAL));
}

/*
 * In USAGE_INTERVAL case,
 *	sres - points to usage struct which is diff of snew and sold
 *	sold - points to stats from last interval
 *	snew - points to stats from current interval
 * In USAGE_HIGH | AVERAGE | TOTAL case,
 *	sres and sold - point same struct that is running total of previous
 *		        intervals.
 *	snew - points to a struct contains diff of snew and sold computed
 *	       in current interval.
 *
 * Return NULL on error.
 * ERRORS:
 *		EINVAL:  Invalid function.
 */
zs_usage_t
zs_usage_compute(zs_usage_t uresin, zs_usage_t uoldin, zs_usage_t unewin,
    zs_compute_t func)
{
	struct zs_usage *ures = (struct zs_usage *)uresin;
	struct zs_usage *unew = (struct zs_usage *)unewin;
	struct zs_usage *uold = (struct zs_usage *)uoldin;
	struct zs_system *sold, *snew, *sres;
	boolean_t alloced = B_FALSE;

	if (func != ZS_COMPUTE_USAGE_INTERVAL &&
	    func != ZS_COMPUTE_USAGE_TOTAL &&
	    func != ZS_COMPUTE_USAGE_AVERAGE &&
	    func != ZS_COMPUTE_USAGE_HIGH)
		abort();

	if (ures == NULL) {
		alloced = B_TRUE;
		ures = zs_usage_alloc();
		if (ures == NULL)
			return (NULL);
	}

	sres = ures->zsu_system;
	sold = uold->zsu_system;
	snew = unew->zsu_system;

	switch (func) {
	case ZS_COMPUTE_USAGE_INTERVAL:
		/* Use system totals from newer interval */
		if (sres != snew)
			*sres = *snew;

		TIMESTRUC_DELTA(sres->zss_cpu_total_time,
		    snew->zss_cpu_total_time, sold->zss_cpu_total_time);
		TIMESTRUC_DELTA(sres->zss_cpu_usage_kern,
		    snew->zss_cpu_usage_kern, sold->zss_cpu_usage_kern);
		TIMESTRUC_DELTA(sres->zss_cpu_usage_zones,
		    snew->zss_cpu_usage_zones, sold->zss_cpu_usage_zones);

		sres->zss_net_rbytes = snew->zss_net_rbytes -
		    sold->zss_net_rbytes;
		sres->zss_net_obytes = snew->zss_net_obytes -
		    sold->zss_net_obytes;
		sres->zss_net_prbytes = snew->zss_net_prbytes -
		    sold->zss_net_prbytes;
		sres->zss_net_pobytes = snew->zss_net_pobytes -
		    sold->zss_net_pobytes;
		sres->zss_net_bytes = snew->zss_net_bytes -
		    sold->zss_net_bytes;
		sres->zss_net_pbytes = snew->zss_net_pbytes -
		    sold->zss_net_pbytes;
		snew->zss_net_pused = zs_uint64_used_pct(snew->zss_net_speed,
		    sres->zss_net_prbytes, B_FALSE);
		sres->zss_net_pused = snew->zss_net_pused;

		ures->zsu_hrintervaltime = unew->zsu_hrintervaltime -
		    uold->zsu_hrintervaltime;

		break;
	case ZS_COMPUTE_USAGE_HIGH:

		/* Find max cpus */
		sres->zss_ncpus = MAX(sold->zss_ncpus, snew->zss_ncpus);
		sres->zss_ncpus_online = MAX(sold->zss_ncpus_online,
		    snew->zss_ncpus_online);

		/* Find max cpu times */
		sres->zss_cpu_total_time = ZS_MAXTS(sold->zss_cpu_total_time,
		    snew->zss_cpu_total_time);
		sres->zss_cpu_usage_kern = ZS_MAXTS(sold->zss_cpu_usage_kern,
		    snew->zss_cpu_usage_kern);
		sres->zss_cpu_usage_zones = ZS_MAXTS(sold->zss_cpu_usage_zones,
		    snew->zss_cpu_usage_zones);

		/* These don't change */
		sres->zss_processes_max = snew->zss_processes_max;
		sres->zss_lwps_max = snew->zss_lwps_max;
		sres->zss_shm_max = snew->zss_shm_max;
		sres->zss_shmids_max = snew->zss_shmids_max;
		sres->zss_semids_max = snew->zss_semids_max;
		sres->zss_msgids_max = snew->zss_msgids_max;
		sres->zss_lofi_max = snew->zss_lofi_max;
		/*
		 * Add in memory values and limits.  Scale memory to
		 * avoid overflow.
		 */
		sres->zss_ram_total = MAX(sold->zss_ram_total,
		    snew->zss_ram_total);
		sres->zss_ram_kern = MAX(sold->zss_ram_kern,
		    snew->zss_ram_kern);
		sres->zss_ram_zones = MAX(sold->zss_ram_zones,
		    snew->zss_ram_zones);
		sres->zss_locked_kern = MAX(sold->zss_locked_kern,
		    snew->zss_locked_kern);
		sres->zss_locked_zones = MAX(sold->zss_locked_zones,
		    snew->zss_locked_zones);
		sres->zss_vm_total = MAX(sold->zss_vm_total,
		    snew->zss_vm_total);
		sres->zss_vm_kern = MAX(sold->zss_vm_kern,
		    snew->zss_vm_kern);
		sres->zss_vm_zones = MAX(sold->zss_vm_zones,
		    snew->zss_vm_zones);
		sres->zss_swap_total = MAX(sold->zss_swap_total,
		    snew->zss_swap_total);
		sres->zss_swap_used = MAX(sold->zss_swap_used,
		    snew->zss_swap_used);

		sres->zss_processes = MAX(sold->zss_processes,
		    snew->zss_processes);
		sres->zss_lwps = MAX(sold->zss_lwps, snew->zss_lwps);
		sres->zss_shm = MAX(sold->zss_shm, snew->zss_shm);
		sres->zss_shmids = MAX(sold->zss_shmids, snew->zss_shmids);
		sres->zss_semids = MAX(sold->zss_semids, snew->zss_semids);
		sres->zss_msgids = MAX(sold->zss_msgids, snew->zss_msgids);
		sres->zss_lofi = MAX(sold->zss_msgids, snew->zss_lofi);

		sres->zss_net_pused = MAX(sold->zss_net_pused,
		    snew->zss_net_pused);
		sres->zss_net_speed = MAX(sold->zss_net_speed,
		    snew->zss_net_speed);
		sres->zss_net_bytes = MAX(sold->zss_net_bytes,
		    snew->zss_net_bytes);
		sres->zss_net_rbytes = MAX(sold->zss_net_rbytes,
		    snew->zss_net_rbytes);
		sres->zss_net_obytes = MAX(sold->zss_net_obytes,
		    snew->zss_net_obytes);
		sres->zss_net_pbytes = MAX(sold->zss_net_pbytes,
		    snew->zss_net_pbytes);
		sres->zss_net_prbytes = MAX(sold->zss_net_prbytes,
		    snew->zss_net_prbytes);
		sres->zss_net_pobytes = MAX(sold->zss_net_pobytes,
		    snew->zss_net_pobytes);
	break;
	case ZS_COMPUTE_USAGE_TOTAL:
		/* FALLTHROUGH */
	case ZS_COMPUTE_USAGE_AVERAGE:
		ures->zsu_intervals++;

		/*
		 * Add cpus.  The total report will divide this by the
		 * number of intervals to give the average number of cpus
		 * over all intervals.
		 */
		sres->zss_ncpus = sold->zss_ncpus + snew->zss_ncpus;
		sres->zss_ncpus_online = sold->zss_ncpus_online +
		    snew->zss_ncpus_online;

		/* Add in cpu times */
		sres->zss_cpu_total_time = sold->zss_cpu_total_time;
		TIMESTRUC_ADD_TIMESTRUC(sres->zss_cpu_total_time,
		    snew->zss_cpu_total_time);
		sres->zss_cpu_usage_kern = sold->zss_cpu_usage_kern;
		TIMESTRUC_ADD_TIMESTRUC(sres->zss_cpu_usage_kern,
		    snew->zss_cpu_usage_kern);
		sres->zss_cpu_usage_zones = sold->zss_cpu_usage_zones;
		TIMESTRUC_ADD_TIMESTRUC(sres->zss_cpu_usage_zones,
		    snew->zss_cpu_usage_zones);

		/* These don't change */
		sres->zss_processes_max = snew->zss_processes_max;
		sres->zss_lwps_max = snew->zss_lwps_max;
		sres->zss_shm_max = snew->zss_shm_max;
		sres->zss_shmids_max = snew->zss_shmids_max;
		sres->zss_semids_max = snew->zss_semids_max;
		sres->zss_msgids_max = snew->zss_msgids_max;
		sres->zss_lofi_max = snew->zss_lofi_max;
		/*
		 * Add in memory values and limits.  Scale memory to
		 * avoid overflow.
		 */
		if (sres != sold) {
			sres->zss_ram_total = sold->zss_ram_total / 1024;
			sres->zss_ram_kern = sold->zss_ram_kern / 1024;
			sres->zss_ram_zones = sold->zss_ram_zones / 1024;
			sres->zss_locked_kern = sold->zss_locked_kern / 1024;
			sres->zss_locked_zones = sold->zss_locked_zones / 1024;
			sres->zss_vm_total = sold->zss_vm_total / 1024;
			sres->zss_vm_kern = sold->zss_vm_kern / 1024;
			sres->zss_vm_zones = sold->zss_vm_zones / 1024;
			sres->zss_swap_total = sold->zss_swap_total / 1024;
			sres->zss_swap_used = sold->zss_swap_used / 1024;

			sres->zss_processes = sold->zss_processes;
			sres->zss_lwps = sold->zss_lwps;
			sres->zss_shm = sold->zss_shm / 1024;
			sres->zss_shmids = sold->zss_shmids;
			sres->zss_semids = sold->zss_semids;
			sres->zss_msgids = sold->zss_msgids;
			sres->zss_lofi = sold->zss_lofi;
		}
		/* Add in new values. */
		sres->zss_ram_total += (snew->zss_ram_total / 1024);
		sres->zss_ram_kern += (snew->zss_ram_kern / 1024);
		sres->zss_ram_zones += (snew->zss_ram_zones / 1024);
		sres->zss_locked_kern += (snew->zss_locked_kern / 1024);
		sres->zss_locked_zones += (snew->zss_locked_zones / 1024);
		sres->zss_vm_total += (snew->zss_vm_total / 1024);
		sres->zss_vm_kern += (snew->zss_vm_kern / 1024);
		sres->zss_vm_zones += (snew->zss_vm_zones / 1024);
		sres->zss_swap_total += (snew->zss_swap_total / 1024);
		sres->zss_swap_used += (snew->zss_swap_used / 1024);
		sres->zss_processes += snew->zss_processes;
		sres->zss_lwps += snew->zss_lwps;
		sres->zss_shm += (snew->zss_shm / 1024);
		sres->zss_shmids += snew->zss_shmids;
		sres->zss_semids += snew->zss_semids;
		sres->zss_msgids += snew->zss_msgids;
		sres->zss_lofi += snew->zss_lofi;

		/* add in new net stats */
		sres->zss_net_bytes += snew->zss_net_bytes;
		sres->zss_net_rbytes += snew->zss_net_rbytes;
		sres->zss_net_obytes += snew->zss_net_obytes;
		sres->zss_net_pbytes += snew->zss_net_pbytes;
		sres->zss_net_prbytes += snew->zss_net_prbytes;
		sres->zss_net_pobytes += snew->zss_net_pobytes;
		sres->zss_net_speed += snew->zss_net_speed;
		sres->zss_net_pused += snew->zss_net_pused;

		ures->zsu_hrintervaltime += unew->zsu_hrintervaltime;

		break;
	default:
		if (alloced)
			zs_usage_free((zs_usage_t)ures);
		abort();
	}

	if (zs_usage_compute_zones(ures, uold, unew, func) != 0)
		goto err;

	if (zs_usage_compute_psets(ures, uold, unew, func) != 0)
		goto err;

	if (zs_usage_compute_datalinks(ures, uold, unew, func) != 0)
		goto err;

	return ((zs_usage_t)ures);
err:
	if (alloced)
		zs_usage_free((zs_usage_t)ures);
	return (NULL);
}

void
zs_usage_free(zs_usage_t usagein)
{
	struct zs_usage *usage = (struct zs_usage *)usagein;
	struct zs_zone *zone, *ztmp;
	struct zs_pset *pset, *ptmp;
	struct zs_pset_zone *pz, *pztmp;

	if (usage->zsu_mmap) {
		(void) munmap((void *)usage, usage->zsu_size);
		return;
	}
	free(usage->zsu_system);
	zone = list_head(&usage->zsu_zone_list);
	while (zone != NULL) {
			ztmp = zone;
			zone = list_next(&usage->zsu_zone_list, zone);
			free(ztmp);
	}
	pset = list_head(&usage->zsu_pset_list);
	while (pset != NULL) {
		pz = list_head(&pset->zsp_usage_list);
		while (pz != NULL) {
			pztmp = pz;
			pz = list_next(&pset->zsp_usage_list, pz);
			free(pztmp);
		}
		ptmp = pset;
		pset = list_next(&usage->zsu_pset_list, pset);
		free(ptmp);
	}
	free(usage);
}

zs_usage_set_t
zs_usage_set_alloc()
{
	struct zs_usage_set *set;

	set = calloc(1, sizeof (struct zs_usage_set));
	if (set == NULL)
		return (NULL);

	if ((set->zsus_total = (struct zs_usage *)zs_usage_alloc()) == NULL)
		goto err;
	if ((set->zsus_avg = (struct zs_usage *)zs_usage_alloc()) == NULL)
		goto err;
	if ((set->zsus_high = (struct zs_usage *)zs_usage_alloc()) == NULL)
		goto err;

	return ((zs_usage_set_t)set);

err:
	if (set->zsus_total != NULL)
		zs_usage_free((zs_usage_t)set->zsus_total);
	if (set->zsus_avg != NULL)
		zs_usage_free((zs_usage_t)set->zsus_avg);
	if (set->zsus_high != NULL)
		zs_usage_free((zs_usage_t)set->zsus_high);

	return (NULL);
}

void
zs_usage_set_free(zs_usage_set_t setin)
{
	struct zs_usage_set *set = (struct zs_usage_set *)setin;

	zs_usage_free((zs_usage_t)set->zsus_total);
	zs_usage_free((zs_usage_t)set->zsus_avg);
	zs_usage_free((zs_usage_t)set->zsus_high);
	free(set);
}

int
zs_usage_set_add(zs_usage_set_t setin, zs_usage_t usagein)
{

	struct zs_usage_set *set = (struct zs_usage_set *)setin;
	struct zs_usage *usage = (struct zs_usage *)usagein;

	/* Compute ongoing functions for usage set */
	(void) zs_usage_compute((zs_usage_t)set->zsus_high,
	    (zs_usage_t)set->zsus_high, (zs_usage_t)usage,
	    ZS_COMPUTE_USAGE_HIGH);

	(void) zs_usage_compute((zs_usage_t)set->zsus_total,
	    (zs_usage_t)set->zsus_total, (zs_usage_t)usage,
	    ZS_COMPUTE_USAGE_TOTAL);

	(void) zs_usage_compute((zs_usage_t)set->zsus_avg,
	    (zs_usage_t)set->zsus_avg, (zs_usage_t)usage,
	    ZS_COMPUTE_USAGE_AVERAGE);

	set->zsus_count++;
	zs_usage_free((zs_usage_t)usage);
	return (0);
}

int
zs_usage_set_count(zs_usage_set_t setin)
{
	struct zs_usage_set *set = (struct zs_usage_set *)setin;
	return (set->zsus_count);
}

zs_usage_t
zs_usage_set_compute(zs_usage_set_t setin,  zs_compute_set_t func)
{
	struct zs_usage_set *set = (struct zs_usage_set *)setin;
	struct zs_usage *u;
	struct zs_system *s;
	struct zs_zone *z;
	struct zs_pset *p;
	struct zs_datalink *l, *vl;
	struct zs_pset_zone *pz;
	uint_t intervals;
	boolean_t average;
	uint64_t divisor, vdivisor;


	switch (func) {
	case ZS_COMPUTE_SET_HIGH:
		return ((zs_usage_t)set->zsus_high);
	case ZS_COMPUTE_SET_TOTAL:
		u = set->zsus_total;
		average = B_FALSE;
		break;
	case ZS_COMPUTE_SET_AVERAGE:
		u = set->zsus_avg;
		average = B_TRUE;
		break;
	default:
		abort();
	}

	s = u->zsu_system;

	s->zss_ram_total /= u->zsu_intervals;
	s->zss_ram_total *= 1024;
	s->zss_ram_kern /= u->zsu_intervals;
	s->zss_ram_kern *= 1024;
	s->zss_ram_zones /= u->zsu_intervals;
	s->zss_ram_zones *= 1024;
	s->zss_locked_kern /= u->zsu_intervals;
	s->zss_locked_kern *= 1024;
	s->zss_locked_zones /= u->zsu_intervals;
	s->zss_locked_zones *= 1024;
	s->zss_vm_total /= u->zsu_intervals;
	s->zss_vm_total *= 1024;
	s->zss_vm_kern /= u->zsu_intervals;
	s->zss_vm_kern *= 1024;
	s->zss_vm_zones /= u->zsu_intervals;
	s->zss_vm_zones *= 1024;
	s->zss_swap_total /= u->zsu_intervals;
	s->zss_swap_total *= 1024;
	s->zss_swap_used /= u->zsu_intervals;
	s->zss_swap_used *= 1024;
	s->zss_processes /= u->zsu_intervals;
	s->zss_lwps /= u->zsu_intervals;
	s->zss_shm /= u->zsu_intervals;
	s->zss_shm *= 1024;
	s->zss_shmids /= u->zsu_intervals;
	s->zss_semids /= u->zsu_intervals;
	s->zss_msgids /= u->zsu_intervals;
	s->zss_lofi /= u->zsu_intervals;

	if (average) {
		divisor = (uint64_t)u->zsu_intervals;
	} else {
		/*
		 * networking total needs to be normalized
		 * to a "bytes per second number".
		 */
		divisor = (uint64_t)u->zsu_hrintervaltime / 1000000000;
	}
	s->zss_net_bytes /= divisor;
	s->zss_net_rbytes /= divisor;
	s->zss_net_obytes /= divisor;
	s->zss_net_pbytes /= divisor;
	s->zss_net_prbytes /= divisor;
	s->zss_net_pobytes /= divisor;
	s->zss_net_pused /= divisor;

	s->zss_ncpus /= u->zsu_intervals;
	s->zss_ncpus_online /= u->zsu_intervals;

	for (z = list_head(&u->zsu_zone_list); z != NULL;
	    z = list_next(&u->zsu_zone_list, z)) {

		if (average) {
			intervals = z->zsz_intervals;
		} else {
			assert(z->zsz_intervals == 0);
			intervals = u->zsu_intervals;
		}

		if (z->zsz_cpu_cap != ZS_LIMIT_NONE)
			z->zsz_cpu_cap /= z->zsz_intervals;
		if (z->zsz_ram_cap != ZS_LIMIT_NONE)
			z->zsz_ram_cap /= z->zsz_intervals;
		if (z->zsz_vm_cap != ZS_LIMIT_NONE)
			z->zsz_vm_cap /= z->zsz_intervals;
		if (z->zsz_locked_cap != ZS_LIMIT_NONE)
			z->zsz_locked_cap /= z->zsz_intervals;
		if (z->zsz_processes_cap != ZS_LIMIT_NONE)
			z->zsz_processes_cap /= z->zsz_intervals;
		if (z->zsz_lwps_cap != ZS_LIMIT_NONE)
			z->zsz_lwps_cap /= z->zsz_intervals;
		if (z->zsz_shm_cap != ZS_LIMIT_NONE)
			z->zsz_shm_cap /= z->zsz_intervals;
		if (z->zsz_shmids_cap != ZS_LIMIT_NONE)
			z->zsz_shmids_cap /= z->zsz_intervals;
		if (z->zsz_semids_cap != ZS_LIMIT_NONE)
			z->zsz_semids_cap /= z->zsz_intervals;
		if (z->zsz_msgids_cap != ZS_LIMIT_NONE)
			z->zsz_msgids_cap /= z->zsz_intervals;
		if (z->zsz_lofi_cap != ZS_LIMIT_NONE)
			z->zsz_lofi_cap /= z->zsz_intervals;

		z->zsz_usage_ram /= intervals;
		z->zsz_usage_locked /= intervals;
		z->zsz_usage_vm /= intervals;
		z->zsz_processes /= intervals;
		z->zsz_lwps /= intervals;
		z->zsz_shm /= intervals;
		z->zsz_shmids /= intervals;
		z->zsz_semids /= intervals;
		z->zsz_msgids /= intervals;
		z->zsz_lofi /= intervals;
		z->zsz_cpus_online /= intervals;
		z->zsz_cpu_shares /= intervals;
	}
	for (p = list_head(&u->zsu_pset_list); p != NULL;
	    p = list_next(&u->zsu_pset_list, p)) {

		intervals = p->zsp_intervals;

		p->zsp_online /= intervals;
		p->zsp_size /= intervals;
		p->zsp_min /= intervals;
		p->zsp_max /= intervals;
		p->zsp_importance /= intervals;
		p->zsp_cpu_shares /= intervals;

		for (pz = list_head(&p->zsp_usage_list); pz != NULL;
		    pz = list_next(&p->zsp_usage_list, pz)) {

			if (average) {
				intervals = pz->zspz_intervals;
			} else {
				assert(pz->zspz_intervals == 0);
				intervals = p->zsp_intervals;
			}
			pz->zspz_cpu_shares /= intervals;
		}
	}

	for (l = list_head(&u->zsu_datalink_list); l != NULL;
	    l = list_next(&u->zsu_datalink_list, l)) {
		if (average) {
			/*
			 * interval should be at least 1 as a down link
			 * may not have a chance to increment interval count
			 */
			if (l->zsl_intervals == 0)
				divisor = 1;
			else
				divisor = (uint64_t)l->zsl_intervals;
		} else
			/*
			 * networking total needs to be normalized
			 * to a "bytes per second number".
			 */
			divisor = ((uint64_t)l->zsl_hrtime) / 1000000000;

		l->zsl_rbytes /= divisor;
		l->zsl_obytes /= divisor;
		l->zsl_prbytes /= divisor;
		l->zsl_pobytes /= divisor;
		l->zsl_total_rbytes /= divisor;
		l->zsl_total_obytes /= divisor;
		l->zsl_total_prbytes /= divisor;
		l->zsl_total_pobytes /= divisor;

		for (vl = list_head(&l->zsl_vlink_list); vl != NULL;
		    vl = list_next(&l->zsl_vlink_list, vl)) {
			if (average) {
				/*
				 * interval should be at least 1 as a down link
				 * may not have a chance to increment interval
				 * count
				 */
				if (vl->zsl_intervals == 0)
					vdivisor = 1;
				else
					vdivisor = (uint64_t)vl->zsl_intervals;
			} else
				/*
				 * networking total needs to be normalized
				 * to a "bytes per second number".
				 */
				vdivisor = ((uint64_t)vl->zsl_hrtime) /
				    1000000000;
			vl->zsl_rbytes /= vdivisor;
			vl->zsl_obytes /= vdivisor;
			vl->zsl_prbytes /= vdivisor;
			vl->zsl_pobytes /= vdivisor;
			vl->zsl_total_rbytes /= vdivisor;
			vl->zsl_total_obytes /= vdivisor;
			vl->zsl_total_prbytes /= vdivisor;
			vl->zsl_total_pobytes /= vdivisor;
		}
	}
	return ((zs_usage_t)u);
}

/*
 * Returns B_TRUE if property is valid.  Otherwise, returns B_FALSE.
 */
boolean_t
zs_resource_property_supported(zs_resource_property_t prop)
{

	switch (prop) {
	case ZS_RESOURCE_PROP_CPU_TOTAL:
	case ZS_RESOURCE_PROP_CPU_ONLINE:
	case ZS_RESOURCE_PROP_CPU_LOAD_1MIN:
	case ZS_RESOURCE_PROP_CPU_LOAD_5MIN:
	case ZS_RESOURCE_PROP_CPU_LOAD_15MIN:
		return (B_TRUE);
	default:
		return (B_FALSE);
	}
}

/*
 * Returns 0 on success.  Trips abort() on invalid property.
 */
zs_property_t
zs_resource_property(zs_usage_t uin, zs_resource_property_t prop)
{
	struct zs_usage *u = (struct zs_usage *)uin;
	struct zs_property *p;

	switch (prop) {
	case ZS_RESOURCE_PROP_CPU_TOTAL:
		p = &u->zsu_system->zss_prop_cpu_total;
		p->zsp_type = DATA_TYPE_UINT64;
		p->zsp_v.zsv_uint64 = u->zsu_system->zss_ncpus;
		break;
	case ZS_RESOURCE_PROP_CPU_ONLINE:
		p = &u->zsu_system->zss_prop_cpu_online;
		p->zsp_type = DATA_TYPE_UINT64;
		p->zsp_v.zsv_uint64 = u->zsu_system->zss_ncpus_online;
		break;
	case ZS_RESOURCE_PROP_CPU_LOAD_1MIN:
		p = &u->zsu_system->zss_prop_cpu_1min;
		p->zsp_type = DATA_TYPE_DOUBLE;
		p->zsp_v.zsv_double = u->zsu_system->zss_load_avg[LOADAVG_1MIN];
		break;
	case ZS_RESOURCE_PROP_CPU_LOAD_5MIN:
		p = &u->zsu_system->zss_prop_cpu_5min;
		p->zsp_type = DATA_TYPE_DOUBLE;
		p->zsp_v.zsv_double = u->zsu_system->zss_load_avg[LOADAVG_5MIN];
		break;
	case ZS_RESOURCE_PROP_CPU_LOAD_15MIN:
		p = &u->zsu_system->zss_prop_cpu_15min;
		p->zsp_type = DATA_TYPE_DOUBLE;
		p->zsp_v.zsv_double =
		    u->zsu_system->zss_load_avg[LOADAVG_15MIN];
		break;
	default:
		abort();
	}
	return ((zs_property_t)p);
}

/*
 * Returns true if resource is supported.
 */
boolean_t
zs_resource_supported(zs_resource_t res)
{
	switch (res)  {
	case ZS_RESOURCE_CPU:
	case ZS_RESOURCE_RAM_RSS:
	case ZS_RESOURCE_RAM_LOCKED:
	case ZS_RESOURCE_VM:
	case ZS_RESOURCE_DISK_SWAP:
	case ZS_RESOURCE_SHM_MEMORY:
	case ZS_RESOURCE_LWPS:
	case ZS_RESOURCE_PROCESSES:
	case ZS_RESOURCE_SHM_IDS:
	case ZS_RESOURCE_SEM_IDS:
	case ZS_RESOURCE_MSG_IDS:
	case ZS_RESOURCE_LOFI:
		return (B_TRUE);
	default:
		return (B_FALSE);
	}
}

/*
 * Returns true if resource supports user
 */
boolean_t
zs_resource_user_supported(zs_resource_t res, zs_user_t user)
{
	switch (res)  {
	case ZS_RESOURCE_CPU:
	case ZS_RESOURCE_RAM_RSS:
	case ZS_RESOURCE_RAM_LOCKED:
	case ZS_RESOURCE_VM:
	case ZS_RESOURCE_SHM_MEMORY:
	case ZS_RESOURCE_LWPS:
	case ZS_RESOURCE_PROCESSES:
	case ZS_RESOURCE_SHM_IDS:
	case ZS_RESOURCE_SEM_IDS:
	case ZS_RESOURCE_MSG_IDS:
	case ZS_RESOURCE_LOFI:
		switch (user) {
		case ZS_USER_ALL:
		case ZS_USER_SYSTEM:
		case ZS_USER_ZONES:
		case ZS_USER_FREE:
			return (B_TRUE);
		default:
			return (B_FALSE);
		}
	case ZS_RESOURCE_DISK_SWAP:
		switch (user) {
		case ZS_USER_ALL:
		case ZS_USER_FREE:
			return (B_TRUE);
		default:
			return (B_FALSE);
		}
	default:
		return (B_FALSE);
	}
}

/*
 * Returns one of ZS_RESOURCE_TYPE_* on success.  Asserts on invalid
 * resource.
 */
zs_resource_type_t
zs_resource_type(zs_resource_t res)
{
	switch (res)  {
	case ZS_RESOURCE_CPU:
		return (ZS_RESOURCE_TYPE_TIME);
		/* NOTREACHED */
		break;
	case ZS_RESOURCE_RAM_RSS:
	case ZS_RESOURCE_RAM_LOCKED:
	case ZS_RESOURCE_VM:
	case ZS_RESOURCE_DISK_SWAP:
	case ZS_RESOURCE_SHM_MEMORY:
		return (ZS_RESOURCE_TYPE_BYTES);
		/* NOTREACHED */
		break;
	case ZS_RESOURCE_LWPS:
	case ZS_RESOURCE_PROCESSES:
	case ZS_RESOURCE_SHM_IDS:
	case ZS_RESOURCE_SEM_IDS:
	case ZS_RESOURCE_MSG_IDS:
	case ZS_RESOURCE_LOFI:
		return (ZS_RESOURCE_TYPE_COUNT);
		/* NOTREACHED */
		break;
	default:
		abort();
		return (0);
	}
}

/*
 * Get total available resource on system
 */
uint64_t
zs_resource_total_uint64(zs_usage_t uin, zs_resource_t res)
{
	struct zs_usage *u = (struct zs_usage *)uin;
	uint64_t v;

	switch (res)  {
	case ZS_RESOURCE_CPU:
		v = zs_cpu_total_cpu(u);
		break;
	case ZS_RESOURCE_RAM_RSS:
		v = zs_physical_memory_total(u);
		break;
	case ZS_RESOURCE_RAM_LOCKED:
		v = zs_locked_memory_total(u);
		break;
	case ZS_RESOURCE_VM:
		v = zs_virtual_memory_total(u);
		break;
	case ZS_RESOURCE_DISK_SWAP:
		v = zs_disk_swap_total(u);
		break;
	case ZS_RESOURCE_LWPS:
		v = zs_lwps_total(u);
		break;
	case ZS_RESOURCE_PROCESSES:
		v = zs_processes_total(u);
		break;
	case ZS_RESOURCE_SHM_MEMORY:
		v = zs_shm_total(u);
		break;
	case ZS_RESOURCE_SHM_IDS:
		v = zs_shmids_total(u);
		break;
	case ZS_RESOURCE_SEM_IDS:
		v = zs_semids_total(u);
		break;
	case ZS_RESOURCE_MSG_IDS:
		v = zs_msgids_total(u);
		break;
	case ZS_RESOURCE_LOFI:
		v = zs_lofi_total(u);
		break;
	case ZS_RESOURCE_NET_SPEED:
		v = zs_net_total(u);
		break;
	default:
		abort();
	}
	return (v);
}

/*
 * Get amount of used resource.
 */
uint64_t
zs_resource_used_uint64(zs_usage_t uin, zs_resource_t res, zs_user_t user)
{
	struct zs_usage *u = (struct zs_usage *)uin;
	uint64_t v;

	switch (res)  {
	case ZS_RESOURCE_CPU:
		switch (user) {
		case ZS_USER_ALL:
			v = zs_cpu_usage_all_cpu(u);
			break;
		case ZS_USER_SYSTEM:
			v = zs_cpu_usage_kernel_cpu(u);
			break;
		case ZS_USER_ZONES:
			v = zs_cpu_usage_zones_cpu(u);
			break;
		case ZS_USER_FREE:
			v = zs_cpu_usage_idle_cpu(u);
			break;
		default:
			abort();
		}
		break;
	case ZS_RESOURCE_RAM_RSS:
		switch (user) {
		case ZS_USER_ALL:
			v = zs_physical_memory_usage_all(u);
			break;
		case ZS_USER_SYSTEM:
			v = zs_physical_memory_usage_kernel(u);
			break;
		case ZS_USER_ZONES:
			v = zs_physical_memory_usage_zones(u);
			break;
		case ZS_USER_FREE:
			v = zs_physical_memory_usage_free(u);
			break;
		default:
			abort();
		}
		break;
	case ZS_RESOURCE_RAM_LOCKED:
		switch (user) {
		case ZS_USER_ALL:
			v = zs_locked_memory_usage_all(u);
			break;
		case ZS_USER_SYSTEM:
			v = zs_locked_memory_usage_kernel(u);
			break;
		case ZS_USER_ZONES:
			v = zs_locked_memory_usage_zones(u);
			break;
		case ZS_USER_FREE:
			v = zs_locked_memory_usage_free(u);
			break;
		default:
			abort();
		}
		break;
	case ZS_RESOURCE_VM:
		switch (user) {
		case ZS_USER_ALL:
			v = zs_virtual_memory_usage_all(u);
			break;
		case ZS_USER_SYSTEM:
			v = zs_virtual_memory_usage_kernel(u);
			break;
		case ZS_USER_ZONES:
			v = zs_virtual_memory_usage_zones(u);
			break;
		case ZS_USER_FREE:
			v = zs_virtual_memory_usage_free(u);
			break;
		default:
			abort();
		}
		break;
	case ZS_RESOURCE_DISK_SWAP:
		switch (user) {
		case ZS_USER_ALL:
			v = zs_disk_swap_usage_all(u);
			break;
		case ZS_USER_FREE:
			v = zs_disk_swap_usage_free(u);
			break;
		case ZS_USER_SYSTEM:
		case ZS_USER_ZONES:
			/* FALLTHROUGH */
		default:
			abort();
		}
		break;
	case ZS_RESOURCE_LWPS:
		switch (user) {
		case ZS_USER_ALL:
		case ZS_USER_ZONES:
			v = zs_lwps_usage_all(u);
			break;
		case ZS_USER_FREE:
			v = zs_lwps_total(u) - zs_lwps_usage_all(u);
			break;
		case ZS_USER_SYSTEM:
			v = 0;
			break;
		default:
			abort();
		}
		break;
	case ZS_RESOURCE_PROCESSES:
		switch (user) {
		case ZS_USER_ALL:
		case ZS_USER_ZONES:
			v = zs_processes_usage_all(u);
			break;
		case ZS_USER_FREE:
			v = zs_processes_total(u) - zs_processes_usage_all(u);
			break;
		case ZS_USER_SYSTEM:
			v = 0;
			break;
		default:
			abort();
		}
		break;
	case ZS_RESOURCE_SHM_MEMORY:
		switch (user) {
		case ZS_USER_ALL:
		case ZS_USER_ZONES:
			v = zs_shm_usage_all(u);
			break;
		case ZS_USER_FREE:
			v = zs_shm_total(u) -
			    zs_shm_usage_all(u);
			break;
		case ZS_USER_SYSTEM:
			v = 0;
			break;
		default:
			abort();
		}
		break;
	case ZS_RESOURCE_SHM_IDS:
		switch (user) {
		case ZS_USER_ALL:
		case ZS_USER_ZONES:
			v = zs_shmids_usage_all(u);
			break;
		case ZS_USER_FREE:
			v = zs_shmids_total(u) - zs_shmids_usage_all(u);
			break;
		case ZS_USER_SYSTEM:
			v = 0;
			break;
		default:
			abort();
		}
		break;
	case ZS_RESOURCE_SEM_IDS:
		switch (user) {
		case ZS_USER_ALL:
		case ZS_USER_ZONES:
			v = zs_semids_usage_all(u);
			break;
		case ZS_USER_FREE:
			v = zs_semids_total(u) - zs_semids_usage_all(u);
			break;
		case ZS_USER_SYSTEM:
			v = 0;
			break;
		default:
			abort();
		}
		break;
	case ZS_RESOURCE_MSG_IDS:
		switch (user) {
		case ZS_USER_ALL:
		case ZS_USER_ZONES:
			v = zs_msgids_usage_all(u);
			break;
		case ZS_USER_FREE:
			v = zs_msgids_total(u) - zs_msgids_usage_all(u);
			break;
		case ZS_USER_SYSTEM:
			v = 0;
			break;
		default:
			abort();
		}
		break;
	case ZS_RESOURCE_LOFI:
		switch (user) {
		case ZS_USER_ALL:
		case ZS_USER_ZONES:
			v = zs_lofi_usage_all(u);
			break;
		case ZS_USER_FREE:
			v = zs_lofi_total(u) - zs_lofi_usage_all(u);
			break;
		case ZS_USER_SYSTEM:
			v = 0;
			break;
		default:
			abort();
		}
		break;
	case ZS_RESOURCE_NET_ALL:
		switch (user) {
		case ZS_USER_ALL:
		case ZS_USER_ZONES:
			v = zs_net_bytes(u);
			break;
		case ZS_USER_SYSTEM:
		case ZS_USER_FREE:
			v = 0;
			break;
		default:
			abort();
		}
		break;
	case ZS_RESOURCE_NET_IN:
		switch (user) {
		case ZS_USER_ALL:
		case ZS_USER_ZONES:
			v = zs_net_rbytes(u);
			break;
		case ZS_USER_SYSTEM:
		case ZS_USER_FREE:
			v = 0;
			break;
		default:
			abort();
		}
		break;
	case ZS_RESOURCE_NET_OUT:
		switch (user) {
		case ZS_USER_ALL:
		case ZS_USER_ZONES:
			v = zs_net_obytes(u);
			break;
		case ZS_USER_SYSTEM:
		case ZS_USER_FREE:
			v = 0;
			break;
		default:
			abort();
		}
		break;
	case ZS_RESOURCE_NET_PHYS_ALL:
		switch (user) {
		case ZS_USER_ALL:
		case ZS_USER_ZONES:
			v = zs_net_pbytes(u);
			break;
		case ZS_USER_SYSTEM:
		case ZS_USER_FREE:
			v = 0;
			break;
		default:
			abort();
		}
		break;
	case ZS_RESOURCE_NET_PHYS_IN:
		switch (user) {
		case ZS_USER_ALL:
		case ZS_USER_ZONES:
			v = zs_net_prbytes(u);
			break;
		case ZS_USER_SYSTEM:
		case ZS_USER_FREE:
			v = 0;
			break;
		default:
			abort();
		}
		break;
	case ZS_RESOURCE_NET_PHYS_OUT:
		switch (user) {
		case ZS_USER_ALL:
		case ZS_USER_ZONES:
			v = zs_net_pobytes(u);
			break;
		case ZS_USER_SYSTEM:
		case ZS_USER_FREE:
			v = 0;
			break;
		default:
			abort();
		}
		break;
	default:
		abort();
	}

	return (v);
}

/*
 * Get used resource as a percent of total resource.
 */
uint_t
zs_resource_used_pct(zs_usage_t uin, zs_resource_t res, zs_user_t user)
{
	struct zs_usage *u = (struct zs_usage *)uin;
	uint64_t v;

	switch (res)  {
	case ZS_RESOURCE_CPU:
		switch (user) {
		case ZS_USER_ALL:
			v = zs_cpu_usage_all_pct(u);
			break;
		case ZS_USER_SYSTEM:
			v = zs_cpu_usage_kernel_pct(u);
			break;
		case ZS_USER_ZONES:
			v = zs_cpu_usage_zones_pct(u);
			break;
		case ZS_USER_FREE:
			v = zs_cpu_usage_idle_pct(u);
			break;
		default:
			abort();
		}
		break;
	case ZS_RESOURCE_RAM_RSS:
		switch (user) {
		case ZS_USER_ALL:
			v = zs_physical_memory_usage_all_pct(u);
			break;
		case ZS_USER_SYSTEM:
			v = zs_physical_memory_usage_kernel_pct(u);
			break;
		case ZS_USER_ZONES:
			v = zs_physical_memory_usage_zones_pct(u);
			break;
		case ZS_USER_FREE:
			v = zs_physical_memory_usage_free_pct(u);
			break;
		default:
			abort();
		}
		break;
	case ZS_RESOURCE_RAM_LOCKED:
		switch (user) {
		case ZS_USER_ALL:
			v = zs_locked_memory_usage_all_pct(u);
			break;
		case ZS_USER_SYSTEM:
			v = zs_locked_memory_usage_kernel_pct(u);
			break;
		case ZS_USER_ZONES:
			v = zs_locked_memory_usage_zones_pct(u);
			break;
		case ZS_USER_FREE:
			v = zs_locked_memory_usage_free_pct(u);
			break;
		default:
			abort();
		}
		break;
	case ZS_RESOURCE_VM:
		switch (user) {
		case ZS_USER_ALL:
			v = zs_virtual_memory_usage_all_pct(u);
			break;
		case ZS_USER_SYSTEM:
			v = zs_virtual_memory_usage_kernel_pct(u);
			break;
		case ZS_USER_ZONES:
			v = zs_virtual_memory_usage_zones_pct(u);
			break;
		case ZS_USER_FREE:
			v = zs_virtual_memory_usage_free_pct(u);
			break;
		default:
			abort();
		}
		break;
	case ZS_RESOURCE_DISK_SWAP:
		switch (user) {
		case ZS_USER_ALL:
			v = zs_disk_swap_usage_all_pct(u);
			break;
		case ZS_USER_FREE:
			v = zs_disk_swap_usage_free_pct(u);
			break;
		case ZS_USER_SYSTEM:
		case ZS_USER_ZONES:
			/* FALLTHROUGH */
		default:
			abort();
		}
		break;
	case ZS_RESOURCE_LWPS:
		switch (user) {
		case ZS_USER_ALL:
		case ZS_USER_ZONES:
			v = zs_lwps_usage_all_pct(u);
			break;
		case ZS_USER_FREE:
			v = ZSD_PCT_INT - zs_lwps_usage_all_pct(u);
			break;
		case ZS_USER_SYSTEM:
			v = 0;
			break;
		default:
			abort();
		}
		break;
	case ZS_RESOURCE_PROCESSES:
		switch (user) {
		case ZS_USER_ALL:
		case ZS_USER_ZONES:
			v = zs_processes_usage_all_pct(u);
			break;
		case ZS_USER_FREE:
			v = ZSD_PCT_INT - zs_processes_usage_all_pct(u);
			break;
		case ZS_USER_SYSTEM:
			v = 0;
			break;
		default:
			abort();
		}
		break;
	case ZS_RESOURCE_SHM_MEMORY:
		switch (user) {
		case ZS_USER_ALL:
		case ZS_USER_ZONES:
			v = zs_shm_usage_all_pct(u);
			break;
		case ZS_USER_FREE:
			v = ZSD_PCT_INT - zs_shm_usage_all_pct(u);
			break;
		case ZS_USER_SYSTEM:
			v = 0;
			break;
		default:
			abort();
		}
		break;
	case ZS_RESOURCE_SHM_IDS:
			switch (user) {
		case ZS_USER_ALL:
		case ZS_USER_ZONES:
			v = zs_shmids_usage_all_pct(u);
			break;
		case ZS_USER_FREE:
			v = ZSD_PCT_INT - zs_shmids_usage_all_pct(u);
			break;
		case ZS_USER_SYSTEM:
			v = 0;
			break;
		default:
			abort();
		}
		break;
	case ZS_RESOURCE_SEM_IDS:
			switch (user) {
		case ZS_USER_ALL:
		case ZS_USER_ZONES:
			v = zs_semids_usage_all_pct(u);
			break;
		case ZS_USER_FREE:
			v = ZSD_PCT_INT - zs_semids_usage_all_pct(u);
			break;
		case ZS_USER_SYSTEM:
			v = 0;
			break;
		default:
			abort();
		}
		break;
	case ZS_RESOURCE_MSG_IDS:
		switch (user) {
		case ZS_USER_ALL:
		case ZS_USER_ZONES:
			v = zs_msgids_usage_all_pct(u);
			break;
		case ZS_USER_FREE:
			v = ZSD_PCT_INT - zs_msgids_usage_all_pct(u);
			break;
		case ZS_USER_SYSTEM:
			v = 0;
			break;
		default:
			abort();
		}
		break;
	case ZS_RESOURCE_LOFI:
		switch (user) {
		case ZS_USER_ALL:
		case ZS_USER_ZONES:
			v = zs_lofi_usage_all_pct(u);
			break;
		case ZS_USER_FREE:
			v = ZSD_PCT_INT - zs_lofi_usage_all_pct(u);
			break;
		case ZS_USER_SYSTEM:
			v = 0;
			break;
		default:
			abort();
		}
		break;
	case ZS_RESOURCE_NET_PHYS_ALL:
		switch (user) {
		case ZS_USER_ALL:
		case ZS_USER_ZONES:
			v = zs_pbytes_usage_all_pct(u);
			break;
		case ZS_USER_FREE:
		case ZS_USER_SYSTEM:
			v = 0;
			break;
		default:
			abort();
		}
		break;

	default:
		abort();
	}

	return (v);
}

/*
 * Get resource used by individual zone.
 */
uint64_t
zs_resource_used_zone_uint64(zs_zone_t zin, zs_resource_t res)
{
	struct zs_zone *z = (struct zs_zone *)zin;
	uint64_t v;

	switch (res)  {
	case ZS_RESOURCE_CPU:
		v = zs_cpu_usage_zone_cpu(z);
		break;
	case ZS_RESOURCE_RAM_RSS:
		v = zs_physical_memory_usage_zone(z);
		break;
	case ZS_RESOURCE_RAM_LOCKED:
		v = zs_locked_memory_usage_zone(z);
		break;
	case ZS_RESOURCE_VM:
		v = zs_virtual_memory_usage_zone(z);
		break;
	case ZS_RESOURCE_DISK_SWAP:
		abort();
		break;
	case ZS_RESOURCE_LWPS:
		v = zs_lwps_usage_zone(z);
		break;
	case ZS_RESOURCE_PROCESSES:
		v = zs_processes_usage_zone(z);
		break;
	case ZS_RESOURCE_SHM_MEMORY:
		v = zs_shm_usage_zone(z);
		break;
	case ZS_RESOURCE_SHM_IDS:
		v = zs_shmids_usage_zone(z);
		break;
	case ZS_RESOURCE_SEM_IDS:
		v = zs_semids_usage_zone(z);
		break;
	case ZS_RESOURCE_MSG_IDS:
		v = zs_msgids_usage_zone(z);
		break;
	case ZS_RESOURCE_LOFI:
		v = zs_lofi_usage_zone(z);
		break;
	case ZS_RESOURCE_NET_PHYS_ALL:
		v = zs_pbytes_usage_zone(z);
		break;
	default:
		abort();
	}
	return (v);
}

/*
 * Get resource used by individual zone as percent
 */
uint_t
zs_resource_used_zone_pct(zs_zone_t zin, zs_resource_t res)
{
	struct zs_zone *z = (struct zs_zone *)zin;
	uint_t v;

	switch (res)  {
	case ZS_RESOURCE_CPU:
		v = zs_cpu_usage_zone_pct(z);
		break;
	case ZS_RESOURCE_RAM_RSS:
		v = zs_physical_memory_usage_zone_pct(z);
		break;
	case ZS_RESOURCE_RAM_LOCKED:
		v = zs_locked_memory_usage_zone_pct(z);
		break;
	case ZS_RESOURCE_VM:
		v = zs_virtual_memory_usage_zone_pct(z);
		break;
	case ZS_RESOURCE_DISK_SWAP:
		abort();
		break;
	case ZS_RESOURCE_LWPS:
		v = zs_lwps_usage_zone_pct(z);
		break;
	case ZS_RESOURCE_PROCESSES:
		v = zs_processes_usage_zone_pct(z);
		break;
	case ZS_RESOURCE_SHM_MEMORY:
		v = zs_shm_usage_zone_pct(z);
		break;
	case ZS_RESOURCE_SHM_IDS:
		v = zs_shmids_usage_zone_pct(z);
		break;
	case ZS_RESOURCE_SEM_IDS:
		v = zs_semids_usage_zone_pct(z);
		break;
	case ZS_RESOURCE_MSG_IDS:
		v = zs_msgids_usage_zone_pct(z);
		break;
	case ZS_RESOURCE_LOFI:
		v = zs_lofi_usage_zone_pct(z);
		break;
	case ZS_RESOURCE_NET_PHYS_ALL:
		v = zs_pbytes_usage_zone_pct(z);
		break;
	default:
		abort();
	}
	return (v);
}

/*
 * Get total time available for a resource
 */
void
zs_resource_total_time(zs_usage_t uin, zs_resource_t res, timestruc_t *t)
{
	struct zs_usage *u = (struct zs_usage *)uin;

	switch (res)  {
	case ZS_RESOURCE_CPU:
		zs_cpu_total_time(u, t);
		break;
	case ZS_RESOURCE_RAM_RSS:
	case ZS_RESOURCE_RAM_LOCKED:
	case ZS_RESOURCE_VM:
	case ZS_RESOURCE_DISK_SWAP:
	case ZS_RESOURCE_LWPS:
	case ZS_RESOURCE_PROCESSES:
	case ZS_RESOURCE_SHM_MEMORY:
	case ZS_RESOURCE_SHM_IDS:
	case ZS_RESOURCE_SEM_IDS:
	case ZS_RESOURCE_MSG_IDS:
	case ZS_RESOURCE_LOFI:
		/* FALLTHROUGH */
	default:
		abort();
	}
}

/*
 * Get total time used for a resource
 */
void
zs_resource_used_time(zs_usage_t uin, zs_resource_t res, zs_user_t user,
    timestruc_t *t)
{
	struct zs_usage *u = (struct zs_usage *)uin;

	switch (res)  {
	case ZS_RESOURCE_CPU:
		switch (user) {
		case ZS_USER_ALL:
			zs_cpu_usage_all(u, t);
			break;
		case ZS_USER_SYSTEM:
			zs_cpu_usage_kernel(u, t);
			break;
		case ZS_USER_ZONES:
			zs_cpu_usage_zones(u, t);
			break;
		case ZS_USER_FREE:
			zs_cpu_usage_idle(u, t);
			break;
		default:
			abort();
		}
		break;
	case ZS_RESOURCE_RAM_RSS:
	case ZS_RESOURCE_RAM_LOCKED:
	case ZS_RESOURCE_VM:
	case ZS_RESOURCE_DISK_SWAP:
	case ZS_RESOURCE_LWPS:
	case ZS_RESOURCE_PROCESSES:
	case ZS_RESOURCE_SHM_MEMORY:
	case ZS_RESOURCE_SHM_IDS:
	case ZS_RESOURCE_SEM_IDS:
	case ZS_RESOURCE_MSG_IDS:
	case ZS_RESOURCE_LOFI:
		/* FALLTHROUGH */
	default:
		abort();
	}
}

/*
 * Get total resource time used for a particular zone
 */
void
zs_resource_used_zone_time(zs_zone_t zin, zs_resource_t res, timestruc_t *t)
{
	struct zs_zone *z = (struct zs_zone *)zin;

	switch (res)  {
	case ZS_RESOURCE_CPU:
		zs_cpu_usage_zone(z, t);
		break;
	case ZS_RESOURCE_RAM_RSS:
	case ZS_RESOURCE_RAM_LOCKED:
	case ZS_RESOURCE_VM:
	case ZS_RESOURCE_DISK_SWAP:
	case ZS_RESOURCE_SHM_MEMORY:
	case ZS_RESOURCE_LWPS:
	case ZS_RESOURCE_PROCESSES:
	case ZS_RESOURCE_SHM_IDS:
	case ZS_RESOURCE_SEM_IDS:
	case ZS_RESOURCE_MSG_IDS:
	case ZS_RESOURCE_LOFI:
		/* FALLTHROUGH */
	default:
		abort();
	}
}


int
zs_zone_list(zs_usage_t uin, zs_zone_t *zonelistin, int num)
{
	int i = 0;
	struct zs_usage *usage = (struct zs_usage *)uin;
	struct zs_zone **zonelist = (struct zs_zone **)zonelistin;
	struct zs_zone *zone, *tmp;

	/* copy what fits of the zone list into the buffer */
	for (zone = list_head(&usage->zsu_zone_list); zone != NULL;
	    zone = list_next(&usage->zsu_zone_list, zone)) {

		/* put the global zone at the first position */
		if (i < num) {
			if (zone->zsz_id == GLOBAL_ZONEID) {
				tmp = zonelist[0];
				zonelist[i] = tmp;
				zonelist[0] = zone;
			} else {
				zonelist[i] = zone;
			}
		}
		i++;
	}
	return (i);
}

zs_zone_t
zs_zone_walk(zs_usage_t uin, zs_zone_t zin)
{
	struct zs_usage *usage = (struct zs_usage *)uin;
	struct zs_zone *zone = (struct zs_zone *)zin;

	if (zone == NULL)
		return (list_head(&usage->zsu_zone_list));

	return (list_next(&usage->zsu_zone_list, zone));
}

/*
 * Test if zone property is supported.
 */
boolean_t
zs_zone_property_supported(zs_zone_property_t prop)
{
	switch (prop) {
	case ZS_ZONE_PROP_NAME:
	case ZS_ZONE_PROP_ID:
	case ZS_ZONE_PROP_IPTYPE:
	case ZS_ZONE_PROP_CPUTYPE:
	case ZS_ZONE_PROP_SCHEDULERS:
	case ZS_ZONE_PROP_CPU_SHARES:
	case ZS_ZONE_PROP_POOLNAME:
	case ZS_ZONE_PROP_PSETNAME:
	case ZS_ZONE_PROP_DEFAULT_SCHED:
		return (B_TRUE);
	default:
		return (B_FALSE);
	}
}

/*
 * Gets a zone property
 */
zs_property_t
zs_zone_property(zs_zone_t zin, zs_zone_property_t prop)
{
	struct zs_zone *zone = (struct zs_zone *)zin;
	struct zs_property *p;

	switch (prop) {
	case ZS_ZONE_PROP_NAME:
		p = &zone->zsz_prop_name;
		p->zsp_type = DATA_TYPE_STRING;
		p->zsp_v.zsv_string = zs_zone_name(zone);
		break;
	case ZS_ZONE_PROP_ID:
		p = &zone->zsz_prop_id;
		p->zsp_type = DATA_TYPE_INT32;
		p->zsp_v.zsv_int = zs_zone_id(zone);
		break;
	case ZS_ZONE_PROP_IPTYPE:
		p = &zone->zsz_prop_iptype;
		p->zsp_type = DATA_TYPE_UINT32;
		p->zsp_v.zsv_uint = zs_zone_iptype(zone);
		break;
	case ZS_ZONE_PROP_CPUTYPE:
		p = &zone->zsz_prop_cputype;
		p->zsp_type = DATA_TYPE_UINT32;
		p->zsp_v.zsv_uint = zs_zone_cputype(zone);
		break;
	case ZS_ZONE_PROP_SCHEDULERS:
		p = &zone->zsz_prop_schedulers;
		p->zsp_type = DATA_TYPE_UINT32;
		p->zsp_v.zsv_uint = zs_zone_schedulers(zone);
		break;
	case ZS_ZONE_PROP_CPU_SHARES:
		p = &zone->zsz_prop_cpushares;
		p->zsp_type = DATA_TYPE_UINT64;
		p->zsp_v.zsv_uint64 = zs_zone_cpu_shares(zone);
		break;
	case ZS_ZONE_PROP_POOLNAME:
		p = &zone->zsz_prop_poolname;
		p->zsp_type = DATA_TYPE_STRING;
		p->zsp_v.zsv_string = zs_zone_poolname(zone);
		break;
	case ZS_ZONE_PROP_PSETNAME:
		p = &zone->zsz_prop_psetname;
		p->zsp_type = DATA_TYPE_STRING;
		p->zsp_v.zsv_string = zs_zone_psetname(zone);
		break;
	case ZS_ZONE_PROP_DEFAULT_SCHED:
		p = &zone->zsz_prop_defsched;
		p->zsp_type = DATA_TYPE_UINT32;
		p->zsp_v.zsv_uint = zs_zone_default_sched(zone);
		break;
	default:
		abort();
	}
	return ((zs_property_t)p);
}

boolean_t
zs_zone_limit_supported(zs_limit_t limit)
{
	switch (limit) {
	case ZS_LIMIT_CPU:
	case ZS_LIMIT_CPU_SHARES:
	case ZS_LIMIT_RAM_RSS:
	case ZS_LIMIT_RAM_LOCKED:
	case ZS_LIMIT_VM:
	case ZS_LIMIT_SHM_MEMORY:
	case ZS_LIMIT_LWPS:
	case ZS_LIMIT_PROCESSES:
	case ZS_LIMIT_SHM_IDS:
	case ZS_LIMIT_MSG_IDS:
	case ZS_LIMIT_SEM_IDS:
	case ZS_LIMIT_LOFI:
		return (B_TRUE);
	default:
		return (B_FALSE);
	}
}

zs_limit_type_t
zs_zone_limit_type(zs_limit_t limit)
{
	switch (limit) {
	case ZS_LIMIT_CPU:
	case ZS_LIMIT_CPU_SHARES:
		return (ZS_LIMIT_TYPE_TIME);
	case ZS_LIMIT_RAM_RSS:
	case ZS_LIMIT_RAM_LOCKED:
	case ZS_LIMIT_VM:
	case ZS_LIMIT_SHM_MEMORY:
		return (ZS_LIMIT_TYPE_BYTES);
	case ZS_LIMIT_LWPS:
	case ZS_LIMIT_PROCESSES:
	case ZS_LIMIT_SHM_IDS:
	case ZS_LIMIT_MSG_IDS:
	case ZS_LIMIT_SEM_IDS:
	case ZS_LIMIT_LOFI:
		return (ZS_LIMIT_TYPE_COUNT);
	default:
		abort();
	}
	return (0);
}
/*
 * Gets the zones limit.  Returns ZS_LIMIT_NONE if no limit set.
 */
uint64_t
zs_zone_limit_uint64(zs_zone_t zin, zs_limit_t limit)
{
	struct zs_zone *z = (struct zs_zone *)zin;
	uint64_t v;

	switch (limit) {
	case ZS_LIMIT_CPU:
		v = zs_zone_cpu_cap(z);
		break;
	case ZS_LIMIT_CPU_SHARES:
		v = zs_zone_cpu_shares(z);
		break;
	case ZS_LIMIT_RAM_RSS:
		v = zs_zone_physical_memory_cap(z);
		break;
	case ZS_LIMIT_RAM_LOCKED:
		v = zs_zone_locked_memory_cap(z);
		break;
	case ZS_LIMIT_VM:
		v = zs_zone_virtual_memory_cap(z);
		break;
	case ZS_LIMIT_LWPS:
		v = z->zsz_lwps_cap;
		break;
	case ZS_LIMIT_PROCESSES:
		v = z->zsz_processes_cap;
		break;
	case ZS_LIMIT_SHM_MEMORY:
		v = z->zsz_shm_cap;
		break;
	case ZS_LIMIT_SHM_IDS:
		v = z->zsz_shmids_cap;
		break;
	case ZS_LIMIT_SEM_IDS:
		v = z->zsz_semids_cap;
		break;
	case ZS_LIMIT_MSG_IDS:
		v = z->zsz_msgids_cap;
		break;
	case ZS_LIMIT_LOFI:
		v = z->zsz_lofi_cap;
		break;
	default:
		abort();
	}
	return (v);
}

/*
 * Gets the amount of resource used for a limit.  Returns ZS_LIMIT_NONE if
 * no limit configured.
 */
uint64_t
zs_zone_limit_used_uint64(zs_zone_t zin, zs_limit_t limit)
{
	struct zs_zone *z = (struct zs_zone *)zin;
	uint64_t v;

	switch (limit) {
	case ZS_LIMIT_CPU:
		v = zs_zone_cpu_cap_used(z);
		break;
	case ZS_LIMIT_CPU_SHARES:
		v = zs_zone_cpu_shares_used(z);
		break;
	case ZS_LIMIT_RAM_RSS:
		v = zs_zone_physical_memory_cap_used(z);
		break;
	case ZS_LIMIT_RAM_LOCKED:
		v = zs_zone_locked_memory_cap_used(z);
		break;
	case ZS_LIMIT_VM:
		v = zs_zone_virtual_memory_cap_used(z);
		break;
	case ZS_LIMIT_LWPS:
		v = z->zsz_lwps;
		break;
	case ZS_LIMIT_PROCESSES:
		v = z->zsz_processes;
		break;
	case ZS_LIMIT_SHM_MEMORY:
		v = z->zsz_shm;
		break;
	case ZS_LIMIT_SHM_IDS:
		v = z->zsz_shmids;
		break;
	case ZS_LIMIT_SEM_IDS:
		v = z->zsz_semids;
		break;
	case ZS_LIMIT_MSG_IDS:
		v = z->zsz_msgids;
		break;
	case ZS_LIMIT_LOFI:
		v = z->zsz_lofi;
		break;
	default:
		abort();
	}
	return (v);
}

/*
 * Gets time used under limit.  Time is zero if no limit is configured
 */
void
zs_zone_limit_time(zs_zone_t zin, zs_limit_t limit, timestruc_t *v)
{
	struct zs_zone *z = (struct zs_zone *)zin;

	switch (limit) {
	case ZS_LIMIT_CPU:
		if (z->zsz_cpu_cap == ZS_LIMIT_NONE) {
			v->tv_sec = 0;
			v->tv_nsec = 0;
			break;
		}
		zs_zone_cpu_cap_time(z, v);
		break;
	case ZS_LIMIT_CPU_SHARES:
		if (z->zsz_cpu_shares == ZS_LIMIT_NONE ||
		    z->zsz_cpu_shares == ZS_SHARES_UNLIMITED ||
		    z->zsz_cpu_shares == 0 ||
		    (z->zsz_scheds & ZS_SCHED_FSS) == 0) {
			v->tv_sec = 0;
			v->tv_nsec = 0;
			break;
		}
		zs_zone_cpu_share_time(z, v);
		break;
	case ZS_LIMIT_RAM_RSS:
	case ZS_LIMIT_RAM_LOCKED:
	case ZS_LIMIT_VM:
	case ZS_LIMIT_SHM_MEMORY:
	case ZS_LIMIT_LWPS:
	case ZS_LIMIT_PROCESSES:
	case ZS_LIMIT_SHM_IDS:
	case ZS_LIMIT_MSG_IDS:
	case ZS_LIMIT_SEM_IDS:
	case ZS_LIMIT_LOFI:
		/* FALLTHROUGH */
	default:
		abort();
	}
}

/*
 * Errno is set on error:
 *
 *	EINVAL: No such property
 *	ENOENT: No time value for the specified limit.
 *	ESRCH:  No limit is configured.
 *
 * If no limit is configured, the value will be ZS_PCT_NONE
 */
void
zs_zone_limit_used_time(zs_zone_t zin, zs_limit_t limit, timestruc_t *t)
{
	struct zs_zone *z = (struct zs_zone *)zin;

	switch (limit) {
	case ZS_LIMIT_CPU:
		if (z->zsz_cpu_cap == ZS_LIMIT_NONE) {
			t->tv_sec = 0;
			t->tv_nsec = 0;
			break;
		}
		zs_zone_cpu_cap_time_used(z, t);
		break;
	case ZS_LIMIT_CPU_SHARES:
		if (z->zsz_cpu_shares == ZS_LIMIT_NONE ||
		    z->zsz_cpu_shares == ZS_SHARES_UNLIMITED ||
		    z->zsz_cpu_shares == 0 ||
		    (z->zsz_scheds & ZS_SCHED_FSS) == 0) {
			t->tv_sec = 0;
			t->tv_nsec = 0;
			break;
		}
		zs_zone_cpu_share_time_used(z, t);
		break;
	case ZS_LIMIT_RAM_RSS:
	case ZS_LIMIT_RAM_LOCKED:
	case ZS_LIMIT_VM:
	case ZS_LIMIT_SHM_MEMORY:
	case ZS_LIMIT_LWPS:
	case ZS_LIMIT_PROCESSES:
	case ZS_LIMIT_SHM_IDS:
	case ZS_LIMIT_MSG_IDS:
	case ZS_LIMIT_SEM_IDS:
	case ZS_LIMIT_LOFI:
		/* FALLTHROUGH */
	default:
		abort();
	}
}

/*
 * Get a zones usage as a percent of the limit.  Return ZS_PCT_NONE if
 * no limit is configured.
 */
uint_t
zs_zone_limit_used_pct(zs_zone_t zin, zs_limit_t limit)
{
	struct zs_zone *z = (struct zs_zone *)zin;
	uint_t v;

	switch (limit) {
	case ZS_LIMIT_CPU:
		v = zs_zone_cpu_cap_pct(z);
		break;
	case ZS_LIMIT_CPU_SHARES:
		v = zs_zone_cpu_shares_pct(z);
		break;
	case ZS_LIMIT_RAM_RSS:
		v = zs_zone_physical_memory_cap_pct(z);
		break;
	case ZS_LIMIT_RAM_LOCKED:
		v = zs_zone_locked_memory_cap_pct(z);
		break;
	case ZS_LIMIT_VM:
		v = zs_zone_virtual_memory_cap_pct(z);
		break;
	case ZS_LIMIT_LWPS:
		v = zs_lwps_zone_cap_pct(z);
		break;
	case ZS_LIMIT_PROCESSES:
		v = zs_processes_zone_cap_pct(z);
		break;
	case ZS_LIMIT_SHM_MEMORY:
		v = zs_shm_zone_cap_pct(z);
		break;
	case ZS_LIMIT_SHM_IDS:
		v = zs_shmids_zone_cap_pct(z);
		break;
	case ZS_LIMIT_SEM_IDS:
		v = zs_semids_zone_cap_pct(z);
		break;
	case ZS_LIMIT_MSG_IDS:
		v = zs_msgids_zone_cap_pct(z);
		break;
	case ZS_LIMIT_LOFI:
		v = zs_lofi_zone_cap_pct(z);
		break;
	default:
		abort();
	}
	return (v);
}

int
zs_pset_list(zs_usage_t uin, zs_pset_t *psetlistin, int num)
{
	struct zs_usage *usage = (struct zs_usage *)uin;
	struct zs_pset **psetlist = (struct zs_pset **)psetlistin;
	int i = 0;
	struct zs_pset *pset, *tmp;

	/* copy what fits of the pset list into the buffer */
	for (pset = list_head(&usage->zsu_pset_list); pset != NULL;
	    pset = list_next(&usage->zsu_pset_list, pset)) {

		/* put the default pset at the first position */
		if (i < num) {
			if (pset->zsp_id == ZS_PSET_DEFAULT) {
				tmp = psetlist[0];
				psetlist[i] = tmp;
				psetlist[0] = pset;
			} else {
				psetlist[i] = pset;
			}
		}
		i++;
	}
	return (i);
}

zs_pset_t
zs_pset_walk(zs_usage_t uin, zs_pset_t pin)
{
	struct zs_usage *usage = (struct zs_usage *)uin;
	struct zs_pset *pset = (struct zs_pset *)pin;

	if (pset == NULL)
		return (list_head(&usage->zsu_pset_list));

	return (list_next(&usage->zsu_pset_list, pset));
}

/*
 * Test if pset property is supported
 */
boolean_t
zs_pset_property_supported(zs_pset_property_t prop)
{
	switch (prop) {
	case ZS_PSET_PROP_NAME:
	case ZS_PSET_PROP_ID:
	case ZS_PSET_PROP_CPUTYPE:
	case ZS_PSET_PROP_SIZE:
	case ZS_PSET_PROP_ONLINE:
	case ZS_PSET_PROP_MIN:
	case ZS_PSET_PROP_MAX:
	case ZS_PSET_PROP_CPU_SHARES:
	case ZS_PSET_PROP_SCHEDULERS:
	case ZS_PSET_PROP_LOAD_1MIN:
	case ZS_PSET_PROP_LOAD_5MIN:
	case ZS_PSET_PROP_LOAD_15MIN:
		return (B_TRUE);
	default:
		return (B_FALSE);
	}
}

/*
 * Test if psets support usage for the given user
 */
boolean_t
zs_pset_user_supported(zs_user_t user)
{
	switch (user) {
	case ZS_USER_ALL:
	case ZS_USER_SYSTEM:
	case ZS_USER_ZONES:
	case ZS_USER_FREE:
		return (B_TRUE);
	default:
		return (B_FALSE);
	}
}

/*
 * Get various properties on a pset.
 */
zs_property_t
zs_pset_property(zs_pset_t pin, zs_pset_property_t prop)
{
	struct zs_pset *pset = (struct zs_pset *)pin;
	struct zs_property *p;

	switch (prop) {
	case ZS_PSET_PROP_NAME:
		p = &pset->zsp_prop_name;
		p->zsp_type = DATA_TYPE_STRING;
		p->zsp_v.zsv_string = zs_pset_name(pset);
		break;
	case ZS_PSET_PROP_ID:
		p = &pset->zsp_prop_id;
		p->zsp_type = DATA_TYPE_INT32;
		p->zsp_v.zsv_int = zs_pset_id(pset);
		break;
	case ZS_PSET_PROP_CPUTYPE:
		p = &pset->zsp_prop_cputype;
		p->zsp_type = DATA_TYPE_UINT32;
		p->zsp_v.zsv_uint = zs_pset_cputype(pset);
		break;
	case ZS_PSET_PROP_SIZE:
		p = &pset->zsp_prop_size;
		p->zsp_type = DATA_TYPE_UINT64;
		p->zsp_v.zsv_uint64 = zs_pset_size(pset);
		break;
	case ZS_PSET_PROP_ONLINE:
		p = &pset->zsp_prop_online;
		p->zsp_type = DATA_TYPE_UINT64;
		p->zsp_v.zsv_uint64 = zs_pset_online(pset);
		break;
	case ZS_PSET_PROP_MIN:
		p = &pset->zsp_prop_min;
		p->zsp_type = DATA_TYPE_UINT64;
		p->zsp_v.zsv_uint64 = zs_pset_min(pset);
		break;
	case ZS_PSET_PROP_MAX:
		p = &pset->zsp_prop_max;
		p->zsp_type = DATA_TYPE_UINT64;
		p->zsp_v.zsv_uint64 = zs_pset_max(pset);
		break;
	case ZS_PSET_PROP_CPU_SHARES:
		p = &pset->zsp_prop_cpushares;
		p->zsp_type = DATA_TYPE_UINT64;
		p->zsp_v.zsv_uint64 = zs_pset_cpu_shares(pset);
		break;
	case ZS_PSET_PROP_SCHEDULERS:
		p = &pset->zsp_prop_schedulers;
		p->zsp_type = DATA_TYPE_UINT32;
		p->zsp_v.zsv_uint = zs_pset_schedulers(pset);
		break;
	case ZS_PSET_PROP_LOAD_1MIN:
		p = &pset->zsp_prop_1min;
		p->zsp_type = DATA_TYPE_DOUBLE;
		p->zsp_v.zsv_double = zs_pset_load(pset, LOADAVG_1MIN);
		break;
	case ZS_PSET_PROP_LOAD_5MIN:
		p = &pset->zsp_prop_5min;
		p->zsp_type = DATA_TYPE_DOUBLE;
		p->zsp_v.zsv_double = zs_pset_load(pset, LOADAVG_5MIN);
		break;
	case ZS_PSET_PROP_LOAD_15MIN:
		p = &pset->zsp_prop_15min;
		p->zsp_type = DATA_TYPE_DOUBLE;
		p->zsp_v.zsv_double = zs_pset_load(pset, LOADAVG_15MIN);
		break;
	default:
		abort();
	}
	return ((zs_property_t)p);
}

void
zs_pset_total_time(zs_pset_t pin, timestruc_t *t)
{
	struct zs_pset *pset = (struct zs_pset *)pin;

	*t = pset->zsp_total_time;
}

uint64_t
zs_pset_total_cpus(zs_pset_t pin)
{
	struct zs_pset *pset = (struct zs_pset *)pin;

	return (pset->zsp_online * ZSD_ONE_CPU);
}

/*
 * Get total time used for pset
 */
void
zs_pset_used_time(zs_pset_t pin, zs_user_t user, timestruc_t *t)
{
	struct zs_pset *pset = (struct zs_pset *)pin;

	switch (user) {
	case ZS_USER_ALL:
		zs_pset_usage_all(pset, t);
		break;
	case ZS_USER_SYSTEM:
		zs_pset_usage_kernel(pset, t);
		break;
	case ZS_USER_ZONES:
		zs_pset_usage_zones(pset, t);
		break;
	case ZS_USER_FREE:
		zs_pset_usage_idle(pset, t);
		break;
	default:
		abort();
	}
}

/*
 * Get cpus used for pset
 */
uint64_t
zs_pset_used_cpus(zs_pset_t pin, zs_user_t user)
{
	struct zs_pset *pset = (struct zs_pset *)pin;
	uint_t v;

	switch (user) {
	case ZS_USER_ALL:
		v = zs_pset_usage_all_cpus(pset);
		break;
	case ZS_USER_SYSTEM:
		v = zs_pset_usage_kernel_cpus(pset);
		break;
	case ZS_USER_ZONES:
		v = zs_pset_usage_zones_cpus(pset);
		break;
	case ZS_USER_FREE:
		v = zs_pset_usage_idle_cpus(pset);
		break;
	default:
		abort();
	}
	return (v);
}
/*
 * Get percent of pset cpu time used
 */
uint_t
zs_pset_used_pct(zs_pset_t pin, zs_user_t user)
{
	struct zs_pset *pset = (struct zs_pset *)pin;
	uint_t v;

	switch (user) {
	case ZS_USER_ALL:
		v = zs_pset_usage_all_pct(pset);
		break;
	case ZS_USER_SYSTEM:
		v = zs_pset_usage_kernel_pct(pset);
		break;
	case ZS_USER_ZONES:
		v = zs_pset_usage_zones_pct(pset);
		break;
	case ZS_USER_FREE:
		v = zs_pset_usage_idle_pct(pset);
		break;
	default:
		abort();
	}
	return (v);
}

int
zs_pset_zone_list(zs_pset_t pin, zs_pset_zone_t *zonelistin, int num)
{
	struct zs_pset *pset = (struct zs_pset *)pin;
	struct zs_pset_zone **zonelist = (struct zs_pset_zone **)zonelistin;
	int i = 0;
	struct zs_pset_zone *zone, *tmp;

	/* copy what fits of the pset's zone list into the buffer */
	for (zone = list_head(&pset->zsp_usage_list); zone != NULL;
	    zone = list_next(&pset->zsp_usage_list, zone)) {

		/* put the global zone at the first position */
		if (i < num) {
			if (zone->zspz_zone->zsz_id == GLOBAL_ZONEID) {
				tmp = zonelist[0];
				zonelist[i] = tmp;
				zonelist[0] = zone;
			} else {
				zonelist[i] = zone;
			}
		}
		i++;
	}
	return (i);
}

zs_pset_zone_t
zs_pset_zone_walk(zs_pset_t pin, zs_pset_zone_t pzin)
{
	struct zs_pset *pset = (struct zs_pset *)pin;
	struct zs_pset_zone *pz = (struct zs_pset_zone *)pzin;

	if (pz == NULL)
		return (list_head(&pset->zsp_usage_list));

	return (list_next(&pset->zsp_usage_list, pz));
}

zs_pset_t
zs_pset_zone_get_pset(zs_pset_zone_t pzin)
{
	struct zs_pset_zone *pz = (struct zs_pset_zone *)pzin;

	return ((zs_pset_t)pz->zspz_pset);
}

zs_zone_t
zs_pset_zone_get_zone(zs_pset_zone_t pzin)
{
	struct zs_pset_zone *pz = (struct zs_pset_zone *)pzin;

	return ((zs_zone_t)pz->zspz_zone);
}

/*
 * Test if property is supported.
 */
boolean_t
zs_pset_zone_property_supported(zs_pz_property_t prop)
{
	switch (prop) {
	case ZS_PZ_PROP_CPU_CAP:
	case ZS_PZ_PROP_CPU_SHARES:
	case ZS_PZ_PROP_SCHEDULERS:
		return (B_TRUE);
	default:
		return (B_FALSE);
	}
}

/*
 * Get a property describing a zone's usage of a pset
 */
zs_property_t
zs_pset_zone_property(zs_pset_zone_t pzin, zs_pz_property_t prop)
{
	struct zs_pset_zone *pz = (struct zs_pset_zone *)pzin;
	struct zs_property *p;

	switch (prop) {
	case ZS_PZ_PROP_CPU_CAP:
		p = &pz->zspz_prop_cpucap;
		p->zsp_type = DATA_TYPE_UINT64;
		p->zsp_v.zsv_uint64 = (int)zs_pset_zone_cpu_cap(pz);
		break;
	case ZS_PZ_PROP_CPU_SHARES:
		p = &pz->zspz_prop_cpushares;
		p->zsp_type = DATA_TYPE_UINT64;
		p->zsp_v.zsv_uint64 = (int)zs_pset_zone_cpu_shares(pz);
		break;
	case ZS_PZ_PROP_SCHEDULERS:
		p = &pz->zspz_prop_schedulers;
		p->zsp_type = DATA_TYPE_UINT32;
		p->zsp_v.zsv_uint = (int)zs_pset_zone_schedulers(pz);
		break;
	default:
		abort();
	}
	return ((zs_property_t)p);
}

void
zs_pset_zone_used_time(zs_pset_zone_t pzin, timestruc_t *t)
{
	struct zs_pset_zone *pz = (struct zs_pset_zone *)pzin;

	zs_pset_zone_usage_time(pz, t);
}

uint64_t
zs_pset_zone_used_cpus(zs_pset_zone_t pzin)
{
	struct zs_pset_zone *pz = (struct zs_pset_zone *)pzin;

	return (zs_pset_zone_usage_cpus(pz));
}

/*
 * Test if percent for a pset_zone is supported
 */
boolean_t
zs_pset_zone_pct_supported(zs_pz_pct_t pct)
{
	switch (pct) {
	case ZS_PZ_PCT_PSET:
	case ZS_PZ_PCT_CPU_CAP:
	case ZS_PZ_PCT_PSET_SHARES:
	case ZS_PZ_PCT_CPU_SHARES:
		return (B_TRUE);
	default:
		return (B_FALSE);
	}
}

/*
 * Get percent of a psets cpus used by a zone
 */
uint_t
zs_pset_zone_used_pct(zs_pset_zone_t pzin, zs_pz_pct_t pct)
{
	struct zs_pset_zone *pz = (struct zs_pset_zone *)pzin;

	uint_t v;

	switch (pct) {
	case ZS_PZ_PCT_PSET:
		v = zs_pset_zone_usage_pct_pset(pz);
		break;
	case ZS_PZ_PCT_CPU_CAP:
		v = zs_pset_zone_usage_pct_cpu_cap(pz);
		break;
	case ZS_PZ_PCT_PSET_SHARES:
		v = zs_pset_zone_usage_pct_pset_shares(pz);
		break;
	case ZS_PZ_PCT_CPU_SHARES:
		v = zs_pset_zone_usage_pct_cpu_shares(pz);
		break;
	default:
		abort();
	}
	return (v);
}

data_type_t
zs_property_type(zs_property_t pin)
{
	struct zs_property *p = (struct zs_property *)pin;

	return (p->zsp_type);
}

char *
zs_property_string(zs_property_t pin)
{
	struct zs_property *p = (struct zs_property *)pin;

	assert(p->zsp_type == DATA_TYPE_STRING);
	return (p->zsp_v.zsv_string);
}

double
zs_property_double(zs_property_t pin)
{
	struct zs_property *p = (struct zs_property *)pin;

	assert(p->zsp_type == DATA_TYPE_DOUBLE);
	return (p->zsp_v.zsv_double);
}

uint64_t
zs_property_uint64(zs_property_t pin)
{
	struct zs_property *p = (struct zs_property *)pin;

	assert(p->zsp_type == DATA_TYPE_UINT64);
	return (p->zsp_v.zsv_uint64);
}

int64_t
zs_property_int64(zs_property_t pin)
{
	struct zs_property *p = (struct zs_property *)pin;

	assert(p->zsp_type == DATA_TYPE_INT64);
	return (p->zsp_v.zsv_int64);
}

uint_t
zs_property_uint(zs_property_t pin)
{
	struct zs_property *p = (struct zs_property *)pin;

	assert(p->zsp_type == DATA_TYPE_UINT32);
	return (p->zsp_v.zsv_uint);
}

int
zs_property_int(zs_property_t pin)
{
	struct zs_property *p = (struct zs_property *)pin;

	assert(p->zsp_type == DATA_TYPE_INT32);
	return (p->zsp_v.zsv_uint);
}

int
zs_datalink_list(zs_usage_t uin, zs_datalink_t *linklistin, int num)
{
	int i = 0;
	struct zs_usage *usage = (struct zs_usage *)uin;
	struct zs_datalink **linklist = (struct zs_datalink **)linklistin;
	struct zs_datalink *link;

	/* copy what fits of the datalink list into the buffer */
	for (link = list_head(&usage->zsu_datalink_list); link != NULL;
	    link = list_next(&usage->zsu_datalink_list, link)) {
		if (i < num)
			linklist[i] = link;
		i++;
	}
	return (i);
}

int
zs_vlink_list(zs_datalink_t linkin, zs_datalink_t *vlinklistin, int num)
{
	int i = 0;
	struct zs_datalink *link = (struct zs_datalink *)linkin;
	struct zs_datalink **vlinklist = (struct zs_datalink **)vlinklistin;
	struct zs_datalink *vlink;

	/* copy what fits of the client list into the buffer */
	for (vlink = list_head(&link->zsl_vlink_list); vlink != NULL;
	    vlink = list_next(&link->zsl_vlink_list, vlink)) {
		if (i < num)
			vlinklist[i] = vlink;
		i++;
	}
	return (i);
}

int
zs_link_zone_list(zs_datalink_t linkin, zs_link_zone_t *linklistin,
    int num)
{
	int i = 0;
	struct zs_datalink *link = (struct zs_datalink *)linkin;
	struct zs_link_zone **linklist = (struct zs_link_zone **)linklistin;
	zs_link_zone_t link_zone;

	/* copy what fits of the client list into the buffer */
	for (link_zone = list_head(&link->zsl_zone_list);
	    link_zone != NULL;
	    link_zone = list_next(&link->zsl_zone_list, link_zone)) {
		if (i < num)
			linklist[i] = link_zone;
		i++;
	}
	return (i);
}

char *
zs_link_name(zs_datalink_t link)
{
	return (link->zsl_linkname);
}

char *
zs_link_devname(zs_datalink_t link)
{
	return (link->zsl_devname);
}

char *
zs_link_zonename(zs_datalink_t link)
{
	return (link->zsl_zonename);
}

static uint64_t
zs_link_speed(zs_datalink_t link)
{
	return (link->zsl_speed);
}

char *
zs_link_state(zs_datalink_t link)
{
	return (link->zsl_state);
}

static int
zs_link_class(zs_datalink_t link)
{
	return (link->zsl_class);
}


static uint64_t
zs_link_maxbw(zs_datalink_t link)
{
	return (link->zsl_maxbw);
}

static uint64_t
zs_link_rbytes(zs_datalink_t link)
{
	return (link->zsl_rbytes);
}

static uint64_t
zs_link_obytes(zs_datalink_t link)
{
	return (link->zsl_obytes);
}

static uint64_t
zs_link_prbytes(zs_datalink_t link)
{
	return (link->zsl_prbytes);
}

static uint64_t
zs_link_pobytes(zs_datalink_t link)
{
	return (link->zsl_pobytes);
}

static uint64_t
zs_link_tot_bytes(zs_datalink_t link)
{
	return (link->zsl_total_rbytes + link->zsl_total_obytes);
}

static uint64_t
zs_link_tot_rbytes(zs_datalink_t link)
{
	return (link->zsl_total_rbytes);
}

static uint64_t
zs_link_tot_obytes(zs_datalink_t link)
{
	return (link->zsl_total_obytes);
}

static uint64_t
zs_link_tot_prbytes(zs_datalink_t link)
{
	return (link->zsl_total_prbytes);
}

static uint64_t
zs_link_tot_pobytes(zs_datalink_t link)
{
	return (link->zsl_total_pobytes);
}

/*
 * Get a datalink property
 */
zs_property_t
zs_link_property(zs_datalink_t linkin, zs_datalink_property_t prop)
{
	struct zs_datalink *link = (struct zs_datalink *)linkin;
	struct zs_property *p;

	switch (prop) {
	case ZS_LINK_PROP_NAME:
		p = &link->zsl_prop_linkname;
		p->zsp_type = DATA_TYPE_STRING;
		p->zsp_v.zsv_string = zs_link_name(link);
		break;
	case ZS_LINK_PROP_DEVNAME:
		p = &link->zsl_prop_devname;
		p->zsp_type = DATA_TYPE_STRING;
		p->zsp_v.zsv_string = zs_link_devname(link);
		break;
	case ZS_LINK_PROP_ZONENAME:
		p = &link->zsl_prop_zonename;
		p->zsp_type = DATA_TYPE_STRING;
		p->zsp_v.zsv_string = zs_link_zonename(link);
		break;
	case ZS_LINK_PROP_SPEED:
		p = &link->zsl_prop_speed;
		p->zsp_type = DATA_TYPE_UINT64;
		p->zsp_v.zsv_uint64 = zs_link_speed(link);
		break;
	case ZS_LINK_PROP_STATE:
		p = &link->zsl_prop_state;
		p->zsp_type = DATA_TYPE_STRING;
		p->zsp_v.zsv_string = zs_link_state(link);
		break;
	case ZS_LINK_PROP_CLASS:
		p = &link->zsl_prop_class;
		p->zsp_type = DATA_TYPE_UINT32;
		p->zsp_v.zsv_uint = zs_link_class(link);
		break;
	case ZS_LINK_PROP_RBYTE:
		p = &link->zsl_prop_rbytes;
		p->zsp_type = DATA_TYPE_UINT64;
		p->zsp_v.zsv_uint64 = zs_link_rbytes(link);
		break;
	case ZS_LINK_PROP_OBYTE:
		p = &link->zsl_prop_obytes;
		p->zsp_type = DATA_TYPE_UINT64;
		p->zsp_v.zsv_uint64 = zs_link_obytes(link);
		break;
	case ZS_LINK_PROP_PRBYTES:
		p = &link->zsl_prop_prbytes;
		p->zsp_type = DATA_TYPE_UINT64;
		p->zsp_v.zsv_uint64 = zs_link_prbytes(link);
		break;
	case ZS_LINK_PROP_POBYTES:
		p = &link->zsl_prop_pobytes;
		p->zsp_type = DATA_TYPE_UINT64;
		p->zsp_v.zsv_uint64 = zs_link_pobytes(link);
		break;
	case ZS_LINK_PROP_TOT_BYTES:
		p = &link->zsl_prop_tot_bytes;
		p->zsp_type = DATA_TYPE_UINT64;
		p->zsp_v.zsv_uint64 = zs_link_tot_bytes(link);
		break;
	case ZS_LINK_PROP_TOT_RBYTES:
		p = &link->zsl_prop_tot_rbytes;
		p->zsp_type = DATA_TYPE_UINT64;
		p->zsp_v.zsv_uint64 = zs_link_tot_rbytes(link);
		break;
	case ZS_LINK_PROP_TOT_OBYTES:
		p = &link->zsl_prop_tot_obytes;
		p->zsp_type = DATA_TYPE_UINT64;
		p->zsp_v.zsv_uint64 = zs_link_tot_obytes(link);
		break;
	case ZS_LINK_PROP_TOT_PRBYTES:
		p = &link->zsl_prop_tot_prbytes;
		p->zsp_type = DATA_TYPE_UINT64;
		p->zsp_v.zsv_uint64 = zs_link_tot_prbytes(link);
		break;
	case ZS_LINK_PROP_TOT_POBYTES:
		p = &link->zsl_prop_tot_pobytes;
		p->zsp_type = DATA_TYPE_UINT64;
		p->zsp_v.zsv_uint64 = zs_link_tot_pobytes(link);
		break;
	case ZS_LINK_PROP_MAXBW:
		p = &link->zsl_prop_maxbw;
		p->zsp_type = DATA_TYPE_UINT64;
		p->zsp_v.zsv_uint64 = zs_link_maxbw(link);
		break;
	default:
		abort();
	}
	return ((zs_property_t)p);
}

char *
zs_link_zone_name(zs_link_zone_t link_zone)
{
	return (link_zone->zlz_name);
}

static uint64_t
zs_link_zone_bytes(zs_link_zone_t link_zone)
{
	return (link_zone->zlz_total_bytes);
}

static uint64_t
zs_link_zone_rbytes(zs_link_zone_t link_zone)
{
	return (link_zone->zlz_total_rbytes);
}

static uint64_t
zs_link_zone_obytes(zs_link_zone_t link_zone)
{
	return (link_zone->zlz_total_obytes);
}

static uint64_t
zs_link_zone_prbytes(zs_link_zone_t link_zone)
{
	return (link_zone->zlz_total_prbytes);
}

static uint64_t
zs_link_zone_pobytes(zs_link_zone_t link_zone)
{
	return (link_zone->zlz_total_pobytes);
}

static uint64_t
zs_link_zone_maxbw(zs_link_zone_t link_zone)
{
	return (link_zone->zlz_total_bw);
}

static uint64_t
zs_link_zone_partbw(zs_link_zone_t link_zone)
{
	return (link_zone->zlz_partial_bw);
}

/*
 * Get property of a zone under a datalink
 */
zs_property_t
zs_link_zone_property(zs_link_zone_t link_zonein, zs_lz_property_t prop)
{
	struct zs_link_zone *link_zone = (struct zs_link_zone *)link_zonein;
	struct zs_property *p;

	switch (prop) {
	case ZS_LINK_ZONE_PROP_NAME:
		p = &link_zone->zlz_prop_name;
		p->zsp_type = DATA_TYPE_STRING;
		p->zsp_v.zsv_string =  zs_link_zone_name(link_zone);
		break;
	case ZS_LINK_ZONE_PROP_BYTES:
		p = &link_zone->zlz_prop_bytes;
		p->zsp_type = DATA_TYPE_UINT64;
		p->zsp_v.zsv_uint64 = zs_link_zone_bytes(link_zone);
		break;
	case ZS_LINK_ZONE_PROP_RBYTES:
		p = &link_zone->zlz_prop_rbytes;
		p->zsp_type = DATA_TYPE_UINT64;
		p->zsp_v.zsv_uint64 = zs_link_zone_rbytes(link_zone);
		break;
	case ZS_LINK_ZONE_PROP_OBYTES:
		p = &link_zone->zlz_prop_obytes;
		p->zsp_type = DATA_TYPE_UINT64;
		p->zsp_v.zsv_uint64 = zs_link_zone_obytes(link_zone);
		break;
	case ZS_LINK_ZONE_PROP_PRBYTES:
		p = &link_zone->zlz_prop_prbytes;
		p->zsp_type = DATA_TYPE_UINT64;
		p->zsp_v.zsv_uint64 = zs_link_zone_prbytes(link_zone);
		break;
	case ZS_LINK_ZONE_PROP_POBYTES:
		p = &link_zone->zlz_prop_pobytes;
		p->zsp_type = DATA_TYPE_UINT64;
		p->zsp_v.zsv_uint64 = zs_link_zone_pobytes(link_zone);
		break;
	case ZS_LINK_ZONE_PROP_MAXBW:
		p = &link_zone->zlz_prop_bw;
		p->zsp_type = DATA_TYPE_UINT64;
		p->zsp_v.zsv_uint64 = zs_link_zone_maxbw(link_zone);
		break;
	case ZS_LINK_ZONE_PROP_PARTBW:
		p = &link_zone->zlz_prop_partbw;
		p->zsp_type = DATA_TYPE_UINT32;
		p->zsp_v.zsv_uint = zs_link_zone_partbw(link_zone);
		break;
	default:
		abort();
	}
	return ((zs_property_t)p);
}
