/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 * Copyright 2000-2002 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_IKED_COMMON_LOCK_H
#define	_IKED_COMMON_LOCK_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifdef	__cplusplus
extern "C" {
#endif

#include <thread.h>
#include <synch.h>

/*
 * Door lock: used to force essentially single-threaded behavior,
 * despite the use of a door for admin!  This lock protects the
 * callbacks in the event loop, as well as the door function(s).
 * Thus door calls will only be processed "between" events.
 *
 * The lock is actually defined in libike.so.1 where it is used to protect the
 * callbacks that handle io in the event loop.
 */
extern mutex_t	door_lock;

#ifdef	__cplusplus
}
#endif

#endif	/* _IKED_COMMON_LOCK_H */
