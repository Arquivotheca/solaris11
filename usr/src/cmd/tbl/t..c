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

/* t..c : external declarations */

#include <stdio.h>
#include <ctype.h>
#include <libintl.h>
#include <strings.h>

#define	MAXLIN		200
#define	MAXHEAD		100
#define	MAXCOL		20
#define	MAXCHS		2000
#define	MAXSTR		1024
#define	MAXRPT		100
#define	CLLEN		10
#define	SHORTLINE	4
#define	BIGBUF		8192

extern int nlin, ncol, iline, nclin, nslin;
extern int style[MAXHEAD][MAXCOL];
extern int ctop[MAXHEAD][MAXCOL];
extern char font[MAXHEAD][MAXCOL][2];
extern char csize[MAXHEAD][MAXCOL][4];
extern char vsize[MAXHEAD][MAXCOL][4];
extern char cll[MAXCOL][CLLEN];
extern int stynum[];
extern int F1, F2;
extern int lefline[MAXHEAD][MAXCOL];
extern int fullbot[];
extern char *instead[];
extern int expflg;
extern int ctrflg;
extern int evenflg;
extern int evenup[];
extern int boxflg;
extern int dboxflg;
extern int linsize;
extern int tab;
extern int pr1403;
extern int linsize, delim1, delim2;
extern int allflg;
extern int textflg;
extern int left1flg;
extern int rightl;
struct colstr {char *col, *rcol; };
extern struct colstr *table[];
extern char *cspace, *cstore;
extern char *exstore, *exlim;
extern int sep[];
extern int used[], lused[], rused[];
extern int linestop[];
extern char *leftover;
extern char *last, *ifile;
extern int texname;
extern int texct;
extern char texstr[];
extern int linstart;

extern FILE *tabin, *tabout;

extern char *chspace(void);
extern char *gets1(char *, int);
extern char *maknew(char *);

extern int *alocv(int);
extern int allh(int);
extern int barent(char *);
extern int ctspan(int, int);
extern int ctype(int, int);
extern int digit(int);
extern int filler(char *);
extern int fspan(int, int);
extern int get1char(void);
extern int get_text(char *, int, int, char *, char *);
extern int ifline(char *);
extern int ineqn(char *, char *);
extern int interh(int, int);
extern int interv(int, int);
extern int lefdata(int, int);
extern int left(int, int, int *);
extern int letter(int);
extern int lspan(int, int);
extern int match(char *, char *);
extern int max(int, int);
extern int midbar(int, int);
extern int midbcol(int, int);
extern int min(int, int);
extern int next(int);
extern int nodata(int);
extern int numb(char *);
extern int oneh(int);
extern int point(int);
extern int prefix(char *, char *);
extern int prev(int);
extern int real(char *);
extern int thish(int, int);
extern int up1(int);
extern int vspand(int, int, int);
extern int vspen(char *);

extern void checkuse(void);
extern void choochar(void);
extern void cleanfc(void);
extern void drawline(int, int, int, int, int, int);
extern void drawvert(int, int, int, int);
extern void endoff(void);
extern void error(char *);
extern void fullwide(int, int);
extern void getcomm(void);
extern void getspec(void);
extern void getstop(void);
extern void gettbl(void);
extern void ifdivert(void);
extern void makeline(int, int, int);
extern void maktab(void);
extern void permute(void);
extern void putfont(char *);
extern void putline(int, int);
extern void putsize(char *);
extern void readspec(void);
extern void release(void);
extern void restline(void);
extern void rstofill(void);
extern void runout(void);
extern void runtabs(int, int);
extern void savefill(void);
extern void saveline(void);
extern void tableput(void);
extern void tcopy(char *, char *);
extern void tohcol(int);
extern void un1getc(int);
extern void untext(void);
extern void yetmore(void);

#define	CRIGHT	80
#define	CLEFT	40
#define	CMID	60
#define	S1	31
#define	S2	32
#define	TMP	38
#define	SF	35
#define	SL	34
#define	LSIZE	33
#define	SIND	37
#define	SVS	36

/* This refers to the relative position of lines */
#define	LEFT	1
#define	RIGHT	2
#define	THRU	3
#define	TOP	1
#define	BOT	2
