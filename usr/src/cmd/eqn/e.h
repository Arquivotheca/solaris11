/*
 * Copyright (c) 1990, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved. The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

#ifndef	_E_H
#define	_E_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <stdio.h>

#define	NONFATAL	0
#define	FATAL	1
#define	ROM	'1'
#ifndef NEQN
#define	ITAL	'2'
#define	BLD	'3'
#else	/* NEQN */
#define	ITAL	'1'
#define	BLD	'1'
#endif	/* NEQN */

#ifndef NEQN
#define	VERT(n)	((((n)+1)/3)*3)
#define	POINT	72
#define	EM(m, ps)	(int)((((float)(m)*(ps) * resolution) / POINT))
#else	/* NEQN */
#define	VERT(n)	(20 * (n))
#endif	/* NEQN */
#define	EFFPS(p)	((p) >= 6 ? (p) : 6)

extern int	dbg;
extern int	ct;
extern int	lp[];
extern int	used[];	/* available registers */
extern int	ps;	/* dflt init pt size */
extern int	resolution;	/* resolution of ditroff */
extern int	deltaps;	/* default change in ps */
extern int	gsize;	/* global size */
extern int	gfont;	/* global font */
extern int	ft;	/* dflt font */
extern FILE	*curfile;	/* current input file */
extern int	ifile;	/* input file number */
extern int	linect;	/* line number in current file */
extern int	eqline;	/* line where eqn started */
extern int	svargc;
extern char	**svargv;
extern int	eht[];
extern int	ebase[];
extern int	lfont[];
extern int	rfont[];
extern int	yyval;
extern int	*yypv;
extern int	yylval;
extern int	eqnreg, eqnht;
extern int	lefteq, righteq;
extern int	lastchar;	/* last character read by lex */
extern int	markline;	/* 1 if this EQ/EN contains mark or lineup */

typedef struct s_tbl {
	char	*name;
	char	*defn;
	struct s_tbl *next;
} tbl;
extern  char    *spaceval;  /* use in place of normal \x (for pic) */

extern	char	*bad_str(void);

extern	int	gtc(void);
extern	int	max(int, int);
extern	int	oalloc(void);
extern	int	openinfile(void);
extern	int	yyparse(void);

extern	void	bshiftb(int, int, int);
extern	void	error(int, char *, /* args */ ...);
extern	void	getstr(char *, int);
extern	void	globfont(void);
extern	void	globsize(void);
extern	void	init_tbl(void);
extern	void	lpile(int, int, int);
extern	void	nrwid(int, int, int);
extern	void	ofree(int);
extern	void	setps(int);
extern	void	shift2(int, int, int);

#ifdef	__cplusplus
}
#endif

#endif	/* _E_H */
