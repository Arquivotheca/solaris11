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
 * Copyright (c) 1988, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

#include <sys/types.h>
#include <sys/param.h>
#include <sys/sysmacros.h>
#include <sys/signal.h>
#include <sys/user.h>
#include <sys/systm.h>
#include <sys/sysinfo.h>
#include <sys/var.h>
#include <sys/errno.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>
#include <sys/inline.h>
#include <sys/disp.h>
#include <sys/class.h>
#include <sys/bitmap.h>
#include <sys/kmem.h>
#include <sys/cpuvar.h>
#include <sys/vtrace.h>
#include <sys/cpupart.h>
#include <sys/lgrp.h>
#include <sys/lgrp_user.h>
#include <sys/pg.h>
#include <sys/cmt.h>
#include <sys/bitset.h>
#include <sys/schedctl.h>
#include <sys/atomic.h>
#include <sys/dtrace.h>
#include <sys/sdt.h>
#include <sys/archsystm.h>
#include <vm/as.h>

/*
 * Dispatch queue allocation structure and functions.
 */
struct disp_queue_info {
	disp_t		*dp;
	dispq_t		*olddispq;
	dispq_t		*newdispq;
	ulong_t		*olddqactmap;
	ulong_t		*newdqactmap;
	int		oldnglobpris;
};

static void	disp_dq_alloc(struct disp_queue_info *, int, disp_t *);
static void	disp_dq_assign(struct disp_queue_info *, int);
static void	disp_dq_free(struct disp_queue_info *);

/*
 * Boot CPU's dispatch queue.
 */
disp_t		cpu0_disp;

/*
 * Common and platform specific idle CPU routines.
 */
static void	disp_idle_generic(void);
void		(*idle_cpu)(void) = disp_idle_generic;
static void	disp_idle_enter(void);
static void	disp_idle_exit(void);
extern void	plat_idle_enter(processorid_t);
extern void	plat_idle_exit(processorid_t);

/*
 * Platform specific routines invoked when a thread is placed on a run queue.
 */
static void	generic_enq_thread(cpu_t *, int);
void		(*disp_enq_thread)(cpu_t *, int) = generic_enq_thread;

/*
 * Priority at which CPU affinity is dropped for kpq.
 */
#define	KPQPRI	(-1)
pri_t		kpqpri = KPQPRI;

/*
 * Priorities at which kernel and user preemption apply.
 */
pri_t		kpreemptpri;
pri_t		upreemptpri = 0;

/*
 * Interrupt thread priority base level.
 */
pri_t		intr_pri;

static void	disp_swapped_setrun(kthread_t *);
void		disp_swapped_enq(kthread_t *);
disp_lock_t	swapped_lock;	/* lock swapped threads and swap queue */
int		nswapped;	/* total number of swapped threads */

static void	disp_cpu_resched(cpu_t *, pri_t);

/*
 * If this is set, only interrupt threads will cause kernel preemptions.
 * This is done by changing the value of kpreemptpri.  kpreemptpri
 * will either be the max sysclass pri + 1 or the min interrupt pri.
 */
int		only_intr_kpreempt;

#define	SETKP_BACK	(0)
#define	SETKP_FRONT	(1)
static void		setkpdq(kthread_t *, int);

/*
 * Parameter that determines how recently a thread must have run
 * on the CPU to be considered loosely-bound to that CPU to reduce
 * cold cache effects.  The interval is in hertz.
 */
#define	RECHOOSE_INTERVAL (3)
static int	rechoose_interval = RECHOOSE_INTERVAL;
static hrtime_t	disp_rechoose_interval;
hrtime_t	disp_cache_warmth_delta;

/*
 * Parameter that determines how long (in nanoseconds) a thread must
 * be sitting on a run queue before it can be stolen by another CPU
 * to reduce migrations. The interval is in nanoseconds.
 *
 * The nosteal_nsec should be set by platform code cmp_set_nosteal_interval()
 * to an appropriate value. nosteal_nsec is set to NOSTEAL_UNINITIALIZED
 * here indicating it is uninitiallized. Setting nosteal_nsec to zero
 * effectively disables the nosteal 'protection'.
 */
#define	NOSTEAL_UNINITIALIZED	(-1)
hrtime_t	nosteal_nsec = NOSTEAL_UNINITIALIZED;
extern void	cmp_set_nosteal_interval(void);

/*
 * System 'default' class id. See dispadmin(1M) for details.
 */
id_t		defaultcid;

disp_lock_t	transition_lock;	/* lock on transitioning threads */
disp_lock_t	stop_lock;		/* lock on stopped threads */

static void	disp_cpu_dq_alloc(int);

/*
 * Sets up the idle thread 'tp' on the specified CPU.
 */
#define	DISP_SETUP_IDLE(cp, tp) {					\
	THREAD_ONPROC(tp, cp);						\
	cp->cpu_dispthread = tp;					\
	cp->cpu_dispatch_pri = -1;					\
	cp->cpu_runrun = cp->cpu_kprunrun = 0;				\
	cp->cpu_chosen_level = -1;					\
}

/*
 * These values are returned by disp_getwork/disp_getbest if we couldn't steal
 * a thread because it was sitting on its run queue for a very short
 * period of time or because a local thread appeared on our run queue,
 * respectively.
 */
#define	T_DONTSTEAL	(kthread_t *)(-1)
#define	T_LOCAL_AVAIL	(kthread_t *)(-2)

static kthread_t	*disp_getwork(cpu_t *);
static kthread_t	*disp_getbest(disp_t *);
static kthread_t	*disp_getkpq(disp_t *);

static kthread_t	*disp_ratify(kthread_t *, disp_t *);

extern kthread_t	*thread_unpin(void);
void			swtch_to(kthread_t *);

/*
 * Mapping between priorities and utilization levels.
 */
uint_t			*disp_pri_util_map = NULL;
boolean_t		disp_pri_util_disable = B_FALSE;
void			disp_pri_util_setup(pri_t, pri_t);

/*
 * disp_setup() is a common code routine to calculate and allocate dispatcher
 * variables and structures based on the maximum priority.
 */
static void
disp_setup(pri_t maxglobpri, pri_t oldnglobpris)
{
	pri_t newnglobpris;

	ASSERT(MUTEX_HELD(&cpu_lock));

	newnglobpris = maxglobpri + 1 + LOCK_LEVEL;

	if (newnglobpris > oldnglobpris) {
		/*
		 * Allocate new kp queues for each CPU partition.
		 */
		cpupart_kpqalloc(newnglobpris);

		/*
		 * Allocate new dispatch queues for each CPU.
		 */
		disp_cpu_dq_alloc(newnglobpris);

		/*
		 * Allocate and setup the mapping between priorities and
		 * utilization levels.
		 */
		disp_pri_util_setup(oldnglobpris, newnglobpris);

		/*
		 * compute new interrupt thread base priority
		 */
		intr_pri = maxglobpri;
		if (only_intr_kpreempt) {
			kpreemptpri = intr_pri + 1;
			if (kpqpri == KPQPRI)
				kpqpri = kpreemptpri;
		}

		v.v_nglobpris = newnglobpris;
	}

	disp_cache_warmth_delta = unscalehrtime(nsec_per_tick/2);
	disp_rechoose_interval = unscalehrtime(TICK_TO_NSEC(rechoose_interval));
}

/*
 * disp_init() initializes all loaded classes and the dispatcher framework.
 */
void
disp_init(void)
{
	id_t cid;
	pri_t maxglobpri = -1;
	pri_t cl_maxglobpri;

	/*
	 * Initialize transition lock, which will always be set.
	 */
	DISP_LOCK_INIT(&transition_lock);
	disp_lock_enter_high(&transition_lock);
	DISP_LOCK_INIT(&stop_lock);

	mutex_enter(&cpu_lock);
	CPU->cpu_disp->disp_maxrunpri = -1;
	CPU->cpu_disp->disp_max_unbound_pri = -1;

	/*
	 * Initialize the default CPU partition.
	 */
	cpupart_initialize_default();

	/*
	 * Call the class specific initialization functions for
	 * all pre-installed schedulers.
	 *
	 * We pass the size of a class specific parameter
	 * buffer to each of the initialization functions
	 * to try to catch problems with backward compatibility
	 * of class modules.
	 *
	 * For example a new class module running on an old system
	 * which didn't provide sufficiently large parameter buffers
	 * would be bad news. Class initialization modules can check for
	 * this and take action if they detect a problem.
	 */
	for (cid = 0; cid < nclass; cid++) {
		sclass_t *sc =  &sclass[cid];

		if (SCHED_INSTALLED(sc)) {
			cl_maxglobpri = sc->cl_init(cid, PC_CLPARMSZ,
			    &sc->cl_funcs);
			if (cl_maxglobpri > maxglobpri)
				maxglobpri = cl_maxglobpri;
		}
	}

	kpreemptpri = (pri_t)v.v_maxsyspri + 1;
	if (kpqpri == KPQPRI)
		kpqpri = kpreemptpri;

	ASSERT(maxglobpri >= 0);
	disp_setup(maxglobpri, 0);

	mutex_exit(&cpu_lock);

	/*
	 * Platform specific sticky scheduler setup.
	 */
	if (nosteal_nsec == NOSTEAL_UNINITIALIZED)
		cmp_set_nosteal_interval();

	/*
	 * Get the default class ID; this may be later modified via
	 * dispadmin(1M).  This will load the class (normally TS) and that will
	 * call disp_add(), which is why we had to drop cpu_lock first.
	 */
	if (getcid(defaultclass, &defaultcid) != 0) {
		cmn_err(CE_PANIC, "Couldn't load default scheduling class '%s'",
		    defaultclass);
	}
}

/*
 * disp_add() is called to initialize the dispatcher for a newly loaded class.
 */
void
disp_add(sclass_t *clp)
{
	pri_t maxglobpri;
	pri_t cl_maxglobpri;

	mutex_enter(&cpu_lock);
	/*
	 * Initialize the scheduler class.
	 */
	maxglobpri = (pri_t)(v.v_nglobpris - LOCK_LEVEL - 1);
	cl_maxglobpri = clp->cl_init(clp - sclass, PC_CLPARMSZ, &clp->cl_funcs);
	if (cl_maxglobpri > maxglobpri)
		maxglobpri = cl_maxglobpri;

	/*
	 * Save old queue information.  Since we're initializing a
	 * new scheduling class which has just been loaded, then
	 * the size of the dispq may have changed.  We need to handle
	 * that here.
	 */
	disp_setup(maxglobpri, v.v_nglobpris);

	mutex_exit(&cpu_lock);
}

/*
 * disp_cpu_dq_alloc() allocates new dispatch queues with the stated number of
 * priorities for each new CPU. Note that we allocate and free memory before
 * and after pausing CPUs, respectively. This is required since one of the
 * paused CPUs could be holding a lock that would deadlock such operations.
 */
static void
disp_cpu_dq_alloc(int numpris)
{
	cpu_t *cpup;
	struct disp_queue_info *disp_mem;
	int i, num;

	ASSERT(MUTEX_HELD(&cpu_lock));

	disp_mem = kmem_zalloc(NCPU *
	    sizeof (struct disp_queue_info), KM_SLEEP);

	/*
	 * Allocate the required memory before pausing CPUs.
	 */
	i = 0;
	cpup = cpu_list;

	do {
		disp_dq_alloc(&disp_mem[i], numpris, cpup->cpu_disp);
		i++;
		cpup = cpup->cpu_next;
	} while (cpup != cpu_list);

	num = i;
	pause_cpus(NULL);

	for (i = 0; i < num; i++)
		disp_dq_assign(&disp_mem[i], numpris);

	start_cpus();

	/*
	 * Free the unused memory after CPUs are started.
	 */
	for (i = 0; i < num; i++)
		disp_dq_free(&disp_mem[i]);

	kmem_free(disp_mem, NCPU * sizeof (struct disp_queue_info));
}

static void
disp_dq_alloc(struct disp_queue_info *dptr, int numpris, disp_t	*dp)
{
	dptr->newdispq = kmem_zalloc(numpris * sizeof (dispq_t), KM_SLEEP);
	dptr->newdqactmap = kmem_zalloc(((numpris / BT_NBIPUL) + 1) *
	    sizeof (long), KM_SLEEP);

	dptr->dp = dp;
}

static void
disp_dq_assign(struct disp_queue_info *dptr, int numpris)
{
	disp_t *dp;

	dp = dptr->dp;
	dptr->olddispq = dp->disp_q;
	dptr->olddqactmap = dp->disp_qactmap;
	dptr->oldnglobpris = dp->disp_npri;

	ASSERT(dptr->oldnglobpris < numpris);

	if (dptr->olddispq != NULL) {
		/*
		 * Use kcopy because bcopy is platform-specific
		 * and could block while we might have paused CPUs.
		 */
		(void) kcopy(dptr->olddispq, dptr->newdispq,
		    dptr->oldnglobpris * sizeof (dispq_t));
		(void) kcopy(dptr->olddqactmap, dptr->newdqactmap,
		    ((dptr->oldnglobpris / BT_NBIPUL) + 1) *
		    sizeof (long));
	}

	dp->disp_q = dptr->newdispq;
	dp->disp_qactmap = dptr->newdqactmap;
	dp->disp_q_limit = &dptr->newdispq[numpris];
	dp->disp_npri = numpris;
}

static void
disp_dq_free(struct disp_queue_info *dptr)
{
	if (dptr->olddispq != NULL)
		kmem_free(dptr->olddispq,
		    dptr->oldnglobpris * sizeof (dispq_t));

	if (dptr->olddqactmap != NULL)
		kmem_free(dptr->olddqactmap,
		    ((dptr->oldnglobpris / BT_NBIPUL) + 1) * sizeof (long));
}

/*
 * Initialize the dispatch queue for a newly created CPU. Called before the
 * CPU is visible through cpu[] or any other list.
 */
void
disp_cpu_init(cpu_t *cp)
{
	disp_t	*dp;
	dispq_t	*newdispq;
	ulong_t	*newdqactmap;

	ASSERT(MUTEX_HELD(&cpu_lock));	/* protect dispatcher queue sizes */

	if (cp == cpu0_disp.disp_cpu)
		dp = &cpu0_disp;
	else
		dp = kmem_alloc(sizeof (disp_t), KM_SLEEP);

	bzero(dp, sizeof (disp_t));
	cp->cpu_disp = dp;
	dp->disp_cpu = cp;
	dp->disp_maxrunpri = -1;
	dp->disp_max_unbound_pri = -1;
	DISP_LOCK_INIT(&cp->cpu_thread_lock);

	/*
	 * Allocate memory for the dispatcher queue headers and the active
	 * queue bitmap.
	 */
	newdispq = kmem_zalloc(v.v_nglobpris * sizeof (dispq_t), KM_SLEEP);
	newdqactmap = kmem_zalloc(((v.v_nglobpris / BT_NBIPUL) + 1) *
	    sizeof (long), KM_SLEEP);

	dp->disp_q = newdispq;
	dp->disp_qactmap = newdqactmap;
	dp->disp_q_limit = &newdispq[v.v_nglobpris];
	dp->disp_npri = v.v_nglobpris;
}

void
disp_cpu_fini(cpu_t *cp)
{
	ASSERT(MUTEX_HELD(&cpu_lock));

	disp_kp_free(cp->cpu_disp);
	if (cp->cpu_disp != &cpu0_disp)
		kmem_free(cp->cpu_disp, sizeof (disp_t));
}

/*
 * Allocate a new, larger kpreempt dispatch queue to replace the old one.
 */
void
disp_kp_alloc(disp_t *dq, pri_t npri)
{
	struct disp_queue_info mem_info;

	if (npri > dq->disp_npri) {
		/*
		 * Allocate memory for the new array.
		 */
		disp_dq_alloc(&mem_info, npri, dq);

		/*
		 * Copy the old structures to the new kpq and free the old one.
		 */
		disp_dq_assign(&mem_info, npri);
		disp_dq_free(&mem_info);
	}
}

/*
 * Free dispatch queue.
 * Used for the kpreempt queues for a removed CPU partition and
 * for the per-CPU queues of deleted CPUs.
 */
void
disp_kp_free(disp_t *dq)
{
	struct disp_queue_info mem_info;

	mem_info.olddispq = dq->disp_q;
	mem_info.olddqactmap = dq->disp_qactmap;
	mem_info.oldnglobpris = dq->disp_npri;

	disp_dq_free(&mem_info);
}

/*
 * Allocate and setup the mapping between priorities and utilization levels.
 */
void
disp_pri_util_setup(pri_t oldmaxpri, pri_t newmaxpri)
{
	uint_t i, *new_map, *old_map;

	ASSERT(newmaxpri > 0);

	new_map = kmem_alloc(newmaxpri * sizeof (uint_t), KM_SLEEP);

	for (i = 0; i < newmaxpri; i++)
		new_map[i] = 1;

	old_map = disp_pri_util_map;
	disp_pri_util_map = new_map;

	if (old_map != NULL)
		kmem_free(old_map, oldmaxpri * sizeof (uint_t));

	/*
	 * Notify the PG framework that we're updating the priority range.
	 */
	pg_priority_setup(oldmaxpri, newmaxpri, disp_pri_util_map);
}

/*
 * disp_anywork() looks for work to steal from other CPU's run queues before
 * going idle. This function must be called with high spl or with kernel
 * preemption disabled to prevent the partition's active CPU list from
 * changing while being traversed.
 *
 * This is essentially a simpler version of disp_getwork() to be called by
 * CPUs preparing to "halt".
 */
int
disp_anywork(void)
{
	cpu_t *cp = CPU, *ocp;
	volatile int *local_nrunnable = &cp->cpu_disp->disp_nrunnable;

	if (!(cp->cpu_flags & CPU_OFFLINE)) {
		if (CP_MAXRUNPRI(cp->cpu_part) >= 0)
			return (1);

		for (ocp = cp->cpu_next_part; ocp != cp;
		    ocp = ocp->cpu_next_part) {
			ASSERT(CPU_ACTIVE(ocp));

			/*
			 * Something has appeared on the local run queue or
			 * another CPU has work for us.
			 */
			if (*local_nrunnable > 0 || cp->cpu_steal_from != NULL)
				return (1);

			/*
			 * If we encounter another CPU that is or will soon be
			 * iterating this list looking for work, terminate our
			 * walk here and let this other CPU patrol the next
			 * part of the list. We need to make sure this other
			 * CPU is idle and really has no work to do. This
			 * statement checks that it is idle but hasn't halted
			 * yet and has nothing in its run queue.
			 */
			if (CPU_DISP_IDLE(ocp) &&
			    !CPU_DISP_IS_HALTED(ocp) &&
			    ocp->cpu_disp->disp_nrunnable == 0)
				return (0);

			/*
			 * Work can be taken from another CPU if:
			 *  - there's unbound work in its run queue;
			 *  - the CPU isn't idle and about to take its only
			 *  runnable thread;
			 *  - the CPU isn't resuming work that is temporarily
			 *  in the run queue;
			 *  - the CPU is not running a transient thread and
			 *  consequently about to switch to the only thread
			 *  in its queue.
			 */
			if (ocp->cpu_disp->disp_max_unbound_pri != -1 &&
			    !(ocp->cpu_disp->disp_nrunnable == 1 && (
			    CPU_DISP_IDLE(ocp) ||
			    CPU_DISP_IS_CTX_SWITCH(ocp) ||
			    CPU_DISP_IS_TRANSIENT(ocp)))) {

				cp->cpu_steal_from = ocp;
				return (1);
			}
		}
	}

	return (0);
}

/*
 * Called when CPU enters the idle loop
 */
static void
disp_idle_enter(void)
{
	cpu_t *cp = CPU;

	new_cpu_mstate(CMS_IDLE, gethrtime_unscaled());
	CPU_STATS_ADDQ(cp, sys, idlethread, 1);
	plat_idle_enter(cp->cpu_id);
}

/*
 * Called when CPU exits the idle loop
 */
static void
disp_idle_exit(void)
{
	cpu_t *cp = CPU;

	new_cpu_mstate(CMS_SYSTEM, gethrtime_unscaled());
	plat_idle_exit(cp->cpu_id);
}

/*
 * Idle loop.
 */
void
idle(void)
{
	cpu_t *cp = CPU;
	kthread_t *t;
	volatile int *local_nrunnable = &cp->cpu_disp->disp_nrunnable;

	disp_idle_enter();

	/*
	 * Uniprocessor version of idle loop.
	 * Do this until notified that we're on an actual multiprocessor.
	 */
	while (ncpus == 1) {
		if (*local_nrunnable == 0) {
			(*idle_cpu)();
			continue;
		}

		disp_idle_exit();
		swtch();

		disp_idle_enter(); /* returned from swtch */
	}

	/*
	 * Multiprocessor idle loop.
	 */
	for (;;) {
		/*
		 * If CPU is completely quiesced by p_online(2), just wait
		 * here with minimal bus traffic until put online.
		 */
		while (cp->cpu_flags & CPU_QUIESCED)
			(*idle_cpu)();

		if (*local_nrunnable > 0) {
			disp_idle_exit();
			swtch();
		} else {
			if (cp->cpu_flags & CPU_OFFLINE)
				continue;

			if ((t = disp_getwork(cp)) == NULL) {
				if (cp->cpu_chosen_level != -1) {
					disp_t *dp = cp->cpu_disp;
					disp_t *kpq;

					disp_lock_enter(&dp->disp_lock);
					/*
					 * Set kpq under lock to prevent
					 * migration between partitions.
					 */
					kpq = &cp->cpu_part->cp_kp_queue;
					if (kpq->disp_maxrunpri == -1)
						cp->cpu_chosen_level = -1;
					disp_lock_exit(&dp->disp_lock);
				}
				(*idle_cpu)();
				continue;
			}

			/*
			 * Try again if there was a thread but we couldn't
			 * steal it. If something appeared on our run queue,
			 * we'll notice it at the top of the loop.
			 */
			if (t == T_DONTSTEAL || t == T_LOCAL_AVAIL)
				continue;

			disp_idle_exit();
			swtch_to(t);
		}

		disp_idle_enter(); /* returned from swtch/swtch_to */
	}
}

/*
 * Preempt the currently running thread in favor of the highest priority thread.
 * The class of the current thread controls where it goes on the dispatcher
 * queues. If panicking, turn preemption off.
 */
void
preempt(void)
{
	kthread_t *t = curthread;
	klwp_t *lwp = ttolwp(curthread);

	if (panicstr)
		return;

	TRACE_0(TR_FAC_DISP, TR_PREEMPT_START, "preempt_start");

	thread_lock(t);

	if (t->t_state != TS_ONPROC || t->t_disp_queue != CPU->cpu_disp) {
		/*
		 * this thread has already been chosen to be run on
		 * another CPU. Clear kprunrun on this CPU since we're
		 * already headed for swtch().
		 */
		CPU->cpu_kprunrun = 0;
		thread_unlock_nopreempt(t);
		TRACE_0(TR_FAC_DISP, TR_PREEMPT_END, "preempt_end");
	} else {
		if (lwp != NULL)
			lwp->lwp_ru.nivcsw++;

		CPU_STATS_ADDQ(CPU, sys, inv_swtch, 1);
		THREAD_TRANSITION(t);
		CL_PREEMPT(t);
		DTRACE_SCHED(preempt);
		thread_unlock_nopreempt(t);

		TRACE_0(TR_FAC_DISP, TR_PREEMPT_END, "preempt_end");

		swtch();		/* clears CPU->cpu_runrun via disp() */
	}
}

/*
 * disp() finds the highest priority thread for this processor to run and
 * sets it in TS_ONPROC state so that resume() can be called to run it.
 */
static kthread_t *
disp(void)
{
	cpu_t *cpup = CPU;
	disp_t *dp, *kpq;
	dispq_t *dq;
	kthread_t *tp;
	int maxrunword;
	pri_t pri;

	TRACE_0(TR_FAC_DISP, TR_DISP_START, "disp_start");

	/*
	 * Find the highest priority loaded, runnable thread.
	 */
	dp = cpup->cpu_disp;

reschedule:
	/*
	 * If there is more important work on the global queue with a better
	 * priority than the maximum on this CPU, take it now.
	 */
	kpq = &cpup->cpu_part->cp_kp_queue;
	while ((pri = kpq->disp_maxrunpri) >= 0 &&
	    pri >= dp->disp_maxrunpri &&
	    (cpup->cpu_flags & CPU_OFFLINE) == 0 &&
	    (tp = disp_getkpq(kpq)) != NULL) {
		if (disp_ratify(tp, kpq) != NULL) {
			TRACE_1(TR_FAC_DISP, TR_DISP_END,
			    "disp_end:tid %p", tp);
			return (tp);
		}
	}

	disp_lock_enter(&dp->disp_lock);

	/*
	 * Check if there's local work to be done. If there isn't, call into
	 * disp_getwork() to look at what's runnable on other CPU's queues.
	 */
	if ((pri = dp->disp_maxrunpri) == -1) {
		/*
		 * Choose the idle thread if the CPU is quiesced. Note that
		 * CPUs that have the CPU_OFFLINE flag set can still run
		 * interrupt threads, but cannot run threads from other queues.
		 */
		if (cpup->cpu_flags & CPU_OFFLINE) {
			disp_lock_exit_high(&dp->disp_lock);
			tp = cpup->cpu_idle_thread;
			DISP_SETUP_IDLE(cpup, tp);

			TRACE_1(TR_FAC_DISP, TR_DISP_END,
			    "disp_end:tid %p", tp);
			return (tp);
		}

		disp_lock_exit(&dp->disp_lock);
		tp = disp_getwork(cpup);

		/*
		 * Switch to idle if we didn't find or couldn't steal any work.
		 */
		if (tp == NULL || tp == T_DONTSTEAL) {
			tp = cpup->cpu_idle_thread;
			(void) splhigh();
			DISP_SETUP_IDLE(cpup, tp);

			TRACE_1(TR_FAC_DISP, TR_DISP_END,
			    "disp_end:tid %p", tp);
			return (tp);
		}

		/*
		 * Check if disp_getwork() indicated that there's local work
		 * to be done. We grab the disp lock to validate its finding.
		 * If that's no longer true, drop the lock and switch to idle.
		 */
		if (tp == T_LOCAL_AVAIL) {
			disp_lock_enter(&dp->disp_lock);
			if ((pri = dp->disp_maxrunpri) == -1) {
				disp_lock_exit_high(&dp->disp_lock);
				tp = cpup->cpu_idle_thread;
				DISP_SETUP_IDLE(cpup, tp);

				TRACE_1(TR_FAC_DISP, TR_DISP_END,
				    "disp_end:tid %p", tp);
				return (tp);
			}
		} else {
			/*
			 * disp_getwork() returned a thread, let's go for it.
			 */
			TRACE_1(TR_FAC_DISP, TR_DISP_END,
			    "disp_end:tid %p", tp);
			return (tp);
		}

		/*
		 * We definitely cannot have made it this far if the current
		 * CPU has nothing in its run queue.
		 */
		ASSERT(pri > -1);
	}

	dq = &dp->disp_q[pri];
	tp = dq->dq_first;

	ASSERT(tp != NULL);
	ASSERT(tp->t_schedflag & TS_LOAD);	/* thread must be swapped in */

	DTRACE_SCHED2(dequeue, kthread_t *, tp, disp_t *, dp);

	/*
	 * Found it so remove it from queue.
	 */
	dp->disp_nrunnable--;
	dq->dq_sruncnt--;

	if ((dq->dq_first = tp->t_link) == NULL) {
		ulong_t	*dqactmap = dp->disp_qactmap;

		ASSERT(dq->dq_sruncnt == 0);
		dq->dq_last = NULL;

		/*
		 * If there are no more runnable threads in this queue we
		 * need to turn off its bit in disp_dqactmap. Otherwise, just
		 * recompute disp_maxrunpri.
		 */
		maxrunword = pri >> BT_ULSHIFT;
		dqactmap[maxrunword] &= ~BT_BIW(pri);

		if (dp->disp_nrunnable == 0) {
			dp->disp_max_unbound_pri = -1;
			dp->disp_maxrunpri = -1;
		} else {
			int ipri = bt_gethighbit(dqactmap, maxrunword);
			dp->disp_maxrunpri = ipri;

			if (ipri < dp->disp_max_unbound_pri)
				dp->disp_max_unbound_pri = ipri;
		}
	} else {
		tp->t_link = NULL;
	}

	/*
	 * Set TS_DONT_SWAP flag to prevent another processor from swapping
	 * out this thread before we have a chance to run it.
	 * While running, it is protected against swapping by t_lock.
	 */
	tp->t_schedflag |= TS_DONT_SWAP;
	cpup->cpu_dispthread = tp;		/* protected by spl only */
	cpup->cpu_dispatch_pri = pri;
	ASSERT(pri == DISP_PRIO(tp));
	thread_onproc(tp, cpup);  		/* set t_state to TS_ONPROC */

	disp_lock_exit_high(&dp->disp_lock);

	ASSERT(tp != NULL);
	TRACE_1(TR_FAC_DISP, TR_DISP_END,
	    "disp_end:tid %p", tp);

	if (disp_ratify(tp, kpq) == NULL)
		goto reschedule;

	return (tp);
}

/*
 * swtch()
 *	Find best runnable thread and run it.
 *	Called with the current thread already switched to a new state,
 *	on a sleep queue, run queue, stopped, and not zombied.
 *	May be called at any spl level less than or equal to LOCK_LEVEL.
 *	Always drops spl to the base level (spl0()).
 */
void
swtch(void)
{
	kthread_t *t = curthread, *next;
	cpu_t *cp;

	TRACE_0(TR_FAC_DISP, TR_SWTCH_START, "swtch_start");

	if (t->t_flag & T_INTR_THREAD)
		cpu_intr_swtch_enter(t);

	if (t->t_intr != NULL) {
		/*
		 * We are an interrupt thread. Setup and return the interrupted
		 * thread to be resumed.
		 */
		(void) splhigh();	/* block other scheduler action */
		cp = CPU;		/* now protected against migration */

		ASSERT(CPU_ON_INTR(cp) == 0);	/* not called with PIL > 10 */
		CPU_STATS_ADDQ(cp, sys, pswitch, 1);
		CPU_STATS_ADDQ(cp, sys, intrblk, 1);

		next = thread_unpin();
		TRACE_0(TR_FAC_DISP, TR_RESUME_START, "resume_start");
		resume_from_intr(next);
	} else {
#ifdef	DEBUG
		if (t->t_state == TS_ONPROC &&
		    t->t_disp_queue->disp_cpu == CPU &&
		    t->t_preempt == 0) {
			thread_lock(t);
			ASSERT(t->t_state != TS_ONPROC ||
			    t->t_disp_queue->disp_cpu != CPU ||
			    t->t_preempt != 0);	/* cannot migrate */
			thread_unlock_nopreempt(t);
		}
#endif	/* DEBUG */
		cp = CPU;

		next = disp();		/* returns with spl high */
		ASSERT(CPU_ON_INTR(cp) == 0);	/* not called with PIL > 10 */

		/* OK to steal anything left on the run queue. */
		cp->cpu_disp_flags &= ~CPU_DISP_CTX_SWITCH;
		cp->cpu_steal_from = NULL;

		if (next != t) {
			hrtime_t hrt = gethrtime_unscaled();

			pg_ev_thread_swtch(cp, hrt, t, next);

			/*
			 * If t was previously in the TS_ONPROC state,
			 * setfrontdq and setbackdq won't have set its t_waitrq.
			 * Since we now finally know that we're switching away
			 * from this thread, set its t_waitrq if it is on a run
			 * queue.
			 */
			if ((t->t_state == TS_RUN) && (t->t_waitrq == 0))
				t->t_waitrq = hrt;

			/*
			 * restore mstate of thread that we are switching to
			 */
			restore_mstate(next);

			CPU_STATS_ADDQ(cp, sys, pswitch, 1);

			cp->cpu_last_swtch = t->t_disp_time = hrt;

			TRACE_0(TR_FAC_DISP, TR_RESUME_START, "resume_start");

			if (dtrace_vtime_active)
				dtrace_vtime_switch(next);

			resume(next);

			/*
			 * The TR_RESUME_END and TR_SWTCH_END trace points
			 * appear at the end of resume(), because we may not
			 * return here
			 */
		} else {
			if (t->t_flag & T_INTR_THREAD)
				cpu_intr_swtch_exit(t);

			/*
			 * Threads that enqueue themselves on a run queue defer
			 * setting t_waitrq. It is then either set in swtch()
			 * when the CPU is actually yielded, or not at all if it
			 * is remaining on the CPU.
			 * There is however a window between where the thread
			 * placed itself on a run queue, and where it selects
			 * itself in disp(), where a third party (eg. clock()
			 * doing tick processing) may have re-enqueued this
			 * thread, setting t_waitrq in the process. We detect
			 * this race by noticing that despite switching to
			 * ourself, our t_waitrq has been set, and should be
			 * cleared.
			 */
			if (t->t_waitrq != 0)
				t->t_waitrq = 0;

			pg_ev_thread_remain(cp, t);

			DTRACE_SCHED(remain__cpu);
			TRACE_0(TR_FAC_DISP, TR_SWTCH_END, "swtch_end");
			(void) spl0();
		}
	}
}

/*
 * swtch_from_zombie() is a special case of swtch() that allows checks for
 * TS_ZOMB to be eliminated from normal resume. This routine finds the best
 * runnable thread and switches to it. Called with the current thread zombied.
 * Note that zombies cannot migrate, so CPU references are safe.
 */
void
swtch_from_zombie(void)
{
	kthread_t *next;
	cpu_t *cpu = CPU;

	TRACE_0(TR_FAC_DISP, TR_SWTCH_START, "swtch_start");

	ASSERT(curthread->t_state == TS_ZOMB);

	next = disp();			/* returns with spl high */

	ASSERT(CPU_ON_INTR(CPU) == 0);	/* not called with PIL > 10 */
	CPU_STATS_ADDQ(CPU, sys, pswitch, 1);
	ASSERT(next != curthread);
	TRACE_0(TR_FAC_DISP, TR_RESUME_START, "resume_start");

	pg_ev_thread_swtch(cpu, gethrtime_unscaled(), curthread, next);

	restore_mstate(next);

	if (dtrace_vtime_active)
		dtrace_vtime_switch(next);

	resume_from_zombie(next);

	/*
	 * The TR_RESUME_END and TR_SWTCH_END trace points appear at the end
	 * of resume() because we certainly will not return here.
	 */
}

#if defined(DEBUG) && (defined(DISP_DEBUG) || defined(lint))

/*
 * search_disp_queues() searches the given dispatch queues for thread 'tp'.
 * It returns one if 'tp' is found and zero otherwise.
 */
static int
search_disp_queues(disp_t *dp, kthread_t *tp)
{
	dispq_t	*dq, *eq;

	disp_lock_enter_high(&dp->disp_lock);

	for (dq = dp->disp_q, eq = dp->disp_q_limit; dq < eq; ++dq) {
		kthread_t *rp;

		ASSERT(dq->dq_last == NULL || dq->dq_last->t_link == NULL);

		for (rp = dq->dq_first; rp; rp = rp->t_link) {
			if (tp == rp) {
				disp_lock_exit_high(&dp->disp_lock);
				return (1);
			}
		}
	}

	disp_lock_exit_high(&dp->disp_lock);

	return (0);
}

/*
 * thread_on_queue() searches all per CPU dispatch queues and all partition
 * wide kpreempt queues for thread 'tp'. It returns one if 'tp' is found and
 * zero otherwise.
 */
static int
thread_on_queue(kthread_t *tp)
{
	cpu_t *cp;
	struct cpupart *part;

	ASSERT(getpil() >= DISP_LEVEL);

	/*
	 * Search the per-CPU dispatch queues for tp.
	 */
	cp = CPU;
	do {
		if (search_disp_queues(cp->cpu_disp, tp))
			return (1);
	} while ((cp = cp->cpu_next_onln) != CPU);

	/*
	 * Search the partition-wide kpreempt queues for tp.
	 */
	part = CPU->cpu_part;
	do {
		if (search_disp_queues(&part->cp_kp_queue, tp))
			return (1);
	} while ((part = part->cp_next) != CPU->cpu_part);

	return (0);
}

#else

#define	thread_on_queue(tp)	0	/* ASSERT must be !thread_on_queue */

#endif  /* DEBUG */

/*
 * Like swtch(), but switch to a specified thread taken from another CPU.
 * Called with spl high.
 */
void
swtch_to(kthread_t *next)
{
	cpu_t *cp = CPU;
	hrtime_t hrt;

	TRACE_0(TR_FAC_DISP, TR_SWTCH_START, "swtch_start");

	/*
	 * Update context switch statistics.
	 */
	CPU_STATS_ADDQ(cp, sys, pswitch, 1);

	TRACE_0(TR_FAC_DISP, TR_RESUME_START, "resume_start");

	hrt = gethrtime_unscaled();
	pg_ev_thread_swtch(cp, hrt, curthread, next);

	/* OK to steal anything left on the run queue. */
	cp->cpu_disp_flags &= ~CPU_DISP_CTX_SWITCH;
	cp->cpu_steal_from = NULL;

	/* record last execution time */
	cp->cpu_last_swtch = curthread->t_disp_time = hrt;

	/*
	 * If t was previously in the TS_ONPROC state, setfrontdq and setbackdq
	 * won't have set its t_waitrq.  Since we now finally know that we're
	 * switching away from this thread, set its t_waitrq if it is on a run
	 * queue.
	 */
	if ((curthread->t_state == TS_RUN) && (curthread->t_waitrq == 0))
		curthread->t_waitrq = hrt;

	/* restore next thread to previously running microstate */
	restore_mstate(next);

	if (dtrace_vtime_active)
		dtrace_vtime_switch(next);

	resume(next);

	/*
	 * The TR_RESUME_END and TR_SWTCH_END trace points appear at the end of
	 * resume() because we may not return here.
	 */
}

static void
disp_cpu_resched(cpu_t *cp, pri_t tpri)
{
	pri_t cpupri = cp->cpu_dispatch_pri;
	boolean_t poke = B_FALSE;

	if (cpupri != -1 && (cpupri < tpri)) {
		TRACE_2(TR_FAC_DISP, TR_CPU_RESCHED,
		    "CPU_RESCHED:Tpri %d Cpupri %d", tpri, cpupri);

		if (tpri >= upreemptpri && cp->cpu_runrun == 0) {
			cp->cpu_runrun = 1;
			aston(cp->cpu_dispthread);
			if (tpri < kpreemptpri && cp != CPU)
				poke = B_TRUE;
		}

		if (tpri >= kpreemptpri && cp->cpu_kprunrun == 0) {
			cp->cpu_kprunrun = 1;
			if (cp != CPU)
				poke = B_TRUE;
		}
	}

	/*
	 * Propagate cpu_runrun, and cpu_kprunrun to global visibility.
	 */
	membar_enter();

	if (poke)
		poke_cpu(cp->cpu_id);
}

/*
 * setbackdq() keeps runqs balanced such that the difference in length
 * between the chosen runq and the next one is no more than RUNQ_MAX_DIFF.
 * For threads with priorities below RUNQ_MATCH_PRI levels, the runq's lengths
 * must match. When per-thread TS_RUNQMATCH flag is set, setbackdq() will
 * try to keep runqs perfectly balanced regardless of the thread priority.
 */
#define	RUNQ_MATCH_PRI (16)	/* pri below which queue lengths must match */
#define	RUNQ_MAX_DIFF	(2)	/* maximum runq length difference */
#define	RUNQ_LEN(cp, pri)	((cp)->cpu_disp->disp_q[pri].dq_sruncnt)

/*
 * Macro that evaluates to true if it is likely that the thread has cache
 * warmth. This is based on the amount of time that has elapsed since the
 * thread last ran. If that amount of time is less than "rechoose_interval"
 * ticks, then we decide that the thread has enough cache warmth to warrant
 * some affinity for t->t_cpu.
 */
#define	THREAD_HAS_CACHE_WARMTH(thread, now)				\
	((thread == curthread) ||					\
	((now - thread->t_disp_time) <= disp_rechoose_interval))

/*
 * Put the specified thread on the back of the dispatcher queue corresponding
 * to its current priority. Called with the thread in transition, onproc or
 * stopped state and locked (transition implies locked) and at high spl.
 * Returns with the thread in TS_RUN state and still locked.
 */
void
setbackdq(kthread_t *tp)
{
	dispq_t *dq;
	disp_t *dp;
	cpu_t *cp, *cmt = NULL;
	pri_t tpri;
	boolean_t bound, self;
	hrtime_t hrt;

	ASSERT(THREAD_LOCK_HELD(tp));
	ASSERT((tp->t_schedflag & TS_ALLSTART) == 0);
	ASSERT(!thread_on_queue(tp));	/* make sure tp isn't on a runq */

	/*
	 * If thread is "swapped" or on the swap queue don't enqueue it, but
	 * wake sched.
	 */
	if ((tp->t_schedflag & (TS_LOAD | TS_ON_SWAPQ)) != TS_LOAD) {
		disp_swapped_setrun(tp);
		return;
	}

	self = (tp == curthread);

	/*
	 * A thread that is ONPROC may be temporarily placed on the run queue
	 * but then chosen to run again by disp.  If the thread we're placing on
	 * the queue is in TS_ONPROC state, don't set its t_waitrq until a
	 * replacement process is actually scheduled in swtch(). In this
	 * situation, curthread is the only thread that could be in the ONPROC
	 * state.
	 */
	hrt = gethrtime_unscaled();
	(void) cpu_update_pct(tp, hrt);

	if ((!self) && (tp->t_waitrq == 0))
		tp->t_waitrq = hrt;

	thread_transience_update(tp);

	bound = (tp->t_bound_cpu || tp->t_weakbound_cpu);
	tpri = DISP_PRIO(tp);

	/*
	 * The thread's current CPU may be in the process of being quiesced,
	 * but not just yet.
	 */
	ASSERT((tp->t_cpu->cpu_flags & CPU_QUIESCED) == 0);

	if (ncpus == 1)
		cp = tp->t_cpu;
	else if (!bound) {
		if (tpri >= kpqpri) {
			setkpdq(tp, SETKP_BACK);
			return;
		}

		/*
		 * We'll generally let this thread continue to run where
		 * it last ran... but will consider migration if:
		 *  - the thread is transient;
		 *  - the thread probably doesn't have much cache warmth;
		 *  - the CPU where it last ran is being offlined;
		 *  - the thread last ran outside it's home lgroup.
		 */
		if (T_TRANSIENT(tp) || !THREAD_HAS_CACHE_WARMTH(tp, hrt) ||
		    tp->t_cpu == cpu_inmotion) {
			cp = disp_lowpri_cpu(tp->t_cpu, tp->t_lpl, tpri, NULL);
		} else {
			if (!LGRP_CONTAINS_CPU(tp->t_lpl->lpl_lgrp, tp->t_cpu))
				cp = disp_lowpri_cpu(tp->t_cpu, tp->t_lpl,
				    tpri, self ? tp->t_cpu : NULL);
			else
				cp = tp->t_cpu;
		}

		if (tp->t_cpupart == cp->cpu_part) {
			/*
			 * cmt_balance() looks for a processor group in this
			 * CPU's hierarchy that we should balance against
			 * according to the CMT policy. If it finds one,
			 * it'll look for an idle CPU within that PG to
			 * dispatch to. If not or the thread is transient,
			 * we perform run queue load balancing on the
			 * previously selected CPU.
			 */
			if (T_TRANSIENT(tp) ||
			    (cmt = cmt_balance(tp, cp)) == cp) {
				int qlen;

				/*
				 * Balance across the run queues.
				 */
				qlen = RUNQ_LEN(cp, tpri);
				if (tpri >= RUNQ_MATCH_PRI &&
				    !(tp->t_schedflag & TS_RUNQMATCH))
					qlen -= RUNQ_MAX_DIFF;

				if (qlen > 0) {
					cpu_t *ncp;

					if (tp->t_lpl->lpl_lgrpid ==
					    LGRP_ROOTID) {
						ncp = cp->cpu_next_part;
					} else {
						/*
						 * Balance against the next CPU
						 * in the partition if there's
						 * only one CPU in this lpl.
						 */
						ncp = cp->cpu_next_lpl;
						if (ncp == cp)
							ncp = cp->cpu_next_part;
					}

					if (RUNQ_LEN(ncp, tpri) < qlen) {
						DTRACE_PROBE3(runq__balance,
						    kthread_t *, tp,
						    cpu_t *, cp, cpu_t *, ncp);

						cp = ncp;
					}
				}
			} else {
				ASSERT(cmt != NULL);
				cp = cmt;
			}
		} else {
			/*
			 * Migrate to a CPU in the new partition.
			 */
			cp = disp_lowpri_cpu(tp->t_cpupart->cp_cpulist,
			    tp->t_lpl, tpri, NULL);
		}
	} else {
		/*
		 * It is possible that t_weakbound_cpu != t_bound_cpu (for
		 * a short time until weak binding that existed when the
		 * strong binding was established has dropped) so we must
		 * favour weak binding over strong.
		 */
		cp = tp->t_weakbound_cpu ?
		    tp->t_weakbound_cpu : tp->t_bound_cpu;
	}

	/*
	 * The selected CPU must not be quiesced.
	 */
	ASSERT((cp->cpu_flags & CPU_QUIESCED) == 0);

	dp = cp->cpu_disp;
	disp_lock_enter_high(&dp->disp_lock);

	DTRACE_SCHED3(enqueue, kthread_t *, tp, disp_t *, dp, int, 0);
	TRACE_3(TR_FAC_DISP, TR_BACKQ, "setbackdq:pri %d cpu %p tid %p",
	    tpri, cp, tp);

	dp->disp_nrunnable++;

	ASSERT(tpri >= 0 && tpri < dp->disp_npri);

	THREAD_RUN(tp, &dp->disp_lock);		/* set t_state to TS_RUN */
	tp->t_disp_queue = dp;
	tp->t_link = NULL;

	dq = &dp->disp_q[tpri];
	if (!bound)
		dp->disp_steal = 0;

	membar_enter();

	if (dq->dq_sruncnt++ != 0) {
		ASSERT(dq->dq_first != NULL);
		dq->dq_last->t_link = tp;
		dq->dq_last = tp;
	} else {
		ASSERT(dq->dq_first == NULL);
		ASSERT(dq->dq_last == NULL);
		dq->dq_first = dq->dq_last = tp;
		BT_SET(dp->disp_qactmap, tpri);
		if (tpri > dp->disp_maxrunpri) {
			dp->disp_maxrunpri = tpri;
			membar_enter();
			disp_cpu_resched(cp, tpri);
		}
	}

	if (!bound && tpri > dp->disp_max_unbound_pri) {
		if (self && dp->disp_max_unbound_pri == -1 && cp == CPU) {
			/*
			 * If there are no other unbound threads on the
			 * run queue, don't allow other CPUs to steal
			 * this thread while we are in the middle of a
			 * context switch. We may just switch to it
			 * again right away. CPU_DISP_CTX_SWITCH is cleared
			 * in swtch and swtch_to.
			 */
			cp->cpu_disp_flags |= CPU_DISP_CTX_SWITCH;
		}
		dp->disp_max_unbound_pri = tpri;
	}

	(*disp_enq_thread)(cp, bound);
}

/*
 * Put the specified thread on the front of the dispatcher queue corresponding
 * to its current priority. Called with the thread in transition, onproc or
 * stopped state and locked (transition implies locked) and at high spl.
 * Returns with the thread in TS_RUN state and still locked.
 */
void
setfrontdq(kthread_t *tp)
{
	disp_t *dp;
	dispq_t *dq;
	cpu_t *cp;
	pri_t tpri;
	boolean_t bound;
	hrtime_t hrt;

	ASSERT(THREAD_LOCK_HELD(tp));
	ASSERT((tp->t_schedflag & TS_ALLSTART) == 0);
	ASSERT(!thread_on_queue(tp));	/* make sure tp isn't on a runq */

	/*
	 * If thread is "swapped" or on the swap queue don't enqueue it, but
	 * wake sched.
	 */
	if ((tp->t_schedflag & (TS_LOAD | TS_ON_SWAPQ)) != TS_LOAD) {
		disp_swapped_setrun(tp);
		return;
	}

	/*
	 * A thread that is ONPROC may be temporarily placed on the run queue
	 * but then chosen to run again by disp.  If the thread we're placing on
	 * the queue is in TS_ONPROC state, don't set its t_waitrq until a
	 * replacement process is actually scheduled in swtch().  In this
	 * situation, curthread is the only thread that could be in the ONPROC
	 * state.
	 */
	hrt = gethrtime_unscaled();
	(void) cpu_update_pct(tp, hrt);

	if ((tp != curthread) && (tp->t_waitrq == 0))
		tp->t_waitrq = hrt;

	thread_transience_update(tp);

	bound = (tp->t_bound_cpu || tp->t_weakbound_cpu);
	tpri = DISP_PRIO(tp);

	/*
	 * The thread's current CPU may be in the process of being quiesced,
	 * but not just yet.
	 */
	ASSERT((tp->t_cpu->cpu_flags & CPU_QUIESCED) == 0);

	if (ncpus == 1)
		cp = tp->t_cpu;
	else if (!bound) {
		if (tpri >= kpqpri) {
			setkpdq(tp, SETKP_FRONT);
			return;
		}

		cp = tp->t_cpu;

		if (tp->t_cpupart == cp->cpu_part) {
			/*
			 * We'll generally let this thread continue to run
			 * where it last ran, but will consider migration if:
			 * - the thread is transient;
			 * - the thread last ran outside it's home lgroup;
			 * - the CPU where it last ran is being offlined
			 *   (a thread_nomigrate() on that CPU relies on
			 *   this when forcing a preempt);
			 * - the thread isn't the highest priority thread where
			 *   it last ran, and it is considered not likely to
			 *   have significant cache warmth.
			 */
			if (T_TRANSIENT(tp) ||
			    !LGRP_CONTAINS_CPU(tp->t_lpl->lpl_lgrp, cp) ||
			    cp == cpu_inmotion) {
				cp = disp_lowpri_cpu(tp->t_cpu, tp->t_lpl,
				    tpri, (tp == curthread) ? cp : NULL);
			} else {
				if (tpri < cp->cpu_disp->disp_maxrunpri &&
				    !THREAD_HAS_CACHE_WARMTH(tp, hrt))
					cp = disp_lowpri_cpu(tp->t_cpu,
					    tp->t_lpl, tpri, NULL);
			}

			/*
			 * cmt_affinity() performs CMT load balancing much like
			 * cmt_balance(), except that it traverses the PG
			 * topology in the opposite direction. The purpose of
			 * this is to implement an affinity oriented version
			 * of load balancing.
			 */
			if (!T_TRANSIENT(tp))
				cp = cmt_affinity(tp, cp);
		} else {
			/*
			 * Migrate to a cpu in the new partition.
			 */
			cp = disp_lowpri_cpu(tp->t_cpupart->cp_cpulist,
			    tp->t_lpl, tpri, NULL);
		}
	} else {
		/*
		 * It is possible that t_weakbound_cpu != t_bound_cpu (for
		 * a short time until weak binding that existed when the
		 * strong binding was established has dropped) so we must
		 * favour weak binding over strong.
		 */
		cp = tp->t_weakbound_cpu ?
		    tp->t_weakbound_cpu : tp->t_bound_cpu;
	}

	/*
	 * The selected CPU must not be quiesced.
	 */
	ASSERT((cp->cpu_flags & CPU_QUIESCED) == 0);

	dp = cp->cpu_disp;
	disp_lock_enter_high(&dp->disp_lock);

	TRACE_2(TR_FAC_DISP, TR_FRONTQ, "frontq:pri %d tid %p", tpri, tp);
	DTRACE_SCHED3(enqueue, kthread_t *, tp, disp_t *, dp, int, 1);

	dp->disp_nrunnable++;

	ASSERT(tpri >= 0 && tpri < dp->disp_npri);

	THREAD_RUN(tp, &dp->disp_lock);		/* set TS_RUN state and lock */
	tp->t_disp_queue = dp;

	dq = &dp->disp_q[tpri];
	if (!bound)
		dp->disp_steal = 0;

	membar_enter();

	if (dq->dq_sruncnt++ != 0) {
		ASSERT(dq->dq_last != NULL);
		tp->t_link = dq->dq_first;
		dq->dq_first = tp;
	} else {
		ASSERT(dq->dq_last == NULL);
		ASSERT(dq->dq_first == NULL);
		tp->t_link = NULL;
		dq->dq_first = dq->dq_last = tp;
		BT_SET(dp->disp_qactmap, tpri);
		if (tpri > dp->disp_maxrunpri) {
			dp->disp_maxrunpri = tpri;
			membar_enter();
			disp_cpu_resched(cp, tpri);
		}
	}

	if (!bound && tpri > dp->disp_max_unbound_pri) {
		if (tp == curthread && dp->disp_max_unbound_pri == -1 &&
		    cp == CPU) {
			/*
			 * If there are no other unbound threads on the
			 * run queue, don't allow other CPUs to steal
			 * this thread while we are in the middle of a
			 * context switch. We may just switch to it
			 * again right away. CPU_DISP_CTX_SWITCH is cleared
			 * in swtch and swtch_to.
			 */
			cp->cpu_disp_flags |= CPU_DISP_CTX_SWITCH;
		}
		dp->disp_max_unbound_pri = tpri;
	}

	(*disp_enq_thread)(cp, bound);
}

/*
 * Put a high-priority unbound thread on the kp queue
 */
static void
setkpdq(kthread_t *tp, int borf)
{
	dispq_t	*dq;
	disp_t *dp;
	cpu_t *cp;
	pri_t tpri;

	tpri = DISP_PRIO(tp);

	dp = &tp->t_cpupart->cp_kp_queue;
	disp_lock_enter_high(&dp->disp_lock);

	TRACE_2(TR_FAC_DISP, TR_FRONTQ, "frontq:pri %d tid %p", tpri, tp);

	ASSERT(tpri >= 0 && tpri < dp->disp_npri);
	DTRACE_SCHED3(enqueue, kthread_t *, tp, disp_t *, dp, int, borf);
	THREAD_RUN(tp, &dp->disp_lock);		/* set t_state to TS_RUN */
	tp->t_disp_queue = dp;
	dp->disp_nrunnable++;
	dq = &dp->disp_q[tpri];

	if (dq->dq_sruncnt++ != 0) {
		if (borf == SETKP_BACK) {
			ASSERT(dq->dq_first != NULL);
			tp->t_link = NULL;
			dq->dq_last->t_link = tp;
			dq->dq_last = tp;
		} else {
			ASSERT(dq->dq_last != NULL);
			tp->t_link = dq->dq_first;
			dq->dq_first = tp;
		}
	} else {
		if (borf == SETKP_BACK) {
			ASSERT(dq->dq_first == NULL);
			ASSERT(dq->dq_last == NULL);
			dq->dq_first = dq->dq_last = tp;
		} else {
			ASSERT(dq->dq_last == NULL);
			ASSERT(dq->dq_first == NULL);
			tp->t_link = NULL;
			dq->dq_first = dq->dq_last = tp;
		}
		BT_SET(dp->disp_qactmap, tpri);
		if (tpri > dp->disp_max_unbound_pri)
			dp->disp_max_unbound_pri = tpri;
		if (tpri > dp->disp_maxrunpri) {
			dp->disp_maxrunpri = tpri;
			membar_enter();
		}
	}

	cp = tp->t_cpu;
	if (tp->t_cpupart != cp->cpu_part) {
		/* migrate to a cpu in the new partition */
		cp = tp->t_cpupart->cp_cpulist;
	}

	cp = disp_lowpri_cpu(cp, tp->t_lpl, tpri, NULL);
	disp_lock_enter_high(&cp->cpu_disp->disp_lock);
	ASSERT((cp->cpu_flags & CPU_QUIESCED) == 0);

	if (cp->cpu_chosen_level < tpri)
		cp->cpu_chosen_level = tpri;

	disp_cpu_resched(cp, tpri);
	disp_lock_exit_high(&cp->cpu_disp->disp_lock);

	(*disp_enq_thread)(cp, 0);
}

/*
 * Remove a thread from the dispatcher queue if it is on it.
 */
int
dispdeq(kthread_t *tp)
{
	disp_t		*dp;
	dispq_t		*dq;
	kthread_t	*rp;
	kthread_t	*trp;
	kthread_t	**ptp;
	int		tpri;

	ASSERT(THREAD_LOCK_HELD(tp));

	if (tp->t_state != TS_RUN)
		return (0);

	/*
	 * The thread is "swapped" or is on the swap queue and hence no longer
	 * on the run queue, so return true.
	 */
	if ((tp->t_schedflag & (TS_LOAD | TS_ON_SWAPQ)) != TS_LOAD)
		return (1);

	tpri = DISP_PRIO(tp);
	dp = tp->t_disp_queue;
	ASSERT(tpri < dp->disp_npri);
	dq = &dp->disp_q[tpri];
	ptp = &dq->dq_first;
	rp = *ptp;
	trp = NULL;

	ASSERT(dq->dq_last == NULL || dq->dq_last->t_link == NULL);

	/*
	 * Search for thread in queue.
	 * Double links would simplify this at the expense of disp/setrun.
	 */
	while (rp != tp && rp != NULL) {
		trp = rp;
		ptp = &trp->t_link;
		rp = trp->t_link;
	}

	if (rp == NULL)
		panic("dispdeq: thread not on queue");

	DTRACE_SCHED2(dequeue, kthread_t *, tp, disp_t *, dp);

	/*
	 * Found it so remove it from queue.
	 */
	if ((*ptp = rp->t_link) == NULL)
		dq->dq_last = trp;

	dp->disp_nrunnable--;

	if (--dq->dq_sruncnt == 0) {
		dp->disp_qactmap[tpri >> BT_ULSHIFT] &= ~BT_BIW(tpri);

		if (dp->disp_nrunnable == 0) {
			dp->disp_max_unbound_pri = -1;
			dp->disp_maxrunpri = -1;
		} else if (tpri == dp->disp_maxrunpri) {
			int ipri = bt_gethighbit(dp->disp_qactmap,
			    dp->disp_maxrunpri >> BT_ULSHIFT);

			if (ipri < dp->disp_max_unbound_pri)
				dp->disp_max_unbound_pri = ipri;

			dp->disp_maxrunpri = ipri;
		}
	}

	tp->t_link = NULL;
	THREAD_TRANSITION(tp);		/* put in intermediate state */

	return (1);
}

/*
 * dq_sruninc and dq_srundec are public functions for
 * incrementing/decrementing the sruncnts when a thread on
 * a dispatcher queue is made schedulable/unschedulable by
 * resetting the TS_LOAD flag.
 *
 * The caller MUST have the thread lock and therefore the dispatcher
 * queue lock so that the operation which changes
 * the flag, the operation that checks the status of the thread to
 * determine if it's on a disp queue AND the call to this function
 * are one atomic operation with respect to interrupts.
 */

/*
 * Called by sched AFTER TS_LOAD flag is set on a swapped, runnable thread.
 */
void
dq_sruninc(kthread_t *t)
{
	ASSERT(t->t_state == TS_RUN);
	ASSERT(t->t_schedflag & TS_LOAD);

	THREAD_TRANSITION(t);
	setfrontdq(t);
}

/*
 * See comment on calling conventions above.
 * Called by sched BEFORE TS_LOAD flag is cleared on a runnable thread.
 */
void
dq_srundec(kthread_t *t)
{
	ASSERT(t->t_schedflag & TS_LOAD);

	(void) dispdeq(t);
	disp_swapped_enq(t);
}

/*
 * Change the dispatcher lock of thread to the "swapped_lock"
 * and return with thread lock still held.
 *
 * Called with thread_lock held, in transition state, and at high spl.
 */
void
disp_swapped_enq(kthread_t *tp)
{
	ASSERT(THREAD_LOCK_HELD(tp));
	ASSERT(tp->t_schedflag & TS_LOAD);

	switch (tp->t_state) {
	case TS_RUN:
		disp_lock_enter_high(&swapped_lock);
		THREAD_SWAP(tp, &swapped_lock);	/* set TS_RUN state and lock */
		break;
	case TS_ONPROC:
		disp_lock_enter_high(&swapped_lock);
		THREAD_TRANSITION(tp);
		wake_sched_sec = 1;		/* tell clock to wake sched */
		THREAD_SWAP(tp, &swapped_lock);	/* set TS_RUN state and lock */
		break;
	default:
		panic("disp_swapped: tp: %p bad t_state", (void *)tp);
	}
}

/*
 * This routine is called by setbackdq/setfrontdq if the thread is
 * not loaded or loaded and on the swap queue.
 *
 * Thread state TS_SLEEP implies that a swapped thread
 * has been woken up and needs to be swapped in by the swapper.
 *
 * Thread state TS_RUN, it implies that the priority of a swapped
 * thread is being increased by scheduling class (e.g. ts_update).
 */
static void
disp_swapped_setrun(kthread_t *tp)
{
	ASSERT(THREAD_LOCK_HELD(tp));
	ASSERT((tp->t_schedflag & (TS_LOAD | TS_ON_SWAPQ)) != TS_LOAD);

	switch (tp->t_state) {
	case TS_SLEEP:
		disp_lock_enter_high(&swapped_lock);
		/*
		 * Wakeup sched immediately (i.e., next tick) if the
		 * thread priority is above maxclsyspri.
		 */
		if (DISP_PRIO(tp) > maxclsyspri)
			wake_sched = 1;
		else
			wake_sched_sec = 1;
		THREAD_RUN(tp, &swapped_lock); /* set TS_RUN state and lock */
		break;
	case TS_RUN:				/* called from ts_update */
		break;
	default:
		panic("disp_swapped_setrun: tp: %p bad t_state", (void *)tp);
	}
}

/*
 *	Make a thread give up its processor.  Find the processor on
 *	which this thread is executing, and have that processor
 *	preempt.
 *
 *	We allow System Duty Cycle (SDC) threads to be preempted even if
 *	they are running at kernel priorities.  To implement this, we always
 *	set cpu_kprunrun; this ensures preempt() will be called.  Since SDC
 *	calls cpu_surrender() very often, we only preempt if there is anyone
 *	competing with us.
 */
void
cpu_surrender(kthread_t *tp)
{
	cpu_t	*cpup;
	int	max_pri;
	int	max_run_pri;
	klwp_t	*lwp;

	ASSERT(THREAD_LOCK_HELD(tp));

	if (tp->t_state != TS_ONPROC)
		return;
	cpup = tp->t_disp_queue->disp_cpu;	/* CPU thread dispatched to */
	max_pri = cpup->cpu_disp->disp_maxrunpri; /* best pri of that CPU */
	max_run_pri = CP_MAXRUNPRI(cpup->cpu_part);
	if (max_pri < max_run_pri)
		max_pri = max_run_pri;

	if (tp->t_cid == sysdccid) {
		uint_t t_pri = DISP_PRIO(tp);
		if (t_pri > max_pri)
			return;		/* we are not competing w/ anyone */
		cpup->cpu_runrun = cpup->cpu_kprunrun = 1;
	} else {
		cpup->cpu_runrun = 1;
		if (max_pri >= kpreemptpri && cpup->cpu_kprunrun == 0) {
			cpup->cpu_kprunrun = 1;
		}
	}

	/*
	 * Propagate cpu_runrun, and cpu_kprunrun to global visibility.
	 */
	membar_enter();

	DTRACE_SCHED1(surrender, kthread_t *, tp);

	/*
	 * Make the target thread take an excursion through trap()
	 * to do preempt() (unless we're already in trap or post_syscall,
	 * calling cpu_surrender via CL_TRAPRET).
	 */
	if (tp != curthread || (lwp = tp->t_lwp) == NULL ||
	    lwp->lwp_state != LWP_USER) {
		aston(tp);
		if (cpup != CPU)
			poke_cpu(cpup->cpu_id);
	}

	TRACE_2(TR_FAC_DISP, TR_CPU_SURRENDER,
	    "cpu_surrender:tid %p cpu %p", tp, cpup);
}

/*
 * Commit to and ratify a scheduling decision
 */
/*ARGSUSED*/
static kthread_t *
disp_ratify(kthread_t *tp, disp_t *kpq)
{
	pri_t	tpri, maxpri;
	pri_t	maxkpri;
	cpu_t	*cpup;

	ASSERT(tp != NULL);
	/*
	 * Commit to, then ratify scheduling decision
	 */
	cpup = CPU;
	if (cpup->cpu_runrun != 0)
		cpup->cpu_runrun = 0;
	if (cpup->cpu_kprunrun != 0)
		cpup->cpu_kprunrun = 0;
	if (cpup->cpu_chosen_level != -1)
		cpup->cpu_chosen_level = -1;
	membar_enter();
	tpri = DISP_PRIO(tp);
	maxpri = cpup->cpu_disp->disp_maxrunpri;
	maxkpri = kpq->disp_maxrunpri;
	if (maxpri < maxkpri)
		maxpri = maxkpri;
	if (tpri < maxpri) {
		/*
		 * should have done better
		 * put this one back and indicate to try again
		 */
		cpup->cpu_dispthread = curthread;	/* fixup dispthread */
		cpup->cpu_dispatch_pri = DISP_PRIO(curthread);
		thread_lock_high(tp);
		THREAD_TRANSITION(tp);
		setfrontdq(tp);
		thread_unlock_nopreempt(tp);

		tp = NULL;
	}
	return (tp);
}

/*
 * See if there is any work on the dispatcher queue for other CPUs.
 * If there is, dequeue the best thread and return.
 */
static kthread_t *
disp_getwork(cpu_t *cp)
{
	cpu_t *ocp, *ocp_start, *tcp = NULL;
	kthread_t *tp, *retval = NULL;
	pri_t maxpri = -1;
	disp_t *kpq;			/* kp queue for this partition */
	lpl_t *lpl, *lpl_leaf;
	int leafidx, startidx;
	hrtime_t stealtime;
	lgrp_id_t local_id;
	volatile int *local_nrunnable = &cp->cpu_disp->disp_nrunnable;

	/*
	 * A CPU can only look for work for itself, not others.
	 */
	ASSERT(CPU == cp);

	kpq = &cp->cpu_part->cp_kp_queue;
	while (kpq->disp_maxrunpri >= 0) {
		/*
		 * Try to take a thread from this partition's kpq.
		 */
		if ((tp = disp_getkpq(kpq)) != NULL)
			return (disp_ratify(tp, kpq));
	}

	kpreempt_disable();		/* protect the cpu_active list */

	/*
	 * Try to find something to do on another CPU's run queue.
	 * Loop through all other CPUs looking for the one with the highest
	 * priority unbound thread.
	 *
	 * On NUMA machines, the partition's CPUs are consulted in order of
	 * distance from the current CPU. This way, the first available
	 * work found is also the closest, and will suffer the least
	 * from being migrated.
	 */
	lpl = lpl_leaf = cp->cpu_lpl;
	local_id = lpl_leaf->lpl_lgrpid;
	leafidx = startidx = 0;

	/*
	 * This loop traverses the lpl hierarchy. Higher level lpls represent
	 * broader levels of locality
	 */
	do {
		/* This loop iterates over the lpl's leaves */
		do {
			if (lpl_leaf != cp->cpu_lpl)
				ocp = lpl_leaf->lpl_cpus;
			else
				ocp = cp->cpu_next_lpl;

			/* This loop iterates over the CPUs in the leaf */
			ocp_start = ocp;
			do {
				pri_t pri;

				ASSERT(CPU_ACTIVE(ocp));

				/*
				 * End our stroll around this lpl if:
				 *  - something became runnable on the local
				 *    queue;
				 *  - another CPU has work for us;
				 *  - we happen across another idle CPU. Since
				 *    it is patrolling the next portion of the
				 *    lpl's list (assuming it's not halted, or
				 *    busy servicing an interrupt), move to
				 *    the next higher level of locality.
				 */
				if (*local_nrunnable > 0) {
					kpreempt_enable();
					return (T_LOCAL_AVAIL);
				}

				if (cp->cpu_steal_from != NULL) {
					DTRACE_PROBE1(disp__tgt__steal,
					    cpu_t *, cp);
					tcp = (cpu_t *)cp->cpu_steal_from;
					cp->cpu_steal_from = NULL;

					goto found_work;
				}

				if (CPU_DISP_IDLE(ocp) &&
				    ocp->cpu_disp->disp_nrunnable == 0) {
					if (CPU_DISP_IS_HALTED(ocp) ||
					    ocp->cpu_intr_actv != 0)
						continue;
					else
						goto next_level;
				}

				/*
				 * Don't steal from this CPU if there's only
				 * one runnable thread in its run queue and:
				 *  - it's idle;
				 *  - switching context to it;
				 *  - running a transient thread;
				 */
				if (ocp->cpu_disp->disp_nrunnable == 1 && (
				    CPU_DISP_IDLE(ocp) ||
				    CPU_DISP_IS_CTX_SWITCH(ocp) ||
				    CPU_DISP_IS_TRANSIENT(ocp)))
					continue;

				pri = ocp->cpu_disp->disp_max_unbound_pri;
				if (pri > maxpri) {
					/*
					 * Don't steal threads that we attempted
					 * to steal recently until they're ready
					 * to be stolen again.
					 */
					stealtime = ocp->cpu_disp->disp_steal;
					if (stealtime == 0 ||
					    stealtime - gethrtime() <= 0) {
						maxpri = pri;
						tcp = ocp;
					} else {
						/*
						 * Don't update tcp, just set
						 * the retval to T_DONTSTEAL, so
						 * that if no acceptable CPUs
						 * are found the return value
						 * will be T_DONTSTEAL rather
						 * then NULL.
						 */
						retval = T_DONTSTEAL;
					}
				}
			} while ((ocp = ocp->cpu_next_lpl) != ocp_start);

			/*
			 * Iterate to the next leaf lpl in the resource set
			 * at this level of locality. If we hit the end of
			 * the set, wrap back around to the beginning.
			 *
			 * Note: This iteration is NULL terminated for a reason
			 * see lpl_topo_bootstrap() in lgrp.c for details.
			 */
			if ((lpl_leaf = lpl->lpl_rset[++leafidx]) == NULL) {
				leafidx = 0;
				lpl_leaf = lpl->lpl_rset[leafidx];
			}
		} while (leafidx != startidx);

next_level:
		/*
		 * Expand the search to include farther away CPUs (next
		 * locality level). The closer CPUs that have already been
		 * checked will be checked again. In doing so, idle CPUs
		 * will tend to be more aggresive about stealing from CPUs
		 * that are closer (since the closer CPUs will be considered
		 * more often).
		 * Begin at this level with the CPUs local leaf lpl.
		 */
		if ((lpl = lpl->lpl_parent) != NULL) {
			leafidx = startidx = lpl->lpl_id2rset[local_id];
			lpl_leaf = lpl->lpl_rset[leafidx];
		}
	} while (!tcp && lpl);

found_work:

	kpreempt_enable();

	/*
	 * If another queue looks good, and there is still nothing on
	 * the local queue, try to steal some work from there.
	 */
	if (tcp) {
		tp = disp_getbest(tcp->cpu_disp);
		if (tp == NULL || tp == T_DONTSTEAL || tp == T_LOCAL_AVAIL)
			return (tp);

		return (disp_ratify(tp, kpq));
	}

	return (retval);
}

/*
 * disp_fix_unbound_pri()
 *	Determines the maximum priority of unbound threads on the queue.
 *	The priority is kept for the queue, but is only increased, never
 *	reduced unless some CPU is looking for something on that queue.
 *
 *	The priority argument is the known upper limit.
 *
 *	Perhaps this should be kept accurately, but that probably means
 *	separate bitmaps for bound and unbound threads.  Since only idled
 *	CPUs will have to do this recalculation, it seems better this way.
 */
static void
disp_fix_unbound_pri(disp_t *dp, pri_t pri)
{
	kthread_t	*tp;
	dispq_t		*dq;
	ulong_t		*dqactmap = dp->disp_qactmap;
	ulong_t		mapword;
	int		wx;

	ASSERT(DISP_LOCK_HELD(&dp->disp_lock));

	ASSERT(pri >= 0);			/* checked by caller */

	/*
	 * Start the search at the next lowest priority below the supplied
	 * priority.  This depends on the bitmap implementation.
	 */
	do {
		wx = pri >> BT_ULSHIFT;		/* index of word in map */

		/*
		 * Form mask for all lower priorities in the word.
		 */
		mapword = dqactmap[wx] & (BT_BIW(pri) - 1);

		/*
		 * Get next lower active priority.
		 */
		if (mapword != 0) {
			pri = (wx << BT_ULSHIFT) + highbit(mapword) - 1;
		} else if (wx > 0) {
			pri = bt_gethighbit(dqactmap, wx - 1); /* sign extend */
			if (pri < 0)
				break;
		} else {
			pri = -1;
			break;
		}

		/*
		 * Search the queue for unbound, runnable threads.
		 */
		dq = &dp->disp_q[pri];
		tp = dq->dq_first;

		while (tp && (tp->t_bound_cpu || tp->t_weakbound_cpu)) {
			tp = tp->t_link;
		}

		/*
		 * If a thread was found, set the priority and return.
		 */
	} while (tp == NULL);

	/*
	 * pri holds the maximum unbound thread priority or -1.
	 */
	if (dp->disp_max_unbound_pri != pri)
		dp->disp_max_unbound_pri = pri;
}

/*
 * disp_adjust_unbound_pri() - thread is becoming unbound, so we should
 * 	check if the CPU to which is was previously bound should have
 * 	its disp_max_unbound_pri increased.
 */
void
disp_adjust_unbound_pri(kthread_t *tp)
{
	disp_t *dp;
	pri_t tpri;

	ASSERT(THREAD_LOCK_HELD(tp));

	/*
	 * Don't do anything if the thread is not bound, or
	 * currently not runnable or swapped out.
	 */
	if (tp->t_bound_cpu == NULL ||
	    tp->t_state != TS_RUN ||
	    tp->t_schedflag & TS_ON_SWAPQ)
		return;

	tpri = DISP_PRIO(tp);
	dp = tp->t_bound_cpu->cpu_disp;
	ASSERT(tpri >= 0 && tpri < dp->disp_npri);
	if (tpri > dp->disp_max_unbound_pri)
		dp->disp_max_unbound_pri = tpri;
}

/*
 * disp_getbest() traverses the specified disp queue for the highest priority
 * runnable thread, while trying to preserve the cache warmth of each thread
 * in the queue. If it finds a thread it can steal, disp_getbest() removes it
 * from it's disp queue and places it on the current CPU (unlocked and at
 * high SPL).
 *
 * This routine returns NULL if no thread was found, T_DONTSTEAL if it found
 * a thread but couldn't steal due to the nosteal_nsec protection or
 * T_LOCAL_AVAIL if it noticed that work appeared on our run queue.
 *
 * Note that disp_getkpq() should be used when the target disp queue is a
 * kernel preemption queue. disp_getbest() will trip an assertion if passed
 * one, and disp_getkpq() will do the same if passed a CPU's disp queue.
 */
static kthread_t *
disp_getbest(disp_t *dp)
{
	kthread_t *tp;
	dispq_t *dq;
	pri_t max_unbnd_pri;
	cpu_t *cp, *tcp;
	lgrp_id_t curr_lgrp, tgt_lgrp;
	boolean_t allbound = B_TRUE;
	volatile int *local_nrunnable;

	/*
	 * Assert that the disp queue we're looking at is not a kpq.
	 */
	ASSERT(dp->disp_cpu != NULL);

	disp_lock_enter(&dp->disp_lock);
	tcp = dp->disp_cpu;
	max_unbnd_pri = dp->disp_max_unbound_pri;

	/*
	 * Now that we have the disp lock, check that this run queue still has
	 * work that we might be able to steal.
	 */
	if (max_unbnd_pri == -1 || (dp->disp_nrunnable == 1 &&
	    (CPU_DISP_IDLE(tcp) || CPU_DISP_IS_CTX_SWITCH(tcp) ||
	    CPU_DISP_IS_TRANSIENT(tcp)))) {
		disp_lock_exit_nopreempt(&dp->disp_lock);
		return (NULL);
	}

	/*
	 * Make sure that disp_max_unbound_pri is pointing to a valid queue,
	 * adjusting it otherwise.
	 */
	if (dp->disp_q[max_unbnd_pri].dq_first == NULL) {
		disp_fix_unbound_pri(dp, max_unbnd_pri);
		if ((max_unbnd_pri = dp->disp_max_unbound_pri) == -1) {
			disp_lock_exit_nopreempt(&dp->disp_lock);
			return (NULL);
		}
	}

	dq = &dp->disp_q[max_unbnd_pri];
	cp = CPU;

	local_nrunnable = &cp->cpu_disp->disp_nrunnable;
	curr_lgrp = cp->cpu_lpl->lpl_lgrpid;
	tgt_lgrp = tcp->cpu_lpl->lpl_lgrpid;

	for (tp = dq->dq_first; tp != NULL; tp = tp->t_link) {
		hrtime_t now, nosteal, rqtime;

		/*
		 * Make sure there's nothing locally available.
		 */
		if (*local_nrunnable > 0) {
			disp_lock_exit_nopreempt(&dp->disp_lock);
			return (T_LOCAL_AVAIL);
		}

		/*
		 * Skip over bound threads which could be here even
		 * though disp_max_unbound_pri indicated this level.
		 */
		if (tp->t_bound_cpu || tp->t_weakbound_cpu)
			continue;

		/*
		 * We've got some unbound threads on this queue, so turn
		 * the allbound flag off now.
		 */
		allbound = B_FALSE;

		/*
		 * We avoid stealing threads that have strong affinity
		 * to the lgroup where they're enqueued on, and none to the
		 * current CPU's lgroup.
		 */
		if (tp->t_lgrp_aff && tp->t_lgrp_aff->aff != NULL &&
		    tp->t_lgrp_aff->aff[tgt_lgrp] == LGRP_AFF_STRONG &&
		    tp->t_lgrp_aff->aff[curr_lgrp] == LGRP_AFF_NONE)
			continue;

		/*
		 * This thread is a candidate for stealing. The following tests
		 * try to ensure that we won't be doing so prematurely and
		 * negatively impacting its cache warmth. These checks are
		 * arranged so that faster ones are executed earlier.
		 *
		 * We allow this thread to be stolen if:
		 *  - it's a transient system thread;
		 *  - it's not enqueued on a CPU that shares cache with the
		 *    CPU where it last ran on;
		 *  - it is enqueued on a CPU that shares cache with the CPU
		 *    trying to steal it (the current one).
		 */
		if ((T_TRANSIENT(tp) && CLASS_SYS(tp->t_cid)) ||
		    pg_cmt_can_migrate(tp->t_cpu, tcp) == B_FALSE ||
		    pg_cmt_can_migrate(cp, tcp) == B_TRUE)
			break;

		/*
		 * Check if the time based stealing protection is disabled.
		 */
		if ((nosteal = nosteal_nsec) == 0)
			break;

		/*
		 * Calculate the time spent sitting on this run queue.
		 */
		now = gethrtime_unscaled();
		rqtime = now - tp->t_waitrq;
		scalehrtime(&rqtime);

		/*
		 * Steal immediately if the time spent on this run queue is
		 * more than allowed by the nosteal delay.
		 *
		 * Negative rqtime check is needed here to avoid infinite
		 * stealing delays caused by unlikely but not impossible
		 * clock drifts between different CPUs.
		 */
		if (rqtime > nosteal || rqtime < 0)
			break;

		DTRACE_PROBE4(nosteal, kthread_t *, tp,
		    cpu_t *, tcp, cpu_t *, cp, hrtime_t, rqtime);

		/*
		 * Calculate when this thread will become stealable.
		 */
		scalehrtime(&now);
		now += (nosteal - rqtime);

		/*
		 * Calculate when other CPUs will be allowed to steal from
		 * this one.
		 */
		if (now < dp->disp_steal)
			dp->disp_steal = now;
	}

	/*
	 * If there were no unbound threads on this queue, find the queue
	 * where they are and then return later. The value of
	 * disp_max_unbound_pri is not always accurate because it isn't
	 * updated until another idle CPU tries to take work from this one.
	 */
	if (allbound)
		disp_fix_unbound_pri(dp, max_unbnd_pri);

	/*
	 * If we reached the end of the queue and found no unbound threads
	 * then return NULL so that other CPUs will be considered. If there
	 * are unbound threads but they cannot yet be stolen, then
	 * return T_DONTSTEAL and try again later.
	 */
	if (tp == NULL) {
		disp_lock_exit_nopreempt(&dp->disp_lock);
		return (allbound ? NULL : T_DONTSTEAL);
	}

	DTRACE_PROBE3(steal, kthread_t *, tp, cpu_t *, tcp, cpu_t *, cp);

	/*
	 * Found a runnable, unbound thread, so remove it from queue.
	 * dispdeq() requires that we have the thread locked, and we do,
	 * by virtue of holding the dispatch queue lock. dispdeq() will
	 * put the thread in transition state, thereby dropping the dispq
	 * lock.
	 */
#ifdef DEBUG
	{
		int thread_was_on_queue = dispdeq(tp);	/* drops disp_lock */
		ASSERT(thread_was_on_queue);
	}
#else /* DEBUG */
	(void) dispdeq(tp);			/* drops disp_lock */
#endif /* DEBUG */

	/*
	 * Reset the disp_queue steal time - we do not know what the shortest
	 * value across the queue is.
	 */
	dp->disp_steal = 0;

	tp->t_schedflag |= TS_DONT_SWAP;

	/*
	 * Setup thread to run on the current CPU.
	 */
	tp->t_disp_queue = cp->cpu_disp;
	cp->cpu_dispthread = tp;		/* protected by spl only */
	cp->cpu_dispatch_pri = max_unbnd_pri;

	/*
	 * There can be a memory synchronization race between disp_getbest()
	 * and disp_ratify() vs disp_cpu_resched() where disp_cpu_resched() is
	 * trying to preempt the current thread to run the enqueued thread while
	 * disp_getbest() and disp_ratify() are changing the current thread
	 * to the stolen thread. This may lead to a situation where
	 * disp_cpu_resched() tries to preempt the wrong thread and the
	 * stolen thread continues to run on the CPU which has been tagged
	 * for preemption.
	 * Later the clock thread gets enqueued but doesn't get to run on the
	 * CPU causing the system to hang.
	 *
	 * To avoid this, grabbing and dropping the disp_lock (which does
	 * a memory barrier) is needed to synchronize the execution of
	 * disp_cpu_resched() with disp_getbest() and disp_ratify() and
	 * synchronize the memory read and written by disp_cpu_resched(),
	 * disp_getbest(), and disp_ratify() with each other.
	 */
	disp_lock_enter_high(&cp->cpu_disp->disp_lock);
	disp_lock_exit_high(&cp->cpu_disp->disp_lock);

	ASSERT(max_unbnd_pri == DISP_PRIO(tp));

	thread_onproc(tp, cp);			/* set t_state to TS_ONPROC */

	/*
	 * Return with spl high so that swtch() won't need to raise it.
	 * The disp_lock was dropped by dispdeq().
	 */
	return (tp);
}

/*
 * Simpler, kernel preemption queue specific version of disp_getbest().
 */
static kthread_t *
disp_getkpq(disp_t *kpq)
{
	kthread_t *tp;
	dispq_t *dq;
	pri_t max_unbnd_pri;
	cpu_t *cp;

	/*
	 * Assert that the disp queue we're looking at is a kpq.
	 */
	ASSERT(kpq->disp_cpu == NULL);

	disp_lock_enter(&kpq->disp_lock);

	/*
	 * Check if this kpq is empty.
	 */
	if ((max_unbnd_pri = kpq->disp_max_unbound_pri) == -1) {
		disp_lock_exit_nopreempt(&kpq->disp_lock);
		return (NULL);
	}

	/*
	 * We have the disp lock and there's unbound work available. Since
	 * we don't enforce any affinity for threads in the kpq, simply take
	 * the first thread off the highest priority queue.
	 */
	dq = &kpq->disp_q[max_unbnd_pri];
	tp = dq->dq_first;
	ASSERT(tp != NULL);

	cp = CPU;

	DTRACE_PROBE3(steal, kthread_t *, tp, cpu_t *, NULL, cpu_t *, cp);

#ifdef DEBUG
	{
		int thread_was_on_queue = dispdeq(tp);	/* drops disp_lock */
		ASSERT(thread_was_on_queue);
	}
#else /* DEBUG */
	(void) dispdeq(tp);			/* drops disp_lock */
#endif /* DEBUG */

	tp->t_schedflag |= TS_DONT_SWAP;

	/*
	 * Setup the thread to run on the current CPU.
	 */
	tp->t_disp_queue = cp->cpu_disp;
	cp->cpu_dispthread = tp;		/* protected by spl only */
	cp->cpu_dispatch_pri = max_unbnd_pri;

	/*
	 * There can be a memory synchronization race between disp_getkpq()
	 * and disp_ratify() vs disp_cpu_resched() where disp_cpu_resched() is
	 * trying to preempt the current thread to run the enqueued thread while
	 * disp_getkpq() and disp_ratify() are changing the current thread
	 * to the stolen thread. This may lead to a situation where
	 * disp_cpu_resched() tries to preempt the wrong thread and the
	 * stolen thread continues to run on the CPU which has been tagged
	 * for preemption.
	 * Later the clock thread gets enqueued but doesn't get to run on the
	 * CPU causing the system to hang.
	 *
	 * To avoid this, grabbing and dropping the disp_lock (which does
	 * a memory barrier) is needed to synchronize the execution of
	 * disp_cpu_resched() with disp_getkpq() and disp_ratify() and
	 * synchronize the memory read and written by disp_cpu_resched(),
	 * disp_getkpq(), and disp_ratify() with each other.
	 */
	disp_lock_enter_high(&cp->cpu_disp->disp_lock);
	disp_lock_exit_high(&cp->cpu_disp->disp_lock);

	ASSERT(max_unbnd_pri == DISP_PRIO(tp));

	thread_onproc(tp, cp);			/* set t_state to TS_ONPROC */

	/*
	 * Return with spl high so that swtch() won't need to raise it.
	 * The disp_lock was dropped by dispdeq().
	 */
	return (tp);
}

/*
 * disp_bound_check() searches for threads that are bound to the specified
 * processor, returning a pointer to the first thread it finds and NULL
 * otherwise.
 *
 * If 'threadlistsafe' is set then there is no need to acquire pidlock to
 * prevent the thread list from changing. This is used, for example, when
 * CPUs are paused.
 */
kthread_t *
disp_bound_check(cpu_t *cp, boolean_t threadlistsafe, uint_t flag)
{
	kthread_t *tp, *found = NULL;

	ASSERT(flag);

	if (threadlistsafe == B_FALSE)
		mutex_enter(&pidlock);

	tp = curthread;		/* faster than allthreads */

	do {
		if (tp->t_state != TS_FREE) {
			/*
			 * If an interrupt thread is busy, but the
			 * caller doesn't care (i.e. BOUND_INTR is off),
			 * then just ignore it and continue through.
			 */
			if ((tp->t_flag & T_INTR_THREAD) &&
			    !(flag & DISP_BOUND_INTR))
				continue;

			/*
			 * Skip the idle thread for the CPU
			 * we're about to set offline.
			 */
			if (tp == cp->cpu_idle_thread)
				continue;

			/*
			 * Skip the pause thread for the CPU
			 * we're about to set offline.
			 */
			if (tp == cp->cpu_pause_thread)
				continue;

			if (flag & DISP_BOUND_CPU) {
				/*
				 * Check if we should skip a hard bound thread
				 * with affinity set. Note that such thread
				 * must not be user bound to this CPU.
				 */
				if ((flag & DISP_BOUND_SKIP_AFF) &&
				    tp->t_affinitycnt > 0 &&
				    tp->t_bound_cpu == cp &&
				    tp->t_bind_cpu != cp->cpu_id)
					continue;

				if (tp->t_bound_cpu == cp ||
				    tp->t_bind_cpu == cp->cpu_id ||
				    tp->t_weakbound_cpu == cp) {
					found = tp;
					break;
				}
			}

			if ((flag & DISP_BOUND_PART) &&
			    (tp->t_cpupart == cp->cpu_part)) {
				found = tp;
				break;
			}
		}
	} while ((tp = tp->t_next) != curthread && found == NULL);

	if (threadlistsafe == B_FALSE)
		mutex_exit(&pidlock);

	return (found);
}

/*
 * disp_cpu_inactive() makes a CPU inactive by moving all of its unbound
 * threads to other CPUs.
 */
void
disp_cpu_inactive(cpu_t *cp)
{
	kthread_t	*tp;
	disp_t		*dp = cp->cpu_disp;
	dispq_t		*dq;
	pri_t		pri;
	int		wasonq;

	disp_lock_enter(&dp->disp_lock);
	while ((pri = dp->disp_max_unbound_pri) != -1) {
		dq = &dp->disp_q[pri];
		tp = dq->dq_first;

		/*
		 * Skip over bound threads.
		 */
		while (tp != NULL && tp->t_bound_cpu != NULL) {
			tp = tp->t_link;
		}

		if (tp == NULL) {
			/* disp_max_unbound_pri must be inaccurate, so fix it */
			disp_fix_unbound_pri(dp, pri);
			continue;
		}

		wasonq = dispdeq(tp);		/* drops disp_lock */
		ASSERT(wasonq);
		ASSERT(tp->t_weakbound_cpu == NULL);

		setbackdq(tp);
		/*
		 * Called from cpu_offline:
		 *
		 * cp has already been removed from the list of active cpus
		 * and tp->t_cpu has been changed so there is no risk of
		 * tp ending up back on cp.
		 *
		 * Called from cpupart_move_cpu:
		 *
		 * The cpu has moved to a new cpupart.  Any threads that
		 * were on it's dispatch queues before the move remain
		 * in the old partition and can't run in the new partition.
		 */
		ASSERT(tp->t_cpu != cp);
		thread_unlock(tp);

		disp_lock_enter(&dp->disp_lock);
	}
	disp_lock_exit(&dp->disp_lock);
}

/*
 * disp_lowpri_cpu - find CPU running the lowest priority thread.
 *	The hint passed in is used as a starting point so we don't favor
 *	CPU 0 or any other CPU.  The caller should pass in the most recently
 *	used CPU for the thread.
 *
 *	The lgroup and priority are used to determine the best CPU to run on
 *	in a NUMA machine.  The lgroup specifies which CPUs are closest while
 *	the thread priority will indicate whether the thread will actually run
 *	there.  To pick the best CPU, the CPUs inside and outside of the given
 *	lgroup which are running the lowest priority threads are found.  The
 *	remote CPU is chosen only if the thread will not run locally on a CPU
 *	within the lgroup, but will run on the remote CPU. If the thread
 *	cannot immediately run on any CPU, the best local CPU will be chosen.
 *
 *	The lpl specified also identifies the cpu partition from which
 *	disp_lowpri_cpu should select a CPU.
 *
 *	curcpu is used to indicate that disp_lowpri_cpu is being called on
 *      behalf of the current thread. (curthread is looking for a new cpu)
 *      In this case, cpu_dispatch_pri for this thread's cpu should be
 *      ignored.
 *
 *      If a cpu is the target of an offline request then try to avoid it.
 *
 *	This function must be called at either high SPL, or with preemption
 *	disabled, so that the "hint" CPU cannot be removed from the online
 *	CPU list while we are traversing it.
 */
cpu_t *
disp_lowpri_cpu(cpu_t *hint, lpl_t *lpl, pri_t tpri, cpu_t *curcpu)
{
	cpu_t *bestcpu, *besthomecpu, *cp, *cpstart;
	pri_t bestpri, cpupri;
	klgrpset_t done, cur_set;
	lpl_t *lpl_iter, *lpl_leaf;
	int bestnrun, idx, start;

	/*
	 * Scan for a CPU currently running the lowest priority thread.
	 * Cannot get cpu_lock here because it is adaptive.
	 * We do not require lock on CPU list.
	 */
	ASSERT(hint != NULL);
	ASSERT(lpl != NULL);
	ASSERT(lpl->lpl_ncpu > 0);

	/*
	 * First examine local CPUs. Note that it's possible the hint CPU
	 * passed in is remote to the specified home lgroup. If our priority
	 * isn't sufficient enough such that we can run immediately at home,
	 * then examine CPUs remote to our home lgroup.
	 * We would like to give preference to CPUs closest to "home".
	 * If we can't find a CPU where we'll run at a given level
	 * of locality, we expand our search to include the next level.
	 */
	bestcpu = besthomecpu = NULL;
	klgrpset_clear(done);

	lpl_iter = lpl;

	do {
		bestpri = SHRT_MAX;
		klgrpset_clear(cur_set);

		ASSERT(lpl_iter->lpl_nrset > 0);
		idx = start = CPU_PSEUDO_RANDOM() % lpl_iter->lpl_nrset;

		do {
			lpl_leaf = lpl_iter->lpl_rset[idx];
			if (klgrpset_ismember(done, lpl_leaf->lpl_lgrpid))
				goto next_level;

			klgrpset_add(cur_set, lpl_leaf->lpl_lgrpid);

			/*
			 * Start from the hint CPU if it's in this lpl or
			 * the CPU list if not.
			 */
			if (hint->cpu_lpl == lpl_leaf)
				cp = cpstart = hint;
			else
				cp = cpstart = lpl_leaf->lpl_cpus;

			do {
				/*
				 * Favor the current CPU if we come around
				 * it during a search.
				 */
				if (cp == curcpu) {
					cpupri = -1;
				} else {
					/*
					 * If the CPU is in motion, make it a
					 * less interesting scheduling target.
					 * We set it to (SHRT_MAX - 1) in case
					 * this CPU is the only one we'll
					 * iterate over.
					 */
					if (cp == cpu_inmotion)
						cpupri = SHRT_MAX - 1;
					else
						cpupri = cp->cpu_dispatch_pri;
				}

				if (cp->cpu_disp->disp_maxrunpri > cpupri)
					cpupri = cp->cpu_disp->disp_maxrunpri;

				if (cp->cpu_chosen_level > cpupri)
					cpupri = cp->cpu_chosen_level;

				if (cpupri < bestpri) {
					if (cpupri == -1 &&
					    cp->cpu_disp->disp_nrunnable == 0) {
						ASSERT((cp->cpu_flags &
						    CPU_QUIESCED) == 0);
						return (cp);
					}

					bestcpu = cp;
					bestpri = cpupri;
					bestnrun = cp->cpu_disp->disp_nrunnable;
				}

				/*
				 * If the current CPU is at the same priority
				 * as the best CPU, check which one has less
				 * threads in its run queue.
				 */
				if (cpupri == bestpri) {
					if (cp->cpu_disp->disp_nrunnable <
					    bestnrun)
						bestcpu = cp;
				}

			} while ((cp = cp->cpu_next_lpl) != cpstart);
next_level:
			if (--idx < 0)
				idx = lpl_iter->lpl_nrset - 1;

		} while (idx != start);

		if (bestcpu && (tpri > bestpri)) {
			ASSERT((bestcpu->cpu_flags & CPU_QUIESCED) == 0);
			return (bestcpu);
		}

		if (besthomecpu == NULL)
			besthomecpu = bestcpu;

		/*
		 * Add the lgrps we just considered to the "done" set
		 */
		klgrpset_or(done, cur_set);

	} while ((lpl_iter = lpl_iter->lpl_parent) != NULL);

	/*
	 * The specified priority isn't high enough to run immediately
	 * anywhere, so just return the best CPU from the home lgroup.
	 */
	ASSERT(besthomecpu != NULL);
	ASSERT((besthomecpu->cpu_flags & CPU_QUIESCED) == 0);
	return (besthomecpu);
}

/*
 * This routine provides the generic idle CPU function for all processors.
 * If a processor has some specific code to execute when idle (say, to stop
 * the pipeline and save power) then that routine should be defined in the
 * processors specific code (module_xx.c) and the global variable idle_cpu
 * set to that function.
 */
static void
disp_idle_generic(void)
{
}

/*ARGSUSED*/
static void
generic_enq_thread(cpu_t *cpu, int bound)
{
}
