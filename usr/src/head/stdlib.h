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

/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

#ifndef _STDLIB_H
#define	_STDLIB_H

#include <iso/stdlib_iso.h>
#include <iso/stdlib_c99.h>

#if defined(__EXTENSIONS__) || defined(_XPG4)
#include <sys/wait.h>
#endif

/*
 * Allow global visibility for symbols defined in
 * C++ "std" namespace in <iso/stdlib_iso.h>.
 */
#if __cplusplus >= 199711L
using std::div_t;
using std::ldiv_t;
using std::size_t;
using std::abort;
using std::abs;
using std::atexit;
using std::atof;
using std::atoi;
using std::atol;
using std::bsearch;
using std::calloc;
using std::div;
using std::exit;
using std::free;
using std::getenv;
using std::labs;
using std::ldiv;
using std::malloc;
using std::mblen;
using std::mbstowcs;
using std::mbtowc;
using std::qsort;
using std::rand;
using std::realloc;
using std::srand;
using std::strtod;
using std::strtol;
using std::strtoul;
using std::system;
using std::wcstombs;
using std::wctomb;
#endif

#ifdef	__cplusplus
extern "C" {
#endif

#ifndef _UID_T
#define	_UID_T
typedef	unsigned int	uid_t;		/* UID type		*/
#endif	/* !_UID_T */

#if defined(__STDC__)

/* large file compilation environment setup */
#if !defined(_LP64) && _FILE_OFFSET_BITS == 64

#ifdef	__PRAGMA_REDEFINE_EXTNAME
#pragma redefine_extname	mkstemp		mkstemp64
#pragma redefine_extname	mkstemps	mkstemps64
#else	/* __PRAGMA_REDEFINE_EXTNAME */
#define	mkstemp			mkstemp64
#define	mkstemps		mkstemps64
#endif	/* __PRAGMA_REDEFINE_EXTNAME */

#endif	/* _FILE_OFFSET_BITS == 64 */

/* In the LP64 compilation environment, all APIs are already large file */
#if defined(_LP64) && defined(_LARGEFILE64_SOURCE)

#ifdef	__PRAGMA_REDEFINE_EXTNAME
#pragma redefine_extname	mkstemp64	mkstemp
#pragma redefine_extname	mkstemps64	mkstemps
#else	/* __PRAGMA_REDEFINE_EXTNAME */
#define	mkstemp64		mkstemp
#define	mkstemps64		mkstemps
#endif	/* __PRAGMA_REDEFINE_EXTNAME */

#endif	/* _LP64 && _LARGEFILE64_SOURCE */

#if defined(__EXTENSIONS__) || \
	(!defined(_STRICT_STDC) && !defined(__XOPEN_OR_POSIX)) || \
	(_POSIX_C_SOURCE - 0 >= 199506L) || defined(_REENTRANT)
extern int rand_r(unsigned int *);
#endif

extern void _exithandle(void);

#if defined(__EXTENSIONS__) || \
	(!defined(_STRICT_STDC) && !defined(_POSIX_C_SOURCE)) || \
	defined(_XPG4)
extern double drand48(void);
extern double erand48(unsigned short *);
extern long jrand48(unsigned short *);
extern void lcong48(unsigned short *);
extern long lrand48(void);
extern long mrand48(void);
extern long nrand48(unsigned short *);
extern unsigned short *seed48(unsigned short *);
extern void srand48(long);
extern int putenv(char *);
extern void setkey(const char *);
#endif /* defined(__EXTENSIONS__) || !defined(_STRICT_STDC) ... */

/*
 * swab() has historically been in <stdlib.h> as delivered from AT&T.
 * As of Issue 4 of the X/Open Portability Guides, swab() was declared
 * in <unistd.h>.  The default compilation environment follows this rule.
 * As a result, the swab() declaration in this header is only visible
 * for the XPG3 environment or when __USE_LEGACY_PROTOTYPES__ is defined.
 */
#if (defined(_XPG3) && !defined(_XPG4)) || defined(__USE_LEGACY_PROTOTYPES__)
#ifndef	_SSIZE_T
#define	_SSIZE_T
#if defined(_LP64) || defined(_I32LPx)
typedef long	ssize_t;	/* size of something in bytes or -1 */
#else
typedef int	ssize_t;	/* (historical version) */
#endif
#endif	/* !_SSIZE_T */
extern void swab(const char *, char *, ssize_t);
#endif	/* (defined(_XPG3) && !defined(_XPG4)) || ... */

#if defined(__EXTENSIONS__) || \
	!defined(__XOPEN_OR_POSIX) || defined(_XPG4_2) || \
	(defined(_LARGEFILE_SOURCE) && _FILE_OFFSET_BITS == 64)
extern int	mkstemp(char *);
#if !defined(_XPG4_2) || defined(__EXTENSIONS__)
extern int	mkstemps(char *, int);
#endif
#endif /* defined(__EXTENSIONS__) ... */

#if	defined(_LARGEFILE64_SOURCE) && !((_FILE_OFFSET_BITS == 64) && \
	    !defined(__PRAGMA_REDEFINE_EXTNAME))
extern int	mkstemp64(char *);
#if !defined(_XPG4_2) || defined(__EXTENSIONS__)
extern int	mkstemps64(char *, int);
#endif
#endif	/* _LARGEFILE64_SOURCE... */

#if defined(__EXTENSIONS__) || \
	(!defined(_STRICT_STDC) && !defined(__XOPEN_OR_POSIX)) || \
	defined(_XPG4_2)
extern long a64l(const char *);
extern char *ecvt(double, int, int *_RESTRICT_KYWD, int *_RESTRICT_KYWD);
extern char *fcvt(double, int, int *_RESTRICT_KYWD, int *_RESTRICT_KYWD);
extern char *gcvt(double, int, char *);
extern int getsubopt(char **, char *const *, char **);
extern int  grantpt(int);
extern char *initstate(unsigned, char *, size_t);
extern char *l64a(long);
extern char *mktemp(char *);
extern char *ptsname(int);
extern long random(void);
extern char *realpath(const char *_RESTRICT_KYWD, char *_RESTRICT_KYWD);
extern char *setstate(const char *);
extern void srandom(unsigned);
extern int  unlockpt(int);
/* Marked LEGACY in SUSv2 and removed in SUSv3 */
#if !defined(_XPG6) || defined(__EXTENSIONS__)
extern int ttyslot(void);
extern void *valloc(size_t);
#endif /* !defined(_XPG6) || defined(__EXTENSIONS__) */
#endif /* defined(__EXTENSIONS__) || ... || defined(_XPG4_2) */

#if defined(__EXTENSIONS__) || \
	(!defined(_STRICT_STDC) && !defined(__XOPEN_OR_POSIX)) || \
	defined(_XPG6)
extern int posix_memalign(void **, size_t, size_t);
extern int posix_openpt(int);
extern int setenv(const char *, const char *, int);
extern int unsetenv(const char *);
#endif

#if defined(__EXTENSIONS__) || \
	(!defined(_STRICT_STDC) && !defined(__XOPEN_OR_POSIX))
extern char *canonicalize_file_name(const char *);
extern int clearenv(void);
extern void closefrom(int);
extern int daemon(int, int);
extern int dup2(int, int);
extern int fdwalk(int (*)(void *, int), void *);
extern char *qecvt(long double, int, int *, int *);
extern char *qfcvt(long double, int, int *, int *);
extern char *qgcvt(long double, int, char *);
extern char *getcwd(char *, size_t);
extern const char *getexecname(void);
extern char *getlogin(void);
extern int getopt(int, char *const *, const char *);
extern char *optarg;
extern int optind, opterr, optopt;
extern char *getpass(const char *);
extern char *getpassphrase(const char *);
extern int getpw(uid_t, char *);
extern int isatty(int);
extern void *memalign(size_t, size_t);
extern char *ttyname(int);
extern char *mkdtemp(char *);
extern const char *getprogname(void);
extern void setprogname(const char *);

#if !defined(_STRICT_STDC) && defined(_LONGLONG_TYPE)
extern char *lltostr(long long, char *);
extern char *ulltostr(unsigned long long, char *);
#endif	/* !defined(_STRICT_STDC) && defined(_LONGLONG_TYPE) */

#endif /* defined(__EXTENSIONS__) || !defined(_STRICT_STDC) ... */

#else /* not __STDC__ */

#if defined(__EXTENSIONS__) || !defined(__XOPEN_OR_POSIX) || \
	(_POSIX_C_SOURCE - 0 >= 199506L) || defined(_REENTRANT)
extern int rand_r();
#endif	/* defined(__EXTENSIONS__) || defined(_REENTRANT) ... */

extern void _exithandle();

#if defined(__EXTENSIONS__) || !defined(_POSIX_C_SOURCE) || defined(_XPG4)
extern double drand48();
extern double erand48();
extern long jrand48();
extern void lcong48();
extern long lrand48();
extern long mrand48();
extern long nrand48();
extern unsigned short *seed48();
extern void srand48();
extern int putenv();
extern void setkey();
#endif /* defined(__EXTENSIONS__) || !defined(_POSIX_C_SOURCE) ... */

#if (defined(__EXTENSIONS__) || !defined(_POSIX_C_SOURCE)) && \
	(!defined(_XOPEN_SOURCE) || (defined(_XPG3) && !defined(_XPG4)))
extern void swab();
#endif

#if defined(__EXTENSIONS__) || \
	!defined(__XOPEN_OR_POSIX) || defined(_XPG4_2) || \
	(defined(_LARGEFILE_SOURCE) && _FILE_OFFSET_BITS == 64)
extern int	mkstemp();
#if !defined(_XPG4_2) || defined(__EXTENSIONS__)
extern int	mkstemps();
#endif
#endif	/* defined(__EXTENSIONS__) ... */

#if	defined(_LARGEFILE64_SOURCE) && !((_FILE_OFFSET_BITS == 64) && \
	    !defined(__PRAGMA_REDEFINE_EXTNAME))
extern int	mkstemp64();
#if !defined(_XPG4_2) || defined(__EXTENSIONS__)
extern int	mkstemps64();
#endif
#endif	/* _LARGEFILE64_SOURCE... */

#if defined(__EXTENSIONS__) || !defined(__XOPEN_OR_POSIX) || defined(_XPG4_2)
extern long a64l();
extern char *ecvt();
extern char *fcvt();
extern char *gcvt();
extern int getsubopt();
extern int grantpt();
extern char *initstate();
extern char *l64a();
extern char *mktemp();
extern char *ptsname();
extern long random();
extern char *realpath();
extern char *setstate();
extern void srandom();
/* Marked LEGACY in SUSv2 and removed in SUSv3 */
#if !defined(_XPG6) || defined(__EXTENSIONS__)
extern int ttyslot();
extern void *valloc();
#endif /* !defined(_XPG6) || defined(__EXTENSIONS__) */
#endif /* defined(__EXTENSIONS__) || ... || defined(_XPG4_2) */

#if defined(__EXTENSIONS__) || !defined(__XOPEN_OR_POSIX) || defined(_XPG6)
extern int posix_memalign();
extern int posix_openpt();
extern int setenv();
extern int unsetenv();
#endif

#if defined(__EXTENSIONS__) || !defined(__XOPEN_OR_POSIX)
extern char *canonicalize_file_name();
extern int clearenv();
extern void closefrom();
extern int daemon();
extern int dup2();
extern int fdwalk();
extern char *qecvt();
extern char *qfcvt();
extern char *qgcvt();
extern char *getcwd();
extern char *getexecname();
extern char *getlogin();
extern int getopt();
extern char *optarg;
extern int optind, opterr, optopt;
extern char *getpass();
extern char *getpassphrase();
extern int getpw();
extern int isatty();
extern void *memalign();
extern char *ttyname();
extern char *mkdtemp();
extern char *getprogname();
extern void setprogname();

#if defined(_LONGLONG_TYPE)
extern char *lltostr();
extern char *ulltostr();
#endif  /* defined(_LONGLONG_TYPE) */
#endif	/* defined(__EXTENSIONS__) || !defined(__XOPEN_OR_POSIX) ... */

#endif	/* __STDC__ */

#ifdef	__cplusplus
}
#endif

#endif	/* _STDLIB_H */
