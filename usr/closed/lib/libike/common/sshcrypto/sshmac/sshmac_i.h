/*
  File: sshmac_i.h

  Description:
        Internal MAC definitions.

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
                  All rights reserved.
*/

#ifndef SSHMAC_I_H
#define SSHMAC_I_H

#include "sshhash_i.h"

/* Definition structure for hash based mac functions. */
typedef struct SshMacDefRec
{
  const char *name;





  SshUInt32 unused_sshuint32;


  size_t digest_length;

  /* Some mac functions need to allocate space of variable length, this
     will indicate it. */
  Boolean allocate_key;

  /* Indicate which hash function to use. This should be generic enough
     for all our needs. But if not, then add more options. */
  const SshHashDefStruct *hash_def;

  size_t (*ctxsize)(const SshHashDefStruct *hash_def);

  void (*init)(void *context, const unsigned char *key, size_t keylen,
               const SshHashDefStruct *hash_def);
  void (*start)(void *context);
  void (*update)(void *context, const unsigned char *buf, size_t len);
  void (*final)(void *context, unsigned char *digest);
  void (*mac_of_buffer)(void *context, const unsigned char *buf,
                        size_t len, unsigned char *digest);
  void (*zeroize)(void *context);
} *SshMacDef, SshMacDefStruct;

/* This structure contains relevant cipher information to use
   for various forms of cipher based macs e.g. cbc-mac, xcbc-mac. */
typedef struct SshCipherMacBaseDefRec
{
  size_t block_length;
  size_t (*ctxsize)(void);
  SshCryptoStatus (*init)(void *context, const unsigned char *key,
                          size_t keylen, Boolean for_encryption);

  void (*cbcmac)(void *context, const unsigned char *src, size_t len,
                  unsigned char *iv_arg);

} *SshCipherMacBaseDef, SshCipherMacBaseDefStruct;


/* Definition structure for mac functions based on a cipher. */
typedef struct SshCipherMacDefRec
{
  const char *name;





  SshUInt32 unused_sshuint32;


  size_t digest_length;

  struct {
    size_t min_key_length;
    size_t default_key_length;
    size_t max_key_length;
  } key_lengths;

  /* Indicate which cipher function to use. */
  const SshCipherMacBaseDefStruct *cipher_def;
  size_t (*ctxsize)(const SshCipherMacBaseDefStruct *cipher_def);

  SshCryptoStatus (*init)(void *context,
                          const unsigned char *key, size_t keylen,
                          const SshCipherMacBaseDefStruct *cipher_def);
  void (*start)(void *context);
  void (*update)(void *context, const unsigned char *buf, size_t len);
  void (*final)(void *context, unsigned char *digest);
  void (*zeroize)(void *context);
} *SshCipherMacDef, SshCipherMacDefStruct;


typedef struct SshMacObjectRec *SshMacObject;

/* We need access to object-level functions for KAT tests */
SshCryptoStatus
ssh_mac_object_allocate(const char *type,
                        const unsigned char *key, size_t keylen,
                        SshMacObject *mac);

/* Free the mac. */
void
ssh_mac_object_free(SshMacObject mac);

void
ssh_mac_object_reset(SshMacObject mac);

void
ssh_mac_object_update(SshMacObject mac, const unsigned char *data, size_t len);

SshCryptoStatus
ssh_mac_object_final(SshMacObject mac, unsigned char *digest);






#endif /* SSHMAC_I_H */
