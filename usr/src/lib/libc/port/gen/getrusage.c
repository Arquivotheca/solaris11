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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*	Copyright (c) 1983, 1984, 1985, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*
 * Portions of this source code were derived from Berkeley 4.3 BSD
 * under license from the Regents of the University of California.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * Compatibility lib for BSD's getrusgae(). Only the
 * CPU time usage is supported for RUSAGE_CHILDREN, and hence does not
 * fully support BSD's rusage semantics.
 */

#include "lint.h"
#include <sys/types.h>
#include <sys/time.h>
#include <sys/times.h>
#include <stdio.h>
#include <sys/param.h>
#include <errno.h>
#include <sys/resource.h>
#include <sys/vm_usage.h>
#include <fcntl.h>
#include <sys/fcntl.h>
#include <procfs.h>
#include <string.h>
#include <unistd.h>

int __rusagesys(int, struct rusage *);

int
getrusage(int who, struct rusage *rusage)
{
	switch (who) {

	case RUSAGE_SELF:
		return (__rusagesys(_RUSAGESYS_GETRUSAGE, rusage));

	case RUSAGE_LWP:
		return (__rusagesys(_RUSAGESYS_GETRUSAGE_LWP, rusage));

	case RUSAGE_CHILDREN:
		return (__rusagesys(_RUSAGESYS_GETRUSAGE_CHLD, rusage));

	default:
		errno = EINVAL;
		return (-1);
	}
}

int
getvmusage(uint_t flags, time_t age, vmusage_t *buf, size_t *nres)
{
	return (syscall(SYS_rusagesys, _RUSAGESYS_GETVMUSAGE, flags, age,
	    buf, nres));
}
