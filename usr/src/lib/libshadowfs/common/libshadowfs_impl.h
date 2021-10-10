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

#ifndef	_LIBSHADOWFS_IMPL_H
#define	_LIBSHADOWFS_IMPL_H

#include <sys/types.h>

#ifdef	__cplusplus
extern "C" {
#endif

/* Shadow conspiracy SMF parameters */
#define	CONFIG_PARAMS		"config_params"
#define	WORKER_THREADS		"shadow_threads"
#define	FAIL_THROTTLE		"shadow_throttle"
#define	DEBUG_FILE		"shadow_debug"

#define	DEFAULT_NWORKERS		8
#define	DEFAULT_THROTTLE		1000000

#ifndef ASSERT
#define	ASSERT(x)	assert((x));
#endif

typedef struct shadow_handle shadow_handle_t;

typedef enum {
	ESHADOW_NONE,		/* no error */
	ESHADOW_NOMEM,		/* out of memory */
	ESHADOW_NOMOUNT,	/* no such mountpoint */
	ESHADOW_NOSHADOW,	/* not a shadow mountpoint */
	ESHADOW_CORRUPT,	/* internal data inconsistency */
	ESHADOW_ZFS_NOENT,	/* failed to open ZFS dataset */
	ESHADOW_ZFS_IO,		/* I/O error */
	ESHADOW_ZFS_IMPL,	/* internal ZFS error */
	ESHADOW_MNT_CLEAR,	/* failed to clear mount option */
	ESHADOW_MIGRATE_BUSY,	/* migration currently busy */
	ESHADOW_MIGRATE_DONE,	/* finished migrating all data */
	ESHADOW_MIGRATE_INTR,	/* a file migration was interrupted */
	ESHADOW_STANDBY,	/* filesystem is in standby mode */
	ESHADOW_UNKNOWN		/* unknown error */
} shadow_errno_t;

typedef struct {
	uint64_t	ss_processed;	/* plain file contents transferred */
	uint64_t	ss_estimated;	/* estimated remaining contents */
	hrtime_t	ss_start;	/* start of migration */
	uint32_t	ss_errors;	/* number of unique errors seen */
} shadow_status_t;

/* Shadow conspiracy status */
typedef struct shadow_conspiracy_status {
	char		*scs_ds_name;
	uint64_t	scs_xferred;
	uint64_t	scs_remaining;
	int		scs_errors;
	uint64_t	scs_elapsed;
	boolean_t	scs_complete;
} shadow_conspiracy_status_t;

typedef struct {
	int		ser_errno;	/* error seen */
	char		*ser_path;	/* path */
} shadow_error_report_t;

extern shadow_handle_t *shadow_open(const char *);
extern void shadow_close(shadow_handle_t *);

extern int shadow_cancel(shadow_handle_t *);
extern shadow_errno_t shadow_errno(void);
extern const char *shadow_errmsg(void);

extern shadow_error_report_t *shadow_get_errors(shadow_handle_t *, size_t);
extern void shadow_free_errors(shadow_error_report_t *, size_t);


extern int shadow_migrate_one(shadow_handle_t *);
extern boolean_t shadow_migrate_only_errors(shadow_handle_t *);
extern int shadow_migrate_finalize(shadow_handle_t *);

extern void shadow_get_status(shadow_handle_t *, shadow_status_t *);

#ifdef	__cplusplus
}
#endif

#endif	/* _LIBSHADOWFS_IMPL_H */
