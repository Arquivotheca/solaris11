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
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * OSF/1 1.2
 */

#if !defined(lint) && !defined(_NOIDENT)
static char rcsid[] = "@(#)$RCSfile: command.c,v $ $Revision: 1.7.3.2 $ (OSF) $Date: 1992/08/24 18:16:01 $";
#endif

#pragma ident	"%Z%%M%	%I%	%E% SMI"

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

#ifndef lint
static char sccsid[] = "@(#)command.c	5.22 (Berkeley) 6/21/92";
#endif /* not lint */

#if defined(sun)
#include <stdio.h>
#include <wchar.h>
#include <ctype.h>
#include <libintl.h>
#include <locale.h>
#include <stdlib.h>
#include <limits.h>
#include <widec.h>
#include <string.h>
#include <termios.h>
#include <sys/param.h>
#include <sys/types.h>
#include "less.h"
#include "more_msg.h"

#define	_PATH_VI	"vi"		/* use user's search path */
#endif

#define	NO_MCA		0
#define	MCA_DONE	1
#define	MCA_MORE	2

#if defined(sun)
extern tcflag_t erase_char, werase_char;
#else
extern int erase_char, kill_char, werase_char;
#endif
extern int ispipe;
extern int sigs;
extern int quit_at_eof;
extern int hit_eof;
extern int sc_width;
extern int sc_height;
extern int sc_window;
extern int curr_ac;
extern int ac;
extern int quitting;
extern off_t scrolln;
extern int screen_trashed;	/* The screen has been overwritten */
extern int verbose;
extern wchar_t *first_cmd;
extern wchar_t *every_first_cmd;
extern char *current_file;

static wchar_t cmdbuf[BUFSIZ];	/* Buffer for holding a multi-char command */
static char *shellcmd = NULL;	/* For holding last shell command for "!!" */
static wchar_t *cp;		/* Pointer into cmdbuf */
static int cmd_col;		/* Current column of the multi-char command */
static int longprompt;		/* if stat command instead of prompt */
static int mca;			/* The multicharacter command (action) */
static int last_mca;		/* The previous mca */
static off_t number;		/* The number typed by the user */
static int  inumber;		/* temp place holder of int */
static int wsearch;		/* Search for matches (1) or non-matches (0) */
static char cmdbuf_c[BUFSIZ * MB_LEN_MAX];

extern int	getwchr();
extern void	lsystem_reset();

static int	cmd_erase(void);
static int	cmd_char(int);
static int	getwcc(void);
static void	exec_mca(void);
static int	mca_char(int c);
static int	taileq(char *, char *);
static int	expand(char **, wchar_t *);
void		conv_wc2mb(char *, wchar_t *);

#define	CMD_RESET	cp = cmdbuf	/* reset command buffer to empty */
#define	CMD_EXEC	lower_left(); flush()

/* backspace in command buffer. */
static int
cmd_erase(void)
{
	int	len;
	/*
	 * backspace past beginning of the string: this usually means
	 * abort the command.
	 */
	if (cp == cmdbuf)
		return(1);

	/* erase an extra character, for the carat. */
	if (CONTROL_CHAR(*--cp)) {
		backspace();
		--cmd_col;
	}

	if ((len = wcwidth(*cp)) <= 0)
		len = 1;
	while(len--) {
		backspace();
		--cmd_col;
	}
	return(0);
}

/* set up the display to start a new multi-character command. */
void
start_mca(int action, char *prompt)
{
	lower_left();
	clear_eol();
	putstr(prompt);
	cmd_col = strlen(prompt);
	mca = action;
}

/*
 * process a single character of a multi-character command, such as
 * a number, or the pattern of a search command.
 */
static int
cmd_char(int c)
{
	int	len;

	if (c == erase_char)
		return(cmd_erase());
	/* in this order, in case werase == erase_char */
	if (c == werase_char) {
		if (cp > cmdbuf) {
			while (iswspace(cp[-1]) && !cmd_erase());
			while (!iswspace(cp[-1]) && !cmd_erase());
			while (iswspace(cp[-1]) && !cmd_erase());
		}
		return(cp == cmdbuf);
	}
#if !defined(sun)
	if (c == kill_char) {
		while (!cmd_erase());
		return(1);
	}
#endif
	/*
	 * No room in the command buffer, or no room on the screen;
	 * {{ Could get fancy here; maybe shift the displayed line
	 * and make room for more chars, like ksh. }}
	 */
	if (cp >= &cmdbuf[BUFSIZ - 1] || cmd_col >= sc_width-3)
#if defined(sun)
		sound_bell();
#else
		bell();
#endif
	else {
		*cp++ = c;
		if (CONTROL_CHAR(c)) {
			putchr('^');
			cmd_col++;
			c = CARAT_CHAR(c);
		}
		putwchr(c);
		if ((len = wcwidth(c)) <= 0)
			len = 1;
		cmd_col += len;
	}
	return(0);
}

int
prompt(void)
{
	extern int linenums, verbose;
	extern char *current_name, *firstsearch, *next_name;
	off_t len, pos;
	char pbuf[40];

	if (first_cmd != NULL && *first_cmd != L'\0')
	{
		/*
		 * No prompt necessary if commands are from first_cmd
		 * rather than from the user.
		 */
#if defined(sun)
		return 1;
#else
		return;
#endif
	}

	/*
	 * if nothing is displayed yet, display starting from line 1;
	 */
	if (position(TOP) == NULL_POSITION) {
		if (forw_line((off_t)0) == NULL_POSITION)
			return(0);
		jump_back(1);
	}
	else if (screen_trashed)
		repaint();

	/* if -e flag and we've hit EOF on the last file, quit. */
	if (quit_at_eof && hit_eof && curr_ac + 1 >= ac)
		quit();

	/* select the proper prompt and display it. */
	lower_left();
	clear_eol();
	if (longprompt) {
		so_enter();
		putstr(current_name);
		putstr(":");
		if (!ispipe) {
#if defined(sun)
			(void)sprintf(pbuf, gettext(" file %d/%d"),
			    curr_ac + 1, ac);
#else
			(void)sprintf(pbuf, MSGSTR(FILEMSG, " file %d/%d"), curr_ac + 1, ac);
#endif
			putstr(pbuf);
		}
		if (linenums) {
#if defined(sun)
			(void)sprintf(pbuf, gettext(" line %lld"),
			    currline(BOTTOM));
#else
			(void)sprintf(pbuf, MSGSTR(LINEMSG, " line %lld"), currline(BOTTOM));
#endif
			putstr(pbuf);
		}
		if ((pos = position(BOTTOM)) != NULL_POSITION) {
#if defined(sun)
			(void)sprintf(pbuf, gettext(" byte %lld"), pos);
#else
			(void)sprintf(pbuf, MSGSTR(BYTEMSG, " byte %lld"), pos);
#endif
			putstr(pbuf);
			if (!ispipe && (len = ch_length())) {
#if defined(sun)
				(void)sprintf(pbuf, gettext("/%lld pct %lld%%"),
				    len, ((100 * pos) / len));
#else
				(void)sprintf(pbuf, MSGSTR(PERCENT, "/%lld pct %lld%%"),
				    len, ((100 * pos) / len));
#endif
				putstr(pbuf);
			}
		}
		so_exit();
		longprompt = 0;
	}
	else {
		so_enter();
		putstr(current_name);
		if (hit_eof)
			if (next_name) {
#if defined(sun)
				putstr(gettext(": END (next file: "));
#else
				putstr(MSGSTR(EOFNEXT, ": END (next file: "));
#endif
				putstr(next_name);
				putstr(")");
			}
			else
#if defined(sun)
				putstr(gettext(": END"));
#else
				putstr(MSGSTR(END, ": END"));
#endif
		else if (!ispipe &&
		    (pos = position(BOTTOM)) != NULL_POSITION &&
		    (len = ch_length())) {
			(void)sprintf(pbuf, " (%lld%%)", ((100 * pos) / len));
			putstr(pbuf);
		}
		if (verbose)
#if defined(sun)
			putstr(gettext(
			    "[Press space to continue, q to quit, "
			    "h for help]"));
#else
			putstr(MSGSTR(PHELP, "[Press space to continue, q to quit, h for help]"));
#endif
		so_exit();
	}
	return(1);
}

/* Get command character.
 * The character normally comes from the keyboard,
 *  but may come from the "first_cmd" string.
 */
static int
getwcc(void)
{
	extern int cmdstack, tagoption;
	int ch;

	/* left over from error() routine. */
	if (cmdstack) {
		ch = cmdstack;
		cmdstack = NULL;
		return(ch);
	}
	if (first_cmd == NULL)
		return (getwchr());

	if (*first_cmd == L'\0')
	{
		/*
		 * Reached end of first_cmd input.
		 */
		first_cmd = NULL;
		if (cp > cmdbuf &&(tagoption || position(TOP) == NULL_POSITION))
		{
			/*
			 * Command is incomplete, so try to complete it.
			 * There are only two cases:
			 * 1. We have "/string" but no newline.  Add the \n.
			 * 2. We have a number but no command.  Treat as #g.
			 * (This is all pretty hokey.)
			 */
			if (mca != A_DIGIT)
				/* Not a number; must be search string */
				return ('\n'); 
			else
				/* A number; append a 'g' */
				return ('g');
		}
		return (getwchr());
	}
	return (*first_cmd++);
}

/* execute a multicharacter command. */
static void
exec_mca(void)
{
	extern char *tagfile;
	register char *p;
	register int n;

	*cp = '\0';
	conv_wc2mb(cmdbuf_c, cmdbuf);
	CMD_EXEC;
	switch (mca) {
	case A_F_SEARCH:
		(void)search(1, cmdbuf_c, number, wsearch);
		break;
	case A_B_SEARCH:
		(void)search(0, cmdbuf_c, number, wsearch);
		break;
	case A_EXAMINE:
		for (p = cmdbuf_c; isspace(*p); ++p);
		(void)edit(mglob(p), 0);
		break;
	case A_TAGFILE:
		for (p = cmdbuf_c; isspace(*p); ++p);
		findtag(p);
		if (tagfile == NULL)
			break;
		if (edit(tagfile, 0))
			(void)tagsearch();
		break;
	case A_SHELL:
		if (expand(&shellcmd, cmdbuf)) {

			if (shellcmd == NULL)
				lsystem("");
			else
				lsystem(shellcmd);
#if defined(sun)
			error(gettext("!done"));
			lsystem_reset();
#else
			error(MSGSTR(SHDONE, "!done"));
#endif
		}
		break;
	}
}

/* add a character to a multi-character command. */
static int
mca_char(int c)
{
	switch (mca) {
	case 0:			/* not in a multicharacter command. */
	case A_PREFIX:		/* in the prefix of a command. */
		return(NO_MCA);
	case A_DIGIT:
		/*
		 * Entering digits of a number.
		 * Terminated by a non-digit.
		 */
		if (!isascii(c) || !isdigit(c) &&
#if defined(sun)
		    c != erase_char && c != werase_char) {
#else
		    c != erase_char && c != kill_char && c != werase_char) {
#endif
			/*
			 * Not part of the number.
			 * Treat as a normal command character.
			 */
			*cp = '\0';
			conv_wc2mb(cmdbuf_c, cmdbuf);
			number = atoll(cmdbuf_c);
			CMD_RESET;
			mca = 0;
			return(NO_MCA);
		}
		break;
	}

	/*
	 * Any other multicharacter command
	 * is terminated by a newline.
	 */
	if (c == '\n' || c == '\r') {
		exec_mca();
		return(MCA_DONE);
	}

	/* append the char to the command buffer. */
	if (cmd_char(c))
		return(MCA_DONE);

	return(MCA_MORE);
}

/*
 * Main command processor.
 * Accept and execute commands until a quit command, then return.
 */
void
commands(void)
{
	register int c;
	register int action;
	off_t skip;
#if defined(sun)
	static int forw_search = 1;
#endif

	last_mca = 0;
#if defined(sun)
	scrolln = (off_t) (sc_window / 2);
#else
	scrolln = (off_t) ((sc_window + 1) / 2);
#endif

	for (;;) {
		mca = 0;
		number = 0;

		/*
		 * See if any signals need processing.
		 */
		if (sigs) {
			psignals();
			if (quitting)
				quit();
		}
		/*
		 * Display prompt and accept a character.
		 */
		CMD_RESET;
		if (!prompt()) {
			if (sigs)
				continue;
			next_file(1);
			continue;
		}
		noprefix();
		c = getwcc();

again:		if (sigs)
			continue;

		/*
		 * If we are in a multicharacter command, call mca_char.
		 * Otherwise we call cmd_decode to determine the
		 * action to be performed.
		 */
		if (mca)
			switch (mca_char(c)) {
			case MCA_MORE:
				/*
				 * Need another character.
				 */
				c = getwcc();
				goto again;
			case MCA_DONE:
				/*
				 * Command has been handled by mca_char.
				 * Start clean with a prompt.
				 */
				continue;
			case NO_MCA:
				/*
				 * Not a multi-char command
				 * (at least, not anymore).
				 */
				break;
			}

		/* decode the command character and decide what to do. */
		switch (action = cmd_decode(c)) {
		case A_DIGIT:		/* first digit of a number */
			start_mca(A_DIGIT, ":");
			goto again;
		case A_F_SCREEN:	/* forward one screen */
			CMD_EXEC;
			if (number <= 0 && (number = (off_t) sc_window) <= 0)
				number = (off_t)(sc_height - 1);
			forward(number, 1);
			break;
		case A_B_SCREEN:	/* backward one screen */
			CMD_EXEC;
			if (number <= 0 && (number = (off_t) sc_window) <= 0)
				number = (off_t) (sc_height - 1);
			backward(number, 1);
			break;
		case A_F_LINE:		/* forward N (default 1) line */
			CMD_EXEC;
			forward(number <= 0 ? 1 : number, 0);
			break;
		case A_B_LINE:		/* backward N (default 1) line */
			CMD_EXEC;
			backward(number <= 0 ? 1 : number, 0);
			break;
		case A_F_SCROLL:	/* forward N lines */
			CMD_EXEC;
			if (number > 0)
				scrolln = number;
			forward(scrolln, 0);
			break;
		case A_SF_SCROLL:	/* forward N lines - set screen size */
			CMD_EXEC;
			if (number > 0)
				sc_window = (int) number;
			if (number <= 0 && (number = (off_t) sc_window) <= 0)
				number =(off_t) sc_height - 1;
			forward(number, 0);
			break;
		case A_SKIP:		/* skip N lines */
			CMD_EXEC;
			if (number > 0)
				skip = number;
			else
				skip = 1;
			forward(skip, 1);
			break;
		case A_B_SCROLL:	/* backward N lines */
			CMD_EXEC;
			if (number > 0)
				scrolln = number;
			backward(scrolln, 0);
			break;
		case A_FREPAINT:	/* flush buffers and repaint */
			if (!ispipe) {
				ch_init(0, 0);
				clr_linenum();
			}
			/* FALLTHROUGH */
		case A_REPAINT:		/* repaint the screen */
			CMD_EXEC;
			home();
#if defined(sun)
			clear_scr();
#else
			clear();
#endif
			repaint();
			break;
		case A_GOLINE:		/* go to line N, default 1 */
			CMD_EXEC;
			if (number <= 0)
				number = 1;
			jump_back(number);
			break;
#if !defined(sun)
		case A_PERCENT:		/* go to percent of file */
			CMD_EXEC;
			if (number < 0)
				number = 0;
			else if (number > 100)
				number = 100;
			jump_percent((int) number);
			break;
#endif
		case A_GOEND:		/* go to line N, default end */
			CMD_EXEC;
			if (number <= 0)
				jump_forw();
			else
				jump_back(number);
			break;
		case A_STAT:		/* print file name, etc. */
			longprompt = 1;
			continue;
		case A_QUIT:		/* exit */
			quit();
		case A_F_SEARCH:	/* search for a pattern */
		case A_B_SEARCH:
			if (number <= 0)
				number = 1;
			start_mca(action, (action==A_F_SEARCH) ? "/" : "?");
			last_mca = mca;
#if defined(sun)
			forw_search = action==A_F_SEARCH;
#endif
			wsearch = 1;
			c = getwcc();
			if (c == '!') {
				/*
				 * Invert the sense of the search; set wsearch
				 * to 0 and get a new character for the start
				 * of the pattern.
				 */
				start_mca(action, 
				    (action == A_F_SEARCH) ? "!/" : "!?");
				wsearch = 0;
				c = getwcc();
			}
			goto again;
		case A_AGAIN_SEARCH:		/* repeat previous search */
			if (number <= 0)
				number = 1;
			if (wsearch)
				start_mca(last_mca, 
				    (last_mca == A_F_SEARCH) ? "/" : "?");
			else
				start_mca(last_mca, 
				    (last_mca == A_F_SEARCH) ? "!/" : "!?");
			CMD_EXEC;
#if defined(sun)
			(void)search(forw_search, (char *)NULL,
			    number, wsearch);
#else
			(void)search(mca != A_F_SEARCH, (char *)NULL,
			    number, wsearch);
#endif
			break;
#if defined(sun)
		case A_BAGAIN_SEARCH:		/* reverse previous search */
			if (number <= 0)
				number = 1;
			if (wsearch)
				start_mca(last_mca,
				    (last_mca == A_F_SEARCH) ? "?" : "/");
			else
				start_mca(last_mca,
				    (last_mca == A_F_SEARCH) ? "!?" : "!/");
			CMD_EXEC;
			forw_search = !forw_search;
			(void)search(forw_search, (char *)NULL,
				number, wsearch);
			break;
#endif
		case A_HELP:			/* help */
			lower_left();
			clear_eol();
#if defined(sun)
			putstr(gettext("help"));
#else
			putstr(MSGSTR(HELP, "help"));
#endif
			CMD_EXEC;
			help();
			break;
		case A_TAGFILE:			/* tag a new file */
			CMD_RESET;
#if defined(sun)
			start_mca(A_TAGFILE, gettext("Tag: "));
#else
			start_mca(A_TAGFILE, MSGSTR(PTAG, "Tag: "));
#endif
			c = getwcc();
			goto again;
#if !defined(sun)
		case A_FILE_LIST:		/* show list of file names */
			CMD_EXEC;
			showlist();
			repaint();
			break;
#endif
		case A_EXAMINE:			/* edit a new file */
			CMD_RESET;
#if defined(sun)
			start_mca(A_EXAMINE, gettext("Examine: "));
#else
			start_mca(A_EXAMINE, MSGSTR(EXAMINE, "Examine: "));
#endif
			c = getwcc();
			goto again;
		case A_VISUAL:			/* invoke the editor */
			if (ispipe) {
#if defined(sun)
				error(gettext("Cannot edit standard input"));
#else
				error(MSGSTR(NOSTDIN, "Cannot edit standard input"));
#endif
				break;
			}
			CMD_EXEC;
			editfile();
			ch_init(0, 0);
			clr_linenum();
			break;
		case A_NEXT_FILE:		/* examine next file */
			if (number <= 0) {
				inumber = 1;
			} else {
				inumber = (int) number;
			}
			next_file(inumber);
			break;
		case A_PREV_FILE:		/* examine previous file */
			if (number <= 0) {
				inumber = 1;
			} else {
				inumber = (int) number; 
			}
			prev_file(inumber);
			break;
		case A_SHELL:
			/*
			 * Shell Escape
			 */
			CMD_RESET;
			start_mca(A_SHELL, "!");
			c = getwcc();
			goto again;
#if !defined(sun)
			break;
#endif
		case A_SETMARK:			/* set a mark */
			lower_left();
			clear_eol();
#if defined(sun)
			start_mca(A_SETMARK, gettext("mark: "));
#else
			start_mca(A_SETMARK, MSGSTR(MARK, "mark: "));
#endif
			c = getwcc();
#if defined(sun)
			if (c == erase_char)
#else
			if (c == erase_char || c == kill_char)
#endif
				break;
			setmark(c);
			break;
		case A_GOMARK:			/* go to mark */
			lower_left();
			clear_eol();
#if defined(sun)
			start_mca(A_GOMARK, gettext("goto mark: "));
#else
			start_mca(A_GOMARK, MSGSTR(GMARK, "goto mark: "));
#endif
			c = getwcc();
#if defined(sun)
			if (c == erase_char)
#else
			if (c == erase_char || c == kill_char)
#endif
				break;
			gomark(c);
			break;
		case A_PREFIX:
			/*
			 * The command is incomplete (more chars are needed).
			 * Display the current char so the user knows what's
			 * going on and get another character.
			 */
			if (mca != A_PREFIX)
				start_mca(A_PREFIX, "");
			if (CONTROL_CHAR(c)) {
				putchr('^');
				c = CARAT_CHAR(c);
			}
			putwchr(c);
			c = getwcc();
			goto again;
		default:
			if (verbose)
#if defined(sun)
				error(gettext("[Press 'h' for instructions.]"));
#else
				error(MSGSTR(PRESSH, "[Press 'h' for instructions.]"));
#endif
			else
#if defined(sun)
				sound_bell();
#else
				bell();
#endif
			break;
		}
	}
}

void
editfile(void)
{
	extern char *current_file;
	static int dolinenumber;
	static char *editor;
	off_t c;
	char buf[MAXPATHLEN * 2 + 20];

	if (editor == NULL) {
		editor = getenv("EDITOR");
		/* pass the line number to vi */
		if (editor == NULL || *editor == '\0') {
			editor = _PATH_VI;
			dolinenumber = 1;
		}
		if (taileq(editor, "vi") || taileq(editor, "ex"))
			dolinenumber = 1;
		else
			dolinenumber = 0;
	}
	if (dolinenumber && (c = currline(TOP)))
		(void)sprintf(buf, "%s +%lld %s", editor, c, current_file);
	else
		(void)sprintf(buf, "%s %s", editor, current_file);
	lsystem(buf);
	lsystem_reset();
}

static int
taileq(char *path, char *file)
{
	char *p;

	p = ((p = strrchr(path, '/')) ? p + 1 : path);
	return(!strcmp(p, file));
}


#if !defined(sun)
void
showlist(void)
{
	extern int sc_width;
	extern char **av;
	register int indx, width;
	int len;
	char *p;

	if (ac <= 0) {
		error(MSGSTR(NOFARGS, "No files provided as arguments."));
		return;
	}
	for (width = indx = 0; indx < ac;) {
		p = strcmp(av[indx], "-") ? av[indx] : MSGSTR(STDIN, "stdin");
		len = strlen(p) + 1;
		if (curr_ac == indx)
			len += 2;
		if (width + len + 1 >= sc_width) {
			if (!width) {
				if (curr_ac == indx)
					putchr('[');
				putstr(p);
				if (curr_ac == indx)
					putchr(']');
				++indx;
			}
			width = 0;
			putchr('\n');
			continue;
		}
		if (width)
			putchr(' ');
		if (curr_ac == indx)
			putchr('[');
		putstr(p);
		if (curr_ac == indx)
			putchr(']');
		width += len;
		++indx;
	}
	putchr('\n');
	error((char *)NULL);
}
#endif /* !defined(sun) */

/* return 1 if inbuf is expanded successfully, otherwise return 0 */
static int
expand (char **outbuf, wchar_t *inbuf)
{
	wchar_t *instr;
	char *outstr;
	wchar_t		ch;
	char temp[BUFSIZ * MB_LEN_MAX];
	extern int mb_cur_max;

	instr = inbuf;
	outstr = temp;
	while ((ch = *instr) != '\0') {
		int len;

		if ((len = wctomb(outstr, ch)) > 1) {
			outstr += len;
			instr++;
			continue;
		}
		
		instr++;

		switch (ch) {
		case '%':
			if (strlcpy(outstr, current_file, sizeof (temp))
			    >= sizeof (temp)) {
				error(gettext("Command too long"));
				return (0);
			}
			outstr += strlen (current_file);
			break;
		case '!':
			if (!shellcmd)
				*outstr++ = (char)ch;
			else {
				if (strlcpy(outstr, shellcmd, sizeof (temp))
				    >= sizeof (temp)) {
					error(gettext("Command too long"));
					return (0);
				}
				outstr += strlen (shellcmd);
			}
			break;
		case '\\':
			if (*instr == '%' || *instr == '!') {
				*outstr++ = (char)*instr++;
				break;
			}
		default:
			*outstr++ = (char)ch;
		}
	}
	*outstr++ = '\0';
	if (*outbuf == NULL)
		free(*outbuf);
	*outbuf = strdup(temp);
	if (*outbuf == NULL) {
		error(gettext("Cannot allocate memory"));
		return (0);
	}
	return (1);
}

void
conv_wc2mb(c_p, w_p)
char	*c_p;
wchar_t	*w_p;
{
	int	len;

	for ( ; *w_p; c_p += len, w_p++) {
		if ((len = wctomb(c_p, *w_p)) <= 0) {
			len = 1;
			*c_p = (unsigned char)*w_p;
		}
	}
	*c_p = 0;
}
