/*

sshgafpi.h

Author: Hannu K. Napari

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
                   All rights reserved

Internal header to ssh GAFP library.

*/

#ifndef _SSHGAFPI_H_
#define _SSHGAFPI_H_

#include "sshadt_intmap.h"
#include "sshgafp.h"
#include "sshbuffer.h"

#define SSH_GAFP_MAX_OPERATION_DATA_FRAGMENT 0x10000

/* Internal status of the agent client. */
typedef enum {
  SSH_GAFP_STATE_INITIAL,
  SSH_GAFP_STATE_STARTING,
  SSH_GAFP_STATE_RUNNING
} SshGafpClientState;

struct SshGafpRec {
  /* The agent uses a ssh packet wrapper */
  SshPacketWrapper packet_conn;

  /* The context that was used to connect to the agent */
  void *connection_context;

  /* The context that is used with the close callback */
  void *close_context;

  /* Agent name, as returned by agent request. */
  char *agent_name;

  /* Agent version, major */
  SshUInt32 agent_version_major;

  /* Agent version, minor */
  SshUInt32 agent_version_minor;

  /* Client name string */
  char *client_name;

  /* Client version, major */
  SshUInt32 client_version_major;

  /* Client version, minor */
  SshUInt32 client_version_minor;

  /* Counter for operation ids. Id 0 is reserved for version query, abort and
     notifications. */
  SshUInt32 operation_id;

  /* Integer map containing the operation handles */
  SshADTContainer operation_map;

  /* Have we received eof from the agent? */
  Boolean got_eof;

  /* Client state information */
  SshGafpClientState state;

  /* Operation handle, which will get unregistered when the
     connection has been opened. */
  SshOperationHandle open_handle;

  /* Operation handle for ssh_local_connect */
  SshOperationHandle connect_handle;

  /* This callback is called when a notification is received */
  SshGafpNotificationCallback notification_callback;
  void *notification_context;

  /* This callback is called if connection is unecpectedly closed */
  SshGafpCompletionCallback connection_closed_callback;

  /* This callback gets called when the connection is complete */
  SshGafpOpenCallback open_callback;

  /* Packet sending callback for external communication */
  SshGafpPacketSendCallback packet_send_callback;
  void *packet_send_context;
  Boolean connection_down;

  /* GAFP error state */
  SshGafpError error_state;
};

typedef struct SshGafpOperationContextRec {
  SshOperationHandle op_handle;
  SshUInt32 op_id;
  void *context;
  Boolean in_abort;

  /* Various callbacks, depending on the state. */
  SshGafpCompletionCallback completion_callback;
  SshGafpListCallback list_callback;
  SshGafpOpCallback op_callback;
  SshGafpOpWithCertsCallback op_with_certs_callback;

  /* Buffer for sending data in chunks */
  SshBufferStruct op_data_buf[1];
  SshUInt32 op_data_seq;
  char *op_data_name;
  char *op_data_cert_enc;
  unsigned char *op_data_cert;
  size_t op_data_cert_len;
  Boolean op_with_cert;
  Boolean op_with_multicert;

  /* Extension callback */
  SshGafpExtensionCallback extension_callback;

  SshGafp agent;
} *SshGafpOperationContext;

/* Client version string.  Version defaults to this if client software
   does not provide its own. */
#define SSH_GAFP_LIBRARY_VERSION_STRING "SSH GAFP Client Library 0.99"
#define SSH_GAFP_LIBRARY_VERSION_MAJOR  0
#define SSH_GAFP_LIBRARY_VERSION_MINOR  99

/* Default key blob encoding string */
#define SSH_GAFP_DEFAULT_KEY_ENCODING SSH_GAFP_KEY_ENCODING_SSH

/* Forwarding information strings */
#define SSH_GAFP_FWD_CLIENT_STRING  "forwarder"
#define SSH_GAFP_FWD_HOST_STRING    "host-name"
#define SSH_GAFP_FWD_IP_STRING      "ip-address"
#define SSH_GAFP_FWD_TCPPORT_STRING "tcp-port"

/* Connects to an existing authentication agent.  Stream to the
   authentication agent is assumed opened.  After this call, the
   caller is not allowed to access the stream.  Call to
   ssh_gafp_close destroys the stream.  This calls the given function
   when the connection is complete. */
void ssh_gafp_open_stream(SshStream auth_stream,
                          const char *client_name,
                          SshUInt32 client_version_major,
                          SshUInt32 client_version_minor,
                          SshGafpOpenCallback callback, 
                          SshGafpCompletionCallback 
                          connection_closed_callback,
                          void *context);

#endif /* _SSHGAFPI_H_ */
/* eof (sshgafpi.h) */
