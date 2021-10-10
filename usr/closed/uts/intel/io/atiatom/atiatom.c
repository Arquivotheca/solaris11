/*
 * Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Copyright 2001, ATI Technologies Inc. All rights reserved.
 * The material in this document constitutes an unpublished work
 * created in 2001. The use of this copyright notice is intended to
 * provide notice that ATI owns a copyright in this unpublished work.
 * The copyright notice is not an admission that publication has occurred.
 * This work contains confidential, proprietary information and trade
 * secrets of ATI. No part of this document may be used, reproduced,
 * or transmitted in any form or by any means without the prior
 * written permission of ATI Technologies Inc
 */


#include <sys/errno.h>
#include <sys/types.h>
#include <sys/conf.h>
#include <sys/kmem.h>
#include <sys/ksynch.h>
#include <sys/visual_io.h>
#include <sys/font.h>
#include <sys/fbio.h>

#include <sys/ddi.h>
#include <sys/stat.h>
#include <sys/sunddi.h>
#include <sys/file.h>
#include <sys/open.h>
#include <sys/modctl.h>
#include <sys/pci.h>
#include <sys/kd.h>
#include <sys/ddi_impldefs.h>

#include <sys/epm.h>
#include <sys/cmn_err.h>
#include "atiatom.h"
#include <sys/vgasubr.h>

#include "gfx_private.h"

#define	MYNAME	"atiatom"

static int	atiatom_open(dev_t *, int, int, cred_t *);
static int	atiatom_close(dev_t, int, int, cred_t *);
static int	atiatom_ioctl(dev_t, int, intptr_t, int, cred_t *, int *);
static int	atiatom_devmap(dev_t, devmap_cookie_t, offset_t, size_t,
		    size_t *, uint_t);

extern caddr_t  i86devmap(pfn_t, pgcnt_t, uint_t);

static struct cb_ops cb_atiatom_ops = {
	atiatom_open,		/* cb_open */
	atiatom_close,		/* cb_close */
	nodev,			/* cb_strategy */
	nodev,			/* cb_print */
	nodev,			/* cb_dump */
	nodev,			/* cb_read */
	nodev,			/* cb_write */
	atiatom_ioctl,		/* cb_ioctl */
	atiatom_devmap,		/* cb_devmap */
	nodev,			/* cb_mmap */
	ddi_devmap_segmap,	/* cb_segmap */
	nochpoll,		/* cb_chpoll */
	ddi_prop_op,		/* cb_prop_op */
	0,			/* cb_stream */
	D_NEW | D_MTSAFE	/* cb_flag */
};


static int
atiatom_info(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg,
    void **result);
static int	atiatom_attach(dev_info_t *, ddi_attach_cmd_t);
static int	atiatom_detach(dev_info_t *, ddi_detach_cmd_t);

/*
 * Several of these functions in turn refer to the common
 * code which is now factored out into gfxp_vgatext
 */
static struct dev_ops atiatom_ops = {
	DEVO_REV,		/* devo_rev */
	0,			/* devo_refcnt */
	atiatom_info,		/* devo_getinfo */
	nulldev,		/* devo_identify */
	nulldev,		/* devo_probe */
	atiatom_attach,		/* devo_attach */
	atiatom_detach,		/* devo_detach */
	nodev,			/* devo_reset */
	&cb_atiatom_ops,	/* devo_cb_ops */
	(struct bus_ops *)NULL, /* devo_bus_ops */
	NULL,			/* power */
	ddi_quiesce_not_needed,	/* devo_quiesce */
};

/*
 * For gfxprivate common vgatext code inclusion
 * we separate out the generic vgatext-related softc from
 * the RageXL-specific softc, since the gfxprivate interfaces
 * work with only the vgatext-relevant data structure.
 */

struct atiatom_softc {
	gfxp_vgatext_softc_ptr_t vgatext_softc; /* Opaque handle to vga text */
	/*
	 * The following are new, specific state entries (beyond the
	 * generic vgatext softc state) needed to implement
	 * Suspend/Resume
	 */
	kmutex_t		lock;
	kcondvar_t		op_cv;
	int			op_count;
	kcondvar_t		suspend_cv;
	boolean_t		suspended;

	ddi_acc_handle_t	conf;		/* for PCI config access */
	dev_info_t		*devi;
	/*
	 * Here are the specific device-access items
	 */
	volatile caddr_t	registers;
	ddi_acc_handle_t 	registersmap;
	volatile caddr_t 	biosrom;
	ddi_acc_handle_t	biosrommap;
	caddr_t			mem_base;
	caddr_t			reg_base;
	uint32_t		videoflags;
	uint32_t		videoflags_cur;
	uint32_t		videomode;
	uint32_t		videomode_cur;
	uint32_t		vrtflag;

	struct crtc_params	*pCRTC_Params;
#define	CRTC__Params (softc->pCRTC_Params)
	uint32_t		h_tot_disp;
	uint32_t		h_sync_strt_wid;
	uint32_t		v_tot_disp;
	uint32_t		v_sync_strt_wid;
	uint32_t		vline_crnt_vline;
	uint32_t		off_pitch;
	uint32_t		gen_cntl;
	unsigned char 		crtc[1024];
	unsigned char 		crt[0x19];
	uint32_t		vBUS_CNTL;
	uint32_t		vCRTC_INT_CNTL;
	uint32_t		vCRTC_GEN_CNTL;
	uint32_t		vGEN_TEST_CNTL;
	uint32_t		vDAC_CNTL;

	/*
	 * Here are the data used by the ATI script engine
	 */
	unsigned int		*RomBios;
	unsigned int		RamBios[(0x1000 / 4) + 0x10];
	TWorkSpace		TWS;
	unsigned char		*pvRomImage;
	unsigned char		*pvInitPll;
	unsigned char		*pvInitExtendedRegisters;
	unsigned char		*pvInitMemory;
	volatile caddr_t	BaseMemAddr;
	ddi_acc_handle_t	BaseMemAddrHandle;
	volatile caddr_t	IOBaseAddr;
	ddi_acc_handle_t	IOBaseAddrHandle;
	volatile caddr_t	AuxBaseAddr;
	ddi_acc_handle_t	AuxBaseAddrHandle;
	struct vgaregmap 	regs;
};

static void    *atiatom_softc_head;

/* Loadable Driver stuff */

static struct modldrv modldrv = {
	&mod_driverops,		/* Type of module.  This one is a driver */
	"ATI Atom driver",	/* Name of the module. */
	&atiatom_ops,		/* driver ops */
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *) &modldrv, NULL
};

static ddi_device_acc_attr_t dev_attr = {
	DDI_DEVICE_ATTR_V0,
	DDI_NEVERSWAP_ACC,
	DDI_STRICTORDER_ACC,
	0
};

int
_init(void)
{

	int	e;
	if ((e = ddi_soft_state_init(&atiatom_softc_head,
	    sizeof (struct atiatom_softc), 1)) != 0) {
		return (e);
	}
	e = mod_install(&modlinkage);

	if (e) {
		ddi_soft_state_fini(&atiatom_softc_head);
	}
	return (e);
}

int
_fini(void)
{
	int	e;

	if ((e = mod_remove(&modlinkage)) != 0)
		return (e);

	ddi_soft_state_fini(&atiatom_softc_head);

	return (0);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

/*
 * getsoftc() - Handy macro to get the device's softc
 */

#define	getsoftc(instance) ((struct atiatom_softc *)	\
			ddi_get_soft_state(atiatom_softc_head, (instance)))

/*
 * atiatom_attach -
 *	Prototye for NEW attach routine, which uses
 * gfxp_vgatext_attach() to support the common vgatext-related
 * attach code.
 */
static int
atiatom_attach(dev_info_t *devi, ddi_attach_cmd_t cmd)
{
	struct atiatom_softc *softc;
	int	unit = ddi_get_instance(devi);
	int	error;
	char	buf[80];	/* Used by minor-node sprintf */

	switch (cmd) {
	case DDI_ATTACH:
		break;

	case DDI_RESUME:
		softc = getsoftc(unit);
		mutex_enter(&softc->lock);
		RageXLInitChip(softc);

		/*
		 * Restoring the VGAtext mode's text is now
		 * handled by the common code's resume processing
		 */

		(void) gfxp_vgatext_attach(devi, DDI_RESUME,
		    softc->vgatext_softc);

		softc->suspended = B_FALSE;
		cv_broadcast(&softc->suspend_cv);
		mutex_exit(&softc->lock);
		return (DDI_SUCCESS);
		/* break; */

	default:
		return (DDI_FAILURE);
		/* break; */
	}

	/* DDI_ATTACH */

	/* Allocate ragexl softc struct */
	if (ddi_soft_state_zalloc(atiatom_softc_head, unit) != DDI_SUCCESS) {
		return (DDI_FAILURE);
	}
	softc = getsoftc(unit);

	/*
	 * We now also need to allocate a vgatext_softc struct
	 * used by the gfxp_vgatext_* routines
	 */

	softc->vgatext_softc = gfxp_vgatext_softc_alloc();
	if (!softc->vgatext_softc) {
		/* Free the driver softc */
		(void) ddi_soft_state_free(atiatom_softc_head, unit);
		return (DDI_FAILURE);
	}

	/*
	 * Common code VGA graphics initialization is now used
	 * for that component of the device attach
	 */
	error = gfxp_vgatext_attach(devi, cmd, softc->vgatext_softc);
	if (error != DDI_SUCCESS) {
		/* We alloc'd a vgatext_softc above, so free it. */
		gfxp_vgatext_softc_free(softc->vgatext_softc);
		/* And free the driver softc */
		(void) ddi_soft_state_free(atiatom_softc_head, unit);
		return (DDI_FAILURE);
	}

	softc->devi = devi;
	ddi_set_driver_private(devi, softc);

	/* Grab VGA IO registers to support suspend & resume. */
	(void) gfxp_fb_map_vga_ioreg(softc->vgatext_softc, &softc->regs);

	/*
	 * Initialization of mutex and cond vars used during ragexl
	 * Suspend/Resume operations
	 */
	mutex_init(&softc->lock, NULL, MUTEX_DRIVER, NULL);
	cv_init(&softc->op_cv, NULL, CV_DRIVER, NULL);
	cv_init(&softc->suspend_cv, NULL, CV_DRIVER, NULL);

	/*
	 * Create a minor node for the text console
	 */
	(void) sprintf(buf, "text-%d", unit);

	error = ddi_create_minor_node(devi, buf, S_IFCHR,
	    unit, DDI_NT_DISPLAY, NULL);
	if (error != DDI_SUCCESS)
		goto fail;

	/*
	 * This code which maps additional RageXL registers for the
	 * specific use of this driver is also unique to this driver.
	 *
	 * It should really be called atiatom_map_vga_registers()
	 * since that does NOT have to do with the generic
	 * vgatext functions in gfxp_vgatext_*()
	 */
	return (atiatom_map_vga_registers(softc));

fail:
	/*
	 * atiatom_detach() in turn will call vgatext_detach() and
	 * thus also free the vgatext_softc if it was allocated.
	 */

	(void) atiatom_detach(devi, DDI_DETACH);
	return (error);
}


static int
atiatom_detach(dev_info_t *devi, ddi_detach_cmd_t cmd)
{
	int	instance = ddi_get_instance(devi);
	struct atiatom_softc *softc = getsoftc(instance);

	switch (cmd) {
	case DDI_DETACH:

		/*
		 * Unmapping of registers etc. now happens in
		 * the gfx_vgatext_detach() routine of the
		 * common VGAtext code
		 */
		gfxp_fb_unmap_vga_ioreg(softc->vgatext_softc, &softc->regs);
		if (gfxp_vgatext_detach(devi, DDI_DETACH, softc->vgatext_softc))
			return (DDI_FAILURE);


		/*
		 * Free the VGAtext-specific softc if it was allocated
		 */
		if (softc->vgatext_softc) {
			gfxp_vgatext_softc_free(softc->vgatext_softc);
		}

		ddi_remove_minor_node(devi, NULL);

		/*
		 * If we're detaching the driver, destroy
		 * the mutex and cv's used for for Susp/Resume
		 */
		mutex_destroy(&softc->lock);
		cv_destroy(&softc->op_cv);
		cv_destroy(&softc->suspend_cv);
		(void) ddi_soft_state_free(atiatom_softc_head, instance);
		return (DDI_SUCCESS);

	case DDI_SUSPEND:
		mutex_enter(&softc->lock);
		ASSERT(softc->op_count >= 0);
		while (softc->op_count > 0)
			cv_wait(&softc->op_cv, &softc->lock);
		softc->suspended = B_TRUE;
		mutex_exit(&softc->lock);

		/*
		 * The gfxp_vgatext_detach routine, in the common
		 * VGAtext code, now saves away the VGA text info
		 */
		if (gfxp_vgatext_detach(devi, DDI_SUSPEND, softc->vgatext_softc)

		    != DDI_SUCCESS) {
			mutex_enter(&softc->lock);
			softc->suspended = B_FALSE;
			cv_broadcast(&softc->suspend_cv);
			mutex_exit(&softc->lock);
			return (DDI_FAILURE);
		}
		return (DDI_SUCCESS);

	default:
		cmn_err(CE_WARN, "atiatom_detach: unknown cmd 0x%x\n", cmd);
		return (DDI_FAILURE);
	}
}

/* ARGSUSED */
static int
atiatom_info(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg, void **result)
{
	dev_t	dev = (dev_t)arg;
	int	error;
	int	instance = getminor(dev);
	struct atiatom_softc	*softc = getsoftc(instance);

	error = DDI_SUCCESS;

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
		*result = (void *) (uintptr_t)instance;
		error = DDI_SUCCESS;
		break;
	default:
		error = DDI_FAILURE;
		break;
	}
	return (error);
}



/*
 * Note: Following the use of misc/gfxprivate common VGAtext code
 *	atiatom_open, atiatom_close, and atiatom_ioctl
 *	are mostly implemented by their peers in gfxprivate
 */

static int
atiatom_open(dev_t *devp, int flag, int otyp, cred_t *cred)
{
	struct atiatom_softc *softc = getsoftc(getminor(*devp));

	return (gfxp_vgatext_open(devp, flag, otyp, cred,
	    softc->vgatext_softc));
}

/* ARGSUSED */
static int
atiatom_close(dev_t devp, int flag, int otyp, cred_t *cred)
{
	return (0);
}


static int
atiatom_ioctl(dev_t dev, int cmd, intptr_t data, int mode,
	cred_t *cred, int *rval)
{
	int	err = 0;

	struct atiatom_softc *softc = getsoftc(getminor(dev));

	/*
	 * Prevent further ioctl operations (activity) on the device
	 * while the device is in a suspended state (during Suspend/Resume).
	 *
	 * All such operations are caused to wait until the device resume
	 * has occurred.
	 */
	mutex_enter(&softc->lock);
	ASSERT(softc->op_count >= 0);
	while (softc->suspended)
		cv_wait(&softc->suspend_cv, &softc->lock);
	ASSERT(softc->op_count >= 0);
	softc->op_count++;
	mutex_exit(&softc->lock);

	/*
	 * Now do the normal vgatext IOCTL processing:
	 *
	 * Hand the VGAtext-specific softc (within the atiatom_softc
	 * data structure) to the gfxp_vgatext_ioctl() routine
	 */
	err = gfxp_vgatext_ioctl(dev, cmd, data, mode, cred, rval,
	    softc->vgatext_softc);

	/*
	 *	Additional Suspend/Resume processing:
	 * Now that the present ioctl operation has completed, if
	 * there are no further ioctl operations outstanding, then
	 * wakeup a suspend thread who is blocked awaiting the
	 * completion of all such operations.
	 */
	mutex_enter(&softc->lock);
	softc->op_count--;
	ASSERT(softc->op_count >= 0);
	if (softc->op_count == 0)
		cv_broadcast(&softc->op_cv);
	mutex_exit(&softc->lock);

	return (err);
}

/* ARGSUSED */
static int
atiatom_devmap(dev_t dev, devmap_cookie_t dhp, offset_t off, size_t len,
    size_t *maplen, uint_t model)
{
	struct atiatom_softc *softc = getsoftc(getminor(dev));
	int	err;

	err = gfxp_vgatext_devmap(dev, dhp, off, len, maplen, model,
	    softc->vgatext_softc);
	return (err);
}

/*
 * atiatom_map_registers
 */
static int
atiatom_map_vga_registers(struct atiatom_softc *softc)
{

	dev_info_t	*devi = softc->devi;
	unsigned long	pagesize, pageoffset, ofs;


	pagesize = ddi_ptob(devi, 1);
	pageoffset = pagesize - 1;
	ofs = ATI_REGBASE8 & ~pageoffset;

	if (ddi_regs_map_setup(devi, 1, (caddr_t *)&softc->registers, ofs,
	    pagesize, &dev_attr, &softc->registersmap) != DDI_SUCCESS) {
		return (DDI_FAILURE);
	}
	softc->registers += (ATI_REGBASE8 - ofs);
	return (DDI_SUCCESS);
}
/*
 * Note:
 *
 * Above is the code to implement the standard Solaris driver entry points.
 * It now relies on the common VGATEXT code found in misc/gfx_private, to
 * avoid a duplicate implementation of those facilities.
 *
 *
 * The code below was provided by ATI and/or was created using
 * ATI NDA documentation about the specific chip(s) this supports.
 * None of this code can made public without prior agreement from
 * ATI.
 *
 * I will try to show how the part is connected to the computer
 * and cover some of the general concepts that may confuse people
 * trying to fix bugs.
 *
 *  The RageXL graphics chip is devided into 2 addressable sections:
 *
 *  1.  The VGA section:
 *	This memory is located at 0xa000 to 0xc000.
 *	This is where we write the text for the console mode.
 *	Also located in this area is the VBIOS, part of which we
 *	copy into system memory just after boot time.
 * 	You will see this during power up from S3 suspend.
 *
 *  2.  The Advanced graphics section:
 *	This section is located in memory at the address specified
 *	in PCI-BAR2 register.  Ths memory aperture is 16MB.
 *
 *  Terms you may see used in this driver:
 *
 *  VGA   - A historical standard interface supporting both
 *	    a text mode and a low resolution graphics mode
 *  VESA  - Video [?] Standards Association
 *  VBIOS - refers to the Video BIOS on the graphics module
 *	    This code is executed by the motherboard's BIOS
 *	    at Power on reset to initialize the graphics chip
 *
 *  CRTC -  The CRT controller and/or its registers
 *  DPMS -  Display power management [system]
 *  FBPM -  Framebuffer power management
 *  S3 suspend/resume - Suspend to RAM as supported on Intel platforms
 *  PCI -   The PCI bus interface and its associated configuration etc.
 */

/*
 * CLOCK_SEL_CNTL is CLOCK_CNTL at page 4-67.  The code is doing byte
 * accesses to this register.
 */

static void
set_clock_reg(struct atiatom_softc *softc, unsigned int addr, unsigned int data)
{
	softc->registers[CLOCK_SEL_CNTL + 1] = addr << 2 | CLOCK_CNTL_WR_EN;
	softc->registers[CLOCK_SEL_CNTL + 2] = (unsigned int)data;
	softc->registers[CLOCK_SEL_CNTL + 1] &= ~CLOCK_CNTL_WR_EN;
}

void
VideoPortMoveMemory(void *pvDestAddr, void *pvSourceAddr, unsigned int ulLength)
{
	unsigned int    i;
	unsigned char  *pucSourceAddr;
	unsigned char  *pucDestAddr;
	pucSourceAddr = (unsigned char *) pvSourceAddr;
	pucDestAddr = (unsigned char *) pvDestAddr;
	for (i = 0; i < ulLength; ++i) {
		*pucDestAddr++ = *pucSourceAddr++;
	}
}
int
VideoPortCompareMemory(unsigned char *pucSourceAddr, unsigned char *pucDestAddr,
    unsigned int ulLength)
{
	int	match = 0;
	unsigned int    i;
	for (i = 0; i < ulLength; ++i) {
		if (*(pucDestAddr + i) == *(pucSourceAddr + i))
			match++;
	}
	return (match);
}

unsigned char
VideoPortReadRegisterUchar(unsigned char *pucRegister)
{
	unsigned char   ucTmp;
	ucTmp = *pucRegister;

	return (ucTmp);
}

unsigned short
VideoPortReadRegisterUshort(unsigned short *pusRegister)
{
	unsigned short  usTmp;
	usTmp = *pusRegister;

	return (usTmp);
}

unsigned int
VideoPortReadRegisterUint(unsigned int *pulRegister)
{
	unsigned int    ulTmp;
	ulTmp = *pulRegister;

	return (ulTmp);
}

void
VideoPortWriteRegisterUchar(unsigned char *pucRegister, unsigned char ucData)
{
	*(pucRegister) = ucData;

}

void
VideoPortWriteRegisterUshort(unsigned short *pusRegister, unsigned short usData)
{
	*(pusRegister) = usData;

}

void
VideoPortWriteRegisterUint(unsigned int *pulRegister, unsigned int ulData)
{
	*(pulRegister) = ulData;
}

static void
VGA_BR_Mode_init(struct atiatom_softc *softc)
{
	/* set read/write pages to 0 */

/* LINTED */
	ATIREGL(softc, MEM_VGA_WP_SEL) = (uint32_t)0x10000;
/* LINTED */
	ATIREGL(softc, MEM_VGA_RP_SEL) = (uint32_t)0x10000;

	/* VGA mem.mapping Disable */

/* LINTED */
	ATIREGL(softc, CONFIG_CNTL) &= 0xfffffffb;

	/*
	 * Clears bits(VGA_TEXT_132 | VGA_XCRT_CNT_EN | VGA_LADDR)->(0x20 |
	 * 0x40 | 0x08) in CRTC_GEN_CNTL(3) Clears bits(CRTC_DBL_SCAN_EN |
	 * CRTC_INTERLACE_EN) (0x01 | 0x02) in CRTC_GEN_CNTL(0)
	 */

/* LINTED */
	ATIREGL(softc, CRTC_GEN_CNTL) = 0x02072200;
/* LINTED */
	ATIREGL(softc, CRTC_GEN_CNTL) = 0x024b2200;
	/*
	 * ATIREGL(softc, MEM_CNTL) = softc->SvMemCntl;
	 */
/* LINTED */
	ATIREGL(softc, MEM_CNTL) = 0x165a2b;
	/*
	 * ATIREGL(softc, MEM_ADDR_CONFIG) = softc->SvMemAddrConfig;
	 */
/* LINTED */
	ATIREGL(softc, MEM_ADDR_CONFIG) = 0x200213;
/* LINTED */
	ATIREGL(softc, HW_DEBUG) = 0x48833800;
/* LINTED */
	ATIREGL(softc, DP_WRITE_MASK) = 0xffffffff;
}

static void
NoBiosPowerOnAdapter(struct atiatom_softc *softc)
{

	RunScriptEngine(softc, INIT_PLL);
	RunScriptEngine(softc, INIT_EXTENDED_REGISTERS);
	RunScriptEngine(softc, INIT_MEMORY);
}
static void
SetVGAMode(struct atiatom_softc *softc)
{
	CXRegTable	VGARegs[12] = {
		/*
		 * CONFIG_STAT2, 0x06000100, CONFIG_STAT0, 0x00400086,
		 * CONFIG_CNTL, 0x00083F42,
		 */
		CRTC_GEN_CNTL, 0x020B2200,
		CRTC_H_TOTAL_DISP, 0x004F005F,
		CRTC_H_SYNC_STRT_WID, 0x00010055,
		CRTC_V_TOTAL_DISP, 0x018F01BF,
		CRTC_V_SYNC_STRT_WID, 0x000E019C,
		CRTC_OFF_PITCH, 0x0A000000,
		CRTC_INT_CNTL, 0x80000174,
		MEM_VGA_WP_SEL, 0x00010000,
		MEM_VGA_RP_SEL, 0x00010000,
		SCRATCH_REG0, 0x04900400,
		DAC_CNTL, 0x8401200A,
		BUS_CNTL, 0x7333A001};
	PllRegTable	VGAPllRegs[6] = {
		0x06, 0xFF,
		0x07, 0xDA,
		0x08, 0xF6,
		0x09, 0x00,
		0x0a, 0x00,
		0x19, 0x00
	};
	vInitPll(softc, VGAPllRegs, 7);
	vLoadInitBlock(softc, VGARegs, 12);
/* LINTED */
	ATIREGL(softc, CRTC_GEN_CNTL) = 0x020B2200;
	ATIREGB(softc, LCD_INDEX) = 0x01;
/* LINTED */
	ATIREGL(softc, LCD_DATA) = 0x000520C1;
	VGA_prog_regs(softc, 3);
}


/*
 * RageXLInitChip -
 *	Main routine to re-initialize the ATI chip after power off
 *
 * Note:
 *	Many of the references are to data that are in the
 * vgatext_softc structure.  We have now split that out of
 * the atiatom_softc structure, and must therefore refer to those
 * elements through a separate structure pointer.
 */

static void
RageXLInitChip(struct atiatom_softc *softc)
{
	/*
	 * Now refer separately to vgatext softc elements
	 */
	dev_info_t		*devi;

	softc->TWS.softc = softc;

	/*
	 * open i/o ports and setup vga
	 */


	/* setup mapping for PCI config space access */
	(void) pci_config_setup(softc->devi, &softc->conf);

	/*
	 * Note: There be Dragons here.
	 *
	 *	There is an undocumented bit (bit 2) in the
	 * RageXL chip's PCI configuration register 0x40.
	 * Furthermore this is an undocumented write-only bit.
	 *
	 * Woefully, it appears to be necessary to set this bit
	 * in order to reset the chip's PCI bus interface
	 * and thus re-enable its response to memory-mapped i/o
	 * accesses to the dang thing on the PCI bus.
	 *
	 * Prior comment: "unblock i/o in phantom register"
	 */
	if (pci_config_get8(softc->conf, 0x40))
		pci_config_put8(softc->conf, 0x40, 0);

	if (!(pci_config_get8(softc->conf, 0x40)&4))
		pci_config_put8(softc->conf, 0x40, 4);

	if ((pci_config_get16(softc->conf, PCI_CONF_COMM) &
	    (PCI_COMM_MAE | PCI_COMM_IO | PCI_COMM_ME)) !=
	    (PCI_COMM_MAE | PCI_COMM_IO | PCI_COMM_ME)) {
	pci_config_put16(softc->conf, PCI_CONF_COMM,
	    (pci_config_get16(softc->conf, PCI_CONF_COMM)
	    | PCI_COMM_MAE | PCI_COMM_IO | PCI_COMM_ME));
	}
	pci_config_put32(softc->conf, 0x4c, 0x80081002);
	ddi_put8(softc->regs.handle, (void *) GENENA, 0x10);
	ddi_put8(softc->regs.handle, (void *) GENVS, 1);
	ddi_put8(softc->regs.handle, (void *) GENENA, 8);
	ddi_put8(softc->regs.handle, (void *) GENMO, 0x67);

	/*
	 * Map all the registers in the ATI chip and plug in all the pointers
	 * here.
	 */

	softc->biosrom = i86devmap(btop((size_t)0xc0000), 1,
	    PROT_READ | PROT_WRITE | PROT_EXEC);

	devi = softc->devi;

	(void) ddi_regs_map_setup(devi, 1, (caddr_t *)&softc->BaseMemAddr, 0,
	    0x1000000, &dev_attr, &softc->BaseMemAddrHandle);


	(void) ddi_regs_map_setup(devi, 2, (caddr_t *)&softc->IOBaseAddr, 0,
	    0x100, &dev_attr, &softc->IOBaseAddrHandle);

	if (!softc->BaseMemAddr)
		return;
	if (!softc->IOBaseAddr)
		return;

	softc->pvInitPll = NULL;
	softc->pvInitExtendedRegisters = NULL;
	softc->pvInitMemory = NULL;

	LoadFirst4K(softc);
	softc->pvRomImage = (unsigned char *) softc->RamBios;
	SetupScriptInitEntry(softc);

	/*
	 * If the first word matches 0xaa55 we will proceed to
	 * NoBiosPowerOnAdapter() and use the previously saved
	 * VBIOS image.
	 */
	if ((unsigned short) softc->RamBios[0] != 0xaa55)
		return;

	NoBiosPowerOnAdapter(softc);


	ddi_put8(softc->regs.handle, (void *) GENENA, 0x10);
	ddi_put8(softc->regs.handle, (void *) GENVS, 1);
	ddi_put8(softc->regs.handle, (void *) GENENA, 8);
	ddi_put8(softc->regs.handle, (void *) GENMO, 0x67);

	SetVGAMode(softc);

	ddi_regs_map_free(&softc->BaseMemAddrHandle);
	ddi_regs_map_free(&softc->IOBaseAddrHandle);
	pci_config_teardown(&softc->conf);
}

static void
VGA_prog_regs(struct atiatom_softc *softc, int vgamode)
{
	/*
	 * Now refer separately to vgatext softc elements
	 */

	static unsigned char VGATable03[0x40] = {
		0x50, 0x18, 0x10,
		0x00, 0x10,
		0x00, 0x03, 0x00, 0x02,
		0x67,
		0x5f, 0x4f, 0x50, 0x82, 0x55, 0x81, 0xbf, 0x1f,
		0x00, 0x4f, 0x0d, 0x0e, 0x00, 0x00, 0x00, 0x00,
		0x9c, 0x8e, 0x8f, 0x28, 0x1f, 0x96, 0xb9, 0xa3, 0xff,
		0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07,
		0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
		0x0c, 0x00, 0x0f, 0x08,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x0e, 0x00, 0xff
	};

	static unsigned char VGATable12[0x40] = {
		0x50, 0x1d, 0x10,
		0x00, 0xa0,
		0x01, 0x0f, 0x00, 0x06,
		0xe3,
		0x5f, 0x4f, 0x50, 0x82, 0x54, 0x80, 0x0b, 0x3e,
		0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0xea, 0x8c, 0xdf, 0x28, 0x00, 0xe7, 0x04, 0xe3, 0xff,
		0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07,
		0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
		0x01, 0x00, 0x0f, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x0f, 0xff
	};

	static unsigned char VGATable62[0x40] = {
		0x50, 0x1d, 0x10,
		0x00, 0xa0,
		0x01, 0x0f, 0x00, 0x0a,
		0xe3,
		0x5f, 0x4f, 0x50, 0x82, 0x54, 0x80, 0x0b, 0x3e,
		0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0xea, 0x8c, 0xdf, 0x28, 0x00, 0xe7, 0x04, 0xe3, 0xff,
		0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
		0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
		0x01, 0x00, 0x0f, 0x00,
		0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x0f, 0xff
	};

	int	i;
	unsigned char  *VGATable;

	/*
	 * Get a parameter table.
	 * vgamode passed in is the actual vga mode.
	 * However, only mode 3, 12, and 62 are supported for now.
	 *
	 * for mode  3 -> index 0
	 * for mode  7 -> index 1
	 * for mode 12 -> index 2
	 * for mode 62 -> index 3
	 */
	switch (vgamode) {
	case 0x03:
		vgamode = VMODE_3;
		VGATable = VGATable03;
		break;
	case 0x12:
		vgamode = VMODE_12;
		VGATable = VGATable12;
		break;
	case 0x62:
		vgamode = VMODE_62;
		VGATable = VGATable62;
		break;
	default:	/* all other non-supported VGA mode, set to mode 3 */
		vgamode = VMODE_3;
		VGATable = VGATable03;
		break;
	}

/* LINTED */
	ATIREGL(softc, CONFIG_STAT0) = 0x00000016;
/* LINTED */
	ATIREGL(softc, CONFIG_CNTL) = 0x00003f02;

	/* Reset CRTC */
/* LINTED */
	ATIREGL(softc, CRTC_GEN_CNTL) = 0x020d2200;
/* LINTED */
	ATIREGL(softc, CRTC_GEN_CNTL) = 0x020f2200;
/* LINTED */
	ATIREGL(softc, CRTC_GEN_CNTL) = 0x024b2200;

	/* turn off the palette before programming the VGA chip */

	(void) ddi_get8(softc->regs.handle, (void *) GENS1);
	ddi_put8(softc->regs.handle, (void *) ATTRX, 0);

	VGA_BR_Mode_init(softc);

	/*
	 *  VGA_prog_sequencer
	 */
	/* stop the sequencer */
	ddi_put16(softc->regs.handle, (void *) SEQX, 0x0100);
	for (i = 1; i <= 4; i++) {
		/* mask off the ATI internal data bits for ATI extended regs */
		ddi_put16(softc->regs.handle, (void *) SEQX,
		    ((VGATable[5 + i - 1] << 8) | i));
	}

	ddi_put8(softc->regs.handle, (void *) GENMO, VGATable[9]);

	/* for mode 3 make sure bits 3:2 = 01 - 28 MHz */

	/*
	 * start the sequencer again - do I need to prog. ext.reg-s before?
	 */
	ddi_put16(softc->regs.handle, (void *) SEQX, 0x300);

	/*
	 *  VGA_prog_crt_controller
	 */

	/* enable write for 0-10h */
	ddi_put16(softc->regs.handle, (void *) CRTX, 0x3011);

#define	CRTC_PARM_LEN   25
	for (i = 0; i < CRTC_PARM_LEN; i++) {
		ddi_put8(softc->regs.handle, (void *) CRTX, i);
		ddi_put8(softc->regs.handle, (void *) CRTD,
		    VGATable[10 + i]);
	}

	ddi_put8(softc->regs.handle, (void *) CRTX, 0x15);

	/* make sure 3da/3ba is zero, such that the cursor blink normally */

	ddi_put8(softc->regs.handle, (void *) GENS1, 0x00);

	/*
	 *  VGA_prog_graphics
	 */

#define	GRP_PARM_LEN    9
	for (i = 0; i < GRP_PARM_LEN; i++) {
		ddi_put8(softc->regs.handle, (void *) GRAX, i);
		ddi_put8(softc->regs.handle, (void *) GRAD,
		    VGATable[55 + i]);
	}
	/*
	 *  VGA_prog_attribute
	 */

	/* clear attribute controller flip-flop */
	(void) ddi_get8(softc->regs.handle, (void *) GENS1);

#define	ATTR_PARM_LEN   20
	for (i = 0; i < ATTR_PARM_LEN; i++) {
		ddi_put8(softc->regs.handle, (void *) ATTRX, i);
		ddi_put8(softc->regs.handle, (void *) ATTRX,
		    VGATable[35 + i]);
	}

	ddi_put8(softc->regs.handle, (void *) ATTRX, 0x14);
	ddi_put8(softc->regs.handle, (void *) ATTRX, 0);

	/*
	 * DAC Programming
	 */
	/* Blank the screen */
	ddi_put8(softc->regs.handle, (void *) SEQX, 1);
	ddi_put8(softc->regs.handle, (void *) SEQD, (VGATable[5] | 0x20));

	ddi_put8(softc->regs.handle, (void *) DAC_MASK, 0xff);
	/* pulled out */
	(void) ddi_get8(softc->regs.handle, (void *) GENS1);
	/* ATTR_PALRW_ENB */
	ddi_put8(softc->regs.handle, (void *) ATTRX, 0x20);
	/* clear the flip-flop in attribute controller */

	(void) ddi_get8(softc->regs.handle, (void *) GENS1);
	ddi_put8(softc->regs.handle, (void *) SEQX, 1);
	ddi_put8(softc->regs.handle, (void *) SEQD, VGATable[5]);

	/* End of dac programming */

/* LINTED */
	ATIREGL(softc, CRTC_GEN_CNTL) = 0x024b2200;

	/* turn on the palette */

	(void) ddi_get8(softc->regs.handle, (void *) GENS1);
	/* ATTR_PALRW_ENB */
	ddi_put8(softc->regs.handle, (void *) ATTRX, 0x20);
	(void) ddi_get8(softc->regs.handle, (void *) GENS1);
	(void) ddi_get8(softc->regs.handle, (void *) GENMOR);
	(void) ddi_get8(softc->regs.handle, (void *) GENENB);

	/* reset the palette */
	for (i = 0; i < VGA_TEXT_CMAP_ENTRIES; i++) {
		vga_put_cmap(&softc->regs, i, VGA_TEXT_PALETTES[i][0] << 2,
		    VGA_TEXT_PALETTES[i][1] << 2,
		    VGA_TEXT_PALETTES[i][2] << 2);
	}
	for (i = VGA_TEXT_CMAP_ENTRIES; i < VGA8_CMAP_ENTRIES; i++) {
		vga_put_cmap(&softc->regs, i, 0, 0, 0);
	}
}
/*
 * vInitPll -
 *	Initialize the ATI graphics chip's phase locked loop hardware
 */
static void
vInitPll(struct atiatom_softc *softc, PllRegTable PllRegs[], int PLLMaxInd)
{
	int	i;
	unsigned char   temp;
	volatile uint8_t *regs = (uint8_t *)softc->registers;

	temp = regs[0x91];
	for (i = 0; i < PLLMaxInd; ++i) {
		set_clock_reg(softc, PllRegs[i].RegAddress,
		    (unsigned int) PllRegs[i].RegContent);
		drv_usecwait(100);
	}
	regs[0x91] = temp;
}

static void
vLoadInitBlock(struct atiatom_softc *softc, CXRegTable CxRegs[], int CxMaxInd)
{
	int	i;

	for (i = 0; i < CxMaxInd; ++i) {
/* LINTED */
		ATIREGL(softc, CxRegs[i].RegAddress) = CxRegs[i].RegContent;
		drv_usecwait(100);
	}
}

/*
 * RunScriptEngine -
 *	Run the ATI script engine (to interpret VBIOS code)
 */
static void
RunScriptEngine(struct atiatom_softc *softc, int type)
{
	TWorkSpace	*pws;

	pws = &softc->TWS;
	(void *) memset(pws, 0, sizeof (TWorkSpace));
	pws->pucBIOSImage = (unsigned char *) softc->pvRomImage;
	pws->bEndOfProcessing = 0;
	pws->softc = softc;

	if (type == INIT_PLL) {
		pws->pucScript = pws->pucScriptTable = (unsigned char *)
		    softc->pvInitPll;
	} else if (type == INIT_EXTENDED_REGISTERS) {
		pws->pucScript = pws->pucScriptTable = (unsigned char *)
		    softc->pvInitExtendedRegisters;
	} else {
		pws->pucScript = pws->pucScriptTable = (unsigned char *)
		    softc->pvInitMemory;
	}
	pws->bPrimaryAdapter = 1;
	ExecuteScript(pws);
}

/*
 * LoadFirst4K -
 *	Copy the first 4K of the ATI graphics module's VBIOS
 * into the RomBios data structure
 */
static void
LoadFirst4K(struct atiatom_softc *softc)
{
/* LINTED */
	softc->RomBios = (unsigned int *) softc->biosrom;
	move_bios(softc->RamBios, (unsigned int *) softc->RomBios);
}

static void
move_bios(volatile unsigned int *out, volatile unsigned int *in)
{
	int	i;

	for (i = 0; i < (0x1000 / 4); i++)
		out[i] = in[i];
}

/*
 * Utility routines to implement the ATI ScriptEngine:
 *
 *	ucFilterByte, usFilterWord, ulFilterLong ... -
 */
static unsigned char
ucFilterBYTE(TWorkSpace * pWS)
{				/* for script  code access    */
	unsigned char   ucValue = *(pWS->pucScript)++;
	return (ucValue);
}

static unsigned short
usFilterWORD(TWorkSpace * pWS)
{				/* for script code access */
/* LINTED */
	unsigned short  wValue = *((unsigned short *) pWS->pucScript);
	pWS->pucScript += sizeof (unsigned short);
	return (wValue);
}

static unsigned int
ulFilterDWORD(TWorkSpace * pWS)
/*
 * for script code access
 */
{
/* LINTED */
	unsigned int    ulValue = *((unsigned int *) pWS->pucScript);
	pWS->pucScript += sizeof (unsigned int);

	return (ulValue);
}

static void
vScanAttribute(TWorkSpace * pWS, RegisterAttribute * pAttr)
{
	/*
	 * gets called from every script command ! !!!!In regAttr. we keep th
	 * attr. of an oper.command, in modeI / O we keep the attr.of a
	 * spec.command
	 */

	switch (pWS->ucIOMode) {
		case ATTRIB_BYTE_PCI_IO:	/* 0x00 */
		case ATTRIB_BYTE_DEST_ARRAY:	/* 0x01 */
		case ATTRIB_BYTE_INDEX_BASE_IO:	/* 0x02 */
		case ATTRIB_BYTE_DOS_DATA:	/* 0x03 */
		pAttr->ucAttrib = ucFilterBYTE(pWS);
		break;
	case ATTRIB_WORD_MEM_MAP_0:
		/* 0x04 */
	case ATTRIB_WORD_MEM_MAP_1:
		/* 0x05 */
	case ATTRIB_WORD_IO_ADDRESS:
		/* 0x06 */
	case ATTRIB_WORD_BIOS_IMAGE:
		/* 0x07 */
		pAttr->usAttrib = usFilterWORD(pWS);
		break;
	case ATTRIB_WORD_BUS_DEV_FCN:
		/* 0x40 */
		pAttr->ucAttrib = ucFilterBYTE(pWS);
		/* offset in Config Space */
		break;
	default:
		break;
	}
}

static unsigned int
ulGetValue(TWorkSpace * pWS)
/* gets the content acc.alignment(the last one is not only for WArray) */
{
	unsigned int    ulValue = 0;
	switch (CLASSIFY[pWS->ucAlignment]) {
	case T_BYTE:
		ulValue = (unsigned int) ucFilterBYTE(pWS);
		break;
	case T_WORD:
		ulValue = (unsigned int) usFilterWORD(pWS);
		break;
	default:
		ulValue = ulFilterDWORD(pWS);
		break;
	}
	return (ulValue);
}

static void
vCompareWithNextRead(TWorkSpace * pWS, unsigned int ulValue2)
{
	/* compares ulValue2 with pWS->ulSavedValue and sets pWS->ucFlags */
	pWS->ucFlags = 0;
	if (pWS->ulSavedValue > ulValue2)
		pWS->compareFlag = GREATER_THAN;
	else if (pWS->ulSavedValue < ulValue2)
		pWS->compareFlag = LESS_THAN;
	else
		pWS->compareFlag = EQUAL;
}

/*
 * vCompareWithNextValue -
 *	Compares ulValue1 with(pucScript) and sets pWS->ucFlags.
 * ulValue1 is the data read from the command
 */
static void
vCompareWithNextValue(TWorkSpace * pWS, unsigned int ulValue1)
{
	unsigned int    ulValue2 = ulGetValue(pWS);
	pWS->ucFlags = 0;

	if (ulValue1 > ulValue2)
		pWS->compareFlag = GREATER_THAN;
	else if (ulValue1 < ulValue2)
		pWS->compareFlag = LESS_THAN;
	else
		pWS->compareFlag = EQUAL;
}

/*
 * vComparePending -
 *	sets COMPARE_PENDING in ucFlags
 */
static void
vComparePending(TWorkSpace * pWS, unsigned int ulValue)

{
	pWS->ucFlags &= ~COMPARE_WITH_NEXTREAD;
	/*
	 * clears COMPARE_WITH_NEXTREAD in ucFlags:
	 */

	pWS->ucFlags |= COMPARE_PENDING;
	/* sets COMPARE_PENDING */
	pWS->ulSavedValue = ulValue;
}

/*
 * vTestWithNextRead -
 *	tests ulValue2 with pWS->ulSavedValue and sets pWS->ucFlags
 */
static void
vTestWithNextRead(TWorkSpace * pWS, unsigned int ulValue2)

{
	pWS->ucFlags = 0;

	ulValue2 &= pWS->ulSavedValue;

	if (!ulValue2)
		pWS->compareFlag = EQUAL;
	else
		pWS->compareFlag = GREATER_THAN;
}

/*
 * vTestWithNextValue -
 *	compares ulValue1 with(pucScript) and sets pWS->ucFlags
 */
static void
vTestWithNextValue(TWorkSpace * pWS, unsigned int ulValue1)
{
	unsigned int    ulValue2 = ulGetValue(pWS);
	pWS->ucFlags = 0;

	ulValue2 &= ulValue1;
	if (!ulValue2)
		pWS->compareFlag = EQUAL;
	else
		pWS->compareFlag = GREATER_THAN;
}

static void
vTestPending(TWorkSpace * pWS, unsigned int ulValue)
{
	pWS->ucFlags &= ~TEST_WITH_NEXTREAD;
	pWS->ucFlags |= TEST_PENDING;
	pWS->ulSavedValue = ulValue;
}

/*
 * Memory test and config functions:
 *
 *	GetMemoryByPage, bIs32BitMemory,
 *	bTest64BitMemory, bTestMemoryWithPattern, bTestPattern
 *	bTestPackedPixel, bTestWrapAround, bMapLfbAperture
 *	bTestMemConfig(main fcn), scriptTestMemConfig
 */
static unsigned char *
GetMemoryByPage(struct atiatom_softc *softc, unsigned int ulMemPage)
{
	unsigned char  *pucBaseAddress = (unsigned char *)(softc->BaseMemAddr);

	return (pucBaseAddress + ulMemPage * 0x8000);
	/* one page is 32 KB */
}

static char
bIs32BitMemory(struct atiatom_softc *softc)
{
	unsigned char   ucConfigStat0 = ATIREGB(softc, CONFIG_STAT0);
	if ((ucConfigStat0 & 0x06) == 0x06)
		return (1);
	return (0);
}

static char
bTest64BitMemory(struct atiatom_softc *softc)
/* ditto  */
{
	unsigned int   *pulPattern;

/* LINTED */
	pulPattern = (unsigned int *) GetMemoryByPage(softc, 0);
	pulPattern[0] = 0x55555555;
	pulPattern[1] = 0xAAAAAAAA;

	if (pulPattern[0] != 0x55555555 || pulPattern[1] != 0xAAAAAAAA) {
		return (0);
	}
	return (1);
}


static char
bTestMemoryWithPattern(struct atiatom_softc *softc, void *pvStartAddress,
    unsigned char ucPattern, unsigned int ulLength)
{
	unsigned int	ulCount;	/* Loop counter */
	unsigned int    ulMemBufCntl;	/* For readback cache invalidation */
	unsigned char	ucSavedByte, ucReadByte;	/* Original byte at */
							/* probed location */

/* LINTED */
	ulMemBufCntl = ATIREGL(softc, MEM_BUF_CNTL);
	ulMemBufCntl |= MEM_BUF_CNTL_InvalidateRbCache;

	for (ulCount = 0; ulCount < ulLength; ulCount++) {
		ucSavedByte = VideoPortReadRegisterUchar((unsigned
		    char *) pvStartAddress + ulCount);
		/* Save original byte */
		VideoPortWriteRegisterUchar((unsigned char *)
		    pvStartAddress + ulCount, ucPattern);

/* LINTED */
		ATIREGL(softc, MEM_BUF_CNTL) = ulMemBufCntl;
		/* Invalidate cache */

		ucReadByte = VideoPortReadRegisterUchar((unsigned char *)
		    pvStartAddress + ulCount);
		if (ucReadByte != ucPattern) {
			return (0);
		}
		VideoPortWriteRegisterUchar((unsigned char *)
		    pvStartAddress + ulCount, ucSavedByte);
		/* Restore original byte */
	}

	return (1);
}

static char
bTestPattern(struct atiatom_softc *softc, unsigned char ucPattern,
    unsigned int ulMemPages)
{
	unsigned char  *pucVideoRam;

	pucVideoRam = GetMemoryByPage(softc, ulMemPages);
	if (bTestMemoryWithPattern(softc, pucVideoRam, ucPattern, 0xEFFF))
		return (1);
	return (0);
}


static char
bTestPackedPixel(struct atiatom_softc *softc, unsigned int ulMemPages)
{
	if (!bTestPattern(softc, 0xAA, ulMemPages))
		return (0);
	if (!bTestPattern(softc, 0x55, ulMemPages))
		return (0);

	return (bTestPattern(softc, 0x00, ulMemPages));
}

#define	TEST_BYTES_NUMBER   32
static char
bTestWrapAround(struct atiatom_softc *softc, unsigned int ulMemPages)
{
	unsigned int    i, ulMemBufCntl;
	unsigned char   ucBuffer[TEST_BYTES_NUMBER],
	    ucCheckBuffer[TEST_BYTES_NUMBER];
	unsigned char  *pucVideoRam;
	/* BOOL bStatus = 1; */

/* LINTED */
	ulMemBufCntl = ATIREGL(softc, MEM_BUF_CNTL);
	ulMemBufCntl |= MEM_BUF_CNTL_InvalidateRbCache;
	for (i = ulMemPages; i >= 32; i -= 32) {
		pucVideoRam = GetMemoryByPage(softc, i);
		(void *) memset(ucBuffer, i, TEST_BYTES_NUMBER);

		VideoPortMoveMemory(pucVideoRam, ucBuffer, TEST_BYTES_NUMBER);

/* LINTED */
		ATIREGL(softc, MEM_BUF_CNTL) = ulMemBufCntl;
		/* Invalidate cache */

		VideoPortMoveMemory(ucCheckBuffer, pucVideoRam,
		    TEST_BYTES_NUMBER);
		if (VideoPortCompareMemory(ucCheckBuffer, ucBuffer,
		    TEST_BYTES_NUMBER) != TEST_BYTES_NUMBER) {

			return (0);
		}
	}
	return (1);
}
static char
bTestMemConfig(struct atiatom_softc *softc, unsigned int ulMemPages)
{
	if (!bTestWrapAround(softc, ulMemPages))
		return (0);
	if (!bIs32BitMemory(softc) && !bTest64BitMemory(softc))
		return (0);
	if (!bTestPackedPixel(softc, ulMemPages))
		return (0);

	return (1);
}

static unsigned int
ulReadDataFromVGA(TWorkSpace * pWS, unsigned short VGAddress,
    unsigned char ucAlignment)
{
	/*
	 * Now refer separately to vgatext softc elements
	 */
	struct atiatom_softc	*softc = pWS->softc;

	unsigned int    ulValue;
	unsigned char   ucOffset;
	unsigned long   pointer;

	switch (ucAlignment) {
	case REGX_LOWER_WORD:
	case REGX_MIDDLE_WORD:
	case REGX_UPPER_WORD:
		ucOffset = ucAlignment & 0x03;
		pointer = (VGAddress + ucOffset);

		ulValue = (unsigned int) ddi_get16(softc->regs.handle,
		    (uint16_t *)pointer);
		break;
	case REGX_DWORD:
		ucOffset = 0;
		pointer = (VGAddress + ucOffset);
		ulValue = (unsigned int)

		    ddi_get32(softc->regs.handle, (void *) pointer);
		break;
	case REGX_BYTE0:
	case REGX_BYTE1:
	case REGX_BYTE2:
	case REGX_BYTE3:
		ucOffset = ucAlignment & 0x03;
		pointer = (VGAddress + ucOffset);
		ulValue = (unsigned int)

		    ddi_get8(softc->regs.handle, (void *) pointer);
		break;
	default:
		break;
	}
	return (ulValue);
}

static unsigned int
ulReadDataFromIO(unsigned char *pIoAddress, unsigned char ucAlignment)
{
	unsigned int    ulValue;
	unsigned char   ucOffset;

	switch (ucAlignment) {
	case REGX_LOWER_WORD:
	case REGX_MIDDLE_WORD:
	case REGX_UPPER_WORD:
		ucOffset = ucAlignment & 0x03;
		ulValue = (unsigned int)
/* LINTED */
		    VideoPortReadRegisterUshort((unsigned short *)
		    (pIoAddress + ucOffset));
		break;
	case REGX_DWORD:
		ucOffset = 0;
/* LINTED */
		ulValue = VideoPortReadRegisterUint((unsigned int *)
		    (pIoAddress + ucOffset));
		break;
	case REGX_BYTE0:
	case REGX_BYTE1:
	case REGX_BYTE2:
	case REGX_BYTE3:
		ucOffset = ucAlignment & 0x03;
		ulValue = (unsigned int)
		    VideoPortReadRegisterUchar((unsigned char *)
		    (pIoAddress + ucOffset));
		break;
	default:
		break;
	}
	return (ulValue);
}

static unsigned int
ulReadDataFromWorkSpace(TWorkSpace * pWS, unsigned char ucIndex,
    unsigned char ucAlignment)
/*
 * reads from workArray acc.index / Align(dword / byte) in case         of
 * PCIConfigAccess
 */
{
	unsigned int    ulValue;
	unsigned char   ucOffset;

	ucIndex = (ucIndex & 0x18) >> 3;
	/* 4:3 INDEX */
	switch (ucAlignment) {
	case REGX_LOWER_WORD:
	case REGX_MIDDLE_WORD:
	case REGX_UPPER_WORD:
		ucOffset = ucAlignment & 0x03;
		ulValue = (unsigned int)
		    (pWS->mem_store[ucIndex].usMem[ucOffset]);
		break;
	case REGX_DWORD:
		ucOffset = 0;
		ulValue = pWS->mem_store[ucIndex].ulMem;
		break;
	case REGX_BYTE0:
	case REGX_BYTE1:
	case REGX_BYTE2:
	case REGX_BYTE3:
		ucOffset = ucAlignment & 0x03;
		ulValue = (unsigned int)
		    (pWS->mem_store[ucIndex].ucMem[ucOffset]);
		break;
	default:
		break;
	}
	return (ulValue);
}

static unsigned int
ulReadDataFromIndexedRegister(TWorkSpace * pWS, RegisterAttribute attr)
/*
 * gets called from ReadDataFrom - attr = attr.of the operat.command
 */
{
	unsigned int    ulValue = 0, ulTemp, ulMask;
	unsigned char  *pIoAddress, *pBaseAddress;
	unsigned long   temp;
	switch (pWS->ucIOMode) {
	case ATTRIB_BYTE_PCI_IO:	/* 0x00 */
		pBaseAddress = (unsigned char *) (pWS->softc->BaseMemAddr);
		break;
	case ATTRIB_BYTE_INDEX_BASE_IO:
		/* 0x02 */
		temp = pWS->usIOBaseAddress;
		pBaseAddress = (unsigned char *) temp;
		/* usIOBaseAddress is assigned at scriptIndexedRegAccess */
		/* THIS IS for Primary */
		break;
	default:
		break;
	}
	ulMask = ulMaskTable[pWS->ucNumberOfIndexBits] <<
	    pWS->ucStartBitOfIndex;
	pIoAddress = pBaseAddress + LfbOffset +
	    BLOCK0_ADDRESS_OF(pWS->ucIndexAddress);
/* LINTED */
	ulTemp = VideoPortReadRegisterUint((unsigned int *) pIoAddress);
	ulTemp &= ~ulMask;
	ulTemp |= (ulMask & (attr.ucAttrib << pWS->ucStartBitOfIndex));
/* LINTED */
	VideoPortWriteRegisterUint((unsigned int *) (pIoAddress), ulTemp);
	/*
	 * DELAY_MICROSECONDS(5);
	 */
	pIoAddress = pBaseAddress + LfbOffset +
	    BLOCK0_ADDRESS_OF(pWS->ucDataAddress);
	ulValue = ulReadDataFromIO(pIoAddress, pWS->ucAlignment);
	/*
	 * why not VidPortRead
	 */
	return (ulValue);
}

static unsigned int
ulReadDataFromPLL(TWorkSpace * pWS, RegisterAttribute attr)
{
	unsigned short  usTemp, usMask;
	unsigned char  *pAddress, ucValue;


	pAddress = (unsigned char *) (pWS->softc->BaseMemAddr) + LfbOffset +
	    BLOCK0_ADDRESS_OF(pWS->ucIndexAddress);

/* LINTED */
	usTemp = VideoPortReadRegisterUshort((unsigned short *) pAddress);
	usMask = usTemp;
	usTemp &= ~pWS->usAndMask;	/* why ~ */
	usTemp |= (attr.ucAttrib << pWS->ucStartBitOfIndex) | pWS->usOrMask;
/* LINTED */
	VideoPortWriteRegisterUshort((unsigned short *) pAddress, usTemp);
	drv_usecwait(100);

	pAddress = (unsigned char *) (pWS->softc->BaseMemAddr) + LfbOffset +
	    BLOCK0_ADDRESS_OF(pWS->ucDataAddress);

	ucValue = VideoPortReadRegisterUchar(pAddress);
	drv_usecwait(100);

	pAddress = (unsigned char *) (pWS->softc->BaseMemAddr) + LfbOffset +
	    BLOCK0_ADDRESS_OF(pWS->ucIndexAddress);

/* LINTED */
	VideoPortWriteRegisterUshort((unsigned short *) pAddress, usMask);
	drv_usecwait(100);
	return ((unsigned int)ucValue);
}

static void
SetConfigData(struct atiatom_softc *softc, int addr, int len,
    unsigned int value)
{
	switch (len) {
	case 1:
		pci_config_put8(softc->conf, addr, (unsigned char)value);
		break;
	case 2:
		pci_config_put16(softc->conf, addr, (unsigned short)value);
		break;
	case 4:
		pci_config_put32(softc->conf, addr, value);
		break;
	default:
		break;
	}
}
static void
GetConfigData(struct atiatom_softc *softc, int addr, int len,
    unsigned int *value)
{
	switch (len) {
	case 1:
		*value = (unsigned int) pci_config_get8(softc->conf, addr);
		break;
	case 2:
		*value = (unsigned int) pci_config_get16(softc->conf, addr);
		break;
	case 4:
		*value = (unsigned int) pci_config_get32(softc->conf, addr);
		break;
	default:
		break;
	}
}

static unsigned int
ulReadDataFromPCIConfigSpace(TWorkSpace * pWS, RegisterAttribute attr)
{
	/*
	 * We need 3 arguments: BusDevFcn, offset from beginning of ConfigSpace
	 * and how many bytes.
	 * BusDevFcn = HW_DEVICE_EXTENSION->bus_dev_fcn in
	 * case ATTRIB_BYTE_PCI_IO BusDevFcn = pWS->ucIndexOfWS,
	 * REGX_UPPER_WORD in case ATTRIB_WORD_BUS_DEV_FCN Offset =
	 * attr.ucAttrib(given after operat.command) HowManyBytes = 0, 1,  2
	 * or 3 in pWS->ucAlignment i.e(REG_SET_BITS or << REG_BYTE0 >>) - *
	 * set by ExecScript Gen.form int GetConfigData(unsigned short
	 * BusDevFcn, unsigned char Offset, int HowManyBytes, (void *)
	 * Buffer)
	 */
	unsigned int    ulValue = 2;			/* buffer */
	int	Len = ACCESS_LEN[pWS->ucAlignment];	/* HowManyBytes */

	switch (pWS->ucIOMode) {
	case ATTRIB_BYTE_PCI_IO:
		GetConfigData(pWS->softc, attr.ucAttrib, Len, &ulValue);
		break;

	case ATTRIB_WORD_BUS_DEV_FCN:
		(void) ulReadDataFromWorkSpace(pWS, pWS->ucIndexOfWS,
		    REGX_UPPER_WORD);
		GetConfigData(pWS->softc, attr.ucAttrib, Len, &ulValue);
		break;

	default:
		break;
	}
	return (ulValue);
}

/*
 * ulReadDataFromDirectRegister -
 *
 * (Note: Following gobbledegook is from earlier ATI code comments):
 *
 *	Called from oper.command. In RegAttr we keep the attr of the
 * oper.com which is address.  In pWS->ucIOMode we keep the attr of the
 * previous special command.
 */
static unsigned int
ulReadDataFromDirectRegister(TWorkSpace * pWS, RegisterAttribute attr)
{
	unsigned int    ulValue = 0;
	unsigned char  *pBaseAddress, *pIoAddress;
	unsigned short  VGAddress;

	switch (pWS->ucIOMode) {
	case ATTRIB_BYTE_PCI_IO:
		/* 0 */
		pBaseAddress = (unsigned char *) (pWS->softc->BaseMemAddr);
		pIoAddress = pBaseAddress + LfbOffset +
		    BLOCK0_ADDRESS_OF(attr.ucAttrib);
		ulValue = ulReadDataFromIO(pIoAddress, pWS->ucAlignment);
		break;
	case ATTRIB_BYTE_DEST_ARRAY:
		/* 1 */
		ulValue = ulReadDataFromWorkSpace(pWS, attr.ucAttrib,
		    pWS->ucAlignment);
		break;
	case ATTRIB_WORD_MEM_MAP_0:
		/* 4, newly added */
		pBaseAddress = (unsigned char *) (pWS->softc->BaseMemAddr);
		pIoAddress = pBaseAddress + LfbOffset +
		    BLOCK0_ADDRESS_OF(attr.usAttrib);
		ulValue = ulReadDataFromIO(pIoAddress, pWS->ucAlignment);
		break;
	case ATTRIB_WORD_MEM_MAP_1:
		/* 5, newly added */
		pBaseAddress = (unsigned char *) (pWS->softc->BaseMemAddr);
		pIoAddress = pBaseAddress + LfbOffset +
		    BLOCK1_ADDRESS_OF(attr.usAttrib);
		ulValue = ulReadDataFromIO(pIoAddress, pWS->ucAlignment);
		break;
	case ATTRIB_WORD_IO_ADDRESS:
		/* 6 */
		VGAddress = (unsigned short) attr.usAttrib;
		ulValue = ulReadDataFromVGA(pWS, VGAddress, pWS->ucAlignment);
		break;
	case ATTRIB_WORD_BIOS_IMAGE:
		/* 7, not used by driver */
	case ATTRIB_BYTE_DOS_DATA:
		/* 3, not used by driver */
	default:
		break;
	}
	return (ulValue);
}


static unsigned int
ulReadDataFrom(TWorkSpace * pWS, RegisterAttribute attr)
/*
 * main proc.for data acquir.
 */
{
	unsigned int    ulValue = 0;
	switch (pWS->ucSpecialCmd) {
	case INDEXED_REG_ACCESS:
		ulValue = ulReadDataFromIndexedRegister(pWS, attr);
		break;
	case PLL_REG_ACCESS:
		ulValue = ulReadDataFromPLL(pWS, attr);
		break;
	case PCI_CONFIG_ACCESS:
		ulValue = ulReadDataFromPCIConfigSpace(pWS, attr);
		break;
	case DIRECT_REG_ACCESS:
		ulValue = ulReadDataFromDirectRegister(pWS, attr);
		break;
	default:
		break;
	}
	return (ulValue);
}

static void
vWriteDataToVGA(TWorkSpace * pWS, unsigned short IoAddress,
    unsigned char ucAlignment, unsigned int ulValue)
{

	struct atiatom_softc	*softc = pWS->softc;

	unsigned char   ucOffset;
	unsigned long   pointer;

	switch (ucAlignment) {
	case REGX_LOWER_WORD:
	case REGX_MIDDLE_WORD:
	case REGX_UPPER_WORD:
		ucOffset = ucAlignment & 0x03;
		pointer = IoAddress + ucOffset;
		ddi_put16(softc->regs.handle, (void *) pointer,
		    (unsigned short) ulValue);
		break;
	case REGX_DWORD:
		ucOffset = 0;
		pointer = IoAddress + ucOffset;

		ddi_put32(softc->regs.handle, (void *) pointer, ulValue);
		break;
	case REGX_BYTE0:
	case REGX_BYTE1:
	case REGX_BYTE2:
	case REGX_BYTE3:
		ucOffset = ucAlignment & 0x03;
		pointer = IoAddress + ucOffset;

		ddi_put8(softc->regs.handle, (void *) pointer,
		    (unsigned char) ulValue);
		break;
	default:
		break;
	}
}

static void
vWriteDataToIO(unsigned char *pIoAddress, unsigned char ucAlignment,
    unsigned int ulValue)
{
	unsigned char   ucOffset;

	switch (ucAlignment) {
	case REGX_LOWER_WORD:
	case REGX_MIDDLE_WORD:
	case REGX_UPPER_WORD:
		ucOffset = ucAlignment & 0x03;
/* LINTED */
		VideoPortWriteRegisterUshort((unsigned short *)
		    (pIoAddress + ucOffset), (unsigned short) ulValue);
		break;
	case REGX_DWORD:
		ucOffset = 0;
/* LINTED */
		VideoPortWriteRegisterUint((unsigned int *)
		    (pIoAddress + ucOffset), ulValue);
		break;
	case REGX_BYTE0:
	case REGX_BYTE1:
	case REGX_BYTE2:
	case REGX_BYTE3:
		ucOffset = ucAlignment & 0x03;
		VideoPortWriteRegisterUchar((unsigned char *)
		    (pIoAddress + ucOffset), (unsigned char) ulValue);
		break;
	default:
		break;
	}
}

static void
vWriteDataToWorkSpace(TWorkSpace * pWS, unsigned char ucIndex,
    unsigned int ulValue)
{
	unsigned char   ucOffset, ucAlignment, ucArrayOp;

	ucArrayOp = ucIndex & 0xE0;
	/* 7:	5 ARRAY_OP */
	ucAlignment = ucIndex & 0x07;
	/* 2:	0 ALIGNMENT */
	ucIndex = (ucIndex & 0x018) >> 3;
	/* 4:	3 INDEX */
	switch (ucAlignment) {
	case REGX_LOWER_WORD:
	case REGX_MIDDLE_WORD:
	case REGX_UPPER_WORD:
		ucOffset = ucAlignment & 0x03;
		if (ucArrayOp == ARRAY_SUB_OP) {
			pWS->compareFlag = GREATER_THAN;
			pWS->mem_store[ucIndex].usMem[ucOffset] -=
			    (unsigned short) ulValue;
			if (pWS->mem_store[ucIndex].usMem[ucOffset] == 0)
				pWS->compareFlag = EQUAL;
		} else if (ucArrayOp == ARRAY_OR_OP)
			pWS->mem_store[ucIndex].usMem[ucOffset] |=
			    (unsigned short) ulValue;
		else if (ucArrayOp == ARRAY_ADD_OP)
			pWS->mem_store[ucIndex].usMem[ucOffset] +=
			    (unsigned short) ulValue;
		else
			/* ARRAY_WRITE_OP */
			pWS->mem_store[ucIndex].usMem[ucOffset] =
			    (unsigned short) ulValue;
		break;
	case REGX_DWORD:
		ucOffset = 0;
		if (ucArrayOp == ARRAY_SUB_OP) {
			pWS->compareFlag = GREATER_THAN;
			pWS->mem_store[ucIndex].ulMem -= ulValue;
			if (pWS->mem_store[ucIndex].ulMem == 0)
				pWS->compareFlag = EQUAL;
		} else if (ucArrayOp == ARRAY_OR_OP)
			pWS->mem_store[ucIndex].ulMem |= ulValue;
		else if (ucArrayOp == ARRAY_ADD_OP)
			pWS->mem_store[ucIndex].ulMem += ulValue;
		else
			/* ARRAY_WRITE_OP */
			pWS->mem_store[ucIndex].ulMem = ulValue;
		break;
	case REGX_BYTE0:
	case REGX_BYTE1:
	case REGX_BYTE2:
	case REGX_BYTE3:
		ucOffset = ucAlignment & 0x03;
		if (ucArrayOp == ARRAY_SUB_OP) {
			pWS->compareFlag = GREATER_THAN;
			pWS->mem_store[ucIndex].ucMem[ucOffset] -=
			    (unsigned char) ulValue;
			if (pWS->mem_store[ucIndex].ucMem[ucOffset] == 0)
				pWS->compareFlag = EQUAL;
		} else if (ucArrayOp == ARRAY_OR_OP)
			pWS->mem_store[ucIndex].ucMem[ucOffset] |=
			    (unsigned char) ulValue;
		else if (ucArrayOp == ARRAY_ADD_OP)
			pWS->mem_store[ucIndex].ucMem[ucOffset] +=
			    (unsigned char) ulValue;
		else
			/* ARRAY_WRITE_OP */
			pWS->mem_store[ucIndex].ucMem[ucOffset] =
			    (unsigned char) ulValue;
		break;
	default:
		break;
	}
}

static void
vWriteDataToIndexedRegister(TWorkSpace * pWS, RegisterAttribute attr,
    unsigned int ulValue)
{
	unsigned int    ulTemp, ulMask;
	unsigned char  *pIoAddress, *pBaseAddress;
	unsigned long   temp;
	switch (pWS->ucIOMode) {
	case ATTRIB_BYTE_PCI_IO:
		/* 0x00 used */
		pBaseAddress = (unsigned char *) (pWS->softc->BaseMemAddr);
		break;
	case ATTRIB_BYTE_INDEX_BASE_IO:
		/* 0x002 not used */
		temp = pWS->usIOBaseAddress;
		pBaseAddress = (unsigned char *) temp;
		/* won 't work that way */
		break;
	default:
		break;
	}
	ulMask = ulMaskTable[pWS->ucNumberOfIndexBits]
	    << pWS->ucStartBitOfIndex;

	pIoAddress = pBaseAddress + LfbOffset +
	    BLOCK0_ADDRESS_OF(pWS->ucIndexAddress);

	/* ? ? ? ? ? ? ? ? ? */
/* LINTED */
	ulTemp = VideoPortReadRegisterUint((unsigned int *) pIoAddress);

	ulTemp &= ~ulMask;
	ulTemp |= (ulMask & (attr.ucAttrib << pWS->ucStartBitOfIndex));
/* LINTED */
	VideoPortWriteRegisterUint((unsigned int *) pIoAddress, ulTemp);
	/*
	 * DELAY_MICROSECONDS(5);
	 */


	pIoAddress = pBaseAddress + LfbOffset +
	    BLOCK0_ADDRESS_OF(pWS->ucDataAddress);

	vWriteDataToIO(pIoAddress, pWS->ucAlignment, ulValue);
}

/*
 * vIOWriteDataToPLL -
 *	Write data to the chip's phase locked loop registers
 *	(IO-mapped access ???)
 */
static void
vIOWriteDataToPLL(TWorkSpace * pWS, RegisterAttribute attr,
    unsigned int ulValue)
{
	/*
	 * Now refer separately to vgatext softc elements
	 */
	struct atiatom_softc	*softc = pWS->softc;

	unsigned char   temp;
	void	*IOAddrIX, *IOAddrData;
	unsigned char   ucMask, ucTemp;
	volatile uint8_t *regs = (uint8_t *)pWS->softc->registers;

	temp = regs[0x91];
	set_clock_reg(pWS->softc, attr.ucAttrib, ulValue);
	drv_usecwait(100);
	regs[0x91] = temp;

	IOAddrIX = pWS->softc->IOBaseAddr + 0x91;
	IOAddrData = pWS->softc->IOBaseAddr + 0x92;

	/* Now use the explicit atiatom_softc handle */
	ucMask = ddi_get8(softc->regs.handle, IOAddrIX);
	ucTemp = (attr.ucAttrib << 2) | 0x02;

	ddi_put8(softc->regs.handle, IOAddrIX, ucTemp);
	drv_usecwait(100);
	ucTemp = ddi_get8(softc->regs.handle, IOAddrData);
	ddi_put8(softc->regs.handle, IOAddrData, (unsigned char) ulValue);
	drv_usecwait(100);
	ucTemp = ddi_get8(softc->regs.handle, IOAddrData);
	ddi_put8(softc->regs.handle, IOAddrIX, ucMask);
	drv_usecwait(100);
}


/*
 * vWriteDataToPCIConfigSpace -
 *
 *	(Note: Earlier comments from ATI code are):
 * We need 4 arguments:BusDevFcn, offset from the begingin of ConfigSpace,
 * how many bytes and value
 *
 * BusDevFcn = HW_DEVICE_EXTENSION->bus_dev_fcn in case
 * ATTRIB_BYTE_PCI_IO
 *
 * BusDevFcn = pWS->ucIndexOfWS, REGX_UPPER_WORD in case
 * ATTRIB_WORD_BUS_DEV_FCN
 *
 * Offset = attr.ucAttrib(given after operat.command)
 *
 * HowManyBytes = 0, 1, 2 or 3 in pWS->ucAlignment i.e(REG_SET_BITS
 * or << REG_BYTE0 >>) - set by ExecScript
 *
 * General form:
 *	int SetConfigData(unsigned short BusDevFcn,
 *	unsigned char Offset, int HowManyBytes, unsigned int ulValue)
 */
static void
vWriteDataToPCIConfigSpace(TWorkSpace * pWS, RegisterAttribute attr,
    unsigned int ulValue)
{
	int	Len = ACCESS_LEN[pWS->ucAlignment];	/* HowManyBytes */

	switch (pWS->ucIOMode) {
	case ATTRIB_BYTE_PCI_IO:
		SetConfigData(pWS->softc, attr.ucAttrib, Len, ulValue);
		break;

	case ATTRIB_WORD_BUS_DEV_FCN:
		(void) ulReadDataFromWorkSpace(pWS, pWS->ucIndexOfWS,
		    REGX_UPPER_WORD);
		SetConfigData(pWS->softc, attr.ucAttrib, Len, ulValue);
		break;

	default:
		break;
	}
}

static void
vWriteDataToDirectRegister(TWorkSpace * pWS, RegisterAttribute attr,
    unsigned int ulValue)
{
	unsigned char  *pBaseAddress, *pIoAddress;
	unsigned short  IoAddress;

	switch (pWS->ucIOMode) {
	case ATTRIB_BYTE_PCI_IO:
		/* 0 - used */
		pBaseAddress = (unsigned char *) (pWS->softc->BaseMemAddr);
		pIoAddress = pBaseAddress + LfbOffset +
		    BLOCK0_ADDRESS_OF(attr.ucAttrib);
		vWriteDataToIO(pIoAddress, pWS->ucAlignment, ulValue);
		break;
	case ATTRIB_BYTE_DEST_ARRAY:
		/* 1 - used */
		vWriteDataToWorkSpace(pWS, attr.ucAttrib, ulValue);
		break;
	case ATTRIB_WORD_MEM_MAP_0:
		/* 4, newly added */
		pBaseAddress = (unsigned char *) (pWS->softc->BaseMemAddr);

		pIoAddress = pBaseAddress + LfbOffset +
		    BLOCK0_ADDRESS_OF(attr.usAttrib);

		vWriteDataToIO(pIoAddress, pWS->ucAlignment, ulValue);
		break;
	case ATTRIB_WORD_MEM_MAP_1:
		/* 5, newly added */
		pBaseAddress = (unsigned char *)
		    (pWS->softc->BaseMemAddr);
		pIoAddress = pBaseAddress + LfbOffset +
		    BLOCK1_ADDRESS_OF(attr.usAttrib);
		vWriteDataToIO(pIoAddress, pWS->ucAlignment, ulValue);
		break;
	case ATTRIB_WORD_IO_ADDRESS:
		/* 6 - used */
		IoAddress = (unsigned short) attr.usAttrib;
		vWriteDataToVGA(pWS, IoAddress, pWS->ucAlignment, ulValue);
		break;
	case ATTRIB_WORD_BIOS_IMAGE:
		/* 7, not used by driver */
	case ATTRIB_BYTE_DOS_DATA:
		/* 3, not used by driver */
	default:
		break;
	}
}

static void
vWriteDataTo(TWorkSpace * pWS, RegisterAttribute attr, unsigned int ulValue)
/*
 * main write oprnd switch
 */
{
	switch (pWS->ucSpecialCmd) {
		case INDEXED_REG_ACCESS:
		vWriteDataToIndexedRegister(pWS, attr, ulValue);
		break;
	case PLL_REG_ACCESS:	/* vWriteDataToPLL(pWS, attr, ulValue); */
		vIOWriteDataToPLL(pWS, attr, ulValue);
		break;
	case PCI_CONFIG_ACCESS:
		vWriteDataToPCIConfigSpace(pWS, attr, ulValue);
		break;
	case DIRECT_REG_ACCESS:
		vWriteDataToDirectRegister(pWS, attr, ulValue);
		break;
	default:
		break;
	}
}
static void
scriptDelayInUs(TWorkSpace * pWS)
{
	unsigned char   nUs = ucFilterBYTE(pWS);
	/* must be based on 100 ns */
	drv_usecwait(nUs * 100);
}

static void
scriptDelayInMs(TWorkSpace * pWS)
{
	unsigned char   nMs = ucFilterBYTE(pWS);
	drv_usecwait(nMs * 1000);
}

static void
scriptIndexedRegAccess(TWorkSpace * pWS)
/*
 * how to prepare work.env.in case Ind / reg / acc.
 */
{
	unsigned char   ucValue;
	pWS->ucSpecialCmd = INDEXED_REG_ACCESS;
	pWS->ucIOMode = ucFilterBYTE(pWS);
	/* "primary adapter" or "NOT primary only script" */
	pWS->bScriptRunnable = pWS->bPrimaryAdapter ||
	    (!(pWS->ucIOMode & ATTRIB_PRIMARY_ONLY) ? 1 : 0);
	/*
	 * bPrimaryAdapter = 0 in the beg.= >Script is Runn.if
	 * ATTRIB_PRIMARY_ONLY = 0
	 */
	pWS->ucIOMode &= 0x7F;

	ucValue = ucFilterBYTE(pWS);
	/* chete 3 - ija byte */
	pWS->ucNumberOfIndexBits = (ucValue & 0xF0) >> 4;
	pWS->ucStartBitOfIndex = ucValue & 0x0F;
	pWS->ucIndexAddress = ucFilterBYTE(pWS);
	pWS->ucDataAddress = ucFilterBYTE(pWS);

	if (pWS->ucIOMode == ATTRIB_BYTE_INDEX_BASE_IO)
		pWS->usIOBaseAddress = usFilterWORD(pWS);


}

static void
scriptDirectRegAccess(TWorkSpace * pWS)
{
	pWS->ucSpecialCmd = DIRECT_REG_ACCESS;
	pWS->ucIOMode = ucFilterBYTE(pWS);
	/* "primary adapter" or "NOT primary only script" */
	pWS->bScriptRunnable = pWS->bPrimaryAdapter ||
	    (!(pWS->ucIOMode & ATTRIB_PRIMARY_ONLY) ? 1 : 0);
	pWS->ucIOMode &= 0x7F;

}

static void
scriptPciConfigAccess(TWorkSpace * pWS)
{
	unsigned char   ucValue;

	pWS->ucSpecialCmd = PCI_CONFIG_ACCESS;
	pWS->ucIOMode = ucFilterBYTE(pWS);
	/* "primary adapter" or "NOT primary only script" */
	pWS->bScriptRunnable = pWS->bPrimaryAdapter ||
	    (!(pWS->ucIOMode & ATTRIB_PRIMARY_ONLY) ? 1 : 0);
	ucValue = pWS->ucIOMode;

	if (pWS->ucIOMode & ATTRIB_WORD_BUS_DEV_FCN) {
		pWS->ucIOMode = ATTRIB_WORD_BUS_DEV_FCN;
		pWS->ucIndexOfWS = (ucValue & 0x18) | REGX_UPPER_WORD;
		/* */
	} else
		pWS->ucIOMode = ATTRIB_BYTE_PCI_IO;


}

static void
scriptCompareReadValue(TWorkSpace * pWS)
{
	pWS->ucFlags = COMPARE_WITH_NEXTVALUE | COMPARE_ENABLED;

}

static void
scriptCompareReads(TWorkSpace * pWS)
{
	pWS->ucFlags = COMPARE_WITH_NEXTREAD | COMPARE_ENABLED;

}

static void
scriptJumpToNthByte(TWorkSpace * pWS)
{
	unsigned char   ucOffset = ucFilterBYTE(pWS);
	pWS->pucScript = pWS->pucScriptTable + ucOffset;
}

static void
scriptJumpToNthByteIfE(TWorkSpace * pWS)
{
	unsigned char   ucOffset = ucFilterBYTE(pWS);

	if (pWS->compareFlag == EQUAL)
		pWS->pucScript = pWS->pucScriptTable + ucOffset;
}

static void
scriptJumpToNthByteIfNE(TWorkSpace * pWS)
{
	unsigned char   ucOffset = ucFilterBYTE(pWS);

	if (pWS->compareFlag != EQUAL)
		pWS->pucScript = pWS->pucScriptTable + ucOffset;
}

static void
scriptJumpToNthByteIfLT(TWorkSpace * pWS)
{
	unsigned char   ucOffset = ucFilterBYTE(pWS);

	if (pWS->compareFlag == LESS_THAN)
		pWS->pucScript = pWS->pucScriptTable + ucOffset;
}

static void
scriptJumpToNthByteIfGT(TWorkSpace * pWS)
{
	unsigned char   ucOffset = ucFilterBYTE(pWS);

	if (pWS->compareFlag == GREATER_THAN)
		pWS->pucScript = pWS->pucScriptTable + ucOffset;
}

static void
scriptJumpToNthByteIfLTE(TWorkSpace * pWS)
{
	unsigned char   ucOffset = ucFilterBYTE(pWS);

	if (pWS->compareFlag != GREATER_THAN)
		pWS->pucScript = pWS->pucScriptTable + ucOffset;
}

static void
scriptJumpToNthByteIfGTE(TWorkSpace * pWS)
{
	unsigned char   ucOffset = ucFilterBYTE(pWS);

	if (pWS->compareFlag != LESS_THAN)
		pWS->pucScript = pWS->pucScriptTable + ucOffset;
}

static void
scriptCallFuncAtNthByte(TWorkSpace * pWS)
{
	/*
	 * create new context for switch to another script table
	 */
	TWorkSpace	*pnewWS;
	unsigned char   ucOffset = ucFilterBYTE(pWS);
	/*
	 * DEBUG_OUTPUT_FOR_CALL
	 */
	pnewWS = (TWorkSpace *) kmem_alloc(sizeof (TWorkSpace), KM_SLEEP);
	/* added */

	pnewWS->softc = pWS->softc;
	VideoPortMoveMemory(pnewWS, pWS, sizeof (TWorkSpace));
	/* pointers are reworked to Punsigned char ? ! */
	pnewWS->pucScript = pWS->pucScriptTable + ucOffset;
	pnewWS->pucScriptTable = pWS->pucScriptTable;
	pnewWS->pucBIOSImage = pWS->pucBIOSImage;
	pnewWS->ucFlags = pWS->ucFlags;
	pnewWS->compareFlag = pWS->compareFlag;
	pnewWS->bPrimaryAdapter = 0;
	pnewWS->bPrimaryAdapter = 1;
	pnewWS->bEndOfProcessing = 0;
	ExecuteScript(pnewWS);
	pWS->compareFlag = pnewWS->compareFlag;
	/* inherited from called function */
	pWS->pucSeqNextScript = NULL;
	/*
	 * for no debug out
	 */
	kmem_free(pnewWS, sizeof (TWorkSpace));
	/* added */
}

static void
scriptJumpRelative(TWorkSpace * pWS)
{
	unsigned char   ucOffset = ucFilterBYTE(pWS);

	if (ucOffset & 0x80)
		pWS->pucScript -= (ucOffset & 0x7F);
	else
		pWS->pucScript += ucOffset;
}

static void
scriptJumpRelativeIfE(TWorkSpace * pWS)
{
	unsigned char   ucOffset = ucFilterBYTE(pWS);

	if (pWS->compareFlag == EQUAL) {
		if (ucOffset & 0x80)
			pWS->pucScript -= (ucOffset & 0x7F);
		else
			pWS->pucScript += ucOffset;
	}
}

static void
scriptJumpRelativeIfNE(TWorkSpace * pWS)
{
	unsigned char   ucOffset = ucFilterBYTE(pWS);

	if (pWS->compareFlag != EQUAL) {
		if (ucOffset & 0x80)
			pWS->pucScript -= (ucOffset & 0x7F);
		else
			pWS->pucScript += ucOffset;
	}
}

static void
scriptJumpRelativeIfLT(TWorkSpace * pWS)
{
	unsigned char   ucOffset = ucFilterBYTE(pWS);

	if (pWS->compareFlag == LESS_THAN) {
		if (ucOffset & 0x80)
			pWS->pucScript -= (ucOffset & 0x7F);
		else
			pWS->pucScript += ucOffset;
	}
}

static void
scriptJumpRelativeIfGT(TWorkSpace * pWS)
{
	unsigned char   ucOffset = ucFilterBYTE(pWS);

	if (pWS->compareFlag == GREATER_THAN) {
		if (ucOffset & 0x80)
			pWS->pucScript -= (ucOffset & 0x7F);
		else
			pWS->pucScript += ucOffset;
	}
}

static void
scriptJumpRelativeIfLTE(TWorkSpace * pWS)
{
	unsigned char   ucOffset = ucFilterBYTE(pWS);

	if (pWS->compareFlag != GREATER_THAN) {
		if (ucOffset & 0x80)
			pWS->pucScript -= (ucOffset & 0x7F);
		else
			pWS->pucScript += ucOffset;
	}
}

static void
scriptJumpRelativeIfGTE(TWorkSpace * pWS)
{
	unsigned char   ucOffset = ucFilterBYTE(pWS);

	if (pWS->compareFlag != LESS_THAN) {
		if (ucOffset & 0x80)
			pWS->pucScript -= (ucOffset & 0x7F);
		else
			pWS->pucScript += ucOffset;
	}
}

static void
scriptCallFuncAtRelative(TWorkSpace * pWS)
{
	/* create new context for switch to another script table */
	TWorkSpace	*pnewWS;
	unsigned char   ucOffset = ucFilterBYTE(pWS);
	unsigned char  *pucScript = pWS->pucScript;

	pnewWS = (TWorkSpace *) kmem_alloc(sizeof (TWorkSpace), KM_SLEEP);
	/* added */

	pnewWS->softc = pWS->softc;

	if (ucOffset & 0x80)
		pucScript -= (ucOffset & 0x7F);
	else
		pucScript += ucOffset;

	VideoPortMoveMemory(pnewWS, pWS, sizeof (TWorkSpace));
	pnewWS->pucScript = pucScript;
	pnewWS->pucScriptTable = pWS->pucScriptTable;
	pnewWS->pucBIOSImage = pWS->pucBIOSImage;
	pnewWS->ucFlags = pWS->ucFlags;
	pnewWS->compareFlag = pWS->compareFlag;
	pnewWS->bPrimaryAdapter = 1;
	pnewWS->bEndOfProcessing = 0;

	ExecuteScript(pnewWS);
	pWS->compareFlag = pnewWS->compareFlag;
	/* inherited from called function */
	pWS->pucSeqNextScript = NULL;
	/*
	 * for no debug out
	 */
	kmem_free(pnewWS, sizeof (TWorkSpace));
	/* * added  */
}

static void
scriptCallFunctionReturn(TWorkSpace * pWS)
{
	pWS->bEndOfProcessing = 1;	/* DumpPll(); */

	DEBUG_CALL_RETURN
}

static void
scriptJumpToTable(TWorkSpace * pWS)
/* not implemented prop.- when return */
{
	unsigned short  usOffset = usFilterWORD(pWS);
	pWS->pucScript = pWS->pucScriptTable = pWS->pucBIOSImage + usOffset;
}
static void
scriptCallTable(TWorkSpace * pWS)
{				/* implemented as  CallTableAtBiosOffset */
	/* create new context for switch to another script table */
	TWorkSpace	*pnewWS;
	unsigned short  usOffset = usFilterWORD(pWS);
	pnewWS = (TWorkSpace *) kmem_alloc(sizeof (TWorkSpace), KM_SLEEP);

	pnewWS->softc = pWS->softc;

	/* DEBUG_OUTPUT_FOR_CALL */

	VideoPortMoveMemory(pnewWS, pWS, sizeof (TWorkSpace));
	pnewWS->pucBIOSImage = pWS->pucBIOSImage;
	pnewWS->pucScript = pnewWS->pucScriptTable =
	    pnewWS->pucBIOSImage + usOffset;
	pnewWS->ucFlags = pWS->ucFlags;
	pnewWS->compareFlag = pWS->compareFlag;
	pnewWS->bPrimaryAdapter = 0;
	pnewWS->bPrimaryAdapter = 1;
	pnewWS->bEndOfProcessing = 0;
	ExecuteScript(pnewWS);
	pWS->compareFlag = pnewWS->compareFlag;
	/* inherited from called function */
	pWS->pucSeqNextScript = NULL;
	/*
	 * for no debug out
	 */
	kmem_free(pnewWS, sizeof (TWorkSpace));
	/* added */
}

static void
scriptCallTableReturn(TWorkSpace * pWS)
{
	pWS->bEndOfProcessing = 1;

	DEBUG_CALL_RETURN
}

static void
scriptPllRegAccess(TWorkSpace * pWS)
{
	unsigned char   ucValue;

	pWS->ucSpecialCmd = PLL_REG_ACCESS;
	pWS->ucIOMode = ucFilterBYTE(pWS);
	/* "primary adapter" or "NOT primary only script" */
	pWS->bScriptRunnable = pWS->bPrimaryAdapter ||
	    (!(pWS->ucIOMode & ATTRIB_PRIMARY_ONLY) ? 1 : 0);
	pWS->ucIOMode &= 0x7F;

	ucValue = ucFilterBYTE(pWS);
	pWS->ucNumberOfIndexBits = (ucValue & 0xF0) >> 4;
	pWS->ucStartBitOfIndex = ucValue & 0x0F;
	pWS->ucIndexAddress = ucFilterBYTE(pWS);
	pWS->ucDataAddress = ucFilterBYTE(pWS);

	pWS->usAndMask = usFilterWORD(pWS);
	pWS->usOrMask = usFilterWORD(pWS);

}

static void
scriptCallAsmFunction(TWorkSpace * pWS)
{
	(void) usFilterWORD(pWS);
}

static void
scriptCallFunctionAtBiosOffset(TWorkSpace * pWS)
{
	scriptCallTable(pWS);
	/*
	 * do the same job ? ? ? -no difference
	 */
}

static void
scriptTestReadAgainstValue(TWorkSpace * pWS)
{
	pWS->ucFlags = TEST_WITH_NEXTVALUE | TEST_ENABLED;
	/* Those flags are not used later */

}

static void
scriptTestTwoReads(TWorkSpace * pWS)
{
	pWS->ucFlags = TEST_WITH_NEXTREAD | TEST_ENABLED;

}

static void
scriptClearZeroFlag(TWorkSpace * pWS)
{
	pWS->compareFlag = GREATER_THAN;
	/*
	 * not EQUAL
	 */
}
static void
scriptSetZeroFlag(TWorkSpace * pWS)
{
	pWS->compareFlag = EQUAL;

}
static void
scriptTestMemConfig(TWorkSpace * pWS)
{
	unsigned char   ucAlignment, ucIndex;
	unsigned int    ulMemPages;

	ucIndex = ucFilterBYTE(pWS);
	ucAlignment = ucIndex & 0x07;

	ulMemPages = ulReadDataFromWorkSpace(pWS, ucIndex, ucAlignment);
	if (bTestMemConfig(pWS->softc, ulMemPages)) {
		pWS->compareFlag = EQUAL;
	} else {
		pWS->compareFlag = GREATER_THAN;
	}

}
static void
scriptBeep()
{
}
static void
scriptNull()
{
}
static void
scriptTestStop()
{
}
static void
scriptPostPort80()
{
}
static void
scriptTracePort80()
{
}
static void
scriptTestBreak()
{
}

/* RegisterCmd Script */
/* REG_CLEAR */
static void
scriptClearCmd(TWorkSpace * pWS)
{
	RegisterAttribute destAttr;

	vScanAttribute(pWS, &destAttr);
	if (pWS->bScriptRunnable)
		vWriteDataTo(pWS, destAttr, 0);
}

/* REG_WRITE */
static void
scriptWriteCmd(TWorkSpace * pWS)
{
	RegisterAttribute destAttr;
	unsigned int    ulValue;

	vScanAttribute(pWS, &destAttr);
	ulValue = ulGetValue(pWS);


	if (pWS->bScriptRunnable)
		vWriteDataTo(pWS, destAttr, ulValue);
}

/* REG_SKEW */
static void
scriptSkewCmd(TWorkSpace * pWS)
{
	RegisterAttribute destAttr;

	vScanAttribute(pWS, &destAttr);
	(void) ulGetValue(pWS);
	(void) ulGetValue(pWS);
	(void) ulGetValue(pWS);
}

/* REG_READ_AND_COMPARE */
static void
scriptReadCmd(TWorkSpace * pWS)
{
	RegisterAttribute srcAttr;
	unsigned int    ulValue;

	vScanAttribute(pWS, &srcAttr);
	if (pWS->bScriptRunnable) {
		ulValue = ulReadDataFrom(pWS, srcAttr);
		if (pWS->ucFlags & COMPARE_ENABLED) {
			if (pWS->ucFlags & COMPARE_WITH_NEXTVALUE)
				vCompareWithNextValue(pWS, ulValue);
			else if (pWS->ucFlags & COMPARE_WITH_NEXTREAD)
				vComparePending(pWS, ulValue);
			else if (pWS->ucFlags & COMPARE_PENDING)
				vCompareWithNextRead(pWS, ulValue);
		} else if (pWS->ucFlags & TEST_ENABLED) {
			if (pWS->ucFlags & TEST_WITH_NEXTVALUE)
				vTestWithNextValue(pWS, ulValue);
			else if (pWS->ucFlags & TEST_WITH_NEXTREAD)
				vTestPending(pWS, ulValue);
			else if (pWS->ucFlags & TEST_PENDING)
				vTestWithNextRead(pWS, ulValue);
		}
	} else if (pWS->ucFlags & COMPARE_WITH_NEXTVALUE)
		/* not runnable */
		ulValue = ulGetValue(pWS);
	/* just skip VALUE, so we can scan next correctly */
}

/* READ_TO_MEM */
static void
scriptReadToMem(TWorkSpace * pWS)
{
	RegisterAttribute srcAttr;
	unsigned char   ucIndex;
	unsigned int    ulValue;

	vScanAttribute(pWS, &srcAttr);
	ucIndex = ucFilterBYTE(pWS);
	if (pWS->bScriptRunnable) {
		ulValue = ulReadDataFrom(pWS, srcAttr);
		vWriteDataToWorkSpace(pWS, ucIndex, ulValue);
	}
}

/* READ_AND_COMPARE_FROM_MEM */
static void
scriptReadAndCompareFromMem(TWorkSpace *pWS)
{
	unsigned char   ucIndex = ucFilterBYTE(pWS);
	unsigned int    ulValue;

	if (pWS->bScriptRunnable) {
		ulValue = ulReadDataFromWorkSpace(pWS, ucIndex,
		    pWS->ucAlignment);
		if (pWS->ucFlags & COMPARE_WITH_NEXTVALUE)
			vCompareWithNextValue(pWS, ulValue);
		else if (pWS->ucFlags & COMPARE_WITH_NEXTREAD)
			vComparePending(pWS, ulValue);
		else if (pWS->ucFlags & COMPARE_PENDING)
			vCompareWithNextRead(pWS, ulValue);
	} else if (pWS->ucFlags & COMPARE_WITH_NEXTVALUE)
		ulValue = ulGetValue(pWS);
	/* just skip VALUE, so we can scan next correctly */
}


/* WRITE_FROM_MEM */
static void
scriptWriteFromMem(TWorkSpace * pWS)
{
	RegisterAttribute destAttr;
	unsigned int    ulValue;
	unsigned char   ucIndex = ucFilterBYTE(pWS);
	vScanAttribute(pWS, &destAttr);

	if (pWS->bScriptRunnable) {
		ulValue = ulReadDataFromWorkSpace(pWS, ucIndex,
		    pWS->ucAlignment);
		vWriteDataTo(pWS, destAttr, ulValue);
	}
}

/* MASK_WRITE_FROM_MEM */
static void
scriptMaskWriteFromMem(TWorkSpace * pWS)
{
	RegisterAttribute destAttr;
	unsigned int    ulAndMask, ulOrMask, ulValue;
	unsigned char   ucIndex;

	ucIndex = ucFilterBYTE(pWS);
	vScanAttribute(pWS, &destAttr);
	ulAndMask = ulGetValue(pWS);
	ulOrMask = ulGetValue(pWS);

	if (pWS->bScriptRunnable) {
		ulValue = ulReadDataFromWorkSpace(pWS, ucIndex,
		    pWS->ucAlignment);
		CLEARBITS(ulValue, ulAndMask);
		SETBITS(ulValue, ulOrMask);
		vWriteDataTo(pWS, destAttr, ulValue);
	}
}

/* SET_BITS_FROM_MEM */
static void
scriptSetBitsFromMem(TWorkSpace *pWS)
{
	RegisterAttribute destAttr;
	unsigned int    ulOrMask, ulValue;

	unsigned char   ucIndex = ucFilterBYTE(pWS);
	vScanAttribute(pWS, &destAttr);
	ulOrMask = ulGetValue(pWS);

	if (pWS->bScriptRunnable) {
		ulValue = ulReadDataFromWorkSpace(pWS, ucIndex,
		    pWS->ucAlignment);
		SETBITS(ulValue, ulOrMask);
		vWriteDataTo(pWS, destAttr, ulValue);
	}
}

/* CLEAR_BITS_FROM_MEM */
static void
scriptClearBitsFromMem(TWorkSpace * pWS)
{
	RegisterAttribute destAttr;
	unsigned int    ulAndMask, ulValue;

	unsigned char   ucIndex;
	ucIndex = ucFilterBYTE(pWS);
	vScanAttribute(pWS, &destAttr);
	ulAndMask = ulGetValue(pWS);

	if (pWS->bScriptRunnable) {
		ulValue = ulReadDataFromWorkSpace(pWS, ucIndex,
		    pWS->ucAlignment);
		CLEARBITS(ulValue, ulAndMask);
		vWriteDataTo(pWS, destAttr, ulValue);
	}
}


/* REG_MASK_WRITE */
static void
scriptMaskWriteCmd(TWorkSpace * pWS)
{
	RegisterAttribute attr;
	unsigned int    ulAndMask, ulOrMask, ulValue;

	vScanAttribute(pWS, &attr);
	ulAndMask = ulGetValue(pWS);
	ulOrMask = ulGetValue(pWS);

	if (pWS->bScriptRunnable) {
		ulValue = ulReadDataFrom(pWS, attr);
		CLEARBITS(ulValue, ulAndMask);
		SETBITS(ulValue, ulOrMask);
		vWriteDataTo(pWS, attr, ulValue);
	}
}

/* REG_SET_BITS */
static void
scriptSetBitsCmd(TWorkSpace * pWS)
{
	RegisterAttribute attr;
	unsigned int    ulOrMask, ulValue;
	vScanAttribute(pWS, &attr);
	ulOrMask = ulGetValue(pWS);

	if (pWS->bScriptRunnable) {
		ulValue = ulReadDataFrom(pWS, attr);
		SETBITS(ulValue, ulOrMask);
		vWriteDataTo(pWS, attr, ulValue);
	}
}

/* REG_CLEAR_BITS */
static void
scriptClearBitsCmd(TWorkSpace * pWS)
{
	RegisterAttribute attr;
	unsigned int    ulAndMask, ulValue;

	vScanAttribute(pWS, &attr);
	ulAndMask = ulGetValue(pWS);

	if (pWS->bScriptRunnable) {
		ulValue = ulReadDataFrom(pWS, attr);
		CLEARBITS(ulValue, ulAndMask);
		vWriteDataTo(pWS, attr, ulValue);
	}
}

/* REG_BATCH_MASK_WRITE */
static void
scriptBatchMaskWriteCmd(TWorkSpace * pWS)
{
	unsigned char   ucCount = ucFilterBYTE(pWS);
	while (ucCount > 0) {
		scriptMaskWriteCmd(pWS);
		ucCount--;
	}
	/* not the same */

}

/* REG_BATCH_WRITE */
static void
scriptBatchWriteCmd(TWorkSpace * pWS)
{

	unsigned char   ucCount = ucFilterBYTE(pWS);
	while (ucCount > 0) {
		scriptWriteCmd(pWS);
		ucCount--;
	}

}

/* REG_BATCH_CLEAR */
static void
scriptBatchClearCmd(TWorkSpace * pWS)
{
	unsigned char   ucCount = ucFilterBYTE(pWS);

	while (ucCount > 0) {
		scriptClearCmd(pWS);
		ucCount--;
	}
}
static void
SetupScriptInitEntry(struct atiatom_softc *softc)
{
	unsigned short  usOffset;
	unsigned char  *pucImage, *pucBaseImage;
	unsigned short *pusImage;
	/*
	 * set to NULL to make sure that the computer will not crash if no
	 * table available
	 */
	softc->pvInitPll = NULL;
	softc->pvInitExtendedRegisters = NULL;
	softc->pvInitMemory = NULL;

	pucBaseImage = pucImage = (unsigned char *) softc->pvRomImage;
/* LINTED */
	pusImage = (unsigned short *) (pucImage + 0x78);
	usOffset = *pusImage;
	pucImage += usOffset;

	if (pucImage[4] < 5) {
		return;
	}
/* LINTED */
	pusImage = (unsigned short *) (pucImage + 0x20);
	usOffset = *pusImage;
	softc->pvInitPll = (void *) (pucBaseImage + usOffset);

/* LINTED */
	pusImage = (unsigned short *) (pucImage + 0x22);
	usOffset = *pusImage;
	softc->pvInitExtendedRegisters = (void *) (pucBaseImage + usOffset);

/* LINTED */
	pusImage = (unsigned short *) (pucImage + 0x24);
	usOffset = *pusImage;
	softc->pvInitMemory = (void *) (pucBaseImage + usOffset);
	/* return (1); */
}


/*
 * ExecuteScript -
 *     Entry point for every call to execute specific function, needs
 * only IP (instruction pointer?).
 * Base cycle - reads the byte, assuming it is an opcode and transfers
 * control to the procedure contained in the opcode dispatch table:
 * either SpecialCmdTable[cmd] or RegisterCmdTable[cmd]
 * depending on the type of opcode.
 *
 * Stops if pWS->bEndOfProcessing = 1
 */
static void
ExecuteScript(TWorkSpace * pWS)
/*
 */

{
	while (!pWS->bEndOfProcessing) {
		unsigned char   cmd;

		cmd = ucFilterBYTE(pWS);

		if (IS_ENDOFTABLE(cmd)) {
			pWS->bEndOfProcessing = 1;
		} else if (IS_SPECIALCMD(cmd)) {
			cmd &= 0x7F;
			if (cmd < NumberOfSpecialCmd)
				(*SpecialCmdTable[cmd]) (pWS);
		} else {
			pWS->ucAlignment = cmd & 0x07;
			cmd >>= 3;
			if (cmd < NumberOfRegisterCmd)
				(*RegisterCmdTable[cmd]) (pWS);

		}
	}
}
