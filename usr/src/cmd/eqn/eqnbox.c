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
eqnbox(int p1, int p2, int lu)
{
	int b, h;
	char *sh;

	yyval = p1;
	b = max(ebase[p1], ebase[p2]);
	eht[yyval] = h = b + max(eht[p1]-ebase[p1], eht[p2]-ebase[p2]);
	ebase[yyval] = b;
	if (dbg)
		(void) printf(".\te:eb: S%d <- S%d S%d; b=%d, h=%d\n",
		    yyval, p1, p2, b, h);
	if (rfont[p1] == ITAL && lfont[p2] == ROM)
		sh = "\\|";
	else
		sh = "";
	if (lu) {
		(void) printf(".nr %d \\w'\\s%d\\*(%d%s'\n", p1, ps, p1, sh);
		(void) printf(
		    ".ds %d \\h'|\\n(97u-\\n(%du'\\*(%d\n", p1, p1, p1);
	}
	(void) printf(".as %d \"%s\\*(%d\n", yyval, sh, p2);
	rfont[p1] = rfont[p2];
	ofree(p2);
}
