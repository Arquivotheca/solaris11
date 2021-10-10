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
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/uvfs.h>
#include <sys/types.h>
#include <sys/vfs.h>
#include <sys/debug.h>
#include <sys/vfs_opreg.h>
#include <sys/vnode.h>
#include <sys/flock.h>
#include <sys/filio.h>
#include <sys/sunddi.h>
#include <sys/sysmacros.h>
#include <sys/vmsystm.h>
#include <fs/fs_subr.h>
#include <sys/uvfs_vfsops.h>
#include <sys/uvfs_uvnode.h>
#include <sys/uvfs_uvdir.h>
#include <sys/uvfs_upcall.h>
#include <sys/gfs.h>
#include <vm/pvn.h>
#include <vm/seg_vn.h>
#include <vm/seg_map.h>
#include <vm/seg_kpm.h>
#include <sys/stat.h>
#include <sys/policy.h>
#include <sys/cred.h>
#include <sys/dnlc.h>

static int
uvfs_use_direct(vnode_t *vp)
{
	uvnode_t *uvp = VTOUV(vp);
	uvfsvfs_t *uvfsvfs = uvp->uv_uvfsvfs;

	if ((! (uvp->uv_flags & UVNODE_FLAG_DIRECTIO)) &&
	    (! (uvfsvfs->uvfs_flags & UVFS_FORCEDIRECT)))
		return (B_FALSE);

	if (vn_has_cached_data(vp))
		return (B_FALSE);

	if (uvp->uv_mapcnt != 0)
		return (B_FALSE);

	return (B_TRUE);
}

void
uvfs_update_attrs(uvnode_t *uvp, libuvfs_stat_t *stat)
{
	uvp->uv_pfid = stat->l_pfid;
	if (!uvp->uv_size_known) {
		uvp->uv_size = stat->l_size;
		uvp->uv_blocks = stat->l_blocks;
		if (!uvfs_use_direct(UVTOV(uvp)))
			uvp->uv_size_known = B_TRUE;
	}
	uvp->uv_blksize = stat->l_blksize;
	uvp->uv_links = stat->l_links;
	uvp->uv_uid = stat->l_uid;
	uvp->uv_gid = stat->l_gid;
	uvp->uv_id = stat->l_id;
	uvp->uv_mode = stat->l_mode;
	uvp->uv_rdev = stat->l_rdev;
	uvp->uv_atime[0] = stat->l_atime[0];
	uvp->uv_atime[1] = stat->l_atime[1];
	uvp->uv_mtime[0] = stat->l_mtime[0];
	uvp->uv_mtime[1] = stat->l_mtime[1];
	uvp->uv_ctime[0] = stat->l_ctime[0];
	uvp->uv_ctime[1] = stat->l_ctime[1];
}

/*
 * Clear Set-UID/Set-GID bits if not privileged and at least one of
 * the execute bits is set.  Called after a successful write.
 */
int
uvfs_update_setid(uvnode_t *uvp, cred_t *cr)
{
	libuvfs_stat_t stat;
	vattr_t set_vattr;
	int error;

	if ((uvp->uv_mode & (S_IXUSR | (S_IXUSR >> 3) | (S_IXUSR >> 6))) == 0)
		return (0);

	if ((uvp->uv_mode & (S_ISUID | S_ISGID)) == 0 ||
	    secpolicy_vnode_setid_retain(cr,
	    (uvp->uv_mode & S_ISUID) != 0 && uvp->uv_uid == 0) == 0)
		return (0);

	uvp->uv_mode &= ~(S_ISUID | S_ISGID);
	set_vattr.va_mask = AT_MODE;
	set_vattr.va_mode = uvp->uv_mode;
	if ((error = uvfs_up_setattr(uvp, &set_vattr, &stat, cr)) == 0)
		uvfs_update_attrs(uvp, &stat);

	return (error);
}

/* ARGSUSED */
static int
uvfs_open(vnode_t **vp, int flag, cred_t *cr, caller_context_t *ct)
{
	uvnode_t *uvp = VTOUV(*vp);
	uvfsvfs_t *uvfsvfsp = uvp->uv_uvfsvfs;
	int error;

	/*
	 * We only track open counts for real roots
	 */
	if (! (uvfsvfsp->uvfs_flags & UVFS_FLAG_MOUNT_COMPLETE))
		return (0);

	mutex_enter(&uvp->uv_open_lock);
	if ((error = uvfs_up_open(uvp, flag,
	    uvp->uv_opencnt + 1, cr)) == 0) {
		uvp->uv_opencnt++;
	}
	mutex_exit(&uvp->uv_open_lock);
	return (error);
}

/* ARGSUSED */
static int
uvfs_access(vnode_t *vp, int mode, int flag, cred_t *cr,
    caller_context_t *ct)
{
	return (uvfs_access_check(VTOUV(vp), mode, cr));
}

/* ARGSUSED */
static int
uvfs_close(vnode_t *vp, int flag, int count, offset_t offset, cred_t *cr,
    caller_context_t *ct)
{
	int error = 0;
	uvnode_t *uvp = VTOUV(vp);
	uvfsvfs_t *uvfsvfsp = uvp->uv_uvfsvfs;

	cleanlocks(vp, ddi_get_pid(), 0);
	cleanshares(vp, ddi_get_pid());

	if (count == 1 && (uvfsvfsp->uvfs_flags & UVFS_FLAG_MOUNT_COMPLETE)) {
		mutex_enter(&uvp->uv_open_lock);
		if (uvp->uv_opencnt == 1) {
			(void) pvn_vplist_dirty(vp, 0, uvfs_putapage,
			    B_INVAL|B_FORCE, cr);
		}
		error = uvfs_up_close(uvp, flag, uvp->uv_opencnt - 1,
		    offset, cr);
		if (uvp->uv_opencnt > 0)
			uvp->uv_opencnt--;
		mutex_exit(&uvp->uv_open_lock);
	}
	return (error);
}

/* ARGSUSED */
static int
uvfs_getattr(vnode_t *vp, vattr_t *vap, int flags, cred_t *cr,
    caller_context_t *ct)
{
	uvnode_t *uvp = VTOUV(vp);
	int error;

	if (vn_has_cached_data(vp))
		(void) pvn_vplist_dirty(vp, 0, uvfs_putapage, 0, cr);

	vap->va_type = vp->v_type;
	vap->va_fsid = vp->v_vfsp->vfs_dev;
	vap->va_rdev = vp->v_rdev;

	error = uvfs_up_getattr(uvp, vap, cr);

	return (error);
}

/* ARGSUSED */
static int
uvfs_lookup(vnode_t *dvp, char *nm, vnode_t **vpp, struct pathname *pnp,
    int flags, vnode_t *rdir, cred_t *cr, caller_context_t *ct,
    int *direntflags, pathname_t *realpnp)
{
	int error;

	if (flags & LOOKUP_XATTR) {
		return (EINVAL);
	}

	/*
	 * First check access of directory
	 */

	error = VOP_ACCESS(dvp, VEXEC, 0, cr, NULL);
	if (error)
		return (error);

	error = uvfs_dirlook(VTOUV(dvp), nm, vpp, cr);

	if (error == 0 && IS_DEVVP(*vpp)) {
		vnode_t *svp;

		svp = specvp(*vpp, (*vpp)->v_rdev, (*vpp)->v_type, cr);
		VN_RELE(*vpp)
		if (svp == NULL)
			error = ENOSYS;
		*vpp = svp;
	}
	return (error);
}

/* ARGSUSED */
static int
uvfs_readdir(vnode_t *dvp, uio_t *uiop, cred_t *cr, int *eofp,
    caller_context_t *ct, int flags)
{
	return (uvfs_up_readdir(VTOUV(dvp), uiop, eofp, cr));
}

/* ARGSUSED */
static int
uvfs_pathconf(vnode_t *vp, int cmd, ulong_t *valp, cred_t *cr,
    caller_context_t *ct)
{
	switch (cmd) {
	case _PC_LINK_MAX:
		*valp = ULONG_MAX;
		break;
	case _PC_FILESIZEBITS:
		*valp = 64;
		break;
	case _PC_TIMESTAMP_RESOLUTION:
		*valp = 1L;
		break;
	case _PC_SATTR_ENABLED:
	case _PC_SATTR_EXISTS:
		*valp = 1;
		break;
	default:
		return (fs_pathconf(vp, cmd, valp, cr, ct));
	}

	return (0);
}

/* ARGSUSED */
static int
uvfs_create(vnode_t *dvp, char *name, vattr_t *vap, vcexcl_t excl,
    int mode, vnode_t **vpp, cred_t *cr, int flag, caller_context_t *ct,
    vsecattr_t *vsecp)
{
	uvnode_t *uvp;
	libuvfs_stat_t stat;
	libuvfs_fid_t fid;
	int error;
	vnode_t *vp = NULL;

	*vpp = NULL;

top:
	if ((error = uvfs_dirlook(VTOUV(dvp), name, &vp, cr)) != 0) {
		if (error != ENOENT)
			goto out;

		if ((error = VOP_ACCESS(dvp, VWRITE|VEXEC, 0, cr, ct)) != 0)
			return (error);

		/*
		 * If device file make sure it will fit in 32 bit dev_t
		 * We don't support 64 bit dev_t's with uvfs
		 */
		if (vap->va_type == VBLK || vap->va_type == VCHR) {
			dev_t dev = vap->va_rdev;
			dev32_t dev32;

			if (!cmpldev(&dev32, dev)) {
				error = EOVERFLOW;
				goto out;
			}
		}
		error = uvfs_up_create(VTOUV(dvp), name, vap, mode,
		    &fid, &stat, cr);
		if (error == 0) {
			error = uvfs_uvget(dvp->v_vfsp, &uvp,
			    &fid, &stat);
			if (error == 0) {
				*vpp = UVTOV(uvp);
				dnlc_update(dvp, name, *vpp);
			}
		} else if (error == EEXIST) {
			if (excl != EXCL)
				goto top;
		}

	} else { /* file already exists */
		if (excl == EXCL) {
			error = EEXIST;
			goto out;
		}

		if ((vp->v_type == VDIR) && (mode & S_IWRITE)) {
			error = EISDIR;
			goto out;
		}

		if (mode && (error = VOP_ACCESS(vp, mode, 0, cr, ct))) {
			goto out;
		}

		if ((vp->v_type == VREG) && (vap->va_mask & AT_SIZE) &&
		    (vap->va_size == 0)) {
			error = uvfs_freesp(VTOUV(vp), vap->va_size, 0, 0, cr);
			if (error)
				goto out;

			vnevent_create(vp, ct);
		}
		*vpp = vp;
	}

out:
	if (error && vp)
		VN_RELE(vp);

	if (error == 0 && IS_DEVVP(*vpp)) {
		vnode_t *svp;

		svp = specvp(*vpp, (*vpp)->v_rdev, (*vpp)->v_type, cr);
		VN_RELE(*vpp);
		if (svp == NULL)
			error = ENOSYS;
		*vpp = svp;
	}

	return (error);
}

/* ARGSUSED */
static int
uvfs_mkdir(vnode_t *dvp, char *dirname, vattr_t *vap, vnode_t **vpp, cred_t *cr,
    caller_context_t *ct, int flags, vsecattr_t *vsecp)
{
	uvnode_t *uvp;
	libuvfs_stat_t stat;
	libuvfs_fid_t fid;
	int error;

	if ((error = VOP_ACCESS(dvp, VEXEC|VWRITE, 0, cr, ct)))
		return (error);

	*vpp = NULL;
	error = uvfs_up_mkdir(VTOUV(dvp), dirname, &fid, vap, &stat, cr);
	if (error == 0) {
		error = uvfs_uvget(dvp->v_vfsp, &uvp, &fid, &stat);
		if (error == 0) {
			*vpp = UVTOV(uvp);
			dnlc_update(dvp, dirname, *vpp);
		}
	}
	return (error);
}

/* ARGSUSED */
static int
uvfs_setattr(vnode_t *vp, vattr_t *vap, int flags, cred_t *cr,
    caller_context_t *ct)
{
	uvnode_t *uvp = VTOUV(vp);
	int error;
	libuvfs_stat_t stat;
	vattr_t oldva;
	vattr_t set_vattr;

	oldva.va_mask = AT_ALL;
	error = uvfs_up_getattr(uvp, &oldva, cr);
	if (error)
		return (error);

	error = secpolicy_vnode_setattr(cr, vp, vap, &oldva, flags,
	    (int (*) (void *, int, cred_t *))uvfs_access_check, VTOUV(vp));

	set_vattr = *vap;

	if (error)
		return (error);

	if (set_vattr.va_mask & AT_SIZE) {
		error = uvfs_freesp(VTOUV(vp), vap->va_size, 0, 0, cr);
		set_vattr.va_mask &= ~AT_SIZE;
	}

	if (error == 0 && set_vattr.va_mask)
		error = uvfs_up_setattr(VTOUV(vp), &set_vattr, &stat, cr);

	if (error == 0)
		uvfs_update_attrs(VTOUV(vp), &stat);

	return (error);
}

static int
uvfs_paged_write(vnode_t *vp, uio_t *uiop, int ioflag, cred_t *cr,
	caller_context_t *ct, int remainder)
{
	uvnode_t	*uvp = VTOUV(vp);
	uvfsvfs_t	*uvfsvfsp = VTOUV(vp)->uv_uvfsvfs;
	u_offset_t	offset;
	long		pageoff;
	ssize_t		bytes;
	ssize_t		bytes_written;
	int		error;
	uint_t		flags;
	ssize_t		saved_resid;
	boolean_t	size_change = B_FALSE;
	uint64_t	old_size;
	int		pagecreate, newpage;

	bytes_written = 0;
	do {
		boolean_t cleared = B_FALSE;

		offset = uiop->uio_offset;
		pageoff = offset & (u_offset_t)PAGEOFFSET;
		bytes = MIN(PAGESIZE - pageoff, uiop->uio_resid);

		pagecreate = (bytes == PAGESIZE) || (uvp->uv_size == 0) ||
		    ((pageoff == 0) && ((pageoff + bytes) >= uvp->uv_size));

		if (offset + bytes > uvp->uv_size) {
			old_size = uvp->uv_size;
			size_change = B_TRUE;
			/* uv_size now contains the new size */
			uvp->uv_size = offset + bytes;
		}

		/*
		 * Save the original uiop value so that we can reset it
		 * in the case of an error.
		 */
		saved_resid = uiop->uio_resid;

		/*
		 * If we are here we know that vpm_enable is set to 1.
		 * This is because at mount time we turn on the file
		 * system's forcedirectio flag if vpm_enable is 0.
		 */
		ASSERT(vpm_enable == 1);

		newpage = 0;
		error = vpm_data_copy(vp, offset, bytes,
		    uiop, !pagecreate, &newpage, 0, S_WRITE);
		ASSERT(error || error == 0 && vn_has_cached_data(vp));

		if (error) {
			/* Set the appropriate flags */
			if (!size_change && !newpage) {
				flags = SM_DESTROY;
			} else {
				flags = SM_INVAL;
			}

			/*
			 * Reset the size, if needed
			 */
			if (size_change)
				uvp->uv_size = old_size;

			(void) vpm_sync_pages(vp, offset,
			    bytes, flags);
		} else {
			flags = 0;

			bytes_written += bytes;
			if ((ioflag & (FSYNC|FDSYNC))) {
				flags = SM_WRITE | SM_DONTNEED;
			} else if (bytes_written >= uvfsvfsp->uvfs_write_size) {
				/*
				 * If we have written a full block, start an
				 * asynchronous write.
				 */
				flags = SM_WRITE | SM_ASYNC | SM_DONTNEED;
				bytes_written = 0;
			}

			error = vpm_sync_pages(vp, offset, bytes,
			    flags);
		}

		if (!cleared && error == 0) {
			cleared = B_TRUE;
			error = uvfs_update_setid(uvp, cr);
		}

	} while (error == 0 && uiop->uio_resid > 0);

	/*
	 * If the write is a synchronous one, call VOP_FSYNC.
	 * This will result in an upcall to userspace in order
	 * to allow those callbacks to take the action needed
	 * in order to handle the synchronous write correctly.
	 */
	if (ioflag & (FSYNC | FDSYNC)) {
		(void) VOP_FSYNC(vp, FSYNC, cr, ct);
	}

	/*
	 * If there was something written, even if it was a short write,
	 * return success.
	 */

	if ((saved_resid + remainder) != uiop->uio_resid)
		error = 0;

	/*
	 * Explanation of the values that uio_resid is getting set
	 * to below:
	 *
	 * Most of the time "remainder" will be zero.  "remainder"
	 * is only non-zero when we determined (in uvfs_write_check())
	 * that this write request would cause us to go past the
	 * file size limit.  If the write will cause us to go past
	 * this limit, uvfs_write_check() reduces uio_resid by the
	 * value of "remainder".
	 *
	 * Therefore, if there was an error, we need to make sure
	 * to add "remainder" back in to correctly reset uio_resid
	 * to its original value.
	 *
	 * If there wasn't an error, we also have to add "remainder"
	 * back in so that the uio_resid always contains the correct
	 * amount of data that there is left to write.
	 */
	uiop->uio_resid += remainder;

	return (error);
}

static int
uvfs_write_check(vnode_t *vp, uio_t *uiop, int ioflag, int *remainder,
    caller_context_t *ct)
{
	uvnode_t *uvp = VTOUV(vp);
	rlim64_t limit = uiop->uio_llimit;
	int error;

	ASSERT(vp->v_type == VREG);
	ASSERT(RW_WRITE_HELD(&uvp->uv_rwlock));

	if (IS_SWAPVP(vp))
		return (EINVAL);

	if (uiop->uio_resid == 0)
		return (0);

	if (MANDLOCK(vp, uvp->uv_mode)) {
		error = chklock(vp, FWRITE, uiop->uio_loffset, uiop->uio_resid,
		    uiop->uio_fmode, ct);
		if (error)
			return (error);
	}

	if (ioflag & FAPPEND) {
		/* XXX getattr to ensure current size */
		uiop->uio_loffset = uvp->uv_size;
	}

	if (uiop->uio_loffset < 0)
		return (EINVAL);

	/*
	 * Check to make sure that the process will not exceed
	 * its limit on file size.  It is okay to write up to
	 * the limit, but not beyond.  Thus, the write which
	 * reaches the limit will be short and the next write
	 * will return an error.
	 */
	if (limit == RLIM64_INFINITY || limit > MAXOFFSET_T)
		limit = MAXOFFSET_T;

	if (uiop->uio_loffset + uiop->uio_resid > limit) {
		/*
		 * "remainder" represents how much over the file size limit
		 * the requested write would go.
		 */
		*remainder = uiop->uio_loffset + uiop->uio_resid - limit;

		/*
		 * The uio_resid will hold the amount of data that can be
		 * written before we reach the file size limit.
		 */
		uiop->uio_resid = limit - uiop->uio_loffset;
		if (uiop->uio_resid <= 0) {
			proc_t *p = ttoproc(curthread);

			uiop->uio_resid += *remainder;
			mutex_enter(&p->p_lock);
			(void) rctl_action(rctlproc_legacy[RLIMIT_FSIZE],
			    p->p_rctls, p, RCA_UNSAFE_SIGINFO);
			mutex_exit(&p->p_lock);
			return (EFBIG);
		}
	} else {
		*remainder = 0;
	}

	return (0);
}

static int
uvfs_write(vnode_t *vp, uio_t *uiop, int ioflag, cred_t *cr,
    caller_context_t *ct)
{
	int remainder;
	int error;

	error = uvfs_write_check(vp, uiop, ioflag, &remainder, ct);
	if (error != 0)
		return (error);

	if (uvfs_use_direct(vp))
		return (uvfs_up_direct_write(vp, uiop, cr, remainder));

	return (uvfs_paged_write(vp, uiop, ioflag, cr, ct, remainder));
}

/*ARGSUSED*/
static int
uvfs_paged_read(vnode_t *vp, uio_t *uio, caller_context_t *ct)
{
	uvnode_t	*uvp = VTOUV(vp);
	int		error = 0, error2 = 0;
	ssize_t		bytes;

	ASSERT(RW_READ_HELD(&uvp->uv_rwlock));

	do {
		long diff;
		long offset;
		pgcnt_t pageoffset;

		offset = uio->uio_offset;
		pageoffset = offset & PAGEOFFSET;
		bytes = MIN(PAGESIZE - pageoffset, uio->uio_resid);

		diff = uvp->uv_size - offset;

		if (diff <= 0) {
			error = 0;
			break;
		}

		if (diff < bytes)
			bytes = diff;

		/*
		 * If we are here we know that vpm_enable is set to 1.
		 * This is because at mount time we turn on the file
		 * system's forcedirectio flag if vpm_enable is 0.
		 */
		ASSERT(vpm_enable == 1);
		error = vpm_data_copy(vp, offset, bytes, uio, 1,
		    NULL, 0, S_READ);
		error2 = vpm_sync_pages(vp, offset, PAGESIZE, 0);

	} while (error == 0 && uio->uio_resid > 0);

	return (error != 0 ? error : error2);
}

/* ARGSUSED */
static int
uvfs_read(vnode_t *vp, uio_t *uio, int ioflag, cred_t *cr,
    caller_context_t *ct)
{
	uvnode_t *uvp = VTOUV(vp);
	int error;

	if (uio->uio_resid == 0)
		return (0);

	if (uio->uio_loffset < 0)
		return (EINVAL);

	if (MANDLOCK(vp, uvp->uv_mode)) {
		error = chklock(vp, FREAD, uio->uio_loffset, uio->uio_resid,
		    uio->uio_fmode, ct);
		if (error)
			return (error);
	}

	/*
	 * If the read is done with FRSYNC, sync out any data before
	 * reading.
	 */
	if (ioflag & FRSYNC)
		(void) VOP_FSYNC(vp, FSYNC, cr, ct);

	if (uvfs_use_direct(vp))
		return (uvfs_up_direct_read(vp, uio, cr));

	return (uvfs_paged_read(vp, uio, ct));
}

caddr_t
uvfs_map_page(page_t *pp, enum seg_rw rw)
{
	if (kpm_enable)
		return (hat_kpm_mapin(pp, 0));
	ASSERT(rw == S_READ || rw == S_WRITE);
	return (ppmapin(pp, PROT_READ | ((rw == S_WRITE) ? PROT_WRITE : 0),
	    (caddr_t)-1));
}

void
uvfs_unmap_page(page_t *pp, caddr_t addr)
{
	if (kpm_enable) {
		hat_kpm_mapout(pp, 0, addr);
	} else {
		ppmapout(addr);
	}
}

/* ARGSUSED */
static int
uvfs_getapage(vnode_t *vp, u_offset_t off, size_t len, uint_t *protp,
    page_t *pl[], size_t plsz, struct seg *seg, caddr_t addr, enum seg_rw rw,
    cred_t *cr)
{
	struct page *pp;
	int error = 0;
	u_offset_t io_off;
	size_t io_len;
	ulong_t uvfs_ksize;
	size_t klen;
	uvnode_t *uvp = VTOUV(vp);
	uvfsvfs_t *uvfsvfsp = VTOUV(vp)->uv_uvfsvfs;
	struct buf *bp;

	/*
	 * Determine the kluster size to use during page klustering
	 */
	uvfs_ksize = MAX(uvfsvfsp->uvfs_read_size, plsz);

	if (protp != NULL)
		*protp = PROT_ALL;

	/* We don't do read ahead yet */
	if (pl == NULL)
		return (0);

	pl[0] = NULL;

top:
	bp = NULL;
	pp = NULL;

	if (pp = page_lookup(vp, off, rw == S_CREATE ? SE_EXCL : SE_SHARED)) {
		/*
		 * Page already exists
		 */
		pl[0] = pp;
		pl[1] = NULL;
	} else {
		if (uvfs_ksize <= PAGESIZE) {
			io_off = off;
			io_len = PAGESIZE;

			pp = page_create_va(vp, io_off, io_len, PG_WAIT, seg,
			    addr);
		} else {
			/*
			 * Round the kluster size up to the nearest page
			 * boundary
			 */
			klen = P2ROUNDUP(uvfs_ksize, PAGESIZE);
			if (off + klen > uvp->uv_size) {
				klen = PAGESIZE;
			}

			pp = pvn_read_kluster(vp, off, seg, addr, &io_off,
			    &io_len, off, klen, 0);
		}

		/* Someone may have created the page before we did. */
		if (pp == NULL)
			goto top;

		if (rw != S_CREATE)
			pagezero(pp, 0, PAGESIZE);

		bp = pageio_setup(pp, io_len, vp, B_READ);

		bp->b_edev = 0;
		bp->b_dev = 0;
		bp->b_lblkno = lbtodb(io_off);
		bp->b_file = vp;
		bp->b_offset = (offset_t)off;
		bp_mapin(bp);

		error = uvfs_up_read(VTOUV(vp), bp->b_un.b_addr, bp->b_bcount,
		    io_off, cr, &io_len);

		bp_mapout(bp);
		pageio_done(bp);

		if (error) {
			pvn_read_done(pp, B_ERROR);
		} else {
			if (pl) {
				uint_t xlen = io_len & PAGEOFFSET;

				if (xlen != 0)
					pagezero(pp->p_prev, xlen,
					    PAGESIZE - xlen);

				pvn_plist_init(pp, pl, plsz, off, io_len, rw);
			}
		}
	}
	return (error);
}

/* ARGSUSED */
static int
uvfs_getpage(vnode_t *vp, offset_t off, size_t len, uint_t *protp,
    page_t *pl[], size_t plsz, struct seg *seg, caddr_t addr,
    enum seg_rw rw, cred_t *cr, caller_context_t *ct)
{
	int error;

	if (len > PAGESIZE)
		error = pvn_getpages(uvfs_getapage, vp, off, len,
		    protp, pl, plsz, seg, addr, rw, cr);
	else
		error = uvfs_getapage(vp, off, len, protp, pl, plsz,
		    seg, addr, rw, cr);

	return (error);
}

/*ARGSUSED*/
static int
uvfs_assert_putapage(vnode_t *vp, page_t *pp, u_offset_t *offp, size_t *lenp,
    int flags, cred_t *cr)
{
	ASSERT(0);
	return (0);
}

/*
 * Push a page up to userland for writing, klustering if possible.
 */
/* ARGSUSED */
int
uvfs_putapage(vnode_t *vp, page_t *pp, u_offset_t *offp, size_t *lenp,
    int flags, cred_t *cr)
{
	int		error;
	uvnode_t	*uvp = VTOUV(vp);
	uvfsvfs_t	*uvfsvfsp = VTOUV(vp)->uv_uvfsvfs;
	u_offset_t	io_off;
	size_t		io_len;
	ulong_t		uvfs_ksize;
	u_offset_t	koff;
	size_t		klen;
	struct buf	*bp = NULL;

	uvfs_ksize = uvfsvfsp->uvfs_write_size;

	/*
	 * Initialize io_off and io_len.  If klustering is possible, these
	 * values will be changed below.
	 */
	io_off = pp->p_offset;
	io_len = MIN(PAGESIZE, uvp->uv_size - io_off);

	/*
	 * Kluster pages together if our kluster (block) size is bigger
	 * than the page size.
	 */
	if (io_off < uvp->uv_size && uvfs_ksize > PAGESIZE) {
		/* Round the kluster size up to the nearest page boundary */
		klen = P2ROUNDUP(uvfs_ksize, PAGESIZE);
		/* Round the offset down to the nearest klen boundary */
		koff = ISP2(klen) ? P2ALIGN(io_off, (u_offset_t)klen) : 0;
		ASSERT(koff <= uvp->uv_size);
		if (koff + klen > uvp->uv_size)
			klen = P2ROUNDUP(uvp->uv_size - koff, PAGESIZE);
		pp = pvn_write_kluster(vp, pp, &io_off, &io_len, koff, klen,
		    flags);
	}

	/*
	 * Can't push pages past end-of-file
	 */
	if (io_off >= uvp->uv_size) {
		/* Ignore all pages */
		error = 0;
		goto out;
	} else if (io_off + io_len > uvp->uv_size) {
		int npages = btopr(uvp->uv_size - io_off);
		page_t *trunc;

		page_list_break(&pp, &trunc, npages);

		/* Ignore and toss pages past end-of-file */
		if (trunc)
			pvn_write_done(trunc, flags);

		io_len = uvp->uv_size - io_off;
	}

	bp = pageio_setup(pp, io_len, vp, B_WRITE);

	bp->b_edev = 0;
	bp->b_dev = 0;
	bp->b_lblkno = lbtodb(io_off);
	bp->b_file = vp;
	bp->b_offset = (offset_t)io_off;

	bp_mapin(bp);

	error = uvfs_up_write(VTOUV(vp), bp->b_un.b_addr, bp->b_bcount,
	    bp->b_offset, cr);

	bp_mapout(bp);
	pageio_done(bp);

out:
	pvn_write_done(pp, (error ? B_ERROR : 0) | B_WRITE | flags);
	if (offp)
		*offp = io_off;
	if (lenp)
		*lenp = io_len;
	return (error);
}

/* ARGSUSED */
static int
uvfs_putpage(vnode_t *vp, offset_t off, size_t len, int flags,
    cred_t *cr, caller_context_t *ct)
{
	page_t		*pp;
	int		error = 0;
	u_offset_t	io_off;
	size_t		io_len = 0;
	uvnode_t	*uvp = VTOUV(vp);
	u_offset_t	eoff;

	if (!vn_has_cached_data(vp))
		return (0);

	if (len == 0) {
		error = pvn_vplist_dirty(vp, off, uvfs_putapage,
		    flags, cr);
	} else {

		eoff = MIN(off+len, uvp->uv_size);
		for (io_off = off; io_off < eoff; io_off += io_len) {
			if ((flags & B_INVAL) || ((flags & B_ASYNC) == 0)) {
				pp = page_lookup(vp, io_off,
				    (flags & (B_INVAL | B_FREE)) ?
				    SE_EXCL : SE_SHARED);
			} else {
				pp = page_lookup_nowait(vp, io_off,
				    (flags & B_FREE) ? SE_EXCL : SE_SHARED);
			}

			if (pp == NULL || pvn_getdirty(pp, flags) == 0) {
				io_len = PAGESIZE;
			} else {
				error = uvfs_putapage(vp, pp, &io_off, &io_len,
				    flags, cr);
				if (error != 0)
					break;
			}
		}
	}
	return (error);
}

/* ARGSUSED */
void
uvfs_inactive(vnode_t *vp, cred_t *cr, caller_context_t *ct)
{
	uvnode_t *uvp = VTOUV(vp);
	uvfsvfs_t *uvfsvfs = uvp->uv_uvfsvfs;
	vfs_t	*vfsp = vp->v_vfsp;
	uint64_t idx;

	idx = uvfs_hash_lock(uvfsvfs->uvfs_vfsp, &uvp->uv_fid);
	mutex_enter(&vp->v_lock);
	vp->v_count--;
	if (vp->v_count > 0) {
		uvfs_hash_unlock(idx);
		mutex_exit(&vp->v_lock);
		return;
	}
	mutex_exit(&vp->v_lock);
	/*
	 * We shouldn't have any dirty pages at this point.
	 */
	if (vn_has_cached_data(vp)) {
		if ((uvfsvfs->uvfs_flags & UVFS_UNMOUNTED) || sys_shutdown) {
			(void) pvn_vplist_dirty(vp, 0, NULL,
			    B_INVAL | B_TRUNC | B_FORCE, cr);
		} else {
			(void) pvn_vplist_dirty(vp, 0, uvfs_assert_putapage,
			    B_INVAL | B_FORCE, cr);
		}
	}

	ASSERT(!vn_has_cached_data(vp));
	ASSERT3U(uvp->uv_opencnt, ==, 0);

	uvfs_hash_remove(uvp, idx);
	vn_free(vp);
	if (uvp->uv_door != NULL)
		door_ki_rele(uvp->uv_door);
	uvfs_uvnode_free(uvp);
	uvfs_hash_unlock(idx);
	VFS_RELE(vfsp);
}

/*ARGSUSED*/
static int
uvfs_addmap(vnode_t *vp, offset_t off, struct as *as, caddr_t addr,
    size_t len, uchar_t prot, uchar_t maxprot, uint_t flags, cred_t *cr,
    caller_context_t *ct)
{
	uvnode_t *uvp = VTOUV(vp);
	uint64_t pages = btopr(len);

	mutex_enter(&uvp->uv_open_lock);
	atomic_add_64(&uvp->uv_mapcnt, pages);
	(void) uvfs_up_addmap(uvp, ++(uvp->uv_opencnt), cr);
	mutex_exit(&uvp->uv_open_lock);
	return (0);
}

/*ARGSUSED*/
static int
uvfs_delmap(vnode_t *vp, offset_t off, struct as *as, caddr_t addr,
    size_t len, uint_t prot, uint_t maxprot, uint_t flags, cred_t *cr,
    caller_context_t *ct)
{
	uvnode_t *uvp = VTOUV(vp);
	uint64_t pages = btopr(len);

	ASSERT3U(uvp->uv_mapcnt, >=, pages);
	atomic_add_64(&uvp->uv_mapcnt, -pages);

	if ((flags & MAP_SHARED) && (prot & PROT_WRITE) &&
	    vn_has_cached_data(vp)) {
		(void) VOP_PUTPAGE(vp, off, len, 0, cr, ct);
	}

	mutex_enter(&uvp->uv_open_lock);
	(void) uvfs_up_delmap(uvp, --(uvp->uv_opencnt), cr);
	mutex_exit(&uvp->uv_open_lock);

	return (0);
}

/*ARGSUSED*/
static int
uvfs_map(vnode_t *vp, offset_t off, struct as *as, caddr_t *addrp,
    size_t len, uchar_t prot, uchar_t maxprot, uint_t flags, cred_t *cr,
    caller_context_t *ct)
{
	uvnode_t	*uvp = VTOUV(vp);
	segvn_crargs_t	vn_a;
	int		error;

	if (vp->v_flag & VNOMAP) {
		return (ENOSYS);
	}

	if (off < 0 || len > MAXOFFSET_T - off) {
		return (ENXIO);
	}

	if (vp->v_type != VREG) {
		return (ENODEV);
	}

	/*
	 * If file is locked, disallow mapping.
	 */
	if (MANDMODE(uvp->uv_mode) && vn_has_flocks(vp)) {
		return (EAGAIN);
	}

	as_rangelock(as);
	error = choose_addr(as, addrp, len, off, ADDR_VACALIGN, flags);
	if (error != 0) {
		as_rangeunlock(as);
		return (error);
	}

	vn_a.vp = vp;
	vn_a.offset = (u_offset_t)off;
	vn_a.type = flags & MAP_TYPE;
	vn_a.prot = prot;
	vn_a.maxprot = maxprot;
	vn_a.cred = cr;
	vn_a.amp = NULL;
	vn_a.flags = flags & ~MAP_TYPE;
	vn_a.szc = 0;
	vn_a.lgrp_mem_policy_flags = 0;

	error = as_map(as, *addrp, len, segvn_create, &vn_a);

	as_rangeunlock(as);
	return (error);
}

/*ARGSUSED*/
static int
uvfs_seek(vnode_t *vp, offset_t ooff, offset_t *noffp, caller_context_t *ct)
{
	if (vp->v_type == VDIR)
		return (0);
	return ((*noffp < 0 || *noffp > MAXOFFSET_T) ? EINVAL : 0);
}

/*
 * Decide whether it is okay to remove within a sticky directory.
 *
 * In sticky directories, write access is not sufficient;
 * you can remove entries from a directory only if:
 *
 *      you own the directory,
 *      you own the entry,
 *      the entry is a plain file and you have write access,
 *      or you are privileged (checked in secpolicy...).
 *
 * The function returns 0 if remove access is granted.
 */
static int
uvfs_sticky_access(uvnode_t *dvp, uvnode_t *vp, cred_t *cr)
{
	uid_t uid;

	if ((dvp->uv_mode & S_ISVTX) == 0)
		return (0);
	if ((uid = crgetuid(cr)) == dvp->uv_uid || uid == vp->uv_uid ||
	    (UVTOV(vp)->v_type == VREG &&
	    uvfs_access_check(vp, VWRITE, cr) == 0))
		return (0);
	return (secpolicy_vnode_remove(cr));
}

/*ARGSUSED*/
static int
uvfs_remove(vnode_t *dvp, char *name, cred_t *cr, caller_context_t *ct,
    int flags)
{
	vnode_t *vp;
	int error;

	/*
	 * Find uvnode for file to remove
	 */

	error = uvfs_dirlook(VTOUV(dvp), name, &vp, cr);
	if (error)
		return (error);

	if (vp->v_type == VDIR) {
		VN_RELE(vp);
		return (EPERM);
	}

	if ((error = VOP_ACCESS(dvp, VEXEC|VWRITE, 0, cr, ct)) != 0 ||
	    (error = uvfs_sticky_access(VTOUV(dvp), VTOUV(vp), cr)) != 0) {
		VN_RELE(vp);
		return (error);
	}

	vnevent_remove(dvp, vp, name, ct);

	error = uvfs_up_remove(VTOUV(dvp), name, cr);

	if (error == 0)
		dnlc_remove(dvp, name);

	VN_RELE(vp);

	return (error);
}

/*ARGSUSED*/
static int
uvfs_rmdir(vnode_t *dvp, char *name, vnode_t *cwd, cred_t *cr,
    caller_context_t *ct, int flags)
{
	vnode_t *vp = NULL;
	int error;

	error = uvfs_dirlook(VTOUV(dvp), name, &vp, cr);

	if (error)
		return (error);

	if (vp->v_type != VDIR) {
		error = ENOTDIR;
		goto out;
	}

	if (vp == cwd) {
		error = EINVAL;
		goto out;
	}

	if (vn_vfswlock(vp) || vn_ismntpt(vp)) {
		error = EBUSY;
		goto out;
	}

	if ((error = VOP_ACCESS(dvp, VEXEC|VWRITE, 0, cr, ct)) != 0) {
		vn_vfsunlock(vp);
		goto out;
	}

	vnevent_rmdir(vp, dvp, name, ct);

	error = uvfs_up_rmdir(VTOUV(dvp), name, cr);

	if (error == 0)
		dnlc_remove(dvp, name);

	vn_vfsunlock(vp);
out:

	if (vp)
		VN_RELE(vp);

	return (error);
}

static int
uvfs_rename_valid(uvnode_t *suvp, uvnode_t *tduvp, uvnode_t *sduvp)
{
	int error;
	uvnode_t *uvp = tduvp;
	vfs_t *vfsp = UVTOV(suvp)->v_vfsp;
	vnode_t *parentvp;
	fid_t fid;

	do {
		if (uvfs_fid_match(suvp, vfsp, &uvp->uv_fid)) {
			error = EINVAL;
			break;
		}
		if (uvp->uv_vnode->v_flag & VROOT) {
			error = 0;
			break;
		}

		fid.fid_len = uvp->uv_pfid.uvfid_len;
		bcopy(uvp->uv_pfid.uvfid_data, fid.fid_data, fid.fid_len);

		if ((error = VFS_VGET(vfsp, &parentvp, &fid)) != 0)
			break;

		if (uvp != tduvp)
			VN_RELE(UVTOV(uvp));

		uvp = VTOUV(parentvp);
	} while (!uvfs_fid_match(uvp, vfsp, &sduvp->uv_fid));

	if (uvp != tduvp)
		VN_RELE(UVTOV(uvp));
	return (error);
}

/*ARGSUSED*/
static int
uvfs_rename(vnode_t *sdvp, char *snm, vnode_t *tdvp, char *tnm, cred_t *cr,
    caller_context_t *ct, int flags)
{
	vnode_t *realvp;
	vnode_t *svp = NULL;
	vnode_t *tvp = NULL;
	int error;

	if (VOP_REALVP(tdvp, &realvp, ct) == 0)
		tdvp = realvp;

	if (tdvp->v_vfsp != sdvp->v_vfsp)
		return (EXDEV);

	/* find source vnode */
	error = uvfs_dirlook(VTOUV(sdvp), snm, &svp, cr);
	if (error) {
		if (strcmp(snm, "..") == 0)
			return (EINVAL);
		else
			return (error);
	}

	error = uvfs_dirlook(VTOUV(tdvp), tnm, &tvp, cr);

	if (error && error != ENOENT) {
		VN_RELE(svp);

		if (strcmp(tnm, "..") == 0)
			return (EINVAL);
		return (error);
	}

	/* verify rename is valid */
	if (svp->v_type == VDIR) {
		if (error = uvfs_rename_valid(VTOUV(svp), VTOUV(tdvp),
		    VTOUV(sdvp)))
			goto out;
	}

	/*
	 * See if the target exists.
	 */
	if (tvp) {
		/*
		 * Check if source and target are the same type
		 */
		if (svp->v_type == VDIR) {
			if (tvp->v_type != VDIR) {
				error = ENOTDIR;
				goto out;
			}
		} else {
			if (tvp->v_type == VDIR) {
				error = EISDIR;
				goto out;
			}
		}
	}

	/*
	 * Must have write access at the source to remove the old entry
	 * and write access at the target to create the new entry.
	 * Note that if target and source are the same, this can be
	 * done in a single check.
	 */
	if ((error = VOP_ACCESS(sdvp, VEXEC|VWRITE, 0, cr, ct)) != 0 ||
	    (error = uvfs_sticky_access(VTOUV(sdvp), VTOUV(svp), cr)) != 0)
		goto out;

	if (tdvp != sdvp) {
		error = VOP_ACCESS(tdvp, VEXEC|VWRITE, 0, cr, ct);
		if (error == 0 && tvp)
			error = uvfs_sticky_access(VTOUV(tdvp), VTOUV(tvp), cr);
		if (error != 0)
			goto out;
	}

	vnevent_rename_src(svp, sdvp, snm, ct);

	if (tvp)
		vnevent_rename_dest(tvp, tdvp, tnm, ct);

	if (tdvp != sdvp)
		vnevent_rename_dest_dir(tdvp, ct);

	error = uvfs_up_rename(VTOUV(sdvp), snm, VTOUV(tdvp), tnm, cr);
out:
	if (tvp)
		VN_RELE(tvp);
	if (svp)
		VN_RELE(svp);
	return (error);
}

static int
uvfs_link(vnode_t *tdvp, vnode_t *svp, char *name, cred_t *cr,
    caller_context_t *ct, int flags)
{
	int error;
	vnode_t *vp;
	vnode_t *realvp;

	if (VOP_REALVP(svp, &realvp, ct) == 0)
		svp = realvp;

	error = uvfs_dirlook(VTOUV(tdvp), name, &vp, cr);
	if (error == 0) {
		VN_RELE(vp);
		return (EEXIST);
	}

	if (error = VOP_ACCESS(tdvp, VWRITE|VEXEC, 0, cr, ct)) {
		return (error);
	}

	error = uvfs_up_link(VTOUV(tdvp), VTOUV(svp), name, flags, cr);

	if (error == 0)
		vnevent_link(svp, ct);

	return (error);
}

/*ARGSUSED*/
static int
uvfs_symlink(vnode_t *dvp, char *name, vattr_t *vap, char *link, cred_t *cr,
    caller_context_t *ct, int flags)
{
	uint64_t len;
	vnode_t *vp;
	int error;

	len = strlen(name);
	if (len > MAXPATHLEN)
		return (ENAMETOOLONG);

	error = uvfs_dirlook(VTOUV(dvp), name, &vp, cr);
	if (error == 0) {
		VN_RELE(vp);
		return (EEXIST);
	}

	if ((error = VOP_ACCESS(dvp, VEXEC|VWRITE, 0, cr, ct)) != 0)
		return (error);

	return (uvfs_up_symlink(VTOUV(dvp), name, link, cr));
}

/*ARGSUSED*/
static int
uvfs_readlink(vnode_t *vp, uio_t *uio, cred_t *cr, caller_context_t *ct)
{
	return (uvfs_up_readlink(VTOUV(vp), uio, cr));
}

static int
uvfs_fsync(vnode_t *vp, int syncflag, cred_t *cr, caller_context_t *ct)
{
	if (vn_has_cached_data(vp))
		(void) VOP_PUTPAGE(vp, 0, 0, 0, cr, ct);

	return (uvfs_up_fsync(VTOUV(vp), syncflag, cr));
}

/*ARGSUSED*/
static int
uvfs_rwlock(struct vnode *vp, int write_lock, caller_context_t *ct)
{
	uvnode_t *uvp = VTOUV(vp);

	if (write_lock) {
		rw_enter(&uvp->uv_rwlock, RW_WRITER);
		return (V_WRITELOCK_TRUE);
	} else {
		rw_enter(&uvp->uv_rwlock, RW_READER);
		return (V_WRITELOCK_FALSE);
	}
}

static int
uvfs_frlock(vnode_t *vp, int cmd, flock64_t *bfp, int flag, offset_t offset,
    flk_callback_t *flk_cbp, cred_t *cr, caller_context_t *ct)
{
	uvnode_t *uvp = VTOUV(vp);
	uvfsvfs_t *uvfsvfs = uvp->uv_uvfsvfs;
	int error;

	if (uvp->uv_mapcnt > 0 && MANDMODE(uvp->uv_mode)) {
		return (EAGAIN);
	}

	/*
	 * If unmounted don't allow anything other than unlock requests
	 */
	if ((uvfsvfs->uvfs_flags & UVFS_UNMOUNTED) && bfp->l_type != F_UNLCK)
		return (EIO);

	error = fs_frlock(vp, cmd, bfp, flag, offset, flk_cbp, cr, ct);

	return (error);
}

/*ARGSUSED*/
static int
uvfs_space(vnode_t *vp, int cmd, flock64_t *bfp, int flag,
    offset_t offset, cred_t *cr, caller_context_t *ct)
{
	int error;

	if (cmd != F_FREESP)
		return (EINVAL);

	if (error = convoff(vp, bfp, 0, offset))
		return (error);

	if (bfp->l_len < 0)
		return (EINVAL);

	error = uvfs_freesp(VTOUV(vp), bfp->l_start, bfp->l_len, flag, cr);
	return (error);
}

/*ARGSUSED*/
static void
uvfs_rwunlock(vnode_t *vp, int write_lock, caller_context_t *ct)
{
	uvnode_t *uvp = VTOUV(vp);

	rw_exit(&uvp->uv_rwlock);
}

/* ARGSUSED */
static int
uvfs_ioctl(vnode_t *vp, int cmd, intptr_t arg, int flag, cred_t *cr, int *rvalp,
	caller_context_t *ct)
{
	switch (cmd) {
		case _FIODIRECTIO:
			return (uvfs_directio(vp, (int)arg, cr));
		default:
			return (ENOTTY);
	}
}

vnodeops_t *uvfs_fvnodeops;
const fs_operation_def_t uvfs_fvnodeops_template[] = {
	VOPNAME_OPEN,		{ .vop_open = uvfs_open },
	VOPNAME_CLOSE,		{ .vop_close = uvfs_close },
	VOPNAME_GETATTR,	{ .vop_getattr = uvfs_getattr },
	VOPNAME_SETATTR,	{ .vop_setattr = uvfs_setattr },
	VOPNAME_LOOKUP,		{ .vop_lookup = uvfs_lookup },
	VOPNAME_INACTIVE,	{ .vop_inactive = uvfs_inactive },
	VOPNAME_ACCESS,		{ .vop_access = uvfs_access },
	VOPNAME_PATHCONF,	{ .vop_pathconf = uvfs_pathconf },
	VOPNAME_WRITE,		{ .vop_write = uvfs_write },
	VOPNAME_READ,		{ .vop_read = uvfs_read },
	VOPNAME_GETPAGE,	{ .vop_getpage = uvfs_getpage },
	VOPNAME_PUTPAGE,	{ .vop_putpage = uvfs_putpage },
	VOPNAME_ADDMAP,		{ .vop_addmap = uvfs_addmap },
	VOPNAME_DELMAP,		{ .vop_delmap = uvfs_delmap },
	VOPNAME_MAP,		{ .vop_map = uvfs_map },
	VOPNAME_SEEK,		{ .vop_seek = uvfs_seek },
	VOPNAME_FSYNC,		{ .vop_fsync = uvfs_fsync },
	VOPNAME_RWLOCK,		{ .vop_rwlock = uvfs_rwlock },
	VOPNAME_RWUNLOCK,	{ .vop_rwunlock = uvfs_rwunlock },
	VOPNAME_SPACE,		{ .vop_space = uvfs_space },
	VOPNAME_IOCTL,		{ .vop_ioctl = uvfs_ioctl },
	VOPNAME_FRLOCK,		{ .vop_frlock = uvfs_frlock },
	VOPNAME_VNEVENT,	{ .vop_vnevent = fs_vnevent_support },
	NULL,	NULL
};

vnodeops_t *uvfs_dvnodeops;
const fs_operation_def_t uvfs_dvnodeops_template[] = {
	VOPNAME_OPEN,		{ .vop_open = uvfs_open },
	VOPNAME_CLOSE,		{ .vop_close = uvfs_close },
	VOPNAME_GETATTR,	{ .vop_getattr = uvfs_getattr },
	VOPNAME_SETATTR,	{ .vop_setattr = uvfs_setattr },
	VOPNAME_LOOKUP,		{ .vop_lookup = uvfs_lookup },
	VOPNAME_INACTIVE,	{ .vop_inactive = uvfs_inactive },
	VOPNAME_ACCESS,		{ .vop_access = uvfs_access },
	VOPNAME_PATHCONF,	{ .vop_pathconf = uvfs_pathconf },
	VOPNAME_READDIR,	{ .vop_readdir = uvfs_readdir },
	VOPNAME_MKDIR,		{ .vop_mkdir = uvfs_mkdir },
	VOPNAME_CREATE,		{ .vop_create = uvfs_create },
	VOPNAME_RMDIR,		{ .vop_rmdir = uvfs_rmdir },
	VOPNAME_RENAME,		{ .vop_rename = uvfs_rename },
	VOPNAME_SYMLINK,	{ .vop_symlink = uvfs_symlink },
	VOPNAME_REMOVE,		{ .vop_remove = uvfs_remove },
	VOPNAME_LINK,		{ .vop_link = uvfs_link },
	VOPNAME_SEEK,		{ .vop_seek = uvfs_seek },
	VOPNAME_RWLOCK,		{ .vop_rwlock = uvfs_rwlock },
	VOPNAME_RWUNLOCK,	{ .vop_rwunlock = uvfs_rwunlock },
	VOPNAME_VNEVENT,	{ .vop_vnevent = fs_vnevent_support },
	NULL,	NULL
};

vnodeops_t *uvfs_symvnodeops;
const fs_operation_def_t uvfs_symvnodeops_template[] = {
	VOPNAME_OPEN,		{ .vop_open = uvfs_open },
	VOPNAME_CLOSE,		{ .vop_close = uvfs_close },
	VOPNAME_GETATTR,	{ .vop_getattr = uvfs_getattr },
	VOPNAME_SETATTR,	{ .vop_setattr = uvfs_setattr },
	VOPNAME_LOOKUP,		{ .vop_lookup = uvfs_lookup },
	VOPNAME_INACTIVE,	{ .vop_inactive = uvfs_inactive },
	VOPNAME_READLINK,	{ .vop_readlink = uvfs_readlink },
	VOPNAME_ACCESS,		{ .vop_access = uvfs_access },
	VOPNAME_VNEVENT,	{ .vop_vnevent = fs_vnevent_support },
	NULL,	NULL
};

vnodeops_t *uvfs_evnodeops;
const fs_operation_def_t uvfs_evnodeops_template[] = {
	VOPNAME_INACTIVE,	{ .vop_inactive = uvfs_inactive },
	NULL,	NULL
};

vnodeops_t *uvfs_eio_vnodeops;
const fs_operation_def_t uvfs_eio_vnodeops_template[] = {
	VOPNAME_INACTIVE,	{ .vop_inactive = uvfs_inactive },
	VOPNAME_OPEN,		{ .vop_open = fs_eio },
	VOPNAME_WRITE,		{ .vop_write = fs_eio },
	VOPNAME_READ,		{ .vop_read = fs_eio },
	VOPNAME_CLOSE,		{ .vop_close = uvfs_close },
	VOPNAME_GETATTR,	{ .vop_getattr = fs_eio },
	VOPNAME_SETATTR,	{ .vop_setattr = fs_eio },
	VOPNAME_LOOKUP,		{ .vop_lookup = fs_eio },
	VOPNAME_ACCESS,		{ .vop_access = fs_eio },
	VOPNAME_PATHCONF,	{ .vop_pathconf = fs_eio },
	VOPNAME_READDIR,	{ .vop_readdir = fs_eio },
	VOPNAME_MKDIR,		{ .vop_mkdir = fs_eio },
	VOPNAME_CREATE,		{ .vop_create = fs_eio },
	VOPNAME_RMDIR,		{ .vop_rmdir = fs_eio },
	VOPNAME_RENAME,		{ .vop_rename = fs_eio },
	VOPNAME_SYMLINK,	{ .vop_symlink = fs_eio },
	VOPNAME_REMOVE,		{ .vop_remove = fs_eio },
	VOPNAME_LINK,		{ .vop_link = fs_eio },
	VOPNAME_FID,		{ .vop_fid = fs_eio },
	VOPNAME_READLINK,	{ .vop_readlink = fs_eio },
	VOPNAME_GETPAGE,	{ .vop_getpage = fs_eio },
	VOPNAME_PUTPAGE,	{ .vop_putpage = fs_eio },
	VOPNAME_ADDMAP,		{ .vop_addmap = fs_eio_addmap },
	VOPNAME_DELMAP,		{ .vop_delmap = uvfs_delmap },
	VOPNAME_MAP,		{ .vop_map = fs_eio_map },
	VOPNAME_SEEK,		{ .vop_seek = fs_eio },
	VOPNAME_FSYNC,		{ .vop_fsync = fs_eio },
	VOPNAME_RWLOCK,		{ .vop_rwlock = uvfs_rwlock },
	VOPNAME_RWUNLOCK,	{ .vop_rwunlock = uvfs_rwunlock },
	VOPNAME_SPACE,		{ .vop_space = fs_eio },
	VOPNAME_IOCTL,		{ .vop_ioctl = fs_eio },
	VOPNAME_FRLOCK,		{ .vop_frlock = uvfs_frlock },
	NULL,	NULL
};
