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
/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/


/*
 * Copyright (c) 1989, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <stdio.h>
#include <string.h>
#include <devmgmt.h>

/*
 * This file provides stubbed versions of the historical getvol(),
 * _getvol() and 'listdev' routines.  These were part of a crufty
 * subsystem called OAM devmgmt which came from SVR4.  These remain to
 * emulate a file called /etc/device.tab, which is consulted by SVR4
 * packaging.  The one device we want to emulate is the 'package spool',
 * which is really just a file.  So listdev() emulates that.
 *
 * See also devattr.c.
 */

char *spooldev[4] = {"alias", "desc", "pathname", NULL};

/*
 * listdev returned a list of the attribute names for a given device,
 * in alphabetical order (not the values, just the available attrs).
 */
char **
listdev(char *device)
{
	if (strcmp(device, "spool") == 0) {
		return (spooldev);
	}
	return (NULL);
}

/*
 * getvol() historically returned:
 *	0 - okay, label matches
 *	1 - device not accessable
 *	2 - unknown device (devattr failed)
 *	3 - user selected quit
 *	4 - label does not match
 *
 * We return '2' in all cases except for the spool device, for which
 * we return '0'.
 */
int
getvol(char *device, char *label, int options, char *prompt)
{
	return (_getvol(device, label, options, prompt, NULL));
}

/*ARGSUSED1*/
int
_getvol(char *device, char *label, int options, char *prompt, char *norewind)
{
	if (strcmp(device, "spool") == 0)
		return (0);
	return (2);
}
