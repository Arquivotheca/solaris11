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

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * Entry point, initialization, miscellaneous routines.
 */

#if defined(sun)
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <libintl.h>
#include <limits.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include "less.h"
#include "more_msg.h"

int exit_status = 0;
#endif

int	ispipe;
int	new_file;
int	is_tty;
char	*current_file, *previous_file, *current_name, *next_name;
wchar_t	*first_cmd = NULL;
wchar_t	*every_first_cmd = NULL;
off_t	prev_pos;
int	any_display;
off_t	scrolln;
int	ac;
char	**av;
int	curr_ac;
int	quitting;
int	mb_cur_max;
#if !defined(sun)
nl_catd	catd;
#endif

static void	cat_file(void);

extern int	file;
extern int	cbufs;
extern int	errmsgs;

extern char	*tagfile;
extern int	tagoption;
extern int	top_scroll;

/*
 * Edit a new file.
 * Filename "-" means standard input.
 * No filename means the "current" file, from the command line.
 */
int
edit(register char *filename, int set_exit)
{
	register int f;
	register char *m;
	off_t initial_pos;
	static int didpipe;
	char message[PATH_MAX*2], *p;

	initial_pos = NULL_POSITION;
	if (filename == NULL || *filename == '\0') {
		if (curr_ac >= ac) {
#if defined(sun)
			error(gettext("No current file"));
			if (set_exit)
				exit_status = 2;
#else
			error(MSGSTR(NOCURR, "No current file"));
#endif
			return(0);
		}
		filename = save(av[curr_ac]);
	}
	else if (strcmp(filename, "#") == 0) {
		if (*previous_file == '\0') {
#if defined(sun)
			error(gettext("No previous file"));
			if (set_exit)
				exit_status = 2;
#else
			error(MSGSTR(NOPREV, "No previous file"));
#endif
			return(0);
		}
		filename = save(previous_file);
		initial_pos = prev_pos;
	} else
		filename = save(filename);

	/* use standard input. */
	if (!strcmp(filename, "-")) {
		if (didpipe) {
#if defined(sun)
			error(gettext("Can view standard input only once"));
			if (set_exit)
				exit_status = 2;
#else
			error(MSGSTR(NOVIEW, "Can view standard input only once"));
#endif
			return(0);
		}
		f = 0;
	}
	else if ((m = bad_file(filename, message, sizeof(message))) != NULL) {
		error(m);
		if (set_exit)
			exit_status = 2;
		free(filename);
		return(0);
	}
	else if ((f = open(filename, O_RDONLY, 0)) < 0) {
		(void)sprintf(message, "%s: %s", filename, strerror(errno));
		error(message);
		if (set_exit)
			exit_status = 2;
		free(filename);
		return(0);
	}

	if (isatty(f)) {
		/*
		 * Not really necessary to call this an error,
		 * but if the control terminal (for commands)
		 * and the input file (for data) are the same,
		 * we get weird results at best.
		 */
#if defined(sun)
		error(gettext("Can't take input from a terminal"));
		if (set_exit)
			exit_status = 2;
#else
		error(MSGSTR(NOTERM, "Can't take input from a terminal"));
#endif
		if (f > 0)
			(void)close(f);
		(void)free(filename);
		return(0);
	}

	/*
	 * We are now committed to using the new file.
	 * Close the current input file and set up to use the new one.
	 */
	if (file > 0)
		(void)close(file);
	new_file = 1;
	if (previous_file != NULL)
		free(previous_file);
	previous_file = current_file;
	current_file = filename;
	pos_clear();
	prev_pos = position(TOP);
	ispipe = (f == 0);
	if (ispipe) {
		didpipe = 1;
#if defined(sun)
		current_name = gettext("stdin");
#else
		current_name = MSGSTR(STDIN, "stdin");
#endif
	} else
#if defined(sun)
		current_name = (p = strrchr(filename, '/')) ? p + 1 : filename;
#else
		current_name = (p = rindex(filename, '/')) ? p + 1 : filename;
#endif
	if (curr_ac >= ac)
		next_name = NULL;
	else
		next_name = av[curr_ac + 1];
	file = f;
	ch_init(cbufs, 0);
	init_mark();

	if (every_first_cmd != NULL)
		first_cmd = every_first_cmd;

	if (is_tty) {
		int no_display = !any_display;
		any_display = 1;
		if (no_display && errmsgs > 0) {
			/*
			 * We displayed some messages on error output
			 * (file descriptor 2; see error() function).
			 * Before erasing the screen contents,
			 * display the file name and wait for a keystroke.
			 */
			error(filename);
			if (set_exit)
				exit_status = 2;
		}
		/*
		 * Indicate there is nothing displayed yet.
		 */
		if (initial_pos != NULL_POSITION)
			jump_loc(initial_pos);
		clr_linenum();
	}
	return(1);
}

#if defined(sun)
/*
 * Edit the next file in the command line list.
 */
void
next_file(int n)
{
	extern int quit_at_eof;
	int new_ac;

	new_ac = curr_ac + n;

	while (new_ac < ac) {
		curr_ac = new_ac;
		if (edit(av[curr_ac], 1) == 0)
			new_ac++;
		else
			return;
	}

	if (new_ac >= ac) {
		if (!quit_at_eof || position(TOP) == NULL_POSITION)
			quit();
		error(gettext("No (N-th) next file"));
		exit_status = 2;
	}
}

/*
 * Edit the previous file in the command line list.
 */
void
prev_file(int n)
{
	int new_ac;

	new_ac = curr_ac - n;

	while (new_ac >= 0) {
		curr_ac = new_ac;
		if (edit(av[curr_ac], 1) == 0)
			new_ac--;
		else
			return;
	}

	if (new_ac < 0) {
		error(gettext("No (N-th) previous file"));
		exit_status = 2;
	}
}
#else /* !defined(sun) */
/*
 * Edit the next file in the command line list.
 */
void
next_file(int n)
{
	extern int quit_at_eof;
	off_t position();

	if (curr_ac + n >= ac) {
		if (!quit_at_eof || position(TOP) == NULL_POSITION)
			quit();
		error(MSGSTR(NONTHF, "No (N-th) next file"));
	}
	else
		(void)edit(av[curr_ac += n]);
}

/*
 * Edit the previous file in the command line list.
 */
void
prev_file(int n)
{
	if (curr_ac - n < 0)
		error(MSGSTR(NONTHF2, "No (N-th) previous file"));
	else
		(void)edit(av[curr_ac -= n]);
}
#endif /* !defined(sun) */

/*
 * copy a file directly to standard output; used if stdout is not a tty.
 * the only processing is to squeeze multiple blank input lines.
 */
static void
cat_file(void)
{
	extern int squeeze;
	register int c, empty;

	if (squeeze) {
		empty = 0;
		while ((c = ch_forw_get()) != EOI)
			if (c != '\n') {
				putchr(c);
				empty = 0;
			}
			else if (empty < 2) {
				putchr(c);
				++empty;
			}
	}
	else while ((c = ch_forw_get()) != EOI)
		putchr(c);
	flush();
}

int
main(int argc, char **argv)
{
	int envargc, argcnt;
#define	MAXARGS		50			/* should be enough */
	char *envargv[MAXARGS+1];
	char *p;
#if defined(sun)
	char *te;				/* temp MORE env ptr */
#endif

#if defined(sun)
	(void) setlocale(LC_ALL, "");
 
#if !defined(TEXT_DOMAIN)		/* Should be defined by cc -D */
#define	TEXT_DOMAIN	"SYS_TEST"	/* Use this only if it weren't. */
#endif
	(void) textdomain(TEXT_DOMAIN);
#else
	setlocale( LC_ALL, "");
	catd = catopen(MF_MORE,0);
#endif
	mb_cur_max = MB_CUR_MAX;
	
	if ((p = strrchr(argv[0], '/')) == NULL)
		p = argv[0];
	else
		p++;
	if (strcmp(p, "page") == 0)
		top_scroll = 1;

	/*
	 * Process command line arguments and MORE environment arguments.
	 * Command line arguments override environment arguments.
	 */
#if defined(sun)
	if (((te = getenv("MORE")) != NULL) &&
	    ((p = strdup(te)) != NULL)) {
#else
	if (p = strdup(getenv("MORE"))) {
#endif
		int i = 2;

		envargv[0] = "more";
		if ((envargv[1] = strtok(p, "\t ")) != NULL) {
			while ((p = strtok(NULL, "\t ")) != NULL &&
			    i < MAXARGS)
				envargv[i++] = p;
			envargv[i] = NULL;
			envargc = i;
			(void)option(envargc, envargv);
		}
	}
	argcnt = option(argc, argv);
	argv += argcnt;
	argc -= argcnt;

	/*
	 * Set up list of files to be examined.
	 */
	ac = argc;
	av = argv;
	curr_ac = 0;

	/*
	 * Set up terminal, etc.
	 */
	is_tty = isatty(1);
	if (!is_tty) {
		/*
		 * Output is not a tty.
		 * Just copy the input file(s) to output.
		 */
		if (ac < 1) {
			(void)edit("-", 1);
			cat_file();
		} else {
			do {
				(void)edit((char *)NULL, 1);
				if (file >= 0)
					cat_file();
			} while (++curr_ac < ac);
		}
#if defined(sun)
		exit(exit_status);
#else
		exit(0);
#endif
	}

	raw_mode(1);
	get_term();
#if defined(sun)
	if (!open_getchr()) {
		error(gettext("cannot read from stderr"));
		exit_status = 2;
		quit();
	}
#else
	open_getchr();
#endif
	init();
	init_signals(1);

	/* select the first file to examine. */
	if (tagoption) {
		/*
		 * A -t option was given; edit the file selected by the
		 * "tags" search, and search for the proper line in the file.
		 */
		if (!tagfile || !edit(tagfile, 1) || tagsearch())
			quit();
	}
	else if (ac < 1)
		(void)edit("-", 1);	/* Standard input */
	else {
		/*
		 * Try all the files named as command arguments.
		 * We are simply looking for one which can be
		 * opened without error.
		 */
		do {
			(void)edit((char *)NULL, 1);
		} while (file < 0 && ++curr_ac < ac);
	}

	if (file >= 0)
		commands();
	quit();
	/*NOTREACHED*/
	return (0);
}

/*
 * Copy a string to a "safe" place
 * (that is, to a buffer allocated by malloc).
 */
char *
save(char *s)
{
	char *p;

	p = malloc(strlen(s)+1);
	if (p == NULL)
	{
#if defined(sun)
		error(gettext("cannot allocate memory"));
		exit_status = 2;
#else
		error(MSGSTR(NOMEM, "cannot allocate memory"));
#endif
		quit();
	}
	return(strcpy(p, s));
}

/*
 * Exit the program.
 */
void
quit(void)
{
	extern char *help_file;
	/*
	 * Put cursor at bottom left corner, clear the line,
	 * reset the terminal modes, and exit.
	 */
	quitting = 1;
	if (help_file != NULL)
		remove(help_file);
#if defined(sun)
	putchr('\r');
#else
	lower_left();
#endif
	clear_eol();
	deinit();
	flush();
	raw_mode(0);
#if defined(sun)
	exit(exit_status);
#else
	exit(0);
#endif
}
