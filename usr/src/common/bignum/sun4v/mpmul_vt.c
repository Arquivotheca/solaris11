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
 *  Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */


#ifdef _KERNEL
#include <kernel_fp_use.h>
#include <sys/kmem.h>
#include <sys/debug.h>
#define	big_malloc(size)	kmem_alloc(size, KM_NOSLEEP)
#define	big_free(ptr, size)	kmem_free(ptr, size)

#else

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#define	ASSERT	assert
#define	big_malloc(size)	malloc(size)
#define	big_free(ptr, size)	free(ptr)

#endif

#include "bignum.h"
#include "mpmul_vt.h"

#ifdef YF_MPMUL

#if BIG_CHUNK_SIZE != 64
#error this only works with BIG_CHUNK_SIZE == 64
#endif

void
mpmul_arr_yf(uint64_t *res, uint64_t *m1, uint64_t *m2, int len)
{
	mpm_yf_pars_t	params;
	BIG_ERR_CODE	err;

	params.m1 = m1;
	params.m2 = m2;
	params.res = res;
	params.functions = mpm_yf_functions_table[len - 1];
again:
	err = mpm_yf_mpmul(&params);

	if (err == 0) {
		return;
	} else {
		goto again;
	}
}


BIG_ERR_CODE
big_mp_mul_yf(BIGNUM *res, BIGNUM *m1, BIGNUM *m2)
{
	BIG_ERR_CODE	err;
	BIG_CHUNK_TYPE	*val;
	BIG_CHUNK_TYPE	tmpbuf[64];
	int	len, size, i;

	len = m1->len;
	while ((len > 1) && (m1->value[len - 1] == 0)) {
		len--;
	}
	m1->len = len;

	len = m2->len;
	while ((len > 1) && (m2->value[len - 1] == 0)) {
		len--;
	}
	m2->len = len;

	if (m1->len > len) {
		len = m1->len;
	}

	ASSERT(len <= 32);


	size = len;
	if (m1->size < size) {
		if ((err = big_extend(m1, size)) != BIG_OK) {
			return (err);
		}
	}

	val = m1->value;

	for (i = m1->len; i < size; i++) {
		val[i] = 0;
	}

	if (m2->size < size) {
		if ((err = big_extend(m2, size)) != BIG_OK) {
			return (err);
		}
	}

	val = m2->value;

	for (i = m2->len; i < size; i++) {
		val[i] = 0;
	}

	if (res->size < 2 * size) {
		if ((err = big_extend(res, 2 * size)) != BIG_OK) {
			return (err);
		}
	}

	mpmul_arr_yf(&(tmpbuf[0]), &(m1->value[0]), &(m2->value[0]), len);

	len = 2 * len;
	while ((len > 1) && tmpbuf[len - 1] == 0) {
		len--;
	}

	if (res->size < len) {
		if ((err = big_extend(res, len)) != BIG_OK) {
			return (err);
		}
	}
	res->len = len;
	bcopy(tmpbuf, res->value, len * sizeof (BIG_CHUNK_TYPE));
	return (BIG_OK);
}

#endif
