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
 * Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <sys/param.h>
#include <sys/priocntl.h>
#include <sys/types.h>
#include "priocntl.h"

static char usage[] =
"usage:	priocntl -l\n\
	priocntl -d [-i idtype] [idlist]\n";

/*
 * A whole lot of to-do for a scheduling class that can't actually be
 * configured or used by user processes.
 */
int
main(int argc, char *argv[])
{
	int	dflag, eflag, iflag, lflag, sflag;
	int	c;
	char	cmdpath[MAXPATHLEN];
	char	basenm[BASENMSZ];
	char	*item;
	id_t	pid, lwp;

	(void) strlcpy(cmdpath, argv[0], MAXPATHLEN);
	(void) strlcpy(basenm, basename(argv[0]), BASENMSZ);
	dflag = eflag = iflag = lflag = sflag = 0;

	while ((c = getopt(argc, argv, "c:dei:ls")) != -1) {
		switch (c) {
		case 'c':
			if (strcmp(optarg, "SDC") != 0)
				fatalerr("error: %s executed for %s class, "
				    "%s is actually sub-command for %s class\n",
				    cmdpath, optarg, cmdpath, "SDC");
			break;

		case 'd':
			dflag++;
			break;

		case 'e':
			eflag++;
			break;

		case 'i':
			iflag++;	/* optarg is parsed, but ignored */
			break;

		case 'l':
			lflag++;
			break;

		case 's':
			sflag++;
			break;

		case '?':
			fatalerr(usage);
			/*NOTREACHED*/

		default:
			break;
		}
	}

	if (sflag && eflag) {
		fatalerr(usage);
	}

	if (sflag || eflag) {
		fatalerr(
		    "priocntl: \"-%c\" may not be used with the %s class\n",
		    (sflag ? 's' : 'e'), "SDC");
	}

	if ((!dflag && !lflag) || (dflag && lflag)) {
		fatalerr(usage);
	}

	if (dflag) {
		pid_lwp_t *pidlist = NULL;
		size_t numread, i;

		/*
		 * No scheduling-class-specific information to print,
		 * but we read the pidlist to avoid generating a SIGPIPE
		 * in the main priocntl(1) process. Once we've read it,
		 * we might as well print it.
		 */
		if ((pidlist = read_pidlist(&numread, stdin)) == NULL) {
			fatalerr("%s: Can't read pidlist.\n", basenm);
		} else if (numread == 0) {
			fatalerr("%s: No pids on input.\n", basenm);
		} else {
			(void) printf("%s\n %11s\n",
			    "SYSTEM DUTY-CYCLE PROCESSES:",
			    "PID[/LWP]");

			for (i = 0; i < numread; i++) {
				item = (char *)pidlist[i];
				pid = (id_t)strtol(item, &item, 10);

				if (*item == '/') {
					item++;
					lwp = (id_t)strtol(item, &item, 10);
					(void) printf("%7ld/%-7ld\n", pid, lwp);
				} else {
					(void) printf("%7ld %-7s\n", pid, " ");
				}
			}
		}

		free_pidlist(pidlist);
	} else {
		(void) printf("SDC (System Duty-Cycle Class)\n");
	}

	return (0);
}
