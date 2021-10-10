/*
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/types.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/systm.h>
#include <sys/kstat.h>
#include <sys/crypto/common.h>
#include <sys/crypto/spi.h>
#include <sys/n2cp.h>

/*
 * Kernel statistics.
 */
static int n2cp_ksupdate(kstat_t *, int);

/* kstat lock */
static kmutex_t n2cp_ks_lock;

#ifdef DEBUG
extern int n2cp_hist_interval;
#endif

/*
 * Initialize Kstats.
 */
void
n2cp_ksinit(n2cp_t *n2cp)
{
	char	buf[64];
	int	instance;
	int	i;
#ifdef DEBUG
	int	j;
#endif /* DEBUG */

	if (ddi_getprop(DDI_DEV_T_ANY, n2cp->n_dip,
	    DDI_PROP_CANSLEEP | DDI_PROP_DONTPASS, "nostats", 0) != 0) {
		/*
		 * sysadmin has explicity disabled stats to prevent
		 * covert channel.
		 */
		return;
	}

	instance = ddi_get_instance(n2cp->n_dip);

	/*
	 * Interrupt kstats.
	 */
	(void) sprintf(buf, "%sc%d", DRIVER, instance);

	/*
	 * Named kstats.
	 */
	if ((n2cp->n_ksp = kstat_create(DRIVER, instance, NULL, "misc",
	    KSTAT_TYPE_NAMED, sizeof (n2cp_stat_t) / sizeof (kstat_named_t),
	    KSTAT_FLAG_WRITABLE)) == NULL) {
		n2cp_error(n2cp, "unable to create kstats");
	} else {
		cwq_entry_t	*cep;
		n2cp_stat_t *dkp = (n2cp_stat_t *)n2cp->n_ksp->ks_data;

		kstat_named_init(&dkp->ns_status, "status", KSTAT_DATA_CHAR);
		kstat_named_init(&dkp->ns_inbuf_allocs, "inbuf_allocs",
		    KSTAT_DATA_ULONGLONG);
		kstat_named_init(&dkp->ns_outbuf_allocs, "outbuf_allocs",
		    KSTAT_DATA_ULONGLONG);

		for (i = 0; i < n2cp->n_cwqmap.m_ncwqs; i++) {
			cep = &n2cp->n_cwqmap.m_cwqlist[i];
			if (cep->mm_state == CWQ_STATE_UNINIT) {
				continue;
			}
			(void) sprintf(buf, "cwq%did", cep->mm_cwqid);
			kstat_named_init(&dkp->ns_cwq[i].ns_cwqid, buf,
			    KSTAT_DATA_INT32);
			dkp->ns_cwq[i].ns_cwqid.value.i32 = cep->mm_cwqid;
			(void) sprintf(buf, "cwq%dhandle", cep->mm_cwqid);
			kstat_named_init(&dkp->ns_cwq[i].ns_cwqhandle, buf,
			    KSTAT_DATA_ULONGLONG);
			dkp->ns_cwq[i].ns_cwqhandle.value.ui64 =
			    cep->mm_queue.cq_handle;
			(void) sprintf(buf, "cwq%dstate", cep->mm_cwqid);
			kstat_named_init(&dkp->ns_cwq[i].ns_cwqstate, buf,
			    KSTAT_DATA_CHAR);
			(void) sprintf(buf, "cwq%dsubmit", cep->mm_cwqid);
			kstat_named_init(&dkp->ns_cwq[i].ns_submit, buf,
			    KSTAT_DATA_ULONGLONG);
			(void) sprintf(buf, "cwq%dcurrjobs", cep->mm_cwqid);
			kstat_named_init(&dkp->ns_cwq[i].ns_currjobs, buf,
			    KSTAT_DATA_ULONGLONG);
			(void) sprintf(buf, "cwq%dcwcount", cep->mm_cwqid);
			kstat_named_init(&dkp->ns_cwq[i].ns_cwcount, buf,
			    KSTAT_DATA_ULONGLONG);
			(void) sprintf(buf, "cwq%dqfull", cep->mm_cwqid);
			kstat_named_init(&dkp->ns_cwq[i].ns_qfull, buf,
			    KSTAT_DATA_ULONGLONG);
			(void) sprintf(buf, "cwq%dqbusy", cep->mm_cwqid);
			kstat_named_init(&dkp->ns_cwq[i].ns_qbusy, buf,
			    KSTAT_DATA_ULONGLONG);
			(void) sprintf(buf, "cwq%dqupdate_failure",
			    cep->mm_cwqid);
			kstat_named_init(&dkp->ns_cwq[i].ns_qupdate_failure,
			    buf, KSTAT_DATA_ULONGLONG);
			(void) sprintf(buf, "cwq%dnsync_jobs", cep->mm_cwqid);
			kstat_named_init(&dkp->ns_cwq[i].ns_nsync_jobs, buf,
			    KSTAT_DATA_ULONGLONG);
			(void) sprintf(buf, "cwq%dnsync_err", cep->mm_cwqid);
			kstat_named_init(&dkp->ns_cwq[i].ns_nsync_err, buf,
			    KSTAT_DATA_ULONGLONG);
#ifdef DEBUG
			(void) sprintf(buf, "cwq%dhighwater", cep->mm_cwqid);
			kstat_named_init(&dkp->ns_cwq[i].ns_highwater, buf,
			    KSTAT_DATA_ULONGLONG);

			for (j = 0; j < N2CP_MAX_INTERVALS; j++) {
				if (j == (N2CP_MAX_INTERVALS - 1)) {
					(void) sprintf(buf,
					    "cwq%dhist > %d ms",
					    cep->mm_cwqid,
					    j * n2cp_hist_interval);

				} else {
					(void) sprintf(buf,
					    "cwq%dhist < %d ms",
					    cep->mm_cwqid,
					    (j + 1) * n2cp_hist_interval);
				}
				kstat_named_init(
				    &dkp->ns_cwq[i].ns_histogram[j], buf,
				    KSTAT_DATA_ULONG);
			}
#endif /* DEBUG */
		}

		kstat_named_init(&dkp->ns_algs[DS_DES], "des",
		    KSTAT_DATA_ULONG);
		kstat_named_init(&dkp->ns_algs[DS_DES3], "des3",
		    KSTAT_DATA_ULONG);
		kstat_named_init(&dkp->ns_algs[DS_AES], "aes",
		    KSTAT_DATA_ULONG);
		kstat_named_init(&dkp->ns_algs[DS_MD5], "md5",
		    KSTAT_DATA_ULONG);
		kstat_named_init(&dkp->ns_algs[DS_SHA1], "sha1",
		    KSTAT_DATA_ULONG);
		kstat_named_init(&dkp->ns_algs[DS_SHA256], "sha256",
		    KSTAT_DATA_ULONG);
		if (is_KT(n2cp)) {
			kstat_named_init(&dkp->ns_algs[DS_SHA384], "sha384",
			    KSTAT_DATA_ULONG);
			kstat_named_init(&dkp->ns_algs[DS_SHA512], "sha512",
			    KSTAT_DATA_ULONG);
		} else {
			kstat_named_init(&dkp->ns_algs[DS_RC4], "arcfour",
			    KSTAT_DATA_ULONG);
		}
		kstat_named_init(&dkp->ns_algs[DS_MD5_HMAC], "md5hmac",
		    KSTAT_DATA_ULONG);
		kstat_named_init(&dkp->ns_algs[DS_SHA1_HMAC], "sha1hmac",
		    KSTAT_DATA_ULONG);
		kstat_named_init(&dkp->ns_algs[DS_SHA256_HMAC], "sha256hmac",
		    KSTAT_DATA_ULONG);
		kstat_named_init(&dkp->ns_algs[DS_SSL_MD5_MAC], "ssl3md5mac",
		    KSTAT_DATA_ULONG);
		kstat_named_init(&dkp->ns_algs[DS_SSL_SHA1_MAC], "ssl3sha1mac",
		    KSTAT_DATA_ULONG);
#ifdef	SSL3_SHA256_MAC_SUPPORT
		kstat_named_init(&dkp->ns_algs[DS_SSL_SHA256_MAC],
		    "sslsha256hmac", KSTAT_DATA_ULONG);
#endif

		n2cp->n_ksp->ks_update = n2cp_ksupdate;
		n2cp->n_ksp->ks_private = n2cp;
		n2cp->n_ksp->ks_lock = &n2cp_ks_lock;

		kstat_install(n2cp->n_ksp);
	}
}

/*
 * Deinitialize Kstats.
 */
void
n2cp_ksdeinit(n2cp_t *n2cp)
{
	if (n2cp->n_ksp != NULL) {
		kstat_delete(n2cp->n_ksp);
		n2cp->n_ksp = NULL;
	}
}

/*
 * Update Kstats.
 */
static int
n2cp_ksupdate(kstat_t *ksp, int rw)
{
	n2cp_t		*n2cp;
	n2cp_stat_t	*dkp;
	int		i;
#ifdef DEBUG
	int		j;
#endif /* DEBUG */
	int		qid;
	uint64_t	total;

	n2cp = (n2cp_t *)ksp->ks_private;
	dkp = (n2cp_stat_t *)ksp->ks_data;

	if (rw == KSTAT_WRITE) {
		for (i = 0; i < DS_MAX; i++) {
			n2cp->n_stats[0][i] = dkp->ns_algs[i].value.ull;
			for (qid = 1; qid < N2CP_MAX_NCWQS; qid++)
				n2cp->n_stats[qid][i] = 0;
		}
	} else {
		/* handy status value */
		if (n2cp->n_flags & N2CP_FAILED) {
			/* device has failed */
			(void) strcpy(dkp->ns_status.value.c, "fail");
		} else if (n2cp->n_cwqmap.m_ncwqs) {
			if (n2cp->n_cwqmap.m_ncwqs_online) {
				/* everything looks good */
				(void) strcpy(dkp->ns_status.value.c, "online");
			} else {
				(void) strcpy(dkp->ns_status.value.c,
				    "offline");
			}
		} else {
			/* No crypto units */
			(void) strcpy(dkp->ns_status.value.c, "unconfigured");
		}

		for (i = 0; i < DS_MAX; i++) {
			total = 0;
			for (qid = 0; qid < N2CP_MAX_NCWQS; qid++)
				total += n2cp->n_stats[qid][i];

			dkp->ns_algs[i].value.ull = total;
		}

		dkp->ns_inbuf_allocs.value.ull = n2cp->n_inbuf_allocs;
		dkp->ns_outbuf_allocs.value.ull = n2cp->n_outbuf_allocs;

		for (i = 0; i < n2cp->n_cwqmap.m_ncwqs; i++) {
			cwq_entry_t	*cep;

			cep = &n2cp->n_cwqmap.m_cwqlist[i];

			switch (cep->mm_state) {
			case CWQ_STATE_ERROR:
				(void) strcpy(
				    dkp->ns_cwq[i].ns_cwqstate.value.c,
				    "error");
				break;
			case CWQ_STATE_OFFLINE:
				(void) strcpy(
				    dkp->ns_cwq[i].ns_cwqstate.value.c,
				    "offline");
				break;
			case CWQ_STATE_ONLINE:
				(void) strcpy(
				    dkp->ns_cwq[i].ns_cwqstate.value.c,
				    "online");
				break;
			case CWQ_STATE_REMOVED:
				(void) strcpy(
				    dkp->ns_cwq[i].ns_cwqstate.value.c,
				    "removed");
				break;
			default:
				(void) strcpy(
				    dkp->ns_cwq[i].ns_cwqstate.value.c,
				    "unknown");
				break;
			}

			dkp->ns_cwq[i].ns_cwqhandle.value.ui64 =
			    cep->mm_queue.cq_handle;
			dkp->ns_cwq[i].ns_submit.value.ull =
			    cep->mm_queue.cq_ks.qks_njobs;
			dkp->ns_cwq[i].ns_currjobs.value.ull =
			    cep->mm_queue.cq_ks.qks_currjobs;
			dkp->ns_cwq[i].ns_cwcount.value.ull =
			    cep->mm_queue.cq_ks.qks_ncws;
			dkp->ns_cwq[i].ns_qfull.value.ull =
			    cep->mm_queue.cq_ks.qks_qfull;
			dkp->ns_cwq[i].ns_qbusy.value.ull =
			    cep->mm_queue.cq_ks.qks_qbusy;
			dkp->ns_cwq[i].ns_qupdate_failure.value.ull =
			    cep->mm_queue.cq_ks.qks_qfail;
			dkp->ns_cwq[i].ns_nsync_jobs.value.ull =
			    cep->mm_queue.cq_ks.qks_nsync_jobs;
			dkp->ns_cwq[i].ns_nsync_err.value.ull =
			    cep->mm_queue.cq_ks.qks_nsync_err;
#ifdef DEBUG
			dkp->ns_cwq[i].ns_highwater.value.ull =
			    cep->mm_queue.cq_ks.qks_highwater;

			for (j = 0; j < N2CP_MAX_INTERVALS; j++) {
				dkp->ns_cwq[i].ns_histogram[j].value.ull =
				    cep->mm_queue.cq_ks.qks_histogram[j];
			}
#endif /* DEBUG */
		}
	}

	return (0);
}

void
n2cp_kstat_clear(n2cp_t *n2cp, int qid)
{
	n2cp_cwq2cpu_map_t	*c2cp = &n2cp->n_cwqmap;
	cwq_entry_t		*cep;
#ifdef DEBUG
	int			i;
#endif

	cep = &c2cp->m_cwqlist[qid];
	if (cep == NULL) {
		return;
	}

	cep->mm_queue.cq_ks.qks_njobs = 0;
	cep->mm_queue.cq_ks.qks_currjobs = 0;
	cep->mm_queue.cq_ks.qks_ncws = 0;
	cep->mm_queue.cq_ks.qks_qfull = 0;
	cep->mm_queue.cq_ks.qks_qbusy = 0;
	cep->mm_queue.cq_ks.qks_qfail = 0;
	cep->mm_queue.cq_ks.qks_nsync_jobs = 0;
	cep->mm_queue.cq_ks.qks_nsync_err = 0;
#ifdef DEBUG
	cep->mm_queue.cq_ks.qks_highwater = 0;
	for (i = 0; i < N2CP_MAX_INTERVALS; i++) {
		cep->mm_queue.cq_ks.qks_histogram[i] = 0;
	}
#endif
}
