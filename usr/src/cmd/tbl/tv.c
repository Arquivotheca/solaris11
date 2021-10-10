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

/* tv.c: draw vertical lines */
#include "t..c"

void
drawvert(int start, int end, int c, int lwid)
{
	char *exb = 0, *ext = 0;
	int tp = 0, sl, ln, pos, epb, ept, vm;

	end++;
	vm = 'v';
	/* note: nr 35 has value of 1m outside of linesize */
	while (instead[end])
		end++;

	for (ln = 0; ln < lwid; ln++) {
		epb = ept = 0;
		pos = 2*ln-lwid+1;
		if (pos != tp)
			(void) fprintf(tabout, "\\h'%dp'", pos-tp);
		tp = pos;
		if (end < nlin) {
			if (fullbot[end]|| (!instead[end] && allh(end))) {
				epb = 2;
			} else {
				switch (midbar(end, c)) {
				case '-':
					exb = "1v-.5m";
					break;

				case '=':
					exb = "1v-.5m";
					epb = 1;
					break;
				}
			}
		}
		if (lwid > 1)
			switch (interh(end, c)) {
			case THRU:
				epb -= 1;
				break;

			case RIGHT:
				epb += (ln == 0 ? 1 : -1);
				break;

			case LEFT:
				epb += (ln == 1 ? 1 : -1);
				break;
			}
		if (lwid == 1) {
			switch (interh(end, c)) {
			case THRU:
				epb -= 1;
				break;

			case RIGHT:
			case LEFT:
				epb += 1;
				break;
			}
		}
		if (start > 0) {
			sl = start-1;
			while (sl >= 0 && instead[sl]) sl--;
			if (sl >= 0 && (fullbot[sl] || allh(sl))) {
				ept = 0;
			} else if (sl >= 0) {
				switch (midbar(sl, c)) {
				case '-':
					ext = ".5m";
					break;
				case '=':
					ext = ".5m";
					ept = -1;
					break;
				default:
					vm = 'm';
					break;
				}
			} else {
				ept = -4;
			}
		} else if (start == 0 && allh(0)) {
			ept = 0;
			vm = 'm';
		}
		if (lwid > 1) {
			switch (interh(start, c)) {
			case THRU:
				ept += 1;
				break;
			case LEFT:
				ept += (ln == 0 ? 1 : -1);
				break;
			case RIGHT:
				ept += (ln == 1 ? 1 : -1);
				break;
			}
		} else if (lwid == 1) {
			switch (interh(start, c)) {
			case THRU:
				ept += 1;
				break;
			case LEFT:
			case RIGHT:
				ept -= 1;
				break;
			}
		}
		if (exb)
			(void) fprintf(tabout, "\\v'%s'", exb);
		if (epb)
			(void) fprintf(tabout, "\\v'%dp'", epb);
		(void) fprintf(tabout, "\\s\\n(%d", LSIZE);
		if (linsize)
			(void) fprintf(tabout, "\\v'-\\n(%dp/6u'", LSIZE);
		/* adjustment for T450 nroff boxes */
		(void) fprintf(tabout, "\\h'-\\n(#~u'");
		(void) fprintf(tabout, "\\L'|\\n(#%cu-%s",
		    linestop[start]+'a'-1, vm == 'v' ? "1v" : "\\n(35u");
		if (ext)
			(void) fprintf(tabout, "-(%s)", ext);
		if (exb)
			(void) fprintf(tabout, "-(%s)", exb);
		pos = ept-epb;
		if (pos)
			(void) fprintf(tabout, "%s%dp",
			    pos >= 0 ? "+" : "", pos);
		/*
		 * the string #d is either "nl" or ".d" depending
		 * on diversions; on GCOS not the same
		 */
		(void) fprintf(tabout, "'\\s0\\v'\\n(\\*(#du-\\n(#%cu+%s",
		    linestop[start]+'a'-1, vm == 'v' ? "1v" : "\\n(35u");
		if (ext)
			(void) fprintf(tabout, "+%s", ext);
		if (ept)
			(void) fprintf(tabout, "%s%dp",
			    (-ept) > 0 ? "+" : "", (-ept));
		(void) fprintf(tabout, "'");
		if (linsize)
			(void) fprintf(tabout, "\\v'\\n(%dp/6u'", LSIZE);
	}
}

int
midbar(int i, int c)
{
	int k;

	k = midbcol(i, c);
	if (k == 0 && c > 0)
		k = midbcol(i, c-1);

	return (k);
}

int
midbcol(int i, int c)
{
	int ct;

	while ((ct = ctype(i, c)) == 's')
		c--;
	if (ct == '-' || ct == '=')
		return (ct);
	if (ct = barent(table[i][c].col))
		return (ct);

	return (0);
}

int
barent(char *s)
{
	if (s == 0)
		return (1);
	if (!point((int)s))
		return (1);
	if (s[0] == '\\')
		s++;
	if (s[1] != 0)
		return (0);

	switch (s[0]) {
	case '_':
		return ('-');
	case '=':
		return ('=');
	}

	return (0);
}
