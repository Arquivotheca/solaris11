/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _INDEX_UTIL_H
#define	_INDEX_UTIL_H

#include <limits.h>

#define	MAXFILEPATH	PATH_MAX	/* max # of file path */
#define	MAXQUERYSIZE	30		/* max # of query length */
#define	MAXTERMSIZE	20		/* max # of term length */
#define	MAXSECTIONSIZE	500		/* max # of section length */
#define	MAXLINESIZE	LINE_MAX	/* max # of man page line length */
#define	LINEWRAP	70		/* line wrap */
#define	NOMAN3		2000
#define	NOSECTION	-2
#define	NOTINSECTION	-1
#define	MERGEFACTOR	100	/* merge factor for index files */
#define	MAN3FACTOR	0.4	/* merge factor for man3* */
#define	NONMAN3FACTOR	1.0	/* merge factor for non-man3* */
#define	RESULTLEN	12	/* define the result number */
#define	INDEXDIR	"man-index"
#define	TERMDOCFILE	"term.doc"
#define	TERMFILE	"term.dic"
#define	TERMINDEXFILE	"term.idx"
#define	TERMFREQ	"term.req"
#define	TERMPOSITION	"term.pos"
#define	VERSIONSTR	"MANINDEX1.0"	/* define the version */
#define	VERSIONSTRLEN	11	/* define the string len for version */
#define	MAXSEC		26	/* max size for section name (-s) */

/*
 * for third part man pages
 */
#define	THIRDPARTMAN	"/usr/share/man/index.d"

typedef struct _Keyword {
	short int sid;
	char sec[MAXSECTIONSIZE];
	char msc[MAXSEC];
	short int size;
	char **word;
} Keyword;

int get_words(const char *, Keyword **, const char *, int);

int free_keyword(Keyword *);

int replace_str(char *, const char *, const char *);

int find_section(const char *);

int normalize(char *);

int compare_str(const char *, const char *);

void malloc_error(void);

#endif /* _INDEX_UTIL_H */
