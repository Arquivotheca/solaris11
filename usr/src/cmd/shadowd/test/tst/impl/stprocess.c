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
 * Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Tests processing of the pending fid list.  This program will iterate calling
 * SHADOW_IOC_PROCESS until it thinks it's complete.
 */

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <sys/fs/shadow.h>

int
main(int argc, char **argv)
{
	int fd;
	shadow_ioc_t ioc;
	char path[PATH_MAX];

	bzero(&ioc, sizeof (ioc));
	ioc.si_buffer = (uint64_t)(uintptr_t)path;
	ioc.si_length = sizeof (path);

	if (argc != 2) {
		(void) fprintf(stderr, "usage: stprocess <root>\n");
		return (2);
	}

	if ((fd = open(argv[1], O_RDONLY)) < 0) {
		(void) fprintf(stderr, "failed to open %s: %s\n",
		    argv[1], strerror(errno));
		return (1);
	}

	for (;;) {
		if (ioctl(fd, SHADOW_IOC_PROCESS, &ioc) != 0) {
			(void) fprintf(stderr, "ioctl() failed: %s\n",
			    strerror(errno));
			return (1);
		}

		if (ioc.si_error) {
			path[PATH_MAX - 1] = '\0';
			(void) fprintf(stderr, "error %d on %s\n",
			    (int)ioc.si_error, path);
			return (1);
		}

		if (!ioc.si_processed)
			break;
	}

	(void) close(fd);
	return (0);
}
