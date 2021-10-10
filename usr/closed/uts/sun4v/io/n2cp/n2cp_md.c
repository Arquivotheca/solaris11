/*
 * Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
 */


/*
 * Niagara 2 Crypto Provider - Machine Description support
 */

#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/cmn_err.h>
#include <sys/kmem.h>
#include <sys/mutex.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/mdesc.h>
#include <sys/mach_descrip.h>
#include <sys/cpuvar.h>
#include <sys/n2cp.h>


#ifdef DEBUG
static void	dump_cpu2cwq_map(n2cp_cwq2cpu_map_t *);
#endif /* DEBUG */

extern int	n2cp_register_ncs_api(void);
extern void	n2cp_unregister_ncs_api(void);

static void n2cp_drain_cwq(cwq_entry_t *);
int is_kt = -1;

boolean_t
is_KT(n2cp_t *n2cp)
{
	char		*name;
	static int	is_kt = -1;

	if (is_kt == -1) {
		if (n2cp == NULL) {
			return (B_FALSE);
		}
		name = ddi_binding_name(n2cp->n_dip);
		if (strncmp(name, N2CP_BINDNAME_KT,
		    strlen(N2CP_BINDNAME_KT)) == 0)
			is_kt = 1;
		else
			is_kt = 0;
	}

	return ((is_kt == 1) ? B_TRUE : B_FALSE);
}

void
n2cp_alloc_cwq2cpu_map(n2cp_t *n2cp)
{
	int			i;
	n2cp_cwq2cpu_map_t	*c2cp = &n2cp->n_cwqmap;
	cwq_entry_t		*cep;

	/*
	 * Allocate cwq_entry structures for all CWQs.
	 */
	c2cp->m_cwqlistsz = N2CP_MAX_NCWQS * sizeof (cwq_entry_t);
	c2cp->m_cwqlist = kmem_zalloc(c2cp->m_cwqlistsz, KM_SLEEP);
	for (i = 0; i < N2CP_MAX_NCWQS; i++) {
		cep = &c2cp->m_cwqlist[i];
		cep->mm_state = CWQ_STATE_UNINIT;
		cep->mm_cwqid = -1;
		cep->mm_ncpus_online = 0;
	}
	c2cp->m_ncwqs = 0;
	c2cp->m_ncwqs_online = 0;
	c2cp->m_nextcwqidx = 0;

	/*
	 * CPUs can be DRed in/out, so allocate the slots for all CPUs
	 * on the system.
	 */
	c2cp->m_cpulistsz = N2CP_MAX_NCPUS * sizeof (cpu_entry_t);
	c2cp->m_cpulist = kmem_alloc(c2cp->m_cpulistsz, KM_SLEEP);
	for (i = 0; i < N2CP_MAX_NCPUS; i++) {
		c2cp->m_cpulist[i].mc_cpuid = i;
		c2cp->m_cpulist[i].mc_cwqid = -1;
		c2cp->m_cpulist[i].mc_state = CWQ_STATE_UNINIT;
	}

	c2cp->m_locklist = kmem_zalloc(N2CP_MAX_NCPUS *
	    sizeof (n2cp_lock_withpad_t), KM_SLEEP);
	for (i = 0; i < N2CP_MAX_NCPUS; i++) {
		mutex_init(&c2cp->m_locklist[i].lock, NULL, MUTEX_DRIVER, NULL);
	}
}


static void
n2cp_free_cep(cwq_entry_t *cep)
{
	if (cep->mm_state == CWQ_STATE_UNINIT) {
		return;
	}
	cv_destroy(&cep->mm_cv);
	cep->mm_user_cnt = 0;
	mutex_destroy(&cep->mm_lock);
	if (cep->mm_cpulistsz > 0)
		kmem_free(cep->mm_cpulist, cep->mm_cpulistsz);
	cep->mm_state = CWQ_STATE_UNINIT;
	cep->mm_cwqid = -1;
	cep->mm_ncpus_online = 0;
}


cwq_t *
n2cp_find_next_cwq(n2cp_t *n2cp)
{
	int			i, m, index;
	n2cp_cwq2cpu_map_t	*c2cp = &n2cp->n_cwqmap;

	if (c2cp->m_ncwqs <= 0)
		return (NULL);

	index = c2cp->m_nextcwqidx;

	for (i = 0; i < c2cp->m_ncwqs; i++) {
		m = index++;
		if (index >= c2cp->m_ncwqs)
			index = 0;

		if (c2cp->m_cwqlist[m].mm_state != CWQ_STATE_ONLINE) {
			continue;
		}

		c2cp->m_nextcwqidx = index;

		DBG1(n2cp, DMD, "nextcwq: cwq(%d)",
		    c2cp->m_cwqlist[m].mm_cwqid);

		return (&c2cp->m_cwqlist[m].mm_queue);
	}

	return (NULL);
}

static int
check_cpu(uint64_t cpu_id)
{
	cpu_t	*cpu;
	int	unlock = 0;
	int	state = -1;

	if (!MUTEX_HELD(&cpu_lock)) {
		mutex_enter(&cpu_lock);
		unlock = 1;
	}

	cpu = cpu_get(cpu_id);
	if (cpu) {
		state = cpu_is_online(cpu);
	}

	if (unlock) {
		mutex_exit(&cpu_lock);
	}

	return (state);
}

/*
 * Look for a CWQ structure associated with the cpu list.
 * If it does not exist, initialize a new CWQ structure.
 * Note: all m_locklist locks must be held by the caller
 */
static cwq_entry_t *
get_matching_cwq(n2cp_t *n2cp, md_t *mdp, mde_cookie_t *clistp,
    int ncpus)
{
	int			i;
	uint64_t		cpu_id;
	n2cp_cwq2cpu_map_t	*c2cp = &n2cp->n_cwqmap;
	int			qid = -1;
	cwq_entry_t		*cep;
	int			valid_cpus = 0;

	/*
	 * Go through each CPUs in the cpulist to see if any of the CPU was
	 * associated with a CWQ previously. If so, return the CWQ.
	 */
	for (i = 0; i < ncpus; i++) {
		if (md_get_prop_val(mdp, clistp[i], "id", &cpu_id)) {
			cmn_err(CE_WARN, "n2cp: CPU instance %d has no "
			    "'id' property", i);
			continue;
		}

		/* make sure cpu is valid */
		if (check_cpu(cpu_id) < 0) {
			continue;
		}

		valid_cpus++;

		if (((qid = c2cp->m_cpulist[cpu_id].mc_cwqid) != -1) &&
		    (c2cp->m_cwqlist[qid].mm_state != CWQ_STATE_UNINIT)) {
			/*
			 * This CPU is already associated with a CWQ.
			 * Simply return the CWQ.
			 */
			ASSERT(qid < N2CP_MAX_NCWQS);
			ASSERT(ncpus <= N2CP_MAX_CPUS_PER_CWQ);
			return (&c2cp->m_cwqlist[qid]);
		}
	}

	if (valid_cpus == 0) {
		return (NULL);
	}

	/*
	 * The CPU list does not match any CWQ: this must be a new CWQ
	 * added to the system.
	 */

	/*
	 * find an unused cwq structure.
	 */
	for (i = 0; i < N2CP_MAX_NCWQS; i++) {
		cep = &c2cp->m_cwqlist[i];
		if (cep->mm_state == CWQ_STATE_UNINIT)
			break;
	}

	ASSERT(i < N2CP_MAX_NCWQS);

	/*
	 * initialize the cwq structure.
	 */
	cep = &c2cp->m_cwqlist[i];
	ASSERT(cep->mm_state != CWQ_STATE_ONLINE);
	cep->mm_cwqid = i;
	cep->mm_cpulistsz = N2CP_MAX_CPUS_PER_CWQ * sizeof (int);
	cep->mm_cpulist = kmem_alloc(cep->mm_cpulistsz, KM_SLEEP);
	for (i = 0; i < N2CP_MAX_CPUS_PER_CWQ; i++) {
		cep->mm_cpulist[i] = -1;
	}
	cep->mm_state = CWQ_STATE_PENDING;
	cep->mm_nextcpuidx = 0;
	mutex_init(&cep->mm_lock, NULL, MUTEX_DRIVER, NULL);
	cv_init(&cep->mm_cv, NULL, CV_DRIVER, NULL);
	cep->mm_user_cnt = 0;
	return (cep);
}

static void
n2cp_reset_ncwqs(n2cp_t *n2cp)
{
	int			i, max = 0;
	cwq_entry_t		*cep;
	n2cp_cwq2cpu_map_t	*c2cp = &n2cp->n_cwqmap;

	for (i = 0; i < N2CP_MAX_NCWQS; i++) {
		cep = &c2cp->m_cwqlist[i];
		if (cep->mm_state != CWQ_STATE_UNINIT) {
			max = i + 1;
		}
	}
	DBG1(n2cp, DMD, "n2cp_reset_ncwqs: %d cwqs", max);
	c2cp->m_nextcwqidx = 0;
	c2cp->m_ncwqs = max;
}


/*
 * This function is called when a new CPU is DR-ed in.
 * Find out which CWQ it is associated with, and enable the CPU.
 * Note: Caller must hold all m_locklist locks
 */
int
n2cp_update_cwq2cpu_map(n2cp_t *n2cp)
{
	md_t		*mdp;
	int		num_nodes;
	mde_cookie_t	rootnode;
	mde_cookie_t	*mlistp = NULL;
	mde_cookie_t	*clistp = NULL;
	int		mlistsz;
	int		clistsz;
	int		ncpus;
	int		ncwqs_total = 0;
	int		nexecunits;
	char		*ntype;
	int		nlen;
	int		i, idx, cx;
	uint64_t	cpu_id;
	cwq_entry_t	*cep;
	n2cp_cwq2cpu_map_t	*c2cp = &n2cp->n_cwqmap;

	if ((mdp = md_get_handle()) == NULL) {
		DBG0(n2cp, DWARN,
		    "n2cp_init_cwq2cpu_map: md_get_handle failed");
		return (-1);
	}
	rootnode = md_root_node(mdp);
	if (rootnode == MDE_INVAL_ELEM_COOKIE) {
		cmn_err(CE_WARN, "n2cp: corrupted MD, no root node");
		goto done;
	}

	num_nodes = md_node_count(mdp);
	if (num_nodes <= 0) {
		cmn_err(CE_WARN, "n2cp: corrupted MD, node count is %d",
		    num_nodes);
		(void) md_fini_handle(mdp);
		return (-1);
	}

	mlistsz = clistsz = num_nodes * sizeof (mde_cookie_t);
	mlistp = (mde_cookie_t *)kmem_zalloc(mlistsz, KM_SLEEP);
	clistp = (mde_cookie_t *)kmem_zalloc(clistsz, KM_SLEEP);


	nexecunits = md_scan_dag(mdp, rootnode,
	    md_find_name(mdp, "exec-unit"), md_find_name(mdp, "fwd"), mlistp);
	if (nexecunits <= 0) {
		DBG1(n2cp, DWARN,
		    "n2cp_md_find_cwqs: no EXEC-UNITs found (cnt = %d)",
		    nexecunits);
		goto done;
	}

	/*
	 * Go through each of the EXEC-UNITs and find which are
	 * CWQs then determine which CPUs have access to that CWQ.
	 * We assign virtual id's to each CWQ we find.
	 */
	for (idx = 0; idx < nexecunits; idx++) {
		/*
		 * Scan for CWQs.
		 */
		if (md_get_prop_data(mdp, mlistp[idx], "type",
		    (uint8_t **)&ntype, &nlen) || strcmp(ntype, "cwq")) {
			continue;
		}

		ncpus = md_scan_dag(mdp, mlistp[idx],
		    md_find_name(mdp, "cpu"), md_find_name(mdp, "back"),
		    clistp);
		if (ncpus <= 0) {
			cmn_err(CE_WARN, "n2cp: no CPUs found (cnt = %d) "
			    "for CWQ id %d", ncpus, c2cp->m_ncwqs);
			/*
			 * This is an error condition since you can't
			 * get to the CWQs without going through a
			 * CPU node in the MD tree.
			 */
			continue;
		}

		cep = get_matching_cwq(n2cp, mdp, clistp, ncpus);

		if (cep == NULL) {
			DBG0(n2cp, DWARN,
			    "n2cp_update_cwq2cpu_map: "
			    "no online cpus found for cwq");
			continue;
		}

		for (cx = 0; cx < ncpus; cx++) {
			int	skipcpu = 0;
			int	online = 0;

			if (md_get_prop_val(mdp, clistp[cx], "id", &cpu_id)) {
				cmn_err(CE_WARN,
				    "n2cp: CPU instance %d has no "
				    "'id' property", cx);
				continue;
			}

			/* skip any cpu solaris does not know about */
			if ((online = check_cpu(cpu_id)) < 0) {
				continue;
			}

			/* If this CPU is already in the list, skip it */
			for (i = 0; i < N2CP_MAX_CPUS_PER_CWQ; i++) {
				if (cep->mm_cpulist[i] == cpu_id) {
					/* This CPU is already in the list */
					skipcpu = 1;
					break;
				}
			}
			if (skipcpu && online) {
				if (c2cp->m_cpulist[cpu_id].mc_state ==
				    CWQ_STATE_ONLINE) {
					/* if it is already online skip */
					continue;
				}
				c2cp->m_cpulist[cpu_id].mc_state =
				    CWQ_STATE_ONLINE;
				cep->mm_ncpus_online++;
				n2cp_online_cwq(n2cp, cep->mm_cwqid);
				continue;
			} else if (skipcpu && !online) {
				n2cp_offline_cpu(n2cp, cpu_id);
				continue;
			}

			/* fill in the empty slot in cpulist */
			for (i = 0; i < N2CP_MAX_CPUS_PER_CWQ; i++) {
				if (cep->mm_cpulist[i] == -1) {
					cep->mm_cpulist[i] = (int)cpu_id;
					break;
				}
			}
			c2cp->m_cpulist[cpu_id].mc_cwqid = cep->mm_cwqid;

			if (online) {
				c2cp->m_cpulist[cpu_id].mc_state =
				    CWQ_STATE_ONLINE;
				cep->mm_ncpus_online++;
				n2cp_online_cwq(n2cp, cep->mm_cwqid);
			} else {
				c2cp->m_cpulist[cpu_id].mc_state =
				    CWQ_STATE_OFFLINE;
			}

		}
	}
	n2cp_reset_ncwqs(n2cp);
	ncwqs_total = c2cp->m_ncwqs;

	DBG1(n2cp, DMD, "n2cp_update_cwq2cpu_map: found %d cwqs",
	    ncwqs_total);

done:
	if (mlistp != NULL)
		kmem_free(mlistp, mlistsz);
	if (clistp != NULL)
		kmem_free(clistp, clistsz);

	(void) md_fini_handle(mdp);

#ifdef DEBUG
	if (n2cp_dflagset(DATTACH))
		dump_cpu2cwq_map(c2cp);
#endif /* DEBUG */

	return (ncwqs_total);
}

/*ARGSUSED*/
void
n2cp_deinit_cwq2cpu_map(n2cp_t *n2cp)
{
	int			i, m;
	cwq_entry_t		*cep;
	n2cp_cwq2cpu_map_t	*c2cp = &n2cp->n_cwqmap;

	/* destroy cwq_entry_t */
	for (m = 0; m < c2cp->m_ncwqs; m++) {
		cep = &c2cp->m_cwqlist[m];
		n2cp_free_cep(cep);
	}
	if (c2cp->m_cwqlistsz > 0) {
		kmem_free(c2cp->m_cwqlist, c2cp->m_cwqlistsz);
		c2cp->m_cwqlist = NULL;
		c2cp->m_cwqlistsz = 0;
	}

	if (c2cp->m_cpulistsz > 0) {
		kmem_free(c2cp->m_cpulist, c2cp->m_cpulistsz);
		c2cp->m_cpulist = NULL;
		c2cp->m_cpulistsz = 0;
	}

	for (i = 0; i < N2CP_MAX_NCPUS; i++) {
		mutex_destroy(&c2cp->m_locklist[i].lock);
	}
	kmem_free(c2cp->m_locklist,
	    N2CP_MAX_NCPUS * sizeof (n2cp_lock_withpad_t));

	c2cp->m_ncwqs = 0;
}


/*
 * n2cp_map_cwq_to_cpu()
 *
 * For the given CWQ find an arbitrary CPU that
 * has access to that CWQ and return its cpu-id.
 * CPUs are selected round-robin.  Caller will
 * use this cpu-id to effectively bind to the
 * core containing the given CWQ.
 *
 * note: all m_locklist locks must be held by the caller
 */
/*ARGSUSED*/
int
n2cp_map_cwq_to_cpu(n2cp_t *n2cp, int cwq_id, int ignore_state)
{
	register int		c;
	cwq_entry_t		*cep;
	n2cp_cwq2cpu_map_t	*c2cp = &n2cp->n_cwqmap;
	int			head;

	if ((cwq_id < 0) || (c2cp->m_ncwqs < cwq_id)) {
		return (-1);
	}

	cep = &c2cp->m_cwqlist[cwq_id];
	ASSERT(cep->mm_cwqid == cwq_id);

	/* this CWQ is invalid */
	if ((cep->mm_state != CWQ_STATE_ONLINE) && !ignore_state) {
		return (-1);
	}

	/* need to protect the next cpu index */
	mutex_enter(&cep->mm_lock);

	/* find a valid CPU */
	head = cep->mm_nextcpuidx;
	do {
		c = cep->mm_cpulist[cep->mm_nextcpuidx];
		if (++cep->mm_nextcpuidx >= N2CP_MAX_CPUS_PER_CWQ) {
			cep->mm_nextcpuidx = 0;
		}
		if ((c < 0) || (c >= N2CP_MAX_NCPUS)) {
			/* skip the invalid CPU slot */
			continue;
		}

		/*
		 * If the cpu is online, return this CPU ID. Otherwise,
		 * look for another CPU.
		 */
		if (c2cp->m_cpulist[c].mc_state == CWQ_STATE_ONLINE) {
			DBG2(n2cp, DMD, "cwq2cpu: cwq(%d)->cpu(%d)", cwq_id, c);
			mutex_exit(&cep->mm_lock);
			return (c2cp->m_cpulist[c].mc_cpuid);
		}


		DBG2(n2cp, DMD, "cwq2cpu: cwq(%d)cpu(%d) was offline",
		    cwq_id, c);
	} while (head != cep->mm_nextcpuidx);

	mutex_exit(&cep->mm_lock);

	return (-1);
}

/*
 * n2cp_map_cpu_to_cwq()
 *
 * For the given CPU determine which CWQ is accessible by it.
 */
/*ARGSUSED*/
int
n2cp_map_cpu_to_cwq(n2cp_t *n2cp, int cpu_id)
{
	int			qid;
	n2cp_cwq2cpu_map_t	*c2cp = &n2cp->n_cwqmap;

	if (c2cp->m_ncwqs <= 0)
		return (-1);

	/* Look for the CWQ id for the CPU */
	qid = c2cp->m_cpulist[cpu_id].mc_cwqid;
	if (qid < 0) {
		return (-1);
	}

	/* make sure cpu is online */
	if (c2cp->m_cpulist[cpu_id].mc_state != CWQ_STATE_ONLINE) {
		DBG2(n2cp, DWARN,
		    "map cpu to cwq failed: cpu %d offline, cwq %d",
		    cpu_id, qid);
		return (-1);
	}

	if ((c2cp->m_cwqlist[qid].mm_state == CWQ_STATE_ONLINE) ||
	    (c2cp->m_cwqlist[qid].mm_state == CWQ_STATE_PENDING)) {
		return (qid);
	}

	DBG2(n2cp, DMD, "map cpu 2 cwq failed, qid %d, cpu_id %d",
	    qid, cpu_id);
	return (-1);
}

/*
 * n2cp_map_findcwq()
 *
 * Locate the CWQ entry structure for the given CWQ.
 */
cwq_entry_t *
n2cp_map_findcwq(n2cp_t *n2cp, int cwq_id)
{
	n2cp_cwq2cpu_map_t	*c2cp = &n2cp->n_cwqmap;

	ASSERT(c2cp->m_cwqlist[cwq_id].mm_cwqid == cwq_id);
	return (&c2cp->m_cwqlist[cwq_id]);
}

/*
 * n2cp_holdcwq()
 * Locate the CWQ entry structure for the given CWQ, and hold the reference
 * on it
 */
cwq_entry_t *
n2cp_holdcwq(n2cp_t *n2cp, int cwq_id, cwq_entry_t *oldcep)
{
	n2cp_cwq2cpu_map_t	*c2cp = &n2cp->n_cwqmap;
	cwq_entry_t		*cep;

	ASSERT(c2cp->m_cwqlist[cwq_id].mm_cwqid == cwq_id);

	cep = &c2cp->m_cwqlist[cwq_id];
	mutex_enter(&cep->mm_lock);

	if ((cep->mm_state != CWQ_STATE_ONLINE) &&
	    (cep->mm_state != CWQ_STATE_PENDING)) {
		DBG1(n2cp, DMD, "n2cp_holdcwq: CWQ[%d] was not online",
		    cwq_id);
		mutex_exit(&cep->mm_lock);
		return (NULL);
	}

	if (cep != oldcep) {
		cep->mm_user_cnt++;
	}
	mutex_exit(&cep->mm_lock);

	return (cep);
}

/*
 * n2cp_relecwq()
 * Release the reference on the CWQ
 */
void
n2cp_relecwq(cwq_entry_t *cep)
{
	mutex_enter(&cep->mm_lock);
	cep->mm_user_cnt--;

	ASSERT(cep->mm_user_cnt >= 0);
	if ((cep->mm_user_cnt == 0) &&
	    ((cep->mm_state == CWQ_STATE_DRAINING) ||
	    (cep->mm_state == CWQ_STATE_SUSPEND))) {
		cv_signal(&cep->mm_cv);
	}
	mutex_exit(&cep->mm_lock);
}


void
n2cp_online_cwq(n2cp_t *n2cp, int cwq_id)
{
	cwq_entry_t	*cep = n2cp_map_findcwq(n2cp, cwq_id);

	if (cep) {
		/* Only online cwq that is currently offline */
		if (cep->mm_state != CWQ_STATE_ONLINE) {
			DBG1(n2cp, DMD, "CWQ[%d] was put online\n", cwq_id);
			cep->mm_state = CWQ_STATE_ONLINE;
			n2cp->n_cwqmap.m_ncwqs_online++;
		}
		/*
		 * If this is the first CWQ, notify the framework
		 * that the provider is ready for requests
		 */
		if (!(n2cp_isregistered(n2cp))) {
			(void) n2cp_provider_register(n2cp);
			n2cp_provider_notify_ready(n2cp);
			DBG0(n2cp, DMD, "The first CWQ was put "
			    "online. Notify the framework");
		}
	}
}

/* all m_locklist locks must be held by the caller */

void
n2cp_offline_cwq(n2cp_t *n2cp, int cwq_id)
{
	cwq_entry_t		*cep = n2cp_map_findcwq(n2cp, cwq_id);

	if (!cep) {
		DBG1(n2cp, DWARN, "offline cwq failed no cwq %d",
		    cwq_id);
		return;
	}

	mutex_enter(&cep->mm_lock);

	/*  not fully online, just mark it as offline */
	if (cep->mm_state == CWQ_STATE_PENDING) {
		cep->mm_state = CWQ_STATE_OFFLINE;
		mutex_exit(&cep->mm_lock);
		return;
	}

	/* Only offline cwqs that are currently online or suspended */
	if ((cep->mm_state == CWQ_STATE_ONLINE) ||
	    (cep->mm_state == CWQ_STATE_SUSPEND)) {

		cep->mm_state = CWQ_STATE_DRAINING;

		/* drain the CWQ */
		n2cp_drain_cwq(cep);

		cep->mm_state = CWQ_STATE_OFFLINE;

		mutex_exit(&cep->mm_lock);

		DBG1(n2cp, DMD, "CWQ[%d] was taken offline\n", cwq_id);
		if (n2cp->n_cwqmap.m_ncwqs_online > 0) {
			n2cp->n_cwqmap.m_ncwqs_online--;

			/*
			 * If this is the last CWQ, unregister from the
			 * framework.
			 */
			if (n2cp->n_cwqmap.m_ncwqs_online == 0) {
				/*
				 * we have to drop m_locklist locks to avoid
				 * a deadlock unregistering. There may be jobs
				 * waiting to find a cwq.
				 */
				MAP_MUTEXES_EXIT_ALL(n2cp->n_cwqmap);
				(void) n2cp_provider_unregister(n2cp);
				MAP_MUTEXES_ENTER_ALL(n2cp->n_cwqmap);
				DBG0(n2cp, DMD, "The last CWQ was taken "
				    "offline. Notify the framework");
			}
		}
		DBG1(n2cp, DCHATTY, "n2cp_offline_cwq: m_ncwqs_online = %d",
		    n2cp->n_cwqmap.m_ncwqs_online);
	} else {
		mutex_exit(&cep->mm_lock);
	}
}



static void
n2cp_drain_cwq(cwq_entry_t *cep)
{
	int			rv, i;
	n2cp_ulcwq_buf_t	*ulcwqbuf;
	int			nbufs;
	n2cp_request_t		*reqp;
	cwq_t			*cwq = &cep->mm_queue;

	ASSERT(MUTEX_HELD(&cep->mm_lock));

	if ((cep->mm_user_cnt > 0) && n2cp_use_ulcwq) {
		/* redirect requests waiting for the cwq's memory page */
		n2cp_ulcwq_detach_waiting_threads(&cep->mm_queue);

		/* release the unused bufs */
		n2cp_ulcwq_move_unused_buf(&cep->mm_queue);
	}

	/* wait for the jobs to be drained */
	if (cep->mm_user_cnt > 0)  {
		rv = cv_reltimedwait_sig(&cep->mm_cv, &cep->mm_lock,
		    drv_usectohz(20 * SECOND), TR_CLOCK_TICK);
		if (rv <= 0) {
			cmn_err(CE_WARN, "n2cp: Unable to drain CWQ[%d]"
			    ": force offline", cep->mm_cwqid);
		}

		if (n2cp_use_ulcwq) {
			/*
			 * redirect requests that have context on the page but
			 * currently not active
			 */
			mutex_enter(&cwq->cq_ulcwq_buf_lock);
			get_ulcwqbuf_array_pars(cwq, &nbufs, &ulcwqbuf);
			for (i = 0; i < nbufs; i++) {
				if ((reqp = ulcwqbuf[i].ub_req) == NULL) {
					continue;
				}
				n2cp_move_req_off_page(reqp, KM_NOSLEEP,
				    B_FALSE);
			}
			mutex_exit(&cwq->cq_ulcwq_buf_lock);
		}

	}
}

/* all m_locklist locks must be held by the caller */
void
n2cp_online_cpu(n2cp_t *n2cp, int cpu_id)
{
	n2cp_cwq2cpu_map_t	*c2cp = &n2cp->n_cwqmap;
	cwq_entry_t		*cep;

	if (c2cp->m_cpulist[cpu_id].mc_state == CWQ_STATE_ONLINE) {
		/* the CPU is already online */
		return;
	}

	/*
	 * The cpu was added for the first time.
	 * Scan through the MD to map the CPU to a CWQ, and add it
	 * to the CWQ's CPU list.
	 */
	if (n2cp_update_cwq2cpu_map(n2cp) <= 0) {
		DBG0(n2cp, DMD, "failed to reinitialize md");
		return;
	}

	if (c2cp->m_cpulist[cpu_id].mc_cwqid < 0) {
		/* The CPU is not associated with a CWQ */
		DBG0(n2cp, DMD, "CPU[%d] is not associated with CWQ");
		return;
	}

	cep = &c2cp->m_cwqlist[c2cp->m_cpulist[cpu_id].mc_cwqid];

	if (c2cp->m_cpulist[cpu_id].mc_state !=  CWQ_STATE_ONLINE) {
		c2cp->m_cpulist[cpu_id].mc_state = CWQ_STATE_ONLINE;
		cep->mm_ncpus_online++;
	}

	if (cep->mm_state != CWQ_STATE_ONLINE) {
		n2cp_ksdeinit(n2cp);

		n2cp_online_cwq(n2cp, c2cp->m_cpulist[cpu_id].mc_cwqid);

		n2cp_ksinit(n2cp);
	}

	DBG1(n2cp, DMD, "CPU[%d] was put online\n", cpu_id);
}

/* all m_locklist locks must be held by the caller */
void
n2cp_offline_cpu(n2cp_t *n2cp, int cpu_id)
{
	n2cp_cwq2cpu_map_t	*c2cp = &n2cp->n_cwqmap;
	cwq_t			*cwq;
	cwq_entry_t		*cep;
	int			i;
	int			qid;
	int			orig_state;

	/*
	 * we can get both CPU_OFF & CPU_UNCONFIG so cpu may be offline
	 * already.
	 */
	if (c2cp->m_cpulist[cpu_id].mc_state == CWQ_STATE_OFFLINE) {
		return;
	}

	c2cp->m_cpulist[cpu_id].mc_state = CWQ_STATE_OFFLINE;

	/* Stop all jobs on the CWQ: drain the CWQ temporarily */
	qid = c2cp->m_cpulist[cpu_id].mc_cwqid;

	if (qid < 0) {
		return;
	}
	cep = &c2cp->m_cwqlist[qid];
	mutex_enter(&cep->mm_lock);
	orig_state = cep->mm_state;
	cep->mm_state = CWQ_STATE_SUSPEND;
	cep->mm_ncpus_online--;

	DBG1(n2cp, DMD, "CPU[%d] was taken offline\n", cpu_id);

	/*
	 * Drain CWQ: the CWQ will be enabled if more than one CPU is
	 * still available on the CWQ.
	 */
	n2cp_drain_cwq(cep);

	for (i = 0; i < N2CP_MAX_CPUS_PER_CWQ; i++) {
		if (cep->mm_cpulist[i] == cpu_id) {
			cep->mm_cpulist[i] = -1;
		}
	}
	if (cep->mm_ncpus_online <= 0) {
		mutex_exit(&cep->mm_lock);

		/* All CPUs on the CWQ are offline: take CWQ offline */
		n2cp_offline_cwq(n2cp, qid);

		/* Unconfigure the cwq */
		cwq = &n2cp->n_cwqmap.m_cwqlist[qid].mm_queue;
		mutex_enter(&cep->mm_lock);
		if (cwq->cq_init != 0) {
			thread_affinity_set(curthread, cpu_id);
			n2cp_cwq_q_unconfigure(n2cp, cwq);
			thread_affinity_clear(curthread);
		}
		mutex_exit(&cep->mm_lock);
	} else {
		/* more than one CPU is still available: enable the CWQ */
		cep->mm_state = orig_state;
		mutex_exit(&cep->mm_lock);
	}
}


void
n2cp_cwq_status(n2cp_t *n2cp, dr_crypto_stat_t *statp)
{
	cwq_entry_t		*cep;
	n2cp_cwq2cpu_map_t	*c2cp = &n2cp->n_cwqmap;
	int			cwq_id;
	n2cp_lock_withpad_t *mp;

	mp = &n2cp->n_cwqmap.m_locklist[statp->cpuid];
	mutex_enter(&mp->lock);
	cwq_id = n2cp_map_cpu_to_cwq(n2cp, statp->cpuid);

	DBG2(n2cp, DDR, "n2cp_cwq_status for cwq %d, cpu %d",
	    cwq_id, statp->cpuid);

	if ((cwq_id < 0) || (c2cp->m_ncwqs <= cwq_id)) {
		mutex_exit(&mp->lock);
		statp->result = DR_CRYPTO_RES_BAD_CPU;
		statp->status = DR_CRYPTO_STAT_NOT_PRESENT;
		return;
	}

	/* get the CWQ */
	cep = &c2cp->m_cwqlist[cwq_id];
	ASSERT(cep->mm_cwqid == cwq_id);

	switch (cep->mm_state) {
		case CWQ_STATE_REMOVED:
		case CWQ_STATE_UNINIT:
			statp->status = DR_CRYPTO_STAT_UNCONFIGURED;
			break;
		case CWQ_STATE_ONLINE:
		case CWQ_STATE_OFFLINE:
		case CWQ_STATE_DRAINING:
		case CWQ_STATE_ERROR:
		case CWQ_STATE_PENDING:
			statp->status = DR_CRYPTO_STAT_CONFIGURED;
			break;
	}
	mutex_exit(&mp->lock);

	statp->result = DR_CRYPTO_RES_OK;
}

void
n2cp_cwq_config(n2cp_t *n2cp, dr_crypto_res_t *resp)
{
	cwq_entry_t		*cep;
	n2cp_cwq2cpu_map_t	*c2cp = &n2cp->n_cwqmap;
	int			cwq_id;
	int			prev_ncwqs, curr_ncwqs;
	int			rv;

	/*
	 * grab cpu_lock now since n2cp_update_cwq2cpu_map
	 * needs it.  lock hierarchy requires m_locklist locks not
	 * be held when acquiring cpu_lock to avoid deadlock.
	 */
	mutex_enter(&cpu_lock);

	/* block other dr requests */
	mutex_enter(&n2cp->n_dr_lock);

	MAP_MUTEXES_ENTER_ALL(n2cp->n_cwqmap);

	/* deinitialize kstats before we mess with the mapping */
	n2cp_ksdeinit(n2cp);

	prev_ncwqs = c2cp->m_ncwqs;

	/* re-read the MD and update the cwq map */
	if ((curr_ncwqs = n2cp_update_cwq2cpu_map(n2cp)) == -1) {
		resp->result = DR_CRYPTO_RES_FAILURE;
		resp->status = DR_CRYPTO_STAT_NOT_PRESENT;
		MAP_MUTEXES_EXIT_ALL(n2cp->n_cwqmap);
		mutex_exit(&n2cp->n_dr_lock);
		mutex_exit(&cpu_lock);
		return;
	}
	mutex_exit(&cpu_lock);

	/* find the targeted CWQ using the provided cpu ID */
	cwq_id = c2cp->m_cpulist[resp->cpuid].mc_cwqid;

	DBG2(n2cp, DDR, "n2cp_cwq_config for cwq %d, cpu %d",
	    cwq_id, resp->cpuid);

	if ((cwq_id < 0) || (c2cp->m_ncwqs <= cwq_id)) {
		resp->result = DR_CRYPTO_RES_BAD_CPU;
		resp->status = DR_CRYPTO_STAT_NOT_PRESENT;
		n2cp_ksinit(n2cp);
		MAP_MUTEXES_EXIT_ALL(n2cp->n_cwqmap);
		mutex_exit(&n2cp->n_dr_lock);
		return;
	}

	cep = &c2cp->m_cwqlist[cwq_id];
	ASSERT(cep->mm_cwqid == cwq_id);

	/*
	 * If previously there were no CWQ configured then register the HV
	 * NCS API.
	 */
	if (prev_ncwqs == 0 && curr_ncwqs > 0) {
		if ((rv = n2cp_register_ncs_api()) != 0) {
			resp->result = DR_CRYPTO_RES_FAILURE;
			resp->status = DR_CRYPTO_STAT_UNCONFIGURED;
			n2cp_ksinit(n2cp);
			MAP_MUTEXES_EXIT_ALL(n2cp->n_cwqmap);
			mutex_exit(&n2cp->n_dr_lock);

			n2cp_diperror(n2cp->n_dip,
			    "n2cp_cwq_config: Failed to register "
			    "HV NCS API (%d)", rv);
			return;
		}
	}

	/*
	 * Configure the CWQ: CWQ is added to the domain but it may be
	 * disabled
	 */
	if (n2cp_update_cwq2cpu_map(n2cp) < 0) {
		resp->result = DR_CRYPTO_RES_BAD_CRYPTO;
		resp->status = DR_CRYPTO_STAT_UNCONFIGURED;
	} else {
		/* clear the kstats for the just added MAU */
		n2cp_kstat_clear(n2cp, cwq_id);

		resp->result = DR_CRYPTO_RES_OK;
		resp->status = DR_CRYPTO_STAT_CONFIGURED;
	}

	/* revise kstats */
	n2cp_ksinit(n2cp);

	MAP_MUTEXES_EXIT_ALL(n2cp->n_cwqmap);
	mutex_exit(&n2cp->n_dr_lock);
}

void
n2cp_cwq_unconfig(n2cp_t *n2cp, dr_crypto_res_t *resp)
{
	cwq_entry_t		*cep;
	n2cp_cwq2cpu_map_t	*c2cp = &n2cp->n_cwqmap;
	int			cwq_id;
	int			i;
	int			remap = 0;
	int			ncwqs;

	mutex_enter(&cpu_lock);

	/* block other DR requests */
	mutex_enter(&n2cp->n_dr_lock);

	/* block incoming requests while we update the cpu/crypto map */
	MAP_MUTEXES_ENTER_ALL(n2cp->n_cwqmap);


	/* deinit kstats before the hw map is changed */
	n2cp_ksdeinit(n2cp);

retry:
	cwq_id = c2cp->m_cpulist[resp->cpuid].mc_cwqid;

	DBG2(n2cp, DDR, "n2cp_cwq_unconfig for cwq %d, cpu %d",
	    cwq_id, resp->cpuid);

	if ((cwq_id < 0) || (c2cp->m_ncwqs <= cwq_id)) {
		/* try and remap as a last ditch effort. */
		if (!remap) {
			if (n2cp_update_cwq2cpu_map(n2cp) != -1) {
				remap = 1;
				goto retry;
			}
		}
		DBG2(n2cp, DWARN, "unconfig failed cwq id %d, ncwqs %d",
		    cwq_id, c2cp->m_ncwqs);
		n2cp_ksinit(n2cp);
		MAP_MUTEXES_EXIT_ALL(n2cp->n_cwqmap);

		mutex_exit(&n2cp->n_dr_lock);
		mutex_exit(&cpu_lock);
		resp->result = DR_CRYPTO_RES_BAD_CPU;
		resp->status = DR_CRYPTO_STAT_NOT_PRESENT;
		return;
	}

	mutex_exit(&cpu_lock);

	/* get the CWQ */
	cep = &c2cp->m_cwqlist[cwq_id];
	ASSERT(cep->mm_cwqid == cwq_id);

	/* prevent new operations for this cwq */
	cep->mm_state = CWQ_STATE_SUSPEND;

	/*
	 * fake offline all of the associated cpus, That
	 * will force the offline/unconfig of the CWQ.
	 */
	for (i = 0; i < N2CP_MAX_CPUS_PER_CWQ; i++) {
		int	cpu_id = cep->mm_cpulist[i];
		if (cpu_id != -1) {
			n2cp_offline_cpu(n2cp, cpu_id);
			/* decouple cpu from cwq */
			c2cp->m_cpulist[cpu_id].mc_cwqid = -1;
		}
	}

	n2cp_free_cep(cep);
	n2cp_reset_ncwqs(n2cp);

	/*
	 * If there are no CWQ configured then unregister the HV NCS API.
	 */
	ncwqs = n2cp->n_cwqmap.m_ncwqs;
	if (ncwqs == 0) {
		n2cp_unregister_ncs_api();
	}

	/* revise kstats */
	n2cp_ksinit(n2cp);

	MAP_MUTEXES_EXIT_ALL(n2cp->n_cwqmap);
	mutex_exit(&n2cp->n_dr_lock);
	resp->status = DR_CRYPTO_STAT_UNCONFIGURED;
	resp->result = DR_CRYPTO_RES_OK;
}

#ifdef DEBUG
static void
dump_cpu2cwq_map(n2cp_cwq2cpu_map_t *c2cp)
{
	int		m, c;
	cwq_entry_t	*cep;

	printf("DUMP cwq2cpu map (ncwqs = %d)\n", c2cp->m_ncwqs);

	for (m = 0; m < c2cp->m_ncwqs; m++) {
		cep = &c2cp->m_cwqlist[m];
		printf("DUMP:%d:cwq %d: ncpus_online = %d (nextcpuidx = %d) "
		    "state = %d\n", m, cep->mm_cwqid, cep->mm_ncpus_online,
		    cep->mm_nextcpuidx, cep->mm_state);
		for (c = 0; c < N2CP_MAX_CPUS_PER_CWQ; c++)
			printf("DUMP:%d:cwq %d (hdl = 0x%lx)-> cpu %d\n",
			    m, cep->mm_cwqid, cep->mm_queue.cq_handle,
			    cep->mm_cpulist[c]);
	}

	for (c = 0; c < N2CP_MAX_NCPUS; c++) {
		printf("DUMP:%d:cpu %d -> cwq %d\n", c,
		    c2cp->m_cpulist[c].mc_cpuid, c2cp->m_cpulist[c].mc_cwqid);
	}
}
#endif /* DEBUG */
