/*
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

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
 * OSF/1 1.2
 */
#if !defined(lint) && !defined(_NOIDENT)
	static char rcsid[] = "@(#)$RCSfile: warn.c,v $ $Revision: 1.2.2.2 "
	    "$ (OSF) $Date: 1991/10/01 15:55:56 $";
#endif
/*
 * warn.c - miscellaneous user warning routines
 *
 * DESCRIPTION
 *
 *	These routines provide the user with various forms of warning
 *	and informational messages.
 *
 * AUTHOR
 *
 *     Mark H. Colburn, NAPS International (mark@jhereg.mn.org)
 *
 * Sponsored by The USENIX Association for public distribution.
 *
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
 *
 * Revision 1.2  89/02/12  10:06:15  mark
 * 1.2 release fixes
 *
 * Revision 1.1  88/12/23  18:02:40  mark
 * Initial revision
 *
 */

/* Headers */

#include "pax.h"


/*
 * warnarch - print an archive-related warning message and offset
 *
 * DESCRIPTION
 *
 *	Present the user with an error message and an archive offset at
 *	which the error occured.   This can be useful for diagnosing or
 *	fixing damaged archives.
 *
 * PARAMETERS
 *
 *	char 	*msg	- A message string to be printed for the user.
 *	OFFSET 	adjust	- An adjustment which is added to the current
 *			  archive position to tell the user exactly where
 *			  the error occurred.
 */

void
warnarch(char *msg, OFFSET adjust)
{
	OFFSET	n;
	char	num[ULONGLONG_MAX_DIGITS + 5];
	int	len = 0;

	adjust = total - adjust;

	if ((n = (adjust / (1024L * 1024L))) != 0) {
		(void) sprintf(num, "%lldm+", n);
		len = strlen(num);
		adjust -= n * 1024L * 1024L;
	}
	if ((n = (adjust / 1024L)) != 0) {
		(void) sprintf(num + len, "%lldk+", n);
		len = strlen(num);
		adjust -= n * 1024L;
	}
	(void) sprintf(num + len, "%lld", adjust);
	(void) fprintf(stderr, gettext("%s: [offset %s]: %s\n"),
	    myname, num, msg);
}


/*
 * fatal - print fatal message and exit
 *
 * DESCRIPTION
 *
 *	Fatal prints the program's name along with an error message, then
 *	exits the program with a non-zero return code.
 *
 * PARAMETERS
 *
 *	char 	*why	- description of reason for termination
 *
 * RETURNS
 *
 *	Returns an exit code of 1 to the parent process.
 */

void
fatal(char *why)
{
	(void) fprintf(stderr, "%s: %s\n", myname, why);
	exit(1);
}


/*
 * warn - print a warning message
 *
 * DESCRIPTION
 *
 *	Print an error message listing the program name, the actual error
 *	which occurred and an informational message as to why the error
 *	occurred on the standard error device.  The standard error is
 *	flushed after the error is printed to assure that the user gets
 *	the message in a timely fasion.
 *
 *	The exit status the program return is set to 1.
 *
 * PARAMETERS
 *
 *	char *what	- Pointer to string describing what failed.
 *	char *why	- Pointer to string describing why did it failed.
 */

void
warn(char *what, char *why)
{
	(void) fprintf(stderr, "%s: %s : %s\n", myname, what, why);
	(void) fflush(stderr);
	exit_status = 1;
}


/*
 * diag - print a warning message
 *
 * DESCRIPTION
 *
 *	Print an error message listing the program name, the actual error
 *	which occurred and an informational message as to why the error
 *	occurred on the standard error device.  The standard error is
 *	flushed after the error is printed to assure that the user gets
 *	the message in a timely fasion.
 *
 *	The exit status the program is not affected.
 *
 * PARAMETERS
 *
 *	char *what	- Pointer to string describing what failed.
 *	char *why	- Pointer to string describing why did it failed.
 */

/*PRINTFLIKE1*/
void
diag(char *format, ...)
{
	va_list		alist;
	static char	MYNAME_FMT[] = "%s: ";

	if (myname != NULL) {
		(void) fprintf(stderr, gettext(MYNAME_FMT), myname);
		va_start(alist, format);
		(void) vfprintf(stderr, format, alist);
		va_end(alist);
	}
	(void) fflush(stderr);
}
