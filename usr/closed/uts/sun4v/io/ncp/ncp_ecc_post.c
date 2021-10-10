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

#define	POST_ECC_MESSAGE_LENGTH		20
#define	POST_ECC_SIGNATURE_MAX_LENGTH	512

#define	UNUSED_SESSION_ID	(-1)

#define	POST_ECC_PRIVATE_ATTR_NUM	4
#define	POST_ECC_PUBLIC_ATTR_NUM	4

/*
 * ECDSA Key Structures
 */
typedef struct post_ecdsa_key {
	size_t		ec_expoutlen;
	uint8_t		*ec_param;
	size_t		ec_param_len;
	uint8_t		*ec_point;
	size_t		ec_point_len;
	uint8_t		*ec_value;
	size_t		ec_value_len;
} post_ecdsa_key_t;

static post_ecdsa_key_t	eckeys[] = {
	{
		48,
		ec_param_oid_secp192r1, sizeof (ec_param_oid_secp192r1),
		ec_point_p192r1, sizeof (ec_point_p192r1),
		ec_value_p192r1, sizeof (ec_value_p192r1)
	},
	{
		56,
		ec_param_oid_secp224r1, sizeof (ec_param_oid_secp224r1),
		ec_point_p224r1, sizeof (ec_point_p224r1),
		ec_value_p224r1, sizeof (ec_value_p224r1)
	},
	{
		64,
		ec_param_oid_secp256r1, sizeof (ec_param_oid_secp256r1),
		ec_point_p256r1, sizeof (ec_point_p256r1),
		ec_value_p256r1, sizeof (ec_value_p256r1)
	},
	{
		96,
		ec_param_oid_secp384r1, sizeof (ec_param_oid_secp384r1),
		ec_point_p384r1, sizeof (ec_point_p384r1),
		ec_value_p384r1, sizeof (ec_value_p384r1)
	},
	{
		132,
		ec_param_oid_secp521r1, sizeof (ec_param_oid_secp521r1),
		ec_point_p521r1, sizeof (ec_point_p521r1),
		ec_value_p521r1, sizeof (ec_value_p521r1)
	},
	{
		42,
		ec_param_oid_sect163k1, sizeof (ec_param_oid_sect163k1),
		ec_point_t163k1, sizeof (ec_point_t163k1),
		ec_value_t163k1, sizeof (ec_value_t163k1)
	},
	{
		58,
		ec_param_oid_sect233k1, sizeof (ec_param_oid_sect233k1),
		ec_point_t233k1, sizeof (ec_point_t233k1),
		ec_value_t233k1, sizeof (ec_value_t233k1)
	},
	{
		72,
		ec_param_oid_sect283k1, sizeof (ec_param_oid_sect283k1),
		ec_point_t283k1, sizeof (ec_point_t283k1),
		ec_value_t283k1, sizeof (ec_value_t283k1)
	},
	{
		102,
		ec_param_oid_sect409k1, sizeof (ec_param_oid_sect409k1),
		ec_point_t409k1, sizeof (ec_point_t409k1),
		ec_value_t409k1, sizeof (ec_value_t409k1)
	},
	{
		144,
		ec_param_oid_sect571k1, sizeof (ec_param_oid_sect571k1),
		ec_point_t571k1, sizeof (ec_point_t571k1),
		ec_value_t571k1, sizeof (ec_value_t571k1)
	},
	{
		42,
		ec_param_oid_sect163r2, sizeof (ec_param_oid_sect163r2),
		ec_point_t163r2, sizeof (ec_point_t163r2),
		ec_value_t163r2, sizeof (ec_value_t163r2)
	},
	{
		60,
		ec_param_oid_sect233r1, sizeof (ec_param_oid_sect233r1),
		ec_point_t233r1, sizeof (ec_point_t233r1),
		ec_value_t233r1, sizeof (ec_value_t233r1)
	},
	{
		72,
		ec_param_oid_sect283r1, sizeof (ec_param_oid_sect283r1),
		ec_point_t283r1, sizeof (ec_point_t283r1),
		ec_value_t283r1, sizeof (ec_value_t283r1)
	},
	{
		104,
		ec_param_oid_sect409r1, sizeof (ec_param_oid_sect409r1),
		ec_point_t409r1, sizeof (ec_point_t409r1),
		ec_value_t409r1, sizeof (ec_value_t409r1)
	},
	{
		144,
		ec_param_oid_sect571r1, sizeof (ec_param_oid_sect571r1),
		ec_point_t571r1, sizeof (ec_point_t571r1),
		ec_value_t571r1, sizeof (ec_value_t571r1)
	},
	{ 0, NULL, 0, NULL, 0, NULL, 0 }
};

static crypto_object_attribute_t	privattrs[POST_ECC_PRIVATE_ATTR_NUM];
static crypto_object_attribute_t	pubattrs[POST_ECC_PUBLIC_ATTR_NUM];

static uint8_t	ecdsa_known_data[POST_ECC_MESSAGE_LENGTH];
static uint8_t	ecdsa_outbuf[POST_ECC_SIGNATURE_MAX_LENGTH];

/*
 * Setup the ECDSA private/public key
 * args: 'mode' has to be either NCP_ECDSA_SIGN or NCP_ECDSA_VRFY
 */
void
setup_ecc_key(int mode, post_ecdsa_key_t *eckey, crypto_key_t *key)
{
	/* ck_length is not applicable for EC key */
	key->ck_length = 0;

	if (mode == NCP_ECDSA_SIGN) {
		key->ck_format = CRYPTO_KEY_ATTR_LIST;
		key->ck_count = POST_ECC_PRIVATE_ATTR_NUM;
		key->ck_attrs = privattrs;

		privattrs[0].oa_type = CKA_EC_PARAMS;
		privattrs[0].oa_value = (char *)eckey->ec_param;
		privattrs[0].oa_value_len = eckey->ec_param_len;
		privattrs[1].oa_type = CKA_VALUE;
		privattrs[1].oa_value = (char *)eckey->ec_value;
		privattrs[1].oa_value_len = eckey->ec_value_len;
	} else {
		ASSERT(mode == NCP_ECDSA_VRFY);
		key->ck_format = CRYPTO_KEY_ATTR_LIST;
		key->ck_count = POST_ECC_PUBLIC_ATTR_NUM;
		key->ck_attrs = pubattrs;

		pubattrs[0].oa_type = CKA_EC_PARAMS;
		pubattrs[0].oa_value = (char *)eckey->ec_param;
		pubattrs[0].oa_value_len = eckey->ec_param_len;
		pubattrs[1].oa_type = CKA_EC_POINT;
		pubattrs[1].oa_value = (char *)eckey->ec_point;
		pubattrs[1].oa_value_len = eckey->ec_point_len;
	}
}


/*
 * Setup the context for the signing operation
 */
static void
setup_context(crypto_mechanism_t *mech, crypto_data_t *data,
    crypto_data_t *sign)
{
	mech->cm_type = ECDSA_MECH_INFO_TYPE;
	mech->cm_param = NULL;
	mech->cm_param_len = 0;

	(void) memset(ecdsa_known_data, 'z', sizeof (ecdsa_known_data));

	data->cd_format = CRYPTO_DATA_RAW;
	data->cd_offset = 0;
	data->cd_raw.iov_base = (char *)ecdsa_known_data;
	data->cd_length = POST_ECC_MESSAGE_LENGTH;
	data->cd_raw.iov_len = POST_ECC_MESSAGE_LENGTH;

	sign->cd_format = CRYPTO_DATA_RAW;
	sign->cd_offset = 0;
	sign->cd_raw.iov_base = (char *)ecdsa_outbuf;
	sign->cd_length = POST_ECC_SIGNATURE_MAX_LENGTH;
	sign->cd_raw.iov_len = POST_ECC_SIGNATURE_MAX_LENGTH;
}


/*
 * ECC Power-On SelfTest(s).
 */
int
ncp_ecc_post(ncp_t *ncp)
{
	int			rv;
	int			i;
	crypto_key_t		key;
	crypto_mechanism_t	mech;
	crypto_data_t		data;
	crypto_data_t		sign;
	post_ecdsa_key_t	*eckey;
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
	while (eckeys[i].ec_param != NULL) {
		eckey = &eckeys[i++];

		/*
		 * Test Sign
		 */
		setup_context(&mech, &data, &sign);
		setup_ecc_key(NCP_ECDSA_SIGN, eckey, &key);

		rv = ncp_ecdsaatomic(ncp, UNUSED_SESSION_ID, &mech, &key,
		    &data, &sign, NCP_FIPS_POST_REQUEST, NCP_ECDSA_SIGN);
		if (rv == CRYPTO_QUEUED) {
			cv_wait(&ncp->n_fips_post_cv, &ncp->n_fips_post_lock);
			rv = ncp->n_fips_post_status;
		}

		if ((rv != CRYPTO_SUCCESS) ||
		    (sign.cd_length != eckey->ec_expoutlen)) {
			DBG1(ncp, DWARN, "ncp_ecc_post: operation "
			    "failed 1[0x%x]", rv);
			rv = CRYPTO_FAILED;
			goto exit;
		}


		/*
		 * Test Verify
		 */
		setup_ecc_key(NCP_ECDSA_VRFY, eckey, &key);
		/* reset the data structure changed by the sign operation */
		data.cd_offset = 0;
		data.cd_length = POST_ECC_MESSAGE_LENGTH;

		rv = ncp_ecdsaatomic(ncp, UNUSED_SESSION_ID, &mech, &key,
		    &data, &sign, NCP_FIPS_POST_REQUEST, NCP_ECDSA_VRFY);
		if (rv == CRYPTO_QUEUED) {
			cv_wait(&ncp->n_fips_post_cv, &ncp->n_fips_post_lock);
			rv = ncp->n_fips_post_status;
		}
		if (rv != CRYPTO_SUCCESS) {
			DBG1(ncp, DWARN, "ncp_ecc_post: operation "
			    "failed 2[0x%x]", rv);
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
ncp_ecc_pairwise_consist_test(ncp_t *ncp,
    uchar_t *ecparam, uint32_t ecparamlen,
    crypto_object_attribute_t *pubattrs, uint_t pubattr_count,
    crypto_object_attribute_t *privattrs, uint_t privattr_count)
{
	int			i;
	int			rv;
	crypto_key_t		key;
	crypto_mechanism_t	mech;
	crypto_data_t		data;
	crypto_data_t		sign;
	crypto_object_attribute_t	myattrs[2];

	/*
	 * Construct template: set EC_PARAMS
	 */
	myattrs[0].oa_type = CKA_EC_PARAMS;
	myattrs[0].oa_value = (char *)ecparam;
	myattrs[0].oa_value_len = ecparamlen;

	/*
	 * Test ECDSA Private Key
	 */
	for (i = 0; i < privattr_count; i++) {
		if (privattrs[i].oa_type == CKA_VALUE) {
			myattrs[1].oa_type = CKA_VALUE;
			myattrs[1].oa_value = privattrs[i].oa_value;
			myattrs[1].oa_value_len = privattrs[i].oa_value_len;
		}
	}
	key.ck_format = CRYPTO_KEY_ATTR_LIST;
	key.ck_count = 2;
	key.ck_attrs = myattrs;

	setup_context(&mech, &data, &sign);

	mutex_enter(&ncp->n_fips_post_lock);

	rv = ncp_ecdsaatomic(ncp, UNUSED_SESSION_ID, &mech, &key,
	    &data, &sign, NCP_FIPS_CONSIST_REQUEST, NCP_ECDSA_SIGN);
	if (rv == CRYPTO_QUEUED) {
		cv_wait(&ncp->n_fips_post_cv, &ncp->n_fips_post_lock);
		rv = ncp->n_fips_post_status;
	}

	if (rv != CRYPTO_SUCCESS) {
		mutex_exit(&ncp->n_fips_post_lock);
		return (CRYPTO_FAILED);
	}

	/*
	 * Test ECDSA Public Key
	 */
	for (i = 0; i < pubattr_count; i++) {
		if (pubattrs[i].oa_type == CKA_EC_POINT) {
			myattrs[1].oa_type = CKA_EC_POINT;
			myattrs[1].oa_value = pubattrs[i].oa_value;
			myattrs[1].oa_value_len = pubattrs[i].oa_value_len;
		}
	}

	/* reset the data structure changed by the sign operation */
	data.cd_offset = 0;
	data.cd_length = POST_ECC_MESSAGE_LENGTH;

	rv = ncp_ecdsaatomic(ncp, UNUSED_SESSION_ID, &mech, &key,
	    &data, &sign, NCP_FIPS_CONSIST_REQUEST, NCP_ECDSA_VRFY);
	if (rv == CRYPTO_QUEUED) {
		cv_wait(&ncp->n_fips_post_cv, &ncp->n_fips_post_lock);
		rv = ncp->n_fips_post_status;
	}

	mutex_exit(&ncp->n_fips_post_lock);

	return (rv);
}
