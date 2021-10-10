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
 * Creates a socket in the filesystem namespace.
 */

#include <door.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stropts.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

/*ARGSUSED*/
static void
server(void *cookie, char *args, size_t alen, door_desc_t *dp,
    uint_t n_desc)
{
}

int
main(int argc, char **argv)
{
	int fd;

	if (argc != 2) {
		(void) fprintf(stderr, "usage: stdoor <file>\n");
		return (2);
	}

	if ((fd = open(argv[1], O_RDWR | O_CREAT, 0700)) < 0) {
		(void) fprintf(stderr, "failed to create file: %s\n",
		    strerror(errno));
		return (1);
	}

	if ((fd = door_create(server, NULL,
	    DOOR_UNREF | DOOR_REFUSE_DESC | DOOR_NO_CANCEL)) < 0) {
		(void) fprintf(stderr, "failed to create door: %s\n",
		    strerror(errno));
		return (1);
	}

	(void) fdetach(argv[1]);
	if (fattach(fd, argv[1]) != 0) {
		(void) fprintf(stderr, "failed to attach descriptor: %s\n",
		    strerror(errno));
		return (1);
	}

	return (0);
}
