/*
  x509cert_sync.c

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
  Espoo, Finland
  All rights reserved.

  Generic certificate handling functions (allocation, freeing etc).
*/

#include "sshincludes.h"
#include "x509.h"
#include "x509internal.h"
#include "oid.h"

Boolean ssh_x509_cert_verify(SshX509Certificate c,
                             SshPublicKey issuer_key)
{
  char *sign, *key_type;
  const SshX509PkAlgorithmDefStruct *algorithm;
  Boolean rv;

  if (issuer_key == NULL)
    return FALSE;

  /* Set the algorithm of the issuer key to correspond the subject. */

  /* Get the signature algorithm type so that we can look very transparent
     to the application. */
  if (ssh_public_key_get_info(issuer_key,
                              SSH_PKF_KEY_TYPE, &key_type,
                              SSH_PKF_SIGN, &sign,
                              SSH_PKF_END) != SSH_CRYPTO_OK)
    return FALSE;

  /* Check that this implementation supports the given algorithm and
     key type pair. */
  algorithm = ssh_x509_match_algorithm(key_type,
                                       c->pop.signature.pk_algorithm, NULL);
  if (algorithm == NULL)
    return FALSE;

  /* Now select the scheme. */
  if (ssh_public_key_select_scheme(issuer_key,
                                   SSH_PKF_SIGN, c->pop.signature.pk_algorithm,
                                   SSH_PKF_END) != SSH_CRYPTO_OK)
    return FALSE;

  rv = ssh_public_key_verify_signature(issuer_key,
                                       c->pop.signature.signature,
                                       c->pop.signature.signature_len,
                                       c->pop.proved_message,
                                       c->pop.proved_message_len);

  /* Return the issuer signature scheme to where it originally was. */

  /* Now select the scheme. */
  if (ssh_public_key_select_scheme(issuer_key,
                                   SSH_PKF_SIGN, sign,
                                   SSH_PKF_END) != SSH_CRYPTO_OK)
    /* We're not really interested in this, but just return an error
       anyway. */
    return FALSE;

  return rv;
}


/* This starts an synchronous encoding call. Arguments are stored to
   encoding context, and the same functions are used as in asynchronous
   case. If the private key passed as an argument here is not capable of
   doing synchronous call, ssh_fatal will be called later. */
SshX509Status ssh_x509_cert_encode(SshX509Certificate c,
                                   SshPrivateKey issuer_key,
                                   unsigned char **buf, size_t *buf_len)
{
  SshX509CertEncodeContext encode_context;
  SshX509Status rv;

  /* Make the context and pass it forward. */
  if ((encode_context = ssh_calloc(1, sizeof(*encode_context))) == NULL)
    return SSH_X509_FAILURE;

  encode_context->cert = c;
  encode_context->issuer_key = issuer_key;
  encode_context->rv = SSH_X509_OK;

  encode_context->operation_handle
    = ssh_operation_register(ssh_x509_cert_encode_async_abort, encode_context);

  /* Pass the context. */
  ssh_x509_cert_encode_internal(encode_context);

  /* Read data from context. */
  *buf = encode_context->buf;
  *buf_len = encode_context->buf_len;
  rv = encode_context->rv;

  /* Free the context. */
  ssh_x509_cert_encode_async_abort(encode_context);
  return rv;
}
