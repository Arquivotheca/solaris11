/*

  sshcipher.h

  Author: Santeri Paavolainen <santtu@ssh.com>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
  All rights reserved.

*/

#ifndef SSHCIPHER_H
#define SSHCIPHER_H

/************************* Secret key cryptography ************************/

/* Type used to represent a cipher object.  The exact semantics of the
   cipher depend on the encryption algorithm used, but generally the
   cipher object will remember its context (initialization vector, current
   context for stream cipher) from one encryption to another. */
typedef struct SshCipherRec *SshCipher;

/* Maximum size of a cipher block for block ciphers, in bytes. */
#define SSH_CIPHER_MAX_BLOCK_SIZE       32

/* Maximum size of the iv (initialization vector) for block ciphers in chained
   modes, in bytes. */
#define SSH_CIPHER_MAX_IV_SIZE          32

/* Returns a comma-separated list of cipher names.  The name may be of
   the format (e.g.) "des-cbc" for block ciphers.  The caller must
   free the returned list with ssh_crypto_free(). */
char *
ssh_cipher_get_supported(void);

/* Returns TRUE or FALSE depending on whether the cipher called "name" is
   supported with this version of crypto library. */
Boolean
ssh_cipher_supported(const char *name);

/* Returns TRUE or FALSE depending whether the cipher is a FIPS
   approved cipher or not (AES, DES and 3-DES in ECB, CBC, CFB or OFB
   modes of operation are approved). */
Boolean
ssh_cipher_is_fips_approved(const char *name);

/* Allocates and initializes a cipher of the specified type and mode.
   The cipher is keyed with the given key. 'for_encryption' should be
   TRUE if the cipher is to be used for encrypting data, and FALSE if
   it is to be used for decrypting.  The initialization vector for
   block ciphers is set to zero.

   If the key is too long for the given cipher, the key will be
   truncated.  If the key is too short, SSH_CRYPTO_KEY_TOO_SHORT is
   returned.

   This returns SSH_CRYPTO_OK on success. */
SshCryptoStatus
ssh_cipher_allocate(const char *type,
                    const unsigned char *key,
                    size_t keylen,
                    Boolean for_encryption,
                    SshCipher *cipher_ret);

/* Clears and frees the cipher from main memory.  The cipher object
   becomes invalid, and any memory associated with it is freed. */
void
ssh_cipher_free(SshCipher cipher);

/* Returns the name of the cipher. The name is the same as that used
   in ssh_cipher_allocate. The name points to an internal data structure and
   should NOT be freed, modified, or used after ssh_cipher_free is called. */
const char *
ssh_cipher_name(SshCipher cipher);

/* Query for the key length in bytes needed for a cipher. If the cipher is a
   variable-length cipher, then this is some sensible "default" value to use.
   This never returns zero if `name' is a valid cipher. */
size_t
ssh_cipher_get_key_length(const char *name);

/* Query for the minimum key length (in bytes) needed for a cipher. */
size_t
ssh_cipher_get_min_key_length(const char *name);

/* Query for the maximum key length (in bytes) needed for a
   cipher. Note that this can be zero if the cipher does not limit the
   maximum key length. */
size_t
ssh_cipher_get_max_key_length(const char *name);

/* A note about key lengths:

  The following assertions will always be true:

  min <= def && min <= max
  max == 0 || (def <= max)

  If you want to know whether the cipher is variable-length cipher,
  check whether min == max (and min != 0). If they differ, then it is
  a variable-length cipher. However you must realize that allocating a
  cipher based on this information might still fail on some key lengths. */

/* The following function checks whether a cipher is a variable-length
   cipher or not. It returns TRUE if the cipher corresponding to 'name'
   has a fixed key length (i.e. the cipher is not a variable-length cipher)
   and returns FALSE otherwise. */
Boolean ssh_cipher_has_fixed_key_length(const char *name);

/* Returns the block length in bytes of the cipher, or 1 if it is a stream
   cipher. The returned value will be at most SSH_CIPHER_MAX_BLOCK_SIZE. */
size_t
ssh_cipher_get_block_length(const char *name);

/* Returns the length in bytes of the initialization vector of the cipher in
   bytes, or 1 if it is a stream cipher. The returned value will be at most
   SSH_CIPHER_MAX_IV_SIZE. */
size_t
ssh_cipher_get_iv_length(const char *name);

/* Sets the initialization vector of the cipher. This is only meaningful for
   block ciphers used in one of the feedback/chaining modes. The default
   initialization vector is zero (every bit 0); changing it is completely
   optional (although highly recommended). The iv buffer must be at least size
   needed for the IV (ssh_cipher_get_iv_length). */

SshCryptoStatus
ssh_cipher_set_iv(SshCipher cipher,
                  const unsigned char *iv);

/* Gets the initialization vector of the cipher. This is only meaningful for
   block ciphers used in one of the feedback/chaining modes. The default
   initialization vector is zero (every bit 0); changing it is completely
   optional. The returned value must have enough space for the IV used by the
   cipher (ssh_cipher_get_iv_length or use SSH_CIPHER_MAX_IV_SIZE that is
   always enough). */

SshCryptoStatus
ssh_cipher_get_iv(SshCipher cipher,
                  unsigned char *iv);

/* Encrypts/decrypts data (depending on the for_encryption flag given when the
   SshCipher object was created).  Data is copied from src to dest while it
   is being encrypted/decrypted.  It is permissible that src and dest be the
   same buffer; however, partial overlap is not allowed.  For block ciphers,
   len must be a multiple of the cipher block size (this is checked); for
   stream ciphers there is no such limitation.

   If the cipher is used in a chaining mode or it is a stream cipher, the
   updated initialization vector or context is passed from one
   encryption/decryption call to the next.  In other words, all blocks
   encrypted with the same context form a single data stream, as if they
   were all encrypted with a single call.  If you wish to encrypt each
   block with a separate context, you must create a new SshCipher object
   every time (or, for block ciphers, you can manually set the initialization
   vector before each encryption). */

SshCryptoStatus
ssh_cipher_transform(SshCipher cipher,
                     unsigned char *dest,
                     const unsigned char *src,
                     size_t len);

/* This performs a combined ssh_cipher_set_iv, ssh_cipher_transform, and
   ssh_cipher_get_iv sequence (except that the iv stored in the cipher context
   is not actually changed by this sequence). This function can be safely
   called from multiple threads concurrently (i.e., the iv is only stored on
   the stack). This function can only be used for block ciphers. The returned
   value must have enough space for the IV used by the cipher
   (ssh_cipher_get_iv_length or use SSH_CIPHER_MAX_IV_SIZE that is always
   enough). */

SshCryptoStatus
ssh_cipher_transform_with_iv(SshCipher cipher,
                             unsigned char *dest,
                             const unsigned char *src,
                             size_t len,
                             unsigned char *iv);

#endif /* SSHCIPHER_H */
