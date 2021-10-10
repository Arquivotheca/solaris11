/*

  Authors: Antti Huima   <huima@ssh.fi>
           Mika Kojo     <mkojo@ssh.fi>
           Patrick Irwin <irwin@ssh.fi>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
  All rights reserved.

  Created: Mon Nov  26 09:32:16  2001 [irwin]

  This file contains generic functions for generating
  multiple-precision primes.

  */

#include "sshincludes.h"
#include "sshmp.h"
#include "sshgenmp.h"
#include "sshcrypt.h"
#include "sshcrypt_i.h"
#include "sshrandom_i.h"
#include "libmonitor.h"
#include "sshgetput.h"

#define SSH_GENMP_MAX_PRIME        16000
#define SSH_GENMP_MAX_SIEVE_MEMORY 8192

#define SSH_DEBUG_MODULE "SshGenMPPrime"


/* Similar to Miller Rabin in the math library. This version however uses 
   the strong cryptographic random number generator when generating random 
   integers for the Miller Rabin test. */
static Boolean ssh_mprz_crypto_miller_rabin(SshMPIntegerConst op, 
                                            unsigned int limit)
{
  SshMPMontIntIdealStruct ideal;
  SshMPMontIntModStruct modint;
  SshMPIntegerStruct q, a, b, op_1;
  Boolean rv = FALSE;
  SshUInt32 t, k, e;

  if (ssh_mprz_isnan(op))
    return FALSE;

  /* Assume primes are larger than 1. */
  if (ssh_mprz_cmp_ui(op, 1) <= 0)
    return FALSE;

  /* 'op' should be odd, so we can use Montgomery ideals. */
  if (!ssh_mpmzm_init_ideal(&ideal, op))
    return FALSE;

  ssh_mpmzm_init(&modint, &ideal);
  ssh_mprz_init(&q);
  ssh_mprz_init(&op_1);
  ssh_mprz_init(&a);
  ssh_mprz_init(&b);

  ssh_mprz_set(&q, op);
  ssh_mprz_sub_ui(&q, &q, 1);
  ssh_mprz_set(&op_1, &q);
  t = 0;
  while ((ssh_mprz_get_ui(&q) & 0x1) == 0)
    {
      ssh_mprz_div_2exp(&q, &q, 1);
      if (ssh_mprz_isnan(&q))
        {
          rv = 0;
          goto failure;
        }

      t++;
    }

  rv = TRUE;
  /* To the witness tests. */
  for (; limit; limit--)
    {
      /* We want to be fast, thus we use 0 < a < 2^(SSH_WORD_BITS).
         Some purists would insist that 'k' should be selected in more
         uniform way, however, this is accordingly to Cohen a reasonable
         approach. */
      do
        {
          k = (((SshUInt32)ssh_random_object_get_byte()) << 24) | 
              (((SshUInt32)ssh_random_object_get_byte()) << 16) |
              (((SshUInt32)ssh_random_object_get_byte()) << 8) |
              ((SshUInt32)ssh_random_object_get_byte());

          /* In the rare case that op is small, we need to ensure that
             k is not a multiple of op. */
          while (ssh_mprz_cmp_ui(op, k) <= 0)
            k = k/2;
        }
      while (k == 0);



      /* Exponentiate. */
      ssh_mprz_powm_ui_g(&b, k, &q, op);
      if (ssh_mprz_cmp_ui(&b, 1) != 0)
        {
          e = 0;
          while (ssh_mprz_cmp_ui(&b, 1) != 0 &&
                 ssh_mprz_cmp(&b, &op_1) != 0 &&
                 e <= t - 1)
            {
              ssh_mpmzm_set_mprz(&modint, &b);
              ssh_mpmzm_square(&modint, &modint);
              ssh_mprz_set_mpmzm(&b, &modint);
              e++;
            }

          if (ssh_mprz_cmp(&b, &op_1) != 0)
            {
              rv = FALSE;
              break;
            }
        }
    }

 failure:
  ssh_mpmzm_clear(&modint);
  ssh_mpmzm_clear_ideal(&ideal);
  ssh_mprz_clear(&q);
  ssh_mprz_clear(&a);
  ssh_mprz_clear(&b);
  ssh_mprz_clear(&op_1);

  return rv;
}

/* Following routine decides if the given value is very likely a prime
   or not. Returns TRUE if 'op' is a probable prime, FALSE otherwise. */
Boolean ssh_mprz_is_strong_probable_prime(SshMPIntegerConst op, 
                                          unsigned int limit)
{
  SshMPIntegerStruct temp;

  /* The small prime test, this one should be performed for speed. */
  static const SshWord
    very_small_primes[10] = { 2, 3, 5, 7, 11, 13, 17, 19, 23, 29 };
  static const SshWord ideal = 3234846615UL;
  SshWord i, res;

  /* Assuming we mostly are looking for primes */
  if (ssh_mprz_isnan(op))
    return FALSE;

  /* Check for trivial cases. */
  if (ssh_mprz_cmp_ui(op, 2) < 0)
    return FALSE;

  if ((ssh_mprz_get_ui(op) & 0x1) == 0)
    {
      /* Perhaps the input is equal to 2. */
      if (ssh_mprz_cmp_ui(op, 2) == 0)
        return TRUE;
      return FALSE;
    }

  /* The small 'ideal' test. */
  res = ssh_mprz_mod_ui(op, ideal);
  for (i = 1; i < 10; i++)
    {
      /* Explicitly testing the base. */
      if ((res % very_small_primes[i]) == 0)
        {
          /* Perhaps the input is equal to the prime element? */
          if (ssh_mprz_cmp_ui(op, very_small_primes[i]) == 0)
            return TRUE;
          /* Was not and hence it must be composite. */
          return FALSE;
        }
    }

  /* Test first with Fermat's test with witness 2. */
  ssh_mprz_init(&temp);
  ssh_mprz_powm_ui_g(&temp, 2, op, op);
  if (ssh_mprz_cmp_ui(&temp, 2) != 0)
    {
      ssh_mprz_clear(&temp);
      return FALSE;
    }
  ssh_mprz_clear(&temp);

  /* Finally try Miller-Rabin test. */
  if (ssh_mprz_crypto_miller_rabin(op, limit))
    return TRUE;
  return FALSE;
}


/* Generate traditional prime. */

/* XXX: this may fail. In failure, the ret is set to NaN. */
void ssh_mprz_random_prime(SshMPInteger ret, unsigned int bits)
{
  SshMPIntegerStruct start, aux;
  SshSieveStruct sieve;
  unsigned int num_primes, p, i;
  SshWord *moduli = NULL, *prime_table = NULL;
  SshWord difference;

  /* Progress monitoring. */
  unsigned int progress_counter = 0;

  /* Initialize the prime search. */
  ssh_mprz_init(&start);
  ssh_mprz_init(&aux);

  if (ssh_mprz_isnan(&start) || ssh_mprz_isnan(&aux))
    {
    failure_nosieve:
      ssh_mprz_clear(&start);
      ssh_mprz_clear(&aux);
      ssh_mprz_makenan(ret, SSH_MP_NAN_ENOMEM);
      return;
    }

  if (bits < 16)
    {
      SshWord temp;

      /* Check from the prime sieve. */
      if (!ssh_sieve_allocate_ui(&sieve, (1 << bits), (1 << bits)))
        goto failure_nosieve;

      /* Do not choose 2. */
      num_primes = ssh_sieve_prime_count(&sieve) - 1;

      ssh_mprz_random_integer(&aux, bits);
      if (ssh_mprz_isnan(&aux))
        goto failure;

      temp = ssh_mprz_get_ui(&aux) % num_primes;

      for (p = 2; p; p = ssh_sieve_next_prime(p, &sieve), temp--)
        if (temp == 0)
          {
            ssh_mprz_set_ui(ret, p);
            break;
          }
      if (temp != 0)
        ssh_fatal("ssh_mprz_random_prime: could not find small prime.");

      ssh_mprz_clear(&start);
      ssh_mprz_clear(&aux);
      return;
    }

  /* Generate the prime sieve, this takes very little time. */
  if (!ssh_sieve_allocate_ui(&sieve, SSH_GENMP_MAX_PRIME,
                             SSH_GENMP_MAX_SIEVE_MEMORY))
    goto failure_nosieve;

  /* Don't count 2. */
  num_primes = ssh_sieve_prime_count(&sieve)-1;

  /* Generate a simply indexed prime table. */
  if ((prime_table = ssh_malloc(num_primes * sizeof(SshWord))) == NULL)
    goto failure;
  /* Allocate moduli table. */
  
  if ((moduli = ssh_malloc(num_primes * sizeof(SshWord))) == NULL)
    goto failure;

  for (p = 2, i = 0; p; p = ssh_sieve_next_prime(p, &sieve), i++)
    prime_table[i] = p;

 retry:

  /* Pick a random integer of the appropriate size. */
  ssh_mprz_random_integer(&start, bits);
  if (ssh_mprz_isnan(&start))
    goto failure;

  /* Set the highest bit. */
  ssh_mprz_set_bit(&start, bits - 1);
  /* Set the lowest bit to make it odd. */
  ssh_mprz_set_bit(&start, 0);

  /* Initialize moduli of the small primes with respect to the given
     random number. */
  for (i = 0; i < num_primes; i++)
    moduli[i] = ssh_mprz_mod_ui(&start, prime_table[i]);

  /* Look for numbers that are not evenly divisible by any of the small
     primes. */
  for (difference = 0; ; difference += 2)
    {
      unsigned int i;

      if (difference > 0x70000000)
        {
          /* Might never happen... */
          goto retry;
        }

      /* Check if it is a multiple of any small prime.  Note that this
         updates the moduli into negative values as difference grows. */
      for (i = 1; i < num_primes; i++)
        {
          while (moduli[i] + difference >= prime_table[i])
            moduli[i] -= prime_table[i];
          if (moduli[i] + difference == 0)
            break;
        }
      if (i < num_primes)
        continue; /* Multiple of a known prime. */

      /* Progress information. */
      ssh_crypto_progress_monitor(SSH_CRYPTO_PRIME_SEARCH,
                                  ++progress_counter);

      /* Compute the number in question. */
      ssh_mprz_add_ui(ret, &start, difference);

      if (ssh_mprz_isnan(ret))
        goto failure;

      /* Perform Miller-Rabin strong pseudo primality tests */
      if (ssh_mprz_is_strong_probable_prime(ret, 50))
        break;
    }

  /* Found a (probable) prime.  It is in ret. */

  /* Sanity check: does it still have the high bit set (we might have
     wrapped around)? */
  ssh_mprz_div_2exp(&aux, ret, bits - 1);
  if (ssh_mprz_get_ui(&aux) != 1)
    {
      goto retry;
    }

  /* Free the small prime moduli; they are no longer needed. Also free
     start, aux and sieve. */

  ssh_free(moduli);
  ssh_free(prime_table);

  ssh_mprz_clear(&start);
  ssh_mprz_clear(&aux);
  ssh_sieve_free(&sieve);

  /* Return value already set in ret. */
  return;

 failure:
  ssh_sieve_free(&sieve);

  ssh_free(moduli);
  ssh_free(prime_table);

  ssh_mprz_clear(&start);
  ssh_mprz_clear(&aux);
  ssh_mprz_makenan(ret, SSH_MP_NAN_ENOMEM);
}

/* Generate a random prime within the [min, max] interval. We observe
   that the process can just choose a random number modulo (max - min)
   and then start from there. If it goes beyond max-1 then it
   cycles. */

void ssh_mprz_random_prime_within_interval(SshMPInteger ret,
                                         SshMPInteger min, SshMPInteger max)
{
  SshMPIntegerStruct pprime, temp, aux;
  SshSieveStruct sieve;
  SshWord *moduli = NULL, *prime_table = NULL;
  SshWord difference, max_difference, num_primes, p;
  unsigned int i, bits;

  /* Progress monitoring. */
  unsigned int progress_counter = 0;

  /* Verify the interval. */
  if (ssh_mprz_cmp(min, max) >= 0)
    ssh_fatal("ssh_mprz_random_prime_within_interval: interval invalid.");

  /* Initialize temps. */
  ssh_mprz_init(&pprime);
  ssh_mprz_init(&temp);
  ssh_mprz_init(&aux);

  /* Allocate a sieve. */
  if (!ssh_sieve_allocate_ui(&sieve, SSH_GENMP_MAX_PRIME,
                             SSH_GENMP_MAX_SIEVE_MEMORY))
    {
      ssh_mprz_clear(&pprime);
      ssh_mprz_clear(&aux);
      ssh_mprz_clear(&temp);
      ssh_mprz_makenan(ret, SSH_MP_NAN_ENOMEM);
      return;
    }

  /* Don't count 2. */
  num_primes = ssh_sieve_prime_count(&sieve) - 1;

  /* Make a table of the primes. */
  if ((prime_table = ssh_malloc(num_primes * sizeof(SshWord))) == NULL)
    goto failure;

  /* Allocate moduli table. */
  if ((moduli = ssh_malloc(num_primes * sizeof(SshWord))) == NULL)
    {
      goto failure;
    }

  for (p = 2, i = 0; p; p = ssh_sieve_next_prime(p, &sieve), i++)
    prime_table[i] = p;
  
retry:

  /* Generate the random number within the interval. */
  ssh_mprz_sub(&temp, max, min);
  bits = ssh_mprz_get_size(&temp, 2);

  /* Generate suitable random number (some additional bits for perhaps
     more uniform distribution, these really shouldn't matter). */
  ssh_mprz_random_integer(&aux, bits + 10);
  /* Compute. */
  ssh_mprz_mod(&aux, &aux, &temp);
  ssh_mprz_add(&pprime, &aux, min);

  /* Fix it as odd. */
  ssh_mprz_set_bit(&pprime, 0);

  /* Compute the max difference. */
  ssh_mprz_sub(&aux, max, &pprime);
  if (ssh_mprz_cmp_ui(&aux, 0) < 0)
    goto retry;

  /* Get it. */
  max_difference = ssh_mprz_get_ui(&aux);

  if (ssh_mprz_isnan(&pprime) || ssh_mprz_isnan(&aux))
    goto failure;

  /* Now we need to set up the moduli table. */
  for (i = 0; i < num_primes; i++)
    moduli[i] = ssh_mprz_mod_ui(&pprime, prime_table[i]);

  /* Look for numbers that are not evenly divisible by any of the small
     primes. */
  for (difference = 0; ; difference += 2)
    {
      unsigned int i;

      if (difference > max_difference)
        /* Although we could just wrap around, we currently choose to
           just start from the scratch again. */
        goto retry;

      /* Check if it is a multiple of any small prime.  Note that this
         updates the moduli into negative values as difference grows. */
      for (i = 1; i < num_primes; i++)
        {
          while (moduli[i] + difference >= prime_table[i])
            moduli[i] -= prime_table[i];
          if (moduli[i] + difference == 0)
            break;
        }
      if (i < num_primes)
        continue; /* Multiple of a known prime. */

      /* Progress information. */
      ssh_crypto_progress_monitor(SSH_CRYPTO_PRIME_SEARCH,
                                  ++progress_counter);

      /* Compute the number in question. */
      ssh_mprz_add_ui(ret, &pprime, difference);

      /* Perform Miller-Rabin strong pseudo primality tests */
      if (ssh_mprz_isnan(ret) || ssh_mprz_is_strong_probable_prime(ret, 50))
        break;
    }

  /* Found a (probable) prime.  It is in ret. */

  /* Sanity check, are we in the interval. */
  if (!ssh_mprz_isnan(ret) &&
      (ssh_mprz_cmp(ret, min) <= 0 || ssh_mprz_cmp(ret, max) >= 0))
    goto retry;

  /* Free the small prime moduli; they are no longer needed. */
  ssh_sieve_free(&sieve);

  ssh_free(moduli);
  ssh_free(prime_table);

  ssh_mprz_clear(&pprime);
  ssh_mprz_clear(&aux);
  ssh_mprz_clear(&temp);
  /* Return value already set in ret. */
  return;

 failure:
  ssh_sieve_free(&sieve);

  ssh_free(moduli);
  ssh_free(prime_table);

  ssh_mprz_clear(&pprime);
  ssh_mprz_clear(&aux);
  ssh_mprz_clear(&temp);
  ssh_mprz_makenan(ret, SSH_MP_NAN_ENOMEM);
}



/* Generate a strong random prime. That is, p = q * c + 1, where p and
   q are prime and c > 1.

   Here we use the idea that given random 2^n-1 < x < 2^n, we can
   compute y = x (mod 2q), and then p = x - y + 1 + 2tq. Given this
   method the probability that we get values that are not in the
   correct range is reasonably small. */

void ssh_mprz_random_strong_prime(SshMPInteger prime,
                                SshMPInteger order,
                                int prime_bits, int order_bits)
{
  SshMPIntegerStruct aux, aux2, u;
  SshSieveStruct sieve;
  SshWord *table_q, *table_u, *prime_table, p;
  unsigned long i, j, table_count, upto;
  Boolean flag;

  unsigned int progress_counter = 0;

  /* Check for bugs. */
  if (prime_bits < order_bits)
    ssh_fatal("ssh_mprz_random_strong_prime: "
              "requested prime less than the group order!");

  /* Keep the running in place. */
  if (prime_bits - order_bits - 1 > 24)
    upto = 1 << 24;
  else
    upto = 1 << (prime_bits - order_bits - 1);

  ssh_mprz_init(&aux);
  ssh_mprz_init(&aux2);
  ssh_mprz_init(&u);

  /* There seems to be no real reason to generate this as a strong
     prime. */
  ssh_mprz_random_prime(order, order_bits);

  /* Generate the sieve again (it was already generated in the random
     prime generation code), but it shouldn't be too slow. */
  ssh_sieve_allocate_ui(&sieve, SSH_GENMP_MAX_PRIME,
                        SSH_GENMP_MAX_SIEVE_MEMORY);
  /* Compute the number of primes. */
  table_count = ssh_sieve_prime_count(&sieve) - 1;
  
  /* Generate a suitable table of primes. */
  if ((prime_table = ssh_malloc(table_count * sizeof(SshWord))) == NULL)
    return;
  
  if ((table_q = ssh_malloc(table_count * sizeof(SshWord) * 2)) == NULL)
    {
      ssh_free(prime_table);
      return;
    }
  
  for (p = 2, i = 0; p; p = ssh_sieve_next_prime(p, &sieve), i++)
    prime_table[i] = p;

  /* Reduce group order. Remember the factor 2. */

  table_u = table_q + table_count;
  for (i = 0; i < table_count; i++)
    {
      table_q[i] =
        (ssh_mprz_mod_ui(order, prime_table[i]) * 2) % prime_table[i];
    }

  /* In case we don't find one quickly enough. */
retry:

  /* Generate a random integer large enough. */
  ssh_mprz_random_integer(&u, prime_bits);

  /* Set the highest bit. */
  ssh_mprz_set_bit(&u, prime_bits - 1);

  /* Compute the initial value for the prime. */
  ssh_mprz_set(&aux, order);
  ssh_mprz_mul_2exp(&aux, &aux, 1);
  ssh_mprz_mod(&aux2, &u, &aux);
  ssh_mprz_sub(&u, &u, &aux2);
  ssh_mprz_add_ui(&u, &u, 1);

  /* Now check whether the value is still large enough. */
  if (ssh_mprz_get_size(&u, 2) <= prime_bits - 1)
    goto retry;

  /* Now compute the residues of the 'probable prime'. */
  for (j = 0; j < table_count; j++)
    table_u[j] = ssh_mprz_mod_ui(&u, prime_table[j]);

  /* Set the 2*q for  later. */
  ssh_mprz_mul_2exp(&aux2, order, 1);

  /* Loop through until a prime is found. */
  for (i = 0; i < upto; i++)
    {
      unsigned long cur_p, value;

      flag = TRUE;
      for (j = 1; j < table_count; j++)
        {
          cur_p = prime_table[j];
          value = table_u[j];

          /* Check if the result seems to indicate divisible value. */
          if (value >= cur_p)
            value -= cur_p;
          if (value == 0)
            flag = FALSE;
          /* For the next round compute. */
          table_u[j] = value + table_q[j];
        }

      if (flag != TRUE)
        continue;

      /* Acknowledge application that again one possibly good value was
         found. */
      ssh_crypto_progress_monitor(SSH_CRYPTO_PRIME_SEARCH,
                                  ++progress_counter);

      /* Compute the proposed prime. */
      ssh_mprz_set(prime, &u);
      ssh_mprz_mul_ui(&aux, &aux2, i);
      ssh_mprz_add(prime, prime, &aux);

      /* Check that the size of the prime is within range. */
      if (ssh_mprz_get_size(prime, 2) > prime_bits)
        goto retry;

      /* Miller-Rabin */
      if (ssh_mprz_is_strong_probable_prime(prime, 50))
        break;
    }

  if (i >= upto)
    goto retry;

  ssh_free(table_q);
  ssh_free(prime_table);

  ssh_sieve_free(&sieve);

  /* Free temporary memory. */
  ssh_mprz_clear(&aux);
  ssh_mprz_clear(&aux2);
  ssh_mprz_clear(&u);
}

/* Basic modular enhancements. Due the nature of extended euclids algorithm
   it sometimes returns integers that are negative. For our cases positive
   results are better. */

int ssh_mprz_mod_invert(SshMPInteger op_dest, SshMPIntegerConst op_src,
                      SshMPIntegerConst modulo)
{
  int status;

  status = ssh_mprz_invert(op_dest, op_src, modulo);

  if (ssh_mprz_cmp_ui(op_dest, 0) < 0)
    ssh_mprz_add(op_dest, op_dest, modulo);

  return status;
}


/* From p and q, compute a generator h (of the multiplicative group mod p)
   of order p-1, and g of order p-1/q with g = h ^ {(p-1)/q} mod p. Returns
   TRUE if g and h can be generated in this manner and FALSE otherwise
   (if p and q are both prime with q dividing p-1, then it is guaranteed
   to return TRUE).  */
static Boolean ssh_mp_random_generator_internal(SshMPInteger g,
                                                SshMPInteger h,
                                                SshMPIntegerConst q,
                                                SshMPIntegerConst p)
{
  SshMPIntegerStruct r, s, tmp;
  Boolean rv = FALSE;
  unsigned int bits;

  ssh_mprz_init(&r);
  ssh_mprz_init(&s);
  ssh_mprz_init(&tmp);

  /* Set r = p-1 and s = (p-1)/q */
  ssh_mprz_sub_ui(&r, p, 1);
  ssh_mprz_div(&s, &r, q);

  /* Verify that q | (p - 1 ) */
  ssh_mprz_mod(&tmp, &r, q);

  if (ssh_mprz_cmp_ui(&tmp, 0) != 0)
    goto fail;

  bits = ssh_mprz_get_size(p, 2);

  /* Search for h such that h^(p-1) mod p != 1 mod p, and then compute
     g = h^(p-1/q) mod p. To begin, we check if h = 2 is a generator. */
  ssh_mprz_set_ui(h, 2);
  while (1)
    {
      ssh_mprz_mod(h, h, p);
      ssh_mprz_powm(g, h, &s, p);

      if (ssh_mprz_cmp_ui(g, 1) != 0)
        break;

      /* If 2 is not a generator, look for a random generator. */
      ssh_mprz_random_integer(h, bits);
    }

  /* Verify that g has order q. */
  ssh_mprz_powm(&tmp, g, q, p);

  if (ssh_mprz_cmp_ui(&tmp, 1) != 0)
    goto fail;

  /* Have now successfully generated h and g. */
  rv = TRUE;

 fail:
  ssh_mprz_clear(&r);
  ssh_mprz_clear(&s);
  ssh_mprz_clear(&tmp);

  return rv;
}

/* Find a random generator of order 'order' modulo 'modulo'. */
Boolean ssh_mprz_random_generator(SshMPInteger g,
                                  SshMPInteger order,
                                  SshMPInteger modulo)
{
  SshMPIntegerStruct aux;
  Boolean rv;

  ssh_mprz_init(&aux);
  rv = ssh_mp_random_generator_internal(g, &aux, order, modulo);
  ssh_mprz_clear(&aux);
  return rv;
}

static Boolean generate_subprime_from_seed(SshMPInteger q,
                                           const unsigned char *seed,
                                           size_t seed_len);

/* Hashes one buffer with selected hash type and returns the
   digest. This can return error codes from either ssh_hash_allocate
   or ssh_hash_final. (This function is essentially the same as the
   one in lib/sshcryptoaux/hashbuf.c. Copied here to avoid addition to
   public API (FIPS).) */
static SshCryptoStatus
genmp_hash_of_buffer(const char *type,
                     const void *buf, size_t len,
                     unsigned char *digest)
{
  SshHash hash;
  SshCryptoStatus status;

  if ((status = ssh_hash_allocate(type, &hash)) != SSH_CRYPTO_OK)
    return status;

  ssh_hash_update(hash, buf, len);
  status = ssh_hash_final(hash, digest);
  ssh_hash_free(hash);

  return status;
}

/* Generate primes p and q according to the method described in
   Appendix 2.2 of FIPS 186-2. The input is p_bits and q_bits, the
   bit sizes of the primes to be generated. Output the primes p, q.
   Returns TRUE on success and FALSE on failure. This function does
   not output the SEED and counter used for generating the primes. */

/* We always use a 160 bit seed value.*/
#define SEED_LEN       20
SshCryptoStatus
ssh_mp_fips186_random_strong_prime(SshMPInteger p,
                                   SshMPInteger q,
                                   unsigned int p_bits,
                                   unsigned int q_bits)
{
  unsigned char digest[20];
  unsigned char seed[SEED_LEN], temp[SEED_LEN];
  SshMPIntegerStruct aux, c, v, w;
  SshCryptoStatus status;
  unsigned int i, counter, offset, n;
  /* Progress monitoring. */
  unsigned int progress_counter = 0;







  /* The subprime q must have 160 bits. */
  if (q_bits != 160)
    return SSH_CRYPTO_KEY_INVALID;

  /* In FIPS mode the following restrictions on the DSA key sizes apply. */









  ssh_mprz_init(&aux);
  ssh_mprz_init(&c);
  ssh_mprz_init(&v);
  ssh_mprz_init(&w);

  /* Loop until a prime value for q is obtained. */
 search_for_q:

  while (1)
    {
      /* Generate a random 160 bit seed value. */
      for (i = 0; i < SEED_LEN; i++)
        seed[i] = ssh_random_object_get_byte();

      if (generate_subprime_from_seed(q, seed, SEED_LEN))
        break;

      /* Check if an error occured in 'generate_subprime_from_seed' */
      if (ssh_mprz_isnan(q))
        {
          memset(seed, 0, SEED_LEN);
          ssh_mprz_clear(&aux);
          ssh_mprz_clear(&c);
          ssh_mprz_clear(&v);
          ssh_mprz_clear(&w);
          return SSH_CRYPTO_OPERATION_FAILED;
        }
    }

  /* Have now found the prime q, search for a valid prime p. */
  counter = 0;
  offset = 2;

  n = (p_bits - 1)/ 160;

  while (counter < 4096)
    {
      ssh_mprz_set_ui(&w, 0);

      ssh_mprz_set_buf(&aux, seed, SEED_LEN);
      ssh_mprz_add_ui(&aux, &aux, offset);

      for (i = 0; i <= n; i++)
        {
          ssh_mprz_mod_2exp(&aux, &aux, SEED_LEN * 8);

          /* Convert to buffer. */
          ssh_mprz_get_buf(temp, SEED_LEN, &aux);

          /* hash */
          if ((status = genmp_hash_of_buffer("sha1", temp, SEED_LEN, digest))
              != SSH_CRYPTO_OK)
            {
              memset(seed, 0, SEED_LEN);
              memset(temp, 0, SEED_LEN);
              ssh_mprz_clear(&aux);
              ssh_mprz_clear(&c);
              ssh_mprz_clear(&v);
              ssh_mprz_clear(&w);
              return status;
            }

          ssh_mprz_set_buf(&v, digest, 20);

          /* On the last iteration we need to cut off the extra bits
             to ensure 'p' will have 'p_bits' bits. */
          if (i == n)
            ssh_mprz_mod_2exp(&v, &v, (p_bits - 1) % 160);

          ssh_mprz_mul_2exp(&v, &v, 160 * i);

          ssh_mprz_add(&w, &w, &v);
          ssh_mprz_add_ui(&aux, &aux, 1);
        }

      /* set the highest bit of w */
      ssh_mprz_set_bit(&w, p_bits - 1);

      /* Compute c = W mod 2q */
      ssh_mprz_mul_ui(&aux, q, 2);
      ssh_mprz_mod(&c, &w, &aux);

      /* Compute p = W - (c - 1)  */
      ssh_mprz_sub(p, &w, &c);
      ssh_mprz_add_ui(p, p, 1);

      ssh_mprz_set_ui(&aux, 1);
      ssh_mprz_mul_2exp(&aux, &aux, p_bits - 1);

      ssh_crypto_progress_monitor(SSH_CRYPTO_PRIME_SEARCH,
                                  ++progress_counter);

      /* Have we generated a valid prime p? */
      if ((ssh_mprz_cmp(p, &aux) >= 0) && 
          ssh_mprz_is_strong_probable_prime(p, 50))
        break;

      /* Increment counter and try again. */
      counter++;
      offset += (n + 1);

      /* If counter reaches 4096 we must generate a new subprime q and
         start over. */
      if (counter == 4096)
        goto search_for_q;
    }

  SSH_DEBUG(4, ("The value of the counter is %d.", counter));

  memset(seed, 0, SEED_LEN);
  memset(temp, 0, SEED_LEN);
  ssh_mprz_clear(&aux);
  ssh_mprz_clear(&c);
  ssh_mprz_clear(&v);
  ssh_mprz_clear(&w);

  return SSH_CRYPTO_OK;
}

/* Generates q from the seed, returns TRUE if q is a probable prime,
   else returns FALSE. Sets q to NaN if an error occurs. */
static Boolean generate_subprime_from_seed(SshMPInteger q,
                                           const unsigned char *seed,
                                           size_t seed_len)
{
  unsigned char digest1[20], digest2[20];
  unsigned char seed_plus_1[SEED_LEN];
  SshMPIntegerStruct aux;
  int i;

  SSH_ASSERT(seed_len == SEED_LEN);

  ssh_mprz_init(&aux);

  /* Add 1 to the seed (considered as an integer msb first) modulo
     2 ^ (8 * seed_len). */
  ssh_mprz_set_buf(&aux, seed, seed_len);
  ssh_mprz_add_ui(&aux, &aux, 1);
  ssh_mprz_mod_2exp(&aux, &aux, seed_len * 8);

  /* Reconvert to buffer. */
  ssh_mprz_get_buf(seed_plus_1, seed_len, &aux);

  /* Do the hashing. */
  if ((genmp_hash_of_buffer("sha1", seed, seed_len, digest1)
       != SSH_CRYPTO_OK)
      || (genmp_hash_of_buffer("sha1", seed_plus_1, seed_len, digest2)
          != SSH_CRYPTO_OK))
    {
      memset(seed_plus_1, 0, seed_len);
      ssh_mprz_clear(&aux);

      /* Make q a NaN to signal an error. */
      ssh_mprz_makenan(q, SSH_MP_NAN_ENOMEM);
      return FALSE;
    }

  for (i = 0; i < 20; i++)
    digest1[i] ^= digest2[i];

  /* Convert the xored hash digests to an integer. */
  ssh_mprz_set_buf(q, digest1, 20);

  /* Set the lowest and highest order bits of the candidate for q. */
  ssh_mprz_set_bit(q, 0);
  ssh_mprz_set_bit(q, 159);

  /* Clear temp variables. */
  memset(seed_plus_1, 0, seed_len);
  ssh_mprz_clear(&aux);

  /* Perform Miller-Rabin strong pseudo primality test. The probability
     of a non-prime passing this test is at most 2^(-100). */
  return ssh_mprz_is_strong_probable_prime(q, 50);
}

/* genmp-prime.c */
