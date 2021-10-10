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

#include <sys/types.h>
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/mutex.h>
#include <sys/conf.h>
#include <sys/file.h>
#include <sys/kmem.h>
#include <sys/mode.h>
#include <sys/fcntl.h>
#include <sys/cmn_err.h>
#include <sys/sysmacros.h>
#include <sys/sunddi.h>
#include <sys/stat.h>
#include <sys/libuvfs_ki.h>
#include <sys/uvfs.h>
#include <sys/uvfs_upcall.h>
#include <sys/uvfs_vfsops.h>
#include <sys/uvfs_uvnode.h>
#include <sys/dirent.h>

uint32_t uvfs_max_write_size = UVFS_MAX_WRITE_SIZE;
uint32_t uvfs_max_read_size = UVFS_MAX_READ_SIZE;
uint32_t uvfs_max_readdir_size = UVFS_MAX_READDIR_SIZE;
uint32_t uvfs_max_dthreads = UVFS_MAX_DTHREADS;

/*
 * Sanity-check the returned results, and retrieve the error code if sane.
 * If not sane, return EIO.
 */
static int
uvfs_darg_error(door_arg_t *dargp, uint32_t minsize)
{
	libuvfs_common_res_t *resp;
	int error;

	/* No res can be smaller than common_res_t */
	ASSERT3U(minsize, >=, sizeof (*resp));

	if (dargp->rsize < minsize)
		return (EIO);

	resp = (libuvfs_common_res_t *)(uintptr_t)dargp->rbuf;
	error = (int)resp->lcr_error;

	/*
	 * Assigning 64-bit to 32 truncates.  If anything was left, something
	 * is wrong.
	 */
	if ((error == 0) && (resp->lcr_error != 0))
		return (EIO);

	return (error);
}

static int
uvfs_get_vfs_door(uvfsvfs_t *uvfsvfs, door_handle_t *dhp)
{
	int error;

	mutex_enter(&uvfsvfs->uvfs_lock);
	error = uvfs_door_wait(uvfsvfs, 0);
	ASSERT(uvfsvfs->uvfs_door || error);
	if (uvfsvfs->uvfs_door != NULL) {
		*dhp = uvfsvfs->uvfs_door;
		door_ki_hold(*dhp);
	} else if (error == 0) {
		error = EIO;
	}
	mutex_exit(&uvfsvfs->uvfs_lock);

	return (error);
}

static void
uvfs_vfs_invalidate_door(uvfsvfs_t *uvfsvfs, door_handle_t dh)
{
	mutex_enter(&uvfsvfs->uvfs_lock);
	if (uvfsvfs->uvfs_door == dh) {
		door_ki_rele(uvfsvfs->uvfs_door);
		uvfsvfs->uvfs_door = NULL;
	}
	mutex_exit(&uvfsvfs->uvfs_lock);
}

static int
uvfs_get_vn_door(uvnode_t *uvp, door_handle_t *dhp)
{
	uvfsvfs_t *uvfsvfs;
	int error;

	mutex_enter(&uvp->uv_lock);
	if (uvp->uv_door != NULL) {
		*dhp = uvp->uv_door;
		door_ki_hold(*dhp);
		mutex_exit(&uvp->uv_lock);
		return (0);
	}
	mutex_exit(&uvp->uv_lock);

	uvfsvfs = uvp->uv_uvfsvfs;
	error = uvfs_get_vfs_door(uvfsvfs, dhp);
	if (error != 0)
		return (error);

	ASSERT3P(*dhp, !=, NULL);

	mutex_enter(&uvp->uv_lock);
	if (uvp->uv_door == NULL) {
		uvp->uv_door = *dhp;
		/* Hold for the per-vnode cache */
		door_ki_hold(uvp->uv_door);
	}
	mutex_exit(&uvp->uv_lock);

	return (error);
}

static void
uvfs_vn_invalidate_door(uvnode_t *uvp, door_handle_t dh)
{
	int found = B_FALSE;

	mutex_enter(&uvp->uv_lock);
	if (uvp->uv_door == dh) {
		door_ki_rele(uvp->uv_door);
		uvp->uv_door = NULL;
		found = B_TRUE;
	}
	mutex_exit(&uvp->uv_lock);

	if (found)
		uvfs_vfs_invalidate_door(uvp->uv_uvfsvfs, dh);
}

static int
uvfs_upcall(uvfsvfs_t *uvfsvfs,
    door_handle_t dh, door_arg_t *door_args, cred_t *cr, size_t maxsize)
{
	int error;

	error = door_ki_upcall_limited(dh, door_args, cr, maxsize, 0);
	if (error == EINTR) {
		klwp_t *lwp = ttolwp(curthread);
		proc_t *procp = ttoproc(curthread);

		/*
		 * There are at least two reasons why error might be EINTR:
		 * we may have received a signal, or the daemon that we were
		 * up-calling may have died.  If it's the latter, we want
		 * to change the error to EAGAIN, so that we will recover
		 * and retry the upcall if/when the new daemon starts.
		 *
		 * The user is always free to interrupt this process, or
		 * force-unmount, or reboot -- none of these should be
		 * prevented by this functionality.
		 */

		if ((lwp != NULL) && (procp != NULL) &&
		    (! ISSIG_FAST(curthread, lwp, procp, JUSTLOOKING)))
			error = EAGAIN;
		else {
			error = uvfs_task_upcall(uvfsvfs, dh, door_args,
			    cr, maxsize);
		}
	}

	return (error);
}

/*
 * Make an upcall against a vnode.
 */
static int
uvfs_up_vop(uvnode_t *uvp, door_arg_t *door_args, size_t maxsize, cred_t *cr)
{
	door_handle_t dh = NULL;
	int error;

	do {
		if (dh == NULL) {
			error = uvfs_get_vn_door(uvp, &dh);
			if (error != 0)
				break;
		}
		ASSERT3P(dh, !=, NULL);

		error = uvfs_upcall(uvp->uv_uvfsvfs, dh, door_args,
		    cr, maxsize);

		if (error == EAGAIN) {
			delay(hz);
		} else if (error == EBADF) {
			uvfs_vn_invalidate_door(uvp, dh);
			door_ki_rele(dh);
			dh = NULL;
			error = EAGAIN;
		}
	} while (error == EAGAIN);

	if (dh != NULL)
		door_ki_rele(dh);
	return (error);
}

/*
 * Make an upcall against the VFS.
 */
static int
uvfs_up_vfs(uvfsvfs_t *uvfsvfs, door_arg_t *door_args,
    size_t maxsize, cred_t *cr)
{
	door_handle_t dh = NULL;
	int error;

	do {
		if (dh == NULL) {
			error = uvfs_get_vfs_door(uvfsvfs, &dh);
			if (error != 0)
				break;
		}
		ASSERT3P(dh, !=, NULL);

		error = uvfs_upcall(uvfsvfs, dh, door_args, cr, maxsize);

		if (error == EAGAIN) {
			delay(hz);
		} else if (error == EBADF) {
			uvfs_vfs_invalidate_door(uvfsvfs, dh);
			door_ki_rele(dh);
			dh = NULL;
			error = EAGAIN;
		}
	} while (error == EAGAIN);

	if (dh != NULL)
		door_ki_rele(dh);
	return (error);
}

static void
uvfs_door_arg_set(door_arg_t *darg, void *arg, size_t arg_size, void *res,
    size_t res_size)
{
	darg->data_ptr = (char *)arg;
	darg->data_size = arg_size;
	darg->desc_ptr = NULL;
	darg->desc_num = 0;
	darg->rbuf = (char *)res;
	darg->rsize = MAX(res_size, sizeof (libuvfs_common_res_t));
}

/*
 * Functions for specific operations, to be called by vops and vfs-ops.
 */

int
uvfs_up_vfsroot(uvfsvfs_t *uvfsvfs, libuvfs_stat_t *stat, cred_t *cr)
{
	libuvfs_common_arg_t arg;
	libuvfs_cb_vfsroot_res_t res;
	door_arg_t darg;
	int error;

	arg.lca_optag = UVFS_CB_VFS_ROOT;
	uvfs_door_arg_set(&darg, &arg, sizeof (arg), &res, sizeof (res));

	error = uvfs_up_vfs(uvfsvfs, &darg, sizeof (res), cr);
	if (error != 0)
		return (error);
	ASSERT(darg.rbuf == (char *)&res);

	mutex_enter(&uvfsvfs->uvfs_lock);
	uvfsvfs->uvfs_root_fid = res.root_fid; /* struct assign */
	uvfsvfs->uvfs_flags |= UVFS_FLAG_ROOTFID;
	mutex_exit(&uvfsvfs->uvfs_lock);
	*stat = res.root_stat;

	return (0);
}

int
uvfs_up_statvfs(uvfsvfs_t *uvfsvfs, statvfs64_t *statp, cred_t *cr)
{
	libuvfs_cb_statvfs_res_t res;
	uint32_t flags;
	int error;

	/*
	 * Kludge: the mount() system call calls VFS_STATVFS(), in order
	 * to get the fsid.  For uvfs, the daemon does not start until
	 * after the mount() has finished.  If we made an upcall before
	 * the daemon was ever started, we would hang.  To remedy, we
	 * skip the upcall if there has never been a daemon present,
	 * and just substitute zeros for all of the upcall-derived
	 * values (which won't be used anyway).
	 */
	mutex_enter(&uvfsvfs->uvfs_lock);
	flags = uvfsvfs->uvfs_flags;
	mutex_exit(&uvfsvfs->uvfs_lock);

	if (flags & UVFS_FLAG_MOUNT_COMPLETE) {
		libuvfs_common_arg_t arg;
		door_arg_t darg;

		arg.lca_optag = UVFS_CB_VFS_STATVFS;
		uvfs_door_arg_set(&darg, &arg, sizeof (arg), &res,
		    sizeof (res));

		error = uvfs_up_vfs(uvfsvfs, &darg, sizeof (res), cr);
		ASSERT(error || darg.rbuf == (char *)&res);
		if (error == 0)
			error = uvfs_darg_error(&darg, sizeof (res));
		if (error != 0)
			return (error);
	} else {
		error = 0;
		bzero(&res, sizeof (res));
	}

	statp->f_bsize = (ulong_t)res.lcsa_bsize;
	statp->f_frsize = (ulong_t)res.lcsa_frsize;

	statp->f_blocks = res.lcsa_blocks;
	statp->f_bfree = res.lcsa_bfree;
	statp->f_bavail = res.lcsa_bavail;

	statp->f_files = res.lcsa_files;
	statp->f_ffree = res.lcsa_ffree;
	statp->f_favail = res.lcsa_favail;
	statp->f_namemax = (unsigned long)res.lcsa_namemax;

	return (0);
}

int
uvfs_up_getattr(uvnode_t *uvp, vattr_t *vap, cred_t *cr)
{
	uvfsvfs_t *uvfsvfs = uvp->uv_uvfsvfs;
	libuvfs_common_vop_arg_t arg;
	libuvfs_cb_getattr_res_t res;
	uint32_t flags;
	int error;

	mutex_enter(&uvfsvfs->uvfs_lock);
	flags = uvfsvfs->uvfs_flags;
	mutex_exit(&uvfsvfs->uvfs_lock);

	/*
	 * If we don't have a daemon then fake up success
	 * otherwise we could get hung early on in mount process
	 */
	if (flags & UVFS_FLAG_MOUNT_COMPLETE) {
		door_arg_t darg;

		uvfs_door_arg_set(&darg, &arg, sizeof (arg), &res,
		    sizeof (res));

		arg.lca_optag = UVFS_CB_VOP_GETATTR;
		arg.lca_fid = uvp->uv_fid; /* struct assign */

		error = uvfs_up_vop(uvp, &darg, sizeof (res), cr);
		ASSERT(error || darg.rbuf == (char *)&res);
		if (error == 0)
			error = uvfs_darg_error(&darg, sizeof (res));
		if (error != 0)
			return (error);
	} else {
		error = 0;
		bzero(&res, sizeof (res));
	}

	vap->va_mode = (mode_t)(res.lcgr_stat.l_mode & MODEMASK);
	vap->va_uid = (uid_t)res.lcgr_stat.l_uid;
	vap->va_gid = (gid_t)res.lcgr_stat.l_gid;
	vap->va_nodeid = res.lcgr_stat.l_id;
	vap->va_nlink = (int)res.lcgr_stat.l_links;
	vap->va_size = uvp->uv_size_known ? uvp->uv_size : res.lcgr_stat.l_size;
	vap->va_blksize = (uint_t)res.lcgr_stat.l_blksize;
	vap->va_nblocks = res.lcgr_stat.l_blocks;
	vap->va_rdev = (dev_t)res.lcgr_stat.l_rdev;

	UVFS_TIME_DECODE(&vap->va_atime, res.lcgr_stat.l_atime);
	UVFS_TIME_DECODE(&vap->va_mtime, res.lcgr_stat.l_mtime);
	UVFS_TIME_DECODE(&vap->va_ctime, res.lcgr_stat.l_ctime);

	return (0);
}

int
uvfs_up_setattr(uvnode_t *uvp, vattr_t *vap, libuvfs_stat_t *stat, cred_t *cr)
{
	uvfsvfs_t *uvfsvfs = uvp->uv_uvfsvfs;
	libuvfs_cb_setattr_arg_t arg;
	libuvfs_cb_setattr_res_t res;
	uint32_t flags;
	int error;

	mutex_enter(&uvfsvfs->uvfs_lock);
	flags = uvfsvfs->uvfs_flags;
	mutex_exit(&uvfsvfs->uvfs_lock);

	if (flags & UVFS_FLAG_MOUNT_COMPLETE) {
		door_arg_t darg;

		uvfs_door_arg_set(&darg, &arg, sizeof (arg), &res,
		    sizeof (res));

		arg.lcsa_optag = UVFS_CB_VOP_SETATTR;
		arg.lcsa_fid = uvp->uv_fid; /* struct assign */
		arg.lcsa_mask = vap->va_mask;
		arg.lcsa_attributes.l_uid = vap->va_uid;
		arg.lcsa_attributes.l_gid = vap->va_gid;
		arg.lcsa_attributes.l_mode = vap->va_mode;
		arg.lcsa_attributes.l_size = vap->va_size;
		UVFS_TIME_ENCODE(&vap->va_atime, arg.lcsa_attributes.l_atime);
		UVFS_TIME_ENCODE(&vap->va_mtime, arg.lcsa_attributes.l_mtime);
		UVFS_TIME_ENCODE(&vap->va_ctime, arg.lcsa_attributes.l_ctime);

		error = uvfs_up_vop(uvp, &darg, sizeof (res), cr);
		ASSERT(error || darg.rbuf == (char *)&res);
		if (error == 0)
			error = uvfs_darg_error(&darg, sizeof (res));
		if (error != 0)
			return (error);
	} else {
		error = 0;
		bzero(&res, sizeof (res));
	}

	*stat = res.set_attributes; /* structure assignment */

	return (0);
}

int
uvfs_up_lookup(uvnode_t *uvp, char *nm, libuvfs_fid_t *fidp,
    libuvfs_stat_t *stat, cred_t *cr)
{
	uvfsvfs_t *uvfsvfs = uvp->uv_uvfsvfs;
	libuvfs_cb_lookup_arg_t arg;
	libuvfs_cb_lookup_res_t res;
	door_arg_t darg;
	uint32_t flags;
	int error;

	mutex_enter(&uvfsvfs->uvfs_lock);
	flags = uvfsvfs->uvfs_flags;
	mutex_exit(&uvfsvfs->uvfs_lock);

	/*
	 * return dummy stats when the daemon isn't yet running.
	 * Otherwise we could get hung waiting for daemon.
	 */
	if (flags & UVFS_FLAG_MOUNT_COMPLETE) {
		uvfs_door_arg_set(&darg, &arg, sizeof (arg), &res,
		    sizeof (res));

		arg.lcla_optag = UVFS_CB_VOP_LOOKUP;
		arg.lcla_dirfid = uvp->uv_fid; /* struct assign */
		(void) strlcpy(arg.lcla_nm, nm, sizeof (arg.lcla_nm));

		error = uvfs_up_vop(uvp, &darg, sizeof (res), cr);
		ASSERT(error || darg.rbuf == (char *)&res);
		if (error == 0)
			error = uvfs_darg_error(&darg, sizeof (res));
		if (error != 0)
			return (error);
	} else {
		error = 0;
		bzero(&res, sizeof (res));
	}

	*fidp = res.lclr_fid; /* structure assignment */
	*stat = res.lclr_stat; /* structure assignment */

	return (0);
}

int
uvfs_up_vget(uvfsvfs_t *uvfsvfs, libuvfs_fid_t *fidp,
    libuvfs_stat_t *stat, cred_t *cr)
{
	libuvfs_cb_vget_arg_t arg;
	libuvfs_cb_vget_res_t res;
	door_arg_t darg;
	int error;

	arg.lcvg_optag = UVFS_CB_VFS_VGET;
	arg.lcvg_fid = *fidp; /* struct assign */
	uvfs_door_arg_set(&darg, &arg, sizeof (arg), &res, sizeof (res));

	error = uvfs_up_vfs(uvfsvfs, &darg, sizeof (res), cr);
	ASSERT(error || darg.rbuf == (char *)&res);

	if (error == 0)
		error = uvfs_darg_error(&darg, sizeof (res));
	if (error != 0)
		return (error);

	*stat = res.lcvg_stat; /* structure assignment */

	return (0);
}

int
uvfs_up_mkdir(uvnode_t *dvp, char *nm, libuvfs_fid_t *fidp, vattr_t *vap,
    libuvfs_stat_t *stat, cred_t *cr)
{
	libuvfs_cb_mkdir_arg_t arg;
	libuvfs_cb_mkdir_res_t res;
	door_arg_t darg;
	int error;

	uvfs_door_arg_set(&darg, &arg, sizeof (arg), &res, sizeof (res));

	arg.lcmd_optag = UVFS_CB_VOP_MKDIR;
	arg.lcmd_dirfid = dvp->uv_fid; /* struct assign */
	arg.lcmd_creation_attrs.l_mode = MAKEIMODE(vap->va_type, vap->va_mode);
	(void) strlcpy(arg.lcmd_name, nm, sizeof (arg.lcmd_name));

	error = uvfs_up_vop(dvp, &darg, sizeof (res), cr);
	ASSERT(error || darg.rbuf == (char *)&res);

	if (error == 0)
		error = uvfs_darg_error(&darg, sizeof (res));
	if (error != 0)
		return (error);

	*fidp = res.lcmd_fid; /* structure assignment */
	*stat = res.lcmd_stat; /* structure assignment */

	return (0);
}

int
uvfs_up_create(uvnode_t *dvp, char *nm, vattr_t *vap, int mode,
    libuvfs_fid_t *fidp, libuvfs_stat_t *stat, cred_t *cr)
{
	libuvfs_cb_create_arg_t arg;
	libuvfs_cb_create_res_t res;
	door_arg_t darg;
	int error;

	uvfs_door_arg_set(&darg, &arg, sizeof (arg), &res, sizeof (res));

	arg.lccf_optag = UVFS_CB_VOP_CREATE;
	arg.lccf_dirfid = dvp->uv_fid; /* struct assign */
	(void) strlcpy(arg.lccf_name, nm, sizeof (arg.lccf_name));
	arg.lccf_creation_attrs.l_size = 0;
	arg.lccf_creation_attrs.l_mode = MAKEIMODE(vap->va_type, vap->va_mode);
	arg.lccf_creation_attrs.l_rdev = uvfs_expldev(vap->va_rdev);
	arg.lccf_mode = mode;

	error = uvfs_up_vop(dvp, &darg, sizeof (res), cr);
	ASSERT(error || darg.rbuf == (char *)&res);
	if (error == 0)
		error = uvfs_darg_error(&darg, sizeof (res));
	if (error != 0)
		return (error);

	*fidp = res.lccf_fid; /* structure assignment */
	*stat = res.lccf_stat; /* structure assignment */

	return (0);
}

int
uvfs_up_open(uvnode_t *vp, int mode, uint64_t count, cred_t *cr)
{
	libuvfs_cb_open_arg_t arg;
	libuvfs_common_res_t res;
	door_arg_t darg;
	int error;

	uvfs_door_arg_set(&darg, &arg, sizeof (arg), &res, sizeof (res));

	arg.lcof_optag = UVFS_CB_VOP_OPEN;
	arg.lcof_fid = vp->uv_fid; /* struct assign */
	arg.lcof_mode = mode;
	arg.lcof_open_count = count;

	error = uvfs_up_vop(vp, &darg, sizeof (res), cr);
	ASSERT(error || darg.rbuf == (char *)&res);
	if (error == 0)
		error = uvfs_darg_error(&darg, sizeof (res));

	return (error);
}

int
uvfs_up_close(uvnode_t *vp, int mode, int count, offset_t offset, cred_t *cr)
{
	libuvfs_cb_close_arg_t arg;
	libuvfs_common_res_t res;
	door_arg_t darg;
	int error;

	uvfs_door_arg_set(&darg, &arg, sizeof (arg), &res, sizeof (res));

	arg.lccf_optag = UVFS_CB_VOP_CLOSE;
	arg.lccf_mode = mode;
	arg.lccf_count = count;
	arg.lccf_offset = offset;
	arg.lccf_fid = vp->uv_fid; /* struct assign */

	error = uvfs_up_vop(vp, &darg, sizeof (res), cr);
	ASSERT(error || darg.rbuf == (char *)&res);
	if (error == 0)
		error = uvfs_darg_error(&darg, sizeof (res));

	return (error);
}

int
uvfs_up_read(uvnode_t *fvp, caddr_t addr, int len, u_offset_t off,
    cred_t *cr, size_t *io_len)
{
	uvfsvfs_t *uvfsvfs = fvp->uv_uvfsvfs;
	libuvfs_cb_read_arg_t arg;
	libuvfs_cb_read_res_t *resp;
	uintptr_t in_data;
	door_arg_t darg;
	size_t maxres;
	int error;

	resp = kmem_cache_alloc(uvfsvfs->uvfs_read_res_cache, KM_SLEEP);
	maxres = sizeof (*resp) + uvfsvfs->uvfs_read_size;
	uvfs_door_arg_set(&darg, &arg, sizeof (arg), resp, maxres);

	arg.lcra_optag = UVFS_CB_VOP_READ;
	arg.lcra_offset = off;
	arg.lcra_len = len;
	arg.lcra_fid = fvp->uv_fid; /* struct assign */

	error = uvfs_up_vop(fvp, &darg, maxres, cr);
	ASSERT3P(darg.rbuf, ==, resp);
	if (error == 0) {
		error = uvfs_darg_error(&darg, sizeof (*resp));

		if (error == 0) {
			in_data = (uintptr_t)resp + sizeof (*resp);
			ASSERT3U(resp->lcrr_length, <=,
			    uvfsvfs->uvfs_read_size);
			bcopy((void *)in_data, (void *)addr, resp->lcrr_length);
			*io_len = resp->lcrr_length;
		}
	}

	kmem_cache_free(uvfsvfs->uvfs_read_res_cache, resp);

	return (error);
}

int
uvfs_up_readdir(uvnode_t *fvp, uio_t *uiop, int *eofp, cred_t *cr)
{
	uvfsvfs_t *uvfsvfs = fvp->uv_uvfsvfs;
	libuvfs_cb_readdir_arg_t arg;
	libuvfs_cb_readdir_res_t *resp;
	size_t maxres;
	uintptr_t in_data;
	door_arg_t darg;
	int error;

	resp = kmem_cache_alloc(uvfsvfs->uvfs_readdir_res_cache, KM_SLEEP);
	maxres = sizeof (*resp) + uvfsvfs->uvfs_readdir_size;
	uvfs_door_arg_set(&darg, &arg, sizeof (arg), resp, maxres);

	arg.lcrda_optag = UVFS_CB_VOP_READDIR;
	arg.lcrda_offset = uiop->uio_loffset;
	arg.lcrda_length = uiop->uio_iov->iov_len;
	arg.lcrda_fid = fvp->uv_fid; /* struct assign */

	error = uvfs_up_vop(fvp, &darg, maxres, cr);
	ASSERT(error || darg.rbuf == (char *)resp);
	if (error == 0) {
		error = uvfs_darg_error(&darg, sizeof (*resp));

		if (error == 0) {
			*eofp = (int)resp->lcrdr_eof;
			in_data = (uintptr_t)resp + sizeof (*resp);

			ASSERT3U(resp->lcrdr_length, <=,
			    uvfsvfs->uvfs_readdir_size);
			error = uiomove((void *)in_data, resp->lcrdr_length,
			    UIO_READ, uiop);
		}

		uiop->uio_loffset = resp->lcrdr_offset;
	}

	kmem_cache_free(uvfsvfs->uvfs_readdir_res_cache, resp);
	return (error);
}

int
uvfs_up_write(uvnode_t *fvp, caddr_t addr, int length,
    u_offset_t off, cred_t *cr)
{
	uvfsvfs_t *uvfsvfs = fvp->uv_uvfsvfs;
	libuvfs_cb_write_arg_t *argp;
	libuvfs_cb_write_res_t res;
	uint32_t argsize;
	door_arg_t darg;
	uintptr_t data;
	int error;

	VERIFY3U(length, <=, uvfsvfs->uvfs_write_size);
	argsize = sizeof (*argp) + length;
	argp = kmem_cache_alloc(uvfsvfs->uvfs_write_args_cache, KM_SLEEP);
	data = (uintptr_t)argp + sizeof (*argp);

	uvfs_door_arg_set(&darg, argp, argsize, &res, sizeof (res));

	argp->lcwa_optag = UVFS_CB_VOP_WRITE;
	argp->lcwa_fid = fvp->uv_fid; /* struct assign */
	argp->lcwa_length = length;
	argp->lcwa_offset = off;
	bcopy(addr, (void *)data, length);

	error = uvfs_up_vop(fvp, &darg, sizeof (res), cr);
	ASSERT(error || darg.rbuf == (char *)&res);
	if (error == 0) {
		error = uvfs_darg_error(&darg, sizeof (res));

		if (error == 0) {
			uvfs_update_attrs(fvp, &res.lcwr_stat);
		}
	}

	kmem_cache_free(uvfsvfs->uvfs_write_args_cache, argp);

	return (error);
}

int
uvfs_up_remove(uvnode_t *fvp, char *name, cred_t *cr)
{
	libuvfs_cb_remove_arg_t arg;
	libuvfs_common_res_t res;
	door_arg_t darg;
	int error;

	uvfs_door_arg_set(&darg, &arg, sizeof (arg), &res, sizeof (res));

	arg.lcrf_optag = UVFS_CB_VOP_REMOVE;
	arg.lcrf_fid = fvp->uv_fid; /* struct assign */
	(void) strlcpy(arg.lcrf_name, name, sizeof (arg.lcrf_name));

	error = uvfs_up_vop(fvp, &darg, sizeof (res), cr);
	ASSERT(error || darg.rbuf == (char *)&res);
	if (error == 0)
		error = uvfs_darg_error(&darg, sizeof (res));

	return (error);
}

int
uvfs_up_rmdir(uvnode_t *fvp, char *name, cred_t *cr)
{
	libuvfs_cb_rmdir_arg_t arg;
	libuvfs_common_res_t res;
	door_arg_t darg;
	int error;

	uvfs_door_arg_set(&darg, &arg, sizeof (arg), &res, sizeof (res));

	arg.lcrd_optag = UVFS_CB_VOP_RMDIR;
	arg.lcrd_fid = fvp->uv_fid; /* struct assign */
	(void) strlcpy(arg.lcrd_name, name, sizeof (arg.lcrd_name));

	error = uvfs_up_vop(fvp, &darg, sizeof (res), cr);
	ASSERT(error || darg.rbuf == (char *)&res);
	if (error == 0)
		error = uvfs_darg_error(&darg, sizeof (res));

	return (error);
}

int
uvfs_up_link(uvnode_t *dvp, uvnode_t *svp, char *name, int flags, cred_t *cr)
{
	libuvfs_cb_link_arg_t arg;
	libuvfs_common_res_t res;
	door_arg_t darg;
	int error;

	uvfs_door_arg_set(&darg, &arg, sizeof (arg), &res, sizeof (res));

	arg.lclf_optag = UVFS_CB_VOP_LINK;
	arg.lclf_dirfid = dvp->uv_fid; /* struct assign */
	arg.lclf_sfid = svp->uv_fid; /* struct assign */
	(void) strlcpy(arg.lclf_name, name, sizeof (arg.lclf_name));
	arg.lclf_flags = flags;

	error = uvfs_up_vop(dvp, &darg, sizeof (res), cr);
	ASSERT(error || darg.rbuf == (char *)&res);

	if (error == 0)
		error = uvfs_darg_error(&darg, sizeof (res));

	return (error);
}

int
uvfs_up_symlink(uvnode_t *dvp, char *name, char *link, cred_t *cr)
{
	libuvfs_cb_symlink_arg_t arg;
	libuvfs_common_res_t res;
	door_arg_t darg;
	int error;

	uvfs_door_arg_set(&darg, &arg, sizeof (arg), &res, sizeof (res));

	arg.lcsl_optag = UVFS_CB_VOP_SYMLINK;
	arg.lcsl_dirfid = dvp->uv_fid; /* struct assign */
	(void) strlcpy(arg.lcsl_name, name, sizeof (arg.lcsl_name));
	(void) strlcpy(arg.lcsl_link, link, sizeof (arg.lcsl_link));

	error = uvfs_up_vop(dvp, &darg, sizeof (res), cr);
	ASSERT(error || darg.rbuf == (char *)&res);

	if (error == 0)
		error = uvfs_darg_error(&darg, sizeof (res));

	return (error);
}

int
uvfs_up_readlink(uvnode_t *dvp, uio_t *uiop, cred_t *cr)
{
	libuvfs_cb_readlink_arg_t arg;
	libuvfs_cb_readlink_res_t *resp = NULL;
	uintptr_t in_data;
	size_t maxres;
	door_arg_t darg;
	int error;

	maxres = sizeof (*resp) + 8 * 1024;
	uvfs_door_arg_set(&darg, &arg, sizeof (arg), NULL, maxres);

	arg.lcrl_optag = UVFS_CB_VOP_READLINK;
	arg.lcrl_dirfid = dvp->uv_fid; /* struct assign */

	error = uvfs_up_vop(dvp, &darg, maxres, cr);
	if (error == 0)
		error = uvfs_darg_error(&darg, sizeof (*resp));

	if (error == 0) {
		resp = (libuvfs_cb_readlink_res_t *)(uintptr_t)darg.rbuf;
		in_data = (uintptr_t)resp + sizeof (*resp);
		error = uiomove((void *)in_data, MIN(resp->lcrl_length,
		    uiop->uio_resid), UIO_READ, uiop);
	}

	if (darg.rbuf != NULL)
		kmem_free(darg.rbuf, darg.rsize);

	return (error);
}

int
uvfs_up_rename(uvnode_t *sdvp, char *snm, uvnode_t *tdvp, char *tnm, cred_t *cr)
{
	libuvfs_cb_rename_arg_t arg;
	libuvfs_common_res_t res;
	door_arg_t darg;
	int error;

	uvfs_door_arg_set(&darg, &arg, sizeof (arg), &res, sizeof (res));

	arg.lcrn_optag = UVFS_CB_VOP_RENAME;
	arg.lcrn_sdfid = sdvp->uv_fid; /* struct assign */
	arg.lcrn_tdfid = tdvp->uv_fid; /* struct assign */
	(void) strlcpy(arg.lcrn_sname, snm, sizeof (arg.lcrn_sname));
	(void) strlcpy(arg.lcrn_tname, tnm, sizeof (arg.lcrn_tname));

	error = uvfs_up_vop(sdvp, &darg, sizeof (res), cr);
	ASSERT(error || darg.rbuf == (char *)&res);

	if (error == 0)
		error = uvfs_darg_error(&darg, sizeof (res));

	return (error);
}

int
uvfs_up_space(uvnode_t *uvp, uint64_t offset, uint64_t len, int flag,
    cred_t *cr)
{
	libuvfs_cb_space_arg_t arg;
	libuvfs_common_res_t res;
	door_arg_t darg;
	int error;

	uvfs_door_arg_set(&darg, &arg, sizeof (arg), &res, sizeof (res));

	arg.lcfs_optag = UVFS_CB_VOP_SPACE;
	arg.lcfs_fid = uvp->uv_fid; /* struct assign */
	arg.lcfs_offset = offset;
	arg.lcfs_len = len;
	arg.lcfs_flag = flag;

	error = uvfs_up_vop(uvp, &darg, sizeof (res), cr);
	ASSERT(error || darg.rbuf == (char *)&res);

	if (error == 0)
		error = uvfs_darg_error(&darg, sizeof (res));

	return (error);
}

int
uvfs_up_fsync(uvnode_t *uvp, int flag, cred_t *cr)
{
	libuvfs_cb_fsync_arg_t arg;
	libuvfs_common_res_t res;
	door_arg_t darg;
	int error;

	uvfs_door_arg_set(&darg, &arg, sizeof (arg), &res, sizeof (res));

	arg.lcfs_optag = UVFS_CB_VOP_FSYNC;
	arg.lcfs_fid = uvp->uv_fid; /* struct assign */
	arg.lcfs_syncflag = flag;

	error = uvfs_up_vop(uvp, &darg, sizeof (res), cr);
	ASSERT(error || darg.rbuf == (char *)&res);

	if (error == 0)
		error = uvfs_darg_error(&darg, sizeof (res));

	return (error);
}

int
uvfs_up_direct_read(vnode_t *vp, uio_t *uiop, cred_t *cr)
{
	uvnode_t *uvp = VTOUV(vp);
	uvfsvfs_t *uvfsvfsp = VTOUV(vp)->uv_uvfsvfs;
	libuvfs_cb_read_arg_t arg;
	libuvfs_cb_read_res_t *resp;
	uintptr_t in_data;
	offset_t offset;
	door_arg_t darg;
	size_t maxres;
	int count;
	int error;

	maxres = sizeof (*resp) + uvfsvfsp->uvfs_read_size;

	resp = kmem_cache_alloc(uvfsvfsp->uvfs_read_res_cache, KM_SLEEP);

	do {
		offset = uiop->uio_loffset;
		count = MIN(uiop->uio_resid, uvfsvfsp->uvfs_read_size);

		uvfs_door_arg_set(&darg, &arg, sizeof (arg), resp, maxres);
		arg.lcra_optag = UVFS_CB_VOP_READ;
		arg.lcra_fid = uvp->uv_fid;
		arg.lcra_offset = offset;
		arg.lcra_len = count;

		error = uvfs_up_vop(uvp, &darg, maxres, cr);
		ASSERT(error || darg.rbuf == (char *)resp);
		if (error == 0)
			error = uvfs_darg_error(&darg, sizeof (*resp));

		if (error == 0) {
			resp = (libuvfs_cb_read_res_t *)(uintptr_t)darg.rbuf;
			if (resp->lcrr_length == 0)
				break;
			in_data = (uintptr_t)resp + sizeof (*resp);
			error = uiomove((void *)in_data, resp->lcrr_length,
			    UIO_READ, uiop);
		}
	} while ((error == 0) && (uiop->uio_resid > 0));

	kmem_cache_free(uvfsvfsp->uvfs_read_res_cache, resp);

	return (error);
}

int
uvfs_up_direct_write(vnode_t *vp, uio_t *uiop, cred_t *cr, int remainder)
{
	uvnode_t *uvp = VTOUV(vp);
	uvfsvfs_t *uvfsvfs = uvp->uv_uvfsvfs;
	libuvfs_cb_write_arg_t *argp;
	libuvfs_cb_write_res_t res;
	int resid, count;
	offset_t offset;
	door_arg_t darg;
	uintptr_t data;
	int error;

	argp = kmem_cache_alloc(uvfsvfs->uvfs_write_args_cache, KM_SLEEP);
	data = (uintptr_t)argp + sizeof (*argp);

	do {
		boolean_t cleared = B_FALSE;

		resid = uiop->uio_resid;
		offset = uiop->uio_loffset;
		count = MIN(uiop->uio_resid, uvfsvfs->uvfs_write_size);
		error = uiomove((void *)data, count, UIO_WRITE, uiop);
		if (error != 0)
			break;

		uvfs_door_arg_set(&darg, argp, sizeof (*argp) + count,
		    &res, sizeof (res));
		argp->lcwa_optag = UVFS_CB_VOP_WRITE;
		argp->lcwa_fid = uvp->uv_fid;
		argp->lcwa_offset = offset;
		argp->lcwa_length = count;

		error = uvfs_up_vop(uvp, &darg, sizeof (res), cr);
		ASSERT(error || darg.rbuf == (char *)&res);
		if (error == 0)
			error = uvfs_darg_error(&darg, sizeof (res));

		if (!cleared && error == 0) {
			cleared = B_TRUE;
			error = uvfs_update_setid(uvp, cr);
		}
	} while ((error == 0) && (uiop->uio_resid > 0));

	kmem_cache_free(uvfsvfs->uvfs_write_args_cache, argp);

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
	if (error == 0) {
		uvp->uv_size = res.lcwr_stat.l_size;
		uiop->uio_resid += remainder;
	} else {
		uiop->uio_resid = resid + remainder;
		uiop->uio_loffset = offset;
	}

	return (error);
}

static int
uvfs_up_map_impl(uvnode_t *uvp, boolean_t add, uint64_t count, cred_t *cr)
{
	libuvfs_cb_map_arg_t arg;
	libuvfs_common_res_t res;
	door_arg_t darg;
	int error;

	arg.lcma_optag = add ? UVFS_CB_VOP_ADDMAP : UVFS_CB_VOP_DELMAP;
	arg.lcma_fid = uvp->uv_fid;
	arg.lcma_count = count;
	uvfs_door_arg_set(&darg, &arg, sizeof (arg), &res, sizeof (res));

	error = uvfs_up_vop(uvp, &darg, sizeof (res), cr);
	ASSERT(error || darg.rbuf == (char *)&res);
	if (error == 0)
		error = uvfs_darg_error(&darg, sizeof (res));

	return (error);
}

int
uvfs_up_addmap(uvnode_t *uvp, uint64_t count, cred_t *cr)
{
	return (uvfs_up_map_impl(uvp, B_TRUE, count, cr));
}

int
uvfs_up_delmap(uvnode_t *uvp, uint64_t count, cred_t *cr)
{
	return (uvfs_up_map_impl(uvp, B_FALSE, count, cr));
}
