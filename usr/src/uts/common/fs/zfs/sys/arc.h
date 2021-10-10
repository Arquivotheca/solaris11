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
 * Copyright (c) 2005, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_SYS_ARC_H
#define	_SYS_ARC_H

#include <sys/zfs_context.h>

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/zio.h>
#include <sys/dmu.h>
#include <sys/spa.h>

typedef struct arc_buf arc_buf_t;
typedef struct arc_ref arc_ref_t;
typedef void arc_done_func_t(zio_t *zio, arc_ref_t *ref, void *private);
typedef void arc_evict_func_t(void *private);

/* generically useful arc_done_func_t's */
arc_done_func_t arc_bcopy_func;
arc_done_func_t arc_getref_func;

/* mutex for arc_do_user_evicts */
kmutex_t arc_evict_interlock;

/* XXX - consider introducing arc_ref_impl_t? */
struct arc_ref {
	/* public portion of ref */
	uint64_t		r_size;
	void			*r_data;

	/* private portion of ref */
	arc_evict_func_t	*r_efunc;
	void			*r_private;
	arc_buf_t		*r_buf;
	arc_ref_t		*r_next;
	krwlock_t		r_lock;
};

/*
 * These are the flags we pass into/outof read calls to the arc
 */
typedef enum arc_options {
	ARC_OPT_METADATA	= (1 << 0),	/* this is meta-data */
	ARC_OPT_NOL2CACHE	= (1 << 1),	/* do not cache in L2ARC */
	ARC_OPT_REPORTCACHED	= (1 << 2)	/* return EEXIST on cache hit */
} arc_options_t;

/*
 * The following breakdowns of arc_size exist for kstat only.
 */
typedef enum arc_space_type {
	ARC_SPACE_DATA,
	ARC_SPACE_METADATA,
	ARC_SPACE_BUFS,
	ARC_SPACE_L2,
	ARC_SPACE_OTHER,
	ARC_SPACE_NUMTYPES
} arc_space_type_t;

uint32_t arc_alloc_id();
void arc_new_guid(uint64_t guid);

void arc_space_consume(uint64_t space, arc_space_type_t type);
void arc_space_return(uint64_t space, arc_space_type_t type);
#if 0
void *arc_data_buf_alloc(uint64_t space);
void arc_data_buf_free(void *buf, uint64_t space);
#endif
arc_ref_t *arc_hole_ref(int size, boolean_t meta);
arc_ref_t *arc_clone_ref(arc_ref_t *ref);
arc_ref_t *arc_alloc_ref(int size, boolean_t meta, void *data);
arc_ref_t *arc_loan_buf(int size);
arc_ref_t *arc_loan_ref(arc_ref_t *ref);
void arc_return_ref(arc_ref_t *ref);
void arc_make_hole(arc_ref_t *ref);
void arc_inactivate_ref(arc_ref_t *ref, arc_evict_func_t *func, void *private);
int arc_reactivate_ref(arc_ref_t *ref);
int arc_buf_size(arc_ref_t *ref);
boolean_t arc_try_make_writable(arc_ref_t *ref,
    boolean_t nocopy, boolean_t pending);
void arc_make_writable(arc_ref_t *ref);
void arc_get_blkptr(arc_ref_t *ref, int off, blkptr_t *bp);
void arc_ref_new_id(arc_ref_t *ref, spa_t *spa, blkptr_t *bp);
int arc_ref_writable(arc_ref_t *ref);
int arc_ref_anonymous(arc_ref_t *ref);
int arc_ref_hole(arc_ref_t *ref);
void arc_ref_freeze(arc_ref_t *ref);
void arc_ref_thaw(arc_ref_t *ref);
void arc_free_ref(arc_ref_t *ref);
#ifdef ZFS_DEBUG
int arc_ref_active(arc_ref_t *ref);
int arc_ref_iopending(arc_ref_t *ref);
#endif

#if 0
int arc_read(zio_t *pio, spa_t *spa, int blkoff, arc_ref_t *pref, uint64_t size,
    arc_done_func_t *done, void *private, int priority, int zio_flags,
    arc_options_t arc_opts, const zbookmark_t *zb);
#endif
int arc_read(zio_t *pio, spa_t *spa, const blkptr_t *bp, uint64_t size,
    arc_done_func_t *done, void *private, int priority, enum zio_flag flags,
    arc_options_t options, const zbookmark_t *zb);
zio_t *arc_write(zio_t *pio, spa_t *spa, uint64_t txg,
    blkptr_t *bp, arc_ref_t *ref, boolean_t l2arc, const zio_prop_t *zp,
    zio_done_func_t *ready, zio_done_func_t *done, void *private,
    int priority, enum zio_flag flags, const zbookmark_t *zb);

int arc_evict_ref(arc_ref_t *ref);

void arc_flush(spa_t *spa);
void arc_tempreserve_clear(uint64_t reserve);
int arc_tempreserve_space(uint64_t reserve, uint64_t txg);

void arc_init(void);
void arc_fini(void);

/*
 * Level 2 ARC
 */

void l2arc_add_vdev(spa_t *spa, vdev_t *vd);
void l2arc_remove_vdev(vdev_t *vd);
boolean_t l2arc_vdev_present(vdev_t *vd);
void l2arc_init(void);
void l2arc_fini(void);
void l2arc_start(void);
void l2arc_stop(void);

/*
 * Hash table routines
 */

#define	HT_LOCK_SPACE	64

struct ht_lock {
	kmutex_t	ht_lock;
#ifdef _KERNEL
	unsigned char	pad[(HT_LOCK_SPACE - sizeof (kmutex_t))];
#endif
};

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_ARC_H */
