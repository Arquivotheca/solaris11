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

#ifndef _SYS_DMU_IMPL_H
#define	_SYS_DMU_IMPL_H

#include <sys/txg_impl.h>
#include <sys/zio.h>
#include <sys/dnode.h>
#include <sys/zfs_context.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * This is the locking strategy for the DMU.  Numbers in parenthesis are
 * cases that use that lock order, referenced below:
 *
 * ARC is self-contained
 * bplist is self-contained
 * refcount is self-contained
 * txg is self-contained (hopefully!)
 * zst_lock
 * zf_rwlock
 *
 * XXX try to improve evicting path?
 *
 * dp_config_rwlock > os_obj_lock > dn_struct_rwlock >
 * 	dn_dbufs_mtx > hash_mutexes > db_mtx > dd_lock > leafs
 *
 * dp_config_rwlock
 *    must be held before: everything
 *    protects dd namespace changes
 *    protects property changes globally
 *    held from:
 *    	dsl_dir_open/r:
 *    	dsl_dir_create_sync/w:
 *    	dsl_dir_sync_destroy/w:
 *    	dsl_dir_rename_sync/w:
 *    	dsl_prop_changed_notify/r:
 *
 * os_obj_lock
 *   must be held before:
 *   	everything except dp_config_rwlock
 *   protects os_obj_next
 *   held from:
 *   	dmu_object_alloc: dn_dbufs_mtx, db_mtx, hash_mutexes, dn_struct_rwlock
 *
 * dn_struct_rwlock
 *   must be held before:
 *   	everything except dp_config_rwlock and os_obj_lock
 *   protects structure of dnode (eg. nlevels)
 *   	dn_maxblkid
 *   	dn_nlevels
 *   	dn_*blksz*
 *   	phys nlevels, maxblkid, physical blkptr_t's (?)
 *   held from:
 *   	callers of dbuf_read_impl, dbuf_hold[_impl], dbuf_prefetch
 *   	dmu_object_info_from_dnode: dn_dirty_mtx (dn_datablksz)
 *   	dmu_tx_count_free:
 *   	dbuf_read_impl: db_mtx, dmu_zfetch()
 *   	dmu_zfetch: zf_rwlock/r, zst_lock, dbuf_prefetch()
 *   	dbuf_new_size: db_mtx
 *   	dbuf_dirty: db_mtx
 *	dbuf_findbp: (callers, phys? - the real need)
 *	dbuf_create: dn_dbufs_mtx, hash_mutexes, db_mtx (phys?)
 *	dbuf_prefetch: dn_dirty_mtx, hash_mutexes, db_mtx, dn_dbufs_mtx
 *	dbuf_hold_impl: hash_mutexes, db_mtx, dn_dbufs_mtx, dbuf_findbp()
 *	dnode_sync/w (increase_indirection): db_mtx (phys)
 *	dnode_set_blksz/w: dn_dbufs_mtx (dn_*blksz*)
 *	dnode_new_blkid/w: (dn_maxblkid)
 *	dnode_free_range/w: dn_dirty_mtx (dn_maxblkid)
 *	dnode_next_offset: (phys)
 *
 * dn_dbufs_mtx
 *    must be held before:
 *    	db_mtx, hash_mutexes
 *    protects:
 *    	dn_dbufs
 *    	dn_evicted
 *    held from:
 *    	dmu_evict_user: db_mtx (dn_dbufs)
 *    	dbuf_free_range: db_mtx (dn_dbufs)
 *    	dbuf_remove_ref: db_mtx, callees:
 *    		dbuf_hash_remove: hash_mutexes, db_mtx
 *    	dbuf_create: hash_mutexes, db_mtx (dn_dbufs)
 *    	dnode_set_blksz: (dn_dbufs)
 *
 * hash_mutexes (global)
 *   must be held before:
 *   	db_mtx
 *   protects dbuf_hash_table (global) and db_hash_next
 *   held from:
 *   	dbuf_find: db_mtx
 *   	dbuf_hash_insert: db_mtx
 *   	dbuf_hash_remove: db_mtx
 *
 * db_mtx (meta-leaf)
 *   must be held before:
 *   	dn_mtx, dn_dirty_mtx, dd_lock (leaf mutexes)
 *   protects:
 *   	db_state
 * 	db_holds
 * 	db_buf
 * 	db_changed
 * 	db_data_pending
 * 	db_dirtied
 * 	db_link
 * 	db_dirty_node (??)
 * 	db_dirtycnt
 * 	db_d.*
 * 	db.*
 *   held from:
 * 	dbuf_dirty: dn_mtx, dn_dirty_mtx
 * 	dbuf_dirty->dsl_dir_willuse_space: dd_lock
 * 	dbuf_dirty->dbuf_new_block->dsl_dataset_block_freeable: dd_lock
 * 	dbuf_undirty_last_dirty: dn_dirty_mtx (db_d)
 * 	dbuf_write_done: dn_dirty_mtx (db_state)
 * 	dbuf_*
 * 	dmu_buf_update_user: none (db_d)
 * 	dmu_evict_user: none (db_d) (maybe can eliminate)
 *   	dbuf_find: none (db_holds)
 *   	dbuf_hash_insert: none (db_holds)
 *   	dmu_buf_read_array_impl: none (db_state, db_changed)
 *   	dmu_sync: none (db_dirty_node, db_d)
 *   	dnode_reallocate: none (db)
 *
 * dn_mtx (leaf)
 *   protects:
 *   	dn_dirty_dbufs
 *   	dn_ranges
 *   	phys accounting
 * 	dn_allocated_txg
 * 	dn_free_txg
 * 	dn_assigned_txg
 * 	dd_assigned_tx
 * 	dn_notxholds
 * 	dn_dirtyctx
 * 	dn_dirtyctx_firstset
 * 	(dn_phys copy fields?)
 * 	(dn_phys contents?)
 *   held from:
 *   	dnode_*
 *   	dbuf_dirty: none
 *   	dbuf_sync: none (phys accounting)
 *   	dbuf_undirty_last_dirty: none (dn_ranges, dn_dirty_dbufs)
 *   	dbuf_write_done: none (phys accounting)
 *   	dmu_object_info_from_dnode: none (accounting)
 *   	dmu_tx_commit: none
 *   	dmu_tx_hold_object_impl: none
 *   	dmu_tx_try_assign: dn_notxholds(cv)
 *   	dmu_tx_unassign: none
 *
 * dn_phys_rwlock(leaf)
 *   protects:
 *	dn_phys
 *   held from:
 *	dnode_*
 *	dbuf_read_impl
 *	dbuf_findoff
 *	dmu_object_info_from_dnode
 *	dmu_object_size_from_db
 *	dmu_tx_count_free
 *
 * dd_lock
 *    must be held before:
 *      ds_lock
 *      ancestors' dd_lock
 *    protects:
 *    	dd_prop_cbs
 *    	dd_sync_*
 *    	dd_used_bytes
 *    	dd_tempreserved
 *    	dd_space_towrite
 *    	dd_myname
 *    	dd_phys accounting?
 *    held from:
 *    	dsl_dir_*
 *    	dsl_prop_changed_notify: none (dd_prop_cbs)
 *    	dsl_prop_register: none (dd_prop_cbs)
 *    	dsl_prop_unregister: none (dd_prop_cbs)
 *    	dsl_dataset_block_freeable: none (dd_sync_*)
 *
 * os_lock (leaf)
 *   protects:
 *   	os_dirty_dnodes
 *   	os_free_dnodes
 *   	os_dnodes
 *   	os_downgraded_dbufs
 *   	dn_dirtyblksz
 *   	dn_dirty_link
 *   held from:
 *   	dnode_create: none (os_dnodes)
 *   	dnode_destroy: none (os_dnodes)
 *   	dnode_setdirty: none (dn_dirtyblksz, os_*_dnodes)
 *   	dnode_free: none (dn_dirtyblksz, os_*_dnodes)
 *
 * ds_lock
 *    protects:
 *    	ds_objset
 *    	ds_open_refcount
 *    	ds_snapname
 *    	ds_phys accounting
 *	ds_phys userrefs zapobj
 *	ds_reserved
 *    held from:
 *    	dsl_dataset_*
 *
 * dr_mtx (leaf)
 *    protects:
 *	dr_children
 *    held from:
 *	dbuf_dirty
 *	dbuf_undirty_last_dirty
 *	dbuf_sync_indirect
 *	dnode_new_blkid
 */

struct objset;
struct dmu_pool;

typedef struct dmu_xuio {
	int next;
	int cnt;
	struct arc_ref **bufs;
	iovec_t *iovp;
} dmu_xuio_t;

typedef struct xuio_stats {
	/* loaned yet not returned arc buf */
	kstat_named_t xuiostat_onloan_rbuf;
	kstat_named_t xuiostat_onloan_wbuf;
	/* whether a copy is made when loaning out a read buffer */
	kstat_named_t xuiostat_rbuf_copied;
	kstat_named_t xuiostat_rbuf_nocopy;
	/* whether a copy is made when assigning a write buffer */
	kstat_named_t xuiostat_wbuf_copied;
	kstat_named_t xuiostat_wbuf_nocopy;
} xuio_stats_t;

static xuio_stats_t xuio_stats = {
	{ "onloan_read_buf",	KSTAT_DATA_UINT64 },
	{ "onloan_write_buf",	KSTAT_DATA_UINT64 },
	{ "read_buf_copied",	KSTAT_DATA_UINT64 },
	{ "read_buf_nocopy",	KSTAT_DATA_UINT64 },
	{ "write_buf_copied",	KSTAT_DATA_UINT64 },
	{ "write_buf_nocopy",	KSTAT_DATA_UINT64 }
};

#define	XUIOSTAT_INCR(stat, val)	\
	atomic_add_64(&xuio_stats.stat.value.ui64, (val))
#define	XUIOSTAT_BUMP(stat)	XUIOSTAT_INCR(stat, 1)


#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_DMU_IMPL_H */
