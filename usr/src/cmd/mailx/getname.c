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
 * Copyright (c) 1985, 2010, Oracle and/or its affiliates. All rights reserved.
 */

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
 * mailx -- a modified version of a University of California at Berkeley
 *	mail program
 */

#include "rcv.h"
#include <pwd.h>

/*
 * Convert the passed name to a user id and return it.  Return -1
 * on error.  Iff the name passed is 0, close the passwd file.
 */

uid_t
getuserid(char name[])
{
	struct passwd *pw;

	if (name == 0) {
		endpwent();
		return (0);
	}
	setpwent();
	pw = getpwnam(name);
	return (pw ? pw->pw_uid : (uid_t)-1);
}
