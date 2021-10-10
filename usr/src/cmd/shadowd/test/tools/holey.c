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

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

int
main(int argc, char **argv)
{
	int fd;
	off64_t prev, offset;
	boolean_t ishole;
	struct stat64 statbuf;

	if (argc != 2) {
		(void) fprintf(stderr, "usage: holey <file>\n");
		return (2);
	}

	if ((fd = open64(argv[1], O_RDONLY)) < 0) {
		(void) fprintf(stderr, "failed to open %s\n", argv[1]);
		return (1);
	}

	if (fstat64(fd, &statbuf) != 0) {
		(void) fprintf(stderr, "failed to stat %s\n", argv[1]);
		return (1);
	}

	ishole = B_TRUE;
	prev = 0;
	for (;;) {
		offset = lseek64(fd, prev, ishole ? SEEK_DATA : SEEK_HOLE);
		if (offset < 0)
			offset = lseek(fd, 0, SEEK_END);
		if (offset < 0) {
			(void) fprintf(stderr, "lseek() failed\n");
			return (1);
		}

		if (prev != offset) {
			(void) printf("%s %16llx %16llx\n",
			    ishole ? "HOLE" : "DATA", prev, offset);
		}

		ishole = !ishole;
		prev = offset;
		if (offset == statbuf.st_size)
			break;
	}

	(void) close(fd);
	return (0);
}
