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
 * Copyright (c) 2007, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_ATTR_H
#define	_ATTR_H

#include <sys/types.h>
#include <sys/nvpair.h>
#include <sys/attr.h>

#ifdef	__cplusplus
extern "C" {
#endif

#if defined(__STDC__)

extern int getattrat(int, xattr_view_t, const char *, nvlist_t **);
extern int fgetattr(int, xattr_view_t, nvlist_t **);
extern int setattrat(int, xattr_view_t, const char *, nvlist_t *);
extern int fsetattr(int, xattr_view_t, nvlist_t *);

#else	/* defined(__STDC__) */

extern int getattrat();
extern int fgetattr();
extern int setattrat();
extern int fsetattr();

#endif	/* defined(__STDC__) */

#ifdef	__cplusplus
}
#endif

#endif	/* _ATTR_H */
