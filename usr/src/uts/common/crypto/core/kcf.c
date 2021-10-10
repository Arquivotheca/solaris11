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
 * Copyright (c) 2003, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Core KCF (Kernel Cryptographic Framework). This file implements
 * the loadable module entry points and module verification routines.
 */

#include <sys/systm.h>
#include <sys/cmn_err.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/modctl.h>
#include <sys/errno.h>
#include <sys/rwlock.h>
#include <sys/kmem.h>
#include <sys/door.h>
#include <sys/kobj.h>

#include <sys/crypto/common.h>
#include <sys/crypto/api.h>
#include <sys/crypto/spi.h>
#include <sys/crypto/impl.h>
#include <sys/crypto/sched_impl.h>
#include <sys/crypto/elfsign.h>
#include <sys/crypto/ioctladmin.h>

#include <fips_checksum.h>

#ifdef DEBUG
int kcf_frmwrk_debug = 0;

#define	KCF_FRMWRK_DEBUG(l, x)	if (kcf_frmwrk_debug >= l) printf x
#else	/* DEBUG */
#define	KCF_FRMWRK_DEBUG(l, x)
#endif	/* DEBUG */

/*
 * Door to make upcalls to kcfd. kcfd will send us this
 * handle when it is coming up.
 */
kmutex_t kcf_dh_lock;
door_handle_t kcf_dh = NULL;

/* Setup FIPS 140 support variables */
uint32_t fips140_mode = 0;	/* FIPS140 disabled by default */
uint32_t global_fips140_state = FIPS140_MODE_UNSET;
kmutex_t fips140_mode_lock;
kcondvar_t cv_fips140;
int	kernel_module_check_status;

/*
 * Kernel FIPS140 boundary module list
 * NOTE: "swrand" must be the last entry.  FIPS 140 shutdown functions stop
 *       before getting to swrand as it is used for non-FIPS 140
 *       operations too.  The FIPS 140 random API separately controls access.
 */
#define	FIPS140_MODULES_MAX 7
static char *fips140_module_list[FIPS140_MODULES_MAX] = {
	"aes", "des", "ecc", "sha1", "sha2", "rsa", "swrand"
};

/*
 * Kernel FIPS 140 Boundary Files
 * List of kernel modules which needs to be verified for the
 * Self Integrity Check.
 */
#define	KCF_NUM_FILES(arr)	((sizeof (arr)) / sizeof (char *))
static char *kcf_fips140_modules[] = {

#ifdef sparc
	"crypto/aes",
	"crypto/des",
	"crypto/ecc",
	"crypto/rsa",
	"crypto/swrand",
	"crypto/sha1",
	"misc/sha1",
	"crypto/sha2",
	"misc/sha2",
	"misc/kcf",
	"drv/crypto",
	"drv/cryptoadm",
#endif /* sparc */

#ifdef x86
	"crypto/aes",
	"crypto/des",
	"crypto/ecc",
	"crypto/rsa",
	"crypto/swrand",
	"crypto/sha1",
	"misc/sha1",
	"crypto/sha2",
	"misc/sha2",
	"misc/kcf",
	"drv/crypto",
	"drv/cryptoadm",
	"misc/amd64/sha1",
	"crypto/amd64/aes",
	"crypto/amd64/des",
	"crypto/amd64/ecc",
	"crypto/amd64/rsa",
	"crypto/amd64/swrand",
	"crypto/amd64/sha1",
	"crypto/amd64/sha2",
	"misc/amd64/sha2",
	"misc/amd64/kcf",
	"drv/amd64/crypto",
	"drv/amd64/cryptoadm",
#endif /* x86 */

};

static struct modlmisc modlmisc = {
	&mod_miscops, "Kernel Crypto Framework"
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modlmisc, NULL
};

static int rngtimer_started;
extern int sys_shutdown;

int
_init()
{
	mutex_init(&fips140_mode_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&cv_fips140, NULL, CV_DEFAULT, NULL);

	/* initialize the mechanisms tables supported out-of-the-box */
	kcf_init_mech_tabs();

	/* initialize the providers tables */
	kcf_prov_tab_init();

	/* initialize the policy table */
	kcf_policy_tab_init();

	/* initialize soft_config_list */
	kcf_soft_config_init();

	/*
	 * Initialize scheduling structures. Note that this does NOT
	 * start any threads since it might not be safe to do so.
	 */
	kcf_sched_init();

	/* initialize the RNG support structures */
	rngtimer_started = 0;
	kcf_rnd_init();

	return (mod_install(&modlinkage));
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

/*
 * We do not allow kcf to unload.
 */
int
_fini(void)
{
	return (EBUSY);
}


/* Returns the value of global_fips140_state */
int
kcf_get_fips140_state(void)
{
	return (global_fips140_state);
}

/*
 * If FIPS 140 has failed its tests.  The providers must be disabled from the
 * framework.
 */
void
kcf_fips140_shutdown()
{
	kcf_provider_desc_t *pd;
	int i;

	cmn_err(CE_WARN,
	    "Shutting down FIPS 140 boundary as verification failed.");

	/* Disable FIPS 140 modules, but leave swrand alone */
	for (i = 0; i < (FIPS140_MODULES_MAX - 1); i++) {
		/*
		 * Remove the predefined entries from the soft_config_list
		 * so the framework does not report the providers.
		 */
		remove_soft_config(fips140_module_list[i]);

		pd = kcf_prov_tab_lookup_by_name(fips140_module_list[i]);
		if (pd == NULL)
			continue;

		/* Allow the unneeded providers to be unloaded */
		pd->pd_mctlp->mod_loadflags &= ~(MOD_NOAUTOUNLOAD);

		/* Invalidate the FIPS 140 providers */
		mutex_enter(&pd->pd_lock);
		pd->pd_state = KCF_PROV_VERIFICATION_FAILED;
		mutex_exit(&pd->pd_lock);
		KCF_PROV_REFRELE(pd);
		undo_register_provider(pd, B_FALSE);

	}
}

/*
 * Activates the kernel providers
 *
 * If we are getting ready to enable FIPS 140 mode, then all providers should
 * be loaded and ready.
 *
 * If FIPS 140 is disabled, then we can skip any errors because some crypto
 * modules may not have been loaded.
 */
void
kcf_activate()
{
	kcf_provider_desc_t *pd;
	int i;

	for (i = 0; i < (FIPS140_MODULES_MAX - 1); i++) {
		pd = kcf_prov_tab_lookup_by_name(fips140_module_list[i]);
		if (pd == NULL) {
			if (global_fips140_state == FIPS140_MODE_DISABLED)
				continue;

			/* There should never be a NULL value in FIPS 140 */
			cmn_err(CE_WARN, "FIPS 140 activation: %s not in "
			    "kernel provider table", fips140_module_list[i]);
			kcf_fips140_shutdown();
			break;
		}

		/*
		 * Change the provider state so the verification functions
		 * can signature verify, if necessary, and ready it.
		 */
		if (pd->pd_state == KCF_PROV_UNVERIFIED_FIPS140) {
			mutex_enter(&pd->pd_lock);
			pd->pd_state = KCF_PROV_UNVERIFIED;
			mutex_exit(&pd->pd_lock);
		}

		KCF_PROV_REFRELE(pd);
	}

	/* If we are not in FIPS 140 mode, then exit */
	if (global_fips140_state != FIPS140_MODE_DISABLED) {
		/* If we in the process of validating FIPS 140, enable it */
		mutex_enter(&fips140_mode_lock);
		global_fips140_state = FIPS140_MODE_ENABLED;
		cv_signal(&cv_fips140);
		mutex_exit(&fips140_mode_lock);
		cmn_err(CE_CONT, "?FIPS 140 enabled. Boundary check complete.");
	}

	verify_unverified_providers();
}


/*
 * Check each crypto module's integrity.
 * This function assumes that all crypto modules are already loaded
 * by kcf_init().
 */
int
kcf_fips140_integrity_check()
{
	int	rv;
	int	i;

	KCF_FRMWRK_DEBUG(1, ("Starting IC check\n"));

	for (i = 0; i < KCF_NUM_FILES(kcf_fips140_modules); i++) {
		rv = fips_check_module(kcf_fips140_modules[i],
		    B_FALSE /* is_optional? */);
		if (rv != 0) {
			cmn_err(CE_WARN, "kcf: FIPS-140 Software Integrity "
			    "Test failed for %s (rv = %d)\n",
			    kcf_fips140_modules[i], rv);
			kernel_module_check_status = 0;
			return (1);
		}
	}

	kernel_module_check_status = 1;

	return (0);
}

/*
 * Load crypto modules and activate them.
 * If FIPS 140 is configured to be enabled, before it can be turned on, the
 * providers must run their Power On Self Test (POST).
 */
void
kcf_init()
{
	kcf_provider_desc_t *pd;
	int post_rv[FIPS140_MODULES_MAX];
	int ret = 0;
	int i;

	/* If FIPS mode is disabled, no verification necessary */
	if (fips140_mode == 0) {
		cmn_err(CE_NOTE, "!kcf: FIPS-140 disabled.");
		mutex_enter(&fips140_mode_lock);
		global_fips140_state = FIPS140_MODE_DISABLED;
		mutex_exit(&fips140_mode_lock);
		kcf_activate();
		return;
	}

#ifndef	__sparc
	if (kcf_fips140_integrity_check() != 0) {
		cmn_err(CE_WARN, "FIPS 140 integrity check failed");
		goto error;
	}
#endif

	/*
	 * If the integrity check wasn't successful, exit out.
	 */
	if (kernel_module_check_status != 1) {
		cmn_err(CE_WARN,
		    "FIPS 140 validation failed: Checksum failure");
		goto error;
	}

	/*
	 * Run POST tests for FIPS 140 modules, if they aren't loaded, load them
	 */
	for (i = 0; i < FIPS140_MODULES_MAX; i++) {
		pd = kcf_prov_tab_lookup_by_name(fips140_module_list[i]);
		if (pd == NULL) {
			/* If the module isn't loaded, load it */
			ret = modload("crypto", fips140_module_list[i]);
			if (ret == -1) {
				cmn_err(CE_WARN, "FIPS 140 validation failed: "
				    "error modloading module %s.",
				    fips140_module_list[i]);
				goto error;
			}

			/* Try again to get provider desc */
			pd = kcf_prov_tab_lookup_by_name(
			    fips140_module_list[i]);
			if (pd == NULL) {
				cmn_err(CE_WARN, "FIPS 140 validation failed: "
				    "Could not find module %s.",
				    fips140_module_list[i]);
				goto error;
			}
		}

		/* Make sure there are FIPS 140 entry points */
		if (KCF_PROV_FIPS140_OPS(pd) == NULL) {
			cmn_err(CE_WARN, "FIPS 140 validation failed: "
			    "No POST function entry point in %s.",
			    fips140_module_list[i]);
			goto error;
		}

		/* Make sure the module is not unloaded */
		pd->pd_mctlp->mod_loadflags |= MOD_NOAUTOUNLOAD;

		KCF_PROV_FIPS140_OPS(pd)->fips140_post(&post_rv[0]);
		if (post_rv[0] != CRYPTO_SUCCESS) {
			cmn_err(CE_WARN, "FIPS 140 POST failed for %s. "
			    "Error = 0x%x", fips140_module_list[i], post_rv[i]);
			goto error;
		}
		KCF_PROV_REFRELE(pd);
	}

	cmn_err(CE_NOTE, "!kcf: Device is in FIPS mode.");

	/* The POSTs for all modules were successful.  Activate the modules */
	kcf_activate();
	return;

error:
	mutex_enter(&fips140_mode_lock);
	global_fips140_state = FIPS140_MODE_SHUTDOWN;
	kcf_fips140_shutdown();
	cv_signal(&cv_fips140);
	mutex_exit(&fips140_mode_lock);

}


/*
 * Return a pointer to the modctl structure of the
 * provider's module.
 */
struct modctl *
kcf_get_modctl(crypto_provider_info_t *pinfo)
{
	struct modctl *mctlp;

	/* Get the modctl struct for this module */
	if (pinfo->pi_provider_type == CRYPTO_SW_PROVIDER)
		mctlp = mod_getctl(pinfo->pi_provider_dev.pd_sw);
	else {
		major_t major;
		char *drvmod;

		if ((major = ddi_driver_major(pinfo->pi_provider_dev.pd_hw))
		    != DDI_MAJOR_T_NONE) {
			drvmod = ddi_major_to_name(major);
			mctlp = mod_find_by_filename("drv", drvmod);
		} else
			return (NULL);
	}

	return (mctlp);
}

/* Check if this provider requires to be verified. */
int
verifiable_provider(crypto_ops_t *prov_ops)
{

	if (prov_ops->co_cipher_ops == NULL && prov_ops->co_dual_ops == NULL &&
	    prov_ops->co_dual_cipher_mac_ops == NULL &&
	    prov_ops->co_key_ops == NULL && prov_ops->co_sign_ops == NULL &&
	    prov_ops->co_verify_ops == NULL)
		return (0);

	return (1);
}

/*
 * With a given provider being registered, this looks through the FIPS 140
 * modules list and returns 1 if it's part of the FIPS 140 boundary and
 * the framework registration must be delayed until we know the FIPS 140 mode
 * status.  Zero means the provider does not need to wait for the FIPS 140
 * boundary. Negative 1 indicates a failure related to FIPS 140.
 *
 * If the provider in the boundary only provides random (like swrand), we
 * can let it register as the random API will block operations.
 */
int
kcf_need_fips140_verification(kcf_provider_desc_t *pd)
{
	int i, ret = 0;

	if (pd->pd_prov_type == CRYPTO_LOGICAL_PROVIDER)
		return (ret);

	mutex_enter(&fips140_mode_lock);

	if (global_fips140_state >= FIPS140_MODE_ENABLED)
		goto exit;

	for (i = 0; i < FIPS140_MODULES_MAX; i++) {
		if (strcmp(fips140_module_list[i], pd->pd_name) != 0)
			continue;

		/* If this module is only random, we can let it register */
		if (KCF_PROV_RANDOM_OPS(pd) &&
		    !verifiable_provider(pd->pd_ops_vector))
			break;

		if (global_fips140_state == FIPS140_MODE_SHUTDOWN) {
			ret = -1;
			break;
		}

		ret = 1;
		break;
	}

exit:
	mutex_exit(&fips140_mode_lock);
	return (ret);
}


/* called from the CRYPTO_LOAD_DOOR ioctl */
int
crypto_load_door(uint_t did)
{
	door_handle_t dh;

	mutex_enter(&kcf_dh_lock);
	dh = door_ki_lookup(did);
	if (dh != NULL)
		kcf_dh = dh;
	mutex_exit(&kcf_dh_lock);

	verify_unverified_providers();

	/* Start the timeout handler to get random numbers */
	if (rngtimer_started == 0) {
		kcf_rnd_schedule_timeout(B_TRUE);
		rngtimer_started = 1;
	}
	return (0);
}
