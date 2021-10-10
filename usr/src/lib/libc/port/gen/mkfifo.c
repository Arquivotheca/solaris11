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
 * Copyright (c) 1989, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*
 * mkfifo(3c) - create a named pipe (FIFO). This code provides
 * a POSIX mkfifo function.
 */

#include "lint.h"
#include <sys/types.h>
#include <sys/stat.h>

int
mkfifoat(int fd, const char *path, mode_t mode)
{
	mode &= 0777;		/* only allow file access permissions */
	mode |= S_IFIFO;	/* creating a FIFO	*/
	return (mknodat(fd, path, mode, 0));
}

#pragma weak _mkfifo = mkfifo
int
mkfifo(const char *path, mode_t mode)
{
	mode &= 0777;		/* only allow file access permissions */
	mode |= S_IFIFO;	/* creating a FIFO	*/
	return (mknod(path, mode, 0));
}
