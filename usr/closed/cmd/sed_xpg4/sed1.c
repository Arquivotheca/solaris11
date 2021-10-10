/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * COPYRIGHT NOTICE
 *
 * This source code is designated as Restricted Confidential Information
 * and is subject to special restrictions in a confidential disclosure
 * agreement between HP, IBM, SUN, NOVELL and OSF.  Do not distribute
 * this source code outside your company without OSF's specific written
 * approval.  This source code, and all copies and derivative works
 * thereof, must be returned or destroyed at request. You must retain
 * this notice on any copies which you make.
 *
 * (c) Copyright 1990, 1991, 1992, 1993 OPEN SOFTWARE FOUNDATION, INC.
 * ALL RIGHTS RESERVED
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * COMPONENT_NAME: sed1.c
 *
 * Based on OSF sed(1) command with POSIX/XCU4 Spec:1170 changes.
 *
 * FUNCTIONS: execute, match, substitute, dosub, place, command, gline,
 * arout and growbuff.
 *
 */

#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <ctype.h>
#include <libintl.h>
#include <wchar.h>
#include "sed.h"

/* Maximum no. of subexpressions allowed in an RE */
static regmatch_t	pmatch[REG_SUBEXP_MAX + 1];
static char	*start;
union reptr	*abuf[ABUFSIZE+1];
union reptr **aptr;
static char	ibuf[512];
static char    *cbp;
static char    *ebp;
static int	dolflag = 0;
static int	sflag = 0;
static int	jflag = 0;
static int	delflag = 0;
static int	cdflag = 0;
static int	nempty = 1;

off_t	seekto = 1;	/* to calculate where to seek to at QUIT command */
off_t	lnum;
char	*spend;
int	nflag;
off_t	tlno[NLINES];
static int	f;
union reptr	*pending;

static char *cur_fname;

static char *gline(char *);
static int match(regex_t *, int);
static void command(union reptr *);
static void arout(void);
static void dosub(char *);
static char *place(char *, char *, char *);
static int substitute(union reptr *);

/*
 *	For each line in the input file, carry out the sequence of
 *	sed commands as  stored in the cmdspace structure.
 */

void
execute(char *file)
{
	struct addr	*p1, *p2;
	union reptr    *ipc;
	char	*p3;
	int	c;
	char    *execp;

	cur_fname = "standard input";
	if (file) {
		if ((f = open(file, 0)) < 0) {
			(void) fprintf(stderr,
			gettext("sed: Cannot open file %s.\n"), file);
			exit_value = 2;
		}
		cur_fname = file;
	} else
		f = 0;

	seekto = 1;	/* reinitialize seek for each file */
	ebp = ibuf;
	cbp = ibuf;

	if (pending) {
		ipc = pending;
		pending = 0;
		goto yes;
	}

	for (;;) {	/* new cycle */
		/*
		 * after D command, do not read new input line on
		 * new cycle if pattern space is not empty (nempty)
		 */

		if (nempty == 1)
			if ((execp = gline(linebuf)) == badp) {
				(void) close(f);
				return;
			}

		spend = execp;

		for (ipc = cmdspace; ipc->r1.command; ) {

			p1 = ipc->r1.ad1;
			p2 = ipc->r1.ad2;

			start = linebuf;
			if (p1) {

				if (ipc->r1.inar) {
					if (p2->afl == STRA &&
						*p2->ad.str == CEND) {
						p1 = 0;
					} else if (p2->afl == STRA &&
						*p2->ad.str == CLNUM) {
						p1 = 0;
						c = p2->ad.str[1];
						if (lnum > tlno[c]) {
							ipc->r1.inar = 0;
							if (ipc->r1.negfl)
								goto yes;
							ipc++;
							continue;
						}
						if (lnum == tlno[c]) {
							ipc->r1.inar = 0;
						}
					} else if (p2->afl == REGA &&
						match(p2->ad.re, 0) == 0) {
						ipc->r1.inar = 0;
					}
				} else if (p1->afl == STRA &&
					*p1->ad.str == CEND) {
					if (!dolflag) {
						if (ipc->r1.negfl)
							goto yes;
						ipc++;
						continue;
					}

				} else if (p1->afl == STRA &&
					*p1->ad.str == CLNUM) {
					c = p1->ad.str[1];
					if (lnum != tlno[c]) {
						if (ipc->r1.negfl)
							goto yes;
						ipc++;
						continue;
					}
					if (p2)
						ipc->r1.inar = 1;
				} else if (p1->afl == REGA &&
					match(p1->ad.re, 0) == 0) {
					if (p2)
						ipc->r1.inar = 1;
				} else {
					if (ipc->r1.negfl)
						goto yes;
					ipc++;
					continue;
				}
				/*
				 * if range is invalid (ie 5,2) or
				 * simple (ie 5,5),  we aren't in a range.
				 */
				if (p2 && (p2->afl == STRA) &&
					(*p2->ad.str == CLNUM) &&
					(lnum >= tlno[p2->ad.str[1]]))
					ipc->r1.inar = 0;
			}	/* if */

			if (ipc->r1.negfl) {
				ipc++;
				continue;
			}
	yes:
			command(ipc);

			if (delflag)	/* start next cycle */
				break;

			if (jflag) {
				jflag = 0;
				/*
				 * if current ipc does not have label
				 * branch to end of script
				 */
				if ((ipc = ipc->r2.lb1) == 0) {
					ipc = cmdspace;
					break;
				}
			} else
				ipc++;

		}	/* for */


		if (!nflag && !delflag) {

			/*
			 * if default output not suppressed AND pattern
			 * space is not deleted, copy the pattern space
			 * to stdout.
			 */

			if (!delflag) {
				for (p3 = linebuf; p3 < spend; p3++)
					(void) putc(*p3, stdout);
				(void) putc('\n', stdout);

			/* if D command AND pattern space not empty; */
			/* set nempty flag */

				if (*linebuf && cdflag)
					nempty = 1;
				else if (!*linebuf && cdflag)
					nempty = 0;
			}
		}

		if (aptr > abuf) {
			arout();
		}

		delflag = 0;	/* turn OFF start next cycle flag */

	}	/* for (;;) */
}

/*
 *	Match text in linebuf according to reg. expr. in exp
 *	returning offsets to the start and end of all matched
 *	substrings in the pmatch structure.
 */

static int
match(regex_t *exp, int gf)
{
	char	*p;
	int	eflags;
	int	len;

	eflags = 0;
	if (gf) {	/* If not searching from beginning of line */
			/* If previously matched null string, increment start */
		if (pmatch[0].rm_so == pmatch[0].rm_eo) {
			if ((len = mblen(start, MB_CUR_MAX)) < 1) {
				len = 1;
			}
			start += len;
		}
		p = start;
		eflags |= REG_NOTBOL;
	} else
		p = linebuf;

	return (regexec(exp, p, (exp->re_nsub + 1), pmatch, eflags));
}

/*
 *	Perform substitution in linebuf according to reg. expr. and
 *	replacement text stored in ipc structure.
 */

static int
substitute(union reptr *ipc)
{
	int	scount, sdone;
	scount = sdone = 0;

	/*
	 *	If re1 is in fact a line number address and not a r.e.
	 *	the replacement text should be inserted at the start of
	 *	the current line. Hence set start = end = 0 and return (1).
	 */
	start = linebuf;
	if (ipc->r1.re1 && ipc->r1.re1->afl == STRA)  {
		pmatch[0].rm_so = pmatch[0].rm_eo = 0;
		sflag = 1;
		dosub(ipc->r1.rhs);
		return (1);
	}

	/*
	 *	If gfl is a positive integer n substitute for
	 *	the nth match in linebuf. If gfl is GLOBAL_SUB
	 *	substitute for every match on the line.
	 */
	while (match(ipc->r1.re1->ad.re, scount++) == 0) {
		if (ipc->r1.gfl == GLOBAL_SUB || ipc->r1.gfl == scount) {
			sdone++;
			dosub(ipc->r1.rhs);
			if (ipc->r1.gfl != GLOBAL_SUB && scount >= ipc->r1.gfl)
				break;
		} else {
			start += pmatch[0].rm_eo;
		}
		if (!*start)
			break;
	}
	if (sdone)
		sflag = 1;
	return (sdone);
}

/*
 *	Perform substitution of replacement text
 *	for the recognized (and matched) regular expresson.
 */
static void
dosub(char *rhsbuf)
{

	char *lp, *rp, *sp;
	char *p;	/* p is needed to store temporary value of sp for */
			/* calls to growbuff */
	char  c;
	int	len;

	lp = linebuf;
	rp = rhsbuf;
	while (gsize < (start + pmatch[0].rm_so - linebuf + 1))
		growbuff(&gsize, &genbuf, &gend, NULL);
	sp = genbuf;
	while (lp < (start + pmatch[0].rm_so))
		*sp++ = *lp++;
	while ((c = *rp) != '\0') {
		while (sp >= gend) {
			p = sp;
			growbuff(&gsize, &genbuf, &gend, &p);
			sp = p;
		}
		if ((len = mblen(rp, MB_CUR_MAX)) < 1) {
			(void) fprintf(stderr, gettext(CGMES), linebuf);
			exit(2);
		}
		if ((len == 1) && (c == '&')) {
			rp++;
			sp = place(sp, (start + pmatch[0].rm_so),
				(start + pmatch[0].rm_eo));
			continue;
		} else if ((len == 1) && (c == '\\')) {
			c = *++rp;		/* discard \\ */

			/* POSIX2/XPG4 states single digit backref. */
			/* expression to be in range 1 to 9 inclusive */

			if (c >= '1' && c <= '9') {
				rp++;
				sp = place(sp, (start + pmatch[c-'0'].rm_so),
					(start + pmatch[c-'0'].rm_eo));
				continue;
			}
		}
		while ((sp+len) >= gend) {
			p = sp;
			growbuff(&gsize, &genbuf, &gend, &p);
			sp = p;
		}
		while (len--)
			*sp++ = *rp++;
	}	/* while */
	lp = start + pmatch[0].rm_eo;
	/* Set start to end of replacement text */
	start = sp - genbuf + linebuf;
	/* Copy the rest of the linebuf to genbuf */
	while (*sp++ = *lp++)
		while (sp >= gend) {
			p = sp;
			growbuff(&gsize, &genbuf, &gend, &p);
			sp = p;
		}
	/* Copy genbuf back into linebuf */
	while (lsize < gsize)
		growbuff(&lsize, &linebuf, &lbend, &start);
	lp = linebuf;
	sp = genbuf;
	while (*lp++ = *sp++);
	spend = lp-1;
}

/*
 *	Copy the text between al1 and al2 into asp.
 *	Used to copy replacement text into buffer during
 *	substitute commands.
 *	N.B. The growbuff here is safe since the new asp is
 *	returned as a result in every usage of this function
 *	and hence if the buffer moves, everything will move with it.
 */
static char
*place(char *asp, char *al1, char *al2)
{
	char *l1, *l2, *sp;
	char *p;

	sp = asp;
	l1 = al1;
	l2 = al2;
	while (l1 < l2) {
		*sp++ = *l1++;
		while (sp >= gend) {
			p = sp;
			growbuff(&gsize, &genbuf, &gend, &p);
			sp = p;
		}
	}
	return (sp);
}

/*
 *	Process commands stored in ipc structure.
 */
static void
command(union reptr *ipc)
{
	int    i;
	char   *p1, *p2;
	char    *execp;

	int col;

	switch (ipc->r1.command) {

		case ACOM:
			*aptr++ = ipc;
			if (aptr >= &abuf[ABUFSIZE]) {
				(void) fprintf(stderr,
		gettext("sed: Too many appends after line %lld\n"), lnum);
				aptr--;
			}
			*aptr = 0;
			break;

		case BCOM:
			jflag = 1;
			break;

		case CCOM:
			delflag = 1;	/* turn ON start next cycle flag */
			if (!ipc->r1.inar || dolflag) {
				for (p1 = ipc->r1.rhs; *p1; )
					(void) putc(*p1++, stdout);
				(void) putc('\n', stdout);
			}
			break;
		case DCOM:
			delflag++;	/* turn ON start next cycle flag */
			break;
		case CDCOM:
			cdflag = 1;	/* D command flag */
			p1 = p2 = linebuf;
			while (*p1 != '\n') {
				if (*p1++ == '\0') {
				/*
				 * found NULL character before <NL>
				 * so start next cycle without writing
				 * pattern space
				 */
					delflag++;
					return;
				}
			}

			p1++;

			/* only preserve pattern space past first <NL> */
			while (*p2++ = *p1++);
			spend = p2-1;
			jflag++;
			break;

		case EQCOM:
			(void) fprintf(stdout, "%lld\n", (offset_t)lnum);
			break;

		case GCOM:
			/* Copy the hold space into the pattern space */
			while (lsize < hsize)
				growbuff(&lsize, &linebuf, &lbend, (char **)0);
			p1 = linebuf;
			p2 = holdsp;
			while (*p1++ = *p2++);
			spend = p1-1;
			break;

		case CGCOM:
			/* Append the hold space into the pattern space */
			*spend++ = '\n';
			while (lbend < (spend + (hspend - holdsp)))
				growbuff(&lsize, &linebuf, &lbend, (char **)0);
			p1 = spend;
			p2 = holdsp;
			while (*p1++ = *p2++);
			spend = p1-1;
			break;

		case HCOM:
			/* Copy the pattern space into the hold space */
			while (hsize < lsize)
				growbuff(&hsize, &holdsp, &hend, (char **)0);
			p1 = holdsp;
			p2 = linebuf;
			while (*p1++ = *p2++);
			hspend = p1-1;
			break;

		case CHCOM:
			/* Append the pattern space into the hold space */

			if (*(hspend-1) != '\n')
				*hspend++ = '\n';
			while ((hspend + (spend - linebuf)) >= hend)
				growbuff(&hsize, &holdsp, &hend, &hspend);
			p1 = hspend;
			p2 = linebuf;
			while (*p1++ = *p2++);
			hspend = p1-1;
			break;

		case ICOM:
			for (p1 = ipc->r1.rhs; *p1; )
				(void) putc(*p1++, stdout);
			(void) putc('\n', stdout);
			break;

		case LCOM:
			{
			int	chrwid, len;
			wchar_t c1;
			unsigned char	c2;

			p2 = genbuf;
			col = 0;
			p1 = linebuf;
			while (*p1) {
				if ((len =
					mbtowc(&c1, p1, MB_CUR_MAX)) == -1 ||
						len == 0) {
					len = 1;
				}
				if (len == 1) {
					/* display single byte characters */
					/* or illegal characters */
					c2 = *(unsigned char *)p1++;
					/* LINTED constant in cond. expr. */
					if (1) { /* XPG/4 Compliance - Omega */
						chrwid = (!iswprint(c1) ? 4
							: wcwidth(c1));
						if (col+chrwid >= 72) {
							*p2 = '\0';
					(void) fprintf(stdout, "%s\\\n",
						genbuf);
							p2 = genbuf;
							col = 0;
						}
						col += chrwid;
						if (c2 == '\007') {
							*p2++ = '\\';
							*p2++ = 'a';
							col -= 2;
						} else if (c2 == '\b') {
							*p2++ = '\\';
							*p2++ = 'b';
							col -= 2;
						} else if (c2 == '\f') {
							*p2++ = '\\';
							*p2++ = 'f';
							col -= 2;
						} else if (c2 == '\r') {
							*p2++ = '\\';
							*p2++ = 'r';
							col -= 2;
						} else if (c2 == '\t') {
							*p2++ = '\\';
							*p2++ = 't';
							col -= 2;
						} else if (c2 == '\v') {
							*p2++ = '\\';
							*p2++ = 'v';
							col -= 2;
						} else if (c2 == '\n') {
							*p2++ = '\\';
							*p2++ = 'n';
							col -= 2;
						} else if (c2 == '\\') {
							*p2++ = '\\';
							*p2++ = '\\';
							col -= 2;
						} else if (!iswprint(c1)) {
					(void) sprintf(p2, "\\%03o", c2);
							p2 += 4;
						} else
							*p2++ = c2;
					}
				} else {
					/* display multi-byte characters */
					chrwid = (!iswprint(c1) ? 4*len
						: wcwidth(c1));
					if (col+chrwid >= 72) {
						col = 0;
						*p2++ = '\\';
						*p2++ = '\n';
					}
					col += chrwid;
					if (!iswprint(c1)) {
						while (len--) {
						(void) sprintf(p2, "\\%03o",
						*(unsigned char *)p1++);
							p2 += 4;
						}
					} else {
						p1 += len;
						p2 += wctomb(p2, c1);
					}
				}
			} /* end while */
			} /* end case LCOM */
			*p2 = '\0';
			(void) fprintf(stdout, "%s$\n", genbuf);
			break;

		case NCOM:
			/*
			 * write pattern_sp to stdout if default output
			 * is not suppressed
			 */
			if (!nflag) {
				for (p1 = linebuf; p1 < spend; p1++)
					(void) putc(*p1, stdout);
				(void) putc('\n', stdout);
			}

			if (aptr > abuf)
				arout();
			if ((execp = gline(linebuf)) == badp) {
				pending = ipc;
				delflag = 1;
				break;
			}
			spend = execp;

			break;
		case CNCOM:
			if (aptr > abuf)
				arout();

			/* to avoid multiple NEWLINE chars. in pattern space */
			/* when multiple file operands are specified */

			if (*(spend-1) != '\n')
				*spend++ = '\n';
			if ((execp = gline(spend)) == badp) {
				pending = ipc;
				delflag = 1;
				break;
			}
			spend = execp;
			break;

		case PCOM:
			for (p1 = linebuf; p1 < spend; p1++)
				(void) putc(*p1, stdout);
			(void) putc('\n', stdout);
			break;
		case CPCOM:
	cpcom:
			for (p1 = linebuf; *p1 != '\n' && *p1 != '\0'; )
				(void) putc(*p1++, stdout);
			(void) putc('\n', stdout);
			break;

		case QCOM:
			if (!nflag) {
				for (p1 = linebuf; p1 < spend; p1++)
					(void) putc(*p1, stdout);
				(void) putc('\n', stdout);
			}
			if (aptr > abuf) arout();
			(void) fclose(stdout);
			seekto++;	/* to compensate for <NL> character */
			(void) lseek(f, (off_t)seekto, SEEK_SET);
			exit(0);
			/* FALLTHROUGH not really */
		case RCOM:

			*aptr++ = ipc;
			if (aptr >= &abuf[ABUFSIZE]) {
				(void) fprintf(stderr,
		gettext("sed: Too  many reads after line %lld.\n"),
					lnum);
				aptr--;
			}
			*aptr = 0;
			break;

		case SCOM:
			i = substitute(ipc);
			if (ipc->r1.pfl && i) {
				if (ipc->r1.pfl == 1) {
					for (p1 = linebuf; p1 < spend; p1++)
						(void) putc(*p1, stdout);
					(void) putc('\n', stdout);
				} else
					goto cpcom;
			}
			if (i && ipc->r1.fcode)
				goto wcom;
			break;

		case TCOM:
			if (sflag == 0)  break;
			sflag = 0;
			jflag = 1;
			break;

		wcom:
		case WCOM:
			(void) fprintf(ipc->r1.fcode, "%s\n", linebuf);
			break;
		case XCOM:
			/* Exchange contents of the pattern and hold spaces */
			while (gsize < lsize)
				growbuff(&gsize, &genbuf, &gend, (char **)0);
			p1 = linebuf;
			p2 = genbuf;
			while (*p2++ = *p1++);
			while (lsize < hsize)
				growbuff(&lsize, &linebuf, &lbend, (char **)0);
			p1 = holdsp;
			p2 = linebuf;
			while (*p2++ = *p1++);
			spend = p2 - 1;
			while (hsize < gsize)
				growbuff(&hsize, &holdsp, &hend, (char **)0);
			p1 = genbuf;
			p2 = holdsp;
			while (*p2++ = *p1++);
			hspend = p2 - 1;
			break;

		case YCOM:
			p1 = linebuf;

			{
			wchar_t *yp;
			char	*p3;
			char *p;
			wchar_t c, tc;
			int	length;

			p3 = genbuf;
			do {
				length = mbtowc(&c, p1, MB_CUR_MAX);
				if (length < 0) {
					c = (wchar_t)((unsigned char)*p1++);
				} else if (length == 0) {
					break;
				} else {
					p1 += length;
				}
				yp = ipc->r1.ytxt;
				/* Find replacement in yp for character c */
				tc = 0;
				while (*yp) {
					if (c == *yp) {
						tc = *++yp;
						break;
					}
					yp += 2;
				}
				if (*yp == '\0') /* replacement not found */
					tc = c;
				length = wctomb(p3, tc);
				if (length < 0) {
					*p3++ = (char)tc;
				} else if (length == 0) {
					break;
				} else {
					p3 += length;
				}
				while (p3 >= gend) {
					p = p3;
					growbuff(&gsize, &genbuf, &gend, &p);
					p3 = p;
				}
			} while (*p1);

			while (lsize < gsize)
				growbuff(&lsize, &linebuf, &lbend, (char **)0);
			*p3 = *p1;	/* Copy the NULL */
			p3 = genbuf;
			p1 = linebuf;
			while (*p1++ = *p3++);
			spend = p1-1;
			}
			break;
	}

}

/*
 *	Read a line from input file.
 */
static char
*gline(char *addr)
{
	char   *p1, *p2;
	char	*p;
	int	c;

	p1 = addr;
	p2 = cbp;
	for (;;) {
		if (p2 >= ebp) {
			if ((c = read(f, ibuf, 512)) <= 0) {
			/* Don't drop lines w/ missing newline */
				if (p2 == cbp)
					return (badp);
				else
				{
			(void) fprintf(stderr,
		gettext("sed: Missing newline at end of file %s.\n"),
			cur_fname);
					exit_value = 2;
					(void) close(f);
					if (eargc == 0)
						dolflag = 1;
					p2 = ibuf;
					ebp = ibuf + c;
					break;
				}
			}
			p2 = ibuf;
			ebp = ibuf+c;
		}
		if ((c = *p2++) == '\n') {
			if (p2 >=  ebp) {
				if ((c = read(f, ibuf, 512)) <= 0) {
					(void) close(f);
					if (eargc == 0)
						dolflag = 1;
				}

				p2 = ibuf;
				ebp = ibuf + c;
			}
			break;
		}
		if (c) {
			while (p1 >= lbend) {
				p = p1;
				growbuff(&lsize, &linebuf, &lbend, &p);
				p1 = p;
			}
			*p1++ = c;
			seekto += 1;
		}
	}
	lnum++;
	sflag = 0;
	while (p1 >= lbend) {
		p = p1;
		growbuff(&lsize, &linebuf, &lbend, &p);
		p1 = p;
	}
	*p1 = 0;
	cbp = p2;

	return (p1);
}

static void
arout(void)
{
	char	*p1;
	FILE	*fi;
	char	c;
	int	t;

	aptr = abuf - 1;
	while (*++aptr) {
		if ((*aptr)->r1.command == ACOM) {
			for (p1 = (*aptr)->r1.rhs; *p1; )
				(void) putc(*p1++, stdout);
			(void) putc('\n', stdout);
		} else {
			if ((fi = fopen((*aptr)->r1.rhs, "r")) == NULL)
				continue;
			while ((t = getc(fi)) != EOF) {
				c = t;
				(void) putc(c, stdout);
			}
			(void) fclose(fi);
		}
	}
	aptr = abuf;
	*aptr = 0;
}

/*
 *	Dynamic memory allocation for buffer storage.
 *	lenp	- current and new length
 *	startp - current and new start of buffer
 *	endp - current and new end of buffer
 *	intop - pointer into current => new
 */
void
growbuff(unsigned int *lenp, char **startp, char **endp, char **intop)
{
	char *new;

	if (!*lenp)
		*lenp = LBSIZE;
	else
		*lenp <<= 1;
	if (*startp)
		new = realloc(*startp, *lenp);
	else
		new = malloc(*lenp);
	if (new == NULL) {
		(void) fprintf(stderr,
			gettext("sed: Memory allocation failed.\n"));
		exit(2);
	}
	if (intop)
		*intop = new + (*intop - *startp);
	*startp = new;
	*endp = new + *lenp;
}
