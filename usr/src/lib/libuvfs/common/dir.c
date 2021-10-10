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
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */


#include <stdio.h>
#include <strings.h>
#include <fcntl.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/dirent.h>
#include <libuvfs_impl.h>

/*
 * This comes from sys/dirent.h, but is wrapped around ifdef _KERNEL
 * need to fix that, but need to be cautious to not expose it when
 * we aren't suppose to or we may break a header issue with one of the
 * POSIX tests, such as vsx?
 */
#define	DIRENT64_RECLEN(namelen)	\
	((offsetof(dirent64_t, d_name[0]) + 1 + (namelen) + 7) & ~ 7)
#define	DIRENT64_NAMELEN(reclen)	\
	((reclen) - (offsetof(dirent64_t, d_name[0])))

size_t
libuvfs_add_direntry(void *data_buf, size_t buflen, const char *name,
    ino64_t ino, off64_t off, void **cookie)
{
	dirent64_t *dp;
	int reclen;

	/*
	 * Remember to add edirent_t support.
	 * need to fix sys/extdirent.h to allow it to be included in
	 * user space
	 */
	if (*cookie == NULL) {
		dp = (dirent64_t *)data_buf;
		*cookie = (void *)dp;
	} else
		dp = (dirent64_t *)*cookie;

	reclen = DIRENT64_RECLEN(strlen(name));

	if (((char *)*cookie + reclen) > ((char *)data_buf + buflen))
		return (0);
	*cookie = (void **)((uintptr_t)*cookie + reclen);
	dp->d_ino = ino;
	dp->d_reclen = reclen;
	dp->d_off = (off == -1) ?  (uintptr_t)*cookie - (uintptr_t)data_buf :
	    off;
	(void) strlcpy(dp->d_name, name, DIRENT64_NAMELEN(reclen));
	return (reclen);
}
