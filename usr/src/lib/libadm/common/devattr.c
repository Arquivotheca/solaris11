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

#include	<sys/types.h>
#include	<errno.h>
#include	<string.h>
#include	<devmgmt.h>

/*
 *  char *devattr(device, attr)
 *
 *  This is a stubbed implementation of devattr(), which was part of
 *  the SVR4 OAM device managment stuff which used to be present in
 *  libadm.  This satisfies an interface needed by SVR4 packaging.
 *
 *  devattr was used to search the device table (/etc/device.tab),
 *  looking for the device specified by <device> and to find specific
 *  attributes of specific devices.  See also getvol.c.
 *
 *  In this implementation, we emulate a device.tab file in which
 *  there is a single line defining "spool"-- the "package spool",
 *  which is the directory in /var/spool/pkg.  This line looked like
 *  this:
 *
 *  spool:::/var/spool/pkg:desc="Packaging Spool Directory"
 *
 *  Arguments:
 *	device		Pointer to the character-string that describes the
 *			device whose record is to be looked for
 *	attr		The device's attribute to be looked for
 *
 *  Returns:  char *
 *	A pointer to the character-string containing the value of the
 *	attribute <attr> for the device <device>, or (char *) NULL if none
 *	was found.  If the function returns (char *) NULL and the error was
 *	detected by this function, it sets "errno" to indicate the problem.
 *
 *  "errno" Values:
 *	EPERM		Permissions deny reading access of the device-table
 *			file
 *	ENOENT		The specified device-table file could not be found
 *	ENODEV		Device not found in the device table
 *	EINVAL		The device does not have that attribute defined
 *	ENOMEM		No memory available
 */

char *
devattr(char *device, char *attribute)
{
	char *r = NULL, *rtnval;

	if (strcmp(device, "spool") != 0) {
		errno = ENODEV;
		return (NULL);
	}

	/* Did they ask for the device alias? */
	if (strcmp(attribute, DTAB_ALIAS) == 0)
		r = "spool";

	/*
	 * Alias, cdevice, bdevice, and pathname attrs were never returned as
	 * NULL (for no apparent reason).  "" was used instead.  We stick with
	 * the scheme here.  Other attributes could return NULL.
	 */
	if ((strcmp(attribute, DTAB_CDEVICE) == 0) ||
	    (strcmp(attribute, DTAB_BDEVICE) == 0)) {
		r = "";
	}

	if (strcmp(attribute, DTAB_PATHNAME) == 0)
		r = "/var/spool/pkg";

	if (strcmp(attribute, "desc") == 0)
		r = "Packaging Spool Directory";

	/* We don't emulate any other attributes */
	if (r == NULL) {
		errno = EINVAL;
		return (NULL);
	}

	if ((rtnval = strdup(r)) == NULL) {
		errno = ENOMEM;
		return (NULL);
	}
	return (rtnval);
}
