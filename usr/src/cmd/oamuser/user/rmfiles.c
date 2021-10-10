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
/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/


/*
 * Copyright (c) 1989, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/types.h>
#include <stdio.h>
#include <userdefs.h>
#include <errno.h>
#include "messages.h"
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <sys/param.h>

int
rm_files(char *homedir)
{
	int	status;
	pid_t pid = fork();

	/* delete all files belonging to owner */
	if (pid == 0) {
		if (execl("/usr/bin/rm", "rm", "-rf", homedir, NULL) == -1) {
			errmsg(M_RMFILES);
			exit(EX_HOMEDIR);
		}
	} else if (pid < 0) {
		errmsg(M_RMFILES);
		return (EX_HOMEDIR);
	} else {
		if (waitpid(pid, &status, 0) != pid) {
			if ((WIFEXITED(status) == 0) ||
			    (WEXITSTATUS(status) != 0)) {
				errmsg(M_RMFILES);
				return (EX_HOMEDIR);
			}
		}
	}
	return (EX_SUCCESS);
}
