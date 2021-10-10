/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <sys/types.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/systm.h>
#include <sys/kstat.h>
#include <sys/crypto/common.h>
#include <sys/crypto/spi.h>
#include <sys/ncp.h>

/*
 * Kernel statistics.
 */
static int ncp_ksupdate(kstat_t *, int);

/* kstat lock */
static kmutex_t ncp_ks_lock;

/*
 * Initialize Kstats.
 */
void
ncp_ksinit(ncp_t *ncp)
{
	char	buf[64];
	int	instance;
	int	i;
	size_t	size;

	if (ddi_getprop(DDI_DEV_T_ANY, ncp->n_dip,
	    DDI_PROP_CANSLEEP | DDI_PROP_DONTPASS, "nostats", 0) != 0) {
		/*
		 * sysadmin has explicity disabled stats to prevent
		 * covert channel.
		 */
		return;
	}

	instance = ddi_get_instance(ncp->n_dip);

	/*
	 * Interrupt kstats.
	 */
	(void) sprintf(buf, "%sc%d", DRIVER, instance);

	size = sizeof (ncp_stat_t) + (sizeof (ncp_mau_stat_t) *
	    ncp->n_maumap.m_nmaus);

	/*
	 * Named kstats.
	 */
	if ((ncp->n_ksp = kstat_create(DRIVER, instance, NULL, "misc",
	    KSTAT_TYPE_NAMED, size / sizeof (kstat_named_t),
	    KSTAT_FLAG_WRITABLE)) == NULL) {
		ncp_error(ncp, "unable to create kstats");
	} else {
		mau_entry_t	*mep;
		ncp_stat_t *dkp = (ncp_stat_t *)ncp->n_ksp->ks_data;
		ncp_mau_stat_t *mkp = (ncp_mau_stat_t *)(dkp + 1);

		kstat_named_init(&dkp->ns_status, "status", KSTAT_DATA_CHAR);

		for (i = 0; i < ncp->n_maumap.m_nmaus; i++) {
			mep = &ncp->n_maumap.m_maulist[i];
			if (mep->mm_state == MAU_STATE_UNINIT) {
				continue;
			}
			(void) sprintf(buf, "mau%did", mep->mm_mauid);
			kstat_named_init(&mkp[i].ns_mauid, buf,
			    KSTAT_DATA_INT32);
			mkp[i].ns_mauid.value.i32 = mep->mm_mauid;
			(void) sprintf(buf, "mau%dhandle", mep->mm_mauid);
			kstat_named_init(&mkp[i].ns_mauhandle, buf,
			    KSTAT_DATA_ULONGLONG);
			mkp[i].ns_mauhandle.value.ui64 =
			    mep->mm_queue.nmq_handle;
			(void) sprintf(buf, "mau%dstate", mep->mm_mauid);
			kstat_named_init(&mkp[i].ns_maustate, buf,
			    KSTAT_DATA_CHAR);
			(void) sprintf(buf, "mau%dsubmit", mep->mm_mauid);
			kstat_named_init(&mkp[i].ns_submit, buf,
			    KSTAT_DATA_ULONGLONG);
			(void) sprintf(buf, "mau%dqfull", mep->mm_mauid);
			kstat_named_init(&mkp[i].ns_qfull, buf,
			    KSTAT_DATA_ULONGLONG);
			(void) sprintf(buf, "mau%dqbusy", mep->mm_mauid);
			kstat_named_init(&mkp[i].ns_qbusy, buf,
			    KSTAT_DATA_ULONGLONG);
			(void) sprintf(buf, "mau%dqupdate_failure",
			    mep->mm_mauid);
			kstat_named_init(&mkp[i].ns_qupdate_failure,
			    buf, KSTAT_DATA_ULONGLONG);
			(void) sprintf(buf, "mau%dnintr", mep->mm_mauid);
			kstat_named_init(&mkp[i].ns_nintr, buf,
			    KSTAT_DATA_ULONGLONG);
			(void) sprintf(buf, "mau%dnintr_err", mep->mm_mauid);
			kstat_named_init(&mkp[i].ns_nintr_err, buf,
			    KSTAT_DATA_ULONGLONG);
			(void) sprintf(buf, "mau%dnintr_jobs", mep->mm_mauid);
			kstat_named_init(&mkp[i].ns_nintr_jobs, buf,
			    KSTAT_DATA_ULONGLONG);
		}

		/* rsa */
		kstat_named_init(&dkp->ns_algs[DS_RSAPUBLIC], "rsapublic",
		    KSTAT_DATA_ULONGLONG);
		kstat_named_init(&dkp->ns_algs[DS_RSAPRIVATE], "rsaprivate",
		    KSTAT_DATA_ULONGLONG);
		kstat_named_init(&dkp->ns_algs[DS_RSAGEN], "rsagenerate",
		    KSTAT_DATA_ULONGLONG);

		/* dsa */
		kstat_named_init(&dkp->ns_algs[DS_DSASIGN], "dsasign",
		    KSTAT_DATA_ULONGLONG);
		kstat_named_init(&dkp->ns_algs[DS_DSAVERIFY], "dsaverify",
		    KSTAT_DATA_ULONGLONG);

		/* dh */
		kstat_named_init(&dkp->ns_algs[DS_DHGEN], "dhgenerate",
		    KSTAT_DATA_ULONGLONG);
		kstat_named_init(&dkp->ns_algs[DS_DHDERIVE], "dhderive",
		    KSTAT_DATA_ULONGLONG);

		/* ecdsa */
		kstat_named_init(&dkp->ns_algs[DS_ECDSASIGN], "ecdsasign",
		    KSTAT_DATA_ULONGLONG);
		kstat_named_init(&dkp->ns_algs[DS_ECDSAVERIFY], "ecdsaverify",
		    KSTAT_DATA_ULONGLONG);
		kstat_named_init(&dkp->ns_algs[DS_ECGEN], "ecgenerate",
		    KSTAT_DATA_ULONGLONG);

		/* ecdh */
		kstat_named_init(&dkp->ns_algs[DS_ECDHDERIVE], "ecdhderive",
		    KSTAT_DATA_ULONGLONG);

		ncp->n_ksp->ks_update = ncp_ksupdate;
		ncp->n_ksp->ks_private = ncp;
		ncp->n_ksp->ks_lock = &ncp_ks_lock;

		kstat_install(ncp->n_ksp);
	}
}

/*
 * Deinitialize Kstats.
 */
void
ncp_ksdeinit(ncp_t *ncp)
{
	if (ncp->n_ksp != NULL) {
		kstat_delete(ncp->n_ksp);
		ncp->n_ksp = NULL;
	}
}

/*
 * Update Kstats.
 */
int
ncp_ksupdate(kstat_t *ksp, int rw)
{
	ncp_t		*ncp;
	ncp_stat_t	*dkp;
	ncp_mau_stat_t	*mkp;
	int		i;

	ncp = (ncp_t *)ksp->ks_private;
	dkp = (ncp_stat_t *)ksp->ks_data;
	mkp = (ncp_mau_stat_t *)(dkp + 1);

	if (rw == KSTAT_WRITE) {
		for (i = 0; i < DS_MAX; i++) {
			ncp->n_stats[i] = dkp->ns_algs[i].value.ull;
		}
	} else {
		/* handy status value */
		if (ncp_isfailed(ncp)) {
			/* device has failed */
			(void) strcpy(dkp->ns_status.value.c, "fail");
		} else if (ncp->n_maumap.m_nmaus > 0) {
			if (ncp->n_maumap.m_nmaus_online > 0) {
				/* everything looks good */
				(void) strcpy(dkp->ns_status.value.c,
				    "online");
			} else {
				/* no online crypto units */
				(void) strcpy(dkp->ns_status.value.c,
				    "offline");
			}
		} else {
			/* no online crypto units */
			(void) strcpy(dkp->ns_status.value.c,
			    "unconfigured");
		}

		for (i = 0; i < DS_MAX; i++) {
			dkp->ns_algs[i].value.ull = ncp->n_stats[i];
		}

		for (i = 0; i < ncp->n_maumap.m_nmaus; i++) {
			mau_entry_t	*mep;

			mep = &ncp->n_maumap.m_maulist[i];

			switch (mep->mm_state) {
			case MAU_STATE_ERROR:
				(void) strcpy(
				    mkp[i].ns_maustate.value.c,
				    "error");
				break;
			case MAU_STATE_OFFLINE:
				(void) strcpy(
				    mkp[i].ns_maustate.value.c,
				    "offline");
				break;
			case MAU_STATE_ONLINE:
				(void) strcpy(
				    mkp[i].ns_maustate.value.c,
				    "online");
				break;
			case MAU_STATE_REMOVED:
				(void) strcpy(
				    mkp[i].ns_maustate.value.c,
				    "removed");
				break;
			default:
				(void) strcpy(
				    mkp[i].ns_maustate.value.c,
				    "unknown");
				break;
			}

			mkp[i].ns_mauhandle.value.ui64 =
			    mep->mm_queue.nmq_handle;
			mkp[i].ns_submit.value.ull =
			    mep->mm_queue.nmq_ks.qks_njobs;
			mkp[i].ns_qfull.value.ull =
			    mep->mm_queue.nmq_ks.qks_qfull;
			mkp[i].ns_qbusy.value.ull =
			    mep->mm_queue.nmq_ks.qks_qbusy;
			mkp[i].ns_qupdate_failure.value.ull =
			    mep->mm_queue.nmq_ks.qks_qfail;
			mkp[i].ns_nintr.value.ull =
			    mep->mm_queue.nmq_ks.qks_nintr;
			mkp[i].ns_nintr_err.value.ull =
			    mep->mm_queue.nmq_ks.qks_nintr_err;
			mkp[i].ns_nintr_jobs.value.ull =
			    mep->mm_queue.nmq_ks.qks_nintr_jobs;
		}
	}

	return (0);
}

void
ncp_kstat_clear(ncp_t *ncp, int mid)
{
	ncp_mau_queue_t *nmq;
	mau_entry_t	*mep;

	/* Find the MAU data structure based on its ID */
	if ((mep = ncp_map_findmau(ncp, mid)) == NULL) {
		return;
	}
	nmq = &mep->mm_queue;

	nmq->nmq_ks.qks_njobs = 0;
	nmq->nmq_ks.qks_qfull = 0;
	nmq->nmq_ks.qks_qbusy = 0;
	nmq->nmq_ks.qks_qfail = 0;
	nmq->nmq_ks.qks_nintr = 0;
	nmq->nmq_ks.qks_nintr_err = 0;
	nmq->nmq_ks.qks_nintr_jobs = 0;
}
