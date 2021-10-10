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
#include "ncp_ecc.h"

#define	CHECK(expr) if ((rv = (expr)) != BIG_OK) { goto cleanexit; }

/* Local function prototypes */
static void ncp_ecdhderivedone(ncp_request_t *, int errno);



/*
 * This function is used by kEF to copyin the mechanism parameter for ECDH.
 * Note: 'inmech' comes in the applications size structure
 * Note: 'outmech' is initialized by the driver so that the mech is in
 * the native size structure.
 */
int
ncp_ecdh1_allocmech(crypto_mechanism_t *inmech, crypto_mechanism_t *outmech,
    int *error, int mode)
{
	STRUCT_DECL(crypto_mechanism, mech);
	STRUCT_DECL(CK_ECDH1_DERIVE_PARAMS, params);
	CK_ECDH1_DERIVE_PARAMS	*ecdh1_params = NULL;
	caddr_t			param;
	size_t			paramlen;
	uchar_t			*buf = NULL;
	size_t			len = 0;
	int			rv = CRYPTO_SUCCESS;

/* EXPORT DELETE START */

	DBG0(NULL, DENTRY, "ncp_ecdh1_allocmech -->");

	*error = 0;

	STRUCT_INIT(mech, mode);
	STRUCT_INIT(params, mode);
	bcopy(inmech, STRUCT_BUF(mech), STRUCT_SIZE(mech));
	param = STRUCT_FGETP(mech, cm_param);
	paramlen = STRUCT_FGET(mech, cm_param_len);

	/*
	 * Parameter is required for ecdh1 key derivation: there is no
	 * crypto_data passed to key derivation, thus, it must be passed
	 * through mechanism.param.
	 */
	if ((param == NULL) || (paramlen != STRUCT_SIZE(params))) {
		DBG0(NULL, DCHATTY, "ncp_ecdh1_allocmech: bad param");
		return (CRYPTO_MECHANISM_PARAM_INVALID);
	}

	outmech->cm_type = STRUCT_FGET(mech, cm_type);
	outmech->cm_param = NULL;
	outmech->cm_param_len = 0;

	if (ddi_copyin(param, STRUCT_BUF(params), paramlen, mode) != 0) {
		DBG0(NULL, DWARN, "ncp_ecdh1_allocmech: copyin(param) failure");
		return (CRYPTO_FAILED);
	}

	/* allocate the native size structure */
	ecdh1_params = (CK_ECDH1_DERIVE_PARAMS *)
	    kmem_alloc(sizeof (CK_ECDH1_DERIVE_PARAMS), KM_SLEEP);
	if (ecdh1_params == NULL) {
		DBG0(NULL, DWARN, "ncp_ecdh1_allocmech: kmem_alloc failure");
		*error = ENOMEM;
		return (CRYPTO_HOST_MEMORY);
	}

	ecdh1_params->kdf = STRUCT_FGET(params, kdf);

	/* XXX: we don't support SHA kdf. */
	if (ecdh1_params->kdf != CKD_NULL) {
		rv = CRYPTO_MECHANISM_PARAM_INVALID;
		goto error_exit;
	}

	if ((ecdh1_params->kdf != CKD_NULL) &&
	    (ecdh1_params->kdf != CKD_SHA1_KDF)) {
		DBG1(NULL, DCHATTY, "ncp_ecdh1_allocmech: invalid kdf[%d]",
		    ecdh1_params->kdf);
		rv = CRYPTO_MECHANISM_PARAM_INVALID;
		goto error_exit;
	}
	ecdh1_params->ulSharedDataLen =
	    STRUCT_FGET(params, ulSharedDataLen);
	ecdh1_params->ulPublicDataLen =
	    STRUCT_FGET(params, ulPublicDataLen);

	/* if kdf is CKD_NULL, shared len should be 0 */
	if ((ecdh1_params->kdf == CKD_NULL) &&
	    (ecdh1_params->ulSharedDataLen != 0)) {
		rv = CRYPTO_MECHANISM_PARAM_INVALID;
		goto error_exit;
	}

	/* allocate the buffer for shared data and public data */
	len = ecdh1_params->ulSharedDataLen + ecdh1_params->ulPublicDataLen;
	buf = kmem_alloc(len, KM_SLEEP);
	if (buf == NULL) {
		DBG0(NULL, DCHATTY, "ncp_ecdh1_allocmech: kmem_alloc failure");
		*error = ENOMEM;
		rv = CRYPTO_HOST_MEMORY;
		goto error_exit;
	}

	/* copyin the optional shared data */
	if (ecdh1_params->ulSharedDataLen > 0) {
		if (ddi_copyin(STRUCT_FGETP(params, pSharedData), buf,
		    ecdh1_params->ulSharedDataLen, mode) != 0) {
			DBG0(NULL, DWARN, "ncp_ecdh1_allocmech: "
			    "copyin(shared data) failure");
			*error = EFAULT;
			rv = CRYPTO_FAILED;
			goto error_exit;
		}
		ecdh1_params->pSharedData = buf;
	} else {
		ecdh1_params->pSharedData = NULL;
	}
	/* copyin the public data */
	if (ddi_copyin(STRUCT_FGETP(params, pPublicData),
	    buf + ecdh1_params->ulSharedDataLen,
	    ecdh1_params->ulPublicDataLen, mode) != 0) {
		DBG0(NULL, DWARN, "ncp_ecdh1_allocmech: "
		    "copyin(public data) failure");
		*error = EFAULT;
		rv = CRYPTO_FAILED;
		goto error_exit;
	}
	ecdh1_params->pPublicData = buf + ecdh1_params->ulSharedDataLen;

	outmech->cm_param = (char *)ecdh1_params;
	outmech->cm_param_len = sizeof (CK_ECDH1_DERIVE_PARAMS);

	DBG0(NULL, DENTRY, "ncp_ecdh1_allocmech was successful");
	return (CRYPTO_SUCCESS);

error_exit:
	if (ecdh1_params != NULL) {
		kmem_free(ecdh1_params, sizeof (CK_ECDH1_DERIVE_PARAMS));
	}
	if (buf != NULL) {
		kmem_free(buf, len);
	}

/* EXPORT DELETE END */

	return (rv);
}

int
ncp_ecdh1_freemech(crypto_mechanism_t *mech)
{
	CK_ECDH1_DERIVE_PARAMS	*ecdh1_params;
	size_t			buflen;

/* EXPORT DELETE START */

	if ((mech->cm_param == NULL) || (mech->cm_param_len == 0)) {
		return (CRYPTO_SUCCESS);
	}
	/* if the parameter size is unexpected, return an error */
	if (mech->cm_param_len != sizeof (CK_ECDH1_DERIVE_PARAMS)) {
		return (CRYPTO_MECHANISM_PARAM_INVALID);
	}

	/*LINTED*/
	ecdh1_params = (CK_ECDH1_DERIVE_PARAMS *)mech->cm_param;
	buflen = ecdh1_params->ulSharedDataLen + ecdh1_params->ulPublicDataLen;
	if (ecdh1_params->pSharedData != NULL) {
		/*
		 * If pSharedData is non-NULL, that is the beginning of the
		 * data. Free for both shared data and public data.
		 */
		kmem_free(ecdh1_params->pSharedData, buflen);
	} else if (ecdh1_params->pPublicData != NULL) {
		/* free the public data */
		kmem_free(ecdh1_params->pPublicData, buflen);
	}

	/* free the parameter structure */
	kmem_free(mech->cm_param, mech->cm_param_len);


/* EXPORT DELETE END */

	return (CRYPTO_SUCCESS);
}




static void
ncp_ecdhderivedone(ncp_request_t *reqp, int errno)
{
	int	atomic = reqp->nr_atomic;
#ifdef	DEBUG
	ncp_t	*ncp = reqp->nr_ncp;
#endif

/* EXPORT DELETE START */

	ASSERT(reqp->nr_kcf_req != NULL);

	/* notify framework that request is completed */
	crypto_op_notification(reqp->nr_kcf_req, errno);
#ifdef DEBUG
	if (errno != CRYPTO_SUCCESS) {
		DBG1(ncp, DWARN,
		    "ncp_ecdhderivedone: rtn 0x%x to the kef via "
		    "crypto_op_notification", errno);
	}
#endif /* DEBUG */

	/* key derivation is atomic */
	if (atomic) {
		crypto_ctx_t ctx;
		ctx.cc_provider_private = reqp;
		ncp_ecdhctxfree(&ctx);
	}

/* EXPORT DELETE END */

}

void
ncp_ecdhctxfree(void *arg)
{
	crypto_ctx_t	*ctx = (crypto_ctx_t *)arg;
	ncp_request_t	*reqp = ctx->cc_provider_private;

/* EXPORT DELETE START */

	if (reqp == NULL)
		return;

	reqp->nr_mode = 0;
	reqp->nr_ctx_cm_type = 0;
	reqp->nr_ecc_oidlen = 0;
	reqp->nr_ecc_dlen = 0;
	reqp->nr_atomic = 0;

	kmem_cache_free(reqp->nr_ncp->n_request_cache, reqp);

	ctx->cc_provider_private = NULL;

/* EXPORT DELETE END */

}


/* ARGSUSED */
int
ncp_ecdh_derive(crypto_provider_handle_t provider,
    crypto_session_id_t session_id, crypto_mechanism_t *mechanism,
    crypto_key_t *base_key, crypto_object_attribute_t *in_attrs,
    uint_t in_attr_count, crypto_object_attribute_t *out_attrs,
    uint_t out_attr_count, crypto_req_handle_t req)
{
	ncp_t				*ncp = (ncp_t *)provider;
	ncp_request_t			*reqp;
	crypto_ctx_t			ctx; /* on the stack */
	int				rv;
	uint32_t			key_type;
	uint32_t			key_len;
	unsigned			oid_len = 0, value_len;
	uchar_t				*oid, *value;
	crypto_object_attribute_t	*attr;
	CK_ECDH1_DERIVE_PARAMS		*ecdh1_params;

/* EXPORT DELETE START */

	if (base_key->ck_format != CRYPTO_KEY_ATTR_LIST) {
		rv = CRYPTO_ARGUMENTS_BAD;
		goto errout;
	}

	if ((rv = ncp_attr_lookup_uint32(in_attrs, in_attr_count,
	    CKA_KEY_TYPE, &key_type)) != CRYPTO_SUCCESS) {
		DBG0(NULL, DWARN, "ncp_ecdh_derive: "
		    "failed to retrieve key type");
		goto errout;
	}
	switch (key_type) {
	case CKK_RC4:
	case CKK_AES:
		if ((rv = ncp_attr_lookup_uint32(in_attrs, in_attr_count,
		    CKA_VALUE_LEN, &key_len)) != CRYPTO_SUCCESS) {
			DBG0(NULL, DWARN, "ncp_ecdh_derive: "
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

	if ((reqp = ncp_getreq(ncp, 1)) == NULL) {
		DBG0(NULL, DWARN,
		    "ncp_ecdh_derive: unable to allocate request");
		rv = CRYPTO_HOST_MEMORY;
		goto errout;
	}

	/*
	 * Input is the public value of the other party's key
	 * Note: the other party's public key value should start with 0x04.
	 */
	/*LINTED*/
	ecdh1_params = (CK_ECDH1_DERIVE_PARAMS *)mechanism->cm_param;
	if (ecdh1_params->pPublicData[0] != 0x04) {
		rv = CRYPTO_MECHANISM_PARAM_INVALID;
		goto errout;
	}
	/* copy the public key: skip the preceding 0x04 */
	bcopy(ecdh1_params->pPublicData + 1, reqp->nr_inbuf,
	    ecdh1_params->ulPublicDataLen - 1);
	reqp->nr_inlen = ecdh1_params->ulPublicDataLen - 1;

	if ((attr = ncp_get_key_attr(base_key)) == NULL) {
		DBG0(NULL, DWARN, "ncp_ecdsa_verify_init: "
		    "key attributes missing");
		rv = CRYPTO_KEY_TYPE_INCONSISTENT;
		goto errout;
	}

	/* EC params (curve OID) */
	if (ncp_attr_lookup_uint8_array(attr, base_key->ck_count, CKA_EC_PARAMS,
	    (void *) &oid, &oid_len)) {
		DBG0(NULL, DWARN, "ncp_ecdsa_sign_init: EC params not present");
		rv = CRYPTO_ATTRIBUTE_VALUE_INVALID;
		goto errout;
	}

	/* Value */
	if (ncp_attr_lookup_uint8_array(attr, base_key->ck_count, CKA_VALUE,
	    (void *) &value, &value_len)) {
		DBG0(NULL, DWARN, "ncp_ecdsa_sign_init: "
		    "private value not present");
		rv = CRYPTO_ATTRIBUTE_VALUE_INVALID;
		goto errout;
	}

	if ((oid_len < EC_MIN_OID_LEN) || (oid_len > EC_MAX_OID_LEN)) {
		rv = CRYPTO_ATTRIBUTE_VALUE_INVALID;
		goto errout;
	}
	if (value_len > CRYPTO_BITS2BYTES(EC_MAX_KEY_LEN)) {
		rv = CRYPTO_KEY_SIZE_RANGE;
		goto errout;
	}

	reqp->nr_ecc_oidlen = oid_len;
	bcopy(oid, reqp->nr_ecc_oid, oid_len);
	reqp->nr_ecc_dlen = value_len;
	bcopy(value, reqp->nr_ecc_d, value_len);


	/* nr_outlen is the length of the derived key */
	reqp->nr_outlen = key_len;

	reqp->nr_private_attrs = out_attrs;
	reqp->nr_private_attrs_count = out_attr_count;

	reqp->nr_ctx_cm_type = mechanism->cm_type;
	reqp->nr_kcf_req = req;
	reqp->nr_mode = NCP_ECDH1_DERIVE;
	reqp->nr_job_stat = DS_ECDHDERIVE;
	reqp->nr_callback = ncp_ecdhderivedone;

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

errout:

	if (rv != CRYPTO_QUEUED) {
		ctx.cc_provider_private = reqp;
		ncp_ecdhctxfree(&ctx);
	}

/* EXPORT DELETE END */

	return (rv);
}


/*
 * at this point, everything is checked already
 */
int
ncp_ecdh_derive_process(ncp_t *ncp, ncp_request_t *reqp)
{
	uint8_t		result[MAXECKEYLEN];
	int		resultlen = sizeof (result);
	int		secret_key_len;
	uint8_t		*d, *x1, *y1;
	int		dlen, x1len, y1len, maxlen;
	ECC_curve_t	*crv;
	int		rv;
	crypto_object_attribute_t	*bap;

/* EXPORT DELETE START */

	crv = ncp_ECC_find_curve(reqp->nr_ecc_oid);

	if (crv == NULL) {
		return (CRYPTO_ATTRIBUTE_VALUE_INVALID);
	}

	maxlen = CRYPTO_BITS2BYTES(crv->orderMSB + 1);

	d = reqp->nr_ecc_d;
	dlen = reqp->nr_ecc_dlen;

	x1len = y1len = reqp->nr_inlen / 2;
	x1 = reqp->nr_inbuf;
	y1 = reqp->nr_inbuf + x1len;

	CHECK(ECC_ECDH_derive(result, &resultlen,
	    d, dlen, x1, x1len, y1, y1len, crv, ncp, reqp));
	bap = ncp_find_attribute(reqp->nr_private_attrs,
	    reqp->nr_private_attrs_count, CKA_VALUE);

	if (bap == NULL || bap->oa_value == NULL || bap->oa_value_len == 0) {
		rv = BIG_BUFFER_TOO_SMALL;
		goto cleanexit;
	}

	if (reqp->nr_outlen == 0) {
		secret_key_len = maxlen;
	} else {
		secret_key_len = reqp->nr_outlen;
	}

	if (bap->oa_value_len < secret_key_len) {
		rv = BIG_BUFFER_TOO_SMALL;
		goto cleanexit;
	}
	if (secret_key_len <= resultlen) {
		bcopy(result + resultlen - secret_key_len,
		    bap->oa_value, secret_key_len);
	} else {
		(void) memset(bap->oa_value, 0, secret_key_len - resultlen);
		bcopy(result, bap->oa_value + secret_key_len - resultlen,
		    resultlen);
	}
	bap->oa_value_len = secret_key_len;

	rv = CRYPTO_SUCCESS;

cleanexit:

/* EXPORT DELETE END */

	return (CONVERTRV);
}
