/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 1995-1998 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/* LINTLIBRARY */

/*
 * curs_set.c
 *
 * XCurses Library
 *
 * Copyright 1990, 1995 by Mortice Kern Systems Inc.  All rights reserved.
 *
 */

#ifdef M_RCSID
#ifndef lint
static char rcsID[] =
"$Header: /team/ps/sun_xcurses/archive/local_changes/xcurses/src/lib/"
"libxcurses/src/libc/xcurses/rcs/curs_set.c 1.3 1998/05/08 15:17:50 "
"cbates Exp $";
#endif
#endif

#include <private.h>

/*
 * Turn cursor off/on.  Assume cursor is on to begin with.
 */
int
curs_set(int visibility)
{
	int old;

	/* Assume cursor is initially on. */
	static int cursor_state = 1;

	old = cursor_state;
	switch (visibility) {
	case 0:
		if (cursor_invisible != NULL) {
			(void) TPUTS(cursor_invisible, 1, __m_outc);
			cursor_state = visibility;
		}
		break;
	case 1:
		if (cursor_normal != NULL) {
			(void) TPUTS(cursor_normal, 1, __m_outc);
			cursor_state = visibility;
		}
		break;
	case 2:
		if (cursor_visible != NULL) {
			(void) TPUTS(cursor_visible, 1, __m_outc);
			cursor_state = visibility;
		}
		break;
	default:
		return (ERR);
	}

	return (old);
}
