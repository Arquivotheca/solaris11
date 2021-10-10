/*
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 */


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
 * Functions in this file generally return BIG_ERR_CODE values.
 */

#include <sys/types.h>
#include <sys/param.h>
#include "ncp_ellipticcrv.h"

#define	CHECK(expr) if ((rv = (expr)) != BIG_OK) { goto cleanexit; }
#define	VAL_LEN(msbpos) ((msbpos) / BIG_CHUNK_SIZE + 1)
/*
 * Some functions (mostly multiply and divide operations) demand an
 * extra word.  Adding it in the begining is essentially free, saves
 * time, and keeps from trying to mutate immutable bignums.
 * EXTRA_SIZE is the number of extra words to allocate.
 */
#define	EXTRA_SIZE 1
#define	POLY_ALLOC_SIZE(msbpos) (VAL_LEN(msbpos) + 1 + EXTRA_SIZE)
#define	PRIME_ALLOC_SIZE(msbpos) (VAL_LEN(msbpos) + 1 + EXTRA_SIZE)
#define	CRV_POLY_ALLOC_SIZE(crv) POLY_ALLOC_SIZE((crv)->modulusinfo.modulusMSB)
#define	CRV_PRIME_ALLOC_SIZE(crv) \
		PRIME_ALLOC_SIZE((crv)->modulusinfo.modulusMSB)
#define	BIG_POLY_MONT_MUL(result, aa, bb) \
		ncp_big_poly_mont_mul((result), (aa), (bb), \
		&crv->modulusinfo.modulus, crv->modulusinfo.nprime)
#define	BIG_POLY_MONT_SQR(result, aa) BIG_POLY_MONT_MUL((result), (aa), (aa))
#define	BIG_MONT_MUL(result, aa, bb) \
	ncp_big_mont_mul_extend((result), (aa), (bb), \
	&crv->modulusinfo.modulus, crv->modulusinfo.nprime)
#define	BIG_MONT_SQR(result, aa) BIG_MONT_MUL((result), (aa), (aa))
#define	BIG_MOD_ADD(result, aa, bb) \
	ncp_big_mod_add((result), (aa), (bb), &crv->modulusinfo.modulus)
#define	BIG_MOD_SUB(result, aa, bb) \
	ncp_big_mod_sub((result), (aa), (bb), &crv->modulusinfo.modulus)
#define	BIG_POLY_INV(result, aa) ncp_big_inverse((result), (aa),  \
	&crv->modulusinfo.modulus, 1, 0, NULL, 0)
#define	BIG_POLY_MONT_INV(result, aa) ncp_big_inverse((result), \
	(aa), &crv->modulusinfo.modulus, 1, 1, &crv->modulusinfo.R2, \
	crv->modulusinfo.nprime)
#define	BIG_INV(result, aa) ncp_big_inverse((result), (aa),  \
	&crv->modulusinfo.modulus, 0, 0, NULL, 0)
#define	BIG_MONT_INV(result, aa) ncp_big_inverse((result), \
	(aa), &crv->modulusinfo.modulus, 0, 1, &crv->modulusinfo.R2, \
	crv->modulusinfo.nprime)


BIG_ERR_CODE
ECC_point_copy(ECC_point_t *target, ECC_point_t *src)
{
	int		rv = BIG_OK;

	if (src->flags & ECC_X_VALID) {
		CHECK(ncp_big_copy(&target->x, &src->x));
	}
	if (src->flags & ECC_Y_VALID) {
		CHECK(ncp_big_copy(&target->y, &src->y));
	}
	if (src->flags & ECC_Z_VALID) {
		CHECK(ncp_big_copy(&target->z, &src->z));
	}
	if (src->flags & ECC_AZ4_VALID) {
		CHECK(ncp_big_copy(&target->az4, &src->az4));
	}
	target->flags = src->flags;

cleanexit:

	return (rv);
}


BIG_ERR_CODE
ECC_point_init(ECC_point_t *target, ECC_curve_t *crv)
{
	int		rv;

	target->x.malloced = 0;
	target->y.malloced = 0;
	target->z.malloced = 0;
	target->az4.malloced = 0;
	target->flags = crv->modulusinfo.flags & ECC_POLY;

	CHECK(ncp_big_init(&target->x, CRV_POLY_ALLOC_SIZE(crv)));
	CHECK(ncp_big_init(&target->y, CRV_POLY_ALLOC_SIZE(crv)));
	CHECK(ncp_big_init(&target->z, CRV_POLY_ALLOC_SIZE(crv)));
	CHECK(ncp_big_init(&target->az4, CRV_POLY_ALLOC_SIZE(crv)));

cleanexit:

	if (rv) {
		ECC_point_finish(target);
	}

	return (rv);
}

void
ECC_point_finish(ECC_point_t *target)
{
	ncp_big_finish(&target->x);
	ncp_big_finish(&target->y);
	ncp_big_finish(&target->z);
	ncp_big_finish(&target->az4);
	target->flags = 0;
}


/*
 * Preconditions: flags and modulus are set.  Post conditions:
 * R, Rinv, nprime, and degree are set.
 */
BIG_ERR_CODE
ECC_fluff_modulus(ECC_modulus_info_t *tgt)
{
	int rv = BIG_OK;
	int nwords;

	if (ncp_big_is_zero(&tgt->modulus)) {
		return (BIG_INVALID_ARGS);
	}

	if (!(tgt->flags & ECC_DEGREE_SET)) {
		tgt->modulusMSB = ncp_big_MSB(&tgt->modulus);
		tgt->flags |= ECC_DEGREE_SET;
	}

	nwords = VAL_LEN(tgt->modulusMSB);

	if (!(tgt->flags & ECC_NPRIME_SET)) {
		if (tgt->flags & ECC_POLY) {
			tgt->nprime = ncp_big_poly_nprime(&tgt->modulus,
			    tgt->modulusMSB);
		} else {
			tgt->nprime = ncp_big_n0(tgt->modulus.value[0]);
		}
		tgt->flags |= ECC_NPRIME_SET;
	}

	if (!(tgt->flags & ECC_RINV_SET)) {
		/*
		 * This will work for both poly and prime styles.
		 * Yes, it's cheating a bit.
		 */
		CHECK(ncp_big_init(&tgt->Rinv, nwords + EXTRA_SIZE));
		CHECK(ncp_big_poly_bit_k(&tgt->Rinv,
		    0, &tgt->modulus, tgt->modulusMSB));
		tgt->flags |= ECC_RINV_SET;
	}

	if (!(tgt->flags & ECC_ONE_SET)) {
		CHECK(ncp_big_init(&tgt->One, nwords + EXTRA_SIZE));
		if (tgt->flags & ECC_POLY) {
			CHECK(ncp_big_poly_bit_k(&tgt->One,
			    BIG_CHUNK_SIZE * nwords,
			    &tgt->modulus, tgt->modulusMSB));
		} else {
			/*
			 * cheat---
			 * use ncp_big_poly_bit_k for a numerical value
			 */
			CHECK(ncp_big_poly_bit_k(&tgt->One,
			    BIG_CHUNK_SIZE * nwords, NULL,
			    tgt->modulusMSB));
			CHECK(ncp_big_div_pos(NULL, &tgt->One, &tgt->One,
			    &tgt->modulus));
		}
		tgt->flags |= ECC_ONE_SET;
	}

	if (!(tgt->flags & ECC_R_SET)) {
		CHECK(ncp_big_init(&tgt->R, nwords + EXTRA_SIZE));
		if (tgt->flags & ECC_POLY) {
			CHECK(ncp_big_poly_bit_k(&tgt->R,
			    2 * BIG_CHUNK_SIZE * nwords,
			    &tgt->modulus, tgt->modulusMSB));
		} else {
			CHECK(ncp_big_mont_rr(&tgt->R, &tgt->modulus));
		}
		tgt->flags |= ECC_R_SET;
	}

	if (!(tgt->flags & ECC_RSQ_SET)) {
		CHECK(ncp_big_init(&tgt->R2, nwords + EXTRA_SIZE));
		if (tgt->flags & ECC_POLY) {
			CHECK(ncp_big_poly_mont_mul(&tgt->R2, &tgt->R, &tgt->R,
			    &tgt->modulus, tgt->nprime));
		} else {
			CHECK(ncp_big_mont_mul_extend(&tgt->R2,
			    &tgt->R, &tgt->R,
			    &tgt->modulus, tgt->nprime));
		}
		tgt->flags |= ECC_RSQ_SET;
	}

cleanexit:

	return (rv);
}

/*
 * X and Y must be the affine coordinates.  Point is both affine and
 * projective.
 */
BIG_ERR_CODE
ECC_point_set(ECC_point_t *result, BIGNUM *X, BIGNUM *Y, ECC_curve_t *crv)
{
	int			rv;
	ECC_modulus_info_t	*mp = &crv->modulusinfo;

	/* We should really check for out-of-range points and reject them. */
	CHECK(ncp_big_mont_encode(&result->x, X,
	    crv->modulusinfo.flags & ECC_POLY,
	    &mp->modulus, mp->nprime, &mp->R));
	CHECK(ncp_big_mont_encode(&result->y, Y,
	    crv->modulusinfo.flags & ECC_POLY,
	    &mp->modulus, mp->nprime, &mp->R));
	CHECK(ncp_big_copy(&result->z, &crv->modulusinfo.One));
	result->flags = crv->modulusinfo.flags & ECC_POLY | ECC_X_VALID |
	    ECC_Y_VALID | ECC_Z_VALID | ECC_AFFINE;

cleanexit:

	return (rv);
}

/*
 * X is set to the non-montgomery encoded affine X coordinate of
 * point.  If point is the point at infinity, BIG_DIV_ZERO is
 * returned.  point can be in either affine or projective coordinates.
 */
BIG_ERR_CODE
ECC_point_x(BIGNUM *x, ECC_point_t *point, ECC_curve_t *crv)
{
	int		rv;
	BIGNUM		Zinverse;
	ECC_modulus_info_t *mp = &crv->modulusinfo;

	if (point->flags & ECC_INFINITY) {
		return (BIG_DIV_BY_0);
	}

	if (!(point->flags & ECC_X_VALID)) {
		return (BIG_INVALID_ARGS);
	}

	Zinverse.malloced = 0;

	if (point->flags & ECC_AFFINE) {
		CHECK(ncp_big_mont_decode(x, &point->x,
		    point->flags & ECC_POLY, &mp->modulus,
		    mp->nprime, &mp->Rinv));
	} else if (!(point->flags & ECC_POLY)) {
		/* JACOBIAN */
		CHECK(ncp_big_init(&Zinverse, CRV_PRIME_ALLOC_SIZE(crv)));
		CHECK(BIG_MONT_INV(&Zinverse, &point->z));
		/* Zinverse is now 1/Z, montgomery encoded */
		CHECK(BIG_MONT_MUL(&Zinverse, &Zinverse, &Zinverse));
		/* Zinverse now holds 1/Z^2, montgomery encoded */
		CHECK(ncp_big_mont_decode(&Zinverse, &Zinverse, 0, &mp->modulus,
		    mp->nprime, &mp->Rinv));
		/* Zinverse now holds 1/Z^2, non montgomery encoded */
		CHECK(BIG_MONT_MUL(x, &Zinverse, &point->x));
		/* x now contains point.x/Z^2, non-montgomery encoded */
	} else {
		/* LOPEZ_DAHAB */
		CHECK(ncp_big_init(&Zinverse, CRV_POLY_ALLOC_SIZE(crv)));
		CHECK(BIG_POLY_INV(&Zinverse, &point->z));
		/* Zinverse is now 1/(R Z) */
		CHECK(BIG_POLY_MONT_MUL(&Zinverse, &Zinverse, &mp->R));
		/* Zinverse now holds 1/Z */
		CHECK(BIG_POLY_MONT_MUL(x, &Zinverse, &point->x));
		/* x now holds  point.x/Z */
	}

cleanexit:

	ncp_big_finish(&Zinverse);
	return (rv);
}


/*
 * result and arg may alias.  Post condition is that result represents
 * the same point as arg, and result is affine or the point at
 * infinity.
 */
BIG_ERR_CODE
ECC_point_to_affine(ECC_point_t *result, ECC_point_t *arg, ECC_curve_t *crv)
{
	int		rv;
	BIGNUM		Zinverse;
	BIGNUM		T;
	ECC_modulus_info_t *mp = &crv->modulusinfo;

	if (arg->flags & (ECC_AFFINE | ECC_INFINITY)) {
		if (result != arg) {
			return (ECC_point_copy(result, arg));
		}
		return (BIG_OK);
	}

	Zinverse.malloced = 0;
	T.malloced = 0;
	/* CRV_POLY_ALLOC_SIZE is safe for prime modulus, too. */
	CHECK(ncp_big_init(&Zinverse, CRV_POLY_ALLOC_SIZE(crv)));

	if (arg->flags & ECC_POLY) {
		CHECK(BIG_POLY_MONT_INV(&Zinverse, &arg->z));
		if (arg->flags & ECC_X_VALID) {
			CHECK(BIG_POLY_MONT_MUL(&result->x, &arg->x,
			    &Zinverse));
		}
		if (arg->flags & ECC_Y_VALID) {
			CHECK(BIG_POLY_MONT_SQR(&Zinverse, &Zinverse));
			CHECK(BIG_POLY_MONT_MUL(&result->y, &arg->y,
			    &Zinverse));
		}
		result->flags = arg->flags & ~ECC_Z_VALID | ECC_AFFINE;
	} else {
		/*
		 * Jacobian coordinates.
		 */
		CHECK(ncp_big_init(&T, CRV_PRIME_ALLOC_SIZE(crv)));
		CHECK(BIG_MONT_INV(&Zinverse, &arg->z));
		/* Zinverse is now 1/Z, montgomery encoded */
		CHECK(ncp_big_mont_mul_extend(&T, &Zinverse, &Zinverse,
		    &mp->modulus, mp->nprime));
		/* T is now 1/Z^2, montgomery encoded */
		if (arg->flags & ECC_X_VALID) {
			CHECK(ncp_big_mont_mul_extend(&result->x, &arg->x, &T,
			    &mp->modulus, mp->nprime));
		}
		if (arg->flags & ECC_Y_VALID) {
			CHECK(ncp_big_mont_mul_extend(&T, &T, &Zinverse,
			    &mp->modulus, mp->nprime));
			/* T is now 1/Z^3, montgomery encoded */
			CHECK(ncp_big_mont_mul_extend(&result->y, &arg->y, &T,
			    &mp->modulus, mp->nprime));
		}
		result->flags = arg->flags &
		    ~(ECC_Z_VALID | ECC_AZ4_VALID) | ECC_AFFINE;
	}

cleanexit:

	ncp_big_finish(&Zinverse);
	ncp_big_finish(&T);
	return (rv);
}


BIG_ERR_CODE
set_AZ4(ECC_point_t *target, ECC_curve_t *crv)
{
	int		rv;

	if (target->flags & ECC_AZ4_VALID) {
		return (BIG_OK);
	}
	if (target->flags & ECC_POLY) {
		return (BIG_OK);
	}
	if (target->flags & ECC_AFFINE) {
		CHECK(ncp_big_copy(&target->az4, &crv->a));
	} else {
		if (!(target->flags & ECC_Z_VALID)) {
			return (BIG_INVALID_ARGS);
		}
		CHECK(BIG_MONT_SQR(&target->az4, &target->z));
		CHECK(BIG_MONT_SQR(&target->az4, &target->az4));
		CHECK(BIG_MONT_MUL(&target->az4, &target->az4, &crv->a));
	}
	target->flags |= ECC_AZ4_VALID;

cleanexit:

	return (rv);
}


/*
 * Returns BIG_OK if aa is in the curve, and BIG_TEST_FAILED if not.
 */
BIG_ERR_CODE
ECC_point_in_curve(ECC_point_t *aa, ECC_curve_t *crv)
{
	BIGNUM		T1;
	BIGNUM		T2;
	BIGNUM		T3;
	ECC_point_t	AAaffine;
	int		rv;

	if ((aa->flags ^ crv->modulusinfo.flags) & ECC_POLY) {
		return (BIG_INVALID_ARGS);
	}
	if (aa->flags & ECC_INFINITY) {
		return (BIG_OK);
	}

	T1.malloced = 0;  /* powers of x or y */
	T2.malloced = 0;  /* terms */
	T3.malloced = 0;  /* sum */
	if (!(aa->flags & ECC_AFFINE)) {
		CHECK(ECC_point_init(&AAaffine, crv));
		CHECK(ECC_point_to_affine(&AAaffine, aa, crv));
		aa = &AAaffine;
	}

	if (aa->flags & ECC_POLY) {
		CHECK(ncp_big_init(&T1, CRV_POLY_ALLOC_SIZE(crv)));
		CHECK(ncp_big_init(&T2, CRV_POLY_ALLOC_SIZE(crv)));
		CHECK(ncp_big_init(&T3, CRV_POLY_ALLOC_SIZE(crv)));

		CHECK(BIG_POLY_MONT_SQR(&T1, &aa->x));
		/* T1 = x^2 */
		CHECK(BIG_POLY_MONT_MUL(&T2, &T1, &crv->a));
		/* T2 = a X^2 */
		CHECK(ncp_big_poly_add(&T3, &T2, &crv->b));
		/* T3 = a X^2 + b */
		CHECK(BIG_POLY_MONT_MUL(&T1, &T1, &aa->x));
		/* T1 = x^3 */
		CHECK(ncp_big_poly_add(&T3, &T3, &T1));
		/* T3 = x^3 + a x^2 + b */
		CHECK(ncp_big_poly_add(&T2, &aa->x, &aa->y));
		/* T2 = x + y */
		CHECK(BIG_POLY_MONT_MUL(&T2, &T2, &aa->y));
		/* T2 = y*(x+y) = y^2 + xy */
		if (ncp_big_cmp_abs(&T2, &T3)) {
			rv = BIG_TEST_FAILED;
		} else {
			rv = BIG_OK;
		}
	} else {
		CHECK(ncp_big_init(&T1, CRV_PRIME_ALLOC_SIZE(crv)));
		CHECK(ncp_big_init(&T2, CRV_PRIME_ALLOC_SIZE(crv)));
		CHECK(ncp_big_init(&T3, CRV_PRIME_ALLOC_SIZE(crv)));

		CHECK(BIG_MONT_SQR(&T1, &aa->x));
		/* T1 = x^2 */
		CHECK(BIG_MONT_MUL(&T2, &aa->x, &crv->a));
		/* T2 = a x */
		CHECK(ncp_big_add(&T3, &T2, &crv->b));
		/* T3 = a x + b */
		CHECK(BIG_MONT_MUL(&T1, &T1, &aa->x));
		/* T1 = x^3 */
		CHECK(ncp_big_add(&T3, &T3, &T1));
		/* T3 = x^3 + a x + b */
		CHECK(ncp_big_reduce(&T3, &crv->modulusinfo.modulus,
		    0 /* !poly */));
		CHECK(BIG_MONT_SQR(&T1, &aa->y));
		/* T1 = y^2 */
		if (ncp_big_cmp_abs(&T1, &T3)) {
			rv = BIG_TEST_FAILED;
		} else {
			rv = BIG_OK;
		}
	}

cleanexit:

	ncp_big_finish(&T1);
	ncp_big_finish(&T2);
	ncp_big_finish(&T3);
	if (aa == &AAaffine) {
		ECC_point_finish(&AAaffine);
	}

	return (rv);
}


/*
 * This does exactly what the corresponding N2 MA operation does.  It
 * is assumed that all 3 coordinates of aa and aa's az4 value are
 * valid.  NO checking is done.
 */
static BIG_ERR_CODE
jacobian_point_double(ECC_point_t *result, ECC_point_t *aa, ECC_curve_t *crv)
{
	int		rv;
	BIGNUM		M1;
	BIGNUM		M2;

	M1.malloced = 0;
	M2.malloced = 0;

	CHECK(ncp_big_init(&M1, CRV_PRIME_ALLOC_SIZE(crv)));
	CHECK(ncp_big_init(&M2, CRV_PRIME_ALLOC_SIZE(crv)));

	CHECK(BIG_MONT_SQR(&M1, &aa->y));		/* M1 = Y1^2 */
	CHECK(BIG_MOD_ADD(&M1, &M1, &M1));		/* M1 = 2 * Y1^2 */
	CHECK(BIG_MOD_ADD(&M2, &aa->y, &aa->y));	/* M2 = 2 * Y1 */
	CHECK(BIG_MONT_MUL(&result->z, &M2, &aa->z));	/* Q_Z = 2*Y1*Z1 */
	CHECK(BIG_MONT_SQR(&M2, &M1));			/* M2 = 4 * Y1^4 */
	CHECK(BIG_MOD_ADD(&M2, &M2, &M2));		/* M2 = 8 * Y1^4 */
	CHECK(BIG_MONT_MUL(&result->y, &M1, &aa->x));	/* Qy = 2*X1*Y1^2 */
	CHECK(BIG_MONT_SQR(&result->x, &aa->x));	/* Qx = X1^2 */
	CHECK(BIG_MOD_ADD(&M1, &result->x, &result->x));
							/* M1 = 2 * X1^2 */
	CHECK(BIG_MOD_ADD(&M1, &M1, &result->x));	/* M1 = 3 * X1^2 */
	CHECK(BIG_MOD_ADD(&result->y, &result->y, &result->y));
							/* Qy = S = 4*X1*Y1^2 */
	CHECK(BIG_MOD_ADD(&M1, &M1, &aa->az4));	/* M1=M=3*X1^2 + a*Z1^4 */
	CHECK(BIG_MONT_SQR(&result->x, &M1));		/* Qx = M^2 */
	CHECK(BIG_MOD_SUB(&result->x, &result->x, &result->y));
							/* Qx = M^2 - S */
	CHECK(BIG_MOD_SUB(&result->x, &result->x, &result->y));
							/* Qx = T = M^2 - 2*S */
	CHECK(BIG_MOD_SUB(&result->y, &result->y, &result->x));
							/* Qy = S - T */
	CHECK(BIG_MONT_MUL(&result->y, &M1, &result->y));
							/* Qy = M*(S-T) */
	CHECK(BIG_MOD_SUB(&result->y, &result->y, &M2));
							/* Qy=Y3=M(S-T)-8Y1^4 */
	CHECK(BIG_MOD_ADD(&M2, &M2, &M2));		/* M2 = 16 * Y1^4 */
	CHECK(BIG_MONT_MUL(&result->az4, &M2, &aa->az4));
							/* AZ4=16Y1^4*a*Z1^4 */
	rv = BIG_OK;

cleanexit:

	ncp_big_finish(&M1);
	ncp_big_finish(&M2);

	return (rv);
}

/*
 * From the Niagara2 Programmer's Reference Manual.  aa != infinty, bb
 * != infinity.  If aa == bb all coordinates of result will be zero.
 * If aa == -bb, then the Z component of result will be zero.  (==
 * means the same point, not necesarily the same representation.) The
 * Z coordinate of the result will be zero any time the affine x
 * coordinates of aa and bb are equal.  bb must be in affine
 * coordinates.  This code does exaclty what the N2 hardware does.
 * There is no checking, and flags are not manipulated.  The az4 field
 * of result also gets set.
 */
static BIG_ERR_CODE
jacobian_affine_point_add(ECC_point_t *result, ECC_point_t *aa, ECC_point_t *bb,
    ECC_curve_t *crv)
{

	BIGNUM		M0;
	BIGNUM		M1;
	BIGNUM		M2;
	int		rv;

	M0.malloced = 0;
	M1.malloced = 0;
	M2.malloced = 0;

	CHECK(ncp_big_init(&M0, CRV_PRIME_ALLOC_SIZE(crv)));
	CHECK(ncp_big_init(&M1, CRV_PRIME_ALLOC_SIZE(crv)));
	CHECK(ncp_big_init(&M2, CRV_PRIME_ALLOC_SIZE(crv)));

	CHECK(BIG_MONT_SQR(&M2, &aa->z));		/* M2 = Z1^2 */
	CHECK(BIG_MONT_MUL(&M1, &bb->x, &M2));		/* M1=U2=X2*Z1^2 */
	CHECK(BIG_MONT_MUL(&M2, &M2, &aa->z));		/* M2 = Z1^3 */
	CHECK(BIG_MONT_MUL(&M2, &M2, &bb->y));		/* M2=S2=Y2*Z1^3 */
	CHECK(BIG_MOD_SUB(&M1, &M1, &aa->x));		/* M1=H=U2-X1 */
	CHECK(BIG_MONT_MUL(&result->z, &aa->z, &M1));	/* QZ=Z3=Z1*H */
	CHECK(BIG_MOD_SUB(&M2, &aa->y, &M2));		/* M2= -r =Y1-S2 */
	CHECK(BIG_MONT_SQR(&result->az4, &M1));		/* AZ4=H^2 */
	CHECK(BIG_MONT_MUL(&M1, &M1, &result->az4));	/* M1 = H^3 */
	CHECK(BIG_MONT_MUL(&result->az4, &aa->x, &result->az4));
							/* AZ4=X1*H^2 */
	CHECK(BIG_MONT_MUL(&result->y, &aa->y, &M1));	/* QY = Y1 * H^3 */
	CHECK(BIG_MOD_ADD(&M1, &M1, &result->az4));	/* M1=H^3+X1*H^2 */
	CHECK(BIG_MOD_ADD(&M1, &M1, &result->az4));	/* M1=H^3+2*X1*H^2 */
	CHECK(BIG_MONT_SQR(&result->x, &M2));		/* QX = r^2 */
	CHECK(BIG_MOD_SUB(&result->x, &result->x, &M1));
						/* QX=X3=-H^3-2*X1*H^2+r^2 */
	CHECK(BIG_MOD_SUB(&result->az4, &result->x, &result->az4));
						/* AZ4 = X3 - X1*H^2 */
	CHECK(BIG_MONT_MUL(&M2, &M2, &result->az4));
						/* M2=-r^2*(X3-X1*H^2) */
	CHECK(BIG_MOD_SUB(&result->y, &M2, &result->y));
					/* QY=Y3=-Y1^3+r*(X1*H^2-2*X3) */
	CHECK(BIG_MONT_SQR(&M2, &result->z));
					/* M2=Z3^2 */
	CHECK(BIG_MONT_SQR(&M1, &M2));	/* M1=Z3^4 */
	CHECK(BIG_MONT_MUL(&result->az4, &crv->a, &M1));
					/* AZ4=a * Z3^4 */

	rv = BIG_OK;

cleanexit:

	ncp_big_finish(&M0);
	ncp_big_finish(&M1);
	ncp_big_finish(&M2);

	return (rv);
}


/*
 * From Hankerson, Menezes, and Vanstone, _Guide to Elliptic Curve
 * Cryptography_, Algorithm 3.24.  Called only from ECC_point_double,
 * which has already checked that aa is not the point at infinity.
 */
static BIG_ERR_CODE
LD_point_double(ECC_point_t *result, ECC_point_t *aa, ECC_curve_t *crv)
{
	int		rv;
	BIGNUM		T1;
	BIGNUM		T2;

	T1.malloced = 0;
	T2.malloced = 0;

	CHECK(ncp_big_init(&T1, CRV_POLY_ALLOC_SIZE(crv)));
	CHECK(ncp_big_init(&T2, CRV_POLY_ALLOC_SIZE(crv)));
	CHECK(BIG_POLY_MONT_SQR(&T1, &aa->z));
	CHECK(BIG_POLY_MONT_SQR(&T2, &aa->x));
	CHECK(BIG_POLY_MONT_MUL(&result->z, &T1, &T2));
	CHECK(BIG_POLY_MONT_SQR(&result->x, &T2));
	CHECK(BIG_POLY_MONT_SQR(&T1, &T1));
	CHECK(BIG_POLY_MONT_MUL(&T2, &T1, &crv->b));
	CHECK(ncp_big_poly_add(&result->x, &result->x, &T2));
	CHECK(BIG_POLY_MONT_SQR(&T1, &aa->y));
	if (crv->abinary == 1ULL) {
		CHECK(ncp_big_poly_add(&T1, &T1, &result->z));
	} else if (crv->abinary != 0ULL) {
		/*
		 * Set T1 = T1 + a * Z3.  This branch is used only if
		 * a is not 0 or 1, which is probably never.  (a == 1
		 * in all NIST named curves.)  That's why it is coded
		 * with all the init and finish here where they will
		 * not be executed unless needed..
		 */
		BIGNUM T3;
		T3.malloced = 0;
		CHECK(ncp_big_init(&T3, CRV_POLY_ALLOC_SIZE(crv)));
		rv = BIG_POLY_MONT_MUL(&T3, &crv->a, &result->z);
		if (rv == 0) {
			rv = ncp_big_poly_add(&T1, &T1, &T3);
		}
		ncp_big_finish(&T3);
		CHECK(rv);
	}
	CHECK(ncp_big_poly_add(&T1, &T1, &T2));
	CHECK(BIG_POLY_MONT_MUL(&result->y, &result->x, &T1));
	CHECK(BIG_POLY_MONT_MUL(&T1, &T2, &result->z));
	CHECK(ncp_big_poly_add(&result->y, &result->y, &T1));
	result->flags = ECC_POLY | ECC_X_VALID | ECC_Y_VALID | ECC_Z_VALID;

cleanexit:

	ncp_big_finish(&T1);
	ncp_big_finish(&T2);
	return (rv);
}


/*
 * Elliptic curve operation on points bb and aa in Montgomery encoded
 * Lopez-Dahab coordinates.  In brief: Sets aa_post = 2aa, bb_post =
 * bb + aa.  Preconditions: bb and aa in the curve.  The y coordinates
 * of aa and bb need not be valid.  bb = aa+pp for some pp. pp must be
 * in affine coordinates.  Only the x coordinate of pp need be valid.
 * Postconditions: aa_post = 2*aa, bb_post = pp + aa.  The y
 * coordinates of aa and bb are invalid.  Weaker preconditions that
 * disregard pp and still retain the postcondition aa_post = 2*aa.  aa
 * and bb are in-out, i.e. updated in place.
 *
 * This exactly mimics the Niagara2 hardware.  It is used in the
 * implementation of point multiplication.  It could also be used for
 * point doubling if the y coordinate was not needed.  Note that it is
 * the caller's responsiblity to make sure that the X and Z
 * components are valid.
 */
static BIG_ERR_CODE
PointDblAdd_special(ECC_point_t *aa, ECC_point_t *bb, ECC_point_t *pp,
    ECC_curve_t *crv)
{

	BIGNUM		M1;
	int		rv;

	M1.malloced = 0;

	if ((aa->flags & bb->flags & pp->flags & ECC_POLY) == 0) {
		return (BIG_INVALID_ARGS);
	}

	aa->flags &= ~(ECC_Y_VALID | ECC_AFFINE);
	bb->flags &= ~(ECC_Y_VALID | ECC_AFFINE);

	CHECK(ncp_big_init(&M1, CRV_POLY_ALLOC_SIZE(crv)));

	CHECK(BIG_POLY_MONT_MUL(&M1, &bb->x, &aa->z)); /* M1=H1=Z1*X2 */
	CHECK(BIG_POLY_MONT_MUL(&bb->z, &bb->z, &aa->x)); /* PZ=H2=X1*Z2 */
	CHECK(BIG_POLY_MONT_SQR(&aa->x, &aa->x)); /* QZ=H3=X1^2 */
	CHECK(BIG_POLY_MONT_SQR(&aa->z, &aa->z)); /* QZ=H4=Z1^2 */
	CHECK(BIG_POLY_MONT_MUL(&bb->x, &bb->z, &M1)); /* PX=H6=H1*H2 */
	CHECK(ncp_big_poly_add(&M1, &M1, &bb->z)); /* M1 = H5 = H1+H2 */
	CHECK(BIG_POLY_MONT_MUL(&bb->z, &M1, &M1)); /* PZ=H10=H5^2 */
	CHECK(BIG_POLY_MONT_MUL(&M1, &aa->z, &aa->z)); /* M1=H3=H4^2 */
	CHECK(BIG_POLY_MONT_MUL(&aa->z, &aa->x, &aa->z)); /* QZ=H9=H3*H4 */
	CHECK(BIG_POLY_MONT_MUL(&M1, &crv->b,  &M1)); /* M1=H11=b*H8 */
	CHECK(BIG_POLY_MONT_MUL(&aa->x, &aa->x, &aa->x)); /* QX=H7 + H3^2 */
	CHECK(ncp_big_poly_add(&aa->x, &M1, &aa->x)); /* QX = H13 = H11 + H7 */
	CHECK(BIG_POLY_MONT_MUL(&M1, &bb->z, &pp->x)); /* M1=H12=BPX*H10 */
	CHECK(ncp_big_poly_add(&bb->x, &bb->x, &M1)); /* PX = H14 = H12 + H6 */

cleanexit:
	ncp_big_finish(&M1);

	return (rv);
}


BIG_ERR_CODE
ECC_point_double(ECC_point_t *result, ECC_point_t *aa, ECC_curve_t *crv)
{
	int		rv;

	if (aa->flags & ECC_INFINITY) {
		if (aa != result) {
			return (ECC_point_copy(result, aa));
		}
		return (BIG_OK);
	}

	/* fluff Z component if necessary */
	if (!(aa->flags & ECC_Z_VALID)) {
		if (aa->flags & ECC_AFFINE) {
			CHECK(ncp_big_copy(&aa->z, &crv->modulusinfo.One));
			aa->flags |= ECC_Z_VALID;
		} else {
			return (BIG_INVALID_ARGS);
		}
	}

	if (crv->modulusinfo.flags & ECC_POLY) {
		if (!(crv->abinary == 0 || crv->abinary == 1)) {
			return (BIG_INVALID_ARGS);
		}
		CHECK(LD_point_double(result, aa, crv));
		if (ncp_big_is_zero(&result->z)) {
			result->flags = ECC_POLY | ECC_INFINITY;
		}
	} else {
		if (crv->abinary != -3LL) {  /* FIXME */
			return (BIG_INVALID_ARGS);
		}
		CHECK(set_AZ4(aa, crv));
		CHECK(jacobian_point_double(result, aa, crv));
		result->flags = ECC_X_VALID | ECC_Y_VALID |
		    ECC_Z_VALID | ECC_AZ4_VALID;
		if (ncp_big_is_zero(&result->z)) {
			result->flags = ECC_INFINITY;
		}
	}

cleanexit:

	return (rv);
}


/*
 * Mixed coordinates.  Result not affine.  Based on Hakerson, Menezes,
 * and Vanstone, Algorithm 3.25.  Precondition: aa != infinity and bb
 * != infinity.  result must not alias aa or bb.
 */
static BIG_ERR_CODE
LD_affine_pt_add(ECC_point_t *result, ECC_point_t *aa, ECC_point_t *bb,
    ECC_curve_t *crv)
{
	int		rv;
	BIGNUM		T1;
	BIGNUM		T2;
	BIGNUM		T3;

	T1.malloced = 0;
	T2.malloced = 0;
	T3.malloced = 0;

	CHECK(ncp_big_init(&T1, CRV_POLY_ALLOC_SIZE(crv)));
	CHECK(ncp_big_init(&T2, CRV_POLY_ALLOC_SIZE(crv)));
	CHECK(ncp_big_init(&T3, CRV_POLY_ALLOC_SIZE(crv)));
	CHECK(BIG_POLY_MONT_MUL(&T1, &aa->z, &bb->x));
	CHECK(BIG_POLY_MONT_SQR(&T2, &aa->z));
	CHECK(ncp_big_poly_add(&result->x, &aa->x, &T1));
	CHECK(BIG_POLY_MONT_MUL(&T1, &aa->z, &result->x));
	CHECK(BIG_POLY_MONT_MUL(&T3, &T2, &bb->y));
	CHECK(ncp_big_poly_add(&result->y, &aa->y, &T3));
	if (ncp_big_is_zero(&result->x)) {
		if (ncp_big_is_zero(&result->y)) {
			/* aa and bb are equal, use point double */
			CHECK(ECC_point_double(result, bb, crv));
			goto cleanexit;
		} else {
			/* aa == -bb, sum is infinity */
			result->flags = result->flags & ~(ECC_X_VALID |
			    ECC_Y_VALID | ECC_Z_VALID) | ECC_INFINITY |
			    ECC_AFFINE;
			goto cleanexit;
		}
	}
	CHECK(BIG_POLY_MONT_SQR(&result->z, &T1));
	CHECK(BIG_POLY_MONT_MUL(&T3, &T1, &result->y));
	if (crv->abinary == 1ULL) {
		CHECK(ncp_big_poly_add(&T1, &T1, &T2));
	} else if (crv->abinary != 0ULL) {
		/*
		 * Set T1 = T1 + a * T2.  This branch is used only if
		 * a is not 0 or 1, which is probably never.  That's
		 * why it is coded with all the init and finish here.
		 */
		BIGNUM T4;
		T4.malloced = 0;
		CHECK(ncp_big_init(&T4, CRV_POLY_ALLOC_SIZE(crv)));
		rv = BIG_POLY_MONT_MUL(&T4, &crv->a, &T2);
		if (rv == 0) {
			rv = ncp_big_poly_add(&T1, &T1, &T4);
		}
		ncp_big_finish(&T4);
		CHECK(rv);
	}
	CHECK(BIG_POLY_MONT_SQR(&T2, &result->x));
	CHECK(BIG_POLY_MONT_MUL(&result->x, &T2, &T1));
	CHECK(BIG_POLY_MONT_SQR(&T2, &result->y));
	CHECK(ncp_big_poly_add(&result->x, &result->x, &T2));
	CHECK(ncp_big_poly_add(&result->x, &result->x, &T3));
	CHECK(BIG_POLY_MONT_MUL(&T2, &bb->x, &result->z));
	CHECK(ncp_big_poly_add(&T2, &T2, &result->x));
	CHECK(BIG_POLY_MONT_SQR(&T1, &result->z));
	CHECK(ncp_big_poly_add(&T3, &T3, &result->z));
	CHECK(BIG_POLY_MONT_MUL(&result->y, &T3, &T2));
	CHECK(ncp_big_poly_add(&T2, &bb->x, &bb->y));
	CHECK(BIG_POLY_MONT_MUL(&T3, &T1, &T2));
	CHECK(ncp_big_poly_add(&result->y, &result->y, &T3));
	result->flags = ECC_POLY | ECC_X_VALID | ECC_Y_VALID | ECC_Z_VALID;

cleanexit:

	ncp_big_finish(&T3);
	ncp_big_finish(&T2);
	ncp_big_finish(&T1);
	return (rv);
}


/*
 * result = a + b (point addition).  result and aa are in projective
 * coordinates.  Point bb must be in affine coordinates.  For
 * polynomial style, the curve "a" parameter must be 0 or 1 and the
 * corresponding flag must be set in the curve's flag field. (This
 * condition is satisfied by all NIST named polynomial style curves.)
 */
BIG_ERR_CODE
ECC_point_add(ECC_point_t *result, ECC_point_t *aa, ECC_point_t *bb,
    ECC_curve_t *crv)
{
	int		rv;
	ECC_point_t	ptT;
	ECC_point_t	*resultp;
	int		aliased = (result == aa) || (result == bb);


	if (aa->flags & ECC_INFINITY) {
		if (bb != result) {
			return (ECC_point_copy(result, bb));
		}
		return (BIG_OK);
	}
	if (bb->flags & ECC_INFINITY) {
		if (aa != result) {
			return (ECC_point_copy(result, aa));
		}
		return (BIG_OK);
	}
	if (!(bb->flags & ECC_AFFINE)) {
		return (BIG_INVALID_ARGS);
	}

	/* work around aliasing */
	if (aliased) {
		resultp = &ptT;
		CHECK(ECC_point_init(&ptT, crv));
	} else {
		resultp = result;
	}

	/* fluff Z components if necessary */
	if (!(aa->flags & ECC_Z_VALID)) {
		if (aa->flags & ECC_AFFINE) {
			CHECK(ncp_big_copy(&aa->z, &crv->modulusinfo.One));
			aa->flags |= ECC_Z_VALID;
		} else {
			rv = BIG_INVALID_ARGS;
			goto cleanexit;
		}
	}
	if (!(bb->flags & ECC_Z_VALID)) {
		/* bb must be affine to get here */
		CHECK(ncp_big_copy(&bb->z, &crv->modulusinfo.One));
		bb->flags |= ECC_Z_VALID;
	}

	if (crv->modulusinfo.flags & ECC_POLY) {
		CHECK(LD_affine_pt_add(resultp, aa, bb, crv));
		/* LD_affine_pt_add checks for special cases itself */
	} else {
		if (!(aa->flags & bb->flags & ECC_Y_VALID)) {
			rv = BIG_INVALID_ARGS;
			goto cleanexit;
		}
		CHECK(jacobian_affine_point_add(resultp, aa, bb, crv));
		/* jacobian_affine_point_add does not check for special cases */
		if (ncp_big_is_zero(&resultp->z)) {
			if (ncp_big_is_zero(&resultp->x)) {
				CHECK(ECC_point_double(resultp, aa, crv));
			} else {
				resultp->flags = ECC_INFINITY;
			}
		} else {
			resultp->flags = ECC_X_VALID | ECC_Y_VALID |
			    ECC_Z_VALID | ECC_AZ4_VALID;
		}
	}

	if (aliased) {
		CHECK(ECC_point_copy(result, resultp));
	}

cleanexit:

	if (aliased) {
		ECC_point_finish(&ptT);
	}

	return (rv);
}


/* EXPORT DELETE START */


int
gfp_pm_fill_ma(uint8_t *mabuf, ma_regs_t *ma_regs, void *params)
{
	ncp_pointmul_params_t	*mpars = (ncp_pointmul_params_t *)params;
	BIGNUM		*mpy = mpars->mpy;
	ECC_point_t	*P = mpars->P;
	ECC_curve_t	*crv = mpars->crv;
	/* LINTED */
	uint64_t	*mamem = (uint64_t *)mabuf;
	int		coordlen;	/* in units of uint64_t */
	int		mamemlen;	/* in units of uint64_t */
	uchar_t		es;		/* length of mpy in bytes - 1 */
	uchar_t		offs;
	int		i;
	BIGNUM		*bn;

	bn = &(crv->modulusinfo.modulus);
	coordlen = bn->len;
	es = (uchar_t)(mpy->len);
	mamemlen = 11 * coordlen + es;
	ASSERT(mamemlen <= MA_SIZE);
	for (i = 0; i < mamemlen; i++) {
		mamem[i] = 0;
	}

	bzero(ma_regs, sizeof (ma_regs_t));
	offs = 0;
	ma_regs->mr_ma.bits.address0 = offs;
	bn = &(P->x);
	ASSERT(bn->len <= coordlen);
	for (i = 0; i < bn->len; i++) {
		mamem[offs + i] = bn->value[i];
	}

	offs += coordlen;
	bn = &(P->y);
	ASSERT(bn->len <= coordlen);
	for (i = 0; i < bn->len; i++) {
		mamem[offs + i] = bn->value[i];
	}

	offs += coordlen;
	ma_regs->mr_ma.bits.address1 = offs;
	bn = &(crv->a);
	ASSERT(bn->len <= coordlen);
	for (i = 0; i < bn->len; i++) {
		mamem[offs + i] = bn->value[i];
	}

	offs += coordlen;
	ma_regs->mr_ma.bits.address2 = offs;
	bn = &(crv->modulusinfo.modulus);
	ASSERT(bn->len <= coordlen);
	for (i = 0; i < bn->len; i++) {
		mamem[offs + i] = bn->value[i];
	}

	offs += coordlen;
	ma_regs->mr_ma.bits.address3 = offs;
	bn = &(P->x);
	ASSERT(bn->len <= coordlen);
	for (i = 0; i < bn->len; i++) {
		mamem[offs + i] = bn->value[i];
	}

	offs += coordlen;
	bn = &(P->y);
	ASSERT(bn->len <= coordlen);
	for (i = 0; i < bn->len; i++) {
		mamem[offs + i] = bn->value[i];
	}

	offs += coordlen;
	bn = &(crv->modulusinfo.One);
	ASSERT(bn->len <= coordlen);
	for (i = 0; i < bn->len; i++) {
		mamem[offs + i] = bn->value[i];
	}

	offs += coordlen;
	ma_regs->mr_ma.bits.address4 = offs;
	bn = mpy;
	ASSERT(bn->len <= coordlen);
	for (i = 0; i < bn->len; i++) {
		mamem[offs + i] = bn->value[i];
	}

	offs += es;
	es = es * sizeof (uint64_t) - 1;
	ma_regs->mr_ma.bits.address5 = es;

	ma_regs->mr_ma.bits.address6 = offs;
	bn = &(crv->a);
	ASSERT(bn->len <= coordlen);
	for (i = 0; i < bn->len; i++) {
		mamem[offs + i] = bn->value[i];
	}

	offs += coordlen;
	ma_regs->mr_ma.bits.address7 = offs;

	coordlen--;
	ma_regs->mr_np = crv->modulusinfo.nprime;
	ma_regs->mr_ctl.bits.n2.operation = MA_OP_POINTMULGFP;
	ma_regs->mr_ctl.bits.n2.length = coordlen;

	return (mamemlen * sizeof (uint64_t));
}


int
gfp_pm_getresult_ma(uint8_t *mabuf, ma_regs_t *ma_regs, void *params)
{
	ncp_pointmul_params_t	*mpars = (ncp_pointmul_params_t *)params;
	ECC_point_t		*result = mpars->Q;
	ECC_curve_t		*crv = mpars->crv;
	int			i, offs, length;
	int			rv;
	/* LINTED */
	uint64_t		*mamem = (uint64_t *)mabuf;

	length = crv->modulusinfo.modulus.len;
	CHECK(ncp_big_extend(&(result->x), length));
	CHECK(ncp_big_extend(&(result->y), length));
	CHECK(ncp_big_extend(&(result->z), length));
	CHECK(ncp_big_extend(&(result->az4), length));

	offs = ma_regs->mr_ma.bits.address6;
	for (i = 0; i < length; i++) {
		result->az4.value[i] = mamem[offs + i];
	}
	result->az4.len = length;

	offs = ma_regs->mr_ma.bits.address3;
	mamem = &(mamem[offs]);
	for (i = 0; i < length; i++) {
		result->x.value[i] = mamem[i];
	}
	result->x.len = length;
	mamem += length;
	for (i = 0; i < length; i++) {
		result->y.value[i] = mamem[i];
	}
	result->y.len = length;
	mamem += length;
	for (i = 0; i < length; i++) {
		result->z.value[i] = mamem[i];
	}
	result->z.len = length;
	result->flags = ECC_X_VALID | ECC_Y_VALID | ECC_Z_VALID |
	    ECC_AZ4_VALID;
	if (ncp_big_is_zero(&(result->z))) {
		result->flags = ECC_INFINITY;
	}

cleanexit:

	return (rv);
}


int
gf2m_pda_pm_fill_ma(uint8_t *mabuf, ma_regs_t *ma_regs, void *params)
{
	ncp_pointmul_params_t	*mpars = (ncp_pointmul_params_t *)params;
	ECC_point_t	*Q = mpars->Q;
	BIGNUM		*mpy = mpars->mpy;
	ECC_curve_t	*crv = mpars->crv;
	/* LINTED */
	uint64_t	*mamem = (uint64_t *)mabuf;
	int		coordlen;	/* in units of uint64_t */
	uchar_t		es;		/* length of mpy in bytes - 1 */
	uchar_t		offs;
	int		i, mamemlen;
	BIGNUM		*bn;

	bn = &(crv->modulusinfo.modulus);
	coordlen = bn->len;
	es = (uchar_t)(mpy->len);
	mamemlen = 9 * coordlen + es;
	ASSERT(mamemlen <= MA_SIZE);
	for (i = 0; i < mamemlen; i++) {
		mamem[i] = 0;
	}

	bzero(ma_regs, sizeof (ma_regs_t));
	offs = 0;
	ma_regs->mr_ma.bits.address0 = offs;
	bn = &(crv->modulusinfo.One);
	ASSERT(bn->len <= coordlen);
	for (i = 0; i < bn->len; i++) {
		mamem[offs + i] = bn->value[i];
	}

	offs += 2 * coordlen;
	ma_regs->mr_ma.bits.address1 = offs;
	bn = &(crv->b);
	ASSERT(bn->len <= coordlen);
	for (i = 0; i < bn->len; i++) {
		mamem[offs + i] = bn->value[i];
	}

	offs += coordlen;
	ma_regs->mr_ma.bits.address2 = offs;
	bn = &(crv->modulusinfo.modulus);
	ASSERT(bn->len <= coordlen);
	for (i = 0; i < bn->len; i++) {
		mamem[offs + i] = bn->value[i];
	}

	offs += coordlen;
	ma_regs->mr_ma.bits.address3 = offs;
	bn = &(Q->x);
	ASSERT(bn->len <= coordlen);
	for (i = 0; i < bn->len; i++) {
		mamem[offs + i] = bn->value[i];
	}

	offs += coordlen;
	bn = &(crv->modulusinfo.One);
	ASSERT(bn->len <= coordlen);
	for (i = 0; i < bn->len; i++) {
		mamem[offs + i] = bn->value[i];
	}

	offs += coordlen;
	ma_regs->mr_ma.bits.address4 = offs;
	bn = mpy;
	ASSERT(bn->len <= coordlen);
	for (i = 0; i < bn->len; i++) {
		mamem[offs + i] = bn->value[i];
	}

	offs += es;
	es = es * sizeof (uint64_t) - 1;
	ma_regs->mr_ma.bits.address5 = es;

	ma_regs->mr_ma.bits.address6 = offs;
	bn = &(Q->x);
	ASSERT(bn->len <= coordlen);
	for (i = 0; i < bn->len; i++) {
		mamem[offs + i] = bn->value[i];
	}

	offs += coordlen;
	ma_regs->mr_ma.bits.address7 = offs;

	coordlen--;
	ma_regs->mr_np = crv->modulusinfo.nprime;
	ma_regs->mr_ctl.bits.n2.operation = MA_OP_POINTMULGF2M;
	ma_regs->mr_ctl.bits.n2.length = coordlen;

	return (mamemlen * sizeof (uint64_t));
}


int
gf2m_pm_getresult_ma(uint8_t *mabuf, ma_regs_t *ma_regs, void *params)
{
	ncp_pointmul_params_t	*mpars = (ncp_pointmul_params_t *)params;
	ECC_point_t		*P = mpars->P;
	ECC_point_t		*Q = mpars->Q;
	ECC_curve_t		*crv = mpars->crv;
	int			i, length;
	int			rv;
	/* LINTED */
	uint64_t		*mamem = (uint64_t *)mabuf;

	length = crv->modulusinfo.modulus.len;
	CHECK(ncp_big_extend(&(Q->x), length));
	CHECK(ncp_big_extend(&(Q->z), length));
	CHECK(ncp_big_extend(&(P->x), length));
	CHECK(ncp_big_extend(&(P->z), length));

	for (i = 0; i < length; i++) {
		Q->x.value[i] = mamem[i];
	}
	Q->x.len = length;
	for (i = 0; i < length; i++) {
		Q->z.value[i] = mamem[i + length];
	}
	Q->z.len = length;
	Q->flags = ECC_POLY | ECC_X_VALID | ECC_Z_VALID;

	mamem = mamem + ma_regs->mr_ma.bits.address3;
	for (i = 0; i < length; i++) {
		P->x.value[i] = mamem[i];
	}
	P->x.len = length;
	for (i = 0; i < length; i++) {
		P->z.value[i] = mamem[i + length];
	}
	P->z.len = length;
	P->flags = ECC_POLY | ECC_X_VALID | ECC_Z_VALID;
	if (ncp_big_is_zero(&(P->z))) {
		P->flags = ECC_INFINITY;
	}

cleanexit:

	return (rv);
}

/* EXPORT DELETE END */



/*
 * result = mpy * aa, where mpy is a simple number (not montgomery
 * encoded) and aa is a point.  The cases mpy == 0, mpy == 1 and aa is
 * the point at infinity are all handled.  Result can alias aa.  If aa
 * is a polynomial, the y component in result is set only if need_y is
 * non-zero.  (Asking for y consumes cycles.)
 */
/* ARGSUSED */
BIG_ERR_CODE
ECC_point_multiply(ECC_point_t *result, ECC_point_t *aa, BIGNUM *mpy,
    ECC_curve_t *crv, int need_y, ncp_t *ncp, ncp_request_t *reqp)
{
	int		rv;
	ECC_point_t	P;
	ECC_point_t	Q;
	int		k;  /* current bit */

/* EXPORT DELETE START */

	CHECK(ECC_point_init(&P, crv));
	CHECK(ECC_point_init(&Q, crv));

	/* 0 * zz = infinity */
	if (ncp_big_is_zero(mpy)) {
		result->flags = aa->flags & ~(ECC_X_VALID | ECC_Y_VALID |
		    ECC_Z_VALID) | ECC_INFINITY;
		rv = BIG_OK;
		goto cleanexit;
	}

	/*
	 * k * infinity = infinity and 1 * aa == aa
	 *
	 * Note: mpy is not Montgomery encoded.
	 */
	if (aa->flags & ECC_INFINITY || ncp_big_equals_one(mpy)) {
		if (aa != result) {
			CHECK(ECC_point_copy(result, aa));
		}
		rv = BIG_OK;
		goto cleanexit;
	}

	if (!(aa->flags & ECC_X_VALID)) {
		rv = BIG_INVALID_ARGS;
		goto cleanexit;
	}
	if (!(aa->flags & ECC_Z_VALID)) {
		if (aa->flags & ECC_AFFINE) {
			/* aa is not infinity */
			CHECK(ncp_big_copy(&aa->z, &crv->modulusinfo.One));
			aa->flags |= ECC_Z_VALID;
		} else {
			rv = BIG_INVALID_ARGS;
			goto cleanexit;
		}
	}

	if (aa->flags & ECC_POLY) {
		/*
		 * Based on Niagara2 Programmers Reference Manual.
		 *
		 * Set up Q = aa and P = 2*aa.  We do that by first
		 * setting up P = aa and then doubling it with
		 * PointDblAdd_special.  For that op, we
		 * don't care about the second and third parameters,
		 * so we use aa there too, except we can't use aa
		 * direclty for the second one, since
		 * PointDblAdd_special mutates its first and
		 * second args.  So we copy it into Q and pass that.
		 */
		if ((ncp != NULL) && (ncp->n_binding != NCP_CPU_N1)) {
			ncp_pointmul_params_t	gf2m_pmpars;

			gf2m_pmpars.Q = aa;
			gf2m_pmpars.mpy = mpy;
			gf2m_pmpars.crv = crv;

			CHECK(ECC_point_copy(&Q, aa));
			gf2m_pmpars.Q = &Q;
			gf2m_pmpars.P = &P;
			CHECK(ncp_ma_activate1(gf2m_pda_pm_fill_ma,
			    gf2m_pm_getresult_ma, (void *)(&gf2m_pmpars),
			    ncp, reqp));
		} else {
			CHECK(ECC_point_copy(&Q, aa));
			CHECK(ECC_point_copy(&P, aa));
			CHECK(PointDblAdd_special(&P, &Q, aa, crv));
			CHECK(ECC_point_copy(&Q, aa));

			/* mpy loop */
			for (k = ncp_big_MSB(mpy) - 1; k >= 0; --k) {
				if (ncp_big_extract_bit(mpy, k)) {
					CHECK(PointDblAdd_special(&P, &Q,
					    aa, crv));
				} else {
					CHECK(PointDblAdd_special(&Q, &P,
					    aa, crv));
				}
			}
		}

		/* check for infinity */
		if (ncp_big_is_zero(&Q.z)) {
			result->flags = ECC_POLY | ECC_INFINITY
			    | ECC_X_VALID | ECC_Z_VALID;
			goto cleanexit;
		}

		if (!need_y) {
			/* fast exit when y is not needed */
			CHECK(ECC_point_copy(result, &Q));
			result->flags = ECC_POLY | ECC_X_VALID | ECC_Z_VALID;
			goto cleanexit;
		}

		/*
		 * The y recovery is based on the Lopez-Dahab paper
		 * "Fast Multiplication on elliptic curves over
		 * GF(2^m) without precomputation".  It says the
		 * check Z2 == 0 is necessary.  If P is infinity it returns
		 * -aa.  Clearly that is correct, but it is not clear
		 * why it is needed.  Perhaps the point double and add
		 * breaks when P := P+Q gets to infinity.
		 */

/* Set up a tranformation to the symbols used by the L-D paper. */
#define	T1 &aa->x
#define	T2 &aa->y
#define	X1 &Q.x
#define	Z1 &Q.z
#define	X2 &P.x
#define	Z2 &P.z
/* scratch, not input */
#define	T3 &P.y
#define	T4 &Q.y
		if (ncp_big_is_zero(Z2)) {
			CHECK(ECC_point_copy(result, aa));
			CHECK(ncp_big_poly_add(&result->y, &aa->x, &aa->y));
			goto cleanexit;
		}
		CHECK(BIG_POLY_MONT_MUL(T3, Z1, Z2));
		CHECK(BIG_POLY_MONT_MUL(Z1, Z1, T1));
		CHECK(ncp_big_poly_add(Z1, Z1, X1));
		CHECK(BIG_POLY_MONT_MUL(Z2, Z2, T1));
		CHECK(BIG_POLY_MONT_MUL(X1, Z2, X1));
		CHECK(ncp_big_poly_add(Z2, Z2, X2));
		CHECK(BIG_POLY_MONT_MUL(Z2, Z2, Z1));
		CHECK(BIG_POLY_MONT_SQR(T4, T1));
		CHECK(ncp_big_poly_add(T4, T4, T2));
		CHECK(BIG_POLY_MONT_MUL(T4, T4, T3));
		CHECK(ncp_big_poly_add(T4, T4, Z2));
		CHECK(BIG_POLY_MONT_MUL(T3, T3, T1));
		CHECK(BIG_POLY_MONT_INV(T3, T3));
		CHECK(BIG_POLY_MONT_MUL(T4, T3, T4));
		CHECK(BIG_POLY_MONT_MUL(X2, X1, T3));
		CHECK(ncp_big_poly_add(Z2, X2, T1));
		CHECK(BIG_POLY_MONT_MUL(Z2, Z2, T4));
		CHECK(ncp_big_poly_add(Z2, Z2, T2));
		/* easier than fighting alias problems */
		CHECK(ncp_big_copy(&result->x, X2));
		CHECK(ncp_big_copy(&result->y, Z2));
		/* end of L-D Mxy algorithm */
		result->flags = ECC_POLY | ECC_X_VALID | ECC_Y_VALID |
		    ECC_AFFINE;
#undef T1
#undef T2
#undef X1
#undef Z1
#undef X2
#undef Z2
#undef T3
#undef T4
	} else {
		if ((ncp != NULL) && (ncp->n_binding != NCP_CPU_N1)) {
			ncp_pointmul_params_t	gfp_pmpars;

			gfp_pmpars.P = aa;
			gfp_pmpars.mpy = mpy;
			gfp_pmpars.Q = result;
			gfp_pmpars.crv = crv;
			CHECK(ncp_ma_activate1(gfp_pm_fill_ma,
			    gfp_pm_getresult_ma, (void *)(&gfp_pmpars),
			    ncp, reqp));
		} else {
			CHECK(ECC_point_copy(&Q, aa));
			/* mpy loop */
			for (k = ncp_big_MSB(mpy) - 1; k >= 0; --k) {
				CHECK(ECC_point_double(&Q, &Q, crv));
				if (ncp_big_extract_bit(mpy, k)) {
					CHECK(ECC_point_add(&Q, &Q, aa, crv));
				}
			}
			CHECK(ECC_point_copy(result, &Q));
		}
	}

cleanexit:

	ECC_point_finish(&P);
	ECC_point_finish(&Q);

/* EXPORT DELETE END */

	return (rv);
}
