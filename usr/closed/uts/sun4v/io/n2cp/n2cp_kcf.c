/*
 * Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <vm/seg_kmem.h>
#include <sys/types.h>
#include <sys/modctl.h>
#include <sys/conf.h>
#include <sys/devops.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/cmn_err.h>
#include <sys/sysmacros.h>
#include <sys/cpuvar.h>
#include <sys/varargs.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/kmem.h>
#include <sys/ioccom.h>
#include <sys/open.h>
#include <sys/cred.h>
#include <sys/kstat.h>
#include <sys/strsun.h>
#include <sys/systm.h>
#include <sys/note.h>
#include <sys/crypto/common.h>
#include <sys/crypto/spi.h>
#include <sys/n2cp.h>
#include <sys/atomic.h>	/* atomic_inc & atomic_dec */
#include <modes/modes.h>

static int n2cp_free_context(crypto_ctx_t *);
static nr_ctx_t *n2cp_alloc_ctx(n2cp_t *);
static void n2cp_free_ctx(n2cp_request_t *);
static size_t round_to_power2(int);

/*
 * We want these inlined for performance.
 */
#ifndef	DEBUG
#pragma inline(n2cp_freereq, n2cp_getreq)
#pragma inline(n2cp_enqueue, n2cp_dequeue, n2cp_rmqueue)
#endif

#define	IDENT_BULK	"Crypto Accel Bulk 1.0"
#define	VENDOR		"Sun Microsystems, Inc."

#define	FATAL_RV(rv) \
	(((rv) != CRYPTO_SUCCESS) && \
	((rv) != CRYPTO_BUFFER_TOO_SMALL) && \
	((rv) != CRYPTO_BUSY))
#define	RETRY_RV(rv) \
	(((rv) == CRYPTO_BUFFER_TOO_SMALL) || \
	((rv) == CRYPTO_BUSY))

static int	n2cp_pagesize;

/*
 * CSPI information (entry points, provider info, etc.)
 */

/* Mechanisms for the bulk cipher provider */
static crypto_mech_info_t n2cp_mech_info_table[] = {
	{SUN_CKM_DES_CBC, DES_CBC_MECH_INFO_TYPE,
	    CRYPTO_FG_ENCRYPT | CRYPTO_FG_ENCRYPT_ATOMIC |
	    CRYPTO_FG_DECRYPT | CRYPTO_FG_DECRYPT_ATOMIC |
	    CRYPTO_FG_ENCRYPT_MAC_ATOMIC | CRYPTO_FG_MAC_DECRYPT_ATOMIC,
	    DES_KEY_LEN, DES_KEY_LEN, CRYPTO_KEYSIZE_UNIT_IN_BYTES},
	{SUN_CKM_DES_CBC_PAD, DES_CBC_PAD_MECH_INFO_TYPE,
	    CRYPTO_FG_ENCRYPT | CRYPTO_FG_ENCRYPT_ATOMIC |
	    CRYPTO_FG_DECRYPT | CRYPTO_FG_DECRYPT_ATOMIC |
	    CRYPTO_FG_ENCRYPT_MAC_ATOMIC | CRYPTO_FG_MAC_DECRYPT_ATOMIC,
	    DES_KEY_LEN, DES_KEY_LEN, CRYPTO_KEYSIZE_UNIT_IN_BYTES},
	{SUN_CKM_DES_ECB, DES_ECB_MECH_INFO_TYPE,
	    CRYPTO_FG_ENCRYPT | CRYPTO_FG_ENCRYPT_ATOMIC |
	    CRYPTO_FG_DECRYPT | CRYPTO_FG_DECRYPT_ATOMIC |
	    CRYPTO_FG_ENCRYPT_MAC_ATOMIC | CRYPTO_FG_MAC_DECRYPT_ATOMIC,
	    DES_KEY_LEN, DES_KEY_LEN, CRYPTO_KEYSIZE_UNIT_IN_BYTES},
	{SUN_CKM_DES3_CBC, DES3_CBC_MECH_INFO_TYPE,
	    CRYPTO_FG_ENCRYPT | CRYPTO_FG_ENCRYPT_ATOMIC |
	    CRYPTO_FG_DECRYPT | CRYPTO_FG_DECRYPT_ATOMIC |
	    CRYPTO_FG_ENCRYPT_MAC_ATOMIC | CRYPTO_FG_MAC_DECRYPT_ATOMIC,
	    DES3_MIN_KEY_LEN, DES3_MAX_KEY_LEN, CRYPTO_KEYSIZE_UNIT_IN_BYTES},
	{SUN_CKM_DES3_CBC_PAD, DES3_CBC_PAD_MECH_INFO_TYPE,
	    CRYPTO_FG_ENCRYPT | CRYPTO_FG_ENCRYPT_ATOMIC |
	    CRYPTO_FG_DECRYPT | CRYPTO_FG_DECRYPT_ATOMIC |
	    CRYPTO_FG_ENCRYPT_MAC_ATOMIC | CRYPTO_FG_MAC_DECRYPT_ATOMIC,
	    DES3_MIN_KEY_LEN, DES3_MAX_KEY_LEN, CRYPTO_KEYSIZE_UNIT_IN_BYTES},
	{SUN_CKM_DES3_ECB, DES3_ECB_MECH_INFO_TYPE,
	    CRYPTO_FG_ENCRYPT | CRYPTO_FG_ENCRYPT_ATOMIC |
	    CRYPTO_FG_DECRYPT | CRYPTO_FG_DECRYPT_ATOMIC |
	    CRYPTO_FG_ENCRYPT_MAC_ATOMIC | CRYPTO_FG_MAC_DECRYPT_ATOMIC,
	    DES3_MIN_KEY_LEN, DES3_MAX_KEY_LEN, CRYPTO_KEYSIZE_UNIT_IN_BYTES},
	{SUN_CKM_AES_CBC, AES_CBC_MECH_INFO_TYPE,
	    CRYPTO_FG_ENCRYPT | CRYPTO_FG_ENCRYPT_ATOMIC |
	    CRYPTO_FG_DECRYPT | CRYPTO_FG_DECRYPT_ATOMIC |
	    CRYPTO_FG_ENCRYPT_MAC_ATOMIC | CRYPTO_FG_MAC_DECRYPT_ATOMIC,
	    AES_MIN_KEY_LEN, AES_MAX_KEY_LEN, CRYPTO_KEYSIZE_UNIT_IN_BYTES},
	{SUN_CKM_AES_CBC_PAD, AES_CBC_PAD_MECH_INFO_TYPE,
	    CRYPTO_FG_ENCRYPT | CRYPTO_FG_ENCRYPT_ATOMIC |
	    CRYPTO_FG_DECRYPT | CRYPTO_FG_DECRYPT_ATOMIC |
	    CRYPTO_FG_ENCRYPT_MAC_ATOMIC | CRYPTO_FG_MAC_DECRYPT_ATOMIC,
	    AES_MIN_KEY_LEN, AES_MAX_KEY_LEN, CRYPTO_KEYSIZE_UNIT_IN_BYTES},
	{SUN_CKM_AES_ECB, AES_ECB_MECH_INFO_TYPE,
	    CRYPTO_FG_ENCRYPT | CRYPTO_FG_ENCRYPT_ATOMIC |
	    CRYPTO_FG_DECRYPT | CRYPTO_FG_DECRYPT_ATOMIC |
	    CRYPTO_FG_ENCRYPT_MAC_ATOMIC | CRYPTO_FG_MAC_DECRYPT_ATOMIC,
	    AES_MIN_KEY_LEN, AES_MAX_KEY_LEN, CRYPTO_KEYSIZE_UNIT_IN_BYTES},
	{SUN_CKM_AES_CTR, AES_CTR_MECH_INFO_TYPE,
	    CRYPTO_FG_ENCRYPT | CRYPTO_FG_ENCRYPT_ATOMIC |
	    CRYPTO_FG_DECRYPT | CRYPTO_FG_DECRYPT_ATOMIC |
	    CRYPTO_FG_ENCRYPT_MAC_ATOMIC | CRYPTO_FG_MAC_DECRYPT_ATOMIC,
	    AES_MIN_KEY_LEN, AES_MAX_KEY_LEN, CRYPTO_KEYSIZE_UNIT_IN_BYTES},
	{SUN_CKM_AES_CCM, AES_CCM_MECH_INFO_TYPE,
	    CRYPTO_FG_ENCRYPT | CRYPTO_FG_ENCRYPT_ATOMIC |
	    CRYPTO_FG_DECRYPT | CRYPTO_FG_DECRYPT_ATOMIC |
	    CRYPTO_FG_ENCRYPT_MAC_ATOMIC | CRYPTO_FG_MAC_DECRYPT_ATOMIC,
	    AES_MIN_KEY_LEN, AES_MAX_KEY_LEN, CRYPTO_KEYSIZE_UNIT_IN_BYTES},
	{SUN_CKM_AES_GCM, AES_GCM_MECH_INFO_TYPE,
	    CRYPTO_FG_ENCRYPT | CRYPTO_FG_ENCRYPT_ATOMIC |
	    CRYPTO_FG_DECRYPT | CRYPTO_FG_DECRYPT_ATOMIC |
	    CRYPTO_FG_ENCRYPT_MAC_ATOMIC | CRYPTO_FG_MAC_DECRYPT_ATOMIC,
	    AES_MIN_KEY_LEN, AES_MAX_KEY_LEN, CRYPTO_KEYSIZE_UNIT_IN_BYTES},
	{SUN_CKM_AES_GMAC, AES_GMAC_MECH_INFO_TYPE,
	    CRYPTO_FG_ENCRYPT | CRYPTO_FG_ENCRYPT_ATOMIC |
	    CRYPTO_FG_DECRYPT | CRYPTO_FG_DECRYPT_ATOMIC |
	    CRYPTO_FG_MAC | CRYPTO_FG_MAC_ATOMIC |
	    CRYPTO_FG_SIGN | CRYPTO_FG_SIGN_ATOMIC |
	    CRYPTO_FG_VERIFY | CRYPTO_FG_VERIFY_ATOMIC,
	    AES_MIN_KEY_LEN, AES_MAX_KEY_LEN, CRYPTO_KEYSIZE_UNIT_IN_BYTES},
	{SUN_CKM_RC4, RC4_WSTRM_MECH_INFO_TYPE,
	    CRYPTO_FG_ENCRYPT | CRYPTO_FG_ENCRYPT_ATOMIC |
	    CRYPTO_FG_DECRYPT | CRYPTO_FG_DECRYPT_ATOMIC,
	    RC4_MIN_KEY_LEN * 8, RC4_MAX_KEY_LEN * 8,
	    CRYPTO_KEYSIZE_UNIT_IN_BITS | CRYPTO_CAN_SHARE_OPSTATE},
	{SUN_CKM_MD5, MD5_MECH_INFO_TYPE,
	    CRYPTO_FG_DIGEST | CRYPTO_FG_DIGEST_ATOMIC,
	    0, MAX_DATA_LEN, 0},
	{SUN_CKM_SHA1, SHA1_MECH_INFO_TYPE,
	    CRYPTO_FG_DIGEST | CRYPTO_FG_DIGEST_ATOMIC,
	    0, MAX_DATA_LEN, 0},
	{SUN_CKM_SHA256, SHA256_MECH_INFO_TYPE,
	    CRYPTO_FG_DIGEST | CRYPTO_FG_DIGEST_ATOMIC,
	    0, MAX_DATA_LEN, 0},
	{SUN_CKM_MD5_HMAC, MD5_HMAC_MECH_INFO_TYPE,
	    CRYPTO_FG_MAC | CRYPTO_FG_MAC_ATOMIC |
	    CRYPTO_FG_SIGN | CRYPTO_FG_SIGN_ATOMIC |
	    CRYPTO_FG_VERIFY | CRYPTO_FG_VERIFY_ATOMIC |
	    CRYPTO_FG_ENCRYPT_MAC_ATOMIC | CRYPTO_FG_MAC_DECRYPT_ATOMIC,
	    HMAC_MIN_KEY_LEN, HMAC_MAX_KEY_LEN, CRYPTO_KEYSIZE_UNIT_IN_BYTES},
	{SUN_CKM_SHA1_HMAC, SHA1_HMAC_MECH_INFO_TYPE,
	    CRYPTO_FG_MAC | CRYPTO_FG_MAC_ATOMIC |
	    CRYPTO_FG_SIGN | CRYPTO_FG_SIGN_ATOMIC |
	    CRYPTO_FG_VERIFY | CRYPTO_FG_VERIFY_ATOMIC |
	    CRYPTO_FG_ENCRYPT_MAC_ATOMIC | CRYPTO_FG_MAC_DECRYPT_ATOMIC,
	    HMAC_MIN_KEY_LEN, HMAC_MAX_KEY_LEN, CRYPTO_KEYSIZE_UNIT_IN_BYTES},
	{SUN_CKM_SHA256_HMAC, SHA256_HMAC_MECH_INFO_TYPE,
	    CRYPTO_FG_MAC | CRYPTO_FG_MAC_ATOMIC |
	    CRYPTO_FG_SIGN | CRYPTO_FG_SIGN_ATOMIC |
	    CRYPTO_FG_VERIFY | CRYPTO_FG_VERIFY_ATOMIC |
	    CRYPTO_FG_ENCRYPT_MAC_ATOMIC | CRYPTO_FG_MAC_DECRYPT_ATOMIC,
	    HMAC_MIN_KEY_LEN, HMAC_MAX_KEY_LEN, CRYPTO_KEYSIZE_UNIT_IN_BYTES},
	{SUN_CKM_MD5_HMAC_GENERAL, MD5_HMAC_GENERAL_MECH_INFO_TYPE,
	    CRYPTO_FG_MAC | CRYPTO_FG_MAC_ATOMIC |
	    CRYPTO_FG_SIGN | CRYPTO_FG_SIGN_ATOMIC |
	    CRYPTO_FG_VERIFY | CRYPTO_FG_VERIFY_ATOMIC |
	    CRYPTO_FG_ENCRYPT_MAC_ATOMIC | CRYPTO_FG_MAC_DECRYPT_ATOMIC,
	    HMAC_MIN_KEY_LEN, HMAC_MAX_KEY_LEN, CRYPTO_KEYSIZE_UNIT_IN_BYTES},
	{SUN_CKM_SHA1_HMAC_GENERAL, SHA1_HMAC_GENERAL_MECH_INFO_TYPE,
	    CRYPTO_FG_MAC | CRYPTO_FG_MAC_ATOMIC |
	    CRYPTO_FG_SIGN | CRYPTO_FG_SIGN_ATOMIC |
	    CRYPTO_FG_VERIFY | CRYPTO_FG_VERIFY_ATOMIC |
	    CRYPTO_FG_ENCRYPT_MAC_ATOMIC | CRYPTO_FG_MAC_DECRYPT_ATOMIC,
	    HMAC_MIN_KEY_LEN, HMAC_MAX_KEY_LEN, CRYPTO_KEYSIZE_UNIT_IN_BYTES},
	{SUN_CKM_SHA256_HMAC_GENERAL, SHA256_HMAC_GENERAL_MECH_INFO_TYPE,
	    CRYPTO_FG_MAC | CRYPTO_FG_MAC_ATOMIC |
	    CRYPTO_FG_SIGN | CRYPTO_FG_SIGN_ATOMIC |
	    CRYPTO_FG_VERIFY | CRYPTO_FG_VERIFY_ATOMIC |
	    CRYPTO_FG_ENCRYPT_MAC_ATOMIC | CRYPTO_FG_MAC_DECRYPT_ATOMIC,
	    HMAC_MIN_KEY_LEN, HMAC_MAX_KEY_LEN, CRYPTO_KEYSIZE_UNIT_IN_BYTES},
	{SUN_CKM_SSL3_MD5_MAC, SSL3_MD5_MAC_MECH_INFO_TYPE,
	    CRYPTO_FG_MAC | CRYPTO_FG_MAC_ATOMIC |
	    CRYPTO_FG_SIGN | CRYPTO_FG_SIGN_ATOMIC |
	    CRYPTO_FG_VERIFY | CRYPTO_FG_VERIFY_ATOMIC,
	    HMAC_MIN_KEY_LEN, HMAC_MAX_KEY_LEN, CRYPTO_KEYSIZE_UNIT_IN_BYTES},
#ifdef	SSL3_SHA256_MAC_SUPPORT
	{SUN_CKM_SSL3_SHA256_MAC, SSL3_SHA256_MAC_MECH_INFO_TYPE,
	    CRYPTO_FG_MAC_ATOMIC,
	    HMAC_MIN_KEY_LEN, HMAC_MAX_KEY_LEN, CRYPTO_KEYSIZE_UNIT_IN_BYTES},
#endif
	{SUN_CKM_SSL3_SHA1_MAC, SSL3_SHA1_MAC_MECH_INFO_TYPE,
	    CRYPTO_FG_MAC_ATOMIC,
	    HMAC_MIN_KEY_LEN, HMAC_MAX_KEY_LEN, CRYPTO_KEYSIZE_UNIT_IN_BYTES}
};

static crypto_mech_info_t kt_mechs[] = {
	{SUN_CKM_SHA384, SHA384_MECH_INFO_TYPE,
	    CRYPTO_FG_DIGEST | CRYPTO_FG_DIGEST_ATOMIC,
	    0, MAX_DATA_LEN, 0},
	{SUN_CKM_SHA512, SHA512_MECH_INFO_TYPE,
	    CRYPTO_FG_DIGEST | CRYPTO_FG_DIGEST_ATOMIC,
	    0, MAX_DATA_LEN, 0},
	{SUN_CKM_AES_CFB128, AES_CFB_MECH_INFO_TYPE,
	    CRYPTO_FG_ENCRYPT | CRYPTO_FG_ENCRYPT_ATOMIC |
	    CRYPTO_FG_DECRYPT | CRYPTO_FG_DECRYPT_ATOMIC,
	    AES_MIN_KEY_LEN, AES_MAX_KEY_LEN, CRYPTO_KEYSIZE_UNIT_IN_BYTES}
};

static void n2cp_provider_status(crypto_provider_handle_t, uint_t *);

static crypto_control_ops_t n2cp_control_ops = {
	n2cp_provider_status
};

static int n2cp_digest_init(crypto_ctx_t *, crypto_mechanism_t *,
    crypto_req_handle_t);
static int n2cp_digest(crypto_ctx_t *, crypto_data_t *, crypto_data_t *,
    crypto_req_handle_t);
static int n2cp_digest_update(crypto_ctx_t *, crypto_data_t *,
    crypto_req_handle_t);
static int n2cp_digest_final(crypto_ctx_t *, crypto_data_t *,
    crypto_req_handle_t);
static int n2cp_digest_atomic(crypto_provider_handle_t, crypto_session_id_t,
    crypto_mechanism_t *, crypto_data_t *, crypto_data_t *,
    crypto_req_handle_t);

static int n2cp_encrypt_init(crypto_ctx_t *, crypto_mechanism_t *,
    crypto_key_t *, crypto_spi_ctx_template_t, crypto_req_handle_t);
static int n2cp_encrypt(crypto_ctx_t *, crypto_data_t *, crypto_data_t *,
    crypto_req_handle_t);
static int n2cp_encrypt_update(crypto_ctx_t *, crypto_data_t *,
    crypto_data_t *, crypto_req_handle_t);
static int n2cp_encrypt_final(crypto_ctx_t *, crypto_data_t *,
    crypto_req_handle_t);
static int n2cp_encrypt_atomic(crypto_provider_handle_t, crypto_session_id_t,
    crypto_mechanism_t *, crypto_key_t *, crypto_data_t *,
    crypto_data_t *, crypto_spi_ctx_template_t, crypto_req_handle_t);

static int n2cp_encrypt_mac_atomic(crypto_provider_handle_t,
    crypto_session_id_t, crypto_mechanism_t *, crypto_key_t *,
    crypto_mechanism_t *, crypto_key_t *, crypto_data_t *,
    crypto_dual_data_t *, crypto_data_t *, crypto_spi_ctx_template_t,
    crypto_spi_ctx_template_t, crypto_req_handle_t);

static int n2cp_decrypt_init(crypto_ctx_t *, crypto_mechanism_t *,
    crypto_key_t *, crypto_spi_ctx_template_t, crypto_req_handle_t);
static int n2cp_decrypt(crypto_ctx_t *, crypto_data_t *, crypto_data_t *,
    crypto_req_handle_t);
static int n2cp_decrypt_update(crypto_ctx_t *, crypto_data_t *,
    crypto_data_t *, crypto_req_handle_t);
static int n2cp_decrypt_final(crypto_ctx_t *, crypto_data_t *,
    crypto_req_handle_t);
static int n2cp_decrypt_atomic(crypto_provider_handle_t, crypto_session_id_t,
    crypto_mechanism_t *, crypto_key_t *, crypto_data_t *,
    crypto_data_t *, crypto_spi_ctx_template_t, crypto_req_handle_t);

static int n2cp_mac_verify_decrypt_atomic(crypto_provider_handle_t,
    crypto_session_id_t, crypto_mechanism_t *, crypto_key_t *,
    crypto_mechanism_t *, crypto_key_t *, crypto_dual_data_t *,
    crypto_data_t *, crypto_data_t *, crypto_spi_ctx_template_t,
    crypto_spi_ctx_template_t, crypto_req_handle_t);

static int n2cp_sign_init(crypto_ctx_t *, crypto_mechanism_t *,
    crypto_key_t *, crypto_spi_ctx_template_t, crypto_req_handle_t);
static int n2cp_sign(crypto_ctx_t *, crypto_data_t *,
    crypto_data_t *, crypto_req_handle_t);
static int n2cp_sign_atomic(crypto_provider_handle_t, crypto_session_id_t,
    crypto_mechanism_t *, crypto_key_t *, crypto_data_t *, crypto_data_t *,
    crypto_spi_ctx_template_t, crypto_req_handle_t);

static int n2cp_verify_init(crypto_ctx_t *, crypto_mechanism_t *,
    crypto_key_t *, crypto_spi_ctx_template_t, crypto_req_handle_t);
static int n2cp_verify(crypto_ctx_t *, crypto_data_t *,
    crypto_data_t *, crypto_req_handle_t);
static int n2cp_verify_atomic(crypto_provider_handle_t, crypto_session_id_t,
    crypto_mechanism_t *, crypto_key_t *, crypto_data_t *, crypto_data_t *,
    crypto_spi_ctx_template_t, crypto_req_handle_t);

static int n2cp_allocate_mechanism(crypto_provider_handle_t,
    crypto_mechanism_t *, crypto_mechanism_t *, int *error, int);
static int n2cp_free_mechanism(crypto_provider_handle_t,
    crypto_mechanism_t *);

static crypto_digest_ops_t n2cp_digest_ops = {
	n2cp_digest_init,
	n2cp_digest,
	n2cp_digest_update,
	NULL,		/* digest_key */
	n2cp_digest_final,
	n2cp_digest_atomic
};

static crypto_cipher_ops_t n2cp_cipher_ops = {
	n2cp_encrypt_init,
	n2cp_encrypt,
	n2cp_encrypt_update,
	n2cp_encrypt_final,
	n2cp_encrypt_atomic,
	n2cp_decrypt_init,
	n2cp_decrypt,
	n2cp_decrypt_update,
	n2cp_decrypt_final,
	n2cp_decrypt_atomic
};

static crypto_mac_ops_t n2cp_mac_ops = {
	n2cp_sign_init,
	n2cp_sign,
	NULL,		/* mac_update */
	NULL,		/* mac_final */
	n2cp_sign_atomic,
	n2cp_verify_atomic
};

static crypto_sign_ops_t n2cp_sign_ops = {
	n2cp_sign_init,
	n2cp_sign,
	NULL,		/* n2cp_sign_update */
	NULL,		/* n2cp_sign_final */
	n2cp_sign_atomic,
	NULL,		/* signrecover_init */
	NULL,		/* signrecover */
	NULL		/* signrecover_atomic */
};

static crypto_verify_ops_t n2cp_verify_ops = {
	n2cp_verify_init,
	n2cp_verify,
	NULL,		/* n2cp_verify_update */
	NULL,		/* n2cp_verify_final */
	n2cp_verify_atomic,
	NULL,		/* verifyrecover_init */
	NULL,		/* verifyrecover */
	NULL		/* verifyrecover_atomic */
};

static crypto_dual_cipher_mac_ops_t n2cp_cipher_mac_ops = {
	NULL,
	NULL,
	NULL,
	NULL,
	n2cp_encrypt_mac_atomic,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	n2cp_mac_verify_decrypt_atomic
};

static int ext_info(crypto_provider_handle_t prov,
    crypto_provider_ext_info_t *ext_info, crypto_req_handle_t cfreq);

static crypto_provider_management_ops_t n2cp_provmanage_ops = {
	ext_info,		/* ext_info */
	NULL,			/* init_token */
	NULL,			/* init_pin */
	NULL			/* set_pin */
};

static crypto_ctx_ops_t n2cp_ctx_ops = {
	NULL,
	n2cp_free_context
};

static crypto_mech_ops_t n2cp_mech_ops = {
	n2cp_allocate_mechanism,	/* copyin_mechanism */
	NULL,				/* copyout_mechanism */
	n2cp_free_mechanism		/* free_mechanism */
};

/* Operations for the bulk cipher provider */
static crypto_ops_t n2cp_crypto_ops = {
	&n2cp_control_ops,
	&n2cp_digest_ops,
	&n2cp_cipher_ops,
	&n2cp_mac_ops,
	&n2cp_sign_ops,
	&n2cp_verify_ops,
	NULL,				/* dual_ops */
	&n2cp_cipher_mac_ops,		/* cipher_mac_ops */
	NULL,				/* random_ops */
	NULL,				/* session_ops */
	NULL,				/* object_ops */
	NULL,				/* key_ops */
	&n2cp_provmanage_ops,		/* management_ops */
	&n2cp_ctx_ops,
	&n2cp_mech_ops,
	NULL,				/* nostore_key_ops */
	NULL,				/* fips140_ops */
	TRUE,				/* co_uio_userspace_ok */
};

/* Provider information for the bulk cipher provider */
static crypto_provider_info_t n2cp_prov_info = {
	CRYPTO_SPI_VERSION_5,
	NULL,				/* pi_provider_description */
	CRYPTO_HW_PROVIDER,
	NULL,				/* pi_provider_dev */
	NULL,				/* pi_provider_handle */
	&n2cp_crypto_ops,
	sizeof (n2cp_mech_info_table)/sizeof (crypto_mech_info_t),
	n2cp_mech_info_table,
	0,				/* pi_logical_provider_count */
	NULL,				/* pi_logical_providers */
	CRYPTO_HMAC_NO_UPDATE		/* pi_flags */
};

#define	N2CP_MECH_FROM_REQ(reqp) \
	((((n2cp_request_t *)(reqp))->nr_cmd) & N2CP_CMD_MASK)

/*
 * Timeout value for waiting for a job completion
 */
int	n2cp_jobtimeout_secs = 60;

/* ARGSUSED */
void
n2cp_req_destructor(void *buf, void *un)
{
	n2cp_request_t	*reqp = (n2cp_request_t *)buf;

	cv_destroy(&reqp->nr_sync_cv);
	mutex_destroy(&reqp->nr_sync_lock);
	if (n2cp_use_ulcwq) {
		if (reqp->nr_ulcwq_buf_cv_initialized) {
			cv_destroy(&reqp->nr_ulcwq_buf_cv);
			reqp->nr_ulcwq_buf_cv_initialized = FALSE;
		}
	} else {
		n2cp_free_ctx(reqp);
	}
}


/* ARGSUSED */
int
n2cp_req_constructor(void *buf, void *un, int kmflags)
{
	n2cp_listnode_t		*node;
	n2cp_request_t		*reqp = (n2cp_request_t *)buf;
	n2cp_t			*n2cp = (n2cp_t *)un;

	bzero(reqp, sizeof (n2cp_request_t));

	if (!n2cp_use_ulcwq) {
		reqp->nr_context_sz = n2cp->n_reqctx_sz;
		reqp->nr_context = n2cp_alloc_ctx(n2cp);
		if (reqp->nr_context == NULL) {
			return (-1);
		}
		bzero(reqp->nr_context, reqp->nr_context_sz);
		reqp->nr_context_paddr = va_to_pa(reqp->nr_context);
	}
	reqp->nr_cep = NULL;

	cv_init(&reqp->nr_sync_cv, NULL, CV_DRIVER, NULL);
	mutex_init(&reqp->nr_sync_lock, NULL, MUTEX_DRIVER, NULL);
	reqp->nr_n2cp = n2cp;
	node = (n2cp_listnode_t *)reqp;
	node->nl_next = node;
	node->nl_prev = node;
	node = &(reqp->nr_activectx);
	node->nl_next = node;
	node->nl_prev = node;

	return (0);
}

/* ARGSUSED */
void
n2cp_buf_destructor(void *buf, void *un)
{
	n2cp_buf_struct_t	*elm = (n2cp_buf_struct_t *)buf;
	n2cp_t			*n2cp = (n2cp_t *)un;

	if (elm->buf) {
		n2_contig_free(elm->buf, MAX_DATA_LEN);
		elm->buf = NULL;
		atomic_dec_32(&n2cp->n_buf_cnt);
	}
	elm->buf_paddr = 0;
}


/* ARGSUSED */
int
n2cp_buf_constructor(void *buf, void *un, int kmflags)
{
	n2cp_buf_struct_t	*elm = (n2cp_buf_struct_t *)buf;
	n2cp_t			*n2cp = (n2cp_t *)un;

	if ((elm->buf = n2_contig_alloc(MAX_DATA_LEN)) == NULL) {
		return (-1);
	}
	elm->buf_paddr = va_to_pa(elm->buf);

	atomic_inc_32(&n2cp->n_buf_cnt);

	return (0);
}

void
n2cp_destroy_kmem_caches(n2cp_t *n2cp)
{

	if (n2cp->n_request_cache) {
		kmem_cache_destroy(n2cp->n_request_cache);
		n2cp->n_request_cache = NULL;
	}

	if (n2cp->n_buffer_cache) {
		kmem_cache_destroy(n2cp->n_buffer_cache);
		n2cp->n_buffer_cache = NULL;
	}

	if (n2cp->n_ctx_cache) {
		kmem_cache_destroy(n2cp->n_ctx_cache);
		n2cp->n_ctx_cache = NULL;
	}

}

int
n2cp_alloc_kmem_caches(n2cp_t *n2cp)
{
	n2cp_pagesize = ddi_ptob(n2cp->n_dip, 1);

	n2cp->n_reqctx_sz = round_to_power2(sizeof (nr_ctx_t));

	/*
	 * allocate context cache - we need these to be physically
	 * contiguous - by rounding size to a power of 2 and specifying
	 * the size as an alignment requirement we can insure that
	 * the context does not cross a page boundary, as long as the
	 * size is < pagesize.  Currently the context is much less
	 * than a page.
	 */

	if (n2cp->n_reqctx_sz > n2cp_pagesize) {
		return (DDI_FAILURE);
	}

	n2cp->n_ctx_cache = kmem_cache_create("n2cp_ctx_cache",
	    n2cp->n_reqctx_sz, n2cp->n_reqctx_sz,
	    NULL, NULL, NULL, NULL, NULL, 0);

	if (n2cp->n_ctx_cache == NULL) {
		goto failed;
	}

	n2cp->n_buffer_cache = kmem_cache_create("n2cp_buf_cache",
	    sizeof (n2cp_buf_struct_t), 64, n2cp_buf_constructor,
	    n2cp_buf_destructor, NULL, n2cp, NULL, 0);

	if (n2cp->n_buffer_cache == NULL) {
		goto failed;
	}

	n2cp->n_request_cache = kmem_cache_create("n2cp_req_cache",
	    sizeof (n2cp_request_t), 64, n2cp_req_constructor,
	    n2cp_req_destructor, NULL, n2cp, NULL, 0);

	if (n2cp->n_request_cache == NULL) {
		goto failed;
	}

	return (DDI_SUCCESS);


failed:
	n2cp_destroy_kmem_caches(n2cp);

	return (DDI_FAILURE);
}

/*
 * Setup and also register to kCF
 */
int
n2cp_init(n2cp_t *n2cp)
{
	int			ret;
	n2cp_cwq2cpu_map_t	*c2cp = &n2cp->n_cwqmap;

	/* initialize kstats */
	n2cp_ksinit(n2cp);

	/* only register if we have online crypto units */
	if (c2cp->m_ncwqs_online) {
		/* register with the crypto framework */
		if ((ret = n2cp_provider_register(n2cp)) != 0) {
			cmn_err(CE_WARN,
			    "crypto_register_provider() failed (%d)", ret);
			n2cp_ksdeinit(n2cp);
			return (DDI_FAILURE);
		}
	}

	return (DDI_SUCCESS);
}

/*
 * Unregister from kCF and cleanup
 */
int
n2cp_uninit(n2cp_t *n2cp)
{
	/*
	 * Unregister from kCF.
	 * This needs to be done at the beginning of detach.
	 */
	if (n2cp_isregistered(n2cp)) {
		if (crypto_unregister_provider(n2cp->n_prov)
		    != CRYPTO_SUCCESS) {
			n2cp_error(n2cp, "unable to unregister from kcf");
			return (DDI_FAILURE);
		}
		n2cp_clrregistered(n2cp);
	}

	/* deinitialize kstats */
	n2cp_ksdeinit(n2cp);

	return (DDI_SUCCESS);
}

/*
 * Make a copy of the mechanism table, removing the specified mechanism.
 */
static void
remove_mechanism(crypto_mech_type_t type)
{
	crypto_mech_info_t *old, *new;
	int count, new_count, i;

	count = n2cp_prov_info.pi_mech_list_count;
	old = n2cp_prov_info.pi_mechanisms;
	for (i = 0; i < count; i++) {
		if (old[i].cm_mech_number == type) {
			break;
		}
	}
	if (i == count)
		return;

	/* make sure there is at least one entry */
	ASSERT(count > 1);

	new_count = count - 1;
	new = kmem_alloc(new_count * sizeof (crypto_mech_info_t), KM_SLEEP);
	bcopy(old, new, i * sizeof (crypto_mech_info_t));
	bcopy(&old[i + 1], &new[i],
	    (new_count - i) * sizeof (crypto_mech_info_t));

	/* don't free the static table */
	if (old != n2cp_mech_info_table) {
		kmem_free(old, count * sizeof (crypto_mech_info_t));
	}
	n2cp_prov_info.pi_mechanisms = new;
	n2cp_prov_info.pi_mech_list_count = new_count;
}

int
n2cp_provider_register(n2cp_t *n2cp)
{
	int		ret;
	char		ID[64];
	dev_info_t	*dip = n2cp->n_dip;

	if (n2cp_isregistered(n2cp)) {
		return (CRYPTO_SUCCESS);
	}

	/* register with the crypto framework */
	/* Be careful not to exceed 32 chars */
	(void) sprintf(ID, "%s/%d %s",
	    ddi_driver_name(dip), ddi_get_instance(dip), IDENT_BULK);
	n2cp_prov_info.pi_provider_description = ID;
	n2cp_prov_info.pi_provider_dev.pd_hw = dip;
	n2cp_prov_info.pi_provider_handle = n2cp;

	if (is_KT(n2cp)) {
		crypto_mech_info_t *new;
		size_t new_size;

		new_size = sizeof (n2cp_mech_info_table) + sizeof (kt_mechs);
		new = kmem_alloc(new_size, KM_SLEEP);
		bcopy(n2cp_mech_info_table, new, sizeof (n2cp_mech_info_table));
		bcopy(kt_mechs, (caddr_t)new + sizeof (n2cp_mech_info_table),
		    sizeof (kt_mechs));
		n2cp_prov_info.pi_mechanisms = new;
		n2cp_prov_info.pi_mech_list_count =
		    new_size / sizeof (crypto_mech_info_t);

		remove_mechanism(RC4_WSTRM_MECH_INFO_TYPE);
	} else {
		n2cp_prov_info.pi_flags |= CRYPTO_HASH_NO_UPDATE;

		/* multi-part hash support is only available on KT */
		n2cp_digest_ops.digest_update = NULL;
		n2cp_digest_ops.digest_final = NULL;
	}

	n2cp_prov_info.pi_flags |= CRYPTO_SYNCHRONOUS;

	ret = crypto_register_provider(&n2cp_prov_info, &n2cp->n_prov);
	/* check if mechanism table is the static table */
	if (n2cp_prov_info.pi_mechanisms != n2cp_mech_info_table) {
		kmem_free(n2cp_prov_info.pi_mechanisms,
		    n2cp_prov_info.pi_mech_list_count *
		    sizeof (crypto_mech_info_t));
	}
	if (ret != CRYPTO_SUCCESS) {
		return (CRYPTO_FAILED);
	}

	n2cp_setregistered(n2cp);

	return (CRYPTO_SUCCESS);
}

/*
 * Notify the framework that it is ready to accept jobs now
 */
void
n2cp_provider_notify_ready(n2cp_t *n2cp)
{
	crypto_provider_notification(n2cp->n_prov, CRYPTO_PROVIDER_READY);
}


int
n2cp_provider_unregister(n2cp_t *n2cp)
{
	int	rv;

	if (!n2cp_isregistered(n2cp)) {
		return (CRYPTO_SUCCESS);
	}

	if ((rv = crypto_unregister_provider(n2cp->n_prov)) != CRYPTO_SUCCESS) {
		n2cp_error(n2cp, "unable to unregister from kcf");
		return (rv);
	}

	n2cp->n_prov = NULL;
	n2cp_clrregistered(n2cp);

	return (CRYPTO_SUCCESS);
}


void
n2cp_initq(n2cp_listnode_t *q)
{
	q->nl_next = q;
	q->nl_prev = q;
}

void
n2cp_enqueue(n2cp_listnode_t *q, n2cp_listnode_t *node)
{
	/*
	 * Enqueue submits at the "tail" of the list, i.e. just
	 * behind the sentinel.
	 */
	node->nl_next = q;
	node->nl_prev = q->nl_prev;
	node->nl_next->nl_prev = node;
	node->nl_prev->nl_next = node;
}

void
n2cp_rmqueue(n2cp_listnode_t *node)
{
	ASSERT(node != NULL);

	node->nl_next->nl_prev = node->nl_prev;
	node->nl_prev->nl_next = node->nl_next;
	node->nl_next = node;
	node->nl_prev = node;
}


n2cp_listnode_t *
n2cp_dequeue(n2cp_listnode_t *q)
{
	n2cp_listnode_t *node;
	/*
	 * Dequeue takes from the "head" of the list, i.e. just after
	 * the sentinel.
	 */
	if ((node = q->nl_next) == q) {
		/* queue is empty */
		return (NULL);
	}
	n2cp_rmqueue(node);
	return (node);
}


static size_t
round_to_power2(int sz)
{
	int	new_size = CWQ_ALIGNMENT;

	while (sz > new_size)
		new_size <<= 1;

	return (new_size);
}


int
n2cp_get_inbuf(n2cp_t *n2cp, n2cp_request_t *reqp)
{
	n2cp_buf_struct_t *elm;

	if (n2cp_use_ulcwq) {
		return (CRYPTO_SUCCESS);
	}

	/* the request already has a buffer */
	if (reqp->nr_in_buf != NULL) {
		return (CRYPTO_SUCCESS);
	}

	elm = kmem_cache_alloc(n2cp->n_buffer_cache, KM_SLEEP);

	if (elm == NULL) {
		return (CRYPTO_HOST_MEMORY);
	}

	reqp->nr_in_buf = elm->buf;
	reqp->nr_in_buf_paddr = elm->buf_paddr;
	reqp->nr_in_buf_struct = elm;
	atomic_inc_64(&n2cp->n_inbuf_allocs);

	return (CRYPTO_SUCCESS);
}

int
n2cp_get_outbuf(n2cp_t *n2cp, n2cp_request_t *reqp)
{
	n2cp_buf_struct_t *elm;

	if (n2cp_use_ulcwq) {
		return (CRYPTO_SUCCESS);
	}

	/* the request already has a buffer */
	if (reqp->nr_out_buf != NULL) {
		return (CRYPTO_SUCCESS);
	}

	elm = kmem_cache_alloc(n2cp->n_buffer_cache, KM_SLEEP);

	if (elm == NULL) {
		return (CRYPTO_HOST_MEMORY);
	}

	reqp->nr_out_buf = elm->buf;
	reqp->nr_out_buf_paddr = elm->buf_paddr;
	reqp->nr_out_buf_struct = elm;

	atomic_inc_64(&n2cp->n_outbuf_allocs);
	return (CRYPTO_SUCCESS);
}

cwq_entry_t *
n2cp_ulcwq_getbuf(n2cp_request_t *reqp, cwq_t *cwq)
{
	uint64_t		paddr;
	uchar_t			*buf;
	void			*unused_link;
	n2cp_ulcwq_buf_t	*ulcwq_buf;
	cwq_entry_t		*cep;
	n2cp_request_t		*other_reqp;

	mutex_enter(&cwq->cq_ulcwq_buf_lock);
	reqp->nr_ulcwq_buf = (n2cp_ulcwq_buf_t *)
	    n2cp_dequeue(&cwq->cq_ulcwq_freebufs);
	if (reqp->nr_ulcwq_buf == NULL) {
		unused_link = (void *) n2cp_dequeue(&cwq->cq_ulcwq_unusedbufs);
		if (unused_link == NULL) {
			if (!reqp->nr_ulcwq_buf_cv_initialized) {
				cv_init(&reqp->nr_ulcwq_buf_cv,
				    NULL, CV_DRIVER, NULL);
				reqp->nr_ulcwq_buf_cv_initialized = TRUE;
			}
			n2cp_enqueue(&cwq->cq_reqs_waiting_for_ulcwq_buf,
			    &(reqp->nr_ulcwq_buf_link));
			cv_wait(&reqp->nr_ulcwq_buf_cv,
			    &cwq->cq_ulcwq_buf_lock);
			if (reqp->nr_cep == NULL) {
				/* this cwq is going down */
				mutex_exit(&cwq->cq_ulcwq_buf_lock);
				return (NULL);
			}
		} else {
			/* LINTED E_BAD_PTR_CAST_ALIGN */
			ulcwq_buf = (n2cp_ulcwq_buf_t *)
			    (((char *)unused_link) -
			    offsetof(n2cp_ulcwq_buf_t, ub_unused_link));
			other_reqp = ulcwq_buf->ub_req;
			cep = other_reqp->nr_cep;
			ASSERT(&(cep->mm_queue) == cwq);
			n2cp_move_req_off_page(other_reqp,
			    KM_NOSLEEP, B_TRUE);
			n2cp_relecwq(cep);
			reqp->nr_ulcwq_buf = ulcwq_buf;
		}
	}
	reqp->nr_ulcwq_buf->ub_req = reqp;
	mutex_exit(&cwq->cq_ulcwq_buf_lock);
	buf = (uchar_t *)(reqp->nr_ulcwq_buf);
	paddr = reqp->nr_ulcwq_buf->ub_buf_paddr;
	reqp->nr_in_buf = buf +
	    offsetof(n2cp_ulcwq_buf_t, ub_inbuf);
	reqp->nr_in_buf_paddr = paddr +
	    offsetof(n2cp_ulcwq_buf_t, ub_inbuf);
	reqp->nr_out_buf = buf +
	    offsetof(n2cp_ulcwq_buf_t, ub_outbuf);
	reqp->nr_out_buf_paddr = paddr +
	    offsetof(n2cp_ulcwq_buf_t, ub_outbuf);
	reqp->nr_context_sz = sizeof (nr_ctx_t);
	/* LINTED E_BAD_PTR_CAST_ALIGN */
	reqp->nr_context = (nr_ctx_t *)(buf +
	    offsetof(n2cp_ulcwq_buf_t, ub_ctx));
	bzero(reqp->nr_context, reqp->nr_context_sz);
	reqp->nr_context_paddr = paddr +
	    offsetof(n2cp_ulcwq_buf_t, ub_ctx);
	return (reqp->nr_cep);
}


void
n2cp_ulcwq_freebuf(n2cp_request_t *reqp)
{
	cwq_entry_t	*cep;
	cwq_t		*cwq;
	n2cp_request_t	*newreqp;
	n2cp_listnode_t *link;

	cep = reqp->nr_cep;
	if (cep == NULL) {
		kmem_free(reqp->nr_context, sizeof (nr_ctx_t));
		return;
	}
	cwq = &(cep->mm_queue);
	mutex_enter(&cwq->cq_ulcwq_buf_lock);
	if (reqp->nr_cep == NULL) {
		kmem_free(reqp->nr_context, sizeof (nr_ctx_t));
		mutex_exit(&cwq->cq_ulcwq_buf_lock);
		return;
	}
	if (reqp->nr_ulcwq_buf->ub_req != reqp) {
		/* the buffer was already given to another request */
		mutex_exit(&cwq->cq_ulcwq_buf_lock);
		return;
	}
	n2cp_relecwq(reqp->nr_cep);
	(void) n2cp_rmqueue(&reqp->nr_ulcwq_buf->ub_unused_link);
	link = n2cp_dequeue(&cwq->cq_reqs_waiting_for_ulcwq_buf);
	reqp->nr_ulcwq_buf->ub_req = NULL;
	if (link != NULL) {
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		newreqp = (n2cp_request_t *)
		    ((char *)link -
		    offsetof(n2cp_request_t, nr_ulcwq_buf_link));
		newreqp->nr_ulcwq_buf = reqp->nr_ulcwq_buf;
		cv_signal(&newreqp->nr_ulcwq_buf_cv);
	} else {
		n2cp_enqueue(&(cwq->cq_ulcwq_freebufs),
		    (n2cp_listnode_t *)(reqp->nr_ulcwq_buf));
	}
	mutex_exit(&cwq->cq_ulcwq_buf_lock);
}


void
check_draining(n2cp_request_t *reqp)
{
	n2cp_listnode_t	*link;
	n2cp_request_t	*newreqp;
	cwq_entry_t	*cep;
	cwq_t		*cwq;

	if (!n2cp_use_ulcwq) {
		return;
	}

	/*
	 * Needs to unbind from cpu if already bound.
	 * This can happen for ulcwq only when the crypto operation
	 * fails before it gets to n2cp_start().
	 */
	if ((curthread->t_affinitycnt > 0) &&
	    (curthread->t_bound_cpu != NULL)) {
		thread_affinity_clear(curthread);
	}

	kpreempt_disable();
	cep = reqp->nr_cep;
	if (cep == NULL) {
		kpreempt_enable();
		return;
	}
	cwq = &(cep->mm_queue);
	mutex_enter(&cwq->cq_ulcwq_buf_lock);

	/* check nr_cep with cq_ulcwq_buf_lock held */
	if (reqp->nr_cep == NULL) {
		goto out;
	}

	if (reqp->nr_cep->mm_state == CWQ_STATE_DRAINING) {
		n2cp_move_req_off_page(reqp, KM_NOSLEEP, B_FALSE);
	} else {
		link = n2cp_dequeue(&cwq->cq_reqs_waiting_for_ulcwq_buf);
		if (link != NULL) {
			n2cp_move_req_off_page(reqp, KM_NOSLEEP, B_TRUE);
			/* LINTED E_BAD_PTR_CAST_ALIGN */
			newreqp = (n2cp_request_t *)((char *)link -
			    offsetof(n2cp_request_t, nr_ulcwq_buf_link));
			newreqp->nr_ulcwq_buf = reqp->nr_ulcwq_buf;
			cv_signal(&newreqp->nr_ulcwq_buf_cv);
		} else {
			link = &(reqp->nr_ulcwq_buf->ub_unused_link);
			n2cp_enqueue(&(cwq->cq_ulcwq_unusedbufs), link);
		}
	}
out:
	kpreempt_enable();
	mutex_exit(&cwq->cq_ulcwq_buf_lock);
}


void
n2cp_move_req_off_page(n2cp_request_t *reqp, int km_flag,
    boolean_t ulcwq_buf_reused)
{
	nr_ctx_t	context;
	cwq_entry_t	*cep = reqp->nr_cep;
	cwq_t		*cwq = &(cep->mm_queue);

	ASSERT(mutex_owned(&cwq->cq_ulcwq_buf_lock));
	if (reqp->nr_cep != NULL) {
		context =  *(reqp->nr_context);
		reqp->nr_context = kmem_alloc(sizeof (nr_ctx_t), km_flag);
		if (reqp->nr_context != NULL) {
			*(reqp->nr_context) = context;
		}
		if (!ulcwq_buf_reused) {
			/*
			 * If the nr_ulcwq_buf is not reused by the caller,
			 * put the buf in the free list.
			 */
			reqp->nr_ulcwq_buf->ub_req = NULL;
			n2cp_enqueue(&(cwq->cq_ulcwq_freebufs),
			    (n2cp_listnode_t *)(reqp->nr_ulcwq_buf));
			cep->mm_user_cnt--;
		}
		reqp->nr_cep = NULL;
	}
}


void
n2cp_ulcwq_detach_waiting_threads(cwq_t *cwq)
{
	n2cp_listnode_t *link;
	n2cp_request_t	*reqp;

	mutex_enter(&cwq->cq_ulcwq_buf_lock);
	link = n2cp_dequeue(&cwq->cq_reqs_waiting_for_ulcwq_buf);
	while (link != NULL) {
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		reqp = (n2cp_request_t *)
		    ((char *)link -
		    offsetof(n2cp_request_t, nr_ulcwq_buf_link));
		reqp->nr_cep = NULL;
		cv_signal(&reqp->nr_ulcwq_buf_cv);
		link = n2cp_dequeue(&cwq->cq_reqs_waiting_for_ulcwq_buf);
	}
	mutex_exit(&cwq->cq_ulcwq_buf_lock);
}

void
n2cp_ulcwq_move_unused_buf(cwq_t *cwq)
{
	n2cp_listnode_t		*link;
	n2cp_request_t		*reqp;
	n2cp_ulcwq_buf_t	*ulcwq_buf;

	mutex_enter(&cwq->cq_ulcwq_buf_lock);
	link = n2cp_dequeue(&cwq->cq_ulcwq_unusedbufs);
	while (link != NULL) {
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		ulcwq_buf = (n2cp_ulcwq_buf_t *)
		    (((char *)link) - offsetof(n2cp_ulcwq_buf_t,
		    ub_unused_link));
		reqp = ulcwq_buf->ub_req;
		n2cp_move_req_off_page(reqp,
		    KM_NOSLEEP, B_FALSE);
		link = n2cp_dequeue(&cwq->cq_ulcwq_unusedbufs);
	}
	mutex_exit(&cwq->cq_ulcwq_buf_lock);
}


void
n2cp_free_buf(n2cp_t *n2cp, n2cp_request_t *reqp)
{
	if (n2cp_use_ulcwq) {
		return;
	}

	if ((reqp->nr_in_buf_struct == NULL) &&
	    (reqp->nr_out_buf_struct == NULL)) {
		return;
	}

	if (reqp->nr_in_buf_struct) {
		n2cp_buf_struct_t *elm = reqp->nr_in_buf_struct;

		kmem_cache_free(n2cp->n_buffer_cache, elm);
		reqp->nr_in_buf = NULL;
		reqp->nr_in_buf_paddr = 0;
		reqp->nr_in_buf_struct = NULL;
	}
	if (reqp->nr_out_buf_struct) {
		n2cp_buf_struct_t *elm = reqp->nr_out_buf_struct;

		kmem_cache_free(n2cp->n_buffer_cache, elm);
		reqp->nr_out_buf = NULL;
		reqp->nr_out_buf_paddr = 0;
		reqp->nr_out_buf_struct = NULL;
	}
}

static nr_ctx_t *
n2cp_alloc_ctx(n2cp_t *n2cp)
{
	if (N2CP_ERRATUM_175_ENABLED()) {
		return ((nr_ctx_t *)n2_contig_alloc(n2cp->n_reqctx_sz));
	} else {
		return ((nr_ctx_t *)kmem_cache_alloc(n2cp->n_ctx_cache,
		    KM_SLEEP));
	}
}

static void
n2cp_free_ctx(n2cp_request_t *reqp)
{
	if (N2CP_ERRATUM_175_ENABLED()) {
		n2_contig_free((void *)reqp->nr_context, reqp->nr_context_sz);
	} else {
		kmem_cache_free(reqp->nr_n2cp->n_ctx_cache, reqp->nr_context);
	}
}


n2cp_request_t *
n2cp_getreq(n2cp_t *n2cp)
{
	n2cp_request_t	*reqp;

	reqp = kmem_cache_alloc(n2cp->n_request_cache, KM_SLEEP);

	if (reqp) {
		if (n2cp_use_ulcwq) {
			cwq_t		*cwq;
			cwq_entry_t	*cep;

		tryagain:
			if (n2cp_find_cep(n2cp, &cep, NULL, -1) !=
			    CRYPTO_SUCCESS) {
				kmem_cache_free(n2cp->n_request_cache, reqp);
				return (NULL);
			}

			thread_affinity_clear(curthread);
			reqp->nr_cep = cep;
			cwq = &cep->mm_queue;
			if (n2cp_ulcwq_getbuf(reqp, cwq) == NULL) {
				n2cp_relecwq(cep);
				/*
				 * If this is a POST request, must be bound to
				 * a specific CWQ.  If it fails to get a buf on
				 * the CWQ, return failure.
				 */
				if (N2CP_FIPS_POST_RUNNING) {
					kmem_cache_free(n2cp->n_request_cache,
					    reqp);
					return (NULL);
				}
				goto tryagain;
			}
		}

		reqp->nr_flags &= ~(N2CP_SCATTER | N2CP_GATHER);
		reqp->nr_callback = NULL;
		reqp->nr_job_stat = -1;

		/* use usecs for timeout */
		reqp->nr_timeout = (n2cp_jobtimeout_secs * SECOND)
		    * n2cp->n_spins_per_usec;
	}

	return (reqp);
}

void
n2cp_freereq(n2cp_request_t *reqp)
{
	n2cp_t *n2cp = reqp->nr_n2cp;
	int	i;

/* EXPORT DELETE START */

	ASSERT(reqp->nr_job_state != N2CP_JOBSTATE_PENDING);
	reqp->nr_job_state = N2CP_JOBSTATE_FREED;

	reqp->nr_kcfreq = NULL;

	/* check mechanism */
	switch (N2CP_MECH_FROM_REQ(reqp)) {
	case AES_CCM_MECH_INFO_TYPE:
	case AES_GCM_MECH_INFO_TYPE:
	case AES_GMAC_MECH_INFO_TYPE:
		crypto_free_mode_ctx(reqp->nr_mode_ctx);
		/* FALLTHRU */
	case DES_CBC_MECH_INFO_TYPE:
	case DES_CBC_PAD_MECH_INFO_TYPE:
	case DES_ECB_MECH_INFO_TYPE:
	case DES_CFB_MECH_INFO_TYPE:
	case DES3_CBC_MECH_INFO_TYPE:
	case DES3_CBC_PAD_MECH_INFO_TYPE:
	case DES3_ECB_MECH_INFO_TYPE:
	case DES3_CFB_MECH_INFO_TYPE:
	case AES_CBC_MECH_INFO_TYPE:
	case AES_CFB_MECH_INFO_TYPE:
	case AES_CBC_PAD_MECH_INFO_TYPE:
	case AES_ECB_MECH_INFO_TYPE:
	case AES_CTR_MECH_INFO_TYPE:
	case RC4_WSTRM_MECH_INFO_TYPE:
	case RC4_WOSTRM_MECH_INFO_TYPE:
		n2cp_clean_blockctx(reqp);
		break;
	case MD5_HMAC_MECH_INFO_TYPE:
	case MD5_HMAC_GENERAL_MECH_INFO_TYPE:
	case SSL3_MD5_MAC_MECH_INFO_TYPE:
	case SHA1_HMAC_MECH_INFO_TYPE:
	case SHA1_HMAC_GENERAL_MECH_INFO_TYPE:
	case SSL3_SHA1_MAC_MECH_INFO_TYPE:
	case SHA256_HMAC_MECH_INFO_TYPE:
	case SHA256_HMAC_GENERAL_MECH_INFO_TYPE:
#ifdef	SSL3_SHA256_MAC_SUPPORT
	case SSL3_SHA256_MAC_MECH_INFO_TYPE:
#endif
		n2cp_clean_hmacctx(reqp);
		break;
	case MD5_MECH_INFO_TYPE:
	case SHA1_MECH_INFO_TYPE:
	case SHA256_MECH_INFO_TYPE:
	case SHA384_MECH_INFO_TYPE:
	case SHA512_MECH_INFO_TYPE:
		/* nothing to clean up */
		break;
	default:
		/* Should never reach here */
		cmn_err(CE_WARN, "free_context: unexpected mech type "
		    "0x%x\n", reqp->nr_cmd);
	}
	/* clear the control words */
	for (i = 0; i < reqp->nr_cwcnt; i++) {
		reqp->nr_cws[i].cw_ctlbits = 0;
		reqp->nr_cws[i].cw_src_addr = 0;
		reqp->nr_cws[i].cw_auth_key_addr = 0;
		reqp->nr_cws[i].cw_auth_iv_addr = 0;
		reqp->nr_cws[i].cw_final_auth_state_addr = 0;
		reqp->nr_cws[i].cw_enc_key_addr = 0;
		reqp->nr_cws[i].cw_enc_iv_addr = 0;
		reqp->nr_cws[i].cw_dst_addr = 0;
	}
	reqp->nr_cwcnt = 0;

	/* make sure buffers are freed */
	if (n2cp_use_ulcwq) {
		/*
		 * Needs to unbind from cpu if already bound.
		 * This can happen for ulcwq only when the crypto operation
		 * fails before it gets to n2cp_start().
		 */
		if ((curthread->t_affinitycnt > 0) &&
		    (curthread->t_bound_cpu != NULL)) {
			thread_affinity_clear(curthread);
		}
		n2cp_ulcwq_freebuf(reqp);
	} else {
		n2cp_free_buf(n2cp, reqp);
	}

	kmem_cache_free(n2cp->n_request_cache, reqp);

/* EXPORT DELETE END */

}

/*
 * data: source crypto_data_t struct
 * off: offset into the source of the current position before commencing copy
 * count: the amount of data to copy
 * dest: destination buffer
 */
void
n2cp_getbufbytes(crypto_data_t *data, int off, int count, char *dest)
{
	uio_t *uiop;
	uint_t vec_idx;
	size_t cur_len;
	mblk_t *mp;

/* EXPORT DELETE START */

	if (count == 0) {
		/* We don't want anything so we're done. */
		return;
	}

	/*
	 * Sanity check that we haven't specified a length greater than the
	 * offset adjusted size of the buffer.
	 */
	ASSERT(count <= (data->cd_length - off));

	/* Add the internal crypto_data offset to the requested offset. */
	off += data->cd_offset;

	switch (data->cd_format) {
	case CRYPTO_DATA_RAW:
		BCOPY((char *)data->cd_raw.iov_base + off, dest, count);
		break;

	case CRYPTO_DATA_UIO:
		/*
		 * Jump to the first iovec containing data to be
		 * processed.
		 */
		uiop = data->cd_uio;
		for (vec_idx = 0; vec_idx < uiop->uio_iovcnt &&
		    off >= uiop->uio_iov[vec_idx].iov_len;
		    off -= uiop->uio_iov[vec_idx++].iov_len)
			;

		/*
		 * The caller specified an offset that is larger than
		 * the total size of the buffers it provided.
		 */
		ASSERT(vec_idx != uiop->uio_iovcnt);


		/*
		 * Now process the iovecs.
		 */
		while (vec_idx < uiop->uio_iovcnt && count > 0) {
			cur_len = min((int)(uiop->uio_iov[vec_idx].iov_len) -
			    off, count);

			(void) ddi_copyin(
			    (char *)uiop->uio_iov[vec_idx].iov_base + off,
			    dest, cur_len,
			    uiop->uio_segflg == UIO_SYSSPACE ? FKIOCTL : 0);
			count -= cur_len;
			dest += cur_len;
			vec_idx++;
			off = 0;
		}

		/*
		 * The end of the specified iovec's was reached but the
		 * length requested could not be processed requested to
		 * digest more data than it provided
		 */
		ASSERT((vec_idx != uiop->uio_iovcnt) || (count == 0));

		break;
	case CRYPTO_DATA_MBLK:
		/*
		 * Jump to the first mblk_t containing data to be processed.
		 */
		for (mp = data->cd_mp;
		    mp != NULL && off >= N_MBLKL(mp);
		    off -= N_MBLKL(mp), mp = mp->b_cont)
			;
		/*
		 * The caller specified an offset that is larger than
		 * the total size of the buffers it provided.
		 */
		ASSERT(mp != NULL);

		/*
		 * Now do the processing on the mblk chain.
		 */
		while (mp != NULL && count > 0) {
			cur_len = min((int)((int)(N_MBLKL(mp) - off)),
			    count);
			BCOPY((char *)(mp->b_rptr + off), dest, cur_len);
			count -= cur_len;
			dest += cur_len;
			mp = mp->b_cont;
			off = 0;
		}

		/*
		 * The end of the mblk was reached but the length
		 * requested could not be processed, (requested to
		 * digest more data than it provided).
		 */
		ASSERT((mp != NULL) || (count == 0));

		break;
	default:
		DBG0(NULL, DWARN, "unrecognised crypto data format");
	}

/* EXPORT DELETE END */

}

/*
 * This functions returns the address of the buffer at the current position
 */
char *
n2cp_get_dataaddr(crypto_data_t *buf, off_t off)
{
	uio_t	*uiop;
	mblk_t	*mp;
	uint_t	vec_idx = 0;
	size_t	offset;

/* EXPORT DELETE START */

	switch (buf->cd_format) {
	case CRYPTO_DATA_RAW:
		return ((char *)buf->cd_raw.iov_base + buf->cd_offset + off);
	case CRYPTO_DATA_UIO:
		uiop = buf->cd_uio;
		offset = buf->cd_offset + off;
		while (offset > uiop->uio_iov[vec_idx].iov_len) {
			offset -= uiop->uio_iov[vec_idx].iov_len;
			vec_idx++;
		}
		return ((char *)uiop->uio_iov[vec_idx].iov_base + offset);
	case CRYPTO_DATA_MBLK:
		mp = buf->cd_mp;
		offset = buf->cd_offset + off;
		while (offset > N_MBLKL(mp)) {
			offset -= N_MBLKL(mp);
			mp = mp->b_cont;
		}
		return ((char *)mp->b_rptr + offset);
	}


/* EXPORT DELETE END */

	return (NULL);
}

int
n2cp_get_bufsz(crypto_data_t *buf)
{
	int	i;
	uio_t	*uiop;
	mblk_t	*mp;
	size_t	len = 0;

/* EXPORT DELETE START */

	switch (buf->cd_format) {
	case CRYPTO_DATA_RAW:
		return (buf->cd_raw.iov_len - buf->cd_offset);
	case CRYPTO_DATA_UIO:
		uiop = buf->cd_uio;
		for (i = 0; i < uiop->uio_iovcnt; i++) {
			len += uiop->uio_iov[i].iov_len;
		}
		return (len - buf->cd_offset);
	case CRYPTO_DATA_MBLK:
		mp = buf->cd_mp;
		while (mp != NULL) {
			len += N_MBLKL(mp);
			mp = mp->b_cont;
		}
		return (len - buf->cd_offset);
	}


/* EXPORT DELETE END */

	return (0);
}

/*
 * This is called for input buffer
 */
void
n2cp_setresid(crypto_data_t *data, int len)
{
	if (data) {
		data->cd_offset += (data->cd_length - len);
		data->cd_length = len;
	}
}

/*
 * This function checks to see if the buffer is physically contiguous
 * by looking at the physical address for each page using va_to_pa.
 * If 'buf' is physically contiguous, return B_TRUE. Otherwise, return B_FALSE.
 * This should be used for small pages.
 */
static boolean_t
is_pcontig_smpg(char *buf, int buflen)
{
	uint64_t	expected_paddr;
	int		chunksz;

	/*
	 * 'expected_paddr' is the physical address of the next page
	 * if the buff is physically contiguous.
	 */
	chunksz = min(buflen, n2cp_pagesize - ((uintptr_t)buf % n2cp_pagesize));
	expected_paddr = va_to_pa(buf) + chunksz;
	buf += chunksz;
	buflen -= chunksz;

	while (buflen > 0) {
		if (va_to_pa(buf) != expected_paddr) {
			/* the buffer is not physically contiguous */
			return (B_FALSE);
		}
		chunksz = min(buflen, n2cp_pagesize);

		/* calculate the expected physical addr of the next page */
		expected_paddr += chunksz;
		buf += chunksz;
		buflen -= chunksz;
	}

	return (B_TRUE);
}

/*
 * This function efficiently checks to see if the buffer is physically
 * contiguous. If 'buf' is physically contiguous, return B_TRUE. Otherwise,
 * return B_FALSE.
 */
static boolean_t
is_pcontig(char *buf, int buflen)
{
	if (buflen <= 0) {
		return (B_TRUE);
	}

	/* do the LARGEPAGE check */
	if (IS_KMEM_VA_LARGEPAGE(buf)) {
		uint64_t	chunksz;

		/*
		 * If 'buf' was allocated on the large page, check to see
		 * if the entire buffer fits in the large page. If so,
		 * no need to do the inefficient page-by-page contiguous
		 * checking.
		 */
		chunksz = ROUNDUP((uintptr_t)buf, segkmem_lpsize) -
		    (uintptr_t)buf;

		if (buflen <= chunksz) {
			/* 'buf' fits in the large page */
			return (B_TRUE);
		} else {
			/*
			 * 'buf' does not fit in the large page. Check if it
			 * is contiguous page-by-page
			 */
			return (is_pcontig_smpg(buf, buflen));
		}
	} else {
		/*
		 * The buf is on small page. Do the contiguous checking
		 * page by page.
		 */
		if (btop((uint64_t)buf) == btop((uint64_t)(&buf[buflen]))) {
			/* all data w/in a single page */
			return (B_TRUE);
		}
		return (is_pcontig_smpg(buf, buflen));
	}
}

/*
 * Performs the input, output or hard scatter/gather checks on the specified
 * crypto_data_t struct. Returns true if the data is scatter/gather in nature
 * ie fails the test.
 */
int
n2cp_sgcheck(crypto_data_t *data, int sg_flags)
{
	uio_t	*uiop;
	mblk_t	*mp;

/* EXPORT DELETE START */

	if (N2CP_ERRATUM_175_ENABLED()) {
		return (TRUE);
	}

	if (n2cp_use_ulcwq) {
		return (TRUE);
	}

	if (CRYPTO_DATA_IS_USERSPACE(data))
		return (TRUE);

	if ((sg_flags & N2CP_SG_CONTIG) || (sg_flags & N2CP_SG_PCONTIG)) {
		/*
		 * Check for a contiguous data buffer.
		 */
		switch (data->cd_format) {
		case CRYPTO_DATA_RAW:
			/* Contiguous in nature */
			break;

		case CRYPTO_DATA_UIO:
			if (data->cd_uio->uio_iovcnt > 1)
				return (TRUE);
			break;

		case CRYPTO_DATA_MBLK:
			mp = data->cd_mp;
			if (mp->b_cont != NULL)
				return (TRUE);
			break;

		default:
			DBG0(NULL, DWARN, "unrecognised crypto data format");
			return (FALSE);
		}
	}

	if (sg_flags & N2CP_SG_PCONTIG) {
		boolean_t	pcontig;

		/*
		 * Check if the buffer is physically contiguous.
		 */
		switch (data->cd_format) {
		case CRYPTO_DATA_RAW:
		{
			iovec_t	*iov = &data->cd_raw;
			pcontig = is_pcontig(iov->iov_base, iov->iov_len);
			break;
		}
		case CRYPTO_DATA_UIO:
		{
			iovec_t *iov = &(data->cd_uio->uio_iov[0]);
			pcontig = is_pcontig(iov->iov_base, iov->iov_len);
			break;
		}
		case CRYPTO_DATA_MBLK:
			mp = data->cd_mp;
			pcontig = is_pcontig((char *)mp->b_rptr, N_MBLKL(mp));
			break;
		default:
			DBG0(NULL, DWARN, "unrecognised crypto data format");
			return (FALSE);
		}
		if (pcontig == B_FALSE) {
			/* it is not physically contiguous: scatter/gather */
			return (TRUE);
		}
	}

	if (sg_flags & N2CP_SG_WALIGN) {
		/*
		 * Check for a contiguous data buffer that is 32-bit word
		 * aligned and is of word multiples in size.
		 */
		switch (data->cd_format) {
		case CRYPTO_DATA_RAW:
			if (!IS_P2ALIGNED(data->cd_raw.iov_len,
			    sizeof (uint32_t)) ||
			    !IS_P2ALIGNED(data->cd_raw.iov_base,
			    sizeof (uint32_t))) {
				return (TRUE);
			}
			break;

		case CRYPTO_DATA_UIO:
			uiop = data->cd_uio;
			if (uiop->uio_iovcnt > 1) {
				return (TRUE);
			}
			/* So there is only one iovec */
			if (!IS_P2ALIGNED(uiop->uio_iov[0].iov_len,
			    sizeof (uint32_t)) ||
			    !IS_P2ALIGNED(uiop->uio_iov[0].iov_base,
			    sizeof (uint32_t))) {
				return (TRUE);
			}
			break;

		case CRYPTO_DATA_MBLK:
			mp = data->cd_mp;
			if (mp->b_cont != NULL) {
				return (TRUE);
			}
			/* So there is only one mblk in the chain */
			if (!IS_P2ALIGNED(N_MBLKL(mp), sizeof (uint32_t)) ||
			    !IS_P2ALIGNED(mp->b_rptr, sizeof (uint32_t))) {
				return (TRUE);
			}
			break;

		default:
			DBG0(NULL, DWARN, "unrecognised crypto data format");
			return (FALSE);
		}
	}

	if (sg_flags & N2CP_SG_PALIGN) {
		/*
		 * Check that the data buffer is page aligned and is of
		 * page multiples in size.
		 */
		switch (data->cd_format) {
		case CRYPTO_DATA_RAW:
			if ((data->cd_length % n2cp_pagesize) ||
			    ((uintptr_t)data->cd_raw.iov_base %
			    n2cp_pagesize)) {
				return (TRUE);
			}
			break;

		case CRYPTO_DATA_UIO:
			uiop = data->cd_uio;
			if ((uiop->uio_iov[0].iov_len % n2cp_pagesize) ||
			    ((uintptr_t)uiop->uio_iov[0].iov_base %
			    n2cp_pagesize)) {
				return (TRUE);
			}
			break;

		case CRYPTO_DATA_MBLK:
			mp = data->cd_mp;
			if ((N_MBLKL(mp) % n2cp_pagesize) ||
			    ((uintptr_t)mp->b_rptr % n2cp_pagesize)) {
				return (TRUE);
			}
			break;

		default:
			DBG0(NULL, DWARN, "unrecognised crypto data format");
			return (FALSE);
		}
	}

/* EXPORT DELETE END */

	return (FALSE);
}

/*
 * Increments the cd_offset and decrements the cd_length as the data is
 * gathered from the crypto_data_t struct.
 */
int
n2cp_gather(crypto_data_t *in, char *dest, int count)
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
		BCOPY((char *)in->cd_raw.iov_base + in->cd_offset, dest, count);
		in->cd_offset += count;
		in->cd_length -= count;
		break;

	case CRYPTO_DATA_UIO:
		/*
		 * Jump to the first iovec containing data to be processed.
		 */
		uiop = in->cd_uio;
		for (vec_idx = 0; vec_idx < uiop->uio_iovcnt &&
		    off >= uiop->uio_iov[vec_idx].iov_len;
		    off -= uiop->uio_iov[vec_idx++].iov_len)
			;
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
			(void) ddi_copyin(
			    (char *)uiop->uio_iov[vec_idx].iov_base + off,
			    dest, cur_len,
			    uiop->uio_segflg == UIO_SYSSPACE ? FKIOCTL : 0);
			dest += cur_len;
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
		for (mp = in->cd_mp; mp != NULL && off >= N_MBLKL(mp);
		    off -= N_MBLKL(mp), mp = mp->b_cont)
			;
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
			BCOPY((char *)(mp->b_rptr + off), dest, cur_len);
			dest += cur_len;
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
		    "n2cp_gather: unrecognised crypto data format");
		rv = CRYPTO_ARGUMENTS_BAD;
	}

/* EXPORT DELETE END */

	return (rv);
}

int
n2cp_gather_zero_pad(crypto_data_t *in, caddr_t dest, size_t inlen, int padlen)
{
	int	rv;

	if (in && (inlen > 0)) {
		rv = n2cp_gather(in, dest, inlen);
		if (rv != CRYPTO_SUCCESS) {
			return (rv);
		}
	}

	/*
	 * pad the end of the dest buffer with zeros
	 */
	(void) memset(dest + inlen, 0, padlen);

	return (CRYPTO_SUCCESS);
}

/*
 * Append PKCS#11 padding to the dest buffer
 */
int
n2cp_gather_PKCS_pad(crypto_data_t *in, caddr_t dest, size_t inlen, int blocksz)
{
	int	rv;
	int	padlen;

	if (in && (inlen > 0)) {
		rv = n2cp_gather(in, dest, inlen);
		if (rv != CRYPTO_SUCCESS) {
			return (rv);
		}
	}

	/*
	 * append the PKCS#11 padding to the dest buffer
	 */
	padlen = blocksz - (inlen % blocksz);
	(void) memset(dest + inlen, (char)padlen, padlen);

	return (CRYPTO_SUCCESS);
}


/*
 * Appends the data to the crypto_data_t struct increasing cd_length.
 * cd_offset is left unchanged.
 */
int
n2cp_scatter(const char *src, crypto_data_t *out, int count)
{
	int	rv = CRYPTO_SUCCESS;
	off_t	offset = out->cd_offset + out->cd_length;
	uint_t	vec_idx;
	uio_t	*uiop;
	size_t	cur_len;
	mblk_t	*mp;

	if (count == 0) {
		/* nothing to do */
		return (CRYPTO_SUCCESS);
	}

	switch (out->cd_format) {
	case CRYPTO_DATA_RAW:
		if (out->cd_raw.iov_len - offset < count) {
			/* Trying to write out more than space available. */
			return (CRYPTO_DATA_LEN_RANGE);
		}
		BCOPY(src, (char *)out->cd_raw.iov_base + offset, count);
		out->cd_length += count;
		break;

	case CRYPTO_DATA_UIO:
		/*
		 * Jump to the first iovec that can be written to.
		 */
		uiop = out->cd_uio;
		for (vec_idx = 0; vec_idx < uiop->uio_iovcnt &&
		    offset >= uiop->uio_iov[vec_idx].iov_len;
		    offset -= uiop->uio_iov[vec_idx++].iov_len)
			;
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
			(void) ddi_copyout(src,
			    (char *)uiop->uio_iov[vec_idx].iov_base + offset,
			    cur_len,
			    uiop->uio_segflg == UIO_SYSSPACE ? FKIOCTL : 0);
			src += cur_len;
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
		for (mp = out->cd_mp; mp != NULL && offset >= N_MBLKL(mp);
		    offset -= N_MBLKL(mp), mp = mp->b_cont)
			;
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
			BCOPY(src, (char *)(mp->b_rptr + offset), cur_len);
			src += cur_len;
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

	return (rv);
}

/*
 * Remove the padding from the buf, and appends the unpadded data to the
 * out increasing cd_length. cd_offset is left unchanged.
 */
int
n2cp_scatter_PKCS_unpad(char *buf, int buflen, crypto_data_t *out,
    int blocksz)
{
	char		*cursor;
	char		padlen;
	int		i;

	padlen = buf[buflen - 1];
	if (padlen > blocksz) {
		DBG0(NULL, DCHATTY, "n2cp_scatter_PKCS_unpad: "
		    "invalid padding length");
		return (CRYPTO_ENCRYPTED_DATA_INVALID);
	}

	/*
	 * Make sure that the value of the padding is correct
	 * i.e. if there is 3 padding bytes, the padding must be 0x030303
	 */
	cursor = buf + buflen - padlen;
	for (i = 0; i < padlen; i++) {
		if (cursor[i] != padlen) {
			DBG0(NULL, DCHATTY, "n2cp_scatter_PKCS_unpad: "
			    "invalid padding");
			return (CRYPTO_ENCRYPTED_DATA_INVALID);
		}
	}

	return (n2cp_scatter((char *)buf, out, buflen - padlen));
}


/*
 * Return the address of the first data buffer. If the data format is
 * unrecognised return NULL.
 * The single buffer has been assured in the calling function.
 */
caddr_t
n2cp_bufdaddr(crypto_data_t *data)
{
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
		    "n2cp_bufdaddr: unrecognised crypto data format");
		return (NULL);
	}
}

/*
 * Control entry points.
 */

/* ARGSUSED */
static void
n2cp_provider_status(crypto_provider_handle_t provider, uint_t *status)
{
	/*
	 * n2cp should never be in a failed or busy state while it
	 * is registered through kEF.
	 */
	*status = CRYPTO_PROVIDER_READY;
}

/*
 * Hash entry points
 */

/*ARGSUSED*/
static int
n2cp_digest_init(crypto_ctx_t *ctx, crypto_mechanism_t *mechanism,
    crypto_req_handle_t req)
{
	return (n2cp_hashinit(ctx, mechanism));
}

static int
n2cp_digest(crypto_ctx_t *ctx, crypto_data_t *data, crypto_data_t *digest,
    crypto_req_handle_t req)
{
	int		rv;
	n2cp_request_t	*reqp;

	if (!ctx || !ctx->cc_provider || !ctx->cc_provider_private)
		return (CRYPTO_OPERATION_NOT_INITIALIZED);

	reqp = (n2cp_request_t *)ctx->cc_provider_private;

	if (!(reqp->nr_cmd & N2CP_OP_DIGEST)) {
		return (CRYPTO_OPERATION_NOT_INITIALIZED);
	}

	/*
	 * In-place operations (input == output) are indicated by having a
	 * NULL output. In this case set the output to point to the input.
	 */
	if (digest == NULL) {
		digest = data;
	}

	if (n2cp_use_ulcwq &&
	    ((rv = n2cp_find_cep_for_req(reqp)) != CRYPTO_SUCCESS)) {
		return (rv);
	}

	rv = n2cp_hash(ctx, data, digest, req);

	/* If it is an fatal error, set the output length to 0 */
	if (FATAL_RV(rv)) {
		digest->cd_length = 0;
	}

	/*
	 * If the op is terminated (successfully or with a fatal error),
	 * free the context.
	 */
	if (!RETRY_RV(rv)) {
		(void) n2cp_freereq(ctx->cc_provider_private);
		ctx->cc_provider_private = NULL;
	} else {
		/* release ulcwq buffer */
		check_draining(reqp);
	}

	return (rv);
}

static int
n2cp_digest_update(crypto_ctx_t *ctx, crypto_data_t *data,
    crypto_req_handle_t req)
{
	int		rv;
	n2cp_request_t	*reqp;

	if (!ctx || !ctx->cc_provider || !ctx->cc_provider_private)
		return (CRYPTO_OPERATION_NOT_INITIALIZED);

	reqp = (n2cp_request_t *)ctx->cc_provider_private;

	if (!(reqp->nr_cmd & N2CP_OP_DIGEST)) {
		return (CRYPTO_OPERATION_NOT_INITIALIZED);
	}

	if (n2cp_use_ulcwq &&
	    ((rv = n2cp_find_cep_for_req(reqp)) != CRYPTO_SUCCESS)) {
		return (rv);
	}

	rv = n2cp_hash_update(ctx, data, req);

	/*
	 * If the op terminated with a fatal error, free the context.
	 */
	if (FATAL_RV(rv)) {
		(void) n2cp_freereq(ctx->cc_provider_private);
		ctx->cc_provider_private = NULL;
	} else {
		/* release ulcwq buffer */
		check_draining(reqp);
	}

	return (rv);
}

static int
n2cp_digest_final(crypto_ctx_t *ctx, crypto_data_t *digest,
    crypto_req_handle_t req)
{
	int		rv;
	n2cp_request_t	*reqp;

	if (!ctx || !ctx->cc_provider || !ctx->cc_provider_private)
		return (CRYPTO_OPERATION_NOT_INITIALIZED);

	reqp = (n2cp_request_t *)ctx->cc_provider_private;

	if (!(reqp->nr_cmd & N2CP_OP_DIGEST)) {
		return (CRYPTO_OPERATION_NOT_INITIALIZED);
	}

	if (n2cp_use_ulcwq &&
	    ((rv = n2cp_find_cep_for_req(reqp)) != CRYPTO_SUCCESS)) {
		return (rv);
	}

	rv = n2cp_hash_final(ctx, digest, req);

	/* If it is an fatal error, set the output length to 0 */
	if (FATAL_RV(rv)) {
		digest->cd_length = 0;
	}

	/*
	 * If the op is terminated (successfully or with a fatal error),
	 * free the context.
	 */
	if (!RETRY_RV(rv)) {
		(void) n2cp_freereq(ctx->cc_provider_private);
		ctx->cc_provider_private = NULL;
	} else {
		/* release ulcwq buffer */
		check_draining(reqp);
	}

	return (rv);
}

/*ARGSUSED*/
static int
n2cp_digest_atomic(crypto_provider_handle_t provider,
    crypto_session_id_t session_id, crypto_mechanism_t *mechanism,
    crypto_data_t *data, crypto_data_t *digest, crypto_req_handle_t req)
{
	n2cp_t		*n2cp = (n2cp_t *)provider;
	int		rv;

	/*
	 * In-place operations (input == output) are indicated by having a
	 * NULL output. In this case set the output to point to the input.
	 */
	if (digest == NULL) {
		digest = data;
	}

	rv = n2cp_hashatomic(n2cp, mechanism, data, digest, req);

	/* If it is an fatal error, set the output length to 0 */
	if (FATAL_RV(rv)) {
		digest->cd_length = 0;
	}

	return (rv);
}

/*
 * Cipher (encrypt/decrypt) entry points.
 */

/* ARGSUSED */
static int
n2cp_encrypt_init(crypto_ctx_t *ctx, crypto_mechanism_t *mechanism,
    crypto_key_t *key, crypto_spi_ctx_template_t ctx_template,
    crypto_req_handle_t req)
{
	return (n2cp_blockinit(ctx, mechanism, key, 1 /* encrypt */));
}

/* ARGSUSED */
static int
n2cp_encrypt(crypto_ctx_t *ctx, crypto_data_t *plain,
    crypto_data_t *cipher, crypto_req_handle_t req)
{
	int		rv;
	n2cp_request_t	*reqp;

	if (!ctx || !ctx->cc_provider || !ctx->cc_provider_private)
		return (CRYPTO_OPERATION_NOT_INITIALIZED);

	reqp = (n2cp_request_t *)ctx->cc_provider_private;

	if (!(reqp->nr_cmd & N2CP_OP_ENCRYPT)) {
		return (CRYPTO_OPERATION_NOT_INITIALIZED);
	}

	/* If 'cipher' is NULL, it is an InPlace operation */
	if (cipher == NULL) {
		cipher = plain;
	}

	if (n2cp_use_ulcwq &&
	    ((rv = n2cp_find_cep_for_req(reqp)) != CRYPTO_SUCCESS)) {
		return (rv);
	}

	/* schedule the job */
	rv = n2cp_block(ctx, plain, cipher, req);

	/* If it is an fatal error, set the output length to 0 */
	if (FATAL_RV(rv)) {
		cipher->cd_length = 0;
	}

	/*
	 * If the op is terminated (successfully or with a fatal error),
	 * free the context.
	 */
	if (!RETRY_RV(rv)) {
		(void) n2cp_freereq(ctx->cc_provider_private);
		ctx->cc_provider_private = NULL;
	} else {
		/* release ulcwq buffer */
		check_draining(reqp);
	}

	return (rv);
}

static int
n2cp_encrypt_update(crypto_ctx_t *ctx, crypto_data_t *plain,
    crypto_data_t *cipher, crypto_req_handle_t cfreq)
{
	int		rv;
	n2cp_request_t	*reqp;
	if (!ctx || !ctx->cc_provider || !ctx->cc_provider_private)
		return (CRYPTO_OPERATION_NOT_INITIALIZED);

	reqp = (n2cp_request_t *)ctx->cc_provider_private;

	if (!(reqp->nr_cmd & N2CP_OP_ENCRYPT)) {
		return (CRYPTO_OPERATION_NOT_INITIALIZED);
	}

	/* If 'cipher' is NULL, it is an InPlace operation */
	if (cipher == NULL) {
		cipher = plain;
	}

	if (n2cp_use_ulcwq &&
	    ((rv = n2cp_find_cep_for_req(reqp)) != CRYPTO_SUCCESS)) {
		return (rv);
	}

	/* schedule the job */
	rv = n2cp_blockupdate(ctx, plain, cipher, cfreq);

	/*
	 * If the op is terminated with a fatal error, set the output length
	 * to zero and free the context.
	 */
	if (FATAL_RV(rv)) {
		cipher->cd_length = 0;
		(void) n2cp_freereq(ctx->cc_provider_private);
		ctx->cc_provider_private = NULL;
	} else {
		/* release ulcwq buffer */
		check_draining(reqp);
	}

	return (rv);
}

static int
n2cp_encrypt_final(crypto_ctx_t *ctx, crypto_data_t *cipher,
    crypto_req_handle_t cfreq)
{
	int		rv;
	n2cp_request_t	*reqp;

	if (!ctx || !ctx->cc_provider || !ctx->cc_provider_private)
		return (CRYPTO_OPERATION_NOT_INITIALIZED);

	reqp = (n2cp_request_t *)ctx->cc_provider_private;

	if (!(reqp->nr_cmd & N2CP_OP_ENCRYPT)) {
		return (CRYPTO_OPERATION_NOT_INITIALIZED);
	}

	if (n2cp_use_ulcwq &&
	    ((rv = n2cp_find_cep_for_req(reqp)) != CRYPTO_SUCCESS)) {
		return (rv);
	}

	/* schedule the job */
	rv = n2cp_blockfinal(ctx, cipher, cfreq);

	/* If it is an fatal error, set the output length to 0 */
	if (FATAL_RV(rv)) {
		cipher->cd_length = 0;
	}

	/*
	 * If the op is terminated (successfully or with a fatal error),
	 * free the context.
	 */
	if (!RETRY_RV(rv)) {
		(void) n2cp_freereq(ctx->cc_provider_private);
		ctx->cc_provider_private = NULL;
	} else {
		/* release ulcwq buffer */
		check_draining(reqp);
	}

	return (rv);
}



/* ARGSUSED */
static int
n2cp_encrypt_atomic(crypto_provider_handle_t provider,
    crypto_session_id_t session_id, crypto_mechanism_t *mechanism,
    crypto_key_t *key, crypto_data_t *plain, crypto_data_t *cipher,
    crypto_spi_ctx_template_t ctx_template, crypto_req_handle_t req)
{
	int		rv = CRYPTO_FAILED;
	n2cp_t		*n2cp = (n2cp_t *)provider;

	/*
	 * In-place operations (input == output) are indicated by having a
	 * NULL output. In this case set the output to point to the input.
	 */
	if (cipher == NULL) {
		cipher = plain;
	}

	/* schedule the job */
	rv = n2cp_blockatomic(n2cp, mechanism, key, plain, cipher,
	    req, 1 /* encrypt */);

	/* If it is an fatal error, set the output length to 0 */
	if (FATAL_RV(rv)) {
		cipher->cd_length = 0;
	}

	return (rv);
}

/*
 * This routine does a combined encryption and MAC.
 *
 * A note on the 'dual_data' argument which is a crypto_dual_data_t -
 * The encrypted text output goes in the dd_data field of this structure.
 * The MAC is computed on the encrypted text starting at dd_offset2,
 * for a length of dd_len2.
 */
/* ARGSUSED */
static int
n2cp_encrypt_mac_atomic(crypto_provider_handle_t provider,
    crypto_session_id_t session_id, crypto_mechanism_t *encrypt_mech,
    crypto_key_t *encrypt_key, crypto_mechanism_t *mac_mech,
    crypto_key_t *mac_key, crypto_data_t *plain,
    crypto_dual_data_t *dual_data, crypto_data_t *mac,
    crypto_spi_ctx_template_t encr_ctx_template,
    crypto_spi_ctx_template_t mac_ctx_template,
    crypto_req_handle_t req)
{
	int		rv = CRYPTO_FAILED;
	n2cp_t		*n2cp = (n2cp_t *)provider;
	crypto_data_t	*cipher_data;
	crypto_data_t	mac_data;

	cipher_data = &dual_data->dd_data;
	/*
	 * encrypt_mac routine has a different convention from the others
	 * for In-place operation. It is indicated by having a NULL plain text
	 * input and the input (which is also the output) is passed in
	 * the dual_data.
	 */
	if (plain == NULL) {
		plain = cipher_data;
	}

	/* schedule the encrypt job */
	rv = n2cp_blockatomic(n2cp, encrypt_mech, encrypt_key,
	    plain, cipher_data, req, 1 /* encrypt */);
	if (FATAL_RV(rv)) {
		cipher_data->cd_length = 0;
		return (rv);
	}

	mac_data = *cipher_data;
	mac_data.cd_offset = dual_data->dd_offset2;
	mac_data.cd_length = dual_data->dd_len2;

	/* schedule the MAC job */
	rv = n2cp_hmac_signatomic(n2cp, mac_mech, mac_key,
	    &mac_data, mac, req);
	if (FATAL_RV(rv)) {
		mac->cd_length = 0;
	}

	return (rv);
}


/* ARGSUSED */
static int
n2cp_decrypt_init(crypto_ctx_t *ctx, crypto_mechanism_t *mechanism,
    crypto_key_t *key, crypto_spi_ctx_template_t ctx_template,
    crypto_req_handle_t req)
{
	/* schedule the job */
	return (n2cp_blockinit(ctx, mechanism, key, 0 /* decrypt */));
}

/* ARGSUSED */
static int
n2cp_decrypt(crypto_ctx_t *ctx, crypto_data_t *cipher,
    crypto_data_t *plain, crypto_req_handle_t req)
{
	int		rv = CRYPTO_FAILED;
	n2cp_request_t	*reqp;

	if (!ctx || !ctx->cc_provider || !ctx->cc_provider_private)
		return (CRYPTO_OPERATION_NOT_INITIALIZED);

	reqp = (n2cp_request_t *)ctx->cc_provider_private;

	if (!(reqp->nr_cmd & N2CP_OP_DECRYPT)) {
		return (CRYPTO_OPERATION_NOT_INITIALIZED);
	}

	/*
	 * In-place operations (input == output) are indicated by having a
	 * NULL output. In this case set the output to point to the input.
	 */
	if (plain == NULL) {
		plain = cipher;
	}

	if (n2cp_use_ulcwq &&
	    ((rv = n2cp_find_cep_for_req(reqp)) != CRYPTO_SUCCESS)) {
		return (rv);
	}

	/* schedule the job */
	rv = n2cp_block(ctx, cipher, plain, req);

	/* If it is an fatal error, set the output length to 0 */
	if (FATAL_RV(rv)) {
		plain->cd_length = 0;
	}
	/*
	 * If the op is terminated (successfully or with a fatal error),
	 * free the context.
	 */
	if (!RETRY_RV(rv)) {
		(void) n2cp_freereq(ctx->cc_provider_private);
		ctx->cc_provider_private = NULL;
	} else {
		/* release ulcwq buffer */
		check_draining(reqp);
	}

	return (rv);
}


static int
n2cp_decrypt_update(crypto_ctx_t *ctx, crypto_data_t *cipher,
    crypto_data_t *plain, crypto_req_handle_t cfreq)
{
	int		rv;
	n2cp_request_t	*reqp;

	if (!ctx || !ctx->cc_provider || !ctx->cc_provider_private)
		return (CRYPTO_OPERATION_NOT_INITIALIZED);

	reqp = (n2cp_request_t *)ctx->cc_provider_private;

	if (!(reqp->nr_cmd & N2CP_OP_DECRYPT)) {
		return (CRYPTO_OPERATION_NOT_INITIALIZED);
	}

	/* If 'plain' is NULL, it is an InPlace operation */
	if (plain == NULL) {
		plain = cipher;
	}

	if (n2cp_use_ulcwq &&
	    ((rv = n2cp_find_cep_for_req(reqp)) != CRYPTO_SUCCESS)) {
		return (rv);
	}

	/* schedule the job */
	rv = n2cp_blockupdate(ctx, cipher, plain, cfreq);

	/*
	 * If the op is terminated with a fatal error, set the output length
	 * to zero and free the context.
	 */
	if (FATAL_RV(rv)) {
		plain->cd_length = 0;
		(void) n2cp_freereq(ctx->cc_provider_private);
		ctx->cc_provider_private = NULL;
	} else {
		/* release ulcwq buffer */
		check_draining(reqp);
	}

	return (rv);
}


static int
n2cp_decrypt_final(crypto_ctx_t *ctx, crypto_data_t *plain,
    crypto_req_handle_t cfreq)
{
	int		rv;
	n2cp_request_t	*reqp;

	if (!ctx || !ctx->cc_provider || !ctx->cc_provider_private)
		return (CRYPTO_OPERATION_NOT_INITIALIZED);

	reqp = (n2cp_request_t *)ctx->cc_provider_private;

	if (!(reqp->nr_cmd & N2CP_OP_DECRYPT)) {
		return (CRYPTO_OPERATION_NOT_INITIALIZED);
	}

	if (n2cp_use_ulcwq &&
	    ((rv = n2cp_find_cep_for_req(reqp)) != CRYPTO_SUCCESS)) {
		return (rv);
	}

	/* schedule the job */
	rv = n2cp_blockfinal(ctx, plain, cfreq);

	/* If it is an fatal error, set the output length to 0 */
	if (FATAL_RV(rv)) {
		plain->cd_length = 0;
	}

	/*
	 * If the op is terminated (successfully or with a fatal error),
	 * free the context.
	 */
	if (!RETRY_RV(rv)) {
		(void) n2cp_freereq(ctx->cc_provider_private);
		ctx->cc_provider_private = NULL;
	} else {
		/* release ulcwq buffer */
		check_draining(reqp);
	}

	return (rv);
}

/* ARGSUSED */
static int
n2cp_decrypt_atomic(crypto_provider_handle_t provider,
    crypto_session_id_t session_id, crypto_mechanism_t *mechanism,
    crypto_key_t *key, crypto_data_t *cipher, crypto_data_t *plain,
    crypto_spi_ctx_template_t ctx_template, crypto_req_handle_t req)
{
	int	rv = CRYPTO_FAILED;
	n2cp_t	*n2cp = (n2cp_t *)provider;

	/*
	 * In-place operations (input == output) are indicated by having a
	 * NULL output. In this case set the output to point to the input.
	 */
	if (plain == NULL) {
		plain = cipher;
	}

	/* schedule the job */
	rv = n2cp_blockatomic(n2cp, mechanism, key, cipher, plain,
	    req, 0 /* decrypt */);

	/* If it is an fatal error, set the output length to 0 */
	if (FATAL_RV(rv)) {
		plain->cd_length = 0;
	}

	return (rv);
}

/*
 * This routine does a combined MAC verify and decryption.
 *
 * A note on the 'dual_data' argument which is a crypto_dual_data_t -
 * The MAC is computed on the dd_data field starting at dd_offset1,
 * for a length of dd_len1. The computed MAC is compared against the
 * expected MAC which is passed in 'mac' argument.
 * The decryption is done on the dd_data field starting at dd_offset2,
 * for a length of dd_len2.
 */
/*ARGSUSED*/
static int
n2cp_mac_verify_decrypt_atomic(crypto_provider_handle_t provider,
    crypto_session_id_t session_id, crypto_mechanism_t *mac_mech,
    crypto_key_t *mac_key, crypto_mechanism_t *decrypt_mech,
    crypto_key_t *decrypt_key, crypto_dual_data_t *dual_data,
    crypto_data_t *mac, crypto_data_t *plaintext,
    crypto_spi_ctx_template_t mac_ctx_template,
    crypto_spi_ctx_template_t decr_ctx_template,
    crypto_req_handle_t req)
{
	int	rv = CRYPTO_FAILED;
	n2cp_t	*n2cp = (n2cp_t *)provider;
	crypto_data_t cipher_data;
	crypto_data_t *mac_data;

	mac_data = &dual_data->dd_data;

	/* schedule the MAC verify job */
	rv = n2cp_hmac_verifyatomic(n2cp, mac_mech, mac_key,
	    mac_data, mac, req);
	if (FATAL_RV(rv)) {
		mac->cd_length = 0;
		return (rv);
	}

	cipher_data = *mac_data;
	cipher_data.cd_length = dual_data->dd_len2;
	cipher_data.cd_offset = dual_data->dd_offset2;

	/*
	 * mac_verify_decrypt routine has a different convention from the others
	 * for In-place operation. It is indicated by having a NULL plain text
	 * input and the input (which is also the output) is passed in
	 * the dual_data.
	 */
	if (plaintext == NULL) {
		plaintext = &cipher_data;
	}

	/* schedule the decrypt job */
	rv = n2cp_blockatomic(n2cp, decrypt_mech, decrypt_key,
	    &cipher_data, plaintext, req, 0 /* decrypt */);
	if (FATAL_RV(rv)) {
		plaintext->cd_length = 0;
	}

	return (rv);
}

/*
 * Sign/Verify entry points
 */

/*ARGSUSED*/
static int
n2cp_sign_init(crypto_ctx_t *ctx, crypto_mechanism_t *mechanism,
    crypto_key_t *key, crypto_spi_ctx_template_t ctx_template,
    crypto_req_handle_t cfreq)
{

	return (n2cp_hmacinit(ctx, mechanism, key));
}

static int
n2cp_sign(crypto_ctx_t *ctx, crypto_data_t *data,
    crypto_data_t *signature, crypto_req_handle_t cfreq)
{
	int		rv;
	n2cp_request_t	*reqp;

	if (!ctx || !ctx->cc_provider || !ctx->cc_provider_private)
		return (CRYPTO_OPERATION_NOT_INITIALIZED);

	/*
	 * In-place operations (input == output) are indicated by having a
	 * NULL output. In this case set the output to point to the input.
	 */
	if (signature == NULL) {
		signature = data;
	}

	reqp = (n2cp_request_t *)ctx->cc_provider_private;
	if (n2cp_use_ulcwq &&
	    ((rv = n2cp_find_cep_for_req(reqp)) != CRYPTO_SUCCESS)) {
		return (rv);
	}

	rv = n2cp_hmac_sign(ctx, data, signature, cfreq);

	/* If it is an fatal error, set the output length to 0 */
	if (FATAL_RV(rv)) {
		signature->cd_length = 0;
	}

	/*
	 * If the op is terminated (successfully or with a fatal error),
	 * free the context.
	 */
	if (!RETRY_RV(rv)) {
		(void) n2cp_freereq(ctx->cc_provider_private);
		ctx->cc_provider_private = NULL;
	} else {
		/* release ulcwq buffer */
		check_draining(reqp);
	}

	return (rv);
}

/*ARGSUSED*/
static int
n2cp_sign_atomic(crypto_provider_handle_t provider,
    crypto_session_id_t session_id, crypto_mechanism_t *mechanism,
    crypto_key_t *key, crypto_data_t *data, crypto_data_t *signature,
    crypto_spi_ctx_template_t ctx_template, crypto_req_handle_t cfreq)
{
	int	rv;
	n2cp_t	*n2cp = (n2cp_t *)provider;

	/*
	 * In-place operations (input == output) are indicated by having a
	 * NULL output. In this case set the output to point to the input.
	 */
	if (signature == NULL) {
		signature = data;
	}

	if (mechanism->cm_type == SSL3_SHA1_MAC_MECH_INFO_TYPE) {
		rv = n2cp_ssl3_sha1_mac_signatomic(n2cp, mechanism,
		    key, data, signature, cfreq);
	} else {
		rv = n2cp_hmac_signatomic(n2cp, mechanism, key,
		    data, signature, cfreq);
	}

	if (FATAL_RV(rv)) {
		signature->cd_length = 0;
	}

	return (rv);
}

/*ARGSUSED*/
static int
n2cp_verify_init(crypto_ctx_t *ctx, crypto_mechanism_t *mechanism,
    crypto_key_t *key, crypto_spi_ctx_template_t ctx_template,
    crypto_req_handle_t cfreq)
{
	return (n2cp_hmacinit(ctx, mechanism, key));
}

static int
n2cp_verify(crypto_ctx_t *ctx, crypto_data_t *data,
    crypto_data_t *signature, crypto_req_handle_t cfreq)
{
	int		rv;
	n2cp_request_t	*reqp;

	if (!ctx || !ctx->cc_provider || !ctx->cc_provider_private)
		return (CRYPTO_OPERATION_NOT_INITIALIZED);

	if (signature == NULL) {
		return (CRYPTO_ARGUMENTS_BAD);
	}

	reqp = (n2cp_request_t *)ctx->cc_provider_private;
	if (n2cp_use_ulcwq &&
	    ((rv = n2cp_find_cep_for_req(reqp)) != CRYPTO_SUCCESS)) {
		return (rv);
	}

	rv = n2cp_hmac_verify(ctx, data, signature, cfreq);

	/*
	 * If the op is terminated (successfully or with a fatal error),
	 * free the context.
	 */
	if (!RETRY_RV(rv)) {
		(void) n2cp_freereq(ctx->cc_provider_private);
		ctx->cc_provider_private = NULL;
	} else {
		/* release ulcwq buffer */
		check_draining(reqp);
	}

	return (rv);
}

/*ARGSUSED*/
static int
n2cp_verify_atomic(crypto_provider_handle_t provider,
    crypto_session_id_t session_id, crypto_mechanism_t *mechanism,
    crypto_key_t *key, crypto_data_t *data, crypto_data_t *signature,
    crypto_spi_ctx_template_t ctx_template, crypto_req_handle_t cfreq)
{
	int		rv;
	n2cp_t		*n2cp = (n2cp_t *)provider;

	if (signature == NULL) {
		return (CRYPTO_ARGUMENTS_BAD);
	}

	if (mechanism->cm_type == SSL3_SHA1_MAC_MECH_INFO_TYPE) {
		rv = n2cp_ssl3_sha1_mac_verifyatomic(n2cp, mechanism,
		    key, data, signature, cfreq);
	} else {
		rv = n2cp_hmac_verifyatomic(n2cp, mechanism, key,
		    data, signature, cfreq);
	}

	return (rv);
}

/*
 * Context management entry points.
 */

static int
n2cp_free_context(crypto_ctx_t *ctx)
{
	n2cp_request_t	*reqp = (n2cp_request_t *)ctx->cc_provider_private;

	if (reqp == NULL)
		return (CRYPTO_SUCCESS);

	n2cp_freereq(reqp);

	return (CRYPTO_SUCCESS);
}

/*
 * create_cwb_chain()
 *
 * create chain of control words (hardware job requests)
 */
int
create_cwb_chain(cwq_cw_t *cwbs, int cwbcnt,
    char *addr, int len, int *chaincnt, int blocksz)
{
	cwq_cw_t	*cw;
	int		cnt = 0;
	int		offset = 0;
	int		max_space_in_page;

	/* sanity check: if there are no CWBs left, return immediately */
	if (cwbcnt < 1) {
		if (len > 0) {
			return (CRYPTO_DATA_LEN_RANGE);
		} else {
			*chaincnt = 0;
			return (CRYPTO_SUCCESS);
		}
	}

	/* add the first buffer to the chain */
	cw = &(cwbs[0]);
	cw->cw_src_addr = va_to_pa(addr);
	cw->cw_eob = 0;
	max_space_in_page = n2cp_pagesize -
	    ((uintptr_t)addr % n2cp_pagesize);
	cw->cw_length = min(max_space_in_page, len) - 1;

	/* cw must contain at least 1 block of data */
	if ((cw->cw_length + 1) < blocksz) {
		return (CRYPTO_DATA_LEN_RANGE);
	}

	len -= (cw->cw_length + 1);
	offset += (cw->cw_length + 1);
	cnt++;

	/* add the rest of the buffer to the chain */
	while (len > 0) {
		if (cnt >= cwbcnt) {
			return (CRYPTO_DATA_LEN_RANGE);
		}
		cw = &(cwbs[cnt]);
		cw->cw_src_addr = va_to_pa(addr + offset);
		cw->cw_length = min(n2cp_pagesize, len) - 1;
		/* cw must contain at least 1 block of data */
		if ((cw->cw_length + 1) < blocksz) {
			return (CRYPTO_DATA_LEN_RANGE);
		}
		cw->cw_eob = 0;
		len -= (cw->cw_length + 1);
		offset += (cw->cw_length + 1);
		cnt++;
	}

	*chaincnt = cnt;

	return (CRYPTO_SUCCESS);
}


/*
 * Construct the chain of control word blocks (hardware job requests)
 * Note that each block is at most 'pagesize' so that we know that each
 * block is physically contiguous.
 */
int
n2cp_construct_chain(cwq_cw_t *cwbs, crypto_data_t *data, int datalen,
    int *chaincnt)
{
	int		cnt = 0;
	char		*addr;
	int		resid_cwbcnt, total_cwbcnt;
	uint_t		vec_idx;
	off_t		off = data->cd_offset;
	size_t		cur_len;
	uio_t		*uiop;
	mblk_t		*mp;
	int		rv;
	offset_t	orig_offset = data->cd_offset;
	size_t		orig_len = data->cd_length;
	int		blocksz = 1;	/* block size */
	cwq_cw_t	orig_cwb0;

	/* save the cwbs in case of failure */
	orig_cwb0 = cwbs[0];
	/* calculate required block size -  only needed for encryption */
	if ((orig_cwb0.cw_op & (~CW_OP_INLINE_BIT)) == CW_OP_ENCRYPT) {
		switch (orig_cwb0.cw_enc_type >> 2) {
			case CW_ENC_ALGO_DES:
			case CW_ENC_ALGO_3DES:
				blocksz = 8;
				break;
			case CW_ENC_ALGO_AES128:
			case CW_ENC_ALGO_AES192:
			case CW_ENC_ALGO_AES256:
				blocksz = 16;
				break;
			default:
				blocksz = 1;
		}
	}

	DBG1(NULL, DCHATTY, "block size %d", blocksz);

	switch (data->cd_format) {
	case CRYPTO_DATA_RAW:
		addr = n2cp_get_dataaddr(data, 0);
		rv = create_cwb_chain(cwbs, N2CP_MAX_CHAIN,
		    addr, datalen, &total_cwbcnt, blocksz);
		if (rv != CRYPTO_SUCCESS) {
			goto errorexit;
		}
		data->cd_offset += datalen;
		data->cd_length -= datalen;
		break;

	case CRYPTO_DATA_UIO:
		uiop = data->cd_uio;

		/*
		 * Jump to the first iovec containing data to be processed.
		 */
		for (vec_idx = 0; vec_idx < uiop->uio_iovcnt &&
		    off >= uiop->uio_iov[vec_idx].iov_len;
		    off -= uiop->uio_iov[vec_idx++].iov_len)
			;
		if (vec_idx == uiop->uio_iovcnt) {
			/*
			 * The caller specified an offset that is larger than
			 * the total size of the buffers it provided.
			 */
			return (CRYPTO_DATA_LEN_RANGE);
		}

		resid_cwbcnt = N2CP_MAX_CHAIN;
		total_cwbcnt = 0;

		/*
		 * Now process the iovecs.
		 */
		while ((vec_idx < uiop->uio_iovcnt) && (datalen > 0)) {
			cur_len = min(uiop->uio_iov[vec_idx].iov_len -
			    off, datalen);
			datalen -= cur_len;
			/* each cw must contain at least one block */
			if (cur_len < blocksz) {
				rv = CRYPTO_DATA_LEN_RANGE;
				goto errorexit;
			}
			addr = (char *)uiop->uio_iov[vec_idx].iov_base + off;
			rv = create_cwb_chain(&cwbs[total_cwbcnt],
			    resid_cwbcnt, addr, cur_len, &cnt, blocksz);
			if (rv != CRYPTO_SUCCESS) {
				goto errorexit;
			}

			data->cd_offset += cur_len;
			data->cd_length -= cur_len;
			resid_cwbcnt -= cnt;
			total_cwbcnt += cnt;
			vec_idx++;
			off = 0;
		}

		if ((vec_idx == uiop->uio_iovcnt) && (datalen > 0)) {
			/*
			 * The end of the specified iovec's was reached but
			 * the length requested could not be processed
			 * (requested to digest more data than it provided).
			 */
			rv = CRYPTO_DATA_LEN_RANGE;
			goto errorexit;
		}
		break;
	case CRYPTO_DATA_MBLK:
		/*
		 * Jump to the first mblk_t containing data to be processed.
		 */
		for (mp = data->cd_mp; mp != NULL && off >= N_MBLKL(mp);
		    off -= N_MBLKL(mp), mp = mp->b_cont)
			;
		if (mp == NULL) {
			/*
			 * The caller specified an offset that is larger than
			 * the total size of the buffers it provided.
			 */
			return (CRYPTO_DATA_LEN_RANGE);
		}

		resid_cwbcnt = N2CP_MAX_CHAIN;
		total_cwbcnt = 0;

		/*
		 * Now do the processing on the mblk chain.
		 */
		while ((mp != NULL) && (datalen > 0)) {
			cur_len = min(N_MBLKL(mp) - off, datalen);
			/*
			 * Each control word must contain at least one block.
			 * Ignore mblk_t's which contain no data.
			 */
			if (cur_len == 0) {
				mp = mp->b_cont;
				continue;
			}
			if (cur_len < blocksz) {
				rv = CRYPTO_DATA_LEN_RANGE;
				goto errorexit;
			}

			datalen -= cur_len;
			addr = (char *)mp->b_rptr + off;
			rv = create_cwb_chain(&cwbs[total_cwbcnt],
			    resid_cwbcnt, addr, cur_len, &cnt, blocksz);
			if (rv != CRYPTO_SUCCESS) {
				goto errorexit;
			}
			data->cd_offset += cur_len;
			data->cd_length -= cur_len;
			resid_cwbcnt -= cnt;
			total_cwbcnt += cnt;
			mp = mp->b_cont;
			off = 0;
		}

		if ((mp == NULL) && (datalen > 0)) {
			/*
			 * The end of the mblk was reached but the length
			 * requested could not be processed, (requested to
			 * digest more data than it provided).
			 */
			rv = CRYPTO_DATA_LEN_RANGE;
			goto errorexit;
		}
		break;
	default:
		return (CRYPTO_ARGUMENTS_BAD);
	}

	/* set the start and end flag */
	cwbs[0].cw_sob = 1;
	cwbs[0].cw_eob = 0;
	cwbs[total_cwbcnt - 1].cw_eob = 1;
	*chaincnt = total_cwbcnt;

	return (CRYPTO_SUCCESS);

errorexit:
	/* undo the changes */
	bzero(cwbs + 1, (N2CP_MAX_CHAIN - 1) * sizeof (cwq_cw_t));
	cwbs[0] = orig_cwb0;
	data->cd_offset = orig_offset;
	data->cd_length = orig_len;
	return (rv);
}

/* ARGSUSED */
static int
ext_info(crypto_provider_handle_t prov,
    crypto_provider_ext_info_t *ext_info, crypto_req_handle_t cfreq)
{
	n2cp_t	*n2cp = (n2cp_t *)prov;
	int	len;
	char	*id = IDENT_BULK;

	/* Label */
	(void) sprintf((char *)ext_info->ei_label, "%s/%d %s",
	    ddi_driver_name(n2cp->n_dip),
	    ddi_get_instance(n2cp->n_dip), id);
	len = strlen((char *)ext_info->ei_label);
	(void) memset(ext_info->ei_label + len, ' ',
	    CRYPTO_EXT_SIZE_LABEL - len);

	/* Manufacturer ID */
	(void) sprintf((char *)ext_info->ei_manufacturerID, "%s",
	    N2CP_MANUFACTURER_ID);
	len = strlen((char *)ext_info->ei_manufacturerID);
	(void) memset(ext_info->ei_manufacturerID + len, ' ',
	    CRYPTO_EXT_SIZE_MANUF - len);

	/* Model */
	(void) sprintf((char *)ext_info->ei_model, "N2CP1");

	DBG1(n2cp, DATTACH, "kCF MODEL: %s", (char *)ext_info->ei_model);

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
	ext_info->ei_firmware_version.cv_major = n2cp->n_hvapi_major_version;
	ext_info->ei_firmware_version.cv_minor = n2cp->n_hvapi_minor_version;

	/* Time. No need to be supplied for token without a clock */
	ext_info->ei_time[0] = '\000';

	if (!is_KT(n2cp)) {
		ASSERT(n2cp_prov_info.pi_flags & CRYPTO_HASH_NO_UPDATE);
		ext_info->ei_hash_max_input_len = MAX_DATA_LEN;
	}
	ext_info->ei_hmac_max_input_len = MAX_DATA_LEN;

	return (CRYPTO_SUCCESS);
}

/*ARGSUSED*/
static int
n2cp_allocate_mechanism(crypto_provider_handle_t provider,
    crypto_mechanism_t *in_mech, crypto_mechanism_t *out_mech,
    int *error, int mode)
{
	switch (out_mech->cm_type) {
	case AES_CTR_MECH_INFO_TYPE:
		return (n2cp_aes_ctr_allocmech(in_mech, out_mech, error, mode));
	case AES_CCM_MECH_INFO_TYPE:
		return (n2cp_aes_ccm_allocmech(in_mech, out_mech, error, mode));
	case AES_GCM_MECH_INFO_TYPE:
		return (n2cp_aes_gcm_allocmech(in_mech, out_mech, error, mode));
	case AES_GMAC_MECH_INFO_TYPE:
		return (n2cp_aes_gmac_allocmech(in_mech, out_mech, error,
		    mode));
	default:
		/* crypto module does alloc/copyin of flat params */
		return (CRYPTO_NOT_SUPPORTED);
	}
}

/*ARGSUSED*/
static int
n2cp_free_mechanism(crypto_provider_handle_t provider,
    crypto_mechanism_t *mech)
{
	switch (mech->cm_type) {
	case AES_CTR_MECH_INFO_TYPE:
		return (n2cp_aes_ctr_freemech(mech));
	case AES_CCM_MECH_INFO_TYPE:
		return (n2cp_aes_ccm_freemech(mech));
	case AES_GCM_MECH_INFO_TYPE:
		return (n2cp_aes_gcm_freemech(mech));
	case AES_GMAC_MECH_INFO_TYPE:
		return (n2cp_aes_gmac_freemech(mech));
	default:
		return (CRYPTO_NOT_SUPPORTED);
	}
}
