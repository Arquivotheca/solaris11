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

#define	POST_DSA_MESSAGE_LENGTH		20
#define	POST_DSA_SIGNATURE_LENGTH	40

#define	UNUSED_SESSION_ID	(-1)

#define	POST_DSA_PRIVATE_ATTR_NUM	4
#define	POST_DSA_PUBLIC_ATTR_NUM	4

static crypto_object_attribute_t	privattrs[POST_DSA_PRIVATE_ATTR_NUM];
static crypto_object_attribute_t	pubattrs[POST_DSA_PUBLIC_ATTR_NUM];

uchar_t	dsa_outbuf[POST_DSA_SIGNATURE_LENGTH];

/*
 * Setup DSA private/public key
 * args: mode must be either NCP_DSA_SIGN or NCP_DSA_VRFY
 */
void
setup_dsa_key(int mode, crypto_key_t *key)
{
	if (mode == NCP_DSA_SIGN) {
		key->ck_length = sizeof (dsa_base_1024);
		key->ck_format = CRYPTO_KEY_ATTR_LIST;
		key->ck_count = POST_DSA_PRIVATE_ATTR_NUM;
		key->ck_attrs = privattrs;

		privattrs[0].oa_type = CKA_BASE;
		privattrs[0].oa_value = (char *)dsa_base_1024;
		privattrs[0].oa_value_len = sizeof (dsa_base_1024);
		privattrs[1].oa_type = CKA_PRIME;
		privattrs[1].oa_value = (char *)dsa_prime_1024;
		privattrs[1].oa_value_len = sizeof (dsa_prime_1024);
		privattrs[2].oa_type = CKA_SUBPRIME;
		privattrs[2].oa_value = (char *)dsa_subprime_1024;
		privattrs[2].oa_value_len = sizeof (dsa_subprime_1024);
		privattrs[3].oa_type = CKA_VALUE;
		privattrs[3].oa_value = (char *)dsa_privalue_1024;
		privattrs[3].oa_value_len = sizeof (dsa_privalue_1024);
	} else {
		ASSERT(mode == NCP_DSA_VRFY);
		key->ck_length = sizeof (dsa_base_1024);
		key->ck_format = CRYPTO_KEY_ATTR_LIST;
		key->ck_count = POST_DSA_PUBLIC_ATTR_NUM;
		key->ck_attrs = pubattrs;

		pubattrs[0].oa_type = CKA_BASE;
		pubattrs[0].oa_value = (char *)dsa_base_1024;
		pubattrs[0].oa_value_len = sizeof (dsa_base_1024);
		pubattrs[1].oa_type = CKA_PRIME;
		pubattrs[1].oa_value = (char *)dsa_prime_1024;
		pubattrs[1].oa_value_len = sizeof (dsa_prime_1024);
		pubattrs[2].oa_type = CKA_SUBPRIME;
		pubattrs[2].oa_value = (char *)dsa_subprime_1024;
		pubattrs[2].oa_value_len = sizeof (dsa_subprime_1024);
		pubattrs[3].oa_type = CKA_VALUE;
		pubattrs[3].oa_value = (char *)dsa_pubvalue_1024;
		pubattrs[3].oa_value_len = sizeof (dsa_pubvalue_1024);
	}
}


/*
 * Setup the context for the signing operation
 */
static void
setup_context(crypto_mechanism_t *mech, crypto_data_t *data,
    crypto_data_t *sign)
{
	mech->cm_type = DSA_MECH_INFO_TYPE;
	mech->cm_param = NULL;
	mech->cm_param_len = 0;

	data->cd_format = CRYPTO_DATA_RAW;
	data->cd_offset = 0;
	data->cd_raw.iov_base = (char *)dsa_known_data;
	data->cd_length = POST_DSA_MESSAGE_LENGTH;
	data->cd_raw.iov_len = POST_DSA_MESSAGE_LENGTH;

	sign->cd_format = CRYPTO_DATA_RAW;
	sign->cd_offset = 0;
	sign->cd_raw.iov_base = (char *)dsa_outbuf;
	sign->cd_length = POST_DSA_SIGNATURE_LENGTH;
	sign->cd_raw.iov_len = POST_DSA_SIGNATURE_LENGTH;
}


/*
 * DSA Power-On SelfTest(s).
 */
int
ncp_dsa_post(ncp_t *ncp)
{
	int			rv;
	crypto_key_t		key;
	crypto_mechanism_t	mech;
	crypto_data_t		data;
	crypto_data_t		sign;
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

	/*
	 * Test Sign
	 */
	setup_context(&mech, &data, &sign);
	setup_dsa_key(NCP_DSA_SIGN, &key);

	rv = ncp_dsaatomic(ncp, UNUSED_SESSION_ID, &mech, &key,
	    &data, &sign, KM_SLEEP, NCP_FIPS_POST_REQUEST, NCP_DSA_SIGN);
	if (rv == CRYPTO_QUEUED) {
		cv_wait(&ncp->n_fips_post_cv, &ncp->n_fips_post_lock);
		rv = ncp->n_fips_post_status;
	}

	if ((rv != CRYPTO_SUCCESS) ||
	    (sign.cd_length != POST_DSA_SIGNATURE_LENGTH)) {
		DBG1(ncp, DWARN, "ncp_dsa_post: operation failed 1[0x%x]", rv);
		rv = CRYPTO_FAILED;
		goto exit;
	}


	/*
	 * Test Verify
	 */
	setup_dsa_key(NCP_DSA_VRFY, &key);
	/* reset the data structure changed by the sign operation */
	data.cd_offset = 0;
	data.cd_length = POST_DSA_MESSAGE_LENGTH;

	rv = ncp_dsaatomic(ncp, UNUSED_SESSION_ID, &mech, &key,
	    &data, &sign, KM_SLEEP, NCP_FIPS_POST_REQUEST, NCP_DSA_VRFY);
	if (rv == CRYPTO_QUEUED) {
		cv_wait(&ncp->n_fips_post_cv, &ncp->n_fips_post_lock);
		rv = ncp->n_fips_post_status;
	}
	if (rv != CRYPTO_SUCCESS) {
		DBG1(ncp, DWARN, "ncp_dsa_post: operation failed 2[0x%x]", rv);
		goto exit;
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
