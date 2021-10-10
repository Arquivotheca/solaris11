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



/*LINTLIBRARY*/

#include <ctype.h>
#include <userdefs.h>
#include <users.h>



/*
 * validate string given as login name.
 */
int
valid_login_check(char *login, struct passwd **pptr, int *warning,
    sec_repository_t *rep, nss_XbyY_buf_t *b)
{
	struct passwd *t_pptr;
	char *ptr = login;
	int bad1char, badc, clower, len;
	char c;

	len = 0;
	clower = 0;
	badc = 0;
	bad1char = 0;
	*warning = 0;
	if (!login || !*login)
		return (INVALID);

	c = *ptr;
	if (!isalpha(c))
		bad1char++;
	for (; c != NULL; ptr++, c = *ptr) {
		len++;
		if (!isprint(c) || (c == ':') || (c == '\n'))
			return (INVALID);
		if (!isalnum(c) && c != '_' && c != '-' && c != '.')
			badc++;
		if (islower(c))
			clower++;
	}

	/*
	 * XXX length checking causes some operational/compatibility problem.
	 * This has to be revisited in the future as ARC/standards issue.
	 */
	if (len > LOGNAME_MAX)
		*warning = *warning | WARN_NAME_TOO_LONG;
	if (clower == 0)
		*warning = *warning | WARN_NO_LOWERCHAR;
	if (badc != 0)
		*warning = *warning | WARN_BAD_LOGNAME_CHAR;
	if (bad1char != 0)
		*warning = *warning | WARN_BAD_LOGNAME_FIRST;

	if (rep->rops->get_pwnam(login, &t_pptr, b) == 0) {
		if (pptr != NULL) {
			*pptr = t_pptr;
		}
		return (NOTUNIQUE);
	}
	return (UNIQUE);
}
