/*
  cmi-key.c

  Copyright:
        Copyright (c) 2002, 2003 SFNT Finland Oy.
	All rights reserved.

  Validator search key handling routines.
*/

#include "sshincludes.h"
#include "cmi.h"
#include "cmi-internal.h"

#define SSH_DEBUG_MODULE "SshCertCMiKey"

/* We use here key types defined in cert-db.h and x509.h. These types
   are not compatible, and thus we need to glue them together here. It
   would be nice to have just one key type, but that is a bit
   difficult. */

static Boolean
cm_key_set_name_from_dn(SshCertDBKey **key, SshCMKeyType type, SshDN dn)
{
  unsigned char *der;
  size_t der_len;

  if (ssh_dn_encode_der_canonical(dn, &der, &der_len, NULL) == 0)
    {
      SSH_DEBUG(SSH_D_FAIL, ("Failure; can't encode DN to canonical DER."));
      ssh_dn_clear(dn);
      return FALSE;
    }
  ssh_dn_clear(dn);
  return ssh_certdb_key_push(key, type, der, der_len);
}

Boolean
ssh_cm_key_canonical_dn(SshCertDBKey **key,
                        unsigned int tag,
                        const unsigned char *ber, size_t ber_len)
{
  SshDNStruct dn;

  SSH_DEBUG(SSH_D_MIDSTART, ("Put canonical dn to key list."));

  ssh_dn_init(&dn);
  if (ssh_dn_decode_der(ber, ber_len, &dn, NULL) == 0)
    {
      ssh_dn_clear(&dn);
      return FALSE;
    }
  return cm_key_set_name_from_dn(key, tag, &dn);
}

Boolean
ssh_cm_key_set_ldap_dn(SshCertDBKey **key, const char *ldap_dn)
{
  SshDNStruct dn;

  SSH_DEBUG(SSH_D_MIDSTART, ("Put LDAP DN to key list."));

  ssh_dn_init(&dn);
  if (ssh_dn_decode_ldap(ssh_custr(ldap_dn), &dn) == 0)
    {
      SSH_DEBUG(SSH_D_FAIL, ("Failure; can't decode LDAP name."));
      ssh_dn_clear(&dn);
      return FALSE;
    }
  return cm_key_set_name_from_dn(key, SSH_CM_KEY_TYPE_DISNAME, &dn);
}

Boolean
ssh_cm_key_set_dn(SshCertDBKey **key,
                  const unsigned char *der_dn, size_t der_dn_len)
{
  SshDNStruct dn;

  SSH_DEBUG(SSH_D_MIDSTART, ("Put DN to key list."));

  ssh_dn_init(&dn);
  if (ssh_dn_decode_der(der_dn, der_dn_len, &dn, NULL) == 0)
    {
      SSH_DEBUG(SSH_D_FAIL, ("Failure; can't decode LDAP name."));
      ssh_dn_clear(&dn);
      return FALSE;
    }
  return cm_key_set_name_from_dn(key, SSH_CM_KEY_TYPE_DISNAME, &dn);
}

Boolean
ssh_cm_key_set_directory_name(SshCertDBKey **key, const char *ldap_dn)
{
  SshDNStruct dn;

  SSH_DEBUG(SSH_D_MIDSTART, ("Put LDAP directory name to key list."));

  ssh_dn_init(&dn);
  if (ssh_dn_decode_ldap(ssh_custr(ldap_dn), &dn) == 0)
    {
      SSH_DEBUG(SSH_D_FAIL, ("Failure; can't encode LDAP name to DN."));
      ssh_dn_clear(&dn);
      return FALSE;
    }

  return cm_key_set_name_from_dn(key, SSH_CM_KEY_TYPE_DIRNAME, &dn);
}

Boolean
ssh_cm_key_set_directory_name_der(SshCertDBKey **key,
                                  const unsigned char *der_dn,
                                  size_t der_dn_len)
{
  SshDNStruct dn;

  SSH_DEBUG(SSH_D_MIDSTART, ("Put directory name to key list."));

  ssh_dn_init(&dn);
  if (ssh_dn_decode_der(der_dn, der_dn_len, &dn, NULL) == 0)
    {
      SSH_DEBUG(SSH_D_FAIL, ("Failure; can't decode DER to DN."));
      ssh_dn_clear(&dn);
      return FALSE;
    }
  return cm_key_set_name_from_dn(key, SSH_CM_KEY_TYPE_DIRNAME, &dn);
}

Boolean ssh_cm_key_set_dns(SshCertDBKey **key,
                           const char *dns, size_t dns_len)
{
  unsigned char *buf;

  SSH_DEBUG(SSH_D_MIDSTART, ("Put dns to key list."));

  if (dns_len == 0)
    dns_len = strlen(dns);

  if ((buf = ssh_memdup(dns, dns_len)) != NULL)
    {
      return ssh_certdb_key_push(key, SSH_CM_KEY_TYPE_DNS, buf, dns_len);
    }
  return FALSE;
}

Boolean
ssh_cm_key_set_email(SshCertDBKey **key,
                     const char *email, size_t email_len)
{
  unsigned char *buf;

  SSH_DEBUG(SSH_D_MIDSTART, ("Put email to key list."));

  if (email_len == 0)
    email_len = strlen(email);

  if ((buf = ssh_memdup(email, email_len)) != NULL)
    {
      return ssh_certdb_key_push(key, SSH_CM_KEY_TYPE_RFC822, buf, email_len);
    }
  return FALSE;
}

Boolean
ssh_cm_key_set_uri(SshCertDBKey **key,
                   const char *uri, size_t uri_len)
{
  unsigned char *buf;

  SSH_DEBUG(SSH_D_MIDSTART, ("Put uri to key list."));

  if (uri_len == 0)
    uri_len = strlen(uri);

  if ((buf = ssh_memdup(uri, uri_len)) != NULL)
    {
      return ssh_certdb_key_push(key, SSH_CM_KEY_TYPE_URI, buf, uri_len);
    }
  return FALSE;
}

Boolean
ssh_cm_key_set_rid(SshCertDBKey **key,
                   const char *rid, size_t rid_len)
{
  unsigned char *buf;

  SSH_DEBUG(SSH_D_MIDSTART, ("Put rid to key list."));

  if (rid_len == 0)
    rid_len = strlen(rid);

  if ((buf = ssh_memdup(rid, rid_len)) != NULL)
    {
      return ssh_certdb_key_push(key, SSH_CM_KEY_TYPE_RID, buf, rid_len);
    }
  return FALSE;
}

Boolean
ssh_cm_key_set_ip(SshCertDBKey **key,
                  const unsigned char *ip, size_t ip_len)
{
  unsigned char *buf;

  SSH_DEBUG(SSH_D_MIDSTART, ("Put ip to key list."));

  if ((buf = ssh_memdup(ip, ip_len)) != NULL)
    {
      return ssh_certdb_key_push(key, SSH_CM_KEY_TYPE_IP, buf, ip_len);
    }
  return FALSE;
}

Boolean
ssh_cm_key_set_serial_no(SshCertDBKey **key, SshMPInteger serial_no)
{
  unsigned char *buf;
  size_t buf_len;

  SSH_DEBUG(SSH_D_MIDSTART, ("Put serial no to key list."));

  buf_len = (ssh_mprz_get_size(serial_no, 2) + 7)/8;
  if (buf_len == 0)
    buf_len = 1;

  /* Function to get the serial number msb first the buffer is filled
     throughout. */

  if ((buf = ssh_calloc(1, buf_len)) != NULL)
    {
      ssh_mprz_get_buf(buf, buf_len, serial_no);
      return ssh_certdb_key_push(key, SSH_CM_KEY_TYPE_SERIAL_NO, buf, buf_len);
    }
  return FALSE;
}

Boolean ssh_cm_key_kid_create(SshPublicKey public_key,
                              unsigned char **buf_ret,
                              size_t *len_ret)
{
  SshX509CertificateStruct c;
  const SshX509PkAlgorithmDefStruct *pkalg;

  if (public_key == NULL)
    {
      SSH_DEBUG(SSH_D_FAIL, ("Public key not provided."));
      return FALSE;
    }

  if ((pkalg = ssh_x509_public_key_algorithm(public_key)) != NULL)
    {
      c.subject_pkey.pk_type = pkalg->algorithm;
      c.subject_pkey.public_key = public_key;

      if ((*buf_ret =
           ssh_x509_cert_compute_key_identifier(&c,
                                                SSH_CM_HASH_ALGORITHM,
                                                len_ret)) != NULL)
        return TRUE;
    }
  return FALSE;
}

Boolean
ssh_cm_key_set_public_key(SshCertDBKey **key, SshPublicKey public_key)
{
  unsigned char *key_digest;
  size_t digest_len;

  if (!ssh_cm_key_kid_create(public_key, &key_digest, &digest_len))
    return FALSE;

  return ssh_certdb_key_push(key,
                             SSH_CM_KEY_TYPE_PUBLIC_KEY_ID,
                             key_digest, digest_len);
}

/* Handles the local DB id number. */
Boolean
ssh_cm_key_set_cache_id(SshCertDBKey **key, unsigned int id)
{
  unsigned char *buf;

  if ((buf = ssh_calloc(1, sizeof(unsigned int))) != NULL)
    {
      SSH_DEBUG(SSH_D_MIDSTART, ("Put cache identifier to key list."));
      *((unsigned int *)buf) = id;
      return ssh_certdb_key_push(key,
                                 SSH_CM_KEY_TYPE_IDNUMBER,
                                 buf, sizeof(unsigned int));
    }
  return FALSE;
}

/* Question: does the critical names weight more than the
   non-critical? This will return TRUE, if at least one of the names
   were assigned into keys */

Boolean
ssh_cm_key_convert_from_x509_name(SshCertDBKey **key, SshX509Name name)
{
  unsigned char *buf;
  size_t buf_len, nassigned = 0;
  Boolean rv;

  /* Push the names in X.509 name structure into the cert db format. */
  for (; name; name = name->next)
    {
      rv = FALSE;

      switch (name->type)
        {
        case SSH_X509_NAME_RFC822:
          /* Copy name and push to the list. */
          buf = ssh_str_get(name->name, &buf_len);
          rv = ssh_certdb_key_push(key, SSH_CM_KEY_TYPE_RFC822, buf, buf_len);
          break;
        case SSH_X509_NAME_DNS:
          /* Copy name and push to the list. */
          buf = ssh_str_get(name->name, &buf_len);
          rv = ssh_certdb_key_push(key, SSH_CM_KEY_TYPE_DNS, buf, buf_len);
          break;
        case SSH_X509_NAME_URI:
          /* Copy name and push to the list. */
          buf = ssh_str_get(name->name, &buf_len);
          rv = ssh_certdb_key_push(key, SSH_CM_KEY_TYPE_URI, buf, buf_len);
          break;
        case SSH_X509_NAME_IP:
          if (name->data_len)
            rv = ssh_certdb_key_push(key, SSH_CM_KEY_TYPE_IP,
                                     ssh_memdup(name->data, name->data_len),
                                     name->data_len);
          break;
        case SSH_X509_NAME_X400:
          if (name->data_len)
            rv = ssh_certdb_key_push(key, SSH_CM_KEY_TYPE_X400,
                                     ssh_memdup(name->data, name->data_len),
                                     name->data_len);
          break;
        case SSH_X509_NAME_OTHER:
          if (name->data_len)
            rv = ssh_certdb_key_push(key, SSH_CM_KEY_TYPE_OTHER,
                                     ssh_memdup(name->data, name->data_len),
                                     name->data_len);
          break;
        case SSH_X509_NAME_RID:
          if (name->data_len)
            rv = ssh_certdb_key_push(key, SSH_CM_KEY_TYPE_RID,
                                     ssh_memdup(name->data, name->data_len),
                                     name->data_len);
          break;
        case SSH_X509_NAME_DN:
          /* Push the name into the key stack after transformation,
             which makes the name as canonical as possible. */
          rv = ssh_cm_key_canonical_dn(key, SSH_CM_KEY_TYPE_DIRNAME,
                                       name->ber, name->ber_len);
          break;
        case SSH_X509_NAME_UNIQUE_ID:
          if (name->data_len)
            rv = ssh_certdb_key_push(key, SSH_CM_KEY_TYPE_RID,
                                     ssh_memdup(name->data, name->data_len),
                                     name->data_len);
          break;
        case SSH_X509_NAME_DISTINGUISHED_NAME:
          /* Push the name into the key stack after transformation,
             which makes the name as canonical as possible. */
          rv = ssh_cm_key_canonical_dn(key, SSH_CM_KEY_TYPE_DISNAME,
                                       name->ber, name->ber_len);
          break;
          /* Following names are not supported yet. */
        default:
          rv = FALSE;
          break;
        }

      if (rv)
        nassigned += 1;
    }

  return nassigned != 0;
}

unsigned char *
ssh_cm_get_hash_of_serial_no_and_issuer_name(SshMPInteger serial_no,
                                             SshX509Name names,
                                             size_t *out_len)
{
  /* Search for the distinguished name. */
  for (; names; names = names->next)
    {
      if (names->type == SSH_X509_NAME_DISTINGUISHED_NAME)
        {
          unsigned char *buf, *ber, *key_digest;
          size_t         buf_len, ber_len;
          SshHash        hash;
          unsigned char  digest[SSH_MAX_HASH_DIGEST_LENGTH];
          size_t         digest_len;
          SshDNStruct    dn;

          /* Handle this as the only case. */
          buf_len = (ssh_mprz_get_size(serial_no, 2) + 7)/8;
          if (buf_len == 0)
            buf_len = 1;

          if ((buf = ssh_calloc(1, buf_len)) == NULL)
            return NULL;

          ssh_mprz_get_buf(buf, buf_len, serial_no);

          /* XXX: memory issues. */

          /* Convert the distinguished name too. */
          ssh_dn_init(&dn);
          if (ssh_dn_decode_der(names->ber, names->ber_len, &dn, NULL) == 0)
            {
              ssh_free(buf);
              ssh_dn_clear(&dn);
              return NULL;
            }
          if (ssh_dn_encode_der_canonical(&dn, &ber, &ber_len, NULL) == 0)
            {
              ssh_free(buf);
              ssh_dn_clear(&dn);
              return NULL;
            }

          /* Clear the DN data structure. */
          ssh_dn_clear(&dn);

          /* Do the hashing. */
          if (ssh_hash_allocate(SSH_CM_HASH_ALGORITHM, &hash) != SSH_CRYPTO_OK)
            {
              ssh_free(buf);
              ssh_free(ber);
              return NULL;
            }

          ssh_hash_reset(hash);
          ssh_hash_update(hash, buf, buf_len);
          ssh_hash_update(hash, ber, ber_len);
          ssh_hash_final(hash, digest);
          digest_len = ssh_hash_digest_length(ssh_hash_name(hash));
          ssh_hash_free(hash);

          /* Free the allocated buffers. */
          ssh_free(buf);
          ssh_free(ber);

          if (digest_len > 20)
            digest_len = 20;

          if ((key_digest = ssh_memdup(digest, digest_len)) == NULL)
            digest_len = 0;

          *out_len = digest_len;
          return key_digest;
        }
    }
  *out_len = 0;
  return NULL;
}

Boolean
ssh_cm_key_set_from_cert(SshCertDBKey **key,
                         SshCMKeyClass classp, SshCMCertificate cm_cert)
{
  SshX509Certificate cert = cm_cert->cert;
  SshX509ExtInfoAccess aia, a;
#if 0
  SshX509ExtKeyId aki;
#endif
  Boolean critical;
  unsigned char *buf;
  size_t buf_len, nassigned = 0;

  if (cert == NULL)
    return FALSE;

  SSH_DEBUG(SSH_D_MIDOK,
            ("Put certificate (%s) names to key list.",
             (classp == SSH_CM_KEY_CLASS_SUBJECT ? "subject" : "issuer")));

  switch (classp)
    {
    case SSH_CM_KEY_CLASS_SUBJECT:
      /* Throw in only the subjects names. */
      if (ssh_cm_key_convert_from_x509_name(key, cert->subject_name))
        nassigned += 1;

      if (ssh_cm_key_convert_from_x509_name(key,
                                            cert->
                                            extensions.subject_alt_names))
        nassigned += 1;

      /* Also the serial number which is unique under the CA. */
      if (ssh_cm_key_set_serial_no(key, &cert->serial_number))
        nassigned += 1;

      /* Set also the public key for identification. */
      if (ssh_cm_key_set_public_key(key, cert->subject_pkey.public_key))
        nassigned += 1;

      /* Set also the SI_HASH. */
      buf = ssh_cm_get_hash_of_serial_no_and_issuer_name(&cert->serial_number,
                                                         cert->issuer_name,
                                                         &buf_len);
      if (buf)
        if (ssh_certdb_key_push(key, SSH_CM_KEY_TYPE_SI_HASH,
                                buf, buf_len))
          nassigned += 1;

#if 0
      if (ssh_x509_cert_get_subject_key_id(cert, &buf, &buf_len, &critical))
        if (ssh_certdb_key_push(key,
                                SSH_CM_KEY_TYPE_PUBLIC_KEY_ID,
                                ssh_memdup(buf, buf_len), buf_len))
          nassigned += 1;
#endif

      break;
    case SSH_CM_KEY_CLASS_ISSUER:
      /* Certs can be found from location given at authority info
         access, */
      if (ssh_x509_cert_get_auth_info_access(cert, &aia, &critical))
        {
          for (a = aia; a; a = a->next)
            {
              if (!strcmp(a->access_method, "1.3.6.1.5.5.7.48.2"))
                {
                  if (ssh_cm_key_convert_from_x509_name(key,
                                                        a->access_location))
                    nassigned += 1;
                }
            }
        }
      /* or from the issuer names. */
      if (ssh_cm_key_convert_from_x509_name(key, cert->issuer_name))
        nassigned += 1;
      if (ssh_cm_key_convert_from_x509_name(key,
                                            cert->
                                            extensions.issuer_alt_names))
        nassigned += 1;

#if 0
      if (ssh_x509_cert_get_authority_key_id(cert, &aki, &critical))
        {
          if (aki->key_id_len)
            {
              if (ssh_certdb_key_push(key,
                                      SSH_CM_KEY_TYPE_PUBLIC_KEY_ID,
                                      ssh_memdup(aki->key_id, aki->key_id_len),
                                      aki->key_id_len))
                nassigned += 1;
            }
          else
            {
              if ((buf =
                   ssh_cm_get_hash_of_serial_no_and_issuer_name(
                                          &aki->auth_cert_serial_number,
                                          aki->auth_cert_issuer,
                                          &buf_len)) != NULL)
                if (ssh_certdb_key_push(key,
                                        SSH_CM_KEY_TYPE_SI_HASH,
                                        buf, buf_len))
                  nassigned += 1;
            }
        }
#endif
      /* More? */
      break;
    default:
      ssh_fatal("error: key class %u not supported.", classp);
      break;
    }
  return nassigned != 0;
}

Boolean
ssh_cm_key_set_from_crl(SshCertDBKey **key, SshCMCrl cm_crl)
{
  SshX509ExtIssuingDistPoint point;
  Boolean critical;
  size_t nassigned = 0;

  SSH_DEBUG(SSH_D_MIDSTART, ("Put CRL names to key list."));

  if (cm_crl->crl == NULL)
    return FALSE;

  /* Issuer */
  if (ssh_cm_key_convert_from_x509_name(key, cm_crl->crl->issuer_name))
    nassigned += 1;
  if (ssh_cm_key_convert_from_x509_name(key,
                                        cm_crl->crl->
                                        extensions.issuer_alt_names))
    nassigned += 1;

  /* Get the issuing distribution point. */
  if (ssh_x509_crl_get_issuing_dist_point(cm_crl->crl, &point, &critical))
    {
      SSH_DEBUG(SSH_D_FAIL, ("Issuing CRL distribution point available."));

      if (point->full_name)
        {
          if (ssh_cm_key_convert_from_x509_name(key, point->full_name))
            nassigned += 1;
        }
      /* Note. Ignores other fields of the distribution point for now.  */
    }

  return nassigned != 0;
}

Boolean ssh_cm_key_push_keys(SshCertDBKey **key, SshCertDBKey *list)
{
  Boolean rv = TRUE;

  for (; rv && list; list = list->next)
    {
      if (!ssh_certdb_key_push(key, list->type,
                               ssh_memdup(list->data, list->data_len),
                               list->data_len))
        rv = FALSE;
    }
  return rv;
}

/* Set conversion function and context for the key. Note this must be done for
   the key after all the search keys are added and before it is added to the
   search constraints. Note also that only last key added will be used when
   searching from the external databases */
void ssh_cm_key_set_conversion_function(SshCertDBKey *key,
                                        SshCMEdbConversionFunction func,
                                        void *context)
{
#if 0
  key->edb_conversion_function = func;
  key->edb_conversion_function_context = context;
#endif
}

/* Set external search index data for given search index. The data must be
   something similar than what is returned by the SshCMGenerateHashDataCB
   function. */
Boolean
ssh_cm_key_set_external(SshCertDBKey **key,
                        SshCMSearchIndexHandle index_handle,
                        const unsigned char *data,
                        size_t len)
{
  SSH_DEBUG_HEXDUMP(SSH_D_MIDSTART,
                    ("Put external data object for search index %d",
                     index_handle),
                    data, len);

  /* Push to the list. */
  return ssh_certdb_key_push(key, index_handle, ssh_memdup(data, len), len);
}

Boolean ssh_cm_key_match(SshCertDBKey *op1, SshCertDBKey *op2)
{
  SshCertDBKey *tmp1, *tmp2;
  size_t match, no_match;
  Boolean some_found;

  SSH_DEBUG(5, ("Match certificate keys."));

  match    = 0;
  no_match = 0;
  some_found = FALSE;

  for (tmp1 = op1; tmp1; tmp1 = tmp1->next)
    for (tmp2 = op2; tmp2; tmp2 = tmp2->next)
      {
        /* Check for same types. */
        if (tmp1->type == tmp2->type)
          {
            if (tmp1->data_len == tmp2->data_len &&
                memcmp(tmp1->data, tmp2->data, tmp1->data_len) == 0)
              {
                switch (tmp1->type)
                  {
                  case SSH_CM_KEY_TYPE_DISNAME:
                    return TRUE;
                  default:
                    match++;
                    break;
                  }
              }
            else
              {
                switch (tmp1->type)
                  {
                  case SSH_CM_KEY_TYPE_DISNAME:
                    return FALSE;
                  default:
                    no_match++;
                    break;
                  }
              }
            some_found = TRUE;
          }
      }

  /* No matches, nor misses. */
  if (some_found == FALSE || match == 0)
    {
      SSH_DEBUG(SSH_D_MIDOK, ("Matching of certificate keys failed."));
      return FALSE;
    }

  SSH_DEBUG(SSH_D_MIDOK, ("Matching succeeded."));
  return TRUE;
}
