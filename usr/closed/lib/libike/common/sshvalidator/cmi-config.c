/*
  cmi-config.c

  Copyright:
        Copyright (c) 2002, 2003 SFNT Finland Oy.
	All rights reserved.

  Validator configuration.
*/

#include "sshincludes.h"
#include "cmi.h"
#include "cmi-internal.h"

#define SSH_DEBUG_MODULE "SshCertCMi"

/************ Configuration Setup ****************/

SshTime ssh_cm_internal_time(void *context)
{
  return ssh_time();
}

SshCMConfig ssh_cm_config_allocate(void)
{
  SshCMConfig config;

  SSH_DEBUG(5, ("Allocate configuration (set up to default values)."));
  if ((config = ssh_calloc(1, sizeof(*config))) != NULL)
    {
      /* Set up the default time function. */
      config->time_func    = ssh_cm_internal_time;
      config->time_context = NULL;

      /* The default path length if 16 certificates. */
      config->max_path_length = 16;

      /* Maximum 4 recursive calls to operation control is allowed
         before returning to the main level. (It is basically tail
         recursion so one could implement it with a loop! But
         recursion looks better.) */
      config->max_operation_depth = 4;

      /* The default maximum restart value. This is the total number
         of successive calls from the bottom of the event loop to the
         internal CM find function. A restart happens when external
         database gets consulted, thus this value depends on the path
         length and availability of certificates and CRL's from the
         application protocol. */
      config->max_restarts = 32;

      /* Set the granularity of timeout checks. */
      config->granularity_msecs = 10;

      /* Set the default operation length in msecs. */
      config->op_delay_msecs = 1000*10;

      /* Assume zero timeout. The application might need some time
         itself thus this is not always optimal. */
      config->timeout_seconds = 0;
      config->timeout_microseconds = 0;

      /*** What is allowed? */
      config->local_db_allowed = TRUE;

      /* By default we want to write to the local db. */
      config->local_db_writable = TRUE;

      /*** Local DB information. */

      /* Not too large, but not too small either cache for
         certificates. Useful in general. */
      config->max_cache_entries = 256;
      config->max_cache_bytes = config->max_cache_entries * 1024;

      /* Default minimum time an entry will be found form the
         local database. */
      config->default_time_lock = 5;

      /*** Path validition information. */

      /* The maximum number of seconds a certificate need to be valid,
         before revalidation computations. Currently a week. */
      config->max_validity_secs = 60*60*24*7;

      /* The minimum number of seconds a crl is kept in the cache. */
      config->min_crl_validity_secs =  1 * 60*60;

      /* The maximum number of seconds a crl is kept in the cache
         is unlimited. */
      config->max_crl_validity_secs = 0;

#ifdef SSHDIST_VALIDATOR_OCSP
      /* The minimum number of seconds the ocsp response is kept valid.
         The value is added to the thisUpdate field of the response. */
      config->min_ocsp_validity_secs = 5*60;
#endif /* SSHDIST_VALIDATOR_OCSP */

      /*** NegaCache information. */

      /* 1024 is the default number of names for one tag to be remembered. */
      config->nega_cache_size = 128;
      /* The invalidity time is in seconds, after that one will try again,
         if still needed. */
      config->nega_cache_invalid_secs = 60;

      /*** Notification. */
      config->notify_events  = NULL;
      config->notify_context = NULL;

      /* External search indexes */
      config->num_external_indexes = 0;
      config->external_index_cb = NULL;
      config->external_index_ctx = NULL;


      /* Limitations for external resource access */
      config->max_certificate_length = 16 * 1024;
      config->max_crl_length = 5000 * 1024;
      config->max_ldap_response_length = 5001 * 1024;
      config->max_ocsp_response_length = 16 * 1024;

      config->ldap_connection_idle_timeout = 0;

    }
  /* Return the default configuration. */
  return config;
}

void ssh_cm_config_free(SshCMConfig config)
{
  SSH_DEBUG(5, ("Free configuration."));
  ssh_free(config);
}

void
ssh_cm_config_ldap_configure(SshCMConfig config,
                             size_t max_response_length,
                             SshUInt32 idle_timeout_seconds)
{
  if (max_response_length)
    config->max_ldap_response_length = max_response_length;
  config->ldap_connection_idle_timeout = idle_timeout_seconds;
}

void
ssh_cm_config_sizes_configure(SshCMConfig config,
                              size_t max_certificate_length,
                              size_t max_crl_length)
{
  if (max_certificate_length)
    config->max_certificate_length = max_certificate_length;
  if (max_crl_length)
    config->max_crl_length = max_crl_length;
}

void ssh_cm_config_set_time_function(SshCMConfig config,
                                     SshCMTimeFunc func,
                                     void *caller_context)
{
  SSH_DEBUG(4, ("Set configuration time function."));
  config->time_func    = func;
  config->time_context = caller_context;
}

void ssh_cm_config_set_default_time_lock(SshCMConfig config,
                                         unsigned int secs)
{
  if (secs > 120)
    secs = 120;
  SSH_DEBUG(4, ("Set configuration default time lock (%u secs).", secs));
  config->default_time_lock = secs;
}

/* Set up the configuration, with simple functions. */
void ssh_cm_config_set_max_path_length(SshCMConfig config,
                                       unsigned int max_path_length)
{
  SSH_DEBUG(4, ("Set configuration max path length (%u).", max_path_length));
  config->max_path_length = max_path_length;
}

void ssh_cm_config_set_max_operation_delay(SshCMConfig config,
                                           unsigned int msecs)
{
  SSH_DEBUG(4, ("Sets the maximum number of delay allowed for "
                "EDB operation to %u msecs.", msecs));
  config->op_delay_msecs = msecs;
}

void ssh_cm_config_set_max_restarts(SshCMConfig config,
                                    unsigned int max_restarts)
{
  SSH_DEBUG(4, ("Set configuration max restarts (%u).", max_restarts));
  config->max_restarts = max_restarts;
}

void ssh_cm_config_set_validity_secs(SshCMConfig config,
                                     unsigned int secs)
{
  SSH_DEBUG(4, ("Set configuration validity seconds (%u).", secs));

  /* Quantize to one hour. */
  secs = (secs + 60*60) - (secs % (60*60));
  config->max_validity_secs = secs;
}

void ssh_cm_config_set_crl_validity_secs(SshCMConfig config,
                                         unsigned int minsecs,
                                         unsigned int maxsecs)
{
  SSH_DEBUG(4, ("Set configuration crl validity seconds (%u-%u).",
                minsecs, maxsecs));

  config->min_crl_validity_secs = minsecs;
  if (maxsecs)
    config->max_crl_validity_secs = maxsecs;
}

#ifdef SSHDIST_VALIDATOR_OCSP
void ssh_cm_config_set_ocsp_validity_secs(SshCMConfig config,
                                          unsigned int secs)
{
  SSH_DEBUG(4, ("Set configuration ocsp validity seconds (%u).", secs));

  config->min_ocsp_validity_secs = secs;
}
#endif /* SSHDIST_VALIDATOR_OCSP */

void ssh_cm_config_set_nega_cache_invalid_secs(SshCMConfig config,
                                               unsigned int secs)
{
  SSH_DEBUG(4, ("Set configuration nega cache invalid seconds (%u).", secs));

  if (secs < 10)
    secs = 10;
  config->nega_cache_invalid_secs = secs;
}

/* Set up the timeout after a 'blocked' operation control call. */
void ssh_cm_config_set_timeout(SshCMConfig config,
                               long seconds,
                               long microseconds)
{
  SSH_DEBUG(4, ("Set configuration timeout (%u secs %u msecs).",
                seconds, microseconds));

  config->timeout_seconds      = seconds;
  config->timeout_microseconds = microseconds;
}

void ssh_cm_config_set_cache_size(SshCMConfig config,
                                  unsigned int bytes)
{
  SSH_DEBUG(4, ("Set configuration cache size (%u bytes).", bytes));

  /* We must have some lower bound, just to allow the
     basic operation. If the cache is smaller, then we cannot
     really do anything (also the code probably is easier to
     handle anyway if we can assume that the cache has atleast 64
     certs at a time). Further, it is unlikely that any path
     validation computations etc. would take more than 10 - 20
     certificates. Thus about 2 path validations could be
     runned at once, without degrating the performance.

     XXX Write code which handles the restricting of searches to
     some reasonable level. E.g. only size/10 searches at a time
     to allow caching. */
  if (bytes < 64 && bytes > 0)
    bytes = 64;
  config->max_cache_bytes = bytes;
}

void ssh_cm_config_set_cache_max_entries(SshCMConfig config,
                                         unsigned int entries)
{
  if (entries < 64 && entries > 0)
    entries = 64;
  SSH_DEBUG(4, ("Set configuration cache max entries (%u entries).", entries));
  config->max_cache_entries = entries;
}

void ssh_cm_config_set_notify_callbacks(SshCMConfig config,
                                        const SshCMNotifyEvents events,
                                        void *caller_context)
{
  SSH_DEBUG(4, ("Set configuration notify callbacks."));

  config->notify_events  = events;
  config->notify_context = caller_context;
}

/* Register external search index. This is given a callback function that is
   used to get hash data out of certificate. There is no way to add new search
   indexes after the cmi has been allocated, and there is no way to remove
   indexes. This returns an SshCMSearchIndexHandle that can be used to
   identify the index later (it is given to the ssh_cm_key_set_external). */
SshCMSearchIndexHandle
ssh_cm_config_register_search_index(SshCMConfig cfg,
                                    SshCMGenerateHashDataCB generate_hash_cb,
                                    void *generate_hash_cb_context)
{
  SshCMSearchIndexHandle idx;
  void *tmp1, *tmp2;
  size_t item_size;

  idx = cfg->num_external_indexes;

  item_size = sizeof(cfg->external_index_cb);
  tmp1 =
    ssh_realloc(cfg->external_index_cb,
                item_size * (cfg->num_external_indexes),
                item_size * (cfg->num_external_indexes + 1));


  item_size = sizeof(cfg->external_index_ctx);
  tmp2 =
    ssh_realloc(cfg->external_index_ctx,
                item_size * (cfg->num_external_indexes),
                item_size * (cfg->num_external_indexes + 1));

  if (tmp1 && tmp2)
    {
      cfg->external_index_cb  = tmp1;
      cfg->external_index_ctx = tmp2;
      cfg->num_external_indexes++;

      cfg->external_index_cb[idx] = generate_hash_cb;
      cfg->external_index_ctx[idx] = generate_hash_cb_context;

      return idx + SSH_CM_KEY_TYPE_NUM;
    }
  else
    {
      return 0;
    }
}
