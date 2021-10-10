/*
  cmi-ldap.c

  Copyright:
        Copyright (c) 2002, 2003 SFNT Finland Oy.
	All rights reserved.

  Certificate validator; LDAP external database for retrieving CRL's
  and Certificates.
*/

#include "sshincludes.h"

#include "cmi.h"
#include "cmi-edb.h"
#include "cmi-debug.h"
#include "cmi-internal.h"

#include "sshurl.h"
#include "sshdsprintf.h"
#include "sshglist.h"
#include "sshldap.h"
#include "sshtimeouts.h"

#include "sshadt.h"
#include "sshadt_map.h"

#define SSH_DEBUG_MODULE "SshCertEdbLdap"

#ifdef SSHDIST_VALIDATOR_LDAP

typedef enum {
  LDAP_NOT_CONNECTED,
  LDAP_CONNECTING,
  LDAP_CONNECTED
} SshCMEdbLdapConnectionState;

typedef struct
{
  /* Concrete header, concrete object model for ADT. */
  SshADTMapHeaderStruct adt_header;

  unsigned char *identifier;
  SshLdapClientParams  params;
  SshLdapClient context;
  SshCMEdbLdapConnectionState state;

  char *ldap_server_name;
  char *ldap_server_port;
  char *bind_name; size_t bind_name_len;
  char *password; size_t password_len;

  void   *search;
  Boolean search_freed;

  int idle;
} *SshCMEdbLdapConnection, SshCMEdbLdapConnectionStruct;

typedef struct
{
  SshCMSearchDatabase    *db;
  SshCMDBDistinguisher   *dg;
  SshCMEdbLdapConnection  connection;
  void                   *context;
  char                   *object_name;
  SshLdapSearchFilter     filter;
  unsigned int            counter;
  SshOperationHandle      msg_id;
  unsigned long           tc;
} *SshCMEdbLdapSearch, SshCMEdbLdapSearchStruct;

typedef struct
{
  SshCMContext cm;
  SshADTContainer map;
  SshTimeoutStruct timeout;
} *SshCMEdbLdap, SshCMEdbLdapStruct;


static void cm_edb_ldap_timeout(void *context);

SshUInt32
cm_ldap_connection_hash(const void *object, void *context)
{
  SshCMEdbLdapConnection conn = (SshCMEdbLdapConnection) object;
  SshUInt32 hash = 0;
  const unsigned char *c = conn->identifier;

  while (*c)
    {
      hash += *c++;
      hash += hash << 10;
      hash ^= hash >> 6;
    }

  hash += hash << 3;
  hash ^= hash >> 11;
  hash += hash << 15;

  return hash;
}

int
cm_ldap_connection_compare(const void *object1,
                           const void *object2,
                           void *context)
{
  SshCMEdbLdapConnection c1 = (SshCMEdbLdapConnection) object1;
  SshCMEdbLdapConnection c2 = (SshCMEdbLdapConnection) object2;

  return strcmp(ssh_csstr(c1->identifier), ssh_csstr(c2->identifier));
}


static void
cm_ldap_connection_destroy(SshCMEdbLdapConnection connection, void *context)
{
  if (connection->context)
    ssh_ldap_client_destroy(connection->context);

  ssh_free(connection->ldap_server_name);
  ssh_free(connection->ldap_server_port);
  ssh_free(connection->bind_name);
  ssh_free(connection->password);
  ssh_free(connection->params->socks);
  ssh_free(connection->params);
  ssh_free(connection->identifier);
  ssh_free(connection);
}

/* Note, this function stores everything. Hence, it may not be very
   memory friendly. Most of the information about LDAP could be freed
   immediately after initialization.

   This steals server, bind_name and password. */
static SshCMEdbLdapConnection
cm_ldap_connection_create(SshCMContext cm,
                          SshCMLocalNetwork net,
                          unsigned char *server,
                          char *bind_name, char *password)
{
  SshCMEdbLdapConnection connection = ssh_calloc(1, sizeof(*connection));
  char *host, *port;

  if (connection == NULL)
    return NULL;

  /* Allocate the params space. */
  if ((connection->params = ssh_calloc(1, sizeof(*connection->params)))
      == NULL)
    {
      ssh_free(connection);
      return NULL;
    }

  connection->state        = LDAP_NOT_CONNECTED;
  connection->search       = NULL;
  connection->search_freed = FALSE;

  /* Parse the server name. */
  if (ssh_url_parse(server, NULL,
                    (unsigned char **)&host, (unsigned char **)&port,
                    NULL, NULL, NULL))
    {
      connection->ldap_server_name = host;
      connection->ldap_server_port = port;
    }
  else
    {
      ssh_free(connection);
      return NULL;
    }

  if (bind_name)
    {
      connection->bind_name = bind_name;
      connection->bind_name_len = strlen(bind_name);
    }
  if (password)
    {
      connection->password = password;
      connection->password_len = strlen(password);
    }

  if (net->socks)
    {
      connection->params->socks = ssh_strdup(net->socks);
    }

  connection->params->version = SSH_LDAP_VERSION_3;
  connection->params->response_bytelimit =
    cm->config->max_ldap_response_length;

  /* Initialize the LDAP client. */
  connection->context = ssh_ldap_client_create(connection->params);
  if (connection->context == NULL)
    {
      cm_ldap_connection_destroy(connection, NULL);
      return NULL;
    }

  /* Store the server also. */
  connection->identifier = server;
  return connection;
}

static void
cm_edb_ldap_reply(SshLdapClient ldap_ctx,
                  SshLdapObject object,
                  void *ctx)
{
  SshCMEdbLdapSearch search = ctx;
  int i, j;

  SSH_DEBUG(4, ("Ldap reply callback reached (%s, %u attributes).",
                search->connection->identifier,
                object->number_of_attributes));

  for (i = 0; i < object->number_of_attributes; i++)
    {
      for (j = 0; j < object->attributes[i].number_of_values; j++)
        {
          search->counter++;
          ssh_cm_edb_reply(search->db, search->context, search->dg,
                           (const unsigned char *)
                           object->attributes[i].values[j],
                           object->attributes[i].value_lens[j]);
        }
    }
  SSH_DEBUG(4, ("Ldap reply ends."));
  ssh_ldap_free_object(object);
}

static void
cm_edb_ldap_result(SshLdapClient ldap_ctx,
                   SshLdapResult result,
                   const SshLdapResultInfo info,
                   void *ctx)
{
  SshCMEdbLdapSearch search = ctx;
  SshCMDBStatus status;

  if (search->connection->search == search)
    search->connection->search_freed = TRUE;

  if (result == SSH_LDAP_RESULT_SUCCESS && info->error_message == NULL)
    {
      SSH_DEBUG(SSH_D_HIGHOK,
                ("EDB/LDAP: Search for %@ [OK].",
                 ssh_cm_edb_distinguisher_render, search->dg));
      if (search->counter)
        status = SSH_CMDB_STATUS_OK;
      else
        status = SSH_CMDB_STATUS_FAILED;
    }
  else
    {
      SSH_DEBUG(SSH_D_HIGHOK,
                ("EDB/LDAP: Search for %@ [FAILED]: %s.",
                 ssh_cm_edb_distinguisher_render, search->dg,
                 ssh_find_keyword_name(ssh_ldap_error_keywords,
                                       result)));

      if (result == SSH_LDAP_RESULT_ABORTED ||
          result == SSH_LDAP_RESULT_DISCONNECTED)
        {
          status = SSH_CMDB_STATUS_TIMEOUT;
          search->connection->state = LDAP_NOT_CONNECTED;
        }
      else
        status = SSH_CMDB_STATUS_FAILED;
    }

  /* XXX: if failed and this search was directed to single host (by
     CDP) restart with search not directed, and do not signal waiters,
     yet, but only when that particular search completes. */

  SSH_DEBUG(SSH_D_LOWOK, ("EDB/LDAP: banning the exact search for a while."));
  ssh_cm_edb_ban_add_ctx(search->context, search->dg,
                         ssh_csstr(search->connection->identifier));

  /* Reset idle timer */
  search->connection->idle = 0;

  if (search->msg_id)
    {
      SSH_DEBUG(SSH_D_MIDOK,
                ("Signalling waiters, search 0x%x LDAP operation 0x%x",
                 search, search->msg_id));

      search->msg_id = NULL;
      ssh_cm_edb_operation_msg(search->context, search->dg,
                               ssh_csstr(search->connection->identifier),
                               status);
    }
}

void ssh_cm_edb_ldap_operation_free(void *context,
                                    void *search_context)
{
  SshCMEdbLdapSearch search = search_context;

  SSH_DEBUG(SSH_D_LOWOK,
            ("Freeing LDAP search 0x%x for %s", search, search->object_name));

  if (search->msg_id)
    {
      SSH_DEBUG(SSH_D_MIDOK,
                ("-> Aborting LDAP operation 0x%x", search->msg_id));
      ssh_operation_abort(search->msg_id);
      search->msg_id = NULL;
    }

  if (search->filter != NULL)
    ssh_ldap_free_filter(search->filter);
  ssh_free(search->object_name);
  ssh_free(search);
}

typedef struct SshCMEdbLdapConnectionEstRec {
  SshCMEdbLdapConnection connection;
  SshCMEdbLdapSearch search;
  SshLdapSearchScope ldap_scope;
  int attribute_cnt;
  size_t *attribute_len_table;
  unsigned char **attribute_table;
  unsigned char *attribute_data;
  SshCMEdbLdap ldap;
} *SshCMEdbLdapConnectionEst;

static void
cm_ldap_connect_establish_free(Boolean aborted, void *context)
{
  SshCMEdbLdapConnectionEst ce = context;

  if (aborted)
    ce->connection->state = LDAP_NOT_CONNECTED;

  ssh_free(ce->attribute_table);
  ssh_free(ce->attribute_len_table);
  ssh_free(ce->attribute_data);
  ssh_free(ce);
}

static void
cm_edb_ldap_start_search(SshLdapClient client,
                         SshLdapResult result,
                         const SshLdapResultInfo info,
                         void *callback_context)
{
  SshCMEdbLdapConnectionEst ce = callback_context;
  SshCMEdbLdapConnection connection = ce->connection;
  SshCMEdbLdapSearch search = ce->search;
  SshCMEdbLdap ldap = ce->ldap;

  search->msg_id = NULL;
  if (result == SSH_LDAP_RESULT_SUCCESS)
    {
      /* Info is only set when call comes from the LDAP
         library. Validator calls this without info being set (if
         deciding to reuse this client connection. */
      if (info)
        {
          connection->state = LDAP_CONNECTED;

          if (ldap->cm->config->ldap_connection_idle_timeout != 0)
            /* Start open connection tracker */
            ssh_register_timeout(&ldap->timeout,
                                 10L, 0L,
                                 cm_edb_ldap_timeout, ldap);
        }

      /* Mark connection as active */
      connection->idle = 0;

      search->msg_id =
        ssh_ldap_client_search(connection->context, search->object_name,
                               ce->ldap_scope,
                               SSH_LDAP_DEREF_ALIASES_NEVER,
                               0, 0, FALSE, search->filter,
                               ce->attribute_cnt,
                               ce->attribute_table,
                               ce->attribute_len_table,
                               cm_edb_ldap_reply,  search,
                               cm_edb_ldap_result, search);
      SSH_DEBUG(SSH_D_MIDOK,
                ("Starting LDAP search 0x%x for %s",
                 search->msg_id, search->object_name));
    }
  else
    {
      SSH_DEBUG(SSH_D_NETFAULT,
                ("Can't connect to server '%s'; %s(%d) for search %s: %s.",
                 connection->identifier,
                 ssh_find_keyword_name(ssh_ldap_error_keywords, result),
                 result,
                 search->object_name,
                 info->error_message));

      cm_edb_ldap_result(connection->context, result, info, search);
      connection->state = LDAP_NOT_CONNECTED;
    }
}

#define SSH_CM_LDAP_SEARCH_FILTER "(objectclass=*)"

Boolean ssh_cm_edb_ldap_add(SshCMContext cm,
                            const char *default_servers);

SshCMSearchMode ssh_cm_edb_ldap_search(SshCMSearchDatabase  *db,
                                       SshCMContext          cm,
                                       void                 *context,
                                       SshCMDBDistinguisher *dg)
{
  SshCMEdbLdap ldap = db->context;
  SshCMSearchMode status = SSH_CM_SMODE_FAILED;
  SshCMDBDistinguisher *new_dg = NULL;
  unsigned char *url = NULL;
  unsigned char *scheme = NULL, *host = NULL, *port = NULL;
  unsigned char *username = NULL, *password = NULL, *path = NULL;
  unsigned char *name = NULL, *attributes = NULL, *scope = NULL;
  unsigned char *filter = NULL;
  unsigned char *ps, *p;
  Boolean one_host = FALSE;
  SshADTHandle handle;
  const unsigned char *null;

  if (dg->edb_conversion_function)
    {
      if (!(*dg->edb_conversion_function)(db, cm, dg, &new_dg,
                                          dg->edb_conversion_function_context))
      {
        SSH_DEBUG(SSH_D_NICETOKNOW, ("EDB/LDAP: conversion function failed"));
        return SSH_CM_SMODE_FAILED;
      }

      /* Start using the new dg */
      if (new_dg != NULL)
        dg = new_dg;
    }

  /* Determine whether the search key is suitable for LDAP. */
  if (dg->key_type == SSH_CM_KEY_TYPE_DIRNAME ||
      dg->key_type == SSH_CM_KEY_TYPE_DISNAME)
    {
      SshDNStruct dn;
      char *base_dn;

      /* Make a LDAP name. */
      ssh_dn_init(&dn);
      if (ssh_dn_decode_der(dg->key, dg->key_length, &dn, NULL) == 0)
        {
          /* Free the allocated space. */
          ssh_dn_clear(&dn);
          if (new_dg) ssh_cm_edb_distinguisher_free(new_dg);
          {
            SSH_DEBUG(SSH_D_NICETOKNOW, ("EDB/LDAP: Base-DN decode failed."));
            return SSH_CM_SMODE_FAILED;
          }
        }
      /* Reverse the DN to the LDAP style. */
      ssh_dn_reverse(&dn);
      if (ssh_dn_encode_ldap(&dn, &base_dn) == 0)
        {
          /* Free the allocated space. */
          ssh_dn_clear(&dn);
          if (new_dg) ssh_cm_edb_distinguisher_free(new_dg);
          SSH_DEBUG(SSH_D_NICETOKNOW, ("EDB/LDAP: Base-DN encode failed."));
          return SSH_CM_SMODE_FAILED;
        }
      ssh_dn_clear(&dn);
      /* Now we have allocated a LDAP name. */

      if (ssh_dsprintf((char **) &url, "ldap:/%s", base_dn) == -1)
	url = NULL;
      ssh_free(base_dn);
    }
  else if (dg->key_type == SSH_CM_KEY_TYPE_URI)
    {
      url = ssh_memdup(dg->key, dg->key_length);
    }
  else
    {
      if (new_dg) ssh_cm_edb_distinguisher_free(new_dg);
      SSH_DEBUG(SSH_D_LOWOK,
                ("EDB/LDAP: Unknown key type %s.",
                 ssh_find_keyword_name(ssh_cm_edb_key_types, dg->key_type)));
      return SSH_CM_SMODE_FAILED;
    }

  if (url == NULL ||
      ssh_url_parse(url,
                    (unsigned char **)&scheme,
                    (unsigned char **)&host, (unsigned char **)&port,
                    (unsigned char **)&username, (unsigned char **)&password,
                    (unsigned char **)&path) == FALSE)
    {
      ssh_free(url);
      if (new_dg) ssh_cm_edb_distinguisher_free(new_dg);
      SSH_DEBUG(SSH_D_NICETOKNOW, ("EDB/LDAP: Invalid URL syntax."));
      return SSH_CM_SMODE_FAILED;
    }

  /* URI is now parsed, we need to know whether this is a LDAP connection
     request. */
  if (strcasecmp(ssh_csstr(scheme), "ldap") != 0)
  {
    SSH_DEBUG(SSH_D_NICETOKNOW,
              ("EDB/LDAP: Invalid URL schema %s not LDAP.", scheme));
    goto exit_point;
  }

  /* If the URL specifies host, we'll make a LDAP connection to that
     host, if it does not already exists.  */
  if (host)
    {
      unsigned char *server;
      SshCMEdbLdapConnectionStruct probe;

      one_host = TRUE;
      ssh_dsprintf((char **) &server, "%s://%s%s%s%s%s:%s",
                   scheme   ? scheme   : ssh_custr("ldap"),
                   username ? username : ssh_custr(""),
                   password ? ":" : "", password ? password : ssh_custr(""),
                   username ? "@" : "",
                   host, port);

      if (server == NULL)
        {
          SSH_DEBUG(SSH_D_FAIL, ("EDB/ldap server encoding: no space."));
          goto exit_point;
        }

      if (ssh_cm_edb_ldap_add(cm, ssh_csstr(server)) == FALSE)
        {
          ssh_free(server);
          SSH_DEBUG(SSH_D_FAIL, ("EDB/ldap server '%s': add failed.",
                                 server));
          goto exit_point;
        }
      probe.identifier = server;
      handle = ssh_adt_get_handle_to_equal(ldap->map, &probe);
      ssh_free(server);
    }
  else
    {
      handle = ssh_adt_enumerate_start(ldap->map);
    }

  /* Split in to pieces */
  if (path == NULL)
    {
      SSH_DEBUG(SSH_D_FAIL, ("EDB/LDAP: No base object."));
      goto exit_point;
    }

  name = path;
  path = ssh_ustr(strchr(ssh_csstr(path), '?'));
  if (path) *path++ = '\0';

  attributes = path;
  if (path) path = ssh_ustr(strchr(ssh_csstr(path), '?'));
  if (path) *path++ = '\0';
  if (attributes == NULL || strlen(ssh_csstr(attributes)) == 0)
    {
      if (dg->data_type == SSH_CM_DATA_TYPE_CRL)
        attributes =
          ssh_strdup("certificaterevocationlist,authorityrevocationlist");
      else
        attributes = ssh_strdup("usercertificate,cacertificate"
                                ",crosscertificatepair");
    }
  else
    {
      attributes = ssh_strdup(attributes);
    }

  if (attributes == NULL)
    goto exit_point;

  scope = path;
  if (path) path = ssh_ustr(strchr(ssh_csstr(path), '?'));
  if (path) *path++ = '\0';
  if (scope == NULL || strlen(ssh_csstr(scope)) == 0)
    {
      scope = ssh_strdup("base");
    }
  else
    {
      scope = ssh_strdup(scope);
    }

  if (scope == NULL)
    goto exit_point;

  filter = path;
  if (path) path = ssh_ustr(strchr(ssh_csstr(path), '?'));
  if (path) *path++ = '\0';
  if (filter == NULL || strlen(ssh_csstr(filter)) == 0)
    {
      ssh_free(filter);
      filter = ssh_strdup(SSH_CM_LDAP_SEARCH_FILTER);
    }
  else
    {
      filter = ssh_strdup(filter);
    }

  if (filter == NULL)
    goto exit_point;

  /* Decode the object name. */
  if ((p = ssh_url_data_decode(name, strlen(ssh_csstr(name)), NULL)) != NULL)
    {
      ssh_free(name);
      name = p;
    }

  path = NULL;

  null = ssh_custr("null");
  SSH_DEBUG(SSH_D_HIGHSTART,
            ("LDAP base dn = <%s> attributes <%s>, scope = <%s>, "
             "filter = <%s>",
             name ? name : null,
             attributes ? attributes : null,
             scope ? scope : null,
             filter ? filter : null));

 restart_with_all_servers:

  for (;
       handle != SSH_ADT_INVALID;
       handle = ssh_adt_enumerate_next(ldap->map, handle))
    {
      SshCMEdbLdapConnection connection;
      SshCMEdbLdapSearch search;
      SshCMEdbLdapConnectionEst ce;
      SshLdapSearchScope ldap_scope;
      unsigned char **attribute_table;
      size_t *attribute_len_table;
      int attribute_alloc;
      int attribute_cnt;

      connection = ssh_adt_get(ldap->map, handle);

      /* The search starting. */
      if (ssh_cm_edb_ban_check(cm, NULL, ssh_csstr(connection->identifier)) ||
          ssh_cm_edb_ban_check(cm, dg, ssh_csstr(connection->identifier)))
        {
          if (one_host)
            {
              if ((handle = ssh_adt_enumerate_start(ldap->map))
                  != SSH_ADT_INVALID)
                {
                  SSH_DEBUG(SSH_D_NICETOKNOW,
                            ("EDB/LDAP: Search for '%s' [single host] "
                             "at %s was banned. Restarting from servers "
                             "given at configuration",
                             name, connection->identifier));
                  one_host = FALSE;
                  goto restart_with_all_servers;
                }
            }
          SSH_DEBUG(SSH_D_NICETOKNOW,
                    ("EDB/LDAP: Search for '%s' at %s was banned.",
                     name, connection->identifier));
          continue;
        }

      /* Try searching, and for that we need to generate a small
         search context. */
      if ((search = ssh_calloc(1, sizeof(*search))) == NULL)
        goto exit_point;

      search->context    = context;
      search->db         = db;
      search->dg         = dg;
      search->connection = connection;
      search->counter    = 0;
      search->tc         = 0;

      /* Take a copy for later use. */
      if ((search->object_name = ssh_strdup(name)) == NULL)
        {
          ssh_free(search);
          goto exit_point;
        }

      if ((ce = ssh_malloc(sizeof(*ce))) == NULL)
        {
          ssh_free(search->object_name);
          ssh_free(search);
          goto exit_point;
        }

      /* Check for search in progress. */
      if (ssh_cm_edb_operation_check(context, dg,
                                     ssh_csstr(connection->identifier))
          == TRUE)
        {
          SSH_DEBUG(5, ("EDB/LDAP: Search already exists for '%s', "
                        "waiting for it to terminate.",
                        search->object_name));

          /* Currently there is an on-going search. We will attach
             to it. */
          if (ssh_cm_edb_operation_link(context, dg, db,
                                        ssh_csstr(connection->identifier),
                                        ssh_cm_edb_ldap_operation_free,
                                        search))
            {
              /* Simple initialization. */
              ssh_cm_edb_mark_search_init_start(db, context, dg);
              ssh_cm_edb_mark_search_init_end(db, context, dg, FALSE);
              status = SSH_CM_SMODE_SEARCH;
            }
          else
            {
              status = SSH_CM_SMODE_FAILED;
            }
          ssh_free(ce);
          goto exit_point;
        }

      /* Set up our search with the LDAP client. */
      if (!ssh_ldap_string_to_filter(filter,
                                     strlen(ssh_csstr(filter)),
                                     &search->filter))
        {
          SSH_DEBUG(SSH_D_NICETOKNOW, ("EDB/LDAP: Filter encode failed."));
          ssh_free(search->object_name);
          ssh_free(search);
          ssh_free(ce);
          goto exit_point;
        }

      /* Initialize the attribute number. */
      p = ps = ssh_strdup(attributes);
      attribute_cnt = 0;
      attribute_alloc = 4;

      attribute_table = ssh_malloc(attribute_alloc * sizeof(char *));
      attribute_len_table = ssh_malloc(attribute_alloc * sizeof(size_t));
      if (attribute_table == NULL || attribute_len_table == NULL)
        {
        attribute_alloc_error:
          SSH_DEBUG(SSH_D_FAIL,
                    ("EDB/LDAP: No space for attributes when doing %s.",
                     connection->identifier));
          ssh_free(attribute_table);
          ssh_free(attribute_len_table);
          ssh_free(search->object_name);
          ssh_free(search);
          ssh_free(ps);
          ssh_free(ce);
          goto exit_point;
        }

      while (1)
        {
          attribute_table[attribute_cnt] = p;
          p = ssh_ustr(strchr(ssh_csstr(p), ','));
          if (p == NULL)
            break;
          *p++ = '\0';
          attribute_len_table[attribute_cnt] =
            strlen(ssh_csstr(attribute_table[attribute_cnt]));
          attribute_cnt++;
          if (attribute_cnt == attribute_alloc)
            {
              void *tmp;
              size_t olditems = attribute_alloc;

              attribute_alloc += 4;
              tmp = ssh_realloc(attribute_table,
                                olditems * sizeof(char *),
                                attribute_alloc * sizeof(char *));
              if (tmp == NULL)
                goto attribute_alloc_error;
              attribute_table = tmp;

              tmp = ssh_realloc(attribute_len_table,
                                olditems * sizeof(size_t),
                                attribute_alloc * sizeof(size_t));
              if (tmp == NULL)
                goto attribute_alloc_error;
              attribute_len_table = tmp;

            }
        }
      attribute_len_table[attribute_cnt] =
        strlen(ssh_csstr(attribute_table[attribute_cnt]));
      attribute_cnt++;

      if (strcmp(ssh_csstr(scope), "one") == 0)
        ldap_scope = SSH_LDAP_SEARCH_SCOPE_SINGLE_LEVEL;
      else if (strcmp(ssh_csstr(scope), "sub") == 0)
        ldap_scope = SSH_LDAP_SEARCH_SCOPE_WHOLE_SUBTREE;
      else
        ldap_scope = SSH_LDAP_SEARCH_SCOPE_BASE_OBJECT;

      connection->search       = search;
      connection->search_freed = FALSE;

      ssh_cm_edb_mark_search_init_start(search->db, search->context,
                                        search->dg);


      ce->connection = connection;
      ce->search = search;
      ce->attribute_cnt = attribute_cnt;
      ce->attribute_table = (unsigned char **)attribute_table;
      ce->attribute_len_table = attribute_len_table;
      ce->ldap_scope = ldap_scope;
      ce->attribute_data = ps;
      ce->ldap = ldap;

      if (connection->state != LDAP_NOT_CONNECTED)
        {
          SSH_DEBUG(SSH_D_HIGHSTART,
                    ("EDB/LDAP: search from server '%s' (old).",
                     connection->identifier));
          cm_edb_ldap_start_search(connection->context,
                                   SSH_LDAP_RESULT_SUCCESS, NULL,
                                   ce);
          cm_ldap_connect_establish_free(FALSE, ce);
        }
      else
        {
          search->msg_id =
            ssh_ldap_client_connect_and_bind(connection->context,
                                             connection->ldap_server_name,
                                             connection->ldap_server_port,
                                             NULL_FNPTR,
                                             (unsigned char *)connection->
                                             bind_name,
                                             connection->bind_name_len,
                                             (unsigned char *)connection->
                                             password,
                                             connection->password_len,
                                             cm_edb_ldap_start_search,
                                             ce);

          SSH_DEBUG(SSH_D_HIGHSTART,
                    ("EDB/LDAP: search from server '%s' (new).",
                     connection->identifier));

          ssh_operation_attach_destructor(search->msg_id,
                                          cm_ldap_connect_establish_free,
                                          ce);

          connection->state = LDAP_CONNECTING;
        }

      ssh_cm_edb_mark_search_init_end(db, context, dg,
                                      connection->search_freed);

      connection->search = NULL;

      if (connection->search_freed == FALSE)
        {
          SSH_DEBUG(SSH_D_MIDOK, ("EDB/LDAP: Search initiated."));

          if (ssh_cm_edb_operation_link(context, dg, db,
                                        ssh_csstr(connection->identifier),
                                        ssh_cm_edb_ldap_operation_free,
                                        search))
            status = SSH_CM_SMODE_SEARCH;
          else
            status = SSH_CM_SMODE_FAILED;
        }
      else
        {
          status = SSH_CM_SMODE_DONE;
        }

      /* Break out from the for loop in case this query was directed
         to single host. */
      if (one_host)
        break;
    }

 exit_point:

  ssh_free(url);
  ssh_free(scheme);
  ssh_free(host);
  ssh_free(port);
  ssh_free(username);
  ssh_free(password);
  ssh_free(path);

  ssh_free(name);
  ssh_free(attributes);
  ssh_free(scope);
  ssh_free(filter);

  /* Free new dg if such allocated */
  if (new_dg)
    ssh_cm_edb_distinguisher_free(new_dg);
  return status;

}

void ssh_cm_edb_ldap_free(SshCMSearchDatabase *db)
{
  SshCMEdbLdap ldap = db->context;

  ssh_adt_destroy(ldap->map);
  ssh_free(ldap);
}

static void cm_edb_ldap_timeout(void *context)
{
  SshCMEdbLdap ldap = context;
  SshADTHandle handle;
  SshCMEdbLdapConnection connection;
  int active_connections = 0;

  if (ldap->cm->config->ldap_connection_idle_timeout == 0)
    return;

  for (handle = ssh_adt_enumerate_start(ldap->map);
       handle != SSH_ADT_INVALID;
       handle = ssh_adt_enumerate_next(ldap->map, handle))
    {
      connection = ssh_adt_get(ldap->map, handle);

      if (connection->state == LDAP_CONNECTED &&
          connection->idle > ldap->cm->config->ldap_connection_idle_timeout)
        {
          ssh_ldap_client_disconnect(connection->context);
          connection->state = LDAP_NOT_CONNECTED;
          continue;
        }
      connection->idle += 10;
      active_connections += 1;
    }
  if (active_connections)
    ssh_register_timeout(&ldap->timeout,
                         10L, 0L,
                         cm_edb_ldap_timeout, ldap);
}

void ssh_cm_edb_ldap_stop(SshCMSearchDatabase *db)
{
  SshCMEdbLdap ldap = db->context;
  SshADTHandle handle;
  SshCMEdbLdapConnection connection;

  /* Cancel connection closing engine and disconnect clients */
  ssh_cancel_timeout(&ldap->timeout);

  for (handle = ssh_adt_enumerate_start(ldap->map);
       handle != SSH_ADT_INVALID;
       handle = ssh_adt_enumerate_next(ldap->map, handle))
    {
      connection = ssh_adt_get(ldap->map, handle);

      ssh_ldap_client_disconnect(connection->context);
      connection->state = LDAP_NOT_CONNECTED;
    }
}

const SshCMSearchFunctionsStruct ssh_cm_edb_ldap_functions =
{
  "ssh.ldap", SSH_CM_SCLASS_SERVER,
  ssh_cm_edb_ldap_search,
  ssh_cm_edb_ldap_stop,
  ssh_cm_edb_ldap_free
};


/* Scan for the next comma */
static size_t skip_comma_sep_token_pos(const char *str)
{
  size_t i;
  Boolean escape = FALSE;
  for (i = 0; str[i] != '\0'; )
    {
      if (escape)
        {
          i++;
          escape = FALSE;
        }
      else
        {
          switch (str[i])
            {
            case ',':
              goto end;
            case '\\':
              escape = TRUE;
              i++;
              break;
            default:
              i++;
              break;
            }
        }
    }
 end:
  return i;
}

/* Get the next token after comma */
static char *skip_comma_sep_token(const char *str)
{
  size_t pos;
  if (str == NULL)
    return NULL;
  pos = skip_comma_sep_token_pos(str);
  if (str[pos] != '\0')
    pos++;
  return &((char *)str)[pos];
}

Boolean get_comma_sep_token(const char *str,
                            char **ret_server, char **ret_username,
                            char **ret_password)
{
  size_t pos;
  unsigned char *tmp = NULL;
  unsigned char *scheme = NULL, *username = NULL, *password = NULL;
  unsigned char *host = NULL, *port = NULL, *path = NULL;
  Boolean rv = TRUE;

  if (str == NULL)
    return FALSE;

  /* Initialize to dummy values. */
  *ret_server   = NULL;
  *ret_username = NULL;
  *ret_password = NULL;

  /* Seek to the end of the token */
  pos = skip_comma_sep_token_pos(str);
  if (pos == 0)
    return FALSE;

  /* ... memdup shall set the last char to '\0' */
  if ((tmp = ssh_memdup(str, pos)) == NULL)
    return FALSE;

  /* Check for ldap schema in server address and append it if necessary. */
  if (strncmp(ssh_csstr(tmp), "ldap://", 7) != 0)
    {
      unsigned char *tmp2 = tmp;

      ssh_dsprintf((char **) &tmp, "ldap://%s", tmp2);
      ssh_free(tmp2);
      if (tmp == NULL)
        return FALSE;
    }

  if (ssh_url_parse_relaxed(tmp, &scheme, &host, &port, &username, &password,
                            &path) == FALSE)
    {
      if (host == NULL)
        rv = FALSE;

      *ret_server   = ssh_sstr(host);
      *ret_username = NULL;
      *ret_password = NULL;

      ssh_free(tmp);
      ssh_free(scheme);
      ssh_free(port);
      ssh_free(username);
      ssh_free(password);
      ssh_free(path);

      return rv;
    }

  /* Return those values that have some use. */
  if (port == NULL)
    port = ssh_strdup("389");

  if (host != NULL)
    {
      if ((ssh_dsprintf(ret_server, "%s://%s:%s",
                        scheme ? scheme : ssh_custr("ldap"),
                        host, port)) == -1)
        rv = FALSE;

      if (username)
        {
          if ((*ret_username = ssh_strdup(username)) == NULL)
            rv = FALSE;
        }

      if (password)
        {
          if ((*ret_password = ssh_strdup(password)) == NULL)
            rv = FALSE;
        }

      /* Check the rest, for consistency. */
      if (rv && (scheme != NULL && strcmp(ssh_csstr(scheme), "ldap") != 0))
        {
          rv = FALSE;
        }
    }

  ssh_free(scheme);
  ssh_free(host);
  ssh_free(port);
  ssh_free(username);
  ssh_free(password);
  ssh_free(path);
  ssh_free(tmp);

  return rv;
}

Boolean ssh_cm_edb_ldap_add(SshCMContext cm,
                            const char *default_servers)
{
  SshCMEdbLdap ldap;
  SshCMSearchDatabase *database;
  unsigned char *server = NULL;
  char *password = NULL, *bind_name = NULL;
  Boolean rv = TRUE;

  database =
    ssh_cm_edb_lookup_database(cm, ssh_cm_edb_ldap_functions.db_identifier);

  if (!database)
    {
      SSH_DEBUG(SSH_D_MIDOK, ("EDB/LDAP: Adding new LDAP backend."));

      /* Allocate the ldap context for the method. */
      if ((ldap = ssh_calloc(1, sizeof(*ldap))) == NULL)
        return FALSE;

      ldap->cm = cm;

      /* Initialize the mapping. */
      if ((ldap->map =
           ssh_adt_create_generic(
               SSH_ADT_MAP,
               SSH_ADT_HASH,    cm_ldap_connection_hash,
               SSH_ADT_COMPARE, cm_ldap_connection_compare,
               SSH_ADT_DESTROY, cm_ldap_connection_destroy,
               SSH_ADT_HEADER,
               SSH_ADT_OFFSET_OF(SshCMEdbLdapConnectionStruct, adt_header),
               SSH_ADT_ARGS_END)) == NULL)
        {
          ssh_free(ldap);
          return FALSE;
        }
    }
  else
    {
      SSH_DEBUG(SSH_D_LOWOK, ("EDB/LDAP: Database already exists."));
      ldap = database->context;
    }

  /* Remark. This code might need changing to be more careful in the
     checking of the data in the hash table. At the moment only
     the `host' and `port' are used to identify a ldap server---and
     this may not be what one would like in somecases. */

  for (;
       get_comma_sep_token(default_servers,
                           (char **) &server, &bind_name, &password);
       default_servers = skip_comma_sep_token(default_servers))
    {
      SshCMEdbLdapConnectionStruct *connection, probe;
      SshADTHandle h;

      probe.identifier = server;
      if ((h = ssh_adt_get_handle_to_equal(ldap->map, &probe))
          != SSH_ADT_INVALID)
        {
          ssh_free(server);
          ssh_free(bind_name);
          ssh_free(password);
          continue;
        }

      SSH_DEBUG(SSH_D_MIDOK, ("EDB/LDAP: Making connection to '%s'.", server));

      connection =
        cm_ldap_connection_create(cm, ssh_cm_edb_get_local_network(cm),
                                  server, bind_name, password);
      if (connection)
        {
          SSH_DEBUG(SSH_D_NICETOKNOW,
                    ("Created ldap client for connecting to server: %s",
                     server));
          (void )ssh_adt_insert(ldap->map, connection);
        }
      else
        {
          SSH_DEBUG(SSH_D_NICETOKNOW, ("Connection to %s failed.", server));
          ssh_free(server);
          ssh_free(bind_name);
          ssh_free(password);
          rv = FALSE;
        }
    }

  /* Set up the servers etc. */
  if (!database)
    ssh_cm_edb_add_database(cm, &ssh_cm_edb_ldap_functions, ldap);

  return rv;
}


Boolean ssh_cm_edb_ldap_init(SshCMContext cm,
                             const char *default_servers)
{
  ssh_cm_edb_remove_database(cm, ssh_cm_edb_ldap_functions.db_identifier);
  return ssh_cm_edb_ldap_add(cm, default_servers);
}

#endif /* SSHDIST_VALIDATOR_LDAP */
