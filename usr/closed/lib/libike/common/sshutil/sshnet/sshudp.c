/*

  Author: Timo J. Rinne <tri@ssh.fi>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
  All rights reserved.

  Generic code of the UDP communications interface.

  */

#include "sshincludes.h"
#include "sshdebug.h"
#include "sshudp.h"
#include "sshtcp.h"
#include "sshtimeouts.h"
#include "ssheloop.h"

/************************** Types and definitions ***************************/

#define SSH_DEBUG_MODULE "SshUdpGeneric"

/* UDP listener. */
struct SshUdpListenerRec
{
  /* Methods. */
  SshUdpMethods methods;

  /* Instance context. */
  void *context;
};

/****************** Platform dependent UDP implementation *******************/

/* Fetch the platform dependent UDP methods and constructor
   context. */
SshUdpMethods ssh_udp_platform_methods(void **constructor_context_return);


/***************************** Public functions *****************************/

SshUdpListener
ssh_udp_make_listener(const unsigned char *local_address,
                      const unsigned char *local_port,
                      const unsigned char *remote_address,
                      const unsigned char *remote_port,
                      SshUdpListenerParams params,
                      SshUdpCallback callback,
                      void *context)
{
  SshUdpListener listener;
  void *make_listener_context;

  listener = ssh_calloc(1, sizeof(*listener));
  if (listener == NULL)
    return NULL;

  if (params && params->udp_methods)
    {
      listener->methods = params->udp_methods;
      make_listener_context = params->make_listener_method_context;
    }
  else
    {
      listener->methods = ssh_udp_platform_methods(&make_listener_context);
    }

  listener->context
    = (*listener->methods->make_listener)(make_listener_context,
                                          listener,
                                          local_address,
                                          local_port,
                                          remote_address,
                                          remote_port,
                                          params,
                                          callback,
                                          context);
  if (listener->context == NULL)
    {
      ssh_free(listener);
      return NULL;
    }

  return listener;
}


void
ssh_udp_destroy_listener(SshUdpListener listener)
{
  (*listener->methods->destroy_listener)(listener->context);
  ssh_free(listener);
}


const unsigned char *
ssh_udp_error_string(SshUdpError error)
{
  switch (error)
    {
    case SSH_UDP_OK:
     return ssh_custr("OK");
    case SSH_UDP_HOST_UNREACHABLE:
     return ssh_custr("Destination Host Unreachable");
    case SSH_UDP_PORT_UNREACHABLE:
     return ssh_custr("Destination Port Unreachable");
    case SSH_UDP_NO_DATA:
     return ssh_custr("No Data");
    default:
     return ssh_custr("Unknown Error");
    }
  /* NOTREACHED */
}


SshUdpError
ssh_udp_read(SshUdpListener listener,
#ifdef SUNWIPSEC
	     SshUdpPacketContext *packet_context,
#endif /* SUNWIPSEC */
             unsigned char *remote_address, size_t remote_address_len,
             unsigned char *remote_port, size_t remote_port_len,
             unsigned char *datagram_buffer,
             size_t datagram_buffer_len,
             size_t *datagram_len_return)
{
  return (*listener->methods->read)(listener->context,
#ifdef SUNWIPSEC
				    packet_context,
#endif /* SUNWIPSEC */
                                    remote_address, remote_address_len,
                                    remote_port, remote_port_len,
                                    datagram_buffer,
                                    datagram_buffer_len,
                                    datagram_len_return);
}


void
ssh_udp_send(SshUdpListener listener,
#ifdef SUNWIPSEC
	     SshUdpPacketContext packet_context,
#endif /* SUNWIPSEC */
             const unsigned char *remote_address,
             const unsigned char *remote_port,
             const unsigned char *datagram_buffer, size_t datagram_len)
{
  (*listener->methods->send)(listener->context,
#ifdef SUNWIPSEC
			     packet_context,
#endif /* SUNWIPSEC */
                             remote_address, remote_port,
                             datagram_buffer, datagram_len);
}


SshUdpError
ssh_udp_multicast_add_membership(SshUdpListener listener,
                                 const unsigned char *group_to_join,
                                 const unsigned char *interface_to_join)
{
  return (*listener->methods->multicast_add_membership)(listener->context,
                                                        group_to_join,
                                                        interface_to_join);
}

SshUdpError
ssh_udp_multicast_drop_membership(SshUdpListener listener,
                                  const unsigned char *group_to_drop,
                                  const unsigned char *interface_to_drop)
{
  return (*listener->methods->multicast_drop_membership)(listener->context,
                                                         group_to_drop,
                                                         interface_to_drop);
}


#ifdef SUNWIPSEC
SshUdpPacketContext
ssh_udp_create_packet_context(SshUdpListener listener, void *ucred)
{
  return (*listener->methods->create_packet_context)(listener->context,
						     ucred);
}
#endif /* SUNWIPSEC */

void *
ssh_udp_return_platform(SshUdpListener listener)
{
  return (listener->context);
}
