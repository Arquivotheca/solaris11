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
static char rcsid[] = "@(#)$RCSfile: signal.c,v $ $Revision: 1.1.2.2 $ (OSF) $Date: 1992/08/24 18:19:24 $";
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

/*
 * Copyright (c) 1995, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ifndef lint
static char sccsid[] = "@(#)signal.c	5.8 (Berkeley) 3/1/91";
#endif /* not lint */

/*
 * Routines dealing with signals.
 *
 * A signal usually merely causes a bit to be set in the "signals" word.
 * At some convenient time, the mainline code checks to see if any
 * signals need processing by calling psignal().
 * If we happen to be reading from a file [in iread()] at the time
 * the signal is received, we call intread to interrupt the iread.
 */

#if defined(sun)
#include <libintl.h>
#include <locale.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include "less.h"
#endif


/*
 * "sigs" contains bits indicating signals which need to be processed.
 */
int sigs;

#define	S_STOP		02
#define S_WINCH		04

extern int sc_width, sc_height;
extern int screen_trashed;
extern int lnloop;
extern int linenums;
extern off_t scrolln;
extern int reading;

/*
 * "Stop" (^Z) signal handler.
 */
static void
stop(int s)
{
	(void)signal(SIGTSTP, stop);
	sigs |= S_STOP;
	if (reading)
		intread();
}

/*
 * "Window" change handler
 */
void
#if defined(sun)
win_chg(int s)
#else
winch(int s)
#endif
{
#if defined(sun)
	(void)signal(SIGWINCH, win_chg);
#else
	(void)signal(SIGWINCH, winch);
#endif
	sigs |= S_WINCH;
	if (reading)
		intread();
}

static void
purgeandquit(int s)
{

	purge();	/* purge buffered output */
	quit();
}

/*
 * Set up the signal handlers.
 */
void
init_signals(int on)
{
	if (on)
	{
		/*
		 * Set signal handlers.
		 */
		(void)signal(SIGINT, purgeandquit);
		(void)signal(SIGTSTP, stop);
#if defined(sun)
		(void)signal(SIGWINCH, win_chg);
#else
		(void)signal(SIGWINCH, winch);
#endif
	} else
	{
		/*
		 * Restore signals to defaults.
		 */
		(void)signal(SIGINT, SIG_DFL);
		(void)signal(SIGTSTP, SIG_DFL);
		(void)signal(SIGWINCH, SIG_IGN);
	}
}

/*
 * Process any signals we have received.
 * A received signal cause a bit to be set in "sigs".
 */
void
psignals(void)
{
	register int tsignals;

	if ((tsignals = sigs) == 0)
		return;
	sigs = 0;

	if (tsignals & S_WINCH)
	{
		int old_width, old_height;
		extern int tablesize;
		extern off_t *table;
		extern int exit_status;
		int i;
		off_t *tptr;
		/*
		 * Re-execute get_term() to read the new window size.
		 */
		old_width = sc_width;
		old_height = sc_height;
		get_term();
		if (sc_width != old_width || sc_height != old_height)
		{
			scrolln = (off_t) ((sc_height + 1) / 2);
			screen_trashed = 1;
			/*
			 * if screen size changed because of SIGWINCH and
			 * screen is now larger, we may need to resize
			 * the position table. This is normally done
			 * by the pos_clear() routine, but calling
			 * that here destroys our current file position,
			 * so we do our own table resize, and only
			 * null out the new entries we've added.
			 */
			if (sc_height > old_height && sc_height >= tablesize) {
				tablesize = sc_height;
				tptr = (off_t *)realloc(table,
					tablesize * sizeof (*table));
				if (tptr == NULL) {
					error(gettext(
						"cannot allocate memory"));
					exit_status = 2;
					quit();
				} else {
					table = tptr;
				}
				for (i = old_height; i < sc_height; i++)
					table[i] = NULL_POSITION;
			}	 
		}
	}
	if (tsignals & S_STOP)
	{
		/*
		 * Clean up the terminal.
		 */
		(void)signal(SIGTTOU, SIG_IGN);
		lower_left();
		clear_eol();
		deinit();
		(void)flush();
		raw_mode(0);
		(void)signal(SIGTTOU, SIG_DFL);
		(void)signal(SIGTSTP, SIG_DFL);
		(void)kill(getpid(), SIGTSTP);
		/*
		 * ... Bye bye. ...
		 * Hopefully we'll be back later and resume here...
		 * Reset the terminal and arrange to repaint the
		 * screen when we get back to the main command loop.
		 */
		(void)signal(SIGTSTP, stop);
		raw_mode(1);
		init();
		screen_trashed = 1;
	}
}
