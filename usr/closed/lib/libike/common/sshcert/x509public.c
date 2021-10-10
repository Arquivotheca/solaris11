/*
  x509public.c

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
  Espoo, Finland
  All rights reserved.

  Decode and encode routines for x509 public keys.
*/

#include "sshincludes.h"
#include "x509.h"
#include "x509internal.h"
#include "oid.h"

#define SSH_DEBUG_MODULE "SshCertX509"

SshX509Status ssh_x509_decode_public_key(SshAsn1Context context,
                                         SshAsn1Node pk_info,
                                         SshX509PublicKey pkey)
{
  unsigned char *pk;
  unsigned char *pk_oid;
  SshMPIntegerStruct n, e, p, q, g, y;
  size_t pk_len;
  SshAsn1Node params, pub_key;
  SshAsn1Status status;
  const SshOidStruct *oid;
  SshAsn1Tree tree;
  unsigned int which;
  SshX509Status rv;

  /* Decode the input blob. */
  status =
    ssh_asn1_read_node(context, pk_info,
                       "(sequence ()"
                       "  (sequence ()"             /* Algorithm identifier! */
                       "    (object-identifier ())" /* object identifier */
                       "    (any ()))"              /* any by algorithm */
                       "  (bit-string ()))",        /* the public key */
                       &pk_oid,
                       &params,
                       &pk, &pk_len);

  if (status != SSH_ASN1_STATUS_OK)
    return SSH_X509_FAILED_PUBLIC_KEY_OPS;

  /* First figure out information about the name of the algorithm. */
  oid = ssh_oid_find_by_oid_of_type(pk_oid, SSH_OID_PK);
  ssh_free(pk_oid);

  if (oid == NULL)
    return SSH_X509_FAILED_UNKNOWN_VALUE;

  /* Set output fields. */
  pkey->pk_type                 = ((SshOidPk)oid->extra)->alg_enum;
  pkey->subject_key_usage_mask  = ((SshOidPk)oid->extra)->key_usage;
  pkey->ca_key_usage_mask       = ((SshOidPk)oid->extra)->ca_key_usage;

  /* Now lets try to find out what the bit string keeps in itself. Then
     take the first node which should be the public key. */
  status = ssh_asn1_decode(context, pk, pk_len/8, &tree); /* pk_len nin bits */
  ssh_free(pk);

  if (status != SSH_ASN1_STATUS_OK)
    return SSH_X509_FAILED_ASN1_DECODE;
  pub_key = ssh_asn1_get_current(tree);

  rv = SSH_X509_FAILED_PUBLIC_KEY_OPS;

  /* We have here at the moment very simple oid->index number which can
     be used to refer into some algorithm internally. E.g. what is being
     done here. */
  switch (((SshOidPk)oid->extra)->alg_enum)
    {
    case SSH_X509_PKALG_RSA:
      ssh_mprz_init(&n);
      ssh_mprz_init(&e);

      /* Get public key. */
      status =
        ssh_asn1_read_node(context, pub_key,
                           "(sequence ()"
                           "  (integer ())"    /* n -- the modulus */
                           "  (integer ()))",  /* e -- the exponent */
                           &n, &e);
      if (status != SSH_ASN1_STATUS_OK)
        {
          rv = SSH_X509_FAILED_ASN1_DECODE;
          goto rsa_failed;
        }
      /* Define the public key. */
      if (ssh_public_key_define(&pkey->public_key, oid->name,
                                SSH_PKF_MODULO_N, &n,
                                SSH_PKF_PUBLIC_E, &e,
                                SSH_PKF_END) != SSH_CRYPTO_OK)
        {
          rv = SSH_X509_FAILED_PUBLIC_KEY_OPS;
          goto rsa_failed;
        }

      rv = SSH_X509_OK;

    rsa_failed:
      ssh_mprz_clear(&e);
      ssh_mprz_clear(&n);
      break;

    case SSH_X509_PKALG_DSA:
      /* Initialize temporary variables for the key. */
      ssh_mprz_init(&p);
      ssh_mprz_init(&q);
      ssh_mprz_init(&g);
      ssh_mprz_init(&y);

      /* With DSA we don't want to skip parameters ;) */
      status =
        ssh_asn1_read_node(context, params,
                           "(choice "
                           "  (null ())"
                           "  (sequence ()"
                           "  (integer ())"   /* p -- the field modulus */
                           "  (integer ())"   /* q -- the order of generator */
                           "  (integer ())))", /* g -- the generator */
                           &which, &p, &q, &g);
      if (status != SSH_ASN1_STATUS_OK || which == 0)
        {
          SSH_DEBUG(SSH_D_FAIL, ("DSA params read failed."));
          rv = SSH_X509_FAILED_ASN1_DECODE;
          goto dsa_failed;
        }

      /* Parse DSA public key. */
      status =
        ssh_asn1_read_node(context, pub_key,
                           "(integer ())",   /* this is easy, public key y */
                           &y);
      if (status != SSH_ASN1_STATUS_OK)
        {
          SSH_DEBUG(SSH_D_FAIL, ("DSA public key read failed."));
          rv = SSH_X509_FAILED_ASN1_DECODE;
          goto dsa_failed;
        }

      /* Should be called only if parameters available! */
      if (ssh_public_key_define(&pkey->public_key, oid->name,
                                SSH_PKF_PRIME_P, &p,
                                SSH_PKF_PRIME_Q, &q,
                                SSH_PKF_GENERATOR_G, &g,
                                SSH_PKF_PUBLIC_Y, &y,
                                SSH_PKF_END) != SSH_CRYPTO_OK)
        {
          SSH_DEBUG(SSH_D_FAIL, ("DSA public key define failed."));
          rv = SSH_X509_FAILED_PUBLIC_KEY_OPS;
          goto dsa_failed;
        }

      rv = SSH_X509_OK;

    dsa_failed:
      ssh_mprz_clear(&p);
      ssh_mprz_clear(&q);
      ssh_mprz_clear(&g);
      ssh_mprz_clear(&y);
      break;

    case SSH_X509_PKALG_DH:
      /* Initialize temporary variables for the key. */
      ssh_mprz_init(&p);
      ssh_mprz_init(&q);
      ssh_mprz_init(&g);

      /* With Diffie-Hellman we don't want to skip parameters ;) */
      status =
        ssh_asn1_read_node(context, params,
                           "(choice "
                           "  (null ())"
                           "  (sequence ()"
                           "  (integer ())"   /* p -- the field modulus */
                           "  (integer ())"   /* q -- the order of generator */
                           "  (integer ())))", /* g -- the generator */
                           &which, &p, &q, &g);
      if (status != SSH_ASN1_STATUS_OK || which == 0)
        {
          rv = SSH_X509_FAILED_ASN1_DECODE;
          goto dh_failed;
        }

      /* Should be called only if parameters available! */
      if (ssh_pk_group_generate(&pkey->public_group, oid->name,
                                SSH_PKF_PRIME_P, &p,
                                SSH_PKF_PRIME_Q, &q,
                                SSH_PKF_GENERATOR_G, &g,
                                SSH_PKF_END) != SSH_CRYPTO_OK)
        {
          rv = SSH_X509_FAILED_PUBLIC_KEY_OPS;
          goto dh_failed;
        }

      rv = SSH_X509_OK;

    dh_failed:
      ssh_mprz_clear(&p);
      ssh_mprz_clear(&q);
      ssh_mprz_clear(&g);
      break;

    default:
      break;
    }

  return rv;
}

SshAsn1Node ssh_x509_encode_public_key_internal(SshAsn1Context context,
                                                SshPublicKey key)
{
  SshAsn1Node pk_param, pk_info;
  SshAsn1Tree pk_tree;
  SshAsn1Status status;
  const SshOidStruct *oids;
  unsigned char *pk;
  size_t pk_len;
  const SshX509PkAlgorithmDefStruct *algorithm;
  SshMPIntegerStruct n, e, p, q, g, y;
  Boolean ok;

  /* Encode information about the public key. */

  if (key == NULL)
    return NULL;

  algorithm = ssh_x509_public_key_algorithm(key);
  if (algorithm == NULL)
    return NULL;

  oids = ssh_oid_find_by_std_name_of_type(algorithm->known_name, SSH_OID_PK);
  if (oids == NULL)
    return NULL;

  /* Initialize pointers. */
  pk_param = NULL;
  pk_tree = NULL;

  ok = FALSE;

  switch (algorithm->algorithm)
    {
    case SSH_X509_PKALG_RSA:
      ssh_mprz_init(&n);
      ssh_mprz_init(&e);

      /* Create null parameters. */
      status = ssh_asn1_create_node(context, &pk_param, "(null ())");
      if (status != SSH_ASN1_STATUS_OK)
        goto rsa_failed;

      if (ssh_public_key_get_info(key,
                                  SSH_PKF_MODULO_N, &n,
                                  SSH_PKF_PUBLIC_E, &e,
                                  SSH_PKF_END) != SSH_CRYPTO_OK)
        goto rsa_failed;

      /* Create the tree out of the public key. */
      status =
        ssh_asn1_create_tree(context, &pk_tree,
                             "(sequence ()"
                             "(integer ())"   /* n */
                             "(integer ()))", /* e */
                             &n, &e);
      if (status != SSH_ASN1_STATUS_OK)
        goto rsa_failed;

      ok = TRUE;

    rsa_failed:
      ssh_mprz_clear(&n);
      ssh_mprz_clear(&e);
      break;

    case SSH_X509_PKALG_DSA:
      ssh_mprz_init(&p);
      ssh_mprz_init(&q);
      ssh_mprz_init(&g);
      ssh_mprz_init(&y);

      if (ssh_public_key_get_info(key,
                                  SSH_PKF_PRIME_P, &p,
                                  SSH_PKF_PRIME_Q, &q,
                                  SSH_PKF_GENERATOR_G, &g,
                                  SSH_PKF_PUBLIC_Y, &y,
                                  SSH_PKF_END) != SSH_CRYPTO_OK)
        goto dsa_failed;

      status =
        ssh_asn1_create_node(context, &pk_param,
                             "(sequence ()"
                             "  (integer ())"
                             "  (integer ())"
                             "  (integer ()))",
                             &p, &q, &g);
      if (status != SSH_ASN1_STATUS_OK)
        goto dsa_failed;

      status =
        ssh_asn1_create_tree(context, &pk_tree,
                             "(integer ())",
                             &y);

      if (status != SSH_ASN1_STATUS_OK)
        goto dsa_failed;

      ok = TRUE;

    dsa_failed:
      ssh_mprz_clear(&p);
      ssh_mprz_clear(&q);
      ssh_mprz_clear(&y);
      ssh_mprz_clear(&g);
      break;

    default:
      ssh_fatal("ssh_x509_encode_public_key: algorithm detection failed.");
      break;
    }

  pk_info = NULL;
  if (ok)
    {
      status = ssh_asn1_encode(context, pk_tree);
      if (status == SSH_ASN1_STATUS_OK)
        {
          ssh_asn1_get_data(pk_tree, &pk, &pk_len);
          status = ssh_asn1_create_node(context, &pk_info,
                                        "(sequence ()"
                                        "  (sequence ()"
                                        "    (object-identifier ())"
                                        "    (any ()))"
                                        "  (bit-string ()))",
                                        oids->oid,
                                        pk_param,
                                        pk, pk_len * 8);
          ssh_free(pk);

          if (status != SSH_ASN1_STATUS_OK)
            pk_info = NULL;
        }
    }
  return pk_info;
}

SshAsn1Node ssh_x509_encode_public_group_internal(SshAsn1Context context,
                                                  SshPkGroup pk_group)
{
  SshAsn1Node pk_param, pk_info;
  SshAsn1Tree pk_tree;
  SshAsn1Status status;
  const SshOidStruct *oids;
  const SshX509PkAlgorithmDefStruct *algorithm;
  SshMPIntegerStruct p, q, g;
  Boolean ok;

  /* Encode information about the public key. */
  if (pk_group == NULL)
    return NULL;

  algorithm = ssh_x509_public_group_algorithm(pk_group);
  if (algorithm == NULL)
    return NULL;

  oids = ssh_oid_find_by_std_name_of_type(algorithm->known_name, SSH_OID_PK);
  if (oids == NULL)
    return NULL;

  /* Initialize pointers. */
  pk_param = NULL;
  pk_tree = NULL;

  ok = FALSE;
  switch (algorithm->algorithm)
    {
    case SSH_X509_PKALG_DH:
      ssh_mprz_init(&p);
      ssh_mprz_init(&q);
      ssh_mprz_init(&g);

      if (ssh_pk_group_get_info(pk_group,
                                SSH_PKF_PRIME_P, &p,
                                SSH_PKF_PRIME_Q, &q,
                                SSH_PKF_GENERATOR_G, &g,
                                SSH_PKF_END) != SSH_CRYPTO_OK)
        goto dh_failed;

      status =
        ssh_asn1_create_node(context, &pk_param,
                             "(sequence ()"
                             "  (integer ())"
                             "  (integer ())"
                             "  (integer ()))",
                             &p, &q, &g);
      if (status != SSH_ASN1_STATUS_OK)
        goto dh_failed;

      ok = TRUE;

    dh_failed:
      ssh_mprz_clear(&p);
      ssh_mprz_clear(&q);
      ssh_mprz_clear(&g);
      break;

    default:
      ssh_fatal("ssh_x509_encode_public_key: algorithm detection failed.");
      break;
    }

  pk_info = NULL;
  if (ok)
    {
      status =
        ssh_asn1_create_node(context, &pk_info,
                             "(sequence ()"
                             "  (sequence ()"
                             "    (object-identifier ())"
                             "    (any ())))",
                             oids->oid,
                             pk_param);
      if (status != SSH_ASN1_STATUS_OK)
        pk_info = NULL;
    }

  return pk_info;
}

SshAsn1Node ssh_x509_encode_public_key(SshAsn1Context context,
                                       SshX509PublicKey pkey)
{
  if (pkey == NULL)
    return NULL;

  if (pkey->public_key)
    return ssh_x509_encode_public_key_internal(context, pkey->public_key);
  if (pkey->public_group)
    return ssh_x509_encode_public_group_internal(context, pkey->public_group);
  return NULL;
}

/* This function computes standard PKIX key identifier for the
   certificate. The method is as RFC2459 section 4.2.1.2 suggests.
   The function returns NULL if the certificate does not contain
   public key. */
unsigned char *
ssh_x509_cert_compute_key_identifier(SshX509Certificate c,
                                     const char *hash_algorithm,
                                     size_t *kid_len)
{
  SshAsn1Node node, any;
  SshAsn1Context context;
  unsigned char *oid, *pk, *kid = NULL;
  size_t pkbits;

  *kid_len = 0;

  if (c->subject_pkey.pk_type == SSH_X509_PKALG_UNKNOWN)
    return NULL;
  else
    {
      if ((context = ssh_asn1_init()) == NULL)
        return NULL;

      node = ssh_x509_encode_public_key(context, &c->subject_pkey);
      if (node)
        {
          if (ssh_asn1_read_node(context, node,
                                 "(sequence ()"
                                 "  (sequence ()"
                                 "    (object-identifier ())"
                                 "    (any ()))"
                                 "  (bit-string ()))",
                                 &oid, &any, &pk, &pkbits)
              == SSH_ASN1_STATUS_OK)
            {
              SshHash hash;

              if (ssh_hash_allocate(hash_algorithm, &hash) == SSH_CRYPTO_OK)
                {
                  *kid_len = ssh_hash_digest_length(hash_algorithm);
                  if ((kid = ssh_malloc(*kid_len)) != NULL)
                    {
                      ssh_hash_update(hash, pk, pkbits/8);
                      ssh_hash_final(hash, kid);
                    }
                  ssh_hash_free(hash);
                }
              ssh_free(oid);
              ssh_free(pk);
            }
        }
      ssh_asn1_free(context);
    }
  return kid;
}
