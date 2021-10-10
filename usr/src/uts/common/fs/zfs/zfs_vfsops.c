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

/* Portions Copyright 2010 Robert Milkowski */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysmacros.h>
#include <sys/kmem.h>
#include <sys/klpd.h>
#include <sys/pathname.h>
#include <sys/vnode.h>
#include <sys/vfs.h>
#include <sys/vfs_opreg.h>
#include <sys/mntent.h>
#include <sys/mount.h>
#include <sys/cmn_err.h>
#include "fs/fs_subr.h"
#include <sys/zfs_znode.h>
#include <sys/zfs_dir.h>
#include <sys/zil.h>
#include <sys/fs/zfs.h>
#include <sys/dmu.h>
#include <sys/dsl_prop.h>
#include <sys/dsl_crypto.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_deleg.h>
#include <sys/spa.h>
#include <sys/zap.h>
#include <sys/unique.h>
#include <sys/sa.h>
#include <sys/varargs.h>
#include <sys/policy.h>
#include <sys/atomic.h>
#include <sys/mkdev.h>
#include <sys/modctl.h>
#include <sys/refstr.h>
#include <sys/zfs_ioctl.h>
#include <sys/zfs_ctldir.h>
#include <sys/zfs_fuid.h>
#include <sys/bootconf.h>
#include <sys/sunddi.h>
#include <sys/dnlc.h>
#include <sys/dmu_objset.h>
#include <sys/spa_boot.h>
#include <sys/sa.h>
#include "zfs_comutil.h"

int zfsfstype;
vfsops_t *zfs_vfsops = NULL;
static major_t zfs_major;
static minor_t zfs_minor;
static kmutex_t	zfs_dev_mtx;

extern int sys_shutdown;

static int zfs_mount(vfs_t *vfsp, vnode_t *mvp, struct mounta *uap, cred_t *cr);
static int zfs_umount(vfs_t *vfsp, int fflag, cred_t *cr);
static int zfs_mountroot(vfs_t *vfsp, enum whymountroot);
static int zfs_root(vfs_t *vfsp, vnode_t **vpp);
static int zfs_statvfs(vfs_t *vfsp, struct statvfs64 *statp);
static int zfs_vget(vfs_t *vfsp, vnode_t **vpp, fid_t *fidp);
static void zfs_freevfs(vfs_t *vfsp);

static const fs_operation_def_t zfs_vfsops_template[] = {
	VFSNAME_MOUNT,		{ .vfs_mount = zfs_mount },
	VFSNAME_MOUNTROOT,	{ .vfs_mountroot = zfs_mountroot },
	VFSNAME_UNMOUNT,	{ .vfs_unmount = zfs_umount },
	VFSNAME_ROOT,		{ .vfs_root = zfs_root },
	VFSNAME_STATVFS,	{ .vfs_statvfs = zfs_statvfs },
	VFSNAME_SYNC,		{ .vfs_sync = zfs_sync },
	VFSNAME_VGET,		{ .vfs_vget = zfs_vget },
	VFSNAME_FREEVFS,	{ .vfs_freevfs = zfs_freevfs },
	NULL,			NULL
};

static const fs_operation_def_t zfs_vfsops_eio_template[] = {
	VFSNAME_FREEVFS,	{ .vfs_freevfs =  zfs_freevfs },
	NULL,			NULL
};

/*
 * We need to keep a count of active fs's.
 * This is necessary to prevent our module
 * from being unloaded after a umount -f
 */
static uint32_t	zfs_active_fs_count = 0;

static char *noatime_cancel[] = { MNTOPT_ATIME, NULL };
static char *atime_cancel[] = { MNTOPT_NOATIME, NULL };
static char *noxattr_cancel[] = { MNTOPT_XATTR, NULL };
static char *xattr_cancel[] = { MNTOPT_NOXATTR, NULL };

/*
 * MO_DEFAULT is not used since the default value is determined
 * by the equivalent property.
 */
static mntopt_t mntopts[] = {
	{ MNTOPT_NOXATTR, noxattr_cancel, NULL, 0, NULL },
	{ MNTOPT_XATTR, xattr_cancel, NULL, 0, NULL },
	{ MNTOPT_NOATIME, noatime_cancel, NULL, 0, NULL },
	{ MNTOPT_ATIME, atime_cancel, NULL, 0, NULL },
	{ MNTOPT_ZFS_MOUNTPOINT, NULL, NULL, MO_HASVALUE, NULL }
};

static mntopts_t zfs_mntopts = {
	sizeof (mntopts) / sizeof (mntopt_t),
	mntopts
};

/*ARGSUSED*/
int
zfs_sync(vfs_t *vfsp, short flag, cred_t *cr)
{
	/*
	 * Data integrity is job one.  We don't want a compromised kernel
	 * writing to the storage pool, so we never sync during panic.
	 */
	if (panicstr)
		return (0);

	/*
	 * SYNC_ATTR is used by fsflush() to force old filesystems like UFS
	 * to sync metadata, which they would otherwise cache indefinitely.
	 * Semantically, the only requirement is that the sync be initiated.
	 * The DMU syncs out txgs frequently, so there's nothing to do.
	 */
	if (flag & SYNC_ATTR)
		return (0);

	if (vfsp != NULL) {
		/*
		 * Sync a specific filesystem.
		 */
		zfsvfs_t *zfsvfs = vfsp->vfs_data;
		dsl_pool_t *dp;

		ZFS_ENTER(zfsvfs);
		dp = dmu_objset_pool(zfsvfs->z_os);

		/*
		 * If the system is shutting down, then skip any
		 * filesystems which may exist on a suspended pool.
		 */
		if (sys_shutdown && spa_suspended(dp->dp_spa)) {
			ZFS_EXIT(zfsvfs);
			return (0);
		}

		zil_commit(zfsvfs->z_log, 0);

		ZFS_EXIT(zfsvfs);
	} else {
		/*
		 * Sync all ZFS filesystems.  This is what happens when you
		 * run sync(1M).  Unlike other filesystems, ZFS honors the
		 * request by waiting for all pools to commit all dirty data.
		 */
		spa_sync_allpools();
	}

	return (0);
}

static int
zfs_create_unique_device(dev_t *dev)
{
	major_t new_major;

	do {
		ASSERT3U(zfs_minor, <=, MAXMIN32);
		minor_t start = zfs_minor;
		do {
			mutex_enter(&zfs_dev_mtx);
			if (zfs_minor >= MAXMIN32) {
				/*
				 * If we're still using the real major
				 * keep out of /dev/zfs and /dev/zvol minor
				 * number space.  If we're using a getudev()'ed
				 * major number, we can use all of its minors.
				 */
				if (zfs_major == ddi_name_to_major(ZFS_DRIVER))
					zfs_minor = ZFS_MIN_MINOR;
				else
					zfs_minor = 0;
			} else {
				zfs_minor++;
			}
			*dev = makedevice(zfs_major, zfs_minor);
			mutex_exit(&zfs_dev_mtx);
		} while (vfs_devismounted(*dev) && zfs_minor != start);
		if (zfs_minor == start) {
			/*
			 * We are using all ~262,000 minor numbers for the
			 * current major number.  Create a new major number.
			 */
			if ((new_major = getudev()) == (major_t)-1) {
				cmn_err(CE_WARN,
				    "zfs_mount: Can't get unique major "
				    "device number.");
				return (-1);
			}
			mutex_enter(&zfs_dev_mtx);
			zfs_major = new_major;
			zfs_minor = 0;

			mutex_exit(&zfs_dev_mtx);
		} else {
			break;
		}
		/* CONSTANTCONDITION */
	} while (1);

	return (0);
}

static void
atime_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;

	if (newval == TRUE) {
		zfsvfs->z_atime = TRUE;
		vfs_clearmntopt(zfsvfs->z_vfs, MNTOPT_NOATIME);
		vfs_setmntopt(zfsvfs->z_vfs, MNTOPT_ATIME, NULL, 0);
	} else {
		zfsvfs->z_atime = FALSE;
		vfs_clearmntopt(zfsvfs->z_vfs, MNTOPT_ATIME);
		vfs_setmntopt(zfsvfs->z_vfs, MNTOPT_NOATIME, NULL, 0);
	}
}

static void
xattr_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;

	if (newval == TRUE) {
		/* XXX locking on vfs_flag? */
		zfsvfs->z_vfs->vfs_flag |= VFS_XATTR;
		vfs_clearmntopt(zfsvfs->z_vfs, MNTOPT_NOXATTR);
		vfs_setmntopt(zfsvfs->z_vfs, MNTOPT_XATTR, NULL, 0);
	} else {
		/* XXX locking on vfs_flag? */
		zfsvfs->z_vfs->vfs_flag &= ~VFS_XATTR;
		vfs_clearmntopt(zfsvfs->z_vfs, MNTOPT_XATTR);
		vfs_setmntopt(zfsvfs->z_vfs, MNTOPT_NOXATTR, NULL, 0);
	}
}

static void
blksz_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;

	if (newval < SPA_MINBLOCKSIZE ||
	    newval > spa_max_block_size(dmu_objset_spa(zfsvfs->z_os)) ||
	    !ISP2(newval))
		newval = spa_max_block_size(dmu_objset_spa(zfsvfs->z_os));

	zfsvfs->z_max_blksz = newval;
	zfsvfs->z_vfs->vfs_bsize = newval;
}

static void
readonly_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;

	if (newval) {
		/* XXX locking on vfs_flag? */
		zfsvfs->z_vfs->vfs_flag |= VFS_RDONLY;
		vfs_clearmntopt(zfsvfs->z_vfs, MNTOPT_RW);
		vfs_setmntopt(zfsvfs->z_vfs, MNTOPT_RO, NULL, 0);
	} else {
		/* XXX locking on vfs_flag? */
		zfsvfs->z_vfs->vfs_flag &= ~VFS_RDONLY;
		vfs_clearmntopt(zfsvfs->z_vfs, MNTOPT_RO);
		vfs_setmntopt(zfsvfs->z_vfs, MNTOPT_RW, NULL, 0);
	}
}

static void
devices_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;

	if (newval == FALSE) {
		zfsvfs->z_vfs->vfs_flag |= VFS_NODEVICES;
		vfs_clearmntopt(zfsvfs->z_vfs, MNTOPT_DEVICES);
		vfs_setmntopt(zfsvfs->z_vfs, MNTOPT_NODEVICES, NULL, 0);
	} else {
		zfsvfs->z_vfs->vfs_flag &= ~VFS_NODEVICES;
		vfs_clearmntopt(zfsvfs->z_vfs, MNTOPT_NODEVICES);
		vfs_setmntopt(zfsvfs->z_vfs, MNTOPT_DEVICES, NULL, 0);
	}
}

static void
setuid_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;

	if (newval == FALSE) {
		zfsvfs->z_vfs->vfs_flag |= VFS_NOSETUID;
		vfs_clearmntopt(zfsvfs->z_vfs, MNTOPT_SETUID);
		vfs_setmntopt(zfsvfs->z_vfs, MNTOPT_NOSETUID, NULL, 0);
	} else {
		zfsvfs->z_vfs->vfs_flag &= ~VFS_NOSETUID;
		vfs_clearmntopt(zfsvfs->z_vfs, MNTOPT_NOSETUID);
		vfs_setmntopt(zfsvfs->z_vfs, MNTOPT_SETUID, NULL, 0);
	}
}

static void
exec_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;

	if (newval == FALSE) {
		zfsvfs->z_vfs->vfs_flag |= VFS_NOEXEC;
		vfs_clearmntopt(zfsvfs->z_vfs, MNTOPT_EXEC);
		vfs_setmntopt(zfsvfs->z_vfs, MNTOPT_NOEXEC, NULL, 0);
	} else {
		zfsvfs->z_vfs->vfs_flag &= ~VFS_NOEXEC;
		vfs_clearmntopt(zfsvfs->z_vfs, MNTOPT_NOEXEC);
		vfs_setmntopt(zfsvfs->z_vfs, MNTOPT_EXEC, NULL, 0);
	}
}

/*
 * The nbmand mount option can be changed at mount time.
 * We can't allow it to be toggled on live file systems or incorrect
 * behavior may be seen from cifs clients
 *
 * This property isn't registered via dsl_prop_register(), but this callback
 * will be called when a file system is first mounted
 */
static void
nbmand_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;
	if (newval == FALSE) {
		vfs_clearmntopt(zfsvfs->z_vfs, MNTOPT_NBMAND);
		vfs_setmntopt(zfsvfs->z_vfs, MNTOPT_NONBMAND, NULL, 0);
	} else {
		vfs_clearmntopt(zfsvfs->z_vfs, MNTOPT_NONBMAND);
		vfs_setmntopt(zfsvfs->z_vfs, MNTOPT_NBMAND, NULL, 0);
	}
}

static void
snapdir_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;

	zfsvfs->z_show_ctldir = newval;
}

static void
vscan_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;

	zfsvfs->z_vscan = newval;
}

static void
rstchown_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;

	if (newval) {
		vfs_clear_feature(zfsvfs->z_vfs, VFSFT_NORSTCHOWN);
		vfs_clearmntopt(zfsvfs->z_vfs, MNTOPT_NORSTCHOWN);
		vfs_setmntopt(zfsvfs->z_vfs, MNTOPT_RSTCHOWN, NULL, 0);
	} else {
		vfs_set_feature(zfsvfs->z_vfs, VFSFT_NORSTCHOWN);
		vfs_clearmntopt(zfsvfs->z_vfs, MNTOPT_RSTCHOWN);
		vfs_setmntopt(zfsvfs->z_vfs, MNTOPT_NORSTCHOWN, NULL, 0);
	}
}

static void
acl_inherit_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;

	zfsvfs->z_acl_inherit = newval;
}

static void
acl_mode_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;

	/*
	 * ZFS_ACL_GROUPMASK has been deprecated.
	 * If newval == ZFS_ACL_GROUPMASK set ZFS_ACL_DISCARD.
	 */
	if (newval == ZFS_ACL_GROUPMASK)
		zfsvfs->z_acl_mode = ZFS_ACL_DISCARD;
	else
		zfsvfs->z_acl_mode = newval;
}

static int
zfs_register_callbacks(vfs_t *vfsp)
{
	struct dsl_dataset *ds = NULL;
	objset_t *os = NULL;
	zfsvfs_t *zfsvfs = NULL;
	uint64_t nbmand;
	int readonly, do_readonly = B_FALSE;
	int setuid, do_setuid = B_FALSE;
	int exec, do_exec = B_FALSE;
	int devices, do_devices = B_FALSE;
	int xattr, do_xattr = B_FALSE;
	int atime, do_atime = B_FALSE;
	int rstchown, do_rstchown = B_FALSE;
	int error = 0;

	ASSERT(vfsp);
	zfsvfs = vfsp->vfs_data;
	ASSERT(zfsvfs);
	os = zfsvfs->z_os;

	/*
	 * The act of registering our callbacks will destroy any mount
	 * options we may have.  In order to enable temporary overrides
	 * of mount options, we stash away the current values and
	 * restore them after we register the callbacks.
	 */
	if (vfs_optionisset(vfsp, MNTOPT_RO, NULL) ||
	    !spa_writeable(dmu_objset_spa(os))) {
		readonly = B_TRUE;
		do_readonly = B_TRUE;
	} else if (vfs_optionisset(vfsp, MNTOPT_RW, NULL)) {
		readonly = B_FALSE;
		do_readonly = B_TRUE;
	}
	if (vfs_optionisset(vfsp, MNTOPT_NOSUID, NULL)) {
		devices = B_FALSE;
		setuid = B_FALSE;
		do_devices = B_TRUE;
		do_setuid = B_TRUE;
	} else {
		if (vfs_optionisset(vfsp, MNTOPT_NODEVICES, NULL)) {
			devices = B_FALSE;
			do_devices = B_TRUE;
		} else if (vfs_optionisset(vfsp, MNTOPT_DEVICES, NULL)) {
			devices = B_TRUE;
			do_devices = B_TRUE;
		}

		if (vfs_optionisset(vfsp, MNTOPT_NOSETUID, NULL)) {
			setuid = B_FALSE;
			do_setuid = B_TRUE;
		} else if (vfs_optionisset(vfsp, MNTOPT_SETUID, NULL)) {
			setuid = B_TRUE;
			do_setuid = B_TRUE;
		}
	}
	if (vfs_optionisset(vfsp, MNTOPT_NOEXEC, NULL)) {
		exec = B_FALSE;
		do_exec = B_TRUE;
	} else if (vfs_optionisset(vfsp, MNTOPT_EXEC, NULL)) {
		exec = B_TRUE;
		do_exec = B_TRUE;
	}
	if (vfs_optionisset(vfsp, MNTOPT_NOXATTR, NULL)) {
		xattr = B_FALSE;
		do_xattr = B_TRUE;
	} else if (vfs_optionisset(vfsp, MNTOPT_XATTR, NULL)) {
		xattr = B_TRUE;
		do_xattr = B_TRUE;
	}
	if (vfs_optionisset(vfsp, MNTOPT_NOATIME, NULL)) {
		atime = B_FALSE;
		do_atime = B_TRUE;
	} else if (vfs_optionisset(vfsp, MNTOPT_ATIME, NULL)) {
		atime = B_TRUE;
		do_atime = B_TRUE;
	}
	if (vfs_optionisset(vfsp, MNTOPT_NORSTCHOWN, NULL)) {
		rstchown = B_FALSE;
		do_rstchown = B_TRUE;
	} else if (vfs_optionisset(vfsp, MNTOPT_RSTCHOWN, NULL)) {
		rstchown = B_TRUE;
		do_rstchown = B_TRUE;
	}

	/*
	 * nbmand is a special property.  It can only be changed at
	 * mount time.
	 *
	 * This is weird, but it is documented to only be changeable
	 * at mount time.
	 */
	if (vfs_optionisset(vfsp, MNTOPT_NONBMAND, NULL)) {
		nbmand = B_FALSE;
	} else if (vfs_optionisset(vfsp, MNTOPT_NBMAND, NULL)) {
		nbmand = B_TRUE;
	} else {
		char osname[MAXNAMELEN];

		dmu_objset_name(os, osname);
		if (error = dsl_prop_get_integer(osname, "nbmand", &nbmand,
		    NULL)) {
			return (error);
		}
	}

	/*
	 * Register property callbacks.
	 *
	 * It would probably be fine to just check for i/o error from
	 * the first prop_register(), but I guess I like to go
	 * overboard...
	 */
	ds = dmu_objset_ds(os);
	error = dsl_prop_register(ds, "atime", atime_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    "xattr", xattr_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    "recordsize", blksz_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    "readonly", readonly_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    "devices", devices_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    "setuid", setuid_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    "exec", exec_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    "snapdir", snapdir_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    "aclinherit", acl_inherit_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    "aclmode", acl_mode_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    "vscan", vscan_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    "rstchown", rstchown_changed_cb, zfsvfs);
	if (error)
		goto unregister;

	/*
	 * Invoke our callbacks to restore temporary mount options.
	 */
	if (do_readonly)
		readonly_changed_cb(zfsvfs, readonly);
	if (do_setuid)
		setuid_changed_cb(zfsvfs, setuid);
	if (do_exec)
		exec_changed_cb(zfsvfs, exec);
	if (do_devices)
		devices_changed_cb(zfsvfs, devices);
	if (do_xattr)
		xattr_changed_cb(zfsvfs, xattr);
	if (do_atime)
		atime_changed_cb(zfsvfs, atime);
	if (do_rstchown)
		rstchown_changed_cb(zfsvfs, rstchown);

	nbmand_changed_cb(zfsvfs, nbmand);

	return (0);

unregister:
	/*
	 * We may attempt to unregister some callbacks that are not
	 * registered, but this is OK; it will simply return ENOMSG,
	 * which we will ignore.
	 */
	(void) dsl_prop_unregister(ds, "atime", atime_changed_cb, zfsvfs);
	(void) dsl_prop_unregister(ds, "xattr", xattr_changed_cb, zfsvfs);
	(void) dsl_prop_unregister(ds, "recordsize", blksz_changed_cb, zfsvfs);
	(void) dsl_prop_unregister(ds, "readonly", readonly_changed_cb, zfsvfs);
	(void) dsl_prop_unregister(ds, "devices", devices_changed_cb, zfsvfs);
	(void) dsl_prop_unregister(ds, "setuid", setuid_changed_cb, zfsvfs);
	(void) dsl_prop_unregister(ds, "exec", exec_changed_cb, zfsvfs);
	(void) dsl_prop_unregister(ds, "snapdir", snapdir_changed_cb, zfsvfs);
	(void) dsl_prop_unregister(ds, "aclinherit", acl_inherit_changed_cb,
	    zfsvfs);
	(void) dsl_prop_unregister(ds, "aclmode", acl_mode_changed_cb,
	    zfsvfs);
	(void) dsl_prop_unregister(ds, "vscan", vscan_changed_cb, zfsvfs);
	(void) dsl_prop_unregister(ds, "rstchown", rstchown_changed_cb, zfsvfs);
	return (error);

}

static int
zfs_space_delta_cb(dmu_object_type_t type, void *data,
    uint64_t *userp, uint64_t *groupp)
{
	/*
	 * Is it a valid type of object to track?
	 */
	if (type != DMU_OT_ZNODE && type != DMU_OT_SA)
		return (ENOENT);

	/*
	 * If we have a NULL data pointer then assume the id's aren't changing.
	 * Return EEXIST to the caller to let it know to use the current ids.
	 */
	if (data == NULL)
		return (EEXIST);

	switch (type) {
	case DMU_OT_ZNODE: {
		znode_phys_t *znp = data;

		*userp = znp->zp_uid;
		*groupp = znp->zp_gid;
		break;
	}
	case DMU_OT_SA: {
		uintptr_t zdata = (uintptr_t)data + sa_hdrsize(data);

		if (zdata == (uintptr_t)data) {
			/*
			 * Newly created file; znode data not filled in yet.
			 */
			*userp = 0;
			*groupp = 0;
		} else {
			*userp = *(uint64_t *)(zdata + SA_UID_OFFSET);
			*groupp = *(uint64_t *)(zdata + SA_GID_OFFSET);
		}
	}
	}
	return (0);
}

static void
fuidstr_to_sid(zfsvfs_t *zfsvfs, const char *fuidstr,
    char *domainbuf, int buflen, uid_t *ridp)
{
	uint64_t fuid;
	const char *domain;

	fuid = strtonum(fuidstr, NULL);

	domain = zfs_fuid_find_by_idx(zfsvfs, FUID_INDEX(fuid));
	if (domain)
		(void) strlcpy(domainbuf, domain, buflen);
	else
		domainbuf[0] = '\0';
	*ridp = FUID_RID(fuid);
}

static uint64_t
zfs_userquota_prop_to_obj(zfsvfs_t *zfsvfs, zfs_userquota_prop_t type)
{
	switch (type) {
	case ZFS_PROP_USERUSED:
		return (DMU_USERUSED_OBJECT);
	case ZFS_PROP_GROUPUSED:
		return (DMU_GROUPUSED_OBJECT);
	case ZFS_PROP_USERQUOTA:
		return (zfsvfs->z_userquota_obj);
	case ZFS_PROP_GROUPQUOTA:
		return (zfsvfs->z_groupquota_obj);
	}
	return (0);
}

int
zfs_userspace_many(zfsvfs_t *zfsvfs, zfs_userquota_prop_t type,
    uint64_t *cookiep, void *vbuf, uint64_t *bufsizep)
{
	int error;
	zap_cursor_t zc;
	zap_attribute_t za;
	zfs_useracct_t *buf = vbuf;
	uint64_t obj;

	if (!dmu_objset_userspace_present(zfsvfs->z_os))
		return (ENOTSUP);

	obj = zfs_userquota_prop_to_obj(zfsvfs, type);
	if (obj == 0) {
		*bufsizep = 0;
		return (0);
	}

	for (zap_cursor_init_serialized(&zc, zfsvfs->z_os, obj, *cookiep);
	    (error = zap_cursor_retrieve(&zc, &za)) == 0;
	    zap_cursor_advance(&zc)) {
		if ((uintptr_t)buf - (uintptr_t)vbuf + sizeof (zfs_useracct_t) >
		    *bufsizep)
			break;

		fuidstr_to_sid(zfsvfs, za.za_name,
		    buf->zu_domain, sizeof (buf->zu_domain), &buf->zu_rid);

		buf->zu_space = za.za_first_integer;
		buf++;
	}
	if (error == ENOENT)
		error = 0;

	ASSERT3U((uintptr_t)buf - (uintptr_t)vbuf, <=, *bufsizep);
	*bufsizep = (uintptr_t)buf - (uintptr_t)vbuf;
	*cookiep = zap_cursor_serialize(&zc);
	zap_cursor_fini(&zc);
	return (error);
}

/*
 * buf must be big enough (eg, 32 bytes)
 */
static int
id_to_fuidstr(zfsvfs_t *zfsvfs, const char *domain, uid_t rid,
    char *buf, boolean_t addok)
{
	uint64_t fuid;
	int domainid = 0;

	if (domain && domain[0]) {
		domainid = zfs_fuid_find_by_domain(zfsvfs, domain, NULL, addok);
		if (domainid == -1)
			return (ENOENT);
	}
	fuid = FUID_ENCODE(domainid, rid);
	(void) sprintf(buf, "%llx", (longlong_t)fuid);
	return (0);
}

int
zfs_userspace_one(zfsvfs_t *zfsvfs, zfs_userquota_prop_t type,
    const char *domain, uint64_t rid, uint64_t *valp)
{
	char buf[32];
	int err;
	uint64_t obj;

	*valp = 0;

	if (!dmu_objset_userspace_present(zfsvfs->z_os))
		return (ENOTSUP);

	obj = zfs_userquota_prop_to_obj(zfsvfs, type);
	if (obj == 0)
		return (0);

	err = id_to_fuidstr(zfsvfs, domain, rid, buf, B_FALSE);
	if (err)
		return (err);

	err = zap_lookup(zfsvfs->z_os, obj, buf, 8, 1, valp);
	if (err == ENOENT)
		err = 0;
	return (err);
}

int
zfs_set_userquota(zfsvfs_t *zfsvfs, zfs_userquota_prop_t type,
    const char *domain, uint64_t rid, uint64_t quota)
{
	char buf[32];
	int err;
	dmu_tx_t *tx;
	uint64_t *objp;
	boolean_t fuid_dirtied;

	if (type != ZFS_PROP_USERQUOTA && type != ZFS_PROP_GROUPQUOTA)
		return (EINVAL);

	if (zfsvfs->z_version < ZPL_VERSION_USERSPACE)
		return (ENOTSUP);

	objp = (type == ZFS_PROP_USERQUOTA) ? &zfsvfs->z_userquota_obj :
	    &zfsvfs->z_groupquota_obj;

	err = id_to_fuidstr(zfsvfs, domain, rid, buf, B_TRUE);
	if (err)
		return (err);
	fuid_dirtied = zfsvfs->z_fuid_dirty;

	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_zap(tx, *objp ? *objp : DMU_NEW_OBJECT, B_TRUE, NULL);
	if (*objp == 0) {
		dmu_tx_hold_zap(tx, MASTER_NODE_OBJ, B_TRUE,
		    zfs_userquota_prop_prefixes[type]);
	}
	if (fuid_dirtied)
		zfs_fuid_txhold(zfsvfs, tx);
	err = dmu_tx_assign(tx, TXG_WAIT);
	if (err) {
		dmu_tx_abort(tx);
		return (err);
	}

	mutex_enter(&zfsvfs->z_lock);
	if (*objp == 0) {
		*objp = zap_create(zfsvfs->z_os, DMU_OT_USERGROUP_QUOTA,
		    DMU_OT_NONE, 0, tx);
		VERIFY(0 == zap_add(zfsvfs->z_os, MASTER_NODE_OBJ,
		    zfs_userquota_prop_prefixes[type], 8, 1, objp, tx));
	}
	mutex_exit(&zfsvfs->z_lock);

	if (quota == 0) {
		err = zap_remove(zfsvfs->z_os, *objp, buf, tx);
		if (err == ENOENT)
			err = 0;
	} else {
		err = zap_update(zfsvfs->z_os, *objp, buf, 8, 1, &quota, tx);
	}
	ASSERT(err == 0);
	if (fuid_dirtied)
		zfs_fuid_sync(zfsvfs, tx);
	dmu_tx_commit(tx);
	return (err);
}

boolean_t
zfs_fuid_overquota(zfsvfs_t *zfsvfs, boolean_t isgroup, uint64_t fuid)
{
	char buf[32];
	uint64_t used, quota, usedobj, quotaobj;
	int err;

	usedobj = isgroup ? DMU_GROUPUSED_OBJECT : DMU_USERUSED_OBJECT;
	quotaobj = isgroup ? zfsvfs->z_groupquota_obj : zfsvfs->z_userquota_obj;

	if (quotaobj == 0 || zfsvfs->z_replay)
		return (B_FALSE);

	(void) sprintf(buf, "%llx", (longlong_t)fuid);
	err = zap_lookup(zfsvfs->z_os, quotaobj, buf, 8, 1, &quota);
	if (err != 0)
		return (B_FALSE);

	err = zap_lookup(zfsvfs->z_os, usedobj, buf, 8, 1, &used);
	if (err != 0)
		return (B_FALSE);
	return (used >= quota);
}

boolean_t
zfs_owner_overquota(zfsvfs_t *zfsvfs, znode_t *zp, boolean_t isgroup)
{
	uint64_t fuid;
	uint64_t quotaobj;

	quotaobj = isgroup ? zfsvfs->z_groupquota_obj : zfsvfs->z_userquota_obj;

	fuid = isgroup ? zp->z_gid : zp->z_uid;

	if (quotaobj == 0 || zfsvfs->z_replay)
		return (B_FALSE);

	return (zfs_fuid_overquota(zfsvfs, isgroup, fuid));
}

int
zfsvfs_create(const char *osname, zfsvfs_t **zfvp)
{
	objset_t *os;
	zfsvfs_t *zfsvfs;
	uint64_t zval;
	int i, error;
	uint64_t sa_obj;

	zfsvfs = kmem_zalloc(sizeof (zfsvfs_t), KM_SLEEP);

	/*
	 * We claim to always be readonly so we can open snapshots;
	 * other ZPL code will prevent us from writing to snapshots.
	 */
	error = dmu_objset_own(osname, DMU_OST_ZFS, B_TRUE, zfsvfs, &os);
	if (error) {
		kmem_free(zfsvfs, sizeof (zfsvfs_t));
		return (error);
	}

	/*
	 * Initialize the zfs-specific filesystem structure.
	 * Should probably make this a kmem cache, shuffle fields,
	 * and just bzero up to z_hold_mtx[].
	 */
	zfsvfs->z_vfs = NULL;
	zfsvfs->z_parent = zfsvfs;
	zfsvfs->z_max_blksz = spa_max_block_size(os->os_spa);
	zfsvfs->z_show_ctldir = ZFS_SNAPDIR_VISIBLE;
	zfsvfs->z_os = os;

	error = zfs_get_zplprop(os, ZFS_PROP_VERSION, &zfsvfs->z_version);
	if (error) {
		goto out;
	} else if (zfsvfs->z_version >
	    zfs_zpl_version_map(spa_version(dmu_objset_spa(os)))) {
		(void) printf("Can't mount a version %lld file system "
		    "on a version %lld pool\n. Pool must be upgraded to mount "
		    "this file system.", (u_longlong_t)zfsvfs->z_version,
		    (u_longlong_t)spa_version(dmu_objset_spa(os)));
		error = ENOTSUP;
		goto out;
	}
	if ((error = zfs_get_zplprop(os, ZFS_PROP_NORMALIZE, &zval)) != 0)
		goto out;
	zfsvfs->z_norm = (int)zval;

	if ((error = zfs_get_zplprop(os, ZFS_PROP_UTF8ONLY, &zval)) != 0)
		goto out;
	zfsvfs->z_utf8 = (zval != 0);

	if ((error = zfs_get_zplprop(os, ZFS_PROP_CASE, &zval)) != 0)
		goto out;
	zfsvfs->z_case = (uint_t)zval;

	/*
	 * Fold case on file systems that are always or sometimes case
	 * insensitive.
	 */
	if (zfsvfs->z_case == ZFS_CASE_INSENSITIVE ||
	    zfsvfs->z_case == ZFS_CASE_MIXED)
		zfsvfs->z_norm |= U8_TEXTPREP_TOUPPER;

	zfsvfs->z_use_fuids = USE_FUIDS(zfsvfs->z_version, zfsvfs->z_os);
	zfsvfs->z_use_sa = USE_SA(zfsvfs->z_version, zfsvfs->z_os);

	if (zfsvfs->z_use_sa) {
		/* should either have both of these objects or none */
		error = zap_lookup(os, MASTER_NODE_OBJ, ZFS_SA_ATTRS, 8, 1,
		    &sa_obj);
		if (error)
			return (error);
	} else {
		/*
		 * Pre SA versions file systems should never touch
		 * either the attribute registration or layout objects.
		 */
		sa_obj = 0;
	}

	error = sa_setup(os, sa_obj, zfs_attr_table, ZPL_END,
	    &zfsvfs->z_attr_table);
	if (error)
		goto out;

	if (zfsvfs->z_version >= ZPL_VERSION_SA)
		sa_register_update_callback(os, zfs_sa_upgrade);

	error = zap_lookup(os, MASTER_NODE_OBJ, ZFS_ROOT_OBJ, 8, 1,
	    &zfsvfs->z_root);
	if (error)
		goto out;
	ASSERT(zfsvfs->z_root != 0);

	error = zap_lookup(os, MASTER_NODE_OBJ, ZFS_UNLINKED_SET, 8, 1,
	    &zfsvfs->z_unlinkedobj);
	if (error)
		goto out;

	error = zap_lookup(os, MASTER_NODE_OBJ,
	    zfs_userquota_prop_prefixes[ZFS_PROP_USERQUOTA],
	    8, 1, &zfsvfs->z_userquota_obj);
	if (error && error != ENOENT)
		goto out;

	error = zap_lookup(os, MASTER_NODE_OBJ,
	    zfs_userquota_prop_prefixes[ZFS_PROP_GROUPQUOTA],
	    8, 1, &zfsvfs->z_groupquota_obj);
	if (error && error != ENOENT)
		goto out;

	error = zap_lookup(os, MASTER_NODE_OBJ, ZFS_FUID_TABLES, 8, 1,
	    &zfsvfs->z_fuid_obj);
	if (error && error != ENOENT)
		goto out;

	error = zap_lookup(os, MASTER_NODE_OBJ, ZFS_SHARES_DIR, 8, 1,
	    &zfsvfs->z_shares_dir);
	if (error && error != ENOENT)
		goto out;

	if (is_system_labeled()) {
		char	ds_mlslabel[MAXLABELSTR];	/* dataset mlslabel */

		error = zfs_get_mlslabel(os, ds_mlslabel, sizeof (ds_mlslabel));
		if ((error == 0) && (strcasecmp(ds_mlslabel,
		    ZFS_MLSLABEL_NONE) != 0))  {
			bslabel_t	ds_slabel;	/* dataset label */

			error = hexstr_to_label(ds_mlslabel, &ds_slabel);
			if (error != 0)
				goto out;
			zfsvfs->z_tslabel = labelalloc(&ds_slabel,
			    default_doi, KM_SLEEP);
			ASSERT(zfsvfs->z_tslabel);
		}
	}

	mutex_init(&zfsvfs->z_znodes_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&zfsvfs->z_lock, NULL, MUTEX_DEFAULT, NULL);
	list_create(&zfsvfs->z_all_znodes, sizeof (znode_t),
	    offsetof(znode_t, z_link_node));
	rrw_init(&zfsvfs->z_teardown_lock);
	rw_init(&zfsvfs->z_teardown_inactive_lock, NULL, RW_DEFAULT, NULL);
	rw_init(&zfsvfs->z_fuid_lock, NULL, RW_DEFAULT, NULL);
	for (i = 0; i != ZFS_OBJ_MTX_SZ; i++)
		mutex_init(&zfsvfs->z_hold_mtx[i], NULL, MUTEX_DEFAULT, NULL);

	*zfvp = zfsvfs;
	return (0);

out:
	dmu_objset_disown(os, zfsvfs);
	*zfvp = NULL;
	kmem_free(zfsvfs, sizeof (zfsvfs_t));
	return (error);
}

static int
zfsvfs_setup(zfsvfs_t *zfsvfs, boolean_t mounting)
{
	int error;

	error = zfs_register_callbacks(zfsvfs->z_vfs);
	if (error)
		return (error);

	/*
	 * Set the objset user_ptr to track its zfsvfs.
	 */
	mutex_enter(&zfsvfs->z_os->os_user_ptr_lock);
	dmu_objset_set_user(zfsvfs->z_os, zfsvfs);
	mutex_exit(&zfsvfs->z_os->os_user_ptr_lock);

	zfsvfs->z_log = zil_open(zfsvfs->z_os, zfs_get_data);

	/*
	 * If we are not mounting (ie: online recv), then we don't
	 * have to worry about replaying the log as we blocked all
	 * operations out since we closed the ZIL.
	 */
	if (mounting) {
		boolean_t readonly;

		/*
		 * During replay we remove the read only flag to
		 * allow replays to succeed.
		 */
		readonly = zfsvfs->z_vfs->vfs_flag & VFS_RDONLY;
		if (readonly != 0)
			zfsvfs->z_vfs->vfs_flag &= ~VFS_RDONLY;
		else
			zfs_unlinked_drain(zfsvfs);

		/*
		 * Parse and replay the intent log.
		 *
		 * Because of ziltest, this must be done after
		 * zfs_unlinked_drain().  (Further note: ziltest
		 * doesn't use readonly mounts, where
		 * zfs_unlinked_drain() isn't called.)  This is because
		 * ziltest causes spa_sync() to think it's committed,
		 * but actually it is not, so the intent log contains
		 * many txg's worth of changes.
		 *
		 * In particular, if object N is in the unlinked set in
		 * the last txg to actually sync, then it could be
		 * actually freed in a later txg and then reallocated
		 * in a yet later txg.  This would write a "create
		 * object N" record to the intent log.  Normally, this
		 * would be fine because the spa_sync() would have
		 * written out the fact that object N is free, before
		 * we could write the "create object N" intent log
		 * record.
		 *
		 * But when we are in ziltest mode, we advance the "open
		 * txg" without actually spa_sync()-ing the changes to
		 * disk.  So we would see that object N is still
		 * allocated and in the unlinked set, and there is an
		 * intent log record saying to allocate it.
		 */
		if (spa_writeable(dmu_objset_spa(zfsvfs->z_os))) {
			if (zil_replay_disable) {
				zil_destroy(zfsvfs->z_log, B_FALSE);
			} else {
				zfsvfs->z_replay = B_TRUE;
				zil_replay(zfsvfs->z_os, zfsvfs,
				    zfs_replay_vector);
				zfsvfs->z_replay = B_FALSE;
			}
		}
		zfsvfs->z_vfs->vfs_flag |= readonly; /* restore readonly bit */
	}

	return (0);
}

void
zfsvfs_free(zfsvfs_t *zfsvfs)
{
	int i;
	extern krwlock_t zfsvfs_lock; /* in zfs_znode.c */

	/*
	 * This is a barrier to prevent the filesystem from going away in
	 * zfs_znode_move() until we can safely ensure that the filesystem is
	 * not unmounted. We consider the filesystem valid before the barrier
	 * and invalid after the barrier.
	 */
	rw_enter(&zfsvfs_lock, RW_READER);
	rw_exit(&zfsvfs_lock);

	zfs_fuid_destroy(zfsvfs);

	mutex_destroy(&zfsvfs->z_znodes_lock);
	mutex_destroy(&zfsvfs->z_lock);
	list_destroy(&zfsvfs->z_all_znodes);
	rrw_destroy(&zfsvfs->z_teardown_lock);
	rw_destroy(&zfsvfs->z_teardown_inactive_lock);
	rw_destroy(&zfsvfs->z_fuid_lock);
	for (i = 0; i != ZFS_OBJ_MTX_SZ; i++)
		mutex_destroy(&zfsvfs->z_hold_mtx[i]);
	kmem_free(zfsvfs, sizeof (zfsvfs_t));
}

static void
zfs_set_fuid_feature(zfsvfs_t *zfsvfs)
{
	zfsvfs->z_use_fuids = USE_FUIDS(zfsvfs->z_version, zfsvfs->z_os);
	if (zfsvfs->z_vfs) {
		if (zfsvfs->z_use_fuids) {
			vfs_set_feature(zfsvfs->z_vfs, VFSFT_XVATTR);
			vfs_set_feature(zfsvfs->z_vfs, VFSFT_SYSATTR_VIEWS);
			vfs_set_feature(zfsvfs->z_vfs, VFSFT_ACEMASKONACCESS);
			vfs_set_feature(zfsvfs->z_vfs, VFSFT_ACLONCREATE);
			vfs_set_feature(zfsvfs->z_vfs, VFSFT_ACCESS_FILTER);
			vfs_set_feature(zfsvfs->z_vfs, VFSFT_REPARSE);
		} else {
			vfs_clear_feature(zfsvfs->z_vfs, VFSFT_XVATTR);
			vfs_clear_feature(zfsvfs->z_vfs, VFSFT_SYSATTR_VIEWS);
			vfs_clear_feature(zfsvfs->z_vfs, VFSFT_ACEMASKONACCESS);
			vfs_clear_feature(zfsvfs->z_vfs, VFSFT_ACLONCREATE);
			vfs_clear_feature(zfsvfs->z_vfs, VFSFT_ACCESS_FILTER);
			vfs_clear_feature(zfsvfs->z_vfs, VFSFT_REPARSE);
		}
	}
	zfsvfs->z_use_sa = USE_SA(zfsvfs->z_version, zfsvfs->z_os);
}

void
zfs_unregister_callbacks(zfsvfs_t *zfsvfs)
{
	objset_t *os = zfsvfs->z_os;
	struct dsl_dataset *ds;

	/*
	 * Unregister properties.
	 */
	if (!dmu_objset_is_snapshot(os)) {
		ds = dmu_objset_ds(os);
		VERIFY(dsl_prop_unregister(ds, "atime", atime_changed_cb,
		    zfsvfs) == 0);

		VERIFY(dsl_prop_unregister(ds, "xattr", xattr_changed_cb,
		    zfsvfs) == 0);

		VERIFY(dsl_prop_unregister(ds, "recordsize", blksz_changed_cb,
		    zfsvfs) == 0);

		VERIFY(dsl_prop_unregister(ds, "readonly", readonly_changed_cb,
		    zfsvfs) == 0);

		VERIFY(dsl_prop_unregister(ds, "devices", devices_changed_cb,
		    zfsvfs) == 0);

		VERIFY(dsl_prop_unregister(ds, "setuid", setuid_changed_cb,
		    zfsvfs) == 0);

		VERIFY(dsl_prop_unregister(ds, "exec", exec_changed_cb,
		    zfsvfs) == 0);

		VERIFY(dsl_prop_unregister(ds, "snapdir", snapdir_changed_cb,
		    zfsvfs) == 0);

		VERIFY(dsl_prop_unregister(ds, "aclinherit",
		    acl_inherit_changed_cb, zfsvfs) == 0);

		VERIFY(dsl_prop_unregister(ds, "aclmode",
		    acl_mode_changed_cb, zfsvfs) == 0);

		VERIFY(dsl_prop_unregister(ds, "vscan",
		    vscan_changed_cb, zfsvfs) == 0);

		VERIFY(dsl_prop_unregister(ds, "rstchown",
		    rstchown_changed_cb, zfsvfs) == 0);
	}
}

/*
 * Teardown the zfsvfs::z_os.
 *
 * Note, if 'unmounting' if FALSE, we return with the 'z_teardown_lock'
 * and 'z_teardown_inactive_lock' held.
 */
static int
zfsvfs_teardown(zfsvfs_t *zfsvfs, boolean_t unmounting)
{
	znode_t	*zp;

	rrw_enter(&zfsvfs->z_teardown_lock, RW_WRITER, FTAG);

	if (!unmounting) {
		/*
		 * We purge the parent filesystem's vfsp as the parent
		 * filesystem and all of its snapshots have their vnode's
		 * v_vfsp set to the parent's filesystem's vfsp.  Note,
		 * 'z_parent' is self referential for non-snapshots.
		 */
		(void) dnlc_purge_vfsp(zfsvfs->z_parent->z_vfs, 0);
	}

	/*
	 * Close the zil. NB: Can't close the zil while zfs_inactive
	 * threads are blocked as zil_close can call zfs_inactive.
	 */
	if (zfsvfs->z_log) {
		zil_close(zfsvfs->z_log);
		zfsvfs->z_log = NULL;
	}

	rw_enter(&zfsvfs->z_teardown_inactive_lock, RW_WRITER);

	/*
	 * If we are not unmounting (ie: online recv) and someone already
	 * unmounted this file system while we were doing the switcheroo,
	 * or a reopen of z_os failed then just bail out now.
	 */
	if (!unmounting && (zfsvfs->z_unmounted || zfsvfs->z_os == NULL)) {
		rw_exit(&zfsvfs->z_teardown_inactive_lock);
		rrw_exit(&zfsvfs->z_teardown_lock, FTAG);
		return (EIO);
	}

	/*
	 * At this point there are no vops active, and any new vops will
	 * fail with EIO since we have z_teardown_lock for writer (only
	 * relevant for forced unmount).
	 *
	 * Release all holds on dbufs.
	 */
	mutex_enter(&zfsvfs->z_znodes_lock);
	for (zp = list_head(&zfsvfs->z_all_znodes); zp != NULL;
	    zp = list_next(&zfsvfs->z_all_znodes, zp))
		if (zp->z_sa_hdl) {
			ASSERT(ZTOV(zp)->v_count > 0);
			zfs_znode_dmu_fini(zp);
		}
	mutex_exit(&zfsvfs->z_znodes_lock);

	/*
	 * z_os will be NULL if there was an error in attempting to reopen
	 * zfsvfs.
	 */
	if (zfsvfs->z_os) {
		/*
		 * Unregister properties.
		 */
		zfs_unregister_callbacks(zfsvfs);

		/*
		 * Evict cached data
		 */
		if (dmu_objset_is_dirty(zfsvfs->z_os))
			txg_wait_synced(dmu_objset_pool(zfsvfs->z_os), 0);
		dmu_objset_evict_dbufs(zfsvfs->z_os);
	}

	/*
	 * If we are unmounting, set the unmounted flag and let new vops
	 * unblock.  zfs_inactive will have the unmounted behavior, and all
	 * other vops will fail with EIO.
	 */
	if (unmounting) {
		zfsvfs->z_unmounted = B_TRUE;
		rrw_exit(&zfsvfs->z_teardown_lock, FTAG);
		rw_exit(&zfsvfs->z_teardown_inactive_lock);
	}
	return (0);
}

/*
 * zfs_get_mlslabel
 *
 * Returns the mlslabel property from the master node to the specified
 * buffer. (zfs_get_zplprop() can't be used for strings....)
 * Returns 0 for success.
 */
int
zfs_get_mlslabel(objset_t *os, char *buf, size_t buflen)
{
	int		error;
	dsl_dataset_t	*ds;

	error = zap_lookup(os, MASTER_NODE_OBJ,
	    zfs_prop_to_name(ZFS_PROP_MLSLABEL), 1, buflen, buf);
	if (error != ENOENT)
		return (error);

	/*
	 * If there's no mlslabel in the master node, check the
	 * dsl in case this may be an older dataset.  If a real
	 * (non-default) label is found there, attempt to migrate
	 * it to the master node if labeling is enabled.
	 */
	ds = dmu_objset_ds(os);
	rw_enter(&DS_POOL(ds)->dp_config_rwlock, RW_READER);
	error = dsl_prop_get_ds(ds, zfs_prop_to_name(ZFS_PROP_MLSLABEL),
	    1, buflen, buf, NULL, DSL_PROP_GET_EFFECTIVE);
	rw_exit(&DS_POOL(ds)->dp_config_rwlock);
	if (error != 0)
		return (error);

	if (is_system_labeled() && (strcasecmp(buf, ZFS_MLSLABEL_NONE) != 0)) {
		char	osname[MAXNAMELEN];

		dmu_objset_name(os, osname);
		(void) zfs_set_mlslabel(osname, buf);
	}

	return (0);
}

int
zfs_set_mlslabel(const char *dsname, char *strval)
{
	objset_t	*os;
	dmu_tx_t	*tx;
	int		sllen;
	int		error;

	sllen = strlen(strval) + 1;

	if (zfs_pool_status(dsname, POOL_CHECK_READONLY))
		return (EROFS);

	error = dmu_objset_hold(dsname, FTAG, &os);
	if (error)
		return (error);

	tx = dmu_tx_create(os);
	dmu_tx_hold_zap(tx, MASTER_NODE_OBJ, B_TRUE,
	    zfs_prop_to_name(ZFS_PROP_MLSLABEL));
	error = dmu_tx_assign(tx, TXG_WAIT);
	if (error) {
		dmu_tx_abort(tx);
		dmu_objset_rele(os, FTAG);
		return (error);
	}

	/* Record mlslabel in master node */
	error = zap_update(os, MASTER_NODE_OBJ,
	    zfs_prop_to_name(ZFS_PROP_MLSLABEL),
	    1, sllen, strval, tx);
	dmu_tx_commit(tx);
	dmu_objset_rele(os, FTAG);

	if (error == 0) {
		/* Clear any vestigial dsl label (on old datasets) */
		(void) dsl_prop_set(dsname, zfs_prop_to_name(ZFS_PROP_MLSLABEL),
		    ZPROP_SRC_NONE, 0, 0, NULL, 0);
	}

	return (error);
}

static int
zfs_domount(vfs_t *vfsp, char *osname)
{
	dev_t mount_dev;
	uint64_t recordsize, fsid_guid;
	int error = 0;
	zfsvfs_t *zfsvfs;

	ASSERT(vfsp);
	ASSERT(osname);

	error = zfsvfs_create(osname, &zfsvfs);
	if (error)
		return (error);
	zfsvfs->z_vfs = vfsp;

	/* Initialize the generic filesystem structure. */
	vfsp->vfs_bcount = 0;
	vfsp->vfs_data = NULL;

	if (zfs_create_unique_device(&mount_dev) == -1) {
		error = ENODEV;
		goto out;
	}
	ASSERT(vfs_devismounted(mount_dev) == 0);

	if (error = dsl_prop_get_integer(osname, "recordsize", &recordsize,
	    NULL))
		goto out;

	vfsp->vfs_dev = mount_dev;
	vfsp->vfs_fstype = zfsfstype;
	vfsp->vfs_bsize = recordsize;
	vfsp->vfs_flag |= VFS_NOTRUNC;
	vfsp->vfs_data = zfsvfs;

	/*
	 * Set features for file system.
	 */
	zfs_set_fuid_feature(zfsvfs);
	if (zfsvfs->z_case == ZFS_CASE_INSENSITIVE) {
		vfs_set_feature(vfsp, VFSFT_DIRENTFLAGS);
		vfs_set_feature(vfsp, VFSFT_CASEINSENSITIVE);
		vfs_set_feature(vfsp, VFSFT_NOCASESENSITIVE);
	} else if (zfsvfs->z_case == ZFS_CASE_MIXED) {
		vfs_set_feature(vfsp, VFSFT_DIRENTFLAGS);
		vfs_set_feature(vfsp, VFSFT_CASEINSENSITIVE);
	}
	vfs_set_feature(vfsp, VFSFT_ZEROCOPY_SUPPORTED);

	if (dmu_objset_is_snapshot(zfsvfs->z_os)) {
		uint64_t pval;

		atime_changed_cb(zfsvfs, B_FALSE);
		readonly_changed_cb(zfsvfs, B_TRUE);
		if (error = dsl_prop_get_integer(osname, "xattr", &pval, NULL))
			goto out;
		xattr_changed_cb(zfsvfs, pval);
		zfsvfs->z_issnap = B_TRUE;
		zfsvfs->z_os->os_sync = ZFS_SYNC_DISABLED;

		mutex_enter(&zfsvfs->z_os->os_user_ptr_lock);
		dmu_objset_set_user(zfsvfs->z_os, zfsvfs);
		mutex_exit(&zfsvfs->z_os->os_user_ptr_lock);
	} else {
		error = zfsvfs_setup(zfsvfs, B_TRUE);
	}

	/*
	 * The fsid comes from one of two places.
	 * 1. There is a special system attribute that specifies what
	 *    it should be.
	 * 2. It is derived from the fsid_guid on the objset.
	 *    The fsid is 64 bits, composed of an 8-bit fs type, which
	 *    separates our fsid from any other filesystem types, and a
	 *    56-bit objset unique ID.  The objset unique ID is unique to
	 *    all objsets open on this system, provided by unique_create().
	 *    The 8-bit fs type must be put in the low bits of fsid[1]
	 *    because that's where other Solaris filesystems put it.
	 *
	 * N.B., Although snapshots inherit their system attributes
	 * from the parent, we don't want to mount snapshots using
	 * that system attribute since that would cause a collision with
	 * the vfs_fsid of the parent.  Therefore, always use the DMU
	 * objectset's fsid_guid to generate the fsid for snapshot
	 * mounts.  This should be fine for migration/replication
	 * since (a) snapshots of shared filesystems share the exported
	 * fsid, but have a separate fid space and (b) we would never
	 * migrate/replicate snapshots independently of the parent FS.
	 */

	if (zfsvfs->z_issnap == B_TRUE ||
	    zap_lookup(zfsvfs->z_os,
	    MASTER_NODE_OBJ, ZFS_FSID, 8, 1, &fsid_guid) != 0) {
		/* Either a snapshot or no "fixed fsid" available */
		fsid_guid = dmu_objset_fsid_guid(zfsvfs->z_os);
		ASSERT((fsid_guid & ~((1ULL<<56)-1)) == 0);
	}

	vfsp->vfs_fsid.val[0] = fsid_guid;
	vfsp->vfs_fsid.val[1] = ((fsid_guid>>32) << 8) |
	    zfsfstype & 0xFF;

	/*
	 * Don't allow ZFS filesystems with duplicate FSIDs
	 * to be mounted.
	 */
	if (vfs_dup_detect(vfsp)) {
		VERIFY(zfsvfs_teardown(zfsvfs, B_TRUE) == 0);
		error = EEXIST;
		goto out;
	}

	if (!zfsvfs->z_issnap)
		zfsctl_create(zfsvfs);

out:
	if (error) {
		/*
		 * We need to unset the objset user_ptr (which is
		 * currently set to to zfsvfs) before we disown the
		 * objset and free zfsvfs.
		 */
		mutex_enter(&zfsvfs->z_os->os_user_ptr_lock);
		dmu_objset_set_user(zfsvfs->z_os, NULL);
		mutex_exit(&zfsvfs->z_os->os_user_ptr_lock);

		dmu_objset_disown(zfsvfs->z_os, zfsvfs);
		zfsvfs_free(zfsvfs);
	} else {
		atomic_add_32(&zfs_active_fs_count, 1);
	}

	return (error);
}

/*
 * Convert a decimal digit string to a uint64_t integer.
 */
static int
str_to_uint64(char *str, uint64_t *objnum)
{
	uint64_t num = 0;

	while (*str) {
		if (*str < '0' || *str > '9')
			return (EINVAL);

		num = num*10 + *str++ - '0';
	}

	*objnum = num;
	return (0);
}

/*
 * The boot path passed from the boot loader is in the form of
 * "rootpool-name/root-filesystem-object-number'. Convert this
 * string to a dataset name: "rootpool-name/root-filesystem-name".
 */
static int
zfs_parse_bootfs(char *bpath, char *outpath)
{
	char *slashp;
	uint64_t objnum;
	int error;

	if (*bpath == 0 || *bpath == '/')
		return (EINVAL);

	(void) strcpy(outpath, bpath);

	slashp = strchr(bpath, '/');

	/* if no '/', just return the pool name */
	if (slashp == NULL) {
		return (0);
	}

	/* if not a number, just return the root dataset name */
	if (str_to_uint64(slashp+1, &objnum)) {
		return (0);
	}

	*slashp = '\0';
	error = dsl_dsobj_to_dsname(bpath, objnum, outpath);
	*slashp = '/';

	return (error);
}

/*
 * zfs_check_global_label:
 *	Check that the hex label string is appropriate for the dataset
 *	being mounted into the global_zone proper.
 *
 *	Return an error if the hex label string is not default or
 *	admin_low/admin_high.  For admin_low labels, the corresponding
 *	dataset must be readonly.
 */
int
zfs_check_global_label(const char *dsname, const char *mlslabel)
{
	if (strcasecmp(mlslabel, ZFS_MLSLABEL_NONE) == 0)
		return (0);
	if (strcasecmp(mlslabel, ADMIN_HIGH) == 0)
		return (0);
	if (strcasecmp(mlslabel, ADMIN_LOW) == 0) {
		/* must be readonly */
		uint64_t rdonly;

		if (dsl_prop_get_integer(dsname,
		    zfs_prop_to_name(ZFS_PROP_READONLY), &rdonly, NULL))
			return (EACCES);
		return (rdonly ? 0 : EACCES);
	}
	return (EACCES);
}

/*
 * zfs_mount_zone:
 *	Returns the zone corresponding to the specified mountpoint,
 *	or NULL if unknown.  Caller must zone_rele valid returned zone.
 */
static zone_t *
zfs_mount_zone(char *mntpt, char *osname)
{
	zone_t		*mntzone;

	mntzone = zone_find_by_any_path(mntpt, B_FALSE);
	if (mntzone->zone_id == GLOBAL_ZONEID) {
		uint64_t zoned = B_TRUE;

		(void) dsl_prop_get_integer(osname,
		    zfs_prop_to_name(ZFS_PROP_ZONED), &zoned, NULL);
		if (zoned != B_FALSE) {
			zone_rele(mntzone);
			return (NULL);
		}
	}
	return (mntzone);
}


/*
 * zfs_mount_label_policy:
 *	Determine whether the mount is allowed according to MAC check.
 *	by comparing (where appropriate) label of the dataset against
 *	the label of the zone being mounted into.  If the dataset has
 *	no label, create one.
 *
 *	Returns:
 *		 0 :	access allowed
 *		>0 :	error code, such as EACCES
 */
static int
zfs_mount_label_policy(vfs_t *vfsp, char *osname)
{
	int		error, retv;
	objset_t	*os;
	zone_t		*mntzone = NULL;
	ts_label_t	*mnt_tsl;
	bslabel_t	*mnt_slabel;		/* mountpoint label */
	bslabel_t	ds_slabel;		/* dataset label */
	char		ds_mlslabel[MAXLABELSTR];

	retv = EACCES;				/* assume the worst */

	/*
	 * Start by getting the dataset label if it exists.
	 */

	/*
	 * Get the dataset label if it exists.
	 */
	error = dmu_objset_hold(osname, FTAG, &os);
	if (error)
		return (error);
	error = zfs_get_mlslabel(os, ds_mlslabel, sizeof (ds_mlslabel));
	dmu_objset_rele(os, FTAG);
	if (error)
		return (EACCES);

	/*
	 * If labeling is NOT enabled, then disallow the mount of datasets
	 * which have a non-default label already.  No other label checks
	 * are needed.
	 */
	if (!is_system_labeled()) {
		if (strcasecmp(ds_mlslabel, ZFS_MLSLABEL_NONE) == 0)
			return (0);
		return (EACCES);
	}

	/*
	 * Get the label of the mountpoint.  If mounting into the global
	 * zone (i.e. mountpoint is not within an active zone and the
	 * zoned property is off), the label must be default or
	 * admin_low/admin_high only; no other checks are needed.
	 */

	mntzone = zfs_mount_zone((char *)refstr_value(vfsp->vfs_mntpt), osname);
	if (mntzone == NULL) {
		/*
		 * This is probably the case of a zone root dataset being
		 * mounted initially, before the zone has been fully created;
		 * allow this mount into global zone.
		 */
		return (0);
	}
	if (mntzone->zone_id == GLOBAL_ZONEID) {
		zone_rele(mntzone);
		return (zfs_check_global_label(osname, ds_mlslabel));
	}

	mnt_tsl = mntzone->zone_slabel;
	ASSERT(mnt_tsl != NULL);
	label_hold(mnt_tsl);
	mnt_slabel = label2bslabel(mnt_tsl);

	if (strcasecmp(ds_mlslabel, ZFS_MLSLABEL_NONE) == 0) {
		/*
		 * The dataset doesn't have a real label, so fabricate one.
		 */
		char *str = NULL;

		retv = l_to_str_internal(mnt_slabel, &str);
		if (retv == 0) {
			retv = zfs_set_mlslabel(osname, str);
		}
		if (str != NULL)
			kmem_free(str, strlen(str) + 1);
	} else if (hexstr_to_label(ds_mlslabel, &ds_slabel) == 0) {
		/*
		 * Now compare labels to complete the MAC check.  If the
		 * labels are equal then allow access.  If the mountpoint
		 * label dominates the dataset label, allow readonly access.
		 * Otherwise, access is denied.
		 */
		if (blequal(mnt_slabel, &ds_slabel))
			retv = 0;
		else if (bldominates(mnt_slabel, &ds_slabel)) {
			vfs_setmntopt(vfsp, MNTOPT_RO, NULL, 0);
			retv = 0;
		}
	}

	label_rele(mnt_tsl);
	zone_rele(mntzone);
	return (retv);
}

static int
zfs_mountroot(vfs_t *vfsp, enum whymountroot why)
{
	int error = 0;
	static int zfsrootdone = 0;
	zfsvfs_t *zfsvfs = NULL;
	znode_t *zp = NULL;
	vnode_t *vp = NULL;
	char *zfs_bootfs;
	char *zfs_devid;

	ASSERT(vfsp);

	/*
	 * The filesystem that we mount as root is defined in the
	 * boot property "zfs-bootfs" with a format of
	 * "poolname/root-dataset-objnum".
	 */
	if (why == ROOT_INIT) {
		if (zfsrootdone++)
			return (EBUSY);
		/*
		 * the process of doing a spa_load will require the
		 * clock to be set before we could (for example) do
		 * something better by looking at the timestamp on
		 * an uberblock, so just set it to -1.
		 */
		clkset(-1);

		if ((zfs_bootfs = spa_get_bootprop("zfs-bootfs")) == NULL) {
			cmn_err(CE_NOTE, "spa_get_bootfs: can not get "
			    "bootfs name");
			return (EINVAL);
		}
		zfs_devid = spa_get_bootprop("diskdevid");
		error = spa_import_rootpool(rootfs.bo_name, zfs_devid);
		if (zfs_devid)
			spa_free_bootprop(zfs_devid);
		if (error) {
			spa_free_bootprop(zfs_bootfs);
			cmn_err(CE_NOTE, "spa_import_rootpool: error %d",
			    error);
			return (error);
		}
		if (error = zfs_parse_bootfs(zfs_bootfs, rootfs.bo_name)) {
			spa_free_bootprop(zfs_bootfs);
			cmn_err(CE_NOTE, "zfs_parse_bootfs: error %d",
			    error);
			return (error);
		}

		spa_free_bootprop(zfs_bootfs);

		if (error = vfs_lock(vfsp))
			return (error);

		if (error = zfs_domount(vfsp, rootfs.bo_name)) {
			cmn_err(CE_NOTE, "zfs_domount: error %d", error);
			goto out;
		}

		zfsvfs = (zfsvfs_t *)vfsp->vfs_data;
		ASSERT(zfsvfs);
		if (error = zfs_zget(zfsvfs, zfsvfs->z_root, &zp)) {
			cmn_err(CE_NOTE, "zfs_zget: error %d", error);
			goto out;
		}

		vp = ZTOV(zp);
		mutex_enter(&vp->v_lock);
		vp->v_flag |= VROOT;
		mutex_exit(&vp->v_lock);
		rootvp = vp;

		/*
		 * Leave rootvp held.  The root file system is never unmounted.
		 */

		vfs_add((struct vnode *)0, vfsp,
		    (vfsp->vfs_flag & VFS_RDONLY) ? MS_RDONLY : 0);
out:
		vfs_unlock(vfsp);
		return (error);
	} else if (why == ROOT_REMOUNT) {
		readonly_changed_cb(vfsp->vfs_data, B_FALSE);
		vfsp->vfs_flag |= VFS_REMOUNT;

		/* refresh mount options */
		zfs_unregister_callbacks(vfsp->vfs_data);
		return (zfs_register_callbacks(vfsp));

	} else if (why == ROOT_UNMOUNT) {
		zfs_unregister_callbacks((zfsvfs_t *)vfsp->vfs_data);
		(void) zfs_sync(vfsp, 0, 0);
		return (0);
	}

	/*
	 * if "why" is equal to anything else other than ROOT_INIT,
	 * ROOT_REMOUNT, or ROOT_UNMOUNT, we do not support it.
	 */
	return (ENOTSUP);
}

/*ARGSUSED*/
static int
zfs_mount(vfs_t *vfsp, vnode_t *mvp, struct mounta *uap, cred_t *cr)
{
	char		*osname;
	pathname_t	spn;
	int		error = 0;
	uio_seg_t	fromspace = (uap->flags & MS_SYSSPACE) ?
	    UIO_SYSSPACE : UIO_USERSPACE;
	int		canwrite;
	zfs_crypt_key_status_t keystatus;
	zone_t		*zone = crgetzone(cr);

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

	/*
	 * ZFS does not support passing unparsed data in via MS_DATA.
	 * Users should use the MS_OPTIONSTR interface; this means
	 * that all option parsing is already done and the options struct
	 * can be interrogated.
	 */
	if ((uap->flags & MS_DATA) && uap->datalen > 0)
		return (EINVAL);

	/*
	 * Get the objset name (the "special" mount argument).  Perform
	 * dataset unaliasing if needed.
	 */
	if (zone != global_zone) {
		char realds[MAXPATHLEN];
		size_t len;

		/* Unalias the datset name. */
		if (fromspace == UIO_USERSPACE) {
			if ((error = copyinstr(uap->spec, realds,
			    sizeof (realds), &len)) != 0)
				return (error);
			if (zone_dataset_unalias(zone, realds, realds,
			    sizeof (realds)) > sizeof (realds) -1)
				return (ENXIO);
		} else {
			if (zone_dataset_unalias(zone, uap->spec, realds,
			    sizeof (realds)) > sizeof (realds) - 1)
				return (ENXIO);
		}

		/* Update the "resource" in the vfs with the unaliased name. */
		vfs_setresource(vfsp, realds, 0);

		error = pn_get(realds, UIO_SYSSPACE, &spn);
	} else {
		error = pn_get(uap->spec, fromspace, &spn);
	}
	if (error != 0)
		return (error);

	osname = spn.pn_path;

	/*
	 * Check for mount privilege?
	 *
	 * If we don't have privilege then see if
	 * we have local permission to allow it
	 */
	error = secpolicy_fs_mount(cr, mvp, vfsp);
	if (error) {
		if (dsl_deleg_access(osname, ZFS_DELEG_PERM_MOUNT, cr) == 0) {
			vattr_t		vattr;

			/*
			 * Make sure user is the owner of the mount point
			 * or has sufficient privileges.
			 */

			vattr.va_mask = AT_UID;

			if (VOP_GETATTR(mvp, &vattr, 0, cr, NULL)) {
				goto out;
			}

			if (secpolicy_vnode_owner(cr, vattr.va_uid) != 0 &&
			    VOP_ACCESS(mvp, VWRITE, 0, cr, NULL) != 0) {
				goto out;
			}
			secpolicy_fs_mount_clearopts(cr, vfsp);
		} else {
			goto out;
		}
	}

	/*
	 * Refuse to mount a filesystem if we are in a local zone and the
	 * dataset is not visible.  If we can't write it, we test a few
	 * additional requirements:
	 *	- The fs must be "zoned"
	 *	- The fs mounted on the default mount point.
	 *	- The VFS_MWAC flag is set if needed.
	 */
	if (!zone_dataset_visible(zone, osname, &canwrite)) {
		error = EPERM;
		goto out;
	}
	if (!canwrite) {
		uint64_t zoned;
		char *defmnt;
		char *mntpnt;
		zone_t *zonep = crgetzone(cr);
		int dlen, mlen, slen;
		char setpoint[MAXNAMELEN];
		char aosname[MAXNAMELEN];	/* alias for osname */

		/*
		 * verify we have a path, a locked zone and the zoned
		 * property.
		 */
		if ((zonep->zone_flags & ZF_LOCKDOWN) == 0 ||
		    mvp->v_path == NULL ||
		    !ZONE_PATH_VISIBLE(mvp->v_path, zonep) ||
		    dsl_prop_get_integer(osname,
		    zfs_prop_to_name(ZFS_PROP_ZONED), &zoned, NULL) ||
		    !zoned) {
			error = EPERM;
			goto out;
		}
		/* get default mount point */
		defmnt = kmem_alloc(MAXPATHLEN, KM_SLEEP);

		if (dsl_prop_get(osname, zfs_prop_to_name(ZFS_PROP_MOUNTPOINT),
		    1, MAXPATHLEN, defmnt, setpoint)) {
			kmem_free(defmnt, MAXPATHLEN);
			error = EPERM;
			goto out;
		}
		/*
		 * dsl_get_prop doesn't actually return the proper mountpoint;
		 * it will return the first "local" mountpoint down and it will
		 * return the dataset where it is defined in setpoint.
		 * The actual mountpoint is the default mountpoint plus
		 * the dataset name with the setpoint as prefix removed.
		 */
		mntpnt = ZONE_PATH_TRANSLATE(mvp->v_path, zonep);
		dlen = strlen(defmnt);
		mlen = strlen(mntpnt);
		slen = strlen(setpoint);
		if (zone_dataset_alias(zonep, osname, aosname,
		    sizeof (aosname)) > sizeof (aosname) - 1 || dlen > mlen ||
		    strncmp(mntpnt, defmnt, dlen) != 0 ||
		    strcmp(aosname + slen, mntpnt + dlen) != 0) {
			kmem_free(defmnt, MAXPATHLEN);
			error = EPERM;
			goto out;
		}
		if (!priv_mwac_mntpnt_writable(cr, defmnt))
			vfsp->vfs_flag |= VFS_MWAC;

		kmem_free(defmnt, MAXPATHLEN);
	}

	error = zfs_mount_label_policy(vfsp, osname);
	if (error)
		goto out;

	/*
	 * Make really sure we have the crypto key available if needed
	 * before continuing with the mount.
	 */
	error = dsl_dataset_keystatus_byname(osname, &keystatus);
	if (error) {
		goto out;
	} else if (keystatus == ZFS_CRYPT_KEY_UNAVAILABLE) {
		error = EACCES;
		goto out;
	}

	/*
	 * When doing a remount, we simply refresh our temporary properties
	 * according to those options set in the current VFS options.
	 */
	if (uap->flags & MS_REMOUNT) {
		/* refresh mount options */
		zfs_unregister_callbacks(vfsp->vfs_data);
		error = zfs_register_callbacks(vfsp);
		goto out;
	}

	error = zfs_domount(vfsp, osname);

	/*
	 * Add an extra VFS_HOLD on our parent vfs so that it can't
	 * disappear due to a forced unmount.
	 */
	if (error == 0 && ((zfsvfs_t *)vfsp->vfs_data)->z_issnap)
		VFS_HOLD(mvp->v_vfsp);

out:
	pn_free(&spn);
	return (error);
}

static int
zfs_statvfs(vfs_t *vfsp, struct statvfs64 *statp)
{
	zfsvfs_t *zfsvfs = vfsp->vfs_data;
	dev32_t d32;
	uint64_t refdbytes, availbytes, usedobjs, availobjs;

	ZFS_ENTER(zfsvfs);

	dmu_objset_space(zfsvfs->z_os,
	    &refdbytes, &availbytes, &usedobjs, &availobjs);

	/*
	 * The underlying storage pool actually uses multiple block sizes.
	 * We report the fragsize as the smallest block size we support,
	 * and we report our blocksize as the filesystem's maximum blocksize.
	 */
	statp->f_frsize = 1UL << SPA_MINBLOCKSHIFT;
	statp->f_bsize = zfsvfs->z_max_blksz;

	/*
	 * The following report "total" blocks of various kinds in the
	 * file system, but reported in terms of f_frsize - the
	 * "fragment" size.
	 */

	statp->f_blocks = (refdbytes + availbytes) >> SPA_MINBLOCKSHIFT;
	statp->f_bfree = availbytes >> SPA_MINBLOCKSHIFT;
	statp->f_bavail = statp->f_bfree; /* no root reservation */

	/*
	 * statvfs() should really be called statufs(), because it assumes
	 * static metadata.  ZFS doesn't preallocate files, so the best
	 * we can do is report the max that could possibly fit in f_files,
	 * and that minus the number actually used in f_ffree.
	 * For f_ffree, report the smaller of the number of object available
	 * and the number of blocks (each object will take at least a block).
	 */
	statp->f_ffree = MIN(availobjs, statp->f_bfree);
	statp->f_favail = statp->f_ffree;	/* no "root reservation" */
	statp->f_files = statp->f_ffree + usedobjs;

	(void) cmpldev(&d32, vfsp->vfs_dev);
	statp->f_fsid = d32;

	/*
	 * We're a zfs filesystem.
	 */
	(void) strcpy(statp->f_basetype, vfssw[vfsp->vfs_fstype].vsw_name);

	statp->f_flag = vf_to_stf(vfsp->vfs_flag);

	statp->f_namemax = ZFS_MAXNAMELEN;

	/*
	 * We have all of 32 characters to stuff a string here.
	 * Is there anything useful we could/should provide?
	 */
	bzero(statp->f_fstr, sizeof (statp->f_fstr));

	ZFS_EXIT(zfsvfs);
	return (0);
}

static int
zfs_root(vfs_t *vfsp, vnode_t **vpp)
{
	zfsvfs_t *zfsvfs = vfsp->vfs_data;
	znode_t *rootzp;
	int error;

	ZFS_ENTER(zfsvfs);

	error = zfs_zget(zfsvfs, zfsvfs->z_root, &rootzp);
	if (error == 0)
		*vpp = ZTOV(rootzp);

	ZFS_EXIT(zfsvfs);
	return (error);
}

/*ARGSUSED*/
static int
zfs_umount(vfs_t *vfsp, int fflag, cred_t *cr)
{
	zfsvfs_t *zfsvfs = vfsp->vfs_data;
	objset_t *os;
	int ret;

	ret = secpolicy_fs_unmount(cr, vfsp);
	if (ret) {
		if (dsl_deleg_access((char *)refstr_value(vfsp->vfs_resource),
		    ZFS_DELEG_PERM_MOUNT, cr))
			return (ret);
	}

	/*
	 * We purge the parent filesystem's vfsp as the parent filesystem
	 * and all of its snapshots have their vnode's v_vfsp set to the
	 * parent's filesystem's vfsp.  Note, 'z_parent' is self
	 * referential for non-snapshots.
	 */
	(void) dnlc_purge_vfsp(zfsvfs->z_parent->z_vfs, 0);

	/*
	 * Unmount any snapshots mounted under .zfs before unmounting the
	 * dataset itself.
	 */
	if (zfsvfs->z_ctldir != NULL &&
	    (ret = zfsctl_umount_snapshots(vfsp, fflag, cr)) != 0) {
		return (ret);
	}

	if (!(fflag & MS_FORCE)) {
		/*
		 * Check the number of active vnodes in the file system.
		 * Our count is maintained in the vfs structure, but the
		 * number is off by 1 to indicate a hold on the vfs
		 * structure itself.
		 *
		 * The '.zfs' directory maintains a reference of its
		 * own, and any active references underneath are
		 * reflected in the vnode count.
		 */
		if (zfsvfs->z_ctldir == NULL) {
			if (vfsp->vfs_count > 1)
				return (EBUSY);
		} else {
			if (vfsp->vfs_count > 2 ||
			    zfsvfs->z_ctldir->v_count > 1)
				return (EBUSY);
		}
	}

	vfsp->vfs_flag |= VFS_UNMOUNTED;

	VERIFY(zfsvfs_teardown(zfsvfs, B_TRUE) == 0);
	os = zfsvfs->z_os;

	/*
	 * z_os will be NULL if there was an error in
	 * attempting to reopen zfsvfs.
	 */
	if (os != NULL) {
		/*
		 * Unset the objset user_ptr.
		 */
		mutex_enter(&os->os_user_ptr_lock);
		dmu_objset_set_user(os, NULL);
		mutex_exit(&os->os_user_ptr_lock);

		/*
		 * Finally release the objset
		 */
		dmu_objset_disown(os, zfsvfs);
	}

	/*
	 * We can now safely destroy the '.zfs' directory node.
	 */
	if (zfsvfs->z_ctldir != NULL)
		zfsctl_destroy(zfsvfs);

	return (0);
}

static int
zfs_vget(vfs_t *vfsp, vnode_t **vpp, fid_t *fidp)
{
	zfsvfs_t	*zfsvfs = vfsp->vfs_data;
	znode_t		*zp;
	uint64_t	object = 0;
	uint64_t	fid_gen = 0;
	uint64_t	gen_mask;
	uint64_t	zp_gen;
	int 		i, err;

	*vpp = NULL;

	ZFS_ENTER(zfsvfs);

	if (fidp->fid_len == LONG_FID_LEN) {
		zfid_long_t	*zlfid = (zfid_long_t *)fidp;
		uint64_t	guid = 0;
		uint64_t	setgen = 0;

		for (i = 0; i < sizeof (zlfid->zf_guid); i++)
			guid |= ((uint64_t)zlfid->zf_guid[i]) << (8 * i);

		for (i = 0; i < sizeof (zlfid->zf_gen); i++)
			setgen |= ((uint64_t)zlfid->zf_gen[i]) << (8 * i);

		ZFS_EXIT(zfsvfs);

		err = zfsctl_lookup_objset(vfsp, guid, &zfsvfs);
		if (err)
			return (EINVAL);
		ZFS_ENTER(zfsvfs);
	}

	if (fidp->fid_len == SHORT_FID_LEN || fidp->fid_len == LONG_FID_LEN) {
		zfid_short_t	*zfid = (zfid_short_t *)fidp;

		for (i = 0; i < sizeof (zfid->zf_object); i++)
			object |= ((uint64_t)zfid->zf_object[i]) << (8 * i);

		for (i = 0; i < sizeof (zfid->zf_gen); i++)
			fid_gen |= ((uint64_t)zfid->zf_gen[i]) << (8 * i);
	} else {
		ZFS_EXIT(zfsvfs);
		return (EINVAL);
	}

	/* A zero fid_gen means we are in the .zfs control directories */
	if (fid_gen == 0 &&
	    (object == ZFSCTL_INO_ROOT || object == ZFSCTL_INO_SNAPDIR)) {
		*vpp = zfsvfs->z_ctldir;
		ASSERT(*vpp != NULL);
		if (object == ZFSCTL_INO_SNAPDIR) {
			VERIFY(zfsctl_root_lookup(*vpp, "snapshot", vpp, NULL,
			    0, NULL, NULL, NULL, NULL, NULL) == 0);
		} else {
			VN_HOLD(*vpp);
		}
		ZFS_EXIT(zfsvfs);
		return (0);
	}

	gen_mask = -1ULL >> (64 - 8 * i);

	dprintf("getting %llu [%u mask %llx]\n", object, fid_gen, gen_mask);
	if (err = zfs_zget(zfsvfs, object, &zp)) {
		ZFS_EXIT(zfsvfs);
		return (err);
	}
	zp_gen = zp->z_gen & gen_mask;
	if (zp_gen == 0)
		zp_gen = 1;
	if (zp->z_links == 0 || zp->z_unlinked || zp_gen != fid_gen) {
		dprintf("znode linkcnt = %ld, unlinked is %c, "
		    "gen (%u) ?== fid gen (%u)\n", zp->z_links,
		    zp->z_unlinked ? 'T' : 'F', zp_gen, fid_gen);
		VN_RELE(ZTOV(zp));
		ZFS_EXIT(zfsvfs);
		return (EINVAL);
	}

	*vpp = ZTOV(zp);
	ZFS_EXIT(zfsvfs);
	return (0);
}

/*
 * Block out VOPs and close zfsvfs_t::z_os
 *
 * Note, if successful, then we return with the 'z_teardown_lock' and
 * 'z_teardown_inactive_lock' write held.
 */
int
zfs_suspend_fs(zfsvfs_t *zfsvfs)
{
	int error;

	if ((error = zfsvfs_teardown(zfsvfs, B_FALSE)) != 0)
		return (error);
	dmu_objset_disown(zfsvfs->z_os, zfsvfs);

	return (0);
}

/*
 * Reopen zfsvfs_t::z_os and release VOPs.
 */
int
zfs_resume_fs(zfsvfs_t *zfsvfs, const char *osname)
{
	int err;

	ASSERT(RRW_WRITE_HELD(&zfsvfs->z_teardown_lock));
	ASSERT(RW_WRITE_HELD(&zfsvfs->z_teardown_inactive_lock));

	err = dmu_objset_own(osname, DMU_OST_ZFS, B_FALSE, zfsvfs,
	    &zfsvfs->z_os);
	if (err) {
		zfsvfs->z_os = NULL;
	} else {
		znode_t *zp;
		uint64_t sa_obj = 0;

		/*
		 * Make sure version hasn't changed
		 */

		err = zfs_get_zplprop(zfsvfs->z_os, ZFS_PROP_VERSION,
		    &zfsvfs->z_version);

		if (err)
			goto bail;

		err = zap_lookup(zfsvfs->z_os, MASTER_NODE_OBJ,
		    ZFS_SA_ATTRS, 8, 1, &sa_obj);

		if (err && zfsvfs->z_version >= ZPL_VERSION_SA)
			goto bail;

		if ((err = sa_setup(zfsvfs->z_os, sa_obj,
		    zfs_attr_table,  ZPL_END, &zfsvfs->z_attr_table)) != 0)
			goto bail;

		if (zfsvfs->z_version >= ZPL_VERSION_SA)
			sa_register_update_callback(zfsvfs->z_os,
			    zfs_sa_upgrade);

		VERIFY(zfsvfs_setup(zfsvfs, B_FALSE) == 0);

		zfs_set_fuid_feature(zfsvfs);

		/*
		 * Attempt to re-establish all the active znodes with
		 * their dbufs.  If a zfs_rezget() fails, then we'll let
		 * any potential callers discover that via ZFS_ENTER_VERIFY_VP
		 * when they try to use their znode.
		 */
		mutex_enter(&zfsvfs->z_znodes_lock);
		for (zp = list_head(&zfsvfs->z_all_znodes); zp;
		    zp = list_next(&zfsvfs->z_all_znodes, zp)) {
			(void) zfs_rezget(zp);
		}
		mutex_exit(&zfsvfs->z_znodes_lock);
	}

bail:
	/* release the VOPs */
	rw_exit(&zfsvfs->z_teardown_inactive_lock);
	rrw_exit(&zfsvfs->z_teardown_lock, FTAG);

	if (err) {
		/*
		 * Since we couldn't reopen zfsvfs::z_os, or
		 * setup the sa framework force unmount this file system.
		 */
		if (vn_vfswlock(zfsvfs->z_vfs->vfs_vnodecovered) == 0)
			(void) dounmount(zfsvfs->z_vfs, MS_FORCE, CRED());
	}
	return (err);
}

static void
zfs_freevfs(vfs_t *vfsp)
{
	zfsvfs_t *zfsvfs = vfsp->vfs_data;

	/*
	 * If this is a snapshot, we have an extra VFS_HOLD on our parent
	 * from zfs_mount().  Release it here.  If we came through
	 * zfs_mountroot() instead, we didn't grab an extra hold, so
	 * skip the VFS_RELE for rootvfs.
	 */
	if (zfsvfs->z_issnap && (vfsp != rootvfs))
		VFS_RELE(zfsvfs->z_parent->z_vfs);

	if (zfsvfs->z_tslabel != NULL)
		label_rele(zfsvfs->z_tslabel);

	zfsvfs_free(zfsvfs);

	atomic_add_32(&zfs_active_fs_count, -1);
}

/*
 * VFS_INIT() initialization.  Note that there is no VFS_FINI(),
 * so we can't safely do any non-idempotent initialization here.
 * Leave that to zfs_init() and zfs_fini(), which are called
 * from the module's _init() and _fini() entry points.
 */
/*ARGSUSED*/
static int
zfs_vfsinit(int fstype, char *name)
{
	int error;

	zfsfstype = fstype;

	/*
	 * Setup vfsops and vnodeops tables.
	 */
	error = vfs_setfsops(fstype, zfs_vfsops_template, &zfs_vfsops);
	if (error != 0) {
		cmn_err(CE_WARN, "zfs: bad vfs ops template");
	}

	error = zfs_create_op_tables();
	if (error) {
		zfs_remove_op_tables();
		cmn_err(CE_WARN, "zfs: bad vnode ops template");
		(void) vfs_freevfsops_by_type(zfsfstype);
		return (error);
	}

	mutex_init(&zfs_dev_mtx, NULL, MUTEX_DEFAULT, NULL);

	/*
	 * Unique major number for all zfs mounts.
	 * If we run out of 32-bit minors, we'll getudev() another major.
	 */
	zfs_major = ddi_name_to_major(ZFS_DRIVER);
	zfs_minor = ZFS_MIN_MINOR;

	return (0);
}

void
zfs_init(void)
{
	/*
	 * Initialize .zfs directory structures
	 */
	zfsctl_init();

	/*
	 * Initialize znode cache, vnode ops, etc...
	 */
	zfs_znode_init();

	dmu_objset_register_type(DMU_OST_ZFS, zfs_space_delta_cb);
}

void
zfs_fini(void)
{
	zfsctl_fini();
	zfs_znode_fini();
}

int
zfs_busy(void)
{
	return (zfs_active_fs_count != 0);
}

int
zfs_set_version(zfsvfs_t *zfsvfs, uint64_t newvers)
{
	int error;
	objset_t *os = zfsvfs->z_os;
	dmu_tx_t *tx;

	if (zfsvfs->z_vfs && (zfsvfs->z_vfs->vfs_flag & VFS_RDONLY))
		return (EROFS);

	if (newvers < ZPL_VERSION_INITIAL || newvers > ZPL_VERSION)
		return (EINVAL);

	if (newvers < zfsvfs->z_version)
		return (EINVAL);

	if (zfs_spa_version_map(newvers) >
	    spa_version(dmu_objset_spa(zfsvfs->z_os)))
		return (ENOTSUP);

	tx = dmu_tx_create(os);
	dmu_tx_hold_zap(tx, MASTER_NODE_OBJ, B_FALSE, ZPL_VERSION_STR);
	if (newvers >= ZPL_VERSION_SA && !zfsvfs->z_use_sa) {
		dmu_tx_hold_zap(tx, MASTER_NODE_OBJ, B_TRUE,
		    ZFS_SA_ATTRS);
		dmu_tx_hold_zap(tx, DMU_NEW_OBJECT, FALSE, NULL);
	}
	error = dmu_tx_assign(tx, TXG_WAIT);
	if (error) {
		dmu_tx_abort(tx);
		return (error);
	}

	error = zap_update(os, MASTER_NODE_OBJ, ZPL_VERSION_STR,
	    8, 1, &newvers, tx);

	if (error) {
		dmu_tx_commit(tx);
		return (error);
	}

	if (newvers >= ZPL_VERSION_SA && !zfsvfs->z_use_sa) {
		uint64_t sa_obj;

		ASSERT3U(spa_version(dmu_objset_spa(zfsvfs->z_os)), >=,
		    SPA_VERSION_SA);
		sa_obj = zap_create(os, DMU_OT_SA_MASTER_NODE,
		    DMU_OT_NONE, 0, tx);

		error = zap_add(os, MASTER_NODE_OBJ,
		    ZFS_SA_ATTRS, 8, 1, &sa_obj, tx);
		ASSERT3U(error, ==, 0);

		VERIFY(0 == sa_set_sa_object(os, sa_obj));
		sa_register_update_callback(os, zfs_sa_upgrade);
	}

	spa_history_log_internal(LOG_DS_UPGRADE,
	    dmu_objset_spa(os), tx, "oldver=%llu newver=%llu dataset = %llu",
	    zfsvfs->z_version, newvers, dmu_objset_id(os));

	dmu_tx_commit(tx);

	zfsvfs->z_version = newvers;

	zfs_set_fuid_feature(zfsvfs);

	return (0);
}

/*
 * Retrieves the "fixed fsid".
 */
int
zfs_get_fixed_fsid(zfsvfs_t *zfsvfs, uint64_t *fsidp)
{
	int error;
	objset_t *os = zfsvfs->z_os;

	error = zap_lookup(os, MASTER_NODE_OBJ, ZFS_FSID, 8, 1, fsidp);
	if (error == ENOENT) {
		/* Can't find the fixed fsid attr */
		error = ESRCH;
	}
	return (error);
}

/*
 * This is used to set or change the "fixed fsid".
 * The flags parameter takes values defined by zfs_fsid_flag_t in zfs.h.
 * If flag has ZFS_FSID_CREATE set, then we attempt to create the FSID
 * attribute.  Otherwise, this is just an update.
 *
 * Input:
 *	zfsvfs - Ptr to zfsvfs for the filesystem being updated
 *	newfsid - If ZFS_FSID_PARAM, use this value for the fixed fsid guid
 *	flags:
 *		ZFS_FSID_CREATE - Create the fsid guid attribute (but fail
 *		if it already exists)
 *
 *		ZFS_FSID_PARAM - Use "newfsid" as the fixed fsid guid.
 *		Otherwise, use a system-supplied value.
 */
int
zfs_set_fixed_fsid(zfsvfs_t *zfsvfs, uint64_t newfsid, uint32_t flags)
{
	int error;
	objset_t *os = zfsvfs->z_os;
	dmu_tx_t *tx;
	boolean_t create_fsid = (flags & ZFS_FSID_CREATE) != 0;
	uint64_t old_fsid_guid = 0LL;
	uint64_t fsid_guid = 0xcafed00d;

	/* FSID changes are *only* allowed on the top-level filesystem */
	if (dmu_objset_is_snapshot(os))
		return (EINVAL);

	error = zap_lookup(os, MASTER_NODE_OBJ, ZFS_FSID, 8, 1, &old_fsid_guid);
	if (error == 0 && create_fsid)
		error = EEXIST;
	else if (error == ENOENT)
		error = (create_fsid ? 0 : ESRCH);

	if (error)
		return (error);

	/*
	 * If the caller didn't supply a value for newfsid, then the system
	 * gets to choose.  Two cases:
	 *
	 * - if flags contains ZFS_FSID_CREATE, use the DMU's fsid_guid for
	 *   this dataset.  This is what ZFS uses by default for the FSID
	 * - Otherwise, call unique_create() to generate a non-conflicting
	 *   value for the fixed fsid
	 */
	if ((flags & ZFS_FSID_PARAM) == 0) {
		if (create_fsid) {
			fsid_guid = dmu_objset_fsid_guid(os);
			ASSERT((fsid_guid & ~((1ULL<<56)-1)) == 0);
		} else {
			/* reset */
			fsid_guid = unique_create();
		}
	} else {
		fsid_guid = newfsid;
	}

	tx = dmu_tx_create(os);
	dmu_tx_hold_zap(tx, MASTER_NODE_OBJ, create_fsid, ZFS_FSID);
	error = dmu_tx_assign(tx, TXG_WAIT);
	if (error) {
		dmu_tx_abort(tx);
		return (error);
	}

	error = zap_update(os, MASTER_NODE_OBJ, ZFS_FSID, 8, 1, &fsid_guid, tx);
	if (error) {
		dmu_tx_commit(tx);
		return (error);
	}

	if (create_fsid) {
		spa_history_log_internal(LOG_DS_SET_FSID,
		    dmu_objset_spa(os), tx,
		    "newfsid=0x%llx dataset = %llu",
		    fsid_guid, dmu_objset_id(os));
	} else {
		spa_history_log_internal(LOG_DS_RESET_FSID,
		    dmu_objset_spa(os), tx,
		    "oldfsid=0x%llx newfsid=0x%llx dataset = %llu",
		    old_fsid_guid, fsid_guid, dmu_objset_id(os));
	}

	dmu_tx_commit(tx);
	return (error);
}

/*
 * Removes the "fixed fsid".  If fsidp is non-NULL, save the value of
 * the removed fsid in *fsidp.
 */
int
zfs_remove_fixed_fsid(zfsvfs_t *zfsvfs, uint64_t *fsidp)
{
	int error;
	objset_t *os = zfsvfs->z_os;
	uint64_t tmp_fsid;
	dmu_tx_t *tx;

	error = zap_lookup(os, MASTER_NODE_OBJ, ZFS_FSID, 8, 1, &tmp_fsid);
	if (error)
		return (error == ENOENT ? ESRCH : error);

	if (fsidp)
		*fsidp = tmp_fsid;

	tx = dmu_tx_create(os);
	dmu_tx_hold_zap(tx, MASTER_NODE_OBJ, B_TRUE, ZFS_FSID);
	error = dmu_tx_assign(tx, TXG_WAIT);
	if (error) {
		dmu_tx_abort(tx);
		return (error);
	}

	error = zap_remove(os, MASTER_NODE_OBJ, ZFS_FSID, tx);

	spa_history_log_internal(LOG_DS_REMOVE_FSID,
	    dmu_objset_spa(os), tx,
	    "fsid=0x%llx dataset = %llu",
	    tmp_fsid, dmu_objset_id(os));

	dmu_tx_commit(tx);
	return (error);
}

/*
 * Read a property stored within the master node.
 */
int
zfs_get_zplprop(objset_t *os, zfs_prop_t prop, uint64_t *value)
{
	const char *pname;
	int error = ENOENT;

	/*
	 * Look up the file system's value for the property.  For the
	 * version property, we look up a slightly different string.
	 */
	if (prop == ZFS_PROP_VERSION)
		pname = ZPL_VERSION_STR;
	else
		pname = zfs_prop_to_name(prop);

	if (os != NULL)
		error = zap_lookup(os, MASTER_NODE_OBJ, pname, 8, 1, value);

	if (error == ENOENT) {
		/* No value set, use the default value */
		switch (prop) {
		case ZFS_PROP_VERSION:
			*value = ZPL_VERSION;
			break;
		case ZFS_PROP_NORMALIZE:
		case ZFS_PROP_UTF8ONLY:
			*value = 0;
			break;
		case ZFS_PROP_CASE:
			/*
			 * Older case sensitive file systems failed
			 * to save the sensitivity value in the master node
			 */
			if (os != NULL)
				*value = ZFS_CASE_SENSITIVE;
			else
				*value = ZFS_CASE_MIXED;
			break;
		default:
			return (error);
		}
		error = 0;
	}
	return (error);
}

/*
 * Search the vfs list for a specified resource.  Returns a pointer to it
 * or NULL if no suitable entry is found. The caller of this routine
 * is responsible for releasing the returned vfs pointer.
 */
vfs_t *
zfs_get_vfs(const char *resource)
{
	struct vfs *vfsp;
	struct vfs *vfs_found = NULL;

	vfs_list_read_lock();
	vfsp = rootvfs;
	do {
		if (strcmp(refstr_value(vfsp->vfs_resource), resource) == 0) {
			VFS_HOLD(vfsp);
			vfs_found = vfsp;
			break;
		}
		vfsp = vfsp->vfs_next;
	} while (vfsp != rootvfs);
	vfs_list_unlock();
	return (vfs_found);
}

static vfsdef_t vfw = {
	VFSDEF_VERSION,
	MNTTYPE_ZFS,
	zfs_vfsinit,
	VSW_HASPROTO|VSW_CANRWRO|VSW_CANREMOUNT|VSW_VOLATILEDEV|VSW_STATS|
	    VSW_XID|VSW_ZMOUNT|VSW_MWAC,
	&zfs_mntopts
};

struct modlfs zfs_modlfs = {
	&mod_fsops, "ZFS filesystem version " SPA_VERSION_STRING, &vfw
};
