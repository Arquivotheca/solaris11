/*
 * Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Niagara 2 Crypto Provider driver
 */

#include <sys/types.h>
#include <sys/atomic.h>	/* for atomic_inc_64 */
#include <sys/sysmacros.h>
#include <sys/modctl.h>
#include <sys/conf.h>
#include <sys/devops.h>
#include <sys/cmn_err.h>
#include <sys/kmem.h>
#include <sys/stat.h>
#include <sys/open.h>
#include <sys/file.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/cpuvar.h>
#include <sys/disp.h>
#include <sys/x_call.h>
#include <sys/error.h>
#include <sys/hsvc.h>
#include <sys/machsystm.h>
#include <sys/crypto/common.h>
#include <sys/crypto/spi.h>
#include <sys/ncs.h>
#include <sys/n2cp.h>
#include <sys/hypervisor_api.h>
#include <sys/time.h>
#include <sys/taskq.h>

#include <vm/hat_sfmmu.h>
#include <fips_checksum.h>

static void	*noncache_contig_mem_span_alloc(vmem_t *, size_t, int);
static void	noncache_contig_mem_span_free(vmem_t *, void *, size_t);
static void	*noncache_contig_mem_alloc(size_t);
static void	noncache_contig_mem_free(void *, size_t);
static void	noncache_contig_mem_init(void);
static void	noncache_contig_mem_deinit(void);
static void	noncache_bcopy(const void *, void *, size_t);
int		n2cp_ulcwq_start(n2cp_t *n2cp, n2cp_request_t *reqp);
static int	is_in_list(int *list, int size, int item);
int	n2cp_fips_post_qid = N2CP_FIPS_POST_NOT_STARTED;

noncache_info_t	n2cp_nc;
#define	N2_ERRATUM_175_STRING		"n2-erratum-175-workaround"
#define	N2_ERRATUM_175_DEFVALUE		0

extern size_t	contig_mem_slab_size;

#define	CWQ_INTR_TASK	1

static int 	n2cp_attach(dev_info_t *, ddi_attach_cmd_t);
static int 	n2cp_detach(dev_info_t *, ddi_detach_cmd_t);
static int 	n2cp_suspend(n2cp_t *);
static int 	n2cp_resume(n2cp_t *);

static n2cp_t	*n2cp_allocate(dev_info_t *, int);
static void	n2cp_deallocate(n2cp_t *, int);
static int	n2cp_cwq_q_init(n2cp_t *);
static void	n2cp_cwq_q_deinit(n2cp_t *);
static int	n2cp_herr2kerr(uint64_t);
static int	n2cp_check_errors(n2cp_t *n2cp, int cwq_id, uint64_t err);
static void	n2cp_check_cwqs(n2cp_t *n2cp);
static void	n2cp_sync_update_head(cwq_t *);
static int	n2cp_sync_process(cwq_t *, n2cp_request_t *,
			cwq_cw_t *, cwq_entry_t *);
static void	n2cp_wake_next_job(cwq_t *);
static void	n2cp_cwq_addjob(cwq_t *, n2cp_request_t *, int);
static void	n2cp_cwq_deljob(cwq_t *, n2cp_request_t *);

static int	n2cp_init_ncs_api(n2cp_t *);
int		n2cp_register_ncs_api(void);
void		n2cp_unregister_ncs_api(void);

static void	copy_cw(cwq_cw_t *, cwq_cw_t *, int);

/*
 * We want these inlined for performance.
 */
#ifndef	DEBUG
#pragma inline(n2cp_wake_next_job, n2cp_sync_update_head)
#pragma	inline(n2cp_cwq_addjob, n2cp_cwq_deljob, copy_cw)
#endif /* !DEBUG */

#define	SSL_DERIVE_ALIGNMENT	16

/*
 * The following are the fixed IVs that hardware
 * is expecting for the respective hash algorithms.
 */
const fixed_iv_t iv_md5 = {
	MD5_DIGESTSZ,
	0x01234567, 0x89abcdef, 0xfedcba98, 0x76543210
};
const fixed_iv_t iv_sha1 = {
	SHA1_DIGESTSZ,
	0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476,
	0xc3d2e1f0
};
const fixed_iv_t iv_sha256 = {
	SHA256_DIGESTSZ,
	0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
	0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
};
const fixed_iv_t iv_sha384 = {
	SHA512_DIGESTSZ,
	0xcbbb9d5d, 0xc1059ed8, 0x629a292a, 0x367cd507,
	0x9159015a, 0x3070dd17, 0x152fecd8, 0xf70e5939,
	0x67332667, 0xffc00b31, 0x8eb44a87, 0x68581511,
	0xdb0c2e0d, 0x64f98fa7, 0x47b5481d, 0xbefa4fa4
};
const fixed_iv_t iv_sha512 = {
	SHA512_DIGESTSZ,
	0x6a09e667, 0xf3bcc908, 0xbb67ae85, 0x84caa73b,
	0x3c6ef372, 0xfe94f82b, 0xa54ff53a, 0x5f1d36f1,
	0x510e527f, 0xade682d1, 0x9b05688c, 0x2b3e6c1f,
	0x1f83d9ab, 0xfb41bd6b, 0x5be0cd19, 0x137e2179
};

/*
 * Device operations.
 */
static struct dev_ops devops = {
	DEVO_REV,		/* devo_rev */
	0,			/* devo_refcnt */
	nodev,			/* devo_getinfo */
	nulldev,		/* devo_identify */
	nulldev,		/* devo_probe */
	n2cp_attach,		/* devo_attach */
	n2cp_detach,		/* devo_detach */
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
	"N2CP Crypto Driver",		/* drv_linkinfo */
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
static void	*n2cp_softstate = NULL;

/*
 * Priority of taskq threads for handling interrupts.
 */
pri_t		n2cp_intrtask_pri = N2CP_INTRTASK_PRI;

/*
 * Hypervisor NCS services information.
 */
static boolean_t ncs_hsvc_available = B_FALSE;

#define	NVERSIONS	1

/*
 * Supported HV API versions by this driver.
 */
static hsvc_info_t ncs_hsvc[NVERSIONS] = {
	{ HSVC_REV_1, NULL, HSVC_GROUP_NCS, 2, 1, "n2cp" },	/* v2.1 */
};
static int	n2cp_ncs_version_idx;	/* index into ncs_hsvc[] */

/*
 * Timeout value for waiting for a full queue to empty out.
 */
int	n2cp_qtimeout_seconds = N2CP_QTIMEOUT_SECONDS;

static int n2cp_sync_threads = N2CP_MAX_SYNC_THREADS;
int n2cp_use_ulcwq = 0;

/*
 * DDI entry points.
 */
int
_init(void)
{
	int	rv;

	rv = ddi_soft_state_init(&n2cp_softstate, sizeof (n2cp_t), 1);
	if (rv != 0) {
		/* this should *never* happen! */
		return (rv);
	}

	if ((rv = mod_install(&modlinkage)) != 0) {
		/* cleanup here */
		ddi_soft_state_fini(&n2cp_softstate);
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
		ddi_soft_state_fini(&n2cp_softstate);
	}

	return (rv);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

static int
n2cp_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	n2cp_t		*n2cp = NULL;
	int		instance;

	instance = ddi_get_instance(dip);
	/*
	 * Only instance 0 of n2cp driver is allowed.
	 */
	if (instance != 0) {
		n2cp_diperror(dip, "only one instance (0) allowed");
		return (DDI_FAILURE);
	}

	switch (cmd) {
	case DDI_RESUME:
		n2cp = (n2cp_t *)ddi_get_soft_state(n2cp_softstate, instance);
		if (n2cp == NULL) {
			n2cp_diperror(dip, "no soft state in attach");
			return (DDI_FAILURE);
		}
		return (n2cp_resume(n2cp));

	case DDI_ATTACH:
		break;
	default:
		return (DDI_FAILURE);
	}

	n2cp_nc.n_workaround_enabled = ddi_getprop(DDI_DEV_T_ANY, dip,
	    DDI_PROP_DONTPASS
	    | DDI_PROP_CANSLEEP
	    | DDI_PROP_NOTPROM,
	    N2_ERRATUM_175_STRING,
	    N2_ERRATUM_175_DEFVALUE);

	if (N2CP_ERRATUM_175_ENABLED()) {
		cmn_err(CE_NOTE, "!ERRATUM_175 crypto workaround enabled");
		n2cp_nc.n_contig_alloc = noncache_contig_mem_alloc;
		n2cp_nc.n_contig_free = noncache_contig_mem_free;
		n2cp_nc.n_bcopy = noncache_bcopy;
	} else {
		cmn_err(CE_NOTE, "!ERRATUM_175 crypto workaround disabled");
		n2cp_nc.n_contig_alloc = contig_mem_alloc;
		n2cp_nc.n_contig_free = contig_mem_free;
		n2cp_nc.n_bcopy = bcopy;
	}

	if ((n2cp = n2cp_allocate(dip, instance)) == NULL)
		return (DDI_FAILURE);

	/* If it is in FIPS mode, run the self integrity test */
	if (n2cp->n_is_fips == TRUE) {
		DBG0(n2cp, DWARN, "n2cp: Device is in FIPS mode");
		if (fips_check_module("drv/n2cp",
		    B_FALSE /* optional */) != 0) {
			cmn_err(CE_WARN, "n2cp: FIPS-140 Software Integrity "
			    "Test failed\n");
			n2cp_deallocate(n2cp, instance);
			return (DDI_FAILURE);
		}
	}


	if (n2cp_dr_init(n2cp) != 0) {
		cmn_err(CE_WARN,
		    "n2cp DR domain service initialization failed");
		n2cp_deallocate(n2cp, instance);
		return (DDI_FAILURE);
	}

	/* kCF registration here */
	if (n2cp_init(n2cp) != DDI_SUCCESS) {
		cmn_err(CE_WARN, "failed to register to kEF Framework");
		(void) n2cp_dr_fini();
		n2cp_deallocate(n2cp, instance);
		return (DDI_FAILURE);
	}

	/* If it is in FIPS mode, run the POST */
	if (n2cp->n_is_fips == TRUE) {
		if ((n2cp_block_post(n2cp) != CRYPTO_SUCCESS) ||
		    (n2cp_hash_post(n2cp) != CRYPTO_SUCCESS)) {
			/* POST failed: do not attach */
			cmn_err(CE_WARN, "n2cp: FIPS-140 POST failed");
			(void) n2cp_uninit(n2cp);
			(void) n2cp_dr_fini();
			/* shutdown device */
			n2cp_deallocate(n2cp, instance);
			return (DDI_FAILURE);
		}

		n2cp_fips_post_qid = N2CP_FIPS_POST_DONE;
	}

	/* notify KEF that we are ready to process */
	n2cp_provider_notify_ready(n2cp);

	/*
	 * If there are no CWQs then unregister the HV NCS API.
	 */
	if (n2cp->n_cwqmap.m_ncwqs == 0) {
		n2cp_unregister_ncs_api();
		DBG0(n2cp, DATTACH, "n2cp_attach: No CWQs. Unregistering "
		    "HV NCS API.");
	}

	return (DDI_SUCCESS);
}

static int
n2cp_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	int		instance;
	n2cp_t		*n2cp;

	instance = ddi_get_instance(dip);
	n2cp = (n2cp_t *)ddi_get_soft_state(n2cp_softstate, instance);
	if (n2cp == NULL) {
		n2cp_diperror(dip, "no soft state in detach");
		return (DDI_FAILURE);
	}

	switch (cmd) {
	case DDI_SUSPEND:
		return (n2cp_suspend(n2cp));
	case DDI_DETACH:
		break;
	default:
		return (DDI_FAILURE);
	}

	/* kCF unregistration here */
	if (n2cp_uninit(n2cp) != DDI_SUCCESS) {
		return (DDI_FAILURE);
	}

	(void) n2cp_dr_fini();

	/*
	 * shutdown device
	 */
	n2cp_deallocate(n2cp, instance);

	return (DDI_SUCCESS);
}

/*ARGSUSED*/
static int
n2cp_suspend(n2cp_t *n2cp)
{
	return (DDI_SUCCESS);
}

/*ARGSUSED*/
static int
n2cp_resume(n2cp_t *n2cp)
{
	return (DDI_SUCCESS);
}

static int
cpu_event_handler(cpu_setup_t what, int cid, void *arg)
{
	n2cp_t	*n2cp = (n2cp_t *)arg;

	ASSERT(MUTEX_HELD(&cpu_lock));

	/* block all other DR requests */
	mutex_enter(&n2cp->n_dr_lock);

	switch (what) {
	case CPU_CONFIG:
		break;
	case CPU_ON:
	case CPU_INIT:
	case CPU_CPUPART_IN:
		DBG2(n2cp, DCHATTY, "cpu_event_handler CID[%d]: event[%d] "
		    "Online CPU", cid, what);
		MAP_MUTEXES_ENTER_ALL(n2cp->n_cwqmap);
		n2cp_online_cpu(n2cp, cid);
		MAP_MUTEXES_EXIT_ALL(n2cp->n_cwqmap);
		break;
	case CPU_UNCONFIG:
	case CPU_OFF:
	case CPU_CPUPART_OUT:
		DBG2(n2cp, DCHATTY, "cpu_event_handler CID[%d]: event[%d] "
		    "Offline CPU", cid, what);
		MAP_MUTEXES_ENTER_ALL(n2cp->n_cwqmap);
		n2cp_offline_cpu(n2cp, cid);
		if (what == CPU_UNCONFIG) {
			/* cpu removed - disassociate the cwq */
			n2cp->n_cwqmap.m_cpulist[cid].mc_cwqid = -1;
		}
		MAP_MUTEXES_EXIT_ALL(n2cp->n_cwqmap);
		break;
	default:
		break;
	}
	mutex_exit(&n2cp->n_dr_lock);
	return (0);
}

/*
 * n2cp_calibrate_spin()
 *
 * calculate how many spins per usec - needed for timeout calculations
 * if n2cp is running in synchronous mode.
 * Return spin_per_usec. If the measured delay is too long or too short,
 * return the default value.
 */
static int
n2cp_calibrate_spin()
{
#define	SPIN_CYCLE	100
	hrtime_t	start, end, nsec;
	int		spu, i;

	thread_affinity_set(curthread, CPU_CURRENT);
	start = gethrtime();
	for (i = 0; i < SPIN_CYCLE; i++) {
		n2cp_delay(10);
	}
	end = gethrtime();
	nsec = (end - start) / SPIN_CYCLE;
	thread_affinity_clear(curthread);

	if ((nsec == 0) || (nsec >= 1000)) {
		/* If nsec = 0 or greater than 1000, use the default count */
		DBG1(NULL, DWARN, "spin time(%d) is zero or greater than "
		    "than 1 usec ", nsec);
		return (N2CP_DEFAULT_SPINS);
	} else {
		spu = 1000 / nsec;
		if ((spu * nsec) % 1000) {
			/* be conservative insure spin is >= 1 usec */
			spu++;
		}
		DBG2(NULL, DCHATTY, "spin time = %d nsecs, spins = %d",
		    nsec, spu);
		return (spu);
	}
}


static n2cp_t *
n2cp_allocate(dev_info_t *dip, int instance)
{
	int			rv;
	n2cp_t			*n2cp;
	n2cp_cwq2cpu_map_t	*c2cp;

	/*
	 * Allocate and initialize n2cp_t struct for this instance.
	 */
	rv = ddi_soft_state_zalloc(n2cp_softstate, instance);
	if (rv != DDI_SUCCESS) {
		n2cp_diperror(dip, "unable to allocate soft state");
		return (NULL);
	}
	n2cp = (n2cp_t *)ddi_get_soft_state(n2cp_softstate, instance);
	ASSERT(n2cp != NULL);
	n2cp->n_dip = dip;
	DBG1(n2cp, DATTACH, "n2cp_allocate: n2cp = %p", n2cp);

	if (n2cp_init_ncs_api(n2cp) != 0) {
		n2cp_diperror(dip, "hsvc_register() failed");
		ddi_soft_state_free(n2cp_softstate, instance);
		return (NULL);
	}

	mutex_init(&n2cp->n_dr_lock, NULL, MUTEX_DRIVER, NULL);

	/* get fips configuration : FALSE by default */
	n2cp->n_is_fips = ddi_getprop(DDI_DEV_T_ANY, dip,
	    DDI_PROP_DONTPASS
	    | DDI_PROP_CANSLEEP
	    | DDI_PROP_NOTPROM,
	    N2CP_FIPS_STRING,
	    FALSE);

	/* calculate how many spins per usec */
	n2cp->n_spins_per_usec = n2cp_calibrate_spin();

	/* determine number of concurrent spinners per core  */
	n2cp_sync_threads = ddi_getprop(DDI_DEV_T_ANY, dip,
	    DDI_PROP_DONTPASS
	    | DDI_PROP_CANSLEEP
	    | DDI_PROP_NOTPROM,
	    N2CP_SYNC_STRING,
	    N2CP_MAX_SYNC_THREADS);

	/* get userland CWQ usage on RF(a.k.a. KT) processors */
	if (is_KT(n2cp)) {
		n2cp_use_ulcwq = ddi_getprop(DDI_DEV_T_ANY, dip,
		    DDI_PROP_DONTPASS
		    | DDI_PROP_CANSLEEP
		    | DDI_PROP_NOTPROM,
		    N2CP_USE_ULCWQ_STRING,
		    N2CP_USE_ULCWQ);
	}

	if (n2cp_use_ulcwq && (n2cp->n_hvapi_minor_version < 1)) {
		n2cp_diperror(dip,
		    "not able to negotiate sufficient hv version for ulcwq");
		ddi_soft_state_free(n2cp_softstate, instance);
		return (NULL);
	}

	/*
	 * Find out where the CWQs are and get the
	 * CPU-2-CWQ mapping.
	 */
	n2cp_alloc_cwq2cpu_map(n2cp);
	c2cp = &n2cp->n_cwqmap;
	MAP_MUTEXES_ENTER_ALL(n2cp->n_cwqmap);
	if (n2cp_update_cwq2cpu_map(n2cp) < 0) {
		MAP_MUTEXES_EXIT_ALL(n2cp->n_cwqmap);
		n2cp_deallocate(n2cp, instance);
		return (NULL);
	}
	MAP_MUTEXES_EXIT_ALL(n2cp->n_cwqmap);

	/* register the callback function for CPU events */
	if (!n2cp_iscpuregistered(n2cp)) {
		mutex_enter(&cpu_lock);
		register_cpu_setup_func(cpu_event_handler, n2cp);
		n2cp_setcpuregistered(n2cp);
		mutex_exit(&cpu_lock);
	}

	if (n2cp_use_ulcwq) {
		n2cp->n_cwq_nentries = CWQ_UL_NENTRIES;
	} else {
		n2cp->n_cwq_nentries = CWQ_NENTRIES;
	}

	if (N2CP_ERRATUM_175_ENABLED()) {
		noncache_contig_mem_init();
	}

	rv = n2cp_alloc_kmem_caches(n2cp);

	if (rv != DDI_SUCCESS) {
		n2cp_deallocate(n2cp, instance);
		return (NULL);
	}

	n2cp->n_taskq = ddi_taskq_create(n2cp->n_dip,
	    "n2cp_taskq", 1, TASKQ_DEFAULTPRI, 0);

	if (n2cp->n_taskq == NULL) {
		n2cp_deallocate(n2cp, instance);
		return (NULL);
	}

	if (c2cp->m_ncwqs_online) {
		rv = n2cp_cwq_q_init(n2cp);
		if (rv != DDI_SUCCESS) {
			n2cp_deallocate(n2cp, instance);
			return (NULL);
		}
	}

	return (n2cp);
}

static void
n2cp_deallocate(n2cp_t *n2cp, int instance)
{
	if (n2cp == NULL)
		return;

	n2cp_cwq_q_deinit(n2cp);

	if (n2cp->n_taskq != NULL) {
		ddi_taskq_destroy(n2cp->n_taskq);
	}

	n2cp_destroy_kmem_caches(n2cp);

	if (N2CP_ERRATUM_175_ENABLED()) {
		noncache_contig_mem_deinit();
	}

	/* unregister the callback function for CPU events */
	if (n2cp_iscpuregistered(n2cp)) {
		mutex_enter(&cpu_lock);
		unregister_cpu_setup_func(cpu_event_handler, n2cp);
		n2cp_clrcpuregistered(n2cp);
		mutex_exit(&cpu_lock);
	}

	n2cp_deinit_cwq2cpu_map(n2cp);

	n2cp_unregister_ncs_api();

	mutex_destroy(&n2cp->n_dr_lock);
	ddi_soft_state_free(n2cp_softstate, instance);
}

static int
n2cp_cwq_q_init(n2cp_t *n2cp)
{
	int	rv = CRYPTO_FAILED;
	int	i;
	int	cid;
	cwq_t	*cwq;
	int	cnt = 0;

	MAP_MUTEXES_ENTER_ALL(n2cp->n_cwqmap);
	for (i = 0; i < n2cp->n_cwqmap.m_ncwqs; i++) {
		cwq = &n2cp->n_cwqmap.m_cwqlist[i].mm_queue;

		bzero(cwq, sizeof (cwq_t));

		cwq->cq_n2cp = n2cp;

		/* bind to the core and configure the CWQ */
		cid = n2cp_map_cwq_to_cpu(n2cp, i, 0);
		if (cid >= 0) {
			thread_affinity_set(curthread, cid);
			rv = n2cp_cwq_q_configure(n2cp, cwq, i);
			thread_affinity_clear(curthread);
			DBG3(n2cp, DATTACH,
			    "q-config: cwq-id (%d) handle = 0x%llx (%s)",
			    i, cwq->cq_handle, rv ? "ERR" : "OK");
			if (rv) {
				DBG2(n2cp, DWARN,
				    "Unable to configure cwq %d (%d)", i, rv);
				continue;
			} else {
				ASSERT((n2cp->n_hvapi_major_version >= 2) ?
				    (cwq->cq_handle > 0) : 1);
				cnt++;
			}
		}

		mutex_init(&cwq->cq_lock, NULL, MUTEX_DRIVER, NULL);
		mutex_init(&cwq->cq_hv_lock, NULL, MUTEX_DRIVER, NULL);
		mutex_init(&cwq->cq_head_lock, NULL, MUTEX_DRIVER, NULL);
		mutex_init(&cwq->cq_job_lock, NULL, MUTEX_DRIVER, NULL);
		cv_init(&cwq->cq_busy_cv, NULL, CV_DRIVER, NULL);
	}
	MAP_MUTEXES_EXIT_ALL(n2cp->n_cwqmap);

	if (!cnt) {
		return (rv);
	} else {
		return (0);
	}
}

static void
n2cp_cwq_q_deinit(n2cp_t *n2cp)
{
	int	i;
	cwq_t	*cwq;

	MAP_MUTEXES_ENTER_ALL(n2cp->n_cwqmap);
	for (i = 0; i < n2cp->n_cwqmap.m_ncwqs; i++) {
		cwq = &n2cp->n_cwqmap.m_cwqlist[i].mm_queue;

		if (cwq->cq_init != 0) {
			int cid;
			cid = n2cp_map_cwq_to_cpu(n2cp, cwq->cq_id, 0);
			if (cid < 0) {
				DBG1(n2cp, DWARN, "n2cp_cwq_q_deinit(qid[%d]) "
				    " failed to get a cpu\n", cwq->cq_id);
				break;
			}

			thread_affinity_set(curthread, cid);
			n2cp_cwq_q_unconfigure(n2cp, cwq);
			thread_affinity_clear(curthread);

			mutex_destroy(&cwq->cq_lock);
			mutex_destroy(&cwq->cq_hv_lock);
			mutex_destroy(&cwq->cq_head_lock);
			mutex_destroy(&cwq->cq_job_lock);
			cv_destroy(&cwq->cq_busy_cv);
		}
	}
	MAP_MUTEXES_EXIT_ALL(n2cp->n_cwqmap);
}

void
get_ulcwqbuf_array_pars(cwq_t *cwq, int *nbufs, n2cp_ulcwq_buf_t **ulcwqbuf)
{
	*nbufs = (cwq->cq_ulqpagesize -
	    (cwq->cq_ulqsize * sizeof (cwq_cw_t))) /
	    sizeof (n2cp_ulcwq_buf_t);
	/* LINTED E_BAD_PTR_CAST_ALIGN */
	*ulcwqbuf = (n2cp_ulcwq_buf_t *)(((char *)(cwq->cq_ulqpage)) +
	    cwq->cq_ulqsize * sizeof (cwq_cw_t));
}


/*
 * Initialize CWQ hardware for respective Control Word Queue.
 * Assumes we are bound to a CPU on the respective core that
 * contains the CWQ we wish to initialize.
 * Caller of this function must be bound to the CWQ.
 * Caller must hold mm_lock.
 */
int
n2cp_cwq_q_configure_work(n2cp_t *n2cp, cwq_t *cwq, int cwq_id)
{
	int		err = 0;
	uint64_t	qhandle;
	uint64_t	devino;
	uint64_t	hverr = H_EOK;
	uint64_t	cwqlen = n2cp->n_cwq_nentries * sizeof (cwq_cw_t);
	uint64_t	qstart, qend;

	if (cwq->cq_init == 1) {
		/* it is already configured */
		return (0);
	}

	cwq->cq_n2cp = n2cp;
	cwq->cq_id = cwq_id;

	if (n2cp_use_ulcwq) {
		uint64_t xxx[4];
		n2cp_ulcwq_buf_t	*ulcwqbuf;
		int	nbufs, j;

		/* LINTED E_ASSIGN_NARROW_CONV */
		cwq->cq_ulqpagesize = contig_mem_slab_size;
		cwq->cq_ulqpagesize_hv = CWQ_UL_PAGESIZE_4M;
		cwq->cq_ulqpage = contig_mem_alloc_align(cwq->cq_ulqpagesize,
		    cwq->cq_ulqpagesize);

		if (cwq->cq_ulqpage == NULL) {
			cwq->cq_ulqpagesize = 0;
			cwq->cq_ulqpagesize_hv = 0;
			n2cp_cwq_q_unconfigure(n2cp, cwq);
			return (ENOMEM);
		}

		cwq->cq_ulqpage_pa = va_to_pa(cwq->cq_ulqpage);
		cwq->cq_ulqsize = CWQ_UL_NENTRIES;

		hverr = hv_ncs_ul_cwqconf(cwq->cq_ulqpage_pa,
		    cwq->cq_ulqpagesize_hv, cwq->cq_ulqsize, xxx);
		qhandle = xxx[0];

		if (hverr != H_EOK) {
			err = n2cp_herr2kerr(hverr);
			n2cp_cwq_q_unconfigure(n2cp, cwq);
			return (err);
		}

		get_ulcwqbuf_array_pars(cwq, &nbufs, &ulcwqbuf);

		mutex_init(&cwq->cq_ulcwq_buf_lock, NULL, MUTEX_DRIVER, NULL);

		mutex_enter(&cwq->cq_ulcwq_buf_lock);
		n2cp_initq(&cwq->cq_reqs_waiting_for_ulcwq_buf);
		n2cp_initq(&cwq->cq_ulcwq_freebufs);
		n2cp_initq(&cwq->cq_ulcwq_unusedbufs);

		for (j = 0; j < nbufs; j++) {
			n2cp_enqueue(&cwq->cq_ulcwq_freebufs,
			    (n2cp_listnode_t *)(&(ulcwqbuf[j])));
			n2cp_initq(&(ulcwqbuf[j].ub_unused_link));
			ulcwqbuf[j].ub_buf_paddr =
			    (uint64_t)(&(ulcwqbuf[j]));
			ulcwqbuf[j].ub_req = NULL;
		}
		mutex_exit(&cwq->cq_ulcwq_buf_lock);
	} else {
		cwq->cq_ulqpagesize = 0;
		cwq->cq_ulqpagesize_hv = 0;
		cwq->cq_ulqpage = NULL;
		cwq->cq_ulqpage_pa = 0;
	}

	cwq->cq_jobs_size = n2cp->n_cwq_nentries * sizeof (n2cp_request_t *);
	cwq->cq_jobs = kmem_zalloc(cwq->cq_jobs_size, KM_SLEEP);
	cwq->cq_qmemsize = (sizeof (cwq_cw_t) * n2cp->n_cwq_nentries);

	if (n2cp_use_ulcwq) {
		cwq->cq_qmem = cwq->cq_ulqpage;
	} else {
		cwq->cq_qmem = contig_mem_alloc_align(cwq->cq_qmemsize,
		    MAX(CWQ_ALIGNMENT, cwq->cq_qmemsize));
		if (cwq->cq_qmem == NULL) {
			cmn_err(CE_WARN, "n2cp_cwq_q_configure: "
			    "contig_mem_alloc_align(%d) failed",
			    cwq->cq_qmemsize);
			n2cp_cwq_q_unconfigure(n2cp, cwq);
			return (ENOMEM);
		}
	}

	ASSERT(((uint64_t)cwq->cq_qmem & (CWQ_ALIGNMENT - 1)) == 0);
	cwq->cq_first = (cwq_cw_t *)cwq->cq_qmem;
	cwq->cq_last = cwq->cq_first + n2cp->n_cwq_nentries - 1;
	cwq->cq_head = cwq->cq_tail = cwq->cq_first;

	n2cp_initq(&cwq->cq_joblist);

	qstart = va_to_pa(cwq->cq_first);
	qend = va_to_pa(cwq->cq_last) + sizeof (cwq_cw_t);
	if ((qend - qstart) != cwqlen) {
		cmn_err(CE_WARN, "n2cp_cwq_q_configure: CWQ not contig "
		    "(vf=%p, vl=%p, qs=%p, qe=%p, ql=%ld)",
		    (void *)cwq->cq_first, (void *)cwq->cq_last,
		    (void *)qstart, (void *)qend, cwqlen);

		n2cp_cwq_q_unconfigure(n2cp, cwq);

		return (EINVAL);
	}

	if (!n2cp_use_ulcwq) {
		qhandle = 0;
		hverr = hv_ncs_qconf(NCS_QTYPE_CWQ,
		    qstart, n2cp->n_cwq_nentries, &qhandle);
		if (hverr != H_EOK) {
			n2cp_diperror(n2cp->n_dip, "hv ncs q-conf failed! "
			    "(rv = %ld)", hverr);
			err = n2cp_herr2kerr(hverr);

			n2cp_cwq_q_unconfigure(n2cp, cwq);

			return (err);
		}
	}

	ASSERT(qhandle > 0);

	devino = 0;
	hverr = hv_ncs_qhandle_to_devino(qhandle, &devino);
	if (hverr != H_EOK) {
		n2cp_diperror(n2cp->n_dip, "hv_ncs_qhandle_to_devino() failed! "
		    "(rv = %ld)", hverr);
		err = n2cp_herr2kerr(hverr);

		n2cp_cwq_q_unconfigure(n2cp, cwq);

		return (err);
	}

	if (devino == 0) {
		n2cp_diperror(n2cp->n_dip, "devino is NULL");

		n2cp_cwq_q_unconfigure(n2cp, cwq);

		return (EINVAL);
	}

	/*
	 * On successful queue-configuration the HV will return the
	 * CWQ handle.
	 */
	cwq->cq_handle = qhandle;
	cwq->cq_init = 1;

	return (0);
}


static void
cwq_q_configure_task(void *arg)
{
	n2cp_configure_params_t	*conf_pars = (n2cp_configure_params_t *)arg;

	mutex_enter(&conf_pars->taskq_lock);
	thread_affinity_set(curthread, conf_pars->cid);
	conf_pars->rv = n2cp_cwq_q_configure_work(conf_pars->n2cp,
	    conf_pars->cwq, conf_pars->cwq_id);
	cv_signal(&conf_pars->taskq_cv);
	thread_affinity_clear(curthread);
	mutex_exit(&conf_pars->taskq_lock);
}


int
n2cp_cwq_q_configure(n2cp_t *n2cp, cwq_t *cwq, int cwq_id)
{
	n2cp_configure_params_t	conf_pars;

	conf_pars.n2cp = n2cp;
	conf_pars.cwq = cwq;
	conf_pars.cwq_id = cwq_id;
	conf_pars.cid = CPU->cpu_id;

	mutex_init(&conf_pars.taskq_lock, NULL, MUTEX_DRIVER, NULL);
	cv_init(&conf_pars.taskq_cv, NULL, CV_DRIVER, NULL);
	mutex_enter(&conf_pars.taskq_lock);
	if (ddi_taskq_dispatch(n2cp->n_taskq, cwq_q_configure_task,
	    (void *)(&conf_pars), DDI_SLEEP) == DDI_SUCCESS) {
		cv_wait(&conf_pars.taskq_cv, &conf_pars.taskq_lock);
	} else {
		conf_pars.rv = EFAULT;
	}
	mutex_exit(&conf_pars.taskq_lock);
	cv_destroy(&conf_pars.taskq_cv);
	mutex_destroy(&conf_pars.taskq_lock);
	return (conf_pars.rv);
}

/*
 * Caller of this function must be bound to the CWQ about to unconfigure
 * Caller must hold mm_lock.
 */
void
n2cp_cwq_q_unconfigure(n2cp_t *n2cp, cwq_t *cwq)
{
	cwq->cq_init = 0;

	if (n2cp_use_ulcwq) {
		mutex_enter(&cwq->cq_ulcwq_buf_lock);
		n2cp_initq(&cwq->cq_reqs_waiting_for_ulcwq_buf);
		n2cp_initq(&cwq->cq_ulcwq_freebufs);
		n2cp_initq(&cwq->cq_ulcwq_unusedbufs);
		mutex_exit(&cwq->cq_ulcwq_buf_lock);
		mutex_destroy(&cwq->cq_ulcwq_buf_lock);
	}

	if (cwq->cq_handle != NULL) {
		DBG2(n2cp, DATTACH, "q-unconfig: cwq-id (%d) handle = 0x%llx ",
		    cwq->cq_id, cwq->cq_handle);
		(void) hv_ncs_qconf(NCS_QTYPE_CWQ, cwq->cq_handle, 0, NULL);
	}

	if ((cwq->cq_ulqpage == NULL) && (cwq->cq_qmem != NULL)) {
		contig_mem_free(cwq->cq_qmem, cwq->cq_qmemsize);
		cwq->cq_qmem = NULL;
	}

	if (cwq->cq_ulqpage != NULL) {
		contig_mem_free(cwq->cq_ulqpage, cwq->cq_ulqpagesize);
		cwq->cq_ulqpage = NULL;
		cwq->cq_qmem = NULL;
	}

	if (cwq->cq_jobs != NULL) {
		kmem_free(cwq->cq_jobs, cwq->cq_jobs_size);
		cwq->cq_jobs = NULL;
	}
}


static void
n2cp_cwq_addjob(cwq_t *cwq, n2cp_request_t *reqp, int id)
{
	ASSERT(CWQ_QINDEX_IS_VALID(cwq->cq_n2cp, id));
	ASSERT(cwq->cq_jobs[id] == NULL);
	ASSERT(cwq->cq_id == reqp->nr_cwq_id);

	reqp->nr_id = id;
	reqp->nr_job_state = N2CP_JOBSTATE_PENDING;

	mutex_enter(&cwq->cq_job_lock);
	cwq->cq_jobs[id] = reqp;

	/* check if we have full allotment of spinners */
	if (cwq->cq_sync_threads < n2cp_sync_threads) {
		/* do not make this request block/sleep */
		reqp->nr_flags |= N2CP_SYNC_SPIN;
		cwq->cq_sync_threads++;
	}

	n2cp_enqueue(&cwq->cq_joblist, (n2cp_listnode_t *)reqp);
	mutex_exit(&cwq->cq_job_lock);
}

static void
n2cp_cwq_deljob(cwq_t *cwq, n2cp_request_t *reqp)
{
	/* caller must hold job lock */
	ASSERT(MUTEX_HELD(&cwq->cq_job_lock));

	n2cp_rmqueue((n2cp_listnode_t *)reqp);

	if ((reqp->nr_job_state == N2CP_JOBSTATE_PENDING) &&
	    (cwq->cq_init)) {
		cwq->cq_jobs[reqp->nr_id] = NULL;
	}

	reqp->nr_job_state = N2CP_JOBSTATE_SOLO;

	mutex_enter(&reqp->nr_sync_lock);
	if (reqp->nr_flags & N2CP_SYNC_SPIN) {
		reqp->nr_flags &= ~N2CP_SYNC_SPIN;
		cwq->cq_sync_threads--;
	}
	mutex_exit(&reqp->nr_sync_lock);
}

/*
 * n2cp_wake_next_job()
 *
 * wake the next job on the runq - only used for sync mode
 *
 */
static void
n2cp_wake_next_job(cwq_t *cwq)
{
	n2cp_request_t  *next;

	mutex_enter(&cwq->cq_job_lock);

	/* see if we already have the full allotment of spinning threads */
	if (cwq->cq_sync_threads >= n2cp_sync_threads) {
		mutex_exit(&cwq->cq_job_lock);
		return;
	}

	/* find the next pending job */
	next = (n2cp_request_t *)cwq->cq_joblist.nl_next;

	while (next != (n2cp_request_t *)&cwq->cq_joblist) {

		mutex_enter(&next->nr_sync_lock);

		/* find next job not already spinning */
		if (next->nr_flags & N2CP_SYNC_SPIN) {
			mutex_exit(&next->nr_sync_lock);
			next = (n2cp_request_t *)next->nr_linkage.nl_next;
		} else {
			/* mark this one  to not block */
			next->nr_flags |= N2CP_SYNC_SPIN;

			cwq->cq_sync_threads++;

			/* don't bother signaling if job's not waiting */
			if (next->nr_flags & N2CP_SYNC_WAIT)
				cv_signal(&next->nr_sync_cv);
			mutex_exit(&next->nr_sync_lock);
			break;
		}
	}
	mutex_exit(&cwq->cq_job_lock);

	/* check for queue full condition and wake up waiting threads */
	if (cwq->cq_busy_wait > 0) {
		mutex_enter(&cwq->cq_lock);
		cv_broadcast(&cwq->cq_busy_cv);
		mutex_exit(&cwq->cq_lock);
	}
}

/*
 * n2cp_sync_job_error()
 *
 * sync mode error handler
 *
 */
static int
n2cp_sync_job_error(cwq_t *cwq, n2cp_request_t *reqp)
{

	n2cp_sync_update_head(cwq);

	if (reqp->nr_job_state == N2CP_JOBSTATE_PENDING) {
		mutex_enter(&cwq->cq_job_lock);
		n2cp_cwq_deljob(cwq, reqp);
		mutex_exit(&cwq->cq_job_lock);
	}
	n2cp_wake_next_job(cwq);

	/* process this job */
	reqp->nr_errno = CRYPTO_DEVICE_ERROR;

	atomic_dec_64(&cwq->cq_ks.qks_currjobs);

	/* update error job counter */
	atomic_inc_64(&cwq->cq_ks.qks_nsync_err);

	return (reqp->nr_errno);
}



/*
 * n2cp_sync_update_head()
 *
 * update the head pointer based on completed jobs.
 * mark all completed jobs and wake any sleeping threads.
 *
 */
static void
n2cp_sync_update_head(cwq_t *cwq)
{
	int		hvret;
	int		idx;
	int		done_idx = -1;
	int		new_head_idx;
	uint64_t	new_head_offset;
	cwq_cw_t	*cw;
	n2cp_request_t	*nreqp;

	/* only one thread can update the head at a time */
	mutex_enter(&cwq->cq_head_lock);

	/* get the new head index */
	hvret = hv_ncs_gethead(cwq->cq_handle, &new_head_offset);

	if (hvret != H_EOK) {
		DBG2(cwq->cq_n2cp, DWARN,
		    "n2cp_sync_update_head (%d) gethead failed (hv = %ld)",
		    cwq->cq_id, hvret);
		mutex_exit(&cwq->cq_head_lock);
		return;
	}

	new_head_idx = CWQ_QOFFSET_TO_QINDEX(new_head_offset);

	/* make sure index is valid */
	if (!CWQ_QINDEX_IS_VALID(cwq->cq_n2cp, new_head_idx)) {
		DBG2(cwq->cq_n2cp, DWARN,
		    "n2cp_sync_update_head: (ID %d) new_head_idx (%d) "
		    "is INVALID", cwq->cq_id, new_head_idx);
		mutex_exit(&cwq->cq_head_lock);
		return;
	}

	/* get the old head value */
	idx = cwq->cq_head - cwq->cq_first;
	cw = cwq->cq_head;

	/* march from old head to new, looking for all completed jobs */
	while (idx != new_head_idx) {
		/* has this one completed? */
		if (cw->cw_resv1 != 0) {
			mutex_enter(&cwq->cq_job_lock);
			if ((nreqp = cwq->cq_jobs[idx]) != NULL) {

				/* remove this one from the runq */
				n2cp_cwq_deljob(cwq, nreqp);

				mutex_enter(&nreqp->nr_sync_lock);
				/* save the status */
				nreqp->nr_csr =
				    ((cw->cw_resv1 << 1) & CWQ_ERR_MASK);

				/* mark job as complete */
				nreqp->nr_flags |= (N2CP_DONE | N2CP_NO_WAIT);

				/* wake up that thread if its sleeping */
				if (nreqp->nr_flags & N2CP_SYNC_WAIT) {
					cv_signal(&nreqp->nr_sync_cv);
				}

				mutex_exit(&nreqp->nr_sync_lock);

				done_idx = idx;
			}
			mutex_exit(&cwq->cq_job_lock);
		}
		/* move on to the next control word */
		if (cw == cwq->cq_last) {
			cw = cwq->cq_first;
		} else {
			cw++;
		}

		idx = CWQ_QINDEX_INCR(cwq->cq_n2cp, idx);
	}

	/* did we find some completions? */
	if (done_idx != -1) {

		/* update the head index */
		idx = CWQ_QINDEX_INCR(cwq->cq_n2cp, done_idx);

		/* use atomic since n2cp_start will read head */
		(void) atomic_swap_64((volatile uint64_t *)&cwq->cq_head,
		    (uint64_t)(cwq->cq_first + idx));
	}
	mutex_exit(&cwq->cq_head_lock);
}




/*
 * n2cp_sync_process()
 *
 * synchronous operation processing
 * we will spin for one job and cv_wait
 * for all others.
 *
 * Caller must hold m_lock.
 */
static int
n2cp_sync_process(cwq_t	*cwq, n2cp_request_t *reqp, cwq_cw_t *cw,
		cwq_entry_t *cep)
{
	n2cp_t		*n2cp = (n2cp_t *)cwq->cq_n2cp;
	clock_t		timeout = reqp->nr_timeout;

	mutex_enter(&reqp->nr_sync_lock);

	/* another thread already spinning - wait. */
	while ((reqp->nr_flags & (N2CP_NO_WAIT | N2CP_SYNC_SPIN)) == 0) {
		reqp->nr_flags |= N2CP_SYNC_WAIT;
		cv_wait(&reqp->nr_sync_cv, &reqp->nr_sync_lock);
		reqp->nr_flags &= ~N2CP_SYNC_WAIT;
	}
	/* if not set to spin - must have completed */
	if ((reqp->nr_flags & N2CP_SYNC_SPIN) == 0) {
		mutex_exit(&reqp->nr_sync_lock);
		goto done;
	}
	mutex_exit(&reqp->nr_sync_lock);

	/* make sure we're still online */
	if (cep->mm_state == CWQ_STATE_OFFLINE) {
		return (n2cp_sync_job_error(cwq, reqp));
	}

	/* ok - our time to spin */
	while ((N2CP_REQ_DONE_CHECK(reqp) == 0) && (cw->cw_resv1 == 0)) {
		n2cp_delay(10);
		timeout--;
		/* check for stalled job */
		if (timeout == 0) {
			cmn_err(CE_WARN, "n2cp sync job timeout (cwq %d) "
			    " (req[%p]:nr_cwq_id %d) @ %lld", cwq->cq_id,
			    (void *)reqp, reqp->nr_cwq_id, gethrtime());

			/* offline the CWQ */
			MAP_MUTEXES_ENTER_ALL(n2cp->n_cwqmap);
			n2cp_offline_cwq(n2cp, cwq->cq_id);
			MAP_MUTEXES_EXIT_ALL(n2cp->n_cwqmap);

			n2cp_diperror(n2cp->n_dip,
			    "cwq %d stalled and was taken offline", cwq->cq_id);
			return (n2cp_sync_job_error(cwq, reqp));
		}
	}

	/* update the head - wake up all completed threads */
	n2cp_sync_update_head(cwq);

	/* wake up pending jobs */
	n2cp_wake_next_job(cwq);

done:

	atomic_dec_64(&cwq->cq_ks.qks_currjobs);

	reqp->nr_flags &= ~(N2CP_DONE | N2CP_NO_WAIT | N2CP_SYNC_WAIT);

	reqp->nr_errno = n2cp_check_errors(n2cp, cwq->cq_id, reqp->nr_csr);

	/* update completed job counter */
	atomic_inc_64(&cwq->cq_ks.qks_nsync_jobs);

	if (reqp->nr_errno != CRYPTO_SUCCESS) {
		/* update error job counter */
		atomic_inc_64(&cwq->cq_ks.qks_nsync_err);
	}

	/* return status to kCF */
	return (reqp->nr_errno);
}


/*
 * n2cp_find_best_cwq()
 *
 * look for the least loaded cwq.  Currently we use the
 * number of jobs as the metric for determining load.
 * The number of bytes could be a better indicator.
 * This function returns the new cwq
 *
 * Note: returns with processor affinity set.
 */
cwq_entry_t *
n2cp_find_best_cwq(n2cp_t *n2cp, cwq_entry_t *oldcep)
{
	cwq_entry_t		*best_cep = NULL;
	cwq_t			*best_cwq = NULL;
	int			i;
	int			cid;
	uint64_t		best_jc;
	n2cp_cwq2cpu_map_t	*c2cp = &n2cp->n_cwqmap;
	int			index_to_skip[N2CP_MAX_NCWQS];
	int			num_index_to_skip = 0;
	int			curr_best_index = -1;


restart:
	best_cep = NULL;
	best_cwq = NULL;
	best_jc = n2cp->n_cwq_nentries + 1;
	for (i = 0; i < c2cp->m_ncwqs; i++) {
		cwq_t	*cwq;
		cwq_entry_t	*this_cep;
		/*
		 * Check if cwq is on the skip list which contains
		 * cwq from previous run. If it is, skip it.
		 */
		if (is_in_list(index_to_skip, num_index_to_skip, i)) {
			continue;
		}

		if (c2cp->m_cwqlist[i].mm_state != CWQ_STATE_ONLINE) {
			continue;
		}

		this_cep =  &c2cp->m_cwqlist[i];
		if (this_cep == NULL) {
			continue;
		}
		cwq = &c2cp->m_cwqlist[i].mm_queue;

		if (cwq == NULL) {
			continue;
		}

		/* mark 1st cwq as best for now */
		if (best_cep == NULL) {
			best_cwq = cwq;
			best_jc = cwq->cq_ks.qks_currjobs;
			best_cep = this_cep;
			curr_best_index = i;
			continue;
		}

		/* is this the best so far? */
		if (cwq->cq_ks.qks_currjobs < best_jc) {
			best_cwq = cwq;
			best_jc = cwq->cq_ks.qks_currjobs;
			best_cep = this_cep;
			curr_best_index = i;
		}
	}

	/* did we find a cwq */
	if (best_cep) {
		if ((best_cep = n2cp_holdcwq(n2cp, best_cwq->cq_id, oldcep))
		    != NULL) {
			cid = n2cp_map_cwq_to_cpu(n2cp, best_cwq->cq_id, 0);
			/* failed to find a cpu - restart search */
			if ((cid == -1) && (best_cep != oldcep)) {
				n2cp_relecwq(best_cep);
				/* skip this cwq in next cwq selection */
				index_to_skip[num_index_to_skip] =
				    curr_best_index;
				num_index_to_skip++;

				goto restart;
			}
			/* bind to the new CPU */
			thread_affinity_set(curthread, cid);
		} else {
			/* failed to hold the cwq - restart search */
			goto restart;
		}
	}

	return (best_cep);
}

static void
copy_cw(cwq_cw_t *sp, cwq_cw_t *dp, int cnt)
{
	int	x;
	for (x = 0; x < cnt; x++) {
		dp[x].cw_ctlbits = sp[x].cw_ctlbits;
		dp[x].cw_src_addr = sp[x].cw_src_addr;
		dp[x].cw_auth_key_addr = sp[x].cw_auth_key_addr;
		dp[x].cw_auth_iv_addr = sp[x].cw_auth_iv_addr;
		dp[x].cw_final_auth_state_addr =
		    sp[x].cw_final_auth_state_addr;
		dp[x].cw_enc_key_addr = sp[x].cw_enc_key_addr;
		dp[x].cw_enc_iv_addr = sp[x].cw_enc_iv_addr;
		dp[x].cw_dst_addr = sp[x].cw_dst_addr;
	}
}


int n2cp_move_req_cnt = 0;

int
n2cp_find_cep_for_req(n2cp_request_t *reqp)
{
	int			rv;
	cwq_entry_t		*cep, *oldcep;
	n2cp_t			*n2cp = reqp->nr_n2cp;
	nr_ctx_t		context;
	cwq_t			*newcwq;
	int			cid;
	n2cp_lock_withpad_t	*mp;
	n2cp_listnode_t		*link;
	boolean_t		is_fips_post = N2CP_FIPS_POST_RUNNING;

	ASSERT(n2cp_use_ulcwq != 0);

	cid = CPU->cpu_id;
	mp = &n2cp->n_cwqmap.m_locklist[cid];
	if (!is_fips_post) {
		/*
		 * All CPU mutexes (cwqmap.m_locklist[]) are held during the
		 * FIPS POST execution, so no need to grab here.
		 */
		mutex_enter(&mp->lock);
	}

	cep = reqp->nr_cep;
	if (cep != NULL) {
		mutex_enter(&(cep->mm_queue.cq_ulcwq_buf_lock));
		if (reqp->nr_cep != NULL) {
			/* remove buffer from the unused list */
			link = &(reqp->nr_ulcwq_buf->ub_unused_link);
			(void) n2cp_rmqueue(link);
		}
		mutex_exit(&(cep->mm_queue.cq_ulcwq_buf_lock));
	}

	if (reqp->nr_cep == NULL) {
		if (reqp->nr_context == NULL) {
			/* we lost the context in n2cp_move_req_off_page() */
			if (!is_fips_post) {
				mutex_exit(&mp->lock);
			}
			return (CRYPTO_DEVICE_ERROR);
		}
		context = *(reqp->nr_context);
		kmem_free(reqp->nr_context, sizeof (nr_ctx_t));
		reqp->nr_context = NULL;
	}

tryagain:
	oldcep = reqp->nr_cep;

	rv = n2cp_find_cep(n2cp, &cep, oldcep, cid);
	if (rv != CRYPTO_SUCCESS) {
		if (!is_fips_post) {
			mutex_exit(&mp->lock);
		}
		return (rv);
	}
	if (cep != oldcep) {
		/*
		 * this counter is just for getting an idea how
		 * frequently this function is called, no need to
		 * slow things down by locking for a perfect count
		 */
		n2cp_move_req_cnt++;

		newcwq =  &cep->mm_queue;
		if (reqp->nr_cep != NULL) {
			context = *(reqp->nr_context);
			n2cp_ulcwq_freebuf(reqp);
		}

		reqp->nr_cep = cep;
		if (n2cp_ulcwq_getbuf(reqp, newcwq) == NULL) {
			/*
			 * this is where nr_cep becomes NULL,
			 * when we drain the CWQ
			 */
			n2cp_relecwq(cep);
			goto tryagain;
		}
		*(reqp->nr_context) = context;
	} else {
		mutex_enter(&(cep->mm_queue.cq_ulcwq_buf_lock));
		if (reqp->nr_cep != cep) {
			/* another thread took our ulcwq buffer */
			mutex_exit(&(cep->mm_queue.cq_ulcwq_buf_lock));
			goto tryagain;
		}
		mutex_exit(&(cep->mm_queue.cq_ulcwq_buf_lock));
	}

	if (!is_fips_post) {
		mutex_exit(&mp->lock);
	}

	return (rv);
}


int
n2cp_find_cep(n2cp_t *n2cp, cwq_entry_t **cep, cwq_entry_t *oldcep,
    int locked_cid)
{
	int	qid, cid, err;
	cwq_t	*cwq = NULL;
	n2cp_lock_withpad_t *mp;

	if (N2CP_FIPS_POST_RUNNING) {
		/*
		 * For the FIPS POST request, bind to the CWQ
		 * designated by 'n2cp_fips_post_qid'.
		 */
		qid = n2cp_fips_post_qid;
		*cep = n2cp_holdcwq(n2cp, qid, NULL);
		if (*cep == NULL) {
			DBG1(n2cp, DWARN, "Unable to hold CWQ[%d]", qid);
			return (CRYPTO_FAILED);
		}
		cwq = &((*cep)->mm_queue);
		cid = n2cp_map_cwq_to_cpu(n2cp, qid, 0);
		if (cid < 0) {
			DBG1(n2cp, DWARN, "No CPU found on CWQ[%d]", qid);
			n2cp_relecwq(*cep);
			return (CRYPTO_FAILED);
		}

		/*
		 * bind to the CPU: m_locklist is already held
		 */
		thread_affinity_set(curthread, cid);
	} else {
		if (locked_cid < 0) {
			/* CPU ID has not been locked */
			mp = &n2cp->n_cwqmap.m_locklist[CPU->cpu_id];
			mutex_enter(&mp->lock);
			cid = CPU->cpu_id;
		} else {
			/* CPU ID has been locked */
			cid = locked_cid;
		}

		qid = n2cp_map_cpu_to_cwq(n2cp, cid);

		if (qid >= 0) {
			*cep = n2cp_holdcwq(n2cp, qid, oldcep);
			if (*cep != NULL) {
				cwq = &((*cep)->mm_queue);
				thread_affinity_set(curthread, cid);
			}
		}

		/*
		 * No CWQ - look for another one.
		 *
		 * Note: n2cp_find_best_cwq returns with thread affinity set
		 */
		if (cwq == NULL) {
			*cep = n2cp_find_best_cwq(n2cp, oldcep);
			if (*cep == NULL) {
				DBG0(n2cp, DWARN, "No CWQ found");
				/* No CWQ found */
				if (locked_cid < 0) {
					mutex_exit(&mp->lock);
				}
				return (CRYPTO_DEVICE_ERROR);
			}
			qid = (*cep)->mm_cwqid;
			cwq = &((*cep)->mm_queue);
		}

		if (locked_cid < 0) {
			mutex_exit(&mp->lock);
		}
	}

	/* '*cep' must be set */
	ASSERT(*cep != NULL);

	/* If the CWQ is not configured, configure it now */
	if (cwq->cq_init == 0) {
		mutex_enter(&((*cep)->mm_lock));
		err = n2cp_cwq_q_configure(n2cp, cwq, qid);
		mutex_exit(&((*cep)->mm_lock));
		DBG3(n2cp, DATTACH,
		    "q-config: cwq-id (%d) handle = 0x%llx (%s)",
		    qid, cwq->cq_handle, err ? "ERR" : "OK");
		if (err) {
			DBG0(n2cp, DWARN, "q_configure failed");
			n2cp_relecwq(*cep);
			thread_affinity_clear(curthread);
			return (CRYPTO_DEVICE_ERROR);
		}
	}
	return (CRYPTO_SUCCESS);
}


int
n2cp_set_affinity_for_req(n2cp_request_t *reqp)
{
	int cid = n2cp_map_cwq_to_cpu(reqp->nr_n2cp, reqp->nr_cwq_id, 0);
	if (cid < 0) {
		return (CRYPTO_FAILED);
	}

	/*
	 * bind to the CPU: m_locklist is already held
	 */
	thread_affinity_set(curthread, cid);

	return (CRYPTO_SUCCESS);
}

int
n2cp_start(n2cp_t *n2cp, n2cp_request_t *reqp)
{
	int		err = 0;
	int		last_ix, tail_idx;
	int		qid;
	cwq_t		*cwq = NULL;
	cwq_entry_t	*cep;
	uint64_t	qoffset;
	cwq_cw_t	*old_cq_tail, *last_cw;
	cwq_cw_t	*cwbp = reqp->nr_cwb;
	int		cwcnt = reqp->nr_cwcnt;
	uint64_t	hverr = H_EOK;
	uint64_t	my_id;
	int		space, wrap;

	if (n2cp_use_ulcwq) {
		return (n2cp_ulcwq_start(n2cp, reqp));
	} else {
		err = n2cp_find_cep(n2cp, &cep, NULL, -1);
		if (err != CRYPTO_SUCCESS) {
			return (err);
		}
	}

	/* the thread affinity has been set */

	qid = cep->mm_cwqid;
	cwq = &cep->mm_queue;

	atomic_inc_64(&cwq->cq_ks.qks_currjobs);

	reqp->nr_cwq_id = cwq->cq_id;

	mutex_enter(&cwq->cq_lock);

#ifdef DEBUG
	/* check for highwater mark */
	if (cwq->cq_ks.qks_currjobs > cwq->cq_ks.qks_highwater) {
		cwq->cq_ks.qks_highwater = cwq->cq_ks.qks_currjobs;
	}
#endif /* DEBUG */

	/* check if we're busy or do not have enough room in the queue */
	if (cwq->cq_busy_wait || (cwcnt > CWQ_SLOTS_AVAIL(n2cp, cwq))) {
		clock_t	qtimeout_time, cvret;

		cwq->cq_ks.qks_qfull++;		/* kstat */

		DBG3(n2cp, DCHATTY,
		    "cwq-submit: queue (%d) full (cnt=%d, avail=%d)",
		    cwq->cq_id, cwcnt, CWQ_SLOTS_AVAIL(n2cp, cwq));

		atomic_inc_32(&cwq->cq_busy_wait);

		/*
		 * Set an absolute timeout for which we'll wait
		 * for the queue to become non-busy.
		 */
		qtimeout_time = n2cp_qtimeout_seconds * SECOND; /* usecs */
		qtimeout_time = ddi_get_lbolt() + drv_usectohz(qtimeout_time);
		err = 0;
		while (cwcnt > CWQ_SLOTS_AVAIL(n2cp, cwq)) {
			cvret = cv_timedwait_sig(&cwq->cq_busy_cv,
			    &cwq->cq_lock, qtimeout_time);
			if (cvret <= 0) {
				cwq->cq_ks.qks_qbusy++;		/* kstat */
				err = CRYPTO_CANCELED;
				break;
			}

			/* Check if cwq was taken offline while waiting */
			if (cep->mm_state == CWQ_STATE_OFFLINE) {
				DBG1(n2cp, DWARN, "cwq-submit: queue (%d) "
				    "canceled job before submission",
				    cep->mm_cwqid);
				err = CRYPTO_DEVICE_ERROR;
				break;
			}
		}
		atomic_dec_32(&cwq->cq_busy_wait);

		if (err != 0) {
			goto s_exit;
		}

	} else if (cep->mm_state == CWQ_STATE_OFFLINE) {
		/* The cwq was taken offline */
		DBG1(n2cp, DWARN, "cwq-submit: queue (%d) canceled job before "
		    "submission", cep->mm_cwqid);
		err = CRYPTO_DEVICE_ERROR;
		goto s_exit;
	}

	old_cq_tail = cwq->cq_tail;

	/* determine whether wrap is required */
	space = cwq->cq_last - cwq->cq_tail + 1;
	wrap = cwcnt - space;

	cwbp[0].cw_intr = 0;

	/* copy what we can without wrapping the queue */
	copy_cw(&cwbp[0], cwq->cq_tail, min(space, cwcnt));

	/* check whether a 2nd bcopy is necessary (wrap) */
	if (wrap > 0) {
		/* wrapped - copy remaining cw entries to front of queue */
		copy_cw(&cwbp[space], cwq->cq_first, wrap);
		cwq->cq_tail = cwq->cq_first + wrap;
		last_cw = cwq->cq_tail - 1;
	} else if (wrap < 0) {
		/* no wrap */
		cwq->cq_tail += cwcnt;
		last_cw = cwq->cq_tail - 1;
	} else {
		/* filled up through last entry - advance tail back to start */
		cwq->cq_tail = cwq->cq_first;
		last_cw = cwq->cq_last;
	}
	last_ix = last_cw - cwq->cq_first;

	tail_idx = cwq->cq_tail - cwq->cq_first;

	/* put the job into the runq */
	n2cp_cwq_addjob(cwq, reqp, last_ix);

	/* update request ID to sequence requests to hv */
	atomic_inc_64(&cwq->cq_next_id);

	/* keep track of our request ID */
	my_id = cwq->cq_next_id;

	/* update cwq kstats */
	cwq->cq_ks.qks_njobs++;
	cwq->cq_ks.qks_ncws += cwcnt;

	/*
	 * release cwq_lock before calling hypervisor.
	 * we want to avoid holding the lock because the
	 * hypervisor call takes significant time.
	 */
	mutex_exit(&cwq->cq_lock);

	qoffset = CWQ_QINDEX_TO_QOFFSET(tail_idx);
	ASSERT(cwq->cq_init == 1);

	/* grab hv lock to insure in-order access */
	mutex_enter(&cwq->cq_hv_lock);

	/*
	 * Prevent out of order settail requests by checking IDs
	 * as long as this is later than last entry sent to the
	 * hypervisor this one can be sent.  Otherwise skip
	 * since a later request passed this one. No need to
	 * worry about wrap with a 64-bit counter.
	 */

	if (my_id > cwq->cq_last_id) {
		/* notify the hypervisor about new requests */
		hverr = hv_ncs_settail(cwq->cq_handle, qoffset);
		cwq->cq_last_id = my_id;
	}

	mutex_exit(&cwq->cq_hv_lock);

	DBG2(n2cp, DHV, "cwq-submit: (cwq %d) hverr = %ld", cwq->cq_id, hverr);

	/* update global kstats */
	if (reqp->nr_job_stat >= 0) {
		atomic_inc_64(&n2cp->n_stats[qid][reqp->nr_job_stat]);
	}

	if (hverr == H_EOK) {
		int rv;

		/* we no longer need affinity so drop it */
		thread_affinity_clear(curthread);

		rv = n2cp_sync_process(cwq, reqp, last_cw, cep);
		n2cp_relecwq(cep);
		return (rv);
	}

	err = n2cp_herr2kerr(hverr);

	cwq->cq_ks.qks_qfail++;		/* kstat */
	DBG3(n2cp, DWARN,
	    "cwq-submit: (cwq %d) HV error %ld (err = %d)",
	    cwq->cq_id, hverr, err);

	qoffset = (uint64_t)-1;
	hverr = hv_ncs_gettail(cwq->cq_handle, &qoffset);
	DBG2(n2cp, DWARN, "cwq-submit: (cwq %d) gettail() hverr = %ld",
	    cwq->cq_id, hverr);
	if (hverr == H_EOK) {
		tail_idx = CWQ_QOFFSET_TO_QINDEX(qoffset);
		old_cq_tail = cwq->cq_first + tail_idx;
		ASSERT(old_cq_tail <= cwq->cq_last);
	}
	DBG3(n2cp, DWARN, "cwq-submit: (cwq %d) submit ERROR "
	    "(reset tail -> %p [idx %d])", cwq->cq_id,
	    old_cq_tail, tail_idx);

	cwq->cq_tail = old_cq_tail;

	mutex_enter(&cwq->cq_job_lock);
	n2cp_cwq_deljob(cwq, reqp);
	mutex_exit(&cwq->cq_job_lock);

	mutex_enter(&cwq->cq_lock);
	if (cwq->cq_busy_wait > 0)
		cv_signal(&cwq->cq_busy_cv);

s_exit:
	atomic_dec_64(&cwq->cq_ks.qks_currjobs);

	mutex_exit(&cwq->cq_lock);
	n2cp_relecwq(cep);
	thread_affinity_clear(curthread);

	return (err);
}


/* the thread affinity should be set */
int
n2cp_ulcwq_start(n2cp_t *n2cp, n2cp_request_t *reqp)
{
	int		err = 0;
	int		tail_idx;
	int		qid;
	cwq_t		*cwq = NULL;
	cwq_entry_t	*cep;
	cwq_cw_t	*cwbp = reqp->nr_cwb;
	int		cwcnt = reqp->nr_cwcnt;
	uint64_t	hverr = H_EOK;
	uint64_t	my_id, status;
	int		space, wrap;

	cep = reqp->nr_cep;
	if (cep == NULL) {
		/* the CWQ was offlined forcibly */
		thread_affinity_clear(curthread);
		return (CRYPTO_DEVICE_ERROR);
	}

	qid = cep->mm_cwqid;
	cwq = &cep->mm_queue;

	atomic_inc_64(&cwq->cq_ks.qks_currjobs);

	reqp->nr_cwq_id = cwq->cq_id;

	ASSERT(cwcnt == 1);

	mutex_enter(&cwq->cq_lock);

	/* determine whether wrap is required */
	space = cwq->cq_last - cwq->cq_tail + 1;
	wrap = cwcnt - space;

	cwbp[0].cw_intr = 0;

	/* copy what we can without wrapping the queue */
	copy_cw(&cwbp[0], cwq->cq_tail, min(space, cwcnt));

	/* check whether a 2nd bcopy is necessary (wrap) */
	if (wrap > 0) {
		/* wrapped - copy remaining cw entries to front of queue */
		copy_cw(&cwbp[space], cwq->cq_first, wrap);
		cwq->cq_tail = cwq->cq_first + wrap;
	} else if (wrap < 0) {
		/* no wrap */
		cwq->cq_tail += cwcnt;
	} else {
		/* filled up through last entry - advance tail back to start */
		cwq->cq_tail = cwq->cq_first;
	}

	tail_idx = cwq->cq_tail - cwq->cq_first;

	reqp->nr_id = tail_idx;
	reqp->nr_job_state = N2CP_JOBSTATE_SOLO;

	/* update request ID to sequence requests to hv */
	atomic_inc_64(&cwq->cq_next_id);

	/* keep track of our request ID */
	my_id = cwq->cq_next_id;

	/* update cwq kstats */
	cwq->cq_ks.qks_njobs++;
	cwq->cq_ks.qks_ncws += cwcnt;

	ASSERT(cwq->cq_init == 1);

	status = get_ulcwq_status();

	if (status != ULCWQ_STATUS_USER_RESET) {
		set_ulcwq_status();
		status = get_ulcwq_status();
	}

	if (status != ULCWQ_STATUS_USER_RESET) {
		/* offline the CWQ */
		DBG1(n2cp, DHV, "offlining cwq #%d\n", cwq->cq_id);

		mutex_enter(&cwq->cq_ulcwq_buf_lock);
		n2cp_move_req_off_page(reqp, KM_NOSLEEP, B_FALSE);
		mutex_exit(&cwq->cq_ulcwq_buf_lock);

		MAP_MUTEXES_ENTER_ALL(n2cp->n_cwqmap);
		n2cp_offline_cwq(n2cp, cwq->cq_id);
		MAP_MUTEXES_EXIT_ALL(n2cp->n_cwqmap);
		err = CRYPTO_DEVICE_ERROR;
		goto error;
	}

	set_ulcwq_tail(cwq->cq_tail);

	do {
		n2cp_delay(1);
		status = get_ulcwq_status();
	} while (!((status == ULCWQ_STATUS_DONE) ||
	    (status == ULCWQ_STATUS_FAILED)));

	if (status == ULCWQ_STATUS_DONE) {
		set_ulcwq_status();
	} else {
		/* XXX handle error */
		err = CRYPTO_DEVICE_ERROR;
		goto error;
	}

	cwq->cq_last_id = my_id;
	mutex_exit(&cwq->cq_lock);

	DBG2(n2cp, DHV, "cwq-submit: (cwq %d) hverr = %ld", cwq->cq_id, hverr);

	/* update global kstats */
	if (reqp->nr_job_stat >= 0) {
		atomic_inc_64(&(n2cp->n_stats[qid][reqp->nr_job_stat]));
	}

	if (hverr == H_EOK) {
		int	rv;

		n2cp_wake_next_job(cwq);
		atomic_dec_64(&cwq->cq_ks.qks_currjobs);

		reqp->nr_flags &= ~(N2CP_DONE | N2CP_NO_WAIT | N2CP_SYNC_WAIT);

		reqp->nr_errno = n2cp_check_errors(n2cp, cwq->cq_id,
		    reqp->nr_csr);

		/* update completed job counter */
		atomic_inc_64(&cwq->cq_ks.qks_nsync_jobs);

		if (reqp->nr_errno != CRYPTO_SUCCESS) {
			/* update error job counter */
			atomic_inc_64(&cwq->cq_ks.qks_nsync_err);
		}

		/* return status to kCF */
		rv = reqp->nr_errno;

		thread_affinity_clear(curthread);
		return (rv);
	}

error:
	err = n2cp_herr2kerr(hverr);
	cwq->cq_ks.qks_qfail++;		/* kstat */
/* XXX handle error */
	atomic_dec_64(&cwq->cq_ks.qks_currjobs);
	mutex_exit(&cwq->cq_lock);
	thread_affinity_clear(curthread);

	return (err);
}


/*
 * Map hypervisor error code to solaris errono. Only
 * H_ENORADDR, H_EBADALIGN, H_EWOULDBLOCK, and H_EIO are meaningful
 * to this device. Any other error codes are mapped EINVAL.
 */
static int
n2cp_herr2kerr(uint64_t hv_errcode)
{
	int	s_errcode;

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



void *
n2_contig_alloc(int len)
{
	void	*ptr;

	if (len < CWQ_ALIGNMENT)
		return (NULL);

	ptr = n2cp_nc.n_contig_alloc(len);
	if (ptr == NULL) {
		DBG1(NULL, DWARN,
		    "n2_contig_alloc: contig_mem_alloc(%d) failed", len);
		return (NULL);
	}
	return (ptr);
}

void
n2_contig_free(void *ptr, int len)
{
	n2cp_nc.n_contig_free(ptr, len);
}

/*
 * Check for hypervisor detected errors.
 * This functions sets the cj_error field of cj.
 */
static int
n2cp_check_errors(n2cp_t *n2cp, int cwq_id, uint64_t csr)
{
	switch (csr) {
	case CWQ_ERR_OK:
		return (0);
	case CWQ_ERR_PROTOCOL:
		n2cp_diperror(n2cp->n_dip,
		    "cwq %d encountered a protocol error, "
		    "service continued. (CSR:0x%x)", cwq_id, csr);
		return (CRYPTO_ARGUMENTS_BAD);
	case CWQ_ERR_HWE:
		/*
		 * Memory UE detected.  In theory the system should panic
		 * immediately after encountering a UE.  Just to be safe we
		 * will still offline the cwq and fail the current job.
		 */
		n2cp_diperror(n2cp->n_dip,
		    "cwq %d encountered a memory error and has been taken "
		    "offline (CSR:0x%x)", cwq_id, csr);
		MAP_MUTEXES_ENTER_ALL(n2cp->n_cwqmap);
		n2cp_offline_cwq(n2cp, cwq_id);
		MAP_MUTEXES_EXIT_ALL(n2cp->n_cwqmap);
		/* Check if last cwq was taken offline */
		n2cp_check_cwqs(n2cp);
		return (CRYPTO_DEVICE_ERROR);
	default:
		/* Should never get here */
		n2cp_diperror(n2cp->n_dip,
		    "cwq %d encountered an unknown error (%08x), "
		    "service continued", cwq_id, csr);
		return (CRYPTO_GENERAL_ERROR);
	}
}

/*
 * This routine is called after a crypto job fails with a ENODEV failure to see
 * if the last available cwq has been taken offline and we should unregister
 * from kcf.
 */
static void
n2cp_check_cwqs(n2cp_t *n2cp)
{
	/* Check if error caused all mmu's to be taken offline */
	if (n2cp->n_cwqmap.m_ncwqs_online == 0) {
		/*
		 * Fault, all CWQs are bad.
		 * 1. Notify kCF of failure
		 * 2. Log an error message
		 */
		n2cp_setfailed(n2cp);

		/*
		 * Report provider failure to kCF so it stops sending jobs
		 */
		crypto_provider_notification(n2cp->n_prov,
		    CRYPTO_PROVIDER_FAILED);
		DBG0(n2cp, DWARN,
		    "CRYPTO_PROVIDER_FAILED notification sent to kCF");

		/* Log error indicating that all CWQs are bad */
		n2cp_diperror(n2cp->n_dip,
		    "n2cp_check_cwqs: all CWQs have failed");

		n2cp_unregister_ncs_api();
	}
}

static int
n2cp_init_ncs_api(n2cp_t *n2cp)
{
	uint64_t	ncs_minor_ver;
	int		v, rv;

	for (v = 0; v < NVERSIONS; v++) {
		rv = hsvc_register(&ncs_hsvc[v], &ncs_minor_ver);
		if (rv == 0) {
			DBG1(n2cp, DWARN,
			    "ncs_minor_ver = %d", (int)ncs_minor_ver);
			break;
		}
		DBG4(n2cp, DATTACH,
		    "n2cp_init_ncs_api: grp: 0x%lx, maj: %ld, min: %ld, "
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
	n2cp_ncs_version_idx = v;
	ncs_hsvc_available = B_TRUE;
	DBG2(n2cp, DATTACH,
	    "n2cp_init_ncs_api: NCS API VERSION (%ld.%ld)",
	    ncs_hsvc[n2cp_ncs_version_idx].hsvc_major, ncs_minor_ver);
	n2cp->n_hvapi_major_version = ncs_hsvc[n2cp_ncs_version_idx].hsvc_major;
	n2cp->n_hvapi_minor_version = (uint_t)ncs_minor_ver;

	return (0);
}

int
n2cp_register_ncs_api(void)
{
	int			rv;
	uint64_t		ncs_minor_ver;

	if (ncs_hsvc_available == B_TRUE)
		return (0);

	rv = hsvc_register(&ncs_hsvc[n2cp_ncs_version_idx], &ncs_minor_ver);
	if (rv == 0) {
		ncs_hsvc_available = B_TRUE;
	} else {
		cmn_err(CE_WARN, "n2cp_register_ncs_api: hsvc_register "
		    "returned 0x%x", rv);
	}

	return (rv);
}

void
n2cp_unregister_ncs_api(void)
{
	int	rv;

	if (ncs_hsvc_available == B_TRUE) {
		rv =  hsvc_unregister(&ncs_hsvc[n2cp_ncs_version_idx]);
		if (rv == EINVAL) {
			cmn_err(CE_WARN, "n2cp_unregister_ncs_api: ncs api not "
			    "registered");
		}
		ncs_hsvc_available = B_FALSE;
	}
}

/*
 * Support to workaround SPU Store bug.
 * Allocate buffers that can be target for SPU store operations
 * as non-cacheable memory.
 */
#define	QUANTUM_SIZE	64

static vmem_t	*noncache_contig_mem_arena;
static vmem_t	*noncache_contig_mem_slab_arena;

static void *
noncache_contig_mem_alloc(size_t size)
{
	if ((size & (size - 1)) != 0)
		return (NULL);

	return (vmem_xalloc(noncache_contig_mem_arena, size, size, 0, 0,
	    NULL, NULL, VM_NOSLEEP));
}

static void
noncache_contig_mem_free(void *vaddr, size_t size)
{
	vmem_xfree(noncache_contig_mem_arena, vaddr, size);
}

/*ARGSUSED*/
static void *
noncache_contig_mem_span_alloc(vmem_t *vmp, size_t size, int vmflag)
{
	void		*addr;
	uint_t		attr;
	uint64_t	flushed;

	addr = contig_mem_alloc_align(contig_mem_slab_size,
	    contig_mem_slab_size);
	if (addr == NULL) {
		n2cp_nc.n_alloc_fail++;
		return (NULL);
	}
	/*
	 * Change the page to (L1) non-cacheable.
	 */
	if (hat_getattr(kas.a_hat, addr, &attr) != 0) {
		contig_mem_free(addr, contig_mem_slab_size);
		n2cp_nc.n_hat_fail++;
		return (NULL);
	}
	attr |= SFMMU_UNCACHEPTTE;
	hat_chgattr(kas.a_hat, addr, contig_mem_slab_size, attr);
	/*
	 * Flush any references to memory from L1 caches.
	 */
	flushed = mem_sync(addr, contig_mem_slab_size);
	if (flushed != contig_mem_slab_size) {
		attr &= ~SFMMU_UNCACHEPTTE;
		hat_chgattr(kas.a_hat, addr, contig_mem_slab_size, attr);
		contig_mem_free(addr, contig_mem_slab_size);
		n2cp_nc.n_sync_fail++;
		return (NULL);
	}

	n2cp_nc.n_alloc++;

	return (addr);
}

/*ARGSUSED*/
static void
noncache_contig_mem_span_free(vmem_t *vmp, void *addr, size_t size)
{
	uint_t	attr;

	ASSERT(((uintptr_t)addr & (contig_mem_slab_size - 1)) == 0);

	/*
	 * Restore cacheable attribute.
	 */
	if (hat_getattr(kas.a_hat, addr, &attr) != 0) {
		cmn_err(CE_PANIC,
		    "noncache_contig_mem_span_free: hat_getattr(%p) failed",
		    addr);
	}
	attr &= ~SFMMU_UNCACHEPTTE;
	hat_chgattr(kas.a_hat, addr, contig_mem_slab_size, attr);

	contig_mem_free(addr, contig_mem_slab_size);

	n2cp_nc.n_free++;
}

static void
noncache_contig_mem_init(void)
{
	noncache_contig_mem_slab_arena =
	    vmem_create("noncache_contig_mem_slab_arena", NULL, 0,
	    contig_mem_slab_size, NULL, NULL, NULL,
	    0, VM_SLEEP);

	noncache_contig_mem_arena = vmem_create("noncache_contig_mem_arena",
	    NULL, 0, QUANTUM_SIZE,
	    noncache_contig_mem_span_alloc,
	    noncache_contig_mem_span_free,
	    noncache_contig_mem_slab_arena,
	    0, VM_SLEEP | VM_BESTFIT);
}

static void
noncache_contig_mem_deinit(void)
{
	if (noncache_contig_mem_arena != NULL) {
		vmem_destroy(noncache_contig_mem_arena);
		noncache_contig_mem_arena = NULL;
	}

	if (noncache_contig_mem_slab_arena != NULL) {
		vmem_destroy(noncache_contig_mem_slab_arena);
		noncache_contig_mem_slab_arena = NULL;
	}
}

static void
noncache_bcopy(const void *src, void *dst, size_t n)
{
	uint64_t	ss, dd;
	uint64_t	a;
	uint64_t	*s64p, *d64p;
	uint32_t	*s32p, *d32p;
	uint16_t	*s16p, *d16p;
	uint8_t		*s8p, *d8p;

	ss = (uint64_t)src;
	dd = (uint64_t)dst;

	a = ss | dd;

	if ((a & 0x7) == 0) {
		/* 8 byte aligned */

		s64p = (uint64_t *)src;
		d64p = (uint64_t *)dst;

		for (; n >= 8; n -= 8)
			*d64p++ = *s64p++;
		if (n > 0) {
			s32p = (uint32_t *)s64p;
			d32p = (uint32_t *)d64p;
			goto bc4;
		}

		return;
	}

	if ((a & 0x3) == 0) {
		/* 4 byte aligned */

		s32p = (uint32_t *)src;
		d32p = (uint32_t *)dst;
bc4:
		for (; n >= 4; n -= 4)
			*d32p++ = *s32p++;
		if (n > 0) {
			s16p = (uint16_t *)s32p;
			d16p = (uint16_t *)d32p;
			goto bc2;
		}

		return;
	}

	if ((a & 0x1) == 0) {

		/* 2 byte aligned */

		s16p = (uint16_t *)src;
		d16p = (uint16_t *)dst;
bc2:
		for (; n >= 2; n -= 2)
			*d16p++ = *s16p++;
		if (n > 0)
			*((uint8_t *)d16p) = *((uint8_t *)s16p);
	} else {
		s8p = (uint8_t *)src;
		d8p = (uint8_t *)dst;

		while (n-- > 0)
			*d8p++ = *s8p++;
	}
}
/*
 * Check if item in list, if so return 1, not return 0
 */
static int
is_in_list(int *list, int size, int item) {
	int i;
	int ret = 0;

	for (i = 0; i < size; i++) {
		if (list[i] == item) {
			ret = 1;
			break;
		}
	}
	return (ret);
}
