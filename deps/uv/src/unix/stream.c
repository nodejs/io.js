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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <unistd.h>
#include <limits.h> /* IOV_MAX */

#if defined(__APPLE__)
# include <sys/event.h>
# include <sys/time.h>
# include <sys/select.h>

/* Forward declaration */
typedef struct uv__stream_select_s uv__stream_select_t;

struct uv__stream_select_s {
  uv_stream_t* stream;
  uv_thread_t thread;
  uv_sem_t close_sem;
  uv_sem_t async_sem;
  uv_async_t async;
  int events;
  int fake_fd;
  int int_fd;
  int fd;
  fd_set* sread;
  size_t sread_sz;
  fd_set* swrite;
  size_t swrite_sz;
};
#endif /* defined(__APPLE__) */

union uv__cmsg {
  struct cmsghdr hdr;
  /* This cannot be larger because of the IBMi PASE limitation that
   * the total size of control messages cannot exceed 256 bytes.
   */
  char pad[256];
};

STATIC_ASSERT(256 == sizeof(union uv__cmsg));

static void uv__stream_connect(uv_stream_t*);
static void uv__write(uv_stream_t* stream);
static void uv__read(uv_stream_t* stream);
static void uv__stream_io(uv_loop_t* loop, uv__io_t* w, unsigned int events);
static void uv__write_callbacks(uv_stream_t* stream);
static size_t uv__write_req_size(uv_write_t* req);
static void uv__drain(uv_stream_t* stream);


void uv__stream_init(uv_loop_t* loop,
                     uv_stream_t* stream,
                     uv_handle_type type) {
  int err;

  uv__handle_init(loop, (uv_handle_t*)stream, type);
  stream->read_cb = NULL;
  stream->alloc_cb = NULL;
  stream->close_cb = NULL;
  stream->connection_cb = NULL;
  stream->connect_req = NULL;
  stream->shutdown_req = NULL;
  stream->accepted_fd = -1;
  stream->queued_fds = NULL;
  stream->delayed_error = 0;
  uv__queue_init(&stream->write_queue);
  uv__queue_init(&stream->write_completed_queue);
  stream->write_queue_size = 0;

  if (loop->emfile_fd == -1) {
    err = uv__open_cloexec("/dev/null", O_RDONLY);
    if (err < 0)
        /* In the rare case that "/dev/null" isn't mounted open "/"
         * instead.
         */
        err = uv__open_cloexec("/", O_RDONLY);
    if (err >= 0)
      loop->emfile_fd = err;
  }

#if defined(__APPLE__)
  stream->select = NULL;
#endif /* defined(__APPLE_) */

  uv__io_init(&stream->io_watcher, uv__stream_io, -1);
}


static void uv__stream_osx_interrupt_select(uv_stream_t* stream) {
#if defined(__APPLE__)
  /* Notify select() thread about state change */
  uv__stream_select_t* s;
  int r;

  s = stream->select;
  if (s == NULL)
    return;

  /* Interrupt select() loop
   * NOTE: fake_fd and int_fd are socketpair(), thus writing to one will
   * emit read event on other side
   */
  do
    r = write(s->fake_fd, "x", 1);
  while (r == -1 && errno == EINTR);

  assert(r == 1);
#else  /* !defined(__APPLE__) */
  /* No-op on any other platform */
#endif  /* !defined(__APPLE__) */
}


#if defined(__APPLE__)
static void uv__stream_osx_select(void* arg) {
  uv_stream_t* stream;
  uv__stream_select_t* s;
  char buf[1024];
  int events;
  int fd;
  int r;
  int max_fd;

  stream = arg;
  s = stream->select;
  fd = s->fd;

  if (fd > s->int_fd)
    max_fd = fd;
  else
    max_fd = s->int_fd;

  for (;;) {
    /* Terminate on semaphore */
    if (uv_sem_trywait(&s->close_sem) == 0)
      break;

    /* Watch fd using select(2) */
    memset(s->sread, 0, s->sread_sz);
    memset(s->swrite, 0, s->swrite_sz);

    if (uv__io_active(&stream->io_watcher, POLLIN))
      FD_SET(fd, s->sread);
    if (uv__io_active(&stream->io_watcher, POLLOUT))
      FD_SET(fd, s->swrite);
    FD_SET(s->int_fd, s->sread);

    /* Wait indefinitely for fd events */
    r = select(max_fd + 1, s->sread, s->swrite, NULL, NULL);
    if (r == -1) {
      if (errno == EINTR)
        continue;

      /* XXX: Possible?! */
      abort();
    }

    /* Ignore timeouts */
    if (r == 0)
      continue;

    /* Empty socketpair's buffer in case of interruption */
    if (FD_ISSET(s->int_fd, s->sread))
      for (;;) {
        r = read(s->int_fd, buf, sizeof(buf));

        if (r == sizeof(buf))
          continue;

        if (r != -1)
          break;

        if (errno == EAGAIN || errno == EWOULDBLOCK)
          break;

        if (errno == EINTR)
          continue;

        abort();
      }

    /* Handle events */
    events = 0;
    if (FD_ISSET(fd, s->sread))
      events |= POLLIN;
    if (FD_ISSET(fd, s->swrite))
      events |= POLLOUT;

    assert(events != 0 || FD_ISSET(s->int_fd, s->sread));
    if (events != 0) {
      ACCESS_ONCE(int, s->events) = events;

      uv_async_send(&s->async);
      uv_sem_wait(&s->async_sem);

      /* Should be processed at this stage */
      assert((s->events == 0) || (stream->flags & UV_HANDLE_CLOSING));
    }
  }
}


static void uv__stream_osx_select_cb(uv_async_t* handle) {
  uv__stream_select_t* s;
  uv_stream_t* stream;
  int events;

  s = container_of(handle, uv__stream_select_t, async);
  stream = s->stream;

  /* Get and reset stream's events */
  events = s->events;
  ACCESS_ONCE(int, s->events) = 0;

  assert(events != 0);
  assert(events == (events & (POLLIN | POLLOUT)));

  /* Invoke callback on event-loop */
  if ((events & POLLIN) && uv__io_active(&stream->io_watcher, POLLIN))
    uv__stream_io(stream->loop, &stream->io_watcher, POLLIN);

  if ((events & POLLOUT) && uv__io_active(&stream->io_watcher, POLLOUT))
    uv__stream_io(stream->loop, &stream->io_watcher, POLLOUT);

  if (stream->flags & UV_HANDLE_CLOSING)
    return;

  /* NOTE: It is important to do it here, otherwise `select()` might be called
   * before the actual `uv__read()`, leading to the blocking syscall
   */
  uv_sem_post(&s->async_sem);
}


static void uv__stream_osx_cb_close(uv_handle_t* async) {
  uv__stream_select_t* s;

  s = container_of(async, uv__stream_select_t, async);
  uv__free(s);
}


int uv__stream_try_select(uv_stream_t* stream, int* fd) {
  /*
   * kqueue doesn't work with some files from /dev mount on osx.
   * select(2) in separate thread for those fds
   */

  struct kevent filter[1];
  struct kevent events[1];
  struct timespec timeout;
  uv__stream_select_t* s;
  int fds[2];
  int err;
  int ret;
  int kq;
  int old_fd;
  int max_fd;
  size_t sread_sz;
  size_t swrite_sz;

  kq = kqueue();
  if (kq == -1) {
    perror("(libuv) kqueue()");
    return UV__ERR(errno);
  }

  EV_SET(&filter[0], *fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, 0);

  /* Use small timeout, because we only want to capture EINVALs */
  timeout.tv_sec = 0;
  timeout.tv_nsec = 1;

  do
    ret = kevent(kq, filter, 1, events, 1, &timeout);
  while (ret == -1 && errno == EINTR);

  uv__close(kq);

  if (ret == -1)
    return UV__ERR(errno);

  if (ret == 0 || (events[0].flags & EV_ERROR) == 0 || events[0].data != EINVAL)
    return 0;

  /* At this point we definitely know that this fd won't work with kqueue */

  /*
   * Create fds for io watcher and to interrupt the select() loop.
   * NOTE: do it ahead of malloc below to allocate enough space for fd_sets
   */
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds))
    return UV__ERR(errno);

  max_fd = *fd;
  if (fds[1] > max_fd)
    max_fd = fds[1];

  sread_sz = ROUND_UP(max_fd + 1, sizeof(uint32_t) * NBBY) / NBBY;
  swrite_sz = sread_sz;

  s = uv__malloc(sizeof(*s) + sread_sz + swrite_sz);
  if (s == NULL) {
    err = UV_ENOMEM;
    goto failed_malloc;
  }

  s->events = 0;
  s->fd = *fd;
  s->sread = (fd_set*) ((char*) s + sizeof(*s));
  s->sread_sz = sread_sz;
  s->swrite = (fd_set*) ((char*) s->sread + sread_sz);
  s->swrite_sz = swrite_sz;

  err = uv_async_init(stream->loop, &s->async, uv__stream_osx_select_cb);
  if (err)
    goto failed_async_init;

  s->async.flags |= UV_HANDLE_INTERNAL;
  uv__handle_unref(&s->async);

  err = uv_sem_init(&s->close_sem, 0);
  if (err != 0)
    goto failed_close_sem_init;

  err = uv_sem_init(&s->async_sem, 0);
  if (err != 0)
    goto failed_async_sem_init;

  s->fake_fd = fds[0];
  s->int_fd = fds[1];

  old_fd = *fd;
  s->stream = stream;
  stream->select = s;
  *fd = s->fake_fd;

  err = uv_thread_create(&s->thread, uv__stream_osx_select, stream);
  if (err != 0)
    goto failed_thread_create;

  return 0;

failed_thread_create:
  s->stream = NULL;
  stream->select = NULL;
  *fd = old_fd;

  uv_sem_destroy(&s->async_sem);

failed_async_sem_init:
  uv_sem_destroy(&s->close_sem);

failed_close_sem_init:
  uv__close(fds[0]);
  uv__close(fds[1]);
  uv_close((uv_handle_t*) &s->async, uv__stream_osx_cb_close);
  return err;

failed_async_init:
  uv__free(s);

failed_malloc:
  uv__close(fds[0]);
  uv__close(fds[1]);

  return err;
}
#endif /* defined(__APPLE__) */


int uv__stream_open(uv_stream_t* stream, int fd, int flags) {
#if defined(__APPLE__)
  int enable;
#endif

  if (!(stream->io_watcher.fd == -1 || stream->io_watcher.fd == fd))
    return UV_EBUSY;

  assert(fd >= 0);
  stream->flags |= flags;

  if (stream->type == UV_TCP) {
    if ((stream->flags & UV_HANDLE_TCP_NODELAY) && uv__tcp_nodelay(fd, 1))
      return UV__ERR(errno);

    /* TODO Use delay the user passed in. */
    if ((stream->flags & UV_HANDLE_TCP_KEEPALIVE) &&
        uv__tcp_keepalive(fd, 1, 60)) {
      return UV__ERR(errno);
    }
  }

#if defined(__APPLE__)
  enable = 1;
  if (setsockopt(fd, SOL_SOCKET, SO_OOBINLINE, &enable, sizeof(enable)) &&
      errno != ENOTSOCK &&
      errno != EINVAL) {
    return UV__ERR(errno);
  }
#endif

  stream->io_watcher.fd = fd;

  return 0;
}


void uv__stream_flush_write_queue(uv_stream_t* stream, int error) {
  uv_write_t* req;
  struct uv__queue* q;
  while (!uv__queue_empty(&stream->write_queue)) {
    q = uv__queue_head(&stream->write_queue);
    uv__queue_remove(q);

    req = uv__queue_data(q, uv_write_t, queue);
    req->error = error;

    uv__queue_insert_tail(&stream->write_completed_queue, &req->queue);
  }
}


void uv__stream_destroy(uv_stream_t* stream) {
  assert(!uv__io_active(&stream->io_watcher, POLLIN | POLLOUT));
  assert(stream->flags & UV_HANDLE_CLOSED);

  if (stream->connect_req) {
    uv__req_unregister(stream->loop);
    stream->connect_req->cb(stream->connect_req, UV_ECANCELED);
    stream->connect_req = NULL;
  }

  uv__stream_flush_write_queue(stream, UV_ECANCELED);
  uv__write_callbacks(stream);
  uv__drain(stream);

  assert(stream->write_queue_size == 0);
}


/* Implements a best effort approach to mitigating accept() EMFILE errors.
 * We have a spare file descriptor stashed away that we close to get below
 * the EMFILE limit. Next, we accept all pending connections and close them
 * immediately to signal the clients that we're overloaded - and we are, but
 * we still keep on trucking.
 *
 * There is one caveat: it's not reliable in a multi-threaded environment.
 * The file descriptor limit is per process. Our party trick fails if another
 * thread opens a file or creates a socket in the time window between us
 * calling close() and accept().
 */
static int uv__emfile_trick(uv_loop_t* loop, int accept_fd) {
  int err;
  int emfile_fd;

  if (loop->emfile_fd == -1)
    return UV_EMFILE;

  uv__close(loop->emfile_fd);
  loop->emfile_fd = -1;

  do {
    err = uv__accept(accept_fd);
    if (err >= 0)
      uv__close(err);
  } while (err >= 0 || err == UV_EINTR);

  emfile_fd = uv__open_cloexec("/", O_RDONLY);
  if (emfile_fd >= 0)
    loop->emfile_fd = emfile_fd;

  return err;
}


void uv__server_io(uv_loop_t* loop, uv__io_t* w, unsigned int events) {
  uv_stream_t* stream;
  int err;
  int fd;

  stream = container_of(w, uv_stream_t, io_watcher);
  assert(events & POLLIN);
  assert(stream->accepted_fd == -1);
  assert(!(stream->flags & UV_HANDLE_CLOSING));

  fd = uv__stream_fd(stream);
  err = uv__accept(fd);

  if (err == UV_EMFILE || err == UV_ENFILE)
    err = uv__emfile_trick(loop, fd);  /* Shed load. */

  if (err < 0)
    return;

  stream->accepted_fd = err;
  stream->connection_cb(stream, 0);

  if (stream->accepted_fd != -1)
    /* The user hasn't yet accepted called uv_accept() */
    uv__io_stop(loop, &stream->io_watcher, POLLIN);
}


int uv_accept(uv_stream_t* server, uv_stream_t* client) {
  int err;

  assert(server->loop == client->loop);

  if (server->accepted_fd == -1)
    return UV_EAGAIN;

  switch (client->type) {
    case UV_NAMED_PIPE:
    case UV_TCP:
      err = uv__stream_open(client,
                            server->accepted_fd,
                            UV_HANDLE_READABLE | UV_HANDLE_WRITABLE);
      if (err) {
        /* TODO handle error */
        uv__close(server->accepted_fd);
        goto done;
      }
      break;

    case UV_UDP:
      err = uv_udp_open((uv_udp_t*) client, server->accepted_fd);
      if (err) {
        uv__close(server->accepted_fd);
        goto done;
      }
      break;

    default:
      return UV_EINVAL;
  }

  client->flags |= UV_HANDLE_BOUND;

done:
  /* Process queued fds */
  if (server->queued_fds != NULL) {
    uv__stream_queued_fds_t* queued_fds;

    queued_fds = server->queued_fds;

    /* Read first */
    server->accepted_fd = queued_fds->fds[0];

    /* All read, free */
    assert(queued_fds->offset > 0);
    if (--queued_fds->offset == 0) {
      uv__free(queued_fds);
      server->queued_fds = NULL;
    } else {
      /* Shift rest */
      memmove(queued_fds->fds,
              queued_fds->fds + 1,
              queued_fds->offset * sizeof(*queued_fds->fds));
    }
  } else {
    server->accepted_fd = -1;
    if (err == 0)
      uv__io_start(server->loop, &server->io_watcher, POLLIN);
  }
  return err;
}


int uv_listen(uv_stream_t* stream, int backlog, uv_connection_cb cb) {
  int err;
  if (uv__is_closing(stream)) {
    return UV_EINVAL;
  }
  switch (stream->type) {
  case UV_TCP:
    err = uv__tcp_listen((uv_tcp_t*)stream, backlog, cb);
    break;

  case UV_NAMED_PIPE:
    err = uv__pipe_listen((uv_pipe_t*)stream, backlog, cb);
    break;

  default:
    err = UV_EINVAL;
  }

  if (err == 0)
    uv__handle_start(stream);

  return err;
}


static void uv__drain(uv_stream_t* stream) {
  uv_shutdown_t* req;
  int err;

  assert(uv__queue_empty(&stream->write_queue));
  if (!(stream->flags & UV_HANDLE_CLOSING)) {
    uv__io_stop(stream->loop, &stream->io_watcher, POLLOUT);
    uv__stream_osx_interrupt_select(stream);
  }

  if (!uv__is_stream_shutting(stream))
    return;

  req = stream->shutdown_req;
  assert(req);

  if ((stream->flags & UV_HANDLE_CLOSING) ||
      !(stream->flags & UV_HANDLE_SHUT)) {
    stream->shutdown_req = NULL;
    uv__req_unregister(stream->loop);

    err = 0;
    if (stream->flags & UV_HANDLE_CLOSING)
      /* The user destroyed the stream before we got to do the shutdown. */
      err = UV_ECANCELED;
    else if (shutdown(uv__stream_fd(stream), SHUT_WR))
      err = UV__ERR(errno);
    else /* Success. */
      stream->flags |= UV_HANDLE_SHUT;

    if (req->cb != NULL)
      req->cb(req, err);
  }
}


static ssize_t uv__writev(int fd, struct iovec* vec, size_t n) {
  if (n == 1)
    return write(fd, vec->iov_base, vec->iov_len);
  else
    return writev(fd, vec, n);
}


static size_t uv__write_req_size(uv_write_t* req) {
  size_t size;

  assert(req->bufs != NULL);
  size = uv__count_bufs(req->bufs + req->write_index,
                        req->nbufs - req->write_index);
  assert(req->handle->write_queue_size >= size);

  return size;
}


/* Returns 1 if all write request data has been written, or 0 if there is still
 * more data to write.
 *
 * Note: the return value only says something about the *current* request.
 * There may still be other write requests sitting in the queue.
 */
static int uv__write_req_update(uv_stream_t* stream,
                                uv_write_t* req,
                                size_t n) {
  uv_buf_t* buf;
  size_t len;

  assert(n <= stream->write_queue_size);
  stream->write_queue_size -= n;

  buf = req->bufs + req->write_index;

  do {
    len = n < buf->len ? n : buf->len;
    if (buf->len != 0)
      buf->base += len;
    buf->len -= len;
    buf += (buf->len == 0);  /* Advance to next buffer if this one is empty. */
    n -= len;
  } while (n > 0);

  req->write_index = buf - req->bufs;

  return req->write_index == req->nbufs;
}


static void uv__write_req_finish(uv_write_t* req) {
  uv_stream_t* stream = req->handle;

  /* Pop the req off tcp->write_queue. */
  uv__queue_remove(&req->queue);

  /* Only free when there was no error. On error, we touch up write_queue_size
   * right before making the callback. The reason we don't do that right away
   * is that a write_queue_size > 0 is our only way to signal to the user that
   * they should stop writing - which they should if we got an error. Something
   * to revisit in future revisions of the libuv API.
   */
  if (req->error == 0) {
    if (req->bufs != req->bufsml)
      uv__free(req->bufs);
    req->bufs = NULL;
  }

  /* Add it to the write_completed_queue where it will have its
   * callback called in the near future.
   */
  uv__queue_insert_tail(&stream->write_completed_queue, &req->queue);
  uv__io_feed(stream->loop, &stream->io_watcher);
}


static int uv__handle_fd(uv_handle_t* handle) {
  switch (handle->type) {
    case UV_NAMED_PIPE:
    case UV_TCP:
      return ((uv_stream_t*) handle)->io_watcher.fd;

    case UV_UDP:
      return ((uv_udp_t*) handle)->io_watcher.fd;

    default:
      return -1;
  }
}

static int uv__try_write(uv_stream_t* stream,
                         const uv_buf_t bufs[],
                         unsigned int nbufs,
                         uv_stream_t* send_handle) {
  struct iovec* iov;
  int iovmax;
  int iovcnt;
  ssize_t n;

  /*
   * Cast to iovec. We had to have our own uv_buf_t instead of iovec
   * because Windows's WSABUF is not an iovec.
   */
  iov = (struct iovec*) bufs;
  iovcnt = nbufs;

  iovmax = uv__getiovmax();

  /* Limit iov count to avoid EINVALs from writev() */
  if (iovcnt > iovmax)
    iovcnt = iovmax;

  /*
   * Now do the actual writev. Note that we've been updating the pointers
   * inside the iov each time we write. So there is no need to offset it.
   */
  if (send_handle != NULL) {
    int fd_to_send;
    struct msghdr msg;
    union uv__cmsg cmsg;

    if (uv__is_closing(send_handle))
      return UV_EBADF;

    fd_to_send = uv__handle_fd((uv_handle_t*) send_handle);

    memset(&cmsg, 0, sizeof(cmsg));

    assert(fd_to_send >= 0);

    msg.msg_name = NULL;
    msg.msg_namelen = 0;
    msg.msg_iov = iov;
    msg.msg_iovlen = iovcnt;
    msg.msg_flags = 0;

    msg.msg_control = &cmsg.hdr;
    msg.msg_controllen = CMSG_SPACE(sizeof(fd_to_send));

    cmsg.hdr.cmsg_level = SOL_SOCKET;
    cmsg.hdr.cmsg_type = SCM_RIGHTS;
    cmsg.hdr.cmsg_len = CMSG_LEN(sizeof(fd_to_send));
    memcpy(CMSG_DATA(&cmsg.hdr), &fd_to_send, sizeof(fd_to_send));

    do
      n = sendmsg(uv__stream_fd(stream), &msg, 0);
    while (n == -1 && errno == EINTR);
  } else {
    do
      n = uv__writev(uv__stream_fd(stream), iov, iovcnt);
    while (n == -1 && errno == EINTR);
  }

  if (n >= 0)
    return n;

  if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ENOBUFS)
    return UV_EAGAIN;

#ifdef __APPLE__
  /* macOS versions 10.10 and 10.15 - and presumbaly 10.11 to 10.14, too -
   * have a bug where a race condition causes the kernel to return EPROTOTYPE
   * because the socket isn't fully constructed. It's probably the result of
   * the peer closing the connection and that is why libuv translates it to
   * ECONNRESET. Previously, libuv retried until the EPROTOTYPE error went
   * away but some VPN software causes the same behavior except the error is
   * permanent, not transient, turning the retry mechanism into an infinite
   * loop. See https://github.com/libuv/libuv/pull/482.
   */
  if (errno == EPROTOTYPE)
    return UV_ECONNRESET;
#endif  /* __APPLE__ */

  return UV__ERR(errno);
}

static void uv__write(uv_stream_t* stream) {
  struct uv__queue* q;
  uv_write_t* req;
  ssize_t n;
  int count;

  assert(uv__stream_fd(stream) >= 0);

  /* Prevent loop starvation when the consumer of this stream read as fast as
   * (or faster than) we can write it. This `count` mechanism does not need to
   * change even if we switch to edge-triggered I/O.
   */
  count = 32;

  for (;;) {
    if (uv__queue_empty(&stream->write_queue))
      return;

    q = uv__queue_head(&stream->write_queue);
    req = uv__queue_data(q, uv_write_t, queue);
    assert(req->handle == stream);

    n = uv__try_write(stream,
                      &(req->bufs[req->write_index]),
                      req->nbufs - req->write_index,
                      req->send_handle);

    /* Ensure the handle isn't sent again in case this is a partial write. */
    if (n >= 0) {
      req->send_handle = NULL;
      if (uv__write_req_update(stream, req, n)) {
        uv__write_req_finish(req);
        if (count-- > 0)
          continue; /* Start trying to write the next request. */

        return;
      }
    } else if (n != UV_EAGAIN)
      goto error;

    /* If this is a blocking stream, try again. */
    if (stream->flags & UV_HANDLE_BLOCKING_WRITES)
      continue;

    /* We're not done. */
    uv__io_start(stream->loop, &stream->io_watcher, POLLOUT);

    /* Notify select() thread about state change */
    uv__stream_osx_interrupt_select(stream);

    return;
  }

error:
  req->error = n;
  uv__write_req_finish(req);
  uv__io_stop(stream->loop, &stream->io_watcher, POLLOUT);
  uv__stream_osx_interrupt_select(stream);
}


static void uv__write_callbacks(uv_stream_t* stream) {
  uv_write_t* req;
  struct uv__queue* q;
  struct uv__queue pq;

  if (uv__queue_empty(&stream->write_completed_queue))
    return;

  uv__queue_move(&stream->write_completed_queue, &pq);

  while (!uv__queue_empty(&pq)) {
    /* Pop a req off write_completed_queue. */
    q = uv__queue_head(&pq);
    req = uv__queue_data(q, uv_write_t, queue);
    uv__queue_remove(q);
    uv__req_unregister(stream->loop);

    if (req->bufs != NULL) {
      stream->write_queue_size -= uv__write_req_size(req);
      if (req->bufs != req->bufsml)
        uv__free(req->bufs);
      req->bufs = NULL;
    }

    /* NOTE: call callback AFTER freeing the request data. */
    if (req->cb)
      req->cb(req, req->error);
  }
}


static void uv__stream_eof(uv_stream_t* stream, const uv_buf_t* buf) {
  stream->flags |= UV_HANDLE_READ_EOF;
  stream->flags &= ~UV_HANDLE_READING;
  uv__io_stop(stream->loop, &stream->io_watcher, POLLIN);
  uv__handle_stop(stream);
  uv__stream_osx_interrupt_select(stream);
  stream->read_cb(stream, UV_EOF, buf);
}


static int uv__stream_queue_fd(uv_stream_t* stream, int fd) {
  uv__stream_queued_fds_t* queued_fds;
  unsigned int queue_size;

  queued_fds = stream->queued_fds;
  if (queued_fds == NULL) {
    queue_size = 8;
    queued_fds = uv__malloc((queue_size - 1) * sizeof(*queued_fds->fds) +
                            sizeof(*queued_fds));
    if (queued_fds == NULL)
      return UV_ENOMEM;
    queued_fds->size = queue_size;
    queued_fds->offset = 0;
    stream->queued_fds = queued_fds;

    /* Grow */
  } else if (queued_fds->size == queued_fds->offset) {
    queue_size = queued_fds->size + 8;
    queued_fds = uv__realloc(queued_fds,
                             (queue_size - 1) * sizeof(*queued_fds->fds) +
                              sizeof(*queued_fds));

    /*
     * Allocation failure, report back.
     * NOTE: if it is fatal - sockets will be closed in uv__stream_close
     */
    if (queued_fds == NULL)
      return UV_ENOMEM;
    queued_fds->size = queue_size;
    stream->queued_fds = queued_fds;
  }

  /* Put fd in a queue */
  queued_fds->fds[queued_fds->offset++] = fd;

  return 0;
}


static int uv__stream_recv_cmsg(uv_stream_t* stream, struct msghdr* msg) {
  struct cmsghdr* cmsg;
  char* p;
  char* pe;
  int fd;
  int err;
  size_t count;

  err = 0;
  for (cmsg = CMSG_FIRSTHDR(msg); cmsg != NULL; cmsg = CMSG_NXTHDR(msg, cmsg)) {
    if (cmsg->cmsg_type != SCM_RIGHTS) {
      fprintf(stderr, "ignoring non-SCM_RIGHTS ancillary data: %d\n",
          cmsg->cmsg_type);
      continue;
    }

    assert(cmsg->cmsg_len >= CMSG_LEN(0));
    count = cmsg->cmsg_len - CMSG_LEN(0);
    assert(count % sizeof(fd) == 0);
    count /= sizeof(fd);

    p = (void*) CMSG_DATA(cmsg);
    pe = p + count * sizeof(fd);

    while (p < pe) {
      memcpy(&fd, p, sizeof(fd));
      p += sizeof(fd);

      if (err == 0) {
        if (stream->accepted_fd == -1)
          stream->accepted_fd = fd;
        else
          err = uv__stream_queue_fd(stream, fd);
      }

      if (err != 0)
        uv__close(fd);
    }
  }

  return err;
}


static void uv__read(uv_stream_t* stream) {
  uv_buf_t buf;
  ssize_t nread;
  struct msghdr msg;
  union uv__cmsg cmsg;
  int count;
  int err;
  int is_ipc;

  stream->flags &= ~UV_HANDLE_READ_PARTIAL;

  /* Prevent loop starvation when the data comes in as fast as (or faster than)
   * we can read it. XXX Need to rearm fd if we switch to edge-triggered I/O.
   */
  count = 32;

  is_ipc = stream->type == UV_NAMED_PIPE && ((uv_pipe_t*) stream)->ipc;

  /* XXX: Maybe instead of having UV_HANDLE_READING we just test if
   * tcp->read_cb is NULL or not?
   */
  while (stream->read_cb
      && (stream->flags & UV_HANDLE_READING)
      && (count-- > 0)) {
    assert(stream->alloc_cb != NULL);

    buf = uv_buf_init(NULL, 0);
    stream->alloc_cb((uv_handle_t*)stream, 64 * 1024, &buf);
    if (buf.base == NULL || buf.len == 0) {
      /* User indicates it can't or won't handle the read. */
      stream->read_cb(stream, UV_ENOBUFS, &buf);
      return;
    }

    assert(buf.base != NULL);
    assert(uv__stream_fd(stream) >= 0);

    if (!is_ipc) {
      do {
        nread = read(uv__stream_fd(stream), buf.base, buf.len);
      }
      while (nread < 0 && errno == EINTR);
    } else {
      /* ipc uses recvmsg */
      msg.msg_flags = 0;
      msg.msg_iov = (struct iovec*) &buf;
      msg.msg_iovlen = 1;
      msg.msg_name = NULL;
      msg.msg_namelen = 0;
      /* Set up to receive a descriptor even if one isn't in the message */
      msg.msg_controllen = sizeof(cmsg);
      msg.msg_control = &cmsg.hdr;

      do {
        nread = uv__recvmsg(uv__stream_fd(stream), &msg, 0);
      }
      while (nread < 0 && errno == EINTR);
    }

    if (nread < 0) {
      /* Error */
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        /* Wait for the next one. */
        if (stream->flags & UV_HANDLE_READING) {
          uv__io_start(stream->loop, &stream->io_watcher, POLLIN);
          uv__stream_osx_interrupt_select(stream);
        }
        stream->read_cb(stream, 0, &buf);
#if defined(__CYGWIN__) || defined(__MSYS__)
      } else if (errno == ECONNRESET && stream->type == UV_NAMED_PIPE) {
        uv__stream_eof(stream, &buf);
        return;
#endif
      } else {
        /* Error. User should call uv_close(). */
        stream->flags &= ~(UV_HANDLE_READABLE | UV_HANDLE_WRITABLE);
        stream->read_cb(stream, UV__ERR(errno), &buf);
        if (stream->flags & UV_HANDLE_READING) {
          stream->flags &= ~UV_HANDLE_READING;
          uv__io_stop(stream->loop, &stream->io_watcher, POLLIN);
          uv__handle_stop(stream);
          uv__stream_osx_interrupt_select(stream);
        }
      }
      return;
    } else if (nread == 0) {
      uv__stream_eof(stream, &buf);
      return;
    } else {
      /* Successful read */
      ssize_t buflen = buf.len;

      if (is_ipc) {
        err = uv__stream_recv_cmsg(stream, &msg);
        if (err != 0) {
          stream->read_cb(stream, err, &buf);
          return;
        }
      }

#if defined(__MVS__)
      if (is_ipc && msg.msg_controllen > 0) {
        uv_buf_t blankbuf;
        int nread;
        struct iovec *old;

        blankbuf.base = 0;
        blankbuf.len = 0;
        old = msg.msg_iov;
        msg.msg_iov = (struct iovec*) &blankbuf;
        nread = 0;
        do {
          nread = uv__recvmsg(uv__stream_fd(stream), &msg, 0);
          err = uv__stream_recv_cmsg(stream, &msg);
          if (err != 0) {
            stream->read_cb(stream, err, &buf);
            msg.msg_iov = old;
            return;
          }
        } while (nread == 0 && msg.msg_controllen > 0);
        msg.msg_iov = old;
      }
#endif
      stream->read_cb(stream, nread, &buf);

      /* Return if we didn't fill the buffer, there is no more data to read. */
      if (nread < buflen) {
        stream->flags |= UV_HANDLE_READ_PARTIAL;
        return;
      }
    }
  }
}


int uv_shutdown(uv_shutdown_t* req, uv_stream_t* stream, uv_shutdown_cb cb) {
  assert(stream->type == UV_TCP ||
         stream->type == UV_TTY ||
         stream->type == UV_NAMED_PIPE);

  if (!(stream->flags & UV_HANDLE_WRITABLE) ||
      stream->flags & UV_HANDLE_SHUT ||
      uv__is_stream_shutting(stream) ||
      uv__is_closing(stream)) {
    return UV_ENOTCONN;
  }

  assert(uv__stream_fd(stream) >= 0);

  /* Initialize request. The `shutdown(2)` call will always be deferred until
   * `uv__drain`, just before the callback is run. */
  uv__req_init(stream->loop, req, UV_SHUTDOWN);
  req->handle = stream;
  req->cb = cb;
  stream->shutdown_req = req;
  stream->flags &= ~UV_HANDLE_WRITABLE;

  if (uv__queue_empty(&stream->write_queue))
    uv__io_feed(stream->loop, &stream->io_watcher);

  return 0;
}


static void uv__stream_io(uv_loop_t* loop, uv__io_t* w, unsigned int events) {
  uv_stream_t* stream;

  stream = container_of(w, uv_stream_t, io_watcher);

  assert(stream->type == UV_TCP ||
         stream->type == UV_NAMED_PIPE ||
         stream->type == UV_TTY);
  assert(!(stream->flags & UV_HANDLE_CLOSING));

  if (stream->connect_req) {
    uv__stream_connect(stream);
    return;
  }

  assert(uv__stream_fd(stream) >= 0);

  /* Ignore POLLHUP here. Even if it's set, there may still be data to read. */
  if (events & (POLLIN | POLLERR | POLLHUP))
    uv__read(stream);

  if (uv__stream_fd(stream) == -1)
    return;  /* read_cb closed stream. */

  /* Short-circuit iff POLLHUP is set, the user is still interested in read
   * events and uv__read() reported a partial read but not EOF. If the EOF
   * flag is set, uv__read() called read_cb with err=UV_EOF and we don't
   * have to do anything. If the partial read flag is not set, we can't
   * report the EOF yet because there is still data to read.
   */
  if ((events & POLLHUP) &&
      (stream->flags & UV_HANDLE_READING) &&
      (stream->flags & UV_HANDLE_READ_PARTIAL) &&
      !(stream->flags & UV_HANDLE_READ_EOF)) {
    uv_buf_t buf = { NULL, 0 };
    uv__stream_eof(stream, &buf);
  }

  if (uv__stream_fd(stream) == -1)
    return;  /* read_cb closed stream. */

  if (events & (POLLOUT | POLLERR | POLLHUP)) {
    uv__write(stream);
    uv__write_callbacks(stream);

    /* Write queue drained. */
    if (uv__queue_empty(&stream->write_queue))
      uv__drain(stream);
  }
}


/**
 * We get called here from directly following a call to connect(2).
 * In order to determine if we've errored out or succeeded must call
 * getsockopt.
 */
static void uv__stream_connect(uv_stream_t* stream) {
  int error;
  uv_connect_t* req = stream->connect_req;
  socklen_t errorsize = sizeof(int);

  assert(stream->type == UV_TCP || stream->type == UV_NAMED_PIPE);
  assert(req);

  if (stream->delayed_error) {
    /* To smooth over the differences between unixes errors that
     * were reported synchronously on the first connect can be delayed
     * until the next tick--which is now.
     */
    error = stream->delayed_error;
    stream->delayed_error = 0;
  } else {
    /* Normal situation: we need to get the socket error from the kernel. */
    assert(uv__stream_fd(stream) >= 0);
    getsockopt(uv__stream_fd(stream),
               SOL_SOCKET,
               SO_ERROR,
               &error,
               &errorsize);
    error = UV__ERR(error);
  }

  if (error == UV__ERR(EINPROGRESS))
    return;

  stream->connect_req = NULL;
  uv__req_unregister(stream->loop);

  if (error < 0 || uv__queue_empty(&stream->write_queue)) {
    uv__io_stop(stream->loop, &stream->io_watcher, POLLOUT);
  }

  if (req->cb)
    req->cb(req, error);

  if (uv__stream_fd(stream) == -1)
    return;

  if (error < 0) {
    uv__stream_flush_write_queue(stream, UV_ECANCELED);
    uv__write_callbacks(stream);
  }
}


static int uv__check_before_write(uv_stream_t* stream,
                                  unsigned int nbufs,
                                  uv_stream_t* send_handle) {
  assert(nbufs > 0);
  assert((stream->type == UV_TCP ||
          stream->type == UV_NAMED_PIPE ||
          stream->type == UV_TTY) &&
         "uv_write (unix) does not yet support other types of streams");

  if (uv__stream_fd(stream) < 0)
    return UV_EBADF;

  if (!(stream->flags & UV_HANDLE_WRITABLE))
    return UV_EPIPE;

  if (send_handle != NULL) {
    if (stream->type != UV_NAMED_PIPE || !((uv_pipe_t*)stream)->ipc)
      return UV_EINVAL;

    /* XXX We abuse uv_write2() to send over UDP handles to child processes.
     * Don't call uv__stream_fd() on those handles, it's a macro that on OS X
     * evaluates to a function that operates on a uv_stream_t with a couple of
     * OS X specific fields. On other Unices it does (handle)->io_watcher.fd,
     * which works but only by accident.
     */
    if (uv__handle_fd((uv_handle_t*) send_handle) < 0)
      return UV_EBADF;

#if defined(__CYGWIN__) || defined(__MSYS__)
    /* Cygwin recvmsg always sets msg_controllen to zero, so we cannot send it.
       See https://github.com/mirror/newlib-cygwin/blob/86fc4bf0/winsup/cygwin/fhandler_socket.cc#L1736-L1743 */
    return UV_ENOSYS;
#endif
  }

  return 0;
}

int uv_write2(uv_write_t* req,
              uv_stream_t* stream,
              const uv_buf_t bufs[],
              unsigned int nbufs,
              uv_stream_t* send_handle,
              uv_write_cb cb) {
  int empty_queue;
  int err;

  err = uv__check_before_write(stream, nbufs, send_handle);
  if (err < 0)
    return err;

  /* It's legal for write_queue_size > 0 even when the write_queue is empty;
   * it means there are error-state requests in the write_completed_queue that
   * will touch up write_queue_size later, see also uv__write_req_finish().
   * We could check that write_queue is empty instead but that implies making
   * a write() syscall when we know that the handle is in error mode.
   */
  empty_queue = (stream->write_queue_size == 0);

  /* Initialize the req */
  uv__req_init(stream->loop, req, UV_WRITE);
  req->cb = cb;
  req->handle = stream;
  req->error = 0;
  req->send_handle = send_handle;
  uv__queue_init(&req->queue);

  req->bufs = req->bufsml;
  if (nbufs > ARRAY_SIZE(req->bufsml))
    req->bufs = uv__malloc(nbufs * sizeof(bufs[0]));

  if (req->bufs == NULL)
    return UV_ENOMEM;

  memcpy(req->bufs, bufs, nbufs * sizeof(bufs[0]));
  req->nbufs = nbufs;
  req->write_index = 0;
  stream->write_queue_size += uv__count_bufs(bufs, nbufs);

  /* Append the request to write_queue. */
  uv__queue_insert_tail(&stream->write_queue, &req->queue);

  /* If the queue was empty when this function began, we should attempt to
   * do the write immediately. Otherwise start the write_watcher and wait
   * for the fd to become writable.
   */
  if (stream->connect_req) {
    /* Still connecting, do nothing. */
  }
  else if (empty_queue) {
    uv__write(stream);
  }
  else {
    /*
     * blocking streams should never have anything in the queue.
     * if this assert fires then somehow the blocking stream isn't being
     * sufficiently flushed in uv__write.
     */
    assert(!(stream->flags & UV_HANDLE_BLOCKING_WRITES));
    uv__io_start(stream->loop, &stream->io_watcher, POLLOUT);
    uv__stream_osx_interrupt_select(stream);
  }

  return 0;
}


/* The buffers to be written must remain valid until the callback is called.
 * This is not required for the uv_buf_t array.
 */
int uv_write(uv_write_t* req,
             uv_stream_t* handle,
             const uv_buf_t bufs[],
             unsigned int nbufs,
             uv_write_cb cb) {
  return uv_write2(req, handle, bufs, nbufs, NULL, cb);
}


int uv_try_write(uv_stream_t* stream,
                 const uv_buf_t bufs[],
                 unsigned int nbufs) {
  return uv_try_write2(stream, bufs, nbufs, NULL);
}


int uv_try_write2(uv_stream_t* stream,
                  const uv_buf_t bufs[],
                  unsigned int nbufs,
                  uv_stream_t* send_handle) {
  int err;

  /* Connecting or already writing some data */
  if (stream->connect_req != NULL || stream->write_queue_size != 0)
    return UV_EAGAIN;

  err = uv__check_before_write(stream, nbufs, NULL);
  if (err < 0)
    return err;

  return uv__try_write(stream, bufs, nbufs, send_handle);
}


int uv__read_start(uv_stream_t* stream,
                   uv_alloc_cb alloc_cb,
                   uv_read_cb read_cb) {
  assert(stream->type == UV_TCP || stream->type == UV_NAMED_PIPE ||
      stream->type == UV_TTY);

  /* The UV_HANDLE_READING flag is irrelevant of the state of the stream - it
   * just expresses the desired state of the user. */
  stream->flags |= UV_HANDLE_READING;
  stream->flags &= ~UV_HANDLE_READ_EOF;

  /* TODO: try to do the read inline? */
  assert(uv__stream_fd(stream) >= 0);
  assert(alloc_cb);

  stream->read_cb = read_cb;
  stream->alloc_cb = alloc_cb;

  uv__io_start(stream->loop, &stream->io_watcher, POLLIN);
  uv__handle_start(stream);
  uv__stream_osx_interrupt_select(stream);

  return 0;
}


int uv_read_stop(uv_stream_t* stream) {
  if (!(stream->flags & UV_HANDLE_READING))
    return 0;

  stream->flags &= ~UV_HANDLE_READING;
  uv__io_stop(stream->loop, &stream->io_watcher, POLLIN);
  uv__handle_stop(stream);
  uv__stream_osx_interrupt_select(stream);

  stream->read_cb = NULL;
  stream->alloc_cb = NULL;
  return 0;
}


int uv_is_readable(const uv_stream_t* stream) {
  return !!(stream->flags & UV_HANDLE_READABLE);
}


int uv_is_writable(const uv_stream_t* stream) {
  return !!(stream->flags & UV_HANDLE_WRITABLE);
}


#if defined(__APPLE__)
int uv___stream_fd(const uv_stream_t* handle) {
  const uv__stream_select_t* s;

  assert(handle->type == UV_TCP ||
         handle->type == UV_TTY ||
         handle->type == UV_NAMED_PIPE);

  s = handle->select;
  if (s != NULL)
    return s->fd;

  return handle->io_watcher.fd;
}
#endif /* defined(__APPLE__) */


void uv__stream_close(uv_stream_t* handle) {
  unsigned int i;
  uv__stream_queued_fds_t* queued_fds;

#if defined(__APPLE__)
  /* Terminate select loop first */
  if (handle->select != NULL) {
    uv__stream_select_t* s;

    s = handle->select;

    uv_sem_post(&s->close_sem);
    uv_sem_post(&s->async_sem);
    uv__stream_osx_interrupt_select(handle);
    uv_thread_join(&s->thread);
    uv_sem_destroy(&s->close_sem);
    uv_sem_destroy(&s->async_sem);
    uv__close(s->fake_fd);
    uv__close(s->int_fd);
    uv_close((uv_handle_t*) &s->async, uv__stream_osx_cb_close);

    handle->select = NULL;
  }
#endif /* defined(__APPLE__) */

  uv__io_close(handle->loop, &handle->io_watcher);
  uv_read_stop(handle);
  uv__handle_stop(handle);
  handle->flags &= ~(UV_HANDLE_READABLE | UV_HANDLE_WRITABLE);

  if (handle->io_watcher.fd != -1) {
    /* Don't close stdio file descriptors.  Nothing good comes from it. */
    if (handle->io_watcher.fd > STDERR_FILENO)
      uv__close(handle->io_watcher.fd);
    handle->io_watcher.fd = -1;
  }

  if (handle->accepted_fd != -1) {
    uv__close(handle->accepted_fd);
    handle->accepted_fd = -1;
  }

  /* Close all queued fds */
  if (handle->queued_fds != NULL) {
    queued_fds = handle->queued_fds;
    for (i = 0; i < queued_fds->offset; i++)
      uv__close(queued_fds->fds[i]);
    uv__free(handle->queued_fds);
    handle->queued_fds = NULL;
  }

  assert(!uv__io_active(&handle->io_watcher, POLLIN | POLLOUT));
}


int uv_stream_set_blocking(uv_stream_t* handle, int blocking) {
  /* Don't need to check the file descriptor, uv__nonblock()
   * will fail with EBADF if it's not valid.
   */
  return uv__nonblock(uv__stream_fd(handle), !blocking);
}
