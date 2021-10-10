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
 * Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * This is a custom kernel driver that allows us to do various things in kernel
 * context while running the test suite.  It create a single pseudo device
 * ("shadowtest") that can be opened and ioctl()ed.
 */

#include <sys/conf.h>
#include <sys/class.h>
#include <sys/cred.h>
#include <sys/file.h>
#include <sys/modctl.h>
#include <sys/stat.h>
#include <sys/sunddi.h>
#include <sys/sysmacros.h>

#include <shadowtest.h>

static dev_info_t *st_dip;

extern boolean_t vfs_shadow_rotate_disable;
extern cred_t *vfs_shadow_cred;

extern int vfs_shadow_dbg_rotate(vfs_t *);
extern int vfs_shadow_dbg_spin(vfs_t *, boolean_t);

#define	ST_BLOCKSIZE	1024

static void
st_migrate_one(vnode_t *vp)
{
	vattr_t attr;
	char *buf;
	uint64_t offset;

	if (VOP_GETATTR(vp, &attr, AT_SIZE, kcred, NULL) != 0)
		return;

	buf = kmem_alloc(ST_BLOCKSIZE, KM_SLEEP);

	for (offset = 0; offset < attr.va_size; offset += ST_BLOCKSIZE)
		(void) vn_rdwr(UIO_READ, vp, (caddr_t)buf, ST_BLOCKSIZE,
		    offset, UIO_SYSSPACE, 0, RLIM64_INFINITY, kcred, NULL);

	kmem_free(buf, ST_BLOCKSIZE);
}

/*ARGSUSED*/
static int
st_ioctl(dev_t dev, int cmd, intptr_t arg, int flag, cred_t *cr, int *rvalp)
{
	st_cmd_t sc;
	int error;
	struct file *fp;
	fid_t fid;
	kthread_t *thread;

	if ((error = xcopyin((void *)arg, &sc, sizeof (st_cmd_t))) != 0)
		return (error);

	switch (cmd) {
	case ST_IOC_GETFID:
		if ((fp = getf(sc.stc_fd)) == NULL)
			return (EBADF);

		fid.fid_len = MAXFIDSZ;
		if ((error = VOP_FID(fp->f_vnode, &fid, NULL)) != 0) {
			releasef(sc.stc_fd);
			return (error);
		}

		sc.stc_fid.fid_len = fid.fid_len;
		bcopy(fid.fid_data, sc.stc_fid.fid_data, fid.fid_len);

		releasef(sc.stc_fd);
		break;

	case ST_IOC_SUSPEND:
		vfs_shadow_rotate_disable = B_TRUE;
		break;

	case ST_IOC_RESUME:
		vfs_shadow_rotate_disable = B_FALSE;
		break;

	case ST_IOC_SPIN:
	case ST_IOC_ROTATE:
		if ((fp = getf(sc.stc_fd)) == NULL)
			return (EBADF);

		switch (cmd) {
		case ST_IOC_SPIN:
			error = vfs_shadow_dbg_spin(fp->f_vnode->v_vfsp,
			    sc.stc_op);
			break;

		case ST_IOC_ROTATE:
			error = vfs_shadow_dbg_rotate(fp->f_vnode->v_vfsp);
			break;
		}

		if (error != 0) {
			releasef(sc.stc_fd);
			return (error);
		}

		releasef(sc.stc_fd);
		break;

	case ST_IOC_CRED_SET:
		if (vfs_shadow_cred != kcred)
			crfree(vfs_shadow_cred);

		vfs_shadow_cred = crdup(cr);
		break;

	case ST_IOC_CRED_CLEAR:
		if (vfs_shadow_cred != kcred)
			crfree(vfs_shadow_cred);
		vfs_shadow_cred = kcred;
		break;

	case ST_IOC_KTHREAD:
		if ((fp = getf(sc.stc_fd)) == NULL)
			return (EBADF);

		thread = thread_create(NULL, 0, st_migrate_one,
		    fp->f_vnode, 0, &p0, TS_RUN, minclsyspri);
		if (thread == NULL) {
			releasef(sc.stc_fd);
			return (EINVAL);
		}
		thread_join(thread->t_did);
		releasef(sc.stc_fd);
		break;

	default:
		return (ENOTSUP);
	}

	return (xcopyout(&sc, (void *)arg, sizeof (st_cmd_t)));
}

/*ARGSUSED*/
static int
st_open(dev_t *devp, int flag, int otype, cred_t *c)
{
	minor_t minor = getminor(*devp);

	if (minor == 0)
		return (0);
	else
		return (ENXIO);
}

/*ARGSUSED*/
static int
st_close(dev_t dev, int flag, int otyp, cred_t *cr)
{
	minor_t minor = getminor(dev);

	if (minor == 0)
		return (0);
	else
		return (ENXIO);
}

static int
st_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	if (cmd != DDI_ATTACH)
		return (DDI_FAILURE);

	if (ddi_create_minor_node(dip, "shadowtest", S_IFCHR, 0,
	    DDI_PSEUDO, 0) == DDI_FAILURE)
		return (DDI_FAILURE);

	ddi_report_dev(dip);
	st_dip = dip;

	return (DDI_SUCCESS);
}

static int
st_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	if (cmd != DDI_DETACH)
		return (DDI_FAILURE);

	st_dip = NULL;
	ddi_prop_remove_all(dip);
	ddi_remove_minor_node(dip, NULL);

	return (DDI_SUCCESS);
}

/*ARGSUSED*/
static int
st_info(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg, void **result)
{
	switch (infocmd) {
	case DDI_INFO_DEVT2DEVINFO:
		*result = st_dip;
		return (DDI_SUCCESS);

	case DDI_INFO_DEVT2INSTANCE:
		*result = (void *)0;
		return (DDI_SUCCESS);
	}

	return (DDI_FAILURE);
}

static struct cb_ops st_cb_ops = {
	st_open,	/* open */
	st_close,	/* close */
	nodev,		/* strategy */
	nodev,		/* print */
	nodev,		/* dump */
	nodev,		/* read */
	nodev,		/* write */
	st_ioctl,	/* ioctl */
	nodev,		/* devmap */
	nodev,		/* mmap */
	nodev,		/* segmap */
	nochpoll,	/* poll */
	ddi_prop_op,	/* prop_op */
	NULL,		/* streamtab */
	D_NEW | D_MP | D_64BIT,	/* Driver compatibility flag */
	CB_REV,		/* version */
	nodev,		/* async read */
	nodev,		/* async write */
};

static struct dev_ops st_dev_ops = {
	DEVO_REV,	/* version */
	0,		/* refcnt */
	st_info,	/* info */
	nulldev,	/* identify */
	nulldev,	/* probe */
	st_attach,	/* attach */
	st_detach,	/* detach */
	nodev,		/* reset */
	&st_cb_ops,	/* driver operations */
	NULL,		/* no bus operations */
	NULL,		/* power */
	ddi_quiesce_not_needed, /* quiesce */
};

static struct modldrv st_modl = {
	&mod_driverops,
	"shadowfs test utilities",
	&st_dev_ops
};

static struct modlinkage modlinkage = {
	MODREV_1,
	{ (void *)&st_modl }
};

int
_init(void)
{
	return (mod_install(&modlinkage));
}

int
_fini(void)
{
	return (mod_remove(&modlinkage));
}

int
_info(struct modinfo *mip)
{
	return (mod_info(&modlinkage, mip));
}
