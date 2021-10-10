/*

  Author: Antti Huima <huima@ssh.fi>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
  All rights reserved.

  Created: Mon May  6 00:13:23 1996 [huima]

  Functions for generating primes.

*/

#ifndef GENMP_H
#define GENMP_H

#include "sshcrypt.h"

/* Generates a random integer of the desired number of bits. */

void ssh_mprz_random_integer(SshMPInteger ret, unsigned int bits);

/* Makes and returns a random pseudo prime of the desired number of bits.
   Note that the random number generator must be initialized properly
   before using this.

   The generated prime will have the highest bit set, and will have
   the two lowest bits set.

   Primality is tested with Miller-Rabin test, ret thus having
   probability about 1 - 2^(-50) (or more) of being a true prime.
   */
void ssh_mprz_random_prime(SshMPInteger ret,unsigned int bits);

/* Generate a random prime within the [min, max] interval. We observe that
   the process can just choose a random number modulo (max - min) and
   then start from there. If it goes beyond max-1 then it cycles.
   */
void ssh_mprz_random_prime_within_interval(SshMPInteger ret,
                                         SshMPInteger min, SshMPInteger max);

/* Generate a strong random prime, where 'prime' = 'order' * u + 1. Similar
   to the P1363 method but takes less time and is almost as 'strong'. The
   P1363 strong primes satisfy some other facts, but in practice these
   primes seem as good with discrete log based cryptosystems. */

void ssh_mprz_random_strong_prime(SshMPInteger prime,
                                SshMPInteger order,
                                int prime_bits, int order_bits);

/* Modular invert with positive results. */

int ssh_mprz_mod_invert(SshMPInteger op_dest, SshMPIntegerConst op_src,
                      SshMPIntegerConst modulo);

/* Random number with special modulus */

void ssh_mprz_mod_random(SshMPInteger op, SshMPIntegerConst modulo);

/* Generate a random integer with entropy at most _bits_ bits. The atmost,
   means that the actual number of bits depends whether the modulus is
   smaller in bits than the _bits_.  */
void ssh_mprz_mod_random_entropy(SshMPInteger op, SshMPIntegerConst modulo,
                               unsigned int bits);


/* Find a random generator of order 'order' modulo 'modulo'. */

Boolean ssh_mprz_random_generator(SshMPInteger g,
                                  SshMPInteger order, SshMPInteger modulo);


/* Generate primes p and q according to the method described in
   Appendix 2.2 of FIPS 186-2. The input is p_bits and q_bits, the
   bit sizes of the primes to be generated. Output the primes p, q.
   Returns TRUE on success and FALSE on failure. This function does
   not output the SEED and counter used for generating the primes.  */
SshCryptoStatus ssh_mp_fips186_random_strong_prime(SshMPInteger p,
                                                   SshMPInteger q,
                                                   unsigned int p_bits,
                                                   unsigned int q_bits);

/* Use the random number generator described in Appendix 3.1 of
   FIPS 186-2 to generate the DSS private key 'x'. 'q' is the
   group subprime. */
SshCryptoStatus
ssh_mp_fips186_mod_random_private_value(SshMPInteger x,
                                        SshMPIntegerConst q);

/* Use the random number generator described in Appendix 3.1 of
   FIPS 186-2 to generate the secret random value input to a DSS
   signature, 'k'. 'q' is the group subprime. */
SshCryptoStatus
ssh_mp_fips186_mod_random_signature_value(SshMPInteger k,
                                          SshMPIntegerConst q);



#ifdef SSH_GENMP_UNUSED

/* Check whether op_src is of order op_ord mod modulo */
int ssh_mprz_is_order(SshMPIntegerConst op_ord, SshMPIntegerConst op_src,
                    SshMPIntegerConst modulo);

/* Similar to the ssh_mprz_random_prime, except that the 'strong' pseudo
   prime is returned. Uses method described in P1363 working draft.

   'big_bits' tells how many bits are in the 'prime' and 'small_bits' how
   many bits in the 'div'. Note that 'prime' - 1 = 0 mod 'div'.

   This method generates good primes for RSA or other factorization based
   cryptosystems. For discrete log based systems this isn't exactly
   neccessary.
*/

void ssh_mprz_strong_p1363_random_prime(SshMPInteger prime, SshMPInteger div,
                                      int big_bits, int small_bits);


/* Check the MOV condition, for elliptic curves */

Boolean ssh_mprz_mov_condition(SshMPIntegerConst b,
                             SshMPIntegerConst q, SshMPIntegerConst r);
/* Lucas functions */

void ssh_mprz_reduced_lucas(SshMPInteger op_dest, SshMPIntegerConst op_e,
                          SshMPIntegerConst op_p, SshMPIntegerConst op_n);

void ssh_mprz_lucas(SshMPInteger op_dest, SshMPIntegerConst op_src1,
                  SshMPIntegerConst op_src2,
                  SshMPIntegerConst k, SshMPIntegerConst modulo);

/* Modular square root */

int ssh_mprz_mod_sqrt(SshMPInteger op_dest, SshMPIntegerConst op_src,
                    SshMPIntegerConst modulo);

#endif /* SSH_GENMP_UNUSED */

#endif /* GENMP_H */
