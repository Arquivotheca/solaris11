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

/*
 * Async tasks for uvfs.
 *
 * N.B.: Tasks must not have a hold on the VFS; i.e., they must not ever
 * call VFS_RELE().  If they do, then VFS_RELE() on the last reference will
 * end up calling uvfsvfs_free(), which does a taskq_wait() on our task
 * queue, so we have a task waiting on itself to finish.  The taskq_wait()
 * in uvfsvfs_free() eliminates the need for the tasks to hold the VFS.
 */

#include <sys/libuvfs_ki.h>
#include <sys/uvfs.h>

#include <sys/uvfs_task.h>
#include <sys/uvfs_upcall.h>
#include <sys/fs/uvfs.h>
#include <sys/uvfs_vfsops.h>
#include <sys/uvfs_uvnode.h>

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysmacros.h>
#include <sys/kmem.h>
#include <sys/taskq.h>
#include <sys/cmn_err.h>
#include <sys/dnlc.h>

static kmem_cache_t *uvfs_task_sync_cache;
static kmem_cache_t *uvfs_task_rootvp_cache;
static kmem_cache_t *uvfs_task_upcall_cache;

int uvfs_taskq_nthreads = UVFS_TASKQ_NTHREADS;
pri_t uvfs_taskq_pri = UVFS_TASKQ_PRI;
int uvfs_taskq_minalloc = UVFS_TASKQ_MINALLOC;
int uvfs_taskq_maxalloc = UVFS_TASKQ_MAXALLOC;
uint_t uvfs_taskq_flags = UVFS_TASKQ_FLAGS;

static uvfs_task_sync_t *
uvfs_task_sync_alloc(uvfsvfs_t *uvfsvfs, cred_t *cr)
{
	uvfs_task_sync_t *task;

	task = kmem_cache_alloc(uvfs_task_sync_cache, KM_SLEEP);

	crhold(cr);

	task->sync_uvfsvfs = uvfsvfs;
	task->sync_cred = cr;

	return (task);
}

static void
uvfs_task_sync_free(uvfs_task_sync_t *task)
{
	crfree(task->sync_cred);

	kmem_cache_free(uvfs_task_sync_cache, task);
}

static void
uvfs_task_sync_task(void *vtask)
{
	uvfs_task_sync_t *task = vtask;
	uvfsvfs_t *uvfsp = task->sync_uvfsvfs;
	uvnode_t *next_uvp;
	uvnode_t *curr_uvp;

	/*
	 * Traverse the per-fs uvnode list looking for
	 * data that needs to be synced.  We cannot push
	 * dirty pages (i.e., call pvn_vplist_dirty()) while
	 * holding uvfs_uvnodes_lock or deadlocks occur.
	 */
	mutex_enter(&uvfsp->uvfs_uvnodes_lock);
	curr_uvp = list_head(&uvfsp->uvfs_uvnodes);
	if (curr_uvp != NULL)
		VN_HOLD(UVTOV(curr_uvp));
	mutex_exit(&uvfsp->uvfs_uvnodes_lock);

	while (curr_uvp != NULL) {
		(void) pvn_vplist_dirty(UVTOV(curr_uvp), 0, uvfs_putapage,
		    B_ASYNC, task->sync_cred);

		mutex_enter(&uvfsp->uvfs_uvnodes_lock);
		next_uvp = list_next(&uvfsp->uvfs_uvnodes, curr_uvp);
		if (next_uvp != NULL)
			VN_HOLD(UVTOV(next_uvp));
		mutex_exit(&uvfsp->uvfs_uvnodes_lock);

		VN_RELE(UVTOV(curr_uvp));

		curr_uvp = next_uvp;
	}

	uvfs_task_sync_free(task);
}

void
uvfs_task_sync(uvfsvfs_t *uvfsvfs, cred_t *cr)
{
	uvfs_task_sync_t *task;

	/*
	 * If unmounted or shutdown is in progress, don't bother trying
	 * the upcall, since it will fail.
	 */
	if (uvfsvfs->uvfs_flags & (UVFS_UNMOUNTED | UVFS_SHUTDOWN))
		return;

	task = uvfs_task_sync_alloc(uvfsvfs, cr);

	(void) taskq_dispatch(uvfsvfs->uvfs_taskq, uvfs_task_sync_task,
	    task, TQ_SLEEP);
}

static uvfs_task_upcall_t *
uvfs_task_upcall_alloc(uvfsvfs_t *uvfsvfs, door_handle_t dh,
    door_arg_t *door_args, cred_t *cr, size_t maxsize)
{
	uvfs_task_upcall_t *task;

	task = kmem_cache_alloc(uvfs_task_upcall_cache, KM_SLEEP);

	crhold(cr);

	task->up_uvfsvfs = uvfsvfs;
	task->up_cr = cr;
	task->up_dh = dh;
	task->up_args = door_args;
	task->up_maxsize = maxsize;
	mutex_init(&task->up_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&task->up_cv, NULL, CV_DEFAULT, NULL);

	return (task);
}

static void
uvfs_task_upcall_free(uvfs_task_upcall_t *task)
{
	crfree(task->up_cr);

	kmem_cache_free(uvfs_task_upcall_cache, task);
}

void
uvfs_task_upcall_task(void *task)
{
	uvfs_task_upcall_t *uptask = task;

	mutex_enter(&uptask->up_lock);
	uptask->up_error = door_ki_upcall_limited(uptask->up_dh,
	    uptask->up_args, uptask->up_cr, uptask->up_maxsize, 0);
	cv_broadcast(&uptask->up_cv);
	mutex_exit(&uptask->up_lock);
}

int
uvfs_task_upcall(uvfsvfs_t *uvfsvfs, door_handle_t dh, door_arg_t *door_args,
    cred_t *cr, size_t maxsize)
{
	int error;
	uvfs_task_upcall_t *task;

	/*
	 * This is not a true async task.
	 * We are dispatching it and then wait for a result.
	 * This is used when we need to retry an upcall, but we can't
	 * use the users thread due to pending signal.
	 *
	 * The arguments passed to the other thread are in the calling threads
	 * stack.
	 */
	task = uvfs_task_upcall_alloc(uvfsvfs, dh, door_args, cr, maxsize);
	mutex_enter(&task->up_lock);
	(void) taskq_dispatch(uvfsvfs->uvfs_taskq, uvfs_task_upcall_task,
	    task, TQ_SLEEP);
	cv_wait(&task->up_cv, &task->up_lock);
	error = task->up_error;
	mutex_exit(&task->up_lock);
	uvfs_task_upcall_free(task);
	return (error);
}

static uvfs_task_rootvp_t *
uvfs_task_rootvp_alloc(uvfsvfs_t *uvfsvfs, cred_t *cr)
{
	uvfs_task_rootvp_t *task;

	task = kmem_cache_alloc(uvfs_task_rootvp_cache, KM_SLEEP);

	crhold(cr);

	task->rvp_uvfsvfs = uvfsvfs;
	task->rvp_cred = cr;

	return (task);
}

static void
uvfs_task_rootvp_free(uvfs_task_rootvp_t *task)
{
	crfree(task->rvp_cred);

	kmem_cache_free(uvfs_task_rootvp_cache, task);
}

static void
uvfs_task_rootvp_task(void *vtask)
{
	uvfs_task_rootvp_t *task = vtask;
	uvfsvfs_t *uvfsvfs = task->rvp_uvfsvfs;
	libuvfs_stat_t stat = { 0 };
	vnode_t *vp, *old;
	boolean_t match;
	uvnode_t *uvp;
	vfs_t *vfsp;
	int error;

	/*
	 * Make the upcall to get the fid for the root.
	 */
	error = uvfs_up_vfsroot(task->rvp_uvfsvfs, &stat, task->rvp_cred);
	if (error != 0) {
		cmn_err(CE_WARN,  "failed vfsroot upcall: %d", error);
		goto done;
	}

	/*
	 * Check the current rootvp against the fid we just retrieved.  If
	 * it matches, we're done.
	 */
	mutex_enter(&uvfsvfs->uvfs_lock);
	uvp = uvfsvfs->uvfs_rootvp->v_data;
	vfsp = uvfsvfs->uvfs_vfsp;
	match = uvfs_fid_match(uvp, vfsp, &uvfsvfs->uvfs_root_fid);
	mutex_exit(&uvfsvfs->uvfs_lock);
	if (match) {
		mutex_enter(&uvfsvfs->uvfs_lock);
		uvfsvfs->uvfs_flags |= UVFS_FLAG_ROOTVP;
		mutex_exit(&uvfsvfs->uvfs_lock);
		goto done;
	}

	/*
	 * We didn't match, so we need a new rootvp.  Create (or find) it,
	 * put it in the uvfsvfs, and release the old rootvp.
	 */
	error = uvfs_uvget(uvfsvfs->uvfs_vfsp, &uvp, &uvfsvfs->uvfs_root_fid,
	    &stat);
	if (error != 0) {
		cmn_err(CE_WARN,  "failed to create rootvp: %d", error);
		goto done;
	}

	vp = uvp->uv_vnode;
	mutex_enter(&vp->v_lock);
	vp->v_flag |= VROOT;
	mutex_exit(&vp->v_lock);

	mutex_enter(&uvfsvfs->uvfs_lock);
	old = uvfsvfs->uvfs_rootvp;
	uvfsvfs->uvfs_rootvp = vp;
	uvfsvfs->uvfs_flags |= UVFS_FLAG_ROOTVP;
	mutex_exit(&uvfsvfs->uvfs_lock);

	dnlc_purge_vp(old);
	VN_RELE(old);

done:
	uvfs_task_rootvp_free(task);
}

void
uvfs_task_rootvp(uvfsvfs_t *uvfsvfs, cred_t *cr)
{
	uvfs_task_rootvp_t *task;

	/*
	 * If unmounted or shutdown is in progress, don't bother trying
	 * the upcall, since it will fail.
	 */

	if (uvfsvfs->uvfs_flags & (UVFS_UNMOUNTED | UVFS_SHUTDOWN))
		return;

	task = uvfs_task_rootvp_alloc(uvfsvfs, cr);

	(void) taskq_dispatch(uvfsvfs->uvfs_taskq, uvfs_task_rootvp_task,
	    task, TQ_SLEEP);
}

void
uvfs_task_uvfsvfs_alloc(uvfsvfs_t *uvfsvfs)
{
	uvfsvfs->uvfs_taskq = taskq_create("uvfs_taskq", uvfs_taskq_nthreads,
	    uvfs_taskq_pri, uvfs_taskq_minalloc, uvfs_taskq_maxalloc,
	    uvfs_taskq_flags);
}

void
uvfs_task_uvfsvfs_free_wait(uvfsvfs_t *uvfsvfs)
{
	taskq_wait(uvfsvfs->uvfs_taskq);
}

void
uvfs_task_uvfsvfs_free(uvfsvfs_t *uvfsvfs)
{
	taskq_destroy(uvfsvfs->uvfs_taskq);
}

void
uvfs_task_init(void)
{
	uvfs_task_sync_cache = kmem_cache_create("uvfs_task_sync_cache",
	    sizeof (uvfs_task_sync_t), 0, NULL, NULL, NULL, NULL, NULL, 0);
	uvfs_task_rootvp_cache = kmem_cache_create("uvfs_task_rootvp_cache",
	    sizeof (uvfs_task_rootvp_t), 0, NULL, NULL, NULL, NULL, NULL, 0);
	uvfs_task_upcall_cache = kmem_cache_create("uvfs_task_upcall_cache",
	    sizeof (uvfs_task_upcall_t), 0, NULL, NULL, NULL, NULL, NULL, 0);
}

void
uvfs_task_fini(void)
{
	kmem_cache_destroy(uvfs_task_rootvp_cache);
	kmem_cache_destroy(uvfs_task_sync_cache);
	kmem_cache_destroy(uvfs_task_upcall_cache);
}
