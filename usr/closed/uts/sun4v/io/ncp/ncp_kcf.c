/*
 * Copyright (c) 2005, 2011, Oracle and/or its affiliates. All rights reserved.
 */


#include <sys/types.h>
#include <sys/modctl.h>
#include <sys/conf.h>
#include <sys/devops.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/cmn_err.h>
#include <sys/varargs.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/kmem.h>
#include <sys/ioccom.h>
#include <sys/cpuvar.h>
#include <sys/open.h>
#include <sys/cred.h>
#include <sys/kstat.h>
#include <sys/strsun.h>
#include <sys/systm.h>
#include <sys/note.h>
#include <sys/crypto/common.h>
#include <sys/crypto/spi.h>
#include <sys/ncp.h>

int ncp_free_context(crypto_ctx_t *);
static int ncp_free_context_low(crypto_ctx_t *);
static void ncp_start_task(void *);

/*
 * We want these inlined for performance.
 */
#ifndef	DEBUG
#pragma inline(ncp_freereq, ncp_getreq)
#pragma inline(ncp_reverse, ncp_length)
#endif

#define	IDENT_ASYM	"Crypto Accel Asym 1.0"
#define	VENDOR		"Sun Microsystems, Inc."

int	ncp_fips_post_mid;

/*
 * CSPI information (entry points, provider info, etc.)
 */

/* Mechanisms for the asymmetric cipher provider */
static crypto_mech_info_t ncp_mech_info_table[] = {
	/* DSA */
	{SUN_CKM_DSA, DSA_MECH_INFO_TYPE,
	    CRYPTO_FG_SIGN | CRYPTO_FG_VERIFY |
	    CRYPTO_FG_SIGN_ATOMIC | CRYPTO_FG_VERIFY_ATOMIC,
	    DSA_MIN_KEY_LEN * 8, DSA_MAX_KEY_LEN * 8,
	    CRYPTO_KEYSIZE_UNIT_IN_BITS},

	/* RSA */
	{SUN_CKM_RSA_X_509, RSA_X_509_MECH_INFO_TYPE,
	    CRYPTO_FG_ENCRYPT | CRYPTO_FG_DECRYPT | CRYPTO_FG_SIGN |
	    CRYPTO_FG_SIGN_RECOVER | CRYPTO_FG_VERIFY |
	    CRYPTO_FG_VERIFY_RECOVER |
	    CRYPTO_FG_ENCRYPT_ATOMIC | CRYPTO_FG_DECRYPT_ATOMIC |
	    CRYPTO_FG_SIGN_ATOMIC | CRYPTO_FG_SIGN_RECOVER_ATOMIC |
	    CRYPTO_FG_VERIFY_ATOMIC | CRYPTO_FG_VERIFY_RECOVER_ATOMIC,
	    RSA_MIN_KEY_LEN * 8, RSA_MAX_KEY_LEN * 8,
	    CRYPTO_KEYSIZE_UNIT_IN_BITS},
	{SUN_CKM_RSA_PKCS, RSA_PKCS_MECH_INFO_TYPE,
	    CRYPTO_FG_ENCRYPT | CRYPTO_FG_DECRYPT | CRYPTO_FG_SIGN |
	    CRYPTO_FG_SIGN_RECOVER | CRYPTO_FG_VERIFY |
	    CRYPTO_FG_VERIFY_RECOVER |
	    CRYPTO_FG_ENCRYPT_ATOMIC | CRYPTO_FG_DECRYPT_ATOMIC |
	    CRYPTO_FG_SIGN_ATOMIC | CRYPTO_FG_SIGN_RECOVER_ATOMIC |
	    CRYPTO_FG_VERIFY_ATOMIC | CRYPTO_FG_VERIFY_RECOVER_ATOMIC,
	    RSA_MIN_KEY_LEN * 8, RSA_MAX_KEY_LEN * 8,
	    CRYPTO_KEYSIZE_UNIT_IN_BITS},
	{SUN_CKM_RSA_PKCS_KEY_PAIR_GEN, RSA_PKCS_KEY_PAIR_GEN_MECH_INFO_TYPE,
	    CRYPTO_FG_GENERATE_KEY_PAIR,
	    RSA_MIN_KEY_LEN * 8, RSA_MAX_KEY_LEN * 8,
	    CRYPTO_KEYSIZE_UNIT_IN_BITS},

	/* DH */
	{SUN_CKM_DH_PKCS_KEY_PAIR_GEN, DH_PKCS_KEY_PAIR_GEN_MECH_INFO_TYPE,
	    CRYPTO_FG_GENERATE_KEY_PAIR,
	    DH_MIN_KEY_LEN * 8, DH_MAX_KEY_LEN * 8,
	    CRYPTO_KEYSIZE_UNIT_IN_BITS},
	{SUN_CKM_DH_PKCS_DERIVE, DH_PKCS_DERIVE_MECH_INFO_TYPE,
	    CRYPTO_FG_DERIVE, DH_MIN_KEY_LEN * 8, DH_MAX_KEY_LEN * 8,
	    CRYPTO_KEYSIZE_UNIT_IN_BITS},

	/* ECC */
	{SUN_CKM_EC_KEY_PAIR_GEN, EC_KEY_PAIR_GEN_MECH_INFO_TYPE,
	    CRYPTO_FG_GENERATE_KEY_PAIR,
	    EC_MIN_KEY_LEN, EC_MAX_KEY_LEN, CRYPTO_KEYSIZE_UNIT_IN_BITS},
	{SUN_CKM_ECDH1_DERIVE, ECDH1_DERIVE_MECH_INFO_TYPE,
	    CRYPTO_FG_DERIVE,
	    EC_MIN_KEY_LEN, EC_MAX_KEY_LEN, CRYPTO_KEYSIZE_UNIT_IN_BITS},
	{SUN_CKM_ECDSA, ECDSA_MECH_INFO_TYPE,
	    CRYPTO_FG_SIGN | CRYPTO_FG_SIGN_ATOMIC |
	    CRYPTO_FG_VERIFY | CRYPTO_FG_VERIFY_ATOMIC,
	    EC_MIN_KEY_LEN, EC_MAX_KEY_LEN, CRYPTO_KEYSIZE_UNIT_IN_BITS},
};

static void ncp_provider_status(crypto_provider_handle_t, uint_t *);

static crypto_control_ops_t ncp_control_ops = {
	ncp_provider_status
};

static int ncp_encrypt_init(crypto_ctx_t *, crypto_mechanism_t *,
    crypto_key_t *, crypto_spi_ctx_template_t, crypto_req_handle_t);
static int ncp_encrypt(crypto_ctx_t *, crypto_data_t *, crypto_data_t *,
    crypto_req_handle_t);
static int ncp_encrypt_atomic(crypto_provider_handle_t, crypto_session_id_t,
    crypto_mechanism_t *, crypto_key_t *, crypto_data_t *,
    crypto_data_t *, crypto_spi_ctx_template_t, crypto_req_handle_t);

static int ncp_decrypt_init(crypto_ctx_t *, crypto_mechanism_t *,
    crypto_key_t *, crypto_spi_ctx_template_t, crypto_req_handle_t);
static int ncp_decrypt(crypto_ctx_t *, crypto_data_t *, crypto_data_t *,
    crypto_req_handle_t);
static int ncp_decrypt_atomic(crypto_provider_handle_t, crypto_session_id_t,
    crypto_mechanism_t *, crypto_key_t *, crypto_data_t *,
    crypto_data_t *, crypto_spi_ctx_template_t, crypto_req_handle_t);

/* RSA does not support multi-part operations */
static crypto_cipher_ops_t ncp_cipher_ops = {
	ncp_encrypt_init,
	ncp_encrypt,
	NULL,			/* encrypt_update */
	NULL,			/* encrypt_final */
	ncp_encrypt_atomic,
	ncp_decrypt_init,
	ncp_decrypt,
	NULL,			/* decrypt_update */
	NULL,			/* decrypt_final */
	ncp_decrypt_atomic
};

static int ncp_sign_init(crypto_ctx_t *, crypto_mechanism_t *, crypto_key_t *,
    crypto_spi_ctx_template_t, crypto_req_handle_t);
static int ncp_sign(crypto_ctx_t *, crypto_data_t *, crypto_data_t *,
    crypto_req_handle_t);
static int ncp_sign_atomic(crypto_provider_handle_t, crypto_session_id_t,
    crypto_mechanism_t *, crypto_key_t *, crypto_data_t *, crypto_data_t *,
    crypto_spi_ctx_template_t, crypto_req_handle_t);
static int ncp_sign_recover_init(crypto_ctx_t *, crypto_mechanism_t *,
    crypto_key_t *, crypto_spi_ctx_template_t, crypto_req_handle_t);
static int ncp_sign_recover(crypto_ctx_t *, crypto_data_t *, crypto_data_t *,
    crypto_req_handle_t);
static int ncp_sign_recover_atomic(crypto_provider_handle_t,
    crypto_session_id_t, crypto_mechanism_t *, crypto_key_t *, crypto_data_t *,
    crypto_data_t *, crypto_spi_ctx_template_t, crypto_req_handle_t);

static crypto_sign_ops_t ncp_sign_ops = {
	ncp_sign_init,
	ncp_sign,
	NULL,
	NULL,
	ncp_sign_atomic,
	ncp_sign_recover_init,
	ncp_sign_recover,
	ncp_sign_recover_atomic
};

static int ncp_verify_init(crypto_ctx_t *, crypto_mechanism_t *,
    crypto_key_t *, crypto_spi_ctx_template_t, crypto_req_handle_t);
static int ncp_verify(crypto_ctx_t *, crypto_data_t *, crypto_data_t *,
    crypto_req_handle_t);
static int ncp_verify_atomic(crypto_provider_handle_t, crypto_session_id_t,
    crypto_mechanism_t *, crypto_key_t *, crypto_data_t *,
    crypto_data_t *, crypto_spi_ctx_template_t, crypto_req_handle_t);
static int ncp_verify_recover_init(crypto_ctx_t *, crypto_mechanism_t *,
    crypto_key_t *, crypto_spi_ctx_template_t, crypto_req_handle_t);
static int ncp_verify_recover(crypto_ctx_t *, crypto_data_t *,
    crypto_data_t *, crypto_req_handle_t);
static int ncp_verify_recover_atomic(crypto_provider_handle_t,
    crypto_session_id_t, crypto_mechanism_t *, crypto_key_t *, crypto_data_t *,
    crypto_data_t *, crypto_spi_ctx_template_t, crypto_req_handle_t);

static crypto_verify_ops_t ncp_verify_ops = {
	ncp_verify_init,
	ncp_verify,
	NULL,
	NULL,
	ncp_verify_atomic,
	ncp_verify_recover_init,
	ncp_verify_recover,
	ncp_verify_recover_atomic
};

static int ncp_allocate_mechanism(crypto_provider_handle_t,
    crypto_mechanism_t *, crypto_mechanism_t *, int *error, int);
static int ncp_free_mechanism(crypto_provider_handle_t,
    crypto_mechanism_t *);

static crypto_mech_ops_t ncp_mech_ops = {
	ncp_allocate_mechanism,		/* copyin_mechanism */
	NULL,				/* copyout_mechanism */
	ncp_free_mechanism		/* free_mechanism */
};


static int ext_info(crypto_provider_handle_t prov,
    crypto_provider_ext_info_t *ext_info, crypto_req_handle_t cfreq);

static crypto_provider_management_ops_t ncp_provmanage_ops = {
	ext_info,		/* ext_info */
	NULL,			/* init_token */
	NULL,			/* init_pin */
	NULL			/* set_pin */
};

static crypto_ctx_ops_t ncp_ctx_ops = {
	NULL,
	ncp_free_context
};

static int ncp_nostore_key_generate_pair(crypto_provider_handle_t,
    crypto_session_id_t, crypto_mechanism_t *, crypto_object_attribute_t *,
    uint_t, crypto_object_attribute_t *, uint_t, crypto_object_attribute_t *,
    uint_t, crypto_object_attribute_t *, uint_t, crypto_req_handle_t);
static int ncp_nostore_key_derive(crypto_provider_handle_t,
    crypto_session_id_t, crypto_mechanism_t *, crypto_key_t *,
    crypto_object_attribute_t *, uint_t, crypto_object_attribute_t *,
    uint_t, crypto_req_handle_t);

static crypto_nostore_key_ops_t ncp_nostore_key_ops = {
	NULL,
	ncp_nostore_key_generate_pair,
	ncp_nostore_key_derive
};

/* Operations for the asymmetric cipher provider */
static crypto_ops_t ncp_crypto_ops = {
	&ncp_control_ops,
	NULL,				/* digest_ops */
	&ncp_cipher_ops,
	NULL,				/* mac_ops */
	&ncp_sign_ops,
	&ncp_verify_ops,
	NULL,				/* dual_ops */
	NULL,				/* cipher_mac_ops */
	NULL,				/* random_ops */
	NULL,				/* session_ops */
	NULL,				/* object_ops */
	NULL,				/* key_ops */
	&ncp_provmanage_ops,		/* management_ops */
	&ncp_ctx_ops,
	&ncp_mech_ops,				/* mech_ops */
	&ncp_nostore_key_ops
};

/* Provider information for the asymmetric cipher provider */
static crypto_provider_info_t ncp_prov_info = {
	CRYPTO_SPI_VERSION_3,
	NULL,				/* pi_provider_description */
	CRYPTO_HW_PROVIDER,
	NULL,				/* pi_provider_dev */
	NULL,				/* pi_provider_handle */
	&ncp_crypto_ops,
	sizeof (ncp_mech_info_table)/sizeof (crypto_mech_info_t),
	ncp_mech_info_table,
	0,				/* pi_logical_provider_count */
	NULL				/* pi_logical_providers */
};

/* Convenience macros */
/* Retrieve the softc and instance number from a SPI crypto context */
#define	NCP_SOFTC_FROM_CTX(ctx, softc, instance) {		\
	(softc) = (ncp_t *)(ctx)->cc_provider;			\
	(instance) = ddi_get_instance((softc)->n_dip);	\
}

#define	NCP_MECH_FROM_CTX(ctx) \
	(((ncp_request_t *)(ctx)->cc_provider_private)->nr_ctx_cm_type)

/*
 * Timeout value for waiting for a job completion
 */
int	ncp_jobtimeout_secs = 120;


/*
 * Setup and also register to kCF
 */
int
ncp_init(ncp_t *ncp)
{
	int		ret;
	dev_info_t	*dip;

	DBG0(ncp, DENTRY, "ncp_init: started");

	dip = ncp->n_dip;
	ncp->n_pagesize = ddi_ptob(dip, 1);

	/* initialize kstats */
	ncp_ksinit(ncp);

	if (ncp->n_maumap.m_nmaus_online) {
		/* register with the crypto framework */
		if ((ret = ncp_provider_register(ncp)) != 0) {
			cmn_err(CE_WARN,
			    "crypto_register_provider() failed (%d)", ret);
			ncp_ksdeinit(ncp);
			return (DDI_FAILURE);
		}
		ncp_provider_notify_ready(ncp);
	}

	DBG0(ncp, DENTRY, "ncp_init: done");

	return (DDI_SUCCESS);
}

/*
 * Unregister from kCF and cleanup
 */
int
ncp_uninit(ncp_t *ncp)
{
	DBG0(ncp, DENTRY, "ncp_uninit: start");

	/*
	 * Unregister from kCF.
	 * This needs to be done at the beginning of detach.
	 */
	if (ncp_provider_unregister(ncp) != CRYPTO_SUCCESS) {
		ncp_error(ncp, "unable to unregister from kcf");
		return (DDI_FAILURE);
	}

	/* deinitialize kstats */
	ncp_ksdeinit(ncp);


	DBG0(ncp, DENTRY, "ncp_uninit: done");

	return (DDI_SUCCESS);
}

int
ncp_provider_register(ncp_t *ncp)
{
	int		ret;
	char		ID[64];
	dev_info_t	*dip = ncp->n_dip;

	if (ncp_isregistered(ncp)) {
		return (CRYPTO_SUCCESS);
	}

	/* register with the crypto framework */
	/* Be careful not to exceed 32 chars */
	(void) sprintf(ID, "%s/%d %s",
	    ddi_driver_name(dip), ddi_get_instance(dip), IDENT_ASYM);
	ncp_prov_info.pi_provider_description = ID;
	ncp_prov_info.pi_provider_dev.pd_hw = dip;
	ncp_prov_info.pi_provider_handle = ncp;

	ret = crypto_register_provider(&ncp_prov_info, &ncp->n_prov);
	if (ret != CRYPTO_SUCCESS) {
		return (CRYPTO_FAILED);
	}

	ncp_setregistered(ncp);

	return (CRYPTO_SUCCESS);
}

void
ncp_provider_notify_ready(ncp_t *ncp)
{
	crypto_provider_notification(ncp->n_prov, CRYPTO_PROVIDER_READY);
}

int
ncp_provider_unregister(ncp_t *ncp)
{
	int	rv;

	if (!ncp_isregistered(ncp)) {
		return (CRYPTO_SUCCESS);
	}

	if ((rv = crypto_unregister_provider(ncp->n_prov)) != CRYPTO_SUCCESS) {
		ncp_error(ncp, "unable to unregister from kcf");
		return (rv);
	}

	ncp->n_prov = NULL;
	ncp_clrregistered(ncp);

	return (CRYPTO_SUCCESS);
}


/*
 * Reverse a string of bytes from s1 into s2.  The reversal happens
 * from the tail of s1.  If len1 < len2, then null bytes will be
 * padded to the end of s2.  If len2 < len1, then (presumably null)
 * bytes will be dropped from the start of s1.
 *
 * The rationale here is that when s1 (source) is shorter, then we
 * are reversing from big-endian ordering, into device ordering, and
 * want to add some extra nulls to the tail (MSB) side of the device.
 *
 * Similarly, when s2 (dest) is shorter, then we are truncating what
 * are presumably null MSB bits from the device.
 *
 * There is an expectation when reversing from the device back into
 * big-endian, that the number of bytes to reverse and the target size
 * will match, and no truncation or padding occurs.
 */
void
ncp_reverse(void *s1, void *s2, int len1, int len2)
{
	caddr_t	src, dst;

	if (len1 == 0) {
		if (len2) {
			bzero(s2, len2);
		}
		return;
	}
	src = (caddr_t)s1 + len1 - 1;
	dst = s2;
	while ((src >= (caddr_t)s1) && (len2)) {
		*dst++ = *src--;
		len2--;
	}
	while (len2 > 0) {
		*dst++ = 0;
		len2--;
	}
}


ncp_request_t *
ncp_getreq(ncp_t *ncp, int tryhard)
{
	ncp_request_t	*reqp;
	int		flag = KM_NOSLEEP;

	if (tryhard) {
		flag = KM_SLEEP;
	}

	reqp = kmem_cache_alloc(ncp->n_request_cache, flag);

	if (reqp) {
		/*
		 * this is a default value, and may be overridden
		 * by specific algorithms or commands.
		 */
		reqp->nr_timeout = drv_usectohz(ncp_jobtimeout_secs * SECOND);
	}

	return (reqp);
}

void
ncp_freereq(ncp_request_t *reqp)
{
	ncp_t *ncp = reqp->nr_ncp;

	reqp->nr_kcf_req = NULL;

	kmem_cache_free(ncp->n_request_cache, reqp);
}

/*
 * Schedule some work.
 */
int
ncp_start(ncp_t *ncp, ncp_request_t *reqp)
{
	int		rv;
	int		mid;
	mau_entry_t	*mep;
	ncp_mau2cpu_map_t	*m2cp =  &ncp->n_maumap;

	/* guard against DR changes */
	mutex_enter(&m2cp->m_lock);

	if (reqp->nr_kcf_req == NCP_FIPS_POST_REQUEST) {
		mid = ncp_fips_post_mid;
	} else {
		/* choose the MAU */
		mid = ncp_map_nextmau(ncp);

		if (mid < 0) {
			mutex_exit(&m2cp->m_lock);
			DBG0(ncp, DWARN, "ncp_start unable to find a MAU");
			return (CRYPTO_FAILED);
		}
	}
	mep = ncp_map_holdmau(ncp, mid);

	mutex_exit(&m2cp->m_lock);

	if (!mep) {
		DBG0(ncp, DWARN, "ncp_start unable to find a MAU(2)");
		return (CRYPTO_FAILED);
	}

	reqp->nr_mep = mep;

	ASSERT(mep->mm_taskq != 0);

	rv = ddi_taskq_dispatch(mep->mm_taskq, ncp_start_task,
	    (void *)reqp, DDI_SLEEP);

	return ((rv == DDI_SUCCESS) ? CRYPTO_QUEUED : CRYPTO_FAILED);
}

/*
 * Schedule some work.
 */
static void
ncp_start_task(void * targ)
{
	int		rv;
	ncp_request_t	*reqp = (ncp_request_t *)targ;
	ncp_t		*ncp = reqp->nr_ncp;
	int		tmp_bind = 0;
	processorid_t	cid;

	/* grab a cpu/bind worker thread */
	if ((cid = ncp_bind_worker(ncp, reqp->nr_mep, curthread)) < 0) {
		DBG0(ncp, DWARN, "ncp_start_task: unable to bind worker!");

		if (reqp->nr_kcf_req == NCP_FIPS_POST_REQUEST) {
			/*
			 * If this is a POST request, do not reschedule it
			 * to a different MAU
			 */
			ncp_map_relemau(reqp->nr_mep);
			reqp->nr_mep = NULL;
			reqp->nr_callback(reqp, CRYPTO_DEVICE_ERROR);
			return;
		}

		mutex_enter(&ncp->n_maumap.m_lock);

		/* unable to find a cpu for the targeted mau - pick another */
		ncp_map_relemau(reqp->nr_mep);
		reqp->nr_mep = NULL;
		tmp_bind = 1;
		if ((reqp->nr_mep = ncp_remap_mau(ncp)) == NULL) {
			mutex_exit(&ncp->n_maumap.m_lock);
			DBG0(ncp, DWARN,
			    "ncp_start_task: NO MAU FOUND!");
			reqp->nr_callback(reqp, CRYPTO_DEVICE_ERROR);
			return;
		}
		/* find a cpu for this MAU */
		cid = ncp_map_mau_to_cpu(ncp, reqp->nr_mep->mm_mauid, 1);
		if (cid < 0) {
			DBG1(ncp, DMD,
			    "ncp_start_task: NO CPU FOUND:mau(%d)!",
			    reqp->nr_mep->mm_mauid);
			ncp_map_relemau(reqp->nr_mep);
			mutex_exit(&ncp->n_maumap.m_lock);
			reqp->nr_mep = NULL;
			reqp->nr_callback(reqp, CRYPTO_DEVICE_ERROR);
			return;
		}
		mutex_exit(&ncp->n_maumap.m_lock);
	}

	switch (reqp->nr_ctx_cm_type) {
	case RSA_X_509_MECH_INFO_TYPE:
	case RSA_PKCS_MECH_INFO_TYPE:
		if (reqp->nr_job_stat == DS_RSAPRIVATE) {
			rv = ncp_rsa_private_process(ncp, reqp);
		} else if (reqp->nr_job_stat == DS_RSAPUBLIC) {
			rv = ncp_rsa_public_process(ncp, reqp);
		} else {
			rv = CRYPTO_ARGUMENTS_BAD;
		}
		break;
	case DH_PKCS_KEY_PAIR_GEN_MECH_INFO_TYPE:
		rv = ncp_dh_generate_process(ncp, reqp);
		break;
	case DH_PKCS_DERIVE_MECH_INFO_TYPE:
		rv = ncp_dh_derive_process(ncp, reqp);
		break;
	case EC_KEY_PAIR_GEN_MECH_INFO_TYPE:
		rv = ncp_ec_generate_process(ncp, reqp);
		break;
	case ECDH1_DERIVE_MECH_INFO_TYPE:
		rv = ncp_ecdh_derive_process(ncp, reqp);
		break;
	case ECDSA_MECH_INFO_TYPE:
		if (reqp->nr_job_stat == DS_ECDSASIGN) {
			rv = ncp_ecdsa_sign_process(ncp, reqp);
		} else if (reqp->nr_job_stat == DS_ECDSAVERIFY) {
			rv = ncp_ecdsa_verify_process(ncp, reqp);
		} else {
			rv = CRYPTO_ARGUMENTS_BAD;
		}
		break;
	case RSA_PKCS_KEY_PAIR_GEN_MECH_INFO_TYPE:
		rv = ncp_rsa_generate_process(ncp, reqp);
		break;
	case DSA_MECH_INFO_TYPE:
		if (reqp->nr_job_stat == DS_DSASIGN) {
			rv = ncp_dsa_sign_process(ncp, reqp);
		} else if (reqp->nr_job_stat == DS_DSAVERIFY) {
			rv = ncp_dsa_verify_process(ncp, reqp);
		} else {
			rv = CRYPTO_ARGUMENTS_BAD;
		}
		break;
	default:
		rv = CRYPTO_ARGUMENTS_BAD;
	}

	if (rv == CRYPTO_SUCCESS) {
		/* Update bytes processed */
		if (reqp->nr_byte_stat >= 0) {
			ncp->n_stats[reqp->nr_byte_stat] +=
			    reqp->nr_pkt_length;
		}

		/* Update jobs processed */
		if (reqp->nr_job_stat >= 0) {
			ncp->n_stats[reqp->nr_job_stat]++;
		}
	}

	/*
	 * release cpu, grabbed by either ncp_bind_worker()
	 * or ncp_map_mau_to_cpu().
	 */
	ncp_unmap_mau_to_cpu(ncp, cid, tmp_bind);

	if (reqp->nr_mep) {
		ncp_map_relemau(reqp->nr_mep);
		reqp->nr_mep = NULL;
	}

	reqp->nr_callback(reqp, rv);
}

int
ncp_length(crypto_data_t *cdata)
{
	return (cdata->cd_length);
}

/*
 * Performs the input, output or hard scatter/gather checks on the specified
 * crypto_data_t struct. Returns true if the data is scatter/gather in nature
 * ie fails the test.
 */
int
ncp_sgcheck(ncp_t *ncp, crypto_data_t *data, ncp_sg_param_t val)
{
	uio_t *uiop;
	mblk_t *mp;
	int rv = FALSE;

/* EXPORT DELETE START */

	if (CRYPTO_DATA_IS_USERSPACE(data))
		return (TRUE);

	switch (val) {
	case NCP_SG_CONTIG:
		/*
		 * Check for a contiguous data buffer.
		 */
		switch (data->cd_format) {
		case CRYPTO_DATA_RAW:
			/* Contiguous in nature */
			break;

		case CRYPTO_DATA_UIO:
			if (data->cd_uio->uio_iovcnt > 1)
				rv = TRUE;
			break;

		case CRYPTO_DATA_MBLK:
			mp = data->cd_mp;
			if (mp->b_cont != NULL)
				rv = TRUE;
			break;

		default:
			DBG0(NULL, DWARN, "unrecognised crypto data format");
		}
		break;

	case NCP_SG_WALIGN:
		/*
		 * Check for a contiguous data buffer that is 32-bit word
		 * aligned and is of word multiples in size.
		 */
		switch (data->cd_format) {
		case CRYPTO_DATA_RAW:
			if ((data->cd_raw.iov_len % sizeof (uint32_t)) ||
			    ((uintptr_t)data->cd_raw.iov_base %
			    sizeof (uint32_t))) {
				rv = TRUE;
			}
			break;

		case CRYPTO_DATA_UIO:
			uiop = data->cd_uio;
			if (uiop->uio_iovcnt > 1) {
				return (TRUE);
			}
			/* So there is only one iovec */
			if ((uiop->uio_iov[0].iov_len % sizeof (uint32_t)) ||
			    ((uintptr_t)uiop->uio_iov[0].iov_base %
			    sizeof (uint32_t))) {
				rv = TRUE;
			}
			break;

		case CRYPTO_DATA_MBLK:
			mp = data->cd_mp;
			if (mp->b_cont != NULL) {
				return (TRUE);
			}
			/* So there is only one mblk in the chain */
			if ((N_MBLKL(mp) % sizeof (uint32_t)) ||
			    ((uintptr_t)mp->b_rptr % sizeof (uint32_t))) {
				rv = TRUE;
			}
			break;

		default:
			DBG0(NULL, DWARN, "unrecognised crypto data format");
		}
		break;

	case NCP_SG_PALIGN:
		/*
		 * Check that the data buffer is page aligned and is of
		 * page multiples in size.
		 */
		switch (data->cd_format) {
		case CRYPTO_DATA_RAW:
			if ((data->cd_length % ncp->n_pagesize) ||
			    ((uintptr_t)data->cd_raw.iov_base %
			    ncp->n_pagesize)) {
				rv = TRUE;
			}
			break;

		case CRYPTO_DATA_UIO:
			uiop = data->cd_uio;
			if ((uiop->uio_iov[0].iov_len % ncp->n_pagesize) ||
			    ((uintptr_t)uiop->uio_iov[0].iov_base %
			    ncp->n_pagesize)) {
				rv = TRUE;
			}
			break;

		case CRYPTO_DATA_MBLK:
			mp = data->cd_mp;
			if ((N_MBLKL(mp) % ncp->n_pagesize) ||
			    ((uintptr_t)mp->b_rptr % ncp->n_pagesize)) {
				rv = TRUE;
			}
			break;

		default:
			DBG0(NULL, DWARN, "unrecognised crypto data format");
		}
		break;

	default:
		DBG0(NULL, DWARN, "unrecognised scatter/gather param type");
	}

/* EXPORT DELETE END */

	return (rv);
}

/*
 * Increments the cd_offset and decrements the cd_length as the data is
 * gathered from the crypto_data_t struct.
 * The data is reverse-copied into the dest buffer if the flag is true.
 */
int
ncp_gather(crypto_data_t *in, char *dest, int count, int reverse)
{
	int	rv = CRYPTO_SUCCESS;
	uint_t	vec_idx;
	uio_t	*uiop;
	off_t	off = in->cd_offset;
	size_t	cur_len;
	mblk_t	*mp;

/* EXPORT DELETE START */

	switch (in->cd_format) {
	case CRYPTO_DATA_RAW:
		if (count > in->cd_length) {
			/*
			 * The caller specified a length greater than the
			 * size of the buffer.
			 */
			return (CRYPTO_DATA_LEN_RANGE);
		}
		if (reverse)
			ncp_reverse((char *)in->cd_raw.iov_base + off,
			    dest, count, count);
		else
			bcopy((char *)in->cd_raw.iov_base + in->cd_offset,
			    dest, count);
		in->cd_offset += count;
		in->cd_length -= count;
		break;

	case CRYPTO_DATA_UIO:
		/*
		 * Jump to the first iovec containing data to be processed.
		 */
		uiop = in->cd_uio;
		vec_idx = 0;
		while (vec_idx < uiop->uio_iovcnt &&
		    off >= uiop->uio_iov[vec_idx].iov_len) {
			off -= uiop->uio_iov[vec_idx++].iov_len;
		}
		if (vec_idx == uiop->uio_iovcnt) {
			/*
			 * The caller specified an offset that is larger than
			 * the total size of the buffers it provided.
			 */
			return (CRYPTO_DATA_LEN_RANGE);
		}

		/*
		 * Now process the iovecs.
		 */
		while (vec_idx < uiop->uio_iovcnt && count > 0) {
			cur_len = min(uiop->uio_iov[vec_idx].iov_len -
			    off, count);
			count -= cur_len;
			if (reverse) {
				/* Fill the dest buffer from the end */
				ncp_reverse(
				    (char *)uiop->uio_iov[vec_idx].iov_base +
				    off, dest+count, cur_len, cur_len);
			} else {
				(void) ddi_copyin(
				    (char *)uiop->uio_iov[vec_idx].iov_base +
				    off, dest, cur_len,
				    uiop->uio_segflg == UIO_SYSSPACE ?
				    FKIOCTL : 0);
				dest += cur_len;
			}
			in->cd_offset += cur_len;
			in->cd_length -= cur_len;
			vec_idx++;
			off = 0;
		}

		if (vec_idx == uiop->uio_iovcnt && count > 0) {
			/*
			 * The end of the specified iovec's was reached but
			 * the length requested could not be processed
			 * (requested to digest more data than it provided).
			 */
			return (CRYPTO_DATA_LEN_RANGE);
		}
		break;

	case CRYPTO_DATA_MBLK:
		/*
		 * Jump to the first mblk_t containing data to be processed.
		 */
		mp = in->cd_mp;
		while (mp != NULL && off >= N_MBLKL(mp)) {
			off -= N_MBLKL(mp), mp = mp->b_cont;
		}
		if (mp == NULL) {
			/*
			 * The caller specified an offset that is larger than
			 * the total size of the buffers it provided.
			 */
			return (CRYPTO_DATA_LEN_RANGE);
		}

		/*
		 * Now do the processing on the mblk chain.
		 */
		while (mp != NULL && count > 0) {
			cur_len = min(N_MBLKL(mp) - off, count);
			count -= cur_len;
			if (reverse) {
				/* Fill the dest buffer from the end */
				ncp_reverse((char *)(mp->b_rptr + off),
				    dest+count, cur_len, cur_len);
			} else {
				bcopy((char *)(mp->b_rptr + off), dest,
				    cur_len);
				dest += cur_len;
			}
			in->cd_offset += cur_len;
			in->cd_length -= cur_len;
			mp = mp->b_cont;
			off = 0;
		}

		if (mp == NULL && count > 0) {
			/*
			 * The end of the mblk was reached but the length
			 * requested could not be processed, (requested to
			 * digest more data than it provided).
			 */
			return (CRYPTO_DATA_LEN_RANGE);
		}
		break;

	default:
		DBG0(NULL, DWARN,
		    "ncp_gather: unrecognised crypto data format");
		rv = CRYPTO_ARGUMENTS_BAD;
	}

/* EXPORT DELETE END */

	return (rv);
}


/*
 * Appends the data to the crypto_data_t struct increasing cd_length.
 * cd_offset is left unchanged.
 * Data is reverse-copied if the flag is TRUE.
 */
int
ncp_scatter(const char *src, crypto_data_t *out, int count, int reverse)
{
	int	rv = CRYPTO_SUCCESS;
	off_t	offset = out->cd_offset + out->cd_length;
	uint_t	vec_idx;
	uio_t	*uiop;
	size_t	cur_len;
	mblk_t	*mp;

/* EXPORT DELETE START */

	switch (out->cd_format) {
	case CRYPTO_DATA_RAW:
		if (out->cd_raw.iov_len - offset < count) {
			/* Trying to write out more than space available. */
			return (CRYPTO_DATA_LEN_RANGE);
		}
		if (reverse)
			ncp_reverse((void *)src,
			    (char *)out->cd_raw.iov_base + offset,
			    count, count);
		else
			bcopy(src, (char *)out->cd_raw.iov_base + offset,
			    count);
		out->cd_length += count;
		break;

	case CRYPTO_DATA_UIO:
		/*
		 * Jump to the first iovec that can be written to.
		 */
		uiop = out->cd_uio;
		vec_idx = 0;
		while (vec_idx < uiop->uio_iovcnt &&
		    offset >= uiop->uio_iov[vec_idx].iov_len) {
			offset -= uiop->uio_iov[vec_idx++].iov_len;
		}
		if (vec_idx == uiop->uio_iovcnt) {
			/*
			 * The caller specified an offset that is larger than
			 * the total size of the buffers it provided.
			 */
			return (CRYPTO_DATA_LEN_RANGE);
		}

		/*
		 * Now process the iovecs.
		 */
		while (vec_idx < uiop->uio_iovcnt && count > 0) {
			cur_len = min(uiop->uio_iov[vec_idx].iov_len -
			    offset, count);
			count -= cur_len;
			if (reverse) {
				ncp_reverse((void*) (src+count),
				    (char *)uiop->uio_iov[vec_idx].iov_base +
				    offset, cur_len, cur_len);
			} else {
				(void) ddi_copyout(src,
				    (char *)uiop->uio_iov[vec_idx].iov_base +
				    offset, cur_len,
				    uiop->uio_segflg == UIO_SYSSPACE ?
				    FKIOCTL : 0);
				src += cur_len;
			}
			out->cd_length += cur_len;
			vec_idx++;
			offset = 0;
		}

		if (vec_idx == uiop->uio_iovcnt && count > 0) {
			/*
			 * The end of the specified iovec's was reached but
			 * the length requested could not be processed
			 * (requested to write more data than space provided).
			 */
			return (CRYPTO_DATA_LEN_RANGE);
		}
		break;

	case CRYPTO_DATA_MBLK:
		/*
		 * Jump to the first mblk_t that can be written to.
		 */
		mp = out->cd_mp;
		while (mp != NULL && offset >= N_MBLKL(mp)) {
			offset -= N_MBLKL(mp);
			mp = mp->b_cont;
		}
		if (mp == NULL) {
			/*
			 * The caller specified an offset that is larger than
			 * the total size of the buffers it provided.
			 */
			return (CRYPTO_DATA_LEN_RANGE);
		}

		/*
		 * Now do the processing on the mblk chain.
		 */
		while (mp != NULL && count > 0) {
			cur_len = min(N_MBLKL(mp) - offset, count);
			count -= cur_len;
			if (reverse) {
				ncp_reverse((void*) (src+count),
				    (char *)(mp->b_rptr + offset), cur_len,
				    cur_len);
			} else {
				bcopy(src, (char *)(mp->b_rptr + offset),
				    cur_len);
				src += cur_len;
			}
			mp->b_wptr += cur_len;
			out->cd_length += cur_len;
			mp = mp->b_cont;
			offset = 0;
		}

		if (mp == NULL && count > 0) {
			/*
			 * The end of the mblk was reached but the length
			 * requested could not be processed, (requested to
			 * digest more data than it provided).
			 */
			return (CRYPTO_DATA_LEN_RANGE);
		}
		break;

	default:
		DBG0(NULL, DWARN, "unrecognised crypto data format");
		rv = CRYPTO_ARGUMENTS_BAD;
	}

/* EXPORT DELETE END */

	return (rv);
}

/*
 * This compares to bignums (in big-endian order).  It ignores leading
 * null bytes.  The result semantics follow bcmp, mempcmp, strcmp, etc.
 */
int
ncp_numcmp(caddr_t n1, int n1len, caddr_t n2, int n2len)
{

/* EXPORT DELETE START */

	while ((n1len > 1) && (*n1 == 0)) {
		n1len--;
		n1++;
	}
	while ((n2len > 1) && (*n2 == 0)) {
		n2len--;
		n2++;
	}
	if (n1len != n2len) {
		return (n1len - n2len);
	}
	while ((n1len > 1) && (*n1 == *n2)) {
		n1++;
		n2++;
		n1len--;
	}

/* EXPORT DELETE END */

	return ((int)(*(uchar_t *)n1) - (int)(*(uchar_t *)n2));
}

/*
 * Copy the bignum value into the specified attribute value;
 * return false on failure.
 */
boolean_t
ncp_bignum_to_attr(crypto_attr_type_t type, crypto_object_attribute_t *attrs,
    uint_t count, BIGNUM *big, uint32_t len)
{
	crypto_object_attribute_t *ap;

/* EXPORT DELETE START */

	ap = ncp_find_attribute(attrs, count, type);
	if (ap == NULL) {
		return (B_FALSE);
	}

	if (ap->oa_value == NULL || ap->oa_value_len == 0) {
		DBG0(NULL, DWARN, "ncp_bignum_to_attr: "
		    "NULL attribute value or zero length\n");
		return (B_FALSE);
	}

	if (ap->oa_value_len < len) {
		DBG2(NULL, DWARN, "ncp_bignum_to_attr: "
		    "attribute buffer too small (%d < %d)\n",
		    ap->oa_value_len, len);
		return (B_FALSE);
	}

	/*
	 * Copy value in bignum format to crypto_object_attribute structure
	 * where it is stored in byte-big-endian format.
	 */
	ncp_bignum2kcl((uchar_t *)ap->oa_value, big, len);
	ap->oa_value_len = len;

/* EXPORT DELETE END */

	return (B_TRUE);
}

/*
 * Return array of key attributes.
 */
crypto_object_attribute_t *
ncp_get_key_attr(crypto_key_t *key)
{

/* EXPORT DELETE START */

	if ((key->ck_format != CRYPTO_KEY_ATTR_LIST) ||
	    (key->ck_count == 0)) {
		return (NULL);
	}

/* EXPORT DELETE END */

	return (key->ck_attrs);
}

/*
 * If attribute type exists data contains the start address of the value,
 * and numelems contains it's length.
 */
int
ncp_attr_lookup_uint8_array(crypto_object_attribute_t *attrp, uint_t atnum,
    uint64_t atype, void **data, unsigned int *numelems)
{
	crypto_object_attribute_t	*bap;

/* EXPORT DELETE START */

	bap = ncp_find_attribute(attrp, atnum, atype);
	if (bap == NULL) {
		return (CRYPTO_ATTRIBUTE_TYPE_INVALID);
	}

	*data = bap->oa_value;
	*numelems = bap->oa_value_len;

/* EXPORT DELETE END */

	return (CRYPTO_SUCCESS);
}

/*
 * If attribute type exists data contains the start address of the value,
 * and numelems contains it's length.
 */
int
ncp_attr_lookup_uint32(crypto_object_attribute_t *attrp, uint_t atnum,
    uint64_t atype, uint32_t *val)
{
	crypto_object_attribute_t	*bap;

/* EXPORT DELETE START */

	bap = ncp_find_attribute(attrp, atnum, atype);
	if (bap == NULL) {
		return (CRYPTO_ATTRIBUTE_TYPE_INVALID);
	}

	switch (bap->oa_value_len) {
	case 4:
		/* LINTED */
		*val = *(uint32_t *)bap->oa_value;
		return (CRYPTO_SUCCESS);
	case 8:
		/* LINTED */
		*val = *(uint64_t *)bap->oa_value;
		return (CRYPTO_SUCCESS);
	default:
		break;
	}

/* EXPORT DELETE END */

	return (CRYPTO_ATTRIBUTE_VALUE_INVALID);
}

/*
 * Finds entry of specified type. If it is not found ncp_find_attribute returns
 * NULL.
 */
crypto_object_attribute_t *
ncp_find_attribute(crypto_object_attribute_t *attrp, uint_t atnum,
    uint64_t atype)
{

/* EXPORT DELETE START */

	while (atnum) {
		if (attrp->oa_type == atype)
			return (attrp);
		atnum--;
		attrp++;
	}

/* EXPORT DELETE END */

	return (NULL);
}

/*
 * Return the address of the first data buffer. If the data format is
 * unrecognised return NULL.
 * The single buffer has been assured in the calling function.
 */
caddr_t
ncp_bufdaddr(crypto_data_t *data)
{

/* EXPORT DELETE START */

	switch (data->cd_format) {
	case CRYPTO_DATA_RAW:
		return ((char *)data->cd_raw.iov_base + data->cd_offset);
	case CRYPTO_DATA_UIO:
		ASSERT(data->cd_uio->uio_iovcnt == 1);
		return ((char *)data->cd_uio->uio_iov[0].iov_base +
		    data->cd_offset);
	case CRYPTO_DATA_MBLK:
		ASSERT(data->cd_mp->b_cont == NULL);
		return ((char *)data->cd_mp->b_rptr + data->cd_offset);
	default:
		DBG0(NULL, DWARN,
		    "ncp_bufdaddr: unrecognised crypto data format");
		break;
	}

/* EXPORT DELETE END */

	return (NULL);
}

/*
 * Control entry points.
 */

/* ARGSUSED */
static void
ncp_provider_status(crypto_provider_handle_t provider, uint_t *status)
{

/* EXPORT DELETE START */

	/*
	 * We should provide real state of the NCP hardware if we can
	 * detect hardware is busy of the taskq is full. This should
	 * be investigated in NCP2.
	 */
	*status = CRYPTO_PROVIDER_READY;

/* EXPORT DELETE END */

}

/* ARGSUSED */
static int
ncp_nostore_key_generate_pair(crypto_provider_handle_t provider,
    crypto_session_id_t session_id, crypto_mechanism_t *mechanism,
    crypto_object_attribute_t *in_pub_attrs, uint_t in_pub_attr_count,
    crypto_object_attribute_t *out_pub_attrs, uint_t out_pub_attr_count,
    crypto_object_attribute_t *in_pri_attrs, uint_t in_pri_attr_count,
    crypto_object_attribute_t *out_pri_attrs, uint_t out_pri_attr_count,
    crypto_req_handle_t req)
{
	int error = CRYPTO_FAILED;
#ifdef DEBUG
	ncp_t *softc = (ncp_t *)provider;
#endif /* DEBUG */

/* EXPORT DELETE START */

	DBG0(softc, DENTRY, "ncp_nostore_key_generate_pair: started");

	/* check mechanism */
	switch (mechanism->cm_type) {
	case DH_PKCS_KEY_PAIR_GEN_MECH_INFO_TYPE:
		error = ncp_dh_generate_key(provider, session_id, mechanism,
		    in_pub_attrs, in_pub_attr_count, out_pub_attrs,
		    out_pub_attr_count, in_pri_attrs, in_pri_attr_count,
		    out_pri_attrs, out_pri_attr_count, KM_SLEEP, req);
		break;
	case EC_KEY_PAIR_GEN_MECH_INFO_TYPE:
		error = ncp_ec_generate_key(provider, session_id, mechanism,
		    in_pub_attrs, in_pub_attr_count, out_pub_attrs,
		    out_pub_attr_count, in_pri_attrs, in_pri_attr_count,
		    out_pri_attrs, out_pri_attr_count, req);
		break;
	case RSA_PKCS_KEY_PAIR_GEN_MECH_INFO_TYPE:
		error = ncp_rsa_generate_key(provider, session_id, mechanism,
		    in_pub_attrs, in_pub_attr_count, out_pub_attrs,
		    out_pub_attr_count, in_pri_attrs, in_pri_attr_count,
		    out_pri_attrs, out_pri_attr_count, KM_SLEEP, req);
		break;
	default:
		cmn_err(CE_WARN, "ncp_nostore_key_generate_pair: "
		    "unexpected mech type 0x%llx\n",
		    (unsigned long long)mechanism->cm_type);
		error = CRYPTO_MECHANISM_INVALID;
		goto out;
	}

	DBG1(softc, DENTRY, "ncp_nostore_key_generate_pair: done, err = 0x%x",
	    error);
out:

/* EXPORT DELETE END */

	return (error);
}

/* ARGSUSED */
static int
ncp_nostore_key_derive(crypto_provider_handle_t provider,
    crypto_session_id_t session_id, crypto_mechanism_t *mechanism,
    crypto_key_t *key, crypto_object_attribute_t *in_attrs,
    uint_t in_attr_count, crypto_object_attribute_t *out_attrs,
    uint_t out_attr_count, crypto_req_handle_t req)
{
	int error = CRYPTO_FAILED;
#ifdef DEBUG
	ncp_t *softc = (ncp_t *)provider;
#endif /* DEBUG */

/* EXPORT DELETE START */

	DBG0(softc, DENTRY, "ncp_nostore_key_derive: started");

	switch (mechanism->cm_type) {
	case DH_PKCS_DERIVE_MECH_INFO_TYPE:
		error = ncp_dh_derive(provider, session_id, mechanism, key,
		    in_attrs, in_attr_count, out_attrs, out_attr_count,
		    KM_SLEEP, req);
		break;
	case ECDH1_DERIVE_MECH_INFO_TYPE:
		error = ncp_ecdh_derive(provider, session_id, mechanism, key,
		    in_attrs, in_attr_count, out_attrs, out_attr_count, req);
		break;
	default:
		cmn_err(CE_WARN, "ncp_nostore_key_derive: "
		    "unexpected mech type 0x%llx\n",
		    (unsigned long long)mechanism->cm_type);
		error = CRYPTO_MECHANISM_INVALID;
		goto out;
	}

	DBG1(softc, DENTRY, "ncp_nostore_key_derive: done, err = 0x%x", error);
out:

/* EXPORT DELETE END */

	return (error);
}

/*
 * Cipher (encrypt/decrypt) entry points.
 */

/* ARGSUSED */
static int
ncp_encrypt_init(crypto_ctx_t *ctx, crypto_mechanism_t *mechanism,
    crypto_key_t *key, crypto_spi_ctx_template_t ctx_template,
    crypto_req_handle_t req)
{
	int error = CRYPTO_FAILED;
	ncp_t *softc;
	/* LINTED E_FUNC_SET_NOT_USED */
	int instance;

/* EXPORT DELETE START */

	/* extract softc and instance number from context */
	NCP_SOFTC_FROM_CTX(ctx, softc, instance);
	DBG0(softc, DENTRY, "ncp_encrypt_init: started");

	/* check mechanism */
	switch (mechanism->cm_type) {
	case RSA_PKCS_MECH_INFO_TYPE:
	case RSA_X_509_MECH_INFO_TYPE:
		error = ncp_rsainit(ctx, mechanism, key, KM_SLEEP);
		break;
	default:
		cmn_err(CE_WARN, "ncp_encrypt_init: unexpected mech type "
		    "0x%llx\n", (unsigned long long)mechanism->cm_type);
		error = CRYPTO_MECHANISM_INVALID;
	}

	DBG1(softc, DENTRY, "ncp_encrypt_init: done, err = 0x%x", error);


/* EXPORT DELETE END */

	return (error);
}

/* ARGSUSED */
static int
ncp_encrypt(crypto_ctx_t *ctx, crypto_data_t *plaintext,
    crypto_data_t *ciphertext, crypto_req_handle_t req)
{
	int error = CRYPTO_FAILED;
	ncp_t *softc;
	/* LINTED E_FUNC_SET_NOT_USED */
	int instance;

/* EXPORT DELETE START */

	if (!ctx || !ctx->cc_provider || !ctx->cc_provider_private)
		return (CRYPTO_OPERATION_NOT_INITIALIZED);

	/* extract softc and instance number from context */
	NCP_SOFTC_FROM_CTX(ctx, softc, instance);
	DBG0(softc, DENTRY, "ncp_encrypt: started");

	/* check mechanism */
	switch (NCP_MECH_FROM_CTX(ctx)) {
	case RSA_PKCS_MECH_INFO_TYPE:
	case RSA_X_509_MECH_INFO_TYPE:
		error = ncp_rsastart(ctx, plaintext, ciphertext, req,
		    NCP_RSA_ENC);
		break;
	default:
		/* Should never reach here */
		cmn_err(CE_WARN, "ncp_encrypt: unexpected mech type "
		    "0x%llx\n", (unsigned long long)NCP_MECH_FROM_CTX(ctx));
		error = CRYPTO_MECHANISM_INVALID;
	}

	if ((error != CRYPTO_QUEUED) && (error != CRYPTO_SUCCESS) &&
	    (error != CRYPTO_BUFFER_TOO_SMALL)) {
		ciphertext->cd_length = 0;
	}

	DBG1(softc, DENTRY, "ncp_encrypt: done, err = 0x%x", error);

/* EXPORT DELETE END */

	return (error);
}

/* ARGSUSED */
static int
ncp_encrypt_atomic(crypto_provider_handle_t provider,
    crypto_session_id_t session_id, crypto_mechanism_t *mechanism,
    crypto_key_t *key, crypto_data_t *plaintext, crypto_data_t *ciphertext,
    crypto_spi_ctx_template_t ctx_template, crypto_req_handle_t req)
{
	int error = CRYPTO_FAILED;
#ifdef DEBUG
	ncp_t *softc = (ncp_t *)provider;
#endif /* DEBUG */

/* EXPORT DELETE START */

	DBG0(softc, DENTRY, "ncp_encrypt_atomic: started");

	if (ctx_template != NULL)
		return (CRYPTO_ARGUMENTS_BAD);

	/* check mechanism */
	switch (mechanism->cm_type) {
	case RSA_PKCS_MECH_INFO_TYPE:
	case RSA_X_509_MECH_INFO_TYPE:
		error = ncp_rsaatomic(provider, session_id, mechanism, key,
		    plaintext, ciphertext, KM_SLEEP, req, NCP_RSA_ENC);
		break;
	default:
		cmn_err(CE_WARN, "ncp_encrypt_atomic: unexpected mech type "
		    "0x%llx\n", (unsigned long long)mechanism->cm_type);
		error = CRYPTO_MECHANISM_INVALID;
	}

	if ((error != CRYPTO_QUEUED) && (error != CRYPTO_SUCCESS)) {
		ciphertext->cd_length = 0;
	}

	DBG1(softc, DENTRY, "ncp_encrypt_atomic: done, err = 0x%x", error);

/* EXPORT DELETE END */

	return (error);
}

/* ARGSUSED */
static int
ncp_decrypt_init(crypto_ctx_t *ctx, crypto_mechanism_t *mechanism,
    crypto_key_t *key, crypto_spi_ctx_template_t ctx_template,
    crypto_req_handle_t req)
{
	int error = CRYPTO_FAILED;
	ncp_t *softc;
	/* LINTED E_FUNC_SET_NOT_USED */
	int instance;

/* EXPORT DELETE START */

	/* extract softc and instance number from context */
	NCP_SOFTC_FROM_CTX(ctx, softc, instance);
	DBG0(softc, DENTRY, "ncp_decrypt_init: started");

	/* check mechanism */
	switch (mechanism->cm_type) {
	case RSA_PKCS_MECH_INFO_TYPE:
	case RSA_X_509_MECH_INFO_TYPE:
		error = ncp_rsainit(ctx, mechanism, key, KM_SLEEP);
		break;
	default:
		cmn_err(CE_WARN, "ncp_decrypt_init: unexpected mech type "
		    "0x%llx\n", (unsigned long long)mechanism->cm_type);
		error = CRYPTO_MECHANISM_INVALID;
	}

	DBG1(softc, DENTRY, "ncp_decrypt_init: done, err = 0x%x", error);


/* EXPORT DELETE END */

	return (error);
}

/* ARGSUSED */
static int
ncp_decrypt(crypto_ctx_t *ctx, crypto_data_t *ciphertext,
    crypto_data_t *plaintext, crypto_req_handle_t req)
{
	int error = CRYPTO_FAILED;
	ncp_t *softc;
	/* LINTED E_FUNC_SET_NOT_USED */
	int instance;

/* EXPORT DELETE START */

	if (!ctx || !ctx->cc_provider || !ctx->cc_provider_private)
		return (CRYPTO_OPERATION_NOT_INITIALIZED);

	/* extract softc and instance number from context */
	NCP_SOFTC_FROM_CTX(ctx, softc, instance);
	DBG0(softc, DENTRY, "ncp_decrypt: started");

	/* check mechanism */
	switch (NCP_MECH_FROM_CTX(ctx)) {
	case RSA_PKCS_MECH_INFO_TYPE:
	case RSA_X_509_MECH_INFO_TYPE:
		error = ncp_rsastart(ctx, ciphertext, plaintext, req,
		    NCP_RSA_DEC);
		break;
	default:
		/* Should never reach here */
		cmn_err(CE_WARN, "ncp_decrypt: unexpected mech type "
		    "0x%llx\n", (unsigned long long)NCP_MECH_FROM_CTX(ctx));
		error = CRYPTO_MECHANISM_INVALID;
	}

	if ((error != CRYPTO_QUEUED) && (error != CRYPTO_SUCCESS) &&
	    (error != CRYPTO_BUFFER_TOO_SMALL)) {
		if (plaintext)
			plaintext->cd_length = 0;
	}

	DBG1(softc, DENTRY, "ncp_decrypt: done, err = 0x%x", error);

/* EXPORT DELETE END */

	return (error);
}

/* ARGSUSED */
static int
ncp_decrypt_atomic(crypto_provider_handle_t provider,
    crypto_session_id_t session_id, crypto_mechanism_t *mechanism,
    crypto_key_t *key, crypto_data_t *ciphertext, crypto_data_t *plaintext,
    crypto_spi_ctx_template_t ctx_template, crypto_req_handle_t req)
{
	int error = CRYPTO_FAILED;
#ifdef DEBUG
	ncp_t *softc = (ncp_t *)provider;
#endif /* DEBUG */

/* EXPORT DELETE START */

	DBG0(softc, DENTRY, "ncp_decrypt_atomic: started");

	if (ctx_template != NULL)
		return (CRYPTO_ARGUMENTS_BAD);

	/* check mechanism */
	switch (mechanism->cm_type) {
	case RSA_PKCS_MECH_INFO_TYPE:
	case RSA_X_509_MECH_INFO_TYPE:
		error = ncp_rsaatomic(provider, session_id, mechanism, key,
		    ciphertext, plaintext, KM_SLEEP, req, NCP_RSA_DEC);
		break;
	default:
		cmn_err(CE_WARN, "ncp_decrypt_atomic: unexpected mech type "
		    "0x%llx\n", (unsigned long long)mechanism->cm_type);
		error = CRYPTO_MECHANISM_INVALID;
	}

	if ((error != CRYPTO_QUEUED) && (error != CRYPTO_SUCCESS)) {
		plaintext->cd_length = 0;
	}

	DBG1(softc, DENTRY, "ncp_decrypt_atomic: done, err = 0x%x", error);

/* EXPORT DELETE END */

	return (error);
}

/*
 * Sign entry points.
 */

/* ARGSUSED */
static int
ncp_sign_init(crypto_ctx_t *ctx, crypto_mechanism_t *mechanism,
    crypto_key_t *key, crypto_spi_ctx_template_t ctx_template,
    crypto_req_handle_t req)
{
	int error = CRYPTO_FAILED;
	ncp_t *softc;
	/* LINTED E_FUNC_SET_NOT_USED */
	int instance;

/* EXPORT DELETE START */

	/* extract softc and instance number from context */
	NCP_SOFTC_FROM_CTX(ctx, softc, instance);
	DBG0(softc, DENTRY, "ncp_sign_init: started\n");

	if (ctx_template != NULL)
		return (CRYPTO_ARGUMENTS_BAD);

	/* check mechanism */
	switch (mechanism->cm_type) {
	case RSA_PKCS_MECH_INFO_TYPE:
	case RSA_X_509_MECH_INFO_TYPE:
		error = ncp_rsainit(ctx, mechanism, key, KM_SLEEP);
		break;
	case DSA_MECH_INFO_TYPE:
		error = ncp_dsainit(ctx, mechanism, key, KM_SLEEP,
		    NCP_DSA_SIGN);
		break;
	case ECDSA_MECH_INFO_TYPE:
		error = ncp_ecdsa_sign_init(ctx, mechanism, key,
		    NCP_ECDSA_SIGN);
		break;
	default:
		cmn_err(CE_WARN, "ncp_sign_init: unexpected mech type "
		    "0x%llx\n", (unsigned long long)mechanism->cm_type);
		error = CRYPTO_MECHANISM_INVALID;
	}

	DBG1(softc, DENTRY, "ncp_sign_init: done, err = 0x%x", error);


/* EXPORT DELETE END */

	return (error);
}

static int
ncp_sign(crypto_ctx_t *ctx, crypto_data_t *data,
    crypto_data_t *signature, crypto_req_handle_t req)
{
	int error = CRYPTO_FAILED;
	ncp_t *softc;
	/* LINTED E_FUNC_SET_NOT_USED */
	int instance;

/* EXPORT DELETE START */

	if (!ctx || !ctx->cc_provider || !ctx->cc_provider_private)
		return (CRYPTO_OPERATION_NOT_INITIALIZED);

	/* extract softc and instance number from context */
	NCP_SOFTC_FROM_CTX(ctx, softc, instance);
	DBG0(softc, DENTRY, "ncp_sign: started\n");

	/* check mechanism */
	switch (NCP_MECH_FROM_CTX(ctx)) {
	case RSA_PKCS_MECH_INFO_TYPE:
	case RSA_X_509_MECH_INFO_TYPE:
		error = ncp_rsastart(ctx, data, signature, req, NCP_RSA_SIGN);
		break;
	case DSA_MECH_INFO_TYPE:
		error = ncp_dsa_sign(ctx, data, signature, req);
		break;
	case ECDSA_MECH_INFO_TYPE:
		error = ncp_ecdsa_sign(ctx, data, signature, req);
		break;
	default:
		cmn_err(CE_WARN, "ncp_sign: unexpected mech type "
		    "0x%llx\n", (unsigned long long)NCP_MECH_FROM_CTX(ctx));
		error = CRYPTO_MECHANISM_INVALID;
	}

	if ((error != CRYPTO_QUEUED) && (error != CRYPTO_SUCCESS) &&
	    (error != CRYPTO_BUFFER_TOO_SMALL)) {
		signature->cd_length = 0;
	}

	DBG1(softc, DENTRY, "ncp_sign: done, err = 0x%x", error);

/* EXPORT DELETE END */

	return (error);
}

static int
ncp_sign_atomic(crypto_provider_handle_t provider,
    crypto_session_id_t session_id, crypto_mechanism_t *mechanism,
    crypto_key_t *key, crypto_data_t *data, crypto_data_t *signature,
    crypto_spi_ctx_template_t ctx_template, crypto_req_handle_t req)
{
	int error = CRYPTO_FAILED;
#ifdef DEBUG
	ncp_t *softc = (ncp_t *)provider;
#endif /* DEBUG */

/* EXPORT DELETE START */

	DBG0(softc, DENTRY, "ncp_sign_atomic: started\n");

	if (ctx_template != NULL)
		return (CRYPTO_ARGUMENTS_BAD);

	/* check mechanism */
	switch (mechanism->cm_type) {
	case RSA_PKCS_MECH_INFO_TYPE:
	case RSA_X_509_MECH_INFO_TYPE:
		error = ncp_rsaatomic(provider, session_id, mechanism, key,
		    data, signature, KM_SLEEP, req, NCP_RSA_SIGN);
		break;
	case DSA_MECH_INFO_TYPE:
		error = ncp_dsaatomic(provider, session_id, mechanism, key,
		    data, signature, KM_SLEEP, req, NCP_DSA_SIGN);
		break;
	case ECDSA_MECH_INFO_TYPE:
		error = ncp_ecdsaatomic(provider, session_id, mechanism, key,
		    data, signature, req, NCP_ECDSA_SIGN);
		break;
	default:
		cmn_err(CE_WARN, "ncp_sign_atomic: unexpected mech type "
		    "0x%llx\n", (unsigned long long)mechanism->cm_type);
		error = CRYPTO_MECHANISM_INVALID;
	}

	if ((error != CRYPTO_QUEUED) && (error != CRYPTO_SUCCESS)) {
		signature->cd_length = 0;
	}

	DBG1(softc, DENTRY, "ncp_sign_atomic: done, err = 0x%x", error);

/* EXPORT DELETE END */

	return (error);
}

/* ARGSUSED */
static int
ncp_sign_recover_init(crypto_ctx_t *ctx, crypto_mechanism_t *mechanism,
    crypto_key_t *key, crypto_spi_ctx_template_t ctx_template,
    crypto_req_handle_t req)
{
	int error = CRYPTO_FAILED;
	ncp_t *softc;
	/* LINTED E_FUNC_SET_NOT_USED */
	int instance;

/* EXPORT DELETE START */

	/* extract softc and instance number from context */
	NCP_SOFTC_FROM_CTX(ctx, softc, instance);
	DBG0(softc, DENTRY, "ncp_sign_recover_init: started\n");

	if (ctx_template != NULL)
		return (CRYPTO_ARGUMENTS_BAD);

	/* check mechanism */
	switch (mechanism->cm_type) {
	case RSA_PKCS_MECH_INFO_TYPE:
	case RSA_X_509_MECH_INFO_TYPE:
		error = ncp_rsainit(ctx, mechanism, key, KM_SLEEP);
		break;
	default:
		cmn_err(CE_WARN, "ncp_sign_recover_init: unexpected mech type "
		    "0x%llx\n", (unsigned long long)mechanism->cm_type);
		error = CRYPTO_MECHANISM_INVALID;
	}

	DBG1(softc, DENTRY, "ncp_sign_recover_init: done, err = 0x%x", error);


/* EXPORT DELETE END */

	return (error);
}

static int
ncp_sign_recover(crypto_ctx_t *ctx, crypto_data_t *data,
    crypto_data_t *signature, crypto_req_handle_t req)
{
	int error = CRYPTO_FAILED;
	ncp_t *softc;
	/* LINTED E_FUNC_SET_NOT_USED */
	int instance;

/* EXPORT DELETE START */

	if (!ctx || !ctx->cc_provider || !ctx->cc_provider_private)
		return (CRYPTO_OPERATION_NOT_INITIALIZED);

	/* extract softc and instance number from context */
	NCP_SOFTC_FROM_CTX(ctx, softc, instance);
	DBG0(softc, DENTRY, "ncp_sign_recover: started\n");

	/* check mechanism */
	switch (NCP_MECH_FROM_CTX(ctx)) {
	case RSA_PKCS_MECH_INFO_TYPE:
	case RSA_X_509_MECH_INFO_TYPE:
		error = ncp_rsastart(ctx, data, signature, req, NCP_RSA_SIGNR);
		break;
	default:
		cmn_err(CE_WARN, "ncp_sign_recover: unexpected mech type "
		    "0x%llx\n", (unsigned long long)NCP_MECH_FROM_CTX(ctx));
		error = CRYPTO_MECHANISM_INVALID;
	}

	if ((error != CRYPTO_QUEUED) && (error != CRYPTO_SUCCESS) &&
	    (error != CRYPTO_BUFFER_TOO_SMALL)) {
		signature->cd_length = 0;
	}

	DBG1(softc, DENTRY, "ncp_sign_recover: done, err = 0x%x", error);

/* EXPORT DELETE END */

	return (error);
}

static int
ncp_sign_recover_atomic(crypto_provider_handle_t provider,
    crypto_session_id_t session_id, crypto_mechanism_t *mechanism,
    crypto_key_t *key, crypto_data_t *data, crypto_data_t *signature,
    crypto_spi_ctx_template_t ctx_template, crypto_req_handle_t req)
{
	int error = CRYPTO_FAILED;
	ncp_t *softc = (ncp_t *)provider;
	/* LINTED E_FUNC_SET_NOT_USED */
	int instance;

/* EXPORT DELETE START */

	instance = ddi_get_instance(softc->n_dip);
	DBG0(softc, DENTRY, "ncp_sign_recover_atomic: started\n");

	if (ctx_template != NULL)
		return (CRYPTO_ARGUMENTS_BAD);

	/* check mechanism */
	switch (mechanism->cm_type) {
	case RSA_PKCS_MECH_INFO_TYPE:
	case RSA_X_509_MECH_INFO_TYPE:
		error = ncp_rsaatomic(provider, session_id, mechanism, key,
		    data, signature, KM_SLEEP, req, NCP_RSA_SIGNR);
		break;
	default:
		cmn_err(CE_WARN, "ncp_sign_recover_atomic: unexpected mech type"
		    " 0x%llx\n", (unsigned long long)mechanism->cm_type);
		error = CRYPTO_MECHANISM_INVALID;
	}

	if ((error != CRYPTO_QUEUED) && (error != CRYPTO_SUCCESS)) {
		signature->cd_length = 0;
	}

	DBG1(softc, DENTRY, "ncp_sign_recover_atomic: done, err = 0x%x", error);

/* EXPORT DELETE END */

	return (error);
}

/*
 * Verify entry points.
 */

/* ARGSUSED */
static int
ncp_verify_init(crypto_ctx_t *ctx, crypto_mechanism_t *mechanism,
    crypto_key_t *key, crypto_spi_ctx_template_t ctx_template,
    crypto_req_handle_t req)
{
	int error = CRYPTO_FAILED;
	ncp_t *softc;
	/* LINTED E_FUNC_SET_NOT_USED */
	int instance;

/* EXPORT DELETE START */

	/* extract softc and instance number from context */
	NCP_SOFTC_FROM_CTX(ctx, softc, instance);
	DBG0(softc, DENTRY, "ncp_verify_init: started\n");

	if (ctx_template != NULL)
		return (CRYPTO_ARGUMENTS_BAD);

	/* check mechanism */
	switch (mechanism->cm_type) {
	case RSA_PKCS_MECH_INFO_TYPE:
	case RSA_X_509_MECH_INFO_TYPE:
		error = ncp_rsainit(ctx, mechanism, key, KM_SLEEP);
		break;
	case DSA_MECH_INFO_TYPE:
		error = ncp_dsainit(ctx, mechanism, key, KM_SLEEP,
		    NCP_DSA_VRFY);
		break;
	case ECDSA_MECH_INFO_TYPE:
		error = ncp_ecdsa_verify_init(ctx, mechanism, key,
		    NCP_ECDSA_VRFY);
		break;
	default:
		cmn_err(CE_WARN, "ncp_verify_init: unexpected mech type "
		    "0x%llx\n", (unsigned long long)mechanism->cm_type);
		error = CRYPTO_MECHANISM_INVALID;
	}

	DBG1(softc, DENTRY, "ncp_verify_init: done, err = 0x%x", error);

/* EXPORT DELETE END */

	return (error);
}

static int
ncp_verify(crypto_ctx_t *ctx, crypto_data_t *data, crypto_data_t *signature,
    crypto_req_handle_t req)
{
	int error = CRYPTO_FAILED;
	ncp_t *softc;
	/* LINTED E_FUNC_SET_NOT_USED */
	int instance;

/* EXPORT DELETE START */

	if (!ctx || !ctx->cc_provider || !ctx->cc_provider_private)
		return (CRYPTO_OPERATION_NOT_INITIALIZED);

	/* extract softc and instance number from context */
	NCP_SOFTC_FROM_CTX(ctx, softc, instance);
	DBG0(softc, DENTRY, "ncp_verify: started\n");

	/* check mechanism */
	switch (NCP_MECH_FROM_CTX(ctx)) {
	case RSA_PKCS_MECH_INFO_TYPE:
	case RSA_X_509_MECH_INFO_TYPE:
		error = ncp_rsastart(ctx, signature, data, req, NCP_RSA_VRFY);
		break;
	case DSA_MECH_INFO_TYPE:
		error = ncp_dsa_verify(ctx, data, signature, req);
		break;
	case ECDSA_MECH_INFO_TYPE:
		error = ncp_ecdsa_verify(ctx, data, signature, req);
		break;
	default:
		cmn_err(CE_WARN, "ncp_verify: unexpected mech type "
		    "0x%llx\n", (unsigned long long)NCP_MECH_FROM_CTX(ctx));
		error = CRYPTO_MECHANISM_INVALID;
	}

	DBG1(softc, DENTRY, "ncp_verify: done, err = 0x%x", error);

/* EXPORT DELETE END */

	return (error);
}

static int
ncp_verify_atomic(crypto_provider_handle_t provider,
    crypto_session_id_t session_id, crypto_mechanism_t *mechanism,
    crypto_key_t *key, crypto_data_t *data, crypto_data_t *signature,
    crypto_spi_ctx_template_t ctx_template, crypto_req_handle_t req)
{
	int error = CRYPTO_FAILED;
#ifdef DEBUG
	ncp_t *softc = (ncp_t *)provider;
#endif /* DEBUG */

/* EXPORT DELETE START */

	DBG0(softc, DENTRY, "ncp_verify_atomic: started\n");

	if (ctx_template != NULL)
		return (CRYPTO_ARGUMENTS_BAD);

	/* check mechanism */
	switch (mechanism->cm_type) {
	case RSA_PKCS_MECH_INFO_TYPE:
	case RSA_X_509_MECH_INFO_TYPE:
		error = ncp_rsaatomic(provider, session_id, mechanism, key,
		    signature, data, KM_SLEEP, req, NCP_RSA_VRFY);
		break;
	case DSA_MECH_INFO_TYPE:
		error = ncp_dsaatomic(provider, session_id, mechanism, key,
		    data, signature, KM_SLEEP, req, NCP_DSA_VRFY);
		break;
	case ECDSA_MECH_INFO_TYPE:
		error = ncp_ecdsaatomic(provider, session_id, mechanism, key,
		    data, signature, req, NCP_ECDSA_VRFY);
		break;
	default:
		cmn_err(CE_WARN, "ncp_verify_atomic: unexpected mech type "
		    "0x%llx\n", (unsigned long long)mechanism->cm_type);
		error = CRYPTO_MECHANISM_INVALID;
	}

	DBG1(softc, DENTRY, "ncp_verify_atomic: done, err = 0x%x", error);

/* EXPORT DELETE END */

	return (error);
}

/* ARGSUSED */
static int
ncp_verify_recover_init(crypto_ctx_t *ctx, crypto_mechanism_t *mechanism,
    crypto_key_t *key, crypto_spi_ctx_template_t ctx_template,
    crypto_req_handle_t req)
{
	int error = CRYPTO_MECHANISM_INVALID;
	ncp_t *softc;
	/* LINTED E_FUNC_SET_NOT_USED */
	int instance;

/* EXPORT DELETE START */

	/* extract softc and instance number from context */
	NCP_SOFTC_FROM_CTX(ctx, softc, instance);
	DBG0(softc, DENTRY, "ncp_verify_recover_init: started\n");

	if (ctx_template != NULL)
		return (CRYPTO_ARGUMENTS_BAD);

	/* check mechanism */
	switch (mechanism->cm_type) {
	case RSA_PKCS_MECH_INFO_TYPE:
	case RSA_X_509_MECH_INFO_TYPE:
		error = ncp_rsainit(ctx, mechanism, key, KM_SLEEP);
		break;
	default:
		cmn_err(CE_WARN, "ncp_verify_recover_init: unexpected mech type"
		    " 0x%llx\n", (unsigned long long)mechanism->cm_type);
	}

	DBG1(softc, DENTRY, "ncp_verify_recover_init: done, err = 0x%x", error);

/* EXPORT DELETE END */

	return (error);
}

static int
ncp_verify_recover(crypto_ctx_t *ctx, crypto_data_t *signature,
    crypto_data_t *data, crypto_req_handle_t req)
{
	int error = CRYPTO_MECHANISM_INVALID;
	ncp_t *softc;
	/* LINTED E_FUNC_SET_NOT_USED */
	int instance;

/* EXPORT DELETE START */

	if (!ctx || !ctx->cc_provider || !ctx->cc_provider_private)
		return (CRYPTO_OPERATION_NOT_INITIALIZED);

	/* extract softc and instance number from context */
	NCP_SOFTC_FROM_CTX(ctx, softc, instance);
	DBG0(softc, DENTRY, "ncp_verify_recover: started\n");

	/* check mechanism */
	switch (NCP_MECH_FROM_CTX(ctx)) {
	case RSA_PKCS_MECH_INFO_TYPE:
	case RSA_X_509_MECH_INFO_TYPE:
		error = ncp_rsastart(ctx, signature, data, req, NCP_RSA_VRFYR);
		break;
	default:
		cmn_err(CE_WARN, "ncp_verify_recover: unexpected mech type "
		    "0x%llx\n", (unsigned long long)NCP_MECH_FROM_CTX(ctx));
	}

	if ((error != CRYPTO_QUEUED) && (error != CRYPTO_SUCCESS) &&
	    (error != CRYPTO_BUFFER_TOO_SMALL)) {
		data->cd_length = 0;
	}

	DBG1(softc, DENTRY, "ncp_verify_recover: done, err = 0x%x", error);

/* EXPORT DELETE END */

	return (error);
}

static int
ncp_verify_recover_atomic(crypto_provider_handle_t provider,
    crypto_session_id_t session_id, crypto_mechanism_t *mechanism,
    crypto_key_t *key, crypto_data_t *data, crypto_data_t *signature,
    crypto_spi_ctx_template_t ctx_template, crypto_req_handle_t req)
{
	int error = CRYPTO_MECHANISM_INVALID;
#ifdef DEBUG
	ncp_t *softc = (ncp_t *)provider;
#endif /* DEBUG */

/* EXPORT DELETE START */

	DBG0(softc, DENTRY, "ncp_verify_recover_atomic: started\n");

	if (ctx_template != NULL)
		return (CRYPTO_ARGUMENTS_BAD);

	/* check mechanism */
	switch (mechanism->cm_type) {
	case RSA_PKCS_MECH_INFO_TYPE:
	case RSA_X_509_MECH_INFO_TYPE:
		error = ncp_rsaatomic(provider, session_id, mechanism, key,
		    signature, data, KM_SLEEP, req, NCP_RSA_VRFYR);
		break;
	default:
		cmn_err(CE_WARN, "ncp_verify_recover_atomic: unexpected mech "
		    "type 0x%llx\n", (unsigned long long)mechanism->cm_type);
		error = CRYPTO_MECHANISM_INVALID;
	}

	if ((error != CRYPTO_QUEUED) && (error != CRYPTO_SUCCESS)) {
		data->cd_length = 0;
	}

	DBG1(softc, DENTRY,
	    "ncp_verify_recover_atomic: done, err = 0x%x", error);

/* EXPORT DELETE END */

	return (error);
}

/*
 * Context management entry points.
 */

int
ncp_free_context(crypto_ctx_t *ctx)
{
	int error = CRYPTO_SUCCESS;
	ncp_t *softc;
	/* LINTED E_FUNC_SET_NOT_USED */
	int instance;

/* EXPORT DELETE START */

	/* extract softc and instance number from context */
	NCP_SOFTC_FROM_CTX(ctx, softc, instance);
	DBG0(softc, DENTRY, "ncp_free_context: entered");

	if (ctx->cc_provider_private == NULL)
		return (error);

	error = ncp_free_context_low(ctx);

	DBG1(softc, DENTRY, "ncp_free_context: done, err = 0x%x", error);

/* EXPORT DELETE END */

	return (error);
}

static int
ncp_free_context_low(crypto_ctx_t *ctx)
{
	int error = CRYPTO_SUCCESS;

/* EXPORT DELETE START */

	/* check mechanism */
	switch (NCP_MECH_FROM_CTX(ctx)) {
	case RSA_PKCS_MECH_INFO_TYPE:
	case RSA_X_509_MECH_INFO_TYPE:
		ncp_rsactxfree(ctx);
		break;
	case DSA_MECH_INFO_TYPE:
		ncp_dsactxfree(ctx);
		break;
	case ECDSA_MECH_INFO_TYPE:
		ncp_ecdsactxfree(ctx);
		break;
	default:
		/* Should never reach here */
		cmn_err(CE_WARN, "ncp_free_context_low: unexpected mech type "
		    "0x%llx\n", (unsigned long long)NCP_MECH_FROM_CTX(ctx));
		error = CRYPTO_MECHANISM_INVALID;
	}

/* EXPORT DELETE END */

	return (error);
}

/* ARGSUSED */
static int
ext_info(crypto_provider_handle_t prov,
    crypto_provider_ext_info_t *ext_info, crypto_req_handle_t cfreq)
{
	ncp_t	*ncp = (ncp_t *)prov;
	int	len;
	char	*id = IDENT_ASYM;

/* EXPORT DELETE START */

	/* Label */
	(void) sprintf((char *)ext_info->ei_label, "%s/%d %s",
	    ddi_driver_name(ncp->n_dip), ddi_get_instance(ncp->n_dip), id);
	len = strlen((char *)ext_info->ei_label);
	(void) memset(ext_info->ei_label + len, ' ',
	    CRYPTO_EXT_SIZE_LABEL - len);

	/* Manufacturer ID */
	(void) sprintf((char *)ext_info->ei_manufacturerID, "%s",
	    NCP_MANUFACTURER_ID);
	len = strlen((char *)ext_info->ei_manufacturerID);
	(void) memset(ext_info->ei_manufacturerID + len, ' ',
	    CRYPTO_EXT_SIZE_MANUF - len);

	/* Model */
	(void) sprintf((char *)ext_info->ei_model, "NCP1");

	DBG1(ncp, DATTACH, "kCF MODEL: %s", (char *)ext_info->ei_model);

	len = strlen((char *)ext_info->ei_model);
	(void) memset(ext_info->ei_model + len, ' ',
	    CRYPTO_EXT_SIZE_MODEL - len);

	/* Serial Number. Blank for Niagara */
	(void) memset(ext_info->ei_serial_number, ' ', CRYPTO_EXT_SIZE_SERIAL);

	ext_info->ei_flags = CRYPTO_EXTF_WRITE_PROTECTED;

	ext_info->ei_max_session_count = CRYPTO_UNAVAILABLE_INFO;
	ext_info->ei_max_pin_len = CRYPTO_UNAVAILABLE_INFO;
	ext_info->ei_min_pin_len = CRYPTO_UNAVAILABLE_INFO;
	ext_info->ei_total_public_memory = CRYPTO_UNAVAILABLE_INFO;
	ext_info->ei_free_public_memory = CRYPTO_UNAVAILABLE_INFO;
	ext_info->ei_total_private_memory = CRYPTO_UNAVAILABLE_INFO;
	ext_info->ei_free_private_memory = CRYPTO_UNAVAILABLE_INFO;
	ext_info->ei_hardware_version.cv_major = 0;
	ext_info->ei_hardware_version.cv_minor = 0;
	ext_info->ei_firmware_version.cv_major = ncp->n_hvapi_major_version;
	ext_info->ei_firmware_version.cv_minor = ncp->n_hvapi_minor_version;

	/* Time. No need to be supplied for token without a clock */
	ext_info->ei_time[0] = '\000';

/* EXPORT DELETE END */

	return (CRYPTO_SUCCESS);
}


/*ARGSUSED*/
static int
ncp_allocate_mechanism(crypto_provider_handle_t provider,
    crypto_mechanism_t *in_mech, crypto_mechanism_t *out_mech,
    int *error, int mode)
{

/* EXPORT DELETE START */

	switch (out_mech->cm_type) {
	case ECDH1_DERIVE_MECH_INFO_TYPE:
		return (ncp_ecdh1_allocmech(in_mech, out_mech, error, mode));
	default:
		/* crypto module does alloc/copyin of flat params */
		break;
	}

/* EXPORT DELETE END */

	return (CRYPTO_NOT_SUPPORTED);

}

/*ARGSUSED*/
static int
ncp_free_mechanism(crypto_provider_handle_t provider,
    crypto_mechanism_t *mech)
{

/* EXPORT DELETE START */

	switch (mech->cm_type) {
	case ECDH1_DERIVE_MECH_INFO_TYPE:
		return (ncp_ecdh1_freemech(mech));
	default:
		break;
	}

/* EXPORT DELETE END */

	return (CRYPTO_NOT_SUPPORTED);
}
