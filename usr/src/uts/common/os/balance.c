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


#include <sys/sysmacros.h>
#include <sys/balance.h>
#include <sys/ksynch.h>
#include <sys/cpuvar.h>
#include <sys/cpupart.h>
#include <sys/pci_tools.h>
#include <sys/sunndi.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/uadmin.h>
#include <sys/callb.h>

/*
 * I/O load balancer.  Depends heavily on the affinity code.
 */

int bal_rebalance_strategy;	/* current I/O balancer strategy */
volatile int bal_balancer_disable = 1;	/* nonzero => disable load balancer */
int bal_balancer_disabled;	/* nonzero => load balancer disabled */
kmutex_t bal_balance_lock;	/* main lock for balancer daemon */
kcondvar_t bal_balance_cv;	/* sleep cv for balancer daemon */
kmutex_t bal_disable_lock;	/* lock for balancer disable */
kcondvar_t bal_disable_cv;	/* disable wait cv for balancer daemon */
static int baddata[BAL_NDELTAS]; /* count of bad data points in delta */
static int bal_goodness;	/* current goodness measure */
static int bal_avgintrload;	/* avg intr load across cpus (percentage) */
static uint64_t bal_avgintrnsec; /* avg nsec spent in interrupts per cpu */
list_t bal_cpulist;		/* list of cpu balance data structures */
list_t bal_scpulist;		/* sorted list of cpu balance data structs */
list_t bal_rccpulist;		/* reconfigured list of cpu data structs */
list_t bal_sivlist;		/* sorted list of ivecs */
list_t bal_uaivlist;		/* list of unassigned ivecs */
list_t bal_nexuslist;		/* list of pci nexus's in use */
volatile int bal_curdelta;	/* index of current delta */
volatile uint64_t bal_nsnaps;	/* how many stat snaps have we taken */
int bal_baseline_goodness;	/* current goodness baseline */
int bal_ualist_inited;		/* set if bal_uaivlist initialized */
#ifdef DEBUG
#define	BAL_DPRINT
int bal_force_rebal;		/* force a rebalance */
processorid_t bal_hicpu;	/* cpu with most ivrecs */
int bal_maxnvec;		/* # ivecs on bal_hicpu */
#endif	/* DEBUG */
/* #define	BAL_DPRINT */
#ifdef BAL_DPRINT
#define	BAL_DEBUG(args)	if (bal_dprint) cmn_err args
int bal_dprint;			/* if != 0 do debug prints */
#else
#define	BAL_DEBUG(args)
#endif	/* BAL_DPRINT */
kmutex_t ivec_list_lock;	/* protects per-cpu ivec lists */
kmutex_t ua_ivec_list_lock;	/* protects unassigned ivec list */
cpu_loadinf_t **bal_cpups;	/* cpu pointer index array */
int bal_firstcpu;
extern const int _ncpu;

static processorid_t bal_get_affinity(ivecdat_t *);
static void bal_clear_deltas(void);
static dev_info_t *bal_get_nexus_node(ivecdat_t *);
static void bal_get_nexus_handle(ivecdat_t *);
static void bal_close_nexus_handles(void);
static void bal_rebalance(int, int);
static int bal_cpu_change(cpu_setup_t, int, void *);
static void bal_new_affinity(ivecdat_t *ip);
static proc_t *proc_balancer;

/*
 * offline cpu info protected by cpu_lock
 */
list_t	bal_offlinedcpus;	/* cpus that need rebalance after offlining */

volatile int bal_onlinedcpus;		/* Set if a cpu has come online */
char bal_nexus_path[MAXPATHLEN];	/* path to pcitool nexus */

/*
 * Called to register a pci device interrupt bound to a particular cpu.
 * Is passed a pointer to the cpu tick accumulator for the service routine.
 */
void
bal_register_int(dev_info_t *dip, dev_info_t *pdip, ddi_intr_handle_impl_t *ih,
    uint64_t *ticksp, int vec, uint32_t cookie)
{
	ivecdat_t *nivp;

	switch (ih->ih_type) {
	case DDI_INTR_TYPE_FIXED:
	case DDI_INTR_TYPE_MSI:
	case DDI_INTR_TYPE_MSIX:
		break;	/* Supported interrupt type */
	default:
		return;	/* Unsupported interrupt type */
	}
	/*
	 * We can't call get_intr_affinity() from here since that will
	 * cause recursive lock entry problems.  We just put this ivec on the
	 * list of unassigned ivecs and when we go to collect data we will
	 * use pcitool via layered driver to determine correct cpu for the ivec.
	 */
	mutex_enter(&ua_ivec_list_lock);
	if (!bal_ualist_inited) {
		bal_ualist_inited = 1;
		list_create(&bal_uaivlist, sizeof (ivecdat_t),
		    offsetof(ivecdat_t, next));
	}
	mutex_exit(&ua_ivec_list_lock);
	nivp = kmem_zalloc(sizeof (ivecdat_t), KM_SLEEP);
	nivp->dip = dip;
	nivp->pdip = pdip;
	nivp->ih = ih;
	nivp->ticksp = ticksp;
	nivp->ocpu = ih->ih_target;
	nivp->ncpu = ih->ih_target;
	nivp->inum = vec;
	nivp->cookie = cookie;
	mutex_enter(&ua_ivec_list_lock);
	list_insert_tail(&bal_uaivlist, nivp);
	mutex_exit(&ua_ivec_list_lock);
	BAL_DEBUG((CE_NOTE, "registered ih 0x%lx ino 0x%x cookie 0x%x",
	    (uintptr_t)ih, vec, cookie));
}

/*
 * Called to remove an interrupt from the list of those bound to a cpu for
 * servicing.
 */
void
bal_remove_int(ddi_intr_handle_impl_t *ih, uint64_t *ticksp)
{
	ivecdat_t *ivdp;
	processorid_t cpu, ncpu;
	cpu_loadinf_t *cpup;
	list_t *lp;
	uint64_t **tp;
	int i;

	mutex_enter(&ivec_list_lock);
	mutex_enter(&ua_ivec_list_lock);
	/*
	 * Check unassigned list first
	 */
	lp = &bal_uaivlist;
	ASSERT(bal_ualist_inited);
	ivdp = list_head(lp);
	for (; ivdp != NULL; ivdp = list_next(lp, ivdp)) {
		if (ivdp->ih == ih)
			break;
		/*
		 * Null out the ticksp if this is one of a group
		 */
		if (ivdp->inocnt > 1) {
			tp = (uint64_t **)ivdp->ticksp;
			for (i = 0; i < ivdp->inocnt; i++)
				if (tp[i] == ticksp)
					tp[i] = NULL;
		}
	}
	if (ivdp != NULL) {
		list_remove(lp, ivdp);
		mutex_exit(&ua_ivec_list_lock);
		goto done;
	}
	mutex_exit(&ua_ivec_list_lock);
	/*
	 * Search the cpus for this ivec
	 */
	if (bal_cpups == NULL)
		goto done; /* XXX - this should not happen */
	ncpu = bal_firstcpu;
	do {
		cpu = ncpu;
		cpup = bal_cpups[cpu];
		ncpu = cpup->nextid;
		lp = &cpup->ivecs;
		ivdp = list_head(lp);
		for (; ivdp != NULL; ivdp = list_next(lp, ivdp)) {
			if (ivdp->ih == ih)
				break;
			/*
			 * Null out the ticksp if this is one of a group
			 */
			if (ivdp->inocnt > 1) {
				tp = (uint64_t **)ivdp->ticksp;
				for (i = 0; i < ivdp->inocnt; i++)
					if (tp[i] == ticksp)
						tp[i] = NULL;
			}
		}
		if (ivdp != NULL) {
			list_remove(lp, ivdp);
			cpup->nvec--;
			break;
		}
	} while (ncpu != 0);
done:
	mutex_exit(&ivec_list_lock);
	if (ivdp != NULL) {
		BAL_DEBUG((CE_NOTE, "unregister ih 0x%lx ino %x cookie %x",
		    (uintptr_t)ivdp->ih, ivdp->inum, ivdp->cookie));
		if (ivdp->inocnt > 1)
			kmem_free(ivdp->ticksp,
			    sizeof (uint64_t *) * ivdp->inocnt);
		kmem_free(ivdp, sizeof (ivecdat_t));
	}
}


/*
 * Called from the NUMA I/O subsystem to inform us that an interrupt is
 * managed by the NUMA I/O subsystem so we will not try any rebalancing
 * of the interrupt.
 */
/* ARGSUSED */
void
bal_numa_managed_int(ddi_intr_handle_t h, processorid_t cpu)
{
	ddi_intr_handle_impl_t *ih;
	ivecdat_t *ivdp;
	cpu_loadinf_t *cpup;
	list_t *lp;
	int i, ncpu;

	ih = (ddi_intr_handle_impl_t *)h;
	mutex_enter(&ivec_list_lock);
	mutex_enter(&ua_ivec_list_lock);
	if (!bal_ualist_inited) {
		bal_ualist_inited = 1;
		list_create(&bal_uaivlist, sizeof (ivecdat_t),
		    offsetof(ivecdat_t, next));
	}
	/*
	 * Check unassigned list first
	 */
	lp = &bal_uaivlist;
	ASSERT(bal_ualist_inited);
	ivdp = list_head(lp);
	for (; ivdp != NULL; ivdp = list_next(lp, ivdp)) {
		if (ivdp->ih == ih) {
			ivdp->numa_managed = 1;
			mutex_exit(&ua_ivec_list_lock);
			goto done;
		}
	}
	mutex_exit(&ua_ivec_list_lock);
	/*
	 * Search the cpus for this ivec
	 */
	if (bal_cpups == NULL)
		goto done;
	ncpu = bal_firstcpu;
	do {
		i = ncpu;
		cpup = bal_cpups[i];
		if (cpup == NULL)
			goto done;
		ncpu = cpup->nextid;
		lp = &cpup->ivecs;
		ivdp = list_head(lp);
		for (; ivdp != NULL; ivdp = list_next(lp, ivdp)) {
			if (ivdp->ih == ih) {
				ivdp->numa_managed = 1;
				goto done;
			}
		}
	} while (ncpu != 0);
done:
	mutex_exit(&ivec_list_lock);
}

/*
 * Called from the NUMA I/O subsystem to inform us that an interrupt is
 * no longer managed by the NUMA I/O subsystem.
 */
void
bal_numa_managed_clear(ddi_intr_handle_t h, processorid_t cpu)
{
	ddi_intr_handle_impl_t *ih;
	ivecdat_t *ivdp;
	cpu_loadinf_t *cpup;
	list_t *lp;

	ih = (ddi_intr_handle_impl_t *)h;
	mutex_enter(&ivec_list_lock);
	if (bal_cpups == NULL)
		goto cpus_uninit;
	cpup = bal_cpups[cpu];
	if (cpup == NULL)
		goto cpus_uninit;
	lp = &cpup->ivecs;
	ivdp = list_head(lp);
	for (; ivdp != NULL; ivdp = list_next(lp, ivdp)) {
		if (ivdp->ih == ih) {
			ivdp->numa_managed = 0;
			mutex_exit(&ivec_list_lock);
			return;
		}
	}
	/*
	 * This int can only be on the cpu indicated or the unassigned list.
	 * If we got here, it was not on the cpu so check the unassigned list.
	 */
cpus_uninit:
	mutex_enter(&ua_ivec_list_lock);
	lp = &bal_uaivlist;
	ASSERT(bal_ualist_inited);
	ivdp = list_head(lp);
	for (; ivdp != NULL; ivdp = list_next(lp, ivdp)) {
		if (ivdp->ih == ih) {
			ivdp->numa_managed = 0;
			break;
		}
	}
	mutex_exit(&ua_ivec_list_lock);
	mutex_exit(&ivec_list_lock);
}


/*
 * Deal with fixed int vectors
 */
static void
bal_assign_fixed(ivecdat_t *ivp, cpu_loadinf_t *cp)
{
	uint64_t **plist, **nplist;
	ivecdat_t *bivp;
	int i, ocnt;

	if (ivp->inocnt > 0) { /* already a combined ivecdat struct */
		list_insert_head(&cp->ivecs, ivp);
		cp->nvec++;
		return;
	}
	/*
	 * Search to see if this is a shared fixed interrupt.
	 */
	bivp = list_head(&cp->ivecs);
	for (; bivp != NULL; bivp = list_next(&cp->ivecs, bivp)) {
		if (bivp->cookie == ivp->cookie)
			break;
	}
	if (bivp == NULL) { /* No sharer found */
		/*
		 * Insert this ivec
		 */
		ivp->inocnt = 1;
		list_insert_head(&cp->ivecs, ivp);
		cp->nvec++;
#ifdef DEBUG
		if (cp->nvec > bal_maxnvec) {
			bal_maxnvec = cp->nvec;
			bal_hicpu = cp->cpuid;
		}
#endif	/* DEBUG */
		return;
	}
	/*
	 * This is a shared fixed irq, collapse into a single ivec for
	 * load balancing purposes.
	 * Add tick pointer to list of tick pointers.
	 */
	plist = (uint64_t **)bivp->ticksp;
	ocnt = bivp->inocnt;
	if (ocnt > 1) {
		/*
		 * Check if we have an empty tick pointer slot already.
		 */
		for (i = 0; i < ocnt; i++) {
			if (plist[i] == NULL) {
				plist[i] = ivp->ticksp;
				goto skipalloc;
			}
		}
	}
	bivp->inocnt++;
	nplist = kmem_alloc(sizeof (uint64_t *) * bivp->inocnt, KM_SLEEP);
	if (ocnt > 1) {
		for (i = 0; i < ocnt; i++)
			nplist[i] = plist[i];
		nplist[i] = ivp->ticksp;
		kmem_free(plist, sizeof (uint64_t *) * ocnt);
	} else {
		nplist[0] = bivp->ticksp;
		nplist[1] = ivp->ticksp;
	}
	bivp->ticksp = (uint64_t *)nplist;
skipalloc:
	/*
	 * Use the highest priority vector for the shared interrupt.
	 */
	if (ivp->inum > bivp->inum)
		bivp->inum = ivp->inum;
	/*
	 * Now free the new ivec struct since not needed
	 */
	kmem_free(ivp, sizeof (ivecdat_t));
}

/*
 * Deal with msi vector group elements
 */
static void
bal_assign_msi_group(ivecdat_t *ivp, cpu_loadinf_t *cp)
{
	int i, ocnt;
	uint64_t **plist, **nplist;
	ivecdat_t *bivp;

	if (ivp->inocnt > 0) { /* already a combined ivecdat struct */
		list_insert_head(&cp->ivecs, ivp);
		cp->nvec++;
		return;
	}
	/*
	 * Find base ivec for this group.  If can't find it, make this the base.
	 */
	bivp = list_head(&cp->ivecs);
	for (; bivp != NULL; bivp = list_next(&cp->ivecs, bivp)) {
		if (bivp->dip == ivp->dip && bivp->inocnt != 0)
			break;
	}
	if (bivp == NULL) { /* No base ivec found */
		/*
		 * Make this the base ivec
		 */
		ivp->inocnt = 1;
		list_insert_head(&cp->ivecs, ivp);
		cp->nvec++;
#ifdef DEBUG
		if (cp->nvec > bal_maxnvec) {
			bal_maxnvec = cp->nvec;
			bal_hicpu = cp->cpuid;
		}
#endif	/* DEBUG */
		return;
	}
	if (ivp->inum < bivp->inum) {
		bivp->inum = ivp->inum;
		bivp->ih = ivp->ih;
	}
	/*
	 * Add tick pointer to list of tick pointers.
	 * Things will not be counted properly if we ever have duplicate
	 * tick pointers.
	 */
	plist = (uint64_t **)bivp->ticksp;
	ocnt = bivp->inocnt;
#ifdef DEBUG
	if (ocnt > 1) {
		for (i = 0; i < ocnt; i++)
			if (plist[i] == ivp->ticksp)
				cmn_err(CE_WARN,
				    "intrd: duplicate tick pointer");
	} else {
		if (bivp->ticksp == ivp->ticksp)
			cmn_err(CE_WARN, "intrd: duplicate tick pointer");
	}
#endif	/* DEBUG */
	if (ocnt > 1) {
		/*
		 * Check if we have an empty tick pointer slot already.
		 */
		for (i = 0; i < ocnt; i++) {
			if (plist[i] == NULL) {
				plist[i] = ivp->ticksp;
				goto skipalloc;
			}
		}
	}
	bivp->inocnt++;
	nplist = kmem_alloc(sizeof (uint64_t *) * bivp->inocnt, KM_SLEEP);
	if (bivp->inocnt > 2) {
		for (i = 0; i < ocnt; i++)
			nplist[i] = plist[i];
		nplist[i] = ivp->ticksp;
		kmem_free(plist, sizeof (uint64_t *) * ocnt);
	} else {
		nplist[0] = bivp->ticksp;
		nplist[1] = ivp->ticksp;
	}
	bivp->ticksp = (uint64_t *)nplist;
skipalloc:
	/*
	 * Now free the ivec struct since not the base
	 */
	kmem_free(ivp, sizeof (ivecdat_t));
}

/*
 * Assign ivecs to their proper cpus, since the ivecs may have moved
 * we start by reassigning them all to their current cpus.
 */
static void
bal_assign_ints()
{
	processorid_t cpu;
	cpu_loadinf_t *cpup;
	ivecdat_t *ivp, *nivp;

	/*
	 * Holding the ivec lock prevents any ivecs from being removed
	 * out from under us while we are moving it to its proper cpu.
	 */
	mutex_enter(&ivec_list_lock);
	mutex_enter(&ua_ivec_list_lock);
	ivp = list_head(&bal_uaivlist);
	mutex_exit(&ua_ivec_list_lock);
	for (; ivp != NULL; ivp = nivp) {
		mutex_enter(&ua_ivec_list_lock);
		nivp = list_next(&bal_uaivlist, ivp);
		mutex_exit(&ua_ivec_list_lock);
		if ((cpu = bal_get_affinity(ivp)) < 0) {
			if (cpu == -1)
				continue; /* leave on unassigned list */
			/*
			 * If here we couldn't find a pci control nexus.
			 * Just free the ivec since we can't manage it.
			 */
			mutex_enter(&ua_ivec_list_lock);
			list_remove(&bal_uaivlist, ivp);
			mutex_exit(&ua_ivec_list_lock);
			if (ivp->inocnt > 1)
				kmem_free(ivp->ticksp,
				    sizeof (uint64_t *) * ivp->inocnt);
			kmem_free(ivp, sizeof (ivecdat_t));
			continue;
		}
		ivp->ocpu = cpu;
		ivp->ncpu = cpu;
		cpup = bal_cpups[cpu];
		/*
		 * If Int is on an offline cpu, leave it on the unassigned list
		 * we will assign it when the cpu comes online.
		 */
		if (cpup->offline)
			continue;
		BAL_DEBUG((CE_NOTE, "ino %x cookie %x assigned to cpu %d",
		    ivp->inum, ivp->cookie, ivp->ocpu));
		mutex_enter(&ua_ivec_list_lock);
		list_remove(&bal_uaivlist, ivp);
		mutex_exit(&ua_ivec_list_lock);
		if (ivp->ih->ih_type == DDI_INTR_TYPE_MSI &&
		    (ivp->ih->ih_cap & DDI_INTR_FLAG_BLOCK)) {
			/*
			 * Part of a msi vector group.  The group must all
			 * Stay together on the same cpu so we will collapse
			 * The group down to a single struct to hold the info
			 * for the whole group.
			 */
			bal_assign_msi_group(ivp, cpup);
			continue;
		}
		if (ivp->ih->ih_type == DDI_INTR_TYPE_FIXED) {
			/*
			 * We need to check if this is a shared fixed int.
			 */
			bal_assign_fixed(ivp, cpup);
			continue;
		}
		list_insert_head(&cpup->ivecs, ivp);
		cpup->nvec++;
#ifdef DEBUG
		if (cpup->nvec > bal_maxnvec && !cpup->offline) {
			bal_maxnvec = cpup->nvec;
			bal_hicpu = cpup->cpuid;
		}
#endif	/* DEBUG */
	}
	mutex_exit(&ivec_list_lock);
}

/*
 * Gather the data intrd uses to evaluate per-cpu I/O load.
 * Returns the number of online cpus found.
 */
static int
bal_gather_load_data_intrd(void)
{
	int i;
	int cpucnt = 0;
	int curdel;
	uint64_t nsnaps;
	hrtime_t tinttime;
	ivecdat_t *ivdp;
	list_t *lp;
	cpu_loadinf_t *cp;
	cpu_t *cpup;

	uint64_t idle_ns, user_ns, kernel_ns, **tp;
	hrtime_t msnsecs[NCMSTATES];
	hrtime_t newdtime;

	/*
	 * Check that all ivecs are assigned to cpus
	 */
	bal_assign_ints();
	/*
	 * To minimize impact we gather cpu load data without taking any locks
	 * The data should be consistent enough for our purposes.
	 */
	/*
	 * Pick up local copy of the delta index.
	 */
	mutex_enter(&bal_balance_lock);
	nsnaps = ++bal_nsnaps;
	curdel = bal_curdelta;
	mutex_exit(&bal_balance_lock);
	/*
	 * Gather cpu load information, off-line cpus are skipped.
	 */
	baddata[curdel] = 0;
	cp = list_head(&bal_cpulist);
	for (; cp != NULL; cp = list_next(&bal_cpulist, cp)) {
		if (cp->offline)
			continue;
		cpup = cpu[cp->cpuid];
		cpucnt++;
		get_cpu_mstate(cpup, msnsecs);
		idle_ns = msnsecs[CMS_IDLE];
		user_ns = msnsecs[CMS_USER];
		kernel_ns = msnsecs[CMS_SYSTEM];
		cp->lastload = cp->curload;
		cp->curload = idle_ns + user_ns + kernel_ns;
		/*
		 * If we have more than one cpu stat snapshot
		 * calculate new delta
		 */
		if (nsnaps > 1) {
			if (cp->lastload <= cp->curload) {
				cp->tload[curdel] = cp->curload - cp->lastload;
			} else { /* load decreased? (should not happen) */
				cp->tload[curdel] = 0;
				baddata[curdel]++;
			}
		}
		/*
		 * Iterate over this cpus interrupt vectors collecting the
		 * amount of time spent in that vectors isr.
		 * We expect that we have been notified as to which vectors
		 * are on which cpus and we only look at vectors we have
		 * been notified of.
		 */
		lp = &cp->ivecs;
		/*
		 * Holding the ivec list lock here protects us from having
		 * the tick pointer yanked out from under us as the interrupt
		 * disable code will need to get the lock before it can
		 * complete.
		 */
		mutex_enter(&ivec_list_lock);
		ivdp = list_head(lp);
		tinttime = 0;
		for (; ivdp != NULL; ivdp = list_next(lp, ivdp)) {
			if (ivdp->ticksp == NULL)
				continue;
			/*
			 * Check if ivec moved to another cpu
			 * If it did reset our stat gathering and bail out.
			 */
			if (bal_get_affinity(ivdp) != cp->cpuid) {
				mutex_exit(&ivec_list_lock);
				/*
				 * Fake a cpu online to force rediscovery
				 * of all ivec locations.
				 */
				bal_onlinedcpus = 1;
				mutex_enter(&bal_balance_lock);
				bal_clear_deltas();
				mutex_exit(&bal_balance_lock);
				baddata[curdel]++;
				return (1);
			}
			ivdp->lastsnap = ivdp->cursnap;
			if (ivdp->inocnt < 2) { /* only one ticksp */
				ivdp->cursnap = (hrtime_t)(*ivdp->ticksp);
			} else { /* sum ticks for ivec group */
				ivdp->cursnap = 0;
				tp = (uint64_t **)ivdp->ticksp;
				for (i = 0; i < ivdp->inocnt; i++) {
					if (tp[i] == NULL)
						continue;
					ivdp->cursnap += (hrtime_t)*tp[i];
				}
			}
			scalehrtime(&ivdp->cursnap);
			/*
			 * If we have more than one stat snapshot
			 * calculate a new delta.
			 * Add delta to total isr time for this cpu and
			 * update the max isr time if this is larger than prev.
			 */
			if (nsnaps <= 1)
				continue;
			if (ivdp->lastsnap <= ivdp->cursnap) {
				newdtime = ivdp->cursnap - ivdp->lastsnap;
			} else { /* cumulative time in isr decreased? */
				newdtime = 0;
				baddata[curdel]++;
			}
			ivdp->inttime[curdel] = newdtime;
			tinttime += newdtime;
#ifdef DEBUG
			if (bal_force_rebal && bal_hicpu == cp->cpuid &&
			    nsnaps > BAL_NDELTAS) {
				/*
				 * fake a high load on cpu with
				 * the most ivecs to force a rebalance
				 */
				for (i = 0; i < BAL_NDELTAS; i++) {
					newdtime = cp->tload[i] / cp->nvec;
					ivdp->inttime[i] = newdtime;
				}
			}
#endif  /* DEBUG */
		}
		mutex_exit(&ivec_list_lock);
		/*
		 * Sanity check interrupt time vs cpu load
		 */
		if (nsnaps > 1 && cp->tload[curdel] < tinttime)
			cp->tload[curdel] = tinttime;
	}
	/*
	 * Update delta index unless it was reset while we were
	 * gathering stats.
	 */
	mutex_enter(&bal_balance_lock);
	if (nsnaps == bal_nsnaps) {
		if (nsnaps > 1)
			if (++bal_curdelta == BAL_NDELTAS)
				bal_curdelta = 0;
	}
	mutex_exit(&bal_balance_lock);
	return (cpucnt);
}

/*
 * Gather the data used to evaluate per-cpu numa I/O load.
 * Returns the number of online cpus found.
 * XXX - TBD
 */
static int
bal_gather_load_data_numa(void)
{
	return (1);
}

/*
 * Gather the data we use to evaluate per-cpu I/O load.
 * Returns the number of online cpus found.
 */
static int
bal_gather_load_data(void)
{
	int cpucnt;

	cpucnt = 1;
	switch (bal_rebalance_strategy) {
	case BAL_INTRD_STRATEGY:
		cpucnt = bal_gather_load_data_intrd();
		break;
	case BAL_NUMA_STRATEGY:
		cpucnt = bal_gather_load_data_numa();
		break;
	default:
		break;
	}
	return (cpucnt);
}

/*
 * Compress the deltas down to a single delta for the entire time covered
 * by the delta list. Returns non-zero if the compression succeeded.
 */
static int
bal_compress_deltas()
{
	int i, cpucnt;
	uint64_t cintrnsec = 0;
	int cintrload = 0;
	ivecdat_t *ivdp;
	list_t *lp;
	cpu_loadinf_t *cp;
	hrtime_t tload;

	ASSERT(MUTEX_HELD(&bal_balance_lock));
	cpucnt = 0;
	cp = list_head(&bal_cpulist);
	for (; cp != NULL; cp = list_next(&bal_cpulist, cp)) {
		if (cp->offline)
			continue;
		cp->bigintr = 0;
		cp->cinttime = 0;
		cp->cload = 0;
		/*
		 * Sum the loads and time in interrupts for each cpu
		 */
		for (i = 0; i < BAL_NDELTAS; i++)
			cp->cload += cp->tload[i];
		lp = &cp->ivecs;
		mutex_enter(&ivec_list_lock);
		ivdp = list_head(lp);
		for (; ivdp != NULL; ivdp = list_next(lp, ivdp)) {
			ivdp->tot_inttime = 0;
			for (i = 0; i < BAL_NDELTAS; i++)
				ivdp->tot_inttime += ivdp->inttime[i];
			if (ivdp->tot_inttime > cp->bigintr)
				cp->bigintr = ivdp->tot_inttime;
			cp->cinttime += ivdp->tot_inttime;
		}
		mutex_exit(&ivec_list_lock);
		tload = cp->cload;
		if (tload == 0)
			tload = 1;
		cp->intrload = (cp->cinttime * 100) / tload;
		cintrload += cp->intrload;
		cintrnsec += cp->cinttime;
		cpucnt++;
	}
	if (cpucnt > 0) {
		bal_avgintrload = cintrload / cpucnt;
		bal_avgintrnsec = cintrnsec / cpucnt;
	} else {
		bal_avgintrload = 0;
		bal_avgintrnsec = 0;
	}
	return (1);
}

#define	BAL_GOODNESS_UNSAFE	90
#define	BAL_GOODNESS_MINDELTA	10

/*
 * Calculate "goodness" heuristic for an individual cpu
 */
static int
bal_goodness_cpu(cpu_loadinf_t *cp)
{
	int goodness;
	int load, load_no_bigintr;
	hrtime_t tload;

	goodness = 0;
	load = cp->intrload;
	if (load < bal_avgintrload) /* low load is fine */
		goto out;
	/*
	 * Calculate load_no_bigintr, which represents the load
	 * due to interrupts, excluding the one biggest interrupt.
	 * This is the most gain we can get on this CPU from
	 * offloading interrupts.
	 */
	tload = cp->cload;
	if (tload == 0)
		tload = 1;
	load_no_bigintr = ((cp->cinttime - cp->bigintr) * 100) / tload;
	/*
	 * A major imbalance is indicated if a CPU is saturated
	 * with interrupt handling, and it has more than one
	 * source of interrupts. Those other interrupts could be
	 * starved if of a lower pil. Return a goodness of 100,
	 * which is the worst possible return value,
	 * which will effectively contaminate this entire delta.
	 */
	if (load > BAL_GOODNESS_UNSAFE && cp->nvec > 1) {
		goodness = 100;
		goto out;
	}
	goodness = load - bal_avgintrload;
	if (goodness > load_no_bigintr)
		goodness = load_no_bigintr;
out:
	return (goodness);
}

/*
 * Calculate a heuristic which describes how good (or bad) the current
 * interrupt balance is.  The value returned will be between 0 and 100,
 * with 0 reoresenting maximum goodness and 100 representing maximum badness.
 */
static int
bal_calc_goodness(void)
{
	int high_goodness, goodness;
	cpu_loadinf_t *cp;

	high_goodness = 0;
	cp = list_head(&bal_cpulist);
	for (; cp != NULL; cp = list_next(&bal_cpulist, cp)) {
		if (cp->offline)
			continue;
		goodness = bal_goodness_cpu(cp);
		if (goodness == 100) { /* worst case, no need to continue */
			high_goodness = goodness;
			break;
		}
		if (goodness > high_goodness)
			high_goodness = goodness;
	}
	return (high_goodness);
}

/*
 * bal_imbalanced() is used to determine if the goodness
 * has shifted far enough from our last baseline to warrant a reassignment
 * of interrupts. A very high goodness indicates that a CPU is way out of
 * whack. If the goodness has varied too much since the baseline, then
 * perhaps a reconfiguration is worth considering.
 */
static int
bal_imbalanced(int goodness)
{
	if (goodness > 50 ||
	    ABS(goodness - bal_baseline_goodness) > BAL_GOODNESS_MINDELTA)
		return (1);
	else
		return (0);
}

/*
 * Load evaluation strategy of original intrd implementation.  Simple
 * balancing among all online cpus.  No NUMA awareness.
 * Returns non-zero if rebalancing is needed.
 */
static int
bal_evaluate_load_intrd()
{
	int rebal = 0;

	/*
	 * If we are here we have a full minute's worth of deltas so we want
	 * to check if the I/O load needs rebalancing.
	 */
	mutex_enter(&bal_balance_lock);
	if (bal_nsnaps < BAL_NDELTAS + 1)
		goto out;
	if (bal_compress_deltas() == 0)
		goto out;
	mutex_exit(&bal_balance_lock);
	/*
	 * Calculate "goodness" of the current load
	 */
	bal_goodness = bal_calc_goodness();
#ifdef DEBUG
	if (bal_force_rebal) /* set goodness to indicate we need a rebalance */
		bal_goodness = 100;
#endif	/* DEBUG */
	rebal = bal_imbalanced(bal_goodness);
	return (rebal);
out:
	mutex_exit(&bal_balance_lock);
	return (rebal);
}

/*
 * Load evaluation strategy using extended statistics and numa framework
 * information.
 * XXX - TBD
 */
static int
bal_evaluate_load_numa()
{
	return (0);
}

/*
 * Evaluate whether rebalancing is called for based on the current balancing
 * strategy.
 * Returns non-zero if rebalancing is needed.
 */
static int
bal_evaluate_load(void)
{
	int rebalance = 0;

	switch (bal_rebalance_strategy) {
	case BAL_INTRD_STRATEGY:
		rebalance = bal_evaluate_load_intrd();
		break;
	case BAL_NUMA_STRATEGY:
		rebalance = bal_evaluate_load_numa();
		break;
	default:
		/*
		 * unknown strategy selected, disable rebalancing
		 */
		bal_balancer_disable = 1;
		break;
	}
	return (rebalance);
}

/*
 * Push an ivec struct on the stack used in the bal_find_goal depth-first
 * search.
 */
static void
bal_push(ivecdat_t **stack, ivecdat_t *ivp)
{
	ivp->istack = *stack;
	*stack = ivp;
}

/*
 * Pop an ivec struct off the stack used in the bal_find_goal depth-first
 * search.
 */
static ivecdat_t *
bal_pop(ivecdat_t **stack)
{
	ivecdat_t *ivp;

	ivp = *stack;
	ASSERT(ivp != NULL);
	*stack = ivp->istack;
	return (ivp);
}

/*
 * Mark all the ivecs that are to be used for the current best set
 * to achieve the desired load.  If the drp arg is non-null then
 * all ivecs in the list after and including that one are in the set.
 */
static void
bal_markbest(ivecdat_t *drp)
{
	ivecdat_t *ivp;
	int64_t bestsum;
	int markit;

	markit = 0;
	bestsum = 0;
	ivp = list_head(&bal_sivlist);
	for (; ivp != NULL; ivp = list_next(&bal_sivlist, ivp)) {
		if (ivp == drp)
			markit = 1;
		if (ivp->ingoal || markit) {
			ivp->inbest = 1;
			bestsum += ivp->tot_inttime;
			BAL_DEBUG((CE_NOTE, "  0x%x-in ocpu %d ncpu %d",
			    ivp->inum, ivp->ocpu, ivp->ncpu));
		} else {
			ivp->inbest = 0;
			BAL_DEBUG((CE_NOTE, "  0x%x-out ocpu %d ncpu %d",
			    ivp->inum, ivp->ocpu, ivp->ncpu));
		}
	}
	BAL_DEBUG((CE_NOTE, "current best sum is %lld", (long long)bestsum));
}

/*
 * Check if the current set of ivecs is the best match for the given goal
 * We want the closest match that exceeds the goal, and if we can't reach
 * the goal then the closest match that still falls short.  We return
 * the delta we are from our goal if below the goal the delta is negative.
 */
static int64_t
bal_checkgoal(ivecdat_t *ivp, uint64_t goal, int64_t best, int dorest)
{
	int64_t newbest, test;
	ivecdat_t *drp;

	newbest = best;
	drp = NULL;
	if (dorest) { /* dorest is set if we are below goal */
		if (best >= 0)
			goto done; /* we already have a better solution */
		test = -((int64_t)(goal - ivp->loadsum));
		if (test > best) {
			newbest = test;
			drp = ivp;
		}
		goto done;
	}
	if (best < 0) {
		if (ivp->tot_inttime < goal)
			test = -((int64_t)(goal - ivp->tot_inttime));
		else
			test = ivp->tot_inttime - goal;
		if (test > best)
			newbest = test;
	} else {
		if (ivp->tot_inttime < goal)
			goto done; /* can't improve */
		test = ivp->tot_inttime - goal;
		if (test < best)
			newbest = test;
	}
done:
	if (newbest != best) {
		/*
		 * Mark all vectors as to whether they are in the
		 * new best match or not.
		 */
		BAL_DEBUG((CE_NOTE, "newbest: %lld, old best %lld",
		    (long long)newbest, (long long)best));
		bal_markbest(drp);
	}
	return (newbest);
}

/*
 * bal_find_goal() is used to find the best
 * combination of interrupts in order to generate a load that is as close
 * as possible to a goal load without falling below that goal. Before returning
 * bal_find_goal() sets the inbest flag in the ivec struct of each interrupt,
 * which if set signifies that this interrupt is one of the interrupts
 * identified as part of the set of interrupts which best meet the goal.
 * The best fit is determined by performing a depth-first search on the tree
 * that represents all possible combinations of the interrupts.  Unfortunately,
 * This is an 2**n algorithm, in the number of interrupts to be balanced.
 * This is equivalent to the Subset Sum problem, which is a variant of the
 * Knapsack problem, which of course is NP-complete.  There are various
 * heuristics to speed the search, but this could get ugly in the face of
 * large n.
 */
static void
bal_find_goal(uint64_t goal)
{
	ivecdat_t *ivp, *istack, *nivp;
	int64_t best;

	BAL_DEBUG((CE_NOTE, "GOAL: inums should total %lld", (long long)goal));
	best = INT64_MIN;
	istack = NULL;
	ivp = list_head(&bal_sivlist);
	ivp->ingoal = 1;
	ivp->goalarg = goal;
	ivp->do_without = 0;
	bal_push(&istack, ivp);
	while (istack != NULL) {
		ivp = bal_pop(&istack);
		goal = ivp->goalarg;
		BAL_DEBUG((CE_NOTE, "inum: 0x%x goal %lld, inttot %lld",
		    ivp->inum, (long long)goal, (long long)ivp->tot_inttime));
		if (ivp->do_without)
			ivp->ingoal = 0;
		/*
		 * If we include all remaining items and we're still below goal,
		 * stop here. We can just return a result that includes this
		 * and all subsequent ivecs. Since this will still be below
		 * goal, there's nothing better to be done.
		 */
		if (ivp->loadsum <= goal) {
			best = bal_checkgoal(ivp, goal, best, 1);
			BAL_DEBUG((CE_NOTE,
			    "inum: 0x%x taking rest, loadsum %lld", ivp->inum,
			    (long long)ivp->loadsum));
			continue;
		}
		/*
		 * We are searching the subtree with this vector included
		 * Evaluate the "with" option, i.e. the best matching goal
		 * which includes this ivec. If its load is more than our goal
		 * load, stop here. Once we're above the goal, there is no need
		 * to consider further interrupts since they'll only take us
		 * further from the goal.
		 */
		if (ivp->ingoal) {
			if (goal <= ivp->tot_inttime) {
				best = bal_checkgoal(ivp, goal, best, 0);
			} else {
				/*
				 * Try subtree with this load included
				 */
				nivp = list_next(&bal_sivlist, ivp);
				if (nivp != NULL) {
					/*
					 * Push ourselves back on the stack
					 * so when we finish the "with" subtree
					 * we will come back and do the
					 * "without" subtree,
					 */
					ivp->do_without = 1;
					bal_push(&istack, ivp);
					nivp->ingoal = 1;
					nivp->goalarg = goal - ivp->tot_inttime;
					nivp->do_without = 0;
					bal_push(&istack, nivp);
					continue;
				} else {
					best =
					    bal_checkgoal(ivp, goal, best, 0);
				}
			}
			ivp->ingoal = 0;
		}
		/*
		 * Evaluate the "without" option, i.e. the best matching goal
		 * which excludes this ivec.
		 */
		nivp = list_next(&bal_sivlist, ivp);
		if (nivp != NULL) {
			/*
			 * Start with the "with" subtree of this node
			 */
			nivp->ingoal = 1;
			nivp->goalarg = goal;
			nivp->do_without = 0;
			bal_push(&istack, nivp);
		} else {
			best = bal_checkgoal(ivp, goal, best, 0);
		}
		if (best == 0)
			break; /* can't improve on this */
	}

}

/*
 * Move the given ivec from one cpu to another.  This routine just shuffles
 * the ivec data structures between the cpus.  It does NOT actually reassign
 * the interrupt.  The original cpu where interrupts are still being delivered
 * has been recorded in the ivec struct ocpu field.  The shuffling is being
 * done as part of an interrupt rebalance attempt.
 */
void
bal_move_intr(ivecdat_t *ivp, cpu_loadinf_t *fromp, cpu_loadinf_t *top)
{
	list_t *lp;
	ivecdat_t *ivdp;
	hrtime_t bigintr, tload;

	BAL_DEBUG((CE_NOTE, "moving ino 0x%x from cpu %d to cpu %d",
	    ivp->inum, fromp->cpuid, top->cpuid));
	if (fromp->cinttime >= ivp->tot_inttime)
		fromp->cinttime -= ivp->tot_inttime;
	else
		cmn_err(CE_NOTE, "!move_intr: intr's time > total time?");
	tload = fromp->cload;
	if (tload == 0)
		tload = 1;
	fromp->intrload = (fromp->cinttime * 100) / tload;
	/*
	 * Recalculate bigintr if this is the biggest interrupt on old cpu
	 */
	if (ivp->tot_inttime >= fromp->bigintr) {
		lp = &fromp->ivecs;
		ivdp = list_head(lp);
		bigintr = 0;
		for (; ivdp != NULL; ivdp = list_next(lp, ivdp)) {
			if (ivdp->tot_inttime > bigintr)
				bigintr = ivdp->tot_inttime;
		}
		fromp->bigintr = bigintr;
	}
	/*
	 * We don't need to actually remove the ivec from the source cpu
	 * We are called after all the ivecs on both cpus have been moved
	 * to a separate sorted ivec list.  The code calling us has just
	 * removed the ivec from the sorted list and wants us to place it
	 * on the target cpu's ivec list.
	 */
	fromp->nvec--;
	list_insert_head(&top->ivecs, ivp);
	top->nvec++;
	ivp->ncpu = top->cpuid;
	top->cinttime += ivp->tot_inttime;
	tload = top->cload;
	if (tload == 0)
		tload = 1;
	top->intrload = (top->cinttime * 100) / tload;
	if (ivp->tot_inttime > top->bigintr)
		top->bigintr = ivp->tot_inttime;
}

/*
 * Sanity check the rebalanced cpus
 */
static void
bal_move_intr_check(cpu_loadinf_t *srcp, cpu_loadinf_t *tgtp)
{
	if (srcp->cload < srcp->cinttime)
		cmn_err(CE_WARN, "!Moved ints left 100+%% load on src cpu");
	if (tgtp->cload < tgtp->cinttime)
		cmn_err(CE_WARN, "!Moved ints left 100+%% load on tgt cpu");
}

/*
 * Juggle interrupts between source cpu (with a high interrupt load) and
 * target cpu (with a lower interrupt load).
 */
static void
bal_reconfig_cpu2cpu(cpu_loadinf_t *srcp, cpu_loadinf_t *tgtp, int srcload)
{
	ivecdat_t *ivp, *sivp, *nivp;
	list_t *lp;
	hrtime_t src_numatot, tgt_numatot, loadsum, tload;
	uint64_t goal, avgnsec;
	processorid_t srcid, tgtid;
	int newload;

	BAL_DEBUG((CE_NOTE, "balancing cpu %d load %d and cpu %d load %d",
	    srcp->cpuid, srcp->intrload, tgtp->cpuid, tgtp->intrload));
	ASSERT(MUTEX_HELD(&ivec_list_lock));
	srcid = srcp->cpuid;
	tgtid = tgtp->cpuid;
	src_numatot = 0;
	tgt_numatot = 0;
	/*
	 * First, make a single list with all of the ivecs from both
	 * CPUs, sorted from highest to lowest load.  We don't allow
	 * considering moving ivecs that have been marked as managed by
	 * the NUMA framework.
	 */
	lp = &srcp->ivecs;
	ivp = list_head(lp);
	for (; ivp != NULL; ivp = nivp) {
		nivp = list_next(lp, ivp);
		/*
		 * Need to keep track of numa managed ivec time
		 * to add in when determining rebalanced load.
		 */
		if (ivp->numa_managed) {
			src_numatot += ivp->tot_inttime;
			continue;
		}
		list_remove(lp, ivp);
		ivp->ingoal = 0;
		ivp->inbest = 0;
		/*
		 * Find proper spot and insert in sorted ivecs list
		 */
		sivp = list_head(&bal_sivlist);
		for (; sivp != NULL; sivp = list_next(&bal_sivlist, sivp)) {
			if (ivp->tot_inttime > sivp->tot_inttime)
				break;
		}
		list_insert_before(&bal_sivlist, sivp, ivp);
	}
	lp = &tgtp->ivecs;
	ivp = list_head(lp);
	for (; ivp != NULL; ivp = nivp) {
		nivp = list_next(lp, ivp);
		/*
		 * Need to keep track of numa managed ivec time
		 * to add in when determining rebalanced load.
		 */
		if (ivp->numa_managed) {
			tgt_numatot += ivp->tot_inttime;
			continue;
		}
		list_remove(lp, ivp);
		ivp->ingoal = 0;
		ivp->inbest = 0;
		/*
		 * Find proper spot and insert in sorted ivecs list
		 */
		sivp = list_head(&bal_sivlist);
		for (; sivp != NULL; sivp = list_next(&bal_sivlist, sivp)) {
			if (ivp->tot_inttime > sivp->tot_inttime)
				break;
		}
		ivp->ncpu = tgtid;
		list_insert_before(&bal_sivlist, sivp, ivp);
	}
	/*
	 * Now have sorted list of all ivecs from both cpus minus
	 * the NUMA framework managed ivecs.  Also have the total
	 * times in each cpu in numa managed ivecs.
	 */
	if (list_is_empty(&bal_sivlist))
		return; /* bail if no movable ivecs */
	/*
	 * Calculate loadsums for the sorted ivecs, it helps optimize the
	 * depth-first search for the best load.
	 */
	sivp = list_tail(&bal_sivlist);
	loadsum = 0;
	for (; sivp != NULL; sivp = list_prev(&bal_sivlist, sivp)) {
		loadsum += sivp->tot_inttime;
		sivp->loadsum = loadsum;
	}
	/*
	 * Our "goal" load for src cpu is the average load across all CPUs.
	 * use bal_find_goal() to determine the optimum selection of the
	 * available interrupts which comes closest to this goal without
	 * falling below the goal.
	 */
	goal = bal_avgintrnsec;
	/*
	 * We know that the interrupt load on tgt cpu is less than that on
	 * src cpu, but its load could still be above avgintrnsec. Don't
	 * choose a goal which would bring src cpu below the load on tgt cpu.
	 */
	avgnsec = (srcp->cinttime + tgtp->cinttime) / 2;
	if (goal < avgnsec)
		goal = avgnsec;
	/*
	 * Now we must adjust the goal to be a goal for movable interrupts.
	 * i.e. those that are not numa bound.
	 * We want to move the entire list if the numa bound ivecs already
	 * exceed the goal.  That is accomplished by simply skipping find
	 * goal since currently none of the ivecs should be marked by the
	 * inbest flag as needing to be on the source cpu.
	 */
	if (goal > src_numatot) {
		goal -= src_numatot;
		bal_find_goal(goal);
	}
	/*
	 * bal_find_goal() returned its results to us by setting inbest if
	 * the ivec should be on src cpu, or clearing it for tgt cpu.
	 * Call bal_move_intr() to update our cpu stats with the new results.
	 * We actually link the ivec structs to the new target cpus now as well.
	 */
	sivp = list_remove_head(&bal_sivlist);
	for (; sivp != NULL; sivp = list_remove_head(&bal_sivlist)) {
		/*
		 * Link the sorted ivec list vecs to the proper cpu
		 * ivec list as indicated by the results of bal_find_goal.
		 */
		ASSERT(sivp->ncpu == srcid || sivp->ncpu == tgtid);
		if (sivp->inbest) {
			if (sivp->ncpu != srcid)
				bal_move_intr(sivp, tgtp, srcp);
			else
				list_insert_tail(&srcp->ivecs, sivp);
		} else {
			if (sivp->ncpu != tgtid)
				bal_move_intr(sivp, srcp, tgtp);
			else
				list_insert_tail(&tgtp->ivecs, sivp);
		}
	}
	bal_move_intr_check(srcp, tgtp);
	tload = srcp->cload;
	if (tload == 0)
		tload = 1;
	newload = (srcp->cinttime * 100) / tload;
	if (newload > srcload || newload < bal_avgintrload) {
		BAL_DEBUG((CE_NOTE, "newload %d, srcload %d", newload,
		    srcload));
		cmn_err(CE_NOTE,
		    "!cpu2cpu: new load didn't end up in expected range");
	}
}

/*
 * Routine to rejuggle interrupts between given cpu and
 * other CPUs found on the sorted cpu list so as to improve the load on
 * the given cpu. We traverse sorted cpu list backwards
 * from lowest to highest interrupt load. One at a
 * time, pick a CPU off of this list of CPUs, and attempt to
 * rejuggle interrupts between the two CPUs. Don't do this if the
 * targeted CPU has a higher load than the given cpu. We're done rejuggling
 * once the given cpu's goodness falls below a threshold.
 */
static void
bal_reconfig_cpu(cpu_loadinf_t *cp)
{
	cpu_loadinf_t *scp;
	int load;

	scp = list_tail(&bal_scpulist);
	for (; scp != NULL; scp = list_prev(&bal_scpulist, scp)) {
		/*
		 * No point in trying to juggle 1 high load interrupt away
		 */
		if (cp->nvec < 2)
			break;
		if (bal_goodness_cpu(cp) < BAL_GOODNESS_MINDELTA)
			break; /* given cpu load is reduced sufficiently */
		load = cp->intrload;
		if (scp->intrload > load)
			break; /* no cpus left on list with  lower load */
		bal_reconfig_cpu2cpu(cp, scp, load);
	}
}

/*
 * Called when a rebalance attempt can't find sufficient improvement.
 * Moves all the ivec structs back to their original cpus.
 */
static void
bal_restore_ivecs()
{
	cpu_loadinf_t *cp, *ncp;
	ivecdat_t *ivp, *nivp;

	ASSERT(MUTEX_HELD(&ivec_list_lock));
	cp = list_head(&bal_cpulist);
	for (; cp != NULL; cp = list_next(&bal_cpulist, cp)) {
		ivp = list_head(&cp->ivecs);
		for (; ivp != NULL; ivp = nivp) {
			nivp = list_next(&cp->ivecs, ivp);
			if (ivp->ocpu == cp->cpuid)
				continue; /* already on right cpu */
			list_remove(&cp->ivecs, ivp);
			cp->nvec--;
			ncp = bal_cpups[ivp->ocpu];
			list_insert_tail(&ncp->ivecs, ivp);
			ivp->ncpu = ivp->ocpu;
			ncp->nvec++;
		}
	}
}


/*
 * Find the pcitool nexus node to issue an ioctl on for the given dev_info
 * pointer.  Builds the pathname to open in bal_nexus_path.
 */
static dev_info_t *
bal_get_nexus_node(ivecdat_t *ip)
{
	struct dev_info *tdip;
	struct ddi_minor_data *minordata;
	int circ;
	dev_info_t *pdip;
	char path[MAXPATHLEN];

	pdip = ip->pdip;
	if (pdip != NULL) {
		(void) ddi_pathname(pdip, path);
		(void) snprintf(bal_nexus_path, MAXPATHLEN,
		    "/devices%s:intr", path);
		return (pdip);
	}
	/*
	 * Fall back to searching for parent pci node
	 */
	tdip = (struct dev_info *)ip->dip;
	while (tdip != NULL) {
		ndi_devi_enter((dev_info_t *)tdip, &circ);
		for (minordata = tdip->devi_minor; minordata != NULL;
		    minordata = minordata->next) {
			if (strncmp(minordata->ddm_node_type, DDI_NT_INTRCTL,
			    strlen(DDI_NT_INTRCTL)) == 0) {
				pdip = minordata->dip;
				(void) ddi_pathname(pdip, path);
				(void) snprintf(bal_nexus_path, MAXPATHLEN,
				    "/devices%s:intr", path);
				ndi_devi_exit((dev_info_t *)tdip, circ);
				return (pdip);
			}
		}
		ndi_devi_exit((dev_info_t *)tdip, circ);
		tdip = tdip->devi_parent;
	}
	return (NULL);
}


/*
 * Get a ldi handle to the pci nexus that controls this int.
 */
static void
bal_get_nexus_handle(ivecdat_t *ip)
{
	dev_info_t *pdip;
	nexusinfo_t *np;

	if (ip->nexus != NULL)
		return; /* already have the info */
	np = NULL;
	pdip = bal_get_nexus_node(ip);
	if (pdip == NULL)
		goto fail;
	/*
	 * Search our current nexus list to see if this nexus already known.
	 */
	np = list_head(&bal_nexuslist);
	for (; np != NULL; np = list_next(&bal_nexuslist, np)) {
		if (strcmp(bal_nexus_path, np->nexus_name) == 0)
			break;
	}
	if (np != NULL) { /* already have a handle to required nexus */
		ip->nexus = np;
		return;
	}
	np = kmem_zalloc(sizeof (nexusinfo_t), KM_SLEEP);
	(void) strncpy(np->nexus_name, bal_nexus_path, MAXPATHLEN);
	np->nexus_name[MAXPATHLEN - 1] = 0;
	if (ldi_ident_from_major(ddi_driver_major(pdip), &np->li) != 0)
		goto fail;
	if (ldi_open_by_name(np->nexus_name, FREAD|FWRITE, kcred,
	    &np->lh, np->li) != 0)
		goto fail;
	list_insert_tail(&bal_nexuslist, np);
	ip->nexus = np;
	return;
fail:
	if (np != NULL)
		kmem_free(np, sizeof (nexusinfo_t));
}

/*
 * Close all open nexus handles, this allows other operations on those nexii.
 * We do this when disabling intrd.  When intrd is re-enabled, the nexii
 * will be re-opened so that we can use the pcitool interface.
 */
static void
bal_close_nexus_handles(void)
{
	cpu_loadinf_t *cp;
	list_t *lp;
	ivecdat_t *ivdp;
	nexusinfo_t *np;

	ASSERT(MUTEX_HELD(&bal_balance_lock));

	mutex_enter(&ivec_list_lock);
	cp = list_head(&bal_cpulist);
	for (; cp != NULL; cp = list_next(&bal_cpulist, cp)) {
		lp = &cp->ivecs;
		ivdp = list_head(lp);
		for (; ivdp != NULL; ivdp = list_next(lp, ivdp)) {
			/*
			 * Null out nexus pointer.  Will be re-filled in
			 * if we are re-enabled.
			 */
			ivdp->nexus = NULL;
		}
	}
	np = list_remove_head(&bal_nexuslist);
	for (; np != NULL; np = list_remove_head(&bal_nexuslist)) {
		if (np->lh)
			(void) ldi_close(np->lh, FREAD|FWRITE, kcred);
		if (np->li)
			ldi_ident_release(np->li);
		kmem_free(np, sizeof (nexusinfo_t));
	}
	mutex_exit(&ivec_list_lock);
}

/*
 * Get an ivec's current interrupt affinity, would like to use
 * get_intr_affinity, but it causes lock ordering problems.  Instead
 * we use the pcitool interface.
 */
static processorid_t
bal_get_affinity(ivecdat_t *ip) {
	pcitool_intr_get_t iget;
	int err;

	/*
	 * Make sure we have a handle to the controlling nexus for this ivec.
	 */
	bal_get_nexus_handle(ip);
	if (ip->nexus == NULL) {
		err = -2;
		goto fail;
	}
	/*
	 * Use layered driver here to issue an PCITOOL_DEVICE_GET_INTR ioctl
	 * to pci nexus pcitool interface
	 */
	iget.num_devs_ret = 0;
	iget.user_version = PCITOOL_VERSION;
	iget.ino = ip->inum;
	iget.cpu_id = ip->ocpu;
	/*
	 * On an APIX based system, when ivecs are moved from one cpu
	 * to another the vector can also change.  The only mechanism
	 * for getting the info relies on having the interrupt handle
	 * for the interrupt.  We pass that here using a couple of 32 bit
	 * fields that are either for output only or only used with an
	 * input flag we don't pass.
	 */
	iget.msi = (uint32_t)(((uintptr_t)ip->ih >> 32) & 0xffffffff);
	iget.sysino = (uint32_t)(((uintptr_t)ip->ih) & 0xffffffff);
	iget.flags = PCITOOL_INTR_FLAG_BALANCER;
	if (ldi_ioctl(ip->nexus->lh, PCITOOL_DEVICE_GET_INTR, (intptr_t)&iget,
	    FKIOCTL, kcred, NULL) != 0) {
		err = -1;
		goto fail;
	}
	/*
	 * If we are returned a different inum, it's because of the APIX
	 * changing our vector.  Record the new inum, we'll need it if
	 * we ever want to move this ivec oursevles.
	 */
	if (iget.ino != ip->inum)
		ip->inum = iget.ino;
	return (iget.cpu_id);
fail:
	return (err);
}

/*
 * Actually set interrupt affinity to the required cpu.  We can't use
 * set_intr_affinity to do it.  Instead use the pcitool layered driver.
 */
static void
bal_new_affinity(ivecdat_t *ip)
{
	pcitool_intr_set_t iset;

	/*
	 * Use layered driver here to issue an PCITOOL_DEVICE_SET_INTR ioctl
	 * to pci nexus pcitool interface
	 */
	bal_get_nexus_handle(ip);
	ASSERT(ip->nexus != NULL);
	iset.old_cpu = ip->ocpu;
	iset.ino = ip->inum;
	iset.msi = 0;
	iset.cpu_id = ip->ncpu;
	iset.flags = 0;
	if (ip->inocnt > 1 && ip->ih->ih_type == DDI_INTR_TYPE_MSI)
		iset.flags |= PCITOOL_INTR_FLAG_SET_GROUP;
	iset.user_version = PCITOOL_VERSION;
	if (ldi_ioctl(ip->nexus->lh, PCITOOL_DEVICE_SET_INTR, (intptr_t)&iset,
	    FKIOCTL, kcred, NULL) != 0) {
		cmn_err(CE_WARN,
		    "!I/O balancer reassign interrupt 0x%x to cpu %d failed",
		    ip->inum, ip->ncpu);
		ip->ncpu = ip->ocpu;
	} else {
		ip->ocpu = ip->ncpu;
	}
}

/*
 * Moves all interrupts that have been rebalanced to their new target cpus.
 */
static void
bal_reassign_ints()
{
	cpu_loadinf_t *cp;
	ivecdat_t *ip;

	ASSERT(MUTEX_HELD(&ivec_list_lock));
	BAL_DEBUG((CE_NOTE, "goal load %d final int assignments:",
	    bal_avgintrload));
	cp = list_head(&bal_cpulist);
	for (; cp != NULL; cp = list_next(&bal_cpulist, cp)) {
		BAL_DEBUG((CE_NOTE, "  cpu %d new load %d", cp->cpuid,
		    cp->intrload));
		ip = list_head(&cp->ivecs);
		for (; ip != NULL; ip = list_next(&cp->ivecs, ip)) {
			ASSERT(ip->ncpu == cp->cpuid);
			if (ip->ocpu == cp->cpuid) {
				BAL_DEBUG((CE_NOTE, "    vec 0x%x", ip->inum));
				continue; /* already on right cpu */
			}
			BAL_DEBUG((CE_NOTE, "    vec 0x%x moved from cpu %d",
			    ip->inum, ip->ocpu));
			bal_new_affinity(ip);
		}
	}
}

/*
 * Rebalancing strategy routine for original intrd balancing scheme. Attempts
 * to evenly distribute the interrupt load across all online cpus.
 */
static void
bal_rebalance_intrd(int reason, int cpu)
{
	int goodness, cload, newgoodness;
	cpu_loadinf_t *cp, *scp;

	if (reason == BAL_CPU_OFFLINE) {
		/*
		 * For intrd style rebalancing, we just restart the
		 * load evaluation for the new configuration.  We assume that
		 * the cpu offline code has reshuffled the interrupts onto new
		 * cpus.
		 * Move this cpus ivec list to the unassigned ivec list.
		 */
		cp = bal_cpups[cpu];
		if (cp == NULL)
			return;
		mutex_enter(&ivec_list_lock);
		if (cp->nvec != 0) {
			mutex_enter(&ua_ivec_list_lock);
			list_move_tail(&bal_uaivlist, &cp->ivecs);
			mutex_exit(&ua_ivec_list_lock);
			cp->nvec = 0;
		}
		mutex_exit(&ivec_list_lock);
		return;
	}
	if (reason == BAL_CPU_ONLINE) {
		/*
		 * Any interrupt from any cpu mught have shuffled to the
		 * newly onlined cpu.  Move all the ivecs to the unassigned
		 * list and re-do cpu affinity discovery.
		 */
		cp = list_head(&bal_cpulist);
		for (; cp != NULL; cp = list_next(&bal_cpulist, cp)) {
			mutex_enter(&ivec_list_lock);
			if (cp->nvec != 0) {
				mutex_enter(&ua_ivec_list_lock);
				list_move_tail(&bal_uaivlist, &cp->ivecs);
				mutex_exit(&ua_ivec_list_lock);
				cp->nvec = 0;
			}
			mutex_exit(&ivec_list_lock);
		}
#ifdef DEBUG
		bal_maxnvec = 0;
		bal_hicpu = 0;
#endif	/* DEBUG */
		return;
	}
	goodness = bal_goodness;
	if (reason == BAL_LOAD_BASED && goodness < BAL_GOODNESS_MINDELTA) {
		return; /* can't improve enough to be worth the effort */
	}
	cmn_err(CE_NOTE, "!Optimizing interrupt assignments");
	/*
	 * Sort the list of CPUs from highest to lowest interrupt load.
	 * Remove the top CPU from that list and attempt to redistribute
	 * its interrupts. If the CPU has a goodness below a threshold,
	 * just ignore the CPU and move to the next one. If the CPU's
	 * load falls below the average load plus that same threshold,
	 * then there are no CPUs left worth reconfiguring, and we're done.
	 */
#ifdef DEBUG
	bal_force_rebal = 0;
#endif	/* DEBUG */
	BAL_DEBUG((CE_NOTE, "rebalancing with average load %d",
	    bal_avgintrload));
	mutex_enter(&ivec_list_lock);
domore:
	cp = list_remove_head(&bal_cpulist);
	for (; cp != NULL; cp = list_remove_head(&bal_cpulist)) {
		/*
		 * Off line cpus go on to already reconfigured list.
		 */
		if (cp->offline != 0) {
			list_insert_tail(&bal_rccpulist, cp);
			continue;
		}
		/*
		 * Find proper spot and insert in sorted cpu list
		 */
		scp = list_head(&bal_scpulist);
		for (; scp != NULL; scp = list_next(&bal_scpulist, scp)) {
			if (cp->intrload > scp->intrload)
				break;
		}
		list_insert_before(&bal_scpulist, scp, cp);
		/*
		 * Interrupt vectors for this cpu were marked as
		 * being on this cpu when they were actually bound to the cpu..
		 */
	}
	cp = list_remove_head(&bal_scpulist);
	list_insert_tail(&bal_rccpulist, cp);
	cload = cp->intrload;
	if (cload <= BAL_GOODNESS_UNSAFE &&
	    cload <= (bal_avgintrload + BAL_GOODNESS_MINDELTA))
		goto done; /* no more cpus worth reconfiguring */
	if (bal_goodness_cpu(cp) >= BAL_GOODNESS_MINDELTA)
		bal_reconfig_cpu(cp);
	/*
	 * Move remaining sortlist to regular list and repeat sorting
	 * and reconfiguring the remaining cpus
	 */
	list_move_tail(&bal_cpulist, &bal_scpulist);
	if (!list_is_empty(&bal_cpulist))
		goto domore;
done:
	/*
	 * Here when all possible useful reconfiguring has been done
	 * append any remaining sorted cpus to the reconfig cpu list.
	 * Then make the reconfigured cpu list be the actual cpu list.
	 */
	list_move_tail(&bal_rccpulist, &bal_scpulist);
	list_move_tail(&bal_cpulist, &bal_rccpulist);
	/*
	 * Check for improvement.
	 * If it's good enough then actually reconfigure the cpus
	 * If not enough improvement, restore the ivec structs to
	 * their proper cpus.
	 */
	newgoodness = bal_calc_goodness();
	if ((goodness != 100 || newgoodness == 100) &&
	    ABS(goodness - newgoodness) < BAL_GOODNESS_MINDELTA) {
		/*
		 * Goodness is near optimum, don't reconfig
		 */
		bal_restore_ivecs();
		mutex_exit(&ivec_list_lock);
		/*
		 * Update baseline goodness to current goodness
		 */
		bal_baseline_goodness = goodness;
		return;
	}
	/*
	 * Actually move the interrupts to the cpus indicated by the
	 * rebalanced ivec structs.
	 */
	bal_reassign_ints();
	mutex_exit(&ivec_list_lock);
	bal_clear_deltas();
	cmn_err(CE_NOTE, "!Interrupt assignments optimized");
}

/*
 * Perform NUMA aware rebalancing of the I/O load.
 * XXX - TBD
 */
/* ARGSUSED */
static void
bal_rebalance_numa(int reason, int cpu)
{
}

/*
 * Rebalance I/O load depending on the reason given.  This routine can be
 * called if the load is out of balance or if a cpu goes on or off line.
 */
static void
bal_rebalance(int reason, int cpu)
{
	ASSERT(MUTEX_HELD(&bal_balance_lock));
	switch (bal_rebalance_strategy) {
	case BAL_INTRD_STRATEGY:
		bal_rebalance_intrd(reason, cpu);
		break;
	case BAL_NUMA_STRATEGY:
		bal_rebalance_numa(reason, cpu);
		break;
	default:
		break;
	}
}

/*
 * This function is called to reset the load data collection to the
 * initial (cleared) state.
 */
static void
bal_clear_deltas(void)
{
	ASSERT(MUTEX_HELD(&bal_balance_lock));
	bal_nsnaps = 0;
	bal_curdelta = 0; /* restart cpu load evaluation */
}

/*
 * This function is called when cpus are going on or off line.
 */
/*ARGSUSED*/
static int
bal_cpu_change(cpu_setup_t what, int cpuid, void *arg)
{
	cpu_loadinf_t *cp;
	int i;

	ASSERT(MUTEX_HELD(&cpu_lock));
	switch (what) {
	case CPU_INTR_ON:
		/*
		 * Could be a hot plugged cpu that has cpuid that we have not
		 * seen online before, possibly allocte a cpu info struct.
		 */
		ASSERT(cpuid < _ncpu);
		if (bal_cpups[cpuid] == NULL) {
			cp = kmem_zalloc(sizeof (cpu_loadinf_t), KM_SLEEP);
			cp->cpuid = cpuid;
			list_create(&cp->ivecs, sizeof (ivecdat_t),
			    offsetof(ivecdat_t, next));
			bal_cpups[cpuid] = cp;
			/*
			 * Link new cpu to next online cpu
			 */
			for (i = cpuid + 1; i < _ncpu; i++)
				if (bal_cpups[i] != NULL) {
					cp->nextid = i;
					break;
				}
			/*
			 * Link previous online cpu to new cpu
			 */
			for (i = cpuid - 1; i >= 0; i--)
				if (bal_cpups[i] != NULL) {
					bal_cpups[i]->nextid = cpuid;
					break;
				}
			if (cpuid < bal_firstcpu)
				bal_firstcpu = cpuid;
			mutex_enter(&bal_balance_lock);
			list_insert_tail(&bal_cpulist, cp);
			mutex_exit(&bal_balance_lock);
		}
		/*
		 * New cpu able to service interrupts.  May need to rebalance
		 * but it's sufficient to wait till next regular rebalance
		 * interval to make any changes.
		 */
		cp = bal_cpups[cpuid];
		/*
		 * Check if the cpu is on the going offline list and
		 * hasn't been processed yet.
		 */
		if (cp->offline == 2)
			list_remove(&bal_offlinedcpus, cp);
		cp->offline = 0;
		bal_onlinedcpus = 1;
		mutex_enter(&bal_balance_lock);
		bal_clear_deltas();
		mutex_exit(&bal_balance_lock);
		break;
	case CPU_OFF:
		/*
		 * Cpu is going offline.  We need to redistribute its I/O
		 * to other proccessors.  Unfortunately, we can't call the
		 * rebalance code directly from this callback as the cpu
		 * lock is held when the callback is made.  On SPARC in
		 * particular the cpu lock is taken in the interrupt
		 * affinity setting code.  So we just set a flag for the
		 * I/O balancer daemon thread to do the rebalance and
		 * wakeup the I/O balancer daemon.  The unfortunate
		 * side effect of having cpu_lock held here is that the
		 * cpu offline code will reassign all the interrupts from
		 * the offlined cpu to other cpus before the balancer runs
		 * so they will potentially spend a short time on non-optimal
		 * cpus.
		 */
		cp = bal_cpups[cpuid];
		if (cp == NULL)
			return (0);
		list_insert_tail(&bal_offlinedcpus, cp);
		cp->offline = 2;
		/*
		 * Kick rebalance daemon awake to do the rebalance
		 */
		cv_signal(&bal_balance_cv);
		break;
	default:
		break;
	}
	return (0);
}

char bal_args[PSARGSZ];

/*
 * Set up our process name and args depending on our configuration
 */
void
bal_setproc(void)
{
	bal_args[0] = 0;
	if (bal_balancer_disable)
		(void) strcat(bal_args, "disabled-");
	if (bal_rebalance_strategy == BAL_INTRD_STRATEGY) {
		bcopy("intrd", curproc->p_user.u_comm, 5);
		(void) strcat(bal_args, "intrd");
	} else {
		bcopy("numa_intrd", curproc->p_user.u_comm, 10);
		(void) strcat(bal_args, "numa_intrd");
	}
	bcopy(bal_args, curproc->p_user.u_psargs, strlen(bal_args) + 1);
}

void
io_balancer(void)
{
	int i, cpucnt, rebalance;
	cpu_loadinf_t *cp;
	int do_online;
	int waittime, lastalloc;
	callb_cpr_t cprinfo;

	proc_balancer = ttoproc(curthread);
	proc_balancer->p_cstime = 0;
	proc_balancer->p_stime =  0;
	proc_balancer->p_cutime =  0;
	proc_balancer->p_utime = 0;
	/*
	 * Allocate data structures to hold load evaluation data.
	 * We evaluate load every 10 seconds and keep a minutes'
	 * worth of data.  When an interrupt service routine for a device
	 * is bound to a cpu we will chain a device load statistics gathering
	 * data structure to the cpu's load collecting data structure.
	 */
	list_create(&bal_cpulist, sizeof (cpu_loadinf_t),
	    offsetof(cpu_loadinf_t, next));
	list_create(&bal_scpulist, sizeof (cpu_loadinf_t),
	    offsetof(cpu_loadinf_t, next));
	list_create(&bal_rccpulist, sizeof (cpu_loadinf_t),
	    offsetof(cpu_loadinf_t, next));
	list_create(&bal_offlinedcpus, sizeof (cpu_loadinf_t),
	    offsetof(cpu_loadinf_t, noffline));
	list_create(&bal_sivlist, sizeof (ivecdat_t),
	    offsetof(ivecdat_t, next));
	list_create(&bal_nexuslist, sizeof (nexusinfo_t),
	    offsetof(nexusinfo_t, next));
	mutex_enter(&ua_ivec_list_lock);
	if (!bal_ualist_inited) {
		bal_ualist_inited = 1;
		list_create(&bal_uaivlist, sizeof (ivecdat_t),
		    offsetof(ivecdat_t, next));
	}
	mutex_exit(&ua_ivec_list_lock);
	/*
	 * Pre-allocate the structures to hold load delta information.
	 * There is a fixed number needed for the sliding load evaluation
	 * window and pre-allocating means no chance of blocking waiting
	 * for memory when trying to gather cpu load data.
	 */
	lastalloc = -1;
	bal_cpups = kmem_zalloc(sizeof (cpu_loadinf_t *) * _ncpu, KM_SLEEP);
	for (i = 0; i < _ncpu; i++) {
		if (cpu[i] == NULL)
			continue;
		cp = kmem_zalloc(sizeof (cpu_loadinf_t), KM_SLEEP);
		cp->cpuid = i;
		if (lastalloc >= 0)
			bal_cpups[lastalloc]->nextid = i;
		else
			bal_firstcpu = i;
		lastalloc = i;
		bal_cpups[i] = cp;
		list_create(&cp->ivecs, sizeof (ivecdat_t),
		    offsetof(ivecdat_t, next));
		mutex_enter(&cpu_lock);
		if (cpu_is_nointr(cpu[i]))
			cp->offline = 1;
		mutex_exit(&cpu_lock);
		list_insert_tail(&bal_cpulist, cp);
	}
	mutex_init(&bal_balance_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&ivec_list_lock, NULL, MUTEX_DEFAULT, NULL);
	/*
	 * Set up to get a callback if cpus go on or off line
	 */
	mutex_enter(&cpu_lock);
	register_cpu_setup_func(bal_cpu_change, NULL);
	mutex_exit(&cpu_lock);
	CALLB_CPR_INIT(&cprinfo, &bal_balance_lock, callb_generic_cpr,
	    "io_balancer");
	mutex_enter(&bal_balance_lock);
	do_online = 0;
	bal_balancer_disabled = 0;
	for (;;) {
		/*
		 * Sleep until next load check interval.  If we are disabled
		 * check once an hour to see if we are re-enabled.
		 */
		if (bal_balancer_disable)
			waittime = SEC_TO_TICK(BAL_DEFWIN * 60);
		else
			waittime = SEC_TO_TICK(BAL_DEF_INTERVAL);
		bal_setproc();
		CALLB_CPR_SAFE_BEGIN(&cprinfo);
		(void) cv_reltimedwait(&bal_balance_cv, &bal_balance_lock,
		    waittime, TR_SEC);
		CALLB_CPR_SAFE_END(&cprinfo, &bal_balance_lock);
		if (bal_balancer_disable) {
			bal_close_nexus_handles();
			mutex_enter(&bal_disable_lock);
			bal_balancer_disabled = 1;
			cv_signal(&bal_disable_cv);
			mutex_exit(&bal_disable_lock);
			continue; /* balancer disabled, just go sleep again */
		} else {
			if (bal_balancer_disabled) {
				/*
				 * Transition from disabled to enabled
				 * reset the delta state.
				 */
				bal_clear_deltas();
				bal_balancer_disabled = 0;
				BAL_DEBUG((CE_NOTE, "I/O balancer enabled"));
			}
		}
		rebalance = 0;
		/*
		 * Deal with interrupts that were bound to offlined cpus
		 */
		mutex_exit(&bal_balance_lock);
		mutex_enter(&cpu_lock);
		cp = list_head(&bal_offlinedcpus);
		for (; cp != NULL; cp = list_head(&bal_offlinedcpus)) {
			list_remove(&bal_offlinedcpus, cp);
			cp->offline = 1; /* mark as off offlined list */
			mutex_exit(&cpu_lock);
			rebalance++;
			mutex_enter(&bal_balance_lock);
			bal_rebalance(BAL_CPU_OFFLINE, cp->cpuid);
			mutex_exit(&bal_balance_lock);
			mutex_enter(&cpu_lock);
		}
		if (bal_onlinedcpus) {
			/*
			 * A cpu has come online, set a flag so we deal
			 * with it properly.
			 */
			do_online = 1;
			bal_onlinedcpus = 0;
		}
		mutex_exit(&cpu_lock);
		mutex_enter(&bal_balance_lock);
		if (do_online) {
			/*
			 * A cpu has come online, any of the interrupts
			 * could have shuffled to the newly onlined cpu.
			 */
			bal_rebalance(BAL_CPU_ONLINE, 0);
			do_online = 0;
			rebalance++;
		}
		mutex_exit(&bal_balance_lock);
		/*
		 * If we rebalanced any on/offline cpus we reset and start
		 * measuring again.
		 */
		if (rebalance) {
			mutex_enter(&bal_balance_lock);
			bal_clear_deltas();
			continue;
		}
		/*
		 * Gather load data and evaluate i/o load
		 */
		cpucnt = bal_gather_load_data();
		/*
		 * If only one online cpu we can't do anything so just bail out.
		 * We also wait till we have a minute's worth of data.
		 */
		if (cpucnt <= 1 || bal_nsnaps < BAL_NDELTAS + 1) {
			mutex_enter(&bal_balance_lock);
			continue;
		}
		rebalance = bal_evaluate_load();
		mutex_enter(&bal_balance_lock);
		/*
		 * Rebalance if load evaluation indicated it's needed
		 */
		if (rebalance)
			bal_rebalance(BAL_LOAD_BASED, 0);
	}
}

/*
 * Control the I/O load balancer daemon
 */
int
io_balancer_control(int arg)
{
	int err;

	err = EINVAL;
	mutex_enter(&bal_balance_lock);
	switch (arg) {
	/* XXX - eventually case AD_BALANCER_START_NUMA: */
	case AD_BALANCER_START:
		bal_rebalance_strategy = BAL_INTRD_STRATEGY;
		bal_balancer_disable = 0;
		err = 0;
		cv_signal(&bal_balance_cv);
		mutex_exit(&bal_balance_lock);
		break;
	case AD_BALANCER_STOP:
		bal_balancer_disable = 1;
		cv_signal(&bal_balance_cv);
		mutex_exit(&bal_balance_lock);
		/*
		 * Wait till balancer actually indicates it's disabled so
		 * we know it has released the resources it holds.
		 */
		mutex_enter(&bal_disable_lock);
		while (bal_balancer_disabled == 0) {
			cv_wait(&bal_disable_cv, &bal_disable_lock);
		}
		mutex_exit(&bal_disable_lock);
		err = 0;
		break;
	default:
		mutex_exit(&bal_balance_lock);
		return (err);
	}
	return (err);
}
