/*
  cmi-cert.c

  Copyright:
        Copyright (c) 2002, 2003 SFNT Finland Oy.
	All rights reserved.

  Certificate related API routines for the validator.
*/

#include "sshincludes.h"
#include "cmi.h"
#include "cmi-internal.h"
#include "ssh_berfile.h"

#define SSH_DEBUG_MODULE "SshCertCMi"

/************ CM Certificate handling ************/

SshCMCertificate ssh_cm_cert_allocate(SshCMContext cm)
{
  SshCMCertificate cert;

  SSH_DEBUG(6, ("Allocate certificate."));

  if ((cert = ssh_calloc(1, sizeof(*cert))) != NULL)
    {
      if ((cert->cert = ssh_x509_cert_allocate(SSH_X509_PKIX_CERT)) == NULL)
	{
	  ssh_free(cert);
	  return NULL;
	}

      /* Initialize */
      cert->cm = cm;
      cert->status_flags = 0;

      /* Set the initialization flags always to zero, before any operation. */
      cert->initialization_flags = 0;

      /* Clean the structures. */
      cert->entry      = NULL;
      cert->ber        = NULL;
      cert->ber_length = 0;

      if (cert->cert == NULL)
        {
          ssh_free(cert);
          return NULL;
        }

      cert->private_data            = NULL;
      cert->private_data_destructor = NULL_FNPTR;

      /* This flag is always set on for new certificates. */
      cert->not_checked_against_crl = TRUE;

      /* Set the trustedness and CRL info. */
      ssh_cm_trust_init(cert);

      /* Set up the CRL information. */
      cert->crl_issuer   = TRUE;
      cert->crl_user     = TRUE;
      cert->self_signed  = 0;
      cert->self_issued  = 0;
      ssh_ber_time_zero(&cert->crl_recompute_after);
#ifdef SSHDIST_VALIDATOR_OCSP
      ssh_ber_time_zero(&cert->ocsp_valid_not_before);
      ssh_ber_time_zero(&cert->ocsp_valid_not_after);
#endif /* SSHDIST_VALIDATOR_OCSP */

      /* Set up the flag indicating whether this is a CA (in X.509v1). */
      cert->acting_ca    = FALSE;

      /* Revocation information. */
      cert->status = SSH_CM_VS_OK;
      cert->revocator_was_trusted = FALSE;
    }
  return cert;
}

void ssh_cm_cert_free(SshCMCertificate cert)
{
  SSH_DEBUG(6, ("Free certificate."));

  if (cert == NULL)
    /* Remove the entry? */
    return;

  /* Notify the event. */
  SSH_CM_NOTIFY_CERT(cert->cm, FREE, cert);

  if (cert->private_data != NULL)
    {
      SSH_DEBUG(5, ("Calling the private data destructor"
                    " in certificate free."));
      if (cert->private_data_destructor)
        (*cert->private_data_destructor)(cert, cert->private_data);
      cert->private_data_destructor = NULL_FNPTR;
      cert->private_data            = NULL;
    }

  if (cert->entry != NULL)
    {
      SSH_DEBUG(SSH_D_ERROR,
                ("Tried to free certificate at the database."));
      return;
    }

  ssh_cm_trust_clear(cert);

  /* Free the current certificate. */
  ssh_free(cert->ber);
  ssh_x509_cert_free(cert->cert);
  ssh_free(cert);
}

void ssh_cm_cert_remove(SshCMCertificate cert)
{
  SSH_DEBUG(5, ("Removing certificate from the cache."));

  if (cert == NULL)
    return;

  if (cert->entry == NULL)
    {
      ssh_cm_cert_free(cert);
      return;
    }

  /* Remove the certificate. */
  if (!ssh_cm_cert_is_locked(cert))
    ssh_certdb_take_reference(cert->entry);

  ssh_certdb_remove_entry(cert->cm->db, cert->entry);
}

void ssh_cm_cert_take_reference(SshCMCertificate cert)
{
  if (cert->entry == NULL)
    return;
  ssh_certdb_take_reference(cert->entry);
}

void ssh_cm_cert_remove_reference(SshCMCertificate cert)
{
  if (cert->entry == NULL)
    return;
  ssh_certdb_release_entry(cert->cm->db, cert->entry);
}

unsigned int ssh_cm_cert_get_cache_id(SshCMCertificate cert)
{
  unsigned int entry_id;

  if (cert->entry == NULL)
    {
      SSH_DEBUG(6, ("Search for the entry identifier."));

      /* The certificate is not itself available thru cache search,
         however, there may be the exactly same certificate. We try to
         return the cache identifier if such certificate exists. */
      ssh_cm_check_db_collision(cert->cm, SSH_CM_DATA_TYPE_CERTIFICATE,
                                cert->ber, cert->ber_length,
                                NULL, &entry_id);

    }
  else
    entry_id = cert->entry->id;

  SSH_DEBUG(SSH_D_LOWOK,
            ("Certificate serial %@ entry identifier %d.",
             ssh_cm_render_mp, &cert->cert->serial_number,
             entry_id));
  return entry_id;
}

SshCMStatus ssh_cm_cert_set_ber(SshCMCertificate c,
                                const unsigned char *ber,
                                size_t ber_length)
{
  SshBERFile bf;

  SSH_DEBUG(5, ("Set certificate in ber."));

  if (c->ber != NULL)
    return SSH_CM_STATUS_FAILURE;

  if (c->cm &&
      ber_length > c->cm->config->max_certificate_length)
    {
      SSH_DEBUG(SSH_D_FAIL,
                ("Certificate (%ld bytes) too long (max %ld bytes)",
                 ber_length, c->cm->config->max_certificate_length));
      return SSH_CM_STATUS_FAILURE;
    }

  if (ssh_ber_file_create(ber, ber_length, &bf) != SSH_BER_FILE_ERR_OK)
    return SSH_CM_STATUS_FAILURE;

  ber_length -= ssh_ber_file_get_free_space(bf);
  ssh_ber_file_destroy(bf);

  /* Start up the certificate. */
  if (ssh_x509_cert_decode(ber, ber_length, c->cert) != SSH_X509_OK)
    {
      SSH_DEBUG(3, ("Certificate decoding in X.509 library failed."));
      return SSH_CM_STATUS_DECODE_FAILED;
    }

  /* Copy the BER encoded part too. */
  c->ber_length = 0;
  if ((c->ber = ssh_memdup(ber, ber_length)) != NULL)
    c->ber_length = ber_length;

  if (cm_verify_issuer_name(c, c))
    c->self_issued = 1;

  return SSH_CM_STATUS_OK;
}

SshCMStatus ssh_cm_cert_force_trusted(SshCMCertificate c)
{
  SSH_DEBUG(5, ("Force the certificate trusted."));
  if (ssh_cm_trust_is_root(c, NULL) == TRUE)
    SSH_DEBUG(4, ("Certificate is already trusted root."));

  if (c->entry != NULL)
    SSH_DEBUG(2, ("Caution! Certificate status changed to trusted root."));
  /* Force as the trusted root, and just ordinary trusted too. */
  if (c->entry)
    {
      ssh_cm_cert_set_class(c, SSH_CM_CCLASS_TRUSTED);
      ssh_cm_trust_make_root(c, NULL);
      c->initialization_flags &= (~SSH_CM_CERT_IF_TRUSTED);
    }
  else
    {
      c->initialization_flags |= SSH_CM_CERT_IF_TRUSTED;
    }

  /* Also lock the certificate into cache. */
  ssh_cm_cert_set_locked(c);

  return SSH_CM_STATUS_OK;
}

SshCMStatus ssh_cm_cert_force_untrusted(SshCMCertificate c)
{
  SSH_DEBUG(5, ("Force the certificate to untrusted state."));
  /* Force as untrusted root certificate. In this case you must
     make it untrusted also. */

  ssh_cm_trust_make_user(c, NULL);

  if (c->entry)
    ssh_cm_cert_set_class(c, SSH_CM_CCLASS_DEFAULT);
  else
    c->initialization_flags &= (~SSH_CM_CERT_IF_TRUSTED);

  /* Unlock the certificate. */
  ssh_cm_cert_set_unlocked(c);

  return SSH_CM_STATUS_OK;
}

SshCMStatus ssh_cm_cert_non_crl_issuer(SshCMCertificate c)
{
  SSH_DEBUG(5, ("Assume the certificate not to issue CRLs."));
  if (c->entry != NULL)
    SSH_DEBUG(2, ("Caution! Certificate is no longer a CRL issuer."));
  c->crl_issuer = FALSE;
  ssh_ber_time_zero(&c->crl_recompute_after);
  return SSH_CM_STATUS_OK;
}

SshCMStatus ssh_cm_cert_make_crl_issuer(SshCMCertificate c)
{
  SSH_DEBUG(5, ("The certificate is now CRL issuer."));
  c->crl_issuer = TRUE;
  return SSH_CM_STATUS_OK;
}

SshCMStatus ssh_cm_cert_non_crl_user(SshCMCertificate c)
{
  SSH_DEBUG(5, ("Assume the certificate not to user CRLs."));
  if (c->entry != NULL)
    SSH_DEBUG(2, ("Caution! Certificate is no longer a CRL user."));
  c->crl_user = FALSE;
  return SSH_CM_STATUS_OK;
}

SshCMStatus ssh_cm_cert_make_crl_user(SshCMCertificate c)
{
  SSH_DEBUG(5, ("The certificate is now CRL user."));
  c->crl_user = TRUE;
  return SSH_CM_STATUS_OK;
}

void ssh_cm_cert_set_trusted_set(SshCMCertificate c,
                                 SshMPInteger trusted_set)
{
  SSH_ASSERT(trusted_set != NULL && c != NULL);

  /* Remark. This function in theory could be used by "malicious" programs
     to create problems. Of course, in practice such a program can do this
     by setting the certificate trusted root and thus avoid this check.
     Anyway, there is practically no reason whatsoever to set this field
     for non-root certificates.

     If you find such use please inform us at SSH. */

  if (!c->trusted.trusted_root &&
      !(c->initialization_flags & SSH_CM_CERT_IF_TRUSTED))
    {
      SSH_DEBUG(5, ("Attempt to force trusted set failed. "
                    "The certificate is not a trusted root."));
      return;
    }

  ssh_mprz_set(&c->trusted.trusted_set, trusted_set);
}

/* The return value MUST not be freed, such an operation would lead
   to mallocation error. */
SshMPInteger ssh_cm_cert_get_trusted_set(SshCMCertificate c)
{
  SSH_ASSERT(c != NULL);
  return &c->trusted.trusted_set;
}

void ssh_cm_cert_set_trusted_not_after(SshCMCertificate c,
                                       SshBerTime trusted_not_after)
{
  SSH_ASSERT(c != NULL);
  if (!c->trusted.trusted_root)
    return;
  ssh_ber_time_set(&c->trusted.trusted_not_after, trusted_not_after);
}

void
ssh_cm_cert_set_path_length(SshCMCertificate c, size_t path_length)
{
  SSH_DEBUG(5, ("Set the path length for the certificate."));
  c->trusted.path_length = path_length;
}

SshCMStatus ssh_cm_cert_set_private_data(SshCMCertificate c,
                                         void *private_context,
                                         SshCMPrivateDataDestructor destructor)
{
  if (c->private_data != NULL)
    {
      if (c->private_data_destructor)
        (*c->private_data_destructor)(c, c->private_data);
      c->private_data_destructor = NULL_FNPTR;
      c->private_data            = NULL;
    }
  c->private_data_destructor = destructor;
  c->private_data            = private_context;
  return SSH_CM_STATUS_OK;
}

SshCMStatus ssh_cm_cert_get_private_data(SshCMCertificate c,
                                         void **private_context)
{
  *private_context = c->private_data;
  if (c->private_data == NULL)
    return SSH_CM_STATUS_NOT_AVAILABLE;
  return SSH_CM_STATUS_OK;
}

SshCMStatus ssh_cm_cert_get_subject_keys(SshCMCertificate c,
                                         SshCertDBKey **keys)
{
  SSH_DEBUG(6, ("Get certificate subject keys."));

  if (ssh_cm_key_set_from_cert(keys, SSH_CM_KEY_CLASS_SUBJECT, c))
    return SSH_CM_STATUS_OK;
  else
    return SSH_CM_STATUS_FAILURE;
}

SshCMStatus ssh_cm_cert_get_issuer_keys(SshCMCertificate c,
                                        SshCertDBKey **keys)
{
  SSH_DEBUG(6, ("Get certificiate issuer keys."));

  if (ssh_cm_key_set_from_cert(keys, SSH_CM_KEY_CLASS_ISSUER, c))
    return SSH_CM_STATUS_OK;
  else
    return SSH_CM_STATUS_FAILURE;
}

/* Return opened certificate. Convenience function. */
SshCMStatus ssh_cm_cert_get_x509(SshCMCertificate c,
                                 SshX509Certificate *cert)
{
  SshX509Certificate x509_cert;

  SSH_DEBUG(SSH_D_MIDOK, ("Get certificate X.509 opened form."));

  *cert = NULL;
  if (c->ber != NULL)
    {
      if ((x509_cert = ssh_x509_cert_allocate(SSH_X509_PKIX_CERT)) != NULL)
        {
          if (ssh_x509_cert_decode(c->ber, c->ber_length, x509_cert)
              != SSH_X509_OK)
            {
              SSH_DEBUG(SSH_D_ERROR, ("CMI contains corrupted certificate."));
              ssh_x509_cert_free(x509_cert);
              return SSH_CM_STATUS_FAILURE;
            }
        }
      else
        {
          SSH_DEBUG(SSH_D_ERROR,
                    ("CMI can't allocate space for return certificate."));
          return SSH_CM_STATUS_FAILURE;
        }
    }
  else
    {
      SSH_DEBUG(SSH_D_ERROR, ("CMI does not contain certificate DER."));
      return SSH_CM_STATUS_FAILURE;
    }

  /* All done. */
  *cert = x509_cert;
  return SSH_CM_STATUS_OK;
}

SshCMStatus ssh_cm_cert_get_ber(SshCMCertificate c,
                                unsigned char **ber, size_t *ber_length)
{
  SSH_DEBUG(6, ("Get certificate ber/der encoding."));

  if (c == NULL)
    return SSH_CM_STATUS_FAILURE;
  if (c->ber == NULL)
    return SSH_CM_STATUS_FAILURE;

  *ber        = c->ber;
  *ber_length = c->ber_length;
  return SSH_CM_STATUS_OK;
}

SshCMStatus ssh_cm_cert_get_computed_validity(SshCMCertificate c,
                                              SshBerTime not_before,
                                              SshBerTime not_after)
{
  SSH_DEBUG(6, ("Get validity of the certificate."));
  /* Check the trustedness flag. */
  if (ssh_cm_trust_check(c, NULL, NULL) == FALSE)
    {
      SSH_DEBUG(3, ("The certificate is not trusted."));
      return SSH_CM_STATUS_FAILURE;
    }

  /* Check that the validity times exist. */
  if (ssh_ber_time_available(&c->trusted.valid_not_before) == FALSE ||
      ssh_ber_time_available(&c->trusted.valid_not_after)  == FALSE)
    {
      SSH_DEBUG(3, ("The certificate has no validity time available."));
      return SSH_CM_STATUS_FAILURE;
    }

  if (not_before)
    ssh_ber_time_set(not_before, &c->trusted.valid_not_before);
  if (not_after)
    ssh_ber_time_set(not_after,  &c->trusted.valid_not_after);

  return SSH_CM_STATUS_OK;
}

SshCMStatus ssh_cm_cert_get_computed_time(SshCMCertificate c,
                                          SshBerTime computed)
{
  SSH_DEBUG(6, ("Get the computed time in certificate."));

  if (computed)
    {
      /* Check the trustedness flag. */
      if (ssh_cm_trust_check(c, NULL, NULL) == FALSE)
        {
          SSH_DEBUG(4, ("The certificate is not trusted."));
          return SSH_CM_STATUS_FAILURE;
        }

      /* Check if the time information is available. */
      if (ssh_ber_time_available(&c->trusted.trusted_computed) == FALSE)
        {
          SSH_DEBUG(4, ("Trust computation time is not available."));
          return SSH_CM_STATUS_FAILURE;
        }

      ssh_ber_time_set(computed, &c->trusted.trusted_computed);
      return SSH_CM_STATUS_OK;
    }
  return SSH_CM_STATUS_FAILURE;
}

/* Following functions '*_is_*' functions return FALSE if the certificate
   is not trusted. This guards from certain problems, however, should this
   be handled by the application? */

Boolean ssh_cm_cert_is_trusted_root(SshCMCertificate c)
{
  if (c->initialization_flags & SSH_CM_CERT_IF_TRUSTED)
    return TRUE;
  return ssh_cm_trust_is_root(c, NULL);
}

Boolean ssh_cm_cert_is_crl_issuer(SshCMCertificate c)
{
  return c->crl_issuer;
}

Boolean ssh_cm_cert_is_crl_user(SshCMCertificate c)
{
  return c->crl_user;
}

Boolean ssh_cm_cert_is_revoked(SshCMCertificate c)
{
  if (ssh_cm_trust_check(c, NULL, NULL) == FALSE)
    {
      SSH_DEBUG(5, ("Claiming the input certificate to be revoked, "
                    "because it is not trusted at the moment."));
      return TRUE;
    }
  return (c->status == SSH_CM_VS_OK) ? TRUE : FALSE;
}

/* Functions which need the availability of CM context. This is a burden
   you need when looking down to the cache level. */

SshCMStatus ssh_cm_cert_set_locked(SshCMCertificate c)
{
  SSH_DEBUG(5, ("The certificate will become permament in the cache."));

  if (c == NULL)
    return SSH_CM_STATUS_FAILURE;

  if (c->entry == NULL)
    {
      c->initialization_flags |= SSH_CM_CERT_IF_LOCKED;
      return SSH_CM_STATUS_OK;
    }

  if (c->cm == NULL || c->cm->db == NULL)
    {
      SSH_DEBUG(SSH_D_ERROR, ("Certificate manager not initialized."));
      return SSH_CM_STATUS_FAILURE;
    }

  /* Clear initialization flags, just in case. */
  c->initialization_flags &= (~SSH_CM_CERT_IF_LOCKED);

  /* Set the certificate as permanent. */
  {
    unsigned int limit = ~((unsigned int)0);
    ssh_certdb_set_option(c->cm->db, c->entry,
                          SSH_CERTDB_OPTION_MEMORY_LOCK, &limit);
  }

  /* Set the class of the certificate. */
  ssh_cm_cert_set_class(c, SSH_CM_CCLASS_LOCKED);

  return SSH_CM_STATUS_OK;
}

SshCMStatus ssh_cm_cert_set_unlocked(SshCMCertificate c)
{
  SSH_DEBUG(5, ("The certificate will be unlocked from the cache."));

  if (c == NULL)
    return SSH_CM_STATUS_FAILURE;

  if (c->entry == NULL)
    {
      c->initialization_flags &= (~SSH_CM_CERT_IF_LOCKED);
      return SSH_CM_STATUS_OK;
    }

  if (c->cm == NULL || c->cm->db == NULL)
    {
      SSH_DEBUG(SSH_D_ERROR, ("Certificate manager not initialized."));
      return SSH_CM_STATUS_FAILURE;
    }

  /* Set the certificate as permanent. */
  {
    unsigned int limit = 0;
    ssh_certdb_set_option(c->cm->db, c->entry,
                          SSH_CERTDB_OPTION_MEMORY_LOCK, &limit);
  }

  /* Move back to the default class. */
  ssh_cm_cert_set_class(c, SSH_CM_CCLASS_DEFAULT);

  return SSH_CM_STATUS_OK;
}

Boolean ssh_cm_cert_is_locked(SshCMCertificate c)
{
  if (c->initialization_flags &= SSH_CM_CERT_IF_LOCKED)
    return TRUE;

  if (c->entry == NULL)
    return FALSE;

  {
    unsigned int limit;

    if (ssh_certdb_get_option(c->cm->db, c->entry,
                              SSH_CERTDB_OPTION_MEMORY_LOCK, &limit)
        != SSH_CDBET_OK)
      return FALSE;

    if (limit == 0)
      return FALSE;
  }
  return TRUE;
}

/* Derive the CM Context. */

SshCMContext ssh_cm_cert_derive_cm_context(SshCMCertificate c)
{
  return c->cm;
}


/* Handle the class functions. */

/* Change the class of a certificate. */

#define SSH_CM_REAL_CLASS(app_class) \
  (((app_class) == SSH_CM_CCLASS_INVALID) \
   ? ((int)-1) \
   : ((int)((unsigned int)(app_class) + 3)))

#define SSH_CM_APP_CLASS(real_class) \
  (((real_class) == -1) \
   ? SSH_CM_CCLASS_INVALID \
   : ((unsigned int)((real_class) - 3)))

SshCMStatus ssh_cm_cert_set_class(SshCMCertificate c,
                                  unsigned int app_class)
{
  int real_class = SSH_CM_REAL_CLASS(app_class);

  /* Check the class number. */
  if (real_class > SSH_CM_REAL_CLASS(SSH_CM_CCLASS_MAX))
    return SSH_CM_STATUS_CLASS_TOO_LARGE;

  /* Change the class of the certificate. */
  if (ssh_cm_trust_is_root(c, NULL))
    return SSH_CM_STATUS_CLASS_UNCHANGED;

  /* Set the real class. */
  ssh_certdb_set_entry_class(c->cm->db, c->entry,
                             real_class);
  return SSH_CM_STATUS_OK;
}

unsigned int ssh_cm_cert_get_class(SshCMCertificate c)
{
  return SSH_CM_APP_CLASS(ssh_certdb_get_entry_class(c->cm->db, c->entry));
}

unsigned int ssh_cm_cert_get_next_class(SshCMContext cm,
                                        unsigned int app_class)
{
  unsigned int real_class = SSH_CM_REAL_CLASS(app_class);
  /* Check the class number. */
  if (real_class > SSH_CM_REAL_CLASS(SSH_CM_CCLASS_MAX))
    return SSH_CM_STATUS_CLASS_TOO_LARGE;
  return
    SSH_CM_APP_CLASS(ssh_certdb_get_next_entry_class(cm->db, real_class));
}

SshCMStatus
ssh_cm_cert_enumerate_class(SshCMContext cm,
                            unsigned int app_class,
                            SshCMCertEnumerateCB callback, void *context)
{
  SshCertDBEntry *entry;
  unsigned int real_class = SSH_CM_REAL_CLASS(app_class);

  /* Check the class number. */
  if (real_class > SSH_CM_REAL_CLASS(SSH_CM_CCLASS_MAX))
    return SSH_CM_STATUS_CLASS_TOO_LARGE;

  SSH_DEBUG(6, ("Enumerate certificate class."));

  /* Check the callback. */
  if (callback == NULL_FNPTR)
    return SSH_CM_STATUS_FAILURE;

  /* Initialize the loop. */
  entry = NULL;
  do
    {
      entry = ssh_certdb_iterate_entry_class(cm->db, real_class, entry);
      /* Now study the entry closer. */
      if (entry != NULL && entry->tag == SSH_CM_DATA_TYPE_CERTIFICATE)
        {
          SshCMCertificate cm_cert;

          /* Get the certificate. */
          cm_cert = entry->context;

          /* Call the callback. */
          (*callback)(cm_cert, context);
        }
    }
  while (entry != NULL);

  return SSH_CM_STATUS_OK;
}


/* Check whether the certificate has a been previously added to the
   database. */
Boolean ssh_cm_cert_check_db_collision(SshCMContext cm,
                                       SshCMCertificate cm_cert,
                                       SshCertDBKey **key)
{
  return ssh_cm_check_db_collision(cm, SSH_CM_DATA_TYPE_CERTIFICATE,
                                   cm_cert->ber, cm_cert->ber_length,
                                   key, NULL);
}

SshCMStatus ssh_cm_add_with_bindings(SshCMCertificate cert,
                                     SshCertDBKey *bindings)
{
  SshCertDBEntry *entry;
  SshCMContext cm = cert->cm;
  int i;

  SSH_DEBUG(5, ("Certificate add to local database/memory cache."));

  if (cert == NULL || cm->db == NULL)
    {
      ssh_certdb_key_free(bindings);
      return SSH_CM_STATUS_FAILURE;
    }

  if (cm->config->local_db_writable == FALSE)
    {
      ssh_certdb_key_free(bindings);
      return SSH_CM_STATUS_LOCAL_DB_NWRITE;
    }

  /* Allocate a new entry. */
  if (ssh_certdb_alloc_entry(cm->db,
                             SSH_CM_DATA_TYPE_CERTIFICATE,
                             cert,
                             &entry) != SSH_CDBET_OK)
    {
      ssh_certdb_key_free(bindings);
      return SSH_CM_STATUS_COULD_NOT_ALLOCATE;
    }

  SSH_DEBUG(SSH_D_MIDOK,
            ("Explicit certificate: %@",
             ssh_cm_render_certificate, cert->cert));

  /* Check for collision in the database. Be a optimist anyway... */
  if (ssh_cm_cert_check_db_collision(cm, cert, &entry->names))
    {
      /* Remove reference from the entry. */
      entry->context = NULL;
      /* Free the entry allocated. */
      ssh_certdb_release_entry(cm->db, entry);
      ssh_certdb_key_free(bindings);

      SSH_DEBUG(4, ("Certificate exists already in the database."));

      return SSH_CM_STATUS_ALREADY_EXISTS;
    }

  /* Initialize the entry. */
  cert->entry = entry;

  if (!ssh_cm_key_set_from_cert(&entry->names, SSH_CM_KEY_CLASS_SUBJECT, cert))
    {
      ssh_certdb_release_entry(cm->db, entry);
      ssh_certdb_key_free(bindings);
      cert->entry = NULL;
      return SSH_CM_STATUS_COULD_NOT_ALLOCATE;
    }

  if (bindings)
    ssh_certdb_entry_add_keys(cm->db, entry, bindings);

  for (i = 0; i < cm->config->num_external_indexes; i++)
    {
      unsigned char *p;
      size_t len;

      p = NULL;
      (*(cm->config->external_index_cb[i]))
        (cert, &p, &len, cm->config->external_index_ctx[i]);
      if (p != NULL)
        ssh_certdb_key_push(&entry->names, i + SSH_CM_KEY_TYPE_NUM, p, len);
    }

  /* Add to the database. */
  if (ssh_certdb_add(cm->db, entry) != SSH_CDBET_OK)
    {
      ssh_certdb_release_entry(cm->db, entry);

      SSH_DEBUG(4, ("Local database/memory cache denies the addition."));

      return SSH_CM_STATUS_DB_ERROR;
    }

  /* Handle now the initialization flags of the certificate. */
  if (cert->initialization_flags & SSH_CM_CERT_IF_LOCKED)
    ssh_cm_cert_set_locked(cert);
  if (cert->initialization_flags & SSH_CM_CERT_IF_TRUSTED)
    ssh_cm_cert_force_trusted(cert);

  /* Now call the event so that the new certificate is introduced to the
     caller. */
  SSH_CM_NOTIFY_CERT(cm, NEW, cert);

  /* Release the entry. */
  ssh_certdb_release_entry(cm->db, entry);

  return SSH_CM_STATUS_OK;
}

SshCMStatus ssh_cm_add(SshCMCertificate cm_cert)
{
  return ssh_cm_add_with_bindings(cm_cert, NULL);
}
