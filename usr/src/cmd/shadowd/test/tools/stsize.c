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
 * Given a file, process the entire file via the SHADOW_IOC_MIGRATE ioctl() and
 * print the resulting size to stdout.
 */

#include <fcntl.h>
#include <stdio.h>
#include <strings.h>
#include <sys/fs/shadow.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
	int fd;
	shadow_ioc_t ioc;

	if (argc != 2) {
		(void) fprintf(stderr, "usage: stsize <file>\n");
		return (2);
	}

	if ((fd = open(argv[1], O_RDONLY)) < 0) {
		(void) fprintf(stderr, "failed to open %s\n", argv[1]);
		return (1);
	}

	bzero(&ioc, sizeof (ioc));
	if (ioctl(fd, SHADOW_IOC_MIGRATE, &ioc) != 0) {
		(void) fprintf(stderr, "migration failed\n");
		return (1);
	}

	(void) close(fd);

	(void) printf("%llu\n", ioc.si_size);
	return (0);
}
