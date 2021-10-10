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

#ifndef	_VM_VMTASK_H
#define	_VM_VMTASK_H

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef	_KERNEL

/*
 * The VMTASK infrastructure is used to distribute various VM related tasks to
 * multiple cpus.  The purpose behind this is to break up a single large task
 * into multiple smaller tasks which can be parallelized efficiently and thus
 * complete in a shorter amount of time.
 *
 * It is implemented via the kernel task queue subsystem and uses the System
 * Duty Cycle (SDC) scheduling class.
 *
 * It dynamically decides the number of threads to execute based upon the number
 * of online CPUs, job size, and minimal recommended job chunk size and will
 * never run more threads than there are online CPUs.  Each thread will take its
 * next piece of the job from a common pool and once it finishes one job, it
 * will take its next piece ...  This helps to balance the impact of all of the
 * VMTASK users and if more users are added, it will automatically balance the
 * workload via this mechanism.  A single job is limited to run
 * vmtask_ntasks_max tasks or the number of online CPUs in the system whichever
 * is smaller.  The total number of threads is never greater than
 * number of online CPUs and this is why new threads might have to wait for
 * threads from the currently running jobs to become available for use.
 *
 * Task queue interface is used to keep number of threads always equal to number
 * of online CPUs.
 *
 * INTERFACES ==================================================================
 *
 * int vmtask_run_job(job_size, job_chunk_min, job_func, func_arg, undop);
 *	Split the job of size job_size into a number of threads.
 *	Each thread receives a multiple of job_chunk_min pieces of work if more
 *	than one thread is started. The last piece of work can be of any size.
 *	undop is optional and can be set to NULL, however, if it is used and
 *	vmtask_run_job() returns an error vmtask_undo() must be called to avoid
 *	memory leaks.
 *
 *	Returns 0 on success. It means that each job_func call returned 0 and in
 *	this case no undo calls are needed and should not be done.
 *
 *	Returns non-zero if at least one call to job_func returned non-zero. The
 *	return value equals to the first recorded non-zero return from func_arg.
 *	All threads stop doing job as they encounter that one of the threads
 *	failed. If undop is not NULL, than vmtask_undo() must be performed.
 *
 *
 * void vmtask_undo(undop, undo_func, func_arg);
 *	Is called to undo work if it failed during vmtask_run_job(). It is
 *	guaranteed that undo is going to be done for each index of work that
 *	has succeeded. The return values from undo_func are ignored.
 *
 *
 * void vmtask_init();
 *	Initialize vmtask interface. Start vmtask process and pre-allocate
 *	threads.
 *
 *
 * vmtask_func_t  (idx, end, arg, *ridx);
 *	Function type used by vmtask_undo() and vmtask_run_job(). idx is the
 *	beginning index of job. (end-idx) is the amount of work.
 *	*ridx return index must be set by this function to where it finished.
 *	On success it is always equals to end. If non-zero is returned
 *	*ridx should be set to the index at which failure occurred.
 *	All of the work from idx to *ridx after the function has returned has
 *	been successfully completed.
 *
 *
 * VMTASK_SPGS_MINJOB
 *	Recommended job_chunk_min for work with  small pages granularity.
 *
 *
 * VMTASK_LPGS_MINJOB
 *	Recommended job_chunk_min for work with large pages granularity.
 * =============================================================================
 *
 * There are many places in VM that do repetitive linear operations.
 * For example:
 * -----------------------------------------------------------------------------
 * | Original function:                  | Re-written to use vmtask_*          |
 * -----------------------------------------------------------------------------
 * | func()                              | func()                              |
 * |     while i < npages                |     vmtask_undo_t undo;             |
 * |         if foo(ppa[i])              |     rv = vmtask_run_job(npages,     |
 * |             break;                  |       VMTASK_SPGS_MINJOB, func_task,|
 * |     if i < npages                   |       ppa, undo);                   |
 * |         while z < i                 |     if rv                           |
 * |             undo_foo(ppa[z]);       |         vmtask_undo(&undo,          |
 * |         return ERROR;               |           func_undo, ppa);          |
 * |     return SUCCESS;                 |             return ERROR;           |
 * |                                     |     return SUCCESS;                 |
 * -----------------------------------------------------------------------------
 *
 * -----------------------------------------------------------------------------
 * | New func_task function:             | New func_undo function:             |
 * -----------------------------------------------------------------------------
 * | func_task(idx, end, ppa, *ridx)     | func_undo(idx, end, ppa, *ridx)     |
 * |     while idx < end                 |     while idx < end                 |
 * |         if foo(ppa[idx])            |         undo_foo(ppa[idx]);         |
 * |             *ridx = idx;            |     *ridx = end;                    |
 * |             return ERROR;           |     return SUCCESS;                 |
 * |     *ridx = end;                    |                                     |
 * |     return SUCCESS;                 |                                     |
 * |                                     |                                     |
 * |                                     |                                     |
 * |                                     |                                     |
 * -----------------------------------------------------------------------------
 *
 * For other examples search for calls to vmtask_run_job().
 */

/* job_chunk_min for small pages granularity */
#define	VMTASK_SPGS_MINJOB	16
/* job_chunk_min for large pages granularity */
#define	VMTASK_LPGS_MINJOB	1

/* Function that performs a job on a given region. */
typedef int	(*vmtask_func_t)(ulong_t, ulong_t, void *, ulong_t *);

/* Undo argument type, used for undoing operations in case of errors */
typedef void	*vmtask_undo_t;

/* Perform multithreaded job */
extern int	vmtask_run_job(ulong_t, ulong_t, vmtask_func_t, void *,
			vmtask_undo_t *);

/* Call undo function for each region where job was performed */
extern void	vmtask_undo(vmtask_undo_t *, vmtask_func_t, void *);

/* Initialize vmtask_* interface, must be called at system startup */
extern void	vmtask_init();

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _VM_VMTASK_H */
