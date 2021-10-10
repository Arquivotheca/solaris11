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

/* t4.c: read table specification */
#include "t..c"
#include <stdlib.h>

int oncol;

void	readspec(void);

void
getspec(void)
{
	int icol, i;

	for (icol = 0; icol < MAXCOL; icol++) {
		sep[icol] = -1;
		evenup[icol] = 0;
		cll[icol][0] = 0;
		for (i = 0; i < MAXHEAD; i++) {
			csize[i][icol][0] = 0;
			vsize[i][icol][0] = 0;
			font[i][icol][0] = lefline[i][icol] = 0;
			ctop[i][icol] = 0;
			style[i][icol] = 'l';
		}
	}
	nclin = ncol = 0;
	oncol = 0;
	left1flg = rightl = 0;
	readspec();
	(void) fprintf(tabout, ".rm");
	for (i = 0; i < ncol; i++)
		(void) fprintf(tabout, " %02d", 80+i);
	(void) fprintf(tabout, "\n");
}

void
readspec(void)
{
	int icol, c, sawchar, stopc, i;
	char sn[10], *snp, *temp;

	sawchar = icol = 0;
	while (c = get1char()) {
		switch (c) {
		default:
			if (c != tab)
				error(gettext(
				    "bad table specification character"));
			/*FALLTHROUGH*/

		case ' ': /* note this is also case tab */
			continue;

		case '\n':
			if (sawchar == 0)
				continue;
			/*FALLTHROUGH*/

		case ',':
		case '.': /* end of table specification */
			ncol = max(ncol, icol);
			if (lefline[nclin][ncol] > 0) {
				ncol++;
				rightl++;
			}
			if (sawchar)
				nclin++;
			if (nclin >= MAXHEAD)
				error(gettext(
				    "too many lines in specification"));
			icol = 0;
			if (ncol == 0 || nclin == 0)
				error(gettext("no specification"));
			if (c == '.') {
				while ((c = get1char()) != NULL && c != '\n')
					if (c != ' ' && c != '\t')
						error(gettext("dot not last "
						    "character on "
						    "format line"));
				/* fix up sep - default is 3 except at edge */
				for (icol = 0; icol < ncol; icol++)
					if (sep[icol] < 0)
						sep[icol] =
						    icol+1 < ncol ? 3 : 1;
				if (oncol == 0)
					oncol = ncol;
				else if (oncol+2 < ncol)
					error(gettext("tried to widen table "
					    "in T&, not allowed"));
				return;
			}
			sawchar = 0;
			continue;

		case 'C':
		case 'S':
		case 'R':
		case 'N':
		case 'L':
		case 'A':
			c += ('a'-'A');
			/*FALLTHROUGH*/

		case '_':
			if (c == '_')
				c = '-';
			/*FALLTHROUGH*/

		case '=':
		case '-':
		case '^':
		case 'c':
		case 's':
		case 'n':
		case 'r':
		case 'l':
		case 'a':
			style[nclin][icol] = c;
			if (c == 's' && icol <= 0)
				error(gettext(
				    "first column can not be S-type"));
			if (c == 's' && style[nclin][icol-1] == 'a') {
				(void) fprintf(tabout,
				    ".tm warning: can't span a-type cols, "
				    "changed to l\n");
				style[nclin][icol-1] = 'l';
			}
			if (c == 's' && style[nclin][icol-1] == 'n') {
				(void) fprintf(tabout,
				    ".tm warning: can't span n-type cols, "
				    "changed to c\n");
				style[nclin][icol-1] = 'c';
			}
			icol++;
			if (c == '^' && nclin <= 0)
				error(gettext(
				    "first row can not contain vertical span"));
			if (icol >= MAXCOL)
				error(gettext("too many columns in table"));
			sawchar = 1;
			continue;

		case 'b':
		case 'i':
			c += 'A'-'a';
			/* FALLTHRU */

		case 'B':
		case 'I':
			if (sawchar == 0)
				continue;
			if (icol == 0)
				continue;
			snp = font[nclin][icol-1];
			snp[0] = (c == 'I' ? '2' : '3');
			snp[1] = 0;
			continue;

		case 't':
		case 'T':
			if (sawchar == 0) {
				continue;
			}
			if (icol > 0)
				ctop[nclin][icol-1] = 1;
			continue;

		case 'd':
		case 'D':
			if (sawchar == 0)
				continue;
			if (icol > 0)
				ctop[nclin][icol-1] = -1;
			continue;

		case 'f':
		case 'F':
			if (sawchar == 0)
				continue;
			if (icol == 0)
				continue;
			snp = font[nclin][icol-1];
			snp[0] = snp[1] = stopc = 0;
			for (i = 0; i < 2; i++) {
				c = get1char();
				if (i == 0 && c == '(') {
					stopc = ')';
					c = get1char();
				}
				if (c == 0)
					break;
				if (c == stopc) {
					stopc = 0;
					break;
				}
				if (stopc == 0)
					if (c == ' ' || c == tab)
						break;
				if (c == '\n') {
					un1getc(c);
					break;
				}
				snp[i] = c;
				if (c >= '0' && c <= '9')
					break;
			}
			if (stopc)
				if (get1char() != stopc)
					error(gettext(
					    "Nonterminated font name"));
			continue;

		case 'P':
		case 'p':
			if (sawchar == 0)
				continue;
			if (icol <= 0)
				continue;
			temp = snp = csize[nclin][icol-1];
			while (c = get1char()) {
				if (c == ' ' || c == tab || c == '\n')
					break;
				if (c == '-' || c == '+') {
					if (snp > temp)
						break;
					else
						*snp++ = c;
				} else if (digit(c)) {
					*snp++ = c;
				} else {
					break;
				}
				if (snp - temp > 4)
					error(gettext("point size too large"));
			}
			*snp = 0;
			if (atoi(temp) > 36)
				error(gettext("point size unreasonable"));
			un1getc(c);
			continue;

		case 'V':
		case 'v':
			if (sawchar == 0)
				continue;
			if (icol <= 0)
				continue;
			temp = snp = vsize[nclin][icol-1];
			while (c = get1char()) {
				if (c == ' ' || c == tab || c == '\n')
					break;
				if (c == '-' || c == '+') {
					if (snp > temp)
						break;
					else
						*snp++ = c;
				} else if (digit(c)) {
					*snp++ = c;
				} else {
					break;
				}
				if (snp - temp > 4)
					error(gettext("vertical spacing "
					    "value too large"));
			}
			*snp = 0;
			un1getc(c);
			continue;

		case 'w':
		case 'W':
			if (sawchar == 0) {
				/*
				 * This should be an error case.
				 * However, for the backward-compatibility,
				 * treat as if 'c' was specified.
				 */
				style[nclin][icol] = 'c';
				icol++;
				if (icol >= MAXCOL) {
					error(gettext(
					    "too many columns in table"));
				}
				sawchar = 1;
			}

			snp = cll[icol-1];
			stopc = 0;
			while (c = get1char()) {
				if (snp == cll[icol-1] && c == '(') {
					stopc = ')';
					continue;
				}
				if (!stopc && (c > '9' || c < '0'))
					break;
				if (stopc && c == stopc)
					break;
				*snp++ = c;
			}
			*snp = 0;
			if (snp - cll[icol-1] > CLLEN)
				error(gettext("column width too long"));
			if (!stopc)
				un1getc(c);
			continue;

		case 'e':
		case 'E':
			if (sawchar == 0)
				continue;
			if (icol < 1)
				continue;
			evenup[icol-1] = 1;
			evenflg = 1;
			continue;

		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
			sn[0] = c;
			snp = sn + 1;
			while (digit(*snp++ = c = get1char()))
				;
			un1getc(c);
			sep[icol-1] = max(sep[icol-1], numb(sn));
			continue;

		case '|':
			lefline[nclin][icol]++;
			if (icol == 0)
				left1flg = 1;
			continue;
		}
	}
	error(gettext("EOF reading table specification"));
}
