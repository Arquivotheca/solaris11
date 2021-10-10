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
move(int dir, int amt, int p)
{
	int a;

	yyval = p;
#ifndef NEQN
	a = VERT(EM(amt/100.0, EFFPS(ps)));
#else	/* NEQN */
	a = VERT((amt+49)/50);	/* nearest number of half-lines */
#endif	/* NEQN */
	(void) printf(".ds %d ", yyval);
	if (dir == FWD || dir == BACK)	/* fwd, back */
		(void) printf("\\h'%s%du'\\*(%d\n",
		    (dir == BACK) ? "-" : "", a, p);
	else if (dir == UP)
		(void) printf("\\v'-%du'\\*(%d\\v'%du'\n", a, p, a);
	else if (dir == DOWN)
		(void) printf("\\v'%du'\\*(%d\\v'-%du'\n", a, p, a);
	if (dbg)
		(void) printf(".\tmove %d dir %d amt %d; h=%d b=%d\n",
		    p, dir, a, eht[yyval], ebase[yyval]);
}
