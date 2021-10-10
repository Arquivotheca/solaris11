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

#include <sys/types.h>
#include <sys/debug.h>
#include <sys/sysmacros.h>
#include <sys/cpuvar.h>
#include <sys/kmem.h>
#include <sys/systm.h>
#include <sys/cmn_err.h>
#include <sys/taskq.h>
#include <sys/lgrp.h>

#include <vm/vmtask.h>
#include <vm/vmtask_impl.h>

/* Maximum number of tasks to dispatch */
uint_t vmtask_ntasks_max = VMTASK_TASKS_MAX_DEFAULT;

/*
 * Some job pieces may take longer to execute than others, so divide the work
 * into more pieces than tasks to achieve dynamic load balancing.  Reduce the
 * size of each job chunk by (1 << vmtask_job_shift).
 */
uint_t vmtask_job_shift = VMTASK_JOB_SHIFT_DEFAULT;

/* If non zero, vmtasks process is not started during boot */
uint_t vmtask_disabled = 0;

/* Duty cycle for vmtask threads */
uint_t vmtask_dc = VMTASK_DC_DEFAULT;

/* task queue with SDC scheduler */
static taskq_t *vmtask_proc_taskq = NULL;

/*
 * Each task takes a piece of job (job_chunk) and works on it. If an error
 * occurs it stores the information about it so error undo mechanism can
 * rollback.
 */
static void
vmtask_task(void *task_arg)
{
	vmtask_arg_t *arg = (vmtask_arg_t *)task_arg;
	ulong_t	start_idx, end, ridx;
	int	rv = 0;
	vmtask_undo_impl_t	*undop = NULL;

	curthread->t_vmtask_user = arg->arg_thread;
	while (rv == 0) {
		/*
		 * Execute the next piece of work starting at arg_next_idx. Quit
		 * if any task hits an error and sets arg_rv, or when all
		 * arg_job_size units of work are complete.
		 */
		mutex_enter(&arg->arg_lock);
		start_idx = arg->arg_next_idx;
		if (arg->arg_rv == 0 && start_idx < arg->arg_job_size) {
			end = start_idx + arg->arg_job_chunk;
			if (end > arg->arg_job_size)
				end = arg->arg_job_size;
			arg->arg_next_idx = end;
		} else {
			end = 0;
		}
		mutex_exit(&arg->arg_lock);

		if (end == 0)
			break;
		rv = arg->arg_job_func(start_idx, end, arg->arg_func_arg,
		    &ridx);
	}
	curthread->t_vmtask_user = NULL;

	/*
	 * If there was an error, and we did not finish the full job, check if
	 * undo information is needed to be kept. If so, allocate undo object
	 * where we insert indexes of work we could not complete.
	 */
	if (rv != 0 && arg->arg_keep_undo == B_TRUE && ridx < end) {
		undop = kmem_alloc(sizeof (vmtask_undo_impl_t), KM_SLEEP);
		undop->undo_start_idx = ridx;
		undop->undo_end = end;
		undop->undo_nextp = NULL;
#ifdef	VMTASK_DEBUG
		undop->undo_rv = rv;
#endif
	}

	mutex_enter(&arg->arg_lock);
	if (rv != 0 && arg->arg_rv == 0)
		arg->arg_rv = rv;
	if (undop != NULL) {
		vmtask_undo_impl_t **undopp = &arg->arg_undop;

		/* Insert undo object into sorted list */
		while (*undopp != NULL && (*undopp)->undo_start_idx < ridx) {
			undopp = &(*undopp)->undo_nextp;
		}
		undop->undo_nextp = *undopp;
		*undopp = undop;
	}

	/* If we are the last to finish notify the main thread */
	if (++arg->arg_ntasks_fini == arg->arg_ntasks)
		cv_signal(&arg->arg_cv);
	mutex_exit(&arg->arg_lock);
}

/*
 * Return number of tasks.
 */
static uint_t
vmtask_getntasks(ulong_t job_size, ulong_t job_chunk_min)
{
	/*
	 * If vmtasks is disabled, or helper process is not running always
	 * return 1.
	 */
	if (vmtask_proc_taskq == NULL || vmtask_disabled) {
		return (1);
	} else {
		/*
		 * Number of tasks is equal either to ncpus_online or
		 * vmtask_ntasks_max whichever is smaller.  However, if job_size
		 * can be split only into smaller pieces of work, we reduce
		 * number of tasks to that value.
		 */
		uint_t oncpus = ncpus_online;
		uint_t ntasks_max = MIN(oncpus, vmtask_ntasks_max);
		uint_t ntasks = (job_size + job_chunk_min - 1) / job_chunk_min;

		return (MIN(ntasks, ntasks_max));
	}
}

/*
 * Get the size of chunk. If there is only one task, then job chunk is equal to
 * job size, otherwise it is a multiple of job_chunk_min.
 */
static ulong_t
vmtask_getchunk(ulong_t job_size, uint_t ntasks, ulong_t job_chunk_min)
{
	ASSERT(ntasks != 0);

	if (ntasks == 1) {
		return (job_size);
	} else {
		/*
		 * Reduce job_chunk to better balance the work load. However,
		 * avoid balancing if job_chunk turns out smaller than
		 * job_chunk_min.
		 */
		ulong_t job_chunk = (job_size / ntasks) >> vmtask_job_shift;

		ASSERT(job_chunk_min != 0);

		/* Make sure it is multiple of job_chunk_min */
		job_chunk -= (job_chunk % job_chunk_min);

		return (MAX(job_chunk, job_chunk_min));
	}
}

/*
 * Start parallel tasks for a provided job and wait for them to complete
 * job_size		specifies the total amount of work that will be split
 *			among tasks.
 *
 * job_chunk_min	The amount of work a single thread will do will be a
 *			multiple of this value.
 *
 * job_func		the function that performs a task in a given range
 *
 * arg			argument block for a task
 *
 * undop		an optional argument for an undo operation. If an
 *			error has occurred, and an undo needs to be performed,
 *			this argument will be populated with the needed data to
 *			perform the undo operation only on the ranges in which
 *			the job was successfully performed.
 */
int
vmtask_run_job(ulong_t job_size, ulong_t job_chunk_min, vmtask_func_t job_func,
    void *func_arg, vmtask_undo_t *undop)
{
	vmtask_arg_t	arg;
	uint_t		ntasks = vmtask_getntasks(job_size, job_chunk_min);

	/* Return success if there is no job to do. */
	if (job_size == 0)
		return (0);

	/* Initialize arg, which is a shared object between tasks */
	mutex_init(&arg.arg_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&arg.arg_cv, NULL, CV_DRIVER, NULL);
	arg.arg_job_size	= job_size;
	arg.arg_job_func	= job_func;
	arg.arg_func_arg	= func_arg;
	arg.arg_ntasks		= ntasks;
	arg.arg_next_idx	= 0;
	arg.arg_ntasks_fini	= 0;
	arg.arg_rv		= 0;
	arg.arg_undop		= NULL;
	arg.arg_keep_undo	= undop == NULL ? B_FALSE : B_TRUE;
	arg.arg_job_chunk	= vmtask_getchunk(job_size, ntasks,
	    job_chunk_min);
	if (lgrp_optimizations())
		arg.arg_thread = curthread;
	else
		arg.arg_thread = NULL;

	ASSERT(arg.arg_job_chunk >= job_chunk_min || ntasks == 1);
	IMPLY(ntasks > 1, vmtask_proc_taskq != NULL);

	/* Start tasks, with the last one executing by current thread */
	while (--ntasks) {
		(void) taskq_dispatch(vmtask_proc_taskq, vmtask_task,
		    (void *)&arg, TQ_SLEEP);
	}

	vmtask_task(&arg);

	/* Wait until every task is finished */
	mutex_enter(&arg.arg_lock);
	while (arg.arg_ntasks_fini < arg.arg_ntasks) {
		cv_wait(&arg.arg_cv, &arg.arg_lock);
	}
	mutex_exit(&arg.arg_lock);

	cv_destroy(&arg.arg_cv);
	mutex_destroy(&arg.arg_lock);

	/* Store the error undo information */
	if (undop != NULL && arg.arg_rv != 0) {
		vmtask_undo_impl_t *up = kmem_alloc(sizeof (vmtask_undo_impl_t),
		    KM_SLEEP);

		up->undo_nextp = arg.arg_undop;
		up->undo_start_idx = 0;
		up->undo_end = arg.arg_next_idx;
		*undop = TO_UNDO(up);
	}

	return (arg.arg_rv);
}

/*
 * Perform an error undo operation. It uses information in undop to
 * undo all of the completed jobs and portions of jobs. All the dynamically
 * allocated data by vmtask_* interface for error undo is cleaned here.
 */
void
vmtask_undo(vmtask_undo_t *undop, vmtask_func_t undo_func, void *func_arg)
{
	vmtask_undo_impl_t	*up = UNDO_IMPLP(undop);
	vmtask_undo_impl_t	*nextp = up->undo_nextp;
	ulong_t			idx = up->undo_start_idx;
	ulong_t			end = up->undo_end;
	ulong_t			tend, tidx, ignore;

	/*
	 * Free the first item in undo list, which contains lower and upper
	 * indexes of performed job, we already saved this information in idx
	 * and end.
	 */
	kmem_free(UNDO_IMPLP(undop), sizeof (vmtask_undo_impl_t));

	/*
	 * Call undo_func() for each region between idx and end skipping holes
	 * specified by members in undo list.
	 */
	while (idx < end) {
		if (nextp != NULL) {
			tend = nextp->undo_start_idx;
			tidx = nextp->undo_end;
			up = nextp;
			nextp = nextp->undo_nextp;
			kmem_free(up, sizeof (vmtask_undo_impl_t));
		} else {
			tidx = tend = end;
		}
		undo_func(idx, tend, func_arg, &ignore);
		idx = tidx;
	}
}

static void
vmtask_pinit()
{
	proc_t		*p = curproc;
	user_t		*pu = PTOU(curproc);

	p->p_cstime = p->p_stime = p->p_cutime = p->p_utime = 0;

	ASSERT(curproc != &p0);
	ASSERT(vmtask_proc_taskq == NULL);

	(void) strncpy(pu->u_psargs, VMTASK_PNAME, sizeof (pu->u_psargs));
	(void) strncpy(pu->u_comm, VMTASK_PNAME, sizeof (pu->u_comm));

	vmtask_proc_taskq = taskq_create_sysdc(VMTASK_PNAME, 100, 32,
	    INT_MAX, curproc, vmtask_dc, TASKQ_PREPOPULATE |
	    TASKQ_THREADS_CPU_PCT);

	mutex_enter(&p->p_lock);
	lwp_exit();
}

/*
 * Initialize the vmtask_* interface: start helper process, which creates the
 * task queue with SDC scheduling.
 */
void
vmtask_init()
{
	if (vmtask_disabled)
		return;

	/* create vm taskq process */
	if (newproc(vmtask_pinit, NULL, syscid, minclsyspri, NULL, 0) != 0) {
#ifdef	VMTASK_DEBUG
		panic("tq_vm_init: unable to fork vmtasks");
#endif
		vmtask_disabled = 1;
	}
}
