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
 * provided that the above copyright notice and this paragraph are
 * duplicated in all such forms and that any documentation,
 * advertising materials, and other materials related to such
 * distribution and use acknowledge that the software was developed
 * by Mark H. Colburn and sponsored by The USENIX Association.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef _PAX_H
#define	_PAX_H

/*
 * pax.h - definitions for entire program
 *
 * DESCRIPTION
 *
 *	This file contains most all of the definitions required by the PAX
 *	software.  This header is included in every source file.
 */

/* Headers */

#include <unistd.h>
#include <errno.h>
#include <nl_types.h>
#include <libintl.h>
#include <locale.h>
#include <limits.h>
#include <regex.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <sys/nvpair.h>

#ifdef	__cplusplus
extern "C" {
#endif

#include "config.h"

/* Note include of "func.h" at the end; it requires some of the */
/* intervening definitions to be truly happy. */

#ifndef FALSE
#define	FALSE (0 == 1)
#endif /* FALSE */

#ifndef TRUE
#define	TRUE (0 == 0)
#endif /* TRUE */

/* Defines */
#define	STRDUP(s1, s2)	if ((s1 = strdup(s2)) == NULL) { \
				fatal(strerror(errno)); \
			}

#define	STDIN	0		/* Standard input  file descriptor */
#define	STDOUT	1		/* Standard output file descriptor */
#define	MAXSTRLEN 256		/* Maximum string length */
#define	UTF8PATHSIZE	UTF_8_FACTOR * PATH_MAX + 1

#define	DEVNULL		"/dev/null"		/* /dev/null string */

#define	TMAGIC		"ustar"		/* ustar and a null */
#define	TMAGLEN		6
#define	TVERSION	"00"		/* 00 and no null */
#define	TVERSLEN	2

/* Extended system attributes */
#ifndef	VIEW_READONLY
#define	VIEW_READONLY	"SUNWattr_ro"
#endif

#ifndef	VIEW_READWRITE
#define	VIEW_READWRITE	"SUNWattr_rw"
#endif

#if defined(O_XATTR)
typedef enum {
	ATTR_OK,
	ATTR_SKIP,
	ATTR_CHDIR_ERR,
	ATTR_OPEN_ERR,
	ATTR_XATTR_ERR,
	ATTR_SATTR_ERR
} attr_status_t;
#endif

#if defined(O_XATTR)
typedef enum {
	ARC_CREATE,
	ARC_RESTORE
} arc_action_t;
#endif

/* Values used in typeflag field */
#define	REGTYPE		'0'		/* Regular File */
#define	AREGTYPE	'\0'		/* Regular File */
#define	LNKTYPE		'1'		/* Link */
#define	SYMTYPE		'2'		/* Reserved */
#define	CHRTYPE		'3'		/* Character Special File */
#define	BLKTYPE		'4'		/* Block Special File */
#define	DIRTYPE		'5'		/* Directory */
#define	FIFOTYPE	'6'		/* FIFO */
#define	CONTTYPE	'7'		/* Reserved */
#define	XHDRTYPE	'X'		/* Extended header */
#define	XXHDRTYPE	'x'		/* Pax extended header */
#define	GXHDRTYPE	'g'		/* Global extended header */

/*
 *  Blocking defaults: blocking factor and block size.
 */

#define	DEFBLK_TAR	20	/* default blocking factor for tar */
#define	DEFBLK_CPIO	10	/* default blocking factor for cpio */
#define	BLOCKSIZE	512	/* all output is padded to 512 bytes */

#define	BLOCK	5120		/* Default archive block size */
#define	H_COUNT	10		/* Number of items in ASCII header */
#define	H_PRINT	"%06o%06o%06o%06o%06o%06o%06o%011lo%06o%011llo"
#define	H_SCAN	"%6lo%6llo%6lo%6o%6o%6lo%6lo%11lo%6o%11llo"
#define	H_STRLEN 70		/* ASCII header string length */
#define	M_ASCII "070707"	/* ASCII magic number */
#define	M_BINARY 070707		/* Binary magic number */
#define	M_STRLEN 6		/* ASCII magic number length */
#define	PATHELEM 256		/* Pathname element count limit */
#define	S_IFSHF	12		/* File type shift (shb in stat.h) */
#define	S_IPERM	01777		/* File permission bits (shb in stat.h) */
#define	S_IPOPN	0777		/* Open access bits (shb in stat.h) */

#define	TAR_OFFSET_MAX	077777777777ULL	/* largest we can archive without */
					/* extensions.  (-E for tar or */
					/* -x xustar for pax */
#define	OCTAL7CHAR	07777777	/* Limit for ustar gid, uid, dev */
					/* unless extended headers are used */

#define	STREQUAL(A, B)	((!(A) || !(B)) ? 0 : \
			    ((strlen(A) != strlen(B)) ? 0 : \
			    (strcmp((A), (B)) == 0)))

#define	OMITOPT(k)	((f_stdpax == 1) ? ((deleteopt != NULL) ? \
			    (is_opt_match(deleteopt, "delete", \
			    k) == 0) : 0) : 0)


/*
 * Access the offsets of the various fields within the tar header
 * (defined in main.c).
 */
extern const int TO_NAME;
extern const int TO_MODE;
extern const int TO_UID;
extern const int TO_GID;
extern const int TO_SIZE;
extern const int TO_MTIME;
extern const int TO_CHKSUM;
extern const int TO_TYPEFLG;
extern const int TO_LINKNAME;
extern const int TO_MAGIC;
extern const int TO_VERSION;
extern const int TO_UNAME;
extern const int TO_GNAME;
extern const int TO_DEVMAJOR;
extern const int TO_DEVMINOR;
extern const int TO_PREFIX;

/*
 *  Access the field lengths for the fields of the tar header
 */

#define	TL_NAME		100
#define	TL_MODE		8
#define	TL_UID		8
#define	TL_GID		8
#define	TL_SIZE		12
#define	TL_MTIME	12
#define	TL_CHKSUM	8
#define	TL_TYPEFLG	1
#define	TL_LINKNAME	100
#define	TL_MAGIC	6
#define	TL_VERSION	2
#define	TL_UNAME	32
#define	TL_GNAME	32
#define	TL_DEVMAJOR	8
#define	TL_DEVMINOR	8
#define	TL_PREFIX	155
#define	TL_TOTAL_TAR	512	/* checksum must cover all 512 bytes */

/*
 *  Because some static global arrays inside names.c use these #defines,
 *  we will keep them around until a better method can be devised for
 *  creating them.
 */

#define	TUNMLEN		32
#define	TGNMLEN		32

/*
 * Trailer pathnames. All must be of the same length.
 */
#define	TRAILER	"TRAILER!!!"	/* Archive trailer (cpio compatible) */
#define	TRAILZ	11		/* Trailer pathname length (including null) */

#define	TAR		1
#define	CPIO		2
#define	PAX		3

#define	AR_READ 	0
#define	AR_WRITE 	1
#define	AR_EXTRACT	2
#define	AR_APPEND 	4

/* defines for get_disposition */
#define	ADD		1
#define	EXTRACT		2
#define	PASS		3


/* The checksum field is filled with this while the checksum is computed. */
#define	CHKBLANKS	"        "	/* 8 blanks, no null */

/*
 * Exit codes from the "tar" program
 */
#define	EX_SUCCESS	0	/* success! */
#define	EX_ARGSBAD	1	/* invalid args */
#define	EX_BADFILE	2	/* invalid filename */
#define	EX_BADARCH	3	/* bad archive */
#define	EX_SYSTEM	4	/* system gave unexpected error */

#define	ROUNDUP(a, b) 	(((a) % (b)) == 0 ? (a) : ((a) + ((b) - ((a) % (b)))))
#define	min(a, b)  ((a) < (b) ? (a) : (b))
#define	max(a, b)  ((a) > (b) ? (a) : (b))

/*
 * Exit codes from read_header
 */

#define	HDR_FIRSTREC	-1
#define	HDR_OK		0
#define	HDR_EOF		1
#define	HDR_ZEROREC	2
#define	HDR_ERROR	3
#define	HDR_NOXHDR	4
#define	HDR_OERROR	5

/*
 * Minimum value.
 */

#ifndef MIN
#define	MIN(a, b)	(((a) < (b)) ? (a) : (b))
#endif


/*
 * Maximum value.
 */

#ifndef MAX
#define	MAX(a, b)	(((a) > (b)) ? (a) : (b))
#endif

/*
 * Remove a file or directory.
 */
#define	REMOVE(name, asb) \
	(((asb)->sb_mode & S_IFMT) == S_IFDIR ? rmdir(name) : unlink(name))

/*
 * Cast and reduce to unsigned short.
 */
#define	USH(n)		(((ushort_t)(n)) & 0177777)

/*
 * Swap bytes
 */
#define	SWAB(n)	((((ushort_t)(n) >> 8) & 0xff) | \
		    (((ushort_t)(n) << 8) & 0xff00))


/* Type Definitions */

/*
 * Binary archive header (obsolete).
 */
typedef struct {
	short		b_dev;		/* Device code */
	ushort_t	b_ino;		/* Inode number */
	ushort_t	b_mode;		/* Type and permissions */
	ushort_t	b_uid;		/* Owner */
	ushort_t	b_gid;		/* Group */
	short		b_nlink;	/* Number of links */
	short		b_rdev;		/* Real device */
	ushort_t	b_mtime[2];	/* Modification time (hi/lo) */
	ushort_t	b_name;		/* Length of pathname (with null) */
	ushort_t	b_size[2];	/* Length of data */
}	Binary;

struct xattr_info {
	struct xattr_hdr *xattrhead;		/* pointer to xattr header */
	struct xattr_buf *xattrp;		/* xattr pathing info */
	struct xattr_buf *xattr_linkp;		/* xattr pathing link info */
	char		 *xattrfname;		/* attr base file name */
	char		 *xattraname;		/* attr name */
	char		 *xattraparent;		/* attr parent name */
	char		 *xattrapath;		/* attr path */
	char		 *xattr_linkfname;	/* attr link base */
	char		 *xattr_linkaname;	/* attr linked to */
	int		 xattr_baseparent_fd;	/* xattr base file's cwd */
	int		 xattr_rw_sysattr;	/* read-write sys attr flag */
};


/*
 * File status with symbolic links. Kludged to hold symbolic link pathname
 * within structure.
 */
typedef struct {
	struct stat		sb_stat;
	char			sb_link[PATH_MAX + 1];
	char			*linkname;		/* for tar links */
	int			symlinkref;  /* file is ref'd by a symlink */
	struct xattr_info	xattr_info;  /* special info about xattr */
} Stat;

typedef struct {
	char	*name;
	size_t	nsize;
} Name;

#define	STAT(name, asb)		stat(name, &(asb)->sb_stat)
#define	FSTAT(fd, asb)		fstat(fd, &(asb)->sb_stat)

#define	sb_dev		sb_stat.st_dev
#define	sb_ino		sb_stat.st_ino
#define	sb_mode		sb_stat.st_mode
#define	sb_nlink	sb_stat.st_nlink
#define	sb_uid		sb_stat.st_uid
#define	sb_gid		sb_stat.st_gid
#define	sb_rdev		sb_stat.st_rdev
#define	sb_size		sb_stat.st_size
#define	sb_atime	sb_stat.st_atime
#define	sb_mtime	sb_stat.st_mtime

#ifdef	S_IFLNK
#define	LSTAT(name, asb)	lstat(name, &(asb)->sb_stat)
#define	sb_blksize		sb_stat.st_blksize
#define	sb_blocks		sb_stat.st_blocks
#else			/* S_IFLNK */
/*
 * File status without symbolic links.
 */
#define	LSTAT(name, asb)	stat(name, &(asb)->sb_stat)
#endif			/* S_IFLNK */

/*
 * Hard link sources. One or more are chained from each link structure.
 */
typedef struct name {
	struct name	*p_forw;	/* Forward chain (terminated) */
	struct name	*p_back;	/* Backward chain (circular) */
	char		 *p_name;	/* Pathname to link from */
	char		 *p_attr;	/* name of attribute */
} Path;

/*
 * File linking information. One entry exists for each unique file with
 * outstanding hard links.
 */
typedef	struct link {
	struct link	*l_forw;	/* Forward chain (terminated) */
	struct link	*l_back;	/* Backward chain (terminated) */
	dev_t		l_dev;		/* Device */
	ino_t		l_ino;		/* Inode */
	ushort_t	l_nlink;	/* Unresolved link count */
	OFFSET		l_size;		/* Length */
	char		*l_name;	/* pathname to link from */
	char		*l_attr;	/* attribute name */
	Path		*l_path;	/* Pathname which link to l_name */
} Link;

/*
 * Structure for ed-style replacement strings (-s option).
 */
typedef struct replstr {
	regex_t		comp;		/* compiled regular expression */
	char		*replace;	/* replacement string */
	char		print;		/* >0 if we are to print replacement */
	char		global;		/* >0 if we are to replace globally */
	struct replstr	*next;		/* pointer to next record */
} Replstr;

/*
 * Structure for list of directories
 */
typedef struct dirlist {
	char		*name;	/* name of the directory */
	uid_t		uid;	/* user id */
	gid_t		gid;	/* group id */
	mode_t		perm;	/* directory mode */
	struct timeval	atime;	/* directory access time */
	struct timeval	mtime;	/* directory modify time */
	struct dirlist	*next;	/* pointer to next record */
} Dirlist;

/*
 * Structure for the hash table
 */
typedef struct hashentry {
	char			*name;	/* Filename of entry */
	struct timeval		mtime;	/* modify time of file */
	struct hashentry	*next;	/* pointer to next entry */
} Hashentry;

#define	HTABLESIZE	(16*1024)	/* Hash table size */

struct xtar_hdr {
	uid_t		x_uid,		/* Uid of file */
			x_gid;		/* Gid of file */
	major_t		x_devmajor;	/* Device major	node */
	minor_t		x_devminor;	/* Device minor	node */
	off_t		x_filesz;	/* Length of file */
	char		*x_uname,	/* Pointer to uname */
			*x_gname,	/* Pointer to gid */
			*x_linkpath,	/* Path	for link */
			*x_path;	/* Path	of file	*/
	timestruc_t	x_mtime,	/* Seconds and nanoseconds */
			x_atime;
	char		*x_charset,	/* Character set used - pax */
			*x_comment,	/* Comment - pax */
			*x_realtime,	/* prefix "realtime." reserved - pax */
			*x_security,	/* prefix "security." reserved - pax */
			*x_holesdata;	/* holey file data */
	uid_t		gx_uid,		/* Uid of file */
			gx_gid;		/* Global Gid of file */
	major_t		gx_devmajor;	/* Global Device major	node */
	minor_t		gx_devminor;	/* Global Device minor	node */
	off_t		gx_filesz;	/* Global Length of file */
	char		*gx_uname,	/* Global Pointer to uname */
			*gx_gname,	/* Global Pointer to gid */
			*gx_linkpath,	/* Global Path	for link */
			*gx_path;	/* Global Path	of file	*/
	timestruc_t	gx_mtime,	/* Global Seconds and nanoseconds */
			gx_atime;
	char		*gx_charset,	/* Global Character set used */
			*gx_comment,	/* Global Comment */
			*gx_realtime,	/* Global prefix "realtime." reserved */
			*gx_security,	/* Global prefix "security." reserved */
			*gx_holesdata;	/* Global holey file data */
};


/*
 * This has to be included here to insure that all of the type
 * declarations are declared for the prototypes.
 */

#ifndef NO_EXTERN
/* Globally Available Identifiers */

extern char	*ar_file;
extern char	*bufend;
extern char	*bufstart;
extern char	*bufidx;
extern nl_catd	catd;
extern char	*lastheader;
extern char	*myname;
extern int	archivefd;
extern int	blocking;
extern uint_t	blocksize;
extern gid_t	gid;
extern int	head_standard;
extern int	ar_interface;
extern int	ar_format;
extern int	mask;
extern int	ttyf;
extern uid_t	uid;
extern int	exit_status;
extern int	firstxhdr;
extern int	xcont;
extern OFFSET	total;
extern short	areof;
extern short	f_access_time;
extern short	f_append;
extern short	f_atime;
extern short	f_blocking;
extern short	f_charmap;
extern short	f_cmdarg;
extern short	f_create;
extern short	f_device;
extern short	f_dir_create;
extern short	f_disposition;
extern short	f_exit_on_error;
extern short	f_extract;
extern short	f_extract_access_time;
extern short	f_extended_attr;
extern short	f_follow_first_link;
extern short	f_follow_links;
extern short	f_group;
extern short	f_interactive;
extern short	f_link;
extern short	f_linkdata;
extern short	f_linksleft;
extern short	f_list;
extern short	f_mtime;
extern short	f_mode;
extern short	f_newer;
extern short	f_no_depth;
extern short	f_no_overwrite;
extern short	f_owner;
extern short	f_pass;
extern short	f_pax;
extern short	f_posix;
extern short	f_reverse_match;
extern short	f_single_match;
extern short	f_stdpax;
extern short	f_sys_attr;
extern short	f_times;
extern short	f_unconditional;
extern short	f_user;
extern short	f_verbose;
extern short	rename_interact;
extern short	no_data_printed;
extern time_t	now;
extern uint_t	arvolume;
extern int	names_from_stdin;
extern Replstr	*rplhead;
extern Replstr	*rpltail;
extern char	**argv;
extern int	argc;
extern FILE	*msgfile;
extern Dirlist	*dirhead;
extern Dirlist	*dirtail;
extern short	bad_last_match;
extern int	Hiddendir;
extern int	xattrbadhead;
extern char	*exthdrnameopt;
extern char	*gexthdrnameopt;
extern int	invalidopt;
extern char	*listopt;
extern nvlist_t	*goptlist;
extern nvlist_t	*xoptlist;
extern nvlist_t	*deleteopt;

/* The following are used for the extended headers */
extern struct xtar_hdr	oXtarhdr;	/* merged ext header override data */
extern struct xtar_hdr	Xtarhdr;	/* extended header data */
extern struct xtar_hdr	coXtarhdr;	/* cmd line global override data */
extern size_t		xrec_size;	/* initial size */
extern char		*xrec_ptr;
extern off_t		xrec_offset;
extern int		charset_type;
extern u_longlong_t	xhdr_flgs;	/* Bits set determine which items */
					/* need to be in extended header. */
extern u_longlong_t	oxxhdr_flgs;	/* Bits set determine which items */
					/* need to be in extended header. */
extern u_longlong_t	ogxhdr_flgs;	/* Bits set determine which items */
					/* need to be in extended header. */
extern u_longlong_t	ohdr_flgs;	/* Bits set determine which items */
					/* need to be in extended header. */
extern u_longlong_t	gxhdr_flgs;	/* Bits set determine which items */
					/* need to be in extended header. */
extern u_longlong_t	exthdr_flgs;	/* Bits set when values are not to */
					/* be ignored. */
extern uint_t		thisgseqnum;	/* global sequence number */
extern long		saverecsum;	/* chksum */
extern char		savetypeflag;	/* typeflag */
extern char		*savemagic;	/* magic */
extern char		saveversion[TL_VERSION + 1];	/* version number */
extern char		*saveprefix;	/* prefix */
extern char		*savename;	/* name */
extern char		*savenamesize;	/* c_namesize */
extern char		*shdrbuf;

#define	PAX_OK			0
#define	PAX_FAIL		1
#define	PAX_RETRY		2
#define	PAX_NOT_FOUND		3

#define	_X_DEVMAJOR	0x1
#define	_X_DEVMINOR	0x2
#define	_X_GID		0x4
#define	_X_GNAME	0x8
#define	_X_LINKPATH	0x10
#define	_X_PATH		0x20
#define	_X_SIZE		0x40
#define	_X_UID		0x80
#define	_X_UNAME	0x100
#define	_X_ATIME	0x200
#define	_X_CTIME	0x400
#define	_X_MTIME	0x800
#define	_X_CHARSET	0x1000
#define	_X_COMMENT	0x2000
#define	_X_REALTIME	0x4000
#define	_X_SECURITY	0x8000
#define	_X_HOLESDATA	0x10000
#define	_X_LAST		0x40000000

#define	_L_X_DEVMAJOR	1
#define	_L_X_DEVMINOR	2
#define	_L_GID		3
#define	_L_GNAME	4
#define	_L_X_LINKPATH	5
#define	_L_X_PATH	6
#define	_L_SIZE		7
#define	_L_UID		8
#define	_L_UNAME	9
#define	_L_X_ATIME	10
#define	_L_X_CTIME	11
#define	_L_MTIME	12
#define	_L_X_CHARSET	13
#define	_L_X_COMMENT	14
#define	_L_X_REALTIME	15
#define	_L_X_SECURITY	16
#define	_L_U_NAME	17
#define	_L_U_MODE	18
#define	_L_U_CHKSUM	19
#define	_L_U_TYPEFLAG	20
#define	_L_U_LINKNAME	21
#define	_L_U_MAGIC	22
#define	_L_U_VERSION	23
#define	_L_U_DEVMAJOR	24
#define	_L_U_DEVMINOR	25
#define	_L_U_PREFIX	26
#define	_L_C_MAGIC	27
#define	_L_C_DEV	28
#define	_L_C_INO	29
#define	_L_C_MODE	30
#define	_L_C_UID	31
#define	_L_C_GID	32
#define	_L_C_NLINK	33
#define	_L_C_RDEV	34
#define	_L_C_MTIME	35
#define	_L_C_NAMESIZE	36
#define	_L_C_FILESIZE	37
#define	_L_C_NAME	38
#define	_L_C_FILEDATA	39
#define	_L_X_HOLESDATA	40
#define	_L_LAST		256


extern u_longlong_t	xhdr_count;
extern char		xhdr_dirname[];
extern char		pidchars[];
extern char		gseqnum[];
extern char		*local_path;
extern char		*xlocal_path;
extern char		*glocal_path;
extern char		*local_linkpath;
extern char		*xlocal_linkpath;
extern char		*glocal_linkpath;
extern char		*local_gname;
extern char		*local_uname;
extern char		*local_charset;
extern char		*local_comment;
extern char		*local_realtime;
extern char		*local_security;
extern char		*local_holesdata;

#endif /* NO_EXTERN */

#define	CHARSET_UNKNOWN	0
#define	CHARSET_7_BIT	1	/* ISO/IEC 646 */
#define	CHARSET_8_BIT	2	/* ISO/IED 8859 */
#define	CHARSET_UTF_8	3	/* ISO/IEC 10646, UTF-8 encoding */
#define	CHARSET_ERROR	-1

#define	INV_BYPASS	0	/* Invalid - bypass */
#define	INV_RENAME	1	/* Invalid - rename */
#define	INV_UTF8	2	/* Invalid - utf8 */
#define	INV_WRITE	3	/* Invalid - write */

#define	PID_MAX_DIGITS		(10 * sizeof (pid_t) / 4)
#define	TIME_MAX_DIGITS		(10 * sizeof (time_t) / 4)
#define	INT_MAX_DIGITS		(10 * sizeof (int) / 4)
#define	LONG_MAX_DIGITS		(10 * sizeof (long) / 4)
#define	ULONGLONG_MAX_DIGITS	(10 * sizeof (u_longlong_t) / 4)

/*
 * UTF_8 encoding requires more space than the current codeset equivalent.
 * Currently a factor of 2-3 would suffice, but it is possible for a factor
 * of 6 to be needed in the future, so for saftey, we use that here.
 */
#define	UTF_8_FACTOR	6

/*
 * extended attribute fixed header constants
 */
#define	XATTR_HDR_VERS	7

#include "func.h"

/*
 * These constants come from archives.h and sys/fcntl.h
 * and were introduced by the extended attributes project
 * in Solaris 9.
 */
#if !defined(O_XATTR)
#define	AT_SYMLINK_NOFOLLOW	0x1000
#define	AT_REMOVEDIR		0x0001
#define	_XATTR_CPIO_MODE	0xB000
#define	_XATTR_HDRTYPE		'E'
#endif

#ifdef	__cplusplus
}
#endif

#endif /* _PAX_H */
