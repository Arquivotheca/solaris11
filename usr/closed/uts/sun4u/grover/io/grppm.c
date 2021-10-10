/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Platform Power Management driver for SUNW,Sun-Blade-100
 */
#include <sys/modctl.h>
#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/ddi_impldefs.h>
#include <sys/ppmvar.h>
#include <sys/ppmio.h>
#include <sys/grppm.h>
#include <sys/epm.h>
#include <sys/stat.h>


static int	grppm_attach(dev_info_t *, ddi_attach_cmd_t);
static int	grppm_detach(dev_info_t *, ddi_detach_cmd_t);
static int	grppm_ctlops(dev_info_t *, dev_info_t *,
		    ddi_ctl_enum_t, void *, void *);
static void	grppm_switch_powerfet(uint8_t);
static uint8_t	grppm_powerfet_status(void);
static void	grppm_dev_init(ppm_dev_t *);

static ppm_domain_t grppm_fet = { "domain_powerfet", PPMD_LOCK_ONE };

ppm_domain_t *ppm_domains[] = {
	&grppm_fet,
	NULL
};

struct ppm_funcs ppmf = {
	grppm_dev_init,			/* dev_init */
	NULL,				/* dev_fini */
	grppm_switch_powerfet,		/* iocset */
	grppm_powerfet_status,		/* iocget */
};


#define	IN_POWERFETD(gpd)	((gpd)->domp == &grppm_fet)

#define	TURN_OFF	0
#define	TURN_ON		1


/*
 * Configuration data structures
 */
static struct cb_ops grppm_cb_ops = {
	ppm_open,		/* open */
	ppm_close,		/* close */
	nodev,			/* strategy */
	nodev,			/* print */
	nodev,			/* dump */
	nodev,			/* read */
	nodev,			/* write */
	ppm_ioctl,		/* ioctl */
	nodev,			/* devmap */
	nodev,			/* mmap */
	nodev,			/* segmap */
	nochpoll,		/* poll */
	ddi_prop_op,		/* prop_op */
	NULL,			/* streamtab */
	D_MP | D_NEW,		/* driver compatibility flag */
	CB_REV,			/* cb_ops revision */
	nodev,			/* async read */
	nodev			/* async write */
};

static struct bus_ops grppm_bus_ops = {
	BUSO_REV,
	0,
	0,
	0,
	0,
	0,
	ddi_no_dma_map,
	ddi_no_dma_allochdl,
	ddi_no_dma_freehdl,
	ddi_no_dma_bindhdl,
	ddi_no_dma_unbindhdl,
	ddi_no_dma_flush,
	ddi_no_dma_win,
	ddi_no_dma_mctl,
	grppm_ctlops,
	0,
	0,			/* (*bus_get_eventcookie)();	*/
	0,			/* (*bus_add_eventcall)();	*/
	0,			/* (*bus_remove_eventcall)();	*/
	0			/* (*bus_post_event)();		*/
};

static struct dev_ops grppm_ops = {
	DEVO_REV,		/* devo_rev */
	0,			/* refcnt */
	ppm_getinfo,		/* info */
	nulldev,		/* identify */
	nulldev,		/* probe */
	grppm_attach,		/* attach */
	grppm_detach,		/* detach */
	nodev,			/* reset */
	&grppm_cb_ops,		/* driver operations */
	&grppm_bus_ops,		/* bus operations */
	NULL,			/* power */
	ddi_quiesce_not_supported,	/* devo_quiesce */
};

static struct modldrv modldrv = {
	&mod_driverops,		/* type of module - driver */
	"platform pm driver",
	&grppm_ops
};

static struct modlinkage modlinkage = {
	MODREV_1,
	&modldrv,
	NULL
};


int
_init(void)
{
	return (ppm_init(&modlinkage, sizeof (grppm_unit_t), "gr"));
}


int
_fini(void)
{
	return (EBUSY);
}


int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}


/*
 * Do the actual attach work
 */
static int
grppm_do_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	char *str = "grppm_do_attach";
	ddi_device_acc_attr_t attr;
	grppm_unit_t *unitp;
	ppm_domain_t **dompp;
	char *regfmt;
	int retval;

	DPRINTF(D_ATTACH, ("%s: dip 0x%p, cmd 0x%x\n", str, (void *)dip, cmd));

	if (ppm_inst != -1) {
		cmn_err(CE_WARN, "%s: an instance is already attached!", str);
		return (DDI_FAILURE);
	}

	ppm_inst = ddi_get_instance(dip);
	if (ddi_soft_state_zalloc(ppm_statep, ppm_inst) != DDI_SUCCESS) {
		cmn_err(CE_WARN, "%s: device states alloc error", str);
		return (DDI_FAILURE);
	}
	unitp = ddi_get_soft_state(ppm_statep, ppm_inst);

	attr.devacc_attr_version = DDI_DEVICE_ATTR_V0;
	attr.devacc_attr_endian_flags = DDI_STRUCTURE_LE_ACC;
	attr.devacc_attr_dataorder = DDI_STRICTORDER_ACC;

	regfmt = "%s: ddi_regs_map_setup failed for regset %d\n";
	retval = ddi_regs_map_setup(dip, 0, (caddr_t *)&unitp->regs.sus_led,
	    0, 1, &attr, &unitp->handles.sus_led);
	if (retval != DDI_SUCCESS) {
		cmn_err(CE_WARN, regfmt, str, 0);
		goto fail1;
	}
	retval = ddi_regs_map_setup(dip, 1, (caddr_t *)&unitp->regs.power_fet,
	    0, 1, &attr, &unitp->handles.power_fet);
	if (retval != DDI_SUCCESS) {
		cmn_err(CE_WARN, regfmt, str, 1);
		goto fail2;
	}

	retval = ddi_create_minor_node(dip, "ppm", S_IFCHR,
	    ppm_inst, "ddi_ppm", 0);
	if (retval != DDI_SUCCESS) {
		cmn_err(CE_WARN, "%s: can't create minor node", str);
		goto fail3;
	}

	unitp->dip = dip;
	retval = ppm_create_db(dip);
	if (retval != DDI_SUCCESS)
		goto fail4;

	mutex_init(&unitp->lock, NULL, MUTEX_DRIVER, NULL);
	for (dompp = ppm_domains; *dompp; dompp++)
		mutex_init(&(*dompp)->lock, NULL, MUTEX_DRIVER, NULL);

	retval = pm_register_ppm(ppm_claim_dev, dip);
	if (retval != DDI_SUCCESS) {
		cmn_err(CE_WARN, "%s: can't register ppm handler", str);
		goto fail4;
	}

	ddi_report_dev(dip);
	return (retval);

fail4:
	ddi_remove_minor_node(dip, "ddi_ppm");
fail3:
	ddi_regs_map_free(&unitp->handles.power_fet);
fail2:
	ddi_regs_map_free(&unitp->handles.sus_led);
fail1:
	ddi_soft_state_free(ppm_statep, ppm_inst);
	ppm_inst = -1;
	return (retval);
}


static int
grppm_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	grppm_unit_t *unitp;

	switch (cmd) {
	case DDI_ATTACH:
		return (grppm_do_attach(dip, cmd));

	case DDI_RESUME:
		DPRINTF(D_ATTACH, ("grppm_attach: driver is resumed\n"));
		unitp = ddi_get_soft_state(ppm_statep, ppm_inst);
		mutex_enter(&unitp->lock);
		unitp->states &= ~GRPPM_STATE_SUSPEND;
		mutex_exit(&unitp->lock);
		return (DDI_SUCCESS);

	default:
		return (DDI_FAILURE);
	}
}


/*
 * This routine determines if LED is currently on or off.
 */
static int
grppm_led_status(grppm_unit_t *unitp)
{
	uint8_t data8;

	data8 = ddi_get8(unitp->handles.sus_led,
	    (uint8_t *)unitp->regs.sus_led);
	return (data8 & (1 << SUS_LED_CTL));
}


static void
grppm_mod8(ddi_acc_handle_t handle, uint8_t *reg, uint8_t bit, int action)
{
	uint8_t data8;

	data8 = ddi_get8(handle, reg);
	if (action == TURN_ON)
		data8 |= (1 << bit);
	else
		data8 &= ~(1 << bit);
	ddi_put8(handle, reg, data8);
	data8 = ddi_get8(handle, reg);
}


/*
 * switch LED between on and off
 */
static void
grppm_blink_led(void *arg)
{
	grppm_unit_t *unitp = arg;
	uint8_t status;
	clock_t intvl;

	mutex_enter(&unitp->lock);
	if (unitp->grppm_led_tid == 0) {
		mutex_exit(&unitp->lock);
		return;
	}

	if (status = grppm_led_status(unitp))
		intvl = PPM_LEDOFF_INTERVAL;
	else
		intvl = PPM_LEDON_INTERVAL;

	DPRINTF(D_LED, ("grppm_blink_led: turn LED %s\n",
	    status ? "off" : "on"));
	grppm_mod8(unitp->handles.sus_led, (uint8_t *)unitp->regs.sus_led,
	    SUS_LED_CTL, !status);
	unitp->grppm_led_tid = timeout(grppm_blink_led, unitp, intvl);
	mutex_exit(&unitp->lock);
}


/*
 * switch LED between blinking and not blinking (remaining on)
 */
static void
grppm_switch_led(int action)
{
	grppm_unit_t *unitp;
	timeout_id_t tid;
	char *fmt;

	fmt = "grppm_switch_led: %s blinking LED\n";
	unitp = ddi_get_soft_state(ppm_statep, ppm_inst);

	mutex_enter(&unitp->lock);
	if (action == PPM_LEDON) {
		if (unitp->states & GRPPM_STATE_SUSPEND) {
			mutex_exit(&unitp->lock);
			return;
		}
		DPRINTF(D_LED, (fmt, "start"));
		grppm_mod8(unitp->handles.sus_led,
		    (uint8_t *)unitp->regs.sus_led, SUS_LED_CTL, TURN_OFF);
		ASSERT(unitp->grppm_led_tid == 0);
		unitp->grppm_led_tid = timeout(grppm_blink_led, unitp,
		    PPM_LEDOFF_INTERVAL);
	} else {
		ASSERT(action == PPM_LEDOFF);
		tid = unitp->grppm_led_tid;
		unitp->grppm_led_tid = 0;
		mutex_exit(&unitp->lock);
		DPRINTF(D_LED, (fmt, "stop"));
		(void) untimeout(tid);
		mutex_enter(&unitp->lock);
		/* Make sure LED remains on */
		if (!grppm_led_status(unitp))
			grppm_mod8(unitp->handles.sus_led,
			    (uint8_t *)unitp->regs.sus_led,
			    SUS_LED_CTL, TURN_ON);
	}
	mutex_exit(&unitp->lock);
}


/* ARGSUSED */
static int
grppm_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
#ifdef DEBUG
	char *str = "grppm_detach";
#endif
	grppm_unit_t *unitp;

	switch (cmd) {
	case DDI_DETACH:
		DPRINTF(D_DETACH, ("%s: trying to detach\n", str));
		return (DDI_FAILURE);

	case DDI_SUSPEND:
		DPRINTF(D_DETACH, ("%s: driver is suspended\n", str));
		unitp = ddi_get_soft_state(ppm_statep, ppm_inst);
		mutex_enter(&unitp->lock);
		unitp->states |= GRPPM_STATE_SUSPEND;
		mutex_exit(&unitp->lock);

		/*
		 * Suspend requires that timeout callouts to be canceled.
		 * Turning off the LED blinking will cancel the timeout.
		 */
		grppm_switch_led(PPM_LEDON);
		return (DDI_SUCCESS);

	default:
		return (DDI_FAILURE);
	}
}


/*
 * switch power FET on or off;
 * see ppmf and ppm_ioctl, PPMIOCSET
 */
static void
grppm_switch_powerfet(uint8_t mode)
{
	grppm_unit_t *unitp;

	unitp = ddi_get_soft_state(ppm_statep, ppm_inst);
	mutex_enter(&unitp->lock);
	grppm_mod8(unitp->handles.power_fet,
	    (uint8_t *)unitp->regs.power_fet, POWER_FET_CTL, mode);
	mutex_exit(&unitp->lock);
	DPRINTF(D_SETPWR, ("grppm_switch_powerfet: mode %d\n", (uint8_t)mode));
}


/*
 * determine if power FET is currently on or off;
 * see ppmf and ppm_ioctl, PPMIOCGET
 */
static uint8_t
grppm_powerfet_status(void)
{
	grppm_unit_t *unitp;
	uint8_t data8, val8;

	unitp = ddi_get_soft_state(ppm_statep, ppm_inst);
	data8 = ddi_get8(unitp->handles.power_fet,
	    (uint8_t *)unitp->regs.power_fet);
	if (data8 & (1 << POWER_FET_CTL))
		val8 = PPM_IDEV_POWER_ON;
	else
		val8 = PPM_IDEV_POWER_OFF;
	return (val8);
}


/*
 * Routine to handle bus control operations.  Only power contorl
 * operations are applicable to this driver.  PM framework makes
 * power control operations for platform power management drivers.
 */
/* ARGSUSED */
static int
grppm_ctlops(dev_info_t *dip, dev_info_t *rdip,
    ddi_ctl_enum_t ctlop, void *arg, void *result)
{
	power_req_t *reqp = arg;
	int new, old, cmpt, ret = DDI_SUCCESS;
	grppm_unit_t *unitp;
	dev_info_t *who;
	ppm_dev_t *gpd;

#ifdef DEBUG
	char path[MAXPATHLEN], *ctlstr, *str = "grppm_ctlops";
	uint_t mask = ppm_debug & (D_CTLOPS1 | D_CTLOPS2);
	if (mask && (ctlstr = ppm_get_ctlstr(reqp->request_type, mask))) {
		prom_printf("%s: \"%s\", %s\n", str,
		    ddi_pathname(rdip, path), ctlstr);
	}
#endif

	if (ctlop != DDI_CTLOPS_POWER)
		return (DDI_FAILURE);

	unitp = ddi_get_soft_state(ppm_statep, ppm_inst);

	switch (reqp->request_type) {
	case PMR_PPM_UNMANAGE:
	case PMR_PPM_INIT_CHILD:
	case PMR_PPM_UNINIT_CHILD:
		break;

	case PMR_PPM_ALL_LOWEST:
		new = (reqp->req.ppm_all_lowest_req.mode == PM_ALL_LOWEST);
		DPRINTF(D_LOWEST, ("%s: %sall devices are at lowest power\n",
		    str, new ? "" : "not "));
		grppm_switch_led(new);
		break;

	case PMR_PPM_SET_POWER:
	case PMR_PPM_POWER_CHANGE_NOTIFY:
		if (reqp->request_type == PMR_PPM_SET_POWER) {
			who = reqp->req.ppm_set_power_req.who;
			ASSERT(who == rdip);
			cmpt = reqp->req.ppm_set_power_req.cmpt;
			old = reqp->req.ppm_set_power_req.old_level;
			new = reqp->req.ppm_set_power_req.new_level;
		} else {
			who = reqp->req.ppm_notify_level_req.who;
			ASSERT(who == rdip);
			cmpt = reqp->req.ppm_notify_level_req.cmpt;
			old = reqp->req.ppm_notify_level_req.old_level;
			new = reqp->req.ppm_notify_level_req.new_level;
		}

		gpd = ppm_get_dev(who, &grppm_fet);
		DPRINTF(D_SETPWR, ("%s: \"%s\", old %d new %d lvl %d\n",
		    str, gpd->path, old, new, gpd->level));

		ASSERT(old == PM_LEVEL_UNKNOWN || old == gpd->level);
		if (new == gpd->level)
			return (DDI_SUCCESS);

		if (IN_POWERFETD(gpd) &&
		    (old == 0 || old == PM_LEVEL_UNKNOWN))
			grppm_switch_powerfet(TURN_ON);
		if (reqp->request_type == PMR_PPM_SET_POWER)
			ret = pm_power(who, cmpt, new);
		if (ret == DDI_SUCCESS)
			gpd->level = new;
		if (IN_POWERFETD(gpd) && gpd->level == 0)
			grppm_switch_powerfet(TURN_OFF);

		break;

	case PMR_PPM_POST_DETACH:
		who = reqp->req.ppm_set_power_req.who;
		ASSERT(who == rdip);
		mutex_enter(&unitp->lock);
		if (reqp->req.ppm_config_req.result != DDI_SUCCESS ||
		    PPM_GET_PRIVATE(who) == NULL) {
			mutex_exit(&unitp->lock);
			break;
		}
		mutex_exit(&unitp->lock);
		ppm_rem_dev(who);
		break;

	case PMR_PPM_LOCK_POWER:
		pm_lock_power_single(reqp->req.ppm_lock_power_req.who,
		    reqp->req.ppm_lock_power_req.circp);
		break;

	case PMR_PPM_UNLOCK_POWER:
		pm_unlock_power_single(reqp->req.ppm_lock_power_req.who,
		    reqp->req.ppm_unlock_power_req.circ);
		break;

	case PMR_PPM_TRY_LOCK_POWER:
		*(int *)result = pm_try_locking_power_single(
		    reqp->req.ppm_lock_power_req.who,
		    reqp->req.ppm_lock_power_req.circp);
		break;

	case PMR_PPM_POWER_LOCK_OWNER:
		who = reqp->req.ppm_set_power_req.who;
		ASSERT(who == rdip);

		reqp->req.ppm_power_lock_owner_req.owner =
		    DEVI(who)->devi_busy_thread;
		return (DDI_SUCCESS);

	case PMR_PPM_PRE_RESUME:
		who = reqp->req.ppm_set_power_req.who;
		ASSERT(who == rdip);
		if ((gpd = PPM_GET_PRIVATE(who)) != NULL)
			gpd->level = PM_LEVEL_UNKNOWN;
		return (DDI_SUCCESS);

	default:
		ret = DDI_FAILURE;
	}

	return (ret);
}

static void
grppm_dev_init(ppm_dev_t *ppmd)
{
	ASSERT(MUTEX_HELD(&ppmd->domp->lock));
	ppmd->level = PM_LEVEL_UNKNOWN;
}
