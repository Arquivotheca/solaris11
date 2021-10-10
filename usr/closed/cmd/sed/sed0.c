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
#include <sys/types.h>
#include <locale.h>
#include <stdarg.h>
#include <strings.h>
#include <errno.h>
#include "sed.h"
#include <limits.h>

FILE	*fin;
FILE    *fcode[12];
char    *lastre;
wchar_t   sseof;
char    *reend;
char    *hend;
char    *gend;
int	eflag;
char	*linebuf;
unsigned int lsize;
unsigned int gsize;
unsigned int hsize;
int	gflag;
int	nlno;
char    fname[12][PATH_MAX+1];
int	nfiles;
union reptr *ptrspace, *ptrend;
union reptr *rep;
char    *cp;
char    *respace;
struct label ltab[LABSIZE];
struct label    *lab;
struct label    *labend;
int	depth;
int	eargc;
char    **eargv;
union reptr	**cmpend[DEPTH];
char    *badp;
char    bad;

#define	CCEOF	22

struct label    *labtab = ltab;

const char    CGMES[]		= "sed: command garbled: %s\n";
const char    TMMES[]		= "Too much text: %s\n";
const char    LTL[]  		= "Label too long: %s\n";
const char    AD0MES[]	= "No addresses allowed: %s\n";
const char    AD1MES[]	= "Only one address allowed: %s\n";
const char	TOOBIG[]	= "Suffix too large - 512 max: %s\n";

extern char *comple();
extern int optind;
extern char *optarg;

static union reptr *allocrep();
static void fcomp(void);
static int rline(void);
static int cmp(char *, char *);
static void dechain(void);

int
main(int argc, char *argv[])
{
	int c;
	int FEoptions = 0;
	extern int err_status;

	badp = &bad;
	aptr = abuf;
	lab = labtab + 1;
	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)	/* Should be defined by cc -D */
#define	TEXT_DOMAIN "SYS_TEST"	/* Use this only if it weren't */
#endif
	(void) textdomain(TEXT_DOMAIN);
				/* initialize the buffers */
	growbuff(&lsize, &linebuf, &lbend, (char **)0);
	growbuff(&hsize, &holdsp, &hend, (char **)0);
	growbuff(&gsize, &genbuf, &gend, (char **)0);

	if ((respace = malloc(RESIZE)) == (char *)0) {
		(void) fprintf(stderr,
		    gettext("Cannot malloc space\n"));
		exit(2);
	}

	rep = allocrep();
	rep->r1.ad1 = respace;
	reend = &respace[RESIZE-1];
	labend = &labtab[LABSIZE];
	lnum = 0;
	pending = 0;
	depth = 0;
	spend = linebuf;
	hspend = holdsp;	/* Avoid "bus error" under "H" cmd. */
	fcode[0] = stdout;
	nfiles = 1;

	if (argc == 1)
		exit(0);


	while ((c = getopt(argc, argv, "gne:f:")) != -1)
		switch (c) {

		case 'n':
			nflag++;
			continue;

		case 'f':

			if ((fin = fopen(optarg, "r")) == NULL) {
				(void) fprintf(stderr,
				    gettext("Cannot open pattern-file: %s\n"),
				    optarg);
				exit(2);
			}

			FEoptions = 1;
			fcomp();
			(void) fclose(fin);
			continue;

		case 'e':
			eflag++;
			fcomp();
			eflag = 0;
			FEoptions = 1;
			continue;

		case 'g':
			gflag++;
			continue;

		default:
			exit(2);
		}


	eargv = argv + optind;
	eargc = argc - optind;
	if (rep == ptrspace && !FEoptions) {
		eflag++;
		optarg = *eargv++;
		if ((nflag || gflag) && (optarg == NULL))
			exit(0);
		fcomp();
		eargc--;
		eflag = 0;
	}

	if (depth) {
		(void) fprintf(stderr,
		    gettext("Too many {'s"));
		exit(2);
	}

	labtab->address = rep;

	dechain();

	if (eargc <= 0) {
		execute((char *)NULL);
	} else {
		while (--eargc >= 0) {
			execute(*eargv++);
		}
	}
	(void) fclose(stdout);
	return (err_status ? 1 : 0);
}

static void
fcomp(void)
{
	char   *p, *op, *tp;
	char    *address();
	char *wp;
	union reptr *pt, *pt1;
	int	i, ii;
	struct label    *lpt;
	int n;
	op = lastre;

	if (rline() < 0)
		return;
	if (*linebuf == '#') {
		if (linebuf[1] == 'n')
			nflag = 1;
	} else {
		cp = linebuf;
		goto comploop;
	}

	for (;;) {
		if (rline() < 0)
			break;

		cp = linebuf;

comploop:
		while (*cp == ' ' || *cp == '\t')
			cp++;
		if (*cp == '\0' || *cp == '#')
			continue;
		if (*cp == ';') {
			cp++;
			goto comploop;
		}

		p = address(&(rep->r1.ad1));
		if (p == badp) {
			(void) fprintf(stderr, gettext(CGMES), linebuf);
			exit(2);
		}

		if (p == rep->r1.ad1) {
			if (op)
				rep->r1.ad1 = op;
			else {
				(void) fprintf(stderr,
				    gettext("First RE may not be null\n"));
				exit(2);
			}
		} else if (p == 0) {
			p = rep->r1.ad1;
			rep->r1.ad1 = 0;
		} else {
			op = rep->r1.ad1;
			if (*cp == ',' || *cp == ';') {
				cp++;
				rep->r1.ad2 = p;
				p = address(&(rep->r1.ad2));
				if (p == badp || p == 0) {
					(void) fprintf(stderr,
					    gettext(CGMES), linebuf);
					exit(2);
				}
				if (p == rep->r1.ad2)
					rep->r1.ad2 = op;
				else
					op = rep->r1.ad2;

			} else
				rep->r1.ad2 = 0;
		}

		while (*cp == ' ' || *cp == '\t')
			cp++;

swit:
		switch (*cp++) {

			default:
				(void) fprintf(stderr,
				    gettext("Unrecognized command: %s\n"),
				    linebuf);
				exit(2);
				/*NOTREACHED*/

			case '!':
				rep->r1.negfl = 1;
				goto swit;

			case '{':
				rep->r1.command = BCOM;
				rep->r1.negfl = !(rep->r1.negfl);
				cmpend[depth++] = &rep->r2.lb1;
				rep = allocrep();
				rep->r1.ad1 = p;
				if (*cp == '\0')
					continue;

				goto comploop;

			case '}':
				if (rep->r1.ad1) {
					(void) fprintf(stderr,
					    gettext(AD0MES), linebuf);
					exit(2);
				}

				if (--depth < 0) {
					(void) fprintf(stderr,
					    gettext("Too many }'s\n"));
					exit(2);
				}
				*cmpend[depth] = rep;

				rep->r1.ad1 = p;
				continue;

			case '=':
				rep->r1.command = EQCOM;
				if (rep->r1.ad2) {
					(void) fprintf(stderr,
					    gettext(AD1MES), linebuf);
					exit(2);
				}
				break;

			case ':':
				if (rep->r1.ad1) {
					(void) fprintf(stderr,
					    gettext(AD0MES), linebuf);
					exit(2);
				}

				while (*cp++ == ' ')
					;
				cp--;


				tp = lab->asc;
				while ((*tp++ = *cp++))
					if (tp >= &(lab->asc[9])) {
						(void) fprintf(stderr,
						    gettext(LTL), linebuf);
						exit(2);
					}
				*--tp = '\0';

				if (lpt = search(lab)) {
					if (lpt->address) {
						(void) fprintf(stderr,
				    gettext("Duplicate labels: %s\n"), linebuf);
						exit(2);
					}
				} else {
					lab->chain = 0;
					lpt = lab;
					if (++lab >= labend) {
						(void) fprintf(stderr,
				    gettext("Too many labels: %s\n"), linebuf);
						exit(2);
					}
				}
				lpt->address = rep;
				rep->r1.ad1 = p;

				continue;

			case 'a':
				rep->r1.command = ACOM;
				if (rep->r1.ad2) {
					(void) fprintf(stderr,
					    gettext(AD1MES), linebuf);
					exit(2);
				}
				if (*cp == '\\')
					cp++;
				if (*cp++ != '\n') {
					(void) fprintf(stderr,
					    gettext(CGMES), linebuf);
					exit(2);
				}
				rep->r1.re1 = p;
				p = text(&(rep->r1.re1), reend);
				break;
			case 'c':
				rep->r1.command = CCOM;
				if (*cp == '\\')
					cp++;
				if (*cp++ != ('\n')) {
					(void) fprintf(stderr,
					    gettext(CGMES), linebuf);
					exit(2);
				}
				rep->r1.re1 = p;
				p = text(&(rep->r1.re1), reend);
				break;
			case 'i':
				rep->r1.command = ICOM;
				if (rep->r1.ad2) {
					(void) fprintf(stderr,
					    gettext(AD1MES), linebuf);
					exit(2);
				}
				if (*cp == '\\')
					cp++;
				if (*cp++ != ('\n')) {
					(void) fprintf(stderr,
					    gettext(CGMES), linebuf);
					exit(2);
				}
				rep->r1.re1 = p;
				p = text(&(rep->r1.re1), reend);
				break;

			case 'g':
				rep->r1.command = GCOM;
				break;

			case 'G':
				rep->r1.command = CGCOM;
				break;

			case 'h':
				rep->r1.command = HCOM;
				break;

			case 'H':
				rep->r1.command = CHCOM;
				break;

			case 't':
				rep->r1.command = TCOM;
				goto jtcommon;

			case 'b':
				rep->r1.command = BCOM;
jtcommon:
				while (*cp++ == ' ')
					;
				cp--;

				if (*cp == '\0') {
					if ((pt = labtab->chain) != 0) {
						while ((pt1 = pt->r2.lb1) != 0)
							pt = pt1;
						pt->r2.lb1 = rep;
					} else
						labtab->chain = rep;
					break;
				}
				tp = lab->asc;
				while ((*tp++ = *cp++))
					if (tp >= &(lab->asc[9])) {
						(void) fprintf(stderr,
						    gettext(LTL), linebuf);
						exit(2);
					}
				cp--;
				*--tp = '\0';

				if (lpt = search(lab)) {
					if (lpt->address) {
						rep->r2.lb1 = lpt->address;
					} else {
						pt = lpt->chain;
						while ((pt1 = pt->r2.lb1) != 0)
							pt = pt1;
						pt->r2.lb1 = rep;
					}
				} else {
					lab->chain = rep;
					lab->address = 0;
					if (++lab >= labend) {
						(void) fprintf(stderr,
				    gettext("Too many labels: %s\n"), linebuf);
						exit(2);
					}
				}
				break;

			case 'n':
				rep->r1.command = NCOM;
				break;

			case 'N':
				rep->r1.command = CNCOM;
				break;

			case 'p':
				rep->r1.command = PCOM;
				break;

			case 'P':
				rep->r1.command = CPCOM;
				break;

			case 'r':
				rep->r1.command = RCOM;
				if (rep->r1.ad2) {
					(void) fprintf(stderr,
					    gettext(AD1MES), linebuf);
					exit(2);
				}
				if (*cp++ != ' ') {
					(void) fprintf(stderr,
					    gettext(CGMES), linebuf);
					exit(2);
				}
				rep->r1.re1 = p;
				p = text(&(rep->r1.re1), reend);
				break;

			case 'd':
				rep->r1.command = DCOM;
				break;

			case 'D':
				rep->r1.command = CDCOM;
				rep->r2.lb1 = ptrspace;
				break;

			case 'q':
				rep->r1.command = QCOM;
				if (rep->r1.ad2) {
					(void) fprintf(stderr,
					    gettext(AD1MES), linebuf);
					exit(2);
				}
				break;

			case 'l':
				rep->r1.command = LCOM;
				break;

			case 's':
				rep->r1.command = SCOM;
				if ((n = mbtowc(&sseof,
				    cp, MULTI_BYTE_MAX)) < 0) {
					(void) fprintf(stderr,
					    gettext(CGMES), linebuf);
					exit(2);
				}
				if (n == 0) {
					/* re-use the same rep */
					goto done2;
				}
				cp += n;
				rep->r1.re1 = p;
				p = comple(&(rep->r1.re1), reend, sseof);
				if (p == badp) {
					(void) fprintf(stderr,
					    gettext(CGMES), linebuf);
					exit(2);
				}
				if (p == rep->r1.re1) {
					if (op)
						rep->r1.re1 = op;
					else {
						(void) fprintf(stderr, gettext(
						"First RE may not be null\n"));
						exit(2);
					}
				} else
					op = rep->r1.re1;
				rep->r1.rhs = p;

				if ((p = compsub(&(rep->r1.rhs))) == badp) {
					(void) fprintf(stderr,
					    gettext(CGMES), linebuf);
					exit(2);
				}

				if (*cp == 'g') {
					cp++;
					rep->r1.gfl = 999;
				} else if (gflag)
					rep->r1.gfl = 999;

				if (*cp >= '1' && *cp <= '9') {
					i = *cp - '0';
					cp++;
					for (;;) {
						ii = *cp;
						if (ii < '0' || ii > '9')
							break;
						i = i*10 + ii - '0';
						if (i > 512) {
							(void) fprintf(stderr,
							    gettext(TOOBIG),
							    linebuf);
							exit(2);
						}
						cp++;
					}
					rep->r1.gfl = i;
				}

				if (*cp == 'p') {
					cp++;
					rep->r1.pfl = 1;
				}

				if (*cp == 'P') {
					cp++;
					rep->r1.pfl = 2;
				}

				if (*cp == 'w') {
					cp++;
					if (*cp++ !=  ' ') {
						(void) fprintf(stderr,
						    gettext(CGMES), linebuf);
						exit(2);
					}
					if (nfiles >= 10) {
						(void) fprintf(stderr,
				    gettext("Too many files in w commands\n"));
						exit(2);
					}

					wp = &fname[nfiles][0];
					(void) text(&wp,
					    &fname[nfiles][PATH_MAX]);
					for (i = nfiles - 1; i >= 0; i--)
						if (cmp(fname[nfiles],
						    fname[i]) == 0) {
							rep->r1.fcode =
							    fcode[i];
							goto done;
						}
					if ((rep->r1.fcode = fopen(
					    fname[nfiles], "w")) == NULL) {
						(void) fprintf(stderr,
						    gettext("cannot open %s\n"),
						    fname[nfiles]);
						exit(2);
					}
					fcode[nfiles++] = rep->r1.fcode;
				}
				break;

			case 'w':
				rep->r1.command = WCOM;
				if (*cp++ != ' ') {
					(void) fprintf(stderr,
					    gettext(CGMES), linebuf);
					exit(2);
				}
				if (nfiles >= 10) {
					(void) fprintf(stderr,
					    gettext(
					    "Too many files in w commands\n"));
					exit(2);
				}

				wp = &fname[nfiles][0];
				(void) text(&wp, &fname[nfiles][PATH_MAX]);
				for (i = nfiles - 1; i >= 0; i--)
					if (cmp(fname[nfiles], fname[i]) == 0) {
						rep->r1.fcode = fcode[i];
						goto done;
					}

				if ((rep->r1.fcode = fopen(fname[nfiles],
				    "w")) == NULL) {
					(void) fprintf(stderr,
					    gettext("Cannot create %s\n"),
					    fname[nfiles]);
					exit(2);
				}
				fcode[nfiles++] = rep->r1.fcode;
				break;

			case 'x':
				rep->r1.command = XCOM;
				break;

			case 'y':
				rep->r1.command = YCOM;
				if ((n = mbtowc(&sseof,
				    cp, MULTI_BYTE_MAX)) < 0) {
					(void) fprintf(stderr,
					    gettext(CGMES), linebuf);
					exit(2);
				}
				if (n == 0) {
					/* re-use the same rep */
					goto done2;
				}
				cp += n;
				rep->r1.re1 = p;
				p = ycomp(&(rep->r1.re1));
				if (p == badp) {
					(void) fprintf(stderr,
					    gettext(CGMES), linebuf);
					exit(2);
				}
				break;

		}
done:
		rep = allocrep();

done2:
		rep->r1.ad1 = p;

		if (*cp++ != '\0') {
			if (cp[-1] == ';')
				goto comploop;
			(void) fprintf(stderr, gettext(CGMES), linebuf);
			exit(2);
		}

	}
	rep->r1.command = 0;
	lastre = op;
}

char *
compsub(char **rhsbuf)
{
	char   *p, *q;
	int length, size;

	p = *rhsbuf;
	q = cp;
	for (;;) {
		wchar_t nextc;
		if (p >= reend - MULTI_BYTE_MAX - 1) {
			size = p - *rhsbuf;
			if (*rhsbuf == respace)
				p = realloc(respace, size + RESIZE);
			else
				p = malloc(size + RESIZE);
			if (p == NULL) {
				(void) fprintf(stderr, gettext(TMMES), linebuf);
				exit(2);
			}
			if (*rhsbuf != respace)
				(void) strncpy(p, *rhsbuf, size);
			respace = *rhsbuf = p;
			reend = p + size + RESIZE - 1;
			p += size;
		}
		if ((length = mbtowc(&nextc, q, MULTI_BYTE_MAX)) <= 0)
			return (badp);
		if (nextc == '\\') {
			q++;
			*p++ = '\\';
			if ((length = mbtowc(&nextc, q, MULTI_BYTE_MAX)) <= 0)
				return (badp);
			if (nextc > nbra + '0' && nextc <= '9')
				return (badp);
			(void) strncpy(p, q, length);
			p += length;
			q += length;
			continue;
		}
		if (nextc == sseof) {
			*p++ = '\0';
			q += length;
			cp = q;
			return (p);
		}
		(void) strncpy(p, q, length);
		p += length;
		q += length;
	}
}

/* rline reads one line into linebuf. */

static int
rline(void)
{
	char   *p, *q;
	int	length;
	int	t;
	static char	*saveq;

	p = linebuf - 1;

	if (eflag) {
		if (eflag > 0) {
			eflag = -1;
			if ((q = optarg) != NULL)
				length = strlen(q);
			else
				exit(2);

			while (lsize <= length)
				growbuff(&lsize, &linebuf, &lbend, (char **)0);
			p = linebuf - 1;

			while (*++p = *q++) {
				if (*p == '\\') {
					if ((*++p = *q++) == '\0') {
						saveq = 0;
						return (-1);
					} else
						continue;
				}
				if (*p == '\n') {
					*p = '\0';
					saveq = q;
					return (1);
				}
			}
			saveq = 0;
			return (1);
		}
		if ((q = saveq) == 0)
			return (-1);

		while (*++p = *q++) {
			if (*p == '\\') {
				if ((*++p = *q++) == '\0') {
					saveq = 0;
					return (-1);
				} else
					continue;
			}
			if (*p == '\n') {
				*p = '\0';
				saveq = q;
				return (1);
			}
		}
		saveq = 0;
		return (1);
	}

	while ((t = getc(fin)) != EOF) {
		if (p+1 >= lbend)
			growbuff(&lsize, &linebuf, &lbend, &p);
		*++p = t;
		if (*p == '\\') {
			if ((t = getc(fin)) == EOF)
				return (-1);
			if (p+1 >= lbend)
				growbuff(&lsize, &linebuf, &lbend, &p);
			*++p = t;
		} else if (*p == '\n') {
			*p = '\0';
			return (1);
		}
	}
	return (-1);
}

char *
address(char **expbuf)
{
	char   *rcp, *p;
	int length;
	long long lno;

	if (*cp == '/' || *cp == '\\') {
		if (*cp == '\\')
			cp++;
		if ((length = mbtowc(&sseof, cp, MULTI_BYTE_MAX)) <= 0) {
			(void) fprintf(stderr, gettext(CGMES));
			exit(2);
		}
		cp += length;
		return (comple(expbuf, reend, sseof));
	}
	p = *expbuf;
	if (*expbuf + 5 > reend) {
		if ((p = malloc(RESIZE)) == (char *)0) {
			(void) fprintf(stderr, gettext(TMMES), linebuf);
			exit(2);
		}
		respace = *expbuf = p;
		reend = respace + RESIZE - 1;
	}
	if (*cp == '$') {
		cp++;
		*p++ = CEND;
		*p++ = CCEOF;
		return (p);
	}

	rcp = cp;
	lno = 0;

	while (*rcp >= '0' && *rcp <= '9')
		lno = lno*10 + *rcp++ - '0';

	if (rcp > cp) {
		*p++ = CLNUM;
		*p++ = nlno;
		tlno[nlno++] = lno;
		if (nlno >= NLINES) {
			(void) fprintf(stderr,
			    gettext("Too many line numbers\n"));
			exit(2);
		}
		*p++ = CCEOF;
		cp = rcp;
		return (p);
	}
	return (0);
}
static int
cmp(char *a, char *b)
{
	char   *ra, *rb;

	ra = a - 1;
	rb = b - 1;

	while (*++ra == *++rb)
		if (*ra == '\0')
			return (0);
	return (1);
}

char *
text(char **textbuf, char *endbuf)
{
	char   *p, *q;

	p = *textbuf;
	q = cp;
	for (;;) {

		if (p >= endbuf) {
			int size;
			char *p2;
			size = p - *textbuf;
			if (endbuf != reend) {
				(void) fprintf(stderr, gettext(TMMES), linebuf);
				exit(2);
			}
			if (*textbuf == respace)
				p2 = realloc(respace, size + RESIZE);
			else
				p2 = malloc(size + RESIZE);
			if (p2 == (char *)0) {
				(void) fprintf(stderr, gettext(TMMES), linebuf);
				exit(2);
			}
			if (*textbuf != respace)
				(void) strncpy(p2, *textbuf, size);
			p = p2 + size;
			endbuf = reend = p2 + size + RESIZE - 1;
			*textbuf = respace = p2;
		}
		if ((*p = *q++) == '\\')
			*p = *q++;
		if (*p == '\0') {
			cp = --q;
			return (++p);
		}
		p++;
	}
}


struct label *
search(struct label *ptr)
{
	struct label    *rp;

	rp = labtab;
	while (rp < ptr) {
		if (cmp(rp->asc, ptr->asc) == 0)
			return (rp);
		rp++;
	}

	return (0);
}

static void
dechain(void)
{
	struct label    *lptr;
	union reptr	*rptr, *trptr;

	for (lptr = labtab; lptr < lab; lptr++) {

		if (lptr->address == NULL) {
			(void) fprintf(stderr,
			    gettext("Undefined label: %s\n"), lptr->asc);
			exit(2);
		}

		if (lptr->chain) {
			rptr = lptr->chain;
			while ((trptr = rptr->r2.lb1) != 0) {
				rptr->r2.lb1 = lptr->address;
				rptr = trptr;
			}
			rptr->r2.lb1 = lptr->address;
		}
	}
}

char *
ycomp(char **expbuf)
{
	wchar_t   c, d;
	int length, size;
	char *ep, *tsp;
	char    *sp, *endptr, *p;

	ep = *expbuf;
	if (ep + 0400 > reend) {
		size = reend - *expbuf;
		if (*expbuf == respace)
			p = realloc(respace, RESIZE + size);
		else
			p = malloc(size + RESIZE);
		if (p == (char *)0) {
			(void) fprintf(stderr, gettext(TMMES), linebuf);
			exit(2);
		}
		ep = *expbuf = respace = p;
		reend = p + size + RESIZE - 1;
	}
	endptr = ep + 0400;

	for (sp = ep; sp < endptr; sp++)
		*sp = sp - ep;
	sp = cp;
	tsp = cp;
	for (;;) {
		if ((length = mbtowc(&c, tsp, MULTI_BYTE_MAX)) <= 0)
			return (badp);
		tsp += length;
		if (c == sseof)
			break;
		if (c == '\\') {
			if ((length = mbtowc(&c, tsp, MULTI_BYTE_MAX)) <= 0)
				return (badp);
			tsp += length;
		}
		if (c == '\n')
			return (badp);
	}

	for (;;) {
		int length2;
		length = mbtowc(&c, sp, MULTI_BYTE_MAX);
		sp += length;
		if (c == sseof)
			break;
		if (c == '\\') {
			length = mbtowc(&c, sp, MULTI_BYTE_MAX);
			sp += length;
			if (c == 'n')
				c = '\n';
		}
		if ((length2 = mbtowc(&d, tsp, MULTI_BYTE_MAX)) <= 0)
			return (badp);
		tsp += length2;

		if (d == '\\') {
			if ((length2 = mbtowc(&d, tsp, MULTI_BYTE_MAX)) <= 0)
				return (badp);
			tsp += length2;
			if (d == 'n')
				d = '\n';
		}
		if (d > 0377 || c > 0377) {
			if (endptr + length + length2 + 1 > reend) {
				size = endptr - *expbuf;
				if (*expbuf == respace)
					p = realloc(respace, RESIZE + size);
				else
					p = malloc(size + RESIZE);
				if (p == (char *)0) {
					(void) fprintf(stderr,
					    gettext(TMMES), linebuf);
					exit(2);
				}
				if (*expbuf != respace)
					(void) memcpy(p, *expbuf, size);
				ep = *expbuf = respace = p;
				reend = p + size + RESIZE - 1;
				endptr = p + size;
			}
			if (c < 0400)
				ep[c] = 0;
			endptr += wctomb(endptr, c);
			endptr += wctomb(endptr, d);
		}
		else
			ep[c] = d;
	}
	if ((length = mbtowc(&d, tsp, MULTI_BYTE_MAX)) <= 0 || d != sseof)
		return (badp);
	cp = tsp + length;
	*endptr++ = '\0';
	return (endptr);
}

static union reptr *
allocrep()
{
	union reptr *tmp;

	tmp = calloc(1, sizeof (union reptr));
	if (tmp == NULL) {
		(void) fprintf(stderr, gettext("Cannot malloc space\n"));
		exit(2);
	}
	if (ptrspace == NULL)
		ptrspace = tmp;
	else
		ptrend->r1.next = tmp;
	ptrend = tmp;
	return (tmp);
}
