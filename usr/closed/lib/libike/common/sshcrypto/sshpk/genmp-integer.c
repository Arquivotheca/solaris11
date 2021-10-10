/*

  Authors: Antti Huima   <huima@ssh.fi>
           Mika Kojo     <mkojo@ssh.fi>
           Patrick Irwin <irwin@ssh.fi>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
  All rights reserved.

  Created: Sun  Nov 25 19:10:02 2001 [irwin]

  This file contains generic functions to generate random
  multiple-precision integers.

*/

#include "sshincludes.h"
#include "sshmp.h"
#include "sshgenmp.h"
#include "sshcrypt.h"
#include "sshcrypt_i.h"
#include "sshrandom_i.h"

#define SSH_DEBUG_MODULE "SshGenMPInteger"

/* Generate a random integer (using the cryptographically strong
   random number generator). */

void ssh_mprz_random_integer(SshMPInteger ret, unsigned int bits)
{
  unsigned int i, bytes;
  unsigned char *buf;

  ssh_mprz_set_ui(ret, 0);

  bytes = (bits + 7) / 8;
  if ((buf = ssh_malloc(bytes)) == NULL)
    {
      ssh_mprz_makenan(ret, SSH_MP_NAN_ENOMEM);
      return;
    }

  for (i = 0; i < bytes; i++)
    buf[i] = ssh_random_object_get_byte();

  ssh_mprz_set_buf(ret, buf, bytes);
  ssh_free(buf);

  /* Cut unneeded bits off */
  ssh_mprz_mod_2exp(ret, ret, bits);
}


/* Get random number mod 'modulo' */

/* Random number with some sense in getting only a small number of
   bits. This will avoid most of the extra bits. However, we could
   do it in many other ways too. Like we could distribute the random bits
   in reasonably random fashion around the available size. This would
   ensure that cryptographical use would be slightly safer. */
void ssh_mprz_mod_random_entropy(SshMPInteger op, SshMPIntegerConst modulo,
                               unsigned int bits)
{
  ssh_mprz_random_integer(op, bits);
  ssh_mprz_mod(op, op, modulo);
}

/* Just plain _modular_ random number generation. */
void ssh_mprz_mod_random(SshMPInteger op, SshMPIntegerConst modulo)
{
  unsigned int bits;

  bits = ssh_mprz_bit_size(modulo);
  ssh_mprz_random_integer(op, bits);
  ssh_mprz_mod(op, op, modulo);
}

static SshCryptoStatus ssh_mp_fips186_mod_random_value(SshMPInteger *ret,
                                                       unsigned int m,
                                                       SshMPIntegerConst q,
                                                       const char *name)
{
  SshRandomObject random;
  SshMPInteger op;
  SshCryptoStatus status;
  unsigned char noise[20], *buf;
  unsigned int i, j, size;

  /* Allocate the FIPS approved DSS specific random number generator. */
  if ((status = ssh_random_object_allocate(name, &random)) != SSH_CRYPTO_OK)
    return status;

  if ((status = ssh_random_set_dsa_prime_param(random, q)) != SSH_CRYPTO_OK)
    {
      ssh_random_object_free(random);
      return status;
    }

  /* We seed the newly allocated random number generator with 160 bits of
     output from the default random number generator (which is assumed
     to have been previously seeded from a high entropy source). Note that
     we do not change the default rng. */
  for (i = 0; i < sizeof(noise); i++)
    noise[i] = (unsigned char) ssh_random_object_get_byte();

  /* Add the noise to the DSS specific random number generator. */
  if ((status = ssh_random_object_add_entropy(random, noise, sizeof(noise)))
      != SSH_CRYPTO_OK)
    {
      ssh_random_object_free(random);
      return status;
    }

  memset(noise, 0, sizeof(noise));

  size = ssh_mprz_byte_size(q);
  SSH_ASSERT(size);

  /* The return integer 'ret' is computed mod q and so has the same number
     of bytes as q. Allocate a buffer of the size and fill it with output
     from the DSS specific random number generator. */
  if (!(buf = ssh_malloc(size)))
    {
      ssh_random_object_free(random);
      return SSH_CRYPTO_NO_MEMORY;
    }

  for (j = 0; j < m; j++)
    {
      op = *(ret + j);

      if ((status = ssh_random_object_get_bytes(random, buf, size)) 
          != SSH_CRYPTO_OK)
        {
          ssh_random_object_free(random);
          return status;
        }

      /* Convert the buffer back to integer form and compute it modulo q. */
      ssh_mprz_set_buf(op, buf, size);
      ssh_mprz_mod(op, op, q);
    }

  /* Free the DSS specific rng. */
  ssh_random_object_free(random);
  ssh_free(buf);
  return SSH_CRYPTO_OK;
}

/* Use the random number generator described in Appendix 3.1 of
   FIPS 186-2 to generate 'm' values of DSS private value x. 'q' is the
   group subprime. 'x' points to an array of 'm' previously allocated
   (and initialized) mp integers. */
SshCryptoStatus
ssh_mp_fips186_mod_random_private_value_array(SshMPInteger *x,
                                              unsigned int m,
                                              SshMPIntegerConst q)
{
  return ssh_mp_fips186_mod_random_value(x, m, q, "ansi-dsa-key-gen");
}

SshCryptoStatus
ssh_mp_fips186_mod_random_private_value(SshMPInteger x,
                                        SshMPIntegerConst q)
{
  return ssh_mp_fips186_mod_random_value(&x, 1, q, "ansi-dsa-key-gen");
}


/* Use the random number generator described in Appendix 3.2 of
   FIPS 186-2 to generate 'm' values the secret random value input
   to a DSS signature, 'k'. 'q' is the group subprime and 'k' points
   to an array of 'm' previously allocated (and initialized) mp integers. */
SshCryptoStatus
ssh_mp_fips186_mod_random_signature_value_array(SshMPInteger *k,
                                                unsigned int m,
                                                SshMPIntegerConst q)
{
  return ssh_mp_fips186_mod_random_value(k, m, q, "ansi-dsa-sig-gen");
}

SshCryptoStatus
ssh_mp_fips186_mod_random_signature_value(SshMPInteger k,
                                          SshMPIntegerConst q)
{
  return ssh_mp_fips186_mod_random_value(&k, 1, q, "ansi-dsa-sig-gen");
}
