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
/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/


/*
 * Copyright (c) 1988, 2003, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_STROPTS_H
#define	_STROPTS_H

/*
 * Streams user options definitions.
 */

#include <sys/feature_tests.h>
#include <sys/stropts.h>

#ifdef	__cplusplus
extern "C" {
#endif

#if defined(__STDC__)

extern int isastream(int);

extern int getmsg(int, struct strbuf *_RESTRICT_KYWD,
		struct strbuf *_RESTRICT_KYWD, int *_RESTRICT_KYWD);
extern int putmsg(int, const struct strbuf *, const struct strbuf *, int);

extern int getpmsg(int, struct strbuf *_RESTRICT_KYWD,
		struct strbuf *_RESTRICT_KYWD, int *_RESTRICT_KYWD,
		int *_RESTRICT_KYWD);
extern int putpmsg(int, const struct strbuf *, const struct strbuf *, int, int);

/*
 * These three routines are duplicated in unistd.h; duplication necessitated
 * by XPG4.2 compliance/namespace issues.
 */
extern int ioctl(int, int, ...);
extern int fattach(int, const char *);
extern int fdetach(const char *);

#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _STROPTS_H */
