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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <sys/types.h>
#include <sys/callb.h>
#include <sys/stat.h>
#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/modctl.h>
#include <sys/ddi_impldefs.h>
#include <sys/kmem.h>
#include <sys/devops.h>
#include <sys/cyclic.h>
#include <sys/uadmin.h>
#include <sys/rmc_comm_dp.h>
#include <sys/rmc_comm_drvintf.h>
#include <sys/lom_io.h>


#define	NTWDT_RSP_WAIT_TIME	10000
#define	NTWDTIOCSTATE	_IOWR('a',  0xa, ntwdt_data_t)
#define	NTWDTIOCCTL	_IOW('a',  0xb, ntwdt_data_t)

#define	NTWDT_SUCCESS	0
#define	NTWDT_FAILURE	1

typedef struct {
	uint32_t	ntwdt_wd1;
	uint8_t		ntwdt_wd2;
} ntwdt_data_t;

/*
 * NTWDT watchdog driver.
 * This driver will have a dedicated cyclic that will pat
 * the hardware watchdog every time it's fired. Also, it
 * will check whether user was able to pat the virtual
 * watchdog, and if so reset the remaining time to the
 * full timeout value. Else it will simply decrement the
 * cyclic interval from time remaining. If time remaining drops
 * below cyclic interval, it will force a panic dump and reset.
 */

enum {
	NTWDT_ACTION_NONE,
	NTWDT_ACTION_XIR,
	NTWDT_ACTION_RESET,
	NTWDT_ACTION_INVALID
} ntwdt_action_val_t;
/*
 * Time periods, in nanoseconds
 */
#define	NTWDT_MAX_TIMEVAL	(180*60) /* in seconds */

#define	NTWDT_PROP_MAX_LEN		16
#define	NTWDT_ACTION_NONE_STR	"none"
#define	NTWDT_ACTION_XIR_STR	"xir"
#define	NTWDT_ACTION_RESET_STR	"reset"

#define	NTWDT_BOOTFAIL_POWEROFF	"poweroff"
#define	NTWDT_BOOTFAIL_POWERCYCLE	"powercycle"

/*
 * Property Names
 */
#define	NTWDT_WDT_ACTION_PROP "ntwdt-autorestart"
#define	NTWDT_BOOT_TIMEOUT_PROP	"ntwdt-boottimeout"
#define	NTWDT_BOOT_RESTART_PROP "ntwdt-bootrestart"
#define	NTWDT_MAX_BOOTFAIL_PROP "ntwdt-maxbootfail"
#define	NTWDT_BOOTFAIL_RECOVERY_PROP "ntwdt-bootfailrecovery"
#define	NTWDT_XIR_TIMEOUT_PROP "ntwdt-xirtimeout"

/*
 * Names of corresponding ALOM config variables
 */
#define	NTWDT_ALOM_WDT_TIMEOUT "sys_wdttimeout"
#define	NTWDT_ALOM_WDT_ACTION "sys_autorestart"
#define	NTWDT_ALOM_BOOT_TIMEOUT "sys_boottimeout"
#define	NTWDT_ALOM_BOOT_RESTART "sys_bootrestart"
#define	NTWDT_ALOM_MAX_BOOTFAIL "sys_maxbootfail"
#define	NTWDT_ALOM_BOOTFAIL_RECOVERY "sys_bootfailrecovery"
#define	NTWDT_ALOM_XIR_TIMEOUT "sys_xirtimeout"


#define	NTWDT_DEFAULT_BOOT_TIMEOUT	(15)

/*
 * Hardware flags for PMU watchdog register (CFh-CCh)
 */
#define	NTWDT_ENABLE_INIT_RESET_BIT	(27)
#define	NTWDT_ENABLE_NMI_RESET_BIT	(26)
#define	NTWDT_ENABLE_SYS_RESET_BIT	(25)
#define	NTWDT_WATCHDOG_RESET_BIT	(8)

#define	NTWDT_TIMEBASE_MASK		(0xC0)
#define	NTWDT_TIMECOUNT_MASK	(0x3F)
#define	NTWDT_WATCHDOG2_STATUS	(0x20)

#define	NTWDT_CYCLIC_CHK_PERCENT	(20)
#define	CHECK_BIT(X, BIT) ((X) & ((uint32_t)1 << (BIT)))
#define	SET_BIT(X, BIT) ((X) |= ((uint32_t)1 << (BIT)))
#define	CLEAR_BIT(X, BIT) ((X) &= ~((uint32_t)1 << (BIT)))

#define	RESET_ENABLED(X)	CHECK_BIT(X, NTWDT_ENABLE_NMI_RESET_BIT)
#define	ENABLE_RESET(X)		SET_BIT(X, NTWDT_ENABLE_NMI_RESET_BIT)
#define	DISABLE_RESET(X)	CLEAR_BIT(X, NTWDT_ENABLE_NMI_RESET_BIT)

#define	PAT_WDOG(X)	SET_BIT(X, NTWDT_WATCHDOG_RESET_BIT)

/*
 * convert timeout specified in 100ms multiple to nanosec
 */
#define	NTWDT_TIMEOUT_TO_NANOSEC(X)	(hrtime_t)((X) * 100 * MICROSEC)

static	dev_info_t	*ntwdt_dip;

typedef struct {
	callb_id_t	ntwdt_panic_cb;
} ntwdt_callback_ids_t;

static ntwdt_callback_ids_t ntwdt_callback_ids;
static int ntwdt_disable_timeout_action = 0;

/*
 * State of watchdog
 */
typedef struct {
	kmutex_t	ntwdt_run_mutex;
	int		ntwdt_watchdog_enabled; /* Whether watchdog enabled */
	int		ntwdt_reset_enabled; /* Whether reset enabled */
	int		ntwdt_timer_running; /* Whether watchdog running */
	int		ntwdt_wdog_triggered; /* Whether watchdog triggered */
	uint32_t	ntwdt_boot_timeout; /* boot timeout in min */
	uint8_t		ntwdt_watchdog_action; /* watchdog action */
	uint32_t	ntwdt_watchdog_timeout;	/* timeout in 1 sec multiple */
	hrtime_t	ntwdt_time_remaining; /* Time remining before expiry */
	hrtime_t	ntwdt_cyclic_interval;	/* cyclic interval */
	cyc_handler_t	ntwdt_cycl_hdlr;
	cyc_time_t	ntwdt_cycl_time;
} ntwdt_runstate_t;

typedef struct {
	kmutex_t		ntwdt_mutex;
	dev_info_t		*ntwdt_dip;
	uint32_t		*ntwdt_reg1;
	ddi_acc_handle_t	ntwdt_reg1_handle;
	uint8_t			*ntwdt_reg2;
	ddi_acc_handle_t	ntwdt_reg2_handle;
	int		ntwdt_open_flag;	/* Whether opened */
	ntwdt_runstate_t	*ntwdt_run_state;
	cyclic_id_t		ntwdt_cycl_id;
} ntwdt_state_t;

static void *ntwdt_statep;

#define	getstate(minor)	\
		((ntwdt_state_t *)ddi_get_soft_state(ntwdt_statep, (minor)))


extern int	rmclomv_watchdog_mode;
extern void	pmugpio_watchdog_pat();

static int ntwdt_attach(dev_info_t *dip, ddi_attach_cmd_t cmd);
static int ntwdt_detach(dev_info_t *dip, ddi_detach_cmd_t cmd);
static int ntwdt_info(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg,
		void **result);

static int	ntwdt_open(dev_t *, int, int, cred_t *);
static int	ntwdt_close(dev_t, int, int, cred_t *);
static int	ntwdt_ioctl(dev_t, int, intptr_t, int, cred_t *, int *);

static void ntwdt_reprogram_wd(ntwdt_state_t *, int);
static boolean_t ntwdt_panic_cb(void *arg, int code);
static void ntwdt_start_timer(ntwdt_state_t *);
static void ntwdt_stop_timer(void *);
static void ntwdt_add_callbacks(ntwdt_state_t *ntwdt_ptr);
static void ntwdt_remove_callbacks();
static void ntwdt_cyclic_pat(void *arg);
static void	ntwdt_enforce_timeout(ntwdt_state_t *);
#ifdef	DEBUG
static int ntwdt_get_watchdog(uint8_t *enable);
#endif
static int ntwdt_set_watchdog(uint8_t enable);
static void ntwdt_pat_watchdog();
static int ntwdt_set_cfgvar(char *name, char *val);
static void ntwdt_set_cfgvar_noreply(char *name, char *val);
static int ntwdt_get_cfgvar(char *name, char *val);
static void ntwdt_set_cyclic_interval(ntwdt_state_t *, uint32_t);
static int ntwdt_read_props(dev_info_t *dip);
static int atoi(const char *p);


struct cb_ops ntwdt_cb_ops = {
	ntwdt_open,	/* open  */
	ntwdt_close,	/* close */
	nulldev,	/* strategy */
	nulldev,	/* print */
	nulldev,	/* dump */
	nulldev,	/* read */
	nulldev,	/* write */
	ntwdt_ioctl,	/* ioctl */
	nulldev,	/* devmap */
	nulldev,	/* mmap */
	nulldev,	/* segmap */
	nochpoll,	/* poll */
	ddi_prop_op,	/* cb_prop_op */
	NULL,		/* streamtab  */
	D_MP | D_NEW
};

static struct dev_ops ntwdt_ops = {
	DEVO_REV,		/* Devo_rev */
	0,			/* Refcnt */
	ntwdt_info,		/* Info */
	nulldev,		/* Identify */
	nulldev,		/* Probe */
	ntwdt_attach,		/* Attach */
	ntwdt_detach,		/* Detach */
	nodev,			/* Reset */
	&ntwdt_cb_ops,		/* Driver operations */
	0,			/* Bus operations */
	NULL,			/* Power */
	ddi_quiesce_not_needed,		/* quiesce */
};

static struct modldrv modldrv = {
	&mod_driverops, 		/* This one is a driver */
	"ntwdt driver",			/* Name of the module. */
	&ntwdt_ops,			/* Driver ops */
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modldrv, NULL
};

int
_init(void)
{
	int error = 0;

	/* Initialize the soft state structures */
	if ((error = ddi_soft_state_init(&ntwdt_statep,
	    sizeof (ntwdt_state_t), 1)) != 0) {
		return (error);
	}

	/* Install the loadable module */
	if ((error = mod_install(&modlinkage)) != 0) {
		ddi_soft_state_fini(&ntwdt_statep);
	}
	return (error);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

int
_fini(void)
{
	int error;

	error = mod_remove(&modlinkage);
	if (error == 0) {
		/* Release per module resources */
		ddi_soft_state_fini(&ntwdt_statep);
	}
	return (error);
}

static int
ntwdt_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	int		instance, ret;
	ntwdt_state_t	*ntwdt_ptr = NULL;
	ntwdt_runstate_t *ntwdt_state = NULL;
	cyc_handler_t *hdlr = NULL;
	uint32_t wd_timeout = 0;
	char wdt_timeout[50];

	switch (cmd) {
	case DDI_ATTACH:
		break;
	case DDI_RESUME:
		return (DDI_SUCCESS);
	default:
		return (DDI_FAILURE);
	}

	/*
	 * Just return if watchdog mode is not app
	 */
	if (!rmclomv_watchdog_mode)
		return (DDI_SUCCESS);

	/* Get the instance and create soft state */
	instance = ddi_get_instance(dip);
	if (ddi_soft_state_zalloc(ntwdt_statep, instance) != 0) {
		return (DDI_FAILURE);
	}

	ntwdt_ptr = ddi_get_soft_state(ntwdt_statep, instance);
	if (ntwdt_ptr == NULL) {
		return (DDI_FAILURE);
	}

	ntwdt_ptr->ntwdt_dip = dip;
	ntwdt_ptr->ntwdt_open_flag = 0;
	ntwdt_ptr->ntwdt_cycl_id = CYCLIC_NONE;
	mutex_init(&ntwdt_ptr->ntwdt_mutex, NULL, MUTEX_DRIVER, NULL);

	ntwdt_state = ntwdt_ptr->ntwdt_run_state =
	    kmem_zalloc(sizeof (ntwdt_runstate_t), KM_SLEEP);
	/*
	 * Initialize the watchdog structure
	 */
	mutex_init(&ntwdt_state->ntwdt_run_mutex, NULL,
	    MUTEX_DRIVER, NULL);
	/*
	 * Initialize these from the hardware state
	 */
	ntwdt_state->ntwdt_watchdog_enabled = 0;
	ntwdt_state->ntwdt_reset_enabled = 0;
	ntwdt_state->ntwdt_watchdog_timeout = 0;

	ntwdt_state->ntwdt_timer_running = 0;
	ntwdt_state->ntwdt_cyclic_interval = NANOSEC;
	ntwdt_state->ntwdt_time_remaining = 0;
	ntwdt_state->ntwdt_wdog_triggered = 0;
	hdlr = &ntwdt_state->ntwdt_cycl_hdlr;
	hdlr->cyh_level = CY_LOW_LEVEL;
	hdlr->cyh_func = ntwdt_cyclic_pat;
	hdlr->cyh_arg = (void *)ntwdt_ptr;

	ntwdt_dip = dip;

	/*
	 * Initialize callbacks for various system events, e.g. panic
	 */
	ntwdt_add_callbacks(ntwdt_ptr);

	/*
	 * Retrieve the watchdog status from ALOM, and initialize the
	 * timeout value
	 */
	ret = ntwdt_get_cfgvar(NTWDT_ALOM_WDT_TIMEOUT, wdt_timeout);
	if (ret == NTWDT_SUCCESS)
		wd_timeout = atoi(wdt_timeout);
	ntwdt_state->ntwdt_watchdog_timeout = wd_timeout;
	ntwdt_set_cyclic_interval(ntwdt_ptr, wd_timeout);

	/*
	 * Read and set the properties in ALOM
	 */
	(void) ntwdt_read_props(dip);
	/*
	 * Create minor node.  The minor device number, inst, has no
	 * meaning.  The model number above, which will be added to
	 * the device's state, is used to direct peculiar behavior.
	 */
	if (ddi_create_minor_node(dip, "lom", S_IFCHR, 0,
	    DDI_PSEUDO, NULL) == DDI_FAILURE) {
		cmn_err(CE_NOTE, "minor number creation failed");
		goto attach_failed;
	}

	/* Display information in the banner */
	ddi_report_dev(dip);

	return (DDI_SUCCESS);

attach_failed:
	/* Free soft state, if allocated. remove minor node if added earlier */
	if (ntwdt_ptr) {
		mutex_destroy(&ntwdt_ptr->ntwdt_mutex);
		mutex_destroy(&ntwdt_state->ntwdt_run_mutex);
		if (ntwdt_state)
			kmem_free(ntwdt_state, sizeof (ntwdt_runstate_t));
		ddi_soft_state_free(ntwdt_statep, instance);
	}

	ddi_remove_minor_node(dip, NULL);

	return (DDI_FAILURE);
}

/* ARGSUSED */
static int
ntwdt_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	/* Pointer to soft state */
	int instance = ddi_get_instance(dip);
	ntwdt_state_t	*ntwdt_ptr = NULL;

	if (!rmclomv_watchdog_mode)
		return (DDI_SUCCESS);

	ntwdt_ptr = ddi_get_soft_state(ntwdt_statep, instance);
	if (ntwdt_ptr == NULL) {
		return (DDI_FAILURE);
	}

	switch (cmd) {
	case DDI_SUSPEND:
		return (DDI_SUCCESS);
	case DDI_DETACH:
		ddi_remove_minor_node(dip, NULL);
		ntwdt_stop_timer((void *)ntwdt_ptr);
		ntwdt_remove_callbacks();
		mutex_destroy(&ntwdt_ptr->ntwdt_mutex);
		mutex_destroy(&ntwdt_ptr->ntwdt_run_state->ntwdt_run_mutex);
		kmem_free(ntwdt_ptr->ntwdt_run_state,
		    sizeof (ntwdt_runstate_t));
		ntwdt_ptr->ntwdt_run_state = NULL;
		ddi_soft_state_free(ntwdt_statep, instance);
		return (DDI_SUCCESS);

	default:
		return (DDI_FAILURE);
	}
}

/* ARGSUSED */
static int
ntwdt_info(dev_info_t *dip, ddi_info_cmd_t infocmd,
		void *arg, void **result)
{
	dev_t dev;
	int instance, error;

	switch (infocmd) {
	case DDI_INFO_DEVT2DEVINFO:
		*result = (void *)ntwdt_dip;
		error = DDI_SUCCESS;
		break;
	case DDI_INFO_DEVT2INSTANCE:
		dev = (dev_t)arg;
		instance = getminor(dev);
		*result = (void *)(uintptr_t)instance;
		error = DDI_SUCCESS;
		break;
	default:
		error = DDI_FAILURE;
	}
	return (error);
}

/* ARGSUSED */
static int
ntwdt_open(dev_t *devp, int flag, int otyp, cred_t *credp)
{
	int	inst = getminor(*devp);
	int ret = 0;
	ntwdt_state_t	*ntwdt_ptr = getstate(inst);

	if (ntwdt_ptr == NULL)
		return (ENXIO);

	/*
	 * ensure caller is a privileged process
	 * This check is being put to avoid
	 * potential conflict with lw8, which
	 * uses permission 644 for the device node
	 */
	if (drv_priv(credp) != 0)
		return (EPERM);

	mutex_enter(&ntwdt_ptr->ntwdt_mutex);
	if (ntwdt_ptr->ntwdt_open_flag) {
		ret = EAGAIN;
		goto end;
	}
	ntwdt_ptr->ntwdt_open_flag = 1;

end:
	mutex_exit(&ntwdt_ptr->ntwdt_mutex);
	return (ret);
}

/* ARGSUSED */
static int
ntwdt_close(dev_t dev, int flag, int otyp, cred_t *credp)
{
	int	inst = getminor(dev);
	ntwdt_state_t	*ntwdt_ptr = getstate(inst);

	if (ntwdt_ptr == NULL)
		return (ENXIO);

	mutex_enter(&ntwdt_ptr->ntwdt_mutex);
	ntwdt_ptr->ntwdt_open_flag = 0;
	mutex_exit(&ntwdt_ptr->ntwdt_mutex);

	return (0);
}

/* ARGSUSED */
static int
ntwdt_ioctl(dev_t dev, int cmd, intptr_t arg, int mode,
		cred_t *credp, int *rvalp)
{
	int		inst = getminor(dev);
	int ret, retval = 0;
	uint32_t wd_timeout;
	uint8_t		enable, action;
	ntwdt_state_t	*ntwdt_ptr = NULL;
	ntwdt_runstate_t *ntwdt_state;
	hrtime_t wdog_interval;
	char wdt_timeout[50];

	if ((ntwdt_ptr = getstate(inst)) == NULL)
		return (ENXIO);

	ntwdt_state = ntwdt_ptr->ntwdt_run_state;
	mutex_enter(&ntwdt_ptr->ntwdt_mutex);

	switch (cmd) {
		case LOMIOCDOGSTATE:
		{
			lom_dogstate_t lom_dogstate;
			lom_dogstate.reset_enable =
			    ntwdt_state->ntwdt_reset_enabled;
			lom_dogstate.dog_enable =
			    ntwdt_state->ntwdt_watchdog_enabled;
			lom_dogstate.dog_timeout =
			    ntwdt_state->ntwdt_watchdog_timeout;

#ifdef	DEBUG
			/*
			 * Get the watchdog state from ALOM
			 */
			(void) ntwdt_get_watchdog(&enable);
			if (enable != ntwdt_state->ntwdt_watchdog_enabled)
				cmn_err(CE_WARN, "ALOM state and host states "
				    " of watchdog are not consistent");
#endif

			if (ddi_copyout((caddr_t)&lom_dogstate, (caddr_t)arg,
			    sizeof (lom_dogstate_t), mode) != 0) {
				retval = EFAULT;
				goto end;
			}
		}
		break;

		case LOMIOCDOGCTL:
		{
			lom_dogctl_t lom_dogctl;

			if (ddi_copyin((caddr_t)arg, (caddr_t)&lom_dogctl,
			    sizeof (lom_dogctl_t), mode) != 0) {
				retval = EFAULT;
				goto end;
			}

			/*
			 * Ignore request to enable reset while disabling wdog
			 */
			if (!lom_dogctl.dog_enable &&
			    lom_dogctl.reset_enable) {
				retval = EINVAL;
				break;
			}
			action = ntwdt_state->ntwdt_watchdog_action;
			/*
			 * Get the current timeout value from ALOM. Note that
			 * we need to ignore the action set in ALOM, since
			 * the action which is initially set in solaris thru
			 * the config file will prevail
			 */
			ret = ntwdt_get_cfgvar(NTWDT_ALOM_WDT_TIMEOUT,
			    wdt_timeout);
			if (ret == NTWDT_SUCCESS)
				wd_timeout = atoi(wdt_timeout);
			ntwdt_state->ntwdt_watchdog_timeout = wd_timeout;
			ntwdt_set_cyclic_interval(ntwdt_ptr, wd_timeout);

			ntwdt_state->ntwdt_reset_enabled =
			    lom_dogctl.reset_enable;
			/*
			 * If no change in watchdog_enabled, do nothing.
			 */
			if (ntwdt_state->ntwdt_watchdog_enabled ==
			    lom_dogctl.dog_enable) {
				/*
				 * Check if this is a request to disable watch-
				 * dog, then explicitly disable watchdog. This
				 * is required to disable boot timer after
				 * system boot.
				 */
				if (!lom_dogctl.dog_enable) {
					enable = DP_USER_WATCHDOG_DISABLE;
					(void) ntwdt_set_watchdog(enable);
				}
				break;
			}

			ntwdt_state->ntwdt_watchdog_enabled =
			    lom_dogctl.dog_enable;

			/*
			 * If timeout value is uninitialized, ignore a request
			 * to enable watchdog
			 */
			if ((wd_timeout == 0) &&
			    ntwdt_state->ntwdt_watchdog_enabled)
				break;

			/*
			 * This must be a valid request to enable to disable
			 * the watchdog
			 */
			if (ntwdt_state->ntwdt_watchdog_enabled) {
				enable = DP_USER_WATCHDOG_ENABLE;
				ntwdt_start_timer(ntwdt_ptr);
			} else {
				enable = DP_USER_WATCHDOG_DISABLE;
				ntwdt_stop_timer((void *)ntwdt_ptr);
			}
			(void) ntwdt_set_watchdog(enable);
		}
		break;

		case LOMIOCDOGTIME:
		{
			uint32_t	lom_dogtime;

			if (ddi_copyin((caddr_t)arg, (caddr_t)&lom_dogtime,
			    sizeof (uint32_t), mode) != 0) {
				retval = EFAULT;
				goto end;
			}

			/*
			 * Disallow timeout value of 0
			 */
			if ((lom_dogtime == 0) ||
			    (lom_dogtime > NTWDT_MAX_TIMEVAL)) {
				retval = EINVAL;
				break;
			}

			/*
			 * If watchdog is currently running, stop the cyclic
			 * and restart
			 */
			if (ntwdt_state->ntwdt_timer_running) {
				ntwdt_stop_timer((void *)ntwdt_ptr);
			}

			ntwdt_state->ntwdt_watchdog_timeout = lom_dogtime;
			ntwdt_set_cyclic_interval(ntwdt_ptr, lom_dogtime);

			/*
			 * Set the timeout in ALOM, which will restart
			 * the ALOM timer
			 */
			(void) sprintf(wdt_timeout, "%d", lom_dogtime);
			(void) ntwdt_set_cfgvar(NTWDT_ALOM_WDT_TIMEOUT,
			    wdt_timeout);
			/*
			 * If watchdog is disabled, do nothing
			 */
			if (!ntwdt_state->ntwdt_watchdog_enabled) {
				break;
			}

			/*
			 * Restart the host cyclics
			 */
			ntwdt_start_timer(ntwdt_ptr);
		}
		break;

		case LOMIOCDOGPAT:
		{
			/*
			 * If watchdog is not enabled, break
			 */
			if (!(ntwdt_state->ntwdt_watchdog_enabled &&
			    ntwdt_state->ntwdt_timer_running))
				break;

			if (ntwdt_state->ntwdt_wdog_triggered)
				break;
			/*
			 * Set the time remaining to the timeout interval
			 */
			wdog_interval = ntwdt_state->ntwdt_watchdog_timeout;
			wdog_interval *= NANOSEC;
			ntwdt_state->ntwdt_time_remaining = wdog_interval;
		}
		break;

	case NTWDTIOCSTATE:
		{
			ntwdt_data_t	ntwdt_data;
			char action_str[50];

			wd_timeout = 0;
			action = 0;
			ret = ntwdt_get_cfgvar(NTWDT_ALOM_WDT_TIMEOUT,
			    wdt_timeout);
			if (ret == NTWDT_SUCCESS)
				wd_timeout = atoi(wdt_timeout);

			ret = ntwdt_get_cfgvar(NTWDT_ALOM_WDT_ACTION,
			    action_str);
			if (strcmp(action_str, NTWDT_ACTION_NONE_STR) == 0)
				action = NTWDT_ACTION_NONE;
			else
			if (strcmp(action_str, NTWDT_ACTION_XIR_STR) == 0)
				action = NTWDT_ACTION_XIR;
			if (strcmp(action_str, NTWDT_ACTION_RESET_STR) == 0)
				action = NTWDT_ACTION_RESET;

			bzero((caddr_t)&ntwdt_data, sizeof (ntwdt_data));

			if (retval != NTWDT_SUCCESS) {
				retval = EIO;
				goto end;
			}
			ntwdt_data.ntwdt_wd1 = (uint32_t)wd_timeout;
			ntwdt_data.ntwdt_wd2 = (uint8_t)action;

			if (ddi_copyout((caddr_t)&ntwdt_data, (caddr_t)arg,
			    sizeof (ntwdt_data_t), mode) != 0) {
				retval = EFAULT;
				goto end;
			}

		}
		break;

	case NTWDTIOCCTL:
		{
			ntwdt_data_t	ntwdt_data;
			char *actr;

			if (ddi_copyin((caddr_t)arg, (caddr_t)&ntwdt_data,
			    sizeof (ntwdt_data_t), mode) != 0) {
				retval = EFAULT;
				goto end;
			}

			(void) sprintf(wdt_timeout, "%d",
			    (int)ntwdt_data.ntwdt_wd1);
			action = (int)ntwdt_data.ntwdt_wd2;
			actr = NTWDT_ACTION_XIR_STR;
			if (action == NTWDT_ACTION_NONE)
				actr = NTWDT_ACTION_NONE_STR;
			else
			if (action == NTWDT_ACTION_XIR)
				actr = NTWDT_ACTION_XIR_STR;
			else
			if (action == NTWDT_ACTION_RESET)
				actr = NTWDT_ACTION_RESET_STR;

			ret = ntwdt_set_cfgvar(NTWDT_ALOM_WDT_TIMEOUT,
			    wdt_timeout);
			ret = ntwdt_set_cfgvar(NTWDT_ALOM_WDT_ACTION, actr);

			if (ret != NTWDT_SUCCESS)
				retval = EIO;
		}
		break;

	default:
		retval = EINVAL;
		break;
	}

end:
	mutex_exit(&ntwdt_ptr->ntwdt_mutex);

	return (retval);
}

/*
 * Cyclic that will be running if a watchdog is enabled
 */
static void
ntwdt_cyclic_pat(void *arg)
{
	ntwdt_state_t *ntwdt_ptr = (ntwdt_state_t *)arg;
	ntwdt_runstate_t *ntwdt_state = ntwdt_ptr->ntwdt_run_state;

	if (!ntwdt_state->ntwdt_timer_running ||
	    (ntwdt_ptr->ntwdt_cycl_id == CYCLIC_NONE))
		return;
	/*
	 * Make sure watchdog is enabled
	 */
	if (!ntwdt_state->ntwdt_watchdog_enabled)
		return;

	mutex_enter(&ntwdt_state->ntwdt_run_mutex);

	/*
	 * Pat the hardware watchdog
	 */
	ntwdt_pat_watchdog();

	/*
	 * Check whether virtual watchdog already expired, else
	 * decrement the timer
	 */
	if (ntwdt_state->ntwdt_time_remaining <
	    ntwdt_state->ntwdt_cyclic_interval) {
		/*
		 * First reprogram the timeout so that a panic can
		 * happen safely, then take timeout action
		 */
		if (!ntwdt_disable_timeout_action)
			ntwdt_reprogram_wd(ntwdt_ptr, 1);
		ntwdt_state->ntwdt_wdog_triggered = 1;
		if (ntwdt_state->ntwdt_reset_enabled)
			ntwdt_enforce_timeout(ntwdt_ptr);
		else
			ntwdt_state->ntwdt_watchdog_enabled = 0;
		/*
		 * Schedule timeout to stop cyclic
		 */
		(void) timeout(ntwdt_stop_timer, ntwdt_ptr, 0);

	} else {
		ntwdt_state->ntwdt_time_remaining -=
		    ntwdt_state->ntwdt_cyclic_interval;
	}
	mutex_exit(&ntwdt_state->ntwdt_run_mutex);
}

/*
 * This function will be invoked by the panic callback
 */
static void
ntwdt_reprogram_wd(ntwdt_state_t *ntwdt_ptr, int enable)
{
	ntwdt_runstate_t *ntwdt_state = ntwdt_ptr->ntwdt_run_state;
	char wdt_timeout[50];

	(void) sprintf(wdt_timeout, "%d", ntwdt_state->ntwdt_boot_timeout);

	/*
	 * Re-program watchdog hardware with the boot-timeout value
	 * This will re-program the hardware and enable watchdog only
	 * if watchdog was enabled and ticking, otherwise watchdog will
	 * remain disabled
	 */
	if (ntwdt_state->ntwdt_watchdog_enabled &&
	    ntwdt_state->ntwdt_reset_enabled &&
	    ntwdt_state->ntwdt_timer_running && enable) {
		if (panicstr != NULL)
			ntwdt_set_cfgvar_noreply(NTWDT_ALOM_WDT_TIMEOUT,
			    wdt_timeout);
		else
			(void) ntwdt_set_cfgvar(NTWDT_ALOM_WDT_TIMEOUT,
			    wdt_timeout);
	}
}

/*ARGSUSED1*/
static boolean_t
ntwdt_panic_cb(void *arg, int code)
{
	ASSERT(panicstr != NULL);
	ntwdt_reprogram_wd((ntwdt_state_t *)arg, 1);

	return (B_TRUE);
}

/*
 * Set the cyclic interval, which is 20% of the timeout
 */
static void
ntwdt_set_cyclic_interval(ntwdt_state_t *ntwdt_ptr, uint32_t wd_timeout)
{
	uint64_t wdog_interval = wd_timeout;
	ntwdt_runstate_t *ntwdt_state = ntwdt_ptr->ntwdt_run_state;

	wdog_interval *= NANOSEC;

	ntwdt_state->ntwdt_time_remaining = wdog_interval;
	ntwdt_state->ntwdt_cyclic_interval = (wdog_interval /
	    100) *  NTWDT_CYCLIC_CHK_PERCENT;
}

/*
 * Start the cyclic in response to a watchdog enable.
 * The argument passed is the interval in 100ms multiple.
 * The cyclic will program the hardware with timeout 100
 * milliseconds more than specified, and the cyclic inter-
 * val will be the same as user specified timeout.
 */
static void
ntwdt_start_timer(ntwdt_state_t *ntwdt_ptr)
{
	ntwdt_runstate_t *ntwdt_state = ntwdt_ptr->ntwdt_run_state;
	cyc_handler_t *hdlr =
	    &ntwdt_state->ntwdt_cycl_hdlr;
	cyc_time_t *when =
	    &ntwdt_state->ntwdt_cycl_time;

	when->cyt_when = 0;
	when->cyt_interval = ntwdt_state->ntwdt_cyclic_interval;

	ntwdt_state->ntwdt_wdog_triggered = 0;
	ntwdt_state->ntwdt_timer_running = 1;
	mutex_enter(&cpu_lock);
	if (ntwdt_ptr->ntwdt_cycl_id == CYCLIC_NONE)
		ntwdt_ptr->ntwdt_cycl_id = cyclic_add(hdlr, when);
	mutex_exit(&cpu_lock);
}

/*
 * Stop the cyclic in response to a watchdog disable
 */
static void
ntwdt_stop_timer(void *arg)
{
	ntwdt_state_t *ntwdt_ptr = (void *)arg;
	ntwdt_runstate_t *ntwdt_state = ntwdt_ptr->ntwdt_run_state;

	mutex_enter(&cpu_lock);
	if (ntwdt_ptr->ntwdt_cycl_id != CYCLIC_NONE)
		cyclic_remove(ntwdt_ptr->ntwdt_cycl_id);
	mutex_exit(&cpu_lock);

	ntwdt_state->ntwdt_timer_running = 0;
	ntwdt_ptr->ntwdt_cycl_id = CYCLIC_NONE;
}

/*
 * Add callbacks for panic
 */
static void
ntwdt_add_callbacks(ntwdt_state_t *ntwdt_ptr)
{
	ntwdt_callback_ids.ntwdt_panic_cb = callb_add(ntwdt_panic_cb,
	    (void *)ntwdt_ptr, CB_CL_PANIC, "ntwdt_panic_cb");
}

/*
 * Remove callbacks
 */
static void
ntwdt_remove_callbacks()
{
	(void) callb_delete(ntwdt_callback_ids.ntwdt_panic_cb);
}

/*
 * Take the specified action when a timeout happens
 */
static void
ntwdt_enforce_timeout(ntwdt_state_t *ntwdt_ptr)
{
	ntwdt_runstate_t *ntwdt_state = ntwdt_ptr->ntwdt_run_state;

	if (ntwdt_disable_timeout_action) {
		cmn_err(CE_NOTE, "OS timeout expired, taking no action");
		return;
	}

	mutex_exit(&ntwdt_state->ntwdt_run_mutex);

	if (ntwdt_state->ntwdt_watchdog_action ==
	    NTWDT_ACTION_RESET)
		(void) kadmin(A_REBOOT, AD_BOOT, NULL, kcred);
	else
		(void) kadmin(A_DUMP, AD_BOOT, NULL, kcred);
	cmn_err(CE_PANIC, "kadmin(A_DUMP/A_REBOOT, AD_BOOT) failed");
	/*NOTREACHED*/
}

/*
 * Reads the properties from driver config file and update ALOM
 * with the new values
 */
static int
ntwdt_read_props(dev_info_t *dip)
{
	int ret, instance = ddi_get_instance(dip);
	int xir_timeout, boot_timeout, bootfail_count;
	ntwdt_state_t	*ntwdt_ptr = NULL;
	ntwdt_runstate_t *ntwdt_state = NULL;
	char *propstr;
	char boottimeout_str[50];
	char wdt_action[NTWDT_PROP_MAX_LEN],
	    boot_action[NTWDT_PROP_MAX_LEN],
	    bootfail_action[NTWDT_PROP_MAX_LEN];

	bzero(wdt_action, NTWDT_PROP_MAX_LEN);
	bzero(boot_action, NTWDT_PROP_MAX_LEN);
	bzero(bootfail_action, NTWDT_PROP_MAX_LEN);

	ntwdt_ptr = ddi_get_soft_state(ntwdt_statep, instance);
	if (ntwdt_ptr == NULL) {
		return (DDI_FAILURE);
	}

	ntwdt_state = ntwdt_ptr->ntwdt_run_state;

	boot_timeout = ddi_prop_get_int(DDI_DEV_T_ANY,
	    dip, DDI_PROP_DONTPASS, NTWDT_BOOT_TIMEOUT_PROP, -1);

	if (boot_timeout != -1) {
		ntwdt_state->ntwdt_boot_timeout = boot_timeout;
		(void) sprintf(boottimeout_str, "%d", boot_timeout);
		(void) ntwdt_set_cfgvar(NTWDT_ALOM_BOOT_TIMEOUT,
		    boottimeout_str);
	} else {
		/*
		 * Retrieve the boot timeout from ALOM
		 */
		ret = ntwdt_get_cfgvar(NTWDT_ALOM_BOOT_TIMEOUT,
		    boottimeout_str);
		if (ret == NTWDT_SUCCESS)
			boot_timeout = atoi(boottimeout_str);
		else
			boot_timeout = NTWDT_DEFAULT_BOOT_TIMEOUT * 60;
		ntwdt_state->ntwdt_boot_timeout = boot_timeout;
	}

	xir_timeout = ddi_prop_get_int(DDI_DEV_T_ANY,
	    dip, DDI_PROP_DONTPASS, NTWDT_XIR_TIMEOUT_PROP, -1);

	if (xir_timeout != -1) {
		char xirtimeout_str[50];
		(void) sprintf(xirtimeout_str, "%d", xir_timeout);
		(void) ntwdt_set_cfgvar(NTWDT_ALOM_XIR_TIMEOUT,
		    xirtimeout_str);
	}

	/*
	 * Only possible values of watchdog action are "xir" or "reset"
	 */
	ntwdt_state->ntwdt_watchdog_action = NTWDT_ACTION_XIR;
	ret = ddi_prop_lookup_string(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    NTWDT_WDT_ACTION_PROP, &propstr);
	if (ret == DDI_PROP_SUCCESS) {
		if (strncmp(propstr, NTWDT_ACTION_XIR_STR,
		    sizeof (NTWDT_ACTION_XIR_STR)) == 0) {
			ntwdt_state->ntwdt_watchdog_action = NTWDT_ACTION_XIR;
			(void) strncpy(wdt_action, NTWDT_ACTION_XIR_STR,
			    sizeof (NTWDT_ACTION_XIR_STR));
		}
		else
		if (strncmp(propstr, NTWDT_ACTION_RESET_STR,
		    sizeof (NTWDT_ACTION_RESET_STR)) == 0) {
			ntwdt_state->ntwdt_watchdog_action = NTWDT_ACTION_RESET;
			(void) strncpy(wdt_action, NTWDT_ACTION_RESET_STR,
			    sizeof (NTWDT_ACTION_RESET_STR));
		}
		else
		{
			cmn_err(CE_NOTE, "driver ntwdt: invalid value for %s",
			    NTWDT_WDT_ACTION_PROP);
			ntwdt_state->ntwdt_watchdog_action =
			    NTWDT_ACTION_INVALID;
			(void) strncpy(wdt_action, NTWDT_ACTION_XIR_STR,
			    sizeof (NTWDT_ACTION_XIR_STR));
		}

		if (ntwdt_state->ntwdt_watchdog_action !=
		    NTWDT_ACTION_INVALID)
			(void) ntwdt_set_cfgvar(NTWDT_ALOM_WDT_ACTION,
			    wdt_action);
		ddi_prop_free(propstr);
	}
	if (((ret == DDI_PROP_SUCCESS) &&
	    (ntwdt_state->ntwdt_watchdog_action ==
	    NTWDT_ACTION_INVALID)) || (ret != DDI_PROP_SUCCESS)) {
		/* Retrieve the setting from ALOM */
		ret = ntwdt_get_cfgvar(NTWDT_ALOM_WDT_ACTION, wdt_action);
		if (ret == NTWDT_SUCCESS) {
			if (strncmp(wdt_action, NTWDT_ACTION_XIR_STR,
			    sizeof (NTWDT_ACTION_XIR_STR)) == 0) {
				ntwdt_state->ntwdt_watchdog_action =
				    NTWDT_ACTION_XIR;
			}
			else
			if (strncmp(wdt_action, NTWDT_ACTION_RESET_STR,
			    sizeof (NTWDT_ACTION_RESET_STR)) == 0) {
				ntwdt_state->ntwdt_watchdog_action =
				    NTWDT_ACTION_RESET;
			} else
				ntwdt_state->ntwdt_watchdog_action =
				    NTWDT_ACTION_XIR;
		}
	}

	if (ddi_prop_lookup_string(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    NTWDT_BOOT_RESTART_PROP, &propstr) == DDI_PROP_SUCCESS) {
		if (strncmp(propstr, NTWDT_ACTION_NONE_STR,
		    sizeof (NTWDT_ACTION_NONE_STR)) == 0) {
			(void) strncpy(boot_action, NTWDT_ACTION_NONE_STR,
			    sizeof (NTWDT_ACTION_NONE_STR));
			(void) ntwdt_set_cfgvar(NTWDT_ALOM_BOOT_RESTART,
			    boot_action);
		}
		else
		if (strncmp(propstr, NTWDT_ACTION_XIR_STR,
		    sizeof (NTWDT_ACTION_XIR_STR)) == 0) {
			(void) strncpy(boot_action, NTWDT_ACTION_XIR_STR,
			    sizeof (NTWDT_ACTION_XIR_STR));
			(void) ntwdt_set_cfgvar(NTWDT_ALOM_BOOT_RESTART,
			    boot_action);
		}
		else
		if (strncmp(propstr, NTWDT_ACTION_RESET_STR,
		    sizeof (NTWDT_ACTION_RESET_STR)) == 0) {
			(void) strncpy(boot_action, NTWDT_ACTION_RESET_STR,
			    sizeof (NTWDT_ACTION_RESET_STR));
			(void) ntwdt_set_cfgvar(NTWDT_ALOM_BOOT_RESTART,
			    boot_action);
		} else {
			cmn_err(CE_NOTE, "driver ntwdt: invalid value for %s",
			    NTWDT_BOOT_RESTART_PROP);
		}

		ddi_prop_free(propstr);
	}

	if (ddi_prop_lookup_string(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    NTWDT_BOOTFAIL_RECOVERY_PROP, &propstr) ==
	    DDI_PROP_SUCCESS) {
		if (strncmp(propstr, NTWDT_BOOTFAIL_POWEROFF,
		    sizeof (NTWDT_BOOTFAIL_POWEROFF)) == 0) {
			(void) strncpy(bootfail_action, NTWDT_BOOTFAIL_POWEROFF,
			    sizeof (NTWDT_BOOTFAIL_POWEROFF));
			(void) ntwdt_set_cfgvar(NTWDT_ALOM_BOOTFAIL_RECOVERY,
			    bootfail_action);
		}
		else
		if (strncmp(propstr, NTWDT_BOOTFAIL_POWERCYCLE,
		    sizeof (NTWDT_BOOTFAIL_POWERCYCLE)) == 0) {
			(void) strncpy(bootfail_action,
			    NTWDT_BOOTFAIL_POWERCYCLE,
			    sizeof (NTWDT_BOOTFAIL_POWERCYCLE));
			(void) ntwdt_set_cfgvar(NTWDT_ALOM_BOOTFAIL_RECOVERY,
			    bootfail_action);
		}
		else
		if (strncmp(propstr, NTWDT_ACTION_NONE_STR,
		    sizeof (NTWDT_ACTION_NONE_STR)) == 0) {
			(void) strncpy(bootfail_action, NTWDT_ACTION_NONE_STR,
			    sizeof (NTWDT_ACTION_NONE_STR));
			(void) ntwdt_set_cfgvar(NTWDT_ALOM_BOOTFAIL_RECOVERY,
			    bootfail_action);
		} else {
			cmn_err(CE_NOTE, "driver ntwdt: invalid value for %s",
			    NTWDT_BOOTFAIL_RECOVERY_PROP);
		}
		ddi_prop_free(propstr);
	}

	bootfail_count = ddi_prop_get_int(DDI_DEV_T_ANY,
	    dip, DDI_PROP_DONTPASS, NTWDT_MAX_BOOTFAIL_PROP, -1);
	if (bootfail_count != -1) {
		char maxbootfail_str[50];
		ntwdt_state->ntwdt_boot_timeout = boot_timeout;
		(void) sprintf(maxbootfail_str, "%d", bootfail_count);
		(void) ntwdt_set_cfgvar(NTWDT_ALOM_MAX_BOOTFAIL,
		    maxbootfail_str);
	}

	return (DDI_SUCCESS);
}

/*
 * Send a message to ALOM
 */
static int
ntwdt_send_recv_msg(int req_cmd, int resp_cmd, int req_len,
		int resp_len, intptr_t arg_req, intptr_t arg_res)
{
	rmc_comm_msg_t request, *reqp = &request;
	rmc_comm_msg_t response, *resp = &response;
	int rv = NTWDT_SUCCESS;

	bzero((caddr_t)&request, sizeof (request));
	reqp->msg_type = req_cmd;
	reqp->msg_buf = (caddr_t)arg_req;
	reqp->msg_len = req_len;

	bzero((caddr_t)&response, sizeof (response));
	resp->msg_type = resp_cmd;
	resp->msg_buf = (caddr_t)arg_res;
	resp->msg_len = resp_len;

	rv = rmc_comm_request_response(reqp, resp,
	    NTWDT_RSP_WAIT_TIME);

	if (rv != RCNOERR) {
		return (NTWDT_FAILURE);
	}

	return (NTWDT_SUCCESS);
}

/*
 * Send an urgent msg without waiting for reply,
 * e.g. during system panic
 */
static void
ntwdt_send_urgent_msg(int req_cmd, int req_len, intptr_t arg_req)
{
	rmc_comm_msg_t request, *reqp = &request;

	bzero((caddr_t)&request, sizeof (request));
	reqp->msg_type = req_cmd;
	reqp->msg_buf = (caddr_t)arg_req;
	reqp->msg_len = req_len;

	(void) rmc_comm_request_nowait(reqp, RMC_COMM_DREQ_URGENT);
}

/*
 * Set a given configuration variable in ALOM. The name
 * and value are specified as NULL terminated strings.
 */
static int
ntwdt_set_cfgvar(char *name, char *val)
{
	int ret;
	int reqlen = strlen(name) + strlen(val) + 2;
	dp_set_cfgvar_r_t cfgvar_r;
	char buffer[DP_MAX_MSGLEN];

	bzero(buffer, DP_MAX_MSGLEN);
	(void) strcpy(buffer, name);
	(void) strcpy(&buffer[strlen(name)+1], val);

	ret = ntwdt_send_recv_msg(DP_SET_CFGVAR, DP_SET_CFGVAR_R, reqlen,
	    sizeof (cfgvar_r), (intptr_t)buffer, (intptr_t)&cfgvar_r);

	return (ret);
}

static void
ntwdt_set_cfgvar_noreply(char *name, char *val)
{
	int reqlen = strlen(name) + strlen(val) + 2;
	char buffer[DP_MAX_MSGLEN];

	bzero(buffer, DP_MAX_MSGLEN);
	(void) strcpy(buffer, name);
	(void) strcpy(&buffer[strlen(name)+1], val);

	ntwdt_send_urgent_msg(DP_SET_CFGVAR, reqlen, (intptr_t)buffer);
}

/*
 * Get value of a configuration variable from ALOM
 */
static int
ntwdt_get_cfgvar(char *name, char *val)
{
	int ret;
	int reqlen = strlen(name) + 1;
	char buffer[DP_MAX_MSGLEN], replybuf[DP_MAX_MSGLEN];

	bzero(buffer, DP_MAX_MSGLEN);
	(void) strcpy(buffer, name);

	ret = ntwdt_send_recv_msg(DP_GET_CFGVAR, DP_GET_CFGVAR_R, reqlen,
	    DP_MAX_MSGLEN, (intptr_t)buffer, (intptr_t)&replybuf);

	/*
	 * Check the status value
	 */
	if (*(int *)replybuf != 0)
		return (NTWDT_FAILURE);

	/*
	 * Copy the variable value to buffer
	 */
	(void) strcpy(val,
	    (char *)(&((char *)replybuf)[sizeof (dp_get_cfgvar_r_t)]));

	return (ret);

}

/*
 * Enable/disable watchdog
 */
static int
ntwdt_set_watchdog(uint8_t enable)
{
	int ret = NTWDT_SUCCESS;
	dp_set_user_watchdog_t user_set_wdt;
	dp_set_user_watchdog_r_t user_set_wdt_r;

	user_set_wdt.enable = enable;

	ret = ntwdt_send_recv_msg(DP_SET_USER_WATCHDOG,
	    DP_SET_USER_WATCHDOG_R, sizeof (user_set_wdt),
	    sizeof (user_set_wdt_r), (intptr_t)&user_set_wdt,
	    (intptr_t)&user_set_wdt_r);

	if ((ret == NTWDT_SUCCESS) &&
	    (user_set_wdt_r.status != DP_USER_WDT_OK))
		ret = NTWDT_FAILURE;

	return (ret);
}

#ifdef	DEBUG
/*
 * Get current watchdog status
 */
static int
ntwdt_get_watchdog(uint8_t *enable)
{
	int ret;
	dp_get_user_watchdog_r_t user_get_wdt_r;

	ret = ntwdt_send_recv_msg(DP_GET_USER_WATCHDOG,
	    DP_GET_USER_WATCHDOG_R, 0, sizeof (user_get_wdt_r),
	    (intptr_t)NULL, (intptr_t)&user_get_wdt_r);

	if (ret == NTWDT_SUCCESS) {
		*enable = user_get_wdt_r.enable;

		return (NTWDT_SUCCESS);
	}

	return (NTWDT_FAILURE);
}
#endif

/*
 * Toggle the PMU GPIO pin to let ALOM know that watchdog is patted
 */
static void
ntwdt_pat_watchdog()
{
	pmugpio_watchdog_pat();
}

/*
 * A routine to convert a number (represented as a string) to
 * the integer value it represents.
 */

static int
isdigit(int ch)
{
	return (ch >= '0' && ch <= '9');
}

#define	isspace(c)	((c) == ' ' || (c) == '\t' || (c) == '\n')
#define	bad(val)	(val == NULL || !isdigit(*val))

static int
atoi(const char *p)
{
	int n;
	int c, neg = 0;

	if (!isdigit(c = *p)) {
		while (isspace(c))
			c = *++p;
		switch (c) {
			case '-':
				neg++;
				/* FALLTHROUGH */
			case '+':
			c = *++p;
		}
		if (!isdigit(c))
			return (0);
	}
	for (n = '0' - c; isdigit(c = *++p); ) {
		n *= 10; /* two steps to avoid unnecessary overflow */
		n += '0' - c; /* accum neg to avoid surprises at MAX */
	}
	return (neg ? n : -n);
}
