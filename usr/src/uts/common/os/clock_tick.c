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
 * Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/thread.h>
#include <sys/proc.h>
#include <sys/task.h>
#include <sys/cmn_err.h>
#include <sys/class.h>
#include <sys/sdt.h>
#include <sys/atomic.h>
#include <sys/cpu.h>
#include <sys/clock_tick.h>
#include <sys/clock_impl.h>
#include <sys/sysmacros.h>
#include <vm/rm.h>

/*
 * This file contains the implementation of CPU time accounting for LWPs,
 * processes, tasks, projects and zones.
 *
 * LWP CPU time accounting (or just LWP accounting) is performed every
 * clock tick using a sampling methodology. That is, each CPU in the system
 * is sampled to check if it is running an LWP. If so, the LWP is charged
 * with one clock tick of CPU time. Along with this, its process, task,
 * project and zone are also charged appropriately.
 *
 * Historically, the CPU sampling and processing were done from a single
 * handler, the clock(). This had obvious scalability issues when the
 * number of CPUs in the system was large. The problem was exacerbated
 * whenever the clock resolution was set to high (hires_tick=1).
 *
 * This file contains a multi-threaded implementation of LWP accounting for
 * scalability. CPUs are divided into accounting sets. Each accounting
 * set is processed by a separate handler in softint context.
 *
 * LWP accounting happens each tick in two phases:
 *
 * Tick scheduling	clock() calls clock_tick_schedule() to select CPUs
 *			for performing LWP accounting, X-call those CPUs and
 *			post a softint on those CPUs to perform accounting.
 *
 * Tick execution	Each softint handler executes clock_tick_execute() to
 *			perform LWP accounting.
 *
 * The size of an accounting set is controlled by the following variable:
 *
 *	int			clock_tick_ncpus;
 *
 * This variable defaults to a value of 64. A platform may choose to set this
 * variable to a value that is appropriate for the platform. This variable
 * is not a documented tuneable. However, should a customer system run into
 * an issue with the accounting set size, it may be set from /etc/system
 * to whatever is suitable. This variable takes effect only at boot time. It
 * should not be dynamically changed using mdb, etc.
 *
 * The decision to multi-thread LWP accounting is controlled by this variable:
 *
 *	int			clock_tick_threshold;
 *
 * On a system that has only clock_tick_threshold CPUs or less, X-calls are not
 * issued. clock_tick_schedule() performs all the LWP accounting in situ. This
 * variable defaults to a value of max_ncpus (i.e., all the CPUs in the system).
 * A platform may choose to set this variable to a value that is appropriate
 * for the platform. This variable is not a documented tuneable. However,
 * should a customer system run into an issue with the threshold, it may be set
 * from /etc/system to whatever is suitable. This variable may be dynamically
 * changed using mdb, etc. If a CPU is onlined and the threshold is crossed,
 * LWP accounting becomes multi-threaded. If a CPU is offlined and the
 * threshold is crossed, LWP accounting becomes single-threaded.
 *
 * The implementation is guided by an accounting policy that can have two
 * settings:
 *
 * CLOCK_TICK_POLICY_PERFORMANCE
 *	This setting favors performance over power savings.
 *
 * CLOCK_TICK_POLICY_POWER
 *	This setting favors power savings over performance.
 *
 * This implementation uses X-calls. So, it must take some measures to
 * preserve performance as well as be power friendly. The following measures
 * have been implemented in this context:
 *
 *	- The CPUs that perform LWP accounting each tick cycle are chosen
 *	  to be adjacent to each other in the online CPUs list. This allows
 *	  accounting data to be cached and used across the handlers. This
 *	  also disturbs the smallest number of power domains.
 *
 *	- In the performance setting, CPUs are selected from the list of
 *	  online CPUs on a rotational basis so that all the CPUs in the
 *	  system participate in LWP accounting and share the load over a
 *	  period of time. No one CPU does more work because of accounting.
 *
 *	- In the power setting, CPUs are always chosen to be adjacent to
 *	  the clock() CPU. The power domain in which clock() runs will never
 *	  be in the idle state because of the clock(). So, it makes sense to
 *	  use atleast some CPUs in the same power domain to perform LWP
 *	  accounting.
 *
 *	- In the power setting, the CPUs in an accounting set are checked for
 *	  idleness. If they are all known to be idle, then no X-call is issued
 *	  for that set.
 *
 *	- If a system contains clock_tick_threshold CPUs or less, no X-calls
 *	  are issued.
 *
 * An API has been defined for the power manager to set the accounting policy.
 * The default policy favors performance. The policy can also be set from
 * /etc/system via clock_tick_policy. (although this is not a documented
 * tuneable).
 */

/*
 * clock_tick_threshold
 *	If the number of CPUs exceeds this threshold, multi-threaded LWP
 *	accounting kicks in.
 *
 * clock_tick_ncpus
 *	The number of CPUs in a set. Each set is scheduled for tick execution
 *	on a separate processor.
 *
 * clock_tick_total_cpus
 *	Total number of online CPUs.
 *
 * clock_tick_cpus
 *	Array of online CPU pointers.
 *
 * clock_tick_cpu
 *	Per-CPU, cache-aligned data structures to facilitate multi-threading.
 *
 * clock_tick_pending
 *	Number of pending ticks that need to be accounted by the softint
 *	handlers.
 *
 * clock_tick_lock
 *	Mutex to synchronize between clock_tick_schedule(),
 *	clock_tick_set_policy() and CPU online/offline. The locking order is
 *	cpu_lock --> clock_tick_lock --> ct_lock.
 *
 * clock_cpu_id
 *	CPU id of the clock() CPU. Used to detect when the clock CPU
 *	is offlined.
 *
 * clock_tick_online_cpuset
 *	CPU set of all online processors that can be X-called.
 *
 * clock_tick_set
 *	Per-set structures. Each structure contains the range of CPUs
 *	to be processed for the set.
 *
 * clock_tick_nsets;
 *	Number of sets.
 *
 * clock_tick_scan
 *	Where to begin the scan for single-threaded mode. In multi-threaded,
 *	the clock_tick_set itself contains a field for this.
 *
 * clock_tick_policy
 *	Power policy.
 *
 * clock_tick_max_sets
 *	Maximum number of sets.
 *
 * clock_tick_highest_set
 *	Highest configured accounting set.
 */
int			clock_tick_threshold;
int			clock_tick_ncpus;
int			clock_tick_total_cpus;
cpu_t			*clock_tick_cpus[NCPU];
clock_tick_cpu_pad_t	*clock_tick_cpu;
int			clock_tick_pending;
kmutex_t		clock_tick_lock;
processorid_t		clock_cpu_id;
cpuset_t		clock_tick_online_cpuset;
clock_tick_set_pad_t	*clock_tick_set;
int			clock_tick_nsets;
int			clock_tick_scan;
int			clock_tick_policy = CLOCK_TICK_POLICY_PERFORMANCE;
int			clock_tick_max_sets;
int			clock_tick_highest_set;
ulong_t			clock_tick_intr;

static uint_t	clock_tick_execute(caddr_t, caddr_t);
static void	clock_tick_execute_common(int, int, int, int, clock_t, int);

void *
clock_tick_alloc(size_t size, size_t *offset)
{
	uintptr_t	buf1, buf2;

	size += CPU_CACHE_COHERENCE_SIZE;
	buf1 = (uintptr_t)kmem_zalloc(size, KM_SLEEP);
	buf2 = P2ROUNDUP(buf1, CPU_CACHE_COHERENCE_SIZE);
	*offset = buf2 - buf1;

	return ((void *)buf2);
}

void
clock_tick_free(void *buf, size_t offset, size_t size)
{
	uintptr_t	buf1;

	size += CPU_CACHE_COHERENCE_SIZE;
	buf1 = (uintptr_t)buf;
	buf1 -= offset;

	kmem_free((void *)buf1, size);
}

void
clock_tick_init(void)
{
	int			i, n;
	clock_tick_set_t	*csp;
	size_t			size, offset;

	size = sizeof (clock_tick_cpu_pad_t) * NCPU;
	clock_tick_cpu = clock_tick_alloc(size, &offset);

	mutex_init(&clock_tick_lock, NULL, MUTEX_DEFAULT, NULL);

	/*
	 * Compute clock_tick_ncpus here. We need it to compute the
	 * maximum number of accounting sets we need to support.
	 */
	ASSERT(clock_tick_ncpus >= 0);
	if (clock_tick_ncpus == 0)
		clock_tick_ncpus = CLOCK_TICK_NCPUS;
	if (clock_tick_ncpus > max_ncpus)
		clock_tick_ncpus = max_ncpus;

	if (clock_tick_threshold == 0)
		clock_tick_threshold = max_ncpus;

	/*
	 * Allocate and initialize the accounting sets.
	 */
	n = (max_ncpus + clock_tick_ncpus - 1)/clock_tick_ncpus;
	size = sizeof (clock_tick_set_pad_t) * n;
	clock_tick_set = clock_tick_alloc(size, &offset);

	for (i = 0; i < n; i++) {
		csp = &clock_tick_set[i].cs_data;
		csp->ct_start = i * clock_tick_ncpus;
		csp->ct_scan = csp->ct_start;
		csp->ct_end = csp->ct_start;
		bitset_init_fanout(&csp->ct_bitset, cp_haltset_fanout);
		bitset_resize(&csp->ct_bitset, max_ncpus);
	}
	clock_tick_max_sets = n;

	if ((clock_tick_policy < 0) ||
	    (clock_tick_policy >= CLOCK_TICK_MAX_POLICIES))
		clock_tick_policy = CLOCK_TICK_POLICY_PERFORMANCE;
	clock_tick_highest_set = -1;
}

void
clock_tick_intr_init(void)
{
	/*
	 * Create the softint needed to perform LWP accounting on different
	 * CPUs.
	 */
	clock_tick_intr = create_softint(LOCK_LEVEL,
	    clock_tick_execute, (caddr_t)NULL, "clock_tick_softint");
}

static void
clock_tick_schedule_one(int set, int pending, processorid_t cid)
{
	clock_tick_set_t	*csp;
	clock_tick_cpu_t	*ctp;

	csp = &clock_tick_set[set].cs_data;
	atomic_inc_ulong(&csp->ct_active);

	/*
	 * Schedule tick accounting for a set of CPUs.
	 */
	ctp = &clock_tick_cpu[cid].ct_data;
	mutex_enter(&ctp->ct_lock);
	ctp->ct_lbolt = LBOLT_NO_ACCOUNT;
	ctp->ct_pending += pending;
	ctp->ct_start = csp->ct_start;
	ctp->ct_end = csp->ct_end;
	ctp->ct_scan = csp->ct_scan;
	ctp->ct_set = set;
	mutex_exit(&ctp->ct_lock);

	invoke_softint(cid, clock_tick_intr);
	/*
	 * Return without waiting for the softint to finish.
	 */
}

/*
 * Handle clock tick processing for a thread.
 * Check for timer action, enforce CPU rlimit, do profiling etc.
 */
void
clock_tick(int set, kthread_t *t, int pending)
{
	struct proc	*pp;
	klwp_id_t	lwp;
	int		poke = 0;		/* notify another CPU */
	lwp_ac_t	*lac;
	task_t		*tk;
	int		need_plock, quantum, virtual, prof, prctl, trctl, i;
	uint64_t	buffered;

	ASSERT(pending > 0);

	/* Must be operating on a lwp/thread */
	if ((lwp = ttolwp(t)) == NULL) {
		panic("clock_tick: no lwp");
		/*NOTREACHED*/
	}

	/*
	 * Increment the number of ticks of CPU time consumed, both user time
	 * and user+sys.
	 */
	lac = &lwp->lwp_ac;
	lac->ac_clock += pending;
	if (lwp->lwp_state == LWP_USER)
		lac->ac_uclock += pending;

	/*
	 * For the buffered clients (see below), we need to update only once
	 * every lwp_ac_bticks.
	 */
	buffered = lac->ac_clock - lac->ac_bclock;
	if (buffered >= lwp_ac_bticks)
		lac->ac_bclock = lac->ac_clock;

	need_plock = quantum = virtual = prof = prctl = trctl = 0;

	/*
	 * Perform class-specific tick processing. If the process lock
	 * is needed, then wait until we get the process lock later.
	 */
	if (t->t_acflag & TA_NO_PROCESS_LOCK) {
		for (i = 0; i < pending; i++) {
			CL_TICK(t);	/* Class specific tick processing */
			DTRACE_SCHED1(tick, kthread_t *, t);
		}
	} else {
		quantum = 1;
		need_plock = 1;
	}

	pp = ttoproc(t);

	/*
	 * Update user profiling statistics. Get the pc from the
	 * lwp when the AST happens.
	 */
	poke |= PROFIL_POLL(pp, lwp, pending);

	/*
	 * If CPU was in user state, process lwp-virtual time
	 * interval timer. If the timer has expired, we need to get the
	 * process lock later and process it.
	 */
	if (lwp->lwp_ac_timer[ITIMER_VIRTUAL].lac_timer <= lac->ac_uclock) {
		virtual = 1;
		need_plock = 1;
	}

	/*
	 * Process lwp-profile interval timer. If the timer has expired, we
	 * need to get the process lock later and process it.
	 */
	if (lwp->lwp_ac_timer[ITIMER_PROF].lac_timer <= lac->ac_clock) {
		prof = 1;
		need_plock = 1;
	}

	/*
	 * Do the buffered clients.
	 */
	if (buffered >= lwp_ac_bticks) {
		/*
		 * Enforce CPU resource controls:
		 *   (a) process.max-cpu-time resource control
		 */
		if (proc_cpu_time_add(pp, set, buffered)) {
			prctl = 1;
			need_plock = 1;
		}

		/*
		 *   (b) task.max-cpu-time resource control
		 */
		tk = ttotask(t);
		if ((tk != NULL) && task_cpu_time_add(tk, set, buffered)) {
			trctl = 1;
			need_plock = 1;
		}

		/*
		 * Update memory usage for the currently running process.
		 */
		proc_rss_update(pp, set);
	}

	LWP_AC_UNLOCK(t);

	if (need_plock == 0)
		goto out;

	mutex_enter(t->t_plockp);
	if ((t->t_proc_flag & TP_LWPEXIT) == 0) {
		if (quantum) {
			/*
			 * The scheduling class has indicated that the
			 * process lock must be acquired for tick
			 * processing.
			 */
			for (i = 0; i < pending; i++) {
				CL_TICK(t);
				DTRACE_SCHED1(tick, kthread_t *, t);
			}
		}

		if (virtual) {
			/*
			 * Handle an expired virtual timer.
			 */
			poke |= ithandler(lwp, ITIMER_VIRTUAL);
		}

		if (prof) {
			/*
			 * Handle an expired profiling timer.
			 */
			poke |= ithandler(lwp, ITIMER_PROF);
		}

		if (prctl) {
			/*
			 * Call the resource control facility to check
			 * for thresholds and take action for
			 * process.max-cpu-time.
			 */
			proc_cpu_time_check(pp);
		}

		if (trctl) {
			/*
			 * Call the resource control facility to check
			 * for thresholds and take action for
			 * task.max-cpu-time.
			 */
			tk = ttotask(t);
			if (tk != NULL)
				task_cpu_time_check(tk, pp);
		}
	}
	mutex_exit(t->t_plockp);
out:
	/*
	 * Notify the CPU the thread is running on.
	 */
	if (poke && t->t_cpu != CPU)
		poke_cpu(t->t_cpu->cpu_id);
}

static void
clock_tick_process(int set, cpu_t *cp, clock_t mylbolt, int pending)
{
	kthread_t	*t;
	int		intr;
	klwp_id_t	lwp;
	lwp_ac_t	*lac;

	/*
	 * The locking here is rather tricky. thread_free_prevent()
	 * prevents the thread returned from being freed while we
	 * are looking at it. We can then check if the thread
	 * is exiting and get the appropriate p_lock if it
	 * is not.  We have to be careful, though, because
	 * the _process_ can still be freed while we've
	 * prevented thread free.  To avoid touching the
	 * proc structure we put a pointer to the p_lock in the
	 * thread structure.  The p_lock is persistent so we
	 * can acquire it even if the process is gone.  At that
	 * point we can check (again) if the thread is exiting
	 * and either drop the lock or do the tick processing.
	 */
	t = cp->cpu_thread;	/* Current running thread */
	if (CPU == cp) {
		/*
		 * 't' will be the tick processing thread on this
		 * CPU.  Use the pinned thread (if any) on this CPU
		 * as the target of the clock tick.
		 */
		if (t->t_intr != NULL)
			t = t->t_intr;
	}

	/*
	 * We use thread_free_prevent to keep the currently running
	 * thread from being freed or recycled while we're
	 * looking at it.
	 */
	thread_free_prevent(t);
	/*
	 * We cannot hold the cpu_lock to prevent the
	 * cpu_active from changing in the clock interrupt.
	 * As long as we don't block (or don't get pre-empted)
	 * the cpu_list will not change (all threads are paused
	 * before list modification).
	 */
	if (CLOCK_TICK_CPU_OFFLINE(cp)) {
		thread_free_allow(t);
		return;
	}

	/*
	 * Make sure the thread is still on the CPU.
	 */
	if ((t != cp->cpu_thread) &&
	    ((cp != CPU) || (t != cp->cpu_thread->t_intr))) {
		/*
		 * We could not locate the thread. Skip this CPU. Race
		 * conditions while performing these checks are benign.
		 * These checks are not perfect and they don't need
		 * to be.
		 */
		thread_free_allow(t);
		return;
	}

	LWP_AC_LOCK(t);

	intr = t->t_flag & T_INTR_THREAD;
	lwp = ttolwp(t);
	if (lwp == NULL || intr) {
		/*
		 * Thread is uninteresting. So don't do any accounting.
		 */
		LWP_AC_UNLOCK(t);
		thread_free_allow(t);
		return;
	}

	lac = &lwp->lwp_ac;
	if (lac->ac_flags & LWP_AC_EXIT) {
		/*
		 * Thread is exiting, so don't do any accounting.
		 */
		LWP_AC_UNLOCK(t);
		thread_free_allow(t);
		return;
	}

	/*
	 * If we haven't done accounting for this
	 * lwp, then do it now. Since we don't hold the
	 * lwp down on a CPU it can migrate and show up
	 * more than once, hence the lbolt check. mylbolt
	 * is copied at the time of tick scheduling to prevent
	 * lbolt mismatches.
	 *
	 * Also, make sure that it's okay to perform the
	 * tick processing before calling clock_tick.
	 */
	if ((cp->cpu_flags & CPU_QUIESCED) || CPU_ON_INTR(cp) ||
	    (cp->cpu_dispthread == cp->cpu_idle_thread)) {
		/*
		 * CPU changed state.
		 */
		LWP_AC_UNLOCK(t);
		thread_free_allow(t);
		return;
	}

	if (lac->ac_lbolt >= mylbolt) {
		LWP_AC_UNLOCK(t);
		thread_free_allow(t);
		return;
	}

	lac->ac_lbolt = mylbolt;
	clock_tick(set, t, pending);		/* releases LWP_AC lock */
	thread_free_allow(t);
}

/*
 * Check if all the CPUs in a set are idle. We try to avoid scheduling
 * accounting for an accounting set if all of its CPUs are idle.
 *
 * We cannot walk all the CPUs and try to determine if they are idle. That
 * would be too much overhead on large systems with many, many accounting
 * sets. So, we try to use the haltset bitmap in the CPU partition. If all
 * the CPUs in an accounting set happen to be members of the same CPU
 * partition, we can check its haltset and accurately determine if all the
 * CPUs are idle or not. If the CPUs are members of different CPU
 * partitions, we just return busy.
 *
 * To do this, it is enough to check the haltset in the first CPU's CPU
 * partition.
 */
static int
clock_tick_idle(clock_tick_set_t *csp)
{
	bitset_t	*haltset;

	haltset = &clock_tick_cpus[csp->ct_start]->cpu_part->cp_haltset;
	return (bitset_contains(haltset, &csp->ct_bitset));
}

void
clock_tick_schedule(int one_sec)
{
	int			i, end;
	clock_tick_set_t	*csp;
	cpu_t			*cp;

	if (clock_cpu_id != CPU->cpu_id)
		clock_cpu_id = CPU->cpu_id;

	/*
	 * If the previous invocation of handlers is not yet finished, then
	 * simply increment a pending count and return. Eventually when they
	 * finish, the pending count is passed down to the next set of
	 * handlers to process. This way, ticks that have already elapsed
	 * in the past are handled as quickly as possible to minimize the
	 * chances of threads getting away before their pending ticks are
	 * accounted. The other benefit is that if the pending count is
	 * more than one, it can be handled by a single invocation of
	 * clock_tick(). This is a good optimization for large configuration
	 * busy systems where tick accounting can get backed up for various
	 * reasons.
	 */
	clock_tick_pending++;

	mutex_enter(&clock_tick_lock);

	for (i = 0; i < clock_tick_nsets; i++) {
		csp = &clock_tick_set[i].cs_data;
		if (csp->ct_active) {
			mutex_exit(&clock_tick_lock);
			return;
		}
	}

	if (clock_tick_total_cpus <= clock_tick_threshold) {
		/*
		 * Do the accounting in situ. Each tick cycle, start the scan
		 * from a different CPU for the sake of fairness.
		 */
		end = clock_tick_total_cpus;
		clock_tick_scan++;
		if (clock_tick_scan >= end)
			clock_tick_scan = 0;

		mutex_exit(&clock_tick_lock);

		clock_tick_execute_common(0, 0, clock_tick_scan, end,
		    LBOLT_NO_ACCOUNT, clock_tick_pending);

		clock_tick_pending = 0;
		return;
	}

	mutex_exit(&clock_tick_lock);

	/*
	 * We want to handle the clock CPU here. If we
	 * scheduled the accounting for the clock CPU to another
	 * processor, that processor will find only the clock() thread
	 * running and not account for any user thread below it. Also,
	 * we want to handle this before we block on anything and allow
	 * the pinned thread below the current thread to escape.
	 */
	clock_tick_process(0, CPU, LBOLT_NO_ACCOUNT, clock_tick_pending);

	mutex_enter(&clock_tick_lock);

	if (clock_tick_policy == CLOCK_TICK_POLICY_POWER) {
		/*
		 * If the policy favors power savings, we would like to
		 * schedule tick processing on CPUs adjacent to the clock()
		 * CPU. The clock() CPU is ticking anyway. So, scheduling
		 * tick processing X-calls on the same power domain is
		 * better for power.
		 */
		clock_cpu_list = CPU;
	} else if (one_sec) {
		/*
		 * If the policy favors performance, we would like to
		 * schedule tick processing on adjacent CPUs and also
		 * move the processing around the system.
		 *
		 * Move the CPU pointer around every second. This is so
		 * all the CPUs can be X-called in a round-robin fashion
		 * to evenly distribute the X-calls. We don't do this
		 * at a faster rate than this because we don't want
		 * to affect cache performance negatively.
		 */
		clock_cpu_list = clock_cpu_list->cpu_next_onln;
	}

	/*
	 * Schedule each set on a separate processor.
	 */
	cp = clock_cpu_list;
	for (i = 0; i < clock_tick_nsets; i++) {
		csp = &clock_tick_set[i].cs_data;

		if (clock_tick_policy == CLOCK_TICK_POLICY_POWER) {
			/*
			 * If all CPUs in this set are idle, do nothing.
			 */
			if (clock_tick_idle(csp))
				continue;
		}

		/*
		 * Pick the next online CPU in list for scheduling tick
		 * accounting. The clock_tick_lock is held by the caller.
		 * So, CPU online/offline cannot muck with this while
		 * we are picking our CPU to X-call.
		 */
		if (cp == CPU)
			cp = cp->cpu_next_onln;

		/*
		 * Each tick cycle, start the scan from a different
		 * CPU for the sake of fairness.
		 */
		csp->ct_scan++;
		if (csp->ct_scan >= csp->ct_end)
			csp->ct_scan = csp->ct_start;

		clock_tick_schedule_one(i, clock_tick_pending, cp->cpu_id);

		cp = cp->cpu_next_onln;
	}

	mutex_exit(&clock_tick_lock);

	clock_tick_pending = 0;
}

static void
clock_tick_execute_common(int set, int start, int scan, int end,
	clock_t mylbolt, int pending)
{
	cpu_t		*cp;
	int		i;

	ASSERT((start <= scan) && (scan <= end));

	/*
	 * Handle the thread on current CPU first. This is to prevent a
	 * pinned thread from escaping if we ever block on something.
	 * Note that in the single-threaded mode, this handles the clock
	 * CPU.
	 */
	clock_tick_process(set, CPU, mylbolt, pending);

	/*
	 * Perform tick accounting for the threads running on
	 * the scheduled CPUs.
	 */
	for (i = scan; i < end; i++) {
		cp = clock_tick_cpus[i];
		if ((cp == NULL) || (cp == CPU) || (cp->cpu_id == clock_cpu_id))
			continue;
		clock_tick_process(set, cp, mylbolt, pending);
	}

	for (i = start; i < scan; i++) {
		cp = clock_tick_cpus[i];
		if ((cp == NULL) || (cp == CPU) || (cp->cpu_id == clock_cpu_id))
			continue;
		clock_tick_process(set, cp, mylbolt, pending);
	}
}

/*ARGSUSED*/
static uint_t
clock_tick_execute(caddr_t arg1, caddr_t arg2)
{
	clock_tick_set_t	*csp;
	clock_tick_cpu_t	*ctp;
	int			start, scan, end, pending, set;
	clock_t			mylbolt;

	ctp = &clock_tick_cpu[CPU->cpu_id].ct_data;

	/*
	 * We could have raced with cpu offline. We don't want to
	 * process anything on an offlined CPU. If we got blocked
	 * on anything, we may not get scheduled when we wakeup
	 * later on.
	 */
	if (!CLOCK_TICK_XCALL_SAFE(CPU)) {
		set = ctp->ct_set;
		goto out;
	}

	mutex_enter(&ctp->ct_lock);
	set = ctp->ct_set;
	pending = ctp->ct_pending;
	ctp->ct_pending = 0;
	start = ctp->ct_start;
	end = ctp->ct_end;
	scan = ctp->ct_scan;
	mylbolt = ctp->ct_lbolt;
	mutex_exit(&ctp->ct_lock);

	clock_tick_execute_common(set, start, scan, end, mylbolt, pending);

out:
	/*
	 * Signal completion to the clock handler.
	 */
	csp = &clock_tick_set[set].cs_data;
	atomic_dec_ulong(&csp->ct_active);

	return (1);
}

/*ARGSUSED*/
static int
clock_tick_cpu_setup(cpu_setup_t what, int cid, void *arg)
{
	cpu_t			*cp, *ncp;
	int			i, set;
	clock_tick_set_t	*csp;

	/*
	 * This function performs some computations at CPU offline/online
	 * time. The computed values are used during tick scheduling and
	 * execution phases. This avoids having to compute things on
	 * an every tick basis. The other benefit is that we perform the
	 * computations only for onlined CPUs (not offlined ones). As a
	 * result, no tick processing is attempted for offlined CPUs.
	 *
	 * Also, cpu_offline() calls this function before checking for
	 * active interrupt threads. This allows us to avoid posting
	 * cross calls to CPUs that are being offlined.
	 */

	cp = cpu[cid];

	mutex_enter(&clock_tick_lock);

	switch (what) {
	case CPU_ON:
		clock_tick_cpus[clock_tick_total_cpus] = cp;
		set = clock_tick_total_cpus / clock_tick_ncpus;
		csp = &clock_tick_set[set].cs_data;
		csp->ct_end++;
		clock_tick_total_cpus++;
		clock_tick_nsets =
		    (clock_tick_total_cpus + clock_tick_ncpus - 1) /
		    clock_tick_ncpus;
		CPUSET_ADD(clock_tick_online_cpuset, cid);
		if (clock_tick_highest_set < (clock_tick_nsets - 1)) {
			/*
			 * clock_tick_highest_set increases monotonically
			 * and never shrinks so that aggregation of data
			 * across multiple sets does not lose data.
			 */
			clock_tick_highest_set = clock_tick_nsets - 1;
		}
		bitset_add(&csp->ct_bitset, cp->cpu_seqid);
		membar_sync();
		break;

	case CPU_OFF:
		sync_softint(cid);
		CPUSET_DEL(clock_tick_online_cpuset, cid);
		clock_tick_total_cpus--;
		clock_tick_cpus[clock_tick_total_cpus] = NULL;
		clock_tick_nsets =
		    (clock_tick_total_cpus + clock_tick_ncpus - 1) /
		    clock_tick_ncpus;
		set = clock_tick_total_cpus / clock_tick_ncpus;
		csp = &clock_tick_set[set].cs_data;
		csp->ct_end--;
		bitset_del(&csp->ct_bitset, cp->cpu_seqid);

		i = 0;
		ncp = cpu_active;
		do {
			if (cp == ncp)
				continue;
			clock_tick_cpus[i] = ncp;
			i++;
		} while ((ncp = ncp->cpu_next_onln) != cpu_active);
		ASSERT(i == clock_tick_total_cpus);
		membar_sync();
		break;

	default:
		break;
	}

	mutex_exit(&clock_tick_lock);

	return (0);
}


void
clock_tick_mp_init(void)
{
	cpu_t	*cp;

	mutex_enter(&cpu_lock);

	cp = cpu_active;
	do {
		(void) clock_tick_cpu_setup(CPU_ON, cp->cpu_id, NULL);
	} while ((cp = cp->cpu_next_onln) != cpu_active);

	register_cpu_setup_func(clock_tick_cpu_setup, NULL);

	mutex_exit(&cpu_lock);
}

void
clock_tick_set_policy(int policy)
{
	ASSERT((policy >= 0) && (policy < CLOCK_TICK_MAX_POLICIES));

	mutex_enter(&clock_tick_lock);
	clock_tick_policy = policy;
	mutex_exit(&clock_tick_lock);
}
