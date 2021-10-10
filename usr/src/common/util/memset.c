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
 * Copyright (c) 1988, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

#if !defined(_KMDB) && !defined(_BOOT) && !defined(_KERNEL)

#include "lint.h"

#endif /* !_KMDB && !_BOOT && !_KERNEL */

/*
 * The SunStudio compiler may generate calls to _memset; So we
 * need to make sure that the correct symbol exists for these calls.
 */
#pragma weak _memset = memset

#include <sys/types.h>

#if defined(_KERNEL)
#include <sys/systm.h>
#elif !defined(_BOOT)
#include <string.h>
#endif

#include "string.h"

/*
 * Set an array of n chars starting at sp to the character c.
 * Return sp.
 */
void *
memset(void *sp1, int c, size_t n)
{
	if (n != 0) {
		unsigned char *sp = sp1;
		do {
			*sp++ = (unsigned char)c;
		} while (--n != 0);
	}

	return (sp1);
}
