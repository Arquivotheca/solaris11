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

#include "ast.h"

static void		*ast_state;
static struct cb_ops	 ast_cb_ops;
static struct   vis_identifier ast_ident = {"SUNWast"};

static int	ast_attach(dev_info_t *, ddi_attach_cmd_t);
static int	ast_detach(dev_info_t *, ddi_detach_cmd_t);
static int	ast_info(dev_info_t *, ddi_info_cmd_t, void *, void **);
static int	ast_open(dev_t *, int, int, cred_t *);
static int	ast_close(dev_t, int, int, cred_t *);
static uint_t	ast_intr(caddr_t);
static int	ast_enable_PCI(struct ast_softc *);
static int	ast_enable_ROM(struct ast_softc *);
static int	ast_disable_ROM(struct ast_softc *);
static int	ast_ioctl(dev_t, int, intptr_t, int, cred_t *, int *);
static int	ast_get_gfx_identifier(struct ast_softc *,
				dev_t, intptr_t, int);
static int	ast_set_video_mode(struct ast_softc *, dev_t, intptr_t, int);
static int	ast_get_video_mode(struct ast_softc *, dev_t, intptr_t, int);
static int	ast_get_edid_length(struct ast_softc *, dev_t, intptr_t, int);
static int	ast_get_edid(struct ast_softc *, dev_t, intptr_t, int);
static int	ast_get_status_flags(struct ast_softc *, dev_t, intptr_t, int);
static int	ast_init_hw(struct ast_softc *);

static unsigned int ast_read32(struct ast_softc *, unsigned int);
static unsigned int ast_read32_8bpp(struct ast_softc *, unsigned int);
static void ast_write32(struct ast_softc *, unsigned int, unsigned int);
static void ast_write32_8bpp(struct ast_softc *, unsigned int, unsigned int);

#if !defined(DUMMY_KERNEL)

static struct dev_ops ast_dev_ops = {
	DEVO_REV,		/* devo_rev		*/
	0,			/* devo_refcnt		*/
	ast_info,		/* getinfo(9e)		*/
	nulldev,		/* identify(9e)		*/
	nulldev,		/* probe(9e)		*/
	ast_attach,		/* attach(9e)		*/
	ast_detach,		/* detach(9e)		*/
	nodev,			/* devo_reset		*/
	&ast_cb_ops,		/* devo_cb_ops		*/
	NULL,			/* devo_bus_ops		*/
	NULL			/* power(9e)		*/
};

static struct cb_ops ast_cb_ops = {
	ast_open,		/* open			*/
	ast_close,		/* close		*/
	nodev,			/* strategy		*/
	nodev,			/* print		*/
	nodev,			/* dump			*/
	nodev,			/* read			*/
	nodev,			/* write		*/
	ast_ioctl,		/* ioctl		*/
	ast_devmap,		/* devmap		*/
	nodev,			/* mmap			*/
	nodev,			/* segmap		*/
	nochpoll,		/* chpoll		*/
	ddi_prop_op,		/* prop_op		*/
	NULL,			/* streamtab		*/
	D_MP|D_64BIT|D_DEVMAP,	/* cb_flag		*/
	CB_REV,			/* cb_rev		*/
	nodev,			/* aread		*/
	nodev,			/* awrite		*/
};

#else

static struct dev_ops ast_dev_ops = {
	DEVO_REV,		/* devo_rev		*/
	0,			/* devo_refcnt		*/
	nulldev,		/* getinfo(9e)		*/
	nulldev,		/* identify(9e)		*/
	nulldev,		/* probe(9e)		*/
	ast_attach,		/* attach(9e)		*/
	ast_detach,		/* detach(9e)		*/
	nodev,			/* devo_reset		*/
	&ast_cb_ops,		/* devo_cb_ops		*/
	NULL,			/* devo_bus_ops		*/
	NULL			/* power(9e)		*/
};

static struct cb_ops ast_cb_ops = {
	nodev,			/* open			*/
	nodev,			/* close		*/
	nodev,			/* strategy		*/
	nodev,			/* print		*/
	nodev,			/* dump			*/
	nodev,			/* read			*/
	nodev,			/* write		*/
	nodev,			/* ioctl		*/
	nodev,			/* devmap		*/
	nodev,			/* mmap			*/
	nodev,			/* segmap		*/
	nochpoll,		/* chpoll		*/
	ddi_prop_op,		/* prop_op		*/
	NULL,			/* streamtab		*/
	D_MP|D_64BIT|D_DEVMAP,	/* cb_flag		*/
	CB_REV,			/* cb_rev		*/
	nodev,			/* aread		*/
	nodev,			/* awrite		*/
};
#endif

static struct modldrv modldrv = {
	&mod_driverops,
	"ast driver v0.000001",
	&ast_dev_ops,
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *) &modldrv, NULL
};

static ddi_device_acc_attr_t acc_attr_registers = {
	DDI_DEVICE_ATTR_V0,
	DDI_NEVERSWAP_ACC,
	DDI_STRICTORDER_ACC
};

static  struct ddi_device_acc_attr fbMem = {
	DDI_DEVICE_ATTR_V0,
	DDI_STRUCTURE_BE_ACC,
	DDI_STRICTORDER_ACC
};


/*
 * Little-endian mapping with strict ordering for registers
 */
static  struct ddi_device_acc_attr littleEnd = {
	DDI_DEVICE_ATTR_V0,
	DDI_STRUCTURE_BE_ACC,
	DDI_STRICTORDER_ACC
};

int
_init(void)
{
	int rcode = 0;

	if (rcode = ddi_soft_state_init(&ast_state,
					sizeof (struct ast_softc), 1)) {
		cmn_err(CE_NOTE, "ddi_soft_state_init failed");
		return (rcode);
	}

	if ((rcode = mod_install(&modlinkage)) != 0) {
		cmn_err(CE_NOTE, "mod_install failed");
		ddi_soft_state_fini(&ast_state);
	}

	return (rcode);
}


int
_info(struct modinfo *modinfop)
{
	int rcode = 0;

	if ((rcode = mod_info(&modlinkage, modinfop)) == 0) {
		cmn_err(CE_NOTE, "mod_info failed");
	}

	return (rcode);
}


int
_fini(void)
{
	int rcode;

	rcode = mod_remove(&modlinkage);

	if (rcode) {
		return (rcode);
	}

	ddi_soft_state_fini(&ast_state);

	return (rcode);
}


static int
ast_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	struct ast_softc *softc;
	int		instance;
	unsigned int	bar[4];
	long		regsize, fbsize, iosize, romsize;


#if !defined(DUMMY_KERNEL)

	instance = ddi_get_instance(dip);

	switch (cmd) {

	case DDI_ATTACH:

		if (ddi_soft_state_zalloc(ast_state, instance) != DDI_SUCCESS) {
			return (DDI_FAILURE);
		}

		softc = ddi_get_soft_state(ast_state, instance);
		ddi_set_driver_private(dip, (caddr_t)softc);

		softc->devi = dip;

		/*
		 * Setup mapping to the pci and register spaces
		 */
		if (ddi_create_minor_node(dip, "ast0", S_IFCHR,
		    instance, DDI_NT_DISPLAY, 0) == DDI_FAILURE) {
			cmn_err(CE_NOTE,
			    "ast_attach: failed to create minor node");
		}

		if (pci_config_setup(dip, &softc->pci_handle) != DDI_SUCCESS) {
			cmn_err(CE_NOTE, "pci_config_setup failed");
		}

		/*
		 * Get the BAR info
		 */
		bar[0] = pci_config_get32(softc->pci_handle, 0x10);
		bar[1] = pci_config_get32(softc->pci_handle, 0x14);
		bar[2] = pci_config_get32(softc->pci_handle, 0x18);
		bar[3] = pci_config_get32(softc->pci_handle, 0x30);

		if (ddi_dev_regsize(dip, REGNUM_CONTROL_FB, &fbsize)
		    != DDI_SUCCESS)
			return (DDI_FAILURE);
		if (ddi_dev_regsize(dip, REGNUM_CONTROL_REG, &regsize)
		    != DDI_SUCCESS)
			return (DDI_FAILURE);
		if (ddi_dev_regsize(dip, REGNUM_CONTROL_IO, &iosize)
		    != DDI_SUCCESS)
			return (DDI_FAILURE);
		if (ddi_dev_regsize(dip, REGNUM_CONTROL_ROM, &romsize)
		    != DDI_SUCCESS)
			return (DDI_FAILURE);

		softc->regspace[0].global_offset = bar[0];
		softc->regspace[0].size = fbsize;
		softc->regspace[0].rnum = REGNUM_CONTROL_FB;
		softc->regspace[0].register_offset = 0;
		softc->regspace[0].attrs = &fbMem;

		softc->regspace[1].global_offset = bar[1];
		softc->regspace[1].size = regsize;
		softc->regspace[1].rnum = REGNUM_CONTROL_REG;
		softc->regspace[1].register_offset = 0;
		softc->regspace[1].attrs = &littleEnd;

		softc->regspace[2].global_offset = bar[2];
		softc->regspace[2].size = iosize;
		softc->regspace[2].rnum = REGNUM_CONTROL_IO;
		softc->regspace[2].register_offset = 0;
		softc->regspace[2].attrs = &littleEnd;

		softc->regspace[3].global_offset = bar[3];
		softc->regspace[3].size = romsize;
		softc->regspace[3].rnum = REGNUM_CONTROL_ROM;
		softc->regspace[3].register_offset = 0;
		softc->regspace[3].attrs = &littleEnd;


		/* Enable PCI */
		if (ast_enable_PCI(softc)) {
			return (DDI_FAILURE);
		}

		if (ddi_regs_map_setup(dip, REGNUM_CONTROL_REG,
		    (caddr_t *)&softc->regbase, 0, regsize, &acc_attr_registers,
		    &softc->regs_handle)) {
			return (DDI_FAILURE);
		}

		if (ddi_regs_map_setup(dip, REGNUM_CONTROL_FB,
		    (caddr_t *)&softc->fbbase, 0, fbsize, &fbMem,
		    &softc->fb_handle)) {
			return (DDI_FAILURE);
		}

		if (ddi_regs_map_setup(dip, REGNUM_CONTROL_IO,
		    (caddr_t *)&softc->iobase, 0, iosize, &acc_attr_registers,
		    &softc->io_handle)) {
			cmn_err(CE_NOTE, "ast%d: regs_map_setup fails",
			    instance);
			return (DDI_FAILURE);
		}

		if (ddi_get_iblock_cookie(dip, 0, &softc->iblock_cookie)
		    != DDI_SUCCESS) {
			cmn_err(CE_NOTE,
			    "ast%d: unable to get interrupt cookie",
			    instance);
		}

		if (ddi_add_intr(dip, 0, &softc->iblock_cookie, NULL, ast_intr,
		    (caddr_t)softc) != DDI_SUCCESS) {
			cmn_err(CE_NOTE, "damn, ddi_get_iblock_cookie failed");
		}

		mutex_init(&softc->lock, NULL, MUTEX_DRIVER,
		    softc->iblock_cookie);
		mutex_init(&softc->ctx_lock, NULL, MUTEX_DRIVER,
		    softc->iblock_cookie);

#if VIS_CONS_REV > 2

		/*
		 * setup for coherent console
		 */
		if (ddi_prop_update_int(DDI_DEV_T_NONE, dip, "tem-support", 1)
		    != DDI_PROP_SUCCESS) {
			cmn_err(CE_NOTE,
			    "ast%d: unable to set tem-support property",
			    instance);
			return (DDI_FAILURE);
		}

		softc->consinfo.bufp	  = NULL;
		softc->consinfo.kcmap_max = 0;

#endif /* VIS_CONS_REV */

		softc->shared_ctx 	= NULL;
		softc->cur_ctx 		= NULL;
		softc->contexts 	= NULL;
		softc->flags		= 0;

		/*
		 * at system boot up time, fcode only supports 8 bits
		 */
		softc->displayDepth 	= 8;
		softc->read32		= NULL;
		softc->write32		= NULL;
	}

	ddi_report_dev(dip);

#endif /* DUMMY_KERNEL */

	return (DDI_SUCCESS);
}

struct ast_softc *
ast_get_softc(int instance)
{
	return ((struct ast_softc *)ddi_get_soft_state(ast_state, instance));
}

static int
ast_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	struct ast_softc *softc;
	int	instance;

#if !defined(DUMMY_KERNEL)

	instance = ddi_get_instance(dip);
	softc    = ast_get_softc(instance);

	switch (cmd) {

	case DDI_DETACH:

		mutex_enter(&softc->lock);
		ddi_remove_minor_node(dip, "ast0");

		if (softc->regs_handle) {
			ddi_regs_map_free(&softc->regs_handle);
		}

		if (softc->fb_handle) {
			ddi_regs_map_free(&softc->fb_handle);
			softc->fb_handle = NULL;
		}

		if (softc->io_handle) {
			ddi_regs_map_free(&softc->io_handle);
			softc->io_handle = NULL;
		}

		ddi_remove_intr(dip, 0, softc->iblock_cookie);
		softc->flags &= ~AST_HW_INITIALIZED;
		mutex_exit(&softc->lock);

		mutex_destroy(&softc->lock);
		mutex_destroy(&softc->ctx_lock);
		pci_config_teardown(&softc->pci_handle);
		ddi_soft_state_free(ast_state, instance);
		break;

	default:
		return (DDI_FAILURE);
	}
#endif /* DUMMY_KERNEL */

	return (DDI_SUCCESS);
}


static int
ast_info(dev_info_t *dip, ddi_info_cmd_t cmd, void *arg, void **resultp)
{
	minor_t	minor    = getminor((dev_t)arg);
	struct ast_softc *softc;
	int	rcode;

	switch (cmd) {

	case DDI_INFO_DEVT2DEVINFO:
		softc    = ast_get_softc(minor);
		if (softc == NULL) {
			*resultp = NULL;
			rcode = DDI_FAILURE;
		} else {
			*resultp = (void *) dip;
			rcode = DDI_SUCCESS;
		}
		break;

	case DDI_INFO_DEVT2INSTANCE:
		*resultp = (void *)(uintptr_t)minor;
		rcode = DDI_SUCCESS;
		break;

	default:
		*resultp = NULL;
		rcode = DDI_FAILURE;
		break;
	}

	return (rcode);
}

static int
ast_open(dev_t *devp, int flag, int otyp, cred_t *credp)
{
	struct ast_softc *softc;
	int		  instance;

	instance = getminor(*devp);

	softc = ast_get_softc(instance);
	if (softc == NULL)
		return (EFAULT);

	return (0);
}


static int
ast_close(dev_t dev, int flag, int otyp, cred_t *credp)
{
	return (0);
}


static uint_t
ast_intr(caddr_t arg)
{
	return (DDI_INTR_CLAIMED);
}

static int
ast_enable_PCI(struct ast_softc *softc)
{
	unsigned tmp;
	ddi_acc_handle_t pci_handle = softc->pci_handle;

	tmp = pci_config_get32(pci_handle, 0x4);
	tmp = tmp | 0x3;
	pci_config_put32(pci_handle, 0x4, tmp);

	return (0);
}

static int
ast_enable_ROM(struct ast_softc *softc)
{
	unsigned tmp;
	ddi_acc_handle_t pci_handle = softc->pci_handle;

	tmp = pci_config_get32(pci_handle, 0x30);
	tmp = tmp | 0x1;
	pci_config_put32(pci_handle, 0x30, tmp);

	return (0);
}

static int
ast_disable_ROM(struct ast_softc *softc)
{
	unsigned tmp;
	ddi_acc_handle_t pci_handle = softc->pci_handle;

	tmp = pci_config_get32(pci_handle, 0x30);
	tmp = tmp & ~0x1;
	pci_config_put32(pci_handle, 0x30, tmp);

	return (0);
}


static int
ast_get_pci_config(struct ast_softc *softc, dev_t dev, intptr_t arg, int mode)
{
	struct gfx_pci_cfg ast_pci_cfg;
	unsigned tmp;
	ddi_acc_handle_t pci_handle = softc->pci_handle;

	tmp = pci_config_get32(pci_handle, 0x00);
	ast_pci_cfg.VendorID = tmp & 0xffff;
	ast_pci_cfg.DeviceID = tmp >> 16;

	tmp = pci_config_get32(pci_handle, 0x08);
	ast_pci_cfg.RevisionID = tmp & 0xff;
	ast_pci_cfg.SubClass = tmp >> 8;

	tmp = pci_config_get32(pci_handle, 0x2c);
	ast_pci_cfg.SubSystemID = tmp & 0xffff;
	ast_pci_cfg.SubVendorID = tmp >> 16;

	ast_pci_cfg.ROMBaseAddress = pci_config_get32(pci_handle, 0x30);

	tmp = pci_config_get32(pci_handle, 0x10);
	ast_pci_cfg.bar[0] = tmp;

	tmp = pci_config_get32(pci_handle, 0x14);
	ast_pci_cfg.bar[1] = tmp;

	tmp = pci_config_get32(pci_handle, 0x18);
	ast_pci_cfg.bar[2] = tmp;

	if (ddi_copyout((caddr_t)&ast_pci_cfg, (caddr_t)arg,
	    sizeof (ast_pci_cfg), mode)) {
		cmn_err(CE_NOTE, "pci_config_setup ddi_copyout failed");
		return (EFAULT);
	}
	return (0);
}


/*ARGSUSED*/
static  int
ast_ioctl(dev_t dev, int cmd, intptr_t arg, int mode, cred_t *cred, int *rval)
{
	struct ast_softc *softc = ast_get_softc(getminor(dev));
	int ret = 0;
	ast_io_reg io_reg;

	if (softc == NULL)
		return (EFAULT);

	switch (cmd) {
	case VIS_GETIDENTIFIER:
		if (ddi_copyout(&ast_ident, (void *)arg,
		    sizeof (ast_ident), mode))
			ret = EFAULT;
		break;

	case VIS_SETIOREG:
	case AST_SET_IO_REG:
		if (ddi_copyin((void*)arg, &io_reg, sizeof (io_reg), mode)) {
			cmn_err(CE_NOTE,
			    "calling AST_SET_IO_REG ddi_copyin failed");
			ret = EFAULT;
		}
		*(softc->iobase + io_reg.offset) = io_reg.value;
		break;

	case VIS_GETIOREG:
	case AST_GET_IO_REG:
		if (ddi_copyin((void*)arg, &io_reg, sizeof (io_reg), mode)) {
			cmn_err(CE_NOTE,
			    "calling AST_GET_IO_REG ddi_copyin failed");
			ret = EFAULT;
		}
		io_reg.value = *(softc->iobase + io_reg.offset);

		if (ddi_copyout(&io_reg, (void*)arg, sizeof (io_reg), mode)) {
			cmn_err(CE_NOTE,
			    "calling AST_GET_IO_REG ddi_copyout failed");
			ret = EFAULT;
		}
		break;

	case AST_ENABLE_ROM:
		ret = ast_enable_ROM(softc);
		break;

	case AST_DISABLE_ROM:
		ret = ast_disable_ROM(softc);
		break;

	case AST_GET_STATUS_FLAGS:
		ret = ast_get_status_flags(softc, dev, arg, mode);
		break;

	case VIS_GETPCICONFIG:
	case GFX_IOCTL_GET_PCI_CONFIG:
		ret = ast_get_pci_config(softc, dev, arg, mode);
		break;

	case VIS_GETGFXIDENTIFIER:
	case GFX_IOCTL_GET_IDENTIFIER:
		ret = ast_get_gfx_identifier(softc, dev, arg, mode);
		break;

	case VIS_STOREVIDEOMODENAME:
	case GFX_IOCTL_SET_VIDEO_MODE:
		ret = ast_set_video_mode(softc, dev, arg, mode);
		break;

	case VIS_GETVIDEOMODENAME:
	case GFX_IOCTL_GET_CURRENT_VIDEO_MODE:
		ret = ast_get_video_mode(softc, dev, arg, mode);
		break;

	case VIS_GETEDIDLENGTH:
	case GFX_IOCTL_GET_EDID_LENGTH:
		ret = ast_get_edid_length(softc, dev, arg, mode);
		break;

	case VIS_GETEDID:
	case GFX_IOCTL_GET_EDID:
		ret = ast_get_edid(softc, dev, arg, mode);
		break;

#if VIS_CONS_REV > 2
	case VIS_DEVINIT:
		if (ast_init_hw(softc) == 0) {
			ret = ast_vis_devinit(softc, dev, arg, mode);
		}
		break;

	case VIS_DEVFINI:
		ret = ast_vis_devfini(softc, dev, arg, mode);
		break;

	case VIS_CONSDISPLAY:
		ret = ast_vis_consdisplay(softc, dev, arg, mode);
		break;

	case VIS_CONSCURSOR:
		ret = ast_vis_conscursor(softc, dev, arg, mode);
		break;

	case VIS_CONSCOPY:
		ret = ast_vis_conscopy(softc, dev, arg, mode);
		break;

	case VIS_PUTCMAP:
		ret = ast_vis_putcmap(softc, dev, arg, mode);
		break;
#endif /* VIS_CONS_REV */

#ifdef AST_DEBUG
	case AST_DEBUG_VIS_TEST:
		ret = ast_vis_debug_test(softc, dev, arg, mode);
		break;

	case AST_DEBUG_GET_VIS_BUF:
		ret = ast_vis_debug_get_buf(softc, dev, arg, mode);
		break;

	case AST_DEBUG_GET_VIS_IMAGE:
		ret = ast_vis_debug_get_image(softc, dev, arg, mode);
		break;

	case AST_DEBUG_TEST:
		ret = ast_debug_test(softc);
		break;

#endif /* AST_DEBUG */

	default:
		return (EINVAL);

	}

	return (ret);
}


static int
ast_get_gfx_identifier(struct ast_softc *softc, dev_t dev, intptr_t arg,
    int mode)
{
	struct gfx_identifier ast_gfx_id;
	char *buf;
	int proplen;

	ast_gfx_id.flags = 0;
	ast_gfx_id.version = GFX_IDENT_VERSION;

	if (ddi_getlongprop(DDI_DEV_T_ANY, softc->devi, DDI_PROP_DONTPASS,
	    "name", (caddr_t)&buf, &proplen) == DDI_SUCCESS) {
		(void) strcpy((caddr_t)&ast_gfx_id.model_name, buf);
		ast_gfx_id.flags |= GFX_IDENT_MODELNAME;
		kmem_free(buf, proplen);
	}

	if (ddi_getlongprop(DDI_DEV_T_ANY, softc->devi, DDI_PROP_DONTPASS,
	    "model", (caddr_t)&buf, &proplen) == DDI_SUCCESS) {
		(void) strcpy((caddr_t)&ast_gfx_id.part_number, buf);
		ast_gfx_id.flags |= GFX_IDENT_PARTNUM;
		kmem_free(buf, proplen);
	}

	if (ddi_copyout((caddr_t)&ast_gfx_id, (caddr_t)arg,
	    sizeof (ast_gfx_id), mode)) {
		return (EFAULT);
	}

	return (0);
}


static int
ast_set_video_mode(struct ast_softc *softc, dev_t dev, intptr_t arg, int mode)
{
	if (ddi_copyin((caddr_t)arg, (caddr_t)&softc->videomode,
			sizeof (struct gfx_video_mode), mode)) {
		return (EFAULT);
	}

	softc->displayDepth = 32;
	softc->read32  = ast_read32;
	softc->write32 = ast_write32;

	/*
	 * video is initialized....hardware is initialized
	 */
	softc->flags |= AST_HW_INITIALIZED;

#if VIS_CONS_REV > 2
	ast_vis_termemu_callback(softc);
#endif /* VIS_CONS_REV */

	return (0);
}

static int
ast_get_video_mode(struct ast_softc *softc, dev_t dev, intptr_t arg, int mode)
{
	if (ddi_copyout(&softc->videomode, (caddr_t)arg,
			sizeof (struct gfx_video_mode), mode)) {
		return (EFAULT);
	}
	return (0);
}

static int
ast_get_edid_length(struct ast_softc *softc, dev_t dev, intptr_t arg, int mode)
{
	int ret = 0;
	unsigned int length = GFX_EDID_BLOCK_SIZE;
	caddr_t results;
	gfx_edid_t edid_buf;

	results = kmem_alloc(length, KM_SLEEP);

	mutex_enter(&softc->lock);
	ret = ast_read_edid(softc, results, &length);
	mutex_exit(&softc->lock);

	if (!ret) {
		/* the 127th byte specifies the extension block count */
		edid_buf.length = GFX_EDID_BLOCK_SIZE * (1 + results[126]);
		if (ddi_copyout(&edid_buf, (caddr_t)arg, sizeof (gfx_edid_t),
		    mode)) {
			ret = EFAULT;
		}
	}
	kmem_free(results, GFX_EDID_BLOCK_SIZE);

	return (ret);
}

static int
ast_get_edid(struct ast_softc *softc, dev_t dev, intptr_t arg, int mode)
{
	int ret;
	int length;
	caddr_t results;
	gfx_edid_t edid_buf;

	if (ddi_copyin((void *)arg, &edid_buf, sizeof (gfx_edid_t), mode)) {
		return (EFAULT);
	}

	length = edid_buf.length;
	if (length <= 0) {
		return (EINVAL);
	}

	results = kmem_alloc(length, KM_SLEEP);

#ifdef _MULTI_DATAMODEL
	if (ddi_model_convert_from(mode & FMODELS) == DDI_MODEL_ILP32) {
		edid_buf.data = (caddr_t)
		    ((unsigned long)edid_buf.data & 0xffffffff);
	}
#endif /* _MULTI_DATAMODEL */

	mutex_enter(&softc->lock);
	ret = ast_read_edid(softc, results, &edid_buf.length);
	mutex_exit(&softc->lock);

	if (!ret) {
		ret = ddi_copyout(results, (caddr_t)edid_buf.data,
		    edid_buf.length, mode);
	}

	if (!ret) {
		ret = ddi_copyout(&edid_buf, (caddr_t)arg,
		    sizeof (gfx_edid_t), mode);
	}

	if (ret) {
		ret = EFAULT;
	}

	kmem_free(results, length);
	return (ret);
}

static int
ast_get_status_flags(struct ast_softc *softc, dev_t dev, intptr_t arg, int mode)
{
	int status = 0;
	int ret;

	if (softc->flags & AST_HW_INITIALIZED)
		status |= AST_STATUS_HW_INITIALIZED;

	ret = ddi_copyout(&status, (void *)arg, sizeof (int), mode);

	return (ret);
}

static int
ast_init_hw(struct ast_softc *softc)
{
	unsigned int val;

	/*
	 * Open Key
	 */
	SetIndexReg(CRTC_PORT, 0x80, 0xA8);

	/*
	 * Enable MMIO
	 */
	SetIndexRegMask(CRTC_PORT, 0xA1, 0xFF, 0x04);

	/*
	 * Enable Big-Endian
	 */
	if (softc->displayDepth == 8) {
		softc->read32		= ast_read32_8bpp;
		softc->write32		= ast_write32_8bpp;
		SetIndexRegMask(CRTC_PORT, 0xA2, 0x3F, 0x00);
	} else {
		softc->read32		= ast_read32;
		softc->write32		= ast_write32;
		SetIndexRegMask(CRTC_PORT, 0xA2, 0x3F, 0x80);
	}

	/*
	 * Enable 2D
	 */
	softc->write32(softc, 0xF004, 0x1e6e0000);
	softc->write32(softc, 0xF000, 0x1);
	val = softc->read32(softc, 0x1200c);
	softc->write32(softc, 0x1200c, val &0xFFFFFFFD);

	SetIndexRegMask(CRTC_PORT, 0xA4, 0xFE, 0x01);

	softc->flags |= AST_HW_INITIALIZED;

	return (0);
}

void
ast_cmap_write(struct ast_softc *softc)
{
	int i;
	unsigned char red, green, blue;
	/* LINTED set but not used in function: tmp (E_FUNC_SET_NOT_USED) */
	unsigned char tmp;

#if VIS_CONS_REV > 2
	switch (softc->displayDepth) {
	case 8:
		for (i = 0; i < softc->consinfo.kcmap_max; i++) {

			if (softc->consinfo.kcmap[3][i]) {
				red   = softc->consinfo.kcmap[0][i];
				green = softc->consinfo.kcmap[1][i];
				blue  = softc->consinfo.kcmap[2][i];

				/*
				 * Load the palette index
				 */
				SetReg(DAC_INDEX_WRITE, (unsigned char)i);
				GetReg(SEQ_PORT, tmp);
				SetReg(DAC_DATA, red);
				GetReg(SEQ_PORT, tmp);
				SetReg(DAC_DATA, green);
				GetReg(SEQ_PORT, tmp);
				SetReg(DAC_DATA, blue);
				GetReg(SEQ_PORT, tmp);
			}
		}
		break;

	default:
		break;
	}
#endif /* VIS_CONS_REV */
}

static unsigned int
ast_read32(struct ast_softc *softc, unsigned int offset)
{
	/* LINTED E_BAD_PTR_CAST_ALIGN */
	return (*((unsigned int *)(softc->regbase + offset)));
}

static void
ast_write32(struct ast_softc *softc, unsigned int offset, unsigned int val)
{
	unsigned int tmp;
	/* LINTED E_BAD_PTR_CAST_ALIGN */
	unsigned int *addr = (unsigned int *)(softc->regbase + offset);

	do {
		*addr = val;
		tmp = *addr;
	} while (tmp != val);
}

static unsigned int
ast_read32_8bpp(struct ast_softc *softc, unsigned int offset)
{
	union {
		unsigned int   ul;
		unsigned char  b[4];
	} data;

	unsigned int m;

	/* LINTED E_BAD_PTR_CAST_ALIGN */
	data.ul = *((unsigned int *)(softc->regbase + offset));

	m = (((unsigned int)data.b[3]) << 24) |
	    (((unsigned int)data.b[2]) << 16) |
	    (((unsigned int)data.b[1]) << 8) |
	    (((unsigned int)data.b[0]));
	return (m);
}

static void
ast_write32_8bpp(struct ast_softc *softc, unsigned int offset, unsigned int val)
{
	union {
		unsigned int   ul;
		unsigned char  b[4];
	} data;

	unsigned int m;

	data.ul = val;
	m = (((unsigned int)data.b[3]) << 24) |
	    (((unsigned int)data.b[2]) << 16) |
	    (((unsigned int)data.b[1]) << 8) |
	    (((unsigned int)data.b[0]));

	ast_write32(softc, offset, m);
}

#ifdef AST_DEBUG
int
ast_debug_test(struct ast_softc *softc)
{
	return (0);
}
#endif /* AST_DEBUG */
