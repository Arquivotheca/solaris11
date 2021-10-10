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

/* t7.c: control to write table entries */
#include "t..c"

#define	realsplit	((ct == 'a' || ct == 'n') && table[ldata][c].rcol)

void	need(void);
void	deftail(void);

void
runout(void)
{
	int i;

	if (boxflg || allflg || dboxflg)
		need();
	if (ctrflg) {
		(void) fprintf(tabout, ".nr #I \\n(.i\n");
		(void) fprintf(tabout, ".in +(\\n(.lu-\\n(TWu-\\n(.iu)/2u\n");
	}
	(void) fprintf(tabout, ".fc %c %c\n", F1, F2);
	(void) fprintf(tabout, ".nr #T 0-1\n");
	deftail();
	for (i = 0; i < nlin; i++)
		putline(i, i);
	if (leftover)
		yetmore();
	(void) fprintf(tabout, ".fc\n");
	(void) fprintf(tabout, ".nr T. 1\n");
	(void) fprintf(tabout, ".T# 1\n");
	if (ctrflg)
		(void) fprintf(tabout, ".in \\n(#Iu\n");
}

void
runtabs(int lform, int ldata)
{
	int c, ct, vforml, lf;

	(void) fprintf(tabout, ".ta ");
	for (c = 0; c < ncol; c++) {
	vforml = lform;
	for (lf = prev(lform); lf >= 0 && vspen(table[lf][c].col);
	    lf = prev(lf))
		vforml = lf;
	if (fspan(vforml, c))
		continue;
	switch (ct = ctype(vforml, c)) {
		case 'n':
		case 'a':
			if (table[ldata][c].rcol)
				if (lused[c]) /* Zero field width */
					(void) fprintf(tabout, "\\n(%du ",
					    c+CMID);
			/*FALLTHROUGH*/

		case 'c':
		case 'l':
		case 'r':
			if (realsplit ? rused[c] : (used[c] + lused[c]))
				(void) fprintf(tabout, "\\n(%du ", c+CRIGHT);
			continue;

		case 's':
			if (lspan(lform, c))
				(void) fprintf(tabout, "\\n(%du ", c+CRIGHT);
			continue;
		}
	}
	(void) fprintf(tabout, "\n");
}

int
ifline(char *s)
{
	if (!point((int)s))
		return (0);
	if (s[0] == '\\')
		s++;
	if (s[1])
		return (0);
	if (s[0] == '_')
		return ('-');
	if (s[0] == '=')
		return ('=');

	return (0);
}

void
need(void)
{
	int texlin, horlin, i;

	for (texlin = horlin = i = 0; i < nlin; i++) {
		if (allh(i))
			horlin++;
		else if (instead[i] != 0)
			continue;
		else
			texlin++;
	}
	(void) fprintf(tabout, ".ne %dv+%dp\n", texlin, 2 * horlin);
	/*
	 * For nroff runs, we need to reserve space for the full height of the
	 * horizontal rules.  If we don't reserve sufficient height, we'll have
	 * problems trying to draw the vertical lines across the page boundary.
	 */
	(void) fprintf(tabout, ".if n .ne %dv\n", 2 * texlin + 2 * horlin + 2);
}

void
deftail(void)
{
	int i, c, lf, lwid;

	for (i = 0; i < MAXHEAD; i++)
		if (linestop[i])
			(void) fprintf(tabout, ".nr #%c 0-1\n",
			    linestop[i]+'a'-1);

	(void) fprintf(tabout, ".nr #a 0-1\n");
	(void) fprintf(tabout, ".eo\n");
	(void) fprintf(tabout, ".de T#\n");
	(void) fprintf(tabout, ".ds #d .d\n");
	(void) fprintf(tabout, ".if \\(ts\\n(.z\\(ts\\(ts .ds #d nl\n");
	(void) fprintf(tabout, ".mk ##\n");
	(void) fprintf(tabout, ".nr ## -1v\n");
	(void) fprintf(tabout, ".ls 1\n");

	for (i = 0; i < MAXHEAD; i++)
		if (linestop[i])
			(void) fprintf(tabout,
			    ".if \\n(#T>=0 .nr #%c \\n(#T\n",
			    linestop[i]+'a'-1);

	if (boxflg || allflg || dboxflg) { /* bottom of table line */
		if (!pr1403)
			(void) fprintf(tabout,
			    ".if \\n(T. .vs \\n(.vu-\\n(.sp\n");
		(void) fprintf(tabout, ".if \\n(T. ");
		drawline(nlin, 0, ncol, dboxflg ? '=' : '-', 1, 0);
		(void) fprintf(tabout, "\n.if \\n(T. .vs\n");
		/*
		 * T. is really an argument to a macro but because of
		 * eqn we don't dare pass it as an argument and reference
		 * by $1
		 */
	}

	for (c = 0; c < ncol; c++) {
		if ((lf = left(nlin-1, c, &lwid)) >= 0) {
			(void) fprintf(tabout, ".if \\n(#%c>=0 .sp -1\n",
			    linestop[lf]+'a'-1);
			(void) fprintf(tabout, ".if \\n(#%c>=0 ",
			    linestop[lf]+'a'-1);
			tohcol(c);
			drawvert(lf, nlin-1, c, lwid);
			(void) fprintf(tabout, "\\h'|\\n(TWu'\n");
		}
	}

	if (boxflg || allflg || dboxflg) { /* right hand line */
		(void) fprintf(tabout, ".if \\n(#a>=0 .sp -1\n");
		(void) fprintf(tabout, ".if \\n(#a>=0 \\h'|\\n(TWu'");
		drawvert(0, nlin-1, ncol, dboxflg? 2 : 1);
		(void) fprintf(tabout, "\n");
	}
	(void) fprintf(tabout, ".ls\n");
	(void) fprintf(tabout, "..\n");
	(void) fprintf(tabout, ".ec\n");
}
