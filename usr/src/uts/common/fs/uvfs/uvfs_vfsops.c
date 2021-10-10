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

#include <sys/libuvfs_ki.h>
#include <sys/uvfs.h>

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysmacros.h>
#include <sys/kmem.h>
#include <sys/pathname.h>
#include <sys/vnode.h>
#include <sys/vfs.h>
#include <sys/vfs_opreg.h>
#include <sys/fcntl.h>
#include <sys/mntent.h>
#include <sys/mount.h>
#include <sys/modctl.h>
#include <sys/mkdev.h>
#include <sys/cmn_err.h>
#include <sys/sunddi.h>
#include <sys/policy.h>
#include <sys/fs/uvfs.h>
#include <sys/uvfs_vfsops.h>
#include <sys/uvfs_uvnode.h>
#include <sys/uvfs_upcall.h>
#include <sys/dnlc.h>
#include <sys/int_fmtio.h>
#include <vm/seg_map.h>

extern size_t door_max_upcall_reply;

vfsops_t *uvfs_vfsops = NULL;

/*
 * The list of uvfs file systems is used from uvfs_sync.  It allows us
 * to traverse all of the uvfs file systems on the system and sync all
 * of the dirty data.  Items are added to this list in uvfs_mount and
 * removed from the list in uvfs_unmount.  uvfs_fs_list_lock protects
 * the list.
 */
list_t uvfs_fs_list;
kmutex_t uvfs_fs_list_lock;

static major_t uvfs_major;
static minor_t uvfs_minor;
static kmutex_t	uvfs_dev_mtx;

kmem_cache_t *uvfsvfs_cache;

static uint64_t uvfs_name_unique_count = 0;
#define	UVFS_NAME_UNIQUE_SIZE	(2 * sizeof (uvfs_name_unique_count) + 1)

extern int sys_shutdown;

static int uvfs_mount(vfs_t *vfsp, vnode_t *mvp, struct mounta *uap,
    cred_t *cr);
static int uvfs_umount(vfs_t *vfsp, int fflag, cred_t *cr);
static int uvfs_statvfs(vfs_t *vfsp, struct statvfs64 *statp);
static int uvfs_vget(vfs_t *vfsp, vnode_t **vpp, fid_t *fidp);
static int uvfs_sync(vfs_t *, short, cred_t *);
static void uvfs_freevfs(vfs_t *vfsp);

static const fs_operation_def_t uvfs_vfsops_template[] = {
	VFSNAME_MOUNT,		{ .vfs_mount = uvfs_mount },
	VFSNAME_UNMOUNT,	{ .vfs_unmount = uvfs_umount },
	VFSNAME_ROOT,		{ .vfs_root = uvfs_root },
	VFSNAME_STATVFS,	{ .vfs_statvfs = uvfs_statvfs },
	VFSNAME_FREEVFS,	{ .vfs_freevfs = uvfs_freevfs },
	VFSNAME_VGET,		{ .vfs_vget = uvfs_vget },
	VFSNAME_SYNC,		{ .vfs_sync = uvfs_sync },
	NULL,			NULL
};

/*
 * We need to keep a count of active fs's.
 * This is necessary to prevent our module
 * from being unloaded after a umount -f
 */
uint32_t uvfs_active_fs_count = 0;

static char *forcedirectio_cancel[] = { MNTOPT_NOFORCEDIRECTIO, NULL};
static char *noforcedirectio_cancel[] = { MNTOPT_FORCEDIRECTIO, NULL};
static char *noallow_other_cancel[] = { MNTOPT_ALLOW_OTHER, NULL };
static char *allow_other_cancel[] = { MNTOPT_NOALLOW_OTHER, NULL };

static mntopt_t mntopts[] = {
	{ MNTOPT_NOFORCEDIRECTIO, noforcedirectio_cancel, NULL, 0, NULL },
	{ MNTOPT_FORCEDIRECTIO, forcedirectio_cancel, NULL, 0, NULL},
	{ MNTOPT_NOALLOW_OTHER, noallow_other_cancel, NULL, 0, NULL },
	{ MNTOPT_ALLOW_OTHER, allow_other_cancel, NULL, 0, NULL },
	{ MNTOPT_MAX_READ, NULL, "0", MO_HASVALUE, NULL},
	{ MNTOPT_MAX_WRITE, NULL, "0", MO_HASVALUE, NULL},
	{ MNTOPT_MAX_DTHREADS, NULL, "0", MO_HASVALUE, NULL},
};

static mntopts_t uvfs_mntopts = {
	sizeof (mntopts) / sizeof (mntopt_t),
	mntopts
};

static int uvfs_vfsinit(int, char *);

static vfsdef_t vfw = {
	VFSDEF_VERSION,
	MNTTYPE_UVFS,
	uvfs_vfsinit,
	VSW_HASPROTO|VSW_CANRWRO|VSW_CANREMOUNT|VSW_VOLATILEDEV|\
	    VSW_STATS|VSW_ZMOUNT,
	&uvfs_mntopts
};

struct modlfs uvfs_modlfs = {
	&mod_fsops,
	"User virtual filesystem",
	&vfw
};

int uvfstype;

extern const fs_operation_def_t uvfs_fvnodeops_template[];
extern const fs_operation_def_t uvfs_dvnodeops_template[];
extern const fs_operation_def_t uvfs_symvnodeops_template[];
extern const fs_operation_def_t uvfs_evnodeops_template[];
extern const fs_operation_def_t uvfs_eio_vnodeops_template[];

static void
uvfsvfs_free_vnodeops(uvfsvfs_t *uvfsvfs)
{
	vn_freevnodeops(uvfsvfs->uvfs_dvnodeops);
	vn_freevnodeops(uvfsvfs->uvfs_fvnodeops);
	vn_freevnodeops(uvfsvfs->uvfs_symvnodeops);
	vn_freevnodeops(uvfsvfs->uvfs_evnodeops);
}

static int
uvfs_name_unique(char *buffer, const int bufsize, const char *prefix)
{
	char hex[UVFS_NAME_UNIQUE_SIZE];

	(void) sprintf(hex, "%" PRIx64,
	    atomic_inc_64_nv(&uvfs_name_unique_count));
	if (strlcpy(buffer, prefix, bufsize) >= bufsize)
		return (ENOMEM);
	if (strlcat(buffer, hex, bufsize) >= bufsize)
		return (ENOMEM);

	return (0);
}

static int
uvfs_create_op_tables(uvfsvfs_t *uvfsvfsp)
{
	int error;

	error = vn_make_ops(MNTTYPE_UVFS, uvfs_dvnodeops_template,
	    &uvfsvfsp->uvfs_dvnodeops);
	if (error)
		return (error);

	error = vn_make_ops(MNTTYPE_UVFS, uvfs_fvnodeops_template,
	    &uvfsvfsp->uvfs_fvnodeops);
	if (error)
		return (error);

	error = vn_make_ops(MNTTYPE_UVFS, uvfs_symvnodeops_template,
	    &uvfsvfsp->uvfs_symvnodeops);
	if (error)
		return (error);

	error = vn_make_ops(MNTTYPE_UVFS, uvfs_evnodeops_template,
	    &uvfsvfsp->uvfs_evnodeops);

	return (error);
}

/*
 * VFS_INIT() initialization.  Note that there is no VFS_FINI(),
 * so we can't safely do any non-idempotent initialization here.
 * Leave that to uvfs_init() and uvfs_fini(), which are called
 * from the module's _init() and _fini() entry points.
 */
/*ARGSUSED*/
static int
uvfs_vfsinit(int fstype, char *name)
{
	int error;

	uvfstype = fstype;

	/*
	 * Setup vfsops table
	 */
	error = vfs_setfsops(fstype, uvfs_vfsops_template,
	    &uvfs_vfsops);
	if (error != 0) {
		cmn_err(CE_WARN, "uvfs: bad vfs ops template");
	}

	mutex_init(&uvfs_dev_mtx, NULL, MUTEX_DEFAULT, NULL);

	/*
	 * Unique major number for all uvfs mounts.
	 * If we run out of 32-bit minors, we'll getudev() another major.
	 */
	uvfs_major = ddi_name_to_major(UVFS_DRIVER);
	uvfs_minor = UVFS_MIN_MINOR;

	return (0);
}

/*
 * This is copied from zfs_vfsops.c
 *
 * This needs to be made into a common function shared
 * by any pseudo file system that needs it.
 *
 * Could also be used by nfs
 */
static int
uvfs_create_unique_device(dev_t *dev)
{
	major_t new_major;

	do {
		ASSERT3U(uvfs_minor, <=, MAXMIN32);
		minor_t start = uvfs_minor;
		do {
			mutex_enter(&uvfs_dev_mtx);
			if (uvfs_minor >= MAXMIN32) {
				/*
				 * If we're still using the real major
				 * keep out of /dev/uvfs
				 * number space.  If we're using a getudev()'ed
				 * major number, we can use all of its minors.
				 */
				if (uvfs_major ==
				    ddi_name_to_major(UVFS_DRIVER))
					uvfs_minor = UVFS_MIN_MINOR;
				else
					uvfs_minor = 0;
			} else {
				uvfs_minor++;
			}
			*dev = makedevice(uvfs_major, uvfs_minor);
			mutex_exit(&uvfs_dev_mtx);
		} while (vfs_devismounted(*dev) && uvfs_minor != start);
		if (uvfs_minor == start) {
			/*
			 * We are using all ~262,000 minor numbers for the
			 * current major number.  Create a new major number.
			 */
			if ((new_major = getudev()) == (major_t)-1) {
				cmn_err(CE_WARN,
				    "uvfs_create_unique_device: Can't get "
				    "unique major device number.");
				return (-1);
			}
			mutex_enter(&uvfs_dev_mtx);
			uvfs_major = new_major;
			uvfs_minor = 0;

			mutex_exit(&uvfs_dev_mtx);
		} else {
			break;
		}
		/* CONSTANTCONDITION */
	} while (1);

	return (0);
}

static int
uvfsvfs_alloc(vfs_t *vfsp, uvfsvfs_t **uvfs_ret, uvfs_mount_opts_t *opts)
{
	uvfsvfs_t *uvfsvfs;
	hrtime_t now = gethrtime();
	libuvfs_fid_t *fid;
	char kstat_wname[MAXNAMELEN];
	char kstat_rname[MAXNAMELEN];
	char kstat_rdirname[MAXNAMELEN];
	uint32_t read_size, write_size;
	libuvfs_stat_t stat = { 0 };
	uvnode_t *uvp;
	vnode_t *vp;
	int error;

	if ((error = uvfs_name_unique(kstat_wname, sizeof (kstat_wname),
	    "uvfs_write_args_cache_")) != 0)
		return (ENOMEM);
	if ((error = uvfs_name_unique(kstat_rname, sizeof (kstat_rname),
	    "uvfs_read_args_cache_")) != 0)
		return (ENOMEM);
	if ((error = uvfs_name_unique(kstat_rdirname, sizeof (kstat_rdirname),
	    "uvfs_readdir_res_cache_")) != 0)
		return (ENOMEM);

	*uvfs_ret = NULL;
	uvfsvfs = kmem_cache_alloc(uvfsvfs_cache, KM_SLEEP);
	uvfsvfs->uvfs_flags = 0;
	uvfsvfs->uvfs_door = NULL;
	uvfsvfs->uvfs_root_fid.uvfid_len = 0;
	uvfsvfs->uvfs_rootvp = NULL;
	uvfsvfs->uvfs_vfsp = vfsp;
	uvfs_task_uvfsvfs_alloc(uvfsvfs);

	/*
	 * uvfs_max_write_size is the amount of file data allowed
	 * in one request.
	 */
	write_size = uvfs_max_write_size;
	if ((opts->max_write) && (opts->max_write < write_size))
		write_size = opts->max_write;
	/*
	 * Round down to the nearest page size.
	 */
	uvfsvfs->uvfs_write_size = P2ALIGN(write_size, PAGESIZE);

	uvfsvfs->uvfs_write_args_cache = kmem_cache_create(kstat_wname,
	    sizeof (libuvfs_cb_write_arg_t) + uvfsvfs->uvfs_write_size, 0,
	    NULL, NULL, NULL,
	    uvfsvfs, NULL, 0);

	/*
	 * Verify that uvfs_max_read_size is not going to cause us to
	 * issue a read that results in attempting to pass too much data
	 * through the door upon return.
	 *
	 * If it is determined that uvfs_max_read_size is set too large,
	 * set uvfsvfs->uvfs_read_size to a value that we know is safe.
	 */
	read_size = uvfs_max_read_size;
	if ((opts->max_read) && (opts->max_read < read_size))
		read_size = opts->max_read;
	if (read_size >
	    (door_max_upcall_reply - sizeof (libuvfs_cb_read_res_t)))
		read_size =
		    door_max_upcall_reply - sizeof (libuvfs_cb_read_res_t);

	/*
	 * Round down to the nearest page size.
	 */
	uvfsvfs->uvfs_read_size = P2ALIGN(read_size, PAGESIZE);

	uvfsvfs->uvfs_read_res_cache = kmem_cache_create(kstat_rname,
	    sizeof (libuvfs_cb_read_res_t) + uvfsvfs->uvfs_read_size, 0,
	    NULL, NULL, NULL,
	    uvfsvfs, NULL, 0);

	/*
	 * uvfs_max_readdir_size is the amount of readdir data allowed in one
	 * response.
	 */
	uvfsvfs->uvfs_readdir_size = uvfs_max_readdir_size;

	uvfsvfs->uvfs_readdir_res_cache = kmem_cache_create(kstat_rdirname,
	    sizeof (libuvfs_cb_readdir_res_t) + uvfsvfs->uvfs_readdir_size, 0,
	    NULL, NULL, NULL,
	    uvfsvfs, NULL, 0);

	uvfsvfs->uvfs_max_dthreads = (opts->max_dthreads > 0) ?
	    opts->max_dthreads : uvfs_max_dthreads;

	/* kludge up a hideous root fid, to get by until uvfs_up_vfsroot() */
	fid = &uvfsvfs->uvfs_root_fid;
	fid->uvfid_len = sizeof (now);
	bcopy(&now, fid->uvfid_data, sizeof (now));

	/*
	 * Create per mount vop tables
	 */
	error = uvfs_create_op_tables(uvfsvfs);
	if (error) {
		cmn_err(CE_WARN, "uvfs: bad vnode ops template");
		uvfsvfs_free_vnodeops(uvfsvfs);
		kmem_cache_free(uvfsvfs_cache, uvfsvfs);
		return (error);
	}

	/*
	 * Set up the root vnode, which must always be present.
	 */
	stat.l_mode = S_IFDIR;
	vfsp->vfs_data = uvfsvfs;
	error = uvfs_uvget(vfsp, &uvp, &uvfsvfs->uvfs_root_fid, &stat);
	if (error != 0) {
		uvfsvfs_free_vnodeops(uvfsvfs);
		kmem_cache_free(uvfsvfs_cache, uvfsvfs);
		vfsp->vfs_data = NULL;
		return (error);
	}

	vp = uvp->uv_vnode;
	mutex_enter(&vp->v_lock);
	vp->v_flag |= VROOT;
	mutex_exit(&vp->v_lock);
	/* vnode already held from uvfs_uvget() */
	uvfsvfs->uvfs_rootvp = vp;

	*uvfs_ret = uvfsvfs;

	return (0);
}

/*ARGSUSED*/
static int
uvfsvfs_construct(void *vuvfsvfs, void *foo, int bar)
{
	uvfsvfs_t *uvfsvfs = vuvfsvfs;

	mutex_init(&uvfsvfs->uvfs_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&uvfsvfs->uvfs_daemon_cv, NULL, CV_DEFAULT, NULL);
	mutex_init(&uvfsvfs->uvfs_uvnodes_lock, NULL, MUTEX_DEFAULT, NULL);
	list_create(&uvfsvfs->uvfs_uvnodes, sizeof (uvnode_t),
	    offsetof(uvnode_t, uv_list_node));

	return (0);
}

/*ARGSUSED*/
static void
uvfsvfs_destroy(void *vuvfsvfs, void *foo)
{
	uvfsvfs_t *uvfsvfs = vuvfsvfs;

	mutex_destroy(&uvfsvfs->uvfs_uvnodes_lock);
	list_destroy(&uvfsvfs->uvfs_uvnodes);
	cv_destroy(&uvfsvfs->uvfs_daemon_cv);
	mutex_destroy(&uvfsvfs->uvfs_lock);
}

static void
uvfsvfs_free(uvfsvfs_t *uvfsvfs)
{
	uvfs_task_uvfsvfs_free_wait(uvfsvfs);

	crfree(uvfsvfs->uvfs_mount_cred);
	uvfs_task_uvfsvfs_free(uvfsvfs);
	kmem_cache_destroy(uvfsvfs->uvfs_write_args_cache);
	kmem_cache_destroy(uvfsvfs->uvfs_read_res_cache);
	kmem_cache_destroy(uvfsvfs->uvfs_readdir_res_cache);

	ASSERT(uvfsvfs->uvfs_door == NULL);

	uvfsvfs_free_vnodeops(uvfsvfs);

	kmem_cache_free(uvfsvfs_cache, uvfsvfs);
}

static int
uvfs_mount(vfs_t *vfsp, vnode_t *mvp, struct mounta *uap, cred_t *cr)
{
	int		error;
	uvfsvfs_t	*uvfsvfs;
	dev_t		mount_dev;
	uvfs_mount_opts_t opts = {0};
	char		*sizestr, *endptr;

	if ((error = secpolicy_fs_mount(cr, mvp, vfsp)) != 0)
		return (error);

	if (mvp->v_type != VDIR)
		return (ENOTDIR);

	mutex_enter(&mvp->v_lock);
	if ((uap->flags & MS_REMOUNT) == 0 &&
	    (uap->flags & MS_OVERLAY) == 0 &&
	    (mvp->v_count != 1 || (mvp->v_flag & VROOT))) {
		mutex_exit(&mvp->v_lock);
		return (EBUSY);
	}
	mutex_exit(&mvp->v_lock);

	if (uvfs_create_unique_device(&mount_dev) == -1)
		return (ENODEV);

	ASSERT(vfs_devismounted(mount_dev) == 0);

	if (vfs_optionisset(vfsp, MNTOPT_MAX_READ, &sizestr)) {
		if (ddi_strtoul(sizestr, &endptr, 10, &opts.max_read) ||
		    endptr != sizestr + strlen(sizestr)) {
			cmn_err(CE_WARN, "uvfs: mount option - argument %s "
			    "is not a valid number", sizestr);
			return (EINVAL);
		}
	}

	if (vfs_optionisset(vfsp, MNTOPT_MAX_WRITE, &sizestr)) {
		if (ddi_strtoul(sizestr, &endptr, 10, &opts.max_write) ||
		    endptr != sizestr + strlen(sizestr)) {
			cmn_err(CE_WARN, "uvfs: mount option - argument %s "
			    "is not a valid number", sizestr);
			return (EINVAL);
		}
	}

	if (vfs_optionisset(vfsp, MNTOPT_MAX_DTHREADS, &sizestr)) {
		if (ddi_strtoul(sizestr, &endptr, 10, &opts.max_dthreads) ||
		    endptr != sizestr + strlen(sizestr)) {
			cmn_err(CE_WARN, "uvfs: mount option - argument %s "
			    "is not a valid number", sizestr);
			return (EINVAL);
		}
	}

	if (error = uvfsvfs_alloc(vfsp, &uvfsvfs, &opts))
		return (error);

	/*
	 * The mount is going to succeed after this point.
	 */

	atomic_inc_32(&uvfs_active_fs_count);

	crhold(cr);
	uvfsvfs->uvfs_mount_cred = cr;

	uvfsvfs->uvfs_allow_other = vfs_optionisset(vfsp,
	    MNTOPT_ALLOW_OTHER, NULL);

	/*
	 * Use directio if the forcedirectio mount option was given or if
	 * vpm_enable is FALSE.
	 */
	if (vfs_optionisset(vfsp, MNTOPT_FORCEDIRECTIO, NULL)) {
		uvfsvfs->uvfs_flags |= UVFS_FORCEDIRECT;
	} else {
		if (!vpm_enable) {
			uvfsvfs->uvfs_flags |= UVFS_FORCEDIRECT;
			vfs_setmntopt(uvfsvfs->uvfs_vfsp, MNTOPT_FORCEDIRECTIO,
			    NULL, 0);
		}
	}

	vfsp->vfs_dev = mount_dev;
	vfsp->vfs_bsize = 8 * 1024;
	vfsp->vfs_flag |= VFS_NOTRUNC;
	vfsp->vfs_fstype = uvfstype;

	vfs_set_feature(vfsp, VFSFT_SYSATTR_VIEWS);

	vfs_make_fsid(&vfsp->vfs_fsid, mount_dev, uvfstype);

	/*
	 * Add file system to the uvfs file systems list.
	 */
	mutex_enter(&uvfs_fs_list_lock);
	list_insert_tail(&uvfs_fs_list, uvfsvfs);
	mutex_exit(&uvfs_fs_list_lock);

	return (0);
}

static int
uvfs_umount(vfs_t *vfsp, int fflag, cred_t *cr)
{
	uvfsvfs_t *uvfsvfsp = (uvfsvfs_t *)vfsp->vfs_data;
	int error = 0;

	if ((error = secpolicy_fs_unmount(cr, vfsp)) != 0)
		goto out;

	/*
	 * dnlc_purge_vfsp may be performed at higher levels before we
	 * get here; nevertheless, we try to minimize any dnlc holdings.
	 */

	(void) dnlc_purge_vfsp(vfsp, 0);

	if (! fflag) {
		vnode_t *rootvp;
		error = uvfs_root(vfsp, &rootvp);
		if (error)
			return (error);

		/*
		 * vfs_count indicates how many vnodes are held, plus
		 * a single hold for the vfsp.
		 */

		mutex_enter(&rootvp->v_lock);
		if (vfsp->vfs_count > 2 || rootvp->v_count > 2) {
			mutex_exit(&rootvp->v_lock);
			VN_RELE(rootvp);
			return (EBUSY);
		}
		mutex_exit(&rootvp->v_lock);
		VN_RELE(rootvp);
	}

	/*
	 * Set vnodeops to error vnode ops
	 */

	mutex_enter(&uvfsvfsp->uvfs_lock);
	(void) vn_replace_ops(MNTTYPE_UVFS, uvfsvfsp->uvfs_dvnodeops,
	    uvfs_eio_vnodeops_template);
	(void) vn_replace_ops(MNTTYPE_UVFS, uvfsvfsp->uvfs_fvnodeops,
	    uvfs_eio_vnodeops_template);
	(void) vn_replace_ops(MNTTYPE_UVFS, uvfsvfsp->uvfs_symvnodeops,
	    uvfs_eio_vnodeops_template);
	if (uvfsvfsp->uvfs_door != NULL) {
		door_ki_rele(uvfsvfsp->uvfs_door);
		uvfsvfsp->uvfs_door = NULL;
	}

	uvfsvfsp->uvfs_flags |= UVFS_UNMOUNTED;
	uvfsvfsp->uvfs_vfsp->vfs_flag |= VFS_UNMOUNTED;
	cv_broadcast(&uvfsvfsp->uvfs_daemon_cv);

	mutex_exit(&uvfsvfsp->uvfs_lock);

	/*
	 * Do this outside of block which holds the uvfs_lock
	 * so we don't deadlock with the uvfs_sync taskq.
	 */
	VN_RELE(uvfsvfsp->uvfs_rootvp);
	uvfsvfsp->uvfs_rootvp = NULL;

	/*
	 * Remove the file system from the uvfs file system list.
	 */
	mutex_enter(&uvfs_fs_list_lock);
	list_remove(&uvfs_fs_list, uvfsvfsp);
	mutex_exit(&uvfs_fs_list_lock);

out:
	return (error);
}

int
uvfs_root(vfs_t *vfsp, vnode_t **vpp)
{
	uvfsvfs_t *uvfsvfs = (uvfsvfs_t *)vfsp->vfs_data;

	if (! (uvfsvfs->uvfs_flags & UVFS_FLAG_ROOTFID)) {
		uvfs_task_rootvp(uvfsvfs, CRED());
		delay(hz);
	}

	mutex_enter(&uvfsvfs->uvfs_lock);

	VERIFY3P(uvfsvfs->uvfs_rootvp, !=, NULL);
	*vpp = uvfsvfs->uvfs_rootvp;
	VN_HOLD(*vpp);

	mutex_exit(&uvfsvfs->uvfs_lock);

	return (0);
}

static int
uvfs_statvfs(vfs_t *vfsp, struct statvfs64 *statp)
{
	uvfsvfs_t *uvfsvfs = vfsp->vfs_data;
	dev32_t d32;
	int error;

	UVFS_VFS_ENTER(uvfsvfs);

	error = uvfs_up_statvfs(uvfsvfs, statp, CRED());
	if (error != 0)
		goto out;

	(void) cmpldev(&d32, vfsp->vfs_dev);
	statp->f_fsid = d32;
	(void) strlcpy(statp->f_basetype, vfssw[vfsp->vfs_fstype].vsw_name,
	    sizeof (statp->f_basetype));
	statp->f_flag = vf_to_stf(vfsp->vfs_flag);
	bzero(statp->f_fstr, sizeof (statp->f_fstr));

out:
	UVFS_VFS_EXIT(uvfsvfs);
	return (error);
}

static int
uvfs_vget(vfs_t *vfsp, vnode_t **vpp, fid_t *fidp)
{
	uvfsvfs_t *uvfsvfsp = (uvfsvfs_t *)vfsp->vfs_data;
	uvnode_t *uvp;
	libuvfs_stat_t stat;
	libuvfs_fid_t uvfid;
	int error;

	UVFS_VFS_ENTER(uvfsvfsp);
	uvfid.uvfid_len = fidp->fid_len;
	bcopy(fidp->fid_data, &uvfid.uvfid_data, fidp->fid_len);

	error = uvfs_up_vget(vfsp->vfs_data, &uvfid, &stat, CRED());
	if (error) {
		UVFS_VFS_EXIT(uvfsvfsp);
		return (error);
	}

	/*
	 * Returns with vnode held
	 */
	*vpp = NULL;
	error = uvfs_uvget(vfsp, &uvp, &uvfid, &stat);
	if (error == 0)
		*vpp = UVTOV(uvp);

	UVFS_VFS_EXIT(uvfsvfsp);
	return (0);
}

/*ARGSUSED*/
static int
uvfs_sync(vfs_t *vfsp, short flags, cred_t *cr)
{
	int error = 0;
	uvfsvfs_t *uvfsp;

	/*
	 * We don't cache attributes so there is no need to sync them.
	 */
	if (flags & SYNC_ATTR)
		return (0);

	/*
	 * SYNC_CLOSE means we're rebooting.  If any threads are stuck in
	 * uvfs_door_wait, free them.
	 */
	if ((vfsp != NULL) && (flags & SYNC_CLOSE)) {
		uvfsvfs_t *uvfsvfs = vfsp->vfs_data;

		mutex_enter(&uvfsvfs->uvfs_lock);
		uvfsvfs->uvfs_flags |= UVFS_SHUTDOWN;
		cv_broadcast(&uvfsvfs->uvfs_daemon_cv);
		mutex_exit(&uvfsvfs->uvfs_lock);
		return (0);
	}

	/*
	 * Kick off the task(s) to sync out the data.
	 */
	if (vfsp != NULL) {
		uvfs_task_sync((uvfsvfs_t *)vfsp->vfs_data, cr);
	} else {
		mutex_enter(&uvfs_fs_list_lock);
		/*
		 * Traverse the uvfs file system list
		 */
		for (uvfsp = list_head(&uvfs_fs_list);
		    uvfsp != NULL;
		    uvfsp = list_next(&uvfs_fs_list, uvfsp)) {
			/*
			 * If we are shutting down, don't do the sync,
			 * just free threads stuck in uvfs_door_wait.
			 */
			if (sys_shutdown) {
				mutex_enter(&uvfsp->uvfs_lock);
				uvfsp->uvfs_flags |= UVFS_SHUTDOWN;
				cv_broadcast(&uvfsp->uvfs_daemon_cv);
				mutex_exit(&uvfsp->uvfs_lock);
			} else {
				uvfs_task_sync(uvfsp, cr);
			}
		}
		mutex_exit(&uvfs_fs_list_lock);
	}

	return (error);
}

static void
uvfs_freevfs(vfs_t *vfsp)
{
	uvfsvfs_free(vfsp->vfs_data);

	atomic_dec_32(&uvfs_active_fs_count);
}

void
uvfs_init(void)
{
	uvfs_uvnode_init();
	uvfs_task_init();

	list_create(&uvfs_fs_list, sizeof (uvfsvfs_t),
	    offsetof(uvfsvfs_t, uvfs_fs_list_node));

	uvfsvfs_cache = kmem_cache_create("uvfsvfs_cache", sizeof (uvfsvfs_t),
	    0, uvfsvfs_construct, uvfsvfs_destroy, NULL, NULL, NULL, 0);
}

void
uvfs_fini(void)
{
	/*
	 * Remove vfs ops
	 */
	ASSERT(uvfstype);
	(void) vfs_freevfsops_by_type(uvfstype);
	uvfstype = 0;

	kmem_cache_destroy(uvfsvfs_cache);
	list_destroy(&uvfs_fs_list);

	uvfs_task_fini();
	uvfs_uvnode_fini();
}
