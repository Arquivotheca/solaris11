/*

sshproxykey.h

Author: Vesa Suontama <vsuontam@ssh.fi>
        Patrick Irwin <irwin@ssh.fi> 

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
              All rights reserved

  Interface for creating proxy keys (and groups) (e.g) creating keys 
  (groups) with a callback that is called when a crypto operation is 
  being performed with a key. 

*/


#include "sshcrypt.h"

#ifndef SSH_PROXYKEY_H_DEFINED
#define SSH_PROXYKEY_H_DEFINED


/* The supported proxy key types. */
typedef enum 
{
  SSH_PROXY_RSA   = 0,
  SSH_PROXY_DSA   = 1,
  SSH_PROXY_GROUP = 2

} SshProxyKeyTypeId;


/* The operation identification (id) number. The operation id specifies 
   to the proxy key operation callback what type of key operation is to be 
   performed. Each operation id specifies a format for the encoding of 
   the input data, 'input_data', to the operation callback SshProxyKeyOpCB 
   and the operated data, 'operated_data', returned by the operation reply 
   callback SshProxyReplyCB. The format is obtained using the sshencode.h 
   routines. Encoding and decoding is only relevant when the input or output 
   data consists of more than one data buffer. 
 */ 
typedef enum
{
  /*** DSA operations ***/ 

  /* 'input_data' is the data buffer, 'operated_data' is the signed buffer.*/ 
  SSH_DSA_PRV_SIGN     = 0,
  
  /* 'input_data' is an encoded array consisting of the data buffer 
     followed by the signature  both encoded as SSH_FORMAT_UINT32_STR,
     'operated_data' is NULL.*/
  SSH_DSA_PUB_VERIFY   = 1,
  
  /*** RSA operations ***/

  /* 'input_data' is the data buffer, 'operated_data' is the 'operated_data' 
     operated on by a RSA decryption. */
  SSH_RSA_PRV_DECRYPT  = 2,

  /* 'input_data' is the data buffer, 'operated_data' is the operated data 
     operated on by a RSA encryption. */
  SSH_RSA_PUB_ENCRYPT  = 3,
  
  /* 'input_data' is the data buffer, 'operated_data' is the operated data 
     operated on by a RSA signature. */
  SSH_RSA_PRV_SIGN     = 4,

    /* This operation is used for RSA signature verifications. 'input_data' is 
     an encoded array consisting the data buffer followed by the signature 
     both encoded as SSH_FORMAT_UINT32_STR, 'operated_data' is NULL. */
  SSH_RSA_PUB_VERIFY   = 5,

  /*** Diffie-Hellman operations ***/ 

  /* 'input_data' is NULL, 'operated_data' is an encoded array consisting of 
     the exchange buffer followed by the Diffie-Hellman private secret
     both encoded as SSH_FORMAT_UINT32_STR. */
  SSH_DH_SETUP         = 6,

  /* 'input_data' is an encoded array of the other's side Diffie-Hellman 
     exchange followed by the Diffie-Hellman private secret, both encoded 
     as  SSH_FORMAT_UINT32_STR. 'operated_data' contains the Diffie-Hellman 
     shared secret. */
  SSH_DH_AGREE         = 7

} SshProxyOperationId;


/* The RGF identification (id) number. The RGF id specifies to the proxy 
   key operation callback what type of RGF (hashing and padding) operation 
   is to be performed.
 */ 
typedef enum 
{
  SSH_INVALID_RGF                    = 0,

  /* DSA signature scheme with SHA1 hashing */
  SSH_DSA_NIST_SHA1                  = 1,

  /* DSA with no hashing */
  SSH_DSA_NONE_NONE                  = 2,

  /* RSA OAEP */
  SSH_RSA_PKCS1V2_OAEP               = 3,

  /* RSA PKCS 1.5 */
  SSH_RSA_PKCS1_SHA1                 = 4,
  SSH_RSA_PKCS1_MD5                  = 5,
  SSH_RSA_PKCS1_MD2                  = 6,

  /* RSA PKCS 1.5 where the input data is already hashed. */
  SSH_RSA_PKCS1_SHA1_NO_HASH         = 7,
  SSH_RSA_PKCS1_MD5_NO_HASH          = 8,
  SSH_RSA_PKCS1_MD2_NO_HASH          = 9,

  /* PKCS1 without OID's or hashing (as used in IKEv1). */
  SSH_RSA_PKCS1_NONE                 = 10,

  /* No hashing or padding */
  SSH_RSA_NONE_NONE                  = 11,

  /* Used for the Diffie-Hellman operations (no hashing or padding) */
  SSH_DH_NONE_NONE                   = 12,

  /* DSA signature scheme with MD5 or MD2 hashing */
  SSH_DSA_MD5                        = 13,
  SSH_DSA_MD2                        = 14,

  /* As in RSA PKCS 1.5, but do not apply the final pkcs1 padding. */
  SSH_RSA_PKCS1_SHA1_NO_PAD          = 15,
  SSH_RSA_PKCS1_MD5_NO_PAD           = 16
} SshProxyRGFId;


/* This handle gets passed to the user supplied callback operation 
   (of type SshProxyKeyOpCB) when performing key operations. It contains 
   a handle to the SshPrivateKey, SshPublicKey or SshPkGroup object 
   returned from the ssh_*_create_proxy call. The key handle may be 
   obtained from the function below. 
*/
typedef struct SshProxyKeyHandleRec *SshProxyKeyHandle;

/* Returns a handle to a proxy key (group) (which is of the type 
   SshPrivateKey, SshPublicKey or SshPkGroup). This function should be 
   called inside the user supplied callback operation (of type 
   SshProxyKeyOpCB) when performing key operations. This functions 
   returns the handle to the SshPrivateKey, SshPublicKey or SshPkGroup 
   object returned from the ssh_*_create_proxy call. 
*/
void * ssh_proxy_key_get_key_handle(SshProxyKeyHandle handle);


/* A generic freeing function that must be provided to the ssh_*_create_proxy 
   functions. Its purpose is to free the context data that is given to 
   the ssh_*_create_proxy functions. 
*/
typedef void (*SshProxyFreeOpCB)(void *context);


/* A callback of this type must be called by the operation callbacks 
   to complete the asynchronous operation. If the operation was successful, 
   status is SSH_CRYPTO_OK and the 'operated_data' contains the operated data. 

   If the operation does not return data, i.e. just a result status, 
   'operated_data' should be set to NULL and 'data_len' to 0.

   If the operation is unsuccessful, status should detail the reason for 
   failure.
 */
typedef void (*SshProxyReplyCB)(SshCryptoStatus status,
                                const unsigned char *operated_data,
                                size_t data_len,
                                void *reply_context);

/* This callback is called to operate on the provided input data 
   'input_data', with the operation specified by operation_id and RGF 
   specified by rgf_id. When 'input data' is operated, the function must 
   call  'reply_cb' to continue the asynchronous operation. 

   'handle' contains the handle to the SshPrivateKey, SshPublicKey or 
   SshPkGroup object returned from the ssh_*_create_proxy call. 

   'context' is the context data given to the ssh_*_create_proxy function.

   If this callback is asynchronous it has to return an SshOperationHandle, 
   which can be used to cancel the operation. 
*/
typedef 
SshOperationHandle (*SshProxyKeyOpCB)(SshProxyOperationId operation_id,
                                      SshProxyRGFId rgf_id,
                                      SshProxyKeyHandle handle,
                                      const unsigned char *input_data,
                                      size_t input_data_len,
                                      SshProxyReplyCB reply_cb,
                                      void *reply_context,
                                      void *context);


/* Create a proxy private key. Calls the operation callback, 'key_operation', 
   with the data that is being operated when the library is performing 
   crypto operations with the returned proxy key. 'key_size_in_bits' specifies 
   the key size (in bits) of the resulting proxy key. The proxy private key 
   is freed with ssh_private_key_free. 
*/
SshPrivateKey 
ssh_private_key_create_proxy(SshProxyKeyTypeId key_type,
                             SshUInt32 key_size_in_bits,
                             SshProxyKeyOpCB key_operation,
                             SshProxyFreeOpCB free_operation,  
                             void *context);



/* Create a proxy public key. 
   
   Calls the 'key_operation' with the data that is being operated when 
   the library is performing crypto operations with the returned proxy key. 
   'key_size_in_bits' specifies the key size (in bits) of the resulting 
   proxy key. The proxy public key is freed with ssh_public_key_free. 
*/
SshPublicKey 
ssh_public_key_create_proxy(SshProxyKeyTypeId key_type,
                            SshUInt32 key_size_in_bits, 
                            SshProxyKeyOpCB key_operation,
                            SshProxyFreeOpCB free_operation,   
                            void *context); 


/* Create a proxy group (for Diffie-Hellman). Calls the 'key_operation' with 
   the data that is being operated when the library is performing crypto 
   operations with the returned proxy group. 'key_size_in_bits' specifies 
   the key size (in bits) of the resulting proxy group. The proxy group is 
   freed with ssh_pk_group_free. 
  
*/
SshPkGroup 
ssh_dh_group_create_proxy(SshProxyKeyTypeId key_type,
                          SshUInt32 key_size_in_bits,
                          SshProxyKeyOpCB key_operation,
                          SshProxyFreeOpCB free_operation,    
                          void *context);



/* The ProxyKey interface requires that the user perform the RGF to the 
   data. This can be often cumbersome. The following functions perform 
   the RGF transform to the input_data with the given SshProxyOperationId 
   and SshProxyRGFId. This function can be used to simplify the 
   construction of the SshProxyKeyOpCB key_operation function which gets 
   passed to the ssh_*_create_proxy functions.
   
   Returns SSH_CRYPTO_OK on success. On failure *output_data will be freed 
   automatically. If no RGF transform is necessary, *output_data is returned 
   as NULL.  
*/ 


/* Used before public key encryption. Pads input data according to the scheme 
   specified by rgf_id. Allocates and places the output buffer in 
   'output_data'. 'maximum_len' specifies the maximum possible length of 
   the output buffer (this is usually the key size in bytes). */
SshCryptoStatus
ssh_proxy_key_rgf_encrypt(SshProxyOperationId operation_id,
                          SshProxyRGFId rgf_id,
                          size_t maximum_len,
                          const unsigned char *input_data,
                          size_t input_data_len,
                          unsigned char **output_data, 
                          size_t *output_data_len);

/* Used after private key decryption. Unpads 'input_data' according to the 
   scheme specified by rgf_id. Allocates and places the output buffer in 
   'output_data'. 'maximum_len' specifies the maximum possible length of 
   the output buffer (this is usually the key size in bytes). */
SshCryptoStatus
ssh_proxy_key_rgf_decrypt(SshProxyOperationId operation_id,
                          SshProxyRGFId rgf_id,
                          size_t maximum_len,
                          const unsigned char *input_data,
                          size_t input_data_len,
                          unsigned char **output_data, 
                          size_t *output_data_len);

/* Used before private key signatures. Hashes and pads 'input_data' according 
   to the scheme specified by rgf_id. Allocates and places the output 
   buffer in 'output_digest'. 'maximum_len' specifies the maximum possible 
   length of the 'output_digest' (this is usually the key size in bytes). */
SshCryptoStatus 
ssh_proxy_key_rgf_sign(SshProxyOperationId operation_id,
                       SshProxyRGFId rgf_id,
                       size_t maximum_len,
                       const unsigned char *input_data,
                       size_t input_data_len,
                       unsigned char **output_digest, 
                       size_t *output_digest_len);

/* Used for public key signature verification. Hashes 'data' and unpads
   'decrypted_signature' according to the scheme specified by rgf_id. Then 
   performs the signature verification. 'maximum_len' specifies the 
   maximum possible length of the unpadded 'decrypted_signature'(this is 
   usually the key size in bytes). 

   Returns SSH_CRYPTO_OK on success and SSH_CRYPTO_SIGNATURE_CHECK_FAILED 
   if the signature is not valid. */
SshCryptoStatus 
ssh_proxy_key_rgf_verify(SshProxyOperationId operation_id,
                         SshProxyRGFId rgf_id,
                         size_t maximum_len,
                         const unsigned char *data,
                         size_t data_len,
                         const unsigned char *decrypted_signature,
                         size_t decrypted_signature_len);


#endif /* SSH_PROXYKEY_H_DEFINED */





