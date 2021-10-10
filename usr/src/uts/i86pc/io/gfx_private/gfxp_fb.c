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

/*	Copyright (c) 1990, 1991 UNIX System Laboratories, Inc.	*/
/*	Copyright (c) 1984, 1986, 1987, 1988, 1989, 1990 AT&T	*/
/*	  All Rights Reserved  	*/

/*
 * Generic entry point for x86 Console Frame Buffer support.
 *
 * The Terminal Emulator (tem, see common/io/tem.c) sends down ioctls based on
 * the Console Visual I/O Interface. The screen can be managed in two modes,
 * VGA 'text' mode (standard 80x25 character-based frame buffer) or 'pixel' mode
 * (hi-resolution bit-mapped frame buffer).
 * We provide here support for both 'text' mode, by calling into gfxp_vga.c
 * routines, and 'pixel' mode, by calling into gfxp_bitmap.c routines.
 *
 * X86 Graphic Device Drivers and the generic vgatext driver
 * (intel/io/vgatext/vgatext.c) are all consumers of our interfaces.
 */

#include <sys/errno.h>
#include <sys/types.h>
#include <sys/conf.h>
#include <sys/kmem.h>
#include <sys/visual_io.h>
#include <sys/fbio.h>
#include <sys/ddi.h>
#include <sys/stat.h>
#include <sys/sunddi.h>
#include <sys/file.h>
#include <sys/open.h>
#include <sys/modctl.h>
#include <sys/kd.h>
#include <sys/pci.h>
#include <sys/ddi_impldefs.h>
/* For console types. */
#include <sys/bootinfo.h>
#include <sys/boot_console.h>

#include "gfx_private.h"
#include "gfxp_fb.h"

static int	gfxp_fb_suspend(struct gfxp_softc *softc);
static void	gfxp_fb_resume(struct gfxp_softc *softc);

extern void gfxp_bm_attach(struct gfxp_softc *softc);
extern void gfxp_bm_detach(struct gfxp_softc *softc);
extern int  gfxp_vga_attach(dev_info_t *devi, struct gfxp_softc *softc);
extern void gfxp_vga_detach(struct gfxp_softc *softc);

extern int gfxp_vga_map_reg(dev_info_t *devi, uint8_t type, int size,
	struct vgaregmap *reg, int *regnum);
extern void gfxp_vga_get_mapped_ioregs(struct gfxp_softc *softc,
	struct vgaregmap *regss);

static void gfxp_check_for_console(dev_info_t *, struct gfxp_softc *, int);

gfxp_fb_softc_ptr_t
gfxp_fb_softc_alloc(void)
{
	return (kmem_zalloc(sizeof (struct gfxp_softc), KM_SLEEP));
}

void
gfxp_fb_softc_free(gfxp_fb_softc_ptr_t ptr)
{
	kmem_free(ptr, sizeof (struct gfxp_softc));
}

int
gfxp_fb_attach(dev_info_t *devi, ddi_attach_cmd_t cmd,
	gfxp_fb_softc_ptr_t ptr)
{
	struct gfxp_softc *softc = (struct gfxp_softc *)ptr;
	int	error;
	int 	pci_pcie_bus = 0;
	int	value = 0;
	char	*parent_type = NULL;

	ASSERT(softc);
	if (softc == NULL)
		return (DDI_FAILURE);

	switch (cmd) {
	case DDI_ATTACH:
		break;

	case DDI_RESUME:
		gfxp_fb_resume(softc);
		return (DDI_SUCCESS);

	default:
		return (DDI_FAILURE);
	}

	/* DDI_ATTACH */
	softc->devi = devi; /* Copy and init DEVI */
	softc->polledio.arg = (struct vis_polledio_arg *)softc;
	mutex_init(&(softc->lock), NULL, MUTEX_DRIVER, NULL);
	softc->fbgattr = NULL;

	error = ddi_prop_lookup_string(DDI_DEV_T_ANY, ddi_get_parent(devi),
	    DDI_PROP_DONTPASS, "device_type", &parent_type);
	if (error != DDI_PROP_SUCCESS) {
		cmn_err(CE_WARN, MYNAME ": can't determine parent type.");
		error = DDI_FAILURE;
		goto fail;
	}

	/*
	 * In case of multiple video card installed on a system, Xorg needs a
	 * way to identify the primary controller. We export a boolean
	 * 'primary-controller' property for that reason.
	 */
	if (STREQ(parent_type, "pci") || STREQ(parent_type, "pciex"))
		pci_pcie_bus = 1;

	ddi_prop_free(parent_type);
	parent_type = NULL;

	gfxp_check_for_console(devi, softc, pci_pcie_bus);
	value = GFXP_IS_CONSOLE(softc) ? 1 : 0;
	if (ddi_prop_update_int(DDI_DEV_T_NONE, devi,
	    "primary-controller", value) != DDI_PROP_SUCCESS) {
		cmn_err(CE_WARN,
		    "Can not %s primary-controller "
		    "property for driver", value ? "set" : "clear");
	}

	/*
	 * Be coherent with the boot console: the user console preference
	 * has been evaluated at boot time and stored inside the 'console'
	 * variable. (see sys/boot_console.h)
	 */
	switch (console) {
	case CONS_SCREEN_GRAPHICS:
	case CONS_SCREEN_FB:
				softc->fb_type = GFXP_IS_BITMAPPED;
				gfxp_bm_attach(softc);
				break;
	case CONS_SCREEN_VGATEXT:
	default:
				softc->fb_type = GFXP_IS_VGATEXT;
				error = gfxp_vga_attach(devi, softc);
				if (error != DDI_SUCCESS)
					goto fail_prop;
				break;
	}

	return (DDI_SUCCESS);

fail_prop:
	(void) ddi_prop_remove(DDI_DEV_T_NONE, devi, "primary-controller");
fail:
	if (parent_type != NULL)
		ddi_prop_free(parent_type);
	(void) gfxp_fb_detach(devi, DDI_DETACH, (void *)softc);
	return (error);
}

/*ARGSUSED*/
int
gfxp_fb_detach(dev_info_t *devi, ddi_detach_cmd_t cmd,
	gfxp_fb_softc_ptr_t ptr)
{
	struct gfxp_softc *softc = (struct gfxp_softc *)ptr;

	ASSERT(softc);
	if (softc == NULL)
		return (DDI_FAILURE);

	switch (cmd) {

	case DDI_SUSPEND:
		return (gfxp_fb_suspend(softc));
		/* break; */
	case DDI_DETACH:
		switch (console) {
		case CONS_SCREEN_GRAPHICS:
		case CONS_SCREEN_FB:
				gfxp_bm_detach(softc);
				break;
		case CONS_SCREEN_VGATEXT:
		default:
				gfxp_vga_detach(softc);
				break;
		}
		mutex_destroy(&(softc->lock));
		return (DDI_SUCCESS);

	default:
		cmn_err(CE_WARN, "gfxp_fb_detach: unknown cmd 0x%x\n",
		    cmd);
		return (DDI_FAILURE);
	}
}

/*ARGSUSED*/
int
gfxp_fb_open(dev_t *devp, int flag, int otyp, cred_t *cred,
	gfxp_fb_softc_ptr_t ptr)
{
	struct gfxp_softc *softc = (struct gfxp_softc *)ptr;

	if (softc == NULL || otyp == OTYP_BLK)
		return (ENXIO);

	return (0);
}

/*ARGSUSED*/
int
gfxp_fb_close(dev_t devp, int flag, int otyp, cred_t *cred,
	gfxp_fb_softc_ptr_t ptr)
{
	return (0);
}

static int
do_gfx_ioctl(int cmd, intptr_t data, int mode, struct gfxp_softc *softc)
{
	int err;
	int kd_mode;

	ASSERT(softc);
	if (softc == NULL)
		return (ENXIO);

	switch (cmd) {
	case KDSETMODE:
		if ((int)data == softc->mode)
			return (0);

		ASSERT(softc->ops);

		if (softc->ops && softc->ops->kdsetmode)
			return (softc->ops->kdsetmode(softc, (int)data));
		else
			return (ENOTSUP);
	case KDGETMODE:
		kd_mode = softc->mode;
		if (ddi_copyout(&kd_mode, (void *)data, sizeof (int), mode))
			return (EFAULT);
		break;

	case VIS_DEVINIT:
		if (!(mode & FKIOCTL)) {
			return (EPERM);
		}

		ASSERT(softc->ops && softc->ops->devinit);

		if (softc->ops && softc->ops->devinit)
			err = softc->ops->devinit(softc,
			    (struct vis_devinit *)data);
		else
			err = ENOTSUP;

		if (err != 0) {
			cmn_err(CE_WARN,
			    "gfxp_fb_ioctl:  could not"
			    " initialize console");
			return (err);
		}
		break;

	case VIS_CONSCOPY:	/* move */
	{
		struct vis_conscopy pma;

		if (!(mode & FKIOCTL)) {
			return (EPERM);
		}

		ASSERT(softc->ops && softc->ops->conscopy);

		if (ddi_copyin((void *)data, &pma,
		    sizeof (struct vis_conscopy), mode))
			return (EFAULT);

		if (softc->ops && softc->ops->conscopy)
			softc->ops->conscopy(softc, &pma);
		else
			return (ENOTSUP);

		break;
	}

	case VIS_CONSDISPLAY:	/* display */
	{
		struct vis_consdisplay display_req;

		if (!(mode & FKIOCTL)) {
			return (EPERM);
		}

		ASSERT(softc->ops && softc->ops->consdisplay);

		if (ddi_copyin((void *)data, &display_req,
		    sizeof (display_req), mode))
			return (EFAULT);

		if (softc->ops && softc->ops->consdisplay)
			softc->ops->consdisplay(softc, &display_req);
		else
			return (ENOTSUP);

		break;
	}

	case VIS_CONSCURSOR:
	{
		struct vis_conscursor cursor_request;

		if (!(mode & FKIOCTL)) {
			return (EPERM);
		}

		ASSERT(softc->ops && softc->ops->conscursor);

		if (ddi_copyin((void *)data, &cursor_request,
		    sizeof (cursor_request), mode))
			return (EFAULT);

		if (softc->ops && softc->ops->conscursor)
			softc->ops->conscursor(softc, &cursor_request);
		else
			return (ENOTSUP);

		if (cursor_request.action == VIS_GET_CURSOR &&
		    ddi_copyout(&cursor_request, (void *)data,
		    sizeof (cursor_request), mode))
			return (EFAULT);
		break;
	}

	case VIS_CONSCLEAR:	/* clear the screen */
	{
		struct vis_consclear clear_req;

		if (!(mode & FKIOCTL)) {
			return (EPERM);
		}

		ASSERT(softc->ops && softc->ops->consclear);

		if (ddi_copyin((void *)data, &clear_req,
		    sizeof (clear_req), mode))
			return (EFAULT);

		if (softc->ops && softc->ops->consclear)
			return (softc->ops->consclear(softc, &clear_req));
		else
			return (ENOTSUP);
	}

	case VIS_GETCMAP:
	case VIS_PUTCMAP:
	case FBIOPUTCMAP:
	case FBIOGETCMAP:
		/*
		 * At the moment, text mode is not considered to have
		 * a color map.
		 */
		return (EINVAL);

	case FBIOGATTR:
		if (softc->fbgattr == NULL)
			return (ENXIO);
		if (copyout(softc->fbgattr, (void *)data,
		    sizeof (struct fbgattr)))
			return (EFAULT);
		break;

	case FBIOGTYPE:
		if (softc->fbgattr == NULL)
			return (ENXIO);
		if (copyout(&(softc->fbgattr->fbtype), (void *)data,
		    sizeof (struct fbtype)))
			return (EFAULT);
		break;
	case FBIOLOADHNDL:
	{
		fbio_load_handle_t	handle;

		if (ddi_copyin((void *)data, &handle,
		    sizeof (fbio_load_handle_t), mode))
			return (EFAULT);

		/* XXX should we check the return value here??? */
		(void) vbios_register_handle(&handle);

		break;
	}

	default:
		return (ENXIO);
	}
	return (0);
}

/*ARGSUSED*/
int
gfxp_fb_ioctl(
    dev_t dev,
    int cmd,
    intptr_t data,
    int mode,
    cred_t *cred,
    int *rval,
    gfxp_fb_softc_ptr_t ptr)
{
	int err;

	struct gfxp_softc *softc = (struct gfxp_softc *)ptr;

	ASSERT(softc);
	if (softc == NULL)
		return (ENXIO);

	mutex_enter(&(softc->lock));
	err = do_gfx_ioctl(cmd, data, mode, softc);
	mutex_exit(&(softc->lock));

	return (err);
}

int
gfxp_fb_devmap(dev_t dev, devmap_cookie_t dhp, offset_t off, size_t len,
		size_t *maplen, uint_t model, void *ptr)
{
	struct gfxp_softc *softc = (struct gfxp_softc *)ptr;
	int	ret;

	ASSERT(softc);
	if (softc == NULL)
		return (ENXIO);

	ASSERT(softc->ops && softc->ops->devmap);
	if (softc->ops && softc->ops->devmap)
		ret = softc->ops->devmap(softc, dev, dhp, off, len, maplen,
		    model);
	else
		ret = -1;

	return (ret);
}

int
gfxp_fb_get_fbtype(gfxp_fb_softc_ptr_t ptr)
{
	struct gfxp_softc *softc = (struct gfxp_softc *)ptr;

	ASSERT(softc);
	if (softc == NULL)
		return (DDI_FAILURE);

	return (softc->fb_type);
}

int
gfxp_fb_map_vga_ioreg(gfxp_fb_softc_ptr_t ptr, struct vgaregmap *reg)
{
	int	error;
	struct gfxp_softc *softc = (struct gfxp_softc *)ptr;

	ASSERT(softc);
	if (softc == NULL || reg == NULL)
		return (DDI_FAILURE);

	/* VGA TEXT mode already mapped the registers at attach time. */
	if (softc->fb_type == GFXP_IS_VGATEXT) {
		gfxp_vga_get_mapped_ioregs(softc, reg);
		error = DDI_SUCCESS;
	} else {
		error = gfxp_vga_map_reg(softc->devi, VGA_REG_IO, VGA_REG_SIZE,
		    reg, NULL);
	}
	return (error);
}

void
gfxp_fb_unmap_vga_ioreg(gfxp_fb_softc_ptr_t ptr, struct vgaregmap *reg)
{
	struct gfxp_softc *softc = (struct gfxp_softc *)ptr;

	if (softc == NULL || reg == NULL)
		return;

	/* gfxp_vga code is responsible of freeing the mapped registers. */
	if (softc->fb_type == GFXP_IS_VGATEXT)
		return;

	if (reg->mapped)
		ddi_regs_map_free(&reg->handle);
}

static int
gfxp_fb_suspend(struct gfxp_softc *softc)
{
	if (softc->mode != KD_TEXT && softc->mode != KD_GRAPHICS) {
		cmn_err(CE_WARN, MYNAME ": unknown mode in"
		    " gfxp_fb_suspend.");
		return (DDI_FAILURE);
	}

	ASSERT(softc->ops && softc->ops->suspend);
	if (softc->ops && softc->ops->suspend)
		return (softc->ops->suspend(softc));
	else
		return (DDI_FAILURE);
}

static void
gfxp_fb_resume(struct gfxp_softc *softc)
{
	if (softc->mode != KD_TEXT && softc->mode != KD_GRAPHICS) {
		cmn_err(CE_WARN, MYNAME ": unknown mode in"
		    " gfxp_fb_resume.");
		return;
	}

	ASSERT(softc->ops && softc->ops->resume);
	if (softc->ops && softc->ops->resume)
		softc->ops->resume(softc);
}

/*
 * NOTE: this function is duplicated here and in consplat/vgatext while
 *       we work on a set of commitable interfaces to sunpci.c.
 *
 * Use the class code to determine if the device is a PCI-to-PCI bridge.
 * Returns:  B_TRUE  if the device is a bridge.
 *           B_FALSE if the device is not a bridge or the property cannot be
 *		     retrieved.
 */
static boolean_t
is_pci_bridge(dev_info_t *dip)
{
	uint32_t class_code;

	class_code = (uint32_t)ddi_prop_get_int(DDI_DEV_T_ANY, dip,
	    DDI_PROP_DONTPASS, "class-code", 0xffffffff);

	if (class_code == 0xffffffff || class_code == DDI_PROP_NOT_FOUND)
		return (B_FALSE);

	class_code &= 0x00ffff00;
	if (class_code == ((PCI_CLASS_BRIDGE << 16) | (PCI_BRIDGE_PCI << 8)))
		return (B_TRUE);

	return (B_FALSE);
}

#define	STREQ(a, b)	(strcmp((a), (b)) == 0)

static void
gfxp_check_for_console(dev_info_t *devi, struct gfxp_softc *softc,
	int pci_pcie_bus)
{
	ddi_acc_handle_t pci_conf;
	dev_info_t *pdevi;
	uint16_t data16;

	/*
	 * Based on Section 11.3, "PCI Display Subsystem Initialization",
	 * of the 1.1 PCI-to-PCI Bridge Architecture Specification
	 * determine if this is the boot console device.  First, see
	 * if the SBIOS has turned on PCI I/O for this device.  Then if
	 * this is PCI/PCI-E, verify the parent bridge has VGAEnable set.
	 */

	if (pci_config_setup(devi, &pci_conf) != DDI_SUCCESS) {
		cmn_err(CE_WARN,
		    MYNAME
		    ": can't get PCI conf handle");
		return;
	}

	data16 = pci_config_get16(pci_conf, PCI_CONF_COMM);
	if (data16 & PCI_COMM_IO)
		softc->flags |= GFXP_FLAG_CONSOLE;

	pci_config_teardown(&pci_conf);

	/* If IO not enabled or ISA/EISA, just return */
	if (!(softc->flags & GFXP_FLAG_CONSOLE) || !pci_pcie_bus)
		return;

	/*
	 * Check for VGA Enable in the Bridge Control register for all
	 * PCI/PCIEX parents.  If not set all the way up the chain,
	 * this cannot be the boot console.
	 */

	pdevi = devi;
	while (pdevi = ddi_get_parent(pdevi)) {
		int	error;
		ddi_acc_handle_t ppci_conf;
		char	*parent_type = NULL;

		error = ddi_prop_lookup_string(DDI_DEV_T_ANY, pdevi,
		    DDI_PROP_DONTPASS, "device_type", &parent_type);
		if (error != DDI_SUCCESS) {
			return;
		}

		/* Verify still on the PCI/PCIEX parent tree */
		if (!STREQ(parent_type, "pci") &&
		    !STREQ(parent_type, "pciex")) {
			ddi_prop_free(parent_type);
			return;
		}

		ddi_prop_free(parent_type);
		parent_type = NULL;

		/* VGAEnable is set only for PCI-to-PCI bridges. */
		if (is_pci_bridge(pdevi) == B_FALSE)
			continue;

		if (pci_config_setup(pdevi, &ppci_conf) != DDI_SUCCESS)
			continue;

		data16 = pci_config_get16(ppci_conf, PCI_BCNF_BCNTRL);
		pci_config_teardown(&ppci_conf);

		if (!(data16 & PCI_BCNF_BCNTRL_VGA_ENABLE)) {
			softc->flags &= ~GFXP_FLAG_CONSOLE;
			return;
		}
	}
}


/*
 * gfxp_vgatext prefixed functions for retro-compatibility with old gfx
 * drivers. (ex. old NVDIA drivers).
 */
gfxp_vgatext_softc_ptr_t
gfxp_vgatext_softc_alloc(void)
{
	return (gfxp_fb_softc_alloc());
}

void
gfxp_vgatext_softc_free(gfxp_vgatext_softc_ptr_t ptr)
{
	gfxp_fb_softc_free(ptr);
}

int
gfxp_vgatext_attach(dev_info_t *devi, ddi_attach_cmd_t cmd,
	gfxp_vgatext_softc_ptr_t ptr)
{
	return (gfxp_fb_attach(devi, cmd, ptr));
}

int
gfxp_vgatext_detach(dev_info_t *devi, ddi_detach_cmd_t cmd,
	gfxp_vgatext_softc_ptr_t ptr)
{
	return (gfxp_fb_detach(devi, cmd, ptr));
}

int
gfxp_vgatext_open(dev_t *devp, int flag, int otyp, cred_t *cred,
	gfxp_vgatext_softc_ptr_t ptr)
{
	return (gfxp_fb_open(devp, flag, otyp, cred, ptr));
}

int
gfxp_vgatext_close(dev_t devp, int flag, int otyp, cred_t *cred,
	gfxp_vgatext_softc_ptr_t ptr)
{
	return (gfxp_fb_close(devp, flag, otyp, cred, ptr));
}

int
gfxp_vgatext_ioctl(
    dev_t dev,
    int cmd,
    intptr_t data,
    int mode,
    cred_t *cred,
    int *rval,
    gfxp_vgatext_softc_ptr_t ptr)
{
	return (gfxp_fb_ioctl(dev, cmd, data, mode, cred, rval, ptr));
}

int
gfxp_vgatext_devmap(dev_t dev, devmap_cookie_t dhp, offset_t off, size_t len,
		size_t *maplen, uint_t model, void *ptr)
{
	return (gfxp_fb_devmap(dev, dhp, off, len, maplen, model, ptr));
}
