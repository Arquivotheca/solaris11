/*

Authors: Tatu Ylonen <ylo@ssh.fi>
         Antti Huima <huima@ssh.fi>
         Sami Lehtinen <sjl@ssh.fi>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
All rights reserved.

Interface to sockets.

*/

#include "sshincludes.h"
#include "sshstream.h"
#include "sshnameserver.h"
#include "sshtcp.h"
#include "sshinet.h"
#include "sshbuffer.h"
#include "sshsocks.h"
#include "sshurl.h"
#include "sshtimeouts.h"
#include "sshfsm.h"

#define SSH_DEBUG_MODULE "SshTcp"

/* A context used to track SOCKS server connection status. */

typedef struct {
  /* Information about local binding. */
  unsigned char *local_address;
  unsigned int local_port;
  SshTcpReusableType local_reusable;

  /* Information about the target host. */
  unsigned char *host_name;             /* host to connect to. */
  unsigned char *host_addresses;        /* addresses for the host to connect */
  const unsigned char *next_address;    /* next address to try */
  unsigned int host_port;               /* port to connect on the host */
  SshUInt32 protocol_mask;              /* protocols used in connect */

  /* User callback. */
  SshTcpCallback user_callback;
  void *user_context;

  /* Miscellaneous request information. */
  unsigned int connection_attempts;
  unsigned int attempts_done;

  /* Information about the socks server. */
  unsigned char *socks_host;            /* socks server host */
  unsigned char *socks_exceptions;      /* exceptions when to use socks */
  unsigned char *socks_addresses;       /* socks server addresses */
  unsigned char *socks_next_address;    /* next address to try */
  unsigned int socks_port;              /* socks port */
  unsigned char *user_name;             /* user requesting connection */
  SshBuffer socks_buf;                  /* Socks buffer */
  SshTcpSocksType socks_type;           /* Socks type */
  /* An open stream to either the socks server or the final destination. */
  SshStream stream;

  /* Lower level operation handle. If this is set we are in the middle of the
     asyncronous call */
  SshOperationHandle handle;
  /* Upper level operation handle. If this is set we started any asyncronous
     call, and the lower level must free this structure. If this is NULL then
     we are in the syncronous code path and the ssh_tcp_connect will take care
     of the freeing of this structure */
  SshOperationHandle upper_handle;

  SshFSMStepCB next_state;
  SshTcpError error;
  SshFSM fsm;
  SshFSMThread thread;
  SshTimeoutStruct timeout;
} *ConnectContext;

SSH_FSM_STEP(tcp_connect_start);
SSH_FSM_STEP(tcp_connect_host_lookup);
SSH_FSM_STEP(tcp_connect_host_connect);
SSH_FSM_STEP(tcp_connect_socks_lookup);
SSH_FSM_STEP(tcp_connect_socks_connect);
SSH_FSM_STEP(tcp_connect_socks_send);
SSH_FSM_STEP(tcp_connect_socks_receive_read_byte);
SSH_FSM_STEP(tcp_connect_socks_receive_method);
SSH_FSM_STEP(tcp_connect_socks_receive);
SSH_FSM_STEP(tcp_connect_socks_error);
SSH_FSM_STEP(tcp_connect_socks_receive_method);
SSH_FSM_STEP(tcp_connect_finish);
SSH_FSM_STEP(tcp_connect_abort);
SSH_FSM_STEP(tcp_connect_cleanup);

/* Connects to the given address/port, and makes a stream for it.
   The address to use is the first address from the list.  This
   function is defined in the machine-specific file. */
SshOperationHandle ssh_socket_low_connect(const unsigned char *local_address,
                                          unsigned int local_port,
                                          SshTcpReusableType local_reusable,
                                          const unsigned char *address_list,
                                          unsigned int port,
                                          SshTcpCallback callback,
                                          void *context);

/* Destroys the connection context. */
void tcp_connect_destroy_ctx(ConnectContext c);

/* Remove addresses that don't match the protocol match from the
   address list.  Overwrites the original list.  Can also return an
   empty string. */
void ssh_remove_non_matching_addresses_from_list(unsigned char *address,
                                                 SshUInt32 protocol_mask);

/* Return TRUE, if address os of type that is specified in the
   protocol mask. */
Boolean ssh_address_type_matches_protocol_mask(unsigned char *address,
                                               SshUInt32 protocol_mask);

/* Connection timed out */
void tcp_connect_time_out(void *context)
{
  ConnectContext c = context;

  c->error = SSH_TCP_TIMEOUT;
  ssh_fsm_set_next(c->thread, tcp_connect_finish);
  ssh_fsm_continue(c->thread);
}

/* Connection aborted out */
void ssh_tcp_connect_aborted(void *context)
{
  ConnectContext c = (ConnectContext) context;

  if (c->handle)
    {
      ssh_operation_abort(c->handle);
      c->handle = NULL;
    }
  /* Make sure we don't receive timeouts or call the user callback after
     we have been aborted. */
  c->user_callback = NULL_FNPTR;
  ssh_cancel_timeouts(tcp_connect_time_out, c);
  ssh_fsm_set_next(c->thread, tcp_connect_abort);
  ssh_fsm_continue(c->thread);
}

/* Opens a connection to the specified host, and calls the callback
   when the connection has been established or has failed.  If
   connecting is successful, the callback will be called with error
   set to SSH_TCP_OK and an SshStream object for the connection passed
   in in the stream argument.  Otherwise, error will indicate the
   reason for the connection failing, and the stream will be NULL.

   Note that the callback may be called either during this
   call or some time later.

   Returns SshOperationHandle that can be used to abort the tcp open.

   The `host_name_or_address' argument may be a numeric IP address or a
   host name (domain name), in which case it is looked up from the name
   servers.

   The params structure can either be NULL or memset to zero to get default
   parameters. All data inside the params is copied during this call, so it can
   be freed immediately when this function returns. */
SshOperationHandle ssh_tcp_connect(const unsigned char *host_name_or_address,
                                   const unsigned char *port_or_service,
                                   const SshTcpConnectParams params,
                                   SshTcpCallback callback,
                                   void *context)
{
  ConnectContext c;

  c = ssh_calloc(1, sizeof(*c));
  if (c == NULL)
    {
      SSH_DEBUG(1, ("Failed to allocate TCP connection context."));
      (*callback)(SSH_TCP_FAILURE, NULL, context);
      return NULL;
    }

  if (params && params->local_address)
    {
      c->local_address = ssh_strdup(params->local_address);
      if (c->local_address == NULL)
        {
        error_local:
          (*callback)(SSH_TCP_FAILURE, NULL, context);
          tcp_connect_destroy_ctx(c);
          return NULL;
        }

      if (params->local_port_or_service)
        {
          c->local_port
            = ssh_inet_get_port_by_service(params->local_port_or_service,
                                           ssh_custr("tcp"));
          if (c->local_port == 0)
            goto error_local;
        }
      c->local_reusable = params->local_reusable;
    }

  c->host_name = ssh_strdup(host_name_or_address);
  c->host_port = ssh_inet_get_port_by_service(port_or_service,
                                              ssh_custr("tcp"));
  c->host_addresses = NULL;
  c->next_address = NULL;

  if (c->host_name == NULL || c->host_port == 0)
    {
      (*callback)(SSH_TCP_FAILURE, NULL, context);
      tcp_connect_destroy_ctx(c);
      return NULL;
    }

  if (params && (params->protocol_mask != 0))
    c->protocol_mask = params->protocol_mask;
  else
    c->protocol_mask = (~0);

  c->user_callback = callback;
  c->user_context = context;
  if (params && params->connection_timeout != 0)
    {
      ssh_register_timeout(&c->timeout,
                           params->connection_timeout, 0,
                           tcp_connect_time_out, c);
    }

  c->connection_attempts = 1;
  if (params && params->connection_attempts != 0)
    c->connection_attempts = params->connection_attempts;

  c->attempts_done = 0;

  c->stream = NULL;

  /* Initialize socks-related data. */
  if (params &&
      params->socks_server_url != NULL &&
      strcmp(ssh_csstr(params->socks_server_url), "") != 0)
    {
      unsigned char *scheme, *port;

      if (ssh_url_parse_and_decode_relaxed(params->socks_server_url, &scheme,
                                           &(c->socks_host), &port,
                                           &(c->user_name), NULL,
                                           &(c->socks_exceptions)))
        {
          if (scheme != NULL && strcmp(ssh_csstr(scheme), "socks") != 0)
            ssh_warning("Socks server scheme not socks");
          if (scheme != NULL)
            ssh_free(scheme);

          if (c->socks_host != NULL)
            {
              if ((c->socks_buf = ssh_buffer_allocate()) == NULL)
                {
                  (*callback)(SSH_TCP_FAILURE, NULL, context);
                  tcp_connect_destroy_ctx(c);
                  return NULL;
                }
              c->socks_addresses = NULL;
              if (port == NULL || strcmp(ssh_csstr(port), "") == 0)
                c->socks_port = 1080; /* The standard socks port. */
              else
                c->socks_port = ssh_inet_get_port_by_service(port,
                                                             ssh_custr("tcp"));
            }
          if (port != NULL)
            ssh_free(port);
        }
      else
        {
          ssh_warning("Socks server URL is malformed.");
        }
    }
  else
    c->socks_host = NULL;

  if (params)
    c->socks_type = params->socks_type;

  c->upper_handle = NULL;
  c->handle = NULL;

  c->fsm = ssh_fsm_create(c);
  if (c->fsm == NULL)
    {
      SSH_DEBUG(2, ("Creating FSM failed."));
      (*callback)(SSH_TCP_FAILURE, NULL, context);
      tcp_connect_destroy_ctx(c);
      return NULL;
    }
  c->thread = ssh_fsm_thread_create(c->fsm, tcp_connect_start,
                                    NULL_FNPTR, NULL_FNPTR, NULL);
  if (c->thread == NULL)
    {
      SSH_DEBUG(2, ("Creating thread failed."));
      (*callback)(SSH_TCP_FAILURE, NULL, context);
      ssh_fsm_destroy(c->fsm);
      tcp_connect_destroy_ctx(c);
      return NULL;
    }
  c->upper_handle = ssh_operation_register(ssh_tcp_connect_aborted, c);
  return c->upper_handle;
}

/* Destroys the connection context. */
void tcp_connect_destroy_ctx(ConnectContext c)
{
  SSH_DEBUG(4, ("Destroying ConnectContext..."));
  SSH_PRECOND(c != NULL);
  if (c->handle)
    ssh_operation_abort(c->handle);

  ssh_cancel_timeout(&c->timeout);
  ssh_free(c->local_address);
  ssh_free(c->host_name);
  ssh_free(c->host_addresses);
  ssh_free(c->socks_host);
  ssh_free(c->socks_addresses);
  ssh_free(c->user_name);
  ssh_free(c->socks_exceptions);
  if (c->socks_buf)
    ssh_buffer_free(c->socks_buf);
  if (c->stream)
    ssh_stream_destroy(c->stream);
  if (c->upper_handle)
    ssh_operation_unregister(c->upper_handle);

  ssh_free(c);
}

Boolean tcp_connect_register_failure(SshFSMThread thread, SshTcpError error)
{
  ConnectContext c = (ConnectContext) ssh_fsm_get_gdata(thread);

  c->attempts_done++;
  if (c->attempts_done < c->connection_attempts)
    return FALSE;

  c->error = error;
  ssh_fsm_set_next(thread, tcp_connect_finish);
  return TRUE;
}

SSH_FSM_STEP(tcp_connect_start)
{
  SSH_FSM_SET_NEXT(tcp_connect_host_lookup);
  return SSH_FSM_CONTINUE;
}

/* This callback is called when the host addresses have been looked up. */
void tcp_connect_host_lookup_cb(SshTcpError error,
                                const unsigned char *result,
                                void *context)
{
  SshFSMThread thread = (SshFSMThread) context;
  ConnectContext c = (ConnectContext) ssh_fsm_get_gdata(thread);
  unsigned char *addrs = NULL;

  SSH_DEBUG(10, ("Got error %d, result = `%s'.", error,
                 result ? result : ssh_custr("NULL")));
  c->handle = NULL;
  if (error == SSH_TCP_OK)
    {
      if ((addrs = ssh_strdup(result)) == NULL)
        {
          error = SSH_TCP_FAILURE;
        }
      else
        {
          ssh_remove_non_matching_addresses_from_list(addrs, c->protocol_mask);
          if (strlen(ssh_csstr(addrs)) == 0)
            {
              ssh_free(addrs);
              addrs = NULL;
              error = SSH_TCP_NO_ADDRESS;
            }
        }
    }
  if (error != SSH_TCP_OK)
    {
      if (c->socks_type == SSH_TCP_SOCKS5 && c->socks_host)
        {
          SSH_DEBUG(2, ("Couldn't resolve client address, trying to connect "
                        "with SOCKS5 server."));
          SSH_FSM_CONTINUE_AFTER_CALLBACK(thread);
          return;
        }
      SSH_FSM_SET_NEXT(tcp_connect_host_lookup);
      tcp_connect_register_failure(thread, error);
      SSH_FSM_CONTINUE_AFTER_CALLBACK(thread);
      return;
    }

  c->host_addresses = addrs;
  c->next_address = c->host_addresses;

  SSH_FSM_CONTINUE_AFTER_CALLBACK(thread);
}

SSH_FSM_STEP(tcp_connect_host_lookup)
{
  ConnectContext c = (ConnectContext) fsm_context;

  if (c->socks_host)
    SSH_FSM_SET_NEXT(tcp_connect_socks_lookup);
  else
    SSH_FSM_SET_NEXT(tcp_connect_host_connect);

  SSH_DEBUG(10, ("Starting address lookup for host `%s'.", c->host_name));

  SSH_FSM_ASYNC_CALL(c->handle = ssh_tcp_get_host_addrs_by_name
                     (c->host_name, tcp_connect_host_lookup_cb,
                      thread));
}

void tcp_connect_socks_lookup_cb(SshTcpError error,
                                 const unsigned char *result, void *context)
{
  SshFSMThread thread = (SshFSMThread) context;
  ConnectContext c = (ConnectContext) ssh_fsm_get_gdata(thread);

  c->handle = NULL;
  if (error != SSH_TCP_OK)
    {
      SSH_DEBUG(0, ("Couldn't resolve IP for SOCKS server `%s'.",
                    c->socks_host));
      tcp_connect_register_failure(thread, error);
      SSH_FSM_CONTINUE_AFTER_CALLBACK(thread);
      return;
    }

  /* Save the lookup result. */
  if ((c->socks_addresses = ssh_strdup(result)) == NULL)
    {
      if (tcp_connect_register_failure(thread, error))
        {
          SSH_FSM_CONTINUE_AFTER_CALLBACK(thread);
          return;
        }
    }

  ssh_free(c->socks_next_address);
  c->socks_next_address = c->socks_addresses;

  /* XXX Make the following a separate state. */
  /* Enter the next state. */
  if (c->socks_exceptions &&
      /* If we are trying SOCKS5, and host address couldn't be resolved,
         don't try to match with exceptions. */
      !((c->socks_type == SSH_TCP_SOCKS5) && !c->next_address))
    {
      char *next;
      SshIpAddrStruct ipaddr;

      next = strchr(ssh_csstr(c->next_address), ',');
      if (next)
        *next = '\0';

      if (! ssh_ipaddr_parse(&ipaddr, c->next_address))
        SSH_FSM_SET_NEXT(tcp_connect_host_connect);
      /* SOCKS5 can handle IPv6. */
      else if (SSH_IP_IS6(&ipaddr) && c->socks_type == SSH_TCP_SOCKS4)
        SSH_FSM_SET_NEXT(tcp_connect_host_connect);
      else if (ssh_inet_compare_netmask(c->socks_exceptions,
                                        c->next_address))
        SSH_FSM_SET_NEXT(tcp_connect_host_connect);
      else
        SSH_FSM_SET_NEXT(tcp_connect_socks_connect);

      if (next)
        *next = ',';
    }
  else
    {
      SSH_FSM_SET_NEXT(tcp_connect_socks_connect);
    }
  SSH_FSM_CONTINUE_AFTER_CALLBACK(thread);
}

SSH_FSM_STEP(tcp_connect_socks_lookup)
{
  ConnectContext c = (ConnectContext) fsm_context;

  SSH_DEBUG(5, ("Resolving SOCKS server `%s' IP.", c->socks_host));
  SSH_FSM_ASYNC_CALL(c->handle = ssh_tcp_get_host_addrs_by_name
                     (c->socks_host, tcp_connect_socks_lookup_cb, thread));
}

/* We are called whenever a notification is received from the stream.
   This shouldn't really happen unless read/write has failed, though
   I wouldn't count on it.  */

void tcp_connect_socks_notify(SshStreamNotification notification,
                              void *context)
{
  SshFSMThread thread = (SshFSMThread) context;
  ConnectContext c = (ConnectContext) ssh_fsm_get_gdata(thread);

  c->handle = NULL;

  switch (notification)
    {
    case SSH_STREAM_INPUT_AVAILABLE:
    case SSH_STREAM_CAN_OUTPUT:
      /* Just retry the processing for the current state. */
      break;

    case SSH_STREAM_DISCONNECTED:
      ssh_debug("ssh_socket_socks_notify: DISCONNECTED");
      ssh_stream_destroy(c->stream);
      c->stream = NULL;
      /* Count this as a failure. */
      if (tcp_connect_register_failure(thread, SSH_TCP_FAILURE))
        break;
      if (c->socks_host)
        {
          if (c->socks_type == SSH_TCP_SOCKS5 &&
              !c->host_addresses)
            {
              SSH_FSM_SET_NEXT(tcp_connect_socks_connect);
            }
          else if (c->socks_exceptions)
            {
              char *next;
              next = strchr(ssh_csstr(c->host_addresses), ',');
              if (next)
                *next = '\0';
              if (ssh_inet_compare_netmask(c->socks_exceptions,
                                           c->host_addresses))
                SSH_FSM_SET_NEXT(tcp_connect_host_connect);
              else
                SSH_FSM_SET_NEXT(tcp_connect_socks_connect);
              if (next)
                *next = ',';
            }
          else
            {
              SSH_FSM_SET_NEXT(tcp_connect_socks_connect);
            }
        }
      else
        {
          SSH_FSM_SET_NEXT(tcp_connect_host_connect);
        }
      break;

    default:
      ssh_fatal("ssh_socket_socks_notify: unexpected notification %d",
                (int)notification);
    }
  ssh_fsm_continue(thread);
}

void tcp_connect_socks_connect_done_cb(SshTcpError error,
                                       SshStream stream,
                                       void *context)
{
  SshFSMThread thread = (SshFSMThread) context;
  ConnectContext c = (ConnectContext) ssh_fsm_get_gdata(thread);
  struct SocksInfoRec socksinfo;
  SocksError ret;
  unsigned char host_port[64], *next = NULL;

  c->handle = NULL;

  if (error != SSH_TCP_OK)
    {
      /* Get next address. */
      if (strchr(ssh_csstr(c->socks_next_address), ','))
        {
          c->socks_next_address =
            ssh_ustr(strchr(ssh_csstr(c->socks_next_address), ',')) + 1;
        }
      else
        { /* At end of list; consider it as a failure. */
          if (tcp_connect_register_failure(thread, error))
            {
              SSH_FSM_CONTINUE_AFTER_CALLBACK(thread);
              return;
            }
          c->socks_next_address = c->socks_addresses;
        }
      /* Try connecting again. */
      SSH_FSM_SET_NEXT(tcp_connect_socks_connect);
      SSH_FSM_CONTINUE_AFTER_CALLBACK(thread);
      return;
    }

  /* Save the stream. */
  c->stream = stream;

  /* Set the callback so that we'll get any required read/write
     notifications. */
  ssh_stream_set_callback(stream, tcp_connect_socks_notify, thread);

  if (c->next_address &&
      (next = ssh_ustr(strchr(ssh_csstr(c->next_address), ','))) != NULL)
    {
      *next = '\0';
      next++;
    }

  if (c->socks_type == SSH_TCP_SOCKS5)
    {
      socksinfo.socks_version_number = 5;
      socksinfo.command_code = SSH_SOCKS5_COMMAND_CODE_CONNECT;
      if (c->next_address)
        /* XXX tzimmo: is it really safe to convert a const char pointer
           to a non-const? XXX */
        socksinfo.ip = (unsigned char *) c->next_address;
      else
        socksinfo.ip = c->host_name;
    }
  else
    {
      socksinfo.socks_version_number = 4;
      socksinfo.command_code = SSH_SOCKS4_COMMAND_CODE_CONNECT;
      /* XXX tzimmo: is it really safe to convert a const char pointer
         to a non-const? XXX */
      socksinfo.ip = (unsigned char *) c->next_address;
    }
  /* XXX temporary casts until library API is changed XXX */
  ssh_snprintf(ssh_sstr(host_port), sizeof(host_port), "%d", c->host_port);
  socksinfo.port = host_port;
  socksinfo.username = c->user_name;

  ssh_buffer_clear(c->socks_buf);
  /* XXX */
  SSH_FSM_SET_NEXT(tcp_connect_socks_send);
  ret = ssh_socks_client_generate_methods(c->socks_buf, &socksinfo);
  if (ret == SSH_SOCKS_SUCCESS)
    ret = ssh_socks_client_generate_open(c->socks_buf, &socksinfo);
  if (ret != SSH_SOCKS_SUCCESS)
    {
      if (next != NULL)
        {
          c->stream = NULL;
          ssh_stream_destroy(stream);
          c->next_address = next;
          SSH_FSM_SET_NEXT(tcp_connect_socks_lookup);
        }
      else
        {
          if (ret == SSH_SOCKS_ERROR_INVALID_ARGUMENT)
            c->error = SSH_TCP_NO_ADDRESS;
          else
            c->error = SSH_TCP_FAILURE;
          SSH_FSM_SET_NEXT(tcp_connect_finish);
        }
    }
  SSH_FSM_CONTINUE_AFTER_CALLBACK(thread);
}

SSH_FSM_STEP(tcp_connect_socks_connect)
{
  ConnectContext c = (ConnectContext) fsm_context;

  SSH_DEBUG(5, ("Connecting SOCKS server %s:%u.", c->socks_next_address,
                c->socks_port));
  SSH_FSM_ASYNC_CALL(c->handle =
                     ssh_socket_low_connect(c->local_address,
                                            c->local_port,
                                            c->local_reusable,
                                            c->socks_next_address,
                                            c->socks_port,
                                            tcp_connect_socks_connect_done_cb,
                                            thread));
}

void tcp_connect_host_connect_done_cb(SshTcpError error,
                                      SshStream stream,
                                      void *context)
{
  SshFSMThread thread = (SshFSMThread) context;
  ConnectContext c = (ConnectContext) ssh_fsm_get_gdata(thread);

  c->handle = NULL;

  if (error != SSH_TCP_OK)
    {
      /* Get next address. */
      if (strchr(ssh_csstr(c->next_address), ','))
        c->next_address =
          ssh_ustr(strchr(ssh_csstr(c->next_address), ',')) + 1;
      else
        { /* At end of list; consider it as a failure. */
          if (tcp_connect_register_failure(thread, error))
            {
              SSH_FSM_CONTINUE_AFTER_CALLBACK(thread);
              return;
            }
          c->next_address = c->host_addresses;
        }

      /* Try connecting again. */
      if (c->socks_host)
        {
          SSH_FSM_SET_NEXT(tcp_connect_socks_lookup);
        }
      /* If SOCKS is not used, we go back to tcp_connect_socks_connect */
      SSH_FSM_CONTINUE_AFTER_CALLBACK(thread);
      return;
    }

  c->stream = stream;
  /* Successfully connected to the host.  Call the user callback and
     destroy context. */
  SSH_FSM_SET_NEXT(tcp_connect_finish);
  SSH_FSM_CONTINUE_AFTER_CALLBACK(thread);
}

SSH_FSM_STEP(tcp_connect_host_connect)
{
  ConnectContext c = (ConnectContext) fsm_context;

  SSH_FSM_ASYNC_CALL(c->handle =
                     ssh_socket_low_connect(c->local_address,
                                            c->local_port,
                                            c->local_reusable,
                                            c->next_address, c->host_port,
                                            tcp_connect_host_connect_done_cb,
                                            thread));
}

SSH_FSM_STEP(tcp_connect_socks_send)
{
  ConnectContext c = (ConnectContext) fsm_context;
  int len;

  do {
    len = ssh_stream_write(c->stream, ssh_buffer_ptr(c->socks_buf),
                           ssh_buffer_len(c->socks_buf));
    if (len > 0)
      ssh_buffer_consume(c->socks_buf, len);
    if (ssh_buffer_len(c->socks_buf) == 0)
      {
        SSH_FSM_SET_NEXT(tcp_connect_socks_receive_method);
        return SSH_FSM_CONTINUE;
      }
  } while (len > 0);

  return SSH_FSM_SUSPENDED;
}

SSH_FSM_STEP(tcp_connect_socks_receive_read_byte)
{
  ConnectContext c = (ConnectContext) fsm_context;
  unsigned char buf[1];
  int len;

  len = ssh_stream_read(c->stream, buf, 1);

  if (len == 0)
    { /* Premature EOF received. */
      SSH_FSM_SET_NEXT(tcp_connect_socks_error);
      return SSH_FSM_CONTINUE;
    }
  if (len > 0)
    {
      if (ssh_buffer_append(c->socks_buf, buf, 1) != SSH_BUFFER_OK)
        {
          SSH_FSM_SET_NEXT(tcp_connect_socks_error);
          return SSH_FSM_CONTINUE;
        }
      SSH_FSM_SET_NEXT(c->next_state);
      return SSH_FSM_CONTINUE;
    }

  return SSH_FSM_SUSPENDED;
}

SSH_FSM_STEP(tcp_connect_socks_receive_method)
{
  ConnectContext c = (ConnectContext) fsm_context;
  SocksError err;

  err = ssh_socks_client_parse_method(c->socks_buf, NULL);

  if (err == SSH_SOCKS_SUCCESS)
    {
      SSH_FSM_SET_NEXT(tcp_connect_socks_receive);
    }
  else if (err == SSH_SOCKS_TRY_AGAIN)
    {
      c->next_state = tcp_connect_socks_receive_method;
      SSH_FSM_SET_NEXT(tcp_connect_socks_receive_read_byte);
    }
  else
    {
      SSH_FSM_SET_NEXT(tcp_connect_socks_error);
    }
  return SSH_FSM_CONTINUE;
}

SSH_FSM_STEP(tcp_connect_socks_receive)
{
  ConnectContext c = (ConnectContext) fsm_context;
  SocksError err;

  err = ssh_socks_client_parse_reply(c->socks_buf, NULL);
  if (err != SSH_SOCKS_TRY_AGAIN)
    SSH_DEBUG(2, ("Got err = %d from "
                  "ssh_socks_client_parse_reply().", err));

  if (err == SSH_SOCKS_SUCCESS)
    {
      SSH_FSM_SET_NEXT(tcp_connect_finish);
    }
  else if (err == SSH_SOCKS_TRY_AGAIN)
    {
      c->next_state = tcp_connect_socks_receive;
      SSH_FSM_SET_NEXT(tcp_connect_socks_receive_read_byte);
    }
  else
    {
      SSH_FSM_SET_NEXT(tcp_connect_socks_error);
    }
  return SSH_FSM_CONTINUE;
}

SSH_FSM_STEP(tcp_connect_socks_error)
{
  ConnectContext c = (ConnectContext) fsm_context;

  ssh_stream_destroy(c->stream);
  c->stream = NULL;

  if (c->socks_type == SSH_TCP_SOCKS5 && !c->next_address)
    {
      c->error = SSH_TCP_FAILURE;
      SSH_FSM_SET_NEXT(tcp_connect_finish);
      return SSH_FSM_CONTINUE;
    }

  /* Get the next host address. */
  if (strchr(ssh_csstr(c->next_address), ','))
    {
      c->next_address = ssh_ustr(strchr(ssh_csstr(c->next_address), ',')) + 1;
    }
  else
    {
      if (tcp_connect_register_failure(thread, SSH_TCP_FAILURE))
        return SSH_FSM_CONTINUE;
      c->next_address = c->host_addresses;
    }
  if (c->socks_exceptions)
    {
      char *next;
      next = strchr(ssh_csstr(c->host_addresses), ',');
      if (next)
        *next = '\0';
      /* XXX SOCKS exceptions with IPv6 addresses only if we're doing
         SOCKS5. */
      if (ssh_inet_compare_netmask(c->socks_exceptions,
                                   c->host_addresses))
        SSH_FSM_SET_NEXT(tcp_connect_host_connect);
      else
        SSH_FSM_SET_NEXT(tcp_connect_socks_connect);
      if (next)
        *next = ',';
    }
  else
    {
      SSH_FSM_SET_NEXT(tcp_connect_socks_connect);
    }
  return SSH_FSM_CONTINUE;
}

SSH_FSM_STEP(tcp_connect_finish)
{
  ConnectContext c = (ConnectContext) fsm_context;

  if (c->error == SSH_TCP_OK)
    {
      /* Clear our callback function.  We don't want to get notifications
         for this stream anymore. */
      ssh_stream_set_callback(c->stream, NULL_FNPTR, NULL);
    }

  /* Call the user callback. */
  if (c->user_callback)
    (*c->user_callback)(c->error, c->stream, c->user_context);

  if (c->error == SSH_TCP_OK)
    {
      /* Prevent the stream from being freed when the context is freed. */
      c->stream = NULL;
    }
  SSH_FSM_SET_NEXT(tcp_connect_cleanup);
  return SSH_FSM_CONTINUE;
}

SSH_FSM_STEP(tcp_connect_abort)
{
  ConnectContext c = (ConnectContext) fsm_context;
  c->upper_handle = NULL;
  SSH_FSM_SET_NEXT(tcp_connect_cleanup);
  return SSH_FSM_CONTINUE;
}

SSH_FSM_STEP(tcp_connect_cleanup)
{
  ConnectContext c = (ConnectContext) fsm_context;
  ssh_fsm_destroy(c->fsm);
  tcp_connect_destroy_ctx(c);
  return SSH_FSM_FINISH;
}

/* Remove addresses that don't match the protocol match from the
   address list.  Overwrites the original list.  Can also result into
   empty address list. */

void ssh_remove_non_matching_addresses_from_list(unsigned char *address,
                                                 SshUInt32 protocol_mask)
{
  char *na;
  unsigned char *cur, *next;

  if ((na = ssh_malloc(strlen(ssh_csstr(address)) + 1)) == NULL)
    return;

  na[0] = '\0';
  cur = address;
  while (cur != NULL)
    {
      next = ssh_ustr(strchr(ssh_csstr(cur), ','));
      if (next)
        *next = '\0';
      if (ssh_address_type_matches_protocol_mask(cur, protocol_mask))
        {
          if (na[0] != '\0')
            {
              strcat(na, ",");
            }
          strcat(na, ssh_csstr(cur));
        }
      if (next)
        {
          cur = next + 1;
          *next = ',';
        }
      else
        {
          cur = NULL;
        }
    }

  SSH_DEBUG(5, ("Original address list = \"%s\"", address));
  strcpy(ssh_sstr(address), na);
  ssh_free(na);
  SSH_DEBUG(5, ("Fixed address list = \"%s\"", address));
  return;
}

/* Return TRUE, if address os of type that is specified in the
   protocol mask. */

Boolean ssh_address_type_matches_protocol_mask(unsigned char *address,
                                               SshUInt32 protocol_mask)
{
  SshIpAddrStruct ipaddr;
  Boolean pr;
  char *next;

  next = strchr(ssh_csstr(address), ',');
  if (next)
    *next = '\0';
  pr = ssh_ipaddr_parse(&ipaddr, address);
  if (next)
    *next = ',';
  if (! pr)
    return FALSE;
  if (SSH_IP_IS6(&ipaddr) && (protocol_mask & SSH_IP_TYPE_MASK_IP6))
    return TRUE;
  if (SSH_IP_IS4(&ipaddr) && (protocol_mask & SSH_IP_TYPE_MASK_IP4))
    return TRUE;
  return FALSE;
}


const unsigned char *ssh_tcp_error_string(SshTcpError error)
{
  switch (error)
    {
    case SSH_TCP_OK:
     return ssh_custr("OK");
    case SSH_TCP_NEW_CONNECTION:
     return ssh_custr("New TCP Connection");
    case SSH_TCP_NO_ADDRESS:
     return ssh_custr("No address associated to the name");
    case SSH_TCP_NO_NAME:
     return ssh_custr("No name associated to the address");
    case SSH_TCP_UNREACHABLE:
     return ssh_custr("Destination Unreachable");
    case SSH_TCP_REFUSED:
     return ssh_custr("Connection Refused");
    case SSH_TCP_TIMEOUT:
     return ssh_custr("Connection Timed Out");
    case SSH_TCP_FAILURE:
     return ssh_custr("TCP/IP Failure");
    default:
     return ssh_custr("Unknown Error");
    }
  /*NOTREACHED*/
}
