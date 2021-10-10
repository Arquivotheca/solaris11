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

#ifndef _SYS_UVFS_H
#define	_SYS_UVFS_H

#include <sys/uvfs_vfsops.h>
#include <sys/uvfs_uvnode.h>
#include <sys/door.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	UVFS_MIN_MINOR	(12)

extern int sys_shutdown;

int uvfs_root(vfs_t *, vnode_t **);
int uvfs_door_wait(uvfsvfs_t *, uint64_t);
int uvfs_update_setid(uvnode_t *, cred_t *cr);
void uvfs_update_attrs(uvnode_t *, libuvfs_stat_t *);
void uvfs_task_sync(uvfsvfs_t *, cred_t *cr);
void uvfs_task_rootvp(uvfsvfs_t *, cred_t *cr);
int uvfs_task_upcall(uvfsvfs_t *, door_handle_t, door_arg_t *,
    cred_t *, size_t);
void uvfs_task_uvfsvfs_alloc(uvfsvfs_t *);
void uvfs_task_uvfsvfs_free(uvfsvfs_t *);
void uvfs_task_uvfsvfs_free_wait(uvfsvfs_t *);
void uvfs_task_init(void);
void uvfs_task_fini(void);
int uvfs_write_uio_final(int, uio_t *, int, offset_t, int);

/* tasks */

void uvfs_task_inactive(uvfsvfs_t *, const libuvfs_fid_t *, cred_t *);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_UVFS_H */
