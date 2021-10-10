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
mark(int p1)
{
	markline = 1;
	(void) printf(".ds %d \\k(97\\*(%d\n", p1, p1);
	yyval = p1;
	if (dbg)
		(void) printf(".\tmark %d\n", p1);
}

void
lineup(int p1)
{
	markline = 1;
	if (p1 == 0) {
		yyval = oalloc();
		(void) printf(".ds %d \\h'|\\n(97u'\n", yyval);
	}
	if (dbg)
		(void) printf(".\tlineup %d\n", p1);
}
