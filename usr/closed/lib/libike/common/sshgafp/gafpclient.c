/*

gafpclient.c

Author: Hannu K. Napari <Hannu.Napari@ssh.com>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
All rights reserved

Interfaces for implementing a client connecting to the authentication agent
using GAFP protocol.

*/

#include "sshincludes.h"
#include "sshcrypt.h"
#include "sshcryptoaux.h"
#include "sshlocalstream.h"
#include "sshpacketstream.h"
#include "sshencode.h"
#include "sshgafp.h"
#include "ssheloop.h"
#include "sshtimeouts.h"
#include "sshgetput.h"



#include "sshdsprintf.h"

#include "sshgafpi.h"

#define SSH_AA_SERVICEPOINT_FILE ".sshaccession/servicepoint"

#define SSH_DEBUG_MODULE "SshGafpClientInterface"

/* Internal interfaces */
SshOperationHandle ssh_gafp_connect(const unsigned char *auth_path,
                                    SshLocalCallback callback,
                                    void *context);
void ssh_gafp_client_open_complete_external(SshGafp agent);
void ssh_gafp_client_open_complete(SshStream stream, void *context);
void ssh_gafp_open_abort_callback(void *context);
void ssh_gafp_client_received_packet(SshPacketType type,
                                     const unsigned char *data,
                                     size_t len,
                                     void *context);
void ssh_gafp_client_received_notification(SshPacketType type,
                                           const unsigned char *data,
                                           size_t len,
                                           void *context);
void ssh_gafp_client_received_eof(void *context);
void ssh_gafp_op_id_increment(SshGafp agent);
Boolean ssh_gafp_op_id_is_reserved(SshUInt32 operation_id);
static SshOperationHandle ssh_gafp_operation_create(SshGafp agent,
                                                    void *context);
static void ssh_gafp_operation_destroy(SshGafpOperationContext op_ctx);
void ssh_gafp_operation_abort_callback(void *context);
void ssh_gafp_operation_destructor_callback(Boolean aborted, void *context);
void ssh_gafp_send(SshGafp agent, SshPacketType type, ...);
void ssh_gafp_send_va(SshGafp agent, SshPacketType type, va_list ap);
void ssh_gafp_fatal_error_handler(SshGafp agent, SshGafpError error);

SshGafp ssh_gafp_allocate(const char *client_name,
                          SshUInt32 client_version_major,
                          SshUInt32 client_version_minor,
                          SshGafpOpenCallback open_callback,
                          SshGafpCompletionCallback connection_closed_cb,
                          void *context);
void ssh_gafp_free(SshGafp agent);

/* Connects to an existing authentication agent. In Unix, this gets
   the path of a unix domain socket from an environment variable and
   connects that socket. This calls the given function when the
   connection is complete. Returned SshOperationHandle can be used
   to abort the operation before the open callback has been called by the
   library. */
SshOperationHandle
ssh_gafp_open(const char *auth_path,
              const char *client_name,
              SshUInt32 client_version_major,
              SshUInt32 client_version_minor,
              SshGafpOpenCallback open_callback,
              SshGafpCompletionCallback connection_closed_callback,
              void *context)
{
  SshOperationHandle open_handle;
  SshGafp agent;

  /* Initialize the agent connection object. */
  agent = ssh_gafp_allocate(client_name, client_version_minor,
                            client_version_major, open_callback,
                            connection_closed_callback, context);

  /* Connect to the agent socket. */
  open_handle = ssh_gafp_connect(ssh_custr(auth_path),
                                 ssh_gafp_client_open_complete,
                                 (void *)agent);

  return open_handle;
}

/* Connects to an existing authentication agent.
   If the path of a local domain socket is not specified, it is read from an
   environment variable. This calls the given function when the connection
   is complete. */
SshOperationHandle ssh_gafp_connect(const unsigned char *auth_path,
                                    SshLocalCallback callback,
                                    void *context)
{
  SshGafp agent;
  SshOperationHandle connect_handle;




  agent = (SshGafp)context;

  /* Get the path of the agent socket. */
  if (auth_path == NULL)
    {
#ifndef WIN32
      auth_path = ssh_custr(getenv(SSH_GAFP_SOCK_VAR));
#else
      auth_path = SSH_GAFP_SOCK_VAR;
#endif /* WIN32 */
    }
































  if (auth_path == NULL)
    {
      ssh_warning("No agent path set");
      (*callback)(NULL, agent);
      ssh_gafp_free(agent);
      return NULL;
    }

#ifndef WIN32
  if (getuid() != geteuid())
    {
      /* XXX  much more checking and care is needed to make this work in
         suid programs.  Talk to kivinen@ssh.fi or ylo@ssh.fi before
         attempting to do anything for that.  Compare with the code in
         ssh1. */
      ssh_warning("ssh_gafp_connect: not secure in a suid program");
      ssh_warning("Refusing to connect to agent.");
      (*callback)(NULL, agent);
      ssh_gafp_free(agent);
      return NULL;
    }
#endif /* WIN32 */

  /* Connect to the agent. */
  agent->open_handle = ssh_operation_register(ssh_gafp_open_abort_callback,
                                              agent);

  connect_handle = ssh_local_connect(auth_path, callback, agent);
  if (connect_handle)
    {
      agent->connect_handle = connect_handle;
    }
  else
    {
      /* If the packet connection is not okay, the stream was not
         succesfully opened. */
      if (agent->packet_conn == NULL)
        {
          (*agent->open_callback)(NULL, agent->connection_context);
          ssh_operation_unregister(agent->open_handle);
          ssh_gafp_free(agent);
          return NULL;
        }
    }

  return agent->open_handle;
}

/* Connects to an authentication agent using external communication
   methods.  Caller provides the callback that can be used by the
   library to send packets to the agent.  Caller feeds the packets
   received from the agent to the library using
   ssh_gafp_receive_packet_external. This calls the given function
   when the connection is complete. This function will always execute
   synchronously. */
void ssh_gafp_open_external(SshGafpPacketSendCallback packet_send_callback,
                            void *packet_send_context,
                            const char *client_name,
                            SshUInt32 client_version_major,
                            SshUInt32 client_version_minor,
                            SshGafpOpenCallback open_callback,
                            SshGafpCompletionCallback connection_closed_cb,
                            void *context)
{
  SshGafp agent;

  /* Initialize the agent connection object. */
  agent = ssh_gafp_allocate(client_name, client_version_minor,
                            client_version_major, open_callback,
                            connection_closed_cb, context);
  agent->packet_send_callback = packet_send_callback;
  agent->packet_send_context = packet_send_context;

  if (agent->packet_send_callback)
    {
      ssh_gafp_client_open_complete_external(agent);
    }
  else if (agent->open_callback)
    {
      (*agent->open_callback)(NULL, agent->connection_context);
      ssh_gafp_free(agent);
    }
}

void ssh_gafp_open_abort_callback(void *context)
{
  SshGafp agent = (SshGafp)context;

  if (agent->packet_conn)
    {
      ssh_packet_wrapper_destroy(agent->packet_conn);
    }

  if (agent->connect_handle)
    {
      ssh_operation_abort(agent->connect_handle);
    }

  ssh_gafp_free(agent);

  return;
}

SshGafp ssh_gafp_allocate(const char *client_name,
                          SshUInt32 client_version_major,
                          SshUInt32 client_version_minor,
                          SshGafpOpenCallback open_callback,
                          SshGafpCompletionCallback connection_closed_cb,
                          void *context)
{
  SshGafp agent;

  /* Initialize the agent connection object. */
  agent = ssh_xcalloc(1, sizeof (*agent));
  agent->open_callback = open_callback;
  agent->client_name = ((client_name) ?
                        ssh_xstrdup(client_name) :
                        ssh_xstrdup(SSH_GAFP_LIBRARY_VERSION_STRING));
  agent->client_version_major = client_version_major;
  agent->client_version_minor = client_version_minor;
  agent->state = SSH_GAFP_STATE_INITIAL;
  agent->connection_closed_callback =
    (SshGafpCompletionCallback)connection_closed_cb;
  agent->connection_context = context;
  agent->close_context = context;
  agent->state = SSH_GAFP_STATE_STARTING;

  return agent;
}

void ssh_gafp_free(SshGafp agent)
{
  ssh_xfree(agent->agent_name);
  ssh_xfree(agent->client_name);

  memset(agent, 'F', sizeof(*agent));
  ssh_xfree(agent);
}

/* Closes the connection to the authentication agent.  If a command is
   active, it is terminated and its callback will never be called. */
void ssh_gafp_close(SshGafp agent)
{
  SshADTHandle handle;

  if (!agent)
    {
      return;
    }

  for (handle = ssh_adt_enumerate_start(agent->operation_map);
       handle != SSH_ADT_INVALID;
       handle = ssh_adt_enumerate_next(agent->operation_map, handle))
    {
      SshOperationHandle op_handle;
      SshUInt32 *op_id;

      op_id = (SshUInt32 *)ssh_adt_get(agent->operation_map, handle);
      op_handle =
        (SshOperationHandle)ssh_adt_intmap_get(agent->operation_map,
                                               *op_id);
      ssh_operation_abort(op_handle);
    }

  ssh_adt_destroy(agent->operation_map);

  if (agent->packet_conn)
    {
      ssh_packet_wrapper_destroy(agent->packet_conn);
    }

  ssh_gafp_free(agent);

  return;
}

/* Register a connection close callback to be called when the agent
   stream sends eof. */
void ssh_gafp_register_close_callback(SshGafp agent,
                                      SshGafpCompletionCallback callback,
                                      void *context)
{
  if (agent == NULL)
    return;
  agent->connection_closed_callback = callback;
  agent->close_context = context;

}


/* Register a notification callback to receive notifications about
   agent state */
void ssh_gafp_register_notification_callback(SshGafp agent,
                                             SshGafpNotificationCallback cb,
                                             void *context)
{
  if (agent)
    {
      agent->notification_callback = cb;
      agent->notification_context = context;
    }
}

/* Adds the given private key to the agent.  The callback can be NULL. */
SshOperationHandle
ssh_gafp_add_key_2(SshGafp agent,
                   const char *public_key_encoding,
                   const unsigned char *public_key_data,
                   size_t public_key_data_len,
                   const char *private_key_encoding,
                   const unsigned char *private_key_data,
                   size_t private_key_data_len,
                   const char *description,
                   SshGafpCompletionCallback callback,
                   void *context)
{
  SshOperationHandle op_handle;
  SshGafpOperationContext op_ctx;

  if (agent->got_eof)
    {
      if (callback)
        {
          (*callback)(SSH_GAFP_ERROR_FAILURE, context);
        }
      return NULL;
    }

  if ((agent->state != SSH_GAFP_STATE_RUNNING) ||
      (public_key_encoding == NULL) ||
      (private_key_encoding == NULL))
    {
      if (callback)
        {
          (*callback)(SSH_GAFP_ERROR_PROTOCOL, context);
        }
      return NULL;
    }

  op_handle = ssh_gafp_operation_create(agent, context);

  if (!op_handle)
    {
      if (callback)
        {
          (*callback)(SSH_GAFP_ERROR_OPERATION_ACTIVE, context);
        }
      return NULL;
    }

  op_ctx = ssh_operation_get_context(op_handle);
  op_ctx->completion_callback = callback;

  ssh_gafp_send(agent,
                (SshPacketType)SSH_GAFP_MSG_ADD_KEY,
                SSH_FORMAT_UINT32,
                (SshUInt32)agent->operation_id,
                SSH_FORMAT_UINT32_STR,
                public_key_encoding, strlen(public_key_encoding),
                SSH_FORMAT_UINT32_STR,
                public_key_data, public_key_data_len,
                SSH_FORMAT_UINT32_STR,
                private_key_encoding,
                strlen(private_key_encoding),
                SSH_FORMAT_UINT32_STR,
                private_key_data, private_key_data_len,
                SSH_FORMAT_UINT32_STR,
                description, strlen(description),
                SSH_FORMAT_END);

  return op_handle;
}

SshOperationHandle ssh_gafp_add_key(SshGafp agent,
                                    SshPublicKey public_key,
                                    const char *private_key_encoding,
                                    const unsigned char *private_key_data,
                                    size_t private_key_data_len,
                                    const char *description,
                                    SshGafpCompletionCallback callback,
                                    void *context)
{
  unsigned char *public_blob;
  size_t public_blob_len;
  SshOperationHandle op_handle;

  if (public_key)
    {
      if (ssh_public_key_export(public_key,
                                &public_blob,
                                &public_blob_len) != SSH_CRYPTO_OK)
        {
          SSH_DEBUG(SSH_D_FAIL, ("public key export failed"));
          if (callback)
            {
              (*callback)(SSH_GAFP_ERROR_FAILURE, context);
            }
          return NULL;
        }

      SSH_DEBUG_HEXDUMP(SSH_D_DATADUMP,
          ("Exported public key blob (encoding %s, description %s, %d bytes):",
            SSH_GAFP_KEY_ENCODING_SSH, description, public_blob_len),
           public_blob, public_blob_len);


    }
  else
    {
      public_blob = NULL;
      public_blob_len = 0;
    }

  op_handle = ssh_gafp_add_key_2(agent,
                                 SSH_GAFP_DEFAULT_KEY_ENCODING,
                                 public_blob,
                                 public_blob_len,
                                 private_key_encoding,
                                 private_key_data,
                                 private_key_data_len,
                                 description,
                                 callback,
                                 context);

  ssh_xfree(public_blob);
  return op_handle;
}

/* Adds the given certificate to the agent.  The callback can be NULL. */
SshOperationHandle
ssh_gafp_add_certificate_2(SshGafp agent,
                           const char *public_key_encoding,
                           const unsigned char *public_key_data,
                           size_t public_key_data_len,
                           const char *certificate_type,
                           const unsigned char *certificate,
                           size_t certificate_len,
                           const char *description,
                           SshGafpCompletionCallback callback,
                           void *context)
{
  SshOperationHandle op_handle;
  SshGafpOperationContext op_ctx;

  if (agent->got_eof)
    {
      if (callback)
        {
          (*callback)(SSH_GAFP_ERROR_FAILURE, context);
        }
      return NULL;
    }

  if (agent->state != SSH_GAFP_STATE_RUNNING)
    {
      if (callback)
        {
          (*callback)(SSH_GAFP_ERROR_PROTOCOL, context);
        }
      return NULL;
    }

  op_handle = ssh_gafp_operation_create(agent, context);

  if (!op_handle)
    {
      if (callback)
        {
          (*callback)(SSH_GAFP_ERROR_OPERATION_ACTIVE, context);
        }
      return NULL;
    }

  op_ctx = ssh_operation_get_context(op_handle);
  op_ctx->completion_callback = callback;

  ssh_gafp_send(agent,
                (SshPacketType)SSH_GAFP_MSG_ADD_CERTIFICATE,
                SSH_FORMAT_UINT32,
                (SshUInt32) agent->operation_id,
                SSH_FORMAT_UINT32_STR,
                public_key_encoding, strlen(public_key_encoding),
                SSH_FORMAT_UINT32_STR,
                public_key_data, public_key_data_len,
                SSH_FORMAT_UINT32_STR,
                certificate_type, strlen(certificate_type),
                SSH_FORMAT_UINT32_STR,
                certificate, certificate_len,
                SSH_FORMAT_UINT32_STR,
                description, strlen(description),
                SSH_FORMAT_END);

  return op_handle;
}

SshOperationHandle ssh_gafp_add_certificate(SshGafp agent,
                                            SshPublicKey public_key,
                                            const char *certificate_type,
                                            const unsigned char *certificate,
                                            size_t certificate_len,
                                            const char *description,
                                            SshGafpCompletionCallback callback,
                                            void *context)
{
  unsigned char *public_blob;
  size_t public_blob_len;
  SshOperationHandle op_handle;

  if (public_key)
    {
      if (ssh_public_key_export(public_key,
                                &public_blob,
                                &public_blob_len) != SSH_CRYPTO_OK)
        {
          SSH_DEBUG(SSH_D_FAIL, ("public key export failed"));
          if (callback)
            {
              (*callback)(SSH_GAFP_ERROR_FAILURE, context);
            }
          return NULL;
        }
    }
  else
    {
      public_blob = NULL;
      public_blob_len = 0;
    }

  op_handle = ssh_gafp_add_certificate_2(agent,
                                         SSH_GAFP_DEFAULT_KEY_ENCODING,
                                         public_blob,
                                         public_blob_len,
                                         certificate_type,
                                         certificate,
                                         certificate_len,
                                         description,
                                         callback,
                                         context);

  ssh_xfree(public_blob);

  return op_handle;
}

SshOperationHandle ssh_gafp_add_extra_certificate(SshGafp agent,
                                          const char *certificate_type,
                                          const unsigned char *certificate,
                                          size_t certificate_len,
                                          const char *description,
                                          SshGafpCompletionCallback callback,
                                          void *context)
{
  SshOperationHandle op_handle;
  SshGafpOperationContext op_ctx;

  if (agent->got_eof)
    {
      if (callback)
        {
          (*callback)(SSH_GAFP_ERROR_FAILURE, context);
        }
      return NULL;
    }

  if (agent->state != SSH_GAFP_STATE_RUNNING)
    {
      if (callback)
        {
          (*callback)(SSH_GAFP_ERROR_PROTOCOL, context);
        }
      return NULL;
    }

  op_handle = ssh_gafp_operation_create(agent, context);

  if (!op_handle)
    {
      if (callback)
        {
          (*callback)(SSH_GAFP_ERROR_OPERATION_ACTIVE, context);
        }
      return NULL;
    }

  op_ctx = ssh_operation_get_context(op_handle);
  op_ctx->completion_callback = callback;

  ssh_gafp_send(agent,
                (SshPacketType)SSH_GAFP_MSG_ADD_EXTRA_CERTIFICATE,
                SSH_FORMAT_UINT32,
                (SshUInt32) agent->operation_id,
                SSH_FORMAT_UINT32_STR,
                certificate_type, strlen(certificate_type),
                SSH_FORMAT_UINT32_STR,
                certificate, certificate_len,
                SSH_FORMAT_UINT32_STR,
                description, strlen(description),
                SSH_FORMAT_END);

  return op_handle;
}

/* Deletes all keys and certificates from the agent. */
SshOperationHandle ssh_gafp_delete_all_keys(SshGafp agent,
                                            SshGafpCompletionCallback callback,
                                            void *context)
{
  SshOperationHandle op_handle;
  SshGafpOperationContext op_ctx;

  if (agent->got_eof)
    {
      if (callback)
        {
          (*callback)(SSH_GAFP_ERROR_FAILURE, context);
        }
      return NULL;
    }

  if (agent->state != SSH_GAFP_STATE_RUNNING)
    {
      if (callback)
        {
          (*callback)(SSH_GAFP_ERROR_PROTOCOL, context);
        }
      return NULL;
    }

  op_handle = ssh_gafp_operation_create(agent, context);

  if (!op_handle)
  {
    if (callback)
      {
        (*callback)(SSH_GAFP_ERROR_OPERATION_ACTIVE, context);
      }
    return NULL;
  }

  op_ctx = ssh_operation_get_context(op_handle);
  op_ctx->completion_callback = callback;

  /* Send the request. */
  ssh_gafp_send(agent,
                (SshPacketType)SSH_GAFP_MSG_DELETE_ALL_KEYS,
                SSH_FORMAT_UINT32,
                (SshUInt32) agent->operation_id,
                SSH_FORMAT_END);

  return op_handle;
}

/* Deletes the given key from the agent. All certificates for this key
   are deleted as well. */
SshOperationHandle ssh_gafp_delete_key_2(SshGafp agent,
                                         const char *public_key_encoding,
                                         const unsigned char *public_key_data,
                                         size_t public_key_data_len,
                                         SshGafpCompletionCallback callback,
                                         void *context)
{
  SshOperationHandle op_handle;
  SshGafpOperationContext op_ctx;

  if (agent->got_eof)
    {
      if (callback)
        {
          (*callback)(SSH_GAFP_ERROR_FAILURE, context);
        }
      return NULL;
    }

  if (agent->state != SSH_GAFP_STATE_RUNNING)
    {
      if (callback)
        {
          (*callback)(SSH_GAFP_ERROR_PROTOCOL, context);
        }
      return NULL;
    }

  op_handle = ssh_gafp_operation_create(agent, context);

  if (!op_handle)
    {
      if (callback)
        {
          (*callback)(SSH_GAFP_ERROR_OPERATION_ACTIVE, context);
        }
      return NULL;
    }

  op_ctx = ssh_operation_get_context(op_handle);
  op_ctx->completion_callback = callback;

  /* Send the request. */
  ssh_gafp_send(agent,
                (SshPacketType)SSH_GAFP_MSG_DELETE_KEY,
                SSH_FORMAT_UINT32,
                (SshUInt32) agent->operation_id,
                SSH_FORMAT_UINT32_STR,
                public_key_encoding, strlen(public_key_encoding),
                SSH_FORMAT_UINT32_STR,
                public_key_data, public_key_data_len,
                SSH_FORMAT_END);

  return op_handle;
}

SshOperationHandle ssh_gafp_delete_key(SshGafp agent,
                                       SshPublicKey public_key,
                                       SshGafpCompletionCallback callback,
                                       void *context)
{
  unsigned char *public_blob;
  size_t public_blob_len;
  SshOperationHandle op_handle;

  if (public_key)
    {
      if (ssh_public_key_export(public_key,
                                &public_blob,
                                &public_blob_len) != SSH_CRYPTO_OK)
        {
          SSH_DEBUG(SSH_D_FAIL, ("public key export failed"));
          if (callback)
            {
              (*callback)(SSH_GAFP_ERROR_FAILURE, context);
            }
          return NULL;
        }
    }
  else
    {
      public_blob = NULL;
      public_blob_len = 0;
    }

  op_handle = ssh_gafp_delete_key_2(agent,
                                    SSH_GAFP_DEFAULT_KEY_ENCODING,
                                    public_blob,
                                    public_blob_len,
                                    callback,
                                    context);

  ssh_xfree(public_blob);

  return op_handle;
}

/* Deletes the given certificate from the agent. */
SshOperationHandle
ssh_gafp_delete_key_certificate_2(SshGafp agent,
                                  const char *public_key_encoding,
                                  const unsigned char *public_key_data,
                                  size_t public_key_data_len,
                                  const char *certificate_type,
                                  const unsigned char *certificate,
                                  size_t certificate_len,
                                  SshGafpCompletionCallback callback,
                                  void *context)
{
  SshOperationHandle op_handle;
  SshGafpOperationContext op_ctx;

  if (agent->got_eof)
    {
      if (callback)
        {
          (*callback)(SSH_GAFP_ERROR_FAILURE, context);
        }
      return NULL;
    }

  if (agent->state != SSH_GAFP_STATE_RUNNING)
    {
      if (callback)
        {
          (*callback)(SSH_GAFP_ERROR_PROTOCOL, context);
        }
      return NULL;
    }

  op_handle = ssh_gafp_operation_create(agent, context);

  if (!op_handle)
    {
      if (callback)
        {
          (*callback)(SSH_GAFP_ERROR_OPERATION_ACTIVE, context);
        }
      return NULL;
    }

  op_ctx = ssh_operation_get_context(op_handle);
  op_ctx->completion_callback = callback;

  /* Send the request. */
  ssh_gafp_send(agent,
                (SshPacketType)SSH_GAFP_MSG_DELETE_KEY_CERTIFICATE,
                SSH_FORMAT_UINT32,
                (SshUInt32) agent->operation_id,
                SSH_FORMAT_UINT32_STR,
                public_key_encoding, strlen(public_key_encoding),
                SSH_FORMAT_UINT32_STR,
                public_key_data, public_key_data_len,
                SSH_FORMAT_UINT32_STR,
                certificate_type,
                strlen(certificate_type),
                SSH_FORMAT_UINT32_STR,
                certificate, certificate_len,
                SSH_FORMAT_END);

  return op_handle;
}

SshOperationHandle
ssh_gafp_delete_key_certificate(SshGafp agent,
                                SshPublicKey public_key,
                                const char *certificate_type,
                                const unsigned char *certificate,
                                size_t certificate_len,
                                SshGafpCompletionCallback callback,
                                void *context)
{
  unsigned char *public_blob;
  size_t public_blob_len;
  SshOperationHandle op_handle;

  if (public_key)
    {
      if (ssh_public_key_export(public_key,
                                &public_blob,
                                &public_blob_len) != SSH_CRYPTO_OK)
        {
          SSH_DEBUG(SSH_D_FAIL, ("public key export failed"));
          if (callback)
            {
              (*callback)(SSH_GAFP_ERROR_FAILURE, context);
            }
          return NULL;
        }
    }
  else
    {
      public_blob = NULL;
      public_blob_len = 0;
    }

  op_handle = ssh_gafp_delete_key_certificate_2(agent,
                                                SSH_GAFP_DEFAULT_KEY_ENCODING,
                                                public_blob,
                                                public_blob_len,
                                                certificate_type,
                                                certificate,
                                                certificate_len,
                                                callback,
                                                context);

  ssh_xfree(public_blob);

  return op_handle;
}


/* Deletes the given certificate from the agent. */
SshOperationHandle
ssh_gafp_delete_extra_certificate(SshGafp agent,
                                  const char *certificate_type,
                                  const unsigned char *certificate,
                                  size_t certificate_len,
                                  SshGafpCompletionCallback callback,
                                  void *context)
{
  SshOperationHandle op_handle;
  SshGafpOperationContext op_ctx;

  if (agent->got_eof)
    {
      if (callback)
        {
          (*callback)(SSH_GAFP_ERROR_FAILURE, context);
        }
      return NULL;
    }

  if (agent->state != SSH_GAFP_STATE_RUNNING)
    {
      if (callback)
        {
          (*callback)(SSH_GAFP_ERROR_PROTOCOL, context);
        }
      return NULL;
    }

  op_handle = ssh_gafp_operation_create(agent, context);

  if (!op_handle)
    {
      if (callback)
        {
          (*callback)(SSH_GAFP_ERROR_OPERATION_ACTIVE, context);
        }
      return NULL;
    }

  op_ctx = ssh_operation_get_context(op_handle);
  op_ctx->completion_callback = callback;

  /* Send the request. */
  ssh_gafp_send(agent,
                (SshPacketType)SSH_GAFP_MSG_DELETE_EXTRA_CERTIFICATE,
                SSH_FORMAT_UINT32,
                (SshUInt32) agent->operation_id,
                SSH_FORMAT_UINT32_STR,
                certificate_type,
                strlen(certificate_type),
                SSH_FORMAT_UINT32_STR,
                certificate, certificate_len,
                SSH_FORMAT_END);

  return op_handle;
}

/* Returns the public keys for all private keys in possession of the agent. */
SshOperationHandle ssh_gafp_list_keys(SshGafp agent,
                                      SshGafpListCallback callback,
                                      void *context)
{
  SshOperationHandle op_handle;
  SshGafpOperationContext op_ctx;

  if (agent->got_eof)
    {
      if (callback)
        {
          (*callback)(SSH_GAFP_ERROR_FAILURE, 0, NULL, context);
        }
      return NULL;
    }

  if (agent->state != SSH_GAFP_STATE_RUNNING)
    {
      if (callback)
        {
          (*callback)(SSH_GAFP_ERROR_PROTOCOL, 0, NULL, context);
        }
      return NULL;
    }

  op_handle = ssh_gafp_operation_create(agent, context);

  if (!op_handle)
    {
      if (callback)
        {
          (*callback)(SSH_GAFP_ERROR_OPERATION_ACTIVE, 0, NULL, context);
        }
      return NULL;
    }

  op_ctx = ssh_operation_get_context(op_handle);
  op_ctx->list_callback = callback;

  /* Send the request. */
  ssh_gafp_send(agent,
                (SshPacketType)SSH_GAFP_MSG_LIST_KEYS,
                SSH_FORMAT_UINT32,
                (SshUInt32) agent->operation_id,
                SSH_FORMAT_END);

  return op_handle;
}

/* Returns the certificates for the given public key */
SshOperationHandle
ssh_gafp_list_key_certificates_2(SshGafp agent,
                                 const char *public_key_encoding,
                                 const unsigned char *public_key_data,
                                 size_t public_key_data_len,
                                 SshGafpListCallback callback,
                                 void *context)
{
  SshOperationHandle op_handle;
  SshGafpOperationContext op_ctx;

  if (agent->got_eof)
    {
      if (callback)
        {
          (*callback)(SSH_GAFP_ERROR_FAILURE, 0, NULL, context);
        }
      return NULL;
    }

  if (agent->state != SSH_GAFP_STATE_RUNNING)
    {
      if (callback)
        {
          (*callback)(SSH_GAFP_ERROR_PROTOCOL, 0, NULL, context);
        }
      return NULL;
    }

  op_handle = ssh_gafp_operation_create(agent, context);

  if (!op_handle)
  {
    if (callback)
      {
        (*callback)(SSH_GAFP_ERROR_OPERATION_ACTIVE, 0, NULL, context);
      }
    return NULL;
  }

  op_ctx = ssh_operation_get_context(op_handle);
  op_ctx->list_callback = callback;

  /* Send the request. */
  ssh_gafp_send(agent,
                (SshPacketType)SSH_GAFP_MSG_LIST_KEY_CERTIFICATES,
                SSH_FORMAT_UINT32,
                (SshUInt32) agent->operation_id,
                SSH_FORMAT_UINT32_STR,
                public_key_encoding, strlen(public_key_encoding),
                SSH_FORMAT_UINT32_STR,
                public_key_data, public_key_data_len,
                SSH_FORMAT_END);

  return op_handle;
}

SshOperationHandle ssh_gafp_list_key_certificates(SshGafp agent,
                                                  SshPublicKey public_key,
                                                  SshGafpListCallback callback,
                                                  void *context)
{
  unsigned char *public_blob;
  size_t public_blob_len;
  SshOperationHandle op_handle;

  if (public_key)
    {
      if (ssh_public_key_export(public_key,
                                &public_blob,
                                &public_blob_len) != SSH_CRYPTO_OK)
        {
          SSH_DEBUG(SSH_D_FAIL, ("public key export failed"));
          if (callback)
            {
              (*callback)(SSH_GAFP_ERROR_FAILURE, 0, NULL, context);
            }
          return NULL;
        }

      SSH_DEBUG_HEXDUMP(SSH_D_DATADUMP,
          ("Exported public key blob (encoding %s, %d bytes):",
            SSH_GAFP_KEY_ENCODING_SSH, public_blob_len),
           public_blob, public_blob_len);

    }
  else
    {
      public_blob = NULL;
      public_blob_len = 0;
    }

  op_handle = ssh_gafp_list_key_certificates_2(agent,
                                               SSH_GAFP_DEFAULT_KEY_ENCODING,
                                               public_blob,
                                               public_blob_len,
                                               callback,
                                               context);

  ssh_xfree(public_blob);

  return op_handle;
}

/* Returns the certificates for all private keys in possession of the agent. */
SshOperationHandle ssh_gafp_list_certificates(SshGafp agent,
                                              SshGafpListCallback callback,
                                              void *context)
{
  SshOperationHandle op_handle;
  SshGafpOperationContext op_ctx;

  if (agent->got_eof)
    {
      if (callback)
        {
          (*callback)(SSH_GAFP_ERROR_FAILURE, 0, NULL, context);
        }
      return NULL;
    }

  if (agent->state != SSH_GAFP_STATE_RUNNING)
    {
      if (callback)
        {
          (*callback)(SSH_GAFP_ERROR_PROTOCOL, 0, NULL, context);
        }
      return NULL;
    }

  op_handle = ssh_gafp_operation_create(agent, context);

  if (!op_handle)
  {
    if (callback)
      {
        (*callback)(SSH_GAFP_ERROR_OPERATION_ACTIVE, 0, NULL, context);
      }
    return NULL;
  }

  op_ctx = ssh_operation_get_context(op_handle);
  op_ctx->list_callback = callback;

  /* Send the request. */
  ssh_gafp_send(agent,
                (SshPacketType)SSH_GAFP_MSG_LIST_CERTIFICATES,
                SSH_FORMAT_UINT32,
                (SshUInt32) agent->operation_id,
                SSH_FORMAT_END);

  return op_handle;
}

/* Returns the certificates that do not have a private key stored
   in the agent. */
SshOperationHandle
ssh_gafp_list_extra_certificates(SshGafp agent,
                                 SshGafpListCallback callback,
                                 void *context)
{
  SshOperationHandle op_handle;
  SshGafpOperationContext op_ctx;

  if (agent->got_eof)
    {
      if (callback)
        {
          (*callback)(SSH_GAFP_ERROR_FAILURE, 0, NULL, context);
        }
      return NULL;
    }

  if (agent->state != SSH_GAFP_STATE_RUNNING)
    {
      if (callback)
        {
          (*callback)(SSH_GAFP_ERROR_PROTOCOL, 0, NULL, context);
        }
      return NULL;
    }

  op_handle = ssh_gafp_operation_create(agent, context);

  if (!op_handle)
  {
    if (callback)
      {
        (*callback)(SSH_GAFP_ERROR_OPERATION_ACTIVE, 0, NULL, context);
      }
    return NULL;
  }

  op_ctx = ssh_operation_get_context(op_handle);
  op_ctx->list_callback = callback;

  /* Send the request. */
  ssh_gafp_send(agent,
                (SshPacketType)
                SSH_GAFP_MSG_LIST_EXTRA_CERTIFICATES,
                SSH_FORMAT_UINT32,
                (SshUInt32) agent->operation_id,
                SSH_FORMAT_END);

  return op_handle;
}

/* Send a ping message to agent.  Call callback when reply arrives. */
SshOperationHandle ssh_gafp_ping(SshGafp agent,
                                 SshGafpCompletionCallback callback,
                                 void *context)
{
  SshOperationHandle op_handle;
  SshGafpOperationContext op_ctx;

  if (agent->got_eof)
    {
      if (callback)
        {
          (*callback)(SSH_GAFP_ERROR_FAILURE, context);
        }
      return NULL;
    }

  if (agent->state != SSH_GAFP_STATE_RUNNING)
    {
      if (callback)
        {
          (*callback)(SSH_GAFP_ERROR_PROTOCOL, context);
        }
      return NULL;
    }

  op_handle = ssh_gafp_operation_create(agent, context);

  if (!op_handle)
    {
      if (callback)
        {
          (*callback)(SSH_GAFP_ERROR_OPERATION_ACTIVE, context);
        }
      return NULL;
    }

  op_ctx = ssh_operation_get_context(op_handle);
  op_ctx->completion_callback = callback;

  /* Send the request. */
  ssh_gafp_send(agent,
                (SshPacketType)SSH_GAFP_MSG_PING,
                SSH_FORMAT_UINT32,
                (SshUInt32) agent->operation_id,
                SSH_FORMAT_END);

  return op_handle;
}

/* Performs a private-key operation using the agent.  Calls the given
   callback when a reply has been received or a timeout occurs.
   Only a single operation may be in progress on the connection at any
   one time.  The caller can free any argument strings as soon as this
   has returned (i.e., no need to wait until the callback has been
   called). */
SshOperationHandle
ssh_gafp_key_operation_2(SshGafp agent,
                         const char *operation,
                         const char *public_key_encoding,
                         const unsigned char *public_key_data,
                         size_t public_key_data_len,
                         const unsigned char *data,
                         size_t len,
                         SshGafpOpCallback callback,
                         void *context)
{
  SshOperationHandle op_handle;
  SshGafpOperationContext op_ctx;

  if (agent->got_eof)
    {
      if (callback)
        {
          (*callback)(SSH_GAFP_ERROR_FAILURE, NULL, 0, context);
        }
      return NULL;
    }

  if (agent->state != SSH_GAFP_STATE_RUNNING)
    {
      if (callback)
        {
          (*callback)(SSH_GAFP_ERROR_PROTOCOL, NULL, 0, context);
        }
      return NULL;
    }

  op_handle = ssh_gafp_operation_create(agent, context);

  if (!op_handle)
    {
      if (callback)
        {
          (*callback)(SSH_GAFP_ERROR_OPERATION_ACTIVE, NULL, 0, context);
        }
      return NULL;
    }

  op_ctx = ssh_operation_get_context(op_handle);
  op_ctx->op_callback = callback;

  if ((len + 4) <= SSH_GAFP_MAX_OPERATION_DATA_FRAGMENT)
    {
      /* Send the request packet. */
      SSH_DEBUG(SSH_D_NICETOKNOW,
                ("entire operation data will be sent immediately"));
      ssh_gafp_send(agent,
                    (SshPacketType)SSH_GAFP_MSG_KEY_OPERATION,
                    SSH_FORMAT_UINT32,
                    (SshUInt32) agent->operation_id,
                    SSH_FORMAT_UINT32_STR,
                    public_key_encoding, strlen(public_key_encoding),
                    SSH_FORMAT_UINT32_STR,
                    public_key_data, public_key_data_len,
                    SSH_FORMAT_UINT32_STR,
                    operation, strlen(operation),
                    SSH_FORMAT_UINT32_STR, data, len,
                    SSH_FORMAT_END);
    }
  else
    {
      SSH_DEBUG(SSH_D_NICETOKNOW,
                ("operation data will be sent in fragments"));
      SSH_DEBUG(SSH_D_NICETOKNOW,
                ("sending fragment 1"));
      ssh_encode_buffer(op_ctx->op_data_buf,
                        SSH_FORMAT_UINT32_STR,
                        data, len,
                        SSH_FORMAT_END);

      op_ctx->op_data_name = ssh_xstrdup(operation);
      op_ctx->op_data_seq = 0;
      op_ctx->op_data_cert_enc = ssh_xstrdup(public_key_encoding);
      op_ctx->op_data_cert = ssh_xmemdup(public_key_data, public_key_data_len);
      op_ctx->op_data_cert_len = public_key_data_len;

      /* Send the first fragment */
      ssh_gafp_send(agent,
                    (SshPacketType)SSH_GAFP_MSG_OPERATION_DATA_FRAGMENT,
                    SSH_FORMAT_UINT32,
                    (SshUInt32) agent->operation_id,
                    SSH_FORMAT_UINT32_STR,
                    op_ctx->op_data_name, strlen(op_ctx->op_data_name),
                    SSH_FORMAT_UINT32,
                    op_ctx->op_data_seq,
                    SSH_FORMAT_DATA,
                    ssh_buffer_ptr(op_ctx->op_data_buf),
                    SSH_GAFP_MAX_OPERATION_DATA_FRAGMENT,
                    SSH_FORMAT_END);

      ssh_buffer_consume(op_ctx->op_data_buf,
                         SSH_GAFP_MAX_OPERATION_DATA_FRAGMENT);
    }

  return op_handle;
}

SshOperationHandle ssh_gafp_key_operation(SshGafp agent,
                                          const char *operation,
                                          SshPublicKey public_key,
                                          const unsigned char *data,
                                          size_t len,
                                          SshGafpOpCallback callback,
                                          void *context)
{
  unsigned char *public_blob;
  size_t public_blob_len;
  SshOperationHandle op_handle;

  if (public_key)
    {
      if (ssh_public_key_export(public_key,
                                &public_blob,
                                &public_blob_len) != SSH_CRYPTO_OK)
        {
          SSH_DEBUG(SSH_D_FAIL, ("public key export failed"));
          if (callback)
            {
              (*callback)(SSH_GAFP_ERROR_FAILURE, NULL, 0, context);
            }
          return NULL;
        }
    }
  else
    {
      public_blob = NULL;
      public_blob_len = 0;
    }

  op_handle = ssh_gafp_key_operation_2(agent,
                                       operation,
                                       SSH_GAFP_DEFAULT_KEY_ENCODING,
                                       public_blob,
                                       public_blob_len,
                                       data,
                                       len,
                                       callback,
                                       context);

  ssh_xfree(public_blob);

  return op_handle;
}

/* Performs a private-key operation with the given certificate using the
   agent.  Calls the given callback when a reply has been received.
   The caller can free any argument strings as soon as this
   has returned (i.e., no need to wait until the callback has been
   called). */
SshOperationHandle
ssh_gafp_key_operation_with_certificate(SshGafp agent,
                                        const char *operation,
                                        const char *certificate_type,
                                        const unsigned char *certificate,
                                        size_t certificate_len,
                                        const unsigned char *data,
                                        size_t len,
                                        SshGafpOpCallback callback,
                                        void *context)
{
  SshOperationHandle op_handle;
  SshGafpOperationContext op_ctx;

  if (agent->got_eof)
    {
      if (callback)
        {
          (*callback)(SSH_GAFP_ERROR_FAILURE, NULL, 0, context);
        }
      return NULL;
    }

  if (agent->state != SSH_GAFP_STATE_RUNNING)
    {
      if (callback)
        {
          (*callback)(SSH_GAFP_ERROR_PROTOCOL, NULL, 0, context);
        }
      return NULL;
    }

  op_handle = ssh_gafp_operation_create(agent, context);

  if (!op_handle)
    {
      if (callback)
        {
          (*callback)(SSH_GAFP_ERROR_OPERATION_ACTIVE, NULL, 0, context);
        }

      return NULL;
    }

  op_ctx = ssh_operation_get_context(op_handle);
  op_ctx->op_callback = callback;

  if ((len + 4) <= SSH_GAFP_MAX_OPERATION_DATA_FRAGMENT)
    {
      /* Send the request packet. */
      SSH_DEBUG(SSH_D_NICETOKNOW,
                ("entire operation data will be sent immediately"));
      ssh_gafp_send(agent,
                    (SshPacketType)SSH_GAFP_MSG_KEY_OPERATION_WITH_CERTIFICATE,
                    SSH_FORMAT_UINT32,
                    (SshUInt32) agent->operation_id,
                    SSH_FORMAT_UINT32_STR,
                    certificate_type, strlen(certificate_type),
                    SSH_FORMAT_UINT32_STR,
                    certificate, certificate_len,
                    SSH_FORMAT_UINT32_STR,
                    operation, strlen(operation),
                    SSH_FORMAT_UINT32_STR, data, len,
                    SSH_FORMAT_END);
    }
  else
    {
      SSH_DEBUG(SSH_D_NICETOKNOW,
                ("operation data will be sent in fragments"));
      SSH_DEBUG(SSH_D_NICETOKNOW,
                ("sending fragment 1"));
      ssh_encode_buffer(op_ctx->op_data_buf,
                        SSH_FORMAT_UINT32_STR,
                        data, len,
                        SSH_FORMAT_END);

      op_ctx->op_with_cert = TRUE;
      op_ctx->op_data_name = ssh_xstrdup(operation);
      op_ctx->op_data_seq = 0;
      op_ctx->op_data_cert_enc = ssh_xstrdup(certificate_type);
      op_ctx->op_data_cert = ssh_xmemdup(certificate, certificate_len);
      op_ctx->op_data_cert_len = certificate_len;

      /* Send the first fragment */
      ssh_gafp_send(agent,
                    (SshPacketType)SSH_GAFP_MSG_OPERATION_DATA_FRAGMENT,
                    SSH_FORMAT_UINT32,
                    (SshUInt32) agent->operation_id,
                    SSH_FORMAT_UINT32_STR,
                    op_ctx->op_data_name, strlen(op_ctx->op_data_name),
                    SSH_FORMAT_UINT32,
                    op_ctx->op_data_seq,
                    SSH_FORMAT_DATA,
                    ssh_buffer_ptr(op_ctx->op_data_buf),
                    SSH_GAFP_MAX_OPERATION_DATA_FRAGMENT,
                    SSH_FORMAT_END);

      ssh_buffer_consume(op_ctx->op_data_buf,
                         SSH_GAFP_MAX_OPERATION_DATA_FRAGMENT);
    }



  /* Send the request packet. */

  return op_handle;
}

/* Performs a private-key operation with the user selected certificate.
   Calls the given callback when a reply has been received.
   The caller can free any argument strings as soon as this
   has returned (i.e., no need to wait until the callback has been
   called). */
SshOperationHandle
ssh_gafp_key_operation_with_selected_certificate(SshGafp agent,
                                         const char *operation,
                                         unsigned int num_certificates,
                                         const char **certificate_type,
                                         const unsigned char **certificate,
                                         size_t *certificate_len,
                                         const unsigned char *data,
                                         size_t len,
                                         SshGafpOpWithCertsCallback callback,
                                         void *context)
{
  SshOperationHandle op_handle;
  SshGafpOperationContext op_ctx;
  SshBufferStruct cert_buffer;
  unsigned int i;

  if (agent->got_eof)
    {
      if (callback)
        {
          (*callback)(SSH_GAFP_ERROR_FAILURE, NULL, NULL, 0, NULL, 0, context);
        }
      return NULL;
    }

  if (agent->state != SSH_GAFP_STATE_RUNNING)
    {
      if (callback)
        {
          (*callback)(SSH_GAFP_ERROR_PROTOCOL, NULL, NULL, 0, NULL, 0,
                      context);
        }
      return NULL;
    }

  op_handle = ssh_gafp_operation_create(agent, context);

  if (!op_handle)
    {
      if (callback)
        {
          (*callback)(SSH_GAFP_ERROR_OPERATION_ACTIVE, NULL, NULL, 0,
                      NULL, 0, context);
        }

      return NULL;
    }

  op_ctx = ssh_operation_get_context(op_handle);
  op_ctx->op_with_certs_callback = callback;

  ssh_buffer_init(&cert_buffer);

  for (i = 0; i < num_certificates; i++)
    {
      ssh_encode_buffer(&cert_buffer,
                        SSH_FORMAT_UINT32_STR,
                        certificate_type[i], strlen(certificate_type[i]),
                        SSH_FORMAT_UINT32_STR,
                        certificate[i], certificate_len[i],
                        SSH_FORMAT_END);

    }

  if ((len + 4) <= SSH_GAFP_MAX_OPERATION_DATA_FRAGMENT)
    {
      /* Send the request packet. */
      SSH_DEBUG(SSH_D_NICETOKNOW,
                ("entire operation data will be sent immediately"));
      ssh_gafp_send(agent,
                    (SshPacketType)
                    SSH_GAFP_MSG_KEY_OPERATION_WITH_SELECTED_CERTIFICATE,
                    SSH_FORMAT_UINT32,
                    (SshUInt32) agent->operation_id,
                    SSH_FORMAT_UINT32,
                    (SshUInt32) num_certificates,
                    SSH_FORMAT_UINT32_STR,
                    ssh_buffer_ptr(&cert_buffer), ssh_buffer_len(&cert_buffer),
                    SSH_FORMAT_UINT32_STR,
                    operation, strlen(operation),
                    SSH_FORMAT_UINT32_STR, data, len,
                    SSH_FORMAT_END);
    } else {
      SSH_DEBUG(SSH_D_NICETOKNOW,
                ("operation data will be sent in fragments"));
      SSH_DEBUG(SSH_D_NICETOKNOW,
                ("sending fragment 1"));
      ssh_encode_buffer(op_ctx->op_data_buf,
                        SSH_FORMAT_UINT32_STR,
                        data, len,
                        SSH_FORMAT_END);
      op_ctx->op_with_cert = TRUE;
      op_ctx->op_with_multicert = TRUE;
      op_ctx->op_data_name = ssh_xstrdup(operation);
      op_ctx->op_data_seq = 0;
      op_ctx->op_data_cert_enc = NULL;
      /* Wrap the number of certificates and cert buffer
         into cert store in the operation context. */
      op_ctx->op_data_cert = ssh_xmemdup(ssh_buffer_ptr(&cert_buffer),
                                         ssh_buffer_len(&cert_buffer));
      op_ctx->op_data_cert_len = ssh_buffer_len(&cert_buffer);
      ssh_buffer_clear(&cert_buffer);
      ssh_encode_buffer(&cert_buffer,
                        SSH_FORMAT_UINT32,
                        (SshUInt32) num_certificates,
                        SSH_FORMAT_UINT32_STR,
                        op_ctx->op_data_cert, op_ctx->op_data_cert_len,
                        SSH_FORMAT_END);
      ssh_xfree(op_ctx->op_data_cert);
      op_ctx->op_data_cert = ssh_xmemdup(ssh_buffer_ptr(&cert_buffer),
                                         ssh_buffer_len(&cert_buffer));
      op_ctx->op_data_cert_len = ssh_buffer_len(&cert_buffer);

      /* Send the first fragment */
      ssh_gafp_send(agent,
                    (SshPacketType)SSH_GAFP_MSG_OPERATION_DATA_FRAGMENT,
                    SSH_FORMAT_UINT32,
                    (SshUInt32) agent->operation_id,
                    SSH_FORMAT_UINT32_STR,
                    op_ctx->op_data_name, strlen(op_ctx->op_data_name),
                    SSH_FORMAT_UINT32,
                    op_ctx->op_data_seq,
                    SSH_FORMAT_DATA,
                    ssh_buffer_ptr(op_ctx->op_data_buf),
                    SSH_GAFP_MAX_OPERATION_DATA_FRAGMENT,
                    SSH_FORMAT_END);

      ssh_buffer_consume(op_ctx->op_data_buf,
                         SSH_GAFP_MAX_OPERATION_DATA_FRAGMENT);
    }
  ssh_buffer_uninit(&cert_buffer);

  return op_handle;
}

/* Performs a passphrase query. The agent will query the passphrase
   interactively from the user or use a stored passphrase. If
   always_ask_interactively is true, the passphrase storage is bypassed. */
SshOperationHandle ssh_gafp_passphrase_query(SshGafp agent,
                                             const char *passphrase_type,
                                             const char *client_program,
                                             const char *description,
                                             Boolean always_ask_interactively,
                                             SshGafpOpCallback callback,
                                             void *context)
{
  SshOperationHandle op_handle;
  SshGafpOperationContext op_ctx;

  if (agent->got_eof)
    {
      if (callback)
        {
          (*callback)(SSH_GAFP_ERROR_FAILURE, NULL, 0, context);
        }
      return NULL;
    }

  if (agent->state != SSH_GAFP_STATE_RUNNING)
    {
      if (callback)
        {
          (*callback)(SSH_GAFP_ERROR_PROTOCOL, NULL, 0, context);
        }
      return NULL;
    }

  op_handle = ssh_gafp_operation_create(agent, context);

  if (!op_handle)
    {
      if (callback)
        {
          (*callback)(SSH_GAFP_ERROR_OPERATION_ACTIVE, NULL, 0, context);
        }
      return NULL;
    }

  op_ctx = ssh_operation_get_context(op_handle);
  op_ctx->op_callback = callback;

  /* Send the request packet. */
  ssh_gafp_send(agent,
                (SshPacketType)SSH_GAFP_MSG_PASSPHRASE_QUERY,
                SSH_FORMAT_UINT32,
                (SshUInt32) agent->operation_id,
                SSH_FORMAT_UINT32_STR,
                passphrase_type, strlen(passphrase_type),
                SSH_FORMAT_UINT32_STR,
                client_program, strlen(client_program),
                SSH_FORMAT_UINT32_STR,
                description, strlen(description),
                SSH_FORMAT_BOOLEAN,
                always_ask_interactively,
                SSH_FORMAT_END);

  return op_handle;
}

/* Request random data from the agent. */
SshOperationHandle ssh_gafp_random(SshGafp agent,
                                   size_t random_len,
                                   SshGafpOpCallback callback,
                                   void *context)
{
  SshOperationHandle op_handle;
  SshGafpOperationContext op_ctx;

  if (agent->got_eof)
    {
      if (callback)
        {
          (*callback)(SSH_GAFP_ERROR_FAILURE, NULL, 0, context);
        }
      return NULL;
    }

  if (agent->state != SSH_GAFP_STATE_RUNNING)
    {
      if (callback)
        {
          (*callback)(SSH_GAFP_ERROR_PROTOCOL, NULL, 0, context);
        }
      return NULL;
    }

  op_handle = ssh_gafp_operation_create(agent, context);

  if (!op_handle)
    {
      if (callback)
        {
          (*callback)(SSH_GAFP_ERROR_OPERATION_ACTIVE, NULL, 0, context);
        }
      return NULL;
    }

  op_ctx = ssh_operation_get_context(op_handle);
  op_ctx->op_callback = callback;

  /* Send the request packet. */
  ssh_gafp_send(agent,
                (SshPacketType)SSH_GAFP_MSG_RANDOM,
                SSH_FORMAT_UINT32,
                (SshUInt32) agent->operation_id,
                SSH_FORMAT_UINT32, (SshUInt32) random_len,
                SSH_FORMAT_END);

  return op_handle;
}

SshOperationHandle ssh_gafp_quit(SshGafp agent,
                                 SshGafpCompletionCallback callback,
                                 void *context)
{
  SshOperationHandle op_handle;
  SshGafpOperationContext op_ctx;

  if (agent->got_eof)
    {
      if (callback)
        {
          (*callback)(SSH_GAFP_ERROR_FAILURE, context);
        }
      return NULL;
    }

  if (agent->state != SSH_GAFP_STATE_RUNNING)
    {
      if (callback)
        {
          (*callback)(SSH_GAFP_ERROR_PROTOCOL, context);
        }
      return NULL;
    }

  op_handle = ssh_gafp_operation_create(agent, context);

  if (!op_handle)
    {
      if (callback)
        {
          (*callback)(SSH_GAFP_ERROR_OPERATION_ACTIVE, context);
        }
      return NULL;
    }

  op_ctx = ssh_operation_get_context(op_handle);
  op_ctx->completion_callback = callback;

  /* Send the request. */
  ssh_gafp_send(agent,
                (SshPacketType)SSH_GAFP_MSG_QUIT,
                SSH_FORMAT_UINT32,
                (SshUInt32) agent->operation_id,
                SSH_FORMAT_END);

  return op_handle;
}


/* Internal functions */


void ssh_gafp_client_open_complete_external(SshGafp agent)
{

  /* Prepare to send version request. */
  /* keytype: "SshUInt32" valuetype: "SshOperationHandle" */
  agent->operation_map = ssh_adt_create_intmap();
  /*
     XXX should use newer version:

     #include "sshadt_conv.h"
     ssh_adt_xcreate_strmap(NULL_FNPTR, destructor)

  */

  agent->state = SSH_GAFP_STATE_STARTING;

  ssh_gafp_send(agent,
                (SshPacketType)SSH_GAFP_MSG_VERSION,
                SSH_FORMAT_UINT32,
                (SshUInt32)0,
                SSH_FORMAT_UINT32,
                (SshUInt32)SSH_GAFP_PROTOCOL_VERSION,
                SSH_FORMAT_UINT32_STR,
                agent->client_name,
                strlen(agent->client_name),
                SSH_FORMAT_UINT32,
                agent->client_version_major,
                SSH_FORMAT_UINT32,
                agent->client_version_minor,
                SSH_FORMAT_END);
}


/* Called when connecting to the agent socket completes. */
void ssh_gafp_client_open_complete(SshStream stream, void *context)
{
  SshGafp agent = (SshGafp)context;

  /* If failed to connect, simply return. */
  if (!stream)
    {
      SSH_DEBUG(SSH_D_FAIL, ("connection failed"));
      return;
    }

  /* Initialize the agent connection object. */
  agent->packet_conn = ssh_packet_wrap(stream,
                                       ssh_gafp_client_received_packet,
                                       ssh_gafp_client_received_eof,
                                       NULL_FNPTR,
                                       (void *)agent);
  ssh_packet_wrapper_can_receive(agent->packet_conn, TRUE);

  /* Send a version request message. */
  ssh_gafp_client_open_complete_external(agent);
}

/* This function is called whenever agent client receives a packet */
void ssh_gafp_client_received_packet(SshPacketType type,
                                     const unsigned char *data,
                                     size_t len,
                                     void *context)
{
  SshGafp agent = (void *)context;
  SshUInt32 protocol_version;
  SshUInt32 operation_id;
  SshUInt32 error_code;
  SshUInt32 seq_no;
  SshGafpOperationContext op_ctx;
  SshOperationHandle op_handle;
  size_t decoded_len;
  SshGafpKeyCert blobs;
  SshUInt32 num_blobs;
  const unsigned char *result;
  size_t result_len;
  int i;

  SSH_DEBUG(SSH_D_NICETOKNOW,
            ("received a packet type %d (length=%lu) in state %d",
             (int)type, (unsigned long)len, (int)agent->state));
  SSH_DEBUG_HEXDUMP(SSH_D_DATADUMP, ("Buffer (%d bytes):", len), data, len);

  op_ctx = (SshGafpOperationContext)NULL;
  operation_id = (SshUInt32)0;
  agent->error_state = SSH_GAFP_ERROR_OK;

  /* If we are starting up and the first packet is not
     version response, failure or notification, we have a protocol error */
  if ((agent->state == SSH_GAFP_STATE_STARTING) &&
     ((int)type != SSH_GAFP_MSG_RESPONSE_VERSION) &&
      ((int)type <  SSH_GAFP_NOTIFICATIONS_START))
    {
      ssh_gafp_fatal_error_handler(agent, SSH_GAFP_ERROR_PROTOCOL);
      return;
    }

  /* Notifications do not have operation id, so we handle them now */
  if (((int)type >=  SSH_GAFP_NOTIFICATIONS_START) &&
      ((int)type < SSH_GAFP_NOTIFICATIONS_END))
    {
      ssh_gafp_client_received_notification(type, data, len, context);
      return;
    }

  decoded_len = ssh_decode_array(data, len,
                                 SSH_FORMAT_UINT32, &operation_id,
                                 SSH_FORMAT_END);
  if (decoded_len == 0)
    {
      ssh_gafp_fatal_error_handler(agent, SSH_GAFP_ERROR_PROTOCOL);
      return;
    }

  data += decoded_len;
  len -= decoded_len;

  if (!ssh_gafp_op_id_is_reserved(operation_id))
    {
      op_handle = ssh_adt_intmap_get(agent->operation_map, operation_id);
      if (!op_handle)
        {
          ssh_gafp_fatal_error_handler(agent, SSH_GAFP_ERROR_PROTOCOL);
          return;
        }
      op_ctx = (SshGafpOperationContext)ssh_operation_get_context(op_handle);
      if (!op_ctx)
        {
          ssh_gafp_fatal_error_handler(agent, SSH_GAFP_ERROR_PROTOCOL);
          return;
        }
    }

  switch ((int)type)
    {
    case SSH_GAFP_MSG_RESPONSE_SUCCESS:
      if (len != 0)
        {
          agent->error_state = SSH_GAFP_ERROR_SIZE_ERROR;
        }

      if (op_ctx)
        {
          ssh_operation_unregister(op_ctx->op_handle);
          if (op_ctx->completion_callback)
            {
              (*op_ctx->completion_callback)(agent->error_state,
                                             op_ctx->context);
            }
          ssh_gafp_operation_destroy(op_ctx);
        }
      break;

    case SSH_GAFP_MSG_RESPONSE_FAILURE:
      decoded_len = ssh_decode_array(data, len,
                                     SSH_FORMAT_UINT32, &error_code,
                                     SSH_FORMAT_END);

      if ((decoded_len != len) || (len == 0))
        {
          agent->error_state = SSH_GAFP_ERROR_SIZE_ERROR;
        }
      else
        {
          agent->error_state = error_code;
        }

      if (op_ctx)
        {
          ssh_operation_unregister(op_ctx->op_handle);
          if (op_ctx->completion_callback)
            {
              (*op_ctx->completion_callback)(agent->error_state,
                                             op_ctx->context);
            }
          else if (op_ctx->op_callback)
            {
              (*op_ctx->op_callback)(agent->error_state,
                                     (const unsigned char *)NULL, (size_t)0,
                                     op_ctx->context);
            }
          else if (op_ctx->list_callback)
            {
              (*op_ctx->list_callback)(agent->error_state,
                                       0, (SshGafpKeyCert)NULL,
                                       op_ctx->context);

          } else if (op_ctx->op_with_certs_callback)
            {
              (*op_ctx->op_with_certs_callback)(agent->error_state,
                                                NULL, NULL, 0, NULL,
                                                0, op_ctx->context);

            }

          ssh_gafp_operation_destroy(op_ctx);
        }
      break;

    case SSH_GAFP_MSG_OPERATION_DATA_FRAGMENT_REPLY:
      decoded_len = ssh_decode_array(data, len,
                                     SSH_FORMAT_UINT32, &seq_no,
                                     SSH_FORMAT_UINT32, &error_code,
                                     SSH_FORMAT_END);
      if (op_ctx)
        {
          if (error_code == SSH_GAFP_ERROR_OK)
            {
              if (ssh_buffer_len(op_ctx->op_data_buf) <=
                  SSH_GAFP_MAX_OPERATION_DATA_FRAGMENT)
                {
                  /* Send the actual command */
                  SSH_DEBUG(SSH_D_NICETOKNOW,
                            ("all fragments sent, operation follows"));
                  if (op_ctx->op_with_multicert)
                    {
                      ssh_gafp_send(
                        agent,
                        (SshPacketType)
                        SSH_GAFP_MSG_KEY_OPERATION_WITH_SELECTED_CERTIFICATE,
                        SSH_FORMAT_UINT32,
                        (SshUInt32) agent->operation_id,
                        SSH_FORMAT_DATA, /* incl uint32 and string */
                        op_ctx->op_data_cert,
                        op_ctx->op_data_cert_len,
                        SSH_FORMAT_UINT32_STR,
                        op_ctx->op_data_name,
                        strlen(op_ctx->op_data_name),
                        SSH_FORMAT_DATA,
                        ssh_buffer_ptr(op_ctx->op_data_buf),
                        ssh_buffer_len(op_ctx->op_data_buf),
                        SSH_FORMAT_END);
                    }
                  else
                    {
                      ssh_gafp_send(
                        agent,
                        (op_ctx->op_with_cert ?
                         (SshPacketType)
                         SSH_GAFP_MSG_KEY_OPERATION_WITH_CERTIFICATE :
                         (SshPacketType)
                         SSH_GAFP_MSG_KEY_OPERATION),
                        SSH_FORMAT_UINT32,
                        (SshUInt32) agent->operation_id,
                        SSH_FORMAT_UINT32_STR,
                        op_ctx->op_data_cert_enc,
                        strlen(op_ctx->op_data_cert_enc),
                        SSH_FORMAT_UINT32_STR,
                        op_ctx->op_data_cert,
                        op_ctx->op_data_cert_len,
                        SSH_FORMAT_UINT32_STR,
                        op_ctx->op_data_name,
                        strlen(op_ctx->op_data_name),
                        SSH_FORMAT_DATA,
                        ssh_buffer_ptr(op_ctx->op_data_buf),
                        ssh_buffer_len(op_ctx->op_data_buf),
                        SSH_FORMAT_END);
                    }
                  ssh_buffer_consume(op_ctx->op_data_buf,
                                     ssh_buffer_len(op_ctx->op_data_buf));
                }
              else
                {
                  /* Send another fragment */
                  op_ctx->op_data_seq++;
                  SSH_DEBUG(SSH_D_NICETOKNOW,
                            ("sending fragment %d",
                             op_ctx->op_data_seq + 1));
                  ssh_gafp_send(
                    agent,
                    (SshPacketType)SSH_GAFP_MSG_OPERATION_DATA_FRAGMENT,
                    SSH_FORMAT_UINT32,
                    (SshUInt32) agent->operation_id,
                    SSH_FORMAT_UINT32_STR,
                    op_ctx->op_data_name, strlen(op_ctx->op_data_name),
                    SSH_FORMAT_UINT32,
                    op_ctx->op_data_seq,
                    SSH_FORMAT_DATA,
                    ssh_buffer_ptr(op_ctx->op_data_buf),
                    SSH_GAFP_MAX_OPERATION_DATA_FRAGMENT,
                    SSH_FORMAT_END);
                  ssh_buffer_consume(op_ctx->op_data_buf,
                                     SSH_GAFP_MAX_OPERATION_DATA_FRAGMENT);
                }
            }
          else
            {
              ssh_operation_unregister(op_ctx->op_handle);
              if (op_ctx->completion_callback)
                {
                  (*op_ctx->completion_callback)(agent->error_state,
                                                 op_ctx->context);
                }
              else if (op_ctx->op_callback)
                {
                  (*op_ctx->op_callback)(agent->error_state,
                                         (const unsigned char *)NULL,
                                         (size_t)0,
                                         op_ctx->context);
                }
              else if (op_ctx->list_callback)
                {
                  (*op_ctx->list_callback)(agent->error_state,
                                           0, (SshGafpKeyCert)NULL,
                                           op_ctx->context);

                } else if (op_ctx->op_with_certs_callback)
                  {
                    (*op_ctx->op_with_certs_callback)(agent->error_state,
                                                      NULL, NULL, 0, NULL,
                                                      0, op_ctx->context);

                  }
              ssh_gafp_operation_destroy(op_ctx);
            }
        }

      break;

    case SSH_GAFP_MSG_RESPONSE_KEY_LIST:
    case SSH_GAFP_MSG_RESPONSE_CERTIFICATE_LIST:
    case SSH_GAFP_MSG_RESPONSE_KEY_CERTIFICATE_LIST:
      decoded_len = ssh_decode_array(data, len,
                                     SSH_FORMAT_UINT32, &num_blobs,
                                     SSH_FORMAT_END);

      if (decoded_len == 0)
        {
          agent->error_state = SSH_GAFP_ERROR_SIZE_ERROR;
          num_blobs = 0;
          blobs = (SshGafpKeyCert)NULL;
          goto exit_case;
        }

      blobs = ssh_xcalloc(num_blobs, sizeof(blobs[0]));
      data += decoded_len;
      len -= decoded_len;
      for (i = 0; i < num_blobs; i++)
        {
          decoded_len = ssh_decode_array(data, len,
                                         SSH_FORMAT_UINT32_STR_NOCOPY,
                                         &blobs[i].blob_encoding, NULL,
                                         SSH_FORMAT_UINT32_STR_NOCOPY,
                                         &blobs[i].blob,
                                         &blobs[i].blob_len,
                                         SSH_FORMAT_UINT32_STR,
                                         &blobs[i].description, NULL,
                                         SSH_FORMAT_END);
          if (decoded_len == 0)
            {
              SSH_DEBUG(SSH_D_FAIL, ("Bad data blob %d", i));
              ssh_xfree(blobs);
              agent->error_state = SSH_GAFP_ERROR_FAILURE;
              num_blobs = 0;
              blobs = (SshGafpKeyCert)NULL;
              goto exit_case;
            }
          data += decoded_len;
          len -= decoded_len;
        }

      if (len != 0)
        {
          SSH_DEBUG(SSH_D_FAIL, ("Data left after blobs"));
          ssh_xfree(blobs);
          agent->error_state = SSH_GAFP_ERROR_SIZE_ERROR;
          num_blobs = 0;
          blobs = (SshGafpKeyCert)NULL;
          goto exit_case;
        }

    exit_case:
      if (op_ctx)
        {
          ssh_operation_unregister(op_ctx->op_handle);
          if (op_ctx->list_callback)
            {
              (*op_ctx->list_callback)(agent->error_state,
                                       (unsigned int)num_blobs,
                                       blobs, op_ctx->context);
            }
          ssh_gafp_operation_destroy(op_ctx);
        }

      for (i = 0; i < num_blobs; i++)
        {
          ssh_xfree((void *)blobs[i].description);
        }
      ssh_xfree(blobs);

      break;

    case SSH_GAFP_MSG_RESPONSE_KEY_OPERATION_COMPLETE:
    case SSH_GAFP_MSG_RESPONSE_RANDOM_DATA:
    case SSH_GAFP_MSG_RESPONSE_PASSPHRASE:
      decoded_len = ssh_decode_array(data, len,
                                     SSH_FORMAT_UINT32_STR_NOCOPY,
                                     &result, &result_len,
                                     SSH_FORMAT_END);

      if ((decoded_len != len) || (len == 0))
        {
          agent->error_state = SSH_GAFP_ERROR_SIZE_ERROR;
          result = NULL;
          result_len = 0;
        }

      if (op_ctx)
        {
          ssh_operation_unregister(op_ctx->op_handle);
          if (op_ctx->op_callback)
            {
              (*op_ctx->op_callback)(agent->error_state, result, result_len,
                                     op_ctx->context);
            }
          ssh_gafp_operation_destroy(op_ctx);
        }

      break;

    case SSH_GAFP_MSG_RESPONSE_SELECTED_KEY_OPERATION_COMPLETE:
      blobs = ssh_xmalloc(sizeof(blobs[0]));

      decoded_len = ssh_decode_array(data, len,
                                     SSH_FORMAT_UINT32_STR_NOCOPY,
                                     &blobs[0].blob_encoding, NULL,
                                     SSH_FORMAT_UINT32_STR_NOCOPY,
                                     &blobs[0].blob,
                                     &blobs[0].blob_len,
                                     SSH_FORMAT_UINT32_STR_NOCOPY,
                                     &result, &result_len,
                                     SSH_FORMAT_END);

      if ((decoded_len != len) || (len == 0))
        {
          agent->error_state = SSH_GAFP_ERROR_SIZE_ERROR;
          result = NULL;
          result_len = 0;
        }

      if (op_ctx)
        {
          ssh_operation_unregister(op_ctx->op_handle);
          if (op_ctx->op_with_certs_callback)
            {
              (*op_ctx->op_with_certs_callback)(agent->error_state,
                                                blobs[0].blob_encoding,
                                                blobs[0].blob,
                                                blobs[0].blob_len,
                                                result, result_len,
                                                op_ctx->context);
            }
          ssh_gafp_operation_destroy(op_ctx);
        }

      break;

    case SSH_GAFP_MSG_RESPONSE_VERSION:
      decoded_len =
        ssh_decode_array(data, len,
                         SSH_FORMAT_UINT32, &protocol_version,
                         SSH_FORMAT_UINT32_STR, &(agent->agent_name), NULL,
                         SSH_FORMAT_UINT32, &(agent->agent_version_major),
                         SSH_FORMAT_UINT32, &(agent->agent_version_minor),
                         SSH_FORMAT_END);

      if ((agent->state != SSH_GAFP_STATE_STARTING) || (decoded_len != len) ||
          (len == 0) || (protocol_version != SSH_GAFP_PROTOCOL_VERSION))
        {
          ssh_gafp_fatal_error_handler(agent, SSH_GAFP_ERROR_PROTOCOL);
          return;
        }

      agent->state = SSH_GAFP_STATE_RUNNING;
      agent->error_state = SSH_GAFP_ERROR_OK;
      if (agent->open_handle)
        ssh_operation_unregister(agent->open_handle);
      agent->open_handle = NULL;

      if (agent->open_callback)
        {
          (*agent->open_callback)(agent, agent->connection_context);
        }

      break;

    case SSH_GAFP_MSG_EXTENDED:
      if (op_ctx)
        {
          ssh_operation_unregister(op_ctx->op_handle);
          if (op_ctx->extension_callback)
            {
              (*op_ctx->extension_callback)(data, len, op_ctx->context);
            }
          ssh_gafp_operation_destroy(op_ctx);
        }
      break;

    default:
      ssh_gafp_fatal_error_handler(agent, SSH_GAFP_ERROR_PROTOCOL);
      break;
    }

}

void ssh_gafp_forwarding_notice(SshGafp agent,
                                unsigned char *buf_out,
                                size_t buf_out_len,
                                const char *forwarding_command,
                                const char *host_name,
                                const char *ip_address,
                                const char *tcp_port)
{
  SshUInt32 num_pairs;
  size_t len;

  len = ssh_decode_array(buf_out, buf_out_len,
                         SSH_FORMAT_UINT32, &num_pairs,
                         SSH_FORMAT_END);

  if (len == 0)
    {
      ssh_gafp_fatal_error_handler(agent, SSH_GAFP_ERROR_PROTOCOL);
      return;
    }

  num_pairs++;

  ssh_gafp_send(agent,
                (SshPacketType)SSH_GAFP_MSG_NOTIFICATION_FORWARDED_CONNECTION,
                SSH_FORMAT_UINT32,
                num_pairs,
                SSH_FORMAT_UINT32_STR,
                SSH_GAFP_FWD_CLIENT_STRING,
                strlen(SSH_GAFP_FWD_CLIENT_STRING),
                SSH_FORMAT_UINT32_STR,
                forwarding_command, strlen(forwarding_command),
                SSH_FORMAT_UINT32_STR,
                SSH_GAFP_FWD_HOST_STRING,
                strlen(SSH_GAFP_FWD_HOST_STRING),
                SSH_FORMAT_UINT32_STR,
                host_name, strlen(host_name),
                SSH_FORMAT_UINT32_STR,
                SSH_GAFP_FWD_IP_STRING,
                strlen(SSH_GAFP_FWD_IP_STRING),
                SSH_FORMAT_UINT32_STR,
                ip_address, strlen(ip_address),
                SSH_FORMAT_UINT32_STR,
                SSH_GAFP_FWD_TCPPORT_STRING,
                strlen(SSH_GAFP_FWD_TCPPORT_STRING),
                SSH_FORMAT_UINT32_STR,
                tcp_port, strlen(tcp_port),
                SSH_FORMAT_DATA,
                buf_out + len, buf_out_len - len,
                SSH_FORMAT_END);
}

Boolean ssh_gafp_receive_packet_external(SshGafp agent,
                                         const unsigned char *packet_data,
                                         size_t packet_len)
{
  SshPacketType type;
  const unsigned char *real_data;
  size_t real_len;
  Boolean rv;

  rv = FALSE;

  if (agent->packet_send_callback)
    {
      type = (SshPacketType)packet_data[4];
      real_data = packet_data + 5;
      real_len = packet_len - 5;

      agent->error_state = SSH_GAFP_ERROR_OK;
      ssh_gafp_client_received_packet(type, real_data, real_len, agent);

      if (agent->error_state == SSH_GAFP_ERROR_OK)
        {
          rv = TRUE;
        }
    }

  return rv;
}

/* This is called when EOF is received from the agent. */
void ssh_gafp_client_received_eof(void *context)
{
  SshGafp agent = (SshGafp)context;

  SSH_DEBUG(SSH_D_NICETOKNOW, ("Got eof from agent connection"));

  agent->got_eof = TRUE;
  if (agent->connection_closed_callback)
    {
      (*agent->connection_closed_callback)(SSH_GAFP_ERROR_EOF,
                                           agent->close_context);
    }
}

/* This function increments the operation counter skipping over
   the reserved value 0. */
void ssh_gafp_op_id_increment(SshGafp agent)
{
  agent->operation_id++;

  if (ssh_gafp_op_id_is_reserved(agent->operation_id))
    {
      agent->operation_id++;
    }
}

Boolean ssh_gafp_op_id_is_reserved(SshUInt32 operation_id)
{
  return ((operation_id == 0) || (operation_id == -1));
}

void ssh_gafp_client_received_notification(SshPacketType type,
                                           const unsigned char *data,
                                           size_t len,
                                           void *context)
{
  SshGafp agent = (SshGafp)context;

  if ((((int)type) < SSH_GAFP_NOTIFICATIONS_START) ||
      (((int)type) > SSH_GAFP_NOTIFICATIONS_END))
    {
      /* It's not a notification message. */
      SSH_DEBUG(SSH_D_FAIL,
                ("notification handler got invalid type %d packet",
                 (int)type));
      return;
    }

  if (agent->notification_callback)
    {
      (*agent->notification_callback)(type,
                                      data,
                                      len,
                                      agent->notification_context);
    }
}

static void ssh_gafp_operation_destroy(SshGafpOperationContext op_ctx)
{
  if (op_ctx)
    {
      ssh_xfree(op_ctx->op_data_name);
      ssh_xfree(op_ctx->op_data_cert_enc);
      ssh_xfree(op_ctx->op_data_cert);
      ssh_buffer_uninit(op_ctx->op_data_buf);
      ssh_xfree(op_ctx);
    }
}

static SshOperationHandle ssh_gafp_operation_create(SshGafp agent,
                                                    void *context)
{
  SshOperationHandle op_handle;
  SshGafpOperationContext op_ctx;

  op_handle = NULL;

  ssh_gafp_op_id_increment(agent);

  if (!ssh_adt_intmap_exists(agent->operation_map, agent->operation_id))
    {
      op_ctx = (SshGafpOperationContext)ssh_xcalloc(1, sizeof(*op_ctx));
      op_handle = ssh_operation_register(ssh_gafp_operation_abort_callback,
                                         op_ctx);
      op_ctx->op_handle = op_handle;
      op_ctx->op_id = agent->operation_id;
      op_ctx->agent = agent;
      op_ctx->context = context;
      op_ctx->in_abort = FALSE;
      ssh_buffer_init(op_ctx->op_data_buf);
      op_ctx->op_data_seq = 0;

      op_ctx->completion_callback = (SshGafpCompletionCallback)NULL;
      op_ctx->list_callback = (SshGafpListCallback)NULL;
      op_ctx->op_callback = (SshGafpOpCallback)NULL;
      op_ctx->extension_callback = (SshGafpExtensionCallback)NULL;

      ssh_operation_attach_destructor(op_handle,
                                      ssh_gafp_operation_destructor_callback,
                                      op_ctx);
      ssh_adt_intmap_add(agent->operation_map, agent->operation_id,
                         op_handle);
    }
  else
    {
      SSH_DEBUG(SSH_D_FAIL, ("Operation %d exists already",
                             agent->operation_id));
    }

  return op_handle;
}


void ssh_gafp_operation_abort_callback(void *context)
{
  SshGafpOperationContext op_ctx = (SshGafpOperationContext)context;

  op_ctx->in_abort = TRUE;

  if (ssh_adt_intmap_exists(op_ctx->agent->operation_map, op_ctx->op_id))
    {
      ssh_gafp_send(op_ctx->agent,
                    (SshPacketType)SSH_GAFP_MSG_ABORT_OPERATION,
                    SSH_FORMAT_UINT32, (SshUInt32)0,
                    SSH_FORMAT_UINT32,
                    (SshUInt32)op_ctx->op_id,
                    SSH_FORMAT_END);
    }
  else
    {
      SSH_DEBUG(SSH_D_FAIL, ("Aborted operation does not exist"));
    }
}

void ssh_gafp_operation_destructor_callback(Boolean aborted, void *context)
{
  SshGafpOperationContext op_ctx = (SshGafpOperationContext)context;

    if (ssh_adt_intmap_exists(op_ctx->agent->operation_map, op_ctx->op_id))
      {
        ssh_adt_intmap_remove(op_ctx->agent->operation_map, op_ctx->op_id);
        op_ctx->op_handle = (SshOperationHandle)NULL;
        if (op_ctx->in_abort)
          {
            /* If we are aborting the call we should free the context here. */
            ssh_gafp_operation_destroy(op_ctx);
          }
      }
    else
      {
        SSH_DEBUG(SSH_D_FAIL, ("Unregistered operation does not exist"));
      }

}

void ssh_gafp_send(SshGafp agent, SshPacketType type, ...)
{
  va_list ap;

  va_start(ap, type);
  ssh_gafp_send_va(agent, type, ap);
  va_end(ap);

  return;
}

void ssh_gafp_send_va(SshGafp agent, SshPacketType type, va_list ap)
{
  SshBufferStruct buffer;
  size_t payload_size;
  unsigned char *p;

  if (agent->got_eof)
    {
      /* We have received EOF. The ssh_gafp_close or operation abort
         can still try to send something. */
      SSH_DEBUG(SSH_D_FAIL,
                ("Tried to send something, but EOF was already received"));
      return;
    }

  if (agent->packet_send_callback)
    {
      if (agent->connection_down)
        {
          /* External connection is down, we can not do anything
             here. */
          return;
        }

      ssh_buffer_init(&buffer);

      /* Construct the packet header with dummy length. */
      ssh_encode_buffer(&buffer,
                        SSH_FORMAT_UINT32, (SshUInt32) 0,
                        SSH_FORMAT_CHAR, (unsigned int)type,
                        SSH_FORMAT_END);

      /* Encode the packet payload. */
      payload_size = ssh_encode_buffer_va(&buffer, ap);

      /* Update the packet header to contain the correct payload size. */
      p = ssh_buffer_ptr(&buffer);
      SSH_PUT_32BIT(p, payload_size + 1);

      agent->connection_down =
        (*agent->packet_send_callback)(ssh_buffer_ptr(&buffer),
                                       ssh_buffer_len(&buffer),
                                       agent->packet_send_context);
      ssh_buffer_uninit(&buffer);
    }
  else
    {
      ssh_packet_wrapper_send_encode_va(agent->packet_conn,
                                        type, ap);
    }

  return;
}

void ssh_gafp_fatal_error_handler(SshGafp agent, SshGafpError error)
{
  SshADTHandle handle;

  agent->error_state = error;

  SSH_DEBUG(SSH_D_FAIL, ("Fatal error handler called"));

  for (handle = ssh_adt_enumerate_start(agent->operation_map);
       handle != SSH_ADT_INVALID;
       handle = ssh_adt_enumerate_next(agent->operation_map, handle))
    {
      SshOperationHandle op_handle;
      SshUInt32 *op_id = (SshUInt32 *)NULL;
      SshGafpOperationContext op_ctx = (SshGafpOperationContext)NULL;

      op_id = (SshUInt32 *)ssh_adt_get(agent->operation_map, handle);
      op_handle =
        (SshOperationHandle)ssh_adt_intmap_get(agent->operation_map,
                                               *op_id);
      if (op_handle)
        {
          op_ctx =
            (SshGafpOperationContext)ssh_operation_get_context(op_handle);
          if (op_ctx)
            {
              if (op_ctx->completion_callback)
                {
                  (*op_ctx->completion_callback)(agent->error_state,
                                                 op_ctx->context);
                }
              else if (op_ctx->list_callback)
                {
                  (*op_ctx->list_callback)(agent->error_state,
                                           0, (SshGafpKeyCert)NULL,
                                           op_ctx->context);
                }
              else if (op_ctx->op_callback)
                {
                  (*op_ctx->op_callback)(agent->error_state,
                                         (const unsigned char *)NULL,
                                         (size_t)0,
                                         op_ctx->context);
                }
              else if (op_ctx->extension_callback)
                {
                  (*op_ctx->extension_callback)((const unsigned char *)NULL,
                                                (size_t)0, op_ctx->context);
                }
            }
        }
    }

  if (agent->connection_closed_callback)
    {
      (*agent->connection_closed_callback)(error,
                                           agent->close_context);
    }
}

/* eof (gafpclient.c) */
