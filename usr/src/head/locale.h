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

#ifndef _LOCALE_H
#define	_LOCALE_H

#include <iso/locale_iso.h>

#if (!defined(_STRICT_STDC) && !defined(__XOPEN_OR_POSIX)) || \
	defined(__EXTENSIONS__)
#include <libintl.h>
#endif

/*
 * Allow global visibility for symbols defined in
 * C++ "std" namespace in <iso/locale_iso.h>.
 */
#if __cplusplus >= 199711L
using std::lconv;
using std::setlocale;
using std::localeconv;
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define	_LastCategory	LC_MESSAGES	/* This must be last category */

#define	_ValidCategory(c) \
	(((int)(c) >= LC_CTYPE) && ((int)(c) <= _LastCategory) || \
	((int)c == LC_ALL))

#if defined(__EXTENSIONS__) || !defined(__XOPEN_OR_POSIX)
/*
 * Possible flag values for locale list query and its behavior control.
 */
#define	LCLIST_QUERY			0x0001
#define	LCLIST_DO_NOT_INCLUDE_POSIX	0x0002
#define	LCLIST_EXCLUDE_SYMBOLIC_LINKS	0x0004
#define	LCLIST_INCLUDE_LC_MESSAGES	0x0008
#define	LCLIST_KEEP			0x0010
#define	LCLIST_VALIDATE			0x0020

typedef struct {
	char *locale;
	void *reserved;
} lclist_t;

#if defined(__STDC__)
extern int localelist(lclist_t **, int);
extern void localelistfree(lclist_t *);
#else
extern int localelist();
extern void localelistfree();
#endif
#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _LOCALE_H */
