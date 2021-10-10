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
/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/


#pragma ident	"%Z%%M%	%I%	%E% SMI" 	/* SVr4.0 2.	*/
#include "mail.h"
/*
	Signal reset
	signals that are not being ignored will be 
	caught by function f
		i	-> signal number
		f	-> signal routine
	return
		rc	-> former signal
 */
void (*setsig(i, f))()
int      i;
void      (*f)();
{
	register void (*rc)();

	if ((rc = signal(i, SIG_IGN)) != (void (*)()) SIG_IGN)
		signal(i, f);
	return(rc);
}
