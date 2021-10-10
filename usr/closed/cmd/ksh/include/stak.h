/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef ___KSH_STAK_H
#define	___KSH_STAK_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifndef STAK_SMALL
/*
 * David Korn
 * AT&T Bell Laboratories
 *
 * Interface definitions for a stack-like storage library
 *
 */

#include	"csi.h"

#if defined(__STDC__) || __cplusplus || c_plusplus
#   define __ARGS(args)	args
#else
#    define const	/*empty*/
#   define __ARGS(args)	()
#endif

/*
 * Need to keep Stak_t (and struct frame) 8-byte aligned.
 * See STAK_ALIGN in sh/stak.c
 */
typedef struct _stak_
{
	int		stakleft;	/* number of bytes left in frame */
	char		*staktop;	/* current stack location */
	char		*stakbot;	/* last returned stack location */
	short		stakref;	/* reference count */
#ifdef _STAK_PRIVATE
	_STAK_PRIVATE
#endif /* _STAK_PRIVATE */
} Stak_t;

#define STAK_SMALL	1	/* argument to stakcreate */

#if __cplusplus
    extern "C"
    {
#endif
	extern Stak_t	*stakcreate __ARGS((int));
	extern Stak_t	*stakinstall __ARGS((Stak_t*, char *(*)(int)));
	extern int	stakdelete __ARGS((Stak_t*));
	extern char	*stakalloc __ARGS((unsigned));
	extern char	*stakcopy __ARGS((const char*));
	extern char	*stakseek __ARGS((unsigned));
	extern int	stakputs __ARGS((const char*));
	extern char	*stakfreeze __ARGS((unsigned));
	extern char	*_stakgrow __ARGS((unsigned));
	extern void	_stakputwc __ARGS((const wchar_t));
	extern void	_stakputwcs __ARGS((const wchar_t *));
	extern void	_wstakputascii __ARGS((const int));
	extern void	_wstakputwc __ARGS((const wchar_t));
	extern void	_wstakputwcs __ARGS((const wchar_t *));
	extern off_t	staksave __ARGS((void));
	extern void	stakrestore __ARGS((off_t, off_t));
	extern void	stakadjust __ARGS((off_t));
#if __cplusplus
    }
#endif

extern Stak_t		_stak_cur;	/* used by macros */

#define	staklink(sp)	((sp)->stakref++)
#define	stakptr(n)	(_stak_cur.stakbot+(n))
#define	staktell()	(_stak_cur.staktop-_stak_cur.stakbot)
#define stakputascii(c)	((--_stak_cur.stakleft<=0? _stakgrow(1):0), \
					*_stak_cur.staktop++=(c))
#define	stakputwc(wc)		_stakputwc(wc)
#define	stakputwcs(wcs)		_stakputwcs(wcs)

/*
 * wstak*() funtions puts data on stak as wide character or
 * wide character string.
 */
#define	wstakputascii(c)	_wstakputascii(c)
#define	wstakputwc(wc)		_wstakputwc(wc)
#define	wstakputwcs(wcs)	_wstakputwcs(wcs)

/*
 * Some functions use stak*() if UseStak is speicified and
 * use wstak*() with UseWStak.
 */
#define	UseStak		0
#define	UseWStak	1

#endif /* STAK_SMALL */

#endif /* !___KSH_STAK_H */
