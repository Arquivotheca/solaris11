/*
 * Copyright (c) 2005, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/types.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/kmem.h>
#include <sys/note.h>
#include <sys/crypto/common.h>
#include <sys/crypto/spi.h>
#include <sys/random.h>
#include <sys/ncp.h>

boolean_t ncp_bignum_to_attr(crypto_attr_type_t, crypto_object_attribute_t *,
    uint_t, BIGNUM *, uint32_t);

/* Exported function prototypes */
int ncp_rsastart(crypto_ctx_t *, crypto_data_t *, crypto_data_t *,
    crypto_req_handle_t, int);
int ncp_rsainit(crypto_ctx_t *, crypto_mechanism_t *, crypto_key_t *, int);
void ncp_rsactxfree(void *);
int ncp_rsaatomic(crypto_provider_handle_t, crypto_session_id_t,
    crypto_mechanism_t *, crypto_key_t *, crypto_data_t *, crypto_data_t *,
    int, crypto_req_handle_t, int);
int ncp_rsa_generate_key(crypto_provider_handle_t, crypto_session_id_t,
    crypto_mechanism_t *, crypto_object_attribute_t *, uint_t,
    crypto_object_attribute_t *, uint_t, crypto_object_attribute_t *,
    uint_t, crypto_object_attribute_t *, uint_t, int, crypto_req_handle_t);

/* Local function prototypes */
static void ncp_rsaverifydone(ncp_request_t *, int errno);
static void ncp_rsagendone(ncp_request_t *, int errno);
static void ncp_rsadone(ncp_request_t *, int errno);
static void rsa_callback(ncp_request_t *reqp, int rv);
static int generate_rsa_key(int, int, BIGNUM *,  ncp_t *, ncp_request_t *);

static int ncp_pkcs1_padding(ncp_t *ncp, caddr_t buf, int flen, int tlen,
    int mode);
static int ncp_pkcs1_unpadding(char *buf, int *tlen, int flen, int mode);
static int ncp_x509_padding(caddr_t buf, int flen, int tlen);
static int ncp_x509_unpadding(char *buf, int tlen, int flen, int mode);
static int decrypt_error_code(int mode, int decrypt, int verify, int def);

static uchar_t default_pub_exp[3] = { 0x01, 0x00, 0x01 };

int
ncp_rsastart(crypto_ctx_t *ctx, crypto_data_t *in, crypto_data_t *out,
    crypto_req_handle_t req, int mode)
{
	ncp_request_t		*reqp = ctx->cc_provider_private;
	ncp_t			*ncp = ctx->cc_provider;
	caddr_t			daddr;
	int			rv = CRYPTO_QUEUED;
	int			len;

/* EXPORT DELETE START */

	/*
	 * In-place operations (in == out) are indicated by having a
	 * NULL output. In this case set the out to point to the in.
	 * Note that this only works for CKM_RSA_X_509 without any padding
	 */
	if (!out) {
		DBG0(ncp, DWARN, "Using inline since output buffer is NULL.");
		out = in;
	}

	/* We don't support non-contiguous buffers for RSA */
	if (ncp_sgcheck(ncp, in, NCP_SG_CONTIG) ||
	    ncp_sgcheck(ncp, out, NCP_SG_CONTIG)) {
		rv = CRYPTO_NOT_SUPPORTED;
		goto errout;
	}

	reqp->nr_inlen = len = ncp_length(in);

	if (mode == NCP_RSA_ENC || mode == NCP_RSA_SIGN ||
	    mode == NCP_RSA_SIGNR) {
		/*
		 * Return length needed to store the output.
		 * For sign, sign-recover, and encrypt, the output buffer
		 * should not be smaller than modlen since PKCS or X_509
		 * padding will be applied
		 */
		if (ncp_length(out) < reqp->nr_rsa_modlen) {
			DBG2(ncp, DCHATTY,
			    "ncp_rsastart: output buffer too short (%d < %d)",
			    ncp_length(out),
			    reqp->nr_rsa_modlen);
			out->cd_length = reqp->nr_rsa_modlen;
			rv = CRYPTO_BUFFER_TOO_SMALL;
			goto errout;
		}

	}
	if (out != in && out->cd_length > reqp->nr_rsa_modlen)
		out->cd_length = reqp->nr_rsa_modlen;

	/* The input length should not be bigger than the modulus */
	if (len > reqp->nr_rsa_modlen) {
		rv = decrypt_error_code(mode, CRYPTO_ENCRYPTED_DATA_LEN_RANGE,
		    CRYPTO_SIGNATURE_LEN_RANGE, CRYPTO_DATA_LEN_RANGE);
		goto errout;
	}

	/*
	 * For decryption, verify, and verifyRecover, the input length should
	 * not be less than the modulus
	 */
	if (len < reqp->nr_rsa_modlen && (mode == NCP_RSA_DEC ||
	    mode == NCP_RSA_VRFY || mode == NCP_RSA_VRFYR)) {
		rv = decrypt_error_code(mode, CRYPTO_ENCRYPTED_DATA_LEN_RANGE,
		    CRYPTO_SIGNATURE_LEN_RANGE, CRYPTO_DATA_LEN_RANGE);
		goto errout;
	}

	/*
	 * For decryption and verifyRecover, the output buffer should not
	 * be less than the modulus
	 */
	if (out->cd_length < reqp->nr_rsa_modlen &&
	    (mode == NCP_RSA_DEC || mode == NCP_RSA_VRFYR) &&
	    reqp->nr_ctx_cm_type == RSA_X_509_MECH_INFO_TYPE) {
		out->cd_length = reqp->nr_rsa_modlen;
		rv = CRYPTO_BUFFER_TOO_SMALL;
		goto errout;
	}

	/* For decrypt and verify, the input should not be less than output */
	if (out && len < out->cd_length) {
		if ((rv = decrypt_error_code(mode,
		    CRYPTO_ENCRYPTED_DATA_LEN_RANGE,
		    CRYPTO_SIGNATURE_LEN_RANGE, CRYPTO_SUCCESS)) !=
		    CRYPTO_SUCCESS)
			goto errout;
	}

	if ((daddr = ncp_bufdaddr(in)) == NULL && len > 0) {
		rv = CRYPTO_ARGUMENTS_BAD;
		goto errout;
	}

	if (ncp_numcmp(daddr, len, (char *)reqp->nr_rsa_mod_orig,
	    reqp->nr_rsa_modlen) > 0) {
		DBG0(ncp, DWARN,
		    "ncp_rsastart: input larger (numerically) than modulus!");
		rv = decrypt_error_code(mode, CRYPTO_ENCRYPTED_DATA_INVALID,
		    CRYPTO_SIGNATURE_INVALID, CRYPTO_DATA_INVALID);
		goto errout;
	}

	reqp->nr_byte_stat = -1;
	reqp->nr_in = in;
	reqp->nr_out = out;
	reqp->nr_kcf_req = req;

	/*
	 * Public key use reqp->nr_rsa_[exp, mod]
	 *  and reqp->nr_inbuf.
	 * Private key use reqp->nr_rsa_[exp, p, q, dp, dq, pinv]
	 *  and reqp->nr_inbuf.
	 */

	bcopy(daddr, reqp->nr_inbuf, len);
	if (mode == NCP_RSA_ENC || mode == NCP_RSA_SIGN ||
	    mode == NCP_RSA_SIGNR) {
		/*
		 * Needs to pad appropriately for encrypt, sign, and
		 * sign_recover
		 */
		if (reqp->nr_ctx_cm_type == RSA_PKCS_MECH_INFO_TYPE) {
			if ((rv = ncp_pkcs1_padding(ncp,
			    (caddr_t)reqp->nr_inbuf,
			    len, reqp->nr_pkt_length,
			    mode)) !=
			    CRYPTO_SUCCESS)
				goto errout;
		} else if (reqp->nr_ctx_cm_type == RSA_X_509_MECH_INFO_TYPE) {
			if ((rv = ncp_x509_padding((caddr_t)reqp->nr_inbuf,
			    len, reqp->nr_pkt_length)) != CRYPTO_SUCCESS)
				goto errout;
		}
		reqp->nr_inlen = reqp->nr_pkt_length;
	}

	reqp->nr_mode = mode;
	reqp->nr_callback = rsa_callback;

	/* schedule the work by doing a submit */
	rv = ncp_start(ncp, reqp);

errout:

	if (rv != CRYPTO_QUEUED && rv != CRYPTO_BUFFER_TOO_SMALL)
		(void) ncp_free_context(ctx);

/* EXPORT DELETE END */

	return (rv);
}

static void
rsa_callback(ncp_request_t *reqp, int rv)
{

/* EXPORT DELETE START */

	if (reqp->nr_mode == NCP_RSA_GEN) {
		ncp_rsagendone(reqp, rv);
	} else if (reqp->nr_mode == NCP_RSA_VRFY) {
		ncp_rsaverifydone(reqp, rv);
	} else {
		ncp_rsadone(reqp, rv);
	}

/* EXPORT DELETE END */

}

static void
ncp_rsagendone(ncp_request_t *reqp, int errno)
{

/* EXPORT DELETE START */
	ASSERT(reqp->nr_kcf_req != NULL);

	reqp->nr_keygen_status = errno;
	mutex_enter(&reqp->nr_keygen_lock);
	cv_signal(&reqp->nr_keygen_cv);
	mutex_exit(&reqp->nr_keygen_lock);

/* EXPORT DELETE END */

}

static void
ncp_rsadone(ncp_request_t *reqp, int errno)
{
	int	outsz = reqp->nr_out->cd_length;
	caddr_t	daddr;
	int	atomic = reqp->nr_atomic;

/* EXPORT DELETE START */

	if (errno == CRYPTO_SUCCESS) {
		if (reqp->nr_mode == NCP_RSA_DEC ||
		    reqp->nr_mode == NCP_RSA_VRFY ||
		    reqp->nr_mode == NCP_RSA_VRFYR) {
			/*
			 * Needs to unpad appropriately for decrypt, verify,
			 * and verify_recover
			 */
			if (reqp->nr_ctx_cm_type == RSA_PKCS_MECH_INFO_TYPE) {
				if ((errno = ncp_pkcs1_unpadding(
				    (caddr_t)reqp->nr_outbuf, &outsz,
				    reqp->nr_pkt_length, reqp->nr_mode)) !=
				    CRYPTO_SUCCESS) {
					if (errno == CRYPTO_BUFFER_TOO_SMALL) {
						reqp->nr_out->cd_length = outsz;
					}
					goto errout;
				}
				/* Reset the output data length */
				reqp->nr_out->cd_length = outsz;
			} else if (reqp->nr_ctx_cm_type ==
			    RSA_X_509_MECH_INFO_TYPE) {
				if ((errno = ncp_x509_unpadding(
				    (caddr_t)reqp->nr_outbuf, outsz,
				    reqp->nr_pkt_length, reqp->nr_mode)) !=
				    CRYPTO_SUCCESS)
					goto errout;
			}
		}

		if ((daddr = ncp_bufdaddr(reqp->nr_out)) == NULL) {
			DBG0(reqp->nr_ncp, DWARN,
			    "ncp_rsadone: reqp->nr_out is bad");
			errno = CRYPTO_ARGUMENTS_BAD;
			goto errout;
		}
		/*
		 * Note that there may be some number of null bytes
		 * at the end of the source (result), but we don't care
		 * about them -- they are place holders only and are
		 * truncated here.
		 */
		bcopy(reqp->nr_outbuf, daddr, outsz);
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
#ifdef	DEBUG
		ncp_t	*ncp = reqp->nr_ncp;
#endif

		/* notify framework that request is completed */
		crypto_op_notification(reqp->nr_kcf_req, errno);
#ifdef DEBUG
		if (errno != CRYPTO_SUCCESS) {
			DBG1(ncp, DCHATTY,
			    "ncp_rsaverifydone: rtn 0x%x to the kef via "
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
		ncp_rsactxfree(&ctx);
	}

/* EXPORT DELETE END */

}

static void
ncp_rsaverifydone(ncp_request_t *reqp, int errno)
{
	char	scratch[RSA_MAX_KEY_LEN];
	int	outsz = reqp->nr_out->cd_length;
	caddr_t	daddr;
	int	atomic = reqp->nr_atomic;

/* EXPORT DELETE START */

	if (errno == CRYPTO_SUCCESS) {
		if (reqp->nr_mode == NCP_RSA_DEC ||
		    reqp->nr_mode == NCP_RSA_VRFY ||
		    reqp->nr_mode == NCP_RSA_VRFYR) {
			/*
			 * Needs to unpad appropriately for decrypt, verify,
			 * and verify_recover
			 */
			if (reqp->nr_ctx_cm_type ==
			    RSA_PKCS_MECH_INFO_TYPE) {
				if ((errno = ncp_pkcs1_unpadding(
				    (caddr_t)reqp->nr_outbuf, &outsz,
				    reqp->nr_pkt_length, reqp->nr_mode)) !=
				    CRYPTO_SUCCESS)
					goto errout;
				/* Reset the output data length */
				reqp->nr_out->cd_length = outsz;
			} else if (reqp->nr_ctx_cm_type ==
			    RSA_X_509_MECH_INFO_TYPE) {
				if ((errno = ncp_x509_unpadding(
				    (caddr_t)reqp->nr_outbuf, outsz,
				    reqp->nr_pkt_length, reqp->nr_mode)) !=
				    CRYPTO_SUCCESS)
					goto errout;
			}
		}

		bcopy(reqp->nr_outbuf, scratch, outsz);

		if ((daddr = ncp_bufdaddr(reqp->nr_out)) == NULL) {
			errno = CRYPTO_ARGUMENTS_BAD;
			goto errout;
		}
		if (ncp_numcmp(daddr, reqp->nr_out->cd_length, scratch, outsz)
		    != 0) {
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
#ifdef	DEBUG
		ncp_t	*ncp = reqp->nr_ncp;
#endif
		/* notify framework that request is completed */
		crypto_op_notification(reqp->nr_kcf_req, errno);
#ifdef DEBUG
		if (errno != CRYPTO_SUCCESS) {
			DBG1(ncp, DCHATTY,
			    "ncp_rsaverifydone: rtn 0x%x to the kef via "
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
		ncp_rsactxfree(&ctx);
	}

/* EXPORT DELETE END */

}

/*
 * Setup either a public or a private RSA key for subsequent uses
 */
/* ARGSUSED */
int
ncp_rsainit(crypto_ctx_t *ctx, crypto_mechanism_t *mechanism,
    crypto_key_t *key, int kmflag)
{
	crypto_object_attribute_t	*attr;
	unsigned			expname = 0;
	void				*mod;
	int rv;

	uchar_t			*exp;
	uchar_t			*p;
	uchar_t			*q;
	uchar_t			*dp;
	uchar_t			*dq;
	uchar_t			*pinv;

	unsigned		explen = 0;
	unsigned		plen = 0;
	unsigned		qlen = 0;
	unsigned		dplen = 0;
	unsigned		dqlen = 0;
	unsigned		pinvlen = 0;
	/* unsigned		modbits; */

	ncp_request_t		*reqp = NULL;
	ncp_t			*ncp = (ncp_t *)ctx->cc_provider;

/* EXPORT DELETE START */

	DBG0(NULL, DENTRY, "ncp_rsainit: start");

	if ((reqp = ncp_getreq(ncp, 1)) == NULL) {
		DBG0(NULL, DWARN,
		    "ncp_rsainit: unable to allocate request for RSA");
		rv = CRYPTO_HOST_MEMORY;
		goto errout;
	}

	reqp->nr_ctx_cm_type = mechanism->cm_type;
	ctx->cc_provider_private = reqp;

	/*
	 * Key type can be either RAW, or REFERENCE, or ATTR_LIST (VALUE).
	 * Only ATTR_LIST is supported for RSA.
	 */
	if ((attr = ncp_get_key_attr(key)) == NULL) {
		DBG0(NULL, DWARN, "ncp_rsainit: key attributes missing");
		rv = CRYPTO_KEY_TYPE_INCONSISTENT;
		goto errout;
	}

	if (ncp_find_attribute(attr, key->ck_count, CKA_PUBLIC_EXPONENT))
		expname = CKA_PUBLIC_EXPONENT;

	/*
	 * RSA public key has only public exponent. RSA private key must have
	 * private exponent. However, it may also have public exponent.
	 * Thus, the existance of a private exponent indicates a private key.
	 */
	if (ncp_find_attribute(attr, key->ck_count, CKA_PRIVATE_EXPONENT))
		expname = CKA_PRIVATE_EXPONENT;

	if (!expname) {
		DBG0(NULL, DWARN, "ncp_rsainit: no exponent in key");
		rv = CRYPTO_ARGUMENTS_BAD;
		goto errout;
	}

	/* Modulus */
	if ((rv = ncp_attr_lookup_uint8_array(attr, key->ck_count,
	    CKA_MODULUS, &mod, &(reqp->nr_rsa_modlen))) !=
	    CRYPTO_SUCCESS) {
		DBG0(NULL, DWARN, "ncp_rsainit: failed to retrieve modulus");
		goto errout;
	}

	if ((reqp->nr_rsa_modlen == 0) ||
	    (reqp->nr_rsa_modlen > RSA_MAX_KEY_LEN)) {
		DBG0(NULL, DWARN, "ncp_rsainit: bad modulus size");
		rv = CRYPTO_ARGUMENTS_BAD;
		goto errout;
	}

	/* save the modulus */
	bcopy(mod, reqp->nr_rsa_mod_orig, reqp->nr_rsa_modlen);

	/* Exponent */
	if ((rv = ncp_attr_lookup_uint8_array(attr, key->ck_count, expname,
	    (void **) &exp, &explen)) != CRYPTO_SUCCESS) {
		DBG0(NULL, DWARN, "ncp_rsainit: failed to retrieve exponent");
		goto errout;
	}
	if ((explen == 0) || (explen > RSA_MAX_KEY_LEN)) {
		DBG0(NULL, DWARN, "ncp_rsainit: bad exponent size");
		rv = CRYPTO_ARGUMENTS_BAD;
		goto errout;
	}

	/* Lookup private attributes */
	if (expname == CKA_PRIVATE_EXPONENT) {
		/* Prime 1 */
		(void) ncp_attr_lookup_uint8_array(attr, key->ck_count,
		    CKA_PRIME_1, (void **)&q, &qlen);

		/* Prime 2 */
		(void) ncp_attr_lookup_uint8_array(attr, key->ck_count,
		    CKA_PRIME_2, (void **)&p, &plen);

		/* Exponent 1 */
		(void) ncp_attr_lookup_uint8_array(attr, key->ck_count,
		    CKA_EXPONENT_1, (void **)&dq, &dqlen);

		/* Exponent 2 */
		(void) ncp_attr_lookup_uint8_array(attr, key->ck_count,
		    CKA_EXPONENT_2, (void **)&dp, &dplen);

		/* Coefficient */
		(void) ncp_attr_lookup_uint8_array(attr, key->ck_count,
		    CKA_COEFFICIENT, (void **)&pinv, &pinvlen);
	}

	if (plen) {
		reqp->nr_job_stat = DS_RSAPRIVATE;
	} else {
		reqp->nr_job_stat = DS_RSAPUBLIC;
	}

	reqp->nr_pkt_length = reqp->nr_rsa_modlen;

	/* save the parameter lengths */
	reqp->nr_rsa_explen = explen;
	reqp->nr_rsa_plen = plen;

	if (plen) {
		/* save the remaining components for RSA private key */
		reqp->nr_rsa_plen = plen;
		reqp->nr_rsa_qlen = qlen;
		reqp->nr_rsa_dplen = dplen;
		reqp->nr_rsa_dqlen = dqlen;
		reqp->nr_rsa_pinvlen = pinvlen;

		bcopy(p, reqp->nr_rsa_p, plen);
		bcopy(q, reqp->nr_rsa_q, qlen);
		bcopy(dp, reqp->nr_rsa_dp, dplen);
		bcopy(dq, reqp->nr_rsa_dq, dqlen);
		bcopy(pinv, reqp->nr_rsa_pinv, pinvlen);
	} else {
		/* save mod and exp for RSA public key */
		bcopy(mod, reqp->nr_rsa_mod,
		    reqp->nr_rsa_modlen);
		bcopy(exp, reqp->nr_rsa_exp,
		    reqp->nr_rsa_explen);
	}

errout:
	if (rv != CRYPTO_SUCCESS)
		ncp_rsactxfree(ctx);

/* EXPORT DELETE END */

	return (rv);
}

void
ncp_rsactxfree(void *arg)
{
	crypto_ctx_t	*ctx = (crypto_ctx_t *)arg;
	ncp_request_t	*reqp = ctx->cc_provider_private;

/* EXPORT DELETE START */

	if (reqp == NULL)
		return;

	reqp->nr_mode = 0;
	reqp->nr_ctx_cm_type = 0;
	reqp->nr_rsa_modlen = 0;
	reqp->nr_rsa_plen = 0;
	reqp->nr_atomic = 0;

	kmem_cache_free(reqp->nr_ncp->n_request_cache, reqp);

	ctx->cc_provider_private = NULL;

/* EXPORT DELETE END */

}

int
ncp_rsaatomic(crypto_provider_handle_t provider,
    crypto_session_id_t session_id, crypto_mechanism_t *mechanism,
    crypto_key_t *key, crypto_data_t *input, crypto_data_t *output,
    int kmflag, crypto_req_handle_t req, int mode)
{
	crypto_ctx_t	ctx;	/* on the stack */
	int		rv;

/* EXPORT DELETE START */

	ctx.cc_provider = provider;
	ctx.cc_session = session_id;

	rv = ncp_rsainit(&ctx, mechanism, key, kmflag);
	if (rv != CRYPTO_SUCCESS) {
		DBG0(NULL, DWARN, "ncp_rsaatomic: ncp_rsainit() failed");
		/* The content of ctx should have been freed already */
		return (rv);
	}

	/*
	 * Set the atomic flag so that the hardware callback function
	 * will free the context.
	 */
	((ncp_request_t *)ctx.cc_provider_private)->nr_atomic = 1;

	rv = ncp_rsastart(&ctx, input, output, req, mode);

/* EXPORT DELETE END */

	return (rv);
}

/* ARGSUSED */
int
ncp_rsa_generate_key(crypto_provider_handle_t provider,
    crypto_session_id_t session_id, crypto_mechanism_t *mechanism,
    crypto_object_attribute_t *in_pub_attrs, uint_t in_pub_attr_count,
    crypto_object_attribute_t *in_pri_attrs, uint_t in_pri_attr_count,
    crypto_object_attribute_t *out_pub_attrs, uint_t out_pub_attr_count,
    crypto_object_attribute_t *out_pri_attrs, uint_t out_pri_attr_count,
    int kmflag, crypto_req_handle_t req)
{
	ncp_t *ncp = (ncp_t *)provider;
	ncp_request_t *reqp;
	crypto_ctx_t ctx; /* on the stack */
	int rv;
	uint32_t len = 0, pub_explen = 0;
	ulong_t modulus_len;
	ulong_t *mp;
	uchar_t *exp;

/* EXPORT DELETE START */

	if ((rv = ncp_attr_lookup_uint8_array(in_pub_attrs, in_pub_attr_count,
	    CKA_MODULUS_BITS, (void **)&mp, &len)) != CRYPTO_SUCCESS) {
		DBG0(NULL, DWARN, "ncp_rsa_generate_key: "
		    "failed to retrieve modulus bits");
		goto errout;
	}

	if (len == 0) {
		DBG0(NULL, DWARN, "ncp_rsa_generate_key: "
		    "failed to retrieve modulus bits");
		rv = CRYPTO_ARGUMENTS_BAD;
		goto errout;
	}
	modulus_len = 0;
	bcopy(mp, ((caddr_t)&modulus_len) + (sizeof (modulus_len) - len), len);

	/* Convert modulus length from bit length to byte length. */
	modulus_len = CRYPTO_BITS2BYTES(modulus_len);

	/* Modulus length needs to be between min key size and max key size. */
	if ((modulus_len < RSA_MIN_KEY_LEN) ||
	    (modulus_len > RSA_MAX_KEY_LEN)) {
		rv = CRYPTO_ATTRIBUTE_VALUE_INVALID;
		goto errout;
	}

	/* Public Exponent */
	if (ncp_find_attribute(in_pub_attrs, in_pub_attr_count,
	    CKA_PUBLIC_EXPONENT)) {
		if ((rv = ncp_attr_lookup_uint8_array(in_pub_attrs,
		    in_pub_attr_count, CKA_PUBLIC_EXPONENT, (void **)&exp,
		    &pub_explen)) != CRYPTO_SUCCESS) {
			DBG0(NULL, DWARN, "ncp_rsa_generate_key: "
			    "failed to retrieve public exponent");
			goto errout;
		}
		if ((pub_explen == 0) || (pub_explen > RSA_MAX_KEY_LEN)) {
			DBG0(NULL, DWARN, "ncp_rsa_generate_key: "
			    "bad exponent size");
			rv = CRYPTO_ARGUMENTS_BAD;
			goto errout;
		}
	} else {
		exp = default_pub_exp;
		pub_explen = sizeof (default_pub_exp);
	}
	if ((reqp = ncp_getreq(ncp, 1)) == NULL) {
		DBG0(NULL, DWARN,
		    "ncp_rsa_generate_key: unable to allocate request");
		rv = CRYPTO_HOST_MEMORY;
		goto errout;
	}

	/* Public Exponent */
	bcopy(exp, reqp->nr_rsa_exp, pub_explen);
	reqp->nr_rsa_explen = pub_explen;

	/* Modulus Length */
	reqp->nr_rsa_modlen = (uint_t)modulus_len;

	reqp->nr_public_attrs = out_pub_attrs;
	reqp->nr_public_attrs_count = out_pub_attr_count;
	reqp->nr_private_attrs = out_pri_attrs;
	reqp->nr_private_attrs_count = out_pri_attr_count;
	reqp->nr_ctx_cm_type = mechanism->cm_type;
	reqp->nr_kcf_req = req;
	reqp->nr_mode = NCP_RSA_GEN;
	reqp->nr_job_stat = DS_RSAGEN;
	reqp->nr_callback = rsa_callback;


	/* schedule the work by doing a submit */
	mutex_enter(&reqp->nr_keygen_lock);
	rv = ncp_start(ncp, reqp);
	if (rv == CRYPTO_QUEUED) {
		/* wait for the job to be completed. */
		cv_wait(&reqp->nr_keygen_cv, &reqp->nr_keygen_lock);
		rv = reqp->nr_keygen_status;
	}
	mutex_exit(&reqp->nr_keygen_lock);

	/* if the key generation is successful, test the key pair */
	if (rv == CRYPTO_SUCCESS) {
		mutex_enter(&ncp->n_fips_consistency_lock);
		rv = ncp_rsa_pairwise_consist_test(ncp,
		    exp, pub_explen,
		    out_pub_attrs, out_pub_attr_count,
		    out_pri_attrs, out_pri_attr_count);
		if (rv != CRYPTO_SUCCESS) {
			cmn_err(CE_WARN,
			    "ncp: FIPS140-2: RSA Key Gen: Pair-wise "
			    "Consistency test failed");
		}
		mutex_exit(&ncp->n_fips_consistency_lock);
	}

	ctx.cc_provider_private = reqp;
	ncp_rsactxfree(&ctx);
errout:

/* EXPORT DELETE END */

	return (rv);
}

/*
 * For RSA_PKCS padding and unpadding
 * 1. The minimum padding is 11 bytes.
 * 2. The first and the last bytes must 0.
 * 3. The second byte is 1 for signature and 2 for encrypt or wrap.
 * 4. Pad with 0xff for signature and non-zero random for encrypt or wrap.
 */
/* ARGSUSED */
static int
ncp_pkcs1_padding(ncp_t *ncp, char *buf, int flen, int tlen, int mode)
{
	int i;

/* EXPORT DELETE START */

	if (flen > tlen - 11)
		return (CRYPTO_DATA_LEN_RANGE);

	for (i = 0; i < flen; i++) {
		buf[tlen - i - 1] = buf[flen - i - 1];
	}

	if ((mode == NCP_RSA_SIGN) || (mode == NCP_RSA_SIGNR)) {
		/* Padding for signature */
		buf[0] = '\0';
		buf[1] = '\1';

		for (i = 2; i < tlen - flen - 1; i++) {
			buf[i] = (unsigned char) 0xff;
		}
		buf[tlen - flen - 1] = '\0';
	} else {
		/* Padding for encrypt or wrap */
		buf[0] = '\0';
		buf[1] = '\2';

		(void) random_get_pseudo_bytes((uchar_t *)(&buf[2]),
		    tlen - flen - 3);
		/* make sure all are nonzero */
		for (i = 2; i < tlen - flen - 1; i++) {
			if (buf[i] == '\0') {
				(void) random_get_pseudo_bytes(
				    (uchar_t *)(&buf[i]), 1);
				i--;
			}
		}

		buf[tlen - flen - 1] = '\0';
	}

/* EXPORT DELETE END */

	return (CRYPTO_SUCCESS);
}

static int
ncp_pkcs1_unpadding(char *buf, int *tlen, int flen, int mode)
{
	int i;
	const unsigned char *p;
	unsigned char type;

/* EXPORT DELETE START */

	p = (unsigned char *) buf;
	if (*(p++) != 0) {
		return (decrypt_error_code(mode,
		    CRYPTO_ENCRYPTED_DATA_INVALID,
		    CRYPTO_SIGNATURE_INVALID, CRYPTO_DATA_INVALID));
	}

	/* It is ok if the data length is 0 after removing the padding */
	type = *(p++);
	if (type == 01) {
		for (i = 2; i < flen; i++) {
			if (*p != 0xff) {
				if (*p == '\0') {
					p++;
					break;
				} else {
					return (decrypt_error_code(mode,
					    CRYPTO_ENCRYPTED_DATA_INVALID,
					    CRYPTO_SIGNATURE_INVALID,
					    CRYPTO_DATA_INVALID));
				}
			}
			p++;
		}
	} else if (type == 02) {
		for (i = 2; i < flen; i++) {
			if (*p == '\0') {
				p++;
				break;
			}
			p++;
		}
	} else {
		return (decrypt_error_code(mode,
		    CRYPTO_ENCRYPTED_DATA_INVALID,
		    CRYPTO_SIGNATURE_INVALID, CRYPTO_DATA_INVALID));
	}

	/* i >= flen means did not find the end of the padding */
	if (i >= flen) {
		return (decrypt_error_code(mode,
		    CRYPTO_ENCRYPTED_DATA_INVALID,
		    CRYPTO_SIGNATURE_INVALID, CRYPTO_DATA_INVALID));
	}

	if (flen - i - 1 > *tlen) {
		*tlen = flen - i - 1;
		return (CRYPTO_BUFFER_TOO_SMALL);
	}

	if (i < 10) {
		return (decrypt_error_code(mode,
		    CRYPTO_ENCRYPTED_DATA_LEN_RANGE,
		    CRYPTO_SIGNATURE_LEN_RANGE, CRYPTO_DATA_LEN_RANGE));
	}

	/* Return the unpadded length to the caller */
	*tlen = flen - i - 1;
	bcopy(buf + i + 1, buf, *tlen);

/* EXPORT DELETE END */

	return (CRYPTO_SUCCESS);
}


static int
ncp_x509_padding(caddr_t buf, int flen, int tlen)
{

/* EXPORT DELETE START */

	DBG2(NULL, DENTRY, "ncp_x509_padding: tlen: %d, flen: %d\n",
	    tlen, flen);

	if (flen < tlen) {
		bcopy(buf, buf + (tlen - flen), flen);
		bzero(buf, tlen - flen);
	}

/* EXPORT DELETE END */

	return (CRYPTO_SUCCESS);
}

/* ARGSUSED */
static int
ncp_x509_unpadding(char *buf, int tlen, int flen, int mode)
{
	int i;
	const unsigned char *p;

/* EXPORT DELETE START */

	p = (unsigned char *) buf;

	for (i = 0; i < flen - tlen; i++) {
		if (*(p++) != 0)
			return (CRYPTO_SIGNATURE_INVALID);
	}
	bcopy(p, buf, tlen);

/* EXPORT DELETE END */

	return (CRYPTO_SUCCESS);
}

static int decrypt_error_code(int mode, int decrypt, int verify, int def)
{

/* EXPORT DELETE START */

	switch (mode) {
	case NCP_RSA_DEC:
		return (decrypt);
	case NCP_RSA_VRFY:
	case NCP_RSA_VRFYR:
		return (verify);
	default:
		break;
	}

/* EXPORT DELETE END */

	return (def);
}


/*
 * at this point, everything is checked already
 */
int
ncp_rsa_private_process(ncp_t *ncp, ncp_request_t *reqp)
{
	int		rv;
	BIGNUM		a, p, q, dp, dq, pinv, result;
	uint32_t	modlen, plen, qlen;
	big_modexp_ncp_info_t	info;

/* EXPORT DELETE START */

	info.func = &ncp_big_modexp_ncp;
	info.ncp = ncp;
	info.reqp = reqp;

	modlen = reqp->nr_rsa_modlen;
	plen = reqp->nr_rsa_plen;
	qlen = reqp->nr_rsa_qlen;

	if (ncp_big_init(&a, CHARLEN2BIGNUMLEN(modlen)) != BIG_OK) {
		return (CRYPTO_HOST_MEMORY);
	}
	if (ncp_big_init(&p, CHARLEN2BIGNUMLEN(plen)) != BIG_OK) {
		rv = CRYPTO_HOST_MEMORY;
		goto err1;
	}
	if (ncp_big_init(&q, CHARLEN2BIGNUMLEN(qlen)) != BIG_OK) {
		rv = CRYPTO_HOST_MEMORY;
		goto err2;
	}
	if (ncp_big_init(&dp, CHARLEN2BIGNUMLEN(plen)) != BIG_OK) {
		rv = CRYPTO_HOST_MEMORY;
		goto err3;
	}
	if (ncp_big_init(&dq, CHARLEN2BIGNUMLEN(qlen)) != BIG_OK) {
		rv = CRYPTO_HOST_MEMORY;
		goto err4;
	}
	if (ncp_big_init(&pinv, CHARLEN2BIGNUMLEN(qlen)) != BIG_OK) {
		rv = CRYPTO_HOST_MEMORY;
		goto err5;
	}
	if (ncp_big_init(&result, modlen) != BIG_OK) {
		rv = CRYPTO_HOST_MEMORY;
		goto err6;
	}

	ncp_kcl2bignum(&a, reqp->nr_inbuf, reqp->nr_inlen);
	ncp_kcl2bignum(&p, reqp->nr_rsa_p, plen);
	ncp_kcl2bignum(&q, reqp->nr_rsa_q, qlen);
	ncp_kcl2bignum(&dp, reqp->nr_rsa_dp,
	    reqp->nr_rsa_dplen);
	ncp_kcl2bignum(&dq, reqp->nr_rsa_dq,
	    reqp->nr_rsa_dqlen);
	ncp_kcl2bignum(&pinv, reqp->nr_rsa_pinv,
	    reqp->nr_rsa_pinvlen);

	rv = ncp_big_modexp_crt(&result, &a, &dp, &dq, &p, &q,
	    &pinv, NULL, NULL, &info);
	switch (rv) {
	case BIG_OK:
		rv = CRYPTO_SUCCESS;
		break;
	case EWOULDBLOCK:
		rv = CRYPTO_BUSY;
		break;
	default:
		rv = CRYPTO_FAILED;
		break;
	}
	if (rv != BIG_OK)
		goto err7;

	ncp_bignum2kcl(reqp->nr_outbuf, &result,
	    reqp->nr_rsa_modlen);

err7:
	ncp_big_finish(&result);
err6:
	ncp_big_finish(&pinv);
err5:
	ncp_big_finish(&dq);
err4:
	ncp_big_finish(&dp);
err3:
	ncp_big_finish(&q);
err2:
	ncp_big_finish(&p);
err1:
	ncp_big_finish(&a);

/* EXPORT DELETE END */

	return (rv);
}


/*
 * at this point, everything is checked already
 */
int
ncp_rsa_public_process(ncp_t *ncp, ncp_request_t *reqp)
{
	int		rv;
	BIGNUM		n, exp, a, result;
	uint32_t	modlen, explen;
	big_modexp_ncp_info_t	info;

/* EXPORT DELETE START */

	info.func = &ncp_big_modexp_ncp;
	info.ncp = ncp;
	info.reqp = reqp;

	modlen = reqp->nr_rsa_modlen;
	explen = reqp->nr_rsa_explen;

	if (ncp_big_init(&n, CHARLEN2BIGNUMLEN(modlen)) != BIG_OK) {
		return (CRYPTO_HOST_MEMORY);
	}
	if (ncp_big_init(&exp, CHARLEN2BIGNUMLEN(explen)) != BIG_OK) {
		rv = CRYPTO_HOST_MEMORY;
		goto err1;
	}
	if (ncp_big_init(&a, CHARLEN2BIGNUMLEN(modlen)) != BIG_OK) {
		rv = CRYPTO_HOST_MEMORY;
		goto err2;
	}
	if (ncp_big_init(&result, CHARLEN2BIGNUMLEN(modlen)) != BIG_OK) {
		rv = CRYPTO_HOST_MEMORY;
		goto err3;
	}

	ncp_kcl2bignum(&n, reqp->nr_rsa_mod, modlen);
	ncp_kcl2bignum(&exp, reqp->nr_rsa_exp, explen);
	ncp_kcl2bignum(&a, reqp->nr_inbuf, reqp->nr_inlen);

	rv = ncp_big_modexp(&result, &a, &exp, &n, NULL, &info);
	switch (rv) {
	case BIG_OK:
		rv = CRYPTO_SUCCESS;
		break;
	case EWOULDBLOCK:
		rv = CRYPTO_BUSY;
		break;
	default:
		rv = CRYPTO_FAILED;
		break;
	}
	if (rv != BIG_OK)
		goto err4;

	ncp_bignum2kcl(reqp->nr_outbuf, &result,
	    reqp->nr_rsa_modlen);

err4:
	ncp_big_finish(&result);
err3:
	ncp_big_finish(&a);
err2:
	ncp_big_finish(&exp);
err1:
	ncp_big_finish(&n);

/* EXPORT DELETE END */

	return (rv);
}

/*
 * at this point, everything is checked already
 */
int
ncp_rsa_generate_process(ncp_t *ncp, ncp_request_t *reqp)
{
	BIGNUM e;
	uchar_t *pub_exp;
	uint32_t modulus_len, pub_explen;
	int rv;

/* EXPORT DELETE START */

	e.malloced = 0;

	/* Public Exponent */
	pub_exp = reqp->nr_rsa_exp;
	pub_explen = reqp->nr_rsa_explen;

	/* Modulus Length */
	modulus_len = reqp->nr_rsa_modlen;

	if (ncp_big_init(&e, CHARLEN2BIGNUMLEN(pub_explen)) != BIG_OK) {
		rv = CRYPTO_HOST_MEMORY;
		goto errout;
	}

	ncp_kcl2bignum(&e, pub_exp, pub_explen);

	if ((rv = generate_rsa_key(modulus_len * 4, modulus_len * 4,
	    &e, ncp, reqp))
	    != CRYPTO_SUCCESS) {
		goto errout;
	}

/* EXPORT DELETE END */

errout:
	ncp_big_finish(&e);
	return (rv);
}

static int
generate_rsa_key(int psize, int qsize, BIGNUM * pubexp, ncp_t *ncp,
    ncp_request_t *reqp)
{
	int rv = CRYPTO_SUCCESS;
	BIGNUM		a, b, c, d, e, f, g, h;
	uint32_t	len, keylen, size;
	BIG_ERR_CODE	brv = BIG_OK;
	big_modexp_ncp_info_t	info;

/* EXPORT DELETE START */

	info.func = &ncp_big_modexp_ncp;
	info.ncp = ncp;
	info.reqp = reqp;

	size = psize + qsize;
	keylen = CHARLEN2BIGNUMLEN(size);
	len = keylen * 2 + 1;
	a.malloced = 0;
	b.malloced = 0;
	c.malloced = 0;
	d.malloced = 0;
	e.malloced = 0;
	f.malloced = 0;
	g.malloced = 0;
	h.malloced = 0;

	if ((ncp_big_init(&a, len) != BIG_OK) ||
	    (ncp_big_init(&b, len) != BIG_OK) ||
	    (ncp_big_init(&c, len) != BIG_OK) ||
	    (ncp_big_init(&d, len) != BIG_OK) ||
	    (ncp_big_init(&e, len) != BIG_OK) ||
	    (ncp_big_init(&f, len) != BIG_OK) ||
	    (ncp_big_init(&g, len) != BIG_OK) ||
	    (ncp_big_init(&h, len) != BIG_OK)) {
		ncp_big_finish(&h);
		ncp_big_finish(&g);
		ncp_big_finish(&f);
		ncp_big_finish(&e);
		ncp_big_finish(&d);
		ncp_big_finish(&c);
		ncp_big_finish(&b);
		ncp_big_finish(&a);

		return (CRYPTO_HOST_MEMORY);
	}
nextp:
	if ((brv = ncp_randombignum(&a, psize)) != BIG_OK)
		goto ret;

	if ((brv = ncp_big_nextprime_pos(&b, &a, &info)) != BIG_OK) {
		goto ret;
	}
	if ((brv = ncp_big_sub_pos(&a, &b, &big_One)) != BIG_OK) {
		goto ret;
	}
	if ((brv = ncp_big_ext_gcd_pos(&f, &d, &g, pubexp, &a)) != BIG_OK) {
		goto ret;
	}
	if (ncp_big_cmp_abs(&f, &big_One) != 0) {
		goto nextp;
	}

	if ((brv = ncp_randombignum(&c, qsize)) != BIG_OK)
		goto ret;
nextq:
	if ((brv = ncp_big_add(&a, &c, &big_Two)) != BIG_OK) {
		goto ret;
	}
	if (ncp_big_bitlength(&a) != qsize) {
		goto nextp;
	}
	if (ncp_big_cmp_abs(&a, &b) == 0) {
		goto nextp;
	}
	if ((brv = ncp_big_nextprime_pos(&c, &a, &info)) != BIG_OK) {
		goto ret;
	}
	if ((brv = ncp_big_mul(&g, &b, &c)) != BIG_OK) {
		goto ret;
	}
	if (ncp_big_bitlength(&g) != size) {
		goto nextp;
	}

	if ((brv = ncp_big_sub_pos(&a, &b, &big_One)) != BIG_OK) {
		goto ret;
	}
	if ((brv = ncp_big_sub_pos(&d, &c, &big_One)) != BIG_OK) {
		goto ret;
	}

	if ((brv = ncp_big_mul(&a, &a, &d)) != BIG_OK) {
		goto ret;
	}
	if ((brv = ncp_big_ext_gcd_pos(&f, &d, &h, pubexp, &a)) != BIG_OK) {
		goto ret;
	}
	if (ncp_big_cmp_abs(&f, &big_One) != 0) {
		goto nextq;
	} else {
		if ((brv = ncp_big_copy(&e, pubexp)) != BIG_OK) {
			goto ret;
		}
	}
	if (d.sign == -1) {
		if ((brv = ncp_big_add(&d, &d, &a)) != BIG_OK) {
			goto ret;
		}
	}

	(void) ncp_bignum_to_attr(CKA_PUBLIC_EXPONENT, reqp->nr_public_attrs,
	    reqp->nr_public_attrs_count, &e,
	    CRYPTO_BITS2BYTES(ncp_big_bitlength(&e)));

	if (!(ncp_bignum_to_attr(CKA_PRIME_2, reqp->nr_private_attrs,
	    reqp->nr_private_attrs_count, &b,
	    CRYPTO_BITS2BYTES(ncp_big_bitlength(&b))) &&
	    ncp_bignum_to_attr(CKA_PRIME_1, reqp->nr_private_attrs,
	    reqp->nr_private_attrs_count, &c,
	    CRYPTO_BITS2BYTES(ncp_big_bitlength(&c))) &&
	    ncp_bignum_to_attr(CKA_MODULUS, reqp->nr_private_attrs,
	    reqp->nr_private_attrs_count, &g,
	    CRYPTO_BITS2BYTES(ncp_big_bitlength(&g))) &&
	    ncp_bignum_to_attr(CKA_MODULUS, reqp->nr_public_attrs,
	    reqp->nr_public_attrs_count, &g,
	    CRYPTO_BITS2BYTES(ncp_big_bitlength(&g))) &&
	    ncp_bignum_to_attr(CKA_PRIVATE_EXPONENT, reqp->nr_private_attrs,
	    reqp->nr_private_attrs_count, &d,
	    CRYPTO_BITS2BYTES(ncp_big_bitlength(&d))))) {
		rv = CRYPTO_FAILED;
		goto ret1;
	}

	if ((brv = ncp_big_ext_gcd_pos(&a, &f, &h, &b, &c)) != BIG_OK) {
		goto ret;
	}
	if (f.sign == -1) {
		if ((brv = ncp_big_add(&f, &f, &c)) != BIG_OK) {
			goto ret;
		}
	}
	if (!ncp_bignum_to_attr(CKA_COEFFICIENT, reqp->nr_private_attrs,
	    reqp->nr_private_attrs_count, &f,
	    CRYPTO_BITS2BYTES(ncp_big_bitlength(&f)))) {
		rv = CRYPTO_FAILED;
		goto ret1;
	}

	if ((brv = ncp_big_sub(&a, &b, &big_One)) != BIG_OK) {
		goto ret;
	}
	if ((brv = ncp_big_div_pos(&a, &f, &d, &a)) != BIG_OK) {
		goto ret;
	}
	if (!ncp_bignum_to_attr(CKA_EXPONENT_2, reqp->nr_private_attrs,
	    reqp->nr_private_attrs_count, &f,
	    CRYPTO_BITS2BYTES(ncp_big_bitlength(&f)))) {
		rv = CRYPTO_FAILED;
		goto ret1;
	}
	if ((brv = ncp_big_sub(&a, &c, &big_One)) != BIG_OK) {
		goto ret;
	}
	if ((brv = ncp_big_div_pos(&a, &f, &d, &a)) != BIG_OK) {
		goto ret;
	}
	if (!ncp_bignum_to_attr(CKA_EXPONENT_1, reqp->nr_private_attrs,
	    reqp->nr_private_attrs_count, &f,
	    CRYPTO_BITS2BYTES(ncp_big_bitlength(&f)))) {
		rv = CRYPTO_FAILED;
		goto ret1;
	}

	if ((brv = ncp_randombignum(&h, size)) != BIG_OK)
		goto ret;

	if ((brv = ncp_big_div_pos(&a, &h, &h, &g)) != BIG_OK) {
		goto ret;
	}
	if ((brv = ncp_big_modexp(&a, &h, &d, &g, NULL, &info)) != BIG_OK) {
		goto ret;
	}

	if ((brv = ncp_big_modexp(&b, &a, &e, &g, NULL, &info)) != BIG_OK) {
		goto ret;
	}

	if (ncp_big_cmp_abs(&b, &h) != 0) {
		goto nextp;
	} else {
		brv = BIG_OK;
	}

ret:
	switch (brv) {
	case BIG_OK:
		rv = CRYPTO_SUCCESS;
		break;
	case EWOULDBLOCK:
		rv = CRYPTO_BUSY;
		break;
	default:
		rv = CRYPTO_FAILED;
		break;
	}
ret1:
	ncp_big_finish(&h);
	ncp_big_finish(&g);
	ncp_big_finish(&f);
	ncp_big_finish(&e);
	ncp_big_finish(&d);
	ncp_big_finish(&c);
	ncp_big_finish(&b);
	ncp_big_finish(&a);

/* EXPORT DELETE END */

	return (rv);
}
