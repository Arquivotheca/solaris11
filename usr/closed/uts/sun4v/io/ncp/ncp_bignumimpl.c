/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/ncp_bignum.h>
#include <sys/random.h>


#define	CHECK(expr) if ((rv = (expr)) != BIG_OK) { goto cleanexit; }


BIG_ERR_CODE
ncp_big_set_int(BIGNUM *tgt, BIG_CHUNK_TYPE_SIGNED value)
{
	if (tgt->size < 1) {
		return (BIG_INVALID_ARGS);
	}
	tgt->len = 1;
	if (value < 0) {
		tgt->sign = -1;
		tgt->value[0] = -value;
	} else {
		tgt->sign = 1;
		tgt->value[0] = value;
	}

	return (BIG_OK);
}


/*
 * converts kn to bn, montgomery encoding if mont is true.  If check
 * it true, it checks that reduce would make no difference.  If check
 * is true arguments through nndegree must be supplied.  If mont is
 * true all arguements must be supplied.  If none of the above are
 * true, args after mont are ignored.  If ispoly is false, nndegree is
 * ignored.
 */
BIG_ERR_CODE
ncp_kcl_to_bignum(BIGNUM *bn, uint8_t *kn, int knlen, int check, int mont,
    int ispoly, BIGNUM *nn, int nndegree, BIG_CHUNK_TYPE nprime, BIGNUM *R)
{
	int		rv;
	int		checkrv = 0;

	CHECK(ncp_big_extend(bn, knlen / (BIG_CHUNK_SIZE / BITSINBYTE) + 2));
	ncp_kcl2bignum(bn, kn, knlen);
	if (check || mont) {
		if (ispoly) {
			if (ncp_big_MSB(bn) >= nndegree) {
				checkrv = BIG_TEST_FAILED;
			}
		} else {
			if (ncp_big_cmp_abs(bn, nn) >= 0) {
				checkrv = BIG_TEST_FAILED;
			}
		}
	}
	if (check && checkrv) {
		rv = checkrv;
		goto cleanexit;
	}
	if (mont) {
		/*
		 * Montgomery encoded values must be reduced before
		 * encoding.
		 */
		if (checkrv) {
			CHECK(ncp_big_reduce(bn, nn, ispoly));
		}
		CHECK(ncp_big_mont_encode(bn, bn, ispoly, nn, nprime, R));
	}

cleanexit:

	return (rv);
}


/*
 * Converts bignum to big-endian uint8t_array.  Converts from
 * montgomery encoding if mont is true.  If mont is false, the last 4
 * arguments are ignored.  knlength is in-out.  If the buffer is too
 * small, BIG_BUFFER_TOO_SMALL is returned and *knlength is set to the
 * required size.  Otherwise if shorten is zero, the value is stored
 * in the buffer right justified.  Otherwise the value is stored in
 * the buffer left justified, and *knlength is set to the number of
 * bytes filled.
 */
BIG_ERR_CODE
ncp_bignum_to_kcl(uint8_t *kn, int *knlength, BIGNUM *bn, int shorten,
    int mont, int ispoly, BIGNUM *nn, BIG_CHUNK_TYPE nprime, BIGNUM *Rinv)
{
	BIGNUM		T;
	BIGNUM		*p;
	int		rv;
	int		minbytes;

	T.malloced = 0;

	if (mont) {
		p = &T;
		CHECK(ncp_big_init(&T, nn->len + 1));
		CHECK(ncp_big_mont_decode(&T, bn, ispoly, nn, nprime, Rinv));
	} else {
		p = bn;
		rv = BIG_OK;
	}

	minbytes = ncp_big_MSB(p) / BITSINBYTE + 1;
	if (minbytes > *knlength) {
		*knlength = minbytes;
		rv = BIG_BUFFER_TOO_SMALL;
		goto cleanexit;
	} else if (shorten) {
		*knlength = minbytes;
	}

	ncp_bignum2kcl(kn, p, *knlength);

cleanexit:

	if (p == &T) {
		big_finish(&T);
	}

	return (rv);
}


BIG_ERR_CODE
ncp_RSA_key_init(RSAkey *key, int psize, int qsize)
{
	int		plen, qlen, nlen;
	BIG_ERR_CODE	err = BIG_OK;

/* EXPORT DELETE START */

	plen = (psize + (BIG_CHUNK_SIZE - 1)) / BIG_CHUNK_SIZE;
	qlen = (qsize + (BIG_CHUNK_SIZE - 1)) / BIG_CHUNK_SIZE;
	nlen = plen + qlen;
	key->size = psize + qsize;
	if ((err = big_init1(&(key->p), plen, NULL, 0)) != BIG_OK) {
		return (err);
	}
	if ((err = big_init1(&(key->q), qlen, NULL, 0)) != BIG_OK) {
		goto ret1;
	}
	if ((err = big_init1(&(key->n), nlen, NULL, 0)) != BIG_OK) {
		goto ret2;
	}
	if ((err = big_init1(&(key->d), nlen, NULL, 0)) != BIG_OK) {
		goto ret3;
	}
	if ((err = big_init1(&(key->e), nlen, NULL, 0)) != BIG_OK) {
		goto ret4;
	}
	if ((err = big_init1(&(key->dmodpminus1), plen, NULL, 0)) != BIG_OK) {
		goto ret5;
	}
	if ((err = big_init1(&(key->dmodqminus1), qlen, NULL, 0)) != BIG_OK) {
		goto ret6;
	}
	if ((err = big_init1(&(key->pinvmodq), qlen, NULL, 0)) != BIG_OK) {
		goto ret7;
	}
	if ((err = big_init1(&(key->p_rr), plen, NULL, 0)) != BIG_OK) {
		goto ret8;
	}
	if ((err = big_init1(&(key->q_rr), qlen, NULL, 0)) != BIG_OK) {
		goto ret9;
	}
	if ((err = big_init1(&(key->n_rr), nlen, NULL, 0)) != BIG_OK) {
		goto ret10;
	}

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


void
ncp_RSA_key_finish(RSAkey *key)
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


BIG_ERR_CODE
ncp_DSA_key_init(DSAkey *key, int size)
{
	BIG_ERR_CODE	err = BIG_OK;
	int		len;

/* EXPORT DELETE START */

	len = (size - 1) / BIG_CHUNK_SIZE + 1;
	key->size = size;
	if ((err = big_init1(&(key->q), BIG_CHUNKS_FOR_160BITS, NULL, 0)) !=
	    BIG_OK) {
		return (err);
	}
	if ((err = big_init1(&(key->p), len, NULL, 0)) != BIG_OK) {
		goto ret1;
	}
	if ((err = big_init1(&(key->g), len, NULL, 0)) != BIG_OK) {
		goto ret2;
	}
	if ((err = big_init1(&(key->x), BIG_CHUNKS_FOR_160BITS, NULL, 0)) !=
	    BIG_OK) {
		goto ret3;
	}
	if ((err = big_init1(&(key->y), len, NULL, 0)) != BIG_OK) {
		goto ret4;
	}
	if ((err = big_init1(&(key->k), BIG_CHUNKS_FOR_160BITS, NULL, 0)) !=
	    BIG_OK) {
		goto ret5;
	}
	if ((err = big_init1(&(key->r), BIG_CHUNKS_FOR_160BITS, NULL, 0)) !=
	    BIG_OK) {
		goto ret6;
	}
	if ((err = big_init1(&(key->s), BIG_CHUNKS_FOR_160BITS, NULL, 0)) !=
	    BIG_OK) {
		goto ret7;
	}
	if ((err = big_init1(&(key->v), BIG_CHUNKS_FOR_160BITS, NULL, 0)) !=
	    BIG_OK) {
		goto ret8;
	}

	return (BIG_OK);

ret8:
	big_finish(&(key->s));
ret7:
	big_finish(&(key->r));
ret6:
	big_finish(&(key->k));
ret5:
	big_finish(&(key->y));
ret4:
	big_finish(&(key->x));
ret3:
	big_finish(&(key->g));
ret2:
	big_finish(&(key->p));
ret1:
	big_finish(&(key->q));

/* EXPORT DELETE END */

	return (err);
}


void
ncp_DSA_key_finish(DSAkey *key)
{

/* EXPORT DELETE START */

	big_finish(&(key->v));
	big_finish(&(key->s));
	big_finish(&(key->r));
	big_finish(&(key->k));
	big_finish(&(key->y));
	big_finish(&(key->x));
	big_finish(&(key->g));
	big_finish(&(key->p));
	big_finish(&(key->q));

/* EXPORT DELETE END */

}



/* precondition: 0 <= aa < nn, 0 <= bb < nn */
BIG_ERR_CODE
ncp_big_mod_add(BIGNUM *result, BIGNUM *aa, BIGNUM *bb, BIGNUM *nn)
{
	BIG_ERR_CODE	rv;

/* EXPORT DELETE START */

	CHECK(big_add_abs(result, aa, bb));
	if (big_cmp_abs(result, nn) >= 0) {
		CHECK(big_sub_pos(result, result, nn));
	}

/* EXPORT DELETE END */

cleanexit:

	return (rv);
}

/* precondition: 0 <= aa < nn, 0 <= bb < nn */
BIG_ERR_CODE
ncp_big_mod_sub(BIGNUM *result, BIGNUM *aa, BIGNUM *bb, BIGNUM *nn)
{
	BIG_ERR_CODE	rv;

/* EXPORT DELETE START */

	/*
	 *  We could be smarter about this, maybe.  N.B. It gets
	 *  called with result and bb aliased.
	 */

	CHECK(big_sub(result, aa, bb));
	if (result->sign < 0) {
		result->sign = 1;
		CHECK(big_sub_pos(result, nn, result));
	}

/* EXPORT DELETE END */

cleanexit:

	return (rv);
}


BIG_ERR_CODE
ncp_big_shiftright(BIGNUM *result, BIGNUM *aa, int offs)
{
	int		rv = 0;

	if ((result != aa) && (result->size < aa->len)) {
		CHECK(ncp_big_extend(result, aa->len));
	}
	big_shiftright(result, aa, offs);

cleanexit:
	return (rv);
}


BIG_ERR_CODE
ncp_big_mul_extend(BIGNUM *result, BIGNUM *aa, BIGNUM *bb)
{
	int		rv;
	CHECK(ncp_big_extend(result,  aa->len + bb->len));
	CHECK(big_mul(result, aa, bb));
cleanexit:
	return (rv);
}


BIG_ERR_CODE
ncp_big_mont_mul_extend(BIGNUM *ret,
    BIGNUM *a, BIGNUM *b, BIGNUM *n, BIG_CHUNK_TYPE n0)
{

	int		rv;

	CHECK(ncp_big_extend(ret, 2 * n->len + 1));
	CHECK(big_mont_mul(ret, a, b, n, n0));

cleanexit:
	return (rv);
}


BIG_ERR_CODE
ncp_randombignum(BIGNUM *r, int lengthinbits)
{
	size_t		len1;
	BIG_ERR_CODE	rv;

/* EXPORT DELETE START */

	len1 = (lengthinbits - 1) / BIG_CHUNK_SIZE + 1;

	if (len1 > r->size) {
		if ((rv = big_extend(r, len1)) != BIG_OK) {
			return (rv);
		}
	}

	r->len = (int)len1;
	(void) random_get_pseudo_bytes((uchar_t *)(r->value),
	    r->len * sizeof (BIG_CHUNK_TYPE));
	r->value[r->len - 1] |= BIG_CHUNK_HIGHBIT;
	if ((lengthinbits % BIG_CHUNK_SIZE) != 0)
		r->value[r->len - 1] =
		    r->value[r->len - 1] >>
		    (BIG_CHUNK_SIZE - (lengthinbits % BIG_CHUNK_SIZE));
	r->sign = 1;

/* EXPORT DELETE END */

	return (BIG_OK);
}


/*
 * New additions of general purpose goodies, plus a function to
 * compute inverses in prime fields.
 */

/* Not montgomery encoded.  result can alias other things. */
BIG_ERR_CODE
ncp_big_numeric_inverse(BIGNUM *result, BIGNUM *aa, BIGNUM *nn)
{
	int		rv;

	BIGNUM		U;
	BIGNUM		V;
	BIGNUM		X1;  /* can get negative */
	BIGNUM		X2;  /* can get negative */
	int		loopcatcher = 6 * 64 * nn->len;

	int modbits = ncp_big_MSB(nn);

	if (modbits < 0) {
		return (BIG_INVALID_ARGS);
	}

	if (ncp_big_is_zero(aa)) {
		return (BIG_DIV_BY_0);
	}

	U.malloced = 0;
	V.malloced = 0;
	X1.malloced = 0;
	X2.malloced = 0;

	CHECK(ncp_big_init(&U, modbits / BIG_CHUNK_SIZE + 2));
	CHECK(ncp_big_init(&V, modbits / BIG_CHUNK_SIZE + 2));
	CHECK(ncp_big_init(&X1, modbits / BIG_CHUNK_SIZE + 2));
	CHECK(ncp_big_init(&X2, modbits / BIG_CHUNK_SIZE + 2));

	CHECK(ncp_big_copy(&U, aa));
	CHECK(ncp_big_copy(&V, nn));
	CHECK(ncp_big_set_int(&X1, 1));
	CHECK(ncp_big_set_int(&X2, 0));

#ifdef BIGNUM_CHUNK_32
#define	IS_EVEN(Xp) (~((Xp)->value[0]) & 1)
#else
#define	IS_EVEN(Xp) (~((Xp)->value[0]) & 1ULL)
#endif

	while (!ncp_big_equals_one(&U) && !ncp_big_equals_one(&V)) {

		while (IS_EVEN(&U)) {
			if (--loopcatcher < 0) {
				rv = BIG_INVALID_ARGS;
				goto cleanexit;
			}

			(void) ncp_big_shiftright(&U, &U, 1);
			if (!IS_EVEN(&X1)) {
				CHECK(ncp_big_add(&X1, &X1, nn));
			}
			(void) ncp_big_shiftright(&X1, &X1, 1);
		}

		while (IS_EVEN(&V)) {
			if (--loopcatcher < 0) {
				rv = BIG_INVALID_ARGS;
				goto cleanexit;
			}


			(void) ncp_big_shiftright(&V, &V, 1);
			if (!IS_EVEN(&X2)) {
				CHECK(ncp_big_add(&X2, &X2, nn));
			}
			(void) ncp_big_shiftright(&X2, &X2, 1);
		}

		if (--loopcatcher < 0) {
			rv = BIG_INVALID_ARGS;
			goto cleanexit;
		}

		if (ncp_big_cmp_abs(&U, &V) >= 0) {
			CHECK(ncp_big_sub_pos(&U, &U, &V));
			CHECK(ncp_big_sub(&X1, &X1, &X2));
		} else {
			CHECK(ncp_big_sub_pos(&V, &V, &U));
			CHECK(ncp_big_sub(&X2, &X2, &X1));
		}
	}

	if (ncp_big_equals_one(&U)) {
		if (X1.sign < 0) {
			X1.sign = 1;
			CHECK(ncp_big_div_pos(NULL, &X1, &X1, nn));
			CHECK(ncp_big_sub_pos(result, nn, &X1));
		} else {
			CHECK(ncp_big_div_pos(NULL, result, &X1, nn));
		}
	} else {
		if (X2.sign < 0) {
			X2.sign = 1;
			CHECK(ncp_big_div_pos(NULL, &X2, &X2, nn));
			CHECK(ncp_big_sub_pos(result, nn, &X2));
		} else {
			CHECK(ncp_big_div_pos(NULL, result, &X2, nn));
		}
	}

#undef IS_EVEN

cleanexit:

	ncp_big_finish(&U);
	ncp_big_finish(&V);
	ncp_big_finish(&X1);
	ncp_big_finish(&X2);

	return (rv);
}


/* used inside big_MSB */
#define	MSBSTEP(word, shift, counter)	\
	if (word & (~((BIG_CHUNK_TYPE)0) << shift)) {	       \
	word >>= shift;		       \
	counter += shift;	       \
}

/*
 * Returns the index of the MSB in abs(X); returns -1 if X==0.  There
 * is a function big_bitlength would also work (after subtracting 1),
 * but this one is faster.
 */
int
ncp_big_MSB(BIGNUM *X)
{
	int		wd;
	int		bit;
	BIG_CHUNK_TYPE	MSwdval;

	for (wd = X->len - 1; wd >= 0; --wd) {
		MSwdval = X->value[wd];
		if (MSwdval) {
			goto foundMSword;
		}
	}
	/* X == 0 */
	return (-1);

foundMSword:
	bit = 0;
#ifndef BIGNUM_CHUNK_32
	MSBSTEP(MSwdval, 32, bit);
#endif
	MSBSTEP(MSwdval, 16, bit);
	MSBSTEP(MSwdval, 8, bit);
	MSBSTEP(MSwdval, 4, bit);
	MSBSTEP(MSwdval, 2, bit);
	MSBSTEP(MSwdval, 1, bit);

	return (BIG_CHUNK_SIZE * wd + bit);
}

/* returns 1 if aa == 1, otherwise returns 0 */
int
ncp_big_equals_one(BIGNUM *aa)
{
	int		i;
	int		len = aa->len;
	BIG_CHUNK_TYPE	*valp = aa->value;

	if (valp[0] != (BIG_CHUNK_TYPE)1) {
		return (0);
	}

	for (i = 2; i < len; ++i) {
		if (valp[i] != 0) {
			return (0);
		}
	}

	return (1);
}


/*
 * returns bit k of aa.  Out of range values of k return 0.
 */
int
ncp_big_extract_bit(BIGNUM *aa, int k)
{
	/*
	 * To avoid testing k < 0, we cast k to unsigned, and then
	 * catch it as a large value.  This is a hack, but it works
	 * for all practical cases.  (length >= 0)
	 */
	unsigned int	word = (unsigned int)k / BIG_CHUNK_SIZE;

	if (word >= aa->len) {
		return (0);
	}
	return ((aa->value[word] >> (k % BIG_CHUNK_SIZE)) & (BIG_CHUNK_TYPE)1);
}

/*
 * From here on down is polynomial stuff.
 */

BIG_ERR_CODE
ncp_big_poly_add(BIGNUM *result, BIGNUM *aa, BIGNUM *bb)
{
	int		i, shorter, longer;
	BIG_CHUNK_TYPE	*r, *a, *b, *c;
	BIG_ERR_CODE	err;
	BIGNUM		*longerarg;

/* EXPORT DELETE START */

	if (aa->len > bb->len) {
		shorter = bb->len;
		longer = aa->len;
		longerarg = aa;
	} else {
		shorter = aa->len;
		longer = bb->len;
		longerarg = bb;
	}
	if (result->size < longer) {
		/* since we have to extend, get an extra word */
		err = ncp_big_extend(result, longer + 1);
		if (err != BIG_OK) {
			return (err);
		}
	}

	r = result->value;
	a = aa->value;
	b = bb->value;
	c = longerarg->value;
	for (i = 0; i < shorter; i++) {
		r[i] = a[i] ^ b[i];
	}
	for (; i < longer; i++) {
		r[i] = c[i];
	}
	result->len = longer;
	result->sign = 1;

/* EXPORT DELETE END */

	return (BIG_OK);
}



/*
 * Reduces target modulo modulus, in place.  Preconditions: modulus !=
 * 0.  Length of target is set to min(length of target, words to hold
 * modulus).
 */
void
ncp_big_poly_reduce(BIGNUM *target, BIGNUM *modulus)
{
	int		modulusMSB = ncp_big_MSB(modulus); /* immutable */
	int		i, k;
	int		ws; /* word shift */
	int		bs; /* bit shift */
	BIG_CHUNK_TYPE	*tgtp = target->value;
	BIG_CHUNK_TYPE	*modp = modulus->value;

	if (modulusMSB == -1) {
		return;
	}

	for (k = ncp_big_MSB(target); k >= modulusMSB; --k) {
		/* skip over zero words */
		if (tgtp[k / BIG_CHUNK_SIZE] == 0) {
			/*
			 * k points into a word that is known to be
			 * zero.  "k &= ~63" moves k to low edge of
			 * current word, continue will move k to high
			 * edge of next less signifcant word.
			 */
			k &= ~(BIG_CHUNK_SIZE - 1);
			continue;
		}
		/*
		 * Skip over zero bits.  Yes, this could be optimized
		 * a bit.
		 */
		if ((tgtp[k / BIG_CHUNK_SIZE] &
		    ((BIG_CHUNK_TYPE)1 << (k % BIG_CHUNK_SIZE))) == 0) {
			continue;
		}
		/* if we get here, bit k is 1, and it must be forced to 0 */
		ws = (k - modulusMSB) / BIG_CHUNK_SIZE; /* word shift */
		bs = (k - modulusMSB) % BIG_CHUNK_SIZE; /* bit shift */
		if (bs == 0) {
			for (i = 0; i <= modulusMSB / BIG_CHUNK_SIZE; ++i) {
				tgtp[i + ws] ^= modp[i];
			}
		} else {
			for (i = 0; i < modulusMSB / BIG_CHUNK_SIZE; ++i) {
				tgtp[i + ws] ^= (modp[i] << bs);
				tgtp[i + ws + 1] ^=
				    (modp[i] >> (BIG_CHUNK_SIZE - bs));
			}
			/* last "iteration".  i preserved from loop */
			tgtp[i + ws] ^= (modp[i] << bs);
			if (i + ws + 1 < target->len) {
				tgtp[i + ws + 1] ^=
				    (modp[i] >> (BIG_CHUNK_SIZE - bs));
			}
		}
	}
	target->len = modulusMSB / BIG_CHUNK_SIZE + 1;
}

BIG_ERR_CODE
ncp_big_reduce(BIGNUM *target, BIGNUM *modulus, int ispoly)
{
	if (ispoly) {
		ncp_big_poly_reduce(target, modulus);
		return (BIG_OK);
	} else {
		return (ncp_big_div_pos(NULL, target, target, modulus));
	}
}



/*
 * returns a * b mod t^BIG_CHUNK_SIZE, where "*" is polynomial multipliation,
 * and all values are represented in BIG_CHUNK_TYPE types.  There are
 * undoubtedly faster ways of doing this.  But this is simple.  For
 * better performance, call with 'a' being the shorter value.
 */
static BIG_CHUNK_TYPE
xormpy(BIG_CHUNK_TYPE a, BIG_CHUNK_TYPE b)
{
	BIG_CHUNK_TYPE res = (BIG_CHUNK_TYPE)0;
	while (a) {
		/* clever bit twiddling to avoid a branch */
		res ^= b & ((BIG_CHUNK_TYPE)0 - (a & (BIG_CHUNK_TYPE)1));
		a >>= 1;
		b <<= 1;
	}

	return (res);
}


/*
 * result = (aa * bb) modulo nn in montgomery encoded polynomial
 * arithmatic modulo 2, i.e. in binary result = (aa * bb *
 * 2^-ROUNDUP64(modulusMSBposition)) modulo nn.  This is the method
 * according to the Koc and Acar paper, "Montgomery Multiplication in
 * GF(2^k)".  Any combination of aa, bb, and result can be aliased. nn
 * cannot be aliased with result.
 */


#define	BITS2BYTES(x)	((x + 7) / 8)

/*ARGSUSED*/
BIG_ERR_CODE
ncp_big_poly_mont_mul(BIGNUM *result, BIGNUM *aa, BIGNUM *bb, BIGNUM *nn,
    BIG_CHUNK_TYPE nprime)
{
	int		i, j, k, rv;
	BIG_CHUNK_TYPE	aavali;
	/*
	 * s is the number of BIG_CHUNK_SIZE bit words necessary
	 * to strictly hold the modulus.
	 * The actual algorithms often index one word
	 * ahead, and to avoid a zillion checks, we always allocate
	 * s+1 words BIG_CHUNK_SIZE bit words.
	 */
	int		s = ncp_big_MSB(nn) / BIG_CHUNK_SIZE + 1;
	BIGNUM		C; /* need since inputs and output can be aliased */

	BIG_CHUNK_TYPE
		nnwords[72 / BITS2BYTES(BIG_CHUNK_SIZE) + 1][BIG_CHUNK_SIZE];
	BIG_CHUNK_TYPE
		bbwords[72 / BITS2BYTES(BIG_CHUNK_SIZE) + 1][BIG_CHUNK_SIZE];

	C.malloced = 0;
	CHECK(ncp_big_init(&C, s + 1));
	C.len = s;
	CHECK(ncp_big_extend(aa, s + 1));
	CHECK(ncp_big_extend(bb, s + 1))
	CHECK(ncp_big_extend(nn, s + 1));
	for (i = aa->len; i < s + 1; ++i) {
		aa->value[i] = 0;
	}
	for (i = bb->len; i < s + 1; ++i) {
		bb->value[i] = 0;
	}
	for (i = nn->len; i < s + 1; ++i) {
		nn->value[i] = 0;
	}
	for (i = 0; i < s + 1; ++i) {
		C.value[i] = 0;
	}

	for (i = 0; i < s + 1; i++) {
		nnwords[i][0] = nn->value[i];
	}
	for (j = 1; j < BIG_CHUNK_SIZE; j ++) {
		nnwords[0][j] = nnwords[0][0] << j;
	}
	for (i = 1; i < s + 1; i++) {
		for (j = 1; j < BIG_CHUNK_SIZE; j ++) {
			nnwords[i][j] =
			    (nnwords[i - 1][0] >> (BIG_CHUNK_SIZE - j)) |
			    (nnwords[i][0] << j);
		}
	}

	for (i = 0; i < s + 1; i++) {
		bbwords[i][0] = bb->value[i];
	}
	for (j = 1; j < BIG_CHUNK_SIZE; j ++) {
		bbwords[0][j] = bbwords[0][0] << j;
	}
	for (i = 1; i < s + 1; i++) {
		for (j = 1; j < BIG_CHUNK_SIZE; j ++) {
			bbwords[i][j] =
			    (bbwords[i - 1][0] >> (BIG_CHUNK_SIZE - j)) |
			    (bbwords[i][0] << j);
		}
	}

	for (i = 0; i < s; ++i) {
		aavali = aa->value[i];
		for (j = 0; j < BIG_CHUNK_SIZE; j++) {
			if (aavali & 1) {
				for (k = 0; k < s + 1; k++) {
					C.value[k] ^= bbwords[k][j];
				}
			}
			aavali = aavali >> 1;
			if (C.value[0] & ((BIG_CHUNK_TYPE)1 << j)) {
				for (k = 0; k < s + 1; k++) {
					C.value[k] ^= nnwords[k][j];
				}
			}
		}

		if (C.value[0] != 0) {
			/*
			 * If we get here, the least significant word
			 * of the product is not zero, and something
			 * is wrong with the Montgomery
			 * multiplication.  A likely cause is that
			 * nprime is wrong.
			 */
			rv = BIG_INVALID_ARGS;
			goto cleanexit;
		}
		/*
		 * shift "right" BIG_CHUNK_SIZE bits,
		 * actually left in LE layout
		 */
		for (j = 0; j < s; ++j) {
			C.value[j] = C.value[j+1];
		}
		C.value[s] = 0;
	}

	CHECK(ncp_big_copy(result, &C));

	rv = BIG_OK;

cleanexit:

	ncp_big_finish(&C);

	return (rv);
}


BIG_ERR_CODE
ncp_big_mont_encode(BIGNUM *result, BIGNUM *input, int ispoly, BIGNUM *nn,
    BIG_CHUNK_TYPE nprime, BIGNUM *R)
{
	BIG_ERR_CODE	rv;

	if (ispoly) {
		rv = ncp_big_poly_mont_mul(result, input, R, nn, nprime);
	} else {
		rv = ncp_big_mont_mul_extend(result, input, R, nn, nprime);
	}
	return (rv);
}


BIG_ERR_CODE
ncp_big_mont_decode(BIGNUM *result, BIGNUM *input, int ispoly, BIGNUM *nn,
    BIG_CHUNK_TYPE nprime, BIGNUM *Rinv)
{
	if (ispoly) {
		return (ncp_big_poly_mont_mul(result,
		    input, Rinv, nn, nprime));
	} else {
		return (ncp_big_mont_mul_extend(result,
		    input, Rinv, nn, nprime));
	}
}

#define	VALWORDS(msbpos) ((msbpos) / BIG_CHUNK_SIZE + 1)


/*
 * Shifts aa left by k bits, and optionally reduces modulo nn, stores
 * in result.  Uses space proportional to k and time proportional to
 * (k + deg(target) - deg(nn)) * deg(nn), so do not call with
 * excessivlely large k.  To not take mod nn, pass NULL for nn.
 * nprime is passed in case we ever want to make this efficient for
 * large k, but at present it is not used.  aa and result can be
 * aliased.  Result is undefined for k < 0.
 */
/*ARGSUSED*/
int
ncp_big_poly_left_shift(BIGNUM *result, BIGNUM *aa, int k,
    BIGNUM *nn, BIG_CHUNK_TYPE nprime)
{
	int		rv;  /* set by CHECK */
	int		aadeg = ncp_big_MSB(aa);
	int		i;
	int		sw = k / BIG_CHUNK_SIZE; /* shift words */
	int		sb = k % BIG_CHUNK_SIZE; /* shift bits */
	BIG_CHUNK_TYPE	*aavalp;
	BIG_CHUNK_TYPE	*resultvalp;

	CHECK(ncp_big_extend(result, VALWORDS(aadeg + k)));
	/*
	 * It would seem that increasing result->len could make some
	 * garbage words be part of the value if aa and result alias,
	 * but any "new" words will not be used and will be assigned
	 * to below.
	 */
	result->len = VALWORDS(aadeg + k);
	resultvalp = result->value;
	/* assignment must be after ncp_big_extend if aa and result alias */
	aavalp = aa->value;
	/*
	 * i is set to MSW of result.  i will walk from most
	 * significant to least significant word.  i is live all the
	 * way though the zeroing code below.
	 */
	i = (aadeg + k) / BIG_CHUNK_SIZE;
	if (sb == 0) {
		for (; i >= sw; --i) {
			resultvalp[i] = aavalp[i - sw];
		}
	} else {
		/*
		 * Do word containing only most significant bits of aa
		 * that shift across a word boundary because of sb.
		 */
		if (i > aadeg / BIG_CHUNK_SIZE + sw) {
			resultvalp[i] =
			    aavalp[i - sw - 1] >> (BIG_CHUNK_SIZE - sb);
			--i;
		}
		/* Now the big middle part */
		for (; i > sw; --i) {
			resultvalp[i] = (aavalp[i - sw] << sb) | /* hi bits */
			    (aavalp[i - sw -1] >> (BIG_CHUNK_SIZE - sb));
								/* low bits */
		}
		/* i == sw; now do last word from aa */
		resultvalp[i] = aavalp[i - sw] << sb;
		--i;
	}
	/* now zero any tail part.  i == sw - 1 */
	for (; i >= 0; --i) {
		resultvalp[i] = 0;
	}
	/* reduce if requested */
	if (nn) {
		ncp_big_poly_reduce(result, nn);
	}

cleanexit:
	return (rv);
}

/*
 * Sets target to 2^k (optionally) mod nn.  k==-1 sets target to 0.
 * If nn is null, the mod is not done.  The length will be at least
 * minlen bits.  If nn == NULL, you can cheat and interpret the result
 * as 2^k.
 */
int
ncp_big_poly_bit_k(BIGNUM *target, int k, BIGNUM *nn, unsigned int minlen)
{
	int		rv;  /* set by CHECK */

	if (k >= 0) {
		if (minlen < k) {
			minlen = k;
		}
	} else {
		if (minlen < 1) {
			minlen = 1;
		}
	}
	minlen = minlen / BIG_CHUNK_SIZE + 1;
	/* minlen now in words */
	CHECK(ncp_big_extend(target, minlen + 1));
	bzero(target->value, (minlen + 1) * sizeof (BIG_CHUNK_TYPE));
	target->len = minlen;
	target->sign = 1;
	if (k >= 0) {
		target->value[k / BIG_CHUNK_SIZE] =
		    1ULL << (k % BIG_CHUNK_SIZE);
		if (nn) {
			ncp_big_poly_reduce(target, nn);
		}
	}
cleanexit:
	return (rv);
}


#ifdef DEBUG
/* debugging */
static int
validate_poly_inverse(BIGNUM *aa, BIGNUM *bb, BIGNUM *nn)
{
	int		nndegree = ncp_big_MSB(nn);
	BIG_CHUNK_TYPE	nprime = ncp_big_poly_nprime(nn, nndegree);
	int		shiftamt = (nndegree + (BIG_CHUNK_SIZE - 1)) &
	    ~(BIG_CHUNK_SIZE - 1);
	BIGNUM		prod;
	int		rv;

	CHECK(ncp_big_init(&prod, 2 * (nndegree / BIG_CHUNK_SIZE + 2)));
	CHECK(ncp_big_poly_mont_mul(&prod, aa, bb, nn, nprime));
	CHECK(ncp_big_poly_left_shift(&prod, &prod, shiftamt, nn, nprime));

	if (!ncp_big_equals_one(&prod)) {
		rv = BIG_TEST_FAILED;
	}

cleanexit:
	ncp_big_finish(&prod);
	return (rv);
}
#endif


/*
 * NOT montgomery encoded.  result = aa^(-1) mod nn.  Aliasing allowed.
 */
BIG_ERR_CODE
ncp_big_poly_inverse(BIGNUM *result, BIGNUM *aa, BIGNUM *nn)
{
	int		rv;
	int		j;
	int		nndegree = ncp_big_MSB(nn);
	int		s = VALWORDS(nndegree);
	BIGNUM		G1, G2, U, V, T;
	BIGNUM		*g1 = &G1;
	BIGNUM		*g2 = &G2;
	BIGNUM		*uu = &U; /* u not allowed by gcc */
	BIGNUM		*v = &V;
	BIGNUM		*tmp;
	int		degu;
	int		degv;
	int		loopcatcher = 2*nndegree + 4;

	if (ncp_big_is_zero(aa)) {
		return (BIG_DIV_BY_0);
	}

	G1.malloced = 0;
	G2.malloced = 0;
	U.malloced = 0;
	V.malloced = 0;
	T.malloced = 0;
	CHECK(ncp_big_init(&G1, s + 1));
	CHECK(ncp_big_init(&G2, s + 1));
	CHECK(ncp_big_init(&U, s + 1));
	CHECK(ncp_big_init(&V, s + 1));
	CHECK(ncp_big_init(&T, s + 1));


	/*
	 * Based on Hankerson, Meneses, and Vanstone, Guide to
	 * Elliptic Curve Cryptography, Algorithm 2.48.  f is nn.
	 */
	CHECK(ncp_big_copy(uu, aa)); /* u := a */
	CHECK(ncp_big_copy(v, nn)); /* v := f */
	CHECK(ncp_big_set_int(g1, 1)); /* v := 1 */
	CHECK(ncp_big_set_int(g2, 0)); /* g2 := 0 */

	while ((degu = ncp_big_MSB(uu)) != 0) {  /* while u != 1 */
		/*
		 * Many things can go wrong.  For example, if GCD(aa,
		 * nn) != 1, we might loop forever.  Rather than try
		 * to think of all these and catch them, we add this
		 * test as a safety net.
		 */
		if (--loopcatcher < 0) {
			return (BIG_INVALID_ARGS);
		}
		degv = ncp_big_MSB(v);
		/* two assignments save half the add steps in u += z^j */
		uu->len = degu / BIG_CHUNK_SIZE + 1;
		v->len = degv / BIG_CHUNK_SIZE + 1;
		j = degu - degv;
		if (j < 0) {
			tmp = uu;
			uu = v;
			v = tmp;
			tmp = g1;
			g1 = g2;
			g2 = tmp;
			j = -j;
		}
		CHECK(ncp_big_poly_left_shift(&T, v, j, NULL, 0));
		CHECK(ncp_big_poly_add(uu, uu, &T)); /* u := u + z^j v */
		CHECK(ncp_big_poly_left_shift(&T, g2, j, NULL, 0));
		CHECK(ncp_big_poly_add(g1, g1, &T)); /* g1 = g1 + z^j g2 */
	}

#ifdef DEBUG
	rv = validate_poly_inverse(g1, aa, nn);  /* debugging XXX */
	ASSERT(rv == 0);
#endif

	CHECK(ncp_big_copy(result, g1));

cleanexit:

	ncp_big_finish(&G1);
	ncp_big_finish(&G2);
	ncp_big_finish(&U);
	ncp_big_finish(&V);
	ncp_big_finish(&T);

	return (rv);
}


BIG_ERR_CODE
ncp_big_inverse(BIGNUM *result,
    BIGNUM *aa, BIGNUM *nn, int poly, int mont,
    BIGNUM *R2, BIG_CHUNK_TYPE nprime)
{
	int		rv;

	if (poly) {
		CHECK(ncp_big_poly_inverse(result, aa, nn));
		if (mont) {
			CHECK(
			    ncp_big_poly_mont_mul(result,
			    result, R2, nn, nprime));
		}
	} else {
		CHECK(ncp_big_numeric_inverse(result, aa, nn));
		if (mont) {
			CHECK(
			    ncp_big_mont_mul_extend(result,
			    result, R2, nn, nprime));
		}
	}

cleanexit:
	return (rv);
}


/*
 * given polynomial style modulus nn and int nndegree equal to degree
 * of nn, returns nprime.  Based on Koc and Acar, "Montgomery
 * Multiplication in GF(2^k)".
 */
/*ARGSUSED*/
BIG_CHUNK_TYPE
ncp_big_poly_nprime(BIGNUM *nn, int nndegree)
{
	int i;
	BIG_CHUNK_TYPE nnLSW = nn->value[0];
	BIG_CHUNK_TYPE nprimev = 1;
	BIG_CHUNK_TYPE t;

	for (i = 2; i < BIG_CHUNK_SIZE; ++i) {
		t = xormpy(nnLSW, nprimev) & (((BIG_CHUNK_TYPE)1 << i) - 1);
		if (t != 1) {
			nprimev |= ((BIG_CHUNK_TYPE)1 << (i - 1));
		}
	}
	t = xormpy(nnLSW, nprimev);
	if (t != 1) {
		nprimev |= ((BIG_CHUNK_TYPE)1 << (BIG_CHUNK_SIZE - 1));
	}

	return (nprimev);
}
