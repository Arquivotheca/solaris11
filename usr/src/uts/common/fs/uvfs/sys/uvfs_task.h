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

#ifndef _SYS_UVFS_TASK_H
#define	_SYS_UVFS_TASK_H

#include <sys/uvfs.h>
#include <sys/taskq.h>
#include <sys/disp.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	UVFS_TASKQ_NTHREADS	(2)
#define	UVFS_TASKQ_PRI		(MINCLSYSPRI)
#define	UVFS_TASKQ_MINALLOC	(0)
#define	UVFS_TASKQ_MAXALLOC	(2)
#define	UVFS_TASKQ_FLAGS	(0)

typedef struct {
	uvfsvfs_t *sync_uvfsvfs;
	cred_t *sync_cred;
} uvfs_task_sync_t;

typedef struct {
	uvfsvfs_t *rvp_uvfsvfs;
	cred_t *rvp_cred;
} uvfs_task_rootvp_t;

typedef struct {
	uvfsvfs_t *up_uvfsvfs;
	door_arg_t *up_args;
	cred_t *up_cr;
	door_handle_t up_dh;
	size_t	up_maxsize;
	kcondvar_t up_cv;
	kmutex_t up_lock;
	int	up_error;
} uvfs_task_upcall_t;

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_UVFS_TASK_H */
