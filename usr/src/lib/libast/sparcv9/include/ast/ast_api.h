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
/* : : generated from /home/gisburn/ksh93/ast_ksh_20110208/build_sparc_64bit_opt/src/lib/libast/features/api by iffe version 2011-01-07 : : */
#ifndef _AST_API_H
#define _AST_API_H	1
#define _sys_types	1	/* #include <sys/types.h> ok */
#define _AST_VERSION AST_VERSION /* pre-20100601 compatibility */

#define AST_VERSION	20100601

#if !defined(_API_ast) && defined(_API_DEFAULT)
#define _API_ast	_API_DEFAULT
#endif

#if ( _BLD_ast || !_API_ast || _API_ast >= 20100601 )
#undef	pathaccess
#define pathaccess	pathaccess_20100601
#endif

#if ( _BLD_ast || !_API_ast || _API_ast >= 20100601 )
#undef	pathcanon
#define pathcanon	pathcanon_20100601
#endif

#if ( _BLD_ast || !_API_ast || _API_ast >= 20100601 )
#undef	pathcat
#define pathcat	pathcat_20100601
#endif

#if ( _BLD_ast || !_API_ast || _API_ast >= 20100601 )
#undef	pathkey
#define pathkey	pathkey_20100601
#endif

#if ( _BLD_ast || !_API_ast || _API_ast >= 20100601 )
#undef	pathpath
#define pathpath	pathpath_20100601
#endif

#if ( _BLD_ast || !_API_ast || _API_ast >= 20100601 )
#undef	pathprobe
#define pathprobe	pathprobe_20100601
#endif

#if ( _BLD_ast || !_API_ast || _API_ast >= 20100601 )
#undef	pathrepl
#define pathrepl	pathrepl_20100601
#endif

#if ( _BLD_ast || !_API_ast || _API_ast >= 20000308 )
#undef	sfkeyprintf
#define sfkeyprintf	sfkeyprintf_20000308
#endif

#define _API_ast_MAP	"pathaccess_20100601 pathcanon_20100601 pathcat_20100601 pathkey_20100601 pathpath_20100601 pathprobe_20100601 pathrepl_20100601 sfkeyprintf_20000308"

#endif
