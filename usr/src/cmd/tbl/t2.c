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

/* t2.c:  subroutine sequencing for one table */
#include "t..c"

void
tableput(void)
{
	saveline();
	savefill();
	ifdivert();
	cleanfc();
	getcomm();
	getspec();
	gettbl();
	getstop();
	checkuse();
	choochar();
	maktab();
	runout();
	release();
	rstofill();
	endoff();
	restline();
}
