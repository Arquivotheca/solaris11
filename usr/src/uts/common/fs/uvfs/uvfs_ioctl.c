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
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/devops.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/int_fmtio.h>
#include <sys/door.h>
#include <sys/dnlc.h>
#include <sys/policy.h>

#include <sys/libuvfs_ki.h>
#include <sys/fs/uvfs.h>
#include <sys/uvfs.h>
#include <sys/uvfs_vfsops.h>
#include <sys/uvfs_upcall.h>
#include <sys/uvfs_task.h>

extern struct modlfs uvfs_modlfs;

extern void uvfs_init(void);
extern void uvfs_fini(void);

ldi_ident_t uvfs_li = NULL;
dev_info_t *uvfs_dip;

typedef int (*uvfsdev_ioctl_handler_t)(uintptr_t, int);

static int
fsid_to_uvfsvfs(uint64_t fsid, vfs_t **vfspp)
{
	fsid_t realfsid;
	vfs_t *vfsp;

	realfsid.val[0] = fsid >> 32;
	realfsid.val[1] = fsid & 0xffffffff;
	vfsp = getvfs(&realfsid);
	if (vfsp == NULL)
		return (ESRCH);

	if (! vfs_matchops(vfsp, uvfs_vfsops))
		return (EINVAL);

	*vfspp = vfsp;
	return (0);
}

/*
 * Wait for the file system's master door to be present in the
 * uvfsvfs.  In other words, wait for a daemon to be present.
 * If timeout_usec is zero, don't time out.
 */
int
uvfs_door_wait(uvfsvfs_t *uvfsvfs, uint64_t timeout_usec)
{
	clock_t waketime, cv_status;
	int error;

	ASSERT(mutex_owned(&uvfsvfs->uvfs_lock));

	for (;;) {
		if (uvfsvfs->uvfs_door) {
			error = 0;
			break;
		}
		if (sys_shutdown || (uvfsvfs->uvfs_flags & UVFS_UNMOUNTED)) {
			error = ETIMEDOUT;
			break;
		}
		if (timeout_usec > 0) {
			waketime = ddi_get_lbolt() + drv_usectohz(timeout_usec);
			cv_status = cv_timedwait_sig(&uvfsvfs->uvfs_daemon_cv,
			    &uvfsvfs->uvfs_lock, waketime);
			if (cv_status == 0) {
				error = EINTR;
				break;
			} else if (cv_status == -1) {
				error = ETIMEDOUT;
				break;
			}
		} else {
			if (! cv_wait_sig(&uvfsvfs->uvfs_daemon_cv,
			    &uvfsvfs->uvfs_lock)) {
				error = EINTR;
				break;
			}
		}
	}

	return (error);
}

static int
uvfsdev_ioctl_daemon_wait(uintptr_t argp, int flag)
{
	uvfs_ioc_daemon_wait_t arg;
	uvfsvfs_t *uvfsvfs;
	vfs_t *vfsp = NULL;
	int error;

	error = ddi_copyin((void *)argp, &arg, sizeof (arg), flag);
	if (error != 0)
		goto out;

	error = fsid_to_uvfsvfs(arg.uidw_fsid, &vfsp);
	if (error != 0)
		goto out;

	uvfsvfs = vfsp->vfs_data;
	mutex_enter(&uvfsvfs->uvfs_lock);
	error = uvfs_door_wait(uvfsvfs, arg.uidw_wait_usec);
	mutex_exit(&uvfsvfs->uvfs_lock);

out:
	if (vfsp != NULL)
		VFS_RELE(vfsp);

	return (error);
}

static int
uvfsdev_ioctl_fsparam_get(uintptr_t argp, int flag)
{
	uvfs_ioc_fsparam_get_t arg;
	uvfsvfs_t *uvfsvfs;
	vfs_t *vfsp = NULL;
	int error;

	error = ddi_copyin((void *)argp, &arg, sizeof (arg), flag);
	if (error != 0)
		goto out;

	error = fsid_to_uvfsvfs(arg.upar_fsid, &vfsp);
	if (error != 0)
		goto out;

	uvfsvfs = vfsp->vfs_data;
	mutex_enter(&uvfsvfs->uvfs_lock);
	arg.upar_maxwrite = uvfsvfs->uvfs_write_size;
	arg.upar_maxread = uvfsvfs->uvfs_read_size;
	arg.upar_max_dthreads = uvfsvfs->uvfs_max_dthreads;
	mutex_exit(&uvfsvfs->uvfs_lock);

	error = ddi_copyout(&arg, (void *)argp, sizeof (arg), flag);
	if (error != 0)
		goto out;

out:
	if (vfsp != NULL)
		VFS_RELE(vfsp);

	return (error);
}

static int
uvfsdev_ioctl_daemon_register(uintptr_t argp, int flag)
{
	uvfs_ioc_daemon_register_t arg;
	vfs_t *vfsp = NULL;
	uvfsvfs_t *uvfsvfs;
	int error;

	error = secpolicy_uvfs_server(CRED());
	if (error != 0)
		goto out;

	error = ddi_copyin((void *)argp, &arg, sizeof (arg), flag);
	if (error != 0)
		goto out;

	error = fsid_to_uvfsvfs(arg.uidr_fsid, &vfsp);
	if (error != 0)
		goto out;
	uvfsvfs = vfsp->vfs_data;

	mutex_enter(&uvfsvfs->uvfs_lock);

	uvfsvfs->uvfs_door = door_ki_lookup(arg.uidr_door);
	uvfsvfs->uvfs_flags |= UVFS_FLAG_MOUNT_COMPLETE;
	uvfsvfs->uvfs_flags &= ~(UVFS_FLAG_ROOTFID | UVFS_FLAG_ROOTVP);
	cv_broadcast(&uvfsvfs->uvfs_daemon_cv);

	mutex_exit(&uvfsvfs->uvfs_lock);

	/*
	 * The root fid may or may not have changed.  Schedule a task to
	 * call the daemon to get the fid, and take the appropriate action.
	 */

	uvfs_task_rootvp(uvfsvfs, CRED());

out:

	if (vfsp != NULL)
		VFS_RELE(vfsp);

	return (error);
}

uvfsdev_ioctl_handler_t uvfsdev_ioctl_handler[] = {
	uvfsdev_ioctl_daemon_wait,
	uvfsdev_ioctl_fsparam_get,
	uvfsdev_ioctl_daemon_register
};
static int uvfsdev_ioctl_handler_max = sizeof (uvfsdev_ioctl_handler) /
    sizeof (uvfsdev_ioctl_handler[0]);

/*ARGSUSED*/
static int
uvfsdev_ioctl(dev_t dev, int cmd, intptr_t arg, int flag,
    cred_t *cr, int *rvalp)
{
	int index = cmd - UVFS_IOC;

	if ((index < 0) || (index >= uvfsdev_ioctl_handler_max))
		return (ENODEV);

	return (uvfsdev_ioctl_handler[index](arg, flag));
}

/* XXX temporary: mdb-setable for whether the uvfs module is unloadable */
int uvfs_unloadable = 0;

static int
uvfs_busy(void)
{
	return (! uvfs_unloadable);
}

/*ARGSUSED*/
static int
uvfs_open(dev_t *devp, int flag, int otyp, cred_t *cr)
{
	minor_t minor = getminor(*devp);

	if (minor == 0)
		return (0);
	return (ENODEV);
}

/*ARGSUSED*/
static int
uvfs_close(dev_t dev, int flag, int otyp, cred_t *cr)
{
	minor_t minor = getminor(dev);

	if (minor == 0)
		return (0);
	return (ENODEV);
}

static int
uvfs_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	if (cmd != DDI_ATTACH)
		return (DDI_FAILURE);

	if (ddi_create_minor_node(dip, "uvfs", S_IFCHR, 0,
	    DDI_PSEUDO, 0) == DDI_FAILURE) {
		printf("failed to create minor node\n");
		return (DDI_FAILURE);
	}

	uvfs_dip = dip;

	ddi_report_dev(dip);

	return (DDI_SUCCESS);
}

static int
uvfs_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	if (uvfs_busy())
		return (DDI_FAILURE);

	if (cmd != DDI_DETACH)
		return (DDI_FAILURE);

	uvfs_dip = NULL;

	ddi_prop_remove_all(dip);
	ddi_remove_minor_node(dip, NULL);

	return (DDI_SUCCESS);
}

/*ARGSUSED*/
static int
uvfs_info(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg, void **result)
{
	switch (infocmd) {
	case DDI_INFO_DEVT2DEVINFO:
		*result = uvfs_dip;
		return (DDI_SUCCESS);

	case DDI_INFO_DEVT2INSTANCE:
		*result = (void *)0;
		return (DDI_SUCCESS);
	}

	return (DDI_FAILURE);
}

static struct cb_ops uvfs_cb_ops = {
	uvfs_open,	/* open */
	uvfs_close,	/* close */
	nodev,		/* strategy */
	nodev,		/* print */
	nodev,		/* dump */
	nodev,		/* read */
	nodev,		/* write */
	uvfsdev_ioctl,	/* ioctl */
	nodev,		/* devmap */
	nodev,		/* mmap */
	nodev,		/* segmap */
	nochpoll,	/* poll */
	ddi_prop_op,	/* prop_op */
	NULL,		/* streamtab */
	D_NEW | D_MP | D_64BIT,		/* Driver compatibility flag */
	CB_REV,		/* version */
	nodev,		/* async read */
	nodev,		/* async write */
};

static struct dev_ops uvfs_dev_ops = {
	DEVO_REV,	/* version */
	0,		/* refcnt */
	uvfs_info,	/* info */
	nulldev,	/* identify */
	nulldev,	/* probe */
	uvfs_attach,	/* attach */
	uvfs_detach,	/* detach */
	nodev,		/* reset */
	&uvfs_cb_ops,	/* driver operations */
	NULL,		/* no bus operations */
	NULL,		/* power */
	ddi_quiesce_not_needed,	/* quiesce */
};

static struct modldrv uvfs_modldrv = {
	&mod_driverops,
	"User virtual file system driver",
	&uvfs_dev_ops
};

static struct modlinkage modlinkage = {
	MODREV_1,
	(void *)&uvfs_modlfs,
	(void *)&uvfs_modldrv,
	NULL
};


int
_init(void)
{
	int error;

	uvfs_init();

	if ((error = mod_install(&modlinkage)) != 0) {
		uvfs_fini();
		return (error);
	}

	error = ldi_ident_from_mod(&modlinkage, &uvfs_li);
	ASSERT(error == 0);

	return (0);
}

int
_fini(void)
{
	int error;

	if (uvfs_busy())
		return (EBUSY);

	if ((error = mod_remove(&modlinkage)) != 0)
		return (error);

	uvfs_fini();

	ldi_ident_release(uvfs_li);
	uvfs_li = NULL;

	return (error);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}
