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
 * Copyright (c) 1990, 2004, Oracle and/or its affiliates. All rights reserved.
 */

/*	Copyright (c) 1986 AT&T	*/
/*	  All Rights Reserved  	*/


/*	This module is created for NLS on Jun.04.86		*/

#ifndef	_WIDEC_H
#define	_WIDEC_H

#include <sys/feature_tests.h>

#if defined(__STDC__)
#include <stdio.h>	/* For definition of FILE */
#endif
#include <euc.h>
#include <wchar.h>

#ifdef	__cplusplus
extern "C" {
#endif

#if defined(__STDC__)
/* Character based input and output functions */
extern wchar_t	*getws(wchar_t *);
extern int	putws(const wchar_t *);

#if !defined(__lint)
#define	getwc(p)	fgetwc(p)
#define	putwc(x, p)	fputwc((x), (p))
#define	getwchar()	getwc(stdin)
#define	putwchar(x)	putwc((x), stdout)
#endif

/* wchar_t string operation functions */
extern wchar_t	*strtows(wchar_t *, char *);
extern wchar_t	*wscpy(wchar_t *, const wchar_t *);
extern wchar_t	*wsncpy(wchar_t *, const wchar_t *, size_t);
extern wchar_t	*wscat(wchar_t *, const wchar_t *);
extern wchar_t	*wsncat(wchar_t *, const wchar_t *, size_t);
extern wchar_t	*wschr(const wchar_t *, wchar_t);
extern wchar_t	*wsrchr(const wchar_t *, wchar_t);
extern wchar_t	*wspbrk(const wchar_t *, const wchar_t *);
extern wchar_t	*wstok(wchar_t *, const wchar_t *);
extern char	*wstostr(char *, wchar_t *);

extern int	wscmp(const wchar_t *, const wchar_t *);
extern int	wsncmp(const wchar_t *, const wchar_t *, size_t);
extern size_t	wslen(const wchar_t *);
extern size_t	wsspn(const wchar_t *, const wchar_t *);
extern size_t	wscspn(const wchar_t *, const wchar_t *);
extern int	wscoll(const wchar_t *, const wchar_t *);
extern size_t	wsxfrm(wchar_t *, const wchar_t *, size_t);

#if !defined(_STRICT_STDC) && !defined(__XOPEN_OR_POSIX)

extern wchar_t	*wsdup(const wchar_t *);
extern int	wscol(const wchar_t *);
extern double	wstod(const wchar_t *, wchar_t **);
extern long	wstol(const wchar_t *, wchar_t **, int);
extern int	wscasecmp(const wchar_t *, const wchar_t *);
extern int	wsncasecmp(const wchar_t *, const wchar_t *, size_t);
extern int	wsprintf(wchar_t *, const char *, ...);
#if defined(_LONGLONG_TYPE)
extern long long	wstoll(const wchar_t *, wchar_t **, int);
#endif	/* defined(_LONGLONG_TYPE) */

#endif /* !defined(_STRICT_STDC) && !defined(__XOPEN_OR_POSIX) */

#else	/* !defined(__STDC__) */
/* Character based input and output functions */
extern wchar_t		*getws();
extern int		putws();

#ifndef getwc
#define	getwc(p)	fgetwc(p)
#endif
#ifndef putwc
#define	putwc(x, p)	fputwc((x), (p))
#endif
#ifndef getwchar
#define	getwchar()	getwc(stdin)
#endif
#ifndef putwchar
#define	putwchar(x)	putwc((x), stdout)
#endif

/* wchar_t string operation functions */
extern wchar_t	*strtows();
extern wchar_t	*wscpy();
extern wchar_t	*wsncpy();
extern wchar_t	*wscat();
extern wchar_t	*wsncat();
extern wchar_t	*wschr();
extern wchar_t	*wsrchr();
extern wchar_t	*wspbrk();
extern wchar_t	*wstok();

extern int	wscmp();
extern int	wsncmp();
extern int	wslen();
extern int	wsspn();
extern int	wscspn();
extern int	wscoll();
extern int	wsxfrm();

extern char	*wstostr();
extern wchar_t	*wsdup();
extern int	wscol();
extern double	wstod();
extern long	wstol();
extern int	wscasecmp();
extern int	wsncasecmp();
extern int	wsprintf();
#if defined(_LONGLONG_TYPE)
extern long long	wstoll();
#endif	/* defined(_LONGLONG_TYPE) */

#endif	/* !defined(__STDC__) */

/* Returns the code set number for the process code c. */
#define	WCHAR_SHIFT	7
#define	WCHAR_S_MASK	0x7f
#define	wcsetno(c) \
	(((c)&0x20000000)?(((c)&0x10000000)?1:3):(((c)&0x10000000)?2:0))

/* Aliases... */
#define	windex		wschr
#define	wrindex		wsrchr

#define	watol(s)	wstol((s), (wchar_t **)0, 10)
#if defined(_LONGLONG_TYPE) && !defined(__lint)
#define	watoll(s)	wstoll((s), (wchar_t **)0, 10)
#endif	/* defined(_LONGLONG_TYPE) && !defined(__lint) */
#define	watoi(s)	((int)wstol((s), (wchar_t **)0, 10))
#define	watof(s)	wstod((s), (wchar_t **)0)

/*
 * other macros.
 */
#define	WCHAR_CSMASK	0x30000000
#define	EUCMASK		0x30000000
#define	WCHAR_CS0	0x00000000
#define	WCHAR_CS1	0x30000000
#define	WCHAR_CS2	0x10000000
#define	WCHAR_CS3	0x20000000
#define	WCHAR_BYTE_OF(wc, i) (((wc&~0x30000000)>>(7*(3-i)))&0x7f)

#ifdef	__cplusplus
}
#endif

#endif	/* _WIDEC_H */
