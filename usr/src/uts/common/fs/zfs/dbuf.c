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

#include <sys/zfs_context.h>
#include <sys/dmu.h>
#include <sys/dmu_impl.h>
#include <sys/dbuf.h>
#include <sys/dmu_objset.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_dir.h>
#include <sys/dmu_tx.h>
#include <sys/spa.h>
#include <sys/zio.h>
#include <sys/dmu_zfetch.h>
#include <sys/sa.h>
#include <sys/sa_impl.h>

static void dbuf_destroy(dmu_buf_impl_t *db);
static void dbuf_write(dbuf_dirty_record_t *dr, arc_ref_t *data, dmu_tx_t *tx);

/*
 * Global data structures and functions for the dbuf cache.
 */
static kmem_cache_t *dbuf_cache;

/* ARGSUSED */
static int
dbuf_cons(void *vdb, void *unused, int kmflag)
{
	dmu_buf_impl_t *db = vdb;
	bzero(db, sizeof (dmu_buf_impl_t));

	mutex_init(&db->db_mtx, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&db->db_changed, NULL, CV_DEFAULT, NULL);
	refcount_create(&db->db_holds);
	return (0);
}

/* ARGSUSED */
static void
dbuf_dest(void *vdb, void *unused)
{
	dmu_buf_impl_t *db = vdb;
	mutex_destroy(&db->db_mtx);
	cv_destroy(&db->db_changed);
	refcount_destroy(&db->db_holds);
}

/*
 * dbuf hash table routines
 */
static dbuf_hash_table_t dbuf_hash_table;

static uint64_t dbuf_hash_count;

static uint64_t
dbuf_hash(void *os, uint64_t obj, uint8_t lvl, uint64_t blkid)
{
	uintptr_t osv = (uintptr_t)os;
	uint64_t crc = -1ULL;

	ASSERT(zfs_crc64_table[128] == ZFS_CRC64_POLY);
	crc = (crc >> 8) ^ zfs_crc64_table[(crc ^ (lvl)) & 0xFF];
	crc = (crc >> 8) ^ zfs_crc64_table[(crc ^ (osv >> 6)) & 0xFF];
	crc = (crc >> 8) ^ zfs_crc64_table[(crc ^ (obj >> 0)) & 0xFF];
	crc = (crc >> 8) ^ zfs_crc64_table[(crc ^ (obj >> 8)) & 0xFF];
	crc = (crc >> 8) ^ zfs_crc64_table[(crc ^ (blkid >> 0)) & 0xFF];
	crc = (crc >> 8) ^ zfs_crc64_table[(crc ^ (blkid >> 8)) & 0xFF];

	crc ^= (osv>>14) ^ (obj>>16) ^ (blkid>>16);

	return (crc);
}

#define	DBUF_HASH(os, obj, level, blkid) dbuf_hash(os, obj, level, blkid);

#define	DBUF_EQUAL(dbuf, os, obj, level, blkid)		\
	((dbuf)->db.db_object == (obj) &&		\
	(dbuf)->db_objset == (os) &&			\
	(dbuf)->db_level == (level) &&			\
	(dbuf)->db_blkid == (blkid))

dmu_buf_impl_t *
dbuf_find(dnode_t *dn, uint8_t level, uint64_t blkid)
{
	dbuf_hash_table_t *h = &dbuf_hash_table;
	objset_t *os = dn->dn_objset;
	uint64_t obj = dn->dn_object;
	uint64_t hv = DBUF_HASH(os, obj, level, blkid);
	uint64_t idx = hv & h->hash_table_mask;
	dmu_buf_impl_t *db;

	mutex_enter(DBUF_HASH_MUTEX(h, idx));
	for (db = h->hash_table[idx]; db != NULL; db = db->db_hash_next) {
		if (DBUF_EQUAL(db, os, obj, level, blkid)) {
			mutex_enter(&db->db_mtx);
			if (db->db_state != DB_EVICTING) {
				mutex_exit(DBUF_HASH_MUTEX(h, idx));
				return (db);
			}
			mutex_exit(&db->db_mtx);
		}
	}
	mutex_exit(DBUF_HASH_MUTEX(h, idx));
	return (NULL);
}

/*
 * Insert an entry into the hash table.  If there is already an element
 * equal to elem in the hash table, then the already existing element
 * will be returned and the new element will not be inserted.
 * Otherwise returns NULL.
 */
static dmu_buf_impl_t *
dbuf_hash_insert(dmu_buf_impl_t *db)
{
	dbuf_hash_table_t *h = &dbuf_hash_table;
	objset_t *os = db->db_objset;
	uint64_t obj = db->db.db_object;
	int level = db->db_level;
	uint64_t blkid = db->db_blkid;
	uint64_t hv = DBUF_HASH(os, obj, level, blkid);
	uint64_t idx = hv & h->hash_table_mask;
	dmu_buf_impl_t *dbf;

	mutex_enter(DBUF_HASH_MUTEX(h, idx));
	for (dbf = h->hash_table[idx]; dbf != NULL; dbf = dbf->db_hash_next) {
		if (DBUF_EQUAL(dbf, os, obj, level, blkid)) {
			mutex_enter(&dbf->db_mtx);
			if (dbf->db_state != DB_EVICTING) {
				mutex_exit(DBUF_HASH_MUTEX(h, idx));
				return (dbf);
			}
			mutex_exit(&dbf->db_mtx);
		}
	}

	mutex_enter(&db->db_mtx);
	db->db_hash_next = h->hash_table[idx];
	h->hash_table[idx] = db;
	mutex_exit(DBUF_HASH_MUTEX(h, idx));
	atomic_add_64(&dbuf_hash_count, 1);

	return (NULL);
}

/*
 * Remove an entry from the hash table.
 * Note: there cannot be any existing holds on the db.
 */
static void
dbuf_hash_remove(dmu_buf_impl_t *db)
{
	dbuf_hash_table_t *h = &dbuf_hash_table;
	uint64_t hv = DBUF_HASH(db->db_objset, db->db.db_object,
	    db->db_level, db->db_blkid);
	uint64_t idx = hv & h->hash_table_mask;
	dmu_buf_impl_t *dbf, **dbp;

	/*
	 * We musn't hold db_mtx to maintin lock ordering:
	 * DBUF_HASH_MUTEX > db_mtx.
	 */
	ASSERT(refcount_is_zero(&db->db_holds));
	ASSERT(db->db_state == DB_EVICTING);
	ASSERT(!MUTEX_HELD(&db->db_mtx));

	mutex_enter(DBUF_HASH_MUTEX(h, idx));
	dbp = &h->hash_table[idx];
	while ((dbf = *dbp) != db) {
		dbp = &dbf->db_hash_next;
		ASSERT(dbf != NULL);
	}
	*dbp = db->db_hash_next;
	db->db_hash_next = NULL;
	mutex_exit(DBUF_HASH_MUTEX(h, idx));
	atomic_add_64(&dbuf_hash_count, -1);
}

static arc_evict_func_t dbuf_do_evict;

static void
dbuf_evict_user(dmu_buf_impl_t *db)
{
	ASSERT(MUTEX_HELD(&db->db_mtx));

	if (db->db_level != 0 || db->db_evict_func == NULL)
		return;

	if (db->db_user_data_ptr_ptr)
		*db->db_user_data_ptr_ptr = db->db.db_data;
	db->db_evict_func(&db->db, db->db_user_ptr);
	db->db_user_ptr = NULL;
	db->db_user_data_ptr_ptr = NULL;
	db->db_evict_func = NULL;
}

boolean_t
dbuf_is_metadata(dmu_buf_impl_t *db)
{
	boolean_t is_metadata = TRUE;

	if (db->db_level == 0 && db->db_blkid != DMU_SPILL_BLKID) {
		dnode_t *dn = DB_HOLD_DNODE(db);
		is_metadata = dmu_ot[dn->dn_type].ot_metadata;
		DB_RELE_DNODE(db);
	}
	return (is_metadata);
}

void
dbuf_init(void)
{
	uint64_t hsize = 1ULL << 16;
	dbuf_hash_table_t *h = &dbuf_hash_table;
	int i;

	/*
	 * The hash table is big enough to fill all of physical memory
	 * with an average 4K block size.  The table will take up
	 * totalmem*sizeof(void*)/4K (i.e. 2MB/GB with 8-byte pointers).
	 */
	while (hsize * 4096 < physmem * PAGESIZE)
		hsize <<= 1;

retry:
	h->hash_table_mask = hsize - 1;
	h->hash_table = kmem_zalloc(hsize * sizeof (void *), KM_NOSLEEP);
	if (h->hash_table == NULL) {
		/* XXX - we should really return an error instead of assert */
		ASSERT(hsize > (1ULL << 10));
		hsize >>= 1;
		goto retry;
	}

	dbuf_cache = kmem_cache_create("dmu_buf_impl_t",
	    sizeof (dmu_buf_impl_t),
	    0, dbuf_cons, dbuf_dest, NULL, NULL, NULL, 0);

	for (i = 0; i < DBUF_MUTEXES; i++)
		mutex_init(&(h->hash_mutexes[i].ht_lock), NULL, MUTEX_DEFAULT,
		    NULL);
}

void
dbuf_fini(void)
{
	dbuf_hash_table_t *h = &dbuf_hash_table;
	int i;

	for (i = 0; i < DBUF_MUTEXES; i++)
		mutex_destroy(&(h->hash_mutexes[i].ht_lock));
	kmem_free(h->hash_table, (h->hash_table_mask + 1) * sizeof (void *));
	kmem_cache_destroy(dbuf_cache);
}

/*
 * Other stuff.
 */

#ifdef ZFS_DEBUG
static void
dbuf_verify(dmu_buf_impl_t *db)
{
	dnode_t *dn;
	dbuf_dirty_record_t *dr;

	ASSERT(MUTEX_HELD(&db->db_mtx));

	if (!(zfs_flags & ZFS_DEBUG_DBUF_VERIFY))
		return;

	ASSERT(db->db_objset != NULL);
	dn = DB_HOLD_DNODE(db);
	if (dn == NULL) {
		ASSERT(db->db_parent == NULL);
	} else {
		ASSERT3U(db->db.db_object, ==, dn->dn_object);
		ASSERT3P(db->db_objset, ==, dn->dn_objset);
		ASSERT3U(db->db_level, <, dn->dn_nlevels);
		ASSERT(db->db_blkid == DMU_BONUS_BLKID ||
		    db->db_blkid == DMU_SPILL_BLKID ||
		    !list_is_empty(&dn->dn_dbufs));
	}
	if (db->db_blkid == DMU_BONUS_BLKID ||
	    db->db_blkid == DMU_SPILL_BLKID) {
		ASSERT(dn != NULL);
		ASSERT3U(db->db.db_size, >=, dn->dn_bonuslen);
		ASSERT3U(db->db.db_offset, ==, db->db_blkid);
	} else {
		ASSERT3U(db->db.db_offset, ==, db->db_blkid * db->db.db_size);
	}

	for (dr = db->db_last_dirty; dr != NULL; dr = dr->dr_next)
		ASSERT(dr->dr_dbuf == db);

	/*
	 * We can't assert that db_size matches dn_datablksz because it
	 * can be momentarily different when another thread is doing
	 * dnode_set_blksz().
	 */
	if (db->db_level == 0 && db->db.db_object == DMU_META_DNODE_OBJECT) {
		dr = db->db_data_pending;
		/*
		 * It should only be modified in syncing context, so
		 * make sure we only have one copy of the data.
		 */
		ASSERT(dr == NULL || dr->dr_ref == db->db_ref);
	}

	/* verify db->db_blkoff */
	if (db->db_parent && db->db_blkid != DMU_BONUS_BLKID) {
		if (db->db_parent == dn->dn_dbuf) {
			/* db is pointed to by the dnode */
			/* ASSERT3U(db->db_blkid, <, dn->dn_nblkptr); */
			if (db->db_blkid == DMU_SPILL_BLKID)
				ASSERT(db->db_blkoff == DBUF_PAST_EOF ||
				    db->db_blkoff == DN_SPILL_OFFSET(dn));
			else
				ASSERT3U(db->db_blkoff, ==,
				    DN_BLKPTR_OFFSET(dn, db->db_blkid));
		} else {
			/* db is pointed to by an indirect block */
			ASSERT3U(db->db_parent->db_level, ==, db->db_level+1);
			ASSERT3U(db->db_parent->db.db_object, ==,
			    db->db.db_object);
			ASSERT3P(db->db_blkoff, ==, DB_BLKPTR_OFFSET(db));
		}
	}
	DB_RELE_DNODE(db);
}
#endif

static void
dbuf_update_data(dmu_buf_impl_t *db)
{
	ASSERT(MUTEX_HELD(&db->db_mtx));
	if (db->db_level == 0 && db->db_user_data_ptr_ptr) {
		ASSERT(!refcount_is_zero(&db->db_holds));
		*db->db_user_data_ptr_ptr = db->db.db_data;
	}
}

void
dbuf_set_data(dmu_buf_impl_t *db, arc_ref_t *buf)
{
	ASSERT(MUTEX_HELD(&db->db_mtx));
	if (buf) {
		ASSERT(buf->r_data != NULL);
		ASSERT(db->db_ref == NULL || db->db_ref == buf ||
		    db->db_ref == db->db_last_dirty->dr_ref);
		db->db_ref = buf;
		db->db.db_data = buf->r_data;
		dbuf_update_data(db);
	} else {
		dbuf_evict_user(db);
		db->db_ref = NULL;
		db->db.db_data = NULL;
		if (db->db_state != DB_NOFILL)
			db->db_state = DB_UNCACHED;
	}
}

/*
 * Expose an arc buffer for "external" access.  This will obtain
 * a new ref for the arc buffer to prevent it from being evicted
 * until the user is finished.  Note: this is for read access
 * only.  Returns a pointer to the arc buffer.
 */
arc_ref_t *
dbuf_hold_arcbuf(dmu_buf_impl_t *db)
{
	arc_ref_t *abuf;

	ASSERT(!refcount_is_zero(&db->db_holds));
	mutex_enter(&db->db_mtx);
	ASSERT(db->db_state == DB_CACHED);
	abuf = arc_loan_ref(db->db_ref);
	mutex_exit(&db->db_mtx);
	return (abuf);
}

uint64_t
dbuf_whichblock(dnode_t *dn, uint64_t offset)
{
	if (dn->dn_datablkshift) {
		return (offset >> dn->dn_datablkshift);
	} else {
		ASSERT3U(offset, <, dn->dn_datablksz);
		return (0);
	}
}

static void
dbuf_read_done(zio_t *zio, arc_ref_t *buf, void *vdb)
{
	dmu_buf_impl_t *db = vdb;

	mutex_enter(&db->db_mtx);
	ASSERT3U(db->db_state, ==, DB_READ);
	/*
	 * All reads are synchronous, so we must have a hold on the dbuf
	 */
	ASSERT(refcount_count(&db->db_holds) > 0);
	ASSERT(db->db_ref == NULL);
	ASSERT(db->db.db_data == NULL);
	if (db->db_freed_in_flight) {
		/* we were freed in flight; disregard any error */
		ASSERT(db->db_level == 0);
		db->db_freed_in_flight = FALSE;
		arc_free_ref(buf);
		buf = arc_hole_ref(db->db.db_size, DBUF_IS_METADATA(db));
		zio = NULL;
	}
	if (zio == NULL || zio->io_error == 0) {
		dbuf_set_data(db, buf);
		db->db_state = DB_CACHED;
	} else {
		ASSERT(db->db_blkid != DMU_BONUS_BLKID);
		ASSERT3P(db->db_ref, ==, NULL);
		arc_free_ref(buf);
		db->db_state = DB_UNCACHED;
	}
	cv_broadcast(&db->db_changed);
	dbuf_rele_and_unlock(db, (void *)dbuf_read_done);
}

static int
dbuf_read_impl(dnode_t *dn, dmu_buf_impl_t *db, zio_t *zio, uint32_t flags)
{
	spa_t *spa;
	zbookmark_t zb;
	arc_options_t aflags = ARC_OPT_REPORTCACHED;
	blkptr_t bp;
	int err;

	/*
	 * Note:  we don't need the dnode struct lock:
	 * 1. dnode_increase_indirection does not clear the old bps until
	 *    after it has moved all child dbufs.
	 * 2. arc_get_blkptr locking prevents changes to the bp, from
	 *    arc_make_writable or bp-rewrite, while it copies.
	 * XXX - we should really not allow any access to bps via db_data...
	 */
	ASSERT(!refcount_is_zero(&db->db_holds));
	ASSERT(MUTEX_HELD(&db->db_mtx));
	ASSERT(db->db_state == DB_UNCACHED);
	ASSERT(db->db_ref == NULL);

	if (db->db_blkid == DMU_BONUS_BLKID) {
		int bonuslen;

		dbuf_set_data(db, arc_alloc_ref(DN_MAX_BONUSLEN, B_TRUE, NULL));
		rw_enter(dn->dn_phys_rwlock, RW_READER);
		bonuslen = MIN(dn->dn_bonuslen, dn->dn_phys->dn_bonuslen);
		ASSERT3U(bonuslen, <=, db->db.db_size);
		if (bonuslen < DN_MAX_BONUSLEN)
			bzero(db->db.db_data, DN_MAX_BONUSLEN);
		if (bonuslen)
			bcopy(DN_BONUS(dn->dn_phys), db->db.db_data, bonuslen);
		rw_exit(dn->dn_phys_rwlock);
		db->db_state = DB_CACHED;
		arc_ref_freeze(db->db_ref);
		mutex_exit(&db->db_mtx);
		return (0);
	}

	if (db->db_blkoff == DBUF_PAST_EOF ||
	    (db->db_level == 0 && dnode_block_freed(dn, db->db_blkid))) {
		dbuf_set_data(db,
		    arc_hole_ref(db->db.db_size, DBUF_IS_METADATA(db)));
		db->db_state = DB_CACHED;
		mutex_exit(&db->db_mtx);
		return (EEXIST);
	}

	spa = dn->dn_objset->os_spa;

	db->db_state = DB_READ;

	if (!DBUF_IS_L2CACHEABLE(db))
		aflags |= ARC_OPT_NOL2CACHE;
	if (DBUF_IS_METADATA(db))
		aflags |= ARC_OPT_METADATA;

	SET_BOOKMARK(&zb, db->db_objset->os_dsl_dataset ?
	    db->db_objset->os_dsl_dataset->ds_object : DMU_META_OBJSET,
	    db->db.db_object, db->db_level, db->db_blkid);

	if (db->db_parent)
		arc_get_blkptr(db->db_parent->db_ref, db->db_blkoff, &bp);
	else
		arc_get_blkptr(db->db_objset->os_phys_buf, db->db_blkoff, &bp);

	mutex_exit(&db->db_mtx);

	/* keep this buffer around until the read completes */
	dbuf_add_ref(db, (void *)dbuf_read_done);

	err = dsl_read(zio, spa, &bp, db->db.db_size,
	    dbuf_read_done, db, ZIO_PRIORITY_SYNC_READ,
	    (flags & DB_RF_CANFAIL) ? ZIO_FLAG_CANFAIL : ZIO_FLAG_MUSTSUCCEED,
	    aflags, &zb);
	return (err);
}

int
dbuf_read(dmu_buf_impl_t *db, zio_t *zio, uint32_t flags)
{
	int err = 0;
	boolean_t havelock = (flags & DB_RF_HAVESTRUCT) != 0;
	boolean_t synchronous = (zio == NULL);
	boolean_t prefetch;
	dnode_t *dn;

	/*
	 * We don't have to hold the mutex to check db_state because it
	 * can't be freed while we have a hold on the buffer.
	 */
	ASSERT(!refcount_is_zero(&db->db_holds));

	if (db->db_state == DB_NOFILL)
		return (EIO);

	dn = DB_HOLD_DNODE(db);
	if (!havelock)
		rw_enter(&dn->dn_struct_rwlock, RW_READER);

	prefetch = db->db_level == 0 && db->db_blkid != DMU_BONUS_BLKID &&
	    (flags & DB_RF_NOPREFETCH) == 0 && DBUF_IS_CACHEABLE(db);

	mutex_enter(&db->db_mtx);
	if (db->db_state == DB_UNCACHED) {
		if (synchronous) {
			spa_t *spa = dn->dn_objset->os_spa;
			zio = zio_root(spa, NULL, NULL, ZIO_FLAG_CANFAIL);
		}
		err = dbuf_read_impl(dn, db, zio, flags);

		/* dbuf_read_impl has dropped db_mtx for us */

		if (prefetch)
			dmu_zfetch(&dn->dn_zfetch, db->db.db_offset,
			    db->db.db_size, err == EEXIST);
		if (!havelock)
			rw_exit(&dn->dn_struct_rwlock);
		DB_RELE_DNODE(db);
		if (synchronous)
			err = zio_wait(zio);
		else if (err == EEXIST)
			err = 0;
	} else {
		synchronous = db->db_state != DB_CACHED &&
		    (flags & DB_RF_NEVERWAIT) == 0;
		mutex_exit(&db->db_mtx);

		if (prefetch)
			dmu_zfetch(&dn->dn_zfetch, db->db.db_offset,
			    db->db.db_size, TRUE);
		if (!havelock)
			rw_exit(&dn->dn_struct_rwlock);
		DB_RELE_DNODE(db);
		if (synchronous) {
			mutex_enter(&db->db_mtx);
			while (db->db_state == DB_READ ||
			    db->db_state == DB_FILL) {
				ASSERT(db->db_state == DB_READ ||
				    (flags & DB_RF_HAVESTRUCT) == 0);
				cv_wait(&db->db_changed, &db->db_mtx);
			}
			if (db->db_state == DB_UNCACHED)
				err = EIO;
			mutex_exit(&db->db_mtx);
		}
	}

	ASSERT(err || !synchronous || db->db_state == DB_CACHED);
	return (err);
}

static void
dbuf_noread(dmu_buf_impl_t *db)
{
	ASSERT(!refcount_is_zero(&db->db_holds));
	ASSERT(db->db_blkid != DMU_BONUS_BLKID);
	mutex_enter(&db->db_mtx);
	while (db->db_state == DB_READ || db->db_state == DB_FILL)
		cv_wait(&db->db_changed, &db->db_mtx);
	if (db->db_state == DB_UNCACHED) {
		ASSERT(db->db_ref == NULL);
		ASSERT(db->db.db_data == NULL);
		dbuf_set_data(db,
		    arc_alloc_ref(db->db.db_size, DBUF_IS_METADATA(db), NULL));
		db->db_state = DB_FILL;
	} else if (db->db_state == DB_NOFILL) {
		dbuf_set_data(db, NULL);
	} else {
		ASSERT3U(db->db_state, ==, DB_CACHED);
	}
	mutex_exit(&db->db_mtx);
}

void
dbuf_unoverride(dbuf_dirty_record_t *dr)
{
	dmu_buf_impl_t *db = dr->dr_dbuf;
	blkptr_t *bp = &dr->dr_overridden_by;
	uint64_t txg = dr->dr_txg;

	ASSERT(MUTEX_HELD(&db->db_mtx));
	ASSERT(dr->dr_override_state != DR_IN_DMU_SYNC);
	ASSERT(db->db_level == 0);

	if (db->db_blkid == DMU_BONUS_BLKID ||
	    dr->dr_override_state == DR_NOT_OVERRIDDEN)
		return;
	/*
	 * Return this buffer to a "dirty" state.
	 */
	arc_make_writable(dr->dr_ref);
	if (db->db.db_data != db->db_ref->r_data)
		dbuf_set_data(db, dr->dr_ref);

	ASSERT(db->db_data_pending != dr);

	/* free the block we wrote */
	if (!BP_IS_HOLE(bp)) {
		dnode_t *dn = DB_HOLD_DNODE(db);
		zio_free(dn->dn_objset->os_spa, txg, bp);
		DB_RELE_DNODE(db);
	}
	dr->dr_override_state = DR_NOT_OVERRIDDEN;
}

/*
 * Called from dbuf_free_range() to void irrelevant dirty records.
 * Note: users must not place a hold on the dbuf before the call.
 */
static void
dbuf_undirty_last_dirty(dmu_buf_impl_t *db)
{
	dnode_t *dn;
	dbuf_dirty_record_t *dr = db->db_last_dirty;

	ASSERT(MUTEX_HELD(&db->db_mtx));
	ASSERT(db->db_level == 0);
	ASSERT(db->db_blkid != DMU_BONUS_BLKID);
	ASSERT(db->db_state == DB_CACHED);
	ASSERT(db->db_last_dirty != NULL);
	ASSERT(refcount_count(&db->db_holds) == db->db_dirtycnt);

	dprintf_dbuf(db, "size=%llx\n", (u_longlong_t)db->db.db_size);

	/* XXX would be nice to fix up dn_towrite_space[] */

	db->db_last_dirty = dr->dr_next;

	dn = DB_HOLD_DNODE(db);

	if (dr->dr_parent) {
		mutex_enter(&dr->dr_parent->dr_mtx);
		list_remove(&dr->dr_parent->dr_children, dr);
		mutex_exit(&dr->dr_parent->dr_mtx);
	} else if (db->db_level+1 == dn->dn_nlevels ||
	    db->db_blkid == DMU_SPILL_BLKID) {
		ASSERT(db->db_parent == NULL || db->db_parent == dn->dn_dbuf);
		mutex_enter(&dn->dn_mtx);
		list_remove(&dn->dn_dirty_records[dr->dr_txg & TXG_MASK], dr);
		mutex_exit(&dn->dn_mtx);
	}
	DB_RELE_DNODE(db);

	ASSERT(db->db_ref != NULL);
	ASSERT(dr->dr_ref == db->db_ref);
	if (refcount_remove(&db->db_holds, (void *)(uintptr_t)dr->dr_txg) > 0) {
		ASSERT(db->db_last_dirty != NULL);
		db->db_ref = NULL;
		dbuf_set_data(db, db->db_last_dirty->dr_ref);
	} else {
		ASSERT(db->db_last_dirty == NULL);
		dbuf_set_data(db, NULL);
	}
	ASSERT(arc_ref_anonymous(dr->dr_ref));
	arc_free_ref(dr->dr_ref);
	kmem_free(dr, sizeof (dbuf_dirty_record_t));

	ASSERT(db->db_dirtycnt != 0);
	db->db_dirtycnt -= 1;
}

/*
 * Evict (if its unreferenced) or clear (if its referenced) any level-0
 * data blocks in the free range, so that any future readers will find
 * empty blocks.  Also, if we happen accross any level-1 dbufs in the
 * range that have not already been marked dirty, mark them dirty so
 * they stay in memory.
 */
void
dbuf_free_range(dnode_t *dn, uint64_t start, uint64_t end, dmu_tx_t *tx)
{
	dmu_buf_impl_t *db, *db_next;
	uint64_t txg = tx->tx_txg;
	int epbs = dn->dn_indblkshift - SPA_BLKPTRSHIFT;
	uint64_t first_l1 = start >> epbs;
	uint64_t last_l1 = end >> epbs;

	if (end > dn->dn_maxblkid && end != DMU_SPILL_BLKID) {
		end = dn->dn_maxblkid;
		last_l1 = end >> epbs;
	}
	dprintf_dnode(dn, "start=%llu end=%llu\n", start, end);
	mutex_enter(&dn->dn_dbufs_mtx);
	for (db = list_head(&dn->dn_dbufs); db; db = db_next) {
		db_next = list_next(&dn->dn_dbufs, db);
		ASSERT(db->db_blkid != DMU_BONUS_BLKID);

		if (db->db_level == 1 &&
		    db->db_blkid >= first_l1 && db->db_blkid <= last_l1) {
			mutex_enter(&db->db_mtx);
			if (db->db_last_dirty &&
			    db->db_last_dirty->dr_txg != txg) {
				dbuf_add_ref(db, FTAG);
				mutex_exit(&db->db_mtx);
				dbuf_will_dirty(db, tx);
				dbuf_rele(db, FTAG);
			} else {
				mutex_exit(&db->db_mtx);
			}
		}

		if (db->db_level != 0 ||
		    db->db_blkid < start || db->db_blkid > end)
			continue;
		dprintf_dbuf(db, "found buf %s\n", "");

		/* found a level 0 buffer in the range */
		mutex_enter(&db->db_mtx);
		if (db->db_state == DB_READ) {
			/* will be handled in dbuf_read_done */
			db->db_freed_in_flight = TRUE;
			mutex_exit(&db->db_mtx);
			continue;
		}
		if (db->db.db_data == NULL) {
			ASSERT(db->db_state != DB_CACHED);
			mutex_exit(&db->db_mtx);
			continue;
		}

		/* if this buffer is active,  wait for the activity to cease */
		if (!db->db_managed && refcount_count(&db->db_holds) >
		    db->db_dirtycnt + db->db_writers_waiting &&
		    (db->db_last_dirty == NULL ||
		    db->db_last_dirty->dr_txg != txg)) {
			(void) refcount_add(&db->db_holds, FTAG);
			db->db_writers_waiting += 1;
			while (refcount_count(&db->db_holds) >
			    db->db_dirtycnt + db->db_writers_waiting &&
			    (db->db_last_dirty == NULL ||
			    db->db_last_dirty->dr_txg != txg))
				cv_wait(&db->db_changed, &db->db_mtx);
			db->db_writers_waiting -= 1;
			(void) refcount_remove(&db->db_holds, FTAG);
		}

		if (db->db_last_dirty && db->db_last_dirty->dr_txg == txg) {
			dbuf_dirty_record_t *dr = db->db_last_dirty;
			ASSERT(db->db_ref == dr->dr_ref);
			dbuf_unoverride(dr);
			if (refcount_count(&db->db_holds) > db->db_dirtycnt) {
				/*
				 * This buffer is active in open context,
				 * zero out the contents and re-adjust the
				 * file size to reflect that this buffer may
				 * contain new data when we sync.
				 */
				ASSERT(db->db_blkid != DMU_SPILL_BLKID);
				arc_ref_thaw(db->db_ref);
				bzero(db->db.db_data, db->db.db_size);
				mutex_exit(&db->db_mtx);
				mutex_enter(&dn->dn_mtx);
				dnode_clear_range(dn, db->db_blkid, 1, tx);
				if (db->db_blkid > dn->dn_maxblkid)
					dn->dn_maxblkid = db->db_blkid;
				mutex_exit(&dn->dn_mtx);
				continue;
			} else {
				dbuf_undirty_last_dirty(db);
			}
		}

		if (refcount_is_zero(&db->db_holds)) {
			dmu_buf_impl_t marker = {0};
			/*
			 * Dbuf is not referenced, lets evict it.
			 * Note: dbuf_evict() could evict multiple dbufs, so
			 * leave a marker so we know where to resume.
			 */
			list_insert_after(&dn->dn_dbufs, db, &marker);
			dbuf_evict(db);
			db_next = list_next(&dn->dn_dbufs, &marker);
			list_remove(&dn->dn_dbufs, &marker);
			continue;
		}

		if (!arc_ref_hole(db->db_ref)) {
			/*
			 * Otherwise, set dbuf contents to the "empty" ref.
			 */
			if (db->db_last_dirty == NULL) {
				ASSERT(db->db_managed);
				arc_free_ref(db->db_ref);
				db->db_ref = NULL;
			}
			dbuf_set_data(db,
			    arc_hole_ref(db->db.db_size, DBUF_IS_METADATA(db)));
		}
		mutex_exit(&db->db_mtx);
	}
	mutex_exit(&dn->dn_dbufs_mtx);
}

int
dbuf_block_freeable(dmu_buf_impl_t *db, uint64_t *birth)
{
	dsl_dataset_t *ds = db->db_objset->os_dsl_dataset;
	uint64_t space;

	ASSERT(MUTEX_HELD(&db->db_mtx));

	if (birth == NULL)
		birth = &space;

	if (db->db_last_dirty)
		*birth = db->db_last_dirty->dr_txg;
	else
		*birth = db->db_birth;

	/* If we don't exist then we can't be freed */
	if (*birth == 0)
		return (FALSE);

	/* MOS data is always freeable */
	if (DMU_OS_IS_MOS(db->db_objset))
		return (TRUE);

	/*
	 * Check to see if we are in a snapshot
	 * Note, we don't pass a bp here because we are holding the
	 * db_mtx and might deadlock if we try to prefetch dedup data.
	 */
	return (dsl_dataset_block_freeable(ds, NULL, *birth));
}

void
dbuf_new_size(dmu_buf_impl_t *db, int size, dmu_tx_t *tx)
{
	arc_ref_t *buf, *obuf;
	int osize = db->db.db_size;
	dnode_t *dn;

	ASSERT(db->db_blkid != DMU_BONUS_BLKID);

	dn = DB_HOLD_DNODE(db);

	/* this lock prevents new holds while we operate */
	ASSERT(RW_WRITE_HELD(&dn->dn_struct_rwlock));

	/*
	 * This call to dbuf_will_dirty() with the dn_struct_rwlock held
	 * is OK, because there can be no other references to the db
	 * when we are changing its size, so no concurrent DB_FILL can
	 * be happening.
	 */
	/*
	 * XXX we should be doing a dbuf_read, checking the return
	 * value and returning that up to our callers
	 */
	dbuf_will_dirty(db, tx);

	/* create the data buffer for the new block */
	buf = arc_alloc_ref(size, DBUF_IS_METADATA(db), NULL);
	arc_make_writable(buf);

	/* copy old block data to the new block */
	obuf = db->db_ref;
	bcopy(obuf->r_data, buf->r_data, MIN(osize, size));
	/* zero the remainder */
	if (size > osize)
		bzero((uint8_t *)buf->r_data + osize, size - osize);

	mutex_enter(&db->db_mtx);
	dbuf_set_data(db, buf);
	arc_free_ref(obuf);
	db->db.db_size = size;

	if (db->db_level == 0) {
		ASSERT3U(db->db_last_dirty->dr_txg, ==, tx->tx_txg);
		db->db_last_dirty->dr_ref = buf;
	}
	mutex_exit(&db->db_mtx);

	dnode_willuse_space(dn, size-osize, tx);
	DB_RELE_DNODE(db);
}

void
dbuf_make_indirect_writable(dmu_buf_impl_t *db)
{
	ASSERT(db->db_parent == NULL ||
	    arc_ref_writable(db->db_parent->db_ref));

	/*
	 * XXX - bprewrite will need something like:
	 * objset_t *os;
	 *
	 * os = DB_HOLD_DNODE(db)->db_objset;
	 * ASSERT(dsl_pool_sync_context(dmu_objset_pool(os)));
	 * ASSERT(arc_ref_writable(os->os_phys_buf) ||
	 *  list_link_active(&os->os_dsl_dataset->ds_synced_link));
	 * zb.zb_objset = os->os_dsl_dataset ?
	 *  os->os_dsl_dataset->ds_object : 0;
	 * DB_RELE_DNODE(db);
	 * zb.zb_object = db->db.db_object;
	 * zb.zb_level = db->db_level;
	 * zb.zb_blkid = db->db_blkid;
	 * arc_validate_ref(db->db_ref, db->db_parent, db->db_blkoff,
	 *  os->os_spa, &zb);
	 */
	arc_make_writable(db->db_ref);
	if (db->db_ref->r_data != db->db.db_data)
		db->db.db_data = db->db_ref->r_data;
}

static dbuf_dirty_record_t *
dbuf_dirty_impl(dmu_buf_impl_t *db, arc_ref_t *ref, dmu_tx_t *tx)
{
	dnode_t *dn;
	objset_t *os;
	dbuf_dirty_record_t **drp, *dr;
	int drop_struct_lock = FALSE;
	int txgoff = tx->tx_txg & TXG_MASK;
	boolean_t prefetch_ddt = B_FALSE;
	blkptr_t bp;

	ASSERT(tx->tx_txg != 0);
	ASSERT(!refcount_is_zero(&db->db_holds));
	DMU_TX_DIRTY_BUF(tx, db);

	dn = DB_HOLD_DNODE(db);
	os = dn->dn_objset;
	/*
	 * Shouldn't dirty a regular buffer in syncing context unless we
	 * are initializing a new objset.
	 */
	ASSERT(!dmu_tx_is_syncing(tx) || os->os_initializing ||
	    DMU_OBJECT_IS_SPECIAL(dn->dn_object) || DMU_OS_IS_MOS(os));
	/*
	 * We make this assert for private objects as well, but after we
	 * check if we're already dirty.  They are allowed to re-dirty
	 * in syncing context.
	 */
	ASSERT(dn->dn_object == DMU_META_DNODE_OBJECT ||
	    dn->dn_dirtyctx == DN_UNDIRTIED || dn->dn_dirtyctx ==
	    (dmu_tx_is_syncing(tx) ? DN_DIRTY_SYNC : DN_DIRTY_OPEN));

	mutex_enter(&db->db_mtx);
	if (ref) {
		while (db->db_state == DB_READ || db->db_state == DB_FILL)
			cv_wait(&db->db_changed, &db->db_mtx);
		if (db->db_state == DB_UNCACHED)
			db->db_state = DB_FILL;
	}

	ASSERT(db->db_state == DB_CACHED || db->db_state == DB_FILL ||
	    db->db_state == DB_NOFILL);

	mutex_enter(&dn->dn_mtx);
	/*
	 * Don't set dirtyctx to SYNC if we're just modifying this as we
	 * initialize the objset.
	 */
	if (dn->dn_dirtyctx == DN_UNDIRTIED && !os->os_initializing) {
		dn->dn_dirtyctx =
		    (dmu_tx_is_syncing(tx) ? DN_DIRTY_SYNC : DN_DIRTY_OPEN);
		ASSERT(dn->dn_dirtyctx_firstset == NULL);
		dn->dn_dirtyctx_firstset = kmem_alloc(1, KM_SLEEP);
	}
	mutex_exit(&dn->dn_mtx);

	if (db->db_blkid == DMU_SPILL_BLKID)
		dn->dn_have_spill = B_TRUE;
again:
	/*
	 * If this buffer is already dirty, we're done.
	 */
	drp = &db->db_last_dirty;
	ASSERT(*drp == NULL || (*drp)->dr_txg <= tx->tx_txg ||
	    db->db.db_object == DMU_META_DNODE_OBJECT);
	while ((dr = *drp) != NULL && dr->dr_txg > tx->tx_txg)
		drp = &dr->dr_next;
	if (dr && dr->dr_txg == tx->tx_txg) {
		DB_RELE_DNODE(db);

		if (db->db_level == 0 && db->db_state != DB_NOFILL &&
		    db->db.db_object != DMU_META_DNODE_OBJECT) {
			/*
			 * Make sure the buffer is still writable.
			 */
			dbuf_unoverride(dr);
			arc_ref_thaw(db->db_ref);
			if (ref) {
				bcopy(ref->r_data, db->db.db_data,
				    db->db.db_size);
				arc_free_ref(ref);
				xuio_stat_wbuf_copied();
				ref = NULL;
			}
		}
		ASSERT(dr->dr_override_state == DR_NOT_OVERRIDDEN);
		ASSERT(ref == NULL);
		mutex_exit(&db->db_mtx);
		return (dr);
	}

	/*
	 * Wait for any active readers to drop their holds on this
	 * dbuf before we dirty it.  This prevents the readers db_data
	 * address from changing unexpectedly.
	 */
	/* XXX - set db_managed if level > 0 or object mos/special? */
	if (!db->db_managed && db->db_level == 0 && db->db_state != DB_FILL &&
	    !DMU_OS_IS_MOS(os) && !DMU_OBJECT_IS_SPECIAL(dn->dn_object)) {
		boolean_t retry = B_FALSE;

		db->db_writers_waiting += 1;
		while (refcount_count(&db->db_holds) >
		    db->db_dirtycnt + db->db_writers_waiting) {
			retry = B_TRUE;
			cv_wait(&db->db_changed, &db->db_mtx);
		}
		db->db_writers_waiting -= 1;
		if (retry)
			goto again;
	}

	ASSERT3U(dn->dn_nlevels, >, db->db_level);

	/*
	 * We should only be dirtying in syncing context if it's the
	 * mos or we're initializing the os or it's a special object.
	 * Note, however, we are allowed to dirty in syncing context
	 * provided we already dirtied in open context.
	 */
	ASSERT(!dmu_tx_is_syncing(tx) || DMU_OS_IS_MOS(os) ||
	    os->os_initializing || DMU_OBJECT_IS_SPECIAL(dn->dn_object));
	ASSERT(db->db.db_size != 0);

	dprintf_dbuf(db, "size=%llx\n", (u_longlong_t)db->db.db_size);

	if (db->db_blkid != DMU_BONUS_BLKID) {
		/*
		 * Update the inflight accounting.
		 */
		dnode_willuse_space(dn, db->db.db_size, tx);
		if (dbuf_block_freeable(db, NULL)) {
			/*
			 * If this block is already dirty we can't predict
			 * its size on disk, so just assume the dbuf size.
			 * Otherwise we will account using the actual PSIZE.
			 */
			if (db->db_last_dirty) {
				dnode_willuse_space(dn, -db->db.db_size, tx);
			} else {
				arc_ref_t *pref = db->db_parent ?
				    db->db_parent->db_ref : os->os_phys_buf;
				arc_get_blkptr(pref, db->db_blkoff, &bp);
				dnode_willuse_space(dn, -BP_GET_PSIZE(&bp), tx);
				if (BP_GET_DEDUP(&bp))
					dsl_pool_willuse_space(tx->tx_pool,
					    2 * SPA_DDTBLOCKSIZE, tx);
				prefetch_ddt = TRUE;
			}
		}
	}

	dr = kmem_zalloc(sizeof (dbuf_dirty_record_t), KM_SLEEP);
	dr->dr_dbuf = db;
	dr->dr_txg = tx->tx_txg;
	dr->dr_next = *drp;
	*drp = dr;
	if (db->db_level == 0) {
		if (db->db_state != DB_NOFILL &&
		    db->db.db_object != DMU_META_DNODE_OBJECT) {
			/*
			 * If this buffer is dirty in a previous transaction
			 * group make a copy of it so that the changes in this
			 * transaction group won't leak out when we sync the
			 * older txg.
			 */
			if (dr->dr_next && dr->dr_next->dr_ref == db->db_ref) {
				if (ref)
					db->db_ref = ref;
				else
					db->db_ref = arc_clone_ref(db->db_ref);
			} else if (ref) {
					if (db->db_ref)
						arc_free_ref(db->db_ref);
					db->db_ref = ref;
					db->db_state = DB_CACHED;
			}
			/*
			 * Bonus buffers are never written (they are copied
			 * into the dnode phys), so just thaw them for update.
			 */
			if (db->db_blkid == DMU_BONUS_BLKID)
				arc_ref_thaw(db->db_ref);
			else
				arc_make_writable(db->db_ref);
			if (db->db.db_data != db->db_ref->r_data)
				dbuf_set_data(db, db->db_ref);
			if (ref) {
				ref = NULL;
				xuio_stat_wbuf_nocopy();
			}
		}
	} else {
		mutex_init(&dr->dr_mtx, NULL, MUTEX_DEFAULT, NULL);
		list_create(&dr->dr_children,
		    sizeof (dbuf_dirty_record_t),
		    offsetof(dbuf_dirty_record_t, dr_dirty_link));
	}
	dr->dr_ref = db->db_ref;
	ASSERT(ref == NULL);

	/* clear this block from any current free ranges */
	if (db->db_level == 0 && db->db_blkid != DMU_BONUS_BLKID &&
	    db->db_blkid != DMU_SPILL_BLKID) {
		mutex_enter(&dn->dn_mtx);
		dnode_clear_range(dn, db->db_blkid, 1, tx);
		mutex_exit(&dn->dn_mtx);
	}

	/*
	 * This buffer is now part of this txg
	 */
	dbuf_add_ref(db, (void *)(uintptr_t)tx->tx_txg);
	db->db_dirtycnt += 1;
	ASSERT3U(db->db_dirtycnt, <=, 3);

	if (db->db_writers_waiting)
		cv_broadcast(&db->db_changed);
	mutex_exit(&db->db_mtx);

	if (db->db_blkid == DMU_BONUS_BLKID ||
	    db->db_blkid == DMU_SPILL_BLKID) {
		mutex_enter(&dn->dn_mtx);
		ASSERT(!list_link_active(&dr->dr_dirty_link));
		list_insert_tail(&dn->dn_dirty_records[txgoff], dr);
		mutex_exit(&dn->dn_mtx);
		dnode_setdirty(dn, tx);
		DB_RELE_DNODE(db);
		return (dr);
	}

	if (prefetch_ddt)
		ddt_prefetch(os->os_spa, &bp);

	if (!RW_WRITE_HELD(&dn->dn_struct_rwlock)) {
		rw_enter(&dn->dn_struct_rwlock, RW_READER);
		drop_struct_lock = TRUE;
	}

	if (db->db_level == 0) {
		dnode_new_blkid(dn, db->db_blkid, tx, drop_struct_lock);
		ASSERT(dn->dn_maxblkid >= db->db_blkid);
	}

	if (db->db_level+1 < dn->dn_nlevels) {
		dmu_buf_impl_t *parent = db->db_parent;
		dbuf_dirty_record_t *di;
		int parent_held = FALSE;

		if (parent == NULL || parent == dn->dn_dbuf) {
			int epbs = dn->dn_indblkshift - SPA_BLKPTRSHIFT;

			parent = dbuf_hold_level(dn, db->db_level+1,
			    db->db_blkid >> epbs, FTAG);
			ASSERT(parent != NULL);
			parent_held = TRUE;
			(void) dbuf_read(parent, NULL, DB_RF_MUST_SUCCEED |
			    DB_RF_HAVESTRUCT | DB_RF_NOPREFETCH);
		}
		if (drop_struct_lock)
			rw_exit(&dn->dn_struct_rwlock);
		ASSERT3U(db->db_level+1, ==, parent->db_level);
		di = dbuf_dirty(parent, tx);
		if (parent_held)
			dbuf_rele(parent, FTAG);

		mutex_enter(&db->db_mtx);
		/*  possible race with dbuf_undirty_last_dirty() */
		if (db->db_last_dirty == dr ||
		    dn->dn_object == DMU_META_DNODE_OBJECT) {
			mutex_enter(&di->dr_mtx);
			ASSERT3U(di->dr_txg, ==, tx->tx_txg);
			ASSERT(!list_link_active(&dr->dr_dirty_link));
			list_insert_tail(&di->dr_children, dr);
			mutex_exit(&di->dr_mtx);
			dr->dr_parent = di;
		}
		mutex_exit(&db->db_mtx);
	} else {
		ASSERT(db->db_level+1 == dn->dn_nlevels);
		ASSERT(db->db_blkid < dn->dn_nblkptr);
		ASSERT(db->db_parent == NULL || db->db_parent == dn->dn_dbuf);
		mutex_enter(&dn->dn_mtx);
		ASSERT(!list_link_active(&dr->dr_dirty_link));
		list_insert_tail(&dn->dn_dirty_records[txgoff], dr);
		mutex_exit(&dn->dn_mtx);
		if (drop_struct_lock)
			rw_exit(&dn->dn_struct_rwlock);
	}

	dnode_setdirty(dn, tx);
	DB_RELE_DNODE(db);
	return (dr);
}

dbuf_dirty_record_t *
dbuf_dirty(dmu_buf_impl_t *db, dmu_tx_t *tx)
{
	return (dbuf_dirty_impl(db, NULL, tx));
}


#pragma weak dmu_buf_will_dirty = dbuf_will_dirty
void
dbuf_will_dirty(dmu_buf_impl_t *db, dmu_tx_t *tx)
{
	int rf = DB_RF_MUST_SUCCEED | DB_RF_NOPREFETCH;
	dnode_t *dn;

	ASSERT(tx->tx_txg != 0);
	ASSERT(!refcount_is_zero(&db->db_holds));

	dn = DB_HOLD_DNODE(db);
	if (RW_WRITE_HELD(&dn->dn_struct_rwlock))
		rf |= DB_RF_HAVESTRUCT;
	DB_RELE_DNODE(db);
	(void) dbuf_read(db, NULL, rf);
	(void) dbuf_dirty(db, tx);
}

void
dmu_buf_will_not_fill(dmu_buf_t *db_fake, dmu_tx_t *tx)
{
	dmu_buf_impl_t *db = (dmu_buf_impl_t *)db_fake;

	db->db_state = DB_NOFILL;

	dmu_buf_will_fill(db_fake, tx);
}

void
dmu_buf_will_fill(dmu_buf_t *db_fake, dmu_tx_t *tx)
{
	dmu_buf_impl_t *db = (dmu_buf_impl_t *)db_fake;

	ASSERT(db->db_blkid != DMU_BONUS_BLKID);
	ASSERT(tx->tx_txg != 0);
	ASSERT(db->db_level == 0);
	ASSERT(!refcount_is_zero(&db->db_holds));

	ASSERT(db->db.db_object != DMU_META_DNODE_OBJECT ||
	    dmu_tx_private_ok(tx));

	dbuf_noread(db);
	(void) dbuf_dirty(db, tx);
}

#pragma weak dmu_buf_fill_done = dbuf_fill_done
/* ARGSUSED */
void
dbuf_fill_done(dmu_buf_impl_t *db, dmu_tx_t *tx)
{
	mutex_enter(&db->db_mtx);
	DBUF_VERIFY(db);

	if (db->db_state == DB_FILL) {
		db->db_state = DB_CACHED;
		cv_broadcast(&db->db_changed);
	}
	mutex_exit(&db->db_mtx);
}

/*
 * Directly assign a provided arc buf to a given dbuf if it's not referenced
 * by anybody except our caller. Otherwise copy arcbuf's contents to dbuf.
 */
void
dbuf_assign_arcbuf(dmu_buf_impl_t *db, arc_ref_t *buf, dmu_tx_t *tx)
{
	ASSERT(!refcount_is_zero(&db->db_holds));
	ASSERT(db->db_blkid != DMU_BONUS_BLKID);
	ASSERT(!DBUF_IS_METADATA(db));
	ASSERT(arc_buf_size(buf) == db->db.db_size);

	arc_return_ref(buf);
	ASSERT(arc_ref_writable(buf));
	(void) dbuf_dirty_impl(db, buf, tx);
}

/*
 * This will mark the dbuf EVICTING and clear (most of) its references.
 * For callers from the DMU we will usually see:
 *	dbuf_evict()->arc_evict_ref()+dbuf_destroy()
 * From the arc callback, we will usually see:
 * 	dbuf_do_evict()->dbuf_evict()->dbuf_destroy()
 * Sometimes, though, we will get a mix of these two:
 *	DMU: dbuf_evict()->arc_evict_ref()
 *	ARC: dbuf_do_evict()->dbuf_destroy()
 */
void
dbuf_evict(dmu_buf_impl_t *db)
{
	dmu_buf_impl_t *parent = db->db_parent;
	dnode_t *dn;
	int err = 0;

	ASSERT(MUTEX_HELD(&db->db_mtx));
	ASSERT(refcount_is_zero(&db->db_holds));

	dbuf_evict_user(db);

	dn = DB_HOLD_DNODE(db);
	if (db->db_parent == dn->dn_dbuf)
		parent = NULL;
	else
		parent = db->db_parent;
	DB_RELE_DNODE(db);

	if (db->db_blkid == DMU_BONUS_BLKID) {
		db->db_dnode_handle = NULL;
	} else if (!list_link_active(&db->db_link)) {
		/*
		 * Note that we don't need to grab the dnode handle because
		 * we have an "active" hold in the form of this dbuf.
		 */
		dnode_rele(DB_DNODE(db), db);
		db->db_dnode_handle = NULL;
	}

	ASSERT(db->db_data_pending == NULL);
	db->db.db_data = NULL;
	db->db_state = DB_EVICTING;
	db->db_parent = NULL;

	if (db->db_ref && (err = arc_evict_ref(db->db_ref)) == 0)
		db->db_ref = NULL;

	mutex_exit(&db->db_mtx);

	/*
	 * If this dbuf is referenced from an indirect dbuf,
	 * decrement the ref count on the indirect dbuf.
	 */
	if (parent)
		dbuf_rele(parent, db);

	/* A destroy request may already have been initiated from the ARC */
	if (err != EINPROGRESS)
		dbuf_destroy(db);
}

static int
dbuf_findoff(dnode_t *dn, int level, uint64_t blkid, int fail_sparse,
    dmu_buf_impl_t **dbp, uint64_t *off)
{
	int nlevels, epbs;
	uint64_t maxblkid;
	boolean_t hasspill;

	*dbp = NULL;
	*off = 0;

	ASSERT(blkid != DMU_BONUS_BLKID);
	ASSERT(RW_LOCK_HELD(&dn->dn_struct_rwlock));

	rw_enter(dn->dn_phys_rwlock, RW_READER);
	nlevels = dn->dn_phys->dn_nlevels;
	if (nlevels == 0)
		nlevels = 1;
	epbs = dn->dn_indblkshift - SPA_BLKPTRSHIFT;
	maxblkid = dn->dn_phys->dn_maxblkid >> (level * epbs);
	hasspill = (dn->dn_phys->dn_flags & DNODE_FLAG_SPILL_BLKPTR);
	rw_exit(dn->dn_phys_rwlock);

	ASSERT3U(level * epbs, <, 64);
	if (blkid == DMU_SPILL_BLKID) {
		if (!hasspill) {
			*off = DBUF_PAST_EOF;
			if (fail_sparse)
				return (ENOENT);
		} else {
			*off = DN_SPILL_OFFSET(dn);
		}
		ASSERT(dn->dn_dbuf != NULL);
		dbuf_add_ref(dn->dn_dbuf, NULL);
		*dbp = dn->dn_dbuf;
	} else if (level >= nlevels || blkid > maxblkid) {
		/* the buffer has no parent yet */
		*off = DBUF_PAST_EOF;
		if (fail_sparse)
			return (ENOENT);
	} else if (fail_sparse && level == 0 && dnode_block_freed(dn, blkid)) {
		return (ENOENT);
	} else if (level < nlevels-1) {
		/* this block is referenced from an indirect block */
		dmu_buf_impl_t *pdb;
		int32_t index = blkid & ((1 << epbs) - 1);
		int32_t flags =
		    DB_RF_HAVESTRUCT | DB_RF_NOPREFETCH | DB_RF_CANFAIL;
		int err = dbuf_hold_impl(dn, level+1,
		    blkid >> epbs, fail_sparse, NULL, &pdb);
		if (err)
			return (err);
		err = dbuf_read(pdb, NULL, flags);
		if (err == 0 && fail_sparse) {
			/* XXX - check dnode_block_freed()? */
			mutex_enter(&pdb->db_mtx);
			if (BP_IS_HOLE(((blkptr_t *)pdb->db.db_data) + index))
				err = ENOENT;
			mutex_exit(&pdb->db_mtx);
		}
		if (err) {
			dbuf_rele(pdb, NULL);
			return (err);
		}
		*dbp = pdb;
		*off = index << SPA_BLKPTRSHIFT;
	} else {
		/* the block is referenced from the dnode */
		ASSERT3U(level, ==, nlevels-1);
		rw_enter(dn->dn_phys_rwlock, RW_READER);
		ASSERT(dn->dn_phys->dn_nblkptr == 0 ||
		    blkid < dn->dn_phys->dn_nblkptr);
		if (fail_sparse && BP_IS_HOLE(&dn->dn_phys->dn_blkptr[blkid])) {
			/* XXX - check dnode_block_freed()? */
			rw_exit(dn->dn_phys_rwlock);
			return (ENOENT);
		}
		if (dn->dn_dbuf) {
			dbuf_add_ref(dn->dn_dbuf, NULL);
			*dbp = dn->dn_dbuf;
		}
		*off = DN_BLKPTR_OFFSET(dn, blkid);
		rw_exit(dn->dn_phys_rwlock);
	}
	ASSERT(*dbp == NULL ||
	    *off < (*dbp)->db.db_size || *off == DBUF_PAST_EOF);
	return (0);
}

static dmu_buf_impl_t *
dbuf_create(dnode_t *dn, uint8_t level, uint64_t blkid,
    dmu_buf_impl_t *parent, uint64_t blkoff)
{
	objset_t *os = dn->dn_objset;
	dmu_buf_impl_t *db, *odb;

	ASSERT(RW_LOCK_HELD(&dn->dn_struct_rwlock));
	ASSERT(dn->dn_type != DMU_OT_NONE);

	db = kmem_cache_alloc(dbuf_cache, KM_SLEEP);

	db->db_objset = os;
	db->db.db_object = dn->dn_object;
	db->db_level = level;
	db->db_blkid = blkid;
	db->db_last_dirty = NULL;
	db->db_dirtycnt = 0;
	db->db_writers_waiting = 0;
	db->db_dnode_handle = dn->dn_handle;
	db->db_parent = parent;
	db->db_blkoff = blkoff;

	db->db_user_ptr = NULL;
	db->db_user_data_ptr_ptr = NULL;
	db->db_evict_func = NULL;
	db->db_immediate_evict = 0;
	db->db_freed_in_flight = 0;

	if (blkid == DMU_BONUS_BLKID) {
		ASSERT3P(parent, ==, dn->dn_dbuf);
		db->db.db_size = DN_MAX_BONUSLEN -
		    (dn->dn_nblkptr-1) * sizeof (blkptr_t);
		ASSERT3U(db->db.db_size, >=, dn->dn_bonuslen);
		db->db.db_offset = DMU_BONUS_BLKID;
		db->db_birth = 0;
		db->db_state = DB_UNCACHED;
		/* the bonus dbuf is not placed in the hash table */
		arc_space_consume(sizeof (dmu_buf_impl_t), ARC_SPACE_OTHER);
		return (db);
	} else if (blkid == DMU_SPILL_BLKID) {
		if (blkoff != DBUF_PAST_EOF)  {
			blkptr_t bp;
			arc_get_blkptr(parent->db_ref, blkoff, &bp);
			db->db.db_size = BP_GET_LSIZE(&bp);
		} else {
			db->db.db_size = SPA_MINBLOCKSIZE;
		}
		db->db.db_offset = DMU_SPILL_BLKID;
	} else {
		int blocksize =
		    db->db_level ? 1<<dn->dn_indblkshift :  dn->dn_datablksz;
		db->db.db_size = blocksize;
		db->db.db_offset = db->db_blkid * blocksize;
	}

	if (parent && db->db_blkoff != DBUF_PAST_EOF) {
		blkptr_t *bp;

		mutex_enter(&parent->db_mtx);
		bp = (blkptr_t *)((char *)(parent->db.db_data) + db->db_blkoff);
		db->db_birth = bp->blk_birth;
		mutex_exit(&parent->db_mtx);
	} else {
		db->db_birth = 0;
	}

	/*
	 * Hold the dn_dbufs_mtx while we get the new dbuf
	 * in the hash table *and* added to the dbufs list.
	 * This prevents a possible deadlock with someone
	 * trying to look up this dbuf before its added to the
	 * dn_dbufs list.
	 */
	mutex_enter(&dn->dn_dbufs_mtx);
	db->db_state = DB_EVICTING;
	if ((odb = dbuf_hash_insert(db)) != NULL) {
		/* someone else inserted it first */
		kmem_cache_free(dbuf_cache, db);
		mutex_exit(&dn->dn_dbufs_mtx);
		return (odb);
	}
	ASSERT(!list_link_active(&db->db_link));
	ASSERT(dn->dn_object == DMU_META_DNODE_OBJECT ||
	    refcount_count(&dn->dn_holds) > 0);

	list_insert_head(&dn->dn_dbufs, db);
	(void) refcount_add(&dn->dn_holds, db);
	dn->dn_dbufs_count += 1;
	db->db_state = DB_UNCACHED;
	mutex_exit(&dn->dn_dbufs_mtx);

	arc_space_consume(sizeof (dmu_buf_impl_t), ARC_SPACE_OTHER);

	if (parent && parent != dn->dn_dbuf)
		dbuf_add_ref(parent, db);

	dprintf_dbuf(db, "db=%p\n", db);

	return (db);
}

/*
 * This is the callback routine for the ARC to let the DMU know that
 * it has evicted the data associated with this dbuf, and so this
 * dbuf should also now be evicted.
 * NOTE: this function "works" because we set DB_EVICTING before
 * removing it from the hash table (so play well with dbuf_find())
 * and we obtain the dn_dbufs_mtx before unlinking it from the
 * dnode (so play well with dnode_evict_dbufs()).
 */
static void
dbuf_do_evict(void *private)
{
	dmu_buf_impl_t *db = private;

	mutex_enter(&arc_evict_interlock);
	mutex_enter(&db->db_mtx);

	ASSERT(refcount_is_zero(&db->db_holds));

	if (db->db_state != DB_EVICTING) {
		dnode_t *dn;

		ASSERT(db->db_state == DB_CACHED);
		DBUF_VERIFY(db);
		dn = DB_HOLD_DNODE(db);
		(void) dnode_add_ref(dn, FTAG);
		mutex_exit(&db->db_mtx);
		mutex_enter(&dn->dn_dbufs_mtx);
		mutex_enter(&db->db_mtx);
		db->db_ref = NULL;
		if (db->db_state == DB_EVICTING) {
			/* we got evicted while waiting for the dn_dbufs mtx */
			ASSERT(db->db_parent == NULL);
			mutex_exit(&db->db_mtx);
			dbuf_destroy(db);
		} else {
			dmu_buf_impl_t *parent = db->db_parent;
			db->db_state = DB_EVICTING;
			if (parent && parent != dn->dn_dbuf) {
				db->db_parent = NULL;
				mutex_exit(&db->db_mtx);
				dbuf_rele(parent, db);
				mutex_enter(&db->db_mtx);
			}
			if (list_link_active(&db->db_link)) {
				list_remove(&dn->dn_dbufs, db);
				dn->dn_dbufs_count -= 1;
			}
			dbuf_evict(db);
		}
		mutex_exit(&dn->dn_dbufs_mtx);
		DN_RELEASE_HANDLE(dn->dn_handle);
		dnode_rele(dn, FTAG);
	} else {
		db->db_ref = NULL;
		mutex_exit(&db->db_mtx);
		dbuf_destroy(db);
	}
	mutex_exit(&arc_evict_interlock);
}

static void
dbuf_destroy(dmu_buf_impl_t *db)
{
	ASSERT(refcount_is_zero(&db->db_holds));
	ASSERT(db->db_parent == NULL);

	if (db->db_blkid != DMU_BONUS_BLKID)
		dbuf_hash_remove(db);

	/*
	 * If this dbuf is still on the dn_dbufs list,
	 * remove it from that list.
	 */
	if (db->db_dnode_handle != NULL) {
		dnode_t *dn;
		boolean_t uselock;

		ASSERT(list_link_active(&db->db_link));

		dn = DB_HOLD_DNODE(db);
		uselock = !MUTEX_HELD(&dn->dn_dbufs_mtx);
		if (uselock)
			mutex_enter(&dn->dn_dbufs_mtx);
		list_remove(&dn->dn_dbufs, db);
		dn->dn_dbufs_count -= 1;
		if (uselock)
			mutex_exit(&dn->dn_dbufs_mtx);
		DB_RELE_DNODE(db);
		dnode_rele(dn, db);
		db->db_dnode_handle = NULL;
	}

	ASSERT(db->db_ref == NULL);
	ASSERT(!list_link_active(&db->db_link));
	ASSERT(db->db.db_data == NULL);
	ASSERT(db->db_hash_next == NULL);
	ASSERT(db->db_data_pending == NULL);

	kmem_cache_free(dbuf_cache, db);
	arc_space_return(sizeof (dmu_buf_impl_t), ARC_SPACE_OTHER);
}

void
dbuf_prefetch(dnode_t *dn, uint64_t blkid)
{
	dmu_buf_impl_t *db;
	uint64_t off;

	ASSERT(blkid != DMU_BONUS_BLKID);
	ASSERT(RW_LOCK_HELD(&dn->dn_struct_rwlock));

	if (dnode_block_freed(dn, blkid))
		return;

	/* dbuf_find() returns with db_mtx held */
	if (db = dbuf_find(dn, 0, blkid)) {
		/*
		 * This dbuf is already in the cache.  We assume that
		 * it is already CACHED, or else about to be either
		 * read or filled.
		 */
		mutex_exit(&db->db_mtx);
		return;
	}

	if (dbuf_findoff(dn, 0, blkid, TRUE, &db, &off) == 0) {
		blkptr_t bp;
		int priority = dn->dn_type == DMU_OT_DDT_ZAP ?
		    ZIO_PRIORITY_DDT_PREFETCH : ZIO_PRIORITY_ASYNC_READ;
		dsl_dataset_t *ds = dn->dn_objset->os_dsl_dataset;
		zio_t *zio;
		zbookmark_t zb;

		SET_BOOKMARK(&zb, ds ? ds->ds_object : DMU_META_OBJSET,
		    dn->dn_object, 0, blkid);

		if (db)
			arc_get_blkptr(db->db_ref, off, &bp);
		else
			arc_get_blkptr(dn->dn_objset->os_phys_buf, off, &bp);

		zio = zio_root(dn->dn_objset->os_spa,
		    NULL, NULL, ZIO_FLAG_CANFAIL);
		(void) dsl_read(zio, dn->dn_objset->os_spa, &bp,
		    dn->dn_datablksz, NULL, NULL, priority,
		    ZIO_FLAG_CANFAIL | ZIO_FLAG_SPECULATIVE, 0, &zb);
		zio_nowait(zio);
		if (db)
			dbuf_rele(db, NULL);
		}
}

/*
 * Returns with db_holds incremented, and db_mtx not held.
 * Note: dn_struct_rwlock must be held.
 */
int
dbuf_hold_impl(dnode_t *dn, uint8_t level, uint64_t blkid, int fail_sparse,
    void *tag, dmu_buf_impl_t **dbp)
{
	dmu_buf_impl_t *db, *parent = NULL;

	ASSERT(blkid != DMU_BONUS_BLKID);
	ASSERT(RW_LOCK_HELD(&dn->dn_struct_rwlock));
	ASSERT3U(dn->dn_nlevels, >, level);

	*dbp = NULL;
retry:
	/* dbuf_find() returns with db_mtx held */
	if ((db = dbuf_find(dn, level, blkid)) == NULL) {
		uint64_t off;
		int err;

		ASSERT3P(parent, ==, NULL);
		err = dbuf_findoff(dn, level, blkid, fail_sparse,
		    &parent, &off);
		if (err)
			return (err);
		/* dbuf_create() returns with db_mtx held */
		db = dbuf_create(dn, level, blkid, parent, off);
	}

	if (db->db_ref && refcount_is_zero(&db->db_holds)) {
		if (arc_reactivate_ref(db->db_ref) == ENOENT) {
			dbuf_evict(db);
			if (parent) {
				dbuf_rele(parent, NULL);
				parent = NULL;
			}
			goto retry;
		} else if (db->db.db_data != db->db_ref->r_data) {
			db->db.db_data = db->db_ref->r_data;
		}
	}

	ASSERT(db->db_ref == NULL || arc_ref_active(db->db_ref));

	(void) refcount_add(&db->db_holds, tag);
	dbuf_update_data(db);
	DBUF_VERIFY(db);
	mutex_exit(&db->db_mtx);

	/* NOTE: we can't rele the parent until after we drop the db_mtx */
	if (parent)
		dbuf_rele(parent, NULL);

	ASSERT3P(DB_DNODE(db), ==, dn);
	ASSERT3U(db->db_blkid, ==, blkid);
	ASSERT3U(db->db_level, ==, level);
	*dbp = db;

	return (0);
}

dmu_buf_impl_t *
dbuf_hold(dnode_t *dn, uint64_t blkid, void *tag)
{
	dmu_buf_impl_t *db;
	int err = dbuf_hold_impl(dn, 0, blkid, FALSE, tag, &db);
	return (err ? NULL : db);
}

dmu_buf_impl_t *
dbuf_hold_level(dnode_t *dn, int level, uint64_t blkid, void *tag)
{
	dmu_buf_impl_t *db;
	int err = dbuf_hold_impl(dn, level, blkid, FALSE, tag, &db);
	return (err ? NULL : db);
}

void
dbuf_create_bonus(dnode_t *dn)
{
	ASSERT(RW_WRITE_HELD(&dn->dn_struct_rwlock));

	ASSERT(dn->dn_bonus == NULL);
	dn->dn_bonus = dbuf_create(dn, 0, DMU_BONUS_BLKID, dn->dn_dbuf, NULL);
}

void
dbuf_spill_set_blksz(dmu_buf_t *db_fake, uint64_t blksz, dmu_tx_t *tx)
{
	dmu_buf_impl_t *db = (dmu_buf_impl_t *)db_fake;
	dnode_t *dn;


	ASSERT(db->db_blkid == DMU_SPILL_BLKID);
	if (blksz == 0)
		blksz = SPA_MINBLOCKSIZE;
	if (blksz > SPA_MAXBLOCKSIZE)
		blksz = SPA_MAXBLOCKSIZE;
	else
		blksz = P2ROUNDUP(blksz, SPA_MINBLOCKSIZE);

	dn = DB_HOLD_DNODE(db);
	rw_enter(&dn->dn_struct_rwlock, RW_WRITER);
	dbuf_new_size(db, blksz, tx);
	rw_exit(&dn->dn_struct_rwlock);
	DB_RELE_DNODE(db);
}

#pragma weak dmu_buf_add_ref = dbuf_add_ref
void
dbuf_add_ref(dmu_buf_impl_t *db, void *tag)
{
	int64_t holds = refcount_add(&db->db_holds, tag);
	ASSERT(holds > 1);
}

/*
 * If you call dbuf_rele() you had better not be referencing the dnode handle
 * unless you have some other direct or indirect hold on the dnode. (An indirect
 * hold is a hold on one of the dnode's dbufs, including the bonus buffer.)
 * Without that, the dbuf_rele() could lead to a dnode_rele() followed by the
 * dnode's parent dbuf evicting its dnode handles.
 */
#pragma weak dmu_buf_rele = dbuf_rele
void
dbuf_rele(dmu_buf_impl_t *db, void *tag)
{
	mutex_enter(&db->db_mtx);
	dbuf_rele_and_unlock(db, tag);
}

/*
 * XXX - ugh, consider introducing: dbuf_dirty_rele() ==
 *		dbuf_rele_impl(db, B_TRUE, tag);
 *	and then dbuf_rele() ==
 *		dbuf_rele_impl(db, B_FALSE, tag);
 *	and so dbuf_rele_impl() ==
 *		mutex_enter();
 *		if (dirty_hold)
 *			db_dirtycnt--;
 *		...
 */

/*
 * dbuf_rele() for an already-locked dbuf.  This is necessary to allow
 * db_dirtycnt and db_holds to be updated atomically.
 */
void
dbuf_rele_and_unlock(dmu_buf_impl_t *db, void *tag)
{
	int64_t holds;

	ASSERT(MUTEX_HELD(&db->db_mtx));
	DBUF_VERIFY(db);

	/*
	 * Remove the reference to the dbuf before removing its hold on the
	 * dnode so we can guarantee in dnode_move() that a referenced bonus
	 * buffer has a corresponding dnode hold.
	 */
	holds = refcount_remove(&db->db_holds, tag);
	ASSERT(holds >= 0);

	/*
	 * We can't freeze indirects if they are dirty, there is a
	 * possibility they may be modified in the current syncing context.
	 */
	if (db->db_ref && holds == (db->db_level == 0 ? db->db_dirtycnt : 0))
		arc_ref_freeze(db->db_ref);

	if (db->db_level == 0 &&
	    holds == db->db_dirtycnt && db->db_immediate_evict)
		dbuf_evict_user(db);

	if (holds == 0) {
		if (db->db_blkid == DMU_BONUS_BLKID) {
			dnode_t *dn;

			mutex_exit(&db->db_mtx);
			dn = DB_HOLD_DNODE(db);
			mutex_enter(&dn->dn_dbufs_mtx);
			dn->dn_dbufs_count -= 1;
			mutex_exit(&dn->dn_dbufs_mtx);
			/*
			 * Now don't need the handle, the dnode cannot move
			 * until we rele our bonus hold.
			 */
			DB_RELE_DNODE(db);
			dnode_rele(DB_DNODE(db), db);
		} else if (db->db_ref == NULL) {
			/*
			 * This is a special case: we never associated this
			 * dbuf with any data allocated from the ARC (or
			 * it was freed).
			 */
			ASSERT(db->db_state == DB_UNCACHED ||
			    db->db_state == DB_NOFILL);
			dbuf_evict(db);
		} else if (arc_ref_anonymous(db->db_ref)) {
			/*
			 * This must be a SPILL block that was byte swapped.
			 * We can't let the arc manage the eviction of this
			 * dbuf since the arc buf is anonymous, so we just
			 * free the arc buf and evict.
			 */
			ASSERT(db->db_blkid == DMU_SPILL_BLKID);
			arc_free_ref(db->db_ref);
			db->db_ref = NULL;
			dbuf_evict(db);
		} else {
			ASSERT(!arc_ref_anonymous(db->db_ref));
			arc_inactivate_ref(db->db_ref, dbuf_do_evict, db);
			if (!DBUF_IS_CACHEABLE(db))
				dbuf_evict(db);
			else
				mutex_exit(&db->db_mtx);
		}
	} else {
		if (db->db_writers_waiting)
			cv_broadcast(&db->db_changed);
		mutex_exit(&db->db_mtx);
	}
}

#pragma weak dmu_buf_refcount = dbuf_refcount
uint64_t
dbuf_refcount(dmu_buf_impl_t *db)
{
	return (refcount_count(&db->db_holds));
}

void *
dmu_buf_set_user(dmu_buf_t *db_fake, void *user_ptr, void *user_data_ptr_ptr,
    dmu_buf_evict_func_t *evict_func)
{
	return (dmu_buf_update_user(db_fake, NULL, user_ptr,
	    user_data_ptr_ptr, evict_func));
}

void *
dmu_buf_set_user_ie(dmu_buf_t *db_fake, void *user_ptr, void *user_data_ptr_ptr,
    dmu_buf_evict_func_t *evict_func)
{
	dmu_buf_impl_t *db = (dmu_buf_impl_t *)db_fake;

	db->db_immediate_evict = TRUE;
	return (dmu_buf_update_user(db_fake, NULL, user_ptr,
	    user_data_ptr_ptr, evict_func));
}

void *
dmu_buf_update_user(dmu_buf_t *db_fake, void *old_user_ptr, void *user_ptr,
    void *user_data_ptr_ptr, dmu_buf_evict_func_t *evict_func)
{
	dmu_buf_impl_t *db = (dmu_buf_impl_t *)db_fake;
	ASSERT(db->db_level == 0);

	ASSERT((user_ptr == NULL) == (evict_func == NULL));

	mutex_enter(&db->db_mtx);

	if (db->db_user_ptr == old_user_ptr) {
		db->db_user_ptr = user_ptr;
		db->db_user_data_ptr_ptr = user_data_ptr_ptr;
		db->db_evict_func = evict_func;

		dbuf_update_data(db);
	} else {
		old_user_ptr = db->db_user_ptr;
	}

	mutex_exit(&db->db_mtx);
	return (old_user_ptr);
}

void *
dmu_buf_get_user(dmu_buf_t *db_fake)
{
	dmu_buf_impl_t *db = (dmu_buf_impl_t *)db_fake;
	ASSERT(!refcount_is_zero(&db->db_holds));

	return (db->db_user_ptr);
}

/*
 * When a buffer is allocated at a time when there is no available
 * blkptr (e.g., way past end of file), then we must wait until sync
 * context to locate the blkptr.
 */
static void
dbuf_check_blkptr(dnode_t *dn, dmu_buf_impl_t *db)
{
	/* Note: no need for the dn_phys_rwlock when the dnode is dirty */
	ASSERT(MUTEX_HELD(&db->db_mtx));
	ASSERT(dn->dn_dbuf == NULL || arc_ref_writable(dn->dn_dbuf->db_ref));

	if (db->db_blkid == DMU_SPILL_BLKID) {
		ASSERT(db->db_parent == dn->dn_dbuf);
		if ((dn->dn_phys->dn_flags & DNODE_FLAG_SPILL_BLKPTR) == 0) {
			mutex_enter(&dn->dn_mtx);
			BP_ZERO(&dn->dn_phys->dn_spill);
			dn->dn_phys->dn_flags |= DNODE_FLAG_SPILL_BLKPTR;
			mutex_exit(&dn->dn_mtx);
		}
		if (db->db_blkoff == DBUF_PAST_EOF)
			db->db_blkoff = DN_SPILL_OFFSET(dn);
		ASSERT(db->db_blkoff == DN_SPILL_OFFSET(dn));
	} else if (db->db_level == dn->dn_phys->dn_nlevels-1) {
		/*
		 * This block is referenced from the dnode.
		 */
		ASSERT(db->db_blkoff > 0);
		if (db->db_blkoff == DBUF_PAST_EOF) {
			ASSERT(db->db_blkid < dn->dn_phys->dn_nblkptr);
			ASSERT(db->db_parent == NULL);
			db->db_parent = dn->dn_dbuf;
			db->db_blkoff = DN_BLKPTR_OFFSET(dn, db->db_blkid);
		}
		ASSERT(db->db_blkoff == DN_BLKPTR_OFFSET(dn, db->db_blkid));
	} else {
		/*
		 * This block is referenced from an indirect block
		 */
		ASSERT(dn->dn_phys->dn_nlevels > 1);
		if (db->db_parent == NULL) {
			dmu_buf_impl_t *parent;
			int epbs =
			    dn->dn_phys->dn_indblkshift - SPA_BLKPTRSHIFT;

			ASSERT(db->db_blkoff == DBUF_PAST_EOF);
			mutex_exit(&db->db_mtx);
			rw_enter(&dn->dn_struct_rwlock, RW_READER);
			parent = dbuf_hold_level(dn, db->db_level+1,
			    db->db_blkid >> epbs, db);
			ASSERT(parent != NULL);
			rw_exit(&dn->dn_struct_rwlock);
			mutex_enter(&db->db_mtx);
			db->db_parent = parent;
			db->db_blkoff = DB_BLKPTR_OFFSET(db);
		}
		ASSERT(db->db_blkoff == DB_BLKPTR_OFFSET(db));
	}
	DBUF_VERIFY(db);
}

static void
dbuf_sync_indirect(dbuf_dirty_record_t *dr, dmu_tx_t *tx)
{
	dmu_buf_impl_t *db = dr->dr_dbuf;
	dnode_t *dn;
	zio_t *zio;

	ASSERT(dmu_tx_is_syncing(tx));

	mutex_enter(&db->db_mtx);

	ASSERT(db->db_level > 0);
	DBUF_VERIFY(db);

	ASSERT(db->db_ref != NULL);
	ASSERT3U(db->db_state, ==, DB_CACHED);


	dn = DB_HOLD_DNODE(db);
	ASSERT3U(db->db.db_size, ==, 1<<dn->dn_phys->dn_indblkshift);
	dbuf_check_blkptr(dn, db);
	DB_RELE_DNODE(db);

	db->db_data_pending = dr;

	/*
	 * Indirect buffers are made writable here rather
	 * than in dbuf_dirty() since they are only modified
	 * in syncing context and we don't want the overhead
	 * of making multiple copies of the data.
	 */
	dbuf_make_indirect_writable(db);

	mutex_exit(&db->db_mtx);

	dbuf_write(dr, db->db_ref, tx);

	zio = dr->dr_zio;
	mutex_enter(&dr->dr_mtx);
	dbuf_sync_list(&dr->dr_children, tx);
	ASSERT(list_head(&dr->dr_children) == NULL);
	mutex_exit(&dr->dr_mtx);
	zio_nowait(zio);
}

static void
dbuf_sync_leaf(dbuf_dirty_record_t *dr, dmu_tx_t *tx)
{
	dmu_buf_impl_t *db = dr->dr_dbuf;
	dnode_t *dn;
	uint64_t txg = tx->tx_txg;

	ASSERT(dmu_tx_is_syncing(tx));

	mutex_enter(&db->db_mtx);

	ASSERT(db->db_state == DB_CACHED || db->db_state == DB_NOFILL);
	DBUF_VERIFY(db);

	dn = DB_HOLD_DNODE(db);

	/*
	 * If this is a bonus buffer, simply copy the bonus data into the
	 * dnode.  It will be written out when the dnode is synced (and it
	 * will be synced, since it must have been dirty for dbuf_sync to
	 * be called).
	 */
	if (db->db_blkid == DMU_BONUS_BLKID) {
		dbuf_dirty_record_t **drp;
		int bonuslen;

		ASSERT(dr->dr_ref != NULL);
		ASSERT3U(db->db_level, ==, 0);
		/* we should not be accounting this block as pending io */
		ASSERT(!arc_ref_iopending(dr->dr_ref));

		bonuslen = dn->dn_phys->dn_bonuslen;
		DB_RELE_DNODE(db);

		ASSERT3U(bonuslen, <=, DN_MAX_BONUSLEN);
		bcopy(dr->dr_ref->r_data, DN_BONUS(dn->dn_phys), bonuslen);

		if (dr->dr_ref != db->db_ref)
			arc_free_ref(dr->dr_ref);
		db->db_data_pending = NULL;
		drp = &db->db_last_dirty;
		while (*drp != dr)
			drp = &(*drp)->dr_next;
		ASSERT(dr->dr_next == NULL);
		*drp = dr->dr_next;
		kmem_free(dr, sizeof (dbuf_dirty_record_t));
		ASSERT(db->db_dirtycnt > 0);
		db->db_dirtycnt -= 1;
		dbuf_rele_and_unlock(db, (void *)(uintptr_t)txg);
		return;
	}

	/*
	 * This function may have dropped the db_mtx lock allowing a dmu_sync
	 * operation to sneak in. As a result, we need to ensure that we
	 * don't check the dr_override_state until we have returned from
	 * dbuf_check_blkptr.
	 * XXX - no longer an issue with current dmu_sync() design?
	 */
	dbuf_check_blkptr(dn, db);

	/*
	 * If this buffer is in the middle of an immediate write,
	 * wait for the synchronous IO to complete.
	 */
	while (dr->dr_override_state == DR_IN_DMU_SYNC) {
		ASSERT(dn->dn_object != DMU_META_DNODE_OBJECT);
		cv_wait(&db->db_changed, &db->db_mtx);
		ASSERT(dr->dr_override_state != DR_NOT_OVERRIDDEN);
	}

	if (db->db_state != DB_NOFILL &&
	    dn->dn_object == DMU_META_DNODE_OBJECT) {
		/*
		 * This is a block of dnodes, we need to make the phys
		 * buffer writable prior to updating the dnode contents
		 * later in dnode_sync().
		 * NOTE: This may have a side-effect of changing the
		 * dn_phys pointers of the dnodes.
		 */
		ASSERT(dr->dr_ref == db->db_ref);
		dnode_buf_will_dirty(&db->db);
	}
	db->db_data_pending = dr;

	mutex_exit(&db->db_mtx);

	dbuf_write(dr, dr->dr_ref, tx);

	ASSERT(!list_link_active(&dr->dr_dirty_link));
	if (dn->dn_object == DMU_META_DNODE_OBJECT) {
		list_insert_tail(&dn->dn_dirty_records[txg&TXG_MASK], dr);
		DB_RELE_DNODE(db);
	} else {
		/*
		 * Although zio_nowait() does not "wait for an IO", it does
		 * initiate the IO. If this is an empty write it seems plausible
		 * that the IO could actually be completed before the nowait
		 * returns. We need to DB_RELE_DNODE() first in case
		 * zio_nowait() invalidates the dbuf.
		 */
		DB_RELE_DNODE(db);
		zio_nowait(dr->dr_zio);
	}
}

void
dbuf_sync_list(list_t *list, dmu_tx_t *tx)
{
	dbuf_dirty_record_t *dr;

	while (dr = list_head(list)) {
		if (dr->dr_zio != NULL) {
			/*
			 * If we find an already initialized zio then we
			 * are processing the meta-dnode, and we have finished.
			 * The dbufs for all dnodes are put back on the list
			 * during processing, so that we can zio_wait()
			 * these IOs after initiating all child IOs.
			 */
			ASSERT3U(dr->dr_dbuf->db.db_object, ==,
			    DMU_META_DNODE_OBJECT);
			break;
		}
		list_remove(list, dr);
		if (dr->dr_dbuf->db_level > 0)
			dbuf_sync_indirect(dr, tx);
		else
			dbuf_sync_leaf(dr, tx);
	}
}

/* ARGSUSED */
static void
dbuf_write_ready(zio_t *zio)
{
	dmu_buf_impl_t *db = zio->io_private;
	dnode_t *dn;
	blkptr_t *bp = zio->io_bp;
	blkptr_t *bp_orig = &zio->io_bp_orig;
	spa_t *spa = zio->io_spa;
	int64_t delta;
	uint64_t fill = 0;
	int i;

	/*
	 * NOTE: we don't need the dn_phys_rwlock here for dn_phys
	 * since we are dirty, so our phys block pointer cannot be
	 * changing.
	 */

	dn = DB_HOLD_DNODE(db);
	delta = bp_get_dsize_sync(spa, bp) - bp_get_dsize_sync(spa, bp_orig);
	dnode_diduse_space(dn, delta - zio->io_prev_space_delta);
	zio->io_prev_space_delta = delta;

	if (BP_IS_HOLE(bp)) {
		ASSERT(bp->blk_fill == 0);
		DB_RELE_DNODE(db);
		return;
	}

	ASSERT((db->db_blkid != DMU_SPILL_BLKID &&
	    BP_GET_TYPE(bp) == dn->dn_type) ||
	    (db->db_blkid == DMU_SPILL_BLKID &&
	    BP_GET_TYPE(bp) == dn->dn_bonustype));
	ASSERT(BP_GET_LEVEL(bp) == db->db_level);

	mutex_enter(&db->db_mtx);

	if (db->db_level == 0) {
		mutex_enter(&dn->dn_mtx);
		if (db->db_blkid > dn->dn_phys->dn_maxblkid &&
		    db->db_blkid != DMU_SPILL_BLKID)
			dn->dn_phys->dn_maxblkid = db->db_blkid;
		mutex_exit(&dn->dn_mtx);

		if (dn->dn_type == DMU_OT_DNODE) {
			dnode_phys_t *dnp = db->db.db_data;
			for (i = db->db.db_size >> DNODE_SHIFT; i > 0;
			    i--, dnp++) {
				if (dnp->dn_type != DMU_OT_NONE)
					fill++;
			}
		} else {
			fill = 1;
		}
	} else {
		blkptr_t *ibp = db->db.db_data;
		ASSERT3U(db->db.db_size, ==, 1<<dn->dn_phys->dn_indblkshift);
		for (i = db->db.db_size >> SPA_BLKPTRSHIFT; i > 0; i--, ibp++) {
			if (BP_IS_HOLE(ibp))
				continue;
			fill += ibp->blk_fill;
		}
	}
	DB_RELE_DNODE(db);

	bp->blk_fill = fill;

	mutex_exit(&db->db_mtx);
}

static void
dbuf_write_done(zio_t *zio)
{
	dmu_buf_impl_t *db = zio->io_private;
	blkptr_t *bp = zio->io_bp;
	blkptr_t *bp_orig = &zio->io_bp_orig;
	uint64_t txg = zio->io_txg;
	dbuf_dirty_record_t **drp, *dr;

	ASSERT3U(zio->io_error, ==, 0);

	if (zio->io_flags & ZIO_FLAG_IO_REWRITE) {
		ASSERT(BP_EQUAL(bp, bp_orig));
	} else {
		dsl_dataset_t *ds;
		dmu_tx_t *tx;
		dnode_t *dn;

		dn = DB_HOLD_DNODE(db);
		ds = dn->dn_objset->os_dsl_dataset;
		tx = dn->dn_objset->os_synctx;
		DB_RELE_DNODE(db);

		(void) dsl_dataset_block_kill(ds, bp_orig, tx, B_TRUE);
		dsl_dataset_block_born(ds, bp, tx);
	}

	mutex_enter(&db->db_mtx);

	DBUF_VERIFY(db);

	drp = &db->db_last_dirty;
	while ((dr = *drp) != db->db_data_pending)
		drp = &dr->dr_next;
	ASSERT(!list_link_active(&dr->dr_dirty_link));
	ASSERT(dr->dr_txg == txg);
	ASSERT(dr->dr_dbuf == db);
	ASSERT(dr->dr_next == NULL);
	*drp = dr->dr_next;

	if (db->db_level == 0) {
		ASSERT(db->db_blkid != DMU_BONUS_BLKID);
		ASSERT(dr->dr_override_state == DR_NOT_OVERRIDDEN);
		if (db->db_state != DB_NOFILL) {
			if (dr->dr_ref != db->db_ref)
				arc_free_ref(dr->dr_ref);
		}
	} else {
		dnode_t *dn;

		dn = DB_HOLD_DNODE(db);
		ASSERT(list_head(&dr->dr_children) == NULL);
		ASSERT3U(db->db.db_size, ==, 1<<dn->dn_phys->dn_indblkshift);
		mutex_destroy(&dr->dr_mtx);
		list_destroy(&dr->dr_children);
		DB_RELE_DNODE(db);
	}
	kmem_free(dr, sizeof (dbuf_dirty_record_t));

	cv_broadcast(&db->db_changed);
	ASSERT(db->db_dirtycnt > 0);
	db->db_dirtycnt -= 1;
	db->db_data_pending = NULL;
	db->db_birth = bp->blk_birth;
	dbuf_rele_and_unlock(db, (void *)(uintptr_t)txg);
}

static void
dbuf_write_override_done(zio_t *zio)
{
	dmu_buf_impl_t *db = zio->io_private;
	dbuf_dirty_record_t *dr = db->db_data_pending;
	blkptr_t *obp = &dr->dr_overridden_by;

	mutex_enter(&db->db_mtx);
	if (!BP_EQUAL(zio->io_bp, obp) && !BP_IS_HOLE(obp)) {
		/*
		 * The override block deduped away, so free it up and
		 * give the ref the new identity.
		 */
		dsl_free(spa_get_dsl(zio->io_spa), zio->io_txg, obp);
		arc_ref_new_id(dr->dr_ref, zio->io_spa, zio->io_bp);
		ASSERT(db->db.db_data == db->db_ref->r_data);
	}
	mutex_exit(&db->db_mtx);

	dbuf_write_done(zio);
}

static void
dbuf_write(dbuf_dirty_record_t *dr, arc_ref_t *data, dmu_tx_t *tx)
{
	dmu_buf_impl_t *db = dr->dr_dbuf;
	dmu_buf_impl_t *parent = db->db_parent;
	dnode_t *dn;
	objset_t *os;
	uint64_t txg = tx->tx_txg;
	blkptr_t *bp;
	zbookmark_t zb;
	zio_prop_t zp;
	zio_t *zio;
	int wp_flag = 0;

	dn = DB_HOLD_DNODE(db);
	os = dn->dn_objset;

	/* bp buffer had better be writable */
	ASSERT((parent == NULL && arc_ref_writable(os->os_phys_buf)) ||
	    arc_ref_writable(parent->db_ref));

	if (parent != dn->dn_dbuf) {
		ASSERT(parent->db_data_pending);
		ASSERT(db->db_level == parent->db_level-1);
		int mask = (parent->db.db_size >> SPA_BLKPTRSHIFT) - 1;
		zio = parent->db_data_pending->dr_zio;
		bp = &((blkptr_t *)parent->db.db_data)[db->db_blkid & mask];
	} else if (db->db_blkid == DMU_SPILL_BLKID) {
		ASSERT(db->db_level == 0);
		zio = dn->dn_zio;
		bp = &dn->dn_phys->dn_spill;
	} else {
		ASSERT(db->db_level == dn->dn_phys->dn_nlevels-1);
		ASSERT3P(db->db_blkoff, ==, DN_BLKPTR_OFFSET(dn, db->db_blkid));
		zio = dn->dn_zio;
		bp = &dn->dn_phys->dn_blkptr[db->db_blkid];
	}

	ASSERT(db->db_level == 0 || data == db->db_ref);
	ASSERT3U(bp->blk_birth, <=, txg);
	ASSERT(zio);

	SET_BOOKMARK(&zb, os->os_dsl_dataset ?
	    os->os_dsl_dataset->ds_object : DMU_META_OBJSET,
	    db->db.db_object, db->db_level, db->db_blkid);

	if (db->db_blkid == DMU_SPILL_BLKID)
		wp_flag = WP_SPILL;
	wp_flag |= (db->db_state == DB_NOFILL) ? WP_NOFILL : 0;

	dmu_write_policy(os, dn, db->db_level, wp_flag, &zp);
	DB_RELE_DNODE(db);

	if (dr->dr_override_state == DR_OVERRIDDEN) {
		ASSERT(db->db_level == 0);
		ASSERT(db->db_state != DB_NOFILL);
		dr->dr_zio = zio_write(zio, os->os_spa, txg,
		    bp, data->r_data, arc_buf_size(data), &zp,
		    dbuf_write_ready, dbuf_write_override_done, db,
		    ZIO_PRIORITY_ASYNC_WRITE, ZIO_FLAG_MUSTSUCCEED, &zb);
		mutex_enter(&db->db_mtx);
		dr->dr_override_state = DR_NOT_OVERRIDDEN;
		zio_write_override(dr->dr_zio, &dr->dr_overridden_by,
		    dr->dr_copies);
		mutex_exit(&db->db_mtx);
	} else if (db->db_state == DB_NOFILL) {
		ASSERT(zp.zp_checksum == ZIO_CHECKSUM_OFF);
		dr->dr_zio = zio_write(zio, os->os_spa, txg,
		    bp, NULL, db->db.db_size, &zp,
		    dbuf_write_ready, dbuf_write_done, db,
		    ZIO_PRIORITY_ASYNC_WRITE,
		    ZIO_FLAG_MUSTSUCCEED | ZIO_FLAG_NODATA, &zb);
	} else {
		ASSERT(arc_ref_anonymous(data));
		dr->dr_zio = arc_write(zio, os->os_spa, txg,
		    bp, data, DBUF_IS_L2CACHEABLE(db), &zp,
		    dbuf_write_ready, dbuf_write_done, db,
		    ZIO_PRIORITY_ASYNC_WRITE, ZIO_FLAG_MUSTSUCCEED, &zb);
	}
}
