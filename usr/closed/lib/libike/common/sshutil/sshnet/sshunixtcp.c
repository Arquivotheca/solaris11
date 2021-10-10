/*

  Author: Tatu Ylonen <ylo@ssh.fi>
          Antti Huima <huima@ssh.fi>
          Tero Kivinen <kivinen@ssh.fi>
          Kim Nordlund <nordlund@ssh.fi>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
  Espoo, Finland
  All rights reserved.

  Unix-specific code for sockets.

*/

#include "sshincludes.h"
#include "sshstream.h"
#include "sshnameserver.h"
#include "sshtcp.h"
#include "sshfdstream.h"
#include "sshtimeouts.h"
#include "ssheloop.h"

#define MAX_IP_ADDR_LEN 16

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
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif /* HAVE_SYS_SELECT_H */

#if !defined(HAVE_GETHOSTNAME)
#if defined(HAVE_UNAME) && defined(HAVE_SYS_UTSNAME_H)
#  include <sys/utsname.h>
#endif
#endif

#if defined(HAVE_SOCKADDR_IN6_STRUCT) && defined(WITH_IPV6)
/* Currently, we include the IPv6 code only if we have the
   `sockaddr_in6' structure. */
#define SSH_HAVE_IPV6
#ifdef IPV6_JOIN_GROUP
#define SSH_HAVE_IPV6_MULTICAST
#endif /* IPV6_JOIN_GROUP */
#endif /* HAVE_SOCKADDR_IN6_STRUCT && WITH_IPV6 */

#define SSH_DEBUG_MODULE "SshUnixTcp"

typedef struct LowConnectRec
{
  int sock;
  unsigned int port;
  SshTcpCallback callback;
  void *context;
  SshIpAddrStruct ipaddr;
  SshOperationHandle handle;
} *LowConnect;

#if !defined(HAVE_GETSERVBYNAME) && defined(WANT_SERVBYNAME)\
 || !defined(HAVE_GETSERVBYPORT) && defined(WANT_SERVBYPORT)

/* that exports __global_table[] as "root" of data */
#include "sshgetservbyname_servicetable.c"

/* done a a define here, so comparison can be turned to
   case-insignificant if needed, first argument should
   rather be the statically allocated string to assure
   that we don't read far past the user string if it
   somehow happens to be non-terminated.
  */
#define GETSERV_STRCMP(x,y) strncmp((x), (y), strlen(x)+1)

static Boolean find_in_aliases(char const * const * const aliases,
                               char const * const name)
{
  int i;
  for (i = 0; ; i++)
    {
      if (aliases[i]==NULL) return FALSE;
      if (GETSERV_STRCMP(aliases[i], name)==0) return TRUE;
    }
}

static struct SshServent const *
ssh_getserv(const char* name, int port, Boolean byname, const char* protocol)
{
  int i;
  int k;

  for (i = 0; ; i++)
    {
      if (__global_table[i].protocol == NULL)
        return NULL;
      if (protocol != NULL &&
          GETSERV_STRCMP(__global_table[i].protocol, protocol)!=0)
        continue;
      /* search item in list for this protocol */
      for (k = 0; ; k++)
        {
          if ((byname!=FALSE)
              ? find_in_aliases(__global_table[i].table[k].s_aliases, name)
              : __global_table[i].table[k].s_port == port )
            return &(__global_table[i].table[k]);
        }
    }
}

#endif /* SERVBYNAME */

void ssh_socket_set_reuseaddr(int sock)
{
#ifdef SO_REUSEADDR
  int on = 1;
  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (void *)&on,
             sizeof(on));
#endif /* SO_REUSEADDR */
}

void ssh_socket_set_reuseport(int sock)
{
#ifdef SO_REUSEPORT
  int on = 1;
  setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, (void *)&on,
             sizeof(on));
#endif /* SO_REUSEPORT */
}

#ifdef NO_NONBLOCKING_CONNECT

SshOperationHandle ssh_socket_low_connect_try_once(unsigned int events,
                                                   void *context)
{
  LowConnect c = (LowConnect)context;
  int ret = -1;
  SshTcpError error;

  if (SSH_IP_IS6(&c->ipaddr))
    {
#ifdef HAVE_SOCKADDR_IN6_STRUCT
      struct sockaddr_in6 sinaddr6;

      memset(&sinaddr6, 0, sizeof(sinaddr6));
      sinaddr6.sin6_family = AF_INET6;
      sinaddr6.sin6_port = htons(c->port);
      memcpy(sinaddr6.sin6_addr.s6_addr, c->ipaddr.addr_data, 16);

      /* Make a blocking connect attempt. */
      ret = connect(c->sock, (struct sockaddr *)&sinaddr6, sizeof(sinaddr6));
#endif /* HAVE_SOCKADDR_IN6_STRUCT */
    }
  else
    {
      struct sockaddr_in sinaddr;

      memset(&sinaddr, 0, sizeof(sinaddr));
      sinaddr.sin_family = AF_INET;
      sinaddr.sin_port = htons(c->port);
      sinaddr.sin_addr.s_addr = htonl(SSH_IP4_TO_INT(&(c->ipaddr)));

      /* Make a blocking connect attempt. */
      ret = connect(c->sock, (struct sockaddr *)&sinaddr, sizeof(sinaddr));
    }

  if (ret >= 0 || errno == EISCONN) /* Connection is ready. */
    {
      SshStream str;

      str = ssh_stream_fd_wrap(c->sock);

      if (str == NULL)
        {
          SSH_DEBUG(SSH_D_FAIL,("Insufficient memory to create TCP stream."));
          close(c->sock);
          (*c->callback)(SSH_TCP_FAILURE,NULL,c->context);
          ssh_free(c);
          return NULL;
        }

      /* Successful connection. */

      (*c->callback)(SSH_TCP_OK, ssh_stream_fd_wrap(c->sock, TRUE),
                     c->context);
      ssh_free(c);
      return NULL;
    }

  /* Connection failed. */
  SSH_DEBUG(5, ("Connect failed: %s", strerror(errno)));
  error = SSH_TCP_FAILURE;
#ifdef ENETUNREACH
  if (errno == ENETUNREACH)
    error = SSH_TCP_UNREACHABLE;
#endif
#ifdef ECONNREFUSED
  if (errno == ECONNREFUSED)
    error = SSH_TCP_REFUSED;
#endif
#ifdef EHOSTUNREACH
  if (errno == EHOSTUNREACH)
    error = SSH_TCP_UNREACHABLE;
#endif
#ifdef ENETDOWN
  if (errno == ENETDOWN)
    error = SSH_TCP_UNREACHABLE;
#endif
#ifdef ETIMEDOUT
  if (errno == ETIMEDOUT)
    error = SSH_TCP_TIMEOUT;
#endif

  close(c->sock);
  (*c->callback)(error, NULL, c->context);
  ssh_free(c);
  return NULL;
}

#else /* NO_NONBLOCKING_CONNECT */

/* Connection aborted out */
void ssh_tcp_low_connect_aborted(void *context)
{
  LowConnect c = (LowConnect)context;

  ssh_io_unregister_fd(c->sock, FALSE);
  close(c->sock);

  ssh_free(c);
}

SshOperationHandle ssh_socket_low_connect_try(unsigned int events,
                                              void *context)
{
  LowConnect c = (LowConnect)context;
  int ret = 1;
  SshTcpError error;

  if (SSH_IP_IS6(&(c->ipaddr)))
    {
#ifdef HAVE_SOCKADDR_IN6_STRUCT
      struct sockaddr_in6 sinaddr6;

      memset(&sinaddr6, 0, sizeof(sinaddr6));
      sinaddr6.sin6_family = AF_INET6;
      sinaddr6.sin6_port = htons(c->port);
      memcpy(sinaddr6.sin6_addr.s6_addr, c->ipaddr.addr_data, 16);

      /* Make a non-blocking connect attempt. */
      ret = connect(c->sock, (struct sockaddr *)&sinaddr6, sizeof(sinaddr6));
#endif /* HAVE_SOCKADDR_IN6_STRUCT */
    }
  else
    {
      struct sockaddr_in sinaddr;

      memset(&sinaddr, 0, sizeof(sinaddr));
      sinaddr.sin_family = AF_INET;
      sinaddr.sin_port = htons(c->port);
      sinaddr.sin_addr.s_addr = htonl(SSH_IP4_TO_INT(&(c->ipaddr)));

      /* Make a non-blocking connect attempt. */
      ret = connect(c->sock, (struct sockaddr *)&sinaddr, sizeof(sinaddr));
    }

  if (ret >= 0 || errno == EISCONN) /* Connection is ready. */
    {
      /* Successful connection. */
      ssh_io_unregister_fd(c->sock, FALSE);
      (*c->callback)(SSH_TCP_OK, ssh_stream_fd_wrap(c->sock, TRUE),
                     c->context);
      if (c->handle)
        ssh_operation_unregister(c->handle);
      ssh_free(c);
      return NULL;
    }
  if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EALREADY)
    {
      /* Connection still in progress.  */
      ssh_io_set_fd_request(c->sock, SSH_IO_WRITE);

      if (!c->handle)
        c->handle = ssh_operation_register(ssh_tcp_low_connect_aborted, c);

      return c->handle;
    }

  SSH_DEBUG(5, ("Connect failed: %s", strerror(errno)));
  /* Connection failed. */
  error = SSH_TCP_FAILURE;
#ifdef ENETUNREACH
  if (errno == ENETUNREACH)
    error = SSH_TCP_UNREACHABLE;
#endif
#ifdef ECONNREFUSED
  if (errno == ECONNREFUSED)
    error = SSH_TCP_REFUSED;
#endif
#ifdef EHOSTUNREACH
  if (errno == EHOSTUNREACH)
    error = SSH_TCP_UNREACHABLE;
#endif
#ifdef ENETDOWN
  if (errno == ENETDOWN)
    error = SSH_TCP_UNREACHABLE;
#endif
#ifdef ETIMEDOUT
  if (errno == ETIMEDOUT)
    error = SSH_TCP_TIMEOUT;
#endif

  ssh_io_unregister_fd(c->sock, FALSE);
  close(c->sock);

  (*c->callback)(error, NULL, c->context);
  if (c->handle)
    ssh_operation_unregister(c->handle);
  ssh_free(c);
  return NULL;
}

#endif /* NO_NONBLOCKING_CONNECT */

/* Connects to the given address/port, and makes a stream for it.
   The address to use is the first address from the list. */

SshOperationHandle ssh_socket_low_connect(
                                const unsigned char *local_address,
                                unsigned int local_port,
                                SshTcpReusableType local_reusable,
                                const unsigned char *address_list,
                                unsigned int port,
                                SshTcpCallback callback,
                                void *context)
{
  int sock = -1, first_len;
  LowConnect c;
  unsigned char *tmp;

  /* Save data in a context structure. */
  if ((c = ssh_calloc(1, sizeof(*c))) == NULL)
    {
      (*callback)(SSH_TCP_FAILURE, NULL, context);
      return NULL;
    }

  /* Compute the length of the first address on the list. */
  if (strchr(ssh_csstr(address_list), ','))
    first_len = ssh_ustr(strchr(ssh_csstr(address_list), ',')) - address_list;
  else
    first_len = strlen(ssh_csstr(address_list));

  tmp = ssh_memdup(address_list, first_len);

  if (!tmp || !ssh_ipaddr_parse(&(c->ipaddr), tmp))
    {
      ssh_free(tmp);
      ssh_free(c);
      (*callback)(SSH_TCP_NO_ADDRESS, NULL, context);
      return NULL;
    }

  ssh_free(tmp);

  /* Create a socket. */
  if (SSH_IP_IS6(&(c->ipaddr)))
    {
#ifdef HAVE_SOCKADDR_IN6_STRUCT
      sock = socket(AF_INET6, SOCK_STREAM, 0);
#endif /* HAVE_SOCKADDR_IN6_STRUCT */
    }
  else
    {
      sock = socket(AF_INET, SOCK_STREAM, 0);
    }
  if (sock < 0)
    {
      ssh_free(c);
      (*callback)(SSH_TCP_FAILURE, NULL, context);
      return NULL;
    }

  switch (local_reusable)
    {
    case SSH_TCP_REUSABLE_PORT:
      ssh_socket_set_reuseport(sock);
      break;

    case SSH_TCP_REUSABLE_ADDRESS:
      ssh_socket_set_reuseaddr(sock);
      break;

    case SSH_TCP_REUSABLE_BOTH:
      ssh_socket_set_reuseport(sock);
      ssh_socket_set_reuseaddr(sock);
      break;

    case SSH_TCP_REUSABLE_NONE:
      break;

    }

  /* Bind local end if requested. */
  if (local_address || local_port)
    {
      SshIpAddrStruct ipaddr;

      if (local_address == NULL || SSH_IS_IPADDR_ANY(local_address))
        {
          if (SSH_IP_IS4(&c->ipaddr))
            local_address = ssh_custr(SSH_IPADDR_ANY_IPV4);
          else
            local_address = ssh_custr(SSH_IPADDR_ANY_IPV6);
        }

      if (!ssh_ipaddr_parse(&ipaddr, local_address))
        {
        error_local:
          close(sock);
          ssh_free(c);
          (*callback)(SSH_TCP_FAILURE, NULL, context);
          return NULL;
        }

      if (SSH_IP_IS4(&ipaddr))
        {
          struct sockaddr_in sinaddr;

          memset(&sinaddr, 0, sizeof(sinaddr));
          sinaddr.sin_family = AF_INET;
          sinaddr.sin_port = htons(local_port);

#ifdef BROKEN_INET_ADDR
          sinaddr.sin_addr.s_addr = inet_network(ssh_csstr(local_address));
#else /* BROKEN_INET_ADDR */
          sinaddr.sin_addr.s_addr = inet_addr(ssh_csstr(local_address));
#endif /* BROKEN_INET_ADDR */
          if ((sinaddr.sin_addr.s_addr & 0xffffffff) == 0xffffffff)
            goto error_local;

          if (bind(sock, (struct sockaddr *) &sinaddr, sizeof(sinaddr)) < 0)
            {
              SSH_DEBUG(SSH_D_FAIL, ("Bind failed: %s", strerror(errno)));
              goto error_local;
            }
        }
      else if (SSH_IP_IS6(&ipaddr))
        {
#ifdef HAVE_SOCKADDR_IN6_STRUCT
          struct sockaddr_in6 sinaddr6;
          size_t addr_len;

          memset(&sinaddr6, 0, sizeof(sinaddr6));
          sinaddr6.sin6_family = AF_INET6;
          sinaddr6.sin6_port = htons(local_port);
          SSH_IP_ENCODE(&ipaddr, sinaddr6.sin6_addr.s6_addr, addr_len);

          if (bind(sock, (struct sockaddr *)&sinaddr6, sizeof(sinaddr6)) < 0)
            {
              SSH_DEBUG(SSH_D_FAIL, ("Bind failed: %s", strerror(errno)));
              goto error_local;
            }
#else /* HAVE_SOCKADDR_IN6_STRUCT */
          SSH_DEBUG(SSH_D_ERROR, ("No sockaddr_in6 structure"));
          goto error_local;
#endif /* HAVE_SOCKADDR_IN6_STRUCT */
        }
      else
        {
          SSH_DEBUG(SSH_D_ERROR, ("Could not parse local address `%s'",
                                  local_address));
          goto error_local;
        }
    }

  c->sock = sock;
  c->port = port;
  c->callback = callback;
  c->context = context;
  c->handle = NULL;

#ifdef NO_NONBLOCKING_CONNECT

  /* Try connect once.  Function calls user callback. */
  return ssh_socket_low_connect_try_once(SSH_IO_WRITE, (void *)c);

#else /* NO_NONBLOCKING_CONNECT */

  /* Register it and request events. */
  if (ssh_io_register_fd(sock,
                         (void (*)(unsigned int events, void *context))
                         ssh_socket_low_connect_try, (void *)c)
      == FALSE)
    {
      SSH_DEBUG(SSH_D_FAIL,("Failed to register file descriptor!"));
      goto error_local;
    }
  ssh_io_set_fd_request(sock, SSH_IO_WRITE);

  /* Fake a callback to start asynchronous connect. */
  return ssh_socket_low_connect_try(SSH_IO_WRITE, (void *)c);

#endif /* NO_NONBLOCKING_CONNECT */
}

/* --------- function for listening for connections ---------- */

struct SshTcpListenerRec
{
  int sock;
  char *path;
  SshTcpCallback callback;
  void *context;
  SshTcpListener sibling;
};

/* This callback is called whenever a new connection is made to a listener
   socket. */

void ssh_tcp_listen_callback(unsigned int events, void *context)
{
  SshTcpListener listener = (SshTcpListener)context;
  int sock;
  struct sockaddr_in sinaddr;
#ifdef HAVE_POSIX_STYLE_SOCKET_PROTOTYPES
  socklen_t addrlen;
#else /* HAVE_POSIX_STYLE_SOCKET_PROTOTYPES */
  int addrlen;
#endif /* HAVE_POSIX_STYLE_SOCKET_PROTOTYPES */

  if (events & SSH_IO_READ)
    {
      SshStream stream;

      addrlen = sizeof(sinaddr);
      sock = accept(listener->sock, (struct sockaddr *)&sinaddr, &addrlen);
      if (sock < 0)
        {
          ssh_debug("ssh_tcp_listen_callback: accept failed");
          return;
        }

      /* Re-enable requests on the listener. */
      ssh_io_set_fd_request(listener->sock, SSH_IO_READ);

      stream = ssh_stream_fd_wrap(sock, TRUE);
      ssh_stream_fd_mark_forked(stream);

      /* Inform user callback of the new socket.  Note that this might
         destroy the listener. */
      (*listener->callback)(SSH_TCP_NEW_CONNECTION,
                            stream,
                            listener->context);
    }
}

/* Creates a socket that listens for new connections.  The address
   must be an ip-address in the form "nnn.nnn.nnn.nnn".  "0.0.0.0"
   indicates any host; otherwise it should be the address of some
   interface on the system.  The given callback will be called whenever
   a new connection is received at the socket.  This returns NULL on error. */

static SshTcpListener
ssh_tcp_make_ip4_listener(const unsigned char *local_address,
                          const unsigned char *port_or_service,
                          const SshTcpListenerParams params,
                          SshTcpCallback callback,
                          void *context)
{
  int sock = -1, port, listen_backlog, buf_len;
  SshTcpListener listener4;
  SshIpAddrStruct ipaddr;

  if (!local_address || SSH_IS_IPADDR_ANY(local_address))
    local_address = ssh_custr(SSH_IP4_NULLADDR);

  if (!ssh_ipaddr_parse(&ipaddr, local_address))
    return NULL;

  /* Parse port and address. */
  port = ssh_inet_get_port_by_service(port_or_service, ssh_custr("tcp"));

  /* Create a socket. */
  sock = socket(AF_INET, SOCK_STREAM, 0);

  if (sock < 0)
    return NULL;

  if (!params)
    {
      ssh_socket_set_reuseaddr(sock);
    }
  else
    {
      switch (params->listener_reusable)
        {
        case SSH_TCP_REUSABLE_PORT:
          ssh_socket_set_reuseport(sock);
          break;
        case SSH_TCP_REUSABLE_ADDRESS:
          ssh_socket_set_reuseaddr(sock);
          break;
        case SSH_TCP_REUSABLE_BOTH:
          ssh_socket_set_reuseport(sock);
          ssh_socket_set_reuseaddr(sock);
          break;
        case SSH_TCP_REUSABLE_NONE:
          break;
        }
    }

    {
      struct sockaddr_in sinaddr;

      memset(&sinaddr, 0, sizeof(sinaddr));
      sinaddr.sin_family = AF_INET;
      sinaddr.sin_port = htons(port);

#ifdef BROKEN_INET_ADDR
      sinaddr.sin_addr.s_addr = inet_network(ssh_csstr(local_address));
#else /* BROKEN_INET_ADDR */
      sinaddr.sin_addr.s_addr = inet_addr(ssh_csstr(local_address));
#endif /* BROKEN_INET_ADDR */
      if ((sinaddr.sin_addr.s_addr & 0xffffffff) == 0xffffffff)
        return NULL;

      if (bind(sock, (struct sockaddr *)&sinaddr, sizeof(sinaddr)) < 0)
        {
          close(sock);
          return NULL;
        }
    }

  listen_backlog = 16;
  if (params && params->listen_backlog != 0)
    listen_backlog = params->listen_backlog;
  if (listen(sock, listen_backlog) < 0)
    {
      close(sock);
      return NULL;
    }

#ifdef SO_SNDBUF
  if (params && params->send_buffer_size != 0)
    {
      buf_len = params->send_buffer_size;
      if (setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &buf_len, sizeof(int)) == -1)
        {
          SSH_DEBUG(3,
                    ("ssh_tcp_make_ip4_listener: setsockopt "
                     "SO_SNDBUF failed: %s",
                     strerror(errno)));
        }
    }
#endif /* SO_SNDBUF */
#ifdef SO_RCVBUF
  if (params && params->receive_buffer_size != 0)
    {
      buf_len = params->receive_buffer_size;
      if (setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &buf_len, sizeof(int)) == -1)
        {
          SSH_DEBUG(3,
                    ("ssh_tcp_make_ip4_listener: setsockopt "
                     "SO_RCVBUF failed: %s",
                     strerror(errno)));
        }
    }
#endif /* SO_RCVBUF */

  if ((listener4 = ssh_calloc(1, sizeof(*listener4))) != NULL)
    {
      listener4->sock = sock;
      listener4->path = NULL;
      listener4->callback = callback;
      listener4->context = context;

      if (ssh_io_register_fd(sock, ssh_tcp_listen_callback, (void *)listener4)
          == FALSE)
        {
          ssh_free(listener4);
          close(sock);
          return NULL;
        }
      ssh_io_set_fd_request(sock, SSH_IO_READ);
    }
  return listener4;
}



#ifdef SSH_HAVE_IPV6
/* Creates a socket that listens for new connections.  The address
   must be an ip-address in the form "nnn.nnn.nnn.nnn".  "0.0.0.0"
   indicates any host; otherwise it should be the address of some
   interface on the system.  The given callback will be called whenever
   a new connection is received at the socket.  This returns NULL on error. */

static SshTcpListener
ssh_tcp_make_ip6_listener(const unsigned char *local_address,
                          const unsigned char *port_or_service,
                          const SshTcpListenerParams params,
                          SshTcpCallback callback,
                          void *context)
{
  int sock = -1, port, listen_backlog, buf_len;
  SshTcpListener listener6;
  SshIpAddrStruct ipaddr;

  if (!local_address || SSH_IS_IPADDR_ANY(local_address))
    local_address = ssh_custr(SSH_IP6_NULLADDR);

  if (!ssh_ipaddr_parse(&ipaddr, local_address))
    return NULL;

  /* Parse port and address. */
  port = ssh_inet_get_port_by_service(port_or_service, ssh_custr("tcp"));

  /* Create a socket. */
#ifdef HAVE_SOCKADDR_IN6_STRUCT
  sock = socket(AF_INET6, SOCK_STREAM, 0);
#endif /* HAVE_SOCKADDR_IN6_STRUCT */

  if (sock < 0)
    return NULL;

  if (!params)
    {
      ssh_socket_set_reuseaddr(sock);
    }
  else
    {
      switch (params->listener_reusable)
        {
        case SSH_TCP_REUSABLE_PORT:
          ssh_socket_set_reuseport(sock);
          break;
        case SSH_TCP_REUSABLE_ADDRESS:
          ssh_socket_set_reuseaddr(sock);
          break;
        case SSH_TCP_REUSABLE_BOTH:
          ssh_socket_set_reuseport(sock);
          ssh_socket_set_reuseaddr(sock);
          break;
        case SSH_TCP_REUSABLE_NONE:
          break;
        }
    }

  if (SSH_IP_IS6(&ipaddr))
    {
#ifdef HAVE_SOCKADDR_IN6_STRUCT
      struct sockaddr_in6 sinaddr6;

      memset(&sinaddr6, 0, sizeof(sinaddr6));
      sinaddr6.sin6_family = AF_INET6;
      sinaddr6.sin6_port = htons(port);
      memcpy(sinaddr6.sin6_addr.s6_addr, ipaddr.addr_data, 16);

      if (bind(sock, (struct sockaddr *)&sinaddr6, sizeof(sinaddr6)) < 0)
        {
          close(sock);
          return NULL;
        }
#endif /* HAVE_SOCKADDR_IN6_STRUCT */
    }

  listen_backlog = 16;
  if (params && params->listen_backlog != 0)
    listen_backlog = params->listen_backlog;
  if (listen(sock, listen_backlog) < 0)
    {
      close(sock);
      return NULL;
    }

#ifdef SO_SNDBUF
  if (params && params->send_buffer_size != 0)
    {
      buf_len = params->send_buffer_size;
      if (setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &buf_len, sizeof(int)) == -1)
        {
          SSH_DEBUG(3,
                    ("ssh_tcp_make_ip6_listener: setsockopt "
                     "SO_SNDBUF failed: %s",
                     strerror(errno)));
        }
    }
#endif /* SO_SNDBUF */
#ifdef SO_RCVBUF
  if (params && params->receive_buffer_size != 0)
    {
      buf_len = params->receive_buffer_size;
      if (setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &buf_len, sizeof(int)) == -1)
        {
          SSH_DEBUG(3,
                    ("ssh_tcp_make_ip6_listener: setsockopt "
                     "SO_RCVBUF failed: %s",
                     strerror(errno)));
        }
    }
#endif /* SO_RCVBUF */

  if ((listener6 = ssh_calloc(1, sizeof(*listener6))) != NULL)
    {
      listener6->sock = sock;
      listener6->path = NULL;
      listener6->callback = callback;
      listener6->context = context;

      if (ssh_io_register_fd(sock, ssh_tcp_listen_callback, (void *)listener6)
          == FALSE)
        {
          ssh_free(listener6);
          close(sock);
          return NULL;
        }

      ssh_io_set_fd_request(sock, SSH_IO_READ);
    }
  return listener6;
}
#endif /* SSH_HAVE_IPV6 */


SshTcpListener ssh_tcp_make_listener(const unsigned char *local_address,
                                     const unsigned char *port_or_service,
                                     const SshTcpListenerParams params,
                                     SshTcpCallback callback,
                                     void *context)
{
  SshTcpListener listener4 = NULL;
#ifdef SSH_HAVE_IPV6
  SshTcpListener listener6 = NULL;
#endif /* SSH_HAVE_IPV6 */

  SSH_DEBUG(SSH_D_HIGHSTART, ("Making TCP listener"));

  /* Let's determine the type of listener to create. */
  if (local_address && !SSH_IS_IPADDR_ANY(local_address))
    {
      SshIpAddrStruct ipaddr;

      /* We are creating only an IPv4 or an IPv6 listener. */
      if (!ssh_ipaddr_parse(&ipaddr, local_address))
        /* Malformed address. */
        return NULL;

      if (SSH_IP_IS4(&ipaddr))
        {
          SSH_DEBUG(SSH_D_HIGHSTART,
                    ("Making IPv4 only TCP listener for address %@",
                     ssh_ipaddr_render, &ipaddr));
          return ssh_tcp_make_ip4_listener(local_address, port_or_service,
                                           params, callback, context);
        }
      else
        {
#ifdef SSH_HAVE_IPV6
          SSH_DEBUG(SSH_D_HIGHSTART,
                    ("Making IPv6 only TCP listener for address %@",
                     ssh_ipaddr_render, &ipaddr));
          return ssh_tcp_make_ip6_listener(local_address, port_or_service,
                                           params, callback, context);
#else /* not  SSH_HAVE_IPV6 */
          SSH_DEBUG(SSH_D_HIGHSTART,
                    ("IPv6 is not supported on this platform"));
          return NULL;
#endif /* not SSH_HAVE_IPV6 */
        }
    }

  /* Create a dual listener for both IPv4 and IPv6. */
  SSH_DEBUG(SSH_D_HIGHSTART, ("Making IPv4 and IPv6 TCP listeners"));

#ifdef SSH_HAVE_IPV6
  /* Try to create an IPv6 listener.  It is ok if this fails since
     there seems to be systems which do not support IPv6 although they
     know the in6 structures. */
  listener6 = ssh_tcp_make_ip6_listener(ssh_custr(SSH_IPADDR_ANY_IPV6),
                                        port_or_service,
                                        params,
                                        callback,
                                        context);
#endif /* SSH_HAVE_IPV6 */
  listener4 = ssh_tcp_make_ip4_listener(ssh_custr(SSH_IPADDR_ANY_IPV4),
                                        port_or_service,
                                        params,
                                        callback,
                                        context);
#ifdef SSH_HAVE_IPV6
  if ((listener4 != NULL) && (listener6 != NULL))
    listener4->sibling = listener6;
  else if (listener4 == NULL)
    listener4 = listener6;
#endif /* SSH_HAVE_IPV6 */
  return listener4;
}

Boolean
ssh_tcp_listener_get_local_port(SshTcpListener listener,
                                unsigned char *buf,
                                size_t buflen)
{
  struct sockaddr_in addr;
#ifdef HAVE_POSIX_STYLE_SOCKET_PROTOTYPES
  socklen_t addr_len;
#else /* HAVE_POSIX_STYLE_SOCKET_PROTOTYPES */
  int addr_len;
#endif /* HAVE_POSIX_STYLE_SOCKET_PROTOTYPES */
  addr_len = sizeof(addr);

  if (getsockname(listener->sock, (struct sockaddr *)&addr, &addr_len) < 0)
    return FALSE;
  /* XXX temporary casts until library API is changed XXX */
  return (ssh_snprintf(ssh_sstr(buf), buflen, "%u", ntohs(addr.sin_port)) > 0);
}

/* Destroys the socket.  It is safe to call this from a callback. */

void ssh_tcp_destroy_listener(SshTcpListener listener)
{
  if (listener->sibling)
    ssh_tcp_destroy_listener(listener->sibling);

  ssh_io_unregister_fd(listener->sock, FALSE);
  close(listener->sock);

  if (listener->path)
    {
      /* Do not remove the listener here.  There are situations where we
         fork after creating a listener, and want to close it in one but not
         the other fork.  Thus, listeners should be removed by the application
         after they have been destroyed. */
      /* remove(listener->path); */
      ssh_free(listener->path);
    }
  ssh_free(listener);
}

/* Returns true (non-zero) if the socket behind the stream has IP options set.
   This returns FALSE if the stream is not a socket stream. */

Boolean ssh_tcp_has_ip_options(SshStream stream)
{
  int sock, ret = -1;
  char *options;
#ifdef HAVE_POSIX_STYLE_SOCKET_PROTOTYPES
  socklen_t option_size;
#else /* HAVE_POSIX_STYLE_SOCKET_PROTOTYPES */
  int option_size;
#endif /* HAVE_POSIX_STYLE_SOCKET_PROTOTYPES */

  sock = ssh_stream_fd_get_readfd(stream);
  if (sock == -1)
    return FALSE;
  option_size = 8192;
  if ((options = ssh_malloc(option_size)) != NULL)
    {
      ret = getsockopt(sock, IPPROTO_IP, IP_OPTIONS, options,
                       &option_size);
      ssh_free(options);
    }
  else
    option_size = 0;

  return (ret >= 0 && option_size != 0);
}

/* Returns the ip-address of the remote host, as string.  This returns
   FALSE if the stream is not a socket stream or buffer space is
   insufficient. */

Boolean ssh_tcp_get_remote_address(SshStream stream, unsigned char *buf,
                                   size_t buflen)
{
#ifdef HAVE_SOCKADDR_IN6_STRUCT
  struct sockaddr_in6 saddr;
  SshIpAddrStruct ip;
#else /* HAVE_SOCKADDR_IN6_STRUCT */
  struct sockaddr_in saddr;
#endif /* HAVE_SOCKADDR_IN6_STRUCT */
  int sock;
#ifdef HAVE_POSIX_STYLE_SOCKET_PROTOTYPES
  socklen_t saddrlen;
#else /* HAVE_POSIX_STYLE_SOCKET_PROTOTYPES */
  int saddrlen;
#endif /* HAVE_POSIX_STYLE_SOCKET_PROTOTYPES */

  sock = ssh_stream_fd_get_readfd(stream);
  if (sock == -1)
    return FALSE;

  saddrlen = sizeof(saddr);
  if (getpeername(sock, (struct sockaddr *)&saddr, &saddrlen) < 0)
    return FALSE;

#ifdef HAVE_SOCKADDR_IN6_STRUCT
  if (saddr.sin6_family == AF_INET6)
    SSH_IP6_DECODE(&ip, saddr.sin6_addr.s6_addr);
  else
    SSH_INT_TO_IP4(&ip, htonl(((struct sockaddr_in*)&saddr)->sin_addr.s_addr));
  ssh_inet_convert_ip6_mapped_ip4_to_ip4(&ip);
  ssh_ipaddr_print(&ip, buf, buflen);
#else /* HAVE_SOCKADDR_IN6_STRUCT */
  strncpy(buf, inet_ntoa(saddr.sin_addr), buflen);
#endif /* HAVE_SOCKADDR_IN6_STRUCT */
  return TRUE;
}

/* Returns the remote port number, as a string.  This returns FALSE if the
   stream is not a socket stream or buffer space is insufficient. */

Boolean ssh_tcp_get_remote_port(SshStream stream, unsigned char *buf,
                                size_t buflen)
{
#ifdef HAVE_SOCKADDR_IN6_STRUCT
  struct sockaddr_in6 saddr;
#else /* HAVE_SOCKADDR_IN6_STRUCT */
  struct sockaddr_in saddr;
#endif /* HAVE_SOCKADDR_IN6_STRUCT */
  int sock;
#ifdef HAVE_POSIX_STYLE_SOCKET_PROTOTYPES
  socklen_t saddrlen;
#else /* HAVE_POSIX_STYLE_SOCKET_PROTOTYPES */
  int saddrlen;
#endif /* HAVE_POSIX_STYLE_SOCKET_PROTOTYPES */

  sock = ssh_stream_fd_get_readfd(stream);
  if (sock == -1)
    return FALSE;

  saddrlen = sizeof(saddr);
  if (getpeername(sock, (struct sockaddr *)&saddr, &saddrlen) < 0)
    return FALSE;

#ifdef HAVE_SOCKADDR_IN6_STRUCT
  /* XXX temporary casts until library API is changed XXX */
  ssh_snprintf(ssh_sstr(buf), buflen, "%u", ntohs(saddr.sin6_port));
#else /* HAVE_SOCKADDR_IN6_STRUCT */
  /* XXX temporary casts until library API is changed XXX */
  ssh_snprintf(ssh_sstr(buf), buflen, "%u", ntohs(saddr.sin_port));
#endif /* HAVE_SOCKADDR_IN6_STRUCT */
  return TRUE;
}

/* Returns the ip-address of the local host, as string.  This returns FALSE
   if the stream is not a socket stream or buffer space is insufficient. */
Boolean ssh_tcp_get_local_address(SshStream stream, unsigned char *buf,
                                  size_t buflen)
{
  SshIpAddrStruct ip;
#ifdef HAVE_SOCKADDR_IN6_STRUCT
  struct sockaddr_in6 saddr;
#else /* HAVE_SOCKADDR_IN6_STRUCT */
  struct sockaddr_in saddr;
#endif /* HAVE_SOCKADDR_IN6_STRUCT */
  int sock;
#ifdef HAVE_POSIX_STYLE_SOCKET_PROTOTYPES
  socklen_t saddrlen;
#else /* HAVE_POSIX_STYLE_SOCKET_PROTOTYPES */
  int saddrlen;
#endif /* HAVE_POSIX_STYLE_SOCKET_PROTOTYPES */

  sock = ssh_stream_fd_get_readfd(stream);
  if (sock == -1)
    return FALSE;

  saddrlen = sizeof(saddr);
  if (getsockname(sock, (struct sockaddr *)&saddr, &saddrlen) < 0)
    return FALSE;

#ifdef HAVE_SOCKADDR_IN6_STRUCT
  if (saddr.sin6_family == AF_INET6)
    SSH_IP6_DECODE(&ip, saddr.sin6_addr.s6_addr);
  else
    SSH_INT_TO_IP4(&ip, htonl(((struct sockaddr_in*)&saddr)->sin_addr.s_addr));
#else /* HAVE_SOCKADDR_IN6_STRUCT */
  SSH_INT_TO_IP4(&ip, htonl(((struct sockaddr_in*)&saddr)->sin_addr.s_addr));
#endif /* HAVE_SOCKADDR_IN6_STRUCT */
  ssh_inet_convert_ip6_mapped_ip4_to_ip4(&ip);
  ssh_ipaddr_print(&ip, buf, buflen);
  return TRUE;
}

/* Returns the local port number, as a string.  This returns FALSE if the
   stream is not a socket stream or buffer space is insufficient. */
Boolean ssh_tcp_get_local_port(SshStream stream, unsigned char *buf,
                               size_t buflen)
{
#ifdef HAVE_SOCKADDR_IN6_STRUCT
  struct sockaddr_in6 saddr;
#else /* HAVE_SOCKADDR_IN6_STRUCT */
  struct sockaddr_in saddr;
#endif /* HAVE_SOCKADDR_IN6_STRUCT */
  int sock;
#ifdef HAVE_POSIX_STYLE_SOCKET_PROTOTYPES
  socklen_t saddrlen;
#else /* HAVE_POSIX_STYLE_SOCKET_PROTOTYPES */
  int saddrlen;
#endif /* HAVE_POSIX_STYLE_SOCKET_PROTOTYPES */

  sock = ssh_stream_fd_get_readfd(stream);
  if (sock == -1)
    return FALSE;

  saddrlen = sizeof(saddr);
  if (getsockname(sock, (struct sockaddr *)&saddr, &saddrlen) < 0)
    return FALSE;

#ifdef HAVE_SOCKADDR_IN6_STRUCT
  if (saddr.sin6_family == AF_INET6)
    /* XXX temporary casts until library API is changed XXX */
    ssh_snprintf(ssh_sstr(buf), buflen, "%u", ntohs(saddr.sin6_port));
  else
    /* XXX temporary casts until library API is changed XXX */
    ssh_snprintf(ssh_sstr(buf), buflen, "%u",
                 ntohs(((struct sockaddr_in *) &saddr)->sin_port));
#else /* HAVE_SOCKADDR_IN6_STRUCT */
  /* XXX temporary casts until library API is changed XXX */
  ssh_snprintf(ssh_sstr(buf), buflen, "%u", ntohs(saddr.sin_port));
#endif /* HAVE_SOCKADDR_IN6_STRUCT */
  return TRUE;
}

/* Sets/resets TCP options TCP_NODELAY for the socket.  */

Boolean ssh_tcp_set_nodelay(SshStream stream, Boolean on)
{
#ifdef ENABLE_TCP_NODELAY
  int onoff = on, sock;

  sock = ssh_stream_fd_get_readfd(stream);
  if (sock == -1)
    return FALSE;

  return setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (void *)&onoff,
                    sizeof(onoff)) == 0;
#else /* ENABLE_TCP_NODELAY */
  return FALSE;
#endif /* ENABLE_TCP_NODELAY */
}

/* Sets/resets socket options SO_KEEPALIVE for the socket.  */

Boolean ssh_tcp_set_keepalive(SshStream stream, Boolean on)
{
  int onoff = on, sock;

  sock = ssh_stream_fd_get_readfd(stream);
  if (sock == -1)
    return FALSE;

#if defined (SOL_SOCKET) && defined (SO_KEEPALIVE)
  return setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, (void *)&onoff,
                    sizeof(onoff)) == 0;
#else /* defined (SOL_SOCKET) && defined (SO_KEEPALIVE) */
  return FALSE;
#endif /* defined (SOL_SOCKET) && defined (SO_KEEPALIVE) */
}

/* Sets/resets socket options SO_LINGER for the socket.  */

Boolean ssh_tcp_set_linger(SshStream stream, Boolean on)
{
#if defined (SOL_SOCKET) && defined (SO_LINGER)
  int sock;
  struct linger linger;

  linger.l_onoff = on ? 1 : 0;
  linger.l_linger = on ? 15 : 0;

  sock = ssh_stream_fd_get_readfd(stream);
  if (sock == -1)
    return FALSE;

  return setsockopt(sock, SOL_SOCKET, SO_LINGER, (void *)&linger,
                    sizeof(linger)) == 0;
#else /* defined (SOL_SOCKET) && defined (SO_LINGER) */
  return FALSE;
#endif /* defined (SOL_SOCKET) && defined (SO_LINGER) */
}

/* -------------- functions for name server lookups ------------------ */

/* Gets the name of the host we are running on.  To get the corresponding IP
   address(es), a name server lookup must be done using the functions below. */

void ssh_tcp_get_host_name(unsigned char *buf, size_t buflen)
{
#if !defined(HAVE_GETHOSTNAME) && defined(HAVE_UNAME)
  struct utsname uts;
#endif

#ifdef HAVE_GETHOSTNAME
  if (gethostname(ssh_sstr(buf), buflen) < 0)
    {
      ssh_debug("gethostname failed, buflen %u, errno %d", buflen, errno);
      strncpy(ssh_sstr(buf), "UNKNOWN", buflen);
    }
#else /* HAVE_GETHOSTNAME */
#ifdef HAVE_UNAME
  if (uname(&uts) < 0)
    {
      ssh_debug("uname failed: %s", strerror(errno));
      strncpy(buf, "UNKNOWN", buflen);
    }
  else
    strncpy(buf, uts.nodename, buflen);
#else /* HAVE_UNAME */
  strncpy(buf, "UNKNOWN", buflen);
#endif /* HAVE_UNAME */
#endif /* HAVE_GETHOSTNAME */
}

/* Looks up all ip-addresses of the host, returning them as a
   comma-separated list. The host name may already be an ip address,
   in which case it is returned directly. This is an simplification
   of function ssh_tcp_get_host_addrs_by_name for situations when
   the operation may block.

   The function returns NULL if the name can not be resolved. When the
   return value is non null, it is a pointer to a string allocated by
   this function, and must be freed by the caller when no longer
   needed. */
unsigned char *ssh_tcp_get_host_addrs_by_name_sync(const unsigned char *name)
{
#ifdef VXWORKS
  struct in_addr address;
  unsigned char outbuf[INET_ADDR_LEN+1];
  size_t outbuflen = 4;
#else /* VXWORKS */
  unsigned char *addresses, *tmp;
  size_t addr_len, addr_ptr;
  unsigned char outbuf[16];
  struct hostent *hp;
  size_t outbuflen = 16;
  SshIpAddrStruct ip;
  int i;
#ifdef HAVE_GETIPNODEBYNAME
  int error_num;
#endif /* HAVE_GETIPNODEBYNAME */
#endif /* VXWORKS */

  /* First check if it is already an ip address. */
  if (ssh_inet_strtobin(name, outbuf, &outbuflen))
    return ssh_strdup(name);

#ifdef VXWORKS
  address.s_addr = hostGetByName(name);
  if (address.s_addr == ERROR) return NULL;
  inet_ntoa_b(address, outbuf);
  return ssh_strdup(outbuf);
#else /* VXWORKS */

#ifdef HAVE_GETIPNODEBYNAME
  hp = getipnodebyname(ssh_csstr(name), AF_INET6,
                       AI_V4MAPPED | AI_ADDRCONFIG | AI_ALL,
                       &error_num);
  if (!hp)
    {
      /* This kludge needed for BSDI (getipnodebyname() returns NULL,
         if AF_INET6 and AI_ADDRCONFIG are specified in a system
         without IPv6 interfaces). */
      hp = getipnodebyname(ssh_csstr(name), AF_INET,
                           AI_V4MAPPED | AI_ADDRCONFIG | AI_ALL,
                           &error_num);
      if (!hp)
        return NULL;
    }

  if (!hp->h_addr_list[0])
    {
      freehostent(hp);
      return NULL;
    }
  outbuflen = 16;
#else /* HAVE_GETIPNODEBYNAME */
  /* Look up the host from the name servers. */
#ifdef HAVE_GETHOSTBYNAME2
#ifdef AF_INET6
  hp = gethostbyname2(name, AF_INET6);
#else /* AF_INET6 */
  hp = NULL;
#endif /* AF_INET6 */

  outbuflen = 16;
#else /* HAVE_GETHOSTBYNAME2 */
  hp = gethostbyname(name);
  outbuflen = 4;
#endif /* HAVE_GETHOSTBYNAME2 */
#endif /* HAVE_GETIPNODEBYNAME */


  /* Format the addresses into a comma-separated string. */
  addr_len = 64;
  if ((addresses = ssh_malloc(addr_len)) == NULL)
    {
#ifdef HAVE_GETIPNODEBYNAME
      freehostent(hp);
#endif /* HAVE_GETIPNODEBYNAME */
      return NULL;
    }

  addr_ptr = 0;
  addresses[addr_ptr] = '\0';
  if (hp && hp->h_addr_list[0])
    {
      for (i = 0; hp->h_addr_list[i]; i++)
        {
          if (outbuflen == 4)
            {
              SSH_IP4_DECODE(&ip, hp->h_addr_list[i]);
            }
          else
            {
              SSH_IP6_DECODE(&ip, hp->h_addr_list[i]);

              /*
                There is no point in keeping v4-only addresses in
                v6 form. RFC1884, section 2.4.4.
              */
              ssh_inet_convert_ip6_mapped_ip4_to_ip4(&ip);

              /* Following is ugly.  It however seems to be so, that in
                 certain systems some IPv4 addresses may get erroneously
                 mapped to IPV6 addresses.  I hope that this is a
                 temporary kludge.  Also we shouldn't look into the
                 internals of the SshIpAddrStruct (sshinet.h).  XXX */
              if (SSH_IP_IS6(&ip) &&
                  (ip.mask_len == 128) &&
                  (ip.addr_data[4] == 0x0) &&
                  (ip.addr_data[5] == 0x0) &&
                  (ip.addr_data[6] == 0x0) &&
                  (ip.addr_data[7] == 0x0) &&
                  (ip.addr_data[8] == 0x0) &&
                  (ip.addr_data[9] == 0x0) &&
                  (ip.addr_data[10] == 0x0) &&
                  (ip.addr_data[11] == 0x0) &&
                  (ip.addr_data[12] == 0x0) &&
                  (ip.addr_data[13] == 0x0) &&
                  (ip.addr_data[14] == 0x0) &&
                  (ip.addr_data[15] == 0x0))
                continue;
            }

          if (addr_len - addr_ptr < 40)
            {
              if ((tmp = ssh_realloc(addresses, addr_len, 2 * addr_len))
                  != NULL)
                {
                  addresses = tmp;
                  addr_len *= 2;
                }
              else
                {
#ifdef HAVE_GETIPNODEBYNAME
                  freehostent(hp);
#endif /* HAVE_GETIPNODEBYNAME */
                  ssh_free(addresses);
                  return NULL;
                }
            }

          if (addr_ptr > 0)
            {
              addresses[addr_ptr++] = ',';
              addresses[addr_ptr] = '\0';
            }
          ssh_ipaddr_print(&ip, addresses + addr_ptr, addr_len - addr_ptr);
          addr_ptr += strlen(ssh_csstr(addresses) + addr_ptr);
        }
    }

#ifdef HAVE_GETHOSTBYNAME2
  hp = gethostbyname2(name, AF_INET);
  outbuflen = 4;
  if (hp && hp->h_addr_list[0])
    {
      for (i = 0; hp->h_addr_list[i]; i++)
        {
          if (outbuflen == 4)
            SSH_IP4_DECODE(&ip, hp->h_addr_list[i]);
          else
            SSH_IP6_DECODE(&ip, hp->h_addr_list[i]);

          if (addr_len - addr_ptr < 40)
            {
              if ((tmp = ssh_realloc(addresses, addr_len, 2 * addr_len))
                  != NULL)
                {
                  addr_len *= 2;
                  addresses = tmp;
                }
              else
                {
                  ssh_free(addresses);
                  return NULL;
                }
            }

          if (addr_ptr > 0)
            {
              addresses[addr_ptr++] = ',';
              addresses[addr_ptr] = '\0';
            }
          ssh_ipaddr_print(&ip, addresses + addr_ptr, addr_len - addr_ptr);
          addr_ptr += strlen(addresses + addr_ptr);
        }
    }
#endif /* HAVE_GETHOSTBYNAME2 */

  if (addresses[0])
    {
# ifdef HAVE_GETIPNODEBYNAME
      freehostent(hp);
# endif /* HAVE_GETIPNODEBYNAME */
      return addresses;
    }
  else
    {
#ifdef HAVE_GETIPNODEBYNAME
      freehostent(hp);
#endif /* HAVE_GETIPNODEBYNAME */
      ssh_free(addresses);
      return NULL;
    }
#endif /* VXWORKS */
}

/* Looks up all ip-addresses of the host, returning them as a
   comma-separated list when calling the callback.  The host name may
   already be an ip address, in which case it is returned directly. */

SshOperationHandle ssh_tcp_get_host_addrs_by_name(const unsigned char *name,
                                                  SshLookupCallback callback,
                                                  void *context)
{
  unsigned char *addrs;

  addrs = ssh_tcp_get_host_addrs_by_name_sync(name);
  if (addrs)
    {
      (*callback)(SSH_TCP_OK, addrs, context);
      ssh_free(addrs);
    }
  else
    (*callback)(SSH_TCP_NO_ADDRESS, NULL, context);
  return NULL;
}


/* Looks up the name of the host by its ip-address.  Verifies that the
   address returned by the name servers also has the original ip
   address. This is an simplification of function
   ssh_tcp_get_host_by_addr for situations when the operation may
   block.

   Function returns NULL, if the reverse lookup fails for some reason,
   or pointer to dynamically allocated memory containing the host
   name.  The memory should be deallocated by the caller when no
   longer needed.  */

unsigned char *ssh_tcp_get_host_by_addr_sync(const unsigned char *addr)
{
#if defined (HAVE_GETIPNODEBYADDR) && defined (HAVE_GETIPNODEBYNAME)
  struct hostent *hp;
  unsigned char outbuf[16];
  size_t outbuflen = 16;
  int error_num;
  unsigned char *name;
  int i;

  if (!ssh_inet_strtobin(addr, outbuf, &outbuflen))
    return NULL;

  hp = getipnodebyaddr(outbuf, outbuflen,
                       (outbuflen == 16) ? AF_INET6 : AF_INET,
                       &error_num);
  if (!hp)
    return NULL;

  name = ssh_strdup(hp->h_name);
  freehostent(hp);

  if (name == NULL)
    return NULL;

  /* Map it back to an IP address and check that the given address
     actually is an address of this host.  This is necessary because
     anyone with access to a name server can define arbitrary names
     for an IP address.  Mapping from name to IP address can be
     trusted better (but can still be fooled if the intruder has
     access to the name server of the domain). */
  hp = getipnodebyname(ssh_csstr(name), (outbuflen == 16) ? AF_INET6 : AF_INET,
                       AI_V4MAPPED | AI_ADDRCONFIG | AI_ALL,
                       &error_num);
  if (!hp)
    {
      ssh_free(name);
      return NULL;
    }

  /* Look for the address from the list of addresses. */
  for (i = 0; hp->h_addr_list[i]; i++)
    if (memcmp(hp->h_addr_list[i], outbuf, outbuflen) == 0)
      break;
  /* If we reached the end of the list, the address was not there. */
  if (!hp->h_addr_list[i])
    {
      freehostent(hp);
      ssh_free(name);
      return NULL;
    }

  freehostent(hp);
  /* Address was found for the host name.  We accept the host name. */
  return name;
#else /* defined (HAVE_GETIPNODEBYADDR) && defined (HAVE_GETIPNODEBYNAME) */
#ifdef VXWORKS
  char name[MAXHOSTNAMELEN+1];
  size_t outbuflen = 4; /* IPv4 only in VxWorks */
  unsigned char outbuf[16];
  int address, address_2;

  if (!ssh_inet_strtobin(addr, outbuf, &outbuflen))
    return NULL;

  if (outbuflen!=4)
    return NULL; /* IPv4 only in VxWorks */

  memmove(&address, outbuf, outbuflen);
  if (hostGetByAddr(address, name) == ERROR) return NULL;

  /* Map it back to an IP address and check that the given address
     actually is an address of this host.  This is necessary because
     anyone with access to a name server can define arbitrary names
     for an IP address.  Mapping from name to IP address can be
     trusted better (but can still be fooled if the intruder has
     access to the name server of the domain). */
  address_2 = hostGetByName(name);
  if (address != address_2) return NULL;

  /* Address was found for the host name.  We accept the host name. */
  return ssh_strdup(name);

#else /* VXWORKS */
  unsigned char outbuf[16];
  size_t outbuflen = 16;
  struct in_addr in_addr;
  struct hostent *hp;
  char *name;
  int i;

  if (!ssh_inet_strtobin(addr, outbuf, &outbuflen))
    return NULL;

  memmove(&in_addr.s_addr, outbuf, outbuflen);
  hp = gethostbyaddr((char *)&in_addr, sizeof(struct in_addr), AF_INET);
  if (!hp)
    return NULL;

  /* Got host name. */
  if ((name = ssh_strdup(hp->h_name)) == NULL)
    return NULL;

  /* Map it back to an IP address and check that the given address
     actually is an address of this host.  This is necessary because
     anyone with access to a name server can define arbitrary names
     for an IP address.  Mapping from name to IP address can be
     trusted better (but can still be fooled if the intruder has
     access to the name server of the domain). */
  hp = gethostbyname(name);
  if (!hp)
    {
      ssh_free(name);
      return NULL;
    }

  /* Look for the address from the list of addresses. */
  for (i = 0; hp->h_addr_list[i]; i++)
    if (memcmp(hp->h_addr_list[i], &in_addr, sizeof(in_addr)) == 0)
      break;
  /* If we reached the end of the list, the address was not there. */
  if (!hp->h_addr_list[i])
    {
      ssh_free(name);
      return NULL;
    }

  /* Address was found for the host name.  We accept the host name. */
  return name;
#endif /* VXWORKS */
#endif /* defined (HAVE_GETIPNODEBYADDR) && defined (HAVE_GETIPNODEBYNAME) */
}

/* Looks up the name of the host by its ip-address.  Verifies that the
   address returned by the name servers also has the original ip address.
   Calls the callback with either error or success.  The callback should
   copy the returned name. */

SshOperationHandle ssh_tcp_get_host_by_addr(const unsigned char *addr,
                                            SshLookupCallback callback,
                                            void *context)
{
  unsigned char *name;

  name = ssh_tcp_get_host_by_addr_sync(addr);
  if (name)
    {
      (*callback)(SSH_TCP_OK, name, context);
      ssh_free(name);
    }
  else
    (*callback)(SSH_TCP_NO_ADDRESS, NULL, context);
  return NULL;
}

/* Looks up the service (port number) by name and protocol.  `protocol' must
   be either "tcp" or "udp".  Returns -1 if the service could not be found. */

int ssh_inet_get_port_by_service(const unsigned char *name,
                                 const unsigned char *proto)
{
#ifdef HAVE_GETSERVBYNAME
  struct servent *se;
  int port;
#endif /* HAVE_GETSERVBYNAME */
  const unsigned char *cp;

  for (cp = name; isdigit(*cp); cp++)
    ;
  if (!*cp && *name)
    return atoi(ssh_csstr(name));
#ifdef HAVE_GETSERVBYNAME
  se = getservbyname(ssh_csstr(name), ssh_csstr(proto));
  if (!se)
    return -1;
  port = ntohs(se->s_port);
#ifdef HAVE_ENDSERVENT
  endservent();
#endif /* HAVE_ENDSERVENT */
  return port;
#else  /* HAVE_GETSERVBYNAME */
#ifdef WANT_SERVBYNAME
    {
      struct SshServent const *se;
      se = ssh_getserv(name, 0, TRUE, proto);
      return (se == NULL)? -1 : se->s_port;
    }
#else /* WANT_SERVBYNAME */
  return -1;
#endif /* WANT_SERVBYNAME */
#endif /* HAVE_GETSERVBYNAME */
}

/* Looks up the name of the service based on port number and protocol.
   `protocol' must be either "tcp" or "udp".  The name is stored in the
   given buffer; is the service is not found, the port number is stored
   instead (without the protocol specification).  The name will be
   truncated if it is too long. */

void ssh_inet_get_service_by_port(unsigned int port,
                                  const unsigned char *proto,
                                  unsigned char *buf, size_t buflen)
{
#ifdef HAVE_GETSERVBYPORT
  struct servent *se;

  se = getservbyport(htons(port), ssh_csstr(proto));
  if (!se)
    /* XXX temporary casts until library API is changed XXX */
    ssh_snprintf(ssh_sstr(buf), buflen, "%u", port);
  else
    strncpy(ssh_sstr(buf), se->s_name, buflen);
#ifdef HAVE_ENDSERVENT
  endservent();
#endif /* HAVE_ENDSERVENT */
#else /* HAVE_GETSERVBYPORT */
#ifdef WANT_SERVBYPORT
    {
      struct SshServent const *se;
      se = ssh_getserv(NULL, port, FALSE, proto);
      if (se)
        if (se->s_aliases[0] != NULL)
          {
            strncpy(buf, se->s_aliases[0], buflen);
            return;
          }
      /* XXX temporary casts until library API is changed XXX */
      ssh_snprintf(ssh_sstr(buf), buflen, "%u", port);
    }
#else /* WANT_SERVBY_PORT */
    /* XXX temporary casts until library API is changed XXX */
    ssh_snprintf(ssh_sstr(buf), buflen, "%u", port);
#endif /* WANT_SERVBY_PORT */
#endif /* HAVE_GETSERVBYPORT */
}

/* --------------------- auxiliary functions -------------------------*/



/* Compares two port number addresses, and returns <0 if port1 is smaller,
   0 if they denote the same number (though possibly written differently),
   and >0 if port2 is smaller.  The result is zero if either address is
   invalid. */
int ssh_inet_port_number_compare(const unsigned char *port1,
                                 const unsigned char *port2,
                                 const unsigned char *proto)
{
  int nport1, nport2;

  nport1 = ssh_inet_get_port_by_service(port1, proto);
  nport2 = ssh_inet_get_port_by_service(port2, proto);

  if (nport1 == -1 || nport2 == -1)
    return 0;
  if (nport1 == nport2)
    return 0;
  else
    if (nport1 < nport2)
      return -1;
    else
      return 1;
}
