/*
 * Copyright (c) 1994, 2011, Oracle and/or its affiliates. All rights reserved.
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

/*
 * create.c - Create a tape archive.
 *
 * DESCRIPTION
 *
 *	These functions are used to create/write and archive from an set of
 *	named files.
 *
 * AUTHOR
 *
 * 	Mark H. Colburn, NAPS International (mark@jhereg.mn.org)
 *
 * Sponsored by The USENIX Association for public distribution.
 */

/* Headers */

/*
 * Note: We need to get the right version of major() and minor() from libc,
 * therefore include <sys/types.h> and <sys/mkdev.h> rather than
 * <sys/sysmacros.h>.
 */
#include <sys/types.h>
#include <sys/mkdev.h>
#include <iconv.h>
#include <langinfo.h>
#include <assert.h>
#include <dirent.h>
#include <archives.h>

#include "pax.h"

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#if defined(_PC_SATTR_ENABLED)
#include <libgen.h>
#include <attr.h>
#include <libcmdutils.h>
#endif


/*
 * For debugging and testing, force extended headers for uid, gid, size,
 * devmajor and devminor for small values of these items.
 */
#ifdef XHDR_DEBUG
#undef OCTAL7CHAR
#define	OCTAL7CHAR 3
#undef TAR_OFFSET_MAX
#define	TAR_OFFSET_MAX 100
#endif

/* Function Prototypes */

static int archive_file(char **name, size_t *namesz, Stat *sb);
static int check_update(char *, Stat *);
static void dump_xattrs(char **name, size_t *namesz, char *attrparent,
    int baseparent_fd);
static void gen_num(const char *keyword, const u_longlong_t number);
static void gen_date(const char *keyword, const timestruc_t time_value);
static void gen_string(const char *keyword, const char *value);
static int gen_utf8_names(int xhdrtype, const char *filename, Stat *asb,
    char *hdr);
static char *get_exthdrname(const char *string, int xhdrtype,
    const char *dname, const char *bname, char *xtar_hdr);
static int local_utf8(char **Xhdr_ptrptr, char *target, const char *source,
    iconv_t iconv_cd, int xhdrflg, int xhdrtype, int maxval);
static void p_alloc(int flg);
static void prepare_xattr_hdr(char **attrbuf, char *filename, char *attrpath,
    char typeflag, Link *linkinfo, int *rlen);
static char *split_pathname(char *pathname, char *xtar_hdr);
static char tartype(mode_t);
static int write_exthdr(int xhdrtype, char *namep, char *filename,
    Stat *asb, char *hdr);
static int writetar(char *, Stat *);
static int writecpio(char *, Stat *);
static void x_gen_date(int, char *, const timestruc_t);

#define	GEHDRNAME	"$TMPDIR/GlobalHead.%p.%n"
#define	EHDRNAME	"%d/PaxHeaders.%p/%f"
#define	PATHSIZE	TL_NAME + TL_PREFIX
#define	LOCAL	0
#define	XLOCAL	1
#define	GLOCAL	2
#define	STRCAT(a, b, i, asz, bsz)	{ \
				if ((i + bsz) >= asz) { \
					asz = bsz + (asz * 2) + 1; \
					if ((a = realloc(a, \
					    asz * sizeof (char))) \
					    == NULL) { \
						fatal(gettext( \
						    "out of " \
						    "memory")); \
					} \
				} \
				if (i == 0) { \
					(void) strcpy(a, b); \
				} else { \
					(void) strcat(a, b); \
				} \
				i += strlen(b); \
			}

#define	ADDCHAR(a, b, i, asz)	{ \
				if ((i + 1) >= asz) { \
					asz = (asz == 0) ? 2 : (asz * 2) + 1; \
					if ((a = realloc(a, \
					    asz * sizeof (char))) \
					    == NULL) { \
						fatal(gettext( \
						    "out of " \
						    "memory")); \
					} \
				} \
				a[i++] = b; \
				a[i] = '\0'; \
			}

/*
 * split_pathname()
 *
 * Returns the name from a given pathname.
 *
 * If the pathname is longer than PATH_MAX characters, then it is too
 * big to fit in the extended header block's name and prefix fields,
 * therefore a message will be printed and NULL will be returned.
 *
 * If the given pathname can fit in the extended header block's name
 * field, then 'pathname' is returned, otherwise, we try to split
 * the given pathname in two, divided at the trailing '/'.
 *
 * If, after then split, the 'name' is still longer than the name
 * field (TL_NAME characters), or the 'prefix' part of the pathname
 * is longer than the prefix field (TL_PREFIX characters) then a
 * message will be printed and NULL will be returned.
 *
 * Otherwise, the 'prefix' portion of the given pathname will be
 * saved in the extended header's prefix field, and the 'name' portion
 * of the given pathname will be returned.
 */
static char *
split_pathname(char *pathname, char *xtar_hdr)
{
	int	bsize;
	char	*newstr = NULL;
	char	prefix[TL_PREFIX + 1];

	if ((pathname == NULL) || pathname[0] == '\0') {
		if (pathname != NULL) {
			free(pathname);
		}
		errno = 0;
		return (NULL);
	}

	/*
	 * Check to make sure the given pathname will fit in the
	 * extended header block (TL_NAME + TL_PREFIX = 256 characters).
	 */

	/* Easy case.  The pathname fits within the ext hdr name field. */
	if ((bsize = strlen(pathname)) <= TL_NAME) {
		newstr = s_calloc(TL_NAME + 1);
		(void) strncpy(newstr, pathname, TL_NAME);

	/* Given pathname is too long for the extended header. */
	} else if (bsize > PATHSIZE) {
		free(pathname);
		errno = ENAMETOOLONG;
		return (NULL);

	/* Try splitting into two fields: name and prefix */
	} else if (bsize > TL_NAME) {
		char	*lastslash;
		size_t	namelen;
		size_t	prefixlen;

		/*
		 * Since the prefix field is limited to TL_PREFIX
		 * characters, look for the last slash within PREFIX + 1
		 * characters.  (PREFIX + 1 because the character in
		 * that position could be '/'.  It can be lost as
		 * when the prefix and name fields are processed,
		 * a '/' is concatenated to the prefix field.)
		 */
		(void) memset(prefix, '\0', sizeof (prefix));
		(void) strncpy(&prefix[0], pathname, TL_PREFIX + 1);
		lastslash = strrchr(prefix, '/');
		if (lastslash == NULL) {
			free(pathname);
			errno = ENAMETOOLONG;
			return (NULL);
		}
		*lastslash = '\0';
		prefixlen = strlen(prefix);
		namelen = bsize - prefixlen - 1;
		/*
		 * If the name is greater than TL_NAME, we
		 * can't archive the file.
		 */
		if (namelen > TL_NAME) {
			free(pathname);
			errno = ENAMETOOLONG;
			return (NULL);
		} else {
			(void) strncpy(&xtar_hdr[TO_PREFIX], prefix,
			    prefixlen);
			STRDUP(newstr, (pathname + prefixlen + 1));
		}
	}
	free(pathname);
	return (newstr);
}

/*
 *
 * pax has been changed to support extended attributes.
 *
 * As part of this change pax now uses the new *at() syscalls
 * such as openat, fchownat(), unlinkat()...
 *
 * This was done so that attributes can be handled with as little code changes
 * as possible.
 *
 * What this means is that pax now opens the directory that a file or directory
 * resides in and then performs *at() functions to manipulate the entry.
 *
 * For example a new file is now created like this:
 *
 * dfd = open(<some dir path>)
 * fd = openat(fd, <name>,....);
 *
 * or in the case of an extended attribute
 *
 * dfd = attropen(<pathname>, ".", ....)
 *
 * Once we have a directory file descriptor all of the *at() functions can
 * be applied to it.
 *
 * unlinkat(dfd, <component name>,...)
 * fchownat(dfd, <component name>,..)
 *
 * This works for both normal namespace files and extended attribute file
 *
 */

/*
 * get_exthdrname()
 *
 * Generates an extended header name (string) after making character
 * substitutions based on the typeflag of the extended header and
 * the -o exthdr.name[:]=string data specified on the command line.
 *
 * The following character substitutions are made for a
 * typeflag 'x' header (-o exthdr.name:=string was specified on the
 * command line):
 *	  String
 *	 Includes           Replaced By
 *	|--------|---------------------------------------|
 *	|   %d   | The directory name of the file.       |
 *	|--------|---------------------------------------|
 *	|   %f   | The filename of the file.             |
 *	|--------|---------------------------------------|
 *	|   %p   | The process ID of the pax process.    |
 *	|--------|---------------------------------------|
 *	|   %%   | A percent '%' character.              |
 *	|--------|---------------------------------------|
 *
 *	If no typeflag 'x' header name string was specified on the command line,
 *	the default value is:
 *		%d/PaxHeaders.%p/%f
 *
 * The following character stubstitutions are made for a typeflag 'g' header
 * (-o exthdr.name=string was specified on the command line):
 *	  String
 *	 Includes           Replaced By
 *	|--------|------------------------------------------------------|
 *	|   %n   | Sequence number (integer) of ext hdr in the archive, |
 *	|        | starting at 1.                                       |
 *	|--------|------------------------------------------------------|
 *	|   %p   | The process ID of the pax process.                   |
 *	|--------|------------------------------------------------------|
 *	|   %%   | A percent '%' character.                             |
 *	|--------|------------------------------------------------------|
 *
 *	If no typeflag 'g' header name string was specified on the command line,
 *	the default value is:
 *		$TMPDIR/GlobalHead.%p.%n
 *	where $TMPDIR is the value of the TMPDIR environment variable.  If
 *	TMPDIR is not set, /tmp is used instead.
 *
 * Note: Any "%" characters which do not occur with the above substitution
 * strings are ignored.  For example, "%s" would be processed as "s".
 *
 *
 * Input:
 *	string		- string of characters to be expanded
 *	xhdrtype	- typeflag (either 'g' or 'x') of the extended header
 *	dname		- directory name of the file
 *	bname		- file name of the file
 *
 * Output:
 *	extended header name
 */
char *
get_exthdrname(const char *string, int xhdrtype,
    const char *dname, const char *bname, char *xtar_hdr)
{
	char	*tmpptr;
	char	*newstr = NULL;
	char	*tmpdirp;
	int	bsize;
	int	dsize;
	int	gsize;
	int	osize;
	int	tsize;
	int	psize;
	int	spaceneeded;


	/* No string to expand, need to use a default exthdrname */
	psize = strlen(pidchars);
	if (string == NULL) {

		/* get the default extended header name */
		switch (xhdrtype) {
		case GXHDRTYPE:		/* $TMPDIR/GlobalHead.%p.%n */
			if ((tmpdirp = getenv("TMPDIR")) == (char *)NULL) {
				STRDUP(tmpdirp, "/tmp");
			}

			/*
			 * Space needed for the extended header name
			 * includes 13 chars for "/GlobalHead." and ".".
			 */
			tsize = strlen(tmpdirp);
			osize = 13;
			(void) memset(gseqnum, '\0', INT_MAX_DIGITS + 1);
			(void) sprintf(gseqnum, "%d", thisgseqnum);
			gsize = strlen(gseqnum);
			spaceneeded = tsize + psize + gsize +
			    osize + 1;
			if ((newstr = calloc(spaceneeded, sizeof (char)))
			    == NULL) {
				fatal(gettext("out of memory"));
			}
			(void) strncpy(newstr, tmpdirp, tsize + 1);
			(void) strncat(newstr, "/GlobalHead.", osize - 1);
			(void) strncat(newstr, pidchars, psize);
			(void) strncat(newstr, ".", 1);
			(void) strncat(newstr, gseqnum, gsize);
			free(tmpdirp);
			break;

		case XXHDRTYPE:		/* %d/PaxHeaders.%p/%f */
			/*
			 * Space needed for the extended header name
			 * includes 13 chars for "/PaxHeaders." and "/".
			 */
			dsize = strlen(dname);
			bsize = strlen(bname);
			osize = 13;
			spaceneeded = dsize + bsize + psize + osize + 1;
			if ((newstr = calloc(spaceneeded, sizeof (char)))
			    == NULL) {
				fatal(gettext("out of memory"));
			}
			(void) strncpy(newstr, dname, dsize + 1);
			(void) strncat(newstr, "/PaxHeaders.", osize - 1);
			(void) strncat(newstr, pidchars, psize);
			(void) strncat(newstr, "/", 1);
			(void) strncat(newstr, bname, bsize);
			break;
		default:
			break;
		}

	} else if (strpbrk(string, "%") == NULL) {
		/*
		 * Easy case.  No possible substitutions in the
		 * specified string.
		 */
		STRDUP(newstr, string);

	} else {
		int	nsize = PATH_MAX + 1;
		int	i;
		int	index = 0;

		/* String specified on command line, expand if necessary. */
		if ((newstr = calloc(nsize, sizeof (char))) == NULL) {
			fatal(gettext(
			    "cannot allocate extended "
			    "header name"));
		}
		/*
		 * Step through the string making needed character
		 * substitutions.  Bail when we've reached then
		 * end of the string specified on the command line
		 * or if we've overrun the newstr buffer.
		 */
		dsize = strlen(dname);
		bsize = strlen(bname);
		(void) memset(gseqnum, '\0', INT_MAX_DIGITS + 1);
		(void) sprintf(gseqnum, "%d", thisgseqnum);
		gsize = strlen(gseqnum);
		STRDUP(tmpptr, string);
		for (i = 0; tmpptr[i] != '\0'; i++) {
			switch (tmpptr[i]) {
			case '%':	/* possible character substitution */
				if (tmpptr[i + 1] != '\0') {
					i++;
					switch (tmpptr[i]) {
					case '%':	/* true '%' character */
						ADDCHAR(newstr, tmpptr[i],
						    index, nsize);
						break;
					case 'p':	/* pax process id */
						STRCAT(newstr, pidchars,
						    index, nsize, psize);
						break;
					case 'n':	/* global ext hdr sub */
						if (xhdrtype == GXHDRTYPE) {
							STRCAT(newstr, gseqnum,
							    index, nsize,
							    gsize);
						} else {
							ADDCHAR(newstr,
							    tmpptr[i], index,
							    nsize);
						}
						break;
					case 'd':	/* directory name */
						if (xhdrtype == XXHDRTYPE) {
							STRCAT(newstr, dname,
							    index, nsize,
							    dsize);
						} else {
							ADDCHAR(newstr,
							    tmpptr[i], index,
							    nsize);
						}
						break;
					case 'f':	/* basename */
						if (xhdrtype == XXHDRTYPE) {
							STRCAT(newstr, bname,
							    index, nsize,
							    bsize);
						} else {
							ADDCHAR(newstr,
							    tmpptr[i], index,
							    nsize);
						}
						break;
					default:
						ADDCHAR(newstr, tmpptr[i],
						    index, nsize);
						break;
					}
				}
				/* else ignore single '%' */
				break;
			default:
				ADDCHAR(newstr, tmpptr[i], index, nsize);
			}
		}
		free(tmpptr);
	}

	/*
	 * Check the expanded pathname to make sure it
	 * fits in the extended header block's name and
	 * prefix fields.  Add it the info to the header
	 * block if it fits.
	 */
	return (split_pathname(newstr, xtar_hdr));
}


/*
 *
 * Extend attribute Format
 *
 * Extended attributes are stored in two pieces.
 * 1. An attribute header which has information about
 *    what file the attribute is for and what the attribute
 *    is named.
 * 2. The attribute record itself.  Stored as a normal file type
 *    of entry.
 * Both the header and attribute record have special modes/typeflags
 * associated with them.
 *
 * The names of the header in the archive look like:
 * /dev/null/attr.hdr
 *
 * The name of the attribute looks like:
 * /dev/null/attr
 *
 * This is done so that an archiver that doesn't understand these formats
 * can just dispose of the attribute records unless the user chooses to
 * rename them via cpio -r or pax -i
 *
 * The format is composed of a fixed size header followed
 * by a variable sized xattr_buf. If the attribute is a hard link
 * to another attribute then another xattr_buf section is included
 * for the link.
 *
 * The xattr_buf is used to define the necessary "pathing" steps
 * to get to the extended attribute.  This is necessary to support
 * a fully recursive attribute model where an attribute may itself
 * have an attribute.
 *
 * The basic layout looks like this:
 *
 *     --------------------------------
 *     |                              |
 *     |         xattr_hdr            |
 *     |                              |
 *     --------------------------------
 *     --------------------------------
 *     |                              |
 *     |        xattr_buf             |
 *     |                              |
 *     --------------------------------
 *     --------------------------------
 *     |                              |
 *     |      (optional link info)    |
 *     |                              |
 *     --------------------------------
 *     --------------------------------
 *     |                              |
 *     |      attribute itself        |
 *     |      stored as normal tar    |
 *     |      or cpio data with       |
 *     |      special mode or         |
 *     |      typeflag                |
 *     |                              |
 *     --------------------------------
 *
 */

/*
 * create_archive - create archive.
 *
 * DESCRIPTION
 *
 *	Create_archive is used as an entry point to both create and append
 *	archives.  Create archive goes through the files specified by the
 *	user and writes each one to the archive if it can.  Create_archive
 *	knows how to write both cpio and tar headers and the padding which
 *	is needed for each type of archive.
 *
 * RETURNS
 *
 *	Always returns 0
 */
void
create_archive(void)
{
	char	*name;
	Stat	sb;
	int	status;
	char	*tmpdirp;
	pid_t	thispid;
	size_t	namesz = PATH_MAX + 1;

	if ((name = malloc(namesz)) == NULL) {
		fatal(gettext("cannot allocate file name buffer"));
	}
	if (f_pax) {
		if ((xrec_ptr = calloc(xrec_size, sizeof (char))) == NULL) {
			fatal(gettext(
			    "cannot allocate extended header buffer"));
		}
		thispid = getpid();
		(void) sprintf(pidchars, "%ld", thispid);
		thispid = strlen(pidchars);
		if (!f_stdpax) {
			if ((tmpdirp = getenv("TMPDIR")) == (char *)NULL) {
				(void) strcpy(xhdr_dirname, "/tmp");
			} else {
				/*
				 * Make sure that dir is no longer than what can
				 * fit in the prefix part of the header.
				 */
				if (strlen(tmpdirp) >
				    (size_t)(TL_PREFIX - thispid - 12)) {
					(void) strcpy(xhdr_dirname, "/tmp");
					(void) warn(gettext(
					    "TMPDIR name too long"),
					    gettext("ignored"));
				} else {
					(void) strcpy(xhdr_dirname, tmpdirp);
				}
			}
			(void) strcat(xhdr_dirname, "/PaxHeaders.");
			(void) strcat(xhdr_dirname, pidchars);
		}
	}

	/*
	 * Dump extended attributes if needed.  We zero sb before
	 * every iteration of the loop to ensure that the extra
	 * fields in the Stat structure aren't bogus.
	 */
	(void) memset(&sb, 0, sizeof (sb));
	while (name_next(&name, &namesz, &sb) != -1) {
		status = archive_file(&name, &namesz, &sb);
#if defined(O_XATTR)
		if ((f_extended_attr || f_sys_attr) && status == 0) {
			dump_xattrs(&name, &namesz, NULL, -1);
		}
#endif
		(void) memset(&sb, 0, sizeof (sb));
	}

	write_eot();
	close_archive();
}


/*
 *  write_exthdr()
 *
 * Write an extended header block.  write_exthdr() creates a ustar header
 * block with the input typeflag value.  If command line options (-o options)
 * were used in the pax interchange format, then these options are included: one
 * extended header record for each keyword specified for the input typeflag.
 *
 * The basic layout looks like this:
 *
 *     ----------------------------------	Contains either global ('g'),
 *     |                                |	ext ('x') header when -x pax
 *     | ustar Header [typeflag=[x|g|X] |	was specified on the cmd line,
 *     |                                |	or ext ('X') hdr when -x xustar
 *     ----------------------------------	was specified on the cmd line.
 *     ----------------------------------	Contains values that are
 *     |      Extended Header Data      |	inappropriate for the ustar
 *     ----------------------------------	header block, including values
 *						specified on the cmd line.
 *
 * In the case where we are trying to write a typeflag 'g' or a typeflag 'x'
 * extended header, and we are unable to get a valid extended header
 * name (i.e., it is too long, or some other error occurred during
 * extended header name expansion), then an error message is written, and
 * if -o invalid=rename was specified on the command line, we will prompt
 * the user for another extended header name, otherwise we will set the
 * exit status and return (no extended header will be written and the
 * following file(s) will be skipped).
 *
 * Input:
 *	xhdrtype	- typeflag (when the interchange format is pax
 *			  (i.e., -x pax, then typeflag can be either
 *			  'g' or 'x', and when the interchange format is
 *			  xustar (i.e., -x xustar), then typeflag is 'X'.
 *	namep		- name of file to archive
 *	asb		- file info to archive
 *	hdr		- the ustar header data for the file
 *
 * Ouput:
 *	0		- Successful
 *	1		- Error
 *
 */
static int
write_exthdr(int xhdrtype, char *namep, char *filename, Stat *asb, char *hdr)
{
	int	sum;
	int	i;
	int	gotit = 0;
	char	*p;
	char	xtar_hdr[BLOCKSIZE];
	char	*longname;
	char	*tname;
	char	*bname;
	char	*xhdname;

	(void) bcopy(hdr, xtar_hdr, BLOCKSIZE);
	(void) memset(&xtar_hdr[TO_NAME], '\0', TL_NAME);
	(void) sprintf(&xtar_hdr[TO_NAME], "%llu", xhdr_count + 1);
	(void) memset(&xtar_hdr[TO_LINKNAME], '\0', TL_LINKNAME);
	(void) memset(&xtar_hdr[TO_PREFIX], '\0', TL_PREFIX);

	/* add the directory name entry */
	switch (xhdrtype) {
	case GXHDRTYPE:
		STRDUP(tname, namep);
		bname = s_calloc(TL_NAME + 1);
		(void) strncpy(bname, &hdr[TO_NAME], TL_NAME);
		xhdname = NULL;
		while (!gotit) {
			if ((xhdname = get_exthdrname(gexthdrnameopt,
			    xhdrtype, dirname(tname), basename(bname),
			    xtar_hdr)) != NULL) {
				/*
				 * Add the ext hdr dir name to the
				 * extended header.
				 */
				(void) strncpy(&xtar_hdr[TO_NAME], xhdname,
				    MIN(strlen(xhdname), TL_NAME));
				gotit = 1;

			} else {
				size_t	ghdrnsize;
				diag(gettext(
				    "Global extended header name "
				    "%s : %s\n"),
				    gexthdrnameopt ? gexthdrnameopt :
				    GEHDRNAME, strerror(errno));
				switch (invalidopt) {
				case INV_RENAME:
					/*
					 * Prompt the user for another
					 * extended header name.
					 */
					free(xhdname);
					xhdname = NULL;
					rename_interact = 1;
					ghdrnsize = strlen(xhdname) + 1;
					if (get_newname(&gexthdrnameopt,
					    &ghdrnsize, asb) != 0) {
						free(xhdname);
						xhdname = NULL;
					}
					break;
				case INV_BYPASS:
					/* Doesn't affect exit status */
					gotit = 1;
					break;
				default:
					exit_status = 1;
					gotit = 1;
					break;
				}
			}
		}
		free(tname);
		free(bname);
		if (xhdname == NULL) {
			return (1);
		} else {
			free(xhdname);
		}
		break;

	case XXHDRTYPE:
		STRDUP(tname, namep);
		bname = s_calloc(TL_NAME + 1);
		(void) strncpy(bname, &hdr[TO_NAME], TL_NAME);
		xhdname = NULL;
		while (!gotit) {
			if ((xhdname = get_exthdrname(exthdrnameopt,
			    xhdrtype, dirname(tname), basename(bname),
			    xtar_hdr)) != NULL) {
				/*
				 * Add the ext hdr dir name to the
				 * extended header.
				 */
				(void) strncpy(&xtar_hdr[TO_NAME], xhdname,
				    MIN(strlen(xhdname), TL_NAME));
				gotit = 1;

			} else {
				size_t	ehdrnsize;
				diag(gettext(
				    "Extended header name "
				    "%s : %s\n"),
				    exthdrnameopt ? exthdrnameopt :
				    EHDRNAME, strerror(errno));
				switch (invalidopt) {
				case INV_RENAME:
					/*
					 * Prompt the user for another
					 * extended header name.
					 */
					free(xhdname);
					xhdname = NULL;
					rename_interact = 1;
					if (get_newname(&exthdrnameopt,
					    &ehdrnsize, asb) != 0) {
						free(exthdrnameopt);
						exthdrnameopt = NULL;
					}
					break;
				case INV_BYPASS:
					/* Doesn't affect exit status */
					gotit = 1;
					break;
				default:
					exit_status = 1;
					gotit = 1;
					break;
				}
			}
		}
		free(tname);
		free(bname);
		if (xhdname == NULL) {
			return (1);
		} else {
			free(xhdname);
		}
		break;
	default:
		(void) strncpy(&xtar_hdr[TO_PREFIX], xhdr_dirname,
		    MIN(strlen(xhdr_dirname), TL_PREFIX));
		break;
	}
	xhdr_count++;
	xrec_offset = 0;

	/*
	 * Add time entries using cmd line overrides if they exist.  If
	 * we are processing an XXHDRTYPE then entries could have been
	 * made in Xtarhdr in the calling code, so make sure to include
	 * these if they haven't been overridden on the command line.
	 */
	switch (xhdrtype) {
	case (XXHDRTYPE):
		if (oxxhdr_flgs & _X_MTIME) {
			x_gen_date(xhdrtype, "mtime", coXtarhdr.x_mtime);
		} else if (xhdr_flgs & _X_MTIME) {
			gen_date("mtime", Xtarhdr.x_mtime);
		} else {
			/* always include mtime */
			gen_date("mtime", asb->sb_stat.st_mtim);
		}
		if (oxxhdr_flgs & _X_ATIME) {
			x_gen_date(xhdrtype, "atime", coXtarhdr.x_atime);
		} else if (xhdr_flgs & _X_ATIME) {
			gen_date("atime", Xtarhdr.x_atime);
		} else if (f_times) {
			gen_date("atime", asb->sb_stat.st_atim);
		}

		if (oxxhdr_flgs & _X_HOLESDATA) {
			gen_string("SUN.holesdata", coXtarhdr.x_holesdata);
		} else if (xhdr_flgs & _X_HOLESDATA) {
			gen_string("SUN.holesdata", Xtarhdr.x_holesdata);
		}
		break;
	case (GXHDRTYPE):
		if (ogxhdr_flgs & _X_MTIME) {
			x_gen_date(xhdrtype, "mtime", coXtarhdr.gx_mtime);
		} else {
			/* always include mtime */
			gen_date("mtime", asb->sb_stat.st_mtim);
		}
		if (ogxhdr_flgs & _X_ATIME) {
			x_gen_date(xhdrtype, "atime", coXtarhdr.gx_atime);
		} else if (f_times) {
			gen_date("atime", asb->sb_stat.st_atim);
		}
		if (ogxhdr_flgs & _X_HOLESDATA) {
			gen_string("SUN.holesdata", coXtarhdr.gx_holesdata);
		}
		break;
	case (XHDRTYPE):
		/* always include mtime */
		gen_date("mtime", asb->sb_stat.st_mtim);
		if (f_times) {
			gen_date("atime", asb->sb_stat.st_atim);
		}
		if (xhdr_flgs & _X_HOLESDATA) {
			gen_string("SUN.holesdata", Xtarhdr.x_holesdata);
		}
		break;
	}

	xtar_hdr[TO_TYPEFLG] = xhdrtype;

	switch (xhdrtype) {
	case (XXHDRTYPE):
		if (oxxhdr_flgs & _X_PATH) {
			longname = coXtarhdr.x_path;
		} else if (xhdr_flgs & _X_PATH) {
			longname = Xtarhdr.x_path;
		} else {
			longname = filename;
		}
		break;
	case (GXHDRTYPE):
		if (ogxhdr_flgs & _X_PATH) {
			longname = coXtarhdr.gx_path;
		} else {
			longname = filename;
		}
		break;
	case (XHDRTYPE):
		if (xhdr_flgs & _X_PATH) {
			longname = Xtarhdr.x_path;
		} else {
			longname = filename;
		}
		break;
	}
	if (gen_utf8_names(xhdrtype, longname, asb, hdr) != 0) {
		exit_status = 1;
		/* reinit just in case there is another xhdr record */
		(void) memset(xrec_ptr, '\0', xrec_offset);
		xhdr_count--;
		return (1);
	}
#ifdef XHDR_DEBUG
	Xtarhdr.x_uname = &hdr[TO_UNAME];
	Xtarhdr.x_gname = &hdr[TO_GNAME];
	xhdr_flgs |= (_X_UNAME | _X_GNAME);
#endif

	/* add various other fields */
	switch (xhdrtype) {
	case (XXHDRTYPE):
		if (oxxhdr_flgs || xhdr_flgs) {
			if (oxxhdr_flgs & _X_DEVMAJOR) {
				gen_num("SUN.devmajor", coXtarhdr.x_devmajor);
			} else if (xhdr_flgs & _X_DEVMAJOR) {
				gen_num("SUN.devmajor", Xtarhdr.x_devmajor);
			}
			if (oxxhdr_flgs & _X_DEVMINOR) {
				gen_num("SUN.devminor", coXtarhdr.x_devminor);
			} else if (xhdr_flgs & _X_DEVMINOR) {
				gen_num("SUN.devminor", Xtarhdr.x_devminor);
			}
			if (oxxhdr_flgs & _X_GID) {
				gen_num("gid", coXtarhdr.x_gid);
			} else if (xhdr_flgs & _X_GID) {
				gen_num("gid", Xtarhdr.x_gid);
			}
			if (oxxhdr_flgs & _X_UID) {
				gen_num("uid", coXtarhdr.x_uid);
			} else if (xhdr_flgs & _X_UID) {
				gen_num("uid", Xtarhdr.x_uid);
			}
			if (oxxhdr_flgs & _X_SIZE) {
				gen_num("size", coXtarhdr.x_filesz);
			} else if (xhdr_flgs & _X_SIZE) {
				gen_num("size", Xtarhdr.x_filesz);
			}
			if (oxxhdr_flgs & _X_PATH) {
				gen_string("path", coXtarhdr.x_path);
			} else if (xhdr_flgs & _X_PATH) {
				gen_string("path", Xtarhdr.x_path);
			}
			if (oxxhdr_flgs & _X_LINKPATH) {
				gen_string("linkpath", coXtarhdr.x_linkpath);
			} else if (xhdr_flgs & _X_LINKPATH) {
				gen_string("linkpath", Xtarhdr.x_linkpath);
			}
			if (oxxhdr_flgs & _X_GNAME) {
				gen_string("gname", coXtarhdr.x_gname);
			} else if (xhdr_flgs & _X_GNAME) {
				gen_string("gname", Xtarhdr.x_gname);
			}
			if (oxxhdr_flgs & _X_UNAME) {
				gen_string("uname", coXtarhdr.x_uname);
			} else if (xhdr_flgs & _X_UNAME) {
				gen_string("uname", Xtarhdr.x_uname);
			}
			if (oxxhdr_flgs & _X_CHARSET) {
				gen_string("charset", coXtarhdr.x_charset);
			} else if (xhdr_flgs & _X_CHARSET) {
				gen_string("charset", Xtarhdr.x_charset);
			}
			if (oxxhdr_flgs & _X_COMMENT) {
				gen_string("comment", coXtarhdr.x_comment);
			} else if (xhdr_flgs & _X_COMMENT) {
				gen_string("comment", Xtarhdr.x_comment);
			}
			if (oxxhdr_flgs & _X_REALTIME) {
				gen_string("realtime.", coXtarhdr.x_realtime);
			} else if (xhdr_flgs & _X_REALTIME) {
				gen_string("realtime.", Xtarhdr.x_realtime);
			}
			if (oxxhdr_flgs & _X_SECURITY) {
				gen_string("realtime.", coXtarhdr.x_realtime);
			} else if (xhdr_flgs & _X_SECURITY) {
				gen_string("realtime.", Xtarhdr.x_realtime);
			}
		}

		break;
	case (GXHDRTYPE):
		if (ogxhdr_flgs) {
			if (ogxhdr_flgs & _X_DEVMAJOR) {
				gen_num("SUN.devmajor", coXtarhdr.gx_devmajor);
			}
			if (ogxhdr_flgs & _X_DEVMINOR) {
				gen_num("SUN.devminor", coXtarhdr.gx_devminor);
			}
			if (ogxhdr_flgs & _X_GID) {
				gen_num("gid", coXtarhdr.gx_gid);
			}
			if (ogxhdr_flgs & _X_UID) {
				gen_num("uid", coXtarhdr.gx_uid);
			}
			if (ogxhdr_flgs & _X_SIZE) {
				gen_num("size", coXtarhdr.gx_filesz);
			}
			if (ogxhdr_flgs & _X_PATH) {
				gen_string("path", coXtarhdr.gx_path);
			}
			if (ogxhdr_flgs & _X_LINKPATH) {
				gen_string("linkpath", coXtarhdr.gx_linkpath);
			}
			if (ogxhdr_flgs & _X_GNAME) {
				gen_string("gname", coXtarhdr.gx_gname);
			}
			if (ogxhdr_flgs & _X_UNAME) {
				gen_string("uname", coXtarhdr.gx_uname);
			}
			if (ogxhdr_flgs & _X_CHARSET) {
				gen_string("charset", coXtarhdr.gx_charset);
			}
			if (ogxhdr_flgs & _X_COMMENT) {
				gen_string("comment", coXtarhdr.gx_comment);
			}
			if (ogxhdr_flgs & _X_REALTIME) {
				gen_string("realtime.", coXtarhdr.gx_realtime);
			}
			if (ogxhdr_flgs & _X_SECURITY) {
				gen_string("security.", coXtarhdr.gx_security);
			}
		}
		break;
	case (XHDRTYPE):
		if (xhdr_flgs) {
			if (xhdr_flgs & _X_DEVMAJOR) {
				gen_num("SUN.devmajor", Xtarhdr.x_devmajor);
			}
			if (xhdr_flgs & _X_DEVMINOR) {
				gen_num("SUN.devminor", Xtarhdr.x_devminor);
			}
			if (xhdr_flgs & _X_GID) {
				gen_num("gid", Xtarhdr.x_gid);
			}
			if (xhdr_flgs & _X_UID) {
				gen_num("uid", Xtarhdr.x_uid);
			}
			if (xhdr_flgs & _X_SIZE) {
				gen_num("size", Xtarhdr.x_filesz);
			}
			if (xhdr_flgs & _X_PATH) {
				gen_string("path", Xtarhdr.x_path);
			}
			if (xhdr_flgs & _X_LINKPATH) {
				gen_string("linkpath", Xtarhdr.x_linkpath);
			}
			if (xhdr_flgs & _X_GNAME) {
				gen_string("gname", Xtarhdr.x_gname);
			}
			if (xhdr_flgs & _X_UNAME) {
				gen_string("uname", Xtarhdr.x_uname);
			}
		}
		break;
	}
	(void) sprintf(&xtar_hdr[TO_SIZE], "%011llo", (OFFSET)xrec_offset);
	/* Calculate the checksum */
	sum = 0;
	p = xtar_hdr;
	for (i = 0; i < TL_TOTAL_TAR; i++) {
		sum += 0xFF & *p++;
	}
	/* Fill in the checksum field. */
	(void) sprintf(&xtar_hdr[TO_CHKSUM], "%07o", sum);
	outwrite(xtar_hdr, (OFFSET) BLOCKSIZE);

	i = xrec_offset % BLOCKSIZE;
	if (i > 0)
		xrec_offset += BLOCKSIZE - i;
	outwrite(xrec_ptr, (OFFSET) xrec_offset);

	/* reinit just in case there is another xhdr record */
	(void) memset(xrec_ptr, '\0', xrec_size);
	return (0);
}


static void
p_alloc(int flg)
{
	switch (flg) {
	case LOCAL:
		if (local_path == NULL) {
			if ((local_path = calloc(UTF8PATHSIZE,
			    sizeof (char))) == NULL) {
				fatal(strerror(errno));
			}
		}
		if (local_linkpath == NULL) {
			if ((local_linkpath = calloc(UTF8PATHSIZE,
			    sizeof (char))) == NULL) {
				fatal(strerror(errno));
			}
		}
		break;
	case XLOCAL:
		if (xlocal_path == NULL) {
			if ((xlocal_path = calloc(UTF8PATHSIZE,
			    sizeof (char))) == NULL) {
				fatal(strerror(errno));
			}
		}
		if (xlocal_linkpath == NULL) {
			if ((xlocal_linkpath = calloc(UTF8PATHSIZE,
			    sizeof (char))) == NULL) {
				fatal(strerror(errno));
			}
		}
		break;
	case GLOCAL:
		if (glocal_path == NULL) {
			if ((glocal_path = calloc(UTF8PATHSIZE,
			    sizeof (char))) == NULL) {
				fatal(strerror(errno));
			}
		}
		if (glocal_linkpath == NULL) {
			if ((glocal_linkpath = calloc(UTF8PATHSIZE,
			    sizeof (char))) == NULL) {
				fatal(strerror(errno));
			}
		}
		break;
	}
}

/*
 * writetar - write a header block for a tar file
 *
 * DESCRIPTION
 *
 * 	Make a header block for the file name whose stat info is in st.
 *
 * 	The tar header block is structured as follows:
 *
 *		FIELD NAME	OFFSET		SIZE
 *      	-------------|---------------|------
 *		name		  0		100
 *		mode		100		  8
 *		uid		108		  8
 *		gid		116		  8
 *		size		124		 12
 *		mtime		136		 12
 *		chksum		148		  8
 *		typeflag	156		  1
 *		linkname	157		100
 *		magic		257		  6
 *		version		263		  2
 *		uname		265		 32
 *		gname		297		 32
 *		devmajor	329		  8
 *		devminor	337		  8
 *		prefix		345		155
 *
 * PARAMETERS
 *
 *	char	*name	- name of file to create a header block for
 *	Stat	*asb	- pointer to the stat structure for the named file
 *
 *
 * RETURN
 *
 *	0 - ok
 *	1 - error - skip file
 *	2 - Archived as link
 */


static int
writetar(char *name, Stat *asb)
{
	char		*p;
	char		*filename;
	char		substitute_name[TL_NAME + 1];
	char		prefix[TL_PREFIX + 2];
	int		i;
	int		sum;
	char		hdr[BLOCKSIZE];
	char		attrhdr[BLOCKSIZE];
	Link		*from = NULL;
	uid_t		uid;
	int		split;
	int		namelen, prefixlen;
	char		*lastslash;
	major_t		dev = 0;
	char		*attrbasefilep;
	char		*attrnamep;
	char		*namep = name;
	char		*attrhdrname;
	char		*attrbuf = NULL;
	char		realtypeflag;
	char		*linknamep;
	int		attrlen;


#if defined(O_XATTR)
	if (asb->xattr_info.xattraname != NULL) {
		size_t	len;
		attrbasefilep = asb->xattr_info.xattrfname;
		attrnamep = asb->xattr_info.xattraname;
		len = strlen(DEVNULL) + strlen(attrnamep) + 2;
		if ((namep = malloc(len)) == NULL) {
			fatal(gettext("out of memory"));
		}
		(void) snprintf(namep, len, "%s%s%s", DEVNULL,
		    (attrnamep[0] == '/') ? "" : "/", attrnamep);
	}
#endif

	(void) memset(substitute_name, '\0', sizeof (substitute_name));
	(void) memset(prefix, '\0', sizeof (prefix));
	(void) memset(hdr, 0, BLOCKSIZE);

	/*
	 * If the length of the name is greater than TL_NAME + TL_PREFIX
	 * or the name cannot be split into components which fit in the
	 * name and prefix fields of the header record, and we are not
	 * doing extended headers, then print a message and return with
	 * an error.
	 * If we are doing extended headers and the name is longer than
	 * PATH_MAX, then also print the message and return with an error.
	 */

	if ((((split = (int)strlen(namep)) > TL_NAME + TL_PREFIX) &&
	    (f_pax == 0)) || (split > PATH_MAX)) {
		warn(gettext("Name too long"), namep);
		return (1);
	} else if ((split > TL_NAME) || ((split == TL_NAME) && (f_posix == 0) &&
	    S_ISDIR(asb->sb_mode))) {
		/*
		 * The length of the name is greater than TL_NAME, so we must
		 * split the name from the path.
		 * Since path is limited to TL_PREFIX characters, look for the
		 * last slash within PRESIZ + 1 characters only.
		 */
		(void) strncpy(&prefix[0], namep, MIN(split, TL_PREFIX + 1));
		lastslash = strrchr(prefix, '/');
		if (lastslash == NULL) {
			namelen = split;
			prefixlen = 0;
			prefix[0] = '\0';
		} else {
			*lastslash = '\0';
			prefixlen = strlen(prefix);
			namelen = split - prefixlen - 1;
		}
		/*
		 * If the name is greater than TL_NAME, we can't archive
		 * the file unless we are using extended headers.
		 */
		if ((namelen > TL_NAME) || ((namelen == TL_NAME) &&
		    (f_posix == 0) && S_ISDIR(asb->sb_mode))) {
			/*
			 * Determine if there is a name which is <= TL_NAME
			 * chars by looking for the last slash in the name.
			 * If so, then the path may be too long...
			 */
			lastslash = strrchr(namep, '/');
			if (lastslash != NULL)
				namelen = strlen(lastslash + 1);
			if (f_pax) {	/* Doing extended headers */
				xhdr_flgs |= _X_PATH;
				Xtarhdr.x_path = namep;
				if (namelen <= TL_NAME)
					(void) strcpy(substitute_name,
					    lastslash + 1);
				else
					(void) sprintf(substitute_name, "%llu",
					    xhdr_count + 1);
				if (split - namelen - 1 > TL_PREFIX)
					(void) strcpy(prefix, xhdr_dirname);
			} else {
				warn(gettext("Name too long"), namep);
				return (1);
			}
		} else
			(void) strncpy(&substitute_name[0],
			    namep + prefixlen + 1,
			    strlen(namep + prefixlen + 1));
		filename = substitute_name;
	} else {
		filename = namep;
	}


#ifdef S_IFLNK
	if ((asb->sb_mode & S_IFMT) == S_IFLNK) {
		if (strlen(asb->sb_link) > (size_t)TL_LINKNAME) {
			if (f_pax == 0) {
				warn(gettext("Link name too long"),
				    asb->sb_link);
				return (1);
			} else {
				xhdr_flgs |= _X_LINKPATH;
				Xtarhdr.x_linkpath = asb->sb_link;
			}
		}

		if ((xhdr_flgs & _X_LINKPATH) == 0)
			(void) strncpy(&hdr[TO_LINKNAME],
			    asb->sb_link, TL_LINKNAME);
		asb->sb_size = 0;
	}
#endif
	if (f_pax) {
		if ((ulong_t)(uid = asb->sb_uid) > (ulong_t)OCTAL7CHAR) {
			xhdr_flgs |= _X_UID;
			Xtarhdr.x_uid = uid;
		}
		if ((ulong_t)(gid = asb->sb_gid) > (ulong_t)OCTAL7CHAR) {
			xhdr_flgs |= _X_GID;
			Xtarhdr.x_gid = gid;
		}
	}

	if ((ulong_t)(uid = asb->sb_uid) > (ulong_t)OCTAL7CHAR)
		uid = UID_NOBODY;
	if ((ulong_t)(gid = asb->sb_gid) > (ulong_t)OCTAL7CHAR)
		gid = GID_NOBODY;

	(void) strncpy(&hdr[TO_NAME], filename, MIN(strlen(filename), TL_NAME));
	(void) sprintf(&hdr[TO_MODE], "%07lo", asb->sb_mode & ~S_IFMT);
	(void) sprintf(&hdr[TO_UID], "%07o", uid);
	(void) sprintf(&hdr[TO_GID], "%07o", gid);
	if (xhdr_flgs & _X_SIZE)
		(void) sprintf(&hdr[TO_SIZE], "%011llo", (OFFSET) 0);
	else
		(void) sprintf(&hdr[TO_SIZE], "%011llo", (OFFSET) asb->sb_size);
	(void) sprintf(&hdr[TO_MTIME], "%011lo", (long)asb->sb_mtime);
	(void) memset(&hdr[TO_CHKSUM], ' ', 8);

	if (asb->xattr_info.xattraname == NULL) {
		if ((hdr[TO_TYPEFLG] = tartype(asb->sb_mode)) == (char)0) {
			(void) fprintf(stderr,
			    gettext("%s: not a file (mode = %%x%04x): %s\n"),
			    myname, asb->sb_mode & S_IFMT, namep);
			(void) fprintf(stderr, gettext("not dumped\n"));
			exit_status = 1;
			return (1);
		}
	} else {
		realtypeflag = tartype(asb->sb_mode);
		hdr[TO_TYPEFLG] = 'E';
	}

	/*
	 * Use name not namep here since we don't want bogus /dev/null
	 * entry screwing up search
	 */
	if (asb->sb_nlink > 1 && (from = linkfrom(name, asb)) != (Link *)NULL) {
		linknamep = from->l_name;
		if (strlen(linknamep) > TL_LINKNAME) {
			if (f_pax) {
				xhdr_flgs |= _X_LINKPATH;
				Xtarhdr.x_linkpath = linknamep;
			} else {
				warn(gettext("Link name too long"),
				    linknamep);
				return (1);
			}
		} else {
			(void) strncpy(&hdr[TO_LINKNAME],
			    linknamep, TL_LINKNAME);
		}
		if (asb->xattr_info.xattraname == NULL)
			hdr[TO_TYPEFLG] = LNKTYPE;
		realtypeflag = LNKTYPE;
	}
	(void) strcpy(&hdr[TO_MAGIC], TMAGIC);
	(void) strncpy(&hdr[TO_VERSION], TVERSION, TL_VERSION);
	(void) strcpy(&hdr[TO_UNAME], finduname((int)asb->sb_uid));
	(void) strcpy(&hdr[TO_GNAME], findgname((int)asb->sb_gid));
	/*
	 * Save major and minor device number info to header.
	 * Retrieve the major and minor number from st_rdev
	 * only if the file is a char special or block special
	 * file as the st_rdev member of the stat structure is
	 * defined only for char special or block special files.
	 * (see stat(2)).
	 */
	switch (asb->sb_mode & S_IFMT) {
		case S_IFBLK:
		case S_IFCHR:
			if ((dev = major(asb->sb_rdev)) > OCTAL7CHAR) {
				if (f_pax) {
					xhdr_flgs |= _X_DEVMAJOR;
					Xtarhdr.x_devmajor = dev;
				} else {
					warn(gettext(
					    "Device major too large"), namep);
					return (1);
				}
				dev = 0;
			}
			(void) sprintf(&hdr[TO_DEVMAJOR], "%07lo", dev);
			if ((dev = minor(asb->sb_rdev)) > OCTAL7CHAR) {
				if (f_pax) {
					xhdr_flgs |= _X_DEVMINOR;
					Xtarhdr.x_devminor = dev;
				} else {
					warn(gettext(
					    "Device minor too large"), namep);
					return (1);
				}
				dev = 0;
			}
			(void) sprintf(&hdr[TO_DEVMINOR], "%07lo", dev);
			break;
		default:
			(void) sprintf(&hdr[TO_DEVMAJOR], "%07lo", dev);
			(void) sprintf(&hdr[TO_DEVMINOR], "%07lo", dev);
			break;
	}
	(void) strncpy(&hdr[TO_PREFIX], prefix, TL_PREFIX);

	if (f_pax) {
		p_alloc(LOCAL);
		if (f_stdpax) {
			/* write the global extended header once per archive */
			if (thisgseqnum != arvolume) {
				p_alloc(GLOCAL);
				thisgseqnum = arvolume;
				if (goptlist != NULL) {
					if (get_oghdrdata() != 0) {
						return (1);
					}
					if (write_exthdr(GXHDRTYPE, namep,
					    filename, asb, hdr) != 0) {
						return (1);
					}
				}
			}
			if ((xoptlist != NULL) || (f_mtime) || (f_times)) {
				p_alloc(XLOCAL);
				if (get_oxhdrdata() != 0) {
					return (1);
				}
				if (write_exthdr(XXHDRTYPE, namep, filename,
				    asb, hdr) != 0) {
					return (1);
				}
			}
		} else {
			if (write_exthdr(XHDRTYPE, namep, filename,
			    asb, hdr) != 0) {
				return (1);
			}
		}
	}


#if defined(O_XATTR)
	if (asb->xattr_info.xattraname != NULL) {
		size_t	len;
		len = strlen(attrnamep) + strlen(".hdr") +
		    strlen(DEVNULL) + 2;
		if ((attrhdrname = malloc(len)) == (char *)NULL) {
			warn(gettext("Out of memory"), name);
			return (1);
		}
		(void) snprintf(attrhdrname, len, "%s%s%s%s", DEVNULL,
		    (name[0] == '/') ? "" : "/", attrnamep, ".hdr");
		(void) memcpy(attrhdr, hdr, sizeof (hdr));
		(void) strlcpy(&attrhdr[TO_NAME], attrhdrname, TL_NAME);
		prepare_xattr_hdr(&attrbuf, attrbasefilep,
		    asb->xattr_info.xattrapath, realtypeflag, from, &attrlen);
		(void) sprintf(&attrhdr[TO_SIZE], "%011llo", (OFFSET) attrlen);
		sum = 0;
		p = attrhdr;
		for (i = 0; i < TL_TOTAL_TAR; i++) {
			sum += 0xFF & *p++;
		}
		(void) sprintf(&attrhdr[TO_CHKSUM], "%07o", sum);
		outxattrhdr(attrhdr, attrbuf, attrlen);
		if (attrhdrname)
			free(attrhdrname);
		free(namep);
	}
#endif

	/* Calculate the checksum */

	sum = 0;
	p = hdr;
	for (i = 0; i < TL_TOTAL_TAR; i++) {
		sum += 0xFF & *p++;
	}
	/* Fill in the checksum field. */

	(void) sprintf(&hdr[TO_CHKSUM], "%07o", sum);

	outwrite(hdr, (OFFSET) BLOCKSIZE);

	return (0);
}

#if defined(O_XATTR)
#define	TBLOCK	512
#define	ROUNDTOTBLOCK(a)	((a + (TBLOCK -1)) & ~(TBLOCK -1))

static void
prepare_xattr_hdr(
	char		**attrbuf,
	char		*filename,
	char		*attrpath,
	char		typeflag,
	Link		*linkinfo,
	int		*rlen)
{
	char			*bufhead;	/* ptr to full buffer */
	char			*aptr;
	struct xattr_hdr 	*hptr;		/* ptr to header in bufhead */
	struct xattr_buf	*tptr;		/* ptr to pathing pieces */
	int			totalen;	/* total buffer length */
	int			len;		/* length returned to user */
	int			stringlen;	/* length of filename + attr */
						/*
						 * length of filename + attr
						 * in link section
						 */
	int			linkstringlen;
	int			complen;	/* length of pathing section */
	int			linklen;	/* length of link section */
	int			attrnames_index; /* attrnames starting index */

	/*
	 * Release previous buffer?
	 */

	if (*attrbuf != (char *)NULL) {
		free(*attrbuf);
		*attrbuf = NULL;
	}

	/*
	 * First add in fixed size stuff
	 */
	len = sizeof (struct xattr_hdr) + sizeof (struct xattr_buf);

	/*
	 * Add space for two nulls
	 */
	stringlen = strlen(attrpath) + strlen(filename) + 2;
	complen = stringlen + sizeof (struct xattr_buf);

	len += stringlen;

	/*
	 * Now add on space for link info if any
	 */

	if (linkinfo != (Link *)NULL) {
		/*
		 * Again add space for two nulls
		 */
		linkstringlen = strlen(linkinfo->l_name) +
		    strlen(linkinfo->l_attr) + 2;
		linklen = linkstringlen + sizeof (struct xattr_buf);
		len += linklen;
	} else {
		linklen = 0;
	}

	/*
	 * Now add padding to end to fill out TBLOCK
	 *
	 * Function returns size of real data and not size + padding.
	 */

	totalen = ROUNDTOTBLOCK(len);

	if ((bufhead = calloc(1, totalen)) == (char *)NULL) {
		fatal(gettext("out of memory"));
	}


	/*
	 * Now we can fill in the necessary pieces
	 */

	/*
	 * first fill in the fixed header
	 */
	hptr = (struct xattr_hdr *)bufhead;
	(void) strncpy(hptr->h_version, XATTR_ARCH_VERS, XATTR_HDR_VERS);
	(void) sprintf(hptr->h_component_len, "%0*d",
	    sizeof (hptr->h_component_len) - 1, complen);
	(void) sprintf(hptr->h_link_component_len, "%0*d",
	    sizeof (hptr->h_link_component_len) - 1, linklen);
	(void) sprintf(hptr->h_size, "%0*d", sizeof (hptr->h_size) - 1, len);

	/*
	 * Now fill in the filename + attrnames section
	 * The filename and attrnames section can be composed of two or more
	 * path segments separated by a null character.  The first segment
	 * is the path to the parent file that roots the entire sequence in
	 * the normal name space. The remaining segments describes a path
	 * rooted at the hidden extended attribute directory of the leaf file of
	 * the previous segment, making it possible to name attributes on
	 * attributes.  Thus, if we are just archiving an extended attribute,
	 * the second segment will contain the attribute name.  If we are
	 * archiving a system attribute of an extended attribute, then the
	 * second segment will contain the attribute name, and a third segment
	 * will contain the system attribute name.  The attribute pathing
	 * information is obtained from 'attrpath'.
	 */

	tptr = (struct xattr_buf *)(bufhead + sizeof (struct xattr_hdr));
	(void) sprintf(tptr->h_namesz, "%0*d", sizeof (tptr->h_namesz) - 1,
	    stringlen);
	(void) strcpy(tptr->h_names, filename);
	attrnames_index = strlen(filename) + 1;
	(void) strcpy(&tptr->h_names[attrnames_index], attrpath);
	tptr->h_typeflag = typeflag;

	/*
	 * Split the attrnames section into two segments if 'attrpath'
	 * contains pathing information for a system attribute of an
	 * extended attribute.  We split them by replacing the '/' with
	 * a '\0'.
	 */
	if ((aptr = strpbrk(&tptr->h_names[attrnames_index], "/")) != NULL) {
		*aptr = '\0';
	}

	/*
	 * Now fill in the optional link section if we have one
	 */

	if (linkinfo != (Link *)NULL) {
		tptr = (struct xattr_buf *)(bufhead +
		    sizeof (struct xattr_hdr) + complen);

		(void) sprintf(tptr->h_namesz, "%0*d",
		    sizeof (tptr->h_namesz) - 1, linkstringlen);
		(void) strcpy(tptr->h_names, linkinfo->l_name);
		(void) strcpy(
		    &tptr->h_names[strlen(linkinfo->l_name) + 1],
		    linkinfo->l_attr);
		tptr->h_typeflag = typeflag;
	}
	*attrbuf = (char *)bufhead;
	*rlen = len;
}
#else
static void
prepare_xattr_hdr(
	char		**attrbuf,
	char		*filename,
	char		*attrpath,
	char		typeflag,
	Link		*linkinfo,
	int		*rlen)
{
	*attrbuf = NULL;
	*rlen = 0;
}

#endif

/*
 * tartype - return tar file type from file mode
 *
 * DESCRIPTION
 *
 *	tartype returns the character which represents the type of file
 *	indicated by "mode". A binary 0 indicates that an unrecognizable
 *	file type was detected.
 *
 * PARAMETERS
 *
 *	mode_t	mode	- file mode from a stat block
 *
 * RETURNS
 *
 *	The character which represents the particular file type in the
 *	ustar standard headers. Binary 0 if there was a problem.
 */


static char
tartype(mode_t mode)
{
	switch (mode & S_IFMT) {

#ifdef S_IFCTG
	case S_IFCTG:
		return (CONTTYPE);
#endif

	case S_IFDIR:
		return (DIRTYPE);

#ifdef S_IFLNK
	case S_IFLNK:
		return (SYMTYPE);
#endif

#ifdef S_IFIFO
	case S_IFIFO:
		return (FIFOTYPE);
#endif

#ifdef S_IFCHR
	case S_IFCHR:
		return (CHRTYPE);
#endif

#ifdef S_IFBLK
	case S_IFBLK:
		return (BLKTYPE);
#endif

#ifdef S_IFREG
	case S_IFREG:
		return (REGTYPE);
#endif

	default:
		return ((char)0);
	}
}


/*
 * writecpio - write a cpio archive header
 *
 * DESCRIPTION
 *
 *	Writes a new CPIO style archive header for the file specified.
 *
 * PARAMETERS
 *
 *	char	*name	- name of file to create a header block for
 *	Stat	*asb	- pointer to the stat structure for the named file
 */


static int
writecpio(char *name, Stat *asb)
{
	uint_t	namelen, namelen2;
	char	header[M_STRLEN + H_STRLEN + 1];
	char	attrheader[M_STRLEN + H_STRLEN + 1];
	mode_t	mode;
	uid_t	uid;
	gid_t	gid;
	char	*attrhdrname, *attrname;
	char	*namep = name;
	char	*attrbuf = NULL;
	int	attrlen;


	if (asb->xattr_info.xattraname != NULL) {
		size_t	len;
		mode = (asb->sb_mode & S_IAMB) | (mode_t)_XATTR_CPIO_MODE;
		len = strlen(DEVNULL) + strlen(asb->xattr_info.xattraname) + 2;
		attrname = malloc(len);
		(void) snprintf(attrname, len, "%s%s",
		    DEVNULL, asb->xattr_info.xattraname);
		len = strlen(attrname) + strlen(".hdr") + 1;
		attrhdrname = malloc(len);
		(void) snprintf(attrhdrname, len, "%s.hdr", attrname);
		namep = attrname;
	} else {
		mode = asb->sb_mode;
	}

	namelen = (uint_t)strlen(namep) + 1;
	(void) strcpy(header, M_ASCII);
	if ((ulong_t)(uid = asb->sb_uid) > (ulong_t)0777777) {
		warn(gettext("uid too large for cpio archive format"), namep);
		uid = UID_NOBODY;
		if (S_ISREG(mode))
			mode &= ~S_ISUID;
	}
	if ((ulong_t)(gid = asb->sb_gid) > (ulong_t)0777777) {
		warn(gettext("gid too large for cpio archive format"), namep);
		gid = GID_NOBODY;
		if (S_ISREG(mode))
			mode &= ~S_ISGID;
	}
	(void) sprintf(header + M_STRLEN, "%06o%06o%06o%06o%06o",
	    USH(asb->sb_dev), USH(asb->sb_ino), USH(mode), uid, gid);
	(void) sprintf(header + M_STRLEN + 30, "%06o%06o%011lo%06o%011llo",
	    USH(asb->sb_nlink), USH(asb->sb_rdev),
	    f_mtime ? asb->sb_mtime : time((time_t *)0),
	    namelen, (OFFSET) asb->sb_size);

	/*
	 * If extended attribute dump out xattr lookup header
	 * before attribute
	 */
	if (asb->xattr_info.xattraname != NULL) {
		Stat newstat;

		(void) memset(&newstat, 0, sizeof (newstat));
		namelen2 = (uint_t)strlen(attrhdrname) + 1;
		prepare_xattr_hdr(&attrbuf, name, asb->xattr_info.xattraname,
		    tartype(asb->sb_mode), NULL, &attrlen);
		(void) strcpy(attrheader, M_ASCII);
		(void) sprintf(attrheader + M_STRLEN, "%06o%06o%06o%06o%06o",
		    USH(asb->sb_dev), USH(asb->sb_ino), USH(mode), uid,
		    gid);
		(void) sprintf(attrheader + M_STRLEN + 30,
		    "%06o%06o%011lo%06o%011llo", USH(asb->sb_nlink),
		    USH(asb->sb_rdev),
		    f_mtime ? asb->sb_mtime : time((time_t *)0),
		    namelen2, (OFFSET) attrlen);
		outwrite(attrheader, (OFFSET) M_STRLEN + H_STRLEN);
		outwrite(attrhdrname, (OFFSET) namelen2);
		outwrite(attrbuf, (OFFSET) attrlen);
		free(attrname);
		free(attrhdrname);
	}
	outwrite(header, (OFFSET) M_STRLEN + H_STRLEN);
	outwrite(namep, (OFFSET) namelen);
#ifdef	S_IFLNK
	if ((asb->sb_mode & S_IFMT) == S_IFLNK)
		outwrite(asb->sb_link, (OFFSET) asb->sb_size);
#endif		/* S_IFLNK */
	return (0);
}


/*
 * check_update - compare modification times of archive file and real file.
 *
 * DESCRIPTION
 *
 *	check_update looks up name in the hash table and compares the
 *	modification time in the table iwth that in the stat buffer.
 *
 * PARAMETERS
 *
 *	char	*name	- The name of the current file
 *	Stat	*sb	- stat buffer of the current file
 *
 * RETURNS
 *
 *	1 - if the names file is new than the one in the archive
 *	0 - if we don't want to add this file to the archive.
 */
static int
check_update(char *name, Stat *sb)
{
	struct timeval mtime;
	if (hash_lookup(name, &mtime) == -1)	/* not found in hash table */
		return (1);
	if ((sb->sb_stat.st_mtim.tv_sec > mtime.tv_sec) ||
	    ((sb->sb_stat.st_mtim.tv_sec == mtime.tv_sec) &&
	    (sb->sb_stat.st_mtim.tv_nsec > mtime.tv_sec *1000)))
		return (1);
	return (0);
}

/*
 * gen_num creates a string from a keyword and an usigned long long in the
 * format:  %d %s=%s\n
 * This is part of the extended header data record.
 */

void
gen_num(const char *keyword, const u_longlong_t number)
{
	char	save_val[ULONGLONG_MAX_DIGITS + 1];
	int	len;
	char	*curr_ptr;

	(void) sprintf(save_val, "%llu", number);
	/*
	 * len = length of entire line, including itself.  The character length
	 * of len must be 1-4 characters, because the maximum size of the path
	 * or the name is PATH_MAX, which is 1024.  So, assume 1 character
	 * for len, one for the space, one for the "=", and one for the newline.
	 * Then adjust as needed.
	 */
	/* LINTED logical expression always true */
	assert(PATH_MAX <= 9996);
	len = strlen(save_val) + strlen(keyword) + 4;
	if (len > 997)
		len += 3;
	else if (len > 98)
		len += 2;
	else if (len > 9)
		len += 1;
	if (xrec_offset + len + 1 > xrec_size) {
		xrec_size = ((xrec_offset + len + 1) > (2 * xrec_size))
		    ? (xrec_offset + len + 1) : (2 * xrec_size);
		if (((curr_ptr = realloc(xrec_ptr, xrec_size)) == NULL))
			fatal(gettext(
			    "cannot allocate extended header buffer"));
		xrec_ptr = curr_ptr;
	}
	/* 'len + 1' to include the null byte */
	(void) snprintf(&xrec_ptr[xrec_offset], len + 1,
	    "%d %s=%s\n", len, keyword, save_val);
	xrec_offset += len;
}

/*
 * gen_date will be called with the input time_value if -x pax
 * was not specified or if there is a time override which
 * contains a decimal point.  If an override value doesn't
 * contain a decimal point, we are to output time exactly if it
 * can be represented exactly as a decimal number, therefore
 * we call gen_num with the override value.  Note: the override
 * values, if any, were already entered into the time_value when
 * the overrides were processed, however, this routine is needed
 * simply to handle the requirement for representing a time
 * exactly if it can be represented exactly.
 */
void
x_gen_date(int xhdrtype, char *keyword, const timestruc_t time_value)
{

	if (!f_stdpax) {
		gen_date(keyword, time_value);
	} else {
		char *val;

		/* typeflag 'x' ext hdr */
		if (xhdrtype == XXHDRTYPE) {
			if (nvlist_lookup_string(xoptlist,
			    keyword, &val) != 0) {
				/* no override */
				gen_date(keyword, time_value);
				return;
			}
		/* typeflag 'g' ext hdr */
		} else if (xhdrtype == GXHDRTYPE) {
			if (nvlist_lookup_string(goptlist,
			    keyword, &val) != 0) {
				/* no override */
				gen_date(keyword, time_value);
				return;
			}
		}

		/*
		 * Output a time exactly if it can be represented as a
		 * decimal number.
		 */
		if ((val != NULL) && (strchr(val, '.') != NULL)) {
			gen_date(keyword, time_value);
		} else {
			gen_num(keyword, time_value.tv_sec);
		}
	}
}

/*
 * gen_date creates a string from a keyword and a timestruc_t in the
 * format:  %d %s=%s\n
 * This is part of the extended header data record.
 * Currently, granularity is only microseconds, so the low-order three digits
 * will be truncated.
 */

void
gen_date(const char *keyword, const timestruc_t time_value)
{
	/* Allow for <seconds>.<nanoseconds>\n */
	char	save_val[TIME_MAX_DIGITS + LONG_MAX_DIGITS + 2];
	int	len;
	char	*curr_ptr;

	(void) sprintf(save_val, "%ld", time_value.tv_sec);
	len = strlen(save_val);
	save_val[len] = '.';
	(void) sprintf(&save_val[len + 1], "%9.9ld", time_value.tv_nsec);

	/*
	 * len = length of entire line, including itself.  len will be
	 * two digits.  So, add the string lengths plus the length of len,
	 * plus a blank, an equal sign, and a newline.
	 */
	len = strlen(save_val) + strlen(keyword) + 5;
	if (xrec_offset + len + 1 > xrec_size) {
		xrec_size = ((xrec_offset + len + 1) > (2 * xrec_size))
		    ? (xrec_offset + len + 1) : (2 * xrec_size);
		if (((curr_ptr = realloc(xrec_ptr, xrec_size)) == NULL))
			fatal(gettext(
			    "cannot allocate extended header buffer"));
		xrec_ptr = curr_ptr;
	}
	(void) snprintf(&xrec_ptr[xrec_offset], len + 1,
	    "%d %s=%s\n", len, keyword, save_val);
	xrec_offset += len;
}

/*
 * gen_string creates a string from a keyword and a char * in the
 * format:  %d %s=%s\n
 * This is part of the extended header data record.
 */

void
gen_string(const char *keyword, const char *value)
{
	int	len;
	char	*curr_ptr;
	char	checklen[ULONGLONG_MAX_DIGITS + 2];

	/*
	 * len = length of entire line, including itself.  The character length
	 * of len must be 1-4 characters, because the maximum size of the path
	 * or the name is PATH_MAX, which is 1024.  So, assume 1 character
	 * for len, one for the space, one for the "=", and one for the newline.
	 * Then adjust as needed.
	 */
	len = strlen(value) + strlen(keyword) + 4;
	(void) memset(checklen, '\0', ULONGLONG_MAX_DIGITS + 2);
	(void) sprintf(checklen, "%lld", (long long) len);
	len += strlen(checklen) - 1;
	if (xrec_offset + len + 1 > xrec_size) {
		xrec_size = ((xrec_offset + len + 1) > (2 * xrec_size))
		    ? (xrec_offset + len + 1) : (2 * xrec_size);
		if (((curr_ptr = realloc(xrec_ptr, xrec_size)) == NULL))
			fatal(gettext(
			    "cannot allocate extended header buffer"));
		xrec_ptr = curr_ptr;
	}
	(void) snprintf(&xrec_ptr[xrec_offset], len + 1,
	    "%d %s=%s\n", len, keyword, value);
	xrec_offset += len;
}


char *
alloc_local(size_t size)
{
	char *lname;

	if ((lname = malloc(size)) == NULL) {
		fatal(gettext("out of memory"));
	}
	(void) memset(lname, 0, size);

	return (lname);
}

/*
 * Check gname, uname, path, and linkpath to see if they need to go in an
 * extended header.  If they are already slated to be in an extended header,
 * or if they are not ascii, then they need to be in the extended header.
 * Then, convert all extended names to UTF-8.
 */

int
gen_utf8_names(int xhdrtype, const char *filename, Stat * asb, char *hdr)
{
	static	iconv_t	iconv_cd;
	char		*nl_target;
	char		*tempbuf;
	int		nbytes;
	int		errors;

	if (charset_type == CHARSET_ERROR) {	/* Previous failure to open. */
		(void) fprintf(stderr, gettext(
		    "%s file # %llu: UTF-8 conversion failed.\n"),
		    myname, xhdr_count);
		return (1);
	}

	if (charset_type == CHARSET_UNKNOWN) {	/* Get conversion descriptor */
		nl_target = nl_langinfo(CODESET);
		if (strlen(nl_target) == 0)	/* locale using 7-bit codeset */
			nl_target = "646";
		if (strcmp(nl_target, "646") == 0) {
			charset_type = CHARSET_7_BIT;
		} else if (strcmp(nl_target, "UTF-8") == 0) {
			charset_type = CHARSET_UTF_8;
		} else {
			if (strncmp(nl_target, "ISO", 3) == 0)
				nl_target += 3;
			charset_type = CHARSET_8_BIT;
			errno = 0;
#ifdef ICONV_DEBUG
			(void) fprintf(stderr,
			    "Opening iconv_cd with target %s\n",
			    nl_target);
#endif
			if ((iconv_cd = iconv_open("UTF-8", nl_target)) ==
			    (iconv_t)-1) {
				if (errno == EINVAL)
					warn(myname, gettext(
					    "conversion routines not "
					    "available for current locale.  "));
				(void) fprintf(stderr, gettext(
				    "file (%s): UTF-8 conversion failed.\n"),
				    filename);
				charset_type = CHARSET_ERROR;
				return (1);
			}
		}
	}

	errors = 0;

	if (!f_stdpax) {
		tempbuf = alloc_local(PATHELEM + 1);
		if (local_gname == NULL) {
			local_gname = alloc_local(UTF8PATHSIZE);
		}
		errors += local_utf8(&Xtarhdr.x_gname, local_gname,
		    &hdr[TO_GNAME], iconv_cd, _X_GNAME, xhdrtype,
		    _POSIX_NAME_MAX);

		if (local_uname == NULL) {
			local_uname = alloc_local(UTF8PATHSIZE);
		}
		errors += local_utf8(&Xtarhdr.x_uname, local_uname,
		    &hdr[TO_UNAME], iconv_cd, _X_UNAME, xhdrtype,
		    _POSIX_NAME_MAX);

		if ((xhdr_flgs & _X_LINKPATH) == 0) {
			/* Need null-terminated string */
			(void) strncpy(tempbuf, asb->sb_link, TL_NAME);
			tempbuf[TL_NAME] = '\0';
		}

		if (local_linkpath == NULL) {
			local_linkpath = alloc_local(UTF8PATHSIZE);
		}
		errors += local_utf8(&Xtarhdr.x_linkpath, local_linkpath,
		    tempbuf, iconv_cd, _X_LINKPATH, xhdrtype, PATH_MAX);


		if (local_path == NULL) {
			local_path = alloc_local(UTF8PATHSIZE);
		}
		(void) memset(tempbuf, '\0', PATHELEM + 1);
		if ((xhdr_flgs & _X_PATH) == 0) {
			/* Concatenate prefix & name */
			(void) strncpy(tempbuf, &hdr[TO_PREFIX], TL_PREFIX);
			tempbuf[TL_PREFIX] = '\0';
			nbytes = strlen(tempbuf);
			if (nbytes > 0) {
				tempbuf[nbytes++] = '/';
				tempbuf[nbytes] = '\0';
			}
			(void) strncat(tempbuf + nbytes, &hdr[TO_NAME],
			    TL_NAME);
			tempbuf[nbytes + TL_NAME] = '\0';
		}
		errors += local_utf8(&Xtarhdr.x_path, local_path,
		    tempbuf, iconv_cd, _X_PATH, xhdrtype, PATH_MAX);

	} else {
		int	maxsize = PATH_MAX;
		int	len = PATH_MAX;


		switch (xhdrtype) {
		case XXHDRTYPE:
			if (oxxhdr_flgs & _X_PATH) {
				len = strlen(coXtarhdr.x_path);
			}
			break;
		case GXHDRTYPE:
			if (ogxhdr_flgs & _X_PATH) {
				len = strlen(coXtarhdr.gx_path);
			}
			break;
		case XHDRTYPE:
			if (xhdr_flgs & _X_PATH) {
				len = strlen(Xtarhdr.x_path);
			}
			break;
		default:
			break;
		}
		if (len > maxsize) {
			maxsize = len;
		}
		if ((tempbuf = calloc((maxsize + 1), sizeof (char))) == NULL) {
			fatal(gettext("out of memory"));
		}

		/* use typeflag 'x' override with typeflag x extended hdr */
		if (oxxhdr_flgs & _X_GNAME) {
			errors += local_utf8(&coXtarhdr.x_gname, local_gname,
			    &hdr[TO_GNAME], iconv_cd, _X_GNAME, xhdrtype,
			    _POSIX_NAME_MAX);
		/* use typeflag 'g' override with typeflag g extended hdr */
		} else if (ogxhdr_flgs & _X_GNAME) {
			errors += local_utf8(&coXtarhdr.gx_gname, local_gname,
			    &hdr[TO_GNAME], iconv_cd, _X_GNAME, xhdrtype,
			    _POSIX_NAME_MAX);
		} else if (xhdr_flgs & _X_GNAME) {
			errors += local_utf8(&Xtarhdr.x_gname, local_gname,
			    &hdr[TO_GNAME], iconv_cd, _X_GNAME, xhdrtype,
			    _POSIX_NAME_MAX);
		}
		if (oxxhdr_flgs & _X_UNAME) {
			errors += local_utf8(&coXtarhdr.x_uname, local_uname,
			    &hdr[TO_UNAME], iconv_cd, _X_UNAME, xhdrtype,
			    _POSIX_NAME_MAX);
		} else if (ogxhdr_flgs & _X_UNAME) {
			errors += local_utf8(&coXtarhdr.gx_uname, local_uname,
			    &hdr[TO_UNAME], iconv_cd, _X_UNAME, xhdrtype,
			    _POSIX_NAME_MAX);
		} else if (xhdr_flgs & _X_UNAME) {
			errors += local_utf8(&Xtarhdr.x_uname, local_uname,
			    &hdr[TO_UNAME], iconv_cd, _X_UNAME, xhdrtype,
			    _POSIX_NAME_MAX);
		}
		if (oxxhdr_flgs & _X_LINKPATH) {
			errors += local_utf8(&coXtarhdr.x_linkpath,
			    local_linkpath, tempbuf, iconv_cd,
			    _X_LINKPATH, xhdrtype, PATH_MAX);
		} else if (ogxhdr_flgs & _X_LINKPATH) {
			errors += local_utf8(&coXtarhdr.gx_linkpath,
			    local_linkpath, tempbuf, iconv_cd,
			    _X_LINKPATH, xhdrtype, PATH_MAX);
		} else if (xhdr_flgs & _X_LINKPATH) {
			errors += local_utf8(&Xtarhdr.x_linkpath,
			    local_linkpath, tempbuf, iconv_cd,
			    _X_LINKPATH, xhdrtype, PATH_MAX);
		}
		(void) memset(tempbuf, '\0', maxsize + 1);
		switch (xhdrtype) {
		case XXHDRTYPE:
			if (oxxhdr_flgs & _X_PATH) {
				(void) strncpy(tempbuf, coXtarhdr.x_path,
				    maxsize);
				errors += local_utf8(&coXtarhdr.x_path,
				    xlocal_path, tempbuf, iconv_cd,
				    _X_PATH, xhdrtype, maxsize);
			} else {
				/*
				 * If an override wasn't specified, we
				 * need to generate the path name
				 * from the prefix and name fields to
				 * use in the extended header.
				 */
				if ((xhdr_flgs & _X_PATH) == 0) {
					/* Concatenate prefix & name */
					(void) strncpy(tempbuf,
					    &hdr[TO_PREFIX], TL_PREFIX);
					tempbuf[TL_PREFIX] = '\0';
					nbytes = strlen(tempbuf);
					if (nbytes > 0) {
						tempbuf[nbytes++] = '/';
						tempbuf[nbytes] = '\0';
					}
					(void) strncat(tempbuf + nbytes,
					    &hdr[TO_NAME],
					    TL_NAME);
					tempbuf[nbytes + TL_NAME] = '\0';
				}
				errors += local_utf8(&coXtarhdr.x_path,
				    xlocal_path, tempbuf, iconv_cd,
				    _X_PATH, xhdrtype, PATH_MAX);
			}
			break;
		case GXHDRTYPE:
			if (ogxhdr_flgs & _X_PATH) {
				(void) strncpy(tempbuf, coXtarhdr.gx_path,
				    maxsize);
				errors += local_utf8(&coXtarhdr.gx_path,
				    glocal_path, tempbuf, iconv_cd,
				    _X_PATH, xhdrtype, maxsize);
			} else {
				if ((xhdr_flgs & _X_PATH) == 0) {
					/* Concatenate prefix & name */
					(void) strncpy(tempbuf,
					    &hdr[TO_PREFIX], TL_PREFIX);
					tempbuf[TL_PREFIX] = '\0';
					nbytes = strlen(tempbuf);
					if (nbytes > 0) {
						tempbuf[nbytes++] = '/';
						tempbuf[nbytes] = '\0';
					}
					(void) strncat(tempbuf + nbytes,
					    &hdr[TO_NAME],
					    TL_NAME);
					tempbuf[nbytes + TL_NAME] = '\0';
				}
				errors += local_utf8(&coXtarhdr.gx_path,
				    glocal_path, tempbuf, iconv_cd, _X_PATH,
				    xhdrtype, PATH_MAX);
			}
			break;
		case XHDRTYPE:
			if (xhdr_flgs & _X_PATH) {
				errors += local_utf8(&Xtarhdr.x_path,
				    local_path, tempbuf, iconv_cd,
				    _X_PATH, xhdrtype, maxsize);
			}
			break;
		default:
			break;
		}
		(void) memset(tempbuf, '\0', maxsize + 1);
		if (oxxhdr_flgs & _X_CHARSET) {
			errors += local_utf8(&coXtarhdr.x_charset,
			    local_charset, tempbuf, iconv_cd,
			    _X_CHARSET, xhdrtype, PATH_MAX);
		} else if (ogxhdr_flgs & _X_CHARSET) {
			errors += local_utf8(&coXtarhdr.gx_charset,
			    local_charset, tempbuf, iconv_cd,
			    _X_CHARSET, xhdrtype, PATH_MAX);
		}
		(void) memset(tempbuf, '\0', maxsize + 1);
		if (oxxhdr_flgs & _X_COMMENT) {
			errors += local_utf8(&coXtarhdr.x_comment,
			    local_comment, tempbuf, iconv_cd,
			    _X_COMMENT, xhdrtype, PATH_MAX);
		} else if (ogxhdr_flgs & _X_COMMENT) {
			errors += local_utf8(&coXtarhdr.gx_comment,
			    local_comment, tempbuf, iconv_cd,
			    _X_COMMENT, xhdrtype, PATH_MAX);
		}
		(void) memset(tempbuf, '\0', maxsize + 1);
		if (oxxhdr_flgs & _X_REALTIME) {
			errors += local_utf8(&coXtarhdr.x_realtime,
			    local_realtime, tempbuf, iconv_cd,
			    _X_REALTIME, xhdrtype, PATH_MAX);
		} else if (ogxhdr_flgs & _X_REALTIME) {
			errors += local_utf8(&coXtarhdr.gx_realtime,
			    local_realtime, tempbuf, iconv_cd,
			    _X_REALTIME, xhdrtype, PATH_MAX);
		}
		(void) memset(tempbuf, '\0', maxsize + 1);
		if (oxxhdr_flgs & _X_SECURITY) {
			errors += local_utf8(&coXtarhdr.x_security,
			    local_security, tempbuf, iconv_cd, _X_SECURITY,
			    xhdrtype, PATH_MAX);
		} else if (ogxhdr_flgs & _X_SECURITY) {
			errors += local_utf8(&coXtarhdr.gx_security,
			    local_security, tempbuf, iconv_cd,
			    _X_SECURITY, xhdrtype, PATH_MAX);
		}
		(void) memset(tempbuf, '\0', maxsize + 1);
		if (oxxhdr_flgs & _X_HOLESDATA) {
			errors += local_utf8(&coXtarhdr.x_holesdata,
			    local_holesdata, tempbuf, iconv_cd, _X_HOLESDATA,
			    xhdrtype, PATH_MAX);
		} else if (ogxhdr_flgs & _X_HOLESDATA) {
			errors += local_utf8(&coXtarhdr.gx_holesdata,
			    local_holesdata, tempbuf, iconv_cd,
			    _X_HOLESDATA, xhdrtype, PATH_MAX);
		}
	}
	free(tempbuf);


	if (errors > 0)
		(void) fprintf(stderr, gettext(
		    "%s: file (%s): UTF-8 conversion failed.\n"),
		    myname, filename);

	if (errors && f_exit_on_error)
		exit(1);
	return (errors);
}

static int
local_utf8(
		char	**Xhdr_ptrptr,
		char	*target,
		const	char	*source,
		iconv_t	iconv_cd,
		int	xhdrflg,
		int	xhdrtype,
		int	max_val)
{
	char		*iconv_src;
	char		*starting_src;
	char		*iconv_trg;
	size_t		inlen;
	size_t		outlen;
#ifdef ICONV_DEBUG
	unsigned char	c_to_hex;
#endif

	/*
	 * If the item is already slated for extended format, get the string
	 * to convert from the extended header record.  Otherwise, get it from
	 * the regular (dblock) area.
	 */
	if (((xhdr_flgs & xhdrflg) && (xhdrtype == XHDRTYPE)) ||
	    ((oxxhdr_flgs & xhdrflg) && (xhdrtype == XXHDRTYPE)) ||
	    ((ogxhdr_flgs & xhdrflg) && (xhdrtype == GXHDRTYPE))) {
		if (charset_type == CHARSET_UTF_8) {	/* Is UTF-8, so copy */
			(void) strcpy(target, *Xhdr_ptrptr);
			*Xhdr_ptrptr = target;
			return (0);
		} else
			iconv_src = (char *)*Xhdr_ptrptr;
	} else {
		if (charset_type == CHARSET_UTF_8)	/* Already UTF-8 fmt */
			return (0);		/* Don't create xhdr record */
		iconv_src = (char *)source;
	}
	starting_src = iconv_src;
	iconv_trg = target;
	if ((inlen = strlen(iconv_src)) == 0)
		return (0);

	if (charset_type == CHARSET_7_BIT) {	/* locale using 7-bit codeset */
		if (c_utf8(target, starting_src) != 0) {
			(void) fprintf(stderr, gettext(
			    "%s: invalid character in UTF-8"
			    " conversion of '%s'\n"), myname, starting_src);
			return (1);
		}
		return (0);
	}

	outlen = max_val * UTF_8_FACTOR;
	errno = 0;
	if (iconv(iconv_cd, &iconv_src, &inlen, &iconv_trg, &outlen) ==
	    (size_t)-1) {
		/* An error occurred, or not all characters were converted */
		if (errno == EILSEQ)
			(void) fprintf(stderr, gettext(
			    "%s: invalid character in UTF-8"
			    " conversion of '%s'\n"), myname, starting_src);
		else
			(void) fprintf(stderr, gettext(
			    "%s: conversion to UTF-8 aborted for '%s'.\n"),
			    myname, starting_src);
		/* Get remaining output; reinitialize conversion descriptor */
		iconv_src = NULL;
		inlen = 0;
		(void) iconv(iconv_cd, &iconv_src, &inlen, &iconv_trg, &outlen);
		return (1);
	}
	/* Get remaining output; reinitialize conversion descriptor */
	iconv_src = NULL;
	inlen = 0;
	if (iconv(iconv_cd, &iconv_src, &inlen, &iconv_trg, &outlen) ==
	    (size_t)-1) {	/* Error occurred:  didn't convert */
		if (errno == EILSEQ)
			(void) fprintf(stderr, gettext(
			    "%s: invalid character in UTF-8"
			    " conversion of '%s'\n"), myname, starting_src);
		else
			(void) fprintf(stderr, gettext(
			    "%s: conversion to UTF-8 aborted for '%s'.\n"),
			    myname, starting_src);
		return (1);
	}

	*iconv_trg = '\0';	/* Null-terminate iconv output string */
	if (strcmp(starting_src, target) != 0) {
		*Xhdr_ptrptr = target;
		xhdr_flgs |= xhdrflg;
#ifdef ICONV_DEBUG
		(void) fprintf(stderr, "***  inlen: %d %d; outlen: %d %d\n",
		    strlen(starting_src), inlen, max_val, outlen);
		(void) fprintf(stderr, "Input string:\n  ");
		for (inlen = 0; inlen < strlen(starting_src); inlen++) {
			c_to_hex = (unsigned char)starting_src[inlen];
			(void) fprintf(stderr, " %2.2x", c_to_hex);
			if (inlen % 20 == 19)
				(void) fprintf(stderr, "\n  ");
		}
		(void) fprintf(stderr, "\nOutput string:\n  ");
		for (inlen = 0; inlen < strlen(target); inlen++) {
			c_to_hex = (unsigned char)target[inlen];
			(void) fprintf(stderr, " %2.2x", c_to_hex);
			if (inlen % 20 == 19)
				(void) fprintf(stderr, "\n  ");
		}
		(void) fprintf(stderr, "\n");
#endif
	}

	return (0);
}

/*
 * get_holesdata()
 *
 * Step through a file discovering and recording pairs of
 * data and hole offsets. Save space separated data/hole
 * offset pairs to the holesdata member of Xtarhdr
 * structure if there is a hole detected in the file.
 * Note: According to lseek(2), only filesystems which
 * support fpathconf(_PC_MIN_HOLE_SIZE) support SEEK_HOLE.
 * For filesystems that do not supply information about
 * holes, the file will be represented as one entire
 * data region, and thus no holes information will be
 * stored in the Stat structure.
 *
 * When a hole is detected a minimum of 2 data/hole offset
 * pairs will be saved in Xtarhdr.x_holesdata in the following
 * format:
 *  <SPACE><data off><SPACE><hole off><SPACE><data off><SPACE><hole off> ...
 * The data in Xtarhdr.x_holesdata will be used later to add the
 * sparse file info to the extended header data for the file so
 * that when this file is restored, the data/hole pairs can be used
 * to correctly restore the holes in the file.
 *
 * If 2 data/hole paris are not found, then the file does
 * not contain any holes, and Xtarhddr.x_holesdata will be NULL.
 */

void
get_holesdata(int fd, off_t filesize)
{
	OFFSET	off = 0;
	OFFSET	data;
	OFFSET	hole;
	char	datanumbuf[ULONGLONG_MAX_DIGITS + 1];
	char	holenumbuf[ULONGLONG_MAX_DIGITS + 1];
	char	*dnumstr;
	char	*hnumstr;
	char	*holesdptr = NULL;
	size_t	sdatalen = 0;
	size_t	len = 0;
	size_t	dnumlen, hnumlen;

	/*
	 * Make sure the file system supports holes,
	 * and there aren't already command line overrides
	 * for the holes data.  Nothing needs to be done
	 * if filesize is 0.
	 */
	if ((filesize == 0) ||
	    (fpathconf(fd, _PC_MIN_HOLE_SIZE) < 0) ||
	    (oXtarhdr.x_holesdata != NULL) ||
	    (Xtarhdr.x_holesdata != NULL)) {
		return;
	}

	/*
	 * Seek through the file gathering pairs of
	 * data and hole offsets.
	 */
	for (;;) {
		data = lseek(fd, off, SEEK_DATA);
		/*
		 * If we don't have any more data offsets,
		 * and have processed all the file bits,
		 * then we're done, otherwise, there's a
		 * hole at the end of the file.
		 */
		if (data == -1) {
			/*
			 * Hole at end of file? Ensure two
			 * data/hole pairs are reported.
			 */
			if ((holesdptr == NULL) && (sdatalen == 0)) {
				/* No data in file */
				data = 0;
				hole = 0;
			} else if (off != filesize) {
				data = filesize;
				hole = filesize;
			} else {
				/* All bits were processed, we're done */
				break;
			}
		} else {
			/*
			 * Hole at beginning of file? Ensure two
			 * data/hole pairs are reported.
			 */
			if ((holesdptr == NULL) && (data > 0)) {
				data = 0;
			}
			hole = lseek(fd, data, SEEK_HOLE);
		}
		off = hole;

		/*
		 * Only save holes data if there is a
		 * hole detected in the file.  If this
		 * is the first data/hole pair
		 * gathered, then if the size of the
		 * data is equal to the size of the
		 * file, then no holes were detected
		 * within the file.
		 */
		if (sdatalen == 0) {
			if (off == filesize) {
				break;
			}
		}
		datanumbuf[ULONGLONG_MAX_DIGITS] = '\0';
		holenumbuf[ULONGLONG_MAX_DIGITS] = '\0';
		dnumstr = lltostr(data, &datanumbuf[ULONGLONG_MAX_DIGITS]);
		hnumstr = lltostr(hole, &holenumbuf[ULONGLONG_MAX_DIGITS]);
		dnumlen = strlen(dnumstr);
		hnumlen = strlen(hnumstr);
		/* "...len...<SP>dnumstr<SP>hnumstr" */
		if ((len + 1 + dnumlen + 1 + hnumlen) >= sdatalen) {
			size_t prevlen = sdatalen;
			sdatalen += BUFSIZ;
			holesdptr = realloc(holesdptr, sdatalen);
			if (holesdptr == NULL)
				fatal(gettext("out of memory"));
			(void) memset(holesdptr + prevlen, '\0',
			    sdatalen - prevlen);
		}
		holesdptr[len++] = ' ';
		(void) memcpy(&holesdptr[len], dnumstr, dnumlen);
		len += dnumlen;
		holesdptr[len++] = ' ';
		(void) memcpy(&holesdptr[len], hnumstr, hnumlen);
		len += hnumlen;

		holesdptr[len] = '\0';
	}
	if (holesdptr != NULL) {
		Xtarhdr.x_holesdata = holesdptr;
		xhdr_flgs |= _X_HOLESDATA;
	}
	/* reset the file pointer */
	(void) lseek(fd, (OFFSET) 0, SEEK_SET);
}

/*
 * Returns:	0 No error
 *		1 Error
 *		2 Archived as link.
 */
static int
archive_file(char **name, size_t *namesz, Stat *sb)
{
	int	fd;
	int	err;

	if ((fd = openin(*name, sb)) < 0)
		return (1);

	if (!f_unconditional && (check_update(*name, sb) == 0)) {
		/* Skip file... one in archive is newer */
		if (fd > 0)
			(void) close(fd);
		return (1);
	}

	if (rplhead != (Replstr *)NULL) {
		rpl_name(name, namesz);
		if (strlen(*name) == 0) {
			if (fd > 0)
				(void) close(fd);
			return (1);
		}
	}

	if (get_disposition(ADD, *name, *namesz) ||
	    get_newname(name, namesz, sb)) {
		/* skip file... */
		if (fd > 0)
			(void) close(fd);
		return (1);
	}

	xhdr_flgs = 0;
	if (sb->sb_size > (OFFSET)(TAR_OFFSET_MAX)) {
		if (f_pax == 0) {
			warn(*name, gettext("too large to archive"));
			return (1);
		} else {
			xhdr_flgs |= _X_SIZE;
			Xtarhdr.x_filesz = sb->sb_size;
		}
	}

	if (!f_link && (sb->sb_nlink > 1)) {
		/*
		 * If we are to write hard link data (-x pax
		 * and -o linkdata were specified on the cmd line)
		 * then we need to skip this step where the hard
		 * links are created.  This will ensure the
		 * contents of the file are written to the header.
		 */
		if (!(f_stdpax && f_linkdata)) {
			if (islink(*name, sb)) {
				sb->sb_size = 0;
			}
			if (sb->xattr_info.xattraname != NULL) {
				if (sb->xattr_info.xattr_linkaname != NULL) {
					free(sb->xattr_info.xattr_linkaname);
				}
				if ((sb->xattr_info.xattr_linkaname = malloc(
				    strlen(sb->xattr_info.xattraname) + 1))
				    == NULL) {
					fatal(gettext("out of memory"));
				}
				(void) strcpy(sb->xattr_info.xattr_linkaname,
				    sb->xattr_info.xattraname);
			}
			(void) linkto(*name, sb);
		}
	}

	/* Get holey file data */
	if (f_pax && (fd > 0)) {
		get_holesdata(fd, sb->sb_size);
	}
	if (ar_format == TAR || ar_format == PAX)
		err = writetar(*name, sb);
	else
		err = writecpio(*name, sb);
	if (err == 0) {
		if (fd)
			outdata(fd, *name, sb);
		if (f_verbose)
			print_entry(*name, sb);
	}
	if (Xtarhdr.x_holesdata != NULL) {
		free(Xtarhdr.x_holesdata);
		Xtarhdr.x_holesdata = NULL;
	}
	return (err);
}

int
is_sysattr(char *name)
{
	return ((strcmp(name, VIEW_READONLY) == 0) ||
	    (strcmp(name, VIEW_READWRITE) == 0));
}

#if defined(O_XATTR)
/*
 * Verify the attribute, attrname, is an attribute we want to restore.
 * Never restore read-only system attribute files.  Only restore read-write
 * system attributes files when -/ was specified, and only traverse into
 * the 2nd level attribute directory containing only system attributes if
 * -@ was specified.  This keeps us from archiving
 *	<attribute name>/<read-write system attribute file>
 * when -/ was specified without -@.
 *
 * attrname		- attribute file name
 * attrparent		- attribute's parent name within the base file's
 *			attribute directory hierarchy
 * arc_rwsysattr	- flag that indicates that read-write system attribute
 *			file should be archived as it contains other than
 *			the default system attributes.
 * rw_sysattr		- on return, flag will indicate if attrname is a
 *			read-write system attribute file.
 */
attr_status_t
verify_attr(char *attrname, char *attrparent, int arc_rwsysattr,
    int *rw_sysattr)
{
#if defined(_PC_SATTR_ENABLED)
	int	attr_supported;

	/* Never restore read-only system attribute files */
	if ((attr_supported = sysattr_type(attrname)) == _RO_SATTR) {
		*rw_sysattr = 0;
		return (ATTR_SKIP);
	} else {
		*rw_sysattr = (attr_supported == _RW_SATTR);
	}

	/*
	 * Don't archive a read-write system attribute file if
	 * it contains only the default system attributes.
	 */
	if (*rw_sysattr && !arc_rwsysattr) {
		return (ATTR_SKIP);
	}

#else
	/* Never restore read-only system attributes */
	if ((*rw_sysattr = is_sysattr(attrname)) == 1) {
		return (ATTR_SKIP);
	}
#endif	/* _PC_SATTR_ENABLED */

	/*
	 * Only restore read-write system attribute files
	 * when -/ was specified.  Only restore extended
	 * attributes when -@ was specified.
	 */
	if (f_extended_attr) {
		if (!f_sys_attr) {
			/*
			 * Only archive/restore the hidden directory "." if
			 * we're processing the top level hidden attribute
			 * directory.  We don't want to process the
			 * hidden attribute directory of the attribute
			 * directory that contains only extended system
			 * attributes.
			 */
			if (*rw_sysattr || (Hiddendir &&
			    (attrparent != NULL))) {
				return (ATTR_SKIP);
			}
		}
	} else if (f_sys_attr) {
		/*
		 * Only archive/restore read-write extended system attribute
		 * files of the base file.
		 */
		if (!*rw_sysattr || (attrparent != NULL)) {
			return (ATTR_SKIP);
		}
	} else {
		return (ATTR_SKIP);
	}

	return (ATTR_OK);
}
#endif

/*
 * Verify the underlying file system supports the attribute type.
 * Only archive extended attribute files when '-@' was specified.
 * Only archive system extended attribute files if '-/' was specified.
 */
#if defined(O_XATTR)
attr_status_t
verify_attr_support(char *filename, int attrflg, arc_action_t actflag,
    int *ext_attrflg)
{
	/*
	 * Verify extended attributes are supported/exist.  We only
	 * need to check if we are processing a base file, not an
	 * extended attribute.
	 */
	if (attrflg) {
		*ext_attrflg = (pathconf(filename, (actflag == ARC_CREATE) ?
		    _PC_XATTR_EXISTS : _PC_XATTR_ENABLED) == 1);
	}

	if (f_extended_attr) {
#if defined(_PC_SATTR_ENABLED)
		if (!*ext_attrflg) {
			if (f_sys_attr) {
				/* Verify system attributes are supported */
				if (sysattr_support(filename,
				    (actflag == ARC_CREATE) ? _PC_SATTR_EXISTS :
				    _PC_SATTR_ENABLED) != 1) {
					return (ATTR_SATTR_ERR);
				}
			} else
				return (ATTR_XATTR_ERR);
#else
				return (ATTR_XATTR_ERR);
#endif	/* _PC_SATTR_ENABLED */
		}

#if defined(_PC_SATTR_ENABLED)
	} else if (f_sys_attr) {
		/* Verify system attributes are supported */
		if (sysattr_support(filename, (actflag == ARC_CREATE) ?
		    _PC_SATTR_EXISTS : _PC_SATTR_ENABLED) != 1) {
			return (ATTR_SATTR_ERR);
		}
#endif	/* _PC_SATTR_ENABLED */
	} else {
		return (ATTR_SKIP);
	}

	return (ATTR_OK);
}
#endif

int
save_cwd(void)
{
	return (open(".", O_RDONLY));
}

void
rest_cwd(int cwd)
{
	(void) fchdir(cwd);
	(void) close(cwd);
}

#if defined(O_XATTR)
static void
dump_xattrs(char **name, size_t *namesz, char *attrparent, int baseparent_fd)
{
	Stat		sb;
	char		*filename = (attrparent == NULL) ? *name : attrparent;
	int		fd;
	int		dirfd;
	int		arc_rwsysattr = 0;
	int		rw_sysattr = 0;
	int		ext_attr = 0;
	int		anamelen = 0;
	int		apathlen;
	int		rc;
	DIR		*dirp;
	struct dirent 	*dp;


	/*
	 *  If the underlying file system supports it, then archive the extended
	 * attributes if -@ was specified, and the extended system attributes
	 * if -/ was specified.
	 */
	if (verify_attr_support(filename, (attrparent == NULL), ARC_CREATE,
	    &ext_attr) != ATTR_OK) {
		return;
	}

	/*
	 * Only want to archive a read-write extended system attribute file
	 * if it contains extended system attribute settings that are not the
	 * default values.
	 */
#if defined(_PC_SATTR_ENABLED)
	if (f_sys_attr) {
		int	filefd;
		nvlist_t *slist = NULL;

		/* Determine if there are non-transient system attributes */
		errno = 0;
		if ((filefd = open(filename, O_RDONLY)) == -1) {
			if (attrparent == NULL) {
				(void) fprintf(stderr, gettext(
				    "%s: unable to open file %s: %s"),
				    myname, *name, strerror(errno));
			}
			return;
		}
		if (((slist = sysattr_list(basename(myname), filefd,
		    filename)) != NULL) || (errno != 0)) {
			arc_rwsysattr = 1;
		}
		if (slist != NULL) {
			(void) nvlist_free(slist);
			slist = NULL;
		}
		(void) close(filefd);
	}

	/*
	 * If we aren't archiving extended system attributes, and we are
	 * processing an attribute, or if we are archiving extended system
	 * attributes, and there are are no extended attributes, then there's
	 * no need to open up the attribute directory of the file unless the
	 * extended system attributes are not transient (i.e, the system
	 * attributes are not the default values).
	 */
	if ((arc_rwsysattr == 0) && ((attrparent != NULL) ||
	    (f_sys_attr && !ext_attr))) {
		return;
	}
#endif	/* _PC_SATTR_ENABLED */

	/* open the parent attribute directory */
	if ((fd = attropen(filename, ".", O_RDONLY)) == -1) {
		(void) fprintf(stderr, gettext(
		    "%s: cannot open attribute directory of "
		    "%s%s%sfile %s: %s\n"), myname,
		    (attrparent == NULL) ? "" : gettext("attribute "),
		    (attrparent == NULL) ? "" : attrparent,
		    (attrparent == NULL) ? "" : gettext(" of "),
		    *name, strerror(errno));
		return;
	}

	dirfd = dup(fd);
	if (dirfd == -1) {
		(void) fprintf(stderr, gettext(
		    "%s: cannot dup(2) attribute directory descriptor of "
		    "%s%s%sfile %s: %s\n"), myname,
		    (attrparent == NULL) ? "" : gettext("attribute "),
		    (attrparent == NULL) ? "" : attrparent,
		    (attrparent == NULL) ? "" : gettext(" of "),
		    *name, strerror(errno));
		if (fd > 0) {
			(void) close(fd);
		}
		return;
	}

	if ((dirp = fdopendir(dirfd)) == NULL) {
		(void) fprintf(stderr,
		    gettext(
		    "%s: cannot convert attribute descriptor into DIR: %s\n"),
		    myname, strerror(errno));
		if (dirfd > 0) {
			(void) close(dirfd);
		}
		if (fd > 0) {
			(void) close(fd);
		}
		return;
	}

	if (attrparent == NULL) {
		baseparent_fd = save_cwd();
	}

	while (dp = readdir(dirp)) {
		if (strcmp(dp->d_name, "..") == 0) {
			continue;
		}

		/* Determine if this attribute should be archived */
		if (verify_attr(dp->d_name, attrparent, arc_rwsysattr,
		    &rw_sysattr) != ATTR_OK) {
			continue;
		}

		if (strcmp(dp->d_name, ".") == 0) {
			Hiddendir = 1;
		} else {
			Hiddendir = 0;
		}

		(void) memset(&sb, 0, sizeof (sb));
		if (fstatat(fd, dp->d_name, &sb.sb_stat,
		    AT_SYMLINK_NOFOLLOW) == -1) {
			(void) fprintf(stderr,
			    gettext("%s: Cannot stat %s%s%sattribute %s of"
			    " file %s :%s\n"), myname,
			    (attrparent == NULL) ? "" : rw_sysattr ?
			    gettext("system attribute ") :
			    gettext("attribute "),
			    (attrparent == NULL) ? "" : dp->d_name,
			    (attrparent == NULL) ? "" : gettext(" of "),
			    (attrparent == NULL) ? dp->d_name : attrparent,
			    *name, strerror(errno));
				continue;
		}

		anamelen = strlen(dp->d_name) + 1;
		apathlen = anamelen;
		if (attrparent != NULL) {
			apathlen += strlen(attrparent) + 1; /* add 1 for '/' */
		}

		if (sb.xattr_info.xattraname != NULL) {
			free(sb.xattr_info.xattraname);
		}
		if ((sb.xattr_info.xattraname = malloc(anamelen)) == NULL) {
			fatal(gettext("out of memory"));
		}
		(void) strcpy(sb.xattr_info.xattraname, dp->d_name);

		if (sb.xattr_info.xattraparent != NULL) {
			free(sb.xattr_info.xattraparent);
		}
		if (attrparent != NULL) {
			STRDUP(sb.xattr_info.xattraparent, attrparent);
		}

		if (sb.xattr_info.xattrapath != NULL) {
			free(sb.xattr_info.xattrapath);
		}
		if ((sb.xattr_info.xattrapath = malloc(apathlen)) == NULL) {
			fatal(gettext("out of memory"));
		}
		(void) snprintf(sb.xattr_info.xattrapath, apathlen, "%s%s%s",
		    (attrparent == NULL) ? "" : attrparent,
		    (attrparent == NULL) ? "" : "/", dp->d_name);

		if (sb.xattr_info.xattrfname != NULL) {
			free(sb.xattr_info.xattrfname);
		}
		if ((sb.xattr_info.xattrfname = malloc(
		    strlen(*name) + 1)) == NULL) {
			fatal(gettext("out of memory"));
		}
		(void) strcpy(sb.xattr_info.xattrfname, *name);

		sb.xattr_info.xattr_rw_sysattr = rw_sysattr;
		sb.xattr_info.xattr_baseparent_fd = baseparent_fd;

		rc = archive_file(name, namesz, &sb);

#if defined(_PC_SATTR_ENABLED)
		/*
		 * If both -/ and -@ were specified, then archive the
		 * attribute's extended system attributes and hidden directory
		 * by making a recursive call to xattrs_put().
		 */
		if (!rw_sysattr && f_sys_attr && f_extended_attr &&
		    (rc != 1) && (Hiddendir == 0)) {

			/*
			 * Change into the attribute's parent attribute
			 * directory to determine to archive the system
			 * attributes.
			 */
			if (fchdir(fd) < 0) {
				(void)  fprintf(stderr, gettext(
				    "%s: cannot change to attribute "
				    "directory of %s%s%sfile %s: %s\n"), myname,
				    (attrparent == NULL) ? "" :
				    gettext("attribute "),
				    (attrparent == NULL) ? "" : attrparent,
				    (attrparent == NULL) ? "" : gettext(" of "),
				    *name, strerror(errno));
				(void) closedir(dirp);
				(void) close(fd);
				return;
			}

			dump_xattrs(name, namesz, dp->d_name, baseparent_fd);
		}
#endif	/* _PC_SATTR_ENABLED */
	}

	if (sb.xattr_info.xattrapath != NULL) {
		free(sb.xattr_info.xattrapath);
		sb.xattr_info.xattrapath = NULL;
	}
	if (sb.xattr_info.xattraparent != NULL) {
		free(sb.xattr_info.xattraparent);
		sb.xattr_info.xattraparent = NULL;
	}

	(void) closedir(dirp);
	(void) close(fd);
	if (attrparent == NULL) {
		rest_cwd(baseparent_fd);
	}
}
#else
static void
dump_xattrs(char **name, size_t *namesz, char *attrparent, int baseparent_fd)
{
}
#endif
