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

#ifndef _BIGNUM_H
#define	_BIGNUM_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/types.h>

#if defined(__sparcv9) || defined(__amd64) || defined(__v8plus)
						/* 64-bit chunk size */
#ifndef UMUL64
#define	UMUL64	/* 64-bit multiplication results are supported */
#endif
#else
#define	BIGNUM_CHUNK_32
#endif


#define	BITSINBYTE	8

/* Bignum "digits" (aka "chunks" or "words") are either 32- or 64-bits */
#ifdef BIGNUM_CHUNK_32
#define	BIG_CHUNK_SIZE		32
#define	BIG_CHUNK_TYPE		uint32_t
#define	BIG_CHUNK_TYPE_SIGNED	int32_t
#define	BIG_CHUNK_HIGHBIT	0x80000000
#define	BIG_CHUNK_ALLBITS	0xffffffff
#define	BIG_CHUNK_LOWHALFBITS	0xffff
#define	BIG_CHUNK_HALF_HIGHBIT	0x8000

#else
#define	BIG_CHUNK_SIZE		64
#define	BIG_CHUNK_TYPE		uint64_t
#define	BIG_CHUNK_TYPE_SIGNED	int64_t
#define	BIG_CHUNK_HIGHBIT	0x8000000000000000ULL
#define	BIG_CHUNK_ALLBITS	0xffffffffffffffffULL
#define	BIG_CHUNK_LOWHALFBITS	0xffffffffULL
#define	BIG_CHUNK_HALF_HIGHBIT	0x80000000ULL
#endif

#define	BITLEN2BIGNUMLEN(x)	((x) > 0 ? \
				((((x) - 1) / BIG_CHUNK_SIZE) + 1) : 0)
#define	CHARLEN2BIGNUMLEN(x)	((x) > 0 ? \
				((((x) - 1) / sizeof (BIG_CHUNK_TYPE)) + 1) : 0)

#define	BIGNUM_WORDSIZE	(BIG_CHUNK_SIZE / BITSINBYTE)  /* word size in bytes */
#define	BIG_CHUNKS_FOR_160BITS	BITLEN2BIGNUMLEN(160)


/*
 * leading 0's are permitted
 * 0 should be represented by size>=1, size>=len>=1, sign=1,
 * value[i]=0 for 0<i<len
 */
typedef struct {
	/* size and len in units of BIG_CHUNK_TYPE words  */
	uint32_t	size;	/* size of memory allocated for value  */
	uint32_t	len;	/* number of valid data words in value */
	int		sign;	/* 1 for nonnegative, -1 for negative  */
	int		malloced; /* 1 if value was malloced, 0 if not */
	BIG_CHUNK_TYPE *value;
} BIGNUM;

#define	BIGTMPSIZE 65

#define	BIG_TRUE 1
#define	BIG_FALSE 0

typedef int BIG_ERR_CODE;

/* error codes */
#define	BIG_OK 0
#define	BIG_NO_MEM -1
#define	BIG_INVALID_ARGS -2
#define	BIG_DIV_BY_0 -3
#define	BIG_NO_RANDOM -4
#define	BIG_GENERAL_ERR	-5
#define	BIG_TEST_FAILED -6
#define	BIG_BUFFER_TOO_SMALL -7

/*
 * this is not an error code, but should be different from possible error codes
 */
#define	RND_TEST_VALUE_SUPPLIED	-8


#define	arraysize(x) (sizeof (x) / sizeof (x[0]))

typedef BIG_ERR_CODE (*big_modexp_ncp_func_ptr)(BIGNUM *result,
    BIGNUM *ma, BIGNUM *e, BIGNUM *n,
    BIGNUM *tmp, BIG_CHUNK_TYPE n0, void *ncp, void *req);

typedef struct {
	big_modexp_ncp_func_ptr	func;
	void			*ncp;
	void 			*reqp;
} big_modexp_ncp_info_t;

#ifdef YF_MODEXP
BIG_ERR_CODE big_modexp_ncp_yf(BIGNUM *result, BIGNUM *ma, BIGNUM *e, BIGNUM *n,
    BIGNUM *tmp, BIG_CHUNK_TYPE n0);
#endif

#ifdef YF_MONTMUL
BIG_ERR_CODE big_mont_mul_yf(BIGNUM *ret,
    BIGNUM *a, BIGNUM *b, BIGNUM *n, BIG_CHUNK_TYPE n0);
#endif

#ifdef YF_MPMUL
BIG_ERR_CODE big_mp_mul_yf(BIGNUM *ret, BIGNUM *a, BIGNUM *b);
void mpmul_arr_yf(uint64_t *res, uint64_t *m1, uint64_t *m2, int len);
#endif

#ifdef USE_FLOATING_POINT
void conv_d16_to_i32(uint32_t *i32, double *d16, int64_t *tmp, int ilen);
void conv_i32_to_d32(double *d32, uint32_t *i32, int len);
void conv_i32_to_d16(double *d16, uint32_t *i32, int len);
void conv_i32_to_d32_and_d16(double *d32, double *d16,
    uint32_t *i32, int len);
void mont_mulf_noconv(uint32_t *result, double *dm1, double *dm2, double *dt,
    double *dn, uint32_t *nint, int nlen, double dn0);
#endif /* USE_FLOATING_POINT */

extern BIGNUM big_One;
extern BIGNUM big_Two;

void printbignum(char *aname, BIGNUM *a);

BIG_ERR_CODE big_init(BIGNUM *number, int size);
BIG_ERR_CODE big_extend(BIGNUM *number, int size);
void big_finish(BIGNUM *number);
void bytestring2bignum(BIGNUM *bn, uchar_t *kn, size_t len);
void bignum2bytestring(uchar_t *kn, BIGNUM *bn, size_t len);
BIG_ERR_CODE big_mont_rr(BIGNUM *result, BIGNUM *n);
BIG_ERR_CODE big_modexp(BIGNUM *result, BIGNUM *a, BIGNUM *e,
    BIGNUM *n, BIGNUM *n_rr);
BIG_ERR_CODE big_modexp_ext(BIGNUM *result, BIGNUM *a, BIGNUM *e,
    BIGNUM *n, BIGNUM *n_rr, big_modexp_ncp_info_t *info);
BIG_ERR_CODE big_modexp_crt(BIGNUM *result, BIGNUM *a, BIGNUM *dmodpminus1,
    BIGNUM *dmodqminus1, BIGNUM *p, BIGNUM *q, BIGNUM *pinvmodq,
    BIGNUM *p_rr, BIGNUM *q_rr);
BIG_ERR_CODE big_modexp_crt_ext(BIGNUM *result, BIGNUM *a, BIGNUM *dmodpminus1,
    BIGNUM *dmodqminus1, BIGNUM *p, BIGNUM *q, BIGNUM *pinvmodq,
    BIGNUM *p_rr, BIGNUM *q_rr, big_modexp_ncp_info_t *info);
int big_cmp_abs(BIGNUM *a, BIGNUM *b);
BIG_ERR_CODE big_random(BIGNUM *r, size_t length,
    int (*rfunc)(void *, size_t), boolean_t precise);
BIG_ERR_CODE big_div_pos(BIGNUM *result, BIGNUM *remainder,
    BIGNUM *aa, BIGNUM *bb);
BIG_ERR_CODE big_ext_gcd_pos(BIGNUM *gcd, BIGNUM *cm, BIGNUM *ce,
    BIGNUM *m, BIGNUM *e);
BIG_ERR_CODE big_add(BIGNUM *result, BIGNUM *aa, BIGNUM *bb);
BIG_ERR_CODE big_add_abs(BIGNUM *result, BIGNUM *aa, BIGNUM *bb);
void big_mul_arr_64(uint64_t *result, uint64_t *a, uint64_t *b, int alen);
BIG_ERR_CODE big_mul(BIGNUM *result, BIGNUM *aa, BIGNUM *bb);
void big_shiftright(BIGNUM *result, BIGNUM *aa, int offs);
BIG_ERR_CODE big_nextprime_pos(BIGNUM *result, BIGNUM *n);
BIG_ERR_CODE big_nextprime_pos_ext(BIGNUM *result, BIGNUM *n,
    big_modexp_ncp_info_t *info);
BIG_ERR_CODE big_sub_pos(BIGNUM *result, BIGNUM *aa, BIGNUM *bb);
BIG_ERR_CODE big_copy(BIGNUM *dest, BIGNUM *src);
BIG_ERR_CODE big_sub(BIGNUM *result, BIGNUM *aa, BIGNUM *bb);
int big_bitlength(BIGNUM *n);
BIG_ERR_CODE big_init1(BIGNUM *number, int size,
    BIG_CHUNK_TYPE *buf, int bufsize);
BIG_ERR_CODE big_mont_mul(BIGNUM *ret,
    BIGNUM *a, BIGNUM *b, BIGNUM *n, BIG_CHUNK_TYPE n0);
int big_is_zero(BIGNUM *n);
BIG_CHUNK_TYPE big_n0(BIG_CHUNK_TYPE n);


/*
 * Kernel bignum module: module integrity test
 */
extern int	bignum_fips_check(void);

#if defined(HWCAP)

#if (BIG_CHUNK_SIZE != 32)
#error HWCAP works only with 32-bit bignum chunks
#endif

#define	BIG_MUL_SET_VEC(r, a, len, digit) \
	(*big_mul_set_vec_impl)(r, a, len, digit)
#define	BIG_MUL_ADD_VEC(r, a, len, digit) \
	(*big_mul_add_vec_impl)(r, a, len, digit)
#define	BIG_MUL_VEC(r, a, alen, b, blen) \
	(*big_mul_vec_impl)(r, a, alen, b, blen)
#define	BIG_SQR_VEC(r, a, len) \
	(*big_sqr_vec_impl)(r, a, len)

extern BIG_CHUNK_TYPE (*big_mul_set_vec_impl)
	(BIG_CHUNK_TYPE *r, BIG_CHUNK_TYPE *a, int len, BIG_CHUNK_TYPE digit);
extern BIG_CHUNK_TYPE (*big_mul_add_vec_impl)
	(BIG_CHUNK_TYPE *r, BIG_CHUNK_TYPE *a, int len, BIG_CHUNK_TYPE digit);
extern void (*big_mul_vec_impl)
	(BIG_CHUNK_TYPE *r, BIG_CHUNK_TYPE *a, int alen, BIG_CHUNK_TYPE *b,
	    int blen);
extern void (*big_sqr_vec_impl)
	(BIG_CHUNK_TYPE *r, BIG_CHUNK_TYPE *a, int len);

#else /* ! HWCAP */

#define	BIG_MUL_SET_VEC(r, a, len, digit) big_mul_set_vec(r, a, len, digit)
#define	BIG_MUL_ADD_VEC(r, a, len, digit) big_mul_add_vec(r, a, len, digit)
#define	BIG_MUL_VEC(r, a, alen, b, blen) big_mul_vec(r, a, alen, b, blen)
#define	BIG_SQR_VEC(r, a, len) big_sqr_vec(r, a, len)

extern BIG_CHUNK_TYPE big_mul_set_vec(BIG_CHUNK_TYPE *r, BIG_CHUNK_TYPE *a,
    int len, BIG_CHUNK_TYPE d);
extern BIG_CHUNK_TYPE big_mul_add_vec(BIG_CHUNK_TYPE *r,
    BIG_CHUNK_TYPE *a, int len, BIG_CHUNK_TYPE d);
extern void big_mul_vec(BIG_CHUNK_TYPE *r, BIG_CHUNK_TYPE *a, int alen,
    BIG_CHUNK_TYPE *b, int blen);
extern void big_sqr_vec(BIG_CHUNK_TYPE *r, BIG_CHUNK_TYPE *a, int len);

#endif /* HWCAP */

#ifdef	__cplusplus
}
#endif

#endif	/* _BIGNUM_H */
