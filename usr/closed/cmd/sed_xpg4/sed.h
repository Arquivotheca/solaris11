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
 * COMPONENT_NAME: sed.h
 *
 * FUNCTIONS: none
 *
 * Based on OSF sed(1) command with POSIX/XCU4 Spec:1170 changes.
 */

#include <limits.h>
#include <stdlib.h>
#include <regex.h>

#define	REG_SUBEXP_MAX	20
#define	CEND	16	/* for end marker on RE string */
#define	CLNUM   14

#define	NLINES  256
#define	DEPTH   20
#define	PTRSIZE 100
#define	ABUFSIZE	20
#define	LBSIZE	8192	/* XPG4: pattern/hold space should hold 8192 bytes */
#define	ESIZE   256
#define	LABSIZE 50
#define	GLOBAL_SUB -1	/* global substitution */

extern int	exit_value;
extern char	CGMES[];
extern union reptr	*abuf[];
extern union reptr	**aptr;

/* linebuf start, end, and size */
extern char    *linebuf;
extern char    *lbend;
extern unsigned int	lsize;

/* holdsp start, end, and size */
extern char    *holdsp;
extern char    *hend;
extern unsigned int	hsize;
extern char    *hspend;

/* gen buf start, end, and size */
extern char    *genbuf;
extern char    *gend;
extern unsigned int	gsize;

extern off_t	lnum;
extern char	*spend;
extern int	nflag;
extern off_t	tlno[];

/*
 *	Define command flags.
 */
#define	ACOM    01
#define	BCOM    020
#define	CCOM    02
#define	CDCOM   025
#define	CNCOM   022
#define	COCOM   017
#define	CPCOM   023
#define	DCOM    03
#define	ECOM    015
#define	EQCOM   013
#define	FCOM    016
#define	GCOM    027
#define	CGCOM   030
#define	HCOM    031
#define	CHCOM   032
#define	ICOM    04
#define	LCOM    05
#define	NCOM    012
#define	PCOM    010
#define	QCOM    011
#define	RCOM    06
#define	SCOM    07
#define	TCOM    021
#define	WCOM    014
#define	CWCOM   024
#define	YCOM    026
#define	XCOM    033

/*
 *	Define some error conditions.
 */
#define	REEMPTY		01    /* An empty regular expression */
#define	NOADDR		02    /* No address field in command */
#define	BADCMD		03    /* Fatal error !! */
#define	MORESPACE	04    /* Need to increase size of buffer */

/*
 *	Define types an address can take.
 */
#define	STRA	10	/* string */
#define	REGA	20	/* regular expression */

/*
 *	Structure to hold address information.
 */
struct 	addr {
	int	afl;		/* STRA or REGA */
	union	adbuf {
		char	*str;
		regex_t *re;
	} ad;
};

/*
 *	Structure to hold sed commands.
 */
union   reptr {
	struct {
		struct addr    *ad1;
		struct addr    *ad2;
		struct addr    *re1;
		char    *rhs;
		wchar_t *ytxt;
		FILE    *fcode;
		char    command;
		short   gfl;
		char    pfl;
		char    inar;
		char    negfl;
	} r1;
	struct {
		struct addr    *ad1;
		struct addr    *ad2;
		union reptr    *lb1;
		char    *rhs;
		wchar_t *ytxt;
		FILE    *fcode;
		char    command;
		short   gfl;
		char    pfl;
		char    inar;
		char    negfl;
	} r2;
};
extern union reptr cmdspace[];

struct label {
	char    asc[9];
	union reptr	*chain;
	union reptr	*address;
};
extern int	eargc;

extern union reptr	*pending;
extern char	*badp;

void 	execute(char *);
void	growbuff(unsigned int *, char **, char **, char **);
