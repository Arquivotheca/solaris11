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
 * mi2cv.c is the nexus driver for the Mentor Graphics MI2CV
 * I2C controller. It supports interrupt mode and polled mode operation.
 * MI2CV can operate in either master and slave mode, and multi-master;
 * we'll only implement (single) master.
 */

#include <sys/types.h>
#include <sys/conf.h>
#include <sys/file.h>
#include <sys/open.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/sunndi.h>
#include <sys/modctl.h>
#include <sys/stat.h>
#include <sys/kmem.h>
#include <sys/machsystm.h>
#include <sys/archsystm.h>
#include <sys/platform_module.h>
#include <sys/atomic.h>

#include <sys/i2c/clients/i2c_client.h>
#include <sys/i2c/misc/i2c_svc.h>
#include <sys/i2c/misc/i2c_svc_impl.h>

#include <sys/i2c/nexus/mi2cv.h>

#include <sys/note.h>

/* prototypes */

static int mi2cv_attach(dev_info_t *dip, ddi_attach_cmd_t cmd);
static int mi2cv_detach(dev_info_t *dip, ddi_detach_cmd_t cmd);
static int mi2cv_doattach(dev_info_t *dip);
static void mi2cv_resume(dev_info_t *dip);
static void mi2cv_suspend(dev_info_t *dip);
static void mi2cv_dodetach(dev_info_t *dip);
static int mi2cv_open(dev_t *devp, int flag, int otyp, cred_t *cred_p);
static int mi2cv_close(dev_t dev, int flag, int otyp, cred_t *cred_p);
static int mi2cv_ioctl(dev_t, int, intptr_t, int, cred_t *, int *);
static int mi2cv_bus_ctl(dev_info_t *dip, dev_info_t *rdip,
	ddi_ctl_enum_t op, void *arg, void *result);
static void mi2cv_error(uint8_t status, mi2cv_t *i2c);
static int mi2cv_initchild(dev_info_t *cdip);
static void mi2cv_uninitchild(dev_info_t *cdip);
static void mi2cv_reportdev(dev_info_t *dip, dev_info_t *rdip);
static uint_t mi2cv_intr(caddr_t arg);
static void mi2cv_init(mi2cv_t *i2c);
static void mi2cv_set_ccr(mi2cv_t *i2c, uint8_t nvalue, uint8_t mvalue);
static void mi2cv_set_cntr(mi2cv_t *i2c, uint8_t cmd);
static uint8_t mi2cv_get_cntr(mi2cv_t *i2c);
static void mi2cv_acquire(mi2cv_t *i2c, dev_info_t *dip, i2c_transfer_t *tp);
static int mi2cv_process(mi2cv_t *i2c);
static void mi2cv_reset(mi2cv_t *i2c);
static int mi2cv_dip_to_addr(dev_info_t *dip);
static void mi2cv_do_polled_io(mi2cv_t *i2c);
static void mi2cv_release(mi2cv_t *i2c);
static void mi2cv_take_over(mi2cv_t *i2c, dev_info_t *dip,
    i2c_transfer_t *tp, kcondvar_t **waiter, int *saved_mode);
static void mi2cv_give_up(mi2cv_t *i2c, kcondvar_t *waiter, int saved_mode);

int mi2cv_transfer(dev_info_t *dip, i2c_transfer_t *tp);

static struct bus_ops mi2cv_busops = {
	BUSO_REV,
	nullbusmap,			/* bus_map */
	NULL,				/* bus_get_intrspec */
	NULL,				/* bus_add_intrspec */
	NULL,				/* bus_remove_intrspec */
	NULL,				/* bus_map_fault */
	ddi_no_dma_map,			/* bus_dma_map */
	ddi_no_dma_allochdl,		/* bus_dma_allochdl */
	ddi_no_dma_freehdl,		/* bus_dma_freehdl */
	ddi_no_dma_bindhdl,		/* bus_ma_bindhdl */
	ddi_no_dma_unbindhdl,		/* bus_unbindhdl */
	ddi_no_dma_flush,		/* bus_dma_flush */
	ddi_no_dma_win,			/* bus_dma_win */
	ddi_no_dma_mctl,		/* bus_dma_ctl */
	mi2cv_bus_ctl,			/* bus_ctl */
	ddi_bus_prop_op,		/* bus_prop_op */
	NULL,				/* bus_get_eventcookie */
	NULL,				/* bus_add_eventcall */
	NULL,				/* bus_remove_eventcall */
	NULL,				/* bus_post_event */
	0,				/* bus_intr_ctl */
	0,				/* bus_config		*/
	0,				/* bus_unconfig		*/
	0,				/* bus_fm_init		*/
	0,				/* bus_fm_fini		*/
	0,				/* bus_fm_access_enter	*/
	0,				/* bus_fm_access_exit	*/
	0,				/* bus_power		*/
	i_ddi_intr_ops			/* bus_intr_op		*/
};

struct cb_ops mi2cv_cb_ops = {
	mi2cv_open,		/* open */
	mi2cv_close,		/* close */
	nodev,			/* strategy */
	nodev,			/* print */
	nodev,			/* dump */
	nodev,			/* read */
	nodev,			/* write */
	mi2cv_ioctl,		/* ioctl */
	nodev,			/* devmap */
	nodev,			/* mmap */
	nodev,			/* segmap */
	nochpoll,		/* poll */
	ddi_prop_op,		/* cb_prop_op */
	0,			/* streamtab  */
	D_MP | D_NEW		/* Driver compatibility flag */
};

static struct dev_ops mi2cv_ops = {
	DEVO_REV,
	0,
	ddi_getinfo_1to1,
	nulldev,
	nulldev,
	mi2cv_attach,
	mi2cv_detach,
	nodev,
	&mi2cv_cb_ops,
	&mi2cv_busops,
	NULL,
	ddi_quiesce_not_supported,	/* devo_quiesce */
};

static struct modldrv modldrv = {
	&mod_driverops,		/* Type of module. This one is a driver */
	"MI2CV Nexus Driver",	/* Name of the module. */
	&mi2cv_ops,		/* driver ops */
};

static struct modlinkage modlinkage = {
	MODREV_1,
	&modldrv,
	NULL
};

/*
 * mi2cv soft state
 */
static void	*mi2cv_state;

i2c_nexus_reg_t mi2cv_regvec = {
	I2C_NEXUS_REV,
	mi2cv_transfer,
};

/*
 * The "interrupt_priorities" property is how a driver can specify a SPARC
 * PIL level to associate with each of its interrupt properties.  Most
 * self-identifying busses have a better mechanism for managing this, but I2C
 * doesn't.
 */
int	mi2cv_pil = MI2CV_PIL;

int
_init(void)
{
	int status;

	status = ddi_soft_state_init(&mi2cv_state, sizeof (mi2cv_t),
	    MI2CV_INITIAL_SOFT_SPACE);

	if (status != 0) {

		return (status);
	}

	if ((status = mod_install(&modlinkage)) != 0) {
		ddi_soft_state_fini(&mi2cv_state);
	}

	return (status);
}

int
_fini(void)
{
	int status;

	if ((status = mod_remove(&modlinkage)) == 0) {
		ddi_soft_state_fini(&mi2cv_state);
	}

	return (status);
}

/*
 * The loadable-module _info(9E) entry point
 */
int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

static int
mi2cv_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	switch (cmd) {
	case DDI_ATTACH:

		return (mi2cv_doattach(dip));
	case DDI_RESUME:
		mi2cv_resume(dip);

		return (DDI_SUCCESS);
	default:

		return (DDI_FAILURE);
	}
}

/*
 * mi2cv_setup_regs() is called to map in registers specific to
 * the mi2cv.
 */
static int
mi2cv_setup_regs(dev_info_t *dip, mi2cv_t *i2c)
{
	ddi_device_acc_attr_t attr;

	attr.devacc_attr_version = DDI_DEVICE_ATTR_V0;
	attr.devacc_attr_endian_flags = DDI_STRUCTURE_BE_ACC;
	attr.devacc_attr_dataorder = DDI_STRICTORDER_ACC;

	/*
	 * Set mi2cv_regs to be the start of the register array from the
	 * OBP node.
	 */

	return (ddi_regs_map_setup(dip, 0, (caddr_t *)&i2c->mi2cv_regs, 0, 0,
	    &attr, &i2c->mi2cv_rhandle));
}

static int
mi2cv_doattach(dev_info_t *dip)
{
	mi2cv_t *i2c;
	int instance = ddi_get_instance(dip);
	int status = DDI_SUCCESS;

	/*
	 * Allocate soft state structure.
	 */
	if (ddi_soft_state_zalloc(mi2cv_state, instance) != DDI_SUCCESS) {

		return (DDI_FAILURE);
	}

	/* Load the soft state stuct into i2c */
	i2c = (mi2cv_t *)ddi_get_soft_state(mi2cv_state, instance);

	/* put the dip for this device into the soft state struct */
	i2c->mi2cv_dip = dip;

	/* put the device name into the soft state struct */
	(void) snprintf(i2c->mi2cv_name, sizeof (i2c->mi2cv_name),
	    "%s_%d", ddi_node_name(dip), instance);

	/*
	 * Check to see if the "interrupt-priorities" property is in the OBP
	 * node.  If not then create the property our mi2cv_pil definition.
	 */
	if (ddi_prop_exists(DDI_DEV_T_ANY, dip,
	    DDI_PROP_NOTPROM | DDI_PROP_DONTPASS,
	    "interrupt-priorities") != 1) {
		(void) ddi_prop_create(DDI_DEV_T_NONE, dip, DDI_PROP_CANSLEEP,
		    "interrupt-priorities", (caddr_t)&mi2cv_pil,
		    sizeof (mi2cv_pil));
		/* set in our attach flags that we have created a property */
		i2c->mi2cv_attachflags |= PROP_CREATE;
	}

	/* Initialize our condition variable */
	cv_init(&i2c->mi2cv_cv, NULL, CV_DRIVER, NULL);

	/* set up our soft state struct with the mi2cv registers */
	if (mi2cv_setup_regs(dip, i2c) != DDI_SUCCESS) {
		mi2cv_dodetach(dip);

		return (DDI_FAILURE);
	}

	/* set in our attach flags that we have set up the registers */
	i2c->mi2cv_attachflags |= SETUP_REGS;

	cv_init(&i2c->mi2cv_icv, NULL, CV_DRIVER, NULL);
	i2c->mi2cv_mode = MI2CV_POLL_MODE;

	/*
	 * Check to see if the "poll-mode" property exists in the OBP node.
	 * If not then we set up our interrupt information.
	 */
	if (ddi_prop_exists(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS |
	    DDI_PROP_CANSLEEP, "poll-mode") != 1) {

		status = ddi_get_iblock_cookie(dip, 0, &i2c->mi2cv_icookie);
		if (status == DDI_SUCCESS) {
			mutex_init(&i2c->mi2cv_imutex, NULL, MUTEX_DRIVER,
			    (void *)i2c->mi2cv_icookie);

			if (ddi_add_intr(dip, 0, NULL, NULL, mi2cv_intr,
			    (caddr_t)i2c) == DDI_SUCCESS) {
				i2c->mi2cv_attachflags |= IMUTEX | ADD_INTR;
				i2c->mi2cv_mode = MI2CV_INTR_MODE;
			} else {
				mutex_destroy(&i2c->mi2cv_imutex);
				cmn_err(CE_WARN, "%s failed to add interrupt.",
				    i2c->mi2cv_name);
			}
		} else {
			cmn_err(CE_WARN, "%s failed to retrieve iblock cookie.",
			    i2c->mi2cv_name);
		}
	}

	/*
	 * For polled mode, still initialize a mutex
	 */
	if (i2c->mi2cv_mode == MI2CV_POLL_MODE) {
		mutex_init(&i2c->mi2cv_imutex, NULL, MUTEX_DRIVER, NULL);
		i2c->mi2cv_attachflags |= IMUTEX;
		cmn_err(CE_WARN,
		    "%s operating in POLL MODE only\n", i2c->mi2cv_name);
	}

	/*
	 * Register this device with the i2c_nexus driver so that when it
	 * gets a request from a child device it will goto mi2cv_transfer.
	 */
	i2c_nexus_register(dip, &mi2cv_regvec);
	i2c->mi2cv_attachflags |= NEXUS_REGISTER;

	if (ddi_create_minor_node(dip, "devctl", S_IFCHR, instance,
	    DDI_NT_NEXUS, 0) == DDI_SUCCESS) {
		i2c->mi2cv_attachflags |= MINOR_NODE;
		mi2cv_init(i2c);
		i2c->mi2cv_nexus_dip = dip;
		i2c->mi2cv_open = 0;

		return (DDI_SUCCESS);
	} else {
		mi2cv_dodetach(dip);

		return (DDI_FAILURE);
	}
}

/*
 * mi2cv_suspend() is called before the system suspends.  Existing
 * transfer in progress or waiting will complete, but new transfers are
 * effectively blocked by "acquiring" the bus.
 */
static void
mi2cv_suspend(dev_info_t *dip)
{
	mi2cv_t *i2c;
	int instance;

	instance = ddi_get_instance(dip);
	i2c = (mi2cv_t *)ddi_get_soft_state(mi2cv_state, instance);
	mi2cv_acquire(i2c, NULL, NULL);
}

/*
 * mi2cv_resume() is called when the system resumes from CPR.  It releases
 * the hold that was placed on the i2c bus, which allows any real
 * transfers to continue.
 */
static void
mi2cv_resume(dev_info_t *dip)
{
	mi2cv_t *i2c;
	int instance;

	instance = ddi_get_instance(dip);
	i2c = (mi2cv_t *)ddi_get_soft_state(mi2cv_state, instance);
	mi2cv_release(i2c);
	mi2cv_init(i2c);
}

static void
mi2cv_dodetach(dev_info_t *dip)
{
	mi2cv_t *i2c;
	int instance = ddi_get_instance(dip);

	i2c = (mi2cv_t *)ddi_get_soft_state(mi2cv_state, instance);

	if ((i2c->mi2cv_attachflags & ADD_INTR) != 0) {
		ddi_remove_intr(dip, 0, i2c->mi2cv_icookie);
	}

	cv_destroy(&i2c->mi2cv_cv);

	if ((i2c->mi2cv_attachflags & IMUTEX) != 0) {
		mutex_destroy(&i2c->mi2cv_imutex);
		cv_destroy(&i2c->mi2cv_icv);
	}
	if ((i2c->mi2cv_attachflags & SETUP_REGS) != 0) {
		ddi_regs_map_free(&i2c->mi2cv_rhandle);
	}
	if ((i2c->mi2cv_attachflags & NEXUS_REGISTER) != 0) {
		i2c_nexus_unregister(dip);
	}
	if ((i2c->mi2cv_attachflags & PROP_CREATE) != 0) {
		(void) ddi_prop_remove(DDI_DEV_T_NONE, dip,
		    "interrupt-priorities");
	}
	if ((i2c->mi2cv_attachflags & MINOR_NODE) != 0) {
		ddi_remove_minor_node(dip, NULL);
	}

	ddi_soft_state_free(mi2cv_state, instance);
}

static int
mi2cv_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	switch (cmd) {
	case DDI_DETACH:
		mi2cv_dodetach(dip);

		return (DDI_SUCCESS);
	case DDI_SUSPEND:
		mi2cv_suspend(dip);

		return (DDI_SUCCESS);
	default:

		return (DDI_FAILURE);
	}
}

/*ARGSUSED*/
static int
mi2cv_open(dev_t *devp, int flag, int otyp, cred_t *cred_p)
{
	int instance;
	mi2cv_t *i2c;

	/*
	 * Make sure the open is for the right file type
	 */
	if (otyp != OTYP_CHR) {
		return (EINVAL);
	}

	instance = getminor(*devp);
	i2c = (mi2cv_t *)ddi_get_soft_state(mi2cv_state, instance);

	if (i2c == NULL) {
		return (ENXIO);
	}

	/*
	 * Enforce exclusive access
	 */
	if (atomic_cas_uint(&i2c->mi2cv_open, 0, 1) != 0) {
		return (EBUSY);
	}

	return (0);
}

/*ARGSUSED*/
static int
mi2cv_close(dev_t dev, int flag, int otyp, cred_t *cred_p)
{
	int instance;
	mi2cv_t *i2c;

	/*
	 * Make sure the close is for the right file type
	 */
	if (otyp != OTYP_CHR) {
		return (EINVAL);
	}

	instance = getminor(dev);
	i2c = (mi2cv_t *)ddi_get_soft_state(mi2cv_state, instance);

	if (i2c == NULL) {
		return (ENXIO);
	}

	i2c->mi2cv_open = 0;

	return (0);
}

/*ARGSUSED*/
static int
mi2cv_ioctl(dev_t dev, int cmd, intptr_t arg, int mode, cred_t *credp,
	int *rvalp)
{
	mi2cv_t *i2c;
	dev_info_t *self;
	struct devctl_iocdata *dcp;
	int rv;

	i2c = (mi2cv_t *)ddi_get_soft_state(mi2cv_state, getminor(dev));

	if (i2c == NULL) {
		return (ENXIO);
	}

	self = (dev_info_t *)i2c->mi2cv_nexus_dip;

	/*
	 * read devctl ioctl data
	 */
	if (ndi_dc_allochdl((void *)arg, &dcp) != NDI_SUCCESS) {

		return (EFAULT);
	}

	switch (cmd) {
		case DEVCTL_BUS_DEV_CREATE:
			rv = ndi_dc_devi_create(dcp, self, 0, NULL);
			break;
		case DEVCTL_DEVICE_REMOVE:
			rv = ndi_devctl_device_remove(self, dcp, 0);
			break;
		default:
			rv = ENOTSUP;
	}

	ndi_dc_freehdl(dcp);

	return (rv);
}

static uint_t
mi2cv_intr(caddr_t arg)
{
	mi2cv_t *i2c = (mi2cv_t *)arg;
	uint8_t cntr;

	/*
	 * If we are in POLL mode, we are in the process of forceably
	 * acquiring the bus. In that case mi2cv_process() will be called
	 * from mi2cv_do_polled_io().
	 */
	if (i2c->mi2cv_mode == MI2CV_POLL_MODE) {

		return (DDI_INTR_CLAIMED);
	}

	mutex_enter(&i2c->mi2cv_imutex);

	/*
	 * It is necessary to check both whether the hardware is interrupting
	 * and that there is a current transaction for the bus in progress.
	 */
	if (i2c->mi2cv_cur_tran == NULL) {
		mutex_exit(&i2c->mi2cv_imutex);

		return (DDI_INTR_UNCLAIMED);
	}

	cntr = mi2cv_get_cntr(i2c);

	if (!(cntr & CNTR_IFLG)) {
		mutex_exit(&i2c->mi2cv_imutex);

		return (DDI_INTR_UNCLAIMED);
	}

	if (mi2cv_process(i2c) == I2C_COMPLETE) {
		i2c->mi2cv_tran_state = TRAN_STATE_NULL;
		i2c->mi2cv_cur_status = MI2CV_TRANSFER_OVER;
		cv_signal(&i2c->mi2cv_icv);
	} else {
		i2c->mi2cv_cur_status = MI2CV_TRANSFER_ON;
	}

	mutex_exit(&i2c->mi2cv_imutex);

	return (DDI_INTR_CLAIMED);
}

static int
mi2cv_bus_ctl(dev_info_t *dip, dev_info_t *rdip, ddi_ctl_enum_t op,
void *arg, void *result)
{
	switch (op) {
	case DDI_CTLOPS_INITCHILD:

		return (mi2cv_initchild((dev_info_t *)arg));
	case DDI_CTLOPS_UNINITCHILD:
		mi2cv_uninitchild((dev_info_t *)arg);

		return (DDI_SUCCESS);
	case DDI_CTLOPS_REPORTDEV:
		mi2cv_reportdev(dip, rdip);

		return (DDI_SUCCESS);
	case DDI_CTLOPS_DMAPMAPC:
	case DDI_CTLOPS_POKE:
	case DDI_CTLOPS_PEEK:
	case DDI_CTLOPS_IOMIN:
	case DDI_CTLOPS_REPORTINT:
	case DDI_CTLOPS_SIDDEV:
	case DDI_CTLOPS_SLAVEONLY:
	case DDI_CTLOPS_AFFINITY:
	case DDI_CTLOPS_PTOB:
	case DDI_CTLOPS_BTOP:
	case DDI_CTLOPS_BTOPR:
	case DDI_CTLOPS_DVMAPAGESIZE:

		return (DDI_FAILURE);
	default:

		return (ddi_ctlops(dip, rdip, op, arg, result));
	}
/*NOTREACHED*/
}

static int
mi2cv_initchild(dev_info_t *cdip)
{
	int32_t cell_size;
	int len;
	int32_t regs[2];
	int err;
	mi2cv_ppvt_t *ppvt;
	char name[30];

	ppvt = kmem_alloc(sizeof (mi2cv_ppvt_t), KM_SLEEP);

	len = sizeof (cell_size);
	err = ddi_getlongprop_buf(DDI_DEV_T_ANY, cdip,
	    DDI_PROP_CANSLEEP, "#address-cells",
	    (caddr_t)&cell_size, &len);

	if (err != DDI_PROP_SUCCESS || len != sizeof (cell_size)) {

		return (DDI_FAILURE);
	}

	len = sizeof (regs);
	err = ddi_getlongprop_buf(DDI_DEV_T_ANY, cdip,
	    DDI_PROP_DONTPASS | DDI_PROP_CANSLEEP,
	    "reg", (caddr_t)regs, &len);

	if (err != DDI_PROP_SUCCESS ||
	    len != (cell_size * sizeof (int32_t))) {

		return (DDI_FAILURE);
	}

	if (cell_size == 1) {
		ppvt->mi2cv_ppvt_addr = regs[0];
		(void) sprintf(name, "%x", regs[0]);
	} else if (cell_size == 2) {
		ppvt->mi2cv_ppvt_bus = regs[0];
		ppvt->mi2cv_ppvt_addr = regs[1];
		(void) sprintf(name, "%x,%x", regs[0], regs[1]);
	} else {

		return (DDI_FAILURE);
	}

	ddi_set_parent_data(cdip, (caddr_t)ppvt);
	ddi_set_name_addr(cdip, name);

	return (DDI_SUCCESS);
}


static void
mi2cv_uninitchild(dev_info_t *cdip)
{
	mi2cv_ppvt_t *ppvt;

	ppvt = (mi2cv_ppvt_t *)ddi_get_parent_data(cdip);
	kmem_free(ppvt, sizeof (mi2cv_ppvt_t));

	ddi_set_parent_data(cdip, NULL);
	ddi_set_name_addr(cdip, NULL);
}

static void
mi2cv_reportdev(dev_info_t *dip, dev_info_t *rdip)
{
	mi2cv_ppvt_t *ppvt;

	ppvt = (mi2cv_ppvt_t *)ddi_get_parent_data(rdip);

	cmn_err(CE_CONT, "?%s%d at %s%d: addr 0x%x",
	    ddi_driver_name(rdip), ddi_get_instance(rdip),
	    ddi_driver_name(dip), ddi_get_instance(dip),
	    ppvt->mi2cv_ppvt_addr);
}

static void
mi2cv_init(mi2cv_t *i2c)
{
	/* Reset the chip */
	mi2cv_reset(i2c);

	/* Set the I2C clock speed */
	mi2cv_set_ccr(i2c, CLK_N, CLK_M);

	/* Enable the bus */
	mi2cv_set_cntr(i2c, CNTR_ENAB);
}

static void
mi2cv_set_ccr(mi2cv_t *i2c, uint8_t nvalue, uint8_t mvalue)
{
	uint64_t clk_val;

	/* get already calculated i2c-clk-val OBP property */
	clk_val = (uint64_t)ddi_getprop(DDI_DEV_T_ANY, i2c->mi2cv_dip,
	    DDI_PROP_DONTPASS, "i2c-clock-val",
	    (mvalue << CLK_SHIFT) | nvalue);

	ddi_put64(i2c->mi2cv_rhandle,
	    &i2c->mi2cv_regs->clock_control_reg, clk_val);
}

/*
 * mi2cv_reset() writes out to the reset register.
 */
static void
mi2cv_reset(mi2cv_t *i2c)
{
	ddi_put64(i2c->mi2cv_rhandle,
	    &i2c->mi2cv_regs->sw_reset_reg, MI2CV_RESET);
}

/*
 * mi2cv_set_cntr() writes out to the control register.
 */
static void
mi2cv_set_cntr(mi2cv_t *i2c, uint8_t cmd)
{
	/* enable interrupts only if in interrupt mode */
	if (i2c->mi2cv_mode == MI2CV_INTR_MODE) {
		cmd |= CNTR_IEN;
	}
	ddi_put64(i2c->mi2cv_rhandle, &i2c->mi2cv_regs->control_reg, cmd);
}

/*
 * mi2cv_set_data() writes out to the data register.
 */
static void
mi2cv_set_data(mi2cv_t *i2c, uint8_t data)
{
	ddi_put64(i2c->mi2cv_rhandle, &i2c->mi2cv_regs->data_byte_reg, data);
}

/*
 * mi2cv_get_cntr() reads from the control register.
 */
static uint8_t
mi2cv_get_cntr(mi2cv_t *i2c)
{
	return ((uint8_t)ddi_get64(i2c->mi2cv_rhandle,
	    &i2c->mi2cv_regs->control_reg));
}

/*
 * mi2cv_get_data() reads from the data register.
 */
static uint8_t
mi2cv_get_data(mi2cv_t *i2c)
{
	return ((uint8_t)ddi_get64(i2c->mi2cv_rhandle,
	    &i2c->mi2cv_regs->data_byte_reg));
}

/*
 * mi2cv_get_status() reads from the status register.
 */
static uint8_t
mi2cv_get_status(mi2cv_t *i2c)
{
	return ((uint8_t)ddi_get64(i2c->mi2cv_rhandle,
	    &i2c->mi2cv_regs->status_reg));
}

/*
 * mi2cv_transfer() is the function that is registered with
 * I2C services to be called from i2c_transfer() for each transfer.
 *
 * This function starts the transfer, and then waits for the
 * interrupt or polled thread to signal that the transfer has
 * completed.
 */
int
mi2cv_transfer(dev_info_t *dip, i2c_transfer_t *tp)
{
	mi2cv_t *i2c;
	int saved_mode, took_over = 0;
	kcondvar_t *waiter = NULL;
	extern int do_polled_io;

	i2c = (mi2cv_t *)ddi_get_soft_state(mi2cv_state,
	    ddi_get_instance(ddi_get_parent(dip)));

	tp->i2c_r_resid = tp->i2c_rlen;
	tp->i2c_w_resid = tp->i2c_wlen;
	tp->i2c_result = I2C_SUCCESS;

begin:
	/*
	 * If we're explicitly asked to do polled io or if we are panicking,
	 * we need to usurp ownership of the I2C bus, bypassing any other
	 * waiters.
	 */
	if (do_polled_io || ddi_in_panic()) {
		mi2cv_take_over(i2c, dip, tp, &waiter, &saved_mode);
		took_over = 1;
	} else {
		mi2cv_acquire(i2c, dip, tp);
		mutex_enter(&i2c->mi2cv_imutex);

		/*
		 * See if someone else had intruded and taken over the bus
		 * between the 'mi2cv_acquire' and 'mutex_enter' above.
		 * If so, we'll have to start all over again.
		 */
		if (i2c->mi2cv_cur_tran != tp) {
			mutex_exit(&i2c->mi2cv_imutex);
			goto begin;
		}
	}

	/*
	 * Set START in the CNTR register. Start will be transferred when
	 * the bus is free. When that happens the IFLG will be set.
	 */
	mi2cv_set_cntr(i2c, CNTR_ENAB | CNTR_START);
	i2c->mi2cv_tran_state = TRAN_STATE_START;

	/*
	 * Update transfer status so anyone taking over the bus
	 * will wait for this transfer to complete first.
	 */
	i2c->mi2cv_cur_status = MI2CV_TRANSFER_ON;

	if (i2c->mi2cv_mode == MI2CV_POLL_MODE) {
		mi2cv_do_polled_io(i2c);
	} else {
		cv_wait(&i2c->mi2cv_icv, &i2c->mi2cv_imutex);
	}

	if (took_over) {
		mi2cv_give_up(i2c, waiter, saved_mode);
	} else {
		mutex_exit(&i2c->mi2cv_imutex);

		/*
		 * Release the I2C bus only if we still own it. If we don't
		 * own it (someone usurped it from us while we were waiting),
		 * we still need to drop the lock that serializes access to
		 * the mi2cv controller on systems where OBP shares the
		 * controller with the OS.
		 */
		if (i2c->mi2cv_cur_tran == tp) {
			mi2cv_release(i2c);
		} else if (&plat_shared_i2c_exit && dip) {
			plat_shared_i2c_exit(i2c->mi2cv_dip);
		}
	}

	return (tp->i2c_result);
}

/*
 * mi2cv_acquire() is called by a thread wishing to "own" the I2C bus.
 * It should not be held across multiple transfers.
 */
static void
mi2cv_acquire(mi2cv_t *i2c, dev_info_t *dip, i2c_transfer_t *tp)
{
	mutex_enter(&i2c->mi2cv_imutex);
	while (i2c->mi2cv_busy) {
		cv_wait(&i2c->mi2cv_cv, &i2c->mi2cv_imutex);
	}
	i2c->mi2cv_busy = 1;
	mutex_exit(&i2c->mi2cv_imutex);

	/*
	 * On systems where OBP shares a mi2cv controller with the
	 * OS, plat_shared_i2c_enter will serialize access to the
	 * mi2cv controller.  Do not grab this lock during CPR
	 * suspend as the CPR thread also acquires this mutex
	 * through prom_setprop which causes recursive mutex enter.
	 *
	 * dip == NULL during CPR.
	 */
	if ((&plat_shared_i2c_enter != NULL) && (dip != NULL)) {
		plat_shared_i2c_enter(i2c->mi2cv_dip);
	}

	mutex_enter(&i2c->mi2cv_imutex);
	i2c->mi2cv_cur_tran = tp;
	i2c->mi2cv_cur_dip = dip;
	mutex_exit(&i2c->mi2cv_imutex);
}

/*
 * mi2cv_type_to_state() converts a transfer type to the
 * next state of the I2C state machine based on the requested
 * transfer type.
 */
static enum tran_state
mi2cv_type_to_state(int i2c_flags)
{
	switch (i2c_flags) {
	case I2C_WR:

		return (TRAN_STATE_WR);
	case I2C_RD:

		return (TRAN_STATE_DUMMY_RD);
	case I2C_WR_RD:

		return (TRAN_STATE_WR_RD);
	default:
		/*
		 * this "cannot happen" because i2c_transfer()
		 * guarantees the integrity of i2c_flags
		 */
		ASSERT(0);
		return (TRAN_STATE_NULL);
	}
	/*NOTREACHED*/
}

static void
mi2cv_error(uint8_t status, mi2cv_t *i2c)
{
	switch (status) {
	case BUS_ERROR:
	case ARB_LOST:
		mi2cv_init(i2c);
		break;

	default:
		mi2cv_set_cntr(i2c, CNTR_ENAB | CNTR_STOP);
	}
}

/*
 * Called from mi2cv_intr() or mi2cv_do_polled_io() when IFLG is set,
 * indicating the device is ready to be serviced.
 */
static int
mi2cv_process(mi2cv_t *i2c)
{
	i2c_transfer_t *tp = i2c->mi2cv_cur_tran;
	int addr = mi2cv_dip_to_addr(i2c->mi2cv_cur_dip);
	int dummy_read;
	uint8_t status_byte;

	ASSERT(i2c->mi2cv_tran_state != TRAN_STATE_NULL);

	status_byte = mi2cv_get_status(i2c);

	switch (i2c->mi2cv_tran_state) {
	case TRAN_STATE_START:
		if (status_byte != START_TRANSMITED) {
			mi2cv_error(status_byte, i2c);
			tp->i2c_result = I2C_FAILURE;

			return (I2C_COMPLETE);
		}
		i2c->mi2cv_tran_state = mi2cv_type_to_state(tp->i2c_flags);

		/* Set read bit if this is a read transaction */
		if (tp->i2c_flags == I2C_RD) {
			addr |= I2C_READ;
		}

		/* Set address into the data register */
		mi2cv_set_data(i2c, (uint8_t)addr);

		/*
		 * Reset control reg to just ENAB (and IEN in interrupt mode),
		 * thus clearing IFLG
		 */
		mi2cv_set_cntr(i2c, CNTR_ENAB);

		return (I2C_PENDING);

	case TRAN_STATE_WR:
		if ((status_byte != ADDR_WR_W_ACK) &&
		    (status_byte != DATA_SENT_W_ACK)) {
			mi2cv_error(status_byte, i2c);
			tp->i2c_result = I2C_FAILURE;

			return (I2C_COMPLETE);
		}

		/* check to see if at end of buffer */
		if (tp->i2c_w_resid == 0) {
			mi2cv_set_cntr(i2c, CNTR_ENAB | CNTR_STOP);

			return (I2C_COMPLETE);
		}

		/* write a byte of data and then clear the IFLG */
		mi2cv_set_data(i2c, tp->i2c_wbuf[tp->i2c_wlen -
		    tp->i2c_w_resid--]);
		mi2cv_set_cntr(i2c, CNTR_ENAB);

		return (I2C_PENDING);

	case TRAN_STATE_DUMMY_RD:
		if (status_byte != ADDR_RD_W_ACK) {
			mi2cv_error(status_byte, i2c);
			tp->i2c_result = I2C_FAILURE;

			return (I2C_COMPLETE);
		}

		/*
		 * The first read is always a dummy read.
		 * This byte is accessed during the next read,
		 * which starts another 8 bit bus shift.
		 *
		 * special case for 1 byte reads:  Clear the ACK bit
		 * here since this read causes the last and only byte
		 * to be sent on the I2C bus.
		 */
		if (tp->i2c_r_resid == 1) {
			mi2cv_set_cntr(i2c, CNTR_ENAB);
		} else {
			mi2cv_set_cntr(i2c, CNTR_ENAB | CNTR_ACK);
		}

		/*
		 * dummy read
		 */
		dummy_read = mi2cv_get_data(i2c);
#ifdef lint
		dummy_read = dummy_read;
#endif
		i2c->mi2cv_tran_state = TRAN_STATE_RD;

		return (I2C_PENDING);

	case TRAN_STATE_RD:
		if ((status_byte != DATA_RECV_W_ACK) &&
		    (status_byte != DATA_RECV_NO_ACK)) {
			mi2cv_error(status_byte, i2c);
			tp->i2c_result = I2C_FAILURE;

			return (I2C_COMPLETE);
		}

		if (tp->i2c_rlen < tp->i2c_r_resid) {
			/*
			 * client driver is responsible for making sure this
			 * doesn't happen; try to do something sane if it does
			 */
			mi2cv_set_cntr(i2c, CNTR_ENAB);
			tp->i2c_result = I2C_FAILURE;

			return (I2C_COMPLETE);
		}

		tp->i2c_rbuf[tp->i2c_rlen - tp->i2c_r_resid] =
		    mi2cv_get_data(i2c);

		switch (tp->i2c_r_resid) {
		case 1:
			/*
			 * the last byte has already been shifted into
			 * the accumulator.  Send STOP
			 */
			mi2cv_set_cntr(i2c, CNTR_ENAB | CNTR_STOP);
			break;
		case 2:
			/*
			 * the next read will cause the I2C bus to start
			 * shifting in the last byte on the I2C bus, which
			 * we don't want to be ACK'd, so clear the ACK bit
			 */
			mi2cv_set_cntr(i2c, CNTR_ENAB);
			break;
		default:
			mi2cv_set_cntr(i2c, CNTR_ENAB | CNTR_ACK);
		}

		return (--tp->i2c_r_resid == 0 ? I2C_COMPLETE : I2C_PENDING);

	case TRAN_STATE_WR_RD:
		if ((status_byte != ADDR_WR_W_ACK) &&
		    (status_byte != DATA_SENT_W_ACK)) {
			mi2cv_error(status_byte, i2c);
			tp->i2c_result = I2C_FAILURE;

			return (I2C_COMPLETE);
		}

		if (tp->i2c_w_resid != 0) {
			mi2cv_set_data(i2c, tp->i2c_wbuf[tp->i2c_wlen -
			    tp->i2c_w_resid--]);
			mi2cv_set_cntr(i2c, CNTR_ENAB);
		} else {
			/* Send Repeated Start and clear IFLG */
			mi2cv_set_cntr(i2c, CNTR_ENAB | CNTR_START);
			i2c->mi2cv_tran_state =	TRAN_STATE_REPEAT_START;
		}

		return (I2C_PENDING);

	case TRAN_STATE_REPEAT_START:
		if (status_byte != REP_START_TRANS) {
			mi2cv_error(status_byte, i2c);
			tp->i2c_result = I2C_FAILURE;

			return (I2C_COMPLETE);
		}

		/* Set address into the data register */
		mi2cv_set_data(i2c, (uint8_t)addr | I2C_READ);

		/*
		 * Reset control reg to just ENAB (and IEN in interrupt mode),
		 * thus clearing IFLG
		 */
		mi2cv_set_cntr(i2c, CNTR_ENAB);
		i2c->mi2cv_tran_state =	TRAN_STATE_DUMMY_RD;

		return (I2C_PENDING);

	default:
		/*
		 * Reset control reg to just ENAB (and IEN in interrupt mode),
		 * thus clearing IFLG
		 */
		mi2cv_set_cntr(i2c, CNTR_ENAB);

		return (I2C_COMPLETE);
	}
}

/*
 * i2_nexus_dip_to_addr() takes a dip and returns an I2C address.
 */
static int
mi2cv_dip_to_addr(dev_info_t *dip)
{
	mi2cv_ppvt_t *ppvt;

	ppvt = (mi2cv_ppvt_t *)ddi_get_parent_data(dip);

	return (ppvt->mi2cv_ppvt_addr);
}

static void
mi2cv_do_polled_io(mi2cv_t *i2c)
{
	int completed = I2C_PENDING;
	uint8_t cntr_byte;
	int times = 0;

	while ((completed != I2C_COMPLETE) && (times <= 10)) {
		cntr_byte = mi2cv_get_cntr(i2c);
		if (cntr_byte & CNTR_IFLG) {
			ASSERT(i2c->mi2cv_cur_tran);
			completed = mi2cv_process(i2c);
			times = 0;
		} else times++;
		drv_usecwait(100);
	}

	if (times > 10) {
		mi2cv_reset(i2c);
	}

	i2c->mi2cv_cur_status = MI2CV_TRANSFER_OVER;
}

/*
 * mi2cv_release() is called to release a hold made by mi2cv_acquire().
 */
static void
mi2cv_release(mi2cv_t *i2c)
{
	mutex_enter(&i2c->mi2cv_imutex);
	i2c->mi2cv_busy = 0;
	i2c->mi2cv_cur_tran = NULL;
	cv_signal(&i2c->mi2cv_cv);
	mutex_exit(&i2c->mi2cv_imutex);

	if ((&plat_shared_i2c_exit != NULL) && (i2c->mi2cv_cur_dip != NULL)) {
		plat_shared_i2c_exit(i2c->mi2cv_dip);
	}
}

/*
 * mi2cv_take_over() switches to polled mode, flushes any pending
 * transaction, and grabs the I2C bus by force.
 */
static void
mi2cv_take_over(mi2cv_t *i2c, dev_info_t *dip, i2c_transfer_t *tp,
    kcondvar_t **waiter, int *saved_mode)
{
	mutex_enter(&i2c->mi2cv_imutex);

	/*
	 * switch to polled mode
	 */
	*saved_mode = i2c->mi2cv_mode;
	i2c->mi2cv_mode = MI2CV_POLL_MODE;

	/*
	 * flush out any currently pending transaction
	 */
	if (i2c->mi2cv_busy && i2c->mi2cv_cur_tran &&
	    (i2c->mi2cv_cur_status == MI2CV_TRANSFER_ON)) {
		mi2cv_do_polled_io(i2c);
		*waiter = &i2c->mi2cv_icv;
	}

	/*
	 * forceably acquire the I2C bus
	 */
	i2c->mi2cv_busy = 1;
	i2c->mi2cv_cur_tran = tp;
	i2c->mi2cv_cur_dip = dip;
	i2c->mi2cv_cur_status = MI2CV_TRANSFER_NEW;
}

/*
 * mi2cv_give_up() returns all resources that were taken over forceably
 */
static void
mi2cv_give_up(mi2cv_t *i2c, kcondvar_t *waiter, int saved_mode)
{
	/*
	 * restore previous transfer mode
	 */
	i2c->mi2cv_mode = saved_mode;

	/*
	 * release the I2C bus and wake up threads waiting to acquire it
	 */
	i2c->mi2cv_busy = 0;
	i2c->mi2cv_cur_tran = NULL;
	i2c->mi2cv_cur_dip = NULL;
	i2c->mi2cv_cur_status = MI2CV_TRANSFER_OVER;
	cv_signal(&i2c->mi2cv_cv);

	/*
	 * wake up the waiter from whom we usurped the bus
	 */
	if (waiter) {
		cv_signal(waiter);
	}

	mutex_exit(&i2c->mi2cv_imutex);
}
