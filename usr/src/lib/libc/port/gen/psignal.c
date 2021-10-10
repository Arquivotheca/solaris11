/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
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
 * Copyright (c) 1988, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*
 * University Copyright- Copyright (c) 1982, 1986, 1988
 * The Regents of the University of California
 * All Rights Reserved
 *
 * University Acknowledgment- Portions of this document are derived from
 * software developed by the University of California, Berkeley, and its
 * contributors.
 */

/*
 * Print the name of the signal indicated by "sig", along with the
 * supplied message
 */

#pragma weak _psignal = psignal

#include "lint.h"
#include "_libc_gettext.h"
#include <sys/types.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <siginfo.h>

#define	strsignal(i)	(_libc_gettext(_sys_siglistp[i]))

void
psignal(int sig, const char *s)
{
	char *c;
	size_t n;
	char buf[256];

	if (sig < 0 || sig >= NSIG)
		sig = 0;
	c = strsignal(sig);
	if (s == NULL)
		n = 0;
	else
		n = strlen(s);

	if (n) {
		(void) snprintf(buf, sizeof (buf), "%s: %s\n", s, c);
	} else {
		(void) snprintf(buf, sizeof (buf), "%s\n", c);
	}
	(void) write(2, buf, strlen(buf));
}
