/* MIT License
 *
 * Copyright (c) Massachusetts Institute of Technology
 * Copyright (c) The c-ares project and its contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */
#include "ares_private.h"

#ifdef HAVE_SYS_UIO_H
#  include <sys/uio.h>
#endif
#ifdef HAVE_NETINET_IN_H
#  include <netinet/in.h>
#endif
#ifdef HAVE_NETINET_TCP_H
#  include <netinet/tcp.h>
#endif
#ifdef HAVE_NETDB_H
#  include <netdb.h>
#endif
#ifdef HAVE_ARPA_INET_H
#  include <arpa/inet.h>
#endif

#ifdef HAVE_STRINGS_H
#  include <strings.h>
#endif
#ifdef HAVE_SYS_IOCTL_H
#  include <sys/ioctl.h>
#endif
#ifdef NETWARE
#  include <sys/filio.h>
#endif

#include <assert.h>
#include <fcntl.h>
#include <limits.h>

#if defined(__linux__) && defined(MSG_FASTOPEN)
#  define TFO_SUPPORTED      1
#  define TFO_SKIP_CONNECT   0
#  define TFO_USE_SENDTO     0
#  define TFO_USE_CONNECTX   0
#  define TFO_CLIENT_SOCKOPT TCP_FASTOPEN_CONNECT
#elif defined(__FreeBSD__) && defined(TCP_FASTOPEN)
#  define TFO_SUPPORTED      1
#  define TFO_SKIP_CONNECT   1
#  define TFO_USE_SENDTO     1
#  define TFO_USE_CONNECTX   0
#  define TFO_CLIENT_SOCKOPT TCP_FASTOPEN
#elif defined(__APPLE__) && defined(HAVE_CONNECTX)
#  define TFO_SUPPORTED    1
#  define TFO_SKIP_CONNECT 0
#  define TFO_USE_SENDTO   0
#  define TFO_USE_CONNECTX 1
#  undef TFO_CLIENT_SOCKOPT
#else
#  define TFO_SUPPORTED 0
#endif


#ifndef HAVE_WRITEV
/* Structure for scatter/gather I/O. */
struct iovec {
  void  *iov_base; /* Pointer to data. */
  size_t iov_len;  /* Length of data.  */
};
#endif


/* Return 1 if the specified error number describes a readiness error, or 0
 * otherwise. This is mostly for HP-UX, which could return EAGAIN or
 * EWOULDBLOCK. See this man page
 *
 * http://devrsrc1.external.hp.com/STKS/cgi-bin/man2html?
 *     manpage=/usr/share/man/man2.Z/send.2
 */
ares_bool_t ares__socket_try_again(int errnum)
{
#if !defined EWOULDBLOCK && !defined EAGAIN
#  error "Neither EWOULDBLOCK nor EAGAIN defined"
#endif

#ifdef EWOULDBLOCK
  if (errnum == EWOULDBLOCK) {
    return ARES_TRUE;
  }
#endif

#if defined EAGAIN && EAGAIN != EWOULDBLOCK
  if (errnum == EAGAIN) {
    return ARES_TRUE;
  }
#endif

  return ARES_FALSE;
}

ares_ssize_t ares__socket_recv(ares_channel_t *channel, ares_socket_t s,
                               void *data, size_t data_len)
{
  if (channel->sock_funcs && channel->sock_funcs->arecvfrom) {
    return channel->sock_funcs->arecvfrom(s, data, data_len, 0, 0, 0,
                                          channel->sock_func_cb_data);
  }

  return (ares_ssize_t)recv((RECV_TYPE_ARG1)s, (RECV_TYPE_ARG2)data,
                            (RECV_TYPE_ARG3)data_len, (RECV_TYPE_ARG4)(0));
}

ares_ssize_t ares__socket_recvfrom(ares_channel_t *channel, ares_socket_t s,
                                   void *data, size_t data_len, int flags,
                                   struct sockaddr *from,
                                   ares_socklen_t  *from_len)
{
  if (channel->sock_funcs && channel->sock_funcs->arecvfrom) {
    return channel->sock_funcs->arecvfrom(s, data, data_len, flags, from,
                                          from_len, channel->sock_func_cb_data);
  }

#ifdef HAVE_RECVFROM
  return (ares_ssize_t)recvfrom(s, data, (RECVFROM_TYPE_ARG3)data_len, flags,
                                from, from_len);
#else
  return ares__socket_recv(channel, s, data, data_len);
#endif
}

/* Use like:
 *   struct sockaddr_storage sa_storage;
 *   ares_socklen_t          salen     = sizeof(sa_storage);
 *   struct sockaddr        *sa        = (struct sockaddr *)&sa_storage;
 *   ares__conn_set_sockaddr(conn, sa, &salen);
 */
static ares_status_t ares__conn_set_sockaddr(const ares_conn_t *conn,
                                             struct sockaddr   *sa,
                                             ares_socklen_t    *salen)
{
  const ares_server_t *server = conn->server;
  unsigned short       port =
    conn->flags & ARES_CONN_FLAG_TCP ? server->tcp_port : server->udp_port;
  struct sockaddr_in  *sin;
  struct sockaddr_in6 *sin6;

  switch (server->addr.family) {
    case AF_INET:
      sin = (struct sockaddr_in *)(void *)sa;
      if (*salen < (ares_socklen_t)sizeof(*sin)) {
        return ARES_EFORMERR;
      }
      *salen = sizeof(*sin);
      memset(sin, 0, sizeof(*sin));
      sin->sin_family = AF_INET;
      sin->sin_port   = htons(port);
      memcpy(&sin->sin_addr, &server->addr.addr.addr4, sizeof(sin->sin_addr));
      return ARES_SUCCESS;
    case AF_INET6:
      sin6 = (struct sockaddr_in6 *)(void *)sa;
      if (*salen < (ares_socklen_t)sizeof(*sin6)) {
        return ARES_EFORMERR;
      }
      *salen = sizeof(*sin6);
      memset(sin6, 0, sizeof(*sin6));
      sin6->sin6_family = AF_INET6;
      sin6->sin6_port   = htons(port);
      memcpy(&sin6->sin6_addr, &server->addr.addr.addr6,
             sizeof(sin6->sin6_addr));
#ifdef HAVE_STRUCT_SOCKADDR_IN6_SIN6_SCOPE_ID
      sin6->sin6_scope_id = server->ll_scope;
#endif
      return ARES_SUCCESS;
    default:
      break;
  }

  return ARES_EBADFAMILY;
}

static ares_status_t ares_conn_set_self_ip(ares_conn_t *conn, ares_bool_t early)
{
  struct sockaddr_storage sa_storage;
  int                     rv;
  ares_socklen_t          len = sizeof(sa_storage);

  /* We call this twice on TFO, if we already have the IP we can go ahead and
   * skip processing */
  if (!early && conn->self_ip.family != AF_UNSPEC) {
    return ARES_SUCCESS;
  }

  memset(&sa_storage, 0, sizeof(sa_storage));

  rv = getsockname(conn->fd, (struct sockaddr *)(void *)&sa_storage, &len);
  if (rv != 0) {
    /* During TCP FastOpen, we can't get the IP this early since connect()
     * may not be called.  That's ok, we'll try again later */
    if (early && conn->flags & ARES_CONN_FLAG_TCP &&
        conn->flags & ARES_CONN_FLAG_TFO) {
      memset(&conn->self_ip, 0, sizeof(conn->self_ip));
      return ARES_SUCCESS;
    }
    return ARES_ECONNREFUSED;
  }

  if (!ares_sockaddr_to_ares_addr(&conn->self_ip, NULL,
                                  (struct sockaddr *)(void *)&sa_storage)) {
    return ARES_ECONNREFUSED;
  }

  return ARES_SUCCESS;
}

ares_ssize_t ares__conn_write(ares_conn_t *conn, const void *data, size_t len)
{
  ares_channel_t *channel = conn->server->channel;
  int             flags   = 0;

#ifdef HAVE_MSG_NOSIGNAL
  flags |= MSG_NOSIGNAL;
#endif

  if (channel->sock_funcs && channel->sock_funcs->asendv) {
    struct iovec vec;
    vec.iov_base = (void *)((size_t)data); /* Cast off const */
    vec.iov_len  = len;
    return channel->sock_funcs->asendv(conn->fd, &vec, 1,
                                       channel->sock_func_cb_data);
  }

  if (conn->flags & ARES_CONN_FLAG_TFO_INITIAL) {
    conn->flags &= ~((unsigned int)ARES_CONN_FLAG_TFO_INITIAL);

#if defined(TFO_USE_SENDTO) && TFO_USE_SENDTO
    {
      struct sockaddr_storage sa_storage;
      ares_socklen_t          salen = sizeof(sa_storage);
      struct sockaddr        *sa    = (struct sockaddr *)&sa_storage;
      ares_status_t           status;
      ares_ssize_t            rv;

      status = ares__conn_set_sockaddr(conn, sa, &salen);
      if (status != ARES_SUCCESS) {
        return status;
      }

      rv = (ares_ssize_t)sendto((SEND_TYPE_ARG1)conn->fd, (SEND_TYPE_ARG2)data,
                                (SEND_TYPE_ARG3)len, (SEND_TYPE_ARG4)flags, sa,
                                salen);

      /* If using TFO, we might not have been able to get an IP earlier, since
       * we hadn't informed the OS of the destination.  When using sendto()
       * now we have so we should be able to fetch it */
      ares_conn_set_self_ip(conn, ARES_TRUE);
      return rv;
    }
#endif
  }

  return (ares_ssize_t)send((SEND_TYPE_ARG1)conn->fd, (SEND_TYPE_ARG2)data,
                            (SEND_TYPE_ARG3)len, (SEND_TYPE_ARG4)flags);
}

/*
 * setsocknonblock sets the given socket to either blocking or non-blocking
 * mode based on the 'nonblock' boolean argument. This function is highly
 * portable.
 */
static int setsocknonblock(ares_socket_t sockfd, /* operate on this */
                           int           nonblock /* TRUE or FALSE */)
{
#if defined(USE_BLOCKING_SOCKETS)

  return 0; /* returns success */

#elif defined(HAVE_FCNTL_O_NONBLOCK)

  /* most recent unix versions */
  int flags;
  flags = fcntl(sockfd, F_GETFL, 0);
  if (nonblock) {
    return fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
  } else {
    return fcntl(sockfd, F_SETFL, flags & (~O_NONBLOCK)); /* LCOV_EXCL_LINE */
  }

#elif defined(HAVE_IOCTL_FIONBIO)

  /* older unix versions */
  int flags = nonblock ? 1 : 0;
  return ioctl(sockfd, FIONBIO, &flags);

#elif defined(HAVE_IOCTLSOCKET_FIONBIO)

#  ifdef WATT32
  char flags = nonblock ? 1 : 0;
#  else
  /* Windows */
  unsigned long flags = nonblock ? 1UL : 0UL;
#  endif
  return ioctlsocket(sockfd, (long)FIONBIO, &flags);

#elif defined(HAVE_IOCTLSOCKET_CAMEL_FIONBIO)

  /* Amiga */
  long flags = nonblock ? 1L : 0L;
  return IoctlSocket(sockfd, FIONBIO, flags);

#elif defined(HAVE_SETSOCKOPT_SO_NONBLOCK)

  /* BeOS */
  long b = nonblock ? 1L : 0L;
  return setsockopt(sockfd, SOL_SOCKET, SO_NONBLOCK, &b, sizeof(b));

#else
#  error "no non-blocking method was found/used/set"
#endif
}

#if defined(IPV6_V6ONLY) && defined(USE_WINSOCK)
/* It makes support for IPv4-mapped IPv6 addresses.
 * Linux kernel, NetBSD, FreeBSD and Darwin: default is off;
 * Windows Vista and later: default is on;
 * DragonFly BSD: acts like off, and dummy setting;
 * OpenBSD and earlier Windows: unsupported.
 * Linux: controlled by /proc/sys/net/ipv6/bindv6only.
 */
static void set_ipv6_v6only(ares_socket_t sockfd, int on)
{
  (void)setsockopt(sockfd, IPPROTO_IPV6, IPV6_V6ONLY, (void *)&on, sizeof(on));
}
#else
#  define set_ipv6_v6only(s, v)
#endif

static ares_status_t configure_socket(ares_conn_t *conn)
{
  union {
    struct sockaddr     sa;
    struct sockaddr_in  sa4;
    struct sockaddr_in6 sa6;
  } local;

  ares_socklen_t  bindlen = 0;
  ares_server_t  *server  = conn->server;
  ares_channel_t *channel = server->channel;

  /* do not set options for user-managed sockets */
  if (channel->sock_funcs && channel->sock_funcs->asocket) {
    return ARES_SUCCESS;
  }

  (void)setsocknonblock(conn->fd, 1);

#if defined(FD_CLOEXEC) && !defined(MSDOS)
  /* Configure the socket fd as close-on-exec. */
  if (fcntl(conn->fd, F_SETFD, FD_CLOEXEC) != 0) {
    return ARES_ECONNREFUSED; /* LCOV_EXCL_LINE */
  }
#endif

  /* No need to emit SIGPIPE on socket errors */
#if defined(SO_NOSIGPIPE)
  {
    int opt = 1;
    (void)setsockopt(conn->fd, SOL_SOCKET, SO_NOSIGPIPE, (void *)&opt,
                     sizeof(opt));
  }
#endif

  /* Set the socket's send and receive buffer sizes. */
  if (channel->socket_send_buffer_size > 0 &&
      setsockopt(conn->fd, SOL_SOCKET, SO_SNDBUF,
                 (void *)&channel->socket_send_buffer_size,
                 sizeof(channel->socket_send_buffer_size)) != 0) {
    return ARES_ECONNREFUSED; /* LCOV_EXCL_LINE: UntestablePath */
  }

  if (channel->socket_receive_buffer_size > 0 &&
      setsockopt(conn->fd, SOL_SOCKET, SO_RCVBUF,
                 (void *)&channel->socket_receive_buffer_size,
                 sizeof(channel->socket_receive_buffer_size)) != 0) {
    return ARES_ECONNREFUSED; /* LCOV_EXCL_LINE: UntestablePath */
  }

#ifdef SO_BINDTODEVICE
  if (ares_strlen(channel->local_dev_name)) {
    /* Only root can do this, and usually not fatal if it doesn't work, so
     * just continue on. */
    (void)setsockopt(conn->fd, SOL_SOCKET, SO_BINDTODEVICE,
                     channel->local_dev_name, sizeof(channel->local_dev_name));
  }
#endif

  if (server->addr.family == AF_INET && channel->local_ip4) {
    memset(&local.sa4, 0, sizeof(local.sa4));
    local.sa4.sin_family      = AF_INET;
    local.sa4.sin_addr.s_addr = htonl(channel->local_ip4);
    bindlen                   = sizeof(local.sa4);
  } else if (server->addr.family == AF_INET6 && server->ll_scope == 0 &&
             memcmp(channel->local_ip6, ares_in6addr_any._S6_un._S6_u8,
                    sizeof(channel->local_ip6)) != 0) {
    /* Only if not link-local and an ip other than "::" is specified */
    memset(&local.sa6, 0, sizeof(local.sa6));
    local.sa6.sin6_family = AF_INET6;
    memcpy(&local.sa6.sin6_addr, channel->local_ip6,
           sizeof(channel->local_ip6));
    bindlen = sizeof(local.sa6);
  }

  if (bindlen && bind(conn->fd, &local.sa, bindlen) < 0) {
    return ARES_ECONNREFUSED;
  }

  if (server->addr.family == AF_INET6) {
    set_ipv6_v6only(conn->fd, 0);
  }

  if (conn->flags & ARES_CONN_FLAG_TCP) {
    int opt = 1;

#ifdef TCP_NODELAY
    /*
     * Disable the Nagle algorithm (only relevant for TCP sockets, and thus not
     * in configure_socket). In general, in DNS lookups we're pretty much
     * interested in firing off a single request and then waiting for a reply,
     * so batching isn't very interesting.
     */
    if (setsockopt(conn->fd, IPPROTO_TCP, TCP_NODELAY, (void *)&opt,
                   sizeof(opt)) != 0) {
      return ARES_ECONNREFUSED;
    }
#endif

#if defined(TFO_CLIENT_SOCKOPT)
    if (conn->flags & ARES_CONN_FLAG_TFO &&
        setsockopt(conn->fd, IPPROTO_TCP, TFO_CLIENT_SOCKOPT, (void *)&opt,
                   sizeof(opt)) != 0) {
      /* Disable TFO if flag can't be set. */
      conn->flags &= ~((unsigned int)ARES_CONN_FLAG_TFO);
    }
#endif
  }

  return ARES_SUCCESS;
}

ares_bool_t ares_sockaddr_to_ares_addr(struct ares_addr      *ares_addr,
                                       unsigned short        *port,
                                       const struct sockaddr *sockaddr)
{
  if (sockaddr->sa_family == AF_INET) {
    /* NOTE: memcpy sockaddr_in due to alignment issues found by UBSAN due to
     *       dnsinfo packing on MacOS */
    struct sockaddr_in sockaddr_in;
    memcpy(&sockaddr_in, sockaddr, sizeof(sockaddr_in));

    ares_addr->family = AF_INET;
    memcpy(&ares_addr->addr.addr4, &(sockaddr_in.sin_addr),
           sizeof(ares_addr->addr.addr4));

    if (port) {
      *port = ntohs(sockaddr_in.sin_port);
    }
    return ARES_TRUE;
  }

  if (sockaddr->sa_family == AF_INET6) {
    /* NOTE: memcpy sockaddr_in6 due to alignment issues found by UBSAN due to
     *       dnsinfo packing on MacOS */
    struct sockaddr_in6 sockaddr_in6;
    memcpy(&sockaddr_in6, sockaddr, sizeof(sockaddr_in6));

    ares_addr->family = AF_INET6;
    memcpy(&ares_addr->addr.addr6, &(sockaddr_in6.sin6_addr),
           sizeof(ares_addr->addr.addr6));
    if (port) {
      *port = ntohs(sockaddr_in6.sin6_port);
    }
    return ARES_TRUE;
  }

  return ARES_FALSE;
}

static ares_status_t ares__conn_connect(ares_conn_t *conn, struct sockaddr *sa,
                                        ares_socklen_t salen)
{
  /* Normal non TCPFastOpen style connect */
  if (!(conn->flags & ARES_CONN_FLAG_TFO)) {
    return ares__connect_socket(conn->server->channel, conn->fd, sa, salen);
  }

  /* FreeBSD don't want any sort of connect() so skip */
#if defined(TFO_SKIP_CONNECT) && TFO_SKIP_CONNECT
  return ARES_SUCCESS;
#elif defined(TFO_USE_CONNECTX) && TFO_USE_CONNECTX
  {
    int rv;
    int err;

    do {
      sa_endpoints_t endpoints;
      memset(&endpoints, 0, sizeof(endpoints));
      endpoints.sae_dstaddr    = sa;
      endpoints.sae_dstaddrlen = salen;

      rv = connectx(conn->fd, &endpoints, SAE_ASSOCID_ANY,
                    CONNECT_DATA_IDEMPOTENT | CONNECT_RESUME_ON_READ_WRITE,
                    NULL, 0, NULL, NULL);

      err = SOCKERRNO;
      if (rv == -1 && err != EINPROGRESS && err != EWOULDBLOCK) {
        return ARES_ECONNREFUSED;
      }

    } while (rv == -1 && err == EINTR);
  }
  return ARES_SUCCESS;
#elif defined(TFO_SUPPORTED) && TFO_SUPPORTED
  return ares__connect_socket(conn->server->channel, conn->fd, sa, salen);
#else
  /* Shouldn't be possible */
  return ARES_ECONNREFUSED;
#endif
}

ares_status_t ares__open_connection(ares_conn_t   **conn_out,
                                    ares_channel_t *channel,
                                    ares_server_t *server, ares_bool_t is_tcp)
{
  ares_status_t           status;
  struct sockaddr_storage sa_storage;
  ares_socklen_t          salen = sizeof(sa_storage);
  struct sockaddr        *sa    = (struct sockaddr *)&sa_storage;
  ares_conn_t            *conn;
  ares__llist_node_t     *node  = NULL;
  int                     stype = is_tcp ? SOCK_STREAM : SOCK_DGRAM;

  *conn_out = NULL;

  conn = ares_malloc(sizeof(*conn));
  if (conn == NULL) {
    return ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  memset(conn, 0, sizeof(*conn));
  conn->fd              = ARES_SOCKET_BAD;
  conn->server          = server;
  conn->queries_to_conn = ares__llist_create(NULL);
  conn->flags           = is_tcp ? ARES_CONN_FLAG_TCP : ARES_CONN_FLAG_NONE;

  /* Enable TFO if the OS supports it and we were passed in data to send during
   * the connect. It might be disabled later if an error is encountered. Make
   * sure a user isn't overriding anything. */
  if (conn->flags & ARES_CONN_FLAG_TCP && channel->sock_funcs == NULL &&
      TFO_SUPPORTED) {
    conn->flags |= ARES_CONN_FLAG_TFO;
  }

  if (conn->queries_to_conn == NULL) {
    /* LCOV_EXCL_START: OutOfMemory */
    status = ARES_ENOMEM;
    goto done;
    /* LCOV_EXCL_STOP */
  }

  /* Convert into the struct sockaddr structure needed by the OS */
  status = ares__conn_set_sockaddr(conn, sa, &salen);
  if (status != ARES_SUCCESS) {
    goto done;
  }

  /* Acquire a socket. */
  conn->fd = ares__open_socket(channel, server->addr.family, stype, 0);
  if (conn->fd == ARES_SOCKET_BAD) {
    status = ARES_ECONNREFUSED;
    goto done;
  }

  /* Configure it. */
  status = configure_socket(conn);
  if (status != ARES_SUCCESS) {
    goto done;
  }

  if (channel->sock_config_cb) {
    int err =
      channel->sock_config_cb(conn->fd, stype, channel->sock_config_cb_data);
    if (err < 0) {
      status = ARES_ECONNREFUSED;
      goto done;
    }
  }

  /* Connect */
  status = ares__conn_connect(conn, sa, salen);
  if (status != ARES_SUCCESS) {
    goto done;
  }

  if (channel->sock_create_cb) {
    int err =
      channel->sock_create_cb(conn->fd, stype, channel->sock_create_cb_data);
    if (err < 0) {
      status = ARES_ECONNREFUSED;
      goto done;
    }
  }

  /* Let the connection know we haven't written our first packet yet for TFO */
  if (conn->flags & ARES_CONN_FLAG_TFO) {
    conn->flags |= ARES_CONN_FLAG_TFO_INITIAL;
  }

  /* Need to store our own ip for DNS cookie support */
  status = ares_conn_set_self_ip(conn, ARES_FALSE);
  if (status != ARES_SUCCESS) {
    goto done; /* LCOV_EXCL_LINE: UntestablePath */
  }

  /* TCP connections are thrown to the end as we don't spawn multiple TCP
   * connections. UDP connections are put on front where the newest connection
   * can be quickly pulled */
  if (is_tcp) {
    node = ares__llist_insert_last(server->connections, conn);
  } else {
    node = ares__llist_insert_first(server->connections, conn);
  }
  if (node == NULL) {
    /* LCOV_EXCL_START: OutOfMemory */
    status = ARES_ENOMEM;
    goto done;
    /* LCOV_EXCL_STOP */
  }

  /* Register globally to quickly map event on file descriptor to connection
   * node object */
  if (!ares__htable_asvp_insert(channel->connnode_by_socket, conn->fd, node)) {
    /* LCOV_EXCL_START: OutOfMemory */
    status = ARES_ENOMEM;
    goto done;
    /* LCOV_EXCL_STOP */
  }

  SOCK_STATE_CALLBACK(channel, conn->fd, 1, is_tcp ? 1 : 0);

  if (is_tcp) {
    server->tcp_conn = conn;
  }

done:
  if (status != ARES_SUCCESS) {
    ares__llist_node_claim(node);
    ares__llist_destroy(conn->queries_to_conn);
    ares__close_socket(channel, conn->fd);
    ares_free(conn);
  } else {
    *conn_out = conn;
  }
  return status;
}

ares_socket_t ares__open_socket(ares_channel_t *channel, int af, int type,
                                int protocol)
{
  if (channel->sock_funcs && channel->sock_funcs->asocket) {
    return channel->sock_funcs->asocket(af, type, protocol,
                                        channel->sock_func_cb_data);
  }

  return socket(af, type, protocol);
}

ares_status_t ares__connect_socket(ares_channel_t        *channel,
                                   ares_socket_t          sockfd,
                                   const struct sockaddr *addr,
                                   ares_socklen_t         addrlen)
{
  int rv;
  int err;

  do {
    if (channel->sock_funcs && channel->sock_funcs->aconnect) {
      rv = channel->sock_funcs->aconnect(sockfd, addr, addrlen,
                                         channel->sock_func_cb_data);
    } else {
      rv = connect(sockfd, addr, addrlen);
    }

    err = SOCKERRNO;

    if (rv == -1 && err != EINPROGRESS && err != EWOULDBLOCK) {
      return ARES_ECONNREFUSED;
    }

  } while (rv == -1 && err == EINTR);

  return ARES_SUCCESS;
}

void ares__close_socket(ares_channel_t *channel, ares_socket_t s)
{
  if (s == ARES_SOCKET_BAD) {
    return;
  }

  if (channel->sock_funcs && channel->sock_funcs->aclose) {
    channel->sock_funcs->aclose(s, channel->sock_func_cb_data);
  } else {
    sclose(s);
  }
}

void ares_set_socket_callback(ares_channel_t           *channel,
                              ares_sock_create_callback cb, void *data)
{
  if (channel == NULL) {
    return;
  }
  channel->sock_create_cb      = cb;
  channel->sock_create_cb_data = data;
}

void ares_set_socket_configure_callback(ares_channel_t           *channel,
                                        ares_sock_config_callback cb,
                                        void                     *data)
{
  if (channel == NULL || channel->optmask & ARES_OPT_EVENT_THREAD) {
    return;
  }
  channel->sock_config_cb      = cb;
  channel->sock_config_cb_data = data;
}

void ares_set_socket_functions(ares_channel_t                     *channel,
                               const struct ares_socket_functions *funcs,
                               void                               *data)
{
  if (channel == NULL || channel->optmask & ARES_OPT_EVENT_THREAD) {
    return;
  }
  channel->sock_funcs        = funcs;
  channel->sock_func_cb_data = data;
}
