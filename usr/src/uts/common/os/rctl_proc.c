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
 * Copyright (c) 2001, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/types.h>
#include <sys/cmn_err.h>
#include <sys/sysmacros.h>
#include <sys/proc.h>
#include <sys/tick_ac.h>
#include <sys/rctl.h>
#include <sys/rctl_impl.h>
#include <sys/port_kernel.h>

#include <sys/vmparam.h>
#include <sys/machparam.h>
#include <sys/time.h>
#include <sys/systm.h>
#include <sys/cpuvar.h>
#include <sys/atomic.h>
#include <sys/clock_tick.h>

/*
 * Process-based resource controls
 *   The structure of the kernel leaves us no particular place where the process
 *   abstraction can be declared--it is intertwined with the growth of the Unix
 *   kernel.  Accordingly, we place all of the resource control logic associated
 *   with processes, both existing and future, in this file.
 */

rctl_hndl_t rctlproc_legacy[RLIM_NLIMITS];
uint_t rctlproc_flags[RLIM_NLIMITS] = {
	RCTL_LOCAL_SIGNAL,			/* RLIMIT_CPU	*/
	RCTL_LOCAL_DENY | RCTL_LOCAL_SIGNAL,	/* RLIMIT_FSIZE */
	RCTL_LOCAL_DENY,				/* RLIMIT_DATA	*/
	RCTL_LOCAL_DENY,				/* RLIMIT_STACK */
	RCTL_LOCAL_DENY,				/* RLIMIT_CORE	*/
	RCTL_LOCAL_DENY,				/* RLIMIT_NOFILE */
	RCTL_LOCAL_DENY				/* RLIMIT_VMEM	*/
};
int rctlproc_signals[RLIM_NLIMITS] = {
	SIGXCPU,				/* RLIMIT_CPU	*/
	SIGXFSZ,				/* RLIMIT_FSIZE	*/
	0, 0, 0, 0, 0				/* remainder do not signal */
};

rctl_hndl_t rc_process_msgmnb;
rctl_hndl_t rc_process_msgtql;
rctl_hndl_t rc_process_semmsl;
rctl_hndl_t rc_process_semopm;
rctl_hndl_t rc_process_portev;

/*
 * process.max-cpu-time / RLIMIT_CPU
 */

/*
 * Check if any threshold has been crossed and take necessary actions.
 */
void
proc_cpu_time_check(proc_t *p)
{
	(void) rctl_test(rctlproc_legacy[RLIMIT_CPU], p->p_rctls, p, 0,
	    RCA_UNSAFE_SIGINFO);
}

/*
 * Set the CPU time threshold.
 */
static void
proc_cpu_time_threshold(proc_t *p, uint64_t left)
{
	proc_ac_ctl_t *pctl;
	proc_ac_t *pac, *pac_end;
	uint64_t threshold;

	pctl = &p->p_ac_ctl;
	pac = pctl->pac;
	ASSERT(pac != NULL);

	if (pctl->pac_multi) {
		if (left != UINT64_MAX)
			left /= clock_tick_max_sets;
		pac_end = &pac[clock_tick_max_sets];
	} else {
		pac_end = &pac[1];
	}

	/*
	 * Set thresholds for a process.
	 */
	while (pac < pac_end) {
		threshold = pac->pac_cpu_time + left;
		if (threshold < pac->pac_cpu_time)	/* check for overflow */
			threshold = UINT64_MAX;
		pac->pac_threshold = threshold;
		pac++;
	}
}

/*
 * Aggregate the CPU time for a process.
 */
static uint64_t
proc_cpu_time_aggregate(proc_t *p)
{
	proc_ac_ctl_t *pctl;
	proc_ac_t *pac, *pac_end;
	uint64_t total;

	pctl = &p->p_ac_ctl;
	pac = pctl->pac;
	ASSERT(pac != NULL);

	if (pctl->pac_multi)
		pac_end = &pac[clock_tick_max_sets];
	else
		pac_end = &pac[1];

	total = 0;
	while (pac < pac_end) {
		total += pac->pac_cpu_time;
		if (total < pac->pac_cpu_time)		/* check for overflow */
			return (UINT64_MAX);
		pac++;
	}

	return (total);
}

/*
 * Called by the LWP accounting handlers to accumulate CPU time for a
 * multi-threaded process. If a threshold has been crossed, return 1.
 */
int
proc_cpu_time_add(proc_t *p, int set, uint64_t delta)
{
	proc_ac_ctl_t *pctl;
	proc_ac_t *pac;

	pctl = &p->p_ac_ctl;
	ASSERT(pctl->pac != NULL);
	if (!pctl->pac_multi)
		set = 0;

	pac = &pctl->pac[set];
	pac->pac_cpu_time += delta;

	return (pac->pac_cpu_time >= pac->pac_threshold);
}

/*
 * Set the next enforced threshold for the process. Aggregate CPU time for the
 * process. Set the new threshold based on the time left.
 */
/*ARGSUSED*/
static int
proc_cpu_time_set(rctl_t *rctl, struct proc *p, rctl_entity_p_t *e,
    rctl_qty_t nv)
{
	proc_ac_ctl_t *pctl;
	uint64_t total, left, ticks;

	ASSERT(e->rcep_t == RCENTITY_PROCESS);

	pctl = &p->p_ac_ctl;
	if (pctl->pac == NULL) {
		/*
		 * The first LWP has not yet been created. Nothing to do.
		 */
		return (0);
	}

	if (nv >= TICK_TO_SEC((uint64_t)UINT64_MAX)) {
		left = UINT64_MAX;
	} else {
		total = proc_cpu_time_aggregate(p);
		ticks = SEC_TO_TICK(nv);
		if (total >= ticks)
			left = 0;
		else
			left = ticks - total;
	}
	proc_cpu_time_threshold(p, left);

	return (0);
}

/*
 * Check if the next enforced threshold for the process has been crossed.
 * Take this opportunity to also update the threshold.
 */
/*ARGSUSED*/
static int
proc_cpu_time_test(rctl_t *r, proc_t *p, rctl_entity_p_t *e,
    struct rctl_val *rcntl, rctl_qty_t incr, uint_t flags)
{
	proc_ac_ctl_t *pctl;
	uint64_t total, left, ticks;
	rctl_qty_t cur, threshold;

	ASSERT(MUTEX_HELD(&p->p_lock));
	ASSERT(e->rcep_t == RCENTITY_PROCESS);

	pctl = &p->p_ac_ctl;
	ASSERT(pctl->pac != NULL);

	total = proc_cpu_time_aggregate(p);
	cur = TICK_TO_SEC(total);

	threshold = rcntl->rcv_value;
	if (threshold >= TICK_TO_SEC((uint64_t)UINT64_MAX)) {
		left = UINT64_MAX;
	} else {
		ticks = SEC_TO_TICK(threshold);
		if (total >= ticks)
			left = 0;
		else
			left = ticks - total;
	}
	proc_cpu_time_threshold(p, left);

	return ((cur + incr) >= threshold);
}

static rctl_ops_t proc_cpu_time_ops = {
	rcop_no_action,
	rcop_no_usage,
	proc_cpu_time_set,
	proc_cpu_time_test
};

/*
 * process.max-file-size / RLIMIT_FSIZE
 */
static int
proc_filesize_set(rctl_t *rctl, struct proc *p, rctl_entity_p_t *e,
    rctl_qty_t nv)
{
	if (p->p_model == DATAMODEL_NATIVE)
		nv = MIN(nv, rctl->rc_dict_entry->rcd_max_native);
	else
		nv = MIN(nv, rctl->rc_dict_entry->rcd_max_ilp32);

	ASSERT(e->rcep_t == RCENTITY_PROCESS);
	e->rcep_p.proc->p_fsz_ctl = nv;

	return (0);
}

static rctl_ops_t proc_filesize_ops = {
	rcop_no_action,
	rcop_no_usage,
	proc_filesize_set,
	rcop_no_test
};

/*
 * process.max-data / RLIMIT_DATA
 */

/*
 * process.max-stack-size / RLIMIT_STACK
 */
static int
proc_stack_set(rctl_t *rctl, struct proc *p, rctl_entity_p_t *e,
    rctl_qty_t nv)
{
	klwp_t *lwp = ttolwp(curthread);

	if (p->p_model == DATAMODEL_NATIVE)
		nv = MIN(nv, rctl->rc_dict_entry->rcd_max_native);
	else
		nv = MIN(nv, rctl->rc_dict_entry->rcd_max_ilp32);

	/*
	 * In the process of changing the rlimit, this function actually
	 * gets called a number of times. We only want to save the current
	 * rlimit the first time we come through here. In post_syscall(),
	 * we copyin() the lwp's ustack, and compare it to the rlimit we
	 * save here; if the two match, we adjust the ustack to reflect
	 * the new stack bounds.
	 *
	 * We check to make sure that we're changing the rlimit of our
	 * own process rather than on behalf of some other process. The
	 * notion of changing this resource limit on behalf of another
	 * process is problematic at best, and changing the amount of stack
	 * space a process is allowed to consume is a rather antiquated
	 * notion that has limited applicability in our multithreaded
	 * process model.
	 */
	ASSERT(e->rcep_t == RCENTITY_PROCESS);
	if (lwp != NULL && lwp->lwp_procp == e->rcep_p.proc &&
	    lwp->lwp_ustack && lwp->lwp_old_stk_ctl == 0) {
		lwp->lwp_old_stk_ctl = (size_t)e->rcep_p.proc->p_stk_ctl;
		curthread->t_post_sys = 1;
	}

	e->rcep_p.proc->p_stk_ctl = nv;

	return (0);
}

static rctl_ops_t proc_stack_ops = {
	rcop_no_action,
	rcop_no_usage,
	proc_stack_set,
	rcop_no_test
};

/*
 * process.max-file-descriptors / RLIMIT_NOFILE
 */
static int
proc_nofile_set(rctl_t *rctl, struct proc *p, rctl_entity_p_t *e, rctl_qty_t nv)
{
	ASSERT(e->rcep_t == RCENTITY_PROCESS);
	if (p->p_model == DATAMODEL_NATIVE)
		nv = MIN(nv, rctl->rc_dict_entry->rcd_max_native);
	else
		nv = MIN(nv, rctl->rc_dict_entry->rcd_max_ilp32);

	e->rcep_p.proc->p_fno_ctl = nv;

	return (0);
}

static rctl_ops_t proc_nofile_ops = {
	rcop_no_action,
	rcop_no_usage,
	proc_nofile_set,
	rcop_absolute_test
};

/*
 * process.max-address-space / RLIMIT_VMEM
 */
static int
proc_vmem_set(rctl_t *rctl, struct proc *p, rctl_entity_p_t *e, rctl_qty_t nv)
{
	ASSERT(e->rcep_t == RCENTITY_PROCESS);
	if (p->p_model == DATAMODEL_ILP32)
		nv = MIN(nv, rctl->rc_dict_entry->rcd_max_ilp32);
	else
		nv = MIN(nv, rctl->rc_dict_entry->rcd_max_native);

	e->rcep_p.proc->p_vmem_ctl = nv;

	return (0);
}

static rctl_ops_t proc_vmem_ops = {
	rcop_no_action,
	rcop_no_usage,
	proc_vmem_set,
	rcop_no_test
};

/*
 * void rctlproc_default_init()
 *
 * Overview
 *   Establish default basic and privileged control values on the init process.
 *   These correspond to the soft and hard limits, respectively.
 */
void
rctlproc_default_init(struct proc *initp, rctl_alloc_gp_t *gp)
{
	struct rlimit64 rlp64;

	/*
	 * RLIMIT_CPU: deny never, sigtoproc(pp, NULL, SIGXCPU).
	 */
	rlp64.rlim_cur = rlp64.rlim_max = RLIM64_INFINITY;
	(void) rctl_rlimit_set(rctlproc_legacy[RLIMIT_CPU], initp, &rlp64, gp,
	    RCTL_LOCAL_SIGNAL, SIGXCPU, kcred);

	/*
	 * RLIMIT_FSIZE: deny always, sigtoproc(pp, NULL, SIGXFSZ).
	 */
	rlp64.rlim_cur = rlp64.rlim_max = RLIM64_INFINITY;
	(void) rctl_rlimit_set(rctlproc_legacy[RLIMIT_FSIZE], initp, &rlp64, gp,
	    RCTL_LOCAL_SIGNAL | RCTL_LOCAL_DENY, SIGXFSZ, kcred);

	/*
	 * RLIMIT_DATA: deny always, no default action.
	 */
	rlp64.rlim_cur = rlp64.rlim_max = RLIM64_INFINITY;
	(void) rctl_rlimit_set(rctlproc_legacy[RLIMIT_DATA], initp, &rlp64, gp,
	    RCTL_LOCAL_DENY, 0, kcred);

	/*
	 * RLIMIT_STACK: deny always, no default action.
	 */
#ifdef __sparc
	rlp64.rlim_cur = DFLSSIZ;
	rlp64.rlim_max = LONG_MAX;
#else
	rlp64.rlim_cur = DFLSSIZ;
	rlp64.rlim_max = MAXSSIZ;
#endif
	(void) rctl_rlimit_set(rctlproc_legacy[RLIMIT_STACK], initp, &rlp64, gp,
	    RCTL_LOCAL_DENY, 0, kcred);

	/*
	 * RLIMIT_CORE: deny always, no default action.
	 */
	rlp64.rlim_cur = rlp64.rlim_max = RLIM64_INFINITY;
	(void) rctl_rlimit_set(rctlproc_legacy[RLIMIT_CORE], initp, &rlp64, gp,
	    RCTL_LOCAL_DENY, 0, kcred);

	/*
	 * RLIMIT_NOFILE: deny always, no action.
	 */
	rlp64.rlim_cur = rlim_fd_cur;
	rlp64.rlim_max = rlim_fd_max;
	(void) rctl_rlimit_set(rctlproc_legacy[RLIMIT_NOFILE], initp, &rlp64,
	    gp, RCTL_LOCAL_DENY, 0, kcred);

	/*
	 * RLIMIT_VMEM
	 */
	rlp64.rlim_cur = rlp64.rlim_max = RLIM64_INFINITY;
	(void) rctl_rlimit_set(rctlproc_legacy[RLIMIT_VMEM], initp, &rlp64, gp,
	    RCTL_LOCAL_DENY, 0, kcred);
}

/*
 * void rctlproc_init()
 *
 * Overview
 *   Register the various resource controls associated with process entities.
 *   The historical rlim_infinity_map and rlim_infinity32_map are now encoded
 *   here as the native and ILP32 infinite values for each resource control.
 */
void
rctlproc_init()
{
	rctl_set_t *set;
	rctl_alloc_gp_t *gp;
	rctl_entity_p_t e;

	rctlproc_legacy[RLIMIT_CPU] = rctl_register("process.max-cpu-time",
	    RCENTITY_PROCESS, RCTL_GLOBAL_LOWERABLE | RCTL_GLOBAL_DENY_NEVER |
	    RCTL_GLOBAL_CPU_TIME | RCTL_GLOBAL_INFINITE | RCTL_GLOBAL_SECONDS,
	    UINT64_MAX, UINT64_MAX, &proc_cpu_time_ops);
	rctlproc_legacy[RLIMIT_FSIZE] = rctl_register("process.max-file-size",
	    RCENTITY_PROCESS, RCTL_GLOBAL_LOWERABLE | RCTL_GLOBAL_DENY_ALWAYS |
	    RCTL_GLOBAL_FILE_SIZE | RCTL_GLOBAL_BYTES,
	    MAXOFFSET_T, MAXOFFSET_T, &proc_filesize_ops);
	rctlproc_legacy[RLIMIT_DATA] = rctl_register("process.max-data-size",
	    RCENTITY_PROCESS, RCTL_GLOBAL_LOWERABLE | RCTL_GLOBAL_DENY_ALWAYS |
	    RCTL_GLOBAL_SIGNAL_NEVER | RCTL_GLOBAL_BYTES,
	    ULONG_MAX, UINT32_MAX, &rctl_default_ops);
#ifdef _LP64
#ifdef __sparc
	rctlproc_legacy[RLIMIT_STACK] = rctl_register("process.max-stack-size",
	    RCENTITY_PROCESS, RCTL_GLOBAL_LOWERABLE | RCTL_GLOBAL_DENY_ALWAYS |
	    RCTL_GLOBAL_SIGNAL_NEVER | RCTL_GLOBAL_BYTES,
	    LONG_MAX, INT32_MAX, &proc_stack_ops);
#else	/* __sparc */
	rctlproc_legacy[RLIMIT_STACK] = rctl_register("process.max-stack-size",
	    RCENTITY_PROCESS, RCTL_GLOBAL_LOWERABLE | RCTL_GLOBAL_DENY_ALWAYS |
	    RCTL_GLOBAL_SIGNAL_NEVER | RCTL_GLOBAL_BYTES,
	    MAXSSIZ, USRSTACK32 - PAGESIZE, &proc_stack_ops);
#endif	/* __sparc */
#else 	/* _LP64 */
	rctlproc_legacy[RLIMIT_STACK] = rctl_register("process.max-stack-size",
	    RCENTITY_PROCESS, RCTL_GLOBAL_LOWERABLE | RCTL_GLOBAL_DENY_ALWAYS |
	    RCTL_GLOBAL_SIGNAL_NEVER | RCTL_GLOBAL_BYTES,
	    USRSTACK - PAGESIZE, USRSTACK - PAGESIZE, &proc_stack_ops);
#endif
	rctlproc_legacy[RLIMIT_CORE] = rctl_register("process.max-core-size",
	    RCENTITY_PROCESS, RCTL_GLOBAL_LOWERABLE | RCTL_GLOBAL_DENY_ALWAYS |
	    RCTL_GLOBAL_SIGNAL_NEVER | RCTL_GLOBAL_BYTES,
	    MIN(MAXOFFSET_T, ULONG_MAX), UINT32_MAX, &rctl_default_ops);
	rctlproc_legacy[RLIMIT_NOFILE] = rctl_register(
	    "process.max-file-descriptor", RCENTITY_PROCESS,
	    RCTL_GLOBAL_LOWERABLE | RCTL_GLOBAL_DENY_ALWAYS |
	    RCTL_GLOBAL_COUNT, INT32_MAX, INT32_MAX, &proc_nofile_ops);
	rctlproc_legacy[RLIMIT_VMEM] =
	    rctl_register("process.max-address-space", RCENTITY_PROCESS,
	    RCTL_GLOBAL_LOWERABLE | RCTL_GLOBAL_DENY_ALWAYS |
	    RCTL_GLOBAL_SIGNAL_NEVER | RCTL_GLOBAL_BYTES,
	    ULONG_MAX, UINT32_MAX, &proc_vmem_ops);

	rc_process_semmsl = rctl_register("process.max-sem-nsems",
	    RCENTITY_PROCESS, RCTL_GLOBAL_DENY_ALWAYS | RCTL_GLOBAL_COUNT,
	    SHRT_MAX, SHRT_MAX, &rctl_absolute_ops);
	rctl_add_legacy_limit("process.max-sem-nsems", "semsys",
	    "seminfo_semmsl", 512, SHRT_MAX);

	rc_process_semopm = rctl_register("process.max-sem-ops",
	    RCENTITY_PROCESS, RCTL_GLOBAL_DENY_ALWAYS | RCTL_GLOBAL_COUNT,
	    INT_MAX, INT_MAX, &rctl_absolute_ops);
	rctl_add_legacy_limit("process.max-sem-ops", "semsys",
	    "seminfo_semopm", 512, INT_MAX);

	rc_process_msgmnb = rctl_register("process.max-msg-qbytes",
	    RCENTITY_PROCESS, RCTL_GLOBAL_DENY_ALWAYS | RCTL_GLOBAL_BYTES,
	    ULONG_MAX, ULONG_MAX, &rctl_absolute_ops);
	rctl_add_legacy_limit("process.max-msg-qbytes", "msgsys",
	    "msginfo_msgmnb", 65536, ULONG_MAX);

	rc_process_msgtql = rctl_register("process.max-msg-messages",
	    RCENTITY_PROCESS, RCTL_GLOBAL_DENY_ALWAYS | RCTL_GLOBAL_COUNT,
	    UINT_MAX, UINT_MAX, &rctl_absolute_ops);
	rctl_add_legacy_limit("process.max-msg-messages", "msgsys",
	    "msginfo_msgtql", 8192, UINT_MAX);

	rc_process_portev = rctl_register("process.max-port-events",
	    RCENTITY_PROCESS, RCTL_GLOBAL_DENY_ALWAYS | RCTL_GLOBAL_COUNT,
	    PORT_MAX_EVENTS, PORT_MAX_EVENTS, &rctl_absolute_ops);
	rctl_add_default_limit("process.max-port-events", PORT_DEFAULT_EVENTS,
	    RCPRIV_PRIVILEGED, RCTL_LOCAL_DENY);

	/*
	 * Place minimal set of controls on "sched" process for inheritance by
	 * processes created via newproc().
	 */
	set = rctl_set_create();
	gp = rctl_set_init_prealloc(RCENTITY_PROCESS);
	mutex_enter(&curproc->p_lock);
	e.rcep_p.proc = curproc;
	e.rcep_t = RCENTITY_PROCESS;
	curproc->p_rctls = rctl_set_init(RCENTITY_PROCESS, curproc, &e,
	    set, gp);
	mutex_exit(&curproc->p_lock);
	rctl_prealloc_destroy(gp);
}
