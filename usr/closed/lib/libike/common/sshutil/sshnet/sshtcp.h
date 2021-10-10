/*

Author: Tatu Ylonen <ylo@ssh.fi>
        Antti Huima <huima@ssh.fi>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
All rights reserved.

Interface to TCP sockets.

*/

#ifndef SSHTCP_H
#define SSHTCP_H

#include "sshinet.h"
#include "sshstream.h"
#include "sshoperation.h"

typedef enum {
  /* The connection or lookup was successful. */
  SSH_TCP_OK = 0,

  /* A new connection has been received.  This result code is only
     given to listeners. */
  SSH_TCP_NEW_CONNECTION,

  /* No address could be found the host. */
  SSH_TCP_NO_ADDRESS,

  /* The address has no name. */
  SSH_TCP_NO_NAME,

  /* The destination is unreachable; this could indicate a routing
     problem, the host being off, or something similar. */
  SSH_TCP_UNREACHABLE,

  /* The destination refused the connection (i.e., is not listening on
     the specified port). */
  SSH_TCP_REFUSED,

  /* A timeout occurred.  This could indicate a network problem. */
  SSH_TCP_TIMEOUT,

  /* An operation has failed.  This is a catch-all error used when
     none of the other codes is appropriate. */
  SSH_TCP_FAILURE
} SshTcpError;

/* Enum to define different ways to make the port and address
   reusable. */
typedef enum {
  SSH_TCP_REUSABLE_ADDRESS = 0, /* Address is reusable if the port is
                                   different. (default) */
  SSH_TCP_REUSABLE_PORT,        /* Port is reusable if the address is
                                   different. */
  SSH_TCP_REUSABLE_BOTH,        /* Both port and address are reusable. */
  SSH_TCP_REUSABLE_NONE         /* Port and address are not reusable. */
} SshTcpReusableType;

typedef enum {
  SSH_TCP_SOCKS4 = 0, /* Use SOCKS4... */
  SSH_TCP_SOCKS5      /* ... or SOCKS5. */
} SshTcpSocksType;

/* Convert TCP error to string */
const unsigned char *ssh_tcp_error_string(SshTcpError error);

/* Callback function for socket creation.  The given function is
   called when a connection is ready. */
typedef void (*SshTcpCallback)(SshTcpError error,
                               SshStream stream,
                               void *context);

/* Parameters to the ssh_tcp_connect function. To get default values
   just memset the structure before giving it the the ssh_tcp_connect
   function. */
typedef struct SshTcpConnectParamsRec {
  /* Socks server url for going out through firewalls. URL specifies
     the SOCKS host, port, username, and socks network exceptions. If
     this is NULL or empty, the connection will be made without
     SOCKS. If port is not given in the url, the default SOCKS port
     (1080) will be used.  */
  unsigned char *socks_server_url;

  SshTcpSocksType socks_type;
  
 /* Number of connection attempts before giving up. (Some systems
   appear to spuriously fail connections wihtout apparent reason, and
   retrying usually succeeds in those cases).  If this is zero then
   the default value of 1 is used. */
  SshUInt32 connection_attempts;

 /* Total timeout in seconds for the whole connection attempt. If the
    connection is not established before this timeout expires then the
    connect operation fails. If this is zero then we use the timeouts
    defined by the operating system. */
  SshUInt32 connection_timeout;

  /* Use given protocol(s) to make connection.  Default value is zero,
     which means that any protocol can be used.  To limit protocols,
     SSH_IP_TYPE_MASK_IP4 or SSH_IP_TYPE_MASK_IP6 or bitwise or
     arbitrary `bitwise or' of the SSH_IP_TYPE_MASK_* can be
     specified. */
  SshUInt32 protocol_mask;

  /* The local address and port to use in the connect operation.  If
     these have the value NULL, the socket is not bind to local
     address; the system will select an unpriviledged port for the
     local socket. */
  const unsigned char *local_address;
  const unsigned char *local_port_or_service;

  /* How the local address is reusable. */
  SshTcpReusableType local_reusable;
} *SshTcpConnectParams, SshTcpConnectParamsStruct;

/* Opens a connection to the specified host, and calls the callback
   when the connection has been established or has failed.  If
   connecting is successful, the callback will be called with error
   set to SSH_TCP_OK and an SshStream object for the connection passed
   in in the stream argument.  Otherwise, error will indicate the
   reason for the connection failing, and the stream will be NULL.

   Note that the callback may be called either during this call or
   some time later.

   Returns SshOperationHandle that can be used to abort the tcp open.

   The `host_name_or_address' argument may be a numeric IP address or
   a host name (domain name), in which case it is looked up from the
   name servers.

   The params structure can either be NULL or memset to zero to get
   default parameters. All data inside the params is copied during
   this call, so it can be freed immediately when this function
   returns. */
SshOperationHandle ssh_tcp_connect(const unsigned char *host_name_or_address,
                                   const unsigned char *port_or_service,
                                   const SshTcpConnectParams params,
                                   SshTcpCallback callback,
                                   void *context);

/****************** Function for listening for connections ******************/

typedef struct SshTcpListenerRec *SshTcpListener;

/* Parameters to the ssh_tcp_make_listener function. To get default
   values just memset the structure before giving it the the
   ssh_tcp_make_listener function. */
typedef struct SshTcpListenerParamsRec {
  SshTcpReusableType listener_reusable; /* How is it reusable. */
  int listen_backlog; /* Listen backlog size for the listener socket. */
  size_t send_buffer_size;      /* Send buffer size in bytes. */
  size_t receive_buffer_size;   /* Receive buffer size in bytes. */
} *SshTcpListenerParams, SshTcpListenerParamsStruct;

/* Creates a socket that listens for new connections.  The address
   must be an ip-address in the form "nnn.nnn.nnn.nnn".
   SSH_IPADDR_ANY indicates any host; otherwise it should be the
   address of some interface on the system.  The given callback will
   be called whenever a new connection is received at the socket.
   This returns NULL on error.  If the params is NULL or if it is
   memset to zero then default values for each parameter is used. */
SshTcpListener ssh_tcp_make_listener(const unsigned char *local_address,
                                     const unsigned char *port_or_service,
                                     const SshTcpListenerParams params,
                                     SshTcpCallback callback,
                                     void *context);

Boolean ssh_tcp_listener_get_local_port(SshTcpListener listener, 
                                        unsigned char *buf, 
                                        size_t buflen);

/* Destroys the socket.  It is safe to call this from a callback.  If
   the listener was local, and a socket was created in the file
   system, this does not automatically remove the socket (so that it
   is possible to close the other copy after a fork).  The application
   should call remove() for the socket path when no longer needed. */
void ssh_tcp_destroy_listener(SshTcpListener listener);

/* Returns true (non-zero) if the socket behind the stream has IP
   options set.  This returns FALSE if the stream is not a socket
   stream. */
Boolean ssh_tcp_has_ip_options(SshStream stream);

/* Returns the ip-address of the remote host, as string.  This returns
   FALSE if the stream is not a socket stream or buffer space is
   insufficient. */
Boolean ssh_tcp_get_remote_address(SshStream stream, unsigned char *buf,
                                   size_t buflen);

/* Returns the remote port number, as a string.  This returns FALSE if
   the stream is not a socket stream or buffer space is
   insufficient. */
Boolean ssh_tcp_get_remote_port(SshStream stream, unsigned char *buf,
                                size_t buflen);

/* Returns the ip-address of the local host, as string.  This returns
   FALSE if the stream is not a socket stream or buffer space is
   insufficient. */
Boolean ssh_tcp_get_local_address(SshStream stream, unsigned char *buf,
                                  size_t buflen);

/* Returns the local port number, as a string.  This returns FALSE if
   the stream is not a socket stream or buffer space is
   insufficient. */
Boolean ssh_tcp_get_local_port(SshStream stream, unsigned char *buf,
                               size_t buflen);

/*********************** Functions for socket options ***********************/

/* Sets/resets TCP options TCP_NODELAY for the socket.  This returns
   TRUE on success. */
Boolean ssh_tcp_set_nodelay(SshStream stream, Boolean on);

/* Sets/resets socket options SO_KEEPALIVE for the socket.  This
   returns TRUE on success. */
Boolean ssh_tcp_set_keepalive(SshStream stream, Boolean on);

/* Sets/resets socket options SO_LINGER for the socket.  This returns
   TRUE on success. */
Boolean ssh_tcp_set_linger(SshStream stream, Boolean on);

#endif /* SSHTCP_H */
