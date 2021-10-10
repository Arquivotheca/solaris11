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

/* tu.c: draws horizontal lines */
#include "t..c"

void
makeline(int i, int c, int lintype)
{
	int cr, type, shortl;

	type = thish(i, c);
	if (type == 0)
		return;
	cr = c;
	if (table[i][c].col == NULL)
		shortl = 0;
	else
		shortl = (table[i][c].col[0] == '\\');
	if (c > 0 && !shortl && thish(i, c-1) == type)
		return;
	if (shortl == 0)
		for (cr = c; cr < ncol && (ctype(i, cr) == 's' ||
		    type == thish(i, cr)); cr++)
			;
	else
		for (cr = c+1; cr < ncol && ctype(i, cr) == 's'; cr++)
			;
	drawline(i, c, cr-1, lintype, 0, shortl);
}

void
fullwide(int i, int lintype)
{
	int cr, cl;

	if (!pr1403)
		(void) fprintf(tabout, ".nr %d \\n(.v\n.vs \\n(.vu-\\n(.sp\n",
		    SVS);
	cr = 0;
	while (cr < ncol) {
		cl = cr;
		while (i > 0 && vspand(prev(i), cl, 1))
			cl++;
		for (cr = cl; cr < ncol; cr++)
			if (i > 0 && vspand(prev(i), cr, 1))
				break;
		if (cl < ncol)
			drawline(i, cl, (cr < ncol ? cr-1 : cr), lintype, 1, 0);
	}
	(void) fprintf(tabout, "\n");
	if (!pr1403)
		(void) fprintf(tabout, ".vs \\n(%du\n", SVS);
}

void
drawline(int i, int cl, int cr, int lintype, int noheight, int shortl)
{
	char *exhr, *exhl, *lnch;
	int lcount, ln, linpos, oldpos, nodata;

	lcount = 0;
	exhr = exhl = "";
	switch (lintype) {
	case '-':
		lcount = 1;
		break;
	case '=':
		lcount = pr1403 ? 1 : 2;
		break;
	case SHORTLINE:
		lcount = 1;
		break;
	}
	if (lcount <= 0)
		return;
	nodata = cr-cl >= ncol || noheight || allh(i);
	if (!nodata)
		(void) fprintf(tabout, "\\v'-.5m'");
	for (ln = oldpos = 0; ln < lcount; ln++) {
		linpos = 2*ln - lcount +1;
		if (linpos != oldpos)
			(void) fprintf(tabout, "\\v'%dp'", linpos-oldpos);
		oldpos = linpos;
		if (shortl == 0) {
			tohcol(cl);
			if (lcount > 1) {
				switch (interv(i, cl)) {
				case TOP:
					exhl = ln == 0 ? "1p" : "-1p";
					break;
				case BOT:
					exhl = ln == 1 ? "1p" : "-1p";
					break;
				case THRU:
					exhl = "1p";
					break;
				}
				if (exhl[0])
					(void) fprintf(tabout, "\\h'%s'", exhl);
			} else if (lcount == 1) {
				switch (interv(i, cl)) {
				case TOP:
				case BOT:
					exhl = "-1p";
					break;
				case THRU:
					exhl = "1p";
					break;
				}
				if (exhl[0])
					(void) fprintf(tabout, "\\h'%s'", exhl);
			}
			if (lcount > 1) {
				switch (interv(i, cr+1)) {
				case TOP:
					exhr = ln == 0 ? "-1p" : "+1p";
					break;
				case BOT:
					exhr = ln == 1 ? "-1p" : "+1p";
					break;
				case THRU:
					exhr = "-1p";
					break;
				}
			} else if (lcount == 1) {
				switch (interv(i, cr+1)) {
				case TOP:
				case BOT:
					exhr = "+1p";
					break;
				case THRU:
					exhr = "-1p";
					break;
				}
			}
		} else {
			(void) fprintf(tabout, "\\h'|\\n(%du'", cl+CLEFT);
		}
		(void) fprintf(tabout, "\\s\\n(%d", LSIZE);
		if (linsize)
			(void) fprintf(tabout, "\\v'-\\n(%dp/6u'", LSIZE);
		if (shortl)
			(void) fprintf(tabout, "\\l'|\\n(%du'", cr+CRIGHT);
		else {
			lnch = "\\(ul";
			if (pr1403)
				lnch = lintype == 2 ? "=" : "\\(ru";
			if (cr+1 >= ncol)
				(void) fprintf(tabout, "\\l'|\\n(TWu%s%s'",
				    exhr, lnch);
			else
				(void) fprintf(tabout,
				    "\\l'(|\\n(%du+|\\n(%du)/2u%s%s'",
				    cr+CRIGHT, cr+1+CLEFT, exhr, lnch);
		}
		if (linsize)
			(void) fprintf(tabout, "\\v'\\n(%dp/6u'", LSIZE);
		(void) fprintf(tabout, "\\s0");
	}
	if (oldpos != 0)
		(void) fprintf(tabout, "\\v'%dp'", -oldpos);
	if (!nodata)
		(void) fprintf(tabout, "\\v'+.5m'");
}

void
getstop(void)
{
	int i, c, k, junk, stopp;

	stopp = 1;
	for (i = 0; i < MAXLIN; i++)
		linestop[i] = 0;
	for (i = 0; i < nlin; i++)
		for (c = 0; c < ncol; c++) {
			k = left(i, c, &junk);
			if (k >= 0 && linestop[k] == 0)
				linestop[k] = ++stopp;
		}
	if (boxflg || allflg || dboxflg)
		linestop[0] = 1;
}

int
left(int i, int c, int *lwidp)
{
	int kind, li, lj;

	/*
	 * Returns -1 if no line to left
	 * returns number of line where it starts
	 * stores into lwid the kind of line
	 */
	*lwidp = 0;
	kind = lefdata(i, c);
	if (kind == 0)
		return (-1);
	if (i+1 < nlin)
		if (lefdata(next(i), c) == kind)
			return (-1);
	while (i >= 0 && lefdata(i, c) == kind)
		i = prev(li = i);
	if (prev(li) == -1)
		li = 0;
	*lwidp = kind;
	for (lj = i+1; lj < li; lj++)
		if (instead[lj] && strcmp(instead[lj], ".TH") == 0)
			return (li);
	for (i = i+1; i < li; i++)
		if (fullbot[i])
			li = i;

	return (li);
}

int
lefdata(int i, int c)
{
	int ck;

	if (i >= nlin)
		i = nlin-1;
	if (ctype(i, c) == 's') {
		for (ck = c; ctype(i, ck) == 's'; ck--)
			;
		if (thish(i, ck) == 0)
			return (0);
	}
	i = stynum[i];
	i = lefline[i][c];
	if (i > 0)
		return (i);
	if (dboxflg && c == 0)
		return (2);
	if (allflg)
		return (1);
	if (boxflg && c == 0)
		return (1);

	return (0);
}

int
next(int i)
{
	while (i+1 < nlin) {
		i++;
		if (!fullbot[i] && !instead[i])
			break;
	}

	return (i);
}

int
prev(int i)
{
	while (--i >= 0 && (fullbot[i] || instead[i]))
		;

	return (i);
}
