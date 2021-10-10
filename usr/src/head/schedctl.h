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
 * Copyright (c) 1996, 2003, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_SCHEDCTL_H
#define	_SCHEDCTL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/schedctl.h>

typedef sc_public_t schedctl_t;

extern void yield(void);

#define	schedctl_start(p)					\
		(void) (((p) == NULL)? 0 :			\
		((((schedctl_t *)(p))->sc_nopreempt = 1), 0))

#define	schedctl_stop(p)					\
		(void) (((p) == NULL)? 0 :			\
		((((schedctl_t *)(p))->sc_nopreempt = 0),	\
		(((schedctl_t *)(p))->sc_yield? (yield(), 0) : 0)))

/*
 * libsched API
 */
#if	defined(__STDC__)
schedctl_t	*schedctl_init(void);
schedctl_t	*schedctl_lookup(void);
void		schedctl_exit(void);
#else
schedctl_t	*schedctl_init();
schedctl_t	*schedctl_lookup();
void		schedctl_exit();
#endif	/* __STDC__ */

#ifdef __cplusplus
}
#endif

#endif	/* _SCHEDCTL_H */
