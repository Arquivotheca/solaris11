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
 * Copyright (c) 1988, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/



/*
 *	UNIX shell
 */
#include	"defs.h"

#define	exit(a)	flushb(); return (a)

extern int exitval;

int
echo(int argc, unsigned char **argv)
{
	unsigned char	*cp;
	int	i, wd;
	int	nflg = 0;
	int	j;
	int	len;
	wchar_t	wc;

	if (ucb_builtins) {

		nflg = 0;
		if (argc > 1 && argv[1][0] == '-' &&
		    argv[1][1] == 'n' && !argv[1][2]) {
			nflg++;
			argc--;
			argv++;
		}

		for (i = 1; i < argc; i++) {
			sigchk();

			for (cp = argv[i]; *cp; cp++) {
				prc_buff(*cp);
			}

			if (i < argc-1)
				prc_buff(' ');
		}

		if (nflg == 0)
			prc_buff('\n');
		exit(0);
	} else {
		if (--argc == 0) {
			prc_buff('\n');
			exit(0);
		}

		for (i = 1; i <= argc; i++) {
			sigchk();
			for (cp = argv[i]; *cp; cp++) {
				if ((len = mbtowc(&wc, (char *)cp,
				    MB_LEN_MAX)) <= 0) {
					prc_buff(*cp);
					continue;
				}

				if (wc == '\\') {
					switch (*++cp) {
					case 'b':
						prc_buff('\b');
						continue;
					case 'c':
						exit(0);

					case 'f':
						prc_buff('\f');
						continue;

					case 'n':
						prc_buff('\n');
						continue;

					case 'r':
						prc_buff('\r');
						continue;

					case 't':
						prc_buff('\t');
						continue;

					case 'v':
						prc_buff('\v');
						continue;

					case '\\':
						prc_buff('\\');
						continue;
					case '0':
						j = wd = 0;
						while ((*++cp >= '0' &&
						    *cp <= '7') && j++ < 3) {
							wd <<= 3;
							wd |= (*cp - '0');
						}
						prc_buff(wd);
						--cp;
						continue;

					default:
						cp--;
					}
					prc_buff(*cp);
					continue;
				} else {
					for (; len > 0; len--)
						prc_buff(*cp++);
					cp--;
					continue;
				}
			}
			prc_buff(i == argc? '\n': ' ');
		}
		exit(0);
	}
}
