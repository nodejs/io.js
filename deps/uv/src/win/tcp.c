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

#include <assert.h>
#include <stdlib.h>

#include "uv.h"
#include "internal.h"
#include "handle-inl.h"
#include "stream-inl.h"
#include "req-inl.h"


/*
 * Number of simultaneous pending AcceptEx calls.
 */
const unsigned int uv_simultaneous_server_accepts = 32;

/* A zero-size buffer for use by uv_tcp_read */
static char uv_zero_[] = "";

static int uv__tcp_nodelay(uv_tcp_t* handle, SOCKET socket, int enable) {
  if (setsockopt(socket,
                 IPPROTO_TCP,
                 TCP_NODELAY,
                 (const char*)&enable,
                 sizeof enable) == -1) {
    return WSAGetLastError();
  }
  return 0;
}


static int uv__tcp_keepalive(uv_tcp_t* handle, SOCKET socket, int enable, unsigned int delay) {
  if (setsockopt(socket,
                 SOL_SOCKET,
                 SO_KEEPALIVE,
                 (const char*)&enable,
                 sizeof enable) == -1) {
    return WSAGetLastError();
  }

  if (!enable)
    return 0;

  if (delay < 1)
    return UV_EINVAL;

  if (setsockopt(socket,
                 IPPROTO_TCP,
                 TCP_KEEPALIVE,
                 (const char*)&delay,
                 sizeof delay) == -1) {
    return WSAGetLastError();
  }

  return 0;
}


static int uv__tcp_set_socket(uv_loop_t* loop,
                              uv_tcp_t* handle,
                              SOCKET socket,
                              int family,
                              int imported) {
  DWORD yes = 1;
  int non_ifs_lsp;
  int err;

  if (handle->socket != INVALID_SOCKET)
    return UV_EBUSY;

  /* Set the socket to nonblocking mode */
  if (ioctlsocket(socket, FIONBIO, &yes) == SOCKET_ERROR) {
    return WSAGetLastError();
  }

  /* Make the socket non-inheritable */
  if (!SetHandleInformation((HANDLE) socket, HANDLE_FLAG_INHERIT, 0))
    return GetLastError();

  /* Associate it with the I/O completion port. Use uv_handle_t pointer as
   * completion key. */
  if (CreateIoCompletionPort((HANDLE)socket,
                             loop->iocp,
                             (ULONG_PTR)socket,
                             0) == NULL) {
    if (imported) {
      handle->flags |= UV_HANDLE_EMULATE_IOCP;
    } else {
      return GetLastError();
    }
  }

  if (family == AF_INET6) {
    non_ifs_lsp = uv_tcp_non_ifs_lsp_ipv6;
  } else {
    non_ifs_lsp = uv_tcp_non_ifs_lsp_ipv4;
  }

  if (!(handle->flags & UV_HANDLE_EMULATE_IOCP) && !non_ifs_lsp) {
    UCHAR sfcnm_flags =
        FILE_SKIP_SET_EVENT_ON_HANDLE | FILE_SKIP_COMPLETION_PORT_ON_SUCCESS;
    if (!SetFileCompletionNotificationModes((HANDLE) socket, sfcnm_flags))
      return GetLastError();
    handle->flags |= UV_HANDLE_SYNC_BYPASS_IOCP;
  }

  if (handle->flags & UV_HANDLE_TCP_NODELAY) {
    err = uv__tcp_nodelay(handle, socket, 1);
    if (err)
      return err;
  }

  /* TODO: Use stored delay. */
  if (handle->flags & UV_HANDLE_TCP_KEEPALIVE) {
    err = uv__tcp_keepalive(handle, socket, 1, 60);
    if (err)
      return err;
  }

  handle->socket = socket;

  if (family == AF_INET6) {
    handle->flags |= UV_HANDLE_IPV6;
  } else {
    assert(!(handle->flags & UV_HANDLE_IPV6));
  }

  return 0;
}


int uv_tcp_init_ex(uv_loop_t* loop, uv_tcp_t* handle, unsigned int flags) {
  int domain;

  /* Use the lower 8 bits for the domain */
  domain = flags & 0xFF;
  if (domain != AF_INET && domain != AF_INET6 && domain != AF_UNSPEC)
    return UV_EINVAL;

  if (flags & ~0xFF)
    return UV_EINVAL;

  uv__stream_init(loop, (uv_stream_t*) handle, UV_TCP);
  handle->tcp.serv.accept_reqs = NULL;
  handle->tcp.serv.pending_accepts = NULL;
  handle->socket = INVALID_SOCKET;
  handle->reqs_pending = 0;
  handle->tcp.serv.func_acceptex = NULL;
  handle->tcp.conn.func_connectex = NULL;
  handle->tcp.serv.processed_accepts = 0;
  handle->delayed_error = 0;

  /* If anything fails beyond this point we need to remove the handle from
   * the handle queue, since it was added by uv__handle_init in uv__stream_init.
   */

  if (domain != AF_UNSPEC) {
    SOCKET sock;
    DWORD err;

    sock = socket(domain, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
      err = WSAGetLastError();
      uv__queue_remove(&handle->handle_queue);
      return uv_translate_sys_error(err);
    }

    err = uv__tcp_set_socket(handle->loop, handle, sock, domain, 0);
    if (err) {
      closesocket(sock);
      uv__queue_remove(&handle->handle_queue);
      return uv_translate_sys_error(err);
    }

  }

  return 0;
}


int uv_tcp_init(uv_loop_t* loop, uv_tcp_t* handle) {
  return uv_tcp_init_ex(loop, handle, AF_UNSPEC);
}


void uv__process_tcp_shutdown_req(uv_loop_t* loop, uv_tcp_t* stream, uv_shutdown_t *req) {
  int err;

  assert(req);
  assert(stream->stream.conn.write_reqs_pending == 0);
  assert(!(stream->flags & UV_HANDLE_SHUT));
  assert(stream->flags & UV_HANDLE_CONNECTION);

  stream->stream.conn.shutdown_req = NULL;
  UNREGISTER_HANDLE_REQ(loop, stream);

  err = 0;
  if (stream->flags & UV_HANDLE_CLOSING)
   /* The user destroyed the stream before we got to do the shutdown. */
    err = UV_ECANCELED;
  else if (shutdown(stream->socket, SD_SEND) == SOCKET_ERROR)
    err = uv_translate_sys_error(WSAGetLastError());
  else /* Success. */
    stream->flags |= UV_HANDLE_SHUT;

  if (req->cb)
    req->cb(req, err);

  DECREASE_PENDING_REQ_COUNT(stream);
}


void uv__tcp_endgame(uv_loop_t* loop, uv_tcp_t* handle) {
  unsigned int i;
  uv_tcp_accept_t* req;

  assert(handle->flags & UV_HANDLE_CLOSING);
  assert(handle->reqs_pending == 0);
  assert(!(handle->flags & UV_HANDLE_CLOSED));
  assert(handle->socket == INVALID_SOCKET);

  if (!(handle->flags & UV_HANDLE_CONNECTION) && handle->tcp.serv.accept_reqs) {
    if (handle->flags & UV_HANDLE_EMULATE_IOCP) {
      for (i = 0; i < uv_simultaneous_server_accepts; i++) {
        req = &handle->tcp.serv.accept_reqs[i];
        if (req->wait_handle != INVALID_HANDLE_VALUE) {
          UnregisterWait(req->wait_handle);
          req->wait_handle = INVALID_HANDLE_VALUE;
        }
        if (req->event_handle != NULL) {
          CloseHandle(req->event_handle);
          req->event_handle = NULL;
        }
      }
    }

    uv__free(handle->tcp.serv.accept_reqs);
    handle->tcp.serv.accept_reqs = NULL;
  }

  if (handle->flags & UV_HANDLE_CONNECTION &&
      handle->flags & UV_HANDLE_EMULATE_IOCP) {
    if (handle->read_req.wait_handle != INVALID_HANDLE_VALUE) {
      UnregisterWait(handle->read_req.wait_handle);
      handle->read_req.wait_handle = INVALID_HANDLE_VALUE;
    }
    if (handle->read_req.event_handle != NULL) {
      CloseHandle(handle->read_req.event_handle);
      handle->read_req.event_handle = NULL;
    }
  }

  uv__handle_close(handle);
}


/* Unlike on Unix, here we don't set SO_REUSEADDR, because it doesn't just
 * allow binding to addresses that are in use by sockets in TIME_WAIT, it
 * effectively allows 'stealing' a port which is in use by another application.
 *
 * SO_EXCLUSIVEADDRUSE is also not good here because it does check all sockets,
 * regardless of state, so we'd get an error even if the port is in use by a
 * socket in TIME_WAIT state.
 *
 * See issue #1360.
 *
 */
static int uv__tcp_try_bind(uv_tcp_t* handle,
                            const struct sockaddr* addr,
                            unsigned int addrlen,
                            unsigned int flags) {
  DWORD err;
  int r;

  /* There is no SO_REUSEPORT on Windows, Windows only knows SO_REUSEADDR.
   * so we just return an error directly when UV_TCP_REUSEPORT is requested
   * for binding the socket. */
  if (flags & UV_TCP_REUSEPORT)
    return ERROR_NOT_SUPPORTED;

  if (handle->socket == INVALID_SOCKET) {
    SOCKET sock;

    /* Cannot set IPv6-only mode on non-IPv6 socket. */
    if ((flags & UV_TCP_IPV6ONLY) && addr->sa_family != AF_INET6)
      return ERROR_INVALID_PARAMETER;

    sock = socket(addr->sa_family, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
      return WSAGetLastError();
    }

    err = uv__tcp_set_socket(handle->loop, handle, sock, addr->sa_family, 0);
    if (err) {
      closesocket(sock);
      return err;
    }
  }

#ifdef IPV6_V6ONLY
  if (addr->sa_family == AF_INET6) {
    int on;

    on = (flags & UV_TCP_IPV6ONLY) != 0;

    /* TODO: how to handle errors? This may fail if there is no ipv4 stack
     * available, or when run on XP/2003 which have no support for dualstack
     * sockets. For now we're silently ignoring the error. */
    setsockopt(handle->socket,
               IPPROTO_IPV6,
               IPV6_V6ONLY,
               (const char*)&on,
               sizeof on);
  }
#endif

  r = bind(handle->socket, addr, addrlen);

  if (r == SOCKET_ERROR) {
    err = WSAGetLastError();
    if (err == WSAEADDRINUSE) {
      /* Some errors are not to be reported until connect() or listen() */
      handle->delayed_error = err;
    } else {
      return err;
    }
  }

  handle->flags |= UV_HANDLE_BOUND;

  return 0;
}


static void CALLBACK post_completion(void* context, BOOLEAN timed_out) {
  uv_req_t* req;
  uv_tcp_t* handle;

  req = (uv_req_t*) context;
  assert(req != NULL);
  handle = (uv_tcp_t*)req->data;
  assert(handle != NULL);
  assert(!timed_out);

  if (!PostQueuedCompletionStatus(handle->loop->iocp,
                                  req->u.io.overlapped.InternalHigh,
                                  0,
                                  &req->u.io.overlapped)) {
    uv_fatal_error(GetLastError(), "PostQueuedCompletionStatus");
  }
}


static void CALLBACK post_write_completion(void* context, BOOLEAN timed_out) {
  uv_write_t* req;
  uv_tcp_t* handle;

  req = (uv_write_t*) context;
  assert(req != NULL);
  handle = (uv_tcp_t*)req->handle;
  assert(handle != NULL);
  assert(!timed_out);

  if (!PostQueuedCompletionStatus(handle->loop->iocp,
                                  req->u.io.overlapped.InternalHigh,
                                  0,
                                  &req->u.io.overlapped)) {
    uv_fatal_error(GetLastError(), "PostQueuedCompletionStatus");
  }
}


static void uv__tcp_queue_accept(uv_tcp_t* handle, uv_tcp_accept_t* req) {
  uv_loop_t* loop = handle->loop;
  BOOL success;
  DWORD bytes;
  SOCKET accept_socket;
  short family;

  assert(handle->flags & UV_HANDLE_LISTENING);
  assert(req->accept_socket == INVALID_SOCKET);

  /* choose family and extension function */
  if (handle->flags & UV_HANDLE_IPV6) {
    family = AF_INET6;
  } else {
    family = AF_INET;
  }

  /* Open a socket for the accepted connection. */
  accept_socket = socket(family, SOCK_STREAM, 0);
  if (accept_socket == INVALID_SOCKET) {
    SET_REQ_ERROR(req, WSAGetLastError());
    uv__insert_pending_req(loop, (uv_req_t*)req);
    handle->reqs_pending++;
    return;
  }

  /* Make the socket non-inheritable */
  if (!SetHandleInformation((HANDLE) accept_socket, HANDLE_FLAG_INHERIT, 0)) {
    SET_REQ_ERROR(req, GetLastError());
    uv__insert_pending_req(loop, (uv_req_t*)req);
    handle->reqs_pending++;
    closesocket(accept_socket);
    return;
  }

  /* Prepare the overlapped structure. */
  memset(&(req->u.io.overlapped), 0, sizeof(req->u.io.overlapped));
  if (handle->flags & UV_HANDLE_EMULATE_IOCP) {
    assert(req->event_handle != NULL);
    req->u.io.overlapped.hEvent = (HANDLE) ((ULONG_PTR) req->event_handle | 1);
  }

  success = handle->tcp.serv.func_acceptex(handle->socket,
                                          accept_socket,
                                          (void*)req->accept_buffer,
                                          0,
                                          sizeof(struct sockaddr_storage),
                                          sizeof(struct sockaddr_storage),
                                          &bytes,
                                          &req->u.io.overlapped);

  if (UV_SUCCEEDED_WITHOUT_IOCP(success)) {
    /* Process the req without IOCP. */
    req->accept_socket = accept_socket;
    handle->reqs_pending++;
    uv__insert_pending_req(loop, (uv_req_t*)req);
  } else if (UV_SUCCEEDED_WITH_IOCP(success)) {
    /* The req will be processed with IOCP. */
    req->accept_socket = accept_socket;
    handle->reqs_pending++;
    if (handle->flags & UV_HANDLE_EMULATE_IOCP &&
        req->wait_handle == INVALID_HANDLE_VALUE &&
        !RegisterWaitForSingleObject(&req->wait_handle,
          req->event_handle, post_completion, (void*) req,
          INFINITE, WT_EXECUTEINWAITTHREAD)) {
      SET_REQ_ERROR(req, GetLastError());
      uv__insert_pending_req(loop, (uv_req_t*)req);
    }
  } else {
    /* Make this req pending reporting an error. */
    SET_REQ_ERROR(req, WSAGetLastError());
    uv__insert_pending_req(loop, (uv_req_t*)req);
    handle->reqs_pending++;
    /* Destroy the preallocated client socket. */
    closesocket(accept_socket);
    /* Destroy the event handle */
    if (handle->flags & UV_HANDLE_EMULATE_IOCP) {
      CloseHandle(req->event_handle);
      req->event_handle = NULL;
    }
  }
}


static void uv__tcp_queue_read(uv_loop_t* loop, uv_tcp_t* handle) {
  uv_read_t* req;
  uv_buf_t buf;
  int result;
  DWORD bytes, flags;

  assert(handle->flags & UV_HANDLE_READING);
  assert(!(handle->flags & UV_HANDLE_READ_PENDING));

  req = &handle->read_req;
  memset(&req->u.io.overlapped, 0, sizeof(req->u.io.overlapped));

  handle->flags |= UV_HANDLE_ZERO_READ;
  buf.base = (char*) &uv_zero_;
  buf.len = 0;

  /* Prepare the overlapped structure. */
  memset(&(req->u.io.overlapped), 0, sizeof(req->u.io.overlapped));
  if (handle->flags & UV_HANDLE_EMULATE_IOCP) {
    assert(req->event_handle != NULL);
    req->u.io.overlapped.hEvent = (HANDLE) ((ULONG_PTR) req->event_handle | 1);
  }

  flags = 0;
  result = WSARecv(handle->socket,
                   (WSABUF*)&buf,
                   1,
                   &bytes,
                   &flags,
                   &req->u.io.overlapped,
                   NULL);

  handle->flags |= UV_HANDLE_READ_PENDING;
  handle->reqs_pending++;

  if (UV_SUCCEEDED_WITHOUT_IOCP(result == 0)) {
    /* Process the req without IOCP. */
    req->u.io.overlapped.InternalHigh = bytes;
    uv__insert_pending_req(loop, (uv_req_t*)req);
  } else if (UV_SUCCEEDED_WITH_IOCP(result == 0)) {
    /* The req will be processed with IOCP. */
    if (handle->flags & UV_HANDLE_EMULATE_IOCP &&
        req->wait_handle == INVALID_HANDLE_VALUE &&
        !RegisterWaitForSingleObject(&req->wait_handle,
          req->event_handle, post_completion, (void*) req,
          INFINITE, WT_EXECUTEINWAITTHREAD)) {
      SET_REQ_ERROR(req, GetLastError());
      uv__insert_pending_req(loop, (uv_req_t*)req);
    }
  } else {
    /* Make this req pending reporting an error. */
    SET_REQ_ERROR(req, WSAGetLastError());
    uv__insert_pending_req(loop, (uv_req_t*)req);
  }
}


int uv_tcp_close_reset(uv_tcp_t* handle, uv_close_cb close_cb) {
  struct linger l = { 1, 0 };

  /* Disallow setting SO_LINGER to zero due to some platform inconsistencies */
  if (uv__is_stream_shutting(handle))
    return UV_EINVAL;

  if (0 != setsockopt(handle->socket, SOL_SOCKET, SO_LINGER, (const char*)&l, sizeof(l)))
    return uv_translate_sys_error(WSAGetLastError());

  uv_close((uv_handle_t*) handle, close_cb);
  return 0;
}


int uv__tcp_listen(uv_tcp_t* handle, int backlog, uv_connection_cb cb) {
  unsigned int i, simultaneous_accepts;
  uv_tcp_accept_t* req;
  int err;

  assert(backlog > 0);

  if (handle->flags & UV_HANDLE_LISTENING) {
    handle->stream.serv.connection_cb = cb;
  }

  if (handle->flags & UV_HANDLE_READING) {
    return WSAEISCONN;
  }

  if (handle->delayed_error) {
    return handle->delayed_error;
  }

  if (!(handle->flags & UV_HANDLE_BOUND)) {
    err = uv__tcp_try_bind(handle,
                           (const struct sockaddr*) &uv_addr_ip4_any_,
                           sizeof(uv_addr_ip4_any_),
                           0);
    if (err)
      return err;
    if (handle->delayed_error)
      return handle->delayed_error;
  }

  if (!handle->tcp.serv.func_acceptex) {
    if (!uv__get_acceptex_function(handle->socket, &handle->tcp.serv.func_acceptex)) {
      return WSAEAFNOSUPPORT;
    }
  }

  /* If this flag is set, we already made this listen call in xfer. */
  if (!(handle->flags & UV_HANDLE_SHARED_TCP_SOCKET) &&
      listen(handle->socket, backlog) == SOCKET_ERROR) {
    return WSAGetLastError();
  }

  handle->flags |= UV_HANDLE_LISTENING;
  handle->stream.serv.connection_cb = cb;
  INCREASE_ACTIVE_COUNT(loop, handle);

  simultaneous_accepts = handle->flags & UV_HANDLE_TCP_SINGLE_ACCEPT ? 1
    : uv_simultaneous_server_accepts;

  if (handle->tcp.serv.accept_reqs == NULL) {
    handle->tcp.serv.accept_reqs =
      uv__malloc(uv_simultaneous_server_accepts * sizeof(uv_tcp_accept_t));
    if (!handle->tcp.serv.accept_reqs) {
      uv_fatal_error(ERROR_OUTOFMEMORY, "uv__malloc");
    }

    for (i = 0; i < simultaneous_accepts; i++) {
      req = &handle->tcp.serv.accept_reqs[i];
      UV_REQ_INIT(req, UV_ACCEPT);
      req->accept_socket = INVALID_SOCKET;
      req->data = handle;

      req->wait_handle = INVALID_HANDLE_VALUE;
      if (handle->flags & UV_HANDLE_EMULATE_IOCP) {
        req->event_handle = CreateEvent(NULL, 0, 0, NULL);
        if (req->event_handle == NULL) {
          uv_fatal_error(GetLastError(), "CreateEvent");
        }
      } else {
        req->event_handle = NULL;
      }

      uv__tcp_queue_accept(handle, req);
    }

    /* Initialize other unused requests too, because uv_tcp_endgame doesn't
     * know how many requests were initialized, so it will try to clean up
     * {uv_simultaneous_server_accepts} requests. */
    for (i = simultaneous_accepts; i < uv_simultaneous_server_accepts; i++) {
      req = &handle->tcp.serv.accept_reqs[i];
      UV_REQ_INIT(req, UV_ACCEPT);
      req->accept_socket = INVALID_SOCKET;
      req->data = handle;
      req->wait_handle = INVALID_HANDLE_VALUE;
      req->event_handle = NULL;
    }
  }

  return 0;
}


int uv__tcp_accept(uv_tcp_t* server, uv_tcp_t* client) {
  int err = 0;
  int family;

  uv_tcp_accept_t* req = server->tcp.serv.pending_accepts;

  if (!req) {
    /* No valid connections found, so we error out. */
    return WSAEWOULDBLOCK;
  }

  if (req->accept_socket == INVALID_SOCKET) {
    return WSAENOTCONN;
  }

  if (server->flags & UV_HANDLE_IPV6) {
    family = AF_INET6;
  } else {
    family = AF_INET;
  }

  err = uv__tcp_set_socket(client->loop,
                          client,
                          req->accept_socket,
                          family,
                          0);
  if (err) {
    closesocket(req->accept_socket);
  } else {
    uv__connection_init((uv_stream_t*) client);
    /* AcceptEx() implicitly binds the accepted socket. */
    client->flags |= UV_HANDLE_BOUND | UV_HANDLE_READABLE | UV_HANDLE_WRITABLE;
  }

  /* Prepare the req to pick up a new connection */
  server->tcp.serv.pending_accepts = req->next_pending;
  req->next_pending = NULL;
  req->accept_socket = INVALID_SOCKET;

  if (!(server->flags & UV_HANDLE_CLOSING)) {
    /* Check if we're in a middle of changing the number of pending accepts. */
    if (!(server->flags & UV_HANDLE_TCP_ACCEPT_STATE_CHANGING)) {
      uv__tcp_queue_accept(server, req);
    } else {
      /* We better be switching to a single pending accept. */
      assert(server->flags & UV_HANDLE_TCP_SINGLE_ACCEPT);

      server->tcp.serv.processed_accepts++;

      if (server->tcp.serv.processed_accepts >= uv_simultaneous_server_accepts) {
        server->tcp.serv.processed_accepts = 0;
        /*
         * All previously queued accept requests are now processed.
         * We now switch to queueing just a single accept.
         */
        uv__tcp_queue_accept(server, &server->tcp.serv.accept_reqs[0]);
        server->flags &= ~UV_HANDLE_TCP_ACCEPT_STATE_CHANGING;
        server->flags |= UV_HANDLE_TCP_SINGLE_ACCEPT;
      }
    }
  }

  return err;
}


int uv__tcp_read_start(uv_tcp_t* handle, uv_alloc_cb alloc_cb,
    uv_read_cb read_cb) {
  uv_loop_t* loop = handle->loop;

  handle->flags |= UV_HANDLE_READING;
  handle->read_cb = read_cb;
  handle->alloc_cb = alloc_cb;
  INCREASE_ACTIVE_COUNT(loop, handle);

  /* If reading was stopped and then started again, there could still be a read
   * request pending. */
  if (!(handle->flags & UV_HANDLE_READ_PENDING)) {
    if (handle->flags & UV_HANDLE_EMULATE_IOCP &&
        handle->read_req.event_handle == NULL) {
      handle->read_req.event_handle = CreateEvent(NULL, 0, 0, NULL);
      if (handle->read_req.event_handle == NULL) {
        uv_fatal_error(GetLastError(), "CreateEvent");
      }
    }
    uv__tcp_queue_read(loop, handle);
  }

  return 0;
}

static int uv__is_loopback(const struct sockaddr_storage* storage) {
  const struct sockaddr_in* in4;
  const struct sockaddr_in6* in6;
  int i;

  if (storage->ss_family == AF_INET) {
    in4 = (const struct sockaddr_in*) storage;
    return in4->sin_addr.S_un.S_un_b.s_b1 == 127;
  }
  if (storage->ss_family == AF_INET6) {
    in6 = (const struct sockaddr_in6*) storage;
    for (i = 0; i < 7; ++i) {
      if (in6->sin6_addr.u.Word[i] != 0)
        return 0;
    }
    return in6->sin6_addr.u.Word[7] == htons(1);
  }
  return 0;
}

// Check if Windows version is 10.0.16299 or later
static int uv__is_fast_loopback_fail_supported(void) {
  OSVERSIONINFOW os_info;
  if (!pRtlGetVersion)
    return 0;
  pRtlGetVersion(&os_info);
  if (os_info.dwMajorVersion < 10)
    return 0;
  if (os_info.dwMajorVersion > 10)
    return 1;
  if (os_info.dwMinorVersion > 0)
    return 1;
  return os_info.dwBuildNumber >= 16299;
}

static int uv__tcp_try_connect(uv_connect_t* req,
                              uv_tcp_t* handle,
                              const struct sockaddr* addr,
                              unsigned int addrlen,
                              uv_connect_cb cb) {
  uv_loop_t* loop = handle->loop;
  TCP_INITIAL_RTO_PARAMETERS retransmit_ioctl;
  const struct sockaddr* bind_addr;
  struct sockaddr_storage converted;
  BOOL success;
  DWORD bytes;
  int err;

  err = uv__convert_to_localhost_if_unspecified(addr, &converted);
  if (err)
    return err;

  if (handle->delayed_error != 0)
    goto out;

  if (!(handle->flags & UV_HANDLE_BOUND)) {
    if (addrlen == sizeof(uv_addr_ip4_any_)) {
      bind_addr = (const struct sockaddr*) &uv_addr_ip4_any_;
    } else if (addrlen == sizeof(uv_addr_ip6_any_)) {
      bind_addr = (const struct sockaddr*) &uv_addr_ip6_any_;
    } else {
      abort();
    }
    err = uv__tcp_try_bind(handle, bind_addr, addrlen, 0);
    if (err)
      return err;
    if (handle->delayed_error != 0)
      goto out;
  }

  if (!handle->tcp.conn.func_connectex) {
    if (!uv__get_connectex_function(handle->socket, &handle->tcp.conn.func_connectex)) {
      return WSAEAFNOSUPPORT;
    }
  }

  /* This makes connect() fail instantly if the target port on the localhost
   * is not reachable, instead of waiting for 2s. We do not care if this fails.
   * This only works on Windows version 10.0.16299 and later.
   */
  if (uv__is_fast_loopback_fail_supported() && uv__is_loopback(&converted)) {
    memset(&retransmit_ioctl, 0, sizeof(retransmit_ioctl));
    retransmit_ioctl.Rtt = TCP_INITIAL_RTO_NO_SYN_RETRANSMISSIONS;
    retransmit_ioctl.MaxSynRetransmissions = TCP_INITIAL_RTO_NO_SYN_RETRANSMISSIONS;
    WSAIoctl(handle->socket,
             SIO_TCP_INITIAL_RTO,
             &retransmit_ioctl,
             sizeof(retransmit_ioctl),
             NULL,
             0,
             &bytes,
             NULL,
             NULL);
  }

out:

  UV_REQ_INIT(req, UV_CONNECT);
  req->handle = (uv_stream_t*) handle;
  req->cb = cb;
  memset(&req->u.io.overlapped, 0, sizeof(req->u.io.overlapped));

  if (handle->delayed_error != 0) {
    /* Process the req without IOCP. */
    handle->reqs_pending++;
    REGISTER_HANDLE_REQ(loop, handle);
    uv__insert_pending_req(loop, (uv_req_t*)req);
    return 0;
  }

  success = handle->tcp.conn.func_connectex(handle->socket,
                                            (const struct sockaddr*) &converted,
                                            addrlen,
                                            NULL,
                                            0,
                                            &bytes,
                                            &req->u.io.overlapped);

  if (UV_SUCCEEDED_WITHOUT_IOCP(success)) {
    /* Process the req without IOCP. */
    handle->reqs_pending++;
    REGISTER_HANDLE_REQ(loop, handle);
    uv__insert_pending_req(loop, (uv_req_t*)req);
  } else if (UV_SUCCEEDED_WITH_IOCP(success)) {
    /* The req will be processed with IOCP. */
    handle->reqs_pending++;
    REGISTER_HANDLE_REQ(loop, handle);
  } else {
    return WSAGetLastError();
  }

  return 0;
}


int uv_tcp_getsockname(const uv_tcp_t* handle,
                       struct sockaddr* name,
                       int* namelen) {

  return uv__getsockpeername((const uv_handle_t*) handle,
                             getsockname,
                             name,
                             namelen,
                             handle->delayed_error);
}


int uv_tcp_getpeername(const uv_tcp_t* handle,
                       struct sockaddr* name,
                       int* namelen) {

  return uv__getsockpeername((const uv_handle_t*) handle,
                             getpeername,
                             name,
                             namelen,
                             handle->delayed_error);
}


int uv__tcp_write(uv_loop_t* loop,
                 uv_write_t* req,
                 uv_tcp_t* handle,
                 const uv_buf_t bufs[],
                 unsigned int nbufs,
                 uv_write_cb cb) {
  int result;
  DWORD bytes;

  UV_REQ_INIT(req, UV_WRITE);
  req->handle = (uv_stream_t*) handle;
  req->cb = cb;

  /* Prepare the overlapped structure. */
  memset(&(req->u.io.overlapped), 0, sizeof(req->u.io.overlapped));
  if (handle->flags & UV_HANDLE_EMULATE_IOCP) {
    req->event_handle = CreateEvent(NULL, 0, 0, NULL);
    if (req->event_handle == NULL) {
      uv_fatal_error(GetLastError(), "CreateEvent");
    }
    req->u.io.overlapped.hEvent = (HANDLE) ((ULONG_PTR) req->event_handle | 1);
    req->wait_handle = INVALID_HANDLE_VALUE;
  }

  result = WSASend(handle->socket,
                   (WSABUF*) bufs,
                   nbufs,
                   &bytes,
                   0,
                   &req->u.io.overlapped,
                   NULL);

  if (UV_SUCCEEDED_WITHOUT_IOCP(result == 0)) {
    /* Request completed immediately. */
    req->u.io.queued_bytes = 0;
    handle->reqs_pending++;
    handle->stream.conn.write_reqs_pending++;
    REGISTER_HANDLE_REQ(loop, handle);
    uv__insert_pending_req(loop, (uv_req_t*) req);
  } else if (UV_SUCCEEDED_WITH_IOCP(result == 0)) {
    /* Request queued by the kernel. */
    req->u.io.queued_bytes = uv__count_bufs(bufs, nbufs);
    handle->reqs_pending++;
    handle->stream.conn.write_reqs_pending++;
    REGISTER_HANDLE_REQ(loop, handle);
    handle->write_queue_size += req->u.io.queued_bytes;
    if (handle->flags & UV_HANDLE_EMULATE_IOCP &&
        !RegisterWaitForSingleObject(&req->wait_handle,
          req->event_handle, post_write_completion, (void*) req,
          INFINITE, WT_EXECUTEINWAITTHREAD | WT_EXECUTEONLYONCE)) {
      SET_REQ_ERROR(req, GetLastError());
      uv__insert_pending_req(loop, (uv_req_t*)req);
    }
  } else {
    /* Send failed due to an error, report it later */
    req->u.io.queued_bytes = 0;
    handle->reqs_pending++;
    handle->stream.conn.write_reqs_pending++;
    REGISTER_HANDLE_REQ(loop, handle);
    SET_REQ_ERROR(req, WSAGetLastError());
    uv__insert_pending_req(loop, (uv_req_t*) req);
  }

  return 0;
}


int uv__tcp_try_write(uv_tcp_t* handle,
                     const uv_buf_t bufs[],
                     unsigned int nbufs) {
  int result;
  DWORD bytes;

  if (handle->stream.conn.write_reqs_pending > 0)
    return UV_EAGAIN;

  result = WSASend(handle->socket,
                   (WSABUF*) bufs,
                   nbufs,
                   &bytes,
                   0,
                   NULL,
                   NULL);

  if (result == SOCKET_ERROR)
    return uv_translate_sys_error(WSAGetLastError());
  else
    return bytes;
}


void uv__process_tcp_read_req(uv_loop_t* loop, uv_tcp_t* handle,
    uv_req_t* req) {
  DWORD bytes, flags, err;
  uv_buf_t buf;
  int count;

  assert(handle->type == UV_TCP);

  handle->flags &= ~UV_HANDLE_READ_PENDING;

  if (!REQ_SUCCESS(req)) {
    /* An error occurred doing the read. */
    if ((handle->flags & UV_HANDLE_READING) ||
        !(handle->flags & UV_HANDLE_ZERO_READ)) {
      handle->flags &= ~UV_HANDLE_READING;
      DECREASE_ACTIVE_COUNT(loop, handle);
      buf = (handle->flags & UV_HANDLE_ZERO_READ) ?
            uv_buf_init(NULL, 0) : handle->tcp.conn.read_buffer;

      err = GET_REQ_SOCK_ERROR(req);

      if (err == WSAECONNABORTED) {
        /* Turn WSAECONNABORTED into UV_ECONNRESET to be consistent with Unix.
         */
        err = WSAECONNRESET;
      }
      handle->flags &= ~(UV_HANDLE_READABLE | UV_HANDLE_WRITABLE);

      handle->read_cb((uv_stream_t*)handle,
                      uv_translate_sys_error(err),
                      &buf);
    }
  } else {
    if (!(handle->flags & UV_HANDLE_ZERO_READ)) {
      /* The read was done with a non-zero buffer length. */
      if (req->u.io.overlapped.InternalHigh > 0) {
        /* Successful read */
        handle->read_cb((uv_stream_t*)handle,
                        req->u.io.overlapped.InternalHigh,
                        &handle->tcp.conn.read_buffer);
        /* Read again only if bytes == buf.len */
        if (req->u.io.overlapped.InternalHigh < handle->tcp.conn.read_buffer.len) {
          goto done;
        }
      } else {
        /* Connection closed */
        if (handle->flags & UV_HANDLE_READING) {
          handle->flags &= ~UV_HANDLE_READING;
          DECREASE_ACTIVE_COUNT(loop, handle);
        }

        buf.base = 0;
        buf.len = 0;
        handle->read_cb((uv_stream_t*)handle, UV_EOF, &handle->tcp.conn.read_buffer);
        goto done;
      }
    }

    /* Do nonblocking reads until the buffer is empty */
    count = 32;
    while ((handle->flags & UV_HANDLE_READING) && (count-- > 0)) {
      buf = uv_buf_init(NULL, 0);
      handle->alloc_cb((uv_handle_t*) handle, 65536, &buf);
      if (buf.base == NULL || buf.len == 0) {
        handle->read_cb((uv_stream_t*) handle, UV_ENOBUFS, &buf);
        break;
      }
      assert(buf.base != NULL);

      flags = 0;
      if (WSARecv(handle->socket,
                  (WSABUF*)&buf,
                  1,
                  &bytes,
                  &flags,
                  NULL,
                  NULL) != SOCKET_ERROR) {
        if (bytes > 0) {
          /* Successful read */
          handle->read_cb((uv_stream_t*)handle, bytes, &buf);
          /* Read again only if bytes == buf.len */
          if (bytes < buf.len) {
            break;
          }
        } else {
          /* Connection closed */
          handle->flags &= ~UV_HANDLE_READING;
          DECREASE_ACTIVE_COUNT(loop, handle);

          handle->read_cb((uv_stream_t*)handle, UV_EOF, &buf);
          break;
        }
      } else {
        err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) {
          /* Read buffer was completely empty, report a 0-byte read. */
          handle->read_cb((uv_stream_t*)handle, 0, &buf);
        } else {
          /* Ouch! serious error. */
          handle->flags &= ~UV_HANDLE_READING;
          DECREASE_ACTIVE_COUNT(loop, handle);

          if (err == WSAECONNABORTED) {
            /* Turn WSAECONNABORTED into UV_ECONNRESET to be consistent with
             * Unix. */
            err = WSAECONNRESET;
          }
          handle->flags &= ~(UV_HANDLE_READABLE | UV_HANDLE_WRITABLE);

          handle->read_cb((uv_stream_t*)handle,
                          uv_translate_sys_error(err),
                          &buf);
        }
        break;
      }
    }

done:
    /* Post another read if still reading and not closing. */
    if ((handle->flags & UV_HANDLE_READING) &&
        !(handle->flags & UV_HANDLE_READ_PENDING)) {
      uv__tcp_queue_read(loop, handle);
    }
  }

  DECREASE_PENDING_REQ_COUNT(handle);
}


void uv__process_tcp_write_req(uv_loop_t* loop, uv_tcp_t* handle,
    uv_write_t* req) {
  int err;

  assert(handle->type == UV_TCP);

  assert(handle->write_queue_size >= req->u.io.queued_bytes);
  handle->write_queue_size -= req->u.io.queued_bytes;

  UNREGISTER_HANDLE_REQ(loop, handle);

  if (handle->flags & UV_HANDLE_EMULATE_IOCP) {
    if (req->wait_handle != INVALID_HANDLE_VALUE) {
      UnregisterWait(req->wait_handle);
      req->wait_handle = INVALID_HANDLE_VALUE;
    }
    if (req->event_handle != NULL) {
      CloseHandle(req->event_handle);
      req->event_handle = NULL;
    }
  }

  if (req->cb) {
    err = uv_translate_sys_error(GET_REQ_SOCK_ERROR(req));
    if (err == UV_ECONNABORTED) {
      /* use UV_ECANCELED for consistency with Unix */
      err = UV_ECANCELED;
    }
    req->cb(req, err);
  }

  handle->stream.conn.write_reqs_pending--;
  if (handle->stream.conn.write_reqs_pending == 0) {
    if (handle->flags & UV_HANDLE_CLOSING) {
      closesocket(handle->socket);
      handle->socket = INVALID_SOCKET;
    }
    if (uv__is_stream_shutting(handle))
      uv__process_tcp_shutdown_req(loop,
                                   handle,
                                   handle->stream.conn.shutdown_req);
  }

  DECREASE_PENDING_REQ_COUNT(handle);
}


void uv__process_tcp_accept_req(uv_loop_t* loop, uv_tcp_t* handle,
    uv_req_t* raw_req) {
  uv_tcp_accept_t* req = (uv_tcp_accept_t*) raw_req;
  int err;

  assert(handle->type == UV_TCP);

  /* If handle->accepted_socket is not a valid socket, then uv_queue_accept
   * must have failed. This is a serious error. We stop accepting connections
   * and report this error to the connection callback. */
  if (req->accept_socket == INVALID_SOCKET) {
    if (handle->flags & UV_HANDLE_LISTENING) {
      handle->flags &= ~UV_HANDLE_LISTENING;
      DECREASE_ACTIVE_COUNT(loop, handle);
      if (handle->stream.serv.connection_cb) {
        err = GET_REQ_SOCK_ERROR(req);
        handle->stream.serv.connection_cb((uv_stream_t*)handle,
                                      uv_translate_sys_error(err));
      }
    }
  } else if (REQ_SUCCESS(req) &&
      setsockopt(req->accept_socket,
                  SOL_SOCKET,
                  SO_UPDATE_ACCEPT_CONTEXT,
                  (char*)&handle->socket,
                  sizeof(handle->socket)) == 0) {
    req->next_pending = handle->tcp.serv.pending_accepts;
    handle->tcp.serv.pending_accepts = req;

    /* Accept and SO_UPDATE_ACCEPT_CONTEXT were successful. */
    if (handle->stream.serv.connection_cb) {
      handle->stream.serv.connection_cb((uv_stream_t*)handle, 0);
    }
  } else {
    /* Error related to accepted socket is ignored because the server socket
     * may still be healthy. If the server socket is broken uv_queue_accept
     * will detect it. */
    closesocket(req->accept_socket);
    req->accept_socket = INVALID_SOCKET;
    if (handle->flags & UV_HANDLE_LISTENING) {
      uv__tcp_queue_accept(handle, req);
    }
  }

  DECREASE_PENDING_REQ_COUNT(handle);
}


void uv__process_tcp_connect_req(uv_loop_t* loop, uv_tcp_t* handle,
    uv_connect_t* req) {
  int err;

  assert(handle->type == UV_TCP);

  UNREGISTER_HANDLE_REQ(loop, handle);

  err = 0;
  if (handle->delayed_error) {
    /* To smooth over the differences between unixes errors that
     * were reported synchronously on the first connect can be delayed
     * until the next tick--which is now.
     */
    err = handle->delayed_error;
    handle->delayed_error = 0;
  } else if (REQ_SUCCESS(req)) {
    if (handle->flags & UV_HANDLE_CLOSING) {
      /* use UV_ECANCELED for consistency with Unix */
      err = ERROR_OPERATION_ABORTED;
    } else if (setsockopt(handle->socket,
                          SOL_SOCKET,
                          SO_UPDATE_CONNECT_CONTEXT,
                          NULL,
                          0) == 0) {
      uv__connection_init((uv_stream_t*)handle);
      handle->flags |= UV_HANDLE_READABLE | UV_HANDLE_WRITABLE;
    } else {
      err = WSAGetLastError();
    }
  } else {
    err = GET_REQ_SOCK_ERROR(req);
  }
  req->cb(req, uv_translate_sys_error(err));

  DECREASE_PENDING_REQ_COUNT(handle);
}


int uv__tcp_xfer_export(uv_tcp_t* handle,
                        int target_pid,
                        uv__ipc_socket_xfer_type_t* xfer_type,
                        uv__ipc_socket_xfer_info_t* xfer_info) {
  if (handle->flags & UV_HANDLE_CONNECTION) {
    *xfer_type = UV__IPC_SOCKET_XFER_TCP_CONNECTION;
  } else {
    *xfer_type = UV__IPC_SOCKET_XFER_TCP_SERVER;
    /* We're about to share the socket with another process. Because this is a
     * listening socket, we assume that the other process will be accepting
     * connections on it. Thus, before sharing the socket with another process,
     * we call listen here in the parent process. */
    if (!(handle->flags & UV_HANDLE_LISTENING)) {
      if (!(handle->flags & UV_HANDLE_BOUND)) {
        return ERROR_NOT_SUPPORTED;
      }
      if (handle->delayed_error == 0 &&
          listen(handle->socket, SOMAXCONN) == SOCKET_ERROR) {
        handle->delayed_error = WSAGetLastError();
      }
    }
  }

  if (WSADuplicateSocketW(handle->socket, target_pid, &xfer_info->socket_info))
    return WSAGetLastError();
  xfer_info->delayed_error = handle->delayed_error;

  /* Mark the local copy of the handle as 'shared' so we behave in a way that's
   * friendly to the process(es) that we share the socket with. */
  handle->flags |= UV_HANDLE_SHARED_TCP_SOCKET;

  return 0;
}


int uv__tcp_xfer_import(uv_tcp_t* tcp,
                        uv__ipc_socket_xfer_type_t xfer_type,
                        uv__ipc_socket_xfer_info_t* xfer_info) {
  int err;
  SOCKET socket;

  assert(xfer_type == UV__IPC_SOCKET_XFER_TCP_SERVER ||
         xfer_type == UV__IPC_SOCKET_XFER_TCP_CONNECTION);

  socket = WSASocketW(FROM_PROTOCOL_INFO,
                      FROM_PROTOCOL_INFO,
                      FROM_PROTOCOL_INFO,
                      &xfer_info->socket_info,
                      0,
                      WSA_FLAG_OVERLAPPED);

  if (socket == INVALID_SOCKET) {
    return WSAGetLastError();
  }

  err = uv__tcp_set_socket(
      tcp->loop, tcp, socket, xfer_info->socket_info.iAddressFamily, 1);
  if (err) {
    closesocket(socket);
    return err;
  }

  tcp->delayed_error = xfer_info->delayed_error;
  tcp->flags |= UV_HANDLE_BOUND | UV_HANDLE_SHARED_TCP_SOCKET;

  if (xfer_type == UV__IPC_SOCKET_XFER_TCP_CONNECTION) {
    uv__connection_init((uv_stream_t*)tcp);
    tcp->flags |= UV_HANDLE_READABLE | UV_HANDLE_WRITABLE;
  }

  return 0;
}


int uv_tcp_nodelay(uv_tcp_t* handle, int enable) {
  int err;

  if (handle->socket != INVALID_SOCKET) {
    err = uv__tcp_nodelay(handle, handle->socket, enable);
    if (err)
      return uv_translate_sys_error(err);
  }

  if (enable) {
    handle->flags |= UV_HANDLE_TCP_NODELAY;
  } else {
    handle->flags &= ~UV_HANDLE_TCP_NODELAY;
  }

  return 0;
}


int uv_tcp_keepalive(uv_tcp_t* handle, int enable, unsigned int delay) {
  int err;

  if (handle->socket != INVALID_SOCKET) {
    err = uv__tcp_keepalive(handle, handle->socket, enable, delay);
    if (err)
      return uv_translate_sys_error(err);
  }

  if (enable) {
    handle->flags |= UV_HANDLE_TCP_KEEPALIVE;
  } else {
    handle->flags &= ~UV_HANDLE_TCP_KEEPALIVE;
  }

  /* TODO: Store delay if handle->socket isn't created yet. */

  return 0;
}


int uv_tcp_simultaneous_accepts(uv_tcp_t* handle, int enable) {
  if (handle->flags & UV_HANDLE_CONNECTION) {
    return UV_EINVAL;
  }

  /* Check if we're already in the desired mode. */
  if ((enable && !(handle->flags & UV_HANDLE_TCP_SINGLE_ACCEPT)) ||
      (!enable && handle->flags & UV_HANDLE_TCP_SINGLE_ACCEPT)) {
    return 0;
  }

  /* Don't allow switching from single pending accept to many. */
  if (enable) {
    return UV_ENOTSUP;
  }

  /* Check if we're in a middle of changing the number of pending accepts. */
  if (handle->flags & UV_HANDLE_TCP_ACCEPT_STATE_CHANGING) {
    return 0;
  }

  handle->flags |= UV_HANDLE_TCP_SINGLE_ACCEPT;

  /* Flip the changing flag if we have already queued multiple accepts. */
  if (handle->flags & UV_HANDLE_LISTENING) {
    handle->flags |= UV_HANDLE_TCP_ACCEPT_STATE_CHANGING;
  }

  return 0;
}


static void uv__tcp_try_cancel_reqs(uv_tcp_t* tcp) {
  SOCKET socket;
  int non_ifs_lsp;
  int reading;
  int writing;

  socket = tcp->socket;
  reading = tcp->flags & UV_HANDLE_READ_PENDING;
  writing = tcp->stream.conn.write_reqs_pending > 0;
  if (!reading && !writing)
    return;

  /* TODO: in libuv v2, keep explicit track of write_reqs, so we can cancel
   * them each explicitly with CancelIoEx (like unix). */
  if (reading)
    CancelIoEx((HANDLE) socket, &tcp->read_req.u.io.overlapped);
  if (writing)
    CancelIo((HANDLE) socket);

  /* Check if we have any non-IFS LSPs stacked on top of TCP */
  non_ifs_lsp = (tcp->flags & UV_HANDLE_IPV6) ? uv_tcp_non_ifs_lsp_ipv6 :
                                                uv_tcp_non_ifs_lsp_ipv4;

  /* If there are non-ifs LSPs then try to obtain a base handle for the socket.
   */
  if (non_ifs_lsp) {
    DWORD bytes;
    if (WSAIoctl(socket,
                 SIO_BASE_HANDLE,
                 NULL,
                 0,
                 &socket,
                 sizeof socket,
                 &bytes,
                 NULL,
                 NULL) != 0) {
      /* Failed. We can't do CancelIo. */
      return;
    }
  }

  assert(socket != 0 && socket != INVALID_SOCKET);

  if (socket != tcp->socket) {
    if (reading)
      CancelIoEx((HANDLE) socket, &tcp->read_req.u.io.overlapped);
    if (writing)
      CancelIo((HANDLE) socket);
  }
}


void uv__tcp_close(uv_loop_t* loop, uv_tcp_t* tcp) {
  if (tcp->flags & UV_HANDLE_CONNECTION) {
    if (tcp->flags & UV_HANDLE_READING) {
      uv_read_stop((uv_stream_t*) tcp);
    }
    uv__tcp_try_cancel_reqs(tcp);
  } else {
    if (tcp->tcp.serv.accept_reqs != NULL) {
      /* First close the incoming sockets to cancel the accept operations before
       * we free their resources. */
      unsigned int i;
      for (i = 0; i < uv_simultaneous_server_accepts; i++) {
        uv_tcp_accept_t* req = &tcp->tcp.serv.accept_reqs[i];
        if (req->accept_socket != INVALID_SOCKET) {
          closesocket(req->accept_socket);
          req->accept_socket = INVALID_SOCKET;
        }
      }
    }
    assert(!(tcp->flags & UV_HANDLE_READING));
  }

  if (tcp->flags & UV_HANDLE_LISTENING) {
    tcp->flags &= ~UV_HANDLE_LISTENING;
    DECREASE_ACTIVE_COUNT(loop, tcp);
  }

  tcp->flags &= ~(UV_HANDLE_READABLE | UV_HANDLE_WRITABLE);
  uv__handle_closing(tcp);

  /* If any overlapped req failed to cancel, calling `closesocket` now would
   * cause Win32 to send an RST packet. Try to avoid that for writes, if
   * possibly applicable, by waiting to process the completion notifications
   * first (which typically should be cancellations). There's not much we can
   * do about canceled reads, which also will generate an RST packet. */
  if (!(tcp->flags & UV_HANDLE_CONNECTION) ||
      tcp->stream.conn.write_reqs_pending == 0) {
    closesocket(tcp->socket);
    tcp->socket = INVALID_SOCKET;
  }

  if (tcp->reqs_pending == 0)
    uv__want_endgame(loop, (uv_handle_t*) tcp);
}


int uv_tcp_open(uv_tcp_t* handle, uv_os_sock_t sock) {
  WSAPROTOCOL_INFOW protocol_info;
  int opt_len;
  int err;
  struct sockaddr_storage saddr;
  int saddr_len;

  /* Detect the address family of the socket. */
  opt_len = (int) sizeof protocol_info;
  if (getsockopt(sock,
                 SOL_SOCKET,
                 SO_PROTOCOL_INFOW,
                 (char*) &protocol_info,
                 &opt_len) == SOCKET_ERROR) {
    return uv_translate_sys_error(GetLastError());
  }

  err = uv__tcp_set_socket(handle->loop,
                          handle,
                          sock,
                          protocol_info.iAddressFamily,
                          1);
  if (err) {
    return uv_translate_sys_error(err);
  }

  /* Support already active socket. */
  saddr_len = sizeof(saddr);
  if (!uv_tcp_getsockname(handle, (struct sockaddr*) &saddr, &saddr_len)) {
    /* Socket is already bound. */
    handle->flags |= UV_HANDLE_BOUND;
    saddr_len = sizeof(saddr);
    if (!uv_tcp_getpeername(handle, (struct sockaddr*) &saddr, &saddr_len)) {
      /* Socket is already connected. */
      uv__connection_init((uv_stream_t*) handle);
      handle->flags |= UV_HANDLE_READABLE | UV_HANDLE_WRITABLE;
    }
  }

  return 0;
}


/* This function is an egress point, i.e. it returns libuv errors rather than
 * system errors.
 */
int uv__tcp_bind(uv_tcp_t* handle,
                 const struct sockaddr* addr,
                 unsigned int addrlen,
                 unsigned int flags) {
  int err;

  err = uv__tcp_try_bind(handle, addr, addrlen, flags);
  if (err)
    return uv_translate_sys_error(err);

  return 0;
}


/* This function is an egress point, i.e. it returns libuv errors rather than
 * system errors.
 */
int uv__tcp_connect(uv_connect_t* req,
                    uv_tcp_t* handle,
                    const struct sockaddr* addr,
                    unsigned int addrlen,
                    uv_connect_cb cb) {
  int err;

  err = uv__tcp_try_connect(req, handle, addr, addrlen, cb);
  if (err)
    return uv_translate_sys_error(err);

  return 0;
}


int uv_socketpair(int type, int protocol, uv_os_sock_t fds[2], int flags0, int flags1) {
  SOCKET server = INVALID_SOCKET;
  SOCKET client0 = INVALID_SOCKET;
  SOCKET client1 = INVALID_SOCKET;
  SOCKADDR_IN name;
  LPFN_ACCEPTEX func_acceptex;
  WSAOVERLAPPED overlap;
  char accept_buffer[sizeof(struct sockaddr_storage) * 2 + 32];
  int namelen;
  int err;
  DWORD bytes;
  DWORD flags;
  DWORD client0_flags = WSA_FLAG_NO_HANDLE_INHERIT;
  DWORD client1_flags = WSA_FLAG_NO_HANDLE_INHERIT;

  if (flags0 & UV_NONBLOCK_PIPE)
      client0_flags |= WSA_FLAG_OVERLAPPED;
  if (flags1 & UV_NONBLOCK_PIPE)
      client1_flags |= WSA_FLAG_OVERLAPPED;

  server = WSASocketW(AF_INET, type, protocol, NULL, 0,
                      WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);
  if (server == INVALID_SOCKET)
    goto wsaerror;
  if (!SetHandleInformation((HANDLE) server, HANDLE_FLAG_INHERIT, 0))
    goto error;
  name.sin_family = AF_INET;
  name.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  name.sin_port = 0;
  if (bind(server, (SOCKADDR*) &name, sizeof(name)) != 0)
    goto wsaerror;
  if (listen(server, 1) != 0)
    goto wsaerror;
  namelen = sizeof(name);
  if (getsockname(server, (SOCKADDR*) &name, &namelen) != 0)
    goto wsaerror;
  client0 = WSASocketW(AF_INET, type, protocol, NULL, 0, client0_flags);
  if (client0 == INVALID_SOCKET)
    goto wsaerror;
  if (!SetHandleInformation((HANDLE) client0, HANDLE_FLAG_INHERIT, 0))
    goto error;
  if (connect(client0, (SOCKADDR*) &name, sizeof(name)) != 0)
    goto wsaerror;
  client1 = WSASocketW(AF_INET, type, protocol, NULL, 0, client1_flags);
  if (client1 == INVALID_SOCKET)
    goto wsaerror;
  if (!SetHandleInformation((HANDLE) client1, HANDLE_FLAG_INHERIT, 0))
    goto error;
  if (!uv__get_acceptex_function(server, &func_acceptex)) {
    err = WSAEAFNOSUPPORT;
    goto cleanup;
  }
  memset(&overlap, 0, sizeof(overlap));
  if (!func_acceptex(server,
                     client1,
                     accept_buffer,
                     0,
                     sizeof(struct sockaddr_storage),
                     sizeof(struct sockaddr_storage),
                     &bytes,
                     &overlap)) {
    err = WSAGetLastError();
    if (err == ERROR_IO_PENDING) {
      /* Result should complete immediately, since we already called connect,
       * but empirically, we sometimes have to poll the kernel a couple times
       * until it notices that. */
      while (!WSAGetOverlappedResult(client1, &overlap, &bytes, FALSE, &flags)) {
        err = WSAGetLastError();
        if (err != WSA_IO_INCOMPLETE)
          goto cleanup;
        SwitchToThread();
      }
    }
    else {
      goto cleanup;
    }
  }
  if (setsockopt(client1, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
                  (char*) &server, sizeof(server)) != 0) {
    goto wsaerror;
  }

  closesocket(server);

  fds[0] = client0;
  fds[1] = client1;

  return 0;

 wsaerror:
    err = WSAGetLastError();
    goto cleanup;

 error:
    err = GetLastError();
    goto cleanup;

 cleanup:
    if (server != INVALID_SOCKET)
      closesocket(server);
    if (client0 != INVALID_SOCKET)
      closesocket(client0);
    if (client1 != INVALID_SOCKET)
      closesocket(client1);

    assert(err);
    return uv_translate_sys_error(err);
}
