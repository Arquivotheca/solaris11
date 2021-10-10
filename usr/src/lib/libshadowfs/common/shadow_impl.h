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

#ifndef	_SHADOW_IMPL_H
#define	_SHADOW_IMPL_H

#include <libshadowfs_impl.h>
#include <libzfs.h>
#include <libscf.h>

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <libintl.h>

#include <sys/sysmacros.h>
#include <sys/mntent.h>
#include <sys/mnttab.h>
#include <sys/mount.h>
#include <sys/stat.h>

#include <sys/fs/shadow.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct shadow_list {
	struct shadow_list *l_prev;
	struct shadow_list *l_next;
} shadow_list_t;

typedef struct shadow_hash_link {
	shadow_list_t  shl_list;	/* next on list of all elements */
	struct shadow_hash_link *shl_next;	/* next on this bucket */
} shadow_hash_link_t;

typedef struct shadow_hash {
	shadow_handle_t *sh_handle;	/* handle to library state */
	shadow_hash_link_t **sh_buckets;	/* array of buckets */
	size_t sh_nbuckets;		/* number of buckets */
	size_t sh_nelements;		/* number of elements */
	shadow_list_t sh_list;		/* list of all elements */
	size_t sh_linkoffs;		/* offset of shadow_hash_link in elem */
	const void *(*sh_convert)(const void *); /* key conversion function */
	ulong_t (*sh_compute)(const void *); /* hash computing function */
	int (*sh_compare)(const void *, const void *); /* compare function */
} shadow_hash_t;

/*
 * Common data structure for managing a priority queue.
 */
typedef uint64_t (shadow_pq_priority_f)(const void *);

typedef struct shadow_pq {
	void			**shpq_items;
	uint32_t		shpq_size;
	uint32_t		shpq_last;
	shadow_pq_priority_f	*shpq_priority;
} shadow_pq_t;

/*
 * File or directory entry in pending list
 */
typedef enum {
	SHADOW_TYPE_DIR,
	SHADOW_TYPE_FILE
} shadow_type_t;

typedef struct shadow_entry {
	char			*se_path;	/* file or directory path */
	uint32_t		se_depth;	/* directory hierarchy depth */
	shadow_type_t		se_type;	/* type (file or directory) */
	timestruc_t		se_timestamp;	/* timestamp (atime or mtime) */
	uint64_t		se_estsize;	/* estimated size */
} shadow_entry_t;

typedef struct shadow_error {
	char			*se_path;	/* remote path */
	int			se_error;	/* errno */
	struct shadow_error	*se_next;	/* next link */
} shadow_error_t;

typedef struct shadow_progress {
	uint64_t		sp_processed;	/* total file data processed */
	uint64_t		sp_dir_seen;	/* number of directories seen */
	uint64_t		sp_interior;	/* number of interior done */
	uint64_t		sp_leaf;	/* number of leaves done */
	uint64_t		sp_leaf_depth;	/* total depth of all leaves */
	uint64_t		sp_dir_queue;	/* number of dirs in queue */
	uint64_t		sp_dir_depth;	/* depth of all dirs in queue */
} shadow_progress_t;

/*
 * Per-mountpoint library handle.
 */
struct shadow_handle {
	char			*sh_mountpoint;	/* Mountpoint path */
	char			*sh_dataset;	/* ZFS dataset name, if any */
	char			*sh_special;	/* Mount special, if not ZFS */
	char			*sh_fstype;	/* Mount fstype, if not ZFS */
	shadow_pq_t		sh_queue;	/* Pending work queue */
	pthread_mutex_t		sh_lock;	/* Work queue lock */
	uint32_t		sh_active;	/* # actively migrating */
	boolean_t		sh_complete;	/* done processing */
	uint32_t		sh_delay;	/* debugging delay */
	hrtime_t		sh_start;	/* start time */
	shadow_progress_t	sh_progress;	/* current progress */
	shadow_error_t		*sh_errors;	/* error list */
	uint32_t		sh_errcount;	/* number of errors in list */
	pthread_mutex_t		sh_errlock;	/* separate error list lock */
	ulong_t			sh_fsid;	/* root fsid */
	boolean_t		sh_onlyerrors;	/* only errors left */
	boolean_t		sh_loaded;	/* migration begun */
	boolean_t		sh_loading;	/* loading FID list */
};

/*
 * Functions in shadow_subr.c
 */
extern void *shadow_alloc(size_t);
extern void *shadow_zalloc(size_t);
extern char *shadow_strdup(const char *);

extern int shadow_error(shadow_errno_t, const char *, ...);
extern int shadow_set_errno(shadow_errno_t);

extern const char *shadow_strerror(shadow_errno_t);
extern const char *shadow_errname(shadow_errno_t);
extern shadow_errno_t shadow_errcode(const char *);

extern int shadow_zfs_error(libzfs_handle_t *);

extern boolean_t shadow_debug_enabled(void);
extern int shadow_debug_enable(const char *);
extern void shadow_debug_disable(void);
extern void _shad_dprintf(int, const char *, const char *, ...);

/*
 * Functions in shadow_pq.c
 */
extern int shadow_pq_init(shadow_pq_t *, shadow_pq_priority_f *);
extern void shadow_pq_fini(shadow_pq_t *);

extern int shadow_pq_enqueue(shadow_pq_t *, void *);
extern void *shadow_pq_dequeue(shadow_pq_t *);
extern void *shadow_pq_peek(shadow_pq_t *);

extern int shadow_pq_remove(shadow_pq_t *, void *);
extern int shadow_pq_iter(shadow_pq_t *, int (*)(shadow_pq_t *, void *,
    void *), void *);

/*
 * Functions in shadow_migrate.c
 */
extern uint64_t shadow_priority(const void *);
extern void shadow_end(shadow_handle_t *);

/*
 * Functions in shadow_status.c
 */
extern void shadow_status_update(shadow_handle_t *, shadow_entry_t *,
    uint64_t, uint64_t);
extern void shadow_status_enqueue(shadow_handle_t *, shadow_entry_t *);
extern void shadow_status_dequeue(shadow_handle_t *, shadow_entry_t *);

/*
 * Function in shadow_list.c
 */
#define	shadow_list_prev(elem)	((void *)(((shadow_list_t *)(elem))->l_prev))
#define	shadow_list_next(elem)	((void *)(((shadow_list_t *)(elem))->l_next))

extern void shadow_list_append(shadow_list_t *, void *);
extern void shadow_list_prepend(shadow_list_t *, void *);
extern void shadow_list_insert_before(shadow_list_t *, void *, void *);
extern void shadow_list_insert_after(shadow_list_t *, void *, void *);
extern void shadow_list_delete(shadow_list_t *, void *);

/*
 * Function in shadow_hash.c
 */
extern shadow_hash_t *shadow_hash_create(size_t,
    const void *(*convert)(const void *),
    ulong_t (*compute)(const void *),
    int (*compare)(const void *, const void *));

extern void shadow_hash_destroy(shadow_hash_t *);
extern void *shadow_hash_lookup(shadow_hash_t *, const void *);
extern void shadow_hash_insert(shadow_hash_t *, void *);
extern void shadow_hash_remove(shadow_hash_t *, void *);
extern size_t shadow_hash_count(shadow_hash_t *);

extern ulong_t shadow_hash_strhash(const void *);
extern int shadow_hash_strcmp(const void *, const void *);

extern ulong_t shadow_hash_ptrhash(const void *);
extern int shadow_hash_ptrcmp(const void *, const void *);

extern void *shadow_hash_first(shadow_hash_t *);
extern void *shadow_hash_next(shadow_hash_t *, void *);


#ifdef	__cplusplus
}
#endif

#endif	/* _SHADOW_IMPL_H */
