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



#include <sys/types.h>
#include <stdio.h>
#include <userdefs.h>

#include <sys/param.h>
#ifndef	MAXUID
#include <limits.h>
#ifdef UID_MAX
#define	MAXUID	UID_MAX
#else
#define	MAXUID	60000
#endif
#endif

/*
 * Check to see that the gid is not a reserved gid
 * -- nobody, noaccess or nogroup
 */
static int
isvalidgid(gid_t gid)
{
	return (gid != 60001 && gid != 60002 && gid != 65534);
}

gid_t
findnextgid()
{
	FILE *fptr;
	gid_t last, next;
	gid_t gid;

	/*
	 * Sort the used GIDs in decreasing order to return MAXUSED + 1
	 */
	if ((fptr = popen("exec sh -c "
	    "\"getent group|cut -f3 -d:|sort -nr|uniq \" 2>/dev/null",
	    "r")) == NULL)
		return ((gid_t)-1);

	if (fscanf(fptr, "%u\n", &next) == EOF) {
		(void) pclose(fptr);
		return (DEFRID + 1);
	}

	/*
	 * 'next' is now the highest allocated gid.
	 *
	 * The simplest allocation is where we just add one, and obtain
	 * a valid gid.  If this fails look for a hole in the gid range ..
	 */

	last = MAXUID;		/* upper limit */
	gid = (gid_t)-1;	/* start invalid */
	do {
		if (!isvalidgid(next))
			continue;

		if (next <= DEFRID) {
			if (last != DEFRID + 1)
				gid = DEFRID + 1;
			break;
		}

		if ((gid = next + 1) != last) {
			while (!isvalidgid(gid))
				gid++;
			if (gid > 0 && gid < last)
				break;
		}

		gid = (gid_t)-1;
		last = next;

	} while (fscanf(fptr, "%u\n", &next) != EOF);

	(void) pclose(fptr);

	return (gid);
}
