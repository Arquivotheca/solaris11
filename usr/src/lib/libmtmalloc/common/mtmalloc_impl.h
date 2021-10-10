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
 * Copyright (c) 1998, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _MTMALLOC_IMPL_H
#define	_MTMALLOC_IMPL_H


/*
 * Various data structures that define the guts of the mt malloc
 * library.
 */

#include <sys/types.h>
#include <synch.h>

#ifdef __cplusplus
extern "C" {
#endif


typedef struct cache {
	/* volatile needed to support atomic operations */
	volatile ulong_t *mt_freemask;	/* free block bit mask */
	volatile ulong_t *mt_efreemask;	/* end of free block bit mask */
	caddr_t mt_arena;		/* addr of arena for dblks */
	size_t mt_size;			/* size of this cache */
	volatile uint_t *mt_nfreeptr;   /* pointer to free space  */
	size_t mt_length;		/* size of the arena in bytes */
	volatile size_t mt_lastoffset;  /* last freemask allocated frm */
	int mt_hunks;			/* at creation time what chunk size */
} cache_t;

typedef struct oversize {
	struct oversize *next_bysize;
	struct oversize *prev_bysize;
	struct oversize *next_byaddr;
	struct oversize *prev_byaddr;
	struct oversize *hash_next;
	caddr_t addr;
	size_t  size;
} oversize_t;

#define	CACHESPACES	512

typedef struct cachespaceblock {
	volatile uint_t mt_nfree[CACHESPACES];	/* free space in cache */
	cache_t *mt_cache[CACHESPACES];		/* Pointer to cache   */
	struct cachespaceblock *mt_nextblock;	/* Ptr to next block */
} cachespaceblock_t;

typedef struct cache_head {
	ulong_t mt_hint;
	cachespaceblock_t *mt_cachespaceblock;		/* caches w space */
	cachespaceblock_t *mt_cachespaceblockhint;	/* last blk w space */
} cache_head_t;

/* used to avoid false sharing, should be power-of-2 >= cache coherency size */
#define	CACHE_COHERENCY_UNIT	64

#define	PERCPU_SIZE	CACHE_COHERENCY_UNIT
#define	PERCPU_PAD	(PERCPU_SIZE - sizeof (mutex_t) - \
			sizeof (cache_head_t *))

typedef struct percpu {
	mutex_t mt_parent_lock;	/* used for hooking in new caches */
	cache_head_t *mt_caches;
	char mt_pad[PERCPU_PAD];
} percpu_t;

typedef uint_t (*curcpu_func)(void);

#define	DATA_SHIFT	1
#define	TAIL_SHIFT	2

/*
 * Oversize bit definitions: 3 bits to represent the oversize for
 * head fragment, data itself, and tail fragment.
 * If the head fragment is oversize, the first bit is on.
 * If the data itself is oversize, the second bit is on.
 * If the tail fragment is oversize, then the third bit is on.
 */
#define	NONE_OVERSIZE		0x0
#define	HEAD_OVERSIZE		0x1
#define	DATA_OVERSIZE		0x2
#define	HEAD_AND_DATA_OVERSIZE	0x3
#define	TAIL_OVERSIZE		0x4
#define	HEAD_AND_TAIL_OVERSIZE	0x5
#define	DATA_AND_TAIL_OVERSIZE	0x6
#define	ALL_OVERSIZE		0x7

#ifdef __cplusplus
}
#endif

#endif /* _MTMALLOC_IMPL_H */
