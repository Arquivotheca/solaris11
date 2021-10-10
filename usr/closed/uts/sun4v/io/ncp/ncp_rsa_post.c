/*
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */
#include <sys/types.h>
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/kmem.h>
#include <sys/systm.h>
#include <sys/sysmacros.h>
#include <sys/crypto/common.h>
#include <sys/crypto/spi.h>
#include <sys/ncp.h>
#include <fips_test_vectors.h>

#define	POST_MAX_MODULUS_LEN			256	/* 2048-bit */

typedef struct post_rsa_key {
	uint8_t		*modulus;
	size_t		modulus_len;
	uint8_t		*public_exponent;
	size_t		public_exponent_len;
	uint8_t		*priexponent;
	size_t		priexponent_len;
	uint8_t		*prime1;
	size_t		prime1_len;
	uint8_t		*prime2;
	size_t		prime2_len;
	uint8_t		*exponent1;
	size_t		exponent1_len;
	uint8_t		*exponent2;
	size_t		exponent2_len;
	uint8_t		*coefficient;
	size_t		coefficient_len;
} post_rsa_key_t;

static post_rsa_key_t	rsakeys[] = {
	{
		rsa_modulus_1024, sizeof (rsa_modulus_1024),
		rsa_public_exponent_1024, sizeof (rsa_public_exponent_1024),
		rsa_private_exponent_1024, sizeof (rsa_private_exponent_1024),
		rsa_prime1_1024, sizeof (rsa_prime1_1024),
		rsa_prime2_1024, sizeof (rsa_prime2_1024),
		rsa_exponent1_1024, sizeof (rsa_exponent1_1024),
		rsa_exponent2_1024, sizeof (rsa_exponent2_1024),
		rsa_coefficient_1024, sizeof (rsa_coefficient_1024)
	},
	{
		rsa_modulus_2048, sizeof (rsa_modulus_2048),
		rsa_public_exponent_2048, sizeof (rsa_public_exponent_2048),
		rsa_private_exponent_2048, sizeof (rsa_private_exponent_2048),
		rsa_prime1_2048, sizeof (rsa_prime1_2048),
		rsa_prime2_2048, sizeof (rsa_prime2_2048),
		rsa_exponent1_2048, sizeof (rsa_exponent1_2048),
		rsa_exponent2_2048, sizeof (rsa_exponent2_2048),
		rsa_coefficient_2048, sizeof (rsa_coefficient_2048)
	},
	{ NULL, 0, NULL, 0, NULL, 0, NULL, 0,
	    NULL, 0, NULL, 0, NULL, 0, NULL, 0}
};

uchar_t	rsa_outbuf[POST_MAX_MODULUS_LEN];


#define	UNUSED_SESSION_ID	(-1)

#define	FIPS_RSA_PRIVATE_ATTR_NUM	7
#define	FIPS_RSA_PUBLIC_ATTR_NUM	2

static crypto_object_attribute_t	privattrs[FIPS_RSA_PRIVATE_ATTR_NUM];
static crypto_object_attribute_t	pubattrs[FIPS_RSA_PUBLIC_ATTR_NUM];

/*
 * Set RSA private/public key
 * args: 'mode' must be either NCP_RSA_SIGN or NCP_RSA_VRFY
 */
void
setup_rsa_key(int mode, post_rsa_key_t *rsakey, crypto_key_t *key)
{
	if (mode == NCP_RSA_SIGN) {
		key->ck_format = CRYPTO_KEY_ATTR_LIST;
		key->ck_count = FIPS_RSA_PRIVATE_ATTR_NUM;
		key->ck_attrs = privattrs;

		privattrs[0].oa_type = SUN_CKA_MODULUS;
		privattrs[0].oa_value = (char *)rsakey->modulus;
		privattrs[0].oa_value_len = rsakey->modulus_len;
		privattrs[1].oa_type = SUN_CKA_PRIVATE_EXPONENT;
		privattrs[1].oa_value = (char *)rsakey->priexponent;
		privattrs[1].oa_value_len = rsakey->priexponent_len;
		privattrs[2].oa_type = SUN_CKA_PRIME_1;
		privattrs[2].oa_value = (char *)rsakey->prime1;
		privattrs[2].oa_value_len = rsakey->prime1_len;
		privattrs[3].oa_type = SUN_CKA_PRIME_2;
		privattrs[3].oa_value = (char *)rsakey->prime2;
		privattrs[3].oa_value_len = rsakey->prime2_len;
		privattrs[4].oa_type = SUN_CKA_EXPONENT_1;
		privattrs[4].oa_value = (char *)rsakey->exponent1;
		privattrs[4].oa_value_len = rsakey->exponent1_len;
		privattrs[5].oa_type = SUN_CKA_EXPONENT_2;
		privattrs[5].oa_value = (char *)rsakey->exponent2;
		privattrs[5].oa_value_len = rsakey->exponent2_len;
		privattrs[6].oa_type = SUN_CKA_COEFFICIENT;
		privattrs[6].oa_value = (char *)rsakey->coefficient;
		privattrs[6].oa_value_len = rsakey->coefficient_len;
	} else {
		ASSERT(mode == NCP_RSA_VRFY);
		key->ck_format = CRYPTO_KEY_ATTR_LIST;
		key->ck_count = FIPS_RSA_PUBLIC_ATTR_NUM;
		key->ck_attrs = pubattrs;

		pubattrs[0].oa_type = SUN_CKA_MODULUS;
		pubattrs[0].oa_value = (char *)rsakey->modulus;
		pubattrs[0].oa_value_len = rsakey->modulus_len;
		pubattrs[1].oa_type = SUN_CKA_PUBLIC_EXPONENT;
		pubattrs[1].oa_value = (char *)rsakey->public_exponent;
		pubattrs[1].oa_value_len = rsakey->public_exponent_len;
	}
}


/*
 * Setup the context for Sign Opearion
 */
static void
setup_context(int modulus_len, crypto_mechanism_t *mech,
    crypto_data_t *in, crypto_data_t *out)
{
	mech->cm_param = NULL;
	mech->cm_param_len = 0;

	in->cd_format = CRYPTO_DATA_RAW;
	in->cd_offset = 0;
	in->cd_raw.iov_base = (char *)rsa_known_plaintext_msg;

	if (mech->cm_type == RSA_X_509_MECH_INFO_TYPE) {
		in->cd_length = modulus_len;
		in->cd_raw.iov_len = modulus_len;
	} else {
		/* input must be at least 11 bytes smaller for PKCS#1 padding */
		in->cd_length = modulus_len - 11;
		in->cd_raw.iov_len = modulus_len - 11;
	}

	out->cd_format = CRYPTO_DATA_RAW;
	out->cd_offset = 0;
	out->cd_length = modulus_len;
	out->cd_raw.iov_base = (char *)rsa_outbuf;
	out->cd_raw.iov_len = modulus_len;
}


/*
 * RSA Power-On SelfTest(s).
 */
int
ncp_rsa_post(ncp_t *ncp)
{
	int			rv;
	int			i;
	post_rsa_key_t		*rsakey;
	crypto_key_t		key;
	crypto_mechanism_t	mech;
	crypto_data_t		in;
	crypto_data_t		out;
	uint8_t			*expout;
	size_t			expoutlen;
	int			orig_mid;
	ncp_mau2cpu_map_t	*m2cp = &ncp->n_maumap;

	/* block other DR requests */
	mutex_enter(&ncp->n_dr_lock);

	/* find a MAU to test */
	mutex_enter(&m2cp->m_lock);
	orig_mid = ncp_fips_post_mid = ncp_map_nextmau(ncp);
	if (ncp_fips_post_mid < 0) {
		mutex_exit(&m2cp->m_lock);
		mutex_exit(&ncp->n_dr_lock);
		DBG0(ncp, DWARN, "ncp_rsa_post: unable to find a MAU");
		return (CRYPTO_FAILED);
	}
	mutex_exit(&m2cp->m_lock);

	mutex_enter(&ncp->n_fips_post_lock);

test_mau:
	i = 0;
	while (rsakeys[i].modulus != NULL) {
		rsakey = &rsakeys[i++];

		/*
		 * Test RSA X.509 Sign
		 */
		mech.cm_type = RSA_X_509_MECH_INFO_TYPE;
		setup_rsa_key(NCP_RSA_SIGN, rsakey, &key);
		setup_context(rsakey->modulus_len, &mech, &in, &out);

		rv = ncp_rsaatomic(ncp, UNUSED_SESSION_ID, &mech, &key,
		    &in, &out, KM_SLEEP, NCP_FIPS_POST_REQUEST, NCP_RSA_SIGN);
		if (rv == CRYPTO_QUEUED) {
			cv_wait(&ncp->n_fips_post_cv, &ncp->n_fips_post_lock);
			rv = ncp->n_fips_post_status;
		}

		if (rsakey->modulus_len == 128) {
			expout = rsa_x509_known_signature_1024;
			expoutlen = sizeof (rsa_x509_known_signature_1024);
		} else {
			expout = rsa_x509_known_signature_2048;
			expoutlen = sizeof (rsa_x509_known_signature_2048);
		}

		if ((rv != CRYPTO_SUCCESS) ||
		    (out.cd_length != expoutlen) ||
		    (memcmp(out.cd_raw.iov_base, expout, expoutlen) != 0)) {
			DBG1(ncp, DWARN, "ncp_rsa_post: operation "
			    "failed 1[0x%x]", rv);
			mutex_exit(&ncp->n_fips_post_lock);
			rv = CRYPTO_FAILED;
			goto exit;
		}

		/*
		 * Test RSA X.509 Verify
		 */
		setup_rsa_key(NCP_RSA_VRFY, rsakey, &key);
		/* reset the data structure changed by the sign operation */
		in.cd_offset = 0;
		in.cd_length = rsakey->modulus_len;

		rv = ncp_rsaatomic(ncp, UNUSED_SESSION_ID, &mech, &key,
		    &out, &in, KM_SLEEP, NCP_FIPS_POST_REQUEST, NCP_RSA_VRFY);
		if (rv == CRYPTO_QUEUED) {
			cv_wait(&ncp->n_fips_post_cv, &ncp->n_fips_post_lock);
			rv = ncp->n_fips_post_status;
		}

		if (rv != CRYPTO_SUCCESS) {
			DBG1(ncp, DWARN, "ncp_rsa_post: operation "
			    "failed 2[0x%x]", rv);
			goto exit;
		}

		/*
		 * Test RSA RSASSA-PKCS1_V1_5 Sign
		 */
		mech.cm_type = RSA_PKCS_MECH_INFO_TYPE;
		setup_rsa_key(NCP_RSA_SIGN, rsakey, &key);
		setup_context(rsakey->modulus_len, &mech, &in, &out);

		rv = ncp_rsaatomic(ncp, UNUSED_SESSION_ID, &mech, &key,
		    &in, &out, KM_SLEEP, NCP_FIPS_POST_REQUEST, NCP_RSA_SIGN);
		if (rv == CRYPTO_QUEUED) {
			cv_wait(&ncp->n_fips_post_cv, &ncp->n_fips_post_lock);
			rv = ncp->n_fips_post_status;
		}

		if (rsakey->modulus_len == 128) {
			expout = rsa_pkcs_known_signature_1024;
			expoutlen = sizeof (rsa_pkcs_known_signature_1024);
		} else {
			expout = rsa_pkcs_known_signature_2048;
			expoutlen = sizeof (rsa_pkcs_known_signature_2048);
		}

		if ((rv != CRYPTO_SUCCESS) ||
		    (out.cd_length != expoutlen) ||
		    (memcmp(out.cd_raw.iov_base, expout,
		    expoutlen) != 0)) {
			DBG1(ncp, DWARN, "ncp_rsa_post: operation "
			    "failed 3[0x%x]", rv);
			rv = CRYPTO_FAILED;
			goto exit;
		}

		/*
		 * Test RSASSA-PKCS1_V1_5 Verify
		 */
		setup_rsa_key(NCP_RSA_VRFY, rsakey, &key);
		/* reset the data structure changed by the sign operation */
		in.cd_offset = 0;
		in.cd_length = rsakey->modulus_len;

		rv = ncp_rsaatomic(ncp, UNUSED_SESSION_ID, &mech, &key,
		    &out, &in, KM_SLEEP, NCP_FIPS_POST_REQUEST, NCP_RSA_VRFY);
		if (rv == CRYPTO_QUEUED) {
			cv_wait(&ncp->n_fips_post_cv, &ncp->n_fips_post_lock);
			rv = ncp->n_fips_post_status;
		}

		if (rv != CRYPTO_SUCCESS) {
			DBG1(ncp, DWARN, "ncp_rsa_post: operation "
			    "failed 4[0x%x]", rv);
			goto exit;
		}
	}

	/* find the next MAU */
	mutex_enter(&m2cp->m_lock);
	ncp_fips_post_mid = ncp_map_nextmau(ncp);
	if (ncp_fips_post_mid < 0) {
		mutex_exit(&m2cp->m_lock);
		DBG0(ncp, DWARN, "ncp_rsa_post: unable to find a MAU");
		rv = CRYPTO_FAILED;
		goto exit;
	}
	mutex_exit(&m2cp->m_lock);
	/* if the next MAU is different from orig_mid, test the MAU */
	if (ncp_fips_post_mid != orig_mid) {
		goto test_mau;
	}

exit:

	mutex_exit(&ncp->n_fips_post_lock);
	mutex_exit(&ncp->n_dr_lock);

	return (rv);
}




int
ncp_rsa_pairwise_consist_test(ncp_t *ncp,
    uchar_t *pubexp, uint32_t pubexplen,
    crypto_object_attribute_t *pubattrs, uint_t pubattr_count,
    crypto_object_attribute_t *privattrs, uint_t privattr_count)
{
	int			i;
	int			rv;
	size_t			modlen = 0;	/* in bytes */
	crypto_key_t		key;
	crypto_mechanism_t	mech;
	crypto_data_t		in;
	crypto_data_t		out;
	crypto_object_attribute_t	mypubattrs[2];

	/*
	 * Test RSA Private Key
	 */
	for (i = 0; i < privattr_count; i++) {
		if (privattrs[i].oa_type == SUN_CKA_MODULUS) {
			modlen = privattrs[i].oa_value_len;
			break;
		}
	}
	if (modlen == 0) {
		/* modlus did not exist or invalid modulus length */
		return (CRYPTO_FAILED);
	}

	key.ck_format = CRYPTO_KEY_ATTR_LIST;
	key.ck_count = privattr_count;
	key.ck_attrs = privattrs;

	mech.cm_type = RSA_X_509_MECH_INFO_TYPE;
	setup_context(modlen, &mech, &in, &out);

	mutex_enter(&ncp->n_fips_post_lock);

	rv = ncp_rsaatomic(ncp, UNUSED_SESSION_ID, &mech, &key,
	    &in, &out, KM_SLEEP, NCP_FIPS_CONSIST_REQUEST, NCP_RSA_SIGN);
	if (rv == CRYPTO_QUEUED) {
		cv_wait(&ncp->n_fips_post_cv, &ncp->n_fips_post_lock);
		rv = ncp->n_fips_post_status;
	}

	if (rv != CRYPTO_SUCCESS) {
		mutex_exit(&ncp->n_fips_post_lock);
		return (CRYPTO_FAILED);
	}


	/*
	 * Test RSA Public Key
	 */

	/* build the RSA public key template */
	mypubattrs[0].oa_type = SUN_CKA_PUBLIC_EXPONENT;
	mypubattrs[0].oa_value = (char *)pubexp;
	mypubattrs[0].oa_value_len = pubexplen;
	modlen = 0;
	for (i = 0; i < pubattr_count; i++) {
		if (pubattrs[i].oa_type == SUN_CKA_MODULUS) {
			modlen = pubattrs[i].oa_value_len;
			mypubattrs[1].oa_type = SUN_CKA_MODULUS;
			mypubattrs[1].oa_value = pubattrs[i].oa_value;
			mypubattrs[1].oa_value_len = modlen;
		}
	}
	if (modlen == 0) {
		/* modlus did not exist or invalid modulus length */
		mutex_exit(&ncp->n_fips_post_lock);
		return (CRYPTO_FAILED);
	}
	key.ck_count = 2;
	key.ck_attrs = mypubattrs;
	/* reset the data structure changed by the sign operation */
	in.cd_offset = 0;
	in.cd_length = modlen;

	rv = ncp_rsaatomic(ncp, UNUSED_SESSION_ID, &mech, &key,
	    &out, &in, KM_SLEEP, NCP_FIPS_CONSIST_REQUEST, NCP_RSA_VRFY);
	if (rv == CRYPTO_QUEUED) {
		cv_wait(&ncp->n_fips_post_cv, &ncp->n_fips_post_lock);
		rv = ncp->n_fips_post_status;
	}

	mutex_exit(&ncp->n_fips_post_lock);

	return (rv);
}
