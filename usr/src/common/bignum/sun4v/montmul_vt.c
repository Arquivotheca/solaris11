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
#include "montmul_vt.h"


static const int sizereq[] = {
	3,   3,  3,  4,  6,  6,  8,  8,  9, 16, 16, 16, 16, 16, 16, 16,
	24, 24, 24, 24, 24, 24, 24, 24, 32, 32, 32, 32, 32, 32, 32, 32
};


#ifdef YF_MODEXP

#define	YF_MODEXP_RETRY_MAX	10
#define	MAX_EXP_BIT_GROUP_SIZE	5
#define	APOWERS_MAX_SIZE	(1 << (MAX_EXP_BIT_GROUP_SIZE - 1))

BIG_ERR_CODE
big_modexp_ncp_yf(BIGNUM *result, BIGNUM *ma, BIGNUM *e, BIGNUM *n,
    BIGNUM *tmp, BIG_CHUNK_TYPE n0)

{
	BIGNUM		apowers[APOWERS_MAX_SIZE];
	BIGNUM		savetmp;
	BIG_CHUNK_TYPE	savetmpvalue[BIGTMPSIZE];
	BIGNUM		savema;
	BIG_CHUNK_TYPE	savemavalue[BIGTMPSIZE];
	BIG_CHUNK_TYPE	*val, *apowersbuf = NULL;
	int		i, j, k, l, m, p;
	int		bit, bitind, bitcount, groupbits, apowerssize;
	int		nbits, len, size;
	mm_yf_funcs_t	functions;
	void		**slp, *naddr;
	int		slpind = 0, slpsize;
#ifdef _KERNEL
	fp_save_t	fp_save_buf;
#endif
	BIG_ERR_CODE	err;

	len = n->len;
	while ((len > 1) && (n->value[len - 1] == 0)) {
		len--;
	}
	n->len = len;

	ASSERT(n->len <= 32);
	/* LINTED E_TRUE_LOGICAL_EXPR */
	ASSERT(BIG_CHUNK_SIZE == 64);

	size = sizereq[n->len - 1];
	if (n->size < size) {
		if ((err = big_extend(n, size)) != BIG_OK) {
			return (err);
		}
	}
	val = n->value;
	for (i = n->len; i < size; i++) {
		val[i] = 0;
	}
	naddr = (void *)(&(n->value[0]));

	if (ma->size < size) {
		if ((err = big_extend(ma, size)) != BIG_OK) {
			return (err);
		}
	}
	val = ma->value;
	for (i = ma->len; i < size; i++) {
		val[i] = 0;
	}

	if (tmp->size < size) {
		if ((err = big_extend(tmp, size)) != BIG_OK) {
			return (err);
		}
	}
	val = tmp->value;
	for (i = tmp->len; i < size; i++) {
		val[i] = 0;
	}

	nbits = big_bitlength(e);
	if (nbits < 50) {
		groupbits = 1;
		apowerssize = 1;
	} else {
		groupbits = MAX_EXP_BIT_GROUP_SIZE;
		apowerssize = 1 << (groupbits - 1);
	}

	if ((err = big_init1(&savetmp, size,
	    savetmpvalue, arraysize(savetmpvalue))) != BIG_OK) {
		return (err);
	}
	bcopy((char *)(&(tmp->value[0])),
	    (char *)(&(savetmp.value[0])), size * sizeof (BIG_CHUNK_TYPE));

	if ((err = big_init1(&savema, size,
	    savemavalue, arraysize(savemavalue))) != BIG_OK) {
		goto ret;
	}
	bcopy((char *)(&(ma->value[0])),
	    (char *)(&(savema.value[0])), size * sizeof (BIG_CHUNK_TYPE));

	/* set the malloced bit to help cleanup */
	for (i = 0; i < apowerssize; i++) {
		apowers[i].malloced = 0;
	}

	if ((apowersbuf = big_malloc(
	    apowerssize * size * sizeof (BIG_CHUNK_TYPE))) == NULL) {
		err = BIG_NO_MEM;
		goto ret;
	}
	for (i = 0; i < apowerssize; i++) {
		if ((err = big_init1(&(apowers[i]),
		    size, &(apowersbuf[i * size]), size)) != BIG_OK) {
			goto ret;
		}
	}

#ifdef ORIG_MODEXP_CODE
	(void) big_copy(&(apowers[0]), ma);
#endif
	bcopy((char *)(&(ma->value[0])),
	    (char *)(&(apowers[0].value[0])), size * sizeof (BIG_CHUNK_TYPE));

	functions = mm_yf_functions_table[len - 1];

	slpsize = 10000 * sizeof (void *); /* XXX - fixme */
	if ((slp = (void *)big_malloc(slpsize)) == NULL) {
		err = BIG_NO_MEM;
		goto ret;
	}

	slpind = 0;
	slp[slpind++] = (void *)(&n0);

#ifdef ORIG_MODEXP_CODE
	if ((err = big_mont_mul(&tmp1, ma, ma, n, n0)) != BIG_OK) {
		goto ret;
	}
	(void) big_copy(ma, &tmp1);
#endif
	slp[slpind++] = functions.load_a_func;
	slp[slpind++] = (void *)(&(ma->value[0]));
	slp[slpind++] = functions.load_n_func;
	slp[slpind++] = naddr;
	slp[slpind++] = functions.montsqr_func;
	slp[slpind++] = functions.store_a_func;
	slp[slpind++] = (void *)(&(ma->value[0]));

	slp[slpind++] = (void *)mm_yf_restore_func;

	slp[slpind++] = functions.load_a_func;
	slp[slpind++] = (void *)(&(apowers[0].value[0]));

	for (i = 1; i < apowerssize; i++) {
#ifdef ORIG_MODEXP_CODE
		if ((err = big_mont_mul(&tmp1, ma,
		    &(apowers[i-1]), n, n0)) != BIG_OK) {
			goto ret;
		}
		(void) big_copy(&apowers[i], &tmp1);
#endif
		slp[slpind++] = functions.load_n_func;
		slp[slpind++] = naddr;
		slp[slpind++] = functions.load_b_func;
		slp[slpind++] = (void *)(&(ma->value[0]));
		slp[slpind++] = functions.montmul_func;
		slp[slpind++] = functions.store_a_func;
		slp[slpind++] = (void *)(&(apowers[i].value[0]));
	}

	slp[slpind++] = (void *)mm_yf_restore_func;

	slp[slpind++] = functions.load_a_func;
	slp[slpind++] = (void *)(&(tmp->value[0]));
	slp[slpind++] = functions.load_n_func;
	slp[slpind++] = naddr;

	bitind = nbits % BIG_CHUNK_SIZE;
	k = 0;
	l = 0;
	p = 0;
	bitcount = 0;
	for (i = nbits / BIG_CHUNK_SIZE; i >= 0; i--) {
		for (j = bitind - 1; j >= 0; j--) {
			bit = (e->value[i] >> j) & 1;
			if ((bitcount == 0) && (bit == 0)) {
#ifdef ORIG_MODEXP_CODE
				if ((err = big_mont_mul(tmp,
				    tmp, tmp, n, n0)) != BIG_OK) {
					goto ret;
				}
#endif
				slp[slpind++] = functions.montsqr_func;

			} else {
				bitcount++;
				p = p * 2 + bit;
				if (bit == 1) {
					k = k + l + 1;
					l = 0;
				} else {
					l++;
				}
				if (bitcount == groupbits) {
					for (m = 0; m < k; m++) {
#ifdef ORIG_MODEXP_CODE
						if ((err = big_mont_mul(tmp,
						    tmp, tmp, n, n0)) !=
						    BIG_OK) {
							goto ret;
						}
#endif
						slp[slpind++] =
						    functions.montsqr_func;
					}
#ifdef ORIG_MODEXP_CODE
					if ((err = big_mont_mul(tmp, tmp,
					    &(apowers[p >> (l + 1)]),
					    n, n0)) != BIG_OK) {
						goto ret;
					}
#endif
					slp[slpind++] =
					    functions.load_b_func;
					slp[slpind++] =  (void *)
					    (&(apowers[p >> (l + 1)].
					    value[0]));
					slp[slpind++] =
					    functions.montmul_func;
					for (m = 0; m < l; m++) {
#ifdef ORIG_MODEXP_CODE
						if ((err = big_mont_mul(tmp,
						    tmp, tmp, n, n0)) !=
						    BIG_OK) {
							goto ret;
						}
#endif
						slp[slpind++] =
						    functions.montsqr_func;
					}
					k = 0;
					l = 0;
					p = 0;
					bitcount = 0;
				}
			}
		}
		bitind = BIG_CHUNK_SIZE;
	}

	for (m = 0; m < k; m++) {
#ifdef ORIG_MODEXP_CODE
		if ((err = big_mont_mul(tmp, tmp, tmp, n, n0)) != BIG_OK) {
			goto ret;
		}
#endif
		slp[slpind++] = functions.montsqr_func;
	}
	if (p != 0) {
#ifdef ORIG_MODEXP_CODE

		if ((err = big_mont_mul(tmp, tmp,
		    &(apowers[p >> (l + 1)]), n, n0)) != BIG_OK) {
			goto ret;
		}
#endif
		slp[slpind++] = functions.load_b_func;
		slp[slpind++] =  (void *)(&(apowers[p >> (l + 1)].value[0]));
		slp[slpind++] = functions.montmul_func;
	}
	for (m = 0; m < l; m++) {
#ifdef ORIG_MODEXP_CODE
		if ((err = big_mont_mul(result, tmp, tmp, n, n0)) != BIG_OK) {
			goto ret;
		}
#endif
		slp[slpind++] = functions.montsqr_func;
	}
	slp[slpind++] = functions.store_a_func;
	slp[slpind++] = (void *)(&(result->value[0]));

	slp[slpind++] = (void *)mm_yf_ret_from_mont_func;
	slp[slpind++] = NULL;

#ifdef _KERNEL
	start_kernel_fp_use(&fp_save_buf);
#endif
	i = 0;
	do {
		err = mm_yf_execute_slp(slp);

		if (err == 0) {
			result->len = len;
		} else {
			bcopy((char *)(&(savema.value[0])),
			    (char *)(&(ma->value[0])),
			    size * sizeof (BIG_CHUNK_TYPE));
			bcopy((char *)(&(savema.value[0])),
			    (char *)(&(apowers[0].value[0])),
			    size * sizeof (BIG_CHUNK_TYPE));
			bcopy((char *)(&(savetmp.value[0])),
			    (char *)(&(tmp->value[0])),
			    size * sizeof (BIG_CHUNK_TYPE));
			i++;

#ifdef YF_MODEXP_DEBUG
			(void) printf("mm_yf_execute_slp() returned %d\n", err);
#endif
		}
	} while ((err != 0) && (i < YF_MODEXP_RETRY_MAX));
	if (err != 0) {
		err = BIG_GENERAL_ERR;
	}

#ifdef _KERNEL
	end_kernel_fp_use(&fp_save_buf);
#endif

ret:
	big_free(slp, slpsize);

	for (i = apowerssize - 1; i >= 0; i--) {
		big_finish(&(apowers[i]));
	}
	if (apowersbuf != NULL) {
		big_free(apowersbuf,
		    apowerssize * size * sizeof (BIG_CHUNK_TYPE));
	}

	big_finish(&savema);
	big_finish(&savetmp);
	return (err);
}
#endif

#ifdef YF_MONTMUL
#ifdef YF_MONTMUL_SLP

BIG_ERR_CODE
big_mont_mul_yf(BIGNUM *ret,
    BIGNUM *a, BIGNUM *b, BIGNUM *n, BIG_CHUNK_TYPE n0)
{
	mm_yf_funcs_t	functions;
	BIG_ERR_CODE	err;
	int		i, len, size, slpind;
	BIG_CHUNK_TYPE	*val;
	void		*slp[20];
	BIG_CHUNK_TYPE	tmpbuf[32];

	len = n->len;
	while ((len > 1) && (n->value[len - 1] == 0)) {
		len--;
	}
	n->len = len;

	ASSERT(n->len <= 32);
	/* LINTED E_TRUE_LOGICAL_EXPR */
	ASSERT(BIG_CHUNK_SIZE == 64);

	size = sizereq[len - 1];
	if (n->size < size) {
		if ((err = big_extend(n, size)) != BIG_OK) {
			return (err);
		}
	}
	val = n->value;
	for (i = len; i < size; i++) {
		val[i] = 0;
	}

	if (a->size < size) {
		if ((err = big_extend(a, size)) != BIG_OK) {
			return (err);
		}
	}
	val = a->value;
	for (i = a->len; i < size; i++) {
		val[i] = 0;
	}

	if (b->size < size) {
		if ((err = big_extend(b, size)) != BIG_OK) {
			return (err);
		}
	}
	val = b->value;
	for (i = b->len; i < size; i++) {
		val[i] = 0;
	}

	if (ret->size < size) {
		if ((err = big_extend(ret, size)) != BIG_OK) {
			return (err);
		}
	}

	functions = mm_yf_functions_table[len - 1];

	slpind = 0;
	slp[slpind++] = &n0;
	slp[slpind++] = functions.load_a_func;
	slp[slpind++] = (void *)(a->value);
	slp[slpind++] = functions.load_n_func;
	slp[slpind++] = (void *)(n->value);
	if (a->value == b->value) {
		slp[slpind++] = functions.montsqr_func;
	} else {
		slp[slpind++] = functions.load_b_func;
		slp[slpind++] = (void *)(b->value);
		slp[slpind++] = functions.montmul_func;
	}
	slp[slpind++] = functions.store_a_func;
	slp[slpind++] = (void *)(&(tmpbuf[0]));
	slp[slpind++] = (void *)mm_yf_ret_from_mont_func;
	slp[slpind++] = NULL;
again:
	err = mm_yf_execute_slp(slp);

	if (err == 0) {
		bcopy(tmpbuf, ret->value, len * sizeof (BIG_CHUNK_TYPE));
		while ((len > 1) && (tmpbuf[len-1] == 0)) {
			len--;
		}
		ret->len = len;
		return (BIG_OK);
	} else {
		goto again;
	}
}

#else /* YF_MONTMUL_SLP */

BIG_ERR_CODE
big_mont_mul_yf(BIGNUM *ret,
    BIGNUM *a, BIGNUM *b, BIGNUM *n, BIG_CHUNK_TYPE n0)
{
	mm_yf_pars_t	params;
	BIG_ERR_CODE	err;
	int		i, len, size;
	BIG_CHUNK_TYPE	*val;
	BIG_CHUNK_TYPE	tmpbuf[32];

	len = n->len;
	while ((len > 1) && (n->value[len - 1] == 0)) {
		len--;
	}
	n->len = len;

	ASSERT(n->len <= 32);
	/* LINTED E_TRUE_LOGICAL_EXPR */
	ASSERT(BIG_CHUNK_SIZE == 64);

	size = sizereq[n->len - 1];
	if (n->size < size) {
		if ((err = big_extend(n, size)) != BIG_OK) {
			return (err);
		}
	}
	val = n->value;
	for (i = n->len; i < size; i++) {
		val[i] = 0;
	}

	if (a->size < size) {
		if ((err = big_extend(a, size)) != BIG_OK) {
			return (err);
		}
	}
	val = a->value;
	for (i = a->len; i < size; i++) {
		val[i] = 0;
	}

	if (b->size < size) {
		if ((err = big_extend(b, size)) != BIG_OK) {
			return (err);
		}
	}
	val = b->value;
	for (i = b->len; i < size; i++) {
		val[i] = 0;
	}
	if (ret->size < size) {
		if ((err = big_extend(ret, size)) != BIG_OK) {
			return (err);
		}
	}


	params.a = a->value;
	params.b = b->value;
	params.n = n->value;
	params.nprime = &n0;
	params.ret = tmpbuf;
	params.functions = mm_yf_functions_table[len - 1];
again:
	if (a == b) {
		err = mm_yf_montsqr(&params);
	} else {
		err = mm_yf_montmul(&params);
	}

	if (err == 0) {
		bcopy(tmpbuf, ret->value, len * sizeof (BIG_CHUNK_TYPE));
		while ((len > 1) && (tmpbuf[len-1] == 0)) {
			len--;
		}
		ret->len = len;
		return (BIG_OK);
	} else {
		goto again;
	}
}

#endif /* YF_MONTMUL_SLP */
#endif /* YF_MONTMUL */
