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
 * Copyright (c) 1993, 2008, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _SYS_RWLOCK_H
#define	_SYS_RWLOCK_H

/*
 * Public interface to readers/writer locks.  See rwlock(9F) for details.
 */

#include <sys/types.h>

#ifdef	__cplusplus
extern "C" {
#endif

#ifndef _ASM

typedef enum {
	RW_DRIVER = 2,		/* driver (DDI) rwlock */
	RW_DEFAULT = 4		/* kernel default rwlock */
} krw_type_t;

typedef enum {
	RW_WRITER,
	RW_READER
} krw_t;

typedef struct _krwlock {
	void	*_opaque[1];
} krwlock_t;

#if defined(_KERNEL)

#define	RW_READ_HELD(x)		(rw_read_held((x)))
#define	RW_WRITE_HELD(x)	(rw_write_held((x)))
#define	RW_LOCK_HELD(x)		(rw_lock_held((x)))
#define	RW_ISWRITER(x)		(rw_iswriter(x))

extern	void	rw_init(krwlock_t *, char *, krw_type_t, void *);
extern	void	rw_destroy(krwlock_t *);
extern	void	rw_enter(krwlock_t *, krw_t);
extern	int	rw_tryenter(krwlock_t *, krw_t);
extern	void	rw_exit(krwlock_t *);
extern	void	rw_downgrade(krwlock_t *);
extern	int	rw_tryupgrade(krwlock_t *);
extern	int	rw_read_held(krwlock_t *);
extern	int	rw_write_held(krwlock_t *);
extern	int	rw_lock_held(krwlock_t *);
extern	int	rw_read_locked(krwlock_t *);
extern	int	rw_iswriter(krwlock_t *);
extern	struct _kthread *rw_owner(krwlock_t *);
extern	uint_t (*rw_lock_backoff)(uint_t);
extern	void (*rw_lock_delay)(uint_t);

#endif	/* defined(_KERNEL) */

#endif	/* _ASM */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_RWLOCK_H */
