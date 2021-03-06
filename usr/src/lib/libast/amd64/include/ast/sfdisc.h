
/* : : generated by proto : : */
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
                  
/*
 * AT&T Research
 *
 * sfio discipline interface definitions
 */

#ifndef _SFDISC_H
#if !defined(__PROTO__)
#include <prototyped.h>
#endif
#if !defined(__LINKAGE__)
#define __LINKAGE__		/* 2004-08-11 transition */
#endif

#define _SFDISC_H

#include <ast.h>

#define SFDCEVENT(a,b,n)	((((a)-'A'+1)<<11)^(((b)-'A'+1)<<6)^(n))

#if _BLD_ast && defined(__EXPORT__)
#undef __MANGLE__
#define __MANGLE__ __LINKAGE__		__EXPORT__
#endif

#define SFSK_DISCARD		SFDCEVENT('S','K',1)

/*
 * %(...) printf support
 */

typedef int (*Sf_key_lookup_t) __PROTO__((__V_*, Sffmt_t*, const char*, char**, Sflong_t*));
typedef char* (*Sf_key_convert_t) __PROTO__((__V_*, Sffmt_t*, const char*, char*, Sflong_t));

extern __MANGLE__ int		sfkeyprintf __PROTO__((Sfio_t*, __V_*, const char*, Sf_key_lookup_t, Sf_key_convert_t));
extern __MANGLE__ int		sfkeyprintf_20000308 __PROTO__((Sfio_t*, __V_*, const char*, Sf_key_lookup_t, Sf_key_convert_t));

/*
 * pure sfio read and/or write disciplines
 */

extern __MANGLE__ int		sfdcdio __PROTO__((Sfio_t*, size_t));
extern __MANGLE__ int		sfdcdos __PROTO__((Sfio_t*));
extern __MANGLE__ int		sfdcfilter __PROTO__((Sfio_t*, const char*));
extern __MANGLE__ int		sfdcmore __PROTO__((Sfio_t*, const char*, int, int));
extern __MANGLE__ int		sfdcprefix __PROTO__((Sfio_t*, const char*));
extern __MANGLE__ int		sfdcseekable __PROTO__((Sfio_t*));
extern __MANGLE__ int		sfdcslow __PROTO__((Sfio_t*));
extern __MANGLE__ int		sfdctee __PROTO__((Sfio_t*, Sfio_t*));
extern __MANGLE__ int		sfdcunion __PROTO__((Sfio_t*, Sfio_t**, int));

extern __MANGLE__ Sfio_t*		sfdcsubstream __PROTO__((Sfio_t*, Sfio_t*, Sfoff_t, Sfoff_t));

#undef __MANGLE__
#define __MANGLE__ __LINKAGE__

#endif
