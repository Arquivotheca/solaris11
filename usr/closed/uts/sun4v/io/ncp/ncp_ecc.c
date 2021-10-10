/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * Do not delete this notice.  It is here at the request of Sun legal.
 * Sun does not support and never will support any the following:
 *
 * The ECMQV or other MQV related algorithms/mechanisms.
 *
 * Point compression and uncompression (representing a point <x,y>
 * using only the x-coordinate and one extra bit  <x, LSB(y)>).
 *
 * Normal basis representation of polynomials (X^1, x^2, x^4, x^8...).
 *
 * Validation of arbitrary curves.
 *
 * ===
 * If you are a developer, and thinking about implementing any of these,
 * stop.  Don't do it.  If you have any questions, call Sheueling Chang.
 */


/*
 * Functions in this file generally return errno values or the union
 * of errno values (>= 0, 0 for success) and BIG_ERR_CODE values (<=
 * 0, 0 for success).  Top level functions return errno values, using
 * the CONVERTRV macro to convert if necessary.
 */

#include <sys/types.h>
#include <sys/sunddi.h>
#include <sys/stat.h>
#include <sys/errno.h>
#include <sys/random.h>
#include "ncp_ecc.h"

#define	CHECK(expr) if ((rv = (expr)) != BIG_OK) { goto cleanexit; }
#define	CHECKSYS(expr)	{ rv = ((expr) < 0) ? EIO : 0; \
			if (rv) {goto cleanexit; } }


int
bigerrcode_to_crypto_errcode(BIG_ERR_CODE rv)
{
	/* already CRYPTO_... error code */
	if (rv >= 0) {
		return (rv);
	}
	switch (rv) {
	case BIG_OK:
		return (CRYPTO_SUCCESS);
	case BIG_NO_MEM:
		return (CRYPTO_HOST_MEMORY);
	case BIG_INVALID_ARGS:
		return (CRYPTO_ARGUMENTS_BAD);
	case BIG_DIV_BY_0:
		return (CRYPTO_FAILED);
	case BIG_NO_RANDOM:
		return (CRYPTO_NOT_SUPPORTED);  /* doubt this can happen */
	case BIG_TEST_FAILED:
		return (CRYPTO_ARGUMENTS_BAD);
	case BIG_BUFFER_TOO_SMALL:
		return (CRYPTO_BUFFER_TOO_SMALL);
	default:
		return (CRYPTO_FAILED);
	}
}


/*
 * Returns a random value x, such that degree(x) <= maxdegree.  If
 * non-zero is true, the resulting value will have at least one bit
 * set.
 */
static int
ECC_random_bits(BIGNUM *result, int maxdegree, int nonzero)
{
	int		rv;

	if (maxdegree < 0) {
		rv = BIG_INVALID_ARGS;
		goto cleanexit;
	}

	CHECK(ncp_big_extend(result, maxdegree / BIG_CHUNK_SIZE + 1));
	result->len = maxdegree / BIG_CHUNK_SIZE + 1;
	do {
		CHECKSYS(random_get_pseudo_bytes((unsigned char *)
		    &result->value[0],
		    result->len * sizeof (BIG_CHUNK_TYPE)));
		result->value[result->len - 1] &=
		    ~((BIG_CHUNK_TYPE)0) >>
		    (BIG_CHUNK_SIZE - 1 - maxdegree % BIG_CHUNK_SIZE);
	} while (!nonzero || ncp_big_is_zero(result));
	result->sign = 1;

cleanexit:
	return (CONVERTRV);
}

/*
 * Return a value R, such that, 0 <= R < limit, and in addition, if
 * nonzero is true, R must be nonzero.
 */
static int
ECC_random_range(BIGNUM *result, BIGNUM *limit, int nonzero)
{
	int rv;
	int maxdegree = ncp_big_MSB(limit);

	do {
		CHECK(ECC_random_bits(result, maxdegree, nonzero));
	} while (ncp_big_cmp_abs(result, limit) >= 0);

cleanexit:

	return (CONVERTRV);
}


static BIG_ERR_CODE
ECC_key_pair_gen_internal(BIGNUM *d, ECC_point_t *Q, ECC_curve_t *crv,
    int d_given, ncp_t *ncp, ncp_request_t *reqp)
{
	int		rv = 0;

	if (!d_given) {
		CHECK(ECC_random_range(d, &crv->order, 1 /* non-zero */));
	}
	CHECK(ECC_point_multiply(Q, &crv->basepoint, d,
	    crv, 1 /* need y */, ncp, reqp));
#if 1
	CHECK(ECC_point_in_curve(Q, crv));
#endif

cleanexit:
	return (CONVERTRV);
}


/*
 * Converts n to a kcl string, Montgomery decoding it if mont is 1.  If mont
 * is 0, crvp is actually ignored, but must still be supplied.
 */
static BIG_ERR_CODE
ECC_push_bignum(uint8_t *datap, int *len,
    BIGNUM *n, int mont, ECC_curve_t *crvp)
{
	int		rv;

	CHECK(ncp_bignum_to_kcl(datap, len, n, 1 /* shorten */, mont,
	    crvp->modulusinfo.flags & ECC_POLY, &crvp->modulusinfo.modulus,
	    crvp->modulusinfo.nprime, &crvp->modulusinfo.Rinv));

cleanexit:
	return (CONVERTRV);
}


int
ECC_key_pair_gen(uint8_t *dp, int *dlen,
    uint8_t *xp, int *xlen, uint8_t *yp, int *ylen, ECC_curve_t *crvp,
    ncp_t *ncp, ncp_request_t *reqp)
{
	int		rv;
	BIGNUM		d;
	ECC_point_t	Q;

	CHECK(ncp_big_init(&d,
	    crvp->modulusinfo.modulusMSB / BIG_CHUNK_SIZE + 1));
	CHECK(ECC_point_init(&Q, crvp));
	CHECK(ECC_key_pair_gen_internal(&d, &Q, crvp, 0, ncp, reqp));
	CHECK(ECC_point_to_affine(&Q, &Q, crvp));
	CHECK(ECC_push_bignum(dp, dlen, &d, 0 /* not mont */, crvp));
	CHECK(ECC_push_bignum(xp, xlen, &Q.x, 1 /* mont */, crvp));
	CHECK(ECC_push_bignum(yp, ylen, &Q.y, 1 /* mont */, crvp));

cleanexit:

	ncp_big_finish(&d);
	ECC_point_finish(&Q);

	return (CONVERTRV);
}


int
ECC_key_pair_gen_given_d(uint8_t *dp, int dlen,
    uint8_t *xp, int *xlen, uint8_t *yp, int *ylen, ECC_curve_t *crvp,
    ncp_t *ncp, ncp_request_t *reqp)
{

	int		rv;
	BIGNUM		d;
	ECC_point_t	Q;

	CHECK(ncp_big_init(&d,
	    crvp->modulusinfo.modulusMSB / BIG_CHUNK_SIZE + 1));
	CHECK(ECC_point_init(&Q, crvp));
	CHECK(ncp_kcl_to_bignum(&d, dp, dlen, 0, 0, 0, NULL, 0, 0, NULL));
	CHECK(ECC_key_pair_gen_internal(&d, &Q, crvp, 1, ncp, reqp));
	CHECK(ECC_point_to_affine(&Q, &Q, crvp));
	CHECK(ECC_push_bignum(dp, &dlen, &d, 0 /* not mont */, crvp));
	CHECK(ECC_push_bignum(xp, xlen, &Q.x, 1 /* mont */, crvp));
	CHECK(ECC_push_bignum(yp, ylen, &Q.y, 1 /* mont */, crvp));

cleanexit:

	ncp_big_finish(&d);
	ECC_point_finish(&Q);

	return (CONVERTRV);
}

int
ECC_ECDSA_sign(uint8_t *messagehash, int hashlen8,
    uint8_t *d, int dlen, uint8_t *r, int *rlen, uint8_t *s, int *slen,
    ECC_curve_t *crv, ncp_t *ncp, ncp_request_t *reqp)
{
	int		rv;
	BIGNUM		D;  /* private key */
	BIGNUM		E;  /* massage hash */
	BIGNUM		K;
	BIGNUM		RS;
	ECC_point_t	Q;

	D.malloced = 0;
	E.malloced = 0;
	K.malloced = 0;
	RS.malloced = 0;

	CHECK(ncp_big_init(&D,
	    crv->modulusinfo.modulusMSB / BIG_CHUNK_SIZE + 1));
	CHECK(ncp_big_init(&E, hashlen8 / (BIG_CHUNK_SIZE / BITSINBYTE) + 1));
	CHECK(ncp_big_init(&K,
	    crv->modulusinfo.modulusMSB / BIG_CHUNK_SIZE + 1));
	CHECK(ncp_big_init(&RS,
	    2 * (crv->modulusinfo.modulusMSB / BIG_CHUNK_SIZE) + 3));
	CHECK(ECC_point_init(&Q, crv));

	/* convert secret key to BIGNUM D.  check for valid range. */
	CHECK(ncp_kcl_to_bignum(&D, d, dlen, 1, 0,
	    crv->modulusinfo.flags & ECC_POLY, &crv->modulusinfo.modulus,
	    crv->modulusinfo.modulusMSB, 0, NULL));

	/* convert hash to BIGNUM E.  Just an int or poly for now */
	CHECK(ncp_kcl_to_bignum(&E, messagehash, hashlen8, 0, 0, 0,
	    NULL, 0, 0, NULL));
	/*
	 * Truncate to same number of bits as curve order, disarding
	 * bits on the right.  (This is from ANSI X9.62.)
	 */
	if (BITSINBYTE * hashlen8 > crv->orderMSB + 1) {
		CHECK(ncp_big_shiftright(&E,
		    &E, BITSINBYTE * hashlen8 - (crv->orderMSB + 1)));
	}

loopback:
	/*
	 * Generate an emphemral key pair.
	 * Set RS to affine, non-montgomery encoded Qx.
	 */

	CHECK(ECC_key_pair_gen_internal(&K, &Q, crv, 0, ncp, reqp));
	CHECK(ECC_point_x(&RS, &Q, crv));
	/* invert K mod order (numeric) */
	CHECK(ncp_big_inverse(&K, &K, &crv->order,
	    0 /* numeric */, 0 /* !mont */, NULL, 0));

	/* compute r = Qx mod order (numeric) */
	CHECK(ncp_big_reduce(&RS, &crv->order, 0 /* not poly */));

	if (ncp_big_is_zero(&RS)) {
		goto loopback;
	}

	/* push r to caller */
	CHECK(ECC_push_bignum(r, rlen, &RS, 0 /* !mont */, crv));

	/*
	 * compute s = Kinv * (e + d*r) mod order.
	 */
	CHECK(ncp_big_mul_extend(&RS, &RS, &D));
	CHECK(ncp_big_reduce(&RS, &crv->order, 0 /* not poly */));
	CHECK(ncp_big_add(&RS, &RS, &E));
	CHECK(ncp_big_mul_extend(&RS, &RS, &K)); /* K holds Kinv */
	CHECK(ncp_big_reduce(&RS, &crv->order, 0 /* not poly */));
	if (ncp_big_is_zero(&RS)) {
		goto loopback;
	}

	/* push s to caller */
	CHECK(ECC_push_bignum(s, slen, &RS, 0, crv));

cleanexit:

	ncp_big_finish(&D);
	ncp_big_finish(&E);
	ncp_big_finish(&K);
	ncp_big_finish(&RS);
	ECC_point_finish(&Q);

	return (CONVERTRV);
}


int
ECC_ECDSA_sign_given_K(uint8_t *messagehash, int hashlen8,
    uint8_t *d, int dlen, uint8_t *givenK, int givenKlen,
    uint8_t *r, int *rlen, uint8_t *s, int *slen, ECC_curve_t *crv,
    ncp_t *ncp, ncp_request_t *reqp)
{
	int		rv;
	BIGNUM		D;  /* private key */
	BIGNUM		E;  /* massaged hash */
	BIGNUM		K;
	BIGNUM		RS;
	ECC_point_t	Q;

	D.malloced = 0;
	E.malloced = 0;
	K.malloced = 0;
	RS.malloced = 0;

	CHECK(ncp_big_init(&D,
	    crv->modulusinfo.modulusMSB / BIG_CHUNK_SIZE + 1));
	CHECK(ncp_big_init(&E, hashlen8 / (BIG_CHUNK_SIZE / BITSINBYTE) + 1));
	CHECK(ncp_big_init(&K,
	    crv->modulusinfo.modulusMSB / BIG_CHUNK_SIZE + 1));
	CHECK(ncp_big_init(&RS,
	    2 * (crv->modulusinfo.modulusMSB / BIG_CHUNK_SIZE) + 3));
	CHECK(ECC_point_init(&Q, crv));

	/* convert secret key to BIGNUM D.  check for valid range. */
	CHECK(ncp_kcl_to_bignum(&D, d, dlen, 1, 0,
	    crv->modulusinfo.flags & ECC_POLY, &crv->modulusinfo.modulus,
	    crv->modulusinfo.modulusMSB, 0, NULL));

	/* convert hash to BIGNUM E.  Just an int or poly for now */
	CHECK(ncp_kcl_to_bignum(&E, messagehash, hashlen8, 0, 0, 0,
	    NULL, 0, 0, NULL));
	/*
	 * Truncate to same number of bits as curve order, disarding
	 * bits on the right.  (This is from ANSI X9.62.)
	 */
	if (BITSINBYTE * hashlen8 > crv->orderMSB + 1) {
		CHECK(ncp_big_shiftright(&E,
		    &E, BITSINBYTE * hashlen8 - (crv->orderMSB + 1)));
	}

	/* convert given ephemeral key to BIGNUM K */
	CHECK(ncp_kcl_to_bignum(&K, givenK, givenKlen, 0, 0, 0,
	    NULL, 0, 0, NULL));

loopback:
	/*
	 * Generate an emphemral key pair.
	 * Set RS to affine, non-montgomery encoded Qx.
	 */
	CHECK(ECC_key_pair_gen_internal(&K, &Q, crv, 1, ncp, reqp));
	CHECK(ECC_point_x(&RS, &Q, crv));

	/* invert K mod order (numeric) */
	CHECK(ncp_big_inverse(&K, &K, &crv->order,
	    0 /* numeric */, 0 /* !mont */, NULL, 0));

	/* compute r = Qx mod order (numeric) */
	CHECK(ncp_big_reduce(&RS, &crv->order, 0 /* not poly */));

	if (ncp_big_is_zero(&RS)) {
		goto loopback;
	}

	/* push r to caller */
	CHECK(ECC_push_bignum(r, rlen, &RS, 0 /* !mont */, crv));

	/*
	 * compute s = Kinv * (e + d*r) mod order.
	 */
	CHECK(ncp_big_mul_extend(&RS, &RS, &D));
	CHECK(ncp_big_reduce(&RS, &crv->order, 0 /* not poly */));
	CHECK(ncp_big_add(&RS, &RS, &E));
	CHECK(ncp_big_mul_extend(&RS, &RS, &K)); /* K holds Kinv */
	CHECK(ncp_big_reduce(&RS, &crv->order, 0 /* not poly */));

	if (ncp_big_is_zero(&RS)) {
		goto loopback;
	}

	/* push s to caller */
	CHECK(ECC_push_bignum(s, slen, &RS, 0, crv));

cleanexit:

	ncp_big_finish(&D);
	ncp_big_finish(&E);
	ncp_big_finish(&K);
	ncp_big_finish(&RS);
	ECC_point_finish(&Q);

	return (CONVERTRV);
}


int
ECC_ECDSA_verify(int *verified, uint8_t *messagehash, int hashlen8, uint8_t *r,
    int rlen, uint8_t *s, int slen, uint8_t *x, int xlen, uint8_t *y, int ylen,
    ECC_curve_t *crv, ncp_t *ncp, ncp_request_t *reqp)
{
	int		rv = 0;
	BIGNUM		E;  /* massaged hash */
	BIGNUM		R;
	BIGNUM		S;
	BIGNUM		XDBG;
	ECC_point_t	Q;
	ECC_point_t	X;

	E.malloced = 0;
	R.malloced = 0;
	S.malloced = 0;
	XDBG.malloced = 0;

	*verified = 0;

	CHECK(ncp_big_init(&E,
	    2 * (crv->modulusinfo.modulusMSB / BIG_CHUNK_SIZE) + 3));
	CHECK(ncp_big_init(&R,
	    2 * (crv->modulusinfo.modulusMSB / BIG_CHUNK_SIZE) + 3));
	CHECK(ncp_big_init(&S,
	    2 * (crv->modulusinfo.modulusMSB / BIG_CHUNK_SIZE) + 3));
	CHECK(ncp_big_init(&XDBG,
	    2 * (crv->modulusinfo.modulusMSB / BIG_CHUNK_SIZE) + 3));
	CHECK(ECC_point_init(&Q, crv));
	CHECK(ECC_point_init(&X, crv));

	/* convert hash to BIGNUM E.  Just an int or poly for now */
	CHECK(ncp_kcl_to_bignum(&E,
	    messagehash, hashlen8, 0, 0, 0, NULL, 0, 0, NULL));
	/*
	 * Truncate to same number of bits as curve order, disarding
	 * bits on the right.  (This is from ANSI X9.62.)
	 */
	if (BITSINBYTE * hashlen8 > crv->orderMSB + 1) {
		CHECK(ncp_big_shiftright(&E,
		    &E, BITSINBYTE * hashlen8 - (crv->orderMSB + 1)));
	}

	/* convert r to BIGNUM R.  Just an int. */
	CHECK(ncp_kcl_to_bignum(&R, r, rlen, 0, 0, 0, NULL, 0, 0, NULL));
	/* convert s to BIGNUM S.  Just an int. */
	CHECK(ncp_kcl_to_bignum(&S, s, slen, 0, 0, 0, NULL, 0, 0, NULL));

	if (ncp_big_is_zero(&R) || ncp_big_is_zero(&S) ||
	    (ncp_big_cmp_abs(&R, &crv->order) >= 0) ||
	    (ncp_big_cmp_abs(&S, &crv->order) >= 0)) {
		rv = BIG_INVALID_ARGS;
		goto cleanexit;
	}
	/* setup Q */
	CHECK(ncp_kcl_to_bignum(&Q.x, x, xlen, 1, 1,
	    crv->modulusinfo.flags & ECC_POLY,
	    &crv->modulusinfo.modulus, crv->modulusinfo.modulusMSB,
	    crv->modulusinfo.nprime, &crv->modulusinfo.R));
	CHECK(ncp_kcl_to_bignum(&Q.y, y, ylen, 1, 1,
	    crv->modulusinfo.flags & ECC_POLY,
	    &crv->modulusinfo.modulus, crv->modulusinfo.modulusMSB,
	    crv->modulusinfo.nprime, &crv->modulusinfo.R));
	Q.flags = crv->modulusinfo.flags & ECC_POLY |
	    ECC_X_VALID | ECC_Y_VALID | ECC_AFFINE;

	/*
	 * It would seem that at this point we should verify that Q is
	 * in the curve.  But neither Hankerson, Menezes, & Vanstone,
	 * nor ANSI X9.62 call for such verification.
	 */
#if 0
	rv = ECC_point_in_curve(&Q, crv);
	if (rv == BIG_TEST_FAILED) {
		rv = BIG_OK;
		goto cleanexit;
	}
#endif
	/* invert s mod n in place */
	CHECK(ncp_big_inverse(&S, &S,
	    &crv->order, 0 /* numerical */, 0 /* !mont */, NULL, 0));
	/* u1 = e * s^(-1) mod n, store in E */
	CHECK(ncp_big_mul_extend(&E, &E, &S));
	CHECK(ncp_big_reduce(&E, &crv->order, 0 /* numerical */));
	/* u2 = r * s^(-1) mod n */
	CHECK(ncp_big_mul_extend(&S, &R, &S));  /* reuse S for u2 */
	CHECK(ncp_big_reduce(&S, &crv->order, 0 /* numerical */));
			/* X = u1*P + u2 * Q; Overwrite Q. */
	CHECK(ECC_point_multiply(&X, &crv->basepoint, &E,
	    crv, 1 /* need y */, ncp, reqp));
	CHECK(ECC_point_multiply(&Q, &Q, &S, crv, 1, ncp, reqp));
			/* u2 (in S) is now dead */
	CHECK(ECC_point_to_affine(&Q, &Q, crv));
			/* second addend to ECC_point_add must be affine */
	CHECK(ECC_point_add(&X, &X, &Q, crv));
	/* reject if X is infinity */
	if (X.flags & ECC_INFINITY) {
		/* rv = BIG_OK;  already true */
		goto cleanexit;
	}
	/* reject if r != X.x mod order. Now use S to hold X.x. */
	CHECK(ECC_point_x(&S, &X, crv));
	/* S is a coordinate, but treat it as a number */
	CHECK(ncp_big_reduce(&S, &crv->order, 0 /* not poly */));

	if (ncp_big_cmp_abs(&R, &S) == 0) {
		*verified = 1;
	}

cleanexit:

	ncp_big_finish(&E);
	ncp_big_finish(&R);
	ncp_big_finish(&S);
	ncp_big_finish(&XDBG);
	ECC_point_finish(&Q);
	ECC_point_finish(&X);

	return (CONVERTRV);
}


int
ECC_ECDH_derive(uint8_t *result, int *resultlen,
    uint8_t *d, int dlen, uint8_t *x1, int x1len, uint8_t *y1, int y1len,
    ECC_curve_t *crv, ncp_t *ncp, ncp_request_t *reqp)
{
	int		rv;
	BIGNUM		D;
	ECC_point_t	Q;
	ECC_point_t	R;

	D.malloced = 0;

	/* y1len == -1 encodes the point at infinity */
	if (y1len == -1) {
		/*
		 * The point at infinity is invalid, as required by
		 * ANSI X9.62 Section 5.2.2.1 step 1.
		 */
		rv = BIG_TEST_FAILED;
		goto cleanexit;
	}
	CHECK(ncp_big_init(&D,
	    2 * (crv->modulusinfo.modulusMSB / BIG_CHUNK_SIZE) + 3));
	CHECK(ECC_point_init(&Q, crv));
	CHECK(ECC_point_init(&R, crv));

	/*
	 * convert multiplier/exponent to BIGNUM D.
	 * Just an int or poly, but check
	 */

	CHECK(ncp_kcl_to_bignum(&D, d, dlen, 1 /* check */, 0 /* !mont */,
	    crv->modulusinfo.flags & ECC_POLY, &crv->modulusinfo.modulus,
	    crv->modulusinfo.modulusMSB, crv->modulusinfo.nprime, NULL));

	/*
	 * Setup Q.  The validity checking requried by ANSI X9.62
	 * Section 5.2.2.1 step 2 is done in ncp_kcl_to_bignum since
	 * the check parameter is 1 in both calls.
	 */

	CHECK(ncp_kcl_to_bignum(&Q.x, x1, x1len, 1 /* check */, 1 /* mont */,
	    crv->modulusinfo.flags & ECC_POLY, &crv->modulusinfo.modulus,
	    crv->modulusinfo.modulusMSB,
	    crv->modulusinfo.nprime, &crv->modulusinfo.R));

	CHECK(ncp_kcl_to_bignum(&Q.y, y1, y1len, 1, 1,
	    crv->modulusinfo.flags & ECC_POLY, &crv->modulusinfo.modulus,
	    crv->modulusinfo.modulusMSB,
	    crv->modulusinfo.nprime, &crv->modulusinfo.R));

	Q.flags = (crv->modulusinfo.flags & ECC_POLY) |
	    ECC_X_VALID | ECC_Y_VALID | ECC_AFFINE;
	/*
	 * Check that point is in curve; required by ANSI X9.62
	 * Section 5.2.2.1 step 3.  If the condition is not satisfied,
	 * fails with BIG_TEST_FAILED.
	 */

	CHECK(ECC_point_in_curve(&Q, crv));
	/*
	 * check that nQ is infinity, required by ANSI X9.63 Section 5.2.2.1
	 * step 4
	 */

	CHECK(ECC_point_multiply(&R, &Q, &crv->order, crv,
	    0 /* don't need y */, ncp, reqp));
	if (!(R.flags & ECC_INFINITY)) {
		rv = BIG_TEST_FAILED;
		goto cleanexit;
	}
	CHECK(ECC_point_multiply(&R, &Q, &D, crv, 0 /* don't need y */,
	    ncp, reqp));

	/* reject if R is infinity */
	if (R.flags & ECC_INFINITY) {
		rv = BIG_TEST_FAILED;
		goto cleanexit;
	}
	/* recover affine X coordinate.  Put it in D, since it is now dead */
	CHECK(ECC_point_x(&D, &R, crv));
	CHECK(ECC_push_bignum(result, resultlen, &D, 0 /* !mont */, crv));

cleanexit:

	ncp_big_finish(&D);
	ECC_point_finish(&Q);
	ECC_point_finish(&R);

	return (CONVERTRV);
}
