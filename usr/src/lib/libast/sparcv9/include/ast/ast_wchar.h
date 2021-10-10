/***********************************************************************
*                                                                      *
*               This software is part of the ast package               *
*          Copyright (c) 1985-2011 AT&T Intellectual Property          *
*                      and is licensed under the                       *
*                  Common Public License, Version 1.0                  *
*                    by AT&T Intellectual Property                     *
*                                                                      *
*                A copy of the License is available at                 *
*            http://www.opensource.org/licenses/cpl1.0.txt             *
*         (with md5 checksum 059e8cd6165cb4c31e351f2b69388fd9)         *
*                                                                      *
*              Information and Software Systems Research               *
*                            AT&T Research                             *
*                           Florham Park NJ                            *
*                                                                      *
*                 Glenn Fowler <gsf@research.att.com>                  *
*                  David Korn <dgk@research.att.com>                   *
*                   Phong Vo <kpv@research.att.com>                    *
*                                                                      *
***********************************************************************/
/* : : generated from /home/gisburn/ksh93/ast_ksh_20110208/build_sparc_64bit_opt/src/lib/libast/features/wchar by iffe version 2011-01-07 : : */
#define _sys_types	1	/* #include <sys/types.h> ok */
#ifndef _AST_WCHAR_H
#define _AST_WCHAR_H	1

#define _hdr_stdlib	1	/* #include <stdlib.h> ok */
#define _hdr_stdio	1	/* #include <stdio.h> ok */
#define _hdr_wchar	1	/* #include <wchar.h> ok */
#define _lib_mbstowcs	1	/* mbstowcs() in default lib(s) */
#define _lib_wctomb	1	/* wctomb() in default lib(s) */
#define _lib_wcrtomb	1	/* wcrtomb() in default lib(s) */
#define _lib_wcslen	1	/* wcslen() in default lib(s) */
#define _lib_wcstombs	1	/* wcstombs() in default lib(s) */
#define _lib_wcwidth	1	/* wcwidth() in default lib(s) */
#define _lib_towlower	1	/* towlower() in default lib(s) */
#define _lib_towupper	1	/* towupper() in default lib(s) */
#define _hdr_time	1	/* #include <time.h> ok */
#define _sys_time	1	/* #include <sys/time.h> ok */
#define _sys_times	1	/* #include <sys/times.h> ok */
#define _hdr_stddef	1	/* #include <stddef.h> ok */
#define _typ_mbstate_t	1	/* mbstate_t is a type */
#define _nxt_wchar <../include/wchar.h>	/* include path for the native <wchar.h> */
#define _nxt_wchar_str "../include/wchar.h"	/* include string for the native <wchar.h> */
#ifndef _SFSTDIO_H
#include <ast_common.h>
#include <stdio.h>
#endif

#define _hdr_unistd	1	/* #include <unistd.h> ok */
#include <wctype.h> /* <wchar.h> includes <wctype.h> */

#if _hdr_wchar && defined(_nxt_wchar)
#include <../include/wchar.h>	/* the native wchar.h */
#endif

#ifndef WEOF
#define WEOF		(-1)
#endif

#undef	fgetwc
#undef	fgetws
#undef	fputwc
#undef	fputws
#undef	getwc
#undef	getwchar
#undef	getws
#undef	putwc
#undef	putwchar
#undef	ungetwc

#define fgetwc		_ast_fgetwc
#define fgetws		_ast_fgetws
#define fputwc		_ast_fputwc
#define fputws		_ast_fputws
#define fwide		_ast_fwide
#define fwprintf	_ast_fwprintf
#define fwscanf		_ast_fwscanf
#define getwc		_ast_getwc
#define getwchar	_ast_getwchar
#define getws		_ast_getws
#define putwc		_ast_putwc
#define putwchar	_ast_putwchar
#define swprintf	_ast_swprintf
#define swscanf		_ast_swscanf
#define ungetwc		_ast_ungetwc
#define vfwprintf	_ast_vfwprintf
#define vfwscanf	_ast_vfwscanf
#define vswprintf	_ast_vswprintf
#define vswscanf	_ast_vswscanf
#define vwprintf	_ast_vwprintf
#define vwscanf		_ast_vwscanf
#define wprintf		_ast_wprintf
#define wscanf		_ast_wscanf

#if !_typ_mbstate_t
#undef	_typ_mbstate_t
#define _typ_mbstate_t	1
typedef char mbstate_t;
#endif

#if _BLD_ast && defined(__EXPORT__)
#define extern		__EXPORT__
#endif

#if !_lib_mbstowcs
extern size_t		mbstowcs(wchar_t*, const char*, size_t);
#endif
#if !_lib_wctomb
extern int		wctomb(char*, wchar_t);
#endif
#if !_lib_wcrtomb
extern size_t		wcrtomb(char*, wchar_t, mbstate_t*);
#endif
#if !_lib_wcslen
extern size_t		wcslen(const wchar_t*);
#endif
#if !_lib_wcstombs
extern size_t		wcstombs(char*, const wchar_t*, size_t);
#endif

extern int		fwprintf(FILE*, const wchar_t*, ...);
extern int		fwscanf(FILE*, const wchar_t*, ...);
extern wint_t		fgetwc(FILE*);
extern wchar_t*		fgetws(wchar_t*, int, FILE*);
extern wint_t		fputwc(wchar_t, FILE*);
extern int		fputws(const wchar_t*, FILE*);
extern int		fwide(FILE*, int);
extern wint_t		getwc(FILE*);
extern wint_t		getwchar(void);
extern wchar_t*		getws(wchar_t*);
extern wint_t		putwc(wchar_t, FILE*);
extern wint_t		putwchar(wchar_t);
extern int		swprintf(wchar_t*, size_t, const wchar_t*, ...);
extern int		swscanf(const wchar_t*, const wchar_t*, ...);
extern wint_t		ungetwc(wint_t, FILE*);
extern int		vfwprintf(FILE*, const wchar_t*, va_list);
extern int		vfwscanf(FILE*, const wchar_t*, va_list);
extern int		vwprintf(const wchar_t*, va_list);
extern int		vwscanf(const wchar_t*, va_list);
extern int		vswprintf(wchar_t*, size_t, const wchar_t*, va_list);
extern int		vswscanf(const wchar_t*, const wchar_t*, va_list);
extern int		wprintf(const wchar_t*, ...);
extern int		wscanf(const wchar_t*, ...);

#undef	extern

#else

/* on some systems <wchar.h> is included multiple times with multiple effects */

#if _hdr_wchar && defined(_nxt_wchar)
#include <../include/wchar.h>	/* the native wchar.h */
#endif

#endif
