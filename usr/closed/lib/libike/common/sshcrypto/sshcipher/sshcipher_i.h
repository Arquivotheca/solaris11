/*
  File: sshcipher_i.h

  Description:
        Cipher internal definitions.

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
                  All rights reserved.
*/


#ifndef SSHCIPHER_I_H
#define SSHCIPHER_I_H


/* Definition structure for cipher functions. */
typedef struct SshCipherDefRec
{
  const char *name;





  SshUInt32 unused_sshuint32;


  /* Block length is 1 for stream ciphers. */
  size_t block_length;

  /* Key length is 0 if supports any length. XXX this is adequate for
     most uses but possibly not suitable always. Might be better to
     have some fixed sized versions of the cipher, rather than
     variable length key version. */
  struct {
    size_t min_key_len;
    size_t def_key_len;
    size_t max_key_len;
  } key_lengths;

  size_t (*ctxsize)(void);

  /* Basic initialization without explicit key checks. */
  SshCryptoStatus (*init)(void *context, const unsigned char *key,
                          size_t keylen, Boolean for_encryption);

  /* Initialization with key checks. */
  SshCryptoStatus (*init_with_check)(void *context, const unsigned char *key,
                                     size_t keylen, Boolean for_encryption);

  /* Encryption and decryption. If src == dest, then this works
     inplace. */
  void (*transform)(void *context, unsigned char *dest,
                    const unsigned char *src, size_t len,
                    unsigned char *iv);

  /* Zeroization of all key and sensitive material (transform is never
     called after this). This can be NULL if the allocated context is
     all the state there is, since that is explicitly zeroized by the
     genciph layer itself (after call to this routine finishes). */
  void (*zeroize)(void *context);

} *SshCipherDef, SshCipherDefStruct;


typedef struct SshCipherObjectRec *SshCipherObject;

/* We need access to object-level functions for KAT tests */
SshCryptoStatus
ssh_cipher_object_allocate(const char *type,
                           const unsigned char *key,
                           size_t keylen,
                           Boolean for_encryption,
                           SshCipherObject *cipher_ret);

void
ssh_cipher_object_free(SshCipherObject cipher);

SshCryptoStatus
ssh_cipher_object_transform_with_iv(SshCipherObject cipher,
                                    unsigned char *dest,
                                    const unsigned char *src,
                                    size_t len,
                                    unsigned char *iv);






#endif /* SSH_CIPHER_I_H */
