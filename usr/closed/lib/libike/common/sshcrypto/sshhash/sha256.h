/*

  sha256.h

  Author: Mika Kojo <mkojo@ssh.fi>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
  All rights reserved.

  Created Sun Feb  4 20:25:55 2001.

  */

#ifndef SSH_SHA256_H
#define SSH_SHA256_H

/* Returns the size of an SHA context. */
size_t ssh_sha256_ctxsize(void);

/* Resets the SHA context to its initial state. */
void ssh_sha256_reset_context(void *context);

/* Add `len' bytes from the given buffer to the hash. */
void ssh_sha256_update(void *context, const unsigned char *buf,
                    size_t len);

/* Finish hashing. Return the 32-byte long digest to the
   caller-supplied buffer. */
void ssh_sha256_final(void *context, unsigned char *digest);

/* Compute SHA digest from the buffer. */
void ssh_sha256_of_buffer(unsigned char digest[32],
                       const unsigned char *buf, size_t len);

/* Finish hashing. Return the 12-byte long digest to the
   caller-supplied buffer. */
void ssh_sha256_96_final(void *context, unsigned char *digest);

/* Compute SHA digest from the buffer. */
void ssh_sha256_96_of_buffer(unsigned char digest[12],
                          const unsigned char *buf, size_t len);

/* Finish hashing. Return the 10-byte long digest to the
   caller-supplied buffer. */
void ssh_sha256_80_final(void *context, unsigned char *digest);

/* Compute SHA digest from the buffer. */
void ssh_sha256_80_of_buffer(unsigned char digest[10],
                          const unsigned char *buf, size_t len);

/* Make the defining structure visible everywhere. */
extern const SshHashDefStruct ssh_hash_sha256_def;
extern const SshHashDefStruct ssh_hash_sha256_96_def;
extern const SshHashDefStruct ssh_hash_sha256_80_def;

#ifdef SUNWIPSEC
extern const SshHashDefStruct ssh_hash_sha384_def;
extern const SshHashDefStruct ssh_hash_sha512_def;
#endif

#endif /* SSH_SHA256_H */
