/*

  rijndael.h

  Author: Timo J. Rinne

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
  All rights reserved.

*/

#ifndef RIJNDAEL_H
#define RIJNDAEL_H

/* Gets the size of Rijndael context. */
size_t ssh_rijndael_ctxsize(void);

/* Sets an already allocated Rijndael key */
SshCryptoStatus ssh_rijndael_init(void *context,
                                  const unsigned char *key,
                                  size_t keylen,
                                  Boolean for_encryption);

/* Sets an already allocated Rijndael key (cfb or ofb mode) */
SshCryptoStatus ssh_rijndael_init_fb(void *context,
                                     const unsigned char *key,
                                     size_t keylen,
                                     Boolean for_encryption);

/* This is like `ssh_rijndael_init', except enforces AES key size limits */
SshCryptoStatus ssh_aes_init(void *context,
                             const unsigned char *key,
                             size_t keylen,
                             Boolean for_encryption);

/* This is like `ssh_rijndael_init_fb', except enforces AES key size limits */
SshCryptoStatus ssh_aes_init_fb(void *context,
                                const unsigned char *key,
                                size_t keylen,
                                Boolean for_encryption);

/* Encrypt/decrypt in electronic code book mode. */
void ssh_rijndael_ecb(void *context, unsigned char *dest,
                      const unsigned char *src, size_t len,
                      unsigned char *iv);

/* Encrypt/decrypt in cipher block chaining mode. */
void ssh_rijndael_cbc(void *context, unsigned char *dest,
                      const unsigned char *src, size_t len,
                      unsigned char *iv);

/* Encrypt/decrypt in cipher feedback mode. */
void ssh_rijndael_cfb(void *context, unsigned char *dest,
                      const unsigned char *src, size_t len,
                      unsigned char *iv);

/* Encrypt/decrypt in output feedback mode. */
void ssh_rijndael_ofb(void *context, unsigned char *dest,
                      const unsigned char *src, size_t len,
                      unsigned char *iv);

/* Counter mode encryption. 'ctr', interpreted as a network byte
   order integer, is incremented by 1 after each block encryption. */
void ssh_rijndael_ctr(void *context, unsigned char *dest,
                      const unsigned char *src, size_t len,
                      unsigned char *ctr);

/* Rijndael CBC-MAC. */
void ssh_rijndael_cbc_mac(void *context, const unsigned char *src, size_t len,
                          unsigned char *iv);














#endif /* RIJNDAEL_H */
