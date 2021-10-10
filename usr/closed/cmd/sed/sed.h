/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

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
 * Copyright (c) 1988, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * sed -- stream  editor
 */

#ifndef _SED_H
#define	_SED_H

#ifdef  __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <limits.h>
#include <regexpr.h>
#define	multibyte	(MB_CUR_MAX > 1)
#define	MULTI_BYTE_MAX	MB_LEN_MAX
#define	CEND	16
#define	CLNUM	14

#define	NLINES		256
#define	DEPTH		20
#define	RESIZE		5000
#define	ABUFSIZE	20
#define	LBSIZE		4000
#define	ESIZE		256
#define	LABSIZE		50
extern union reptr	*abuf[];
extern union reptr **aptr;

extern char    *linebuf; /* buffers */
extern char    *holdsp;
extern char    *genbuf;

extern char    *lbend;  /* end of buffers */
extern char    *hend;
extern char    *gend;

extern char    *spend;  /* pointer to the terminatting null at */
extern char    *hspend; /* the end of the current contents of the buffer */

extern long long lnum;

extern unsigned int	lsize; /* size of buffers */
extern unsigned int	gsize;
extern unsigned int	hsize;
extern int		nflag;
extern long long tlno[];

#define	ACOM	01
#define	BCOM	020
#define	CCOM	02
#define	CDCOM	025
#define	CNCOM	022
#define	COCOM	017
#define	CPCOM	023
#define	DCOM	03
#define	ECOM	015
#define	EQCOM	013
#define	FCOM	016
#define	GCOM	027
#define	CGCOM	030
#define	HCOM	031
#define	CHCOM	032
#define	ICOM	04
#define	LCOM	05
#define	NCOM	012
#define	PCOM	010
#define	QCOM	011
#define	RCOM	06
#define	SCOM	07
#define	TCOM	021
#define	WCOM	014
#define	CWCOM	024
#define	YCOM	026
#define	XCOM	033

union   reptr {
	struct reptr1 {
		union	reptr	*next;
		char    *ad1;
		char    *ad2;
		char    *re1;
		char    *rhs;
		FILE    *fcode;
		char    command;
		int    gfl;
		char    pfl;
		char    inar;
		char    negfl;
	} r1;
	struct reptr2 {
		union	reptr	*next;
		char    *ad1;
		char    *ad2;
		union reptr	*lb1;
		char    *rhs;
		FILE    *fcode;
		char    command;
		int    gfl;
		char    pfl;
		char    inar;
		char    negfl;
	} r2;
};
extern union reptr *ptrspace;

struct label {
	char	asc[9];
	union	reptr	*chain;
	union	reptr	*address;
};

extern int	eargc;

extern union reptr	*pending;
extern char    *badp, *cp;
extern char *respace, *reend;

#ifdef __STDC__
extern const char TMMES[];
#else
extern char TMMES[];
#endif

char    *compile();
char    *ycomp();
char    *address();
char    *text();
char    *compsub();
struct label    *search();
char    *gline();
char    *place();
extern void execute(char *);
extern void growbuff(unsigned int *, char **, char **, char **);

#ifdef  __cplusplus
}
#endif

#endif	/* _SED_H */
