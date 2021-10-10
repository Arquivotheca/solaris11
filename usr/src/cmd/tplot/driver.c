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
 * Copyright (c) 1992, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

#include <stdio.h>
#include <math.h>
#include <plot.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

float deltx = 4095.0;
float delty = 4095.0;

static void	fplt(FILE *);
static int	getsi(FILE *);
static void	getsd(char *, int, FILE *);

int
main(int argc, char **argv)
{
	int std = 1;
	FILE *fin;

	while (argc-- > 1) {
		if (*argv[1] == '-') {
			switch (argv[1][1]) {
				case 'l':
					deltx = atoi(&argv[1][2]) - 1;
					break;
				case 'w':
					delty = atoi(&argv[1][2]) - 1;
					break;
				}

		} else {
			std = 0;
			if ((fin = fopen(argv[1], "r")) == NULL) {
				(void) fprintf(stderr,
				    "can't open %s\n", argv[1]);
				exit(1);
			}
			fplt(fin);
		}
		argv++;
	}
	if (std)
		fplt(stdin);
	return (0);
}


static void
fplt(FILE *fin)
{
	int c;
	char s[256];
	int xi, yi, x0, y0, x1, y1, r, dx, n, i;
	int pat[256];

	openpl();
	while ((c = getc(fin)) != EOF) {
		switch (c) {
		case 'm':
			xi = getsi(fin);
			yi = getsi(fin);
			move(xi, yi);
			break;
		case 'l':
			x0 = getsi(fin);
			y0 = getsi(fin);
			x1 = getsi(fin);
			y1 = getsi(fin);
			line(x0, y0, x1, y1);
			break;
		case 't':
			getsd(s, sizeof (s), fin);
			label(s);
			break;
		case 'e':
			erase();
			break;
		case 'p':
			xi = getsi(fin);
			yi = getsi(fin);
			point(xi, yi);
			break;
		case 'n':
			xi = getsi(fin);
			yi = getsi(fin);
			cont(xi, yi);
			break;
		case 's':
			x0 = getsi(fin);
			y0 = getsi(fin);
			x1 = getsi(fin);
			y1 = getsi(fin);
			space(x0, y0, x1, y1);
			break;
		case 'a':
			xi = getsi(fin);
			yi = getsi(fin);
			x0 = getsi(fin);
			y0 = getsi(fin);
			x1 = getsi(fin);
			y1 = getsi(fin);
			arc(xi, yi, x0, y0, x1, y1);
			break;
		case 'c':
			xi = getsi(fin);
			yi = getsi(fin);
			r = getsi(fin);
			circle(xi, yi, r);
			break;
		case 'f':
			getsd(s, sizeof (s), fin);
			linemod(s);
			break;
		case 'd':
			xi = getsi(fin);
			yi = getsi(fin);
			dx = getsi(fin);
			n = getsi(fin);
			if (n < 0 || n > (sizeof (pat) / sizeof (int)))
				exit(1);
			for (i = 0; i < n; i++)
				pat[i] = getsi(fin);
			dot(xi, yi, dx, n, pat);
			break;
			}
		}
	closepl();
}

static int
getsi(FILE *fin)
{
	/* get an integer stored in 2 ascii bytes. */
	short a, b;
	if ((b = getc(fin)) == EOF)
		return (EOF);
	if ((a = getc(fin)) == EOF)
		return (EOF);
	a = a << 8;
	return (a | b);
}

static void
getsd(char *s, int maxlen, FILE *fin)
{
	int len;

	if (fgets(s, maxlen, fin) == NULL)
		exit(1);

	/* fgets copies the trailing \n which we don't want */
	len = strlen(s);
	if (s[len - 1] == '\n')
		s[len - 1] = '\0';
}

int
matherr(struct exception *x)
{
	if (x->type == DOMAIN) {
		errno = EDOM;
		if (strcmp("log", x->name) == 0)
			x->retval = (-HUGE);
		else
			x->retval = 0;
		return (1);
	} else  if ((x->type) == SING) {
		errno = EDOM;
		x->retval = (-HUGE);
		return (1);
	} else
		return (0);
}
