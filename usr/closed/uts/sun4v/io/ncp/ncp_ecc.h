/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _NCP_ECC_H
#define	_NCP_ECC_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/ncp.h>
#include "ncp_ellipticcrv.h"

#define	ECC_NUM_CURVES	15
#define	MAXECKEYLEN	72	/* 571 bits */

#define	CONVERTRV (rv >= 0 ? rv : bigerrcode_to_crypto_errcode(rv))

extern ECC_curve_t ncp_ECC_curves[ECC_NUM_CURVES];

int ECC_key_pair_gen(uint8_t *dp, int *dlen,
    uint8_t *xp, int *xlen, uint8_t *yp, int *ylen, ECC_curve_t *crvp,
    ncp_t *ncp, ncp_request_t *reqp);
int ECC_ECDH_derive(uint8_t *result, int *resultlen,
    uint8_t *d, int dlen, uint8_t *x1, int x1len, uint8_t *y1, int y1len,
    ECC_curve_t *crv, ncp_t *ncp, ncp_request_t *reqp);
int ECC_ECDSA_sign(uint8_t *messagehash, int hashlen8, uint8_t *d, int dlen,
    uint8_t *r, int *rlen, uint8_t *s, int *slen, ECC_curve_t *crv,
    ncp_t *ncp, ncp_request_t *reqp);
int ECC_ECDSA_verify(int *verified,
    uint8_t *messagehash, int hashlen8, uint8_t *r,
    int rlen, uint8_t *s, int slen, uint8_t *x, int xlen, uint8_t *y, int ylen,
    ECC_curve_t *crv, ncp_t *ncp, ncp_request_t *reqp);
int bigerrcode_to_crypto_errcode(BIG_ERR_CODE rv);
ECC_curve_t *ncp_ECC_find_curve_by_name(char *name);
int ECC_build_curve_table();

#ifdef	__cplusplus
}
#endif

#endif	/* _NCP_ECC_H */
