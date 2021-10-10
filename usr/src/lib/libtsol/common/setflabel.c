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
 * Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 *	Change the label of a file
 */

#include <ctype.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <errno.h>
#include <sys/mman.h>

#include <tsol/label.h>

#include "labeld.h"
#include <sys/tsol/label_macro.h>

#include <sys/types.h>

#include <zone.h>
#include <sys/zone.h>
#include <sys/param.h>
#include <string.h>

static int abspath(char *, const char *, char *);

/*
 * setflabel(3TSOL) - set file label
 *
 * This is the library interface to the door call.
 */

#define	clcall callp->param.acall.cargs.setfbcl_arg
#define	clret callp->param.aret.rvals.setfbcl_ret
/*
 *
 *	Exit	error = If error reported, the error indicator,
 *				-1, Unable to access label encodings file;
 *				 0, Invalid binary label passed;
 *				>0, Position after the first character in
 *				    string of error, 1 indicates entire string.
 *			Otherwise, unchanged.
 *
 *	Returns	0, If error.
 *		1, If successful.
 *
 *	Calls	__call_labeld(SETFLABEL)
 *
 */

int
setflabel(const char *path, m_label_t *label)
{
	labeld_data_t	call;
	labeld_data_t	*callp = &call;
	size_t	bufsize = sizeof (labeld_data_t);
	size_t	datasize;
	size_t	path_len;
	static char	cwd[MAXPATHLEN];
	char		canon[MAXPATHLEN];


	/*
	 * If path is relative and we haven't already determined the current
	 * working directory, do so now.  Calculating the working directory
	 * here lets us do the work once, instead of (potentially) repeatedly
	 * in realpath().
	 */
	if (*path != '/' && cwd[0] == '\0') {
		if (getcwd(cwd, MAXPATHLEN) == NULL) {
			cwd[0] = '\0';
			return (-1);
		}
	}
	/*
	 * Find an absolute pathname in the native file system name space that
	 * corresponds to path, stuffing it into canon.
	 */
	if (abspath(cwd, path, canon) < 0)
		return (-1);

	path_len = strlen(canon) + 1;

	datasize = CALL_SIZE(setfbcl_call_t, path_len - BUFSIZE);
	datasize += 2; /* PAD */

	if (datasize > bufsize) {
		if ((callp = (labeld_data_t *)malloc(datasize)) == NULL) {
			return (-1);
		}
		bufsize = datasize;
	}

	callp->callop = SETFLABEL;

	clcall.sl = *label;
	(void) strcpy(clcall.pathname, canon);

	if (__call_labeld(&callp, &bufsize, &datasize) == SUCCESS) {
		int err = callp->reterr;

		if (callp != &call)
			(void) munmap((void *)callp, bufsize);

		/*
		 * reterr == 0, OK,
		 * reterr < 0, invalid binary label,
		 */
		if (err == 0) {
			if (clret.status > 0) {
				errno = clret.status;
				return (-1);
			} else {
				return (0);
			}
		} else if (err < 0) {
			err = 0;
		}
		errno = ECONNREFUSED;
		return (-1);
	} else {
		if (callp != &call)
			(void) munmap((void *)callp, bufsize);

		/* server not present */
		errno = ECONNREFUSED;
		return (-1);
	}
}  /* setflabel */

#undef	clcall
#undef	clret

/*
 * Convert the path given in raw to canonical, absolute, symlink-free
 * form, storing the result in the buffer named by canon, which must be
 * at least MAXPATHLEN bytes long.  If wd is non-NULL, assume that it
 * points to a path for the current working directory and use it instead
 * of invoking getcwd; accepting this value as an argument lets our caller
 * cache the value, so that realpath (called from this routine) doesn't have
 * to recalculate it each time it's given a relative pathname.
 *
 * Return 0 on success, -1 on failure.
 */
int
abspath(char *wd, const char *raw, char *canon)
{
	char		absbuf[MAXPATHLEN];

	/*
	 * Preliminary sanity check.
	 */
	if (raw == NULL || canon == NULL)
		return (-1);

	/*
	 * If the path is relative, convert it to absolute form,
	 * using wd if it's been supplied.
	 */
	if (raw[0] != '/') {
		char	*limit = absbuf + sizeof (absbuf);
		char	*d;

		/* Fill in working directory. */
		if (wd != NULL)
			(void) strncpy(absbuf, wd, sizeof (absbuf));
		else if (getcwd(absbuf, strlen(absbuf)) == NULL)
			return (-1);

		/* Add separating slash. */
		d = absbuf + strlen(absbuf);
		if (d < limit)
			*d++ = '/';

		/* Glue on the relative part of the path. */
		while (d < limit && (*d++ = *raw++))
			continue;

		raw = absbuf;
	}

	/*
	 * Call realpath to canonicalize and resolve symlinks.
	 */
	return (realpath(raw, canon) == NULL ? -1 : 0);
}
