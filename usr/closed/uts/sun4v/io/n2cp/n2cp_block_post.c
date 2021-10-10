/*
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */


/*
 * Niagara 2 Crypto Provider: Block Cipher POST for FIPS140-2
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

#define	BLOCK_DECRYPT			0
#define	BLOCK_ENCRYPT			1

#define	DES3_BLOCK_SZ			8
#define	DES3_ENCRYPT_LENGTH		DES3_BLOCK_SZ
#define	DES3_KEY_SIZE			192

#define	AES_BLOCK_SZ			16
#define	AES_KEY_SIZE_128		128
#define	AES_KEY_SIZE_192		192
#define	AES_KEY_SIZE_256		256
#define	AES_ENCRYPT_LENGTH		AES_BLOCK_SZ

/* output buffer for both encrypt and decrypt. */
uint8_t output_buffer[512];

/* AES CTR Parameters */
static const CK_AES_CTR_PARAMS	aes_ctr_param = {
	16,	/* ulCounterBits */
	{	/* cb */
		0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
		0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff
	}
};

/* AES CCM Parameters */
static const CK_AES_CCM_PARAMS	aes_ccm128_param = {
	AES_CCM_TLEN,		/* ulMACSize */
	AES_CCM_NONCE_SZ,	/* ulNonceSize */
	AES_CCM_AUTHDATA_SZ,	/* ulAuthDataSize */
	(ulong_t)-1,		/* ulDataSize: assigned later */
	aes_ccm128_known_nonce,
	aes_ccm128_known_adata,
};

static const CK_AES_CCM_PARAMS	aes_ccm192_param = {
	AES_CCM_TLEN,		/* ulMACSize */
	AES_CCM_NONCE_SZ,	/* ulNonceSize */
	AES_CCM_AUTHDATA_SZ,	/* ulAuthDataSize */
	(ulong_t)-1,		/* ulDataSize: assigned later */
	aes_ccm192_known_nonce,
	aes_ccm192_known_adata,
};

static const CK_AES_CCM_PARAMS	aes_ccm256_param = {
	AES_CCM_TLEN,		/* ulMACSize */
	AES_CCM_NONCE_SZ,	/* ulNonceSize */
	AES_CCM_AUTHDATA_SZ,	/* ulAuthDataSize */
	(ulong_t)-1,		/* ulDataSize: assigned later */
	aes_ccm256_known_nonce,
	aes_ccm256_known_adata,
};

/* AES GCM Parameters */
static CK_AES_GCM_PARAMS	aes_gcm128_param = {
	aes_gcm128_known_iv,	/* pIv */
	AES_GMAC_IV_LEN,	/* ulIvLen */
	AES_GMAC_IV_LEN * 8,	/* ulIvBits */
	aes_gcm128_known_adata,	/* pAAD */
	AES_GCM_AAD_LEN,	/* ulAADLen */
	AES_GMAC_TAG_BITS	/* ulTagBits */
};

static CK_AES_GCM_PARAMS	aes_gcm192_param = {
	aes_gcm192_known_iv,	/* pIv */
	AES_GMAC_IV_LEN,	/* ulIvLen */
	AES_GMAC_IV_LEN * 8,	/* ulIvBits */
	aes_gcm192_known_adata,	/* pAAD */
	AES_GCM_AAD_LEN,	/* ulAADLen */
	AES_GMAC_TAG_BITS	/* ulTagBits */
};

static CK_AES_GCM_PARAMS	aes_gcm256_param = {
	aes_gcm256_known_iv,	/* pIv */
	AES_GMAC_IV_LEN,	/* ulIvLen */
	AES_GMAC_IV_LEN * 8,	/* ulIvBits */
	aes_gcm256_known_adata,	/* pAAD */
	AES_GCM_AAD_LEN,	/* ulAADLen */
	AES_GMAC_TAG_BITS	/* ulTagBits */
};

/* AES GMAC Parameters */
static CK_AES_GMAC_PARAMS	aes_gmac128_param = {
	aes_gmac128_known_iv,		/* pIv */
	aes_gmac128_known_adata,	/* pAAD */
	AES_GMAC_AAD_LEN
};

static CK_AES_GMAC_PARAMS	aes_gmac192_param = {
	aes_gmac192_known_iv,		/* pIv */
	aes_gmac192_known_adata,	/* pAAD */
	AES_GMAC_AAD_LEN
};

static CK_AES_GMAC_PARAMS	aes_gmac256_param = {
	aes_gmac256_known_iv,		/* pIv */
	aes_gmac256_known_adata,	/* pAAD */
	AES_GMAC_AAD_LEN
};



/*
 * Test Case Vectors
 */
typedef	struct	post_block_test {
	crypto_mech_type_t	block_mech_type;
	void			*block_param;
	size_t			block_param_len;
	uchar_t			*block_key;
	size_t			block_key_len;
	uchar_t			*block_message;
	size_t			block_message_len;
	uchar_t			*block_cipher;
	size_t			block_cipher_len;
} post_block_test_t;

static post_block_test_t	block_tests[] = {
{
	DES3_ECB_MECH_INFO_TYPE,
	NULL,
	0,
	des3_known_key,
	DES3_KEY_SIZE,
	des3_ecb_known_plaintext,
	DES3_BLOCK_SZ,
	des3_ecb_known_ciphertext,
	sizeof (des3_ecb_known_ciphertext)
},
{
	DES3_CBC_MECH_INFO_TYPE,
	des3_cbc_known_iv,
	DES3_BLOCK_SZ,
	des3_known_key,
	DES3_KEY_SIZE,
	des3_cbc_known_plaintext,
	DES3_BLOCK_SZ,
	des3_cbc_known_ciphertext,
	sizeof (des3_cbc_known_ciphertext)
},
{
	AES_ECB_MECH_INFO_TYPE,
	NULL,
	0,
	aes_known_key,
	128,
	aes_known_plaintext,
	AES_BLOCK_SZ,
	aes_ecb128_known_ciphertext,
	sizeof (aes_ecb128_known_ciphertext)
},
{
	AES_ECB_MECH_INFO_TYPE,
	NULL,
	0,
	aes_known_key,
	192,
	aes_known_plaintext,
	AES_BLOCK_SZ,
	aes_ecb192_known_ciphertext,
	sizeof (aes_ecb192_known_ciphertext)
},
{
	AES_ECB_MECH_INFO_TYPE,
	NULL,
	0,
	aes_known_key,
	256,
	aes_known_plaintext,
	AES_BLOCK_SZ,
	aes_ecb256_known_ciphertext,
	sizeof (aes_ecb256_known_ciphertext)
},
{
	AES_CBC_MECH_INFO_TYPE,
	aes_cbc_known_initialization_vector,
	AES_BLOCK_SZ,
	aes_known_key,
	128,
	aes_known_plaintext,
	AES_BLOCK_SZ,
	aes_cbc128_known_ciphertext,
	sizeof (aes_cbc128_known_ciphertext)
},
{
	AES_CBC_MECH_INFO_TYPE,
	aes_cbc_known_initialization_vector,
	AES_BLOCK_SZ,
	aes_known_key,
	192,
	aes_known_plaintext,
	AES_BLOCK_SZ,
	aes_cbc192_known_ciphertext,
	sizeof (aes_cbc192_known_ciphertext)
},
{
	AES_CBC_MECH_INFO_TYPE,
	aes_cbc_known_initialization_vector,
	AES_BLOCK_SZ,
	aes_known_key,
	256,
	aes_known_plaintext,
	AES_BLOCK_SZ,
	aes_cbc256_known_ciphertext,
	sizeof (aes_cbc256_known_ciphertext)
},
{
	AES_CTR_MECH_INFO_TYPE,
	(void *)&aes_ctr_param,
	sizeof (CK_AES_CTR_PARAMS),
	aes_ctr128_known_key,
	128,
	aes_ctr_known_plaintext,
	AES_BLOCK_SZ,
	aes_ctr128_known_ciphertext,
	sizeof (aes_ctr128_known_ciphertext)
},
{
	AES_CTR_MECH_INFO_TYPE,
	(void *)&aes_ctr_param,
	sizeof (CK_AES_CTR_PARAMS),
	aes_ctr192_known_key,
	192,
	aes_ctr_known_plaintext,
	AES_BLOCK_SZ,
	aes_ctr192_known_ciphertext,
	sizeof (aes_ctr192_known_ciphertext)
},
{
	AES_CTR_MECH_INFO_TYPE,
	(void *)&aes_ctr_param,
	sizeof (CK_AES_CTR_PARAMS),
	aes_ctr256_known_key,
	256,
	aes_ctr_known_plaintext,
	AES_BLOCK_SZ,
	aes_ctr256_known_ciphertext,
	sizeof (aes_ctr256_known_ciphertext)
},
{
	AES_CCM_MECH_INFO_TYPE,
	(void *)&aes_ccm128_param,
	sizeof (CK_AES_CCM_PARAMS),
	aes_ccm128_known_key,
	128,
	aes_ccm128_known_plaintext,
	sizeof (aes_ccm128_known_plaintext),
	aes_ccm128_known_ciphertext,
	sizeof (aes_ccm128_known_ciphertext)
},
{
	AES_CCM_MECH_INFO_TYPE,
	(void *)&aes_ccm192_param,
	sizeof (CK_AES_CCM_PARAMS),
	aes_ccm192_known_key,
	192,
	aes_ccm192_known_plaintext,
	sizeof (aes_ccm192_known_plaintext),
	aes_ccm192_known_ciphertext,
	sizeof (aes_ccm192_known_ciphertext)
},
{
	AES_CCM_MECH_INFO_TYPE,
	(void *)&aes_ccm256_param,
	sizeof (CK_AES_CCM_PARAMS),
	aes_ccm256_known_key,
	256,
	aes_ccm256_known_plaintext,
	sizeof (aes_ccm256_known_plaintext),
	aes_ccm256_known_ciphertext,
	sizeof (aes_ccm256_known_ciphertext)
},
{
	AES_GCM_MECH_INFO_TYPE,
	(void *)&aes_gcm128_param,
	sizeof (CK_AES_GCM_PARAMS),
	aes_gcm128_known_key,
	128,
	aes_gcm128_known_plaintext,
	sizeof (aes_gcm128_known_plaintext),
	aes_gcm128_known_ciphertext,
	sizeof (aes_gcm128_known_ciphertext)
},
{
	AES_GCM_MECH_INFO_TYPE,
	(void *)&aes_gcm192_param,
	sizeof (CK_AES_GCM_PARAMS),
	aes_gcm192_known_key,
	192,
	aes_gcm192_known_plaintext,
	sizeof (aes_gcm192_known_plaintext),
	aes_gcm192_known_ciphertext,
	sizeof (aes_gcm192_known_ciphertext)
},
{
	AES_GCM_MECH_INFO_TYPE,
	(void *)&aes_gcm256_param,
	sizeof (CK_AES_GCM_PARAMS),
	aes_gcm256_known_key,
	256,
	aes_gcm256_known_plaintext,
	sizeof (aes_gcm256_known_plaintext),
	aes_gcm256_known_ciphertext,
	sizeof (aes_gcm256_known_ciphertext)
},
{
	AES_GMAC_MECH_INFO_TYPE,
	(void *)&aes_gmac128_param,
	sizeof (CK_AES_GMAC_PARAMS),
	aes_gmac128_known_key,
	128,
	NULL,
	0,
	aes_gmac128_known_tag,
	sizeof (aes_gmac128_known_tag)
},
{
	AES_GMAC_MECH_INFO_TYPE,
	(void *)&aes_gmac192_param,
	sizeof (CK_AES_GMAC_PARAMS),
	aes_gmac192_known_key,
	192,
	NULL,
	0,
	aes_gmac192_known_tag,
	sizeof (aes_gmac192_known_tag)
},
{
	AES_GMAC_MECH_INFO_TYPE,
	(void *)&aes_gmac256_param,
	sizeof (CK_AES_GMAC_PARAMS),
	aes_gmac256_known_key,
	256,
	NULL,
	0,
	aes_gmac256_known_tag,
	sizeof (aes_gmac256_known_tag)
},
};

#define	BLOCK_TEST_NUM	(sizeof (block_tests) / sizeof (post_block_test_t))


static void
setup_context(post_block_test_t *blocktest, crypto_mechanism_t *mech,
    crypto_key_t *key, crypto_data_t *in, crypto_data_t *out, int mode)
{
	mech->cm_type = blocktest->block_mech_type;
	mech->cm_param = (char *)blocktest->block_param;
	mech->cm_param_len = blocktest->block_param_len;

	if (mode == BLOCK_ENCRYPT) {
		in->cd_raw.iov_base = (char *)blocktest->block_message;
		in->cd_raw.iov_len = blocktest->block_message_len;
		in->cd_length = blocktest->block_message_len;
		out->cd_raw.iov_len = blocktest->block_cipher_len;
		out->cd_length =  blocktest->block_cipher_len;
		if (mech->cm_type == AES_CCM_MECH_INFO_TYPE) {
			((CK_AES_CCM_PARAMS *)(blocktest->block_param))->
			    ulDataSize = AES_CCM_DATA_SZ;
		}
	} else {
		in->cd_raw.iov_base = (char *)blocktest->block_cipher;
		in->cd_raw.iov_len = blocktest->block_cipher_len;
		in->cd_length = blocktest->block_cipher_len;
		out->cd_raw.iov_len = blocktest->block_message_len;
		out->cd_length =  blocktest->block_message_len;
		if (mech->cm_type == AES_CCM_MECH_INFO_TYPE) {
			((CK_AES_CCM_PARAMS *)(blocktest->block_param))->
			    ulDataSize = AES_CCM_CIPHER_SZ;
		}
	}

	/* setup crypto_key_t */
	key->ck_format = CRYPTO_KEY_RAW;
	key->ck_data = blocktest->block_key;
	key->ck_length = blocktest->block_key_len;

	/* setup crypto_data_t (input) */
	in->cd_format = CRYPTO_DATA_RAW;
	in->cd_offset = 0;
	in->cd_miscdata = NULL;

	/* setup crypto_data_t (output) */
	out->cd_format = CRYPTO_DATA_RAW;
	out->cd_offset = 0;
	out->cd_raw.iov_base = (char *)output_buffer;
}



/*
 * note: keysize in bits
 */
static int
run_block_post(n2cp_t *n2cp, post_block_test_t *blocktest)
{
	crypto_mechanism_t	mech;
	crypto_key_t		key;
	crypto_data_t		in;
	crypto_data_t		out;
	int			rv = CRYPTO_SUCCESS;

	/*
	 * Test Encrypt
	 */

	/* setup the context for encryption */
	setup_context(blocktest, &mech, &key, &in, &out, BLOCK_ENCRYPT);

	/* submit the encrypt request to the HW */
	rv = n2cp_blockatomic(n2cp, &mech, &key, &in, &out,
	    N2CP_FIPS_POST_REQUEST, BLOCK_ENCRYPT);

	if ((rv != CRYPTO_SUCCESS) ||
	    (out.cd_length != blocktest->block_cipher_len) ||
	    (memcmp(out.cd_raw.iov_base, blocktest->block_cipher,
	    out.cd_length) != 0)) {
		/* POST failed */
		return (CRYPTO_FAILED);
	}

	/*
	 * Test Decrypt
	 */

	/* setup the context for decryption */
	setup_context(blocktest, &mech, &key, &in, &out, BLOCK_DECRYPT);

	/* submit the decrypt request to the HW */
	rv = n2cp_blockatomic(n2cp, &mech, &key, &in, &out,
	    N2CP_FIPS_POST_REQUEST, BLOCK_DECRYPT);

	if ((rv != CRYPTO_SUCCESS) ||
	    (out.cd_length != blocktest->block_message_len) ||
	    (memcmp(out.cd_raw.iov_base, blocktest->block_message,
	    out.cd_length) != 0)) {
		/* POST failed */
		return (CRYPTO_FAILED);
	}

	return (CRYPTO_SUCCESS);
}


int
n2cp_block_post(n2cp_t *n2cp)
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
		DBG0(n2cp, DWARN, "n2cp_block_start: unable to find a CWQ");
		MAP_MUTEXES_EXIT_ALL(n2cp->n_cwqmap);
		return (CRYPTO_FAILED);
	}
	orig_qid = n2cp_fips_post_qid = cwq->cq_id;

test_cwq:

	for (i = 0; i < BLOCK_TEST_NUM; i++) {
		rv = run_block_post(n2cp, &block_tests[i]);
		if (rv != CRYPTO_SUCCESS) {
			MAP_MUTEXES_EXIT_ALL(n2cp->n_cwqmap);
			return (rv);
		}
	}

	/* find the next CWQ */
	cwq = n2cp_find_next_cwq(n2cp);
	if (cwq == NULL) {
		DBG0(n2cp, DWARN, "n2cp_block_start: unable to find the "
		    "next CWQ");
		MAP_MUTEXES_EXIT_ALL(n2cp->n_cwqmap);
		return (CRYPTO_FAILED);
	}
	n2cp_fips_post_qid = cwq->cq_id;
	/* if the next CWQ is different from orig_qid, test the CWQ */
	if (n2cp_fips_post_qid != orig_qid) {
		goto test_cwq;
	}

	/* Release all CPU locks */
	MAP_MUTEXES_EXIT_ALL(n2cp->n_cwqmap);

	return (CRYPTO_SUCCESS);
}
