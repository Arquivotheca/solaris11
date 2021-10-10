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
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Monitor /etc/mnttab file to handle media device hotplug event.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <poll.h>

int
main(int argc, char **argv)
{
	int	fd;
	char	buf[256];
	struct pollfd	fds;
	id_t	pid;

	pid = 0;

	/* get caller pid for send signal */
	if (argc == 2) {
		pid = atol(argv[1]);
	} else {
		pid = getppid();
	}

	fd = open("/etc/mnttab", O_RDONLY);

	if (fd < 0) {
		(void) printf("error open mnttab.\n");
		return (1);
	}

	fds.fd = fd;
	fds.events = POLLRDBAND;
	fds.revents = 0;

	/* poll the file and skip existing content */
	while (poll(&fds, 1, 0) > 0) {
		(void) lseek(fd, 0, SEEK_SET);
		(void) read(fd, buf, 256);
	}

	for (;;) {
		/* if found mnttab changed, send signal to caller */
		(void) poll(&fds, 1, -1);
		(void) sleep(5);
		while (poll(&fds, 1, 0) > 0) {
			(void) lseek(fd, 0, SEEK_SET);
			(void) read(fd, buf, 256);
		}

		(void) sigsend(P_PID, pid, SIGUSR2);
	}
}
