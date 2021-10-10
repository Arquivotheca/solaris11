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
 * Copyright (c) 1989, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

#include <sys/types.h>
#include <stdio.h>
#include <userdefs.h>
#include "messages.h"
#include <unistd.h>
#include <sys/stat.h>
#include <utime.h>
#include <limits.h>
#include <strings.h>
#include "users.h"

#define	SBUFSZ	(2 * PATH_MAX)
#define	CBUFSZ	(SBUFSZ + 100)

extern int access(), rm_files();
static boolean_t add_escape();

static char cmdbuf[CBUFSZ];	/* buffer for system call */

/*
 *	Move directory contents from one place to another
 */
int
move_dir(char *from, char *to, char *login, int copy_flag)
		/* directory to move files from */
		/* directory to move files to */
		/* login id of owner */
{
	size_t len = 0;
	struct stat statbuf;
	struct utimbuf times;
	char	escape_to_str[SBUFSZ];
	char	escape_from_str[SBUFSZ];
	/*
	 * ***** THIS IS WHERE SUFFICIENT SPACE CHECK GOES
	 */

	if (access(from, F_OK) == 0) {	/* home dir exists */

		/*
		 * Check that to dir is not a subdirectory of from
		 */
		len = strlen(from);
		if (strncmp(from, to, len) == 0 &&
		    strncmp(to+len, "/", 1) == 0) {
			errmsg(M_RMFILES);
			return (EX_HOMEDIR);
		}

		/* Escape the shell special characters */
		if (!add_escape(to, escape_to_str, sizeof (escape_to_str)) ||
		    !add_escape(from, escape_from_str,
		    sizeof (escape_from_str))) {
			return (EX_HOMEDIR);
		}

		/* move all files */
		(void) snprintf(cmdbuf, CBUFSZ,
		    "cd \"%s\" && /usr/bin/find . -print "
		    "| /usr/bin/cpio -m -pd \"%s\"",
		    escape_from_str, escape_to_str);

		if (execute_cmd_str(cmdbuf, NULL, 0) > 0) {
			errmsg(M_NOSPACE, from, to);
			return (EX_NOSPACE);
		}

		/* Retain the original permission and modification time */
		if (stat(from, &statbuf) == 0) {
			chmod(to, statbuf.st_mode);
			times.actime = statbuf.st_atime;
			times.modtime = statbuf.st_mtime;
			(void) utime(to, &times);

		}

		/* Remove the files in the old place */
		if (copy_flag == 0)
			(void) rm_files(from, login);
	}
	return (EX_SUCCESS);
}

/*
 * Return the string with added characters \ to escape the shell
 * special characters $ ` " \
 * RETURNS: 1 if escaped string fits in destination buffer
 *          0 for overflow
 */
static boolean_t
add_escape(char *src, char *dest, int size)
{
	char	ch, *sp, *dp;
	int	destlen = 0;

	sp = src;
	dp = dest;

	while ((ch = *sp) != '\0') {
		switch (ch) {
		case '$':
		case '`':
		case '"':
		case '\\':
			if (destlen >= (size - 2)) {
				return (0);
			}
			*dp++ = '\\';
			*dp++ = *sp;
			destlen += 2;
			break;
		default:
			if (destlen >= size - 1) {
				return (0);
			}
			*dp++ = *sp;
			destlen++;
		}
		sp++;
	}
	*dp = '\0';
	return (1);
}
