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
 * Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/proc.h>
#include <sys/tick_ac.h>
#include <sys/cpuvar.h>
#include <vm/rm.h>
#include <sys/clock_tick.h>

static kmem_cache_t	*proc_ac_cache;
static size_t		proc_ac_size;

/*
 * Install process context ops for the current process.
 */
void
installpctx(
	proc_t *p,
	void	*arg,
	void	(*save)(void *),
	void	(*restore)(void *),
	void	(*fork)(void *, void *),
	void	(*exit)(void *),
	void	(*free)(void *, int))
{
	struct pctxop *pctx;

	pctx = kmem_alloc(sizeof (struct pctxop), KM_SLEEP);
	pctx->save_op = save;
	pctx->restore_op = restore;
	pctx->fork_op = fork;
	pctx->exit_op = exit;
	pctx->free_op = free;
	pctx->arg = arg;
	pctx->next = p->p_pctx;
	p->p_pctx = pctx;
}

/*
 * Remove a process context ops from the current process.
 */
int
removepctx(
	proc_t *p,
	void	*arg,
	void	(*save)(void *),
	void	(*restore)(void *),
	void	(*fork)(void *, void *),
	void	(*exit)(void *),
	void	(*free)(void *, int))
{
	struct pctxop *pctx, *prev_pctx;

	prev_pctx = NULL;
	for (pctx = p->p_pctx; pctx != NULL; pctx = pctx->next) {
		if (pctx->save_op == save && pctx->restore_op == restore &&
		    pctx->fork_op == fork &&
		    pctx->exit_op == exit && pctx->free_op == free &&
		    pctx->arg == arg) {
			if (prev_pctx)
				prev_pctx->next = pctx->next;
			else
				p->p_pctx = pctx->next;
			if (pctx->free_op != NULL)
				(pctx->free_op)(pctx->arg, 0);
			kmem_free(pctx, sizeof (struct pctxop));
			return (1);
		}
		prev_pctx = pctx;
	}
	return (0);
}

void
savepctx(proc_t *p)
{
	struct pctxop *pctx;

	ASSERT(p == curthread->t_procp);
	for (pctx = p->p_pctx; pctx != 0; pctx = pctx->next)
		if (pctx->save_op != NULL)
			(pctx->save_op)(pctx->arg);
}

void
restorepctx(proc_t *p)
{
	struct pctxop *pctx;

	ASSERT(p == curthread->t_procp);
	for (pctx = p->p_pctx; pctx != 0; pctx = pctx->next)
		if (pctx->restore_op != NULL)
			(pctx->restore_op)(pctx->arg);
}

void
forkpctx(proc_t *p, proc_t *cp)
{
	struct pctxop *pctx;

	for (pctx = p->p_pctx; pctx != NULL; pctx = pctx->next)
		if (pctx->fork_op != NULL)
			(pctx->fork_op)(p, cp);
}

/*
 * exitpctx is called during thread/lwp exit to perform any actions
 * needed when an LWP in the process leaves the processor for the last
 * time. This routine is not intended to deal with freeing memory; freepctx()
 * is used for that purpose during proc_exit(). This routine is provided to
 * allow for clean-up that can't wait until thread_free().
 */
void
exitpctx(proc_t *p)
{
	struct pctxop *pctx;

	for (pctx = p->p_pctx; pctx != NULL; pctx = pctx->next)
		if (pctx->exit_op != NULL)
			(pctx->exit_op)(p);
}

/*
 * freepctx is called from proc_exit() to get rid of the actual context ops.
 */
void
freepctx(proc_t *p, int isexec)
{
	struct pctxop *pctx;

	while ((pctx = p->p_pctx) != NULL) {
		p->p_pctx = pctx->next;
		if (pctx->free_op != NULL)
			(pctx->free_op)(pctx->arg, isexec);
		kmem_free(pctx, sizeof (struct pctxop));
	}
}

void
proc_cpu_time_update(proc_t *p)
{
	hrtime_t user, sys;

	ASSERT(MUTEX_HELD(&p->p_lock));

	user = mstate_aggr_state(p, LMS_USER);
	sys = mstate_aggr_state(p, LMS_SYSTEM);
	p->p_utime = (clock_t)NSEC_TO_TICK(user);
	p->p_stime = (clock_t)NSEC_TO_TICK(sys);
}

void
proc_ac_init(void)
{
	proc_ac_cache = kmem_cache_create("proc_ac_cache",
	    sizeof (proc_ac_t), CPU_CACHE_COHERENCE_SIZE, NULL,
	    NULL, NULL, NULL, NULL, 0);
	proc_ac_size = sizeof (proc_ac_t) * clock_tick_max_sets;
}

/*
 * Create stuff needed to do process CPU time accounting.
 */
void
proc_ac_create(proc_t *p)
{
	proc_ac_ctl_t	*pctl;
	proc_ac_t	*old_pac, *new_pac;
	size_t		offset;

	ASSERT(p->p_lwpcnt >= 1);

	pctl = &p->p_ac_ctl;
	if (pctl->pac_multi) {
		/*
		 * We have already created the accounting stuff for a
		 * multi-threaded process. Nothing to do.
		 */
		return;
	}

	if (p->p_lwpcnt == 1) {
		/*
		 * The first LWP is being created. Create a single accounting
		 * structure as this is a single-threaded process at this
		 * point.
		 */
		pctl->pac = kmem_cache_alloc(proc_ac_cache, KM_SLEEP);
		bzero(pctl->pac, sizeof (proc_ac_t));
		pctl->pac_thread = p->p_tlist;
	} else {
		/*
		 * The second LWP is being created. Put the process in
		 * multi-threaded mode. From here on, till the process exits,
		 * it will remain in this mode. Allocate a set of buffers. The
		 * LWP accounting handlers will accumulate data from
		 * multiple CPUs into these buffers without using any
		 * locks. The handlers will also check for the rctl threshold
		 * when they accumulate CPU time. Copy the accounting
		 * information that has already been accumulated for the
		 * first LWP.
		 */
		old_pac = pctl->pac;
		new_pac = clock_tick_alloc(proc_ac_size, &offset);

		LWP_AC_LOCK(pctl->pac_thread);
		pctl->pac_multi = 1;
		pctl->pac = new_pac;
		pctl->pac_offset = offset;
		pctl->pac[0] = *old_pac;
		LWP_AC_UNLOCK(pctl->pac_thread);

		kmem_cache_free(proc_ac_cache, old_pac);
	}

	/*
	 * Now, set the thresholds for the process based on the number
	 * of LWPs. proc_cpu_time_check() calls rctl_test(). rctl_test() calls
	 * proc_cpu_time_test() which sets the thresholds.
	 */
	proc_cpu_time_check(p);
}

void
proc_ac_destroy(proc_t *p)
{
	proc_ac_ctl_t		*pctl;

	pctl = &p->p_ac_ctl;
	if (pctl->pac == NULL)
		return;

	if (!pctl->pac_multi) {
		kmem_cache_free(proc_ac_cache, pctl->pac);
		pctl->pac = NULL;
		return;
	}

	clock_tick_free(pctl->pac, pctl->pac_offset, proc_ac_size);
	pctl->pac = NULL;
}
/*
 * RSS samples are collect by each individual LWP of the process. We have
 * to aggregate them to get the total. Plus, LWPs add their RSS samples
 * to the user area when they exit. That needs to be added as well.
 */
void
proc_rss_aggregate(proc_t *p, uint64_t *rssp, size_t *rss_maxp,
    ulong_t *rss_samplesp)
{
	uint64_t rss;
	size_t rss_max;
	ulong_t rss_samples;
	user_t *userp;
	proc_ac_ctl_t *pctl;
	proc_ac_t *pac, *pac_end;

	userp = PTOU(p);
	pctl = &p->p_ac_ctl;
	ASSERT(pctl->pac != NULL);

	pac = pctl->pac;
	pac_end = pctl->pac_multi ? &pac[clock_tick_highest_set] : &pac[0];

	rss = userp->u_mem;
	rss_max = 0;
	rss_samples = 0;

	while (pac <= pac_end) {
		rss += pac->pac_rss;
		if (rss_max < pac->pac_rss_max)
			rss_max = pac->pac_rss_max;
		rss_samples += pac->pac_rss_samples;
		pac++;
	}

	*rssp = rss;
	*rss_maxp = rss_max;
	*rss_samplesp = rss_samples;
}

void
proc_rss_update(proc_t *p, int set)
{
	proc_ac_ctl_t	*pctl;
	proc_ac_t	*pac;
	size_t		rss;

	pctl = &p->p_ac_ctl;
	ASSERT(pctl->pac != NULL);
	if (!pctl->pac_multi)
		set = 0;
	pac = &pctl->pac[set];

	rss = rm_asrss(p->p_as);
	pac->pac_rss += rss;
	pac->pac_rss_samples++;
	if (rss > pac->pac_rss_max)
		pac->pac_rss_max = rss;
}
