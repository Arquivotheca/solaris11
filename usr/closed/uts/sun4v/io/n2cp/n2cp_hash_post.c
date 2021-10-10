/*
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Niagara 2 Crypto Provider: Hash and HMAC POST for FIPS140-2
 */

#include <sys/types.h>
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
#include <sys/crypto/ioctl.h>	/* for crypto_mechanism32 */
#include <sys/byteorder.h>
#include <modes/modes.h>
#include <fips_test_vectors.h>


/*
 * Digest Test Vectors
 */

#define	IS_KT_MECH(mech) \
	(((mech) == SHA384_MECH_INFO_TYPE) || \
	((mech) == SHA512_MECH_INFO_TYPE))

typedef	struct post_digest_test {
	crypto_mech_type_t	d_mech_type;
	uchar_t			*d_message;
	size_t			d_message_len;
	uchar_t			*d_hash;
	size_t			d_hash_len;
} post_digest_test_t;

static post_digest_test_t digest_tests[] = {
	{
		SHA1_MECH_INFO_TYPE,
		sha1_known_hash_message,
		sizeof (sha1_known_hash_message),
		sha1_known_digest,
		sizeof (sha1_known_digest)
	},
	{
		SHA256_MECH_INFO_TYPE,
		sha256_known_hash_message,
		sizeof (sha256_known_hash_message),
		known_sha256_digest,
		sizeof (known_sha256_digest)
	},
	{
		SHA384_MECH_INFO_TYPE,
		sha384_known_hash_message,
		sizeof (sha384_known_hash_message),
		known_sha384_digest,
		sizeof (known_sha384_digest)
	},
	{
		SHA512_MECH_INFO_TYPE,
		sha512_known_hash_message,
		sizeof (sha512_known_hash_message),
		known_sha512_digest,
		sizeof (known_sha512_digest)
	},
};

#define	DIGEST_TEST_NUM	(sizeof (digest_tests) / sizeof (post_digest_test_t))

/*
 * HMAC Test Vectors
 */

#define	POST_SIGN	1
#define	POST_VERIFY	2

static size_t		hmaclen;

typedef	struct post_hmac_test {
	crypto_mech_type_t	h_mech_type;
	uchar_t			*h_key;
	size_t			h_key_len;
	uchar_t			*h_message;
	size_t			h_message_len;
	uchar_t			*h_mac;
	size_t			h_mac_len;
} post_hmac_test_t;

static post_hmac_test_t	hmac_tests[] = {
	{
		SHA1_HMAC_GENERAL_MECH_INFO_TYPE,
		HMAC_known_secret_key,
		sizeof (HMAC_known_secret_key),
		hmac_sha1_known_hash_message,
		sizeof (hmac_sha1_known_hash_message),
		known_SHA1_hmac,
		sizeof (known_SHA1_hmac),
	},
	{
		SHA1_HMAC_GENERAL_MECH_INFO_TYPE,
		sha1_hmac_known_secret_key_2,
		sizeof (sha1_hmac_known_secret_key_2),
		(uchar_t *)sha1_hmac_known_hash_message_2,
		sizeof (sha1_hmac_known_hash_message_2),
		sha1_known_hmac_2,
		sizeof (sha1_known_hmac_2),
	},
	{
		SHA256_HMAC_GENERAL_MECH_INFO_TYPE,
		sha256_hmac_known_secret_key_1,
		sizeof (sha256_hmac_known_secret_key_1),
		sha256_hmac_known_hash_message_1,
		sizeof (sha256_hmac_known_hash_message_1),
		sha256_known_hmac_1,
		sizeof (sha256_known_hmac_1),
	},
	{
		SHA256_HMAC_GENERAL_MECH_INFO_TYPE,
		sha256_hmac_known_secret_key_2,
		sizeof (sha256_hmac_known_secret_key_2),
		sha256_hmac_known_hash_message_2,
		sizeof (sha256_hmac_known_hash_message_2),
		sha256_known_hmac_2,
		sizeof (sha256_known_hmac_2),
	},
};

#define	HMAC_TEST_NUM	(sizeof (hmac_tests) / sizeof (post_hmac_test_t))


static uint8_t	digest_outbuf[512];

static void
setup_digest_context(post_digest_test_t *test, crypto_mechanism_t *mech,
    crypto_data_t *in, crypto_data_t *out)
{
	mech->cm_type = test->d_mech_type;
	mech->cm_param = NULL;
	mech->cm_param_len = 0;

	/* setup crypto_data_t (input) */
	in->cd_format = CRYPTO_DATA_RAW;
	in->cd_offset = 0;
	in->cd_miscdata = NULL;
	in->cd_raw.iov_base = (char *)test->d_message;
	in->cd_raw.iov_len = test->d_message_len;
	in->cd_length = test->d_message_len;

	/* setup crypto_data_t (output) */
	out->cd_format = CRYPTO_DATA_RAW;
	out->cd_offset = 0;
	in->cd_miscdata = NULL;
	out->cd_raw.iov_base = (char *)digest_outbuf;
	out->cd_raw.iov_len = test->d_hash_len;
	out->cd_length = test->d_hash_len;
}


static void
setup_hmac_context(post_hmac_test_t *test, crypto_mechanism_t *mech,
    crypto_key_t *key, crypto_data_t *in, crypto_data_t *out, int mode)
{

	/* setup crypto_key_t */
	key->ck_format = CRYPTO_KEY_RAW;
	key->ck_data = test->h_key;
	key->ck_length = test->h_key_len * 8;

	/* setup context: HMAC length */
	hmaclen = test->h_mac_len;
	mech->cm_type = test->h_mech_type;
	mech->cm_param = (void *)&hmaclen;
	mech->cm_param_len = sizeof (hmaclen);


	/* setup crypto_data_t (input) */
	in->cd_format = CRYPTO_DATA_RAW;
	in->cd_offset = 0;
	in->cd_raw.iov_base = (char *)test->h_message;
	in->cd_raw.iov_len = test->h_message_len;
	in->cd_length = test->h_message_len;
	in->cd_miscdata = NULL;

	/* setup crypto_data_t (output) */
	out->cd_format = CRYPTO_DATA_RAW;
	out->cd_offset = 0;
	if (mode == POST_SIGN) {
		/* for the sign operation, out.buf should be empty buf */
		out->cd_raw.iov_base = (char *)digest_outbuf;
	} else {
		/* for the verify operation, out.buf should contain hmac */
		out->cd_raw.iov_base = (char *)test->h_mac;
	}

	out->cd_raw.iov_len = test->h_mac_len;
	out->cd_length = test->h_mac_len;
}


static int
run_digest_post(n2cp_t *n2cp, post_digest_test_t *test)
{
	crypto_mechanism_t	mech;
	crypto_data_t		in;
	crypto_data_t		out;
	int			rv = CRYPTO_SUCCESS;

	if (!is_KT(n2cp) && IS_KT_MECH(test->d_mech_type)) {
		/* If this is not KT, skip the KT-only mech */
		return (CRYPTO_SUCCESS);
	}

	/* setup the context for digest */
	setup_digest_context(test, &mech, &in, &out);

	/* submit the digest request to the HW */
	rv = n2cp_hashatomic(n2cp, &mech, &in, &out, N2CP_FIPS_POST_REQUEST);

	if ((rv != CRYPTO_SUCCESS) ||
	    (out.cd_length != test->d_hash_len) ||
	    (memcmp(out.cd_raw.iov_base, test->d_hash, out.cd_length) != 0)) {
		/* POST failed */
		return (CRYPTO_FAILED);
	}

	return (CRYPTO_SUCCESS);
}


/*
 */
static int
run_hmac_post(n2cp_t *n2cp, post_hmac_test_t *test)
{
	crypto_mechanism_t	mech;
	crypto_key_t		key;
	crypto_data_t		in;
	crypto_data_t		out;
	int			rv = CRYPTO_SUCCESS;

	/*
	 * Test HMAC
	 */

	/* setup the context for HMAC sign */
	setup_hmac_context(test, &mech, &key, &in, &out, POST_SIGN);

	/* submit the HMAC sign request to the HW */
	rv = n2cp_hmac_signatomic(n2cp, &mech, &key, &in, &out,
	    N2CP_FIPS_POST_REQUEST);

	if ((rv != CRYPTO_SUCCESS) ||
	    (out.cd_length != test->h_mac_len) ||
	    (memcmp(out.cd_raw.iov_base, test->h_mac, out.cd_length) != 0)) {
		/* POST failed */
		return (CRYPTO_FAILED);
	}

	/* setup the context for HMAC verify */
	setup_hmac_context(test, &mech, &key, &in, &out, POST_VERIFY);

	/* submit the HMAC verify request to the HW */
	rv = n2cp_hmac_verifyatomic(n2cp, &mech, &key, &in, &out,
	    N2CP_FIPS_POST_REQUEST);

	if (rv != CRYPTO_SUCCESS) {
		/* POST failed */
		return (rv);
	}

	return (CRYPTO_SUCCESS);
}


int
n2cp_hash_post(n2cp_t *n2cp)
{
	int	rv;
	int	i;
	cwq_t	*cwq;
	int	orig_qid;

	/* Hold all CPU locks to prevent CPU DR */
	MAP_MUTEXES_ENTER_ALL(n2cp->n_cwqmap);

	/* find a CWQ to test */
	cwq = n2cp_find_next_cwq(n2cp);
	if (cwq == NULL) {
		DBG0(n2cp, DWARN, "n2cp_hash_start: unable to find a CWQ");
		MAP_MUTEXES_EXIT_ALL(n2cp->n_cwqmap);
		return (CRYPTO_FAILED);
	}
	orig_qid = n2cp_fips_post_qid = cwq->cq_id;

test_cwq:

	/* Run Digest Test */
	for (i = 0; i < DIGEST_TEST_NUM; i++) {
		rv = run_digest_post(n2cp, &digest_tests[i]);
		if (rv != CRYPTO_SUCCESS) {
			MAP_MUTEXES_EXIT_ALL(n2cp->n_cwqmap);
			return (rv);
		}
	}

	/* Run HMAC Test */
	for (i = 0; i < HMAC_TEST_NUM; i++) {
		rv = run_hmac_post(n2cp, &hmac_tests[i]);
		if (rv != CRYPTO_SUCCESS) {
			MAP_MUTEXES_EXIT_ALL(n2cp->n_cwqmap);
			return (rv);
		}
	}

	/* find the next CWQ */
	cwq = n2cp_find_next_cwq(n2cp);
	if (cwq == NULL) {
		DBG0(n2cp, DWARN, "n2cp_hash_start: unable to find the "
		    "next CWQ");
		MAP_MUTEXES_EXIT_ALL(n2cp->n_cwqmap);
		return (CRYPTO_FAILED);
	}
	n2cp_fips_post_qid = cwq->cq_id;
	/* if the next CWQ is different from orig_qid, test the CWQ */
	if (n2cp_fips_post_qid != orig_qid) {
		goto test_cwq;
	}

	/* release all CPU locks */
	MAP_MUTEXES_EXIT_ALL(n2cp->n_cwqmap);

	return (CRYPTO_SUCCESS);
}
