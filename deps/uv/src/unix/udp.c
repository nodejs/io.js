/* Copyright Joyent, Inc. and other Node contributors. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "uv.h"
#include "internal.h"

#include <assert.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#if defined(__MVS__)
#include <xti.h>
#endif
#include <sys/un.h>

#if defined(IPV6_JOIN_GROUP) && !defined(IPV6_ADD_MEMBERSHIP)
# define IPV6_ADD_MEMBERSHIP IPV6_JOIN_GROUP
#endif

#if defined(IPV6_LEAVE_GROUP) && !defined(IPV6_DROP_MEMBERSHIP)
# define IPV6_DROP_MEMBERSHIP IPV6_LEAVE_GROUP
#endif

static void uv__udp_run_completed(uv_udp_t* handle);
static void uv__udp_io(uv_loop_t* loop, uv__io_t* w, unsigned int revents);
static void uv__udp_recvmsg(uv_udp_t* handle);
static void uv__udp_sendmsg(uv_udp_t* handle);
static int uv__udp_maybe_deferred_bind(uv_udp_t* handle,
                                       int domain,
                                       unsigned int flags);
static int uv__udp_sendmsg1(int fd,
                            const uv_buf_t* bufs,
                            unsigned int nbufs,
                            const struct sockaddr* addr);


void uv__udp_close(uv_udp_t* handle) {
  uv__io_close(handle->loop, &handle->io_watcher);
  uv__handle_stop(handle);

  if (handle->io_watcher.fd != -1) {
    uv__close(handle->io_watcher.fd);
    handle->io_watcher.fd = -1;
  }
}


void uv__udp_finish_close(uv_udp_t* handle) {
  uv_udp_send_t* req;
  struct uv__queue* q;

  assert(!uv__io_active(&handle->io_watcher, POLLIN | POLLOUT));
  assert(handle->io_watcher.fd == -1);

  while (!uv__queue_empty(&handle->write_queue)) {
    q = uv__queue_head(&handle->write_queue);
    uv__queue_remove(q);

    req = uv__queue_data(q, uv_udp_send_t, queue);
    req->status = UV_ECANCELED;
    uv__queue_insert_tail(&handle->write_completed_queue, &req->queue);
  }

  uv__udp_run_completed(handle);

  assert(handle->send_queue_size == 0);
  assert(handle->send_queue_count == 0);

  /* Now tear down the handle. */
  handle->recv_cb = NULL;
  handle->alloc_cb = NULL;
  /* but _do not_ touch close_cb */
}


static void uv__udp_run_completed(uv_udp_t* handle) {
  uv_udp_send_t* req;
  struct uv__queue* q;

  assert(!(handle->flags & UV_HANDLE_UDP_PROCESSING));
  handle->flags |= UV_HANDLE_UDP_PROCESSING;

  while (!uv__queue_empty(&handle->write_completed_queue)) {
    q = uv__queue_head(&handle->write_completed_queue);
    uv__queue_remove(q);

    req = uv__queue_data(q, uv_udp_send_t, queue);
    uv__req_unregister(handle->loop);

    handle->send_queue_size -= uv__count_bufs(req->bufs, req->nbufs);
    handle->send_queue_count--;

    if (req->bufs != req->bufsml)
      uv__free(req->bufs);
    req->bufs = NULL;

    if (req->send_cb == NULL)
      continue;

    /* req->status >= 0 == bytes written
     * req->status <  0 == errno
     */
    if (req->status >= 0)
      req->send_cb(req, 0);
    else
      req->send_cb(req, req->status);
  }

  if (uv__queue_empty(&handle->write_queue)) {
    /* Pending queue and completion queue empty, stop watcher. */
    uv__io_stop(handle->loop, &handle->io_watcher, POLLOUT);
    if (!uv__io_active(&handle->io_watcher, POLLIN))
      uv__handle_stop(handle);
  }

  handle->flags &= ~UV_HANDLE_UDP_PROCESSING;
}


static void uv__udp_io(uv_loop_t* loop, uv__io_t* w, unsigned int revents) {
  uv_udp_t* handle;

  handle = container_of(w, uv_udp_t, io_watcher);
  assert(handle->type == UV_UDP);

  if (revents & POLLIN)
    uv__udp_recvmsg(handle);

  if (revents & POLLOUT && !uv__is_closing(handle)) {
    uv__udp_sendmsg(handle);
    uv__udp_run_completed(handle);
  }
}

static int uv__udp_recvmmsg(uv_udp_t* handle, uv_buf_t* buf) {
#if defined(__linux__) || defined(__FreeBSD__) || defined(__APPLE__)
  struct sockaddr_in6 peers[20];
  struct iovec iov[ARRAY_SIZE(peers)];
  struct mmsghdr msgs[ARRAY_SIZE(peers)];
  ssize_t nread;
  uv_buf_t chunk_buf;
  size_t chunks;
  int flags;
  size_t k;

  /* prepare structures for recvmmsg */
  chunks = buf->len / UV__UDP_DGRAM_MAXSIZE;
  if (chunks > ARRAY_SIZE(iov))
    chunks = ARRAY_SIZE(iov);
  for (k = 0; k < chunks; ++k) {
    iov[k].iov_base = buf->base + k * UV__UDP_DGRAM_MAXSIZE;
    iov[k].iov_len = UV__UDP_DGRAM_MAXSIZE;
    memset(&msgs[k].msg_hdr, 0, sizeof(msgs[k].msg_hdr));
    msgs[k].msg_hdr.msg_iov = iov + k;
    msgs[k].msg_hdr.msg_iovlen = 1;
    msgs[k].msg_hdr.msg_name = peers + k;
    msgs[k].msg_hdr.msg_namelen = sizeof(peers[0]);
    msgs[k].msg_hdr.msg_control = NULL;
    msgs[k].msg_hdr.msg_controllen = 0;
    msgs[k].msg_hdr.msg_flags = 0;
    msgs[k].msg_len = 0;
  }

#if defined(__APPLE__)
  do
    nread = recvmsg_x(handle->io_watcher.fd, msgs, chunks, MSG_DONTWAIT);
  while (nread == -1 && errno == EINTR);
#else
  do
    nread = recvmmsg(handle->io_watcher.fd, msgs, chunks, 0, NULL);
  while (nread == -1 && errno == EINTR);
#endif

  if (nread < 1) {
    if (nread == 0 || errno == EAGAIN || errno == EWOULDBLOCK)
      handle->recv_cb(handle, 0, buf, NULL, 0);
    else
      handle->recv_cb(handle, UV__ERR(errno), buf, NULL, 0);
  } else {
    /* pass each chunk to the application */
    for (k = 0; k < (size_t) nread && handle->recv_cb != NULL; k++) {
      flags = UV_UDP_MMSG_CHUNK;
      if (msgs[k].msg_hdr.msg_flags & MSG_TRUNC)
        flags |= UV_UDP_PARTIAL;

      chunk_buf = uv_buf_init(iov[k].iov_base, iov[k].iov_len);
      handle->recv_cb(handle,
                      msgs[k].msg_len,
                      &chunk_buf,
                      msgs[k].msg_hdr.msg_name,
                      flags);
    }

    /* one last callback so the original buffer is freed */
    if (handle->recv_cb != NULL)
      handle->recv_cb(handle, 0, buf, NULL, UV_UDP_MMSG_FREE);
  }
  return nread;
#else  /* __linux__ || ____FreeBSD__ || __APPLE__ */
  return UV_ENOSYS;
#endif  /* __linux__ || ____FreeBSD__ || __APPLE__ */
}

static void uv__udp_recvmsg(uv_udp_t* handle) {
  struct sockaddr_storage peer;
  struct msghdr h;
  ssize_t nread;
  uv_buf_t buf;
  int flags;
  int count;

  assert(handle->recv_cb != NULL);
  assert(handle->alloc_cb != NULL);

  /* Prevent loop starvation when the data comes in as fast as (or faster than)
   * we can read it. XXX Need to rearm fd if we switch to edge-triggered I/O.
   */
  count = 32;

  do {
    buf = uv_buf_init(NULL, 0);
    handle->alloc_cb((uv_handle_t*) handle, UV__UDP_DGRAM_MAXSIZE, &buf);
    if (buf.base == NULL || buf.len == 0) {
      handle->recv_cb(handle, UV_ENOBUFS, &buf, NULL, 0);
      return;
    }
    assert(buf.base != NULL);

    if (uv_udp_using_recvmmsg(handle)) {
      nread = uv__udp_recvmmsg(handle, &buf);
      if (nread > 0)
        count -= nread;
      continue;
    }

    memset(&h, 0, sizeof(h));
    memset(&peer, 0, sizeof(peer));
    h.msg_name = &peer;
    h.msg_namelen = sizeof(peer);
    h.msg_iov = (void*) &buf;
    h.msg_iovlen = 1;

    do {
      nread = recvmsg(handle->io_watcher.fd, &h, 0);
    }
    while (nread == -1 && errno == EINTR);

    if (nread == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
        handle->recv_cb(handle, 0, &buf, NULL, 0);
      else
        handle->recv_cb(handle, UV__ERR(errno), &buf, NULL, 0);
    }
    else {
      flags = 0;
      if (h.msg_flags & MSG_TRUNC)
        flags |= UV_UDP_PARTIAL;

      handle->recv_cb(handle, nread, &buf, (const struct sockaddr*) &peer, flags);
    }
    count--;
  }
  /* recv_cb callback may decide to pause or close the handle */
  while (nread != -1
      && count > 0
      && handle->io_watcher.fd != -1
      && handle->recv_cb != NULL);
}


/* On the BSDs, SO_REUSEPORT implies SO_REUSEADDR but with some additional
 * refinements for programs that use multicast. Therefore we preferentially
 * set SO_REUSEPORT over SO_REUSEADDR here, but we set SO_REUSEPORT only
 * when that socket option doesn't have the capability of load balancing.
 * Otherwise, we fall back to SO_REUSEADDR.
 *
 * Linux as of 3.9, DragonflyBSD 3.6, AIX 7.2.5 have the SO_REUSEPORT socket
 * option but with semantics that are different from the BSDs: it _shares_
 * the port rather than steals it from the current listener. While useful,
 * it's not something we can emulate on other platforms so we don't enable it.
 *
 * zOS does not support getsockname with SO_REUSEPORT option when using
 * AF_UNIX.
 */
static int uv__sock_reuseaddr(int fd) {
  int yes;
  yes = 1;

#if defined(SO_REUSEPORT) && defined(__MVS__)
  struct sockaddr_in sockfd;
  unsigned int sockfd_len = sizeof(sockfd);
  if (getsockname(fd, (struct sockaddr*) &sockfd, &sockfd_len) == -1)
      return UV__ERR(errno);
  if (sockfd.sin_family == AF_UNIX) {
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)))
      return UV__ERR(errno);
  } else {
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes)))
       return UV__ERR(errno);
  }
#elif defined(SO_REUSEPORT) && !defined(__linux__) && !defined(__GNU__) && \
	!defined(__sun__) && !defined(__DragonFly__) && !defined(_AIX73)
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes)))
    return UV__ERR(errno);
#else
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)))
    return UV__ERR(errno);
#endif

  return 0;
}

/*
 * The Linux kernel suppresses some ICMP error messages by default for UDP
 * sockets. Setting IP_RECVERR/IPV6_RECVERR on the socket enables full ICMP
 * error reporting, hopefully resulting in faster failover to working name
 * servers.
 */
static int uv__set_recverr(int fd, sa_family_t ss_family) {
#if defined(__linux__)
  int yes;

  yes = 1;
  if (ss_family == AF_INET) {
    if (setsockopt(fd, IPPROTO_IP, IP_RECVERR, &yes, sizeof(yes)))
      return UV__ERR(errno);
  } else if (ss_family == AF_INET6) {
    if (setsockopt(fd, IPPROTO_IPV6, IPV6_RECVERR, &yes, sizeof(yes)))
       return UV__ERR(errno);
  }
#endif
  return 0;
}


int uv__udp_bind(uv_udp_t* handle,
                 const struct sockaddr* addr,
                 unsigned int addrlen,
                 unsigned int flags) {
  int err;
  int yes;
  int fd;

  /* Check for bad flags. */
  if (flags & ~(UV_UDP_IPV6ONLY | UV_UDP_REUSEADDR |
                UV_UDP_REUSEPORT | UV_UDP_LINUX_RECVERR))
    return UV_EINVAL;

  /* Cannot set IPv6-only mode on non-IPv6 socket. */
  if ((flags & UV_UDP_IPV6ONLY) && addr->sa_family != AF_INET6)
    return UV_EINVAL;

  fd = handle->io_watcher.fd;
  if (fd == -1) {
    err = uv__socket(addr->sa_family, SOCK_DGRAM, 0);
    if (err < 0)
      return err;
    fd = err;
    handle->io_watcher.fd = fd;
  }

  if (flags & UV_UDP_LINUX_RECVERR) {
    err = uv__set_recverr(fd, addr->sa_family);
    if (err)
      return err;
  }

  if (flags & UV_UDP_REUSEADDR) {
    err = uv__sock_reuseaddr(fd);
    if (err)
      return err;
  }

  if (flags & UV_UDP_REUSEPORT) {
    err = uv__sock_reuseport(fd);
    if (err)
      return err;
  }

  if (flags & UV_UDP_IPV6ONLY) {
#ifdef IPV6_V6ONLY
    yes = 1;
    if (setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &yes, sizeof yes) == -1) {
      err = UV__ERR(errno);
      return err;
    }
#else
    err = UV_ENOTSUP;
    return err;
#endif
  }

  if (bind(fd, addr, addrlen)) {
    err = UV__ERR(errno);
    if (errno == EAFNOSUPPORT)
      /* OSX, other BSDs and SunoS fail with EAFNOSUPPORT when binding a
       * socket created with AF_INET to an AF_INET6 address or vice versa. */
      err = UV_EINVAL;
    return err;
  }

  if (addr->sa_family == AF_INET6)
    handle->flags |= UV_HANDLE_IPV6;

  handle->flags |= UV_HANDLE_BOUND;
  return 0;
}


static int uv__udp_maybe_deferred_bind(uv_udp_t* handle,
                                       int domain,
                                       unsigned int flags) {
  union uv__sockaddr taddr;
  socklen_t addrlen;

  if (handle->io_watcher.fd != -1)
    return 0;

  switch (domain) {
  case AF_INET:
  {
    struct sockaddr_in* addr = &taddr.in;
    memset(addr, 0, sizeof *addr);
    addr->sin_family = AF_INET;
    addr->sin_addr.s_addr = INADDR_ANY;
    addrlen = sizeof *addr;
    break;
  }
  case AF_INET6:
  {
    struct sockaddr_in6* addr = &taddr.in6;
    memset(addr, 0, sizeof *addr);
    addr->sin6_family = AF_INET6;
    addr->sin6_addr = in6addr_any;
    addrlen = sizeof *addr;
    break;
  }
  default:
    assert(0 && "unsupported address family");
    abort();
  }

  return uv__udp_bind(handle, &taddr.addr, addrlen, flags);
}


int uv__udp_connect(uv_udp_t* handle,
                    const struct sockaddr* addr,
                    unsigned int addrlen) {
  int err;

  err = uv__udp_maybe_deferred_bind(handle, addr->sa_family, 0);
  if (err)
    return err;

  do {
    errno = 0;
    err = connect(handle->io_watcher.fd, addr, addrlen);
  } while (err == -1 && errno == EINTR);

  if (err)
    return UV__ERR(errno);

  handle->flags |= UV_HANDLE_UDP_CONNECTED;

  return 0;
}

/* From https://pubs.opengroup.org/onlinepubs/9699919799/functions/connect.html
 * Any of uv supported UNIXs kernel should be standardized, but the kernel
 * implementation logic not same, let's use pseudocode to explain the udp
 * disconnect behaviors:
 *
 * Predefined stubs for pseudocode:
 *   1. sodisconnect: The function to perform the real udp disconnect
 *   2. pru_connect: The function to perform the real udp connect
 *   3. so: The kernel object match with socket fd
 *   4. addr: The sockaddr parameter from user space
 *
 * BSDs:
 *   if(sodisconnect(so) == 0) { // udp disconnect succeed
 *     if (addr->sa_len != so->addr->sa_len) return EINVAL;
 *     if (addr->sa_family != so->addr->sa_family) return EAFNOSUPPORT;
 *     pru_connect(so);
 *   }
 *   else return EISCONN;
 *
 * z/OS (same with Windows):
 *   if(addr->sa_len < so->addr->sa_len) return EINVAL;
 *   if (addr->sa_family == AF_UNSPEC) sodisconnect(so);
 *
 * AIX:
 *   if(addr->sa_len != sizeof(struct sockaddr)) return EINVAL; // ignore ip proto version
 *   if (addr->sa_family == AF_UNSPEC) sodisconnect(so);
 *
 * Linux,Others:
 *   if(addr->sa_len < sizeof(struct sockaddr)) return EINVAL;
 *   if (addr->sa_family == AF_UNSPEC) sodisconnect(so);
 */
int uv__udp_disconnect(uv_udp_t* handle) {
    int r;
#if defined(__MVS__)
    struct sockaddr_storage addr;
#else
    struct sockaddr addr;
#endif

    memset(&addr, 0, sizeof(addr));

#if defined(__MVS__)
    addr.ss_family = AF_UNSPEC;
#else
    addr.sa_family = AF_UNSPEC;
#endif

    do {
      errno = 0;
#ifdef __PASE__
      /* On IBMi a connectionless transport socket can be disconnected by
       * either setting the addr parameter to NULL or setting the
       * addr_length parameter to zero, and issuing another connect().
       * https://www.ibm.com/docs/en/i/7.4?topic=ssw_ibm_i_74/apis/connec.htm
       */
      r = connect(handle->io_watcher.fd, (struct sockaddr*) NULL, 0);
#else
      r = connect(handle->io_watcher.fd, (struct sockaddr*) &addr, sizeof(addr));
#endif
    } while (r == -1 && errno == EINTR);

    if (r == -1) {
#if defined(BSD)  /* The macro BSD is from sys/param.h */
      if (errno != EAFNOSUPPORT && errno != EINVAL)
        return UV__ERR(errno);
#else
      return UV__ERR(errno);
#endif
    }

    handle->flags &= ~UV_HANDLE_UDP_CONNECTED;
    return 0;
}

int uv__udp_send(uv_udp_send_t* req,
                 uv_udp_t* handle,
                 const uv_buf_t bufs[],
                 unsigned int nbufs,
                 const struct sockaddr* addr,
                 unsigned int addrlen,
                 uv_udp_send_cb send_cb) {
  int err;
  int empty_queue;

  assert(nbufs > 0);

  if (addr) {
    err = uv__udp_maybe_deferred_bind(handle, addr->sa_family, 0);
    if (err)
      return err;
  }

  /* It's legal for send_queue_count > 0 even when the write_queue is empty;
   * it means there are error-state requests in the write_completed_queue that
   * will touch up send_queue_size/count later.
   */
  empty_queue = (handle->send_queue_count == 0);

  uv__req_init(handle->loop, req, UV_UDP_SEND);
  assert(addrlen <= sizeof(req->u.storage));
  if (addr == NULL)
    req->u.storage.ss_family = AF_UNSPEC;
  else
    memcpy(&req->u.storage, addr, addrlen);
  req->send_cb = send_cb;
  req->handle = handle;
  req->nbufs = nbufs;

  req->bufs = req->bufsml;
  if (nbufs > ARRAY_SIZE(req->bufsml))
    req->bufs = uv__malloc(nbufs * sizeof(bufs[0]));

  if (req->bufs == NULL) {
    uv__req_unregister(handle->loop);
    return UV_ENOMEM;
  }

  memcpy(req->bufs, bufs, nbufs * sizeof(bufs[0]));
  handle->send_queue_size += uv__count_bufs(req->bufs, req->nbufs);
  handle->send_queue_count++;
  uv__queue_insert_tail(&handle->write_queue, &req->queue);
  uv__handle_start(handle);

  if (empty_queue && !(handle->flags & UV_HANDLE_UDP_PROCESSING)) {
    uv__udp_sendmsg(handle);

    /* `uv__udp_sendmsg` may not be able to do non-blocking write straight
     * away. In such cases the `io_watcher` has to be queued for asynchronous
     * write.
     */
    if (!uv__queue_empty(&handle->write_queue))
      uv__io_start(handle->loop, &handle->io_watcher, POLLOUT);
  } else {
    uv__io_start(handle->loop, &handle->io_watcher, POLLOUT);
  }

  return 0;
}


int uv__udp_try_send(uv_udp_t* handle,
                     const uv_buf_t bufs[],
                     unsigned int nbufs,
                     const struct sockaddr* addr,
                     unsigned int addrlen) {
  int err;

  if (nbufs < 1)
    return UV_EINVAL;

  /* already sending a message */
  if (handle->send_queue_count != 0)
    return UV_EAGAIN;

  if (addr) {
    err = uv__udp_maybe_deferred_bind(handle, addr->sa_family, 0);
    if (err)
      return err;
  } else {
    assert(handle->flags & UV_HANDLE_UDP_CONNECTED);
  }

  err = uv__udp_sendmsg1(handle->io_watcher.fd, bufs, nbufs, addr);
  if (err > 0)
    return uv__count_bufs(bufs, nbufs);

  return err;
}


static int uv__udp_set_membership4(uv_udp_t* handle,
                                   const struct sockaddr_in* multicast_addr,
                                   const char* interface_addr,
                                   uv_membership membership) {
  struct ip_mreq mreq;
  int optname;
  int err;

  memset(&mreq, 0, sizeof mreq);

  if (interface_addr) {
    err = uv_inet_pton(AF_INET, interface_addr, &mreq.imr_interface.s_addr);
    if (err)
      return err;
  } else {
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
  }

  mreq.imr_multiaddr.s_addr = multicast_addr->sin_addr.s_addr;

  switch (membership) {
  case UV_JOIN_GROUP:
    optname = IP_ADD_MEMBERSHIP;
    break;
  case UV_LEAVE_GROUP:
    optname = IP_DROP_MEMBERSHIP;
    break;
  default:
    return UV_EINVAL;
  }

  if (setsockopt(handle->io_watcher.fd,
                 IPPROTO_IP,
                 optname,
                 &mreq,
                 sizeof(mreq))) {
#if defined(__MVS__)
  if (errno == ENXIO)
    return UV_ENODEV;
#endif
    return UV__ERR(errno);
  }

  return 0;
}


static int uv__udp_set_membership6(uv_udp_t* handle,
                                   const struct sockaddr_in6* multicast_addr,
                                   const char* interface_addr,
                                   uv_membership membership) {
  int optname;
  struct ipv6_mreq mreq;
  struct sockaddr_in6 addr6;

  memset(&mreq, 0, sizeof mreq);

  if (interface_addr) {
    if (uv_ip6_addr(interface_addr, 0, &addr6))
      return UV_EINVAL;
    mreq.ipv6mr_interface = addr6.sin6_scope_id;
  } else {
    mreq.ipv6mr_interface = 0;
  }

  mreq.ipv6mr_multiaddr = multicast_addr->sin6_addr;

  switch (membership) {
  case UV_JOIN_GROUP:
    optname = IPV6_ADD_MEMBERSHIP;
    break;
  case UV_LEAVE_GROUP:
    optname = IPV6_DROP_MEMBERSHIP;
    break;
  default:
    return UV_EINVAL;
  }

  if (setsockopt(handle->io_watcher.fd,
                 IPPROTO_IPV6,
                 optname,
                 &mreq,
                 sizeof(mreq))) {
#if defined(__MVS__)
  if (errno == ENXIO)
    return UV_ENODEV;
#endif
    return UV__ERR(errno);
  }

  return 0;
}


#if !defined(__OpenBSD__) &&                                        \
    !defined(__NetBSD__) &&                                         \
    !defined(__ANDROID__) &&                                        \
    !defined(__DragonFly__) &&                                      \
    !defined(__QNX__) &&                                            \
    !defined(__GNU__)
static int uv__udp_set_source_membership4(uv_udp_t* handle,
                                          const struct sockaddr_in* multicast_addr,
                                          const char* interface_addr,
                                          const struct sockaddr_in* source_addr,
                                          uv_membership membership) {
  struct ip_mreq_source mreq;
  int optname;
  int err;

  err = uv__udp_maybe_deferred_bind(handle, AF_INET, UV_UDP_REUSEADDR);
  if (err)
    return err;

  memset(&mreq, 0, sizeof(mreq));

  if (interface_addr != NULL) {
    err = uv_inet_pton(AF_INET, interface_addr, &mreq.imr_interface.s_addr);
    if (err)
      return err;
  } else {
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
  }

  mreq.imr_multiaddr.s_addr = multicast_addr->sin_addr.s_addr;
  mreq.imr_sourceaddr.s_addr = source_addr->sin_addr.s_addr;

  if (membership == UV_JOIN_GROUP)
    optname = IP_ADD_SOURCE_MEMBERSHIP;
  else if (membership == UV_LEAVE_GROUP)
    optname = IP_DROP_SOURCE_MEMBERSHIP;
  else
    return UV_EINVAL;

  if (setsockopt(handle->io_watcher.fd,
                 IPPROTO_IP,
                 optname,
                 &mreq,
                 sizeof(mreq))) {
    return UV__ERR(errno);
  }

  return 0;
}


static int uv__udp_set_source_membership6(uv_udp_t* handle,
                                          const struct sockaddr_in6* multicast_addr,
                                          const char* interface_addr,
                                          const struct sockaddr_in6* source_addr,
                                          uv_membership membership) {
  struct group_source_req mreq;
  struct sockaddr_in6 addr6;
  int optname;
  int err;

  err = uv__udp_maybe_deferred_bind(handle, AF_INET6, UV_UDP_REUSEADDR);
  if (err)
    return err;

  memset(&mreq, 0, sizeof(mreq));

  if (interface_addr != NULL) {
    err = uv_ip6_addr(interface_addr, 0, &addr6);
    if (err)
      return err;
    mreq.gsr_interface = addr6.sin6_scope_id;
  } else {
    mreq.gsr_interface = 0;
  }

  STATIC_ASSERT(sizeof(mreq.gsr_group) >= sizeof(*multicast_addr));
  STATIC_ASSERT(sizeof(mreq.gsr_source) >= sizeof(*source_addr));
  memcpy(&mreq.gsr_group, multicast_addr, sizeof(*multicast_addr));
  memcpy(&mreq.gsr_source, source_addr, sizeof(*source_addr));

  if (membership == UV_JOIN_GROUP)
    optname = MCAST_JOIN_SOURCE_GROUP;
  else if (membership == UV_LEAVE_GROUP)
    optname = MCAST_LEAVE_SOURCE_GROUP;
  else
    return UV_EINVAL;

  if (setsockopt(handle->io_watcher.fd,
                 IPPROTO_IPV6,
                 optname,
                 &mreq,
                 sizeof(mreq))) {
    return UV__ERR(errno);
  }

  return 0;
}
#endif


int uv__udp_init_ex(uv_loop_t* loop,
                    uv_udp_t* handle,
                    unsigned flags,
                    int domain) {
  int fd;

  fd = -1;
  if (domain != AF_UNSPEC) {
    fd = uv__socket(domain, SOCK_DGRAM, 0);
    if (fd < 0)
      return fd;
  }

  uv__handle_init(loop, (uv_handle_t*)handle, UV_UDP);
  handle->alloc_cb = NULL;
  handle->recv_cb = NULL;
  handle->send_queue_size = 0;
  handle->send_queue_count = 0;
  uv__io_init(&handle->io_watcher, uv__udp_io, fd);
  uv__queue_init(&handle->write_queue);
  uv__queue_init(&handle->write_completed_queue);

  return 0;
}


int uv_udp_using_recvmmsg(const uv_udp_t* handle) {
#if defined(__linux__) || defined(__FreeBSD__) || defined(__APPLE__)
  if (handle->flags & UV_HANDLE_UDP_RECVMMSG)
    return 1;
#endif
  return 0;
}


int uv_udp_open(uv_udp_t* handle, uv_os_sock_t sock) {
  int err;

  /* Check for already active socket. */
  if (handle->io_watcher.fd != -1)
    return UV_EBUSY;

  if (uv__fd_exists(handle->loop, sock))
    return UV_EEXIST;

  err = uv__nonblock(sock, 1);
  if (err)
    return err;

  err = uv__sock_reuseaddr(sock);
  if (err)
    return err;

  handle->io_watcher.fd = sock;
  if (uv__udp_is_connected(handle))
    handle->flags |= UV_HANDLE_UDP_CONNECTED;

  return 0;
}


int uv_udp_set_membership(uv_udp_t* handle,
                          const char* multicast_addr,
                          const char* interface_addr,
                          uv_membership membership) {
  int err;
  struct sockaddr_in addr4;
  struct sockaddr_in6 addr6;

  if (uv_ip4_addr(multicast_addr, 0, &addr4) == 0) {
    err = uv__udp_maybe_deferred_bind(handle, AF_INET, UV_UDP_REUSEADDR);
    if (err)
      return err;
    return uv__udp_set_membership4(handle, &addr4, interface_addr, membership);
  } else if (uv_ip6_addr(multicast_addr, 0, &addr6) == 0) {
    err = uv__udp_maybe_deferred_bind(handle, AF_INET6, UV_UDP_REUSEADDR);
    if (err)
      return err;
    return uv__udp_set_membership6(handle, &addr6, interface_addr, membership);
  } else {
    return UV_EINVAL;
  }
}


int uv_udp_set_source_membership(uv_udp_t* handle,
                                 const char* multicast_addr,
                                 const char* interface_addr,
                                 const char* source_addr,
                                 uv_membership membership) {
#if !defined(__OpenBSD__) &&                                        \
    !defined(__NetBSD__) &&                                         \
    !defined(__ANDROID__) &&                                        \
    !defined(__DragonFly__) &&                                      \
    !defined(__QNX__) &&                                            \
    !defined(__GNU__)
  int err;
  union uv__sockaddr mcast_addr;
  union uv__sockaddr src_addr;

  err = uv_ip4_addr(multicast_addr, 0, &mcast_addr.in);
  if (err) {
    err = uv_ip6_addr(multicast_addr, 0, &mcast_addr.in6);
    if (err)
      return err;
    err = uv_ip6_addr(source_addr, 0, &src_addr.in6);
    if (err)
      return err;
    return uv__udp_set_source_membership6(handle,
                                          &mcast_addr.in6,
                                          interface_addr,
                                          &src_addr.in6,
                                          membership);
  }

  err = uv_ip4_addr(source_addr, 0, &src_addr.in);
  if (err)
    return err;
  return uv__udp_set_source_membership4(handle,
                                        &mcast_addr.in,
                                        interface_addr,
                                        &src_addr.in,
                                        membership);
#else
  return UV_ENOSYS;
#endif
}


static int uv__setsockopt(uv_udp_t* handle,
                         int option4,
                         int option6,
                         const void* val,
                         socklen_t size) {
  int r;

  if (handle->flags & UV_HANDLE_IPV6)
    r = setsockopt(handle->io_watcher.fd,
                   IPPROTO_IPV6,
                   option6,
                   val,
                   size);
  else
    r = setsockopt(handle->io_watcher.fd,
                   IPPROTO_IP,
                   option4,
                   val,
                   size);
  if (r)
    return UV__ERR(errno);

  return 0;
}

static int uv__setsockopt_maybe_char(uv_udp_t* handle,
                                     int option4,
                                     int option6,
                                     int val) {
#if defined(__sun) || defined(_AIX) || defined(__MVS__)
  char arg = val;
#elif defined(__OpenBSD__)
  unsigned char arg = val;
#else
  int arg = val;
#endif

  if (val < 0 || val > 255)
    return UV_EINVAL;

  return uv__setsockopt(handle, option4, option6, &arg, sizeof(arg));
}


int uv_udp_set_broadcast(uv_udp_t* handle, int on) {
  if (setsockopt(handle->io_watcher.fd,
                 SOL_SOCKET,
                 SO_BROADCAST,
                 &on,
                 sizeof(on))) {
    return UV__ERR(errno);
  }

  return 0;
}


int uv_udp_set_ttl(uv_udp_t* handle, int ttl) {
  if (ttl < 1 || ttl > 255)
    return UV_EINVAL;

#if defined(__MVS__)
  if (!(handle->flags & UV_HANDLE_IPV6))
    return UV_ENOTSUP;  /* zOS does not support setting ttl for IPv4 */
#endif

/*
 * On Solaris and derivatives such as SmartOS, the length of socket options
 * is sizeof(int) for IP_TTL and IPV6_UNICAST_HOPS,
 * so hardcode the size of these options on this platform,
 * and use the general uv__setsockopt_maybe_char call on other platforms.
 */
#if defined(__sun) || defined(_AIX) || defined(__OpenBSD__) || \
    defined(__MVS__) || defined(__QNX__)

  return uv__setsockopt(handle,
                        IP_TTL,
                        IPV6_UNICAST_HOPS,
                        &ttl,
                        sizeof(ttl));

#else /* !(defined(__sun) || defined(_AIX) || defined (__OpenBSD__) ||
           defined(__MVS__) || defined(__QNX__)) */

  return uv__setsockopt_maybe_char(handle,
                                   IP_TTL,
                                   IPV6_UNICAST_HOPS,
                                   ttl);

#endif /* defined(__sun) || defined(_AIX) || defined (__OpenBSD__) ||
          defined(__MVS__) || defined(__QNX__) */
}


int uv_udp_set_multicast_ttl(uv_udp_t* handle, int ttl) {
/*
 * On Solaris and derivatives such as SmartOS, the length of socket options
 * is sizeof(int) for IPV6_MULTICAST_HOPS and sizeof(char) for
 * IP_MULTICAST_TTL, so hardcode the size of the option in the IPv6 case,
 * and use the general uv__setsockopt_maybe_char call otherwise.
 */
#if defined(__sun) || defined(_AIX) || defined(__OpenBSD__) || \
    defined(__MVS__) || defined(__QNX__)
  if (handle->flags & UV_HANDLE_IPV6)
    return uv__setsockopt(handle,
                          IP_MULTICAST_TTL,
                          IPV6_MULTICAST_HOPS,
                          &ttl,
                          sizeof(ttl));
#endif /* defined(__sun) || defined(_AIX) || defined(__OpenBSD__) || \
    defined(__MVS__) || defined(__QNX__) */

  return uv__setsockopt_maybe_char(handle,
                                   IP_MULTICAST_TTL,
                                   IPV6_MULTICAST_HOPS,
                                   ttl);
}


int uv_udp_set_multicast_loop(uv_udp_t* handle, int on) {
/*
 * On Solaris and derivatives such as SmartOS, the length of socket options
 * is sizeof(int) for IPV6_MULTICAST_LOOP and sizeof(char) for
 * IP_MULTICAST_LOOP, so hardcode the size of the option in the IPv6 case,
 * and use the general uv__setsockopt_maybe_char call otherwise.
 */
#if defined(__sun) || defined(_AIX) || defined(__OpenBSD__) || \
    defined(__MVS__) || defined(__QNX__)
  if (handle->flags & UV_HANDLE_IPV6)
    return uv__setsockopt(handle,
                          IP_MULTICAST_LOOP,
                          IPV6_MULTICAST_LOOP,
                          &on,
                          sizeof(on));
#endif /* defined(__sun) || defined(_AIX) ||defined(__OpenBSD__) ||
    defined(__MVS__) || defined(__QNX__) */

  return uv__setsockopt_maybe_char(handle,
                                   IP_MULTICAST_LOOP,
                                   IPV6_MULTICAST_LOOP,
                                   on);
}

int uv_udp_set_multicast_interface(uv_udp_t* handle, const char* interface_addr) {
  struct sockaddr_storage addr_st;
  struct sockaddr_in* addr4;
  struct sockaddr_in6* addr6;

  addr4 = (struct sockaddr_in*) &addr_st;
  addr6 = (struct sockaddr_in6*) &addr_st;

  if (!interface_addr) {
    memset(&addr_st, 0, sizeof addr_st);
    if (handle->flags & UV_HANDLE_IPV6) {
      addr_st.ss_family = AF_INET6;
      addr6->sin6_scope_id = 0;
    } else {
      addr_st.ss_family = AF_INET;
      addr4->sin_addr.s_addr = htonl(INADDR_ANY);
    }
  } else if (uv_ip4_addr(interface_addr, 0, addr4) == 0) {
    /* nothing, address was parsed */
  } else if (uv_ip6_addr(interface_addr, 0, addr6) == 0) {
    /* nothing, address was parsed */
  } else {
    return UV_EINVAL;
  }

  if (addr_st.ss_family == AF_INET) {
    if (setsockopt(handle->io_watcher.fd,
                   IPPROTO_IP,
                   IP_MULTICAST_IF,
                   (void*) &addr4->sin_addr,
                   sizeof(addr4->sin_addr)) == -1) {
      return UV__ERR(errno);
    }
  } else if (addr_st.ss_family == AF_INET6) {
    if (setsockopt(handle->io_watcher.fd,
                   IPPROTO_IPV6,
                   IPV6_MULTICAST_IF,
                   &addr6->sin6_scope_id,
                   sizeof(addr6->sin6_scope_id)) == -1) {
      return UV__ERR(errno);
    }
  } else {
    assert(0 && "unexpected address family");
    abort();
  }

  return 0;
}

int uv_udp_getpeername(const uv_udp_t* handle,
                       struct sockaddr* name,
                       int* namelen) {

  return uv__getsockpeername((const uv_handle_t*) handle,
                             getpeername,
                             name,
                             namelen);
}

int uv_udp_getsockname(const uv_udp_t* handle,
                       struct sockaddr* name,
                       int* namelen) {

  return uv__getsockpeername((const uv_handle_t*) handle,
                             getsockname,
                             name,
                             namelen);
}


int uv__udp_recv_start(uv_udp_t* handle,
                       uv_alloc_cb alloc_cb,
                       uv_udp_recv_cb recv_cb) {
  int err;

  if (alloc_cb == NULL || recv_cb == NULL)
    return UV_EINVAL;

  if (uv__io_active(&handle->io_watcher, POLLIN))
    return UV_EALREADY;  /* FIXME(bnoordhuis) Should be UV_EBUSY. */

  err = uv__udp_maybe_deferred_bind(handle, AF_INET, 0);
  if (err)
    return err;

  handle->alloc_cb = alloc_cb;
  handle->recv_cb = recv_cb;

  uv__io_start(handle->loop, &handle->io_watcher, POLLIN);
  uv__handle_start(handle);

  return 0;
}


int uv__udp_recv_stop(uv_udp_t* handle) {
  uv__io_stop(handle->loop, &handle->io_watcher, POLLIN);

  if (!uv__io_active(&handle->io_watcher, POLLOUT))
    uv__handle_stop(handle);

  handle->alloc_cb = NULL;
  handle->recv_cb = NULL;

  return 0;
}


static int uv__udp_prep_pkt(struct msghdr* h,
                            const uv_buf_t* bufs,
                            const unsigned int nbufs,
                            const struct sockaddr* addr) {
  memset(h, 0, sizeof(*h));
  h->msg_name = (void*) addr;
  h->msg_iov = (void*) bufs;
  h->msg_iovlen = nbufs;
  if (addr == NULL)
    return 0;
  switch (addr->sa_family) {
  case AF_INET:
    h->msg_namelen = sizeof(struct sockaddr_in);
    return 0;
  case AF_INET6:
    h->msg_namelen = sizeof(struct sockaddr_in6);
    return 0;
  case AF_UNIX:
    h->msg_namelen = sizeof(struct sockaddr_un);
    return 0;
  case AF_UNSPEC:
    h->msg_name = NULL;
    return 0;
  }
  return UV_EINVAL;
}


static int uv__udp_sendmsg1(int fd,
                            const uv_buf_t* bufs,
                            unsigned int nbufs,
                            const struct sockaddr* addr) {
  struct msghdr h;
  int r;

  if ((r = uv__udp_prep_pkt(&h, bufs, nbufs, addr)))
    return r;

  do
    r = sendmsg(fd, &h, 0);
  while (r == -1 && errno == EINTR);

  if (r < 0) {
    r = UV__ERR(errno);
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ENOBUFS)
      r = UV_EAGAIN;
    return r;
  }

  /* UDP sockets don't EOF so we don't have to handle r=0 specially,
   * that only happens when the input was a zero-sized buffer.
   */
  return 1;
}


static int uv__udp_sendmsgv(int fd,
                            unsigned int count,
                            uv_buf_t* bufs[/*count*/],
                            unsigned int nbufs[/*count*/],
                            struct sockaddr* addrs[/*count*/]) {
  unsigned int i;
  int nsent;
  int r;

  r = 0;
  nsent = 0;

#if defined(__linux__) || defined(__FreeBSD__) || defined(__APPLE__)
  if (count > 1) {
    for (i = 0; i < count; /*empty*/) {
      struct mmsghdr m[20];
      unsigned int n;

      for (n = 0; i < count && n < ARRAY_SIZE(m); i++, n++)
        if ((r = uv__udp_prep_pkt(&m[n].msg_hdr, bufs[i], nbufs[i], addrs[i])))
          goto exit;

      do
#if defined(__APPLE__)
        r = sendmsg_x(fd, m, n, MSG_DONTWAIT);
#else
        r = sendmmsg(fd, m, n, 0);
#endif
      while (r == -1 && errno == EINTR);

      if (r < 1)
        goto exit;

      nsent += r;
      i += r;
    }

    goto exit;
  }
#endif  /* defined(__linux__) || defined(__FreeBSD__) || defined(__APPLE__) */

  for (i = 0; i < count; i++, nsent++)
    if ((r = uv__udp_sendmsg1(fd, bufs[i], nbufs[i], addrs[i])))
      goto exit;  /* goto to avoid unused label warning. */

exit:

  if (nsent > 0)
    return nsent;

  if (r < 0) {
    r = UV__ERR(errno);
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ENOBUFS)
      r = UV_EAGAIN;
  }

  return r;
}


static void uv__udp_sendmsg(uv_udp_t* handle) {
  static const int N = 20;
  struct sockaddr* addrs[N];
  unsigned int nbufs[N];
  uv_buf_t* bufs[N];
  struct uv__queue* q;
  uv_udp_send_t* req;
  int n;

  if (uv__queue_empty(&handle->write_queue))
    return;

again:
  n = 0;
  q = uv__queue_head(&handle->write_queue);
  do {
    req = uv__queue_data(q, uv_udp_send_t, queue);
    addrs[n] = &req->u.addr;
    nbufs[n] = req->nbufs;
    bufs[n] = req->bufs;
    q = uv__queue_next(q);
    n++;
  } while (n < N && q != &handle->write_queue);

  n = uv__udp_sendmsgv(handle->io_watcher.fd, n, bufs, nbufs, addrs);
  while (n > 0) {
    q = uv__queue_head(&handle->write_queue);
    req = uv__queue_data(q, uv_udp_send_t, queue);
    req->status = uv__count_bufs(req->bufs, req->nbufs);
    uv__queue_remove(&req->queue);
    uv__queue_insert_tail(&handle->write_completed_queue, &req->queue);
    n--;
  }

  if (n == 0) {
    if (uv__queue_empty(&handle->write_queue))
      goto feed;
    goto again;
  }

  if (n == UV_EAGAIN)
    return;

  /* Register the error against first request in queue because that
   * is the request that uv__udp_sendmsgv tried but failed to send,
   * because if it did send any requests, it won't return an error.
   */
  q = uv__queue_head(&handle->write_queue);
  req = uv__queue_data(q, uv_udp_send_t, queue);
  req->status = n;
  uv__queue_remove(&req->queue);
  uv__queue_insert_tail(&handle->write_completed_queue, &req->queue);
feed:
  uv__io_feed(handle->loop, &handle->io_watcher);
}


int uv__udp_try_send2(uv_udp_t* handle,
                      unsigned int count,
                      uv_buf_t* bufs[/*count*/],
                      unsigned int nbufs[/*count*/],
                      struct sockaddr* addrs[/*count*/]) {
  int fd;

  fd = handle->io_watcher.fd;
  if (fd == -1)
    return UV_EINVAL;

  return uv__udp_sendmsgv(fd, count, bufs, nbufs, addrs);
}
