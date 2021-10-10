/*
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/types.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/kmem.h>
#include <sys/crypto/common.h>
#include <sys/crypto/spi.h>
#include <sys/ncp.h>
#include "ncp_ecc.h"

/*
 * ECDSA implementation.
 */

#define	CHECK(expr) if ((rv = (expr)) != BIG_OK) { goto cleanexit; }

static void ncp_ecdsa_sign_done(ncp_request_t *, int errno);
static void ncp_ecdsa_verify_done(ncp_request_t *, int errno);
static void ncp_ecgendone(ncp_request_t *, int errno);


void
ncp_ecdsactxfree(void *arg)
{
	crypto_ctx_t	*ctx = (crypto_ctx_t *)arg;
	ncp_request_t	*reqp = ctx->cc_provider_private;

/* EXPORT DELETE START */

	if (reqp == NULL)
		return;

	reqp->nr_ctx_cm_type = 0;
	reqp->nr_atomic = 0;
	kmem_cache_free(reqp->nr_ncp->n_request_cache, reqp);
	ctx->cc_provider_private = NULL;

/* EXPORT DELETE END */

}


/* ARGSUSED */
int
ncp_ecdsa_sign_init(crypto_ctx_t *ctx, crypto_mechanism_t *mechanism,
    crypto_key_t *key, int mode)
{
	crypto_object_attribute_t	*attr;
	unsigned			oid_len = 0, value_len;
	uchar_t				*oid, *value;
	ncp_request_t			*reqp = NULL;
	ncp_t				*ncp = (ncp_t *)ctx->cc_provider;

/* EXPORT DELETE START */

	if ((reqp = ncp_getreq(ncp, 1)) == NULL) {
		ncp_error(ncp, "ncp_ecdsa_sign_init: "
		    "unable to allocate request for ECDSA");
		return (CRYPTO_HOST_MEMORY);
	}

	ctx->cc_provider_private = reqp;
	reqp->nr_ctx_cm_type = mechanism->cm_type;
	reqp->nr_mode = mode;

	if ((attr = ncp_get_key_attr(key)) == NULL) {
		DBG0(NULL, DWARN, "ncp_ecdsa_sign_init: "
		    "key attributes missing");
		ncp_ecdsactxfree(ctx);
		return (CRYPTO_KEY_TYPE_INCONSISTENT);
	}

	/* EC params (curve OID) */
	if (ncp_attr_lookup_uint8_array(attr, key->ck_count, CKA_EC_PARAMS,
	    (void *) &oid, &oid_len)) {
		DBG0(NULL, DWARN, "ncp_ecdsa_sign_init: EC params not present");
		ncp_ecdsactxfree(ctx);
		return (CRYPTO_ARGUMENTS_BAD);
	}

	/* Value */
	if (ncp_attr_lookup_uint8_array(attr, key->ck_count, CKA_VALUE,
	    (void *) &value, &value_len)) {
		DBG0(NULL, DWARN, "ncp_ecdsa_sign_init: "
		    "private value not present");
		ncp_ecdsactxfree(ctx);
		return (CRYPTO_ARGUMENTS_BAD);
	}

	if ((oid_len < EC_MIN_OID_LEN) || (oid_len > EC_MAX_OID_LEN)) {
		ncp_ecdsactxfree(ctx);
		return (CRYPTO_ATTRIBUTE_VALUE_INVALID);
	}
	if (value_len > CRYPTO_BITS2BYTES(EC_MAX_KEY_LEN)) {
		ncp_ecdsactxfree(ctx);
		return (CRYPTO_KEY_SIZE_RANGE);
	}

	reqp->nr_ecc_oidlen = oid_len;
	bcopy(oid, reqp->nr_ecc_oid, oid_len);
	reqp->nr_ecc_dlen = value_len;
	bcopy(value, reqp->nr_ecc_d, value_len);

/* EXPORT DELETE END */

	return (CRYPTO_SUCCESS);
}



int
ncp_ecdsa_sign(crypto_ctx_t *ctx, crypto_data_t *data, crypto_data_t *sig,
    crypto_req_handle_t req)
{
	ncp_request_t	*reqp = ctx->cc_provider_private;
	ncp_t		*ncp = ctx->cc_provider;
	int		err;
	int		rv = CRYPTO_QUEUED;
	uint32_t	buflen, siglen;

/* EXPORT DELETE START */

	buflen = ncp_length(data);
	if (buflen > ECDSA_MAX_INLEN) {
		DBG2(ncp, DCHATTY,
		    "ncp_ecdsa_sign: data length[%d] is greater than max"
		    "inlen[%d]", buflen, ECDSA_MAX_INLEN);
		rv = CRYPTO_DATA_LEN_RANGE;
		goto errout;
	}
	siglen = ncp_length(sig);
	if (siglen > ECDSA_MAX_SIGLEN) {
		siglen = ECDSA_MAX_SIGLEN;
	}

	/*
	 * Don't change the data values of the data crypto_data_t structure
	 * yet. Only reset the sig cd_length to zero before writing to it.
	 */

	reqp->nr_job_stat = DS_ECDSASIGN;
	reqp->nr_byte_stat = -1;
	reqp->nr_in = data;
	reqp->nr_out = sig;

	reqp->nr_kcf_req = req;

	/*
	 * ncp_gather() increments cd_offset & dec. cd_length by
	 * ECDSAPARTLEN
	 */
	err = ncp_gather(data, (caddr_t)reqp->nr_inbuf, buflen, 0);
	if (err != CRYPTO_SUCCESS) {
		DBG0(ncp, DWARN, "ncp_ecdsa_sign: ncp_gather() failed");
		rv = err;
		goto errout;
	}

	reqp->nr_pkt_length = (uint16_t)buflen;
	reqp->nr_inlen = buflen;
	reqp->nr_outlen = siglen;
	reqp->nr_callback = ncp_ecdsa_sign_done;

	/* schedule the work by doing a submit */
	rv = ncp_start(ncp, reqp);

errout:

	if ((rv != CRYPTO_QUEUED) && (rv != CRYPTO_BUFFER_TOO_SMALL))
		(void) ncp_free_context(ctx);

/* EXPORT DELETE END */

	return (rv);
}


static void
ncp_ecdsa_sign_done(ncp_request_t *reqp, int errno)
{
	int	atomic = reqp->nr_atomic;

/* EXPORT DELETE START */

	if (errno != CRYPTO_SUCCESS) {
		goto errout;
	}

	/*
	 * Set the sig cd_length to zero so it's ready to take the
	 * signature.
	 */
	reqp->nr_out->cd_length = 0;
	errno = ncp_scatter((caddr_t)reqp->nr_outbuf, reqp->nr_out,
	    reqp->nr_outlen, 0);
	if (errno != CRYPTO_SUCCESS) {
		DBG0(reqp->nr_ncp, DWARN,
		    "ncp_ecdsa_sign_done: ncp_scatter() failed");
		goto errout;
	}

errout:

	if (errno == CRYPTO_BUFFER_TOO_SMALL) {
		reqp->nr_out->cd_length = reqp->nr_outlen;
	}

	ASSERT(reqp->nr_kcf_req != NULL);

	if ((reqp->nr_kcf_req == NCP_FIPS_POST_REQUEST) ||
	    (reqp->nr_kcf_req == NCP_FIPS_CONSIST_REQUEST)) {
		/* FIPS POST request */
		reqp->nr_ncp->n_fips_post_status = errno;
		mutex_enter(&reqp->nr_ncp->n_fips_post_lock);
		cv_signal(&reqp->nr_ncp->n_fips_post_cv);
		mutex_exit(&reqp->nr_ncp->n_fips_post_lock);
	} else {
#ifdef	DEBUG
		ncp_t	*ncp = reqp->nr_ncp;
#endif

		/* notify framework that request is completed */
		crypto_op_notification(reqp->nr_kcf_req, errno);
#ifdef DEBUG
		if (errno != CRYPTO_SUCCESS) {
			DBG1(ncp, DWARN,
			    "ncp_ecdsa_sign_done: rtn 0x%x to kef via "
			    "crypto_op_notification", errno);
		}
#endif /* DEBUG */
	}

	/*
	 * For non-atomic operations, reqp will be freed in the kCF
	 * callback function since it may be needed again if
	 * CRYPTO_BUFFER_TOO_SMALL is returned to kCF
	 */
	if (atomic) {
		crypto_ctx_t ctx; /* on the stack */
		ctx.cc_provider_private = reqp;
		ncp_ecdsactxfree(&ctx);
	}

/* EXPORT DELETE END */

}


/* ARGSUSED */
int
ncp_ecdsa_verify_init(crypto_ctx_t *ctx, crypto_mechanism_t *mechanism,
    crypto_key_t *key, int mode)
{
	crypto_object_attribute_t	*attr;
	unsigned			oid_len = 0, pointlen, len;
	uchar_t				*oid, *point;
	ncp_request_t			*reqp = NULL;
	ncp_t				*ncp = (ncp_t *)ctx->cc_provider;

/* EXPORT DELETE START */

	if ((reqp = ncp_getreq(ncp, 1)) == NULL) {
		ncp_error(ncp, "ncp_ecdsa_verify_init: "
		    "unable to allocate request for ECDSA");
		return (CRYPTO_HOST_MEMORY);
	}

	ctx->cc_provider_private = reqp;
	reqp->nr_ctx_cm_type = mechanism->cm_type;
	reqp->nr_mode = mode;

	if ((attr = ncp_get_key_attr(key)) == NULL) {
		DBG0(NULL, DWARN, "ncp_ecdsa_verify_init: "
		    "key attributes missing");
		ncp_ecdsactxfree(ctx);
		return (CRYPTO_KEY_TYPE_INCONSISTENT);
	}

	/* EC params (curve OID) */
	if (ncp_attr_lookup_uint8_array(attr, key->ck_count, CKA_EC_PARAMS,
	    (void *) &oid, &oid_len)) {
		DBG0(NULL, DWARN, "ncp_ecdsa_verify_init: "
		    "EC params not present");
		ncp_ecdsactxfree(ctx);
		return (CRYPTO_ARGUMENTS_BAD);
	}

	/* EC point (0x4|X|Y) */
	if (ncp_attr_lookup_uint8_array(attr, key->ck_count, CKA_EC_POINT,
	    (void *) &point, &pointlen)) {
		DBG0(NULL, DWARN, "ncp_ecdsa_verify_init: "
		    "EC point not present");
		ncp_ecdsactxfree(ctx);
		return (CRYPTO_ARGUMENTS_BAD);
	}

	if ((oid_len < EC_MIN_OID_LEN) || (oid_len > EC_MAX_OID_LEN)) {
		ncp_ecdsactxfree(ctx);
		return (CRYPTO_ATTRIBUTE_VALUE_INVALID);
	}
	/* Make sure EC Point came in as 0x04|X|Y */
	if (point[0] != 0x04) {
		ncp_ecdsactxfree(ctx);
		return (CRYPTO_ATTRIBUTE_VALUE_INVALID);
	}
	point++;
	pointlen--;
	if ((pointlen % 2) != 0) {
		ncp_ecdsactxfree(ctx);
		return (CRYPTO_ATTRIBUTE_VALUE_INVALID);
	}
	len = pointlen / 2;
	if (len > EC_MAX_KEY_LEN) {
		ncp_ecdsactxfree(ctx);
		return (CRYPTO_KEY_SIZE_RANGE);
	}
	reqp->nr_ecc_oidlen = oid_len;
	bcopy(oid, reqp->nr_ecc_oid, oid_len);

	reqp->nr_ecc_xlen =
	    reqp->nr_ecc_ylen = len;
	bcopy(point, reqp->nr_ecc_x, len);
	bcopy(point + len, reqp->nr_ecc_y, len);

/* EXPORT DELETE END */

	return (CRYPTO_SUCCESS);
}


int
ncp_ecdsa_verify(crypto_ctx_t *ctx, crypto_data_t *data, crypto_data_t *sig,
    crypto_req_handle_t req)
{
	ncp_request_t	*reqp = ctx->cc_provider_private;
	ncp_t		*ncp = ctx->cc_provider;
	char		*cursor;
	int		len;
	int		rv = CRYPTO_QUEUED;

/* EXPORT DELETE START */

	/* Impossible for verify to be an in-place operation. */
	if (sig == NULL) {
		rv = CRYPTO_ARGUMENTS_BAD;
		goto errout;
	}

	if (ncp_length(data) > ECDSA_MAX_INLEN) {
		DBG2(ncp, DCHATTY,
		    "ncp_ecdsa_verify: datalen[%d] > maxinlen[%d]",
		    ncp_length(data), ECDSA_MAX_INLEN);
		rv = CRYPTO_DATA_LEN_RANGE;
		goto errout;
	}

	if (ncp_length(sig) > ECDSA_MAX_SIGLEN) {
		DBG2(ncp, DCHATTY, "ncp_ecdsa_verify: signature length[%d] >"
		    " max_siglen[%d]", ncp_length(sig), ECDSA_MAX_SIGLEN);
		rv = CRYPTO_SIGNATURE_LEN_RANGE;
		goto errout;
	}

	/* kstat */
	reqp->nr_job_stat = DS_ECDSAVERIFY;
	reqp->nr_byte_stat = -1;

	reqp->nr_in = data;
	reqp->nr_out = sig;
	reqp->nr_kcf_req = req;
	reqp->nr_flags |= DR_SCATTER | DR_GATHER;

	/*
	 * nr_inbuf is formatted as follows:
	 * 	signature len : uint32_t
	 *	signature : char[]
	 *	data : char[]
	 * nr_inlen = sizeof (uint32) + data len + signature len
	 */
	cursor = (char *)reqp->nr_inbuf;
	len = ncp_length(sig);
	/* LINTED */
	*(uint32_t *)cursor = len;
	cursor += sizeof (uint32_t);
	reqp->nr_inlen = sizeof (uint32_t);

	rv = ncp_gather(sig, cursor, len, 0);
	cursor += len;
	reqp->nr_inlen += len;
	sig->cd_length = len;
	if (rv == CRYPTO_SUCCESS) {
		len = ncp_length(data);
		reqp->nr_inlen += len;
		rv = ncp_gather(data, cursor, len, 0);
		data->cd_length = len;
		cursor += len;
	}
	if (rv != CRYPTO_SUCCESS) {
		goto errout;
	}

	reqp->nr_outlen = 0;
	reqp->nr_callback = ncp_ecdsa_verify_done;

	/* schedule the work by doing a submit */
	rv = ncp_start(ncp, reqp);

errout:

	if (rv != CRYPTO_QUEUED && rv != CRYPTO_BUFFER_TOO_SMALL)
		(void) ncp_free_context(ctx);

/* EXPORT DELETE END */

	return (rv);
}


static void
ncp_ecdsa_verify_done(ncp_request_t *reqp, int errno)
{
	int	atomic = reqp->nr_atomic;
/* EXPORT DELETE START */

	ASSERT(reqp->nr_kcf_req != NULL);

	if ((reqp->nr_kcf_req == NCP_FIPS_POST_REQUEST) ||
	    (reqp->nr_kcf_req == NCP_FIPS_CONSIST_REQUEST)) {
		/* FIPS POST request */
		reqp->nr_ncp->n_fips_post_status = errno;
		mutex_enter(&reqp->nr_ncp->n_fips_post_lock);
		cv_signal(&reqp->nr_ncp->n_fips_post_cv);
		mutex_exit(&reqp->nr_ncp->n_fips_post_lock);
	} else {
#ifdef	DEBUG
		ncp_t	*ncp = reqp->nr_ncp;
#endif
		/* notify framework that request is completed */
		crypto_op_notification(reqp->nr_kcf_req, errno);
#ifdef DEBUG
		if (errno != CRYPTO_SUCCESS) {
			DBG1(ncp, DWARN,
			    "ncp_ecdsa_verify_done: rtn 0x%x to kef via "
			    "crypto_op_notification", errno);
		}
#endif /* DEBUG */
	}

	/*
	 * For non-atomic operations, reqp will be freed in the kCF
	 * callback function since it may be needed again if
	 * CRYPTO_BUFFER_TOO_SMALL is returned to kCF
	 */
	if (atomic) {
		crypto_ctx_t ctx; /* on the stack */
		ctx.cc_provider_private = reqp;
		ncp_ecdsactxfree(&ctx);
	}

/* EXPORT DELETE END */

}


int
ncp_ecdsaatomic(crypto_provider_handle_t provider,
    crypto_session_id_t session_id, crypto_mechanism_t *mechanism,
    crypto_key_t *key, crypto_data_t *data, crypto_data_t *sig,
    crypto_req_handle_t req, int mode)
{
	crypto_ctx_t	ctx;	/* on the stack */
	int		rv;

/* EXPORT DELETE START */

	ctx.cc_provider = provider;
	ctx.cc_session = session_id;

	if (mode == NCP_ECDSA_SIGN) {
		rv = ncp_ecdsa_sign_init(&ctx, mechanism, key, mode);
		if (rv != CRYPTO_SUCCESS) {
			DBG0(NULL, DWARN, "ncp_ecdsaatomic: "
			    "ncp_ecdsa_sign_init() failed");
			return (rv);
		}

		/*
		 * Set the atomic flag so that the hardware callback function
		 * will free the context.
		 */
		((ncp_request_t *)ctx.cc_provider_private)->nr_atomic = 1;

		rv = ncp_ecdsa_sign(&ctx, data, sig, req);
	} else {
		ASSERT(mode == NCP_ECDSA_VRFY);

		rv = ncp_ecdsa_verify_init(&ctx, mechanism, key, mode);
		if (rv != CRYPTO_SUCCESS) {
			DBG0(NULL, DWARN, "ncp_ecdsaatomic: "
			    "ncp_ecdsa_verify_init() failed");
			return (rv);
		}

		/*
		 * Set the atomic flag so that the hardware callback function
		 * will free the context.
		 */
		((ncp_request_t *)ctx.cc_provider_private)->nr_atomic = 1;

		rv = ncp_ecdsa_verify(&ctx, data, sig, req);
	}


/* EXPORT DELETE END */

	return (rv);
}


/*
 * at this point, everything is checked already
 */

int
ncp_ecdsa_sign_process(ncp_t *ncp, ncp_request_t *reqp)
{
	char		*out;
	uint8_t		*d, *messagehash;
	int		dlen, rlen, slen, hashlen, nlen;
	ECC_curve_t	*crv;
	uint8_t		r[MAXECKEYLEN];
	uint8_t		s[MAXECKEYLEN];

/* EXPORT DELETE START */

	hashlen = reqp->nr_inlen;
	messagehash = reqp->nr_inbuf;
	crv = ncp_ECC_find_curve(reqp->nr_ecc_oid);

	if (crv == NULL) {
		return (CRYPTO_ATTRIBUTE_VALUE_INVALID);
	}

	nlen = CRYPTO_BITS2BYTES(crv->orderMSB + 1);

	if (reqp->nr_outlen < 2 * nlen) {
		reqp->nr_outlen = 2 * nlen;
		return (CRYPTO_BUFFER_TOO_SMALL);
	}

	d = reqp->nr_ecc_d;
	dlen = reqp->nr_ecc_dlen;
	rlen = slen = nlen;

	if (ECC_ECDSA_sign(messagehash, hashlen, d, dlen,
	    r, &rlen, s, &slen, crv, ncp, reqp) != BIG_OK) {
		return (CRYPTO_ATTRIBUTE_VALUE_INVALID);
	}

	out = (char *)(reqp->nr_outbuf);
	(void) memset(out, 0, 2 * nlen);
	(void) memcpy(out + nlen - rlen, r, rlen);
	(void) memcpy(out + 2 * nlen - slen, s, slen);
	reqp->nr_outlen = 2 * nlen;

/* EXPORT DELETE END */

	return (CRYPTO_SUCCESS);
}


/*
 * at this point, everything is checked already
 */
int
ncp_ecdsa_verify_process(ncp_t *ncp, ncp_request_t *reqp)
{
	uint8_t		*r, *s, *x, *y, *messagehash;
	int		rlen, slen, xlen, ylen;
	int		hashlen, verified;
	ECC_curve_t	*crv;

/* EXPORT DELETE START */

	crv = ncp_ECC_find_curve(reqp->nr_ecc_oid);

	if (crv == NULL) {
		return (CRYPTO_ATTRIBUTE_VALUE_INVALID);
	}

	/* LINTED */
	rlen = slen = *((uint32_t *)(reqp->nr_inbuf)) / 2;
	r = ((uint8_t *)(reqp->nr_inbuf)) + sizeof (uint32_t);
	s = r + rlen;

	hashlen = reqp->nr_inlen - rlen - slen - sizeof (uint32_t);
	messagehash = ((uchar_t *)(reqp->nr_inbuf)) +
	    sizeof (uint32_t) + rlen + slen;
	xlen = reqp->nr_ecc_xlen;
	ylen = reqp->nr_ecc_ylen;
	x = (uint8_t *)(reqp->nr_ecc_x);
	y = (uint8_t *)(reqp->nr_ecc_y);

	if (ECC_ECDSA_verify(&verified, messagehash, hashlen,
	    r, rlen, s, slen, x, xlen, y, ylen, crv, ncp, reqp) != BIG_OK) {
		return (CRYPTO_ATTRIBUTE_VALUE_INVALID);
	}

/* EXPORT DELETE END */

	if (verified) {
		return (CRYPTO_SUCCESS);
	} else {
		return (CRYPTO_SIGNATURE_INVALID);
	}
}


/*
 * EC key pair gen
 */
/* ARGSUSED */
int
ncp_ec_generate_key(crypto_provider_handle_t provider,
    crypto_session_id_t session_id, crypto_mechanism_t *mechanism,
    crypto_object_attribute_t *in_pub_attrs, uint_t in_pub_attr_count,
    crypto_object_attribute_t *in_pri_attrs, uint_t in_pri_attr_count,
    crypto_object_attribute_t *out_pub_attrs, uint_t out_pub_attr_count,
    crypto_object_attribute_t *out_pri_attrs, uint_t out_pri_attr_count,
    crypto_req_handle_t req)
{
	ncp_t		*ncp = (ncp_t *)provider;
	ncp_request_t	*reqp;
	int		rv;
	uint8_t		*ecparam;
	uint32_t	ecparamlen;
	crypto_ctx_t	ctx;

/* EXPORT DELETE START */

	/* Public Exponent */
	if ((rv = ncp_attr_lookup_uint8_array(in_pub_attrs,
	    in_pub_attr_count, CKA_EC_PARAMS, (void **)&ecparam,
	    &ecparamlen)) != CRYPTO_SUCCESS) {
		DBG0(NULL, DWARN, "ncp_ec_generate_key: "
		    "failed to retrieve ec params");
		return (rv);
	}

	if ((reqp = ncp_getreq(ncp, 1)) == NULL) {
		DBG0(NULL, DWARN,
		    "ncp_ec_generate_key: unable to allocate request");
		return (CRYPTO_HOST_MEMORY);
	}

	/* store OID */
	bcopy(ecparam, reqp->nr_ecc_oid, ecparamlen);
	reqp->nr_ecc_oidlen = ecparamlen;

	reqp->nr_public_attrs = out_pub_attrs;
	reqp->nr_public_attrs_count = out_pub_attr_count;
	reqp->nr_private_attrs = out_pri_attrs;
	reqp->nr_private_attrs_count = out_pri_attr_count;
	reqp->nr_ctx_cm_type = mechanism->cm_type;
	reqp->nr_kcf_req = req;
	reqp->nr_mode = NCP_EC_GEN;
	reqp->nr_job_stat = DS_ECGEN;
	reqp->nr_callback = ncp_ecgendone;


	/*
	 * Set the atomic flag so that the hardware callback function
	 * will free the context.
	 */
	reqp->nr_atomic = 1;

	/* schedule the work by doing a submit */
	mutex_enter(&reqp->nr_keygen_lock);
	rv = ncp_start(ncp, reqp);
	if (rv == CRYPTO_QUEUED) {
		/* wait for the job to be completed. */
		cv_wait(&reqp->nr_keygen_cv, &reqp->nr_keygen_lock);
		rv = reqp->nr_keygen_status;
	}
	mutex_exit(&reqp->nr_keygen_lock);

	/*
	 * Run Pair-wise Consistency Test on the generated key pair
	 */
	if (rv == CRYPTO_SUCCESS) {
		mutex_enter(&ncp->n_fips_consistency_lock);
		rv = ncp_ecc_pairwise_consist_test(ncp,
		    reqp->nr_ecc_oid, reqp->nr_ecc_oidlen,
		    reqp->nr_public_attrs, reqp->nr_public_attrs_count,
		    reqp->nr_private_attrs, reqp->nr_private_attrs_count);
		if (rv != CRYPTO_SUCCESS) {
			mutex_exit(&ncp->n_fips_consistency_lock);
			cmn_err(CE_WARN,
			    "ncp: FIPS140-2: ECDSA Key Gen: Pair-Wise "
			    "Consistency Test failed");
			return (rv);
		}
		mutex_exit(&ncp->n_fips_consistency_lock);
	}

	ctx.cc_provider_private = reqp;
	ncp_ecdsactxfree(&ctx);

/* EXPORT DELETE END */

	return (rv);
}

static void
ncp_ecgendone(ncp_request_t *reqp, int errno)
{
/* EXPORT DELETE START */

	ASSERT(reqp->nr_kcf_req != NULL);

	reqp->nr_keygen_status = errno;
	mutex_enter(&reqp->nr_keygen_lock);
	cv_signal(&reqp->nr_keygen_cv);
	mutex_exit(&reqp->nr_keygen_lock);

/* EXPORT DELETE END */

}


/*
 * at this point, everything is checked already
 */
int
ncp_ec_generate_process(ncp_t *ncp, ncp_request_t *reqp)
{
	/*
	 * fill "CKA_VALUE" in private template, and "CKA_EC_POINT" in
	 * public template.  ecpoint should be 0x04|X|Y
	 */
	uint8_t		d[MAXECKEYLEN];
	uint8_t		x[MAXECKEYLEN];
	uint8_t		y[MAXECKEYLEN];
	int		i;
	int		dlen, xlen, ylen, plen;
	ECC_curve_t	*crv;
	int		rv;
	crypto_object_attribute_t	*ap;
	uchar_t		*attrvalp;

/* EXPORT DELETE START */

	crv = ncp_ECC_find_curve(reqp->nr_ecc_oid);

	if (crv == NULL) {
		return (CRYPTO_ATTRIBUTE_VALUE_INVALID);
	}

	dlen = xlen = ylen = MAXECKEYLEN;
	rv = ECC_key_pair_gen(d, &dlen, x, &xlen, y, &ylen,
	    crv, ncp, reqp);
	if (rv != BIG_OK) {
		DBG1(ncp, DWARN, "ECC_key_pair_gen failed with "
		    "0x%x\n", rv);
		return (CRYPTO_FAILED);
	}

	plen = CRYPTO_BITS2BYTES(crv->modulusinfo.modulusMSB + 1);

	ap = ncp_find_attribute(reqp->nr_public_attrs,
	    reqp->nr_public_attrs_count, CKA_EC_POINT);

	if (ap == NULL) {
		return (CRYPTO_FAILED);
	}

	if (ap->oa_value == NULL || ap->oa_value_len == 0) {
		DBG0(NULL, DWARN, "ncp_ec_generate: "
		    "NULL attribute value or zero length\n");
		return (CRYPTO_FAILED);
	}

	if (ap->oa_value_len < 2 * plen + 1) {
		DBG2(NULL, DWARN, "ncp_ec_generate: "
		    "attribute buffer too small (%d < %d)\n",
		    ap->oa_value_len, plen);
		return (CRYPTO_FAILED);
	}

	attrvalp = (uchar_t *)ap->oa_value;
	attrvalp[0] = 0x4;
	for (i = 0; i < plen - xlen; i++) {
		attrvalp[1 + i] = 0;
	}
	bcopy(x, attrvalp + (1 + plen - xlen), xlen);
	for (i = 0; i < plen - ylen; i++) {
		attrvalp[1 + plen + i] = 0;
	}
	bcopy(y, attrvalp + (1 + 2 * plen - ylen), ylen);
	ap->oa_value_len = 2 * plen + 1;

	ap = ncp_find_attribute(reqp->nr_private_attrs,
	    reqp->nr_private_attrs_count, CKA_VALUE);

	if (ap == NULL) {
		return (CRYPTO_FAILED);
	}

	if (ap->oa_value == NULL || ap->oa_value_len == 0) {
		DBG0(NULL, DWARN, "ncp_ec_generate: "
		    "NULL attribute value2 or zero length\n");
		return (CRYPTO_FAILED);
	}

	if (ap->oa_value_len < dlen) {
		DBG2(NULL, DWARN, "ncp_ec_generate: "
		    "attribute buffer2 too small (%d < %d)\n",
		    ap->oa_value_len, dlen);
		return (CRYPTO_FAILED);
	}

	attrvalp = (uchar_t *)ap->oa_value;
	bcopy(d, attrvalp, dlen);
	ap->oa_value_len = dlen;

/* EXPORT DELETE END */

	return (CRYPTO_SUCCESS);
}
