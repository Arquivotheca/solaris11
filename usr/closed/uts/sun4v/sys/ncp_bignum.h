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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_NCP_BIGNUM_H
#define	_NCP_BIGNUM_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"


#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <bignum.h>

typedef struct {
	int64_t	size;		/* key size in bits */
	BIGNUM	p;		/* p */
	BIGNUM	q;		/* q */
	BIGNUM	n;		/* n = p * q (the modulus) */
	BIGNUM	d;		/* private exponent */
	BIGNUM	e;		/* public exponent */
	BIGNUM	dmodpminus1;	/* d mod (p - 1) */
	BIGNUM	dmodqminus1;	/* d mod (q - 1) */
	BIGNUM	pinvmodq;	/* p^(-1) mod q */
	BIGNUM	p_rr;		/* 2^(2*(32*p->len)) mod p */
	BIGNUM	q_rr;		/* 2^(2*(32*q->len)) mod q */
	BIGNUM	n_rr;		/* 2^(2*(32*n->len)) mod n */
} RSAkey;

typedef struct {
	int64_t	size;		/* key size in bits */
	BIGNUM	q;		/* q (160-bit prime) */
	BIGNUM	p;		/* p (<size-bit> prime) */
	BIGNUM	g;		/* g (the base) */
	BIGNUM	x;		/* private key (< q) */
	BIGNUM	y;		/* = g^x mod p */
	BIGNUM	k;		/* k (random number < q) */
	BIGNUM	r;		/* r (signiture 1st part) */
	BIGNUM	s;		/* s (signiture 1st part) */
	BIGNUM	v;		/* v (verification value - should be = r ) */
	BIGNUM	p_rr;		/* 2^(2*(32*p->len)) mod p */
	BIGNUM	q_rr;		/* 2^(2*(32*q->len)) mod q */
} DSAkey;



#define	arraysize(x) (sizeof (x) / sizeof (x[0]))

#define	ncp_bignum2kcl		bignum2bytestring
#define	ncp_kcl2bignum		bytestring2bignum
#define	ncp_big_init		big_init
#define	ncp_big_extend		big_extend
#define	ncp_big_finish		big_finish
#define	ncp_big_is_zero 	big_is_zero
#define	ncp_big_mont_rr		big_mont_rr
#define	ncp_big_n0		big_n0
#define	ncp_big_modexp		big_modexp_ext
#define	ncp_big_modexp_crt	big_modexp_crt_ext
#define	ncp_big_cmp_abs		big_cmp_abs
#define	ncp_big_div_pos		big_div_pos
#define	ncp_big_ext_gcd_pos	big_ext_gcd_pos
#define	ncp_big_add		big_add
#define	ncp_big_mul		big_mul
#define	ncp_big_nextprime_pos	big_nextprime_pos_ext
#define	ncp_big_sub_pos		big_sub_pos
#define	ncp_big_copy		big_copy
#define	ncp_big_sub		big_sub
#define	ncp_big_bitlength	big_bitlength

BIG_ERR_CODE ncp_DSA_key_init(DSAkey *key, int size);
void ncp_DSA_key_finish(DSAkey *key);
BIG_ERR_CODE ncp_RSA_key_init(RSAkey *key, int psize, int qsize);
void ncp_RSA_key_finish(RSAkey *key);

int ncp_big_equals_one(BIGNUM *aa);
BIG_ERR_CODE ncp_kcl_to_bignum(BIGNUM *bn,
    uint8_t *kn, int knlen, int check, int mont,
    int ispoly, BIGNUM *nn, int nndegree, BIG_CHUNK_TYPE nprime, BIGNUM *R);
BIG_ERR_CODE ncp_bignum_to_kcl(uint8_t *kn, int *knlength,
    BIGNUM *bn, int shorten, int mont, int ispoly,
    BIGNUM *nn, BIG_CHUNK_TYPE nprime, BIGNUM *Rinv);
BIG_ERR_CODE ncp_big_set_int(BIGNUM *tgt, BIG_CHUNK_TYPE_SIGNED value);
BIG_ERR_CODE ncp_big_shiftright(BIGNUM *result, BIGNUM *aa, int offs);
BIG_ERR_CODE ncp_big_modexp_ncp(BIGNUM *result, BIGNUM *ma,
    BIGNUM *e, BIGNUM *n, BIGNUM *tmp, BIG_CHUNK_TYPE n0,
    void *ncp, void *reqp);
BIG_ERR_CODE ncp_randombignum(BIGNUM *r, int lengthinbits);
BIG_ERR_CODE ncp_big_mul_extend(BIGNUM *result, BIGNUM *aa, BIGNUM *bb);
int ncp_big_MSB(BIGNUM *X);
int ncp_big_extract_bit(BIGNUM *aa, int k);
BIG_ERR_CODE ncp_big_mod_add(BIGNUM *result,
    BIGNUM *aa, BIGNUM *bb, BIGNUM *nn);
BIG_ERR_CODE ncp_big_mod_sub(BIGNUM *result,
    BIGNUM *aa, BIGNUM *bb, BIGNUM *nn);
int ncp_big_poly_bit_k(BIGNUM *target, int k, BIGNUM *nn, unsigned int minlen);
BIG_ERR_CODE ncp_big_mont_encode(BIGNUM *result, BIGNUM *input,
    int ispoly, BIGNUM *nn, BIG_CHUNK_TYPE nprime, BIGNUM *R);
BIG_ERR_CODE ncp_big_mont_decode(BIGNUM *result, BIGNUM *input,
    int ispoly, BIGNUM *nn, BIG_CHUNK_TYPE nprime, BIGNUM *Rinv);
BIG_ERR_CODE ncp_big_reduce(BIGNUM *target, BIGNUM *modulus, int ispoly);
BIG_CHUNK_TYPE ncp_big_poly_nprime(BIGNUM *nn, int nndegree);
BIG_ERR_CODE ncp_big_poly_add(BIGNUM *result, BIGNUM *aa, BIGNUM *bb);
BIG_ERR_CODE ncp_big_poly_mont_mul(BIGNUM *result,
    BIGNUM *aa, BIGNUM *bb, BIGNUM *nn, BIG_CHUNK_TYPE nprime);
BIG_ERR_CODE ncp_big_mont_mul_extend(BIGNUM *ret,
    BIGNUM *a, BIGNUM *b, BIGNUM *n, BIG_CHUNK_TYPE n0);
BIG_ERR_CODE ncp_big_inverse(BIGNUM *result,
    BIGNUM *aa, BIGNUM *nn, int poly, int mont,
    BIGNUM *R2, BIG_CHUNK_TYPE nprime);

#ifdef	__cplusplus
}
#endif

#endif	/* _NCP_BIGNUM_H */
