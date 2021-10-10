/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * ctsmc_kstat - Kstat routines for SMC driver
 */

#include <sys/types.h>
#include <sys/conf.h>
#include <sys/dditypes.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/ksynch.h>
#include <sys/strsun.h>
#include <sys/envctrl_gen.h>

#include <sys/smc_commands.h>
#include <sys/ctsmc_debug.h>
#include <sys/ctsmc.h>

extern int ctsmc_command_send(ctsmc_state_t *ctsmc, uint8_t cmd,
		uchar_t *inbuf, uint8_t	inlen, uchar_t	*outbuf,
		uint8_t	*outlen);
#define	CTSMC_KS_SIZE	\
	(CTSMC_NUM_COUNTERS * sizeof (struct kstat_named))

/*
 * This will maintain the current values of warning and
 * threshold temperatures, these may be modified using
 * kstat_write()
 */
static struct envctrl_temp ctsmc_temp_kstats;

static	char *ctsmc_ks_strings[] = {
	"ctsmc_pkt_xmit",
	"ctsmc_xmit_failure",
	"ctsmc_pkt_recv",
	"ctsmc_rsp_unclaimed",
	"ctsmc_recv_failure",
	"ctsmc_regular_req",
	"ctsmc_regular_rsp",
	"ctsmc_pvt_req",
	"ctsmc_pvt_rsp",
	"ctsmc_sync_req",
	"ctsmc_sync_rsp",
	"ctsmc_async_recv",
	"ctsmc_wdog_exp",
	"ctsmc_ipmi_notif",
	"ctsmc_enum_notif",
	"ctsmc_ipmi_xmit",
	"ctsmc_ipmi_rsps",
	"ctsmc_ipmi_evts",
	"ctsmc_ipmi_rsps_drop",
	"ctsmc_bad_ipmi",
	"ctsmc_bmc_xmit",
	"ctsmc_bmc_rsps",
	"ctsmc_bmc_evts",
};

/*
 * --------------------------
 * SMC kstat support routines
 * --------------------------
 */
static int
ctsmc_update_ks_hwstat(kstat_t *ksp, int rw)
{
	int i;
	struct kstat_named *smcksp = (struct kstat_named *)ksp->ks_data;

	if (rw == KSTAT_WRITE)
		return (DDI_FAILURE);

	/*
	 * copy the entire hwstat into pks_hwstat one item at a time
	 */
	for (i = 0; i < CTSMC_NUM_COUNTERS; i++)
		smcksp[i].value.ui64 = SMC_STAT_VAL(i);

	return (DDI_SUCCESS);
}

void
ctsmc_free_kstat(ctsmc_state_t *ctsmc)
{
	if (ctsmc->smcksp != NULL)
		kstat_delete(ctsmc->smcksp);
	if (ctsmc->smctempksp != NULL)
		kstat_delete(ctsmc->smctempksp);

	ctsmc->smcksp = NULL;
	ctsmc->smctempksp = NULL;
}

#define	SHUTDOWN_TEMP_MIN	55
#define	SHUTDOWN_TEMP_MAX	85
#define	SMC_CPU_TEMP_SENSOR	2
static int
ctsmc_pcf8591_temp_kstat_update(kstat_t *ksp, int rw)
{
	ctsmc_state_t *ctsmc;
	char *kstatp;
	int err = 0, result = SMC_SUCCESS;
	int warn_temp = 0;
	int shutdown_temp = 0;

	ctsmc = (ctsmc_state_t *)ksp->ks_private;

	LOCK_DATA(ctsmc);
	while (ctsmc->ctsmc_flag & SMC_IS_BUSY) {
		if (cv_wait_sig(&ctsmc->ctsmc_cv,
			&ctsmc->lock) <= 0) {
			UNLOCK_DATA(ctsmc);

			return (EINTR);
		}
	}

	ctsmc->ctsmc_flag |= SMC_IS_BUSY;
	UNLOCK_DATA(ctsmc);

	kstatp = (char *)ksp->ks_data;

	if (rw == KSTAT_WRITE) {

		/* check for the size of buffer */
		if (ksp->ks_data_size != sizeof (struct envctrl_temp)) {
			err = EIO;
			goto bail;
		}

		warn_temp = ((envctrl_temp_t *)kstatp)->warning_threshold;
		shutdown_temp = ((envctrl_temp_t *)kstatp)->shutdown_threshold;

		if (shutdown_temp < SHUTDOWN_TEMP_MIN || shutdown_temp >
							SHUTDOWN_TEMP_MAX) {
			err = EIO;
			goto bail;
		}

		if (warn_temp < 0 || shutdown_temp <= warn_temp) {
			err = EIO;
			goto bail;
		}

		/* Store these values in ctsmc_temp_kstats */
		ctsmc_temp_kstats.warning_threshold = warn_temp;
		ctsmc_temp_kstats.shutdown_threshold = shutdown_temp;

	} else {

		uint8_t sensor_num = SMC_CPU_TEMP_SENSOR, out[5], outlen;
		result = ctsmc_command_send(ctsmc, SMC_SENSOR_READING_GET,
				&sensor_num, 1, out, &outlen);
		if (result != SMC_SUCCESS) {
			err = EIO;
			goto bail;
		}
		ctsmc_temp_kstats.value = out[0];
		bcopy((caddr_t)&ctsmc_temp_kstats, kstatp,
			sizeof (struct envctrl_temp));
	}

bail:

	LOCK_DATA(ctsmc);
	ctsmc->ctsmc_flag &= ~SMC_IS_BUSY;
	cv_signal(&ctsmc->ctsmc_cv);
	UNLOCK_DATA(ctsmc);

	return (err);
}

/*
 * Create and initialize kstat data structure
 */
int
ctsmc_alloc_kstat(ctsmc_state_t *ctsmc)
{
	int i;
	struct kstat_named *ctsmc_named_ksp;

	SMC_DEBUG(SMC_KSTAT_DEBUG, "ctsmc_alloc_kstat: create "
			"ctsmc_cmd_stat: %d bytes", CTSMC_KS_SIZE);
	if ((ctsmc->smcksp = kstat_create(SMC_CLONE_DEV, ctsmc->ctsmc_instance,
			SMC_KSTAT_NAME, "misc", KSTAT_TYPE_NAMED,
			CTSMC_KS_SIZE, KSTAT_FLAG_PERSISTENT))
			== NULL) {
		return (SMC_FAILURE);
	}
	ctsmc->smcksp->ks_update = ctsmc_update_ks_hwstat;
	ctsmc->smcksp->ks_private = (void *)ctsmc;
	ctsmc_named_ksp = (struct kstat_named *)(ctsmc->smcksp->ks_data);

	if (ctsmc_update_ks_hwstat(ctsmc->smcksp, KSTAT_READ) != DDI_SUCCESS) {
		ctsmc_free_kstat(ctsmc);

		return (DDI_FAILURE);
	}
	for (i = 0; i < CTSMC_NUM_COUNTERS; i++)
		kstat_named_init(ctsmc_named_ksp + i,
			ctsmc_ks_strings[i], KSTAT_DATA_INT64);

	kstat_install(ctsmc->smcksp);

	/*
	 * Install temperature kstats
	 */
	if ((ctsmc->smctempksp = kstat_create(SMC_I2C_PCF8591_NAME,
		ctsmc->ctsmc_instance, SMC_I2C_KSTAT_CPUTEMP, "misc",
		KSTAT_TYPE_RAW, sizeof (struct envctrl_temp),
		KSTAT_FLAG_PERSISTENT | KSTAT_FLAG_WRITABLE)) == NULL) {

		return (DDI_FAILURE);
	}

	/*
	 * The kstat fields are already initialized in the attach routine..
	 */

	ctsmc->smctempksp->ks_update = ctsmc_pcf8591_temp_kstat_update;
	ctsmc->smctempksp->ks_private = (void *)ctsmc;

	kstat_install(ctsmc->smctempksp);

	return (DDI_SUCCESS);
}
