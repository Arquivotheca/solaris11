/*
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <sshincludes.h>
#include <sshpk_i.h>
#include <sshprvkey.h>
#include <sshpubkey.h>
#include <pkcs11-glue.h>
#include <dlfcn.h>
#include <link.h>
#include <syslog.h>
#include <cryptoutil.h>
#include <isakmp.h>

extern struct SshEkProviderOpsRec pkcs11_ops;
CK_RV (*Our_C_GetFunctionList)(CK_FUNCTION_LIST_PTR_PTR);

static const char *pkcs11_prefix = "PKCS11:";

/*
 * Due to the current optional nature of libpkcs11.so, I need this
 * table of function pointers.
 */
CK_FUNCTION_LIST_PTR p11f;

CK_MECHANISM rsa_pkcs1_bin = {CKM_RSA_PKCS, NULL, 0};
CK_MECHANISM_PTR rsa_pkcs1 = &rsa_pkcs1_bin;
CK_MECHANISM dsa_bin = {CKM_DSA, NULL, 0};
CK_MECHANISM_PTR dsa = &dsa_bin;
CK_MECHANISM rsa_raw_bin = {CKM_RSA_X_509, NULL, 0};
CK_MECHANISM_PTR rsa_raw = &rsa_raw_bin;
CK_MECHANISM dh_generate_bin = {CKM_DH_PKCS_KEY_PAIR_GEN, NULL, 0};
CK_MECHANISM_PTR dh_generate = &dh_generate_bin;
CK_MECHANISM ecp_generate_bin = {CKM_EC_KEY_PAIR_GEN, NULL, 0};
CK_MECHANISM_PTR ecp_generate = &ecp_generate_bin;

static CK_BBOOL pkcs11_true = TRUE;
static CK_ULONG pkcs11_numslots;
static CK_SLOT_ID_PTR pkcs11_slots;

static pkcs11_state_t *ike_p11s;

/*
 * Now using libcryptoutil's pkcs11_strerror().
 */
void
pkcs11_error(CK_RV errval, char *prepend)
{
	syslog(LOG_WARNING | LOG_DAEMON, "%s: %s.", prepend,
	    pkcs11_strerror(errval));
}

/* ARGSUSED */
static CK_RV
pkcs11_callback_handler(CK_SESSION_HANDLE session, CK_NOTIFICATION surrender,
    void *context)
{
	assert(surrender == CKN_SURRENDER);

	return (CKR_OK);
}

static boolean_t
alg_supported(CK_SLOT_ID slot, CK_MECHANISM_TYPE alg, CK_FLAGS flags)
{
	CK_MECHANISM_INFO mech_info;
	CK_RV pkcs11_rc;

	pkcs11_rc = p11f->C_GetMechanismInfo(slot, alg, &mech_info);
	if (pkcs11_rc != CKR_OK) {
		pkcs11_error(pkcs11_rc,
		    "libike:useful_slot:C_GetMechanismInfo");
		return (_B_FALSE);
	}
	return ((mech_info.flags & flags) == flags);
}

static uint_t
useful_slot(CK_SLOT_ID slot, CK_TOKEN_INFO *token_info)
{
	CK_RV pkcs11_rc;
	uint_t rc = 0;

	/*
	 * What to check for in a useful slot...
	 *
	 * 1. Support for RSA and DSA operations.  D-H can be done in
	 *    terms of RSA, so that's why RSA and DSA are first.
	 *    For now, the slot must have support for all of the operations,
	 *    or it's not useful.
	 * 2. If possible, see whether or not we have hardware key storage.
	 *    According to implementors of the PKCS#11 spec, that's
	 *    unfortunately implementation-specific behavior.  :-P
	 * 3. See if the slot supports out-of-the-box D-H.
	 */

	pkcs11_rc = p11f->C_GetTokenInfo(slot, token_info);
	if (pkcs11_rc != CKR_OK) {
		pkcs11_error(pkcs11_rc, "libike:useful_slot:C_GetTokenInfo");
		return (rc);
	}

	if (alg_supported(slot, CKM_RSA_PKCS, CKF_ENCRYPT | CKF_DECRYPT |
	    CKF_VERIFY_RECOVER | CKF_SIGN_RECOVER))
		rc |= P11F_RSA;

	if (alg_supported(slot, CKM_DSA, CKF_VERIFY | CKF_SIGN))
		rc |= P11F_DSA;

	if (alg_supported(slot, CKM_RSA_X_509, CKF_ENCRYPT))
		rc |= P11F_DH_RSA;

	if (alg_supported(slot, CKM_DH_PKCS_KEY_PAIR_GEN,
	    CKF_GENERATE_KEY_PAIR) &&
	    alg_supported(slot, CKM_DH_PKCS_DERIVE, CKF_DERIVE))
		rc |= P11F_DH;

	if (alg_supported(slot, CKM_EC_KEY_PAIR_GEN, CKF_GENERATE_KEY_PAIR) &&
	    alg_supported(slot, CKM_ECDH1_DERIVE, CKF_DERIVE))
		rc |= P11F_ECDHP;

	return (rc);
}

pkcs11_inst_t *
find_p11i_flags(uint_t flags)
{
	int i;

	if (ike_p11s == NULL)
		return (NULL);

	for (i = 0; i < ike_p11s->p11s_numinst; i++)
		if ((ike_p11s->p11s_p11is[i]->p11i_flags & flags) != 0) {
			return (ike_p11s->p11s_p11is[i]);
		}

	return (NULL);
}

pkcs11_inst_t *
find_p11i_slot(char *slot)
{
	int i;

	if (ike_p11s == NULL)
		return (NULL);

	for (i = 0; i < ike_p11s->p11s_numinst; i++)
		if (strncmp(slot,
		    ike_p11s->p11s_p11is[i]->p11i_token_label,
		    PKCS11_TOKSIZE) == 0)
			return (ike_p11s->p11s_p11is[i]);

	return (NULL);
}

/*
 */
static boolean_t
pkcs11_prime_slots(void)
{
	pkcs11_state_t *p11s;
	pkcs11_inst_t *p11i, **newbie;
	CK_RV pkcs11_rc;
	int i;
	uint_t flags;

	p11s = (pkcs11_state_t *)ssh_calloc(1, sizeof (*p11s));
	if (p11s == NULL) {
		return (_B_FALSE);
	}

	for (i = 0; i < pkcs11_numslots; i++) {
		CK_TOKEN_INFO token_info;

		if ((flags = useful_slot(pkcs11_slots[i], &token_info)) == 0)
			continue;

		newbie = ssh_realloc(p11s->p11s_p11is,
		    p11s->p11s_numinst++ * sizeof (pkcs11_inst_t *),
		    p11s->p11s_numinst * sizeof (pkcs11_inst_t *));
		if (newbie == NULL) {
			p11s->p11s_numinst--;
			continue;
		}
		p11s->p11s_p11is = newbie;

		p11i = ssh_calloc(1, sizeof (pkcs11_inst_t));
		if (p11i == NULL) {
			p11s->p11s_numinst--;
			continue;
		}
		p11s->p11s_p11is[p11s->p11s_numinst - 1] = p11i;

		/*
		 * These sessions are used only by iked, so we will not need
		 * read/write set.  The certificate utilities use
		 * pkcs11_get_session() to retrieve a read/write session.
		 */
		pkcs11_rc = p11f->C_OpenSession(pkcs11_slots[i],
		    CKF_SERIAL_SESSION, NULL,
		    pkcs11_callback_handler, &p11i->p11i_session);
		if (pkcs11_rc != CKR_OK) {
			pkcs11_error(pkcs11_rc, "C_OpenSession");
			ssh_free(p11i);
			p11s->p11s_numinst--;
			continue;
		}

		p11i->p11i_refcnt = 1;
		p11i->p11i_flags = flags;
		memcpy(p11i->p11i_token_label, token_info.label,
		    PKCS11_TOKSIZE);
	}

	if (p11s->p11s_numinst == 0) {
		ssh_free(p11s->p11s_p11is);	/* Works even if NULL. */
		ssh_free(p11s);
		p11s = NULL;
		return (_B_FALSE);
	}

	ike_p11s = p11s;
	return (_B_TRUE);
}

void
p11i_free(pkcs11_inst_t *p11i)
{
	/* Do any additional p11i cleanup before freeing. */
	assert(p11i->p11i_refcnt == 0);

	(void) p11f->C_CloseSession(p11i->p11i_session);
	ssh_free(p11i);
}

static void
pkcs11_uninit(void *context)
{
	pkcs11_state_t *p11s = (pkcs11_state_t *)context;
	int i;

	for (i = 0; i < p11s->p11s_numinst; i++)
		P11I_REFRELE(p11s->p11s_p11is[i]);
	ssh_free(p11s);
}

CK_ATTRIBUTE_PTR
pkcs11_privkey_attrs(CK_ULONG_PTR attrcount,
    SshPrivateKey source, char *name, uint8_t *buf1, uint8_t *buf2,
    uint8_t *buf3, uint8_t *buf4, uint8_t *buf5, uint8_t *buf6, uint8_t *buf7,
    uint8_t *buf8, CK_OBJECT_CLASS *classp, CK_KEY_TYPE *keytypep)
{
	CK_ATTRIBUTE_PTR rc;
	SshMPIntegerStruct private_d, prime_p, subprime_q, dmodp,
	    dmodq, pinvmodq_u, public_e, modulus_n, base_g, private_x;
	size_t n_len, d_len, g_len, x_len, p_len, q_len, dmodp_len, dmodq_len,
	    u_len, e_len, k_len;
	SshCryptoStatus ssh_rc;

	*attrcount = 0;
	rc = ssh_calloc(MAX_ATTRS, sizeof (CK_ATTRIBUTE));
	if (rc == NULL)
		return (NULL);

	ATTR_INIT(rc[*attrcount], CKA_CLASS, classp, sizeof (*classp));
	(*attrcount)++;
	ATTR_INIT(rc[*attrcount], CKA_LABEL, name, strlen(name));
	(*attrcount)++;
	ATTR_INIT(rc[*attrcount], CKA_ID, name, strlen(name));
	(*attrcount)++;
	ATTR_INIT(rc[*attrcount], CKA_KEY_TYPE, keytypep, sizeof (*keytypep));
	(*attrcount)++;

	/* WARNING - These attributes assume a legit IKE private key. */
	ATTR_INIT(rc[*attrcount], CKA_DECRYPT, &pkcs11_true,
	    sizeof (pkcs11_true));
	(*attrcount)++;
	ATTR_INIT(rc[*attrcount], CKA_SIGN, &pkcs11_true,
	    sizeof (pkcs11_true));
	(*attrcount)++;
	ATTR_INIT(rc[*attrcount], CKA_SIGN_RECOVER, &pkcs11_true,
	    sizeof (pkcs11_true));
	(*attrcount)++;
	ATTR_INIT(rc[*attrcount], CKA_UNWRAP, &pkcs11_true,
	    sizeof (pkcs11_true));
	(*attrcount)++;

	if (*keytypep == CKK_RSA) {
		ssh_mprz_init(&modulus_n);
		ssh_mprz_init(&private_d);
		ssh_mprz_init(&public_e);
		ssh_mprz_init(&prime_p);
		ssh_mprz_init(&subprime_q);
		ssh_mprz_init(&pinvmodq_u);
		ssh_mprz_init(&dmodp);
		ssh_mprz_init(&dmodq);

		ssh_rc = ssh_private_key_get_info(source, SSH_PKF_MODULO_N,
		    &modulus_n, SSH_PKF_SECRET_D, &private_d,
		    SSH_PKF_PRIME_P, &prime_p, SSH_PKF_PRIME_Q, &subprime_q,
		    SSH_PKF_INVERSE_U, &pinvmodq_u,
		    SSH_PKF_PUBLIC_E, &public_e, SSH_PKF_SIZE, &k_len,
		    SSH_PKF_END);

		if (ssh_rc != SSH_CRYPTO_OK) {
			ssh_free(rc);
			ssh_mprz_clear(&modulus_n);
			ssh_mprz_clear(&private_d);
			ssh_mprz_clear(&public_e);
			ssh_mprz_clear(&prime_p);
			ssh_mprz_clear(&subprime_q);
			ssh_mprz_clear(&pinvmodq_u);
			ssh_mprz_clear(&dmodp);
			ssh_mprz_clear(&dmodq);
			return (NULL);
		}

		/* Compute d mod p-1 */
		ssh_mp_sub_ui(&dmodp, &prime_p, 1);
		ssh_mp_mod(&dmodp, &private_d, &dmodp);

		/* Compute d mod q-1 */
		ssh_mp_sub_ui(&dmodq, &subprime_q, 1);
		ssh_mp_mod(&dmodq, &private_d, &dmodq);

		/*
		 * Get size in bytes.
		 * Account for leading zeros.
		 */

		/* Get n size and d size from key length */
		n_len = d_len = k_len / 8;

		/*
		 * In practice, below sizes are likely half
		 * of the key length, but are not guaranteed, so
		 * we derive their lengths, assuming even number.
		 */
		p_len = SSH_GET_ROUNDED_SIZE(&prime_p, 256, 4);
		q_len = SSH_GET_ROUNDED_SIZE(&subprime_q, 256, 4);
		u_len = SSH_GET_ROUNDED_SIZE(&pinvmodq_u, 256, 4);
		dmodp_len = SSH_GET_ROUNDED_SIZE(&dmodp, 256, 2);
		dmodq_len = SSH_GET_ROUNDED_SIZE(&dmodq, 256, 4);

		/*
		 * encryption key will be much smaller
		 */
		e_len = SSH_GET_ROUNDED_SIZE(&public_e, 256, 2);

		ssh_mprz_get_buf(buf1, n_len, &modulus_n);
		ssh_mprz_get_buf(buf2, d_len, &private_d);
		ssh_mprz_get_buf(buf3, p_len, &prime_p);
		ssh_mprz_get_buf(buf4, q_len, &subprime_q);
		ssh_mprz_get_buf(buf5, u_len, &pinvmodq_u);
		ssh_mprz_get_buf(buf6, e_len, &public_e);
		ssh_mprz_get_buf(buf7, dmodp_len, &dmodp);
		ssh_mprz_get_buf(buf8, dmodq_len, &dmodq);

		ssh_mprz_clear(&modulus_n);
		ssh_mprz_clear(&private_d);
		ssh_mprz_clear(&public_e);
		ssh_mprz_clear(&prime_p);
		ssh_mprz_clear(&subprime_q);
		ssh_mprz_clear(&pinvmodq_u);
		ssh_mprz_clear(&dmodp);
		ssh_mprz_clear(&dmodq);

		/* Use ssh_mprz_get_size(&num, 256) to get length in bytes. */
		ATTR_INIT(rc[*attrcount], CKA_MODULUS, buf1, n_len);
		(*attrcount)++;
		ATTR_INIT(rc[*attrcount], CKA_PRIVATE_EXPONENT, buf2, d_len);
		(*attrcount)++;
		ATTR_INIT(rc[*attrcount], CKA_PRIME_2, buf3, p_len);
		(*attrcount)++;
		ATTR_INIT(rc[*attrcount], CKA_PRIME_1, buf4, q_len);
		(*attrcount)++;
		ATTR_INIT(rc[*attrcount], CKA_COEFFICIENT, buf5, u_len);
		(*attrcount)++;
		ATTR_INIT(rc[*attrcount], CKA_PUBLIC_EXPONENT, buf6, e_len);
		(*attrcount)++;
		ATTR_INIT(rc[*attrcount], CKA_EXPONENT_2, buf7, dmodp_len);
		(*attrcount)++;
		ATTR_INIT(rc[*attrcount], CKA_EXPONENT_1, buf8, dmodq_len);
		(*attrcount)++;
	} else if (*keytypep == CKK_DSA) {
		ssh_mprz_init(&prime_p);
		ssh_mprz_init(&subprime_q);
		ssh_mprz_init(&base_g);
		ssh_mprz_init(&private_x);

		ssh_rc = ssh_private_key_get_info(source, SSH_PKF_PRIME_P,
		    &prime_p, SSH_PKF_PRIME_Q, &subprime_q,
		    SSH_PKF_GENERATOR_G, &base_g, SSH_PKF_SECRET_X, &private_x,
		    SSH_PKF_SIZE, &k_len, SSH_PKF_END);
		if (ssh_rc != SSH_CRYPTO_OK) {
			ssh_free(rc);
			ssh_mprz_clear(&prime_p);
			ssh_mprz_clear(&subprime_q);
			ssh_mprz_clear(&base_g);
			ssh_mprz_clear(&private_x);
			return (NULL);
		}
		/*
		 * Get size in bytes.
		 * Account for leading zeros.
		 */

		/* Extract prime and generator length from keysize */
		p_len = g_len = k_len / 8;

		/*
		 * q is fixed at 160 bits by the standard
		 * so set to 20 bytes.
		 */
		q_len = 20;

		/*
		 * In practice, x_len is probably the same as q_len,
		 * but the spec defines it as simply a number less than
		 * q, so we do our best here to derive its length.
		 */
		x_len = SSH_GET_ROUNDED_SIZE(&private_x, 256, 2);

		ssh_mprz_get_buf(buf1, p_len, &prime_p);
		ssh_mprz_get_buf(buf2, q_len, &subprime_q);
		ssh_mprz_get_buf(buf3, g_len, &base_g);
		ssh_mprz_get_buf(buf4, x_len, &private_x);

		ssh_mprz_clear(&prime_p);
		ssh_mprz_clear(&subprime_q);
		ssh_mprz_clear(&base_g);
		ssh_mprz_clear(&private_x);

		ATTR_INIT(rc[*attrcount], CKA_PRIME, buf1, p_len);
		(*attrcount)++;
		ATTR_INIT(rc[*attrcount], CKA_SUBPRIME, buf2, q_len);
		(*attrcount)++;
		ATTR_INIT(rc[*attrcount], CKA_BASE, buf3, g_len);
		(*attrcount)++;
		ATTR_INIT(rc[*attrcount], CKA_VALUE, buf4, x_len);
		(*attrcount)++;
	} else {
		/* NOTE:  Other types include ec-modp, dl-gf2n, and ec-gf2n. */
		ssh_free(rc);
		rc = NULL;
	}

	return (rc);
}

CK_OBJECT_HANDLE
pkcs11_convert_private(pkcs11_inst_t *p11i, CK_KEY_TYPE intype,
    SshPrivateKey source, char *name)
{
	CK_RV pkcs11_rc;
	CK_ATTRIBUTE_PTR attrs;
	CK_ULONG attrcount;
	uint8_t buf1[BIGINT_BUFLEN], buf2[BIGINT_BUFLEN], buf3[BIGINT_BUFLEN],
	    buf4[BIGINT_BUFLEN], buf5[BIGINT_BUFLEN], buf6[BIGINT_BUFLEN],
	    buf7[BIGINT_BUFLEN], buf8[BIGINT_BUFLEN];
	CK_KEY_TYPE keytype = intype;
	CK_OBJECT_CLASS class = CKO_PRIVATE_KEY;
	CK_OBJECT_HANDLE newkey;

	/* Load up the PKCS#11 definition, and create an object. */
	attrs = pkcs11_privkey_attrs(&attrcount, source, name,
	    buf1, buf2, buf3, buf4, buf5, buf6, buf7, buf8, &class, &keytype);
	if (attrs != NULL) {
		pkcs11_rc = p11f->C_CreateObject(p11i->p11i_session, attrs,
		    attrcount, &newkey);
		ssh_free(attrs);
	} else {
		pkcs11_rc = CKR_HOST_MEMORY;
	}

	if (pkcs11_rc != CKR_OK) {
		pkcs11_error(pkcs11_rc,
		    "pkcs11_convert_private: C_CreateObject");
		newkey = NULL;
	}

	return (newkey);
}

/*
 * Return an attribute vector suitable for a C_CreateObject() call.
 */
CK_ATTRIBUTE_PTR
pkcs11_pubkey_attrs(CK_ULONG_PTR attrcount,
    SshPublicKey source, char *name, uint8_t *buf1, uint8_t *buf2,
    uint8_t *buf3, uint8_t *buf4, CK_OBJECT_CLASS *classp,
    CK_KEY_TYPE *keytypep, CK_BBOOL *token_obj)
{
	CK_ATTRIBUTE_PTR rc;
	SshMPIntegerStruct modulus_n, exponent_e, prime_p, subprime_q,
	    base_g, public_y;
	size_t n_len, e_len, g_len, y_len, p_len, q_len;
	SshCryptoStatus ssh_rc;

	*attrcount = 0;	/* Common batch of information. */
	rc = ssh_calloc(MAX_ATTRS, sizeof (CK_ATTRIBUTE));
	if (rc == NULL)
		return (NULL);

	ATTR_INIT(rc[*attrcount], CKA_CLASS, classp, sizeof (*classp));
	(*attrcount)++;
	ATTR_INIT(rc[*attrcount], CKA_LABEL, name, strlen(name));
	(*attrcount)++;
	ATTR_INIT(rc[*attrcount], CKA_ID, name, strlen(name));
	(*attrcount)++;
	ATTR_INIT(rc[*attrcount], CKA_TOKEN, token_obj, sizeof (CK_BBOOL));
	(*attrcount)++;
	ATTR_INIT(rc[*attrcount], CKA_KEY_TYPE, keytypep, sizeof (*keytypep));
	(*attrcount)++;

	/* WARNING - These attributes assume a legit IKE certificate. */
	ATTR_INIT(rc[*attrcount], CKA_VERIFY_RECOVER, &pkcs11_true,
	    sizeof (pkcs11_true));
	(*attrcount)++;
	ATTR_INIT(rc[*attrcount], CKA_VERIFY, &pkcs11_true,
	    sizeof (pkcs11_true));
	(*attrcount)++;
	ATTR_INIT(rc[*attrcount], CKA_WRAP, &pkcs11_true,
	    sizeof (pkcs11_true));
	(*attrcount)++;
	ATTR_INIT(rc[*attrcount], CKA_ENCRYPT, &pkcs11_true,
	    sizeof (pkcs11_true));
	(*attrcount)++;

	if (*keytypep == CKK_RSA) {
		ssh_mprz_init(&modulus_n);
		ssh_mprz_init(&exponent_e);
		ssh_rc = ssh_public_key_get_info(source, SSH_PKF_MODULO_N,
		    &modulus_n, SSH_PKF_PUBLIC_E, &exponent_e, SSH_PKF_END);
		if (ssh_rc != SSH_CRYPTO_OK) {
			ssh_mprz_clear(&modulus_n);
			ssh_mprz_clear(&exponent_e);
			ssh_free(rc);
			return (NULL);
		}

		n_len = ssh_mprz_get_size(&modulus_n, 256);
		e_len = ssh_mprz_get_size(&exponent_e, 256);

		ssh_mprz_get_buf(buf1, n_len, &modulus_n);
		ssh_mprz_get_buf(buf2, e_len, &exponent_e);

		ssh_mprz_clear(&modulus_n);
		ssh_mprz_clear(&exponent_e);

		/* Use ssh_mprz_get_size(&num, 256) to get length in bytes. */
		ATTR_INIT(rc[*attrcount], CKA_MODULUS, buf1, n_len);
		(*attrcount)++;
		ATTR_INIT(rc[*attrcount], CKA_PUBLIC_EXPONENT, buf2, e_len);
		(*attrcount)++;
	} else if (*keytypep == CKK_DSA) {
		ssh_mprz_init(&prime_p);
		ssh_mprz_init(&subprime_q);
		ssh_mprz_init(&base_g);
		ssh_mprz_init(&public_y);

		ssh_rc = ssh_public_key_get_info(source, SSH_PKF_PRIME_P,
		    &prime_p, SSH_PKF_PRIME_Q, &subprime_q,
		    SSH_PKF_GENERATOR_G, &base_g, SSH_PKF_PUBLIC_Y, &public_y,
		    SSH_PKF_END);
		if (ssh_rc != SSH_CRYPTO_OK) {
			ssh_mprz_clear(&prime_p);
			ssh_mprz_clear(&subprime_q);
			ssh_mprz_clear(&base_g);
			ssh_mprz_clear(&public_y);
			ssh_free(rc);
			return (NULL);
		}

		p_len = ssh_mprz_get_size(&prime_p, 256);
		q_len = ssh_mprz_get_size(&subprime_q, 256);
		g_len = ssh_mprz_get_size(&base_g, 256);
		y_len = ssh_mprz_get_size(&public_y, 256);

		ssh_mprz_get_buf(buf1, p_len, &prime_p);
		ssh_mprz_get_buf(buf2, q_len, &subprime_q);
		ssh_mprz_get_buf(buf3, g_len, &base_g);
		ssh_mprz_get_buf(buf4, y_len, &public_y);

		ssh_mprz_clear(&prime_p);
		ssh_mprz_clear(&subprime_q);
		ssh_mprz_clear(&base_g);
		ssh_mprz_clear(&public_y);

		ATTR_INIT(rc[*attrcount], CKA_PRIME, buf1, p_len);
		(*attrcount)++;
		ATTR_INIT(rc[*attrcount], CKA_SUBPRIME, buf2, q_len);
		(*attrcount)++;
		ATTR_INIT(rc[*attrcount], CKA_BASE, buf3, g_len);
		(*attrcount)++;
		ATTR_INIT(rc[*attrcount], CKA_VALUE, buf4, y_len);
		(*attrcount)++;
	} else {
		/* NOTE:  Other types include ec-modp, dl-gf2n, and ec-gf2n. */
		ssh_free(rc);
		rc = NULL;
	}

	return (rc);
}

CK_OBJECT_HANDLE
pkcs11_convert_public(pkcs11_inst_t *p11i, CK_KEY_TYPE intype,
    SshPublicKey source, char *name)
{
	CK_RV pkcs11_rc;
	CK_ATTRIBUTE_PTR attrs;
	CK_ULONG attrcount;
	uint8_t buf1[BIGINT_BUFLEN], buf2[BIGINT_BUFLEN], buf3[BIGINT_BUFLEN],
	    buf4[BIGINT_BUFLEN];
	CK_KEY_TYPE keytype = intype;
	CK_OBJECT_CLASS class = CKO_PUBLIC_KEY;
	CK_BBOOL token_object = FALSE;
	SshCryptoStatus ssh_rc;
	CK_OBJECT_HANDLE newkey;

	/* Load up the PKCS#11 definition, and create an object. */
	attrs = pkcs11_pubkey_attrs(&attrcount, source, name,
	    buf1, buf2, buf3, buf4, &class, &keytype, &token_object);
	if (attrs != NULL) {
		pkcs11_rc = p11f->C_CreateObject(p11i->p11i_session, attrs,
		    attrcount, &newkey);
		ssh_free(attrs);
	} else {
		pkcs11_rc = CKR_HOST_MEMORY;
	}
	if (pkcs11_rc != CKR_OK) {
		pkcs11_error(pkcs11_rc,
		    "pkcs11_convert_public: C_CreateObject");
		newkey = NULL;
	}

	return (newkey);
}

static boolean_t
pkcs11_group_native_attrs(pkcs11_group_t *p11g)
{
	CK_ATTRIBUTE_PTR attrs;
	int attrcount = 0;

	attrs = ssh_calloc(MAX_ATTRS, sizeof (CK_ATTRIBUTE));
	if (attrs == NULL)
		return (_B_FALSE);

	p11g->p11g_use_rsa = FALSE;
	ATTR_INIT(attrs[attrcount], CKA_PRIME, &p11g->p11g_n,
	    p11g->p11g_bufsize);
	attrcount++;
	ATTR_INIT(attrs[attrcount], CKA_BASE, &p11g->p11g_g,
	    p11g->p11g_gsize);
	attrcount++;

	p11g->p11g_attrs = attrs;
	p11g->p11g_attrcount = attrcount;
	return (_B_TRUE);
}

static boolean_t
pkcs11_group_rsa_attrs(pkcs11_group_t *p11g)
{
	CK_ATTRIBUTE_PTR attrs;
	int attrcount = 0;

	attrs = ssh_calloc(MAX_ATTRS, sizeof (CK_ATTRIBUTE));
	if (attrs == NULL)
		return (_B_FALSE);

	/*
	 * NOTE: CKA_PUBLIC_EXPONENT will be our Diffie-Hellman secret
	 * for the setup() and agree() functions.  We can't set the acutal
	 * value up a priori.  Keep this parameter, however, at offset 0,
	 * so the setup() and final() functions know the correct thing to
	 * do.
	 */
	ATTR_INIT(attrs[attrcount], CKA_PUBLIC_EXPONENT, NULL, 0);
	attrcount++;
	p11g->p11g_class = CKO_PUBLIC_KEY;
	ATTR_INIT(attrs[attrcount], CKA_CLASS, &p11g->p11g_class,
	    sizeof (CK_OBJECT_CLASS));
	attrcount++;
	p11g->p11g_keytype = CKK_RSA;
	ATTR_INIT(attrs[attrcount], CKA_KEY_TYPE, &p11g->p11g_keytype,
	    sizeof (CK_KEY_TYPE));
	attrcount++;
	p11g->p11g_true = TRUE;	/* Implicitly sets p11g_use_rsa to TRUE also! */
	ATTR_INIT(attrs[attrcount], CKA_ENCRYPT, &p11g->p11g_true,
	    sizeof (CK_BBOOL));
	attrcount++;
	ATTR_INIT(attrs[attrcount], CKA_MODULUS, &p11g->p11g_n,
	    p11g->p11g_bufsize);
	attrcount++;

	p11g->p11g_attrs = attrs;
	p11g->p11g_attrcount = attrcount;
	return (_B_TRUE);
}

/* Hacky OID data arrays. */
static uint8_t ecp_oid_192[] =
	{0x6, 0x8, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x01};
static uint8_t ecp_oid_224[] = {0x6, 0x5, 0x2b, 0x81, 0x04, 0x00, 0x21};
static uint8_t ecp_oid_256[] =
	{0x6, 0x8, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07};
static uint8_t ecp_oid_384[] = {0x6, 0x5, 0x2b, 0x81, 0x04, 0x00, 0x22};
static uint8_t ecp_oid_521[] = {0x6, 0x5, 0x2b, 0x81, 0x04, 0x00, 0x23};

/*
 * Initialize group descriptor for an elliptic-curve-over-prime-field
 * (ECP) group
 */
SshPkGroup
pkcs11_generate_ecp(pkcs11_inst_t *p11i, int group)
{
	pkcs11_ecp_group_t *p11ecpg = NULL;
	CK_ATTRIBUTE_PTR attrs;
	int attrcount = 0;
	uint8_t *oidptr;
	SshPkGroup rc;

	/* Allocate and initialize p11ecpg. */
	p11ecpg = ssh_calloc(1, sizeof (*p11ecpg));
	if (p11ecpg == NULL)
		return (NULL);

	P11I_REFHOLD(p11i);
	p11ecpg->p11ecpg_p11i = p11i;

	attrs = ssh_calloc(MAX_ATTRS, sizeof (CK_ATTRIBUTE));
	if (attrs == NULL) {
		ssh_free(p11ecpg);
		return (NULL);
	}
	p11ecpg->p11ecpg_attrs = attrs;

	switch (group) {
	case 19:	/* 256-bit Random ECP group per RFC 4753. */
		p11ecpg->p11ecpg_oidsz = sizeof (ecp_oid_256);
		oidptr = ecp_oid_256;
		p11ecpg->p11ecpg_gsize = 256;
		p11ecpg->p11ecpg_bytes = 32;
		break;
	case 20:	/* 384-bit Random ECP group per RFC 4753. */
		p11ecpg->p11ecpg_oidsz = sizeof (ecp_oid_384);
		oidptr = ecp_oid_384;
		p11ecpg->p11ecpg_gsize = 384;
		p11ecpg->p11ecpg_bytes = 48;
		break;
	case 21:	/* 521-bit Random ECP group per RFC 4753. */
		p11ecpg->p11ecpg_oidsz = sizeof (ecp_oid_521);
		oidptr = ecp_oid_521;
		p11ecpg->p11ecpg_gsize = 521;
		p11ecpg->p11ecpg_bytes = 66;
		break;
	case 25:	/* 192-bit Random ECP group per RFC5114 */
		p11ecpg->p11ecpg_oidsz = sizeof (ecp_oid_192);
		oidptr = ecp_oid_192;
		p11ecpg->p11ecpg_gsize = 192;
		p11ecpg->p11ecpg_bytes = 24;
		break;
	case 26:	/* 224-bit Random ECP group per RFC5114 */
		p11ecpg->p11ecpg_oidsz = sizeof (ecp_oid_224);
		oidptr = ecp_oid_224;
		p11ecpg->p11ecpg_gsize = 224;
		p11ecpg->p11ecpg_bytes = 28;
		break;
	default:
		rc = NULL;
		goto bail;
	}

	if (p11ecpg->p11ecpg_oidsz > PKCS11_OIDSIZE) {
		ssh_policy_sun_info("OID too big for group %d: %u > %u",
		    group, p11ecpg->p11ecpg_oidsz, PKCS11_OIDSIZE);
		ssh_free(p11ecpg);
		return (NULL);
	}

	memcpy(p11ecpg->p11ecpg_oid, oidptr, p11ecpg->p11ecpg_oidsz);
	ATTR_INIT(attrs[attrcount], CKA_EC_PARAMS, &p11ecpg->p11ecpg_oid,
	    p11ecpg->p11ecpg_oidsz);
	attrcount++;
	p11ecpg->p11ecpg_attrcount = attrcount;


	rc = ssh_dh_group_create_proxy(SSH_PROXY_GROUP, p11ecpg->p11ecpg_gsize,
	    pkcs11_ecp_dispatch, pkcs11_ecp_free, p11ecpg);
bail:
	if (rc == NULL) {
		ssh_free(attrs);
		ssh_free(p11ecpg);
	}
	return (rc);
}

SshPkGroup
pkcs11_convert_group(pkcs11_inst_t *p11i, SshPkGroup source)
{
	pkcs11_group_t *p11g = NULL;
	SshMPIntegerStruct generator_g, modulus_n;
	size_t n_len, g_len;
	SshPkGroup newgroup;
	SshCryptoStatus ssh_rc;

	/* Allocate and initialize p11g. */
	p11g = ssh_calloc(1, sizeof (*p11g));
	if (p11g == NULL)
		return (NULL);

	P11I_REFHOLD(p11i);
	p11g->p11g_p11i = p11i;
	p11g->p11g_group = source;

	/*
	 * Setup p11g to contain useful data.
	 */
	ssh_mprz_init(&generator_g);
	ssh_mprz_init(&modulus_n);
	if ((ssh_rc = ssh_pk_group_get_info(p11g->p11g_group,
	    SSH_PKF_PRIME_P, &modulus_n, SSH_PKF_GENERATOR_G,
	    &generator_g, SSH_PKF_END)) != SSH_CRYPTO_OK) {
		P11I_REFRELE(p11i);
		ssh_mprz_clear(&generator_g);
		ssh_mprz_clear(&modulus_n);
		ssh_free(p11g);
		return (NULL);
	}

	p11g->p11g_bufsize = ssh_mprz_get_size(&modulus_n, 256);
	p11g->p11g_gsize = ssh_mprz_get_size(&generator_g, 256);
	ssh_mprz_get_buf(p11g->p11g_n, p11g->p11g_bufsize, &modulus_n);
	ssh_mprz_get_buf(p11g->p11g_g, p11g->p11g_gsize, &generator_g);

	ssh_mprz_clear(&generator_g);
	ssh_mprz_clear(&modulus_n);

	/*
	 * Alter the order of these if, given both, we prefer DH with
	 * RSA ops instead of DH with PKCS#11 DH ops.
	 */
	if (((p11i->p11i_flags & P11F_DH) &&
	    pkcs11_group_native_attrs(p11g)) ||
	    ((p11i->p11i_flags & P11F_DH_RSA) &&
	    pkcs11_group_rsa_attrs(p11g))) {
		/* We're good to go! */
		newgroup = ssh_dh_group_create_proxy(SSH_PROXY_GROUP,
		    p11g->p11g_gsize << 3, pkcs11_dh_dispatch,
		    pkcs11_dh_free, p11g);
	} else {
		/* We're in bad shape. */
		newgroup = NULL;
		pkcs11_dh_free(p11g);
	}

	return (newgroup);
}

/*
 * Detect and load a PKCS11 library, and return a function-list pointer.
 * This should be called immediately from a consumer's initialization code.
 */
CK_FUNCTION_LIST_PTR
pkcs11_setup(char *libpath)
{
	void *dldesc;
	CK_RV pkcs11_rc;

	if (libpath == NULL)
		return (NULL);

	/*
	 * Check for presence of libpkcs11.so.
	 */
	dldesc = dlopen(libpath, RTLD_LAZY);
	if (dldesc == NULL) {
		char *fmtstring = "Cannot load PKCS#11 library %s.";

		syslog(LOG_DAEMON | LOG_WARNING, fmtstring, libpath);
		(void) fprintf(stderr, fmtstring, libpath);
		fputc('\n', stderr);
		return (NULL);
	}

	Our_C_GetFunctionList = (CK_RV(*)())dlsym(dldesc, "C_GetFunctionList");
	if (Our_C_GetFunctionList == NULL) {
		syslog(LOG_DAEMON | LOG_WARNING,
		    "Cannot locate C_GetFunctionList");
		(void) dlclose(dldesc);
		return (NULL);
	}

	pkcs11_rc = Our_C_GetFunctionList(&p11f);
	if (pkcs11_rc != CKR_OK) {
		pkcs11_error(pkcs11_rc, "C_GetFunctionList");
		(void) dlclose(dldesc);
		return (NULL);
	}

	/*
	 * Initialize PKCS#11.
	 */

	/* Initialize */
	pkcs11_rc = p11f->C_Initialize(NULL);
	if (pkcs11_rc != CKR_OK) {
		pkcs11_error(pkcs11_rc, "C_Initialize");
		(void) dlclose(dldesc);
		return (NULL);
	}

	/* Get slot information. */
	pkcs11_rc = p11f->C_GetSlotList(FALSE, NULL, &pkcs11_numslots);
	if (pkcs11_rc != CKR_OK) {
		pkcs11_error(pkcs11_rc, "C_GetSlotList");
		(void) dlclose(dldesc);
		return (NULL);
	}
	pkcs11_slots = ssh_calloc(pkcs11_numslots, sizeof (CK_SLOT_ID));
	if (pkcs11_slots == NULL) {
		(void) dlclose(dldesc);
		return (NULL);
	}
	pkcs11_rc = p11f->C_GetSlotList(FALSE, pkcs11_slots, &pkcs11_numslots);
	if (pkcs11_rc != CKR_OK) {
		pkcs11_error(pkcs11_rc, "C_GetSlotList #2");
		ssh_free(pkcs11_slots);
		(void) dlclose(dldesc);
		return (NULL);
	}

	if (!pkcs11_prime_slots()) {
		pkcs11_error(0, "pkcs11_prime_slots() failed.\n");
		ssh_free(pkcs11_slots);
		(void) dlclose(dldesc);
		return (NULL);
	}

	return (p11f);
}

void
print_pkcs11_slots(void)
{
	int i;
	CK_TOKEN_INFO token_info;

	for (i = 0; i < pkcs11_numslots; i++) {
		if (useful_slot(pkcs11_slots[i], &token_info) != 0)
			printf("\"%.32s\"\n", token_info.label);
	}
}

/*
 * Open a PKCS#11 session.  If the passed-in return_pin has a zero first
 * character, then prompt for a PIN.  The token_id must be well-formed ala
 * PKCS#11.  And return_pin must point to something > 256 bytes long.
 */
CK_SESSION_HANDLE
pkcs11_get_session(char *token_id, char *return_pin, boolean_t need_login)
{
	int i;
	CK_SESSION_HANDLE session;
	CK_RV pkcs11_rc;
	char *pin;
	CK_TOKEN_INFO token_info;

	for (i = 0; i < pkcs11_numslots; i++) {
		if (useful_slot(pkcs11_slots[i], &token_info) &&
		    (token_id == NULL ||
		    strncmp(token_id, (char *)token_info.label,
		    PKCS11_TOKSIZE) == 0))
			break;
	}

	if (i == pkcs11_numslots)
		return (PKCS11_NO_SUCH_TOKEN);

	/* Called by admin commands, so we will need read/write set! */
	pkcs11_rc = p11f->C_OpenSession(pkcs11_slots[i],
	    CKF_SERIAL_SESSION | CKF_RW_SESSION, NULL, NULL, &session);
	if (pkcs11_rc != CKR_OK) {
		pkcs11_error(pkcs11_rc, "pkcs11_get_session: C_OpenSession:");
		return (PKCS11_OPEN_FAILED);
	}

	if (return_pin == NULL) {
		pin = NULL;
	} else if (*return_pin != '\0') {
		pin = return_pin;
	} else {
		if (need_login)
			pin = getpassphrase("Enter PIN for PKCS#11 token: ");
		else
			pin = NULL;
	}
	if (pin != NULL && need_login) {
		/* Login! */
do_login:
		pkcs11_rc = p11f->C_Login(session, CKU_USER, (uchar_t *)pin,
		    strlen(pin));
		switch (pkcs11_rc) {
		case CKR_OK:
			break;
		case CKR_USER_ALREADY_LOGGED_IN:
			/* Log out if we're logged in and try again */
			pkcs11_rc = p11f->C_Logout(session);
			if (pkcs11_rc != CKR_OK) {
				pkcs11_error(pkcs11_rc, "pkcs11_get_session: "
				    "C_Logout:");
				return (PKCS11_OPEN_FAILED);
			}
			goto do_login;
			break;
		default:
			/* Assume calling app exits in this return case. */
			pkcs11_error(pkcs11_rc,
			    "pkcs11_get_session: C_Login:");
			p11f->C_CloseSession(session);
			return (PKCS11_LOGIN_FAILED);
		}

		if (return_pin != pin)
			strlcpy(return_pin, pin, 256);
	}  /* Else try anyway without a passphrase or login. */

	return (session);
}

/*
 * Find the public value, as specified by bignum_type, out of the public-key
 * PKCS#11 object, and put it into an easy-to-chew SSH bignum.  And while
 * we're here, check the type to make sure it's okay!
 */
boolean_t
extract_pkcs11_public(CK_SESSION_HANDLE session, CK_OBJECT_HANDLE obj,
    SshMPIntegerStruct *obj_bignum, CK_KEY_TYPE type,
    CK_ATTRIBUTE_TYPE bignum_type)
{
	CK_RV pkcs11_rc;
	CK_KEY_TYPE keytype;
	CK_ATTRIBUTE type_vec = {CKA_KEY_TYPE, NULL, sizeof (CK_KEY_TYPE)};
	uint8_t bignum_buf[2048];	/* Up to 16384 bits raw! */
	CK_ATTRIBUTE bignum_vec = {0, NULL, 0};

	type_vec.pValue = &keytype;

	pkcs11_rc = p11f->C_GetAttributeValue(session, obj, &type_vec, 1);
	if (pkcs11_rc != CKR_OK || keytype != type)
		return (_B_FALSE);

	bignum_vec.type = bignum_type;
	pkcs11_rc = p11f->C_GetAttributeValue(session, obj, &bignum_vec, 1);
	if (pkcs11_rc != CKR_OK)
		return (_B_FALSE);

	/* Now that we know the length, set the actual value. */
	if (bignum_vec.ulValueLen > sizeof (bignum_buf))
		return (_B_FALSE);
	bignum_vec.pValue = bignum_buf;
	pkcs11_rc = p11f->C_GetAttributeValue(session, obj, &bignum_vec, 1);
	if (pkcs11_rc != CKR_OK)
		return (_B_FALSE);

	/* PKCS#11 and SSH agree on raw bignum formats (big-endian vectors)! */
	ssh_mprz_set_buf(obj_bignum, bignum_buf, bignum_vec.ulValueLen);

	return (_B_TRUE);
}

/*
 * Pad out src to be a PKCS#11-happy space-padded string.  Dst must have been
 * zeroed.  Using calloc() to get the storage helps here immensely.
 */
void
pkcs11_pad_out(char *dst, char *src)
{
	char *spacer;

	/* Yes, I mean strncpy(). */
	strncpy(dst, src, PKCS11_TOKSIZE);
	spacer = dst + PKCS11_TOKSIZE - 1;

	/* A pre-zeroed dst makes the loop simple. */
	while (*spacer == '\0')
		*spacer-- = ' ';
}
