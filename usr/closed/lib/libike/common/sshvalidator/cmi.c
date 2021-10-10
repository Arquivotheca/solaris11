/*
  cmi.c

  Copyright:
        Copyright (c) 2002, 2003 SFNT Finland Oy.
 	All rights reserved.

  Implementation of the SSH Certificate Validator (former Certificate
  Manager, thus still called SSH CMi).
*/

#include "sshincludes.h"
#include "sshcrypt.h"
#include "cmi.h"
#include "cmi-internal.h"
#include "sshadt.h"
#include "sshadt_map.h"

#define SSH_DEBUG_MODULE "SshCertCMi"

SshCMStatus ssh_cm_find_internal(SshCMSearchContext *search);

SshCMStatus ssh_cm_find_internal(SshCMSearchContext *search);
Boolean ssh_cm_check_db_collision(SshCMContext cm,
                                  unsigned int tag,
                                  const unsigned char *ber,
                                  size_t ber_length,
                                  SshCertDBKey **key,
                                  unsigned int *entry_id);

/* Subject has failed in conjunction with the issuer. */
static void
cm_failure_list_add(SshCMSearchContext *search,
                    unsigned int issuer_id, unsigned int subject_id)
{
  SshCMSearchSignatureFailure tmp;

  if ((tmp = ssh_realloc(search->failure_list,
                         search->failure_list_size * sizeof(*tmp),
                         (1 + search->failure_list_size) * sizeof(*tmp)))
      != NULL)
    {
      tmp[search->failure_list_size].issuer_id = issuer_id;
      tmp[search->failure_list_size].subject_id = subject_id;
      search->failure_list = tmp;
      search->failure_list_size += 1;
    }
}

static Boolean
cm_failure_list_member(SshCMSearchContext *search,
                       unsigned int issuer_id, unsigned int subject_id)
{
  int i;
  SshCMSearchSignatureFailure fentry;

  for (i = 0; i < search->failure_list_size; i++)
    {
      fentry = &search->failure_list[i];
      if (fentry->issuer_id == issuer_id &&
          fentry->subject_id == subject_id)
        {
          return TRUE;
        }
    }
  return FALSE;
}


/* Append relative to DN kind of issuer */
static SshX509Name
cm_dp_make_full_name(SshX509Name issuer,
                     SshDN relative)
{
  SshX509Name full = NULL;
  unsigned char *i_der;
  size_t i_der_len;

  if (issuer && relative)
    {
      ssh_x509_name_reset(issuer);
      if (ssh_x509_name_pop_der_dn(issuer, &i_der, &i_der_len))
        {
          SshDNStruct fulldn;

          ssh_dn_init(&fulldn);
          ssh_dn_decode_der(i_der, i_der_len, &fulldn, NULL);
          ssh_free(i_der);

          ssh_dn_put_rdn(&fulldn, ssh_rdn_copy(*relative->rdn));
          ssh_dn_encode_der(&fulldn, &i_der, &i_der_len, NULL);
          ssh_dn_clear(&fulldn);

          ssh_x509_name_push_directory_name_der(&full, i_der, i_der_len);
          ssh_free(i_der);
        }
    }
  return full;
}

/* Canonize DER */
static unsigned char *
cm_canon_der(unsigned char *der, size_t der_len, size_t *canon_der_len)
{
  unsigned char *canon_der;
  SshDNStruct dn;

  *canon_der_len = 0;

  ssh_dn_init(&dn);
  if (ssh_dn_decode_der(der, der_len, &dn, NULL) == 0)
    {
      ssh_dn_clear(&dn);
      return NULL;
    }

  if (ssh_dn_encode_der_canonical(&dn, &canon_der, canon_der_len, NULL) == 0)
    {
      ssh_dn_clear(&dn);
      return NULL;
    }

  ssh_dn_clear(&dn);
  return canon_der;
}

/* True iff canonical der's of n1 and n2 match */
Boolean
cm_name_equal(SshX509Name n1, SshX509Name n2)
{
  Boolean rv;
  unsigned char *d1, *d2, *c_d1, *c_d2;
  size_t d1_len, d2_len, c_d1_len, c_d2_len;

  ssh_x509_name_reset(n1);
  if (!ssh_x509_name_pop_der_dn(n1, &d1, &d1_len))
    return FALSE;
  ssh_x509_name_reset(n2);
  if (!ssh_x509_name_pop_der_dn(n2, &d2, &d2_len))
    {
      ssh_free(d1);
      return FALSE;
    }

  c_d1 = cm_canon_der(d1, d1_len, &c_d1_len);
  ssh_free(d1);
  c_d2 = cm_canon_der(d2, d2_len, &c_d2_len);
  ssh_free(d2);

  if (c_d1_len != c_d1_len)
    rv = FALSE;
  else
    rv = !memcmp(c_d1, c_d2, c_d1_len);

  ssh_free(c_d1);
  ssh_free(c_d2);
  return rv;
}

/* Return true, if issuer is really issuer of the subject, e.g. the
   issuer canonical name matches subject's issuer canonical name. We
   need to check these, as we might have encountered wrong cert via
   issuer/subject key identifier mapping. */
Boolean
cm_verify_issuer_name(SshCMCertificate subject, SshCMCertificate issuer)
{
  return cm_name_equal(subject->cert->issuer_name,
                       issuer->cert->subject_name);
}

Boolean
cm_verify_issuer_id(SshCMCertificate subject, SshCMCertificate issuer)
{
  Boolean rv;
  SshX509ExtKeyId s_ikid;
  Boolean critical;

  if (!ssh_x509_cert_get_authority_key_id(subject->cert,
                                          &s_ikid, &critical))
    {
      /* Subject does not specify issuer kid, we assume it's OK, as
         issuer was originally found by name */
      return TRUE;
    }

  if (s_ikid->key_id_len)
    {
      unsigned char *i_skid;
      size_t i_skid_len;

      if (ssh_x509_cert_get_subject_key_id(issuer->cert,
                                           &i_skid, &i_skid_len,
                                           &critical))
        {
          if (i_skid_len == s_ikid->key_id_len &&
              memcmp(i_skid, s_ikid->key_id, i_skid_len) == 0)
            {
              /* Key ID matches */
              return TRUE;
            }
          else
            {
              /* subject's issuer key id does not match issuer
                 candidates subject key id. */
              return FALSE;
            }
        }
      /* subject had issuer binary issuer kid, but the issuer did not
         have subject key id, assume its OK. */
      return TRUE;
    }

  if (s_ikid->auth_cert_issuer)
    {
      SshMPIntegerStruct serial;

      ssh_mprz_init(&serial);
      ssh_x509_cert_get_serial_number(issuer->cert, &serial);
      if (ssh_mprz_cmp(&s_ikid->auth_cert_serial_number, &serial) != 0)
        {
          ssh_mprz_clear(&serial);
          return FALSE;
        }
      ssh_mprz_clear(&serial);

      rv = cm_name_equal(s_ikid->auth_cert_issuer,
                         issuer->cert->subject_name);
      ssh_x509_name_reset(s_ikid->auth_cert_issuer);
      ssh_x509_name_reset(issuer->cert->subject_name);

      return rv;
    }

  return TRUE;
}

/* Search terminated, call the callback */
static void
cm_search_callback(SshCMSearchContext *search,
                   int status,
                   SshCertDBEntryList *result)
{
  struct SshCMSearchInfoRec info;

  info.status = status;
  info.state = search->state;

  search->cm->in_callback++;
  (*search->callback)(search->search_context, &info, result);
  search->cm->in_callback--;
}

/* A routine that is called by the certificate cache when data needs
   to be freed. */
void ssh_cm_data_free(unsigned int tag, void *context)
{
  SSH_DEBUG(5, ("Data free called by database."));

  /* Ignore NULL contexts. */
  if (context == NULL)
    {
      SSH_DEBUG(3, ("*** For some reason data was NULL."));
      return;
    }

  switch (tag)
    {
    case SSH_CM_DATA_TYPE_CERTIFICATE:
      {
        SshCMCertificate cm_cert = context;

        cm_cert->entry = NULL;
        ssh_cm_cert_free(cm_cert);
        break;
      }
    case SSH_CM_DATA_TYPE_CRL:
      {
        SshCMCrl cm_crl = context;

        cm_crl->entry = NULL;
        ssh_cm_crl_free(cm_crl);
        break;
      }
    default:
      /* Failure not supported. */
      return;
    }
}

/************ CM Search constraints  *************/

SshCMSearchConstraints ssh_cm_search_allocate(void)
{
  SshCMSearchConstraints constraints = ssh_calloc(1, sizeof(*constraints));

  SSH_DEBUG(SSH_D_MIDSTART, ("Allocate search constraints."));

  if (constraints)
    {
      constraints->keys        = NULL;
      constraints->issuer_keys = NULL;
      ssh_ber_time_zero(&constraints->not_before);
      ssh_ber_time_zero(&constraints->not_after);
      constraints->max_path_length = (size_t)-1;
      constraints->key_usage_flags = 0;
      constraints->pk_algorithm = SSH_X509_PKALG_UNKNOWN;
      constraints->rule         = SSH_CM_SEARCH_RULE_AND;
      constraints->group        = FALSE;
      constraints->upto_root    = FALSE;

      constraints->local.crl    = FALSE;
      constraints->local.cert   = FALSE;

#ifdef SSHDIST_VALIDATOR_OCSP
      constraints->ocsp_mode = SSH_CM_OCSP_CRL_AFTER_OCSP;
#endif /* SSHDIST_VALIDATOR_OCSP */

      ssh_mprz_init(&constraints->trusted_roots.trusted_set);
      ssh_ber_time_zero(&constraints->trusted_roots.trusted_not_after);

      constraints->check_revocation = TRUE;

      constraints->inhibit_policy_mapping = 0;
      constraints->inhibit_any_policy = 0;
      constraints->policy_mapping = 0;

      constraints->user_initial_policy_set = NULL;
      constraints->user_initial_policy_set_size = 0;
    }
  return constraints;
}

void ssh_cm_search_free(SshCMSearchConstraints constraints)
{
  int i;

  SSH_DEBUG(SSH_D_MIDSTART, ("Free search constraints."));

  /* Clean the search constraints. */
  ssh_certdb_key_free(constraints->keys);
  ssh_mprz_clear(&constraints->trusted_roots.trusted_set);

  for (i = 0; i < constraints->user_initial_policy_set_size; i++)
    ssh_free(constraints->user_initial_policy_set[i]);
  ssh_free(constraints->user_initial_policy_set);
  ssh_free(constraints);
}

void ssh_cm_search_set_policy(SshCMSearchConstraints constraints,
                              SshUInt32 explicit_policy,
                              SshUInt32 inhibit_policy_mappings,
                              SshUInt32 inhibit_any_policy)
{
  constraints->explicit_policy = explicit_policy;
  constraints->inhibit_any_policy = inhibit_any_policy;
  constraints->inhibit_policy_mapping = inhibit_policy_mappings;
}

void
ssh_cm_search_add_user_initial_policy(SshCMSearchConstraints constraints,
                                      char *policy_oid)
{
  char **tmp;

  tmp =
    ssh_realloc(constraints->user_initial_policy_set,
                constraints->user_initial_policy_set_size * sizeof(char *),
                (1+constraints->user_initial_policy_set_size) *
                sizeof(char *));
  if (tmp)
    {
      tmp[constraints->user_initial_policy_set_size] = ssh_strdup(policy_oid);
      constraints->user_initial_policy_set_size += 1;
      constraints->user_initial_policy_set = tmp;
    }
}

/* The result has to be valid thru this time period. 'not_before' is
   the time when query is assumed to be made, and it can not be in the
   future. It this is not called, current time is assumed. */
void ssh_cm_search_set_time(SshCMSearchConstraints constraints,
                            SshBerTime not_before,
                            SshBerTime not_after)
{
  SSH_DEBUG(SSH_D_MIDSTART, ("Set search time from %@ to %@",
                             ssh_ber_time_render, not_before,
                             ssh_ber_time_render, not_after));

  if (not_before) ssh_ber_time_set(&constraints->not_before, not_before);
  if (not_after)  ssh_ber_time_set(&constraints->not_after, not_after);
}

void ssh_cm_search_set_keys(SshCMSearchConstraints constraints,
                            SshCertDBKey *keys)
{
  SSH_DEBUG(SSH_D_MIDSTART, ("Set search keys."));
  constraints->keys = keys;
}

void ssh_cm_search_set_key_type(SshCMSearchConstraints constraints,
                                SshX509PkAlgorithm algorithm)
{
  SSH_DEBUG(SSH_D_MIDSTART, ("Set search key type (%u).", algorithm));
  constraints->pk_algorithm = algorithm;
}

void ssh_cm_search_set_key_usage(SshCMSearchConstraints constraints,
                                 SshX509UsageFlags flags)
{
  SSH_DEBUG(SSH_D_MIDSTART, ("Set search key usage (%u).", flags));
  constraints->key_usage_flags = flags;
}

void ssh_cm_search_set_path_length(SshCMSearchConstraints constraints,
                                   size_t path_length)
{
  SSH_DEBUG(SSH_D_MIDSTART, ("Set search path length (%u).", path_length));
  constraints->max_path_length = path_length;
}

void ssh_cm_search_force_local(SshCMSearchConstraints constraints,
                               Boolean cert, Boolean crl)
{
  SSH_DEBUG(SSH_D_MIDSTART, ("Forcing use of cached cert = %s, crl = %s.",
                             (cert == TRUE ? "true" : "false"),
                             (crl  == TRUE ? "true" : "false")));

  constraints->local.cert = cert;
  constraints->local.crl  = crl;
}

void ssh_cm_search_set_trusted_set(SshCMSearchConstraints constraints,
                                   SshMPInteger trusted_set)
{
  SSH_ASSERT(constraints != NULL && trusted_set != NULL);

  SSH_DEBUG(SSH_D_MIDSTART, ("Set search trust set %@",
                             ssh_cm_render_mp, trusted_set));
  ssh_mprz_set(&constraints->trusted_roots.trusted_set, trusted_set);
}

void ssh_cm_search_set_trusted_not_after(SshCMSearchConstraints constraints,
                                         SshBerTime trusted_not_after)
{
  SSH_ASSERT(constraints != NULL && trusted_not_after != NULL);

  SSH_DEBUG(SSH_D_MIDSTART, ("Set search trust not after %@",
                             ssh_ber_time_render, trusted_not_after));
  ssh_ber_time_set(&constraints->trusted_roots.trusted_not_after,
                   trusted_not_after);
}

void ssh_cm_search_set_rule(SshCMSearchConstraints constraints,
                            SshCMSearchRule rule)
{
  SSH_DEBUG(SSH_D_MIDSTART, ("Set search rule (%u).", rule));
  constraints->rule = rule;
}

void ssh_cm_search_set_group_mode(SshCMSearchConstraints constraints)
{
  SSH_DEBUG(SSH_D_MIDSTART, ("Set search to use group mode."));
  constraints->group = TRUE;
}

void ssh_cm_search_set_until_root(SshCMSearchConstraints constraints)
{
  SSH_DEBUG(SSH_D_MIDSTART,
            ("Set search to look until trusted root has been found."));
  constraints->upto_root = TRUE;
}

void
ssh_cm_search_check_revocation(SshCMSearchConstraints constraints,
                               Boolean onoff)
{
  SSH_DEBUG(SSH_D_MIDSTART, ("Set search to ignore revocation checks."));
  constraints->check_revocation = onoff;
}

#ifdef SSHDIST_VALIDATOR_OCSP
void ssh_cm_search_set_ocsp_vs_crl(SshCMSearchConstraints constraints,
                                   SshCMOcspMode mode)
{

#ifdef DEBUG_LIGHT
  char *mode_str[] = {
    "No OCSP",
    "OCSP only",
    "Use CRLs if OCSP fails",
    "No OCSP check for end entity" };
  SSH_DEBUG(SSH_D_MIDSTART, ("Setting OCSP mode to '%s'.", mode_str[mode]));
#endif /* DEBUG_LIGHT */

  constraints->ocsp_mode = mode;
}
#endif /* SSHDIST_VALIDATOR_OCSP */




/************ Main Certificate Manager Routines **/

/* Handle the searching list. */
SshCMStatus ssh_cm_add_search(SshCMContext cm,
                              SshCMSearchContext *search)
{
  SSH_DEBUG(5, ("New search to be added to the queue."));

  if (cm->searching)
    {
      if (cm->current == NULL)
        ssh_fatal("ssh_cm_add_search: "
                  "searching but no current context available!");

      cm->last->next = search;
      search->next = NULL;
      cm->last       = search;
    }
  else
    {
      if (cm->current != NULL)
        ssh_fatal("ssh_cm_add_search: "
                  "not searching but still current available!");
      cm->current = search;
      cm->last    = search;
    }

  /* We are thus now searching, and the current search can continue. */
  cm->searching = TRUE;
  return SSH_CM_STATUS_OK;
}

SshCMSearchContext *ssh_cm_remove_search(SshCMContext cm,
                                         SshCMSearchContext *op,
                                         SshCMSearchContext *prev)
{
  SshCMSearchContext *tmp;

  SSH_DEBUG(5, ("Old search to be removed from the queue."));

  if (cm->searching)
    {
      if (op == NULL)
        ssh_fatal("ssh_cm_remove_search: "
                  "searching but no current context available!");
      tmp = op->next;
      /* Handle the removal. */
      if (prev)
        prev->next = tmp;
      else
        cm->current = tmp;
      if (tmp == NULL)
        cm->last = prev;

      if (cm->current == NULL)
        {
          cm->last = NULL;
          /* No longer searching. */
          cm->searching = FALSE;
        }
      op->next = NULL;
    }
  else
    ssh_fatal("ssh_cm_remove_search: "
              "remove attempt, but not searching.");
  return op;
}

Boolean ssh_cm_searching(SshCMContext cm)
{
  return cm->searching;
}

SshCMContext ssh_cm_allocate(SshCMConfig config)
{
  unsigned int num_key_types;
  SshCMContext cm = ssh_calloc(1, sizeof(*cm));

  SSH_DEBUG(SSH_D_HIGHOK, ("Allocate certificate manager."));

  if (cm == NULL)
    {
      /* Always free config data. */
      ssh_cm_config_free(config);
      return NULL;
    }

  /* Initialize. */
  cm->config = config;
  cm->db     = NULL;

  /* Current status. */
  cm->operation_depth = 0;
  cm->session_id      = 1;
  cm->searching       = FALSE;
  cm->in_callback     = 0;
  cm->current = cm->last = NULL;

  ssh_ber_time_zero(&cm->ca_last_revoked_time);

  /* Initialize the local cache. */

  num_key_types = cm->config->num_external_indexes + SSH_CM_KEY_TYPE_NUM;

  if (cm->config->local_db_allowed)
    {
      if (ssh_certdb_init(NULL_FNPTR, NULL_FNPTR,
                          ssh_cm_data_free,
                          cm->config->max_cache_entries,
                          cm->config->max_cache_bytes,
                          cm->config->default_time_lock,
                          &cm->db) != SSH_CDBET_OK)
        {
          SSH_DEBUG(3, ("Memory cache initialization failed."));
          goto failed;
        }
    }

  /* Allocate the certificate databases. */

  /* Initialize the negative cache. */
  if ((cm->negacache =
       ssh_edb_nega_cache_allocate(cm->config->nega_cache_size,
                                   num_key_types,
                                   cm->config->nega_cache_invalid_secs))
      == NULL)
    goto failed;

  /* Initialize the operation table. */
  if ((cm->op_map = ssh_cm_map_allocate()) == NULL)
    goto failed;

  /* Set up the external database system. */
  if (!ssh_cm_edb_init(&cm->edb))
    goto failed;

#ifdef SSHDIST_VALIDATOR_LDAP
  if (!ssh_cm_edb_ldap_init(cm, ""))
    goto failed;
#endif /* SSHDIST_VALIDATOR_LDAP */

  cm->control_timeout_active = FALSE;
  cm->map_timeout_active = FALSE;
  return cm;

 failed:
  if (cm->op_map) ssh_cm_map_free(cm->op_map);
  ssh_cm_config_free(config);
  ssh_free(cm);
  return NULL;
}

void ssh_cm_map_timeout_control(void *context)
{
  SshCMContext cm = (SshCMContext) context;

  SSH_ASSERT(cm->map_timeout_active == TRUE);
  cm->map_timeout_active = FALSE;
  ssh_cm_operation_control(cm);
}

void ssh_cm_timeout_control(void *context)
{
  SshCMContext cm = (SshCMContext) context;

  SSH_ASSERT(cm->control_timeout_active == TRUE);
  cm->control_timeout_active = FALSE;
  ssh_cm_operation_control(cm);
}

void cm_stopped(SshCMContext cm)
{
  if (cm->stopped_callback)
    {
      (*cm->stopped_callback)(cm->stopped_callback_context);
    }

  if (cm->control_timeout_active)
    {
      ssh_cancel_timeout(&cm->control_timeout);
      cm->control_timeout_active = FALSE;
    }

  if (cm->map_timeout_active)
    {
      ssh_cancel_timeout(&cm->map_timeout);
      cm->map_timeout_active = FALSE;
    }

  cm->stopping = FALSE;
  cm->stopped_callback = NULL_FNPTR;
}

static void cm_stop(void *context)
{
  SshCMContext cm = context;
  SshCMSearchContext *tmp;

  /* Terminate current searches. */
  for (tmp = cm->current; tmp; tmp = tmp->next)
    {
      SSH_DEBUG(SSH_D_LOWOK, ("Terminating search %p", tmp));
      if (!tmp->terminated)
        {
          cm_search_callback(tmp, SSH_CM_STATUS_NOT_FOUND, NULL);
          tmp->terminated = TRUE;
          ssh_cm_edb_operation_remove(cm, tmp);
        }
    }
  ssh_cm_edb_stop(&cm->edb);
  ssh_cm_operation_control(cm);
}


void ssh_cm_stop(SshCMContext cm,
                 SshCMDestroyedCB callback, void *callback_context)
{
  /* Disable new searches. */
  cm->stopping = TRUE;
  cm->stopped_callback = callback;
  cm->stopped_callback_context = callback_context;
  ssh_register_timeout(NULL, 0L, 0L, cm_stop, cm);
}

/* Must not be called before eventloop is uninitialized. */
void ssh_cm_free(SshCMContext cm)
{
  SSH_DEBUG(SSH_D_HIGHOK, ("Free certificate manager."));

  /* Cancel timeouts, in case being part of old application not
     calling ssh_cm_stop */
  cm_stopped(cm);

  ssh_cm_map_free(cm->op_map);

  ssh_certdb_free(cm->db);
  ssh_cm_edb_free(&cm->edb);
  ssh_edb_nega_cache_free(cm->negacache);

  ssh_cm_config_free(cm->config);

  ssh_free(cm);
}

/* Check whether the certificate has a been previously added to the
   database. */
Boolean
ssh_cm_check_db_collision(SshCMContext cm,
                          unsigned int tag,
                          const unsigned char *ber, size_t ber_length,
                          SshCertDBKey **key,
                          unsigned int *entry_id)
{
  unsigned char digest[SSH_MAX_HASH_DIGEST_LENGTH];
  unsigned char *key_digest;
  size_t length;
  SshHash hash;
  SshCertDBEntryList *found;
  SshCertDBEntryListNode list;

  SSH_DEBUG(SSH_D_MIDOK,
            ("Collision check for (%s).",
             (tag == SSH_CM_DATA_TYPE_CERTIFICATE ? "certificate" : "crl")));

  /* Set up the returned entry identifier for an error. */
  if (entry_id)
    *entry_id = 0;

  /* Error. */
  if (ber == NULL)
    {
      /* The certificate is not correct. */
      SSH_DEBUG(SSH_D_ERROR, ("DER of input to collision check is NULL."));
      return TRUE;
    }

  /* Allocate hash algorithm. */
  if (ssh_hash_allocate(SSH_CM_HASH_ALGORITHM, &hash) != SSH_CRYPTO_OK)
    {
      SSH_DEBUG(SSH_D_ERROR, ("Can't allocate %s", SSH_CM_HASH_ALGORITHM));
      return TRUE;
    }

  ssh_hash_update(hash, ber, ber_length);
  ssh_hash_final(hash, digest);
  length = ssh_hash_digest_length(ssh_hash_name(hash));
  ssh_hash_free(hash);

  /* Use only 8 bytes maximum for this information, the hash function
     used is a very good one, thus even 2^64 different values should
     be enough for reasonably small amount of matches. */
  if (length > 8)
    length = 8;

  /* Try to find it from the database. */
  if (ssh_certdb_find(cm->db,
                      tag,
                      SSH_CM_KEY_TYPE_BER_HASH,
                      digest, length,
                      &found) != SSH_CDBET_OK)
    /* If not found then clearly cannot be in the database. */
    goto add_key;

  /* It didn't found anything that we could use. */
  if (found == NULL)
    goto add_key;

  /* Seek through the table. We expect that collisions are possible with
     because our accuracy with the hash is just 64 bits. With larger size
     the probability would become so negligible that we wouldn't need this
     test. */
  for (list = found->head; list; list = list->next)
    {
      SshCMCertificate cm_tmp_cert;
      SshCMCrl         cm_tmp_crl;

      switch (tag)
        {
        case SSH_CM_DATA_TYPE_CERTIFICATE:
          cm_tmp_cert = list->entry->context;

          if (cm_tmp_cert->ber_length == ber_length)
            {
              if (memcmp(cm_tmp_cert->ber, ber, ber_length) == 0)
                {
                  /* Return the entry identifier also. */
                  if (entry_id)
                    *entry_id = cm_tmp_cert->entry->id;

                  ssh_certdb_entry_list_free_all(cm->db, found);
                  return TRUE;
                }
            }
          break;
        case SSH_CM_DATA_TYPE_CRL:
          cm_tmp_crl = list->entry->context;

          if (cm_tmp_crl->ber_length == ber_length)
            {
              if (memcmp(cm_tmp_crl->ber, ber, ber_length) == 0)
                {
                  /* CRL entry identifier. */
                  if (entry_id)
                    *entry_id = cm_tmp_crl->entry->id;

                  ssh_certdb_entry_list_free_all(cm->db, found);
                  return TRUE;
                }
            }
          break;
        default:
          ssh_fatal("ssh_cm_check_db_collision: tag %u not supported.", tag);
          break;
        }
    }
  ssh_certdb_entry_list_free_all(cm->db, found);

  /* Add a key to the key list. */
 add_key:

  /* Check if the list actually is there. */
  if (key)
    {
      /* Was not found, thus push the hash to the key list. */
      key_digest = ssh_memdup(digest, length);
      /* Push to the list. */
      ssh_certdb_key_push(key, SSH_CM_KEY_TYPE_BER_HASH, key_digest, length);
    }

  /* The thing was not found from the database. */
  return FALSE;
}

/********************************************************/

static SshCMStatus
cm_search_process_rule(SshCertDB db,
                       SshCMSearchRule rule,
                       SshCertDBEntryList *combined,
                       SshCertDBEntryList *result)
{
  /* Handle the new list which was found. */
  switch (rule)
    {
    case SSH_CM_SEARCH_RULE_AND:
      if (!ssh_certdb_entry_list_empty(result))
        ssh_certdb_entry_list_intersect(db, combined, result);

      if (ssh_certdb_entry_list_empty(result) ||
          ssh_certdb_entry_list_empty(combined))
        {
          /* No entries found for current key, or the combination
             became empty. The search failed. */
          ssh_certdb_entry_list_free_all(db, result);
          ssh_certdb_entry_list_free_all(db, combined);
          return SSH_CM_STATUS_NOT_FOUND;
        }
      break;

    case SSH_CM_SEARCH_RULE_OR:
      if (!ssh_certdb_entry_list_empty(result))
        ssh_certdb_entry_list_union(db, combined, result);
      break;

    default:
      ssh_fatal("ssh_cm_search_dbs: rule %u unsupported.", rule);
      break;
    }
  return SSH_CM_STATUS_OK;
}

/* Searching from the local certificate cache. This will not consult
   external databases. */
static SshCMStatus
cm_search_local_cache(SshCMContext cm,
                      unsigned int tag,
                      SshCertDBKey *keys,
                      SshCMSearchRule rule,
                      SshCertDBEntryList **ret_found)
{
  SshCertDBEntryList *combined, *result;
  Boolean             first = TRUE;
  SshCMStatus         rv;

  SSH_DEBUG(SSH_D_MIDOK,
            ("Local cache search (%s).",
             (tag == SSH_CM_DATA_TYPE_CERTIFICATE
              ? "certificate" : "crl")));

  if (cm->db == NULL)
    return SSH_CM_STATUS_FAILURE;

  combined = NULL;
  for (; keys; keys = keys->next)
    {
      result = NULL;
      if (ssh_certdb_find(cm->db, tag,
                          keys->type, keys->data, keys->data_len,
                          &result) != SSH_CDBET_OK
          || !result)
        continue;

      /* Found something. Check if this is the first item. If so, only
         fill the combined list. */
      if (first)
        {
          combined = result;
          first = FALSE;
          continue;
        }

      if ((rv =
           cm_search_process_rule(cm->db, rule, combined, result))
          != SSH_CM_STATUS_OK)
        {
          *ret_found = NULL;
          return rv;
        }

      /* Free the key result list. */
      ssh_certdb_entry_list_free_all(cm->db, result);
    }

  *ret_found = combined;

  if (combined == NULL)
    return SSH_CM_STATUS_NOT_FOUND;

  /* Success. */
  return SSH_CM_STATUS_OK;
}

/* Higher level routines for search from the local database.

   NOTE: these functions are not to be used when trusted data is
   searched for. This interface is given for implementations that want
   to investigate the local database. */

/* Updates entry list by removing certs that do not fill time
   constraints given at the search. */
static void
cm_check_cert_time_constraint(SshCertDB db,
                              SshCertDBEntryList *list,
                              SshCMSearchConstraints constraints)
{
  SshCertDBEntryListNode node, next;

  for (node = list->head; node; node = next)
    {
      SshCMCertificate cm_cert = node->entry->context;
      SshX509Certificate cert = cm_cert->cert;

      next = node->next;

      if (ssh_ber_time_available(&constraints->not_before))
        {
          /* If it was issued after search start time? */
          if (ssh_ber_time_cmp(&constraints->not_before, &cert->not_before)
              < 0)
            {
              SSH_DEBUG(SSH_D_MIDOK,
                        ("Cert issued after search start time; %@ < %@",
                         ssh_ber_time_render, &cert->not_before,
                         ssh_ber_time_render, &constraints->not_before));

              ssh_certdb_entry_list_remove(db, node);
              continue;
            }
        }
      /* If it expires before search end time ? */
      if (ssh_ber_time_available(&constraints->not_after))
        {
          if (ssh_ber_time_cmp(&constraints->not_after, &cert->not_after) > 0)
            {
              SSH_DEBUG(SSH_D_MIDOK,
                        ("Cert expires before search end time; %@ < %@",
                         ssh_ber_time_render, &cert->not_before,
                         ssh_ber_time_render, &constraints->not_before));

              ssh_certdb_entry_list_remove(db, node);
              continue;
            }
        }
    }
}

SshCMStatus
ssh_cm_find_local_cert(SshCMContext cm,
                       SshCMSearchConstraints constraints,
                       SshCMCertList *cert_list)
{
  SshCertDBEntryList *list;

  if (cm_search_local_cache(cm, SSH_CM_DATA_TYPE_CERTIFICATE,
                            constraints->keys,
                            constraints->rule,
                            cert_list) != SSH_CM_STATUS_OK)
    {
      ssh_cm_search_free(constraints);
      return SSH_CM_STATUS_NOT_FOUND;
    }

  list = *cert_list;
  cm_check_cert_time_constraint(cm->db, list, constraints);
  ssh_cm_search_free(constraints);

  /* Check if the list is actually empty. */
  if (ssh_certdb_entry_list_empty(list))
    {
      ssh_certdb_entry_list_free_all(cm->db, list);
      *cert_list = NULL;
      return SSH_CM_STATUS_NOT_FOUND;
    }
  return SSH_CM_STATUS_OK;
}


SshCMStatus ssh_cm_find_local_crl(SshCMContext cm,
                                  SshCMSearchConstraints constraints,
                                  SshCMCrlList *crl_list)
{
  SshCertDBEntryListNode node, next;
  SshCertDBEntryList *list;

  if (cm_search_local_cache(cm, SSH_CM_DATA_TYPE_CRL,
                            constraints->keys,
                            constraints->rule,
                            crl_list) != SSH_CM_STATUS_OK)
    {
      ssh_cm_search_free(constraints);
      return SSH_CM_STATUS_NOT_FOUND;
    }

  /* Now traverse the found list in order to determine whether all
     of these are really useful CRL's. However, we will not delete our
     sole CRL from the list. */
  list = *crl_list;
  for (node = list->head; node; node = next)
    {
      SshCMCrl cm_crl = node->entry->context;
      SshX509Crl crl;

      next = node->next;

      if (cm_crl->status_flags & SSH_CM_CRL_FLAG_SKIP)
        {
          /* Remove from the list. */
          ssh_certdb_entry_list_remove(cm->db, node);
          continue;
        }

      crl = cm_crl->crl;

      if (ssh_ber_time_available(&constraints->not_after))
        {
          /* CRL issued after our time of interest ends */
          if (ssh_ber_time_cmp(&constraints->not_after, &crl->this_update) < 0)
            {
              /* Too new. */
              ssh_certdb_entry_list_remove(cm->db, node);
              continue;
            }
        }
      if (ssh_ber_time_available(&constraints->not_before))
        {
          /* CRL is only valid before our time of interest begins */
          if (ssh_ber_time_available(&crl->next_update) &&
              ssh_ber_time_cmp(&constraints->not_before, &crl->next_update)
              >= 0)
            {
              /* Too old. */
              ssh_certdb_entry_list_remove(cm->db, node);
              continue;
            }
        }
    }
  ssh_cm_search_free(constraints);

  /* Check if the list is actually empty. */
  if (ssh_certdb_entry_list_empty(list))
    {
      ssh_certdb_entry_list_free_all(cm->db, list);
      *crl_list = NULL;
      return SSH_CM_STATUS_NOT_FOUND;
    }

  return SSH_CM_STATUS_OK;
}

SshCMStatus
ssh_cm_find_local_cert_issuer(SshCMContext cm,
                              SshCMSearchConstraints constraints,
                              SshCMCertList *issuer_list)
{
  SshCertDBEntryList *list, *issuers, *comb;
  SshCertDBEntryListNode tmp;
  SshCertDBKey *names;

  comb = NULL;

  SSH_DEBUG(SSH_D_MIDOK, ("Local database/memory cache search issuer."));

  /* First find the certificate denoted by the search information. */
  list = NULL;
  if (ssh_cm_find_local_cert(cm, constraints, &list) != SSH_CM_STATUS_OK)
    {
      ssh_cm_search_free(constraints);
      return SSH_CM_STATUS_NOT_FOUND;
    }

  /* After that search for the issuer based on the information gained. */
  for (tmp = list->head; tmp; tmp = tmp->next)
    {
      SshCMCertificate cm_cert;

      cm_cert = tmp->entry->context;

      names = NULL;
      if (ssh_cm_cert_get_issuer_keys(cm_cert, &names) != SSH_CM_STATUS_OK)
        continue;

      issuers = NULL;
      if (cm_search_local_cache(cm, SSH_CM_DATA_TYPE_CERTIFICATE,
                                names,
                                constraints->rule,
                                &issuers) != SSH_CM_STATUS_OK)
        continue;

      if (comb == NULL)
        comb = issuers;
      else
        {
          ssh_certdb_entry_list_union(cm->db, comb, issuers);
          ssh_certdb_entry_list_free_all(cm->db, issuers);
        }
    }

  cm_check_cert_time_constraint(cm->db, comb, constraints);
  ssh_cm_search_free(constraints);

  if (ssh_certdb_entry_list_empty(comb) == TRUE)
    {
      ssh_certdb_entry_list_free_all(cm->db, comb);
      return SSH_CM_STATUS_NOT_FOUND;
    }

  *issuer_list = comb;
  return SSH_CM_STATUS_OK;
}


/**** The general search routine that finds everything. */

/* First search from local databases, that are fast to search. Then
   one can deside whether the information was sufficient and whether
   external database searches are needed. However, this needs to be
   taken care of by the above layer.

   There are reasons why this division have been made, mainly they are
   special cases where otherwise we would find ourselves in situations
   that end up as failures even if success would be possible. */

static SshCMStatus
cm_search_local_dbs(SshCMSearchContext *search,
                    unsigned int tag,
                    SshCertDBKey *keys,
                    SshCMSearchRule rule,
                    SshCertDBEntryList **ret_found)
{
  SshCertDBEntryList   *combined, *result;
  Boolean               first = TRUE;
  SshCMDBDistinguisher *distinguisher = NULL;
  SshCMContext          cm = search->cm;
  SshCMStatus           rv;

  SSH_DEBUG(SSH_D_MIDOK,
            ("Local database search for %s.",
             (tag == SSH_CM_DATA_TYPE_CERTIFICATE ? "certificate" : "crl")));


  *ret_found = NULL;

  combined     = NULL;
  for (; keys; keys = keys->next)
    {
      Boolean found = FALSE;

      result = NULL;

      /* The special case of searching from the local cache. Search
         always as it is fast, and usually we find what we are looking
         for. (If not then the time spent here didn't actually cost
         anything.) */

      if (cm->db)
        {
        retry_local:
          /* Find from the database. */
          if (ssh_certdb_find(cm->db, tag,
                              keys->type, keys->data, keys->data_len,
                              &result) == SSH_CDBET_OK
              && result)
            {
              found = TRUE;
              goto found_local;
            }
        }

      /* Now try other locally configured db searching. */

      /* Create distinguisher. */
      if ((distinguisher = ssh_cm_edb_distinguisher_allocate()) == NULL)
        {
          ssh_certdb_entry_list_free_all(cm->db, combined);
          return SSH_CM_STATUS_NOT_FOUND;
        }

      distinguisher->data_type  = tag;
      distinguisher->key_type   = keys->type;
      distinguisher->key        = ssh_memdup(keys->data, keys->data_len);
      distinguisher->key_length = keys->data_len;
#if 0
      distinguisher->edb_conversion_function = keys->edb_conversion_function;
      distinguisher->edb_conversion_function_context =
        keys->edb_conversion_function_context;
#endif

      if (distinguisher->key == NULL)
        {
          ssh_cm_edb_distinguisher_free(distinguisher);
          ssh_certdb_entry_list_free_all(cm->db, combined);
          return SSH_CM_STATUS_NOT_FOUND;
        }

      switch (ssh_cm_edb_search_local(search, distinguisher))
        {
        case SSH_CMEDB_OK:
          SSH_DEBUG(SSH_D_LOWOK,
                    ("Found from a local DB; retrying from the cache."));
          ssh_cm_edb_distinguisher_free(distinguisher);
          goto retry_local;

        case SSH_CMEDB_NOT_FOUND:
          SSH_DEBUG(SSH_D_LOWOK,
                    ("Local DB search failed for the distinguisher."));
          ssh_cm_edb_distinguisher_free(distinguisher);
          break;

        default:
          ssh_fatal("cm_search_local_dbs: "
                    "unknown search result, possible implementation failure.");
          break;
        }

      if (!result)
        continue;

    found_local:
      /* Now we have something at the result. Check if this was a
         first hit. If so, initialize combined result list with the
         data found and look for next key. */
      if (first)
        {
          combined = result;
          first = FALSE;
          continue;
        }

      if ((rv =
           cm_search_process_rule(cm->db, rule, combined, result))
          != SSH_CM_STATUS_OK)
        {
          *ret_found = NULL;
          return rv;
        }

      /* Free the key result list. */
      ssh_certdb_entry_list_free_all(cm->db, result);
    }

  *ret_found = combined;
  if (combined == NULL)
    {
      SSH_DEBUG(SSH_D_HIGHOK, ("ssh.local: [failed]."));
      return SSH_CM_STATUS_NOT_FOUND;
    }

  SSH_DEBUG(SSH_D_HIGHOK, ("ssh.local: [finished]."));
  return SSH_CM_STATUS_OK;
}


/* This function is the general search function that is called
   when ever things are needed. */
static SshCMStatus
cm_search_dbs(SshCMSearchContext *search,
              unsigned int tag,
              SshCertDBKey *keys,
              SshCMSearchRule rule,
              SshCertDBEntryList **ret_found)
{
  SshCertDBEntryList   *combined, *result;
  SshCMContext          cm = search->cm;
  Boolean               first = TRUE;
  SshCMDBDistinguisher *distinguisher = NULL;
  SshCMStatus           rv;

  /* Initialize the found values computation. */
  combined     = NULL;
  for (; keys; keys = keys->next)
    {
      result = NULL;

      /* Now try external database search, first create distinguisher. */
      if ((distinguisher = ssh_cm_edb_distinguisher_allocate()) == NULL)
        {
          ssh_certdb_entry_list_free_all(cm->db, combined);
          return SSH_CM_STATUS_NOT_FOUND;
        }

      distinguisher->data_type  = tag;
      distinguisher->key_type   = keys->type;
      if ((distinguisher->key = ssh_memdup(keys->data, keys->data_len))
          != NULL)
        distinguisher->key_length = keys->data_len;
#if 0
      distinguisher->edb_conversion_function = keys->edb_conversion_function;
      distinguisher->edb_conversion_function_context =
        keys->edb_conversion_function_context;
#endif

      if (distinguisher->key == NULL)
        {
          ssh_cm_edb_distinguisher_free(distinguisher);
          ssh_certdb_entry_list_free_all(cm->db, combined);
          return SSH_CM_STATUS_NOT_FOUND;
        }

      switch (ssh_cm_edb_search(search, distinguisher))
        {
        case SSH_CMEDB_OK:
          ssh_cm_edb_distinguisher_free(distinguisher);

          /* Grab the object found from the cache, where the search
             routine puts objects found. */
          if (cm->db)
            ssh_certdb_find(cm->db, tag,
                            keys->type, keys->data, keys->data_len,
                            &result);
          else
            result = NULL;
          break;

        case SSH_CMEDB_SEARCHING:
          ssh_cm_edb_distinguisher_free(distinguisher);
          ssh_certdb_entry_list_free_all(cm->db, combined);
          *ret_found = NULL;
          return SSH_CM_STATUS_SEARCHING;

        case SSH_CMEDB_DELAYED:
          ssh_cm_edb_distinguisher_free(distinguisher);
          break;

        case SSH_CMEDB_NOT_FOUND:
          ssh_cm_edb_distinguisher_free(distinguisher);
          break;

        default:
          ssh_fatal("ssh_cm_search_dbs: unknown search result.");
          break;
        }


      if (result)
        {
          if (first)
            {
              combined = result;
              first    = FALSE;
              continue;
            }

          if ((rv =
               cm_search_process_rule(cm->db, rule, combined, result))
              != SSH_CM_STATUS_OK)
            {
              *ret_found = NULL;
              return rv;
            }

          /* Free the key result list. */
          ssh_certdb_entry_list_free_all(cm->db, result);
        }
    }

  *ret_found = combined;
  if (combined == NULL)
    {
      SSH_DEBUG(4, ("Search external DB's was a failure."));
      return SSH_CM_STATUS_NOT_FOUND;
    }

  SSH_DEBUG(4, ("Search external DB's was a success."));
   return SSH_CM_STATUS_OK;
}

SshCMStatus
ssh_cm_compute_validity_times(SshCMSearchContext *search)
{
  SshCMContext               cm = search->cm;
  SshCMSearchConstraints constraints = search->end_cert;
  SshTime                   now;

  SSH_DEBUG(5, ("Compute validity times and get the current time."));

  now = (*cm->config->time_func)(cm->config->time_context);

  if (ssh_ber_time_available(&constraints->not_before))
    now = ssh_ber_time_get_unix_time(&constraints->not_before);

  /* First, set current time from system clock. This may be changed by
     the current search constraints */
  ssh_ber_time_set_from_unix_time(&search->cur_time, now);
  ssh_ber_time_set_from_unix_time(&search->max_cert_validity_time,
                                  now + cm->config->max_validity_secs);
  ssh_ber_time_set_from_unix_time(&search->max_crl_validity_time,
                                  now + cm->config->min_crl_validity_secs);

  ssh_ber_time_set(&search->valid_time_start, &search->cur_time);

  if (ssh_ber_time_available(&constraints->not_after))
    ssh_ber_time_set(&search->valid_time_end, &constraints->not_after);
  else
    ssh_ber_time_set(&search->valid_time_end,
                     &search->max_cert_validity_time);

  return SSH_CM_STATUS_OK;
}

/* current is the current certificate being considered on path
   building.  previous is the head of found so far, that is base cert
   for issuer search (or NULL if for_end_cert is set). Constraints set
   by previous will be enforced on the subject. */
SshCMStatus
ssh_cm_cert_apply_constraints(SshCMSearchContext *search,
                              SshCMCertificate current,
                              SshCMCertificate previous,
                              Boolean for_end_cert)
{
  SshCMContext cm = search->cm;
  SshCMSearchConstraints constraints = search->end_cert;
  SshBerTimeStruct cert_not_before, cert_not_after;
  Boolean critical;

  SSH_DEBUG(SSH_D_HIGHOK, ("Applying constraints: Time is '%@'.",
                           ssh_ber_time_render, &search->cur_time));

  /* Check whether the certificate must be removed always from the
     search list! */

  /* Check first if the certificate is revoked. */
  if (constraints->check_revocation
      && current->status == SSH_CM_VS_REVOKED
      && !ssh_cm_trust_is_valid(current, search))
    {
      SSH_DEBUG(SSH_D_NETFAULT, ("Cert is revoked (terminates chain)."));
      SSH_CM_NOTEX(search, CERT_REVOKED);
      return SSH_CM_STATUS_CANNOT_BE_VALID;
    }

  /* The hard time limit is given here. */

  ssh_ber_time_zero(&cert_not_before);
  ssh_ber_time_zero(&cert_not_after);

  /* Check the certificate dates. */
  if (ssh_x509_cert_get_validity(current->cert,
                                 &cert_not_before,
                                 &cert_not_after) == FALSE)
    {
      SSH_DEBUG(SSH_D_NETFAULT,
                ("Cert validity times not available (terminates chain)."));
      SSH_CM_NOTEX(search, CERT_INVALID);
      return SSH_CM_STATUS_CANNOT_BE_VALID;
    }

  /* Check the times. */
  if (ssh_ber_time_cmp(&cert_not_before, &search->valid_time_start) >= 0 ||
      ssh_ber_time_cmp(&cert_not_after,  &search->valid_time_start) < 0 ||
      ssh_ber_time_cmp(&cert_not_before, &search->valid_time_end) > 0 ||
      ssh_ber_time_cmp(&cert_not_after,  &search->valid_time_end) <= 0)
    {
      /* Cannot succeed, as this certificate does not allow the full
         search interval to be applied. */
      SSH_DEBUG(SSH_D_NETFAULT,
                ("Cert is not within search interval (terminates chain)."));
      SSH_CM_NOTEX(search, CERT_NOT_IN_INTERVAL);
      return SSH_CM_STATUS_CANNOT_BE_VALID;
    }

  /* Check if we can optimize the search with additional constraints
     for the end certificate. */
  if (for_end_cert)
    {
      SshX509Certificate cert = current->cert;

      if (constraints->pk_algorithm != SSH_X509_PKALG_UNKNOWN &&
          cert->subject_pkey.pk_type != constraints->pk_algorithm)
        {
          SSH_DEBUG(SSH_D_NETFAULT,
                    ("Cert pubkey algorithm did not match the search "
                     "constraints (terminates chain)."));
          SSH_CM_NOTEX(search, CERT_ALG_MISMATCH);
          return SSH_CM_STATUS_CANNOT_BE_VALID;
        }

      if (constraints->key_usage_flags != 0)
        {
          SshX509UsageFlags flags;

          if (ssh_x509_cert_get_key_usage(cert, &flags, &critical))
            {
              if (flags != 0 && (flags & constraints->key_usage_flags) == 0)
                {
                  SSH_DEBUG(SSH_D_NETFAULT,
                            ("Cert key usage did not match the search "
                             "constraints (terminates chain)."));
                  SSH_CM_NOTEX(search, CERT_KEY_USAGE_MISMATCH);
                  return SSH_CM_STATUS_CANNOT_BE_VALID;
                }
            }
        }
    }

  if (previous)
    {
      if (!cm_verify_issuer_name(previous, current) ||
          !cm_verify_issuer_id(previous, current))
        {
          SSH_DEBUG(SSH_D_NETFAULT,
                    ("Issuer name or key identifier does not match "
                     "constraints set by subject; "
                     "offending certificates %@ and %@",
                     ssh_cm_render_certificate, current->cert,
                     ssh_cm_render_certificate, previous->cert));
          SSH_CM_NOTEX(search, CERT_CA_INVALID);
          return SSH_CM_STATUS_CANNOT_BE_VALID;
        }
    }

  /* Should the certificate be trusted for the full validity period? */
  if (ssh_cm_trust_is_root(current, search) == TRUE)
    {
      /* Note; it would be impossible to get here if the given
         certificate would not be a trusted root. However, we check here
         for the case that the certificate is a revoked by a trusted
         certificate. */
      if (constraints->check_revocation
          && !ssh_cm_trust_is_valid(current, search)
          && current->revocator_was_trusted)
        {
          SSH_DEBUG(SSH_D_NETFAULT,
                    ("Trust anchor %@ was revoked by another trust-anchor.",
                     ssh_cm_render_certificate, current->cert));
          SSH_CM_NOTEX(search, CERT_INVALID);
          return SSH_CM_STATUS_CANNOT_BE_VALID;
        }
      goto check_ca;
    }

  /* Check if a CA has been revoked recently. In such a case no
     certificate is valid until proven so. The problem here is that
     such a recomputation can be very time consuming. We'd like to
     avoid it as much as possible... */
  if (ssh_ber_time_cmp(&cm->ca_last_revoked_time,
                       &current->trusted.trusted_computed) >= 0)
    {
      /* We could check the validity period within the actual certificate
         here also. */
      SSH_DEBUG(SSH_D_MIDOK,
                ("Possibly a CA has been revoked before trust computation "
                 "took place, hence recomputing trust."));
      return SSH_CM_STATUS_NOT_VALID;
    }

  /* Check the validity period, if the certificate has been recently
     checked. */
  if (ssh_cm_trust_check(current, NULL, search))
    {
      /* Check the times. */
      if (ssh_ber_time_cmp(&current->trusted.valid_not_before,
                           &search->valid_time_start) >= 0 ||
          ssh_ber_time_cmp(&current->trusted.valid_not_after,
                           &search->valid_time_start) < 0 ||
          ssh_ber_time_cmp(&current->trusted.valid_not_before,
                           &search->valid_time_end) > 0 ||
          ssh_ber_time_cmp(&current->trusted.valid_not_after,
                           &search->valid_time_end) <= 0)
        {
          SSH_DEBUG(SSH_D_NETFAULT,
                    ("Trusted certificate was not valid in "
                     "the computed interval."));

          SSH_DEBUG(SSH_D_NETFAULT,
                    ("Trusted during interval '%@' -> '%@'",
                     ssh_ber_time_render, &current->trusted.valid_not_before,
                     ssh_ber_time_render, &current->trusted.valid_not_after));

          SSH_DEBUG(SSH_D_NETFAULT,
                    ("Requested interval '%@' -> '%@'",
                     ssh_ber_time_render, &search->valid_time_start,
                     ssh_ber_time_render, &search->valid_time_end));

          /* The trust computation might lead to a better path. */
          return SSH_CM_STATUS_NOT_VALID;
        }

      SSH_DEBUG(SSH_D_MIDOK,
                ("Certificate trusted until '%@'",
                 ssh_ber_time_render, &current->trusted.trusted_not_after));

      if (ssh_ber_time_cmp(&current->trusted.trusted_not_after,
                           &search->cur_time) < 0)
        {
          SSH_DEBUG(SSH_D_NETFAULT,
                    ("Trusted certificate was not trusted at present time."));
          return SSH_CM_STATUS_NOT_VALID;
        }
    }

 check_ca:

  /* Check if the search is upto trusted roots. */
  if (constraints->upto_root)
    if (ssh_cm_trust_is_root(current, search) == FALSE)
      {
        SSH_DEBUG(SSH_D_NETFAULT,
                  ("Not a trusted root certificate (terminated chain)."));
        return SSH_CM_STATUS_NOT_VALID;
      }

  /* Are we searching for a path to some particular CA? */
  if (search->ca_cert != NULL)
    {
      SshCertDBEntryListNode tmp;
      for (tmp = search->ca_cert->head; tmp; tmp = tmp->next)
        {
          if (tmp->entry->context == current)
            {
              /* We have found the correct CA. Thus we are on the
                 right path. And because this CA has been found
                 before (with same parameters) it holds that
                 the path validation can be called. */
              return SSH_CM_STATUS_OK;
            }

          /* Not found yet. */
        }
      SSH_DEBUG(SSH_D_MIDOK, ("Selected CA was not yet found."));
      /* Cannot be valid until the correct CA is found! */
      return SSH_CM_STATUS_NOT_VALID;
    }

  /* Not valid path yet. */
  return SSH_CM_STATUS_OK;
}

/* Revoked certificates are kept in a hash table indexed by serial
   number. One hash table is kept per CRL stored. For each revoked
   entry we store serial and revocation date and reason. */

typedef struct SshCMRevokedRec
{
  /* Concrete header and object model. */
  SshADTMapHeaderStruct adt_header;

  SshMPIntegerStruct serial;
  SshBerTimeStruct revocation;
  SshX509CRLReasonCode reason;
} *SshCMRevoked, SshCMRevokedStruct;

static SshUInt32
cm_revoked_hash(const void *object, void *context)
{
  SshCMRevoked r = (SshCMRevoked) object;
  SshUInt32 h = 0;
  size_t len, i;
  unsigned char linear[1024];

  if ((len = ssh_mprz_get_buf(linear, sizeof(linear), &r->serial)) != 0)
    {
      for (i = 0; i < len; i++)
        h = linear[i] ^ ((h << 7) | (h >> 26));
    }
  return h;
}

static int
cm_revoked_compare(const void *object1, const void *object2,
                   void *context)
{
  SshCMRevoked r1 = (SshCMRevoked) object1;
  SshCMRevoked r2 = (SshCMRevoked) object2;

  return ssh_mprz_cmp(&r1->serial, &r2->serial);
}

static void
cm_revoked_destroy(void *object, void *context)
{
  SshCMRevoked r = (SshCMRevoked) object;

  ssh_mprz_clear(&r->serial);
  ssh_free(r);
}

void ssh_cm_cert_revoke(SshCMSearchContext *search,
                        SshCMCertificate ca,
                        SshCMCertificate subject,
                        SshCMRevoked revoked)
{
  SshCMContext cm = search->cm;

  if (subject->acting_ca)
    {
      /* We set here the time of last CA revocation to
         the current time. It could be tried to get
         from the actual dates in the revocation
         information. However, we don't entirely know
         whether it is in future or in past or
         whatever. It might be safest to use just
         the current time. */
      ssh_ber_time_set(&cm->ca_last_revoked_time,
                       &search->cur_time);
    }

  switch (revoked->reason)
    {
    case SSH_X509_CRLF_CERTIFICATE_HOLD:
      /* The certitificate is on hold. */
      SSH_DEBUG(SSH_D_NETFAULT,
                ("Certificate on hold: %@",
                 ssh_cm_render_certificate, subject->cert));

      /* Make it unsafe nevertheless. */
      subject->status = SSH_CM_VS_HOLD;
      /* Set crl_recompute_after in subject from ca. This is checked when
         we find suspended certificate from cache to determine if its
         suspension status should be rechecked from its issuer. */
      ssh_ber_time_set(&subject->crl_recompute_after,
                       &ca->crl_recompute_after);
      SSH_CM_NOTIFY_CERT(cm, REVOKED, subject);
      break;
    case SSH_X509_CRLF_REMOVE_FROM_CRL:
      /* It is apparently not on CRL anymore. */
      SSH_DEBUG(SSH_D_HIGHOK,
                ("Certificate removed from CRL: %@",
                 ssh_cm_render_certificate, subject->cert));
      break;
    default:

      /* Revoked! */
      SSH_DEBUG(SSH_D_NETFAULT,
                ("Certificate revoked: %@",
                 ssh_cm_render_certificate, subject->cert));

      /* The certificate is revoked. */
      subject->status = SSH_CM_VS_REVOKED;
      if (ssh_cm_trust_is_root(ca, search))
        subject->revocator_was_trusted = TRUE;
      ssh_cm_trust_make_user(subject, search);

      ssh_ber_time_set(&subject->trusted.trusted_not_after,
                       &revoked->revocation);
      /* Let the application know of the revocation. */
      SSH_CM_NOTIFY_CERT(cm, REVOKED, subject);
      break;
    }
}

void ssh_cm_crl_initial_cert_transform(SshCMSearchContext *search,
                                       SshCMCertificate ca,
                                       SshCMCertificate subject)
{
  SSH_DEBUG(SSH_D_LOWOK,
            ("Initial transform for %@",
             ssh_cm_render_certificate, subject->cert));

  switch (subject->status)
    {
    case SSH_CM_VS_HOLD:
      /* Remove the hold status as the CRL did not contain
         the certificate anymore. */
      SSH_DEBUG(SSH_D_MIDOK,
                ("Hold status reset for %@",
                 ssh_cm_render_certificate, subject->cert));
      subject->status = SSH_CM_VS_OK;
      break;
    case SSH_CM_VS_OK:
      break;
    default:
      break;
    }
}

void ssh_cm_crl_final_cert_transform(SshCMSearchContext *search,
                                     SshCMCertificate ca,
                                     SshCMCertificate subject,
                                     Boolean result)
{
  if (result)
    {
      switch (subject->status)
        {
        case SSH_CM_VS_HOLD:
          break;
        case SSH_CM_VS_OK:
          break;
        default:
          break;
        }
    }
  else
    {
      /* The certificate is not valid. */
    }
}

/* Handle revocation in this function.
   Return values: 0 -> error, crl is invalid
                  1 -> ok, crl is for this certificate and was processed.
                  2 -> ok, crl is valid, but not for this certificate.
 */
static int
cm_crl_revoke(SshCMSearchContext *search,
              SshCMCrl cm_crl,
              SshCMCertificate ca,
              SshCMCertificate subject,
              SshX509ReasonFlags *reasons)
{
  SshCMContext cm = search->cm;
  SshX509Crl   crl;
  SshX509RevokedCerts revoked, next_revoked;
  SshCertDBEntryList *found;
  SshCertDBEntryListNode tmp;
  SshADTHandle handle;
  SshCMRevoked r;
  SshX509UsageFlags flags;
  Boolean critical;

  if (ssh_x509_cert_get_key_usage(ca->cert, &flags, &critical))
    {
      if (flags != 0 && (flags & SSH_X509_UF_CRL_SIGN) == 0)
        {
          SSH_DEBUG(SSH_D_FAIL, ("Issuer of CRL is not allowed to sign CRL"));
          SSH_CM_NOTEX(search, CRL_INVALID);
          return 0;
        }
    }

  /* Get the X.509 crl. */
  crl    = cm_crl->crl;

  /* Dump the found CRL. */
  SSH_DEBUG(SSH_D_MIDOK, ("Found CRL: %@", ssh_cm_render_crl, crl));

  ssh_ber_time_set(&ca->crl_recompute_after, &search->max_crl_validity_time);

  /* Set the new CRL recompute after values. */
  if (ssh_ber_time_available(&crl->next_update))
    {
      if (ssh_ber_time_cmp(&crl->next_update, &search->cur_time) > 0)
        {
          if (ssh_ber_time_cmp(&crl->next_update,
                               &ca->crl_recompute_after) < 0)
            {
              ssh_ber_time_set(&ca->crl_recompute_after, &crl->next_update);
              SSH_DEBUG(SSH_D_LOWOK,
                        ("Adjusting CRL recomputation time to '%@'.",
                         ssh_ber_time_render, &ca->crl_recompute_after));
            }
        }
      else
        {
          SSH_DEBUG(SSH_D_NETFAULT,
                    ("This CRL is not valid at requested time."));

          /* In this case, that is, we have the latest CRL which is
             still not valid! We can only deduce if no other CRL's can
             be found, from external databases, then this path
             validation process must terminate as non-valid. */
          SSH_CM_NOTEX(search, CRL_INVALID);
          ssh_ber_time_set(&ca->crl_recompute_after, &search->cur_time);
          return 0;
        }
    }

  SSH_DEBUG(SSH_D_LOWOK,
            ("CRL recomputation time set to '%@'.",
             ssh_ber_time_render, &ca->crl_recompute_after));

  /* Check through the X.509 CRL extensions. */
  {
    SshX509ExtIssuingDistPoint  idp;
    SshX509Certificate          subject_cert;
    SshMPIntegerStruct          delta;

    /* Determine the X.509 certificate of the subject. */
    if (ssh_cm_cert_get_x509(subject, &subject_cert) != SSH_CM_STATUS_OK)
      {
        SSH_DEBUG(SSH_D_FAIL, ("Can't get DER out of CMI certificate."));
        SSH_CM_NOTEX(search, CERT_INVALID);
        return 0;
      }
    ssh_mprz_init(&delta);
    if (ssh_x509_crl_get_delta_crl_indicator(crl, &delta, &critical))
      {
        ssh_x509_cert_free(subject_cert);
        SSH_DEBUG(SSH_D_FAIL, ("Delta CRL is not supported."));
        SSH_CM_NOTEX(search, CRL_INVALID);
        ssh_mprz_clear(&delta);
        return 0;
      }
    ssh_mprz_clear(&delta);

    /* Issuing distribution point. */
    if (ssh_x509_crl_get_issuing_dist_point(crl, &idp, &critical))
      {
        Boolean ca, matches_subject, free_full_idp;
        size_t  path_length, der_len;
        SshX509ExtCRLDistPoints cdp, p;
        unsigned char *der;
        char *idpuri;
        SshX509Name full_idp;

        if (critical != TRUE)
          {
            /* CRL issuing distribution points should be critical. */
            ssh_x509_cert_free(subject_cert);
            SSH_DEBUG(SSH_D_NETFAULT, ("IssuingDP not critical on CRL."));
            SSH_CM_NOTEX(search, CRL_INVALID);
            return 0;
          }
        /* Check if this IDP is for the subject certificate, e.g IDP
           matches what is stated in the subject certificate. */
        if (!ssh_x509_cert_get_crl_dist_points(subject_cert,
                                               &cdp, &critical))
          {
            cdp = NULL;
          }

        free_full_idp = FALSE;
        if (idp->dn_relative_to_issuer)
          {
            full_idp = cm_dp_make_full_name(subject_cert->issuer_name,
                                            idp->dn_relative_to_issuer);
            free_full_idp = TRUE;
          }
        else if (idp->full_name)
          {
            full_idp = idp->full_name;
          }
        else
          {
            full_idp = NULL;
          }

        /* If IDP contains a full name of the distributionPoint, check that
           a matching name is found from certificate. */
        if (full_idp)
          {
            matches_subject = FALSE;

            if (!cdp)
              {
                if (free_full_idp) ssh_x509_name_free(full_idp);
                ssh_x509_cert_free(subject_cert);
                SSH_DEBUG(SSH_D_NETFAULT,
                          ("IssuingDP on CRL but subject does not utilize "
                           "multiple CDP's."));
                SSH_CM_NOTEX(search, CRL_INVALID);
                return 2;
              }

            /* First loop to check whether we have a matching DN */
            ssh_x509_name_reset(full_idp);
            while (ssh_x509_name_pop_directory_name_der(full_idp,
                                                        &der, &der_len))
              {
                Boolean free_full_cdp;
                SshX509Name full_cdp;

                for (p = cdp; p; p = p->next)
                  {
                    free_full_cdp = FALSE;

                    if (p->full_name == NULL)
                      {
                        full_cdp =
                          cm_dp_make_full_name(subject_cert->issuer_name,
                                               p->dn_relative_to_issuer);
                        free_full_cdp = TRUE;
                      }
                    else
                      full_cdp = p->full_name;

                    /* CRL's issuer must match CRL issuer if any */
                    if (p->crl_issuer)
                      {
                        if (!cm_name_equal(p->crl_issuer,
                                           crl->issuer_name))
                          {
                            SSH_DEBUG(SSH_D_MIDOK,
                                      ("Certificate indicated CRL issuer "
                                       "does not match this CRL's issuer."));
                            continue;
                          }
                      }

                    /* Distribution point name must match */
                    if (full_cdp->ber_len == der_len &&
                        memcmp(der, full_cdp->ber, der_len) == 0)
                      {
                        if (free_full_cdp)
                          ssh_x509_name_free(full_cdp);
                        matches_subject = TRUE;
                        ssh_free(der);
                        goto foundit;
                      }
                    if (free_full_cdp)
                      ssh_x509_name_free(full_cdp);
                  }
                ssh_free(der);
              }

            /* Second loop to check whether we have a matching URI */
            ssh_x509_name_reset(full_idp);
            while (ssh_x509_name_pop_uri(idp->full_name, &idpuri))
              {
                for (p = cdp; p; p = p->next)
                  {
                    unsigned char *cdpuri;
                    size_t cdpuri_len; /* ignored */

                    if (p->full_name == NULL || p->full_name->name == NULL ||
                        (cdpuri =
                         ssh_str_get(p->full_name->name, &cdpuri_len)) == NULL)
                      continue;

                    if (0 == strcmp(idpuri, (char *)cdpuri))
                      {
                        matches_subject = TRUE;
                        ssh_free(cdpuri); ssh_free(idpuri);
                        goto foundit;
                      }
                    ssh_free(cdpuri);
                  }
                ssh_free(idpuri);
              }

          foundit:
            ssh_x509_name_reset(idp->full_name);
            if (free_full_idp)
              ssh_x509_name_free(full_idp);

            if (!matches_subject)
              {
                ssh_x509_cert_free(subject_cert);
                SSH_DEBUG(SSH_D_NETFAULT,
                          ("IssuingDP does not match the subject "
                           "certificate."));
                SSH_CM_NOTEX(search, CRL_INVALID);
                return 2;
              }
          }

        /* Get the basic constraints. */
        if (!ssh_x509_cert_get_basic_constraints(subject_cert,
                                                 &path_length,
                                                 &ca,
                                                 &critical))
          ca = FALSE;

        ssh_x509_cert_free(subject_cert);

        /* Check that they conform to the flags. */
        if (idp->only_contains_attribute_certs)
          {
            SSH_DEBUG(SSH_D_NETFAULT,
                      ("IssuingDP for attributeCerts only."));
            SSH_CM_NOTEX(search, CRL_INVALID);
            return 2;
          }

        if (idp->only_contains_user_certs && ca == TRUE)
          {
            SSH_DEBUG(SSH_D_NETFAULT,
                      ("IssuingDP for userCerts, subjectCert is CA."));
            SSH_CM_NOTEX(search, CRL_INVALID);
            return 2;
          }

        if (idp->only_contains_ca_certs && ca == FALSE)
          {
            SSH_DEBUG(SSH_D_NETFAULT,
                      ("IssuingDP for CaCerts, subjectCert is User."));
            SSH_CM_NOTEX(search, CRL_INVALID);
            return 2;
          }

        /* Check the reason flags. */
        if (idp->only_some_reasons != 0)
          /* Which reasons? */
          *reasons |= idp->only_some_reasons;
        else
          /* All reasons. */
          *reasons |= 0x80ff;
      }
    else
      {
        /* No issuing distribution point -> all reasons. */
        ssh_x509_cert_free(subject_cert);
        *reasons |=  0x80ff;
      }
  }

  /* Create revoked mapping on the cm_crl entry. */
  if (cm_crl->revoked == NULL)
    {
      if ((cm_crl->revoked =
           ssh_adt_create_generic(SSH_ADT_MAP,
                                  SSH_ADT_HASH,    cm_revoked_hash,
                                  SSH_ADT_COMPARE, cm_revoked_compare,
                                  SSH_ADT_DESTROY, cm_revoked_destroy,
                                  SSH_ADT_HEADER,
                                  SSH_ADT_OFFSET_OF(SshCMRevokedStruct,
                                                    adt_header),
                                  SSH_ADT_ARGS_END)) == NULL)
        {
          SSH_CM_NOTEX(search, CERT_NOT_ADDED);
          return 0;
        }

      /* Now apply this revocation list to all the certificates on our
         local cache, and add entry to mapping of all revoked serial
         numbers. */
      for (revoked = crl->revoked; revoked; revoked = next_revoked)
        {
          unsigned char *buf;
          size_t buf_len;

          /* Processing will remove revoked cert list from CRL, copy
             will remain at ADT mapping above. */
          next_revoked = revoked->next;

          if ((r = ssh_calloc(1, sizeof(*r))) == NULL)
            {
              SSH_CM_NOTEX(search, CERT_NOT_ADDED);
              return 0;
            }

          ssh_mprz_init_set(&r->serial, &revoked->serial_number);
          ssh_ber_time_set(&r->revocation, &revoked->revocation_date);
          r->reason = revoked->extensions.reason_code;

          /* Insert into set of revoked certificates. */
          ssh_adt_insert(cm_crl->revoked, r);

          /* Make space by getting rid of duplicate data. Don't want
             to free next, unprocessed though. */
          revoked->next = NULL;
          ssh_x509_revoked_free(revoked);

          /* Check revocation date against current time. */
          if (ssh_ber_time_cmp(&r->revocation, &search->cur_time) > 0)
            {
              /* Don't revoke (mark cert as revoked) yet, instead make
                 the needed adjustments to the next check. */
              if (ssh_ber_time_cmp(&r->revocation, &ca->crl_recompute_after)
                  < 0)
                ssh_ber_time_set(&ca->crl_recompute_after, &r->revocation);
              continue;
            }

          /* Heuristically try searching first with the serial number and
             issuer name hash. This might lead to faster search. */
          buf =
            ssh_cm_get_hash_of_serial_no_and_issuer_name(&r->serial,
                                                         crl->issuer_name,
                                                         &buf_len);
          if (buf)
            {
              if (ssh_certdb_find(cm->db,
                                  SSH_CM_DATA_TYPE_CERTIFICATE,
                                  SSH_CM_KEY_TYPE_SI_HASH,
                                  buf, buf_len,
                                  &found) != SSH_CDBET_OK)
                {
                  ssh_free(buf);
                  continue;
                }
              ssh_free(buf);
            }
          else
            {
              /* Convert the serial number into buffer inplace. */
              buf_len = (ssh_mprz_get_size(&r->serial, 2) + 7)/8;
              if ((buf = ssh_calloc(1, buf_len)) != NULL)
                {
                  ssh_mprz_get_buf(buf, buf_len, &r->serial);
                  if (ssh_certdb_find(cm->db,
                                      SSH_CM_DATA_TYPE_CERTIFICATE,
                                      SSH_CM_KEY_TYPE_SERIAL_NO,
                                      buf, buf_len,
                                      &found) != SSH_CDBET_OK)
                    {
                      ssh_free(buf);
                      continue;
                    }
                  /* Free the buf. */
                  ssh_free(buf);
                }
              else
                {
                  SSH_CM_NOTEX(search, CERT_NOT_ADDED);
                  return 0;
                }
            }

          if (found == NULL)
            continue;

          /* Now we must skip all those certificates that do not match
             our issuers names. At the moment we match to ALL of the
             names of the issuer, and thus this is not perhaps the
             best way. */

          /* Revoke the certificates. */
          for (tmp = found->head; tmp; tmp = tmp->next)
            {
              SshCMCertificate cm_cert = tmp->entry->context;
              SshCertDBKey *issuer_names;

              /* Serial numbers did not match. */
              if (ssh_mprz_cmp(&r->serial, &cm_cert->cert->serial_number) != 0)
                continue;

              /* Has already been revoked! */
              if (!ssh_cm_trust_is_valid(cm_cert, search)
                  && ssh_cm_trust_is_root(cm_cert, search) == FALSE)
                continue;

              /* Get the issuer names of the certificate. */
              issuer_names = NULL;
              if (ssh_cm_key_set_from_cert(&issuer_names,
                                           SSH_CM_KEY_CLASS_ISSUER,
                                           cm_cert))
                {
                  if (!ssh_cm_key_match(issuer_names, ca->entry->names))
                    {
                      /* Not a match. */
                      ssh_certdb_key_free(issuer_names);
                      continue;
                    }
                  ssh_certdb_key_free(issuer_names);
                }
              else
                continue;

              /* Revoke this certificate */
              ssh_cm_cert_revoke(search, ca, cm_cert, r);
            }

          /* Free the list. */
          ssh_certdb_entry_list_free_all(cm->db, found);
          found = NULL;
        }

      /* Don't free it again later. */
      crl->revoked = NULL;
    }


  /* Now check the subject certificate against CRL. NOTE: here we
     assume the subject was indeed issued by ca. */
  if (subject)
    {
      SshCMRevokedStruct probe;

      if (!ssh_cm_trust_is_valid(subject, search)
          && ssh_cm_trust_is_root(subject, search) == FALSE)
        goto quitnow;

      ssh_mprz_init_set(&probe.serial, &subject->cert->serial_number);

      if ((handle = ssh_adt_get_handle_to_equal(cm_crl->revoked, &probe))
          != SSH_ADT_INVALID)
        {
          r = ssh_adt_get(cm_crl->revoked, handle);
          /* The subject is revoked? */
          if (ssh_ber_time_cmp(&r->revocation, &search->cur_time) > 0)
            {
              /* Don't revoke yet, instead make the needed adjustments
                 to the next check. */
              if (ssh_ber_time_cmp(&r->revocation, &ca->crl_recompute_after)
                  < 0)
                ssh_ber_time_set(&ca->crl_recompute_after, &r->revocation);

              /* Continue. */
              ssh_mprz_clear(&probe.serial);
              goto quitnow;
            }
          ssh_cm_cert_revoke(search, ca, subject, r);
        }
      ssh_mprz_clear(&probe.serial);
    }
 quitnow:
  return 1;
}


/* Asynchronous verification callbacks. */

typedef struct
{
  SshCMCrl          crl;
  SshCMCertificate  ca;
  SshCMContext      cm;
  SshCMSearchContext *search;
  unsigned int      issuer_id;
  unsigned int      subject_id;
} SshCMVerifyCrl;

typedef struct
{
  SshCMCertificate  cert;
  SshCMCertificate  ca;
  SshCMContext      cm;
  SshCMSearchContext *search;
  unsigned int      issuer_id;
  unsigned int      subject_id;
} SshCMVerifyCert;

/* Completion callback for asynchronous CRL validation. */

static void
cm_crl_verify_async(SshX509Status status, void *param)
{
  SshCMVerifyCrl *v_crl = param;
  SshCMContext    cm = v_crl->cm;
  SshCMSearchContext *search = v_crl->search;

  search->waiting -= 1;
  search->async_completed = TRUE;

  ssh_certdb_release_entry(cm->db, v_crl->ca->entry);
  ssh_certdb_release_entry(cm->db, v_crl->crl->entry);

  if (status == SSH_X509_OK)
    {
      /* Operation was a success. */
      v_crl->crl->trusted = TRUE;
      search->async_ok   = TRUE;

      /* Discard proved message data for the CRL */
      ssh_free(v_crl->crl->crl->pop.proved_message);
      v_crl->crl->crl->pop.proved_message = NULL;
      v_crl->crl->crl->pop.proved_message_len = 0;
    }
  else
    {
      /* Flag the CRL to be deleted. */
      search->async_ok   = FALSE;
      cm_failure_list_add(search, v_crl->issuer_id, v_crl->subject_id);

      /* Not a valid CRL, please remove. */
      SSH_DEBUG(SSH_D_NETFAULT,
                ("Reject: "
                 "Invalid signature on CRL validation."));
      SSH_CM_NOTEX(search, CRL_INVALID_SIGNATURE);
    }

  /* Free the temporary context. */
  memset(v_crl, 0, sizeof(*v_crl));
  ssh_free(v_crl);

  /* Set a timeout for the operation control. */
  search->cm->in_callback++;
  ssh_cm_operation_control(cm);
  search->cm->in_callback--;
}

void ssh_x509_pop_clear(SshX509Pop p);


void cm_cert_verify_async(SshX509Status status, void *param)
{
  SshCMVerifyCert *v_cert = param;
  SshCMContext   cm = v_cert->cm;
  SshCMSearchContext *search = v_cert->search;

  search->waiting -= 1;
  search->async_completed = TRUE;

  if (status == SSH_X509_OK)
    {
      /* Operation was a success. */
      ssh_cm_trust_mark_signature_ok(v_cert->cert, v_cert->ca, search);
      search->async_ok   = TRUE;

      if (v_cert->cert->self_issued && v_cert->ca == NULL)
        v_cert->cert->self_signed = 1;
    }
  else
    {
      SSH_DEBUG(SSH_D_NETFAULT,
                ("Reject: "
                 "Invalid signature on certificate validation."));
      SSH_CM_NOTEX(search, CERT_INVALID_SIGNATURE);

      search->async_ok   = FALSE;
      cm_failure_list_add(search, v_cert->issuer_id, v_cert->subject_id);
    }

  /* Remove references. */
  if (v_cert->ca != NULL)
    ssh_certdb_release_entry(cm->db, v_cert->ca->entry);
  ssh_certdb_release_entry(cm->db, v_cert->cert->entry);

  /* Free the temporary context. */
  memset(v_cert, 0, sizeof(*v_cert));
  ssh_free(v_cert);

  /* Set a timeout for the operation control.
   */
  search->cm->in_callback++;
  ssh_cm_operation_control(cm);
  search->cm->in_callback--;
}

void
cm_cert_db_entry_list_print(SshCertDBEntryList *list)
{
  SshCertDBEntryListNode entry;
  SshCMCertificate cert;
  SshCMCrl crl;

  for (entry = list->head; entry; entry = entry->next)
    {
      if (entry->entry->tag == SSH_CM_DATA_TYPE_CRL)
        {
          crl = entry->entry->context;
          SSH_DEBUG(SSH_D_MIDOK, ("%@",
                                  ssh_cm_render_crl, crl->crl));
        }
      else if (entry->entry->tag == SSH_CM_DATA_TYPE_CERTIFICATE)
        {
          cert = entry->entry->context;
          SSH_DEBUG(SSH_D_MIDOK, ("%@",
                                  ssh_cm_render_certificate, cert->cert));
        }
    }
}

/* Apply CRLs at 'list' to subject certificate. */
static SshCMStatus
cm_crl_apply_internal(SshCMSearchContext *search,
                      SshCMCertificate    ca,
                      SshCMCertificate    subject,
                      SshCertDBEntryList *list)
{
  SshCertDBEntryListNode entry, next;
  SshCMContext cm = search->cm;
  SshCMCrl cm_crl;
  SshX509ReasonFlags reasons = 0;
  int rv;

  SSH_DEBUG(SSH_D_MIDSTART,
            ("CRL applying to the local database."));

  if (list == NULL)
    return SSH_CM_STATUS_FAILURE;

  if (cm->db == NULL)
    ssh_fatal("error: local db has not been defined in crl apply!");

  ssh_cm_crl_initial_cert_transform(search, ca, subject);

  cm_crl = NULL;
  /* Find the latest of the CRL's.
     This should be sped up with some actual computations.
     Also those CRL's which have been found to be old could as well be
     thrown out. */
  for (entry = list->head; entry; entry = next)
    {
      next = entry->next;

      if (entry->entry->tag == SSH_CM_DATA_TYPE_CRL)
        {
          SshBerTimeStruct refetch_time;

          cm_crl = entry->entry->context;

          /* Check whether this CRL has been already checked and found
             to be invalid (or expired). */
          if (cm_crl->status_flags & SSH_CM_CRL_FLAG_SKIP)
            continue;

          /* Must not be older that max_crl_validity_secs */
          if (cm->config->max_crl_validity_secs)
            {
              ssh_ber_time_set(&refetch_time, &cm_crl->fetch_time);
              ssh_ber_time_add_secs(&refetch_time,
                                    cm->config->max_crl_validity_secs);
              if (ssh_ber_time_cmp(&refetch_time, &search->cur_time) < 0)
                {
                  SSH_DEBUG(SSH_D_MIDOK, ("Too old; max_crl_validity_secs"));
                  goto too_old;
                }
            }

          /* Next update has to be later than the time searched. */
          if (ssh_ber_time_available(&cm_crl->crl->next_update) &&
              ssh_ber_time_cmp(&cm_crl->crl->next_update,
                               &search->cur_time) < 0)
            {
              SSH_DEBUG(SSH_D_MIDOK, ("Too old; next_update"));

            too_old:
              /* Remove the CRL, too old for us. */
              cm_crl->status_flags |= SSH_CM_CRL_FLAG_SKIP;
#if 0
              ssh_certdb_entry_list_remove(cm->db, entry);
              ssh_certdb_remove_entry(cm->db, cm_crl->entry);
#endif
              SSH_CM_NOTEX(search, CRL_OLD);
              continue;
            }

          /* Check whether is trusted or not. */
          if (cm_crl->trusted == FALSE)
            {
              SshCMVerifyCrl *v_crl;
              unsigned int iid, sid;

              iid = ssh_cm_cert_get_cache_id(ca);
              sid = ssh_cm_crl_get_cache_id(cm_crl);
              if (cm_failure_list_member(search, iid, sid))
                {
                  SSH_CM_NOTEX(search, CRL_INVALID);
                  return SSH_CM_STATUS_FAILURE;
                }

              /* Build a verification context, with appropriately
                 referenced CA and CRL. */
              if ((v_crl = ssh_calloc(1, sizeof(*v_crl))) == NULL)
                {
                  SSH_CM_NOTEX(search, CRL_INVALID);
                  return SSH_CM_STATUS_FAILURE;
                }

              v_crl->crl = cm_crl;
              v_crl->ca  = ca;
              v_crl->cm  = cm;
              v_crl->search = search;
              v_crl->issuer_id = iid;
              v_crl->subject_id = sid;

              /* Clean the async parameters. */
              search->async_completed = FALSE;
              search->async_ok        = FALSE;

              /* Take CRL and CA references. */
              ssh_certdb_take_reference(ca->entry);
              ssh_certdb_take_reference(cm_crl->entry);

              /* Start the asynchronous verification. We will not
                 support aborting of this operation. This means that
                 the time control cannot kill asynchronous
                 verification operation, even if it takes a long
                 time. This is reasonable, as if the asynchronous
                 operation is progressing we can hope that the
                 underlying library timeouts if the method for
                 verification is e.g. removed. */

              /* This arranges v_crl to be freed. */
              search->waiting += 1;
              ssh_x509_crl_verify_async(cm_crl->crl,
                                        ca->cert->subject_pkey.public_key,
                                        cm_crl_verify_async,
                                        v_crl);

              if (!search->async_completed)
                {
                  SSH_DEBUG(SSH_D_MIDOK, ("CRL verification running..."));
                  return SSH_CM_STATUS_SEARCHING;
                }

              if (!search->async_ok)
                {
                  /* Observe that the list exists still at this point,
                     and we have removed the CRL entry. Thus remove
                     also the list entry. */
#if 0
                  ssh_certdb_entry_list_remove(cm->db, entry);
#endif
                  continue;
                }
            }

          /* Work through the CRL. At this point the CRL has been
             validated against the issuer.  */
          rv = cm_crl_revoke(search, cm_crl, ca, subject, &reasons);

          if (rv == 0)
            {
              cm_crl->status_flags |= SSH_CM_CRL_FLAG_SKIP;
              /* Not a valid CRL, please remove. */
#if 0
              ssh_certdb_entry_list_remove(cm->db, entry);
              ssh_certdb_remove_entry(cm->db, cm_crl->entry);
#endif
              SSH_CM_NOTEX(search, CRL_INVALID);
            }
        }
    }

  /* Check the reasons. */
  if (reasons == 0x80ff)
    {
      ssh_cm_crl_final_cert_transform(search, ca, subject, TRUE);
      return SSH_CM_STATUS_OK;
    }
  ssh_cm_crl_final_cert_transform(search, ca, subject, FALSE);

  /* Reason codes were not all available, cannot let this go. */
  SSH_CM_NOTEX(search, CRL_INVALID);
  if (cm_crl)
    cm_failure_list_add(search,
                        ssh_cm_cert_get_cache_id(ca),
                        ssh_cm_crl_get_cache_id(cm_crl));

  return SSH_CM_STATUS_FAILURE;
}

void ssh_cm_cert_list_clean_key_flags(SshCertDBEntryList *list)
{
  SshCertDBEntryListNode node;
  SshCertDBKey *key;

  for (node = list->head; node; node = node->next)
    for (key = node->entry->names; key; key = key->next)
      ;
}


SshCMStatus
cm_crl_find_and_apply(SshCMSearchContext *search,
                      SshCMCertificate ca,
                      SshCMCertificate subject,
                      SshCertDBKey *keys)
{
  SshCertDBEntryList *list, *clist;
  SshCMContext cm = search->cm;
  SshCMSearchConstraints constraints = search->end_cert;
  SshCMStatus rv;
  Boolean ca_referenced = FALSE;

  SSH_DEBUG(SSH_D_HIGHSTART,
            ("CMI: Find and apply CRL with ca: %@ and subject %@",
             ssh_cm_render_certificate, ca->cert,
             ssh_cm_render_certificate, subject->cert));

  /* Look up CRL from the local database. */
  rv = cm_search_local_dbs(search,
                           SSH_CM_DATA_TYPE_CRL, keys, SSH_CM_SEARCH_RULE_OR,
                           &list);
  switch (rv)
    {
    case SSH_CM_STATUS_OK:
    case SSH_CM_STATUS_NOT_FOUND:
      /* Found or not found from the local cache. We do not care now. */
      break;
    default:
      SSH_CM_NOTEX(search, CRL_NOT_FOUND);
      return rv;
    }

  while (1)
    {
      /* If not found and we are allowed to look from external databases,
         do so. */
      if (constraints->local.crl == FALSE && rv == SSH_CM_STATUS_NOT_FOUND)
        {
          rv = cm_search_dbs(search,
                             SSH_CM_DATA_TYPE_CRL, keys, SSH_CM_SEARCH_RULE_OR,
                             &list);
          switch (rv)
            {
            case SSH_CM_STATUS_OK:
              /* Rare but possible situation, found it synchronously. */
              break;
            case SSH_CM_STATUS_SEARCHING:
              return rv;
            case SSH_CM_STATUS_NOT_FOUND:
              SSH_CM_NOTEX(search, CRL_NOT_FOUND);
              break;
            default:
              SSH_CM_NOTEX(search, CRL_NOT_FOUND);
              return rv;
              break;
            }
        }
      if (rv == SSH_CM_STATUS_NOT_FOUND)
        break;

      /* Now apply this CRL. This validates the CRL, so it might be
         asyncronous. */
      rv = cm_crl_apply_internal(search, ca, subject, list);
      if (ca_referenced)
	{
	  ssh_certdb_release_entry(cm->db, ca->entry);
	  ca_referenced = FALSE;
	}

      switch (rv)
        {
        case SSH_CM_STATUS_OK:
          subject->not_checked_against_crl = FALSE;
          ssh_certdb_entry_list_free_all(cm->db, list);
          return SSH_CM_STATUS_OK;

        case SSH_CM_STATUS_SEARCHING:
          ssh_certdb_entry_list_free_all(cm->db, list);
          return SSH_CM_STATUS_SEARCHING;

        default:
          break;
        }

      /* CRL apply with given CA failed. Now check if we have other,
         certificates with the same subject. Also, the found
         certificates either must have been issued by current CA
         (e.g. are certificate issuers or CA key renewals), or they
         must be issuers of the current CA. */
      rv = cm_search_local_dbs(search,
                               SSH_CM_DATA_TYPE_CERTIFICATE,
                               keys, SSH_CM_SEARCH_RULE_OR, &clist);
      switch (rv)
        {
        case SSH_CM_STATUS_OK:
          {
            SshCMCertificate c;
            SshCMCrl l;
            Boolean found = FALSE;
            SshCertDBEntryListNode entry, crlentry, next;

            c = NULL;

            for (entry = clist->head; !found && entry; entry = next)
              {
                c = entry->entry->context;
                next = entry->next;

                if (ssh_cm_cert_get_cache_id(c) !=
                    ssh_cm_cert_get_cache_id(ca))
                  {
                    for (crlentry = list->head;
                         crlentry;
                         crlentry = crlentry->next)
                      {
                        l = crlentry->entry->context;
                        if (!cm_failure_list_member(search,
                                            ssh_cm_cert_get_cache_id(c),
                                            ssh_cm_crl_get_cache_id(l)))
                        {
                          found = TRUE;
			  ca_referenced = TRUE;
			  ssh_certdb_take_reference(c->entry);
			  break;
                        }
                      }
                  }
              }

            ssh_certdb_entry_list_free_all(cm->db, clist);
            if (found)
              {
                ca = c;
                SSH_DEBUG(SSH_D_HIGHOK,
                          ("Checking CRL with: %@",
                           ssh_cm_render_certificate, ca->cert));
                continue;
              }
            else
              break;
          }
        case SSH_CM_STATUS_SEARCHING:
          return rv;
        default:
          break;
        }

      SSH_DEBUG(SSH_D_NETFAULT, ("CRL apply failed directly."));
      ssh_certdb_entry_list_free_all(cm->db, list);
      rv = SSH_CM_STATUS_NOT_FOUND;
    }
  return SSH_CM_STATUS_NOT_FOUND;
}


/* Routine which verifies the path from ca to end certificate. */
SshCMStatus ssh_cm_verify_path(SshCMSearchContext *search,
                               SshCertDBEntryList *chosen)
{
  SshCertDBEntryListNode tmp, prev, pprev;
  SshCMContext cm = search->cm;
  SshCMCertificate ca_cert, subject_cert, crl_signer = NULL;
  SshX509Certificate cert;
  SshX509ExtCRLDistPoints cdps;
  SshBerTimeStruct not_before, not_after, cert_not_before, cert_not_after;
  Boolean critical;
  size_t path_length;
  SshX509CertExtType ext_type;
  Boolean subject_check_revocation;
  Boolean search_check_revocation;

  SshUInt32 policy_mapping, explicit_policy;
  SshUInt32 inhibit_policy_mapping;
  SshUInt32 inhibit_any_policy;

  SshCMPolicyTree valid_policy_tree;
  SshUInt32 depth, level;

  SshCMSearchConstraints constraints = search->end_cert;

  SSH_DEBUG(SSH_D_HIGHSTART, ("CMI: Path validation in process."));

  /* Before each verify path operation we need to clean up the flags
     of the keys. This makes the searching from different databases
     work. */
  ssh_cm_cert_list_clean_key_flags(chosen);

  if ((valid_policy_tree = ssh_cm_ptree_alloc()) == NULL)
    {
      SSH_DEBUG(SSH_D_NETFAULT, ("Reject: Out of memory"));
      SSH_CM_NOTEX(search, CERT_NOT_ADDED);
      return SSH_CM_STATUS_FAILURE;
    }

  /* Retry this function again. Useful when applied the CRL before. */
 retry:

  path_length = cm->config->max_path_length;
  ssh_ber_time_zero(&not_before);
  ssh_ber_time_zero(&not_after);

  subject_cert = ca_cert = NULL;

  if (constraints && !constraints->check_revocation)
    search_check_revocation = FALSE;
  else
    search_check_revocation = TRUE;

  /* Count */
  for (tmp = chosen->head, depth = 0; tmp; tmp = tmp->next, depth++);

  policy_mapping = constraints->policy_mapping;
  if (constraints->explicit_policy)
    explicit_policy = 0;
  else
    explicit_policy = depth + 1;

  if (constraints->inhibit_any_policy)
    inhibit_any_policy = 0;
  else
    inhibit_any_policy = depth + 1;

  inhibit_policy_mapping = constraints->inhibit_policy_mapping;
  if (constraints->inhibit_policy_mapping)
    policy_mapping = 0;
  else
    policy_mapping = depth + 1;

  cm_cert_db_entry_list_print(chosen);

  /* Run through all the certificates. */
  for (tmp = chosen->head, pprev = prev = NULL, level = 0;
       tmp;
       pprev = prev, prev = tmp, tmp = tmp->next, level++)
    {
      Boolean end_entity = FALSE;

      /* Path == 0 means that only a end user is allowed after the
         given issuer. */
      if (path_length == (size_t)-1)
        {
          SSH_DEBUG(SSH_D_NETFAULT, ("Reject: "
                                     "Path length was exceeded."));
          SSH_CM_NOTEX(search, PATH_LENGTH_REACHED);
          ssh_cm_ptree_free(valid_policy_tree);
          return SSH_CM_STATUS_NOT_VALID;
        }

      if (tmp->entry == NULL)
        ssh_fatal("error: certificate cache contains corrupted certificates.");

      /* Get the contexts. */
      if (tmp->entry->tag != SSH_CM_DATA_TYPE_CERTIFICATE)
        ssh_fatal("error: path verification expected a certificate.");

      /* Take the CM certificate. */
      subject_cert = tmp->entry->context;

      /* Keep the acting CA department updated. */
      if (prev)
        ca_cert = prev->entry->context;
      else
        ca_cert = NULL;

      crl_signer = NULL;

      if (tmp->next)
        subject_cert->acting_ca = TRUE;
      else
        end_entity = TRUE;

      cert = subject_cert->cert;

      if (ca_cert)
        {
          size_t cert_path_length;
          Boolean ca;

          if (!ssh_x509_cert_get_basic_constraints(ca_cert->cert,
                                                   &cert_path_length,
                                                   &ca,
                                                   &critical))
            ca = FALSE;

          if (pprev &&
              (ca_cert->self_issued && !ca_cert->self_signed) &&
              !ca)
            {
              /* If we have issued-to-self certificate that is not a
                 CA in the middle of the path, consider is as a CRL
                 issuer. */
              crl_signer = ca_cert;
              ca_cert = pprev->entry->context;
            }
          else
            {
              if (!ca)
                {
                  SSH_DEBUG(SSH_D_NETFAULT,
                            ("Reject: "
                             "Acting CA is missing basicConstraints/CA"));

                  SSH_CM_NOTEX(search, CERT_CA_INVALID);
                  ssh_cm_ptree_free(valid_policy_tree);
                  cm_failure_list_add(search,
                                      ssh_cm_cert_get_cache_id(ca_cert),
                                      ssh_cm_cert_get_cache_id(subject_cert));
                  return SSH_CM_STATUS_NOT_VALID;
                }
            }
        }

      SSH_DEBUG(SSH_D_MIDOK,
                ("Considering %s serial: %@",
                 !ca_cert ? "CA" : "UserCertificate or SubCA",
                 ssh_cm_render_mp, &subject_cert->cert->serial_number));

      /* First and foremost check that the certificate isn't yet
         revoked. This information is scanned in the search phase too, so
         we might aswell skip this. However, for now lets be on the safe
         side. */
      if (!ssh_cm_trust_is_valid(subject_cert, search) &&
          ssh_cm_trust_is_root(subject_cert, search) != TRUE &&
          subject_cert->status == SSH_CM_VS_REVOKED)
        {
          SSH_DEBUG(SSH_D_NETFAULT,
                    ("Reject: "
                     "A revoked certificate encountered."));
          SSH_CM_NOTEX(search, CERT_REVOKED);
          ssh_cm_ptree_free(valid_policy_tree);
          return SSH_CM_STATUS_NOT_VALID;
        }

      if (!ssh_cm_trust_is_valid(subject_cert, search) &&
          ssh_cm_trust_is_root(subject_cert, search) != TRUE &&
          subject_cert->status == SSH_CM_VS_HOLD)
        {
          SSH_DEBUG(SSH_D_NETFAULT,
                    ("Reject: "
                     "A suspended certificate encountered."));
          SSH_CM_NOTEX(search, CERT_REVOKED);

          /* When certificate is marked with HOLD, it's crl_recompute_after
             it set from the ca that suspended it. Recheck hold status
             regularly to ensure timely reactivation. */

          /* XXX check that suspended subca's crl is never checked as
             otherwise crl_recompute_after gets reset and reactivation
             won't be noticed until subca's crl expires. */
          SSH_DEBUG(SSH_D_MIDOK,
                    ("Next CRL recomputation at '%@'.",
                     ssh_ber_time_render, &subject_cert->crl_recompute_after));

          if (ssh_ber_time_cmp(&subject_cert->crl_recompute_after,
                               &search->cur_time) < 0)
            subject_cert->not_checked_against_crl = TRUE;
        }

      /* Now we can try something more computational. */

      /* Check the signature, if not already checked. */
      if (!ssh_cm_trust_is_root(subject_cert, search))
        {
          /* Get the previous certificate (e.g. issuer for this). */
          if (prev)
            {
              unsigned int iid, sid;
              Boolean already_failed = FALSE;

              iid = ssh_cm_cert_get_cache_id(ca_cert);
              sid = ssh_cm_cert_get_cache_id(subject_cert);

              /* Check signature unless check has already failed for
                 this search and subject is not not trusted or issuer
                 of trust does not match the current CA. */
              already_failed = cm_failure_list_member(search, iid, sid);

              if (!already_failed
                  && (!ssh_cm_trust_in_signature_predicate(subject_cert,
                                                           search)
                      || subject_cert->trusted.trusted_issuer_id
                      != ca_cert->entry->id))
                {
                  SshCMVerifyCert *v_cert = ssh_calloc(1, sizeof(*v_cert));

                  if (v_cert)
                    {
                      v_cert->cert = subject_cert;
                      v_cert->ca   = ca_cert;
                      v_cert->cm   = cm;
                      v_cert->search = search;
                      v_cert->issuer_id = iid;
                      v_cert->subject_id = sid;

                      /* Async set up. */
                      search->async_completed = FALSE;
                      search->async_ok        = FALSE;

                      /* Reference both parties. */
                      ssh_certdb_take_reference(ca_cert->entry);
                      ssh_certdb_take_reference(subject_cert->entry);

                      /* Start the asynchronous operation. */
                      search->waiting += 1;
                      ssh_x509_cert_verify_async(subject_cert->cert,
                                                 ca_cert->cert->
                                                 subject_pkey.public_key,
                                                 cm_cert_verify_async,
                                                 v_cert);

                      if (!search->async_completed)
                        {
                          SSH_DEBUG(SSH_D_LOWOK, ("Verifying asyncronously"));
                          ssh_cm_ptree_free(valid_policy_tree);
                          return SSH_CM_STATUS_SEARCHING;
                        }

                      if (!search->async_ok)
                        {
                          ssh_cm_ptree_free(valid_policy_tree);
                          return SSH_CM_STATUS_NOT_VALID;
                        }
                    }
                  else
                    {
                      SSH_DEBUG(SSH_D_NETFAULT,
                                ("Reject: "
                                 "No space for certificate validation."));
                      SSH_CM_NOTEX(search, CERT_NOT_ADDED);
                      ssh_cm_ptree_free(valid_policy_tree);
                      return SSH_CM_STATUS_FAILURE;
                    }
                }
            }
          else
            {
              /* Check first that the certificate actually is or
                 has been valid. */
              if (!ssh_cm_trust_check(subject_cert, NULL, search))
                {
                  SSH_DEBUG(SSH_D_NETFAULT, ("Reject: "
                                "Certificate is not trusted currently."));
                  SSH_CM_NOTEX(search, CERT_CA_INVALID);
                  ssh_cm_ptree_free(valid_policy_tree);
                  return SSH_CM_STATUS_NOT_VALID;
                }
            }

          /* Check that the certificates signature is valid. */
          if (subject_cert->trusted.trusted_signature == FALSE)
            {
              /* We cannot continue without valid signature. */
              SSH_DEBUG(SSH_D_NETFAULT, ("Reject: " "Invalid signature."));
              SSH_CM_NOTEX(search, CERT_INVALID_SIGNATURE);
              ssh_cm_ptree_free(valid_policy_tree);
              return SSH_CM_STATUS_NOT_VALID;
            }
        }
      else
        {
          if (ssh_cm_trust_in_signature_predicate(subject_cert,
                                                  search) == FALSE &&
              subject_cert->self_issued)
            {
              SshCMVerifyCert *v_cert = ssh_calloc(1, sizeof(*v_cert));

              if (v_cert)
                {
                  v_cert->cert = subject_cert;
                  v_cert->ca   = NULL;
                  v_cert->cm   = cm;
                  v_cert->search = search;

                  /* Async set up. */
                  search->async_completed = FALSE;
                  search->async_ok        = FALSE;

                  /* Reference both parties. */
                  ssh_certdb_take_reference(subject_cert->entry);

                  /* Start the asynchronous operation. */
                  search->waiting += 1;
                  ssh_x509_cert_verify_async(subject_cert->cert,
                                             subject_cert->cert->
                                             subject_pkey.public_key,
                                             cm_cert_verify_async,
                                             v_cert);

                  if (!search->async_completed)
                    {
                      SSH_DEBUG(SSH_D_LOWOK, ("Verifying asyncronously"));
                      ssh_cm_ptree_free(valid_policy_tree);
                      return SSH_CM_STATUS_SEARCHING;
                    }

                  if (!search->async_ok)
                    {
                      ssh_cm_ptree_free(valid_policy_tree);
                      return SSH_CM_STATUS_NOT_VALID;
                    }
                }
              else
                {
                  SSH_DEBUG(SSH_D_NETFAULT,
                            ("Reject: "
                             "No space for certificate validation."));
                  SSH_CM_NOTEX(search, CERT_NOT_ADDED);
                  ssh_cm_ptree_free(valid_policy_tree);
                  return SSH_CM_STATUS_FAILURE;
                }
            }
          else
            {
              /* This is done only for non-selfsigned roots. */
              ssh_cm_trust_mark_signature_ok(subject_cert,
                                             subject_cert,
                                             search);
            }
        }

      crl_signer = crl_signer ? crl_signer : ca_cert;

      /* If not an end certificate, and the current one has been defined
         to be a CRL issuer then try to find the actual CRL (and apply it). */
      if (crl_signer)
        {
          subject_check_revocation = TRUE;

          if ((subject_cert->self_issued && !subject_cert->self_signed) &&
              ssh_cm_trust_is_valid(crl_signer, search))
            {
              SSH_DEBUG(SSH_D_MIDOK,
                        ("Self issued CA key updated SUBCA of trusted issuer. "
                         "Not checking revocation for this cert, as old key "
                         "may no longer issues CRL's"));
              subject_check_revocation = FALSE;
              path_length++;
            }

          /* Test for CRL. */
          if (search_check_revocation &&
              subject_cert->not_checked_against_crl == FALSE &&
              ssh_ber_time_cmp(&crl_signer->crl_recompute_after,
                               &search->cur_time) > 0)
            {
              /* The revocation information has been verified recently
                 enough, thus we may happily continue to computing the
                 validity times. */
              SSH_DEBUG(SSH_D_MIDOK,
                        ("Certificate %@ has been checked against CRL.",
                         ssh_cm_render_mp,
                         &subject_cert->cert->serial_number));
              SSH_DEBUG(SSH_D_MIDOK,
                        ("CRL not rechecked until '%@'.",
                         ssh_ber_time_render,
                         &crl_signer->crl_recompute_after));
              subject_check_revocation = FALSE;
            }

#ifdef SSHDIST_VALIDATOR_OCSP
          /* Test for OCSP. */
          if (search_check_revocation &&
              subject_cert->not_checked_against_crl == FALSE &&
              ssh_ber_time_available(&subject_cert->ocsp_valid_not_after) &&
              ssh_ber_time_cmp(&subject_cert->ocsp_valid_not_after,
                               &search->cur_time) > 0)
            {
              /* The revocation information has been verified recently
                 enough, thus we may happily continue to computing the
                 validity times. */
              SSH_DEBUG(SSH_D_MIDOK,
                        ("Certificate %@ has been checked against OCSP.",
                         ssh_cm_render_mp,
                         &subject_cert->cert->serial_number));
              SSH_DEBUG(SSH_D_MIDOK,
                        ("OCSP not rechecked until '%@'.",
                         ssh_ber_time_render,
                         &subject_cert->ocsp_valid_not_after));
              subject_check_revocation = FALSE;
            }
#endif /* SSHDIST_VALIDATOR_OCSP */

          if (search_check_revocation && subject_check_revocation)
            {
#ifdef SSHDIST_VALIDATOR_OCSP
              /* Check the validity of the CRL currently in use. */

              /* Check also whether OCSP is used at all. */
              if (constraints->ocsp_mode != SSH_CM_OCSP_NO_OCSP &&
                  !(end_entity &&
                    constraints->ocsp_mode == SSH_CM_OCSP_NO_END_ENTITY_OCSP))
                {
                  /* Check the certificate status using OCSP. */
                  if (!constraints->local.crl)
                    {
                      switch (ssh_cm_ocsp_check_status(search,
                                                       subject_cert,
                                                       crl_signer))
                        {
                        case SSH_CM_STATUS_SEARCHING:
                          SSH_DEBUG(SSH_D_HIGHOK,
                                    ("Searching from an OCSP server."));
                          ssh_cm_ptree_free(valid_policy_tree);
                          return SSH_CM_STATUS_SEARCHING;
                        default:
                          SSH_DEBUG(SSH_D_NETFAULT,
                                    ("OCSP based validation did not succeed, "
                                     "attempting other means."));
                          /* Check if allowed to continue. */
                          if (constraints->ocsp_mode !=
                              SSH_CM_OCSP_CRL_AFTER_OCSP)
                            {
                              ssh_cm_ptree_free(valid_policy_tree);
                              return SSH_CM_STATUS_NOT_FOUND;
                            }
                          break;
                        }
                    }
                }
              else
                {
                  SSH_DEBUG(SSH_D_MIDOK,
                            ("No OCSP check for: %@",
                             ssh_cm_render_certificate, subject_cert->cert));
                }
#endif /* SSHDIST_VALIDATOR_OCSP */

              if ((crl_signer->crl_issuer == FALSE) ||
                  (subject_cert->crl_user == FALSE))
                {
                  /* Seems to be in order. */
                  SSH_DEBUG(SSH_D_MIDOK,
                            ("CA does not issue CRL's or this certificate is "
                             "requested to ignore CRL's."));
                }
              else
                {
                  SshCertDBKey *key = NULL, *cdpkey = NULL;
                  SshCMStatus rv = SSH_CM_STATUS_FAILURE;

#ifdef SSHDIST_VALIDATOR_OCSP
                  if (constraints->ocsp_mode == SSH_CM_OCSP_OCSP_ONLY)
                    {
                      SSH_DEBUG(SSH_D_MIDOK,
                                ("No CRL check for: %@",
                                 ssh_cm_render_certificate,
                                 subject_cert->cert));
                      ssh_cm_ptree_free(valid_policy_tree);
                      return SSH_CM_STATUS_NOT_FOUND;
                    }
#endif /* SSHDIST_VALIDATOR_OCSP */

                  SSH_DEBUG(SSH_D_MIDOK, ("Searching for a CRL."));


                  /* Check if the subject has an explicit distribution
                     point mentioned. That will be used if available. */
                  cdps = NULL;
                  if (ssh_x509_cert_get_crl_dist_points(subject_cert->cert,
                                                        &cdps,
                                                        &critical))
                    {
                      SSH_DEBUG(5, ("CDP is available."));

                      for (; cdps; cdps = cdps->next)
                        {
                          /* Build up a name for searching. */
                          Boolean free_full_name = FALSE;
                          SshX509Name full_name;

                          if (cdps->full_name)
                            {
                              full_name = cdps->full_name;
                            }
                          else if (cdps->dn_relative_to_issuer)
                            {
                              full_name =
                                cm_dp_make_full_name(
                                             subject_cert->cert->issuer_name,
                                             cdps->dn_relative_to_issuer);
                              free_full_name = TRUE;
                            }
                          else
                            {
                              full_name = NULL;
                            }
                          if (full_name)
                            {
                              ssh_cm_key_convert_from_x509_name(&cdpkey,
                                                                full_name);
                              if (free_full_name)
                                ssh_x509_name_free(full_name);
                            }
#if 0
                          if (cdps->crl_issuer)
                            {
                              ssh_cm_key_convert_from_x509_name(&cdpkey,
                                                                cdps->
                                                                crl_issuer);
                            }
#endif
                        }
                    }

                  if (prev->entry->names)
                    {
                      SSH_DEBUG(SSH_D_HIGHSTART,
                                ("CMI: Looking for CRL by the issuer name"));
                      ssh_cm_key_push_keys(&key, prev->entry->names);
                      /* Make sure the CDP is pushed last, so it will
                         get searched first. */
                      ssh_cm_key_push_keys(&key, cdpkey);
                      ssh_certdb_key_free(cdpkey);
                    }
                  else
                    key = cdpkey;

                  switch (cm_crl_find_and_apply(search,
                                                crl_signer, subject_cert,
                                                key))
                    {
                    case SSH_CM_STATUS_OK:
                      ssh_certdb_key_free(key);
                      goto retry;
                      break;
                    case SSH_CM_STATUS_NOT_FOUND:
                      ssh_certdb_key_free(key);
                      break;
                    case SSH_CM_STATUS_SEARCHING:
                      ssh_certdb_key_free(key);
                      ssh_cm_ptree_free(valid_policy_tree);
                      return SSH_CM_STATUS_SEARCHING;
                      break;
                    default:
                      /* Error. */
                      ssh_certdb_key_free(key);
                      ssh_cm_ptree_free(valid_policy_tree);
                      return rv;
                      break;
                    }
                  /* All tries failed and nothing found! Too bad. */
                  ssh_cm_ptree_free(valid_policy_tree);
                  return SSH_CM_STATUS_NOT_VALID;
                }
            }
        }

      if (search_check_revocation &&
          !ssh_cm_trust_is_valid(subject_cert, search))
        {
          /* The certificate status is not yet resolved to OK. We
             cannot trust it as this implies that there is not
             enough revocation data to rigorously prove that it
             has not been revoked. Thus we fail at this point.

             This is the first place where all revocation information
             should be available, as CRLs and possibly OCSP has been
             now checked.
          */
          SSH_DEBUG(SSH_D_NETFAULT,
                    ("Reject: Certificate status is unknown."));
          SSH_CM_NOTEX(search, CERT_INVALID);
          ssh_cm_ptree_free(valid_policy_tree);
          return SSH_CM_STATUS_NOT_VALID;
        }

      /* Dates */
      ssh_ber_time_zero(&cert_not_before);
      ssh_ber_time_zero(&cert_not_after);

      if (ssh_x509_cert_get_validity(subject_cert->cert,
                                     &cert_not_before,
                                     &cert_not_after) == FALSE)
        {
          SSH_DEBUG(SSH_D_NETFAULT,
                    ("Certificate validity information unavailable."));
          SSH_CM_NOTEX(search, CERT_NOT_IN_INTERVAL);

          /* Should really remove this certificate from the
             search, but sadly its too difficult to do... at the
             moment anyway. */
          ssh_cm_ptree_free(valid_policy_tree);
          return SSH_CM_STATUS_NOT_VALID;
        }

      if (!ssh_ber_time_available(&not_before) &&
          !ssh_ber_time_available(&not_after))
        {
          ssh_ber_time_set(&not_before, &cert_not_before);
          ssh_ber_time_set(&not_after,  &cert_not_after);
        }

      /* Compute intersection between the certificate validity times and
         the global times. */
      if (ssh_ber_time_cmp(&not_before, &cert_not_before) < 0)
        ssh_ber_time_set(&not_before, &cert_not_before);
      if (ssh_ber_time_cmp(&not_after, &cert_not_after) > 0)
        ssh_ber_time_set(&not_after, &cert_not_after);

      /* Clip to the searching validity times. */
      if (ssh_ber_time_cmp(&not_before, &search->valid_time_start) < 0)
        ssh_ber_time_set(&not_before, &search->valid_time_start);
      if (ssh_ber_time_cmp(&not_after, &search->valid_time_end) > 0)
        ssh_ber_time_set(&not_after, &search->valid_time_end);

#ifdef SSHDIST_VALIDATOR_OCSP
      /* Apply OCSP time restrictions if available. */
      if (ssh_ber_time_available(&subject_cert->ocsp_valid_not_before))
        if (ssh_ber_time_cmp(&not_before,
                             &subject_cert->ocsp_valid_not_before) < 0)
          ssh_ber_time_set(&not_after,
                           &subject_cert->ocsp_valid_not_before);
      if (ssh_ber_time_available(&subject_cert->ocsp_valid_not_after))
        if (ssh_ber_time_cmp(&not_after,
                             &subject_cert->ocsp_valid_not_after) > 0)
          ssh_ber_time_set(&not_after,
                           &subject_cert->ocsp_valid_not_after);
#endif /* SSHDIST_VALIDATOR_OCSP */

      /* CRL time restriction. */
      if (crl_signer &&
          ssh_ber_time_available(&crl_signer->crl_recompute_after) &&
          search_check_revocation == TRUE &&
          subject_cert->crl_user == TRUE)
        if (ssh_ber_time_cmp(&not_after,
                             &crl_signer->crl_recompute_after) > 0)
          ssh_ber_time_set(&not_after,
                           &crl_signer->crl_recompute_after);

      SSH_DEBUG(SSH_D_MIDOK,
                ("Validity: not before '%@' not after '%@'",
                 ssh_ber_time_render, &not_before,
                 ssh_ber_time_render, &not_after));

      /* Check that the validity times do not cross (e.g. become
         invalid). */
      if (ssh_ber_time_cmp(&not_before, &not_after) > 0)
        {
          SSH_DEBUG(SSH_D_NETFAULT,
                    ("Reject: "
                     "Validity interval impossible."));

          SSH_CM_NOTEX(search, INTERVAL_NOT_VALID);
          ssh_cm_ptree_free(valid_policy_tree);
          return SSH_CM_STATUS_NOT_VALID;
        }

      /* Following code apparently modifies the validity times of the
         CM certificate. */
      ssh_cm_trust_update_validity(subject_cert,
                                   crl_signer,
                                   &not_before, &not_after, search);

      /*******************************************************/
      /* PKIX specific checks */

      /* Policy information, constraints and mapping processing;
         initialize for certificate at current level. */
      if (level > 0)
        ssh_cm_policy_init(subject_cert,
                           &valid_policy_tree, depth, level,
                           &policy_mapping, &inhibit_policy_mapping,
                           &inhibit_any_policy, &explicit_policy);

      /* Handle critical extensions. */
      for (ext_type = 0; ext_type < SSH_X509_EXT_MAX; ext_type++)
        {
          /* Check if the extension is available and critical. */
          if (!ssh_x509_cert_ext_available(subject_cert->cert,
                                           ext_type,
                                           &critical))
            continue;

          /* Now handle the extensions. */
          switch (ext_type)
            {
              /* The basic constraints. */
            case SSH_X509_EXT_BASIC_CNST:
              {
                size_t cert_path_length;
                Boolean ca;

                /* Handle the PKIX certificate parameters. */
                if (ssh_x509_cert_get_basic_constraints(subject_cert->cert,
                                                        &cert_path_length,
                                                        &ca,
                                                        &critical) == TRUE)
                  {
                    /* Strict check for validity of the CA. */
                    if (subject_cert->acting_ca && ca == FALSE)
                      {
                        SSH_DEBUG(SSH_D_NETFAULT,
                                  ("Reject: "
                                   "Acting CA is not really a CA."));
                        SSH_CM_NOTEX(search, CERT_CA_INVALID);
                        ssh_cm_ptree_free(valid_policy_tree);
                        return SSH_CM_STATUS_NOT_VALID;
                      }

                    if (cert_path_length != SSH_X509_MAX_PATH_LEN)
                      {
                        cert_path_length++;

                        if (path_length > cert_path_length ||
                            path_length == SSH_X509_MAX_PATH_LEN)
                          path_length = cert_path_length;
                      }
                  }
                else
                  {
                    SSH_DEBUG(SSH_D_NETFAULT,
                              ("Reject: "
                               "Basic constraints extension unavailable."));
                    SSH_CM_NOTEX(search, CERT_DECODE_FAILED);
                    ssh_cm_ptree_free(valid_policy_tree);
                    return SSH_CM_STATUS_NOT_VALID;
                  }
              }
              break;

            case SSH_X509_EXT_KEY_USAGE:
              {
                SshX509UsageFlags flags;

                if (ssh_x509_cert_get_key_usage(subject_cert->cert,
                                                &flags, &critical))
                  {
                    if (subject_cert->acting_ca &&
                        flags != 0 &&
                        (flags & SSH_X509_UF_KEY_CERT_SIGN) == 0)
                      {
                        SSH_DEBUG(SSH_D_NETFAULT,
                                  ("Reject: "
                                   "Acting CA is not allowed to sign."));
                        SSH_CM_NOTEX(search, CERT_CA_INVALID);
                        ssh_cm_ptree_free(valid_policy_tree);
                        return SSH_CM_STATUS_NOT_VALID;
                      }
                  }
                else
                  {
                    SSH_DEBUG(SSH_D_NETFAULT,
                              ("Reject: "
                               "Key usage extensions unavailable."));
                    SSH_CM_NOTEX(search, CERT_DECODE_FAILED);
                    ssh_cm_ptree_free(valid_policy_tree);
                    return SSH_CM_STATUS_NOT_VALID;
                  }
              }
              break;

            case SSH_X509_EXT_CERT_POLICIES:
            case SSH_X509_EXT_POLICY_MAPPINGS:
            case SSH_X509_EXT_POLICY_CNST:
            case SSH_X509_EXT_INHIBIT_ANY_POLICY:
              /* Nothing here */
              break;

            case SSH_X509_EXT_NAME_CNST:
              /* Checks need to be written. */

              /* Basically: if either the permitted or excluded
                 subtrees is present then compute the union of the
                 current ones. Then check all the names of the
                 certificates after that they admit these constraints.

                 This implementation may need following subroutines.

                 (a) code to handle the subtree checks (and matching
                 the wildcards)
                 (b) computing the union etc.
                 (c) matching all the name types (which is a bit
                 awful exercise).
              */

              /* For now either let this pass, or return an error. */
              SSH_DEBUG(SSH_D_NETFAULT,
                        ("WARNING: IGNORED: "
                         "permitted and excluded trees (name constraints)."));
              break;

              /* Ignorable extensions. */
            case SSH_X509_EXT_AUTH_KEY_ID:
            case SSH_X509_EXT_SUBJECT_KEY_ID:
            case SSH_X509_EXT_SUBJECT_ALT_NAME:
            case SSH_X509_EXT_ISSUER_ALT_NAME:
            case SSH_X509_EXT_CRL_DIST_POINTS:
            case SSH_X509_EXT_SUBJECT_DIR_ATTR:
            case SSH_X509_EXT_AUTH_INFO_ACCESS:
            case SSH_X509_EXT_EXT_KEY_USAGE:
              break;

            default:
              if (critical)
                {
                  SSH_DEBUG(SSH_D_NETFAULT,
                            ("Reject: "
                             "Unknown critical extension found."));
                  SSH_CM_NOTEX(search, CERT_CRITICAL_EXT);
                  ssh_cm_ptree_free(valid_policy_tree);
                  return SSH_CM_STATUS_NOT_VALID;
                }
            }
        }

      /* Handle the fixed path length case. E.g. we can override the
         certificate if we want to. Usually we don't.  */
      if (path_length > subject_cert->trusted.path_length &&
          subject_cert->trusted.path_length != (size_t)-1)
        path_length = subject_cert->trusted.path_length;

      /* Update the trust computation date. */
      ssh_cm_trust_computed(subject_cert, search);

      if (ssh_cm_trust_check(subject_cert, ca_cert, search) == FALSE)
        {
          /* Something happened and the certificate is not
             valid. However, we require that all certificates in the
             chain are valid, thus we return an error here. */
          SSH_DEBUG(SSH_D_FAIL,
                    ("Reject: "
                     "Cert was not marked trusted in finalization."));
          SSH_CM_NOTEX(search, CERT_INVALID);
          ssh_cm_ptree_free(valid_policy_tree);
          return SSH_CM_STATUS_NOT_VALID;
        }

      /* Prepare for next certificate */
      if (level > 0)
        {
          if (!ssh_cm_policy_prepare(subject_cert,
                                     &valid_policy_tree, depth, level,
                                     &policy_mapping, &inhibit_policy_mapping,
                                     &inhibit_any_policy,
                                     &explicit_policy))
            {
              SSH_DEBUG(SSH_D_FAIL,
                        ("Reject: "
                         "Invalid policy when preparing next certificate"));
              SSH_CM_NOTEX(search, INVALID_POLICY);
              ssh_cm_ptree_free(valid_policy_tree);
              return SSH_CM_STATUS_NOT_VALID;
            }
        }

      /* Reduce the path length by one. */
      path_length--;
    }

  if (ssh_cm_policy_wrapup(subject_cert,
                           &valid_policy_tree, depth, level - 1,
                           constraints->user_initial_policy_set,
                           constraints->user_initial_policy_set_size,
                           &policy_mapping, &inhibit_policy_mapping,
                           &inhibit_any_policy,
                           &explicit_policy))
    {
      ssh_cm_ptree_free(valid_policy_tree);
      return SSH_CM_STATUS_OK;
    }
  else
    {
      SSH_CM_NOTEX(search, INVALID_POLICY);
      ssh_cm_ptree_free(valid_policy_tree);
      return SSH_CM_STATUS_NOT_VALID;
    }
}

/* Simple stack. */
typedef struct SshDStackRec
{
  struct SshDStackRec *next;
  void *data;
} *SshDStack, SshDStackStruct;

static void *ssh_dstack_pop(SshDStack *stack)
{
  void *data;
  SshDStack next;

  if (stack == NULL)
    return NULL;

  if (*stack != NULL)
    {
      data = (*stack)->data;
      next = (*stack)->next;
      ssh_free(*stack);
      *stack = next;
      return data;
    }
  return NULL;
}

static void ssh_dstack_push(SshDStack *stack, void *data)
{
  SshDStack node;

  if (stack == NULL)
    return;

  if ((node = ssh_malloc(sizeof(*node))) != NULL)
    {
      node->data = data;
      node->next = *stack;
      *stack = node;
    }
}

static Boolean ssh_dstack_exists(SshDStack *stack)
{
  if (stack == NULL)
    return FALSE;
  if (*stack == NULL)
    return FALSE;
  return TRUE;
}

/* The following function is the heart of all these things. It has
   been written to be restartable, thus is a bit hard to follow. */
SshCMStatus ssh_cm_find_internal(SshCMSearchContext *search)
{
  SshCMContext           cm     = search->cm;
  SshCMSearchConstraints constraints = search->end_cert;
  SshCertDBKey *keys;
  Boolean       keys_allocated;
  /* The chosen list. */
  SshCertDBEntryList *chosen, *comb;
  SshCertDBEntry     *entry;
  SshCertDBEntryList *found;
  /* The possible stack of lists. */
  SshDStack possible;
  unsigned int session_id;
  /* Assume that chosen list should be freed when leaving. */
  Boolean should_chosen_be_freed = TRUE;
  /* A flag for termination of an search. Assuming that no such thing
     happens. */
  Boolean search_terminated = FALSE;
  /* Make the search for the end user slightly different. */
  Boolean for_first, group_mode_on;
  SshCMStatus rv = SSH_CM_STATUS_FAILURE;
  /* The configured maximum path length allowed for any search within our
     library. (Sometimes longer chains are be allowed, but you should not
     count on that.) */
  size_t path_length = 0;

  SSH_DEBUG(SSH_D_HIGHOK, ("Certificate searching (re)started."));

  keys = NULL;
  keys_allocated = FALSE;

  if (search->status != SSH_CM_STATUS_OK)
    {
      SSH_DEBUG(SSH_D_NETFAULT, ("Killed due error before CA searches."));
      SSH_DEBUG(SSH_D_HIGHOK,   ("Notes: %@",
                                 ssh_cm_render_state, search->state));

      /* Call the application callback and tell it that the operation
         has now expired and will be aborted. */
      cm_search_callback(search, search->status, NULL);

      /* Search has terminated. Thus handle the next one is exists. */
      search->terminated = TRUE;
      return ssh_cm_operation_control(cm);
    }

  /* Handle the case of failure by too many restarts. */
  if (search->restarts > cm->config->max_restarts)
    {
      SSH_DEBUG(SSH_D_NETFAULT, ("Killed due too many restarts."));
      SSH_DEBUG(SSH_D_HIGHOK,   ("Notes: %@",
                                 ssh_cm_render_state, search->state));

      /* Call the application callback and tell it that the operation
         has now expired and will be aborted. */
      cm_search_callback(search, SSH_CM_STATUS_OPERATION_EXPIRED, NULL);

      /* Search has terminated. Thus handle the next one is exists. */
      search->terminated = TRUE;
      return ssh_cm_operation_control(cm);
    }

  /* Set up either for the first time, or restart. */
  if (search->restarts == 0)
    /* Start from total blankness. */
    ;
  else
    {
      /* Restart from where we were left before. */

      /* Nothing yet, because we just restart everything. However, we
         could store small number of things. But that would need
         processing which we probably don't want to do? */
    }

  /* Compute times before starting the loop. */
  if (ssh_cm_compute_validity_times(search) != SSH_CM_STATUS_OK)
    {
      SSH_DEBUG(SSH_D_NETFAULT, ("Time information incorrect."));
      SSH_DEBUG(SSH_D_HIGHOK,   ("Notes: %@",
                                 ssh_cm_render_state, search->state));

      SSH_CM_NOTEX(search, TIMES_UNAVAILABLE);

      cm_search_callback(search, SSH_CM_STATUS_FAILURE, NULL);

      /* Search has terminated. Thus handle the next one is exists. */
      search->terminated = TRUE;
      return ssh_cm_operation_control(cm);
    }

  SSH_DEBUG(SSH_D_HIGHOK,
            ("Current time for the search: '%@'.",
             ssh_ber_time_render, &search->cur_time));

  /* Initialize the possible. We should not try to store the possible
     stack of lists thing in any way. */
  possible = NULL;

  chosen   = ssh_certdb_entry_list_allocate(cm->db);
  comb     = ssh_certdb_entry_list_allocate(cm->db);

  if (chosen == NULL || comb == NULL)
    {
      SSH_CM_NOTEX(search, DB_METHOD_FAILED);
      found = NULL;
      goto failure_in_search;
    }

  /* Get a new session identifier. We will update every certificate we
     touch. */
  session_id = cm->session_id;
  cm->session_id++;
  SSH_DEBUG(SSH_D_LOWOK,
            ("Allocating new cmi session id %d and restarting", session_id));

  /* Update the restarts counter. */
  search->restarts++;

  /* Start travelling the keys. */
  keys = constraints->keys;
  keys_allocated = FALSE;

  /* Reset the extra information within the keys now. */
  ssh_certdb_key_clean_extra(keys);

  /* Initialize the found list. */
  found         = NULL;
  for_first     = TRUE;
  group_mode_on = FALSE;

  /* Main loop, which hopefully stops. */
  while (1)
    {
      /* First look for the certificate from the local database. */
      rv = cm_search_local_dbs(search,
                               SSH_CM_DATA_TYPE_CERTIFICATE,
                               keys, constraints->rule, &found);
      switch (rv)
        {
        case SSH_CM_STATUS_OK:
          {
            SshCertDBEntryListNode node, prev;
            Boolean local_possible = FALSE, multiple = FALSE;

            for (node = found->head, prev = NULL; node; node = node->next)
              {
                if (prev)
                  {
                    SshCMCertificate cert;
                    unsigned int iid, sid;

                    cert = (SshCMCertificate)node->entry->context;
                    sid = ssh_cm_cert_get_cache_id(cert);
                    cert = (SshCMCertificate)prev->entry->context;
                    iid = ssh_cm_cert_get_cache_id(cert);

                    if (!cm_failure_list_member(search, iid, sid))
                      local_possible = TRUE;
                    multiple = TRUE;
                  }
                prev = node;
            }

            if (!multiple || local_possible)
              break;
            else
              {
                ssh_certdb_entry_list_free_all(cm->db, found);
                found = NULL;
                /* FALLTHROUGH */;
              }
          }

        case SSH_CM_STATUS_NOT_FOUND:
          /* Try from external databases then... */
          if (!constraints->local.cert)
            rv = cm_search_dbs(search,
                               SSH_CM_DATA_TYPE_CERTIFICATE,
                               keys, constraints->rule, &found);
          else
            rv = SSH_CM_STATUS_NOT_FOUND;

          switch (rv)
            {
            case SSH_CM_STATUS_OK:
              break;
            case SSH_CM_STATUS_NOT_FOUND:
              SSH_CM_NOTEX(search, CERT_NOT_FOUND);
              break;
            case SSH_CM_STATUS_SEARCHING:
              ssh_certdb_entry_list_free_all(cm->db, found);
              found = NULL;
              goto goaway;
            default:
              SSH_DEBUG(SSH_D_NETFAULT, ("Error searching external DB."));
              found = NULL;
              goto failure_in_search;
            }

          /* Break. */
          break;

        default:
          /* Local search failed. */
          SSH_DEBUG(SSH_D_NETFAULT, ("Error searching local DB."));

        failure_in_search:
          SSH_DEBUG(SSH_D_HIGHOK, ("Notes: %@",
                                   ssh_cm_render_state, search->state));

          /* Let the application know as well ... */
          cm_search_callback(search, rv, NULL);
          ssh_certdb_entry_list_free_all(cm->db, found);
          found = NULL;

          /* ... and terminate search. */
          search_terminated = TRUE;
          /* Return happily though. */
          rv = SSH_CM_STATUS_OK;
          goto goaway;
        }

      /* Check if the entry list contains anything. */
      if (ssh_certdb_entry_list_empty(found) != TRUE)
        {
          SshCertDBEntryListNode list, next;

          for (list = found->head; list; list = next)
            {
              /* Get the next. */
              next = list->next;

              /* Check the session id. */
              if (list->entry->session_id == session_id)
                {
                  /* Remove certificate from the entry list. */
                  SSH_CM_NOTEX(search, CERT_CHAIN_LOOP);
                  ssh_certdb_entry_list_free(cm->db, list);
                }
              else
                {
                  SshCMCertificate current, head;

                  /* Get the certificate from the search results.
                     Also, get the certificate 'current' possibly has
                     issued from the head of chosen certificates. */
                  current = (SshCMCertificate) list->entry->context;
                  if (ssh_certdb_entry_list_empty(chosen))
                    head = NULL;
                  else
                    head = (SshCMCertificate) chosen->head->entry->context;

                  SSH_DEBUG(SSH_D_MIDOK,
                            ("Applying constraints to: %@ and %@",
                             ssh_cm_render_certificate, current->cert,
                             ssh_cm_render_certificate,
                             head ? head->cert : current->cert));

                  switch (ssh_cm_cert_apply_constraints(search,
                                                        current, head,
                                                        for_first))
                    {
                    case SSH_CM_STATUS_NOT_VALID:
                      /* We believe that the search can still, however,
                         be valid given a suitable CA. */
                      SSH_DEBUG(SSH_D_MIDOK,
                                ("CMI: Certificate validity can not yet "
                                 "be determined."));
                      /* Mark with current session id. */
                      list->entry->session_id = session_id;
                      break;
                    case SSH_CM_STATUS_CANNOT_BE_VALID:
                      SSH_DEBUG(SSH_D_HIGHOK,
                                ("CMI: Certificate cannot be valid in "
                                 "this search."));
                      SSH_CM_NOTEX(search, PATH_NOT_VERIFIED);
                      ssh_certdb_entry_list_free(cm->db, list);
                      break;

                    case SSH_CM_STATUS_OK:
                      /* Mark with current session id. */
                      list->entry->session_id = session_id;

                      /* Throw the ca into the list. */
                      if (!ssh_certdb_entry_list_add_head(cm->db,
                                                          chosen,
                                                          list->entry))
                        {
                          SSH_CM_NOTEX(search, DB_METHOD_FAILED);
                          SSH_DEBUG(SSH_D_ERROR,
                                    ("Out of memory when verifying path"));
                          break;
                        }

                      /* Call the verify function. */
                      switch ((rv = ssh_cm_verify_path(search, chosen)))
                        {
                        case SSH_CM_STATUS_NOT_VALID:
                        case SSH_CM_STATUS_NOT_FOUND:
                          SSH_DEBUG(SSH_D_NETFAULT, ("CMI: Path is invalid."));
                          SSH_CM_NOTEX(search, PATH_NOT_VERIFIED);
                          break;

                        case SSH_CM_STATUS_SEARCHING:
                          SSH_DEBUG(SSH_D_MIDOK,
                                    ("CMI: Searching for a CRL..."));
                          ssh_certdb_entry_list_free_all(cm->db, found);
                          found = NULL;
                          goto goaway;

                        case SSH_CM_STATUS_OK:
                          /* Happily for us, we succeeded. */

                          SSH_DEBUG(SSH_D_HIGHOK, ("CMI: Path is good."));

                          /* Handle the group searching here, due we
                             have found yet another good path. */
                          if (constraints->group)
                            {
                              SshCertDBEntryList *dummy;

                              SSH_DEBUG(SSH_D_MIDOK, ("Group mode search."));

                              /* Add the tail to the tail of the combined
                                 list. */
                              (void) ssh_certdb_entry_list_add_tail(cm->db,
                                                                    comb,
                                                                    chosen->
                                                                    tail->
                                                                    entry);

                              /* Only if we are searching for CA's is
                                 the current "found" list of further
                                 interest. Otherwise, we will happily
                                 jump to the next end-entity
                                 search. */
                              if (ssh_certdb_entry_list_first(chosen) ==
                                  ssh_certdb_entry_list_last(chosen)
                                  && !ssh_certdb_entry_list_empty(found))
                                {
                                  group_mode_on = TRUE;
                                  break;
                                }

                              ssh_certdb_entry_list_free_all(cm->db, found);
                              ssh_certdb_entry_list_free_all(cm->db, chosen);
                              chosen = NULL;
                              found = NULL;

                              /* The group mode will now be handled. */
                              group_mode_on = FALSE;

                              /* Free all but the last of the lists in
                                 the stack. */
                              dummy = NULL;
                              while (ssh_dstack_exists(&possible))
                                {
                                  ssh_certdb_entry_list_free_all(cm->db,
                                                                 dummy);
                                  dummy = ssh_dstack_pop(&possible);
                                  path_length--;
                                }

                              /* Check if we can continue searching
                                 with another end certificate. */
                              if (ssh_certdb_entry_list_empty(dummy) != TRUE)
                                {
                                  found = dummy;

                                  if ((chosen =
                                       ssh_certdb_entry_list_allocate(cm->db))
                                      == NULL)
                                    {
                                      SSH_CM_NOTEX(search, DB_METHOD_FAILED);
                                      goto goaway;
                                    }

                                  /* Note. We need to update the
                                     session id. */
                                  session_id = cm->session_id;
                                  cm->session_id++;
                                  SSH_DEBUG(SSH_D_LOWOK,
                                            ("Allocationg new cmi session "
                                             "id %d, without restart.",
                                             session_id));
                                  goto keep_searching;
                                }

                              ssh_certdb_entry_list_free_all(cm->db, dummy);

                              /* Set the chosen to be the comb list instead. */
                              ssh_certdb_entry_list_free_all(cm->db, chosen);
                              chosen = comb;
                              comb   = NULL;

                              /* Continue to call the callback. */
                            }

                          ssh_certdb_entry_list_free_all(cm->db, found);
                          found = NULL;

                          /* Callback. */
                          SSH_DEBUG(SSH_D_HIGHOK,
                                    ("Notes: %@",
                                     ssh_cm_render_state, search->state));

                          cm_search_callback(search, rv, chosen);

                          /* ...and the search is terminated. */
                          search_terminated = TRUE;
                          /* Set up to leave. */
                          should_chosen_be_freed = FALSE;
                          rv = SSH_CM_STATUS_OK;
                          goto goaway;

                        default:
                          SSH_DEBUG(SSH_D_NETFAULT,
                                    ("CMI: Path is invalid."));
                          SSH_DEBUG(SSH_D_HIGHOK,
                                    ("Notes: %@",
                                     ssh_cm_render_state, search->state));

                          /* Callback. Inform the application of the
                             error. */
                          cm_search_callback(search, rv, NULL);
                          ssh_certdb_entry_list_free_all(cm->db, found);
                          found = NULL;
                          /* ...and the search is terminated. */
                          search_terminated = TRUE;

                          rv = SSH_CM_STATUS_OK;
                          goto goaway;
                        } /* End for switch(verify_path()) */


                      /* Not valid! Remove from the chosen list! */
                      ssh_certdb_entry_list_free(cm->db, chosen->head);
                      break;

                    default:
                      /* Now happens something that is not expected. */
                      ssh_fatal("ssh_cm_find_internal: "
                                "bad certificate constraints.");
                      break;
                    } /* End for switch(apply_constraints) */

                  /* Seems that either the path didn't verify or the
                     certificate wasn't a trusted. */
                }
            }
        }

      /* Seek for the keys of the next issuer! */

      if (group_mode_on)
        {
          SshCertDBEntryList *dummy;

          SSH_DEBUG(6, ("Delayed group mode handling."));

          /* The group mode will now be handled. */
          group_mode_on = FALSE;

          ssh_certdb_entry_list_free_all(cm->db, found);
          ssh_certdb_entry_list_free_all(cm->db, chosen);
          found = NULL;
          chosen = NULL;

          /* Free all but the last of the lists in the stack. */
          dummy = NULL;
          while (ssh_dstack_exists(&possible))
            {
              ssh_certdb_entry_list_free_all(cm->db, dummy);
              dummy = ssh_dstack_pop(&possible);
              /* Remember to adjust the path length. */
              path_length--;
            }

          /* Check if we can continue searching with another end
             certificate. */
          if (ssh_certdb_entry_list_empty(dummy) != TRUE)
            {
              /* Make sure that we continue searching. */
              found = dummy;
              if ((chosen = ssh_certdb_entry_list_allocate(cm->db)) == NULL)
                goto goaway;

              session_id = cm->session_id;
              cm->session_id++;
              goto keep_searching;
            }

          ssh_certdb_entry_list_free_all(cm->db, dummy);

          /* Set the chosen to be the comb list instead. */
          chosen = comb;
          comb   = NULL;

          /* Continue to call the callback. */

          ssh_certdb_entry_list_free_all(cm->db, found);
          found = NULL;

          /* Callback. */
          SSH_DEBUG(SSH_D_HIGHOK,
                    ("Notes: %@", ssh_cm_render_state, search->state));

          cm_search_callback(search, rv, chosen);

          /* ...and the search is terminated. */
          search_terminated = TRUE;
          /* Set up to leave. */
          should_chosen_be_freed = FALSE;
          rv = SSH_CM_STATUS_OK;
          goto goaway;
        }

      /* The keep searching for group searches. */
    keep_searching:
      SSH_DEBUG(SSH_D_HIGHOK, ("Still possible paths, keep searching."));

      /* Free keys also. */
      if (keys_allocated)
        ssh_certdb_key_free(keys);
      keys = NULL;

      path_length++;
      if (cm->config->max_path_length <= path_length ||
          constraints->max_path_length <= path_length)
        {
          ssh_certdb_entry_list_free_all(cm->db, found);
          found = NULL;
        }

      while (keys == NULL)
        {
          /* Handle the stack structure. Pop from the stack if nothing
             found nil. */
          while (ssh_certdb_entry_list_empty(found) &&
                 ssh_dstack_exists(&possible))
            {
              ssh_certdb_entry_list_free_all(cm->db, found);
              found = ssh_dstack_pop(&possible);

              entry = ssh_certdb_entry_list_remove(cm->db, chosen->head);
              /* Enable re-use in path construction. */
              entry->session_id = 0;

              ssh_certdb_release_entry(cm->db, entry);
              path_length--;
            }

          /* Now check if nothing to do still. */
          if (ssh_certdb_entry_list_empty(found))
            {
              ssh_certdb_entry_list_free_all(cm->db, found);
              found = NULL;
              goto end;
            }

          entry = found->head->entry;
          ssh_certdb_entry_list_move(chosen, found->head);
          ssh_dstack_push(&possible, found);
          found = NULL;

          /* Check the keys. */
          if (entry->context != NULL)
            {
              SshCMCertificate current;
              current = entry->context;

              /* Get the issuer keys. And remember that we have allocated
                 them. */
              if (ssh_cm_key_set_from_cert(&keys,
                                           SSH_CM_KEY_CLASS_ISSUER,
                                           current))
                keys_allocated = TRUE;
            }
        } /* while (keys == NULL) */

      /* Next round is not the first round. */
      for_first = FALSE;
    } /* while (mainloop) */

 end:

  SSH_DEBUG(SSH_D_NETFAULT, ("Returning from internal find after an error."));
  SSH_DEBUG(SSH_D_HIGHOK,
            ("Notes: %@", ssh_cm_render_state, search->state));

  /* Hello? Cannot do more, and still haven't succeeded in
     finding the list. Exit. */

  if (ssh_certdb_entry_list_empty(comb))
	  cm_search_callback(search, SSH_CM_STATUS_NOT_FOUND, NULL);
  else
	  cm_search_callback(search, SSH_CM_STATUS_OK, comb);

  /* ...and the search is terminated. */
  search_terminated = TRUE;
  /* Not too bad though, we want the upper caller to be happy enough. */
  rv = SSH_CM_STATUS_OK;

  /* The standard way of leaving this function. */
 goaway:

  /* Free stack. */
  while (ssh_dstack_exists(&possible))
    {
      SshCertDBEntryList *entry_list;
      /* Free the stack entry, which is just a list of
         entries. */
      entry_list = ssh_dstack_pop(&possible);
      ssh_certdb_entry_list_free_all(cm->db, entry_list);
    }

  /* Free the chosen list. */
  if (should_chosen_be_freed)
    ssh_certdb_entry_list_free_all(cm->db, chosen);

  /* 'comb' is always freed. */
  ssh_certdb_entry_list_free_all(cm->db, comb);

  /* Free keys also. */
  if (keys_allocated)
    ssh_certdb_key_free(keys);

  SSH_DEBUG(4, ("Stepping out from the internal find function."));

  /* Check whether next search could be launched. */
  if (search_terminated)
    {
      search->terminated = TRUE;
      /* Call the OP's center for more operations. */
      return ssh_cm_operation_control(cm);
    }
  return rv;
}

SshCMStatus ssh_cm_operation_control(SshCMContext cm)
{
  SshCMSearchContext *tmp, *prev;
  SshCMStatus rv = SSH_CM_STATUS_OK;

  SSH_DEBUG(5, ("OP control."));
  if (cm->current == NULL)
    {
      if (cm->searching)
        {
          ssh_fatal("ssh_cm_operation_control: searching is set "
                    "even when current is NULL");
        }

      if (cm->stopping)
        cm_stopped(cm);

      return rv;
    }


  /* We don't continue if in callback. */
  if (cm->in_callback)
    {
      /* We are now continuing directly after performed a
         callback. Restart again from the bottom of the eventloop. */

      SSH_DEBUG(3, ("Retrying later."));

      if (!cm->control_timeout_active)
        {
          cm->control_timeout_active = TRUE;
          ssh_register_timeout(&cm->control_timeout,
                               cm->config->timeout_seconds,
                               cm->config->timeout_microseconds,
                               ssh_cm_timeout_control, cm);
        }
      return rv;
    }

  /* Add one to the depth of the operation recursion. This should
     work to restrict the time taken by the code to do searches
     one time. E.g. the application gets some time too if it likes. */
  cm->operation_depth++;

  /* Is the current search terminated? */
  for (tmp = cm->current, prev = NULL;
       tmp;
       prev = tmp, tmp = tmp->next)
    {
      if (tmp->terminated == TRUE && tmp->waiting == 0)
        {
          SSH_DEBUG(4, ("Removing finished search."));

          tmp = ssh_cm_remove_search(cm, tmp, prev);

          /* Remove the related searches. */
          ssh_cm_edb_operation_remove(cm, tmp);

          ssh_cm_search_free(tmp->end_cert);
          ssh_certdb_entry_list_free_all(cm->db, tmp->ca_cert);
          ssh_free(tmp->failure_list);
          ssh_free(tmp);
          tmp = prev;

          /* Check for trivial exit. */
          if (tmp == NULL)
            break;
        }
    }

  /* Control operation MAP */
  if (ssh_cm_map_control(cm->op_map))
    {
      if (!cm->map_timeout_active)
        {
          /* Notice; we register a longer timeout than default,
             therefore we use different context, even if the
             timeout function is the same */
          cm->map_timeout_active = TRUE;
          ssh_register_timeout(&cm->map_timeout,
                               cm->config->op_delay_msecs / 1000,
                               1000 * (cm->config->op_delay_msecs % 1000),
                               ssh_cm_map_timeout_control,
                               cm);
        }
    }

  /* Check whether a new search should be started. */
  if (cm->searching && cm->operation_depth < cm->config->max_operation_depth)
    {
      SshCMSearchContext *search;

      /* Run the next search. */
      for (search = cm->current; search; search = search->next)
        if (search->waiting == 0 && search->terminated != TRUE)
          {
            rv = ssh_cm_find_internal(search);
            break;
          }
    }
  else
    {
      if (cm->searching)
        {
          SSH_DEBUG(3, ("Too many levels of recursion. Trying again later."));

          /* Launch a timeout for later runs. Some applications may need
             timely answer and thus this is necessary. However, in general
             applications could handle the eventloop from their inner
             loops. */
          if (!cm->control_timeout_active)
            {
              cm->control_timeout_active = TRUE;
              ssh_register_timeout(&cm->control_timeout,
                                   cm->config->timeout_seconds,
                                   cm->config->timeout_microseconds,
                                   ssh_cm_timeout_control,
                                   cm);
            }
        }
    }

  /* Subtract one from the operation depth. */
  cm->operation_depth--;

  if (cm->stopping && cm->current == NULL)
    cm_stopped(cm);

  return rv;
}

/* Callback which restarts from the original certificate, with
   the CA now known. */
void ssh_cm_find_next(void *ctx,
                      SshCMSearchInfo info,
                      SshCertDBEntryList *list)
{
  SshCMSearchContext *search = ctx;

  SSH_DEBUG(4, ("CA search terminated."));

  /* Copy the state. It might be that application may discover
     things through this information. */
  search->state  = info->state;

  if (info->status != SSH_CM_STATUS_OK)
    {
      /* Copy the info. */
      search->status = info->status;

      /* Add the search to the search list. */
      ssh_cm_add_search(search->cm, search);
      return;
    }

  /* Add the CA certificates to the following search. */
  search->ca_cert = list;

  /* Note that the current searching called us. */
  if (search->cm->current == search)
    ssh_fatal("ssh_cm_find_next: tried to restart itself.");

  /* Add the search to the search list. */
  ssh_cm_add_search(search->cm, search);
}

/* Routines for finding the certificate from the databases. */

SshCMStatus ssh_cm_find(SshCMContext cm,
                        SshCMSearchConstraints constraints,
                        SshCMSearchResult search_callback,
                        void *caller_context)
{
  SshCMSearchContext *search;

  if (cm->stopping)
    return SSH_CM_STATUS_FAILURE;

  if ((search = ssh_calloc(1, sizeof(*search))) == NULL)
    return SSH_CM_STATUS_FAILURE;

  SSH_DEBUG(3, ("A new search initiated."));

  /* Initialize the cm pointer. */
  search->cm       = cm;
  search->next     = NULL;

  /* The status. */
  search->status   = SSH_CM_STATUS_OK;
  SSH_CM_NOTEXINIT(search);

  /* Clear up. */
  search->terminated  = FALSE;

  search->async_completed = FALSE;
  search->async_ok = FALSE;
  search->waiting = 0;

  /* Set up search for end certificate, which is trusted. No specific
     ca defined. */
  search->end_cert = constraints;
  search->ca_cert  = NULL;

  /* Handle the restarts counter. */
  search->restarts = 0;

  /* Set up the application callback and context. */
  search->callback       = search_callback;
  search->search_context = caller_context;

  /* Failure list */
  search->failure_list_size = 0;
  search->failure_list = NULL;

  /* Add to the search list. */
  ssh_cm_add_search(cm, search);

  /* Add the search to the operation map. */
  ssh_cm_edb_operation_add(cm, search);

  /* Call the searching routine. */
  return ssh_cm_operation_control(cm);
}

SshCMStatus ssh_cm_find_path(SshCMContext cm,
                             SshCMSearchConstraints ca_constraints,
                             SshCMSearchConstraints end_constraints,
                             SshCMSearchResult search_callback,
                             void *caller_context)
{
  SshCMSearchContext *search;
  SshCMSearchContext *f_search;

  if (cm->stopping)
    return SSH_CM_STATUS_FAILURE;

  SSH_DEBUG(3, ("A new search (with given CA) initiated."));

  if ((search = ssh_calloc(1, sizeof(*search))) == NULL)
    return SSH_CM_STATUS_FAILURE;

  if ((f_search = ssh_calloc(1, sizeof(*f_search))) == NULL)
    {
      ssh_free(search);
      return SSH_CM_STATUS_FAILURE;
    }

  /* Initialize the cm pointer. */
  search->cm       = cm;
  f_search->cm     = cm;

  /* The error status. */
  search->status   = SSH_CM_STATUS_OK;
  f_search->status = SSH_CM_STATUS_OK;
  SSH_CM_NOTEXINIT(search);
  SSH_CM_NOTEXINIT(f_search);

  /* Clear up. */
  search->terminated  = FALSE;

  f_search->terminated  = FALSE;

  /* Set up search for end certificate, which is trusted. No specific
     ca defined. */
  search->end_cert = ca_constraints;
  search->ca_cert  = NULL;
  /* Specific validity information given! */

  f_search->end_cert = end_constraints;
  f_search->ca_cert  = NULL;

  f_search->async_completed = search->async_completed = FALSE;
  f_search->async_ok = search->async_ok = FALSE;
  f_search->waiting = search->waiting = 0;

  /* Handle the restarts counter. */
  search->restarts = 0;
  /* Handle the restarts counter. */
  f_search->restarts = 0;

  /* Set up the certificate callback and context. Notice that we,
     don't go the application yet, but to the another certificate
     searcher. */

  search->callback       = ssh_cm_find_next;
  search->search_context = f_search;

  /* Search for the certificate (with the knowledge of the CA)! */
  f_search->callback       = search_callback;
  f_search->search_context = caller_context;

  /* Failure lists */
  search->failure_list_size = 0;
  search->failure_list = NULL;
  f_search->failure_list_size = 0;
  f_search->failure_list = NULL;

  /* Push the first search to the list. */
  ssh_cm_add_search(cm, search);

  /* Add the search to the operation map. */
  ssh_cm_edb_operation_add(cm, search);
  /* Add the search to the operation map. */
  ssh_cm_edb_operation_add(cm, f_search);

  /* Call the searching routine. */
  return ssh_cm_operation_control(cm);
}

#ifdef SUNWIPSEC

/* Mapping between cert error codes and error strings. */
const SshKeywordStruct cm_status_keywords[] = {
  { "OK", SSH_CM_STATUS_OK },
  { "Item already exists", SSH_CM_STATUS_ALREADY_EXISTS },
  { "Item not found", SSH_CM_STATUS_NOT_FOUND },
  { "Invalid input", SSH_CM_STATUS_INVALID_DATA },
  { "Search still in progress", SSH_CM_STATUS_SEARCHING },
  { "Could not delinearize data", SSH_CM_STATUS_COULD_NOT_DELINEARIZE },
  { "Decoding failed", SSH_CM_STATUS_DECODE_FAILED },
  { "Local database not writable", SSH_CM_STATUS_LOCAL_DB_NWRITE },
  { "Database error", SSH_CM_STATUS_DB_ERROR },
  { "Validity time too short", SSH_CM_STATUS_VALIDITY_TIME_TOO_SHORT },
  { "Certificate not valid (Authentication chain may be incomplete)",
    SSH_CM_STATUS_NOT_VALID },
  { "Certificate cannot be valid", SSH_CM_STATUS_CANNOT_BE_VALID },
  { "Operation expired", SSH_CM_STATUS_OPERATION_EXPIRED },
  { "Allocation failed", SSH_CM_STATUS_COULD_NOT_ALLOCATE },
  { "Status not available", SSH_CM_STATUS_NOT_AVAILABLE },
  { "Certificate class number too large", SSH_CM_STATUS_CLASS_TOO_LARGE },
  { "Certificate class number not changed", SSH_CM_STATUS_CLASS_UNCHANGED },
  { "Operation failed", SSH_CM_STATUS_FAILURE },
  { NULL, 0 }
};

/* Convert CM status to human readable string */
const char *cm_status_to_string(SshCMStatus code)
{
  const char *str;

  str = ssh_find_keyword_name(cm_status_keywords, code);
  if (str == NULL)
    str = "unknown";
  return str;
}
#endif /* SUNWIPSEC */
/* cmi.c */
