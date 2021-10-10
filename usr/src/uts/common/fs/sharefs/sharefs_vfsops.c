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

#include <sys/atomic.h>
#include <sys/cmn_err.h>
#include <sys/errno.h>
#include <sys/mount.h>
#include <sharefs/sharefs.h>
#include <sys/vfs_opreg.h>
#include <sys/policy.h>
#include <sys/sunddi.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/stat.h>

#include <sys/mntent.h>
#include <sys/vfs.h>

/*
 * Kernel sharetab filesystem.
 *
 * This is a pseudo filesystem which exports information about shares currently
 * in kernel memory. The only element of the pseudo filesystem is a file.
 *
 * This file contains functions that interact with the VFS layer.
 *
 *	sharetab	sharefs_datanode_t	sharefs.c
 *
 */

vnodeops_t			*sharefs_ops_data;

static const fs_operation_def_t	sharefs_vfstops[];
static gfs_opsvec_t		 sharefs_opsvec[];

static int sharefs_init(int, char *);

/*
 * Module linkage
 */
static mntopts_t sharefs_mntopts = {
	0,
	NULL
};

static vfsdef_t vfw = {
	VFSDEF_VERSION,
	"sharefs",
	sharefs_init,
	VSW_HASPROTO | VSW_ZMOUNT,
	&sharefs_mntopts,
};

extern struct mod_ops	mod_fsops;

static struct modlfs modlfs = {
	&mod_fsops,
	"sharetab filesystem",
	&vfw
};

static struct modlinkage modlinkage = {
	MODREV_1,
	&modlfs,
	NULL
};

int
_init(void)
{
	return (mod_install(&modlinkage));
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

int
_fini(void)
{
	/*
	 * The sharetab filesystem cannot be unloaded.
	 */
	return (EBUSY);
}

/*
 * Filesystem initialization.
 */

static gfs_opsvec_t sharefs_opsvec[] = {
	{ "sharefs sharetab file", sharefs_tops_data, &sharefs_ops_data },
	{ NULL }
};

/* ARGSUSED */
static int
sharefs_init(int fstype, char *name)
{
	vfsops_t	*vfsops;
	int		error;

	mutex_init(&sfs.sg_lock, NULL, MUTEX_DEFAULT, NULL);
	sfs.sg_fstype = fstype;

	if (error = vfs_setfsops(fstype, sharefs_vfstops, &vfsops)) {
		cmn_err(CE_WARN, "sharefs_init: bad vfs ops template");
		return (error);
	}

	if (error = gfs_make_opsvec(sharefs_opsvec)) {
		(void) vfs_freevfsops(vfsops);
		return (error);
	}

	if ((sfs.sg_majordev = getudev()) == (major_t)-1) {
		cmn_err(CE_WARN,
		    "sharefs_init: can't get unique device number");
		sfs.sg_majordev = 0;
	}

	sfs.sg_zone_cnt = 0;
	list_create(&sfs.sg_zones, sizeof (sharefs_zone_t),
	    offsetof(sharefs_zone_t, sz_gnode));
	zone_key_create(&sfs.sg_sfszone_key, NULL, NULL, NULL);

	return (0);
}

/*
 * VFS entry points
 */
static int
sharefs_mount(vfs_t *vfsp, vnode_t *mvp, struct mounta *uap, cred_t *cr)
{
	sharefs_vfs_t	*data;
	dev_t		dev;
	sharefs_zone_t	*szp;

	if (secpolicy_fs_mount(cr, mvp, vfsp) != 0)
		return (EPERM);

	if ((uap->flags & MS_OVERLAY) == 0 &&
	    (mvp->v_count > 1 || (mvp->v_flag & VROOT)))
		return (EBUSY);

	if ((szp = sharefs_zone_create()) == NULL)
		return (ENOENT);

	mutex_enter(&szp->sz_lock);

	/*
	 * Initialize vfs fields
	 */
	vfsp->vfs_bsize = DEV_BSIZE;
	vfsp->vfs_fstype = sfs.sg_fstype;
	dev = makedevice(sfs.sg_majordev, szp->sz_minordev);
	vfs_make_fsid(&vfsp->vfs_fsid, dev, sfs.sg_fstype);

	if (vfs_devismounted(dev)) {
		mutex_exit(&szp->sz_lock);
		sharefs_zone_rele(szp);
		return (EINVAL);
	}

	ASSERT(szp->sz_mounted == 0);
	szp->sz_mounted = 1;
	mutex_exit(&szp->sz_lock);

	data = kmem_alloc(sizeof (sharefs_vfs_t), KM_SLEEP);
	vfsp->vfs_data = data;
	vfsp->vfs_dev = dev;

	/*
	 * Create root
	 */
	data->sharefs_zsd = szp;
	data->sharefs_vfs_root = sharefs_create_root_file(szp, vfsp);
	sharefs_zone_rele(szp);

	return (0);
}

static int
sharefs_unmount(vfs_t *vfsp, int flag, struct cred *cr)
{
	sharefs_vfs_t	*data;
	sharefs_zone_t *szp;

	if (secpolicy_fs_unmount(cr, vfsp) != 0)
		return (EPERM);

	/*
	 * We do not currently support forced unmounts
	 */
	if (flag & MS_FORCE)
		return (ENOTSUP);

	/*
	 * We should never have a reference count of less than 2: one for the
	 * caller, one for the root vnode.
	 */
	ASSERT(vfsp->vfs_count >= 2);

	/*
	 * Any active vnodes will result in a hold on the szp
	 */
	data = vfsp->vfs_data;
	szp = data->sharefs_zsd;

	mutex_enter(&szp->sz_lock);

	if (zone_status_get(szp->sz_zonep) != ZONE_IS_RUNNING) {
		/* Zone is shutting down clean up and get out! */
		(void) shtab_cache_flush(szp);
	}

	if ((data->sharefs_vfs_root->v_count > 1) ||
	    (szp->sz_shnode_count > 1)) {
		mutex_exit(&szp->sz_lock);
		return (EBUSY);
	}

	/*
	 * Only allow an unmount iff there are no entries in memory.
	 */
	if (!shtab_is_empty(szp)) {
		mutex_exit(&szp->sz_lock);
		return (EBUSY);
	}

	szp->sz_mounted = 0;

	/*
	 * Release the last hold on the root vnode
	 */
	ASSERT(data->sharefs_vfs_root->v_count == 1);
	ASSERT(szp->sz_shnode_count == 1);
	mutex_exit(&szp->sz_lock);

	VN_RELE(data->sharefs_vfs_root);

	kmem_free(data, sizeof (sharefs_vfs_t));


	return (0);
}

static int
sharefs_root(vfs_t *vfsp, vnode_t **vpp)
{
	sharefs_vfs_t	*data = vfsp->vfs_data;

	*vpp = data->sharefs_vfs_root;
	VN_HOLD(*vpp);

	return (0);
}

static int
sharefs_statvfs(vfs_t *vfsp, statvfs64_t *sp)
{
	dev32_t	d32;
	int	total = 1;

	bzero(sp, sizeof (*sp));
	sp->f_bsize = DEV_BSIZE;
	sp->f_frsize = DEV_BSIZE;
	sp->f_files = total;
	sp->f_ffree = sp->f_favail = INT_MAX - total;
	(void) cmpldev(&d32, vfsp->vfs_dev);
	sp->f_fsid = d32;
	(void) strlcpy(sp->f_basetype, vfssw[vfsp->vfs_fstype].vsw_name,
	    sizeof (sp->f_basetype));
	sp->f_flag = vf_to_stf(vfsp->vfs_flag);
	sp->f_namemax = SHAREFS_NAME_MAX;
	(void) strlcpy(sp->f_fstr, "sharefs", sizeof (sp->f_fstr));

	return (0);
}

static const fs_operation_def_t sharefs_vfstops[] = {
	{ VFSNAME_MOUNT,	{ .vfs_mount = sharefs_mount } },
	{ VFSNAME_UNMOUNT,	{ .vfs_unmount = sharefs_unmount } },
	{ VFSNAME_ROOT,		{ .vfs_root = sharefs_root } },
	{ VFSNAME_STATVFS,	{ .vfs_statvfs = sharefs_statvfs } },
	{ NULL }
};
