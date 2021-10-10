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
 * Copyright (c) 2003, 2004, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * An application should not include this header directly.  Instead it
 * should be included only through the inclusion of other Sun headers.
 *
 * The contents of this header is limited to identifiers specified in
 * the C99 standard and in conflict with the C++ implementation of the
 * standard header.  The C++ standard may adopt the C99 standard at
 * which point it is expected that the symbols included here will
 * become part of the C++ std namespace.
 */

#ifndef _ISO_STDLIB_C99_H
#define	_ISO_STDLIB_C99_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * The following have been added as a result of the ISO/IEC 9899:1999
 * standard. For a strictly conforming C application, visibility is
 * contingent on the value of __STDC_VERSION__ (see sys/feature_tests.h).
 * For non-strictly conforming C applications, there are no restrictions
 * on the C namespace.
 */

#if defined(_LONGLONG_TYPE)
typedef struct {
	long long	quot;
	long long	rem;
} lldiv_t;
#endif  /* defined(_LONGLONG_TYPE) */

#ifdef __STDC__

#if (!defined(_STRICT_STDC) && !defined(__XOPEN_OR_POSIX)) || \
	defined(_STDC_C99) || defined(__EXTENSIONS__)

extern void _Exit(int);
extern float strtof(const char *_RESTRICT_KYWD, char **_RESTRICT_KYWD);
extern long double strtold(const char *_RESTRICT_KYWD, char **_RESTRICT_KYWD);

#if defined(_LONGLONG_TYPE)
extern long long atoll(const char *);
extern long long llabs(long long);
extern lldiv_t lldiv(long long, long long);
extern long long strtoll(const char *_RESTRICT_KYWD, char **_RESTRICT_KYWD,
	int);
extern unsigned long long strtoull(const char *_RESTRICT_KYWD,
	char **_RESTRICT_KYWD, int);
#endif /* defined(_LONGLONG_TYPE) */

#endif  /* (!defined(_STRICT_STDC) && !defined(__XOPEN_OR_POSIX)) ... */

#else /* __STDC__ */

#if !defined(__XOPEN_OR_POSIX) || defined(_XPG6) || defined(__EXTENSIONS__)

extern void _Exit();
extern float strtof();
extern long double strtold();

#if defined(_LONGLONG_TYPE)
extern long long atoll();
extern long long llabs();
extern lldiv_t lldiv();
extern long long strtoll();
extern unsigned long long strtoull();
#endif /* defined(_LONGLONG_TYPE) */

#endif /* !defined(__XOPEN_OR_POSIX) || defined(_XPG6)... */

#endif /* __STDC__ */

#ifdef	__cplusplus
}
#endif

#endif	/* _ISO_STDLIB_C99_H */
