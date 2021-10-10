/*
 * Copyright (c) 1990, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved. The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

#include "e.h"

void
column(int type, int p1)
{
	int i;

	lp[p1] = ct - p1 - 1;
	if (dbg) {
		(void) printf(".\t%d column of", type);
		for (i = p1 + 1; i < ct; i++)
			(void) printf(" S%d", lp[i]);
		(void) printf(", rows=%d\n", lp[p1]);
	}
	lp[ct++] = type;
}

void
matrix(int p1)
{
	int nrow, ncol, i, j, k, hb, b, val[100];
	char *space;

	space = "\\ \\ ";
	nrow = lp[p1];	/* disaster if rows inconsistent */
	ncol = 0;
	for (i = p1; i < ct; i += lp[i] + 2) {
		ncol++;
		if (dbg)
			(void) printf(".\tcolct=%d\n", lp[i]);
	}
	for (k = 1; k <= nrow; k++) {
		hb = b = 0;
		j = p1 + k;
		for (i = 0; i < ncol; i++) {
			hb = max(hb, eht[lp[j]]-ebase[lp[j]]);
			b = max(b, ebase[lp[j]]);
			j += nrow + 2;
		}
		if (dbg)
			(void) printf(".\trow %d: b=%d, hb=%d\n", k, b, hb);
		j = p1 + k;
		for (i = 0; i < ncol; i++) {
			ebase[lp[j]] = b;
			eht[lp[j]] = b + hb;
			j += nrow + 2;
		}
	}
	j = p1;
	for (i = 0; i < ncol; i++) {
		lpile(lp[j+lp[j]+1], j+1, j+lp[j]+1);
		val[i] = yyval;
		j += nrow + 2;
	}
	yyval = oalloc();
	eht[yyval] = eht[val[0]];
	ebase[yyval] = ebase[val[0]];
	lfont[yyval] = rfont[yyval] = 0;
	if (dbg)
		(void) printf(".\tmatrix S%d: r=%d, c=%d, h=%d, b=%d\n",
		    yyval, nrow, ncol, eht[yyval], ebase[yyval]);
	(void) printf(".ds %d \"", yyval);
	for (i = 0; i < ncol; i++) {
		(void) printf("\\*(%d%s", val[i], i == ncol-1 ? "" : space);
		ofree(val[i]);
	}
	(void) printf("\n");
	ct = p1;
}
