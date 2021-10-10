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

/*	factor	COMPILE:	cc -O factor.c -s -i -lm -o factor	*/
/*
 * works up to 14 digit numbers
 * running time is proportional to sqrt(n)
 * accepts arguments either as input or on command line
 * 0 input terminates processing
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

double nn, vv;
double huge = 1.0e14;
double sq[] = {
	10, 2, 4, 2, 4, 6, 2, 6,
	4, 2, 4, 6, 6, 2, 6, 4,
	2, 6, 4, 6, 8, 4, 2, 4,
	2, 4, 8, 6, 4, 6, 2, 4,
	6, 2, 6, 6, 4, 2, 4, 6,
	2, 6, 4, 2, 4, 2, 10, 2,
};

void try(double);

int
main(int argc, char *argv[])
{
	int test = 1;
	int ret;
	int j;
	double junk, temp;
	double fr;
	double ii;

	if (argc > 2) {
		(void) printf("Usage: factor number\n");
		exit(1);
	}
	if (argc == 2) {
		ret = sscanf(argv[1], "%lf", &nn);
		test = 0;
		(void) printf("%.0f\n", nn);
		goto start;
	}
	while (test == 1) {
		ret = scanf("%lf", &nn);
start:
		if ((ret < 1) || (nn == 0.0)) {
			exit(0);
		}
		if ((nn < 0.0) || (nn > huge)) {
			(void) printf("Ouch!\n");
			continue;
		}
		fr = modf(nn, &junk);
		if (fr != 0.0) {
			(void) printf("Not an integer!\n");
			continue;
		}
		vv = 1. + sqrt(nn);
		try(2.0);
		try(3.0);
		try(5.0);
		try(7.0);
		ii = 1.0;
		while (ii <= vv) {
			for (j = 0; j < 48; j++) {
				ii += sq[j];
retry:
				(void) modf(nn/ii, &temp);
				if (nn == temp*ii) {
					(void) printf("     %.0f\n", ii);
					nn = nn/ii;
					vv = 1 + sqrt(nn);
					goto retry;
				}
			}
		}
		if (nn > 1.0) {
			(void) printf("     %.0f\n", nn);
		}
		(void) printf("\n");
	}
	return (0);
}

void
try(double arg)
{
	double temp;
retry:
	(void) modf(nn/arg, &temp);
	if (nn == temp*arg) {
		(void) printf("     %.0f\n", arg);
		nn = nn/arg;
		vv = 1 + sqrt(nn);
		goto retry;
	}
}
