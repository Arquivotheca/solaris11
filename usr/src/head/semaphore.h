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
 * Copyright (c) 1993, 2004, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _SEMAPHORE_H
#define	_SEMAPHORE_H

#include <sys/feature_tests.h>

#include <sys/types.h>
#include <sys/fcntl.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct {
	/* this structure must be the same as sema_t in <synch.h> */
	uint32_t	sem_count;	/* semaphore count */
	uint16_t	sem_type;
	uint16_t	sem_magic;
	upad64_t	sem_pad1[3];	/* reserved for a mutex_t */
	upad64_t 	sem_pad2[2];	/* reserved for a cond_t */
} sem_t;

#define	SEM_FAILED	((sem_t *)(-1))

/*
 * function prototypes
 */
#if	defined(__STDC__)
int	sem_init(sem_t *, int, unsigned int);
int	sem_destroy(sem_t *);
sem_t	*sem_open(const char *, int, ...);
int	sem_close(sem_t *);
int	sem_unlink(const char *);
int	sem_wait(sem_t *);
/*
 * Inclusion of <time.h> breaks X/Open and POSIX namespace.
 * The timespec structure while allowed in XPG6 and POSIX.1003d-1999,
 * is not permitted in prior POSIX or X/Open specifications even
 * though functions beginning with sem_* are allowed.
 */
#if !defined(__XOPEN_OR_POSIX) || defined(_XPG6) || defined(__EXTENSIONS__)
struct timespec;
int	sem_timedwait(sem_t *_RESTRICT_KYWD,
		const struct timespec *_RESTRICT_KYWD);
int	sem_reltimedwait_np(sem_t *_RESTRICT_KYWD,
		const struct timespec *_RESTRICT_KYWD);
#endif	/* !defined(__XOPEN_OR_POSIX) || defined(_XPG6) ... */
int	sem_trywait(sem_t *);
int	sem_post(sem_t *);
int	sem_getvalue(sem_t *_RESTRICT_KYWD, int *_RESTRICT_KYWD);
#else
int	sem_init();
int	sem_destroy();
sem_t	*sem_open();
int	sem_close();
int	sem_unlink();
int	sem_wait();
#if !defined(__XOPEN_OR_POSIX) || defined(_XPG6) || defined(__EXTENSIONS__)
int	sem_timedwait();
int	sem_reltimedwait_np();
#endif	/* #if !defined(__XOPEN_OR_POSIX) || defined(_XPG6) ... */
int	sem_trywait();
int	sem_post();
int	sem_getvalue();
#endif	/* __STDC__ */

#ifdef	__cplusplus
}
#endif

#endif	/* _SEMAPHORE_H */
