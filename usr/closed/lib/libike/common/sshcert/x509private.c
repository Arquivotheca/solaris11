/*
  x509private.c

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
  Espoo, Finland
  All rights reserved.

  Decode and encode routines for x509 private keys.
*/

#include "sshincludes.h"
#include "x509.h"
#include "x509internal.h"
#include "oid.h"

/* Routines for encoding and decoding. */
SshX509Status ssh_x509_encode_private_key(SshPrivateKey private_key,
                                          unsigned char **buf,
                                          size_t *buf_len)
{
  SshAsn1Context context;
  SshAsn1Tree tree;
  SshAsn1Node prv_key;
  const SshOidStruct *oids;
  SshX509Status rv = SSH_X509_FAILURE;
  SshAsn1Status status;
  const SshX509PkAlgorithmDefStruct *algorithm;
  SshMPIntegerStruct n, p, q, e, d, u, g, y, x;

  /* Find the algorithm we are after. */
  algorithm = ssh_x509_private_key_algorithm(private_key);
  if (algorithm == NULL)
    return SSH_X509_FAILED_PRIVATE_KEY_OPS;

  oids = ssh_oid_find_by_std_name_of_type(algorithm->known_name,
                                          SSH_OID_PK);
  if (oids == NULL)
    return SSH_X509_FAILED_UNKNOWN_VALUE;

  if ((context = ssh_asn1_init()) == NULL)
    return SSH_X509_FAILURE;

  switch (algorithm->algorithm)
    {
    case SSH_X509_PKALG_RSA:
      ssh_mprz_init(&n);
      ssh_mprz_init(&e);
      ssh_mprz_init(&d);
      ssh_mprz_init(&p);
      ssh_mprz_init(&q);
      ssh_mprz_init(&u);

      if (ssh_private_key_get_info(private_key,
                                   SSH_PKF_MODULO_N,  &n,
                                   SSH_PKF_PUBLIC_E,  &e,
                                   SSH_PKF_SECRET_D,  &d,
                                   SSH_PKF_PRIME_P,   &p,
                                   SSH_PKF_PRIME_Q,   &q,
                                   SSH_PKF_INVERSE_U, &u,
                                   SSH_PKF_END) != SSH_CRYPTO_OK)
        {
          ssh_mprz_clear(&n);
          ssh_mprz_clear(&e);
          ssh_mprz_clear(&d);
          ssh_mprz_clear(&p);
          ssh_mprz_clear(&q);
          ssh_mprz_clear(&u);

          rv = SSH_X509_FAILED_PRIVATE_KEY_OPS;
          goto failed;
        }

      status =
        ssh_asn1_create_node(context, &prv_key,
                             "(sequence ()"
                             "  (integer ())"  /* n */
                             "  (integer ())"  /* e */
                             "  (integer ())"  /* d */
                             "  (integer ())"  /* p */
                             "  (integer ())"  /* q */
                             "  (integer ()))", /* u */
                             &n, &e, &d, &p, &q, &u);

      ssh_mprz_clear(&n);
      ssh_mprz_clear(&e);
      ssh_mprz_clear(&d);
      ssh_mprz_clear(&p);
      ssh_mprz_clear(&q);
      ssh_mprz_clear(&u);

      if (status != SSH_ASN1_STATUS_OK)
        {
          rv = SSH_X509_FAILED_ASN1_ENCODE;
          goto failed;
        }

      break;
    case SSH_X509_PKALG_DSA:
      ssh_mprz_init(&p);
      ssh_mprz_init(&q);
      ssh_mprz_init(&g);
      ssh_mprz_init(&y);
      ssh_mprz_init(&x);

      if (ssh_private_key_get_info(private_key,
                                   SSH_PKF_PRIME_P, &p,
                                   SSH_PKF_PRIME_Q, &q,
                                   SSH_PKF_GENERATOR_G, &g,
                                   SSH_PKF_PUBLIC_Y, &y,
                                   SSH_PKF_SECRET_X, &x,
                                   SSH_PKF_END) != SSH_CRYPTO_OK)
        {
          ssh_mprz_clear(&p);
          ssh_mprz_clear(&g);
          ssh_mprz_clear(&q);
          ssh_mprz_clear(&y);
          ssh_mprz_clear(&x);

          rv = SSH_X509_FAILED_PRIVATE_KEY_OPS;
          goto failed;
        }

      status =
        ssh_asn1_create_node(context, &prv_key,
                             "(sequence ()"
                             "  (integer ())"   /* p */
                             "  (integer ())"   /* q */
                             "  (integer ())"   /* g */
                             "  (integer ())"   /* y */
                             "  (integer ()))",  /* x */
                             &p, &q, &g, &y, &x);

      ssh_mprz_clear(&p);
      ssh_mprz_clear(&g);
      ssh_mprz_clear(&q);
      ssh_mprz_clear(&y);
      ssh_mprz_clear(&x);
      if (status != SSH_ASN1_STATUS_OK)
        {
          rv = SSH_X509_FAILED_ASN1_ENCODE;
          goto failed;
        }
      break;
    default:
      break;
    }

  status =
    ssh_asn1_create_tree(context, &tree,
                         "(sequence ()"
                         "  (sequence ()"
                         "    (object-identifier ())"
                         "    (null ()))"
                         "  (any ()))",
                         oids->oid, prv_key);
  if (status != SSH_ASN1_STATUS_OK)
    {
      rv = SSH_X509_FAILED_ASN1_ENCODE;
      goto failed;
    }

  status = ssh_asn1_encode(context, tree);
  if (status != SSH_ASN1_STATUS_OK)
    {
      rv = SSH_X509_FAILED_ASN1_ENCODE;
      goto failed;
    }

  ssh_asn1_get_data(tree, buf, buf_len);

  rv = SSH_X509_OK;
failed:
  ssh_asn1_free(context);
  return rv;
}

SshPrivateKey ssh_x509_decode_private_key(const unsigned char *buf,
                                          size_t buf_len)
{
  SshAsn1Context context;
  SshAsn1Tree tree;
  SshPrivateKey private_key;
  SshAsn1Node prv_key, params;
  const SshOidStruct *oids;
  unsigned char *oid;
  SshAsn1Status status;
  SshCryptoStatus crypt_status;
  SshMPIntegerStruct n, p, q, e, d, u, g, y, x;

  /* Initialize. */
  private_key = NULL;

  /* Initialize ASN.1 context. */
  if ((context = ssh_asn1_init()) == NULL)
    return NULL;

  status =
    ssh_asn1_decode(context, buf, buf_len, &tree);
  if (status != SSH_ASN1_STATUS_OK &&
      status != SSH_ASN1_STATUS_OK_GARBAGE_AT_END &&
      status != SSH_ASN1_STATUS_BAD_GARBAGE_AT_END)
    goto failed;

  status =
    ssh_asn1_read_tree(context, tree,
                       "(sequence ()"
                       "  (sequence ()"
                       "    (object-identifier ())"
                       "    (any ()))"
                       "  (any ()))",
                       &oid,
                       &params,
                       &prv_key);
  if (status != SSH_ASN1_STATUS_OK)
    goto failed;

  oids = ssh_oid_find_by_oid_of_type(oid, SSH_OID_PK);
  /* Free the oid array. */
  ssh_free(oid);

  if (oids == NULL)
    goto failed;

  switch (((SshOidPk)oids->extra)->alg_enum)
    {
    case SSH_X509_PKALG_RSA:
      ssh_mprz_init(&n);
      ssh_mprz_init(&e);
      ssh_mprz_init(&d);
      ssh_mprz_init(&p);
      ssh_mprz_init(&q);
      ssh_mprz_init(&u);

      status =
        ssh_asn1_read_node(context, prv_key,
                           "(sequence ()"
                           "  (integer ())"  /* n */
                           "  (integer ())"  /* e */
                           "  (integer ())"  /* d */
                           "  (integer ())"  /* p */
                           "  (integer ())"  /* q */
                           "  (integer ()))", /* u */
                           &n, &e, &d, &p, &q, &u);

      if (status != SSH_ASN1_STATUS_OK)
        {
          ssh_mprz_clear(&n);
          ssh_mprz_clear(&e);
          ssh_mprz_clear(&d);
          ssh_mprz_clear(&p);
          ssh_mprz_clear(&q);
          ssh_mprz_clear(&u);
          goto failed;
        }

      crypt_status =
        ssh_private_key_define(&private_key,
                               oids->name,
                               SSH_PKF_MODULO_N, &n,
                               SSH_PKF_PUBLIC_E, &e,
                               SSH_PKF_SECRET_D, &d,
                               SSH_PKF_PRIME_P,  &p,
                               SSH_PKF_PRIME_Q,  &q,
                               SSH_PKF_INVERSE_U, &u,
                               SSH_PKF_END);
      ssh_mprz_clear(&n);
      ssh_mprz_clear(&e);
      ssh_mprz_clear(&d);
      ssh_mprz_clear(&p);
      ssh_mprz_clear(&q);
      ssh_mprz_clear(&u);
      if (crypt_status != SSH_CRYPTO_OK)
        {
          private_key = NULL;
          goto failed;
        }
      break;
    case SSH_X509_PKALG_DSA:
      ssh_mprz_init(&p);
      ssh_mprz_init(&q);
      ssh_mprz_init(&g);
      ssh_mprz_init(&y);
      ssh_mprz_init(&x);

      status =
        ssh_asn1_read_node(context, prv_key,
                           "(sequence ()"
                           "(integer ())"  /* p */
                           "(integer ())"  /* q */
                           "(integer ())"  /* g */
                           "(integer ())"  /* y */
                           "(integer ()))", /* x */
                           &p, &q, &g, &y, &x);
      if (status != SSH_ASN1_STATUS_OK)
        {
          ssh_mprz_clear(&p);
          ssh_mprz_clear(&g);
          ssh_mprz_clear(&q);
          ssh_mprz_clear(&y);
          ssh_mprz_clear(&x);
          goto failed;
        }

      crypt_status =
        ssh_private_key_define(&private_key,
                               oids->name,
                               SSH_PKF_PRIME_P, &p,
                               SSH_PKF_PRIME_Q, &q,
                               SSH_PKF_GENERATOR_G, &g,
                               SSH_PKF_PUBLIC_Y, &y,
                               SSH_PKF_SECRET_X, &x,
                               SSH_PKF_END);
      ssh_mprz_clear(&p);
      ssh_mprz_clear(&g);
      ssh_mprz_clear(&q);
      ssh_mprz_clear(&y);
      ssh_mprz_clear(&x);
      if (crypt_status != SSH_CRYPTO_OK)
        {
          private_key = NULL;
          goto failed;
        }
      break;
    default:
      break;
    }

failed:
  ssh_asn1_free(context);
  return private_key;
}
