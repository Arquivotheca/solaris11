/*

DES implementation; 1995-2002 Tatu Ylonen <ylo@cs.hut.fi>

This implementation is derived from libdes-3.06, which is copyright
(c) 1993 Eric Young, and distributed under the GNU GPL or the ARTISTIC licence
(at the user's option).  The original distribution can be found e.g. from
ftp://ftp.dsi.unimi.it/pub/security/crypt/libdes/libdes-3.06.tar.gz.

This implementation is distributed under the same terms.  See
libdes-README, libdes-ARTISTIC, and libdes-COPYING for more
information.

*/

#ifndef SSHDES_H
#define SSHDES_H

/* Single des */

/* Returns the size of a des key context. */
size_t ssh_des_ctxsize(void);

/* Initializes an already allocated des key context */
SshCryptoStatus ssh_des_init(void *context,
                             const unsigned char *key, size_t keylen,
                             Boolean for_encryption);

/* Initializes an already allocated des key context */
SshCryptoStatus ssh_des_init_with_key_check(void *context,
                                            const unsigned char *key,
                                            size_t keylen,
                                            Boolean for_encryption);

/* Encrypt in ecb/cbc/cfb/ofb modes. */
void ssh_des_ecb(void *context, unsigned char *dest,
             const unsigned char *src, size_t len,
             unsigned char *iv);

void ssh_des_cbc(void *context, unsigned char *dest,
             const unsigned char *src, size_t len,
             unsigned char *iv);

void ssh_des_cfb(void *context, unsigned char *dest,
             const unsigned char *src, size_t len,
             unsigned char *iv);

void ssh_des_ofb(void *context, unsigned char *dest,
             const unsigned char *src, size_t len,
             unsigned char *iv);

void ssh_des_cbc_mac(void *context, const unsigned char *src, size_t len,
                     unsigned char *iv);

/* Triple des */

/* Returns the size of a 3des key context. */
size_t ssh_des3_ctxsize(void);

#ifndef KERNEL
/* Sets the des key for the context.  Initializes the context.  The least
   significant bit of each byte of the key is ignored as parity. */
void *ssh_des3_allocate(const unsigned char *key, size_t keylen,
                    Boolean for_encryption);
#endif /* !KERNEL */

/* Sets an already allocated 3des context. */
SshCryptoStatus ssh_des3_init(void *context,
                              const unsigned char *key, size_t keylen,
                              Boolean for_encryption);

SshCryptoStatus ssh_des3_init_with_key_check(void *ptr,
                                             const unsigned char *key,
                                             size_t keylen,
                                             Boolean for_encryption);

/* Destroy any sensitive data in the context. */
void ssh_des3_free(void *context);

/* Encrypt using ecb/cbc/cfb/ofb modes. */
void ssh_des3_ecb(void *context, unsigned char *dest,
                  const unsigned char *src, size_t len,
                  unsigned char *iv);

void ssh_des3_cbc(void *context, unsigned char *dest,
                  const unsigned char *src, size_t len,
                  unsigned char *iv);

void ssh_des3_cfb(void *context, unsigned char *dest,
                  const unsigned char *src, size_t len,
                  unsigned char *iv);

void ssh_des3_ofb(void *context, unsigned char *dest,
                  const unsigned char *src, size_t len,
                  unsigned char *iv);

void ssh_des3_cbc_mac(void *context, const unsigned char *src, size_t len,
                      unsigned char *iv);

#endif /* SSHDES_H */
