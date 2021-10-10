/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"
/*
 * COMPONENT_NAME: (CMDPOSIX) new commands required by Posix 1003.2
 *
 * FUNCTIONS: None
 *
 * ORIGINS: 27, 85
 *
 * This module contains IBM CONFIDENTIAL code. -- (IBM
 * Confidential Restricted when combined with the aggregated
 * modules for this product)
 * OBJECT CODE ONLY SOURCE MATERIALS
 * (C) COPYRIGHT International Business Machines Corp. 1993
 * All Rights Reserved
 *
 * US Government Users Restricted Rights - Use, duplication or
 * disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
 *
 * (c) Copyright 1990, 1991, 1992, 1993 OPEN SOFTWARE FOUNDATION, INC.
 * ALL RIGHTS RESERVED
 *
 * OSF/1 1.2
 */

/*
 * Header:
 *
 */

#ifndef _PATCH_COMMON_H
#define	_PATCH_COMMON_H

#define	DEBUGGING /* Do not remove DEBUGGING in order to support the -x flag */

#include "config.h"

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <assert.h>
#include <wchar.h>
#include <widec.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <libintl.h>
#include <limits.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* constants */

#define	SCCSPREFIX "s."
#define	GET "sccs edit %s"

#define	RCSSUFFIX ",v"
#define	CHECKOUT "co -l %s"

/* Exit codes */
#define	OK_EXIT_VALUE		0
#define	REJECT_EXIT_VALUE	1
#define	ABORT_EXIT_VALUE	2
#define	FAIL_EXIT_VALUE		3

/* typedefs */

typedef char bool;
#define	    FALSE	0
#define	    TRUE	1

/*
 * Needs to be large enough to hold any line number.
 * 64-bit file systems will require change later.
 */
typedef long LINENUM;			/* must be signed */


/*
 * line_address contains the information to fetch a particular line
 * from the temp file.
 */

typedef struct {
	off_t		offset;		/* Offset in bytes into file */
	unsigned long	length;		/* Lenght in bytes of line */
} line_address;


/*
 * Everything that is necessary to handle a file.
 */

typedef struct {
	char		*name;		/* Name of original file */
	char		*temp_file;	/* Name of temp file  */
	int		tempfd;		/* file descriptor for temp file */
	int		flags;		/* Control flags */
	LINENUM		line_count;	/* Number of lines currently in file */
	LINENUM		max_lines;	/* Max lines before needing expansion */
	unsigned long	mapped_page;	/* Page currently mapped */
	unsigned long	mapped_npages;	/* number of pages currently mapped */
	caddr_t		mapped_address;	/* Address page maped to */
	line_address	*lines;		/* Array of line pointers */
} file_info;

/* Bits for file_info.flags */
#define	UPDATE_ON_EXIT	1
#define	SAVE_ORIGINAL	2


/*
 * Fully describes a hunk.
 */

typedef struct {
	int	max_lines;		/* Max lines before needing expansion */
	int	line_count;		/* Number of lines currently in hunk */
	LINENUM	file1_start;		/* file offset for old lines */
	LINENUM	file1_lines;		/* # of old lines represented here */
	LINENUM	file2_start;		/* file offset for new lines */
	LINENUM	file2_lines;		/* # of new lines represented here */
	wchar_t	*lines[1];		/* hunk lines */
} hunk_info;


/* globals */
extern struct stat	filestat;		/* file statistics area */

extern char		*buf;			/* general purpose buffer */
extern wchar_t		*wbuf;			/* general purpose buffer */

extern char		*outname;		/* Name of output file */
extern bool		verbose;		/* Be noisey */
extern bool		reverse;		/* Reverse patch before */
extern bool		skip_rest_of_patch;	/* Skip */
extern int		strippath;		/* # of components to strip */
extern file_info	**opened_files;		/* Open file table pointer */
extern unsigned long	opened_file_descriptors; /* # of opened files */

extern int		diff_type;		/* current diff type */
extern int		cdiff_type;		/* Command line diff type */
#define			CONTEXT_DIFF		1
#define			NORMAL_DIFF		2
#define			ED_DIFF			3
#define			NEW_CONTEXT_DIFF	4
#define			UNIFIED_DIFF 		5
#define			MOD_ED_DIFF 		6

extern long		max_input;		/* Maximum line length */
extern int		dont_sync;		/* Don't sync files on exit */

/*
 * Functions defined in file.c
 */

int		open_file(char *, int);
void		close_file(file_info *);
void		insert_line(file_info *, wchar_t *, LINENUM);
void		delete_line(file_info *, LINENUM);
wchar_t		*fetch_line(file_info *, LINENUM);
void		sync_file(file_info *, char *, int);
void		update_with_file_contents(file_info *, const char *);
void		free_hunk(hunk_info *);

/*
 * Functions defined in pch.c
 */

void		re_patch(void);
int		open_patch_file(char *);
bool		there_is_another_patch(file_info *, char **);
void		skip_to(file_info *, long);
hunk_info	*another_hunk(file_info *);
void		do_ed_script(file_info *, file_info *);
void		pch_swap(hunk_info *);


/*
 * Functions defined in util.c
 */

char		*fetchname(wchar_t *, int, int);
int		move_file(char *, char *);
void		copy_file(char *, char *);
void		say(char *, ...);
void		fatal(char *, ...);
void		pfatal(char *, ...);
void		ask(char *, ...);
char		*savestr(char *);
wchar_t		*wsavestr(wchar_t *);
void		set_signals(void);
void		ignore_signals(void);
void		makedirs(char *, bool);
void		*allocate(size_t);
void		*reallocate(void *, size_t);
void		cleanup(void);

#endif /* _PATCH_COMMON_H */
