/*

  sshrgf.c

  Author: Mika Kojo     <mkojo@ssh.fi>
          Patrick Irwin <irwin@ssh.fi>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
  All rights reserved.

  Created Thu Dec  9 21:25:47 1999.

  A library for redundancy generation functions for specific algorithms.

*/

#include "sshincludes.h"
#include "pkcs1.h"
#include "sshcrypt.h"
#include "sshmp.h"
#include "sshgenmp.h"
#include "sshrgf.h"
#include "sshrgf-internal.h"
#include "sshpk_i.h"
#include "sshhash_i.h"

#ifdef SSHDIST_CRYPT_RSA
#ifdef WITH_RSA
#include "rsa.h"
#endif /* WITH_RSA */
#endif /* SSHDIST_CRYPT_RSA */

#define SSH_DEBUG_MODULE "SshCryptoRGF"

#define SSH_RGF_MAXLEN  0xffffffff


/************** RGF's that have a standard hash function. ***********/

SshRGF ssh_rgf_std_allocate(const SshRGFDefStruct *def)
{
  SshRGF created;
  SshCryptoStatus status;

  if (def == NULL || def->hash_def == NULL)
    {
      SSH_DEBUG(SSH_D_ERROR, ("No hash definition."));
      return NULL;
    }

  if ((created = ssh_calloc(1, sizeof(*created))) != NULL)
    {
      created->def = def;

      if ((status = 
           ssh_hash_object_allocate_internal(def->hash_def,
                                             (SshHashObject*)
                                             &created->context))
          != SSH_CRYPTO_OK)
        {
          ssh_free(created);
          return NULL;
        }

      ssh_hash_object_reset(created->context);
    }
  return created;
}

void ssh_rgf_std_free(SshRGF rgf)
{
  ssh_hash_object_free(rgf->context);
  ssh_free(rgf);
}

Boolean ssh_rgf_std_hash_update(SshRGF rgf, Boolean for_digest,
                                const unsigned char *data, size_t data_len)
{
  /* Handle the case when possibly setting the finalized digest
     beforehand. */
  if (for_digest)
    {
      if (rgf->def->hash_def->digest_length == data_len)
        {
          /* This does not allocate new space for the data. */
          rgf->precomp_digest        = data;
          rgf->precomp_digest_length = data_len;
          return TRUE;
        }
      return FALSE;
    }

  if (rgf->precomp_digest)
    return FALSE;

  ssh_hash_object_update(rgf->context, data, data_len);
  return TRUE;
}

Boolean ssh_rgf_ignore_hash_update(SshRGF rgf, Boolean for_digest,
                                   const unsigned char *data, size_t data_len)
{
  if (rgf->def->hash_def->digest_length != data_len)
      return FALSE;

  /* This does not allocate new space for the data. */
  rgf->precomp_digest        = data;
  rgf->precomp_digest_length = data_len;
  return TRUE;
}

Boolean ssh_rgf_std_hash_finalize(SshRGF rgf, unsigned char **digest,
                                  size_t *digest_length)
{
  unsigned char *buf;
  size_t buflen;

  *digest = NULL;
  *digest_length = 0;

  buflen = rgf->def->hash_def->digest_length;

  if ((buf = ssh_malloc(buflen)) == NULL)
    return FALSE;

  if (rgf->precomp_digest)
    {
      SSH_ASSERT(rgf->precomp_digest_length == buflen);
      memcpy(buf, rgf->precomp_digest, rgf->precomp_digest_length);
    }
  else
    {
      ssh_hash_object_final(rgf->context, buf);
    }

  *digest = buf;
  *digest_length = buflen;
  return TRUE;
}

Boolean ssh_rgf_ignore_hash_finalize(SshRGF rgf, unsigned char **digest,
                                     size_t *digest_length)
{
  unsigned char *buf;
  size_t buflen;

  *digest = NULL;
  *digest_length = 0;

  if (rgf->precomp_digest)
    {
      buflen = rgf->precomp_digest_length;

      if ((buf = ssh_malloc(buflen)) == NULL)
        return FALSE;

      memcpy(buf, rgf->precomp_digest, rgf->precomp_digest_length);
      *digest = buf;
      *digest_length = buflen;
      return TRUE;
    }
  else
    {
      return FALSE;
    }
}

size_t
ssh_rgf_hash_asn1_oid_compare(SshRGF rgf,
 			      const unsigned char *oid,
 			      size_t max_len)
{
  if (rgf->def->hash_def)
    return (*rgf->def->hash_def->compare_asn1_oid)(oid, max_len);
  else
    return 0;
}

const unsigned char *
ssh_rgf_hash_asn1_oid_generate(SshRGF rgf, size_t *len)
{
  if (len) *len = 0;
  if (rgf->def->hash_def)
    return (*rgf->def->hash_def->generate_asn1_oid)(len);
  else
    return NULL;
}


/************** RGF's that have no standard hash function. ***********/

SshRGF ssh_rgf_none_allocate(const SshRGFDefStruct *def)
{
  SshRGF created = NULL;

  if (def == NULL)
    {
      SSH_DEBUG(SSH_D_ERROR, ("No hash definition."));
      return NULL;
    }

  if ((created = ssh_calloc(1, sizeof(*created))) != NULL)
    created->def = def;

 return created;
}

void ssh_rgf_none_free(SshRGF rgf)
{
  ssh_free(rgf);
}

Boolean ssh_rgf_none_hash_update(SshRGF rgf, Boolean for_digest,
                                 const unsigned char *data, size_t data_len)
{
  rgf->precomp_digest        = data;
  rgf->precomp_digest_length = data_len;
  return TRUE;
}

Boolean ssh_rgf_none_hash_finalize(SshRGF rgf, unsigned char **digest,
                                   size_t *digest_length)
{
  unsigned char *buf;
  size_t buflen;

  *digest = NULL;
  *digest_length = 0;

  if (!rgf->precomp_digest)
    return FALSE;

  buflen = rgf->precomp_digest_length;

  if ((buf = ssh_malloc(buflen)) == NULL)
    return FALSE;

  memcpy(buf, rgf->precomp_digest, rgf->precomp_digest_length);
  *digest = buf;
  *digest_length = buflen;
  return TRUE;
}

Boolean ssh_rgf_none_hash_finalize_no_allocate(SshRGF rgf,
                                               unsigned char **digest,
                                               size_t *digest_length)
{
  *digest  = (unsigned char *) rgf->precomp_digest;
  *digest_length = rgf->precomp_digest_length;
  return TRUE;
}


size_t
ssh_rgf_none_hash_asn1_oid_compare(SshRGF rgf,
				   const unsigned char *oid,
				   size_t max_len)
{
  return 0;
}

const unsigned char *
ssh_rgf_none_hash_asn1_oid_generate(SshRGF rgf, size_t *len)
{
  return NULL;
}

/* Basic redundancy function implementations. */

#ifdef SSHDIST_CRYPT_SHA
extern const SshHashDefStruct ssh_hash_sha_def;
#endif /* SSHDIST_CRYPT_SHA */

#ifdef SSHDIST_CRYPT_MD5
extern const SshHashDefStruct ssh_hash_md5_def;
#endif /* SSHDIST_CRYPT_MD5 */

#ifdef SSHDIST_CRYPT_MD2
extern const SshHashDefStruct ssh_hash_md2_def;
#endif /* SSHDIST_CRYPT_MD2 */


#ifdef SSHDIST_CRYPT_RSA

#ifdef WITH_RSA
/* RSA PKCS-1 v1.5 */

/* Some useful routines doing the dirty work. */
SshCryptoStatus
ssh_rgf_pkcs1_encrypt(SshRGF rgf, const unsigned char *msg, size_t msg_len,
                      size_t max_output_msg_len, unsigned char **output_msg,
                      size_t *output_msg_len)
{
  unsigned char *buf;

  if ((buf = ssh_malloc(max_output_msg_len)) == NULL)
      return SSH_CRYPTO_NO_MEMORY;

  if (!ssh_pkcs1_pad(msg, msg_len, 2, buf, max_output_msg_len))
      return SSH_CRYPTO_OPERATION_FAILED;

  *output_msg     = buf;
  *output_msg_len = max_output_msg_len;
  return SSH_CRYPTO_OK;
}

SshCryptoStatus
ssh_rgf_pkcs1_decrypt(SshRGF rgf, const unsigned char *decrypted_msg,
                      size_t decrypted_msg_len,
                      size_t max_output_msg_len,
                      unsigned char **output_msg,
                      size_t *output_msg_len)
{
  unsigned char *buf;
  size_t         buf_len;

  if ((buf = ssh_malloc(max_output_msg_len)) == NULL)
      return SSH_CRYPTO_NO_MEMORY;

  if (!ssh_pkcs1_unpad(decrypted_msg, decrypted_msg_len, 2,
                       buf, max_output_msg_len, &buf_len))
    {
      ssh_free(buf);
      return SSH_CRYPTO_OPERATION_FAILED;
    }

  /* Return the unpadded msg. */
  *output_msg     = buf;
  *output_msg_len = buf_len;
  return SSH_CRYPTO_OK;
}

static SshCryptoStatus
rgf_pkcs1_sign(Boolean do_padding, SshRGF rgf, size_t max_output_msg_len,
               unsigned char **output_msg, size_t *output_msg_len)
{
  unsigned char *digest, *buf;
  const unsigned char *encoded_oid;
  size_t digest_len, encoded_oid_len;
  Boolean rv = FALSE;

  encoded_oid = (*rgf->def->rgf_hash_asn1_oid_generate)(rgf, &encoded_oid_len);
  if (encoded_oid == NULL || encoded_oid_len == 0)
    return SSH_CRYPTO_OPERATION_FAILED;

  if (!(*rgf->def->rgf_hash_finalize)(rgf, &digest, &digest_len))
     return SSH_CRYPTO_OPERATION_FAILED;

  if ((buf = ssh_calloc(1, max_output_msg_len)) == NULL)
   {
     ssh_free(digest);
     return SSH_CRYPTO_NO_MEMORY;
   }

  if (do_padding)
    {
      rv = ssh_pkcs1_wrap_and_pad(encoded_oid, encoded_oid_len,
                                  digest, digest_len, 1,
                                  buf, max_output_msg_len);
    }
  else
    {
      if (max_output_msg_len < digest_len + encoded_oid_len)
        rv = FALSE;

      memcpy(buf, encoded_oid, encoded_oid_len);
      memcpy(buf + encoded_oid_len, digest, digest_len);
      rv = TRUE;
    }

  ssh_free(digest);

  if (rv)
    {
      *output_msg     = buf;
      *output_msg_len = max_output_msg_len;
      return SSH_CRYPTO_OK;
    }

  ssh_free(buf);
  return SSH_CRYPTO_OPERATION_FAILED;
}


SshCryptoStatus
ssh_rgf_pkcs1_nopad_sign(SshRGF rgf, size_t max_output_msg_len,
                         unsigned char **output_msg, size_t *output_msg_len)
{
  return rgf_pkcs1_sign(FALSE, rgf, max_output_msg_len,
                        output_msg, output_msg_len);
}

SshCryptoStatus
ssh_rgf_pkcs1_sign(SshRGF rgf, size_t max_output_msg_len,
                   unsigned char **output_msg, size_t *output_msg_len)
{
  return rgf_pkcs1_sign(TRUE, rgf, max_output_msg_len,
                        output_msg, output_msg_len);
}


static SshCryptoStatus
rgf_pkcs1_verify(Boolean do_unpad,
                 SshRGF rgf,
                 const unsigned char *decrypted_signature,
                 size_t decrypted_signature_len)
{
  unsigned char *ber_buf;
  unsigned char *digest;
  size_t digest_len, return_len, encoded_oid_len;
  size_t max_output_msg_len;
  Boolean rv;
  SshCryptoStatus status = SSH_CRYPTO_OPERATION_FAILED;

  max_output_msg_len = decrypted_signature_len;

  /* Decode the msg. */
  if ((ber_buf = ssh_malloc(max_output_msg_len)) == NULL)
    return SSH_CRYPTO_NO_MEMORY;

  if (do_unpad)
    {
      rv = ssh_pkcs1_unpad(decrypted_signature, decrypted_signature_len,
                           1, ber_buf, max_output_msg_len, &return_len);
      if (!rv)
        {
          ssh_free(ber_buf);
          return SSH_CRYPTO_SIGNATURE_CHECK_FAILED;
        }
    }
  else
    {
      memcpy(ber_buf, decrypted_signature, decrypted_signature_len);
      return_len = decrypted_signature_len;
    }

  /* Finalize the hash */
  if (!(*rgf->def->rgf_hash_finalize)(rgf, &digest, &digest_len))
    {
      ssh_free(ber_buf);
      return SSH_CRYPTO_OPERATION_FAILED;
    }

  encoded_oid_len =
    (*rgf->def->rgf_hash_asn1_oid_compare)(rgf, ber_buf, return_len);
  if (encoded_oid_len == 0 || return_len != encoded_oid_len + digest_len)
    {
      ssh_free(ber_buf);
      ssh_free(digest);
      return SSH_CRYPTO_SIGNATURE_CHECK_FAILED;
    }

  /* Compare. */
  if (memcmp(ber_buf + encoded_oid_len, digest, digest_len) == 0)
    status = SSH_CRYPTO_OK;
  else
    status = SSH_CRYPTO_SIGNATURE_CHECK_FAILED;

  ssh_free(digest);
  ssh_free(ber_buf);
  return status;
}

SshCryptoStatus
ssh_rgf_pkcs1_nopad_verify(SshRGF rgf,
                           const unsigned char *decrypted_signature,
                           size_t decrypted_signature_len)
{
  return rgf_pkcs1_verify(FALSE, rgf, decrypted_signature,
                          decrypted_signature_len);
}

SshCryptoStatus ssh_rgf_pkcs1_verify(SshRGF rgf,
                                     const unsigned char *decrypted_signature,
                                     size_t decrypted_signature_len)
{
  return rgf_pkcs1_verify(TRUE, rgf, decrypted_signature,
                          decrypted_signature_len);
}


SshCryptoStatus
ssh_rgf_pkcs1_sign_nohash(SshRGF rgf, size_t max_output_msg_len,
                          unsigned char **output_msg, size_t *output_msg_len)
{
  Boolean rv;
  unsigned char *digest, *buf;
  size_t digest_length;

 if ((buf = ssh_malloc(max_output_msg_len)) == NULL)
     return SSH_CRYPTO_NO_MEMORY;

 /* Finalize the hash */
 if (!(*rgf->def->rgf_hash_finalize)(rgf, &digest, &digest_length))
   {
     ssh_free(buf);
     return SSH_CRYPTO_OPERATION_FAILED;
   }

  rv = ssh_pkcs1_pad(digest, digest_length, 1, buf, max_output_msg_len);

  ssh_free(digest);

  *output_msg     = buf;
  *output_msg_len = max_output_msg_len;

  if (rv)
    return SSH_CRYPTO_OK;
  else
    return SSH_CRYPTO_OPERATION_FAILED;
}

SshCryptoStatus
ssh_rgf_pkcs1_verify_nohash(SshRGF rgf,
                            const unsigned char *decrypted_signature,
                            size_t decrypted_signature_len)
{
  unsigned char *buf;
  size_t return_len;
  unsigned char *digest;
  size_t digest_length, max_output_msg_len;

  max_output_msg_len = decrypted_signature_len;

  /* Allocate a suitable decoding buffer. */
  if ((buf = ssh_malloc(max_output_msg_len)) == NULL)
    return SSH_CRYPTO_NO_MEMORY;

  /* Unpad. */
  if (ssh_pkcs1_unpad(decrypted_signature, decrypted_signature_len, 1,
                      buf, max_output_msg_len, &return_len) == FALSE)
    {
      ssh_free(buf);
      return SSH_CRYPTO_SIGNATURE_CHECK_FAILED;
    }

  /* Finalize the hash */
  if (!(*rgf->def->rgf_hash_finalize)(rgf, &digest, &digest_length))
    {
      ssh_free(buf);
      return SSH_CRYPTO_OPERATION_FAILED;
    }

  if (digest_length != return_len ||
      memcmp(digest, buf, digest_length) != 0)
    {
      ssh_free(digest);
      ssh_free(buf);
      return SSH_CRYPTO_SIGNATURE_CHECK_FAILED;
    }
  ssh_free(digest);
  ssh_free(buf);
  return SSH_CRYPTO_OK;
}

/* RSA PKCS-1 v2.0 */

SshCryptoStatus
ssh_rgf_pkcs1v2_encrypt(SshRGF rgf, const unsigned char *msg, size_t msg_len,
                        size_t max_output_msg_len, unsigned char **output_msg,
                        size_t *output_msg_len)
{
  unsigned char *param, *buf;
  size_t param_len;

  if (rgf->def->hash_def == NULL)
    return SSH_CRYPTO_OPERATION_FAILED;

  param = ssh_rsa_pkcs1v2_default_explicit_param(rgf->def->hash_def,
                                                 &param_len);
  if (param == NULL)
    return SSH_CRYPTO_OPERATION_FAILED;

  if (max_output_msg_len == 0)
    return SSH_CRYPTO_OPERATION_FAILED;

  if ((buf = ssh_malloc(max_output_msg_len)) == NULL)
      return SSH_CRYPTO_NO_MEMORY;

  /* Initialize the highest octet. */
  buf[0] = 0;
  if (ssh_rsa_oaep_encode_with_mgf1(rgf->def->hash_def,
                                    msg, msg_len,
                                    param, param_len,
                                    buf+1, max_output_msg_len-1) == FALSE)
    {
      ssh_free(param);
      ssh_free(buf);
      return SSH_CRYPTO_OPERATION_FAILED;
    }
  ssh_free(param);

  *output_msg = buf;
  *output_msg_len = max_output_msg_len;
  return SSH_CRYPTO_OK;
}

SshCryptoStatus
ssh_rgf_pkcs1v2_decrypt(SshRGF rgf, const unsigned char *decrypted_msg,
                        size_t decrypted_msg_len, size_t max_output_msg_len,
                        unsigned char **output_msg, size_t *output_msg_len)
{
  unsigned char *param;
  size_t param_len;

  if (rgf->def->hash_def == NULL)
    return SSH_CRYPTO_OPERATION_FAILED;

  if (decrypted_msg_len == 0 ||
      decrypted_msg[0] != 0)
    return SSH_CRYPTO_OPERATION_FAILED;

  /* Find params. */
  param = ssh_rsa_pkcs1v2_default_explicit_param(rgf->def->hash_def,
                                                 &param_len);
  if (param == NULL)
    return SSH_CRYPTO_OPERATION_FAILED;

  /* Apply the OAEP decoding. */
  if (ssh_rsa_oaep_decode_with_mgf1(rgf->def->hash_def,
                                    decrypted_msg+1, decrypted_msg_len-1,
                                    param, param_len,
                                    output_msg, output_msg_len) == FALSE)
    {
      ssh_free(param);
      return SSH_CRYPTO_OPERATION_FAILED;
    }
  ssh_free(param);
  return SSH_CRYPTO_OK;
}
#endif /* WITH_RSA */
#endif /* SSHDIST_CRYPT_RSA */


/* A generic routines that can be used with many cryptosystems with
   little redundancy management. These include e.g. the DSA algorithm.

   Common idea with all the methods is that they basically do not
   do any redundancy related operations. For example, they just hash
   the message for signature using one of the standard hash functions.
   They do not pad the digest before signing, usually because these
   methods include the digest into the cryptosystem in more complicated
   manner than RSA does, for example.
   */

SshCryptoStatus
ssh_rgf_std_encrypt(SshRGF rgf, const unsigned char *msg, size_t msg_len,
                    size_t max_output_msg_len, unsigned char **output_msg,
                    size_t *output_msg_len)
{
  unsigned char *buf;

  if (msg_len > max_output_msg_len)
    return SSH_CRYPTO_DATA_TOO_LONG;

  /* Allocate a suitable decoding buffer. */
  if ((buf = ssh_malloc(max_output_msg_len)) == NULL)
    return SSH_CRYPTO_NO_MEMORY;

  /* Zero the output msg. */
  memset(buf, 0, max_output_msg_len);
  memcpy(buf + (max_output_msg_len - msg_len), msg, msg_len);

  *output_msg     = buf;
  *output_msg_len = max_output_msg_len;

  return SSH_CRYPTO_OK;
}

SshCryptoStatus
ssh_rgf_std_decrypt(SshRGF rgf, const unsigned char *decrypted_msg,
                    size_t decrypted_msg_len, size_t max_output_msg_len,
                    unsigned char **output_msg, size_t *output_msg_len)
{
  if (decrypted_msg_len > max_output_msg_len)
    return SSH_CRYPTO_OPERATION_FAILED;

  if ((*output_msg = ssh_memdup(decrypted_msg, decrypted_msg_len)) != NULL)
    *output_msg_len = decrypted_msg_len;
  else
    {
      *output_msg_len = 0;
      return SSH_CRYPTO_NO_MEMORY;
    }
  return SSH_CRYPTO_OK;
}

SshCryptoStatus
ssh_rgf_std_sign(SshRGF rgf, size_t max_output_msg_len,
                 unsigned char **output_msg, size_t *output_msg_len)
{
  unsigned char  *digest, *buf;
  size_t digest_len;

  if ((buf = ssh_malloc(max_output_msg_len)) == NULL)
      return SSH_CRYPTO_NO_MEMORY;

  /* Finalize the hash */
  if (!(*rgf->def->rgf_hash_finalize)(rgf, &digest, &digest_len))
    {
      ssh_free(buf);
      return SSH_CRYPTO_OPERATION_FAILED;
    }

  /* Now check whether we can output the digest or not. */
  if (digest_len > max_output_msg_len)
    {
      ssh_free(buf);
      ssh_free(digest);
      return SSH_CRYPTO_DATA_TOO_SHORT;
    }

  memset(buf, 0, max_output_msg_len);
  memcpy(buf + (max_output_msg_len - digest_len), digest, digest_len);

  *output_msg = buf;
  *output_msg_len  = max_output_msg_len;

  ssh_free(digest);

  return SSH_CRYPTO_OK;
}

SshCryptoStatus ssh_rgf_std_verify(SshRGF rgf,
                                   const unsigned char *decrypted_signature,
                                   size_t decrypted_signature_len)
{
  unsigned char  *digest;
  size_t digest_len;

  /* Finalize the hash */
  if (!(*rgf->def->rgf_hash_finalize)(rgf, &digest, &digest_len))
      return SSH_CRYPTO_OPERATION_FAILED;

  if (digest_len != decrypted_signature_len ||
      memcmp(decrypted_signature, digest, digest_len) != 0)
    {
      ssh_free(digest);
      return SSH_CRYPTO_SIGNATURE_CHECK_FAILED;
    }
  ssh_free(digest);
  return SSH_CRYPTO_OK;
}

SshCryptoStatus
ssh_rgf_std_encrypt_no_allocate(SshRGF rgf, const unsigned char *msg,
                            size_t msg_len, size_t max_output_msg_len,
                            unsigned char **output_msg,
                            size_t *output_msg_len)
{
  *output_msg     = (unsigned char *) msg;
  *output_msg_len = max_output_msg_len;
  return SSH_CRYPTO_OK;
}

SshCryptoStatus
ssh_rgf_std_decrypt_no_allocate(SshRGF rgf,
                            const unsigned char *decrypted_msg,
                            size_t decrypted_msg_len,
                            size_t max_output_msg_len,
                            unsigned char **output_msg,
                            size_t *output_msg_len)
{
  *output_msg     = (unsigned char *) decrypted_msg;
  *output_msg_len = decrypted_msg_len;
  return SSH_CRYPTO_OK;
}

SshCryptoStatus
ssh_rgf_std_sign_no_allocate(SshRGF rgf, size_t max_output_msg_len,
                         unsigned char **output_msg, size_t *output_msg_len)
{
  *output_msg     = (unsigned char *) rgf->precomp_digest;
  *output_msg_len = rgf->precomp_digest_length;
  return SSH_CRYPTO_OK;
}

SshCryptoStatus
ssh_rgf_std_verify_no_allocate(SshRGF rgf,
                           const unsigned char *decrypted_signature,
                           size_t decrypted_signature_len)
{
  if (rgf->precomp_digest_length != decrypted_signature_len ||
      memcmp(decrypted_signature, rgf->precomp_digest,
             rgf->precomp_digest_length) != 0)
      return SSH_CRYPTO_SIGNATURE_CHECK_FAILED;

  return SSH_CRYPTO_OK;
}

/********************* Externally visible  functions. ***********************/

SshRGF ssh_rgf_allocate(const SshRGFDefStruct *rgf_def)
{
  SshRGF rgf;

  if (!rgf_def->rgf_allocate)
    return NULL;

  rgf = (*rgf_def->rgf_allocate)(rgf_def);

  if (rgf)
    rgf->def = rgf_def;

  return rgf;
}

void ssh_rgf_free(SshRGF rgf)
{
  if (rgf)
    {
      (*rgf->def->rgf_free)(rgf);
    }
}

void ssh_rgf_hash_update(SshRGF rgf, const unsigned char *data,
                         size_t data_len)
{
  (*rgf->def->rgf_hash_update)(rgf, FALSE, data, data_len);
}

Boolean ssh_rgf_hash_update_with_digest(SshRGF rgf,
                                        const unsigned char *data,
                                        size_t data_len)
{
  rgf->sign_digest = TRUE;
  return (*rgf->def->rgf_hash_update)(rgf, TRUE, data, data_len);
}

SshHash ssh_rgf_derive_hash(SshRGF rgf)
{
  SshCryptoStatus status;
  SshHash h;

  /* Check whether the conversion is possible. */
  if (rgf->def->hash_def == NULL)
    return NULL;

  status = ssh_hash_allocate_internal(rgf->def->hash_def, &h);

  if (status != SSH_CRYPTO_OK)
    return NULL;

  return h;
}

SshCryptoStatus ssh_rgf_for_encryption(SshRGF rgf,
                                       const unsigned char *msg,
                                       size_t msg_len,
                                       size_t max_output_msg_len,
                                       unsigned char **output_msg,
                                       size_t *output_msg_len)
{
  SshCryptoStatus status = SSH_CRYPTO_UNSUPPORTED;

  if (rgf->def->rgf_encrypt)
   status = (*rgf->def->rgf_encrypt)(rgf, msg, msg_len,
                                     max_output_msg_len,
                                     output_msg, output_msg_len);
  return status;
}

SshCryptoStatus ssh_rgf_for_decryption(SshRGF rgf,
                                       const unsigned char *decrypted_msg,
                                       size_t decrypted_msg_len,
                                       size_t max_output_msg_len,
                                       unsigned char **output_msg,
                                       size_t *output_msg_len)
{
  SshCryptoStatus status = SSH_CRYPTO_UNSUPPORTED;

  if (rgf->def->rgf_decrypt)
    status = (*rgf->def->rgf_decrypt)(rgf, decrypted_msg, decrypted_msg_len,
                                      max_output_msg_len,
                                      output_msg, output_msg_len);
  return status;
}

SshCryptoStatus ssh_rgf_for_signature(SshRGF rgf,
                                      size_t max_output_msg_len,
                                      unsigned char **output_msg,
                                      size_t *output_msg_len)
{
  SshCryptoStatus status = SSH_CRYPTO_UNSUPPORTED;

  if (rgf->def->rgf_sign)
    status = (*rgf->def->rgf_sign)(rgf, max_output_msg_len,
                                   output_msg, output_msg_len);

  return status;
}

SshCryptoStatus
ssh_rgf_for_verification(SshRGF rgf,
                         const unsigned char *decrypted_signature,
                         size_t decrypted_signature_len)
{
  SshCryptoStatus status = SSH_CRYPTO_UNSUPPORTED;

  if (rgf->def->rgf_verify)
    status = (*rgf->def->rgf_verify)(rgf,
                                     decrypted_signature,
                                     decrypted_signature_len);
  return status;
}

Boolean ssh_rgf_data_is_digest(SshRGF rgf)
{
  if (rgf->sign_digest)
    return TRUE;
  else
    return FALSE;
}

size_t ssh_rgf_hash_digest_length(SshRGF rgf)
{
  if (rgf->def->hash_def)
    return rgf->def->hash_def->digest_length;
  else
    return 0;
}
 


#ifdef SSHDIST_CRYPT_RSA
#ifdef WITH_RSA
#ifdef SSHDIST_CRYPT_SHA
/* RSA PKCS-1 v1.5 */
const SshRGFDefStruct ssh_rgf_pkcs1_sha1_def =
{
  ssh_rgf_std_allocate,
  ssh_rgf_std_free,

  ssh_rgf_std_hash_update,
  ssh_rgf_std_hash_finalize,
  ssh_rgf_hash_asn1_oid_compare,
  ssh_rgf_hash_asn1_oid_generate,
  &ssh_hash_sha_def,

  ssh_rgf_pkcs1_encrypt,
  ssh_rgf_pkcs1_decrypt,
  ssh_rgf_pkcs1_sign,
  ssh_rgf_pkcs1_verify
};

const SshRGFDefStruct ssh_rgf_pkcs1_sha1_no_hash_def =
{
  ssh_rgf_std_allocate,
  ssh_rgf_std_free,

  ssh_rgf_ignore_hash_update,
  ssh_rgf_ignore_hash_finalize,
  ssh_rgf_hash_asn1_oid_compare,
  ssh_rgf_hash_asn1_oid_generate,
  &ssh_hash_sha_def,

  ssh_rgf_pkcs1_encrypt,
  ssh_rgf_pkcs1_decrypt,
  ssh_rgf_pkcs1_sign,
  ssh_rgf_pkcs1_verify
};

#endif /* SSHDIST_CRYPT_SHA */

#ifdef SSHDIST_CRYPT_MD5

const SshRGFDefStruct ssh_rgf_pkcs1_md5_def =
{
  ssh_rgf_std_allocate,
  ssh_rgf_std_free,

  ssh_rgf_std_hash_update,
  ssh_rgf_std_hash_finalize,
  ssh_rgf_hash_asn1_oid_compare,
  ssh_rgf_hash_asn1_oid_generate,
  &ssh_hash_md5_def,

  ssh_rgf_pkcs1_encrypt,
  ssh_rgf_pkcs1_decrypt,
  ssh_rgf_pkcs1_sign,
  ssh_rgf_pkcs1_verify
};

const SshRGFDefStruct ssh_rgf_pkcs1_md5_no_hash_def =
{
  ssh_rgf_std_allocate,
  ssh_rgf_std_free,

  ssh_rgf_ignore_hash_update,
  ssh_rgf_ignore_hash_finalize,
  ssh_rgf_hash_asn1_oid_compare,
  ssh_rgf_hash_asn1_oid_generate,
  &ssh_hash_md5_def,

  ssh_rgf_pkcs1_encrypt,
  ssh_rgf_pkcs1_decrypt,
  ssh_rgf_pkcs1_sign,
  ssh_rgf_pkcs1_verify
};


#endif /* SSHDIST_CRYPT_MD5 */

#ifdef SSHDIST_CRYPT_MD2

const SshRGFDefStruct ssh_rgf_pkcs1_md2_def =
{
  ssh_rgf_std_allocate,
  ssh_rgf_std_free,

  ssh_rgf_std_hash_update,
  ssh_rgf_std_hash_finalize,
  ssh_rgf_hash_asn1_oid_compare,
  ssh_rgf_hash_asn1_oid_generate,
  &ssh_hash_md2_def,

  ssh_rgf_pkcs1_encrypt,
  ssh_rgf_pkcs1_decrypt,
  ssh_rgf_pkcs1_sign,
  ssh_rgf_pkcs1_verify
};

const SshRGFDefStruct ssh_rgf_pkcs1_md2_no_hash_def =
{
  ssh_rgf_std_allocate,
  ssh_rgf_std_free,

  ssh_rgf_ignore_hash_update,
  ssh_rgf_ignore_hash_finalize,
  ssh_rgf_hash_asn1_oid_compare,
  ssh_rgf_hash_asn1_oid_generate,
  &ssh_hash_md2_def,

  ssh_rgf_pkcs1_encrypt,
  ssh_rgf_pkcs1_decrypt,
  ssh_rgf_pkcs1_sign,
  ssh_rgf_pkcs1_verify
};
#endif /* SSHDIST_CRYPT_MD2 */

const SshRGFDefStruct ssh_rgf_pkcs1_none_def =
{
  ssh_rgf_none_allocate,
  ssh_rgf_none_free,

  ssh_rgf_none_hash_update,
  ssh_rgf_none_hash_finalize,
  ssh_rgf_none_hash_asn1_oid_compare,
  ssh_rgf_none_hash_asn1_oid_generate,
  NULL,

  ssh_rgf_pkcs1_encrypt,
  ssh_rgf_pkcs1_decrypt,
  ssh_rgf_pkcs1_sign_nohash,
  ssh_rgf_pkcs1_verify_nohash
};
#ifdef SSHDIST_CRYPT_SHA

/* RSA PKCS-1 v2.0 */

const SshRGFDefStruct ssh_rgf_pkcs1v2_sha1_def =
{
  ssh_rgf_std_allocate,
  ssh_rgf_std_free,

  ssh_rgf_std_hash_update,
  ssh_rgf_std_hash_finalize,
  ssh_rgf_hash_asn1_oid_compare,
  ssh_rgf_hash_asn1_oid_generate,
  &ssh_hash_sha_def,

  ssh_rgf_pkcs1v2_encrypt,
  ssh_rgf_pkcs1v2_decrypt,
  ssh_rgf_pkcs1_sign,
  ssh_rgf_pkcs1_verify
};
#endif /* SSHDIST_CRYPT_SHA */

#ifdef SSHDIST_CRYPT_MD5

const SshRGFDefStruct ssh_rgf_pkcs1v2_md5_def =
{
  ssh_rgf_std_allocate,
  ssh_rgf_std_free,

  ssh_rgf_std_hash_update,
  ssh_rgf_std_hash_finalize,
  ssh_rgf_hash_asn1_oid_compare,
  ssh_rgf_hash_asn1_oid_generate,
  &ssh_hash_md5_def,

  ssh_rgf_pkcs1v2_encrypt,
  ssh_rgf_pkcs1v2_decrypt,
  ssh_rgf_pkcs1_sign,
  ssh_rgf_pkcs1_verify
};
#endif /* SSHDIST_CRYPT_MD5 */


#ifdef SSHDIST_CRYPT_MD2
const SshRGFDefStruct ssh_rgf_pkcs1v2_md2_def =
{
  ssh_rgf_std_allocate,
  ssh_rgf_std_free,

  ssh_rgf_std_hash_update,
  ssh_rgf_std_hash_finalize,
  ssh_rgf_hash_asn1_oid_compare,
  ssh_rgf_hash_asn1_oid_generate,
  &ssh_hash_md2_def,

  ssh_rgf_pkcs1v2_encrypt,
  ssh_rgf_pkcs1v2_decrypt,
  ssh_rgf_pkcs1_sign,
  ssh_rgf_pkcs1_verify
};
#endif /* SSHDIST_CRYPT_MD2 */

const SshRGFDefStruct ssh_rgf_pkcs1v2_none_def =
{
  ssh_rgf_none_allocate,
  ssh_rgf_none_free,

  ssh_rgf_none_hash_update,
  ssh_rgf_none_hash_finalize,
  ssh_rgf_none_hash_asn1_oid_compare,
  ssh_rgf_none_hash_asn1_oid_generate,
  NULL,

  ssh_rgf_pkcs1v2_encrypt,
  ssh_rgf_pkcs1v2_decrypt,
  ssh_rgf_pkcs1_sign_nohash,
  ssh_rgf_pkcs1_verify_nohash
};
#endif /* WITH_RSA */
#endif /* SSHDIST_CRYPT_RSA */

#ifdef SSHDIST_CRYPT_SHA

const SshRGFDefStruct ssh_rgf_std_sha1_def =
{
  ssh_rgf_std_allocate,
  ssh_rgf_std_free,

  ssh_rgf_std_hash_update,
  ssh_rgf_std_hash_finalize,
  ssh_rgf_hash_asn1_oid_compare,
  ssh_rgf_hash_asn1_oid_generate,
  &ssh_hash_sha_def,

  ssh_rgf_std_encrypt,
  ssh_rgf_std_decrypt,
  ssh_rgf_std_sign,
  ssh_rgf_std_verify
};

#ifdef SSHDIST_CRYPT_RSA
#ifdef WITH_RSA
const SshRGFDefStruct ssh_rgf_pkcs1_nopad_sha1_def =
{
  ssh_rgf_std_allocate,
  ssh_rgf_std_free,

  ssh_rgf_std_hash_update,
  ssh_rgf_std_hash_finalize,
  ssh_rgf_hash_asn1_oid_compare,
  ssh_rgf_hash_asn1_oid_generate,
  &ssh_hash_sha_def,

  ssh_rgf_std_encrypt,
  ssh_rgf_std_decrypt,
  ssh_rgf_pkcs1_nopad_sign,
  ssh_rgf_pkcs1_nopad_verify
};
#endif /* WITH_RSA */
#endif /* SSHDIST_CRYPT_RSA */
#endif /* SSHDIST_CRYPT_SHA */

#ifdef SSHDIST_CRYPT_MD5

const SshRGFDefStruct ssh_rgf_std_md5_def =
{
  ssh_rgf_std_allocate,
  ssh_rgf_std_free,

  ssh_rgf_std_hash_update,
  ssh_rgf_std_hash_finalize,
  ssh_rgf_hash_asn1_oid_compare,
  ssh_rgf_hash_asn1_oid_generate,
  &ssh_hash_md5_def,

  ssh_rgf_std_encrypt,
  ssh_rgf_std_decrypt,
  ssh_rgf_std_sign,
  ssh_rgf_std_verify
};

#ifdef SSHDIST_CRYPT_RSA
#ifdef WITH_RSA
const SshRGFDefStruct ssh_rgf_pkcs1_nopad_md5_def =
{
  ssh_rgf_std_allocate,
  ssh_rgf_std_free,

  ssh_rgf_std_hash_update,
  ssh_rgf_std_hash_finalize,
  ssh_rgf_hash_asn1_oid_compare,
  ssh_rgf_hash_asn1_oid_generate,
  &ssh_hash_md5_def,

  ssh_rgf_std_encrypt,
  ssh_rgf_std_decrypt,
  ssh_rgf_pkcs1_nopad_sign,
  ssh_rgf_pkcs1_nopad_verify
};
#endif /* WITH_RSA */
#endif /* SSHDIST_CRYPT_RSA */
#endif /* SSHDIST_CRYPT_MD5 */

#ifdef SSHDIST_CRYPT_MD2
const SshRGFDefStruct ssh_rgf_std_md2_def =
{
  ssh_rgf_std_allocate,
  ssh_rgf_std_free,

  ssh_rgf_std_hash_update,
  ssh_rgf_std_hash_finalize,
  ssh_rgf_hash_asn1_oid_compare,
  ssh_rgf_hash_asn1_oid_generate,
  &ssh_hash_md2_def,

  ssh_rgf_std_encrypt,
  ssh_rgf_std_decrypt,
  ssh_rgf_std_sign,
  ssh_rgf_std_verify
};
#endif /* SSHDIST_CRYPT_MD2 */


const SshRGFDefStruct ssh_rgf_dummy_def =
{
  ssh_rgf_none_allocate,
  ssh_rgf_none_free,

  ssh_rgf_none_hash_update,
  ssh_rgf_none_hash_finalize,
  ssh_rgf_none_hash_asn1_oid_compare,
  ssh_rgf_none_hash_asn1_oid_generate,
  NULL,

  ssh_rgf_std_encrypt,
  ssh_rgf_std_decrypt,
  ssh_rgf_std_sign,
  ssh_rgf_std_verify
};

const SshRGFDefStruct ssh_rgf_dummy_no_allocate_def =
{
  ssh_rgf_none_allocate,
  ssh_rgf_none_free,

  ssh_rgf_none_hash_update,
  ssh_rgf_none_hash_finalize_no_allocate,
  ssh_rgf_none_hash_asn1_oid_compare,
  ssh_rgf_none_hash_asn1_oid_generate,
  NULL,

  ssh_rgf_std_encrypt_no_allocate,
  ssh_rgf_std_decrypt_no_allocate,
  ssh_rgf_std_sign_no_allocate,
  ssh_rgf_std_verify_no_allocate
};

/* rgf.c */
