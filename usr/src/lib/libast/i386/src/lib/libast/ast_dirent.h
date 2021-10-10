
/* : : generated by proto : : */
/* : : generated from /home/gisburn/ksh93/ast_ksh_20110208/build_i386_32bit_opt/src/lib/libast/features/dirent by iffe version 2011-01-07 : : */
                  
#ifndef _def_dirent_ast
#if !defined(__PROTO__)
#  if defined(__STDC__) || defined(__cplusplus) || defined(_proto) || defined(c_plusplus)
#    if defined(__cplusplus)
#      define __LINKAGE__	"C"
#    else
#      define __LINKAGE__
#    endif
#    define __STDARG__
#    define __PROTO__(x)	x
#    define __OTORP__(x)
#    define __PARAM__(n,o)	n
#    if !defined(__STDC__) && !defined(__cplusplus)
#      if !defined(c_plusplus)
#      	define const
#      endif
#      define signed
#      define void		int
#      define volatile
#      define __V_		char
#    else
#      define __V_		void
#    endif
#  else
#    define __PROTO__(x)	()
#    define __OTORP__(x)	x
#    define __PARAM__(n,o)	o
#    define __LINKAGE__
#    define __V_		char
#    define const
#    define signed
#    define void		int
#    define volatile
#  endif
#  define __MANGLE__	__LINKAGE__
#  if defined(__cplusplus) || defined(c_plusplus)
#    define __VARARG__	...
#  else
#    define __VARARG__
#  endif
#  if defined(__STDARG__)
#    define __VA_START__(p,a)	va_start(p,a)
#  else
#    define __VA_START__(p,a)	va_start(p)
#  endif
#  if !defined(__INLINE__)
#    if defined(__cplusplus)
#      define __INLINE__	extern __MANGLE__ inline
#    else
#      if defined(_WIN32) && !defined(__GNUC__)
#      	define __INLINE__	__inline
#      endif
#    endif
#  endif
#endif
#if !defined(__LINKAGE__)
#define __LINKAGE__		/* 2004-08-11 transition */
#endif

#define _def_dirent_ast	1
#define _lib_opendir	1	/* opendir() in default lib(s) */
#define _hdr_dirent	1	/* #include <dirent.h> ok */
#define _nxt_dirent <../include/dirent.h>	/* include path for the native <dirent.h> */
#define _nxt_dirent_str "../include/dirent.h"	/* include string for the native <dirent.h> */
/*
 * <dirent.h> for [fl]stat64 and off64_t
 */

#ifndef _AST_STD_H

#include <../include/dirent.h>	/* the native <dirent.h> */

#else

#ifndef _DIR64_H
#define _DIR64_H

#include <ast_std.h>

#if _typ_off64_t
#undef	off_t
#endif

#include <../include/dirent.h>	/* the native <dirent.h> */

#if _typ_off64_t
#define	off_t		off64_t
#endif

#if _lib_readdir64 && _typ_struct_dirent64
#ifndef	dirent
#define dirent		dirent64
#endif
#ifndef	readdir
#define readdir		readdir64
#endif
#endif

#endif

#endif
#endif
