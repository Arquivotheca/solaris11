/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _NCP_ELLIPTICCRV_H
#define	_NCP_ELLIPTICCRV_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/ncp.h>

/* for ECC_modulus_info and EC_point */
#define	ECC_POLY 1
/* for ECC_modulus_info */
#define	ECC_R_SET 2
#define	ECC_ONE_SET 4
#define	ECC_RINV_SET 8
#define	ECC_RSQ_SET 0x10
#define	ECC_NPRIME_SET 0x20
#define	ECC_DEGREE_SET 0x40
/* for ECC_curve */
#define	ECC_ORDER_SET 0x80
#define	ECC_COFACTOR_SET 0x100
/* for ECC_point */
#define	ECC_X_VALID	 0x200
#define	ECC_Y_VALID	 0x400
#define	ECC_Z_VALID	 0x800
#define	ECC_AZ4_VALID	 0x1000
#define	ECC_AFFINE	 0x2000
#define	ECC_INFINITY	 0x4000
/* macro to return name */
#define	ECC_CURVE_NAME(crvp) ((crvp)->NISTname)
/* macro for size compuations */
#define	ECC_BUFFER_SIZE(crvp) ((crvp)->modulusinfo.modulusMSB / 8 + 1)

/* special value for abinary */
#define	ECC_BIG_A	(1 << (BIG_CHUNK_SIZE - 1))

typedef struct ECC_modulus_info {
	int		flags;
	BIGNUM		modulus;
	BIGNUM		R;    /* montgomery encoded R */
	BIGNUM		One;  /* montgomery encoded 1 */
	BIGNUM		Rinv; /* montgomery encoded 1/R */
	BIGNUM		R2;   /* montgomery encoded R^2 */
	BIG_CHUNK_TYPE	nprime;
	int		modulusMSB;
} ECC_modulus_info_t;


typedef struct ECC_point {
	int		flags;
	BIGNUM		x;
	BIGNUM		y;	/* used only for affine and Jacobian */
	BIGNUM		z;	/* used only for Jacobian and Lopez-Dahab */
	BIGNUM		az4;	/* used only for Niagara2-like operations */
} ECC_point_t;

typedef struct ECC_curve {
	char			name[20];
	char			*OID;
	int			OIDlen;
	int			flags;
	ECC_modulus_info_t	modulusinfo;
	/*
	 * abinary is the "obvious" representation of curve parameter
	 * "a" in a single scalar, signed for prime style, and
	 * unsigned for polynomial style.  If it won't fit, set
	 * abinary to ECC_BIG_A.
	 */
	BIG_CHUNK_TYPE_SIGNED	abinary;
	BIGNUM			a;  /* montgomery encoded */
	BIGNUM			b;  /* montgomery encoded */
	ECC_point_t		basepoint; /* montgomery projective */
	BIGNUM			order; /* ordinary number */
	int			orderMSB;
	int			cofactor; /* not sure we need this */
} ECC_curve_t;

typedef struct {
	ECC_point_t	*P;
	ECC_point_t	*Q;
	BIGNUM		*mpy;
	ECC_curve_t	*crv;
} ncp_pointmul_params_t;


/* calls ncp_big_init on each BIGNUM, clears flag etc. */
BIG_ERR_CODE ECC_point_init(ECC_point_t *point, ECC_curve_t *crv);

/* calls ncp_big_finish on each BIGNUM, etc. */
void ECC_point_finish(ECC_point_t *point);

BIG_ERR_CODE ECC_set_curve_immutable(ECC_curve_t *crv);

BIG_ERR_CODE ECC_point_set(ECC_point_t *point,
    BIGNUM *x, BIGNUM *y, ECC_curve_t *crv);

BIG_ERR_CODE ECC_point_x(BIGNUM *result, ECC_point_t *point, ECC_curve_t *crv);

/* result and arg can alias, point must be in affine coordinates */
BIG_ERR_CODE ECC_point_to_projective(ECC_point_t *result, ECC_point_t *arg,
    ECC_curve_t *crv);

BIG_ERR_CODE ECC_fluff_modulus(ECC_modulus_info_t *tgt);
BIG_ERR_CODE set_AZ4(ECC_point_t *target, ECC_curve_t *crv);

/*
 * result and arg can alias.  If arg is in Lopez-Dahab coordinates,
 * only the X coordinate will be converted to affine coordinates, and
 * ECC_AFFINE_Y_VALID will be false in result->flags.  If the point is
 * infinity, ECC_INFINITY will be set and ECC_AFFINE_X_VALID and
 * ECC_AFFINE_X_VALID will be clear.  Point must be in projective
 * (Jacobian or Lopez-Dahab) coordinates.
 */
BIG_ERR_CODE ECC_point_to_affine(ECC_point_t *result, ECC_point_t *arg,
    ECC_curve_t *crv);

BIG_ERR_CODE ECC_point_in_curve(ECC_point_t *aa, ECC_curve_t *crv);


BIG_ERR_CODE ECC_point_add(ECC_point_t *result, ECC_point_t *aa,
    ECC_point_t *bb, ECC_curve_t *crv);

BIG_ERR_CODE ECC_point_double(ECC_point_t *result, ECC_point_t *aa,
    ECC_curve_t *crv);

BIG_ERR_CODE ECC_point_multiply(ECC_point_t *result, ECC_point_t *aa, BIGNUM *k,
    ECC_curve_t *crv, int needy, ncp_t *ncp, ncp_request_t *reqp);
ECC_curve_t *ncp_ECC_find_curve(uint8_t *OID);

#ifdef	__cplusplus
}
#endif

#endif /* _NCP_ELLIPTICCRV_H */
