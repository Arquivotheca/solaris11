/*

sshproxykey.c

Authors: Vesa Suontama     <vsuontam@ssh.fi>
         Jukka Aittokallio <jai@ssh.fi>
         Patrick Irwin     <irwin@ssh.fi>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
              All rights reserved

*/

#include "sshincludes.h"
#include "sshproxykey.h"
#include "sshpk_i.h"
#include "sshrgf.h"
#include "sshgetput.h"
#include "sshencode.h"

#define SSH_DEBUG_MODULE "SshProxyKey"

#define KEY_SIZE_TO_BYTES(x) ((((x) + 7) >> 3))

/* Declare the proxy keys defined later on this file. */
const SshPkType ssh_proxy_key_if_modn;
const SshPkType ssh_proxy_key_dl_modp;


typedef struct SshProxyKeyBaseRec {
  /* This parameter counts how many proxy keys share 'context'. */
  SshUInt16 ref_count;

  /* The context that is supplied to the ssh_*_create_proxy functions. */
  void *context;
} *SshProxyKeyBase;


/* This object contains handles to the generated key object and the
   user supplied context data. */
struct SshProxyKeyHandleRec {
  /* The address of the generated SshPrivateKey, SshPublicKey or
     SshPkGroup will be stored here. */
  void *key_addr;

  SshProxyKeyBase base;
};


typedef struct ProxyKeyRec {
  SshProxyKeyHandle handle;

  SshProxyKeyTypeId key_type;
  SshUInt32 key_size;
  SshProxyKeyOpCB key_operation;
  SshProxyFreeOpCB free_operation;
} *ProxyKey;



/************************************************************************/

typedef struct ProxySignContextRec {
  SshOperationHandle op;
  SshOperationHandle sub_op;
  ProxyKey key;
  SshPrivateKeySignCB callback;
  void *context;
} *ProxyKeySignContext;


/************************ The Proxy Key Signature Scheme. *****************/

void ssh_proxy_sign_abort(void *context)
{
  ProxyKeySignContext ctx = context;

  ssh_operation_abort(ctx->sub_op);
  ssh_free(ctx);
}

void ssh_proxy_sign_free(void *context)
{
  ProxyKeySignContext ctx = context;

  ssh_operation_unregister(ctx->op);
  ssh_proxy_sign_abort(ctx);
}

void ssh_proxy_sign_op_done(SshCryptoStatus status,
                            const unsigned char *data,
                            size_t data_len,
                            void *context)
{
  ProxyKeySignContext sign_ctx = context;

  sign_ctx->sub_op = NULL;

  (*sign_ctx->callback)(status, data, data_len, sign_ctx->context);

  ssh_proxy_sign_free(sign_ctx);
}

/* Converts RGF schemes of the form 'hash then pad' to 'pad'. Necessary for
   the ssh_private_key_sign_digest and ssh_public_key_verify_with_digest
   functions.  */
static SshProxyRGFId get_rgf_pad_id(SshProxyRGFId rgf_id)
{
  if (rgf_id == SSH_RSA_PKCS1_SHA1)
    return SSH_RSA_PKCS1_SHA1_NO_HASH;

  if (rgf_id == SSH_RSA_PKCS1_MD5)
    return SSH_RSA_PKCS1_MD5_NO_HASH;

  if (rgf_id == SSH_RSA_PKCS1_MD2)
    return SSH_RSA_PKCS1_MD2_NO_HASH;

  /* The following RGF is not used for sigature schemes. */
  if (rgf_id == SSH_RSA_PKCS1V2_OAEP)
    return SSH_INVALID_RGF;

  if (rgf_id == SSH_RSA_PKCS1_NONE)
    return SSH_RSA_PKCS1_NONE;

  if (rgf_id == SSH_RSA_NONE_NONE)
    return SSH_RSA_NONE_NONE;

  if (rgf_id == SSH_DSA_NIST_SHA1 || rgf_id == SSH_DSA_MD5 ||
      rgf_id == SSH_DSA_MD2 || rgf_id == SSH_DSA_NONE_NONE)
    return SSH_DSA_NONE_NONE;

  /* XXX Define the NO_PAD_NO_HASH rgf's. */
  if (rgf_id == SSH_RSA_PKCS1_SHA1_NO_PAD ||
      rgf_id == SSH_RSA_PKCS1_MD5_NO_PAD)
  return SSH_INVALID_RGF;

  return SSH_INVALID_RGF;
}

SshOperationHandle
ssh_proxy_sign_async(const void *private_key,
                     SshProxyRGFId rgf_id,
                     SshRGF rgf,
                     SshPrivateKeySignCB callback,
                     void *context)
{
  ProxyKeySignContext sign_ctx;
  SshProxyOperationId operation_id;
  SshCryptoStatus status;
  SshOperationHandle sub_op;
  ProxyKey key = (ProxyKey)private_key;
  unsigned char *raw_data;
  size_t raw_data_len;

  /* Determine the operation id. */
  if (key->key_type == SSH_PROXY_RSA)
    {
      operation_id = SSH_RSA_PRV_SIGN;
    }
  else if (key->key_type == SSH_PROXY_DSA)
    {
      operation_id = SSH_DSA_PRV_SIGN;
    }
  else
    {
      (*callback)(SSH_CRYPTO_UNSUPPORTED, NULL, 0, context);
      return NULL;
    }

  if ((sign_ctx = ssh_calloc(1, sizeof(*sign_ctx))) != NULL)
    {
      sign_ctx->callback = callback;
      sign_ctx->context = context;
      sign_ctx->key = (ProxyKey)private_key;

      /* If the hash is already done, we only want the pad name. */
      if (ssh_rgf_data_is_digest(rgf))
        rgf_id = get_rgf_pad_id(rgf_id);

      if (rgf_id == SSH_INVALID_RGF)
        {
          ssh_free(sign_ctx);
          (*callback)(SSH_CRYPTO_UNSUPPORTED, NULL, 0, context);
          return NULL;
        }

      /* We use to no allocate rgf here. */
      if ((status = ssh_rgf_for_signature(rgf,
                                          (size_t) -1,
                                          &raw_data,
                                          &raw_data_len))
           != SSH_CRYPTO_OK)
        {
          (*callback)(status, NULL, 0, context);
          ssh_proxy_sign_free(sign_ctx);

          return NULL;
        }

      /* Register the abort operation. */
      sign_ctx->op = ssh_operation_register(ssh_proxy_sign_abort,
                                            sign_ctx);

      sub_op = (*key->key_operation)(operation_id,
                                     rgf_id,
                                     key->handle,
                                     raw_data,
                                     raw_data_len,
                                     ssh_proxy_sign_op_done,
                                     sign_ctx,
                                     key->handle->base->context);

      if (sub_op)
        {
          sign_ctx->sub_op = sub_op;
          return sign_ctx->op;
        }
      return NULL;
    }
  else
    {
      (*callback)(SSH_CRYPTO_NO_MEMORY, NULL, 0, context);
      return NULL;
    }
}


SshOperationHandle
ssh_proxy_rsa_sign_none_none_async(const void *private_key,
                                   SshRGF rgf,
                                   SshPrivateKeySignCB callback,
                                   void *context)
{
  return ssh_proxy_sign_async(private_key, SSH_RSA_NONE_NONE,
                              rgf, callback, context);
}

SshOperationHandle
ssh_proxy_rsa_sign_pkcs1_sha1_async(const void *private_key,
                                   SshRGF rgf,
                                   SshPrivateKeySignCB callback,
                                   void *context)
{
  return ssh_proxy_sign_async(private_key, SSH_RSA_PKCS1_SHA1,
                              rgf, callback, context);
}


SshOperationHandle
ssh_proxy_rsa_sign_pkcs1_md5_async(const void *private_key,
                                   SshRGF rgf,
                                   SshPrivateKeySignCB callback,
                                   void *context)
{
  return ssh_proxy_sign_async(private_key, SSH_RSA_PKCS1_MD5,
                              rgf, callback, context);
}


SshOperationHandle
ssh_proxy_rsa_sign_pkcs1_md2_async(const void *private_key,
                                   SshRGF rgf,
                                   SshPrivateKeySignCB callback,
                                   void *context)
{
  return ssh_proxy_sign_async(private_key, SSH_RSA_PKCS1_MD2,
                              rgf, callback, context);
}

SshOperationHandle
ssh_proxy_rsa_sign_pkcs1_none_async(const void *private_key,
                                    SshRGF rgf,
                                    SshPrivateKeySignCB callback,
                                    void *context)
{
  return ssh_proxy_sign_async(private_key, SSH_RSA_PKCS1_NONE,
                              rgf, callback, context);
}

SshOperationHandle
ssh_proxy_dsa_sign_none_none_async(const void *private_key,
                                   SshRGF rgf,
                                   SshPrivateKeySignCB callback,
                                   void *context)
{
  return ssh_proxy_sign_async(private_key, SSH_DSA_NONE_NONE,
                              rgf, callback, context);
}

SshOperationHandle
ssh_proxy_dsa_sign_nist_sha1_async(const void *private_key,
                                   SshRGF rgf,
                                   SshPrivateKeySignCB callback,
                                   void *context)
{
  return ssh_proxy_sign_async(private_key, SSH_DSA_NIST_SHA1,
                              rgf, callback, context);
}


/************************* The Proxy Verification Scheme. ****************/


typedef struct ProxyKeyVerifyContextRec {
  SshOperationHandle op;
  SshOperationHandle sub_op;
  ProxyKey key;
  unsigned char *buf;
  size_t buf_len;
  SshPublicKeyVerifyCB callback;
  void *context;
} *ProxyKeyVerifyContext;


void ssh_proxy_verify_abort(void *context)
{
  ProxyKeyVerifyContext ctx = context;

  ssh_operation_abort(ctx->sub_op);

  ssh_free(ctx->buf);
  ssh_free(ctx);
}

void ssh_proxy_verify_free(void *context)
{
  ProxyKeyVerifyContext ctx = context;

  ssh_operation_unregister(ctx->op);
  ssh_proxy_verify_abort(ctx);
}

/* 'data' is ignored */
void ssh_proxy_verify_op_done(SshCryptoStatus status,
                              const unsigned char *data,
                              size_t data_len,
                              void *context)
{
  ProxyKeyVerifyContext verify_ctx = context;

  verify_ctx->sub_op = NULL;

  (*verify_ctx->callback)(status, verify_ctx->context);

  ssh_proxy_verify_free(verify_ctx);
}

SshOperationHandle
ssh_proxy_verify_async(const void *public_key,
                       SshProxyRGFId rgf_id,
                       const unsigned char *signature,
                       size_t signature_len,
                       SshRGF rgf,
                       SshPublicKeyVerifyCB callback,
                       void *context)
{
  ProxyKeyVerifyContext verify_ctx;
  SshProxyOperationId operation_id;
  SshOperationHandle sub_op;
  unsigned char *data, *buf;
  size_t data_len, buf_len;
  SshCryptoStatus status;
  ProxyKey key = (ProxyKey) public_key;

  /* Determine the operation id */
  if (key->key_type == SSH_PROXY_RSA)
    {
      operation_id = SSH_RSA_PUB_VERIFY;
    }
  else if (key->key_type == SSH_PROXY_DSA)
    {
      operation_id = SSH_DSA_PUB_VERIFY;
    }
  else
    {
      (*callback)(SSH_CRYPTO_UNSUPPORTED, context);
      return NULL;
    }

  if ((verify_ctx = ssh_calloc(1, sizeof(*verify_ctx))) != NULL)
    {
      verify_ctx->callback = callback;
      verify_ctx->context = context;
      verify_ctx->key = key;

      /* Register the abort operation. */
      verify_ctx->op = ssh_operation_register(ssh_proxy_verify_abort,
                                              verify_ctx);

      if ((status = ssh_rgf_for_signature(rgf,
                                          (size_t) -1,
                                          &data, &data_len))
          != SSH_CRYPTO_OK)
        {
          (*callback)(status, context);
          ssh_proxy_verify_free(verify_ctx);
          return NULL;
        }

     /* If the hash is already done, only pass the pad name to the cb. */
      if (ssh_rgf_data_is_digest(rgf))
        rgf_id = get_rgf_pad_id(rgf_id);

      if (rgf_id == SSH_INVALID_RGF)
        {
          (*callback)(SSH_CRYPTO_UNSUPPORTED, context);
          ssh_proxy_verify_free(verify_ctx);

          return NULL;
        }

      /* Encode the data followed by the signature */
      buf_len =
        ssh_encode_array_alloc(&buf,
                               SSH_FORMAT_UINT32_STR, data, data_len,
                               SSH_FORMAT_UINT32_STR, signature, signature_len,
                               SSH_FORMAT_END);
      /* no memory */
      if (!buf)
        {
          (*callback)(SSH_CRYPTO_NO_MEMORY, context);
          ssh_proxy_verify_free(verify_ctx);
          return NULL;
        }

      verify_ctx->buf = buf;
      verify_ctx->buf_len = buf_len;

      sub_op = (*key->key_operation)(operation_id,
                                     rgf_id,
                                     key->handle,
                                     buf,
                                     buf_len,
                                     ssh_proxy_verify_op_done,
                                     verify_ctx,
                                     key->handle->base->context);


      if (sub_op)
        {
          verify_ctx->sub_op = sub_op;
          return verify_ctx->op;
        }
      return NULL;
    }
  else
    {
      (*callback)(SSH_CRYPTO_NO_MEMORY, context);
      return NULL;
    }
}


SshOperationHandle
ssh_proxy_rsa_verify_none_none_async(const void *public_key,
                                     const unsigned char *signature,
                                     size_t signature_len,
                                     SshRGF rgf,
                                     SshPublicKeyVerifyCB callback,
                                     void *context)
{
  return ssh_proxy_verify_async(public_key,
                                SSH_RSA_NONE_NONE,
                                signature,
                                signature_len,
                                rgf,
                                callback,
                                context);
}

SshOperationHandle
ssh_proxy_rsa_verify_pkcs1_sha1_async(const void *public_key,
                                      const unsigned char *signature,
                                      size_t signature_len,
                                      SshRGF rgf,
                                      SshPublicKeyVerifyCB callback,
                                      void *context)
{
  return ssh_proxy_verify_async(public_key,
                                SSH_RSA_PKCS1_SHA1,
                                signature,
                                signature_len,
                                rgf,
                                callback,
                                context);
}

SshOperationHandle
ssh_proxy_rsa_verify_pkcs1_md5_async(const void *public_key,
                                     const unsigned char *signature,
                                     size_t signature_len,
                                     SshRGF rgf,
                                     SshPublicKeyVerifyCB callback,
                                     void *context)
{
  return ssh_proxy_verify_async(public_key,
                                SSH_RSA_PKCS1_MD5,
                                signature,
                                signature_len,
                                rgf,
                                callback,
                                context);
}
SshOperationHandle
ssh_proxy_rsa_verify_pkcs1_md2_async(const void *public_key,
                                     const unsigned char *signature,
                                     size_t signature_len,
                                     SshRGF rgf,
                                     SshPublicKeyVerifyCB callback,
                                     void *context)
{
  return ssh_proxy_verify_async(public_key,
                                SSH_RSA_PKCS1_MD2,
                                signature,
                                signature_len,
                                rgf,
                                callback,
                                context);
}

SshOperationHandle
ssh_proxy_rsa_verify_pkcs1_none_async(const void *public_key,
                                      const unsigned char *signature,
                                      size_t signature_len,
                                      SshRGF rgf,
                                      SshPublicKeyVerifyCB callback,
                                      void *context)
{
  return ssh_proxy_verify_async(public_key,
                                SSH_RSA_PKCS1_NONE,
                                signature,
                                signature_len,
                                rgf,
                                callback,
                                context);
}


SshOperationHandle
ssh_proxy_dsa_verify_none_none_async(const void *public_key,
                                     const unsigned char *signature,
                                     size_t signature_len,
                                     SshRGF rgf,
                                     SshPublicKeyVerifyCB callback,
                                     void *context)
{
  return ssh_proxy_verify_async(public_key,
                                SSH_DSA_NONE_NONE,
                                signature,
                                signature_len,
                                rgf,
                                callback,
                                context);

}

SshOperationHandle
ssh_proxy_dsa_verify_nist_sha1_async(const void *public_key,
                                     const unsigned char *signature,
                                     size_t signature_len,
                                     SshRGF rgf,
                                     SshPublicKeyVerifyCB callback,
                                     void *context)
{
  return ssh_proxy_verify_async(public_key,
                                SSH_DSA_NIST_SHA1,
                                signature,
                                signature_len,
                                rgf,
                                callback,
                                context);
}



/******************** The Proxy Key Encryption Scheme. **************/

typedef struct ProxyKeyEncryptContextRec {
  SshOperationHandle op;
  SshOperationHandle sub_op;
  ProxyKey key;
  SshPublicKeyEncryptCB callback;
  void *context;
} *ProxyKeyEncryptContext;


void ssh_proxy_encrypt_abort(void *context)
{
  ProxyKeyEncryptContext ctx = context;

  ssh_operation_abort(ctx->sub_op);
  ssh_free(ctx);
}

void ssh_proxy_encrypt_free(void *context)
{
  ProxyKeyEncryptContext ctx = context;

  ssh_operation_unregister(ctx->op);
  ssh_proxy_encrypt_abort(context);
}

void ssh_proxy_encrypt_op_done(SshCryptoStatus status,
                               const unsigned char *data,
                               size_t data_len,
                               void *context)
{
  ProxyKeyEncryptContext encrypt_ctx = context;

  encrypt_ctx->sub_op = NULL;

  (*encrypt_ctx->callback)(status,
                           data,
                           data_len,
                           encrypt_ctx->context);

  ssh_proxy_encrypt_free(encrypt_ctx);
}

SshOperationHandle
ssh_proxy_encrypt_async(const void *public_key,
                        SshProxyRGFId rgf_id,
                        const unsigned char *plaintext,
                        size_t plaintext_len,
                        SshRGF rgf,
                        SshPublicKeyEncryptCB callback,
                        void *context)
{
  ProxyKeyEncryptContext encrypt_ctx;
  SshProxyOperationId operation_id;
  SshOperationHandle sub_op;
  ProxyKey key = (ProxyKey)public_key;

  /* Determine the operation id. */
  if (key->key_type == SSH_PROXY_RSA)
    {
      operation_id = SSH_RSA_PUB_ENCRYPT;
    }
  else
    {
      (*callback)(SSH_CRYPTO_UNSUPPORTED, NULL, 0, context);
      return NULL;
    }

  if ((encrypt_ctx = ssh_calloc(1, sizeof(*encrypt_ctx))) != NULL)
    {
      encrypt_ctx->callback = callback;
      encrypt_ctx->context = context;
      encrypt_ctx->key = key;

      /* Register the abort operation. */
      encrypt_ctx->op = ssh_operation_register(ssh_proxy_encrypt_abort,
                                               encrypt_ctx);

      sub_op = (*key->key_operation)(operation_id,
                                     rgf_id,
                                     key->handle,
                                     plaintext,
                                     plaintext_len,
                                     ssh_proxy_encrypt_op_done,
                                     encrypt_ctx,
                                     key->handle->base->context);

      if (sub_op)
        {
          encrypt_ctx->sub_op = sub_op;
          return encrypt_ctx->op;
        }
      return NULL;
    }
  else
    {
      (*callback)(SSH_CRYPTO_NO_MEMORY, NULL, 0, context);
      return NULL;
    }
}

SshOperationHandle
ssh_proxy_rsa_encrypt_none_none_async(const void *public_key,
                                      const unsigned char *plaintext,
                                      size_t plaintext_len,
                                      SshRGF rgf,
                                      SshPublicKeyEncryptCB callback,
                                      void *context)
{
  return ssh_proxy_encrypt_async(public_key,
                                 SSH_RSA_NONE_NONE,
                                 plaintext,
                                 plaintext_len,
                                 rgf,
                                 callback,
                                 context);
}


SshOperationHandle
ssh_proxy_rsa_encrypt_pkcs1v2_oaep_async(const void *public_key,
                                         const unsigned char *plaintext,
                                         size_t plaintext_len,
                                         SshRGF rgf,
                                         SshPublicKeyEncryptCB callback,
                                         void *context)
{
  return ssh_proxy_encrypt_async(public_key,
                                     SSH_RSA_PKCS1V2_OAEP,
                                     plaintext,
                                     plaintext_len,
                                     rgf,
                                     callback,
                                     context);
}

SshOperationHandle
ssh_proxy_rsa_encrypt_pkcs1_none_async(const void *public_key,
                                       const unsigned char *plaintext,
                                       size_t plaintext_len,
                                       SshRGF rgf,
                                       SshPublicKeyEncryptCB callback,
                                       void *context)
{
  return ssh_proxy_encrypt_async(public_key,
                                 SSH_RSA_PKCS1_NONE,
                                 plaintext,
                                 plaintext_len,
                                 rgf,
                                 callback,
                                 context);
}

/************** The Proxy Key Decryption Scheme. *******************/

typedef struct ProxyKeyDecryptContextRec {
  SshOperationHandle op;
  SshOperationHandle sub_op;
  ProxyKey key;
  SshPrivateKeyDecryptCB callback;
  void *context;
} *ProxyKeyDecryptContext;

void ssh_proxy_decrypt_abort(void *context)
{
  ProxyKeyDecryptContext ctx = context;

  ssh_operation_abort(ctx->sub_op);
  ssh_free(ctx);
}

void ssh_proxy_decrypt_free(void *context)
{
  ProxyKeyDecryptContext ctx = context;

  ssh_operation_unregister(ctx->op);
  ssh_proxy_decrypt_abort(context);
}

void ssh_proxy_decrypt_op_done(SshCryptoStatus status,
                               const unsigned char *data,
                               size_t data_len,
                               void *context)
{
  ProxyKeyDecryptContext decrypt_ctx = context;

  decrypt_ctx->sub_op = NULL;
  (*decrypt_ctx->callback)(status, data, data_len, decrypt_ctx->context);

  ssh_proxy_decrypt_free(decrypt_ctx);
}

SshOperationHandle
ssh_proxy_decrypt_async(const void *private_key,
                        SshProxyRGFId rgf_id,
                        const unsigned char *ciphertext,
                        size_t ciphertext_len,
                        SshRGF rgf,
                        SshPrivateKeyDecryptCB callback,
                        void *context)
{
  ProxyKeyDecryptContext decrypt_ctx;
  SshProxyOperationId operation_id;
  SshOperationHandle sub_op;
  ProxyKey key = (ProxyKey)private_key;

  /* Determine the operation id. */
  if (key->key_type == SSH_PROXY_RSA)
    {
      operation_id = SSH_RSA_PRV_DECRYPT;
    }
  else
    {
      (*callback)(SSH_CRYPTO_UNSUPPORTED, NULL, 0, context);
      return NULL;
    }

  if ((decrypt_ctx = ssh_calloc(1, sizeof(*decrypt_ctx))) != NULL)
    {
      decrypt_ctx->callback = callback;
      decrypt_ctx->context = context;
      decrypt_ctx->key = key;

      /* Register the abort operation. */
      decrypt_ctx->op = ssh_operation_register(ssh_proxy_decrypt_abort,
                                               decrypt_ctx);

      sub_op = (*key->key_operation)(operation_id,
                                     rgf_id,
                                     key->handle,
                                     ciphertext,
                                     ciphertext_len,
                                     ssh_proxy_decrypt_op_done,
                                     decrypt_ctx,
                                     key->handle->base->context);

      if (sub_op)
        {
          decrypt_ctx->sub_op = sub_op;
          return decrypt_ctx->op;
        }
      return NULL;
    }
  else
    {
      (*callback)(SSH_CRYPTO_NO_MEMORY, NULL, 0, context);
      return NULL;
    }
}

SshOperationHandle
ssh_proxy_rsa_decrypt_none_none_async(const void *private_key,
                                      const unsigned char *ciphertext,
                                      size_t ciphertext_len,
                                      SshRGF rgf,
                                      SshPrivateKeyDecryptCB callback,
                                      void *context)
{
  return ssh_proxy_decrypt_async(private_key,
                                 SSH_RSA_NONE_NONE,
                                 ciphertext,
                                 ciphertext_len,
                                 rgf,
                                 callback,
                                 context);
}

SshOperationHandle
ssh_proxy_rsa_decrypt_pkcs1v2_oaep_async(const void *private_key,
                                         const unsigned char *ciphertext,
                                         size_t ciphertext_len,
                                         SshRGF rgf,
                                         SshPrivateKeyDecryptCB callback,
                                         void *context)
{
  return ssh_proxy_decrypt_async(private_key,
                                 SSH_RSA_PKCS1V2_OAEP,
                                 ciphertext,
                                 ciphertext_len,
                                 rgf,
                                 callback,
                                 context);
}


SshOperationHandle
ssh_proxy_rsa_decrypt_pkcs1_none_async(const void *private_key,
                                       const unsigned char *ciphertext,
                                       size_t ciphertext_len,
                                       SshRGF rgf,
                                       SshPrivateKeyDecryptCB callback,
                                       void *context)
{
  return ssh_proxy_decrypt_async(private_key,
                                 SSH_RSA_PKCS1_NONE,
                                 ciphertext,
                                 ciphertext_len,
                                 rgf,
                                 callback,
                                 context);
}



/* ****************** Diffie-Hellman Groups ********************* */




/* *************** Diffie-Hellman Setup Scheme ******************* */

typedef struct ProxyDHSetupContextRec {
  SshOperationHandle op;
  SshOperationHandle sub_op;
  ProxyKey group;
  SshPkGroupDHSetup callback;
  void *context;
} *ProxyDHSetupContext;


void ssh_proxy_dh_setup_abort(void *context)
{
  ProxyDHSetupContext ctx = context;

  ssh_operation_abort(ctx->sub_op);
  ssh_free(ctx);
}

void ssh_proxy_dh_setup_free(void *context)
{
  ProxyDHSetupContext ctx = context;

  ssh_operation_unregister(ctx->op);
  ssh_proxy_dh_setup_abort(ctx);
}


void ssh_proxy_dh_setup_op_done(SshCryptoStatus status,
                                const unsigned char *operated_data,
                                size_t operated_data_len,
                                void *context)
{
  ProxyDHSetupContext setup_ctx = context;
  ProxyKey group;
  SshPkGroupDHSecret dh_secret;
  unsigned char *exchange, *secret;
  size_t exchange_len, secret_len;

  group = setup_ctx->group;
  setup_ctx->sub_op = NULL;

  if (status != SSH_CRYPTO_OK)
    {
      (*setup_ctx->callback)(status, NULL, NULL, 0, setup_ctx->context);
      ssh_proxy_dh_setup_free(setup_ctx);
      return;
    }

  if (ssh_decode_array(operated_data, operated_data_len,
                       SSH_FORMAT_UINT32_STR_NOCOPY, &exchange, &exchange_len,
                       SSH_FORMAT_UINT32_STR_NOCOPY, &secret, &secret_len,
                       SSH_FORMAT_END) != operated_data_len)
    {
      (*setup_ctx->callback)(SSH_CRYPTO_OPERATION_FAILED, NULL, NULL,
                             0, setup_ctx->context);
      ssh_proxy_dh_setup_free(setup_ctx);
      return;
    }

  if (((dh_secret = ssh_malloc(sizeof(*dh_secret))) == NULL) || 
      ((dh_secret->buf = ssh_malloc(secret_len)) == NULL))
    {
      ssh_free(dh_secret);
      (*setup_ctx->callback)(SSH_CRYPTO_NO_MEMORY, NULL, NULL,
                             0, setup_ctx->context);
      ssh_proxy_dh_setup_free(setup_ctx);
      return;
    }

  dh_secret->len = secret_len;
  memcpy(dh_secret->buf, secret, secret_len);
  
  (*setup_ctx->callback)(SSH_CRYPTO_OK,
                         dh_secret,
                         exchange,
                         exchange_len,
                         setup_ctx->context);

  ssh_proxy_dh_setup_free(setup_ctx);
}

SshOperationHandle
ssh_proxy_dh_setup_async(void *pk_group,
                         SshPkGroupDHSetup callback,
                         void *context)
{
  SshProxyOperationId operation_id;
  ProxyDHSetupContext setup_ctx;
  SshOperationHandle sub_op;
  ProxyKey group = (ProxyKey)pk_group;

  /* Determine the operation id. */
  if (group->key_type == SSH_PROXY_GROUP)
    {
      operation_id = SSH_DH_SETUP;
    }
  else
    {
      (*callback)(SSH_CRYPTO_UNSUPPORTED, NULL, NULL, 0, context);
      return NULL;
    }

  if ((setup_ctx = ssh_calloc(1, sizeof(*setup_ctx))) != NULL)
    {
      setup_ctx->callback = callback;
      setup_ctx->context = context;
      setup_ctx->group = (ProxyKey)pk_group;

      /* Register the abort callback. */
      setup_ctx->op = ssh_operation_register(ssh_proxy_dh_setup_abort,
                                             setup_ctx);

      /* No raw data is given to the proxy callback operation. It is the
         responsibility of the callback operation to generate the
         private Diffie-Hellman exponent. */
      sub_op = (*group->key_operation)(operation_id,
                                       SSH_DH_NONE_NONE,
                                       group->handle,
                                       NULL,
                                       0,
                                       ssh_proxy_dh_setup_op_done,
                                       setup_ctx,
                                       group->handle->base->context);

      if (sub_op)
        {
          setup_ctx->sub_op = sub_op;
          return setup_ctx->op;
        }
      return NULL;
    }
  else
    {
      (*callback)(SSH_CRYPTO_NO_MEMORY, NULL, NULL, 0, context);
      return NULL;
    }
}

/* ********************** Diffie-Hellman Agree Scheme ******************* */

typedef struct ProxyDHAgreeContextRec {
  SshOperationHandle op;
  SshOperationHandle sub_op;
  ProxyKey group;
  SshPkGroupDHAgree callback;
  void *context;
} *ProxyDHAgreeContext;


void ssh_proxy_dh_agree_abort(void *context)
{
  ProxyDHAgreeContext ctx = context;

  ssh_operation_abort(ctx->sub_op);
  ssh_free(ctx);
}

void ssh_proxy_dh_agree_free(void *context)
{
  ProxyDHAgreeContext ctx = context;

  ssh_operation_unregister(ctx->op);
  ssh_proxy_dh_agree_abort(ctx);
}


void ssh_proxy_dh_agree_op_done(SshCryptoStatus status,
                                const unsigned char *data,
                                size_t data_len,
                                void *context)
{
  ProxyDHAgreeContext agree_ctx = context;

  agree_ctx->sub_op = NULL;

  (*agree_ctx->callback)(status, data, data_len, agree_ctx->context);

  ssh_proxy_dh_agree_free(agree_ctx);
}

/* This function frees dh_extra. */
SshOperationHandle
ssh_proxy_dh_agree_async(const void *pk_group,
                         SshPkGroupDHSecret dh_secret,
                         const unsigned char *exchange,
                         size_t exchange_len,
                         SshPkGroupDHAgree callback,
                         void *context)
{
  SshProxyOperationId operation_id;
  ProxyDHAgreeContext agree_ctx;
  SshOperationHandle sub_op;
  ProxyKey group = (ProxyKey)pk_group;
  unsigned char *buffer, *dh_buf;
  size_t buffer_len, dh_len;

  /* Determine the operation id. */
  if (group->key_type == SSH_PROXY_GROUP)
    {
      operation_id = SSH_DH_AGREE;
    }
  else
    {
      (*callback)(SSH_CRYPTO_UNSUPPORTED, NULL, 0, context);
      return NULL;
    }

  if (dh_secret == NULL || dh_secret->buf == NULL)
    {
      (*callback)(SSH_CRYPTO_OPERATION_FAILED, NULL, 0, context);
      return NULL;
    }

  dh_len = dh_secret->len;
  dh_buf = dh_secret->buf;

  if ((agree_ctx = ssh_calloc(1, sizeof(*agree_ctx))) != NULL)
    {
      agree_ctx->callback = callback;
      agree_ctx->context = context;
      agree_ctx->group = group;

      /* Register the abort operation. */
      agree_ctx->op = ssh_operation_register(ssh_proxy_dh_agree_abort,
                                             agree_ctx);

      /* Encode the exchange followed by the DH secret */
      buffer_len =
        ssh_encode_array_alloc(&buffer,
                               SSH_FORMAT_UINT32_STR, exchange, exchange_len,
                               SSH_FORMAT_UINT32_STR, dh_buf, dh_len,
                               SSH_FORMAT_END);
      /* no memory */
      if (!buffer)
        {
          (*callback)(SSH_CRYPTO_NO_MEMORY, NULL, 0, context);
          ssh_pk_group_dh_secret_free(dh_secret);
          ssh_proxy_dh_agree_free(agree_ctx);
          return NULL;
        }

      /* Free our side's secret exponent. */
      ssh_pk_group_dh_secret_free(dh_secret);

      sub_op = (*group->key_operation)(operation_id,
                                       SSH_DH_NONE_NONE,
                                       group->handle,
                                       buffer,
                                       buffer_len,
                                       ssh_proxy_dh_agree_op_done,
                                       agree_ctx,
                                       group->handle->base->context);

      ssh_free(buffer);

      if (sub_op)
        {
          agree_ctx->sub_op = sub_op;
          return agree_ctx->op;
        }
      return NULL;
    }
  else
    {
      (*callback)(SSH_CRYPTO_NO_MEMORY, NULL, 0, context);
      return NULL;
    }
}


/*************  Utility Functions. ********************/


typedef struct ProxyKeyActionRec {
  void *proxykey;
} *ProxyKeyAction;



SshCryptoStatus ssh_proxy_key_action_init(void **context)
{
  if ((*context = ssh_calloc(1, sizeof(struct ProxyKeyActionRec))) == NULL)
    return SSH_CRYPTO_NO_MEMORY;

  return SSH_CRYPTO_OK;
}

const char *ssh_proxy_key_action_put(void *context,
                                     va_list ap,
                                     void *input_context,
                                     SshPkFormat format)
{
  ProxyKeyAction ctx = context;
  char *r;

  r = "p";
  switch (format)
    {
    case SSH_PKF_PROXY:
      ctx->proxykey = va_arg(ap, void *);
      r = "p";
      break;
    default:
      r = NULL;
    }
  return r;
}

SshCryptoStatus ssh_proxy_key_action_make(void *context, void **key_ctx)
{
  ProxyKeyAction act = context;
  *key_ctx = act->proxykey;

  return SSH_CRYPTO_OK;
}

void ssh_proxy_key_action_free(void *context)
{
  ssh_free(context);
}

void ssh_proxy_key_free(void *key)
{
  ProxyKey proxykey = key;

  SSH_DEBUG(SSH_D_LOWOK, ("Are now freeing a proxy key."));

  if (proxykey->handle->base->ref_count == 0)
    {
      SSH_DEBUG(SSH_D_LOWOK, ("Are now calling the proxy key destructor."));
      proxykey->free_operation(proxykey->handle->base->context);
      ssh_free(proxykey->handle->base);
    }
  else
    {
      proxykey->handle->base->ref_count--;
    }

  ssh_free(proxykey->handle);
  ssh_free(proxykey);
}

SshCryptoStatus
ssh_proxy_key_set_key_pointer_to_context(void *key, void *context)
{
  ProxyKey proxykey = context;

  proxykey->handle->key_addr = key;

  return SSH_CRYPTO_OK;
}

SshCryptoStatus
ssh_proxy_private_key_derive_public_key(const void *private_key,
                                        void **public_key)
{
  return SSH_CRYPTO_UNSUPPORTED;
}

SshCryptoStatus
ssh_proxy_key_copy(void *op_src, void **op_dest)
{
  /* Make copying explicit. */
  ProxyKey key = op_src, dest;

  *op_dest = NULL;

  if ((dest = ssh_calloc(1, sizeof(*dest))) == NULL)
    return SSH_CRYPTO_NO_MEMORY;

  memcpy(dest, key, sizeof(*dest));

  if ((dest->handle = ssh_calloc(1, sizeof(struct SshProxyKeyHandleRec)))
      == NULL)
    {
      ssh_free(dest);
      return SSH_CRYPTO_NO_MEMORY;
    }

  memcpy(dest->handle, key->handle, sizeof(struct SshProxyKeyHandleRec));

  key->handle->base->ref_count++;

  *op_dest = dest;

  return SSH_CRYPTO_OK;
}


void * ssh_proxy_key_get_key_handle(SshProxyKeyHandle handle)
{
  return (void *)handle->key_addr;
}


/* Compute sizes needed in each RSA operation, these agree with those in
   rsa-generate.c  */

#define SSH_RSA_MINIMUM_PADDING 10
#define SSH_RSA_MAX_BYTES       65535

size_t ssh_proxy_rsa_max_encrypt_input_len(const void *public_key,
                                           SshRGF rgf)
{
  ProxyKey ctx = (ProxyKey)public_key;
  size_t len;

  len = KEY_SIZE_TO_BYTES(ctx->key_size) - 3 - SSH_RSA_MINIMUM_PADDING;

  if (len > 0 && len < SSH_RSA_MAX_BYTES)
    return len;
  return 0;
}

size_t ssh_proxy_rsa_max_oaep_sha1_encrypt_input_len(const void *public_key,
                                                     SshRGF rgf)
{
  ProxyKey ctx = (ProxyKey)public_key;
  size_t len;

#if 0
  len = (KEY_SIZE_TO_BYTES(ctx->key_size) - 2 - 
         2 * ssh_rgf_hash_digest_length(rgf));
#endif 

  /* The commented line above is not valid, since the RGF in use in 
     sshproxykey are all dummy RGF's. But for OAEP we need the hash 
     digest length in order to compute the maximum encrypt input 
     length. For SHA1 the hash digest is 20 bytes.*/
  len = KEY_SIZE_TO_BYTES(ctx->key_size) - 2 - (2 * 20); 

  SSH_DEBUG(7, ("The max OAEP public key encrypt input len is %d "
                "with key size %d and digest length %d", len, 
                ctx->key_size, ssh_rgf_hash_digest_length(rgf)));
  
  if (len > 0 && len < SSH_RSA_MAX_BYTES)
    return len;
  return 0;
}

size_t ssh_proxy_rsa_max_none_encrypt_input_len(const void *public_key,
                                                SshRGF rgf)
{
  ProxyKey ctx = (ProxyKey)public_key;
  size_t len = KEY_SIZE_TO_BYTES(ctx->key_size);

  if (len > 0 && len < SSH_RSA_MAX_BYTES)
    return len;
  return 0;
}

size_t ssh_proxy_rsa_max_decrypt_input_len(const void *private_key,
                                           SshRGF rgf)
{
  ProxyKey ctx = (ProxyKey)private_key;
  return KEY_SIZE_TO_BYTES(ctx->key_size);
}

size_t ssh_proxy_rsa_max_signature_input_len(const void *private_key,
                                             SshRGF rgf)
{
  return (size_t)-1;
}

size_t ssh_proxy_rsa_max_signature_unhash_input_len(const void *private_key,
                                                    SshRGF rgf)
{
  ProxyKey ctx = (ProxyKey)private_key;
  size_t len = KEY_SIZE_TO_BYTES(ctx->key_size) - 3 - SSH_RSA_MINIMUM_PADDING;

  if (len > 0 && len < SSH_RSA_MAX_BYTES)
    return len;
  return 0;
}

size_t ssh_proxy_rsa_max_encrypt_output_len(const void *public_key,
                                            SshRGF rgf)
{
  ProxyKey ctx = (ProxyKey)public_key;
  return KEY_SIZE_TO_BYTES(ctx->key_size);
}

size_t ssh_proxy_rsa_max_decrypt_output_len(const void *private_key,
                                            SshRGF rgf)
{
  ProxyKey ctx = (ProxyKey)private_key;
  return KEY_SIZE_TO_BYTES(ctx->key_size);
}


size_t ssh_proxy_rsa_max_signature_output_len(const void *private_key,
                                              SshRGF rgf)
{
  ProxyKey ctx = (ProxyKey)private_key;
  return KEY_SIZE_TO_BYTES(ctx->key_size);
}

/* Compute sizes needed in each DSA and DH operation, these agree with those
   in dl-dh.c and dl-dsa.c */
size_t ssh_proxy_dsa_max_signature_input_len(const void *private_key,
                                             SshRGF rgf)
{
  return (size_t)-1;
}

size_t ssh_proxy_dsa_max_signature_output_len(const void *private_key,
                                              SshRGF rgf)
{
  ProxyKey key = (ProxyKey)private_key;
  return KEY_SIZE_TO_BYTES(key->key_size) * 2;

}

size_t ssh_proxy_diffie_hellman_exchange_length(const void *parameters)
{
  ProxyKey key = (ProxyKey)parameters;
  return KEY_SIZE_TO_BYTES(key->key_size);
}

size_t ssh_proxy_diffie_hellman_shared_secret_length(const void *parameters)
{
  ProxyKey key = (ProxyKey)parameters;
  return KEY_SIZE_TO_BYTES(key->key_size);
}


/************************ RSA Actions and Schemes *************************/

const SshPkAction ssh_proxy_key_if_modn_actions[] =
{
  { SSH_PKF_KEY_TYPE,
    SSH_PK_ACTION_FLAG_KEY_TYPE | SSH_PK_ACTION_FLAG_PRIVATE_KEY |
    SSH_PK_ACTION_FLAG_PUBLIC_KEY,
    0, NULL_FNPTR, 0, NULL_FNPTR },

  { SSH_PKF_PROXY,
    SSH_PK_ACTION_FLAG_GET_PUT | SSH_PK_ACTION_FLAG_PRIVATE_KEY |
    SSH_PK_ACTION_FLAG_PUBLIC_KEY,
    0, ssh_proxy_key_action_put,
    0, NULL_FNPTR },

  { SSH_PKF_END }
};

const SshPkSignature ssh_proxy_key_if_modn_signature_schemes[] =
{
  { "rsa-pkcs1-sha1",
    0,
    &ssh_rgf_dummy_no_allocate_def,
    ssh_proxy_rsa_max_signature_input_len,
    ssh_proxy_rsa_max_signature_output_len,
    NULL_FNPTR,
    ssh_proxy_rsa_verify_pkcs1_sha1_async,
    NULL_FNPTR,
    ssh_proxy_rsa_sign_pkcs1_sha1_async },
  { "rsa-pkcs1-md5",
    0,
    &ssh_rgf_dummy_no_allocate_def,
    ssh_proxy_rsa_max_signature_input_len,
    ssh_proxy_rsa_max_signature_output_len,
    NULL_FNPTR,
    ssh_proxy_rsa_verify_pkcs1_md5_async,
    NULL_FNPTR,
    ssh_proxy_rsa_sign_pkcs1_md5_async },
#ifdef SSHDIST_CRYPT_MD2
 { "rsa-pkcs1-md2",
    0,
    &ssh_rgf_dummy_no_allocate_def,
    ssh_proxy_rsa_max_signature_input_len,
    ssh_proxy_rsa_max_signature_output_len,
    NULL_FNPTR,
    ssh_proxy_rsa_verify_pkcs1_md2_async,
    NULL_FNPTR,
    ssh_proxy_rsa_sign_pkcs1_md2_async },
#endif /* SSHDIST_CRYPT_MD2 */
  { "rsa-pkcs1-none",
    0,
    &ssh_rgf_dummy_no_allocate_def,
    ssh_proxy_rsa_max_signature_unhash_input_len,
    ssh_proxy_rsa_max_signature_output_len,
    NULL_FNPTR,
    ssh_proxy_rsa_verify_pkcs1_none_async,
    NULL_FNPTR,
    ssh_proxy_rsa_sign_pkcs1_none_async },
  { NULL }
};

const SshPkEncryption ssh_proxy_key_if_modn_encryption_schemes[] =
{
#ifdef SSHDIST_CRYPT_SHA
  { "rsa-pkcs1v2-oaep",
    0,
    &ssh_rgf_dummy_no_allocate_def,
    ssh_proxy_rsa_max_decrypt_input_len,
    ssh_proxy_rsa_max_decrypt_output_len,
    NULL_FNPTR,
    ssh_proxy_rsa_decrypt_pkcs1v2_oaep_async,
    ssh_proxy_rsa_max_oaep_sha1_encrypt_input_len,
    ssh_proxy_rsa_max_encrypt_output_len,
    NULL_FNPTR,
    ssh_proxy_rsa_encrypt_pkcs1v2_oaep_async
  },
  { "rsa-pkcs1-none",
    0,
    &ssh_rgf_dummy_no_allocate_def,
    ssh_proxy_rsa_max_decrypt_input_len,
    ssh_proxy_rsa_max_decrypt_output_len,
    NULL_FNPTR,
    ssh_proxy_rsa_decrypt_pkcs1_none_async,
    ssh_proxy_rsa_max_encrypt_input_len,
    ssh_proxy_rsa_max_encrypt_output_len,
    NULL_FNPTR,
    ssh_proxy_rsa_encrypt_pkcs1_none_async
  },
#endif /* SSHDIST_CRYPT_SHA */
  { "rsa-none-none",
    0,
    &ssh_rgf_dummy_no_allocate_def,
    ssh_proxy_rsa_max_decrypt_input_len,
    ssh_proxy_rsa_max_decrypt_output_len,
    NULL_FNPTR,
    ssh_proxy_rsa_decrypt_none_none_async,
    ssh_proxy_rsa_max_none_encrypt_input_len,
    ssh_proxy_rsa_max_encrypt_output_len,
    NULL_FNPTR,
    ssh_proxy_rsa_encrypt_none_none_async
  },

  { NULL }
};

const SshPkType ssh_proxy_key_if_modn =
{
  "proxy:if-modn",
  0, /* XXX FIPS */
  ssh_proxy_key_if_modn_actions,
  ssh_proxy_key_if_modn_signature_schemes,
  ssh_proxy_key_if_modn_encryption_schemes,
  NULL,

  /* No group operations */
  NULL_FNPTR, NULL_FNPTR, NULL_FNPTR,
  NULL_FNPTR, NULL_FNPTR, NULL_FNPTR, NULL_FNPTR, NULL_FNPTR, NULL_FNPTR,
  NULL_FNPTR, NULL_FNPTR, NULL_FNPTR, NULL_FNPTR,

  /* Public key operations */
  ssh_proxy_key_action_init,
  ssh_proxy_key_action_make,
  ssh_proxy_key_action_free,

  NULL_FNPTR,
  NULL_FNPTR,
  ssh_proxy_key_free,
  ssh_proxy_key_copy,
  NULL_FNPTR,
  NULL_FNPTR,

  /* Private key operations */
  ssh_proxy_key_action_init,
  ssh_proxy_key_action_make,
  ssh_proxy_key_action_make,
  ssh_proxy_key_action_free,

  NULL_FNPTR,
  NULL_FNPTR,
  ssh_proxy_key_free,
  ssh_proxy_private_key_derive_public_key,
  ssh_proxy_key_copy,
  NULL_FNPTR,
  NULL_FNPTR,
  ssh_proxy_key_set_key_pointer_to_context
};

/******************************* DSA Schemes ********************************/

const SshPkSignature ssh_proxy_key_dl_modp_signature_schemes[] =
{
  /* Use when the proxy callback wants to do the hashing. */
  { "dsa-nist-sha1",
    0,
    &ssh_rgf_dummy_no_allocate_def,
    ssh_proxy_dsa_max_signature_input_len,
    ssh_proxy_dsa_max_signature_output_len,
    NULL_FNPTR,
    ssh_proxy_dsa_verify_nist_sha1_async,
    NULL_FNPTR,
    ssh_proxy_dsa_sign_nist_sha1_async },

  { NULL }
};

/* No supported encryption schemes for dl-modp keys. */
const SshPkEncryption ssh_proxy_key_dl_modp_encryption_schemes[] =
{
  { NULL }
};



/************************ Diffie_Hellman Schemes. ************************/

#ifdef SSHDIST_CRYPT_DH
/* Table of all supported diffie-hellman schemes for proxy modp keys. */
const SshPkDiffieHellman ssh_proxy_group_dl_modp_diffie_hellman_schemes[] =
{
  { "plain",
    0,
    ssh_proxy_diffie_hellman_exchange_length,
    ssh_proxy_diffie_hellman_shared_secret_length,
    NULL_FNPTR,
    ssh_proxy_dh_setup_async,
    NULL_FNPTR,
    ssh_proxy_dh_agree_async
  },
  { NULL },
};
#endif /* SSHDIST_CRYPT_DH */



/**************************** DSA Actions and PkType *******************/

const SshPkAction ssh_proxy_key_dl_modp_actions[] =
{

  { SSH_PKF_KEY_TYPE,
    SSH_PK_ACTION_FLAG_KEY_TYPE | SSH_PK_ACTION_FLAG_PRIVATE_KEY |
    SSH_PK_ACTION_FLAG_PUBLIC_KEY | SSH_PK_ACTION_FLAG_PK_GROUP,
    0, NULL_FNPTR, 0, NULL_FNPTR },

  { SSH_PKF_PROXY,
    SSH_PK_ACTION_FLAG_GET_PUT | SSH_PK_ACTION_FLAG_PRIVATE_KEY |
    SSH_PK_ACTION_FLAG_PUBLIC_KEY | SSH_PK_ACTION_FLAG_PK_GROUP,
    0, ssh_proxy_key_action_put,
    0, NULL_FNPTR },

  { SSH_PKF_END }
};


const SshPkType ssh_proxy_key_dl_modp =
{
  "proxy:dl-modp",
  0, /* XXX FIPS */
  ssh_proxy_key_dl_modp_actions,
  ssh_proxy_key_dl_modp_signature_schemes,
  NULL,
#ifdef SSHDIST_CRYPT_DH
  ssh_proxy_group_dl_modp_diffie_hellman_schemes,
#else /* SSHDIST_CRYPT_DH */
  NULL,
#endif /* SSHDIST_CRYPT_DH */

  /* Group operations */
  ssh_proxy_key_action_init,
  ssh_proxy_key_action_make,
  ssh_proxy_key_action_free,

  /* Import, export */
  NULL_FNPTR, NULL_FNPTR,

  ssh_proxy_key_free,
  ssh_proxy_key_copy,

  /* No predefined or precompute */
  NULL_FNPTR, NULL_FNPTR,

  /* No randomizer handling */
  NULL_FNPTR, NULL_FNPTR, NULL_FNPTR, NULL_FNPTR,

  /* Public key operations */
  ssh_proxy_key_action_init,
  ssh_proxy_key_action_make,
  ssh_proxy_key_action_free,

  NULL_FNPTR,
  NULL_FNPTR,
  ssh_proxy_key_free,
  ssh_proxy_key_copy,
  NULL_FNPTR,
  NULL_FNPTR,

  /* Private key operations */
  ssh_proxy_key_action_init,
  ssh_proxy_key_action_make,
  ssh_proxy_key_action_make,
  ssh_proxy_key_action_free,

  NULL_FNPTR,
  NULL_FNPTR,
  ssh_proxy_key_free,
  ssh_proxy_private_key_derive_public_key,
  ssh_proxy_key_copy,
  NULL_FNPTR,
  NULL_FNPTR,
  ssh_proxy_key_set_key_pointer_to_context
};


/**************** Helper functions ***************************/

Boolean ssh_proxy_register(const SshPkType *type)
{
  if (ssh_pk_provider_register(type) != SSH_CRYPTO_OK)
    {
      SSH_DEBUG(0, ("Could not register proxy type"));
      return FALSE;
    }
    return TRUE;
}

Boolean ssh_register_proxy_key(SshProxyKeyTypeId key_type)
{
  switch (key_type)
    {
    case SSH_PROXY_RSA:
      return ssh_proxy_register(&ssh_proxy_key_if_modn);

    case SSH_PROXY_DSA:
    case SSH_PROXY_GROUP:
      return ssh_proxy_register(&ssh_proxy_key_dl_modp);

    default:
      return FALSE;
    }
}

static char *ssh_make_proxy_key_name(SshProxyKeyTypeId key_type)
{
  char *proxy_key_name;

  /* Return 'proxy:' prefix folowed by the the key type */
  switch (key_type)
    {
    case SSH_PROXY_RSA:
      proxy_key_name = ssh_strdup("proxy:if-modn");
      break;

    case SSH_PROXY_DSA:
      proxy_key_name = ssh_strdup("proxy:dl-modp{sign{dsa-nist-sha1}");
      break;
    case SSH_PROXY_GROUP:
      proxy_key_name = ssh_strdup("proxy:dl-modp{dh}");
      break;

    default:
      proxy_key_name = NULL;
    }

  return proxy_key_name;
}


/****************** The Proxy Key Generation Functions ********************/

SshPrivateKey ssh_private_key_create_proxy(SshProxyKeyTypeId key_type,
                                           SshUInt32 key_size_in_bits,
                                           SshProxyKeyOpCB key_operation,
                                           SshProxyFreeOpCB free_operation,
                                           void *context)
{
  SshPrivateKey key;
  ProxyKey proxykey;
  SshProxyKeyHandle proxykey_handle;
  SshProxyKeyBase base_ctx;
  char *proxy_name;

  /* Only RSA and DSA are supported. */
  if (key_type != SSH_PROXY_RSA && key_type != SSH_PROXY_DSA)
    return NULL;

  /* Register the proxy key. */
  if (ssh_register_proxy_key(key_type) == FALSE)
    return NULL;

  /* Construct the key name. */
  proxy_name = ssh_make_proxy_key_name(key_type);

  if (proxy_name == NULL || key_operation == NULL_FNPTR)
    return NULL;

  if ((base_ctx = ssh_calloc(1, sizeof(*base_ctx))) != NULL)
    {
      base_ctx->ref_count = 0;
      base_ctx->context = context;
    }
  else
    {
      ssh_free(proxy_name);
      return NULL;
    }

  if ((proxykey_handle = ssh_calloc(1, sizeof(*proxykey_handle))) != NULL)
    {
      proxykey_handle->base = base_ctx;
      proxykey_handle->key_addr = NULL;
    }
  else
    {
      ssh_free(base_ctx);
      ssh_free(proxy_name);
      return NULL;
    }

  if ((proxykey = ssh_calloc(1, sizeof(*proxykey))) != NULL)
    {
      proxykey->handle = proxykey_handle;
      proxykey->key_type = key_type;
      proxykey->key_operation = key_operation;
      proxykey->free_operation = free_operation;
      proxykey->key_size = key_size_in_bits;

      if (ssh_private_key_define(&key,
                                 proxy_name,
                                 SSH_PKF_PROXY, proxykey,
                                 SSH_PKF_END) != SSH_CRYPTO_OK)
        {
          SSH_DEBUG(SSH_D_FAIL, ("Error in defining the key"));
          ssh_free(proxy_name);
          return NULL;
        }
      ssh_free(proxy_name);

      return key;
    }
  else
    {
      ssh_free(proxykey_handle);
      ssh_free(base_ctx);
      ssh_free(proxy_name);
      return NULL;
    }
}


SshPublicKey ssh_public_key_create_proxy(SshProxyKeyTypeId key_type,
                                         SshUInt32 size_in_bits,
                                         SshProxyKeyOpCB key_operation,
                                         SshProxyFreeOpCB free_operation,
                                         void *context)
{
  SshPublicKey key;
  ProxyKey proxykey;
  SshProxyKeyHandle proxykey_handle;
  SshProxyKeyBase base_ctx;
  char *proxy_name;

  /* Only RSA and DSA are supported. */
  if (key_type != SSH_PROXY_RSA && key_type != SSH_PROXY_DSA)
    return NULL;

  /* Register the proxy key. */
  if (ssh_register_proxy_key(key_type) == FALSE)
    return NULL;

  /* Construct the key name. */
  proxy_name = ssh_make_proxy_key_name(key_type);

  if (proxy_name == NULL)
    return NULL;

  if ((base_ctx = ssh_calloc(1, sizeof(*base_ctx))) != NULL)
    {
      base_ctx->ref_count = 0;
      base_ctx->context = context;
    }
  else
    {
      ssh_free(proxy_name);
      return NULL;
    }

  if ((proxykey_handle = ssh_calloc(1, sizeof(*proxykey_handle))) != NULL)
    {
      proxykey_handle->base = base_ctx;
      proxykey_handle->key_addr = NULL;
    }
  else
    {
      ssh_free(base_ctx);
      ssh_free(proxy_name);
      return NULL;
    }

  if ((proxykey = ssh_calloc(1, sizeof(*proxykey))) != NULL)
    {
      proxykey->handle = proxykey_handle;
      proxykey->key_type = key_type;
      proxykey->key_operation = key_operation;
      proxykey->free_operation = free_operation;
      proxykey->key_size = size_in_bits;

      if (ssh_public_key_define(&key,
                                 proxy_name,
                                 SSH_PKF_PROXY, proxykey,
                                 SSH_PKF_END) != SSH_CRYPTO_OK)
        {
          SSH_DEBUG(SSH_D_FAIL, ("Error in defining the key"));
          ssh_free(proxy_name);
          return NULL;
        }
      ssh_free(proxy_name);
      return key;
    }
  else
    {
      ssh_free(proxykey_handle);
      ssh_free(base_ctx);
      ssh_free(proxy_name);
      return NULL;
    }
}


/* Create a proxy group for Diffie-Hellman usage. The returned group is of
   type group_type, which must be prefixed with "proxy:dl-modp".

   Calls the operation_cb with the data that is being operated when
   the library is performing crypto operations with the returned proxy group.

   The proxy group is freed with ssh_group_free. It is error to free a group
   that is currently being used. If name is supplied, it will set
   the scheme of the proxy group.
*/
SshPkGroup ssh_dh_group_create_proxy(SshProxyKeyTypeId key_type,
                                     SshUInt32 size_in_bits,
                                     SshProxyKeyOpCB key_operation,
                                     SshProxyFreeOpCB free_operation,
                                     void *context)
{
  SshPkGroup group;
  ProxyKey proxykey;
  SshProxyKeyHandle proxykey_handle;
  SshProxyKeyBase base_ctx;
  char *proxy_name;

  /* Only dl-modp groups are supported. */
  if (key_type != SSH_PROXY_GROUP)
    return NULL;

  /* Register the proxy key. */
  if (ssh_register_proxy_key(key_type) == FALSE)
    return NULL;

  /* Construct the group name. */
  proxy_name = ssh_make_proxy_key_name(key_type);

  if (proxy_name == NULL)
    return NULL;

  if ((base_ctx = ssh_calloc(1, sizeof(*base_ctx))) != NULL)
    {
      base_ctx->ref_count = 0;
      base_ctx->context = context;
    }
  else
    {
      ssh_free(proxy_name);
      return NULL;
    }

  if ((proxykey_handle = ssh_calloc(1, sizeof(*proxykey_handle))) != NULL)
    {
      proxykey_handle->base = base_ctx;
      proxykey_handle->key_addr = NULL;
    }
  else
    {
      ssh_free(base_ctx);
      ssh_free(proxy_name);
      return NULL;
    }

  if ((proxykey = ssh_calloc(1, sizeof(*proxykey))) != NULL)
    {
      proxykey->key_type = key_type;
      proxykey->handle = proxykey_handle;
      proxykey->key_operation = key_operation;
      proxykey->free_operation = free_operation;
      proxykey->key_size = size_in_bits;

      if (ssh_pk_group_generate(&group,
                                proxy_name,
                                SSH_PKF_PROXY, proxykey,
                                SSH_PKF_DH, "plain",
                                SSH_PKF_END) != SSH_CRYPTO_OK)
        {
          SSH_DEBUG(SSH_D_FAIL, ("Cannot generate the group"));
          ssh_free(proxy_name);
          return NULL;
        }

      ssh_free(proxy_name);
      return group;
    }
  else
    {
      ssh_free(proxykey_handle);
      ssh_free(base_ctx);
      ssh_free(proxy_name);
      return NULL;
    }
}

/* Map the ProxyRGFId to a SShRGFDefStruct */
const SshRGFDefStruct * ssh_map_proxy_id_to_rgf(SshProxyRGFId rgf_id)
{
  switch (rgf_id)
    {
    case SSH_DSA_NIST_SHA1:
      return &ssh_rgf_std_sha1_def;
#ifdef SSHDIST_CRYPT_RSA
#ifdef WITH_RSA
#ifdef SSHDIST_CRYPT_SHA
    case SSH_RSA_PKCS1_SHA1:
      return &ssh_rgf_pkcs1_sha1_def;
    case SSH_RSA_PKCS1_SHA1_NO_HASH:
      return &ssh_rgf_pkcs1_sha1_no_hash_def;
    case SSH_RSA_PKCS1_SHA1_NO_PAD:
      return &ssh_rgf_pkcs1_nopad_sha1_def;
#endif /* SSHDIST_CRYPT_SHA */
#ifdef SSHDIST_CRYPT_MD5
    case SSH_RSA_PKCS1_MD5:
      return &ssh_rgf_pkcs1_md5_def;
    case SSH_RSA_PKCS1_MD5_NO_HASH:
      return &ssh_rgf_pkcs1_md5_no_hash_def;
    case SSH_DSA_MD5:
      return &ssh_rgf_std_md5_def;
    case SSH_RSA_PKCS1_MD5_NO_PAD:
      return &ssh_rgf_pkcs1_nopad_md5_def;
#endif /* SSHDIST_CRYPT_MD5 */
#ifdef SSHDIST_CRYPT_MD2
    case SSH_RSA_PKCS1_MD2:
      return &ssh_rgf_pkcs1_md2_def;
    case SSH_RSA_PKCS1_MD2_NO_HASH:
      return &ssh_rgf_pkcs1_md2_no_hash_def;
    case SSH_DSA_MD2:
      return &ssh_rgf_std_md2_def;
#endif /* SSHDIST_CRYPT_MD2 */
    case SSH_RSA_PKCS1_NONE:
      return &ssh_rgf_pkcs1_none_def;
    case SSH_RSA_PKCS1V2_OAEP:
      return &ssh_rgf_pkcs1v2_sha1_def;
#endif /* WITH_RSA */
#endif /* SSHDIST_CRYPT_RSA */
    case SSH_DSA_NONE_NONE:
    case SSH_RSA_NONE_NONE:
      return &ssh_rgf_dummy_def;
    case SSH_INVALID_RGF:
    case SSH_DH_NONE_NONE:
      return NULL;
    default:
      return NULL;
    }
}

SshCryptoStatus
ssh_proxy_key_rgf_encrypt(SshProxyOperationId operation_id,
                          SshProxyRGFId rgf_id,
                          size_t maximum_size,
                          const unsigned char *input_data,
                          size_t input_data_len,
                          unsigned char **output_data,
                          size_t *output_data_len)

{
  SshRGF rgf = NULL;
  SshCryptoStatus status = SSH_CRYPTO_UNSUPPORTED;
  const SshRGFDefStruct *rgf_def;

  *output_data = NULL;
  *output_data_len = 0;

  /* Allocate the RGF corresponding to rgf_id. */
  rgf_def =  ssh_map_proxy_id_to_rgf(rgf_id);
  if (rgf_def)
    rgf = ssh_rgf_allocate(rgf_def);

  if (rgf == NULL)
    return SSH_CRYPTO_UNSUPPORTED;

  if (operation_id == SSH_RSA_PUB_ENCRYPT)
    {
      if ((status = ssh_rgf_for_encryption(rgf, input_data, input_data_len,
                                           maximum_size, output_data,
                                           output_data_len))
          != SSH_CRYPTO_OK)
        {
          ssh_rgf_free(rgf);
          ssh_free(*output_data);
          return status;
        }
    }

  ssh_rgf_free(rgf);
  return status;
}

SshCryptoStatus
ssh_proxy_key_rgf_decrypt(SshProxyOperationId operation_id,
                          SshProxyRGFId rgf_id,
                          size_t maximum_size,
                          const unsigned char *input_data,
                          size_t input_data_len,
                          unsigned char **output_data,
                          size_t *output_data_len)

{
  SshRGF rgf = NULL;
  SshCryptoStatus status = SSH_CRYPTO_UNSUPPORTED;
  const SshRGFDefStruct *rgf_def;

  *output_data = NULL;
  *output_data_len = 0;

  /* Allocate the RGF corresponding to rgf_id. */
  rgf_def =  ssh_map_proxy_id_to_rgf(rgf_id);
  if (rgf_def)
    rgf = ssh_rgf_allocate(rgf_def);

  if (rgf == NULL)
    return SSH_CRYPTO_UNSUPPORTED;

  if (operation_id == SSH_RSA_PRV_DECRYPT)
    {
      if ((status = ssh_rgf_for_decryption(rgf, input_data, input_data_len,
                                           maximum_size,
                                           output_data,
                                           output_data_len)) != SSH_CRYPTO_OK)
        {
          ssh_rgf_free(rgf);
          ssh_free(*output_data);
          return status;
        }
    }

  ssh_rgf_free(rgf);
  return status;
}


SshCryptoStatus
ssh_proxy_key_rgf_sign(SshProxyOperationId operation_id,
                       SshProxyRGFId rgf_id,
                       size_t maximum_size,
                       const unsigned char *input_data,
                       size_t input_data_len,
                       unsigned char **output_data,
                       size_t *output_data_len)

{
  SshRGF rgf = NULL;
  const SshRGFDefStruct *rgf_def;
  SshCryptoStatus status = SSH_CRYPTO_UNSUPPORTED;

  *output_data = NULL;
  *output_data_len = 0;

  /* Allocate the RGF corresponding to rgf_id. */
  rgf_def =  ssh_map_proxy_id_to_rgf(rgf_id);
  if (rgf_def)
    rgf = ssh_rgf_allocate(rgf_def);

  if (rgf == NULL)
    return SSH_CRYPTO_UNSUPPORTED;

  if (operation_id == SSH_DSA_PRV_SIGN ||
      operation_id == SSH_RSA_PRV_SIGN)
    {
      ssh_rgf_hash_update(rgf, input_data, input_data_len);

      status = ssh_rgf_for_signature(rgf, maximum_size,
                                     output_data, output_data_len);
    }

  ssh_rgf_free(rgf);
  return status;
}


/* Used for signature verification */
SshCryptoStatus
ssh_proxy_key_rgf_verify(SshProxyOperationId operation_id,
                         SshProxyRGFId rgf_id,
                         size_t maximum_size,
                         const unsigned char *data,
                         size_t data_len,
                         const unsigned char *decrypted_signature,
                         size_t decrypted_signature_len)
{
  SshRGF rgf = NULL;
  const SshRGFDefStruct *rgf_def;
  SshCryptoStatus status = SSH_CRYPTO_UNSUPPORTED;


  /* Allocate the RGF corresponding to rgf_id. */
  rgf_def =  ssh_map_proxy_id_to_rgf(rgf_id);
  if (rgf_def)
    rgf = ssh_rgf_allocate(rgf_def);

  if (rgf == NULL)
    return SSH_CRYPTO_UNSUPPORTED;

  if (operation_id == SSH_DSA_PUB_VERIFY ||
      operation_id == SSH_RSA_PUB_VERIFY)
    {
      ssh_rgf_hash_update(rgf, data, data_len);
      status = ssh_rgf_for_verification(rgf, decrypted_signature,
                                        decrypted_signature_len);
    }

  ssh_rgf_free(rgf);
  return status;
}
