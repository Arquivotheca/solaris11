/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil -*- */

/*
 * Copyright (c) 2002, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * kdc/network.c
 *
 * Copyright 1990,2000,2007,2008,2009 by the Massachusetts Institute of Technology.
 *
 * Export of this software from the United States of America may
 *   require a specific license from the United States Government.
 *   It is the responsibility of any person or organization contemplating
 *   export to obtain such a license before exporting.
 *
 * WITHIN THAT CONSTRAINT, permission to use, copy, modify, and
 * distribute this software and its documentation for any purpose and
 * without fee is hereby granted, provided that the above copyright
 * notice appear in all copies and that both that copyright notice and
 * this permission notice appear in supporting documentation, and that
 * the name of M.I.T. not be used in advertising or publicity pertaining
 * to distribution of the software without specific, written prior
 * permission.  Furthermore if you modify this software you must label
 * your software as modified software and not distribute it in such a
 * fashion that it might be confused with the original M.I.T. software.
 * M.I.T. makes no representations about the suitability of
 * this software for any purpose.  It is provided "as is" without express
 * or implied warranty.
 *
 *
 * Network code for Kerberos v5 KDC.
 */

#include "k5-int.h"
#include "kdc_util.h"
#include "extern.h"
#include "kdc5_err.h"
#include "adm_proto.h"
#include <sys/ioctl.h>
#include <syslog.h>

#include <stddef.h>
#include <ctype.h>
#include "port-sockets.h"
#include "socket-utils.h"

#ifdef HAVE_NETINET_IN_H
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#ifdef HAVE_SYS_SOCKIO_H
/* for SIOCGIFCONF, etc. */
#include <sys/sockio.h>
#endif
#include <sys/time.h>
/* Solaris Kerberos */
#include <libintl.h>

#if HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif
#include <arpa/inet.h>

#ifndef ARPHRD_ETHER /* OpenBSD breaks on multiple inclusions */
#include <net/if.h>
#endif

#ifdef HAVE_SYS_FILIO_H
#include <sys/filio.h>          /* FIONBIO */
#endif

#include "fake-addrinfo.h"

/* Misc utility routines.  */
static void
set_sa_port(struct sockaddr *addr, int port)
{
    switch (addr->sa_family) {
    case AF_INET:
        sa2sin(addr)->sin_port = port;
        break;
#ifdef KRB5_USE_INET6
    case AF_INET6:
        sa2sin6(addr)->sin6_port = port;
        break;
#endif
    default:
        break;
    }
}

static int ipv6_enabled()
{
#ifdef KRB5_USE_INET6
    static int result = -1;
    if (result == -1) {
        int s;
        s = socket(AF_INET6, SOCK_STREAM, 0);
        if (s >= 0) {
            result = 1;
            close(s);
        } else
            result = 0;
    }
    return result;
#else
    return 0;
#endif
}

static int
setreuseaddr(int sock, int value)
{
    return setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &value, sizeof(value));
}

#if defined(KRB5_USE_INET6) && defined(IPV6_V6ONLY)
static int
setv6only(int sock, int value)
{
    return setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, &value, sizeof(value));
}
#endif

/* Use RFC 3542 API below, but fall back from IPV6_RECVPKTINFO to
   IPV6_PKTINFO for RFC 2292 implementations.  */
#ifndef IPV6_RECVPKTINFO
#define IPV6_RECVPKTINFO IPV6_PKTINFO
#endif
/* Parallel, though not standardized.  */
#ifndef IP_RECVPKTINFO
#define IP_RECVPKTINFO IP_PKTINFO
#endif

static int
set_pktinfo(int sock, int family)
{
    int sockopt = 1;
    int option = 0, proto = 0;

    switch (family) {
#if defined(IP_PKTINFO) && defined(HAVE_STRUCT_IN_PKTINFO)
    case AF_INET:
        proto = IPPROTO_IP;
        option = IP_RECVPKTINFO;
        break;
#endif
#if defined(IPV6_PKTINFO) && defined(HAVE_STRUCT_IN6_PKTINFO)
    case AF_INET6:
        proto = IPPROTO_IPV6;
        option = IPV6_RECVPKTINFO;
        break;
#endif
    default:
        return EINVAL;
    }
    if (setsockopt(sock, proto, option, &sockopt, sizeof(sockopt)))
        return errno;
    return 0;
}


static const char *paddr (struct sockaddr *sa)
{
    static char buf[100];
    char portbuf[10];
    if (getnameinfo(sa, socklen(sa),
                    buf, sizeof(buf), portbuf, sizeof(portbuf),
                    NI_NUMERICHOST|NI_NUMERICSERV))
        strlcpy(buf, "<unprintable>", sizeof(buf));
    else {
        unsigned int len = sizeof(buf) - strlen(buf);
        char *p = buf + strlen(buf);
        if (len > 2+strlen(portbuf)) {
            *p++ = '.';
            len--;
            strncpy(p, portbuf, len);
        }
    }
    return buf;
}

/* KDC data.  */

enum conn_type {
    CONN_UDP, CONN_UDP_PKTINFO, CONN_TCP_LISTENER, CONN_TCP,
    CONN_ROUTING
};

/* Per-connection info.  */
struct connection {
    int fd;
    enum conn_type type;
    void (*service)(struct connection *, int);
    union {
        /* Type-specific information.  */
        struct {
            /* connection */
            struct sockaddr_storage addr_s;
            socklen_t addrlen;
            char addrbuf[56];
            krb5_fulladdr faddr;
            krb5_address kaddr;
            /* incoming */
            size_t bufsiz;
            size_t offset;
            char *buffer;
            size_t msglen;
            /* outgoing */
            krb5_data *response;
            unsigned char lenbuf[4];
            sg_buf sgbuf[2];
            sg_buf *sgp;
            int sgnum;
            /* crude denial-of-service avoidance support */
            time_t start_time;
        } tcp;
    } u;
};


#define SET(TYPE) struct { TYPE *data; int n, max; }

/* Start at the top and work down -- this should allow for deletions
   without disrupting the iteration, since we delete by overwriting
   the element to be removed with the last element.  */
#define FOREACH_ELT(set,idx,vvar)                                       \
    for (idx = set.n-1; idx >= 0 && (vvar = set.data[idx], 1); idx--)

#define GROW_SET(set, incr, tmpptr)                                     \
    (((int)(set.max + incr) < set.max                                   \
      || (((size_t)((int)(set.max + incr) * sizeof(set.data[0]))        \
           / sizeof(set.data[0]))                                       \
          != (set.max + incr)))                                         \
     ? 0                          /* overflow */                        \
     : ((tmpptr = realloc(set.data,                                     \
                          (int)(set.max + incr) * sizeof(set.data[0]))) \
        ? (set.data = tmpptr, set.max += incr, 1)                       \
        : 0))

/* 1 = success, 0 = failure */
#define ADD(set, val, tmpptr)                           \
    ((set.n < set.max || GROW_SET(set, 10, tmpptr))     \
     ? (set.data[set.n++] = val, 1)                     \
     : 0)

#define DEL(set, idx)                           \
    (set.data[idx] = set.data[--set.n], 0)

#define FREE_SET_DATA(set)                                      \
    (free(set.data), set.data = 0, set.max = 0, set.n = 0)


/* Set<struct connection *> connections; */
static SET(struct connection *) connections;
#define n_sockets       connections.n
#define conns           connections.data

/* Set<u_short> udp_port_data, tcp_port_data; */
/*
 * N.B.: The Emacs cc-mode indentation code seems to get confused if
 * the macro argument here is one word only.  So use "unsigned short"
 * instead of the "u_short" we were using before.
 */
static SET(unsigned short) udp_port_data, tcp_port_data;

#include "cm.h"

static struct select_state sstate;

static krb5_error_code add_udp_port(int port)
{
    int i;
    void *tmp;
    u_short val;
    u_short s_port = port;

    if (s_port != port)
        return EINVAL;

    FOREACH_ELT (udp_port_data, i, val)
        if (s_port == val)
            return 0;
    if (!ADD(udp_port_data, s_port, tmp))
        return ENOMEM;
    return 0;
}

static krb5_error_code add_tcp_port(int port)
{
    int i;
    void *tmp;
    u_short val;
    u_short s_port = port;

    if (s_port != port)
        return EINVAL;

    FOREACH_ELT (tcp_port_data, i, val)
        if (s_port == val)
            return 0;
    if (!ADD(tcp_port_data, s_port, tmp))
        return ENOMEM;
    return 0;
}


#define USE_AF AF_INET
#define USE_TYPE SOCK_DGRAM
#define USE_PROTO 0
#define SOCKET_ERRNO errno
#include "foreachaddr.h"

struct socksetup {
    krb5_error_code retval;
    int udp_flags;
#define UDP_DO_IPV4 1
#define UDP_DO_IPV6 2
};

static struct connection *
add_fd (struct socksetup *data, int sock, enum conn_type conntype,
        void (*service)(struct connection *, int))
{
    struct connection *newconn;
    void *tmp;

#ifndef _WIN32
    if (sock >= FD_SETSIZE) {
        data->retval = EMFILE;  /* XXX */
        kdc_err(NULL, 0, "file descriptor number %d too high", sock);
        return 0;
    }
#endif
    newconn = malloc(sizeof(*newconn));
    if (newconn == 0) {
        data->retval = ENOMEM;
        kdc_err(NULL, ENOMEM,
                gettext("cannot allocate storage for connection info"));
        return 0;
    }
    if (!ADD(connections, newconn, tmp)) {
        data->retval = ENOMEM;
        kdc_err(NULL, ENOMEM, gettext("cannot save socket info"));
        free(newconn);
        return 0;
    }

    memset(newconn, 0, sizeof(*newconn));
    newconn->type = conntype;
    newconn->fd = sock;
    newconn->service = service;
    return newconn;
}

static void process_packet(struct connection *, int);
static void accept_tcp_connection(struct connection *, int);
static void process_tcp_connection(struct connection *, int);

static struct connection *
add_udp_fd (struct socksetup *data, int sock, int pktinfo)
{
    return add_fd(data, sock, pktinfo ? CONN_UDP_PKTINFO : CONN_UDP,
                  process_packet);
}

static struct connection *
add_tcp_listener_fd (struct socksetup *data, int sock)
{
    return add_fd(data, sock, CONN_TCP_LISTENER, accept_tcp_connection);
}

static struct connection *
add_tcp_data_fd (struct socksetup *data, int sock)
{
    return add_fd(data, sock, CONN_TCP, process_tcp_connection);
}

static void
delete_fd (struct connection *xconn)
{
    struct connection *conn;
    int i;

    FOREACH_ELT(connections, i, conn)
        if (conn == xconn) {
            DEL(connections, i);
            break;
        }
    free(xconn);
}

static const int one = 1;

static int
setnbio(int sock)
{
    return ioctlsocket(sock, FIONBIO, (const void *)&one);
}

static int
setkeepalive(int sock)
{
    return setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one));
}

static int
setnolinger(int s)
{
    static const struct linger ling = { 0, 0 };
    return setsockopt(s, SOL_SOCKET, SO_LINGER, &ling, sizeof(ling));
}

/* Returns -1 or socket fd.  */
static int
setup_a_tcp_listener(struct socksetup *data, struct sockaddr *addr)
{
    int sock;

    sock = socket(addr->sa_family, SOCK_STREAM, 0);
    if (sock == -1) {
        kdc_err(NULL, errno, 
                gettext("Cannot create TCP server socket on %s"),
                paddr(addr));
        return -1;
    }
    set_cloexec_fd(sock);
#ifndef _WIN32
    if (sock >= FD_SETSIZE) {
        close(sock);
        kdc_err(NULL, 0, "TCP socket fd number %d (for %s) too high",
                sock, paddr(addr));
        return -1;
    }
#endif
    if (setreuseaddr(sock, 1) < 0)
        kdc_err(NULL, errno,
                gettext("enabling SO_REUSEADDR on TCP socket"));
#ifdef KRB5_USE_INET6
    if (addr->sa_family == AF_INET6) {
#ifdef IPV6_V6ONLY
        if (setv6only(sock, 1))
            kdc_err(NULL, errno, "setsockopt(%d,IPV6_V6ONLY,1) failed", sock);
        else
            kdc_err(NULL, 0, "setsockopt(%d,IPV6_V6ONLY,1) worked", sock);
#else
        krb5_klog_syslog(LOG_INFO, "no IPV6_V6ONLY socket option support");
#endif /* IPV6_V6ONLY */
    }
#endif /* KRB5_USE_INET6 */
    if (bind(sock, addr, socklen(addr)) == -1) {
        kdc_err(NULL, errno,
                gettext("Cannot bind TCP server socket on %s"), paddr(addr));
        close(sock);
        return -1;
    }
    if (listen(sock, 5) < 0) {
        kdc_err(NULL, errno, 
                gettext("Cannot listen on TCP server socket on %s"),
                paddr(addr));
        close(sock);
        return -1;
    }
    if (setnbio(sock)) {
        kdc_err(NULL, errno,
                gettext("cannot set listening tcp socket on %s non-blocking"),
                paddr(addr));
        close(sock);
        return -1;
    }
    if (setnolinger(sock)) {
        kdc_err(NULL, errno, 
                gettext("disabling SO_LINGER on TCP socket on %s"),
                paddr(addr));
        close(sock);
        return -1;
    }
    return sock;
}

static int
setup_tcp_listener_ports(struct socksetup *data)
{
    struct sockaddr_in sin4;
#ifdef KRB5_USE_INET6
    struct sockaddr_in6 sin6;
#endif
    int i, port;

    memset(&sin4, 0, sizeof(sin4));
    sin4.sin_family = AF_INET;
#ifdef HAVE_SA_LEN
    sin4.sin_len = sizeof(sin4);
#endif
    sin4.sin_addr.s_addr = INADDR_ANY;

#ifdef KRB5_USE_INET6
    memset(&sin6, 0, sizeof(sin6));
    sin6.sin6_family = AF_INET6;
#ifdef SIN6_LEN
    sin6.sin6_len = sizeof(sin6);
#endif
    sin6.sin6_addr = in6addr_any;
#endif

    FOREACH_ELT (tcp_port_data, i, port) {
        int s4, s6;

        set_sa_port((struct sockaddr *)&sin4, htons(port));
        if (!ipv6_enabled()) {
            s4 = setup_a_tcp_listener(data, (struct sockaddr *)&sin4);
            if (s4 < 0)
                return -1;
            s6 = -1;
        } else {
#ifndef KRB5_USE_INET6
            abort();
#else
            s4 = s6 = -1;

            set_sa_port((struct sockaddr *)&sin6, htons(port));

            s6 = setup_a_tcp_listener(data, (struct sockaddr *)&sin6);
            if (s6 < 0)
                return -1;

            s4 = setup_a_tcp_listener(data, (struct sockaddr *)&sin4);
#endif /* KRB5_USE_INET6 */
        }

        /* Sockets are created, prepare to listen on them.  */
        if (s4 >= 0) {
            if (add_tcp_listener_fd(data, s4) == NULL)
                close(s4);
            else {
                FD_SET(s4, &sstate.rfds);
                if (s4 >= sstate.max)
                    sstate.max = s4 + 1;
                krb5_klog_syslog(LOG_INFO, "listening on fd %d: tcp %s",
                                 s4, paddr((struct sockaddr *)&sin4));
            }
        }
#ifdef KRB5_USE_INET6
        if (s6 >= 0) {
            if (add_tcp_listener_fd(data, s6) == NULL) {
                close(s6);
                s6 = -1;
            } else {
                FD_SET(s6, &sstate.rfds);
                if (s6 >= sstate.max)
                    sstate.max = s6 + 1;
                krb5_klog_syslog(LOG_INFO, "listening on fd %d: tcp %s",
                                 s6, paddr((struct sockaddr *)&sin6));
            }
            if (s4 < 0)
                krb5_klog_syslog(LOG_INFO,
                                 "assuming IPv6 socket accepts IPv4");
        }
#endif
    }
    return 0;
}

#if defined(CMSG_SPACE) && defined(HAVE_STRUCT_CMSGHDR) && (defined(IP_PKTINFO) || defined(IPV6_PKTINFO))
union pktinfo {
#ifdef HAVE_STRUCT_IN6_PKTINFO
    struct in6_pktinfo pi6;
#endif
#ifdef HAVE_STRUCT_IN_PKTINFO
    struct in_pktinfo pi4;
#endif
    char c;
};

static int
setup_udp_port_1(struct socksetup *data, struct sockaddr *addr,
                 char *haddrbuf, int pktinfo);

static void
setup_udp_pktinfo_ports(struct socksetup *data)
{
#ifdef IP_PKTINFO
    {
        struct sockaddr_in sa;
        int r;

        memset(&sa, 0, sizeof(sa));
        sa.sin_family = AF_INET;
#ifdef HAVE_SA_LEN
        sa.sin_len = sizeof(sa);
#endif
        r = setup_udp_port_1(data, (struct sockaddr *) &sa, "0.0.0.0", 4);
        if (r == 0)
            data->udp_flags &= ~UDP_DO_IPV4;
    }
#endif
#ifdef IPV6_PKTINFO
    {
        struct sockaddr_in6 sa;
        int r;

        memset(&sa, 0, sizeof(sa));
        sa.sin6_family = AF_INET6;
#ifdef HAVE_SA_LEN
        sa.sin6_len = sizeof(sa);
#endif
        r = setup_udp_port_1(data, (struct sockaddr *) &sa, "::", 6);
        if (r == 0)
            data->udp_flags &= ~UDP_DO_IPV6;
    }
#endif
}
#else /* no pktinfo compile-time support */
static void
setup_udp_pktinfo_ports(struct socksetup *data)
{
}
#endif

static int
setup_udp_port_1(struct socksetup *data, struct sockaddr *addr,
                 char *haddrbuf, int pktinfo)
{
    int sock = -1, i, r;
    u_short port;

    FOREACH_ELT (udp_port_data, i, port) {
        sock = socket (addr->sa_family, SOCK_DGRAM, 0);
        if (sock == -1) {
            data->retval = errno;
            kdc_err(NULL, data->retval,
                    "Cannot create server socket for port %d address %s",
                    port, haddrbuf);
            return 1;
        }
        set_cloexec_fd(sock);
#ifdef KRB5_USE_INET6
        if (addr->sa_family == AF_INET6) {
#ifdef IPV6_V6ONLY
            if (setv6only(sock, 1))
                kdc_err(NULL, errno, gettext("setsockopt(%d,IPV6_V6ONLY,1) failed"),
                        sock);
            else
                kdc_err(NULL, 0, gettext("setsockopt(%d,IPV6_V6ONLY,1) worked"), sock);
#else
            krb5_klog_syslog(LOG_INFO, "no IPV6_V6ONLY socket option support");
#endif /* IPV6_V6ONLY */
        }
#endif
        set_sa_port(addr, htons(port));
        if (bind (sock, (struct sockaddr *)addr, socklen (addr)) == -1) {
            data->retval = errno;
            kdc_err(NULL, data->retval,
                    gettext("Cannot bind server socket to port %d address %s"),
                    port, haddrbuf);
            close(sock);
            return 1;
        }
#if !(defined(CMSG_SPACE) && defined(HAVE_STRUCT_CMSGHDR) && (defined(IP_PKTINFO) || defined(IPV6_PKTINFO)))
        assert(pktinfo == 0);
#endif
        if (pktinfo) {
            r = set_pktinfo(sock, addr->sa_family);
            if (r) {
                kdc_err(NULL, r,
                        gettext("Cannot request packet info for udp socket address %s port %d"),
                        haddrbuf, port);
                close(sock);
                return 1;
            }
        }
        krb5_klog_syslog (LOG_INFO, "listening on fd %d: udp %s%s", sock,
                          paddr((struct sockaddr *)addr),
                          pktinfo ? " (pktinfo)" : "");
        if (add_udp_fd (data, sock, pktinfo) == 0) {
            close(sock);
            return 1;
        }
        FD_SET (sock, &sstate.rfds);
        if (sock >= sstate.max)
            sstate.max = sock + 1;
    }
    return 0;
}

static int
setup_udp_port(void *P_data, struct sockaddr *addr)
{
    struct socksetup *data = P_data;
    char haddrbuf[NI_MAXHOST];
    int err;

    if (addr->sa_family == AF_INET && !(data->udp_flags & UDP_DO_IPV4))
        return 0;
#ifdef AF_INET6
    if (addr->sa_family == AF_INET6 && !(data->udp_flags & UDP_DO_IPV6))
        return 0;
#endif
    err = getnameinfo(addr, socklen(addr), haddrbuf, sizeof(haddrbuf),
                      0, 0, NI_NUMERICHOST);
    if (err)
        strlcpy(haddrbuf, "<unprintable>", sizeof(haddrbuf));

    switch (addr->sa_family) {
    case AF_INET:
        break;
#ifdef AF_INET6
    case AF_INET6:
#ifdef KRB5_USE_INET6
        break;
#else
        {
            static int first = 1;
            if (first) {
                krb5_klog_syslog (LOG_INFO, "skipping local ipv6 addresses");
                first = 0;
            }
            return 0;
        }
#endif
#endif
#ifdef AF_LINK /* some BSD systems, AIX */
    case AF_LINK:
        return 0;
#endif
#ifdef AF_DLI /* Direct Link Interface - DEC Ultrix/OSF1 link layer? */
    case AF_DLI:
        return 0;
#endif
#ifdef AF_APPLETALK
    case AF_APPLETALK:
        return 0;
#endif
    default:
        krb5_klog_syslog (LOG_INFO,
                          "skipping unrecognized local address family %d",
                          addr->sa_family);
        return 0;
    }
    return setup_udp_port_1(data, addr, haddrbuf, 0);
}

#if 1
static void klog_handler(const void *data, size_t len)
{
    static char buf[BUFSIZ];
    static int bufoffset;
    void *p;

#define flush_buf()                             \
    (bufoffset                                  \
     ? (((buf[0] == 0 || buf[0] == '\n')        \
         ? (fork()==0?abort():(void)0)          \
         : (void)0),                            \
        krb5_klog_syslog(LOG_INFO, "%s", buf),  \
        memset(buf, 0, sizeof(buf)),            \
        bufoffset = 0)                          \
     : 0)

    p = memchr(data, 0, len);
    if (p)
        len = (const char *)p - (const char *)data;
scan_for_newlines:
    if (len == 0)
        return;
    p = memchr(data, '\n', len);
    if (p) {
        if (p != data)
            klog_handler(data, (size_t)((const char *)p - (const char *)data));
        flush_buf();
        len -= ((const char *)p - (const char *)data) + 1;
        data = 1 + (const char *)p;
        goto scan_for_newlines;
    } else if (len > sizeof(buf) - 1 || len + bufoffset > sizeof(buf) - 1) {
        size_t x = sizeof(buf) - len - 1;
        klog_handler(data, x);
        flush_buf();
        len -= x;
        data = (const char *)data + x;
        goto scan_for_newlines;
    } else {
        memcpy(buf + bufoffset, data, len);
        bufoffset += len;
    }
}
#endif

static int network_reconfiguration_needed = 0;

#ifdef HAVE_STRUCT_RT_MSGHDR
#include <net/route.h>

static char *rtm_type_name(int type)
{
    switch (type) {
    case RTM_ADD: return "RTM_ADD";
    case RTM_DELETE: return "RTM_DELETE";
    case RTM_NEWADDR: return "RTM_NEWADDR";
    case RTM_DELADDR: return "RTM_DELADDR";
    case RTM_IFINFO: return "RTM_IFINFO";
    case RTM_OLDADD: return "RTM_OLDADD";
    case RTM_OLDDEL: return "RTM_OLDDEL";
    case RTM_RESOLVE: return "RTM_RESOLVE";
#ifdef RTM_NEWMADDR
    case RTM_NEWMADDR: return "RTM_NEWMADDR";
    case RTM_DELMADDR: return "RTM_DELMADDR";
#endif
    case RTM_MISS: return "RTM_MISS";
    case RTM_REDIRECT: return "RTM_REDIRECT";
    case RTM_LOSING: return "RTM_LOSING";
    case RTM_GET: return "RTM_GET";
    default: return "?";
    }
}

static void process_routing_update(struct connection *conn, int selflags)
{
    int n_read;
    struct rt_msghdr rtm;

    while ((n_read = read(conn->fd, &rtm, sizeof(rtm))) > 0) {
        if (n_read < sizeof(rtm)) {
            /* Quick hack to figure out if the interesting
               fields are present in a short read.

               A short read seems to be normal for some message types.
               Only complain if we don't have the critical initial
               header fields.  */
#define RS(FIELD) (offsetof(struct rt_msghdr, FIELD) + sizeof(rtm.FIELD))
            if (n_read < RS(rtm_type) ||
                n_read < RS(rtm_version) ||
                n_read < RS(rtm_msglen)) {
                krb5_klog_syslog(LOG_ERR,
                                 "short read (%d/%d) from routing socket",
                                 n_read, (int) sizeof(rtm));
                return;
            }
        }
#if 0
        krb5_klog_syslog(LOG_INFO,
                         "got routing msg type %d(%s) v%d",
                         rtm.rtm_type, rtm_type_name(rtm.rtm_type),
                         rtm.rtm_version);
#endif
        if (rtm.rtm_msglen > sizeof(rtm)) {
            /* It appears we get a partial message and the rest is
               thrown away?  */
        } else if (rtm.rtm_msglen != n_read) {
            krb5_klog_syslog(LOG_ERR,
                             "read %d from routing socket but msglen is %d",
                             n_read, rtm.rtm_msglen);
        }
        switch (rtm.rtm_type) {
        case RTM_ADD:
        case RTM_DELETE:
        case RTM_NEWADDR:
        case RTM_DELADDR:
        case RTM_IFINFO:
        case RTM_OLDADD:
        case RTM_OLDDEL:
            /*
             * Some flags indicate routing table updates that don't
             * indicate local address changes.  They may come from
             * redirects, or ARP, etc.
             *
             * This set of symbols is just an initial guess based on
             * some messages observed in real life; working out which
             * other flags also indicate messages we should ignore,
             * and which flags are portable to all system and thus
             * don't need to be conditionalized, is left as a future
             * exercise.
             */
#ifdef RTF_DYNAMIC
            if (rtm.rtm_flags & RTF_DYNAMIC)
                break;
#endif
#ifdef RTF_CLONED
            if (rtm.rtm_flags & RTF_CLONED)
                break;
#endif
#ifdef RTF_LLINFO
            if (rtm.rtm_flags & RTF_LLINFO)
                break;
#endif
#if 0
            krb5_klog_syslog(LOG_DEBUG,
                             "network reconfiguration message (%s) received",
                             rtm_type_name(rtm.rtm_type));
#endif
            network_reconfiguration_needed = 1;
            break;
        case RTM_RESOLVE:
#ifdef RTM_NEWMADDR
        case RTM_NEWMADDR:
        case RTM_DELMADDR:
#endif
        case RTM_MISS:
        case RTM_REDIRECT:
        case RTM_LOSING:
        case RTM_GET:
            /* Not interesting.  */
#if 0
            krb5_klog_syslog(LOG_DEBUG, "routing msg not interesting");
#endif
            break;
        default:
            krb5_klog_syslog(LOG_INFO,
                             "unhandled routing message type %d, will reconfigure just for the fun of it",
                             rtm.rtm_type);
            network_reconfiguration_needed = 1;
            break;
        }
    }
}

static void
setup_routing_socket(struct socksetup *data)
{
    int sock = socket(PF_ROUTE, SOCK_RAW, 0);
    if (sock < 0) {
        int e = errno;
        krb5_klog_syslog(LOG_INFO, "couldn't set up routing socket: %s",
                         strerror(e));
    } else {
        krb5_klog_syslog(LOG_INFO, "routing socket is fd %d", sock);
        add_fd(data, sock, CONN_ROUTING, process_routing_update);
        setnbio(sock);
        FD_SET(sock, &sstate.rfds);
    }
}
#endif

/* XXX */
extern int krb5int_debug_sendto_kdc;
extern void (*krb5int_sendtokdc_debug_handler)(const void*, size_t);

krb5_error_code
setup_network()
{
    struct socksetup setup_data;
    krb5_error_code retval;
    char *cp;
    int i, port;

    FD_ZERO(&sstate.rfds);
    FD_ZERO(&sstate.wfds);
    FD_ZERO(&sstate.xfds);
    sstate.max = 0;

/*    krb5int_debug_sendto_kdc = 1; */
    krb5int_sendtokdc_debug_handler = klog_handler;

    /* Handle each realm's ports */
    for (i=0; i<kdc_numrealms; i++) {
        cp = kdc_realmlist[i]->realm_ports;
        while (cp && *cp) {
            if (*cp == ',' || isspace((int) *cp)) {
                cp++;
                continue;
            }
            port = strtol(cp, &cp, 10);
            if (cp == 0)
                break;
            retval = add_udp_port(port);
            if (retval)
                return retval;
        }

        cp = kdc_realmlist[i]->realm_tcp_ports;
        while (cp && *cp) {
            if (*cp == ',' || isspace((int) *cp)) {
                cp++;
                continue;
            }
            port = strtol(cp, &cp, 10);
            if (cp == 0)
                break;
            retval = add_tcp_port(port);
            if (retval)
                return retval;
        }
    }

    setup_data.retval = 0;
    krb5_klog_syslog (LOG_INFO, "setting up network...");
#ifdef HAVE_STRUCT_RT_MSGHDR
    setup_routing_socket(&setup_data);
#endif
    /* To do: Use RFC 2292 interface (or follow-on) and IPV6_PKTINFO,
       so we might need only one UDP socket; fall back to binding
       sockets on each address only if IPV6_PKTINFO isn't
       supported.  */
    setup_data.udp_flags = UDP_DO_IPV4 | UDP_DO_IPV6;
    setup_udp_pktinfo_ports(&setup_data);
    if (setup_data.udp_flags) {
        if (foreach_localaddr (&setup_data, setup_udp_port, 0, 0)) {
            return setup_data.retval;
        }
    }
    setup_tcp_listener_ports(&setup_data);
    krb5_klog_syslog (LOG_INFO, "set up %d sockets", n_sockets);
    if (n_sockets == 0) {
        kdc_err(NULL, 0, gettext("no sockets set up?"));
        exit (1);
    }

    return 0;
}

static void init_addr(krb5_fulladdr *faddr, struct sockaddr *sa)
{
    switch (sa->sa_family) {
    case AF_INET:
        faddr->address->addrtype = ADDRTYPE_INET;
        faddr->address->length = 4;
        faddr->address->contents = (krb5_octet *) &sa2sin(sa)->sin_addr;
        faddr->port = ntohs(sa2sin(sa)->sin_port);
        break;
#ifdef KRB5_USE_INET6
    case AF_INET6:
        if (IN6_IS_ADDR_V4MAPPED(&sa2sin6(sa)->sin6_addr)) {
            faddr->address->addrtype = ADDRTYPE_INET;
            faddr->address->length = 4;
            faddr->address->contents = 12 + (krb5_octet *) &sa2sin6(sa)->sin6_addr;
        } else {
            faddr->address->addrtype = ADDRTYPE_INET6;
            faddr->address->length = 16;
            faddr->address->contents = (krb5_octet *) &sa2sin6(sa)->sin6_addr;
        }
        faddr->port = ntohs(sa2sin6(sa)->sin6_port);
        break;
#endif
    default:
        faddr->address->addrtype = -1;
        faddr->address->length = 0;
        faddr->address->contents = 0;
        faddr->port = 0;
        break;
    }
}

/*
 * This holds whatever additional information might be needed to
 * properly send back to the client from the correct local address.
 *
 * In this case, we only need one datum so far: On Mac OS X, the
 * kernel doesn't seem to like sending from link-local addresses
 * unless we specify the correct interface.
 */

union aux_addressing_info {
    int ipv6_ifindex;
};

static int
recv_from_to(int s, void *buf, size_t len, int flags,
             struct sockaddr *from, socklen_t *fromlen,
             struct sockaddr *to, socklen_t *tolen,
             union aux_addressing_info *auxaddr)
{
#if (!defined(IP_PKTINFO) && !defined(IPV6_PKTINFO)) || !defined(CMSG_SPACE)
    if (to && tolen) {
        /* Clobber with something recognizeable in case we try to use
           the address.  */
        memset(to, 0x40, *tolen);
        *tolen = 0;
    }
    return recvfrom(s, buf, len, flags, from, fromlen);
#else
    int r;
    struct iovec iov;
    char cmsg[CMSG_SPACE(sizeof(union pktinfo))];
    struct cmsghdr *cmsgptr;
    struct msghdr msg;

    if (!to || !tolen)
        return recvfrom(s, buf, len, flags, from, fromlen);

    /* Clobber with something recognizeable in case we can't extract
       the address but try to use it anyways.  */
    memset(to, 0x40, *tolen);

    iov.iov_base = buf;
    iov.iov_len = len;
    memset(&msg, 0, sizeof(msg));
    msg.msg_name = from;
    msg.msg_namelen = *fromlen;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = cmsg;
    msg.msg_controllen = sizeof(cmsg);

    r = recvmsg(s, &msg, flags);
    if (r < 0)
        return r;
    *fromlen = msg.msg_namelen;

    /* On Darwin (and presumably all *BSD with KAME stacks),
       CMSG_FIRSTHDR doesn't check for a non-zero controllen.  RFC
       3542 recommends making this check, even though the (new) spec
       for CMSG_FIRSTHDR says it's supposed to do the check.  */
    if (msg.msg_controllen) {
        cmsgptr = CMSG_FIRSTHDR(&msg);
        while (cmsgptr) {
#ifdef IP_PKTINFO
            if (cmsgptr->cmsg_level == IPPROTO_IP
                && cmsgptr->cmsg_type == IP_PKTINFO
                && *tolen >= sizeof(struct sockaddr_in)) {
                struct in_pktinfo *pktinfo;
                memset(to, 0, sizeof(struct sockaddr_in));
                pktinfo = (struct in_pktinfo *)CMSG_DATA(cmsgptr);
                ((struct sockaddr_in *)to)->sin_addr = pktinfo->ipi_addr;
                ((struct sockaddr_in *)to)->sin_family = AF_INET;
                *tolen = sizeof(struct sockaddr_in);
                return r;
            }
#endif
#if defined(KRB5_USE_INET6) && defined(IPV6_PKTINFO)&& defined(HAVE_STRUCT_IN6_PKTINFO)
            if (cmsgptr->cmsg_level == IPPROTO_IPV6
                && cmsgptr->cmsg_type == IPV6_PKTINFO
                && *tolen >= sizeof(struct sockaddr_in6)) {
                struct in6_pktinfo *pktinfo;
                memset(to, 0, sizeof(struct sockaddr_in6));
                pktinfo = (struct in6_pktinfo *)CMSG_DATA(cmsgptr);
                ((struct sockaddr_in6 *)to)->sin6_addr = pktinfo->ipi6_addr;
                ((struct sockaddr_in6 *)to)->sin6_family = AF_INET6;
                *tolen = sizeof(struct sockaddr_in6);
                auxaddr->ipv6_ifindex = pktinfo->ipi6_ifindex;
                return r;
            }
#endif
            cmsgptr = CMSG_NXTHDR(&msg, cmsgptr);
        }
    }
    /* No info about destination addr was available.  */
    *tolen = 0;
    return r;
#endif
}

static int
send_to_from(int s, void *buf, size_t len, int flags,
             const struct sockaddr *to, socklen_t tolen,
             const struct sockaddr *from, socklen_t fromlen,
             union aux_addressing_info *auxaddr)
{
#if (!defined(IP_PKTINFO) && !defined(IPV6_PKTINFO)) || !defined(CMSG_SPACE)
    return sendto(s, buf, len, flags, to, tolen);
#else
    struct iovec iov;
    struct msghdr msg;
    struct cmsghdr *cmsgptr;
    char cbuf[CMSG_SPACE(sizeof(union pktinfo))];

    if (from == 0 || fromlen == 0 || from->sa_family != to->sa_family) {
    use_sendto:
        return sendto(s, buf, len, flags, to, tolen);
    }

    iov.iov_base = buf;
    iov.iov_len = len;
    /* Truncation?  */
    if (iov.iov_len != len)
        return EINVAL;
    memset(cbuf, 0, sizeof(cbuf));
    memset(&msg, 0, sizeof(msg));
    msg.msg_name = (void *) to;
    msg.msg_namelen = tolen;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = cbuf;
    /* CMSG_FIRSTHDR needs a non-zero controllen, or it'll return NULL
       on Linux.  */
    msg.msg_controllen = sizeof(cbuf);
    cmsgptr = CMSG_FIRSTHDR(&msg);
    msg.msg_controllen = 0;

    switch (from->sa_family) {
#if defined(IP_PKTINFO)
    case AF_INET:
        if (fromlen != sizeof(struct sockaddr_in))
            goto use_sendto;
        cmsgptr->cmsg_level = IPPROTO_IP;
        cmsgptr->cmsg_type = IP_PKTINFO;
        cmsgptr->cmsg_len = CMSG_LEN(sizeof(struct in_pktinfo));
        {
            struct in_pktinfo *p = (struct in_pktinfo *)CMSG_DATA(cmsgptr);
            const struct sockaddr_in *from4 = (const struct sockaddr_in *)from;
            p->ipi_spec_dst = from4->sin_addr;
        }
        msg.msg_controllen = CMSG_SPACE(sizeof(struct in_pktinfo));
        break;
#endif
#if defined(KRB5_USE_INET6) && defined(IPV6_PKTINFO) && defined(HAVE_STRUCT_IN6_PKTINFO)
    case AF_INET6:
        if (fromlen != sizeof(struct sockaddr_in6))
            goto use_sendto;
        cmsgptr->cmsg_level = IPPROTO_IPV6;
        cmsgptr->cmsg_type = IPV6_PKTINFO;
        cmsgptr->cmsg_len = CMSG_LEN(sizeof(struct in6_pktinfo));
        {
            struct in6_pktinfo *p = (struct in6_pktinfo *)CMSG_DATA(cmsgptr);
            const struct sockaddr_in6 *from6 = (const struct sockaddr_in6 *)from;
            p->ipi6_addr = from6->sin6_addr;
            /*
             * Because of the possibility of asymmetric routing, we
             * normally don't want to specify an interface.  However,
             * Mac OS X doesn't like sending from a link-local address
             * (which can come up in testing at least, if you wind up
             * with a "foo.local" name) unless we do specify the
             * interface.
             */
            if (IN6_IS_ADDR_LINKLOCAL(&from6->sin6_addr))
                p->ipi6_ifindex = auxaddr->ipv6_ifindex;
            /* otherwise, already zero */
        }
        msg.msg_controllen = CMSG_SPACE(sizeof(struct in6_pktinfo));
        break;
#endif
    default:
        goto use_sendto;
    }
    return sendmsg(s, &msg, flags);
#endif
}

static krb5_error_code
make_too_big_error (krb5_data **out)
{
    krb5_error errpkt;
    krb5_error_code retval;
    krb5_data *scratch;

    *out = NULL;
    memset(&errpkt, 0, sizeof(errpkt));

    retval = krb5_us_timeofday(kdc_context, &errpkt.stime, &errpkt.susec);
    if (retval)
        return retval;
    errpkt.error = KRB_ERR_RESPONSE_TOO_BIG;
    errpkt.server = tgs_server;
    errpkt.client = NULL;
    errpkt.text.length = 0;
    errpkt.text.data = 0;
    errpkt.e_data.length = 0;
    errpkt.e_data.data = 0;
    scratch = malloc(sizeof(*scratch));
    if (scratch == NULL)
        return ENOMEM;
    retval = krb5_mk_error(kdc_context, &errpkt, scratch);
    if (retval) {
        free(scratch);
        return retval;
    }

    *out = scratch;
    return 0;
}

static void process_packet(struct connection *conn, int selflags)
{
    int cc;
    socklen_t saddr_len, daddr_len;
    krb5_fulladdr faddr;
    krb5_error_code retval;
    struct sockaddr_storage saddr, daddr;
    krb5_address addr;
    krb5_data request;
    krb5_data *response;
    char pktbuf[MAX_DGRAM_SIZE];
    int port_fd = conn->fd;
    union aux_addressing_info auxaddr;

    response = NULL;
    saddr_len = sizeof(saddr);
    daddr_len = sizeof(daddr);
    memset(&auxaddr, 0, sizeof(auxaddr));
    cc = recv_from_to(port_fd, pktbuf, sizeof(pktbuf), 0,
                      (struct sockaddr *)&saddr, &saddr_len,
                      (struct sockaddr *)&daddr, &daddr_len,
                      &auxaddr);
    if (cc == -1) {
        if (errno != EINTR
            /* This is how Linux indicates that a previous
               transmission was refused, e.g., if the client timed out
               before getting the response packet.  */
            && errno != ECONNREFUSED
        )
            kdc_err(NULL, errno, gettext("while receiving from network"));
        return;
    }
    if (!cc)
        return;         /* zero-length packet? */

#if 0
    if (daddr_len > 0) {
        char addrbuf[100];
        if (getnameinfo(ss2sa(&daddr), daddr_len, addrbuf, sizeof(addrbuf),
                        0, 0, NI_NUMERICHOST))
            strlcpy(addrbuf, "?", sizeof(addrbuf));
        kdc_err(NULL, 0, "pktinfo says local addr is %s", addrbuf);
    }
#endif

    request.length = cc;
    request.data = pktbuf;
    faddr.address = &addr;
    init_addr(&faddr, ss2sa(&saddr));
    /* this address is in net order */
    if ((retval = dispatch(&request, &faddr, &response))) {
        kdc_err(NULL, retval, gettext("while dispatching (udp)"));
        return;
    }
    if (response == NULL)
        return;
    if (response->length > max_dgram_reply_size) {
        krb5_free_data(kdc_context, response);
        retval = make_too_big_error(&response);
        if (retval) {
            krb5_klog_syslog(LOG_ERR,
                             "error constructing KRB_ERR_RESPONSE_TOO_BIG error: %s",
                             error_message(retval));
            return;
        }
    }
    cc = send_to_from(port_fd, response->data, (socklen_t) response->length, 0,
                      (struct sockaddr *)&saddr, saddr_len,
                      (struct sockaddr *)&daddr, daddr_len,
                      &auxaddr);
    if (cc == -1) {
        /*
         * Note that the local address (daddr*) has no port number
         * info associated with it.
         */
        char saddrbuf[NI_MAXHOST], sportbuf[NI_MAXSERV];
        char daddrbuf[NI_MAXHOST];
        int e = errno;
        krb5_free_data(kdc_context, response);
        if (getnameinfo((struct sockaddr *)&daddr, daddr_len,
                        daddrbuf, sizeof(daddrbuf), 0, 0,
                        NI_NUMERICHOST) != 0) {
            strlcpy(daddrbuf, "?", sizeof(daddrbuf));
        }
        if (getnameinfo((struct sockaddr *)&saddr, saddr_len,
                        saddrbuf, sizeof(saddrbuf), sportbuf, sizeof(sportbuf),
                        NI_NUMERICHOST|NI_NUMERICSERV) != 0) {
            strlcpy(saddrbuf, "?", sizeof(saddrbuf));
            strlcpy(sportbuf, "?", sizeof(sportbuf));
        }
        kdc_err(NULL, errno, gettext("while sending reply to %s/%d"),
                saddrbuf, sportbuf, daddrbuf);
        return;
    }
    if (cc != response->length) {
        kdc_err(NULL, 0, gettext("short reply write %d vs %d\n"),
                response->length, cc);
    }
    krb5_free_data(kdc_context, response);
    return;
}

static int tcp_data_counter;
/* Solaris kerberos: getting this value from elsewhere */
extern int max_tcp_data_connections;

static void kill_tcp_connection(struct connection *);

static void accept_tcp_connection(struct connection *conn, int selflags)
{
    int s;
    struct sockaddr_storage addr_s;
    struct sockaddr *addr = (struct sockaddr *)&addr_s;
    socklen_t addrlen = sizeof(addr_s);
    struct socksetup sockdata;
    struct connection *newconn;
    char tmpbuf[10];

    s = accept(conn->fd, addr, &addrlen);
    if (s < 0)
        return;
    set_cloexec_fd(s);
#ifndef _WIN32
    if (s >= FD_SETSIZE) {
        close(s);
        return;
    }
#endif
    setnbio(s), setnolinger(s), setkeepalive(s);

    sockdata.retval = 0;

    newconn = add_tcp_data_fd(&sockdata, s);
    if (newconn == NULL)
        return;

    if (getnameinfo((struct sockaddr *)&addr_s, addrlen,
                    newconn->u.tcp.addrbuf, sizeof(newconn->u.tcp.addrbuf),
                    tmpbuf, sizeof(tmpbuf),
                    NI_NUMERICHOST | NI_NUMERICSERV))
        strlcpy(newconn->u.tcp.addrbuf, "???", sizeof(newconn->u.tcp.addrbuf));
    else {
        char *p, *end;
        p = newconn->u.tcp.addrbuf;
        end = p + sizeof(newconn->u.tcp.addrbuf);
        p += strlen(p);
        if (end - p > 2 + strlen(tmpbuf)) {
            *p++ = '.';
            strlcpy(p, tmpbuf, end - p);
        }
    }
#if 0
    krb5_klog_syslog(LOG_INFO, "accepted TCP connection on socket %d from %s",
                     s, newconn->u.tcp.addrbuf);
#endif

    newconn->u.tcp.addr_s = addr_s;
    newconn->u.tcp.addrlen = addrlen;
    newconn->u.tcp.bufsiz = 1024 * 1024;
    newconn->u.tcp.buffer = malloc(newconn->u.tcp.bufsiz);
    newconn->u.tcp.start_time = time(0);

    if (++tcp_data_counter > max_tcp_data_connections) {
        struct connection *oldest_tcp = NULL;
        struct connection *c;
        int i;

        krb5_klog_syslog(LOG_INFO, "too many connections");

        FOREACH_ELT (connections, i, c) {
            if (c->type != CONN_TCP)
                continue;
            if (c == newconn)
                continue;
#if 0
            krb5_klog_syslog(LOG_INFO, "fd %d started at %ld", c->fd,
                             c->u.tcp.start_time);
#endif
            if (oldest_tcp == NULL
                || oldest_tcp->u.tcp.start_time > c->u.tcp.start_time)
                oldest_tcp = c;
        }
        if (oldest_tcp != NULL) {
            krb5_klog_syslog(LOG_INFO, "dropping tcp fd %d from %s",
                             oldest_tcp->fd, oldest_tcp->u.tcp.addrbuf);
            kill_tcp_connection(oldest_tcp);
        }
    }
    if (newconn->u.tcp.buffer == 0) {
        kdc_err(NULL, errno, gettext("allocating buffer for new TCP session from %s"),
                newconn->u.tcp.addrbuf);
        delete_fd(newconn);
        close(s);
        tcp_data_counter--;
        return;
    }
    newconn->u.tcp.offset = 0;
    newconn->u.tcp.faddr.address = &newconn->u.tcp.kaddr;
    init_addr(&newconn->u.tcp.faddr, ss2sa(&newconn->u.tcp.addr_s));
    SG_SET(&newconn->u.tcp.sgbuf[0], newconn->u.tcp.lenbuf, 4);
    SG_SET(&newconn->u.tcp.sgbuf[1], 0, 0);

    FD_SET(s, &sstate.rfds);
    if (sstate.max <= s)
        sstate.max = s + 1;
}

static void
kill_tcp_connection(struct connection *conn)
{
    if (conn->u.tcp.response)
        krb5_free_data(kdc_context, conn->u.tcp.response);
    if (conn->u.tcp.buffer)
        free(conn->u.tcp.buffer);
    FD_CLR(conn->fd, &sstate.rfds);
    FD_CLR(conn->fd, &sstate.wfds);
    if (sstate.max == conn->fd + 1)
        while (sstate.max > 0
               && ! FD_ISSET(sstate.max-1, &sstate.rfds)
               && ! FD_ISSET(sstate.max-1, &sstate.wfds)
               /* && ! FD_ISSET(sstate.max-1, &sstate.xfds) */
        )
            sstate.max--;
    close(conn->fd);
    conn->fd = -1;
    delete_fd(conn);
    tcp_data_counter--;
}

static krb5_error_code
make_toolong_error (krb5_data **out)
{
    krb5_error errpkt;
    krb5_error_code retval;
    krb5_data *scratch;

    retval = krb5_us_timeofday(kdc_context, &errpkt.stime, &errpkt.susec);
    if (retval)
        return retval;
    errpkt.error = KRB_ERR_FIELD_TOOLONG;
    errpkt.server = tgs_server;
    errpkt.client = NULL;
    errpkt.cusec = 0;
    errpkt.ctime = 0;
    errpkt.text.length = 0;
    errpkt.text.data = 0;
    errpkt.e_data.length = 0;
    errpkt.e_data.data = 0;
    scratch = malloc(sizeof(*scratch));
    if (scratch == NULL)
        return ENOMEM;
    retval = krb5_mk_error(kdc_context, &errpkt, scratch);
    if (retval) {
        free(scratch);
        return retval;
    }

    *out = scratch;
    return 0;
}

static void
queue_tcp_outgoing_response(struct connection *conn)
{
    store_32_be(conn->u.tcp.response->length, conn->u.tcp.lenbuf);
    SG_SET(&conn->u.tcp.sgbuf[1], conn->u.tcp.response->data,
           conn->u.tcp.response->length);
    conn->u.tcp.sgp = conn->u.tcp.sgbuf;
    conn->u.tcp.sgnum = 2;
    FD_SET(conn->fd, &sstate.wfds);
}

static void
process_tcp_connection(struct connection *conn, int selflags)
{
    if (selflags & SSF_WRITE) {
        ssize_t nwrote;
        SOCKET_WRITEV_TEMP tmp;

        nwrote = SOCKET_WRITEV(conn->fd, conn->u.tcp.sgp, conn->u.tcp.sgnum,
                               tmp);
        if (nwrote < 0) {
            goto kill_tcp_connection;
        }
        if (nwrote == 0)
            /* eof */
            goto kill_tcp_connection;
        while (nwrote) {
            sg_buf *sgp = conn->u.tcp.sgp;
            if (nwrote < SG_LEN(sgp)) {
                SG_ADVANCE(sgp, nwrote);
                nwrote = 0;
            } else {
                nwrote -= SG_LEN(sgp);
                conn->u.tcp.sgp++;
                conn->u.tcp.sgnum--;
                if (conn->u.tcp.sgnum == 0 && nwrote != 0)
                    abort();
            }
        }
        if (conn->u.tcp.sgnum == 0) {
            /* finished sending */
            /* We should go back to reading, though if we sent a
               FIELD_TOOLONG error in reply to a length with the high
               bit set, RFC 4120 says we have to close the TCP
               stream.  */
            goto kill_tcp_connection;
        }
    } else if (selflags & SSF_READ) {
        /* Read message length and data into one big buffer, already
           allocated at connect time.  If we have a complete message,
           we stop reading, so we should only be here if there is no
           data in the buffer, or only an incomplete message.  */
        size_t len;
        ssize_t nread;
        if (conn->u.tcp.offset < 4) {
            /* msglen has not been computed */
            /* XXX Doing at least two reads here, letting the kernel
               worry about buffering.  It'll be faster when we add
               code to manage the buffer here.  */
            len = 4 - conn->u.tcp.offset;
            nread = SOCKET_READ(conn->fd,
                                conn->u.tcp.buffer + conn->u.tcp.offset, len);
            if (nread < 0)
                /* error */
                goto kill_tcp_connection;
            if (nread == 0)
                /* eof */
                goto kill_tcp_connection;
            conn->u.tcp.offset += nread;
            if (conn->u.tcp.offset == 4) {
                unsigned char *p = (unsigned char *)conn->u.tcp.buffer;
                conn->u.tcp.msglen = load_32_be(p);
                if (conn->u.tcp.msglen > conn->u.tcp.bufsiz - 4) {
                    krb5_error_code err;
                    /* message too big */
                    krb5_klog_syslog(LOG_ERR, "TCP client %s wants %lu bytes, cap is %lu",
                                     conn->u.tcp.addrbuf, (unsigned long) conn->u.tcp.msglen,
                                     (unsigned long) conn->u.tcp.bufsiz - 4);
                    /* XXX Should return an error.  */
                    err = make_toolong_error (&conn->u.tcp.response);
                    if (err) {
                        krb5_klog_syslog(LOG_ERR,
                                         "error constructing KRB_ERR_FIELD_TOOLONG error! %s",
                                         error_message(err));
                        goto kill_tcp_connection;
                    }
                    goto have_response;
                }
            }
        } else {
            /* msglen known */
            krb5_data request;
            krb5_error_code err;

            len = conn->u.tcp.msglen - (conn->u.tcp.offset - 4);
            nread = SOCKET_READ(conn->fd,
                                conn->u.tcp.buffer + conn->u.tcp.offset, len);
            if (nread < 0)
                /* error */
                goto kill_tcp_connection;
            if (nread == 0)
                /* eof */
                goto kill_tcp_connection;
            conn->u.tcp.offset += nread;
            if (conn->u.tcp.offset < conn->u.tcp.msglen + 4)
                return;
            /* have a complete message, and exactly one message */
            request.length = conn->u.tcp.msglen;
            request.data = conn->u.tcp.buffer + 4;
            err = dispatch(&request, &conn->u.tcp.faddr,
                           &conn->u.tcp.response);
            if (err) {
                kdc_err(NULL, err, gettext("while dispatching (tcp)"));
                goto kill_tcp_connection;
            }
        have_response:
            queue_tcp_outgoing_response(conn);
            FD_CLR(conn->fd, &sstate.rfds);
        }
    } else
        abort();

    return;

kill_tcp_connection:
    kill_tcp_connection(conn);
}

static void service_conn(struct connection *conn, int selflags)
{
    conn->service(conn, selflags);
}

/* from sendto_kdc.c */
static int getcurtime(struct timeval *tvp)
{
#ifdef _WIN32
    struct _timeb tb;
    _ftime(&tb);
    tvp->tv_sec = tb.time;
    tvp->tv_usec = tb.millitm * 1000;
    return 0;
#else
    return gettimeofday(tvp, 0) ? errno : 0;
#endif
}

krb5_error_code
listen_and_process()
{
    int                 nfound;
    /* This struct contains 3 fd_set objects; on some platforms, they
       can be rather large.  Making this static avoids putting all
       that junk on the stack.  */
    static struct select_state sout;
    int                 i, sret, netchanged = 0;
    krb5_error_code     err;

    if (conns == (struct connection **) NULL)
        return KDC5_NONET;

    while (!signal_requests_exit) {
        if (signal_requests_hup) {
            int k;

            krb5_klog_reopen(kdc_context);
            for (k = 0; k < kdc_numrealms; k++)
                krb5_db_invoke(kdc_realmlist[k]->realm_context,
                               KRB5_KDB_METHOD_REFRESH_POLICY,
                               NULL, NULL);
            signal_requests_hup = 0;
        }

        if (network_reconfiguration_needed) {
            /* No point in re-logging what we've just logged.  */
            if (netchanged == 0)
                krb5_klog_syslog(LOG_INFO, "network reconfiguration needed");
            /* It might be tidier to add a timer-callback interface to
               the control loop here, but for this one use, it's not a
               big deal.  */
            err = getcurtime(&sstate.end_time);
            if (err) {
                kdc_err(NULL, err, gettext("while getting the time"));
                continue;
            }
            sstate.end_time.tv_sec += 3;
            netchanged = 1;
        } else
            sstate.end_time.tv_sec = sstate.end_time.tv_usec = 0;

        err = krb5int_cm_call_select(&sstate, &sout, &sret);
        if (err) {
            if (err != EINTR)
                kdc_err(NULL, err, gettext("while selecting for network input(1)"));
            continue;
        }
        if (sret == 0 && netchanged) {
            network_reconfiguration_needed = 0;
            closedown_network();
            err = setup_network();
            if (err) {
                kdc_err(NULL, err, gettext("while reinitializing network"));
                return err;
            }
            netchanged = 0;
        }
        if (sret == -1) {
            if (errno != EINTR)
                kdc_err(NULL, errno, gettext("while selecting for network input(2)"));
            continue;
        }
        nfound = sret;
        for (i=0; i<n_sockets && nfound > 0; i++) {
            int sflags = 0;
            if (conns[i]->fd < 0)
                abort();
            if (FD_ISSET(conns[i]->fd, &sout.rfds))
                sflags |= SSF_READ, nfound--;
            if (FD_ISSET(conns[i]->fd, &sout.wfds))
                sflags |= SSF_WRITE, nfound--;
            if (sflags)
                service_conn(conns[i], sflags);
        }
    }
    krb5_klog_syslog(LOG_INFO, "shutdown signal received");
    return 0;
}

krb5_error_code
closedown_network()
{
    int i;
    struct connection *conn;

    if (conns == (struct connection **) NULL)
        return KDC5_NONET;

    FOREACH_ELT (connections, i, conn) {
        if (conn->fd >= 0) {
            krb5_klog_syslog(LOG_INFO, "closing down fd %d", conn->fd);
            (void) close(conn->fd);
        }
        DEL (connections, i);
        /* There may also be per-connection data in the tcp structure
           (tcp.buffer, tcp.response) that we're not freeing here.
           That should only happen if we quit with a connection in
           progress.  */
        free(conn);
    }
    FREE_SET_DATA(connections);
    FREE_SET_DATA(udp_port_data);
    FREE_SET_DATA(tcp_port_data);

    return 0;
}

#endif /* INET */
