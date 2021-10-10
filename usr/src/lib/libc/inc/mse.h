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
 * Copyright (c) 1997, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_MSE_H
#define	_MSE_H

#include <stdio.h>
#include <wchar.h>
#include <string.h>
#include "stdiom.h"

typedef enum {
	_NO_MODE,					/* not bound */
	_BYTE_MODE,					/* Byte orientation */
	_WC_MODE					/* Wide orientation */
} _IOP_orientation_t;

/*
 * DESCRIPTION:
 * This function gets the pointer to the mbstate_t structure associated
 * with the specified iop.
 *
 * RETURNS:
 * If the associated mbstate_t found, the pointer to the mbstate_t is
 * returned.  Otherwise, (mbstate_t *)NULL is returned.
 */
#ifdef _LP64
#define	_getmbstate(iop)	(&(iop)->_state)
#else
extern mbstate_t	*_getmbstate(FILE *);
#endif

/*
 * DESCRIPTION:
 * This function/macro gets the orientation bound to the specified iop.
 *
 * RETURNS:
 * _WC_MODE	if iop has been bound to Wide orientation
 * _BYTE_MODE	if iop has been bound to Byte orientation
 * _NO_MODE	if iop has been bound to neither Wide nor Byte
 */
extern _IOP_orientation_t	_getorientation(FILE *);

/*
 * DESCRIPTION:
 * This function/macro sets the orientation to the specified iop.
 *
 * INPUT:
 * flag may take one of the following:
 *	_WC_MODE	Wide orientation
 *	_BYTE_MODE	Byte orientation
 *	_NO_MODE	Unoriented
 */
extern void	_setorientation(FILE *, _IOP_orientation_t);

/*
 * From page 32 of XSH5
 * Once a wide-character I/O function has been applied
 * to a stream without orientation, the stream becomes
 * wide-orientated.  Similarly, once a byte I/O function
 * has been applied to a stream without orientation,
 * the stream becomes byte-orientated.  Only a call to
 * the freopen() function or the fwide() function can
 * otherwise alter the orientation of a stream.
 */

/*
 * libc_i18n provides the following functions:
 */
extern int	_set_orientation_wide(FILE *, void **, void (*(*))(void), int);
extern void	*__mbst_get_lc_and_fp(const mbstate_t *,
    void (*(*))(void), int);
extern void	mark_locale_as_oriented(void);

/*
 * Above two functions take either FP_WCTOMB or FP_FGETWC for the integer
 * argument.
 */
#define	FP_WCTOMB	0
#define	FP_FGETWC	1

#define	_SET_ORIENTATION_BYTE(iop) \
{ \
	if (GET_NO_MODE(iop)) \
		_setorientation(iop, _BYTE_MODE); \
}

/* The following is specified in the argument of _get_internal_mbstate() */
#define	_MBRLEN		0
#define	_MBRTOWC	1
#define	_WCRTOMB	2
#define	_MBSRTOWCS	3
#define	_WCSRTOMBS	4
#define	_MAX_MB_FUNC	_WCSRTOMBS

extern void	_clear_internal_mbstate(void);
extern mbstate_t	*_get_internal_mbstate(int);

#define	MBSTATE_INITIAL(ps)	MBSTATE_RESTART(ps)
#define	MBSTATE_RESTART(ps) \
	(void) memset((void *)ps, 0, sizeof (mbstate_t))

#endif	/* _MSE_H */
