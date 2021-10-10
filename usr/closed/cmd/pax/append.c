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
 * append.c - append to a tape archive.
 *
 * DESCRIPTION
 *
 *	Routines to allow appending of archives
 *
 * AUTHORS
 *
 *	Mark H. Colburn, NAPS International (mark@jhereg.mn.org)
 *
 * Sponsored by The USENIX Association for public distribution.
 */


/* Headers */

#include "pax.h"
#include <sys/mtio.h>


/* Function Prototypes */

static void backup(void);

/*
 * append_archive - main loop for appending to a tar archive
 *
 * DESCRIPTION
 *
 *	Append_archive reads an archive until the end of the archive is
 *	reached once the archive is reached, the buffers are reset and the
 *	create_archive function is called to handle the actual writing of
 *	the appended archive data.  This is quite similar to the
 *	read_archive function, however, it does not do all the processing.
 */


void
append_archive(void)

{
	Stat	sb;
	char	*name;
	size_t	namesz = PATH_MAX + 1;

	(void) memset(&sb, 0, sizeof (Stat));
	if ((name = calloc(namesz, sizeof (char))) == NULL) {
		fatal(gettext("out of memory"));
	}

	firstxhdr = 1;
	xcont = 1;
	while (get_header(&name, &namesz, &sb) == 0) {
		if (!f_unconditional)
			hash_name(name, &sb);

		if (((ar_format == TAR || ar_format == PAX)
		    ? buf_skip(ROUNDUP((OFFSET) sb.sb_size, BLOCKSIZE))
		    : buf_skip((OFFSET) sb.sb_size)) < 0) {
			warn(name, gettext("File data is corrupt"));
		}
		xcont = 1;
	}
	/* we have now reached the end of the archive... */

	backup();	/* adjusts the file descriptor and buffer pointers */

	create_archive();

#if defined(O_XATTR)
	if (sb.xattr_info.xattrhead != (struct xattr_hdr *)NULL) {
		if (sb.xattr_info.xattrfname != NULL) {
			free(sb.xattr_info.xattrfname);
		}
		if (sb.xattr_info.xattraname != NULL) {
			free(sb.xattr_info.xattraname);
		}
		if (sb.xattr_info.xattr_linkfname != NULL) {
			free(sb.xattr_info.xattr_linkfname);
		}
		if (sb.xattr_info.xattr_linkaname != NULL) {
			free(sb.xattr_info.xattr_linkaname);
		}

		free(sb.xattr_info.xattrhead);
		sb.xattr_info.xattrhead = NULL;
		sb.xattr_info.xattrp = NULL;
	}
#endif

}


/*
 * backup - back the tape up to the end of data in the archive.
 *
 * DESCRIPTION
 *
 *	The last header we have read is either the cpio TRAILER!!! entry
 *	or the two blocks (512 bytes each) of zero's for tar archives.
 * 	adjust the file pointer and the buffer pointers to point to
 * 	the beginning of the trailer headers.
 */


static void
backup(void)

{
	static int mtdev = 1;
	static struct mtop mtop = {MTBSR, 1};	/* Backspace record */
	struct mtget mtget;

	if (mtdev == 1)
		mtdev = ioctl(archivefd, MTIOCGET, (char *)&mtget);
	if (mtdev == 0) {
		if (ioctl(archivefd, MTIOCTOP, (char *)&mtop) < 0) {
			fatal(gettext("The append option is not valid for "
			    "specified device."));
		}
	} else {
		if (lseek(archivefd, -(off_t)(bufend-bufstart), SEEK_CUR) ==
		    -1) {
			warn("lseek", strerror(errno));
			fatal(gettext("backspace error"));
		}
	}

	bufidx = lastheader;	/* point to beginning of trailer */
	/*
	 * if lastheader points to the very end of the buffer
	 * Then the trailer really started at the beginning of this buffer
	 */
	if (bufidx == bufstart + blocksize)
		bufidx = bufstart;
}
