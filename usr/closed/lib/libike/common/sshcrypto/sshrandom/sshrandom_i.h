/*

  sshrandom.h

  Author: Santeri Paavolainen <santtu@ssh.com>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
  All rights reserved.

*/

#ifndef SSHRANDOM_I_H
#define SSHRANDOM_I_H

#ifndef KERNEL
#include "sshmp.h"
#endif /* !KERNEL */

/* Definition structure for random number generators */
typedef struct SshRandomDefSRec {
  const char *name;





  SshUInt32 unused_sshuint32;


  /* Initialize */
  SshCryptoStatus (*init)(void **context_ret);

  /* Uninitialize */
  void (*uninit)(void *context);

  /* Add noise (can be NULL) */
  SshCryptoStatus (*add_noise)(void *context,
                               const unsigned char *buf, size_t buflen);

  /* Get bufferlen bytes of random data, write to buffer */
  SshCryptoStatus (*get_bytes)(void *context,
                               unsigned char *buf, size_t buflen);

  /* Zeroize all sensitive state */
  void (*zeroize)(void *context);

} *SshRandomDef, SshRandomDefStruct;










typedef struct SshRandomObjectRec
{
  SSH_CRYPTO_OBJECT_HEADER

  const SshRandomDefStruct *ops;
  void *context;















} *SshRandomObject, SshRandomObjectStruct;

/* This function can be used to query the `pool' RNG its current
   entropy size. This works only for that RNG. Takes handle argument
   (event if internal). */
SshCryptoStatus
ssh_random_pool_get_length(SshRandom handle, size_t *size_ret);

#ifndef KERNEL
/* Used only for the FIPS DSA random numbers generators. Takes handle
   argument (even if internal) */
SshCryptoStatus
ssh_random_set_dsa_prime_param(SshRandomObject random, SshMPIntegerConst q);
#endif /* !KERNEL */

const char *
ssh_random_object_name(SshRandomObject random);

/* We need access to object-level creation for KAT etc. tests */
SshCryptoStatus
ssh_random_object_allocate(const char *name, SshRandomObject *random_ret);

void
ssh_random_object_free(SshRandomObject random);






SshCryptoStatus
ssh_random_object_get_bytes(SshRandomObject random,
                            unsigned char *buf, size_t buflen);

SshCryptoStatus
ssh_random_object_add_entropy(SshRandomObject random,
                              const unsigned char *buf, size_t buflen);






#endif /* SSHRANDOM_I_H */
