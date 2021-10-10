/*
 * Copyright (c) 2005, 2011, Oracle and/or its affiliates. All rights reserved.
 */


/*
 * Niagara Crypto Provider driver
 */

#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/modctl.h>
#include <sys/conf.h>
#include <sys/devops.h>
#include <sys/cmn_err.h>
#include <sys/kmem.h>
#include <sys/machsystm.h>
#include <sys/stat.h>
#include <sys/open.h>
#include <sys/file.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/cpuvar.h>
#include <sys/disp.h>
#include <sys/hsvc.h>
#include <sys/ncp.h>
#include <sys/hypervisor_api.h>
#include <fips_checksum.h>

static int	ncp_attach(dev_info_t *, ddi_attach_cmd_t);
static int	ncp_detach(dev_info_t *, ddi_detach_cmd_t);
static int	ncp_suspend(ncp_t *);
static int	ncp_resume(ncp_t *);

static int	ncp_add_intrs(ncp_t *);
static void	ncp_remove_intrs(ncp_t *);
static void	ncp_intr_enable(ncp_t *);
static void	ncp_deallocate(ncp_t *, int);
static int	ncp_ma_ldst(ncp_t *, ncp_desc_t **, ncp_desc_t **,
			ncp_ma_t *, size_t, int);

static int	ncp_desc_submit(ncp_t *, ncp_request_t *reqp, ncp_desc_t *);
static ncp_desc_t *ncp_desc_alloc(kmem_cache_t *, ncp_ma_t *, int);
static void	ncp_desc_free(kmem_cache_t *, ncp_desc_t *);
static ncp_descjob_t
		*ncp_desc_addjob(ncp_mau_queue_t *, ncp_desc_t *, int);
static void	ncp_desc_deljob(ncp_mau_queue_t *, ncp_descjob_t *);

static ncp_ma_t	*ncp_ma_alloc(ncp_t *);
static void	ncp_ma_free(ncp_t *, ncp_ma_t *);

static int	ncp_mau_q_init(ncp_t *);
static void	ncp_mau_set_intrmid(ncp_t *);
static void	ncp_mau_q_deinit(ncp_t *);

static int	ncp_herr2kerr(uint64_t hv_errcode);

static void	ncp_ma_reverse(void *, size_t);

static uint_t	ncp_mau_ihandler(void *, void *);

extern int	vnex_ino_to_inum(dev_info_t *, uint32_t);

static void	ncp_init_job_timeout_info(ncp_t *ncp);
static void	ncp_fini_job_timeout_info(ncp_t *ncp);
static void	ncp_jobtimeout_start(ncp_t *ncp);
static void	ncp_jobtimeout_stop(ncp_t *ncp);
static void	ncp_jobtimeout(void *arg);
static int	ncp_jobtimeout_mau(ncp_t *ncp, int mid);
static void	ncp_drain_jobs(ncp_t *ncp, int mid);
static void	ncp_check_errors(ncp_t *ncp, int mid, ncp_descjob_t *dj);
static void	ncp_check_maus(ncp_t *ncp);

static int	ncp_init_ncs_api(ncp_t *);
int		ncp_register_ncs_api(void);
void		ncp_unregister_ncs_api(void);

/*
 * Device operations.
 */
static struct dev_ops devops = {
	DEVO_REV,		/* devo_rev */
	0,			/* devo_refcnt */
	nodev,			/* devo_getinfo */
	nulldev,		/* devo_identify */
	nulldev,		/* devo_probe */
	ncp_attach,		/* devo_attach */
	ncp_detach,		/* devo_detach */
	nodev,			/* devo_reset */
	NULL,			/* devo_cb_ops */
	NULL,			/* devo_bus_ops */
	nulldev,		/* devo_power */
	ddi_quiesce_not_supported,	/* devo_quiesce */
};

/*
 * Module linkage.
 */
static struct modldrv modldrv = {
	&mod_driverops,			/* drv_modops */
	"NCP Crypto Driver",		/* drv_linkinfo */
	&devops,			/* drv_dev_ops */
};

static struct modlinkage modlinkage = {
	MODREV_1,			/* ml_rev */
	&modldrv,			/* ml_linkage */
	NULL
};

/*
 * Driver globals Soft state.
 */
static void *ncp_softstate = NULL;

/*
 * Hypervisor NCS services information.
 */
static boolean_t ncs_hsvc_available = B_FALSE;

#define	NVERSIONS	2

/*
 * Supported HV API versions by this driver.
 */
static hsvc_info_t ncs_hsvc[NVERSIONS] = {
	{ HSVC_REV_1, NULL, HSVC_GROUP_NCS, 2, 1, "ncp" },	/* v2.1 */
	{ HSVC_REV_1, NULL, HSVC_GROUP_NCS, 1, 0, "ncp" },	/* v1.0 */
};
static int	ncp_ncs_version_idx;	/* index into ncs_hsvc[] */

/*
 * Timeout value for waiting for a full queue to empty out.
 */
int	ncp_qtimeout_seconds = NCP_QTIMEOUT_SECONDS;

/*
 * DDI entry points.
 */
int
_init(void)
{
	int	rv;

	rv = ddi_soft_state_init(&ncp_softstate, sizeof (ncp_t), 1);
	if (rv != 0) {
		/* this should *never* happen! */
		return (rv);
	}

	if ((rv = mod_install(&modlinkage)) != 0) {
		/* cleanup here */
		ddi_soft_state_fini(&ncp_softstate);
		return (rv);
	}

	return (0);
}

int
_fini(void)
{
	int	rv;

	rv = mod_remove(&modlinkage);
	if (rv == 0) {
		/* cleanup here */
		ddi_soft_state_fini(&ncp_softstate);
	}

	return (rv);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

static int
cpu_event_handler(cpu_setup_t what, int cid, void *arg)
{
	ncp_t  *ncp = (ncp_t *)arg;

	/* block other DR requests */
	mutex_enter(&ncp->n_dr_lock);

	switch (what) {
	case CPU_CONFIG:
		break;
	case CPU_INIT:
	case CPU_ON:
	case CPU_CPUPART_IN:
		DBG2(ncp, DMD, "cpu_event_handler CID[%d]: event[%d] "
		    "Online CPU", cid, what);
		mutex_enter(&ncp->n_maumap.m_lock);
		ncp_online_cpu(ncp, cid);
		mutex_exit(&ncp->n_maumap.m_lock);
		break;
	case CPU_UNCONFIG:
	case CPU_OFF:
	case CPU_CPUPART_OUT:
		DBG2(ncp, DMD, "cpu_event_handler CID[%d]: event[%d] "
		    "Offline CPU", cid, what);
		mutex_enter(&ncp->n_maumap.m_lock);
		ncp_offline_cpu(ncp, cid);
		if (what == CPU_UNCONFIG) {
			/* cpu removed - disassociate the mau */
			ncp->n_maumap.m_cpulist[cid].mc_mauid = -1;
		}
		mutex_exit(&ncp->n_maumap.m_lock);
		break;
	default:
		break;
	}

	mutex_exit(&ncp->n_dr_lock);
	return (0);
}

/* ARGSUSED */
int
ncp_request_constructor(void *buf, void *un, int kmflags)
{
	ncp_request_t	*reqp = (ncp_request_t *)buf;
	ncp_t		*ncp = (ncp_t *)un;

	bzero(reqp, sizeof (*reqp));
	reqp->nr_ncp = ncp;
	mutex_init(&reqp->nr_keygen_lock, NULL, MUTEX_DRIVER, NULL);
	cv_init(&reqp->nr_keygen_cv, NULL, CV_DRIVER, NULL);

	return (0);
}

/* ARGSUSED */
void
ncp_request_destructor(void *buf, void *un)
{
	ncp_request_t	*reqp = (ncp_request_t *)buf;

	mutex_destroy(&reqp->nr_keygen_lock);
	cv_destroy(&reqp->nr_keygen_cv);
}

/* ARGSUSED */
int
ncp_mactl_constructor(void *buf, void *un, int kmflags)
{
	ncp_ma_t	*mp = (ncp_ma_t *)buf;

	mutex_init(&mp->nma_lock, NULL, MUTEX_DRIVER, NULL);

	return (0);
}

/* ARGSUSED */
void
ncp_mactl_destructor(void *buf, void *un)
{
	ncp_ma_t	*mp = (ncp_ma_t *)buf;

	mutex_destroy(&mp->nma_lock);
}

int
ncp_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	ncp_t		*ncp = NULL;
	int		instance;
	int		rv;
	int		nmaus;

	/*CONSTCOND*/
	if (NCS_HVDESC_SIZE_ACTUAL != NCS_HVDESC_SIZE_EXPECTED) {
		cmn_err(CE_WARN,
		    "ncp: NCS_HVDESC_SIZE actual(%d) != expected(%d)",
		    (int)NCS_HVDESC_SIZE_ACTUAL,
		    (int)NCS_HVDESC_SIZE_EXPECTED);
		return (DDI_FAILURE);
	}

	instance = ddi_get_instance(dip);
	/*
	 * Only instance 0 of ncp driver is allowed.
	 */
	if (instance != 0) {
		ncp_diperror(dip, "only one instance (0) allowed");
		return (DDI_FAILURE);
	}

	switch (cmd) {
	case DDI_RESUME:
		ncp = (ncp_t *)ddi_get_soft_state(ncp_softstate, instance);
		if (ncp == NULL) {
			ncp_diperror(dip, "no soft state in attach");
			return (DDI_FAILURE);
		}
		return (ncp_resume(ncp));

	case DDI_ATTACH:
		break;
	default:
		return (DDI_FAILURE);
	}

	/*
	 * Allocate and initialize ncp_t struct for this instance.
	 */
	rv = ddi_soft_state_zalloc(ncp_softstate, instance);
	if (rv != DDI_SUCCESS) {
		ncp_diperror(dip, "unable to allocate soft state");
		goto exit;
	}
	ncp = (ncp_t *)ddi_get_soft_state(ncp_softstate, instance);
	ASSERT(ncp != NULL);
	ncp->n_dip = dip;
	DBG1(ncp, DATTACH, "ncp_attach: ncp = %p", ncp);

	ncp->n_binding_name = ddi_binding_name(dip);
	if (strncmp(ncp->n_binding_name, NCP_BINDNAME_N1,
	    strlen(NCP_BINDNAME_N1)) == 0) {
		/*
		 * Niagara 1
		 */
		ncp->n_binding = NCP_CPU_N1;
		ncp->n_max_cpus_per_mau = NCP_N1_MAX_CPUS_PER_MAU;
	} else if (strncmp(ncp->n_binding_name, NCP_BINDNAME_N2,
	    strlen(NCP_BINDNAME_N2)) == 0) {
		/*
		 * Niagara 2
		 */
		ncp->n_binding = NCP_CPU_N2;
		ncp->n_max_cpus_per_mau = NCP_N2_MAX_CPUS_PER_MAU;
	} else if (strncmp(ncp->n_binding_name, NCP_BINDNAME_VF,
	    strlen(NCP_BINDNAME_VF)) == 0) {
		/*
		 * Victoria Falls - currently treated the same as N2
		 */
		ncp->n_binding = NCP_CPU_VF;
		ncp->n_max_cpus_per_mau = NCP_N2_MAX_CPUS_PER_MAU;
	} else if (strncmp(ncp->n_binding_name, NCP_BINDNAME_KT,
	    strlen(NCP_BINDNAME_KT)) == 0) {
		/*
		 * Rainbow Falls - currently treated the same as N2
		 */
		ncp->n_binding = NCP_CPU_KT;
		ncp->n_max_cpus_per_mau = NCP_N2_MAX_CPUS_PER_MAU;
	} else {
		ncp_diperror(dip, "unable to determine ncp (cpu) binding (%s)",
		    ncp->n_binding_name);
		goto exit;
	}
	DBG1(ncp, DATTACH,
	    "ncp_attach: ncp->n_binding_name = %s",
	    ncp->n_binding_name);

	if (ncp_init_ncs_api(ncp) != 0) {
		goto exit;
	}

	/* check for reconfig of threads per core */
	ncp->n_threads_per_core = ddi_getprop(DDI_DEV_T_ANY, dip,
	    DDI_PROP_CANSLEEP | DDI_PROP_DONTPASS, "ncp-threads-per-core",
	    ncp->n_max_cpus_per_mau);

	/* get fips configuration : FALSE by default */
	ncp->n_is_fips = ddi_getprop(DDI_DEV_T_ANY, dip,
	    DDI_PROP_DONTPASS | DDI_PROP_CANSLEEP,
	    "ncp-fips-140", FALSE);
	if (ncp->n_is_fips) {
		DBG0(ncp, DWARN, "ncp: Device is in FIPS mode");
		/* If it's in FIPS mode, run the module verification test */
		if ((bignum_fips_check() != 0) ||
		    (fips_check_module("drv/ncp",
		    B_FALSE /* optional? */) != 0)) {
			ncp_diperror(dip, "FIPS-140 Software Integrity Test "
			    "failed");
			goto exit;
		}
	}

	/*
	 * Allocate cache for descriptors
	 */
	ncp->n_ds_cache = kmem_cache_create("ncp_ds_cache",
	    sizeof (ncp_desc_t), 0, NULL, NULL, NULL, NULL, NULL, 0);
	if (ncp->n_ds_cache == NULL) {
		ncp_diperror(dip, "kmem_cache_create(ncp_ds_cache) failed!");
		goto exit;
	}
	/*
	 * Allocate cache for MA control structures.
	 */
	ncp->n_mactl_cache = kmem_cache_create("ncp_mactl_cache",
	    sizeof (ncp_ma_t), 0, ncp_mactl_constructor, ncp_mactl_destructor,
	    NULL, NULL, NULL, 0);
	if (ncp->n_mactl_cache == NULL) {
		ncp_diperror(dip, "kmem_cache_create(ncp_mactl_cache) failed!");
		goto exit;
	}

	/*
	 * Allocate cache for request structures
	 */
	ncp->n_request_cache = kmem_cache_create("ncp_request_cache",
	    sizeof (ncp_request_t), 0, ncp_request_constructor,
	    ncp_request_destructor, NULL, ncp, NULL, 0);

	if (ncp->n_request_cache == NULL) {
		ncp_diperror(dip,
		    "kmem_cache_create(ncp_request_cache) failed!");
		goto exit;
	}

	/*
	 * Allocate cache for MA buffers.
	 */
	ncp->n_mabuf_cache = kmem_cache_create("ncp_mabuf_cache",
	    MA_BUF_SIZE, MA_BUF_SIZE, NULL, NULL, NULL, NULL, NULL, 0);
	if (ncp->n_mabuf_cache == NULL) {
		ncp_diperror(dip, "kmem_cache_create(ncp_mabuf_cache) failed!");
		goto exit;
	}

	/*
	 * Find out where our MAUs are and get the
	 * CPU-2-MAU mapping.
	 */
	ncp_alloc_mau2cpu_map(ncp);
	mutex_enter(&ncp->n_maumap.m_lock);
	if ((nmaus = ncp_update_mau2cpu_map(ncp)) < 0) {
		/*
		 * ncp_update_mau2cpu_map() returns either an error (-1)
		 * or the number of usuable MAUs in the system.
		 * If there are no MAUs available then we simply
		 * fail the Attach as we can't provide any crypto
		 * capability.
		 */
		mutex_exit(&ncp->n_maumap.m_lock);
		DBG0(ncp, DWARN, "ncp_update_mau2cpu_map failed");
		goto exit;
	}
	mutex_exit(&ncp->n_maumap.m_lock);


	/* register the callback function for CPU events */
	if (!(ncp_iscpuregistered(ncp))) {
		mutex_enter(&cpu_lock);
		register_cpu_setup_func(cpu_event_handler, ncp);
		ncp_setcpuregistered(ncp);
		mutex_exit(&cpu_lock);
	}

	/* initialize/configure the MAUs */
	if (nmaus && ncp_mau_q_init(ncp) != 0) {
		ncp_diperror(dip, "ncp_mau_q_init() failed");
		goto exit;
	}

	if (ncp_ECC_build_curve_table() != 0) {
		ncp_diperror(dip, "ncp_ECC_build_curve_table failed");
		goto exit;
	}

	if (ncp->n_hvapi_major_version >= 2) {
		if ((rv = ncp_add_intrs(ncp)) != DDI_SUCCESS) {
			ncp_diperror(dip, "ncp_add_intrs() failed");
			goto exit;
		}
		ncp_intr_enable(ncp);
	}


	/* kCF registration here */
	if ((rv = ncp_init(ncp)) != DDI_SUCCESS) {
		ncp_diperror(dip, "ncp_init() failed");
		goto exit;
	}

	/* initialize the DR lock */
	mutex_init(&ncp->n_dr_lock, NULL, MUTEX_DRIVER, NULL);

	/* LDOMs/DR not supported with version 1.0 */
	if (ncp->n_hvapi_major_version >= 2) {
		if (ncp_dr_init(ncp) != 0) {
			ncp_diperror(dip, "ncp_dr_init() failed");
			mutex_destroy(&ncp->n_dr_lock);
			(void) ncp_uninit(ncp);
			goto exit;
		}
	}

	ncp_init_job_timeout_info(ncp);

	/* Start the job timeout routine */
	ncp_jobtimeout_start(ncp);

	/* if it is in FIPS mode, run the POST */
	if (ncp->n_is_fips) {
		mutex_init(&ncp->n_fips_post_lock, NULL, MUTEX_DRIVER, NULL);
		cv_init(&ncp->n_fips_post_cv, NULL, CV_DRIVER, NULL);
		mutex_init(&ncp->n_fips_consistency_lock, NULL,
		    MUTEX_DRIVER, NULL);

		/*
		 * Run bignum module verification test,
		 */
		if ((ncp_rsa_post(ncp) != CRYPTO_SUCCESS) ||
		    (ncp_dsa_post(ncp) != CRYPTO_SUCCESS) ||
		    (ncp_ecc_post(ncp) != CRYPTO_SUCCESS)) {
			/* POST failed: do not attach */
			cmn_err(CE_WARN, "ncp: FIPS-140 POST failed");
			(void) ncp_uninit(ncp);
			if (ncp->n_hvapi_major_version >= 2) {
				(void) ncp_dr_fini();
			}
			/* Stop the job timeout routine */
			ncp_jobtimeout_stop(ncp);
			ncp_fini_job_timeout_info(ncp);
			mutex_destroy(&ncp->n_fips_post_lock);
			mutex_destroy(&ncp->n_fips_consistency_lock);
			cv_destroy(&ncp->n_fips_post_cv);
			goto exit;
		}
	}
	ncp_provider_notify_ready(ncp);

	/*
	 * If there are no MAUs then unregister the HV NCS API.
	 */
	if (nmaus == 0) {
		ncp_unregister_ncs_api();
		DBG0(ncp, DATTACH, "ncp_attach: No MAUs. Unregistering "
		    "HV NCS API.");
	}

	return (DDI_SUCCESS);

exit:
	ncp_deallocate(ncp, instance);

	return (DDI_FAILURE);
}

/*ARGSUSED*/
static int
ncp_resume(ncp_t *ncp)
{
	return (DDI_SUCCESS);
}

static void
ncp_deallocate(ncp_t *ncp, int instance)
{
	if (ncp == NULL)
		return;
	if (ncp->n_request_cache) {
		kmem_cache_destroy(ncp->n_request_cache);
		ncp->n_request_cache = NULL;
	}

	if (ncp->n_ds_cache != NULL) {
		kmem_cache_destroy(ncp->n_ds_cache);
		ncp->n_ds_cache = NULL;
	}
	if (ncp->n_mactl_cache != NULL) {
		kmem_cache_destroy(ncp->n_mactl_cache);
		ncp->n_mactl_cache = NULL;
	}
	if (ncp->n_mabuf_cache != NULL) {
		kmem_cache_destroy(ncp->n_mabuf_cache);
		ncp->n_mabuf_cache = NULL;
	}

	ncp_remove_intrs(ncp);

	ncp_ECC_destroy_curve_table();

	/* uninitialize/unconfigure all MAUs */
	ncp_mau_q_deinit(ncp);

	/* unregister the callback function for CPU events */
	if (ncp_iscpuregistered(ncp)) {
		mutex_enter(&cpu_lock);
		unregister_cpu_setup_func(cpu_event_handler, ncp);
		ncp_clrcpuregistered(ncp);
		mutex_exit(&cpu_lock);
	}

	ncp_deinit_mau2cpu_map(ncp);

	ncp_unregister_ncs_api();

	ddi_soft_state_free(ncp_softstate, instance);
}

int
ncp_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	int		rv = DDI_SUCCESS;
	int		instance;
	ncp_t		*ncp;

	instance = ddi_get_instance(dip);
	ncp = (ncp_t *)ddi_get_soft_state(ncp_softstate, instance);
	if (ncp == NULL) {
		ncp_diperror(dip, "no soft state in detach");
		return (DDI_FAILURE);
	}

	switch (cmd) {
	case DDI_SUSPEND:
		return (ncp_suspend(ncp));
	case DDI_DETACH:
		break;
	default:
		return (DDI_FAILURE);
	}

	/* kCF unregistration here */
	if ((rv = ncp_uninit(ncp)) != DDI_SUCCESS)
		return (rv);

	/* LDOMs/DR not supported with version 1.0 */
	if (ncp->n_hvapi_major_version >= 2) {
		/* unregister DR domain service handler */
		if (ncp_dr_fini() != 0) {
			ncp_diperror(dip, "ncp_dr_fini failed");
		}
	}

	/* Stop the job timeout routine */
	ncp_jobtimeout_stop(ncp);

	ncp_fini_job_timeout_info(ncp);

	if (ncp->n_is_fips) {
		/* destroy the mutexes after draining */
		mutex_destroy(&ncp->n_fips_post_lock);
		cv_destroy(&ncp->n_fips_post_cv);
		mutex_destroy(&ncp->n_fips_consistency_lock);
	}

	mutex_destroy(&ncp->n_dr_lock);

	/*
	 * shutdown device
	 */
	ncp_deallocate(ncp, instance);

	ncp = NULL;

	return (DDI_SUCCESS);
}

/*ARGSUSED*/
static int
ncp_suspend(ncp_t *ncp)
{
	return (DDI_SUCCESS);
}

static uint_t
ncp_mau_ihandler(void *arg1, void *arg2)
{
	ncp_t		*ncp = (ncp_t *)arg1;
	int		mid = *((int *)arg2);
	int		ix;
	int		old_head_idx, new_head_idx;
	uint64_t	new_head_offset;
	uint64_t	hvret;
	mau_entry_t	*mep;
	ncp_mau_queue_t	*nmq;
	ncp_descjob_t	*dj;

	DBG1(ncp, DINTR, "mau-intr: HELLO! ID = %d", mid);

	if ((mid < 0) || (mid >= ncp->n_maumap.m_nmaus)) {
		ncp_mau_set_intrmid(ncp);
		DBG1(ncp, DINTR,
		    "mau-intr: intr without configured queue (mid = %d)", mid);
		return (DDI_INTR_CLAIMED);
	}

	mep = &ncp->n_maumap.m_maulist[mid];
	nmq = &mep->mm_queue;

	mutex_enter(&nmq->nmq_lock);

	nmq->nmq_ks.qks_nintr++;	/* kstat */

	hvret = hv_ncs_gethead(nmq->nmq_handle, &new_head_offset);
	new_head_idx = NCP_QOFFSET_TO_QINDEX(new_head_offset);

	DBG4(ncp, DHV, "mau-intr: (ID %d) GETHEAD (hv = %ld) = 0x%llx "
	    "(index %d)", mid, hvret, new_head_offset, new_head_idx);

	if (hvret == H_EOK) {
		int	njobs;
		/*
		 * Make sure new head index is a valid one.
		 */
		if (!NCP_QINDEX_IS_VALID(new_head_idx)) {
			ncp_diperror(ncp->n_dip,
			    "ncp_mau_ihandler: mau-id(%d) invalid head "
			    "index(%d)", mid, new_head_idx);
			nmq->nmq_ks.qks_nintr_err++;	/* kstat */
			goto i_done;
		}

		/*
		 * Determine which jobs on joblist have
		 * completed and wake them up.  Then
		 * set our local copy of the head
		 * pointer based on the new index.
		 */
		old_head_idx = nmq->nmq_head;

		DBG3(ncp, DINTR,
		    "mau-intr: (ID %d) old_head_idx = %d, new_head_idx = %d",
		    mid, old_head_idx, new_head_idx);

		/*
		 * Determine completed jobs.
		 */
		njobs = 0;
		for (ix = old_head_idx; ix != new_head_idx;
		    ix = NCP_QINDEX_INCR(ix)) {
			if ((dj = nmq->nmq_jobs[ix]) == NULL)
				continue;

			dj->dj_pending = B_FALSE;
			nmq->nmq_jobs[ix] = NULL;

			DBG2(ncp, DINTR, "mau-intr: (ID %d) found job-id (%d)",
			    mid, dj->dj_id);
			njobs++;

			/* Check protocol, system mem, and MAU mem errors */
			ncp_check_errors(ncp, mid, dj);
			cv_broadcast(&dj->dj_cv);
		}

		nmq->nmq_ks.qks_nintr_jobs += njobs;	/* kstat */

		DBG2(ncp, DINTR, "mau-intr: (ID %d) found (%d) jobs",
		    mid, njobs);

		nmq->nmq_head = new_head_idx;

		(void) hv_ncs_sethead_marker(nmq->nmq_handle, new_head_offset);
	} else {
		ncp_diperror(ncp->n_dip,
		    "ncp_mau_ihandler: error encounted code: %ld\n",
		    hvret);
		nmq->nmq_ks.qks_nintr_err++;	/* kstat */
	}

i_done:
	mutex_exit(&nmq->nmq_lock);

	return (DDI_INTR_CLAIMED);
}

static int
ncp_add_intrs(ncp_t *ncp)
{
	dev_info_t	*dip = ncp->n_dip;
	ddi_intr_handle_t	*htp;
	int		actual = 0, count = 0;
	int		x, y, rc, inum = 0;

	/* get number of interrupts */
	rc = ddi_intr_get_nintrs(dip, DDI_INTR_TYPE_FIXED, &count);
	if ((rc != DDI_SUCCESS) || (count == 0)) {
		ncp_diperror(dip, "ddi_intr_get_nintrs() failed");
		return (DDI_FAILURE);
	}
	DBG1(ncp, DATTACH, "ncp_add_intrs: count = %d", count);

	if (count < ncp->n_maumap.m_nmaus) {
		/*
		 * There should be at least as many interrupts
		 * as there are MAUs.
		 */
		ncp_diperror(dip,
		    "ncp_add_intrs: count (%d) < nmaus (%d)",
		    count, ncp->n_maumap.m_nmaus);
		return (DDI_FAILURE);
	}

	rc = ddi_intr_alloc(dip, ncp->n_htable, DDI_INTR_TYPE_FIXED,
	    inum, count, &actual, DDI_INTR_ALLOC_STRICT);

	if ((rc != DDI_SUCCESS) || (actual != count)) {
		ncp_diperror(dip, "ddi_intr_alloc() error (rc=%d) "
		    "(actual(%d) < count(%d))", rc, actual, count);
		for (x = 0; x < actual; x++) {
			(void) ddi_intr_free(ncp->n_htable[x]);
		}

		return (DDI_FAILURE);
	}

	ncp->n_intr_cnt = actual;

	/* Get intr priority */
	rc = ddi_intr_get_pri(ncp->n_htable[0], &ncp->n_intr_pri);
	DBG1(ncp, DATTACH, "ncp_add_intrs: pri = %d", ncp->n_intr_pri);
	if (rc != DDI_SUCCESS) {
		ncp_diperror(dip, "ddi_intr_get_pri() failed");
		for (x = 0; x < actual; x++) {
			(void) ddi_intr_free(ncp->n_htable[x]);
		}

		return (DDI_FAILURE);
	}

	/* Call ddi_intr_add_handler() */
	htp = ncp->n_htable;

	for (x = 0; x < actual; x++) {
		ncp->n_intr_mid[x] = -1;
		rc = ddi_intr_add_handler(htp[x],
		    (ddi_intr_handler_t *)ncp_mau_ihandler,
		    (caddr_t)ncp, (void *)&ncp->n_intr_mid[x]);
		if (rc != DDI_SUCCESS) {
			ncp_diperror(dip,
			    "ddi_intr_add_handler(x = %d) failed", x);
			for (y = 0; y < x; y++) {
				(void) ddi_intr_remove_handler(htp[y]);
			}

			for (y = 0; y < actual; y++) {
				(void) ddi_intr_free(htp[y]);
			}

			return (DDI_FAILURE);
		}
	}

	return (DDI_SUCCESS);
}

static void
ncp_remove_intrs(ncp_t *ncp)
{
	int	x;

	DBG1(ncp, DINTR, "ncp_remove_intrs: count = %d", ncp->n_intr_cnt);

	for (x = 0; x < ncp->n_intr_cnt; x++) {
		if (ncp->n_htable[x] == NULL)
			continue;
		(void) ddi_intr_disable(ncp->n_htable[x]);
		(void) ddi_intr_remove_handler(ncp->n_htable[x]);
		(void) ddi_intr_free(ncp->n_htable[x]);
		ncp->n_htable[x] = NULL;
		ncp->n_intr_mid[x] = -1;
	}
}

static void
ncp_intr_enable(ncp_t *ncp)
{
	int	x;

	for (x = 0; x < ncp->n_intr_cnt; x++) {
		/* Note: this may fail if the associated MAU is offlined */
		(void) ddi_intr_enable(ncp->n_htable[x]);
	}
	ncp_mau_set_intrmid(ncp);
}

void
ncp_intr_enable_mau(ncp_t *ncp, int mid)
{
	int	i;

	for (i = 0; i < ncp->n_intr_cnt; i++) {
		if (ncp->n_intr_mid[i] == mid) {
			(void) ddi_intr_enable(ncp->n_htable[i]);
			return;
		}
	}

	ncp_mau_set_intrmid(ncp);

	for (i = 0; i < ncp->n_intr_cnt; i++) {
		if (ncp->n_intr_mid[i] == mid) {
			(void) ddi_intr_enable(ncp->n_htable[i]);
			return;
		}
	}
}

void
ncp_intr_disable_mau(ncp_t *ncp, int mid)
{
	int	i;

	for (i = 0; i < ncp->n_intr_cnt; i++) {
		if (ncp->n_intr_mid[i] == mid) {
			(void) ddi_intr_disable(ncp->n_htable[i]);
			return;
		}
	}

	ncp_mau_set_intrmid(ncp);

	for (i = 0; i < ncp->n_intr_cnt; i++) {
		if (ncp->n_intr_mid[i] == mid) {
			(void) ddi_intr_disable(ncp->n_htable[i]);
			return;
		}
	}
}


#if	(BIG_CHUNK_SIZE != 64)
#error THIS ONLY WORKS WITH 64-bit CHUNKS of BIGNUM
#endif


BIG_ERR_CODE
ncp_ma_activate(uint64_t *mamemout, uint64_t *mamemin, int mamemlen,
    uchar_t *maind, uint64_t nprime, int length, int optype,
    void *ncp_in, void *reqp_in)
{
	ncp_t		*ncp = (ncp_t *)ncp_in;
	ncp_request_t	*reqp = (ncp_request_t *)reqp_in;
	ncp_desc_t	*djobp = NULL, *djoblastp, *dp, *ndp;
	ncp_ma_t	*ma1p = NULL, *ma2p = NULL;
	ma_regs_t	*ma_regs;
	uint8_t		*ma1, *ma2;
	int		rv = BIG_OK;

	/*
	 * Allocate MA memory.
	 */
	ma1p = ncp_ma_alloc(ncp);
	if (ma1p == NULL) {
		ncp_diperror(ncp->n_dip, "ncp_ma_alloc(ma1) failed");
		rv = BIG_NO_MEM;
		goto exit;
	}
	ma1 = ma1p->nma_mem;
	ma2p = ncp_ma_alloc(ncp);
	if (ma2p == NULL) {
		ncp_diperror(ncp->n_dip, "ncp_ma_alloc(ma2) failed");
		rv = BIG_NO_MEM;
		goto exit;
	}
	ma2 = ma2p->nma_mem;

	/* Copy values into local copy of MA memory */
	bcopy((void *)mamemin, (void *)ma1, mamemlen);

	/*
	 * Post load/op/store descriptors to queue
	 */
	djobp = djoblastp = NULL;
	/*
	 * Allocate MA load(s) tasks.
	 */
	rv = ncp_ma_ldst(ncp, &djobp, &djoblastp, ma1p, mamemlen,
	    MA_OP_LOAD);
	if (rv != 0) {
		rv = BIG_NO_MEM;
		goto exit;
	}

	/*
	 * Post MA operation
	 */
	dp = ncp_desc_alloc(ncp->n_ds_cache, NULL, ND_TYPE_CONT);
	if (dp == NULL) {
		ncp_diperror(ncp->n_dip,
		    "ncp_desc_alloc(optype = %d) failed", optype);
		rv = BIG_NO_MEM;
		goto exit;
	}
	djoblastp->nd_link = dp;
	djoblastp = dp;

	ma_regs = &dp->nd_hv.nhd_regs;
	bzero(ma_regs, sizeof (ma_regs_t));

	/* Contstruct ASI_MA_ADDR */
	ma_regs->mr_ma.bits.address0 = maind[0];
	ma_regs->mr_ma.bits.address1 = maind[1];
	ma_regs->mr_ma.bits.address2 = maind[2];
	ma_regs->mr_ma.bits.address3 = maind[3];
	ma_regs->mr_ma.bits.address4 = maind[4];
	ma_regs->mr_ma.bits.address5 = maind[5];
	ma_regs->mr_ma.bits.address6 = maind[6];
	ma_regs->mr_ma.bits.address7 = maind[7];

	/* Construct ASI_MA_NP */
	ma_regs->mr_np = nprime;

	/* Construct ASI_MA_CONTROL */
	if (ncp->n_binding == NCP_CPU_N1) {
		if (optype == MA_OP_EXPONENTIATE) {
			ma_regs->mr_ctl.bits.n1.operation =
			    MA_OP_EXPONENTIATE;
			ma_regs->mr_ctl.bits.n1.length = length;
		} else {
			rv = BIG_INVALID_ARGS;
			goto exit;
		}
	} else {
		ma_regs->mr_ctl.bits.n2.length = length;
		ma_regs->mr_ctl.bits.n2.operation = optype;
	}

	/*
	 * Post MA store of results.
	 */
	rv = ncp_ma_ldst(ncp, &djobp, &djoblastp, ma2p, mamemlen,
	    MA_OP_STORE);
	if (rv != 0) {
		rv = BIG_NO_MEM;
		goto exit;
	}
	djobp->nd_hv.nhd_type = ND_TYPE_START;
	djoblastp->nd_hv.nhd_type |= ND_TYPE_END;

	/*
	 * Submit all tasks to NCS
	 */
	rv = ncp_desc_submit(ncp, reqp, djobp);
	if (rv != 0) {
		/*
		 * Note that we leave rv set to the errno (>0)
		 * that it was given rather than translate to
		 * a BIG_ERR_CODE (<0).  This will ultimately
		 * result in bignum code returning CRYPTO_FAILURE
		 * to framework.
		 */
		goto exit;
	}

	/* Copy result buffer to the destination */
	bcopy(ma2, mamemout, mamemlen);

exit:
	for (dp = djobp; dp; dp = ndp) {
		ndp = dp->nd_link;
		ncp_desc_free(ncp->n_ds_cache, dp);
	}

	if (ma1p)
		ncp_ma_free(ncp, ma1p);

	if (ma2p)
		ncp_ma_free(ncp, ma2p);

	return (rv);
}



typedef struct
{
	BIGNUM		*result;
	BIGNUM		*a;
	BIGNUM		*e;
	BIGNUM		*n;
	BIGNUM		*x;
	BIG_CHUNK_TYPE	n0;
	int		ncp_binding;
} ncp_modex_params_t;


int
modexp_fill_ma(uint8_t *mabuf, ma_regs_t *ma_regs, void *params)
{
	ncp_modex_params_t	*mpars = (ncp_modex_params_t *)params;
	BIGNUM		*a = mpars->a;
	BIGNUM		*e = mpars->e;
	BIGNUM		*n = mpars->n;
	BIGNUM		*x = mpars->x;
	BIG_CHUNK_TYPE	n0 = mpars->n0;
	int		ncp_binding = mpars->ncp_binding;
	size_t		a_offset;	/* MA operand byte offsets */
	size_t		m_offset;
	size_t		n_offset;
	size_t		x_offset;
	size_t		e_offset;
	size_t		e_len;
	size_t		n_len;
	size_t		ma_len;
	/* LINTED */
	uint64_t	*ma1 = (uint64_t *)mabuf;
	uint64_t	*src, *dst;
	size_t		len;
	int		i;

	n_len = n->len;

	/* Calculate MA total length in bytes */
	ma_len = n_len * 5 * sizeof (uint64_t);
	ASSERT(ma_len <= MA_SIZE);
	for (i = 0; i < 5 * n_len; i++) {
		ma1[i] = 0;
	}

	/* Convert exponent length into bytes - XXX snip out leading 0's */
	e_len = e->len * sizeof (uint64_t);
	ASSERT(e_len <= 256);

	/* Calculate operand byte offsets into MA */
	a_offset = 0;
	m_offset = a_offset + n_len;
	n_offset = m_offset + n_len;
	x_offset = n_offset + n_len;
	e_offset = x_offset + n_len;

	/* Copy values into MA memory */
	len = a->len * sizeof (uint64_t);
	ASSERT(len <= n_len * sizeof (uint64_t));
	src = a->value;
	dst = &ma1[a_offset];
	bcopy((void *)src, (void *)dst, len);

	len = n->len * sizeof (uint64_t);
	ASSERT(len <= n_len * sizeof (uint64_t));
	src = n->value;
	dst = &ma1[n_offset];
	bcopy((void *)src, (void *)dst, len);

	if (ncp_binding == NCP_CPU_N1) {
		/* copy X to the X field of the MA memory */
		len = x->len * sizeof (uint64_t);
		ASSERT(len <= n_len * sizeof (uint64_t));
		src = x->value;
	} else {
		/* copy A to the X field of the MA memory */
		len = a->len * sizeof (uint64_t);
		ASSERT(len <= n_len * sizeof (uint64_t));
		src = a->value;
	}
	dst = &ma1[x_offset];
	bcopy((void *)src, (void *)dst, len);

	len = e->len * sizeof (uint64_t);

	ASSERT(len <= n_len * sizeof (uint64_t));
	src = e->value;
	dst = &ma1[e_offset];
	bcopy((void *)src, (void *)dst, len);
	if (ncp_binding == NCP_CPU_N1) {
		/*
		 * For N1, exponent needs to be converted to big endian
		 * across 64 bit words.
		 */
		ncp_ma_reverse((void *)dst, len);
	}

	/* Contstruct ASI_MA_ADDR */
	bzero(ma_regs, sizeof (ma_regs_t));

	/* Contstruct ASI_MA_ADDR */
	ma_regs->mr_ma.bits.address0 = (uchar_t)a_offset;
	ma_regs->mr_ma.bits.address1 = (uchar_t)m_offset;
	ma_regs->mr_ma.bits.address2 = (uchar_t)n_offset;
	ma_regs->mr_ma.bits.address3 = (uchar_t)x_offset;
	ma_regs->mr_ma.bits.address4 = (uchar_t)e_offset;
	/* Exponent length in bytes - 1 */
	ma_regs->mr_ma.bits.address5 =  e_len - 1;
	ma_regs->mr_ma.bits.address6 = 0;
	ma_regs->mr_ma.bits.address7 = 0;

	/* Construct ASI_MA_NP */
	ma_regs->mr_np = n0;

	/* Contstruct ASI_MA_CONTROL */
	if (ncp_binding == NCP_CPU_N1) {
		ma_regs->mr_ctl.bits.n1.operation = MA_OP_EXPONENTIATE;
		ma_regs->mr_ctl.bits.n1.length = n_len - 1;
	} else	{
		ma_regs->mr_ctl.bits.n2.operation = MA_OP_EXPONENTIATE;
		ma_regs->mr_ctl.bits.n2.length = n_len - 1;
	}

	return ((4 * n->len + e->len) * sizeof (uint64_t));
}


/* ARGSUSED */
int
modexp_getresult_ma(uint8_t *mabuf, ma_regs_t *ma_regs, void *params)
{
	ncp_modex_params_t	*mpars = (ncp_modex_params_t *)params;
	BIGNUM			*n = mpars->n;
	BIGNUM			*result = mpars->result;
	int			len;
	uint8_t			*src;
	uint64_t		*dst;

	result->len = n->len;

	/* Copy result buffer to bignum */
	src = mabuf + 3 * n->len * sizeof (uint64_t);
	len = result->len * sizeof (uint64_t);
	dst = result->value;
	bcopy((void *)src, (void *)dst, len);
	return (BIG_OK);
}


BIG_ERR_CODE
ncp_ma_activate1(ma_fill_fun_t filler, ma_getresult_fun_t getresult,
    void *params, void *ncp_in, void *reqp_in)
{
	ncp_t		*ncp = (ncp_t *)ncp_in;
	ncp_request_t	*reqp = (ncp_request_t *)reqp_in;
	ncp_desc_t	*djobp = NULL, *djoblastp, *dp, *dp1, *ndp;
	ncp_ma_t	*ma1p = NULL, *ma2p = NULL;
	uint8_t		*ma1, *ma2;
	int		mamemlen;
	int		rv = BIG_OK;

	/*
	 * Allocate MA memory.
	 */
	ma1p = ncp_ma_alloc(ncp);
	if (ma1p == NULL) {
		ncp_diperror(ncp->n_dip, "ncp_ma_alloc(ma1) failed");
		rv = BIG_NO_MEM;
		goto exit;
	}
	ma1 = ma1p->nma_mem;

	ma2p = ncp_ma_alloc(ncp);
	if (ma2p == NULL) {
		ncp_diperror(ncp->n_dip, "ncp_ma_alloc(ma2) failed");
		rv = BIG_NO_MEM;
		goto exit;
	}
	ma2 = ma2p->nma_mem;

	dp = ncp_desc_alloc(ncp->n_ds_cache, NULL, ND_TYPE_CONT);
	if (dp == NULL) {
		ncp_diperror(ncp->n_dip,
		    "ncp_desc_alloc in ncp_ma_activate failed");
		rv = BIG_NO_MEM;
		goto exit;
	}
	mamemlen = filler(ma1, &dp->nd_hv.nhd_regs, params);

	if ((ncp->n_binding != NCP_CPU_N1) &&
	    (dp->nd_hv.nhd_regs.mr_ctl.bits.n2.operation ==
	    MA_OP_POINTMULGF2M)) {
		dp1 = ncp_desc_alloc(ncp->n_ds_cache, NULL, ND_TYPE_CONT);
		if (dp1 == NULL) {
			ncp_diperror(ncp->n_dip,
			    "ncp_desc_alloc in ncp_ma_activate failed");
			rv = BIG_NO_MEM;
			goto exit;
		}
		bcopy(&dp->nd_hv.nhd_regs,
		    &dp1->nd_hv.nhd_regs, sizeof (ma_regs_t));
		dp->nd_hv.nhd_regs.mr_ctl.bits.n2.operation =
		    MA_OP_POINTDBLADDGF2M;
	} else {
		dp1 = NULL;
	}

	/*
	 * Post load/op/store descriptors to queue
	 */
	djobp = djoblastp = NULL;

	/*
	 * Post MA load of parameters
	 */
	rv = ncp_ma_ldst(ncp, &djobp, &djoblastp, ma1p, mamemlen, MA_OP_LOAD);
	if (rv != 0) {
		rv = BIG_NO_MEM;
		goto exit;
	}

	/*
	 * Post MA operation(s)
	 */
	djoblastp->nd_link = dp;
	djoblastp = dp;
	if (dp1 != NULL) {
		djoblastp->nd_link = dp1;
		djoblastp = dp1;
	}

	/*
	 * Post MA store of results.
	 */
	rv = ncp_ma_ldst(ncp, &djobp, &djoblastp, ma2p, mamemlen,
	    MA_OP_STORE);
	if (rv != 0) {
		rv = BIG_NO_MEM;
		goto exit;
	}
	djobp->nd_hv.nhd_type = ND_TYPE_START;
	djoblastp->nd_hv.nhd_type |= ND_TYPE_END;

	/*
	 * Submit all tasks to NCS
	 */
	rv = ncp_desc_submit(ncp, reqp, djobp);
	if (rv != 0) {
		/*
		 * Note that we leave rv set to the errno (>0)
		 * that it was given rather than translate to
		 * a BIG_ERR_CODE (<0).  This will ultimately
		 * result in bignum code returning CRYPTO_FAILURE
		 * to framework.
		 */
		goto exit;
	}

	/* Copy result buffer to the destination */
	rv = getresult(ma2, &dp->nd_hv.nhd_regs, params);

exit:
	for (dp = djobp; dp; dp = ndp) {
		ndp = dp->nd_link;
		ncp_desc_free(ncp->n_ds_cache, dp);
	}

	if (ma1p)
		ncp_ma_free(ncp, ma1p);

	if (ma2p)
		ncp_ma_free(ncp, ma2p);

	return (rv);
}



/*
 * Convert modular exponentiation arguments into MA memory and submit to NCS.
 *
 * a	Base
 * n	Modulus
 * e	Exponent
 * x	R^2 mod N, where R = 2^(n->len * 64): montgomery encoded 1
 * n0	Inverse modulus operand ("n0" in bignum terminology -
 *          that is (n[0] * n0 mod 2^64) = 0xffffffffffffffff )
 *
 *    1) Load MA memory
 *	 - Put operands and exponent size into ncp_ma byte array
 *	 - Put physical address of ncp_ma into ASI_MA_MPA
 *	 - Put starting offset of ncp_ma to load from into ADDR0 of
 *	   ASI_MA_ADDR (always 0 ?).
 *	 - Write ASI_MA_CONTROL:
 *		o op == MA load (0)
 *		o Our HW thread ID
 *		o Length ==  size of valid portion of ncp_ma in 64 bit
 *		  words - 1.
 *	 - Poll ASI_MA_CONTROL until load completes
 *    2) Execute modular exponentiation:
 *	 - Write ASI_MA_CONTROL such that:
 *		o op == Modular exponentiation (4)
 *		o Length == size of operation (a, m, n) in 64 bit words.
 *	 - Poll ASI_MA_CONTROL until modular exponentiation completes
 *    3) Store MA memory
 *	 - Put starting offset of ncp_ma to store to into ADDR0 of
 *	   ASI_MA_ADDR (always 0 ?).
 *	 - Write ASI_MA_CONTROL:
 *		o op == MA store (1)
 *		o Length ==  size of valid portion of ncp_ma in 64 bit
 *		  words - 1.
 *	 - Poll ASI_MA_CONTROL until store completes
 *    4) Extract restult (x) from ncp_ma and return value.
 */



BIG_ERR_CODE
ncp_big_modexp_ncp(BIGNUM *result, BIGNUM *a, BIGNUM *e, BIGNUM *n,
		BIGNUM *x, BIG_CHUNK_TYPE n0, void *ncp_in, void *reqp_in)
{
	ncp_t			*ncp = (ncp_t *)ncp_in;
	ncp_modex_params_t	mpars;
	int			rv = BIG_OK;

	if (ncp->n_binding != NCP_CPU_N1) {
		/*
		 * If e is zero, return the X value, which is a Montgomery
		 * encoded 1.  This is necessary for N2 & derivatives.
		 */
		if (ncp_big_is_zero(e)) {
			return (ncp_big_copy(result, x));
		}
	}

	mpars.result = result;
	mpars.a = a;
	mpars.e = e;
	mpars.n = n;
	mpars.x = x;
	mpars.n0 = n0;
	mpars.ncp_binding = ncp->n_binding;

	rv = ncp_ma_activate1(modexp_fill_ma, modexp_getresult_ma,
	    (void *)(&mpars), ncp_in, reqp_in);

	return (rv);
}


/*
 * Returns a linked list of qtask entries in tlistpp to the
 * caller for subsequent submission to the MA unit.
 *
 * Returns EBUSY if we run out of qtask entries.
 *
 * Note that we do not set *tlistpp or *tlastpp to NULL as they
 * may have been set from a previous ncp_ma_ldst call.  We rely
 * on the caller knowing what they're doing and that if these
 * parameters are non-NULL, then the intent is to simply add
 * the new tasks to the end of the task list passed in.
 */
static int
ncp_ma_ldst(ncp_t *ncp, ncp_desc_t **dheadpp, ncp_desc_t **dtailpp,
	ncp_ma_t *mp, size_t ma_len, int ma_ldst)
{
	ncp_desc_t	*dp, *ndp, *dhp, *dtp;
	ma_regs_t	*ma_regs;
	uint64_t	realaddr;
	uint8_t		*ma;
	size_t		ma_words;
	size_t		i;
	int		rv = 0;
	int		ma_load_max;
#ifdef DEBUG
	int		dcount = 0;
#endif /* DEBUG */

	ma = mp->nma_mem;

	/* Convert to real addresses */
	realaddr = (uint64_t)va_to_pa(ma);

	dhp = dtp = NULL;
	/*
	 * We can only load/store up to 64 MA words at a time in N1
	 */
	ma_words = BYTES_TO_UINT64(ma_len);
	if (ncp->n_binding == NCP_CPU_N1) {
		ma_load_max = MA_LOAD_MAX_N1;
	} else {
		ma_load_max = MA_LOAD_MAX_N2;
	}

	for (i = 0; i < ma_words; i += ma_load_max) {

		dp = ncp_desc_alloc(ncp->n_ds_cache, mp, ND_TYPE_CONT);
		if (dp == NULL) {
			/*
			 * Not enough descriptors available
			 * for what we need.  Free up the ones
			 * we did allocate and return busy.
			 */
			for (dp = dhp; dp; dp = ndp) {
				ndp = dp->nd_link;
				ncp_desc_free(ncp->n_ds_cache, dp);
			}
			return (EBUSY);
		}

		if (dhp == NULL) {
			dhp = dtp = dp;
		} else {
			dtp->nd_link = dp;
			dtp = dp;
		}

		/*
		 * Build the MA registers for load/store operation
		 */
		ma_regs = &dp->nd_hv.nhd_regs;
		bzero(ma_regs, sizeof (ma_regs_t));

		/* Contstruct ASI_MA_CONTROL value */
		if (ncp->n_binding == NCP_CPU_N1)
			ma_regs->mr_ctl.bits.n1.operation = ma_ldst;
		else
			ma_regs->mr_ctl.bits.n2.operation = ma_ldst;

		/* Construct source physical address and MA offset */
		ma_regs->mr_mpa.value =
		    realaddr + (i * sizeof (uint64_t));

#ifdef DEBUG
		ASSERT(ma_regs->mr_mpa.bits.reserved1 == 0);
		{
			uint64_t	e;

			e = ma_regs->mr_mpa.value +
			    (MIN((ma_words - i), ma_load_max) *
			    sizeof (uint64_t));
			ASSERT(e <= (realaddr + MA_SIZE));
		}
#endif /* DEBUG */

		ma_regs->mr_ma.bits.address0 = i;

		/* Length in 64 bit words minus 1 */
		if (ncp->n_binding == NCP_CPU_N1)
			ma_regs->mr_ctl.bits.n1.length =
			    MIN(ma_words - i, ma_load_max) - 1;
		else
			ma_regs->mr_ctl.bits.n2.length =
			    MIN(ma_words - i, ma_load_max) - 1;

#ifdef DEBUG
		dcount++;
#endif /* DEBUG */
	}

	/*
	 * Tack on the list we built to the one the caller
	 * passed in.
	 */
	if (*dheadpp == NULL)
		*dheadpp = dhp;
	else
		(*dtailpp)->nd_link = dhp;
	*dtailpp = dtp;

	DBG1(NULL, DMA_LDST, "ncp_ma_ldst: dcount = %d", dcount);

	return (rv);
}


mau_entry_t *
ncp_remap_mau(ncp_t *ncp)
{
	int		mid;

	/* find a MAU unit and bind to it */
	mid = ncp_map_nextmau(ncp);
	if (mid < 0) {
		/*
		 * Yikes!  No MAU found!
		 */
		DBG0(ncp, DWARN, "ncs_desc_submit: NO MAU FOUND!");
		return (NULL);
	}
	return (ncp_map_holdmau(ncp, mid));

}

/*
 * Takes the given list of descriptors and enqueues
 * them into the Hypervisor MAU queue.  Once completed
 * a NCS_QTAIL_UPDATE call is made to the Hypervisor
 * to effectively tell it that there is work to do
 * in its MAU queue.
 */
static int
ncp_desc_submit(ncp_t *ncp, ncp_request_t *reqp, ncp_desc_t *dtaskp)
{
	ncp_desc_t		*dp;
	ncp_descjob_t		*dj;
	ncp_mau_queue_t		*nmq;
	mau_entry_t		*mep;
	uint64_t		qoffset;
	int			mid, ix, err = 0;
	int			last_ix;
	int			orig_tail;
	int			dcnt;
	uint64_t		hvret = H_EOK;
	ncs_qtail_update_arg_t	qtup;

	mep = reqp->nr_mep;
	mid = reqp->nr_mep->mm_mauid;


	ASSERT(mid == mep->mm_mauid);
	mutex_enter(&mep->mm_lock);
	nmq = &mep->mm_queue;

	if (nmq->nmq_init == 0) {
		int	rv;

		rv = ncp_mau_q_configure(ncp, nmq, mid);
		DBG4(ncp, DATTACH,
		    "q-config: mau-id (%d) handle = 0x%llx (dino=0x%lx) "
		    "(%s)", mid, nmq->nmq_handle, nmq->nmq_devino,
		    rv ? "ERR" : "OK");
		if (rv) {
			DBG0(ncp, DWARN, "q_configure failed");
			mutex_exit(&mep->mm_lock);
			return (CRYPTO_DEVICE_ERROR);
		}
		ASSERT((ncp->n_hvapi_major_version >= 2) ?
		    (nmq->nmq_handle > 0) : 1);

		/* enable the interrupt for the MAU */
		ncp_intr_enable_mau(ncp, mid);
	}
	mutex_exit(&mep->mm_lock);

	mutex_enter(&nmq->nmq_lock);

	nmq->nmq_ks.qks_njobs++;	/* kstat */

	/*
	 * Count how many descriptors we have to make sure
	 * there is room in the queue.
	 */
	for (dcnt = 0, dp = dtaskp; dp; dcnt++, dp = dp->nd_link)
		;

	if (dcnt > NCP_MAQUEUE_SLOTS_AVAIL(nmq)) {
		clock_t	qtimeout_time, cvret;

		nmq->nmq_ks.qks_qfull++;	/* kstat */

		DBG2(ncp, DWARN,
		    "ncp_desc_submit: queue full (cnt=%d, avail=%d)",
		    dcnt, NCP_MAQUEUE_SLOTS_AVAIL(nmq));

		nmq->nmq_busy_wait++;
		/*
		 * Set an absolute timeout for which we'll wait
		 * for the queue to become non-busy.
		 */
		qtimeout_time = ncp_qtimeout_seconds * SECOND;	/* usecs */
		qtimeout_time = ddi_get_lbolt() + drv_usectohz(qtimeout_time);
		err = 0;
		while (dcnt > NCP_MAQUEUE_SLOTS_AVAIL(nmq)) {
			cvret = cv_timedwait_sig(&nmq->nmq_busy_cv,
			    &nmq->nmq_lock, qtimeout_time);
			if (cvret <= 0) {
				nmq->nmq_ks.qks_qbusy++;	/* kstat */
				err = EWOULDBLOCK;
				break;
			}

			/* Check if mau was taken offline while waiting */
			if (mep->mm_state != MAU_STATE_ONLINE) {
				DBG1(ncp, DWARN, "ncp_desc_submit: queue (%d) "
				    "canceled job before submission",
				    mep->mm_mauid);
				err = ECANCELED;
				break;
			}
		}
		nmq->nmq_busy_wait--;

		if (err != 0)
			goto s_exit;

	} else if (mep->mm_state != MAU_STATE_ONLINE) {
		DBG1(ncp, DWARN, "ncp_desc_submit: queue (%d) "
		    "canceled job before submission", mep->mm_mauid);
		err = ECANCELED;
		goto s_exit;
	}

	/*
	 * Copy our local copy of the ncs_hvdesc_t entries
	 * into the actual descriptor queue that the hypervisor
	 * can see.
	 */
	last_ix = -1;
	for (ix = nmq->nmq_tail, dp = dtaskp; dp; dp = dp->nd_link) {
		/*
		 * v1.0 - no interrupt support.
		 * v2.0 - does support interrupts.
		 *
		 * Make sure the MAU hardware knows what we want.
		 */
		if (ncp->n_binding == NCP_CPU_N1) {
			dp->nd_hv.nhd_regs.mr_ctl.bits.n1.interrupt =
			    (ncp->n_hvapi_major_version == 1) ? 0 : 1;
		} else {
			dp->nd_hv.nhd_regs.mr_ctl.bits.n2.interrupt =
			    (ncp->n_hvapi_major_version == 1) ? 0 : 1;
		}

		bcopy((caddr_t)&dp->nd_hv, (caddr_t)&nmq->nmq_desc[ix],
		    sizeof (ncs_hvdesc_t));

		last_ix = ix;
		ix = NCP_QINDEX_INCR(ix);
	}
	ASSERT(dp == NULL);
	ASSERT(ix != nmq->nmq_head);

	if (last_ix < 0) {
		DBG0(ncp, DWARN, "ncp_desc_submit: task list EMPTY!");
		goto s_exit;
	}
	dj = ncp_desc_addjob(nmq, dtaskp, last_ix);

	/*
	 * We successfully enqueued all of the descriptors.
	 * Update our local tail index then issue the update
	 * to the hypervisor which effectively kicks off
	 * the MAU operations.
	 */
	orig_tail = nmq->nmq_tail;
	nmq->nmq_tail = ix;

	if (ncp->n_hvapi_major_version == 1) {
		qtup.nu_mid = mid;
		qtup.nu_tail = nmq->nmq_tail;
		qtup.nu_syncflag = NCS_SYNC;

		hvret = hv_ncs_request(NCS_QTAIL_UPDATE, va_to_pa(&qtup),
		    sizeof (qtup));
	} else {
		qoffset = NCP_QINDEX_TO_QOFFSET(nmq->nmq_tail);

		hvret = hv_ncs_settail(nmq->nmq_handle, qoffset);
	}
	err = ncp_herr2kerr(hvret);

	DBG1(ncp, DHV, "ncp_desc_submit: hvret = %ld", hvret);

	if (hvret == H_EOK) {
		if (ncp->n_hvapi_major_version == 1) {
			/*
			 * In synchronous operation the queue will
			 * be empty.
			 */
			nmq->nmq_head = nmq->nmq_tail;
		} else {
			/*
			 * Update job timeout information before submitting.
			 */
			mutex_enter(&ncp->n_job[mid].lock);
			ncp->n_job[mid].submitted++;
			ncp->n_job[mid].stalled.limit += reqp->nr_timeout;
			mutex_exit(&ncp->n_job[mid].lock);

			(void) cv_wait_sig(&dj->dj_cv, &nmq->nmq_lock);

			/*
			 * Update job timeout information after coming back.
			 */
			mutex_enter(&ncp->n_job[mid].lock);
			ncp->n_job[mid].reclaimed++;
			ncp->n_job[mid].stalled.limit -= reqp->nr_timeout;
			mutex_exit(&ncp->n_job[mid].lock);

			/* Update the error if the job has been canceled. */
			if (!err && dj->dj_error)
				err = dj->dj_error;
		}
	} else {

		ncp_diperror(ncp->n_dip,
		    "ncp_desc_submit: error code: %ld\n", hvret);

		/*
		 * Some kind of error occurred in HV.
		 * Determine where current Tail is and update
		 * our local copy appropriately.
		 */
		DBG1(ncp, DHV,
		    "ncp_desc_submit: submit ERROR (orig_tail = %d)",
		    orig_tail);
		nmq->nmq_ks.qks_qfail++;	/* kstat */

		/*
		 * In HV API version 1.0 we simply reset our copy
		 * of the tail index back to the previous location.
		 * In HV API version 2.0 we directly ask the HV
		 * where its tail is.
		 */
		if (ncp->n_hvapi_major_version >= 2) {
			qoffset = (uint64_t)-1;
			hvret = hv_ncs_gettail(nmq->nmq_handle, &qoffset);
			DBG1(ncp, DHV,
			    "ncp_desc_submit: gettail() hvret = %ld", hvret);
			if (hvret == H_EOK) {
				orig_tail = NCP_QOFFSET_TO_QINDEX(qoffset);
			}
			DBG1(ncp, DHV, "ncp_desc_submit: submit ERROR "
			    "(reset tail -> %d)", orig_tail);
		}
		nmq->nmq_tail = orig_tail;
	}

	ncp_desc_deljob(nmq, dj);

	if (nmq->nmq_busy_wait > 0)
		cv_broadcast(&nmq->nmq_busy_cv);

s_exit:
	mutex_exit(&nmq->nmq_lock);

	return (err);
}

/*
 * Allocate a queue task structure.  If a MA buffer reference
 * is passed in then we ensure that new task has a reference
 * to that buffer.
 */
static ncp_desc_t *
ncp_desc_alloc(kmem_cache_t *dcache, ncp_ma_t *mp, int nd_type)
{
	ncp_desc_t	*dp;

	dp = kmem_cache_alloc(dcache, KM_SLEEP);
	dp->nd_link = NULL;
	dp->nd_hv.nhd_state = ND_STATE_PENDING;
	dp->nd_hv.nhd_type = nd_type;
	dp->nd_hv.nhd_errstatus = MAU_ERR_OK;
	if (mp != NULL) {
		mutex_enter(&mp->nma_lock);
		mp->nma_ref++;
		mutex_exit(&mp->nma_lock);
		dp->nd_ma = mp;
	} else {
		dp->nd_ma = NULL;
	}

	return (dp);
}

/*
 * Free a descriptor structure and remove any MA buffer
 * references.
 */
static void
ncp_desc_free(kmem_cache_t *dcache, ncp_desc_t *dp)
{
	ncp_ma_t	*mp;

	mp = dp->nd_ma;
	if (mp != NULL) {
		mutex_enter(&mp->nma_lock);
		mp->nma_ref--;
		mutex_exit(&mp->nma_lock);
		dp->nd_ma = NULL;
	}
	kmem_cache_free(dcache, dp);
}

static ncp_descjob_t *
ncp_desc_addjob(ncp_mau_queue_t *nmq, ncp_desc_t *djobp, int id)
{
	ncp_descjob_t	*dj;

	ASSERT(MUTEX_HELD(&nmq->nmq_lock));
	ASSERT(NCP_QINDEX_IS_VALID(id));

	ASSERT(nmq->nmq_jobs[id] == NULL);

	dj = kmem_zalloc(sizeof (ncp_descjob_t), KM_SLEEP);

	nmq->nmq_jobs[id] = dj;
	dj->dj_id = id;
	dj->dj_jobp = djobp;
	dj->dj_pending = B_TRUE;

	cv_init(&dj->dj_cv, NULL, CV_DRIVER, NULL);

	if (nmq->nmq_joblist == NULL) {
		dj->dj_next = dj->dj_prev = dj;
		nmq->nmq_joblist = dj;
	} else {
		dj->dj_next = nmq->nmq_joblist;
		dj->dj_prev = nmq->nmq_joblist->dj_prev;
		dj->dj_next->dj_prev = dj;
		dj->dj_prev->dj_next = dj;
	}

	nmq->nmq_joblistcnt++;

	return (dj);
}

static void
ncp_desc_deljob(ncp_mau_queue_t *nmq, ncp_descjob_t *dj)
{
	ASSERT(MUTEX_HELD(&nmq->nmq_lock));

	dj->dj_next->dj_prev = dj->dj_prev;
	dj->dj_prev->dj_next = dj->dj_next;

	if (nmq->nmq_joblist == dj)
		nmq->nmq_joblist = dj->dj_next;

	if (dj->dj_pending == B_TRUE)
		nmq->nmq_jobs[dj->dj_id] = NULL;
	nmq->nmq_joblistcnt--;
	if (nmq->nmq_joblistcnt == 0)
		nmq->nmq_joblist = NULL;

	cv_destroy(&dj->dj_cv);
	kmem_free(dj, sizeof (ncp_descjob_t));
}

/*
 * Allocate a MA buffer for communication of
 * modular exponentiation parameters to MA unit.
 */
static ncp_ma_t *
ncp_ma_alloc(ncp_t *ncp)
{
	ncp_ma_t	*mp;

	mp = kmem_cache_alloc(ncp->n_mactl_cache, KM_SLEEP);
	mp->nma_mem = kmem_cache_alloc(ncp->n_mabuf_cache, KM_SLEEP);
	mp->nma_ref = 0;
	bzero(mp->nma_mem, MA_BUF_SIZE);

	return (mp);
}

/*
 * Free a MA buffer and control structure.
 */
void
ncp_ma_free(ncp_t *ncp, ncp_ma_t *mp)
{
	ASSERT(mp->nma_ref == 0);
	kmem_cache_free(ncp->n_mabuf_cache, mp->nma_mem);
	kmem_cache_free(ncp->n_mactl_cache, mp);
}

static int
ncp_mau_q_init(ncp_t *ncp)
{
	int		cid, mau_id;
	ncp_mau_queue_t	*nmq;
	int		err = 0;
	int		cnt = 0;

	mutex_enter(&ncp->n_maumap.m_lock);
	for (mau_id = 0; mau_id < NCP_MAX_MAX_NMAUS; mau_id++) {
		nmq = &ncp->n_maumap.m_maulist[mau_id].mm_queue;
		bzero(nmq, sizeof (ncp_mau_queue_t));

		/* bind to the MAU */
		cid = ncp_map_mau_to_cpu(ncp, mau_id, 1);
		if (cid < 0) {
			/* No CPU associated with the MAU */
			continue;
		}

		err = ncp_mau_q_configure(ncp, nmq, mau_id);

		ncp_unmap_mau_to_cpu(ncp, cid, 1);


		if (err != 0) {
			DBG1(ncp, DWARN, "Unable to configure MAU %d", mau_id);
			break;
		}
		cnt++;
	}
	if (err != 0) {
		ncp_mau_q_deinit(ncp);
	}
	mutex_exit(&ncp->n_maumap.m_lock);
	return (err);
}


static void
ncp_mau_q_deinit(ncp_t *ncp)
{
	int		i;
	ncp_mau_queue_t	*nmq;

	for (i = 0; i < ncp->n_maumap.m_nmaus; i++) {
		nmq = &ncp->n_maumap.m_maulist[i].mm_queue;
		if (nmq->nmq_init == 0) {
			continue;
		}

		ncp_mau_q_unconfigure(ncp, nmq);
	}
}


/*
 * Caller must hold mm_lock
 */
int
ncp_mau_q_configure(ncp_t *ncp, ncp_mau_queue_t *nmq, int mau_id)
{
	int		err = 0;
	uint64_t	qhandle;
	uint64_t	devino;
	uint64_t	hvret;
	uint64_t	qsize;
	ncs_qconf_arg_t	qconf;

	if (nmq->nmq_init == 1)
		return (0);

	/*
	 * Queues need to reside within a physical page since
	 * we want the descriptor entries on a contiguous range
	 * of addresses for the hypervisor.
	 */
	nmq->nmq_head = nmq->nmq_tail = 0;
	nmq->nmq_wrapmask = NCP_MAQUEUE_WRAPMASK;
	nmq->nmq_inum = -1;
	nmq->nmq_mauid = mau_id;
	mutex_init(&nmq->nmq_lock, NULL, MUTEX_DRIVER, NULL);
	cv_init(&nmq->nmq_busy_cv, NULL, CV_DRIVER, NULL);
	nmq->nmq_jobs_size = NCP_MAQUEUE_NENTRIES * sizeof (ncp_descjob_t *);
	nmq->nmq_jobs = kmem_zalloc(nmq->nmq_jobs_size, KM_SLEEP);
	qsize = NCP_MAQUEUE_NENTRIES * sizeof (ncs_hvdesc_t);
	nmq->nmq_memsize = qsize + NCP_MAQUEUE_ALIGN;
	/*
	 * The HV logic assumes that queues reside on physically
	 * contiguous memory.
	 */
	nmq->nmq_mem = contig_mem_alloc_align(nmq->nmq_memsize, PAGESIZE);
	if (nmq->nmq_mem == NULL) {
		cmn_err(CE_WARN,
		    "ncp: contig_mem_alloc_align(%ld) failed",
		    nmq->nmq_memsize);
		ncp_mau_q_unconfigure(ncp, nmq);

		return (ENOMEM);
	}
	nmq->nmq_desc = (ncs_hvdesc_t *)P2ROUNDUP((uint64_t)nmq->nmq_mem,
	    NCP_MAQUEUE_SIZE);

	DBG2(ncp, DATTACH, "q-config: qsize = %ld, nentries = %d",
	    NCP_MAQUEUE_SIZE, NCP_MAQUEUE_NENTRIES);

	ASSERT(((uint64_t)nmq->nmq_desc - (uint64_t)nmq->nmq_mem +
	    NCP_MAQUEUE_SIZE) <= nmq->nmq_memsize);

	if (ncp->n_hvapi_major_version == 1) {
		qconf.nq_mid = mau_id;
		qconf.nq_base = va_to_pa(nmq->nmq_desc);
		qconf.nq_end = qconf.nq_base + qsize;
		qconf.nq_nentries = NCP_MAQUEUE_NENTRIES;
		hvret = hv_ncs_request(NCS_QCONF, va_to_pa(&qconf),
		    sizeof (qconf));
	} else {
		qhandle = 0;
		hvret = hv_ncs_qconf(NCS_QTYPE_MAU, va_to_pa(nmq->nmq_desc),
		    NCP_MAQUEUE_NENTRIES, &qhandle);
	}
	if (hvret != H_EOK) {

		ncp_diperror(ncp->n_dip, "hv ncs q-conf failed! "
		    "(rv = %ld)", hvret);
		err = ncp_herr2kerr(hvret);

		ncp_mau_q_unconfigure(ncp, nmq);

		return (err);
	}

	if (ncp->n_hvapi_major_version == 1) {
		/*
		 * For 1.0 we do not support interrupts so
		 * we're done.
		 */
		nmq->nmq_init = 1;
		return (err);
	}

	ASSERT(qhandle > 0);

	devino = 0;
	hvret = hv_ncs_qhandle_to_devino(qhandle, &devino);
	if (hvret != H_EOK) {

		ncp_diperror(ncp->n_dip, "hv_ncs_qhandle_to_devino() failed! "
		    "(rv = %ld)", hvret);
		err = ncp_herr2kerr(hvret);

		ncp_mau_q_unconfigure(ncp, nmq);

		return (err);
	}
	if (devino == 0) {
		ncp_diperror(ncp->n_dip, "devino is NULL");
		ncp_mau_q_unconfigure(ncp, nmq);
		return (EINVAL);
	}

	/*
	 * On successful queue-configuration the HV will return the
	 * MAU handle.
	 */
	nmq->nmq_handle = qhandle;
	nmq->nmq_devino = devino;
	nmq->nmq_init = 1;

	return (err);
}

/*
 * Caller must hold mm_lock
 */
void
ncp_mau_q_unconfigure(ncp_t *ncp, ncp_mau_queue_t *nmq)
{
	ncs_qconf_arg_t	qconf;

	mutex_enter(&nmq->nmq_lock);

	if (ncp->n_hvapi_major_version == 1) {
		qconf.nq_mid = nmq->nmq_mauid;
		qconf.nq_base = 0;

		(void) hv_ncs_request(NCS_QCONF, va_to_pa(&qconf),
		    sizeof (qconf));
	} else {
		(void) hv_ncs_qconf(NCS_QTYPE_MAU, nmq->nmq_handle, 0, NULL);
	}

	if ((nmq->nmq_inum >= 0) && ncp->n_intr_mid) {
		ncp->n_intr_mid[nmq->nmq_inum] = -1;
	}
	nmq->nmq_inum = -1;

	cv_destroy(&nmq->nmq_busy_cv);
	if (nmq->nmq_mem != NULL) {
		contig_mem_free(nmq->nmq_mem, nmq->nmq_memsize);
		nmq->nmq_mem = NULL;
	}
	nmq->nmq_desc = NULL;
	if (nmq->nmq_jobs != NULL) {
		kmem_free(nmq->nmq_jobs, nmq->nmq_jobs_size);
	}
	mutex_exit(&nmq->nmq_lock);
	mutex_destroy(&nmq->nmq_lock);
	nmq->nmq_init = 0;
}


/*
 * This function is called from ihandler
 */
static void
ncp_mau_set_intrmid(ncp_t *ncp)
{
	int		i;
	int		inum;
	ncp_mau_queue_t	*nmq;

	for (i = 0; i < ncp->n_maumap.m_nmaus; i++) {
		if (ncp->n_maumap.m_maulist[i].mm_state != MAU_STATE_ONLINE) {
			DBG2(ncp, DINTR, "mau %d not online (state %d)",
			    i, ncp->n_maumap.m_maulist[i].mm_state);
			continue;
		}
		nmq = &ncp->n_maumap.m_maulist[i].mm_queue;
		if (!nmq->nmq_init) {
			/*
			 * If the nmq is not initialized, devino is not set
			 * correctly.  Do not set intrmid for the nmq.
			 */
			DBG1(ncp, DINTR, "mau %d not initialized", i);
			continue;
		}

		inum = vnex_ino_to_inum(ncp->n_dip, nmq->nmq_devino);
		if (inum < 0) {
			ncp_diperror(ncp->n_dip, "vnex_ino_to_inum(ino=0x%lx) "
			    "failed", nmq->nmq_devino);
			return;
		}
		nmq->nmq_inum = inum;
		ncp->n_intr_mid[nmq->nmq_inum] = i;

		DBG2(ncp, DINTR, " setting intr MID: inum %d, mid %d",
		    inum, i);
	}
}

/*
 * Reverse endianess of 64 bit words in value.
 */
static void
ncp_ma_reverse(void *value, size_t len)
{
	int		lo, hi;
	uint64_t	*value_w;
	uint64_t	tmp;

	ASSERT(len % sizeof (uint64_t) == 0);

	/*
	 * Walk list from lo and hi end twards middle, swaping words at each
	 * interation.
	 */
	value_w = (uint64_t *)value;
	lo = 0;
	hi = (len / sizeof (uint64_t)) - 1;
	while (lo < hi) {
		tmp = value_w[lo];
		value_w[lo] = value_w[hi];
		value_w[hi] = tmp;

		lo++;
		hi--;
	}
}

/*
 * Map hypervisor error code to solaris. Only
 * H_ENORADDR, H_EBADALIGN and H_EWOULDBLOCK are meaningful
 * to this device. Any other error codes are mapped EINVAL.
 */
static int
ncp_herr2kerr(uint64_t hv_errcode)
{
	int s_errcode;

	switch (hv_errcode) {
	case H_ENORADDR:
	case H_EBADALIGN:
		s_errcode = EFAULT;
		break;
	case H_EWOULDBLOCK:
		s_errcode = EWOULDBLOCK;
		break;
	case H_EIO:
		s_errcode = EIO;
		break;
	case H_EOK:
		s_errcode = 0;
		break;
	default:
		s_errcode = EINVAL;
		break;
	}
	return (s_errcode);
}

/*
 * Check for hypervisor detected errors returned from the interrupt handler.
 */
static void
ncp_check_errors(ncp_t *ncp, int mid, ncp_descjob_t *dj)
{
	uint8_t		err;
	uint8_t		op;
	int		dcnt;
	int		ecnt;
	ncp_desc_t	*dp;

	/* Hardware error bits do not apply to N1 */
	if (ncp->n_binding == NCP_CPU_N1)
		return;

	/*
	 * Check for errors in every job descriptor looking for errors.
	 * report first one found
	 */
	for (dcnt = 0, ecnt = 0, dp = dj->dj_jobp, dj->dj_error = 0;
	    dp; dcnt++, dp = dp->nd_link) {

		/* Extract error code from the crypto job descriptor */
		err = (uint8_t)(dp->nd_hv.nhd_errstatus) & MAU_ERR_MASK;
		if (err == MAU_ERR_OK)
			continue;

		switch (err) {
		case MAU_ERR_INVOP:
			ncp_diperror(ncp->n_dip,
			    "mau %d encountered a protocol error, "
			    "service continued.\n", mid);
			if (!ecnt) {
				dj->dj_error = EPROTO;
			}
			ecnt++;
			break;

		case MAU_ERR_HWE:
			/* This only applies to load or store operations */
			op = dp->nd_hv.nhd_regs.mr_ctl.bits.n2.operation;
			if ((op == MA_OP_LOAD) || (op == MA_OP_STORE)) {
				/* Take device offline after all mem errors */
				ncp_diperror(ncp->n_dip, "mau %d encountered "
				    "a %s and has been taken offline", mid,
				    (op == MA_OP_LOAD) ? "UE" : "PE");
				/* Offline the mau */
				mutex_enter(&ncp->n_maumap.m_lock);
				ncp_offline_mau(ncp, mid);
				mutex_exit(&ncp->n_maumap.m_lock);

				/* Check last mau was taken offline */
				ncp_check_maus(ncp);

				if (!ecnt) {
					dj->dj_error = EFAULT;
				}
				ecnt++;
			} else {
				DBG1(ncp, DWARN, "Received invalid hwe for "
				    "operation = %d", op);
			}
			break;
		default:
			/* Should never get here */
			ncp_diperror(ncp->n_dip,
			    "mau %d encountered an unknown error (%08x), "
			    "service continued", mid, err);
			if (!ecnt) {
				dj->dj_error = EIO;
			}
			ecnt++;
		}
	}
}

/*
 * This routine is called after a crypto job fails with a ENODEV failure to see
 * if the last available mau has been taken offline and we should unregister
 * from kcf.
 */
static void
ncp_check_maus(ncp_t *ncp)
{
	/* Check if error caused all mmu's to be taken offline */
	if (ncp->n_maumap.m_nmaus_online <= 0) {
		/*
		 * Fault, all MAUs are bad.
		 * 1. Notify kCF of failure
		 * 2. Log an error message
		 */
		ncp_setfailed(ncp);

		/*
		 * Report provider failure to kCF so it stops sending jobs
		 */
		crypto_provider_notification(ncp->n_prov,
		    CRYPTO_PROVIDER_FAILED);
		DBG0(ncp, DWARN,
		    "CRYPTO_PROVIDER_FAILED notification sent to kCF");

		/* Log error indicating that all MAUs are bad */
		ncp_diperror(ncp->n_dip,
		    "ncp_check_maus: all MAUs have failed");
	}
}

static void
ncp_init_job_timeout_info(ncp_t *ncp)
{
	int		i;
	ncp_job_info_t	*job;

	mutex_init(&ncp->n_timeout_lock, NULL, MUTEX_DRIVER, NULL);

	bzero(&ncp->n_timeout, sizeof (ncp_timeout_t));
	ncp->n_timeout.ticks = drv_usectohz(SECOND);

	for (i = 0; i < NCP_MAX_MAX_NMAUS; i++) {
		job = &ncp->n_job[i];

		bzero(job, sizeof (ncp_job_info_t));

		/* set ticks for one second granularity */
		job->timeout.ticks = drv_usectohz(SECOND);

		/* setup base amount to add per outstanding job */
		job->stalled.addend = job->timeout.ticks;

		/* setup the stall margin  */
		job->stalled.limit = NCP_JOB_STALL_LIMIT * job->timeout.ticks;

		mutex_init(&job->lock, NULL, MUTEX_DRIVER, NULL);
	}
}


static void
ncp_fini_job_timeout_info(ncp_t *ncp)
{
	int		i;
	ncp_job_info_t	*job;

	mutex_destroy(&ncp->n_timeout_lock);
	for (i = 0; i < NCP_MAX_MAX_NMAUS; i++) {
		job = &ncp->n_job[i];
		mutex_destroy(&job->lock);
	}
}

/*
 * This function continuously re-schedules a ncp_jobtimeout() call to check
 * for stalled MAUs.  The default re-schedule frequency ksone second.
 */
static void
ncp_jobtimeout_start(ncp_t *ncp)
{
	mutex_enter(&ncp->n_timeout_lock);

	/* Check again in <n_timeout.ticks>. */
	ncp->n_timeout.id = timeout(ncp_jobtimeout, (void *)ncp,
	    ncp->n_timeout.ticks);

	mutex_exit(&ncp->n_timeout_lock);
}

/*
 * This function is called to timeout the currently scheduled timeout thread
 * during detach.
 */
static void
ncp_jobtimeout_stop(ncp_t *ncp)
{
	timeout_id_t	tid;

	/* untimeout the timeouts */
	mutex_enter(&ncp->n_timeout_lock);
	tid = ncp->n_timeout.id;
	ncp->n_timeout.id = 0;
	mutex_exit(&ncp->n_timeout_lock);
	if (tid)
		(void) untimeout(tid);
}

/*
 * In case a MAU is hung, we need to reject the pending jobs to the framework.
 */
static void
ncp_drain_jobs(ncp_t *ncp, int mid)
{
	ncp_mau_queue_t	*nmq;
	mau_entry_t	*mep;
	ncp_descjob_t	*dj;
	int		i;

	/* Find the MAU data structure based on its ID */
	if ((mep = ncp_map_findmau(ncp, mid)) == NULL)
		return;

	nmq = &mep->mm_queue;

	mutex_enter(&nmq->nmq_lock);

	/* Walk through the pending job list and wake up each. */
	dj = nmq->nmq_joblist;
	for (i = 0; i < nmq->nmq_joblistcnt; i++) {
		dj->dj_error = ECANCELED;
		cv_broadcast(&dj->dj_cv);
		dj = dj->dj_next;
	}
	mutex_exit(&nmq->nmq_lock);
}

/*
 * This function will check for a timeout (stall) condition on all MAUs.
 */

static void
ncp_jobtimeout(void *arg)
{
	int	i;
	ncp_t	*ncp = (ncp_t *)arg;
	int	rv = MAU_STATE_ONLINE;

	mutex_enter(&ncp->n_maumap.m_lock);
	/* Check every mau for the timeout condition */
	for (i = 0; i < ncp->n_maumap.m_nmaus; i++) {
		int mid = ncp->n_maumap.m_maulist[i].mm_mauid;
		int state = ncp->n_maumap.m_maulist[i].mm_state;

		/* Only check maus that are currently online */
		if (state != MAU_STATE_ONLINE)
			continue;

		ASSERT(mid >= 0 && mid < NCP_MAX_MAX_NMAUS);

		/* Take an mau offline if a timeout (stall) is detected */
		if ((rv = ncp_jobtimeout_mau(ncp, mid)) != MAU_STATE_ONLINE) {

			/* Stop checking if we are out of maus */
			if (ncp->n_maumap.m_nmaus_online <= 0)
				break;
		}
	}
	mutex_exit(&ncp->n_maumap.m_lock);

	if ((rv != MAU_STATE_ONLINE) && (ncp->n_maumap.m_nmaus_online <= 0)) {
		/*
		 * Fault, all MAUs are bad.
		 * 1. unregister from the framework.
		 * 2. log an error message.
		 */

		DBG0(ncp, DWARN, "ncp_jobtimeout: Unregistering from kCF");
		/* Unregister from kCF since we are out of maus */
		ncp_check_maus(ncp);

	} else {
		/*
		 * Restart.
		 */
		ncp_jobtimeout_start(ncp);
	}
}


/*
 * Check for timeout (stall) on an individual MAU.
 * Return code: 1 - normal/online, 0 - hang/offlined.
 */
static int
ncp_jobtimeout_mau(ncp_t *ncp, int mid)
{
	ncp_counter_t		submitted, reclaimed;
	int			limit;
	int			ret = MAU_STATE_ONLINE;
	ncp_job_info_t		*job = &ncp->n_job[mid];

	/* Increment counter for statistics */
	job->timeout.count++;

	mutex_enter(&job->lock);
	submitted = job->submitted;
	reclaimed = job->reclaimed;
	limit = job->stalled.limit;
	mutex_exit(&job->lock);

	/*
	 * There are 2 `impossible' error conditions we ought to look for:
	 * 1. submitted < reclaimed
	 * 2. reclaimed < progress
	 * `1' is possible if submitted wraps.
	 * `2' is possible if reclaimed wraps.
	 * But considering that UINT64_MAX == 18446744073709551615ULL,
	 * it seems extremely unlikely that either event would happen.
	 * For now, we'll keep these error checks until we
	 * are sure the corresponding errors won't show up.
	 */

	if (submitted < reclaimed || reclaimed < job->progress) {
		cmn_err(CE_WARN, "in [%lld] / out [%lld] / high [%lld]",
		    submitted, reclaimed, job->progress);
		goto exit;
	}

	/*
	 * Check if all submitted jobs have completed or that progress has at
	 * least been made (more jobs completed) since the last check
	 */
	if (submitted == reclaimed || 		/* We're caught up */
	    job->progress < reclaimed) {	/* There was progress */
		job->progress = reclaimed;	/* Record current progress */
		job->stalled.count = 0;		/* Reset timeout counter */
		goto exit;
	}

	/*
	 * We have not made progress or caught up since our last check.
	 * Increment timeout counter by the amount of time we just waited.
	 */
	job->stalled.count += job->stalled.addend;

	/* Check if we have waited longer than the cumulative timeout limit */
	if (job->stalled.count >= limit) {

		/* This MAU is hung and needs to be taken offline */
		ncp_offline_mau(ncp, mid);
		ncp_diperror(ncp->n_dip,
		    "mau %d stalled and was taken offline, count [%ld] / "
		    "limit [%d]", mid, job->stalled.count, limit);
		ret = MAU_STATE_OFFLINE;

		/* Reject any pending jobs to the framework. */
		ncp_drain_jobs(ncp, mid);
	}

exit:

	return (ret);
}

static int
ncp_init_ncs_api(ncp_t *ncp)
{
	uint64_t	ncs_minor_ver;
	int		v, rv;

	for (v = 0; v < NVERSIONS; v++) {
		rv = hsvc_register(&ncs_hsvc[v], &ncs_minor_ver);
		if (rv == 0)
			break;

		DBG4(ncp, DATTACH,
		    "ncp_init_ncs_api: grp: 0x%lx, maj: %ld, min: %ld, "
		    "errno: %d",
		    ncs_hsvc[v].hsvc_group, ncs_hsvc[v].hsvc_major,
		    ncs_hsvc[v].hsvc_minor, rv);
	}
	if (v == NVERSIONS) {
		for (v = 0; v < NVERSIONS; v++) {
			cmn_err(CE_WARN,
			    "%s: cannot negotiate hypervisor services "
			    "group: 0x%lx major: %ld minor: %ld errno: %d",
			    ncs_hsvc[v].hsvc_modname,
			    ncs_hsvc[v].hsvc_group, ncs_hsvc[v].hsvc_major,
			    ncs_hsvc[v].hsvc_minor, rv);
		}
		return (-1);
	}
	ncp_ncs_version_idx = v;
	ncs_hsvc_available = B_TRUE;
	DBG2(ncp, DATTACH,
	    "ncp_init_ncs_api: NCS API VERSION (%ld.%ld)",
	    ncs_hsvc[ncp_ncs_version_idx].hsvc_major, ncs_minor_ver);
	ncp->n_hvapi_major_version = ncs_hsvc[ncp_ncs_version_idx].hsvc_major;
	ncp->n_hvapi_minor_version = (uint_t)ncs_minor_ver;

	return (0);
}

int
ncp_register_ncs_api(void)
{
	int			rv;
	uint64_t		ncs_minor_ver;

	if (ncs_hsvc_available == B_TRUE)
		return (0);

	rv = hsvc_register(&ncs_hsvc[ncp_ncs_version_idx], &ncs_minor_ver);
	if (rv == 0) {
		ncs_hsvc_available = B_TRUE;
	} else {
		cmn_err(CE_WARN, "ncp_register_ncs_api: hsvc_register "
		    "returned 0x%x", rv);
	}

	return (rv);
}

void
ncp_unregister_ncs_api(void)
{
	int	rv;

	if (ncs_hsvc_available) {
		rv =  hsvc_unregister(&ncs_hsvc[ncp_ncs_version_idx]);
		if (rv == EINVAL) {
			cmn_err(CE_WARN, "ncp_unregister_ncs_api: ncs api not "
			    "registered");
		}
		ncs_hsvc_available = B_FALSE;
	}
}
