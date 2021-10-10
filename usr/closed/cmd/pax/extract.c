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
 * extract.c - Extract files from a tar archive.
 *
 * DESCRIPTION
 *
 * AUTHOR
 *
 *	Mark H. Colburn, NAPS International (mark@jhereg.mn.org)
 *
 * Sponsored by The USENIX Association for public distribution.
 */

/* Headers */

#include <utime.h>
#include <archives.h>
#include "pax.h"


/* Function Prototypes */


static int inbinary(char *, char *, Stat *);
static int inascii(char *, char *, Stat *);
static int inswab(char *, char *, Stat *);
static int readtar(char **, size_t *, Stat *);
static int readcpio(char **, size_t *, Stat *);
static void reset_directories(void);
static mode_t attrmode(char);

void
init_xattr_info(Stat *sb)
{
#if defined(O_XATTR)
	if (sb->xattr_info.xattrfname != NULL) {
		free(sb->xattr_info.xattrfname);
		sb->xattr_info.xattrfname = NULL;
	}
	if (sb->xattr_info.xattraparent != NULL) {
		free(sb->xattr_info.xattraparent);
		sb->xattr_info.xattraparent = NULL;
	}
	if (sb->xattr_info.xattraname != NULL) {
		free(sb->xattr_info.xattraname);
		sb->xattr_info.xattraname = NULL;
	}
	if (sb->xattr_info.xattrapath != NULL) {
		free(sb->xattr_info.xattrapath);
		sb->xattr_info.xattrapath = NULL;
	}
	if (sb->xattr_info.xattr_linkfname != NULL) {
		free(sb->xattr_info.xattr_linkfname);
		sb->xattr_info.xattr_linkfname = NULL;
	}
	if (sb->xattr_info.xattr_linkaname != NULL) {
		free(sb->xattr_info.xattr_linkaname);
		sb->xattr_info.xattr_linkaname = NULL;
	}
	if (sb->linkname != NULL) {
		free(sb->linkname);
		sb->linkname = NULL;
	}

	if (sb->xattr_info.xattrhead != NULL) {
		free(sb->xattr_info.xattrhead);
		sb->xattr_info.xattrhead = NULL;
	}
	sb->xattr_info.xattrp = NULL;
	sb->xattr_info.xattr_baseparent_fd = -1;
	sb->xattr_info.xattr_rw_sysattr = 0;
#endif
}

/*
 * read_archive - read in an archive
 *
 * DESCRIPTION
 *
 *	Read_archive is the central entry point for reading archives.
 *	Read_archive determines the proper archive functions to call
 *	based upon the archive type being processed.
 *
 * RETURNS
 *
 */


void
read_archive(void)
{
	Stat    sb;
	char    *name;
	char	*new_name;
	char	*namep;
	int	match;
	int	pad;
	int	my_cwd = -1;
	size_t	len;
	size_t	namesz = PATH_MAX + 1;

	if ((name = calloc(namesz, sizeof (char))) == NULL) {
		fatal(gettext("out of memory"));
	}

	(void) memset(&sb, 0, sizeof (Stat));
	name_gather();		/* get names from command line */

	firstxhdr = 1;
	xcont = 1;
	if (f_extended_attr || f_sys_attr) {
		my_cwd = save_cwd();
	}
	while (get_header(&name, &namesz, &sb) == 0) {
		if (sb.xattr_info.xattrfname != NULL) {
			namep = sb.xattr_info.xattrfname;
		} else {
			namep = name;
		}
		if ((match = name_match(namep, ((sb.sb_mode & S_IFMT) ==
		    S_IFDIR))) == -1)
			break;
		else
			match ^= f_reverse_match;

		len = (sb.xattr_info.xattrfname == NULL) ? strlen(name) + 1 :
		    strlen(sb.xattr_info.xattrfname) + 1;
		if (len > namesz) {
			namesz = len;
			if ((name = realloc(name, namesz)) == NULL) {
				fatal(gettext("out of memory"));
			}
		}
		if ((new_name = calloc(len, sizeof (char))) == NULL) {
			fatal(gettext("out of memory"));
		}
		(void) strncpy(new_name,
		    (sb.xattr_info.xattrfname == NULL) ? name :
		    sb.xattr_info.xattrfname, len);

		/*
		 * Fix up name if attribute.  Get rid of bogus /dev/null/XXXXXX
		 */
		if (sb.xattr_info.xattraname != NULL) {
			(void) strcpy(name, sb.xattr_info.xattrfname);
		}
		if (rplhead != NULL) {
			rpl_name(&new_name, &len);
			if (strlen(new_name) == 0) {
				if (((ar_format == TAR || ar_format == PAX) ?
				    buf_skip(ROUNDUP((OFFSET) sb.sb_size,
				    BLOCKSIZE)) :
				    buf_skip((OFFSET) sb.sb_size)) < 0) {
					warn(name, gettext(
					    "File data is corrupt"));
				}
				free(new_name);
				if (sb.xattr_info.xattrhead != NULL) {
					init_xattr_info(&sb);
				}
				Hiddendir = 0;
				xcont = 1;
				continue;
			}
		}

#if defined(O_XATTR)
		if (sb.xattr_info.xattraname != NULL) {
			sb.xattr_info.xattr_baseparent_fd = my_cwd;
			sb.xattr_info.xattr_rw_sysattr = is_sysattr(
			    sb.xattr_info.xattraname);
		}
#endif

		if (f_list) {	/* only wanted a table of contents */
			if (match) {
				print_entry(name, &sb);
			}
			if (no_data_printed) {
				if (((ar_format == TAR || ar_format == PAX)
				    ? buf_skip(ROUNDUP((OFFSET) sb.sb_size,
				    BLOCKSIZE))
				    : buf_skip((OFFSET) sb.sb_size)) < 0) {
					warn(name, gettext(
					    "File data is corrupt"));
				}
				no_data_printed = 1;
			}

		} else if (match) {
			int	skip;
			skip = (get_disposition(EXTRACT, name, namesz) ||
			    get_newname(&new_name, &len, &sb));

#if defined(O_XATTR)
			/*
			 * Don't restore the attribute if any of the
			 * following are true:
			 * 1. neither -@ or -/ was specified.
			 * 2. -@ was specified, -/ wasn't specified, and we're
			 * processing a hidden attribute directory of an
			 * attribute, or we're processing a read-write system
			 * attribute file.
			 * 3. -@ wasn't specified, -/ was specified, and the
			 * file we're processing is not a read-write system
			 * attribute file, or we're processing the hidden
			 * attribute directory of an attribute.
			 *
			 * We always process the attributes if we're
			 * just generating the table of contents, or if both
			 * -@ and -/ were specified.
			 */
			if (!skip && (sb.xattr_info.xattraname != NULL)) {
				skip = ((!f_extended_attr && !f_sys_attr) ||
				    (f_extended_attr && !f_sys_attr &&
				    ((sb.xattr_info.xattraparent != NULL) ||
				    sb.xattr_info.xattr_rw_sysattr)) ||
				    (!f_extended_attr && f_sys_attr &&
				    ((sb.xattr_info.xattraparent != NULL) ||
				    !sb.xattr_info.xattr_rw_sysattr)));
			}
#endif
			if (skip) {
				/* skip file... */
				if (((ar_format == TAR ||
				    ar_format == PAX) ?
				    buf_skip(ROUNDUP((OFFSET) sb.sb_size,
				    BLOCKSIZE)) :
				    buf_skip((OFFSET) sb.sb_size)) < 0) {
					warn(name, gettext(
					    "File data is corrupt"));
				}
				free(new_name);
				if (sb.xattr_info.xattrhead != NULL) {
					init_xattr_info(&sb);
				}
				Hiddendir = 0;
				xcont = 1;
				continue;
			}
			if (inentry(&new_name, &len, name, &sb) < 0)
				warn(name, gettext("File data is corrupt"));
			if (my_cwd != -1) {
				(void) fchdir(my_cwd);
			}
			if (f_verbose)
				print_entry(new_name, &sb);
			if ((ar_format == TAR || ar_format == PAX) &&
			    sb.sb_nlink > 1) {
				/*
				 * This kludge makes sure that the link
				 * table is cleared before attempting to
				 * process any other links.
				 */
				if (sb.sb_nlink > 1)
					(void) linkfrom(name, &sb);
			}
			if ((ar_format == TAR || ar_format == PAX) &&
			    (pad = sb.sb_size % BLOCKSIZE) != 0) {
				pad = BLOCKSIZE - pad;
				(void) buf_skip((OFFSET) pad);
			}
		} else {
			if (((ar_format == TAR || ar_format == PAX) ?
			    buf_skip(ROUNDUP((OFFSET) sb.sb_size, BLOCKSIZE)) :
			    buf_skip((OFFSET) sb.sb_size)) < 0) {
				warn(name, gettext("File data is corrupt"));
			}
		}
#if defined(O_XATTR)
next:
		if (sb.xattr_info.xattrhead != NULL) {
			init_xattr_info(&sb);
		}
#endif
		free(new_name);
		Hiddendir = 0;
		xcont = 1;
	}

	if (my_cwd != -1) {
		rest_cwd(my_cwd);
	}

	close_archive();

	if (!f_list)
		reset_directories();
}


/*
 * get_header - figures which type of header needs to be read.
 *
 * DESCRIPTION
 *
 *	This is merely a single entry point for the two types of archive
 *	headers which are supported.  The correct header is selected
 *	depending on the archive type.
 *
 * PARAMETERS
 *
 *	char	**name	- name of the file (passed to header routine)
 *	size_t	*namesz	- size of 'name' buffer
 *	Stat	*asb	- Stat block for the file (passed to header routine)
 *
 * RETURNS
 *
 *	Returns the value which was returned by the proper header
 *	function.
 */


int
get_header(char **name, size_t *namesz, Stat *asb)
{
#if defined(O_XATTR)
	if (asb->xattr_info.xattrhead != (struct xattr_hdr *)NULL) {
		init_xattr_info(asb);
	}
#endif
	if (asb->linkname != NULL) {
		free(asb->linkname);
	}
	Hiddendir = 0;
	(void) memset(asb, 0, sizeof (*asb));
	if (ar_format == TAR || ar_format == PAX)
		return (readtar(name, namesz, asb));
	else
		return (readcpio(name, namesz, asb));
}


/*
 * readtar - read a tar header
 *
 * DESCRIPTION
 *
 *	Tar_head read a tar format header from the archive.  The name
 *	and asb parameters are modified as appropriate for the file listed
 *	in the header.   Name is assumed to be a pointer to an array of
 *	at least PATH_MAX+1 bytes.
 *
 * PARAMETERS
 *
 *	char	**name 	- name of the file for which the header is
 *			  for.  This is modified and passed back to
 *			  the caller.
 *	size_t	*namesz - size of 'name' buffer
 *	Stat	*asb	- Stat block for the file for which the header
 *			  is for.  The fields of the stat structure are
 *			  extracted from the archive header.  This is
 *			  also passed back to the caller.
 *
 * RETURNS
 *
 *	Returns 0 if a valid header was found, or -1 if EOF is
 *	encountered.
 */


static int
readtar(char **name, size_t *namesz, Stat *asb)
{
	int		status = HDR_FIRSTREC;	/* Initial status */
	static int	prev_status;

	for (;;) {
		prev_status = status;
		if (f_pax) {		/* Process extended header records. */

			/*
			 * Process extended header records.  For an xustar hdr,
			 * this would be typeflag 'X' extended header.  For pax
			 * hdr, this could be either a typeflag 'g' or a
			 * typeflag 'x' extended header.
			 */
			if (((status = get_xdata()) != HDR_EOF) &&
			    (status != HDR_ZEROREC)) {
				if (f_stdpax) {
					if (xcont) {
						/*
						 * We processed a typeflag 'g'
						 * header, now we need to check
						 * for a typeflag 'x' header.
						 * (If we had processed a
						 * typeflag 'x' ext hdr, the
						 * xcont flag would be set to
						 * false.
						 */
						if (((status = get_xdata()) !=
						    HDR_EOF) &&
						    (status != HDR_ZEROREC)) {
							status = read_header(
							    name, namesz,
							    asb, status);
						}
					} else {
						status = read_header(
						    name, namesz, asb, status);
					}
				} else {
					status = read_header(name, namesz,
					    asb, status);
				}
			}
		} else {
			status = read_header(name, namesz, asb,
			    HDR_OK);
		}
		switch (status) {

		case HDR_OK:		/* Valid header */
			return (0);

		case HDR_ERROR:		/* Invalid header */
			switch (prev_status) {

			case HDR_FIRSTREC:	/* Error on first record */
				warn(ar_file, gettext(
				    "This doesn't look like a tar archive"));
				/* FALLTHRU */

			case HDR_ZEROREC:	/* Error after rec. of zeroes */
			case HDR_OK:		/* Error after header rec */
			case HDR_ERROR:		/* Error after error */
			case HDR_NOXHDR:	/* Error after Xhdr error */
				warn(ar_file, gettext(
				    "Skipping to next file..."));
				/* FALLTHRU */

			default:
				break;

			}
			break;

		case HDR_OERROR:		/* Header override error */
			(void) name_match(*name,
			    ((asb->sb_mode & S_IFMT) == S_IFDIR));
				/* FALLTHRU */
		case HDR_ZEROREC:		/* Record of zeroes */
		case HDR_EOF:			/* End of archive */
		default:
			return (-1);
		}
	}
}


/*
 * readcpio - read a CPIO header
 *
 * DESCRIPTION
 *
 *	Read in a cpio header.  Understands how to determine and read ASCII,
 *	binary and byte-swapped binary headers.  Quietly translates
 *	old-fashioned binary cpio headers (and arranges to skip the possible
 *	alignment byte). Returns zero if successful, -1 upon archive trailer.
 *
 * PARAMETERS
 *
 *	char	**name 	- name of the file for which the header is
 *			  for.  This is modified and passed back to
 *			  the caller.
 *	size_t	*namesz - size of the 'name' buffer
 *	Stat	*asb	- Stat block for the file for which the header
 *			  is for.  The fields of the stat structure are
 *			  extracted from the archive header.  This is
 *			  also passed back to the caller.
 *
 * RETURNS
 *
 *	Returns 0 if a valid header was found, or -1 if EOF is
 *	encountered.
 */


static int
readcpio(char **name, size_t *namesz, Stat *asb)
{
	OFFSET		skipped;
	char		magic[M_STRLEN];
	static int	align;
	char		*namep = *name;

top:
	if (align > 0)
		(void) buf_skip((OFFSET) align);
	align = 0;
	for (;;) {
		if (f_append)
			lastheader = bufidx;	/* remember for backup */
		(void) buf_read(magic, M_STRLEN);
		if (savemagic != NULL) {
			free(savemagic);
		}
		savemagic = s_calloc(M_STRLEN + 1);
		(void) strncpy(savemagic, magic, M_STRLEN);
		skipped = 0;
		while ((align = inascii(magic, *name, asb)) < 0 &&
		    (align = inbinary(magic, *name, asb)) < 0 &&
		    (align = inswab(magic, *name, asb)) < 0) {
			if (++skipped == 1) {
				if (total - sizeof (magic) == 0) {
					fatal(gettext(
					    "Unrecognizable archive"));
				}
				warnarch(gettext("Bad magic number"),
				    (OFFSET) sizeof (magic));
				if ((*name)[0]) {
					warn(*name, gettext("May be corrupt"));
				}
			}
			(void) memcpy(magic, magic + 1, sizeof (magic) - 1);
			(void) buf_read(magic + sizeof (magic) - 1, 1);
		}
		if (skipped) {
			warnarch(gettext("Apparently resynchronized"),
			    (OFFSET) sizeof (magic));
			warn(*name, gettext("Continuing"));
		}
#if defined(O_XATTR)
		if (asb->xattr_info.xattraname != NULL &&
		    xattrbadhead == 0) {
			asb->sb_mode = asb->sb_mode & (~_XATTR_CPIO_MODE);
			asb->sb_mode |= attrmode(
			    asb->xattr_info.xattrp->h_typeflag);
		}
#endif
		if (strcmp(*name, TRAILER) == 0)
			return (-1);
		if (nameopt(*name) >= 0)
			break;
		(void) buf_skip((OFFSET) asb->sb_size + align);
	}
#ifdef S_IFLNK
	if ((asb->sb_mode & S_IFMT) == S_IFLNK) {
		if (buf_read(asb->sb_link, (uint_t)asb->sb_size) < 0) {
			warn(*name, gettext("Corrupt symbolic link"));
			return (readcpio(name, namesz, asb));
		}
		asb->sb_link[asb->sb_size] = '\0';
		asb->sb_size = 0;
	}
#endif					/* S_IFLNK */

#if defined(O_XATTR)
	if ((asb->sb_mode & S_IFMT) == _XATTR_CPIO_MODE) {
		if (xattrbadhead == 0) {
			if (read_xattr_hdr(NULL, asb) != 0) {
				(void) fprintf(stderr, gettext(
				    "Unrecognizable extended attribute"
				    " header"));
			}
			*name = namep;
			goto top;
		}
	}
#endif
	if (savename != NULL) {
		free(savename);
	}
	STRDUP(savename, *name);
	asb->sb_atime = -1;		/* the access time will be 'now' */
	if (asb->sb_nlink > 1) {
		if (asb->xattr_info.xattraname != NULL) {
			size_t	asz = strlen(asb->xattr_info.xattraname) + 1;

			if (asb->xattr_info.xattr_linkaname != NULL) {
				free(asb->xattr_info.xattr_linkaname);
			}
			if ((asb->xattr_info.xattr_linkaname = malloc(asz))
			    == NULL) {
				fatal(gettext("out of memory"));
			}
			(void) strlcpy(asb->xattr_info.xattr_linkaname,
			    asb->xattr_info.xattraname, asz);

			if (asb->xattr_info.xattr_linkfname == NULL) {
				size_t	fsz = strlen(
				    asb->xattr_info.xattrfname) + 1;

				if ((asb->xattr_info.xattr_linkfname =
				    malloc(fsz)) == NULL) {
					fatal(gettext("out of memory"));
				}
				(void) strlcpy(
				    asb->xattr_info.xattr_linkfname,
				    asb->xattr_info.xattrfname, fsz);
			}
			namep = asb->xattr_info.xattr_linkfname;
		}
		(void) linkto(namep, asb);
	}

	return (0);
}


/*
 * inswab - read a reversed by order binary header
 *
 * DESCRIPTIONS
 *
 *	Reads a byte-swapped CPIO binary archive header
 *
 * PARMAMETERS
 *
 *	char	*magic	- magic number to match
 *	char	*name	- name of the file which is stored in the header.
 *			  (modified and passed back to caller).
 *	Stat	*asb	- stat block for the file (modified and passed back
 *			  to the caller).
 *
 *
 * RETURNS
 *
 * 	Returns the number of trailing alignment bytes to skip; -1 if
 *	unsuccessful.
 *
 */


static int
inswab(char *magic, char *name, Stat *asb)
{
	ushort_t	namesize;
	uint_t		namefull;
	Binary		binary;
	char		cs[BUFSIZ];

	/* LINTED alignment */
	if (*((ushort_t *)magic) != SWAB(M_BINARY))
		return (-1);
	(void) memcpy((char *)&binary, magic + sizeof (ushort_t),
	    M_STRLEN - sizeof (ushort_t));
	if (buf_read((char *)&binary + M_STRLEN - sizeof (ushort_t),
	    sizeof (binary) - (M_STRLEN - sizeof (ushort_t))) < 0) {
		warnarch(gettext("Corrupt swapped header"),
		    (OFFSET) sizeof (binary) - (M_STRLEN - sizeof (ushort_t)));
		return (-1);
	}
	asb->sb_dev = (dev_t)SWAB(binary.b_dev);
	asb->sb_ino = (ino_t)SWAB(binary.b_ino);
	asb->sb_mode = SWAB(binary.b_mode);
	asb->sb_uid = SWAB(binary.b_uid);
	asb->sb_gid = SWAB(binary.b_gid);
	asb->sb_nlink = SWAB(binary.b_nlink);
	asb->sb_rdev = (dev_t)SWAB(binary.b_rdev);
	asb->sb_mtime = SWAB(binary.b_mtime[0]) << 16 | SWAB(binary.b_mtime[1]);
	asb->sb_size = SWAB(binary.b_size[0]) << 16 | SWAB(binary.b_size[1]);
	if ((namesize = SWAB(binary.b_name)) == 0 ||
	    namesize >= (size_t)PATH_MAX+1) {
		warnarch(gettext("Bad swapped pathname length"),
		    (OFFSET) sizeof (binary) - (M_STRLEN - sizeof (ushort_t)));
		return (-1);
	}
	(void) memset(cs, '\0', sizeof (cs));
	(void) snprintf(cs, sizeof (cs), "%hu", namesize);
	if (savenamesize != NULL) {
		free(savenamesize);
	}
	STRDUP(savenamesize, cs);
	if (buf_read(name, namefull = namesize + namesize % 2) < 0) {
		warnarch(gettext("Corrupt swapped pathname"),
		    (OFFSET)namefull);
		return (-1);
	}
	if (name[namesize - 1] != '\0') {
		warnarch(gettext("Bad swapped pathname"),
		    (OFFSET)namefull);
		return (-1);
	}
	return (asb->sb_size % 2);
}


/*
 * inascii - read in an ASCII cpio header
 *
 * DESCRIPTION
 *
 *	Reads an ASCII format cpio header
 *
 * PARAMETERS
 *
 *	char	*magic	- magic number to match
 *	char	*name	- name of the file which is stored in the header.
 *			  (modified and passed back to caller).
 *	Stat	*asb	- stat block for the file (modified and passed back
 *			  to the caller).
 *
 * RETURNS
 *
 *	Returns zero if successful; -1 otherwise. Assumes that the entire
 *	magic number has been read.
 */


static int
inascii(char *magic, char *name, Stat *asb)
{
	uint_t	namelen;
	char	header[H_STRLEN + 1];
	char	cs[BUFSIZ];
	uint64_t ino64;
	uint64_t size64;

	if (strncmp(magic, M_ASCII, M_STRLEN) != 0)
		return (-1);
	if (buf_read(header, H_STRLEN) < 0) {
		warnarch(gettext("Corrupt ASCII header"), (OFFSET) H_STRLEN);
		return (-1);
	}
	header[H_STRLEN] = '\0';
	if (sscanf(header, H_SCAN, &asb->sb_dev,
	    &ino64, &asb->sb_mode, (uint_t *)&asb->sb_uid,
	    (uint_t *)&asb->sb_gid, &asb->sb_nlink, &asb->sb_rdev,
	    (ulong_t *)&asb->sb_mtime, &namelen, &size64) != H_COUNT) {
		warnarch(gettext("Bad ASCII header"), (OFFSET) H_STRLEN);
		return (-1);
	}
	asb->sb_ino = ino64;
	asb->sb_size = size64;
	(void) memset(cs, '\0', sizeof (cs));
	(void) snprintf(cs, sizeof (cs), "%u", namelen);
	if (savenamesize != NULL) {
		free(savenamesize);
	}
	STRDUP(savenamesize, cs);
	if (namelen == 0 || namelen >= PATH_MAX+1) {
		warnarch(gettext("Bad ASCII pathname length"),
		    (OFFSET) H_STRLEN);
		return (-1);
	}
	if (buf_read(name, namelen) < 0) {
		warnarch(gettext("Corrupt ASCII pathname"),
		    (OFFSET) namelen);
		return (-1);
	}
	if (name[namelen - 1] != '\0') {
		warnarch(gettext("Bad ASCII pathname"),
		    (OFFSET) namelen);
		return (-1);
	}
	return (0);
}


/*
 * inbinary - read a binary header
 *
 * DESCRIPTION
 *
 *	Reads a CPIO format binary header.
 *
 * PARAMETERS
 *
 *	char	*magic	- magic number to match
 *	char	*name	- name of the file which is stored in the header.
 *			  (modified and passed back to caller).
 *	Stat	*asb	- stat block for the file (modified and passed back
 *			  to the caller).
 *
 * RETURNS
 *
 * 	Returns the number of trailing alignment bytes to skip; -1 if
 *	unsuccessful.
 */


static int
inbinary(char *magic, char *name, Stat *asb)
{
	uint_t	namefull;
	Binary	binary;
	char	cs[BUFSIZ];

	/* LINTED alignment */
	if (*((ushort_t *)magic) != M_BINARY) {
		return (-1);
	}
	(void) memcpy((char *)&binary, magic + sizeof (ushort_t),
	    M_STRLEN - sizeof (ushort_t));
	if (buf_read((char *)&binary + M_STRLEN - sizeof (ushort_t),
	    sizeof (binary) - (M_STRLEN - sizeof (ushort_t))) < 0) {
		warnarch(gettext("Corrupt binary header"),
		    (OFFSET) sizeof (binary) - (M_STRLEN - sizeof (ushort_t)));
		return (-1);
	}
	asb->sb_dev = binary.b_dev;
	asb->sb_ino = binary.b_ino;
	asb->sb_mode = binary.b_mode;
	asb->sb_uid = binary.b_uid;
	asb->sb_gid = binary.b_gid;
	asb->sb_nlink = binary.b_nlink;
	asb->sb_rdev = binary.b_rdev;
	asb->sb_mtime = binary.b_mtime[0] << 16 | binary.b_mtime[1];
	asb->sb_size = binary.b_size[0] << 16 | binary.b_size[1];
	if (binary.b_name == 0 || binary.b_name >= (size_t)PATH_MAX+1) {
		warnarch(gettext("Bad binary pathname length"),
		    (OFFSET)sizeof (binary) - (M_STRLEN - sizeof (ushort_t)));
		return (-1);
	}
	if (buf_read(name, namefull = binary.b_name + binary.b_name % 2) < 0) {
		warnarch(gettext("Corrupt binary pathname"), (OFFSET) namefull);
		return (-1);
	}
	if (name[binary.b_name - 1] != '\0') {
		warnarch(gettext("Bad binary pathname"), (OFFSET) namefull);
		return (-1);
	}
	(void) memset(cs, '\0', sizeof (cs));
	(void) snprintf(cs, sizeof (cs), "%hu", binary.b_name);
	if (savenamesize != NULL) {
		free(savenamesize);
	}
	STRDUP(savenamesize, cs);
	return (asb->sb_size % 2);
}


/*
 * reset_directories - reset time/mode on directories we have restored.
 *
 * DESCRIPTION
 *
 *	Walk through the list of directories we have extracted
 *	from the archive, and reset the permissions, the
 *	modification times, the owner and group and if we have it,
 * 		the access times.
 *
 */


static void
reset_directories(void)
{
	Dirlist		*dp;
	mode_t		perm;
	struct timeval	tstamp[2];
	time_t		now;
	char 		*name;

	now = time((time_t)0);		/* cut down on time calls */

	for (dp = dirhead; dp; dp = dp->next) {
		name = dp->name;
		if (f_mode && f_owner) 		/* restore all mode bits */
			perm = dp->perm & (S_IPERM | S_ISUID | S_ISGID);
		else if (f_mode)
			perm = dp->perm & S_IPERM;
		else
			perm = (dp->perm & S_IPOPN) & ~mask; 	/* use umask */
		if (chmod(name, perm) < 0)
			warn(name, strerror(errno));

		if (f_extract_access_time || f_mtime) {
			if (f_extract_access_time &&
			    !(dp->atime.tv_sec == (time_t)-1)) {
				tstamp[0].tv_sec = dp->atime.tv_sec;
				tstamp[0].tv_usec = dp->atime.tv_usec;
			} else {
				tstamp[0].tv_sec = now;
				tstamp[0].tv_usec = 0;
			}
			if (f_mtime) {
				tstamp[1].tv_sec = dp->mtime.tv_sec;
				tstamp[1].tv_usec = dp->mtime.tv_usec;
			} else {
				tstamp[1].tv_sec = now;
				tstamp[1].tv_usec = 0;
			}
			(void) utimes(name, tstamp);
		}

		if (f_owner && (chown(name, dp->uid, dp->gid) < 0)) {
			warn(name, gettext("could not restore owner/group"));
			/* Clear SUID/SGID bits */
			(void) chmod(name, (perm & S_IPERM));
		}
	}
}

#if defined(O_XATTR)
int
read_xattr_hdr(char *hdrbuf, Stat *asb)
{
	off_t		bytes;
	int		comp_len, link_len;
	int		namelen;
	int		parentfilelen;
	char		*tptr, *tptr2;
	char		*xattrapath;
	int		fsz, asz;

	if ((ar_format == TAR) || (ar_format == PAX)) {
		if (hdrbuf[TO_TYPEFLG] != _XATTR_HDRTYPE)
			return (1);
	} else if ((asb->sb_mode & S_IFMT) != _XATTR_CPIO_MODE) {
		return (1);
	}

	/*
	 * Include any padding in read.  We need to be positioned
	 * at the beginning of the next header
	 */

	if ((ar_format == TAR) || (ar_format == PAX)) {
		bytes = ROUNDUP(asb->sb_size, BLOCKSIZE);
	} else {
		bytes = asb->sb_size;
	}

	if (asb->xattr_info.xattrhead != (struct xattr_hdr *)NULL) {
		init_xattr_info(asb);
	}
	if ((asb->xattr_info.xattrhead = malloc((size_t)bytes)) ==
	    (struct xattr_hdr *)NULL) {
		(void) fprintf(stderr, gettext(
		    "Insufficient memory for extended attribute\n"));
		return (1);
	}

	if (buf_read((char *)asb->xattr_info.xattrhead, bytes) != 0) {
		(void) fprintf(stderr, gettext(
		    "Failed to read extended attributes header"));
		return (1);
	}

	/*
	 * Validate that we can handle header format
	 */
	if (strcmp(asb->xattr_info.xattrhead->h_version,
	    XATTR_ARCH_VERS) != 0) {
		(void) fprintf(stderr,
		    gettext("Unknown extended attribute format encountered\n"));
		(void) fprintf(stderr,
		    gettext("Disabling extended attribute header parsing\n"));
		xattrbadhead = 1;
		return (1);
	}
	(void) sscanf(asb->xattr_info.xattrhead->h_component_len,
	    "%10d", &comp_len);
	(void) sscanf(asb->xattr_info.xattrhead->h_link_component_len,
	    "%10d", &link_len);
	asb->xattr_info.xattrp =
	    (struct xattr_buf *)(((char *)asb->xattr_info.xattrhead) +
	    sizeof (struct xattr_hdr));
	(void) sscanf(asb->xattr_info.xattrp->h_namesz, "%7d", &namelen);
	if (link_len > 0)
		asb->xattr_info.xattr_linkp = (struct xattr_buf *)
		    ((int)asb->xattr_info.xattrp + (int)comp_len);
	else
		asb->xattr_info.xattr_linkp = NULL;

	/*
	 * Gather the attribute path from the filename and attrnames section.
	 * The filename and attrnames section can be composed of two or more
	 * path segments separated by a null character.  The first segment
	 * is the path to the parent file that roots the entire sequence in
	 * the normal name space. The remaining segments describes a path
	 * rooted at the hidden extended attribute directory of the leaf file of
	 * the previous segment, making it possible to name attributes on
	 * attributes.
	 */
	parentfilelen = strlen(asb->xattr_info.xattrp->h_names);
	xattrapath = asb->xattr_info.xattrp->h_names + parentfilelen + 1;
	STRDUP(asb->xattr_info.xattraname, xattrapath);
	asz = strlen(xattrapath);
	if ((asz + parentfilelen + 2) < namelen) {
		/*
		 * The attrnames section contains a system attribute on an
		 * attribute.  Replace the null separating the attribute name
		 * from the system attribute name with a '/' so that the
		 * string in xattrapath can be used to display messages with
		 * the full attribute path name rooted at the hidden attribute
		 * directory of the base file in normal name space.
		 */
		asb->xattr_info.xattraparent = asb->xattr_info.xattraname;
		STRDUP(asb->xattr_info.xattraname, xattrapath + asz + 1);
		xattrapath[asz] = '/';
		asz = strlen(xattrapath);
	}
	STRDUP(asb->xattr_info.xattrapath, xattrapath);

	/* Save parent file name */
	fsz = parentfilelen + 1;
	tptr = asb->xattr_info.xattrp->h_names;
	if ((asb->xattr_info.xattrfname = malloc(fsz)) == NULL) {
		fatal(gettext("out of memory"));
	}
	if (strlcpy(asb->xattr_info.xattrfname, tptr, fsz) >= fsz) {
		goto bad;
	}

	/* Save link info */
	if (asb->xattr_info.xattr_linkp) {
		tptr = asb->xattr_info.xattr_linkp->h_names;
		fsz = strlen(tptr) + 1;
		tptr2 = tptr + fsz;
		asz = strlen(tptr2) + 1;

		if ((asb->xattr_info.xattr_linkfname = malloc(fsz))
		    == NULL) {
			fatal(gettext("out of memory"));
		}
		if ((asb->xattr_info.xattr_linkaname = malloc(asz))
		    == NULL) {
			fatal(gettext("out of memory"));
		}
		if (strlcpy(asb->xattr_info.xattr_linkfname,
		    tptr, fsz) >= fsz) {
			goto bad;
		}
		if (strlcpy(asb->xattr_info.xattr_linkaname,
		    tptr2, asz) >= asz) {
			goto bad;
		}
	}
	return (0);
bad:
	(void) fprintf(stderr, gettext("Extended attribute pathname is too"
	    " long, skipping attribute"));
	if (asb->xattr_info.xattrfname != NULL) {
		free(asb->xattr_info.xattrfname);
		asb->xattr_info.xattrfname = NULL;
	}
	if (asb->xattr_info.xattraname != NULL) {
		free(asb->xattr_info.xattraname);
		asb->xattr_info.xattraname = NULL;
	}
	if (asb->xattr_info.xattrapath != NULL) {
		free(asb->xattr_info.xattrapath);
		asb->xattr_info.xattrapath = NULL;
	}
	if (asb->xattr_info.xattr_linkfname != NULL) {
		free(asb->xattr_info.xattr_linkfname);
		asb->xattr_info.xattr_linkfname = NULL;
	}
	if (asb->xattr_info.xattr_linkaname != NULL) {
		free(asb->xattr_info.xattr_linkaname);
		asb->xattr_info.xattr_linkaname = NULL;
	}
	return (1);
}
#else
int
read_xattr_hdr(char *hdrbuf, Stat *asb)
{
	return (0);
}
#endif

static mode_t
attrmode(char type)
{
	mode_t mode;

	switch (type) {
	case '\0':
	case '1':
	case '0':
		mode = S_IFREG;
		break;

	case '2':
		mode = S_IFLNK;
		break;

	case '3':
		mode = S_IFCHR;
		break;
	case '4':
		mode = S_IFBLK;
		break;
	case '5':
	case 'E':
		mode = S_IFDIR;
		break;
	case '6':
		mode = S_IFIFO;
		break;
	case '7':
	default:
		mode = 0;
	}

	return (mode);
}
