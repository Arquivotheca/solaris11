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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_VM_VMTASK_IMPL_H
#define	_VM_VMTASK_IMPL_H

#include <vm/vmtask.h>

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef	_KERNEL

/* Default value for maximum number of threads a single job will use */
#define	VMTASK_TASKS_MAX_DEFAULT	16

/* Default job balancing shift value */
#define	VMTASK_JOB_SHIFT_DEFAULT	2

/* Default duty cycle value */
#define	VMTASK_DC_DEFAULT		75

/* Helper process name */
#define	VMTASK_PNAME			"vmtasks"

#ifdef	DEBUG
#define	VMTASK_DEBUG	1
#endif

/*
 * If some tasks fail, the undo list records information so that the completed
 * index ranges can be undone.  The undo information is stored in a linked list
 * which is sorted based on undo_start_idx and the elements in the linked list
 * do not overlap.  The list contains pieces of jobs which were started but not
 * completed.  This is the logical approach for this since the list is only used
 * if there was an error and thus most of the time, the list should never need
 * to be used and will remain empty.  If it does need to be used, the last step
 * in creating the list is to add a node at the front which contains the start
 * and end index of all the work which was started.  Thus we can undo all of the
 * work which was successfully completed.
 */
typedef	struct vmtask_undo_impl {
	ulong_t			undo_start_idx;
	ulong_t			undo_end;
	struct vmtask_undo_impl	*undo_nextp;
#ifdef VMTASK_DEBUG
	/*
	 * Return values of all threads that failed are stored in this linked
	 * list for debugging purpose
	 */
	int			undo_rv;
#endif
} vmtask_undo_impl_t;

/* convert (vmtask_undo_t *) to (vmtask_undo_impl_t *) */
#define	UNDO_IMPLP(p)	((vmtask_undo_impl_t *)(*(p)))

/* convert (vmtask_undo_impl_t *) to (vmtask_undo_t) */
#define	TO_UNDO(p)	((vmtask_undo_t *)(p))

/*
 * This data is used internally by vmtask_* functions to perform synchronization
 * between tasks.
 * If any call to arg_job_func returns non-zero it is stored to arg_rv unless
 * this value is already not zero (changed by another thread already).
 */
typedef struct vmtask_arg {
	ulong_t			arg_next_idx;	/* next idx for new job chunk */
	ulong_t			arg_job_size;	/* total job size */
	ulong_t			arg_job_chunk;	/* recommended job chunk */
	uint_t			arg_ntasks;	/* number of tasks working */
	uint_t			arg_ntasks_fini; /* number of tasks finished */
	kmutex_t		arg_lock;	/* lock for struct elements */
	kcondvar_t		arg_cv;		/* cv for arg_ntasks_fini */
	vmtask_func_t		arg_job_func;	/* function that does job */
	void			*arg_func_arg;	/* argument for job_func */
	int			arg_rv;		/* job_func return value */
	vmtask_undo_impl_t	*arg_undop;	/* error undo data */
	boolean_t		arg_keep_undo;	/* store error undo flag */
	kthread_t		*arg_thread;	/* thread that requested job */
} vmtask_arg_t;

#ifdef	__cplusplus
}
#endif

#endif	/* _KERNEL */

#endif	/* _VM_VMTASK_IMPL_H */
