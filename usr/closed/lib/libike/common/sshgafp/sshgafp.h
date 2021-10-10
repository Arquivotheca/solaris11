/*

sshgafp.h

Author: Hannu K. Napari <Hannu.Napari@ssh.com>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
                   All rights reserved

Public interface to ssh GAFP library.

*/

#ifndef _SSHGAFP_H_
#define _SSHGAFP_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "sshlocalstream.h"
#include "sshcrypt.h"

/* Generic Authentication Forwarding Protocol */

/* Protocol version */
#define SSH_GAFP_PROTOCOL_VERSION 3

/* Message types */
typedef enum {
  /* Version exchange (client <-> agent) */
  SSH_GAFP_MSG_VERSION = 1,

  /* Agent control commands */
  SSH_GAFP_MSG_QUIT = 2,
  SSH_GAFP_MSG_CLOSE = 3,

  /* Abort an ongoing operation */
  SSH_GAFP_MSG_ABORT_OPERATION = 4,

  /* Key administration messages (client -> agent) */
  SSH_GAFP_MSG_ADD_KEY = 10,
  SSH_GAFP_MSG_ADD_CERTIFICATE = 11,
  SSH_GAFP_MSG_DELETE_ALL_KEYS = 12,
  SSH_GAFP_MSG_DELETE_KEY = 13,
  SSH_GAFP_MSG_DELETE_KEY_CERTIFICATE = 14,
  SSH_GAFP_MSG_ADD_EXTRA_CERTIFICATE = 15,
  SSH_GAFP_MSG_DELETE_EXTRA_CERTIFICATE = 16,

  /* Key retrieval messages (client -> agent) */
  SSH_GAFP_MSG_LIST_KEYS = 50,
  SSH_GAFP_MSG_LIST_KEY_CERTIFICATES = 51,
  SSH_GAFP_MSG_LIST_CERTIFICATES = 52,
  SSH_GAFP_MSG_LIST_EXTRA_CERTIFICATES = 53,

  /* Operations (client -> agent) */
  SSH_GAFP_MSG_PING = 100,
  SSH_GAFP_MSG_KEY_OPERATION = 101,
  SSH_GAFP_MSG_KEY_OPERATION_WITH_CERTIFICATE = 102,
  SSH_GAFP_MSG_PASSPHRASE_QUERY = 103,
  SSH_GAFP_MSG_RANDOM = 104,
  SSH_GAFP_MSG_KEY_OPERATION_WITH_SELECTED_CERTIFICATE = 105,
  SSH_GAFP_MSG_OPERATION_DATA_FRAGMENT = 106,

  /* Responses to client requests (agent -> client) */
  SSH_GAFP_MSG_RESPONSE_SUCCESS = 150,
  SSH_GAFP_MSG_RESPONSE_FAILURE = 151,
  SSH_GAFP_MSG_RESPONSE_KEY_LIST = 152,
  SSH_GAFP_MSG_RESPONSE_KEY_CERTIFICATE_LIST = 153,
  SSH_GAFP_MSG_RESPONSE_CERTIFICATE_LIST = 154,
  SSH_GAFP_MSG_RESPONSE_KEY_OPERATION_COMPLETE = 155,
  SSH_GAFP_MSG_RESPONSE_PASSPHRASE = 156,
  SSH_GAFP_MSG_RESPONSE_RANDOM_DATA = 157,
  SSH_GAFP_MSG_RESPONSE_VERSION = 158,
  SSH_GAFP_MSG_RESPONSE_SELECTED_KEY_OPERATION_COMPLETE = 159,
  SSH_GAFP_MSG_OPERATION_DATA_FRAGMENT_REPLY = 160,

  /* Begin notifications range */
  SSH_GAFP_NOTIFICATIONS_START = 200,

  /* Notifications (client -> agent) (200-209) */
  SSH_GAFP_MSG_NOTIFICATION_FORWARDED_CONNECTION = 200,
  /* Notifications (agent -> client) (210-250) */
  SSH_GAFP_MSG_NOTIFICATION_KEY_ADDED = 210,
  SSH_GAFP_MSG_NOTIFICATION_KEY_DELETED = 211,
  SSH_GAFP_MSG_NOTIFICATION_CERTIFICATE_ADDED = 212,
  SSH_GAFP_MSG_NOTIFICATION_CERTIFICATE_DELETED = 213,
  SSH_GAFP_MSG_NOTIFICATION_EXTRA_CERTIFICATE_ADDED = 214,
  SSH_GAFP_MSG_NOTIFICATION_EXTRA_CERTIFICATE_DELETED = 215,

  /* End notifications range */
  SSH_GAFP_NOTIFICATIONS_END = 250,

  /* Extension packets (client <-> agent) */
  SSH_GAFP_MSG_EXTENDED = 255

} SshGafpMsgType;

/* Error codes */
typedef enum {
  SSH_GAFP_ERROR_OK = 0,                /* Operation completed successfully. */
  SSH_GAFP_ERROR_TIMEOUT = 1,           /* Operation timed out. */
  SSH_GAFP_ERROR_KEY_NOT_FOUND = 2,     /* Private key is not available. */
  SSH_GAFP_ERROR_DECRYPT_FAILED = 3,    /* Decryption failed. */
  SSH_GAFP_ERROR_SIZE_ERROR = 4,        /* Data size is inappropriate. */
  SSH_GAFP_ERROR_KEY_NOT_SUITABLE = 5,  /* Key is not suitable for request. */
  SSH_GAFP_ERROR_DENIED = 6,            /* Administratively prohibited. */
  SSH_GAFP_ERROR_FAILURE = 7,           /* Unspecific agent error. */
  SSH_GAFP_ERROR_UNSUPPORTED_OP = 8,    /* Operation not supported by agent. */
  SSH_GAFP_ERROR_PROTOCOL = 9,             /* Protocol error */
  SSH_GAFP_ERROR_OPERATION_ACTIVE = 10,    /* Protocol error */
  SSH_GAFP_ERROR_OPERATION_NOT_FOUND = 11, /* Protocol error */
  SSH_GAFP_ERROR_EOF = 12,                 /* EOF received, this is not
                                              necessarily an error. */
  SSH_GAFP_ERROR_SEQUENCE_NUMBER = 13,     /* Sequence number error in
                                              fragmented data. */
  SSH_GAFP_ERROR_SIZE_DENIED = 14          /* Data size exceeds the
                                              administrative limit. */
} SshGafpError;

/* A data structure used for storing key and certificate information. */
typedef struct SshGafpKeyCertRec {
  const char *blob_encoding;
  const unsigned char *blob;
  size_t blob_len;
  const char *description;
} *SshGafpKeyCert;

/* An opaque structure that carries information of the agent the client
   is connected to. */
typedef struct SshGafpRec *SshGafp;

#define SSH_GAFP_KEY_ENCODING_SSH   "ssh-crypto-library-public-key@ssh.com"
#define SSH_GAFP_KEY_ENCODING_PKCS1 "pkcs#1"

/* Unix specific environment variables */
#ifndef WIN32
#define SSH_GAFP_SOCK_VAR "SSH_AA_SOCK"
#define SSH_GAFP_PID_VAR "SSH_AA_PID"
#else
#define SSH_GAFP_SOCK_VAR "SSH Accession"
#endif /* WIN32 */

/* Agent client callback types. */

/* Callback to be called by operations that return a success/failure result. */
typedef void (*SshGafpCompletionCallback)(SshGafpError result,
                                          void *context);

/* Callback function to be called when a opening a connection to the
   agent completes.  If `agent' is NULL, connecting to the agent failed.
   Otherwise, it is a pointer to an agent connection object that can be
   used to perform operations on the agent. */
typedef void (*SshGafpOpenCallback)(SshGafp agent,
                                    void *context);

/* Callback for ssh_gafp_list.  This is passed the public keys (or
   certificates) for all private keys that the agent has in its
   possession.  The data will be freed automatically when the callback
   returns, so if it is needed for a longer time, it must be copied
   by the callback. */
typedef void (*SshGafpListCallback)(SshGafpError error,
                                    unsigned int num_keys,
                                    SshGafpKeyCert keys,
                                    void *context);

/* Callback function that is called when an operation is
   complete.  The data passed as the argument is only valid until this call
   returns, and this must copy the data if it needs to be accessed after
   returning. */
typedef void (*SshGafpOpCallback)(SshGafpError error,
                                  const unsigned char *result,
                                  size_t len,
                                  void *context);


/* Callback function that is called when an operation with selected certificate
   is complete.  The data passed as the argument is only valid until this call
   returns, and this must copy the data if it needs to be accessed after
   returning. */
typedef void (*SshGafpOpWithCertsCallback)(SshGafpError error,
                                           const char *certificate_type,
                                           const unsigned char *certificate, 
                                           size_t certificate_len,
                                           const unsigned char *result,
                                           size_t len,
                                           void *context);

/* Callback function that is called when a a notification message
   arrives from the agent.  The data passed as the argument is only
   valid until this call returns, and this must copy the data if it
   needs to be accessed after returning. */
typedef void (*SshGafpNotificationCallback)
       (SshGafpMsgType notification_type,
        const unsigned char *data, 
        size_t data_len,
        void *context);

/* Callback function for extension packets. This gets called with the
   whole contents of the received packet, including operation id. */
typedef void (*SshGafpExtensionCallback)(const unsigned char *data,
                                         size_t len,
                                         void *context);

/* GAFP library calls this callback in order to send a packet to the
   agent using external communications method.  This callback should
   return TRUE if the packet is sent (or queued) succesfully.  If
   packet can't be sent or queued, return value is FALSE.  Return
   value FALSE also means that all upcoming calls will also fail
   (i.e. connection has been closed).  This callback is provided to the
   GAFP library as a parameter of ssh_gafp_open_external. */
typedef Boolean (*SshGafpPacketSendCallback)(const unsigned char *packet_data,
                                             size_t packet_len, 
                                             void *context);

/* Interfaces for starting and communicating to the agent service. */

/* Connects to an existing authentication agent.  In Unix, this gets
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
              void *context);

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
                            void *context);

/* Hand over the externally received packet (from the agent) to the
   GAFP library.  This function returns TRUE on succesful execution.
   If the return value is FALSE, GAFP library has failed to handle the
   current packet and will not be able to handle any further packets.
   This function can only be used, if connection to the agent has been
   opened with ssh_gafp_open_external. */
Boolean ssh_gafp_receive_packet_external(SshGafp agent,
                                         const unsigned char *packet_data, 
                                         size_t packet_len);

/* Tell GAFP library that external connection to the agent has closed
   and no more packets will be provided.  This function can only be
   used, if connection to the agent has been opened with
   ssh_gafp_open_external.  This usually triggers connection closed
   callback to be called immediately. */
void ssh_gafp_receive_eof_external(SshGafp agent);

/* Interfaces for agent clients.  These interfaces are the ones that
   clients connected to the agent uses. */

/* Register a notification callback to receive notifications about
   agent state */
void ssh_gafp_register_notification_callback(SshGafp agent,
                                             SshGafpNotificationCallback cb,
                                             void *context);

/* Register a connection close callback to be called when the agent
   stream sends eof. */
void ssh_gafp_register_close_callback(SshGafp agent,
                                      SshGafpCompletionCallback callback,
                                      void *context);

/* Closes the connection to the authentication agent. If a command is
   active, it is terminated and its callback will never be called. */
void ssh_gafp_close(SshGafp agent);

/* Adds the given public and private key to the agent.
   The callback can be NULL. */
SshOperationHandle
ssh_gafp_add_key(SshGafp agent,
                 SshPublicKey public_key,
                 const char *private_key_encoding,
                 const unsigned char *private_key_data,
                 size_t private_key_data_len,
                 const char *description,
                 SshGafpCompletionCallback callback,
                 void *context);

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
                   void *context);

/* Adds the given certificate to the agent.  The callback can be NULL. */
SshOperationHandle
ssh_gafp_add_certificate(SshGafp agent,
                         SshPublicKey key,
                         const char *certificate_type,
                         const unsigned char *certificate,
                         size_t certificate_len,
                         const char *description,
                         SshGafpCompletionCallback callback,
                         void *context);

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
                           void *context);

/* Adds the given certificate without a private key to the agent.
   The callback can be NULL. */
SshOperationHandle
ssh_gafp_add_extra_certificate(SshGafp agent,
                               const char *certificate_type,
                               const unsigned char *certificate,
                               size_t certificate_len,
                               const char *description,
                               SshGafpCompletionCallback callback,
                               void *context);

/* Deletes all keys and certificates from the agent.
   The callback can be NULL. */
SshOperationHandle ssh_gafp_delete_all_keys(SshGafp agent, 
                                            SshGafpCompletionCallback callback,
                                            void *context);

/* Deletes the given key from the agent. All certificates for this key
   are deleted as well. */
SshOperationHandle ssh_gafp_delete_key(SshGafp agent,
                                       SshPublicKey public_key,
                                       SshGafpCompletionCallback callback, 
                                       void *context);

SshOperationHandle ssh_gafp_delete_key_2(SshGafp agent,
                                         const char *public_key_encoding,
                                         const unsigned char *public_key_data,
                                         size_t public_key_data_len,
                                         SshGafpCompletionCallback callback, 
                                         void *context);

/* Deletes the given certificate from the agent. The callback can be NULL. */
SshOperationHandle
ssh_gafp_delete_key_certificate(SshGafp agent, 
                                SshPublicKey public_key,
                                const char *certificate_type,
                                const unsigned char *certificate,
                                size_t certificate_len,
                                SshGafpCompletionCallback callback,
                                void *context);

SshOperationHandle
ssh_gafp_delete_key_certificate_2(SshGafp agent, 
                                  const char *public_key_encoding,
                                  const unsigned char *public_key_data,
                                  size_t public_key_data_len,
                                  const char *certificate_type,
                                  const unsigned char *certificate,
                                  size_t certificate_len,
                                  SshGafpCompletionCallback callback,
                                  void *context);

/* Deletes the given certificate without a private key from the agent.
   The callback can be NULL. */
SshOperationHandle
ssh_gafp_delete_extra_certificate(SshGafp agent, 
                                  const char *certificate_type,
                                  const unsigned char *certificate,
                                  size_t certificate_len,
                                  SshGafpCompletionCallback callback,
                                  void *context);

/* Returns the public keys for all private keys in possession of the agent. */
SshOperationHandle ssh_gafp_list_keys(SshGafp agent, 
                                      SshGafpListCallback callback,
                                      void *context);

/* Returns the certificates for the given public key */
SshOperationHandle 
ssh_gafp_list_key_certificates(SshGafp agent,
                               SshPublicKey public_key,
                               SshGafpListCallback callback,
                               void *context);

SshOperationHandle 
ssh_gafp_list_key_certificates_2(SshGafp agent,
                                 const char *public_key_encoding,
                                 const unsigned char *public_key_data,
                                 size_t public_key_data_len,
                                 SshGafpListCallback callback,
                                 void *context);

/* Returns the certificates for all private keys in possession of the agent. */
SshOperationHandle ssh_gafp_list_certificates(SshGafp agent,
                                              SshGafpListCallback callback,
                                              void *context);

/* Returns the certificates that do not have a private key stored 
   in the agent. */
SshOperationHandle
ssh_gafp_list_extra_certificates(SshGafp agent,
                                 SshGafpListCallback callback,
                                 void *context);

/* Send a ping message to agent.  Call callback when reply arrives. */
SshOperationHandle ssh_gafp_ping(SshGafp agent,
                                 SshGafpCompletionCallback callback,
                                 void *context);

/* Performs a private-key operation using the agent.
   Calls the given callback when a reply has been received.
   The caller can free any argument strings as soon as this
   has returned (i.e., no need to wait until the callback has been
   called). */
SshOperationHandle
ssh_gafp_key_operation(SshGafp agent,
                       const char *operation,
                       SshPublicKey public_key,
                       const unsigned char *data, 
                       size_t len,
                       SshGafpOpCallback callback, 
                       void *context);

SshOperationHandle
ssh_gafp_key_operation_2(SshGafp agent,
                         const char *operation,
                         const char *public_key_encoding,
                         const unsigned char *public_key_data,
                         size_t public_key_data_len,
                         const unsigned char *data, 
                         size_t len,
                         SshGafpOpCallback callback, 
                         void *context);


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
                                        void *context);

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
                                         void *context);

/* Performs a passphrase query. The agent will query the passphrase
   interactively from the user or use a stored passphrase. If
   always_ask_interactively is true, the passphrase storage is bypassed. */
SshOperationHandle
ssh_gafp_passphrase_query(SshGafp agent,
                          const char *passphrase_type,
                          const char *client_program,
                          const char *description,
                          Boolean always_ask_interactively,
                          SshGafpOpCallback callback,
                          void *context);

/* Request random data from the agent. */
SshOperationHandle ssh_gafp_random(SshGafp agent, 
                                   size_t random_len,
                                   SshGafpOpCallback callback, 
                                   void *context);

/* Send a forwarding notification to the agent. */
void ssh_gafp_forwarding_notice(SshGafp agent,
                                unsigned char *buf_out,
                                size_t buf_out_len,
                                const char *forwarding_command,
                                const char *host_name,
                                const char *ip_address,
                                const char *tcp_port);

/* Tell the agent to quit (die). */
SshOperationHandle ssh_gafp_quit(SshGafp agent, 
                                 SshGafpCompletionCallback callback, 
                                 void *context);

/* Decode the public key blob and return it as SshPublicKey */
SshCryptoStatus
ssh_gafp_decode_public_key_blob(const char *public_key_encoding,
                                const unsigned char *public_key_blob,
                                size_t public_key_blob_len,
                                SshPublicKey *key_return);

/* Decode the private key blob and return it as SshPrivateKey */
SshCryptoStatus
ssh_gafp_decode_private_key_blob(const char *private_key_encoding,
                                 const unsigned char *private_key_blob,
                                 size_t private_key_blob_len,
                                 const unsigned char *cipher_key,
                                 size_t cipher_key_len,
                                 SshPrivateKey *key_return);

/* Return readable error string according to the error code.
   Always returns a 0-terminated string.  Never fails. */
const char *ssh_gafp_error_string(SshGafpError error);

#ifdef __cplusplus
}
#endif

#endif /* _SSHGAFP_H_ */
/* eof (sshgafp.h) */
