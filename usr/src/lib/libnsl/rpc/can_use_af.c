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
 * Copyright (c) 1999, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include "mt.h"
#include "rpc_mt.h"
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <net/if.h>
#include <sys/sockio.h>

/*
 * Determine if any network interface is present that supports the specified
 * address family, af. The status of the interface is not important.
 *
 * getifaddrs(3SOCKET) could be used to replace the SIOCGLIFNUM ioctl()
 * call being used, however, SIOCFGLIFNUM is the right choice because of
 * its speed and simplicity. Given this function's purpose, using
 * getifaddrs() would be excessive.
 */
int
__can_use_af(sa_family_t af)
{
	struct lifnum	lifn;
	int		fd;

	if ((fd = open("/dev/udp", O_RDONLY)) < 0) {
		return (0);
	}
	lifn.lifn_family = af;
	lifn.lifn_flags = 0;
	if (ioctl(fd, SIOCGLIFNUM, &lifn, sizeof (lifn)) < 0) {
		lifn.lifn_count = 0;
	}

	(void) close(fd);
	return (lifn.lifn_count);
}
