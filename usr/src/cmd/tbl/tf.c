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

/* tf.c: save and restore fill mode around table */
#include "t..c"

void
savefill(void)
{
	/* remembers various things: fill mode, vs, ps in mac 35 (SF) */
	(void) fprintf(tabout, ".de %d\n", SF);
	(void) fprintf(tabout, ".ps \\n(.s\n");
	(void) fprintf(tabout, ".vs \\n(.vu\n");
	(void) fprintf(tabout, ".in \\n(.iu\n");
	(void) fprintf(tabout, ".if \\n(.u .fi\n");
	(void) fprintf(tabout, ".if \\n(.j .ad\n");
	(void) fprintf(tabout, ".if \\n(.j=0 .na\n");
	(void) fprintf(tabout, "..\n");
	(void) fprintf(tabout, ".nf\n");

	/* set obx offset if useful */
	(void) fprintf(tabout, ".nr #~ 0\n");
	(void) fprintf(tabout, ".if n .nr #~ 0.6n\n");
}

void
rstofill(void)
{
	(void) fprintf(tabout, ".%d\n", SF);
}

void
endoff(void)
{
	int i;

	for (i = 0; i < MAXHEAD; i++)
		if (linestop[i])
			(void) fprintf(tabout, ".nr #%c 0\n", 'a'+i);
	for (i = 0; i < texct; i++)
		(void) fprintf(tabout, ".rm %c+\n", texstr[i]);
	(void) fprintf(tabout, "%s\n", last);
}

void
ifdivert(void)
{
	(void) fprintf(tabout, ".ds #d .d\n");
	(void) fprintf(tabout, ".if \\(ts\\n(.z\\(ts\\(ts .ds #d nl\n");
}

void
saveline(void)
{
	(void) fprintf(tabout, ".if \\n+(b.=1 .nr d. \\n(.c-\\n(c.-1\n");
	linstart = iline;
}

void
restline(void)
{
	(void) fprintf(tabout,
	    ".if \\n-(b.=0 .nr c. \\n(.c-\\n(d.-%d\n", iline-linstart);
	linstart = 0;
}

void
cleanfc(void)
{
	(void) fprintf(tabout, ".fc\n");
}
