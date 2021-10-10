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
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <fs/fs_subr.h>

#include <sys/errno.h>
#include <sys/file.h>
#include <sys/kmem.h>
#include <sys/kobj.h>
#include <sys/cmn_err.h>
#include <sys/stat.h>
#include <sys/systm.h>
#include <sys/sunddi.h>
#include <sys/sysmacros.h>
#include <sys/vfs.h>
#include <sys/vfs_opreg.h>
#include <sys/sa_share.h>

#include <sharefs/sharefs.h>

/* ARGSUSED */
static int
sharefs_getattr(vnode_t *vp, vattr_t *vap, int flags, cred_t *cr,
    caller_context_t *ct)
{
	timestruc_t	now;
	shtab_stats_t	st_stats;
	shnode_t	*sft = VTOSH(vp);
	sharefs_zone_t	*szp = VTOZSD(vp);

	vap->va_type = VREG;
	vap->va_mode = S_IRUSR | S_IRGRP | S_IROTH;
	vap->va_nodeid = SHAREFS_INO_FILE;
	vap->va_nlink = 1;

	rw_enter(&szp->sz_sharefs_rwlock, RW_READER);

	/*
	 * If we get asked about a snapped vnode, then
	 * we must report the data in that vnode.
	 *
	 * Else we report what is currently in the
	 * sharetab.
	 */
	if (sft->sharefs_real_vp) {
		(void) shtab_stats(szp, &st_stats);
		vap->va_size = st_stats.sts_size;
		vap->va_mtime = st_stats.sts_mtime;
	} else {
		vap->va_size = sft->sharefs_size;
		vap->va_mtime = sft->sharefs_mtime;
	}
	rw_exit(&szp->sz_sharefs_rwlock);

	gethrestime(&now);
	vap->va_atime = vap->va_ctime = now;

	vap->va_uid = 0;
	vap->va_gid = 0;
	vap->va_rdev = 0;
	vap->va_blksize = DEV_BSIZE;
	vap->va_nblocks = howmany(vap->va_size, vap->va_blksize);
	vap->va_seq = 0;
	vap->va_fsid = vp->v_vfsp->vfs_dev;

	return (0);
}

/* ARGSUSED */
static int
sharefs_access(vnode_t *vp, int mode, int flags, cred_t *cr,
    caller_context_t *ct)
{
	if (mode & (VWRITE|VEXEC))
		return (EROFS);

	return (0);
}

/* ARGSUSED */
int
sharefs_open(vnode_t **vpp, int flag, cred_t *cr, caller_context_t *ct)
{
	vnode_t		*vp;
	vnode_t		*ovp = *vpp;
	shnode_t	*sft;
	sharefs_zone_t	*szp = VTOZSD(ovp);
	int		error = 0;

	if (flag & FWRITE)
		return (EINVAL);

	/*
	 * Create a new sharefs vnode for each operation. In order to
	 * avoid locks, we create a snapshot which can not change during
	 * reads.
	 */
	vp = gfs_file_create(sizeof (shnode_t), NULL, sharefs_ops_data);

	((gfs_file_t *)vp->v_data)->gfs_ino = SHAREFS_INO_FILE;

	/*
	 * Hold the parent!
	 */
	VFS_HOLD(ovp->v_vfsp);

	VN_SET_VFS_TYPE_DEV(vp, ovp->v_vfsp, VREG, 0);

	vp->v_flag |= VROOT | VNOCACHE | VNOMAP | VNOSWAP | VNOMOUNT;

	sft = VTOSH(vp);

	/*
	 * No need for the lock, no other thread can be accessing
	 * this data structure.
	 */
	sft->sharefs_real_vp = 0;

	/*
	 * Since the sharetab could easily change on us whilst we
	 * are dumping an extremely huge sharetab, we make a copy
	 * of it here and use it to dump instead.
	 */
	error = shtab_snap_create(szp, sft);

	sharefs_zone_shnode_hold(szp);

	*vpp = vp;
	VN_RELE(ovp);

	return (error);
}

/* ARGSUSED */
int
sharefs_close(vnode_t *vp, int flag, int count,
    offset_t off, cred_t *cr, caller_context_t *ct)
{
	shnode_t	*sft = VTOSH(vp);
	sharefs_zone_t	*szp = VTOZSD(vp);

	if (count > 1)
		return (0);

	rw_enter(&szp->sz_sharefs_rwlock, RW_WRITER);
	if (vp->v_count == 1) {
		if (sft->sharefs_snap != NULL) {
			kmem_free(sft->sharefs_snap, sft->sharefs_size + 1);
			sft->sharefs_size = 0;
			sft->sharefs_snap = NULL;
			sft->sharefs_generation = 0;
		}
	}
	rw_exit(&szp->sz_sharefs_rwlock);
	return (0);
}

/* ARGSUSED */
static int
sharefs_read(vnode_t *vp, uio_t *uio, int ioflag, cred_t *cr,
    caller_context_t *ct)
{
	shnode_t	*sft = VTOSH(vp);
	sharefs_zone_t	*szp = VTOZSD(vp);
	off_t		off = uio->uio_offset;
	size_t		len = uio->uio_resid;
	int		error = 0;

	/*
	 * First check to see if we need to grab a new snapshot.
	 */
	if (off == (off_t)0) {
		rw_enter(&szp->sz_sharefs_rwlock, RW_WRITER);

		error = shtab_snap_create(szp, sft);
		if (error) {
			rw_exit(&szp->sz_sharefs_rwlock);
			return (EFAULT);
		}
		rw_exit(&szp->sz_sharefs_rwlock);
	}

	rw_enter(&szp->sz_sharefs_rwlock, RW_READER);

	/* LINTED */
	if (len <= 0 || off >= sft->sharefs_size) {
		rw_exit(&szp->sz_sharefs_rwlock);
		return (error);
	}

	if ((size_t)(off + len) > sft->sharefs_size)
		len = sft->sharefs_size - off;

	if (off < 0 || len > sft->sharefs_size) {
		rw_exit(&szp->sz_sharefs_rwlock);
		return (EFAULT);
	}

	if (len != 0) {
		error = uiomove(sft->sharefs_snap + off,
		    len, UIO_READ, uio);
	}

	rw_exit(&szp->sz_sharefs_rwlock);
	return (error);
}

/* ARGSUSED */
static void
sharefs_inactive(vnode_t *vp, cred_t *cr, caller_context_t *ct)
{
	gfs_file_t	*fp = vp->v_data;
	shnode_t	*sft;
	sharefs_zone_t *szp = VTOZSD(vp);

	sft = (shnode_t *)gfs_file_inactive(vp);
	if (sft) {
		if (sft->sharefs_snap != NULL)
			kmem_free(sft->sharefs_snap, sft->sharefs_size + 1);
		kmem_free(sft, fp->gfs_size);
		sharefs_zone_shnode_rele(szp);
	}
}

vnode_t *
sharefs_create_root_file(sharefs_zone_t *szp, vfs_t *vfsp)
{
	vnode_t		*vp;
	shnode_t	*sft;

	vp = gfs_root_create_file(sizeof (shnode_t),
	    vfsp, sharefs_ops_data, SHAREFS_INO_FILE);

	sft = VTOSH(vp);

	sft->sharefs_real_vp = 1;
	sharefs_zone_shnode_hold(szp);

	return (vp);
}

/* ARGSUSED */
static int
sharefs_ioctl(vnode_t *vp, int cmd, intptr_t argp, int flags, cred_t *cr,
    int *rvalp, caller_context_t *ct)
{
	sharefs_ioc_t *ioc;
	sharefs_ioc_hdr_t ioc_hdr;
	sharefs_ioc_lookup_t *ioc_lu;
	sharefs_ioc_find_init_t *ioc_finit;
	sharefs_ioc_find_next_t *ioc_fnext;
	sharefs_ioc_find_fini_t *ioc_ffini;
	uint32_t crc;
	sharefs_zone_t	*szp = VTOZSD(vp);
	int rc;

	/*
	 * copyin and validate header
	 */
	if (ddi_copyin((const void *)argp, &ioc_hdr, sizeof (sharefs_ioc_hdr_t),
	    flags) || (ioc_hdr.version != SHAREFS_IOC_VERSION)) {
		rc = EFAULT;
		goto rele_out;
	}

	crc = ioc_hdr.crc;
	ioc_hdr.crc = 0;
	if (sa_crc_gen((uint8_t *)&ioc_hdr, sizeof (ioc_hdr)) != crc) {
		rc =  EFAULT;
		goto rele_out;
	}

	/*
	 * copyin complete request
	 */
	ioc = kmem_alloc(ioc_hdr.len, KM_SLEEP);
	if (ddi_copyin((const void *)argp, ioc, ioc_hdr.len, flags)) {
		kmem_free(ioc, ioc_hdr.len);
		rc = EFAULT;
		goto rele_out;
	}

	switch (cmd) {
	case SHAREFS_IOC_LOOKUP:
		ioc_lu = (sharefs_ioc_lookup_t *)ioc;

		rc = shtab_cache_lookup(szp, ioc_lu->sh_name, ioc_lu->sh_path,
		    ioc_lu->proto, ioc_lu->share, ioc_lu->shrlen);
		break;

	case SHAREFS_IOC_FIND_INIT:
		ioc_finit = (sharefs_ioc_find_init_t *)ioc;

		rc = shtab_cache_find_init(szp, ioc_finit->mntpnt,
		    ioc_finit->proto, &ioc_finit->hdl);
		break;

	case SHAREFS_IOC_FIND_NEXT:
		ioc_fnext = (sharefs_ioc_find_next_t *)ioc;

		rc = shtab_cache_find_next(szp, &ioc_fnext->hdl,
		    ioc_fnext->share, ioc_fnext->shrlen);
		break;

	case SHAREFS_IOC_FIND_FINI:
		ioc_ffini = (sharefs_ioc_find_fini_t *)ioc;

		rc = shtab_cache_find_fini(szp, &ioc_ffini->hdl);
		break;

	default:
		rc = ENOTTY;
		break;
	}

	if (rc == 0) {
		if (ddi_copyout((const void *)ioc, (void *)argp, ioc_hdr.len,
		    flags))
			rc = EFAULT;
	}
	kmem_free(ioc, ioc_hdr.len);

rele_out:
	return (rc);
}

const fs_operation_def_t sharefs_tops_data[] = {
	{ VOPNAME_OPEN,		{ .vop_open = sharefs_open } },
	{ VOPNAME_CLOSE,	{ .vop_close = sharefs_close } },
	{ VOPNAME_IOCTL,	{ .vop_ioctl = sharefs_ioctl } },
	{ VOPNAME_GETATTR,	{ .vop_getattr = sharefs_getattr } },
	{ VOPNAME_ACCESS,	{ .vop_access = sharefs_access } },
	{ VOPNAME_INACTIVE,	{ .vop_inactive = sharefs_inactive } },
	{ VOPNAME_READ,		{ .vop_read = sharefs_read } },
	{ VOPNAME_SEEK,		{ .vop_seek = fs_seek } },
	{ NULL }
};
