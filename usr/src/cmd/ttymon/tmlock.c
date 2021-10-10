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
 * Copyright (c) 1989, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/



#include	<unistd.h>
#include	<string.h>
#include	<sys/termios.h>
/* --------------------------------------------------------- */
/* the follwing are here so we can use routines in ulockf.c */
int	Debug = 0;
char	*Bnptr;
/* dummies for using uucp .o routines */
/*VARARGS*/
/*ARGSUSED*/
void
assert(char *s1, char *s2, int i1, char *s3, int i2)
{}

void
cleanup()
{}

/*ARGSUSED*/
void
logent(char *s1, char *s2)
{}		/* so we can load ulockf() */
/* ---------------------------------------------------------- */
extern	int	lockf();

/*
 *	lastname	- If the path name starts with "/dev/",
 *			  return the rest of the string.
 *			- Otherwise, return the last token of the path name
 */
char *
lastname(char *name)
{
	char	*sp, *p;

	sp = name;
	if (strncmp(sp, "/dev/", 5) == 0)
		sp += 5;
	else
		while ((p = (char *)strchr(sp, '/')) != (char *)NULL) {
			sp = ++p;
		}
	return (sp);
}

/*
 *	tm_lock(fd)	- set advisory lock on the device
 */
int
tm_lock(int fd)
{
	extern	int	fd_mklock();
	return (fd_mklock(fd));
}
