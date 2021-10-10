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
 * This file contains RSA helper routines common to
 * the PKCS11 soft token code and the kernel RSA code.
 */

#include <sys/types.h>
#include <bignum.h>

#ifdef _KERNEL
#include <sys/param.h>
#include <sys/debug.h>
#else
#include <strings.h>
#include <cryptoutil.h>
#include <assert.h>
#define	ASSERT			assert
#endif

#include <sys/crypto/common.h>
#include <sys/crypto/impl.h>
#include <sys/random.h>
#include <sys/md5.h>
#include <sys/sha1.h>
#define	_SHA2_IMPL
#include <sys/sha2.h>
#include <padding/padding.h>
#define	_RSA_FIPS_POST
#include "rsa_impl.h"

/*
 * DER encoding T of the DigestInfo values for MD5, SHA1, and SHA2
 * from PKCS#1 v2.1: RSA Cryptography Standard Section 9.2 Note 1
 *
 * MD5:     (0x)30 20 30 0c 06 08 2a 86 48 86 f7 0d 02 05 05 00 04 10 || H
 * SHA-1:   (0x)30 21 30 09 06 05 2b 0e 03 02 1a 05 00 04 14 || H
 * SHA-256: (0x)30 31 30 0d 06 09 60 86 48 01 65 03 04 02 01 05 00 04 20 || H.
 * SHA-384: (0x)30 41 30 0d 06 09 60 86 48 01 65 03 04 02 02 05 00 04 30 || H.
 * SHA-512: (0x)30 51 30 0d 06 09 60 86 48 01 65 03 04 02 03 05 00 04 40 || H.
 *
 * Where H is the digested output from MD5 or SHA1. We define the constant
 * byte array (the prefix) here and use it rather than doing the DER
 * encoding of the OID in a separate routine.
 */
const CK_BYTE MD5_DER_PREFIX[MD5_DER_PREFIX_Len] = {0x30, 0x20, 0x30, 0x0c,
    0x06, 0x08, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x02, 0x05, 0x05, 0x00,
    0x04, 0x10};

const CK_BYTE SHA1_DER_PREFIX[SHA1_DER_PREFIX_Len] = {0x30, 0x21, 0x30,
    0x09, 0x06, 0x05, 0x2b, 0x0e, 0x03, 0x02, 0x1a, 0x05, 0x00, 0x04, 0x14};

const CK_BYTE SHA1_DER_PREFIX_OID[SHA1_DER_PREFIX_OID_Len] = {0x30, 0x1f, 0x30,
    0x07, 0x06, 0x05, 0x2b, 0x0e, 0x03, 0x02, 0x1a, 0x04, 0x14};

const CK_BYTE SHA256_DER_PREFIX[SHA2_DER_PREFIX_Len] = {0x30, 0x31, 0x30, 0x0d,
    0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01, 0x05,
    0x00, 0x04, 0x20};

const CK_BYTE SHA384_DER_PREFIX[SHA2_DER_PREFIX_Len] = {0x30, 0x41, 0x30, 0x0d,
    0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x02, 0x05,
    0x00, 0x04, 0x30};

const CK_BYTE SHA512_DER_PREFIX[SHA2_DER_PREFIX_Len] = {0x30, 0x51, 0x30, 0x0d,
    0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x03, 0x05,
    0x00, 0x04, 0x40};

const CK_BYTE DEFAULT_PUB_EXPO[DEFAULT_PUB_EXPO_Len] = { 0x01, 0x00, 0x01 };


static CK_RV
convert_rv(BIG_ERR_CODE err)
{
	switch (err) {

	case BIG_OK:
		return (CKR_OK);

	case BIG_NO_MEM:
		return (CKR_HOST_MEMORY);

	case BIG_NO_RANDOM:
		return (CKR_DEVICE_ERROR);

	case BIG_INVALID_ARGS:
		return (CKR_ARGUMENTS_BAD);

	case BIG_DIV_BY_0:
	default:
		return (CKR_GENERAL_ERROR);
	}
}

/* psize and qsize are in bits */
static BIG_ERR_CODE
RSA_key_init(RSAkey *key, int psize, int qsize)
{
	BIG_ERR_CODE err = BIG_OK;

/* EXPORT DELETE START */

	int plen, qlen, nlen;

	plen = BITLEN2BIGNUMLEN(psize);
	qlen = BITLEN2BIGNUMLEN(qsize);
	nlen = plen + qlen;
	key->size = psize + qsize;
	if ((err = big_init(&(key->p), plen)) != BIG_OK)
		return (err);
	if ((err = big_init(&(key->q), qlen)) != BIG_OK)
		goto ret1;
	if ((err = big_init(&(key->n), nlen)) != BIG_OK)
		goto ret2;
	if ((err = big_init(&(key->d), nlen)) != BIG_OK)
		goto ret3;
	if ((err = big_init(&(key->e), nlen)) != BIG_OK)
		goto ret4;
	if ((err = big_init(&(key->dmodpminus1), plen)) != BIG_OK)
		goto ret5;
	if ((err = big_init(&(key->dmodqminus1), qlen)) != BIG_OK)
		goto ret6;
	if ((err = big_init(&(key->pinvmodq), qlen)) != BIG_OK)
		goto ret7;
	if ((err = big_init(&(key->p_rr), plen)) != BIG_OK)
		goto ret8;
	if ((err = big_init(&(key->q_rr), qlen)) != BIG_OK)
		goto ret9;
	if ((err = big_init(&(key->n_rr), nlen)) != BIG_OK)
		goto ret10;

	return (BIG_OK);

ret10:
	big_finish(&(key->q_rr));
ret9:
	big_finish(&(key->p_rr));
ret8:
	big_finish(&(key->pinvmodq));
ret7:
	big_finish(&(key->dmodqminus1));
ret6:
	big_finish(&(key->dmodpminus1));
ret5:
	big_finish(&(key->e));
ret4:
	big_finish(&(key->d));
ret3:
	big_finish(&(key->n));
ret2:
	big_finish(&(key->q));
ret1:
	big_finish(&(key->p));

/* EXPORT DELETE END */

	return (err);
}

static void
RSA_key_finish(RSAkey *key)
{

/* EXPORT DELETE START */

	big_finish(&(key->n_rr));
	big_finish(&(key->q_rr));
	big_finish(&(key->p_rr));
	big_finish(&(key->pinvmodq));
	big_finish(&(key->dmodqminus1));
	big_finish(&(key->dmodpminus1));
	big_finish(&(key->e));
	big_finish(&(key->d));
	big_finish(&(key->n));
	big_finish(&(key->q));
	big_finish(&(key->p));

/* EXPORT DELETE END */

}

/*
 * Generate RSA key
 */
static CK_RV
generate_rsa_key(RSAkey *key, int psize, int qsize, BIGNUM *pubexp,
    int (*rfunc)(void *, size_t))
{
	CK_RV		rv = CKR_OK;

/* EXPORT DELETE START */

	int		(*rf)(void *, size_t);
	BIGNUM		a, b, c, d, e, f, g, h;
	int		len, keylen, size;
	BIG_ERR_CODE	brv = BIG_OK;

	size = psize + qsize;
	keylen = BITLEN2BIGNUMLEN(size);
	len = keylen * 2 + 1;
	key->size = size;

	/*
	 * Note: It is not really necessary to compute e, it is in pubexp:
	 * 	(void) big_copy(&(key->e), pubexp);
	 */

	a.malloced = 0;
	b.malloced = 0;
	c.malloced = 0;
	d.malloced = 0;
	e.malloced = 0;
	f.malloced = 0;
	g.malloced = 0;
	h.malloced = 0;

	if ((big_init(&a, len) != BIG_OK) ||
	    (big_init(&b, len) != BIG_OK) ||
	    (big_init(&c, len) != BIG_OK) ||
	    (big_init(&d, len) != BIG_OK) ||
	    (big_init(&e, len) != BIG_OK) ||
	    (big_init(&f, len) != BIG_OK) ||
	    (big_init(&g, len) != BIG_OK) ||
	    (big_init(&h, len) != BIG_OK)) {
		big_finish(&h);
		big_finish(&g);
		big_finish(&f);
		big_finish(&e);
		big_finish(&d);
		big_finish(&c);
		big_finish(&b);
		big_finish(&a);

		return (CKR_HOST_MEMORY);
	}

	rf = rfunc;
	if (rf == NULL) {
#ifdef _KERNEL
		rf = (int (*)(void *, size_t))random_get_pseudo_bytes;
#else
		rf = pkcs11_get_urandom;
#endif
	}

nextp:
	if ((brv = big_random(&a, psize, rf, B_TRUE)) != BIG_OK) {
		goto ret;
	}

	if ((brv = big_nextprime_pos(&b, &a)) != BIG_OK) {
		goto ret;
	}
	/* b now contains the potential prime p */

	(void) big_sub_pos(&a, &b, &big_One);
	if ((brv = big_ext_gcd_pos(&f, &d, &g, pubexp, &a)) != BIG_OK) {
		goto ret;
	}
	if (big_cmp_abs(&f, &big_One) != 0) {
		goto nextp;
	}

	if ((brv = big_random(&c, qsize, rf, B_TRUE)) != BIG_OK) {
		goto ret;
	}

nextq:
	(void) big_add(&a, &c, &big_Two);

	if (big_bitlength(&a) != qsize) {
		goto nextp;
	}
	if (big_cmp_abs(&a, &b) == 0) {
		goto nextp;
	}
	if ((brv = big_nextprime_pos(&c, &a)) != BIG_OK) {
		goto ret;
	}
	/* c now contains the potential prime q */

	if ((brv = big_mul(&g, &b, &c)) != BIG_OK) {
		goto ret;
	}
	if (big_bitlength(&g) != size) {
		goto nextp;
	}
	/* g now contains the potential modulus n */

	(void) big_sub_pos(&a, &b, &big_One);
	(void) big_sub_pos(&d, &c, &big_One);

	if ((brv = big_mul(&a, &a, &d)) != BIG_OK) {
		goto ret;
	}
	if ((brv = big_ext_gcd_pos(&f, &d, &h, pubexp, &a)) != BIG_OK) {
		goto ret;
	}
	if (big_cmp_abs(&f, &big_One) != 0) {
		goto nextq;
	} else {
		(void) big_copy(&e, pubexp);
	}
	if (d.sign == -1) {
		if ((brv = big_add(&d, &d, &a)) != BIG_OK) {
			goto ret;
		}
	}
	(void) big_copy(&(key->p), &b);
	(void) big_copy(&(key->q), &c);
	(void) big_copy(&(key->n), &g);
	(void) big_copy(&(key->d), &d);
	(void) big_copy(&(key->e), &e);

	if ((brv = big_ext_gcd_pos(&a, &f, &h, &b, &c)) != BIG_OK) {
		goto ret;
	}
	if (f.sign == -1) {
		if ((brv = big_add(&f, &f, &c)) != BIG_OK) {
			goto ret;
		}
	}
	(void) big_copy(&(key->pinvmodq), &f);

	(void) big_sub(&a, &b, &big_One);
	if ((brv = big_div_pos(&a, &f, &d, &a)) != BIG_OK) {
		goto ret;
	}
	(void) big_copy(&(key->dmodpminus1), &f);
	(void) big_sub(&a, &c, &big_One);
	if ((brv = big_div_pos(&a, &f, &d, &a)) != BIG_OK) {
		goto ret;
	}
	(void) big_copy(&(key->dmodqminus1), &f);

	/* pairwise consistency check:  decrypt and encrypt restores value */
	if ((brv = big_random(&h, size, rf, B_FALSE)) != BIG_OK) {
		goto ret;
	}
	if ((brv = big_div_pos(&a, &h, &h, &g)) != BIG_OK) {
		goto ret;
	}
	if ((brv = big_modexp(&a, &h, &d, &g, NULL)) != BIG_OK) {
		goto ret;
	}

	if ((brv = big_modexp(&b, &a, &e, &g, NULL)) != BIG_OK) {
		goto ret;
	}

	if (big_cmp_abs(&b, &h) != 0) {
		/* this should not happen */
		rv = generate_rsa_key(key, psize, qsize, pubexp, rf);
		goto ret1;
	} else {
		brv = BIG_OK;
	}

ret:
	rv = convert_rv(brv);
ret1:
	big_finish(&h);
	big_finish(&g);
	big_finish(&f);
	big_finish(&e);
	big_finish(&d);
	big_finish(&c);
	big_finish(&b);
	big_finish(&a);

/* EXPORT DELETE END */

	return (rv);
}

CK_RV
rsa_genkey_pair(RSAbytekey *bkey)
{
	/*
	 * NOTE:  Whomever originally wrote this function swapped p and q.
	 * This table shows the mapping between name convention used here
	 * versus what is used in most texts that describe RSA key generation.
	 *	This function:			Standard convention:
	 *	--------------			--------------------
	 *	modulus, n			-same-
	 *	prime 1, q			prime 1, p
	 *	prime 2, p			prime 2, q
	 *	private exponent, d		-same-
	 *	public exponent, e		-same-
	 *	exponent 1, d mod (q-1)		d mod (p-1)
	 *	exponent 2, d mod (p-1)		d mod (q-1)
	 *	coefficient, p^-1 mod q		q^-1 mod p
	 *
	 * Also notice the struct member for coefficient is named .pinvmodq
	 * rather than .qinvmodp, reflecting the switch.
	 *
	 * The code here wasn't unswapped, because "it works".  Further,
	 * p and q are interchangeable as long as exponent 1 and 2 and
	 * the coefficient are kept straight too.  This note is here to
	 * make the reader aware of the switcheroo.
	 */
	CK_RV	rv = CKR_OK;

/* EXPORT DELETE START */

	BIGNUM	public_exponent = {0};
	RSAkey	rsakey;
	uint32_t modulus_bytes;

	if (bkey == NULL)
		return (CKR_ARGUMENTS_BAD);

	/* Must have modulus bits set */
	if (bkey->modulus_bits == 0)
		return (CKR_ARGUMENTS_BAD);

	/* Must have public exponent set */
	if (bkey->pubexpo_bytes == 0 || bkey->pubexpo == NULL)
		return (CKR_ARGUMENTS_BAD);

	/* Note: modulus_bits may not be same as (8 * sizeof (modulus)) */
	modulus_bytes = CRYPTO_BITS2BYTES(bkey->modulus_bits);

	/* Modulus length needs to be between min key size and max key size. */
	if ((modulus_bytes < MIN_RSA_KEYLENGTH_IN_BYTES) ||
	    (modulus_bytes > MAX_RSA_KEYLENGTH_IN_BYTES)) {
		return (CKR_KEY_SIZE_RANGE);
	}

	/*
	 * Initialize the RSA key.
	 */
	if (RSA_key_init(&rsakey, modulus_bytes * 4, modulus_bytes * 4) !=
	    BIG_OK) {
		return (CKR_HOST_MEMORY);
	}

	/* Create a public exponent in bignum format. */
	if (big_init(&public_exponent,
	    CHARLEN2BIGNUMLEN(bkey->pubexpo_bytes)) != BIG_OK) {
		rv = CKR_HOST_MEMORY;
		goto clean1;
	}
	bytestring2bignum(&public_exponent, bkey->pubexpo, bkey->pubexpo_bytes);

	/* Generate RSA key pair. */
	if ((rv = generate_rsa_key(&rsakey,
	    modulus_bytes * 4, modulus_bytes * 4, &public_exponent,
	    bkey->rfunc)) != CKR_OK) {
		big_finish(&public_exponent);
		goto clean1;
	}
	big_finish(&public_exponent);

	/* modulus_bytes = rsakey.n.len * (int)sizeof (BIG_CHUNK_TYPE); */
	bignum2bytestring(bkey->modulus, &(rsakey.n), modulus_bytes);

	bkey->privexpo_bytes = rsakey.d.len * (int)sizeof (BIG_CHUNK_TYPE);
	bignum2bytestring(bkey->privexpo, &(rsakey.d), bkey->privexpo_bytes);

	bkey->pubexpo_bytes = rsakey.e.len * (int)sizeof (BIG_CHUNK_TYPE);
	bignum2bytestring(bkey->pubexpo, &(rsakey.e), bkey->pubexpo_bytes);

	bkey->prime1_bytes = rsakey.q.len * (int)sizeof (BIG_CHUNK_TYPE);
	bignum2bytestring(bkey->prime1, &(rsakey.q), bkey->prime1_bytes);

	bkey->prime2_bytes = rsakey.p.len * (int)sizeof (BIG_CHUNK_TYPE);
	bignum2bytestring(bkey->prime2, &(rsakey.p), bkey->prime2_bytes);

	bkey->expo1_bytes =
	    rsakey.dmodqminus1.len * (int)sizeof (BIG_CHUNK_TYPE);
	bignum2bytestring(bkey->expo1, &(rsakey.dmodqminus1),
	    bkey->expo1_bytes);

	bkey->expo2_bytes =
	    rsakey.dmodpminus1.len * (int)sizeof (BIG_CHUNK_TYPE);
	bignum2bytestring(bkey->expo2,
	    &(rsakey.dmodpminus1), bkey->expo2_bytes);

	bkey->coeff_bytes =
	    rsakey.pinvmodq.len * (int)sizeof (BIG_CHUNK_TYPE);
	bignum2bytestring(bkey->coeff, &(rsakey.pinvmodq), bkey->coeff_bytes);

clean1:
	RSA_key_finish(&rsakey);

/* EXPORT DELETE END */

	return (rv);
}

#define	MAX(a, b) ((a) < (b) ? (b) : (a))

/*
 * RSA encrypt operation
 */
CK_RV
rsa_encrypt(RSAbytekey *bkey, uchar_t *in, uint32_t in_len, uchar_t *out)
{
	CK_RV rv = CKR_OK;

/* EXPORT DELETE START */

	BIGNUM msg;
	RSAkey rsakey;
	uint32_t modulus_bytes;

	if (bkey == NULL)
		return (CKR_ARGUMENTS_BAD);

	/* Must have modulus and public exponent set */
	if (bkey->modulus_bits == 0 || bkey->modulus == NULL ||
	    bkey->pubexpo_bytes == 0 || bkey->pubexpo == NULL)
		return (CKR_ARGUMENTS_BAD);

	/* Note: modulus_bits may not be same as (8 * sizeof (modulus)) */
	modulus_bytes = CRYPTO_BITS2BYTES(bkey->modulus_bits);

	if (bkey->pubexpo_bytes > modulus_bytes) {
		return (CKR_KEY_SIZE_RANGE);
	}

	/* psize and qsize for RSA_key_init is in bits. */
#ifdef  YF_MONTMUL
	/* T4 optimization needs bigger than actual size for the bignums */
	if (RSA_key_init(&rsakey, modulus_bytes * 8, modulus_bytes * 8) !=
	    BIG_OK) {
#else
	if (RSA_key_init(&rsakey, modulus_bytes * 4, modulus_bytes * 4) !=
	    BIG_OK) {
#endif
		return (CKR_HOST_MEMORY);
	}

	/* Size for big_init is in BIG_CHUNK_TYPE words. */
#ifdef  YF_MONTMUL
	if (big_init(&msg,
	    MAX(CHARLEN2BIGNUMLEN(in_len), modulus_bytes * 8)) != BIG_OK) {
#else
	if (big_init(&msg, CHARLEN2BIGNUMLEN(in_len)) != BIG_OK) {
#endif
		rv = CKR_HOST_MEMORY;
		goto clean2;
	}
	bytestring2bignum(&msg, in, in_len);

	/* Convert public exponent and modulus to big integer format. */
	bytestring2bignum(&(rsakey.e), bkey->pubexpo, bkey->pubexpo_bytes);
	bytestring2bignum(&(rsakey.n), bkey->modulus, modulus_bytes);

	if (big_cmp_abs(&msg, &(rsakey.n)) > 0) {
		rv = CKR_DATA_LEN_RANGE;
		goto clean3;
	}

	/* Perform RSA computation on big integer input data. */
	if (big_modexp(&msg, &msg, &(rsakey.e), &(rsakey.n), NULL) !=
	    BIG_OK) {
		rv = CKR_HOST_MEMORY;
		goto clean3;
	}

	/* Convert the big integer output data to octet string. */
	bignum2bytestring(out, &msg, modulus_bytes);

clean3:
	big_finish(&msg);
clean2:
	RSA_key_finish(&rsakey);

/* EXPORT DELETE END */

	return (rv);
}

/*
 * RSA decrypt operation
 */
CK_RV
rsa_decrypt(RSAbytekey *bkey, uchar_t *in, uint32_t in_len, uchar_t *out)
{
	CK_RV rv = CKR_OK;

/* EXPORT DELETE START */

	BIGNUM msg;
	RSAkey rsakey;
	uint32_t modulus_bytes;

	if (bkey == NULL)
		return (CKR_ARGUMENTS_BAD);

	/* Must have modulus, prime1, prime2, expo1, expo2, and coeff set */
	if (bkey->modulus_bits == 0 || bkey->modulus == NULL ||
	    bkey->prime1_bytes == 0 || bkey->prime1 == NULL ||
	    bkey->prime2_bytes == 0 || bkey->prime2 == NULL ||
	    bkey->expo1_bytes == 0 || bkey->expo1 == NULL ||
	    bkey->expo2_bytes == 0 || bkey->expo2 == NULL ||
	    bkey->coeff_bytes == 0 || bkey->coeff == NULL)
		return (CKR_ARGUMENTS_BAD);

	/* Note: modulus_bits may not be same as (8 * sizeof (modulus)) */
	modulus_bytes = CRYPTO_BITS2BYTES(bkey->modulus_bits);

	/* psize and qsize for RSA_key_init is in bits. */
#ifdef YF_MONTMUL
	/* T4 optimization needs bigger than actual size for the bignums */
	if (RSA_key_init(&rsakey, 2 * CRYPTO_BYTES2BITS(bkey->prime2_bytes),
	    2 * CRYPTO_BYTES2BITS(bkey->prime1_bytes)) != BIG_OK) {
#else
	if (RSA_key_init(&rsakey, CRYPTO_BYTES2BITS(bkey->prime2_bytes),
	    CRYPTO_BYTES2BITS(bkey->prime1_bytes)) != BIG_OK) {
#endif
		return (CKR_HOST_MEMORY);
	}

	/* Size for big_init is in BIG_CHUNK_TYPE words. */
#ifdef  YF_MONTMUL
	if (big_init(&msg,
	    MAX(CHARLEN2BIGNUMLEN(in_len),
	    CRYPTO_BYTES2BITS(bkey->prime2_bytes) +
	    CRYPTO_BYTES2BITS(bkey->prime1_bytes))) != BIG_OK) {
#else
	if (big_init(&msg, CHARLEN2BIGNUMLEN(in_len)) != BIG_OK) {
#endif
		rv = CKR_HOST_MEMORY;
		goto clean3;
	}
	/* Convert octet string input data to big integer format. */
	bytestring2bignum(&msg, in, in_len);

	/* Convert octet string modulus to big integer format. */
	bytestring2bignum(&(rsakey.n), bkey->modulus, modulus_bytes);

	if (big_cmp_abs(&msg, &(rsakey.n)) > 0) {
		rv = CKR_DATA_LEN_RANGE;
		goto clean4;
	}

	/* Convert the rest of private key attributes to big integer format. */
	bytestring2bignum(&(rsakey.q), bkey->prime1, bkey->prime1_bytes);
	bytestring2bignum(&(rsakey.p), bkey->prime2, bkey->prime2_bytes);
	bytestring2bignum(&(rsakey.dmodqminus1),
	    bkey->expo1, bkey->expo1_bytes);
	bytestring2bignum(&(rsakey.dmodpminus1),
	    bkey->expo2, bkey->expo2_bytes);
	bytestring2bignum(&(rsakey.pinvmodq),
	    bkey->coeff, bkey->coeff_bytes);

	if ((big_cmp_abs(&(rsakey.dmodpminus1), &(rsakey.p)) > 0) ||
	    (big_cmp_abs(&(rsakey.dmodqminus1), &(rsakey.q)) > 0) ||
	    (big_cmp_abs(&(rsakey.pinvmodq), &(rsakey.q)) > 0)) {
		rv = CKR_KEY_SIZE_RANGE;
		goto clean4;
	}

	/* Perform RSA computation on big integer input data. */
	if (big_modexp_crt(&msg, &msg, &(rsakey.dmodpminus1),
	    &(rsakey.dmodqminus1), &(rsakey.p), &(rsakey.q),
	    &(rsakey.pinvmodq), NULL, NULL) != BIG_OK) {
		rv = CKR_HOST_MEMORY;
		goto clean4;
	}

	/* Convert the big integer output data to octet string. */
	bignum2bytestring(out, &msg, modulus_bytes);

clean4:
	big_finish(&msg);
clean3:
	RSA_key_finish(&rsakey);

/* EXPORT DELETE END */

	return (rv);
}

/*
 * The functions below implement RSA for ucrypto/libsoftcrypto.
 * These functions were originally implemented as part of the
 * RSA provider for the Kernel Cryptographic Framework (KCF)
 */

/*
 * Context for RSA_PKCS/RSA_X_509 and MD5_RSA_PKCS/SHA*_RSA_PKCS mechanisms.
 */
typedef struct rsa_ctx {
	rsa_mech_type_t	mech_type;
	crypto_key_t *key;
	size_t keychunk_size;
	union {		/* Only MD5_RSA_PKCS/SHA*_RSA_PKCS uses this union. */
		MD5_CTX md5ctx;
		SHA1_CTX sha1ctx;
		SHA2_CTX sha2ctx;
	} dctx_u;
} rsa_ctx_t;

#define	md5_ctx		dctx_u.md5ctx
#define	sha1_ctx	dctx_u.sha1ctx
#define	sha2_ctx	dctx_u.sha2ctx

#define	RSA_VALID_CIPHER_MECH(mech)			\
	(((mech)->cm_type == CRYPTO_RSA_PKCS ||		\
	(mech)->cm_type == CRYPTO_RSA_X_509) ? 1 : 0)

#define	RSA_VALID_DIGEST_MECH(mech)			\
	(((mech)->cm_type == CRYPTO_MD5_RSA_PKCS ||	\
	(mech)->cm_type == CRYPTO_SHA1_RSA_PKCS ||	\
	(mech)->cm_type == CRYPTO_SHA256_RSA_PKCS ||	\
	(mech)->cm_type == CRYPTO_SHA384_RSA_PKCS ||	\
	(mech)->cm_type == CRYPTO_SHA512_RSA_PKCS) ? 1 : 0)

#define	RSA_VALID_MECH(mech)				\
	(RSA_VALID_CIPHER_MECH((mech)) ||		\
	RSA_VALID_DIGEST_MECH((mech)))


static int rsaprov_encrypt_common(rsa_mech_type_t, crypto_key_t *,
    crypto_data_t *, crypto_data_t *);
static int rsaprov_decrypt_common(rsa_mech_type_t, crypto_key_t *,
    crypto_data_t *, crypto_data_t *);
static int rsaprov_sign_common(rsa_mech_type_t, crypto_key_t *,
    crypto_data_t *, crypto_data_t *);
static int rsaprov_verify_common(rsa_mech_type_t, crypto_key_t *,
    crypto_data_t *, crypto_data_t *);
static int rsaprov_compare_data(crypto_data_t *, uchar_t *);

/* EXPORT DELETE START */

static int core_rsa_encrypt(crypto_key_t *, uchar_t *, int, uchar_t *, int);
static int core_rsa_decrypt(crypto_key_t *, uchar_t *, int, uchar_t *);

/* EXPORT DELETE END */

#ifdef _KERNEL

/*
 * kmemset() and knzero_random_generator() need to be defined for
 * padding/pkcs1.c, which the RSA implementation depends on.
 */

void
kmemset(uint8_t *buf, char pattern, size_t len)
{
	int i = 0;

	while (i < len)
		buf[i++] = pattern;
}

int
knzero_random_generator(uint8_t *ran_out, size_t ran_len)
{
	int rv;
	size_t ebc = 0; /* count of extra bytes in extrarand */
	size_t i = 0;
	uint8_t extrarand[32];
	size_t extrarand_len;

	if ((rv = random_get_pseudo_bytes_fips140(ran_out, ran_len)) != 0)
		return (rv);

	/*
	 * Walk through the returned random numbers pointed by ran_out,
	 * and look for any random number which is zero.
	 * If we find zero, call random_get_pseudo_bytes() to generate
	 * another 32 random numbers pool. Replace any zeros in ran_out[]
	 * from the random number in pool.
	 */
	while (i < ran_len) {
		if (ran_out[i] != 0) {
			i++;
			continue;
		}

		/*
		 * Note that it is 'while' so we are guaranteed a
		 * non-zero value on exit.
		 */
		if (ebc == 0) {
			/* refresh extrarand */
			extrarand_len = sizeof (extrarand);
			if ((rv = random_get_pseudo_bytes_fips140(extrarand,
			    extrarand_len)) != 0) {
				return (rv);
			}

			ebc = extrarand_len;
		}
		/* Replace zero with byte from extrarand. */
		-- ebc;

		/*
		 * The new random byte zero/non-zero will be checked in
		 * the next pass through the loop.
		 */
		ran_out[i] = extrarand[ebc];
	}

	return (CRYPTO_SUCCESS);
}

#endif /* _KERNEL */

static int
rsaprov_check_mech_and_key(crypto_mechanism_t *mechanism, crypto_key_t *key)
{
	int rv = CRYPTO_FAILED;

/* EXPORT DELETE START */

	uchar_t *modulus;
	ssize_t modulus_len; /* In bytes */

	if (mechanism == NULL || !RSA_VALID_MECH(mechanism))
		return (CRYPTO_MECHANISM_INVALID);

	/*
	 * We only support RSA keys that are passed as a list of
	 * object attributes.
	 */
	if (key == NULL || key->ck_format != CRYPTO_KEY_ATTR_LIST) {
		return (CRYPTO_KEY_TYPE_INCONSISTENT);
	}

	if ((rv = crypto_get_key_attr(key, SUN_CKA_MODULUS, &modulus,
	    &modulus_len)) != CRYPTO_SUCCESS) {
		return (rv);
	}
	if (modulus_len < MIN_RSA_KEYLENGTH_IN_BYTES ||
	    modulus_len > MAX_RSA_KEYLENGTH_IN_BYTES)
		return (CRYPTO_KEY_SIZE_RANGE);

/* EXPORT DELETE END */

	return (rv);
}

static int
rsaprov_compare_data(crypto_data_t *data, uchar_t *buf)
{
	int len;
	uchar_t *dptr;

	len = data->cd_length;
	switch (data->cd_format) {
	case CRYPTO_DATA_RAW:
		dptr = (uchar_t *)data->cd_raw.iov_base + data->cd_offset;
		return (bcmp(dptr, buf, len));

#ifdef _KERNEL
	case CRYPTO_DATA_UIO:
		return (crypto_uio_data(data, buf, len,
		    COMPARE_TO_DATA, NULL, NULL));

	case CRYPTO_DATA_MBLK:
		return (crypto_mblk_data(data, buf, len,
		    COMPARE_TO_DATA, NULL, NULL));
#endif /* _KERNEL */
	}

	return (CRYPTO_FAILED);
}

int
rsaprov_free_context(crypto_ctx_t *ctx)
{
	rsa_ctx_t *ctxp = ctx->cc_provider_private;

	if (ctxp != NULL) {
		CRYPTO_ZFREE(ctxp->key, ctxp->keychunk_size);
		CRYPTO_FREE(ctxp, sizeof (rsa_ctx_t));
		ctx->cc_provider_private = NULL;
	}

	return (CRYPTO_SUCCESS);
}

/* ARGSUSED */
int
rsaprov_common_init(crypto_ctx_t *ctx, crypto_mechanism_t *mechanism,
    crypto_key_t *key, crypto_spi_ctx_template_t template,
    crypto_req_handle_t req)
{
	int rv;
	int kmflag;
	rsa_ctx_t *ctxp;

	if (ctx == NULL)
		return (CRYPTO_ARGUMENTS_BAD);

	if ((rv = rsaprov_check_mech_and_key(mechanism, key)) != CRYPTO_SUCCESS)
		return (rv);

	/*
	 * Allocate a RSA context.
	 */
	kmflag = CRYPTO_KMFLAG(req);
	if ((ctxp = CRYPTO_ZALLOC(sizeof (rsa_ctx_t), kmflag)) == NULL)
		return (CRYPTO_HOST_MEMORY);

	if ((rv = crypto_copy_key_to_ctx(key, &ctxp->key, &ctxp->keychunk_size,
	    kmflag)) != CRYPTO_SUCCESS) {
		CRYPTO_FREE(ctxp, sizeof (rsa_ctx_t));
		return (rv);
	}
	ctxp->mech_type = mechanism->cm_type;

	ctx->cc_provider_private = ctxp;

	return (CRYPTO_SUCCESS);
}

/* ARGSUSED */
int
rsaprov_encrypt(crypto_ctx_t *ctx, crypto_data_t *plaintext,
    crypto_data_t *ciphertext, crypto_req_handle_t req)
{
	int rv;
	rsa_ctx_t *ctxp;

	if (ctx == NULL || plaintext == NULL)
		return (CRYPTO_ARGUMENTS_BAD);

	ASSERT(ctx->cc_provider_private != NULL);
	ctxp = ctx->cc_provider_private;

	CRYPTO_ARG_INPLACE(plaintext, ciphertext);

	/*
	 * Note on the KM_SLEEP flag passed to the routine below -
	 * rsaprov_encrypt() is a single-part encryption routine which is
	 * currently usable only by /dev/crypto. Since /dev/crypto calls are
	 * always synchronous, we can safely pass KM_SLEEP here.
	 */
	rv = rsaprov_encrypt_common(ctxp->mech_type, ctxp->key, plaintext,
	    ciphertext);

	if (rv != CRYPTO_BUFFER_TOO_SMALL)
		(void) rsaprov_free_context(ctx);

	return (rv);
}

/* ARGSUSED */
int
rsaprov_encrypt_atomic(crypto_provider_handle_t provider,
    crypto_session_id_t session_id, crypto_mechanism_t *mechanism,
    crypto_key_t *key, crypto_data_t *plaintext, crypto_data_t *ciphertext,
    crypto_spi_ctx_template_t template, crypto_req_handle_t req)
{
	int rv;

	if (plaintext == NULL)
		return (CRYPTO_ARGUMENTS_BAD);

	if ((rv = rsaprov_check_mech_and_key(mechanism, key)) != CRYPTO_SUCCESS)
		return (rv);

	CRYPTO_ARG_INPLACE(plaintext, ciphertext);

	return (rsaprov_encrypt_common(mechanism->cm_type, key, plaintext,
	    ciphertext));
}

static int
rsaprov_encrypt_common(rsa_mech_type_t mech_type, crypto_key_t *key,
    crypto_data_t *plaintext, crypto_data_t *ciphertext)
{
	int rv = CRYPTO_FAILED;

/* EXPORT DELETE START */

	int plen;
	uchar_t *ptptr;
	uchar_t *modulus;
	ssize_t modulus_len;
	uchar_t tmp_data[MAX_RSA_KEYLENGTH_IN_BYTES];
	uchar_t plain_data[MAX_RSA_KEYLENGTH_IN_BYTES];
	uchar_t cipher_data[MAX_RSA_KEYLENGTH_IN_BYTES];

	if ((rv = crypto_get_key_attr(key, SUN_CKA_MODULUS, &modulus,
	    &modulus_len)) != CRYPTO_SUCCESS) {
		return (rv);
	}

	plen = plaintext->cd_length;
	if (mech_type == CRYPTO_RSA_PKCS) {
		if (plen > (modulus_len - MIN_PKCS1_PADLEN))
			return (CRYPTO_DATA_LEN_RANGE);
	} else {
		if (plen > modulus_len)
			return (CRYPTO_DATA_LEN_RANGE);
	}

	/*
	 * Output buf len must not be less than RSA modulus size.
	 */
	if (ciphertext->cd_length < modulus_len) {
		ciphertext->cd_length = modulus_len;
		return (CRYPTO_BUFFER_TOO_SMALL);
	}

	ASSERT(plaintext->cd_length <= sizeof (tmp_data));
	if ((rv = crypto_get_input_data(plaintext, &ptptr, tmp_data))
	    != CRYPTO_SUCCESS)
		return (rv);

	if (mech_type == CRYPTO_RSA_PKCS) {
		rv = pkcs1_encode(PKCS1_ENCRYPT, ptptr, plen,
		    plain_data, modulus_len);

		if (rv != CRYPTO_SUCCESS)
			return (rv);
	} else {
		bzero(plain_data, modulus_len - plen);
		bcopy(ptptr, &plain_data[modulus_len - plen], plen);
	}

	rv = core_rsa_encrypt(key, plain_data, modulus_len, cipher_data, 1);
	if (rv == CRYPTO_SUCCESS) {
		/* copy out to ciphertext */
		if ((rv = crypto_put_output_data(cipher_data,
		    ciphertext, modulus_len)) != CRYPTO_SUCCESS)
			return (rv);

		ciphertext->cd_length = modulus_len;
	}

/* EXPORT DELETE END */

	return (rv);
}

/* EXPORT DELETE START */

static int
core_rsa_encrypt(crypto_key_t *key, uchar_t *in,
    int in_len, uchar_t *out, int is_public)
{
	int rv;
	uchar_t *expo, *modulus;
	ssize_t	expo_len;
	ssize_t modulus_len;
	RSAbytekey k;

	if (is_public) {
		if ((rv = crypto_get_key_attr(key, SUN_CKA_PUBLIC_EXPONENT,
		    &expo, &expo_len)) != CRYPTO_SUCCESS)
			return (rv);
	} else {
		/*
		 * SUN_CKA_PRIVATE_EXPONENT is a required attribute for a
		 * RSA secret key. See the comments in core_rsa_decrypt
		 * routine which calls this routine with a private key.
		 */
		if ((rv = crypto_get_key_attr(key, SUN_CKA_PRIVATE_EXPONENT,
		    &expo, &expo_len)) != CRYPTO_SUCCESS)
			return (rv);
	}

	if ((rv = crypto_get_key_attr(key, SUN_CKA_MODULUS, &modulus,
	    &modulus_len)) != CRYPTO_SUCCESS) {
		return (rv);
	}

	k.modulus = modulus;
	k.modulus_bits = CRYPTO_BYTES2BITS(modulus_len);
	k.pubexpo = expo;
	k.pubexpo_bytes = expo_len;
	k.rfunc = NULL;

	rv = rsa_encrypt(&k, in, in_len, out);

	return (rv);
}

/* EXPORT DELETE END */

/* ARGSUSED */
int
rsaprov_decrypt(crypto_ctx_t *ctx, crypto_data_t *ciphertext,
    crypto_data_t *plaintext, crypto_req_handle_t req)
{
	int rv;
	rsa_ctx_t *ctxp;

	if (ctx == NULL || ciphertext == NULL)
		return (CRYPTO_ARGUMENTS_BAD);

	ASSERT(ctx->cc_provider_private != NULL);
	ctxp = ctx->cc_provider_private;

	CRYPTO_ARG_INPLACE(ciphertext, plaintext);

	/* See the comments on KM_SLEEP flag in rsaprov_encrypt() */
	rv = rsaprov_decrypt_common(ctxp->mech_type, ctxp->key,
	    ciphertext, plaintext);

	if (rv != CRYPTO_BUFFER_TOO_SMALL)
		(void) rsaprov_free_context(ctx);

	return (rv);
}

/* ARGSUSED */
int
rsaprov_decrypt_atomic(crypto_provider_handle_t provider,
    crypto_session_id_t session_id, crypto_mechanism_t *mechanism,
    crypto_key_t *key, crypto_data_t *ciphertext, crypto_data_t *plaintext,
    crypto_spi_ctx_template_t template, crypto_req_handle_t req)
{
	int rv;

	if (ciphertext == NULL)
		return (CRYPTO_ARGUMENTS_BAD);

	if ((rv = rsaprov_check_mech_and_key(mechanism, key)) != CRYPTO_SUCCESS)
		return (rv);

	CRYPTO_ARG_INPLACE(ciphertext, plaintext);

	return (rsaprov_decrypt_common(mechanism->cm_type, key, ciphertext,
	    plaintext));
}

static int
rsaprov_decrypt_common(rsa_mech_type_t mech_type, crypto_key_t *key,
    crypto_data_t *ciphertext, crypto_data_t *plaintext)
{
	int rv = CRYPTO_FAILED;

/* EXPORT DELETE START */

	size_t plain_len;
	uchar_t *ctptr;
	uchar_t *modulus;
	ssize_t modulus_len;
	uchar_t plain_data[MAX_RSA_KEYLENGTH_IN_BYTES];
	uchar_t tmp_data[MAX_RSA_KEYLENGTH_IN_BYTES];

	if ((rv = crypto_get_key_attr(key, SUN_CKA_MODULUS, &modulus,
	    &modulus_len)) != CRYPTO_SUCCESS) {
		return (rv);
	}

	/*
	 * Ciphertext length must be equal to RSA modulus size.
	 */
	if (ciphertext->cd_length != modulus_len)
		return (CRYPTO_ENCRYPTED_DATA_LEN_RANGE);

	ASSERT(ciphertext->cd_length <= sizeof (tmp_data));
	if ((rv = crypto_get_input_data(ciphertext, &ctptr, tmp_data))
	    != CRYPTO_SUCCESS)
		return (rv);

	rv = core_rsa_decrypt(key, ctptr, modulus_len, plain_data);
	if (rv == CRYPTO_SUCCESS) {
		plain_len = modulus_len;

		if (mech_type == CRYPTO_RSA_PKCS) {
			/* Strip off the PKCS block formatting data. */
			rv = pkcs1_decode(PKCS1_DECRYPT, plain_data,
			    &plain_len);
			if (rv != CRYPTO_SUCCESS)
				return (rv);
		}

		if (plain_len > plaintext->cd_length) {
			plaintext->cd_length = plain_len;
			return (CRYPTO_BUFFER_TOO_SMALL);
		}

		if ((rv = crypto_put_output_data(
		    plain_data + modulus_len - plain_len,
		    plaintext, plain_len)) != CRYPTO_SUCCESS)
			return (rv);

		plaintext->cd_length = plain_len;
	}

/* EXPORT DELETE END */

	return (rv);
}

/* EXPORT DELETE START */

static int
core_rsa_decrypt(crypto_key_t *key, uchar_t *in, int in_len, uchar_t *out)
{
	int rv;
	uchar_t *modulus, *prime1, *prime2, *expo1, *expo2, *coef;
	ssize_t modulus_len;
	ssize_t	prime1_len, prime2_len;
	ssize_t	expo1_len, expo2_len, coef_len;
	RSAbytekey k;

	if ((rv = crypto_get_key_attr(key, SUN_CKA_MODULUS, &modulus,
	    &modulus_len)) != CRYPTO_SUCCESS) {
		return (rv);
	}

	/*
	 * The following attributes are not required to be
	 * present in a RSA secret key. If any of them is not present
	 * we call the encrypt routine with a flag indicating use of
	 * private exponent (d). Note that SUN_CKA_PRIVATE_EXPONENT is
	 * a required attribute for a RSA secret key.
	 */
	if ((crypto_get_key_attr(key, SUN_CKA_PRIME_1, &prime1, &prime1_len)
	    != CRYPTO_SUCCESS) ||
	    (crypto_get_key_attr(key, SUN_CKA_PRIME_2, &prime2, &prime2_len)
	    != CRYPTO_SUCCESS) ||
	    (crypto_get_key_attr(key, SUN_CKA_EXPONENT_1, &expo1, &expo1_len)
	    != CRYPTO_SUCCESS) ||
	    (crypto_get_key_attr(key, SUN_CKA_EXPONENT_2, &expo2, &expo2_len)
	    != CRYPTO_SUCCESS) ||
	    (crypto_get_key_attr(key, SUN_CKA_COEFFICIENT, &coef, &coef_len)
	    != CRYPTO_SUCCESS)) {
		return (core_rsa_encrypt(key, in, in_len, out, 0));
	}

	k.modulus = modulus;
	k.modulus_bits = CRYPTO_BYTES2BITS(modulus_len);
	k.prime1 = prime1;
	k.prime1_bytes = prime1_len;
	k.prime2 = prime2;
	k.prime2_bytes = prime2_len;
	k.expo1 = expo1;
	k.expo1_bytes = expo1_len;
	k.expo2 = expo2;
	k.expo2_bytes = expo2_len;
	k.coeff = coef;
	k.coeff_bytes = coef_len;
	k.rfunc = NULL;

	rv = rsa_decrypt(&k, in, in_len, out);

	return (rv);
}

/* EXPORT DELETE END */

/* ARGSUSED */
int
rsaprov_sign_verify_common_init(crypto_ctx_t *ctx,
    crypto_mechanism_t *mechanism, crypto_key_t *key,
    crypto_spi_ctx_template_t ctx_template, crypto_req_handle_t req)
{
	int rv;
	int kmflag;
	rsa_ctx_t *ctxp;

	if (ctx == NULL)
		return (CRYPTO_ARGUMENTS_BAD);

	if ((rv = rsaprov_check_mech_and_key(mechanism, key)) != CRYPTO_SUCCESS)
		return (rv);

	/*
	 * Allocate a RSA context.
	 */
	kmflag = CRYPTO_KMFLAG(req);
	ctxp = CRYPTO_ZALLOC(sizeof (rsa_ctx_t), kmflag);
	if (ctxp == NULL)
		return (CRYPTO_HOST_MEMORY);

	ctxp->mech_type = mechanism->cm_type;
	if ((rv = crypto_copy_key_to_ctx(key, &ctxp->key, &ctxp->keychunk_size,
	    kmflag)) != CRYPTO_SUCCESS) {
		CRYPTO_FREE(ctxp, sizeof (rsa_ctx_t));
		return (rv);
	}

	switch (mechanism->cm_type) {
	case CRYPTO_RSA_PKCS:
	case CRYPTO_RSA_X_509:
		break;

	case CRYPTO_MD5_RSA_PKCS:
		MD5Init(&(ctxp->md5_ctx));
		break;

	case CRYPTO_SHA1_RSA_PKCS:
		SHA1Init(&(ctxp->sha1_ctx));
		break;

	case CRYPTO_SHA256_RSA_PKCS:
		SHA2Init(SHA256, &(ctxp->sha2_ctx));
		break;

	case CRYPTO_SHA384_RSA_PKCS:
		SHA2Init(SHA384, &(ctxp->sha2_ctx));
		break;

	case CRYPTO_SHA512_RSA_PKCS:
		SHA2Init(SHA512, &(ctxp->sha2_ctx));
		break;

	default:
		CRYPTO_FREE(ctxp, sizeof (rsa_ctx_t));
		return (CRYPTO_MECH_NOT_SUPPORTED);
	}

	ctx->cc_provider_private = ctxp;

	return (CRYPTO_SUCCESS);
}

static int
rsaprov_digest_svrfy_common(rsa_ctx_t *ctxp, crypto_data_t *data,
    crypto_data_t *signature, uchar_t flag)
{
	int rv = CRYPTO_FAILED;

/* EXPORT DELETE START */

	uchar_t digest[SHA512_DIGEST_LENGTH];
	/* The der_data size is enough for MD5 also */
	uchar_t der_data[SHA512_DIGEST_LENGTH + SHA2_DER_PREFIX_Len];
	ulong_t der_data_len;
	crypto_data_t der_cd;
	rsa_mech_type_t mech_type;

	ASSERT(flag & CRYPTO_DO_SIGN || flag & CRYPTO_DO_VERIFY);
	ASSERT(data != NULL || (flag & CRYPTO_DO_FINAL));

	mech_type = ctxp->mech_type;
	if (mech_type == CRYPTO_RSA_PKCS ||
	    mech_type == CRYPTO_RSA_X_509)
		return (CRYPTO_MECHANISM_INVALID);

	/*
	 * We need to do the BUFFER_TOO_SMALL check before digesting
	 * the data. No check is needed for verify as signature is not
	 * an output argument for verify.
	 */
	if (flag & CRYPTO_DO_SIGN) {
		uchar_t *modulus;
		ssize_t modulus_len;

		if ((rv = crypto_get_key_attr(ctxp->key, SUN_CKA_MODULUS,
		    &modulus, &modulus_len)) != CRYPTO_SUCCESS) {
			return (rv);
		}

		if (signature->cd_length < modulus_len) {
			signature->cd_length = modulus_len;
			return (CRYPTO_BUFFER_TOO_SMALL);
		}
	}

	if (mech_type == CRYPTO_MD5_RSA_PKCS)
		rv = crypto_digest_data(data, &(ctxp->md5_ctx),
		    digest, MD5Update, MD5Final, flag | CRYPTO_DO_MD5);

	else if (mech_type == CRYPTO_SHA1_RSA_PKCS)
		rv = crypto_digest_data(data, &(ctxp->sha1_ctx),
		    digest, SHA1Update, SHA1Final, flag | CRYPTO_DO_SHA1);

	else if (mech_type == CRYPTO_SHA256_RSA_PKCS ||
	    mech_type == CRYPTO_SHA384_RSA_PKCS ||
	    mech_type == CRYPTO_SHA512_RSA_PKCS)
		rv = crypto_digest_data(data, &(ctxp->sha2_ctx),
		    digest, SHA2Update, SHA2Final, flag | CRYPTO_DO_SHA2);
	else
		rv = CRYPTO_MECH_NOT_SUPPORTED;

	if (rv != CRYPTO_SUCCESS)
		return (rv);


	/*
	 * Prepare the DER encoding of the DigestInfo value as follows:
	 * MD5:		MD5_DER_PREFIX || H
	 * SHA-1:	SHA1_DER_PREFIX || H
	 *
	 * See rsa_impl.c for more details.
	 */
	switch (mech_type) {
	case CRYPTO_MD5_RSA_PKCS:
		bcopy(MD5_DER_PREFIX, der_data, MD5_DER_PREFIX_Len);
		bcopy(digest, der_data + MD5_DER_PREFIX_Len, MD5_DIGEST_LENGTH);
		der_data_len = MD5_DER_PREFIX_Len + MD5_DIGEST_LENGTH;
		break;

	case CRYPTO_SHA1_RSA_PKCS:
		bcopy(SHA1_DER_PREFIX, der_data, SHA1_DER_PREFIX_Len);
		bcopy(digest, der_data + SHA1_DER_PREFIX_Len,
		    SHA1_DIGEST_LENGTH);
		der_data_len = SHA1_DER_PREFIX_Len + SHA1_DIGEST_LENGTH;
		break;

	case CRYPTO_SHA256_RSA_PKCS:
		bcopy(SHA256_DER_PREFIX, der_data, SHA2_DER_PREFIX_Len);
		bcopy(digest, der_data + SHA2_DER_PREFIX_Len,
		    SHA256_DIGEST_LENGTH);
		der_data_len = SHA2_DER_PREFIX_Len + SHA256_DIGEST_LENGTH;
		break;

	case CRYPTO_SHA384_RSA_PKCS:
		bcopy(SHA384_DER_PREFIX, der_data, SHA2_DER_PREFIX_Len);
		bcopy(digest, der_data + SHA2_DER_PREFIX_Len,
		    SHA384_DIGEST_LENGTH);
		der_data_len = SHA2_DER_PREFIX_Len + SHA384_DIGEST_LENGTH;
		break;

	case CRYPTO_SHA512_RSA_PKCS:
		bcopy(SHA512_DER_PREFIX, der_data, SHA2_DER_PREFIX_Len);
		bcopy(digest, der_data + SHA2_DER_PREFIX_Len,
		    SHA512_DIGEST_LENGTH);
		der_data_len = SHA2_DER_PREFIX_Len + SHA512_DIGEST_LENGTH;
		break;

	default:
		return (CRYPTO_MECH_NOT_SUPPORTED);
	}

	CRYPTO_SET_RAW_DATA(der_cd, der_data, der_data_len);

	/*
	 * Now, we are ready to sign or verify the DER_ENCODED data.
	 */
	if (flag & CRYPTO_DO_SIGN)
		rv = rsaprov_sign_common(mech_type, ctxp->key, &der_cd,
		    signature);
	else
		rv = rsaprov_verify_common(mech_type, ctxp->key, &der_cd,
		    signature);

/* EXPORT DELETE END */

	return (rv);
}

static int
rsaprov_sign_common(rsa_mech_type_t mech_type, crypto_key_t *key,
    crypto_data_t *data, crypto_data_t *signature)
{
	int rv = CRYPTO_FAILED;

/* EXPORT DELETE START */

	int dlen;
	uchar_t *dataptr, *modulus;
	ssize_t modulus_len;
	uchar_t tmp_data[MAX_RSA_KEYLENGTH_IN_BYTES];
	uchar_t plain_data[MAX_RSA_KEYLENGTH_IN_BYTES];
	uchar_t signed_data[MAX_RSA_KEYLENGTH_IN_BYTES];

	if ((rv = crypto_get_key_attr(key, SUN_CKA_MODULUS, &modulus,
	    &modulus_len)) != CRYPTO_SUCCESS) {
		return (rv);
	}

	dlen = data->cd_length;
	switch (mech_type) {
	case CRYPTO_RSA_PKCS:
		if (dlen > (modulus_len - MIN_PKCS1_PADLEN))
			return (CRYPTO_DATA_LEN_RANGE);
		break;
	case CRYPTO_RSA_X_509:
		if (dlen > modulus_len)
			return (CRYPTO_DATA_LEN_RANGE);
		break;
	}

	if (signature->cd_length < modulus_len) {
		signature->cd_length = modulus_len;
		return (CRYPTO_BUFFER_TOO_SMALL);
	}

	ASSERT(data->cd_length <= sizeof (tmp_data));
	if ((rv = crypto_get_input_data(data, &dataptr, tmp_data))
	    != CRYPTO_SUCCESS)
		return (rv);

	switch (mech_type) {
	case CRYPTO_RSA_PKCS:
	case CRYPTO_MD5_RSA_PKCS:
	case CRYPTO_SHA1_RSA_PKCS:
	case CRYPTO_SHA256_RSA_PKCS:
	case CRYPTO_SHA384_RSA_PKCS:
	case CRYPTO_SHA512_RSA_PKCS:
		/*
		 * Add PKCS padding to the input data to format a block
		 * type "01" encryption block.
		 */
		rv = pkcs1_encode(PKCS1_SIGN, dataptr, dlen, plain_data,
		    modulus_len);
		if (rv != CRYPTO_SUCCESS)
			return (rv);

		break;

	case CRYPTO_RSA_X_509:
		bzero(plain_data, modulus_len - dlen);
		bcopy(dataptr, &plain_data[modulus_len - dlen], dlen);
		break;

	default:
		return (CRYPTO_MECH_NOT_SUPPORTED);
	}

	rv = core_rsa_decrypt(key, plain_data, modulus_len, signed_data);
	if (rv == CRYPTO_SUCCESS) {
		/* copy out to signature */
		if ((rv = crypto_put_output_data(signed_data,
		    signature, modulus_len)) != CRYPTO_SUCCESS)
			return (rv);

		signature->cd_length = modulus_len;
	}

/* EXPORT DELETE END */

	return (rv);
}

/* ARGSUSED */
int
rsaprov_sign(crypto_ctx_t *ctx, crypto_data_t *data, crypto_data_t *signature,
    crypto_req_handle_t req)
{
	int rv;
	rsa_ctx_t *ctxp;

	if (ctx == NULL || data == NULL || signature == NULL)
		return (CRYPTO_ARGUMENTS_BAD);

	ASSERT(ctx->cc_provider_private != NULL);
	ctxp = ctx->cc_provider_private;

	/* See the comments on KM_SLEEP flag in rsaprov_encrypt() */
	switch (ctxp->mech_type) {
	case CRYPTO_MD5_RSA_PKCS:
	case CRYPTO_SHA1_RSA_PKCS:
	case CRYPTO_SHA256_RSA_PKCS:
	case CRYPTO_SHA384_RSA_PKCS:
	case CRYPTO_SHA512_RSA_PKCS:
		rv = rsaprov_digest_svrfy_common(ctxp, data, signature,
		    CRYPTO_DO_SIGN | CRYPTO_DO_UPDATE | CRYPTO_DO_FINAL);
		break;
	default:
		rv = rsaprov_sign_common(ctxp->mech_type, ctxp->key, data,
		    signature);
		break;
	}

	if (rv != CRYPTO_BUFFER_TOO_SMALL)
		(void) rsaprov_free_context(ctx);

	return (rv);
}

/* ARGSUSED */
int
rsaprov_sign_update(crypto_ctx_t *ctx, crypto_data_t *data,
    crypto_req_handle_t req)
{
	int rv;
	rsa_ctx_t *ctxp;

	if (ctx == NULL || data == NULL)
		return (CRYPTO_ARGUMENTS_BAD);

	ASSERT(ctx->cc_provider_private != NULL);
	ctxp = ctx->cc_provider_private;

	switch (ctxp->mech_type) {
	case CRYPTO_RSA_PKCS:
	case CRYPTO_RSA_X_509:
		return (CRYPTO_MECHANISM_INVALID);

	case CRYPTO_MD5_RSA_PKCS:
		rv = crypto_digest_data(data, &(ctxp->md5_ctx),
		    NULL, MD5Update, MD5Final,
		    CRYPTO_DO_MD5 | CRYPTO_DO_UPDATE);
		break;

	case CRYPTO_SHA1_RSA_PKCS:
		rv = crypto_digest_data(data, &(ctxp->sha1_ctx),
		    NULL, SHA1Update, SHA1Final,
		    CRYPTO_DO_SHA1 | CRYPTO_DO_UPDATE);
		break;

	case CRYPTO_SHA256_RSA_PKCS:
	case CRYPTO_SHA384_RSA_PKCS:
	case CRYPTO_SHA512_RSA_PKCS:
		rv = crypto_digest_data(data, &(ctxp->sha2_ctx),
		    NULL, SHA2Update, SHA2Final,
		    CRYPTO_DO_SHA2 | CRYPTO_DO_UPDATE);
		break;

	default:
		return (CRYPTO_MECH_NOT_SUPPORTED);
	}

	return (rv);
}

/* ARGSUSED2 */
int
rsaprov_sign_final(crypto_ctx_t *ctx, crypto_data_t *signature,
    crypto_req_handle_t req)
{
	int rv;
	rsa_ctx_t *ctxp;
	rsa_mech_type_t mech_type;

	if (ctx == NULL || signature == NULL)
		return (CRYPTO_ARGUMENTS_BAD);

	ASSERT(ctx->cc_provider_private != NULL);
	ctxp = ctx->cc_provider_private;
	mech_type = ctxp->mech_type;

	if (mech_type == CRYPTO_RSA_PKCS ||
	    mech_type == CRYPTO_RSA_X_509)
		return (CRYPTO_MECHANISM_INVALID);

	rv = rsaprov_digest_svrfy_common(ctxp, NULL, signature,
	    CRYPTO_DO_SIGN | CRYPTO_DO_FINAL);
	if (rv != CRYPTO_BUFFER_TOO_SMALL)
		(void) rsaprov_free_context(ctx);

	return (rv);
}

/* ARGSUSED */
int
rsaprov_sign_atomic(crypto_provider_handle_t provider,
    crypto_session_id_t session_id, crypto_mechanism_t *mechanism,
    crypto_key_t *key, crypto_data_t *data, crypto_data_t *signature,
    crypto_spi_ctx_template_t ctx_template, crypto_req_handle_t req)
{
	int rv;
	rsa_ctx_t ctx;

	if (data == NULL || signature == NULL)
		return (CRYPTO_ARGUMENTS_BAD);

	if ((rv = rsaprov_check_mech_and_key(mechanism, key)) != CRYPTO_SUCCESS)
		return (rv);

	if (RSA_VALID_CIPHER_MECH(mechanism))
		rv = rsaprov_sign_common(mechanism->cm_type, key, data,
		    signature);

	else {
		ctx.mech_type = mechanism->cm_type;
		ctx.key = key;
		switch (mechanism->cm_type) {
		case CRYPTO_MD5_RSA_PKCS:
			MD5Init(&(ctx.md5_ctx));
			break;

		case CRYPTO_SHA1_RSA_PKCS:
			SHA1Init(&(ctx.sha1_ctx));
			break;

		case CRYPTO_SHA256_RSA_PKCS:
			SHA2Init(SHA256, &(ctx.sha2_ctx));
			break;

		case CRYPTO_SHA384_RSA_PKCS:
			SHA2Init(SHA384, &(ctx.sha2_ctx));
			break;

		case CRYPTO_SHA512_RSA_PKCS:
			SHA2Init(SHA512, &(ctx.sha2_ctx));
			break;

		default:
			return (CRYPTO_MECH_NOT_SUPPORTED);
		}

		rv = rsaprov_digest_svrfy_common(&ctx, data, signature,
		    CRYPTO_DO_SIGN | CRYPTO_DO_UPDATE | CRYPTO_DO_FINAL);
	}

	return (rv);
}

static int
rsaprov_verify_common(rsa_mech_type_t mech_type, crypto_key_t *key,
    crypto_data_t *data, crypto_data_t *signature)
{
	int rv = CRYPTO_FAILED;

/* EXPORT DELETE START */

	uchar_t *sigptr, *modulus;
	ssize_t modulus_len;
	uchar_t plain_data[MAX_RSA_KEYLENGTH_IN_BYTES];
	uchar_t tmp_data[MAX_RSA_KEYLENGTH_IN_BYTES];

	if ((rv = crypto_get_key_attr(key, SUN_CKA_MODULUS, &modulus,
	    &modulus_len)) != CRYPTO_SUCCESS) {
		return (rv);
	}

	if (signature->cd_length != modulus_len)
		return (CRYPTO_SIGNATURE_LEN_RANGE);

	ASSERT(signature->cd_length <= sizeof (tmp_data));
	if ((rv = crypto_get_input_data(signature, &sigptr, tmp_data))
	    != CRYPTO_SUCCESS)
		return (rv);

	rv = core_rsa_encrypt(key, sigptr, modulus_len, plain_data, 1);
	if (rv != CRYPTO_SUCCESS)
		return (rv);

	if (mech_type == CRYPTO_RSA_X_509) {
		if (rsaprov_compare_data(data, (plain_data + modulus_len
		    - data->cd_length)) != 0)
			rv = CRYPTO_SIGNATURE_INVALID;

	} else {
		size_t data_len = modulus_len;

		/*
		 * Strip off the encoded padding bytes in front of the
		 * recovered data, then compare the recovered data with
		 * the original data.
		 */
		rv = pkcs1_decode(PKCS1_VERIFY, plain_data, &data_len);
		if (rv != CRYPTO_SUCCESS)
			return (rv);

		if (data_len != data->cd_length)
			return (CRYPTO_SIGNATURE_LEN_RANGE);

		if (rsaprov_compare_data(data, (plain_data + modulus_len
		    - data_len)) != 0)
			rv = CRYPTO_SIGNATURE_INVALID;
	}

/* EXPORT DELETE END */

	return (rv);
}

/* ARGSUSED */
int
rsaprov_verify(crypto_ctx_t *ctx, crypto_data_t *data,
    crypto_data_t *signature, crypto_req_handle_t req)
{
	int rv;
	rsa_ctx_t *ctxp;

	if (ctx == NULL || data == NULL || signature == NULL)
		return (CRYPTO_ARGUMENTS_BAD);

	ASSERT(ctx->cc_provider_private != NULL);
	ctxp = ctx->cc_provider_private;

	/* See the comments on KM_SLEEP flag in rsaprov_encrypt() */
	switch (ctxp->mech_type) {
	case CRYPTO_MD5_RSA_PKCS:
	case CRYPTO_SHA1_RSA_PKCS:
	case CRYPTO_SHA256_RSA_PKCS:
	case CRYPTO_SHA384_RSA_PKCS:
	case CRYPTO_SHA512_RSA_PKCS:
		rv = rsaprov_digest_svrfy_common(ctxp, data, signature,
		    CRYPTO_DO_VERIFY | CRYPTO_DO_UPDATE | CRYPTO_DO_FINAL);
		break;
	default:
		rv = rsaprov_verify_common(ctxp->mech_type, ctxp->key, data,
		    signature);
		break;
	}

	if (rv != CRYPTO_BUFFER_TOO_SMALL)
		(void) rsaprov_free_context(ctx);

	return (rv);
}

/* ARGSUSED */
int
rsaprov_verify_update(crypto_ctx_t *ctx, crypto_data_t *data,
    crypto_req_handle_t req)
{
	int rv;
	rsa_ctx_t *ctxp;

	if (ctx == NULL || data == NULL)
		return (CRYPTO_ARGUMENTS_BAD);

	ASSERT(ctx->cc_provider_private != NULL);
	ctxp = ctx->cc_provider_private;

	switch (ctxp->mech_type) {
	case CRYPTO_RSA_PKCS:
	case CRYPTO_RSA_X_509:
		return (CRYPTO_MECHANISM_INVALID);

	case CRYPTO_MD5_RSA_PKCS:
		rv = crypto_digest_data(data, &(ctxp->md5_ctx),
		    NULL, MD5Update, MD5Final, CRYPTO_DO_MD5 |
		    CRYPTO_DO_UPDATE);
		break;

	case CRYPTO_SHA1_RSA_PKCS:
		rv = crypto_digest_data(data, &(ctxp->sha1_ctx),
		    NULL, SHA1Update, SHA1Final, CRYPTO_DO_SHA1 |
		    CRYPTO_DO_UPDATE);
		break;

	case CRYPTO_SHA256_RSA_PKCS:
	case CRYPTO_SHA384_RSA_PKCS:
	case CRYPTO_SHA512_RSA_PKCS:
		rv = crypto_digest_data(data, &(ctxp->sha2_ctx),
		    NULL, SHA2Update, SHA2Final, CRYPTO_DO_SHA2 |
		    CRYPTO_DO_UPDATE);
		break;

	default:
		return (CRYPTO_MECH_NOT_SUPPORTED);
	}

	return (rv);
}

/* ARGSUSED2 */
int
rsaprov_verify_final(crypto_ctx_t *ctx, crypto_data_t *signature,
    crypto_req_handle_t req)
{
	int rv;
	rsa_ctx_t *ctxp;
	rsa_mech_type_t mech_type;

	if (ctx == NULL || signature == NULL)
		return (CRYPTO_ARGUMENTS_BAD);

	ASSERT(ctx->cc_provider_private != NULL);
	ctxp = ctx->cc_provider_private;
	mech_type = ctxp->mech_type;

	if (mech_type == CRYPTO_RSA_PKCS ||
	    mech_type == CRYPTO_RSA_X_509)
		return (CRYPTO_MECHANISM_INVALID);

	rv = rsaprov_digest_svrfy_common(ctxp, NULL, signature,
	    CRYPTO_DO_VERIFY | CRYPTO_DO_FINAL);
	if (rv != CRYPTO_BUFFER_TOO_SMALL)
		(void) rsaprov_free_context(ctx);

	return (rv);
}

/* ARGSUSED */
int
rsaprov_verify_atomic(crypto_provider_handle_t provider,
    crypto_session_id_t session_id,
    crypto_mechanism_t *mechanism, crypto_key_t *key, crypto_data_t *data,
    crypto_data_t *signature, crypto_spi_ctx_template_t ctx_template,
    crypto_req_handle_t req)
{
	int rv;
	rsa_ctx_t ctx;

	if (data == NULL || signature == NULL)
		return (CRYPTO_ARGUMENTS_BAD);

	if ((rv = rsaprov_check_mech_and_key(mechanism, key)) != CRYPTO_SUCCESS)
		return (rv);

	if (RSA_VALID_CIPHER_MECH(mechanism))
		rv = rsaprov_verify_common(mechanism->cm_type, key, data,
		    signature);

	else {
		ctx.mech_type = mechanism->cm_type;
		ctx.key = key;

		switch (mechanism->cm_type) {
		case CRYPTO_MD5_RSA_PKCS:
			MD5Init(&(ctx.md5_ctx));
			break;

		case CRYPTO_SHA1_RSA_PKCS:
			SHA1Init(&(ctx.sha1_ctx));
			break;

		case CRYPTO_SHA256_RSA_PKCS:
			SHA2Init(SHA256, &(ctx.sha2_ctx));
			break;

		case CRYPTO_SHA384_RSA_PKCS:
			SHA2Init(SHA384, &(ctx.sha2_ctx));
			break;

		case CRYPTO_SHA512_RSA_PKCS:
			SHA2Init(SHA512, &(ctx.sha2_ctx));
			break;

		default:
			return (CRYPTO_MECH_NOT_SUPPORTED);
		}

		rv = rsaprov_digest_svrfy_common(&ctx, data, signature,
		    CRYPTO_DO_VERIFY | CRYPTO_DO_UPDATE | CRYPTO_DO_FINAL);
	}

	return (rv);
}

static int
rsaprov_verify_recover_common(rsa_mech_type_t mech_type, crypto_key_t *key,
    crypto_data_t *signature, crypto_data_t *data)
{
	int rv = CRYPTO_FAILED;

/* EXPORT DELETE START */

	size_t data_len;
	uchar_t *sigptr, *modulus;
	ssize_t modulus_len;
	uchar_t plain_data[MAX_RSA_KEYLENGTH_IN_BYTES];
	uchar_t tmp_data[MAX_RSA_KEYLENGTH_IN_BYTES];

	if ((rv = crypto_get_key_attr(key, SUN_CKA_MODULUS, &modulus,
	    &modulus_len)) != CRYPTO_SUCCESS) {
		return (rv);
	}

	if (signature->cd_length != modulus_len)
		return (CRYPTO_SIGNATURE_LEN_RANGE);

	ASSERT(signature->cd_length <= sizeof (tmp_data));
	if ((rv = crypto_get_input_data(signature, &sigptr, tmp_data))
	    != CRYPTO_SUCCESS)
		return (rv);

	rv = core_rsa_encrypt(key, sigptr, modulus_len, plain_data, 1);
	if (rv != CRYPTO_SUCCESS)
		return (rv);

	data_len = modulus_len;

	if (mech_type == CRYPTO_RSA_PKCS) {
		/*
		 * Strip off the encoded padding bytes in front of the
		 * recovered data, then compare the recovered data with
		 * the original data.
		 */
		rv = pkcs1_decode(PKCS1_VERIFY, plain_data, &data_len);
		if (rv != CRYPTO_SUCCESS)
			return (rv);
	}

	if (data->cd_length < data_len) {
		data->cd_length = data_len;
		return (CRYPTO_BUFFER_TOO_SMALL);
	}

	if ((rv = crypto_put_output_data(plain_data + modulus_len - data_len,
	    data, data_len)) != CRYPTO_SUCCESS)
		return (rv);
	data->cd_length = data_len;

/* EXPORT DELETE END */

	return (rv);
}

/* ARGSUSED */
int
rsaprov_verify_recover(crypto_ctx_t *ctx, crypto_data_t *signature,
    crypto_data_t *data, crypto_req_handle_t req)
{
	int rv;
	rsa_ctx_t *ctxp;

	if (ctx == NULL || signature == NULL || data == NULL)
		return (CRYPTO_ARGUMENTS_BAD);

	ASSERT(ctx->cc_provider_private != NULL);
	ctxp = ctx->cc_provider_private;

	/* See the comments on KM_SLEEP flag in rsaprov_encrypt() */
	rv = rsaprov_verify_recover_common(ctxp->mech_type, ctxp->key,
	    signature, data);

	if (rv != CRYPTO_BUFFER_TOO_SMALL)
		(void) rsaprov_free_context(ctx);

	return (rv);
}

/* ARGSUSED */
int
rsaprov_verify_recover_atomic(crypto_provider_handle_t provider,
    crypto_session_id_t session_id, crypto_mechanism_t *mechanism,
    crypto_key_t *key, crypto_data_t *signature, crypto_data_t *data,
    crypto_spi_ctx_template_t ctx_template, crypto_req_handle_t req)
{
	int rv;

	if (signature == NULL || data == NULL)
		return (CRYPTO_ARGUMENTS_BAD);

	if ((rv = rsaprov_check_mech_and_key(mechanism, key)) != CRYPTO_SUCCESS)
		return (rv);

	return (rsaprov_verify_recover_common(mechanism->cm_type, key,
	    signature, data));
}
