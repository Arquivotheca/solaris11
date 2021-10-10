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

#include <stdarg.h>
#include <libintl.h>
#include "users.h"

extern  char    *cmdname;


char *errmsgs[] = {
	"WARNING: gid %ld is reserved.\n",
	"ERROR: invalid syntax.\nusage: groupadd [-S [files | ldap]] "
	    "[-U user1[,user2]] [-g gid [-o]] group\n",
	"ERROR: invalid syntax.\nusage: groupdel [-S [files | ldap]] group\n",
	"ERROR: invalid syntax.\nusage: groupmod [-S [files | ldap]] "
	    "[-U user1[,user2]] [-g gid [-o]] [-n name] group\n",
	"ERROR: Cannot update system files - group cannot be %s.\n",
	"ERROR: %s is not a valid group id.  Choose another.\n",
	"ERROR: %s is already in use.  Choose another.\n",
	"ERROR: %s is not a valid group name.  Choose another.\n",
	"ERROR: %s does not exist.\n",
	"ERROR: Group id %ld is too big.  Choose another.\n",
	"ERROR: Permission denied.\n",
	"ERROR: Syntax error in group file at line %d.\n",
	"ERROR: %s is not a valid user name.  Choose another.\n",
};

int lasterrmsg = sizeof (errmsgs) / sizeof (char *);


/*
 *	synopsis: errmsg(msgid, (arg1, ..., argN))
 */

void
errmsg(int msgid, ...)
{
	va_list	args;

	va_start(args, msgid);

	if (msgid >= 0 && msgid < lasterrmsg) {
		(void) fprintf(stderr, "UX: %s: ", cmdname);
		(void) vfprintf(stderr, gettext(errmsgs[msgid]), args);
	}

	va_end(args);
}

void
warningmsg(int what, char *name)
{
	if ((what & WARN_NAME_TOO_LONG) != 0) {
		(void) fprintf(stderr, "UX: %s: ", cmdname);
		(void) fprintf(stderr, gettext("%s name too long.\n"), name);
	}
	if ((what & WARN_BAD_GROUP_NAME) != 0) {
		(void) fprintf(stderr, "UX: %s: ", cmdname);
		(void) fprintf(stderr, gettext("%s name should be all lower"
		    "case or numeric.\n"), name);
	}
	if ((what & WARN_BAD_PROJ_NAME) != 0) {
		(void) fprintf(stderr, "UX: %s: ", cmdname);
		(void) fprintf(stderr, gettext("%s name should be all lower"
		    "case or numeric.\n"), name);
	}
	if ((what & WARN_BAD_LOGNAME_CHAR) != 0) {
		(void) fprintf(stderr, "UX: %s: ", cmdname);
		(void) fprintf(stderr, gettext("%s name should be all"
		    " alphanumeric, '-', '_', or '.'\n"), name);
	}
	if ((what & WARN_BAD_LOGNAME_FIRST) != 0) {
		(void) fprintf(stderr, "UX: %s: ", cmdname);
		(void) fprintf(stderr, gettext("%s name first character"
		    " should be alphabetic.\n"), name);
	}
	if ((what & WARN_NO_LOWERCHAR) != 0) {
		(void) fprintf(stderr, "UX: %s: ", cmdname);
		(void) fprintf(stderr, gettext("%s name should have at least"
		    " one lower case character.\n"), name);
	}
	if ((what & WARN_LOGGED_IN) != 0) {
		(void) fprintf(stderr, "UX: %s: ", cmdname);
		(void) fprintf(stderr, gettext("%s is currently logged in,"
		    " some changes may not take effect until next login.\n"),
		    name);
	}
}
