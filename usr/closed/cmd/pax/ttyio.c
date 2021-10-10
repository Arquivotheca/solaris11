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
 * ttyio.c - Terminal/Console I/O functions for all archive interfaces
 *
 * Author: Mark H. Colburn, NAPS International
 * Sponsored by The USENIX Association for public distribution.
 *
 * DESCRIPTION
 *
 *	These routines provide a consistent, general purpose interface to
 *	the user via the user's terminal, if it is available to the
 *	process.
 */

/* Headers */

#include <signal.h>
#include "pax.h"

/*
 * open_tty - open the terminal for interactive queries
 *
 * DESCRIPTION
 *
 * 	Assumes that background processes ignore interrupts and that the
 *	open() or the isatty() will fail for processes which are not
 *	attached to terminals. Returns a file descriptor or -1 if
 *	unsuccessful.
 *
 * RETURNS
 *
 *	Returns a file descriptor which can be used to read and write
 *	directly to the user's terminal, or -1 on failure.
 *
 * ERRORS
 *
 *	If SIGINT cannot be ignored, or the open fails, or the newly opened
 *	terminal device is not a tty, then open_tty will return a -1 to the
 *	caller.
 */


int
open_tty(void)
{
	int	fd;		/* file descriptor for terminal */
	SIG_T	(*intr)();	/* used to restore interrupts if signal fails */

	if ((intr = signal(SIGINT, SIG_IGN)) == SIG_IGN)
		return (-1);
	(void) signal(SIGINT, intr);
	if ((fd = open(TTY, O_RDWR)) < 0)
		return (-1);
	if (isatty(fd))
		return (fd);
	(void) close(fd);
	return (-1);
}


/*
 * nextask - ask a question and get a response
 *
 * DESCRIPTION
 *
 *	Give the user a prompt and wait for their response.  The prompt,
 *	located in "msg" is printed, then the user is allowed to type
 *	a response to the message.  The first "limit" characters of the
 *	user response is stored in "answer".
 *
 *	Nextask ignores spaces and tabs.
 *
 * PARAMETERS
 *
 *	char *msg	- Message to display for user
 *	char *answer	- Pointer to user's response to question
 *	size_t *limit	- Limit of length for user's response
 *
 * RETURNS
 *
 *	Returns the number of characters in the user response to the
 *	calling function.  If an EOF was encountered, a -1 is returned to
 *	the calling function.  If an error occured which causes the read
 *	to return with a value of -1, then the function will return a
 *	non-zero return status to the calling process, and abort
 *	execution.
 */


int
nextask(char *msg, char **answer, size_t *limit)
{
	int	idx;		/* index into answer for character input */
	int	got;		/* number of characters read */
	char	c;		/* character read */

	if (ttyf < 0)
		fatal(gettext("/dev/tty unavailable"));
	(void) write(ttyf, msg, (uint_t)strlen(msg));
	idx = 0;
	while ((got = read(ttyf, &c, 1)) == 1) {
		if (c == '\n') {
			break;
		} else if (c == ' ' || c == '\t') {
			continue;
		} else if (idx < *limit - 1) {
			(*answer)[idx++] = c;
		} else {
			/*
			 * Extend the limit on the number of chars read
			 * Note: Prior to SUSv3 changes, pathnames
			 * were defined as static arrays of size
			 * PATH_MAX + 1.  When making changes to align
			 * with SUSv3 TC2 (having to do with -o invalid=write,
			 * which is only applicable when -x pax is
			 * specified (f_stdpax is set), and the ability
			 * to have pathnames which possibly are longer than
			 * PATH_MAX characters) the goal was to maintain
			 * existing behavior (limit the pathname to PATH_MAX
			 * characters), and add the ability to dynamically
			 * extend this limit when -x pax is specified.  It
			 * should also be noted that the behavior, when
			 * -x pax is not specified, has always been to
			 * truncate the pathname at PATH_MAX characters.
			 */
			if (((!f_stdpax) && (*limit < PATH_MAX + 1)) ||
			    (f_stdpax)) {
				if (f_stdpax) {
					*limit = *limit * 2;
				} else {
					/*
					 * The size limit for the answer
					 * was 20 when nextask() was called
					 * from next(), 10 when called from
					 * get_disposition() and PATH_MAX + 1
					 * when called from elsewhere.  Since
					 * nextask() and get_disposition()
					 * really don't rely on an answer of
					 * size 20 or 10, respectively, it
					 * does no harm to go ahead and set
					 * the size of the answer to
					 * PATH_MAX + 1.
					 */
					*limit = PATH_MAX + 1;
				}
				if ((*answer = realloc(*answer,
				    *limit * sizeof (char))) == NULL) {
					fatal(gettext("out of memory"));
				}
				(*answer)[idx++] = c;
			}
		}
	}
	if (got == 0)			/* got an EOF */
		return (-1);
	if (got < 0)
		fatal(strerror(errno));
	(*answer)[idx] = '\0';
	return (0);
}


/*
 * lineget - get a line from a given stream
 *
 * DESCRIPTION
 *
 *	Get a line of input for the stream named by "stream".  The data on
 *	the stream is put into the buffer "buf".
 *
 * PARAMETERS
 *
 *	FILE *stream		- Stream to get input from
 *	char *buf		- Buffer to put input into
 *
 * RETURNS
 *
 * 	Returns 0 if successful, -1 at EOF.
 */


int
lineget(FILE *stream, char *buf)
{
	int	c;

	for (;;) {
		if ((c = getc(stream)) == EOF)
			return (-1);
		if (c == '\n')
			break;
		*buf++ = c;
	}
	*buf = '\0';
	return (0);
}


/*
 * next - Advance to the next archive volume.
 *
 * DESCRIPTION
 *
 *	Prompts the user to replace the backup medium with a new volume
 *	when the old one is full.  There are some cases, such as when
 *	archiving to a file on a hard disk, that the message can be a
 *	little surprising.  Assumes that background processes ignore
 *	interrupts and that the open() or the isatty() will fail for
 *	processes which are not attached to terminals. Returns a file
 *	descriptor or -1 if unsuccessful.
 *
 * PARAMETERS
 *
 *	int mode	- mode of archive (READ, WRITE, PASS)
 */


void
next(int mode)
{
	char	msg[200];	/* buffer for message display */
	char	*answer;	/* buffer for user's answer */
	size_t	ansz = 20;	/* buffer for user's answer */
	int	ret;
	char	*go;		/* pointer to go string */
	char	*quit;		/* pointer to quit string */

	close_archive();

	/*
	 * TRANSLATION_NOTE
	 *	The following two strings should be translated only
	 *	in those locales where the translation will make it
	 *	easier for the user.  They are the characters that
	 *	must be typed by the user to either continue or
	 *	quit processing.
	 */
	go = gettext("go");
	quit = gettext("quit");

	(void) snprintf(msg, sizeof (msg), gettext(
	    "%s: Ready for volume %u\n%s: Type \"%s\" when "
	    "ready to proceed (or \"%s\" to abort): \07"), myname,
	    arvolume + 1, myname, go, quit);
	if ((answer = calloc(ansz, sizeof (char))) == NULL) {
		fatal(gettext("out of memory"));
	}
	for (;;) {
		ret = nextask(msg, &answer, &ansz);
		if (ret == -1 || strcmp(answer, quit) == 0)
			fatal(gettext("Aborted"));
		if (strcmp(answer, go) == 0 && open_archive(mode) == 0)
			break;
	}
	free(answer);
	warnarch(gettext("Continuing"), (OFFSET) 0);
}
