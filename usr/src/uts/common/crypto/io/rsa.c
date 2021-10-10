/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2003, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * RSA provider for the Kernel Cryptographic Framework (KCF)
 */

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/modctl.h>
#include <sys/cmn_err.h>
#include <sys/ddi.h>
#include <sys/crypto/spi.h>
#include <sys/sysmacros.h>
#include <sys/strsun.h>
#include <sys/md5.h>
#include <sys/sha1.h>
#define	_SHA2_IMPL
#include <sys/sha2.h>
#include <sys/random.h>
#include <sys/crypto/impl.h>
#include <sha1/sha1_impl.h>
#include <sha2/sha2_impl.h>
#include <padding/padding.h>
#define	_RSA_FIPS_POST
#include <rsa/rsa_impl.h>

extern struct mod_ops mod_cryptoops;

/*
 * Module linkage information for the kernel.
 */
static struct modlcrypto modlcrypto = {
	&mod_cryptoops,
	"RSA Kernel SW Provider"
};

static struct modlinkage modlinkage = {
	MODREV_1,
	(void *)&modlcrypto,
	NULL
};

/*
 * Mechanism info structure passed to KCF during registration.
 */
static crypto_mech_info_t rsa_mech_info_tab[] = {
	/* RSA_PKCS */
	{SUN_CKM_RSA_PKCS, CRYPTO_RSA_PKCS,
	    CRYPTO_FG_ENCRYPT | CRYPTO_FG_ENCRYPT_ATOMIC |
	    CRYPTO_FG_DECRYPT | CRYPTO_FG_DECRYPT_ATOMIC |
	    CRYPTO_FG_SIGN | CRYPTO_FG_SIGN_ATOMIC |
	    CRYPTO_FG_VERIFY | CRYPTO_FG_VERIFY_ATOMIC |
	    CRYPTO_FG_SIGN_RECOVER | CRYPTO_FG_SIGN_RECOVER_ATOMIC |
	    CRYPTO_FG_VERIFY_RECOVER | CRYPTO_FG_VERIFY_RECOVER_ATOMIC,
	    RSA_MIN_KEY_LEN, RSA_MAX_KEY_LEN, CRYPTO_KEYSIZE_UNIT_IN_BITS},

	/* RSA_X_509 */
	{SUN_CKM_RSA_X_509, CRYPTO_RSA_X_509,
	    CRYPTO_FG_ENCRYPT | CRYPTO_FG_ENCRYPT_ATOMIC |
	    CRYPTO_FG_DECRYPT | CRYPTO_FG_DECRYPT_ATOMIC |
	    CRYPTO_FG_SIGN | CRYPTO_FG_SIGN_ATOMIC |
	    CRYPTO_FG_VERIFY | CRYPTO_FG_VERIFY_ATOMIC |
	    CRYPTO_FG_SIGN_RECOVER | CRYPTO_FG_SIGN_RECOVER_ATOMIC |
	    CRYPTO_FG_VERIFY_RECOVER | CRYPTO_FG_VERIFY_RECOVER_ATOMIC,
	    RSA_MIN_KEY_LEN, RSA_MAX_KEY_LEN, CRYPTO_KEYSIZE_UNIT_IN_BITS},

	/* MD5_RSA_PKCS */
	{SUN_CKM_MD5_RSA_PKCS, CRYPTO_MD5_RSA_PKCS,
	    CRYPTO_FG_SIGN | CRYPTO_FG_SIGN_ATOMIC |
	    CRYPTO_FG_VERIFY | CRYPTO_FG_VERIFY_ATOMIC,
	    RSA_MIN_KEY_LEN, RSA_MAX_KEY_LEN, CRYPTO_KEYSIZE_UNIT_IN_BITS},

	/* SHA1_RSA_PKCS */
	{SUN_CKM_SHA1_RSA_PKCS, CRYPTO_SHA1_RSA_PKCS,
	    CRYPTO_FG_SIGN | CRYPTO_FG_SIGN_ATOMIC |
	    CRYPTO_FG_VERIFY | CRYPTO_FG_VERIFY_ATOMIC,
	    RSA_MIN_KEY_LEN, RSA_MAX_KEY_LEN, CRYPTO_KEYSIZE_UNIT_IN_BITS},

	/* SHA256_RSA_PKCS */
	{SUN_CKM_SHA256_RSA_PKCS, CRYPTO_SHA256_RSA_PKCS,
	    CRYPTO_FG_SIGN | CRYPTO_FG_SIGN_ATOMIC |
	    CRYPTO_FG_VERIFY | CRYPTO_FG_VERIFY_ATOMIC,
	    RSA_MIN_KEY_LEN, RSA_MAX_KEY_LEN, CRYPTO_KEYSIZE_UNIT_IN_BITS},

	/* SHA384_RSA_PKCS */
	{SUN_CKM_SHA384_RSA_PKCS, CRYPTO_SHA384_RSA_PKCS,
	    CRYPTO_FG_SIGN | CRYPTO_FG_SIGN_ATOMIC |
	    CRYPTO_FG_VERIFY | CRYPTO_FG_VERIFY_ATOMIC,
	    RSA_MIN_KEY_LEN, RSA_MAX_KEY_LEN, CRYPTO_KEYSIZE_UNIT_IN_BITS},

	/* SHA512_RSA_PKCS */
	{SUN_CKM_SHA512_RSA_PKCS, CRYPTO_SHA512_RSA_PKCS,
	    CRYPTO_FG_SIGN | CRYPTO_FG_SIGN_ATOMIC |
	    CRYPTO_FG_VERIFY | CRYPTO_FG_VERIFY_ATOMIC,
	    RSA_MIN_KEY_LEN, RSA_MAX_KEY_LEN, CRYPTO_KEYSIZE_UNIT_IN_BITS}

};

static void rsaprov_provider_status(crypto_provider_handle_t, uint_t *);

static crypto_control_ops_t rsa_control_ops = {
	rsaprov_provider_status		/* provider_status */
};

static crypto_ctx_ops_t rsa_ctx_ops = {
	NULL,				/* create_ctx_template */
	rsaprov_free_context		/* free_context */
};

/*
 * The RSA mechanisms do not have multiple-part cipher operations.
 * So, the update and final routines are set to NULL.
 */
static crypto_cipher_ops_t rsa_cipher_ops = {
	rsaprov_common_init,		/* encrypt_init */
	rsaprov_encrypt,		/* encrypt */
	NULL,				/* encrypt_update */
	NULL,				/* encrypt_final */
	rsaprov_encrypt_atomic,		/* encrypt_atomic */
	rsaprov_common_init,		/* decrypt_init */
	rsaprov_decrypt,		/* decrypt */
	NULL,				/* decrypt_update */
	NULL,				/* decrypt_final */
	rsaprov_decrypt_atomic		/* decrypt_atomic */
};

/*
 * We use the same routine for sign_init and sign_recover_init fields
 * as they do the same thing. Same holds for sign and sign_recover fields,
 * and sign_atomic and sign_recover_atomic fields.
 */
static crypto_sign_ops_t rsa_sign_ops = {
	rsaprov_sign_verify_common_init,	/* sign_init */
	rsaprov_sign,			/* sign */
	rsaprov_sign_update,		/* sign_update */
	rsaprov_sign_final,		/* sign_final */
	rsaprov_sign_atomic,		/* sign_atomic */
	rsaprov_sign_verify_common_init,	/* sign_recover_init */
	rsaprov_sign,			/* sign_recover */
	rsaprov_sign_atomic		/* sign_recover_atomic */
};

/*
 * We use the same routine (rsa_sign_verify_common_init) for verify_init
 * and verify_recover_init fields as they do the same thing.
 */
static crypto_verify_ops_t rsa_verify_ops = {
	rsaprov_sign_verify_common_init,	/* verify_init */
	rsaprov_verify,			/* verify */
	rsaprov_verify_update,		/* verify_update */
	rsaprov_verify_final,		/* verify_final */
	rsaprov_verify_atomic,		/* verify_atomic */
	rsaprov_sign_verify_common_init,	/* verify_recover_init */
	rsaprov_verify_recover,		/* verify_recover */
	rsaprov_verify_recover_atomic	/* verify_recover_atomic */
};

static void rsaprov_POST(int *);

static crypto_fips140_ops_t rsa_fips140_ops = {
	rsaprov_POST			/* fips140_post */
};

static crypto_ops_t rsa_crypto_ops = {
	&rsa_control_ops,		/* control_ops */
	NULL,				/* digest_ops */
	&rsa_cipher_ops,		/* cipher_ops */
	NULL,				/* mac_ops */
	&rsa_sign_ops,			/* sign_ops */
	&rsa_verify_ops,		/* verify_ops */
	NULL,				/* dual_ops */
	NULL,				/* dual_cipher_mac_ops */
	NULL,				/* random_ops */
	NULL,				/* session_ops */
	NULL,				/* object_ops */
	NULL,				/* key_ops */
	NULL,				/* provider_ops */
	&rsa_ctx_ops,			/* ctx_ops */
	NULL,				/* mech_ops (v2) */
	NULL,				/* nostore_key_ops (v3) */
	&rsa_fips140_ops		/* fips140_ops (v4) */
};

static crypto_provider_info_t rsa_prov_info = {
	CRYPTO_SPI_VERSION_4,
	"RSA Software Provider",
	CRYPTO_SW_PROVIDER,
	{&modlinkage},
	NULL,
	&rsa_crypto_ops,
	sizeof (rsa_mech_info_tab)/sizeof (crypto_mech_info_t),
	rsa_mech_info_tab
};

static crypto_kcf_provider_handle_t	rsa_prov_handle;
static boolean_t			is_rsa_registered = B_FALSE;

int
_init(void)
{
	int ret;

	if ((ret = mod_install(&modlinkage)) != 0)
		return (ret);

	/* Register with KCF.  If the registration fails, remove the module. */
	if (ret = crypto_register_provider(&rsa_prov_info, &rsa_prov_handle)) {
		(void) mod_remove(&modlinkage);
		if (ret == CRYPTO_MECH_NOT_SUPPORTED) {
			return (ENOTSUP);
		} else {
			return (EACCES);
		}
	}
	is_rsa_registered = B_TRUE;

	return (0);
}

int
_fini(void)
{
	/* Unregister from KCF if module is registered */
	if (is_rsa_registered) {
		if (crypto_unregister_provider(rsa_prov_handle))
			return (EBUSY);

		is_rsa_registered = B_FALSE;
	}

	return (mod_remove(&modlinkage));
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

/* ARGSUSED */
static void
rsaprov_provider_status(crypto_provider_handle_t provider, uint_t *status)
{
	*status = CRYPTO_PROVIDER_READY;
}

/*
 * RSA Power-On Self-Test
 */
void
rsaprov_POST(int *rc)
{
	*rc = fips_rsa_post();
}
