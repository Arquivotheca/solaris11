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

#include <sys/conf.h>
#include <sys/file.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/modctl.h>
#include <sys/scsi/scsi.h>
#include <sys/scsi/impl/scsi_reset_notify.h>
#include <sys/scsi/generic/mode.h>
#include <sys/disp.h>
#include <sys/byteorder.h>
#include <sys/atomic.h>
#include <sys/sdt.h>
#include <sys/dkio.h>
#include <sys/dmu.h>
#include <sys/arc.h>
#include <sys/zvol.h>
#include <sys/zfs_rlock.h>

#include <sys/stmf.h>
#include <sys/lpif.h>
#include <sys/portif.h>
#include <sys/stmf_ioctl.h>
#include <sys/stmf_sbd_ioctl.h>

#include "stmf_sbd.h"
#include "sbd_impl.h"


/*
 * This file contains direct calls into the zfs module.
 * These functions mimic zvol_read and zvol_write except pointers
 * to the data buffers are passed instead of copying the data itself.
 *
 * zfs internal interfaces referenced here:
 *
 * FUNCTIONS
 *    dmu_buf_hold_array_by_bonus()
 *    dmu_buf_rele_array()
 *
 *    dmu_request_arcbuf()
 *    dmu_assign_arcbuf()
 *    dmu_release_arcbuf()
 *    arc_buf_size()
 *
 *    dmu_tx_create()
 *    dmu_tx_hold_write()
 *    dmu_tx_assign()
 *    dmu_tx_commit(tx)
 *    dmu_tx_abort(tx)
 *    zil_commit()
 *
 *    zfs_range_lock()
 *    zfs_range_unlock()
 *
 *    zvol_log_write()
 *    zvol_get_volume_params()
 *    zvol_get_volume_readonly()
 *    zvol_get_volume_size()
 *    zvol_get_volume_wce()
 *
 *    dmu_read_uio()
 *    dmu_write_uio()
 * MINOR DATA
 *    zv_volsize
 *    zv_volblocksize
 *    zv_flags		- for WCE
 *    zv_objset		- dmu_tx_create
 *    zv_zilog		- zil_commit
 *    zv_znode		- zfs_range_lock
 *    zv_dbuf		- dmu_buf_hold_array_by_bonus, dmu_request_arcbuf
 * GLOBAL DATA
 *    zvol_maxphys
 */

/*
 * Take direct control of the volume instead of using the driver
 * interfaces provided by zvol.c. Gather parameters and handles
 * needed to make direct calls into zfs/dmu/zvol. The driver is
 * opened exclusively at this point, so these parameters cannot change.
 *
 * NOTE: the object size and WCE can change while the device
 * is open, so they must be fetched for every operation.
 */
int
sbd_zvol_get_volume_params(sbd_lu_t *sl)
{
	int ret;

	ret = zvol_get_volume_params(sl->sl_zvol_minor,
	    &sl->sl_blksize,		/* volume block size */
	    &sl->sl_max_xfer_len,	/* max data chunk size */
	    &sl->sl_zvol_minor_hdl,	/* minor soft state */
	    &sl->sl_zvol_objset_hdl,	/* dmu_tx_create */
	    &sl->sl_zvol_zil_hdl,	/* zil_commit */
	    &sl->sl_zvol_rl_hdl,	/* zfs_range_lock */
	    &sl->sl_zvol_bonus_hdl);	/* dmu_buf_hold_array_by_bonus, */
					/* dmu_request_arcbuf, */
					/* dmu_assign_arcbuf */

	if (ret == 0 && sl->sl_blksize < MMU_PAGESIZE) {
		cmn_err(CE_NOTE, "COMSTAR reduced copy disabled due to "
		    "small zvol blocksize (%d)\n", (int)sl->sl_blksize);
		ret = ENOTSUP;
	}

	return (ret);
}
/*
 * call zvol to determine its rdonly state
 */
int
sbd_zvol_get_rdonly(sbd_lu_t *sl, int *rdonly)
{
	if (sl->sl_zvol_minor_hdl != NULL) {
		*rdonly = zvol_get_volume_readonly(sl->sl_zvol_minor_hdl);
		return (0);
	}
	return (-1);
}
/*
 * Return the number of elements in a scatter/gather list required for
 * the given span in the zvol. Elements are 1:1 with zvol blocks.
 */
uint32_t
sbd_zvol_numsegs(sbd_lu_t *sl, uint64_t off, uint32_t len)
{
	uint64_t blksz = sl->sl_blksize;
	uint64_t endoff = off + len;
	uint64_t numsegs;

	numsegs = (P2ROUNDUP(endoff, blksz) - P2ALIGN(off, blksz)) / blksz;
	return ((uint32_t)numsegs);
}

/*
 * Return an array of dmu_buf_t pointers for the requested range.
 * The dmu buffers are either in cache or read in synchronously.
 * Fill in the dbuf sglist from the dmu_buf_t array.
 */
int
sbd_zvol_alloc_read_bufs(sbd_lu_t *sl, stmf_data_buf_t *dbuf)
{
	sbd_zvol_io_t	*zvio = dbuf->db_lu_private;
	rl_t 		*rl;
	int 		numbufs, error;
	uint64_t 	len = dbuf->db_data_size;
	uint64_t 	offset = zvio->zvio_offset;
	xuio_t		*xuio;
	uio_t		*uio;

	/* Make sure request is reasonable */
	if (len > sl->sl_max_xfer_len)
		return (E2BIG);
	if (offset + len  > zvol_get_volume_size(sl->sl_zvol_minor_hdl))
		return (EIO);

	xuio = kmem_zalloc(sizeof (xuio_t), KM_SLEEP);
	uio = &xuio->xu_uio;
	uio->uio_extflg = UIO_XUIO;
	uio->uio_loffset = offset;
	xuio->xu_type = UIOTYPE_ZEROCOPY;
	XUIO_XUZC_RW(xuio) = UIO_READ;

	numbufs = (int)dbuf->db_sglist_length;
	dmu_xuio_init(xuio, numbufs);
	/*
	 * The range lock is only held until the dmu buffers read in and
	 * held; not during the callers use of the data.
	 */
	rl = zfs_range_lock(sl->sl_zvol_rl_hdl, offset, len, RL_READER);

	error = dmu_read_uio(sl->sl_zvol_objset_hdl, ZVOL_OBJ, uio, len);

	zfs_range_unlock(rl);

	if (error == ECKSUM)
		error = EIO;

	if (error == 0) {
		/*
		 * Fill in db_sglist from the iovec_t array.
		 */
		int		i;
		stmf_sglist_ent_t *sgl;
		iovec_t		*iov = xuio->xu_uio.uio_iov;

		zvio->zvio_uio = uio;
		sgl = &dbuf->db_sglist[0];
		for (i = 0; i < numbufs; i++) {
			sgl->seg_addr = (uint8_t *)iov->iov_base;
			sgl->seg_length = (uint32_t)iov->iov_len;
			len -= sgl->seg_length;
			sgl++;
			iov++;
		}
		ASSERT(len == 0);

	}
	return (error);
}

/*
 * Release a dmu_buf_t array.
 */
/*ARGSUSED*/
void
sbd_zvol_rele_read_bufs(sbd_lu_t *sl, stmf_data_buf_t *dbuf)
{
	sbd_zvol_io_t *zvio = dbuf->db_lu_private;
	int i;
	xuio_t *xuio = (xuio_t *)zvio->zvio_uio;

	ASSERT(dbuf->db_sglist_length);
	ASSERT(xuio && xuio->xu_type == UIOTYPE_ZEROCOPY);

	i = dmu_xuio_cnt(xuio);
	while (i-- > 0) {
		dmu_release_arcbuf(dmu_xuio_arcbuf(xuio, i));
	}
	dmu_xuio_fini(xuio);
	kmem_free(xuio, sizeof (xuio_t));
}

/*
 * Allocate enough loaned arc buffers for the requested region.
 * Mimic the handling of the dmu_buf_t array used for reads as closely
 * as possible even though the arc_ref_t's are anonymous until released.
 * The buffers will match the zvol object blocks sizes and alignments
 * such that a data copy may be avoided when the buffers are assigned.
 */
int
sbd_zvol_alloc_write_bufs(sbd_lu_t *sl, stmf_data_buf_t *dbuf)
{
	sbd_zvol_io_t	*zvio = dbuf->db_lu_private;
	int		blkshift, numbufs, i;
	uint64_t	blksize;
	arc_ref_t	**abp;
	stmf_sglist_ent_t *sgl;
	uint64_t 	len = dbuf->db_data_size;
	uint64_t 	offset = zvio->zvio_offset;

	/* Make sure request is reasonable */
	if (len > sl->sl_max_xfer_len)
		return (E2BIG);
	if (offset + len  > zvol_get_volume_size(sl->sl_zvol_minor_hdl))
		return (EIO);

	/*
	 * Break up the request into chunks to match
	 * the volume block size. Only full, and aligned
	 * buffers will avoid the data copy in the dmu.
	 */
	/*
	 * calculate how may dbufs are needed
	 */
	blksize = sl->sl_blksize;
	ASSERT(ISP2(blksize));
	blkshift = highbit(blksize - 1);
	/*
	 * taken from dmu_buf_hold_array_by_dnode()
	 */
	numbufs = (P2ROUNDUP(offset+len, 1ULL<<blkshift) -
	    P2ALIGN(offset, 1ULL<<blkshift)) >> blkshift;
	if (dbuf->db_sglist_length != numbufs) {
		cmn_err(CE_PANIC, "wrong size sglist: dbuf %d != %d\n",
		    dbuf->db_sglist_length, numbufs);
	}
	/*
	 * allocate a holder for the needed arc_ref pointers
	 */
	abp = kmem_alloc(sizeof (arc_ref_t *) * numbufs, KM_SLEEP);
	/*
	 * The write operation uses loaned arc buffers so that
	 * the xfer_data is done outside of a dmu transaction.
	 * These buffers will exactly match the request unlike
	 * the dmu buffers obtained from the read operation.
	 */
	/*
	 * allocate the arc buffers and fill in the stmf sglist
	 */
	sgl = &dbuf->db_sglist[0];
	for (i = 0; i < numbufs; i++) {
		uint64_t seglen;

		/* first block may not be aligned */
		seglen = P2NPHASE(offset, blksize);
		if (seglen == 0)
			seglen = blksize;
		seglen = MIN(seglen, len);
		abp[i] = dmu_request_arcbuf((int)seglen);
		ASSERT(arc_buf_size(abp[i]) == (int)seglen);
		sgl->seg_addr = abp[i]->r_data;
		sgl->seg_length = (uint32_t)seglen;
		sgl++;
		offset += seglen;
		len -= seglen;
	}
	ASSERT(len == 0);

	zvio->zvio_abp = abp;
	return (0);
}

/*ARGSUSED*/
void
sbd_zvol_rele_write_bufs_abort(sbd_lu_t *sl, stmf_data_buf_t *dbuf)
{
	sbd_zvol_io_t *zvio = dbuf->db_lu_private;
	int i;
	arc_ref_t **abp = zvio->zvio_abp;

	/* free arcbufs */
	for (i = 0; i < dbuf->db_sglist_length; i++)
		dmu_release_arcbuf(*abp++);
	kmem_free(zvio->zvio_abp,
	    sizeof (arc_ref_t *) * dbuf->db_sglist_length);
	zvio->zvio_abp = NULL;
}

/*
 * Release the arc_ref_t array allocated above and handle these cases :
 *
 * flags == 0 - create transaction and assign all arc bufs to offsets
 * flags == ZVIO_COMMIT - same as above and commit to zil on sync devices
 */
int
sbd_zvol_rele_write_bufs(sbd_lu_t *sl, stmf_data_buf_t *dbuf)
{
	sbd_zvol_io_t	*zvio = dbuf->db_lu_private;
	dmu_tx_t	*tx;
	int		sync, i, error;
	rl_t 		*rl;
	arc_ref_t	**abp = zvio->zvio_abp;
	int		flags = zvio->zvio_flags;
	uint64_t	toffset, offset = zvio->zvio_offset;
	uint64_t	resid, len = dbuf->db_data_size;

	ASSERT(flags == 0 || flags == ZVIO_COMMIT || flags == ZVIO_ABORT);

	rl = zfs_range_lock(sl->sl_zvol_rl_hdl, offset, len, RL_WRITER);

	tx = dmu_tx_create(sl->sl_zvol_objset_hdl);
	dmu_tx_hold_write(tx, ZVOL_OBJ, offset, (int)len);
	error = dmu_tx_assign(tx, TXG_WAIT);

	if (error) {
		dmu_tx_abort(tx);
		zfs_range_unlock(rl);
		sbd_zvol_rele_write_bufs_abort(sl, dbuf);
		return (error);
	}

	toffset = offset;
	resid = len;
	for (i = 0; i < dbuf->db_sglist_length; i++) {
		arc_ref_t *abuf;
		int size;

		abuf = abp[i];
		size = arc_buf_size(abuf);
		dmu_assign_arcbuf(sl->sl_zvol_bonus_hdl, toffset, abuf, tx);
		toffset += size;
		resid -= size;
	}
	ASSERT(resid == 0);

	sync = !zvol_get_volume_wce(sl->sl_zvol_minor_hdl);
	zvol_log_write_minor(sl->sl_zvol_minor_hdl, tx, offset,
	    (ssize_t)len, sync);
	dmu_tx_commit(tx);
	zfs_range_unlock(rl);
	kmem_free(zvio->zvio_abp,
	    sizeof (arc_ref_t *) * dbuf->db_sglist_length);
	zvio->zvio_abp = NULL;
	if (sync && (flags & ZVIO_COMMIT))
		zil_commit(sl->sl_zvol_zil_hdl, ZVOL_OBJ);
	return (0);
}

/*
 * Copy interface for callers using direct zvol access.
 * Very similar to zvol_read but the uio may have multiple iovec entries.
 */
int
sbd_zvol_copy_read(sbd_lu_t *sl, uio_t *uio)
{
	int		error;
	rl_t 		*rl;
	uint64_t	len = (uint64_t)uio->uio_resid;
	uint64_t	offset = (uint64_t)uio->uio_loffset;

	/* Make sure request is reasonable */
	if (len > sl->sl_max_xfer_len)
		return (E2BIG);
	if (offset + len  > zvol_get_volume_size(sl->sl_zvol_minor_hdl))
		return (EIO);

	rl = zfs_range_lock(sl->sl_zvol_rl_hdl, offset, len, RL_READER);

	error =  dmu_read_uio(sl->sl_zvol_objset_hdl, ZVOL_OBJ, uio, len);

	zfs_range_unlock(rl);
	if (error == ECKSUM)
		error = EIO;
	return (error);
}

/*
 * Copy interface for callers using direct zvol access.
 * Very similar to zvol_write but the uio may have multiple iovec entries.
 */
int
sbd_zvol_copy_write(sbd_lu_t *sl, uio_t *uio, int flags)
{
	rl_t 		*rl;
	dmu_tx_t 	*tx;
	int		error, sync;
	uint64_t	len = (uint64_t)uio->uio_resid;
	uint64_t	offset = (uint64_t)uio->uio_loffset;

	ASSERT(flags == 0 || flags == ZVIO_COMMIT);

	/* Make sure request is reasonable */
	if (len > sl->sl_max_xfer_len)
		return (E2BIG);
	if (offset + len  > zvol_get_volume_size(sl->sl_zvol_minor_hdl))
		return (EIO);

	rl = zfs_range_lock(sl->sl_zvol_rl_hdl, offset, len, RL_WRITER);

	sync = !zvol_get_volume_wce(sl->sl_zvol_minor_hdl);

	tx = dmu_tx_create(sl->sl_zvol_objset_hdl);
	dmu_tx_hold_write(tx, ZVOL_OBJ, offset, (int)uio->uio_resid);
	error = dmu_tx_assign(tx, TXG_WAIT);
	if (error) {
		dmu_tx_abort(tx);
	} else {
		/*
		 * XXX use the new bonus handle entry.
		 */
		error = dmu_write_uio(sl->sl_zvol_objset_hdl, ZVOL_OBJ,
		    uio, len, tx);
		if (error == 0) {
			zvol_log_write_minor(sl->sl_zvol_minor_hdl, tx, offset,
			    (ssize_t)len, sync);
		}
		dmu_tx_commit(tx);
	}
	zfs_range_unlock(rl);
	if (sync && (flags & ZVIO_COMMIT))
		zil_commit(sl->sl_zvol_zil_hdl, ZVOL_OBJ);
	if (error == ECKSUM)
		error = EIO;
	return (error);
}
