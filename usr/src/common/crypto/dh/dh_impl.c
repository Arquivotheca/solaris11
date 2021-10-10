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
 * This file contains DH helper routines common to
 * the PKCS11 soft token code and the kernel DH code.
 */

#include <sys/types.h>
#include <sys/sysmacros.h>
#include <bignum.h>

#ifdef _KERNEL
#include <sys/param.h>
#else
#include <strings.h>
#include <cryptoutil.h>
#endif

#include <sys/crypto/common.h>
#include <des/des_impl.h>
#include "dh_impl.h"


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

/* size is in bits */
static BIG_ERR_CODE
DH_key_init(DHkey *key, int size)
{
	BIG_ERR_CODE err = BIG_OK;
	int len;

	len = BITLEN2BIGNUMLEN(size);
	key->size = size;

	if ((err = big_init(&(key->p), len)) != BIG_OK)
		return (err);
	if ((err = big_init(&(key->g), len)) != BIG_OK)
		goto ret1;
	if ((err = big_init(&(key->x), len)) != BIG_OK)
		goto ret2;
	if ((err = big_init(&(key->y), len)) != BIG_OK)
		goto ret3;

	return (BIG_OK);

ret3:
	big_finish(&(key->x));
ret2:
	big_finish(&(key->g));
ret1:
	big_finish(&(key->p));
	return (err);
}

static void
DH_key_finish(DHkey *key)
{

	big_finish(&(key->y));
	big_finish(&(key->x));
	big_finish(&(key->g));
	big_finish(&(key->p));

}

/*
 * Generate DH key pair x and y, given prime p and base g.
 * Can optionally provided bit length of x, not to exceed bit length of p.
 */
CK_RV
dh_genkey_pair(DHbytekey *bkey)
{
	CK_RV		rv = CKR_OK;
	BIG_ERR_CODE	brv;
	uint32_t	primebit_len;
	DHkey		dhkey;
	int		(*rf)(void *, size_t);
	uint32_t	prime_bytes;
	uint32_t	x_len_bytes;
	uint32_t	y_len_bytes;

	if (bkey == NULL)
		return (CKR_ARGUMENTS_BAD);

	/* Must have prime and base set, value bits can be 0 or non-0 */
	if (bkey->prime_bits == 0 || bkey->prime == NULL ||
	    bkey->base_bytes == 0 || bkey->base == NULL)
		return (CKR_ARGUMENTS_BAD);

	prime_bytes = CRYPTO_BITS2BYTES(bkey->prime_bits);

	if ((prime_bytes < MIN_DH_KEYLENGTH_IN_BYTES) ||
	    (prime_bytes > MAX_DH_KEYLENGTH_IN_BYTES)) {
		return (CKR_KEY_SIZE_RANGE);
	}

	/*
	 * Initialize the DH key.
	 * Note: big_extend takes length in words.
	 */
	if ((brv = DH_key_init(&dhkey, bkey->prime_bits)) != BIG_OK) {
		rv = convert_rv(brv);
		goto ret;
	}

	if ((brv = big_extend(&(dhkey.g),
	    CHARLEN2BIGNUMLEN(bkey->base_bytes))) != BIG_OK) {
		rv = convert_rv(brv);
		goto ret;
	}

	/* Convert prime p and base g to bignum. */
	bytestring2bignum(&(dhkey.p), bkey->prime, prime_bytes);
	bytestring2bignum(&(dhkey.g), bkey->base, bkey->base_bytes);

	/* Base g cannot be greater than prime p. */
	if (big_cmp_abs(&(dhkey.g), &(dhkey.p)) >= 0) {
		rv = CKR_ATTRIBUTE_VALUE_INVALID;
		goto ret;
	}

	/*
	 * The intention of selecting a private-value length is to reduce
	 * the computation time for key agreement, while maintaining a
	 * given level of security.
	 */

	/* Maximum bit length for private-value x is bit length of prime p */
	primebit_len = big_bitlength(&(dhkey.p));

	if (bkey->x_len_bits == 0)
		bkey->x_len_bits = primebit_len;

	if (bkey->x_len_bits > primebit_len) {
		rv = CKR_ATTRIBUTE_VALUE_INVALID;
		goto ret;
	}

	x_len_bytes = CRYPTO_BITS2BYTES(bkey->x_len_bits);
	y_len_bytes = CRYPTO_BITS2BYTES(bkey->y_len_bits);

	/* Generate DH key pair private and public values. */
	if ((brv = big_extend(&(dhkey.x), CHARLEN2BIGNUMLEN(x_len_bytes)))
	    != BIG_OK) {
		rv = convert_rv(brv);
		goto ret;
	}

	/*
	 * The big integer of the private value shall be generated privately
	 * and randomly.
	 */
	rf = bkey->rfunc;
	if (rf == NULL) {
#ifdef _KERNEL
		rf = random_get_pseudo_bytes;
#else
		rf = pkcs11_get_urandom;
#endif
	}

	if ((brv = big_random(&(dhkey.x), bkey->x_len_bits, rf, B_FALSE)) !=
	    BIG_OK) {
		rv = convert_rv(brv);
		goto ret;
	}

	/*
	 * The base g shall be raised to the private value x modulo p to
	 * give an integer y, the integer public value, i.e. y = (g^x) mod p.
	 */
	if ((brv = big_modexp(&(dhkey.y), &(dhkey.g), &(dhkey.x),
	    &(dhkey.p), NULL)) != BIG_OK) {
		rv = convert_rv(brv);
		goto ret;
	}

	bignum2bytestring(bkey->private_x, &(dhkey.x), x_len_bytes);
	bignum2bytestring(bkey->public_y, &(dhkey.y), y_len_bytes);

ret:
	DH_key_finish(&dhkey);

	return (rv);
}

/*
 * DH key derive operation
 */
CK_RV
dh_key_derive(DHbytekey *bkey, uint32_t key_type,	/* = CKK_KEY_TYPE */
    uchar_t *secretkey, uint32_t *secretkey_len)	/* derived secret */
{
	CK_RV		rv = CKR_OK;
	BIG_ERR_CODE	brv;
	DHkey		dhkey;
	uchar_t		*s = NULL;
	size_t		s_allocbytes = 0;
	uint32_t	s_bytes = 0;
	uint32_t	prime_bytes;
	uint32_t	x_len_bytes;
	uint32_t	y_len_bytes;

	if (bkey == NULL)
		return (CKR_ARGUMENTS_BAD);

	/* Must have prime, private value and public value */
	if (bkey->prime_bits == 0 || bkey->prime == NULL ||
	    bkey->x_len_bits == 0 || bkey->private_x == NULL ||
	    bkey->y_len_bits == 0 || bkey->public_y == NULL)
		return (CKR_ARGUMENTS_BAD);

	if (secretkey == NULL) {
		return (CKR_ARGUMENTS_BAD);
	}

	prime_bytes = CRYPTO_BITS2BYTES(bkey->prime_bits);
	x_len_bytes = CRYPTO_BITS2BYTES(bkey->x_len_bits);
	y_len_bytes = CRYPTO_BITS2BYTES(bkey->y_len_bits);

	/*
	 * Initialize the DH key.
	 * Note: big_extend takes length in words.
	 */
	if ((brv = DH_key_init(&dhkey, bkey->prime_bits)) != BIG_OK) {
		rv = convert_rv(brv);
		goto ret;
	}

	if ((brv = big_extend(&(dhkey.x), CHARLEN2BIGNUMLEN(x_len_bytes))) !=
	    BIG_OK) {
		rv = convert_rv(brv);
		goto ret;
	}

	if ((brv = big_extend(&(dhkey.y), CHARLEN2BIGNUMLEN(y_len_bytes))) !=
	    BIG_OK) {
		rv = convert_rv(brv);
		goto ret;
	}

	/* Convert prime p, private-value x, public-value y to bignum */
	bytestring2bignum(&(dhkey.p), bkey->prime, prime_bytes);
	bytestring2bignum(&(dhkey.x), bkey->private_x, x_len_bytes);
	bytestring2bignum(&(dhkey.y), bkey->public_y, y_len_bytes);

	/*
	 * Recycle base g as a temporary variable to compute the derived
	 * secret value which is "g" = (y^x) mod p.  (Not recomputing g.)
	 */
	if ((brv = big_modexp(&(dhkey.g), &(dhkey.y), &(dhkey.x),
	    &(dhkey.p), NULL)) != BIG_OK) {
		rv = convert_rv(brv);
		goto ret;
	}

	s_allocbytes = P2ROUNDUP_TYPED(prime_bytes, sizeof (BIG_CHUNK_SIZE),
	    size_t);
	if ((s = CRYPTO_ALLOC(s_allocbytes, KM_SLEEP)) == NULL) {
		s_allocbytes = 0;		/* keep parfait happy */
		rv = CKR_HOST_MEMORY;
		goto ret;
	}
	s_bytes = dhkey.g.len * (int)sizeof (BIG_CHUNK_TYPE);
	bignum2bytestring(s, &(dhkey.g), s_bytes);

	switch (key_type) {

	case CKK_DES:
		*secretkey_len = DES_KEYSIZE;
		break;
	case CKK_DES2:
		*secretkey_len = DES2_KEYSIZE;
		break;
	case CKK_DES3:
		*secretkey_len = DES3_KEYSIZE;
		break;
	case CKK_RC4:
	case CKK_AES:
	case CKK_GENERIC_SECRET:
		/* use provided secret key length, if any */
		break;
	default:
		/* invalid key type */
		rv = CKR_ATTRIBUTE_TYPE_INVALID;
		goto ret;
	}

	if (*secretkey_len == 0) {
		*secretkey_len = s_bytes;
	}

	if (*secretkey_len > s_bytes) {
		rv = CKR_ATTRIBUTE_VALUE_INVALID;
		goto ret;
	}

	/*
	 * The truncation removes bytes from the leading end of the
	 * secret value.
	 */
	(void) memcpy(secretkey, (s + s_bytes - *secretkey_len),
	    *secretkey_len);

ret:
	if (s != NULL)
		CRYPTO_FREE(s, s_allocbytes);

	DH_key_finish(&dhkey);

	return (rv);
}
