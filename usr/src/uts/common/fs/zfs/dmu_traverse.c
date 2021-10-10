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
#include <sys/dmu_objset.h>
#include <sys/dmu_traverse.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_dir.h>
#include <sys/dsl_pool.h>
#include <sys/dnode.h>
#include <sys/spa.h>
#include <sys/zio.h>
#include <sys/dmu_impl.h>
#include <sys/sa.h>
#include <sys/sa_impl.h>
#include <sys/callb.h>

int zfs_pd_blks_max = 100;

typedef struct prefetch_data {
	kmutex_t pd_mtx;
	kcondvar_t pd_cv;
	int pd_blks_max;
	int pd_blks_fetched;
	int pd_flags;
	boolean_t pd_cancel;
	boolean_t pd_exited;
} prefetch_data_t;

typedef struct traverse_data {
	spa_t *td_spa;
	uint64_t td_objset;
	blkptr_t *td_rootbp;
	uint64_t td_min_txg;
	int td_flags;
	prefetch_data_t *td_pfd;
	blkptr_cb_t *td_func;
	void *td_arg;
} traverse_data_t;

static int traverse_dnode(traverse_data_t *td, const dnode_phys_t *dnp,
    uint64_t objset, uint64_t object);

static int
traverse_zil_block(zilog_t *zilog, blkptr_t *bp, void *arg, uint64_t claim_txg)
{
	traverse_data_t *td = arg;
	zbookmark_t zb;

	if (bp->blk_birth == 0)
		return (0);

	if (claim_txg == 0 && bp->blk_birth >= spa_first_txg(td->td_spa))
		return (0);

	SET_BOOKMARK(&zb, td->td_objset, ZB_ZIL_OBJECT, ZB_ZIL_LEVEL,
	    bp->blk_cksum.zc_word[ZIL_ZC_SEQ]);

	(void) td->td_func(td->td_spa, zilog, bp, &zb, NULL, td->td_arg);

	return (0);
}

static int
traverse_zil_record(zilog_t *zilog, lr_t *lrc, void *arg, uint64_t claim_txg)
{
	traverse_data_t *td = arg;

	if (lrc->lrc_txtype == TX_WRITE) {
		lr_write_t *lr = (lr_write_t *)lrc;
		blkptr_t *bp = &lr->lr_blkptr;
		zbookmark_t zb;

		if (bp->blk_birth == 0)
			return (0);

		if (claim_txg == 0 || bp->blk_birth < claim_txg)
			return (0);

		SET_BOOKMARK(&zb, td->td_objset, lr->lr_foid,
		    ZB_ZIL_LEVEL, lr->lr_offset / BP_GET_LSIZE(bp));

		(void) td->td_func(td->td_spa, zilog, bp, &zb, NULL,
		    td->td_arg);
	}
	return (0);
}

static void
traverse_zil(traverse_data_t *td, zil_header_t *zh)
{
	uint64_t claim_txg = zh->zh_claim_txg;
	zilog_t *zilog;

	/*
	 * We only want to visit blocks that have been claimed but not yet
	 * replayed; plus, in read-only mode, blocks that are already stable.
	 */
	if (claim_txg == 0 && spa_writeable(td->td_spa))
		return;

	zilog = zil_alloc(spa_get_dsl(td->td_spa)->dp_meta_objset, zh);

	(void) zil_parse(zilog, traverse_zil_block, traverse_zil_record, td,
	    claim_txg);

	zil_free(zilog);
}

static int
traverse_visitbp(traverse_data_t *td, const dnode_phys_t *dnp,
    const blkptr_t *bp, const zbookmark_t *zb)
{
	zbookmark_t czb;
	int err = 0, lasterr = 0;
	arc_ref_t *buf = NULL;
	prefetch_data_t *pd = td->td_pfd;
	boolean_t keep_going = td->td_flags & TRAVERSE_HARD;

	if (bp->blk_birth == 0) {
		err = td->td_func(td->td_spa, NULL, NULL, zb, dnp, td->td_arg);
		return (err);
	}

	if (bp->blk_birth <= td->td_min_txg)
		return (0);

	if (pd && !pd->pd_exited &&
	    ((pd->pd_flags & TRAVERSE_PREFETCH_DATA) ||
	    BP_GET_TYPE(bp) == DMU_OT_DNODE || BP_GET_LEVEL(bp) > 0)) {
		mutex_enter(&pd->pd_mtx);
		ASSERT(pd->pd_blks_fetched >= 0);
		while (pd->pd_blks_fetched == 0 && !pd->pd_exited)
			cv_wait(&pd->pd_cv, &pd->pd_mtx);
		pd->pd_blks_fetched--;
		cv_broadcast(&pd->pd_cv);
		mutex_exit(&pd->pd_mtx);
	}

	if (td->td_flags & TRAVERSE_PRE) {
		err = td->td_func(td->td_spa, NULL, bp, zb, dnp, td->td_arg);
		if (err)
			return (err == TRAVERSE_VISIT_NO_CHILDREN ? 0 : err);
	}

	if (BP_GET_LEVEL(bp) > 0) {
		blkptr_t *cbp;
		int epb = BP_GET_LSIZE(bp) >> SPA_BLKPTRSHIFT;

		err = dsl_read(NULL, td->td_spa, bp, 0,
		    arc_getref_func, &buf, ZIO_PRIORITY_ASYNC_READ,
		    ZIO_FLAG_CANFAIL, 0, zb);
		if (err)
			return (err);

		/* recursively visitbp() blocks below this */
		cbp = buf->r_data;
		for (int i = 0; i < epb; i++, cbp++) {
			SET_BOOKMARK(&czb, zb->zb_objset, zb->zb_object,
			    zb->zb_level - 1,
			    zb->zb_blkid * epb + i);
			err = traverse_visitbp(td, dnp, cbp, &czb);
			if (err) {
				if (!keep_going)
					break;
				lasterr = err;
			}
		}
	} else if (BP_GET_TYPE(bp) == DMU_OT_DNODE) {
		int epb = BP_GET_LSIZE(bp) >> DNODE_SHIFT;

		err = dsl_read(NULL, td->td_spa, bp, 0,
		    arc_getref_func, &buf, ZIO_PRIORITY_ASYNC_READ,
		    ZIO_FLAG_CANFAIL, 0, zb);
		if (err)
			return (err);

		/* recursively visitbp() blocks below this */
		dnp = buf->r_data;
		for (int i = 0; i < epb; i++, dnp++) {
			err = traverse_dnode(td, dnp, zb->zb_objset,
			    zb->zb_blkid * epb + i);
			if (err) {
				if (!keep_going)
					break;
				lasterr = err;
			}
		}
	} else if (BP_GET_TYPE(bp) == DMU_OT_OBJSET) {
		objset_phys_t *osp;
		dnode_phys_t *dnp;

		err = dsl_read(NULL, td->td_spa, bp, 0,
		    arc_getref_func, &buf, ZIO_PRIORITY_ASYNC_READ,
		    ZIO_FLAG_CANFAIL, 0, zb);
		if (err)
			return (err);

		osp = buf->r_data;
		dnp = &osp->os_meta_dnode;
		err = traverse_dnode(td, dnp, zb->zb_objset,
		    DMU_META_DNODE_OBJECT);
		if (err && keep_going) {
			lasterr = err;
			err = 0;
		}
		if (err == 0 && arc_buf_size(buf) >= sizeof (objset_phys_t)) {
			dnp = &osp->os_userused_dnode;
			err = traverse_dnode(td, dnp, zb->zb_objset,
			    DMU_USERUSED_OBJECT);
			if (err)
				lasterr = err;
			if (err == 0 || keep_going) {
				dnp = &osp->os_groupused_dnode;
				err = traverse_dnode(td, dnp, zb->zb_objset,
				    DMU_GROUPUSED_OBJECT);
			}
		}
	}

	if (buf)
		arc_free_ref(buf);

	if (err == 0 && lasterr == 0 && (td->td_flags & TRAVERSE_POST))
		err = td->td_func(td->td_spa, NULL, bp, zb, dnp, td->td_arg);

	return (err != 0 ? err : lasterr);
}

static int
traverse_dnode(traverse_data_t *td, const dnode_phys_t *dnp,
    uint64_t objset, uint64_t object)
{
	int err = 0, lasterr = 0;
	zbookmark_t czb;
	boolean_t keep_going = (td->td_flags & TRAVERSE_HARD);

	if (dnp->dn_flags & DNODE_FLAG_SPILL_BLKPTR) {
		SET_BOOKMARK(&czb, objset, object, 0, DMU_SPILL_BLKID);
		err = traverse_visitbp(td, dnp, &dnp->dn_spill, &czb);
		if (err) {
			if (!keep_going)
				return (err);
			lasterr = err;
		}
	}

	for (int i = 0; i < dnp->dn_nblkptr; i++) {
		SET_BOOKMARK(&czb, objset, object, dnp->dn_nlevels - 1, i);
		err = traverse_visitbp(td, dnp, &dnp->dn_blkptr[i], &czb);
		if (err) {
			if (!keep_going)
				break;
			lasterr = err;
		}
	}
	return (err != 0 ? err : lasterr);
}

/* ARGSUSED */
static int
traverse_prefetcher(spa_t *spa, zilog_t *zilog, const blkptr_t *bp,
    const zbookmark_t *zb, const dnode_phys_t *dnp, void *arg)
{
	prefetch_data_t *pfd = arg;
	zio_t *zio;

	ASSERT(pfd->pd_blks_fetched >= 0);
	if (pfd->pd_cancel)
		return (EINTR);

	if (bp == NULL || !((pfd->pd_flags & TRAVERSE_PREFETCH_DATA) ||
	    BP_GET_TYPE(bp) == DMU_OT_DNODE || BP_GET_LEVEL(bp) > 0) ||
	    BP_GET_TYPE(bp) == DMU_OT_INTENT_LOG)
		return (0);

	mutex_enter(&pfd->pd_mtx);
	while (!pfd->pd_cancel && pfd->pd_blks_fetched >= pfd->pd_blks_max)
		cv_wait(&pfd->pd_cv, &pfd->pd_mtx);
	pfd->pd_blks_fetched++;
	cv_broadcast(&pfd->pd_cv);
	mutex_exit(&pfd->pd_mtx);

	zio = zio_root(spa, NULL, NULL, ZIO_FLAG_CANFAIL);
	(void) dsl_read(zio, spa, bp, 0, NULL, NULL, ZIO_PRIORITY_ASYNC_READ,
	    ZIO_FLAG_CANFAIL | ZIO_FLAG_SPECULATIVE, 0, zb);
	zio_nowait(zio);

	return (0);
}

static void
traverse_prefetch_thread(void *arg)
{
	traverse_data_t *td_main = arg;
	traverse_data_t td = *td_main;
	zbookmark_t czb;

	td.td_func = traverse_prefetcher;
	td.td_arg = td_main->td_pfd;
	td.td_pfd = NULL;

	SET_BOOKMARK(&czb, td.td_objset,
	    ZB_ROOT_OBJECT, ZB_ROOT_LEVEL, ZB_ROOT_BLKID);
	(void) traverse_visitbp(&td, NULL, td.td_rootbp, &czb);

	mutex_enter(&td_main->td_pfd->pd_mtx);
	td_main->td_pfd->pd_exited = B_TRUE;
	cv_broadcast(&td_main->td_pfd->pd_cv);
	mutex_exit(&td_main->td_pfd->pd_mtx);
}

/*
 * NB: dataset must not be changing on-disk (eg, is a snapshot or we are
 * in syncing context).
 */
static int
traverse_impl(spa_t *spa, dsl_dataset_t *ds, blkptr_t *rootbp,
    uint64_t txg_start, int flags, blkptr_cb_t func, void *arg)
{
	traverse_data_t td;
	prefetch_data_t pd = { 0 };
	zbookmark_t czb;
	int err;

	td.td_spa = spa;
	td.td_objset = ds ? ds->ds_object : 0;
	td.td_rootbp = rootbp;
	td.td_min_txg = txg_start;
	td.td_func = func;
	td.td_arg = arg;
	td.td_pfd = &pd;
	td.td_flags = flags;

	pd.pd_blks_max = zfs_pd_blks_max;
	pd.pd_flags = flags;
	mutex_init(&pd.pd_mtx, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&pd.pd_cv, NULL, CV_DEFAULT, NULL);

	/* See comment on ZIL traversal in dsl_scan_visitds. */
	if (ds != NULL && !dsl_dataset_is_snapshot(ds)) {
		objset_t *os;

		err = dmu_objset_from_ds(ds, &os);
		if (err)
			return (err);

		traverse_zil(&td, &os->os_zil_header);
	}

	if (!(flags & TRAVERSE_PREFETCH) ||
	    0 == taskq_dispatch(system_taskq, traverse_prefetch_thread,
	    &td, TQ_NOQUEUE))
		pd.pd_exited = B_TRUE;

	SET_BOOKMARK(&czb, td.td_objset,
	    ZB_ROOT_OBJECT, ZB_ROOT_LEVEL, ZB_ROOT_BLKID);
	err = traverse_visitbp(&td, NULL, rootbp, &czb);

	mutex_enter(&pd.pd_mtx);
	pd.pd_cancel = B_TRUE;
	cv_broadcast(&pd.pd_cv);
	while (!pd.pd_exited)
		cv_wait(&pd.pd_cv, &pd.pd_mtx);
	mutex_exit(&pd.pd_mtx);

	mutex_destroy(&pd.pd_mtx);
	cv_destroy(&pd.pd_cv);

	return (err);
}

/*
 * NB: dataset must not be changing on-disk (eg, is a snapshot or we are
 * in syncing context).
 */
int
traverse_dataset(dsl_dataset_t *ds, uint64_t txg_start, int flags,
    blkptr_cb_t func, void *arg)
{
	return (traverse_impl(ds->ds_dir->dd_pool->dp_spa, ds,
	    &ds->ds_phys->ds_bp, txg_start, flags, func, arg));
}

/*
 * NB: pool must not be changing on-disk (eg, from zdb or sync context).
 */
int
traverse_pool(spa_t *spa, uint64_t txg_start, int flags,
    blkptr_cb_t func, void *arg)
{
	int err, lasterr = 0;
	uint64_t obj;
	dsl_pool_t *dp = spa_get_dsl(spa);
	objset_t *mos = dp->dp_meta_objset;
	boolean_t hard = (flags & TRAVERSE_HARD);

	/* visit the MOS */
	err = traverse_impl(spa, NULL, spa_get_rootblkptr(spa),
	    txg_start, flags, func, arg);
	if (err)
		return (err);

	/* visit each dataset */
	for (obj = 1; err == 0 || (err != ESRCH && hard);
	    err = dmu_object_next(mos, &obj, FALSE, txg_start)) {
		dmu_object_info_t doi;

		err = dmu_object_info(mos, obj, &doi);
		if (err) {
			if (!hard)
				return (err);
			lasterr = err;
			continue;
		}

		if (doi.doi_type == DMU_OT_DSL_DATASET) {
			dsl_dataset_t *ds;
			uint64_t txg = txg_start;

			rw_enter(&dp->dp_config_rwlock, RW_READER);
			err = dsl_dataset_hold_obj(dp, obj, FTAG, &ds);
			rw_exit(&dp->dp_config_rwlock);
			if (err) {
				if (!hard)
					return (err);
				lasterr = err;
				continue;
			}
			if (ds->ds_phys->ds_prev_snap_txg > txg)
				txg = ds->ds_phys->ds_prev_snap_txg;
			err = traverse_dataset(ds, txg, flags, func, arg);
			dsl_dataset_rele(ds, FTAG);
			if (err) {
				if (!hard)
					return (err);
				lasterr = err;
			}
		}
	}
	if (err == ESRCH)
		err = 0;
	return (err != 0 ? err : lasterr);
}
