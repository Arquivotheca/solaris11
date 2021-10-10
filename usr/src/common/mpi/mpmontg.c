/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is the Netscape security libraries.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 2000
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Sheueling Chang Shantz <sheueling.chang@sun.com>,
 *   Stephen Fung <stephen.fung@sun.com>, and
 *   Douglas Stebila <douglas@stebila.ca> of Sun Laboratories.
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */
/*
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 *
 * Oracle elects to use this software under the MPL license.
 */


/* $Id: mpmontg.c,v 1.20 2006/08/29 02:41:38 nelson%bolyard.com Exp $ */

/* This file implements moduluar exponentiation using Montgomery's
 * method for modular reduction.  This file implements the method
 * described as "Improvement 1" in the paper "A Cryptogrpahic Library for
 * the Motorola DSP56000" by Stephen R. Dusse' and Burton S. Kaliski Jr.
 * published in "Advances in Cryptology: Proceedings of EUROCRYPT '90"
 * "Lecture Notes in Computer Science" volume 473, 1991, pg 230-244,
 * published by Springer Verlag.
 */

#define MP_USING_CACHE_SAFE_MOD_EXP 1 
#ifndef _KERNEL
#include <string.h>
#include <stddef.h> /* ptrdiff_t */
#endif
#include "mpi-priv.h"
#include "mplogic.h"
#include "mpprime.h"
#ifdef YF_MONTMUL
#include "bignum.h"
#endif

/* if MP_CHAR_STORE_SLOW is defined, we  */
/* need to know endianness of this platform. */
#ifdef MP_CHAR_STORE_SLOW
#if !defined(MP_IS_BIG_ENDIAN) && !defined(MP_IS_LITTLE_ENDIAN)
#error "You must define MP_IS_BIG_ENDIAN or MP_IS_LITTLE_ENDIAN\n" \
       "  if you define MP_CHAR_STORE_SLOW."
#endif
#endif

#ifndef STATIC
#define STATIC
#endif

#define MAX_ODD_INTS    32   /* 2 ** (WINDOW_BITS - 1) */

#ifndef _KERNEL
#if defined(_WIN32_WCE)
#define ABORT  res = MP_UNDEF; goto CLEANUP
#else
#define ABORT abort()
#endif
#else /* !_KERNEL */
#define ABORT  res = MP_UNDEF; goto CLEANUP
#endif /* _KERNEL */

/* computes T = REDC(T), 2^b == R */
mp_err s_mp_redc(mp_int *T, mp_mont_modulus *mmm)
{
  mp_err res;
  mp_size i;

  i = MP_USED(T) + MP_USED(&mmm->N) + 2;
  MP_CHECKOK( s_mp_pad(T, i) );
  for (i = 0; i < MP_USED(&mmm->N); ++i ) {
    mp_digit m_i = MP_DIGIT(T, i) * mmm->n0prime;
    /* T += N * m_i * (MP_RADIX ** i); */
    MP_CHECKOK( s_mp_mul_d_add_offset(&mmm->N, m_i, T, i) );
  }
  s_mp_clamp(T);

  /* T /= R */
  s_mp_div_2d(T, mmm->b); 

  if ((res = s_mp_cmp(T, &mmm->N)) >= 0) {
    /* T = T - N */
    MP_CHECKOK( s_mp_sub(T, &mmm->N) );
#ifdef DEBUG
    if ((res = mp_cmp(T, &mmm->N)) >= 0) {
      res = MP_UNDEF;
      goto CLEANUP;
    }
#endif
  }
  res = MP_OKAY;
CLEANUP:
  return res;
}


#ifdef YF_MONTMUL
void
mpi_to_bignum64(const mp_int *a, BIGNUM *a_bn)
{
	int		i, useda;
	uint64_t	*d;
	mp_digit	*ad;

	useda = MP_USED(a);
	ad = MP_DIGITS(a);
	d = &((a_bn->value)[0]);

#ifdef MP_USE_UINT_DIGIT
	a_bn->len = (useda + 1) / 2;
	for (i = 0; i < useda / 2; i++) {
		d[i] = ad[2 * i] + (((uint64_t)ad[2 * i + 1]) << 32);
	}

	if (useda & 1) {
		d[useda / 2 ] = ad[useda - 1];
	}

	for (i = (useda + 1) / 2; i < a_bn->len; i++) {
		d[i] = 0;
	}
#else
	a_bn->len = useda;
	for (i = 0; i < useda; i++) {
		d[i] = ad[i];
	}
#endif /* MP_USE_UINT_DIGIT */
}

void
bignum64_to_mpi(BIGNUM *a_bn, mp_int *a)
{
	int		i, useda;
	uint64_t	*d;
	mp_digit	*ad;

	ad = MP_DIGITS(a);
	d = &((a_bn->value)[0]);

#ifdef MP_USE_UINT_DIGIT
	useda = USED(a) = a_bn->len * 2;
	for (i = 0; i < useda / 2; i++) {
		ad[2 * i] = d[i] & 0xffffffff;
		ad[2 * i + 1] = d[i] >> 32;
	}
	if (ad[useda - 1] == 0) {
		USED(a)--;
	}
#else
	useda = USED(a) = a_bn->len;
	for (i = 0; i < useda; i++) {
		ad[i] = d[i];
	}
#endif /* MP_USE_UINT_DIGIT */
}

#endif /* YF_MONTMUL */

#if !defined(MP_ASSEMBLY_MUL_MONT) && !defined(MP_MONT_USE_MP_MUL)

mp_err s_mp_mul_mont(const mp_int *a, const mp_int *b, mp_int *c, 
	           mp_mont_modulus *mmm)
{
  mp_digit *pb;
  mp_digit m_i;
  mp_err   res;
  mp_size  ib;
  mp_size  useda, usedb;

  ARGCHK(a != NULL && b != NULL && c != NULL, MP_BADARG);

#ifdef YF_MONTMUL
	{
#if BIG_CHUNK_SIZE != 64
#error this only works with BIG_CHUNK_SIZE == 64
#endif
	BIGNUM	a_bn, b_bn, c_bn, n_bn;
	BIG_CHUNK_TYPE	a_bnvalue[BIGTMPSIZE];
	BIG_CHUNK_TYPE	b_bnvalue[BIGTMPSIZE];
	BIG_CHUNK_TYPE	c_bnvalue[BIGTMPSIZE];
	BIG_CHUNK_TYPE	n_bnvalue[BIGTMPSIZE];
	BIG_CHUNK_TYPE	n0;
	int nsize;

	/* to make big_finish a no-op if an error happens */
	a_bn.malloced = b_bn.malloced = c_bn.malloced = n_bn.malloced = 0;

	MP_USED(c) = 1;
	MP_DIGIT(c, 0) = 0;
	if ((res = s_mp_pad(c, ib)) != MP_OKAY) {
		goto CLEANUP;
	}

#ifdef MP_USE_UINT_DIGIT
	nsize = (MP_USED(&(mmm->N) + 1)) / 2;
	n0 = big_n0(((uint64_t)(MP_DIGITS(&(mmm->N))[1]) << 32) +
	    MP_DIGITS(&(mmm->N))[0]);
#else
	nsize = MP_USED(&(mmm->N));
	n0 = mmm->n0prime;
#endif /* MP_USE_UINT_DIGIT */
	res = MP_MEM;
	if (big_init1(&a_bn, nsize, a_bnvalue, arraysize(a_bnvalue))
	    != BIG_OK) {
		goto CLEANUP_bn;
	}
	mpi_to_bignum64(a, &a_bn);

	if (a != b) {
		if (big_init1(&b_bn, nsize, b_bnvalue, arraysize(b_bnvalue))
		    != BIG_OK) {
			goto CLEANUP_bn;
		}
		mpi_to_bignum64(b, &b_bn);
	}

	if (big_init1(&n_bn, nsize, n_bnvalue, arraysize(n_bnvalue))
	    != BIG_OK) {
		goto CLEANUP_bn;
	}
	mpi_to_bignum64(&(mmm->N), &n_bn);

	if (big_init1(&c_bn, 2 * nsize + 1, c_bnvalue, arraysize(c_bnvalue))
	    != BIG_OK) {
		goto CLEANUP_bn;
	}
	c_bn.len = 1;
	c_bn.value[0] = 0;

	if (a == b) {
		big_mont_mul(&c_bn, &a_bn, &a_bn, &n_bn, n0);
	} else {
		big_mont_mul(&c_bn, &a_bn, &b_bn, &n_bn, n0);
	}

	bignum64_to_mpi(&c_bn, c);

	res = MP_OKAY;

CLEANUP_bn:
	big_finish(&n_bn);
	big_finish(&c_bn);
	big_finish(&b_bn);
	big_finish(&a_bn);

	return (res);
	}
#else /* YF_MONTMUL */


  if (MP_USED(a) < MP_USED(b)) {
    const mp_int *xch = b;	/* switch a and b, to do fewer outer loops */
    b = a;
    a = xch;
  }

  MP_USED(c) = 1; MP_DIGIT(c, 0) = 0;
  ib = MP_USED(a) + MP_MAX(MP_USED(b), MP_USED(&mmm->N)) + 2;
  if((res = s_mp_pad(c, ib)) != MP_OKAY)
    goto CLEANUP;

  useda = MP_USED(a);
  pb = MP_DIGITS(b);
  s_mpv_mul_d(MP_DIGITS(a), useda, *pb++, MP_DIGITS(c));
  s_mp_setz(MP_DIGITS(c) + useda + 1, ib - (useda + 1));
  m_i = MP_DIGIT(c, 0) * mmm->n0prime;
  s_mp_mul_d_add_offset(&mmm->N, m_i, c, 0);

  /* Outer loop:  Digits of b */
  usedb = MP_USED(b);
  for (ib = 1; ib < usedb; ib++) {
    mp_digit b_i    = *pb++;

    /* Inner product:  Digits of a */
    if (b_i)
      s_mpv_mul_d_add_prop(MP_DIGITS(a), useda, b_i, MP_DIGITS(c) + ib);
    m_i = MP_DIGIT(c, ib) * mmm->n0prime;
    s_mp_mul_d_add_offset(&mmm->N, m_i, c, ib);
  }
  if (usedb < MP_USED(&mmm->N)) {
    for (usedb = MP_USED(&mmm->N); ib < usedb; ++ib ) {
      m_i = MP_DIGIT(c, ib) * mmm->n0prime;
      s_mp_mul_d_add_offset(&mmm->N, m_i, c, ib);
    }
  }
  s_mp_clamp(c);
  s_mp_div_2d(c, mmm->b); 
  if (s_mp_cmp(c, &mmm->N) >= 0) {
    MP_CHECKOK( s_mp_sub(c, &mmm->N) );
  }

  res = MP_OKAY;

#endif /* YF_MONTMUL */

CLEANUP:
  return res;
}
#endif /*  !defined(MP_ASSEMBLY_MUL_MONT) && !defined(MP_MONT_USE_MP_MUL) */
