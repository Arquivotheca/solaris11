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
 * Copyright (c) 1988, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

#include "lint.h"
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>

#if !defined(_LP64) && _FILE_OFFSET_BITS == 64

int
fgetpos64(FILE *stream, fpos64_t *pos)
{
	if ((*pos = (fpos64_t)ftello64(stream)) == (fpos64_t)-1)
		return (-1);
	return (0);
}

int
fsetpos64(FILE *stream, const fpos64_t *pos)
{
	if (fseeko64(stream, (off64_t)*pos, SEEK_SET) != 0)
		return (-1);
	return (0);
}

#else	/* !defined(_LP64) && _FILE_OFFSET_BITS == 64 */

int
fgetpos(FILE *stream, fpos_t *pos)
{
	if ((*pos = (fpos_t)ftello(stream)) == (fpos_t)-1)
		return (-1);
	return (0);
}

int
fsetpos(FILE *stream, const fpos_t *pos)
{
	if (fseeko(stream, (off_t)*pos, SEEK_SET) != 0)
		return (-1);
	return (0);
}

#endif	/* !defined(_LP64) && _FILE_OFFSET_BITS == 64 */
