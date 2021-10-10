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

/*	Copyright (c) 1983, 1984, 1985, 1986, 1987, 1988, 1989 AT&T	*/
/*	All Rights Reserved  	*/

/*
 *	University Copyright- Copyright (c) 1982, 1986, 1988
 *	The Regents of the University of California
 *	All Rights Reserved
 *
 *	University Acknowledgment- Portions of this document are derived from
 *	software developed by the University of California, Berkeley, and its
 *	contributors.
 */


#include <libintl.h>
#include <stdlib.h>

#include "ftp_var.h"

#include <signal.h>
#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <sys/types.h>

/*
 * Reads string from stdin and appends it to the contents of the global
 * "line" buffer. A single space character is placed before the string
 * in the buffer.
 *
 * In any outcome, the global "line" buffer is left terminated
 * with zero character.
 *
 * It is an error when the global "line" buffer does not contain enough
 * space to store a non-zero length string.
 *
 * Returns B_FALSE on error.
 * Returns B_TRUE  on success.
 */
static boolean_t read_string(const char *prompt)
{
	unsigned long len;
	const char *chk;

	/*
	 * Append single space character into the "line" buffer.
	 *
	 * Check that there is enough space to append non-zero length
	 * string into the "line" buffer.
	 *
	 * -4 is for <space> <character> <newline> <null>
	 */
	len = strlen(line);
	if (len <= (BUFSIZE - 4)) {
		line[len] = ' ';
		len++;
		line[len] = 0;
	} else {
		(void) printf("line too long\n");
		return (B_FALSE);
	}

	(void) printf("%s", prompt);

	chk = fgets(line + len, BUFSIZE - len, stdin);

	if (chk != NULL) {
		/*
		 * On success, the last character in the "line" buffer
		 * is newline. Strip it.
		 */
		len = strlen(line);
		if (line[len-1] != '\n') {
			(void) printf("line too long\n");
			return (B_FALSE);
		}
		line[len-1] = 0;

	} else {
		if (ferror(stdin)) {
			(void) printf("input error\n");
		}

		/* EOF */
		return (B_FALSE);
	}

	return (B_TRUE);
}

/* The "code" variable is not used in the caller code. */
void
domacro(int argc, char *argv[])
{
	register int i, j;
	register char *cp1, *cp2;
	int count = 2, loopflg = 0;
	char line2[BUFSIZE];
	struct cmd *c;
	int	len;

	if (argc < 2) {
		stop_timer();
		if (read_string("(macro name) ") == B_FALSE) {
			code = -1;
			return;
		}
		reset_timer();
		makeargv();
		argc = margc;
		argv = margv;
	}
	if (argc < 2) {
		(void) printf("Usage: %s macro_name.\n", argv[0]);
		code = -1;
		return;
	}
	if (strlen(argv[1]) >= MACRO_NAME_SIZE) {
		(void) printf("%s\n", LONG_MACRO_NAME);
		code = -1;
		return;
	}
	for (i = 0; i < macnum; ++i) {
		if (strncmp(argv[1], macros[i].mac_name,
		    MACRO_NAME_SIZE) == 0) {
			break;
		}
	}
	if (i == macnum) {
		(void) printf("'%s' macro not found.\n", argv[1]);
		code = -1;
		return;
	}
	(void) strcpy(line2, line);
TOP:
	cp1 = macros[i].mac_start;
	while (cp1 != macros[i].mac_end) {
		while (isspace(*cp1)) {
			cp1++;
		}
		cp2 = line;
		while (*cp1 != '\0') {
			switch (*cp1) {
			case '\\':
				cp1++;
				if ((len = mblen(cp1, MB_CUR_MAX)) <= 0)
					len = 1;
				(void) memcpy(cp2, cp1, len);
				cp2 += len;
				cp1 += len;
				break;

			case '$':
				if (isdigit(*(cp1+1))) {
					j = 0;
					while (isdigit(*++cp1))
						j = 10 * j +  *cp1 - '0';
					if (argc - 2 >= j) {
						(void) strcpy(cp2, argv[j+1]);
						cp2 += strlen(argv[j+1]);
					}
					break;
				}
				if (*(cp1+1) == 'i') {
					loopflg = 1;
					cp1 += 2;
					if (count < argc) {
						(void) strcpy(cp2, argv[count]);
						cp2 += strlen(argv[count]);
					}
					break;
				}
				/* intentional drop through */
			default:
				if ((len = mblen(cp1, MB_CUR_MAX)) <= 0)
					len = 1;
				(void) memcpy(cp2, cp1, len);
				cp2 += len;
				cp1 += len;
				break;
			}
		}
		*cp2 = '\0';
		makeargv();
		if (margv[0] == NULL) {
			code = -1;
			return;
		} else {
			c = getcmd(margv[0]);
		}
		if (c == (struct cmd *)-1) {
			(void) printf("?Ambiguous command\n");
			code = -1;
		} else if (c == 0) {
			(void) printf("?Invalid command\n");
			code = -1;
		} else if (c->c_conn && !connected) {
			(void) printf("Not connected.\n");
			code = -1;
		} else {
			if (verbose) {
				(void) printf("%s\n", line);
			}
			(*c->c_handler)(margc, margv);
#define	CTRL(c) ((c)&037)
			if (bell && c->c_bell) {
				(void) putchar(CTRL('g'));
			}
			(void) strcpy(line, line2);
			makeargv();
			argc = margc;
			argv = margv;
		}
		if (cp1 != macros[i].mac_end) {
			cp1++;
		}
	}
	if (loopflg && ++count < argc) {
		goto TOP;
	}
}
