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
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Additional API for Identity Mapping Service
 */

#ifndef _IDMAP_PRIV_H
#define	_IDMAP_PRIV_H


#include <libscf.h>
#include <resolv.h>

#include "idmap.h"
#include <rpcsvc/idmap_prot.h>

#ifdef __cplusplus
extern "C" {
#endif

#define	IDMAP_MAX_NAME_LEN	512

#define	IDMAP_ERROR(rc)		(rc != IDMAP_SUCCESS && rc != IDMAP_NEXT)
#define	IDMAP_FATAL_ERROR(rc)	(rc == IDMAP_ERR_MEMORY ||\
				rc == IDMAP_ERR_DB)


/* is_user values */

#define	IDMAP_YES 1
#define	IDMAP_NO 0
#define	IDMAP_UNKNOWN -1

#define	CHECK_NULL(s)	(s != NULL ? s : "null")
#define	EMPTY_STRING(str)	(str == NULL || *str == '\0')

/* Opaque handle to batch config add/remove operations */
typedef struct idmap_udt_handle idmap_udt_handle_t;

/* Opaque iterator */
typedef struct idmap_iter idmap_iter_t;


/*
 * Update API
 */

/* Create handle for updates */
extern idmap_stat idmap_udt_create(idmap_udt_handle_t **);

/* Commit */
extern idmap_stat idmap_udt_commit(idmap_udt_handle_t *);

/* Get index of the failed batch element */
extern idmap_stat idmap_udt_get_error_index(idmap_udt_handle_t *, int64_t *);

/* Get the rule which caused the batch to failed */
extern idmap_stat idmap_udt_get_error_rule(idmap_udt_handle_t *, char **,
    char **, char **, boolean_t *, boolean_t *, boolean_t *, int *);

/* Get the rule which caused a conflict */
extern idmap_stat idmap_udt_get_conflict_rule(idmap_udt_handle_t *, char **,
    char **, char **, boolean_t *, boolean_t *, boolean_t *, int *);

/* Destroy the update handle */
extern void idmap_udt_destroy(idmap_udt_handle_t *);

/* Add name-based mapping rule */
extern idmap_stat idmap_udt_add_namerule(idmap_udt_handle_t *, const char *,
	boolean_t, boolean_t, const char *, const char *, boolean_t, int);

/* Remove name-based mapping rule */
extern idmap_stat idmap_udt_rm_namerule(idmap_udt_handle_t *, boolean_t,
	boolean_t, const char *, const char *, const char *, int);

/* Flush name-based mapping rules */
extern idmap_stat idmap_udt_flush_namerules(idmap_udt_handle_t *);

/* Flush caches */
extern idmap_stat idmap_flush(idmap_flush_op);

/*
 * Iterator API
 */

/* Create a iterator to get SID to UID/GID mappings */
extern idmap_stat idmap_iter_mappings(idmap_iter_t **, int flag);

/* Iterate through the SID to UID/GID mappings */
extern idmap_stat idmap_iter_next_mapping(idmap_iter_t *, char **,
	idmap_rid_t *, uid_t *, char **, char **, char **, boolean_t *,
	boolean_t *, int *, idmap_info *);

/* Create a iterator to get name-based mapping rules */
extern idmap_stat idmap_iter_namerules(const char *,
	boolean_t, boolean_t, const char *, const char *, idmap_iter_t **);

/* Iterate through the name-based mapping rules */
extern idmap_stat idmap_iter_next_namerule(idmap_iter_t *, char **,
	char **, char **, boolean_t *, boolean_t *, boolean_t *, int *);

/* Set the number of entries requested per batch */
extern idmap_stat idmap_iter_set_limit(idmap_iter_t *, uint64_t);

/* Destroy the iterator */
extern void idmap_iter_destroy(idmap_iter_t *);


/*
 * Get mapping
 */
extern idmap_stat idmap_get_w2u_mapping(const char *,
	idmap_rid_t *, const char *, const char *, int, int *, int *,
	uid_t *, char **, int *, idmap_info *);

extern idmap_stat idmap_get_u2w_mapping(uid_t *,
	const char *, int, int, int *, char **, idmap_rid_t *, char **,
	char **, int *, idmap_info *);


/*
 * Miscellaneous
 */

/* string to status */
extern idmap_stat idmap_string2stat(const char *);

/* internal status to protocol status */
extern idmap_stat idmap_stat4prot(idmap_stat);

/* copy idmap_namerule including strings */
extern idmap_stat idmap_namerule_cpy(idmap_namerule *, idmap_namerule *);

/* Move  idmap_info info including strings */
extern void idmap_info_mov(idmap_info *to, idmap_info *from);

/* free idmap_info info from  user supplied struct */
extern void idmap_info_free(idmap_info *);
extern void idmap_how_clear(idmap_how *);


/*
 * Extended API to batch SID to UID/GID mapping requests
 */

/* Given SID, get UID */
extern idmap_stat idmap_getext_uidbysid(idmap_get_handle_t *, char *,
	idmap_rid_t, int, uid_t *, idmap_info *, idmap_stat *);

/* Given SID, get GID */
extern idmap_stat idmap_getext_gidbysid(idmap_get_handle_t *, char *,
	idmap_rid_t, int, gid_t *, idmap_info *, idmap_stat *);

/* Given SID, get UID or GID */
extern idmap_stat idmap_getext_pidbysid(idmap_get_handle_t *, char *,
	idmap_rid_t, int, uid_t *, int *, idmap_info *, idmap_stat *);

/* Given UID, get SID */
extern idmap_stat idmap_getext_sidbyuid(idmap_get_handle_t *, uid_t, int,
	char **, idmap_rid_t *, idmap_info *, idmap_stat *);

/* Given GID, get SID */
extern idmap_stat idmap_getext_sidbygid(idmap_get_handle_t *, gid_t, int,
	char **, idmap_rid_t *, idmap_info *, idmap_stat *);

/* Properties */
extern idmap_stat idmap_get_prop_ds(idmap_prop_type, idmap_ad_disc_ds_t *);
extern idmap_stat idmap_get_prop_str(idmap_prop_type, char **);

/*
 * Trace
 *
 * These are the names of the trace parameters that may be returned
 * when tracing is requested.
 */
#define	IDMAP_TRACE_NAME_MAX	100	/* max length of a trace param name */
#define	IDMAP_TRACE_TYPE	"-type"		/* prefix with "from" or "to" */
#define	IDMAP_TRACE_NAME	"-name"		/* prefix with "from" or "to" */
#define	IDMAP_TRACE_SID		"-sid"		/* prefix with "from" or "to" */
#define	IDMAP_TRACE_UNIXID	"-unixid"	/* prefix with "from" or "to" */
#define	IDMAP_TRACE_ERROR	"error"
#define	IDMAP_TRACE_MESSAGE	"message"

extern void idmap_trace_print_1(FILE *out, char *prefix, nvlist_t *entry);
extern void idmap_trace_print(FILE *out, char *prefix, nvlist_t *trace);

#ifdef __cplusplus
}
#endif

#endif /* _IDMAP_PRIV_H */
