/*
  File: ansi-x962rand.c

  Author: Patrick Irwin <irwin@ssh.fi>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
  All rights reserved.


  This file implements three FIPS approved pseudo random number generators.

  1. The FIPS approved pseudo random number generator described in
  Annex A.4 of ANSI X9.62 "Public Key Cryptography for the Financial
  Services Industry".

  2. The FIPS approved pseudo random number generator described in
  Appendix 3.1 of FIPS 186-2.

  3. The FIPS approved pseudo random number generator described in
  Appendix 3.2 of FIPS 186-2.

  In all three cases, we use the maximum possible number of bits for the
  seed key, b = 512 bits, and the one way function G(t,c) is constructed
  via SHA-1 (as opposed to the other alternative, DES).

*/

#include "sshincludes.h"
#include "sshgetput.h"
#include "sshcrypt.h"
#include "sshmp.h"
#include "sshcrypt_i.h"
#include "sshrandom_i.h"
#include "sshhash_i.h"
#include "sha.h"

#define SSH_DEBUG_MODULE "AnsiX962Rand"

#define SEED_KEY_SIZE   64
#define SHA_DIGEST_SIZE 20

/* The random state context. */
typedef struct SshRandomAnsiStateRec {
  SshMPIntegerStruct xseed, xkey, xval, output, q;

  unsigned char   xval_buf[SEED_KEY_SIZE];
  unsigned char output_buf[SHA_DIGEST_SIZE];

  size_t next_available_byte;
} *SshRandomAnsiState;

static SshCryptoStatus
ssh_random_ansi_add_entropy(void *context,
                                 const unsigned char *buf, size_t buflen)
{
  SshRandomAnsiState state = (SshRandomAnsiState) context;

  /* Convert the input buffer to an multiple precision integer, xseed. */
  ssh_mprz_set_buf_lsb_first(&state->xseed, buf, buflen);

  /*Add xseed to xkey modulo 2^b (= 2^512). */
  ssh_mprz_add(&state->xseed, &state->xseed, &state->xkey);
  ssh_mprz_mod_2exp(&state->xseed, &state->xseed, 8 * SEED_KEY_SIZE);

  if (ssh_mprz_isnan(&state->xseed))
    return SSH_CRYPTO_OPERATION_FAILED;

  return SSH_CRYPTO_OK;
}

static void ssh_random_ansi_uninit(void *context)
{
  SshRandomAnsiState state = (SshRandomAnsiState) context;

  /* Clear the multiple precision integers. */
  ssh_mprz_clear(&state->xkey);
  ssh_mprz_clear(&state->xval);
  ssh_mprz_clear(&state->xseed);
  ssh_mprz_clear(&state->output);
  ssh_mprz_clear(&state->q);

  /* Zeroize and free. */
  memset(state, 0, sizeof(*state));
  ssh_crypto_free_i(state);
}

static SshCryptoStatus
ssh_random_ansi_init(void **context_ret)
{
  SshRandomAnsiState state;

  /* The output of SHA-1 is 160 bits and we assume the seed key size
     to be 512 bits. */
  SSH_ASSERT(SHA_DIGEST_SIZE == 20);
  SSH_ASSERT(SEED_KEY_SIZE == 64);

  if (!(state = ssh_crypto_calloc_i(1, sizeof(*state))))
    return SSH_CRYPTO_NO_MEMORY;

  /* Initialize the multiple precision integers. */
  ssh_mprz_init_set_ui(&state->xkey, 0);
  ssh_mprz_init_set_ui(&state->xval, 0);
  ssh_mprz_init_set_ui(&state->xseed, 0);
  ssh_mprz_init_set_ui(&state->output, 0);
  ssh_mprz_init_set_ui(&state->q, 0);

  if (ssh_mprz_isnan(&state->xkey) || ssh_mprz_isnan(&state->xseed)
      || ssh_mprz_isnan(&state->xval) || ssh_mprz_isnan(&state->output)
      || ssh_mprz_isnan(&state->q))
    {
      ssh_random_ansi_uninit(state);
      return SSH_CRYPTO_OPERATION_FAILED;
    }

  state->next_available_byte = SHA_DIGEST_SIZE;
  *context_ret = state;
  return SSH_CRYPTO_OK;
}

/* Returns a random byte. If 'key_gen' is TRUE use the standard SHA-1 internal
   function as the hash 'engine', otherwise use SHA-1 with the initialization
   vectors permuted (as described in Appendix 3.2 of FIPS 186-2) as the hash
   'engine'. */
static SshCryptoStatus
ssh_random_ansi_get_byte(Boolean key_gen,
                         SshRandomAnsiState state,
                         unsigned char *byte_ret)
{
  if (state->next_available_byte >= SHA_DIGEST_SIZE)
    {
      SshUInt32 buf[5];

      ssh_mprz_set(&state->xval, &state->xseed);

      /* Linearize xkey to a buffer. */
      if (ssh_mprz_get_buf_lsb_first(state->xval_buf,
                                     SEED_KEY_SIZE, &state->xval) == 0)
        return SSH_CRYPTO_OPERATION_FAILED;

      if (key_gen)
        {
          /* Apply the sha transform. */
          ssh_sha_transform(buf, state->xval_buf);
        }
      else
        {
          /* Apply the permuted sha transform. */
          ssh_sha_transform(buf, state->xval_buf);
        }

      SSH_PUT_32BIT(state->output_buf,      buf[0]);
      SSH_PUT_32BIT(state->output_buf + 4,  buf[1]);
      SSH_PUT_32BIT(state->output_buf + 8,  buf[2]);
      SSH_PUT_32BIT(state->output_buf + 12, buf[3]);
      SSH_PUT_32BIT(state->output_buf + 16, buf[4]);

      memset(buf, 0, sizeof(buf));

   /* Convert the 'output_buf' buffer back to integer form. */
      ssh_mprz_set_buf_lsb_first(&state->output, state->output_buf,
                                 sizeof(state->output_buf));

      /* If 'q' is nonzero, modulo 'output' by 'q'. */
      if (ssh_mprz_cmp_ui(&state->q, 0))
        ssh_mprz_mod(&state->output, &state->output, &state->q);

      /* Update the integer 'xkey' by adding to it 1 plus 'output' and
         then taking its modulus mod 2^b. */
      ssh_mprz_add_ui(&state->xkey, &state->xkey, 1);
      ssh_mprz_add(&state->xkey, &state->xkey, &state->output);
      ssh_mprz_mod_2exp(&state->xkey, &state->xkey, 8 * SEED_KEY_SIZE);

      /* Update xseed to the new value of xkey. */
      ssh_mprz_set(&state->xseed, &state->xkey);

      if (ssh_mprz_isnan(&state->xseed))
        return SSH_CRYPTO_OPERATION_FAILED;

      state->next_available_byte = 0;
    }

  *byte_ret = state->output_buf[state->next_available_byte++];

  return SSH_CRYPTO_OK;
}

static SshCryptoStatus
ssh_random_ansi_dsa_key_gen_get_bytes(void *context,
                                      unsigned char *buf, size_t buflen)
{
  unsigned int i;
  SshRandomAnsiState state = (SshRandomAnsiState) context;
  SshCryptoStatus status;

  for (i = 0; i < buflen; i++)
    {
      status = ssh_random_ansi_get_byte(TRUE, state, &buf[i]);

      if (status != SSH_CRYPTO_OK)
        return status;
    }

  return SSH_CRYPTO_OK;
}


static SshCryptoStatus
ssh_random_ansi_dsa_sig_gen_get_bytes(void *context,
                                      unsigned char *buf, size_t buflen)
{
  unsigned int i;
  SshRandomAnsiState state = (SshRandomAnsiState) context;
  SshCryptoStatus status;

  for (i = 0; i < buflen; i++)
    {
      status = ssh_random_ansi_get_byte(FALSE, state, &buf[i]);

      if (status != SSH_CRYPTO_OK)
        return status;
    }

  return SSH_CRYPTO_OK;
}

/* The ANSI X9.62 random number generator is identical to the random
   number generator in Appendix 3.1 of FIPS 186-2 when the prime 'q' is
   set to 1. Since q in the SshAnsiState is initialized to 1 and it cannot
   be set to a different value for the ssh_random_ansi_x962 random number
   generator, we can use the ssh_random_ansi_get_byte function. */
static SshCryptoStatus
ssh_random_ansi_x962_get_bytes(void *context,
                               unsigned char *buf, size_t buflen)
{
  unsigned int i;
  SshRandomAnsiState state = (SshRandomAnsiState) context;
  SshCryptoStatus status;

  for (i = 0; i < buflen; i++)
    {
      status = ssh_random_ansi_get_byte(TRUE, state, &buf[i]);

      if (status != SSH_CRYPTO_OK)
        return status;
    }

  return SSH_CRYPTO_OK;
}



const SshRandomDefStruct ssh_random_ansi_x962 = {
  "ansi-x9.62",



  0,
  ssh_random_ansi_init,
  ssh_random_ansi_uninit,
  ssh_random_ansi_add_entropy,
  ssh_random_ansi_x962_get_bytes
};

const SshRandomDefStruct ssh_random_ansi_dsa_key_gen = {
  "ansi-dsa-key-gen",



  0,
  ssh_random_ansi_init,
  ssh_random_ansi_uninit,
  ssh_random_ansi_add_entropy,
  ssh_random_ansi_dsa_key_gen_get_bytes
};

const SshRandomDefStruct ssh_random_ansi_dsa_sig_gen = {
  "ansi-dsa-sig-gen",



  0,
  ssh_random_ansi_init,
  ssh_random_ansi_uninit,
  ssh_random_ansi_add_entropy,
  ssh_random_ansi_dsa_sig_gen_get_bytes
};


/* Internal (but not static) function */
SshCryptoStatus
ssh_random_set_dsa_prime_param(SshRandomObject random, SshMPIntegerConst q)
{
  SshRandomAnsiState state;

  if (random->ops != &ssh_random_ansi_dsa_key_gen &&
      random->ops != &ssh_random_ansi_dsa_sig_gen)
    return SSH_CRYPTO_UNSUPPORTED;

  state = (SshRandomAnsiState) random->context;

  /* We do not try to verify that 'q' is a valid DSA prime, just check
     it is not a NaN. */
  ssh_mprz_set(&state->q, q);

  if (ssh_mprz_isnan(&state->q))
    return SSH_CRYPTO_OPERATION_FAILED;

  return SSH_CRYPTO_OK;
}
