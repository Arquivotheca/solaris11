/*

  Hmac.h

  Author: Pekka Nikander <pnr@tequila.nixu.fi>
  Author: Mika Kojo <mkojo@ssh.fi>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
  All rights reserved.

  Created: Thu Jan  9 14:59:20 1997 [pnr]

  Message authentication code routines using the HMAC structure.

  */

#ifndef HMAC_H
#define HMAC_H

/* Generic Hmac interface. */

/* XXX Comment these well! */

size_t
ssh_hmac_ctxsize(const SshHashDefStruct *hash_def);

void
ssh_hmac_init(void *context, const unsigned char *key, size_t keylen,
              const SshHashDefStruct *hash_def);

void ssh_hmac_start(void *context);

void ssh_hmac_update(void *context, const unsigned char *buf,
                     size_t len);

void ssh_hmac_final(void *context, unsigned char *digest);

void ssh_hmac_96_final(void *context, unsigned char *digest);

void ssh_hmac_of_buffer(void *context, const unsigned char *buf,
                        size_t len, unsigned char *digest);

void ssh_hmac_96_of_buffer(void *context, const unsigned char *buf,
                           size_t len, unsigned char *digest);

void ssh_hmac_zeroize(void *context);

/* HMAC MD5 */

/* Returns the size of an md5 hmac context. */
size_t ssh_hmac_md5_ctxsize(void);

/* Allocates and initializes an md5 hmac context. */
void *ssh_hmac_md5_allocate(const unsigned char *key, size_t keylen);

/* Initializes an allocated md5 hmac context. */
void ssh_hmac_md5_init(void *context, const unsigned char *key,
                       size_t keylen);

/* Frees the md5 hmac context. */
void ssh_hmac_md5_free_context(void *context);

/* Reset md5 hmac context. */
void ssh_hmac_md5_start(void *context);

/* Update the md5 hmac context with buf of len bytes. */
void ssh_hmac_md5_update(void *context, const unsigned char *buf,
                         size_t len);

/* Output the md5 hmac digest. */
void ssh_hmac_md5_final(void *context, unsigned char *digest);

/* Directly computes the md5 hmac of the given buffer. */
void ssh_hmac_md5_of_buffer(void *context, const unsigned char *buf,
                            size_t len, unsigned char *digest);

/* Output the md5 hmac digest. */
void ssh_hmac_md5_96_final(void *context, unsigned char *digest);

/* Directly computes the md5 hmac of the given buffer. */
void ssh_hmac_md5_96_of_buffer(void *context, const unsigned char *buf,
                               size_t len, unsigned char *digest);

/* HMAC SHA */

/* Returns the size of an sha hmac context. */
size_t ssh_hmac_sha_ctxsize(void);

/* Allocates and initializes an sha hmac context. */
void *ssh_hmac_sha_allocate(const unsigned char *key, size_t keylen);

/* Initializes an allocated sha hmac context. */
void ssh_hmac_sha_init(void *context, const unsigned char *key,
                       size_t keylen);

/* Frees the sha context. */
void ssh_hmac_sha_free_context(void *context);

/* Reset md5 hmac context. */
void ssh_hmac_sha_start(void *context);

/* Update the md5 hmac context with buf of len bytes. */
void ssh_hmac_sha_update(void *context, const unsigned char *buf,
                         size_t len);

/* Output the md5 hmac digest. */
void ssh_hmac_sha_final(void *context, unsigned char *digest);

/* Directly computes the sha hmac of the given buffer. */
void ssh_hmac_sha_of_buffer(void *context, const unsigned char *buf,
                            size_t len, unsigned char *digest);

/* Output the md5 hmac digest. */
void ssh_hmac_sha_96_final(void *context, unsigned char *digest);

/* Directly computes the sha hmac of the given buffer. */
void ssh_hmac_sha_96_of_buffer(void *context, const unsigned char *buf,
                               size_t len, unsigned char *digest);
#endif /* HMAC_H */
