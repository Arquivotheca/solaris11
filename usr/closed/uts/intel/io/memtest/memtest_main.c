/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <sys/stat.h>
#include <sys/errno.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/types.h>
#include <sys/modctl.h>
#include <sys/conf.h>
#include <sys/sunddi.h>
#include <sys/devops.h>
#include <sys/open.h>
#include <sys/policy.h>
#include <sys/varargs.h>
#include <sys/cpu_module.h>
#include <sys/memtest.h>

#include "memtest_impl.h"

memtest_t memtest;

void
memtest_dprintf(const char *fmt, ...)
{
	va_list ap;
	char buf[512] = "DEBUG: ";

	if (!(memtest.mt_flags & MEMTEST_F_DEBUG))
		return;

	va_start(ap, fmt);
	(void) vsnprintf(buf + 7, sizeof (buf) - 7, fmt, ap);
	va_end(ap);

	(void) printf(buf);
}

boolean_t
memtest_dryrun(void)
{
	return (memtest.mt_flags & MEMTEST_F_DRYRUN ? B_TRUE : B_FALSE);
}

static int
memtest_inquire(intptr_t arg, int mode)
{
	memtest_inq_t inq = { MEMTEST_VERSION };

	if (ddi_copyout(&inq, (void *)arg, sizeof (memtest_inq_t), mode) < 0)
		return (EFAULT);

	return (0);
}

static int
memtest_config(intptr_t arg)
{
	uint_t flags = 0;

	if (arg & MEMTEST_F_DEBUG) {
		flags |= MEMTEST_F_DEBUG;
		arg &= ~MEMTEST_F_DEBUG;
	}

	if (arg & MEMTEST_F_DRYRUN) {
		flags |= MEMTEST_F_DRYRUN;
		arg &= ~MEMTEST_F_DRYRUN;
	}

	if (arg != 0)
		return (EINVAL);

	memtest.mt_flags = flags;

	memtest_dprintf("flags changed to 0x%x\n", memtest.mt_flags);

	return (0);
}

/*ARGSUSED*/
static int
memtest_open(dev_t *devp, int flag, int otyp, cred_t *credp)
{
	if (otyp != OTYP_CHR)
		return (EINVAL);

	if (secpolicy_error_inject(credp) != 0)
		return (EPERM);

	mutex_enter(&memtest.mt_lock);
	if (memtest.mt_rsrvs != NULL) {
		mutex_exit(&memtest.mt_lock);
		return (EBUSY);
	}

	memtest.mt_rsrvs = kmem_zalloc(sizeof (memtest_rsrv_t) *
	    memtest.mt_rsrv_maxnum, KM_SLEEP);
	mutex_exit(&memtest.mt_lock);

	return (0);
}

/*ARGSUSED*/
static int
memtest_close(dev_t dev, int flag, int otyp, cred_t *credp)
{
	ASSERT(memtest.mt_rsrvs != NULL);

	memtest_release_all(); /* don't let clients cause leaks */

	mutex_enter(&memtest.mt_lock);

	kmem_free(memtest.mt_rsrvs, sizeof (memtest_rsrv_t) *
	    memtest.mt_rsrv_maxnum);
	memtest.mt_rsrvs = NULL;
	memtest.mt_flags = 0;

	mutex_exit(&memtest.mt_lock);

	return (0);
}

/*ARGSUSED*/
static int
memtest_ioctl(dev_t dev, int cmd, intptr_t arg, int mode, cred_t *credp,
    int *rvalp)
{
	switch (cmd) {
	case MEMTESTIOC_INQUIRE:
		return (memtest_inquire(arg, mode));
	case MEMTESTIOC_CONFIG:
		return (memtest_config(arg));
	case MEMTESTIOC_INJECT:
		return (memtest_inject(arg, mode));
	case MEMTESTIOC_MEMREQ:
		return (memtest_reserve(arg, mode, rvalp));
	case MEMTESTIOC_MEMREL:
		return (memtest_release(arg));
	default:
		return (EINVAL);
	}
}

static struct cb_ops memtest_cb_ops = {
	memtest_open,	/* open */
	memtest_close,	/* close */
	nodev,		/* strategy */
	nodev,		/* print */
	nodev,		/* dump */
	nodev,		/* read */
	nodev,		/* write */
	memtest_ioctl,	/* ioctl */
	nodev,		/* devmap */
	nodev,		/* mmap */
	nodev,		/* segmap */
	nochpoll,	/* chpoll */
	ddi_prop_op,	/* prop_op */
	NULL,		/* cb_str */
	D_NEW | D_MP,
	CB_REV,
	NULL,
	NULL
};

/*ARGSUSED*/
static int
memtest_getinfo(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg,
    void **resultp)
{
	switch (infocmd) {
	case DDI_INFO_DEVT2DEVINFO:
		*resultp = memtest.mt_dip;
		return (DDI_SUCCESS);

	case DDI_INFO_DEVT2INSTANCE:
		*resultp = (void *)(uintptr_t)getminor((dev_t)arg);
		return (DDI_SUCCESS);

	default:
		return (DDI_FAILURE);
	}
}

static int
memtest_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	if (cmd != DDI_ATTACH)
		return (DDI_FAILURE);

	if (ddi_create_minor_node(dip, ddi_get_name(dip), S_IFCHR,
	    ddi_get_instance(dip), DDI_PSEUDO, 0) != DDI_SUCCESS)
		return (DDI_FAILURE);

	memtest.mt_rsrv_maxsize = ddi_getprop(DDI_DEV_T_ANY, dip,
	    DDI_PROP_DONTPASS, "reservations-maxsize",
	    MEMTEST_RESERVATION_MAXSIZE);
	memtest.mt_rsrv_maxnum = ddi_getprop(DDI_DEV_T_ANY, dip,
	    DDI_PROP_DONTPASS, "reservations-maxnum",
	    MEMTEST_RESERVATION_MAXNUM);
	memtest.mt_inject_maxnum = ddi_getprop(DDI_DEV_T_ANY, dip,
	    DDI_PROP_DONTPASS, "inject-statements-maxnum",
	    MEMTEST_INJECT_MAXNUM);

	memtest.mt_dip = dip;
	return (DDI_SUCCESS);
}

static int
memtest_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	if (cmd != DDI_DETACH)
		return (DDI_FAILURE);

	ASSERT(memtest.mt_rsrvs == NULL);
	ddi_remove_minor_node(dip, NULL);
	return (DDI_SUCCESS);
}

static struct dev_ops memtest_ops = {
	DEVO_REV,
	0,
	memtest_getinfo,
	nulldev,
	nulldev,
	memtest_attach,
	memtest_detach,
	nodev,
	&memtest_cb_ops,
	NULL,
	NULL,
	ddi_quiesce_not_needed,		/* quiesce */
};

static struct modldrv modldrv = {
	&mod_driverops,
	"CPU/Memory Error Injection Driver",
	&memtest_ops
};

static struct modlinkage modlinkage = {
	MODREV_1,
	&modldrv,
	NULL
};

int
_init(void)
{
	mutex_init(&memtest.mt_lock, NULL, MUTEX_DRIVER, NULL);
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
	return (mod_remove(&modlinkage));
}
