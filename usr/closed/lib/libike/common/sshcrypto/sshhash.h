/*

  sshhash.h

  Author: Santeri Paavolainen <santtu@ssh.com>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
  All rights reserved.

*/

#ifndef SSHHASH_H
#define SSHHASH_H

/*********************** Hash functions ***********************************/

typedef struct SshHashRec *SshHash;

/* Maximum digest length in bytes that may be output by any hash function. */
#define SSH_MAX_HASH_DIGEST_LENGTH   64

/* Returns a comma-separated list of supported hash functions names.
   The caller must free the returned value with ssh_crypto_free(). */
char *
ssh_hash_get_supported(void);

/* Returns TRUE or FALSE depending whether the hash function called
   "name" is supported with this version of the crypto library. */
Boolean
ssh_hash_supported(const char *name);

/* Returns TRUE or FALSE depending whether the hash is
   a FIPS approved hash (SHA-1 is approved). */
Boolean
ssh_hash_is_fips_approved(const char *name);

/* Get the ASN.1 Object Identifier of the hash, if available. Returns the
   OID in 'standard' form e.g. "1.2.3.4". Returns NULL if a OID is not
   available. The returned value points to internal constant data and
   must not be freed. */
const char *
ssh_hash_asn1_oid(const char *name);

size_t
ssh_hash_asn1_oid_compare(const char *name, const unsigned char *oid,
 			  size_t max_len);
 
const unsigned char *
ssh_hash_asn1_oid_generate(const char *name, size_t *len);

/* Get the digest length in bytes of the hash. */
size_t
ssh_hash_digest_length(const char *name);

/* Get input block size in bytes (used for hmac padding). */
size_t
ssh_hash_input_block_size(const char *name);

/* Allocates and initializes a hash. */
SshCryptoStatus
ssh_hash_allocate(const char *name, SshHash *hash);

/* Free a hash. This can also be called when the library is in an error
   state. */
void
ssh_hash_free(SshHash hash);

/* Duplicate a hash object. This function allocates a new hash object which is
   an exact duplicate of the input hash object, even with the exactly same
   state. The returned hash object must be freed separately with ssh_hash_free.
   The function returns NULL if the input hash object is also NULL. */
SshHash
ssh_hash_duplicate(const SshHash hash);

/* Resets the hash context to its initial state. */
void
ssh_hash_reset(SshHash hash);

/* Updates the hash context by adding the given text. If any internal error is
   encountered, it is noted and reported at ssh_hash_final. */
void
ssh_hash_update(SshHash hash, const unsigned char *buf, size_t len);

/* Outputs the hash digest. The user allocated digest buffer must be
   at least ssh_hash_digest_length(hash) bytes long.  */
SshCryptoStatus
ssh_hash_final(SshHash hash, unsigned char *digest);

/* Returns name of the hash. The name is same as that what was used in
   hash allocate. The name points to an internal data structure and
   should NOT be freed, modified, or used after ssh_hash_free. */
const char *
ssh_hash_name(SshHash hash);

#endif /* SSHHASH_H */
