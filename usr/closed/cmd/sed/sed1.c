/*
 * Copyright (c) 1988, 2010, Oracle and/or its affiliates. All rights reserved.
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

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>
#include <libintl.h>
#include <unistd.h>
#include "sed.h"

extern int scrwidth(wchar_t);
static int match(char *, int);
static int substitute(union reptr *);
static void dosub(char *, int);
static void command(union reptr *);
static void arout(void);

union reptr	*abuf[ABUFSIZE];
union reptr **aptr;
char    ibuf[512];
char    *cbp;
char    *ebp;
char    *genbuf;
char    *lbend;
int	dolflag;
int	sflag;
int	jflag;
int	delflag;
long long lnum;
char    *holdsp;
char    *spend;
char    *hspend;
int	nflag;
long long tlno[NLINES];
int	f;
int	numpass;
int	err_status;
union reptr	*pending;
char	*trans[040]  = {
	"\\01",
	"\\02",
	"\\03",
	"\\04",
	"\\05",
	"\\06",
	"\\07",
	"-<",
	"->",
	"\n",
	"\\13",
	"\\14",
	"\\15",
	"\\16",
	"\\17",
	"\\20",
	"\\21",
	"\\22",
	"\\23",
	"\\24",
	"\\25",
	"\\26",
	"\\27",
	"\\30",
	"\\31",
	"\\32",
	"\\33",
	"\\34",
	"\\35",
	"\\36",
	"\\37"
};

void
execute(char *file)
{
	char *p1, *p2;
	union reptr	*ipc;
	int	c;
	char	*execp;

	if (file) {
		if ((f = open(file, 0)) < 0) {
			err_status++;
			(void) fprintf(stderr,
			    gettext("Can't open %s\n"), file);
		}
	} else
		f = 0;

	ebp = ibuf;
	cbp = ibuf;

	if (pending) {
		ipc = pending;
		pending = 0;
		goto yes;
	}

	for (;;) {
		if ((execp = gline(linebuf)) == badp) {
			(void) close(f);
			return;
		}
		spend = execp;

		for (ipc = ptrspace; ipc->r1.command; ) {

			p1 = ipc->r1.ad1;
			p2 = ipc->r1.ad2;

			if (p1) {

				if (ipc->r1.inar) {
					if (*p2 == CEND) {
						p1 = 0;
					} else if (*p2 == CLNUM) {
						c = (unsigned char)p2[1];
						if (lnum > tlno[c]) {
							ipc->r1.inar = 0;
							if (ipc->r1.negfl)
								goto yes;
							ipc = ipc->r1.next;
							continue;
						}
						if (lnum == tlno[c]) {
							ipc->r1.inar = 0;
						}
					} else if (match(p2, 0)) {
						ipc->r1.inar = 0;
					}
				} else if (*p1 == CEND) {
					if (!dolflag) {
						if (ipc->r1.negfl)
							goto yes;
						ipc = ipc->r1.next;
						continue;
					}

				} else if (*p1 == CLNUM) {
					c = (unsigned char)p1[1];
					if (lnum != tlno[c]) {
						if (ipc->r1.negfl)
							goto yes;
						ipc = ipc->r1.next;
						continue;
					}
					if (p2)
						ipc->r1.inar = 1;
				} else if (match(p1, 0)) {
					if (p2)
						ipc->r1.inar = 1;
				} else {
					if (ipc->r1.negfl)
						goto yes;
					ipc = ipc->r1.next;
					continue;
				}
			}

			if (ipc->r1.negfl) {
				ipc = ipc->r1.next;
				continue;
			}
	yes:
			command(ipc);

			if (delflag)
				break;

			if (jflag) {
				jflag = 0;
				if ((ipc = ipc->r2.lb1) == 0) {
					ipc = ptrspace;
					break;
				}
			} else
				ipc = ipc->r1.next;

		}
		if (!nflag && !delflag) {
			for (p1 = linebuf; p1 < spend; p1++)
				(void) putc(*p1, stdout);
			(void) putc('\n', stdout);
		}

		if (aptr > abuf) {
			arout();
		}

		delflag = 0;

	}
}

static int
match(char *expbuf, int gf)
{
	char   *p1;

	if (gf) {
		if (expbuf[0])
			return (0);
		locs = p1 = loc2;
	} else {
		p1 = linebuf;
		locs = 0;
	}
	return (step(p1, expbuf));
}

static int
substitute(union reptr *ipc)
{
	if (match(ipc->r1.re1, 0) == 0)
		return (0);

	numpass = 0;
	sflag = 0;		/* Flags if any substitution was made */
	dosub(ipc->r1.rhs, ipc->r1.gfl);

	if (ipc->r1.gfl) {
		while (*loc2) {
			if (match(ipc->r1.re1, 1) == 0) break;
			dosub(ipc->r1.rhs, ipc->r1.gfl);
		}
	}
	return (sflag);
}

static void
dosub(char *rhsbuf, int n)
{
	char *lp, *sp, *rp;
	int c;

	if (n > 0 && n < 999) {
		numpass++;
		if (n != numpass)
			return;
	}
	sflag = 1;
	while (gsize < (loc1 - linebuf + 1))
		growbuff(&gsize, &genbuf, &gend, (char **)0);
	lp = linebuf;
	sp = genbuf;
	rp = rhsbuf;
	while (lp < loc1)
		*sp++ = *lp++;
	while ((c = *rp++) != 0) {
		if (c == '&') {
			sp = place(sp, loc1, loc2);
			continue;
		}
		if (c == '\\') {
			c = *rp++;
			if (c >= '1' && c <= '9') {
				sp = place(sp, braslist[c-'1'],
				    braelist[c-'1']);
				continue;
			}
		}
		*sp++ = c;
		if (sp >= gend)
			growbuff(&gsize, &genbuf, &gend, &sp);
	}
	lp = loc2;
	loc2 = sp - genbuf + linebuf;
	while (*sp++ = *lp++) {
		if (sp >= gend)
			growbuff(&gsize, &genbuf, &gend, &sp);
	}
	while (lsize < sp - genbuf)
		growbuff(&lsize, &linebuf, &lbend, &loc2);
			/* loc2 points the end of regex(exbuf) in linebuf */
	lp = linebuf;
	sp = genbuf;
	while (*lp++ = *sp++)
		;
	spend = lp-1;
}

char *
place(char *asp, char *al1, char *al2)
{
	char *sp, *l1, *l2;

	sp = asp;
	l1 = al1;
	l2 = al2;
	while (l1 < l2) {
		*sp++ = *l1++;
		if (sp >= gend)
			growbuff(&gsize, &genbuf, &gend, &sp);
	}
	return (sp);
}

static int col; /* column count for 'l' command */

static void
command(union reptr *ipc)
{
	int	i;
	char   *p1, *p2, *p3;
	int length;
	long int c;
	char	*execp;


	switch (ipc->r1.command) {

		case ACOM:
			*aptr++ = ipc;
			if (aptr >= &abuf[ABUFSIZE]) {
				(void) fprintf(stderr, gettext(
				    "Too many appends after line %lld\n"),
				    lnum);
			}
			*aptr = 0;
			break;

		case CCOM:
			delflag = 1;
			if (!ipc->r1.inar || dolflag) {
				for (p1 = ipc->r1.re1; *p1; )
					(void) putc(*p1++, stdout);
				(void) putc('\n', stdout);
			}
			break;
		case DCOM:
			delflag++;
			break;
		case CDCOM:
			p1 = p2 = linebuf;

			while (*p1 != '\n') {
				if (*p1++ == 0) {
					delflag++;
					return;
				}
			}

			p1++;
			while (*p2++ = *p1++)
				;
			spend = p2-1;
			jflag++;
			break;

		case EQCOM:
			(void) fprintf(stdout, "%lld\n", lnum);
			break;

		case GCOM:
			while (lsize < hspend - holdsp + 1)
				growbuff(&lsize, &linebuf, &lbend, (char **)0);
			p1 = linebuf;
			p2 = holdsp;
			while (*p1++ = *p2++)
				;
			spend = p1-1;
			break;

		case CGCOM:
			*spend++ = '\n';
			while (lsize - (spend - linebuf) < hspend - holdsp + 1)
				growbuff(&lsize, &linebuf, &lbend, &spend);
			p1 = spend;
			p2 = holdsp;
			while (*p1++ = *p2++)
				;
			spend = p1-1;
			break;

		case HCOM:
			while (hsize < spend - linebuf + 1)
				growbuff(&hsize, &holdsp, &hend, (char **)0);
			p1 = holdsp;
			p2 = linebuf;
			while (*p1++ = *p2++)
				;
			hspend = p1-1;
			break;

		case CHCOM:
			*hspend++ = '\n';
			while (hend < (hspend + (spend - linebuf) + 1))
				growbuff(&hsize, &holdsp, &hend, &hspend);
			p2 = linebuf;
			p1 = hspend;
			while (*p1++ = *p2++)
				;
			hspend = p1-1;
			break;

		case ICOM:
			for (p1 = ipc->r1.re1; *p1; )
				(void) putc(*p1++, stdout);
			(void) putc('\n', stdout);
			break;

		case BCOM:
			jflag = 1;
			break;


		case LCOM:
			while (gsize <= 71)
				growbuff(&gsize, &genbuf, &gend, (char **)0);
			p1 = linebuf;
			p2 = genbuf;
			col = 1;
			while (*p1) {
				if ((unsigned char)*p1 >= 040) {
					int width;
					length = mbtowc(&c, p1, MULTI_BYTE_MAX);
					/* unprintable bytes? */
					if (length < 0 ||
					    (width = scrwidth(c)) == 0) {
						if (length < 0)
							length = 1;
						while (length--) {
							*p2++ = '\\';
							if (++col >= 72) {
								*p2++ = '\\';
								*p2++ = '\n';
								col = 1;
							}
							*p2++ = (((int)(unsigned char)*p1 >> 6) & 03) + '0';
							if (++col >= 72) {
								*p2++ = '\\';
								*p2++ = '\n';
								col = 1;
							}
							*p2++ = (((int)(unsigned char)*p1 >> 3) & 07) + '0';
							if (++col >= 72) {
								*p2++ = '\\';
								*p2++ = '\n';
								col = 1;
							}
							*p2++ = ((unsigned char) *p1++ & 07) + '0';
							if (++col >= 72) {
								*p2++ = '\\';
								*p2++ = '\n';
								col = 1;
							}
						}
					} else {
						col += width;
						if (col >= 72) {
					/*
					 * print out character on current
					 * line if it doesn't go
					 * go past column 71
					 */
							if (col == 72)
								while (length) {
									*p2++ = *p1++;
									length--;
								}
							*p2++ = '\\';
							*p2++ = '\n';
							col = 1;
						}
						while (length--)
							*p2++ = *p1++;
					}
				} else {
					p3 = trans[(unsigned char)*p1-1];
					while (*p2++ = *p3++)
						if (++col >= 72) {
							*p2++ = '\\';
							*p2++ = '\n';
							col = 1;
						}
					p2--;
					p1++;
				}
			/*
			 * because genbuf is dynamically allocated
			 * lcomend was replaced with &genbuf[71]
			 */
				if (p2 >= &genbuf[71]) {
					*p2 = '\0';
					(void) fprintf(stdout, "%s", genbuf);
					p2 = genbuf;
				}
			}
			*p2 = 0;
			(void) fprintf(stdout, "%s\n", genbuf);
			break;

		case NCOM:
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
			exit(0);
			/*NOTREACHED*/

		case RCOM:

			*aptr++ = ipc;
			if (aptr >= &abuf[ABUFSIZE])
				(void) fprintf(stderr,
				    gettext("Too many reads after line%lld\n"),
				    lnum);

			*aptr = 0;

			break;

		case SCOM:
			i = substitute(ipc);
			if (ipc->r1.pfl && i)
				if (ipc->r1.pfl == 1) {
					for (p1 = linebuf; p1 < spend; p1++)
						(void) putc(*p1, stdout);
					(void) putc('\n', stdout);
				}
				else
					goto cpcom;
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
			while (gsize < (spend - linebuf + 1))
				growbuff(&gsize, &genbuf, &gend, (char **)0);
			p1 = linebuf;
			p2 = genbuf;
			while (*p2++ = *p1++)
				;
			p3 = p2 - 1;

			while (lsize < (hspend - holdsp + 1))
				growbuff(&lsize, &linebuf, &lbend, (char **)0);
			p1 = holdsp;
			p2 = linebuf;
			while (*p2++ = *p1++)
				;
			spend = p2 - 1;

			while (hsize < (p3 - genbuf + 1))
				growbuff(&hsize, &holdsp, &hend, (char **)0);
			p1 = genbuf;
			p2 = holdsp;
			while (*p2++ = *p1++)
				;
			hspend = p2 - 1;
			break;

		case YCOM:
			p1 = linebuf;
			p2 = ipc->r1.re1;
			if (!multibyte)
				while ((*p1 = p2[(unsigned char)*p1]) != 0)
					p1++;
			else {
				char *ep;
				wchar_t c, d;
				int length;
				ep = ipc->r1.re1;
				p3 = genbuf;

				length = mbtowc(&c, p1, MULTI_BYTE_MAX);
				while (length) {
					char multic[MULTI_BYTE_MAX];
					if (length == -1) {
						if (p3 >= gend) {
							growbuff(&gsize,
							    &genbuf,
							    &gend, &p3);
						}
						*p3++ = *p1++;
						length = mbtowc(&c, p1,
						    MULTI_BYTE_MAX);
						continue;
					}
					p1 += length;
					if (c <= 0377 && ep[c] != 0)
						d = (unsigned char)ep[c];
					else {
						p2 = ep + 0400;
						for (;;) {
							length = mbtowc(&d,
							    p2, MULTI_BYTE_MAX);
							p2 += length;
							if (length == 0 ||
							    d == c)
								break;
							p2 += mbtowc(&d, p2,
							    MULTI_BYTE_MAX);
						}
						if (length == 0)
							d = c;
						else
							(void) mbtowc(&d, p2,
							    MULTI_BYTE_MAX);
					}
					length = wctomb(multic, d);
					if (p3 + length > gend) {
						growbuff(&gsize, &genbuf,
						    &gend, &p3);
					}
					(void) strncpy(p3, multic, length);
					p3 += length;
					length = mbtowc(&c, p1, MULTI_BYTE_MAX);
				}
				if (p3 >= gend)
					growbuff(&gsize, &genbuf, &gend, &p3);
				*p3 = '\0';
				while (lsize < (p3 - genbuf + 1))
					growbuff(&lsize, &linebuf, &lbend,
					    (char **)0);
				p3 = genbuf;
				p1 = linebuf;
				while (*p1++ = *p3++)
					;
				spend = p1 - 1;
			}
			break;
	}

}

char *
gline(char *addr)
{
	char   *p1, *p2;
	int	c;
	p1 = addr;
	p2 = cbp;
	for (;;) {
		if (p2 >= ebp) {
			if ((c = read(f, ibuf, 512)) <= 0) {
				return (badp);
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
			if (p1 >= lbend)
				growbuff(&lsize, &linebuf, &lbend, &p1);
			*p1++ = c;
		}
	}
	lnum++;
	while (p1 >= lbend)
		growbuff(&lsize, &linebuf, &lbend, &p1);
	*p1 = 0;
	cbp = p2;

	sflag = 0;
	return (p1);
}

/*ARGSUSED*/
char *
comple(char **ep, char *x3, wchar_t x4)
{
	char *pcp, *p;
	wchar_t c;
	int length;
	int cclass = 0;

	pcp = cp;
	length = mbtowc(&c, pcp, MULTI_BYTE_MAX);
	while (length > 0) {
		if (c == x4 && !cclass)
			break;
		if (c == '\n')
			return (badp);
		if (cclass && c == ']') {
			cclass = 0;
			continue;
		}
		if (c == '[' && !cclass) {
			cclass = 1;
			pcp += length;
			if ((length = mbtowc(&c, pcp, MULTI_BYTE_MAX)) <= 0 ||
			    c == '\n')
				return (badp);
		}
		if (c == '\\' && !cclass) {
			pcp += length;
			if ((length = mbtowc(&c, pcp, MULTI_BYTE_MAX)) <= 0 ||
			    c == '\n')
				return (badp);
		}
		pcp += length;
		length = mbtowc(&c, pcp, MULTI_BYTE_MAX);
	}
	if (length <= 0)
		return (badp);
	c = (unsigned char)*pcp;
	*pcp = '\0';
	p = compile(cp, *ep, reend);
	while (regerrno == 50) {
		char *p2;
		int size;
		size = reend - *ep;
		if (*ep == respace)
			p2 = realloc(respace, size + RESIZE);
		else
			p2 = malloc(size + RESIZE);
		if (p2 == (char *)0) {
			(void) fprintf(stderr, gettext(TMMES), linebuf);
			exit(2);
		}
		respace = *ep = p2;
		reend = p2 + size + RESIZE - 1;
		p = compile(cp, *ep, reend);
	}
	*pcp = c;
	cp = pcp + length;
	if (regerrno && regerrno != 41)
		return (badp);
	if (regerrno == 41)
		return (*ep);
	return (p);
}

static void
arout(void)
{
	char   *p1;
	FILE	*fi;
	char	c;
	int	t;

	aptr = abuf - 1;
	while (*++aptr) {
		if ((*aptr)->r1.command == ACOM) {
			for (p1 = (*aptr)->r1.re1; *p1; )
				(void) putc(*p1++, stdout);
			(void) putc('\n', stdout);
		} else {
			if ((fi = fopen((*aptr)->r1.re1, "r")) == NULL)
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
 *      Dynamic memory allocation for buffer storage.
 *      lenp    - current and new length
 *      startp - current and new start of buffer
 *      endp - current and new end of buffer
 *      intop - pointer into current => new
 */
void
growbuff(unsigned int *lenp, char **startp, char **endp, char **intop)
{
	char *new;
	int delta = *lenp;

	if (!*lenp)
		*lenp = LBSIZE;
	else
		*lenp <<= 1;
	if (*startp)
		new = realloc(*startp, *lenp);
	else
		new = calloc(*lenp, sizeof (char));
	if (new == NULL) {
		(void) fprintf(stderr,
		    gettext("sed: Memory allocation failed.\n"));
		exit(2);
	}
	if (intop)
		*intop = new + (*intop - *startp);
	if (*lenp > LBSIZE)   /* zero-clear the new area of buffer */
		(void) memset(new+(*endp-*startp), '\0', *lenp-delta);
	*startp = new;
	*endp = new + *lenp;
}
