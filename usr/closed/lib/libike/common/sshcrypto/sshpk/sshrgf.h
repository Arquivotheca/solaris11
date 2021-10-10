/*

  sshrgf.h

  Author: Mika Kojo     <mkojo@ssh.fi>
          Patrick Irwin <irwin@ssh.fi>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
  All rights reserved.

  Created Thu Dec  9 23:42:00 1999.
  
*/

/*  Redundancy generating functions for public key cryptosystems.  */

#ifndef SSHRGF_H
#define SSHRGF_H


/* Redundancy functions are used in public key cryptosystems for two 
   purposes. Firstly, to create cryptographically good padding for public 
   and private key operations, and secondly, to hash input data to 
   signature operations. These two operations are combined using the 
   RGF functions defined here. */

/* An RGF definition structure, this determines which operations the 
   resulting RGF object may perform. */
typedef struct SshRGFDefRec *SshRGFDef, SshRGFDefStruct;

/* An RGF object. */
typedef struct SshRGFRec *SshRGF;


/**************** Allocation and Freeing. ******************************/
 
SshRGF ssh_rgf_allocate(const SshRGFDefStruct *rgf_def);

void ssh_rgf_free(SshRGF hash);


/**************** Hash data into the RGF. ******************************/

/* Update the RGF function with data to be hashed. This works exactly 
   like the SshHash update, but only with an RGF. */
void ssh_rgf_hash_update(SshRGF rgf, const unsigned char *data, 
                         size_t data_len);

/* Update the RGF with a previously hashed digest. This operation may 
   fail if the mechanism does not allow setting the resulting digest. 
   In such a case, this function returns FALSE. */
Boolean ssh_rgf_hash_update_with_digest(SshRGF rgf,
                                        const unsigned char *digest,
                                        size_t digest_len);


/*************************************************************************/
/* Padding functions appplied to data during public key operations. */

/* Pad the input data 'msg' of length 'msg_len' before encryption. 
   'max_output_msg_len' is the desired size of the output message which 
   is usually the key size in bytes of the public key which will encrypt 
   the data. The output padded data is allocated and returned in 
   'output_msg', the length of the output data is returned in 
   'output_msg_len'. Returns SSH_CRYPTO_OK on success. */
SshCryptoStatus ssh_rgf_for_encryption(SshRGF rgf,
                                       const unsigned char *msg,
                                       size_t msg_len,
                                       size_t max_output_msg_len,
                                       unsigned char **output_msg,
                                       size_t *output_msg_len);

/* Unpad the input data 'msg' of length 'msg_len' after decryption. 
   'max_output_msg_len' is an upper limit on the size of the output 
   message which can be taken to be 'decrypted_msg_len' as in all cases 
   removing the padding will shorten the message length. The output 
   padded data is allocated and returned in 'output_msg', the length of 
   the output data is returned in 'output_msg_len'. Returns SSH_CRYPTO_OK 
   on success. */
SshCryptoStatus ssh_rgf_for_decryption(SshRGF rgf,
                                       const unsigned char *decrypted_msg,
                                       size_t decrypted_msg_len,
                                       size_t max_output_msg_len,
                                       unsigned char **output_msg,
                                       size_t *output_msg_len);

/* Output padded data from the RGF. Input data should have been previously 
   hashed into the RGF by calling ssh_rgf_hash_update_with_digest or 
   ssh_rgf_hash_update. 'max_output_msg_len' is an upper limit on the 
   size of the output message. The output padded data is allocated and 
   returned in 'output_msg', the length of the output data is returned 
   in 'output_msg_len'. Returns SSH_CRYPTO_OK on success. */
SshCryptoStatus ssh_rgf_for_signature(SshRGF rgf,
                                      size_t max_output_msg_len,
                                      unsigned char **output_msg,
                                      size_t *output_msg_len);

/* Use the RGF for signature verification. Input data whose signature is 
   to be verified should have been previously hashed into the RGF by 
   calling ssh_rgf_hash_update_with_digest or ssh_rgf_hash_update. 
   This function then verifies the signature with input 'rgf' and the 
   decrypted signature buffer obtained from a public key operation on the
   received signature. Returns SSH_CRYPTO_OK if the signature validates 
   and SSH_CRYPTO_SIGNATURE_CHECK_FAILED if the signature is invalid. */
SshCryptoStatus 
ssh_rgf_for_verification(SshRGF rgf,
                         const unsigned char *decrypted_signature,
                         size_t decrypted_signature_len);


/******************** Utility Functions. ***************************/

size_t ssh_rgf_hash_digest_length(SshRGF rgf);

/* Derive a SSH hash function. This call may fail, and in such a case
   will return NULL. This means that the RGF update part cannot be
   separated into a "simple" hash function.

   Most public key mechanisms available use standard "simple" hash
   functions as a basis and hence this derivation is often possible. Please
   observe, that this is sensible mainly if you want to derive from the
   hash function e.g. a MAC or do similar complicated processing. Otherwise
   you can just use the ssh_rgf_hash_update. */
SshHash ssh_rgf_derive_hash(SshRGF rgf);

/* Returns TRUE if the data in the SshRGF object has already been hashed 
   (the data has been input to the RGF using ssh_rgf_hash_update_with_digest) 
   and FALSE otherwise (the data has been input using ssh_rgf_hash_update). 
   This function is needed by the proxykey library: when delegating 
   crypto operations to external devices, some devices insist on performing 
   the RGF operation themselves. In this case, the proxykey library needs 
   to know whether the data has been previously hashed so it can correctly 
   inform the external device which RGF to apply. */
Boolean ssh_rgf_data_is_digest(SshRGF rgf);


/******************** Definition structures. ************************/


#ifdef SSHDIST_CRYPT_SHA
extern const SshRGFDefStruct ssh_rgf_pkcs1_sha1_def;
extern const SshRGFDefStruct ssh_rgf_pkcs1v2_sha1_def;
extern const SshRGFDefStruct ssh_rgf_std_sha1_def;
extern const SshRGFDefStruct ssh_rgf_pkcs1_nopad_sha1_def;
extern const SshRGFDefStruct ssh_rgf_pkcs1_sha1_no_hash_def;
#endif /* SSHDIST_CRYPT_SHA */

#ifdef SSHDIST_CRYPT_MD5
extern const SshRGFDefStruct ssh_rgf_pkcs1_md5_def;
extern const SshRGFDefStruct ssh_rgf_pkcs1v2_md5_def;
extern const SshRGFDefStruct ssh_rgf_std_md5_def;
extern const SshRGFDefStruct ssh_rgf_pkcs1_nopad_md5_def;
extern const SshRGFDefStruct ssh_rgf_pkcs1_md5_no_hash_def;
#endif /* SSHDIST_CRYPT_MD5 */

#ifdef SSHDIST_CRYPT_MD2
extern const SshRGFDefStruct ssh_rgf_pkcs1_md2_def;
extern const SshRGFDefStruct ssh_rgf_pkcs1v2_md2_def;
extern const SshRGFDefStruct ssh_rgf_std_md2_def;
extern const SshRGFDefStruct ssh_rgf_pkcs1_md2_no_hash_def;

#endif /* SSHDIST_CRYPT_MD2 */

extern const SshRGFDefStruct ssh_rgf_pkcs1_none_def;
extern const SshRGFDefStruct ssh_rgf_pkcs1v2_none_def;
extern const SshRGFDefStruct ssh_rgf_dummy_def;

/* This RGF behaves differently from all others in that the ssh_rgf_for_* 
   functions do not allocate the return data but instead return pointers 
   to the input data. */
extern const SshRGFDefStruct ssh_rgf_dummy_no_allocate_def;

#endif /* SSHRGF_H */




