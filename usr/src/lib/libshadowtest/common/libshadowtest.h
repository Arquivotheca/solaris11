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
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_LIBSHADOWTEST_H
#define	_LIBSHADOWTEST_H

#include <sys/vfs.h>
#include <sys/fs/shadow.h>

#ifdef	__cplusplus
extern "C" {
#endif

extern int st_init(void);
extern void st_fini(void);

extern int st_verify_pending(int, int, char **);
extern int st_verify_pending_empty(int, const char *);

extern int st_suspend(void);
extern int st_resume(void);
extern int st_rotate(const char *);
extern int st_spin(const char *, boolean_t);

extern int st_cred_set(void);
extern int st_cred_clear(void);

extern int st_get_fid(const char *, fid_t *);

extern int st_migrate_kthread(const char *);

extern int st_iter_space_map(const char *,
    void (*)(int, uint64_t, uint64_t, void *), void *);

#ifdef	__cplusplus
}
#endif

#endif	/* _LIBSHADOWTEST_H */
