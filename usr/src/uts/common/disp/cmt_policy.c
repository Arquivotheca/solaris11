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
 * Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/systm.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/thread.h>
#include <sys/cpuvar.h>
#include <sys/cpupart.h>
#include <sys/cmn_err.h>
#include <sys/disp.h>
#include <sys/group.h>
#include <sys/bitset.h>
#include <sys/lgrp.h>
#include <sys/cmt.h>
#include <sys/sdt.h>

/*
 * CMT dispatcher policies
 * -----------------------
 *
 * This file implements CMT dispatching policies using Processor Groups.
 *
 * The scheduler/dispatcher leverages knowledge of the performance
 * relevant CMT sharing relationships existing between CPUs to implement
 * load balancing, and coalescence thread placement policies.
 *
 * Load balancing policy seeks to improve performance by minimizing
 * contention over shared processor resources / facilities. Coalescence
 * policies improve resource utilization and ultimately power efficiency.
 *
 * On NUMA systems, the dispatcher will generally perform load balancing and
 * coalescence within (and not across) lgroups. This is because there isn't
 * much sense in trying to correct an imbalance by sending a thread outside
 * of its home, if it would attempt to return home a short while later.
 * The dispatcher will implement CMT policy across lgroups however, if
 * it can do so with a thread homed to the root lgroup, since root homed
 * threads have no lgroup affinity.
 */

/*
 * Return non-zero if, given the policy, we should migrate from running
 * somewhere "here" to somewhere "there".
 */
static int
cmt_should_migrate(pg_cmt_t *here, pg_cmt_t *there, pg_cmt_policy_t policy,
    uint64_t self)
{
	uint64_t here_util, there_util;

	here_util = here->cmt_utilization;
	there_util = there->cmt_utilization;

	/*
	 * Ignore curthread's effect.
	 */
	if (self > 0 && bitset_in_set(&here->cmt_cpus_actv_set, CPU->cpu_seqid))
		here_util = here_util - self;

	/*
	 * Load balancing and coalescence are conflicting policies
	 */
	ASSERT((policy & (CMT_BALANCE|CMT_COALESCE)) !=
	    (CMT_BALANCE|CMT_COALESCE));

	switch (policy) {
	case CMT_BALANCE:
		/*
		 * Balance utilization
		 *
		 * If the target is comparatively underutilized
		 * (either in an absolute sense, or scaled by capacity),
		 * then choose to balance.
		 */
		if ((here_util > there_util) ||
		    (here_util == there_util &&
		    (CMT_CAPACITY(there) > CMT_CAPACITY(here)))) {
			return (1);
		}

		break;

	case CMT_COALESCE:
		/*
		 * Attempt to drive group utilization up to capacity
		 */
		if (there_util > here_util &&
		    there_util < CMT_CAPACITY(there))
			return (1);

		break;
	}

	return (0);
}

/*
 * Perform multi-level CMT load balancing of running threads.
 *
 * The caller passes in:
 *  'tp' the thread being enqueued.
 *  'cp' a hint CPU, against which CMT load balancing will be performed.
 *
 * This routine returns a CPU better than 'cp' with respect to PG utilization
 * or 'cp' if one cannot be found.
 */
cpu_t *
cmt_balance(kthread_t *tp, cpu_t *cp)
{
	int idx, start, nsiblings, level = 0, tgt_size;
	pg_cmt_t *pg, *sib, *tgt = NULL;
	group_t *cmt_pgs, *siblings;
	uint64_t self = 0;
	cpu_t *ncp;
	extern cmt_lgrp_t *cmt_root;

	ASSERT(THREAD_LOCK_HELD(tp));

	cmt_pgs = &cp->cpu_pg->cmt_pgs;

	if (GROUP_SIZE(cmt_pgs) == 0)
		return (cp);

	if (tp == curthread)
		self = cp->cpu_initial_util;

	/*
	 * Balance across siblings in the CPUs CMT lineage. If the thread is
	 * homed to the root lgroup, perform top level balancing against other
	 * top level PGs in the system. Otherwise, start with the default top
	 * level siblings group, which is within the leaf lgroup.
	 */
	pg = GROUP_ACCESS(cmt_pgs, level);
	if (tp->t_lpl->lpl_lgrpid == LGRP_ROOTID)
		siblings = &cmt_root->cl_pgs;
	else
		siblings = pg->cmt_siblings;

	/*
	 * Traverse down the lineage until we find a level that needs
	 * balancing, or we get to the end.
	 */
	for (;;) {
		/*
		 * No need to balance if this level is/becomes underutilized.
		 */
		if ((pg->cmt_utilization - self) == 0)
			return (cp);

		if ((nsiblings = GROUP_SIZE(siblings)) == 1)
			goto next_level;

		/*
		 * Find a balancing candidate from among our siblings
		 * "start" is a hint for where to start looking.
		 */
		idx = start = CPU_PSEUDO_RANDOM() % nsiblings;

		do {
			ASSERT(idx < nsiblings);
			sib = GROUP_ACCESS(siblings, idx);

			/*
			 * The candidate must not be us, and must
			 * have some CPU resources in the thread's
			 * partition.
			 */
			if (sib != pg &&
			    bitset_in_set(&tp->t_cpupart->cp_cmt_pgs,
			    ((pg_t *)sib)->pg_id)) {
				tgt = sib;
				break;
			}

			if (--idx < 0)
				idx = nsiblings - 1;

		} while (idx != start);

		if (tgt != NULL) {
			/*
			 * Decide if we should migrate from the current
			 * PG to a target PG given a policy.
			 */
			if (cmt_should_migrate(pg, tgt,
			    pg->cmt_policy, self))
				break;

			tgt = NULL;
		}

next_level:
		if (++level == GROUP_SIZE(cmt_pgs))
			return (cp);

		pg = GROUP_ACCESS(cmt_pgs, level);
		siblings = pg->cmt_siblings;
	}

	/*
	 * We found a target PG to load balance against, now we need to find
	 * an idle CPU from within it. This CPU must:
	 *  - be in the same partition as the thread we're balancing;
	 *  - have nothing in its runq;
	 *  - be idle or running a transient thread.
	 */
	ASSERT(tgt != NULL);

	tgt_size = GROUP_SIZE(&tgt->cmt_cpus_actv);
	idx = start = CPU_PSEUDO_RANDOM() % tgt_size;

	do {
		ncp = GROUP_ACCESS(&tgt->cmt_cpus_actv, idx);

		if (ncp->cpu_part == tp->t_cpupart &&
		    ncp->cpu_disp->disp_nrunnable == 0 &&
		    (CPU_DISP_IDLE(ncp) || CPU_DISP_IS_TRANSIENT(ncp))) {

			DTRACE_PROBE4(cmt__balance, kthread_t *, tp,
			    pg_cmt_t *, tgt, cpu_t *, cp,
			    cpu_t *, ncp);

			cp = ncp;
			break;
		}

		if (--idx < 0)
			idx = tgt_size - 1;

	} while (idx != start);

	return (cp);
}

/*
 * cmt_affinity() implements the same load balancing mechanism as cmt_balance()
 * but traversing the CMT lineage of the given CPU in the opposite direction.
 * The goal here is to load balance threads with a larger focus on affinity.
 */
cpu_t *
cmt_affinity(kthread_t *tp, cpu_t *cp)
{
	int idx, start, nsiblings, level, tgt_size;
	pg_cmt_t *pg, *sib, *tgt = NULL;
	group_t *cmt_pgs, *siblings;
	uint64_t self = 0;
	cpu_t *ncp;

	ASSERT(THREAD_LOCK_HELD(tp));

	cmt_pgs = &cp->cpu_pg->cmt_pgs;

	if ((level = GROUP_SIZE(cmt_pgs)) == 0)
		return (cp);

	level -= 1;

	if (tp == curthread)
		self = cp->cpu_initial_util;

	pg = GROUP_ACCESS(cmt_pgs, level);
	siblings = pg->cmt_siblings;

	/*
	 * Traverse up the lineage until we find a level that needs
	 * balancing, or we get to the end.
	 */
	for (;;) {
		/*
		 * No need to balance if this level is/becomes underutilized.
		 */
		if ((pg->cmt_utilization - self) == 0)
			return (cp);

		if ((nsiblings = GROUP_SIZE(siblings)) == 1)
			goto next_level;

		/*
		 * Find a balancing candidate from among our siblings
		 * "start" is a hint for where to start looking.
		 */
		idx = start = CPU_PSEUDO_RANDOM() % nsiblings;

		do {
			ASSERT(idx < nsiblings);
			sib = GROUP_ACCESS(siblings, idx);

			/*
			 * The candidate must not be us, and must
			 * have some CPU resources in the thread's
			 * partition.
			 */
			if (sib != pg &&
			    bitset_in_set(&tp->t_cpupart->cp_cmt_pgs,
			    ((pg_t *)sib)->pg_id)) {
				tgt = sib;
				break;
			}

			if (--idx < 0)
				idx = nsiblings - 1;

		} while (idx != start);

		if (tgt != NULL) {
			/*
			 * Decide if we should migrate from the current
			 * PG to a target PG given a policy.
			 */
			if (cmt_should_migrate(pg, tgt,
			    pg->cmt_policy, self))
				break;

			tgt = NULL;
		}

next_level:
		if (level-- == 0)
			return (cp);

		pg = GROUP_ACCESS(cmt_pgs, level);
		siblings = pg->cmt_siblings;
	}

	/*
	 * We found a target PG to load balance against, now we need to find
	 * an idle CPU from within it. This CPU must:
	 *  - be in the same partition as the thread we're balancing;
	 *  - have nothing in its runq;
	 *  - be idle or running a transient thread.
	 */
	ASSERT(tgt != NULL);

	tgt_size = GROUP_SIZE(&tgt->cmt_cpus_actv);
	idx = start = CPU_PSEUDO_RANDOM() % tgt_size;

	do {
		ncp = GROUP_ACCESS(&tgt->cmt_cpus_actv, idx);

		if (ncp->cpu_part == tp->t_cpupart &&
		    ncp->cpu_disp->disp_nrunnable == 0 &&
		    (CPU_DISP_IDLE(ncp) || CPU_DISP_IS_TRANSIENT(ncp))) {

			DTRACE_PROBE4(cmt__affinity, kthread_t *, tp,
			    pg_cmt_t *, tgt, cpu_t *, cp,
			    cpu_t *, ncp);

			cp = ncp;
			break;
		}

		if (--idx < 0)
			idx = tgt_size - 1;

	} while (idx != start);

	return (cp);
}
