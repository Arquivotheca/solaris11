/*
 * Copyright (c) 2005, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/types.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/kmem.h>
#include <sys/crypto/common.h>
#include <sys/crypto/spi.h>
#include <sys/ncp.h>

/*
 * DSA implementation.
 */

static void ncp_dsa_sign_done(ncp_request_t *, int errno);
static void ncp_dsa_verify_done(ncp_request_t *, int errno);

int ncp_dsa_sign(crypto_ctx_t *ctx, crypto_data_t *data, crypto_data_t *sig,
    crypto_req_handle_t req);
int ncp_dsa_verify(crypto_ctx_t *ctx, crypto_data_t *data, crypto_data_t *sig,
    crypto_req_handle_t req);
int ncp_dsainit(crypto_ctx_t *ctx, crypto_mechanism_t *mechanism,
    crypto_key_t *key, int kmflag, int mode);

int
ncp_dsa_sign(crypto_ctx_t *ctx, crypto_data_t *data, crypto_data_t *sig,
    crypto_req_handle_t req)
{
	ncp_request_t	*reqp = ctx->cc_provider_private;
	ncp_t		*ncp = ctx->cc_provider;
	int		err;
	int		rv = CRYPTO_QUEUED;
	size_t		buflen;

/* EXPORT DELETE START */

	buflen = ncp_length(data);
	if (buflen != DSAPARTLEN) {
		DBG1(ncp, DCHATTY,
		    "ncp_dsa_sign: data length != %d", DSAPARTLEN);
		rv = CRYPTO_DATA_LEN_RANGE;
		goto errout;
	}

	/* Return length needed to store the output. */
	if (ncp_length(sig) < DSASIGLEN) {
		DBG2(ncp, DCHATTY,
		    "ncp_dsa_sign: output buffer too short (%d < %d)",
		    ncp_length(sig), DSASIGLEN);
		sig->cd_length = DSASIGLEN;
		rv = CRYPTO_BUFFER_TOO_SMALL;
		goto errout;
	}

	/*
	 * Don't change the data values of the data crypto_data_t structure
	 * yet. Only reset the sig cd_length to zero before writing to it.
	 */

	reqp->nr_job_stat = DS_DSASIGN;
	reqp->nr_byte_stat = -1;
	reqp->nr_in = data;
	reqp->nr_out = sig;

	reqp->nr_kcf_req = req;

	/*
	 * ncp_gather() increments cd_offset & dec. cd_length by
	 * DSAPARTLEN
	 */
	err = ncp_gather(data, (caddr_t)reqp->nr_inbuf, DSAPARTLEN, 0);
	if (err != CRYPTO_SUCCESS) {
		DBG0(ncp, DWARN, "ncp_dsa_sign: ncp_gather() failed");
		rv = err;
		goto errout;
	}

	reqp->nr_pkt_length = (uint16_t)buflen;
	reqp->nr_callback = ncp_dsa_sign_done;

	/* schedule the work by doing a submit */

	rv = ncp_start(ncp, reqp);
errout:
	if ((rv != CRYPTO_QUEUED) && (rv != CRYPTO_BUFFER_TOO_SMALL))
		(void) ncp_free_context(ctx);

/* EXPORT DELETE END */

	return (rv);
}


static void
ncp_dsa_sign_done(ncp_request_t *reqp, int errno)
{
	int	atomic = reqp->nr_atomic;

/* EXPORT DELETE START */

	/*
	 * Set the sig cd_length to zero so it's ready to take the
	 * signature. Have already confirmed its size is adequate.
	 */
	if (errno == CRYPTO_SUCCESS) {
		reqp->nr_out->cd_length = 0;
		errno = ncp_scatter((caddr_t)reqp->nr_outbuf, reqp->nr_out,
		    DSAPARTLEN, 0);
		if (errno != CRYPTO_SUCCESS) {
			DBG0(reqp->nr_ncp, DWARN,
			    "ncp_dsa_sign_done: ncp_scatter() failed");
			goto errout;
		}

		errno = ncp_scatter((char *)(reqp->nr_outbuf) + DSAPARTLEN,
		    reqp->nr_out, DSAPARTLEN, 0);
		if (errno != CRYPTO_SUCCESS) {
			DBG0(reqp->nr_ncp, DWARN,
			    "ncp_dsa_sign_done: ncp_scatter() failed");
			goto errout;
		}
	}
errout:

	ASSERT(reqp->nr_kcf_req != NULL);

	if ((reqp->nr_kcf_req == NCP_FIPS_POST_REQUEST) ||
	    (reqp->nr_kcf_req == NCP_FIPS_CONSIST_REQUEST)) {
		/* FIPS POST request */
		reqp->nr_ncp->n_fips_post_status = errno;
		mutex_enter(&reqp->nr_ncp->n_fips_post_lock);
		cv_signal(&reqp->nr_ncp->n_fips_post_cv);
		mutex_exit(&reqp->nr_ncp->n_fips_post_lock);
	} else {
#ifdef DEBUG
		ncp_t	*ncp = reqp->nr_ncp;
#endif

		/* notify framework that request is completed */
		crypto_op_notification(reqp->nr_kcf_req, errno);
#ifdef DEBUG
		if (errno != CRYPTO_SUCCESS) {
			DBG1(ncp, DWARN,
			    "ncp_dsa_sign_done: rtn 0x%x to kef via "
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
		crypto_ctx_t ctx;
		ctx.cc_provider_private = reqp;
		ncp_dsactxfree(&ctx);
	}

/* EXPORT DELETE END */

}

int
ncp_dsa_verify(crypto_ctx_t *ctx, crypto_data_t *data, crypto_data_t *sig,
    crypto_req_handle_t req)
{
	ncp_request_t	*reqp = ctx->cc_provider_private;
	ncp_t		*ncp = ctx->cc_provider;
	int		err;
	int		rv = CRYPTO_QUEUED;

/* EXPORT DELETE START */

	/* Impossible for verify to be an in-place operation. */
	if (sig == NULL) {
		rv = CRYPTO_ARGUMENTS_BAD;
		goto errout;
	}

	if (ncp_length(data) != DSAPARTLEN) {
		DBG1(ncp, DCHATTY,
		    "ncp_dsa_verify: input length != %d", DSAPARTLEN);
		rv = CRYPTO_DATA_LEN_RANGE;
		goto errout;
	}

	if (ncp_length(sig) != DSASIGLEN) {
		DBG1(ncp, DCHATTY, "ncp_dsa_verify: signature length != %d",
		    DSASIGLEN);
		rv = CRYPTO_SIGNATURE_LEN_RANGE;
		goto errout;
	}

	/* Don't change the data & sig values for verify. */

	reqp->nr_job_stat = DS_DSAVERIFY;
	reqp->nr_byte_stat = -1;

	/*
	 * Grab h, r and s.
	 */
	err = ncp_gather(data, (char *)(reqp->nr_inbuf), DSAPARTLEN, 0);
	if (err != CRYPTO_SUCCESS) {
		DBG0(ncp, DWARN,
		    "ncp_dsa_vrfy: ncp_gather() failed for h");
		rv = err;
		goto errout;
	}
	err = ncp_gather(sig,
	    (caddr_t)(reqp->nr_inbuf) + DSAPARTLEN, DSAPARTLEN, 0);
	if (err != CRYPTO_SUCCESS) {
		DBG0(ncp, DWARN,
		    "ncp_dsa_vrfy: ncp_gather() failed for r");
		rv = err;
		goto errout;
	}
	err = ncp_gather(sig, (char *)(reqp->nr_inbuf) + 2 * DSAPARTLEN,
	    DSAPARTLEN, 0);
	if (err != CRYPTO_SUCCESS) {
		DBG0(ncp, DWARN,
		    "ncp_dsa_vrfy: ncp_gather() failed for s");
		rv = err;
		goto errout;
	}

	/*
	 * As ncp_gather() increments the cd_offset and decrements
	 * the cd_length as it copies the data rewind the values ready for
	 * the final compare.
	 */
	sig->cd_offset -= (DSAPARTLEN * 2);
	sig->cd_length += (DSAPARTLEN * 2);

	reqp->nr_in = data;
	reqp->nr_out = sig;
	reqp->nr_kcf_req = req;
	reqp->nr_flags |= DR_SCATTER | DR_GATHER;

	reqp->nr_callback = ncp_dsa_verify_done;

	/* schedule the work by doing a submit */
	rv = ncp_start(ncp, reqp);

errout:

	if (rv != CRYPTO_QUEUED && rv != CRYPTO_BUFFER_TOO_SMALL)
		(void) ncp_free_context(ctx);

/* EXPORT DELETE END */

	return (rv);
}


static void
ncp_dsa_verify_done(ncp_request_t *reqp, int errno)
{
	crypto_data_t	*sig = reqp->nr_out;
	caddr_t		daddr;
	int		atomic = reqp->nr_atomic;

/* EXPORT DELETE START */

	if (errno == CRYPTO_SUCCESS) {
		/* Can only handle a contiguous data buffer currently. */
		if (ncp_sgcheck(reqp->nr_ncp, sig, NCP_SG_CONTIG)) {
			errno = CRYPTO_SIGNATURE_INVALID;
			goto errout;
		}

		if ((daddr = ncp_bufdaddr(sig)) == NULL) {
			errno = CRYPTO_ARGUMENTS_BAD;
			goto errout;
		}
		if (bcmp(daddr, reqp->nr_outbuf, DSAPARTLEN) != 0) {
			/* VERIFY FAILED */
			errno = CRYPTO_SIGNATURE_INVALID;
		}
	}
errout:

	ASSERT(reqp->nr_kcf_req != NULL);

	if ((reqp->nr_kcf_req == NCP_FIPS_POST_REQUEST) ||
	    (reqp->nr_kcf_req == NCP_FIPS_CONSIST_REQUEST)) {
		/* FIPS POST request */
		reqp->nr_ncp->n_fips_post_status = errno;
		mutex_enter(&reqp->nr_ncp->n_fips_post_lock);
		cv_signal(&reqp->nr_ncp->n_fips_post_cv);
		mutex_exit(&reqp->nr_ncp->n_fips_post_lock);
	} else {
#ifdef DEBUG
		ncp_t	*ncp = reqp->nr_ncp;
#endif
		/* notify framework that request is completed */
		crypto_op_notification(reqp->nr_kcf_req, errno);
#ifdef DEBUG
		if (errno != CRYPTO_SUCCESS) {
			DBG1(ncp, DWARN,
			    "ncp_dsa_verify_done: rtn 0x%x to kef via "
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
		crypto_ctx_t ctx;
		ctx.cc_provider_private = reqp;
		ncp_dsactxfree(&ctx);
	}

/* EXPORT DELETE END */

}

/* ARGSUSED */
int
ncp_dsainit(crypto_ctx_t *ctx, crypto_mechanism_t *mechanism,
    crypto_key_t *key, int kmflag, int mode)
{
	crypto_object_attribute_t	*attr;
	unsigned			plen = 0, qlen = 0, glen = 0, xlen = 0;
	uchar_t				*p, *q, *g, *x;
	ncp_request_t			*reqp = NULL;
	ncp_t				*ncp = (ncp_t *)ctx->cc_provider;
	int				rv = CRYPTO_SUCCESS;

/* EXPORT DELETE START */

	if ((reqp = ncp_getreq(ncp, 1)) == NULL) {
		ncp_error(ncp,
		    "ncp_dsainit: unable to allocate request for DSA");
		rv = CRYPTO_HOST_MEMORY;
		goto errout;
	}

	ctx->cc_provider_private = reqp;
	reqp->nr_ctx_cm_type = mechanism->cm_type;
	reqp->nr_mode = mode;

	if ((attr = ncp_get_key_attr(key)) == NULL) {
		DBG0(NULL, DWARN, "ncp_dsainit: key attributes missing");
		rv = CRYPTO_KEY_TYPE_INCONSISTENT;
		goto errout;
	}

	/* Prime */
	if (ncp_attr_lookup_uint8_array(attr, key->ck_count, CKA_PRIME,
	    (void *) &p, &plen)) {
		DBG0(NULL, DWARN, "ncp_dsainit: prime key value not present");
		rv = CRYPTO_ARGUMENTS_BAD;
		goto errout;
	}

	/* Subprime */
	if (ncp_attr_lookup_uint8_array(attr, key->ck_count, CKA_SUBPRIME,
	    (void *) &q, &qlen)) {
		DBG0(NULL, DWARN,
		    "ncp_dsainit: subprime key value not present");
		rv = CRYPTO_ARGUMENTS_BAD;
		goto errout;
	}

	/* Base */
	if (ncp_attr_lookup_uint8_array(attr, key->ck_count, CKA_BASE,
	    (void *) &g, &glen)) {
		DBG0(NULL, DWARN, "ncp_dsainit: base key value not present");
		rv = CRYPTO_ARGUMENTS_BAD;
		goto errout;
	}

	/* Value */
	if (ncp_attr_lookup_uint8_array(attr, key->ck_count, CKA_VALUE,
	    (void *) &x, &xlen)) {
		DBG0(NULL, DWARN, "ncp_dsainit: value key not present");
		rv = CRYPTO_ARGUMENTS_BAD;
		goto errout;
	}

	if (plen == 0 || qlen == 0 || glen == 0 || xlen == 0) {
		rv = CRYPTO_ARGUMENTS_BAD;
		goto errout;
	}

	if (plen > DSA_MAX_KEY_LEN) {
		/* maximum 1Kbit key */
		DBG1(NULL, DWARN, "ncp_dsainit: maximum 1Kbit key (%d)", plen);
		rv = CRYPTO_KEY_SIZE_RANGE;
		goto errout;
	}

	if (qlen != DSAPARTLEN) {
		DBG1(NULL, DWARN, "ncp_dsainit: q is too long (%d)", qlen);
		rv = CRYPTO_KEY_SIZE_RANGE;
		goto errout;
	}

	if (mode == NCP_DSA_SIGN && xlen > DSAPARTLEN) {
		DBG1(NULL, DWARN,
		    "ncp_dsainit: private key is too long (%d)", xlen);
		rv = CRYPTO_KEY_SIZE_RANGE;
		goto errout;
	}

	reqp->nr_dsa_plen = plen;
	reqp->nr_dsa_qlen = qlen;
	reqp->nr_dsa_glen = glen;
	reqp->nr_dsa_xylen = xlen;

	bcopy(q, reqp->nr_dsa_q, qlen);
	bcopy(p, reqp->nr_dsa_p, plen);
	bcopy(g, reqp->nr_dsa_g, glen);
	bcopy(x, reqp->nr_dsa_xy, xlen);

	return (CRYPTO_SUCCESS);

errout:

	ncp_dsactxfree(ctx);

/* EXPORT DELETE END */

	return (rv);
}

void
ncp_dsactxfree(void *arg)
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

int
ncp_dsaatomic(crypto_provider_handle_t provider,
    crypto_session_id_t session_id, crypto_mechanism_t *mechanism,
    crypto_key_t *key, crypto_data_t *data, crypto_data_t *sig,
    int kmflag, crypto_req_handle_t req, int mode)
{
	crypto_ctx_t	ctx;	/* on the stack */
	int		rv;

/* EXPORT DELETE START */

	ctx.cc_provider = provider;
	ctx.cc_session = session_id;

	rv = ncp_dsainit(&ctx, mechanism, key, kmflag, mode);
	if (rv != CRYPTO_SUCCESS) {
		DBG0(NULL, DWARN, "ncp_dsaatomic: ncp_dsainit() failed");
		return (rv);
	}

	/*
	 * Set the atomic flag so that the hardware callback function
	 * will free the context.
	 */
	((ncp_request_t *)ctx.cc_provider_private)->nr_atomic = 1;

	if (mode == NCP_DSA_SIGN) {
		rv = ncp_dsa_sign(&ctx, data, sig, req);
	} else {
		ASSERT(mode == NCP_DSA_VRFY);
		rv = ncp_dsa_verify(&ctx, data, sig, req);
	}


/* EXPORT DELETE END */

	return (rv);
}



/*
 * at this point, everything is checked already
 */
int
ncp_dsa_sign_process(ncp_t *ncp, ncp_request_t *reqp)
{
	uint_t glen, xlen, plen, qlen;
	DSAkey dsakey;
	BIGNUM msg, tmp, tmp1, tmp2;
	BIG_ERR_CODE err;
	big_modexp_ncp_info_t	info;

/* EXPORT DELETE START */

	info.func = &ncp_big_modexp_ncp;
	info.ncp = ncp;
	info.reqp = reqp;

	plen = reqp->nr_dsa_plen;
	qlen = reqp->nr_dsa_qlen;
	glen = reqp->nr_dsa_glen;
	xlen = reqp->nr_dsa_xylen;

	if ((err = ncp_DSA_key_init(&dsakey, plen * BITSINBYTE)) != BIG_OK) {
		return (CRYPTO_HOST_MEMORY);
	}
	if ((err = ncp_big_init(&msg, BIG_CHUNKS_FOR_160BITS)) != BIG_OK) {
		goto ret1;
	}
	if ((err = ncp_big_init(&tmp, plen / sizeof (BIG_CHUNK_TYPE) +
	    2 * BIG_CHUNKS_FOR_160BITS + 1)) != BIG_OK) {
		goto ret2;
	}
	if ((err = ncp_big_init(&tmp1,  2 * BIG_CHUNKS_FOR_160BITS + 1)) !=
	    BIG_OK) {
		goto ret3;
	}
	if ((err = ncp_big_init(&tmp2, BIG_CHUNKS_FOR_160BITS)) != BIG_OK) {
		goto ret4;
	}

	ncp_kcl2bignum(&(dsakey.g), reqp->nr_dsa_g, glen);
	ncp_kcl2bignum(&(dsakey.x), reqp->nr_dsa_xy, xlen);
	ncp_kcl2bignum(&(dsakey.p), reqp->nr_dsa_p, plen);
	ncp_kcl2bignum(&(dsakey.q), reqp->nr_dsa_q, qlen);
	ncp_kcl2bignum(&msg, reqp->nr_inbuf, DSAPARTLEN);

	if ((err = ncp_randombignum(&(dsakey.k),
	    DSAPARTLEN * BITSINBYTE)) != BIG_OK) {
		goto ret;
	}

	if ((err = ncp_big_div_pos(NULL,
	    &(dsakey.k), &(dsakey.k), &(dsakey.q))) != BIG_OK) {
		goto ret;
	}
	if ((err = ncp_big_modexp(&tmp, &(dsakey.g), &(dsakey.k),
	    &(dsakey.p), NULL, &info)) != BIG_OK) {
		goto ret;
	}
	if ((err = ncp_big_div_pos(NULL, &(dsakey.r), &tmp, &(dsakey.q))) !=
	    BIG_OK) {
		goto ret;
	}
	if ((err = ncp_big_ext_gcd_pos(NULL, NULL, &tmp,
	    &(dsakey.q), &(dsakey.k))) != BIG_OK) {
		goto ret;
	}
	if (tmp.sign == -1) {
		if ((err = ncp_big_add(&tmp, &tmp, &(dsakey.q))) != BIG_OK) {
			goto ret;			/* tmp <- k^-1 */
		}
	}

	if ((err = ncp_big_mul(&tmp1, &(dsakey.x), &(dsakey.r))) != BIG_OK) {
		goto ret;
	}
	if ((err = ncp_big_add(&tmp1, &tmp1, &msg)) != BIG_OK) {
		goto ret;
	}
	if ((err = ncp_big_mul(&tmp, &tmp1, &tmp)) != BIG_OK) {
		goto ret;
	}
	if ((err = ncp_big_div_pos(NULL, &(dsakey.s), &tmp, &(dsakey.q))) !=
	    BIG_OK) {
		goto ret;
	}

	ncp_bignum2kcl((uchar_t *)(reqp->nr_outbuf), &(dsakey.r), DSAPARTLEN);
	ncp_bignum2kcl((uchar_t *)(reqp->nr_outbuf) + DSAPARTLEN,
	    &(dsakey.s), DSAPARTLEN);

	err = BIG_OK;
ret:
	ncp_big_finish(&tmp2);
ret4:
	ncp_big_finish(&tmp1);
ret3:
	ncp_big_finish(&tmp);
ret2:
	ncp_big_finish(&msg);
ret1:
	ncp_DSA_key_finish(&dsakey);

	switch (err) {
	case BIG_OK:
		err = CRYPTO_SUCCESS;
		break;
	case BIG_NO_MEM:
		err = CRYPTO_HOST_MEMORY;
		break;
	case EWOULDBLOCK:
		err = CRYPTO_BUSY;
		break;
	default:
		err = CRYPTO_FAILED;
		break;
	}

/* EXPORT DELETE END */

	return (err);
}


/*
 * at this point, everything is checked already
 */
int
ncp_dsa_verify_process(ncp_t *ncp, ncp_request_t *reqp)
{
	uint_t glen, ylen, plen, qlen;
	DSAkey dsakey;
	BIGNUM msg, tmp1, tmp2, tmp3;
	BIG_ERR_CODE err;
	big_modexp_ncp_info_t	info;

/* EXPORT DELETE START */

	info.func = &ncp_big_modexp_ncp;
	info.ncp = ncp;
	info.reqp = reqp;

	plen = reqp->nr_dsa_plen;
	qlen = reqp->nr_dsa_qlen;
	glen = reqp->nr_dsa_glen;
	ylen = reqp->nr_dsa_xylen;

	ASSERT(qlen == DSAPARTLEN);

	if ((err = ncp_DSA_key_init(&dsakey, plen * BITSINBYTE)) != BIG_OK) {
		return (CRYPTO_HOST_MEMORY);
	}
	if ((err = ncp_big_init(&msg, BIG_CHUNKS_FOR_160BITS)) != BIG_OK) {
		goto ret1;
	}
	if ((err = ncp_big_init(&tmp1,
	    2 * plen / sizeof (BIG_CHUNK_TYPE))) != BIG_OK) {
		goto ret2;
	}
	if ((err = ncp_big_init(&tmp2, plen / sizeof (BIG_CHUNK_TYPE))) !=
	    BIG_OK) {
		goto ret3;
	}
	if ((err = ncp_big_init(&tmp3, 2 * BIG_CHUNKS_FOR_160BITS)) !=
	    BIG_OK) {
		goto ret4;
	}

	ncp_kcl2bignum(&(dsakey.g), reqp->nr_dsa_g, glen);
	ncp_kcl2bignum(&(dsakey.y), reqp->nr_dsa_xy, ylen);
	ncp_kcl2bignum(&(dsakey.p), reqp->nr_dsa_p, plen);
	ncp_kcl2bignum(&(dsakey.q), reqp->nr_dsa_q, qlen);
	ncp_kcl2bignum(&msg, (uchar_t *)(reqp->nr_inbuf), DSAPARTLEN);
	ncp_kcl2bignum(&(dsakey.r),
	    (uchar_t *)(reqp->nr_inbuf) + DSAPARTLEN, DSAPARTLEN);
	ncp_kcl2bignum(&(dsakey.s),
	    (uchar_t *)(reqp->nr_inbuf) + 2 * DSAPARTLEN, DSAPARTLEN);

	if (ncp_big_is_zero(&(dsakey.s))) {
		err = BIG_INVALID_ARGS;
		goto ret;
	}

	if ((err = ncp_big_ext_gcd_pos(NULL, &tmp2, NULL,
	    &(dsakey.s), &(dsakey.q))) != BIG_OK) {
		goto ret;
	}
	if (tmp2.sign == -1) {
		if ((err = ncp_big_add(&tmp2, &tmp2, &(dsakey.q))) !=
		    BIG_OK) {
			goto ret;			/* tmp2 <- w */
		}
	}
	if ((err = ncp_big_mul(&tmp1, &msg, &tmp2)) != BIG_OK) {
		goto ret;
	}
	if ((err = ncp_big_div_pos(NULL, &tmp1, &tmp1, &(dsakey.q))) !=
	    BIG_OK) {
		goto ret;				/* tmp1 <- u_1 */
	}
	if ((err = ncp_big_mul(&tmp2, &tmp2, &(dsakey.r))) != BIG_OK) {
		goto ret;
	}
	if ((err = ncp_big_div_pos(NULL, &tmp2, &tmp2, &(dsakey.q))) !=
	    BIG_OK) {
		goto ret;				/* tmp2 <- u_2 */
	}
	if ((err = ncp_big_modexp(&tmp1,
	    &(dsakey.g), &tmp1, &(dsakey.p), NULL, &info)) != BIG_OK) {
		goto ret;
	}
	if ((err = ncp_big_modexp(&tmp2,
	    &(dsakey.y), &tmp2, &(dsakey.p), NULL, &info)) != BIG_OK) {
		goto ret;
	}
	if ((err = ncp_big_mul(&tmp1, &tmp1, &tmp2)) != BIG_OK) {
		goto ret;
	}
	if ((err = ncp_big_div_pos(NULL, &tmp1, &tmp1, &(dsakey.p))) !=
	    BIG_OK) {
		goto ret;
	}
	if ((err = ncp_big_div_pos(NULL, &tmp1, &tmp1, &(dsakey.q))) !=
	    BIG_OK) {
		goto ret;
	}

	ncp_bignum2kcl((uchar_t *)(reqp->nr_outbuf), &(tmp1), DSAPARTLEN);

ret:
	ncp_big_finish(&tmp3);
ret4:
	ncp_big_finish(&tmp2);
ret3:
	ncp_big_finish(&tmp1);
ret2:
	ncp_big_finish(&msg);
ret1:
	ncp_DSA_key_finish(&dsakey);

	switch (err) {
	case BIG_OK:
		err = CRYPTO_SUCCESS;
		break;
	case BIG_NO_MEM:
		err = CRYPTO_HOST_MEMORY;
		break;
	case BIG_INVALID_ARGS:
		err = CRYPTO_SIGNATURE_INVALID;
		break;
	case EWOULDBLOCK:
		err = CRYPTO_BUSY;
		break;
	default:
		err = CRYPTO_FAILED;
		break;
	}

/* EXPORT DELETE END */

	return (err);
}
