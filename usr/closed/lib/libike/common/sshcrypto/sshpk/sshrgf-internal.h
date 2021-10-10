/*
  File: sshrgf-internal.h

  Authors:
        Patrick Irwin <irwin@ssh.fi>

  Description:
        Redundancy Generators

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
                  All rights reserved.
*/

#include "sshhash_i.h"
#include "sshrgf.h"


/* The RGF allocation function. */
typedef SshRGF (*SshRGFAllocate)(const SshRGFDefStruct *def);

/* The RGF freeing function. */
typedef void (*SshRGFFree)(SshRGF rgf);


/********* Functions related to the hashing part of an RGF. ***********/


/* The RGF hash function update. By definition this function updates the 
   rgf context by the input data. The data may be either usual update data, 
   which should be handled by a hash function or the resulting digest 
   computed "outside".

   In the latter case the 'for_digest' is set to TRUE. If such a case
   happens the update function may either reject the operation or
   function in a such a way that the resulting digest is equal to the
   input. */
typedef Boolean (*SshRGFHashUpdate)(SshRGF rgf, Boolean for_digest,
                                    const unsigned char *data,
                                    size_t data_len);

/* Allocate and return the computed hashed digest. */
typedef Boolean (*SshRGFHashFinalize)(SshRGF rgf, unsigned char **digest, 
                                      size_t *digest_length);

/* Compares the given oid with max size of max_len to the oid
   defined for the hash. If they match, then return the number
   of bytes actually used by the oid. If they do not match, return
   0. */
typedef size_t (*SshRGFHashAsn1OidCompare)(SshRGF rgf,
					   const unsigned char *oid,
					   size_t max_len);
/* Generate encoded asn1 oid. Returns the pointer to the staticly
   allocated buffer of the oid. Sets the len to be the length
   of the oid. 
   XXX this might need to be changed to be dynamic buffer if
   we have hash functions which have parameters. */
typedef const unsigned char *(*SshRGFHashAsn1OidGenerate)(SshRGF rgf,
							  size_t *len);

/********* Functions related to the padding part of an RGF. ***********/

/* The RGF encryption and decryption functions. */
typedef SshCryptoStatus (*SshRGFEncrypt)(SshRGF rgf,
                                         const unsigned char *msg,
                                         size_t msg_len,
                                         size_t max_output_msg_len,
                                         unsigned char **output_msg,
                                         size_t *output_msg_len);

typedef SshCryptoStatus (*SshRGFDecrypt)(SshRGF rgf,
                                         const unsigned char *decrypted_msg,
                                         size_t decrypted_msg_len,
                                         size_t max_output_msg_len,
                                         unsigned char **output_msg,
                                         size_t *output_msg_len);

/* The RGF signature and verification functions. */
typedef SshCryptoStatus (*SshRGFSign)(SshRGF rgf,
                                      size_t max_output_msg_len,
                                      unsigned char **output_msg,
                                      size_t *output_msg_len);

typedef 
SshCryptoStatus (*SshRGFVerify)(SshRGF rgf,
                                const unsigned char *decrypted_signature,
                                size_t decrypted_signature_len);

size_t 
ssh_rgf_hash_asn1_oid_compare(SshRGF rgf,
			      const unsigned char *oid,
			      size_t max_len);
const unsigned char *
ssh_rgf_hash_asn1_oid_generate(SshRGF rgf, size_t *len);



struct SshRGFDefRec
{
  SshRGFAllocate rgf_allocate;
  SshRGFFree rgf_free;

  /* Hashing related functions. */
  SshRGFHashUpdate               rgf_hash_update;
  SshRGFHashFinalize             rgf_hash_finalize;
  SshRGFHashAsn1OidCompare	 rgf_hash_asn1_oid_compare;
  SshRGFHashAsn1OidGenerate	 rgf_hash_asn1_oid_generate;

  /* The hash function definition. */
  const SshHashDefStruct *hash_def;

  /* Redundancy generation functions. */
  SshRGFEncrypt rgf_encrypt;
  SshRGFDecrypt rgf_decrypt;
  SshRGFSign    rgf_sign;
  SshRGFVerify  rgf_verify;
};


/* The RGF function context. */ 
struct SshRGFRec
{
  /* The RGF method definition. */
  const SshRGFDefStruct *def;

  /* The area for storing a precomputed hash digest. */
  const unsigned char *precomp_digest;
  size_t precomp_digest_length;

  /* TRUE if the RGF has been updated with a digest, FALSE otherwise. */
  Boolean sign_digest;

  /* The state context. */
  void *context;
};




