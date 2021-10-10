/*
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <pkcs11-glue.h>
#include <sshincludes.h>
#include <sshgenmp.h>
#include <sshencode.h>
#include <isakmp.h>
#include <cryptoutil.h>

/*
 * Error-code return remapping.
 */
SshCryptoStatus
ckrv_to_sshcryptostatus(CK_RV pkcs11_rc)
{
	switch (pkcs11_rc) {
	case CKR_OK:
		return (SSH_CRYPTO_OK);
	case CKR_SIGNATURE_INVALID:
		return (SSH_CRYPTO_SIGNATURE_CHECK_FAILED);
	}
	/* Return a catch-all. */
	pkcs11_error(pkcs11_rc, "PKCS#11 error not converted to cryptostatus");
	return (SSH_CRYPTO_PROVIDER_ERROR);
}

SshCryptoStatus
pkcs11_dsa_public_key_verify(pkcs11_key_t *p11k, uint8_t *data, size_t data_len,
    uint8_t *signature, size_t signature_len)
{
	pkcs11_inst_t *p11i = p11k->p11k_p11i;
	CK_RV pkcs11_rc;

	pkcs11_rc = p11f->C_VerifyInit(p11i->p11i_session, dsa,
	    p11k->p11k_p11pub);
	if (pkcs11_rc != CKR_OK)
		return (ckrv_to_sshcryptostatus(pkcs11_rc));

	pkcs11_rc = p11f->C_Verify(p11i->p11i_session, data, data_len,
	    signature, signature_len);

	return (ckrv_to_sshcryptostatus(pkcs11_rc));
}

SshCryptoStatus
pkcs11_dsa_private_key_sign(pkcs11_key_t *p11k, uint8_t *data, size_t data_len,
    uint8_t *signature_buffer, boolean_t use_pkcs11_hash)
{
	pkcs11_inst_t *p11i = p11k->p11k_p11i;
	CK_RV pkcs11_rc;
	CK_ULONG pkcs11_len;
	CK_MECHANISM dsa_sha1 = {CKM_DSA_SHA1, NULL, 0};

	pkcs11_rc = p11f->C_SignInit(p11i->p11i_session,
	    use_pkcs11_hash ? &dsa_sha1 : dsa, p11k->p11k_p11priv);
	if (pkcs11_rc != CKR_OK)
		return (ckrv_to_sshcryptostatus(pkcs11_rc));

	pkcs11_len = p11k->p11k_bufsize;

	pkcs11_rc = p11f->C_Sign(p11i->p11i_session, data, data_len,
	    signature_buffer, &pkcs11_len);

	if (pkcs11_rc != CKR_OK)
		return (ckrv_to_sshcryptostatus(pkcs11_rc));

	if (pkcs11_len != p11k->p11k_bufsize)
		return (SSH_CRYPTO_DATA_TOO_SHORT);

	return (SSH_CRYPTO_OK);
}

static Boolean
pkcs11_rsa_public_key_verify(pkcs11_key_t *p11k, uint8_t *data, size_t data_len,
    uint8_t *signature, size_t signature_len)
{
	pkcs11_inst_t *p11i = p11k->p11k_p11i;
	CK_RV pkcs11_rc;
	uint8_t recovered[512];
	CK_ULONG reclen = 512;
	uint_t ssh_rc;
	boolean_t rc;

	pkcs11_rc = p11f->C_VerifyRecoverInit(p11i->p11i_session, rsa_pkcs1,
	    p11k->p11k_p11pub);
	if (pkcs11_rc != CKR_OK)
		return (ckrv_to_sshcryptostatus(pkcs11_rc));

	pkcs11_rc = p11f->C_VerifyRecover(p11i->p11i_session,
	    (uint8_t *)signature, signature_len, recovered, &reclen);
	if (pkcs11_rc != CKR_OK)
		return (ckrv_to_sshcryptostatus(pkcs11_rc));

	if (reclen != data_len)
		return (SSH_CRYPTO_DATA_TOO_SHORT);

	if (memcmp(data, recovered, reclen) != 0)
		return (SSH_CRYPTO_SIGNATURE_CHECK_FAILED);

	return (SSH_CRYPTO_OK);
}

static Boolean
pkcs11_rsa_private_key_sign(pkcs11_key_t *p11k, uint8_t *data, size_t data_len,
    uint8_t *signature_buffer, boolean_t use_raw)
{
	pkcs11_inst_t *p11i = p11k->p11k_p11i;
	CK_RV pkcs11_rc;
	CK_ULONG pkcs11_len;

	pkcs11_rc = p11f->C_SignRecoverInit(p11i->p11i_session,
	    use_raw ? rsa_raw : rsa_pkcs1, p11k->p11k_p11priv);
	if (pkcs11_rc != CKR_OK)
		return (ckrv_to_sshcryptostatus(pkcs11_rc));

	pkcs11_len = p11k->p11k_bufsize;
	pkcs11_rc = p11f->C_SignRecover(p11i->p11i_session,
	    data, data_len, signature_buffer, &pkcs11_len);

	if (pkcs11_rc != CKR_OK)
		return (ckrv_to_sshcryptostatus(pkcs11_rc));

	if (pkcs11_len != p11k->p11k_bufsize)
		return (SSH_CRYPTO_DATA_TOO_SHORT);

	return (SSH_CRYPTO_OK);
}

static SshCryptoStatus
pkcs11_rsa_encrypt(pkcs11_key_t *p11k, uint8_t *plaintext,
    size_t plaintext_len, uint8_t **ciphertext, size_t *ciphertext_len)
{
	pkcs11_inst_t *p11i = p11k->p11k_p11i;
	CK_RV pkcs11_rc;
	CK_ULONG pkcs11_len;

	pkcs11_len = p11k->p11k_bufsize;

	/*
	 * The +1 indicates that the plaintext must be one less than the
	 * output buffer for RSA encryption.
	 */
	if (pkcs11_len < plaintext_len + 1)
		return (SSH_CRYPTO_KEY_TOO_SHORT);

	*ciphertext_len = 0;
	*ciphertext = ssh_malloc(pkcs11_len);
	if (*ciphertext == NULL)
		return (SSH_CRYPTO_NO_MEMORY);

	pkcs11_rc = p11f->C_EncryptInit(p11i->p11i_session, rsa_pkcs1,
	    p11k->p11k_p11pub);
	if (pkcs11_rc != CKR_OK) {
		pkcs11_error(pkcs11_rc, "C_EncryptInit");
		ssh_free(*ciphertext);
		*ciphertext = NULL;
		return (ckrv_to_sshcryptostatus(pkcs11_rc));
	}

	pkcs11_rc = p11f->C_Encrypt(p11i->p11i_session,
	    plaintext, plaintext_len, *ciphertext, &pkcs11_len);
	if (pkcs11_rc != CKR_OK) {
		pkcs11_error(pkcs11_rc, "C_Encrypt");
		ssh_free(*ciphertext);
		*ciphertext = NULL;
		return (ckrv_to_sshcryptostatus(pkcs11_rc));
	}

	*ciphertext_len = pkcs11_len;
	return (SSH_CRYPTO_OK);
}

static SshCryptoStatus
pkcs11_rsa_decrypt(pkcs11_key_t *p11k, uint8_t *ciphertext,
    size_t ciphertext_len, uint8_t **plaintext, size_t *plaintext_len)
{
	pkcs11_inst_t *p11i = p11k->p11k_p11i;
	CK_RV pkcs11_rc;
	CK_ULONG pkcs11_len;

	/* Use +1 for the same reson we check for +1 on the encrypt side. */
	*plaintext_len = 0;
	pkcs11_len = p11k->p11k_bufsize + 1;
	*plaintext = ssh_malloc(pkcs11_len);
	if (*plaintext == NULL)
		return (SSH_CRYPTO_NO_MEMORY);

	pkcs11_rc = p11f->C_DecryptInit(p11i->p11i_session, rsa_pkcs1,
	    p11k->p11k_p11priv);
	if (pkcs11_rc != CKR_OK) {
		pkcs11_error(pkcs11_rc, "C_DecryptInit");
		ssh_free(*plaintext);
		*plaintext = NULL;
		return (ckrv_to_sshcryptostatus(pkcs11_rc));
	}

	pkcs11_rc = p11f->C_Decrypt(p11i->p11i_session, ciphertext,
	    ciphertext_len, *plaintext, &pkcs11_len);
	if (pkcs11_rc != CKR_OK) {
		pkcs11_error(pkcs11_rc, "C_Decrypt");
		ssh_free(*plaintext);
		*plaintext = NULL;
		return (ckrv_to_sshcryptostatus(pkcs11_rc));
	}
	*plaintext_len = pkcs11_len;

	return (SSH_CRYPTO_OK);
}

/*
 * Re-institute leading zeroes.  Fortunately, there's the right amount of
 * space, assuming the PKCS#11 library doesn't realloc (and it shouldn't,
 * lest the library be in gross violation of PKCS#11's philosophy that the
 * caller take care of all memory allocations).
 */
static void
restore_leading_zeroes(uint8_t *space, int full_len, int reduced_len)
{
	int number_of_zeroes = full_len - reduced_len;

	(void) memmove(space + number_of_zeroes, space, reduced_len);
	(void) memset(space, 0, number_of_zeroes);
}

static SshCryptoStatus
pkcs11_dh_setup_native(pkcs11_group_t *p11g, uint8_t **dh_private,
    size_t *dhpriv_len, uint8_t **exchange_buffer, size_t *eb_len)
{
	SshCryptoStatus rc;
	CK_RV pkcs11_rc;
	CK_OBJECT_HANDLE dh_privobj, dh_pubobj;
	CK_SESSION_HANDLE sess = p11g->p11g_p11i->p11i_session;
	CK_ATTRIBUTE privattrs[MAX_ATTRS];
	CK_ATTRIBUTE bignum_vec = {0, NULL, 0};
	CK_ULONG key_bytes = p11g->p11g_bufsize;

	pkcs11_rc = p11f->C_GenerateKeyPair(sess, dh_generate,
	    p11g->p11g_attrs, p11g->p11g_attrcount, NULL, 0,
	    &dh_pubobj, &dh_privobj);
	if (pkcs11_rc != CKR_OK) {
		pkcs11_error(pkcs11_rc, "dh_setup_native: C_GenerateKeyPair");
		ssh_policy_sun_info("PKCS#11 Error generating D-H Values: %s",
		    pkcs11_strerror(pkcs11_rc));
		return (ckrv_to_sshcryptostatus(pkcs11_rc));
	}

	*eb_len = p11g->p11g_bufsize;
	*exchange_buffer = ssh_malloc(*eb_len);
	if (*exchange_buffer == NULL) {
		rc = SSH_CRYPTO_NO_MEMORY;
		goto bail;
	}
	ATTR_INIT(bignum_vec, CKA_VALUE, *exchange_buffer, *eb_len);
	pkcs11_rc = p11f->C_GetAttributeValue(sess, dh_pubobj, &bignum_vec, 1);
	if (pkcs11_rc != CKR_OK) {
		pkcs11_error(pkcs11_rc, "dh_setup_native: C_GetAttributeValue");
		ssh_policy_sun_info("PKCS#11 Error: %s",
		    pkcs11_strerror(pkcs11_rc));
		rc = ckrv_to_sshcryptostatus(pkcs11_rc);
		goto bail;
	}

	*dhpriv_len = key_bytes;
	*dh_private = ssh_malloc(key_bytes);
	if (*dh_private == NULL) {
		rc = SSH_CRYPTO_NO_MEMORY;
		goto bail;
	}
	ATTR_INIT(bignum_vec, CKA_VALUE, *dh_private, key_bytes);
	pkcs11_rc = p11f->C_GetAttributeValue(sess, dh_privobj, &bignum_vec, 1);
	if (pkcs11_rc != CKR_OK) {
		pkcs11_error(pkcs11_rc, "dh_setup_native: "
		    "second C_GetAttributeValue");
		ssh_policy_sun_info("PKCS#11 Error: %s",
		    pkcs11_strerror(pkcs11_rc));
		rc = ckrv_to_sshcryptostatus(pkcs11_rc);
		goto bail;
	} else {
		rc = SSH_CRYPTO_OK;
	}

	if (bignum_vec.ulValueLen < key_bytes) {
		restore_leading_zeroes(*dh_private, key_bytes,
		    bignum_vec.ulValueLen);
	} else if (bignum_vec.ulValueLen > key_bytes) {
		/*
		 * Yike!  Memory-overscribble notwithstanding, we should
		 * return an error in this case.
		 */
		rc = SSH_CRYPTO_TEST_PK;	/* Internal PK error. */
	}

bail:
	if (rc != SSH_CRYPTO_OK) {
		ssh_free(*exchange_buffer);
		ssh_free(*dh_private);
		/* Be pedantic in case of brain-damage in OEM code. */
		*exchange_buffer = NULL;
		*dh_private = NULL;
		*eb_len = 0;
		*dhpriv_len = 0;
	}
	(void) p11f->C_DestroyObject(sess, dh_privobj);
	(void) p11f->C_DestroyObject(sess, dh_pubobj);
	return (rc);
}

static SshCryptoStatus
pkcs11_dh_setup_rsa(pkcs11_group_t *p11g, uint8_t **dh_private,
    size_t *dhpriv_len, uint8_t **exchange_buffer, size_t *eb_len)
{
	CK_OBJECT_HANDLE rsa_key;
	CK_SESSION_HANDLE sess = p11g->p11g_p11i->p11i_session;
	SshMPIntegerStruct x;	/* In the classic DH sense of "x" */
	uint8_t *x_buf;
	CK_RV pkcs11_rc;
	CK_ULONG pkcs11_len;

	/*
	 * Generate an RSA public key, put it in dh_private.
	 */

	/* Allocate an exchange buffer. */
	pkcs11_len = p11g->p11g_bufsize;
	*exchange_buffer = ssh_malloc(pkcs11_len);
	if (*exchange_buffer == NULL)
		return (SSH_CRYPTO_NO_MEMORY);

	/* Get random bits.  (Question is, how many?) */
	x_buf = ssh_malloc(BIGINT_BUFLEN);
	if (x_buf == NULL) {
		ssh_free(*exchange_buffer);
		return (SSH_CRYPTO_NO_MEMORY);
	}
	ssh_mprz_init(&x);
	/* ssh_mp_random_integer() takes a length in bits, hence << 3. */
	ssh_mprz_random_integer(&x, p11g->p11g_bufsize << 3);
	ssh_mprz_get_buf(x_buf, p11g->p11g_bufsize, &x);

	ssh_mprz_clear(&x);

	p11g->p11g_attrs[0].pValue = x_buf;
	p11g->p11g_attrs[0].ulValueLen = p11g->p11g_bufsize;

	pkcs11_rc = p11f->C_CreateObject(sess, p11g->p11g_attrs,
	    p11g->p11g_attrcount, &rsa_key);
	if (pkcs11_rc != CKR_OK) {
		ssh_policy_sun_info("PKCS#11 Error generating D-H Values: %s",
		    pkcs11_strerror(pkcs11_rc));
		pkcs11_error(pkcs11_rc, "dh_setup: C_CreateObject");
		ssh_free(x_buf);
		ssh_free(*exchange_buffer);
		return (ckrv_to_sshcryptostatus(pkcs11_rc));
	}

	/* RSA encrypt generator ("2"), and put results in exchange_buffer. */

	pkcs11_rc = p11f->C_EncryptInit(sess, rsa_raw, rsa_key);
	if (pkcs11_rc != CKR_OK) {
		ssh_policy_sun_info("PKCS#11 Error: %s",
		    pkcs11_strerror(pkcs11_rc));
		pkcs11_error(pkcs11_rc, "C_EncryptInit");
		(void) p11f->C_DestroyObject(sess, rsa_key);
		ssh_free(x_buf);
		ssh_free(*exchange_buffer);
		return (ckrv_to_sshcryptostatus(pkcs11_rc));
	}
	pkcs11_rc = p11f->C_Encrypt(sess, p11g->p11g_g, p11g->p11g_gsize,
	    *exchange_buffer, &pkcs11_len);
	if (pkcs11_rc != CKR_OK) {
		ssh_policy_sun_info("PKCS#11 Error: %s",
		    pkcs11_strerror(pkcs11_rc));
		pkcs11_error(pkcs11_rc, "C_Encrypt");
		(void) p11f->C_DestroyObject(sess, rsa_key);
		ssh_free(x_buf);
		ssh_free(*exchange_buffer);
		return (ckrv_to_sshcryptostatus(pkcs11_rc));
	}

	(void) p11f->C_DestroyObject(sess, rsa_key);
	*eb_len = pkcs11_len;
	*dh_private = x_buf;
	*dhpriv_len = p11g->p11g_bufsize;
	return (SSH_CRYPTO_OK);
}

static SshCryptoStatus
pkcs11_dh_agree_native(pkcs11_group_t *p11g, uint8_t *exchange_buffer,
    size_t exchange_buffer_len, uint8_t *secret_buffer,
    size_t secret_buffer_len, uint8_t **return_buf, size_t *rb_len)
{
	SshCryptoStatus rc;
	CK_RV pkcs11_rc;
	CK_MECHANISM dh_derive = {CKM_DH_PKCS_DERIVE, NULL, 0};
	CK_SESSION_HANDLE sess = p11g->p11g_p11i->p11i_session;
	CK_OBJECT_HANDLE dh_privobj = NULL, shared_secret = NULL;
	CK_ATTRIBUTE privattrs[MAX_ATTRS], attrs[MAX_ATTRS];
	CK_ULONG pacount = 0, attrcount = 0;
	CK_ULONG key_bytes = p11g->p11g_bufsize;
	CK_OBJECT_CLASS oclass = CKO_PRIVATE_KEY;
	CK_KEY_TYPE keytype = CKK_DH;
	CK_BBOOL true = TRUE;

	*return_buf = NULL;
	*rb_len = 0;

	dh_derive.pParameter = exchange_buffer;
	dh_derive.ulParameterLen = exchange_buffer_len;

	/*
	 * NOTE:  It would be nice if there were a way to preserve the
	 * private object from dh_setup_native() and reuse it here.
	 * Unfortunately, that requires modifying either the proxy-key
	 * interface, or the higher-level caller to do the right thing.
	 * It would save a few microseconds, for sure.
	 */
	ATTR_INIT(privattrs[pacount], CKA_CLASS, &oclass, sizeof (oclass));
	pacount++;
	ATTR_INIT(privattrs[pacount], CKA_KEY_TYPE, &keytype, sizeof (keytype));
	pacount++;
	ATTR_INIT(privattrs[pacount], CKA_VALUE, secret_buffer,
	    secret_buffer_len);
	pacount++;
	ATTR_INIT(privattrs[pacount], CKA_DERIVE, &true, sizeof (true));
	pacount++;
	ATTR_INIT(privattrs[pacount], CKA_PRIME, &p11g->p11g_n, key_bytes);
	pacount++;
	ATTR_INIT(privattrs[pacount], CKA_BASE, &p11g->p11g_g,
	    p11g->p11g_gsize);
	pacount++;

	pkcs11_rc = p11f->C_CreateObject(sess, privattrs, pacount, &dh_privobj);
	if (pkcs11_rc != CKR_OK) {
		pkcs11_error(pkcs11_rc, "dh_agree_native: C_CreateObject");
		rc = ckrv_to_sshcryptostatus(pkcs11_rc);
		goto bail;
	}

	oclass = CKK_GENERIC_SECRET;
	ATTR_INIT(attrs[attrcount], CKA_KEY_TYPE, &oclass, sizeof (oclass));
	attrcount++;

	pkcs11_rc = p11f->C_DeriveKey(sess, &dh_derive, dh_privobj,
	    attrs, attrcount, &shared_secret);
	if (pkcs11_rc != CKR_OK) {
		pkcs11_error(pkcs11_rc, "dh_agree_native: C_DeriveKey");
		rc = ckrv_to_sshcryptostatus(pkcs11_rc);
		goto bail;
	}

	*rb_len = key_bytes;
	*return_buf = ssh_malloc(*rb_len);
	if (*return_buf == NULL) {
		rc = SSH_CRYPTO_NO_MEMORY;
		*rb_len = 0;
		goto bail;
	}

	ATTR_INIT(attrs[0], CKA_VALUE, *return_buf, key_bytes);
	pkcs11_rc = p11f->C_GetAttributeValue(sess, shared_secret, attrs, 1);
	if (pkcs11_rc != CKR_OK) {
		rc = ckrv_to_sshcryptostatus(pkcs11_rc);
		pkcs11_error(pkcs11_rc, "dh_agree_native: C_GetAttributeValue");
		ssh_free(*return_buf);
		*rb_len = 0;
		*return_buf = NULL;
	} else if (attrs[0].ulValueLen < key_bytes) {
		rc = SSH_CRYPTO_OK;
		restore_leading_zeroes(*return_buf, key_bytes,
		    attrs[0].ulValueLen);
	} else if (attrs[0].ulValueLen > key_bytes) {
		/*
		 * Yike!  Memory-overscribble notwithstanding, we should
		 * return an error in this case.
		 */
		rc = SSH_CRYPTO_TEST_PK;	/* Internal PK error. */
		ssh_free(*return_buf);
		*rb_len = 0;
		*return_buf = NULL;
	} else {
		rc = SSH_CRYPTO_OK;	/* Everything's cool if we're here. */
	}

bail:
	(void) p11f->C_DestroyObject(sess, dh_privobj);
	(void) p11f->C_DestroyObject(sess, shared_secret);
	return (rc);
}

static SshCryptoStatus
pkcs11_dh_agree_rsa(pkcs11_group_t *p11g, uint8_t *exchange_buffer,
    size_t exchange_buffer_len, uint8_t *secret_buffer,
    size_t secret_buffer_len, uint8_t **return_buf, size_t *rb_len)
{
	CK_OBJECT_HANDLE rsa_key;
	CK_SESSION_HANDLE sess = p11g->p11g_p11i->p11i_session;
	CK_ULONG pkcs11_len;
	CK_RV pkcs11_rc;
	uint8_t *new_buf;

	/* See ssh_dlp_diffie_hellman_final() */

	if (exchange_buffer_len > p11g->p11g_bufsize ||
	    secret_buffer_len > p11g->p11g_bufsize)
		return (SSH_CRYPTO_KEY_TOO_SHORT);

	*rb_len = p11g->p11g_bufsize;
	new_buf = ssh_calloc(*rb_len, 1);
	if (new_buf == NULL)
		return (SSH_CRYPTO_NO_MEMORY);

	/* Setup the RSA public key again. */
	p11g->p11g_attrs[0].pValue = secret_buffer;
	p11g->p11g_attrs[0].ulValueLen = secret_buffer_len;

	pkcs11_rc = p11f->C_CreateObject(sess, p11g->p11g_attrs,
	    p11g->p11g_attrcount, &rsa_key);
	if (pkcs11_rc != CKR_OK) {
		pkcs11_error(pkcs11_rc, "dh_agree: C_CreateObject");
		ssh_free(new_buf);
		return (ckrv_to_sshcryptostatus(pkcs11_rc));
	}

	/*
	 * RSA encrypt the exchange_buffer, and put results in the
	 * return buffer.
	 */
	pkcs11_rc = p11f->C_EncryptInit(sess, rsa_raw, rsa_key);
	if (pkcs11_rc != CKR_OK) {
		pkcs11_error(pkcs11_rc, "C_EncryptInit");
		(void) p11f->C_DestroyObject(sess, rsa_key);
		ssh_free(new_buf);
		return (ckrv_to_sshcryptostatus(pkcs11_rc));
	}
	pkcs11_len = *rb_len;
	pkcs11_rc = p11f->C_Encrypt(sess, exchange_buffer, exchange_buffer_len,
	    new_buf, &pkcs11_len);
	if (pkcs11_rc != CKR_OK) {
		pkcs11_error(pkcs11_rc, "C_Encrypt");
		(void) p11f->C_DestroyObject(sess, rsa_key);
		ssh_free(new_buf);
		return (ckrv_to_sshcryptostatus(pkcs11_rc));
	}
	*return_buf = new_buf;
	(void) p11f->C_DestroyObject(sess, rsa_key);

	return (SSH_CRYPTO_OK);
}

void
pkcs11_public_key_free(void *my_context)
{
	pkcs11_key_t *p11k = (pkcs11_key_t *)my_context;
	pkcs11_inst_t *p11i = p11k->p11k_p11i;
	CK_RV pkcs11_rc;

	if (p11k->p11k_origpub != NULL) {
		ssh_public_key_free(p11k->p11k_origpub);
		/*
		 * Only destroy the object if it was created from an
		 * on-disk key.
		 */
		pkcs11_rc = p11f->C_DestroyObject(p11i->p11i_session,
		    p11k->p11k_p11pub);
		if (pkcs11_rc != CKR_OK)
			pkcs11_error(pkcs11_rc,
			    "pkcs11_public_key_free: C_DestroyObject");
	}
	P11I_REFRELE(p11i);
	ssh_free(p11k);
}

void
pkcs11_private_key_free(void *my_context)
{
	pkcs11_key_t *p11k = (pkcs11_key_t *)my_context;
	pkcs11_inst_t *p11i = p11k->p11k_p11i;
	CK_RV pkcs11_rc;

	if (p11k->p11k_origpriv != NULL) {
		ssh_private_key_free(p11k->p11k_origpriv);
		/*
		 * Only destroy the object if it was created from an
		 * on-disk key.
		 */
		pkcs11_rc = p11f->C_DestroyObject(p11i->p11i_session,
		    p11k->p11k_p11priv);
		if (pkcs11_rc != CKR_OK)
			pkcs11_error(pkcs11_rc,
			    "pkcs11_private_key_free: C_DestroyObject");
	}
	P11I_REFRELE(p11i);
	ssh_free(p11k);
}

void
pkcs11_dh_free(void *my_context)
{
	pkcs11_group_t *p11g = (pkcs11_group_t *)my_context;

	P11I_REFRELE(p11g->p11g_p11i);
	ssh_free(p11g->p11g_attrs);
	ssh_pk_group_free(p11g->p11g_group);
	ssh_free(p11g);
}

void
pkcs11_ecp_free(void *my_context)
{
	pkcs11_ecp_group_t *p11ecpg = (pkcs11_ecp_group_t *)my_context;

	P11I_REFRELE(p11ecpg->p11ecpg_p11i);
	ssh_free(p11ecpg->p11ecpg_attrs);
	ssh_free(p11ecpg);
}

/*
 * If this function goes asynch, return a handle of some sort.
 */
SshOperationHandle
pkcs11_public_key_dispatch(SshProxyOperationId op_id, SshProxyRGFId rgf_id,
    SshProxyKeyHandle handle, const uint8_t *input_data, size_t input_data_len,
    SshProxyReplyCB callback, void *callback_context, void *my_context)
{
	pkcs11_key_t *p11k = (pkcs11_key_t *)my_context;
	uint8_t *data, *signature, *encrypted_data;
	size_t data_len, signature_len, encrypted_data_len, decode_rc;
	SshCryptoStatus rc;

	switch (op_id) {
	case SSH_DSA_PUB_VERIFY:
	case SSH_RSA_PUB_VERIFY:
		decode_rc = ssh_decode_array(input_data, input_data_len,
		    SSH_FORMAT_UINT32_STR_NOCOPY, &data, &data_len,
		    SSH_FORMAT_UINT32_STR_NOCOPY, &signature, &signature_len,
		    SSH_FORMAT_END);
		if (decode_rc != input_data_len) {
			pkcs11_error(0,
			    "pkcs11_public_key: ssh_decode_array failed");
			rc = SSH_CRYPTO_INTERNAL_ERROR;
			goto bail;
		}
		if (op_id == SSH_DSA_PUB_VERIFY) {
			rc = pkcs11_dsa_public_key_verify(p11k, data, data_len,
			    signature, signature_len);
		} else {
			rc = pkcs11_rsa_public_key_verify(p11k, data, data_len,
			    signature, signature_len);
		}
bail:
		callback(rc, NULL, 0, callback_context);
		return (NULL);

	case SSH_RSA_PUB_ENCRYPT:
		rc = pkcs11_rsa_encrypt(p11k, (uint8_t *)input_data,
		    input_data_len, &encrypted_data, &encrypted_data_len);
		callback(rc, encrypted_data, encrypted_data_len,
		    callback_context);
		/* FALLTHRU */
		ssh_free(encrypted_data);
	}

	return (NULL);
}

SshOperationHandle
pkcs11_private_key_dispatch(SshProxyOperationId op_id, SshProxyRGFId rgf_id,
    SshProxyKeyHandle handle, const uint8_t *input_data, size_t input_data_len,
    SshProxyReplyCB callback, void *callback_context, void *my_context)
{
	pkcs11_key_t *p11k = (pkcs11_key_t *)my_context;
	uint8_t *signature_buffer = NULL, *plaintext, *hashed_input;
	size_t signature_buffer_len = 0, plaintext_len, hashed_input_len;
	SshCryptoStatus rc;
	boolean_t rgf_did_something = _B_TRUE;

	switch (op_id) {
	case SSH_DSA_PRV_SIGN:
	case SSH_RSA_PRV_SIGN:

		signature_buffer = ssh_malloc(p11k->p11k_bufsize);
		if (signature_buffer == NULL) {
			rc = SSH_CRYPTO_NO_MEMORY;
			goto bail;
		}

		signature_buffer_len = p11k->p11k_bufsize;
		if (op_id == SSH_DSA_PRV_SIGN) {
			rc = pkcs11_dsa_private_key_sign(p11k,
			    (uint8_t *)input_data, input_data_len,
			    signature_buffer, (rgf_id == SSH_DSA_NIST_SHA1));
		} else {
			/*
			 * If this function is reached from IKE itself, the
			 * hashing was done in advance, and the
			 * proxy_key_rgf_sign() will be a NOP.  If this
			 * function was reached from the ikecert(1m) certlocal
			 * command, the proxy_key_rgf_sign() will actually
			 * hash things down.
			 *
			 * Unlike the DSA case, the PKCS#11 RSA-with-hash
			 * primitive behaves slightly differently than the
			 * libike/SafeNet code seems to.  Instead of trying
			 * to work this out, we simply comply with
			 * libike/SafeNet's method, and let PKCS#11 do the
			 * bignum heavy-lifting.
			 */
			rc = ssh_proxy_key_rgf_sign(op_id, rgf_id,
			    p11k->p11k_bufsize, input_data, input_data_len,
			    &hashed_input, &hashed_input_len);
			if (rc != SSH_CRYPTO_OK) {
				goto bail;
			} else if (hashed_input == NULL) {
				/*
				 * Reassign input_data{,_len} for the
				 * signature function.
				 */
				hashed_input_len = input_data_len;
				hashed_input = (uint8_t *)input_data;
				rgf_did_something = _B_FALSE;
			}

			rc = pkcs11_rsa_private_key_sign(p11k,
			    hashed_input, hashed_input_len, signature_buffer,
			    rgf_did_something);
			if (rgf_did_something)
				ssh_free(hashed_input);
		}
bail:
		callback(rc, signature_buffer, signature_buffer_len,
		    callback_context);
		ssh_free(signature_buffer);
		return (NULL);

	case SSH_RSA_PRV_DECRYPT:
		rc = pkcs11_rsa_decrypt(p11k, (uint8_t *)input_data,
		    input_data_len, &plaintext, &plaintext_len);
		callback(rc, plaintext, plaintext_len, callback_context);
		ssh_free(plaintext);
		/* FALLTHRU */
	}

	return (NULL);
}

SshOperationHandle
pkcs11_dh_dispatch(SshProxyOperationId op_id, SshProxyRGFId rgf_id,
    SshProxyKeyHandle handle, const uint8_t *input_data, size_t input_data_len,
    SshProxyReplyCB callback, void *callback_context, void *my_context)
{
	pkcs11_group_t *p11g = (pkcs11_group_t *)my_context;
	uint8_t *dh_private = NULL, *exchange_buffer = NULL, *return_buf;
	size_t dhpriv_len, eb_len;
	SshCryptoStatus rc;
	size_t decode_rc, rb_len;

	switch (op_id) {
	case SSH_DH_SETUP:
		if (p11g->p11g_use_rsa)
			rc = pkcs11_dh_setup_rsa(p11g, &dh_private, &dhpriv_len,
			    &exchange_buffer, &eb_len);
		else
			rc = pkcs11_dh_setup_native(p11g, &dh_private,
			    &dhpriv_len, &exchange_buffer, &eb_len);

		/* PKCS#11 provider failed */
		if (rc != SSH_CRYPTO_OK)
			return (NULL);

		/*
		 * Encode return_buf, use allocation (UINT32_STR), instead of
		 * reference (UINT32_STR_NOCOPY) in preparation of any
		 * asynchrony we introduce later.
		 */
		rb_len = ssh_encode_array_alloc(&return_buf,
		    SSH_FORMAT_UINT32_STR, exchange_buffer, eb_len,
		    SSH_FORMAT_UINT32_STR, dh_private, dhpriv_len,
		    SSH_FORMAT_END);
		if (rb_len <= 0) {
			pkcs11_error(0,
			    "pkcs11_dh_setup: ssh_encode_array failed");
			rb_len = 0;
			return_buf = NULL;
		}
		ssh_free(dh_private);
		ssh_free(exchange_buffer);
		break;
	case SSH_DH_AGREE:
		/*
		 * Using NOCOPY means I don't have to free exchange_buffer
		 * and dh_private.
		 */
		decode_rc = ssh_decode_array(input_data, input_data_len,
		    SSH_FORMAT_UINT32_STR_NOCOPY, &exchange_buffer, &eb_len,
		    SSH_FORMAT_UINT32_STR_NOCOPY, &dh_private, &dhpriv_len,
		    SSH_FORMAT_END);
		if (decode_rc != input_data_len) {
			pkcs11_error(0,
			    "pkcs11_dh_agree: ssh_decode_array failed");
			rc = SSH_CRYPTO_INTERNAL_ERROR;
			return_buf = NULL;
			break;	/* out of switch */
		}

		if (p11g->p11g_use_rsa)
			rc = pkcs11_dh_agree_rsa(p11g, exchange_buffer,
			    eb_len, dh_private, dhpriv_len,
			    &return_buf, &rb_len);
		else
			rc = pkcs11_dh_agree_native(p11g, exchange_buffer,
			    eb_len, dh_private, dhpriv_len,
			    &return_buf, &rb_len);
		/* FALLTHRU */
	}

	callback(rc, return_buf, rb_len, callback_context);
	ssh_free(return_buf);
	return (NULL);
}

/*
 * The pkcs11 implementation of elliptic-curve diffie-hellman uses an
 * uncompressed point representation for the public key.
 *
 * The first byte is always 0x4 (indicating an uncompressed point); it
 * is followed by the x and then the y coordinate of the point.
 *
 * IKE sends just the x and y; we need to trim it off the front before
 * returning it to the caller, and put it back on before feeding the
 * peer's public value into C_DeriveKey.
 */
#define	EC_POINT_FORM_UNCOMPRESSED	0x04

static SshCryptoStatus
pkcs11_dh_setup_ecp(pkcs11_ecp_group_t *p11ecpg, uint8_t **ecp_private,
    size_t *ecppriv_len, uint8_t **exchange_buffer, size_t *eb_len)
{
	SshCryptoStatus rc;
	CK_RV pkcs11_rc;
	CK_SESSION_HANDLE sess = p11ecpg->p11ecpg_p11i->p11i_session;
	CK_OBJECT_HANDLE ecp_priv, ecp_pub;
	CK_ATTRIBUTE privattrs[MAX_ATTRS];
	CK_ATTRIBUTE bignum_vec = {0, NULL, 0};
	CK_ULONG key_bytes = p11ecpg->p11ecpg_bytes;
	uint8_t *eeb;		/* Encoded exchange buffer */
	uint32_t eeb_len;

	*exchange_buffer = NULL;

	pkcs11_rc = p11f->C_GenerateKeyPair(sess, ecp_generate,
	    p11ecpg->p11ecpg_attrs, p11ecpg->p11ecpg_attrcount, NULL, 0,
	    &ecp_pub, &ecp_priv);
	if (pkcs11_rc != CKR_OK) {
		pkcs11_error(pkcs11_rc, "dh_setup_ecp: C_GenerateKeyPair");
		ssh_policy_sun_info("PKCS#11 Error generating ECC D-H Values:"
		    " %s", pkcs11_strerror(pkcs11_rc));
		return (ckrv_to_sshcryptostatus(pkcs11_rc));
	}

	eeb_len = key_bytes * 2 + 1; /* EC_POINT_FORM_UNCOMPRESSED, x, y */
	eeb = ssh_malloc(eeb_len);
	if (eeb == NULL) {
		rc = SSH_CRYPTO_NO_MEMORY;
		goto bail;
	}

	ssh_policy_sun_info("dh_setup_ecp: key_bytes %d buf_len %d\n",
	    key_bytes, *eb_len);

	ATTR_INIT(bignum_vec, CKA_EC_POINT, eeb, eeb_len);
	pkcs11_rc = p11f->C_GetAttributeValue(sess, ecp_pub, &bignum_vec, 1);
	if (pkcs11_rc != CKR_OK) {
		pkcs11_error(pkcs11_rc, "dh_setup_ecp: C_GetAttributeValue");
		ssh_policy_sun_info("PKCS#11 Error: %s",
		    pkcs11_strerror(pkcs11_rc));
		rc = ckrv_to_sshcryptostatus(pkcs11_rc);
		goto bail;
	}

	if (eeb[0] != EC_POINT_FORM_UNCOMPRESSED) {
		ssh_policy_sun_info("PKCS#11 Error: not an uncompressed point");
		rc = SSH_CRYPTO_PROVIDER_ERROR;
		goto bail;
	}

	*eb_len = eeb_len - 1;
	*exchange_buffer = ssh_memdup(eeb + 1, eeb_len - 1);
	if (*exchange_buffer == NULL) {
		rc = SSH_CRYPTO_NO_MEMORY;
		goto bail;
	}

	ssh_free(eeb);
	eeb = NULL;

	*ecppriv_len = key_bytes;
	*ecp_private = ssh_malloc(key_bytes);
	if (*ecp_private == NULL) {
		rc = SSH_CRYPTO_NO_MEMORY;
		goto bail;
	}
	ATTR_INIT(bignum_vec, CKA_VALUE, *ecp_private, key_bytes);
	pkcs11_rc = p11f->C_GetAttributeValue(sess, ecp_priv, &bignum_vec, 1);
	if (pkcs11_rc != CKR_OK) {
		pkcs11_error(pkcs11_rc, "dh_setup_ecp: "
		    "second C_GetAttributeValue");
		ssh_policy_sun_info("PKCS#11 Error: %s",
		    pkcs11_strerror(pkcs11_rc));
		rc = ckrv_to_sshcryptostatus(pkcs11_rc);
		goto bail;
	} else {
		rc = SSH_CRYPTO_OK;
	}

	ssh_policy_sun_info("dh_setup_ecp: bignum_vec len %d key_bytes %d\n",
	    bignum_vec.ulValueLen, key_bytes);

	if (bignum_vec.ulValueLen < key_bytes) {
		restore_leading_zeroes(*ecp_private, key_bytes,
		    bignum_vec.ulValueLen);
	} else if (bignum_vec.ulValueLen > key_bytes) {
		ssh_policy_sun_info("dh_setup_ecp: Yike!\n");
		/*
		 * Yike!  Memory-overscribble notwithstanding, we should
		 * return an error in this case.
		 */
		rc = SSH_CRYPTO_TEST_PK;	/* Internal PK error. */
	}

bail:
	if (rc != SSH_CRYPTO_OK) {
		ssh_free(eeb);
		ssh_free(*exchange_buffer);
		ssh_free(*ecp_private);
		/* Be pedantic in case of brain-damage in OEM code. */
		*exchange_buffer = NULL;
		*ecp_private = NULL;
		*eb_len = 0;
		*ecppriv_len = 0;
	}
	(void) p11f->C_DestroyObject(sess, ecp_priv);
	(void) p11f->C_DestroyObject(sess, ecp_pub);
	ssh_policy_sun_info("dh_setup_ecp: returning %d\n", rc);
	return (rc);
}

static SshCryptoStatus
pkcs11_dh_agree_ecp(pkcs11_ecp_group_t *p11ecpg, uint8_t *exchange_buffer,
    size_t exchange_buffer_len, uint8_t *secret_buffer,
    size_t secret_buffer_len, uint8_t **return_buf, size_t *rb_len)
{
	SshCryptoStatus rc;
	CK_RV pkcs11_rc;
	CK_MECHANISM dh_derive = { CKM_ECDH1_DERIVE, NULL, 0};
	CK_ECDH1_DERIVE_PARAMS ecdh_derive;
	CK_SESSION_HANDLE sess = p11ecpg->p11ecpg_p11i->p11i_session;
	CK_OBJECT_HANDLE ecp_priv = NULL, shared_secret = NULL;
	CK_ATTRIBUTE privattrs[MAX_ATTRS], attrs[MAX_ATTRS];
	CK_ULONG pacount = 0, attrcount = 0;
	CK_ULONG key_bytes = p11ecpg->p11ecpg_bytes;
	CK_OBJECT_CLASS oclass = CKO_PRIVATE_KEY;
	CK_KEY_TYPE keytype = CKK_EC;
	CK_BBOOL true = TRUE;
	uint8_t *eeb;
	size_t eeb_len;

	*return_buf = NULL;
	*rb_len = 0;

	eeb_len = exchange_buffer_len + 1;
	eeb = ssh_malloc(eeb_len);
	if (eeb == NULL) {
		rc = SSH_CRYPTO_NO_MEMORY;
		goto bail;
	}
	*eeb = EC_POINT_FORM_UNCOMPRESSED;
	memcpy(eeb + 1, exchange_buffer, exchange_buffer_len);

	ecdh_derive.kdf = CKD_NULL;
	ecdh_derive.pSharedData = NULL;
	ecdh_derive.ulSharedDataLen = 0;
	ecdh_derive.pPublicData = eeb;
	ecdh_derive.ulPublicDataLen = eeb_len;

	dh_derive.pParameter = &ecdh_derive;
	dh_derive.ulParameterLen = sizeof (ecdh_derive);

	/*
	 * NOTE:  It would be nice if there were a way to preserve the
	 * private object from dh_setup_native() and reuse it here.
	 * Unfortunately, that requires modifying either the proxy-key
	 * interface, or the higher-level caller to do the right thing.
	 * It would save a few microseconds, for sure.
	 */
	ATTR_INIT(privattrs[pacount], CKA_CLASS, &oclass, sizeof (oclass));
	pacount++;
	ATTR_INIT(privattrs[pacount], CKA_KEY_TYPE, &keytype, sizeof (keytype));
	pacount++;
	ATTR_INIT(privattrs[pacount], CKA_VALUE, secret_buffer,
	    secret_buffer_len);
	pacount++;
	ATTR_INIT(privattrs[pacount], CKA_DERIVE, &true, sizeof (true));
	pacount++;
	ATTR_INIT(privattrs[pacount], CKA_EC_PARAMS, &p11ecpg->p11ecpg_oid,
	    p11ecpg->p11ecpg_oidsz);
	pacount++;

	pkcs11_rc = p11f->C_CreateObject(sess, privattrs, pacount, &ecp_priv);
	if (pkcs11_rc != CKR_OK) {
		pkcs11_error(pkcs11_rc, "dh_agree_ecp: C_CreateObject");
		ssh_policy_sun_info("PKCS#11 Error: agree CreateObject %s",
		    pkcs11_strerror(pkcs11_rc));
		rc = ckrv_to_sshcryptostatus(pkcs11_rc);
		goto bail;
	}

	oclass = CKK_GENERIC_SECRET;
	ATTR_INIT(attrs[attrcount], CKA_KEY_TYPE, &oclass, sizeof (oclass));
	attrcount++;

	pkcs11_rc = p11f->C_DeriveKey(sess, &dh_derive,
	    ecp_priv, attrs, attrcount, &shared_secret);
	if (pkcs11_rc != CKR_OK) {
		pkcs11_error(pkcs11_rc, "dh_agree_ecp_native: C_DeriveKey");
		ssh_policy_sun_info("PKCS#11 Error: agree DeriveKey %s",
		    pkcs11_strerror(pkcs11_rc));
		rc = ckrv_to_sshcryptostatus(pkcs11_rc);
		goto bail;
	}

	*rb_len = key_bytes;
	*return_buf = ssh_malloc(*rb_len);
	if (*return_buf == NULL) {
		rc = SSH_CRYPTO_NO_MEMORY;
		*rb_len = 0;
		goto bail;
	}

	ATTR_INIT(attrs[0], CKA_VALUE, *return_buf, key_bytes);
	pkcs11_rc = p11f->C_GetAttributeValue(sess, shared_secret, attrs, 1);
	if (pkcs11_rc != CKR_OK) {
		rc = ckrv_to_sshcryptostatus(pkcs11_rc);
		pkcs11_error(pkcs11_rc, "dh_agree_native: C_GetAttributeValue");
		ssh_free(*return_buf);
		*rb_len = 0;
		*return_buf = NULL;
	} else if (attrs[0].ulValueLen < key_bytes) {
		rc = SSH_CRYPTO_OK;
		restore_leading_zeroes(*return_buf, key_bytes,
		    attrs[0].ulValueLen);
	} else if (attrs[0].ulValueLen > key_bytes) {
		/*
		 * Yike!  Memory-overscribble notwithstanding, we should
		 * return an error in this case.
		 */
		rc = SSH_CRYPTO_TEST_PK;	/* Internal PK error. */
		ssh_free(*return_buf);
		*rb_len = 0;
		*return_buf = NULL;
	} else {
		rc = SSH_CRYPTO_OK;	/* Everything's cool if we're here. */
	}
bail:
	ssh_free(eeb);
	(void) p11f->C_DestroyObject(sess, ecp_priv);
	(void) p11f->C_DestroyObject(sess, shared_secret);
	return (rc);
}

SshOperationHandle
pkcs11_ecp_dispatch(SshProxyOperationId op_id, SshProxyRGFId rgf_id,
    SshProxyKeyHandle handle, const uint8_t *input_data, size_t input_data_len,
    SshProxyReplyCB callback, void *callback_context, void *my_context)
{
	pkcs11_ecp_group_t *p11ecpg = (pkcs11_ecp_group_t *)my_context;
	uint8_t *dh_private = NULL, *exchange_buffer = NULL, *return_buf;
	size_t dhpriv_len, eb_len;
	SshCryptoStatus rc;
	size_t decode_rc, rb_len;

	switch (op_id) {
	case SSH_DH_SETUP:
		rc = pkcs11_dh_setup_ecp(p11ecpg, &dh_private, &dhpriv_len,
		    &exchange_buffer, &eb_len);

		/* PKCS#11 provider failed */
		if (rc != SSH_CRYPTO_OK)
			return (NULL);

		/*
		 * Encode return_buf, use allocation (UINT32_STR), instead of
		 * reference (UINT32_STR_NOCOPY) in preparation of any
		 * asynchrony we introduce later.
		 */
		rb_len = ssh_encode_array_alloc(&return_buf,
		    SSH_FORMAT_UINT32_STR, exchange_buffer, eb_len,
		    SSH_FORMAT_UINT32_STR, dh_private, dhpriv_len,
		    SSH_FORMAT_END);
		if (rb_len <= 0) {
			pkcs11_error(0,
			    "pkcs11_dh_setup: ssh_encode_array failed");
			rb_len = 0;
			return_buf = NULL;
		}
		ssh_free(dh_private);
		ssh_free(exchange_buffer);
		break;
	case SSH_DH_AGREE:
		/*
		 * Using NOCOPY means I don't have to free exchange_buffer
		 * and dh_private.
		 */
		decode_rc = ssh_decode_array(input_data, input_data_len,
		    SSH_FORMAT_UINT32_STR_NOCOPY, &exchange_buffer, &eb_len,
		    SSH_FORMAT_UINT32_STR_NOCOPY, &dh_private, &dhpriv_len,
		    SSH_FORMAT_END);
		if (decode_rc != input_data_len) {
			pkcs11_error(0,
			    "pkcs11_dh_agree: ssh_decode_array failed");
			rc = SSH_CRYPTO_INTERNAL_ERROR;
			return_buf = NULL;
			break;	/* out of switch */
		}

		rc = pkcs11_dh_agree_ecp(p11ecpg, exchange_buffer, eb_len,
		    dh_private, dhpriv_len, &return_buf, &rb_len);

		/* FALLTHRU */
	}
	callback(rc, return_buf, rb_len, callback_context);
	ssh_free(return_buf);
	return (NULL);
}
