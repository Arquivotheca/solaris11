/*

  Author: Tomi Salo <ttsalo@ssh.fi>
          Tatu Ylonen <ylo@ssh.fi>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
  All rights reserved.

  Portable interface for UDP communications.  (The implementation is
  machine-dependent, but provides this interface on all platforms.)

*/

#ifndef SSHUDP_H
#define	SSHUDP_H

/* Data type for an UDP listener. */
typedef struct SshUdpListenerRec *SshUdpListener;

/* A forward declaration for UDP listener parameters. */
typedef struct SshUdpListenerParamsRec *SshUdpListenerParams;

#ifdef SUNWIPSEC
/* A forward declaration for UDP packet parameters */
typedef struct SshUdpPacketContextRec *SshUdpPacketContext;
#endif /* SUNWIPSEC */

/* Callback function to be called when a packet or notification is
   available from the udp listener.  ssh_udp_read should be called
   from the callback. */
typedef void (*SshUdpCallback)(SshUdpListener listener, void *context);

/* Error codes for UDP operations. */
typedef enum
{
  /* A packet was successfully read from the listener. */
  SSH_UDP_OK,

  /* A host or network unreachable notification was received. */
  SSH_UDP_HOST_UNREACHABLE,

  /* A port unreachable notification was received. */
  SSH_UDP_PORT_UNREACHABLE,

  /* No packet or notification is available from the listener at this time. */
  SSH_UDP_NO_DATA,

  /* Invalid arguments */
  SSH_UDP_INVALID_ARGUMENTS
} SshUdpError;

/* Methods for hooking UDP listeners and for different UDP
   implementations. */
struct SshUdpMethodsRec
{
  /* Create a new listener.  The argument
     `make_listener_method_context' is the context data that was
     specified for the method functions with the
     `make_listener_method_context' field of the
     SshUdpListenerParamsStruct.  The function must return a new
     listener object or NULL if the listener creation fails.  The
     returned listener object is passed as the `listener_context' for
     all other methods. */
  void *(*make_listener)(void *make_listener_method_context,
                         SshUdpListener listener,
                         const unsigned char *local_address,
                         const unsigned char *local_port,
                         const unsigned char *remote_address,
                         const unsigned char *remote_port,
                         SshUdpListenerParams params,
                         SshUdpCallback callback,
                         void *callback_context);

  /* Destroy the listener object `listener_context'. */
  void (*destroy_listener)(void *listener_context);

  /* Implements the read operation.  The argument `listener_context'
     identifies the UDP listener object from which the data is
     read. */
  SshUdpError (*read)(void *lister_context,
#ifdef SUNWIPSEC
		      SshUdpPacketContext *packet_context,
#endif /* SUNWIPSEC */
                      unsigned char *remote_address, size_t remote_address_len,
                      unsigned char *remote_port, size_t remote_port_len,
                      unsigned char *datagram_buffer,
                      size_t datagram_buffer_len,
                      size_t *datagram_len_return);

  /* Implements the send operation.  The argument `lister_context'
     identifies the UDP listener that is sending data. */
  void (*send)(void *listener_context,
#ifdef SUNWIPSEC
	       SshUdpPacketContext packet_context,
#endif /* SUNWIPSEC */
               const unsigned char *remote_address,
               const unsigned char *remote_port,
               const unsigned char *datagram_buffer, size_t datagram_len);

  /* Implements the multicast group join operation.  The argument
     `listener_context' identifies the UDP listener that is joining to
     the multicast group. */
  SshUdpError (*multicast_add_membership)(void *listener_context,
                                          const unsigned char *group_to_join,
                                          const unsigned char *
                                            interface_to_join);

  /* Implements the multicast group leave operation.  The argument
     `listener_context' identifies the UDP listener that is leaving
     from the multicast group. */
  SshUdpError (*multicast_drop_membership)(void *listener_context,
                                           const unsigned char *group_to_drop,
                                           const unsigned char *
					   interface_to_drop);

#ifdef SUNWIPSEC
  /* Create a packet context */
  SshUdpPacketContext (*create_packet_context)(void *listener_context,
					     void *cred);
#endif /* SUNWIPSEC */
};

typedef struct SshUdpMethodsRec SshUdpMethodsStruct;
typedef struct SshUdpMethodsRec *SshUdpMethods;

/* Parameters for the ssh_udp_make_listener function.  If any of the
   fields has the value NULL or 0, the default value will be used
   instead. */
struct SshUdpListenerParamsRec
{
  /* The listener has permission to send broadcast packets. */
  Boolean broadcasting;

  /* Multicast hop count limit (ttl). This affects when sending
     multicast traffic from the socket. You don't need to be part of
     the multicast group to send packets to it. */
  SshUInt32 multicast_hops;

  /* Enable/disable multicast looping in the local host. If this is
     enabled then multicast sent from this socket is looped back
     inside the machine to all sockets that are member of the
     multicast group. Normally this is enabled, which means that all
     processes (including you) in this host that are part of the group
     can also hear your tranmissions. If you are sure that you are
     only member in this host you can disable this saving you time to
     process your own multicast packets. */
  Boolean multicast_loopback;

  /* Optional methods for the UDP implementation.  If these are set,
     they are used for all UDP operations with the created listener.
     If the methods are unset, the platform specific UDP
     implementation will be used instead. */
  SshUdpMethods udp_methods;

  /* Context data for the `make_listener' method, defined in
     `udp_methods'. */
  void *make_listener_method_context;
};

typedef struct SshUdpListenerParamsRec SshUdpListenerParamsStruct;

/* Creates a listener for sending and receiving UDP packets.  The listener is
   connected if remote_address is non-NULL.  Connected listeners may receive
   notifications about the destination host/port being unreachable.
     local_address    local address for sending; SSH_IPADDR_ANY chooses
                      automatically
     local_port       local port for receiving udp packets (NULL lets system
                      pick one)
     remote_address   specifies the remote address for this listener
                      is non-NULL.  If specified, unreachable notifications
                      may be received for packets sent to the address.
     remote_port      remote port for packets sent using this listener, or NULL
     params           additional paameters for the listener.  This can be
                      NULL in which case the default parameters are used.
     callback         function to call when packet or notification available
     context          argument to pass to the callback.
  This returns the listener, or NULL if the listener could not be created
  (e.g., due to a resource shortage or unparsable address). */
SshUdpListener ssh_udp_make_listener(const unsigned char *local_address,
                                     const unsigned char *local_port,
                                     const unsigned char *remote_address,
                                     const unsigned char *remote_port,
                                     SshUdpListenerParams params,
                                     SshUdpCallback callback,
                                     void *context);

/* Destroys the udp listener. */
void ssh_udp_destroy_listener(SshUdpListener listener);

/* Convert UDP error to string */
const unsigned char *ssh_udp_error_string(SshUdpError error);

/* Reads the received packet or notification from the listener.  This
   function should be called from the listener callback.  This can be
   called multiple times from a callback; each call will read one more
   packet or notification from the listener until no more are
   available. */
SshUdpError ssh_udp_read(SshUdpListener listener,
#ifdef SUNWIPSEC
			 SshUdpPacketContext *packet_context,
#endif /* SUNWIPSEC */
                         unsigned char *remote_address,
                         size_t remote_address_len,
                         unsigned char *remote_port, size_t remote_port_len,
                         unsigned char *datagram_buffer,
                         size_t datagram_buffer_len,
                         size_t *datagram_len_return);

/* This sends udp datagram to remote destination. This call always success, or
   the if not then datagram is silently dropped (udp is not reliable anyways */
void ssh_udp_send(SshUdpListener listener,
#ifdef SUNWIPSEC
		  SshUdpPacketContext packet_context,
#endif /* SUNWIPSEC */
                  const unsigned char *remote_address,
                  const unsigned char *remote_port,
                  const unsigned char *datagram_buffer, size_t datagram_len);

/* Add membership to given multicast group. The group to join is a ip address
   of the multicast group you want to join. The interface to join can be ip
   address of the interface if you want to join to that group only in one
   interface or SSH_IPADDR_ANY if you want to listen all interfaces. If the
   group_to_join is an ipv4 address then this function joins to the ipv4
   multicast group. If it is ipv6 address then we join to the ipv6 address (in
   which case the listener must be one listening ipv6 address or
   SSH_IPADDR_ANY. You don't need to be part of the multicast group to send
   packets to it. */
SshUdpError
ssh_udp_multicast_add_membership(SshUdpListener listener,
                                 const unsigned char *group_to_join,
                                 const unsigned char *interface_to_join);

/* Drop membership to given multicast group. The group to drop is a ip address
   of the multicast group you want to drop. The interface to drop can be ip
   address of the interface if you want to drop to that group only in one
   interface or SSH_IPADDR_ANY if you want to drop listening in all
   interfaces. Normally interface_to_drop is same value that was used in the
   ssh_udp_multicast_add_membership function.  */
SshUdpError
ssh_udp_multicast_drop_membership(SshUdpListener listener,
                                  const unsigned char *group_to_drop,
                                  const unsigned char *interface_to_drop);

#ifdef SUNWIPSEC
void
ssh_udp_free_context(SshUdpPacketContext packet_context);

SshUdpPacketContext
ssh_udp_dup_context(SshUdpPacketContext packet_context);

Boolean ssh_push_natt_mod(SshUdpListener listener);
Boolean ssh_set_mac_bypass(SshUdpListener listener, Boolean enable);
#endif /* SUNWIPSEC */

#endif /* SSHUDP_H */
