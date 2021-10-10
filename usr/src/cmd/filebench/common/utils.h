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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_FB_UTILS_H
#define	_FB_UTILS_H

#include "config.h"

#include <sys/types.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	E_ERROR		1		/* Exit status for error */
#define	E_USAGE		2		/* Exit status for usage error */

extern char *fb_stralloc(char *str);

#ifdef sun
#define	fb_strlcat	strlcat
#define	fb_strlcpy	strlcpy
#else
extern size_t fb_strlcat(char *dst, const char *src, size_t dstsize);
extern size_t fb_strlcpy(char *dst, const char *src, size_t dstsize);
#endif /* sun */

#ifdef	__cplusplus
}
#endif

#endif	/* _FB_UTILS_H */
