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
  return sread(s, data, data_len);
#endif
}

ares_ssize_t ares__socket_recv(ares_channel_t *channel, ares_socket_t s,
                               void *data, size_t data_len)
{
  if (channel->sock_funcs && channel->sock_funcs->arecvfrom) {
    return channel->sock_funcs->arecvfrom(s, data, data_len, 0, 0, 0,
                                          channel->sock_func_cb_data);
  }

  /* sread() is a wrapper for read() or recv() depending on the system */
  return sread(s, data, data_len);
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

static int configure_socket(ares_socket_t s, struct server_state *server)
{
  union {
    struct sockaddr     sa;
    struct sockaddr_in  sa4;
    struct sockaddr_in6 sa6;
  } local;

  ares_socklen_t  bindlen = 0;
  ares_channel_t *channel = server->channel;

  /* do not set options for user-managed sockets */
  if (channel->sock_funcs && channel->sock_funcs->asocket) {
    return 0;
  }

  (void)setsocknonblock(s, 1);

#if defined(FD_CLOEXEC) && !defined(MSDOS)
  /* Configure the socket fd as close-on-exec. */
  if (fcntl(s, F_SETFD, FD_CLOEXEC) == -1) {
    return -1; /* LCOV_EXCL_LINE */
  }
#endif

  /* No need to emit SIGPIPE on socket errors */
#if defined(SO_NOSIGPIPE)
  {
    int opt = 1;
    setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, (void *)&opt, sizeof(opt));
  }
#endif

  /* Set the socket's send and receive buffer sizes. */
  if ((channel->socket_send_buffer_size > 0) &&
      setsockopt(s, SOL_SOCKET, SO_SNDBUF,
                 (void *)&channel->socket_send_buffer_size,
                 sizeof(channel->socket_send_buffer_size)) == -1) {
    return -1; /* LCOV_EXCL_LINE: UntestablePath */
  }

  if ((channel->socket_receive_buffer_size > 0) &&
      setsockopt(s, SOL_SOCKET, SO_RCVBUF,
                 (void *)&channel->socket_receive_buffer_size,
                 sizeof(channel->socket_receive_buffer_size)) == -1) {
    return -1; /* LCOV_EXCL_LINE: UntestablePath */
  }

#ifdef SO_BINDTODEVICE
  if (channel->local_dev_name[0] &&
      setsockopt(s, SOL_SOCKET, SO_BINDTODEVICE, channel->local_dev_name,
                 sizeof(channel->local_dev_name))) {
    /* Only root can do this, and usually not fatal if it doesn't work, so */
    /* just continue on. */
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

  if (bindlen && bind(s, &local.sa, bindlen) < 0) {
    return -1;
  }

  if (server->addr.family == AF_INET6) {
    set_ipv6_v6only(s, 0);
  }

  return 0;
}

ares_status_t ares__open_connection(ares_channel_t      *channel,
                                    struct server_state *server,
                                    ares_bool_t          is_tcp)
{
  ares_socket_t  s;
  int            opt;
  ares_socklen_t salen;

  union {
    struct sockaddr_in  sa4;
    struct sockaddr_in6 sa6;
  } saddr;
  struct sockaddr          *sa;
  struct server_connection *conn;
  ares__llist_node_t       *node;
  int                       type = is_tcp ? SOCK_STREAM : SOCK_DGRAM;

  switch (server->addr.family) {
    case AF_INET:
      sa    = (void *)&saddr.sa4;
      salen = sizeof(saddr.sa4);
      memset(sa, 0, (size_t)salen);
      saddr.sa4.sin_family = AF_INET;
      saddr.sa4.sin_port = htons(is_tcp ? server->tcp_port : server->udp_port);
      memcpy(&saddr.sa4.sin_addr, &server->addr.addr.addr4,
             sizeof(saddr.sa4.sin_addr));
      break;
    case AF_INET6:
      sa    = (void *)&saddr.sa6;
      salen = sizeof(saddr.sa6);
      memset(sa, 0, (size_t)salen);
      saddr.sa6.sin6_family = AF_INET6;
      saddr.sa6.sin6_port = htons(is_tcp ? server->tcp_port : server->udp_port);
      memcpy(&saddr.sa6.sin6_addr, &server->addr.addr.addr6,
             sizeof(saddr.sa6.sin6_addr));
#ifdef HAVE_STRUCT_SOCKADDR_IN6_SIN6_SCOPE_ID
      saddr.sa6.sin6_scope_id = server->ll_scope;
#endif
      break;
    default:
      return ARES_EBADFAMILY; /* LCOV_EXCL_LINE */
  }

  /* Acquire a socket. */
  s = ares__open_socket(channel, server->addr.family, type, 0);
  if (s == ARES_SOCKET_BAD) {
    return ARES_ECONNREFUSED;
  }

  /* Configure it. */
  if (configure_socket(s, server) < 0) {
    ares__close_socket(channel, s);
    return ARES_ECONNREFUSED;
  }

#ifdef TCP_NODELAY
  if (is_tcp) {
    /*
     * Disable the Nagle algorithm (only relevant for TCP sockets, and thus not
     * in configure_socket). In general, in DNS lookups we're pretty much
     * interested in firing off a single request and then waiting for a reply,
     * so batching isn't very interesting.
     */
    opt = 1;
    if ((!channel->sock_funcs || !channel->sock_funcs->asocket) &&
        setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (void *)&opt, sizeof(opt)) ==
          -1) {
      ares__close_socket(channel, s); /* LCOV_EXCL_LINE: UntestablePath */
      return ARES_ECONNREFUSED;       /* LCOV_EXCL_LINE: UntestablePath */
    }
  }
#endif

  if (channel->sock_config_cb) {
    int err = channel->sock_config_cb(s, type, channel->sock_config_cb_data);
    if (err < 0) {
      ares__close_socket(channel, s);
      return ARES_ECONNREFUSED;
    }
  }

  /* Connect to the server. */
  if (ares__connect_socket(channel, s, sa, salen) == -1) {
    int err = SOCKERRNO;

    if (err != EINPROGRESS && err != EWOULDBLOCK) {
      ares__close_socket(channel, s);
      return ARES_ECONNREFUSED;
    }
  }

  if (channel->sock_create_cb) {
    int err = channel->sock_create_cb(s, type, channel->sock_create_cb_data);
    if (err < 0) {
      ares__close_socket(channel, s);
      return ARES_ECONNREFUSED;
    }
  }

  conn = ares_malloc(sizeof(*conn));
  if (conn == NULL) {
    ares__close_socket(channel, s); /* LCOV_EXCL_LINE: OutOfMemory */
    return ARES_ENOMEM;             /* LCOV_EXCL_LINE: OutOfMemory */
  }
  memset(conn, 0, sizeof(*conn));
  conn->fd              = s;
  conn->server          = server;
  conn->queries_to_conn = ares__llist_create(NULL);
  conn->is_tcp          = is_tcp;
  if (conn->queries_to_conn == NULL) {
    /* LCOV_EXCL_START: OutOfMemory */
    ares__close_socket(channel, s);
    ares_free(conn);
    return ARES_ENOMEM;
    /* LCOV_EXCL_STOP */
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
    ares__close_socket(channel, s);
    ares__llist_destroy(conn->queries_to_conn);
    ares_free(conn);
    return ARES_ENOMEM;
    /* LCOV_EXCL_STOP */
  }

  /* Register globally to quickly map event on file descriptor to connection
   * node object */
  if (!ares__htable_asvp_insert(channel->connnode_by_socket, s, node)) {
    /* LCOV_EXCL_START: OutOfMemory */
    ares__close_socket(channel, s);
    ares__llist_destroy(conn->queries_to_conn);
    ares__llist_node_claim(node);
    ares_free(conn);
    return ARES_ENOMEM;
    /* LCOV_EXCL_STOP */
  }

  SOCK_STATE_CALLBACK(channel, s, 1, 0);

  if (is_tcp) {
    server->tcp_conn = conn;
  }

  return ARES_SUCCESS;
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

int ares__connect_socket(ares_channel_t *channel, ares_socket_t sockfd,
                         const struct sockaddr *addr, ares_socklen_t addrlen)
{
  if (channel->sock_funcs && channel->sock_funcs->aconnect) {
    return channel->sock_funcs->aconnect(sockfd, addr, addrlen,
                                         channel->sock_func_cb_data);
  }

  return connect(sockfd, addr, addrlen);
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

#ifndef HAVE_WRITEV
/* Structure for scatter/gather I/O. */
struct iovec {
  void  *iov_base; /* Pointer to data. */
  size_t iov_len;  /* Length of data.  */
};
#endif

ares_ssize_t ares__socket_write(ares_channel_t *channel, ares_socket_t s,
                                const void *data, size_t len)
{
  if (channel->sock_funcs && channel->sock_funcs->asendv) {
    struct iovec vec;
    vec.iov_base = (void *)((size_t)data); /* Cast off const */
    vec.iov_len  = len;
    return channel->sock_funcs->asendv(s, &vec, 1, channel->sock_func_cb_data);
  }
  return swrite(s, data, len);
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
