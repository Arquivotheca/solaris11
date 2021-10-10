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

/* tt.c: subroutines for drawing horizontal lines */
#include "t..c"

int
ctype(int il, int ic)
{
	if (instead[il])
		return (0);
	if (fullbot[il])
		return (0);
	il = stynum[il];

	return (style[il][ic]);
}

int
min(int a, int b)
{
	return (a < b ? a : b);
}

int
fspan(int i, int c)
{
	c++;

	return (c < ncol && ctype(i, c) == 's');
}

int
lspan(int i, int c)
{
	int k;

	if (ctype(i, c) != 's')
		return (0);
	c++;
	if (c < ncol && ctype(i, c) == 's')
		return (0);
	for (k = 0; ctype(i, --c) == 's'; k++)
		;

	return (k);
}

int
ctspan(int i, int c)
{
	int k;

	c++;
	for (k = 1; c < ncol && ctype(i, c) == 's'; k++)
		c++;

	return (k);
}

void
tohcol(int ic)
{
	if (ic == 0)
		(void) fprintf(tabout, "\\h'|0'");
	else
		(void) fprintf(tabout, "\\h'(|\\n(%du+|\\n(%du)/2u'",
		    ic+CLEFT, ic+CRIGHT-1);
}

/* Return true if every element in line i is horizontal */
/* also at least one must be horizontal */

int
allh(int i)
{
	int c, one, k;

	if (fullbot[i])
		return (1);
	for (one = c = 0; c < ncol; c++) {
		k = thish(i, c);
		if (k == 0)
			return (0);
		if (k == 1)
			continue;
		one = 1;
	}

	return (one);
}

int
thish(int i, int c)
{
	int t;
	char *s;
	struct colstr *pc;

	if (c < 0)
		return (0);
	if (i < 0)
		return (0);
	t = ctype(i, c);
	if (t == '_' || t == '-')
		return ('-');
	if (t == '=')
		return ('=');
	if (t == '^')
		return (1);
	if (fullbot[i])
		return (fullbot[i]);
	if (t == 's')
		return (thish(i, c-1));
	if (t == 0)
		return (1);
	pc = &table[i][c];
	if (pc == NULL)
		return (0);
	s = (t == 'a' ? pc->rcol : pc->col);
	if (s == 0 || (point((int)s) && *s == 0))
		return (1);
	if (vspen(s))
		return (1);
	if (t = barent(s))
		return (t);

	return (0);
}
