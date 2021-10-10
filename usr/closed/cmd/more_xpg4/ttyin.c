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
static char rcsid[] = "@(#)$RCSfile: ttyin.c,v $ $Revision: 1.1.2.2 $ (OSF) $Date: 1992/08/24 18:19:50 $";
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
static char sccsid[] = "@(#)ttyin.c	5.4 (Berkeley) 6/1/90";
#endif /* not lint */

/*
 * Routines dealing with getting input from the keyboard (i.e. from the user).
 */

#if defined(sun)
#include <libintl.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/types.h>
#if defined(TEST_STDERR)
#include <fcntl.h>
#endif
#include <stdio.h>
#include "less.h"

extern int exit_status;
#endif

static int tty;

static void unget_char();

/*
 * Open keyboard for input.
 * (Just use file descriptor 2.)
 */
#if defined(sun)
int
#else
void
#endif
open_getchr(void)
{
#if defined(sun)
#if defined(TEST_STDERR)
	int stderr_mode;
#endif
#endif

	tty = 2;

#if defined(sun)
#if defined(TEST_STDERR)
	/*
	 * This check is here for VSC assertion #32.  The assertion is
	 * incorrect and will be removed.  I have been reassured that the
	 * assertion that requires this will be ignored.
	 *
	 * I am leaving this code in (though behind an #ifdef) just in
	 * case someone changes his mind and this check becomes required.
	 */
	stderr_mode = fcntl(tty, F_GETFL) & O_ACCMODE;
	if ((stderr_mode == O_RDWR) || (stderr_mode == O_WRONLY))
		return 1;
	return 0;
#endif /* defined(TEST_STDERR) */
	return 1;
#endif /* defined(sun) */
}

/*
 * Get a character from the keyboard.
 */
static char	get_char_buf[MB_LEN_MAX * 2];
static int	get_char_p;

int
getchr(void)
{
	unsigned char c;
	int result;

	if (get_char_p) {
		get_char_p--;
		return ((unsigned char)get_char_buf[get_char_p]);
	}
		
	do
	{
		result = iread(tty, &c, 1);
		if (result == READ_INTR)
			return (READ_INTR);
		if (result < 0)
		{
			/*
			 * Don't call error() here,
			 * because error calls getchr!
			 */
			exit_status = 2;
			quit();
		}
	} while (result != 1);
	return (c);
}

void
unget_char(c)
char	c;
{
	get_char_buf[get_char_p++] = c;
}

/*
 * Get a character from the keyboard.
 */
int
getwchr(void)
{
	wchar_t	c;
	int	i;
	char	mbuf[MB_LEN_MAX + 1];

	for (i = 0; i < (unsigned int)MB_CUR_MAX; i++) {
		mbuf[i] = c = getchr();
		if (c == READ_INTR) {
			i++;
			break;
		}
		if (mbtowc((wchar_t *)&c, mbuf, i + 1) > 0)
			return (c);
		if (c == '\n') {
			i++;
			break;
		}
	}

	for (i--; i > 0; i--)
		unget_char(mbuf[i]);
	c = (unsigned char)mbuf[0];
	return (c);
}
