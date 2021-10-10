/*
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
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
#include <sys/machsystm.h>
#include <sys/crypto/common.h>
#include <sys/crypto/spi.h>
#include <sys/ncs.h>
#include <sys/n2cp.h>
#include <sys/hypervisor_api.h>
#include <sys/sha1.h>
#include <modes/modes.h>

static void hmac_done(n2cp_request_t *);
static int hmac_start(n2cp_request_t *);
static int process_gmac_mech(crypto_mechanism_t *, crypto_data_t *,
    CK_AES_GCM_PARAMS *);

static crypto_data_t null_crypto_data = { CRYPTO_DATA_RAW };

#define	IS_GENERAL(mech) \
	(((mech) == MD5_HMAC_GENERAL_MECH_INFO_TYPE) || \
	((mech) == SHA1_HMAC_GENERAL_MECH_INFO_TYPE) || \
	((mech) == SHA256_HMAC_GENERAL_MECH_INFO_TYPE))

#ifdef	SSL3_SHA256_MAC_SUPPORT
#define	IS_SSL(mech) \
	(((mech) == SSL3_MD5_MAC_MECH_INFO_TYPE) || \
	((mech) == SSL3_SHA1_MAC_MECH_INFO_TYPE) || \
	((mech) == SSL3_SHA256_MAC_MECH_INFO_TYPE))
#else
#define	IS_SSL(mech) \
	(((mech) == SSL3_MD5_MAC_MECH_INFO_TYPE) || \
	((mech) == SSL3_SHA1_MAC_MECH_INFO_TYPE))
#endif

/*
 * The following are the fixed IVs that hardware
 * is expecting for the respective hmac algorithms.
 * Note: assume big endianess
 */
extern fixed_iv_t	iv_md5, iv_sha1, iv_sha256;

static int
alloc_hmac_ctx(n2cp_t *n2cp, crypto_mechanism_t *mechanism, crypto_key_t *key,
    n2cp_request_t **argreq)
{
	n2cp_request_t	*reqp;
	n2cp_hmac_ctx_t	*hmacctx;
	int		keylen;
	uint32_t	*hmaciv;

/* EXPORT DELETE START */

	*argreq = NULL;

	if ((reqp = n2cp_getreq(n2cp)) == NULL) {
		DBG0(NULL, DWARN,
		    "alloc_des_request: unable to allocate request for hmac");
		return (CRYPTO_HOST_MEMORY);
	}

	/* store the key value in ctx */
	if (key->ck_format != CRYPTO_KEY_RAW) {
		/* the key must be passed by value */
		n2cp_freereq(reqp);
		return (CRYPTO_KEY_TYPE_INCONSISTENT);
	}
	keylen = CRYPTO_BITS2BYTES(key->ck_length);
	if (keylen > CW_MAX_KEY_LEN) {
		cmn_err(CE_WARN, "alloc_hmac_ctx: keylen(%d) > maxlen(%d)\n",
		    keylen, CW_MAX_KEY_LEN);
		n2cp_freereq(reqp);
		return (CRYPTO_KEY_SIZE_RANGE);
	}

	hmacctx = &(reqp->nr_context->hmacctx);
	BCOPY(key->ck_data, hmacctx->keyvalue, keylen);
	hmacctx->keylen = keylen;

	switch (mechanism->cm_type) {
	case MD5_HMAC_MECH_INFO_TYPE:
	case MD5_HMAC_GENERAL_MECH_INFO_TYPE:
	case SSL3_MD5_MAC_MECH_INFO_TYPE:
		hmacctx->hashsz = iv_md5.ivsize;
		hmaciv = iv_md5.iv;
		break;
	case SHA1_HMAC_MECH_INFO_TYPE:
	case SHA1_HMAC_GENERAL_MECH_INFO_TYPE:
	case SSL3_SHA1_MAC_MECH_INFO_TYPE:
		hmacctx->hashsz = iv_sha1.ivsize;
		hmaciv = iv_sha1.iv;
		break;
	case SHA256_HMAC_MECH_INFO_TYPE:
	case SHA256_HMAC_GENERAL_MECH_INFO_TYPE:
#ifdef	SSL3_SHA256_MAC_SUPPORT
	case SSL3_SHA256_MAC_MECH_INFO_TYPE:
#endif
		hmacctx->hashsz = iv_sha256.ivsize;
		hmaciv = iv_sha256.iv;
		break;
	default:
		n2cp_freereq(reqp);
		return (CRYPTO_MECHANISM_INVALID);
	}

	BCOPY(hmaciv, hmacctx->iv, hmacctx->hashsz);

	if (IS_GENERAL(mechanism->cm_type & N2CP_CMD_MASK) ||
	    IS_SSL(mechanism->cm_type & N2CP_CMD_MASK)) {
		if (mechanism->cm_param_len == sizeof (uint32_t)) {
			hmacctx->signlen =
			    *(uint32_t *)(void *)mechanism->cm_param;
		} else if (mechanism->cm_param_len == sizeof (uint64_t)) {
			hmacctx->signlen =
			    *(uint64_t *)(void *)mechanism->cm_param;
		} else {
			n2cp_freereq(reqp);
			return (CRYPTO_MECHANISM_PARAM_INVALID);
		}
		if ((hmacctx->signlen == 0) ||
		    (hmacctx->signlen > hmacctx->hashsz)) {
			n2cp_freereq(reqp);
			return (CRYPTO_MECHANISM_PARAM_INVALID);
		}
	} else {
		hmacctx->signlen = hmacctx->hashsz;
	}

	reqp->nr_cmd = mechanism->cm_type;
	reqp->nr_n2cp = n2cp;
	*argreq = reqp;

/* EXPORT DELETE END */

	return (CRYPTO_SUCCESS);
}

void
n2cp_clean_hmacctx(n2cp_request_t *reqp)
{
	/* clear the key value field - direct write faster than bzero */
	reqp->nr_context->hmacctx.keystruct.val64[0] = 0;
	reqp->nr_context->hmacctx.keystruct.val64[1] = 0;
	reqp->nr_context->hmacctx.keystruct.val64[2] = 0;
	reqp->nr_context->hmacctx.keystruct.val64[3] = 0;
}

int
n2cp_hmacinit(crypto_ctx_t *ctx, crypto_mechanism_t *mechanism,
    crypto_key_t *key)
{
	int		rv;
	n2cp_t		*n2cp = (n2cp_t *)ctx->cc_provider;
	n2cp_request_t	*reqp;

/* EXPORT DELETE START */

	rv = alloc_hmac_ctx(n2cp, mechanism, key, &reqp);
	if (rv != CRYPTO_SUCCESS) {
		return (rv);
	}

	ctx->cc_provider_private = reqp;
	/* release ulcwq buffer */
	check_draining(reqp);

/* EXPORT DELETE END */

	return (CRYPTO_SUCCESS);
}

int
n2cp_hmac_sign(crypto_ctx_t *ctx, crypto_data_t *in, crypto_data_t *out,
    crypto_req_handle_t kcfreq)
{
	n2cp_request_t	*reqp = (n2cp_request_t *)(ctx->cc_provider_private);
	n2cp_hmac_ctx_t	*hmacctx = &(reqp->nr_context->hmacctx);

/* EXPORT DELETE START */

	/*
	 * the chip does not support multi-part, and therefore, we can
	 * support upto MAX_DATA_LEN input
	 */
	if (in->cd_length > MAX_DATA_LEN) {
		return (CRYPTO_DATA_LEN_RANGE);
	}
	if (out->cd_length < hmacctx->signlen) {
		out->cd_length = hmacctx->signlen;
		return (CRYPTO_BUFFER_TOO_SMALL);
	}

	N2CP_REQ_SETUP(reqp, in, out);

	reqp->nr_callback = hmac_done;
	reqp->nr_kcfreq = kcfreq;
	reqp->nr_cmd |= N2CP_OP_SINGLE;

/* EXPORT DELETE END */

	return (hmac_start(reqp));
}

int
n2cp_hmac_verify(crypto_ctx_t *ctx, crypto_data_t *in, crypto_data_t *out,
    crypto_req_handle_t kcfreq)
{
	n2cp_request_t	*reqp = (n2cp_request_t *)(ctx->cc_provider_private);
	n2cp_hmac_ctx_t	*hmacctx = &(reqp->nr_context->hmacctx);

/* EXPORT DELETE START */

	/* For signature verification, input must be different from output */
	if (in == out) {
		return (CRYPTO_ARGUMENTS_BAD);
	}

	/*
	 * the chip does not support multi-part, and therefore, we can
	 * support upto MAX_DATA_LEN input
	 */
	if (in->cd_length > MAX_DATA_LEN) {
		return (CRYPTO_DATA_LEN_RANGE);
	}
	if (out->cd_length != hmacctx->signlen) {
		out->cd_length = hmacctx->signlen;
		return (CRYPTO_SIGNATURE_LEN_RANGE);
	}

	reqp->nr_in = in;
	reqp->nr_out = out;

	reqp->nr_callback = hmac_done;
	reqp->nr_kcfreq = kcfreq;
	reqp->nr_cmd |= (N2CP_OP_SINGLE | N2CP_OP_VERIFY);

/* EXPORT DELETE END */

	return (hmac_start(reqp));
}

int
n2cp_hmac_signatomic(n2cp_t *n2cp, crypto_mechanism_t *mechanism,
    crypto_key_t *key, crypto_data_t *in, crypto_data_t *out,
    crypto_req_handle_t kcfreq)
{
	int			rv;
	n2cp_request_t		*reqp;
	n2cp_hmac_ctx_t		*hmacctx;

/* EXPORT DELETE START */

	if (mechanism->cm_type == AES_GMAC_MECH_INFO_TYPE) {
		CK_AES_GCM_PARAMS gcm_params;
		crypto_mechanism_t gcm_mech;
		int rv;

		if ((rv = process_gmac_mech(mechanism, in, &gcm_params))
		    != CRYPTO_SUCCESS) {
			return (rv);
		}

		gcm_mech.cm_type = AES_GCM_MECH_INFO_TYPE;
		gcm_mech.cm_param_len = sizeof (CK_AES_GCM_PARAMS);
		gcm_mech.cm_param = (char *)&gcm_params;

		return (n2cp_blockatomic(n2cp, &gcm_mech, key,
		    &null_crypto_data, out, kcfreq, 1 /* encrypt */));
	}

	/*
	 * the chip does not support multi-part, and therefore, we can
	 * support upto MAX_DATA_LEN input
	 */
	if (in->cd_length > MAX_DATA_LEN) {
		return (CRYPTO_DATA_LEN_RANGE);
	}

	rv = alloc_hmac_ctx(n2cp, mechanism, key, &reqp);
	if (rv != CRYPTO_SUCCESS) {
		return (rv);
	}
	hmacctx = &(reqp->nr_context->hmacctx);

	if (out->cd_length < hmacctx->signlen) {
		out->cd_length = hmacctx->signlen;
		n2cp_freereq(reqp);
		return (CRYPTO_BUFFER_TOO_SMALL);
	}

	/* bind to cep */
	if (n2cp_use_ulcwq &&
	    (rv = n2cp_find_cep_for_req(reqp) != CRYPTO_SUCCESS)) {
		DBG1(NULL, DWARN, "n2cp_hmac_signatomic: n2cp_find_cep_for_req"
		    "failed with 0x%x", rv);
		n2cp_freereq(reqp);
		return (rv);
	}

	N2CP_REQ_SETUP(reqp, in, out);

	reqp->nr_callback = hmac_done;
	reqp->nr_kcfreq = kcfreq;
	reqp->nr_cmd |= N2CP_OP_SINGLE;

	rv = hmac_start(reqp);

	n2cp_freereq(reqp);

/* EXPORT DELETE END */

	return (rv);
}


int
n2cp_hmac_verifyatomic(n2cp_t *n2cp, crypto_mechanism_t *mechanism,
    crypto_key_t *key, crypto_data_t *in, crypto_data_t *out,
    crypto_req_handle_t kcfreq)
{
	int			rv;
	size_t			saved_length;
	n2cp_request_t		*reqp;
	n2cp_hmac_ctx_t		*hmacctx;

/* EXPORT DELETE START */

	if (mechanism->cm_type == AES_GMAC_MECH_INFO_TYPE) {
		CK_AES_GCM_PARAMS gcm_params;
		crypto_mechanism_t gcm_mech;
	int rv;

		if ((rv = process_gmac_mech(mechanism, in, &gcm_params))
		    != CRYPTO_SUCCESS) {
			return (rv);
		}

		gcm_mech.cm_type = AES_GCM_MECH_INFO_TYPE;
		gcm_mech.cm_param_len = sizeof (CK_AES_GCM_PARAMS);
		gcm_mech.cm_param = (char *)&gcm_params;

		return (n2cp_blockatomic(n2cp, &gcm_mech, key, out,
		    &null_crypto_data, kcfreq, 0 /* decrypt */));
	}

	/* For signature verification, input must be different from output */
	if (in == out) {
		return (CRYPTO_ARGUMENTS_BAD);
	}

	/*
	 * the chip does not support multi-part, and therefore, we can
	 * support upto MAX_DATA_LEN input
	 */
	if (in->cd_length > MAX_DATA_LEN) {
		return (CRYPTO_DATA_LEN_RANGE);
	}

	rv = alloc_hmac_ctx(n2cp, mechanism, key, &reqp);
	if (rv != CRYPTO_SUCCESS) {
		return (rv);
	}
	hmacctx = &(reqp->nr_context->hmacctx);

	if (out->cd_length != hmacctx->signlen) {
		out->cd_length = hmacctx->signlen;
		n2cp_freereq(reqp);
		return (CRYPTO_SIGNATURE_LEN_RANGE);
	}

	/* bind to the cep */
	if (n2cp_use_ulcwq &&
	    (rv = n2cp_find_cep_for_req(reqp) != CRYPTO_SUCCESS)) {
		DBG1(NULL, DWARN, "n2cp_hmac_verifyatomic: "
		    "n2cp_find_cep_for_req failed with 0x%x", rv);
		n2cp_freereq(reqp);
		return (rv);
	}

	saved_length = out->cd_length;
	N2CP_REQ_SETUP(reqp, in, out);
	out->cd_length = saved_length;

	reqp->nr_callback = hmac_done;
	reqp->nr_kcfreq = kcfreq;
	reqp->nr_cmd |= (N2CP_OP_SINGLE | N2CP_OP_VERIFY);

	rv = hmac_start(reqp);

	n2cp_freereq(reqp);

/* EXPORT DELETE END */

	return (rv);
}

static int
hmac_start(n2cp_request_t *reqp)
{
	n2cp_hmac_ctx_t	*hmacctx;
	n2cp_t		*n2cp = (n2cp_t *)reqp->nr_n2cp;
	cwq_cw_t	*cb;
	crypto_data_t	*in = reqp->nr_in;
	int		rv = CRYPTO_SUCCESS;
	int		flag = 0;

/* EXPORT DELETE START */

	/*
	 * Set the cw fields assuming cw is not chained for now.
	 * If chained, some fields are overwritten.
	 */
	cb = &(reqp->nr_cws[0]);
	cb->cw_op = CW_OP_MAC_AUTH | CW_OP_INLINE_BIT;
	cb->cw_enc = 0;
	cb->cw_sob = 1;
	cb->cw_eob = 1;
	cb->cw_intr = 1;
	reqp->nr_cwb = cb;
	reqp->nr_cwcnt = 1;

	switch (reqp->nr_cmd & N2CP_CMD_MASK) {
	case MD5_HMAC_MECH_INFO_TYPE:
	case MD5_HMAC_GENERAL_MECH_INFO_TYPE:
		cb->cw_auth_type = CW_AUTH_HMAC_MD5;
		reqp->nr_job_stat = DS_MD5_HMAC;
		break;
	case SHA1_HMAC_MECH_INFO_TYPE:
	case SHA1_HMAC_GENERAL_MECH_INFO_TYPE:
		cb->cw_auth_type = CW_AUTH_HMAC_SHA1;
		reqp->nr_job_stat = DS_SHA1_HMAC;
		break;
	case SHA256_HMAC_MECH_INFO_TYPE:
	case SHA256_HMAC_GENERAL_MECH_INFO_TYPE:
		cb->cw_auth_type = CW_AUTH_HMAC_SHA256;
		reqp->nr_job_stat = DS_SHA256_HMAC;
		break;
	case SSL3_MD5_MAC_MECH_INFO_TYPE:
		cb->cw_auth_type = CW_AUTH_SSL_HMAC_MD5;
		reqp->nr_job_stat = DS_SSL_MD5_MAC;
		flag = N2CP_SG_WALIGN;
		break;
	case SSL3_SHA1_MAC_MECH_INFO_TYPE:
		cb->cw_auth_type = CW_AUTH_SSL_HMAC_SHA1;
		reqp->nr_job_stat = DS_SSL_SHA1_MAC;
		flag = N2CP_SG_WALIGN;
		break;
#ifdef	SSL3_SHA256_MAC_SUPPORT
	case SSL3_SHA256_MAC_MECH_INFO_TYPE:
		cb->cw_auth_type = CW_AUTH_SSL_HMAC_SHA256;
		reqp->nr_job_stat = DS_SSL_SHA256_MAC;
		flag = N2CP_SG_WALIGN;
		break;
#endif
	default:
		cmn_err(CE_WARN,
		    "hmac_start: mech(%d) unsupported", reqp->nr_cmd);
		return (CRYPTO_MECHANISM_INVALID);
	}

	hmacctx = &(reqp->nr_context->hmacctx);

	/* Setup the input */
	cb->cw_length = in->cd_length - 1;
	if (n2cp_sgcheck(in, N2CP_SG_PCONTIG | flag)) {
		if (n2cp_use_ulcwq ||
		    CRYPTO_DATA_IS_USERSPACE(reqp->nr_in) ||
		    ((rv = n2cp_construct_chain(&(reqp->nr_cws[0]),
		    in, in->cd_length, &reqp->nr_cwcnt)) != CRYPTO_SUCCESS)) {
			/* get a 64K contig mem from the pool */
			if ((rv = n2cp_get_inbuf(n2cp, reqp))
			    != CRYPTO_SUCCESS) {
				return (rv);
			}

			/*
			 * Copy the data to input buf: n2cp_gather updates
			 * in->cd_length
			 */
			rv = n2cp_gather(in, (char *)reqp->nr_in_buf,
			    in->cd_length);
			if (rv != CRYPTO_SUCCESS) {
				n2cp_free_buf(n2cp, reqp);
				return (rv);
			}
			cb->cw_src_addr = reqp->nr_in_buf_paddr;
			reqp->nr_flags |= N2CP_GATHER;
		}
	} else {
		cb->cw_src_addr = va_to_pa(n2cp_get_dataaddr(in, 0));
	}

	/* Setup the output */
	if (reqp->nr_cmd & N2CP_OP_VERIFY) {
		/*
		 * use the context provided digest buffer since
		 * the driver has to supply this buffer
		 */
		cb->cw_dst_addr = reqp->nr_context_paddr + HMAC_DIGEST_OFFSET;
	} else
	if (n2cp_sgcheck(reqp->nr_out, N2CP_SG_PCONTIG | flag)) {
		reqp->nr_flags |= N2CP_SCATTER;
		cb->cw_dst_addr = reqp->nr_context_paddr + HMAC_DIGEST_OFFSET;
	} else {
		cb->cw_dst_addr =
		    va_to_pa(n2cp_get_dataaddr(reqp->nr_out, 0));
	}

	cb->cw_hmac_keylen = hmacctx->keylen - 1;
	cb->cw_hlen = hmacctx->signlen - 1;
	cb->cw_auth_iv_addr = reqp->nr_context_paddr + HMAC_IV_OFFSET;
	cb->cw_auth_key_addr = reqp->nr_context_paddr + HMAC_KEY_OFFSET;

	reqp->nr_resultlen = hmacctx->signlen;

	rv = n2cp_start(n2cp, reqp);
	reqp->nr_errno = rv;

	reqp->nr_callback(reqp);
	n2cp_free_buf(n2cp, reqp);

/* EXPORT DELETE END */

	return (reqp->nr_errno);
}



static void
hmac_done(n2cp_request_t *reqp)
{
	int		rv = CRYPTO_SUCCESS;
	crypto_data_t	*out = reqp->nr_out;
	n2cp_hmac_ctx_t	*hmacctx = &(reqp->nr_context->hmacctx);

/* EXPORT DELETE START */

	if (reqp->nr_errno != CRYPTO_SUCCESS) {
		rv = reqp->nr_errno;
		goto done;
	}

	if (reqp->nr_cmd & N2CP_OP_VERIFY) {
		char	tmpbuf[MAX_DIGESTSZ];
		/*
		 * compare the given signature with the one we just
		 * calculated
		 */
		rv = n2cp_gather(reqp->nr_out, tmpbuf, hmacctx->signlen);
		if (rv != CRYPTO_SUCCESS) {
			goto done;
		}
		if (memcmp(reqp->nr_context->hmacctx.digest, tmpbuf,
		    hmacctx->signlen) != 0) {
			rv = CRYPTO_SIGNATURE_INVALID;
		}
	} else if (reqp->nr_flags & N2CP_SCATTER) {
		/*
		 * if output buffer consists of multiple buffers,
		 * scatter out the result.
		 */
		rv = n2cp_scatter((char *)reqp->nr_context->hmacctx.digest,
		    reqp->nr_out, reqp->nr_resultlen);
		if (rv != CRYPTO_SUCCESS) {
			goto done;
		}
	} else {
		/*
		 * the signature is already copied into the output buffer
		 */
		out->cd_length += reqp->nr_resultlen;
	}

done:
	reqp->nr_errno = rv;

/* EXPORT DELETE END */

}


/*
 * Implement SSL3_SHA1_HMAC using SHA-1 hasing to workaround the N2
 * errata: Metrax Bug ID#107002
 */

#define	SSL_SHA1_PADLEN		40
#define	SHA1_DIGESTLEN		20

/* EXPORT DELETE START */

static uint32_t pad1[] = {0x36363636, 0x36363636, 0x36363636, 0x36363636,
    0x36363636, 0x36363636, 0x36363636, 0x36363636,
    0x36363636, 0x36363636, 0x36363636, 0x36363636};
static uint32_t pad2[] = {0x5c5c5c5c, 0x5c5c5c5c, 0x5c5c5c5c, 0x5c5c5c5c,
    0x5c5c5c5c, 0x5c5c5c5c, 0x5c5c5c5c, 0x5c5c5c5c,
    0x5c5c5c5c, 0x5c5c5c5c, 0x5c5c5c5c, 0x5c5c5c5c};

/* EXPORT DELETE END */

/*
 * this does SSL3_SHA1_HMAC using hash operation
 */
/*ARGSUSED5*/
int
n2cp_ssl3_sha1_mac_signatomic(n2cp_t *n2cp, crypto_mechanism_t *mechanism,
    crypto_key_t *key, crypto_data_t *in, crypto_data_t *out,
    crypto_req_handle_t kcfreq)
{
	int		rv;
	uchar_t		*data;
	size_t		datalen;
	int		in_alloced = 0;
	int		signlen;
	int		keylen;
	SHA1_CTX	ctx;
	uchar_t		hash[SHA1_DIGESTLEN];

/* EXPORT DELETE START */

	if (mechanism->cm_param_len == sizeof (uint32_t)) {
		signlen = *(uint32_t *)(void *)mechanism->cm_param;
	} else if (mechanism->cm_param_len == sizeof (uint64_t)) {
		signlen = *(uint64_t *)(void *)mechanism->cm_param;
	} else {
		return (CRYPTO_MECHANISM_PARAM_INVALID);
	}
	if ((signlen <= 0) || (signlen > SHA1_DIGESTLEN)) {
		/* invalid parameter value */
		DBG1(n2cp, DWARN, "n2cp_ssl3_sha1_hmac: invalid signlen [%d]",
		    signlen);
		return (EINVAL);
	}
	if (signlen > out->cd_length) {
		DBG2(n2cp, DWARN, "n2cp_ssl3_sha1_hmac: not enough space "
		    "param[%d] > signlen[%d]", signlen, out->cd_length);
		out->cd_length = signlen;
		return (ENOSPC);
	}

	/* get the key value */
	if (key->ck_format != CRYPTO_KEY_RAW) {
		/* the key must be passed by value */
		return (CRYPTO_KEY_TYPE_INCONSISTENT);
	}
	keylen = CRYPTO_BITS2BYTES(key->ck_length);
	if (keylen > CW_MAX_KEY_LEN) {
		cmn_err(CE_WARN, "alloc_hmac_ctx: keylen(%d) > maxlen(%d)\n",
		    keylen, CW_MAX_KEY_LEN);
		return (CRYPTO_KEY_SIZE_RANGE);
	}

	/* prepare the input: if the input is chained, gather the buffers */
	if (n2cp_sgcheck(in, N2CP_SG_CONTIG)) {
		datalen = in->cd_length;
		data = kmem_alloc(datalen, KM_SLEEP);
		in_alloced = 1;
		rv = n2cp_gather(in, (char *)data, datalen);
		if (rv != CRYPTO_SUCCESS) {
			kmem_free(data, datalen);
			return (rv);
		}
	} else {
		datalen = in->cd_length;
		data = (uchar_t *)n2cp_bufdaddr(in);
		if (data == NULL) {
			return (CRYPTO_ARGUMENTS_BAD);
		}
	}

	/*
	 * Inner Hash [key + pad1 + data]
	 */
	SHA1Init(&ctx);
	SHA1Update(&ctx, key->ck_data, keylen);
	SHA1Update(&ctx, (uchar_t *)pad1, SSL_SHA1_PADLEN);
	SHA1Update(&ctx, data, datalen);
	SHA1Final(hash, &ctx);

	/* free the input buffer if allocated */
	if (in_alloced) {
		kmem_free(data, datalen);
	}

	/*
	 * Outter Hash [key + pad2 + inner_digest]
	 */
	SHA1Init(&ctx);
	SHA1Update(&ctx, key->ck_data, keylen);
	SHA1Update(&ctx, (uchar_t *)pad2, SSL_SHA1_PADLEN);
	SHA1Update(&ctx, hash, SHA1_DIGESTLEN);
	SHA1Final(hash, &ctx);

	/* n2cp_scatter expects the cd_length to be zero */
	out->cd_length = 0;

	/*
	 * Update kstat. We always use n_stats[0] for the counter
	 * since this is an emulated operation and hence there is
	 * no performance impact from having a global counter.
	 */
	atomic_inc_64(&n2cp->n_stats[0][DS_SSL_SHA1_MAC]);

	/* copy out the output to the provided buffer */

/* EXPORT DELETE END */

	return (n2cp_scatter((char *)hash, out, signlen));
}

int
n2cp_ssl3_sha1_mac_verifyatomic(n2cp_t *n2cp, crypto_mechanism_t *mechanism,
    crypto_key_t *key, crypto_data_t *in, crypto_data_t *out,
    crypto_req_handle_t kcfreq)
{
	crypto_data_t	tmpout;
	int		siglen;
	int		rv;
	char		tmpbuf[SHA1_DIGESTLEN];
	char		sig[SHA1_DIGESTLEN];

/* EXPORT DELETE START */

	tmpout.cd_format = CRYPTO_DATA_RAW;
	tmpout.cd_length = SHA1_DIGESTLEN;
	tmpout.cd_offset = 0;
	tmpout.cd_raw.iov_base = tmpbuf;
	tmpout.cd_raw.iov_len = SHA1_DIGESTLEN;

	rv = n2cp_ssl3_sha1_mac_signatomic(n2cp, mechanism, key,
	    in, &tmpout, kcfreq);
	if (rv != CRYPTO_SUCCESS) {
		DBG0(n2cp, DWARN, "n2cp_ssl3_sha1_mac_verifyatomic: "
		    "Failed to sign");
		return (rv);
	}

	/* kstat is updated by n2cp_ssl3_sha1_mac_signatomic */

	siglen = out->cd_length;
	rv = n2cp_gather(out, sig, siglen);
	if (rv != CRYPTO_SUCCESS) {
		DBG0(n2cp, DWARN, "n2cp_ssl3_sha1_mac_verifyatomic: "
		    "Failed to gather the given signature");
		return (rv);
	}

	if (memcmp(tmpbuf, sig, siglen) != 0) {
		DBG0(n2cp, DWARN, "n2cp_ssl3_sha1_mac_verifyatomic: "
		    "Signature mismatch");
		return (CRYPTO_SIGNATURE_INVALID);
	}

/* EXPORT DELETE END */

	return (CRYPTO_SUCCESS);
}

static int
process_gmac_mech(crypto_mechanism_t *mech, crypto_data_t *data,
    CK_AES_GCM_PARAMS *gcm_params)
{
	/* LINTED: pointer alignment */
	CK_AES_GMAC_PARAMS *params = (CK_AES_GMAC_PARAMS *)mech->cm_param;

	if (mech->cm_type != AES_GMAC_MECH_INFO_TYPE)
		return (CRYPTO_MECHANISM_INVALID);

	if (mech->cm_param_len != sizeof (CK_AES_GMAC_PARAMS))
		return (CRYPTO_MECHANISM_PARAM_INVALID);

	if (params->pIv == NULL)
		return (CRYPTO_MECHANISM_PARAM_INVALID);

	gcm_params->pIv = params->pIv;
	gcm_params->ulIvLen = AES_GMAC_IV_LEN;
	gcm_params->ulTagBits = AES_GMAC_TAG_BITS;

	if (data == NULL)
		return (CRYPTO_SUCCESS);

	if (data->cd_format != CRYPTO_DATA_RAW)
		return (CRYPTO_ARGUMENTS_BAD);

	gcm_params->pAAD = (uchar_t *)data->cd_raw.iov_base;
	gcm_params->ulAADLen = data->cd_length;
	return (CRYPTO_SUCCESS);
}
