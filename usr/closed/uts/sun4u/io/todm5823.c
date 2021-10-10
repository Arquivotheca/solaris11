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
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * tod driver module for ALI M5823 and compatible devices
 */

#include <sys/types.h>
#include <sys/conf.h>
#include <sys/kmem.h>
#include <sys/open.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>

#include <sys/todm5823.h>
#include <sys/rmc_comm_dp.h>
#include <sys/rmc_comm_drvintf.h>
#include <sys/modctl.h>
#include <sys/stat.h>
#include <sys/clock.h>
#include <sys/hrt.h>
#include <sys/reboot.h>
#include <sys/machsystm.h>
#include <sys/platform_module.h>
#if 0
#include <sys/poll.h>
#include <sys/pbio.h>
#endif

static timestruc_t	todm5823_get(void);
static void		todm5823_set(timestruc_t);
static uint_t		todm5823_set_watchdog_timer(uint_t);
static uint_t		todm5823_clear_watchdog_timer(void);
static void		todm5823_set_power_alarm(timestruc_t);
static void		todm5823_clear_power_alarm(void);
static uint64_t		todm5823_get_cpufrequency(void);

extern uint64_t		find_cpufrequency(volatile uint8_t *);

/*
 * External variables
 */
extern int	watchdog_enable;
extern int	watchdog_available;
extern int	boothowto;

/*
 * Global variables
 */
int m5823_debug_flags;
uint_t m5823_hrestime_count = 0;
uint_t m5823_uip_count = 0;

/*
 * Module linkage information for the kernel.
 */
static struct modlmisc modlmisc = {
	&mod_miscops, "tod module for M5823 and compatible devices"
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modlmisc, NULL
};

static uint_t read_rtc(struct rtc_t *);
static void write_rtc_time(struct rtc_t *);
static void write_rtc_alarm(struct rtc_t *);

int
_init(void)
{
	if (strcmp(tod_module_name, "todm5823") == 0) {
		M5823_ADDR_BANK0_REG = RTC_B;
		M5823_DATA_BANK0_REG = (RTC_DM | RTC_HM);

		tod_ops.tod_get = todm5823_get;
		tod_ops.tod_set = todm5823_set;

		tod_ops.tod_set_watchdog_timer =
		    todm5823_set_watchdog_timer;
		tod_ops.tod_clear_watchdog_timer =
		    todm5823_clear_watchdog_timer;
		tod_ops.tod_set_power_alarm = todm5823_set_power_alarm;
		tod_ops.tod_clear_power_alarm = todm5823_clear_power_alarm;
		tod_ops.tod_get_cpufrequency = todm5823_get_cpufrequency;

		/*
		 * check if hardware watchdog timer is available and user
		 * enabled it.
		 */
		if (watchdog_enable) {
			if (!watchdog_available) {
				cmn_err(CE_WARN, "todm5823: Hardware watchdog "
				    "unavailable");
			} else if (boothowto & RB_DEBUG) {
				cmn_err(CE_WARN, "todm5823: Hardware watchdog"
				    " disabled [debugger]");
			}
		}
	}

	return (mod_install(&modlinkage));
}

int
_fini(void)
{
	if (strcmp(tod_module_name, "todm5823") == 0)
		return (EBUSY);

	return (mod_remove(&modlinkage));
}

/*
 * The loadable-module _info(9E) entry point
 */
int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}


/*
 * Read the current time from the clock chip and convert to UNIX form.
 * Assumes that the year in the clock chip is valid.
 * Must be called with tod_lock held.
 */
static timestruc_t
todm5823_get(void)
{
	timestruc_t ts;
	todinfo_t tod;
	struct rtc_t rtc;

	ASSERT(MUTEX_HELD(&tod_lock));

	/*
	 * Set the hw watchdog timer if it's been activated.
	 * This will toggle the watchdog.
	 */
	if (watchdog_activated) {
		int ret = 0;
		ret = tod_ops.tod_set_watchdog_timer(0);
		/*
		 * The empty set_watchdog routine returns a 0. So if a
		 * coded routine fails we will look for a -1 for a failure.
		 */
		if (ret == -1)
			cmn_err(CE_WARN, "todm5823: failed to set hardware "
			    "watchdog timer.");
	}


	if (!read_rtc(&rtc)) {
		/*
		 * We could not read from the tod
		 */
		m5823_hrestime_count++;
		tod_status_set(TOD_GET_FAILED);
		ts.tv_sec = hrt->hrestime.tv_sec;
		ts.tv_nsec = hrt->hrestime.tv_nsec;
		return (ts);
	}

	DPRINTF("todm5823_get: century=%d year=%d dom=%d hrs=%d\n",
	    rtc.rtc_century, rtc.rtc_year, rtc.rtc_dom, rtc.rtc_hrs);

	/*
	 * tod_year is base 1900 so this code needs to adjust the true
	 * year retrieved from the rtc's century and year fields.
	 */
	tod.tod_year	= rtc.rtc_year + (rtc.rtc_century * 100) - 1900;
	tod.tod_month	= rtc.rtc_mon;
	tod.tod_day	= rtc.rtc_dom;
	tod.tod_dow	= rtc.rtc_dow;
	tod.tod_hour	= rtc.rtc_hrs;
	tod.tod_min	= rtc.rtc_min;
	tod.tod_sec	= rtc.rtc_sec;

	/* read was successful so ensure failure flag is clear */
	tod_status_clear(TOD_GET_FAILED);

	ts.tv_sec = tod_to_utc(tod);
	ts.tv_nsec = 0;
	return (ts);
}

static uint_t
read_rtc(struct rtc_t *rtc)
{
	int i;
	unsigned int spl_old;
	volatile uint8_t rega;
	uint_t rtc_readable = 0;

	for (i = 0; i < TODM5823_UIP_RETRY_THRESH; i++) {
		spl_old = ddi_enter_critical();
		/* Read register A */
		M5823_ADDR_BANK0_REG = RTC_A;
		rega = M5823_DATA_BANK0_REG;

		if (!(rega & RTC_UIP)) {
			M5823_ADDR_BANK0_REG = RTC_SEC;
			rtc->rtc_sec = M5823_DATA_BANK0_REG;
			M5823_ADDR_BANK0_REG = RTC_ASEC;
			rtc->rtc_asec = M5823_DATA_BANK0_REG;
			M5823_ADDR_BANK0_REG = RTC_MIN;
			rtc->rtc_min = M5823_DATA_BANK0_REG;
			M5823_ADDR_BANK0_REG = RTC_AMIN;
			rtc->rtc_amin = M5823_DATA_BANK0_REG;
			M5823_ADDR_BANK0_REG = RTC_HRS;
			rtc->rtc_hrs = M5823_DATA_BANK0_REG;
			M5823_ADDR_BANK0_REG = RTC_AHRS;
			rtc->rtc_ahrs = M5823_DATA_BANK0_REG;
			M5823_ADDR_BANK0_REG = RTC_DOW;
			rtc->rtc_dow = M5823_DATA_BANK0_REG;
			M5823_ADDR_BANK0_REG = RTC_DOM;
			rtc->rtc_dom = M5823_DATA_BANK0_REG;
			M5823_ADDR_BANK0_REG = RTC_MON;
			rtc->rtc_mon = M5823_DATA_BANK0_REG;
			M5823_ADDR_BANK0_REG = RTC_YEAR;
			rtc->rtc_year = M5823_DATA_BANK0_REG;
			M5823_ADDR_BANK0_REG = RTC_CENTURY;
			rtc->rtc_century = M5823_DATA_BANK0_REG;

			/* Read date alarm */
			M5823_ADDR_BANK0_REG = RTC_ADOM_REG;
			rtc->rtc_adom = (M5823_DATA_BANK0_REG) & RTC_ADOM;

			/* Read month and week alarm */
			M5823_ADDR_BANK1_REG = RTC_AMON;
			rtc->rtc_amon = M5823_DATA_BANK1_REG;
			M5823_ADDR_BANK1_REG = RTC_AWEK;
			rtc->rtc_awek = M5823_DATA_BANK1_REG;

			rtc_readable = 1;
			ddi_exit_critical(spl_old);
			break;
		}
		ddi_exit_critical(spl_old);
		drv_usecwait(TODM5823_UIP_WAIT_USEC);
	}

	if (i > 0)
		m5823_uip_count++;

	return (rtc_readable);
}

/*
 * Write the specified time into the clock chip.
 * Must be called with tod_lock held.
 */
static void
todm5823_set(timestruc_t ts)
{
	struct rtc_t	rtc;
	todinfo_t tod = utc_to_tod(ts.tv_sec);
	int year;
	rmc_comm_msg_t request;
	dp_set_date_time_t set_time_msg;

	ASSERT(MUTEX_HELD(&tod_lock));

	/* tod_year is base 1900 so this code needs to adjust */
	year = 1900 + tod.tod_year;
	rtc.rtc_year	= year % 100;
	rtc.rtc_century = year / 100;
	rtc.rtc_mon	= (uint8_t)tod.tod_month;
	rtc.rtc_dom	= (uint8_t)tod.tod_day;
	rtc.rtc_dow	= (uint8_t)tod.tod_dow;
	rtc.rtc_hrs	= (uint8_t)tod.tod_hour;
	rtc.rtc_min	= (uint8_t)tod.tod_min;
	rtc.rtc_sec	= (uint8_t)tod.tod_sec;
	DPRINTF("todm5823_set: year=%d dom=%d hrs=%d min=%d sec=%d\n",
	    rtc.rtc_year, rtc.rtc_dom, rtc.rtc_hrs, rtc.rtc_min, rtc.rtc_sec);

	write_rtc_time(&rtc);

	if (&plat_rmc_comm_req) {
		/*
		 * We are running on a platform that has ALOM.
		 * Example: Boston, Seattle.
		 *
		 * On these platforms, the RTC value does not persist across
		 * power cycles.  However, ALOM has its own TOD clock that
		 * is battery backed and *does* hold its value across power
		 * cycles.
		 *
		 * On these platforms, on startup OBP reads ALOM's TOD and
		 * populates the RTC.  However, this means that we need to
		 * update ALOM's TOD whenever Solaris updates the RTC.
		 *
		 * Send ALOM an RMC message telling it about the new time
		 * value.  ALOM will update its own TOD clock with this new
		 * value.
		 *
		 * plat_rmc_comm_req() is a pragma weak function implemented
		 * in the Boston/Seattle platmod.  This function in turn
		 * calls rmc_comm_request_nowait() in the rmc_comm module.
		 * On platforms without ALOM, plat_rmc_comm_req() will be
		 * empty, and this section of code will be skipped.
		 */

		set_time_msg.year	= year - 1900;
		set_time_msg.month	= tod.tod_month - 1;
		set_time_msg.day	= tod.tod_day;
		set_time_msg.hour	= tod.tod_hour;
		set_time_msg.minute	= tod.tod_min;
		set_time_msg.second	= tod.tod_sec;

		request.msg_type = DP_SET_DATE_TIME;
		request.msg_len = sizeof (set_time_msg);
		request.msg_buf = (caddr_t)&set_time_msg;

		plat_rmc_comm_req(&request);
	}
}

void
write_rtc_time(struct rtc_t *rtc)
{
	uint8_t	regb;
	uint8_t	rega;
	int	i;

	/*
	 * Freeze
	 */
	M5823_ADDR_BANK0_REG = RTC_B;
	regb = M5823_DATA_BANK0_REG;
	M5823_DATA_BANK0_REG = (regb | RTC_SET);

	/*
	 * If an update is in progress wait for the UIP flag to clear.
	 * If we write whilst UIP is still set there is a slight but real
	 * possibility of corrupting the RTC date and time registers.
	 *
	 * The expected wait is one internal cycle of the chip.  We could
	 * simply spin but this may hang a CPU if we were to have a broken
	 * RTC chip where UIP is stuck, so we use a retry loop instead.
	 * No critical section is needed here as the UIP flag will not be
	 * re-asserted until we clear RTC_SET.
	 */
	for (i = 0; i < TODM5823_UIP_RETRY_THRESH; i++) {
		/* Read register A */
		M5823_ADDR_BANK0_REG = RTC_A;
		rega = M5823_DATA_BANK0_REG;

		if (!(rega & RTC_UIP)) {
			M5823_ADDR_BANK0_REG = RTC_SEC;
			M5823_DATA_BANK0_REG = rtc->rtc_sec;
			M5823_ADDR_BANK0_REG = RTC_MIN;
			M5823_DATA_BANK0_REG = rtc->rtc_min;
			M5823_ADDR_BANK0_REG = RTC_HRS;
			M5823_DATA_BANK0_REG = rtc->rtc_hrs;
			M5823_ADDR_BANK0_REG = RTC_DOW;
			M5823_DATA_BANK0_REG = rtc->rtc_dow;
			M5823_ADDR_BANK0_REG = RTC_DOM;
			M5823_DATA_BANK0_REG = rtc->rtc_dom;
			M5823_ADDR_BANK0_REG = RTC_MON;
			M5823_DATA_BANK0_REG = rtc->rtc_mon;
			M5823_ADDR_BANK0_REG = RTC_YEAR;
			M5823_DATA_BANK0_REG = rtc->rtc_year;
			M5823_ADDR_BANK0_REG = RTC_CENTURY;
			M5823_DATA_BANK0_REG = rtc->rtc_century;
			break;
		}
		drv_usecwait(TODM5823_UIP_WAIT_USEC);
	}

	/*
	 * Unfreeze
	 */
	M5823_ADDR_BANK0_REG = RTC_B;
	M5823_DATA_BANK0_REG = regb;

	if (i == TODM5823_UIP_RETRY_THRESH)
		cmn_err(CE_WARN, "todm5823: Could not write the RTC\n");
}

void
write_rtc_alarm(struct rtc_t *rtc)
{
	int 			i;
	unsigned int 		spl_old;
	volatile uint8_t 	rega;
	volatile uint8_t 	hi_regd;

	for (i = 0; i < TODM5823_UIP_RETRY_THRESH; i++) {
		spl_old = ddi_enter_critical();
		/* Read register A */
		M5823_ADDR_BANK0_REG = RTC_A;
		rega = M5823_DATA_BANK0_REG;

		if (!(rega & RTC_UIP)) {
			M5823_ADDR_BANK0_REG = RTC_ASEC;
			M5823_DATA_BANK0_REG = rtc->rtc_asec;
			M5823_ADDR_BANK0_REG = RTC_AMIN;
			M5823_DATA_BANK0_REG = rtc->rtc_amin;
			M5823_ADDR_BANK0_REG = RTC_AHRS;
			M5823_DATA_BANK0_REG = rtc->rtc_ahrs;
			M5823_ADDR_BANK1_REG = RTC_AMON;
			M5823_DATA_BANK1_REG = rtc->rtc_amon;
			M5823_ADDR_BANK1_REG = RTC_AWEK;
			M5823_DATA_BANK1_REG = rtc->rtc_awek;

			M5823_ADDR_BANK0_REG = RTC_ADOM_REG;
			hi_regd = M5823_DATA_BANK0_REG & ~RTC_ADOM;
			M5823_DATA_BANK0_REG = rtc->rtc_adom | hi_regd;
			ddi_exit_critical(spl_old);
			break;
		}
		ddi_exit_critical(spl_old);
		drv_usecwait(TODM5823_UIP_WAIT_USEC);
	}

	if (i >= TODM5823_UIP_RETRY_THRESH)
		cmn_err(CE_WARN, "m5823: Could not set the RTC alarm\n");
}

/*
 * program the rtc registers for alarm to go off at the specified time
 */
static void
todm5823_set_power_alarm(timestruc_t ts)
{
	todinfo_t	tod;
	uint8_t		regb;
	struct rtc_t	rtc;

	ASSERT(MUTEX_HELD(&tod_lock));
	tod = utc_to_tod(ts.tv_sec);

	/*
	 * disable alarms and clear AF flag by reading reg C
	 */
	M5823_ADDR_BANK0_REG = RTC_B;
	regb = M5823_DATA_BANK0_REG;
	M5823_DATA_BANK0_REG = regb & ~RTC_AIE;
	M5823_ADDR_BANK0_REG = RTC_C;
	(void) M5823_DATA_BANK0_REG;

	rtc.rtc_asec = (uint8_t)tod.tod_sec;
	rtc.rtc_amin = (uint8_t)tod.tod_min;
	rtc.rtc_ahrs = (uint8_t)tod.tod_hour;
	rtc.rtc_adom = (uint8_t)tod.tod_day;
	rtc.rtc_amon = (uint8_t)tod.tod_month;
	rtc.rtc_awek = (uint8_t)(1 << (tod.tod_dow - 1));


	DPRINTF("todm5823_set_alarm: mon=%d dom=%d hrs=%d min=%d sec=%d\n",
	    rtc.rtc_amon, rtc.rtc_adom, rtc.rtc_ahrs, rtc.rtc_amin,
	    rtc.rtc_asec);
	/*
	 * Write alarm values and enable alarm
	 */
	write_rtc_alarm(&rtc);

	M5823_ADDR_BANK0_REG = RTC_B;
	M5823_DATA_BANK0_REG = regb | RTC_AIE;
}

/*
 * clear alarm interrupt
 */
static void
todm5823_clear_power_alarm(void)
{
	uint8_t regb;

	ASSERT(MUTEX_HELD(&tod_lock));

	M5823_ADDR_BANK0_REG = RTC_B;
	regb = M5823_DATA_BANK0_REG;
	M5823_DATA_BANK0_REG = regb & ~RTC_AIE;
}

/*
 * Determine the cpu frequency by watching the TOD chip rollover twice.
 * Cpu clock rate is determined by computing the ticks added (in tick register)
 * during one second interval on TOD.
 */
uint64_t
todm5823_get_cpufrequency(void)
{
	ASSERT(MUTEX_HELD(&tod_lock));
	M5823_ADDR_BANK0_REG = RTC_SEC;
	return (find_cpufrequency(v_rtc_data_reg));
}

/*ARGSUSED*/
static uint_t
todm5823_set_watchdog_timer(uint_t timeoutval)
{
	ASSERT(MUTEX_HELD(&tod_lock));
	return (0);
}

static uint_t
todm5823_clear_watchdog_timer(void)
{
	ASSERT(MUTEX_HELD(&tod_lock));
	return (0);
}
