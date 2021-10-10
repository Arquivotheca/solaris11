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
#include "e.def"

void
integral(int p, int p1, int p2)
{
#ifndef	NEQN
	if (p1 != 0)
		(void) printf(
		    ".ds %d \\h'-0.4m'\\v'0.4m'\\*(%d\\v'-0.4m'\n", p1, p1);
	if (p2 != 0)
		(void) printf(".ds %d \\v'-0.3m'\\*(%d\\v'0.3m'\n", p2, p2);
#endif
	if (p1 != 0 && p2 != 0)
		shift2(p, p1, p2);
	else if (p1 != 0)
		bshiftb(p, SUB, p1);
	else if (p2 != 0)
		bshiftb(p, SUP, p2);
	if (dbg)
		(void) printf(".\tintegral: S%d; h=%d b=%d\n",
		    p, eht[p], ebase[p]);
	lfont[p] = ROM;
}

void
setintegral(void)
{
	char *f;

	yyval = oalloc();
	f = "\\(is";
#ifndef NEQN
	(void) printf(".ds %d \\s%d\\v'.1m'\\s+4%s\\s-4\\v'-.1m'\\s%d\n",
	    yyval, ps, f, ps);
	eht[yyval] = VERT(EM(1.15, ps+4));
	ebase[yyval] = VERT(EM(0.3, ps));
#else	/* NEQN */
	(void) printf(".ds %d %s\n", yyval, f);
	eht[yyval] = VERT(2);
	ebase[yyval] = 0;
#endif	/* NEQN */
	lfont[yyval] = rfont[yyval] = ROM;
}
