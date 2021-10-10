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
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#pragma weak _poll = poll

#include "lint.h"
#include <sys/time.h>
#include <sys/poll.h>
#include "libc.h"

int
ppoll(struct pollfd *_RESTRICT_KYWD fds, nfds_t nfd,
    const struct timespec *_RESTRICT_KYWD tsp,
    const sigset_t *_RESTRICT_KYWD sigmask)
{
	return (_pollsys(fds, nfd, tsp, sigmask));
}

int
poll(struct pollfd *fds, nfds_t nfd, int timeout)
{
	timespec_t ts;
	timespec_t *tsp;

	if (timeout < 0)
		tsp = NULL;
	else {
		ts.tv_sec = timeout / MILLISEC;
		ts.tv_nsec = (timeout % MILLISEC) * MICROSEC;
		tsp = &ts;
	}

	return (_pollsys(fds, nfd, tsp, NULL));
}
