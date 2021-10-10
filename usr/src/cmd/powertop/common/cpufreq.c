/*
 * This file is part of PowerTOP
 *
 * This program file is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program in a file named COPYING; if not, write to the
 * Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA
 *
 * Authors:
 *	Arjan van de Ven <arjan@linux.intel.com>
 *	Eric C Saxe <eric.saxe@sun.com>
 *	Aubrey Li <aubrey.li@intel.com>
 */

/*
 * Copyright (c) 2009, Intel Corporation.
 * All Rights Reserved.
 */

/*
 * Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * GPL Disclaimer
 *
 * For the avoidance of doubt, except that if any license choice other
 * than GPL or LGPL is available it will apply instead, Sun elects to
 * use only the General Public License version 2 (GPLv2) at this time
 * for any software where a choice of GPL license versions is made
 * available with the language indicating that GPLv2 or any later
 * version may be used, or where a choice of which version of the GPL
 * is applied is otherwise unspecified.
 */
#include <stdlib.h>
#include <string.h>
#include <dtrace.h>
#include <kstat.h>
#include <errno.h>
#include "powertop.h"

/*
 * The following are needed in order to use the private interface
 * (the ioctl: PM_GET_CPUPM_STATE) in /dev/pm
 */
#include <stdio.h>
#include <unistd.h>
#include <stropts.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/pm.h>

#define	HZ2MHZ(speed)	((speed) / MICROSEC)
#define	DTP_ARG_COUNT	2
#define	DTP_ARG_LENGTH	5

static uint64_t		max_cpufreq = 0;
static dtrace_hdl_t	*dtp;
static char		**dtp_argv;

/*
 * Buffer containing DTrace program to track CPU frequency transitions
 */
static const char *dtp_cpufreq =
"hrtime_t last[$0];"
""
"BEGIN"
"{"
"	begin = timestamp;"
"}"
""
":::cpu-change-speed"
"/last[(processorid_t)arg0] != 0/"
"{"
"	this->cpu = (processorid_t)arg0;"
"	this->oldspeed = (uint64_t)arg1;"
"	@times[this->cpu, this->oldspeed] = sum(timestamp - last[this->cpu]);"
"	last[this->cpu] = timestamp;"
"}"
":::cpu-change-speed"
"/last[(processorid_t)arg0] == 0/"
"{"
"	this->cpu = (processorid_t)arg0;"
"	this->oldspeed = (uint64_t)arg1;"
"	@times[this->cpu, this->oldspeed] = sum(timestamp - begin);"
"	last[this->cpu] = timestamp;"
"}";

/*
 * Same as above, but only for a specific CPU
 */
static const char *dtp_cpufreq_c =
"hrtime_t last;"
""
"BEGIN"
"{"
"	begin = timestamp;"
"}"
""
":::cpu-change-speed"
"/(processorid_t)arg0 == $1 &&"
" last != 0/"
"{"
"	this->cpu = (processorid_t)arg0;"
"	this->oldspeed = (uint64_t)arg1;"
"	@times[this->cpu, this->oldspeed] = sum(timestamp - last);"
"	last = timestamp;"
"}"
":::cpu-change-speed"
"/(processorid_t)arg0 == $1 &&"
" last == 0/"
"{"
"	this->cpu = (processorid_t)arg0;"
"	this->oldspeed = (uint64_t)arg1;"
"	@times[this->cpu, this->oldspeed] = sum(timestamp - begin);"
"	last = timestamp;"
"}";

static int	pt_cpufreq_setup(void);
static int	pt_cpufreq_snapshot(void);
static int	pt_cpufreq_dtrace_walk(const dtrace_aggdata_t *, void *);
static void	pt_cpufreq_stat_account(double, uint_t);
static int	pt_cpufreq_snapshot_cpu(kstat_ctl_t *, uint_t);
static int	pt_cpufreq_check_pm(void);

static boolean_t pt_check_cpupm_enabled(void);
static void	pt_cpufreq_null_enable(void);

static int
pt_cpufreq_setup(void)
{
	if ((dtp_argv = malloc(sizeof (char *) * DTP_ARG_COUNT)) == NULL)
		return (1);

	if ((dtp_argv[0] = malloc(sizeof (char) * DTP_ARG_LENGTH)) == NULL) {
		free(dtp_argv);
		return (1);
	}

	(void) snprintf(dtp_argv[0], 5, "%d\0", g_ncpus_observed);

	if (PT_ON_CPU) {
		if ((dtp_argv[1] = malloc(sizeof (char) * DTP_ARG_LENGTH))
		    == NULL) {
			free(dtp_argv[0]);
			free(dtp_argv);
			return (1);
		}
		(void) snprintf(dtp_argv[1], 5, "%d\0", g_observed_cpu);
	}

	return (0);
}

/*
 * Perform setup necessary to enumerate and track CPU speed changes
 */
int
pt_cpufreq_stat_prepare(void)
{
	dtrace_prog_t 		*prog;
	dtrace_proginfo_t 	info;
	dtrace_optval_t 	statustime;
	kstat_ctl_t 		*kc;
	kstat_t 		*ksp;
	kstat_named_t 		*knp;
	freq_state_info_t 	*state;
	char 			*s, *token, *prog_ptr;
	int 			err;

	if ((err = pt_cpufreq_setup()) != 0) {
		pt_error("failed to setup %s report (couldn't allocate "
		    "memory)\n", g_msg_freq_state);
		return (errno);
	}

	state = g_pstate_info;
	if ((g_cpu_power_states = calloc((size_t)g_ncpus,
	    sizeof (cpu_power_info_t))) == NULL)
		return (-1);

	/*
	 * Enumerate the CPU frequencies
	 */
	if ((kc = kstat_open()) == NULL)
		return (errno);

	ksp = kstat_lookup(kc, "cpu_info", g_cpu_table[g_observed_cpu], NULL);

	if (ksp == NULL) {
		err = errno;
		(void) kstat_close(kc);
		return (err);
	}

	(void) kstat_read(kc, ksp, NULL);

	knp = kstat_data_lookup(ksp, "supported_frequencies_Hz");
	s = knp->value.str.addr.ptr;

	g_npstates = 0;

	for (token = strtok(s, ":"), s = NULL;
	    token != NULL && g_npstates < NSTATES;
	    token = strtok(NULL, ":")) {

		state->speed = HZ2MHZ(atoll(token));

		if (state->speed > max_cpufreq)
			max_cpufreq = state->speed;

		state->total_time = (uint64_t)0;

		g_npstates++;
		state++;
	}

	if (token != NULL)
		pt_error("CPU exceeds the supported number of %s\n",
		    g_msg_freq_state);

	(void) kstat_close(kc);

	/*
	 * Return if speed transition is not supported
	 */
	if (g_npstates < 2)
		return (-1);

	/*
	 * Setup DTrace to look for CPU frequency changes
	 */
	if ((dtp = dtrace_open(DTRACE_VERSION, 0, &err)) == NULL) {
		pt_error("cannot open dtrace library for the %s report: %s\n",
		    g_msg_freq_state, dtrace_errmsg(NULL, err));
		return (-2);
	}

	/*
	 * Execute different scripts (defined above) depending on
	 * user specified options. Default mode uses dtp_cpufreq.
	 */
	if (PT_ON_CPU)
		prog_ptr = (char *)dtp_cpufreq_c;
	else
		prog_ptr = (char *)dtp_cpufreq;

	if ((prog = dtrace_program_strcompile(dtp, prog_ptr,
	    DTRACE_PROBESPEC_NAME, 0, (1 + g_argc), dtp_argv)) == NULL) {
		pt_error("failed to compile %s program\n", g_msg_freq_state);
		return (dtrace_errno(dtp));
	}

	if (dtrace_program_exec(dtp, prog, &info) == -1) {
		pt_error("failed to enable %s probes\n", g_msg_freq_state);
		return (dtrace_errno(dtp));
	}

	if (dtrace_setopt(dtp, "aggsize", "128k") == -1)
		pt_error("failed to set %s 'aggsize'\n", g_msg_freq_state);

	if (dtrace_setopt(dtp, "aggrate", "0") == -1)
		pt_error("failed to set %s 'aggrate'\n", g_msg_freq_state);

	if (dtrace_setopt(dtp, "aggpercpu", 0) == -1)
		pt_error("failed to set %s 'aggpercpu'\n", g_msg_freq_state);

	if (dtrace_go(dtp) != 0) {
		pt_error("failed to start %s observation\n", g_msg_freq_state);
		return (dtrace_errno(dtp));
	}

	if (dtrace_getopt(dtp, "statusrate", &statustime) == -1) {
		pt_error("failed to get %s 'statusrate'\n", g_msg_freq_state);
		return (dtrace_errno(dtp));
	}

	return (0);
}

/*
 * The DTrace probes have already been enabled, and are tracking
 * CPU speed transitions. Take a snapshot of the aggregations, and
 * look for any CPUs that have made a speed transition over the last
 * sampling interval. Note that the aggregations may be empty if no
 * speed transitions took place over the last interval. In that case,
 * notate that we have already accounted for the time, so that when
 * we do encounter a speed transition in a future sampling interval
 * we can subtract that time back out.
 */
int
pt_cpufreq_stat_collect(double interval)
{
	int i, ret;

	/*
	 * Zero out the interval time reported by DTrace for
	 * this interval
	 */
	for (i = 0; i < g_npstates; i++)
		g_pstate_info[i].total_time = 0;

	for (i = 0; i < g_ncpus; i++)
		g_cpu_power_states[i].dtrace_time = 0;

	if (dtrace_status(dtp) == -1)
		return (-1);

	if (dtrace_aggregate_snap(dtp) != 0)
		pt_error("failed to collect data for %s\n", g_msg_freq_state);

	if (dtrace_aggregate_walk_keyvarsorted(dtp, pt_cpufreq_dtrace_walk,
	    NULL) != 0)
		pt_error("failed to sort data for %s\n", g_msg_freq_state);

	dtrace_aggregate_clear(dtp);

	if ((ret = pt_cpufreq_snapshot()) != 0) {
		pt_error("failed to snapshot %s state\n", g_msg_freq_state);
		return (ret);
	}

	switch (g_op_mode) {
	case PT_MODE_CPU:
		pt_cpufreq_stat_account(interval, g_observed_cpu);
		break;
	case PT_MODE_DEFAULT:
	default:
		for (i = 0; i < g_ncpus_observed; i++)
			pt_cpufreq_stat_account(interval, i);
		break;
	}

	return (0);
}

static void
pt_cpufreq_stat_account(double interval, uint_t cpu)
{
	cpu_power_info_t 	*cpu_pow;
	uint64_t 		speed;
	hrtime_t 		duration;
	int			i;

	cpu_pow = &g_cpu_power_states[cpu];
	speed = cpu_pow->current_pstate;

	duration = (hrtime_t)(interval * NANOSEC) - cpu_pow->dtrace_time;

	/*
	 * 'duration' may be a negative value when we're using or forcing a
	 * small interval, and the amount of time already accounted ends up
	 * being larger than the the former.
	 */
	if (duration < 0)
		return;

	for (i = 0; i < g_npstates; i++) {
		if (g_pstate_info[i].speed == speed) {
			g_pstate_info[i].total_time += duration;
			cpu_pow->time_accounted += duration;
			cpu_pow->speed_accounted = speed;
		}
	}
}

/*
 * Take a snapshot of each CPU's speed by looking through the cpu_info kstats.
 */
static int
pt_cpufreq_snapshot(void)
{
	kstat_ctl_t 	*kc;
	int 		ret;
	uint_t		i;

	if ((kc = kstat_open()) == NULL)
		return (errno);

	switch (g_op_mode) {
	case PT_MODE_CPU:
		ret = pt_cpufreq_snapshot_cpu(kc, g_observed_cpu);
		break;
	case PT_MODE_DEFAULT:
	default:
		for (i = 0; i < g_ncpus_observed; i++)
			if ((ret = pt_cpufreq_snapshot_cpu(kc, i)) != 0)
				break;
		break;
	}

	if (kstat_close(kc) != 0)
		pt_error("couldn't close %s kstat\n", g_msg_freq_state);

	return (ret);
}

static int
pt_cpufreq_snapshot_cpu(kstat_ctl_t *kc, uint_t cpu)
{
	kstat_t 		*ksp;
	kstat_named_t 		*knp;

	ksp = kstat_lookup(kc, "cpu_info", g_cpu_table[cpu], NULL);
	if (ksp == NULL) {
		pt_error("couldn't find 'cpu_info' kstat for CPU %d\n while "
		    "taking a snapshot of %s\n", cpu, g_msg_freq_state);
		return (1);
	}

	if (kstat_read(kc, ksp, NULL) == -1) {
		pt_error("couldn't read 'cpu_info' kstat for CPU %d\n while "
		    "taking a snapshot of %s\n", cpu, g_msg_freq_state);
		return (2);
	}

	knp = kstat_data_lookup(ksp, "current_clock_Hz");
	if (knp == NULL) {
		pt_error("couldn't find 'current_clock_Hz' kstat for CPU %d "
		    "while taking a snapshot of %s\n", cpu, g_msg_freq_state);
		return (3);
	}

	g_cpu_power_states[cpu].current_pstate = HZ2MHZ(knp->value.ui64);

	return (0);
}

/*
 * DTrace aggregation walker that sorts through a snapshot of the
 * aggregation data collected during firings of the cpu-change-speed
 * probe.
 */
/*ARGSUSED*/
static int
pt_cpufreq_dtrace_walk(const dtrace_aggdata_t *data, void *arg)
{
	dtrace_aggdesc_t 	*aggdesc = data->dtada_desc;
	dtrace_recdesc_t 	*cpu_rec, *speed_rec;
	cpu_power_info_t 	*cp;
	int32_t 		cpu;
	uint64_t 		speed;
	hrtime_t 		res;
	int 			i;

	if (strcmp(aggdesc->dtagd_name, "times") == 0) {
		cpu_rec = &aggdesc->dtagd_rec[1];
		speed_rec = &aggdesc->dtagd_rec[2];

		/* LINTED - alignment */
		cpu = *(int32_t *)(data->dtada_data + cpu_rec->dtrd_offset);

		/* LINTED - alignment */
		res = *((hrtime_t *)(data->dtada_percpu[cpu]));

		/* LINTED - alignment */
		speed = *(uint64_t *)(data->dtada_data +
		    speed_rec->dtrd_offset);

		if (speed == 0)
			speed = max_cpufreq;
		else
			speed = HZ2MHZ(speed);

		/*
		 * We have an aggregation record for "cpu" being at "speed"
		 * for an interval of "n" nanoseconds. The reported interval
		 * may exceed the powertop sampling interval, since we only
		 * notice during potentially infrequent firings of the
		 * "speed change" DTrace probe. In this case powertop would
		 * have already accounted for the portions of the interval
		 * that happened during prior powertop samplings, so subtract
		 * out time already accounted.
		 */
		cp = &g_cpu_power_states[cpu];

		for (i = 0; i < g_npstates; i++) {
			if (g_pstate_info[i].speed == speed) {

				if (cp->time_accounted > 0 &&
				    cp->speed_accounted == speed) {
					if (res > cp->time_accounted) {
						res -= cp->time_accounted;
						cp->time_accounted = 0;
						cp->speed_accounted = 0;
					} else {
						return (DTRACE_AGGWALK_NEXT);
					}
				}

				g_pstate_info[i].total_time += res;
				cp->dtrace_time += res;
			}
		}
	}

	return (DTRACE_AGGWALK_NEXT);
}

/*
 * The following functions are used to decide whether CPU power management
 * is enabled, and relate to the use of a private interface (in /dev/pm)
 *
 * The new administrative interface to power management offered by PM 3.0
 * (via poweradm(1m) and libpower(3)) is not suited to determine whether any
 * given specific PM feature (in this case, the enablement of CPU P-states
 * (earlier known as 'CPU PM') is enabled.
 *
 * For the time-being therefore, we use a private interface offered directly
 * by the pm device driver (see pt_check_cpupm_enabled() below)
 */

/*
 * pt_cpufreq_check_pm() -
 *
 *	This routine is called repeatedly (via pt_cpufreq_suggest())
 * from the main loop of powertop.c to see whether CPUPM is on or not.
 *
 * This implementation now checks directly (using /dev/pm), if CPUPM
 * is enabled (thus allowing the use of P-states)
 *
 * returns:
 *
 *     0 if pm is disabled or if CPUPM is not enabled
 *
 *     1 if there's nothing for us to do because:
 *         (a) the system does not support frequency scaling
 *         (b) we cannot open /dev/pm (need check for proper
 *		 authorization above)
 *
 *     2 if cpupm is enabled
 *
 * Notice the ordering of the return values, they will be picked up and
 * switched upon ascendingly.
 */
static int
pt_cpufreq_check_pm(void)
{
	int pm_fd = 0;			/* /dev/pm */

	if (g_npstates < 2) {
		return (1);
	}

	if ((pm_fd = open("/dev/pm", O_RDWR)) < 0) {
		return (1);
	} else {
		(void) close(pm_fd);
	}

	/*
	 * now, let's see if CPUPM is enabled
	 */
	if (pt_check_cpupm_enabled() == B_FALSE) {
		return (0);
	}
	return (2);
}

/*
 * We define a g_cpufreq_msg here, to say that CPUPM is disabled
 * and a null function, which we need to associate with the
 * suggestion that we will post when CPUPM is disabled.
 */
static char *g_cpufreq_msg = "(CPU PM is disabled)";

static void
pt_cpufreq_null_enable(void)
{
}

/*
 * pt_check_cpupm_enabled() -
 *	Access the kernel directly (using /dev/pm) to determine
 * whether cpupm is enabled or not.
 */
static boolean_t pt_check_cpupm_enabled(void)
{
	int		pm_fd;		/* /dev/pm */
	int		ioc_ret = 0;
	boolean_t	cpupm_enabled;	/* kernel CPUPM operating or not */

	/*
	 *  Check that CPUPM is enabled.
	 */

	if ((pm_fd = open("/dev/pm", O_RDWR)) < 0) {
		return (B_FALSE);
	}

	ioc_ret = ioctl(pm_fd, PM_GET_CPUPM_STATE, NULL);
	if (ioc_ret < 0) {
		(void) close(pm_fd);
		return (B_FALSE); /* Should be a sep. error condition really */
	}
	switch (ioc_ret) {
	case PM_CPU_PM_ENABLED:
		cpupm_enabled = B_TRUE;
		break;

	case PM_CPU_PM_DISABLED:
		cpupm_enabled = B_FALSE;
		break;
	default:
		cpupm_enabled = B_FALSE;
		break;
	}
	(void) close(pm_fd);
	return (cpupm_enabled);
}

/*
 * pt_cpufreq_suggest() -
 *
 * 	This is an extern that is called by main (in powertop.c)
 * It installs a suggestion if CPUPM is disabled.
 *
 * If there is a null function, then nothing gets called when the
 * associated key ('X' here) is struck.
 */

void
pt_cpufreq_suggest(void)
{
	int result = pt_cpufreq_check_pm();

	switch (result) {
	/*
	 * 0 -  means that CPUPM (P-states) is not enabled.
	 * Put up a suggestion to let the user know that they
	 * can enable it using poweradm(1m).
	 */
	case 0:
		pt_sugg_add("Suggestion: enable CPU power management "
		    "using poweradm(1m)", 40, 'X', (char *)g_cpufreq_msg,
		    pt_cpufreq_null_enable);

		break;
	/*
	 * 2 -  means that CPUPM (P-states) is enabled.
	 * Take down the suggestion.
	 */
	case 2:
		if (pt_sugg_remove(pt_cpufreq_null_enable) == 0) {
			pt_error("failed to remove a %s suggestion\n",
			    g_cpufreq_msg);
		}
		break;
	}
}
