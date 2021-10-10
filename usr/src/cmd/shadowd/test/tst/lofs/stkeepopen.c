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
#include <string.h>
#include <errno.h>

int
main(int argc, char **argv)
{
	int fd;

	if (argc != 2) {
		(void) printf("usage: stkeepopen <file>\n");
		return (2);
	}

	if ((fd = open(argv[1], O_RDONLY)) < 0) {
		(void) printf("failed to open '%s': %s\n", argv[1],
		    strerror(errno));
		return (1);
	}

	for (;;)
		(void) sleep(10);

	/*NOTREACHED*/
	(void) close(fd);
	return (0);
}
