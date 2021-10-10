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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * pseudo driver to sample working set via tracing
 */

#include <sys/types.h>
#include <sys/time.h>
#include <sys/param.h>
#include <sys/sysmacros.h>
#include <sys/file.h>
#include <sys/t_lock.h>
#include <sys/proc.h>
#include <sys/fcntl.h>
#include <sys/uio.h>
#include <sys/kmem.h>
#include <sys/cred.h>
#include <sys/mman.h>
#include <sys/errno.h>
#include <sys/ioccom.h>
#include <sys/cpuvar.h>
#include <sys/stat.h>
#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/modctl.h>
#include <sys/kobj.h>
#include <sys/tuneable.h>
#include <sys/inline.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/user.h>
#include <sys/var.h>
#include <sys/buf.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/swap.h>
#include <sys/vm.h>
#include <vm/as.h>
#include <vm/seg.h>
#include <vm/seg_vn.h>
#include <vm/seg_dev.h>
#include <vm/seg_map.h>
#include <vm/anon.h>
#include <sys/debug.h>
#include <sys/sysinfo.h>
#include <vm/hat.h>
#include <sys/vtrace.h>

extern int nodev();
static int wsdrv_open(dev_t *, int, int, struct cred *);
static int wsdrv_close(dev_t, int, int, struct cred *);
static int wsdrv_ioctl(dev_t, int, intptr_t, int, struct cred *, int *);
static dev_info_t *wsdrv_devi;
static void 	get_working_set();

static kmutex_t	*wsdrv_lock;

struct cb_ops	wsdrv_cb_ops = {
	wsdrv_open,		/* open */
	wsdrv_close,		/* close */
	nodev,			/* strategy */
	nodev,			/* print */
	nodev,			/* dump */
	nodev,			/* read */
	nodev,			/* write */
	wsdrv_ioctl,		/* ioctl */
	nodev,			/* devmap */
	nodev,			/* mmap */
	nodev,			/* segmap */
	nochpoll,		/* poll */
	ddi_prop_op,		/* prop_op */
	0,			/* streamtab  */
	D_NEW | D_MTSAFE	/* Driver compatibility flag */
};

static int wsdrv_attach(dev_info_t *, ddi_attach_cmd_t);
static int wsdrv_detach(dev_info_t *, ddi_detach_cmd_t);
static int wsdrv_info(dev_info_t *dip, ddi_info_cmd_t infocmd,
	void *arg, void **result);

struct dev_ops	wsdrv_ops = {
	DEVO_REV,		/* devo_rev, */
	0,			/* refcnt  */
	wsdrv_info,		/* get_dev_info */
	nulldev,		/* identify */
	nulldev,		/* probe */
	wsdrv_attach,		/* attach */
	wsdrv_detach,		/* detach */
	nodev,			/* reset */
	&wsdrv_cb_ops,		/* driver operations */
	(struct bus_ops *)0,	/* no bus operations */
	NULL,			/* power */
	ddi_quiesce_not_needed,		/* quiesce */
};

extern struct mod_ops mod_driverops;

static struct modldrv modldrv = {
	&mod_driverops,
	"user working set driver",
	&wsdrv_ops,
};

static struct modlinkage modlinkage = {
	MODREV_1,
	&modldrv,
	NULL
};

int
_init(void)
{
	int rc;

	if ((wsdrv_lock = (kmutex_t *)kmem_alloc(sizeof (kmutex_t), KM_SLEEP))
	    == NULL) {
		return (ENOMEM);
	}
	mutex_init(wsdrv_lock, NULL, MUTEX_DEFAULT, NULL);
	if ((rc = mod_install(&modlinkage)) != 0)  {
		mutex_destroy(wsdrv_lock);
		kmem_free(wsdrv_lock, sizeof (kmutex_t));
		return (rc);
	}
	return (0);
}

int
_fini(void)
{
	int	rc;

	if ((rc = mod_remove(&modlinkage)) == 0) {
		mutex_destroy(wsdrv_lock);
		kmem_free(wsdrv_lock, sizeof (kmutex_t));
		return (0);
	}
	return (rc);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

static int
wsdrv_attach(dev_info_t *devi, ddi_attach_cmd_t cmd)
{
	if (cmd != DDI_ATTACH)
		return (DDI_FAILURE);

	if (ddi_create_minor_node(devi, "wsdrv", S_IFCHR,
	    0, DDI_PSEUDO, NULL) == DDI_FAILURE) {
		ddi_remove_minor_node(devi, NULL);
		return (DDI_FAILURE);
	}
	wsdrv_devi = devi;
	return (DDI_SUCCESS);
}

static int
wsdrv_detach(dev_info_t *devi, ddi_detach_cmd_t cmd)
{
	if (cmd != DDI_DETACH)
		return (DDI_FAILURE);

	ddi_remove_minor_node(devi, NULL);
	return (DDI_SUCCESS);
}

/* ARGSUSED */
static int
wsdrv_info(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg,
	void **result)
{
	register int error;

	switch (infocmd) {
		case DDI_INFO_DEVT2DEVINFO:
		*result = (void *) wsdrv_devi;
		error = DDI_SUCCESS;
		break;
		case DDI_INFO_DEVT2INSTANCE:
		*result = (void *)0;
		error = DDI_SUCCESS;
		break;
		default:
		error = DDI_FAILURE;
	}
	return (error);
}

/* ARGSUSED */
static int
wsdrv_open(dev_t *devp, int flag, int otyp, struct cred *cred)
{
	return (0);
}

/* ARGSUSED */
static int
wsdrv_close(dev_t dev, int flag, int otyp, struct cred *cred)
{
	return (0);
}

/*ARGSUSED*/
static int
wsdrv_ioctl(dev_t dev, int Zcmd, intptr_t arg, int flag,
	cred_t *cred, int *rvalp)
{
	int cmd = Zcmd & 0xff;	/* dump high-order 'Z' in the ioctl code */

	/*
	 * "cmd" is the ioctl number as supplied to /usr/bin/getws, in the
	 * range 0-255.
	 */
	if (cmd == 1) {
		mutex_enter(wsdrv_lock);
		TRACE_0(TR_FAC_VM, TR_SAMPLE_WS_START, "sample ws start");
		get_working_set();
		TRACE_0(TR_FAC_VM, TR_SAMPLE_WS_END, "sample ws end");
		mutex_exit(wsdrv_lock);
		return (0);
	} else {
		return (EINVAL);
	}
}

static void
get_working_set()
{
	proc_t 		*p;
	struct as	*as;
	struct seg 	*s;
	ulong_t		size;
	caddr_t		base;
	struct vnode	*vp;
	extern struct   as	kas;
	extern kmutex_t	pidlock;

	/*
	 * Traverse the process table. for each process's segment
	 * trace the referenced and modified bits via hat_sync
	 * NOTE: this driver is expecting to be run as a real
	 * time process so it can sample everyones address space
	 * in 1 pass with no preemptions
	 */
	mutex_enter(&pidlock);
	for (p = practive; p; p = p->p_next) {
		as = p->p_as;
		if ((as == NULL) || (as == &kas)) {
			continue;
		}
		TRACE_2(TR_FAC_VM, TR_AS_INFO, "p %p as %p", p, as);
		AS_LOCK_ENTER(as, &as->a_lock, RW_READER);
		for (s = AS_SEGFIRST(as); s != NULL; s = AS_SEGNEXT(as, s)) {
			SEGOP_GETVP(s, s->s_base, &vp);
			as = s->s_as;
			base = s->s_base;
			size = s->s_size;
			TRACE_4(TR_FAC_VM, TR_SEG_INFO,
			    "seg info:seg %p base %p size %ld vp %p",
			    s, base, size, vp);
			hat_sync(as->a_hat, base, size, 1);
		}
		AS_LOCK_EXIT(as, &as->a_lock);
	}
	mutex_exit(&pidlock);
}
