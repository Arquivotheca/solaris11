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
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/types.h>
#include <sys/smp_impldefs.h>
#include <sys/promif.h>

#include <sys/kmem.h>
#include <sys/archsystm.h>
#include <sys/cpuvar.h>
#include <sys/pte.h>
#include <vm/seg_kmem.h>
#include <sys/epm.h>
#include <sys/cpr.h>
#include <sys/machsystm.h>
#include <sys/clock.h>

#include <sys/cpr_wakecode.h>
#include <sys/acpi/acpi.h>

#ifdef OLDPMCODE
#include "acpi.h"
#endif

#include	<sys/x86_archext.h>
#include	<sys/reboot.h>
#include	<sys/cpu_module.h>
#include	<sys/kdi.h>

/*
 * S3 stuff
 */

int acpi_rtc_wake = 0x0;		/* wake in N seconds */

#if 0	/* debug */
static uint8_t	branchbuf[64 * 1024];	/* for the HDT branch trace stuff */
#endif	/* debug */

extern int boothowto;

#define	BOOTCPU	0	/* cpu 0 is always the boot cpu */

extern void		kernel_wc_code(void);
extern tod_ops_t	*tod_ops;
extern int flushes_require_xcalls;
extern int tsc_gethrtime_enable;

extern cpuset_t cpu_ready_set;
extern void *(*cpu_pause_func)(void *);



/*
 * This is what we've all been waiting for!
 */
int
acpi_enter_sleepstate(sxa_t *sxap)
{
	ACPI_PHYSICAL_ADDRESS	wakephys = sxap->sxa_wakephys;
	caddr_t			wakevirt = rm_platter_va;
	/*LINTED*/
	wakecode_t		*wp = (wakecode_t *)wakevirt;
	uint_t			Sx = sxap->sxa_state;
	int			save_wake = 0;

	/* Return appropriately if a "test" suspend */
	switch (sxap->sxa_test_point) {
	case DEVICE_SUSPEND:
	case LOOP_BACK_PASS:
		return (0);
	case LOOP_BACK_FAIL:
		return (1);
	case AD_CPR_TESTZ:
	case AD_CPR_TESTNOZ:
		/*
		 * If test suspend by entry, enable timed wake in
		 * 5 seconds.
		 */
		save_wake = acpi_rtc_wake;
		acpi_rtc_wake = 5;
		break;
	default:
		ASSERT(sxap->sxa_test_point == LOOP_BACK_NONE);
	}

	PT(PT_SWV);
	/* Set waking vector */
	if (AcpiSetFirmwareWakingVector(wakephys) != AE_OK) {
		PT(PT_SWV_FAIL);
		PMD(PMD_SX, ("Can't SetFirmwareWakingVector(%lx)\n",
		    (long)wakephys))
		goto insomnia;
	}

	PT(PT_EWE);
	/*
	 * Enable wake events
	 * This would probably best stuffed in a list somewhere,
	 * so we can have run through them all based on some user
	 * input that decides what can wake a machine.
	 */
	if (AcpiEnableEvent(ACPI_EVENT_POWER_BUTTON, 0) != AE_OK) {
		PT(PT_EWE_FAIL);
		PMD(PMD_SX, ("Can't EnableEvent(POWER_BUTTON)\n"))
	}

	if (acpi_rtc_wake > 0) {
		/* clear the RTC bit first */
		(void) AcpiWriteBitRegister(ACPI_BITREG_RT_CLOCK_STATUS, 1);
		PT(PT_RTCW);
		if (AcpiEnableEvent(ACPI_EVENT_RTC, 0) != AE_OK) {
			PT(PT_RTCW_FAIL);
			PMD(PMD_SX, ("Can't EnableEvent(RTC)\n"))
		}

		/*
		 * Set RTC to wake us in a wee while.
		 */
		mutex_enter(&tod_lock);
		PT(PT_TOD);
		TODOP_SETWAKE(tod_ops, acpi_rtc_wake);
		mutex_exit(&tod_lock);
	}

	/*
	 * Now that we have done a TODOP_SETWAKE, reset acpi_rtc_wake
	 * back to it's original value
	 */
	if ((sxap->sxa_test_point == AD_CPR_TESTZ) ||
	    (sxap->sxa_test_point == AD_CPR_TESTNOZ)) {
		acpi_rtc_wake = save_wake;
	}

	/*
	 * Prepare for sleep ... should we have done this earlier?
	 */
	PT(PT_SXP);
	PMD(PMD_SX, ("Calling AcpiEnterSleepStatePrep(%d) ...\n", Sx))
	if (AcpiEnterSleepStatePrep(Sx) != AE_OK) {
		PMD(PMD_SX, ("... failed\n!"))
		goto insomnia;
	}

	/*
	 * Tell the hardware to sleep.
	 */
	PT(PT_SXE);
	PMD(PMD_SX, ("Calling AcpiEnterSleepState(%d) ...\n", Sx))
	if (AcpiEnterSleepState(Sx) != AE_OK) {
		PT(PT_SXE_FAIL);
		PMD(PMD_SX, ("... failed!\n"))
	}

insomnia:
	PT(PT_INSOM);
	/* cleanup is done by the caller */
	return (1);
}

int
acpi_exit_sleepstate(sxa_t *sxap)
{
	int Sx = sxap->sxa_state;
	ACPI_STATUS status;

	PT(PT_WOKE);
	PMD(PMD_SX, ("!We woke up!\n"))

	PT(PT_LSS);
	if ((status = AcpiLeaveSleepState(Sx)) != AE_OK) {
		PT(PT_LSS_FAIL);
		PMD(PMD_SX, ("Problem with LeaveSleepState %d: %d!\n",
		    Sx, status))
	}

	PT(PT_CPB);
	if ((status = AcpiClearEvent(ACPI_EVENT_POWER_BUTTON)) != AE_OK) {
		PT(PT_CPB_FAIL);
		PMD(PMD_SX, ("Problem w/ ClearEvent(POWER_BUTTON): %d\n",
		    status))
	}
	if (acpi_rtc_wake > 0 &&
	    (status = AcpiDisableEvent(ACPI_EVENT_RTC, 0)) != AE_OK) {
		PT(PT_DRTC_FAIL);
		PMD(PMD_SX, ("Problem w/ DisableEvent(RTC): %d\n", status))
	}

	PMD(PMD_SX, ("Exiting acpi_sleepstate(%d) => 0\n", Sx))

	return (0);
}
