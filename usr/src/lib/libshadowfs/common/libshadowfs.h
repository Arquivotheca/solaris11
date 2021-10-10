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

#ifndef	_LIBSHADOWFS_H
#define	_LIBSHADOWFS_H

#include <sys/types.h>
#include <sys/list.h>
#include <stdarg.h>
#include <synch.h>
#include <paths.h>
#include <libscf.h>
#include <libscf_priv.h>
#include <libshadowfs_impl.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct shadow_conspiracy shadow_conspiracy_t;

#define	SHADOWD_INST_NAME	"system/filesystem/shadowd:default"
#define	SHADOWD_INST_FMRI	\
    SCF_FMRI_SVC_PREFIX SCF_FMRI_SERVICE_PREFIX SHADOWD_INST_NAME

#define	SHADOWD_STATUS_REPORTS_PARENT_DIR	_PATH_SYSVOL "/daemon"
#define	SHADOWD_STATUS_REPORTS_DIR	_PATH_SYSVOL "/daemon/shadowd"
#define	SHADOWD_STATUS_PREFIX		"shadowstat_request"

#define	SHADOWD_STATUS_NOMIGRATIONS	"%NOMIP%"
#define	SHADOWD_STATUS_BUFSZ	2044

#define	SHADOWD_STATUS_PARTIAL	1
#define	SHADOWD_STATUS_FINAL	2

#define	SHADOWD_STATUS_MAX_PENDING	3
#define	SHADOWD_STATUS_WAIT		10

typedef struct shadstatus {
	long ss_type;
	char ss_buf[SHADOWD_STATUS_BUFSZ];
} shadstatus_t;

typedef struct shadstatus_list {
	list_node_t ssl_next;
	struct shadstatus ssl_status;
} shadstatus_list_t;

extern boolean_t shadow_migrate_done(shadow_handle_t *);

extern int shadow_migrate_iter(shadow_handle_t *,
    void (*)(const char *, void *), void *);
extern void shadow_migrate_delay(shadow_handle_t *, uint32_t);

extern shadow_conspiracy_t *shadcons_init(const char *);
extern int shadcons_enable_output(shadow_conspiracy_t *, const char *,
    boolean_t);
extern void shadcons_disable_output(shadow_conspiracy_t *);
extern size_t shadcons_hash_count(shadow_conspiracy_t *);
extern const char *shadcons_hashentry_dataset(shadow_conspiracy_t *, void *);
extern void *shadcons_hash_first(shadow_conspiracy_t *);
extern void *shadcons_hash_next(shadow_conspiracy_t *, void *);
extern void shadcons_hash_remove(shadow_conspiracy_t *, void *);
extern void shadcons_fini(shadow_conspiracy_t *);
extern void shadcons_lock(shadow_conspiracy_t *);
extern void shadcons_unlock(shadow_conspiracy_t *);
extern void shadcons_stop(shadow_conspiracy_t *, const char *);
extern int shadcons_cancel(shadow_conspiracy_t *, const char *);
extern int shadcons_svc_refresh(shadow_conspiracy_t *);
extern int shadcons_svc_start(shadow_conspiracy_t *);
extern int shadcons_svc_stop(shadow_conspiracy_t *);
extern int shadcons_start(shadow_conspiracy_t *, const char *, const char *,
    const char *, boolean_t *);

extern int shadcons_status_all(shadow_conspiracy_t *,
    shadow_conspiracy_status_t **, size_t *);
extern int shadcons_status(shadow_conspiracy_t *, const char *,
    shadow_conspiracy_status_t *);

extern void shadcons_dprintf(shadow_conspiracy_t *, const char *, ...);
extern void shadcons_warn(shadow_conspiracy_t *, const char *, ...);

#ifdef	__cplusplus
}
#endif

#endif	/* _LIBSHADOWFS_H */
