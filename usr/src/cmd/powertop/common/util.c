/*
 * Copyright 2009, Intel Corporation
 * Copyright 2009, Sun Microsystems, Inc
 *
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

#include <stdarg.h>
#include <stdlib.h>
#include <libgen.h>
#include <unistd.h>
#include <strings.h>
#include <sys/systeminfo.h>
#include <kstat.h>
#include <errno.h>
#include "powertop.h"

static char 	PROG_FMT[] = "%s: ";
static char 	ERR_FMT[] = ": %s";
static char 	*progname;

void
pt_set_progname(char *name)
{
	progname = basename(name);
}

/*PRINTFLIKE1*/
void
pt_error(char *format, ...)
{
	int 	err = errno;
	va_list alist;

	if (g_gui)
		return;

	if (progname != NULL)
		(void) fprintf(stderr, PROG_FMT, progname);

	va_start(alist, format);
	(void) vfprintf(stderr, format, alist);
	va_end(alist);

	if (strchr(format, '\n') == NULL)
		(void) fprintf(stderr, ERR_FMT, strerror(err));
}

/*
 * Returns the number of online CPUs.
 */
uint_t
pt_enumerate_cpus(void)
{
	int	cpuid;
	int	max, cpus_conf;
	uint_t	ncpus = 0;

	max 		= sysconf(_SC_CPUID_MAX);
	cpus_conf	= sysconf(_SC_NPROCESSORS_CONF);

	/* Fall back to one CPU if any of the sysconf calls above failed */
	if (max == -1 || cpus_conf == -1) {
		max = cpus_conf = 1;
	}

	if ((g_cpu_table = malloc(cpus_conf * sizeof (processorid_t))) == NULL)
		return (0);

	for (cpuid = 0; cpuid < max; cpuid++) {
		if (p_online(cpuid, P_STATUS) != -1) {
			g_cpu_table[ncpus] = cpuid;
			ncpus++;
		}
	}
	return (ncpus);
}

void
pt_usage(void)
{
	(void) fprintf(stderr, "%s   %s\n\n", TITLE, COPYRIGHT_INTEL);
	(void) fprintf(stderr, "usage: powertop [option]\n");
	(void) fprintf(stderr, "  -d, --dump [count]	Read wakeups count "
	    "times and print list of top offenders\n");
	(void) fprintf(stderr, "  -t, --time [interval]	Default time to gather "
	    "data in seconds [1-30s]\n");
	(void) fprintf(stderr, "  -v, --verbose		Verbose mode, reports "
	    "kernel cyclic activity\n");
	(void) fprintf(stderr, "  -c, --cpu [CPU]	Only observe a specific"
	    " CPU\n");
	(void) fprintf(stderr, "  -h, --help		Show this help "
	    "message\n");
}

int
pt_get_bit_depth(void)
{
	/*
	 * This little routine was derived from isainfo.c to look up
	 * the system's bit depth. It feeds a 10 byte long buffer to
	 * sysinfo (we only need the first word, sysinfo truncates and
	 * \0 terminates the rest) from which we figure out which isa
	 * we're running on.
	 */
	char	buf[BIT_DEPTH_BUF];

	if (sysinfo(SI_ARCHITECTURE_64, buf, BIT_DEPTH_BUF) == -1)
		if (sysinfo(SI_ARCHITECTURE_32, buf, BIT_DEPTH_BUF) == -1)
			return (-2);

	if (strcmp(buf, "sparc") == 0 || strcmp(buf, "i386") == 0)
		return (32);

	if (strcmp(buf, "sparcv9") == 0 || strcmp(buf, "amd64") == 0)
		return (64);

	return (-3);
}

/*
 * Simple integer comparison routine for the event report qsort(3C).
 */
int
pt_event_compare(const void *p1, const void *p2)
{
	event_info_t i = *((event_info_t *)p1);
	event_info_t j = *((event_info_t *)p2);

	if (i.total_count > j.total_count)
		return (-1);

	if (i.total_count < j.total_count)
		return (1);

	return (0);
}
