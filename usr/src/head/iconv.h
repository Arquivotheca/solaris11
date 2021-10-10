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
 * Copyright (c) 1993, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_ICONV_H
#define	_ICONV_H

#include <sys/feature_tests.h>
#include <sys/types.h>

#ifdef	__cplusplus
extern "C" {
#endif

#if defined(__EXTENSIONS__) || !defined(_XOPEN_SOURCE)
/*
 * Possible operation requests for iconv code conversion behavior
 * control and query.
 */
#define	ICONV_GET_CONVERSION_BEHAVIOR		1
#define	ICONV_GET_DISCARD_ILSEQ			2
#define	ICONV_GET_TRANSLITERATE			3
#define	ICONV_SET_CONVERSION_BEHAVIOR		4
#define	ICONV_SET_DISCARD_ILSEQ			5
#define	ICONV_SET_TRANSLITERATE			6
#define	ICONV_TRIVIALP				7

/*
 * Possible code conversion options for string-based code conversions.
 */
#define	ICONV_IGNORE_NULL			0x0001
#define	ICONV_REPLACE_INVALID			0x0002

/*
 * Possible code conversion behavior settings and modifications.
 */
#define	ICONV_CONV_ILLEGAL_DISCARD		0x0100
#define	ICONV_CONV_ILLEGAL_REPLACE_HEX		0x0200
#define	ICONV_CONV_ILLEGAL_RESTORE_HEX		0x0400
#define	ICONV_CONV_NON_IDENTICAL_DISCARD	0x0800
#define	ICONV_CONV_NON_IDENTICAL_REPLACE_HEX	0x1000
#define	ICONV_CONV_NON_IDENTICAL_RESTORE_HEX	0x2000
#define	ICONV_CONV_NON_IDENTICAL_TRANSLITERATE	0x4000
#endif	/* defined(__EXTENSIONS__) || !defined(_XOPEN_SOURCE) */

typedef struct _iconv_info *iconv_t;

#if defined(__STDC__)

extern iconv_t	iconv_open(const char *, const char *);
extern int	iconv_close(iconv_t);

#if (defined(_XOPEN_SOURCE) && !defined(_XPG6)) || \
	defined(__USE_LEGACY_PROTOTYPES__)

/* old (_XPG5 and prior) standard function signature */
#if defined(__PRAGMA_REDEFINE_EXTNAME)
#pragma redefine_extname iconv __xpg5_iconv
extern size_t	iconv(iconv_t, const char **_RESTRICT_KYWD,
		size_t *_RESTRICT_KYWD, char **_RESTRICT_KYWD,
		size_t *_RESTRICT_KYWD);
#else
extern size_t	__xpg5_iconv(iconv_t, const char **_RESTRICT_KYWD,
		size_t *_RESTRICT_KYWD, char **_RESTRICT_KYWD,
		size_t *_RESTRICT_KYWD);
#define	iconv	__xpg5_iconv
#endif	/* __PRAGMA_REDEFINE_EXTNAME */

#else	/* (defined(_XOPEN_SOURCE) && !defined(_XPG6)) ... */

/* new (_XPG6 and later) standard and default function signature */
extern size_t	iconv(iconv_t, char **_RESTRICT_KYWD,
		size_t *_RESTRICT_KYWD, char **_RESTRICT_KYWD,
		size_t *_RESTRICT_KYWD);

#endif	/* (defined(_XOPEN_SOURCE) && !defined(_XPG6)) ... */

#if defined(__EXTENSIONS__) || !defined(_XOPEN_SOURCE)
extern int	iconvctl(iconv_t, int, void *);
extern size_t	iconvstr(const char *, const char *, char *, size_t *,
		char *, size_t *, int);
#endif	/* defined(__EXTENSIONS__) || !defined(_XOPEN_SOURCE) */

#else	/* __STDC__ */

extern iconv_t	iconv_open();
extern int	iconv_close();
extern size_t	iconv();
#if defined(__EXTENSIONS__) || !defined(_XOPEN_SOURCE)
extern int	iconvctl();
extern size_t	iconvstr();
#endif	/* defined(__EXTENSIONS__) || !defined(_XOPEN_SOURCE) */

#endif	/* __STDC__ */

#ifdef	__cplusplus
}
#endif

#endif	/* _ICONV_H */
