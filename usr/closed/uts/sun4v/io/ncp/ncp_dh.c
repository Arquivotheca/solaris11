/*
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
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
void ncp_dhctxfree(void *);
int ncp_dh_generate_key(crypto_provider_handle_t, crypto_session_id_t,
    crypto_mechanism_t *, crypto_object_attribute_t *, uint_t,
    crypto_object_attribute_t *, uint_t, crypto_object_attribute_t *,
    uint_t, crypto_object_attribute_t *, uint_t, int, crypto_req_handle_t);
int ncp_dh_derive(crypto_provider_handle_t, crypto_session_id_t,
    crypto_mechanism_t *, crypto_key_t *, crypto_object_attribute_t *, uint_t,
    crypto_object_attribute_t *, uint_t, int, crypto_req_handle_t);

/* Local function prototypes */
static void ncp_dhgendone(ncp_request_t *, int errno);
static void ncp_dhderivedone(ncp_request_t *, int errno);
static void dh_callback(ncp_request_t *reqp, int rv);

static void
dh_callback(ncp_request_t *reqp, int rv)
{

/* EXPORT DELETE START */

	if (reqp->nr_mode == NCP_DH_GEN) {
		ncp_dhgendone(reqp, rv);
	} else if (reqp->nr_mode == NCP_DH_DERIVE) {
		ncp_dhderivedone(reqp, rv);
	}

/* EXPORT DELETE END */

}

static void
ncp_dhgendone(ncp_request_t *reqp, int errno)
{
	int	atomic = reqp->nr_atomic;
#ifdef DEBUG
	ncp_t	*ncp = reqp->nr_ncp;
#endif

/* EXPORT DELETE START */

	ASSERT(reqp->nr_kcf_req != NULL);

	/* notify framework that request is completed */
	crypto_op_notification(reqp->nr_kcf_req, errno);
#ifdef DEBUG
	if (errno != CRYPTO_SUCCESS) {
		DBG1(ncp, DWARN,
		    "ncp_dhgendone: rtn 0x%x to the kef via "
		    "crypto_op_notification", errno);
	}
#endif /* DEBUG */

	/* key generation is atomic */
	if (atomic) {
		crypto_ctx_t ctx;
		ctx.cc_provider_private = reqp;
		ncp_dhctxfree(&ctx);
	}

/* EXPORT DELETE END */

}

static void
ncp_dhderivedone(ncp_request_t *reqp, int errno)
{
	int	atomic = reqp->nr_atomic;
#ifdef DEBUG
	ncp_t	*ncp = reqp->nr_ncp;
#endif

/* EXPORT DELETE START */

	ASSERT(reqp->nr_kcf_req != NULL);

	/* notify framework that request is completed */
	crypto_op_notification(reqp->nr_kcf_req, errno);
#ifdef DEBUG
	if (errno != CRYPTO_SUCCESS) {
		DBG1(ncp, DWARN,
		    "ncp_dhderivedone: rtn 0x%x to the kef via "
		    "crypto_op_notification", errno);
	}
#endif /* DEBUG */

	/* key derivation is atomic */
	if (atomic) {
		crypto_ctx_t ctx;
		ctx.cc_provider_private = reqp;
		ncp_dhctxfree(&ctx);
	}

/* EXPORT DELETE END */

}

void
ncp_dhctxfree(void *arg)
{
	crypto_ctx_t	*ctx = (crypto_ctx_t *)arg;
	ncp_request_t	*reqp = ctx->cc_provider_private;

/* EXPORT DELETE START */

	if (reqp == NULL)
		return;

	reqp->nr_mode = 0;
	reqp->nr_ctx_cm_type = 0;
	reqp->nr_dh_xlen = 0;
	reqp->nr_dh_plen = 0;
	reqp->nr_atomic = 0;

	kmem_cache_free(reqp->nr_ncp->n_request_cache, reqp);
	ctx->cc_provider_private = NULL;

/* EXPORT DELETE END */

}

/* ARGSUSED */
int
ncp_dh_generate_key(crypto_provider_handle_t provider,
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
	uint32_t prime_len, base_len, len = 0;
	uint32_t value_bits;
	ulong_t *prime, *base;
	ulong_t *vp;

/* EXPORT DELETE START */

	if ((rv = ncp_attr_lookup_uint8_array(in_pub_attrs, in_pub_attr_count,
	    CKA_PRIME, (void **)&prime, &prime_len)) != CRYPTO_SUCCESS) {
		DBG0(NULL, DWARN, "ncp_dh_generate_key: "
		    "failed to retrieve prime");
		goto errout;
	}

	if (prime_len == 0) {
		DBG0(NULL, DWARN, "ncp_dh_generate_key: "
		    "failed to retrieve prime");
		rv = CRYPTO_ARGUMENTS_BAD;
		goto errout;
	}

	/* Prime length needs to be between min key size and max key size. */
	if ((prime_len < DH_MIN_KEY_LEN) || (prime_len > DH_MAX_KEY_LEN)) {
		rv = CRYPTO_ATTRIBUTE_VALUE_INVALID;
		goto errout;
	}

	if ((rv = ncp_attr_lookup_uint8_array(in_pub_attrs, in_pub_attr_count,
	    CKA_BASE, (void **)&base, &base_len)) != CRYPTO_SUCCESS) {
		DBG0(NULL, DWARN, "ncp_dh_generate_key: "
		    "failed to retrieve base");
		goto errout;
	}

	if (base_len == 0) {
		DBG0(NULL, DWARN, "ncp_dh_generate_key: bad base size");
		rv = CRYPTO_ARGUMENTS_BAD;
		goto errout;
	}

	/* value bits are optional */
	if ((rv = ncp_attr_lookup_uint8_array(in_pri_attrs, in_pri_attr_count,
	    CKA_VALUE_BITS, (void **)&vp, &len)) == CRYPTO_SUCCESS) {
		if (len == 0) {
			DBG0(NULL, DWARN, "ncp_dh_generate_key: "
			    "failed to retrieve value bits");
			rv = CRYPTO_ARGUMENTS_BAD;
			goto errout;
		}
		value_bits = (uint32_t)*vp;
	} else {
		value_bits = 0;
	}

	if ((reqp = ncp_getreq(ncp, 1)) == NULL) {
		DBG0(NULL, DWARN,
		    "ncp_dh_generate_key: unable to allocate request");
		rv = CRYPTO_HOST_MEMORY;
		goto errout;
	}

	/* Prime */
	bcopy(prime, reqp->nr_dh_p, prime_len);
	reqp->nr_dh_plen = prime_len;

	/* Base */
	bcopy(base, reqp->nr_dh_g, base_len);
	reqp->nr_dh_glen = base_len;

	/* Value Bits */
	reqp->nr_dh_xlen = value_bits;

	reqp->nr_public_attrs = out_pub_attrs;
	reqp->nr_public_attrs_count = out_pub_attr_count;
	reqp->nr_private_attrs = out_pri_attrs;
	reqp->nr_private_attrs_count = out_pri_attr_count;

	reqp->nr_ctx_cm_type = mechanism->cm_type;
	reqp->nr_kcf_req = req;
	reqp->nr_mode = NCP_DH_GEN;
	reqp->nr_job_stat = DS_DHGEN;
	reqp->nr_callback = dh_callback;

	/*
	 * Set the atomic flag so that the hardware callback function
	 * will free the context.
	 */
	reqp->nr_atomic = 1;

	/* schedule the work by doing a submit */
	rv = ncp_start(ncp, reqp);

	if (rv != CRYPTO_QUEUED) {
		ctx.cc_provider_private = reqp;
		ncp_dhctxfree(&ctx);
	}
errout:

/* EXPORT DELETE END */

	return (rv);
}

/* ARGSUSED */
int
ncp_dh_derive(crypto_provider_handle_t provider,
    crypto_session_id_t session_id, crypto_mechanism_t *mechanism,
    crypto_key_t *base_key, crypto_object_attribute_t *in_attrs,
    uint_t in_attr_count, crypto_object_attribute_t *out_attrs,
    uint_t out_attr_count, int kmflag, crypto_req_handle_t req)
{
	ncp_t *ncp = (ncp_t *)provider;
	ncp_request_t *reqp;
	crypto_ctx_t ctx; /* on the stack */
	int rv;
	uint32_t prime_len, value_len;
	uint32_t key_len;
	ulong_t *prime, *value;
	uint32_t key_type;

/* EXPORT DELETE START */

	if (base_key->ck_format != CRYPTO_KEY_ATTR_LIST) {
		rv = CRYPTO_ARGUMENTS_BAD;
		goto errout;
	}

	if ((rv = ncp_attr_lookup_uint32(in_attrs, in_attr_count,
	    CKA_KEY_TYPE, &key_type)) != CRYPTO_SUCCESS) {
		DBG0(NULL, DWARN, "ncp_dh_derive: "
		    "failed to retrieve key type");
		goto errout;
	}
	switch (key_type) {
	case CKK_RC4:
	case CKK_AES:
		if ((rv = ncp_attr_lookup_uint32(in_attrs, in_attr_count,
		    CKA_VALUE_LEN, &key_len)) != CRYPTO_SUCCESS) {
			DBG0(NULL, DWARN, "ncp_dh_derive: "
			    "failed to retrieve value_len");
			goto errout;
		}
		break;
	case CKK_DES:
		key_len = DES_KEYSIZE;
		break;
	case CKK_DES2:
		key_len = DES2_KEYSIZE;
		break;
	case CKK_DES3:
		key_len = DES3_KEYSIZE;
		break;
	case CKK_GENERIC_SECRET:
		if ((rv = ncp_attr_lookup_uint32(in_attrs, in_attr_count,
		    CKA_VALUE_LEN, &key_len)) != CRYPTO_SUCCESS) {
			key_len = 0;
		}
		break;
	default:
		key_len = 0;
	}

	if ((rv = ncp_attr_lookup_uint8_array(base_key->ck_attrs,
	    base_key->ck_count, CKA_PRIME, (void **)&prime, &prime_len))
	    != CRYPTO_SUCCESS) {
		DBG0(NULL, DWARN, "ncp_dh_derive: failed to retrieve prime");
		goto errout;
	}
	if (prime_len == 0) {
		DBG0(NULL, DWARN, "ncp_dh_derive: zero length prime");
		rv = CRYPTO_ARGUMENTS_BAD;
		goto errout;
	}
	/* Prime length needs to be between min key size and max key size. */
	if ((prime_len < DH_MIN_KEY_LEN) || (prime_len > DH_MAX_KEY_LEN)) {
		rv = CRYPTO_ATTRIBUTE_VALUE_INVALID;
		goto errout;
	}

	if ((rv = ncp_attr_lookup_uint8_array(base_key->ck_attrs,
	    base_key->ck_count, CKA_VALUE, (void **)&value, &value_len))
	    != CRYPTO_SUCCESS) {
		DBG0(NULL, DWARN, "ncp_dh_derive: failed to retrieve value");
		goto errout;
	}
	if (value_len == 0) {
		DBG0(NULL, DWARN, "ncp_dh_derive: zero length private value");
		rv = CRYPTO_ARGUMENTS_BAD;
		goto errout;
	}
	if (value_len > DH_MAX_KEY_LEN) {
		rv = CRYPTO_ATTRIBUTE_VALUE_INVALID;
		goto errout;
	}

	if (mechanism->cm_param == NULL || mechanism->cm_param_len == 0) {
		DBG0(NULL, DWARN, "ncp_dh_derive: "
		    "NULL or zero length mechanism parameter");
		rv = CRYPTO_MECHANISM_PARAM_INVALID;
		goto errout;
	}
	/* check if parameter is too large to be copied into our buffer */
	if (mechanism->cm_param_len > DH_MAX_KEY_LEN) {
		rv = CRYPTO_MECHANISM_PARAM_INVALID;
		goto errout;
	}

	if ((reqp = ncp_getreq(ncp, 1)) == NULL) {
		DBG0(NULL, DWARN,
		    "ncp_dh_generate_key: unable to allocate request");
		rv = CRYPTO_HOST_MEMORY;
		goto errout;
	}

	/* Prime */
	bcopy(prime, reqp->nr_dh_p, prime_len);
	reqp->nr_dh_plen = prime_len;

	/* Private Value */
	bcopy(value, reqp->nr_dh_x, value_len);
	reqp->nr_dh_xlen = value_len;

	/* Secret Key Len */
	reqp->nr_dh_glen = key_len;

	/* Public Value */
	bcopy(mechanism->cm_param, reqp->nr_dh_y,
	    mechanism->cm_param_len);
	reqp->nr_dh_ylen = mechanism->cm_param_len;

	reqp->nr_private_attrs = out_attrs;
	reqp->nr_private_attrs_count = out_attr_count;

	reqp->nr_ctx_cm_type = mechanism->cm_type;
	reqp->nr_kcf_req = req;
	reqp->nr_mode = NCP_DH_DERIVE;
	reqp->nr_job_stat = DS_DHDERIVE;
	reqp->nr_callback = dh_callback;

	/*
	 * Set the atomic flag so that the hardware callback function
	 * will free the context.
	 */
	reqp->nr_atomic = 1;

	ctx.cc_provider = provider;
	ctx.cc_session = session_id;
	ctx.cc_provider_private = reqp;

	/* schedule the work by doing a submit */
	rv = ncp_start(ncp, reqp);

	if (rv != CRYPTO_QUEUED) {
		ctx.cc_provider_private = reqp;
		ncp_dhctxfree(&ctx);
	}
errout:

/* EXPORT DELETE END */

	return (rv);
}

/*
 * at this point, everything is checked already
 */
int
ncp_dh_generate_process(ncp_t *ncp, ncp_request_t *reqp)
{
	int		rv;
	BIG_ERR_CODE	brv;
	uint32_t	primebit_len;
	uint32_t	value_bits;
	BIGNUM		bnprime;
	BIGNUM		bnbase;
	BIGNUM		bnprival;
	BIGNUM		bnpubval;
	uint32_t	prime_len, base_len;
	big_modexp_ncp_info_t	info;

/* EXPORT DELETE START */

	info.func = &ncp_big_modexp_ncp;
	info.ncp = ncp;
	info.reqp = reqp;

	bnprime.malloced = 0;
	bnbase.malloced = 0;
	bnprival.malloced = 0;
	bnpubval.malloced = 0;

	prime_len = reqp->nr_dh_plen;
	base_len = reqp->nr_dh_glen;

	if ((ncp_big_init(&bnprime, CHARLEN2BIGNUMLEN(prime_len)) != BIG_OK) ||
	    (ncp_big_init(&bnbase, CHARLEN2BIGNUMLEN(base_len)) != BIG_OK) ||
	    (ncp_big_init(&bnprival, CHARLEN2BIGNUMLEN(prime_len)) != BIG_OK) ||
	    (ncp_big_init(&bnpubval, CHARLEN2BIGNUMLEN(prime_len)) != BIG_OK)) {
		ncp_big_finish(&bnpubval);
		ncp_big_finish(&bnprival);
		ncp_big_finish(&bnbase);
		ncp_big_finish(&bnprime);

		return (CRYPTO_HOST_MEMORY);
	}

	/* Convert the prime octet string to big integer format. */
	ncp_kcl2bignum(&bnprime, reqp->nr_dh_p, prime_len);

	/* Convert the base octet string to big integer format. */
	ncp_kcl2bignum(&bnbase, reqp->nr_dh_g, base_len);

	if (ncp_big_cmp_abs(&bnbase, &bnprime) >= 0) {
		rv = CRYPTO_ATTRIBUTE_VALUE_INVALID;
		goto ret1;
	}

	/* yes we store value bits in xlen */
	value_bits = reqp->nr_dh_xlen;
	primebit_len = ncp_big_bitlength(&bnprime);
	if (value_bits > primebit_len) {
		rv = CRYPTO_ATTRIBUTE_VALUE_INVALID;
		goto ret1;
	}

	if ((brv = ncp_randombignum(&bnprival,
	    (value_bits == 0) ? primebit_len : value_bits)) != BIG_OK)
		goto ret;

	/*
	 * The base shall be raised to the private value modulo the prime
	 * to give the public value.
	 */
	if ((brv = ncp_big_modexp(&bnpubval, &bnbase, &bnprival, &bnprime,
	    NULL, &info)) != BIG_OK) {
		goto ret;
	}

	if (!(ncp_bignum_to_attr(CKA_VALUE, reqp->nr_public_attrs,
	    reqp->nr_public_attrs_count, &bnpubval, prime_len) &&
	    ncp_bignum_to_attr(CKA_VALUE, reqp->nr_private_attrs,
	    reqp->nr_private_attrs_count, &bnprival, prime_len))) {
		rv = CRYPTO_FAILED;
		goto ret1;
	}
	rv = CRYPTO_SUCCESS;
	goto ret1;

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
	ncp_big_finish(&bnpubval);
	ncp_big_finish(&bnprival);
	ncp_big_finish(&bnbase);
	ncp_big_finish(&bnprime);

/* EXPORT DELETE END */

	return (rv);
}

/*
 * at this point, everything is checked already
 */
int
ncp_dh_derive_process(ncp_t *ncp, ncp_request_t *reqp)
{
	int		rv;
	BIG_ERR_CODE	brv;
	BIGNUM		bnprime;
	BIGNUM		bnprival;
	BIGNUM		bnpubval;
	BIGNUM		bnsecret;
	uint32_t	prime_len, pri_len, pub_len;
	uint32_t	secret_key_len, secret_key_max;
	crypto_object_attribute_t *bap;
	big_modexp_ncp_info_t	info;

/* EXPORT DELETE START */

	info.func = &ncp_big_modexp_ncp;
	info.ncp = ncp;
	info.reqp = reqp;

	bnprime.malloced = 0;
	bnprival.malloced = 0;
	bnpubval.malloced = 0;
	bnsecret.malloced = 0;

	prime_len = reqp->nr_dh_plen;
	pri_len = reqp->nr_dh_xlen;
	pub_len = reqp->nr_dh_ylen;
	secret_key_len = reqp->nr_dh_glen;

	if ((ncp_big_init(&bnprime, CHARLEN2BIGNUMLEN(prime_len)) != BIG_OK) ||
	    (ncp_big_init(&bnprival, CHARLEN2BIGNUMLEN(pri_len)) != BIG_OK) ||
	    (ncp_big_init(&bnpubval, CHARLEN2BIGNUMLEN(pub_len)) != BIG_OK) ||
	    (ncp_big_init(&bnsecret, CHARLEN2BIGNUMLEN(prime_len)) != BIG_OK)) {
		ncp_big_finish(&bnsecret);
		ncp_big_finish(&bnpubval);
		ncp_big_finish(&bnprival);
		ncp_big_finish(&bnprime);

		return (CRYPTO_HOST_MEMORY);
	}

	/* Convert the prime octet string to big integer format. */
	ncp_kcl2bignum(&bnprime, reqp->nr_dh_p, prime_len);

	/* Convert the private value octet string to big integer format. */
	ncp_kcl2bignum(&bnprival, reqp->nr_dh_x, pri_len);

	/* Convert the public value octet string to big integer format. */
	ncp_kcl2bignum(&bnpubval, reqp->nr_dh_y, pub_len);

	if ((brv = ncp_big_modexp(&bnsecret, &bnpubval, &bnprival, &bnprime,
	    NULL, &info)) != BIG_OK) {
		goto ret;
	}

	secret_key_max = CRYPTO_BITS2BYTES(ncp_big_bitlength(&bnprime));
	if (secret_key_len == 0)
		secret_key_len = secret_key_max;

	/*
	 * The truncation removes bytes from the leading end of the
	 * secret value. nr_dh_g is used as a scratch
	 * buffer since we don't need it in the shared secret derivation.
	 */
	if (secret_key_max > DH_MAX_KEY_LEN) {
		rv = CRYPTO_FAILED;
		goto ret1;
	}
	ncp_bignum2kcl((uchar_t *)reqp->nr_dh_g,
	    &bnsecret, secret_key_max);

	bap = ncp_find_attribute(reqp->nr_private_attrs,
	    reqp->nr_private_attrs_count, CKA_VALUE);
	if (bap == NULL) {
		rv = CRYPTO_FAILED;
		goto ret1;
	}

	if (bap->oa_value == NULL || bap->oa_value_len == 0) {
		rv = CRYPTO_FAILED;
		goto ret1;
	}

	if (bap->oa_value_len < secret_key_len) {
		rv = CRYPTO_FAILED;
		goto ret1;
	}
	bcopy(reqp->nr_dh_g + secret_key_max - secret_key_len,
	    bap->oa_value, secret_key_len);
	bap->oa_value_len = secret_key_len;

	rv = CRYPTO_SUCCESS;
	goto ret1;

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
	ncp_big_finish(&bnsecret);
	ncp_big_finish(&bnpubval);
	ncp_big_finish(&bnprival);
	ncp_big_finish(&bnprime);

/* EXPORT DELETE END */

	return (rv);
}
