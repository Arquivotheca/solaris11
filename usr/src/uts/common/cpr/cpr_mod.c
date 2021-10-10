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
 * Copyright (c) 1992, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * System call to checkpoint and resume the currently running kernel
 */
#include <sys/types.h>
#include <sys/errno.h>
#include <sys/modctl.h>
#include <sys/syscall.h>
#include <sys/cred.h>
#include <sys/uadmin.h>
#include <sys/cmn_err.h>
#include <sys/systm.h>
#include <sys/cpr.h>
#include <sys/cpr_priv.h>
#include <sys/swap.h>
#include <sys/vfs.h>
#include <sys/autoconf.h>
#include <sys/machsystm.h>

extern struct mod_ops mod_miscops;

static struct modlmisc modlmisc = {
	&mod_miscops, "checkpoint resume"
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modlmisc, NULL
};

char _depends_on[] = "misc/bootdev";	/* i_devname_to_promname() */

int cpr_reusable_mode = 0;

kmutex_t	cpr_slock;	/* cpr serial lock */
cpr_t		cpr_state;
int		cpr_debug;
int		cpr_test_mode; /* true if called via uadmin testmode */
int		cpr_test_point = LOOP_BACK_NONE;	/* cpr test point */

major_t		cpr_device = 0;	/* major number for suspending on one device */

/*
 * All the loadable module related code follows
 */
int
_init(void)
{
	register int e;

	if ((e = mod_install(&modlinkage)) == 0) {
		mutex_init(&cpr_slock, NULL, MUTEX_DEFAULT, NULL);
	}
	return (e);
}

int
_fini(void)
{
	register int e;

	if ((e = mod_remove(&modlinkage)) == 0) {
		mutex_destroy(&cpr_slock);
	}
	return (e);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

static
int
atoi(char *p)
{
	int	i;

	i = (*p++ - '0');

	while (*p != '\0')
		i = 10 * i + (*p++ - '0');

	return (i);
}

/*
 * These are legacy options, and *only* supported on
 * SPARC platforms, but left here should they be needed
 * for x64.
 */

static const char noswapstr[] = "reusable statefile requires "
	"that no swap area be configured.\n";

static const char blockstr[] = "reusable statefile must be "
	"a block device.\n";
static const char normalfmt[] = "cannot run normal "
	"checkpoint/resume when in reusable statefile mode. "
	"use uadmin A_FREEZE AD_REUSEFINI (uadmin %d %d) "
	"to exit reusable statefile mode.\n";
static const char modefmt[] = "%s in reusable mode.\n";

int
cpr(int fcn, void *mdep)
{
	register int rc = 0;

	/*
	 * First, reject commands that we don't (yet) support on this arch.
	 * This is easier to understand broken out like this than groking
	 * through the second switch below (plus, here we can define
	 * different capabilities for different architectures without
	 * putting in a bunch of #ifdef's).
	 */
	if ((rc = i_cpr_checkargs(fcn, mdep)) != 0)
		return (rc);

	/*
	 * If we are legitimately entering an S5 state, there is
	 * fundamentally no reason to save anything, as this is a
	 * poweroff.  So call kadmin with a poweroff.
	 * Note that SYSTEM_POWER_S5 can only be set if it is
	 * a supported method by the administrative tools (or if
	 * purposely overridden).
	 */
	if (pm_cpr_state == SYSTEM_POWER_S5) {
		extern cred_t *kcred;
		(void) kadmin(A_SHUTDOWN, AD_POWEROFF, mdep, kcred);
	}

	/*
	 * If the target state is S0, then suspend has been specifically
	 * denied.  However, this isn't an error condition, as we are
	 * (or should be) already in an S0 state.  So, return success.
	 */
	if (pm_cpr_state == SYSTEM_POWER_S0) {
		return (0);
	}

	/*
	 * Need to know if we're in reusable mode, but we will likely have
	 * rebooted since REUSEINIT, so we have to get the info from the
	 * file system
	 */
	if (!cpr_reusable_mode)
		cpr_reusable_mode = cpr_get_reusable_mode();

	cpr_forget_cprconfig();

	/*
	 * We know that fcn is valid for this platform, so do additional
	 * checks and setup for specific fcn's.
	 */
	switch (fcn) {

	case AD_CHECK_SUSPEND_TO_RAM:
	case AD_CHECK:
		if (!i_cpr_is_supported(pm_cpr_state) || cpr_reusable_mode)
			return (ENOTSUP);
		return (0);

	case AD_CPR_REUSEINIT:
		if (!i_cpr_reusable_supported())
			return (ENOTSUP);
		if (!cpr_statefile_is_spec(0)) {
			cpr_err(CE_CONT, blockstr);
			return (EINVAL);
		}
		if ((rc = cpr_check_spec_statefile()) != 0)
			return (rc);
		if (swapinfo) {
			cpr_err(CE_CONT, noswapstr);
			return (EINVAL);
		}
		cpr_test_mode = 0;
		break;

	case AD_CPR_NOCOMPRESS:
	case AD_SUSPEND:
	case AD_SUSPEND_TO_RAM:
		if (cpr_reusable_mode) {
			cpr_err(CE_CONT, normalfmt, A_FREEZE, AD_REUSEFINI);
			return (ENOTSUP);
		}
		cpr_test_point = LOOP_BACK_NONE;
		cpr_test_mode = 0;
		break;

	case AD_CPR_REUSABLE:
		if (!i_cpr_reusable_supported())
			return (ENOTSUP);
		if (!cpr_statefile_is_spec(0)) {
			cpr_err(CE_CONT, blockstr);
			return (EINVAL);
		}
		if ((rc = cpr_check_spec_statefile()) != 0)
			return (rc);
		if (swapinfo) {
			cpr_err(CE_CONT, noswapstr);
			return (EINVAL);
		}
		if ((rc = cpr_reusable_mount_check()) != 0)
			return (rc);
		cpr_test_mode = 0;
		break;

	case AD_CPR_REUSEFINI:
		if (!i_cpr_reusable_supported())
			return (ENOTSUP);
		cpr_test_mode = 0;
		break;

	case AD_CPR_TESTZ:
	case AD_CPR_TESTNOZ:
	case AD_CPR_TESTHALT:
	case AD_LOOPBACK_SUSPEND_TO_RAM_PASS:
		if (cpr_reusable_mode) {
			cpr_err(CE_CONT, normalfmt, A_FREEZE, AD_REUSEFINI);
			return (EINVAL);
		}
		cpr_test_point = fcn;
		cpr_test_mode = 1;
		break;

	case AD_LOOPBACK_SUSPEND_TO_RAM_FAIL:
		cpr_test_point = LOOP_BACK_FAIL;
		break;

	case AD_DEVICE_SUSPEND_TO_RAM:
		if (mdep == NULL) {
			/* Didn't pass enough arguments */
			return (EINVAL);
		}
		cpr_test_point = DEVICE_SUSPEND;
		cpr_device = (major_t)atoi((char *)mdep);
		break;

	case AD_CPR_PRINT:
		CPR_STAT_EVENT_END("POST CPR DELAY");
		cpr_stat_event_print();
		return (0);

	case AD_CPR_DEBUG0:
		cpr_debug = 0;
		return (0);

	case AD_CPR_DEBUG1:
	case AD_CPR_DEBUG2:
	case AD_CPR_DEBUG3:
	case AD_CPR_DEBUG4:
	case AD_CPR_DEBUG5:
	case AD_CPR_DEBUG7:
	case AD_CPR_DEBUG8:
		cpr_debug |= CPR_DEBUG_BIT(fcn);
		return (0);

	case AD_CPR_DEBUG9:
		cpr_debug |= CPR_DEBUG6;
		return (0);

	default:
		return (ENOTSUP);
	}

	if (fcn == AD_CPR_REUSEINIT) {
		if (mutex_tryenter(&cpr_slock) == 0)
			return (EBUSY);
		if (cpr_reusable_mode) {
			cpr_err(CE_CONT, modefmt, "already");
			mutex_exit(&cpr_slock);
			return (EBUSY);
		}
		rc = i_cpr_reuseinit();
		mutex_exit(&cpr_slock);
		return (rc);
	}

	if (fcn == AD_CPR_REUSEFINI) {
		if (mutex_tryenter(&cpr_slock) == 0)
			return (EBUSY);
		if (!cpr_reusable_mode) {
			cpr_err(CE_CONT, modefmt, "not");
			mutex_exit(&cpr_slock);
			return (EINVAL);
		}
		rc = i_cpr_reusefini();
		mutex_exit(&cpr_slock);
		return (rc);
	}

	/*
	 * acquire cpr serial lock and init cpr state structure.
	 */
	if (rc = cpr_init(fcn)) {
		cpr_done();
		return (rc);
	}

	if (fcn == AD_CPR_REUSABLE) {
		if ((rc = i_cpr_check_cprinfo()) != 0)  {
			mutex_exit(&cpr_slock);
			return (rc);
		}
	}

	/*
	 * Call the main cpr routine. If we are successful, we will be coming
	 * down the 'else' branch on resume, otherwise, suspend failed.
	 */
	cpr_err(CE_CONT, "System is being suspended");
	if (rc = cpr_main()) {
		/* This is the error path for s/r. */
		CPR->c_flags |= C_ERROR;
		PMD(PMD_SX, ("cpr: Suspend operation failed.\n"))
		cpr_err(CE_NOTE, "Suspend operation failed.");
	} else {
		/*
		 * If we get here, we are back from a successful
		 * suspend.
		 */
		PMD(PMD_SX, ("cpr: S%d successful\n", pm_cpr_state))
	}
	PMD(PMD_SX, ("cpr: cpr done\n"))
	cpr_done();
	return (rc);
}
