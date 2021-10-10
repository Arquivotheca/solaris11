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
 * Copyright (c) 2005, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*	Copyright (c) 1990, 1991 UNIX System Laboratories, Inc.	*/
/*	Copyright (c) 1984, 1986, 1987, 1988, 1989, 1990 AT&T	*/
/*	  All Rights Reserved  	*/

#include <sys/param.h>
#include <sys/time.h>
#include <sys/systm.h>
#include <sys/archsystm.h>
#include <sys/sysmacros.h>
#include <sys/lockstat.h>

#include <sys/clock.h>
#include <sys/hrt.h>
#include <sys/debug.h>
#include <sys/smp_impldefs.h>
#include <sys/rtc.h>

extern void before_tod_get(void);
extern void after_tod_get(void);
extern void after_tod_set(timestruc_t);

/*
 * This file contains all generic part of clock and timer handling.
 * Specifics are now in separate files and may be overridden by TOD
 * modules.
 */

char *tod_module_name;		/* Settable in /etc/system */

/*
 * Write the specified time into the clock chip.
 * Must be called with tod_lock held.
 */
void
tod_set(timestruc_t ts)
{
	ASSERT(MUTEX_HELD(&tod_lock));

	TODOP_SET(tod_ops, ts);
	after_tod_set(ts);		/* for tod_validate() */
	tod_status_set(TOD_SET_DONE);	/* TOD was modified */
}

/*
 * Read the current time from the clock chip and convert to UNIX form.
 * Assumes that the year in the clock chip is valid.
 * Must be called with tod_lock held.
 */
timestruc_t
tod_get(void)
{
	timestruc_t ts;

	ASSERT(MUTEX_HELD(&tod_lock));

	before_tod_get();			/* for tod_validate() */
	ts = TODOP_GET(tod_ops);
	after_tod_get(); 			/* for tod_validate() */
	ts.tv_sec = tod_validate(ts.tv_sec);
	return (ts);
}

/*
 * The following wrappers have been added so that locking
 * can be exported to platform-independent clock routines
 * (ie adjtime(), clock_setttime()), via a functional interface.
 */
int
hr_clock_lock(void)
{
	ushort_t s;

	CLOCK_LOCK(&s);
	return (s);
}

void
hr_clock_unlock(int s)
{
	CLOCK_UNLOCK(s);
}

/*
 * Support routines for horrid GMT lag handling
 */

static time_t gmt_lag;		/* offset in seconds of gmt to local time */

void
sgmtl(time_t arg)
{
	gmt_lag = arg;
}

time_t
ggmtl(void)
{
	return (gmt_lag);
}

/* rtcsync() - set 'time', assuming RTC and GMT lag are correct */

void
rtcsync(void)
{
	timestruc_t ts;

	mutex_enter(&tod_lock);
	ts = TODOP_GET(tod_ops);
	set_hrestime(&ts);
	mutex_exit(&tod_lock);
}

/*
 * This is called in boot before enabling MP.
 */
void
plat_boot_hrt_switch(hrt_t *hp)
{
	ulong_t	iflags;

	iflags = intr_clear();
	*hp = *hrt;
	hrt = hp;
	intr_restore(iflags);
}
