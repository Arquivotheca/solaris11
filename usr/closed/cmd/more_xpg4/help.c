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
static char rcsid[] = "@(#)$RCSfile: help.c,v $ $Revision: 1.1.2.2 $ (OSF) $Date: 1992/08/24 18:16:51 $";
#endif

/*
 * Copyright (c) 1988 Mark Nudleman
 * Copyright (c) 1988 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Copyright (c) 1995, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef lint
static char sccsid[] = "@(#)help.c	5.7 (Berkeley) 6/1/90";
#endif /* not lint */

#if defined(sun)
#include <stdio.h>
#include <fcntl.h>
#include <libintl.h>
#include <locale.h>
#include <string.h>
#include <unistd.h>
#include <sys/param.h>
#include "less.h"
#include "more_msg.h"
#endif


static char *help_msg[] = {

"  Commands flagged with an asterisk (``*'') may be preceded by a number.\n",
"  Commands of the form ``^X'' are control characters, i.e. control-X.\n",
"\n",
"  h              Display this help.\n",
"\n",
"  f, ^F, SPACE * Forward  N lines, default one screen.\n",
"  b, ^B        * Backward N lines, default one screen.\n",
"  j, CR        * Forward  N lines, default 1 line.\n",
"  k            * Backward N lines, default 1 line.\n",
"  d, ^D        * Forward  N lines, default half screen or last N to d/u.\n",
"  u, ^U        * Backward N lines, default half screen or last N to d/u.\n",
"  g            * Go to line N, default 1.\n",
"  G            * Go to line N, default the end of the file.\n",
#if !defined(sun)
"  p, %         * Position to N percent into the file.\n",
#endif
"\n",
"  r, ^L          Repaint screen.\n",
"  R              Repaint screen, discarding buffered input.\n",
"\n",
"  m[a-z]         Mark the current position with the supplied letter.\n",
"  '[a-z]         Return to the position previously marked by this letter.\n",
"  ''             Return to previous position.\n",
"\n",
"  /pattern     * Search forward  for N-th line containing the pattern.\n",
"  /!pattern    * Search forward  for N-th line NOT containing the pattern.\n",
"  ?pattern     * Search backward for N-th line containing the pattern.\n",
"  ?!pattern    * Search backward for N-th line NOT containing the pattern.\n",
"  n            * Repeat previous search (for N-th occurence).\n",
#if defined(sun)
"  N            * Reverse previous search (for N-th occurence).\n",
#endif
"\n",
#if defined(sun)
"  :e [file]       Examine a new file.\n",
"  :n           *  Examine the next file.\n",
"  :p           *  Examine the previous file.\n",
#else
"  :a              Display the list of files.\n",
"  E [file]        Examine a new file.\n",
"  :n, N        *  Examine the next file.\n",
"  :p, P        *  Examine the previous file.\n",
#endif
"  :t [tag]        Examine the tag.\n",
"  v               Run an editor on the current file.\n",
"\n",
"  =, ^G           Print current file name and stats.\n",
"\n",
"  q, :q, or ZZ    Exit.\n",
NULL };

extern void	lsystem_reset();

/*
 * Print out the help screen.
 *
 * What this used to do was invoke more on the help file,
 * but thanks to i18n, we have to get this out of a message catalog.
 * This is slow, but effective (hey, its the help case!).
 */

char *help_file = NULL;	/* name of file with help strings */

void
help(void)
{
	char 	cmd[MAXPATHLEN + 20];
	int 	i=0;
	int	fd;
	char 	*msg;

#ifdef _PATH_HELPFILE
	/*
	 * optimise the non-messsage catalog case
	 */
#if defined(sun)
	msg = gettext("default");
#else
	msg = MSGSTR(HELP_01, "default");
#endif
	if (strcmp(msg, "default") == 0) {
		(void)sprintf(cmd, "-more %s", _PATH_HELPFILE);
		lsystem(cmd);
		lsystem_reset();
		return;
	}
#endif

	/*
	 * Strategy:
	 *	Get message from the catalog and write in to a temp file.
	 * 	Run more on this temp file.
	 */
	if (help_file == NULL) {
		if ((help_file = tmpnam(NULL)) == NULL ||
		    (fd = open(help_file, (O_WRONLY|O_CREAT), 0666)) == -1) {
			error("Can't create temp file for help");
			return;
		}
		while (help_msg[i] != NULL) {
#if defined(sun)
			msg = gettext(help_msg[i]);
#else
			msg = MSGSTR(HELP_01 + i, help_msg[i]);
#endif
			write(fd, msg, strlen(msg));
			i++;
		}
		close(fd);
	}

	(void)sprintf(cmd, "-more %s", help_file);
	lsystem(cmd);
#if defined(sun)
	error(gettext("End of help"));
	lsystem_reset();
#else
	error(MSGSTR(HELPEND, "End of help"));
#endif
}
