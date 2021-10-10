/*

  Author: Tomi Salo <ttsalo@ssh.fi>
          Tatu Ylonen <ylo@ssh.fi>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
  All rights reserved.

  Unix implementation of the UDP communications interface.

  */

#include "sshincludes.h"
#include "sshdebug.h"
#include "sshudp.h"
#include "sshtcp.h"
#include "sshtimeouts.h"
#include "ssheloop.h"
#include "sshinet.h"

#define SSH_DEBUG_MODULE "SshUdp"

#include <sys/socket.h>
#include <netinet/in.h>
#ifdef HAVE_NETINET_IN_SYSTM_H
#include <netinet/in_systm.h>
#else /* Some old linux systems at least have in_system.h instead. */
#include <netinet/in_system.h>
#endif /* HAVE_NETINET_IN_SYSTM_H */
#if !defined(__PARAGON__)
#include <netinet/ip.h>
#endif /* !__PARAGON__ */
#include <arpa/inet.h>

#ifdef SUNWIPSEC
#include <ucred.h>
#include <ipsec_util.h>
#endif /* SUNWIPSEC */

#if defined(HAVE_SOCKADDR_IN6_STRUCT) && defined(WITH_IPV6)
/* Currently, we include the IPv6 code only if we have the
   `sockaddr_in6' structure. */
#define SSH_HAVE_IPV6
#ifdef IPV6_JOIN_GROUP
#define SSH_HAVE_IPV6_MULTICAST
#endif /* IPV6_JOIN_GROUP */
#endif /* HAVE_SOCKADDR_IN6_STRUCT && WITH_IPV6 */

#ifdef SSH_HAVE_IPV6
/* An IPv6 link-local address scope ID. */
struct SshScopeIdRec
{
  union
  {
    SshUInt32 ui32;
  } u;
};

typedef struct SshScopeIdRec SshScopeIdStruct;
typedef struct SshScopeIdRec *SshScopeId;
#endif /* SSH_HAVE_IPV6 */

/* Internal representation of Listener structure, not exported */
struct SshUdpPlatformListenerRec
{
  /* Pointer to the generic listener object. */
  SshUdpListener listener;

  int sock;
  Boolean ipv6;
  struct SshUdpPlatformListenerRec *sibling;
  SshUdpCallback callback;
  void *context;
  Boolean connected;
#ifdef SSH_HAVE_IPV6
  Boolean scope_id_cached;
  SshScopeIdStruct cached_scope_id;
#endif /* SSH_HAVE_IPV6 */
};

typedef struct SshUdpPlatformListenerRec SshUdpPlatformListenerStruct;
typedef struct SshUdpPlatformListenerRec *SshUdpPlatformListener;

#ifdef SUNWIPSEC
struct SshUdpPacketContextRec
{
  ucred_t *ucred;
};
#endif /* SUNWIPSEC */

static void
ssh_udp_io_cb(unsigned int events, void *context)
{
  SshUdpPlatformListener listener = (SshUdpPlatformListener)context;

  if (events & SSH_IO_READ)
    {
      /* Call the callback to inform about a received packet or
         notification. */
      if (listener->callback)
        (*listener->callback)(listener->listener, listener->context);
    }
}

#ifdef SUNWIPSEC
SshUdpPacketContext
ssh_udp_platform_create_context(void *listener_context, void *cred)
{
  ucred_t *ucred = (ucred_t *)cred;
  SshUdpPacketContext ret;
  int size = ucred_size();

  ret = ssh_malloc(sizeof (*ret));
  if (ret == NULL)
    return (NULL);

  memset(ret, 0, sizeof (*ret));

  ret->ucred = ssh_memdup(ucred, size);
  if (ret->ucred == NULL)
    {
      ssh_free(ret);
      return (NULL);
    }
  return (ret);
}

SshUdpPacketContext
ssh_udp_dup_context(SshUdpPacketContext packet_context)
{
  SshUdpPacketContext ret;

  if (packet_context == NULL)
    return (NULL);

  ret = ssh_malloc(sizeof (*ret));
  memset(ret, 0, sizeof (*ret));

  if (packet_context->ucred != NULL)
    {
      ret->ucred = ssh_memdup(packet_context->ucred, ucred_size());
      if (ret->ucred == NULL)
	{
	  ssh_free(ret);
	  return (NULL);
	}
    }

  return (ret);
}

void
ssh_udp_free_context(SshUdpPacketContext packet_context)
{
  if (packet_context == NULL)
    return;
  if (packet_context->ucred != NULL)
    {
      ucred_free(packet_context->ucred);
      packet_context->ucred = NULL;
    }
  ssh_free(packet_context);
}
#endif /* SUNWIPSEC */

/* Set the common (both IPv4 and IPv6) socket options for the UDP
   listener `listener'. */

static void
ssh_udp_set_common_socket_options(SshUdpPlatformListener listener,
                                  SshUdpListenerParams params)
{
#ifdef SO_REUSEADDR
  {
    int value;

    value = 1;
    if (setsockopt(listener->sock, SOL_SOCKET, SO_REUSEADDR, (void *) &value,
                   sizeof(value)) == -1)
      {
        SSH_DEBUG(SSH_D_FAIL,
                  ("ssh_udp_set_common_socket_options: setsockopt " \
                   "SO_REUSEADDR failed: %s", strerror(errno)));
      }
  }
#endif /* SO_REUSEADDR */
#ifdef SO_REUSEPORT
  {
    int value;

    value = 1;
    if (setsockopt(listener->sock, SOL_SOCKET, SO_REUSEPORT, (void *) &value,
                   sizeof(value)) == -1)
      {
        SSH_DEBUG(SSH_D_FAIL,
                  ("ssh_udp_set_common_socket_options: setsockopt " \
                   "SO_REUSEPORT failed: %s", strerror(errno)));
      }
  }
#endif /* SO_REUSEPORT */

#ifdef SUNWIPSEC
/*
 * These three socket options are all {,Open}Solaris-specific.
 * Nest 'em in SUNWIPSEC even though it's redundant.
 */
#ifdef SO_ALLZONES
  {
    int value;

    value = 1;
    if (setsockopt(listener->sock, SOL_SOCKET, SO_ALLZONES, (void *) &value,
		   sizeof (value)) == -1)
      {
        SSH_DEBUG(2,
		  ("ssh_udp_make_listener: setsockopt SO_ALLZONES failed: %s",
		   strerror(errno)));
      }
  }
#endif
#ifdef IP_SEC_OPT
  {
    ipsec_req_t r;
    int protocol = (listener->ipv6 ? IPPROTO_IPV6 : IPPROTO_IP);

    memset(&r, 0, sizeof(r));
    r.ipsr_ah_req = IPSEC_PREF_NEVER;
    r.ipsr_esp_req = IPSEC_PREF_NEVER;
    r.ipsr_self_encap_req = IPSEC_PREF_NEVER;

    /* Here, we use the fact that IP_SEC_OPT == IPV6_SEC_OPT */
    if (setsockopt(listener->sock, protocol, IP_SEC_OPT, &r, sizeof(r)) == -1)
      {
        SSH_DEBUG(2,
	    ("ssh_udp_make_listener: setsockopt IP_SEC_OPT failed: %s",
		strerror(errno)));
      }
  }
#endif
#ifdef SO_RECVUCRED
  {
    int value;

    value = 1;

    if (setsockopt(listener->sock, SOL_SOCKET, SO_RECVUCRED, (void *) &value,
		   sizeof (value)) == -1)
      {
        SSH_DEBUG(2,
		  ("ssh_udp_make_listener: setsockopt SO_RECVUCRED failed: %s",
		   strerror(errno)));
      }
  }
#endif
#endif /* SUNWIPSEC */
}

/* Set more common (both IPv4 and IPv6) socket options for the UDP
   listener `listener'.  These are set after the socket is bound to IP
   addresses. */

static void
ssh_udp_set_more_common_socket_options(SshUdpPlatformListener listener,
                                       SshUdpListenerParams params)
{
#ifdef SO_SNDBUF
  {
    int buf_len;

    buf_len = 65535;
    if (setsockopt(listener->sock, SOL_SOCKET, SO_SNDBUF, &buf_len,
                   sizeof(int)) == -1)
      {
        SSH_DEBUG(2, ("ssh_udp_set_more_common_socket_options: " \
                      "setsockopt SO_SNDBUF failed: %s", strerror(errno)));
      }
  }
#endif /* SO_SNDBUF */

#ifdef SO_RCVBUF
  {
    int buf_len;

    buf_len = 65535;
    if (setsockopt(listener->sock, SOL_SOCKET, SO_RCVBUF, &buf_len,
                   sizeof(int)) == -1)
      {
        SSH_DEBUG(2, ("ssh_udp_set_more_common_socket_options: " \
                      "setsockopt SO_RCVBUF failed: %s", strerror(errno)));
      }
  }
#endif /* SO_RCVBUF */
#ifdef SO_BROADCAST
  if (params && params->broadcasting)
    {
      int option = 1;

      if (setsockopt(listener->sock, SOL_SOCKET, SO_BROADCAST, &option,
                     sizeof(int)) == -1)
        SSH_DEBUG(SSH_D_FAIL,
                  ("setsockopt SO_BROADCAST failed: %s", strerror(errno)));
    }
#endif /* SO_BROADCAST */
  if (params && params->multicast_hops)
    /* XXX */
    ssh_fatal("SshUdpListenerParamsStruct.multicast_hops "
              "not implemented yet XXX!");
  if (params && params->multicast_loopback)
    /* XXX */
    ssh_fatal("SshUdpListenerParamsStruct.multicast_loopback "
              "not implemented yet XXX!");
}

#ifdef SSH_HAVE_IPV6

#ifdef HAVE_SOCKADDR_IN6_SCOPE_ID
#if defined(__NetBSD__) || defined(__FreeBSD__) || defined(sun)

#include <net/if.h>

/* Resolve the scope ID into `id' from the `ip' and `scope_id'. */
static Boolean
ssh_udp_resolve_scope_id(SshUdpPlatformListener listener, SshScopeId id,
                         SshIpAddr ip, const unsigned char *scope_id)
{
  id->u.ui32 = if_nametoindex(ssh_csstr(scope_id));
  if (id->u.ui32 == 0)
    return FALSE;

  return TRUE;
}

/* Set the scope ID `id' into the socket address `sinaddr'. */
static void
ssh_udp_set_sockaddr_scope_id(SshUdpPlatformListener listener,
                              struct sockaddr_in6 *sinaddr,
                              SshScopeId id)
{
  sinaddr->sin6_scope_id = id->u.ui32;
}

#else /* not __NetBSD__ and not __FreeBSD__ */
#ifdef __linux__

#include <linux/types.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#ifdef NETLINK_ROUTE
struct SshLinuxNetlinkLinkRequestRec
{
  struct nlmsghdr nh;    /* Netlink message header */
  struct ifinfomsg info; /* GETLINK payload */
  char buf[128];         /* Some pad for netlink alignment macros */
};
#endif /* NETLINK_ROUTE */

static Boolean
ssh_udp_resolve_scope_id(SshUdpPlatformListener listener, SshScopeId id,
                         SshIpAddr ip, const unsigned char *scope_id)
{
#ifdef NETLINK_ROUTE
  int sd;
  struct SshLinuxNetlinkLinkRequestRec req;
  struct sockaddr_nl nladdr;
  unsigned char response_buf[4096];
  struct nlmsghdr *nh;
  struct iovec iov;
  struct msghdr msg;
  struct ifinfomsg *ifi_res;
  struct nlmsgerr *errmsg;
  struct rtattr *rta;
  int res, offset, offset2, addr_len;
  char *addr_buf;

  SSH_ASSERT(scope_id != NULL);

  /* Open a netlink/route socket. This should not require root
     permissions or special capabilities. */
  sd = socket(PF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
  if (sd < 0)
    {
      SSH_DEBUG(SSH_D_FAIL,
                ("failed to open PF_NETLINK/NETLINK_ROUTE socket"));
      goto fail;
    }

  /* Build a request for all interfaces */
  memset(&req, 0, sizeof(req));
  req.nh.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
  req.nh.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK | NLM_F_DUMP;
  req.nh.nlmsg_type = RTM_GETLINK;
  req.nh.nlmsg_seq = 0;
  req.nh.nlmsg_pid = 0; /* Message is directed to kernel */

  req.info.ifi_family = AF_UNSPEC;

  memset(&nladdr, 0, sizeof(nladdr));
  nladdr.nl_family = AF_NETLINK;
  nladdr.nl_pid = 0; /* pid = 0 is kernel in nladdr sock */

  /* Send the request. This request should not require
     root permissions or any special capabilities. */
  if (sendto(sd, &req, req.nh.nlmsg_len, 0,
             (struct sockaddr*)&nladdr, sizeof(nladdr)) < 0)
    {
      SSH_DEBUG(SSH_D_FAIL, ("sendto() of GETLINK request failed."));
      goto fail;
    }

  /* Parse replies from kernel */
  nh = NULL;
  do {
    /* Read a response from the kernel, for some very odd reason
       recvmsg() seemed to work better in this instance during
       testing.. */
    msg.msg_name = (struct sockaddr*)&nladdr;
    msg.msg_namelen = sizeof(nladdr);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = NULL;
    msg.msg_controllen = 0;
    msg.msg_flags = 0;

    iov.iov_base = response_buf;
    iov.iov_len = sizeof(response_buf);

    res = recvmsg(sd, &msg, 0);
    if (res <= 0)
      {
        SSH_DEBUG(SSH_D_FAIL, ("recvmsg() failed"));
        goto fail;
      }

    /* This response contains several netlink messages
       concatenated. */
    for (offset = 0; offset < res; offset += nh->nlmsg_len)
      {
        nh = (struct nlmsghdr *)((unsigned char*)response_buf + offset);

        if (nh->nlmsg_len == 0)
          {
            SSH_DEBUG(SSH_D_ERROR,
                      ("Received netlink message of length 0.."));
            goto fail;
          }

        if (nh->nlmsg_type == NLMSG_ERROR)
          {
            errmsg = NLMSG_DATA(nh);
            SSH_DEBUG(SSH_D_ERROR,
                      ("PF_NETLINK/NETLINK_ROUTE GETLINK request returned "
                       "error %d", errmsg->error));
            goto fail;
          }

        if (nh->nlmsg_type == RTM_GETLINK || nh->nlmsg_type == RTM_NEWLINK
            || nh->nlmsg_type == RTM_DELLINK)
          {
            ifi_res = (struct ifinfomsg *)NLMSG_DATA(nh);
            rta = NULL;
            for (offset2 = NLMSG_ALIGN(sizeof(struct ifinfomsg));
                 offset2 < nh->nlmsg_len;
                 offset2 += RTA_ALIGN(rta->rta_len))
              {
                rta = (struct rtattr *)(((unsigned char*)ifi_res) + offset2);

                if (RTA_ALIGN(rta->rta_len) == 0)
                  break;

                switch(rta->rta_type)
                  {
                  case IFLA_IFNAME:
                    addr_buf = ((char *)RTA_DATA(rta));
                    addr_len = RTA_PAYLOAD(rta);

                    if (strncmp(scope_id, addr_buf, addr_len) != 0)
                      break;

                    id->u.ui32 = ifi_res->ifi_index;
                    goto ok;

                    break;
                  default:
                    break;
                  }
              }
          }
        else
          {
            /* It does not matter if this is a message of type
               NLMSG_DONE or some other type, in either case
               if we have not jumped to "ok:", we have not found
               the correct interface. */
            goto fail;
          }
      }
  } while((nh != NULL) && (nh->nlmsg_flags & NLM_F_MULTI) != 0);

  SSH_DEBUG(SSH_D_FAIL,
            ("could not find interface for '%s'", scope_id));
 fail:
  if (sd >= 0) close(sd);
#endif
  return FALSE;

#ifdef NETLINK_ROUTE
 ok:
  close(sd);
  return TRUE;
#endif /* NETLINK_ROUTE */
}

static void
ssh_udp_set_sockaddr_scope_id(SshUdpPlatformListener listener,
                              struct sockaddr_in6 *sinaddr,
                              SshScopeId id)
{
  sinaddr->sin6_scope_id = id->u.ui32;
}

#else /* not __linux__ */

static Boolean
ssh_udp_resolve_scope_id(SshUdpPlatformListener listener, SshScopeId id,
                         SshIpAddr ip, const unsigned char *scope_id)
{
  SSH_DEBUG(SSH_D_ERROR, ("Don't know how to resolve IPv6 link-local "
                          "address scope ID on this platform"));
  return FALSE;
}

static void
ssh_udp_set_sockaddr_scope_id(SshUdpPlatformListener listener,
                              struct sockaddr_in6 *sinaddr,
                              SshScopeId id)
{
  SSH_NOTREACHED;
}
#endif /* not __linux__ **/
#endif /* not __NetBSD__ and not __FreeBSD__ */
#endif /* HAVE_SOCKADDR_IN6_SCOPE_ID */

/* Set the IPv6 link-local address scope ID to the sockaddr `sinaddr'.
   The argument `ip' specifies the link-local address and `scope_id'
   specifies the interface name that was parsed from the address
   string of the IP address `ip'.  The argument `cache_in_listener'
   specifies whether the scope ID information can be cached in the
   listener `listener'.  If the argument `scope_id' has the value
   NULL, the function may use a cached scope ID from the listener
   object. */

static Boolean
ssh_udp_set_scope_id(SshUdpPlatformListener listener,
                     struct sockaddr_in6 *sinaddr,
                     SshIpAddr ip, const unsigned char *scope_id,
                     Boolean cache_in_listener)
{
#ifdef HAVE_SOCKADDR_IN6_SCOPE_ID
  SshScopeIdStruct scope_id_struct;
#endif /* HAVE_SOCKADDR_IN6_SCOPE_ID */

  if (!SSH_IP6_IS_LINK_LOCAL(ip))
    return TRUE;

#ifdef HAVE_SOCKADDR_IN6_SCOPE_ID
  /* Resolve the scope ID from the argument. */
  if (scope_id)
    {
      if (!ssh_udp_resolve_scope_id(listener, &scope_id_struct, ip, scope_id))
        {
          SSH_DEBUG(SSH_D_FAIL, ("Could not resolve scope ID of the IPv6 "
                                 "link-local address %@",
                                 ssh_ipaddr_render, ip));
          return FALSE;
        }
    }
  else
    {
      /* Does the listener have a cached scope ID? */
      if (listener->scope_id_cached)
        scope_id_struct = listener->cached_scope_id;
      else
        /* No further methods left. */
        return FALSE;
    }

  /* Set the scope ID into the sockaddr6. */
  ssh_udp_set_sockaddr_scope_id(listener, sinaddr, &scope_id_struct);

  /* Should the scope ID be cached into the listener? */
  if (cache_in_listener && !listener->scope_id_cached)
    {
      listener->cached_scope_id = scope_id_struct;
      listener->scope_id_cached = TRUE;
    }

#else /* HAVE_SOCKADDR_IN6_SCOPE_ID */
  /* No scope ID to set in the sockaddr6 structure. */
#endif /* HAVE_SOCKADDR_IN6_SCOPE_ID */

  return TRUE;
}
#endif /* SSH_HAVE_IPV6 */

/* Creates an IPv4 UDP listener. */

static SshUdpPlatformListener
ssh_udp_make_ip4_listener(SshUdpListener generic_listener,
                          const unsigned char *local_address,
                          const unsigned char *local_port,
                          const unsigned char *remote_address,
                          const unsigned char *remote_port,
                          SshUdpListenerParams params,
                          SshUdpCallback callback,
                          void *context)
{
  SshUdpPlatformListener listener;
  struct sockaddr_in sinaddr;
  int ret, port;
  SshIpAddrStruct ip;

  /* Allocate and initialize the listener context. */
  listener = ssh_calloc(1, sizeof(*listener));

  if (listener == NULL)
    return NULL;

  listener->listener = generic_listener;
  listener->ipv6 = FALSE;
  listener->context = context;
  listener->callback = callback;
  listener->connected = FALSE;

  /* Create the socket. */
  listener->sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (listener->sock == -1)
    {
      ssh_free(listener);
      return NULL;
    }

  /* Set the common socket options. */
  ssh_udp_set_common_socket_options(listener, params);

  /* XXX multicast and interface selector in the local IP address. */

  if (local_address != NULL || local_port != NULL)
    {
      /* Initialize the address structure for the local address. */
      memset(&sinaddr, 0, sizeof(sinaddr));
      sinaddr.sin_family = AF_INET;

      if (local_port != NULL)
        {
          /* Look up the service name for the local port. */
          port = ssh_inet_get_port_by_service(local_port, ssh_custr("udp"));
          if (port == -1)
            {
              close(listener->sock);
              ssh_free(listener);
              return NULL;
            }
          sinaddr.sin_port = htons(port);
        }

      if (local_address != NULL && !SSH_IS_IPADDR_ANY(local_address))
        {
          /* Decode the IP address.  Host names are not accepted. */
          if (!ssh_ipaddr_parse(&ip, local_address))
            {
              close(listener->sock);
              ssh_free(listener);
              return NULL;
            }
          SSH_IP4_ENCODE(&ip, &sinaddr.sin_addr);
        }
      ret = bind(listener->sock, (struct sockaddr *)&sinaddr,
                 sizeof(sinaddr));
      if (ret == -1)
        {
          SSH_DEBUG(SSH_D_FAIL, ("ssh_udp_make_ip4_listener: "
                                 " bind failed: %s", strerror(errno)));
          close(listener->sock);
          ssh_free(listener);
          return NULL;
        }
    }

  if (remote_address != NULL || remote_port != NULL)
    {
      /* Initialize the address structure for the remote address. */
      memset(&sinaddr, 0, sizeof(sinaddr));
      sinaddr.sin_family = AF_INET;

      if (remote_port != NULL)
        {
          /* Look up the service name for the remote port. */
          port = ssh_inet_get_port_by_service(remote_port, ssh_custr("udp"));
          if (port == -1)
            {
              close(listener->sock);
              ssh_free(listener);
              return NULL;
            }
          sinaddr.sin_port = htons(port);
        }

      if (remote_address != NULL)
        {
          /* Decode the IP address.  Host names are not accepted. */
          if (!ssh_ipaddr_parse(&ip, remote_address))
            {
              close(listener->sock);
              ssh_free(listener);
              return NULL;
            }
          SSH_IP4_ENCODE(&ip, &sinaddr.sin_addr);
        }

      /* Mark the socket to be connected */
      listener->connected = TRUE;

      /* Connect the socket, so that we will receive unreachable
         notifications. */
      ret = connect(listener->sock, (struct sockaddr *)&sinaddr,
                    sizeof(sinaddr));
      if (ret == -1)
        {
          SSH_DEBUG(SSH_D_FAIL, ("ssh_udp_make_ip4_listener: connect failed: "\
                                 " %s", strerror(errno)));
          close(listener->sock);
          ssh_free(listener);
          return NULL;
        }
    }

  /* Set more common UDP socket options. */
  ssh_udp_set_more_common_socket_options(listener, params);

  /* Socket creation succeeded. Do the event loop stuff */
  if (ssh_io_register_fd(listener->sock, ssh_udp_io_cb,
                         (void *)listener) == FALSE)
    {
      close(listener->sock);
      ssh_free(listener);
      return NULL;
    }

  ssh_io_set_fd_request(listener->sock, callback ? SSH_IO_READ : 0);

  return listener;
}

#ifdef SSH_HAVE_IPV6
/* Creates an IPv6 UDP listener. */

static SshUdpPlatformListener
ssh_udp_make_ip6_listener(SshUdpListener generic_listener,
                          const unsigned char *local_address,
                          const unsigned char *local_port,
                          const unsigned char *remote_address,
                          const unsigned char *remote_port,
                          SshUdpListenerParams params,
                          SshUdpCallback callback,
                          void *context)
{
  SshUdpPlatformListener listener;
  struct sockaddr_in6 sinaddr;
  int ret, port;
  SshIpAddrStruct ip;
  unsigned char *scope_id;

  /* Allocate and initialize the listener context. */
  listener = ssh_calloc(1, sizeof(*listener));
  if (listener == NULL)
    goto error;

  listener->listener = generic_listener;
  listener->ipv6 = TRUE;
  listener->context = context;
  listener->callback = callback;
  listener->connected = FALSE;

  /* Create the socket. */
  listener->sock = socket(AF_INET6, SOCK_DGRAM, 0);
  if (listener->sock == -1)
    goto error;

  /* Set the common socket options. */
  ssh_udp_set_common_socket_options(listener, params);

  /* XXX multicast and interface selector in the local IP address. */

  if (local_address != NULL || local_port != NULL)
    {
      /* Initialize the address structure for the local address. */
      memset(&sinaddr, 0, sizeof(sinaddr));
      sinaddr.sin6_family = AF_INET6;

      if (local_port != NULL)
        {
          /* Look up the service name for the local port. */
          port = ssh_inet_get_port_by_service(local_port, ssh_custr("udp"));
          if (port == -1)
            goto error;
          sinaddr.sin6_port = htons(port);
        }

      if (local_address != NULL && !SSH_IS_IPADDR_ANY(local_address))
        {
          /* Decode the IP address.  Host names are not accepted. */
          if (!ssh_ipaddr_parse_with_scope_id(&ip, local_address, &scope_id))
            goto error;
          SSH_IP6_ENCODE(&ip, &sinaddr.sin6_addr);
          if (!ssh_udp_set_scope_id(listener, &sinaddr, &ip, scope_id, TRUE))
            goto error;
        }

      ret = bind(listener->sock, (struct sockaddr *)&sinaddr,
                 sizeof(sinaddr));
      if (ret == -1)
        {
          SSH_DEBUG(SSH_D_FAIL, ("ssh_udp_make_ip6_listener: bind failed: %s",
                                 strerror(errno)));
          goto error;
        }
    }

  if (remote_address != NULL || remote_port != NULL)
    {
      /* Initialize the address structure for the remote address. */
      memset(&sinaddr, 0, sizeof(sinaddr));
      sinaddr.sin6_family = AF_INET6;

      if (remote_port != NULL)
        {
          /* Look up the service name for the remote port. */
          port = ssh_inet_get_port_by_service(remote_port, ssh_custr("udp"));
          if (port == -1)
            goto error;
          sinaddr.sin6_port = htons(port);
        }

      if (remote_address != NULL)
        {
          /* Decode the IP address.  Host names are not accepted. */
          if (!ssh_ipaddr_parse_with_scope_id(&ip, remote_address, &scope_id))
            goto error;
          SSH_IP6_ENCODE(&ip, &sinaddr.sin6_addr);
          if (!ssh_udp_set_scope_id(listener, &sinaddr, &ip, scope_id, FALSE))
            goto error;
        }

      /* Mark the socket to be connected */
      listener->connected = TRUE;

      /* Connect the socket, so that we will receive unreachable
         notifications. */
      ret = connect(listener->sock, (struct sockaddr *)&sinaddr,
                    sizeof(sinaddr));
      if (ret == -1)
        {
          SSH_DEBUG(SSH_D_FAIL, ("ssh_udp_make_ip6_listener: connect failed: "\
                                 "%s", strerror(errno)));
          goto error;
        }
    }

  /* Set more common UDP socket options. */
  ssh_udp_set_more_common_socket_options(listener, params);

  /* Socket creation succeeded. Do the event loop stuff */
  if (ssh_io_register_fd(listener->sock, ssh_udp_io_cb,
                         (void *)listener) == FALSE)
    goto error;
  ssh_io_set_fd_request(listener->sock, callback ? SSH_IO_READ : 0);

  return listener;


  /* Error handling. */

 error:

  if (listener)
    {
      if (listener->sock >= 0)
        close(listener->sock);
      ssh_free(listener);
    }

  return NULL;
}
#endif /* SSH_HAVE_IPV6 */

/* Creates a listener for sending and receiving UDP packets.  The listener is
   connected if remote_address is non-NULL.  Connected listeners may receive
   notifications about the destination host/port being unreachable.
     local_address    local address for sending; SSH_IPADDR_ANY chooses
                      automatically
     local_port       local port for receiving udp packets
     remote_address   specifies the remote address for this listener
                      is non-NULL.  If specified, unreachable notifications
                      may be received for packets sent to the address.
     remote_port      remote port for packets sent using this listener, or NULL
     params           additional paameters for the listener.  This can be
                      NULL in which case the default parameters are used.
     callback         function to call when packet or notification available
     context          argument to pass to the callback. */

static void *
ssh_udp_platform_make_listener(void *make_listener_method_context,
                               SshUdpListener listener,
                               const unsigned char *local_address,
                               const unsigned char *local_port,
                               const unsigned char *remote_address,
                               const unsigned char *remote_port,
                               SshUdpListenerParams params,
                               SshUdpCallback callback,
                               void *context)
{
  SshUdpPlatformListener listener4 = NULL;
#ifdef SSH_HAVE_IPV6
  SshUdpPlatformListener listener6 = NULL;
#endif /* SSH_HAVE_IPV6 */
  unsigned char *scope_id;

  SSH_DEBUG(SSH_D_HIGHSTART, ("Making UDP listener"));

  /* Let's determine the type of listener to create. */
  if (local_address && !SSH_IS_IPADDR_ANY(local_address))
    {
      SshIpAddrStruct ipaddr;

      /* We are creating only an IPv4 or an IPv6 listener. */
      if (!ssh_ipaddr_parse_with_scope_id(&ipaddr, local_address, &scope_id))
        /* Malformed address. */
        return NULL;

      if (SSH_IP_IS4(&ipaddr))
        {
          SSH_DEBUG(SSH_D_HIGHSTART,
                    ("Making IPv4 only UDP listener for address %@",
                     ssh_ipaddr_render, &ipaddr));
          return ssh_udp_make_ip4_listener(listener,
                                           local_address, local_port,
                                           remote_address, remote_port,
                                           params,
                                           callback, context);
        }
      else
        {
#ifdef SSH_HAVE_IPV6
          SSH_DEBUG(SSH_D_HIGHSTART,
                    ("Making IPv6 only UDP listener for address %@",
                     ssh_ipaddr_render, &ipaddr));
          return ssh_udp_make_ip6_listener(listener,
                                           local_address, local_port,
                                           remote_address, remote_port,
                                           params,
                                           callback, context);
#else /* not  SSH_HAVE_IPV6 */
          SSH_DEBUG(SSH_D_HIGHSTART,
                    ("IPv6 is not supported on this platform"));
          return NULL;
#endif /* not SSH_HAVE_IPV6 */
        }
    }

  /* Create a dual listener for both IPv4 and IPv6. */
  SSH_DEBUG(SSH_D_HIGHSTART, ("Making IPv4 and IPv6 UDP listeners"));

  listener4 = ssh_udp_make_ip4_listener(listener,
                                        local_address, local_port,
                                        remote_address, remote_port,
                                        params,
                                        callback, context);
  if (listener4 == NULL)
    return NULL;

#ifdef SSH_HAVE_IPV6
  /* Try to create an IPv6 listener.  It is ok if this fails since
     there seems to be systems which do not support IPv6 although they
     know the in6 structures. */
  listener6 = ssh_udp_make_ip6_listener(listener,
                                        local_address, local_port,
                                        remote_address, remote_port,
                                        params,
                                        callback, context);
  if (listener6 != NULL)
    {
      /* We managed to make them both. */
      listener4->sibling = listener6;
    }
#endif /* SSH_HAVE_IPV6 */

  return listener4;
}

/* Destroys the udp listener. */

static void
ssh_udp_platform_destroy_listener(void *listener_context)
{
  SshUdpPlatformListener listener = (SshUdpPlatformListener) listener_context;

  if (listener->sibling)
    ssh_udp_platform_destroy_listener(listener->sibling);

  ssh_io_unregister_fd(listener->sock, TRUE);
  close(listener->sock);
  ssh_free(listener);
}

/* Reads the received packet or notification from the listener.  This
   function should be called from the listener callback.  This can be
   called multiple times from a callback; each call will read one more
   packet or notification from the listener until no more are
   available. */

static SshUdpError
ssh_udp_platform_read(void *listener_context,
#ifdef SUNWIPSEC
		      SshUdpPacketContext *packet_context,
#endif /* SUNWIPSEC */
                      unsigned char *remote_address, size_t remote_address_len,
                      unsigned char *remote_port, size_t remote_port_len,
                      unsigned char *datagram_buffer,
                      size_t datagram_buffer_len,
                      size_t *datagram_len_return)
{
  SshUdpPlatformListener listener = (SshUdpPlatformListener) listener_context;
  size_t ret;
  struct sockaddr_in from_addr4;
#ifdef SSH_HAVE_IPV6
  struct sockaddr_in6 from_addr6;
#endif /* SSH_HAVE_IPV6 */
  struct sockaddr *from_addr = NULL;
  int port = 0;
#ifdef HAVE_POSIX_STYLE_SOCKET_PROTOTYPES
  socklen_t fromlen = 0L, fromlen_min = 0L;
#else /* HAVE_POSIX_STYLE_SOCKET_PROTOTYPES */
  int fromlen = 0, fromlen_min = 0;
#endif /* HAVE_POSIX_STYLE_SOCKET_PROTOTYPES */
  SshIpAddrStruct ipaddr;
#ifdef SUNWIPSEC
#ifdef SO_RECVUCRED
  struct msghdr msg;
  struct iovec iov;
  char *ucredbuf;
  int ucredlen = ucred_size();
  int ucredbuflen;
  struct cmsghdr *cm;
  ucred_t *ucred = NULL;
#endif

  if (packet_context)
    *packet_context = NULL;
#endif /* SUNWIPSEC */

  if (datagram_len_return)
    *datagram_len_return = 0;

#ifdef SSH_HAVE_IPV6
  if (listener->ipv6)
    {
      from_addr = (struct sockaddr *) &from_addr6;
      fromlen = sizeof(from_addr6);
      fromlen_min = sizeof(from_addr6);
    }
#endif /* SSH_HAVE_IPV6 */
  if (!listener->ipv6)
    {
      from_addr = (struct sockaddr *) &from_addr4;
      fromlen = sizeof(from_addr4);
      fromlen_min = sizeof(from_addr4);
    }

#if defined(SUNWIPSEC) && defined(SO_RECVUCRED)
  ucredlen = ucred_size() + sizeof (struct cmsghdr);
  ucredbuf = ssh_malloc(ucredlen);

  if (ucredbuf == NULL)
	  return SSH_UDP_NO_DATA;	/* XXX Default choice for now. */

  iov.iov_base  = (void *)datagram_buffer;
  iov.iov_len = datagram_buffer_len;

  msg.msg_name = from_addr;
  msg.msg_namelen = fromlen;
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  msg.msg_control = ucredbuf;
  msg.msg_controllen = ucredlen;

  ret = recvmsg(listener->sock, &msg, 0);
#else

  ret = recvfrom(listener->sock, (void *)datagram_buffer, datagram_buffer_len,
                 0, from_addr, &fromlen);
#endif
  if (ret == (size_t)-1)
    {
#if defined(SUNWIPSEC) && defined(SO_RECVUCRED)
      /* Free the ucred buffer information now. */
      ssh_free(ucredbuf);
#endif
      SSH_DEBUG(SSH_D_UNCOMMON, ("Read result %ld, error = %s", (long)ret,
                                 strerror(errno)));
      switch (errno)
        {
#ifdef EHOSTDOWN
        case EHOSTDOWN:
#endif /* EHOSTDOWN */
#ifdef EHOSTUNREACH
        case EHOSTUNREACH:
#endif /* EHOSTUNREACH */
          return SSH_UDP_HOST_UNREACHABLE;

#ifdef ECONNREFUSED
        case ECONNREFUSED:
#endif /* ECONNREFUSED */
#ifdef ENOPROTOOPT
        case ENOPROTOOPT:
#endif /* ENOPROTOOPT */
          return SSH_UDP_PORT_UNREACHABLE;
        default:
          return SSH_UDP_NO_DATA;
        }
    }
  SSH_DEBUG(SSH_D_NICETOKNOW, ("Read got %ld bytes", (long)ret));

#if defined(SUNWIPSEC) && defined(SO_RECVUCRED)

  for (cm = CMSG_FIRSTHDR(&msg); cm != NULL; cm = CMSG_NXTHDR(&msg, cm))
    {
      if ((cm->cmsg_level == SOL_SOCKET) && (cm->cmsg_type = SCM_UCRED))
	{
	  bslabel_t *label;
	  char *buf;

	  ucred = ssh_malloc(ucredlen);
	  if (ucred == NULL) {
	    ssh_free(ucredbuf);
	    return SSH_UDP_NO_DATA;	/* XXX Default choice for now. */
	  }

	  memcpy(ucred, CMSG_DATA(cm), ucredlen);

	  label = ucred_getlabel(ucred);
	  if (label == NULL) {
	    ssh_free(ucredbuf);
	    return SSH_UDP_NO_DATA;	/* XXX Default choice for now. */
	  }
	  ipsec_convert_bslabel_to_hex(label, &buf);

	  ssh_free(buf);

	  break;
	}
    }
  ssh_free(ucredbuf);
  if (ucred != NULL)
    {
      SshUdpPacketContext pc = ssh_malloc (sizeof (*pc));
      pc->ucred = ucred;
      *packet_context = pc;
    }
#endif


  /* XXX __linux__ IPv6:

  Issue:
   - stupid glibc _and_ kernel _both_ have their own sockaddr_in6.

   Funnily enough, the one in glibc is bigger _and_ used by us,
   _but_ the kernel returns shorter one.

   Therefore, we cannot do size checks although stuff we're interested
   in is in the beginning and available in both.. hopefully. */


#ifndef __linux__
  if (fromlen >= fromlen_min)
#endif /* __linux__ */
    {
      /* Format port number in user buffer. */
      if (remote_port != NULL)
        {
#ifdef SSH_HAVE_IPV6
          if (listener->ipv6)
            port = ntohs(from_addr6.sin6_port);
#endif /* SSH_HAVE_IPV6 */
          if (!listener->ipv6)
            port = ntohs(from_addr4.sin_port);
          /* XXX temporary casts until library API is changed XXX */
          ssh_snprintf(ssh_sstr(remote_port), remote_port_len, "%d", port);
        }

      /* Format source address in user buffer. */
      if (remote_address != NULL)
        {
#ifdef SSH_HAVE_IPV6
          if (listener->ipv6)
            {
              SSH_IP6_DECODE(&ipaddr, &from_addr6.sin6_addr.s6_addr);

#ifdef __linux__
              {
                Boolean is_ipv4_mapped_address = TRUE;
                int i;

                /* IPv6 allows for mapping of ipv4 addresses
                   directly to IPv6 scope. For IKE purposes the
                   addresses are _not_ the same, and therefore
                   for now we simply change the addresses to IPv4
                   when they are really IPv4 - that is, match
                   mask

                   ::FFFF:0:0/96
                */
                for (i = 0 ; i < 10 ; i++)
                  if (ipaddr.addr_data[i])
                    {
                      is_ipv4_mapped_address = FALSE;
                      break;
                    }
                for (/* EMPTY */; i < 11 ; i++)
                  if (ipaddr.addr_data[i] != 0xff)
                    {
                      is_ipv4_mapped_address = FALSE;
                      break;
                    }
                if (is_ipv4_mapped_address)
                  SSH_IP4_DECODE(&ipaddr,
                                 &ipaddr.addr_data[12]);
              }
#endif /* __linux__ */
            }
#endif /* SSH_HAVE_IPV6 */
          if (!listener->ipv6)
            SSH_IP4_DECODE(&ipaddr, &from_addr4.sin_addr.s_addr);

          ssh_ipaddr_print(&ipaddr, remote_address, remote_address_len);
        }
    }

  /* Return the length of the received packet. */
  if (datagram_len_return)
    *datagram_len_return = ret;

  return SSH_UDP_OK;
}

/* This sends udp datagram to remote destination. This call always success, or
   the if not then datagram is silently dropped (udp is not reliable anyways */

static void
ssh_udp_platform_send(void *listener_context,
#ifdef SUNWIPSEC
		      SshUdpPacketContext packet_context,
#endif /* SUNWIPSEC */
                      const unsigned char *remote_address,
                      const unsigned char *remote_port,
                      const unsigned char *datagram_buffer,
                      size_t datagram_len)
{
  SshUdpPlatformListener listener = (SshUdpPlatformListener) listener_context;
  struct sockaddr *to_addr;
#ifdef HAVE_POSIX_STYLE_SOCKET_PROTOTYPES
  size_t to_addr_len;
#else /* HAVE_POSIX_STYLE_SOCKET_PROTOTYPES */
  int to_addr_len;
#endif /* HAVE_POSIX_STYLE_SOCKET_PROTOTYPES */
#if defined(SUNWIPSEC) && defined(SO_RECVUCRED)
  void *ucredbuf;
  size_t ucredbuflen;
  struct iovec iov;
  struct msghdr msg;
  int ret;
#endif /* SUNWIPSEC */

  int port = 0;
  SshIpAddrStruct ipaddr;
  unsigned char *scope_id;

  SSH_DEBUG(SSH_D_NICETOKNOW, ("Send %ld bytes", (long)datagram_len));

  if (listener->connected)
    {
      if (remote_port != NULL)
        SSH_DEBUG(SSH_D_FAIL,
                  ("ssh_udp_platform_send: Remote port number `%s' specified "
                   "for connected socket, ignored", remote_port));
      if (remote_address != NULL)
        SSH_DEBUG(SSH_D_FAIL,
                  ("ssh_udp_platform_send: Remote address `%s' specified for "
                   "connected socket, ignored", remote_address));

      /* Send the packet to the connected socket. */
      if (send(listener->sock, (void *)datagram_buffer, datagram_len,
               0) == -1)
        SSH_DEBUG(SSH_D_FAIL, ("ssh_udp_platform_send: send failed: %s",
                               strerror(errno)));
      return;
    }

  /* Decode the port number if given. */
  if (remote_port != NULL)
    {
      port = ssh_inet_get_port_by_service(remote_port, ssh_custr("udp"));
      if (port == -1)
        {
          SSH_DEBUG(2, ("ssh_udp_platform_send: bad port %s", remote_port));
          return;
        }
    }

  /* Decode the destination address if given. */
  memset(&ipaddr, 0, sizeof(ipaddr));
  if (remote_address)
    {
      /* First check if it is already an ip address. */
      if (!ssh_ipaddr_parse_with_scope_id(&ipaddr, remote_address, &scope_id))
        {
          SSH_DEBUG(SSH_D_FAIL, ("ssh_udp_platform_send: bad address %s",
                                 remote_address));
          return;
        }
    }

  if (SSH_IP_IS6(&ipaddr))
    {
      /* IPv6 addresses. */
#ifdef SSH_HAVE_IPV6
      struct sockaddr_in6 to_addr6;

      /* Do we have an IPv6 listener? */
      if (listener->ipv6)
        ;
      else if (listener->sibling && listener->sibling->ipv6)
        listener = listener->sibling;
      else
        {
          /* We do not have it. */
          SSH_DEBUG(SSH_D_FAIL, ("ssh_udp_platform_send: no IPv6 listener"));
          return;
        }

      memset(&to_addr6, 0, sizeof(to_addr6));
      to_addr6.sin6_family = AF_INET6;
      to_addr6.sin6_port = htons(port);
      SSH_IP6_ENCODE(&ipaddr, &to_addr6.sin6_addr);
      if (!ssh_udp_set_scope_id(listener, &to_addr6, &ipaddr, scope_id, FALSE))
        return;

      to_addr = (struct sockaddr *) &to_addr6;
      to_addr_len = sizeof(to_addr6);

#else /* not SSH_HAVE_IPV6 */
      SSH_DEBUG(SSH_D_FAIL, ("IPv6 is not supported on this platform"));
      return;
#endif /* SSH_HAVE_IPV6 */
    }
  else
    {
      /* IPv4 and unspecified remote address cases. */
      struct sockaddr_in to_addr4;

      memset(&to_addr4, 0, sizeof(to_addr));
      to_addr4.sin_family = AF_INET;
      to_addr4.sin_port = htons(port);

      if (remote_address)
        SSH_IP4_ENCODE(&ipaddr, &to_addr4.sin_addr);

      to_addr = (struct sockaddr *) &to_addr4;
      to_addr_len = sizeof(to_addr4);
    }

  /* Send the packet. */
#if defined(SUNWIPSEC) && defined(SO_RECVUCRED)

  ucredbuf = NULL;
  ucredbuflen = 0;

  if (packet_context != NULL)
    {
      ucred_t *ucred = packet_context->ucred;
      struct cmsghdr *hdr;
      bslabel_t *label;
      char *buf;

      size_t ucredlen = ucred_size();

      ucredbuflen = CMSG_SPACE(ucredlen);
      ucredbuf = ssh_malloc(ucredbuflen);

      if (ucredbuf == NULL) {
	SSH_DEBUG(SSH_D_FAIL, ("ssh_udp_platform_send: sendto failed: "
		               "no memory"));
	return;
      }

      hdr = (struct cmsghdr *)ucredbuf;

      hdr->cmsg_level = SOL_SOCKET;
      hdr->cmsg_type = SCM_UCRED;
      hdr->cmsg_len = CMSG_LEN(ucredlen);
      memcpy(CMSG_DATA(hdr), ucred, ucredlen);

      /* DEBUG CODE */

      label = ucred_getlabel(ucred);
      if (label == NULL) {
	ssh_free(ucredbuf);
	SSH_DEBUG(SSH_D_FAIL, ("ssh_udp_platform_send: sendto failed: "
		               "no memory"));
	return;
      }
      ipsec_convert_bslabel_to_hex(label, &buf);

      printf("sending SCM_UCRED at %p label %s length %d ucredlen %d\n",
	     hdr, buf, hdr->cmsg_len, ucredlen);
      /* END DEBUG CODE */

      ssh_free(buf);
    }
  else
    {
      if (is_system_labeled())
	printf("\n\nsending without packet context!\n\n");
    }

  iov.iov_base = (void *)datagram_buffer;
  iov.iov_len = datagram_len;

  msg.msg_name = to_addr;
  msg.msg_namelen = to_addr_len;
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  msg.msg_control = ucredbuf;
  msg.msg_controllen = ucredbuflen;

  ret = sendmsg(listener->sock, &msg, 0);
  ssh_free(ucredbuf);
#else
  ret = sendto(listener->sock, (void *)datagram_buffer, datagram_len, 0,
             to_addr, to_addr_len)
#endif
  if (ret == -1)
    SSH_DEBUG(SSH_D_FAIL, ("ssh_udp_platform_send: sendto failed: %s",
                           strerror(errno)));
}

/* Add membership to given multicast group */

static SshUdpError
ssh_udp_platform_multicast_add_membership(void *listener_context,
                                          const unsigned char *group_to_join,
                                          const unsigned char *
                                            interface_to_join)
{
  SshUdpPlatformListener listener = (SshUdpPlatformListener) listener_context;

  for (; listener; listener = listener->sibling)
    {
#ifdef SSH_HAVE_IPV6_MULTICAST
      if (listener->ipv6)
        {
          struct ipv6_mreq mreq6;
          size_t size = 16;

          memset(&mreq6, 0, sizeof(mreq6));

          if (!ssh_inet_strtobin(group_to_join,
                                 (unsigned char *)&(mreq6.ipv6mr_multiaddr.
                                                    s6_addr), &size) ||
              size != 16)
            continue;
          if (interface_to_join && !SSH_IS_IPADDR_ANY(interface_to_join))
            {
              /* XXX, add this later */
            }
          if (!setsockopt(listener->sock,
                          IPPROTO_IPV6,
                          IPV6_JOIN_GROUP,
                          &mreq6,
                          sizeof(mreq6)))
            {
              return SSH_UDP_OK;
            }
        }
      else
#endif /* SSH_HAVE_IPV6_MUILTICAST */
        {
#ifdef IP_ADD_MEMBERSHIP
          struct ip_mreq mreq;
          size_t size = 4;

          memset(&mreq, 0, sizeof(mreq));

          if (!ssh_inet_strtobin(group_to_join,
                                 (unsigned char *)&(mreq.imr_multiaddr.
                                                    s_addr), &size) ||
              size != 4)
            continue;

          if (interface_to_join && !SSH_IS_IPADDR_ANY(interface_to_join))
            {
              if (!ssh_inet_strtobin(interface_to_join,
                                     (unsigned char *)&(mreq.imr_interface.
                                                        s_addr), &size) ||
                  size != 4)
                continue;
            }
          if (!setsockopt(listener->sock,
                          IPPROTO_IP,
                          IP_ADD_MEMBERSHIP,
                          &mreq,
                          sizeof(mreq)))
            {
              return SSH_UDP_OK;
            }
#else /* IP_ADD_MEMBERSHIP */
          continue;
#endif /* IP_ADD_MEMBERSHIP */
        }
    }
  return SSH_UDP_INVALID_ARGUMENTS;
}

/* Drop membership to given multicast group */

static SshUdpError
ssh_udp_platform_multicast_drop_membership(void *listener_context,
                                           const unsigned char *group_to_drop,
                                           const unsigned char *
                                             interface_to_drop)
{
  SshUdpPlatformListener listener = (SshUdpPlatformListener) listener_context;

  for (; listener; listener = listener->sibling)
    {
#ifdef SSH_HAVE_IPV6_MULTICAST
      if (listener->ipv6)
        {
          struct ipv6_mreq mreq6;
          size_t size = 16;

          memset(&mreq6, 0, sizeof(mreq6));

          if (!ssh_inet_strtobin(group_to_drop,
                                 (unsigned char *)&(mreq6.ipv6mr_multiaddr.
                                                    s6_addr), &size) ||
              size != 16)
            continue;
          if (interface_to_drop && !SSH_IS_IPADDR_ANY(interface_to_drop))
            {
              /* XXX, add this later */
            }
          setsockopt(listener->sock,
                     IPPROTO_IPV6,
                     IPV6_LEAVE_GROUP,
                     &mreq6,
                     sizeof(mreq6));
        }
      else
#endif /* SSH_HAVE_IPV6_MULTICAST */
        {
#ifdef IP_DROP_MEMBERSHIP
          struct ip_mreq mreq;
          size_t size = 4;

          memset(&mreq, 0, sizeof(mreq));

          if (!ssh_inet_strtobin(group_to_drop,
                                 (unsigned char *)&(mreq.imr_multiaddr.
                                                    s_addr), &size) ||
              size != 4)
            continue;

          if (interface_to_drop && !SSH_IS_IPADDR_ANY(interface_to_drop))
            {
              if (!ssh_inet_strtobin(interface_to_drop,
                                     (unsigned char *)&(mreq.imr_interface.
                                                        s_addr), &size) ||
                  size != 4)
                continue;
            }
          setsockopt(listener->sock,
                     IPPROTO_IP,
                     IP_DROP_MEMBERSHIP,
                     &mreq,
                     sizeof(mreq));
#else /* IP_DROP_MEMBERSHIP */
          continue;
#endif /* IP_DROP_MEMBERSHIP */
        }
    }
  return SSH_UDP_OK;
}

/* Platform dependent UDP methods. */
static const SshUdpMethodsStruct ssh_udp_methods =
{
  ssh_udp_platform_make_listener,
  ssh_udp_platform_destroy_listener,
  ssh_udp_platform_read,
  ssh_udp_platform_send,
  ssh_udp_platform_multicast_add_membership,
  ssh_udp_platform_multicast_drop_membership,
#ifdef SUNWIPSEC
  ssh_udp_platform_create_context,
#endif /* SUNWIPSEC */
};

/* Fetch the platform dependent UDP methods and constructor
   context. */

SshUdpMethods
ssh_udp_platform_methods(void **constructor_context_return)
{
  *constructor_context_return = NULL;
  return (SshUdpMethods) &ssh_udp_methods;
}


#ifdef SUNWIPSEC

#include <netinet/udp.h>

/*
 * Pardon the old-fashioned name.  We don't push a STREAMS module any more for
 * NAT-Traversal semantics, we merely use a socket option.
 */
Boolean
ssh_push_natt_mod(SshUdpListener generic)
{
	extern void *ssh_udp_return_platform(SshUdpListener);
	SshUdpPlatformListener listener = ssh_udp_return_platform(generic);
	int on = 1;

	if (setsockopt(listener->sock, IPPROTO_UDP, UDP_NAT_T_ENDPOINT, &on,
	    sizeof (on)) < 0) {
		SSH_DEBUG(SSH_D_FAIL,
		    ("ssh_push_natt_mod: UDP_NAT_T_ENDPOINT failed: %s",
			strerror(errno)));
		close(listener->sock);
		return (FALSE);
	}

	return (TRUE);
}


Boolean
ssh_set_mac_bypass(SshUdpListener generic, Boolean enable)
{
	extern void *ssh_udp_return_platform(SshUdpListener);
	SshUdpPlatformListener listener;
	int bypass = enable ? 1 : 0;

	if (generic == NULL)
		return (FALSE);

	listener = ssh_udp_return_platform(generic);

	if (setsockopt(listener->sock, SOL_SOCKET, SO_MAC_IMPLICIT, &enable,
	    sizeof (enable)) < 0) {
		SSH_DEBUG(SSH_D_FAIL,
		    ("ssh_set_mac_bypass: SO_MAC_IMPLICIT failed: %s",
			strerror(errno)));
		close(listener->sock);
		return (FALSE);
	}

	return (TRUE);
}


#endif
