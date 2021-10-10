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
 * Copyright (c) 1998, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*	Copyright (c) 1990, 1991 UNIX System Laboratories, Inc.	*/
/*	Copyright (c) 1984, 1986, 1987, 1988, 1989, 1990 AT&T	*/
/*	  All Rights Reserved  	*/

#include <sys/errno.h>
#include <sys/types.h>
#include <sys/conf.h>
#include <sys/kmem.h>
#include <sys/visual_io.h>
#include <sys/font.h>
#include <sys/fbio.h>

#include <sys/ddi.h>
#include <sys/stat.h>
#include <sys/sunddi.h>
#include <sys/file.h>
#include <sys/open.h>
#include <sys/modctl.h>
#include <sys/vgareg.h>
#include <sys/vgasubr.h>
#include <sys/pci.h>
#include <sys/kd.h>
#include <sys/ddi_impldefs.h>
#include <sys/sunldi.h>
#include <sys/agpgart.h>
#include <sys/agp/agpdefs.h>
#include <sys/agp/agpmaster_io.h>

/* vgatext is now a consumer of gfx_private. */
#include "gfx_private.h"

#define	MYNAME	"vgatext"

/*
 * Each instance of this driver has 2 minor nodes:
 * 0: for common graphics operations
 * 1: for agpmaster operations
 */
#define	GFX_MINOR		0
#define	AGPMASTER_MINOR		1

#define	MY_NBITSMINOR		1
#define	DEV2INST(dev)		(getminor(dev) >> MY_NBITSMINOR)
#define	DEV2MINOR(dev)		(getminor(dev) & ((1 << MY_NBITSMINOR) - 1))
#define	INST2NODE1(inst)	((inst) << MY_NBITSMINOR + GFX_MINOR)
#define	INST2NODE2(inst)	(((inst) << MY_NBITSMINOR) + AGPMASTER_MINOR)

/*
 * This variable allows for this driver to suspend even if it
 * shouldn't.  Note that by setting it, the framebuffer will probably
 * not come back.  So use it with a serial console, or with serial
 * line debugging (say, for example, if this driver is being modified
 * to support _some_ hardware doing suspend and resume).
 */
int vgatext_force_suspend = 0;

static int vgatext_open(dev_t *, int, int, cred_t *);
static int vgatext_close(dev_t, int, int, cred_t *);
static int vgatext_ioctl(dev_t, int, intptr_t, int, cred_t *, int *);
static int vgatext_devmap(dev_t, devmap_cookie_t, offset_t, size_t,
			    size_t *, uint_t);

static 	struct cb_ops cb_vgatext_ops = {
	vgatext_open,		/* cb_open */
	vgatext_close,		/* cb_close */
	nodev,			/* cb_strategy */
	nodev,			/* cb_print */
	nodev,			/* cb_dump */
	nodev,			/* cb_read */
	nodev,			/* cb_write */
	vgatext_ioctl,		/* cb_ioctl */
	vgatext_devmap,		/* cb_devmap */
	nodev,			/* cb_mmap */
	ddi_devmap_segmap,	/* cb_segmap */
	nochpoll,		/* cb_chpoll */
	ddi_prop_op,		/* cb_prop_op */
	0,			/* cb_stream */
	D_NEW | D_MTSAFE	/* cb_flag */
};

static int vgatext_info(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg,
		void **result);
static int vgatext_attach(dev_info_t *, ddi_attach_cmd_t);
static int vgatext_detach(dev_info_t *, ddi_detach_cmd_t);

static struct vis_identifier text_ident = { "SUNWtext" };

static struct dev_ops vgatext_ops = {
	DEVO_REV,		/* devo_rev */
	0,			/* devo_refcnt */
	vgatext_info,		/* devo_getinfo */
	nulldev,		/* devo_identify */
	nulldev,		/* devo_probe */
	vgatext_attach,		/* devo_attach */
	vgatext_detach,		/* devo_detach */
	nodev,			/* devo_reset */
	&cb_vgatext_ops,	/* devo_cb_ops */
	(struct bus_ops *)NULL,	/* devo_bus_ops */
	NULL,			/* power */
	ddi_quiesce_not_needed,	/* quiesce */
};

struct vgatext_softc {
	gfxp_fb_softc_ptr_t	gfxp_softc;
	dev_info_t		*devi;
	agp_master_softc_t	*agp_master; /* NULL means not PCI, for AGP */
	ddi_acc_handle_t	*pci_cfg_hdlp;	/* PCI conf handle */
	kmutex_t lock;
};

static void	*vgatext_softc_head;

/* Loadable Driver stuff */

static struct modldrv modldrv = {
	&mod_driverops,		/* Type of module.  This one is a driver */
	"VGA text driver",	/* Name of the module. */
	&vgatext_ops,		/* driver ops */
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *) &modldrv, NULL
};

int
_init(void)
{
	int e;

	if ((e = ddi_soft_state_init(&vgatext_softc_head,
		    sizeof (struct vgatext_softc), 1)) != 0) {
	    return (e);
	}

	e = mod_install(&modlinkage);

	if (e) {
		ddi_soft_state_fini(&vgatext_softc_head);
	}
	return (e);
}

int
_fini(void)
{
	int e;

	if ((e = mod_remove(&modlinkage)) != 0)
		return (e);

	ddi_soft_state_fini(&vgatext_softc_head);

	return (0);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

/*
 * handy macros
 */
#define	getsoftc(instance) ((struct vgatext_softc *)	\
			ddi_get_soft_state(vgatext_softc_head, (instance)))
#define	STREQ(a, b)	(strcmp((a), (b)) == 0)

static int
vgatext_attach(dev_info_t *devi, ddi_attach_cmd_t cmd)
{
	struct vgatext_softc *softc;
	int	unit = ddi_get_instance(devi);
	int	error;
	int	agpm = 0;
	char	*parent_type = NULL;
	char	buf[80];

	switch (cmd) {
	case DDI_ATTACH:
		break;

	case DDI_RESUME:
		softc = getsoftc(unit);
		ASSERT(softc != NULL);
		return (gfxp_fb_attach(devi, cmd, softc->gfxp_softc));
	default:
		return (DDI_FAILURE);
	}

	/* DDI_ATTACH */

	/* Allocate softc struct */
	if (ddi_soft_state_zalloc(vgatext_softc_head, unit) != DDI_SUCCESS) {
		return (DDI_FAILURE);
	}
	softc = getsoftc(unit);

	/* Allocate a gfxp_softc struct for gfx_private routines. */
	softc->gfxp_softc = gfxp_fb_softc_alloc();
	if (!softc->gfxp_softc) {
		/* Free the driver softc */
		(void) ddi_soft_state_free(vgatext_softc_head, unit);
		return (DDI_FAILURE);
	}

	/* link it in */
	softc->devi = devi;
	ddi_set_driver_private(devi, softc);

	error = gfxp_fb_attach(devi, cmd, softc->gfxp_softc);
	if (error != DDI_SUCCESS) {
		gfxp_fb_softc_free(softc->gfxp_softc);
		(void) ddi_soft_state_free(vgatext_softc_head, unit);
		return (DDI_FAILURE);
	}

	/* Check if we should try AGP master attach */
	error = ddi_prop_lookup_string(DDI_DEV_T_ANY, ddi_get_parent(devi),
	    DDI_PROP_DONTPASS, "device_type", &parent_type);
	if (error != DDI_SUCCESS) {
		cmn_err(CE_WARN, MYNAME ": can't determine parent type.");
		goto fail;
	}

	if (STREQ(parent_type, "pci") || STREQ(parent_type, "pciex")) {
		agpm = 1;
	}
	ddi_prop_free(parent_type);

	/* Create a minor node for the text console */
	(void) sprintf(buf, "text-%d", unit);
	error = ddi_create_minor_node(devi, buf, S_IFCHR,
	    INST2NODE1(unit), DDI_NT_DISPLAY, NULL);
	if (error != DDI_SUCCESS)
		goto fail;

	if (agpm != 0) { /* try AGP master attach */
		/* setup mapping for PCI config space access */
		softc->pci_cfg_hdlp = (ddi_acc_handle_t *)
		    kmem_zalloc(sizeof (ddi_acc_handle_t), KM_SLEEP);
		error = pci_config_setup(devi, softc->pci_cfg_hdlp);
		if (error != DDI_SUCCESS) {
			cmn_err(CE_WARN, "vgatext_attach: "
			    "PCI configuration space setup failed");
			goto fail;
		}

		(void) agpmaster_attach(softc->devi, &softc->agp_master,
		    *softc->pci_cfg_hdlp, INST2NODE2(unit));
	}

	return (DDI_SUCCESS);

fail:
	(void) vgatext_detach(devi, DDI_DETACH);
	return (error);
}

static int
vgatext_detach(dev_info_t *devi, ddi_detach_cmd_t cmd)
{
	int instance = ddi_get_instance(devi);
	struct vgatext_softc *softc = getsoftc(instance);


	switch (cmd) {
	case DDI_DETACH:
		if (softc->agp_master != NULL) { /* agp initiated */
			agpmaster_detach(&softc->agp_master);
			pci_config_teardown(softc->pci_cfg_hdlp);
		}

		(void) gfxp_fb_detach(devi, cmd, softc->gfxp_softc);

		if (softc->gfxp_softc)
			gfxp_fb_softc_free(softc->gfxp_softc);

		mutex_destroy(&(softc->lock));
		ddi_remove_minor_node(devi, NULL);
		(void) ddi_soft_state_free(vgatext_softc_head, instance);
		return (DDI_SUCCESS);

	case DDI_SUSPEND:
		/*
		 * This is a generic VGA file, and therefore, cannot
		 * understand how to deal with suspend and resume on
		 * a generic interface.  So we fail any attempt to
		 * suspend.  At some point in the future, we might use
		 * this as an entrypoint for display drivers and this
		 * assumption may change.
		 *
		 * However, from a platform development perspective,
		 * it is important that this driver suspend if a
		 * developer is using a serial console and/or working
		 * on a framebuffer driver that will support suspend
		 * and resume.  Therefore, we have this module tunable
		 * (purposely using a long name) that will allow for
		 * suspend if it is set.  Otherwise we fail.
		 */
		if (vgatext_force_suspend != 0) {
			int	ret;
			ret = gfxp_fb_detach(devi, cmd, softc->gfxp_softc);
			return (ret);
		} else {
			return (DDI_FAILURE);
		}

	default:
		cmn_err(CE_WARN, "vgatext_detach: unknown cmd 0x%x\n", cmd);
		return (DDI_FAILURE);
	}
}

/*ARGSUSED*/
static int
vgatext_info(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg, void **result)
{
	dev_t dev;
	int error;
	int instance;
	struct vgatext_softc *softc;

	error = DDI_SUCCESS;

	dev = (dev_t)arg;
	instance = DEV2INST(dev);
	softc = getsoftc(instance);

	switch (infocmd) {
	case DDI_INFO_DEVT2DEVINFO:
		if (softc == NULL || softc->devi == NULL) {
			error = DDI_FAILURE;
		} else {
			*result = (void *) softc->devi;
			error = DDI_SUCCESS;
		}
		break;
	case DDI_INFO_DEVT2INSTANCE:
		*result = (void *)(uintptr_t)instance;
		error = DDI_SUCCESS;
		break;
	default:
		error = DDI_FAILURE;
		break;
	}
	return (error);
}


static int
vgatext_open(dev_t *devp, int flag, int otyp, cred_t *cred)
{
	struct vgatext_softc *softc = getsoftc(DEV2INST(*devp));

	return (gfxp_fb_open(devp, flag, otyp, cred, softc->gfxp_softc));
}

/*ARGSUSED*/
static int
vgatext_close(dev_t devp, int flag, int otyp, cred_t *cred)
{
	struct vgatext_softc *softc = getsoftc(DEV2INST(devp));

	return (gfxp_fb_close(devp, flag, otyp, cred, softc->gfxp_softc));
}

/*ARGSUSED*/
static int
vgatext_ioctl(
    dev_t dev,
    int cmd,
    intptr_t data,
    int mode,
    cred_t *cred,
    int *rval)
{
	struct vgatext_softc *softc = getsoftc(DEV2INST(dev));
	int err;

	switch (DEV2MINOR(dev)) {
	case GFX_MINOR:
		err = gfxp_fb_ioctl(dev, cmd, data, mode, cred, rval,
		    softc->gfxp_softc);
		break;

	case AGPMASTER_MINOR:
		err = agpmaster_ioctl(dev, cmd, data, mode, cred, rval,
		    softc->agp_master);
		break;

	default:
		/* not a valid minor node */
		return (EBADF);
	}
	return (err);
}


static int
vgatext_devmap(dev_t dev, devmap_cookie_t dhp, offset_t off, size_t len,
		size_t *maplen, uint_t model)
{
	struct vgatext_softc *softc = getsoftc(DEV2INST(dev));

	return (gfxp_fb_devmap(dev, dhp, off, len, maplen, model,
	    softc->gfxp_softc));

}
