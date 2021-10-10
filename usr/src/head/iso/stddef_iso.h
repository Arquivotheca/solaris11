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
/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/


/*
 * Copyright (c) 1999, 2003, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * An application should not include this header directly.  Instead it
 * should be included only through the inclusion of other Sun headers.
 *
 * The contents of this header is limited to identifiers specified in the
 * C Standard.  Any new identifiers specified in future amendments to the
 * C Standard must be placed in this header.  If these new identifiers
 * are required to also be in the C++ Standard "std" namespace, then for
 * anything other than macro definitions, corresponding "using" directives
 * must also be added to <stddef.h.h>.
 */

#ifndef _ISO_STDDEF_ISO_H
#define	_ISO_STDDEF_ISO_H

#include <sys/isa_defs.h>

#ifdef	__cplusplus
extern "C" {
#endif

#if __cplusplus >= 199711L
namespace std {
#endif

#ifndef	NULL
#if defined(_LP64)
#define	NULL    0L
#else
#define	NULL    0
#endif
#endif

#if !defined(_PTRDIFF_T) || __cplusplus >= 199711L
#define	_PTRDIFF_T
#if defined(_LP64) || defined(_I32LPx)
typedef	long	ptrdiff_t;		/* pointer difference */
#else
typedef int	ptrdiff_t;		/* (historical version) */
#endif
#endif	/* !_PTRDIFF_T */

#if !defined(_SIZE_T) || __cplusplus >= 199711L
#define	_SIZE_T
#if defined(_LP64) || defined(_I32LPx)
typedef unsigned long	size_t;		/* size of something in bytes */
#else
typedef unsigned int	size_t;		/* (historical version) */
#endif
#endif	/* !_SIZE_T */

#if __cplusplus >= 199711L
}
#endif /* end of namespace std */

#if __cplusplus >= 199711L
#define	offsetof(s, m)  (std::size_t)(&(((s *)0)->m))
#else
#define	offsetof(s, m)  (size_t)(&(((s *)0)->m))
#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _ISO_STDDEF_ISO_H */
