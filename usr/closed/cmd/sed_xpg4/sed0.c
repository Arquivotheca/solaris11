/*
 * Copyright (c) 1995, 2011, Oracle and/or its affiliates. All rights reserved.
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

/*
 * COMPONENT_NAME: sed0.c
 *
 * Based on OSF sed(1) command with POSIX/XCU4 Spec:1170 changes.
 *
 * FUNCTIONS: main, fcomp, comploop, compsub, rline, address, cmp,
 * text, search, dechain, ycomp, comple, getre and growspace.
 *
 */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <locale.h>
#include <stdarg.h>
#include <errno.h>
#include "sed.h"

#define	CCEOF   22	/* end marker to indicate match to end of line */
#define	MAX_FILES 11

static int	our_errno;
static FILE	*fin;
static struct addr *lastre;
static wchar_t    sseof;
static char    *reend = NULL;

static char  *rhsend = NULL;
static char  *rhsp = NULL;

static int	eflag;
static int	gflag;

/* linebuf start, end and size */
char    *linebuf = NULL;
char	*lbend = NULL;
unsigned int	lsize;

/* holdsp start, end and size */
char	*holdsp = NULL;
char	*hend = NULL;
unsigned int	hsize;
char	*hspend = NULL;	/* end of active hold */

/* genbuf start, end and size */
char	*genbuf = NULL;
char	*gend = NULL;
unsigned int	gsize;

/* pattern start, end and size */
/* Used to hold regular expression pattern string before call to regcomp */
static char	*pattern = NULL;
static char	*pend = NULL;
static unsigned int	psize;

static int	nbra_subexp;
static int	nlno;
static FILE	*fcode[MAX_FILES];
static char	fname[MAX_FILES][PATH_MAX];
static int	nfiles;

/*
 *	cmdspace is an array of data structures that stores info. of sed
 *	commands read with all "-e", "-f" option argument: stores address,
 *	data, type of command, regular expressions and any new/inserted text,
 *	as well as any flags that are necessary for the processing
 *	of these commands.
 */
union reptr	cmdspace[PTRSIZE];
static	union reptr	*cmdend;
static union reptr	*rep;
static char	*cp = NULL;
static char	*sp = NULL;
static struct label	ltab[LABSIZE];
static struct label	*lab;
static struct label	*labend;
static int	depth;
int	eargc;
static union reptr	**cmpend[DEPTH];
char	*badp;
static char	bad;

static struct label	*labtab = ltab;

int	exit_value = 0;

/*
 *	Define default messages.
 */
#ifdef __STDC__
char    CGMES[]    = "sed: command garbled: %s\n";
static const char	LTL[]	= "sed: Label too long: %s\n";
static const char    AD0MES[]	= "sed: No addresses allowed: %s\n";
static const char    AD1MES[]	= "sed: Only one address allowed: %s\n";
#else
char    CGMES[]	= "sed: command garbled: %s\n";
static char    LTL[]	= "sed: Label too long: %s\n";
static char    AD0MES[]	= "sed: No addresses allowed: %s\n";
static char    AD1MES[]	= "sed: Only one address allowed: %s\n";
#endif

/*
 *	Function prototypes.
 */
static void fcomp(char *);
static void dechain(void);
static int rline(char *, char *, int *);
static struct addr *address(void);
static struct label *search(struct label *);
static char *text(char *, char *);
static struct addr *comple(wchar_t);
static int compsub(char **);
static int cmp(char *, char *);
static wchar_t	*ycomp(void);
static int getre(wchar_t);
static char *growspace(char *, char **);

int
main(int argc, char **argv)
{
	int	compflag = 0;
	int 	ch;

	(void) setlocale(LC_ALL, "");	/* required by NLS environment tests */

#if !defined(TEXT_DOMAIN)	/* Should be defined by cc -D */
#define	TEXT_DOMAIN "SYS_TEST"  /* Use this only if it weren't */
#endif
	(void) textdomain(TEXT_DOMAIN);

	nlno = 0;
	badp = &bad;
	aptr = abuf;
	lab = labtab + 1;	/* 0 reserved for end-pointer */
	rep = cmdspace;
	/* Dynamic memory allocation for buffer storage */
	sp = growspace((char *)0, &reend);
	growbuff(&lsize, &linebuf, &lbend, (char **)0);
	growbuff(&hsize, &holdsp, &hend, (char **)0);
	growbuff(&gsize, &genbuf, &gend, (char **)0);

	cmdend = &cmdspace[PTRSIZE];
	labend = &labtab[LABSIZE];

	rhsp = growspace((char *)0, &rhsend);

	lastre = 0;
	lnum = 0;
	pending = 0;
	depth = 0;
	spend = linebuf;
	hspend = holdsp;
	fcode[0] = stdout;
	nfiles = 1;

	if (argc == 1)
		exit(0);

	while ((ch = getopt(argc, argv, "e:f:n")) != -1) {
		switch (ch) {

		case 'n':
			nflag++;
			continue;

		case 'f':
			if ((fin = fopen(optarg, "r")) == NULL) {
				(void) fprintf(stderr,
				    gettext("sed: Cannot open pattern-file: "
				    "%s\n"),
				    optarg);
				exit(2);
			}

			fcomp(NULL);
			compflag++;
			(void) fclose(fin);
			continue;

		case 'e':
			eflag++;
			fcomp(optarg);
			compflag++;
			eflag = 0;
			continue;

		case '?':
			(void) fprintf(stderr, gettext(
			    "Usage:	sed [-n] script [file...]\n"
			    "	sed [-n] [-e script]...[-f script_file]..."
			    "[file...]\n"));
			exit(1);
		}
	}

	argc -= optind;
	argv += optind;

	if ((argc > 0) && (!compflag) && (rep == cmdspace)) {
		eflag++;
		fcomp(*argv);
		argv++;
		argc--;
		eflag = 0;
	}

	if (depth) {
		(void) fprintf(stderr,
		    gettext("sed: Too many {'s"));
		exit(2);
	}

	labtab->address = rep;

	dechain();

	eargc = argc;
	if (eargc <= 0)
		execute((char *)NULL);
	else while (--eargc >= 0) {
		execute(*argv++);
	}
	(void) fclose(stdout);
	return (exit_value);
}

/*
 *	Read sed commands into reptr structure cmdspace.
 *	cmdspace stores address data, type of command,
 *	regular expressions and any new/inserted text, as well
 *	as any flags that are necessary for the processing
 *	of these commands.
 *
 * 	The source argument points to either the expression
 * 	we want to compile, or NULL if we read it from a file.
 * 	This function uses rline.
 */
static void
fcomp(char *source)
{
	char   *tp;
	struct addr 	*op;
	int last_bad = 0;

	union reptr	*pt;
	int	i;
	struct label    *lpt;

	int	n;

	op = lastre;

	if (rline(linebuf, source, &last_bad) < 0)
		return;
	if (last_bad) {
		(void) fprintf(stderr,
		    gettext("sed: "
		    "Found escape character at end of editing script.\n"));
		exit(2);
	}
	if (*linebuf == '#') {
		/* if "#n" on first line, same effect as */
		/* using -n flag from command line */
		if (linebuf[1] == 'n')
			nflag = 1;
	} else {
		cp = linebuf;
		goto comploop;
	}

	for (;;) {
		if (rline(linebuf, source, &last_bad) < 0)
			break;
		if (last_bad) {
			(void) fprintf(stderr, gettext("sed: Found escape "
			    "character at end of editing script.\n"));
			exit(2);
		}
		if (*linebuf == '#')	/* skip comments anywhere! */
			continue;

		cp = linebuf;
comploop:
		while (*cp == ' ' || *cp == '\t')	/* skip white space */
			cp++;
		if (*cp == '\0')
			continue;
		if (*cp == ';') {
			cp++;
			goto comploop;
		}

		for (;;) {
			rep->r1.ad1 = address();
			if (our_errno != MORESPACE)
				break;
			sp = growspace(sp, &reend);
		}
		if (our_errno == BADCMD) {
			(void) fprintf(stderr, gettext(CGMES), linebuf);
			exit(2);
		}

		if (our_errno == REEMPTY) {
			if (op)
				rep->r1.ad1 = op;
			else {
				(void) fprintf(stderr,
				    gettext("sed: "
				    "The first RE may not be null.\n"));
				exit(2);
			}
		} else if (our_errno == NOADDR) {
			rep->r1.ad1 = 0;
		} else {
			op = rep->r1.ad1;
			if (*cp == ',' || *cp == ';') {
				cp++;
				for (;;) {
					rep->r1.ad2 = address();
					if (our_errno != MORESPACE)
						break;
					sp = growspace(sp, &reend);
				}
				if (our_errno == BADCMD ||
				    our_errno == NOADDR) {
					(void) fprintf(stderr, gettext(CGMES),
					    linebuf);
					exit(2);
				}
				if (our_errno == REEMPTY) {
					rep->r1.ad2 = op;
					(void) fprintf(stderr,
					    gettext("sed: "
					    "The first RE may not be null.\n"));
					exit(2);
				}
			} else
				rep->r1.ad2 = 0;
		}

		while (sp >= reend)
			sp = growspace(sp, &reend);

		while (*cp == ' ' || *cp == '\t')	/* skip white space */
			cp++;
swit:
		switch (*cp++) {

		default:
			(void) fprintf(stderr,
			    gettext("sed: %s is an unrecognized command.\n"),
			    linebuf);
			exit(2);
			/* FALLTHROUGH not really */

		case '!':
			rep->r1.negfl = 1;
			goto swit;

		case '{':
			rep->r1.command = BCOM;
			rep->r1.negfl = !(rep->r1.negfl);
			cmpend[depth++] = &rep->r2.lb1;
			if (++rep >= cmdend) {
				(void) fprintf(stderr,
				    gettext("sed: Too many commands: %s\n"),
				    linebuf);
				exit(2);
			}
			if (*cp == '\0') continue;

			goto comploop;

		case '}':
			if (rep->r1.ad1) {
				(void) fprintf(stderr,
				    gettext(AD0MES), linebuf);
				exit(2);
			}

			if (--depth < 0) {
				(void) fprintf(stderr,
				    gettext("sed: Too many }'s.\n"));
				exit(2);
			}
			*cmpend[depth] = rep;

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

			while (*cp == ' ' || *cp == '\t')
				cp++;


			tp = lab->asc;
			while ((*tp++ = *cp++))

				/* fix bug: code errors if label length = 8 */

				if (tp > &(lab->asc[8])) {
					(void) fprintf(stderr,
					    gettext(LTL), linebuf);
					exit(2);
				}

			if (lpt = search(lab)) {
				if (lpt->address) {
					(void) fprintf(stderr,
					    gettext("sed: Duplicate labels: "
					    "%s\n"), linebuf);
					exit(2);
				}
			} else {
				lab->chain = 0;
				lpt = lab;
				if (++lab >= labend) {
					(void) fprintf(stderr,
					    gettext("sed: "
					    "Too many labels: %s\n"), linebuf);
					exit(2);
				}
			}
			lpt->address = rep;

			continue;

		case 'a':
			rep->r1.command = ACOM;
			if (rep->r1.ad2) {
				(void) fprintf(stderr,
				    gettext(AD1MES), linebuf);
				exit(2);
			}
			if (*cp == '\\') cp++;
			if (*cp++ != '\n') {
				(void) fprintf(stderr,
				    gettext(CGMES), linebuf);
				exit(2);
			}
			rep->r1.rhs = sp;
			while ((sp = text(rep->r1.rhs, reend)) == 0) {
				sp = growspace(rep->r1.rhs, &reend);
				rep->r1.rhs = sp;
			}
			break;
		case 'c':
			rep->r1.command = CCOM;
			if (*cp == '\\') cp++;
			if (*cp++ != ('\n')) {
				(void) fprintf(stderr,
				    gettext(CGMES), linebuf);
				exit(2);
			}
			rep->r1.rhs = sp;
			while ((sp = text(rep->r1.rhs, reend)) == 0) {
				sp = growspace(rep->r1.rhs, &reend);
				rep->r1.rhs = sp;
			}
			break;
		case 'i':
			rep->r1.command = ICOM;
			if (rep->r1.ad2) {
				(void) fprintf(stderr,
				    gettext(AD1MES), linebuf);
				exit(2);
			}
			if (*cp == '\\') cp++;
			if (*cp++ != ('\n')) {
				(void) fprintf(stderr,
				    gettext(CGMES), linebuf);
				exit(2);
			}
			rep->r1.rhs = sp;
			while ((sp = text(rep->r1.rhs, reend)) == 0) {
				sp = growspace(rep->r1.rhs, &reend);
				rep->r1.rhs = sp;
			}
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
			while (*cp == ' ' || *cp == '\t')
				cp++;

			if (*cp == '\0') {
				if ((pt = labtab->chain) != NULL) {
					while (pt->r2.lb1)
						pt = pt->r2.lb1;
					pt->r2.lb1 = rep;
				} else
					labtab->chain = rep;
				break;
			}
			tp = lab->asc;
			while ((*tp++ = *cp++))

				/* fix bug: code errors if label length = 8 */

				if (tp > &(lab->asc[8])) {
					(void) fprintf(stderr,
					    gettext(LTL), linebuf);
					exit(2);
				}
			cp--;

			if (lpt = search(lab)) {
				if (lpt->address) {
					rep->r2.lb1 = lpt->address;
				} else {
					pt = lpt->chain;
					while (pt->r2.lb1)
						pt = pt->r2.lb1;
					pt->r2.lb1 = rep;
				}
			} else {
				lab->chain = rep;
				lab->address = 0;
				if (++lab >= labend) {
					(void) fprintf(stderr,
					    gettext("sed: "
					    "Too many labels: %s\n"), linebuf);
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
			rep->r1.rhs = sp;
			while ((sp = text(rep->r1.rhs, reend)) == 0) {
				sp = growspace(rep->r1.rhs, &reend);
				rep->r1.rhs = sp;
			}
			break;

		case 'd':
			rep->r1.command = DCOM;
			break;

		case 'D':
			rep->r1.command = CDCOM;
			rep->r2.lb1 = cmdspace;
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
			if ((n = mbtowc(&sseof, cp, MB_CUR_MAX)) <= 0) {
				(void) fprintf(stderr,
				    gettext(CGMES), linebuf);
				exit(2);
			}
			cp += n;
			rep->r1.re1 = comple(sseof);
			if (our_errno == BADCMD) {
				(void) fprintf(stderr,
				    gettext(CGMES), linebuf);
				exit(2);
			}
			if (our_errno == REEMPTY)
				rep->r1.re1 = op;
			else
				op = rep->r1.re1;

			rep->r1.rhs = rhsp;
			while (our_errno = compsub(&(rep->r1.rhs))) {
				if (our_errno == MORESPACE) {
					rhsp = growspace(rhsp, &rhsend);
					rep->r1.rhs = rhsp;
					continue;
				}
				(void) fprintf(stderr,
				    gettext(CGMES), linebuf);
				exit(2);
			}
			if (gflag)
				rep->r1.gfl = GLOBAL_SUB;
			else
				rep->r1.gfl = 1;
			while (strspn(cp, "gpPw0123456789")) {
				if (*cp == 'g')
					rep->r1.gfl = GLOBAL_SUB;
				else if (*cp == 'p')
					rep->r1.pfl = 1;
				else if (*cp == 'P')
					rep->r1.pfl = 2;
				else if (*cp == 'w') {
					cp++;
					if (*cp++ !=  ' ') {
						(void) fprintf(stderr,
						    gettext(CGMES), linebuf);
						exit(2);
					}
					if (nfiles >= MAX_FILES) {
						(void) fprintf(stderr,
						    gettext("sed: "
						    "The w function allows a "
						    "maximum of ten files.\n"));
						exit(2);
					}

					(void) text(fname[nfiles],
					    (char *)0);
					for (i = nfiles - 1; i >= 0; i--)
						if (cmp(fname[nfiles], fname[i])
						    == 0) {
							rep->r1.fcode =
							    fcode[i];
							goto done;
						}
					if ((rep->r1.fcode =
					    fopen(fname[nfiles], "w")) ==
					    NULL) {
						(void) fprintf(stderr,
						    gettext("sed: "
						    "cannot open %s\n"),
						    fname[nfiles]);
						exit(2);
					}
					fcode[nfiles++] = rep->r1.fcode;
					break;
				} else {
					rep->r1.gfl =
					    (short)strtol(cp, &tp, 10);
					if (rep->r1.gfl == 0) {
						(void) fprintf(stderr,
						    gettext(CGMES),
						    linebuf);
						exit(2);
					}
					cp = --tp;
				}
				cp++;
			}
			break;

		case 'w':
			rep->r1.command = WCOM;
			if (*cp++ != ' ') {
				(void) fprintf(stderr,
				    gettext(CGMES), linebuf);
				exit(2);
			}
			if (nfiles >= MAX_FILES) {
				(void) fprintf(stderr,
				    gettext("sed: The w function allows a "
				    "maximum of ten files.\n"));
				exit(2);
			}

			(void) text(fname[nfiles], (char *)0);
			for (i = nfiles - 1; i >= 0; i--)
				if (cmp(fname[nfiles], fname[i]) == 0) {
					rep->r1.fcode = fcode[i];
					goto done;
				}

			if ((rep->r1.fcode =
			    fopen(fname[nfiles], "w")) == NULL) {
				(void) fprintf(stderr,
				    gettext("sed: Cannot create file %s.\n"),
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
			if ((n = mbtowc(&sseof, cp, MB_CUR_MAX)) <= 0) {
				(void) fprintf(stderr,
				    gettext(CGMES), linebuf);
				exit(2);
			}
			cp += n;

			rep->r1.ytxt = ycomp();
			if (rep->r1.ytxt == 0) {
				(void) fprintf(stderr,
				    gettext(CGMES), linebuf);
				exit(2);
			}
			break;

		}
done:
		if (rep->r1.command == SCOM && !rep->r1.re1)
			if (lastre)
				rep->r1.re1 = lastre;
			else {
				(void) fprintf(stderr,
				    gettext("sed: "
				    "The first RE cannot be null.\n"));
				exit(2);
			}
		if (++rep >= cmdend) {
			(void) fprintf(stderr,
			    gettext("sed: Too many commands, last: %s\n"),
			    linebuf);
			exit(2);
		}

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

/*
 *	Write replacement string for substitution command
 *	into rhsbufparm. Any '\\' characters are left in the
 *	string and parsed for in the replacement phase.
 */
static int    compsub(char **rhsbufparm)
{
	char   *q;
	char   *p;
	wchar_t	wc;
	int	len;

	p = *rhsbufparm;
	q = cp;
	while (*q) {
		if (p > rhsend)
			return (MORESPACE);
		if (*q == '\\') {
			*p++ = *q++;
			/* check for illegal subexpression */
			if (*q > nbra_subexp + '0' && *q <= '9')
				return (0);
			if ((len = mblen(q, MB_CUR_MAX)) < 1)
				break;
		} else {
			if ((len = mbtowc(&wc, q, MB_CUR_MAX)) < 1)
				break;
			if (wc == sseof) {
				*p++ = '\0';
				cp = q + len;
				if (p > rhsend)
					return (MORESPACE);
				rhsp = p;	/* update the rhsbuf pointer */
				return (0);
			}
		}
		while (len--)
			*p++ = *q++;
	}
	return (BADCMD);
}

/*
 *	Read in a single command line.
 *
 * 	lbuf 	- buffer to put line in
 * 	source 	- points to command line expr if any.
 */
static int	rline(char *lbuf, char *source, int *bad)
{
	char   *p, *q;
	int	t;
	static char	*saveq;
	int	i, len;
	char 	str[MB_LEN_MAX];

	*bad = 0;
	p = lbuf - 1;

	if (source != NULL) {
		if (eflag > 0) {
			eflag = -1;
			q = source;
		} else {
			if ((q = saveq) == 0)
				return (-1);
		}

		saveq = NULL;
		while (*q) {
			/* Don't test for '\n' after '\\' */
			if (*q == '\\') {
				*++p = *q++; 	/* Copy the '\\' */
				if (*q == '\0') {
					*bad = 1;
					*++p = '\0';
					return (1);
				}
			} else if (*q == '\n') {
				*++p = '\0';
				saveq = ++q;
				return (1);
			}
			if ((len = mblen(q, MB_CUR_MAX)) < 1)
				return (-1);
			while (len--)
				*++p = *q++;
		}
		*++p = '\0';
		return (1);
	}

	/*
	 * If source is NULL, read from file (fin) which is
	 * is already opened for us
	 */
	while ((t = getc(fin)) != EOF) {
		if (t == '\\') {
			*++p = t;
			t = getc(fin);
			if (t == EOF) {
				*bad = 1;
				*++p = '\0';
				return (1);
			}
			if (t == '\n') {
				if ((t = getc(fin)) == EOF) {
					*bad = 1;
					*++p = '\0';
					return (1);
				} else {
					(void) ungetc(t, fin);
					t = '\n';
				}
			}
		} else if (t == '\n') {
			*++p = '\0';
			return (1);
		}
		len = 1;
		str[0] = t;
		while (t != EOF && mblen(str, MB_CUR_MAX) != len) {
			if (++len > (int)MB_CUR_MAX)
				return (-1);
			str[len-1] = t = getc(fin);
		}
		for (i = 0; t != EOF && i < len; i++)
			*++p = str[i];
	}
	if (p != lbuf - 1) {
		*++p = '\0';
		return (1);
	}
	return (-1);
}

/*
 *	Store an address into addr structure if one is present.
 *	If not, set our_errno flag :
 *		- BADCMD, error in command line.
 *		- REEMPTY, a regular expr. identified but empty.
 *			i.e. substitute previous RE.
 *		- NOADDR, no address given.
 *		- MORESPACE, need a bigger buffer
 */
static struct addr *address(void)
{
	struct addr	*abuf;
	char   *rcp, *rsp;
	off_t		lno;
	int	length;

	our_errno = 0;
	rsp = sp;

	if (*cp == '$') {
		if ((abuf = (struct addr *)malloc(sizeof (struct addr))) == 0) {
			(void) fprintf(stderr,
			    gettext("sed: Memory allocation failed.\n"));
			return (0);
		}
		abuf->afl = STRA;
		abuf->ad.str = rsp;
		*rsp++ = CEND;
		*rsp++ = CCEOF;
		if (rsp >= reend) {
			our_errno = MORESPACE;
			free((void *)abuf);
			return (0);
		}
		cp++;
		sp = rsp;
		return (abuf);
	}

	if (*cp == '/' || *cp == '\\') {	/* address is RE */
		if (*cp == '\\')
			cp++;
		if ((length = mbtowc(&sseof, cp, MB_CUR_MAX)) <= 0) {
			(void) fprintf(stderr, gettext(CGMES), linebuf);
			exit(2);
		}
		cp += length;
		return (comple(sseof));
	}

	rcp = cp;
	lno = 0;

	while (*rcp >= '0' && *rcp <= '9')	/* address is line number */
		lno = lno*10 + *rcp++ - '0';

	if (rcp > cp) {
		if ((abuf = (struct addr *)malloc(sizeof (struct addr))) == 0) {
			(void) fprintf(stderr,
			    gettext("sed: Memory allocation failed.\n"));
			return (0);
		}
		abuf->afl = STRA;
		abuf->ad.str = rsp;
		*rsp++ = CLNUM;
		*rsp++ = nlno;
		tlno[nlno++] = lno;
		if (nlno >= NLINES) {
			(void) fprintf(stderr,
			    gettext("sed: "
			    "There are too many line numbers specified.\n"));
			exit(2);
		}
		*rsp++ = CCEOF;
		if (rsp >= reend) {
			our_errno = MORESPACE;
			free((void *)abuf);
			return (0);
		}
		cp = rcp;
		sp = rsp;
		return (abuf);
	}
	our_errno = NOADDR;
	return (0);
}


static int cmp(char *a, char *b)
{
	char   *ra, *rb;

	ra = a - 1;
	rb = b - 1;

	while (*++ra == *++rb)
		if (*ra == '\0')
			return (0);
	return (1);
}


/*
 *	Read text from linebuf(cp) into textbuf.
 *	Return null if textbuf exceeds endbuf.
 */
static char    *text(char *textbuf, char *endbuf)
{
	char   *p, *q;
	int	len;

	p = textbuf;
	q = cp;
	for (;;) {
		if (endbuf && (p >= endbuf))
			return (0);
		if (*q == '\\')
			q++;	/* Discard '\\' and read next character */
		if (*q == '\0') {
			*p = *q;
			cp = q;
			return (++p);
		}
		/* Copy multi-byte character to p */
		if ((len = mblen(q, MB_CUR_MAX)) < 1)
			(void) fprintf(stderr,
			    gettext(CGMES), linebuf);
		while (len--)
			*p++ = *q++;
	}
}


static struct label    *search(struct label *ptr)
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


static void dechain(void)
{
	struct label    *lptr;
	union reptr	*rptr, *trptr;

	for (lptr = labtab; lptr < lab; lptr++) {

		if (lptr->address == 0) {
			(void) fprintf(stderr,
			    gettext("sed: %s is not a defined label.\n"),
			    lptr->asc);
			exit(2);
		}

		if (lptr->chain) {
			rptr = lptr->chain;
			while ((trptr = rptr->r2.lb1) != NULL) {
				rptr->r2.lb1 = lptr->address;
				rptr = trptr;
			}
			rptr->r2.lb1 = lptr->address;
		}
	}
}


/*
 *	Parse a 'y' command i.e y/xyz/abc/
 *	where xyz are the characters to be matched and
 *	abc are their replacement characters.
 *	N.B. these characters can be multi-byte or the string "\\n"
 *	Return a pointer to the buffer storing these characters.
 */
static wchar_t	*ycomp(void)
{
	wchar_t c1, c2;
	wchar_t *ybuf, *yp;
	char	*tsp, *ssp;
	int	count = 0, len1, len2;

	ssp = cp;
	tsp = cp;
	/* Determine no of characters to be matched */
	/* and then allocate space for their storage */
	/* Set tsp to point to the replacement characters */
	for (;;) {
		if (*tsp == '\0')
			return (0);
		if ((len1 = mbtowc(&c1, tsp, MB_CUR_MAX)) > 0) {
			tsp += len1;
			if (c1 == sseof)
				break;
			if (c1 == '\\') {
				if ((len2 = mbtowc(&c2, tsp, MB_CUR_MAX)) > 0) {
					tsp += len2;
					if (c2 != '\\' && c2 != sseof &&
					    c2 != 'n') {
						return (0);
					}
				}
			}
			count++;
		} else
			return (0);
	}

	/* Allocate space for the characters to be replaced and */
	/* their replacements. The buffer will be built up by storing */
	/* the characters to be replaced and their replacement one after */
	/* the other i.e. in pairs in the buffer. For the search and replace */
	/* stage every second char will be tested and when a match is found */
	/* it will be replaced by the next character in the search buffer */

	ybuf = (wchar_t *)malloc((count * 2 + 1) * sizeof (wchar_t));
	if (!ybuf) {
		(void) fprintf(stderr,
		    gettext("sed: Memory allocation failed.\n"));
		return (0);
	}

	len2 = 0;
	yp = ybuf;
	while ((len1 = mbtowc(&c1, ssp, MB_CUR_MAX)) > 0) {
		len2 = mbtowc(&c2, tsp, MB_CUR_MAX);
		if (c1 == sseof)
			break;
		if (len2 < 1 || *tsp == '\0' || c2 == sseof)
			return (0);
		/* check for new line */
		if (len1 == 1 && *ssp == '\\' && ssp[1] == 'n') {
			ssp++;
			c1 = L'\n';
		}
		/*
		 * check for \ preceding delimiter character and use
		 * as one single pattern
		 */
		if (len1 == 1 && *ssp == '\\' && ssp[1] == sseof) {
			ssp++;
			c1 = (long)sseof;
		}
		/*
		 * check for \ preceding \ character and use
		 * as one single pattern which is slash
		 */
		if (len1 == 1 && *ssp == '\\' && ssp[1] == '\\') {
			ssp++;
			c1 = L'\\';
		}
		ssp += len1;
		*yp++ = c1;

		/* check for new line */
		if (len2 == 1 && *tsp == '\\' && tsp[1] == 'n') {
			tsp++;
			c2 = L'\n';
		}
		/*
		 * check for \ preceding delimiter character and use
		 * as one single pattern
		 */
		if (len2 == 1 && *tsp == '\\' && tsp[1] == sseof) {
			tsp++;
			c2 = (long)sseof;
		}
		/*
		 * check for \ preceding \ character and use
		 * as one single pattern which is slash
		 */
		if (len2 == 1 && *tsp == '\\' && tsp[1] == '\\') {
			tsp++;
			c2 = L'\\';
		}
		tsp += len2;
		*yp++ = c2;
	}
	if (c2 != sseof)
		return (0);
	cp = tsp + len2;
	yp = '\0';

	return (ybuf);
}


/*
 *	Compile the regular expression, returning the results
 *	in a struct addr structure and setting the appropriate
 *	error number if required.
 */
static struct addr	*comple(wchar_t reeof)
{
	struct addr	*res;
	regex_t	*reg;
	int	cflag;

	our_errno = 0;
	/* Read reg. expr. string into pattern */
	cflag = getre(reeof);
	if (!cflag) {
		if ((reg = (regex_t *)malloc(sizeof (regex_t))) == 0) {
			(void) fprintf(stderr,
			    gettext("sed: Memory allocation failed.\n"));
			return (0);
		}


		/*
		 * NULL RE: do not compile a null regular expression; but
		 * process string with last regular expression encountered
		 */

		if (pattern[0] != '\0')
			if (regcomp(reg, pattern, 0) == 0) {
				if ((res =
				    (struct addr *)malloc(sizeof (struct addr)))
				    == 0) {
					(void) fprintf(stderr,
					    gettext("sed: "
					    "Memory allocation failed.\n"));
					return (0);
				}
				res->afl = REGA;
				res->ad.re = reg;
				nbra_subexp = reg->re_nsub;
				return (res);
			} else {
				free((void *)reg);
				our_errno = BADCMD;
			}
	} else
		our_errno = cflag;
	return (0);
}


/*
 *	Read regular expression into pattern, replacing reeof
 *	with a null terminator. Maintains cp, the pointer
 *	into the linebuf string.
 */
static int	getre(wchar_t reeof)
{
	char	*p1;
	char *p2;
	wchar_t	wc;
	int	empty = 1, len;
	int	brackcnt = 0;

	if (pattern == NULL)
		growbuff(&psize, &pattern, &pend, (char **)0);
	p1 = pattern;
	for (;;) {
		if (*cp == '\0' || *cp == '\n')
			break;
		if ((len = mbtowc(&wc, cp, MB_CUR_MAX)) < 1)
			break;
		if (!brackcnt && wc == reeof) {
			cp += len;
			*p1 = '\0';
			return (empty ? REEMPTY : 0);
		}
		empty = 0;
		/*
		 * Outside a bracket expression, the sequece "\n" is
		 * converted to the newline character.
		 * Inside a bracket expression [\n], both '\' and 'n' are
		 * literal characters matching either '\' or 'n'.
		 */
		if (*cp == '\\' && brackcnt == 0) {
			/* Outside a bracket expression */
			*p1 = *cp++;
			if (*cp == 'n') {
				while (p1 >= pend) {
					p2 = p1;
					growbuff(&psize, &pattern, &pend, &p2);
					p1 = p2;
				}
				*p1++ = '\n';
				cp++;
				continue;
			}
			while (p1 >= pend) {
				p2 = p1;    /* need p2 because p1 is register */
				growbuff(&psize, &pattern, &pend, &p2);
				p1 = p2;
			}
			p1++;
			if ((len = mblen(cp, MB_CUR_MAX)) < 1)
				break;
			/*
			 * Special code to allow delimiter to occur within
			 * bracket expression without a preceding backslash
			 */

		} else if (*cp == '[') {
			if (!brackcnt) {
				if (((*(cp+1) == '^') && (*(cp+2) == ']')) ||
				    (*(cp+1) == ']'))
					brackcnt++;
				brackcnt++;
			} else {
				if ((*(cp+1) == '.') || (*(cp+1) == ':') ||
				    (*(cp+1) == '='))
					brackcnt++;
			}
		} else if (*cp == ']' && brackcnt)
			brackcnt--;
		while ((p1 + len) >= pend) {
			p2 = p1;	/* need p2 because p1 is register */
			growbuff(&psize, &pattern, &pend, &p2);
			p1 = p2;
		}
		while (len--)
			*p1++ = *cp++;
	}
	return (BADCMD);
}


/*
 *	Dynamic memory allocation for buffer storage.
 */
static char *growspace(char *buf, char **endp)
{
	int amount;
	static char *last = 0;

	if (last && buf == last) { /* can do realloc */
		amount = (*endp - last) << 1;
		last = realloc(last, amount);
	} else {
		if (!buf || (amount = *endp - buf) < LBSIZE)
			amount = LBSIZE;
		last = malloc(amount);
	}
	if (!last) {
		(void) fprintf(stderr,
		    gettext("sed: Memory allocation failed.\n"));
		exit(2);
	}
	*endp = last + amount;
	return (last);
}
