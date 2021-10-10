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

#ifndef _UTIL_H
#define	_UTIL_H

#include <dhcpagent_ipc.h>
#include <libdlwlan.h>
#include <libnwam.h>
#include <pthread.h>
#include <string.h>
#include <sys/note.h>
#include <sys/time.h>
#include <sys/zone.h>
#include <syslog.h>

#include "events.h"
#include "ncu.h"

/*
 * A few functions here from files other than util.c, saves having
 * .h files for one or two functions.
 */

/* properties in the nwamd (NWAMD_PG) property group */
#define	NWAMD_DEBUG_PROP			"debug"
#define	NWAMD_AUTOCONF_PROP			"autoconf"
#define	NWAMD_STRICT_BSSID_PROP			"strict_bssid"
#define	NWAMD_CONDITION_CHECK_INTERVAL_PROP	"condition_check_interval"
#define	NWAMD_WIRELESS_SCAN_INTERVAL_PROP	"scan_interval"
#define	NWAMD_WIRELESS_SCAN_LEVEL_PROP		"scan_level"
#define	NWAMD_NCU_WAIT_TIME_PROP		"ncu_wait_time"
#define	OLD_NWAMD_VERSION_PROP			"version"

#define	NET_LOC_FMRI				"svc:/network/location:default"
#define	NET_LOC_PG				"location"
#define	NET_LOC_SELECTED_PROP			"selected"
#define	NET_LOC_FALLBACK_PROP			"fallback"

#define	NSEC_TO_SEC(nsec)	(nsec) / (long)NANOSEC
#define	NSEC_TO_FRACNSEC(nsec)	(nsec) % (long)NANOSEC
#define	SEC_TO_NSEC(sec)	(sec) * (long)NANOSEC

extern boolean_t debug;
extern boolean_t shutting_down;
/* Whether nwamd has daemonized or not */
extern boolean_t daemonized;
/* Time by which nwamd will daemonize regardless of NCU states */
extern uint32_t nwamd_daemon_time;

/*
 * Current DB version is 2.  Version history:
 *   v0 - Phase 0/0.5 llp file; nwamd/version property in nwam instance not
 *	  defined
 *   v1 - Phase 1 nwam-only files
 *   v2 - nwam instance obsoleted; version stored in netcfg/version in
 *	  default instance
 */
#define	NETCFG_DB_VERSION	2

/* logging.c: log support functions */
extern void nlog(int, const char *, ...);
extern void pfail(const char *fmt, ...);
extern int syslog_stack(uintptr_t addr, int sig, void *arg);

/* door_if.c: door interface functions */
extern void nwamd_door_init(void);
extern void nwamd_door_fini(void);

/* util.c: utility & ipc functions */
extern int nwamd_start_childv(const char *, const char * const *);
extern boolean_t nwamd_link_belongs_to_this_zone(const char *);

extern void nwamd_create_daemonize_event(void);
extern void nwamd_inform_parent_exit(int);
extern void nwamd_refresh(void);

extern void nwamd_init_privileges(void);
extern void nwamd_become_root();
extern void nwamd_release_root();
extern void nwamd_become_netadm(void);
extern void nwamd_add_privs(int, ...);
extern void nwamd_drop_privs(int, ...);

/* SCF helper functions */
extern int nwamd_lookup_boolean_property(const char *, const char *,
    const char *, boolean_t *);
extern int nwamd_lookup_count_property(const char *, const char *, const char *,
    uint64_t *);
extern int nwamd_lookup_string_property(const char *, const char *,
    const char *, char *, size_t);

extern int nwamd_set_count_property(const char *, const char *, const char *,
    uint64_t);
extern int nwamd_set_string_property(const char *, const char *, const char *,
    const char *);

extern int nwamd_delete_scf_property(const char *, const char *, const char *);

#endif /* _UTIL_H */
