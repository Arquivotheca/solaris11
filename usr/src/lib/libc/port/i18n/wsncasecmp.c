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
 * Copyright (c) 1992, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Compare strings ignoring case difference.
 *	returns:  s1>s2: >0  s1==s2: 0  s1<s2: <0
 * All letters are converted to the lowercase and compared.
 */

#pragma weak _wsncasecmp = wsncasecmp

#include "lint.h"
#include <stdlib.h>
#include <widec.h>
#include "libc.h"

int
wcsncasecmp(const wchar_t *s1, const wchar_t *s2, size_t n)
{
	if (s1 == s2)
		return (0);

	n++;
	while (--n > 0 && towlower(*s1) == towlower(*s2++))
		if (*s1++ == 0)
			return (0);
	return ((n == 0) ? 0 : (towlower(*s1) - towlower(*(s2 - 1))));
}

int
wsncasecmp(const wchar_t *s1, const wchar_t *s2, size_t n)
{
	return (wcsncasecmp(s1, s2, n));
}
