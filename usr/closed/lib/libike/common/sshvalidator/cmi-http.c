/*
  cmi-http.c

  Copyright:
        Copyright (c) 2002, 2003 SFNT Finland Oy.
	All rights reserved.

  Validator HTTP backend.
*/

#include "sshincludes.h"
#include "cmi.h"
#include "cmi-edb.h"
#include "cmi-debug.h"
#include "sshurl.h"
#include "sshhttp.h"

#define SSH_DEBUG_MODULE "SshCertEdbHttp"

#ifdef SSHDIST_VALIDATOR_HTTP
/* Implementation of the HTTP external database client. */

typedef struct
{
  SshCMSearchDatabase  *db;
  SshCMDBDistinguisher *dg;
  void                 *context;
  unsigned char        *url;
  SshStream             stream;
  SshBufferStruct       buffer;
  SshOperationHandle    op_handle;
  unsigned long         tc;
} SshCMEdbHttpSearch;

typedef struct
{
  SshHttpClientContext client;
  SshHttpClientParams *params;

  /* Waiting, or directly done? */
  void                *search;
  Boolean              search_freed;

  /* Basically searches should be stored here, but we don't do that
     yet. */

} SshCMEdbHttp;

void ssh_cm_edb_http_free_search(SshCMEdbHttpSearch *search)
{
  if (search->stream)
    ssh_stream_destroy(search->stream);

  ssh_free(search->url);
  ssh_buffer_uninit(&search->buffer);
  ssh_free(search);
}

void ssh_cm_edb_http_stream_cb(SshStreamNotification notification,
                               void *context)
{
  SshCMEdbHttpSearch *search = context;
  int l;
  unsigned char buf[1024];

  while (1)
    {
      l = ssh_stream_read(search->stream, buf, sizeof(buf));
      if (l == 0)
        {
          /* Throw a reply. */
          ssh_cm_edb_reply(search->db, search->context, search->dg,
                           ssh_buffer_ptr(&search->buffer),
                           ssh_buffer_len(&search->buffer));

          ssh_cm_edb_operation_msg(search->context, search->dg,
                                   search->db->functions->db_identifier,
                                   SSH_CMDB_STATUS_OK);
          return;
        }
      if (l < 0)
        {
          /* Would block. */
          return;
        }

      /* Append the buffer. */
      if ((ssh_buffer_append(&search->buffer, buf, l)) != SSH_BUFFER_OK)
        {
          ssh_stream_destroy(search->stream);
          ssh_cm_edb_reply(search->db, search->context, search->dg, NULL, 0);
          return;
        }
    }
}

void ssh_cm_edb_http_result(SshHttpClientContext http_ctx,
                            SshHttpResult        result,
                            SshTcpError          ip_error,
                            SshStream            stream,
                            void                *ctx)
{
  SshCMEdbHttpSearch *search = ctx;
  SshCMEdbHttp       *glob   = search->db->context;

  /* Determine direct reply. */
  if (glob->search == search)
    glob->search_freed = TRUE;

  SSH_DEBUG(6, ("BAN: banning the search."));
  ssh_cm_edb_ban_add_ctx(search->context, search->dg,
                         search->db->functions->db_identifier);

  if (result != SSH_HTTP_RESULT_SUCCESS)
    {
      SSH_DEBUG(3, ("Error: %s", ssh_http_error_code_to_string(result)));

      if (search->op_handle)
        {
          search->op_handle = NULL;
          ssh_cm_edb_operation_msg(search->context,
                                   search->dg,
                                   search->db->functions->db_identifier,
                                   SSH_CMDB_STATUS_FAILED);
        }
      else
        search->op_handle = NULL;
    }
  else
    {
      /* Some debugging. */
      SSH_DEBUG(5, ("Content-type: %s",
                    ssh_http_get_header_field(http_ctx, "content-type")));
      search->stream = stream;
      ssh_stream_set_callback(stream, ssh_cm_edb_http_stream_cb,
                              search);
      ssh_cm_edb_http_stream_cb(SSH_STREAM_INPUT_AVAILABLE, search);
    }
}

/* The time control routines, very rough at the moment. */

#if 0
SshCMTimeControlState ssh_cm_edb_http_tc_state(SshCMTimeControl tc,
                                               void *context)
{
  SshCMEdbHttpSearch *search = context;
  if (search != NULL)
    {
      ssh_operation_abort(search->op_handle);
      ssh_cm_edb_result(search->db, SSH_CMDB_STATUS_TIMEOUT,
                        search->context, search->dg);
      ssh_cm_edb_http_free_search(search);
    }
  return SSH_CM_TC_FREE;
}

void ssh_cm_edb_http_tc_free(SshCMTimeControl tc,
                             void *context)
{
  SshCMEdbHttpSearch *search = context;
  /* Check if we have actually finished. */
  if (search != NULL)
    {
      ssh_operation_abort(search->op_handle);
      ssh_cm_edb_result(search->db, SSH_CMDB_STATUS_TIMEOUT,
                        search->context, search->dg);
      ssh_cm_edb_http_free_search(search);
    }
}

SshCMTimeControlOpStruct ssh_cm_edb_http_time_controls =
{
  "ssh.http",
  ssh_cm_edb_http_tc_state,
  ssh_cm_edb_http_tc_free
};

#endif

void ssh_cm_edb_http_operation_free(void *context,
                                    void *search_context)
{
  SshCMEdbHttpSearch *search = search_context;

  if (search->op_handle)
    {
      ssh_operation_abort(search->op_handle);
      search->stream = NULL;
    }

  ssh_cm_edb_http_free_search(search);
}

static Boolean is_http(const unsigned char *str)
{
  Boolean rv;
  unsigned char *scheme = NULL;

  if (str == NULL ||
      ssh_url_parse(str, &scheme, NULL, NULL, NULL, NULL, NULL) == FALSE)
    {
      ssh_free(scheme);
      return FALSE;
    }
  rv = (scheme == NULL ? FALSE :
        strcasecmp(ssh_csstr(scheme), "http") != 0 ? FALSE :
        TRUE);
  ssh_free(scheme);
  return rv;
}

SshCMSearchMode ssh_cm_edb_http_search(SshCMSearchDatabase  *db,
                                       SshCMContext          cm,
                                       void                 *context,
                                       SshCMDBDistinguisher *dg)
{
  SshCMEdbHttp *http_ctx = db->context;
  SshCMEdbHttpSearch *search;
  unsigned char *url;
  SshCMDBDistinguisher *new_dg = NULL;

  /* Allocate the http client. */
  if (http_ctx->client == NULL)
    {
      if ((http_ctx->client = ssh_http_client_init(http_ctx->params)) == NULL)
        {
          return SSH_CM_SMODE_FAILED;
        }
    }

  if (dg->edb_conversion_function)
    {
      if (!(*dg->edb_conversion_function)(db, cm, dg, &new_dg,
                                          dg->edb_conversion_function_context))
        return SSH_CM_SMODE_FAILED;

      /* Start using the new dg */
      if (new_dg != NULL)
        dg = new_dg;
    }

  /* Check that the key suggested is really an URL. */
  if (dg->key_type != SSH_CM_KEY_TYPE_URI)
    {
      if (new_dg) ssh_cm_edb_distinguisher_free(new_dg);
      return SSH_CM_SMODE_FAILED;
    }

  /* Generate suitable search string. */
  url = ssh_memdup(dg->key, dg->key_length);
  if (!is_http(url))
    {
      ssh_free(url);
      if (new_dg) ssh_cm_edb_distinguisher_free(new_dg);
      return SSH_CM_SMODE_FAILED;
    }

  /* Check for a ban. */
  if (ssh_cm_edb_ban_check(cm, dg, db->functions->db_identifier) == TRUE)
    {
      ssh_free(url);
      if (new_dg) ssh_cm_edb_distinguisher_free(new_dg);
      return SSH_CM_SMODE_FAILED;
    }

  /* Allocate search context. */
  if ((search = ssh_malloc(sizeof(*search))) == NULL)
    {
      ssh_free(url);
      if (new_dg) ssh_cm_edb_distinguisher_free(new_dg);
      return SSH_CM_SMODE_FAILED;
    }

  search->db        = db;
  search->dg        = dg;
  search->context   = context;
  search->url       = url;
  search->stream    = NULL;
  search->op_handle = NULL;
  search->tc        = 0;

  /* Allocate the buffer. */
  ssh_buffer_init(&search->buffer);

  /* Determine whether there is a search already for the same
     data. */
  if (ssh_cm_edb_operation_check(context, dg,
                                 db->functions->db_identifier) == TRUE)
    {
      SSH_DEBUG(5, ("Http search already exists for '%s', waiting "
                    "for it to terminate.", url));
      /* Currently there is an on-going search. We will attach to it. */
      if (ssh_cm_edb_operation_link(context,
                                    dg, db, db->functions->db_identifier,
                                    ssh_cm_edb_http_operation_free,
                                    search))
        {
          /* Simple initialization. */
          ssh_cm_edb_mark_search_init_start(db, context, dg);
          ssh_cm_edb_mark_search_init_end  (db, context, dg, FALSE);
          return SSH_CM_SMODE_SEARCH;
        }
      else
        return SSH_CM_SMODE_FAILED;
    }

  SSH_DEBUG(5, ("Http search of '%s'.", url));

  /* Make sure that we can handle the case when the search
     immediately returns. */
  http_ctx->search       = search;
  http_ctx->search_freed = FALSE;

  ssh_cm_edb_mark_search_init_start(search->db, search->context, search->dg);

  /* We don't know whether the URL is valid, or whether it is even
     a string, however, hopefully the http client code checks these. */
  search->op_handle =
    ssh_http_get(http_ctx->client, ssh_csstr(url), ssh_cm_edb_http_result,
                 search,
                 SSH_HTTP_HDR_END);

  ssh_cm_edb_mark_search_init_end(db, context, dg, http_ctx->search_freed);

  http_ctx->search       = NULL;

  if (http_ctx->search_freed == FALSE)
    {
      /* Register time control for the search function. */
      if (ssh_cm_edb_operation_link(context,
                                    dg, db, db->functions->db_identifier,
                                    ssh_cm_edb_http_operation_free,
                                    search))
        {
          if (new_dg) ssh_cm_edb_distinguisher_free(new_dg);
          return SSH_CM_SMODE_SEARCH;
        }
      else
        {
          return SSH_CM_SMODE_FAILED;
        }
    }

  /* Free new dg if such allocated */
  if (new_dg)
    ssh_cm_edb_distinguisher_free(new_dg);
  return SSH_CM_SMODE_DONE;
}


void ssh_cm_edb_http_free(SshCMSearchDatabase *db)
{
  SshCMEdbHttp *context = db->context;

  SSH_DEBUG(SSH_D_HIGHSTART, ("Freeing HTTP"));
  /* Simply free the client. */
  if (context->client)
    ssh_http_client_uninit(context->client);

  /* Free the params. */
  ssh_free(context->params->socks);
  ssh_free(context->params->http_proxy_url);
  ssh_free(context->params);
  ssh_free(context);
}

void ssh_cm_edb_http_stop(SshCMSearchDatabase *db)
{
  SshCMEdbHttp *context = db->context;

  SSH_DEBUG(SSH_D_HIGHSTART, ("Stopping HTTP"));
  /* Simply free the client. */
  if (context->client)
    {
      ssh_http_client_uninit(context->client);
      context->client = NULL;
    }
}

const SshCMSearchFunctionsStruct ssh_cm_edb_http_functions =
{
  "ssh.http", SSH_CM_SCLASS_SERVER,
  ssh_cm_edb_http_search,
  ssh_cm_edb_http_stop,
  ssh_cm_edb_http_free
};

Boolean ssh_cm_edb_http_init(SshCMContext cm)
{
  SshCMEdbHttp *context;
  SshCMLocalNetwork net;

  if (ssh_cm_edb_lookup_database(cm,
                                 ssh_cm_edb_http_functions.db_identifier))
    return TRUE;

  /* Allocate the context for the method. */
  if ((context = ssh_malloc(sizeof(*context))) == NULL)
    return FALSE;

  /* Create parameters. */
  if ((context->params = ssh_calloc(1, sizeof(*context->params))) == NULL)
    {
      ssh_free(context);
      return FALSE;
    }

  context->search       = NULL;
  context->search_freed = FALSE;

  /* Take the local net. */
  net = ssh_cm_edb_get_local_network(cm);

  /* Make a copy. */
  if (net->socks)
    context->params->socks = ssh_strdup(net->socks);
  if (net->proxy)
    context->params->http_proxy_url = ssh_strdup(net->proxy);

#ifdef SUNWIPSEC
  /* Hack until we can work out a better interface with SafeNet. */
  context->params->num_redirections = 3;
#endif

  /* Ignore user_name and password for now. We also let the http code
     to use defauls here, it may be later productive to add
     configurability for that too. Perhaps even take the http params
     directly (although that loses the help of local network
     information, which isn't that much of a loss). */

  context->client = NULL;

  /* Basically we are now done. */
  ssh_cm_edb_add_database(cm, &ssh_cm_edb_http_functions, context);
  return TRUE;
}
#endif /* SSHDIST_VALIDATOR_HTTP */

/* cmi-http.c */
