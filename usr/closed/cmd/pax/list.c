/*
 * Copyright (c) 2005, 2011, Oracle and/or its affiliates. All rights reserved.
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
 * list.c - List all files on an archive
 *
 * DESCRIPTION
 *
 *	These function are needed to support archive table of contents and
 *	verbose mode during extraction and creation of achives.
 *
 * AUTHOR
 *
 *	Mark H. Colburn, NAPS International (mark@jhereg.mn.org)
 *
 * Sponsored by The USENIX Association for public distribution.
 */

/* Headers */

/*
 * Note: We need to get the right version of mkdev() from libc, therefore
 * include <sys/types.h> and <sys/mkdev.h> rather than <sys/sysmacros.h>.
 */
#include <sys/param.h>
#include <pwd.h>
#include <grp.h>
#include <sys/types.h>
#include <sys/mkdev.h>
#include <stdlib.h>
#include <ctype.h>
#include <iconv.h>
#include <assert.h>
#include <langinfo.h>
#include <libgen.h>
#include <archives.h>
#include "pax.h"

/* Defines */

/*
 * isodigit returns non zero iff argument is an octal digit, zero otherwise
 */
#define	ISODIGIT(c)	(((c) >= '0') && ((c) <= '7'))

/*
 *  MAX_MONTH is the size needed to accommodate the month-name
 *  abbreviation in any locale (+ 1 for the trailing NUL).  If a value is
 *  defined by the standard at some point, it should be substituted for
 *  this.
 */

#define	MAX_MONTH	6

/* format string token types */
#define	TOK_PERCENT	0
#define	TOK_CONVSPEC	1
#define	TOK_TXT		2
#define	TOK_POSITION	3
#define	TOK_KEYWORD	4

/* format conversion specifier types */
#define	CONV_D		'D'
#define	CONV_F		'F'
#define	CONV_L		'L'
#define	CONV_M		'M'
#define	CONV_T		'T'

/*
 * Date and time formats
 * b --- abbreviated month name
 * e --- day number
 * H --- hour(24-hour version)
 * M --- minute
 * Y --- year in the form ccyy
 */
#define	FORMAT1	"%b %e %H:%M %Y"

#define	RPAREN ")"
#define	ADDTOIND(i, s)	(void) strlcat(i, s, MAXSTRLEN)
#define	ISPCONVSPEC(c)	((c == CONV_D) || (c == CONV_F) || \
			(c == CONV_L) || (c == CONV_M) || \
			(c == CONV_T))

typedef	int	ttype_t;

typedef struct titem {
	char		*t_str;
	ttype_t		t_type;
	struct titem	*t_last;
	struct titem	*t_next;
} titem_t;

static struct keylist_pair {
	int keynum;
	char *keylist;
} keylist_pair[] = {	_X_DEVMAJOR, "SUN.devmajor",
				_X_DEVMINOR, "SUN.devminor",
				_X_GID, "gid",
				_X_GNAME, "gname",
				_X_LINKPATH, "linkpath",
				_X_PATH, "path",
				_X_SIZE, "size",
				_X_UID, "uid",
				_X_UNAME, "uname",
				_X_ATIME, "atime",
				_X_MTIME, "mtime",
				_X_CHARSET, "charset",
				_X_COMMENT, "comment",
				_X_REALTIME, "realtime.",
				_X_SECURITY, "security.",
				_X_HOLESDATA, "SUN.holesdata",
				_X_LAST, "NULL" };
static struct listfmt_pair {
	int fmtnum;
	char *fmtlist;
} listfmt_pair[] = {	/* [X,x,g]ustar header */
			_L_X_DEVMAJOR, "SUN.devmajor",
			_L_X_DEVMINOR, "SUN.devminor",
			_L_GID, "gid",
			_L_GNAME, "gname",
			_L_X_LINKPATH, "linkpath",
			_L_X_PATH, "path",
			_L_SIZE, "size",
			_L_UID, "uid",
			_L_UNAME, "uname",
			_L_X_ATIME, "atime",
			_L_MTIME, "mtime",
			_L_X_HOLESDATA, "SUN.holesdata",

			/* [x,g]ustar header */
			_L_X_CHARSET, "charset",
			_L_X_COMMENT, "comment",
			_L_X_REALTIME, "realtime.",
			_L_X_SECURITY, "security.",

			/* ustar header */
			_L_U_NAME, "name",
			_L_U_MODE, "mode",
			_L_U_CHKSUM, "chksum",
			_L_U_TYPEFLAG, "typeflag",
			_L_U_LINKNAME, "linkname",
			_L_U_MAGIC, "magic",
			_L_U_VERSION, "version",
			_L_U_DEVMAJOR, "devmajor",
			_L_U_DEVMINOR, "devminor",
			_L_U_PREFIX, "prefix",

			/* cpio archive */
			_L_C_MAGIC, "c_magic",
			_L_C_DEV, "c_dev",
			_L_C_INO, "c_ino",
			_L_C_MODE, "c_mode",
			_L_C_UID, "c_uid",
			_L_C_GID, "c_gid",
			_L_C_NLINK, "c_nlink",
			_L_C_RDEV, "c_rdev",
			_L_C_MTIME, "c_mtime",
			_L_C_NAMESIZE, "c_namesize",
			_L_C_FILESIZE, "c_filesize",
			_L_C_NAME, "c_name",
			_L_C_FILEDATA, "c_filedata",

			_L_LAST, "NULL" };

/* Function Prototypes */
static void add_fmt_token(titem_t **fptr, char *tstr, int ttype);
static void add_ind_token(titem_t **fptr, char *iptr, char *indicator);
static char *build_path(const char *, const char *, Stat *);
static int cksum(char *hdrbuf);
static void cpio_entry(char *, Stat *);
static OFFSET from_oct(int digs, char *where);
static void get_xtime(char *value, timestruc_t *xtime);
static void handle_invconv(titem_t *fptr);
static void init_globals(void);
static int key_to_val(const char *, const char *, const char *,
    char **, size_t *, Stat *);
static void pax_entry(char *, Stat *);
static int preserve_esc(int, char *, int);
static void print_conv(char *name, char *kword,
    const char *indicator, char ctype, Stat *asb);
static void print_format(char *, Stat *);
static char *print_mode(ushort_t);
static int set_format(titem_t ***fmt);
static void s_realloc(char **, size_t, size_t *);
static void strsplit(const char *, char *, char *, char);
static void tar_entry(char *, Stat *);
static int utf8_local(char *, char **, char *, const char *, int);
static int valid_keyword(const char *kword);

char *
s_calloc(size_t size)
{
	char *new_string;

	if ((new_string = calloc(size, sizeof (char))) == NULL) {
		fatal(gettext("out of memory"));
	}
	return (new_string);
}

static void
s_realloc(char **str, size_t new, size_t *current)
{
	if (new > *current) {
		*current = new;
		if ((*str = realloc(*str, *current)) == NULL) {
			fatal(gettext("out of memory"));
		}
	}
}

static void
init_globals(void)
{
	saverecsum = 0;
	savetypeflag = '\0';
	if (savename != NULL) {
		free(savename);
		savename = NULL;
	}
	if (saveprefix != NULL) {
		free(saveprefix);
		saveprefix = NULL;
	}
	if (savenamesize != NULL) {
		free(savenamesize);
		savenamesize = NULL;
	}
	if (savemagic != NULL) {
		free(savemagic);
		savemagic = NULL;
	}
}

static int is_numeric_key;

/*
 * read_header - read a header record
 *
 * DESCRIPTION
 *
 * 	Read a record that's supposed to be a header record. Return its
 *	address in "head", and if it is good, the file's size in
 *	asb->sb_size.  Decode things from a file header record into a "Stat".
 *	Also set "head_standard" to !=0 or ==0 depending whether header record
 *	is "Unix Standard" tar format or regular old tar format.
 *
 *	If there is an extended header keyword/value override, it is used in
 *	place of the value encountered for that keyword in the header record.
 *
 * PARAMETERS
 *
 *	char   **name		- pointer which will contain name of file
 *	size_t *namesz		- size of 'name' buffer
 *	Stat   *asb		- pointer which will contain stat info
 *	int    status		- status while reading the header buffer
 *
 * RETURNS
 *
 * 	Return 1 for success, 0 if the checksum is bad, EOF on eof, 2 for a
 * 	record full of zeros (EOF marker).
 */
int
read_header(char **name, size_t *namesz, Stat *asb, int status)
{
	long	sum;
	long	recsum;
	char	hdrbuf[BLOCKSIZE];
	char	*templink = NULL;
	size_t	tsz = 0;
	dev_t	majornum, minornum;

	(void) memset((char *)asb, 0, sizeof (Stat));

top:
	if (f_append)
		lastheader = bufidx;		/* remember for backup */

	/*
	 * Ensure globals are reset/reinitialized.
	 */
	init_globals();

	/*
	 * read the header from the buffer, except if status is HDR_NOXHDR,
	 * because then the record has already been read by get_xdata,
	 * which determined that it was not an extended header.
	 */
	if (status != HDR_NOXHDR) {
		if (buf_read(hdrbuf, BLOCKSIZE) != 0)
			return (HDR_EOF);
	} else {
		/*
		 * If we read the header in get_xdata, then we should
		 * use it.
		 */
		if (f_stdpax && (shdrbuf != NULL) && (*shdrbuf != '\0')) {
			(void) memcpy(hdrbuf, shdrbuf, BLOCKSIZE);
			free(shdrbuf);
			shdrbuf = NULL;
		}
	}


	/* merge extended header override data */
	if (f_stdpax) {
		if ((get_oxhdrdata() == HDR_OK) &&
		    (get_oghdrdata() == HDR_OK)) {
			merge_xhdrdata();
		} else {
			return (HDR_OERROR);
		}
	}

	/*
	 * Construct the file name from the prefix and name fields, if
	 * necessary.
	 */
	if (f_stdpax) {
		if (ohdr_flgs & _X_PATH) {
			s_realloc(name, strlen(oXtarhdr.x_path) + 1, namesz);
			(void) strlcpy(*name, oXtarhdr.x_path, *namesz);
		} else {
			if (hdrbuf[TO_PREFIX] != '\0') {
				(void) strncpy(*name, &hdrbuf[TO_PREFIX],
				    TL_PREFIX);
				(*name)[TL_PREFIX] = '\0';
				(void) strcat(*name, "/");
				(void) strncat(*name, &hdrbuf[TO_NAME],
				    TL_NAME);
				(*name)[PATH_MAX] = '\0';
			} else {
				(void) strncpy(*name, &hdrbuf[TO_NAME],
				    TL_NAME);
				(*name)[TL_NAME] = '\0';
			}
		}
	} else if (xhdr_flgs & _X_PATH) {
		(void) strcpy(*name, Xtarhdr.x_path);
	} else {
		if (hdrbuf[TO_PREFIX] != '\0') {
			(void) strncpy(*name, &hdrbuf[TO_PREFIX], TL_PREFIX);
			(*name)[TL_PREFIX] = '\0';
			(void) strcat(*name, "/");
			(void) strncat(*name, &hdrbuf[TO_NAME], TL_NAME);
			(*name)[PATH_MAX] = '\0';
		} else {
			(void) strncpy(*name, &hdrbuf[TO_NAME], TL_NAME);
			(*name)[TL_NAME] = '\0';
		}
	}

	recsum = (long)from_oct(8, &hdrbuf[TO_CHKSUM]);
	if ((sum = cksum(hdrbuf)) == -1)
		return (HDR_ZEROREC);
	if (sum == recsum) {
		/*
		 * Good record.  Decode file size and return.
		 */
		saverecsum = recsum;
		if (hdrbuf[TO_TYPEFLG] != LNKTYPE) {
			if (f_stdpax) {
				if (ohdr_flgs & _X_SIZE) {
					asb->sb_size = oXtarhdr.x_filesz;
				} else {
					asb->sb_size = (OFFSET) from_oct(1 + 12,
					    &hdrbuf[TO_SIZE]);
				}
			} else if (xhdr_flgs & _X_SIZE) {
				asb->sb_size = Xtarhdr.x_filesz;
			} else {
				asb->sb_size = (OFFSET) from_oct(1 + 12,
				    &hdrbuf[TO_SIZE]);
			}
		}
#if defined(O_XATTR)
		/*
		 * Override typeflag if we have already read the
		 * extended attribute header
		 */
		if ((asb->xattr_info.xattrp != NULL) &&
		    (xattrbadhead == 0)) {
			hdrbuf[TO_TYPEFLG] = asb->xattr_info.xattrp->h_typeflag;
			if (asb->xattr_info.xattraname[0] == '.' &&
			    asb->xattr_info.xattraname[1] == '\0' &&
			    asb->xattr_info.xattrp->h_typeflag == DIRTYPE) {
				Hiddendir = 1;
			} else {
				Hiddendir = 0;
			}
		}
#endif
		if (f_stdpax) {
			if (ohdr_flgs & _X_MTIME) {
				asb->sb_stat.st_mtim.tv_sec =
				    oXtarhdr.x_mtime.tv_sec;
				asb->sb_stat.st_mtim.tv_nsec =
				    oXtarhdr.x_mtime.tv_nsec;
			} else {
				asb->sb_stat.st_mtim.tv_sec =
				    (time_t)from_oct(1 + 12, &hdrbuf[TO_MTIME]);
				asb->sb_stat.st_mtim.tv_nsec = 0;
			}
		} else if (xhdr_flgs & _X_MTIME) {
			asb->sb_stat.st_mtim.tv_sec = Xtarhdr.x_mtime.tv_sec;
			asb->sb_stat.st_mtim.tv_nsec = Xtarhdr.x_mtime.tv_nsec;
		} else {
			asb->sb_stat.st_mtim.tv_sec =
			    (time_t)from_oct(1 + 12, &hdrbuf[TO_MTIME]);
			asb->sb_stat.st_mtim.tv_nsec = 0;
		}
		asb->sb_mode = (mode_t)from_oct(8, &hdrbuf[TO_MODE]);
		if (f_stdpax) {
			if (ohdr_flgs & _X_ATIME) {
				asb->sb_stat.st_atim.tv_sec =
				    oXtarhdr.x_atime.tv_sec;
				asb->sb_stat.st_atim.tv_nsec =
				    oXtarhdr.x_atime.tv_nsec;
			} else {
				/* access time will be 'now' */
				asb->sb_atime = -1;
			}
		} else if (xhdr_flgs & _X_ATIME) {
			asb->sb_stat.st_atim.tv_sec = Xtarhdr.x_atime.tv_sec;
			asb->sb_stat.st_atim.tv_nsec = Xtarhdr.x_atime.tv_nsec;
		} else {
			asb->sb_atime = -1;	/* access time will be 'now' */
		}
		/*
		 * Save off info for -v -o listopt= which can't be obtained
		 * from asb.
		 */
		savetypeflag = hdrbuf[TO_TYPEFLG];
		savemagic = s_calloc(TL_MAGIC + 1);
		(void) strncpy(savemagic, &hdrbuf[TO_MAGIC], TL_MAGIC);
		saveversion[0] = hdrbuf[TO_VERSION];
		saveversion[1] = hdrbuf[TO_VERSION + 1];
		saveversion[2] = '\0';
		if (hdrbuf[TO_PREFIX] != '\0') {
			saveprefix = s_calloc(TL_PREFIX + 1);
			(void) strncpy(saveprefix, &hdrbuf[TO_PREFIX],
			    TL_PREFIX);
		}
		savename = s_calloc(TL_NAME + 1);
		(void) strncpy(savename, &hdrbuf[TO_NAME], TL_NAME);
		if (strcmp(&hdrbuf[TO_MAGIC], TMAGIC) == 0) {
			uid_t duid;
			gid_t dgid;
			mode_t inmode = asb->sb_mode;

			/* Unix Standard tar archive */

			head_standard = 1;

			if (f_stdpax) {
				if (ohdr_flgs & _X_UID) {
					asb->sb_uid = oXtarhdr.x_uid;
				} else {
					asb->sb_uid = (uid_t)from_oct(8,
					    &hdrbuf[TO_UID]);
				}
			} else if (xhdr_flgs & _X_UID) {
				asb->sb_uid = Xtarhdr.x_uid;
			} else {
				asb->sb_uid = (uid_t)from_oct(8,
				    &hdrbuf[TO_UID]);
			}
			if (f_stdpax) {
				if (ohdr_flgs & _X_GID) {
					asb->sb_gid = oXtarhdr.x_gid;
				} else {
					asb->sb_gid = (gid_t)from_oct(8,
					    &hdrbuf[TO_GID]);
				}
			} else if (xhdr_flgs & _X_GID) {
				asb->sb_gid = Xtarhdr.x_gid;
			} else {
				asb->sb_gid = (gid_t)from_oct(8,
				    &hdrbuf[TO_GID]);
			}

			if (f_stdpax) {
				if (ohdr_flgs & _X_UNAME) {
					duid = finduid(oXtarhdr.x_uname);
				} else {
					duid = finduid(&hdrbuf[TO_UNAME]);
				}
			} else if (xhdr_flgs & _X_UNAME) {
				duid = finduid(Xtarhdr.x_uname);
			} else {
				duid = finduid(&hdrbuf[TO_UNAME]);
			}
			if (f_stdpax) {
				if (ohdr_flgs & _X_GNAME) {
					dgid = findgid(oXtarhdr.x_gname);
				} else {
					dgid = findgid(&hdrbuf[TO_GNAME]);
				}
			} else if (xhdr_flgs & _X_GNAME) {
				dgid = findgid(Xtarhdr.x_gname);
			} else {
				dgid = findgid(&hdrbuf[TO_GNAME]);
			}

			switch (hdrbuf[TO_TYPEFLG]) {

			case BLKTYPE:
			case CHRTYPE:
				if (f_stdpax) {
					if (ohdr_flgs & _X_DEVMAJOR) {
						majornum = oXtarhdr.x_devmajor;
					} else {
						majornum = (dev_t)from_oct(8,
						    &hdrbuf[TO_DEVMAJOR]);
					}
				} else if (xhdr_flgs & _X_DEVMAJOR) {
					majornum = Xtarhdr.x_devmajor;
				} else {
					majornum = (dev_t)from_oct(8,
					    &hdrbuf[TO_DEVMAJOR]);
				}
				if (f_stdpax) {
					if (ohdr_flgs & _X_DEVMINOR) {
						minornum = oXtarhdr.x_devminor;
					} else {
						minornum = (dev_t)from_oct(8,
						    &hdrbuf[TO_DEVMINOR]);
					}
				} else if (xhdr_flgs & _X_DEVMINOR) {
					minornum = Xtarhdr.x_devminor;
				} else {
					minornum = (dev_t)from_oct(8,
					    &hdrbuf[TO_DEVMINOR]);
				}
				asb->sb_rdev = makedev(majornum, minornum);
				break;

			case REGTYPE:
			case AREGTYPE:
			case CONTTYPE:
				/*
				 * In the case where we archived a setuid or
				 * setgid program owned by a uid or gid too big
				 * to fit in the format, and the name service
				 * doesn't recognise the username or groupname,
				 * we have * to make sure that we don't
				 * accidentally create a setuid or setgid
				 * nobody file.
				 */
				if ((asb->sb_mode & S_ISUID) == S_ISUID &&
				    duid == (uid_t)-1 &&
				    asb->sb_uid == UID_NOBODY)
					asb->sb_mode &= ~S_ISUID;
				if ((asb->sb_mode & S_ISGID) == S_ISGID &&
				    dgid == (gid_t)-1 &&
				    asb->sb_gid == GID_NOBODY)
					asb->sb_mode &= ~S_ISGID;
				/*
				 * We're required to warn the user if they were
				 * expecting modes to be preserved!
				 */
				if (f_mode && inmode != asb->sb_mode)
					warn(*name, gettext(
					    "unable to preserve "
					    "setuid/setgid mode"));
				break;

			case _XATTR_HDRTYPE:
				if (xattrbadhead == 0) {
					if (read_xattr_hdr(hdrbuf, asb) != 0) {
						warn(*name,
						    gettext("Failed to read "
						    "extended attribute"
						    " header"));
					}
					goto top;

					/* NOTREACHED */
					break;
				}
			default:
				/* do nothing */
				break;
			}

			if (duid != (uid_t)-1)
				asb->sb_uid = duid;
			if (dgid != (gid_t)-1)
				asb->sb_gid = dgid;
		} else {
			/* Old fashioned tar archive */
			head_standard = 0;
			asb->sb_uid = (uid_t)from_oct(8, &hdrbuf[TO_UID]);
			asb->sb_gid = (gid_t)from_oct(8, &hdrbuf[TO_GID]);
		}

		switch (hdrbuf[TO_TYPEFLG]) {

		case REGTYPE:
		case AREGTYPE:
			/*
			 * Berkeley tar stores directories as regular files
			 * with a trailing /
			 */
			if ((*name)[strlen(*name) - 1] == '/') {
				(*name)[strlen(*name) - 1] = '\0';
				asb->sb_mode |= S_IFDIR;
			} else
				asb->sb_mode |= S_IFREG;
			break;

		case LNKTYPE:
			asb->sb_nlink = 2;
			/*
			 * We need to save the linkname so that it is available
			 * later when we have to search the link chain for
			 * this link.
			 */
			if (asb->linkname != NULL) {
				free(asb->linkname);
				asb->linkname = NULL;
			}
			if (f_stdpax) {
				if (ohdr_flgs & _X_LINKPATH) {
					asb->linkname =
					    mem_rpl_name(oXtarhdr.x_linkpath);

					/* don't use linkname here */
					(void) linkto(oXtarhdr.x_linkpath, asb);
				} else {
					if (templink != NULL) {
						free(templink);
					}
					/* Need null-terminated entry */
					tsz = TL_LINKNAME + 1;
					templink = s_calloc(tsz);
					(void) strncpy(templink,
					    &hdrbuf[TO_LINKNAME],
					    TL_LINKNAME);
					asb->linkname = mem_rpl_name(
					    templink);
					/* don't use linkname here */
#if defined(O_XATTR)
					if ((asb->xattr_info.xattr_linkaname
					    != NULL) &&
					    (asb->xattr_info.xattr_linkfname
					    != NULL)) {
					s_realloc(&templink, strlen(
					    asb->xattr_info.xattr_linkfname)
					    + 1, &tsz);
					(void) strlcpy(templink,
					    asb->xattr_info.xattr_linkfname,
					    tsz);
					}
#endif
					if (templink != NULL) {
						(void) linkto(templink, asb);
					}
				}
			} else if (xhdr_flgs & _X_LINKPATH) {
				asb->linkname =
				    mem_rpl_name(Xtarhdr.x_linkpath);
				/* don't use linkname here */
				(void) linkto(Xtarhdr.x_linkpath, asb);
			} else {
				if (templink != NULL) {
					free(templink);
				}
				/* Need null-terminated entry */
				tsz = TL_LINKNAME + 1;
				templink = s_calloc(tsz);
				(void) strncpy(templink, &hdrbuf[TO_LINKNAME],
				    TL_LINKNAME);
				asb->linkname = mem_rpl_name(templink);
				/* don't use linkname here */
#if defined(O_XATTR)
				if (asb->xattr_info.xattr_linkaname
				    != NULL) {
					s_realloc(&templink, strlen(
					    asb->xattr_info.xattr_linkfname)
					    + 1, &tsz);
					(void) strlcpy(templink,
					    asb->xattr_info.xattr_linkfname,
					    tsz);
				}
#endif
				(void) linkto(templink, asb);
			}

#if defined(O_XATTR)
			if (asb->xattr_info.xattr_linkfname != NULL)
				(void) linkto(asb->xattr_info.xattr_linkfname,
				    asb);
			else
#endif
				(void) linkto(*name, asb);
			asb->sb_mode |= S_IFREG;
			break;

		case BLKTYPE:
			asb->sb_mode |= S_IFBLK;
			break;

		case CHRTYPE:
			asb->sb_mode |= S_IFCHR;
			break;

		case DIRTYPE:
			/* Remove trailing slash */
			if ((*name)[strlen(*name) - 1] == '/') {
				(*name)[strlen(*name) - 1] = '\0';
			}
			asb->sb_mode |= S_IFDIR;
			break;

#ifdef S_IFLNK
		case SYMTYPE:
			asb->sb_mode |= S_IFLNK;
			if (f_stdpax) {
				(void) strncpy(asb->sb_link,
				    &hdrbuf[TO_LINKNAME], TL_LINKNAME);
				asb->sb_link[TL_LINKNAME] = '\0';
			} else if (xhdr_flgs & _X_LINKPATH) {
				(void) strcpy(asb->sb_link,
				    Xtarhdr.x_linkpath);
			} else {
				(void) strncpy(asb->sb_link,
				    &hdrbuf[TO_LINKNAME], TL_LINKNAME);
				asb->sb_link[TL_LINKNAME] = '\0';
			}
			break;
#endif

#ifdef S_IFIFO
		case FIFOTYPE:
			asb->sb_mode |= S_IFIFO;
			break;
#endif

		/*
		 * The UNIX2003 specification states that a typeflag of '7'
		 * (CONTTYPE) is reserved to represent a file to which an
		 * implementation has associated some high-performance
		 * attribute.  Implementations without such extensions
		 * should treat this file as a regular file (type 0).
		 */
		case CONTTYPE:
#ifdef S_IFCTG
			asb->sb_mode |= S_IFCTG;
#else
			asb->sb_mode |= S_IFREG;
#endif
			break;
		}
		savetypeflag = hdrbuf[TO_TYPEFLG];
		if (templink != NULL) {
			free(templink);
		}
		return (HDR_OK);
	}
	warn(gettext("checksum error on header record"), *name);
	if (templink != NULL) {
		free(templink);
	}
	return (HDR_ERROR);
}


/*
 * print_entry - print a single table-of-contents entry
 *
 * DESCRIPTION
 *
 *	print_entry prints a single line of file information.  The format
 *	of the line is the same as that used by the LS command.  For some
 *	archive formats, various fields may not make any sense, such as
 *	the link count on tar archives.  No error checking is done for bad
 *	or invalid data.
 *
 * PARAMETERS
 *
 *	char   *name		- pointer to name to print an entry for
 *	Stat   *asb		- pointer to the stat structure for the file
 */


void
print_entry(char *name, Stat *asb)
{
	switch (ar_interface) {
	case TAR:
		tar_entry(name, asb);
		break;

	case CPIO:
		cpio_entry(name, asb);
		break;

	case PAX: pax_entry(name, asb);
		break;
	}
}


/*
 * cpio_entry - print a verbose cpio-style entry
 *
 * DESCRIPTION
 *
 *	cpio_entry prints a single line of file information.  The format
 *	of the line is the same as that used by the traditional cpio
 *	command.  No error checking is done for bad or invalid data.
 *
 * PARAMETERS
 *
 *	char   *name		- pointer to name to print an entry for
 *	Stat   *asb		- pointer to the stat structure for the file
 */


static void
cpio_entry(char *name, Stat *asb)
{
	struct tm	*atm;
	Link		*from;
	struct passwd	*pwp;
	char		mon[MAX_MONTH]; /* abreviated month name */

	if (f_list && f_verbose) {
		(void) fprintf(msgfile, "%-7lo", asb->sb_mode);
		atm = localtime(&asb->sb_mtime);
		if (pwp = getpwuid(asb->sb_uid)) {
			(void) fprintf(msgfile, "%-6s", pwp->pw_name);
		} else {
			(void) fprintf(msgfile, "%-6u", asb->sb_uid);
		}
		(void) strftime(mon, sizeof (mon), "%b", atm);
		if ((OFFSET)(asb->sb_size) < (OFFSET)(1LL << 31)) {
			(void) fprintf(msgfile,  dcgettext(NULL,
			    "%7lld  %3s %2d %02d:%02d:%02d %4d  ", LC_TIME),
			    (OFFSET)(asb->sb_size), mon,
			    atm->tm_mday, atm->tm_hour, atm->tm_min,
			    atm->tm_sec, atm->tm_year + 1900);
		} else {
			(void) fprintf(msgfile, dcgettext(NULL,
			    "%11lld  %3s %2d %02d:%02d:%02d %4d  ", LC_TIME),
			    (OFFSET)(asb->sb_size), mon,
			    atm->tm_mday, atm->tm_hour, atm->tm_min,
			    atm->tm_sec, atm->tm_year + 1900);
		}

	}
	(void) fprintf(msgfile, "%s", name);
	if ((asb->sb_nlink > 1) && (from = islink(name, asb))) {
		(void) fprintf(msgfile, gettext(" linked to %s"), from->l_name);
	}
#ifdef	S_IFLNK
	if ((asb->sb_mode & S_IFMT) == S_IFLNK) {
		(void) fprintf(msgfile,
		    gettext(" symbolic link to %s"), asb->sb_link);
	}
#endif /* S_IFLNK */
	(void) putc('\n', msgfile);
}


/*
 * tar_entry - print a tar verbose mode entry
 *
 * DESCRIPTION
 *
 *	tar_entry prints a single line of tar file information.  The format
 *	of the line is the same as that produced by the traditional tar
 *	command.  No error checking is done for bad or invalid data.
 *
 * PARAMETERS
 *
 *	char   *name		- pointer to name to print an entry for
 *	Stat   *asb		- pointer to the stat structure for the file
 */


static void
tar_entry(char *name, Stat *asb)
{
	struct tm	*atm;
	int		i;
	int		mode;
	char		*symnam = "NULL";
	Link		*link;
	char		mon[MAX_MONTH];		/* abbreviated month name */

	if ((mode = asb->sb_mode & S_IFMT) == S_IFDIR)
		return;		/* don't print directories */
	if (f_extract) {
		switch (mode) {
#ifdef S_IFLNK
		case S_IFLNK: 	/* This file is a symbolic link */
			i = readlink(name, symnam, PATH_MAX);
			if (i < 0) { /* Could not find symbolic link */
				warn(gettext("can't read symbolic link"),
				    strerror(errno));
			} else { /* Found symbolic link filename */
				symnam[i] = '\0';
				(void) fprintf(msgfile, gettext("x %s "
				    "symbolic link to " "%s\n"), name, symnam);
			}
			break;
#endif
		case S_IFREG: 	/* It is a link or a file */
			if ((asb->sb_nlink > 1) &&
			    (link = islink(name, asb))) {
				(void) fprintf(msgfile, gettext(
				    "%s linked to %s\n"), name, link->l_name);
			} else {
				(void) fprintf(msgfile, gettext(
				    "x %s, %lld bytes, %lld tape blocks\n"),
				    name, (OFFSET)(asb->sb_size),
				    ROUNDUP((OFFSET)(asb->sb_size),
				    BLOCKSIZE) / BLOCKSIZE);
			}
		}
	} else if (f_append || f_create) {
		switch (mode) {
#ifdef S_IFLNK
		case S_IFLNK: 	/* This file is a symbolic link */
			i = readlink(name, symnam, PATH_MAX);
			if (i < 0) { /* Could not find symbolic link */
				warn(gettext("can't read symbolic link"),
				    strerror(errno));
			} else { /* Found symbolic link filename */
				symnam[i] = '\0';
				(void) fprintf(msgfile, gettext(
				    "a %s symbolic link to %s\n"),
				    name, symnam);
			}
			break;
#endif

		case S_IFREG: 	/* It is a link or a file */
			(void) fprintf(msgfile, "a %s ", name);
			if ((asb->sb_nlink > 1) && (link = islink(name, asb))) {
				(void) fprintf(msgfile, gettext(
				    "link to %s\n"), link->l_name);
			} else {
				(void) fprintf(msgfile,
				    gettext("%lld Blocks\n"),
				    ROUNDUP((OFFSET)(asb->sb_size), BLOCKSIZE)
				    / BLOCKSIZE);
			}
			break;
		}
	} else if (f_list) {
		if (f_verbose) {
			atm = localtime(&asb->sb_mtime);
			(void) strftime(mon, sizeof (mon), "%b", atm);
			(void) fprintf(msgfile, "%s", print_mode(asb->sb_mode));
			if ((OFFSET)asb->sb_size < (OFFSET)(1LL << 31)) {
				(void) fprintf(msgfile, dcgettext(NULL,
				    " %d/%d %7lld %3s %2d %02d:%02d %4d %s",
				    LC_TIME), asb->sb_uid, asb->sb_gid,
				    (OFFSET)(asb->sb_size), mon, atm->tm_mday,
				    atm->tm_hour, atm->tm_min,
				    atm->tm_year + 1900, name);
			} else {
				(void) fprintf(msgfile, dcgettext(NULL,
				    " %d/%d %11lld %3s %2d %02d:%02d %4d %s",
				    LC_TIME), asb->sb_uid, asb->sb_gid,
				    (OFFSET)(asb->sb_size), mon, atm->tm_mday,
				    atm->tm_hour, atm->tm_min,
				    atm->tm_year + 1900, name);
			}
		} else
			(void) fprintf(msgfile, "%s", name);

		switch (mode) {
#ifdef S_IFLNK
		case S_IFLNK: 	/* This file is a symbolic link */
			i = readlink(name, symnam, PATH_MAX);
			if (i < 0) { /* Could not find symbolic link */
				warn(gettext("can't read symbolic "
				    "link"), strerror(errno));
			} else { /* Found symbolic link filename */
				symnam[i] = '\0';
				(void) fprintf(msgfile, gettext(
				    " symbolic link to %s"), symnam);
			}
			break;
#endif

		case S_IFREG: 	/* It is a link or a file */
			if ((asb->sb_nlink > 1) && (link = islink(name, asb))) {
				(void) fprintf(msgfile, gettext(
				    " linked to %s"), link->l_name);
			}
			break;	/* Do not print out directories */
		}
		(void) fputc('\n', msgfile);
	} else {
		(void) fprintf(msgfile, gettext("? %s %lld blocks\n"), name,
		    ROUNDUP((OFFSET)(asb->sb_size), BLOCKSIZE) / BLOCKSIZE);
	}
}

/*
 * strsplit()
 *
 * Split a character string into two NULL terminated strings using a
 * the specified character separator.
 *
 * Input
 *	s	- character string to split
 *	c	- separator
 *
 * Output
 *	p1	- string on left side of the separator
 *	p2	- string on right side of the separator
 */
static void
strsplit(const char *s, char *p1, char *p2, char c)
{
	*p1 = *p2 = '\0';

	while (*s != '\0' && *s != c) {
		*p1++ = *s++;
	}
	*p1 = '\0';
	s++;

	while (*s != '\0') {
		*p2++ = *s++;
	}
	*p2 = '\0';
}

/*
 * build_path()
 *
 * Builds a path from a comma separated set of keywords
 * For example:
 *	Input String		Output String
 * 	keyword			keyword_val
 *	keyword1,keyword2	keyword1_val/keyword2_val
 *
 * If any portion of the set of keywords cannot be resolved
 * (converted) then NULL is returned, otherwise a string
 * comprised of the converted keyword values separated by
 * '/' is returned.
 */
static char *
build_path(const char *name, const char *kws, Stat *asb)
{
	char *p = NULL;
	char *fstr;
	static char *tmpstr;
	char s1[MAXSTRLEN];
	char s2[MAXSTRLEN];
	size_t dstsize;
	size_t fsize = MAXSTRLEN;
	int done = 0;

	fstr = s_calloc(fsize);
	if (kws != NULL) {
		STRDUP(tmpstr, kws);
		while (!done) {
			/*
			 * Split the first comma separated word (s1)
			 * from the rest (s2).
			 */
			strsplit(tmpstr, s1, s2, ',');

			/* Resolve the component of the path */
			(void) memset(fstr, '\0', fsize);
			if (key_to_val(name, s1, NULL, &fstr,
			    &fsize, asb) < 0) {
				p = NULL;
				done = 1;
			} else {
				/*
				 * If the value of the component is NULL,
				 * skip it.
				 */
				if (fstr[0] != '\0') {
					dstsize = strlen(fstr) + 1;
					if (p == NULL) {
						if ((p = calloc(1,
						    dstsize * sizeof (char)))
						    == NULL) {
							fatal(gettext(
							    "cannot allocate "
							    "space to build "
							    "path"));
						}
						if (strlcpy(p, fstr,
						    dstsize) > dstsize) {
							fatal(gettext(
							    "buffer overflow"));
						}
					} else {
						s_realloc(&p, dstsize +
						    strlen(p) + 1, &dstsize);
						(void) strcat(p, "/");
						if (strlcat(p, fstr,
						    dstsize) > dstsize) {
							fatal(gettext(
							    "buffer overflow"));
						}
					}
				}
			}
			if (s2[0] == '\0') {
				done = 1;
			} else {
				free(tmpstr);
				STRDUP(tmpstr, s2);
			}
		}
		free(tmpstr);
	}
	free(fstr);
	return (p);
}

/*
 * preserve_esc()
 *
 * Preserves escape sequences marked by
 * a backslash.  On return, olist will contain
 * a null terminated result of the preserved
 * escape character sequences.
 *
 * Returns the number of characters processed.
 */
static int
preserve_esc(int lindx, char *olist, int oindx)
{
	int rc = 1;
	int j;
	wchar_t wd;

	/* Preserve escape character sequences */
	if ((listopt[lindx] == '\\') && (listopt[lindx + 1] != '\0')) {
		switch (listopt[lindx + 1]) {
		case 'a':	/* alert */
			olist[oindx] = '\a';
			break;
		case 'b':	/* backspace */
			olist[oindx] = '\b';
			break;
		case 'f':	/* form-feed */
			olist[oindx] = '\f';
			break;
		case 'n':	/* newline */
			olist[oindx] = '\n';
			break;
		case 'r':	/* carriage-return */
			olist[oindx] = '\r';
			break;
		case 't':	/* tab */
			olist[oindx] = '\t';
			break;
		case 'v':	/* vertical tab */
			olist[oindx] = '\v';
			break;
		case '\\':	/* backslash */
			olist[oindx] = '\\';
			break;
		case '0':	/* 0-prefixed octal chars */
		case '1':	/* non-prefixed octal chars */
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
			j = wd = 0;
			while ((listopt[lindx + 1] >= '0') &&
			    (listopt[lindx + 1] <= '7') && j++ < 3) {
				wd <<= 3;
				wd |= (listopt[lindx + 1] - '0');
				lindx++;
			}
			olist[oindx] = (char)wd;
			rc = j;
			break;
		default:
			olist[oindx] = listopt[lindx];
			rc = 0;
		}
	} else {
		olist[oindx] = listopt[lindx];
		rc = 0;
	}
	olist[oindx + 1] = '\0';

	return (rc);
}

/*
 * valid_keyword()
 *
 * Returns then index into the listfmt_pair if the input keyword
 * is a valid keyword specifier, otherwise returns -1.
 *
 */
static int
valid_keyword(const char *kword)
{
	int	k;

	/* step through all keywords looking for a match */
	for (k = 0; listfmt_pair[k].fmtnum != _L_LAST; k++) {
		if (STREQUAL(listfmt_pair[k].fmtlist, kword)) {
			break;
		}
	}

	if (listfmt_pair[k].fmtnum == _L_LAST) {
		/*
		 * keyword is not a valid keyword.
		 */
		return (-1);
	} else {
		return (k);
	}
}

/*
 * key_to_val()
 *
 * Converts a keyword (kword) to its value from the applicable header
 * field or extended header, and returns string (fmtstr)
 * which contains the value without any trailing NULs.
 *
 * Returns 0 if converted successfully, otherwise
 * returns -1.
 */
static int
key_to_val(const char *name, const char *kword, const char *val,
    char **fmtstr, size_t *fmtsize, Stat *asb)
{
	/*
	 * Verify the keyword is one of the supported
	 * keywords.
	 */
	const char	*tmfmt;
	char		num_val[ULONGLONG_MAX_DIGITS + 1];
	char		type_val[2];
	char		time_buf[50];
	int		k;


	/* step through all keywords looking for a match */
	if ((k = valid_keyword(kword)) < 0) {
		return (-1);

	/*
	 * If we are to omit this keyword, just return.
	 */
	} else if (OMITOPT(listfmt_pair[k].fmtlist)) {
		return (0);
	} else {
		/*
		 * We have a valid matching keyword. Now convert it to
		 * its value and place the result in the string fmtstr.
		 * fmtstr will be used later to print to the msgfile.
		 */
		switch (listfmt_pair[k].fmtnum) {
		case _L_X_DEVMAJOR:
			(void) memset(num_val, 0, ULONGLONG_MAX_DIGITS + 1);
			if (ohdr_flgs & _X_DEVMAJOR) {
				(void) sprintf(num_val, (val ? val : "%lu"),
				    oXtarhdr.x_devmajor);
			} else {
				(void) sprintf(num_val, (val ? val : "%d"), 0);
			}
			(void) strlcpy(*fmtstr, num_val, *fmtsize);
			is_numeric_key = 1;
			break;
		case _L_X_DEVMINOR:
			(void) memset(num_val, 0, ULONGLONG_MAX_DIGITS + 1);
			if (ohdr_flgs & _X_DEVMINOR) {
				(void) sprintf(num_val, (val ? val : "%lu"),
				    oXtarhdr.x_devminor);
			} else {
				(void) sprintf(num_val, (val ? val : "%d"), 0);
			}
			(void) strlcpy(*fmtstr, num_val, *fmtsize);
			is_numeric_key = 1;
			break;
		case _L_C_GID:
		case _L_GID:
			(void) memset(num_val, 0, ULONGLONG_MAX_DIGITS + 1);
			(void) sprintf(num_val, (val ? val : "%ld"),
			    asb->sb_gid);
			(void) strlcpy(*fmtstr, num_val, *fmtsize);
			is_numeric_key = 1;
			break;
		case _L_GNAME:
			(void) strlcpy(*fmtstr, findgname(asb->sb_gid),
			    *fmtsize);
			break;
		case _L_X_LINKPATH:
		case _L_U_LINKNAME:
			if (asb->linkname != NULL) {
				s_realloc(fmtstr, strlen(asb->linkname) + 1,
				    fmtsize);
				(void) strlcpy(*fmtstr, asb->linkname,
				    *fmtsize);
			}
			break;
		case _L_X_PATH:
			if (name != NULL) {
				s_realloc(fmtstr, strlen(name) + 1, fmtsize);
				(void) strlcpy(*fmtstr, name, *fmtsize);
			}
			break;
		case _L_C_FILESIZE:
		case _L_SIZE:
			(void) memset(num_val, 0, ULONGLONG_MAX_DIGITS + 1);
			(void) sprintf(num_val, (val ? val : "%llu"),
			    asb->sb_size);
			(void) strlcpy(*fmtstr, num_val, *fmtsize);
			is_numeric_key = 1;
			break;
		case _L_C_UID:
		case _L_UID:
			(void) memset(num_val, 0, ULONGLONG_MAX_DIGITS + 1);
			(void) snprintf(num_val, sizeof (num_val), (val ?
			    val : "%ld"), asb->sb_uid);
			(void) strlcpy(*fmtstr, num_val, *fmtsize);
			is_numeric_key = 1;
			break;
		case _L_UNAME:
			(void) strlcpy(*fmtstr, finduname(asb->sb_uid),
			    *fmtsize);
			break;
		case _L_X_ATIME:
			if (val && *val) {
				tmfmt = val;
			} else {
				tmfmt = dcgettext(NULL, FORMAT1,
				    LC_TIME);
			}
			if (strftime(time_buf, sizeof (time_buf),
			    tmfmt, localtime(&asb->sb_atime)) == 0) {
				return (-1);
			}
			(void) strlcpy(*fmtstr, time_buf, *fmtsize);
			break;
		case _L_C_MTIME:
		case _L_MTIME:
			if (val && *val) {
				tmfmt = val;
			} else {
				tmfmt = dcgettext(NULL, FORMAT1,
				    LC_TIME);
			}
			if (strftime(time_buf, sizeof (time_buf),
			    tmfmt, localtime(&asb->sb_mtime)) == 0) {
				return (-1);
			}
			(void) strlcpy(*fmtstr, time_buf, *fmtsize);
			break;
		case _L_X_CHARSET:
			if (ohdr_flgs & _X_CHARSET) {
				s_realloc(fmtstr, strlen(oXtarhdr.x_charset)
				    + 1, fmtsize);
				(void) strlcpy(*fmtstr, oXtarhdr.x_charset,
				    *fmtsize);
			}
			break;
		case _L_X_COMMENT:
			if (ohdr_flgs & _X_COMMENT) {
				s_realloc(fmtstr, strlen(oXtarhdr.x_comment)
				    + 1, fmtsize);
				(void) strlcpy(*fmtstr, oXtarhdr.x_comment,
				    *fmtsize);
			}
			break;
		case _L_X_REALTIME:
			if (ohdr_flgs & _X_REALTIME) {
				s_realloc(fmtstr, strlen(oXtarhdr.x_realtime)
				    + 1, fmtsize);
				(void) strlcpy(*fmtstr, oXtarhdr.x_realtime,
				    *fmtsize);
			}
			break;
		case _L_X_SECURITY:
			if (ohdr_flgs & _X_SECURITY) {
				s_realloc(fmtstr, strlen(oXtarhdr.x_security)
				    + 1, fmtsize);
				(void) strlcpy(*fmtstr, oXtarhdr.x_security,
				    *fmtsize);
			}
			break;
		case _L_X_HOLESDATA:
			if (ohdr_flgs & _X_HOLESDATA) {
				s_realloc(fmtstr, strlen(oXtarhdr.x_holesdata)
				    + 1, fmtsize);
				(void) strlcpy(*fmtstr, oXtarhdr.x_holesdata,
				    *fmtsize);
			} else if (xhdr_flgs & _X_HOLESDATA) {
				s_realloc(fmtstr, strlen(Xtarhdr.x_holesdata)
				    + 1, fmtsize);
				(void) strlcpy(*fmtstr, Xtarhdr.x_holesdata,
				    *fmtsize);
			}
			break;
		case _L_C_NAME:
		case _L_U_NAME:
			/* name shouldn't ever be null */
			if (name != NULL) {
				s_realloc(fmtstr, strlen(name) + 1,
				    fmtsize);
				(void) strlcpy(*fmtstr, name, *fmtsize);
			} else {
				s_realloc(fmtstr, strlen(savename) + 1,
				    fmtsize);
				(void) strlcpy(*fmtstr, savename, *fmtsize);
			}
			break;
		case _L_C_MODE:
		case _L_U_MODE:
			(void) strlcpy(*fmtstr, print_mode(asb->sb_mode),
			    *fmtsize);
			break;
		case _L_U_CHKSUM:
			(void) memset(num_val, 0, ULONGLONG_MAX_DIGITS + 1);
			(void) sprintf(num_val, (val ? val : "%ld"),
			    saverecsum);
			(void) strlcpy(*fmtstr, num_val, *fmtsize);
			is_numeric_key = 1;
			break;
		case _L_U_TYPEFLAG:
			if (savetypeflag != '\0') {
				type_val[0] = savetypeflag;
				type_val[1] = '\0';
			} else {
				type_val[0] = '\0';
			}
			if (type_val[0] != '\0') {
				(void) strlcpy(*fmtstr, type_val, *fmtsize);
			}
			break;
		case _L_C_MAGIC:
			/*
			 * If this is a portable archive, magic is
			 * "070707".
			 */
			if ((strncmp(savemagic, M_ASCII, M_STRLEN) == 0) ||
			    /* LINTED alignment */
			    (*((ushort_t *)savemagic) == M_BINARY) ||
			    /* LINTED alignment */
			    (*((ushort_t *)savemagic) == SWAB(M_BINARY))) {
				(void) strcpy(*fmtstr, M_ASCII);
			} else {
				(void) strcpy(*fmtstr, savemagic);
			}
			break;
		case _L_U_MAGIC:
			(void) strlcpy(*fmtstr, savemagic, *fmtsize);
			break;
		case _L_U_VERSION:
			(void) strcpy(*fmtstr, saveversion);
			break;
		case _L_U_DEVMAJOR:
			(void) memset(num_val, 0, ULONGLONG_MAX_DIGITS + 1);
			(void) sprintf(num_val, (val ? val : "%lu"),
			    major(asb->sb_rdev));
			(void) strlcpy(*fmtstr, num_val, *fmtsize);
			is_numeric_key = 1;
			break;
		case _L_U_DEVMINOR:
			(void) memset(num_val, 0, ULONGLONG_MAX_DIGITS + 1);
			(void) sprintf(num_val, (val ? val : "%lu"),
			    minor(asb->sb_rdev));
			(void) strlcpy(*fmtstr, num_val, *fmtsize);
			is_numeric_key = 1;
			break;
		case _L_C_DEV:
			(void) memset(num_val, 0, ULONGLONG_MAX_DIGITS + 1);
			(void) sprintf(num_val, (val ? val : "%lo"),
			    asb->sb_dev);
			(void) strlcpy(*fmtstr, num_val, *fmtsize);
			is_numeric_key = 1;
			break;
		case _L_C_RDEV:
			(void) memset(num_val, 0, ULONGLONG_MAX_DIGITS + 1);
			(void) sprintf(num_val, (val ? val : "%lo"),
			    asb->sb_rdev);
			(void) strlcpy(*fmtstr, num_val, *fmtsize);
			is_numeric_key = 1;
			break;
		case _L_U_PREFIX:
			if (saveprefix != NULL) {
				s_realloc(fmtstr, strlen(saveprefix) + 1,
				    fmtsize);
				(void) strlcpy(*fmtstr, saveprefix, *fmtsize);
			}
			break;
		case _L_C_INO:
			(void) memset(num_val, 0, ULONGLONG_MAX_DIGITS + 1);
			(void) sprintf(num_val, (val ? val : "%llo"),
			    asb->sb_ino);
			(void) strlcpy(*fmtstr, num_val, *fmtsize);
			is_numeric_key = 1;
			break;
		case _L_C_NLINK:
			(void) memset(num_val, 0, ULONGLONG_MAX_DIGITS + 1);
			(void) sprintf(num_val, (val ? val : "%lo"),
			    asb->sb_nlink);
			(void) strlcpy(*fmtstr, num_val, *fmtsize);
			is_numeric_key = 1;
			break;
		case _L_C_NAMESIZE:
			(void) memset(num_val, 0, ULONGLONG_MAX_DIGITS + 1);
			if (savenamesize != NULL) {
				(void) sprintf(num_val, (val ? val : "%o"),
				    (ushort_t)from_oct(6, savenamesize));
			}
			(void) strlcpy(*fmtstr, num_val, *fmtsize);
			is_numeric_key = 1;
			break;
		case _L_C_FILEDATA:
			no_data_printed = 0;
			(void) fflush(msgfile);
			(void) indata(fileno(msgfile), 0, asb->sb_size,
			    strdup(name));
			(*fmtstr)[0] = '\0';
			break;
		default:
			(*fmtstr)[0] = '\0';
			return (-1);
			/* NOTREACHED */
			break;
		}
	}
	return (0);

}

/*
 * print_format()
 *
 * Prints a line of output to "msgfile" using the format string
 * read on the command line with the "-o listopt = <format string>"
 * option.
 *
 * The format string specified with the listopt option follows
 * the same guidelines as the format string described for the
 * printf().
 *
 * Characters in the format string that are not "escape sequences"
 * or "conversion specifications" are copied to the output.
 *
 * Escape sequences represent non-graphic characters.
 *
 * Conversion specifications specify the output format of each
 * argument.
 *
 *
 * The result of the keyword conversion argument is the value
 * from the applicable header field or extended header, without
 * any trailing NULs.
 *
 * A conversion specification is introduced by the percent ('%')
 * character.  After the '%', the following can appear in
 * sequence:
 *
 * field width	An optional string of decimal digits to specify
 *		a minimum field width.
 *
 * (keyword)	The conversion argument is defined by the value
 *		of "keyword".  For example, "%(charset)s" is
 *		the string value of the name of the character
 *		set in the extended header.
 *
 *
 * There are a few additional conversion specifiers
 * which can be specified with -o listopt:
 *
 * Specifier	Description
 * ---------	-----------
 * T		Used to specify time formats.  The T conversion
 *		specifier can be preceded by the sequence
 *		(keyword=subformat), where subformat is a date
 *		format as defined by date operands.  The default
 *		keyword is "mtime" and the default subformat is
 *		%b %e %H:%M %Y.
 *
 * M		Used to specify the file mode string as define
 *		in the 'ls' standard output.  If (keyword) is
 *		omitted, the "mode" keyword is used.  For
 *		example, %.1M writes the single character
 *		corresponding to the <entry type> field of the
 *		ls -l command.
 *
 * D		Used to specify the device for block or special
 *		files, if applicable.
 *
 * F		Used to specify a pathname.  It can be
 *		preceded by a sequence of comma-separated
 *		keywords: (keyword[,keyword] ... ).  The values
 *		for all the keywords that are non-null are
 *		concatenated together, each separated by a '/'.
 *		The default keyword sequence is "(path)" if
 *		the keyword "path" is defined; otherwise, the
 *		default is "(prefix,name)".
 *
 * L		Used to specify a symbolic line expansion.  If
 *		the current file is a symbolic link, then %L
 *		expands to:
 *		"%s -> %s", <value of keyword>, <contents of link>
 *		If the current file is not a symbolic link, then
 *		the %L conversion specification is the same as %F.
 *
 *
 * Input:
 * 	name		- name of file we are processing
 *	asb		- the Stat of the file we are processing
 *
 */

static void
print_format(char *name, Stat *asb)
{
	int		i;
	static int	inum = 0;	/* number of items in the format */
	static titem_t	**fmt = NULL;	/* the format for output */
	titem_t		*tptr;
	char		*kword = NULL;
	char		indicator[MAXSTRLEN];

	/*
	 * Get the format to output in if we haven't
	 * already done so.
	 */
	if (fmt == NULL) {
		inum = set_format(&fmt);
	}

	/*
	 * Print the line by stepping through the format tokens.
	 */
	(void) memset(indicator, '\0', MAXSTRLEN);
	for (i = 0; i < inum; i++) {
		for (tptr = fmt[i]; tptr != NULL; tptr = tptr->t_next) {
			/* Process each format line token */
			switch (tptr->t_type) {
			case TOK_TXT:		/* text */
			case TOK_PERCENT:	/* a percent character */
				(void) fprintf(msgfile, "%s", tptr->t_str);
				break;
			case TOK_CONVSPEC:
				if (*indicator == '\0') {
					/* reuse the indicator field */
					(void) strlcpy(indicator, "%",
					    MAXSTRLEN);
				}
				if (*tptr->t_str == CONV_D) {
					ADDTOIND(indicator, "s");
					ADDTOIND(indicator, tptr->t_str + 1);
					print_conv(name, kword, indicator,
					    CONV_D, asb);
				} else if (*tptr->t_str == CONV_F) {
					ADDTOIND(indicator, "s");
					ADDTOIND(indicator, tptr->t_str + 1);
					print_conv(name, kword, indicator,
					    CONV_F, asb);
				} else if (*tptr->t_str == CONV_L) {
					ADDTOIND(indicator, "s");
					ADDTOIND(indicator, tptr->t_str + 1);
					print_conv(name, kword, indicator,
					    CONV_L, asb);
				} else if (*tptr->t_str == CONV_M) {
					ADDTOIND(indicator, "s");
					ADDTOIND(indicator, tptr->t_str + 1);
					print_conv(name, kword, indicator,
					    CONV_M, asb);
				} else if (*tptr->t_str == CONV_T) {
					ADDTOIND(indicator, "s");
					ADDTOIND(indicator, tptr->t_str + 1);
					print_conv(name, kword, indicator,
					    CONV_T, asb);
				} else {
					ADDTOIND(indicator, tptr->t_str);
					print_conv(name, kword, indicator,
					    '\0', asb);
				}
				if (kword != NULL) {
					free(kword);
					kword = NULL;
				}
				(void) memset(indicator, '\0', MAXSTRLEN);
				break;
			case TOK_POSITION:
				(void) memset(indicator, '\0', MAXSTRLEN);
				if ((strlen(tptr->t_str) + 2) > MAXSTRLEN) {
					warn(tptr->t_str, gettext(
					    "Field/position indicator too "
					    "long"));
				} else {
					(void) strlcpy(indicator, "%",
					    MAXSTRLEN);
					(void) strlcat(indicator, tptr->t_str,
					    MAXSTRLEN);
				}
				break;
			case TOK_KEYWORD:
				STRDUP(kword, tptr->t_str);
				break;
			default:
				break;
			}
		}
	}
	/* append a newline to the listopt output for each selected file */
	if (i > 0) {
		(void) putc('\n', msgfile);
	}
}


/*
 * get_keyword()
 *
 * Returns the NULL terminated keyword found between a left paren
 * and a right paren.  If a right paren is not found, NULL
 * is returned.
 */
static char *
get_keyword(int index, int optlen)
{
	char	*str;
	char	*ptr;

	/* check if we've reached the end of listopt */
	if (index >= optlen) {
		return (NULL);
	}

	STRDUP(str, listopt + index);
	if ((ptr = strpbrk(str, RPAREN)) != NULL) {
		*ptr = '\0';
		return (str);
	} else {
		return (NULL);
	}
}

/*
 * malloc_tok()
 *
 * Allocate space and return the point to a titem.  Only t_next of the
 * titem structure is initialized.
 */
static titem_t *
malloc_tok(void)
{
	static struct titem	*ntok;

	if ((ntok = (titem_t *)malloc(sizeof (titem_t))) != NULL) {
		ntok->t_next = NULL;
	}
	return (ntok);
}

/*
 * add_ind_token()
 *
 * If a field/position indicator was specified, add a token to the
 * format line token list.
 */
static void
add_ind_token(titem_t **fptr, char *iptr, char *indicator)
{
	if (iptr != indicator) {
		add_fmt_token(fptr, indicator, TOK_POSITION);
		(void) memset(indicator, '\0', MAXSTRLEN);
	}
}

/*
 * add_fmt_token()
 *
 * Add a token with the specified characteristics to the format
 * line token list.
 */
static void
add_fmt_token(titem_t **fptr, char *tstr, int ttype)
{
	titem_t	*tmptok;
	titem_t	*tfptr;

	assert(fptr != NULL);

	if ((tmptok = malloc_tok()) == NULL) {
		fatal(gettext("out of memory"));
	}
	STRDUP(tmptok->t_str, tstr);
	tmptok->t_type = ttype;
	tmptok->t_next = NULL;
	if (*fptr == NULL) {
		/* First entry in the list. */
		tmptok->t_last = tmptok;
		*fptr = tmptok;
	} else {
		/*
		 * Add the format token onto the last item
		 * in the list and update the head entry's
		 * pointer to the last item.
		 */
		tfptr = (*fptr)->t_last;
		tfptr->t_next = tmptok;
		(*fptr)->t_last = tmptok;
	}
}

static void
handle_invconv(titem_t *fptr)
{
	int	size = PATH_MAX + 1;
	int	ind = 1;
	char	*tstr;
	titem_t	*tptr;

	if ((tstr = malloc(size)) == NULL) {
		fatal(gettext("out of memory"));
	}
	(void) memset(tstr, '\0', size);
	tstr[0] = '%';
	while (fptr != NULL) {
		if (fptr->t_str != NULL) {
			int	ssize = strlen(fptr->t_str);
			if (fptr->t_type == TOK_KEYWORD) {
				ssize += 2;
			}
			if ((ind + ssize + 1) > size) {
				size = (size * 2) + ssize;
				if ((tstr = realloc(tstr, size)) == NULL) {
					fatal(gettext("out of memory"));
				}
			}
			if (fptr->t_type == TOK_KEYWORD) {
				if (tstr == NULL) {
					(void) strcpy(tstr, "(");
				} else {
					(void) strcat(tstr, "(");
				}
			}

			(void) strlcat(tstr, fptr->t_str, size);
			ind += ssize;

			if (fptr->t_type == TOK_KEYWORD) {
				(void) strcat(tstr, ")");
			}
		}
		/* free all tokens in the list */
		tptr = fptr;
		fptr = fptr->t_next;
		free(tptr->t_str);
		free(tptr);
	}
	diag(gettext("Invalid format specifier for %s\n"), tstr);
	exit_status = 1;
	free(tstr);
}


/*
 * set_format()
 *
 * Set the format line.  The 'listopt' string is broken up into a series of
 * tokens, delineated by a '%', and is maintained into a 'format' array.
 * Each of the tokens delineated by a '%' is broken up even further, delineated
 * by
 *	'('			start of a keyword (TOK_KEYWORD).  ')' indicates
 *				    the end of the keyword token.
 *	'.' or digit		field/position indicator to applied
 *				    with conv spec (TOK_POSITION).
 *	ascii char		conversion specifier to be applied to 'keyword'
 *				    (TOK_CONVSPEC).
 *
 * Plain text occurring before a '%', but after a conversion specifier is also
 * considered a token (TOK_TXT).  Escape characters are translated and saved
 * as a text token.
 *
 * Two percent characters in a row '%%' is considered a token (TOK_PERCENT).
 *
 * Input
 *	fmt		pointer to the 'format' array to be set
 *
 * Output
 *	Number of entries in the 'format' array.
 */
static int
set_format(titem_t ***fmt)
{
	int		i = 0;
	int		tindx = 0;
	int		optlen;
	int		percent;
	int		keyword;
	int		cind;
	size_t		isize;
	static titem_t	*format[BUFSIZ];
	char		buf;
	char		*iptr;	/* ptr to indicator str */
	char		indicator[MAXSTRLEN];	/* ptr to indicator str */
	char		fline[MAXSTRLEN];	/* ptr to indicator str */
	char		cs[MAXSTRLEN];		/* conversion specifier */

	if (listopt == NULL) {
		return (0);
	}

	optlen = strlen(listopt);
	percent = 0;
	keyword = 0;
	(void) memset(indicator, '\0', MAXSTRLEN);
	(void) memset(cs, '\0', MAXSTRLEN);
	iptr = indicator;
	format[tindx] = NULL;
	cs[1] = '\0';
	while ((buf = listopt[i++]) != '\0') {
		switch (buf) {
		case '%':
			/* 2 percents in a row should print just 1 */
			if (percent) {
				add_fmt_token(&format[tindx], "%", TOK_PERCENT);
				percent = 0;
			}
			percent++;
			break;
		default:
			/*
			 * If we've already processed a '%', then the next
			 * character will be either the start of a keyword
			 * ('('), the start of a field/position indicator
			 * ('.' or a digit), or just plain text.
			 */
			if (percent) {
				char	*tstr;
				switch (buf) {
				case '(':	/* keyword */
					if ((tstr = get_keyword(i, optlen))
					    == NULL) {
						warn(gettext("Invalid keyword "
						    "for format conversion"),
						    (listopt + i));
						i = optlen;
					} else {
						int	k;
						if ((k = valid_keyword(tstr))
						    < 0) {
							warn(gettext(
							    "Invalid keyword "
							    "for format "
							    "conversion"),
							    tstr);
							i = optlen;
						} else if (OMITOPT(
						    listfmt_pair[k].fmtlist)) {
							/*
							 * Omit the keyword.
							 * Skip the conversion
							 * specifier.
							 */
							i += strlen(tstr) + 2;

							/*
							 * Skip the field/pos
							 * indicator.
							 */
							if (*indicator !=
							    '\0') {
								(void) memset(
								    indicator,
								    '\0',
								    MAXSTRLEN);
								iptr =
								    indicator;
							}

							/*
							 * Skip to a delimiter
							 * (end of format list,
							 * white space, or a
							 * "%").
							 */
							for (; (i < optlen) &&
							    !isspace(
							    listopt[i]) &&
							    (listopt[i] != '%');
							    i++) {
								;
							}

							percent = 0;
							free(tstr);
						} else {
							keyword++;
							/*
							 * If there was a
							 * positional indicator,
							 * insert it into the
							 * token list.
							 */
							add_ind_token(
							    &format[tindx],
							    iptr, indicator);
							iptr = indicator;
							/*
							 * Add the key word
							 * it to the format line
							 * token list.
							 */
							add_fmt_token(
							    &format[tindx],
							    tstr, TOK_KEYWORD);
							/*
							 * Update listopt index
							 * to point to the char
							 * just after the right
							 * parenthesis.
							 */
							i += strlen(tstr) + 1;
							free(tstr);
						}
					}
					break;
				default:
					/*
					 * Need to preserve characters
					 * between a percent character and
					 * a format conversion specifier,
					 * as they represent field widths
					 * and precisions.
					 * Example: "%.1T"
					 */
					if ((!keyword) && ((buf == '.') ||
					    (buf == '+') || (buf == '-') ||
					    isdigit(buf))) {
						isize = iptr - indicator;
						(void) snprintf(iptr,
						    sizeof (indicator) - isize,
						    "%c", buf);
						iptr++;
					} else {
						/*
						 * We've got a conversion spec.
						 * If it's not one of the pax
						 * designated ones, then there
						 * must be a keyword specified
						 * with it.
						 */
						cs[0] = buf;
						if (!keyword &&
						    !ISPCONVSPEC(buf)) {
							warn(gettext(
							    "Invalid "
							    "conversion "
							    "specifier"),
							    cs);
							i = optlen;
							percent = 0;
						} else {
							/*
							 * Add the
							 * field/position
							 * indicator if one was
							 * provided.
							 */
							add_ind_token(
							    &format[tindx],
							    iptr, indicator);
							iptr = indicator;

							/*
							 * Add the conv spec.
							 * To get the full conv
							 * spec, read to
							 * delimiter (end of fmt
							 * list, white space, or
							 * "%").
							 */
							for (cind = 1;
							    (i < optlen) &&
							    !isspace(
							    listopt[i]) &&
							    (listopt[i]
							    != '%') &&
							    (cind < MAXSTRLEN);
							    i++) {
								cs[cind++] =
								    listopt[i];
							}
							add_fmt_token(
							    &format[tindx],
							    cs, TOK_CONVSPEC);
							(void) memset(cs, '\0',
							    cind);
							cind = 0;
							if (buf == ' ') {
							handle_invconv(
							    format[tindx]);
							} else {
								tindx++;
							}
							keyword = 0;
							percent = 0;
							format[tindx] = NULL;
						}
					}
					break;
				}

			/* Just plain text to process */
			} else {
				int	done = 0;
				int findx = 0;
				int	tlen = 0;
				char	tc[2];
				char	txtline[MAXSTRLEN];

				/*
				 * Read text characters.  We're done reading
				 * when we've reached a '%' character, or
				 * we've processed the whole listopt string.
				 */
				tc[1] = '\0';
				(void) memset(txtline, '\0', MAXSTRLEN);
				while (!done) {

					/* Preserve escape characters */
					if (buf == '\\') {
						int j;

						if ((j = preserve_esc(i - 1,
						    fline, findx)) == 0) {
							/* processed the '\' */
							tlen++;
						} else {
							tlen += j;
						}

						/*
						 * Add the escape char to the
						 * format text line.
						 */
						if (*txtline == '\0') {
							(void) strlcpy(txtline,
							    fline, MAXSTRLEN);
						} else {
							(void) strlcat(
							    txtline,
							    strdup(fline),
							    MAXSTRLEN);
						}
						i += j;
					/* Just copy chars to format line */
					} else {
						tc[0] = buf;
						tlen++;
						if (*txtline == '\0') {
							(void) strlcpy(txtline,
							    tc, MAXSTRLEN);
						} else {
							(void) strlcat(txtline,
							    tc, MAXSTRLEN);
						}
					}

					/*
					 * If we've processed all the chars in
					 * the listopt string, or if we've
					 * reached another '%', then we're done
					 * processing the plain text string.
					 */
					if ((i >= optlen) ||
					    (listopt[i] == '%')) {
						add_fmt_token(&format[tindx],
						    txtline, TOK_TXT);
						(void) memset(txtline, '\0',
						    MAXSTRLEN);
						tindx++;
						format[tindx] = NULL;
						done = 1;
					} else {
						buf = listopt[i++];
					}
				}
			}
		}
	}

	/*
	 * Special case - Processed a '%' but never found a
	 * conversion specifier.  Collapse the format tokens
	 * into one "text" token.
	 */
	if (percent) {
		handle_invconv(format[tindx]);
	}

	/* We allocated one to many in format. */
	*fmt = format;
	return (tindx);
}

static void
print_conv(char *name, char *kword, const char *indicator,
    char ctype, Stat *asb)
{
	char	key[MAXSTRLEN];
	char	val[MAXSTRLEN];
	char	*fmtstr;
	char	*tmp;
	size_t	fmtsize = MAXSTRLEN;
	size_t	tmpsize = MAXSTRLEN;

	(void) memset(key, '\0', MAXSTRLEN);
	(void) memset(val, '\0', MAXSTRLEN);
	fmtstr = s_calloc(fmtsize);
	tmp = s_calloc(tmpsize);
	is_numeric_key = 0;
	switch (ctype) {
	case CONV_D:	/* Device for block/special files */
		/*
		 * Default output format will be (major,minor)
		 */
		if (kword == NULL) {
			(void) key_to_val(name, "devmajor", NULL,
			    &fmtstr, &fmtsize, asb);
			(void) key_to_val(name, "devminor", NULL,
			    &tmp, &tmpsize, asb);
			/*
			 * Ensure enough space for major device, comma,
			 * minor device and null.
			 */
			s_realloc(&fmtstr, strlen(fmtstr) + 1 +
			    strlen(tmp) + 1, &fmtsize);
			(void) strlcat(fmtstr, ",", fmtsize);
			(void) strlcat(fmtstr, tmp, fmtsize);
		} else {
			if (key_to_val(name, kword, NULL, &fmtstr,
			    &fmtsize, asb) < 0) {
				warn(kword, gettext(
				    "Invalid key word for device list format"));
			}
		}
		break;
	case CONV_F:	/* Pathname */
		/*
		 * The default is (path), which was figured out in
		 * read_header() if the keyword path is defined, otherwise,
		 * (prefix,name) is used.
		 */
		if (kword != NULL) {
			char	*str;
			str = build_path(name, kword, asb);
			s_realloc(&tmp, strlen(str) + 1, &tmpsize);
			(void) strlcpy(tmp, str, tmpsize);
			free(str);
		} else {
			s_realloc(&tmp, strlen(name) + 1, &tmpsize);
			(void) strlcpy(tmp, name, tmpsize);
		}
		s_realloc(&fmtstr, strlen(tmp) + 1, &fmtsize);
		(void) strlcpy(fmtstr, tmp, fmtsize);
		break;
	case CONV_L:	/* Symbolic link expansion */
		/*
		 * If the current file is a symbolic link, then expand it,
		 * otherwise the same thing is done as would be for 'F'
		 * conversion specification.
		 */
		if (kword != NULL) {
			char	*str;
			str = build_path(name, kword, asb);
			s_realloc(&tmp, strlen(str) + 1, &tmpsize);
			(void) strlcpy(tmp, str, tmpsize);
			free(str);
		} else {
			s_realloc(&tmp, strlen(name) + 1, &tmpsize);
			(void) strlcpy(tmp, name, tmpsize);
		}

#ifdef	S_IFLNK
		if ((asb->sb_mode & S_IFMT) == S_IFLNK) {
			char *s;
			STRDUP(s, asb->sb_link);
			s_realloc(&tmp, strlen(tmp) + strlen(s) + 5, &tmpsize);
			(void) strlcat(tmp, " -> ", tmpsize);
			(void) strlcat(tmp, s, tmpsize);
			free(s);
		}
#endif	/* S_IFLNK */

		s_realloc(&fmtstr, strlen(tmp) + 1, &fmtsize);
		(void) strlcpy(fmtstr, tmp, fmtsize);
		break;
	case CONV_M:	/* Mode conversion */
		if (kword == NULL) {
			STRDUP(kword, "mode");
		}
		if (key_to_val(name, kword, NULL, &fmtstr, &fmtsize, asb) < 0) {
			warn(kword, gettext(
			    "Invalid key word for mode list format"));
		}
		break;
	case CONV_T:	/* Time conversion */
		if (kword != NULL) {
			/*
			 * A key was entered or possibly a key=subformat,
			 * so split the key up into the actual key and
			 * and subformat (val).
			 */
			strsplit(kword, key, val, '=');
			if (*key == '\0') {
				warn(kword, gettext(
				    "Invalid key word for mode list format"));
			}
		}
		/*
		 * If a "key" wasn't specified, use 'mtime' as the default.
		 * A subformat of "%b %e %H:%M %Y" will be used.
		 */
		if (*key == '\0') {
			(void) strcpy(key, "mtime");
		}
		if (key_to_val(name, key, val, &fmtstr, &fmtsize, asb) < 0) {
			warn(key, gettext(
			    "Invalid key word for time list format"));
		}
		break;
	default:
		if (kword == NULL) {
			free(fmtstr);
			free(tmp);
			return;
		} else if (key_to_val(name, kword, indicator,
		    &fmtstr, &fmtsize, asb) < 0) {
			warn(key, gettext(
			    "Invalid key word for list format"));
		}
		break;
	}
	free(tmp);
	if (!is_numeric_key && (*indicator != '\0')) {
		(void) fprintf(msgfile, indicator,
		    (*fmtstr != '\0') ? fmtstr : "");
	} else {
		(void) fprintf(msgfile, "%s",
		    (*fmtstr != '\0') ? fmtstr : "");
	}
	free(fmtstr);
	is_numeric_key = 0;
}

/*
 * pax_entry - print a verbose pax-style entry
 *
 * DESCRIPTION
 *
 *	pax_entry prints a single line of file information.  The format
 *	of the line is the same as that used by the LS command.
 *	No error checking is done for bad or invalid data.
 *
 * PARAMETERS
 *
 *	char   *name		- pointer to name to print an entry for
 *	Stat   *asb		- pointer to the stat structure for the file
 */


static void
pax_entry(char *name, Stat *asb)
{
	struct tm	*atm;
	Link		*from;
	struct passwd	*pwp;
	struct group	*grp;
	char		mon[MAX_MONTH];		/* abbreviated month name */
	time_t		six_months_ago;
	char		*attrfnamep;
	int		issysattr = 0;

	/* use list mode format if specified */
	if (listopt != NULL) {
		print_format(name, asb);
	} else if (f_list && f_verbose) {
		(void) fprintf(msgfile, "%s", print_mode(asb->sb_mode));
		(void) fprintf(msgfile, " %2lu", asb->sb_nlink);
		atm = localtime(&asb->sb_mtime);
		six_months_ago = now - 6L*30L*24L*60L*60L; /* 6 months ago */
		(void) strftime(mon, sizeof (mon), "%b", atm);
		if (pwp = getpwuid(asb->sb_uid))
			(void) fprintf(msgfile, " %-8s", pwp->pw_name);
		else
			(void) fprintf(msgfile, " %-8u", asb->sb_uid);
		if (grp = getgrgid(asb->sb_gid))
			(void) fprintf(msgfile, " %-8s", grp->gr_name);
		else
			(void) fprintf(msgfile, " %-8u", asb->sb_gid);

		switch (asb->sb_mode & S_IFMT) {

		case S_IFBLK:
		case S_IFCHR:
			(void) fprintf(msgfile, "\t%3lu, %3lu",
			    major(asb->sb_rdev), minor(asb->sb_rdev));
			break;

		case S_IFREG:
			if ((OFFSET)asb->sb_size < (OFFSET)(1LL << 31))
				(void) fprintf(msgfile, "\t%7lld",
				    (OFFSET)(asb->sb_size));
			else
				(void) fprintf(msgfile, "\t%11lld",
				    (OFFSET)(asb->sb_size));
			break;

		default:
			(void) fprintf(msgfile, "\t        ");
			break;
		}

		if ((asb->sb_mtime < six_months_ago) || (asb->sb_mtime > now)) {
			(void) fprintf(msgfile,
			    dcgettext(NULL, " %3s %2d  %4d ",
			    LC_TIME), mon, atm->tm_mday, atm->tm_year + 1900);
		} else {
			(void) fprintf(msgfile, " %3s %2d %02d:%02d ", mon,
			    atm->tm_mday, atm->tm_hour, atm->tm_min);
		}
		if (asb->xattr_info.xattrp != NULL) {
			issysattr = is_sysattr(basename(
			    asb->xattr_info.xattraname));
		}
		(void) fprintf(msgfile, "%s%s%s",
		    (asb->xattr_info.xattrp == NULL) ?
		    name : asb->xattr_info.xattrfname,
		    (asb->xattr_info.xattrp == NULL) ? "" :
		    issysattr ? gettext(" system attribute ") :
		    gettext(" attribute "),
		    (asb->xattr_info.xattrp == NULL) ?
		    "" : asb->xattr_info.xattrapath);

		if ((asb->sb_nlink > 1) && (from = islink(name, asb))) {
			if (asb->xattr_info.xattr_linkfname != NULL) {
				(void) fprintf(msgfile, " == %s attribute %s",
				    asb->xattr_info.xattr_linkfname,
				    from->l_attr);
			} else {
				(void) fprintf(msgfile, " == %s", from->l_name);
			}
		}
#ifdef	S_IFLNK
		if ((asb->sb_mode & S_IFMT) == S_IFLNK)
			(void) fprintf(msgfile, " -> %s", asb->sb_link);
#endif	/* S_IFLNK */
		(void) putc('\n', msgfile);
	} else {
#if defined(O_XATTR)
		if (asb->xattr_info.xattrfname == NULL) {
			attrfnamep = NULL;
		} else {
			if (f_interactive && asb->xattr_info.xattrp) {
				attrfnamep = asb->xattr_info.xattrfname;
			} else {
				attrfnamep = name;
			}
			issysattr = is_sysattr(basename(
			    asb->xattr_info.xattraname));
		}
#else
		attrfnamep = NULL;
#endif
		(void) fprintf(msgfile, "%s%s%s\n",
		    (asb->xattr_info.xattrfname == NULL) ? name :
		    attrfnamep,
		    (asb->xattr_info.xattraname == NULL) ? "" :
		    issysattr ? gettext(" system attribute ") :
		    gettext(" attribute "),
		    (asb->xattr_info.xattraname == NULL) ? "" :
		    asb->xattr_info.xattrapath);
	}
}


/*
 * print_mode - fancy file mode display
 *
 * DESCRIPTION
 *
 *	Print_mode displays a numeric file mode in the standard unix
 *	representation, ala ls:  -rwxrwxrwx.  No error checking is done
 *	for bad mode combinations.  FIFOS, sybmbolic links, sticky bits,
 *	block- and character-special devices are supported if supported
 *	by the hosting implementation.
 *
 * PARAMETERS
 *
 *	ushort	mode	- The integer representation of the mode to print.
 *
 * OUTPUT
 *
 *	A null terminated string containing the numeric file mode.
 */
static char *
print_mode(ushort_t mode)
{
	char buf[11];
	char *bufptr;

	(void) memset(buf, '\0', sizeof (buf));
	bufptr = buf;

	/* Tar does not print the leading identifier... */
	if (ar_interface != TAR) {
	switch (mode & S_IFMT) {

	case S_IFDIR:
		*bufptr++ = 'd';
		break;

#ifdef	S_IFLNK
	case S_IFLNK:
		*bufptr++ = 'l';
		break;

#endif	/* S_IFLNK */
	case S_IFBLK:
		*bufptr++ = 'b';
		break;

	case S_IFCHR:
		*bufptr++ = 'c';
		break;

#ifdef	S_IFIFO
	case S_IFIFO:
		*bufptr++ = 'p';
		break;

#endif	/* S_IFIFO */
	case S_IFREG:
	default:
		*bufptr++ = '-';
		break;

	}
	}

	(void) sprintf(bufptr, "%c%c%c%c%c%c%c%c%c",
	    (mode & 0400 ? 'r' : '-'),
	    (mode & 0200 ? 'w' : '-'),
	    (mode & 0100 ? mode & 04000 ? 's' :
	    'x' : mode & 04000 ? 'S' : '-'),
	    (mode & 0040 ? 'r' : '-'),
	    (mode & 0020 ? 'w' : '-'),
	    (mode & 0010 ? mode & 02000 ? 's' :
	    'x' : mode & 02000 ? 'S' : '-'),
	    (mode & 0004 ? 'r' : '-'),
	    (mode & 0002 ? 'w' : '-'),
	    (mode & 0001 ? mode & 01000 ? 't' :
	    'x' : mode & 01000 ? 'T' : '-'));

	return (strdup(buf));
}


/*
 * from_oct - quick and dirty octal conversion
 *
 * DESCRIPTION
 *
 *	From_oct will convert an ASCII representation of an octal number
 *	to the numeric representation.  The number of characters to convert
 *	is given by the parameter "digs".  If there are less numbers than
 *	specified by "digs", then the routine returns -1.
 *
 * PARAMETERS
 *
 *	int digs	- Number to of digits to convert
 *	char *where	- Character representation of octal number
 *
 * RETURNS
 *
 *	The value of the octal number represented by the first digs
 *	characters of the string where.  Result is -1 if the field
 *	is invalid (all blank, or nonoctal).
 *
 * ERRORS
 *
 *	If the field is all blank, then the value returned is -1.
 *
 */


static OFFSET
from_oct(int digs, char *where)
{
	OFFSET value;

	while (isspace(*where)) {	/* Skip spaces */
		where++;
		if (--digs <= 0)
			return (-1);		/* All blank field */
	}
	value = 0;
	while (digs > 0 && ISODIGIT(*where)) {	/* Scan til nonoctal */
		value = (OFFSET)((value << 3) | (*where++ - '0'));
		--digs;
	}

	if (digs > 0 && *where && !isspace(*where))
		return (-1);		/* Ended on non-space/nul */
	return (value);
}

/*
 * merge_xhdrdata()
 *
 * Merge the extended header overrides from 4 sources: 1) specified with
 * -o keyword=value on the command line, 2) specified with -o keyword:=value
 * on the command line, 3) entry in extended header, 4) entry in the global
 * extended header.  The keyword/value pairs have already been processed
 * and reside in two separate xtarhdr structures: 1) coXtarhdr contains the
 * keyword/value pairs specified on the command line, 2) Xtarhdr contains the
 * keyword/value pairs retrieved from the extended and global extended header.
 * The data from the two sources (coXtarhdr and Xtarhdr) are merged together
 * into the oXtarhdr structure, in the following precedence order:
 * If there is a typeflag 'x' override specified on the command line with
 * the -o keyword:=value option, then it will be used as the extended header
 * data override, otherwise if there is a typeflag 'g' override specified
 * on the command line with the -o keyword=value option, then it will be
 * used as the override data, otherwise if there is a typeflag 'x' entry
 * in an extended header that was processed, then it is used, and otherwise
 * if none of the previous keyword/value pairs were encountered, then the
 * typeflag 'g' entry in a global extended header that was processed, if any,
 * is used.
 *
 * read_header() just needs to check if ohdr_flgs is set for any of the
 * specific keywords.
 */
void
merge_xhdrdata(void)
{
	ohdr_flgs = 0;

	/* if we've got an 'x' override from the cmd line, use it */
	if (oxxhdr_flgs & _X_UID) {
		oXtarhdr.x_uid = coXtarhdr.x_uid;
		ohdr_flgs |= _X_UID;
	/* else if we've got a 'g' override from the cmd line, use it */
	} else if (ogxhdr_flgs & _X_UID) {
		oXtarhdr.x_uid = coXtarhdr.gx_uid;
		ohdr_flgs |= _X_UID;
	/* else if we've got an 'x' override from an ext hdr, use it */
	} else if (xhdr_flgs & _X_UID) {
		oXtarhdr.x_uid = Xtarhdr.x_uid;
		ohdr_flgs |= _X_UID;
	/* else if we've got a 'g' override from an ext hdr, use it */
	} else if (gxhdr_flgs & _X_UID) {
		oXtarhdr.x_uid = Xtarhdr.gx_uid;
		ohdr_flgs |= _X_UID;
	/* there's no override value for this keyword */
	} else {
		oXtarhdr.x_uid = 0;
	}

	if (oxxhdr_flgs & _X_GID) {
		oXtarhdr.x_gid = coXtarhdr.x_gid;
		ohdr_flgs |= _X_GID;
	} else if (ogxhdr_flgs & _X_GID) {
		oXtarhdr.x_gid = coXtarhdr.gx_gid;
		ohdr_flgs |= _X_GID;
	} else if (xhdr_flgs & _X_GID) {
		oXtarhdr.x_gid = Xtarhdr.x_gid;
		ohdr_flgs |= _X_GID;
	} else if (gxhdr_flgs & _X_GID) {
		oXtarhdr.x_gid = Xtarhdr.gx_gid;
		ohdr_flgs |= _X_GID;
	} else {
		oXtarhdr.x_gid = 0;
	}

	if (oxxhdr_flgs & _X_DEVMAJOR) {
		oXtarhdr.x_devmajor = coXtarhdr.x_devmajor;
		ohdr_flgs |= _X_DEVMAJOR;
	} else if (ogxhdr_flgs & _X_DEVMAJOR) {
		oXtarhdr.x_devmajor = coXtarhdr.gx_devmajor;
		ohdr_flgs |= _X_DEVMAJOR;
	} else if (xhdr_flgs & _X_DEVMAJOR) {
		oXtarhdr.x_devmajor = Xtarhdr.x_devmajor;
		ohdr_flgs |= _X_DEVMAJOR;
	} else if (gxhdr_flgs & _X_DEVMAJOR) {
		oXtarhdr.x_devmajor = Xtarhdr.gx_devmajor;
		ohdr_flgs |= _X_DEVMAJOR;
	} else {
		oXtarhdr.x_devmajor = 0;
	}

	if (oxxhdr_flgs & _X_DEVMINOR) {
		oXtarhdr.x_devminor = coXtarhdr.x_devminor;
		ohdr_flgs |= _X_DEVMINOR;
	} else if (ogxhdr_flgs & _X_DEVMINOR) {
		oXtarhdr.x_devminor = coXtarhdr.gx_devminor;
		ohdr_flgs |= _X_DEVMINOR;
	} else if (xhdr_flgs & _X_DEVMINOR) {
		oXtarhdr.x_devminor = Xtarhdr.x_devminor;
		ohdr_flgs |= _X_DEVMINOR;
	} else if (gxhdr_flgs & _X_DEVMINOR) {
		oXtarhdr.x_devminor = Xtarhdr.gx_devminor;
		ohdr_flgs |= _X_DEVMINOR;
	} else {
		oXtarhdr.x_devminor = 0;
	}

	if (oxxhdr_flgs & _X_SIZE) {
		oXtarhdr.x_filesz = coXtarhdr.x_filesz;
		ohdr_flgs |= _X_SIZE;
	} else if (ogxhdr_flgs & _X_SIZE) {
		oXtarhdr.x_filesz = coXtarhdr.gx_filesz;
		ohdr_flgs |= _X_SIZE;
	} else if (xhdr_flgs & _X_SIZE) {
		oXtarhdr.x_filesz = Xtarhdr.x_filesz;
		ohdr_flgs |= _X_SIZE;
	} else if (gxhdr_flgs & _X_SIZE) {
		oXtarhdr.x_filesz = Xtarhdr.gx_filesz;
		ohdr_flgs |= _X_SIZE;
	} else {
		oXtarhdr.x_filesz = 0;
	}

	if (oxxhdr_flgs & _X_UNAME) {
		oXtarhdr.x_uname = coXtarhdr.x_uname;
		ohdr_flgs |= _X_UNAME;
	} else if (ogxhdr_flgs & _X_UNAME) {
		oXtarhdr.x_uname = coXtarhdr.gx_uname;
		ohdr_flgs |= _X_UNAME;
	} else if (xhdr_flgs & _X_UNAME) {
		oXtarhdr.x_uname = Xtarhdr.x_uname;
		ohdr_flgs |= _X_UNAME;
	} else if (gxhdr_flgs & _X_UNAME) {
		oXtarhdr.x_uname = Xtarhdr.gx_uname;
		ohdr_flgs |= _X_UNAME;
	} else {
		oXtarhdr.x_uname = NULL;
	}

	if (oxxhdr_flgs & _X_GNAME) {
		oXtarhdr.x_gname = coXtarhdr.x_gname;
		ohdr_flgs |= _X_GNAME;
	} else if (ogxhdr_flgs & _X_GNAME) {
		oXtarhdr.x_gname = coXtarhdr.gx_gname;
		ohdr_flgs |= _X_GNAME;
	} else if (xhdr_flgs & _X_GNAME) {
		oXtarhdr.x_gname = Xtarhdr.x_gname;
		ohdr_flgs |= _X_GNAME;
	} else if (gxhdr_flgs & _X_GNAME) {
		oXtarhdr.x_gname = Xtarhdr.gx_gname;
		ohdr_flgs |= _X_GNAME;
	} else {
		oXtarhdr.x_gname = NULL;
	}

	if (oxxhdr_flgs & _X_LINKPATH) {
		oXtarhdr.x_linkpath = coXtarhdr.x_linkpath;
		ohdr_flgs |= _X_LINKPATH;
	} else if (ogxhdr_flgs & _X_LINKPATH) {
		oXtarhdr.x_linkpath = coXtarhdr.gx_linkpath;
		ohdr_flgs |= _X_LINKPATH;
	} else if (xhdr_flgs & _X_LINKPATH) {
		oXtarhdr.x_linkpath = Xtarhdr.x_linkpath;
		ohdr_flgs |= _X_LINKPATH;
	} else if (gxhdr_flgs & _X_LINKPATH) {
		oXtarhdr.x_linkpath = Xtarhdr.gx_linkpath;
		ohdr_flgs |= _X_LINKPATH;
	} else {
		oXtarhdr.x_linkpath = NULL;
	}

/* Do we need to override prefix too incase something was put in there? */
	if (oxxhdr_flgs & _X_PATH) {
		oXtarhdr.x_path = coXtarhdr.x_path;
		ohdr_flgs |= _X_PATH;
	} else if (ogxhdr_flgs & _X_PATH) {
		oXtarhdr.x_path = coXtarhdr.gx_path;
		ohdr_flgs |= _X_PATH;
	} else if (xhdr_flgs & _X_PATH) {
		oXtarhdr.x_path = Xtarhdr.x_path;
		ohdr_flgs |= _X_PATH;
	} else if (gxhdr_flgs & _X_PATH) {
		oXtarhdr.x_path = Xtarhdr.gx_path;
		ohdr_flgs |= _X_PATH;
	} else {
		oXtarhdr.x_path = NULL;
	}

	if (oxxhdr_flgs & _X_ATIME) {
		oXtarhdr.x_atime.tv_sec = coXtarhdr.x_atime.tv_sec;
		oXtarhdr.x_atime.tv_nsec = coXtarhdr.x_atime.tv_nsec;
		ohdr_flgs |= _X_ATIME;
	} else if (ogxhdr_flgs & _X_ATIME) {
		oXtarhdr.x_atime.tv_sec = coXtarhdr.gx_atime.tv_sec;
		oXtarhdr.x_atime.tv_nsec = coXtarhdr.gx_atime.tv_nsec;
		ohdr_flgs |= _X_ATIME;
	} else if (xhdr_flgs & _X_ATIME) {
		oXtarhdr.x_atime.tv_sec = Xtarhdr.x_atime.tv_sec;
		oXtarhdr.x_atime.tv_nsec = Xtarhdr.x_atime.tv_nsec;
		ohdr_flgs |= _X_ATIME;
	} else if (gxhdr_flgs & _X_ATIME) {
		oXtarhdr.x_atime.tv_sec = Xtarhdr.gx_atime.tv_sec;
		oXtarhdr.x_atime.tv_nsec = Xtarhdr.gx_atime.tv_nsec;
		ohdr_flgs |= _X_ATIME;
	} else {
		oXtarhdr.x_atime.tv_sec = 0;
		oXtarhdr.x_atime.tv_nsec = 0;
	}

	if (oxxhdr_flgs & _X_MTIME) {
		oXtarhdr.x_mtime.tv_sec = coXtarhdr.x_mtime.tv_sec;
		oXtarhdr.x_mtime.tv_nsec = coXtarhdr.x_mtime.tv_nsec;
		ohdr_flgs |= _X_MTIME;
	} else if (ogxhdr_flgs & _X_MTIME) {
		oXtarhdr.x_mtime.tv_sec = coXtarhdr.gx_mtime.tv_sec;
		oXtarhdr.x_mtime.tv_nsec = coXtarhdr.gx_mtime.tv_nsec;
		ohdr_flgs |= _X_MTIME;
	} else if (xhdr_flgs & _X_MTIME) {
		oXtarhdr.x_mtime.tv_sec = Xtarhdr.x_mtime.tv_sec;
		oXtarhdr.x_mtime.tv_nsec = Xtarhdr.x_mtime.tv_nsec;
		ohdr_flgs |= _X_MTIME;
	} else if (gxhdr_flgs & _X_MTIME) {
		oXtarhdr.x_mtime.tv_sec = Xtarhdr.gx_mtime.tv_sec;
		oXtarhdr.x_mtime.tv_nsec = Xtarhdr.gx_mtime.tv_nsec;
		ohdr_flgs |= _X_MTIME;
	} else {
		oXtarhdr.x_mtime.tv_sec = 0;
		oXtarhdr.x_mtime.tv_nsec = 0;
	}

	if (oxxhdr_flgs & _X_CHARSET) {
		oXtarhdr.x_charset = coXtarhdr.x_charset;
		ohdr_flgs |= _X_CHARSET;
	} else if (ogxhdr_flgs & _X_CHARSET) {
		oXtarhdr.x_charset = coXtarhdr.gx_charset;
		ohdr_flgs |= _X_CHARSET;
	} else if (xhdr_flgs & _X_CHARSET) {
		oXtarhdr.x_charset = Xtarhdr.x_charset;
		ohdr_flgs |= _X_CHARSET;
	} else if (gxhdr_flgs & _X_CHARSET) {
		oXtarhdr.x_charset = Xtarhdr.gx_charset;
		ohdr_flgs |= _X_CHARSET;
	} else {
		oXtarhdr.x_charset = NULL;
	}

	if (oxxhdr_flgs & _X_COMMENT) {
		oXtarhdr.x_comment = coXtarhdr.x_comment;
		ohdr_flgs |= _X_COMMENT;
	} else if (ogxhdr_flgs & _X_COMMENT) {
		oXtarhdr.x_comment = coXtarhdr.gx_comment;
		ohdr_flgs |= _X_COMMENT;
	} else if (xhdr_flgs & _X_COMMENT) {
		oXtarhdr.x_comment = Xtarhdr.x_comment;
		ohdr_flgs |= _X_COMMENT;
	} else if (gxhdr_flgs & _X_COMMENT) {
		oXtarhdr.x_comment = Xtarhdr.gx_comment;
		ohdr_flgs |= _X_COMMENT;
	} else {
		oXtarhdr.x_comment = NULL;
	}

	if (oxxhdr_flgs & _X_REALTIME) {
		oXtarhdr.x_realtime = coXtarhdr.x_realtime;
		ohdr_flgs |= _X_REALTIME;
	} else if (ogxhdr_flgs & _X_REALTIME) {
		oXtarhdr.x_realtime = coXtarhdr.gx_realtime;
		ohdr_flgs |= _X_REALTIME;
	} else if (xhdr_flgs & _X_REALTIME) {
		oXtarhdr.x_realtime = Xtarhdr.x_realtime;
		ohdr_flgs |= _X_REALTIME;
	} else if (gxhdr_flgs & _X_REALTIME) {
		oXtarhdr.x_realtime = Xtarhdr.gx_realtime;
		ohdr_flgs |= _X_REALTIME;
	} else {
		oXtarhdr.x_realtime = NULL;
	}

	if (oxxhdr_flgs & _X_SECURITY) {
		oXtarhdr.x_security = coXtarhdr.x_security;
		ohdr_flgs |= _X_SECURITY;
	} else if (ogxhdr_flgs & _X_SECURITY) {
		oXtarhdr.x_security = coXtarhdr.gx_security;
		ohdr_flgs |= _X_SECURITY;
	} else if (xhdr_flgs & _X_SECURITY) {
		oXtarhdr.x_security = Xtarhdr.x_security;
		ohdr_flgs |= _X_SECURITY;
	} else if (gxhdr_flgs & _X_SECURITY) {
		oXtarhdr.x_security = Xtarhdr.gx_security;
		ohdr_flgs |= _X_SECURITY;
	} else {
		oXtarhdr.x_security = NULL;
	}

	if (oxxhdr_flgs & _X_HOLESDATA) {
		oXtarhdr.x_holesdata = coXtarhdr.x_holesdata;
		ohdr_flgs |= _X_HOLESDATA;
	} else if (ogxhdr_flgs & _X_HOLESDATA) {
		oXtarhdr.x_holesdata = coXtarhdr.gx_holesdata;
		ohdr_flgs |= _X_HOLESDATA;
	} else if (xhdr_flgs & _X_HOLESDATA) {
		oXtarhdr.x_holesdata = Xtarhdr.x_holesdata;
		ohdr_flgs |= _X_HOLESDATA;
	} else if (gxhdr_flgs & _X_HOLESDATA) {
		oXtarhdr.x_holesdata = Xtarhdr.gx_holesdata;
		ohdr_flgs |= _X_HOLESDATA;
	} else {
		oXtarhdr.x_holesdata = NULL;
	}
	/* oXtarhdr.gx_* are not used */
}

/*
 * Process the command line "-o keyword:=pair" overrides.  Saving the
 * values to the Xtarhdr structure will override any values that
 * existed if keyword/value pairs were processed while reading an
 * extended header (in get_xdata()).
 */
int
get_oxhdrdata(void)
{
	char	*val;
	int	errors = 0;
	int	i;
	int	maxsize;

	if (!firstxhdr) {
		/* free memory */
		if (coXtarhdr.x_uname != NULL) {
			free(coXtarhdr.x_uname);
		}
		if (coXtarhdr.x_gname != NULL) {
			free(coXtarhdr.x_gname);
		}
		if (coXtarhdr.x_linkpath != NULL) {
			free(coXtarhdr.x_linkpath);
		}
		if (coXtarhdr.x_path != NULL) {
			free(coXtarhdr.x_path);
		}
		if (coXtarhdr.x_charset != NULL) {
			free(coXtarhdr.x_charset);
		}
		if (coXtarhdr.x_comment != NULL) {
			free(coXtarhdr.x_comment);
		}
		if (coXtarhdr.x_realtime != NULL) {
			free(coXtarhdr.x_realtime);
		}
		if (coXtarhdr.x_security != NULL) {
			free(coXtarhdr.x_security);
		}
		if (coXtarhdr.x_holesdata != NULL) {
			free(coXtarhdr.x_holesdata);
		}
	}

	coXtarhdr.x_uid = 0;
	coXtarhdr.x_gid = 0;
	coXtarhdr.x_devmajor = 0;
	coXtarhdr.x_devminor = 0;
	coXtarhdr.x_filesz = 0;
	coXtarhdr.x_uname = NULL;
	coXtarhdr.x_gname = NULL;
	coXtarhdr.x_linkpath = NULL;
	coXtarhdr.x_path = NULL;
	coXtarhdr.x_atime.tv_sec = 0;
	coXtarhdr.x_atime.tv_nsec = 0;
	coXtarhdr.x_mtime.tv_sec = 0;
	coXtarhdr.x_mtime.tv_nsec = 0;
	coXtarhdr.x_charset = NULL;
	coXtarhdr.x_comment = NULL;
	coXtarhdr.x_realtime = NULL;
	coXtarhdr.x_security = NULL;
	coXtarhdr.x_holesdata = NULL;
	oxxhdr_flgs = 0;

	/* easy case - no keyword=pair options specified on cmd line */
	if (xoptlist == NULL) {
		return (HDR_OK);
	}

	i = 0;
	while (keylist_pair[i].keynum != (int)_X_LAST) {
		if (!OMITOPT(keylist_pair[i].keylist)) {
			val = NULL;
			if (nvlist_lookup_string(xoptlist,
			    keylist_pair[i].keylist, &val) == 0) {
				errno = 0;
				switch (keylist_pair[i].keynum) {
				case _X_DEVMAJOR:
					coXtarhdr.x_devmajor =
					    (major_t)strtoul(val, NULL, 0);
					if (errno) {
						diag(gettext(
						    "invalid device major "
						    "number override value "
						    "%s\n"), val);
						exit_status = 1;
						errors++;
					} else
						oxxhdr_flgs |= _X_DEVMAJOR;
					break;

				case _X_DEVMINOR:
					coXtarhdr.x_devminor =
					    (minor_t)strtoul(val, NULL, 0);
					if (errno) {
						diag(gettext(
						    "invalid device minor "
						    "number override value "
						    "%s\n"), val);
						exit_status = 1;
						errors++;
					} else
						oxxhdr_flgs |= _X_DEVMINOR;
					break;

				case _X_GID:
					oxxhdr_flgs |= _X_GID;
					f_group = 1;
					coXtarhdr.x_gid = strtol(val, NULL, 0);
					if ((errno) ||
					    (coXtarhdr.x_gid > UID_MAX)) {
						diag(gettext(
						    "invalid gid "
						    "override value "
						    "%s\n"), val);
						exit_status = 1;
						coXtarhdr.x_gid = GID_NOBODY;
					}
					break;

				case _X_GNAME:
					maxsize = strlen(val) *
					    UTF_8_FACTOR + 1;
					if (local_gname != NULL) {
						free(local_gname);
					}
					local_gname = s_calloc(maxsize);
					if (utf8_local("gname",
					    &coXtarhdr.x_gname,
					    local_gname, val,
					    maxsize) == 0) {
						oxxhdr_flgs |= _X_GNAME;
						f_group = 1;
					}
					break;

				case _X_LINKPATH:
					maxsize = strlen(val) *
					    UTF_8_FACTOR + 1;
					if (xlocal_linkpath != NULL) {
						free(xlocal_linkpath);
					}
					xlocal_linkpath = s_calloc(maxsize);
					if (utf8_local("linkpath",
					    &coXtarhdr.x_linkpath,
					    xlocal_linkpath, val,
					    maxsize) == 0) {
						oxxhdr_flgs |=
						    _X_LINKPATH;
					} else {
						errors++;
					}
					break;

				case _X_PATH:
					maxsize = strlen(val) *
					    UTF_8_FACTOR + 1;
					if (xlocal_path != NULL) {
						free(xlocal_path);
					}
					xlocal_path = s_calloc(maxsize);
					if (utf8_local("path",
					    &coXtarhdr.x_path,
					    xlocal_path, val,
					    maxsize) == 0) {
						oxxhdr_flgs |= _X_PATH;
					} else {
						errors++;
					}
					break;

				case _X_SIZE:
					coXtarhdr.x_filesz = strtoull(val,
					    NULL, 0);
					if (errno) {
						diag(gettext(
						    "invalid file size "
						    "override value "
						    "%s\n"), val);
						exit_status = 1;
						errors++;
					} else {
						oxxhdr_flgs |= _X_SIZE;
					}
					break;

				case _X_UID:
					oxxhdr_flgs |= _X_UID;
					f_user = 1;
					coXtarhdr.x_uid = strtol(val, NULL, 0);
					if ((errno) ||
					    (coXtarhdr.x_uid > UID_MAX)) {
						diag(gettext(
						    "invalid uid "
						    "override value "
						    "%s\n"), val);
						exit_status = 1;
						coXtarhdr.x_uid = UID_NOBODY;
					}
					break;

				case _X_UNAME:
					maxsize = strlen(val) *
					    UTF_8_FACTOR + 1;
					if (local_uname != NULL) {
						free(local_uname);
					}
					local_uname = s_calloc(maxsize);
					if (utf8_local("uname",
					    &coXtarhdr.x_uname, local_uname,
					    val, maxsize) == 0) {
						oxxhdr_flgs |= _X_UNAME;
						f_user = 1;
					}
					break;

				case _X_ATIME:
					get_xtime(val, &(coXtarhdr.x_atime));
					if (errno) {
						diag(gettext(
						    "invalid file access time "
						    "override value "
						    "%s\n"), val);
						exit_status = 1;
					} else {
						oxxhdr_flgs |= _X_ATIME;
						f_extract_access_time = 1;
					}
					break;

				case _X_MTIME:
					get_xtime(val, &(coXtarhdr.x_mtime));
					if (errno) {
						diag(gettext(
						    "invalid file modification "
						    "time override value "
						    "%s\n"), val);
						exit_status = 1;
					} else {
						oxxhdr_flgs |= _X_MTIME;
						f_mtime = 1;
					}
					break;

				case _X_CHARSET:
					maxsize = strlen(val) *
					    UTF_8_FACTOR + 1;
					if (local_charset != NULL) {
						free(local_charset);
					}
					local_charset = s_calloc(maxsize);
					if (utf8_local("charset",
					    &coXtarhdr.x_charset,
					    local_charset, val,
					    maxsize) == 0) {
						oxxhdr_flgs |= _X_CHARSET;
					} else {
						errors++;
					}
					break;
				case _X_COMMENT:
					maxsize = strlen(val) *
					    UTF_8_FACTOR + 1;
					if (local_comment != NULL) {
						free(local_comment);
					}
					local_comment = s_calloc(maxsize);
					if (utf8_local("comment",
					    &coXtarhdr.x_comment,
					    local_comment, val,
					    maxsize) == 0) {
						oxxhdr_flgs |= _X_COMMENT;
					} else {
						errors++;
					}
					break;
				case _X_REALTIME:
					maxsize = strlen(val) *
					    UTF_8_FACTOR + 1;
					if (local_realtime != NULL) {
						free(local_realtime);
					}
					local_realtime = s_calloc(maxsize);
					if (utf8_local("realtime.",
					    &coXtarhdr.x_realtime,
					    local_realtime, val,
					    maxsize) == 0) {
						oxxhdr_flgs |= _X_REALTIME;
					} else {
						errors++;
					}
					break;
				case _X_SECURITY:
					maxsize = strlen(val) *
					    UTF_8_FACTOR + 1;
					if (local_security != NULL) {
						free(local_security);
					}
					local_security = s_calloc(maxsize);
					if (utf8_local("security.",
					    &coXtarhdr.x_security,
					    local_security,
					    val, maxsize) == 0) {
						oxxhdr_flgs |= _X_SECURITY;
					} else {
						errors++;
					}
					break;

				case _X_HOLESDATA:
					maxsize = strlen(val) *
					    UTF_8_FACTOR + 1;
					if (local_holesdata != NULL) {
						free(local_holesdata);
					}
					local_holesdata = s_calloc(maxsize);
					if (utf8_local("SUN.holesdata",
					    &coXtarhdr.x_holesdata,
					    local_holesdata,
					    val, maxsize) == 0) {
						oxxhdr_flgs |= _X_HOLESDATA;
					} else {
						errors++;
					}
					break;

				default:
					break;
				}
			}
		}
		i++;
	}
	if (errors && f_exit_on_error)
		exit(1);
	if (errors) {
		return (HDR_ERROR);
	} else {
		return (HDR_OK);
	}
}

/*
 * Process the command line "-o keyword=pair" overrides.  Saving the
 * values to the Xtarhdr structure will override any values that
 * existed if keyword/value pairs were processed while reading a
 * global extended header (in get_xdata()).
 */
int
get_oghdrdata(void)
{
	char	*val;
	int	errors = 0;
	int	i;
	int	len;
	int	maxsize;


	if (!firstxhdr) {
		/* free memory */
		if (coXtarhdr.gx_uname != NULL) {
			free(coXtarhdr.gx_uname);
		}
		if (coXtarhdr.gx_gname != NULL) {
			free(coXtarhdr.gx_gname);
		}
		if (coXtarhdr.gx_linkpath != NULL) {
			free(coXtarhdr.gx_linkpath);
		}
		if (coXtarhdr.gx_path != NULL) {
			free(coXtarhdr.gx_path);
		}
		if (coXtarhdr.gx_charset != NULL) {
			free(coXtarhdr.gx_charset);
		}
		if (coXtarhdr.gx_comment != NULL) {
			free(coXtarhdr.gx_comment);
		}
		if (coXtarhdr.gx_realtime != NULL) {
			free(coXtarhdr.gx_realtime);
		}
		if (coXtarhdr.gx_security != NULL) {
			free(coXtarhdr.gx_security);
		}
		if (coXtarhdr.gx_holesdata != NULL) {
			free(coXtarhdr.gx_holesdata);
		}
	}

	coXtarhdr.gx_uid = 0;
	coXtarhdr.gx_gid = 0;
	coXtarhdr.gx_devmajor = 0;
	coXtarhdr.gx_devminor = 0;
	coXtarhdr.gx_filesz = 0;
	coXtarhdr.gx_uname = NULL;
	coXtarhdr.gx_gname = NULL;
	coXtarhdr.gx_linkpath = NULL;
	coXtarhdr.gx_path = NULL;
	coXtarhdr.gx_atime.tv_sec = 0;
	coXtarhdr.gx_atime.tv_nsec = 0;
	coXtarhdr.gx_mtime.tv_sec = 0;
	coXtarhdr.gx_mtime.tv_nsec = 0;
	coXtarhdr.gx_charset = NULL;
	coXtarhdr.gx_comment = NULL;
	coXtarhdr.gx_realtime = NULL;
	coXtarhdr.gx_security = NULL;
	coXtarhdr.gx_holesdata = NULL;
	ogxhdr_flgs = 0;

	/* easy case - no keyword=pair options specified on cmd line */
	if (goptlist == NULL) {
		return (HDR_OK);
	}

	i = 0;
	while (keylist_pair[i].keynum != (int)_X_LAST) {
		if (!OMITOPT(keylist_pair[i].keylist)) {
			val = NULL;
			if (nvlist_lookup_string(goptlist,
			    keylist_pair[i].keylist, &val) == 0) {
				errno = 0;
				switch (keylist_pair[i].keynum) {
				case _X_DEVMAJOR:
					coXtarhdr.gx_devmajor =
					    (major_t)strtoul(val, NULL, 0);
					if (errno) {
						diag(gettext(
						    "invalid device major "
						    "number override value "
						    "%s\n"), val);
						exit_status = 1;
						errors++;
					} else
						ogxhdr_flgs |= _X_DEVMAJOR;
					break;

				case _X_DEVMINOR:
					coXtarhdr.gx_devminor =
					    (minor_t)strtoul(val, NULL, 0);
					if (errno) {
						diag(gettext(
						    "invalid device minor "
						    "number override value "
						    "%s\n"), val);
						exit_status = 1;
						errors++;
					} else
						ogxhdr_flgs |= _X_DEVMINOR;
					break;

				case _X_GID:
					ogxhdr_flgs |= _X_GID;
					f_group = 1;
					coXtarhdr.gx_gid = strtol(val, NULL, 0);
					if ((errno) ||
					    (coXtarhdr.gx_gid > UID_MAX)) {
						diag(gettext(
						    "invalid gid "
						    "override value "
						    "%s\n"), val);
						exit_status = 1;
						coXtarhdr.gx_gid = GID_NOBODY;
					}
					break;

				case _X_GNAME:
					maxsize = strlen(val) *
					    UTF_8_FACTOR + 1;
					if (local_gname != NULL) {
						free(local_gname);
					}
					local_gname = s_calloc(maxsize);
					if (utf8_local("gname",
					    &coXtarhdr.gx_gname,
					    local_gname, val,
					    maxsize) == 0) {
						ogxhdr_flgs |= _X_GNAME;
						f_group = 1;
					}
					break;

				case _X_LINKPATH:
					len = strlen(val) * UTF_8_FACTOR;
					maxsize = UTF8PATHSIZE;

					if ((len + 1) > UTF8PATHSIZE) {
						maxsize = len + 1;
					}
					if (glocal_linkpath != NULL) {
						free(glocal_linkpath);
					}
					glocal_linkpath = s_calloc(maxsize);
					if (utf8_local("linkpath",
					    &coXtarhdr.gx_linkpath,
					    glocal_linkpath, val,
					    maxsize) == 0) {
						ogxhdr_flgs |=
						    _X_LINKPATH;
					} else {
						errors++;
					}
					break;

				case _X_PATH:
					len = strlen(val) * UTF_8_FACTOR;
					maxsize = UTF8PATHSIZE;

					if (len + 1 > UTF8PATHSIZE) {
						maxsize = len + 1;
					}
					if (glocal_path != NULL) {
						free(glocal_path);
					}
					glocal_path = s_calloc(maxsize);
					if (utf8_local("path",
					    &coXtarhdr.gx_path,
					    glocal_path, val,
					    maxsize) == 0) {
						ogxhdr_flgs |= _X_PATH;
					} else {
						errors++;
					}
					break;

				case _X_SIZE:
					coXtarhdr.gx_filesz = strtoull(val,
					    NULL, 0);
					if (errno) {
						(void) fprintf(stderr, gettext(
						    "invalid global override "
						    "filesize\n"));
						exit_status = 1;
						errors++;
					} else {
						ogxhdr_flgs |= _X_SIZE;
					}
					break;

				case _X_UID:
					ogxhdr_flgs |= _X_UID;
					f_user = 1;
					coXtarhdr.gx_uid = strtol(val, NULL, 0);
					if ((errno) ||
					    (coXtarhdr.gx_uid > UID_MAX)) {
						diag(gettext(
						    "invalid uid "
						    "override value "
						    "%s\n"), val);
						exit_status = 1;
						coXtarhdr.gx_uid = UID_NOBODY;
					}
					break;

				case _X_UNAME:
					maxsize = strlen(val) *
					    UTF_8_FACTOR + 1;
					if (local_uname != NULL) {
						free(local_uname);
					}
					local_uname = s_calloc(maxsize);
					if (utf8_local("uname",
					    &coXtarhdr.gx_uname, local_uname,
					    val, maxsize) == 0) {
						ogxhdr_flgs |= _X_UNAME;
						f_user = 1;
					}
					break;

				case _X_ATIME:
					get_xtime(val, &(coXtarhdr.gx_atime));
					if (errno) {
						diag(gettext(
						    "invalid file access time "
						    "override value "
						    "%s\n"), val);
						exit_status = 1;
					} else {
						ogxhdr_flgs |= _X_ATIME;
						f_extract_access_time = 1;
					}
					break;

				case _X_MTIME:
					get_xtime(val, &(coXtarhdr.gx_mtime));
					if (errno) {
						diag(gettext(
						    "invalid file modification "
						    "time override value "
						    "%s\n"), val);
						exit_status = 1;
					} else {
						ogxhdr_flgs |= _X_MTIME;
						f_mtime = 1;
					}
					break;

				case _X_CHARSET:
					maxsize = strlen(val) *
					    UTF_8_FACTOR + 1;
					if (local_charset != NULL) {
						free(local_charset);
					}
					local_charset = s_calloc(maxsize);
					if (utf8_local("charset",
					    &coXtarhdr.gx_charset,
					    local_charset,
					    val, maxsize) == 0) {
						ogxhdr_flgs |= _X_CHARSET;
					} else {
						errors++;
					}
					break;
				case _X_COMMENT:
					maxsize = strlen(val) *
					    UTF_8_FACTOR + 1;
					if (local_comment != NULL) {
						free(local_comment);
					}
					local_comment = s_calloc(maxsize);
					if (utf8_local("comment",
					    &coXtarhdr.gx_comment,
					    local_comment,
					    val, maxsize) == 0) {
						ogxhdr_flgs |= _X_COMMENT;
					} else {
						errors++;
					}
					break;
				case _X_REALTIME:
					maxsize = strlen(val) *
					    UTF_8_FACTOR + 1;
					if (local_realtime != NULL) {
						free(local_realtime);
					}
					local_realtime = s_calloc(maxsize);
					if (utf8_local("realtime.",
					    &coXtarhdr.gx_realtime,
					    local_realtime,
					    val, maxsize) == 0) {
						ogxhdr_flgs |= _X_REALTIME;
					} else {
						errors++;
					}
					break;
				case _X_SECURITY:
					maxsize = strlen(val) *
					    UTF_8_FACTOR + 1;
					if (local_security != NULL) {
						free(local_security);
					}
					local_security = s_calloc(maxsize);
					if (utf8_local("security.",
					    &coXtarhdr.gx_security,
					    local_security,
					    val, maxsize) == 0) {
						ogxhdr_flgs |= _X_SECURITY;
					} else {
						errors++;
					}
					break;

				case _X_HOLESDATA:
					maxsize = strlen(val) *
					    UTF_8_FACTOR + 1;
					if (local_holesdata != NULL) {
						free(local_holesdata);
					}
					local_holesdata = s_calloc(maxsize);
					if (utf8_local("SUN.holesdata",
					    &coXtarhdr.gx_holesdata,
					    local_holesdata,
					    val, maxsize) == 0) {
						ogxhdr_flgs |= _X_HOLESDATA;
					} else {
						errors++;
					}
					break;

				default:
					break;
				}
			}
		}
		i++;
	}
	if (errors && f_exit_on_error)
		exit(1);
	if (errors) {
		return (HDR_ERROR);
	} else {
		return (HDR_OK);
	}
}


/*
 * Read the data record for extended headers and then the regular header.
 * The data are read into the buffer and then null-terminated.  Entries
 * are of the format:
 * 	"%d %s=%s\n"
 *
 * When an extended header record is found, the extended header must
 * be processed and its values used to override the values in the
 * normal header.  The way this is done is to process the extended
 * header data record and set the data values, then process the extended
 * header overrides specified on the command line with the -o option merging
 * the overrides into a coXtarhdr structure (available when in pax interchange
 * mode), then call read_header() to process the regular header, then to
 * reconcile the possible three sets of data.  Note: There is one exception
 * with the overrides in the pax interchange mode:  if -oinvalid=keyword
 * is specified on the command line, then the override value for that
 * keyword is ignored.
 *
 * When an extended header record is processed when in pax interchange
 * mode (-x pax specified on the command line), then there are two different
 * header types which may contain possible overrides: typeflag 'g' header
 * typeflag 'x' header.
 *
 * The typeflag 'g' global extended header record contains
 * keyword/value pairs.  Each of these values affects all subsequent
 * files that do not override that value in their own extended header
 * record (typeflag 'x') and until another global extended header record
 * is reached that provides another value for the same field.
 *
 * The typeflag 'x' extended header record contains keyword/value pairs
 * which either add attributes to the following file, or override values
 * in the following header block(s).
 *
 * The typeflag 'g' and typeflag 'x' extended header consists of one or
 * more records with the following format:
 * 	"%d %s=%s\n", <length>, <keyword>, <value>
 * These values override the fields in the following header block(s).
 * Note: This is the same format contained in the typeflag 'X' (-x xustar
 * specified on the command line).
 *
 * Possible keywords of keyword/value pairs are as follows:
 * 	atime		File access time.  This will be restored when
 *			process has appropriate privilege.
 * 	charset		Name of character set used to encode the data.
 *	comment		Series of characters used as a comment.  The
 *			value field is ignored by pax.
 *	gid		Group ID of the group that owns the file(s).
 *	gname		The group name of the file.
 *	linkpath	The pathname of a link created to another file
 *			previously archived.
 *	mtime		File modification time.  This will be restored
 *			when proess has appropriate privilege.
 *	path		The pathname of the following file(s).  Overrides
 *			name and prefix fields in the following header
 *			block(s).
 *	realtime.   	Keywords prefixed by "realtime." are reserved for
 *			future standardization.
 *	security.   	Keywords prefixed by "security." are reserved for
 *			future standardization.
 *	size		Size of the following file in octets.
 *	uid		User ID of the file owner.
 *	uname		Owner of the following file(s).
 *	SUNW.holesdata	Data pairs: start of data, start of hole.  Used
 *			to restore a sparse file.
 *
 * Note: the Xtarhdr entries can be overwritten by get_oxhdrdata() and
 * get_oghdrdata() as the keyword/value pairs specified on the command
 * line are processed by these two routines and those values take
 * precedence over keyword/value pairs specified in an extended header
 * block.
 *
 * Return 1 for success, 0 if the checksum is bad, EOF on eof, 2 for a
 * record full of zeros (EOF marker).
 */

int
get_xdata(void)
{
	char		*lineloc;
	int		length, i;
	int		maxsize;
	char		*keyword, *value;
	blkcnt_t	nblocks;
	off_t		bufneeded;
	int		errors;
	long		sum;
	off_t		xbufsize;
	char		hdrbuf[BLOCKSIZE];

	if (!firstxhdr) {
		/* free memory */
		if (Xtarhdr.x_uname != NULL) {
			free(Xtarhdr.x_uname);
		}
		if (Xtarhdr.x_gname != NULL) {
			free(Xtarhdr.x_gname);
		}
		if (Xtarhdr.x_linkpath != NULL) {
			free(Xtarhdr.x_linkpath);
		}
		if (Xtarhdr.x_path != NULL) {
			free(Xtarhdr.x_path);
		}
		if (Xtarhdr.x_charset != NULL) {
			free(Xtarhdr.x_charset);
		}
		if (Xtarhdr.x_comment != NULL) {
			free(Xtarhdr.x_comment);
		}
		if (Xtarhdr.x_realtime != NULL) {
			free(Xtarhdr.x_realtime);
		}
		if (Xtarhdr.x_security != NULL) {
			free(Xtarhdr.x_security);
		}
		if (Xtarhdr.x_holesdata != NULL) {
			free(Xtarhdr.x_holesdata);
		}
	}

	Xtarhdr.x_uid = 0;
	Xtarhdr.x_gid = 0;
	Xtarhdr.x_devmajor = 0;
	Xtarhdr.x_devminor = 0;
	Xtarhdr.x_filesz = 0;
	Xtarhdr.x_uname = NULL;
	Xtarhdr.x_gname = NULL;
	Xtarhdr.x_linkpath = NULL;
	Xtarhdr.x_path = NULL;
	Xtarhdr.x_atime.tv_sec = 0;
	Xtarhdr.x_atime.tv_nsec = 0;
	Xtarhdr.x_mtime.tv_sec = 0;
	Xtarhdr.x_mtime.tv_nsec = 0;
	Xtarhdr.x_charset = NULL;
	Xtarhdr.x_comment = NULL;
	Xtarhdr.x_realtime = NULL;
	Xtarhdr.x_security = NULL;
	Xtarhdr.x_holesdata = NULL;
	xhdr_flgs = 0;
	errors = 0;

	/*
	 * Only reinitialize the global extended header data the
	 * once for each archive volume.
	 */
	if (firstxhdr) {
		if (f_stdpax) {
			Xtarhdr.gx_uid = 0;
			Xtarhdr.gx_gid = 0;
			Xtarhdr.gx_devmajor = 0;
			Xtarhdr.gx_devminor = 0;
			Xtarhdr.gx_filesz = 0;
			Xtarhdr.gx_uname = NULL;
			Xtarhdr.gx_gname = NULL;
			Xtarhdr.gx_linkpath = NULL;
			Xtarhdr.gx_path = NULL;
			Xtarhdr.gx_atime.tv_sec = 0;
			Xtarhdr.gx_atime.tv_nsec = 0;
			Xtarhdr.gx_mtime.tv_sec = 0;
			Xtarhdr.gx_mtime.tv_nsec = 0;
			Xtarhdr.gx_charset = NULL;
			Xtarhdr.gx_comment = NULL;
			Xtarhdr.gx_realtime = NULL;
			Xtarhdr.gx_security = NULL;
			Xtarhdr.gx_holesdata = NULL;
			gxhdr_flgs = 0;
		}
		firstxhdr = 0;
	}

	if (f_append)
		lastheader = bufidx;		/* remember for backup */

	if (buf_read(hdrbuf, BLOCKSIZE) != 0)
		return (HDR_EOF);
	if ((sum = cksum(hdrbuf)) == -1)
		return (HDR_ZEROREC);
	xhdr_count++;
	if (sum != (long)from_oct(8, &hdrbuf[TO_CHKSUM])) {
		(void) fprintf(stderr, gettext(
		    "checksum error on extended header record in "
		    "file # %llu\n"), xhdr_count);
		exit_status = 1;
		return (HDR_ERROR);
	}
	if (f_stdpax) {
		if ((hdrbuf[TO_TYPEFLG] != GXHDRTYPE) &&
		    (hdrbuf[TO_TYPEFLG] != XXHDRTYPE) &&
		    (hdrbuf[TO_TYPEFLG] != XHDRTYPE)) {
			/* Make sure we process at least one ext hdr record */
			if (xhdr_count == 1) {
				diag(gettext(
				    "expected extended header record in "
				    "file # %llu\n"), xhdr_count);
				exit_status = 1;
			}
			/*
			 * Save the hdr data as we'll need it when reading the
			 * header.
			 */
			if ((shdrbuf = malloc(BLOCKSIZE)) == NULL) {
				fatal(gettext("out of memory"));
			}
			(void) memcpy(shdrbuf, hdrbuf, BLOCKSIZE);
			xhdr_count--;
			xcont = 0;
			return (HDR_NOXHDR);
		}
	} else {
		if (hdrbuf[TO_TYPEFLG] != XHDRTYPE) {
			(void) fprintf(stderr, gettext(
			    "expected extended header record in file # %llu\n"),
			    xhdr_count);
			exit_status = 1;
			return (HDR_NOXHDR);
		}
	}
	xbufsize = (OFFSET) from_oct(1 + 12, &hdrbuf[TO_SIZE]);
	nblocks = ROUNDUP(xbufsize, BLOCKSIZE) / BLOCKSIZE;
	bufneeded = nblocks * BLOCKSIZE;
	if ((bufneeded >= xrec_size) || (xrec_ptr == NULL)) {
		if (xrec_ptr)
			free(xrec_ptr);
		xrec_size = MAX(bufneeded + 1, xrec_size);
		if ((xrec_ptr = malloc(xrec_size)) == NULL)
			fatal(gettext(
			    "cannot allocate extended header buffer"));
	}

	lineloc = xrec_ptr;

	while (nblocks-- > 0) {
		if (buf_read(lineloc, BLOCKSIZE) != 0)
			return (HDR_EOF);
		lineloc += BLOCKSIZE;
	}

	lineloc = xrec_ptr;
	xrec_ptr[xbufsize] = '\0';
	while (lineloc < xrec_ptr + xbufsize) {
		length = atoi(lineloc);
		*(lineloc + length - 1) = '\0';
		keyword = strchr(lineloc, ' ') + 1;
		value = strchr(keyword, '=') + 1;
		*(value - 1) = '\0';
		i = 0;
		lineloc += length;
		while (keylist_pair[i].keynum != (int)_X_LAST) {
			if (strcmp(keyword, keylist_pair[i].keylist) == 0)
				break;
			i++;
		}
		if (OMITOPT(keyword)) {
			continue;
		}
		errno = 0;
		switch (keylist_pair[i].keynum) {
		case _X_DEVMAJOR:
			if (f_stdpax) {
				if (hdrbuf[TO_TYPEFLG] != GXHDRTYPE) {
					Xtarhdr.x_devmajor = (major_t)strtoul(
					    value, NULL, 0);
				} else {
					Xtarhdr.gx_devmajor = (major_t)strtoul(
					    value, NULL, 0);
				}
			} else {
				Xtarhdr.x_devmajor = (major_t)strtoul(value,
				    NULL, 0);
			}
			if (errno) {
				(void) fprintf(stderr, gettext(
				    "extended header major value error for "
				    "file # %llu"), xhdr_count);
				exit_status = 1;
				errors++;
			} else {
				if (f_stdpax) {
					if (hdrbuf[TO_TYPEFLG] != GXHDRTYPE) {
						xhdr_flgs |= _X_DEVMAJOR;
					} else {
						gxhdr_flgs |= _X_DEVMAJOR;
					}
				} else {
					xhdr_flgs |= _X_DEVMAJOR;
				}
			}
			break;

		case _X_DEVMINOR:
			if (f_stdpax) {
				if (hdrbuf[TO_TYPEFLG] != GXHDRTYPE) {
					Xtarhdr.x_devminor = (minor_t)strtoul(
					    value, NULL, 0);
				} else {
					Xtarhdr.gx_devminor = (minor_t)strtoul(
					    value, NULL, 0);
				}
			} else {
				Xtarhdr.x_devminor = (minor_t)strtoul(value,
				    NULL, 0);
			}
			if (errno) {
				(void) fprintf(stderr, gettext(
				    "extended header minor value error for "
				    "file # %llu"), xhdr_count);
				exit_status = 1;
				errors++;
			} else {
				if (f_stdpax) {
					if (hdrbuf[TO_TYPEFLG] != GXHDRTYPE) {
						xhdr_flgs |= _X_DEVMINOR;
					} else {
						gxhdr_flgs |= _X_DEVMINOR;
					}
				} else {
					xhdr_flgs |= _X_DEVMINOR;
				}
			}
			break;

		case _X_GID:
			if (f_stdpax) {
				if (hdrbuf[TO_TYPEFLG] != GXHDRTYPE) {
					xhdr_flgs |= _X_GID;
					Xtarhdr.x_gid = strtol(value, NULL, 0);
					if (errno ||
					    (Xtarhdr.x_gid > UID_MAX)) {
						(void) fprintf(stderr, gettext(
						    "extended header gid value "
						    "error for file # %llu\n"),
						    xhdr_count);
						exit_status = 1;
						Xtarhdr.x_gid = GID_NOBODY;
					}
				} else {
					gxhdr_flgs |= _X_GID;
					Xtarhdr.gx_gid = strtol(value, NULL, 0);
					if (errno ||
					    (Xtarhdr.gx_gid > UID_MAX)) {
						(void) fprintf(stderr, gettext(
						    "extended header gid value "
						    "error for file # %llu\n"),
						    xhdr_count);
						exit_status = 1;
						Xtarhdr.gx_gid = GID_NOBODY;
					}
				}
			} else {
				xhdr_flgs |= _X_GID;
				Xtarhdr.x_gid = strtol(value, NULL, 0);
				if (errno || (Xtarhdr.x_gid > UID_MAX)) {
					(void) fprintf(stderr, gettext(
					    "extended header gid value error "
					    "for file # %llu\n"), xhdr_count);
					exit_status = 1;
					Xtarhdr.x_gid = GID_NOBODY;
				}
			}
			break;

		case _X_GNAME:
			maxsize = strlen(value) * UTF_8_FACTOR + 1;
			if (local_gname != NULL) {
				free(local_gname);
			}
			local_gname = s_calloc(maxsize);
			if (f_stdpax) {
				if (hdrbuf[TO_TYPEFLG] != GXHDRTYPE) {
					if (utf8_local("gname",
					    &Xtarhdr.x_gname, local_gname,
					    value, maxsize) == 0)
						xhdr_flgs |= _X_GNAME;
				} else {
					if (utf8_local("gname",
					    &Xtarhdr.gx_gname, local_gname,
					    value, maxsize) == 0)
						gxhdr_flgs |= _X_GNAME;
				}
			} else {
				if (utf8_local("gname", &Xtarhdr.x_gname,
				    local_gname, value, _POSIX_NAME_MAX) == 0)
					xhdr_flgs |= _X_GNAME;
			}
			break;

		case _X_LINKPATH:
			maxsize = strlen(value) * UTF_8_FACTOR + 1;

			if (local_linkpath != NULL) {
				free(local_path);
			}

			local_linkpath = s_calloc(maxsize);
			if (f_stdpax) {
				if (hdrbuf[TO_TYPEFLG] != GXHDRTYPE) {
					if (utf8_local("linkpath",
					    &Xtarhdr.x_linkpath, local_linkpath,
					    value, maxsize) == 0)
						xhdr_flgs |= _X_LINKPATH;
					else
						errors++;
				} else {
					if (utf8_local("linkpath",
					    &Xtarhdr.gx_linkpath,
					    local_linkpath, value,
					    maxsize) == 0)
						gxhdr_flgs |= _X_LINKPATH;
					else
						errors++;
				}
			} else {
				if (utf8_local("linkpath", &Xtarhdr.x_linkpath,
				    local_linkpath, value, PATH_MAX) == 0)
					xhdr_flgs |= _X_LINKPATH;
				else
					errors++;
			}
			break;

		case _X_PATH:
			maxsize = strlen(value) * UTF_8_FACTOR + 1;

			if (local_path != NULL) {
				free(local_path);
			}

			local_path = s_calloc(maxsize);
			if (f_stdpax) {
				if (hdrbuf[TO_TYPEFLG] != GXHDRTYPE) {
					if (utf8_local("path", &Xtarhdr.x_path,
					    local_path, value, maxsize) == 0)
						xhdr_flgs |= _X_PATH;
					else
						errors++;
				} else {
					if (utf8_local("path", &Xtarhdr.gx_path,
					    local_path, value, maxsize) == 0)
						gxhdr_flgs |= _X_PATH;
					else
						errors++;
				}
			} else {
				if (utf8_local("path", &Xtarhdr.x_path,
				    local_path, value, PATH_MAX) == 0)
					xhdr_flgs |= _X_PATH;
				else
					errors++;
			}
			break;

		case _X_SIZE:
			if (f_stdpax) {
				if (hdrbuf[TO_TYPEFLG] != GXHDRTYPE) {
					Xtarhdr.x_filesz = strtoull(value,
					    NULL, 0);
				} else {
					Xtarhdr.gx_filesz = strtoull(value,
					    NULL, 0);
				}
			} else {
				Xtarhdr.x_filesz = strtoull(value, NULL, 0);
			}
			if (errno) {
				(void) fprintf(stderr, gettext(
				    "extended header invalid filesize for "
				    "file # %llu"), xhdr_count);
				exit_status = 1;
				errors++;
			} else {
				if (f_stdpax) {
					if (hdrbuf[TO_TYPEFLG] != GXHDRTYPE) {
						xhdr_flgs |= _X_SIZE;
					} else {
						gxhdr_flgs |= _X_SIZE;
					}
				} else {
					xhdr_flgs |= _X_SIZE;
				}
			}
			break;

		case _X_UID:
			if (f_stdpax) {
				if (hdrbuf[TO_TYPEFLG] != GXHDRTYPE) {
					xhdr_flgs |= _X_UID;
					Xtarhdr.x_uid = strtol(value, NULL, 0);
					if (errno ||
					    (Xtarhdr.x_uid > UID_MAX)) {
						(void) fprintf(stderr, gettext(
						    "extended header uid value "
						    "error for file # %llu\n"),
						    xhdr_count);
						exit_status = 1;
						Xtarhdr.x_uid = UID_NOBODY;
					}
				} else {
					gxhdr_flgs |= _X_UID;
					Xtarhdr.gx_uid = strtol(value, NULL, 0);
					if (errno ||
					    (Xtarhdr.gx_uid > UID_MAX)) {
						(void) fprintf(stderr, gettext(
						    "extended header uid value "
						    "error for file # %llu\n"),
						    xhdr_count);
						exit_status = 1;
						Xtarhdr.gx_uid = UID_NOBODY;
					}
				}
			} else {
				xhdr_flgs |= _X_UID;
				Xtarhdr.x_uid = strtol(value, NULL, 0);
				if (errno || (Xtarhdr.x_uid > UID_MAX)) {
					(void) fprintf(stderr, gettext(
					    "extended header uid value error "
					    "for file # %llu\n"), xhdr_count);
					exit_status = 1;
					Xtarhdr.x_uid = UID_NOBODY;
				}
			}
			break;

		case _X_UNAME:
			maxsize = strlen(value) * UTF_8_FACTOR + 1;

			if (local_uname != NULL) {
				free(local_uname);
			}

			local_uname = s_calloc(maxsize);
			if (f_stdpax) {
				if (hdrbuf[TO_TYPEFLG] != GXHDRTYPE) {
					if (utf8_local("uname",
					    &Xtarhdr.x_uname, local_uname,
					    value, _POSIX_NAME_MAX) == 0)
						xhdr_flgs |= _X_UNAME;
				} else {
					if (utf8_local("uname",
					    &Xtarhdr.gx_uname, local_uname,
					    value, _POSIX_NAME_MAX) == 0)
						gxhdr_flgs |= _X_UNAME;
				}
			} else {
				if (utf8_local("uname", &Xtarhdr.x_uname,
				    local_uname, value, _POSIX_NAME_MAX) == 0)
					xhdr_flgs |= _X_UNAME;
			}
			break;

		case _X_ATIME:
			if (f_stdpax) {
				if (hdrbuf[TO_TYPEFLG] != GXHDRTYPE) {
					get_xtime(value, &(Xtarhdr.x_atime));
				} else {
					get_xtime(value, &(Xtarhdr.gx_atime));
				}
			} else {
				get_xtime(value, &(Xtarhdr.x_atime));
			}
			if (errno) {
				(void) fprintf(stderr, gettext(
				    "extended header access time value error "
				    "for file # %llu"), xhdr_count);
				exit_status = 1;
			} else {
				if (f_stdpax) {
					if (hdrbuf[TO_TYPEFLG] != GXHDRTYPE) {
						xhdr_flgs |= _X_ATIME;
					} else {
						gxhdr_flgs |= _X_ATIME;
					}
					/* always restore atime */
					f_extract_access_time = 1;
				} else {
					xhdr_flgs |= _X_ATIME;
				}
			}
			break;

		case _X_MTIME:
			if (f_stdpax) {
				if (hdrbuf[TO_TYPEFLG] != GXHDRTYPE) {
					get_xtime(value, &(Xtarhdr.x_mtime));
				} else {
					get_xtime(value, &(Xtarhdr.gx_mtime));
				}
			} else {
				get_xtime(value, &(Xtarhdr.x_mtime));
			}
			if (errno) {
				(void) fprintf(stderr, gettext(
				    "extended header modification time value "
				    "error for file # %llu"), xhdr_count);
				exit_status = 1;
			} else {
				if (f_stdpax) {
					if (hdrbuf[TO_TYPEFLG] != GXHDRTYPE) {
						xhdr_flgs |= _X_MTIME;
					} else {
						gxhdr_flgs |= _X_MTIME;
					}
					/* always restore mtime */
					f_mtime = 1;
				} else {
					xhdr_flgs |= _X_MTIME;
				}
			}
			break;

		case _X_CHARSET:
			if (f_stdpax) {
				maxsize = strlen(value) * UTF_8_FACTOR + 1;

				if (local_charset != NULL) {
					free(local_charset);
				}

				local_charset = s_calloc(maxsize);
				if (hdrbuf[TO_TYPEFLG] != GXHDRTYPE) {
					if (utf8_local("charset",
					    &Xtarhdr.x_charset, local_charset,
					    value, PATH_MAX) == 0) {
						xhdr_flgs |= _X_CHARSET;
					} else {
						errors++;
					}
				} else {
					if (utf8_local("charset",
					    &Xtarhdr.gx_charset, local_charset,
					    value, PATH_MAX) == 0) {
						gxhdr_flgs |= _X_CHARSET;
					} else {
						errors++;
					}
				}
			}
			break;
		case _X_COMMENT:
			if (f_stdpax) {
				maxsize = strlen(value) * UTF_8_FACTOR + 1;

				if (local_comment != NULL) {
					free(local_comment);
				}

				local_comment = s_calloc(maxsize);
				if (hdrbuf[TO_TYPEFLG] != GXHDRTYPE) {
					if (utf8_local("comment",
					    &Xtarhdr.x_comment, local_comment,
					    value, PATH_MAX) == 0) {
						xhdr_flgs |= _X_COMMENT;
					} else {
						errors++;
					}
				} else {
					if (utf8_local("comment",
					    &Xtarhdr.gx_comment, local_comment,
					    value, PATH_MAX) == 0) {
						gxhdr_flgs |= _X_COMMENT;
					} else {
						errors++;
					}
				}
			}
			break;
		case _X_REALTIME:
			if (f_stdpax) {
				maxsize = strlen(value) * UTF_8_FACTOR + 1;

				if (local_realtime != NULL) {
					free(local_realtime);
				}

				local_realtime = s_calloc(maxsize);
				if (hdrbuf[TO_TYPEFLG] != GXHDRTYPE) {
					if (utf8_local("realtime.",
					    &Xtarhdr.x_realtime, local_realtime,
					    value, PATH_MAX) == 0) {
						xhdr_flgs |= _X_REALTIME;
					} else {
						errors++;
					}
				} else {
					if (utf8_local("realtime.",
					    &Xtarhdr.gx_realtime,
					    local_realtime, value,
					    PATH_MAX) == 0) {
						gxhdr_flgs |= _X_REALTIME;
					} else {
						errors++;
					}
				}
			}
			break;
		case _X_SECURITY:
			if (f_stdpax) {
				maxsize = strlen(value) * UTF_8_FACTOR + 1;

				if (local_security != NULL) {
					free(local_security);
				}

				local_security = s_calloc(maxsize);
				if (hdrbuf[TO_TYPEFLG] != GXHDRTYPE) {
					if (utf8_local("security.",
					    &Xtarhdr.x_security, local_security,
					    value, PATH_MAX) == 0) {
						xhdr_flgs |= _X_SECURITY;
					} else {
						errors++;
					}
				} else {
					if (utf8_local("security.",
					    &Xtarhdr.gx_security,
					    local_security, value,
					    PATH_MAX) == 0) {
						gxhdr_flgs |= _X_SECURITY;
					} else {
						errors++;
					}
				}
			}
			break;

		case _X_HOLESDATA:
			maxsize = strlen(value) * UTF_8_FACTOR + 1;

			if (local_holesdata != NULL) {
				free(local_holesdata);
			}

			local_holesdata = s_calloc(maxsize);
			if (hdrbuf[TO_TYPEFLG] != GXHDRTYPE) {
				if (utf8_local("SUN.holesdata",
				    &Xtarhdr.x_holesdata,
				    local_holesdata,
				    value, PATH_MAX) == 0) {
					xhdr_flgs |= _X_HOLESDATA;
				} else {
					errors++;
				}
			} else {
				if (utf8_local("SUN.holesdata",
				    &Xtarhdr.gx_holesdata,
				    local_holesdata, value,
				    PATH_MAX) == 0) {
					gxhdr_flgs |= _X_HOLESDATA;
				} else {
					errors++;
				}
			}
			break;

		default:
			/*
			 * Silently ignore any extended header keywords
			 * we don't recognize.
			 */
			break;
		}
	}
	if (errors && f_exit_on_error)
		exit(1);

	if (f_stdpax && ((hdrbuf[TO_TYPEFLG] == XXHDRTYPE) ||
	    (hdrbuf[TO_TYPEFLG] == XHDRTYPE))) {
		xcont = 0;
	}

	if (errors) {
		return (HDR_ERROR);
	} else {
		return (HDR_OK);
	}
}

/*
 * Convert time found in the extended header data to seconds and nanoseconds.
 */

void
get_xtime(char *value, timestruc_t *xtime)
{
	char nanosec[10];
	char *period;
	int i;

	(void) memset(nanosec, '0', 9);
	nanosec[9] = '\0';

	period = strchr(value, '.');
	if (period != NULL)
		period[0] = '\0';
	xtime->tv_sec = strtol(value, NULL, 10);
	if (period == NULL)
		xtime->tv_nsec = 0;
	else {
		i = strlen(period +1);
		(void) strncpy(nanosec, period + 1, MIN(i, 9));
		xtime->tv_nsec = strtol(nanosec, NULL, 10);
	}
}

/*
 * Convert from UTF-8 to local character set.
 */

static int
utf8_local(
	char		*option,
	char		**Xhdr_ptrptr,
	char		*target,
	const char	*source,
	int		max_val)
{
	static	iconv_t	iconv_cd;
	char		*nl_target;
	char		*iconv_src;
	char		*iconv_trg;
	size_t		inlen;
	size_t		outlen;

	if (charset_type == CHARSET_ERROR) {	/* iconv_open failed earlier */
		(void) fprintf(stderr, gettext(
		    "%s:  file # %llu: (%s) UTF-8 conversion failed.\n"),
		    myname, xhdr_count, source);
		exit_status = 1;
		return (1);
	} else if (charset_type == CHARSET_UNKNOWN) {	/* no iconv_open yet */
		nl_target = nl_langinfo(CODESET);
		if (strlen(nl_target) == 0)	/* locale using 7-bit codeset */
			nl_target = "646";
		if (strcmp(nl_target, "646") == 0)
			charset_type = CHARSET_7_BIT;
		else if (strcmp(nl_target, "UTF-8") == 0)
			charset_type = CHARSET_UTF_8;
		else {
			if (strncmp(nl_target, "ISO", 3) == 0)
				nl_target += 3;
			charset_type = CHARSET_8_BIT;
			errno = 0;
			if ((iconv_cd = iconv_open(nl_target, "UTF-8")) ==
			    (iconv_t)-1) {
				if (errno == EINVAL)
					(void) fprintf(stderr, gettext(
					    "%s: conversion routines not "
					    "available for current locale.  "),
					    myname);
				(void) fprintf(stderr, gettext(
				    "file # %llu: (%s) UTF-8 conversion"
				    " failed.\n"), xhdr_count, source);
				charset_type = CHARSET_ERROR;
				exit_status = 1;
				return (1);
			}
		}
	}

	/* locale using 7-bit codeset or UTF-8 locale */
	if (charset_type == CHARSET_7_BIT || charset_type == CHARSET_UTF_8) {
		if (strlen(source) > max_val) {
			(void) fprintf(stderr, gettext(
			    "%s: file # %llu: extended header %s too long.\n"),
			    myname, xhdr_count, option);
			if (!f_stdpax) {
				exit_status = 1;
			}
			return (1);
		}
		if (charset_type == CHARSET_UTF_8) {
			(void) strcpy(target, source);
		} else if (c_utf8(target, source) != 0) {
			(void) fprintf(stderr, gettext(
			    "%s:  file # %llu: (%s) UTF-8 conversion"
			    " failed.\n"), myname, xhdr_count, source);
			exit_status = 1;
			return (1);
		}
		STRDUP(*Xhdr_ptrptr, target);
		return (0);
	}

	iconv_src = (char *)source;
	iconv_trg = target;
	inlen = strlen(source);
	outlen = max_val * UTF_8_FACTOR;
	if (iconv(iconv_cd, &iconv_src, &inlen, &iconv_trg, &outlen) ==
	    (size_t)-1) {	/* Error occurred:  didn't convert */
		(void) fprintf(stderr, gettext(
		    "%s:  file # %llu: (%s) UTF-8 conversion failed.\n"),
		    myname, xhdr_count, source);
		exit_status = 1;
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
		(void) fprintf(stderr, gettext(
		    "%s:  file # %llu: (%s) UTF-8 conversion failed.\n"),
		    myname, xhdr_count, source);
		exit_status = 1;
		return (1);
	}

	*iconv_trg = '\0';	/* Null-terminate iconv output string */
	if (strlen(target) > max_val) {
		(void) fprintf(stderr, gettext(
		    "%s: file # %llu: extended header %s too long.\n"),
		    myname, xhdr_count, option);
		exit_status = 1;
		return (1);
	}

	STRDUP(*Xhdr_ptrptr, target);
	return (0);
}

/*
 *	Function to test each byte of the source string to make sure it is
 *	in within bounds (value between 0 and 127).
 *	If valid, copy source to target.
 */

int
c_utf8(char *target, const char *source)
{
	size_t		len;
	const char	*thischar;

	len = strlen(source);
	thischar = source;
	while (len-- > 0) {
		if (!isascii((int)(*thischar++)))
			return (1);
	}

	(void) strcpy(target, source);
	return (0);
}

/*
 * Compute checksum.  If the record is an EOF record, return -1.  Otherwise,
 * return the checksum.
 */

int
cksum(char *hdrbuf) {
	long sum;
	char *p = hdrbuf;
	int i;

	sum = 0;
	for (i = 0; i < TL_TOTAL_TAR; i++) {
		sum += 0xFF & *p++;
	}

	/* Adjust checksum to count the "chksum" field as blanks. */
	for (i = 0; i < 8; i++) {
		sum -= 0xFF & hdrbuf[TO_CHKSUM + i];
	}
	sum += ' ' * 8;

	if (sum == 8 * ' ') {
		/*
		 * This is a zeroed record...whole record is 0's except for
		 * the 8 blanks we faked for the checksum field.
		 */
		return (-1);
	}
	return (sum);
}
