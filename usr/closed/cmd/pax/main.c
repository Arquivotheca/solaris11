/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
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

/*
 * Copyright (c) 1989 Mark H. Colburn.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that the above copyright notice is duplicated in all such
 * forms and that any documentation, advertising materials, and other
 * materials related to such distribution and use acknowledge that the
 * software was developed * by Mark H. Colburn and sponsored by The
 * USENIX Association.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * main.c (renamed from pax.c for simplicity in Makefile messaging structure)
 *
 * DESCRIPTION
 *
 *	Pax is the archiver described in IEEE P1003.2.  It is an archiver
 *	which understands both tar and cpio archives and has a new interface.
 *
 * 	Currently supports Draft 11 functionality.
 *	The charmap option was new in Draft 11 and removed in 11.1
 * 	The code for this is present but deactivated.
 *
 * SYNOPSIS
 *
 *	pax -[cdnv] [-H|-L] [-f archive] [-s replstr] [pattern...]
 *	pax -r [-cdiknuv] [-H|-L] [-f archive] [-p string] [-s replstr]
 *	       [pattern...]
 *	pax -w [-dituvX] [-H|-L] [-b blocking] [[-a] -f archive]
 *	       [-s replstr]...] [-x format] [pathname...]
 *	pax -r -w [-diklntuvX] [-H|-L] [-p string] [-s replstr]
 *	       [pathname...] directory
 *
 * DESCRIPTION
 *
 * 	PAX - POSIX conforming tar and cpio archive handler.  This
 *	program implements POSIX conformant versions of tar, cpio and pax
 *	archive handlers for UNIX.  These handlers have defined befined
 *	by the IEEE P1003.2 commitee.
 *
 * COMPILATION
 *
 *	A number of different compile time configuration options are
 *	available, please see the Makefile and config.h for more details.
 *
 * AUTHOR
 *
 *     Mark H. Colburn, NAPS International (mark@jhereg.mn.org)
 *
 * Sponsored by The USENIX Association for public distribution.
 */

/* Headers */

#include <unistd.h>
#include <locale.h>
/* Exclude the extern declarations in pax.h that are defined in main.c */
#define	NO_EXTERN
#include "pax.h"

/* Globally Available Identifiers */

char	*ar_file;		/* File containing name of archive */
char	*bufend;		/* End of data within archive buffer */
char	*bufstart;		/* Archive buffer */
char	*bufidx;		/* Archive buffer index */

#ifdef OSF_MESSAGES
nl_catd	catd;			/* message catalog pointer */
#endif /* ifdef OSF_MESSAGES */

char	*lastheader;			/* pointer to header in buffer */
char	*myname;			/* name of executable (argv[0]) */
char	**argv;				/* global for access by name_* */
int	argc;				/* global for access by name_* */
int	archivefd;			/* Archive file descriptor */
int	blocking;			/* Size of each block, in records */
gid_t	gid;				/* Group ID */
int	head_standard;			/* true if archive is POSIX format */
int	ar_interface;			/* defines interface we are using */
int	ar_format;			/* defines current archve format */
int	mask;				/* File creation mask */
int	ttyf;				/* For interactive queries */
uid_t	uid;				/* User ID */
int	names_from_stdin;		/* names for files are from stdin */
int	firstxhdr;			/* true if 1st xhdr in archive */
int	xcont = 1;			/* true if need to read xhdr */
uint_t	thisgseqnum;			/* global sequence number */

int	exit_status;			/* Exit status of pax */
OFFSET	total;				/* Total number of bytes transferred */
short	areof;				/* End of input volume reached */
short	f_access_time;			/* Reset access times of input files */
short	f_append;			/* Add named files to end of archive */
short	f_blocking;			/* blocking option was specified */
short	f_cmdarg;			/* file is a command line argument */
short	f_create;			/* create a new archive */
short	f_device;			/* stay on the same device */
short	f_dir_create;			/* Create missing directories */
short	f_disposition;			/* ask for file disposition */
short	f_exit_on_error = 0;		/* Exit as soon as an error is seen */
short	f_extract;			/* Extract named files from archive */
short	f_extract_access_time;		/* Reset access times of output files */
short	f_extended_attr = 0;		/* traverse extended attributes */
short	f_follow_first_link;		/* follow symbolic on command line */
short	f_follow_links;			/* follow symbolic links */
short	f_group;			/* Restore group */
short	f_interactive;			/* Interactivly extract files */
short	f_linksleft;			/* Report on unresolved links */
short	f_link;				/* link files where possible */
short	f_linkdata = 0;			/* write contents of file to archive */
short	f_list;				/* List files on the archive */
short	f_mtime;			/* Retain file modification time */
short	f_mode;				/* Preserve the file mode */
short	f_newer;			/* append files to archive if newer */
short	f_stdpax = 0;			/* want pax format per Standard */
short	f_no_depth;			/* Don't go into directories */
short	f_no_overwrite;			/* Don't overwrite existing files */
short	f_owner;			/* extract files as the user */
short	f_pass;				/* pass files between directories */
short	f_pax = 0;			/* want pax format per draft standard */
short	f_posix;			/* Don't put trailing slash in dir */
short	f_reverse_match;		/* Reverse sense of pattern match */
short	f_single_match;			/* Match only once for each pattern */
short	f_sys_attr = 0;			/* traverse ext system attributes */
short	f_times;			/* Put atime, mtime into xhdr */
short	f_unconditional;		/* Copy unconditionally */
short	f_user;				/* Restore file as user */
short	f_verbose;			/* Turn on verbose mode */
short	rename_interact;		/* Rename file interactively */
short	no_data_printed = 1;		/* Unset when listing c_filedata */
time_t	now = 0;			/* Current time */
uint_t	arvolume;			/* Volume number */
uint_t	blocksize = BLOCKSIZE;		/* Archive block size */
FILE	*msgfile;			/* message output file stdout/stderr */
Replstr	*rplhead = (Replstr *)NULL;	/*  head of replstr list */
Replstr	*rpltail;			/* pointer to tail of replstr list */
Dirlist	*dirhead = (Dirlist *)NULL;	/* head of directory list */
Dirlist	*dirtail;			/* tail of directory list */
short		bad_last_match = 0;	/* dont count last match as valid */
int	Hiddendir;			/* Hidden attribute dir ? */
int	xattrbadhead;			/* Is extended attr hdr bad */
char	*exthdrnameopt = NULL;		/* specified name for ext hdr block */
char	*gexthdrnameopt = NULL;		/* name for global ext hdr blk */
int	invalidopt = INV_BYPASS;	/* action to take on invalid values */
char	*listopt = NULL;		/* output format of table of contents */
nvlist_t	*goptlist = NULL; 	/* Options for 'g' extended hdr */
nvlist_t	*xoptlist = NULL;	/* Options for 'x' extended hdr */
nvlist_t	*deleteopt = NULL;	/* List of patterns to omit or ignore */

/* The following are used for extended headers */
struct xtar_hdr	coXtarhdr;
struct xtar_hdr	oXtarhdr;
struct xtar_hdr	Xtarhdr;
size_t		xrec_size = 8 * PATH_MAX + 1;
char		*xrec_ptr = NULL;
off_t		xrec_offset = 0;
int		charset_type = CHARSET_UNKNOWN;
u_longlong_t	oxxhdr_flgs = 0;	/* cmd line 'x' overrides */
u_longlong_t	ogxhdr_flgs = 0;	/* cmd line 'g' overrides */
u_longlong_t	ohdr_flgs = 0;		/* global ext hdr overrides */
u_longlong_t	xhdr_flgs;		/* ext hdr overrides */
u_longlong_t	gxhdr_flgs;
u_longlong_t	xhdr_count = 0;
char		xhdr_dirname[TL_PREFIX + 1];
char		pidchars[PID_MAX_DIGITS + 1];
char		gseqnum[INT_MAX_DIGITS + 1];
char		*local_path = NULL;
char		*xlocal_path = NULL;
char		*glocal_path = NULL;
char		*local_linkpath = NULL;
char		*xlocal_linkpath = NULL;
char		*glocal_linkpath = NULL;
char		*local_gname = NULL;
char		*local_uname = NULL;
char		*local_charset = NULL;
char		*local_comment = NULL;
char		*local_realtime = NULL;
char		*local_security = NULL;
char		*local_holesdata = NULL;
long		saverecsum = 0;
char		savetypeflag = '\0';
char		*savemagic = NULL;
char		saveversion[TL_VERSION + 1];
char		*saveprefix = NULL;
char		*savename = NULL;
char		*savenamesize = NULL;
char		*shdrbuf;

struct option {
	char		*optstr;
	struct option	*nextopt;
};

#ifdef FNMATCH_OLD
    int f_fnmatch_old = 1;
#else /* ifdef FNMATCH_OLD */
    int f_fnmatch_old = 0;
#endif /* ifdef FNMATCH_OLD */

/*
 * The following mechanism is provided to allow us to debug pax in complicated
 * situations, like when it is part of a pipe.  The idea is that you compile
 * with -DWAITAROUND defined, and then add the "-z" command line option to the
 * target pax invocation.  If stderr is available, it will tell you to which
 * pid to attach the debugger; otherwise, use ps to find it.  Attach to the
 * process from the debugger, and, *PRESTO*, you are there!
 *
 * Simply assign "waitaround = 0" once you attach to the process, and then
 * proceed from there as usual.
 */

#ifdef WAITAROUND
    int waitaround = 0;		/* wait for rendezvous with the debugger */
#endif

/*
 *  Define the offsets and lengths into the tar header in a form that is
 *  useful for debugging.
 */

/*
 *  Offsets:
 */

const int TO_NAME = 0;
const int TO_MODE = 100;
const int TO_UID = 108;
const int TO_GID = 116;
const int TO_SIZE = 124;
const int TO_MTIME = 136;
const int TO_CHKSUM = 148;
const int TO_TYPEFLG = 156;
const int TO_LINKNAME = 157;
const int TO_MAGIC = 257;
const int TO_VERSION = 263;
const int TO_UNAME = 265;
const int TO_GNAME = 297;
const int TO_DEVMAJOR = 329;
const int TO_DEVMINOR = 337;
const int TO_PREFIX = 345;

/* Function Prototypes */

static void	do_pax(void);
static int	get_archive_type(void);
static OFFSET   pax_optsize(char *);
static void 	usage(void);

/* External linkages */

extern void	add_replstr(char *);
extern void	do_cpio(void);
extern void	do_tar(void);
extern int	parseopt(const char *);

/*
 * parse_opt_strings()
 *
 * Steps through the linked list of strings, read in with
 * the -o option, and parses them into key=value pairs.
 */
static void
parse_opt_strings(struct option *opts)
{
	struct option	*tmpopt;

	/* Parse each of the options strings */
	while (opts != NULL) {
		tmpopt = opts;
		if (parseopt(tmpopt->optstr) != 0) {
			fatal(strerror(errno));
		}
		opts = tmpopt->nextopt;
		free(tmpopt->optstr);
		free(tmpopt);
	}
}


/*
 * main - main routine for handling all archive formats.
 *
 * DESCRIPTION
 *
 * 	Set up globals and call the proper interface as specified by the user.
 *
 * PARAMETERS
 *
 *	int argc	- count of user supplied arguments
 *	char **argv	- user supplied arguments
 *
 * RETURNS
 *
 *	Exits with an appropriate exit code.
 */


int
main(int ac, char **av)
{
	(void) setlocale(LC_ALL, "");

#ifdef OSF_MESSAGES
	catd = catopen(MF_PAX, 0);
#else /* ifdef OSF_MESSAGES */
#if	!defined(TEXT_DOMAIN)	/* Should be defined by cc -D */
#define	TEXT_DOMAIN "SYS_TEST"	/* Use this only if it weren't */
#endif

	(void) textdomain(TEXT_DOMAIN);
#endif /* ifdef OSF_MESSAGES */

	argv = av;
	argc = ac;

	/* strip the pathname off of the name of the executable */
	if ((myname = strrchr(argv[0], '/')) != (char *)NULL)
		myname++;
	else
		myname = argv[0];

	/* get all our necessary information */
	mask = umask(0);
	(void) umask(mask);	/* Draft 11 - umask affects extracted files */
	uid = getuid();
	gid = getgid();
	now = time((time_t *)0);

	/* open terminal for interactive queries */
	ttyf = open_tty();

	if (strcmp(myname, "tar") == 0)
		do_tar();
	else if (strcmp(myname, "cpio") == 0)
		do_cpio();
	else
		do_pax();

	return (exit_status);
}


/*
 * do_pax - provide a PAX conformant user interface for archive handling
 *
 * DESCRIPTION
 *
 *	Process the command line parameters given, doing some minimal sanity
 *	checking, and then launch the specified archiving functions.
 *
 * PARAMETERS
 *
 * RETURNS
 *
 *    Normally returns 0.  If an error occurs, -1 is returned
 *    and state is set to reflect the error.
 *
 */


static void
do_pax(void)
{
	int	c;
	char	*dirname;
	Stat	st;
	char	*string;
	int	x_format = 0;	/* format as specified via the -x option */
	int	act_format; /* the actual format of existing archive */
#if defined(O_XATTR)
#if defined(_PC_SATTR_ENABLED)
#ifdef WAITAROUND
	char	*opts_p = "zab:cdf:HikLlMno:p:rs:tuvwx:Xy@/";
#else
	char	*opts_p = "ab:cdf:HikLlMno:p:rs:tuvwx:Xy@/";
#endif /* WAITAROUND */

#else	/* _PC_SATTR_ENABLED */
#ifdef WAITAROUND
	char	*opts_p = "zab:cdf:HikLlMno:p:rs:tuvwx:Xy@";
#else
	char	*opts_p = "ab:cdf:HikLlMno:p:rs:tuvwx:Xy@";
#endif /* WAITAROUND */
#endif	/* _PC_SATTR_ENABLED */


#else /* if defined(O_XATTR) */
#ifdef WAITAROUND
	char	*opts_p = "zab:cdf:HikLlMno:p:rs:tuvwx:Xy";
#else
	char	*opts_p = "ab:cdf:HikLlMno:p:rs:tuvwx:Xy";
#endif /* WAITAROUND */
#endif /* if defined(O_XATTR) */
	struct option	*opts = NULL;
	struct option	*tmpopt;
	struct option	*optr;

	/* default input/output file for PAX is STDIN/STDOUT */

	ar_file = "-";

	/*
	 * set up the flags to reflect the default pax interface.  Unfortunately
	 * the pax interface has several options which are completely opposite
	 * of the tar and/or cpio interfaces...
	 */

	ar_format = TAR;	/* default interface if none given for -w */
	ar_interface = PAX;
	blocking = 0;
	blocksize = 0;
	f_append = 0;
	f_blocking = 0;
	f_cmdarg = 0;
	f_create = 0;
	f_device = 0;
	f_dir_create = 1;
	f_disposition = 0;
	f_extract = 0;
	f_follow_first_link = 0;
	f_follow_links = 0;
	f_interactive = 0;
	f_link = 0;
	f_list = 1;
	f_no_depth = 0;
	f_no_overwrite = 0;
	f_pass = 0;
	f_posix = 1;
	f_reverse_match = 0;
	f_single_match = 0;
	f_unconditional = 1;
	f_verbose = 0;
	msgfile = stdout;
	rename_interact = 0;

	f_access_time = 0;
	f_extract_access_time = 1;
	f_mode = 0;
	f_mtime = 1;
	f_group = 0;
	f_user = 0;


	while ((c = getopt(argc, argv, opts_p)) != EOF) {
		switch (c) {
#ifdef WAITAROUND
		case 'z':
			/* rendezvous with the debugger */
			waitaround = 1;
			break;
#endif /* WAITAROUND */

		case 'a':	/* append to archive */
			f_append = 1;
			f_list = 0;
			break;

		case 'b':	/* b <blocking>: set blocking factor */
			f_blocking = 1;

			if ((blocksize = pax_optsize(optarg)) == 0)
				fatal(gettext("Bad block size"));
			break;

		case 'c':	/* match all files execpt those named */
			f_reverse_match = 1;
			break;

		case 'd':	/* do not recurse on directories */
			f_no_depth = 1;
			break;

		case 'f':	/* f <archive>: specify archive */
			ar_file = optarg;
			break;

		case 'H':	/* follow symlink of cmd line arg */
			f_follow_first_link = 1;

			/* -H and -L are mutually exclusive */
			f_follow_links = 0;
			break;

		case 'L':	/* follow symlinks */
			f_follow_links = 1;

			/* -H and -L are mutually exlusive */
			f_follow_first_link = 0;
			break;
		case 'i':	/* interactively rename files */
			f_interactive = 1;
			break;

		case 'k':	/* don't overwrite existing files */
			f_no_overwrite = 1;
			break;

		case 'l':	/* make hard-links when copying */
			f_link = 1;
			break;

		case 'M':	/* match with '/' having no special meaning */
			f_fnmatch_old = (f_fnmatch_old) ? 0 : 1;
			break;

		case 'n':	/* only first match for each pattern */
			f_single_match = 1;
			break;

		case 'o':
			if (opts == NULL) {
				if ((opts = malloc(sizeof (struct option)))
				    == NULL) {
					fatal(strerror(errno));
				}
				STRDUP(opts->optstr, optarg);
				opts->nextopt = NULL;
				optr = opts;
			} else {
				if ((tmpopt = malloc(sizeof (struct option)))
				    == NULL) {
					fatal(strerror(errno));
				}
				STRDUP(tmpopt->optstr, optarg);
				tmpopt->nextopt = NULL;
				optr->nextopt = tmpopt;
				optr = tmpopt;
			}
			break;

		case 'p':	/* privilege options */
			string = optarg;
			while (*string != '\0')
				switch (*string++) {
				case 'a':	/* don't preserve access time */
					f_extract_access_time = 0;
					break;

				case 'e':	/* preserve everything */
					f_extract_access_time = 1;
					f_mtime = 1;	/* mod time */
					f_owner = 1;	/* owner and group */
					f_mode = 1;	/* file mode */

					/*
					 * owner and group need to be
					 * separated for -x pax as
					 * -o delete can be used to
					 * keep from restoring one but not
					 * the other.
					 */
					f_user = 1;	/* owner */
					f_group = 1;	/* group */
					break;

				case 'm':	/* don't preserve mod. time */
					f_mtime = 0;
					break;

				case 'o':	/* preserve uid and gid */
					f_owner = 1;
					/*
					 * owner and group need to be
					 * separated for -x pax as
					 * -o delete can be used to
					 * keep from restoring one but not
					 * the other.
					 */
					f_user = 1;
					f_group = 1;
					break;

				case 'p':	/* preserve file mode bits */
					f_mode = 1;
					break;

				default:
					fatal(gettext("Invalid privileges"));
					break;
				}
			break;

		case 'r':	/* read from archive */
			if (f_create) {
				f_create = 0;
				f_pass = 1;
			} else {
				f_list = 0;
				f_extract = 1;
			}
			msgfile = stderr;
			break;

		case 's':	/* ed-like substitute */
			add_replstr(optarg);
			break;

		case 't':	/* preserve access times on files read */
			f_access_time = 1;
			break;

		case 'u':	/* ignore older files */
			f_unconditional = 0;
			break;

		case 'v':	/* verbose */
			f_verbose = 1;
			break;

		case 'w':	/* write to archive */
			if (f_extract) {
				f_extract = 0;
				f_pass = 1;
			} else {
				f_list = 0;
				f_create = 1;
			}
			msgfile = stderr;
			break;

		case 'x':	/* x <format>: specify archive format */
			if (strcmp(optarg, "ustar") == 0) {
				x_format = TAR;
				if (blocksize == 0)
					/* Draft 11 */
					blocksize = DEFBLK_TAR * BLOCKSIZE;
			} else if (strcmp(optarg, "cpio") == 0) {
				x_format = CPIO;
				if (blocksize == 0)
					/* Draft 11 */
					blocksize = DEFBLK_CPIO * BLOCKSIZE;
			} else if (strcmp(optarg, "xustar") == 0) {
				x_format = PAX;
				f_pax = 1;
				if (blocksize == 0)
					/* Draft 11 */
					blocksize = DEFBLK_TAR * BLOCKSIZE;
			} else if (strcmp(optarg, "pax") == 0) {
				x_format = PAX;
				f_pax = 1;
				f_stdpax = 1;
				thisgseqnum = 0;
				if (blocksize == 0) {
					blocksize = DEFBLK_CPIO * BLOCKSIZE;
				}
			} else {
				usage();
			}
			break;

		case 'X':		/* do not descend into dirs on other */
			f_device = 1;	/* filesystems */
			break;

		case 'y':		/* Not in std: interactively ask for */
					/* the disposition of all files (from */
					/* the net version) */
			f_disposition = 1;
			break;

#if defined(O_XATTR)
		case '@':
			f_extended_attr = 1;
			break;
#if defined(_PC_SATTR_ENABLED)
		case '/':
			f_sys_attr = 1;
			break;
#endif	/* _PC_SATTR_ENABLED */
#endif /* if defined(O_XATTR) */

		default:
			usage();
		}
	}

#ifdef WAITAROUND
	if (waitaround) {
		/*
		 * use this delay point to attach to an instance of the program
		 * in a pipe from the debugger.
		 */

		(void) fprintf(stderr, gettext("Rendezvous with pax on pid"
		    " %d\n"), getpid());

		while (waitaround) {
			(void) sleep(10);
		}
	}
#endif /* WAITAROUND */

	if (blocksize == 0) {
		blocking = DEFBLK_TAR;		/* default for ustar is 20 */
		blocksize = blocking * BLOCKSIZE;
	}
	buf_allocate((OFFSET) blocksize);

	if (!f_unconditional && f_create) {	/* -wu should be an append */
		f_create = 0;
		f_append = 1;
	}

	/* If the archive doesn't exist, we must create rather than */
	/* append. */

	if (f_append && strcmp(ar_file, "-") != 0 &&
	    access(ar_file, F_OK) < 0 && errno == ENOENT) {
		f_create = 1;
		f_append = 0;
	}

	if (f_extract || f_list) {			/* -r or nothing */
		(void) open_archive(AR_READ);
		f_pax = 0;	/* First header record determines type */
		ar_format = get_archive_type();
		parse_opt_strings(opts);
		read_archive();
	} else if (f_create && !f_append) {		/* -w without -a */
		if (optind >= argc)
			names_from_stdin++;	/* args from stdin */
		(void) open_archive(AR_WRITE);
		if (x_format)
			ar_format = x_format;
		parse_opt_strings(opts);
		create_archive();
	} else if (f_append) {				/* -w with -a */
		if (optind >= argc)
			names_from_stdin++;	/* args from stdin */
		(void) open_archive(AR_APPEND);
		act_format = get_archive_type();

		if (x_format && x_format != act_format)
			fatal(gettext("Archive format specified is different "
			    "from existing archive"));
		else if (!x_format && ar_format != act_format)
			fatal(gettext("The default archive format is different "
			    "from existing archive"));

		ar_format = act_format;
		parse_opt_strings(opts);
		append_archive();
	} else if (f_pass && optind < argc) { /* -r and -w (ie, pass mode) */
		f_pax = 1;	/* Use as much precision as possible on times */
		/*
		 * If no -x format was specified, behave as if -x pax were
		 * specified.
		 */
		if (!x_format) {
			x_format = PAX;
			f_stdpax = 1;
			thisgseqnum = 0;
			if (blocksize == 0) {
				blocksize = DEFBLK_CPIO * BLOCKSIZE;
			}
		}
		dirname = argv[--argc];
		if (LSTAT(dirname, &st) < 0) {
			fatal(strerror(errno));
		}
		if ((st.sb_mode & S_IFMT) != S_IFDIR)
			fatal(gettext("Not a directory"));
		if (optind >= argc)
			names_from_stdin++;	/* args from stdin */
		parse_opt_strings(opts);
		pass(dirname);
	} else
		usage();

	names_notfound();
}


/*
 * get_archive_type - determine input archive type from archive header
 *
 * DESCRIPTION
 *
 * 	reads the first block of the archive and determines the archive
 *	type from the data.  Exits if the archive cannot be read.  If
 *	verbose mode is on, then the archive type will be printed on the
 *	standard error device as it is determined.
 *
 */


static int
get_archive_type(void)
{
	int act_format;

	if (ar_read() != 0)
		fatal(gettext("Unable to determine archive type."));
	if (strncmp(bufstart, M_ASCII, strlen(M_ASCII)) == 0) {
		act_format = CPIO;
		if (f_verbose)
			(void) fputs(gettext("ASCII CPIO format archive\n"),
			    stderr);
	} else if (strncmp(&bufstart[TO_MAGIC], TMAGIC, strlen(TMAGIC)) == 0) {
		act_format = TAR;
		if ((bufstart[TO_TYPEFLG] == XHDRTYPE) ||
		    (bufstart[TO_TYPEFLG] == XXHDRTYPE) ||
		    (bufstart[TO_TYPEFLG] == GXHDRTYPE)) {
			f_pax = 1;
			act_format = PAX;
			if ((bufstart[TO_TYPEFLG] == XXHDRTYPE) ||
			    (bufstart[TO_TYPEFLG] == GXHDRTYPE)) {
				f_stdpax = 1;
			}
		}
		if (f_verbose) {
			if (f_pax)
				(void) fputs(gettext(
				    "USTAR format archive extended\n"), stderr);
			else
				(void) fputs(gettext(
				    "USTAR format archive\n"), stderr);
		}
	/* LINTED alignment */
	} else if (*((ushort_t *)bufstart) == M_BINARY ||
	    /* LINTED alignment */
	    *((ushort_t *)bufstart) == SWAB(M_BINARY)) {
		act_format = CPIO;
		if (f_verbose)
			(void) fputs(gettext("Binary CPIO format archive\n"),
			    stderr);
	} else {
		/* should we return 0 here? */
		act_format = TAR;
	}

	return (act_format);
}


/*
 * pax_optsize - interpret a size argument
 *
 * DESCRIPTION
 *
 * 	Recognizes suffixes for blocks (512-bytes), k-bytes and megabytes.
 * 	Also handles simple expressions containing '+' for addition.
 *
 * PARAMETERS
 *
 *    char 	*str	- A pointer to the string to interpret
 *
 * RETURNS
 *
 *    Normally returns the value represented by the expression in the
 *    the string.
 *
 * ERRORS
 *
 *	If the string cannot be interpreted, the program will fail, since
 *	the buffering will be incorrect.
 *
 */


static OFFSET
pax_optsize(char *str)
{
	char	*idx;
	OFFSET	number;	/* temporary storage for current number */
	OFFSET	result;	/* cumulative total to be returned to caller */

	result = 0;
	idx = str;
	for (;;) {
		number = 0;
		while (*idx >= '0' && *idx <= '9')
			number = number * 10 + *idx++ - '0';

		switch (*idx++) {
		case 'b':
			result += number * 512L;
			continue;

		case 'k':
			result += number * 1024L;
			continue;

		case 'm':
			result += number * 1024L * 1024L;
			continue;

		case '+':
			result += number;
			continue;

		case '\0':
			result += number;
			break;

		default:
			break;
		}
		break;
	}
	if (*--idx)
		fatal(gettext("Unrecognizable value"));
	return (result);
}



/*
 * usage - print a helpful message and exit
 *
 * DESCRIPTION
 *
 *	Usage prints out the usage message for the PAX interface and then
 *	exits with a non-zero termination status.  This is used when a user
 *	has provided non-existant or incompatible command line arguments.
 *
 * RETURNS
 *
 *	Returns an exit status of 1 to the parent process.
 *
 */


static void
usage(void)
{
#ifdef WAITAROUND

#if defined(O_XATTR)
#if defined(_PC_SATTR_ENABLED)
	(void) fprintf(stderr, gettext(
	    "Usage:	%s [-z] -[cdnv] [-H|-L] [-f archive] "
	    "[-s replstr] [pattern...]\n"
	    "	%s [-z] -r [-cdiknuvy@/] [-H|-L] [-f archive] [-p string] "
	    "[-s replstr]\n		[pattern...]\n"
	    "	%s [-z] -w [-dituvyX@/] [-H|-L] [-b blocking]\n"
	    "		[[-a] -f archive] [-s replstr] "
	    "[-x format [-o options]] [pathname...]\n"
	    "	%s [-z] -r -w [-diklntuvyX@/] [-H|-L] [-p string] [-s replstr] "
	    "[-o options]\n"
	    "		[pathname...] directory\n"),
	    myname, myname, myname, myname);
#else	/* _PC_SATTR_ENABLED */
	(void) fprintf(stderr, gettext(
	    "Usage:	%s [-z] -[cdnv] [-H|-L] [-f archive] "
	    "[-s replstr] [pattern...]\n"
	    "	%s [-z] -r [-cdiknuvy@] [-H|-L] [-f archive] [-p string] "
	    "[-s replstr]\n		[pattern...]\n"
	    "	%s [-z] -w [-dituvyX@] [-H|-L] [-b blocking]\n"
	    "		[[-a] -f archive] [-s replstr] "
	    "[-x format [-o options]] [pathname...]\n"
	    "	%s [-z] -r -w [-diklntuvyX@] [-H|-L] [-p string] [-s replstr] "
	    "[-o options]\n"
	    "		[pathname...] directory\n"),
	    myname, myname, myname, myname);
#endif	/* _PC_SATTR_ENABLED */

#else /* if defined(O_XATTR) */
	(void) fprintf(stderr, gettext(
	    "Usage:	%s [-z] -[cdnv] [-H|-L] [-f archive] "
	    "[-s replstr] [pattern...]\n"
	    "	%s [-z] -r [-cdiknuvy] [-H|-L] [-f archive] [-p string] "
	    "[-s replstr]\n		[pattern...]\n"
	    "	%s [-z] -w [-dituvyX] [-H|-L] [-b blocking] [[-a] -f archive]\n"
	    "		[-s replstr] [-x format] [-o options] [pathname...]\n"
	    "	%s [-z] -r -w [-diklntuvyX] [-H|-L] [-p string] [-s replstr] "
	    "[-o options]\n"
	    "		[pathname...] directory\n"),
	    myname, myname, myname, myname);
#endif /* if defined(O_XATTR) */

#else /* WAITAROUND */

#if defined(O_XATTR)
#if defined(_PC_SATTR_ENABLED)
	(void) fprintf(stderr, gettext(
	    "Usage:	%s -[cdnv] [-H|-L] [-f archive] "
	    "[-s replstr] [pattern...]\n"
	    "	%s -r [-cdiknuvy@/] [-H|-L] [-f archive] [-p string] "
	    "[-s replstr]\n		[pattern...]\n"
	    "	%s -w [-dituvyX@/] [-H|-L] [-b blocking] [[-a] -f archive]\n"
	    "		[-s replstr] [-x format] [-o options] [pathname...]\n"
	    "	%s -r -w [-diklntuvyX@/] [-H|-L] [-p string] [-s replstr] "
	    "[-o options]\n		[pathname...] directory\n"), myname,
	    myname, myname, myname);
#else	/* _PC_SATTR_ENABLED */
	(void) fprintf(stderr, gettext(
	    "Usage:	%s -[cdnv] [-H|-L] [-f archive] "
	    "[-s replstr] [pattern...]\n"
	    "	%s -r [-cdiknuvy@] [-H|-L] [-f archive] [-p string] "
	    "[-s replstr]\n		[pattern...]\n"
	    "	%s -w [-dituvyX@] [-H|-L] [-b blocking] [[-a] -f archive]\n"
	    "		[-s replstr] [-x format] [-o options] [pathname...]\n"
	    "	%s -r -w [-diklntuvyX@] [-H|-L] [-p string] [-s replstr] "
	    "[-o options]\n		[pathname...] directory\n"), myname,
	    myname, myname, myname);
#endif	/* _PC_SATTR_ENABLED */

#else /* if defined(O_XATTR) */
	(void) fprintf(stderr, gettext(
	    "Usage:	%s -[cdnv] [-H|-L] [-f archive] "
	    "[-s replstr] [pattern...]\n"
	    "	%s -r [-cdiknuvy] [-H|-L] [-f archive] [-p string] "
	    "[-s replstr]\n		[pattern...]\n"
	    "	%s -w [-dituvyX] [-H|-L] [-b blocking] [[-a] -f archive]\n"
	    "		[-s replstr] [-x format] [-o options] [pathname...]\n"
	    "	%s -r -w [-diklntuvyX] [-H|-L] [-p string] [-s replstr] "
	    "[-o options]\n		[pathname...] directory\n"), myname,
	    myname, myname, myname);
#endif /* if defined(O_XATTR) */

#endif /* WAITAROUND */
	exit(1);
}
