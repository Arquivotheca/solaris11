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
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/dmu.h>
#include <sys/dmu_impl.h>
#include <sys/dmu_tx.h>
#include <sys/dbuf.h>
#include <sys/dnode.h>
#include <sys/zfs_context.h>
#include <sys/dmu_objset.h>
#include <sys/dmu_traverse.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_dir.h>
#include <sys/dsl_pool.h>
#include <sys/dsl_synctask.h>
#include <sys/zfs_ioctl.h>
#include <sys/zap.h>
#include <sys/zio_checksum.h>
#include <sys/zfs_znode.h>

struct diffarg {
	struct vnode *da_vp;		/* file to which we are reporting */
	offset_t *da_offp;
	int da_err;			/* error that stopped diff search */
	dmu_diff_record_t da_ddr;
};

static int
write_record(struct diffarg *da)
{
	ssize_t resid; /* have to get resid to get detailed errno */

	if (da->da_ddr.ddr_type == DDR_NONE) {
		da->da_err = 0;
		return (0);
	}

	da->da_err = vn_rdwr(UIO_WRITE, da->da_vp, (caddr_t)&da->da_ddr,
	    sizeof (da->da_ddr), 0, UIO_SYSSPACE, FAPPEND,
	    RLIM64_INFINITY, CRED(), &resid);
	*da->da_offp += sizeof (da->da_ddr);
	return (da->da_err);
}

static int
report_free_dnode_range(struct diffarg *da, uint64_t first, uint64_t last)
{
	ASSERT(first <= last);
	if (da->da_ddr.ddr_type != DDR_FREE ||
	    first != da->da_ddr.ddr_last + 1) {
		if (write_record(da) != 0)
			return (da->da_err);
		da->da_ddr.ddr_type = DDR_FREE;
		da->da_ddr.ddr_first = first;
		da->da_ddr.ddr_last = last;
		return (0);
	}
	da->da_ddr.ddr_last = last;
	return (0);
}

static int
report_dnode(struct diffarg *da, uint64_t object, dnode_phys_t *dnp)
{
	ASSERT(dnp != NULL);
	if (dnp->dn_type == DMU_OT_NONE)
		return (report_free_dnode_range(da, object, object));

	if (da->da_ddr.ddr_type != DDR_INUSE ||
	    object != da->da_ddr.ddr_last + 1) {
		if (write_record(da) != 0)
			return (da->da_err);
		da->da_ddr.ddr_type = DDR_INUSE;
		da->da_ddr.ddr_first = da->da_ddr.ddr_last = object;
		return (0);
	}
	da->da_ddr.ddr_last = object;
	return (0);
}

#define	DBP_SPAN(dnp, level)				  \
	(((uint64_t)dnp->dn_datablkszsec) << (SPA_MINBLOCKSHIFT + \
	(level) * (dnp->dn_indblkshift - SPA_BLKPTRSHIFT)))

/* ARGSUSED */
static int
diff_cb(spa_t *spa, zilog_t *zilog, const blkptr_t *bp,
    const zbookmark_t *zb, const dnode_phys_t *dnp, void *arg)
{
	struct diffarg *da = arg;
	int err = 0;

	if (issig(JUSTLOOKING) && issig(FORREAL))
		return (EINTR);

	if (zb->zb_object != DMU_META_DNODE_OBJECT)
		return (0);

	if (bp == NULL) {
		uint64_t span = DBP_SPAN(dnp, zb->zb_level);
		uint64_t dnobj = (zb->zb_blkid * span) >> DNODE_SHIFT;

		err = report_free_dnode_range(da, dnobj,
		    dnobj + (span >> DNODE_SHIFT) - 1);
		if (err)
			return (err);
	} else if (zb->zb_level == 0) {
		dnode_phys_t *blk;
		arc_ref_t *abuf;
		int blksz = BP_GET_LSIZE(bp);
		int i;

		if (dsl_read(NULL, spa, bp, blksz, arc_getref_func, &abuf,
		    ZIO_PRIORITY_ASYNC_READ, ZIO_FLAG_CANFAIL, 0, zb) != 0)
			return (EIO);

		blk = abuf->r_data;
		for (i = 0; i < blksz >> DNODE_SHIFT; i++) {
			uint64_t dnobj = (zb->zb_blkid <<
			    (DNODE_BLOCK_SHIFT - DNODE_SHIFT)) + i;
			err = report_dnode(da, dnobj, blk+i);
			if (err)
				break;
		}
		arc_free_ref(abuf);
		if (err)
			return (err);
		/* Don't care about the data blocks */
		return (TRAVERSE_VISIT_NO_CHILDREN);
	}
	return (0);
}

static int
dmu_diff_traverse(dsl_dataset_t *ds, vnode_t *vp, offset_t *offp,
    uint64_t from_txg)
{
	struct diffarg da;
	int err;

	da.da_vp = vp;
	da.da_offp = offp;
	da.da_ddr.ddr_type = DDR_NONE;
	da.da_ddr.ddr_first = da.da_ddr.ddr_last = 0;
	da.da_err = 0;

	err = traverse_dataset(ds, from_txg,
	    TRAVERSE_PRE | TRAVERSE_PREFETCH_METADATA, diff_cb, &da);

	if (err) {
		da.da_err = err;
	} else {
		/* we set the da.da_err we return as side-effect */
		(void) write_record(&da);
	}

	return (da.da_err);
}

int
dmu_diff(objset_t *tosnap, objset_t *fromsnap, struct vnode *vp,
    offset_t *offp)
{
	dsl_dataset_t *ds = tosnap->os_dsl_dataset;
	dsl_dataset_t *fromds;
	dsl_dataset_t *findds;
	dsl_dataset_t *relds;
	int err = 0;

	if (fromsnap == NULL)
		return (dmu_diff_traverse(ds, vp, offp, 0));
	fromds = fromsnap->os_dsl_dataset;

	/* make certain we are looking at snapshots */
	if (!dsl_dataset_is_snapshot(ds) || !dsl_dataset_is_snapshot(fromds))
		return (EINVAL);

	/* fromsnap must be earlier and from the same lineage as tosnap */
	if (fromds->ds_phys->ds_creation_txg >= ds->ds_phys->ds_creation_txg)
		return (EXDEV);

	relds = NULL;
	findds = ds;

	while (fromds->ds_dir != findds->ds_dir) {
		dsl_pool_t *dp = ds->ds_dir->dd_pool;

		if (!dsl_dir_is_clone(findds->ds_dir)) {
			if (relds)
				dsl_dataset_rele(relds, FTAG);
			return (EXDEV);
		}

		rw_enter(&dp->dp_config_rwlock, RW_READER);
		err = dsl_dataset_hold_obj(dp,
		    findds->ds_dir->dd_phys->dd_origin_obj, FTAG, &findds);
		rw_exit(&dp->dp_config_rwlock);

		if (relds)
			dsl_dataset_rele(relds, FTAG);

		if (err)
			return (EXDEV);

		relds = findds;
	}

	if (relds)
		dsl_dataset_rele(relds, FTAG);

	return (dmu_diff_traverse(ds, vp, offp,
	    fromds->ds_phys->ds_creation_txg));
}
