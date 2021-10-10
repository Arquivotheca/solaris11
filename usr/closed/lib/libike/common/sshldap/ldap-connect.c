/*
  File: ldap-connect.c

  Description:
        Connect to the LDAP server.

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
        Helsinki, Finland.
        All rights reserved.
*/

#include "sshincludes.h"
#include "sshldap.h"
#include "ldap-internal.h"

#define SSH_DEBUG_MODULE "SshLdapConnect"

/* Callback to be called when connection is finished */
void ssh_ldap_connect_callback(SshTcpError error,
                               SshStream stream,
                               void *context)
{
  SshLdapClientOperation op = context;
  SshLdapClient client = op->client;

  op->suboperation = NULL;
  if (error != SSH_TCP_OK)
    {
      if (op->connect_cb)
        (*op->connect_cb)(client, error, op->connect_cb_context);
      ssh_ldap_client_disconnect(client);
      return;
    }
  else
    {
      client->connected = TRUE;

      if (client->stream_wrap)
        client->ldap_stream =
          (*client->stream_wrap)(client,
                                 SSH_LDAP_RESULT_SUCCESS,
                                 NULL,
                                 stream,
                                 client->stream_wrap_context);
      else
        client->ldap_stream = stream;

      if (op->connect_cb)
        (*op->connect_cb)(client, SSH_TCP_OK, op->connect_cb_context);

      ssh_stream_set_callback(client->ldap_stream,
                              ssh_ldap_stream_callback,
                              client);
    }
  ssh_ldap_free_operation(client, op);
}

SshOperationHandle
ssh_ldap_client_connect(SshLdapClient client,
                        const char *server_name,
                        const  char *server_port,
                        SshLdapConnectCB callback, void *callback_context)
{
  SshTcpConnectParamsStruct tcp_connect_params;
  SshOperationHandle h;
  SshLdapClientOperation op;
  SshLdapResultInfoStruct info;

  memset(&info, 0, sizeof(info));

  if (server_name == NULL)
    server_name = SSH_LDAP_DEFAULT_SERVER_NAME;
  if (server_port == NULL)
    server_port = SSH_LDAP_DEFAULT_SERVER_PORT;

  SSH_DEBUG(SSH_D_MIDSTART,
            ("connect(0x%x) server=%s:%s", client, server_name, server_port));

  memset(&tcp_connect_params, 0, sizeof(tcp_connect_params));
  tcp_connect_params.socks_server_url = ssh_ustr(client->socks);
  tcp_connect_params.connection_attempts = client->connection_attempts;

  op = ssh_ldap_new_operation(client,
                              SSH_LDAP_OPERATION_CONNECT, NULL_FNPTR, NULL);

  if (op)
    {
      op->connect_cb = callback;
      op->connect_cb_context = callback_context;

      ssh_free(client->current_server_name);
      client->current_server_name = ssh_strdup(server_name);

      ssh_free(client->current_server_port);
      client->current_server_port = ssh_strdup(server_port);

      h = ssh_tcp_connect(ssh_custr(server_name), ssh_custr(server_port),
                          &tcp_connect_params,
                          ssh_ldap_connect_callback, op);
      if (h)
        op->suboperation = h;

      return op->operation;
    }
  else
    {
      MAKEINFO(&info, "Can't start connect operation, client is busy.");
      ssh_ldap_result(client, op, SSH_LDAP_RESULT_INTERNAL, &info);
      return NULL;
    }
}

static void ldap_client_close_connection(void *context)
{
  SshLdapClient client = context;

  if (client->ldap_stream)
    {
      ssh_stream_output_eof(client->ldap_stream); /* XXX Unnecessary? */
      ssh_stream_destroy(client->ldap_stream);
      client->ldap_stream = NULL;
    }
}

void ssh_ldap_client_disconnect(SshLdapClient client)
{
  client->connected = FALSE;

  SSH_DEBUG(SSH_D_MIDSTART,
            ("disconnect(0x%x) server=%s:%s",
             client,
             client->current_server_name, client->current_server_port));

  ssh_free(client->current_server_name);
  client->current_server_name = NULL;
  ssh_free(client->current_server_port);
  client->current_server_port = NULL;

  /* After this callbacks will not be called. Close the stream from
     the bottom of the event loop, to allow streams to flush abandon
     messages possibly send. */
  ssh_ldap_abort_all_operations(client);
  ldap_client_close_connection(client);
}
