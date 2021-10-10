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

/*
 * DVA-based Adaptive Replacement Cache
 *
 * While much of the theory of operation used here is
 * based on the self-tuning, low overhead replacement cache
 * presented by Megiddo and Modha at FAST 2003, there are some
 * significant differences:
 *
 * 1. The Megiddo and Modha model assumes any page is evictable.
 * Pages in its cache cannot be "locked" into memory.  This makes
 * the eviction algorithm simple: evict the last page in the list.
 * This also make the performance characteristics easy to reason
 * about.  Our cache is not so simple.  At any given moment, some
 * subset of the blocks in the cache are un-evictable because we
 * have handed out a reference to them.  Blocks are only evictable
 * when there are no external references active.  This makes
 * eviction far more problematic:  we choose to evict the evictable
 * blocks that are the "lowest" in the list.
 *
 * There are times when it is not possible to evict the requested
 * space.  In these circumstances we are unable to adjust the cache
 * size.  To prevent the cache growing unbounded at these times we
 * implement a "cache throttle" that slows the flow of new data
 * into the cache until we can make space available.
 *
 * 2. The Megiddo and Modha model assumes a fixed cache size.
 * Pages are evicted when the cache is full and there is a cache
 * miss.  Our model has a variable sized cache.  It grows with
 * high use, but also tries to react to memory pressure from the
 * operating system: decreasing its size when system memory is
 * tight.
 *
 * 3. The Megiddo and Modha model assumes a fixed page size. All
 * elements of the cache are therefor exactly the same size.  So
 * when adjusting the cache size following a cache miss, its simply
 * a matter of choosing a single page to evict.  In our model, we
 * have variable sized cache blocks (rangeing from 512 bytes to
 * 1M bytes).  We therefor choose a set of blocks to evict to make
 * space for a cache miss that approximates as closely as possible
 * the space used by the new block.
 *
 * See also:  "ARC: A Self-Tuning, Low Overhead Replacement Cache"
 * by N. Megiddo & D. Modha, FAST 2003
 */

/*
 * The locking model:
 *
 * A new reference to a cache buffer can be obtained in two
 * ways: 1) via a hash table lookup using the DVA as a key,
 * or 2) via one of the ARC lists.  The arc_read() interface
 * uses method 1, while the internal arc algorithms for
 * adjusting the cache use method 2.  We therefor provide two
 * types of locks: 1) the hash table lock array, and 2) the
 * arc list locks.
 *
 * Buffers have their own mutexes, most fields in the arc_buf_t
 * are protected by this mutex.
 *
 * arc_hash_find() returns the buffer with an active hold if it
 * locates the requested buffer in the hash table.  This prevents
 * any attempt to evict.
 *
 * arc_hash_remove() expects the appropriate hash mutex to be
 * already held before it is invoked to prevent racing with
 * any thread trying to find this buffer.
 *
 * Each arc state has mutexes used to protect the buffer lists
 * associated with the state.  When attempting to obtain buffer
 * lock or a hash table lock while holding an arc list lock you
 * must use: mutex_tryenter() to avoid deadlock.  Also note that
 * the active state mutex must be held before the ghost state mutex.
 *
 * Arc buffers may have an associated eviction callback function.
 * This function will be invoked prior to removing the buffer (e.g.
 * in arc_do_user_evicts()).  Note however that the data associated
 * with the buffer may be evicted prior to the callback.  The callback
 * must be made with *no locks held* (to prevent deadlock).  Additionally,
 * the users of callbacks must ensure that their private data is
 * protected from simultaneous callbacks from arc_evict_ref()
 * and arc_do_user_evicts().
 *
 * The arc state eviction lists must only be manipulated while holding
 * the appropriate lock: l_update_lock protects the l_insert_list (and
 * the l_size); l_evict_lock protects the l_remove_list.  Always obtain
 * the l_evict_lock before obtaining the l_update_lock (when both locks
 * are required).
 *
 * Note that the majority of the performance stats are manipulated
 * with atomic operations.
 *
 * The L2ARC uses the l2arc_buflist_mtx global mutex for the following:
 *
 *	- L2ARC buflist creation
 *	- L2ARC buflist eviction
 *	- L2ARC write completion, which walks L2ARC buflists
 */

#include <sys/spa.h>
#include <sys/zio.h>
#include <sys/zfs_context.h>
#include <sys/arc.h>
#include <sys/refcount.h>
#include <sys/vdev.h>
#include <sys/vdev_impl.h>
#include <sys/zio_checksum.h>
#ifdef _KERNEL
#include <sys/vmsystm.h>
#include <vm/anon.h>
#include <sys/fs/swapnode.h>
#include <sys/dnlc.h>
#endif
#include <sys/callb.h>
#include <sys/kstat.h>
#include <sys/time.h>
#include <zfs_fletcher.h>

static kmutex_t		arc_reclaim_thr_lock;
static kcondvar_t	arc_reclaim_thr_cv;	/* used to signal reclaim thr */
static uint8_t		arc_thread_exit;

kmutex_t	arc_evict_interlock;

extern int zfs_write_limit_shift;
extern uint64_t zfs_write_limit_max;
extern kmutex_t zfs_write_limit_lock;

#define	ARC_REDUCE_DNLC_PERCENT	3
uint_t arc_reduce_dnlc_percent = ARC_REDUCE_DNLC_PERCENT;

typedef enum arc_reclaim_strategy {
	ARC_RECLAIM_AGGR,		/* Aggressive reclaim strategy */
	ARC_RECLAIM_CONS		/* Conservative reclaim strategy */
} arc_reclaim_strategy_t;

/* number of seconds before growing cache again */
static int		arc_grow_retry = 60;

/* number of seconds between buf0 trims */
static int		arc_trim_retry = 60;

/* shift of arc_c for calculating both min and max arc_p */
static int		arc_p_min_shift = 4;

/* log2(fraction of arc to reclaim) */
static int		arc_shrink_shift = 5;

/* block size used to compute hash table size (default: 64K) */
static int		arc_average_blocksize = (64 * 1024);
/*
 * minimum time between successive buf access before we consider the
 * access to be "significant".  This prevents a series of sequential
 * reads of a data block from kicking it into the MFU state.
 */
#define	ARC_MINTIME	(hz>>4) /* 62 ms */

static boolean_t arc_dead;

/*
 * The arc has filled available memory and has now warmed up.
 */
static boolean_t arc_warm;

/*
 * These tunables are for performance analysis.
 */
uint64_t zfs_arc_max;
uint64_t zfs_arc_min;
uint64_t zfs_arc_meta_limit = 0;
int zfs_arc_grow_retry = 0;
int zfs_arc_shrink_shift = 0;
int zfs_arc_p_min_shift = 0;

/*
 * Note that buffers can be in one of 4 states:
 *	ARC_mru		- recently used, currently cached
 *	ARC_mru_ghost	- recently used, no longer in cache
 *	ARC_mfu		- frequently used, currently cached
 *	ARC_mfu_ghost	- frequently used, no longer in cache
 * When there are no active references to the buffer, they are
 * are linked onto a list in one of these arc states.  These are
 * the only buffers that can be evicted or deleted.
 */

typedef struct arc_hdr arc_hdr_t;
typedef struct arc_elink arc_elink_t;

struct arc_elink {
	arc_buf_t *e_buf;
	uint64_t e_gen;
	list_node_t e_link;
};

typedef struct arc_evict_list {
	uint64_t l_generation;		/* generation number for this list */
	uint64_t l_size;		/* amount of evictable data */
	kmutex_t l_link_lock;		/* lock for updating a list link */
	kmutex_t l_update_lock;		/* lock for adding bufs */
	list_t l_insert;		/* list evictables are added to */
	kmutex_t l_evict_lock;		/* lock for removing bufs */
	list_t l_remove;		/* list evictables are removed from */
} arc_evict_list_t;

typedef struct arc_state {
	uint64_t s_size;		/* total amount of data in this state */
	arc_evict_list_t s_evictable;	/* evictable buffers */
} arc_state_t;

/* The 4 states: */
static arc_state_t ARC_mru;
static arc_state_t ARC_mru_ghost;
static arc_state_t ARC_mfu;
static arc_state_t ARC_mfu_ghost;

typedef struct arc_stats {
	kstat_named_t arcstat_hits;
	kstat_named_t arcstat_misses;
	kstat_named_t arcstat_demand_data_hits;
	kstat_named_t arcstat_demand_data_misses;
	kstat_named_t arcstat_demand_metadata_hits;
	kstat_named_t arcstat_demand_metadata_misses;
	kstat_named_t arcstat_prefetch_data_hits;
	kstat_named_t arcstat_prefetch_data_misses;
	kstat_named_t arcstat_prefetch_metadata_hits;
	kstat_named_t arcstat_prefetch_metadata_misses;
	kstat_named_t arcstat_mru_hits;
	kstat_named_t arcstat_mru_ghost_hits;
	kstat_named_t arcstat_mfu_hits;
	kstat_named_t arcstat_mfu_ghost_hits;
	kstat_named_t arcstat_deleted;
	kstat_named_t arcstat_mutex_miss;
	kstat_named_t arcstat_hash_elements;
	kstat_named_t arcstat_hash_elements_max;
	kstat_named_t arcstat_hash_collisions;
	kstat_named_t arcstat_hash_chains;
	kstat_named_t arcstat_hash_chain_max;
	kstat_named_t arcstat_p;
	kstat_named_t arcstat_c;
	kstat_named_t arcstat_c_min;
	kstat_named_t arcstat_c_max;
	kstat_named_t arcstat_size;
	kstat_named_t arcstat_buf_size;
	kstat_named_t arcstat_data_size;
	kstat_named_t arcstat_other_size;
	kstat_named_t arcstat_l2_hits;
	kstat_named_t arcstat_l2_misses;
	kstat_named_t arcstat_l2_feeds;
	kstat_named_t arcstat_l2_rw_clash;
	kstat_named_t arcstat_l2_read_bytes;
	kstat_named_t arcstat_l2_write_bytes;
	kstat_named_t arcstat_l2_writes_sent;
	kstat_named_t arcstat_l2_writes_done;
	kstat_named_t arcstat_l2_writes_error;
	kstat_named_t arcstat_l2_writes_hdr_miss;
	kstat_named_t arcstat_l2_evict_lock_retry;
	kstat_named_t arcstat_l2_evict_reading;
	kstat_named_t arcstat_l2_abort_lowmem;
	kstat_named_t arcstat_l2_cksum_bad;
	kstat_named_t arcstat_l2_io_error;
	kstat_named_t arcstat_l2_hdr_size;
	kstat_named_t arcstat_memory_throttle_count;
	kstat_named_t arcstat_meta_used;
	kstat_named_t arcstat_meta_max;
	kstat_named_t arcstat_meta_limit;
} arc_stats_t;

static arc_stats_t arc_stats = {
	{ "hits",			KSTAT_DATA_UINT64 },
	{ "misses",			KSTAT_DATA_UINT64 },
	{ "demand_data_hits",		KSTAT_DATA_UINT64 },
	{ "demand_data_misses",		KSTAT_DATA_UINT64 },
	{ "demand_metadata_hits",	KSTAT_DATA_UINT64 },
	{ "demand_metadata_misses",	KSTAT_DATA_UINT64 },
	{ "prefetch_data_hits",		KSTAT_DATA_UINT64 },
	{ "prefetch_data_misses",	KSTAT_DATA_UINT64 },
	{ "prefetch_metadata_hits",	KSTAT_DATA_UINT64 },
	{ "prefetch_metadata_misses",	KSTAT_DATA_UINT64 },
	{ "mru_hits",			KSTAT_DATA_UINT64 },
	{ "mru_ghost_hits",		KSTAT_DATA_UINT64 },
	{ "mfu_hits",			KSTAT_DATA_UINT64 },
	{ "mfu_ghost_hits",		KSTAT_DATA_UINT64 },
	{ "deleted",			KSTAT_DATA_UINT64 },
	{ "mutex_miss",			KSTAT_DATA_UINT64 },
	{ "hash_elements",		KSTAT_DATA_UINT64 },
	{ "hash_elements_max",		KSTAT_DATA_UINT64 },
	{ "hash_collisions",		KSTAT_DATA_UINT64 },
	{ "hash_chains",		KSTAT_DATA_UINT64 },
	{ "hash_chain_max",		KSTAT_DATA_UINT64 },
	{ "p",				KSTAT_DATA_UINT64 },
	{ "c",				KSTAT_DATA_UINT64 },
	{ "c_min",			KSTAT_DATA_UINT64 },
	{ "c_max",			KSTAT_DATA_UINT64 },
	{ "size",			KSTAT_DATA_UINT64 },
	{ "buf_size",			KSTAT_DATA_UINT64 },
	{ "data_size",			KSTAT_DATA_UINT64 },
	{ "other_size",			KSTAT_DATA_UINT64 },
	{ "l2_hits",			KSTAT_DATA_UINT64 },
	{ "l2_misses",			KSTAT_DATA_UINT64 },
	{ "l2_feeds",			KSTAT_DATA_UINT64 },
	{ "l2_rw_clash",		KSTAT_DATA_UINT64 },
	{ "l2_read_bytes",		KSTAT_DATA_UINT64 },
	{ "l2_write_bytes",		KSTAT_DATA_UINT64 },
	{ "l2_writes_sent",		KSTAT_DATA_UINT64 },
	{ "l2_writes_done",		KSTAT_DATA_UINT64 },
	{ "l2_writes_error",		KSTAT_DATA_UINT64 },
	{ "l2_writes_hdr_miss",		KSTAT_DATA_UINT64 },
	{ "l2_evict_lock_retry",	KSTAT_DATA_UINT64 },
	{ "l2_evict_reading",		KSTAT_DATA_UINT64 },
	{ "l2_abort_lowmem",		KSTAT_DATA_UINT64 },
	{ "l2_cksum_bad",		KSTAT_DATA_UINT64 },
	{ "l2_io_error",		KSTAT_DATA_UINT64 },
	{ "l2_hdr_size",		KSTAT_DATA_UINT64 },
	{ "memory_throttle_count",	KSTAT_DATA_UINT64 },
	{ "meta_used",			KSTAT_DATA_UINT64 },
	{ "meta_max",			KSTAT_DATA_UINT64 },
	{ "meta_limit",			KSTAT_DATA_UINT64 }
};

#define	ARCSTAT(stat)	(arc_stats.stat.value.ui64)

#define	ARCSTAT_INCR(stat, val) \
	atomic_add_64(&arc_stats.stat.value.ui64, (val));

#define	ARCSTAT_BUMP(stat)	ARCSTAT_INCR(stat, 1)
#define	ARCSTAT_BUMPDOWN(stat)	ARCSTAT_INCR(stat, -1)

#define	ARCSTAT_MAX(stat, val) {					\
	uint64_t m;							\
	while ((val) > (m = arc_stats.stat.value.ui64) &&		\
	    (m != atomic_cas_64(&arc_stats.stat.value.ui64, m, (val))))	\
		continue;						\
}

#define	ARCSTAT_MAXSTAT(stat) \
	ARCSTAT_MAX(stat##_max, arc_stats.stat.value.ui64)

/*
 * We define a macro to allow ARC hits/misses to be easily broken down by
 * two separate conditions, giving a total of four different subtypes for
 * each of hits and misses (so eight statistics total).
 */
#define	ARCSTAT_CONDSTAT(cond1, stat1, notstat1, cond2, stat2, notstat2, stat) \
	if (cond1) {							\
		if (cond2) {						\
			ARCSTAT_BUMP(arcstat_##stat1##_##stat2##_##stat); \
		} else {						\
			ARCSTAT_BUMP(arcstat_##stat1##_##notstat2##_##stat); \
		}							\
	} else {							\
		if (cond2) {						\
			ARCSTAT_BUMP(arcstat_##notstat1##_##stat2##_##stat); \
		} else {						\
			ARCSTAT_BUMP(arcstat_##notstat1##_##notstat2##_##stat);\
		}							\
	}

/*
 * There are several ARC variables that are critical to export as kstats --
 * but we don't want to have to grovel around in the kstat whenever we wish to
 * manipulate them.  For these variables, we therefore define them to be in
 * terms of the statistic variable.  This assures that we are not introducing
 * the possibility of inconsistency by having shadow copies of the variables,
 * while still allowing the code to be readable.
 */
#define	arc_size	ARCSTAT(arcstat_size)	/* actual total arc size */
#define	arc_p		ARCSTAT(arcstat_p)	/* target size of MRU */
#define	arc_c		ARCSTAT(arcstat_c)	/* target size of cache */
#define	arc_c_min	ARCSTAT(arcstat_c_min)	/* min target cache size */
#define	arc_c_max	ARCSTAT(arcstat_c_max)	/* max target cache size */

#define	arc_meta_used	ARCSTAT(arcstat_meta_used) /* metadata stored in ARC */
#define	arc_meta_max	ARCSTAT(arcstat_meta_max)
#define	arc_meta_limit	ARCSTAT(arcstat_meta_limit)

kstat_t			*arc_ksp;
static arc_state_t	*arc_mru;
static arc_state_t	*arc_mru_ghost;
static arc_state_t	*arc_mfu;
static arc_state_t	*arc_mfu_ghost;

static int		arc_no_grow;	/* Don't try to grow cache size */
static uint64_t		arc_tempreserve;
static uint64_t		arc_loaned_bytes = 0;

typedef enum arc_flags {
	ARC_FLAG_METADATA	= (1 << 0),	/* buffer has meta-data */
	ARC_FLAG_FREQUENTLY_USED = (1 << 1),	/* buffer in mfu state */
	ARC_FLAG_HASHED		= (1 << 2),	/* buffer in hash table */
	ARC_FLAG_PREFETCHED	= (1 << 3), 	/* prefetched buffer */
	ARC_FLAG_GHOST		= (1 << 4), 	/* ghost buffer */
	ARC_FLAG_BP_CKSUM_INUSE	= (1 << 5),	/* use BP checksum */
	ARC_FLAG_DONT_L2CACHE	= (1 << 6),	/* don't place in l2arc */
	ARC_FLAG_L2BUF		= (1 << 7)	/* this is an l2arc buffer */
} arc_flags_t;

struct arc_hdr {
	/* immutable once hashed */
	uint64_t		h_spa;
	dva_t			h_dva;
	uint64_t		h_birth;

	/* protected by hash lock */
	arc_flags_t		h_flags;
	uint32_t		h_hash_idx;
	arc_hdr_t		*h_next;
};

typedef struct arc_cksum {
	enum zio_checksum	c_func;
	zio_cksum_t		c_value;
} arc_cksum_t;

struct arc_buf {
	arc_hdr_t		b_id;

	/* immutable */
	uint64_t		b_size;

	/* protected by buf lock */
	/* XXX - consider using flags for state determination */
	arc_state_t		*b_state;
	arc_ref_t		*b_inactive;
	void			*b_data;
	clock_t			b_last_access;
	void			*b_cookie;
	arc_cksum_t		*b_cksum;
	arc_elink_t		*b_link;

	/* self protecting */
	refcount_t		b_active;
	kmutex_t		b_lock;
};

typedef struct arc_ghost {
	arc_hdr_t		g_id;

	/* protected by state list lock */
	list_node_t		g_link;
} arc_ghost_t;

typedef struct arc_read_callback {
	void		*cb_private;
	arc_done_func_t	*cb_done;
} arc_read_callback_t;

typedef struct arc_write_callback {
	void		*cb_private;
	zio_done_func_t	*cb_ready;
	zio_done_func_t	*cb_done;
	arc_ref_t	*cb_ref;
	arc_flags_t	cb_flags;
} arc_write_callback_t;

static kmutex_t arc_eviction_mtx;
static arc_buf_t arc_eviction_head;

static refcount_t arc_anon_size;
static refcount_t arc_iopending_size;
static refcount_t arc_writing_size;
static arc_buf_t arc_anon_metadata;
static arc_buf_t arc_anon_data;
static arc_buf_t arc_data_buf0;
static arc_buf_t arc_metadata_buf0;


static void arc_hold(arc_buf_t *buf, void *tag);
static void arc_rele(arc_buf_t *buf, void *tag);
static void arc_promote_buf(arc_buf_t *buf);
static void arc_destroy_buf(arc_buf_t *buf);
static void *arc_get_data_block(uint64_t size, boolean_t meta);
static void arc_free_data_block(void *data, uint64_t size, boolean_t meta);
static void arc_data_free(arc_buf_t *buf);
static void arc_access(arc_buf_t *buf);
static void arc_adjust_ghost(void);
static void arc_adapt(int bytes, arc_state_t *state);

#define	ARC_FLAG_SET(h, f)	((arc_hdr_t *)h)->h_flags |= ARC_FLAG_##f
#define	ARC_FLAG_CLEAR(h, f)	((arc_hdr_t *)h)->h_flags &= ~ARC_FLAG_##f
#define	ARC_FLAG_ISSET(h, f)	((((arc_hdr_t *)h)->h_flags & ARC_FLAG_##f) > 0)

#define	GHOST_STATE(state)	\
	((state) == arc_mru_ghost || (state) == arc_mfu_ghost)

#define	BUF_METADATA(buf)	ARC_FLAG_ISSET(buf, METADATA)
#define	BUF_GHOST(buf)		ARC_FLAG_ISSET(buf, GHOST)
#define	BUF_HASHED(buf)		ARC_FLAG_ISSET(buf, HASHED)
#define	BUF_PREFETCHED(buf)	ARC_FLAG_ISSET(buf, PREFETCHED)
#define	BUF_DONTL2CACHE(buf)	ARC_FLAG_ISSET(buf, DONTL2CACHE)
#define	BUF_L2(buf)		ARC_FLAG_ISSET(buf, L2BUF)

#define	BUF_REFERENCED(buf)	\
	((buf)->b_inactive || \
	refcount_count(&(buf)->b_active) > BUF_HASHED(buf))
#define	BUF_STATE(buf)		\
	(ARC_FLAG_ISSET(buf, FREQUENTLY_USED) ? \
	(ARC_FLAG_ISSET(buf, GHOST) ? arc_mfu_ghost : arc_mfu) : \
	(ARC_FLAG_ISSET(buf, GHOST) ? arc_mru_ghost : arc_mru))
#define	BUF_CKSUM_EXISTS(buf)	((buf)->b_cksum != NULL)
#define	BUF_L2CACHED(buf)	((buf)->b_cookie != NULL)
#define	BUF_IS_HOLE(buf)	\
	((buf) == &arc_metadata_buf0 || (buf) == &arc_data_buf0)

#define	REF_AUTONOMOUS(ref)	((ref)->r_data != (ref)->r_buf->b_data)
#define	REF_ANONYMOUS(ref)	\
	((ref)->r_buf == &arc_anon_data || (ref)->r_buf == &arc_anon_metadata)
#define	REF_WRITE_PENDING(ref)	((ref)->r_efunc == arc_write_pending)
#define	REF_WRITING(ref)	((ref)->r_efunc == arc_write_in_progress)
#define	REF_CKSUM_EXISTS(ref)	((ref)->r_private != NULL)
#define	REF_INACTIVE(ref)	((ref)->r_data == NULL)

/*
 * Other sizes
 */

#define	ARC_DEFAULT_GHOST_SIZE	(64*1024)

/*
 * BUF_LOCK_RANGE is the maximum number of hash buckets covered by a
 * single hash lock (i.e., we divide the number of buckets by this
 * value to determine the number of hash locks to allocate).
 */
#define	BUF_LOCK_RANGE 512
#define	BUF_MIN_LOCKS 256

typedef struct arc_hash_table {
	uint64_t ht_mask;
	arc_hdr_t **ht_table;
	uint64_t ht_lock_mask;
	struct ht_lock *ht_locks;
} arc_hash_table_t;

static arc_hash_table_t arc_hash_table;

#define	HASH_INDEX(spa, dva, birth) \
	(arc_hash(spa, dva, birth) & arc_hash_table.ht_mask)
#define	HASH_LOCK_NTRY(idx) \
	(arc_hash_table.ht_locks[idx & arc_hash_table.ht_lock_mask])
#define	HASH_LOCK(b)	\
	(&(HASH_LOCK_NTRY(((arc_hdr_t *)b)->h_hash_idx).ht_lock))

uint64_t zfs_crc64_table[256];

/*
 * Level 2 ARC
 */

#define	L2ARC_WRITE_SIZE	(8 * 1024 * 1024)	/* initial write max */
#define	L2ARC_HEADROOM		2		/* num of writes */
#define	L2ARC_FEED_SECS		1		/* caching interval secs */
#define	L2ARC_FEED_MIN_MS	200		/* min caching interval ms */

#define	l2arc_writes_sent	ARCSTAT(arcstat_l2_writes_sent)
#define	l2arc_writes_done	ARCSTAT(arcstat_l2_writes_done)

/*
 * L2ARC Performance Tunables
 */
uint64_t l2arc_write_max = L2ARC_WRITE_SIZE;	/* default max write size */
uint64_t l2arc_write_boost = L2ARC_WRITE_SIZE;	/* extra write during warmup */
uint64_t l2arc_headroom = L2ARC_HEADROOM;	/* number of dev writes */
uint64_t l2arc_feed_secs = L2ARC_FEED_SECS;	/* interval seconds */
uint64_t l2arc_feed_min_ms = L2ARC_FEED_MIN_MS;	/* min interval milliseconds */
boolean_t l2arc_noprefetch = B_TRUE;		/* don't cache prefetch bufs */
boolean_t l2arc_feed_again = B_TRUE;		/* turbo warmup */
boolean_t l2arc_norw = B_TRUE;			/* no reads during writes */

/*
 * L2ARC Internals
 */
typedef struct l2arc_dev {
	vdev_t			*l2ad_vdev;	/* vdev */
	spa_t			*l2ad_spa;	/* spa */
	uint64_t		l2ad_hand;	/* next write location */
	uint64_t		l2ad_write;	/* desired write size, bytes */
	uint64_t		l2ad_boost;	/* warmup write boost, bytes */
	uint64_t		l2ad_start;	/* first addr on device */
	uint64_t		l2ad_end;	/* last addr on device */
	uint64_t		l2ad_evict;	/* last addr eviction reached */
	boolean_t		l2ad_first;	/* first sweep through */
	boolean_t		l2ad_writing;	/* currently writing */
	list_t			l2ad_buflist;	/* buffer list */
	list_node_t		l2ad_node;	/* device list node */
} l2arc_dev_t;

static list_t L2ARC_dev_list;			/* device list */
static list_t *l2arc_dev_list;			/* device list pointer */
static kmutex_t l2arc_dev_mtx;			/* device list mutex */
static l2arc_dev_t *l2arc_dev_last;		/* last device used */
static kmutex_t l2arc_buflist_mtx;		/* mutex for all buflists */
static uint64_t l2arc_ndev;			/* number of devices */

typedef struct l2arc_buf {
	arc_hdr_t	b_id;

	/* protected by the hash lock */
	l2arc_dev_t		*b_dev;		/* L2ARC device */
	uint64_t		b_daddr;	/* disk address, offset byte */
	arc_cksum_t		*b_cksum;

	/* protected by list lock */
	list_node_t		b_link;
} l2arc_buf_t;

typedef struct l2arc_read_callback {
	l2arc_buf_t		*l2rcb_hdr;	/* L2ARC hdr */
	arc_read_callback_t	*l2rcb_cb;	/* original read callback */
	blkptr_t		l2rcb_bp;	/* original blkptr */
	zbookmark_t		l2rcb_zb;	/* original bookmark */
	int			l2rcb_flags;	/* original flags */
} l2arc_read_callback_t;

static kmutex_t l2arc_feed_thr_lock;
static kcondvar_t l2arc_feed_thr_cv;
static uint8_t l2arc_thread_exit;

static arc_hdr_t *l2arc_hdr_from_cookie(void *cookie);
static void l2arc_destroy_cookie(void *cookie);
static void *l2arc_cache_check(spa_t *spa, arc_hdr_t *hdr);
static void l2arc_read_done(zio_t *zio);
static int l2arc_read(zio_t *pio, spa_t *spa, const blkptr_t *bp,
    void *data, void *l2cookie, arc_read_callback_t *cb,
    int priority, enum zio_flag flags, const zbookmark_t *zb);

/* dummy function used to tag anonymous refs on loan */
static void
arc_ref_onloan(void *private)
{
	panic("Evicting loaned data! arg=%p", private);
}

/* dummy function used to tag anonymous refs after arc_make_writable() */
static void
arc_write_pending(void *private)
{
	panic("Evicting anonymous data! arg=%p", private);
}

/* dummy function used to tag anonymous refs after arc_write() */
static void
arc_write_in_progress(void *private)
{
	panic("Evicting while writing! arg=%p", private);
}

static uint64_t
arc_hash(uint64_t spa, const dva_t *dva, uint64_t birth)
{
	uint8_t *vdva = (uint8_t *)dva;
	uint64_t crc = -1ULL;
	int i;

	ASSERT(zfs_crc64_table[128] == ZFS_CRC64_POLY);

	for (i = 0; i < sizeof (dva_t); i++)
		crc = (crc >> 8) ^ zfs_crc64_table[(crc ^ vdva[i]) & 0xFF];

	crc ^= (spa>>8) ^ birth;

	return (crc);
}

#define	ID_ANONYMOUS(hdr)					\
	((hdr)->h_dva.dva_word[0] == 0 &&			\
	(hdr)->h_dva.dva_word[1] == 0 &&			\
	(hdr)->h_birth == 0)

#define	ID_EQUAL(spa, dva, birth, hdr)			\
	((hdr)->h_dva.dva_word[0] == (dva)->dva_word[0]) &&	\
	((hdr)->h_dva.dva_word[1] == (dva)->dva_word[1]) &&	\
	((hdr)->h_birth == birth) && ((hdr)->h_spa == spa)

/*
 * Look up a BP in the hash table.  If we find a matching arc buffer,
 * return a pointer to it (after placing a hold on it).  Otherwise
 * return NULL.  If we find an L2 entry (and l2cookie is non-null),
 * return a cookie for the entry.
 */
static arc_buf_t *
arc_hash_find(spa_t *spa, const blkptr_t *bp, void *tag, void **l2cookie)
{
	const dva_t *dva = BP_IDENTITY(bp);
	uint64_t birth = BP_PHYSICAL_BIRTH(bp);
	uint64_t id = spa_guid(spa);
	uint32_t idx = HASH_INDEX(id, dva, birth);
	kmutex_t *hash_lock = &(HASH_LOCK_NTRY(idx).ht_lock);
	arc_hdr_t *hdr;

	if (l2cookie)
		*l2cookie = NULL;

	mutex_enter(hash_lock);
	for (hdr = arc_hash_table.ht_table[idx]; hdr; hdr = hdr->h_next) {
		if (ID_EQUAL(id, dva, birth, hdr)) {
			arc_buf_t *buf;

			if (!BUF_GHOST(hdr)) {
				buf = (arc_buf_t *)hdr;
				arc_hold(buf, tag);
			} else {
				buf = NULL;
				if (l2cookie)
					*l2cookie = l2arc_cache_check(spa, hdr);
			}
			mutex_exit(hash_lock);
			return (buf);
		}
	}
	mutex_exit(hash_lock);
	return (NULL);
}

/*
 * Insert an entry into the hash table.  If there is already an element
 * equal to the new entry in the hash table, then:
 * 	1. if the existing entry is a ghost entry, replace it.
 * 	2. else, return the exiting entry (the new entry will not be inserted).
 * Returns NULL if insert is successfull.
 */
static arc_buf_t *
arc_hash_insert(arc_buf_t *buf, boolean_t promote, void *tag)
{
	arc_hdr_t *hdr = &buf->b_id;
	uint32_t idx = HASH_INDEX(hdr->h_spa, &hdr->h_dva, hdr->h_birth);
	kmutex_t *hash_lock = &(HASH_LOCK_NTRY(idx).ht_lock);
	arc_hdr_t **hdrp, *entry;
	uint32_t i = 0;

	ASSERT(!BUF_HASHED(hdr));
	mutex_enter(hash_lock);
	hdr->h_hash_idx = idx;
	hdrp = &arc_hash_table.ht_table[idx];
	while ((entry = *hdrp) != NULL) {
		if (ID_EQUAL(hdr->h_spa, &hdr->h_dva, hdr->h_birth, entry)) {
			if (BUF_GHOST(entry)) {
				*hdrp = entry->h_next;
				ARC_FLAG_CLEAR(entry, HASHED);
				if (BUF_L2(entry)) {
					ASSERT(buf->b_cookie == NULL);
					buf->b_cookie = entry;
					entry->h_next = &buf->b_id;
				} else {
					entry->h_next = NULL;
				}
				arc_adapt(buf->b_size, BUF_STATE(entry));
				ASSERT(!BUF_PREFETCHED(entry) ||
				    ARC_FLAG_ISSET(entry, FREQUENTLY_USED));
				if (promote)
					arc_promote_buf(buf);
#ifdef ZFS_DEBUG
				continue;
#else
				break;
#endif
			} else {
				/* we should not have also found an L2 entry */
				ASSERT(buf->b_cookie == NULL);
				arc_hold((arc_buf_t *)entry, tag);
			}
			mutex_exit(hash_lock);
			return ((arc_buf_t *)entry);
		}
		hdrp = &entry->h_next;
		i++;
	}

	/* not found, add this entry */
	hdr->h_next = arc_hash_table.ht_table[idx];
	arc_hash_table.ht_table[idx] = hdr;
	ARC_FLAG_SET(buf, HASHED);
	arc_hold(buf, &arc_hash_table);
	arc_hold(buf, tag);
	mutex_exit(hash_lock);

	/* collect some hash table performance data */
	if (i > 0) {
		ARCSTAT_BUMP(arcstat_hash_collisions);
		if (i == 1)
			ARCSTAT_BUMP(arcstat_hash_chains);

		ARCSTAT_MAX(arcstat_hash_chain_max, i);
	}

	ARCSTAT_BUMP(arcstat_hash_elements);
	ARCSTAT_MAXSTAT(arcstat_hash_elements);

	return (NULL);
}

/*
 * Remove an entry from the hash table.
 */
static void
arc_hash_remove(arc_hdr_t *hdr)
{
	arc_hdr_t *fhdr, **hdrp;
	kmutex_t *hash_lock = HASH_LOCK(hdr);
	uint32_t idx = hdr->h_hash_idx;

	ASSERT(MUTEX_HELD(hash_lock));
	ASSERT(BUF_HASHED(hdr));

	hdrp = &arc_hash_table.ht_table[hdr->h_hash_idx];
	while ((fhdr = *hdrp) != hdr)
		hdrp = &fhdr->h_next;
	*hdrp = hdr->h_next;
	hdr->h_next = NULL;
	hdr->h_hash_idx = 0;
	ARC_FLAG_CLEAR(hdr, HASHED);
	if (!BUF_GHOST(hdr)) {
		arc_buf_t *buf = (arc_buf_t *)hdr;
		(void) refcount_remove(&buf->b_active, &arc_hash_table);
	}

	/* collect some hash table performance data */
	ARCSTAT_BUMPDOWN(arcstat_hash_elements);

	if (arc_hash_table.ht_table[idx] &&
	    arc_hash_table.ht_table[idx]->h_next == NULL)
		ARCSTAT_BUMPDOWN(arcstat_hash_chains);
}

/*
 * Replace a hash table entry with a new entry.  This is most often
 * used to swap a ghost entry in when we evict data from the arc,
 * but also is used in sync-to-convergence.
 */
static void
arc_hash_replace(arc_buf_t *old, arc_hdr_t *new)
{
	arc_hdr_t **hdrp;
	arc_hdr_t *hdr = &old->b_id;
	kmutex_t *hash_lock = HASH_LOCK(old);

	ASSERT(MUTEX_HELD(hash_lock));
	ASSERT(BUF_HASHED(old));
	ASSERT(hdr->h_hash_idx == new->h_hash_idx);

	hdrp = &arc_hash_table.ht_table[hdr->h_hash_idx];
	while (*hdrp != hdr)
		hdrp = &(*hdrp)->h_next;
	*hdrp = new;
	new->h_next = hdr->h_next;
	hdr->h_next = NULL;
	hdr->h_hash_idx = 0;
	ARC_FLAG_CLEAR(old, HASHED);
	(void) refcount_remove(&old->b_active, &arc_hash_table);
	ARC_FLAG_SET(new, HASHED);
	if (!BUF_GHOST(new)) {
		arc_buf_t *buf = (arc_buf_t *)new;
		arc_hold(buf, &arc_hash_table);
	}
}

static void
arc_hash_init(void)
{
	uint64_t *ct;
	uint64_t hsize = 1ULL << 12;
	uint64_t nlocks;
	int i, j;

	/*
	 * Target hash table is big enough to fill all of physical memory
	 * with an average 64K block size.  The table will take up
	 * totalmem*sizeof(void*)/64K (eg. 128KB/GB with 8-byte pointers).
	 * Note: we limit ht_mask to 32-bits.
	 */
	while (hsize * arc_average_blocksize < physmem * PAGESIZE)
		hsize <<= 1;

	do {
		arc_hash_table.ht_table =
		    kmem_zalloc(hsize * sizeof (void*), KM_NOSLEEP);
		if (arc_hash_table.ht_table != NULL)
			break;
		hsize >>= 1;
	} while (hsize > (1ULL << 8));
	VERIFY(arc_hash_table.ht_table != NULL);
	arc_hash_table.ht_mask = hsize - 1;

	for (i = 0; i < 256; i++)
		for (ct = zfs_crc64_table + i, *ct = i, j = 8; j > 0; j--)
			*ct = (*ct >> 1) ^ (-(*ct & 1) & ZFS_CRC64_POLY);

	/* calculate the number of locks for the hash table */
	nlocks = 1 << highbit(MAX(hsize / BUF_LOCK_RANGE, BUF_MIN_LOCKS) - 1);
	arc_hash_table.ht_lock_mask = nlocks - 1;
	arc_hash_table.ht_locks =
	    kmem_alloc(nlocks * sizeof (struct ht_lock), KM_SLEEP);

	for (i = 0; i < nlocks; i++)
		mutex_init(&arc_hash_table.ht_locks[i].ht_lock,
		    NULL, MUTEX_DEFAULT, NULL);
}

static void
arc_hash_fini(void)
{
	int i;

	kmem_free(arc_hash_table.ht_table,
	    ((uint64_t)arc_hash_table.ht_mask + 1) * sizeof (void *));
	for (i = 0; i <= arc_hash_table.ht_lock_mask; i++)
		mutex_destroy(&arc_hash_table.ht_locks[i].ht_lock);
	kmem_free(arc_hash_table.ht_locks,
	    (arc_hash_table.ht_lock_mask + 1) * sizeof (struct ht_lock));
}

/*
 * Global data structures and functions for the kmem caches.
 */
static kmem_cache_t *buf_cache;
static kmem_cache_t *ref_cache;
static kmem_cache_t *ghost_cache;

/*
 * Constructor callbacks - called when the cache is empty
 * and a new buf is requested.
 */
/* ARGSUSED */
static int
ghost_cons(void *vhdr, void *unused, int kmflag)
{
	arc_ghost_t *ghost = vhdr;

	bzero(ghost, sizeof (arc_ghost_t));
	arc_space_consume(sizeof (arc_ghost_t), ARC_SPACE_BUFS);

	return (0);
}

/* ARGSUSED */
static int
buf_cons(void *vbuf, void *unused, int kmflag)
{
	arc_buf_t *buf = vbuf;

	bzero(buf, sizeof (arc_buf_t));
	refcount_create(&buf->b_active);
	mutex_init(&buf->b_lock, NULL, MUTEX_DEFAULT, NULL);
	arc_space_consume(sizeof (arc_buf_t), ARC_SPACE_BUFS);

	return (0);
}

/* ARGSUSED */
static int
ref_cons(void *vref, void *unused, int kmflag)
{
	arc_ref_t *ref = vref;

	bzero(ref, sizeof (arc_ref_t));
	rw_init(&ref->r_lock, NULL, RW_DEFAULT, NULL);
	arc_space_consume(sizeof (arc_ref_t), ARC_SPACE_BUFS);

	return (0);
}

/*
 * Destructor callbacks - called when a cached buf is no longer required.
 */
/* ARGSUSED */
static void
ghost_dest(void *vghost, void *unused)
{
	ASSERT(ID_ANONYMOUS((arc_hdr_t *)vghost));
	arc_space_return(sizeof (arc_ghost_t), ARC_SPACE_BUFS);
}

/* ARGSUSED */
static void
buf_dest(void *vbuf, void *unused)
{
	arc_buf_t *buf = vbuf;

	refcount_destroy(&buf->b_active);
	mutex_destroy(&buf->b_lock);
	arc_space_return(sizeof (arc_buf_t), ARC_SPACE_BUFS);
}

/* ARGSUSED */
static void
ref_dest(void *vref, void *unused)
{
	arc_ref_t *ref = vref;

	rw_destroy(&ref->r_lock);
	arc_space_return(sizeof (arc_ref_t), ARC_SPACE_BUFS);
}

/*
 * Reclaim callbacks -- invoked when memory is low.
 */
/* ARGSUSED */
static void
buf_recl(void *unused)
{
	dprintf("buf_recl called\n");
	/*
	 * We need to check for 'arc_dead' because umem calls the reclaim
	 * func when we destroy the buf cache after we do arc_fini().
	 */
	if (!arc_dead && mutex_tryenter(&arc_reclaim_thr_lock)) {
		cv_signal(&arc_reclaim_thr_cv);
		mutex_exit(&arc_reclaim_thr_lock);
	}
}

/*
 * Return the number of bytes in the evictable list
 * for the indicated state.
 */
static uint64_t
arc_evictable_memory(arc_state_t *state)
{
	arc_evict_list_t *list = &state->s_evictable;
	uint64_t size;

	mutex_enter(&list->l_update_lock);
	size = list->l_size;
	mutex_exit(&list->l_update_lock);
	return (size);
}

/*
 * Return TRUE if the checksum matches the data.
 */
static int
arc_cksum_valid(arc_cksum_t *cksum, void *data, uint64_t size)
{
	zio_checksum_info_t *ci = &zio_checksum_table[cksum->c_func];
	zio_cksum_t zc;

	ASSERT((uint_t)cksum->c_func < ZIO_CHECKSUM_FUNCTIONS);
	ASSERT(ci->ci_func[0] != NULL);

	ci->ci_func[0](data, size, &zc);
	return (ZIO_CHECKSUM_EQUAL(cksum->c_value, zc));
}

/*
 * Verify that the checksum, if present, is correct for the buffer.
 */
static void
arc_cksum_verify(arc_buf_t *buf)
{
	ASSERT(MUTEX_HELD(&buf->b_lock));

	if (!(zfs_flags & ZFS_DEBUG_MODIFY) || !BUF_CKSUM_EXISTS(buf))
		return;

	if (!arc_cksum_valid(buf->b_cksum, buf->b_data, buf->b_size))
		panic("buffer modified while frozen!");
}

/*
 * Compute a default checksum for the data.
 */
static arc_cksum_t *
arc_cksum_compute(void *data, uint64_t size)
{
	arc_cksum_t *cksum = kmem_alloc(sizeof (arc_cksum_t), KM_SLEEP);

	cksum->c_func = ZIO_CHECKSUM_FLETCHER_2;
	fletcher_2_native(data, size, &cksum->c_value);
	return (cksum);
}

/*
 * Add a checksum to the buffer if it doesn't already exist.
 */
static void
arc_buf_cksum_compute(arc_buf_t *buf)
{
	ASSERT(MUTEX_HELD(&buf->b_lock));
	if (buf->b_cksum == NULL) {
		buf->b_cksum = arc_cksum_compute(buf->b_data, buf->b_size);
		ARC_FLAG_CLEAR(buf, BP_CKSUM_INUSE);
	} else {
		arc_cksum_verify(buf);
	}
}

/*
 * Set a checksum for the buffer. Try to use the in-hand bp checksum
 * if valid (it will not be valid if the data is compressed or
 * encrypted on disk).
 */
static void
arc_cksum_set(arc_buf_t *buf, void *data, blkptr_t *bp)
{
	ASSERT(MUTEX_HELD(&buf->b_lock));

	/* XXX - no cksum if hole... could also just arc_cksum_compute() */
	if (BP_IS_HOLE(bp)) {
		if (BUF_CKSUM_EXISTS(buf)) {
			kmem_free(buf->b_cksum, sizeof (arc_cksum_t));
			buf->b_cksum = NULL;
		}
		return;
	}

	if (!BUF_CKSUM_EXISTS(buf))
		buf->b_cksum = kmem_alloc(sizeof (arc_cksum_t), KM_SLEEP);

	buf->b_cksum->c_func = BP_GET_CHECKSUM(bp);
	buf->b_cksum->c_value = bp->blk_cksum;
	if (arc_cksum_valid(buf->b_cksum, data, buf->b_size)) {
		ARC_FLAG_SET(buf, BP_CKSUM_INUSE);
	} else {
		kmem_free(buf->b_cksum, sizeof (arc_cksum_t));
		buf->b_cksum = NULL;
		arc_buf_cksum_compute(buf);
	}
}

static char *arc_ref_frozen_flag = "this ref is frozen";

/*
 * Add a checksum to this reference so that we can detect if the
 * the buffer is modified after this point (without a thaw).
 */
void
arc_ref_freeze(arc_ref_t *ref)
{
	if (!(zfs_flags & ZFS_DEBUG_MODIFY)) {
		if (REF_ANONYMOUS(ref) && ref->r_private == NULL)
			ref->r_private = arc_ref_frozen_flag;
		return;
	}

	rw_enter(&ref->r_lock, RW_READER);
	if (!REF_ANONYMOUS(ref)) {
		mutex_enter(&ref->r_buf->b_lock);
		ASSERT(BUF_CKSUM_EXISTS(ref->r_buf));
		arc_cksum_verify(ref->r_buf);
		mutex_exit(&ref->r_buf->b_lock);
	} else if (!REF_CKSUM_EXISTS(ref)) {
		ref->r_private = arc_cksum_compute(ref->r_data, ref->r_size);
	} else if (!arc_cksum_valid(ref->r_private, ref->r_data, ref->r_size)) {
		panic("buffer modified while frozen!");
	}
	rw_exit(&ref->r_lock);
}

/*
 * Remove any existing checksum for this reference
 * (because we are about to modify the contents of its data buffer).
 */
void
arc_ref_thaw(arc_ref_t *ref)
{
	rw_enter(&ref->r_lock, RW_READER);
	if (!REF_ANONYMOUS(ref))
		panic("modifying non-anon buffer!");

	if (!(zfs_flags & ZFS_DEBUG_MODIFY)) {
		ref->r_private = NULL;
	} else if (REF_CKSUM_EXISTS(ref)) {
		if (!arc_cksum_valid(ref->r_private, ref->r_data, ref->r_size))
			panic("buffer modified while frozen!");
		kmem_free(ref->r_private, sizeof (arc_cksum_t));
		ref->r_private = NULL;
	}
	rw_exit(&ref->r_lock);
}

/*
 * Remove this buffer from the list of evictable buffers.
 */
static void
arc_remove_from_evictables(arc_buf_t *buf)
{
	arc_evict_list_t *list = &buf->b_state->s_evictable;

	ASSERT(buf->b_link != NULL);
	mutex_enter(&list->l_link_lock);
	buf->b_link->e_buf = NULL;
	buf->b_link = NULL;
	mutex_exit(&list->l_link_lock);

	mutex_enter(&list->l_update_lock);
	ASSERT3U(list->l_size, >=, buf->b_size);
	list->l_size -= buf->b_size;
	mutex_exit(&list->l_update_lock);
}

/*
 * Make this buffer eligible for eviction by adding it to the list
 * of evictable buffers in this state.  If this buf is already on
 * the eviction list (because we were lazy about removing it when
 * it was last accessed) then move it to the list head.
 */
static void
arc_add_to_evictables(arc_buf_t *buf)
{
	arc_evict_list_t *list = &buf->b_state->s_evictable;
	arc_elink_t *link;

	ASSERT(BUF_HASHED(buf));
	if (buf->b_link) {
		link = buf->b_link;
		if (link == list_head(&list->l_insert)) {
			/* don't bother moving this buf */
			return;
		} else if (link->e_gen == list->l_generation) {
			/* move this buf to the head of the insert list */
			mutex_enter(&list->l_update_lock);
			if (link->e_gen == list->l_generation) {
				list_remove(&list->l_insert, link);
				list_insert_head(&list->l_insert, link);
				mutex_exit(&list->l_update_lock);
				return;
			}
			mutex_exit(&list->l_update_lock);
		}
		/* unhook this buffer from its link on l_remove */
		arc_remove_from_evictables(buf);
	}

	link = kmem_zalloc(sizeof (arc_elink_t), KM_SLEEP);
	/* Safe to do without the link lock, as we are not on the list yet */
	link->e_buf = buf;
	buf->b_link = link;
	mutex_enter(&list->l_update_lock);
	link->e_gen = list->l_generation;
	list_insert_head(&list->l_insert, link);
	list->l_size += buf->b_size;
	buf->b_last_access = ddi_get_lbolt();
	mutex_exit(&list->l_update_lock);
}

/*
 * Place a hold on this buffer, preventing if from being evicted.
 */
static void
arc_hold(arc_buf_t *buf, void *tag)
{
	/*
	 * NOTE: if this buffer is currently on the eviction list, we
	 * don't bother removing it here.  We will either reposition it
	 * on the list later when we remove our last hold, or we will
	 * remove it from the list if we come accross it in an eviction
	 * scan.
	 */
	(void) refcount_add(&buf->b_active, tag);
}

/*
 * Remove a hold from an arc buffer.
 * If this is the last hold:
 *	- add it to the evictables list if it is still hashed
 *	- else destroy the buffer
 * Note, given the possible side-effects, we can't have the b_lock
 * held when calling this function.
 */
static void
arc_rele(arc_buf_t *buf, void *tag)
{
	int cnt;

	mutex_enter(&buf->b_lock);
	cnt = refcount_remove(&buf->b_active, tag);

	if (cnt == 1 && BUF_HASHED(buf)) {
		arc_add_to_evictables(buf);
		mutex_exit(&buf->b_lock);
	} else if (cnt == 0 && buf->b_link == NULL) {
		arc_data_free(buf);
		mutex_exit(&buf->b_lock);
		arc_destroy_buf(buf);
	} else {
		mutex_exit(&buf->b_lock);
	}
}

/*
 * Move the "insert" list contents to the head of the "remove" list.
 * This makes sure that all evictable data is on the remove list.
 */
static void
arc_collect_evictables(arc_evict_list_t *list)
{
	ASSERT(MUTEX_HELD(&list->l_evict_lock));

	mutex_enter(&list->l_update_lock);
	list_move_tail(&list->l_insert, &list->l_remove);
	list_move_tail(&list->l_remove, &list->l_insert);
	list->l_generation += 1;
	mutex_exit(&list->l_update_lock);
}

/*
 * Move a buffer from the MRU state to the MFU state.
 */
static void
arc_promote_buf(arc_buf_t *buf)
{
	uint64_t delta = buf->b_size;

	ASSERT(MUTEX_HELD(&buf->b_lock));
	ASSERT(buf->b_state == arc_mru);
	ASSERT(!ID_ANONYMOUS(&buf->b_id));

	ASSERT3U(arc_mru->s_size, >=, delta);
	atomic_add_64(&arc_mru->s_size, -delta);

	if (buf->b_link)
		arc_remove_from_evictables(buf);

	buf->b_state = arc_mfu;
	atomic_add_64(&arc_mfu->s_size, delta);
	ARC_FLAG_SET(buf, FREQUENTLY_USED);
}

/*
 * Move a buffer to the ghost state.
 * This can only be done while under the hash lock.
 */
static void
arc_kill_buf(arc_buf_t *buf, boolean_t noghost)
{
	arc_hdr_t *hdr;
	arc_ghost_t *ghost;
	arc_state_t *state = (buf->b_state == arc_mru) ?
	    arc_mru_ghost : arc_mfu_ghost;

	ASSERT(BUF_HASHED(buf));
	ASSERT(!BUF_REFERENCED(buf));
	ASSERT(MUTEX_HELD(HASH_LOCK(buf)));
	ASSERT(buf->b_state == arc_mru || buf->b_state == arc_mfu);
	ASSERT(buf->b_data == NULL);
	ASSERT(buf->b_link == NULL);

	if (buf->b_cookie) {
		/*
		 * This block is cached in the l2 arc, get the
		 * ghost record from there.
		 */
		ghost = NULL;
		hdr = l2arc_hdr_from_cookie(buf->b_cookie);
		ASSERT(ARC_FLAG_ISSET(hdr, GHOST));
		buf->b_cookie = NULL;
		if (buf->b_state == arc_mfu)
			ARC_FLAG_SET(hdr, FREQUENTLY_USED);
		if (buf->b_last_access)
			ARC_FLAG_CLEAR(hdr, PREFETCHED);
		else
			ARC_FLAG_SET(hdr, PREFETCHED);
	} else if (buf->b_last_access && !noghost) {
		/*
		 * Create a new ghost record for this block.
		 */
		ghost = kmem_cache_alloc(ghost_cache, KM_PUSHPAGE);
		hdr = &ghost->g_id;
		ARC_FLAG_CLEAR(buf, PREFETCHED);
		ghost->g_id = buf->b_id;
		ARC_FLAG_CLEAR(ghost, HASHED);
		ARC_FLAG_SET(ghost, GHOST);
		ASSERT((state == arc_mfu_ghost) ==
		    ARC_FLAG_ISSET(ghost, FREQUENTLY_USED));
	} else {
		/*
		 * This block was never accessed, don't bother to
		 * keep a ghost history record.
		 */
		ASSERT(noghost || buf->b_state == arc_mru);
		arc_hash_remove(&buf->b_id);
		arc_destroy_buf(buf);
		return;
	}
	ASSERT(ID_EQUAL(hdr->h_spa, &hdr->h_dva, hdr->h_birth, &buf->b_id));

	/* adjust ghost state size */
	atomic_inc_64(&state->s_size);

	hdr->h_next = NULL;
	ASSERT(!BUF_HASHED(hdr));
	arc_hash_replace(buf, hdr);
	arc_destroy_buf(buf);

	/* add this buffer to the ghost list */
	if (ghost) {
		arc_evict_list_t *list = &state->s_evictable;

		ASSERT(!MUTEX_HELD(&list->l_update_lock));
		mutex_enter(&list->l_update_lock);
		list_insert_head(&list->l_insert, ghost);
		list->l_size += 1;
		mutex_exit(&list->l_update_lock);
	}
	ASSERT(BUF_HASHED(hdr));
}

/*
 * Increment the amount of space currently used by the arc.
 */
void
arc_space_consume(uint64_t space, arc_space_type_t type)
{
	switch (type) {
	case ARC_SPACE_DATA:
	case ARC_SPACE_METADATA:
		ARCSTAT_INCR(arcstat_data_size, space);
		break;
	case ARC_SPACE_OTHER:
		ARCSTAT_INCR(arcstat_other_size, space);
		break;
	case ARC_SPACE_BUFS:
		ARCSTAT_INCR(arcstat_buf_size, space);
		break;
	case ARC_SPACE_L2:
		ARCSTAT_INCR(arcstat_l2_hdr_size, space);
		break;
	}

	if (type != ARC_SPACE_DATA && type != ARC_SPACE_METADATA) {
		atomic_add_64(&arc_meta_used, space);
		if (arc_meta_max < arc_meta_used)
			arc_meta_max = arc_meta_used;
	}
	ASSERT3U(arc_meta_used, >=, ARCSTAT(arcstat_buf_size));
	atomic_add_64(&arc_size, space);
}

/*
 * Decrement the amount of space currently used by the arc.
 */
void
arc_space_return(uint64_t space, arc_space_type_t type)
{
	switch (type) {
	case ARC_SPACE_DATA:
	case ARC_SPACE_METADATA:
		ARCSTAT_INCR(arcstat_data_size, -space);
		break;
	case ARC_SPACE_OTHER:
		ARCSTAT_INCR(arcstat_other_size, -space);
		break;
	case ARC_SPACE_BUFS:
		ARCSTAT_INCR(arcstat_buf_size, -space);
		break;
	case ARC_SPACE_L2:
		ARCSTAT_INCR(arcstat_l2_hdr_size, -space);
		break;
	}

	if (type != ARC_SPACE_DATA && type != ARC_SPACE_METADATA) {
		ASSERT(arc_meta_used >= space);
		atomic_add_64(&arc_meta_used, -space);
	}
	ASSERT(arc_size >= space);
	atomic_add_64(&arc_size, -space);
}

/*
 * Allocate a new arc buffer using the identity and data provided.
 * Note that 'data' must be "anonymous" data.
 */
static arc_buf_t *
arc_buf_alloc(spa_t *spa, blkptr_t *bp, void *data)
{
	arc_buf_t *buf;

	buf = kmem_cache_alloc(buf_cache, KM_PUSHPAGE);
	ASSERT(ID_ANONYMOUS(&buf->b_id));
	ASSERT(buf->b_cksum == NULL);
	ASSERT(buf->b_cookie == NULL);
	buf->b_id.h_spa = spa_guid(spa);
	buf->b_id.h_flags = 0;
	buf->b_id.h_hash_idx = 0;
	buf->b_id.h_dva = *BP_IDENTITY(bp);
	buf->b_id.h_birth = BP_PHYSICAL_BIRTH(bp);
	buf->b_size = BP_GET_LSIZE(bp);
	buf->b_state = arc_mru;
	buf->b_data = data;
	buf->b_inactive = NULL;
	buf->b_last_access = 0;
	if (BP_IS_METADATA(bp))
		ARC_FLAG_SET(buf, METADATA);
	/* transfer 'size' from anon to mru */
	(void) refcount_remove_many(&arc_anon_size,
	    buf->b_size, (void *)(ulong_t)buf->b_size);
	atomic_add_64(&arc_mru->s_size, buf->b_size);
	return (buf);
}

/*
 * Add a new reference to an arc buffer.
 */
static arc_ref_t *
arc_add_ref(arc_buf_t *buf)
{
	arc_ref_t *ref;

	ref = kmem_cache_alloc(ref_cache, KM_PUSHPAGE);
	ref->r_buf = buf;
	ref->r_data = buf->b_data;
	ref->r_size = buf->b_size;
	ref->r_efunc = NULL;
	ref->r_private = NULL;
	arc_hold(buf, ref);
	return (ref);
}

/*
 * Create a new reference for a "hole" (a block of zeros).
 */
arc_ref_t *
arc_hole_ref(int size, boolean_t meta)
{
	arc_buf_t *buf;
	arc_ref_t *ref;

	if (meta)
		buf = &arc_metadata_buf0;
	else
		buf = &arc_data_buf0;
	ref = arc_add_ref(buf);
	ref->r_size = size;
	return (ref);
}

/*
 * Turn an anonymous reference into a reference to a hole.
 * Note: we assume the existing ref is to a block of zeros.
 * Note2: used when we end up "emptying" an indirect block after
 * freeing a range of blocks.
 */
void
arc_make_hole(arc_ref_t *ref)
{
	arc_buf_t *buf;
	boolean_t meta;

	ASSERT(REF_ANONYMOUS(ref));
	ASSERT(!REF_WRITING(ref));
	rw_enter(&ref->r_lock, RW_WRITER);
	meta = BUF_METADATA(ref->r_buf);
	if (meta)
		buf = &arc_metadata_buf0;
	else
		buf = &arc_data_buf0;
	ASSERT(bcmp(ref->r_data, buf->b_data, ref->r_size) == 0);
	if (REF_WRITE_PENDING(ref)) {
		(void) refcount_remove_many(&arc_iopending_size, ref->r_size,
		    (void*)(ulong_t)ref->r_size);
		ref->r_efunc = NULL;
	}
	arc_free_data_block(ref->r_data, ref->r_size, meta);
	ref->r_buf = buf;
	ref->r_data = buf->b_data;
	mutex_enter(&buf->b_lock);
	arc_hold(buf, ref);
	mutex_exit(&buf->b_lock);
	rw_exit(&ref->r_lock);
}

/*
 * Allocate an anonymous reference.
 * If 'data' is NULL, allocate a new empty buffer.
 */
arc_ref_t *
arc_alloc_ref(int size, boolean_t meta, void *data)
{
	arc_ref_t *ref = kmem_cache_alloc(ref_cache, KM_PUSHPAGE);

	ref->r_size = size;
	if (meta)
		ref->r_buf = &arc_anon_metadata;
	else
		ref->r_buf = &arc_anon_data;
	if (data == NULL)
		ref->r_data = arc_get_data_block(size, meta);
	else
		ref->r_data = data;
	ref->r_efunc = NULL;
	ref->r_private = NULL;
	return (ref);
}

/*
 * Loan out an anonymous arc buffer.  Loaned buffers must be returned
 * back to the arc before they can be used by the DMU or freed.
 */
arc_ref_t *
arc_loan_buf(int size)
{
	arc_ref_t *ref;

	ref = arc_alloc_ref(size, B_FALSE, NULL);
	ref->r_efunc = arc_ref_onloan;
	atomic_add_64(&arc_loaned_bytes, size);
	return (ref);
}

/*
 * Loan out a new reference to the buffer referenced by 'ref'.
 */
arc_ref_t *
arc_loan_ref(arc_ref_t *ref)
{
	arc_ref_t *nref;

	/*
	 * Loan out a clone of ref if it's anonymous.
	 */
	nref = (REF_ANONYMOUS(ref))? arc_clone_ref(ref) :
	    arc_add_ref(ref->r_buf);
	nref->r_efunc = arc_ref_onloan;
	atomic_add_64(&arc_loaned_bytes, nref->r_size);
	return (nref);
}

/*
 * Return a loaned arc buffer to the arc.  This does not free up the
 * ref, it just removes it from the "loaned" state.
 */
void
arc_return_ref(arc_ref_t *ref)
{
	ASSERT(ref->r_efunc == arc_ref_onloan);
	ASSERT(arc_loaned_bytes >= ref->r_size);
	atomic_add_64(&arc_loaned_bytes, -ref->r_size);
	ref->r_efunc = NULL;
}

/*
 * Create an anonymous clone of another ref
 * Note: we assume this is for a pending write
 */
arc_ref_t *
arc_clone_ref(arc_ref_t *from)
{
	arc_buf_t *buf;
	arc_ref_t *ref;

	rw_enter(&from->r_lock, RW_READER);
	buf = from->r_buf;
	mutex_enter(&buf->b_lock);
	ref = arc_alloc_ref(from->r_size, BUF_METADATA(buf), NULL);
	bcopy(from->r_data, ref->r_data, from->r_size);
	mutex_exit(&buf->b_lock);
	rw_exit(&from->r_lock);
	return (ref);
}

/*
 * Reactivate a reference to an arc buffer.  Note that the data
 * buffer address for the reactivated reference is not guaranteed
 * to be the same as it was when the ref was inactivated, so
 * callers must check for this.  Returns:
 *	ENOENT - buffer is no longer available, ref invalid
 *	0 - otherwise
 */
int
arc_reactivate_ref(arc_ref_t *ref)
{
	arc_buf_t *buf;
	arc_ref_t **refp;

	/*
	 * Check to see if this buffer is evicted.
	 */
	rw_enter(&ref->r_lock, RW_READER);
	ASSERT(REF_INACTIVE(ref));
	buf = ref->r_buf;
	if (buf == NULL || buf == &arc_eviction_head) {
		rw_exit(&ref->r_lock);
		return (ENOENT);
	}
	mutex_enter(&buf->b_lock);

	ASSERT(buf->b_state == arc_mru || buf->b_state == arc_mfu);

	/* remove ref from callback list */
	refp = &buf->b_inactive;
	while (*refp != ref)
		refp = &(*refp)->r_next;
	*refp = ref->r_next;

	ref->r_data = buf->b_data;
	ref->r_efunc = NULL;
	ref->r_private = NULL;
	arc_hold(buf, ref);
	DTRACE_PROBE1(arc__hit, arc_buf_t *, buf);
	arc_access(buf);
	mutex_exit(&buf->b_lock);
	rw_exit(&ref->r_lock);

	ARCSTAT_BUMP(arcstat_hits);
	ARCSTAT_CONDSTAT(!BUF_PREFETCHED(buf),
	    demand, prefetch, !BUF_METADATA(buf), data, metadata, hits);
	return (0);
}

/*
 * Free an anonymous data buffer.
 */
static void
arc_free_data_block(void *data, uint64_t size, boolean_t meta)
{
	ASSERT(data != NULL);
	if (meta) {
		arc_space_return(size, ARC_SPACE_METADATA);
		zio_buf_free(data, size);
	} else {
		arc_space_return(size, ARC_SPACE_DATA);
		zio_data_buf_free(data, size);
	}
	(void) refcount_remove_many(&arc_anon_size, size,
	    (void *)(ulong_t)size);
}

/*
 * Free the data for an arc buf.
 */
static void
arc_data_free(arc_buf_t *buf)
{
	ASSERT(MUTEX_HELD(&buf->b_lock));
	ASSERT(buf->b_inactive == NULL);
	ASSERT(refcount_count(&buf->b_active) == BUF_HASHED(buf));

	if (buf->b_data == NULL)
		return;

	if (BUF_METADATA(buf)) {
		arc_space_return(buf->b_size, ARC_SPACE_METADATA);
		zio_buf_free(buf->b_data, buf->b_size);
	} else {
		arc_space_return(buf->b_size, ARC_SPACE_DATA);
		zio_data_buf_free(buf->b_data, buf->b_size);
	}

	ASSERT3U(buf->b_state->s_size, >=, buf->b_size);
	atomic_add_64(&buf->b_state->s_size, -buf->b_size);

	ASSERT(buf->b_link == NULL);
	buf->b_data = NULL;
}

/*
 * Disassociate a reference from an arc buf.
 */
static void
arc_remove_ref(arc_ref_t *ref)
{
	arc_buf_t *buf = ref->r_buf;
	arc_ref_t **refp = &buf->b_inactive;

	ASSERT(MUTEX_HELD(&buf->b_lock));
	ASSERT(REF_INACTIVE(ref));
	ASSERT(ref->r_efunc);

	/* remove the ref from the callback list */
	while (*refp != ref)
		refp = &(*refp)->r_next;
	*refp = ref->r_next;
	ref->r_next = NULL;
	ref->r_buf = NULL;
}

/*
 * Add a reference to the eviction queue.
 */
static void
arc_queue_ref(arc_ref_t *ref)
{
	ASSERT(RW_WRITE_HELD(&ref->r_lock));
	ASSERT(ref->r_efunc);
	ASSERT(REF_INACTIVE(ref));

	if (ref->r_buf)
		arc_remove_ref(ref);
	ref->r_buf = &arc_eviction_head;

	mutex_enter(&arc_eviction_mtx);
	ref->r_next = arc_eviction_head.b_inactive;
	arc_eviction_head.b_inactive = ref;
	mutex_exit(&arc_eviction_mtx);
}

/*
 * Destroy a reference.
 */
static void
arc_destroy_ref(arc_ref_t *ref)
{
	ASSERT(ref->r_buf == NULL);
	ASSERT(ref->r_data == NULL);
	ASSERT(ref->r_efunc == NULL);
	ASSERT(ref->r_private == NULL);
	kmem_cache_free(ref_cache, ref);
}

/*
 * Destroy a buffer.
 */
static void
arc_destroy_buf(arc_buf_t *buf)
{
	ASSERT(!MUTEX_HELD(&buf->b_lock));
	ASSERT(!BUF_HASHED(buf));
	ASSERT(!BUF_REFERENCED(buf));
	ASSERT(!BUF_L2CACHED(buf));
	ASSERT(buf->b_link == NULL);
	ASSERT(buf->b_data == NULL);

	if (buf->b_cksum) {
		kmem_free(buf->b_cksum, sizeof (arc_cksum_t));
		buf->b_cksum = NULL;
	}

	ASSERT3P(buf->b_id.h_next, ==, NULL);

	bzero(&buf->b_id.h_dva, sizeof (dva_t));
	buf->b_id.h_birth = 0;
	buf->b_last_access = 0;
	kmem_cache_free(buf_cache, buf);
}

/*
 * Clean up a reference and then destroy it.
 */
void
arc_free_ref(arc_ref_t *ref)
{
	arc_buf_t *buf;

	rw_enter(&ref->r_lock, RW_WRITER);
	if (REF_WRITE_PENDING(ref)) {
		/* we are not going to write this buf after all */
		(void) refcount_remove_many(&arc_iopending_size, ref->r_size,
		    (void*)(ulong_t)ref->r_size);
		ref->r_efunc = NULL;
	}
	/* cannot free a reference if there is a callback registered */
	ASSERT(ref->r_efunc == NULL);

	buf = ref->r_buf;
	mutex_enter(&buf->b_lock);
	if (ref->r_private != NULL) {
		ASSERT(REF_ANONYMOUS(ref));
		if (zfs_flags & ZFS_DEBUG_MODIFY) {
			ASSERT(arc_cksum_valid(ref->r_private,
			    ref->r_data, ref->r_size));
			kmem_free(ref->r_private, sizeof (arc_cksum_t));
		}
		ref->r_private = NULL;
	} else {
		arc_cksum_verify(buf);
	}
	if (ref->r_data != buf->b_data)
		arc_free_data_block(ref->r_data,
		    ref->r_size, BUF_METADATA(buf));
	mutex_exit(&buf->b_lock);
	ref->r_data = NULL;
	if (!REF_ANONYMOUS(ref))
		arc_rele(buf, ref);
	ref->r_buf = NULL;
	rw_exit(&ref->r_lock);
	arc_destroy_ref(ref);
}

/*
 * Preserve a ref for later re-activation or eviction.
 */
void
arc_inactivate_ref(arc_ref_t *ref, arc_evict_func_t *func, void *private)
{
	arc_buf_t *buf;

	rw_enter(&ref->r_lock, RW_WRITER);
	buf = ref->r_buf;

	ASSERT(!REF_INACTIVE(ref));
	ASSERT(ref->r_efunc == NULL);
	ASSERT(BUF_HASHED(buf) || BUF_IS_HOLE(buf));

	if (REF_AUTONOMOUS(ref))
		arc_free_data_block(ref->r_data,
		    ref->r_size, BUF_METADATA(buf));
	ref->r_data = NULL;
	ref->r_efunc = func;
	ref->r_private = private;
	mutex_enter(&buf->b_lock);
	ref->r_next = buf->b_inactive;
	buf->b_inactive = ref;
	mutex_exit(&buf->b_lock);
	arc_rele(buf, ref);
	rw_exit(&ref->r_lock);
}

/*
 * Attempt to evict a buffer.  If successful, the buffer will be
 * either "killed" or "destroyed".
 */
static int
arc_evict_buf(arc_evict_list_t *list, arc_elink_t *link, uint64_t *size)
{
	arc_buf_t *buf;
	kmutex_t *hash_lock;

	*size = 0;

	mutex_enter(&list->l_link_lock);
	buf = (arc_buf_t *)link->e_buf;
	if (buf == NULL) {
		mutex_exit(&list->l_link_lock);
		return (0);
	}
	*size = buf->b_size;

	if (!mutex_tryenter(&buf->b_lock)) {
		mutex_exit(&list->l_link_lock);
		return (EWOULDBLOCK);
	}
	mutex_exit(&list->l_link_lock);

	if (refcount_is_zero(&buf->b_active)) {
		/*
		 * This buffer has been freed; destroy it.
		 */
		ASSERT(!BUF_HASHED(buf));
		arc_remove_from_evictables(buf);
		arc_data_free(buf);
		mutex_exit(&buf->b_lock);
		arc_destroy_buf(buf);
		return (0);
	}

	ASSERT(BUF_HASHED(buf));
	hash_lock = HASH_LOCK(buf);
	if (!mutex_tryenter(hash_lock)) {
		mutex_exit(&buf->b_lock);
		return (EWOULDBLOCK);
	}

	if (refcount_count(&buf->b_active) > 1) {
		/*
		 * This buffer has active refs.  Take it off
		 * the eviction list.
		 */
		mutex_exit(hash_lock);
		arc_remove_from_evictables(buf);
		mutex_exit(&buf->b_lock);
		return (EBUSY);
	}

	while (buf->b_inactive) {
		arc_ref_t *ref = buf->b_inactive;

		if (!rw_tryenter(&ref->r_lock, RW_WRITER)) {
			mutex_exit(hash_lock);
			mutex_exit(&buf->b_lock);
			return (EWOULDBLOCK);
		}
		arc_queue_ref(ref);
		rw_exit(&ref->r_lock);
	}

	arc_remove_from_evictables(buf);
	arc_data_free(buf);
	DTRACE_PROBE1(arc__evict_buf, arc_buf_t *, buf);
	mutex_exit(&buf->b_lock);
	arc_kill_buf(buf, B_FALSE);
	mutex_exit(hash_lock);
	return (0);
}

/*
 * Evict buffers from a state until we've removed the specified number of bytes
 */
static void
arc_evict_bytes(arc_state_t *state, int64_t bytes, boolean_t nowait)
{
	uint64_t bytes_evicted = 0, missed = 0;
	arc_elink_t *link, *link_prev = NULL;
	arc_evict_list_t *list = &state->s_evictable;
	list_t *remove = &list->l_remove;

	ASSERT(state == arc_mru || state == arc_mfu);

	if (nowait) {
		if (!mutex_tryenter(&list->l_evict_lock))
			return;
	} else {
		mutex_enter(&list->l_evict_lock);
	}

	if (list_head(remove) == NULL)
		arc_collect_evictables(list);

	for (link = list_tail(remove); link; link = link_prev) {
		uint64_t size;

		link_prev = list_prev(remove, link);

		if (arc_evict_buf(list, link, &size) == 0)
			bytes_evicted += size;
		else
			missed++;

		if (link->e_buf == NULL) {
			list_remove(remove, link);
			kmem_free(link, sizeof (arc_elink_t));
		}

		if (bytes_evicted >= bytes)
			break;
	}
	mutex_exit(&list->l_evict_lock);

	if (missed) {
		ARCSTAT_INCR(arcstat_mutex_miss, missed);
	}
}

/*
 * Remove ghost records from indicated state until we've removed the
 * specified number of records.  Destroy the records that are removed.
 */
static void
arc_evict_from_ghost(arc_state_t *state, int64_t cnt)
{
	arc_ghost_t *ghost, *ghost_prev;
	arc_evict_list_t *list = &state->s_evictable;
	int64_t ghosts_removed = 0;
	uint64_t hashed_ghosts_removed = 0;
	uint64_t ghosts_skipped = 0;

	ASSERT(GHOST_STATE(state));
	ASSERT(cnt > 0);

	mutex_enter(&list->l_evict_lock);

	arc_collect_evictables(list);

	for (ghost = list_tail(&list->l_remove); ghost; ghost = ghost_prev) {
		ghost_prev = list_prev(&list->l_remove, ghost);

		ASSERT(BUF_GHOST(ghost));
		ASSERT(!BUF_L2(ghost));

		if (BUF_HASHED(ghost)) {
			kmutex_t *hash_lock = HASH_LOCK(ghost);

			if (!mutex_tryenter(hash_lock)) {
				ghosts_skipped += 1;
				continue;
			}
			if (BUF_HASHED(ghost)) {
				arc_hash_remove(&ghost->g_id);
				hashed_ghosts_removed += 1;
			}
			mutex_exit(hash_lock);
		}
		ASSERT(ghost->g_id.h_next == NULL);

		list_remove(&list->l_remove, ghost);
		ghosts_removed += 1;

		bzero(ghost, sizeof (arc_ghost_t));
		kmem_cache_free(ghost_cache, ghost);
		ARCSTAT_BUMP(arcstat_deleted);
		DTRACE_PROBE1(arc__delete, arc_ghost_t *, ghost);

		if (hashed_ghosts_removed >= cnt)
			break;
	}

	mutex_enter(&list->l_update_lock);
	ASSERT3U(list->l_size, >=, ghosts_removed);
	list->l_size -= ghosts_removed;
	mutex_exit(&list->l_update_lock);

	ASSERT3U(state->s_size, >=, ghosts_removed);
	atomic_add_64(&state->s_size, -ghosts_removed);
	mutex_exit(&list->l_evict_lock);

	if (ghosts_skipped)
		ARCSTAT_INCR(arcstat_mutex_miss, ghosts_skipped);
}

/*
 * Destroy all ghost entries associated with the indicated spa.
 */
static void
arc_evict_spa_from_ghost(uint64_t spa)
{
	arc_state_t *state;
	arc_ghost_t *ghost;
	list_t kill_list;

	list_create(&kill_list,
	    sizeof (arc_ghost_t), offsetof(arc_ghost_t, g_link));

	/* move the target ghost records to the kill list */
	state = arc_mru_ghost;
	while (state != NULL) {
		arc_ghost_t *ghost_prev;
		arc_evict_list_t *list = &state->s_evictable;
		int64_t cnt = 0;

		mutex_enter(&list->l_evict_lock);

		arc_collect_evictables(list);

		for (ghost = list_tail(&list->l_remove);
		    ghost; ghost = ghost_prev) {
			ghost_prev = list_prev(&list->l_remove, ghost);

			ASSERT(BUF_GHOST(ghost));
			ASSERT(!BUF_L2(ghost));

			if (spa == 0 || ghost->g_id.h_spa == spa) {
				list_remove(&list->l_remove, ghost);
				list_insert_head(&kill_list, ghost);
				cnt++;
			}
		}

		mutex_enter(&list->l_update_lock);
		ASSERT3U(list->l_size, >=, cnt);
		list->l_size -= cnt;
		mutex_exit(&list->l_update_lock);
		ASSERT3U(state->s_size, >=, cnt);
		atomic_add_64(&state->s_size, -cnt);

		mutex_exit(&list->l_evict_lock);

		if (state == arc_mru_ghost)
			state = arc_mfu_ghost;
		else
			state = NULL;
	}

	/* process the kill list */
	while (ghost = list_tail(&kill_list)) {

		list_remove(&kill_list, ghost);

		if (BUF_HASHED(ghost)) {
			kmutex_t *hash_lock = HASH_LOCK(ghost);

			mutex_enter(hash_lock);
			if (BUF_HASHED(ghost))
				arc_hash_remove(&ghost->g_id);
			mutex_exit(hash_lock);
		}
		bzero(ghost, sizeof (arc_ghost_t));
		kmem_cache_free(ghost_cache, ghost);
		ARCSTAT_BUMP(arcstat_deleted);
		DTRACE_PROBE1(arc__delete_spa, arc_ghost_t *, ghost);
	}
}

/*
 * Evict all the buffers associated with the indicated spa.
 */
static void
arc_evict_spa(uint64_t spa)
{
	arc_state_t *state;
	arc_elink_t *link;
	list_t kill_list;

	list_create(&kill_list,
	    sizeof (arc_elink_t), offsetof(arc_elink_t, e_link));

	/*
	 * Pull out the target buffers from the eviction lists,
	 * put them on the kill list
	 */
	state = arc_mru;
	while (state != NULL) {
		arc_elink_t *link_prev = NULL;
		arc_evict_list_t *list = &state->s_evictable;
		list_t *remove = &list->l_remove;

		mutex_enter(&list->l_evict_lock);

		arc_collect_evictables(list);

		for (link = list_tail(remove); link; link = link_prev) {
			arc_buf_t *buf;

			link_prev = list_prev(remove, link);

			mutex_enter(&list->l_link_lock);
			buf = (arc_buf_t *)link->e_buf;
			if (buf == NULL) {
				list_remove(remove, link);
				kmem_free(link, sizeof (arc_elink_t));
			} else if (spa == 0 || buf->b_id.h_spa == spa) {
				list_remove(remove, link);
				list_insert_head(&kill_list, link);
			}
			mutex_exit(&list->l_link_lock);
		}
		mutex_exit(&list->l_evict_lock);

		if (state == arc_mru)
			state = arc_mfu;
		else
			state = NULL;
	}

	/* Process the kill list */
	while (link = list_tail(&kill_list)) {
		arc_buf_t *buf = (arc_buf_t *)link->e_buf;

		list_remove(&kill_list, link);

		if (buf == NULL) {
			kmem_free(link, sizeof (arc_elink_t));
			continue;
		}

		mutex_enter(&buf->b_lock);

		/* the buf may have moved while we obtained the lock */
		if (buf->b_link != link) {
			mutex_exit(&buf->b_lock);
			kmem_free(link, sizeof (arc_elink_t));
			continue;
		}
		arc_remove_from_evictables(buf);
		kmem_free(link, sizeof (arc_elink_t));

		/* we may be called while the pool is still active */
		while (buf->b_inactive) {
			arc_ref_t *ref = buf->b_inactive;
			if (!rw_tryenter(&ref->r_lock, RW_WRITER))
				break;
			arc_queue_ref(ref);
			rw_exit(&ref->r_lock);
		}
		if (buf->b_inactive) {
			/* this buf is becoing active, skip it */
			mutex_exit(&buf->b_lock);
			continue;
		}

		if (BUF_HASHED(buf)) {
			kmutex_t *hash_lock = HASH_LOCK(buf);
			mutex_enter(hash_lock);
			if (refcount_count(&buf->b_active) > 1) {
				/* this buf is still active, skip it */
				mutex_exit(hash_lock);
				mutex_exit(&buf->b_lock);
				continue;
			}
			if (buf->b_cookie) {
				l2arc_destroy_cookie(buf->b_cookie);
				buf->b_cookie = NULL;
			}
			arc_hash_remove(&buf->b_id);
			mutex_exit(hash_lock);
		}
		ASSERT(buf->b_cookie == NULL);
		ASSERT(refcount_is_zero(&buf->b_active));

		arc_data_free(buf);
		DTRACE_PROBE1(arc__evict, arc_buf_t *, buf);
		mutex_exit(&buf->b_lock);
		arc_destroy_buf(buf);
	}

	list_destroy(&kill_list);

	arc_evict_spa_from_ghost(spa);
}

/*
 * Adjust ghost lists.  The arc + the ghost arc should be (2 * arc_c).
 */
static void
arc_adjust_ghost(void)
{
	int64_t adjustment, cnt, mru_ghost_size, mfu_ghost_size;

	mru_ghost_size = arc_mru_ghost->s_evictable.l_size *
	    ARC_DEFAULT_GHOST_SIZE;

	/*
	 * MRU + ghost MRU should equal arc_c
	 */
	adjustment = arc_mru->s_size + mru_ghost_size - arc_c;

	if (adjustment > 0) {
		cnt = adjustment / ARC_DEFAULT_GHOST_SIZE;
		arc_evict_from_ghost(arc_mru_ghost, cnt == 0 ? 1: cnt);
		mru_ghost_size = arc_mru_ghost->s_evictable.l_size *
		    ARC_DEFAULT_GHOST_SIZE;
	}

	mfu_ghost_size = arc_mfu_ghost->s_evictable.l_size *
	    ARC_DEFAULT_GHOST_SIZE;

	/*
	 * ghost MFU + ghost MRU should equal arc_c
	 */
	adjustment = mru_ghost_size + mfu_ghost_size - arc_c;

	if (adjustment > 0) {
		cnt = adjustment / ARC_DEFAULT_GHOST_SIZE;
		arc_evict_from_ghost(arc_mfu_ghost, cnt == 0 ? 1 : cnt);
	}
}

/*
 * Adjust state lists.
 */
static void
arc_adjust(void)
{
	int64_t adjustment;

	/*
	 * Adjust MRU size
	 */

	adjustment = refcount_count(&arc_anon_size) +
	    arc_mru->s_size + arc_meta_used - arc_p;
	if (adjustment > 0)
		arc_evict_bytes(arc_mru, adjustment, B_FALSE);

	/*
	 * Adjust MFU size
	 */

	adjustment = arc_size - arc_c;
	if (adjustment > 0)
		arc_evict_bytes(arc_mfu, adjustment, B_FALSE);

	arc_adjust_ghost();
}

/*
 * Process the eviction queue.
 */
static void
arc_do_user_evicts(void)
{
	mutex_enter(&arc_eviction_mtx);
	while (arc_eviction_head.b_inactive != NULL) {
		arc_ref_t *ref = arc_eviction_head.b_inactive;
		arc_eviction_head.b_inactive = ref->r_next;
		rw_enter(&ref->r_lock, RW_WRITER);
		ref->r_buf = NULL;
		rw_exit(&ref->r_lock);
		mutex_exit(&arc_eviction_mtx);

		if (ref->r_efunc != NULL)
			ref->r_efunc(ref->r_private);

		ref->r_efunc = NULL;
		ref->r_private = NULL;
		kmem_cache_free(ref_cache, ref);
		mutex_enter(&arc_eviction_mtx);
	}
	mutex_exit(&arc_eviction_mtx);
}

/*
 * Flush all *evictable* data from the cache for the given spa.
 * NOTE: this will not touch "active" (i.e. referenced) data.
 */
void
arc_flush(spa_t *spa)
{
	uint64_t spa_id = spa ? spa_guid(spa) : 0;

	arc_evict_spa(spa_id);

	mutex_enter(&arc_reclaim_thr_lock);
	arc_do_user_evicts();
	mutex_exit(&arc_reclaim_thr_lock);
	ASSERT(spa || arc_eviction_head.b_inactive == NULL);
}

/*
 * Are we under memory pressure?
 */
static int
arc_reclaim_needed(void)
{
	uint64_t extra;

#ifdef _KERNEL

	if (needfree)
		return (1);

	/*
	 * Take 'desfree' extra pages, so we reclaim sooner, rather than later
	 */
	extra = desfree;

	/*
	 * check that we're out of range of the pageout scanner.  It starts to
	 * schedule paging if freemem is less than lotsfree and needfree.
	 * lotsfree is the high-water mark for pageout, and needfree is the
	 * number of needed free pages.  We add extra pages here to make sure
	 * the scanner doesn't start up while we're freeing memory.
	 */
	if (freemem < lotsfree + needfree + extra)
		return (1);

	/*
	 * Check to make sure that swapfs has enough space so that anon
	 * reservations can still succeed. anon_resvmem() checks that the
	 * availrmem is greater than swapfs_minfree, and the number of reserved
	 * swap pages.  We also add a bit of extra here just to prevent
	 * circumstances from getting really dire.
	 */
	if (availrmem < swapfs_minfree + swapfs_reserve + extra)
		return (1);

#if defined(__i386)
	/*
	 * If we're on an i386 platform, it's possible that we'll exhaust the
	 * kernel heap space before we ever run out of available physical
	 * memory.  Most checks of the size of the heap_area compare against
	 * tune.t_minarmem, which is the minimum available real memory that we
	 * can have in the system.  However, this is generally fixed at 25 pages
	 * which is so low that it's useless.  In this comparison, we seek to
	 * calculate the total heap-size, and reclaim if more than 3/4ths of the
	 * heap is allocated.  (Or, in the calculation, if less than 1/4th is
	 * free)
	 */
	if (vmem_size(heap_arena, VMEM_FREE) <
	    (vmem_size(heap_arena, VMEM_FREE | VMEM_ALLOC) >> 2))
		return (1);
#endif
	/*
	 * If zio data pages are being allocated out of a separate heap segment,
	 * then enforce that the size of available vmem for this area remains
	 * above about 1/16th free.
	 */
	if (zio_arena != NULL &&
	    vmem_size(zio_arena, VMEM_FREE) <
	    (vmem_size(zio_arena, VMEM_ALLOC) >> 4))
		return (1);

#else
	if (spa_get_random(100) == 0)
		return (1);
#endif
	return (0);
}

/*
 * Adapt arc info given the number of bytes we are trying to add and
 * the state that we are coming from.  This function is only called
 * when we are adding new content to the cache.
 */
static void
arc_adapt(int bytes, arc_state_t *state)
{
	int mult;
	int64_t arc_p_min = (arc_c >> arc_p_min_shift);
	int64_t arc_p_max = arc_c - arc_p_min;
	uint64_t mru_ghost_size, mfu_ghost_size;

	ASSERT(bytes > 0);
	mru_ghost_size = arc_mru_ghost->s_evictable.l_size;
	mfu_ghost_size = arc_mfu_ghost->s_evictable.l_size;

	/*
	 * Adapt the target size of the MRU list:
	 *	- if we just hit in the MRU ghost list, then increase
	 *	  the target size of the MRU list.
	 *	- if we just hit in the MFU ghost list, then increase
	 *	  the target size of the MFU list by decreasing the
	 *	  target size of the MRU list.
	 */
	if (state == arc_mru_ghost) {
		mult = (mru_ghost_size && mru_ghost_size < mfu_ghost_size) ?
		    (mfu_ghost_size/mru_ghost_size) : 1;

		atomic_add_64(&arc_p, (int64_t)(bytes * mult));
		if (arc_p > arc_p_max)
			arc_p = arc_p_max;
	} else {
		ASSERT(state == arc_mfu_ghost);
		mult = (mfu_ghost_size && mfu_ghost_size < mru_ghost_size) ?
		    (mru_ghost_size/mfu_ghost_size) : 1;

		atomic_add_64(&arc_p, -(int64_t)(bytes * mult));
		if ((int64_t)arc_p < arc_p_min)
			arc_p = arc_p_min;
		ASSERT((int64_t)arc_p >= 0);
	}
}

/*
 * Allocate a new data block.
 *
 * If we are at cache max, determine which cache should be victimized:
 *
 * If p <= arc_anon_size + sizeof(arc_mru) then
 *	The MRU cache is larger than the target size of 'p' (anon data
 *	is implicitly part of the MRU cache), attempt to victimize this
 *	cache for the new block.
 *
 * Else,
 *	The MFU cache must be over its limit, so attempt to victimize
 *	the MFU for the new block.
 */
static void *
arc_get_data_block(uint64_t size, boolean_t meta)
{
	void *data;

	/*
	 * If we're within (2 * maxblocksize) bytes of the target
	 * cache size, increase the target cache size
	 */
	if (!arc_no_grow && arc_c < arc_c_max &&
	    arc_size > arc_c - (2ULL << SPA_MAXBLOCKSHIFT)) {
		atomic_add_64(&arc_c, size);
		if (arc_c > arc_c_max)
			arc_c = arc_c_max;
	}

	if (arc_size >= arc_c || arc_no_grow) {
		arc_state_t *state;
		uint64_t mfu_target = arc_c - arc_p;

		state = (arc_mfu->s_size <= mfu_target &&
		    arc_evictable_memory(arc_mru) >= size) ? arc_mru : arc_mfu;

		arc_evict_bytes(state, size, B_TRUE);

		if (arc_reclaim_needed() &&
		    mutex_tryenter(&arc_reclaim_thr_lock)) {
			cv_signal(&arc_reclaim_thr_cv);
			mutex_exit(&arc_reclaim_thr_lock);
		}
	} else {
		uint64_t arc_p_max = arc_c - (arc_c >> arc_p_min_shift);
		atomic_add_64(&arc_p, size);
		if (arc_p > arc_p_max)
			arc_p = arc_c;
	}

	/* allocate a new block */
	if (meta) {
		data = zio_buf_alloc(size);
		arc_space_consume(size, ARC_SPACE_METADATA);
	} else {
		data = zio_data_buf_alloc(size);
		arc_space_consume(size, ARC_SPACE_DATA);
	}

	/*
	 * This block is now anonymous data
	 */
	(void) refcount_add_many(&arc_anon_size, size, (void*)(ulong_t)size);

	return (data);
}

/*
 * Update the last access time for the buffer (and possibly promote its state).
 * This routine is called whenever a buffer is accessed.
 */
static void
arc_access(arc_buf_t *buf)
{
	clock_t lbolt = ddi_get_lbolt();

	ASSERT(MUTEX_HELD(&buf->b_lock));

	if (buf->b_state == arc_mru) {
		/*
		 * This buffer has been accessed only "once" so far.
		 * Avoid false promotion from rapid successive access
		 * of the buffer.
		 */
		if (lbolt > buf->b_last_access + ARC_MINTIME) {
			if (buf->b_last_access) {
				/*
				 * This is the second significant access
				 * to this buffer, move it to the
				 * most frequently used state.
				 */
				DTRACE_PROBE1(new_state__mfu, arc_buf_t *, buf);
				arc_promote_buf(buf);
			}
			ARCSTAT_BUMP(arcstat_mru_hits);
			buf->b_last_access = lbolt;
		}
	} else if (buf->b_state == arc_mfu) {
		/*
		 * This buffer has been accessed more than once.
		 * Keep it in the MFU state.
		 */
		ARCSTAT_BUMP(arcstat_mfu_hits);
		buf->b_last_access = lbolt;
	} else {
		ASSERT(!"invalid arc state");
	}
	if (BUF_PREFETCHED(buf))
		ARC_FLAG_CLEAR(buf, PREFETCHED);
}

/*
 * This is used to let the ARC know that the caller is done
 * with an inactive reference, so the ARC can clean up.
 * Returns:
 *	EINPROGRESS	- if we are already in the process of evicting
 * 	0		- otherwise
 */
int
arc_evict_ref(arc_ref_t *ref)
{
	arc_buf_t *buf;

	rw_enter(&ref->r_lock, RW_WRITER);
	ASSERT(!REF_ANONYMOUS(ref));
	ASSERT(REF_INACTIVE(ref));
	buf = ref->r_buf;
	if (buf == NULL) {
		/*
		 * We are in arc_do_user_evicts().
		 */
		rw_exit(&ref->r_lock);
		return (EINPROGRESS);
	} else if (buf == &arc_eviction_head) {
		/*
		 * We are already on the eviction list,
		 * let arc_do_user_evicts() do the clean up.
		 */
		ref->r_efunc = NULL;
		rw_exit(&ref->r_lock);
		return (0);
	}
	mutex_enter(&buf->b_lock);

	/* the r_lock prevents our eviction while we obtain the b_lock */
	ASSERT(ref->r_buf == buf);
	/* sanity check */
	ASSERT(buf->b_state == arc_mru || buf->b_state == arc_mfu);

	arc_remove_ref(ref);
	if (!BUF_REFERENCED(buf) && mutex_tryenter(HASH_LOCK(buf))) {
		kmutex_t *hash_lock = HASH_LOCK(buf);
		if (!BUF_REFERENCED(buf)) {
			arc_remove_from_evictables(buf);
			arc_data_free(buf);
			mutex_exit(&buf->b_lock);
			arc_kill_buf(buf, B_TRUE);
		} else {
			mutex_exit(&buf->b_lock);
		}
		mutex_exit(hash_lock);
	} else {
		mutex_exit(&buf->b_lock);
	}
	rw_exit(&ref->r_lock);
	ref->r_efunc = NULL;
	ref->r_private = NULL;
	arc_destroy_ref(ref);
	return (0);
}

/*
 * Return a ref with anonymous data.
 *
 * Attempt to steal the data from this buffer by "killing" it
 * (transitioning it to the ghost state).
 * If this attempt fails because the buffer is currently "busy",
 * then make a new copy of the data.
 */
static boolean_t
arc_anonymize_ref(arc_ref_t *ref, boolean_t nocopy)
{
	arc_buf_t *buf = ref->r_buf;
	kmutex_t *hash_lock = HASH_LOCK(buf);
	boolean_t meta = BUF_METADATA(buf);

	ASSERT(RW_WRITE_HELD(&ref->r_lock));
	ASSERT(!REF_AUTONOMOUS(ref));

	if (BUF_IS_HOLE(buf)) {
		if (ref->r_data == buf->b_data) {
			if (nocopy)
				return (B_FALSE);
			ref->r_data = arc_get_data_block(ref->r_size, meta);
			bzero(ref->r_data, ref->r_size);
		}
		arc_rele(buf, ref);
		goto out;
	}
	mutex_enter(&buf->b_lock);
	ASSERT(BUF_HASHED(buf));
	mutex_enter(hash_lock);

	if (buf->b_inactive || refcount_count(&buf->b_active) > 2) {
		/* can't steal the data, make a private copy */
		mutex_exit(hash_lock);
		if (nocopy) {
			mutex_exit(&buf->b_lock);
			return (B_FALSE);
		}
		ref->r_data = arc_get_data_block(ref->r_size, meta);
		bcopy(buf->b_data, ref->r_data, ref->r_size);
		mutex_exit(&buf->b_lock);
		arc_rele(buf, ref);
		goto out;
	}

	if (buf->b_link)
		arc_remove_from_evictables(buf);

	ASSERT3U(buf->b_state->s_size, >=, buf->b_size);
	atomic_add_64(&buf->b_state->s_size, -buf->b_size);
	(void) refcount_add_many(&arc_anon_size,
	    buf->b_size, (void *)(ulong_t)buf->b_size);

	(void) refcount_remove(&buf->b_active, ref);
	buf->b_data = NULL;
	mutex_exit(&buf->b_lock);
	arc_kill_buf(buf, B_TRUE);
	mutex_exit(hash_lock);
out:
	if (meta)
		ref->r_buf = &arc_anon_metadata;
	else
		ref->r_buf = &arc_anon_data;
	ref->r_efunc = NULL;
	ref->r_private = NULL;
	return (B_TRUE);
}

/*
 * Make reference refer to writable data by removing it from
 * the buffer that it is currently associated with (only
 * anonymous data buffers may be modified).  It may be necessary
 * to make a new copy of the data for the reference.  Callers
 * must therefor check to see if r_data has changed.
 */
boolean_t
arc_try_make_writable(arc_ref_t *ref, boolean_t nocopy, boolean_t pending)
{
	arc_buf_t *buf;
	uint64_t size;

	rw_enter(&ref->r_lock, RW_WRITER);
	ASSERT(!REF_WRITING(ref));
	buf = ref->r_buf;
	size = ref->r_size;

	if (REF_AUTONOMOUS(ref)) {
		ASSERT(ref->r_data != NULL);
		if (!REF_ANONYMOUS(ref)) {
			if (BUF_METADATA(buf))
				ref->r_buf = &arc_anon_metadata;
			else
				ref->r_buf = &arc_anon_data;
			ref->r_efunc = 0;
			ref->r_private = NULL;
			arc_rele(buf, ref);
		}
		ASSERT(ref->r_efunc == NULL ||
		    ref->r_efunc == arc_write_pending);
		if (ref->r_efunc == arc_write_pending)
			size = 0;
		else if (pending)
			ref->r_efunc = arc_write_pending;
		rw_exit(&ref->r_lock);
		/* this buf may have been frozen since last use */
		arc_ref_thaw(ref);
	} else {
		mutex_enter(&buf->b_lock);
		/* XXX - should we just hash arc_buf0? */
		ASSERT(BUF_HASHED(buf) || BUF_IS_HOLE(buf));
		ASSERT(refcount_count(&buf->b_active) > 1);
		arc_cksum_verify(buf);
		mutex_exit(&buf->b_lock);

		if (!arc_anonymize_ref(ref, nocopy)) {
			ASSERT(nocopy);
			rw_exit(&ref->r_lock);
			return (B_FALSE);
		}
		if (pending)
			ref->r_efunc = arc_write_pending;
		rw_exit(&ref->r_lock);
	}
	/* we may already have been in the iopending state when called */
	if (pending && size > 0)
		(void) refcount_add_many(&arc_iopending_size, size,
		    (void*)(ulong_t)size);
	return (B_TRUE);
}

void
arc_make_writable(arc_ref_t *ref)
{
	VERIFY(arc_try_make_writable(ref, B_FALSE, B_TRUE));
}

/*
 * Return the size of the referenced arc buffer.
 */
int
arc_buf_size(arc_ref_t *ref)
{
	uint64_t size;

	rw_enter(&ref->r_lock, RW_READER);
	size = ref->r_size;
	rw_exit(&ref->r_lock);
	return (size);
}

/*
 * True if reference is in the anon state.
 */
int
arc_ref_anonymous(arc_ref_t *ref)
{
	int is_anonymous;

	rw_enter(&ref->r_lock, RW_READER);
	is_anonymous = REF_ANONYMOUS(ref);
	ASSERT(!is_anonymous || ref->r_data != NULL);
	rw_exit(&ref->r_lock);
	return (is_anonymous);
}

/*
 * True if reference is in the anon state, thawed, and not inactive.
 */
int
arc_ref_writable(arc_ref_t *ref)
{
	int writable;

	rw_enter(&ref->r_lock, RW_READER);
	writable = REF_ANONYMOUS(ref) && ref->r_private == NULL;
	rw_exit(&ref->r_lock);
	return (writable);
}

/*
 * True if reference is to a hole
 */
int
arc_ref_hole(arc_ref_t *ref)
{
	return (BUF_IS_HOLE(ref->r_buf));
}

#ifdef ZFS_DEBUG
/*
 * True if reference is active
 */
int
arc_ref_active(arc_ref_t *ref)
{
	int active;

	rw_enter(&ref->r_lock, RW_READER);
	active = ref->r_data != NULL;
	rw_exit(&ref->r_lock);
	return (active);
}

/*
 * True if reference is in a pending write state
 */
int
arc_ref_iopending(arc_ref_t *ref)
{
	int pending;

	rw_enter(&ref->r_lock, RW_READER);
	pending = ref->r_efunc == arc_write_pending;
	rw_exit(&ref->r_lock);
	return (pending);
}
#endif

/*
 * Associate this ref with a new arc buffer (same data, different id).
 */
void
arc_ref_new_id(arc_ref_t *ref, spa_t *spa, blkptr_t *bp)
{
	arc_buf_t *buf, *exists;

	ASSERT(!BP_IS_HOLE(bp));

	rw_enter(&ref->r_lock, RW_WRITER);
	if (!REF_AUTONOMOUS(ref))
		VERIFY(arc_anonymize_ref(ref, B_FALSE));
	else if (!REF_ANONYMOUS(ref))
		arc_rele(ref->r_buf, ref);

	buf = arc_buf_alloc(spa, bp, ref->r_data);

	mutex_enter(&buf->b_lock);
	exists = arc_hash_insert(buf, B_FALSE, FTAG);
	if (exists) {
		/* Dedup */
		ASSERT(BP_GET_DEDUP(bp));
		ASSERT(BP_GET_LEVEL(bp) == 0);
		buf->b_data = NULL;
		(void) refcount_add_many(&arc_anon_size,
		    ref->r_size, (void *)(ulong_t)ref->r_size);
		mutex_exit(&buf->b_lock);
		arc_destroy_buf(buf);
		buf = exists;
		mutex_enter(&buf->b_lock);
		ASSERT(arc_cksum_valid(buf->b_cksum, ref->r_data, ref->r_size));
	}
	arc_access(buf);

	ref->r_buf = buf;
	arc_hold(buf, ref);

	if ((zfs_flags & ZFS_DEBUG_MODIFY) && !BUF_CKSUM_EXISTS(buf))
		arc_cksum_set(buf, ref->r_data, bp);

	mutex_exit(&buf->b_lock);
	rw_exit(&ref->r_lock);
	arc_rele(buf, FTAG);
}

/* generic arc_done_func_t to obtain a copy of buffer data */
/* ARGSUSED */
void
arc_bcopy_func(zio_t *zio, arc_ref_t *ref, void *arg)
{
	bcopy(ref->r_data, arg, ref->r_buf->b_size);
	arc_free_ref(ref);
}

/* generic arc_done_func_t to obtain a reference to a buffer */
void
arc_getref_func(zio_t *zio, arc_ref_t *ref, void *arg)
{
	arc_ref_t **refp = arg;
	if (zio && zio->io_error) {
		arc_free_ref(ref);
		*refp = NULL;
	} else {
		*refp = ref;
	}
}

/*
 * Read completion callback.
 */
static void
arc_read_done(zio_t *zio)
{
	arc_read_callback_t *cb = zio->io_private;
	boolean_t meta = BP_IS_METADATA(zio->io_bp);
	boolean_t prefetch = cb->cb_done == NULL;
	uint64_t size = zio->io_size;
	arc_buf_t *buf;
	arc_ref_t *ref;

	if (zio->io_error) {
		if (prefetch) {
			/* failed prefetch, clean up and exit */
			arc_free_data_block(zio->io_data, size, meta);
			goto out;
		}
		/* try to recover by looking for this block in the cache */
		buf = arc_hash_find(zio->io_spa, zio->io_bp, FTAG, NULL);
		if (buf) {
			/* found block  in cache, clear the error */
			mutex_enter(&buf->b_lock);
			zio->io_error = 0;
			arc_free_data_block(zio->io_data, size, meta);
			zio->io_data = buf->b_data;
		} else {
			/* read failed, return an anonymous ref... */
			ref = arc_alloc_ref(size, meta, zio->io_data);
			goto out;
		}
	} else {
		arc_buf_t *exists;

		if (BP_SHOULD_BYTESWAP(zio->io_bp)) {
			arc_byteswap_func_t *func =
			    BP_GET_LEVEL(zio->io_bp) > 0 ?
			    byteswap_uint64_array :
			    dmu_ot[BP_GET_TYPE(zio->io_bp)].ot_byteswap;
			func(zio->io_data, size);
		}

		buf = arc_buf_alloc(zio->io_spa, zio->io_bp, zio->io_data);
		if (prefetch)
			ARC_FLAG_SET(buf, PREFETCHED);

		mutex_enter(&buf->b_lock);
		exists = arc_hash_insert(buf, B_TRUE, FTAG);
		if (exists) {
			/*
			 * We lost a race with another reader.
			 */
			arc_data_free(buf);
			mutex_exit(&buf->b_lock);
			arc_destroy_buf(buf);
			buf = exists;
			mutex_enter(&buf->b_lock);
		}
	}

	if (zfs_flags & ZFS_DEBUG_MODIFY)
		arc_cksum_set(buf, buf->b_data, zio->io_bp);
	if (!prefetch) {
		ref = arc_add_ref(buf);
		arc_access(buf);
	}

	mutex_exit(&buf->b_lock);
	arc_rele(buf, FTAG);
out:
	/* execute callback and free its structure */
	if (cb->cb_done)
		cb->cb_done(zio, ref, cb->cb_private);
	kmem_free(cb, sizeof (arc_read_callback_t));
}

/*
 * Return a copy of a block pointer.
 */
void
arc_get_blkptr(arc_ref_t *ref, int offset, blkptr_t *bp)
{
	rw_enter(&ref->r_lock,  RW_READER);
	ASSERT(!refcount_is_zero(&ref->r_buf->b_active));
	ASSERT3U(offset, <, ref->r_size);
	*bp = *(blkptr_t *)((char *)ref->r_data + offset);
	rw_exit(&ref->r_lock);
}

/*
 * Look for the requested data in the cache. If not found, issue a
 * read (and cache the results).  If a pio is supplied we will nowait
 * the read, otherwise the read will be synchronous.
 */
int
arc_read(zio_t *pio, spa_t *spa, const blkptr_t *bp, uint64_t size,
    arc_done_func_t *done, void *private, int priority, enum zio_flag  flags,
    arc_options_t options, const zbookmark_t *zb)
{
	arc_buf_t *buf;
	boolean_t meta = (options & ARC_OPT_METADATA) != 0;
	boolean_t prefetch = (done == NULL);
	void *l2cookie;

	if (BP_IS_HOLE(bp)) {
		if (!prefetch) {
			arc_ref_t *ref;

			if (meta)
				buf = &arc_metadata_buf0;
			else
				buf = &arc_data_buf0;
			ref = arc_add_ref(buf);
			ref->r_size = size;
			done(NULL, ref, private);
		}

		ARCSTAT_BUMP(arcstat_hits);
		ARCSTAT_CONDSTAT(prefetch, prefetch, demand,
		    meta, metadata, data, hits);

		if (options & ARC_OPT_REPORTCACHED)
			return (EEXIST);
		return (0);
	}
	/* bp overrides arguments */
	size = BP_GET_LSIZE(bp);
	meta = BP_IS_METADATA(bp);

	buf = arc_hash_find(spa, bp, FTAG, &l2cookie);
	if (buf) {
		arc_ref_t *ref;

		mutex_enter(&buf->b_lock);

		ASSERT(buf->b_state == arc_mru || buf->b_state == arc_mfu);
		ASSERT(buf->b_size == size);
		ASSERT(meta == BUF_METADATA(buf));
		ASSERT(buf->b_data);

		if (!prefetch) {
			ref = arc_add_ref(buf);
			arc_access(buf);
		} else {
			ARC_FLAG_SET(buf, PREFETCHED);
		}
		if (options & ARC_OPT_NOL2CACHE)
			ARC_FLAG_SET(buf, DONT_L2CACHE);

		DTRACE_PROBE1(arc__hit, arc_buf_t *, buf);

		mutex_exit(&buf->b_lock);

		ARCSTAT_BUMP(arcstat_hits);
		ARCSTAT_CONDSTAT(BUF_PREFETCHED(buf), prefetch, demand,
		    BUF_METADATA(buf), metadata, data, hits);

		if (!prefetch)
			done(NULL, ref, private);
		arc_rele(buf, FTAG);

		if (options & ARC_OPT_REPORTCACHED)
			return (EEXIST);
	} else {
		arc_read_callback_t *cb;
		void *data;
		zio_t *zio;

		DTRACE_PROBE2(arc__miss, blkptr_t *, bp, zbookmark_t *, zb);
		ARCSTAT_BUMP(arcstat_misses);
		ARCSTAT_CONDSTAT(prefetch, prefetch, demand,
		    meta, metadata, data, misses);

		cb = kmem_zalloc(sizeof (arc_read_callback_t), KM_SLEEP);
		cb->cb_done = done;
		cb->cb_private = private;

		data = arc_get_data_block(size, meta);

		if (l2cookie) {
			if (l2arc_read(pio, spa, bp, data,
			    l2cookie, cb, priority, flags, zb) == 0)
				return (0);
		} else {
			DTRACE_PROBE1(l2arc__miss, blkptr_t *, bp);
			ARCSTAT_BUMP(arcstat_l2_misses);
		}

		zio = zio_read(pio, spa, bp, data, size,
		    arc_read_done, cb, priority, flags, zb);

		if (pio)
			zio_nowait(zio);
		else
			return (zio_wait(zio));
	}
	return (0);
}

/*
 * Write done callback.  Now that the buffer has an identity,
 * insert it into the hash table.
 */
static void
arc_write_done(zio_t *zio)
{
	arc_write_callback_t *callback = zio->io_private;
	arc_ref_t *ref = callback->cb_ref;
	uint64_t size = ref->r_size;

	ASSERT(ref->r_data == zio->io_data);
	ASSERT(REF_ANONYMOUS(ref) && REF_WRITING(ref));
	ref->r_efunc = NULL;

	if (zio->io_error == 0 && BP_PHYSICAL_BIRTH(zio->io_bp)) {
		arc_buf_t *exists;
		arc_buf_t *buf;

		buf = arc_buf_alloc(zio->io_spa, zio->io_bp, zio->io_data);
		if (callback->cb_flags & ARC_FLAG_DONT_L2CACHE)
			ARC_FLAG_SET(buf, DONT_L2CACHE);
		if (ref->r_private != arc_ref_frozen_flag)
			buf->b_cksum = ref->r_private;

		rw_enter(&ref->r_lock, RW_WRITER);
		mutex_enter(&buf->b_lock);
		exists = arc_hash_insert(buf, B_FALSE, FTAG);
		if (exists) {
			/*
			 * This is either an overwrite for
			 * sync-to-convergence, a dedup, or a prefetch race.
			 */
			if (zio->io_flags & ZIO_FLAG_IO_REWRITE) {
				kmutex_t *hash_lock = HASH_LOCK(exists);

				if (!BP_EQUAL(&zio->io_bp_orig, zio->io_bp))
					panic("bad overwrite, buf=%p exists=%p",
					    (void *)buf, (void *)exists);
				mutex_enter(hash_lock);
				ASSERT(exists->b_inactive == NULL);
				arc_hold(buf, FTAG);
				arc_hash_replace(exists, &buf->b_id);
				mutex_exit(hash_lock);
				arc_rele(exists, FTAG);
				exists = NULL;
			} else {
				/* Dedup or prefetch race */
				ASSERT(BP_GET_DEDUP(zio->io_bp) ||
				    BUF_PREFETCHED(exists));
				ASSERT(BP_GET_LEVEL(zio->io_bp) == 0);
				buf->b_data = NULL;
				(void) refcount_add_many(&arc_anon_size,
				    ref->r_size, (void *)(ulong_t)ref->r_size);
				mutex_exit(&buf->b_lock);
				arc_destroy_buf(buf);
				buf = exists;
				mutex_enter(&buf->b_lock);
			}
		}

		/*
		 * Associate this ref with the buf.
		 */
		ref->r_private = NULL;
		ref->r_buf = buf;
		arc_hold(buf, ref);
		arc_access(buf);

		if ((zfs_flags & ZFS_DEBUG_MODIFY) && !BUF_CKSUM_EXISTS(buf))
			arc_cksum_set(buf, zio->io_data, zio->io_bp);
		else
			arc_cksum_verify(buf);

		mutex_exit(&buf->b_lock);
		rw_exit(&ref->r_lock);
		arc_rele(buf, FTAG);
	} else {
		rw_enter(&ref->r_lock, RW_WRITER);
		ASSERT(ref->r_data == zio->io_data);
		if (zio->io_error == 0) {
			/*
			 * This block was compressed away,
			 * reparent the ref to buf0.
			 */
			ASSERT(BP_PHYSICAL_BIRTH(zio->io_bp) == 0);
			if (BUF_METADATA(ref->r_buf))
				ref->r_buf = &arc_metadata_buf0;
			else
				ref->r_buf = &arc_data_buf0;
			arc_hold(ref->r_buf, ref);
		}
		if (ref->r_private && ref->r_private != arc_ref_frozen_flag)
			kmem_free(ref->r_private, sizeof (arc_cksum_t));
		ref->r_private = NULL;
		rw_exit(&ref->r_lock);
	}
	zio->io_private = callback->cb_private;
	callback->cb_done(zio);

	kmem_free(callback, sizeof (arc_write_callback_t));
	(void) refcount_remove_many(&arc_writing_size, size,
	    (void*)(ulong_t)size);
}

/*
 * Write ready callback.
 */
static void
arc_write_ready(zio_t *zio)
{
	arc_write_callback_t *callback = zio->io_private;

	/*
	 * It is the responsibility of the callback to handle the
	 * accounting for any re-write attempt.
	 */
	zio->io_private = callback->cb_private;
	callback->cb_ready(zio);
	zio->io_private = callback;
}

/*
 * Write the requested data to the pool, intercept the done callback
 * so that we can add the new non-anonymous arc buffer to the hash table.
 */
zio_t *
arc_write(zio_t *pio, spa_t *spa, uint64_t txg,
    blkptr_t *bp, arc_ref_t *ref, boolean_t nol2arc, const zio_prop_t *zp,
    zio_done_func_t *ready, zio_done_func_t *done, void *private,
    int priority, enum zio_flag flags, const zbookmark_t *zb)
{
	arc_write_callback_t *callback;
	uint64_t size;
	zio_t *zio;

	ASSERT(ready != NULL);
	ASSERT(done != NULL);
	ASSERT(REF_ANONYMOUS(ref));
	callback = kmem_zalloc(sizeof (arc_write_callback_t), KM_SLEEP);
	callback->cb_ready = ready;
	callback->cb_done = done;
	callback->cb_private = private;
	callback->cb_ref = ref;
	if (nol2arc)
		callback->cb_flags |=  ARC_FLAG_DONT_L2CACHE;
	rw_enter(&ref->r_lock, RW_WRITER);
	size = ref->r_size;
	ASSERT(REF_WRITE_PENDING(ref));
	(void) refcount_remove_many(&arc_iopending_size, size,
	    (void*)(ulong_t)size);
	(void) refcount_add_many(&arc_writing_size, size, (void*)(ulong_t)size);
	ref->r_efunc = arc_write_in_progress;
	rw_exit(&ref->r_lock);
	zio = zio_write(pio, spa, txg, bp, ref->r_data, ref->r_size, zp,
	    arc_write_ready, arc_write_done, callback, priority, flags, zb);

	return (zio);
}

/*
 * Throttle IOs when we start to use too much memory.
 */
static int
arc_memory_throttle(uint64_t reserve, uint64_t inflight_data, uint64_t txg)
{
#ifdef _KERNEL
	uint64_t available_memory = ptob(freemem);
	static uint64_t page_load = 0;
	static uint64_t last_txg = 0;

#if defined(__i386)
	available_memory =
	    MIN(available_memory, vmem_size(heap_arena, VMEM_FREE));
#endif
	if (available_memory >= zfs_write_limit_max)
		return (0);

	if (txg > last_txg) {
		last_txg = txg;
		page_load = 0;
	}
	/*
	 * If we are in pageout, we know that memory is already tight,
	 * the arc is already going to be evicting, so we just want to
	 * continue to let page writes occur as quickly as possible.
	 */
	if (curproc == proc_pageout) {
		if (page_load > MAX(ptob(minfree), available_memory) / 4)
			return (ERESTART);
		/* Note: reserve is inflated, so we deflate */
		page_load += reserve / 8;
		return (0);
	} else if (page_load > 0 && arc_reclaim_needed()) {
		/* memory is low, delay before restarting */
		ARCSTAT_INCR(arcstat_memory_throttle_count, 1);
		return (EAGAIN);
	}
	page_load = 0;

	if (arc_size > arc_c_min) {
		uint64_t evictable_memory;

		evictable_memory = arc_evictable_memory(arc_mru) +
		    arc_evictable_memory(arc_mfu);
		available_memory += MIN(evictable_memory, arc_size - arc_c_min);
	}

	if (inflight_data > available_memory / 4) {
		ARCSTAT_INCR(arcstat_memory_throttle_count, 1);
		return (ERESTART);
	}
#endif
	return (0);
}

/*
 * Clear a temporary reservation.
 */
void
arc_tempreserve_clear(uint64_t reserve)
{
	atomic_add_64(&arc_tempreserve, -reserve);
	ASSERT((int64_t)arc_tempreserve >= 0);
}

/*
 * Place a temporary reservation for arc space.  This makes sure that
 * there will be enough memory/space-in-the-cache to accomodate a new
 * write request.
 */
int
arc_tempreserve_space(uint64_t reserve, uint64_t txg)
{
	uint64_t io_size = refcount_count(&arc_iopending_size) +
	    refcount_count(&arc_writing_size);
	int error;

#ifdef ZFS_DEBUG
	/*
	 * Once in a while, fail for no reason.  Everything should cope.
	 */
	if (spa_get_random(10000) == 0) {
		dprintf("forcing random failure\n");
		return (ERESTART);
	}
#endif
	if (reserve > arc_c/4 && !arc_no_grow)
		arc_c = MIN(arc_c_max, reserve * 4);
	if (reserve > arc_c)
		return (ENOMEM);

	/*
	 * Writes will, almost always, require additional memory allocations
	 * in order to compress/encrypt/etc the data.  We therefor need to
	 * make sure that there is sufficient available memory for this.
	 */
	if (error = arc_memory_throttle(reserve, io_size, txg))
		return (error);

	/*
	 * Throttle writes when the amount of dirty data in the cache
	 * gets too large.  We try to keep the cache less than half full
	 * of dirty blocks so that our sync times don't grow too large.
	 * Note: if two requests come in concurrently, we might let them
	 * both succeed, when one of them should fail.  Not a huge deal.
	 */

	if (reserve + arc_tempreserve + io_size > arc_c / 2 &&
	    io_size > arc_c / 4) {
		dprintf("failing, arc_tempreserve=%lluK "
		    "io_data=%lluK tempreserve=%lluK arc_c=%lluK\n",
		    arc_tempreserve>>10, io_size>>10,
		    reserve>>10, arc_c>>10);
		return (ERESTART);
	}
	atomic_add_64(&arc_tempreserve, reserve);
	return (0);
}

/*
 * Shrink the size of the arc.
 */
void
arc_shrink(void)
{
	if (arc_c > arc_c_min) {
		uint64_t to_free;

#ifdef _KERNEL
		to_free = MAX(arc_c >> arc_shrink_shift, ptob(needfree));
#else
		to_free = arc_c >> arc_shrink_shift;
#endif
		if (arc_c > arc_c_min + to_free)
			atomic_add_64(&arc_c, -to_free);
		else
			arc_c = arc_c_min;

		atomic_add_64(&arc_p, -(arc_p >> arc_shrink_shift));
		if (arc_c > arc_size)
			arc_c = MAX(arc_size, arc_c_min);
		if (arc_p > arc_c)
			arc_p = (arc_c >> 1);
		ASSERT(arc_c >= arc_c_min);
		ASSERT((int64_t)arc_p >= 0);
	}
}

/*
 * Reap the zio and arc kmem caches to free up memory.
 */
static void
arc_kmem_reap_now(arc_reclaim_strategy_t strat)
{
	size_t			i;
	kmem_cache_t		*prev_cache = NULL;
	kmem_cache_t		*prev_data_cache = NULL;
	extern kmem_cache_t	*zio_buf_cache[];
	extern kmem_cache_t	*zio_data_buf_cache[];

#ifdef _KERNEL
	if (arc_meta_limit && arc_meta_used >= arc_meta_limit) {
		/*
		 * We are exceeding our meta-data cache limit.
		 * Purge some DNLC entries to release holds on meta-data.
		 */
		dnlc_reduce_cache((void *)(uintptr_t)arc_reduce_dnlc_percent);
	}
#if defined(__i386)
	/*
	 * Reclaim unused memory from all kmem caches.
	 */
	kmem_reap();
#endif
#endif

	/*
	 * An aggressive reclamation will shrink the cache size as well as
	 * reap free buffers from the arc kmem caches.
	 */
	if (strat == ARC_RECLAIM_AGGR && arc_c > (arc_size - (1<<20)))
		arc_shrink();

	for (i = 0; i < SPA_MAXBLOCKSIZE >> SPA_MINBLOCKSHIFT; i++) {
		if (zio_buf_cache[i] != prev_cache) {
			prev_cache = zio_buf_cache[i];
			kmem_cache_reap_now(zio_buf_cache[i]);
		}
		if (zio_data_buf_cache[i] != prev_data_cache) {
			prev_data_cache = zio_data_buf_cache[i];
			kmem_cache_reap_now(zio_data_buf_cache[i]);
		}
	}
	kmem_cache_reap_now(ref_cache);
	kmem_cache_reap_now(ghost_cache);
}

static void
arc_trim_refs(arc_ref_t *marker)
{
	arc_buf_t *buf = marker->r_buf;
	arc_ref_t **last_refp, **refp;

	mutex_enter(&buf->b_lock);
	last_refp = &buf->b_inactive;
	while (*last_refp && *last_refp != marker)
		last_refp = &(*last_refp)->r_next;
	refp = &marker->r_next;
	while (*refp) {
		arc_ref_t *ref = *refp;

		if (!rw_tryenter(&ref->r_lock, RW_WRITER)) {
			refp = &ref->r_next;
			continue;
		}
		*refp = ref->r_next;
		/* XXX - use arc_remove_ref()? */
		ref->r_next = NULL;
		ref->r_buf = NULL;
		arc_queue_ref(ref);
		rw_exit(&ref->r_lock);
	}
	*last_refp = marker->r_next;
	marker->r_next = buf->b_inactive;
	buf->b_inactive = marker;
	mutex_exit(&buf->b_lock);
}

/*
 * Try to keep the arcs memory usage under control.
 */
static void
arc_reclaim_thread(void)
{
	clock_t			lbolt, trimtime = 0, growtime = 0;
	arc_reclaim_strategy_t	last_reclaim = ARC_RECLAIM_CONS;
	callb_cpr_t		cpr;
	arc_ref_t		metadata_ref0 = { 0 }, data_ref0 = { 0 };

	CALLB_CPR_INIT(&cpr, &arc_reclaim_thr_lock, callb_generic_cpr, FTAG);

	metadata_ref0.r_buf = &arc_metadata_buf0;
	data_ref0.r_buf = &arc_data_buf0;
	mutex_enter(&arc_reclaim_thr_lock);
	while (arc_thread_exit == 0) {
		lbolt = ddi_get_lbolt();

		if (arc_reclaim_needed()) {

			if (arc_no_grow) {
				if (last_reclaim == ARC_RECLAIM_CONS) {
					last_reclaim = ARC_RECLAIM_AGGR;
				} else {
					last_reclaim = ARC_RECLAIM_CONS;
				}
			} else {
				arc_no_grow = TRUE;
				last_reclaim = ARC_RECLAIM_AGGR;
				membar_producer();
			}

			/* reset the growth delay for every reclaim */
			growtime = lbolt + (arc_grow_retry * hz);

			arc_kmem_reap_now(last_reclaim);
			arc_warm = B_TRUE;

		} else if (arc_no_grow && lbolt >= growtime) {
			arc_no_grow = FALSE;
		}

		arc_adjust();

		if (arc_eviction_head.b_inactive != NULL)
			arc_do_user_evicts();

		/* Keep buf0 inactive ref lists under control */
		if (lbolt > trimtime) {
			trimtime = lbolt + (arc_trim_retry * hz);

			arc_trim_refs(&metadata_ref0);
			arc_trim_refs(&data_ref0);
		}

		/* block until needed, or one second, whichever is shorter */
		CALLB_CPR_SAFE_BEGIN(&cpr);
		(void) cv_timedwait(&arc_reclaim_thr_cv,
		    &arc_reclaim_thr_lock, (ddi_get_lbolt() + hz));
		CALLB_CPR_SAFE_END(&cpr, &arc_reclaim_thr_lock);
	}

	arc_thread_exit = 0;
	cv_broadcast(&arc_reclaim_thr_cv);
	CALLB_CPR_EXIT(&cpr);		/* drops arc_reclaim_thr_lock */
	thread_exit();
}

/*
 * Initialize an arc state.
 */
static void
state_init(arc_state_t *state)
{
	arc_evict_list_t *list = &state->s_evictable;

	list->l_generation = 0;
	mutex_init(&list->l_link_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&list->l_update_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&list->l_evict_lock, NULL, MUTEX_DEFAULT, NULL);
	if (GHOST_STATE(state)) {
		list_create(&list->l_insert,
		    sizeof (arc_ghost_t), offsetof(arc_ghost_t, g_link));
		list_create(&list->l_remove,
		    sizeof (arc_ghost_t), offsetof(arc_ghost_t, g_link));
	} else {
		list_create(&list->l_insert,
		    sizeof (arc_elink_t), offsetof(arc_elink_t, e_link));
		list_create(&list->l_remove,
		    sizeof (arc_elink_t), offsetof(arc_elink_t, e_link));
	}
}

/*
 * Initialize the arc.
 */
void
arc_init(void)
{
	uint64_t availmem = physmem * PAGESIZE;

	refcount_create(&arc_anon_size);
	refcount_create(&arc_writing_size);
	refcount_create(&arc_iopending_size);

	mutex_init(&arc_reclaim_thr_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&arc_reclaim_thr_cv, NULL, CV_DEFAULT, NULL);

#ifdef _KERNEL
	/*
	 * On architectures where the physical memory can be larger
	 * than the addressable space (intel in 32-bit mode), we may
	 * need to limit the cache to the VM size.
	 */
	availmem = MIN(availmem, vmem_size(heap_arena, VMEM_ALLOC | VMEM_FREE));
#endif

	/*
	 * Unless overridden with zfs_arc_min/max, the default arc
	 * [min, max] range is:
	 *   [64MB, MAX(3/4 * physmem, physmem - 1GB)]
	 * Note that the arc min/max is clamped at: [64MB, physmem]
	 */
	if (zfs_arc_max)
		arc_c_max = MIN(MAX(zfs_arc_max, 64<<20), availmem);
	else if (availmem < (4ULL<<30))
		arc_c_max = availmem / 4 * 3;
	else
		arc_c_max = availmem - (1<<30);
	if (zfs_arc_min)
		arc_c_min = MIN(MAX(zfs_arc_min, 64<<20), arc_c_max);
	else
		arc_c_min = MIN(arc_c_max, 64<<20);

	arc_c = arc_c_max;
	arc_p = (arc_c >> 1);

	arc_meta_limit = zfs_arc_meta_limit;

	if (zfs_arc_grow_retry > 0)
		arc_grow_retry = zfs_arc_grow_retry;

	if (zfs_arc_shrink_shift > 0)
		arc_shrink_shift = zfs_arc_shrink_shift;

	if (zfs_arc_p_min_shift > 0)
		arc_p_min_shift = zfs_arc_p_min_shift;

	/* if kmem_flags are set, lets try to use less memory */
	if (kmem_debugging())
		arc_c = arc_c / 2;
	if (arc_c < arc_c_min)
		arc_c = arc_c_min;

	arc_mru = &ARC_mru;
	arc_mru_ghost = &ARC_mru_ghost;
	arc_mfu = &ARC_mfu;
	arc_mfu_ghost = &ARC_mfu_ghost;
	arc_size = 0;

	bzero(&arc_anon_data, sizeof (arc_buf_t));
	bzero(&arc_anon_metadata, sizeof (arc_buf_t));

	arc_hash_init();

	buf_cache = kmem_cache_create("arc_buf_t", sizeof (arc_buf_t),
	    0, buf_cons, buf_dest, buf_recl, NULL, NULL, 0);
	ref_cache = kmem_cache_create("arc_ref_t", sizeof (arc_ref_t),
	    0, ref_cons, ref_dest, NULL, NULL, NULL, 0);
	ghost_cache = kmem_cache_create("arc_ghost_t", sizeof (arc_ghost_t),
	    0, ghost_cons, ghost_dest, NULL, NULL, NULL, 0);

	state_init(arc_mru);
	state_init(arc_mru_ghost);
	state_init(arc_mfu);
	state_init(arc_mfu_ghost);

	arc_thread_exit = 0;
	mutex_init(&arc_evict_interlock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&arc_eviction_mtx, NULL, MUTEX_DEFAULT, NULL);
	bzero(&arc_eviction_head, sizeof (arc_buf_t));

	arc_ksp = kstat_create("zfs", 0, "arcstats", "misc", KSTAT_TYPE_NAMED,
	    sizeof (arc_stats) / sizeof (kstat_named_t), KSTAT_FLAG_VIRTUAL);

	if (arc_ksp != NULL) {
		arc_ksp->ks_data = &arc_stats;
		kstat_install(arc_ksp);
	}

	arc_dead = B_FALSE;
	arc_warm = B_FALSE;

	if (zfs_write_limit_max == 0)
		zfs_write_limit_max = ptob(physmem) >> zfs_write_limit_shift;
	else
		zfs_write_limit_shift = 0;
	mutex_init(&zfs_write_limit_lock, NULL, MUTEX_DEFAULT, NULL);

	/* Initialize special-purpose arc buffers */
	(void) buf_cons(&arc_anon_data, NULL, 0);
	arc_hold(&arc_anon_data, NULL);

	(void) buf_cons(&arc_anon_metadata, NULL, 0);
	ARC_FLAG_SET(&arc_anon_metadata, METADATA);
	arc_hold(&arc_anon_metadata, NULL);

	(void) buf_cons(&arc_data_buf0, NULL, 0);
	arc_data_buf0.b_state = arc_mfu;
	arc_data_buf0.b_size = SPA_MAXBLOCKSIZE;
	arc_data_buf0.b_data = kmem_zalloc(SPA_MAXBLOCKSIZE, KM_SLEEP);
	mutex_enter(&arc_data_buf0.b_lock);
	arc_buf_cksum_compute(&arc_data_buf0);
	mutex_exit(&arc_data_buf0.b_lock);
	arc_hold(&arc_data_buf0, NULL);

	(void) buf_cons(&arc_metadata_buf0, NULL, 0);
	arc_metadata_buf0.b_state = arc_mfu;
	arc_metadata_buf0.b_size = SPA_MAXBLOCKSIZE;
	arc_metadata_buf0.b_data = arc_data_buf0.b_data;
	arc_metadata_buf0.b_cksum = arc_data_buf0.b_cksum;
	ARC_FLAG_SET(&arc_metadata_buf0, METADATA);
	arc_hold(&arc_metadata_buf0, NULL);

	(void) thread_create(NULL, 0, arc_reclaim_thread, NULL, 0, &p0,
	    TS_RUN, minclsyspri);
}

/*
 * Finalize an arc state.
 */
static void
state_fini(arc_state_t *state)
{
	arc_evict_list_t *list = &state->s_evictable;

	mutex_destroy(&list->l_link_lock);
	mutex_destroy(&list->l_update_lock);
	list_destroy(&list->l_insert);
	mutex_destroy(&list->l_evict_lock);
	list_destroy(&list->l_remove);
}

/*
 * Finalize the arc.
 */
void
arc_fini(void)
{
	mutex_enter(&arc_reclaim_thr_lock);
	arc_thread_exit = 1;
	while (arc_thread_exit != 0)
		cv_wait(&arc_reclaim_thr_cv, &arc_reclaim_thr_lock);
	mutex_exit(&arc_reclaim_thr_lock);

	arc_flush(NULL);

	arc_dead = B_TRUE;

	if (arc_ksp != NULL) {
		kstat_delete(arc_ksp);
		arc_ksp = NULL;
	}

	(void) refcount_remove(&arc_anon_data.b_active, NULL);
	buf_dest(&arc_anon_data, NULL);
	(void) refcount_remove(&arc_anon_metadata.b_active, NULL);
	buf_dest(&arc_anon_metadata, NULL);
	(void) refcount_remove(&arc_data_buf0.b_active, NULL);
	kmem_free(arc_data_buf0.b_cksum, sizeof (arc_cksum_t));
	buf_dest(&arc_data_buf0, NULL);
	(void) refcount_remove(&arc_metadata_buf0.b_active, NULL);
	buf_dest(&arc_metadata_buf0, NULL);

	mutex_destroy(&arc_evict_interlock);
	mutex_destroy(&arc_eviction_mtx);
	mutex_destroy(&arc_reclaim_thr_lock);
	cv_destroy(&arc_reclaim_thr_cv);

	state_fini(arc_mru);
	state_fini(arc_mru_ghost);
	state_fini(arc_mfu);
	state_fini(arc_mfu_ghost);

	mutex_destroy(&zfs_write_limit_lock);

	arc_hash_fini();

	ASSERT(arc_loaned_bytes == 0);

	refcount_destroy(&arc_iopending_size);
	refcount_destroy(&arc_writing_size);
	refcount_destroy(&arc_anon_size);

	kmem_cache_destroy(ghost_cache);
	kmem_cache_destroy(buf_cache);
	kmem_cache_destroy(ref_cache);
}

/*
 * Level 2 ARC
 *
 * The level 2 ARC (L2ARC) is a cache layer in-between main memory and disk.
 * It uses dedicated storage devices to hold cached data, which are populated
 * using large infrequent writes.  The main role of this cache is to boost
 * the performance of random read workloads.  The intended L2ARC devices
 * include short-stroked disks, solid state disks, and other media with
 * substantially faster read latency than disk.
 *
 *                 +-----------------------+
 *                 |         ARC           |
 *                 +-----------------------+
 *                    |         ^     ^
 *                    |         |     |
 *      l2arc_feed_thread()    arc_read()
 *                    |         |     |
 *                    |  l2arc read   |
 *                    V         |     |
 *               +---------------+    |
 *               |     L2ARC     |    |
 *               +---------------+    |
 *                   |    ^           |
 *          l2arc_write() |           |
 *                   |    |           |
 *                   V    |           |
 *                 +-------+      +-------+
 *                 | vdev  |      | vdev  |
 *                 | cache |      | cache |
 *                 +-------+      +-------+
 *                 +=========+     .-----.
 *                 :  L2ARC  :    |-_____-|
 *                 : devices :    | Disks |
 *                 +=========+    `-_____-'
 *
 * Read requests are satisfied from the following sources, in order:
 *
 *	1) ARC
 *	2) vdev cache of L2ARC devices
 *	3) L2ARC devices
 *	4) vdev cache of disks
 *	5) disks
 *
 * Some L2ARC device types exhibit extremely slow write performance.
 * To accommodate for this there are some significant differences between
 * the L2ARC and traditional cache design:
 *
 * 1. There is no eviction path from the ARC to the L2ARC.  Evictions from
 * the ARC behave as usual, freeing buffers and placing headers on ghost
 * lists.  The ARC does not send buffers to the L2ARC during eviction as
 * this would add inflated write latencies for all ARC memory pressure.
 *
 * 2. The L2ARC attempts to cache data from the ARC before it is evicted.
 * It does this by periodically scanning buffers from the eviction-end of
 * the MFU and MRU ARC lists, copying them to the L2ARC devices if they are
 * not already there.  It scans until a headroom of buffers is satisfied,
 * which itself is a buffer for ARC eviction.  The thread that does this is
 * l2arc_feed_thread(), illustrated below; example sizes are included to
 * provide a better sense of ratio than this diagram:
 *
 *	       head -->                        tail
 *	        +---------------------+----------+
 *	ARC_mfu |:::::#:::::::::::::::|o#o###o###|-->.   # already on L2ARC
 *	        +---------------------+----------+   |   o L2ARC eligible
 *	ARC_mru |:#:::::::::::::::::::|#o#ooo####|-->|   : ARC buffer
 *	        +---------------------+----------+   |
 *	             15.9 Gbytes      ^ 32 Mbytes    |
 *	                           headroom          |
 *	                                      l2arc_feed_thread()
 *	                                             |
 *	                 l2arc write hand <--[oooo]--'
 *	                         |           8 Mbyte
 *	                         |          write max
 *	                         V
 *		  +==============================+
 *	L2ARC dev |####|#|###|###|    |####| ... |
 *	          +==============================+
 *	                     32 Gbytes
 *
 * 3. If an ARC buffer is copied to the L2ARC but then hit instead of
 * evicted, then the L2ARC has cached a buffer much sooner than it probably
 * needed to, potentially wasting L2ARC device bandwidth and storage.  It is
 * safe to say that this is an uncommon case, since buffers at the end of
 * the ARC lists have moved there due to inactivity.
 *
 * 4. If the ARC evicts faster than the L2ARC can maintain a headroom,
 * then the L2ARC simply misses copying some buffers.  This serves as a
 * pressure valve to prevent heavy read workloads from both stalling the ARC
 * with waits and clogging the L2ARC with writes.  This also helps prevent
 * the potential for the L2ARC to churn if it attempts to cache content too
 * quickly, such as during backups of the entire pool.
 *
 * 5. After system boot and before the ARC has filled main memory, there are
 * no evictions from the ARC and so the tails of the ARC_mfu and ARC_mru
 * lists can remain mostly static.  Instead of searching from tail of these
 * lists as pictured, the l2arc_feed_thread() will search from the list heads
 * for eligible buffers, greatly increasing its chance of finding them.
 *
 * The L2ARC device write speed is also boosted during this time so that
 * the L2ARC warms up faster.  Since there have been no ARC evictions yet,
 * there are no L2ARC reads, and no fear of degrading read performance
 * through increased writes.
 *
 * 6. Writes to the L2ARC devices are grouped and sent in-sequence, so that
 * the vdev queue can aggregate them into larger and fewer writes.  Each
 * device is written to in a rotor fashion, sweeping writes through
 * available space then repeating.
 *
 * 7. The L2ARC does not store dirty content.  It never needs to flush
 * write buffers back to disk based storage.
 *
 * 8. If an ARC buffer which exists in the L2ARC is freed, the now stale
 * L2ARC buffer is immediately dropped.
 *
 * The performance of the L2ARC can be tweaked by a number of tunables, which
 * may be necessary for different workloads:
 *
 *	l2arc_write_max		max write bytes per interval
 *	l2arc_write_boost	extra write bytes during device warmup
 *	l2arc_noprefetch	skip caching prefetched buffers
 *	l2arc_headroom		number of max device writes to precache
 *	l2arc_feed_secs		seconds between L2ARC writing
 *
 * Tunables may be removed or added as future performance improvements are
 * integrated, and also may become zpool properties.
 *
 * There are three key functions that control how the L2ARC warms up:
 *
 *	l2arc_write_eligible()	check if a buffer is eligible to cache
 *	l2arc_write_size()	calculate how much to write
 *	l2arc_write_interval()	calculate sleep delay between writes
 *
 * These three functions determine what to write, how much, and how quickly
 * to send writes.
 */

static l2arc_buf_t L2ARC_write_buf = { 0 };
static l2arc_buf_t *l2arc_write_head = &L2ARC_write_buf;

static arc_hdr_t *
l2arc_hdr_from_cookie(void *cookie)
{
	l2arc_buf_t *l2buf = cookie;

	ASSERT(BUF_L2(l2buf));
	ASSERT(list_link_active(&l2buf->b_link));
	return (&l2buf->b_id);
}

static void
l2arc_destroy_cookie(void *cookie)
{
	l2arc_buf_t *l2buf = cookie;

	ASSERT(MUTEX_HELD(HASH_LOCK(l2buf->b_id.h_next)));
	l2buf->b_id.h_next = NULL;
}

static void *
l2arc_cache_check(spa_t *spa, arc_hdr_t *hdr)
{
	l2arc_buf_t *l2buf = (l2arc_buf_t *)hdr;
#if 0
	vdev_t *vd;
	boolean_t devw;
#endif
	ASSERT(BUF_GHOST(hdr));

	if (!BUF_L2(hdr) ||
	    !spa_config_tryenter(spa, SCL_L2ARC, l2buf, RW_READER))
		return (NULL);

#if 0
	/*
	 * We have now locked out device removal.
	 */
	vd = l2buf->b_dev->l2ad_vdev;
	devw = l2buf->b_dev->l2ad_writing;
	/* XXX - we should have a lock for devw check */
	if (vd  == NULL || vdev_is_dead(vd) || (l2arc_norw && devw)) {
		spa_config_exit(spa, SCL_L2ARC, l2buf);
		return (NULL);
	}
#endif

	return (l2buf);
}

static int
l2arc_read(zio_t *pio, spa_t *spa, const blkptr_t *bp,
    void *data, void *l2cookie, arc_read_callback_t *cb,
    int priority, enum zio_flag flags, const zbookmark_t *zb)
{
	l2arc_read_callback_t *l2cb;
	l2arc_buf_t *l2buf = l2cookie;
	vdev_t *vd = l2buf->b_dev->l2ad_vdev;
	boolean_t devw = l2buf->b_dev->l2ad_writing;
	uint64_t addr = l2buf->b_daddr;
	uint64_t size = BP_GET_LSIZE(bp);
	zio_t *zio;

	/* XXX - we should have a lock for devw check */
	if (vd  == NULL ||
	    vdev_is_dead(vd) || (l2arc_norw && devw)) {
		spa_config_exit(spa, SCL_L2ARC, l2buf);
		return (ENODEV);
	}

	/* we are holding the config lock */
	ASSERT(l2arc_ndev > 0);

	DTRACE_PROBE1(l2arc__hit, l2arc_buf_t *, l2buf);
	ARCSTAT_BUMP(arcstat_l2_hits);

	l2cb = kmem_zalloc(sizeof (l2arc_read_callback_t), KM_SLEEP);
	l2cb->l2rcb_hdr = l2buf;
	l2cb->l2rcb_cb = cb;
	l2cb->l2rcb_bp = *bp;
	l2cb->l2rcb_zb = *zb;
	l2cb->l2rcb_flags = flags;

	/*
	 * l2arc read.  The SCL_L2ARC lock will be
	 * released by l2arc_read_done().
	 */
	zio = zio_read_phys(pio, vd, addr, size, data,
	    ZIO_CHECKSUM_OFF, l2arc_read_done, l2cb, priority, flags |
	    ZIO_FLAG_CANFAIL | ZIO_FLAG_DONT_CACHE | ZIO_FLAG_DONT_RETRY |
	    ZIO_FLAG_DONT_PROPAGATE, B_FALSE);
	DTRACE_PROBE2(l2arc__read, vdev_t *, vd, zio_t *, zio);
	ARCSTAT_INCR(arcstat_l2_read_bytes, size);

	if (pio)
		zio_nowait(zio);
	else
		return (zio_wait(zio));

	return (0);
}

static boolean_t
l2arc_write_eligible(uint64_t spa_id, arc_buf_t *buf)
{
	/*
	 * A buffer is eligible for the L2ARC if it:
	 * 1. belongs to a this spa.
	 * 2. is in the hash table.
	 * 3. is not already cached on the L2ARC.
	 * 4. is not flagged ineligible (zfs property).
	 * 5. isn't prefetched or l2arc_noprefetch is FALSE.
	 */
	return (buf->b_id.h_spa == spa_id && BUF_HASHED(buf) &&
	    !(BUF_L2CACHED(buf) || ARC_FLAG_ISSET(buf, DONT_L2CACHE)) &&
	    !(l2arc_noprefetch && BUF_PREFETCHED(buf)));
}

static uint64_t
l2arc_write_size(l2arc_dev_t *dev)
{
	uint64_t size;

	size = dev->l2ad_write;

	if (!arc_warm)
		size += dev->l2ad_boost;

	return (size);

}

static clock_t
l2arc_write_interval(clock_t began, uint64_t wanted, uint64_t wrote)
{
	clock_t interval, next, now;

	/*
	 * If the ARC lists are busy, increase our write rate; if the
	 * lists are stale, idle back.  This is achieved by checking
	 * how much we previously wrote - if it was more than half of
	 * what we wanted, schedule the next write much sooner.
	 */
	if (l2arc_feed_again && wrote > (wanted / 2))
		interval = (hz * l2arc_feed_min_ms) / 1000;
	else
		interval = hz * l2arc_feed_secs;

	now = ddi_get_lbolt();
	next = MAX(now, MIN(now + interval, began + interval));

	return (next);
}

/*
 * Cycle through L2ARC devices.  This is how L2ARC load balances.
 * If a device is returned, this also returns holding the spa config lock.
 */
static l2arc_dev_t *
l2arc_dev_get_next(void)
{
	l2arc_dev_t *first, *next = NULL;

	/*
	 * Lock out the removal of spas (spa_namespace_lock), then removal
	 * of cache devices (l2arc_dev_mtx).  Once a device has been selected,
	 * both locks will be dropped and a spa config lock held instead.
	 */
	mutex_enter(&spa_namespace_lock);
	mutex_enter(&l2arc_dev_mtx);

	/* if there are no vdevs, there is nothing to do */
	if (l2arc_ndev == 0)
		goto out;

	first = NULL;
	next = l2arc_dev_last;
	do {
		/* loop around the list looking for a non-faulted vdev */
		if (next == NULL) {
			next = list_head(l2arc_dev_list);
		} else {
			next = list_next(l2arc_dev_list, next);
			if (next == NULL)
				next = list_head(l2arc_dev_list);
		}

		/* if we have come back to the start, bail out */
		if (first == NULL)
			first = next;
		else if (next == first)
			break;

	} while (vdev_is_dead(next->l2ad_vdev));

	/* if we were unable to find any usable vdevs, return NULL */
	if (vdev_is_dead(next->l2ad_vdev))
		next = NULL;

	l2arc_dev_last = next;

out:
	mutex_exit(&l2arc_dev_mtx);

	/*
	 * Grab the config lock to prevent the 'next' device from being
	 * removed while we are writing to it.
	 */
	if (next != NULL)
		spa_config_enter(next->l2ad_spa, SCL_L2ARC, next, RW_READER);
	mutex_exit(&spa_namespace_lock);

	return (next);
}

/*
 * A write to a cache device has completed.  Drop our holds on the data.
 */
static void
l2arc_write_done(zio_t *zio)
{
	l2arc_dev_t *dev;
	list_t *buflist;
	l2arc_buf_t *l2buf, *l2buf_prev;
	arc_buf_t *buf;

	dev = zio->io_private;
	ASSERT(dev != NULL);
	buflist = &dev->l2ad_buflist;
	DTRACE_PROBE2(l2arc__iodone, zio_t *, zio, l2arc_dev_t *, dev);

	if (zio->io_error != 0)
		ARCSTAT_BUMP(arcstat_l2_writes_error);

	mutex_enter(&l2arc_buflist_mtx);

	/*
	 * All writes completed, or an error was hit.
	 */
	for (l2buf = list_prev(buflist, l2arc_write_head); l2buf;
	    l2buf = l2buf_prev) {
		l2buf_prev = list_prev(buflist, l2buf);
		/*
		 * Note: next pointer is a back-pointer
		 * to the arc buffer while the l2buf is unhashed
		 */
		ASSERT(!ARC_FLAG_ISSET(l2buf, HASHED));
		buf = (arc_buf_t *)l2buf->b_id.h_next;
		ASSERT(buf->b_cookie == l2buf);
		mutex_enter(&buf->b_lock);

		if (zio->io_error) {
			/*
			 * Error - drop L2ARC entry.
			 */
			list_remove(buflist, l2buf);
			kmem_free(l2buf, sizeof (l2arc_buf_t));
			arc_space_return(sizeof (l2arc_buf_t), ARC_SPACE_L2);
			buf->b_cookie = NULL;
		}
		mutex_exit(&buf->b_lock);
		/*
		 * Allow ARC to evict/destroy this buffer
		 */
		/* XXX - change buflist tag to l2buf? */
		arc_rele(buf, buflist);
	}

	atomic_inc_64(&l2arc_writes_done);
	list_remove(buflist, l2arc_write_head);
	mutex_exit(&l2arc_buflist_mtx);
}

/*
 * A read to a cache device completed.  Validate buffer contents before
 * handing over to the regular ARC routines.
 */
static void
l2arc_read_done(zio_t *zio)
{
	l2arc_read_callback_t *cb;
	l2arc_buf_t *l2buf;
	arc_cksum_t cksum;

	ASSERT(zio->io_vd != NULL);
	ASSERT(zio->io_flags & ZIO_FLAG_DONT_PROPAGATE);

	cb = zio->io_private;
	ASSERT(cb != NULL);
	l2buf = cb->l2rcb_hdr;
	ASSERT(l2buf != NULL);

	spa_config_exit(zio->io_spa, SCL_L2ARC, l2buf);

	/*
	 * Check this survived the L2ARC journey.
	 */
	if (ARC_FLAG_ISSET(l2buf, BP_CKSUM_INUSE)) {
		cksum.c_value = cb->l2rcb_bp.blk_cksum;
		cksum.c_func = BP_GET_CHECKSUM(&cb->l2rcb_bp);
	} else {
		cksum = *l2buf->b_cksum;
	}
	if (zio->io_error == 0 &&
	    arc_cksum_valid(&cksum, zio->io_data, zio->io_size)) {
		zio->io_private = cb->l2rcb_cb;
		zio->io_bp_copy = cb->l2rcb_bp;	/* XXX fix in L2ARC 2.0	*/
		zio->io_bp = &zio->io_bp_copy;	/* XXX fix in L2ARC 2.0	*/
		arc_read_done(zio);
	} else {
		/*
		 * Buffer didn't survive caching.  Increment stats and
		 * reissue to the original storage device.
		 */
		if (zio->io_error != 0) {
			ARCSTAT_BUMP(arcstat_l2_io_error);
		} else {
			ARCSTAT_BUMP(arcstat_l2_cksum_bad);
			/* XXX - should this be ECKSUM? */
			zio->io_error = EIO;
		}

		/*
		 * If there's no waiter, issue an async i/o to the primary
		 * storage now.  If there *is* a waiter, the caller must
		 * issue the i/o in a context where it's OK to block.
		 */
		if (zio->io_waiter == NULL) {
			zio_t *pio = zio_unique_parent(zio);

			ASSERT(!pio || pio->io_child_type == ZIO_CHILD_LOGICAL);

			zio_nowait(zio_read(pio, zio->io_spa, &cb->l2rcb_bp,
			    zio->io_data, zio->io_size, arc_read_done,
			    cb->l2rcb_cb, zio->io_priority, cb->l2rcb_flags,
			    &cb->l2rcb_zb));
		}
	}

	kmem_free(cb, sizeof (l2arc_read_callback_t));
}

/*
 * This is the list priority from which the L2ARC will search for blocks to
 * cache.  This is used within loops (0..1) to cycle through lists in the
 * desired order.  This order can have a significant effect on cache
 * performance.
 *
 * Currently the MFU list is hit first, followed by the MRU list.
 * This function returns a locked list and lock pointer.
 */
static list_t *
l2arc_list_locked(int list_num, kmutex_t **list_lock, kmutex_t **link_lock)
{
	arc_evict_list_t *list;

	ASSERT(list_num >= 0 && list_num <= 1);

	switch (list_num) {
	case 0:
		list = &arc_mfu->s_evictable;
		break;
	case 1:
		list = &arc_mru->s_evictable;
		break;
	}
	*link_lock = &list->l_link_lock;
	*list_lock = &list->l_evict_lock;

	mutex_enter(*list_lock);
	return (&list->l_remove);
}

/*
 * Evict buffers from the device write hand to the distance specified in
 * bytes.  This distance may span populated buffers, it may span nothing.
 * This is clearing a region on the L2ARC device ready for writing.
 * If the 'all' boolean is set, every buffer is evicted.
 */
static void
l2arc_evict(l2arc_dev_t *dev, uint64_t distance, boolean_t all)
{
	list_t *buflist;
	l2arc_buf_t *l2buf;
	kmutex_t *hash_lock;
	uint64_t taddr;

	buflist = &dev->l2ad_buflist;

	if (!all && dev->l2ad_first) {
		/*
		 * This is the first sweep through the device.  There is
		 * nothing to evict.
		 */
		return;
	}

	if (dev->l2ad_hand >= (dev->l2ad_end - (2 * distance))) {
		/*
		 * When nearing the end of the device, evict to the end
		 * before the device write hand jumps to the start.
		 */
		taddr = dev->l2ad_end;
	} else {
		taddr = dev->l2ad_hand + distance;
	}
	DTRACE_PROBE4(l2arc__evict, l2arc_dev_t *, dev, list_t *, buflist,
	    uint64_t, taddr, boolean_t, all);

	mutex_enter(&l2arc_buflist_mtx);
	while (l2buf = list_tail(buflist)) {
		hash_lock = HASH_LOCK(l2buf);
		mutex_enter(hash_lock);

		/* eviction and writing are serialized wrt each other */
		ASSERT(l2buf != l2arc_write_head);
		ASSERT(BUF_L2(l2buf));

		if (!all && (l2buf->b_daddr > taddr ||
		    l2buf->b_daddr < dev->l2ad_hand)) {
			/*
			 * We've evicted to the target address,
			 * or the end of the device.
			 */
			mutex_exit(hash_lock);
			break;
		} else if (BUF_HASHED(l2buf)) {
			arc_state_t *state = BUF_STATE(l2buf);

			arc_hash_remove(&l2buf->b_id);
			ASSERT(GHOST_STATE(state));
			ASSERT3U(state->s_size, >=, 1);
			atomic_dec_64(&state->s_size);
		} else if (l2buf->b_id.h_next) {
			arc_buf_t *buf = (arc_buf_t *)l2buf->b_id.h_next;

			mutex_enter(&buf->b_lock);
			ASSERT(l2buf == buf->b_cookie);
			buf->b_cookie = NULL;
			mutex_exit(&buf->b_lock);
		}
		mutex_exit(hash_lock);

		/*
		 * Free l2arc hdr
		 */
		list_remove(buflist, l2buf);
		kmem_free(l2buf, sizeof (l2arc_buf_t));
		arc_space_return(sizeof (l2arc_buf_t), ARC_SPACE_L2);
	}
	mutex_exit(&l2arc_buflist_mtx);

	vdev_space_update(dev->l2ad_vdev, -(taddr - dev->l2ad_evict), 0, 0);
	dev->l2ad_evict = taddr;
}

/*
 * Find and write ARC buffers to the L2ARC device.
 *
 * Note that the ARC will not attempt to read these buffers while the
 * write is in progress since we "hold" the buffer in the cache until
 * the write completes.
 */
static uint64_t
l2arc_write_buffers(spa_t *spa, l2arc_dev_t *dev, uint64_t target_sz)
{
	list_t *buflist;
	uint64_t write_sz, buf_sz;
	boolean_t full;
	zio_t *pio, *zio;
	uint64_t spa_id = spa_guid(spa);

	ASSERT(dev->l2ad_vdev != NULL);

	pio = NULL;
	write_sz = 0;
	full = B_FALSE;
	buflist = &dev->l2ad_buflist;

	/*
	 * Copy buffers for L2ARC writing.
	 */
	mutex_enter(&l2arc_buflist_mtx);
	for (int try = 0; try <= 1 && !full; try++) {
		kmutex_t *list_lock, *link_lock;
		arc_elink_t *link, *next_link;
		uint64_t passed_sz = 0;
		uint64_t headroom = target_sz * l2arc_headroom;
		list_t *list = l2arc_list_locked(try, &list_lock, &link_lock);

		/*
		 * L2ARC fast warmup.
		 *
		 * Until the ARC is warm and starts to evict, read from the
		 * head of the ARC lists rather than the tail.
		 */
		link = arc_warm ? list_tail(list) : list_head(list);

		for (; link; link = next_link) {
			arc_buf_t *buf;
			l2arc_buf_t *l2buf;
			void *buf_data;

			next_link = arc_warm ?
			    list_prev(list, link) : list_next(list, link);

			mutex_enter(link_lock);
			buf = (arc_buf_t *)link->e_buf;
			if (buf == NULL || !mutex_tryenter(&buf->b_lock)) {
				mutex_exit(link_lock);
				continue;
			}
			mutex_exit(link_lock);

			passed_sz += buf->b_size;
			if (passed_sz > headroom) {
				/*
				 * Searched too far.
				 */
				mutex_exit(&buf->b_lock);
				break;
			}

			if (!l2arc_write_eligible(spa_id, buf)) {
				mutex_exit(&buf->b_lock);
				continue;
			}

			if ((write_sz + buf->b_size) > target_sz) {
				full = B_TRUE;
				mutex_exit(&buf->b_lock);
				break;
			}
			arc_hold(buf, buflist);

			if (pio == NULL) {
				/*
				 * Insert a dummy header on the buflist so
				 * l2arc_write_done() can find where the
				 * write buffers begin without searching.
				 */
				list_insert_head(buflist, l2arc_write_head);

				pio = zio_root(spa, l2arc_write_done, dev,
				    ZIO_FLAG_CANFAIL);
			}

			/*
			 * Create and add a new L2ARC buffer.
			 */
			l2buf = kmem_alloc(sizeof (l2arc_buf_t), KM_SLEEP);
			l2buf->b_dev = dev;
			l2buf->b_daddr = dev->l2ad_hand;
			l2buf->b_id = buf->b_id;
			l2buf->b_id.h_next = &buf->b_id;
			bzero(&l2buf->b_link, sizeof (list_node_t));
			ARC_FLAG_CLEAR(l2buf, HASHED);
			ARC_FLAG_SET(l2buf, GHOST);
			ARC_FLAG_SET(l2buf, L2BUF);
			arc_space_consume(sizeof (l2arc_buf_t), ARC_SPACE_L2);

			ASSERT(buf->b_cookie == NULL);
			buf->b_cookie = l2buf;
			list_insert_head(buflist, l2buf);
			buf_data = buf->b_data;
			buf_sz = buf->b_size;
			mutex_exit(&buf->b_lock);

			/*
			 * It would be nice not to have to store this in
			 * the l2buf and instead always rely on the bp
			 * checksum when we read, but that checksum will
			 * not work if the data is compressed or encrypted
			 * on disk.
			 */
			if (!ARC_FLAG_ISSET(buf, BP_CKSUM_INUSE))
				l2buf->b_cksum =
				    arc_cksum_compute(buf_data, buf_sz);

			zio = zio_write_phys(pio, dev->l2ad_vdev,
			    dev->l2ad_hand, buf_sz, buf_data, ZIO_CHECKSUM_OFF,
			    NULL, NULL, ZIO_PRIORITY_ASYNC_WRITE,
			    ZIO_FLAG_CANFAIL, B_FALSE);

			DTRACE_PROBE2(l2arc__write,
			    vdev_t *, dev->l2ad_vdev, zio_t *, zio);
			(void) zio_nowait(zio);

			/*
			 * Keep the clock hand suitably device-aligned.
			 */
			buf_sz = vdev_psize_to_asize(dev->l2ad_vdev, buf_sz,
			    DVA_LAYOUT_STANDARD, 1);

			write_sz += buf_sz;
			dev->l2ad_hand += buf_sz;
		}
		mutex_exit(list_lock);
	}
	mutex_exit(&l2arc_buflist_mtx);

	if (pio == NULL) {
		ASSERT3U(write_sz, ==, 0);
		return (0);
	}

	ASSERT3U(write_sz, <=, target_sz);
	ARCSTAT_BUMP(arcstat_l2_writes_sent);
	ARCSTAT_INCR(arcstat_l2_write_bytes, write_sz);
	vdev_space_update(dev->l2ad_vdev, write_sz, 0, 0);

	/*
	 * Bump device hand to the device start if it is approaching the end.
	 * l2arc_evict() will already have evicted ahead for this case.
	 */
	if (dev->l2ad_hand >= (dev->l2ad_end - target_sz)) {
		vdev_space_update(dev->l2ad_vdev,
		    dev->l2ad_end - dev->l2ad_hand, 0, 0);
		dev->l2ad_hand = dev->l2ad_start;
		dev->l2ad_evict = dev->l2ad_start;
		dev->l2ad_first = B_FALSE;
	}

	dev->l2ad_writing = B_TRUE;
	(void) zio_wait(pio);
	dev->l2ad_writing = B_FALSE;

	return (write_sz);
}

/*
 * This thread feeds the L2ARC at regular intervals.  This is the beating
 * heart of the L2ARC.
 */
static void
l2arc_feed_thread(void)
{
	callb_cpr_t cpr;
	l2arc_dev_t *dev;
	spa_t *spa;
	uint64_t size, wrote;
	clock_t begin, next = ddi_get_lbolt();

	CALLB_CPR_INIT(&cpr, &l2arc_feed_thr_lock, callb_generic_cpr, FTAG);

	mutex_enter(&l2arc_feed_thr_lock);

	while (l2arc_thread_exit == 0) {
		CALLB_CPR_SAFE_BEGIN(&cpr);
		(void) cv_timedwait(&l2arc_feed_thr_cv, &l2arc_feed_thr_lock,
		    next);
		CALLB_CPR_SAFE_END(&cpr, &l2arc_feed_thr_lock);
		next = ddi_get_lbolt() + hz;

		/*
		 * Quick check for L2ARC devices.
		 */
		mutex_enter(&l2arc_dev_mtx);
		if (l2arc_ndev == 0) {
			mutex_exit(&l2arc_dev_mtx);
			continue;
		}
		mutex_exit(&l2arc_dev_mtx);
		begin = ddi_get_lbolt();

		/*
		 * This selects the next l2arc device to write to, and in
		 * doing so the next spa to feed from: dev->l2ad_spa.   This
		 * will return NULL if there are now no l2arc devices or if
		 * they are all faulted.
		 *
		 * If a device is returned, its spa's config lock is also
		 * held to prevent device removal.  l2arc_dev_get_next()
		 * will grab and release l2arc_dev_mtx.
		 */
		if ((dev = l2arc_dev_get_next()) == NULL)
			continue;

		spa = dev->l2ad_spa;
		ASSERT(spa != NULL);

		/*
		 * If the pool is read-only then force the feed thread to
		 * sleep a little longer.
		 */
		if (!spa_writeable(spa)) {
			next = ddi_get_lbolt() + 5 * l2arc_feed_secs * hz;
			spa_config_exit(spa, SCL_L2ARC, dev);
			continue;
		}

		/*
		 * Avoid contributing to memory pressure.
		 */
		if (arc_reclaim_needed()) {
			ARCSTAT_BUMP(arcstat_l2_abort_lowmem);
			spa_config_exit(spa, SCL_L2ARC, dev);
			continue;
		}

		ARCSTAT_BUMP(arcstat_l2_feeds);

		size = l2arc_write_size(dev);

		/*
		 * Evict L2ARC buffers that will be overwritten.
		 */
		l2arc_evict(dev, size, B_FALSE);

		/*
		 * Write ARC buffers.
		 */
		wrote = l2arc_write_buffers(spa, dev, size);

		/*
		 * Calculate interval between writes.
		 */
		next = l2arc_write_interval(begin, size, wrote);
		spa_config_exit(spa, SCL_L2ARC, dev);
	}

	l2arc_thread_exit = 0;
	cv_broadcast(&l2arc_feed_thr_cv);
	CALLB_CPR_EXIT(&cpr);		/* drops l2arc_feed_thr_lock */
	thread_exit();
}

boolean_t
l2arc_vdev_present(vdev_t *vd)
{
	l2arc_dev_t *dev;

	mutex_enter(&l2arc_dev_mtx);
	for (dev = list_head(l2arc_dev_list); dev != NULL;
	    dev = list_next(l2arc_dev_list, dev)) {
		if (dev->l2ad_vdev == vd)
			break;
	}
	mutex_exit(&l2arc_dev_mtx);

	return (dev != NULL);
}

/*
 * Add a vdev for use by the L2ARC.  By this point the spa has already
 * validated the vdev and opened it.
 */
void
l2arc_add_vdev(spa_t *spa, vdev_t *vd)
{
	l2arc_dev_t *adddev;

	ASSERT(!l2arc_vdev_present(vd));

	/*
	 * Create a new l2arc device entry.
	 */
	adddev = kmem_zalloc(sizeof (l2arc_dev_t), KM_SLEEP);
	adddev->l2ad_spa = spa;
	adddev->l2ad_vdev = vd;
	adddev->l2ad_write = l2arc_write_max;
	adddev->l2ad_boost = l2arc_write_boost;
	adddev->l2ad_start = VDEV_LABEL_START_SIZE;
	adddev->l2ad_end = VDEV_LABEL_START_SIZE + vdev_get_min_asize(vd);
	adddev->l2ad_hand = adddev->l2ad_start;
	adddev->l2ad_evict = adddev->l2ad_start;
	adddev->l2ad_first = B_TRUE;
	adddev->l2ad_writing = B_FALSE;
	ASSERT3U(adddev->l2ad_write, >, 0);

	/*
	 * This is a list of all ARC buffers that are still valid on the
	 * device.
	 */
	list_create(&adddev->l2ad_buflist, sizeof (l2arc_buf_t),
	    offsetof(l2arc_buf_t, b_link));

	vdev_space_update(vd, 0, 0, adddev->l2ad_end - adddev->l2ad_hand);

	/*
	 * Add device to global list
	 */
	mutex_enter(&l2arc_dev_mtx);
	list_insert_head(l2arc_dev_list, adddev);
	atomic_inc_64(&l2arc_ndev);
	mutex_exit(&l2arc_dev_mtx);
}

/*
 * Remove a vdev from the L2ARC.
 */
void
l2arc_remove_vdev(vdev_t *vd)
{
	l2arc_dev_t *dev, *nextdev, *remdev = NULL;

	/*
	 * Find the device by vdev
	 */
	mutex_enter(&l2arc_dev_mtx);
	for (dev = list_head(l2arc_dev_list); dev; dev = nextdev) {
		nextdev = list_next(l2arc_dev_list, dev);
		if (vd == dev->l2ad_vdev) {
			remdev = dev;
			break;
		}
	}
	ASSERT(remdev != NULL);

	/*
	 * Remove device from global list
	 */
	list_remove(l2arc_dev_list, remdev);
	l2arc_dev_last = NULL;		/* may have been invalidated */
	atomic_dec_64(&l2arc_ndev);
	mutex_exit(&l2arc_dev_mtx);

	/*
	 * Clear all buflists and ARC references.  L2ARC device flush.
	 */
	l2arc_evict(remdev, 0, B_TRUE);
	ASSERT3P(list_head(&remdev->l2ad_buflist), ==, NULL);
	list_destroy(&remdev->l2ad_buflist);
	kmem_free(remdev, sizeof (l2arc_dev_t));
}

void
l2arc_init(void)
{
	l2arc_thread_exit = 0;
	l2arc_ndev = 0;
	l2arc_writes_sent = 0;
	l2arc_writes_done = 0;

	mutex_init(&l2arc_feed_thr_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&l2arc_feed_thr_cv, NULL, CV_DEFAULT, NULL);
	mutex_init(&l2arc_dev_mtx, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&l2arc_buflist_mtx, NULL, MUTEX_DEFAULT, NULL);

	l2arc_dev_list = &L2ARC_dev_list;
	list_create(l2arc_dev_list, sizeof (l2arc_dev_t),
	    offsetof(l2arc_dev_t, l2ad_node));
}

void
l2arc_fini(void)
{
	/*
	 * This is called from dmu_fini(), which is called from spa_fini();
	 * Because of this, we can assume that all l2arc devices have
	 * already been removed when the pools themselves were removed.
	 */

	mutex_destroy(&l2arc_feed_thr_lock);
	cv_destroy(&l2arc_feed_thr_cv);
	mutex_destroy(&l2arc_dev_mtx);
	mutex_destroy(&l2arc_buflist_mtx);

	list_destroy(l2arc_dev_list);
}

void
l2arc_start(void)
{
	if (!(spa_mode_global & FWRITE))
		return;

	(void) thread_create(NULL, 0, l2arc_feed_thread, NULL, 0, &p0,
	    TS_RUN, minclsyspri);
}

void
l2arc_stop(void)
{
	if (!(spa_mode_global & FWRITE))
		return;

	mutex_enter(&l2arc_feed_thr_lock);
	cv_signal(&l2arc_feed_thr_cv);	/* kick thread out of startup */
	l2arc_thread_exit = 1;
	while (l2arc_thread_exit != 0)
		cv_wait(&l2arc_feed_thr_cv, &l2arc_feed_thr_lock);
	mutex_exit(&l2arc_feed_thr_lock);
}
