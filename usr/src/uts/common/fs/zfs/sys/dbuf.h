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

#ifndef	_SYS_DBUF_H
#define	_SYS_DBUF_H

#include <sys/dmu.h>
#include <sys/spa.h>
#include <sys/txg.h>
#include <sys/zio.h>
#include <sys/arc.h>
#include <sys/zfs_context.h>
#include <sys/refcount.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	IN_DMU_SYNC 2

/*
 * define flags for dbuf_read
 */

#define	DB_RF_MUST_SUCCEED	(1 << 0)
#define	DB_RF_CANFAIL		(1 << 1)
#define	DB_RF_HAVESTRUCT	(1 << 2)
#define	DB_RF_NOPREFETCH	(1 << 3)
#define	DB_RF_NEVERWAIT		(1 << 4)

/*
 * The simplified state transition diagram for dbufs looks like:
 *
 *		+----> READ ----+
 *		|		|
 *		|		V
 *  (alloc)-->UNCACHED	     CACHED-->EVICTING-->(free)
 *		|		^	 ^
 *		|		|	 |
 *		+----> FILL ----+	 |
 *		|			 |
 *		|			 |
 *		+--------> NOFILL -------+
 */
typedef enum dbuf_states {
	DB_UNCACHED,
	DB_FILL,
	DB_NOFILL,
	DB_READ,
	DB_CACHED,
	DB_EVICTING
} dbuf_states_t;

#define	DB_BPSINBLK(idb)	((idb)->db.db_size / (1 << SPA_BLKPTRSHIFT))
#define	DB_BLKPTR_OFFSET(db)	\
	((((db)->db_blkid) & (DB_BPSINBLK((db)->db_parent) - 1)) \
	<< SPA_BLKPTRSHIFT)

struct dnode;
struct dmu_tx;

/*
 * level = 0 means the user data
 * level = 1 means the single indirect block
 * etc.
 */

struct dmu_buf_impl;

typedef enum override_states {
	DR_NOT_OVERRIDDEN,
	DR_IN_DMU_SYNC,
	DR_OVERRIDDEN
} override_states_t;

typedef struct dbuf_dirty_record dbuf_dirty_record_t;
typedef struct dmu_buf_impl dmu_buf_impl_t;

struct dbuf_dirty_record {
	uint64_t		dr_txg;		/* txg this data will sync in */
	zio_t			*dr_zio;	/* zio of current write IO */
	dmu_buf_impl_t		*dr_dbuf;	/* pointer back to our dbuf */
	dbuf_dirty_record_t	*dr_next;	/* pointer to next record */
	dbuf_dirty_record_t	*dr_parent;	/* pointer to our parent */
	list_node_t		dr_dirty_link;	/* link on our parents list */
	kmutex_t		dr_mtx;		/* protect access to list */
	list_t			dr_children;	/* our list of dirty children */
	arc_ref_t		*dr_ref;	/* ref to our arc buf */
	override_states_t	dr_override_state;
	blkptr_t		dr_overridden_by;
	uint8_t			dr_copies;
};

struct dmu_buf_impl {
	/*
	 * The following members are immutable, with the exception of
	 * db.db_data, which is protected by db_mtx.
	 */
	dmu_buf_t		db;		/* our public face (readers) */

	struct objset		*db_objset;	/* the objset we belong to */
	struct dnode_handle	*db_dnode_handle; /* our dnode handle */
	dmu_buf_impl_t		*db_parent;	/* our parent */
	dmu_buf_impl_t		*db_hash_next;	/* hash table bucket link */
	uint64_t		db_blkid;	/* our block number */
	uint32_t		db_blkoff;	/* offset for the dbuf blkptr */
	uint8_t			db_level;	/* or "level" in the dnode */

	kmutex_t		db_mtx;

	/* members protected by the db_mtx */
	uint64_t		db_birth;	/* birth txg of block */
	dbuf_states_t		db_state;	/* current buffer state */
	refcount_t		db_holds;	/* dbuf hold count */
	uint64_t		db_writers_waiting;
	arc_ref_t		*db_ref;	/* ref to ARC buffer for data */
	kcondvar_t		db_changed;	/* cv to signal state changes */
	dbuf_dirty_record_t	*db_last_dirty;	/* dirty record list */
	dbuf_dirty_record_t	*db_data_pending; /* dr for current IO */
	list_node_t		db_link;	/* our link on the dnode */

	/* Data which is unique to data (leaf) blocks: */

	/* stuff we store for the user (see dmu_buf_set_user) */
	void			*db_user_ptr;
	void			**db_user_data_ptr_ptr;
	dmu_buf_evict_func_t	*db_evict_func;

	/* flags */
	uint8_t			db_immediate_evict;
	uint8_t			db_freed_in_flight;
	uint8_t			db_managed;

	uint8_t			db_dirtycnt;	/* length of db_last_dirty */
};

/* Note: the dbuf hash table is exposed only for the mdb module */
#define	DBUF_MUTEXES 256
#define	DBUF_HASH_MUTEX(h, idx) (&((h)->hash_mutexes[(idx) & \
	(DBUF_MUTEXES-1)]).ht_lock)
typedef struct dbuf_hash_table {
	uint64_t hash_table_mask;
	dmu_buf_impl_t **hash_table;
	struct ht_lock hash_mutexes[DBUF_MUTEXES];
} dbuf_hash_table_t;

#define	DBUF_PAST_EOF	-1

uint64_t dbuf_whichblock(struct dnode *di, uint64_t offset);

dmu_buf_impl_t *dbuf_create_tlib(struct dnode *dn, char *data);
void dbuf_create_bonus(struct dnode *dn);
void dbuf_spill_set_blksz(dmu_buf_t *db, uint64_t blksz, dmu_tx_t *tx);

dmu_buf_impl_t *dbuf_hold(struct dnode *dn, uint64_t blkid, void *tag);
dmu_buf_impl_t *dbuf_hold_level(struct dnode *dn, int level, uint64_t blkid,
    void *tag);
int dbuf_hold_impl(struct dnode *dn, uint8_t level, uint64_t blkid, int create,
    void *tag, dmu_buf_impl_t **dbp);

void dbuf_prefetch(struct dnode *dn, uint64_t blkid);

void dbuf_add_ref(dmu_buf_impl_t *db, void *tag);
uint64_t dbuf_refcount(dmu_buf_impl_t *db);

void dbuf_rele(dmu_buf_impl_t *db, void *tag);
void dbuf_rele_and_unlock(dmu_buf_impl_t *db, void *tag);

dmu_buf_impl_t *dbuf_find(struct dnode *dn, uint8_t level, uint64_t blkid);

int dbuf_read(dmu_buf_impl_t *db, zio_t *zio, uint32_t flags);
void dbuf_will_dirty(dmu_buf_impl_t *db, dmu_tx_t *tx);
void dbuf_fill_done(dmu_buf_impl_t *db, dmu_tx_t *tx);
void dmu_buf_will_not_fill(dmu_buf_t *db, dmu_tx_t *tx);
void dmu_buf_will_fill(dmu_buf_t *db, dmu_tx_t *tx);
void dmu_buf_fill_done(dmu_buf_t *db, dmu_tx_t *tx);
void dbuf_assign_arcbuf(dmu_buf_impl_t *db, arc_ref_t *buf, dmu_tx_t *tx);
dbuf_dirty_record_t *dbuf_dirty(dmu_buf_impl_t *db, dmu_tx_t *tx);
arc_ref_t *dbuf_hold_arcbuf(dmu_buf_impl_t *db);

void dbuf_evict(dmu_buf_impl_t *db);

void dbuf_setdirty(dmu_buf_impl_t *db, dmu_tx_t *tx);
void dbuf_unoverride(dbuf_dirty_record_t *dr);
void dbuf_set_data(dmu_buf_impl_t *db, arc_ref_t *buf);
void dbuf_sync_list(list_t *list, dmu_tx_t *tx);
void dbuf_make_indirect_writable(dmu_buf_impl_t *db);

void dbuf_free_range(struct dnode *dn, uint64_t start, uint64_t end,
    struct dmu_tx *);

void dbuf_new_size(dmu_buf_impl_t *db, int size, dmu_tx_t *tx);

#define	DB_DNODE(_db)		((_db)->db_dnode_handle->dnh_dnode)
#define	DB_HOLD_DNODE(_db)	(DN_HOLD_HANDLE((_db)->db_dnode_handle),\
				(_db)->db_dnode_handle->dnh_dnode)
#define	DB_RELE_DNODE(_db)	DN_RELEASE_HANDLE((_db)->db_dnode_handle)

int dbuf_block_freeable(dmu_buf_impl_t *db, uint64_t *birth);

void dbuf_init(void);
void dbuf_fini(void);

boolean_t dbuf_is_metadata(dmu_buf_impl_t *db);

#define	DBUF_IS_METADATA(_db)	\
	(dbuf_is_metadata(_db))

#define	DBUF_IS_CACHEABLE(_db)						\
	((_db)->db_objset->os_primary_cache == ZFS_CACHE_ALL ||		\
	(DBUF_IS_METADATA(_db) &&					\
	((_db)->db_objset->os_primary_cache == ZFS_CACHE_METADATA)))

#define	DBUF_IS_L2CACHEABLE(_db)					\
	((_db)->db_objset->os_secondary_cache == ZFS_CACHE_ALL ||	\
	(DBUF_IS_METADATA(_db) &&					\
	((_db)->db_objset->os_secondary_cache == ZFS_CACHE_METADATA)))

#ifdef ZFS_DEBUG

/*
 * There should be a ## between the string literal and fmt, to make it
 * clear that we're joining two strings together, but gcc does not
 * support that preprocessor token.
 */
#define	dprintf_dbuf(dbuf, fmt, ...) do { \
	if (zfs_flags & ZFS_DEBUG_DPRINTF) { \
	char __db_buf[32]; \
	uint64_t __db_obj = (dbuf)->db.db_object; \
	if (__db_obj == DMU_META_DNODE_OBJECT) \
		(void) strcpy(__db_buf, "mdn"); \
	else \
		(void) snprintf(__db_buf, sizeof (__db_buf), "%lld", \
		    (u_longlong_t)__db_obj); \
	dprintf_ds((dbuf)->db_objset->os_dsl_dataset, \
	    "obj=%s lvl=%u blkid=%lld " fmt, \
	    __db_buf, (dbuf)->db_level, \
	    (u_longlong_t)(dbuf)->db_blkid, __VA_ARGS__); \
	} \
_NOTE(CONSTCOND) } while (0)

#define	dprintf_dbuf_bp(db, bp, fmt, ...) do {			\
	if (zfs_flags & ZFS_DEBUG_DPRINTF) {			\
	char *__blkbuf = kmem_alloc(BP_SPRINTF_LEN, KM_SLEEP);	\
	sprintf_blkptr(__blkbuf, bp);				\
	dprintf_dbuf(db, fmt " %s\n", __VA_ARGS__, __blkbuf);	\
	kmem_free(__blkbuf, BP_SPRINTF_LEN);			\
	}							\
_NOTE(CONSTCOND) } while (0)

#define	DBUF_VERIFY(db)	dbuf_verify(db)

#else

#define	dprintf_dbuf(db, fmt, ...)
#define	dprintf_dbuf_bp(db, bp, fmt, ...)
#define	DBUF_VERIFY(db)

#endif


#ifdef	__cplusplus
}
#endif

#endif /* _SYS_DBUF_H */
