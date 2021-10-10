/*
 * Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
 */


/*
 * Niagara Crypto Provider - Machine Description support
 */

#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/cmn_err.h>
#include <sys/kmem.h>
#include <sys/mutex.h>

#include <sys/mdesc.h>
#include <sys/mach_descrip.h>
#include <sys/cpuvar.h>
#include <sys/ncp.h>
#include <sys/n2_crypto_dr.h>

#ifdef DEBUG
static void	dump_cpu2mau_map(ncp_mau2cpu_map_t *);
#endif /* DEBUG */

extern int	ncp_register_ncs_api(void);
extern void	ncp_unregister_ncs_api(void);

void
ncp_alloc_mau2cpu_map(ncp_t *ncp)
{
	int			i;
	ncp_mau2cpu_map_t	*m2cp = &ncp->n_maumap;
	mau_entry_t		*mep;

	/*
	 * Allocate mau_entry structures for all MAUs.
	 */
	m2cp->m_maulistsz = NCP_MAX_MAX_NMAUS * sizeof (mau_entry_t);
	m2cp->m_maulist = kmem_zalloc(m2cp->m_maulistsz, KM_SLEEP);
	for (i = 0; i < NCP_MAX_MAX_NMAUS; i++) {
		mep = &m2cp->m_maulist[i];
		mep->mm_state = MAU_STATE_UNINIT;
		mep->mm_mauid = -1;
	}
	m2cp->m_nmaus = 0;
	m2cp->m_nmaus_online = 0;
	m2cp->m_nextmauidx = 0;

	/*
	 * CPUs can be DRed in/out, so allocate the slots for all CPUs
	 * on the system.
	 */
	m2cp->m_cpulistsz = NCP_MAX_NCPUS * sizeof (cpu_entry_t);
	m2cp->m_cpulist = kmem_alloc(m2cp->m_cpulistsz, KM_SLEEP);
	for (i = 0; i < NCP_MAX_NCPUS; i++) {
		m2cp->m_cpulist[i].mc_cpuid = i;
		m2cp->m_cpulist[i].mc_mauid = -1;
		m2cp->m_cpulist[i].mc_state = MAU_STATE_UNINIT;
		mutex_init(&m2cp->m_cpulist[i].mc_lock, NULL,
		    MUTEX_DRIVER, NULL);
		cv_init(&m2cp->m_cpulist[i].mc_cv, NULL, CV_DRIVER, NULL);
		m2cp->m_cpulist[i].mc_refcnt = 0;
		m2cp->m_cpulist[i].mc_worker = NULL;
	}

	mutex_init(&m2cp->m_lock, NULL, MUTEX_DRIVER, NULL);
}


static void
ncp_free_mep(ncp_t *ncp, mau_entry_t *mep, int lock)
{
	ncp_mau2cpu_map_t	*m2cp = &ncp->n_maumap;

	if (mep->mm_state == MAU_STATE_UNINIT) {
		return;
	}

	if (mep->mm_taskq) {
		/*
		 * drop m_lock (if held) to allow taskq threads to complete.
		 */
		if (lock) {
			mutex_exit(&m2cp->m_lock);
		}
		ddi_taskq_destroy(mep->mm_taskq);
		mep->mm_taskq = NULL;
		if (lock) {
			mutex_enter(&m2cp->m_lock);
		}
	}

	cv_destroy(&mep->mm_cv);
	mep->mm_user_cnt = 0;
	mutex_destroy(&mep->mm_lock);
	if (mep->mm_cpulistsz > 0) {
		kmem_free(mep->mm_cpulist, mep->mm_cpulistsz);
		mep->mm_cpulist = NULL;
	}
	if (mep->mm_workers) {
		kmem_free(mep->mm_workers,
		    ncp->n_threads_per_core *
		    sizeof (mau_worker_t));
		mep->mm_workers = NULL;
	}
	mep->mm_state = MAU_STATE_UNINIT;
	mep->mm_mauid = -1;
	mep->mm_ncpus = 0;
}

/*
 * check the state of a cpu.
 *
 * returns:
 * 	-1 if cpu invalid
 *       0 if cpu offline
 *	 1 if cpu is online
 */
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
 * Look for an MAU structure associated with the cpu list.
 * If it does not exist, initialize a new MAU structure.
 */
static mau_entry_t *
get_matching_mep(ncp_t *ncp, md_t *mdp, mde_cookie_t *clistp,
    int ncpus)
{
	int			i;
	uint64_t		cpu_id;
	ncp_mau2cpu_map_t	*m2cp = &ncp->n_maumap;
	int			mid = -1;
	mau_entry_t		*mep;
	char 			buf[32];
	int			valid_cpus = 0;

	/*
	 * Go through each CPUs in the cpulist to see if any of the CPU was
	 * associated with an MAU previously. If so, return the MAU.
	 */
	for (i = 0; i < ncpus; i++) {
		if (md_get_prop_val(mdp, clistp[i], "id", &cpu_id)) {
			cmn_err(CE_WARN, "ncp: CPU instance %d has no "
			    "'id' property", i);
			continue;
		}

		/* make sure cpu is valid */
		if (check_cpu(cpu_id) < 0) {
			continue;
		}

		valid_cpus++;

		if (((mid = m2cp->m_cpulist[cpu_id].mc_mauid) != -1) &&
		    (m2cp->m_maulist[mid].mm_state != MAU_STATE_UNINIT)) {
			/*
			 * This CPU is already associated with an MAU.
			 * Simply return the MAU.
			 */
			ASSERT(mid < NCP_MAX_MAX_NMAUS);
			ASSERT(ncpus <= ncp->n_max_cpus_per_mau);
			return (&m2cp->m_maulist[mid]);
		}
	}

	if (valid_cpus == 0) {
		return (NULL);
	}

	/*
	 * If there is an MAU structure which is no longer in use,
	 * reuse it.
	 */
	for (i = 0; i < NCP_MAX_MAX_NMAUS; i++) {
		mep = &m2cp->m_maulist[i];
		if (mep->mm_state == MAU_STATE_UNINIT) {
			break;
		}
	}

	ASSERT(i < NCP_MAX_MAX_NMAUS);

	/*
	 * initialize the new mau entry.
	 */
	mep = &m2cp->m_maulist[i];
	ASSERT(mep->mm_state != MAU_STATE_ONLINE);
	mep->mm_mauid = i;
	mep->mm_cpulistsz = ncp->n_max_cpus_per_mau * sizeof (int);
	mep->mm_cpulist = kmem_zalloc(mep->mm_cpulistsz, KM_SLEEP);
	for (i = 0; i < ncp->n_max_cpus_per_mau; i++) {
		mep->mm_cpulist[i] = -1;
	}
	/* alloc the worker entries */
	mep->mm_workers = kmem_alloc(ncp->n_threads_per_core *
	    sizeof (mau_worker_t), KM_SLEEP);
	for (i = 0; i < ncp->n_threads_per_core; i++) {
		mep->mm_workers[i].mw_bind = NCP_MW_UNBOUND;
		mep->mm_workers[i].mw_thread = NULL;
		mep->mm_workers[i].mw_next = NULL;
	}
	mep->mm_ncpus = 0;
	mep->mm_state = MAU_STATE_PENDING;
	mep->mm_nextcpuidx = 0;
	mutex_init(&mep->mm_lock, NULL, MUTEX_DRIVER, NULL);
	cv_init(&mep->mm_cv, NULL, CV_DRIVER, NULL);

	(void) sprintf(buf, "ncp_taskq(%d)", mep->mm_mauid);

	mep->mm_taskq = ddi_taskq_create(ncp->n_dip,
	    buf, ncp->n_threads_per_core,
	    TASKQ_DEFAULTPRI, 0);

	mep->mm_user_cnt = 0;
	return (mep);
}

static void
ncp_reset_nmaus(ncp_t *ncp)
{
	int			i, max = 0;
	mau_entry_t		*mep;
	ncp_mau2cpu_map_t	*m2cp = &ncp->n_maumap;

	for (i = 0; i < NCP_MAX_MAX_NMAUS; i++) {
		mep = &m2cp->m_maulist[i];
		if (mep->mm_state != MAU_STATE_UNINIT) {
			max = i + 1;
		}
	}
	m2cp->m_nextmauidx = 0;
	m2cp->m_nmaus = max;
}



/*
 * This function is called when a new CPU is DR-ed in.
 * Find out which MAU it is associated with, and enable the CPU.
 */
int
ncp_update_mau2cpu_map(ncp_t *ncp)
{
	int		nmaus_total = 0;
	md_t		*mdp;
	int		num_nodes;
	mde_cookie_t	rootnode;
	mde_cookie_t	*mlistp = NULL;
	mde_cookie_t	*clistp = NULL;
	int		mlistsz;
	int		clistsz;
	int		ncpus;
	int		nexecunits;
	char		*ntype;
	int		nlen;
	int		idx, cx;
	int		i;
	uint64_t	cpu_id;
	cpu_t   	*cpu;
	mau_entry_t	*mep;
	ncp_mau2cpu_map_t	*m2cp = &ncp->n_maumap;

	/* grab a snap shot of the MD tree */
	if ((mdp = md_get_handle()) == NULL) {
		return (-1);
	}
	rootnode = md_root_node(mdp);
	if (rootnode == MDE_INVAL_ELEM_COOKIE) {
		cmn_err(CE_WARN, "ncp: corrupted MD, no root node");
		(void) md_fini_handle(mdp);
		return (-1);
	}

	num_nodes = md_node_count(mdp);
	if (num_nodes <= 0) {
		cmn_err(CE_WARN, "ncp: corrupted MD, node count is %d",
		    num_nodes);
		(void) md_fini_handle(mdp);
		return (-1);
	}

	mlistsz = clistsz = num_nodes * sizeof (mde_cookie_t);
	mlistp = (mde_cookie_t *)kmem_zalloc(mlistsz, KM_SLEEP);
	clistp = (mde_cookie_t *)kmem_zalloc(clistsz, KM_SLEEP);

	/*
	 * For v1.0 of the HV API there is no mau information in
	 * the MD.  So, we have to assume the physical relationship
	 * between the cpus and maus.  This is a valid assumption since
	 * prior to LDOMs the relationship was static.
	 */
	if (ncp->n_hvapi_major_version == 1) {
		int		mau_id;

		ncpus = md_scan_dag(mdp, rootnode, md_find_name(mdp, "cpu"),
		    md_find_name(mdp, "fwd"), clistp);
		if (ncpus <= 0) {
			cmn_err(CE_WARN, "ncp: no CPUS found");
			nmaus_total = -1;
			goto done;
		}

		nmaus_total = m2cp->m_nmaus;
		for (cx = 0; cx < ncpus; cx++) {
			int	unlock = 0;
			if (md_get_prop_val(mdp, clistp[cx], "id", &cpu_id)) {
				cmn_err(CE_WARN, "ncp: CPU instance %d has no "
				    "'id' property", cx);
				continue;
			}

			m2cp->m_cpulist[cpu_id].mc_cpuid = (int)cpu_id;
			mau_id = NCP_CPUID2MAUID(ncp, cpu_id);

			/* check whether the MAU was already initialized */
			for (idx = 0; idx < nmaus_total; idx++) {
				if (m2cp->m_maulist[idx].mm_mauid == mau_id)
					break;
			}
			if (idx == nmaus_total) {
				char	buf[32];
				/*
				 * The MAU has not been initialized:
				 * initialize the MAU.
				 */
				mep = &m2cp->m_maulist[nmaus_total];
				mep->mm_mauid = mau_id;
				mep->mm_cpulistsz = ncp->n_max_cpus_per_mau *
				    sizeof (int);
				mep->mm_cpulist =
				    kmem_zalloc(mep->mm_cpulistsz, KM_SLEEP);
				for (i = 0; i < ncp->n_max_cpus_per_mau; i++) {
					mep->mm_cpulist[i] = -1;
				}

				/* alloc the worker entries */
				mep->mm_workers = kmem_alloc(
				    ncp->n_threads_per_core *
				    sizeof (mau_worker_t), KM_SLEEP);

				for (i = 0; i < ncp->n_threads_per_core; i++) {
					mep->mm_workers[i].mw_bind =
					    NCP_MW_UNBOUND;
					mep->mm_workers[i].mw_thread = NULL;
					mep->mm_workers[i].mw_next = NULL;
				}
				mep->mm_state = MAU_STATE_OFFLINE;
				mep->mm_nextcpuidx = 0;
				mutex_init(&mep->mm_lock, NULL, MUTEX_DRIVER,
				    NULL);
				cv_init(&mep->mm_cv, NULL, CV_DRIVER, NULL);
				mep->mm_user_cnt = 0;

				(void) sprintf(buf, "ncp_taskq(%d)", mau_id);

				mep->mm_taskq = ddi_taskq_create(ncp->n_dip,
				    buf, ncp->n_threads_per_core,
				    TASKQ_DEFAULTPRI, 0);

				nmaus_total++;
				ASSERT(nmaus_total <= NCP_MAX_MAX_NMAUS);
			} else {
				mep = &m2cp->m_maulist[idx];
			}

			/*
			 * Add the CPU to the MAU:
			 * If this CPU is already in the list,
			 * skip it
			 */
			for (i = 0; i < mep->mm_ncpus; i++) {
				if (mep->mm_cpulist[i] == cpu_id) {
					/* This CPU is already in the list */
					break;
				}
			}
			if (i < mep->mm_ncpus) {
				/* Skip this CPU */
				continue;
			}

			mep->mm_ncpus++;
			ASSERT(mep->mm_ncpus <= ncp->n_max_cpus_per_mau);
			for (i = 0; i < mep->mm_ncpus; i++) {
				if (mep->mm_cpulist[i] == -1) {
					mep->mm_cpulist[i] = (int)cpu_id;
					break;
				}
			}

			m2cp->m_cpulist[cpu_id].mc_mauid = mau_id;
			if (!MUTEX_HELD(&cpu_lock)) {
				mutex_enter(&cpu_lock);
				unlock = 1;
			}
			cpu = cpu_get(cpu_id);
			if (cpu == NULL) {
				DBG0(ncp, DMD, "ncp_update_mau2cpu_map: "
				    "the CPU is not accessible");
				m2cp->m_cpulist[cpu_id].mc_state =
				    MAU_STATE_OFFLINE;
			} else {
				if (cpu_is_online(cpu)) {
					m2cp->m_cpulist[cpu_id].mc_state =
					    MAU_STATE_ONLINE;
					mep->mm_state = MAU_STATE_ONLINE;
				} else {
					m2cp->m_cpulist[cpu_id].mc_state =
					    MAU_STATE_OFFLINE;
				}
			}
			if (unlock) {
				mutex_exit(&cpu_lock);
			}
		}

		m2cp->m_nmaus = nmaus_total;
		m2cp->m_nmaus_online = nmaus_total;
		goto done;
	}

	/*
	 * For v2.0 of the HV API we can glean the cpu/mau configuration
	 * information from the MD.
	 *
	 * Scan for MAUs.
	 */
	nexecunits = md_scan_dag(mdp, rootnode,
	    md_find_name(mdp, "exec-unit"), md_find_name(mdp, "fwd"), mlistp);
	if (nexecunits <= 0) {
		DBG1(ncp, DWARN, "ncp_update_mau2cpu_map: no EXEC-UNITs found "
		    "(cnt = %d)", nexecunits);
		nmaus_total = -1;
		goto done;
	}

	/*
	 * Go through each of the EXEC-UNITs and find which are
	 * MAUs then determine which CPUs have access to that MAU.
	 * We assign virtual id's to each MAU we find.
	 */
	for (idx = 0; idx < nexecunits; idx++) {
		/*
		 * Scan for MAUs.
		 */
		if (md_get_prop_data(mdp, mlistp[idx], "type",
		    (uint8_t **)&ntype, &nlen) || strcmp(ntype, "mau")) {
			continue;
		}

		ncpus = md_scan_dag(mdp, mlistp[idx], md_find_name(mdp, "cpu"),
		    md_find_name(mdp, "back"), clistp);
		if (ncpus <= 0) {
			cmn_err(CE_WARN, "ncp: no CPUs found (cnt = %d) "
			    "for the exec unit", ncpus);
			/*
			 * This is an error condition since you can't
			 * get to the MAUs without going through a
			 * CPU node in the MD tree.
			 */
			continue;
		}

		mep = get_matching_mep(ncp, mdp, clistp, ncpus);
		if (mep == NULL) {
			DBG0(ncp, DWARN,
			    "ncp_update mau2cpu_map: no valid cpus for mau");
			continue;
		}

		for (cx = 0; cx < ncpus; cx++) {
			int	i;
			int	skipcpu = 0;
			int	online = 0;

			if (md_get_prop_val(mdp, clistp[cx], "id", &cpu_id)) {
				cmn_err(CE_WARN, "ncp: CPU instance %d has no "
				    "'id' property", cx);
				continue;
			}

			if ((online = check_cpu(cpu_id)) < 0) {
				DBG2(ncp, DMD,
				    "cpu %ld for mau %d not valid, skip",
				    cpu_id, mep->mm_mauid);
				continue;
			}

			/* If this CPU is already in the list, skip it */
			for (i = 0; i < ncp->n_max_cpus_per_mau; i++) {
				if (mep->mm_cpulist[i] == cpu_id) {
					/* This CPU is already in the list */
					skipcpu = 1;
					break;
				}
			}
			if (skipcpu && online) {
				/* Skip this CPU */
				if (m2cp->m_cpulist[cpu_id].mc_state ==
				    MAU_STATE_ONLINE) {
					/* if it is already online skip */
					continue;
				}
				m2cp->m_cpulist[cpu_id].mc_state =
				    MAU_STATE_ONLINE;
				mep->mm_ncpus++;

				(void) ncp_online_mau(ncp, mep->mm_mauid);
				continue;
			} else if (skipcpu && !online) {
				ncp_offline_cpu(ncp, cpu_id);
				continue;
			}

			/* fill in the empty slot in cpulist */
			for (i = 0; i < ncp->n_max_cpus_per_mau; i++) {
				if (mep->mm_cpulist[i] == -1) {
					mep->mm_cpulist[i] = (int)cpu_id;
					break;
				}
			}
			m2cp->m_cpulist[cpu_id].mc_mauid = mep->mm_mauid;

			if (online) {
				m2cp->m_cpulist[cpu_id].mc_state =
				    MAU_STATE_ONLINE;
				mep->mm_ncpus++;

				(void) ncp_online_mau(ncp, mep->mm_mauid);
			} else {
				m2cp->m_cpulist[cpu_id].mc_state =
				    MAU_STATE_OFFLINE;
			}
		}
	}
	ncp_reset_nmaus(ncp);
	nmaus_total = m2cp->m_nmaus;

	DBG1(ncp, DMD, "ncp_update_mau2cpu_map: found %d maus",
	    nmaus_total);


done:

	kmem_free(mlistp, mlistsz);
	kmem_free(clistp, clistsz);
	(void) md_fini_handle(mdp);

#ifdef DEBUG
	if (ncp_dflagset(DATTACH))
		dump_cpu2mau_map(m2cp);
#endif /* DEBUG */

	return (nmaus_total);
}


/*ARGSUSED*/
void
ncp_deinit_mau2cpu_map(ncp_t *ncp)
{
	int			m;
	mau_entry_t		*mep;
	ncp_mau2cpu_map_t	*m2cp = &ncp->n_maumap;

	for (m = 0; m < m2cp->m_nmaus; m++) {
		mep = &m2cp->m_maulist[m];
		ncp_free_mep(ncp, mep, 0);
	}
	if (m2cp->m_maulistsz > 0) {
		kmem_free(m2cp->m_maulist, m2cp->m_maulistsz);
		m2cp->m_maulist = NULL;
		m2cp->m_maulistsz = 0;
	}
	if (m2cp->m_cpulistsz > 0) {
		kmem_free(m2cp->m_cpulist, m2cp->m_cpulistsz);
		m2cp->m_cpulist = NULL;
		m2cp->m_cpulistsz = 0;
	}

	m2cp->m_nmaus = 0;
	mutex_destroy(&m2cp->m_lock);
}


/*
 * ncp_map_mau_to_cpu()
 *
 * For the given MAU find an available CPU that
 * has access to that MAU and return its cpu-id.
 * CPUs are selected round-robin.  Caller will
 * use this cpu-id to effectively bind to the
 * core containing the given MAU.
 *
 * Note: need to hold the ncp map lock when
 * entering this routine to guard against
 * configuration changes.
 */
/*ARGSUSED*/
int
ncp_map_mau_to_cpu(ncp_t *ncp, int mau_id, int anycpu)
{
	register int		c;
	mau_entry_t		*mep;
	ncp_mau2cpu_map_t	*m2cp = &ncp->n_maumap;
	int			head;

	if ((mau_id < 0) || (m2cp->m_nmaus <= mau_id)) {
		return (-1);
	}

	/* lookup the MAU */
	mep = &m2cp->m_maulist[mau_id];
	ASSERT(mep->mm_mauid == mau_id);


	mutex_enter(&mep->mm_lock);
	/* this MAU is offline */
	if (mep->mm_state != MAU_STATE_ONLINE) {
		mutex_exit(&mep->mm_lock);
		return (-1);
	}

	/* find a valid CPU */
	head = mep->mm_nextcpuidx;
	do {
		c = mep->mm_cpulist[mep->mm_nextcpuidx];

		if (++mep->mm_nextcpuidx >= ncp->n_max_cpus_per_mau) {
			mep->mm_nextcpuidx = 0;
		}

		if ((c < 0) || (c > NCP_MAX_NCPUS)) {
			DBG4(ncp, DMD, "MAU[%d] mau2cpu: bad cpu[%d] "
			    "(nextcpuid=0x%x) ncpus %d", mep->mm_mauid, c,
			    mep->mm_nextcpuidx, mep->mm_ncpus);
			continue;
		}

		/* does the user want to find an unused cpu? */
		if (!anycpu) {
			/* if already bound - look for another */
			if (m2cp->m_cpulist[c].mc_worker != NULL)
				continue;
		}

		/*
		 * If the cpu is online, return this CPU ID. Otherwise,
		 * look for another CPU.
		 */
		mutex_enter(&m2cp->m_cpulist[c].mc_lock);

		if (m2cp->m_cpulist[c].mc_state == MAU_STATE_ONLINE) {
			DBG2(ncp, DMD, "mau2cpu: mau(%d)->cpu(%d)", mau_id, c);
			/* hold MAU-CPU */
			if (ncp_holdcpu(ncp, m2cp->m_cpulist[c].mc_cpuid)
			    == -1) {
				mutex_exit(&m2cp->m_cpulist[c].mc_lock);
				continue;
			}
			thread_affinity_set(curthread,
			    m2cp->m_cpulist[c].mc_cpuid);
			mutex_exit(&m2cp->m_cpulist[c].mc_lock);
			mutex_exit(&mep->mm_lock);
			return (m2cp->m_cpulist[c].mc_cpuid);
		}

		mutex_exit(&m2cp->m_cpulist[c].mc_lock);

		DBG2(ncp, DMD, "mau2cpu: mau(%d)cpu(%d) was offline",
		    mau_id, c);
	} while (head != mep->mm_nextcpuidx);

	mutex_exit(&mep->mm_lock);

	return (-1);
}

void
ncp_unmap_mau_to_cpu(ncp_t *ncp, int cid, int unbind)
{
	if (unbind) {
		thread_affinity_clear(curthread);
	}
	ncp_relecpu(ncp, cid);
}

/*
 * ncp_map_cpu_to_mau()
 *
 * For the given CPU determine which MAU is accessible by it.
 */
/*ARGSUSED*/
int
ncp_map_cpu_to_mau(ncp_t *ncp, int cpu_id)
{
	int			mid;
	ncp_mau2cpu_map_t	*m2cp = &ncp->n_maumap;

	if (m2cp->m_nmaus <= 0)
		return (-1);

	ASSERT(m2cp->m_cpulist[cpu_id].mc_cpuid == cpu_id);

	mid = m2cp->m_cpulist[cpu_id].mc_mauid;
	if (mid < 0) {
		/* the CPU is not associated with an MAU */
		return (-1);
	}

	if ((m2cp->m_maulist[mid].mm_state == MAU_STATE_ONLINE) ||
	    (m2cp->m_maulist[mid].mm_state == MAU_STATE_PENDING)) {
		DBG2(ncp, DMD, "cpu2mau: cpu(%d) -> mau(%d)", cpu_id, mid);
		return (mid);
	}
	return (-1);
}

/*
 * ncp_map_nextmau()
 *
 * Select via round-robin a MAU.  This routine is primarily
 * used when the caller did not locate a MAU for some
 * given CPU.  Once a valid MAU is found then the caller
 * can find an available CPU which has access to that
 * MAU and bind to it.
 *
 * note: m_lock must be held
 */
int
ncp_map_nextmau(ncp_t *ncp)
{
	int	i, m;
	ncp_mau2cpu_map_t	*m2cp = &ncp->n_maumap;

	if (m2cp->m_nmaus <= 0) {
		return (-1);
	}

	m = m2cp->m_nextmauidx;

	for (i = 0; i < m2cp->m_nmaus; i++) {
		if (m2cp->m_maulist[m].mm_state != MAU_STATE_ONLINE) {
			m++;
			m = (m >= m2cp->m_nmaus) ? 0 : m;
			continue;
		}

		/* update next MAU index */
		m2cp->m_nextmauidx = m + 1;

		if (m2cp->m_nextmauidx >= m2cp->m_nmaus) {
			m2cp->m_nextmauidx = 0;
		}


		DBG1(ncp, DMD, "nextmau: mau(%d)",
		    m2cp->m_maulist[m].mm_mauid);

		return (m2cp->m_maulist[m].mm_mauid);
	}

	return (-1);
}

/*
 * ncp_map_findmau()
 *
 * Locate the MAU entry structure for the given MAU.
 */
mau_entry_t *
ncp_map_findmau(ncp_t *ncp, int mau_id)
{
	ncp_mau2cpu_map_t	*m2cp = &ncp->n_maumap;

	ASSERT(m2cp->m_maulist[mau_id].mm_mauid == mau_id);
	return (&m2cp->m_maulist[mau_id]);
}


/*
 * ncp_map_holdmau()
 * Locate the MAU entry structure for the given MAU, and hold the reference
 * on it
 */
mau_entry_t *
ncp_map_holdmau(ncp_t *ncp, int mau_id)
{
	ncp_mau2cpu_map_t	*m2cp = &ncp->n_maumap;
	mau_entry_t		*mep;

	ASSERT(m2cp->m_maulist[mau_id].mm_mauid == mau_id);

	mep = &m2cp->m_maulist[mau_id];
	mutex_enter(&mep->mm_lock);

	if ((mep->mm_state != MAU_STATE_ONLINE) &&
	    (mep->mm_state != MAU_STATE_PENDING)) {
		DBG1(ncp, DMD, "ncp_map_holdmau: MAU[%d] was not online",
		    mau_id);
		mutex_exit(&mep->mm_lock);
		return (NULL);
	}

	mep->mm_user_cnt++;
	mutex_exit(&mep->mm_lock);

	return (mep);
}

/*
 * ncp_map_relemau()
 * Release the reference on the MAU
 */
void
ncp_map_relemau(mau_entry_t *mep)
{
	mutex_enter(&mep->mm_lock);
	mep->mm_user_cnt--;
	ASSERT(mep->mm_user_cnt >= 0);
	if ((mep->mm_user_cnt == 0) && (mep->mm_state == MAU_STATE_DRAINING)) {
		cv_signal(&mep->mm_cv);
	}
	mutex_exit(&mep->mm_lock);
}


/*
 * Note: caller must hold m_lock.
 */
int
ncp_online_mau(ncp_t *ncp, int mau_id)
{
	mau_entry_t	*mep = ncp_map_findmau(ncp, mau_id);

	if (mep) {

		/* Only online mau that is currently offline */
		if (mep->mm_state != MAU_STATE_ONLINE) {
			DBG1(ncp, DDR, "ncp_online_mau: online maus %d",
			    ncp->n_maumap.m_nmaus_online);
			/* mark the MAU as online */
			mep->mm_state = MAU_STATE_ONLINE;
			ncp->n_maumap.m_nmaus_online++;

			/* Re-enable the interrupt */
			ncp_intr_enable_mau(ncp, mau_id);
		}

		/*
		 * If this is the first MAU, notify the framework
		 * that the provider is ready for requests
		 */
		if (!(ncp_isregistered(ncp))) {
			(void) ncp_provider_register(ncp);
			ncp_provider_notify_ready(ncp);
			DBG0(ncp, DMD, "The first MAU was put "
			    "online. Notify the framework");
		}
		return (DR_CRYPTO_RES_OK);
	}
	return (DR_CRYPTO_RES_BAD_CRYPTO);
}

/*
 * Caller must hold m_lock
 */
void
ncp_offline_mau(ncp_t *ncp, int mid)
{
	mau_entry_t		*mep = ncp_map_findmau(ncp, mid);
	ncp_mau2cpu_map_t	*m2cp = &ncp->n_maumap;

	mutex_enter(&mep->mm_lock);

	/*
	 * If the MAU is in the PENDING state, it has not be initialized.
	 * Just mark it OFFLINE.
	 */
	if (mep->mm_state == MAU_STATE_PENDING) {
		mep->mm_state = MAU_STATE_OFFLINE;
		mutex_exit(&mep->mm_lock);
		return;
	}

	/* Only offline maus that are currently online */
	if (mep->mm_state == MAU_STATE_ONLINE) {

		mep->mm_state = MAU_STATE_OFFLINE;

		DBG1(ncp, DMD, "MAU[%d] was taken offline\n", mid);
		if (ncp->n_maumap.m_nmaus_online > 0) {
			ncp->n_maumap.m_nmaus_online--;

			/* If this is the last MAU, take it offline */
			if (ncp->n_maumap.m_nmaus_online == 0) {
				mutex_exit(&m2cp->m_lock);
				mutex_exit(&mep->mm_lock);
				(void) ncp_provider_unregister(ncp);
				mutex_enter(&m2cp->m_lock);
				mutex_enter(&mep->mm_lock);
				DBG0(ncp, DMD, "The last MAU was taken "
				    "offline. Notify the framework");
			}
		}

		DBG1(ncp, DMD, "ncp_offline_mau: m_nmaus_online = %d",
		    ncp->n_maumap.m_nmaus_online);

		/*
		 * Disable the interrupt: needed to disable the interrupt
		 * while the CPU is still avaiable.
		 */
		ncp_intr_disable_mau(ncp, mid);
	}

	mutex_exit(&mep->mm_lock);
}

int
ncp_holdcpu(ncp_t *ncp, int cid)
{
	ASSERT(MUTEX_HELD(&ncp->n_maumap.m_cpulist[cid].mc_lock));
	if (ncp->n_maumap.m_cpulist[cid].mc_state != MAU_STATE_ONLINE) {
		return (-1);
	}
	ncp->n_maumap.m_cpulist[cid].mc_refcnt++;
	return (0);
}

void
ncp_relecpu(ncp_t *ncp, int cid)
{
	cpu_entry_t	*cpuentry;

	cpuentry = &ncp->n_maumap.m_cpulist[cid];

	mutex_enter(&cpuentry->mc_lock);
	cpuentry->mc_refcnt--;

	ASSERT(cpuentry->mc_refcnt >= 0);


	if ((cpuentry->mc_refcnt <= 0) &&
	    (cpuentry->mc_state & MAU_STATE_DRAINING)) {
		cv_signal(&cpuentry->mc_cv);
	}

	mutex_exit(&cpuentry->mc_lock);
}

void
ncp_online_cpu(ncp_t *ncp, int cpu_id)
{
	ncp_mau2cpu_map_t	*m2cp = &ncp->n_maumap;
	mau_entry_t		*mep;

	if (m2cp->m_cpulist[cpu_id].mc_state == MAU_STATE_ONLINE) {
		/* the CPU is already online */
		return;
	}

	/*
	 * The cpu was added for the first time.
	 * Scan through the MD to map the CPU to an MAU, and add it
	 * to the MAU's CPU list.
	 */
	if (ncp_update_mau2cpu_map(ncp) < 0) {
		DBG0(ncp, DMD, "failed to reinitialize md");
		return;
	}

	/* no associated mau - done */
	if (m2cp->m_cpulist[cpu_id].mc_mauid == -1) {
		DBG0(ncp, DMD, "This CPU is not associated with a MAU");
		return;
	}

	mep = &m2cp->m_maulist[m2cp->m_cpulist[cpu_id].mc_mauid];

	/* mark cpu online */
	if (m2cp->m_cpulist[cpu_id].mc_state != MAU_STATE_ONLINE) {
		m2cp->m_cpulist[cpu_id].mc_state = MAU_STATE_ONLINE;
		mep->mm_ncpus++;
	}

	/* check if MAU needs to be brought online */
	if (mep->mm_state != MAU_STATE_ONLINE) {
		ncp_ksdeinit(ncp);

		(void) ncp_online_mau(ncp, m2cp->m_cpulist[cpu_id].mc_mauid);

		ncp_ksinit(ncp);
	}

	DBG1(ncp, DMD, "CPU[%d] was put online\n", cpu_id);
}


void
ncp_offline_cpu(ncp_t *ncp, int cpu_id)
{
	ncp_mau2cpu_map_t	*m2cp = &ncp->n_maumap;
	ncp_mau_queue_t		*nmq;
	mau_entry_t		*mep;
	int			i;
	int			mid;

	mutex_enter(&m2cp->m_cpulist[cpu_id].mc_lock);

	/*
	 * we can get both CPU_OFF & CPU_UNCONFIG so cpu may be offline
	 * already.
	 */
	if (m2cp->m_cpulist[cpu_id].mc_state == MAU_STATE_OFFLINE) {
		mutex_exit(&m2cp->m_cpulist[cpu_id].mc_lock);
		return;
	}

	if (m2cp->m_cpulist[cpu_id].mc_refcnt > 0) {
		m2cp->m_cpulist[cpu_id].mc_state = MAU_STATE_DRAINING;
		cv_wait(&m2cp->m_cpulist[cpu_id].mc_cv,
		    &m2cp->m_cpulist[cpu_id].mc_lock);
	}
	m2cp->m_cpulist[cpu_id].mc_state = MAU_STATE_OFFLINE;

	mid = m2cp->m_cpulist[cpu_id].mc_mauid;

	mutex_exit(&m2cp->m_cpulist[cpu_id].mc_lock);

	DBG1(ncp, DMD, "CPU[%d] was taken offline\n", cpu_id);

	if (mid < 0) {
		/*
		 * The cpu is not associated with an mau. Nothing to do.
		 */
		return;
	}

	ncp_unbind_worker(ncp, cpu_id);

	mep = &m2cp->m_maulist[mid];

	for (i = 0; i < ncp->n_max_cpus_per_mau; i++) {
		if (mep->mm_cpulist[i] == cpu_id) {
			mep->mm_cpulist[i] = -1;
		}
	}
	mep->mm_ncpus--;

	if (mep->mm_nextcpuidx >= mep->mm_ncpus) {
		mep->mm_nextcpuidx = 0;
	}

	if (mep->mm_ncpus <= 0) {
		/* All CPUs on the MAU are offline: take MAU offline */
		ncp_offline_mau(ncp, mid);

		/* Unconfigure the mau */
		nmq = &ncp->n_maumap.m_maulist[mid].mm_queue;
		mutex_enter(&mep->mm_lock);
		if (nmq->nmq_init != 0) {
			ncp_mau_q_unconfigure(ncp, nmq);
		}
		mutex_exit(&mep->mm_lock);
	}
}

void
ncp_unbind_worker(ncp_t *ncp, processorid_t cid)
{
	mau_worker_t	*worker, *prev_worker = NULL;

	mutex_enter(&ncp->n_maumap.m_cpulist[cid].mc_lock);

	worker = ncp->n_maumap.m_cpulist[cid].mc_worker;

	while (worker) {
		if (worker->mw_bind != NCP_MW_UNBOUND) {
			worker->mw_bind = NCP_MW_UNBOUND;

			thread_affinity_clear(worker->mw_thread);

			worker->mw_thread = NULL;
		}

		prev_worker = worker;
		worker = worker->mw_next;
		prev_worker->mw_next = NULL;
	}
	ncp->n_maumap.m_cpulist[cid].mc_worker = NULL;
	mutex_exit(&ncp->n_maumap.m_cpulist[cid].mc_lock);
}


processorid_t
ncp_bind_worker(ncp_t *ncp, mau_entry_t *mep, kthread_id_t thread)
{
	processorid_t		cid;
	cpu_entry_t		*cpuentry;
	mau_worker_t		*worker;
	ncp_mau2cpu_map_t	*m2cp = &ncp->n_maumap;

	cpuentry = &ncp->n_maumap.m_cpulist[CPU->cpu_id];

	mutex_enter(&cpuentry->mc_lock);

	/* see if this thread already bound */
	worker = cpuentry->mc_worker;
	while (worker != NULL) {
		if (worker->mw_thread == thread) {
			if (cpuentry->mc_state == MAU_STATE_ONLINE) {
				/* bump refcnt - hold cpu */
				cpuentry->mc_refcnt++;
				mutex_exit(&cpuentry->mc_lock);
				return (worker->mw_bind);
			} else {
				/* force thread to be unbound */
				if (thread->t_affinitycnt) {
					thread_affinity_clear(thread);
				}
				worker->mw_thread = NULL;
				worker->mw_bind = NCP_MW_UNBOUND;
				break;
			}
		}
		worker = worker->mw_next;
	}
	mutex_exit(&cpuentry->mc_lock);


	/*
	 * worker has not been bound yet, initially try and find one
	 * that is not being used by any worker thread.
	 */
	mutex_enter(&m2cp->m_lock);
	if ((cid = ncp_map_mau_to_cpu(ncp, mep->mm_mauid, 0)) < 0) {
		/* at this point just take any online cpu */
		cid = ncp_map_mau_to_cpu(ncp, mep->mm_mauid, 1);
	}

	if (cid >= 0) {
		int	i;

		/* save worker thread info */
		mutex_enter(&mep->mm_lock);
		for (i = 0; i < ncp->n_threads_per_core; i++) {
			ASSERT(mep->mm_workers[i].mw_thread != thread);
			if (mep->mm_workers[i].mw_thread == NULL) {

				cpuentry = &ncp->n_maumap.m_cpulist[cid];
				mutex_enter(&cpuentry->mc_lock);

				mep->mm_workers[i].mw_thread = thread;
				mep->mm_workers[i].mw_bind = cid;
				/*
				 * if one already bound to this cpu -
				 * add to the list.
				 */
				if (cpuentry->mc_worker) {
					mep->mm_workers[i].mw_next =
					    cpuentry->mc_worker;
					DBG3(ncp, DMD,
					    "MAU%d: adding thread 0x%p "
					    "to cpu %d", mep->mm_mauid,
					    thread, cid);
				}
				cpuentry->mc_worker = &mep->mm_workers[i];
				mutex_exit(&cpuentry->mc_lock);
				break;
			}
		}
		mutex_exit(&mep->mm_lock);
	}
	mutex_exit(&m2cp->m_lock);
	return (cid);
}

void
ncp_mau_status(ncp_t *ncp, dr_crypto_stat_t *statp)
{
	mau_entry_t		*mep;
	ncp_mau2cpu_map_t	*m2cp = &ncp->n_maumap;
	int			mau_id;

	mutex_enter(&m2cp->m_lock);
	mau_id = ncp_map_cpu_to_mau(ncp, statp->cpuid);

	DBG2(ncp, DDR, "ncp_mau_status for mau %d, cpu %d",
	    mau_id, statp->cpuid);

	if ((mau_id < 0) || (m2cp->m_nmaus <= mau_id)) {
		ncp_diperror(ncp->n_dip,
		    "ncp_mau_status: unable to find MAU for cpu %d",
		    statp->cpuid);
		statp->result = DR_CRYPTO_RES_BAD_CRYPTO;
		statp->status = DR_CRYPTO_STAT_NOT_PRESENT;
		mutex_exit(&m2cp->m_lock);
		return;
	}
	/* get the MAU */
	mep = &m2cp->m_maulist[mau_id];
	ASSERT(mep->mm_mauid == mau_id);

	switch (mep->mm_state) {
		case MAU_STATE_REMOVED:
		case MAU_STATE_UNINIT:
			statp->status = DR_CRYPTO_STAT_UNCONFIGURED;
			break;
		case MAU_STATE_ONLINE:
		case MAU_STATE_OFFLINE:
		case MAU_STATE_DRAINING:
		case MAU_STATE_ERROR:
		case MAU_STATE_PENDING:
			statp->status = DR_CRYPTO_STAT_CONFIGURED;
			break;
	}
	mutex_exit(&m2cp->m_lock);

	statp->result = DR_CRYPTO_RES_OK;
}

void
ncp_mau_config(ncp_t *ncp, dr_crypto_res_t *resp)
{
	mau_entry_t		*mep;
	ncp_mau2cpu_map_t	*m2cp = &ncp->n_maumap;
	int			mau_id;
	int			prev_nmaus, curr_nmaus;
	int			rv;

	mutex_enter(&cpu_lock);
	mutex_enter(&ncp->n_dr_lock);
	mutex_enter(&m2cp->m_lock);

	/* remove kstats while mau changes being made */
	ncp_ksdeinit(ncp);

	prev_nmaus = m2cp->m_nmaus;

	/* re-read the MD and update the mau map */
	if ((curr_nmaus = ncp_update_mau2cpu_map(ncp)) == -1) {
		ncp_diperror(ncp->n_dip,
		    "ncp_mau_config: update map failed");
		resp->result = DR_CRYPTO_RES_FAILURE;
		resp->status = DR_CRYPTO_STAT_NOT_PRESENT;
		ncp_ksinit(ncp);
		mutex_exit(&m2cp->m_lock);
		mutex_exit(&ncp->n_dr_lock);
		mutex_exit(&cpu_lock);
		return;
	}
	mutex_exit(&cpu_lock);

	/* find the targeted MAU using the provided cpu ID */
	mau_id = m2cp->m_cpulist[resp->cpuid].mc_mauid;

	DBG2(ncp, DDR, "ncp_mau_config for mau %d, cpu %d",
	    mau_id, resp->cpuid);

	if ((mau_id < 0) || (m2cp->m_nmaus <= mau_id)) {
		ncp_ksinit(ncp);
		mutex_exit(&m2cp->m_lock);
		mutex_exit(&ncp->n_dr_lock);
		ncp_diperror(ncp->n_dip,
		    "ncp_mau_config: unable to find MAU for cpu %d",
		    resp->cpuid);
		resp->result = DR_CRYPTO_RES_BAD_CRYPTO;
		resp->status = DR_CRYPTO_STAT_NOT_PRESENT;
		return;
	}

	mep = &m2cp->m_maulist[mau_id];

	ASSERT(mep->mm_mauid == mau_id);

	/*
	 * If previously there were no MAUs configured then register the HV
	 * NCS API.
	 */
	if (prev_nmaus == 0 && curr_nmaus > 0) {
		if ((rv = ncp_register_ncs_api()) != 0) {
			ncp_ksinit(ncp);
			mutex_exit(&m2cp->m_lock);
			mutex_exit(&ncp->n_dr_lock);
			ncp_diperror(ncp->n_dip,
			    "ncp_mau_config: Failed to register "
			    "HV NCS API (%d)", rv);
			resp->result = DR_CRYPTO_RES_FAILURE;
			resp->status = DR_CRYPTO_STAT_UNCONFIGURED;
			return;
		}
	}

	/*
	 * Configure the MAU: MAU is added to the domain but it may be
	 * disabled
	 */
	if (ncp_update_mau2cpu_map(ncp) < 0) {
		resp->result = DR_CRYPTO_RES_BAD_CRYPTO;
		resp->status = DR_CRYPTO_STAT_UNCONFIGURED;
	} else {
		/* clear the kstats for the just added MAU */
		ncp_kstat_clear(ncp, mau_id);

		resp->result = DR_CRYPTO_RES_OK;
		resp->status = DR_CRYPTO_STAT_CONFIGURED;
	}

	/* re-add kstats */
	ncp_ksinit(ncp);


	mutex_exit(&m2cp->m_lock);
	mutex_exit(&ncp->n_dr_lock);
}

void
ncp_mau_unconfig(ncp_t *ncp, dr_crypto_res_t *resp)
{
	mau_entry_t		*mep;
	ncp_mau2cpu_map_t	*m2cp = &ncp->n_maumap;
	int			mau_id;
	int			i;
	int			remap = 0;
	int			nmaus;

	mutex_enter(&cpu_lock);

	/* block other DR requests */
	mutex_enter(&ncp->n_dr_lock);

	mutex_enter(&m2cp->m_lock);

	ncp_ksdeinit(ncp);

retry:

	mau_id = m2cp->m_cpulist[resp->cpuid].mc_mauid;

	DBG2(ncp, DDR, "ncp_mau_unconfig for mau %d, cpu %d",
	    mau_id, resp->cpuid);

	if ((mau_id < 0) || (m2cp->m_nmaus <= mau_id)) {
		/* try and remap as last ditch effort */
		if (!remap) {
			DBG1(ncp, DDR,
			    "mau unconfig failed for cpu %d - try remap",
			    resp->cpuid);
			if (ncp_update_mau2cpu_map(ncp) != -1) {
				remap = 1;
				goto retry;
			}
		}
		ncp_diperror(ncp->n_dip,
		    "ncp_mau_unconfig: unable to find MAU for cpu %d",
		    resp->cpuid);

		resp->result = DR_CRYPTO_RES_BAD_CRYPTO;
		resp->status = DR_CRYPTO_STAT_NOT_PRESENT;

		ncp_ksinit(ncp);
		mutex_exit(&m2cp->m_lock);
		mutex_exit(&ncp->n_dr_lock);
		mutex_exit(&cpu_lock);
		return;
	}

	mutex_exit(&cpu_lock);

	/* get the MAU */
	mep = &m2cp->m_maulist[mau_id];
	ASSERT(mep->mm_mauid == mau_id);

	/*
	 * fake offline all of the associated cpus
	 * that will also force offline/unconfig the MAU
	 */
	for (i = 0; i < ncp->n_max_cpus_per_mau; i++) {
		int	cpu_id = mep->mm_cpulist[i];
		if (cpu_id != -1) {
			ncp_offline_cpu(ncp, cpu_id);
			/* decouple cpu from mau */
			m2cp->m_cpulist[cpu_id].mc_mauid = -1;
		}
	}

	ncp_free_mep(ncp, mep, 1);
	ncp_reset_nmaus(ncp);

	/*
	 * If there are no MAUs configured then unregister the HV NCS API.
	 */
	nmaus = ncp->n_maumap.m_nmaus;
	if (nmaus == 0) {
		ncp_unregister_ncs_api();
	}

	ncp_ksinit(ncp);

	mutex_exit(&m2cp->m_lock);
	mutex_exit(&ncp->n_dr_lock);

	resp->status = DR_CRYPTO_STAT_UNCONFIGURED;
	resp->result = DR_CRYPTO_RES_OK;

}

#ifdef DEBUG
static void
dump_cpu2mau_map(ncp_mau2cpu_map_t *m2cp)
{
	int		m, c;
	mau_entry_t	*mep;

	printf("DUMP mau2cpu map (nmaus = %d)\n", m2cp->m_nmaus);

	for (m = 0; m < m2cp->m_nmaus; m++) {
		mep = &m2cp->m_maulist[m];
		printf("DUMP:%d:mau %d: ncpus = %d (nextcpuidx = %d) "
		    "state = %d\n", m, mep->mm_mauid, mep->mm_ncpus,
		    mep->mm_nextcpuidx, mep->mm_state);
		for (c = 0; c < mep->mm_ncpus; c++)
			printf("DUMP:%d:mau %d (hdl = 0x%lx)-> cpu %d\n",
			    m, mep->mm_mauid, mep->mm_queue.nmq_handle,
			    mep->mm_cpulist[c]);
	}

	for (c = 0; c < NCP_MAX_NCPUS; c++) {
		printf("DUMP:%d:cpu %d -> mau %d\n", c,
		    m2cp->m_cpulist[c].mc_cpuid, m2cp->m_cpulist[c].mc_mauid);
	}
}
#endif /* DEBUG */
