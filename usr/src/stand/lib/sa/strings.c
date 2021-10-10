/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 * Copyright 1998, 2003 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"	/* from SVR4.0 1.1 */

#include <stdio.h>
#include <strings.h>

/*
 * NOTE: strcasecmp()/strncasecmp() are in $SRC/common/util/string.c.
 */

int
bcmp(const void *s1, const void *s2, size_t len)
{
	return (memcmp(s1, s2, len) != 0);
}

/*
 * NOTE: our memmove() implementation depends on the fact that this bcopy()
 * supports overlapping regions.
 */
void
bcopy(const void *src, void *dest, size_t count)
{
	const char	*csrc	= src;
	char		*cdest	= dest;

	if (count == 0)
		return;

	if (csrc < cdest && (csrc + count) > cdest) {
		/* overlap copy */
		while (count != 0)
			--count, *(cdest + count) = *(csrc + count);
	} else {
		while (count != 0)
			--count, *cdest++ = *csrc++;
	}
}

void
bzero(void *p, size_t n)
{
	char	*cp;

	for (cp = p; n != 0; n--)
		*cp++ = 0;
}
