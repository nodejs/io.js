/* MIT License
 *
 * Copyright (c) 2024 Brad House
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


#if defined(__linux__) && defined(TCP_FASTOPEN_CONNECT)
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

ares_status_t
  ares_set_socket_functions_ex(ares_channel_t                        *channel,
                               const struct ares_socket_functions_ex *funcs,
                               void                                  *user_data)
{
  unsigned int known_versions[] = { 1 };
  size_t       i;

  if (channel == NULL || funcs == NULL) {
    return ARES_EFORMERR;
  }

  /* Check to see if we know the version referenced */
  for (i = 0; i < sizeof(known_versions) / sizeof(*known_versions); i++) {
    if (funcs->version == known_versions[i]) {
      break;
    }
  }
  if (i == sizeof(known_versions) / sizeof(*known_versions)) {
    return ARES_EFORMERR;
  }

  memset(&channel->sock_funcs, 0, sizeof(channel->sock_funcs));

  /* Copy individually for ABI compliance.  memcpy() with a sizeof would do
   * invalid reads */
  if (funcs->version >= 1) {
    if (funcs->asocket == NULL || funcs->aclose == NULL ||
        funcs->asetsockopt == NULL || funcs->aconnect == NULL ||
        funcs->arecvfrom == NULL || funcs->asendto == NULL) {
      return ARES_EFORMERR;
    }
    channel->sock_funcs.version      = funcs->version;
    channel->sock_funcs.flags        = funcs->flags;
    channel->sock_funcs.asocket      = funcs->asocket;
    channel->sock_funcs.aclose       = funcs->aclose;
    channel->sock_funcs.asetsockopt  = funcs->asetsockopt;
    channel->sock_funcs.aconnect     = funcs->aconnect;
    channel->sock_funcs.arecvfrom    = funcs->arecvfrom;
    channel->sock_funcs.asendto      = funcs->asendto;
    channel->sock_funcs.agetsockname = funcs->agetsockname;
    channel->sock_funcs.abind        = funcs->abind;
  }

  /* Implement newer versions here ...*/


  channel->sock_func_cb_data = user_data;

  return ARES_SUCCESS;
}

static int setsocknonblock(ares_socket_t sockfd, /* operate on this */
                           int           nonblock /* TRUE or FALSE */)
{
#if defined(HAVE_FCNTL_O_NONBLOCK)

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

static int default_aclose(ares_socket_t sock, void *user_data)
{
  (void)user_data;

#if defined(HAVE_CLOSESOCKET)
  return closesocket(sock);
#elif defined(HAVE_CLOSESOCKET_CAMEL)
  return CloseSocket(sock);
#elif defined(HAVE_CLOSE_S)
  return close_s(sock);
#else
  return close(sock);
#endif
}

static ares_socket_t default_asocket(int domain, int type, int protocol,
                                     void *user_data)
{
  ares_socket_t s;
  (void)user_data;

  s = socket(domain, type, protocol);
  if (s == ARES_SOCKET_BAD) {
    return s;
  }

  if (setsocknonblock(s, 1) != 0) {
    goto fail; /* LCOV_EXCL_LINE */
  }

#if defined(FD_CLOEXEC) && !defined(MSDOS)
  /* Configure the socket fd as close-on-exec. */
  if (fcntl(s, F_SETFD, FD_CLOEXEC) != 0) {
    goto fail; /* LCOV_EXCL_LINE */
  }
#endif

  /* No need to emit SIGPIPE on socket errors */
#if defined(SO_NOSIGPIPE)
  {
    int opt = 1;
    (void)setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, (void *)&opt, sizeof(opt));
  }
#endif


  if (type == SOCK_STREAM) {
    int opt = 1;

#ifdef TCP_NODELAY
    /*
     * Disable the Nagle algorithm (only relevant for TCP sockets, and thus not
     * in configure_socket). In general, in DNS lookups we're pretty much
     * interested in firing off a single request and then waiting for a reply,
     * so batching isn't very interesting.
     */
    if (setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (void *)&opt, sizeof(opt)) !=
        0) {
      goto fail;
    }
#endif
  }

#if defined(IPV6_V6ONLY) && defined(USE_WINSOCK)
  /* Support for IPv4-mapped IPv6 addresses.
   * Linux kernel, NetBSD, FreeBSD and Darwin: default is off;
   * Windows Vista and later: default is on;
   * DragonFly BSD: acts like off, and dummy setting;
   * OpenBSD and earlier Windows: unsupported.
   * Linux: controlled by /proc/sys/net/ipv6/bindv6only.
   */
  if (domain == PF_INET6) {
    int on = 0;
    (void)setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, (void *)&on, sizeof(on));
  }
#endif

  return s;

fail:
  default_aclose(s, user_data);
  return ARES_SOCKET_BAD;
}

static int default_asetsockopt(ares_socket_t sock, ares_socket_opt_t opt,
                               const void *val, ares_socklen_t val_size,
                               void *user_data)
{
  switch (opt) {
    case ARES_SOCKET_OPT_SENDBUF_SIZE:
      if (val_size != sizeof(int)) {
        SET_SOCKERRNO(EINVAL);
        return -1;
      }
      return setsockopt(sock, SOL_SOCKET, SO_SNDBUF, val, val_size);

    case ARES_SOCKET_OPT_RECVBUF_SIZE:
      if (val_size != sizeof(int)) {
        SET_SOCKERRNO(EINVAL);
        return -1;
      }
      return setsockopt(sock, SOL_SOCKET, SO_RCVBUF, val, val_size);

    case ARES_SOCKET_OPT_BIND_DEVICE:
      if (!ares_str_isprint(val, (size_t)val_size)) {
        SET_SOCKERRNO(EINVAL);
        return -1;
      }
#ifdef SO_BINDTODEVICE
      return setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE, val, val_size);
#else
      SET_SOCKERRNO(ENOSYS);
      return -1;
#endif

    case ARES_SOCKET_OPT_TCP_FASTOPEN:
      if (val_size != sizeof(ares_bool_t)) {
        SET_SOCKERRNO(EINVAL);
        return -1;
      }
#if defined(TFO_CLIENT_SOCKOPT)
      {
        int                oval;
        const ares_bool_t *pval = val;
        oval                    = (int)*pval;
        return setsockopt(sock, IPPROTO_TCP, TFO_CLIENT_SOCKOPT, (void *)&oval,
                          sizeof(oval));
      }
#elif TFO_SUPPORTED
      return 0;
#else
      SET_SOCKERRNO(ENOSYS);
      return -1;
#endif
  }

  (void)user_data;
  SET_SOCKERRNO(ENOSYS);
  return -1;
}

static int default_aconnect(ares_socket_t sock, const struct sockaddr *address,
                            ares_socklen_t address_len, unsigned int flags,
                            void *user_data)
{
  (void)user_data;

#if defined(TFO_SKIP_CONNECT) && TFO_SKIP_CONNECT
  if (flags & ARES_SOCKET_CONN_TCP_FASTOPEN) {
    return 0;
  }
  return connect(sock, address, address_len);
#elif defined(TFO_USE_CONNECTX) && TFO_USE_CONNECTX
  if (flags & ARES_SOCKET_CONN_TCP_FASTOPEN) {
    sa_endpoints_t endpoints;

    memset(&endpoints, 0, sizeof(endpoints));
    endpoints.sae_dstaddr    = address;
    endpoints.sae_dstaddrlen = address_len;

    return connectx(sock, &endpoints, SAE_ASSOCID_ANY,
                    CONNECT_DATA_IDEMPOTENT | CONNECT_RESUME_ON_READ_WRITE,
                    NULL, 0, NULL, NULL);
  } else {
    return connect(sock, address, address_len);
  }
#else
  (void)flags;
  return connect(sock, address, address_len);
#endif
}

static ares_ssize_t default_arecvfrom(ares_socket_t sock, void *buffer,
                                      size_t length, int flags,
                                      struct sockaddr *address,
                                      ares_socklen_t  *address_len,
                                      void            *user_data)
{
  (void)user_data;

#ifdef HAVE_RECVFROM
  return (ares_ssize_t)recvfrom(sock, buffer, (RECVFROM_TYPE_ARG3)length, flags,
                                address, address_len);
#else
  if (address != NULL && address_len != NULL) {
    memset(address, 0, (size_t)*address_len);
    address->sa_family = AF_UNSPEC;
  }
  return (ares_ssize_t)recv(sock, buffer, (RECVFROM_TYPE_ARG3)length, flags);
#endif
}

static ares_ssize_t default_asendto(ares_socket_t sock, const void *buffer,
                                    size_t length, int flags,
                                    const struct sockaddr *address,
                                    ares_socklen_t address_len, void *user_data)
{
  (void)user_data;

  if (address != NULL) {
#ifdef HAVE_SENDTO
    return (ares_ssize_t)sendto((SEND_TYPE_ARG1)sock, (SEND_TYPE_ARG2)buffer,
                                (SEND_TYPE_ARG3)length, (SEND_TYPE_ARG4)flags,
                                address, address_len);
#else
    (void)address_len;
#endif
  }

  return (ares_ssize_t)send((SEND_TYPE_ARG1)sock, (SEND_TYPE_ARG2)buffer,
                            (SEND_TYPE_ARG3)length, (SEND_TYPE_ARG4)flags);
}

static int default_agetsockname(ares_socket_t sock, struct sockaddr *address,
                                ares_socklen_t *address_len, void *user_data)
{
  (void)user_data;
  return getsockname(sock, address, address_len);
}

static int default_abind(ares_socket_t sock, unsigned int flags,
                         const struct sockaddr *address, socklen_t address_len,
                         void *user_data)
{
  (void)user_data;

#ifdef IP_BIND_ADDRESS_NO_PORT
  if (flags & ARES_SOCKET_BIND_TCP && flags & ARES_SOCKET_BIND_CLIENT) {
    int opt = 1;
    (void)setsockopt(sock, SOL_IP, IP_BIND_ADDRESS_NO_PORT, &opt, sizeof(opt));
  }
#else
  (void)flags;
#endif

  return bind(sock, address, address_len);
}

static unsigned int default_aif_nametoindex(const char *ifname, void *user_data)
{
  (void)user_data;
  return ares_os_if_nametoindex(ifname);
}

static const char *default_aif_indextoname(unsigned int ifindex,
                                           char        *ifname_buf,
                                           size_t       ifname_buf_len,
                                           void        *user_data)
{
  (void)user_data;
  return ares_os_if_indextoname(ifindex, ifname_buf, ifname_buf_len);
}

static const struct ares_socket_functions_ex default_socket_functions = {
  1,
  ARES_SOCKFUNC_FLAG_NONBLOCKING,
  default_asocket,
  default_aclose,
  default_asetsockopt,
  default_aconnect,
  default_arecvfrom,
  default_asendto,
  default_agetsockname,
  default_abind,
  default_aif_nametoindex,
  default_aif_indextoname
};

void ares_set_socket_functions_def(ares_channel_t *channel)
{
  ares_set_socket_functions_ex(channel, &default_socket_functions, NULL);
}

static int legacycb_aclose(ares_socket_t sock, void *user_data)
{
  ares_channel_t *channel = user_data;

  if (channel->legacy_sock_funcs != NULL &&
      channel->legacy_sock_funcs->aclose != NULL) {
    return channel->legacy_sock_funcs->aclose(
      sock, channel->legacy_sock_funcs_cb_data);
  }

  return default_aclose(sock, NULL);
}

static ares_socket_t legacycb_asocket(int domain, int type, int protocol,
                                      void *user_data)
{
  ares_channel_t *channel = user_data;

  if (channel->legacy_sock_funcs != NULL &&
      channel->legacy_sock_funcs->asocket != NULL) {
    return channel->legacy_sock_funcs->asocket(
      domain, type, protocol, channel->legacy_sock_funcs_cb_data);
  }

  return default_asocket(domain, type, protocol, NULL);
}

static int legacycb_asetsockopt(ares_socket_t sock, ares_socket_opt_t opt,
                                const void *val, ares_socklen_t val_size,
                                void *user_data)
{
  (void)sock;
  (void)opt;
  (void)val;
  (void)val_size;
  (void)user_data;
  SET_SOCKERRNO(ENOSYS);
  return -1;
}

static int legacycb_aconnect(ares_socket_t sock, const struct sockaddr *address,
                             ares_socklen_t address_len, unsigned int flags,
                             void *user_data)
{
  ares_channel_t *channel = user_data;

  if (channel->legacy_sock_funcs != NULL &&
      channel->legacy_sock_funcs->aconnect != NULL) {
    return channel->legacy_sock_funcs->aconnect(
      sock, address, address_len, channel->legacy_sock_funcs_cb_data);
  }

  return default_aconnect(sock, address, address_len, flags, NULL);
}

static ares_ssize_t legacycb_arecvfrom(ares_socket_t sock, void *buffer,
                                       size_t length, int flags,
                                       struct sockaddr *address,
                                       ares_socklen_t  *address_len,
                                       void            *user_data)
{
  ares_channel_t *channel = user_data;

  if (channel->legacy_sock_funcs != NULL &&
      channel->legacy_sock_funcs->arecvfrom != NULL) {
    if (address != NULL && address_len != NULL) {
      memset(address, 0, (size_t)*address_len);
      address->sa_family = AF_UNSPEC;
    }
    return channel->legacy_sock_funcs->arecvfrom(
      sock, buffer, length, flags, address, address_len,
      channel->legacy_sock_funcs_cb_data);
  }

  return default_arecvfrom(sock, buffer, length, flags, address, address_len,
                           NULL);
}

static ares_ssize_t legacycb_asendto(ares_socket_t sock, const void *buffer,
                                     size_t length, int flags,
                                     const struct sockaddr *address,
                                     ares_socklen_t         address_len,
                                     void                  *user_data)
{
  ares_channel_t *channel = user_data;

  if (channel->legacy_sock_funcs != NULL &&
      channel->legacy_sock_funcs->asendv != NULL) {
    struct iovec vec;
    vec.iov_base = (void *)((size_t)buffer); /* Cast off const */
    vec.iov_len  = length;
    return channel->legacy_sock_funcs->asendv(
      sock, &vec, 1, channel->legacy_sock_funcs_cb_data);
  }

  return default_asendto(sock, buffer, length, flags, address, address_len,
                         NULL);
}


static const struct ares_socket_functions_ex legacy_socket_functions = {
  1,
  0,
  legacycb_asocket,
  legacycb_aclose,
  legacycb_asetsockopt,
  legacycb_aconnect,
  legacycb_arecvfrom,
  legacycb_asendto,
  NULL, /* agetsockname */
  NULL, /* abind */
  NULL, /* aif_nametoindex */
  NULL  /* aif_indextoname */
};

void ares_set_socket_functions(ares_channel_t                     *channel,
                               const struct ares_socket_functions *funcs,
                               void                               *data)
{
  if (channel == NULL || channel->optmask & ARES_OPT_EVENT_THREAD) {
    return;
  }

  channel->legacy_sock_funcs         = funcs;
  channel->legacy_sock_funcs_cb_data = data;
  ares_set_socket_functions_ex(channel, &legacy_socket_functions, channel);
}
