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
#include <errno.h>
#include <string.h>
#include <sys/un.h>
#include <unistd.h>
#include <stdlib.h>


/* Does the file path contain embedded nul bytes? */
static int includes_nul(const char *s, size_t n) {
  if (n == 0)
    return 0;
#ifdef __linux__
  /* Accept abstract socket namespace path ("\0/virtual/path"). */
  s++;
  n--;
#endif
  return NULL != memchr(s, '\0', n);
}


int uv_pipe_init(uv_loop_t* loop, uv_pipe_t* handle, int ipc) {
  uv__stream_init(loop, (uv_stream_t*)handle, UV_NAMED_PIPE);
  handle->shutdown_req = NULL;
  handle->connect_req = NULL;
  handle->pipe_fname = NULL;
  handle->ipc = ipc;
  return 0;
}


int uv_pipe_bind(uv_pipe_t* handle, const char* name) {
  return uv_pipe_bind2(handle, name, strlen(name), 0);
}


int uv_pipe_bind2(uv_pipe_t* handle,
                  const char* name,
                  size_t namelen,
                  unsigned int flags) {
  struct sockaddr_un saddr;
  char* pipe_fname;
  int sockfd;
  int err;
  socklen_t addrlen;

  pipe_fname = NULL;

  if (flags & ~UV_PIPE_NO_TRUNCATE)
    return UV_EINVAL;

  if (name == NULL)
    return UV_EINVAL;

  /* namelen==0 on Linux means autobind the listen socket in the abstract
   * socket namespace, see `man 7 unix` for details.
   */
#if !defined(__linux__)
  if (namelen == 0)
    return UV_EINVAL;
#endif

  if (includes_nul(name, namelen))
    return UV_EINVAL;

  if (flags & UV_PIPE_NO_TRUNCATE)
    if (namelen > sizeof(saddr.sun_path))
      return UV_EINVAL;

  /* Truncate long paths. Documented behavior. */
  if (namelen > sizeof(saddr.sun_path))
    namelen = sizeof(saddr.sun_path);

  /* Already bound? */
  if (uv__stream_fd(handle) >= 0)
    return UV_EINVAL;

  if (uv__is_closing(handle))
    return UV_EINVAL;

  /* Make a copy of the file path unless it is an abstract socket.
   * We unlink the file later but abstract sockets disappear
   * automatically since they're not real file system entities.
   */
  if (*name == '\0') {
    addrlen = offsetof(struct sockaddr_un, sun_path) + namelen;
  } else {
    pipe_fname = uv__malloc(namelen + 1);
    if (pipe_fname == NULL)
      return UV_ENOMEM;
    memcpy(pipe_fname, name, namelen);
    pipe_fname[namelen] = '\0';
    addrlen = sizeof saddr;
  }

  err = uv__socket(AF_UNIX, SOCK_STREAM, 0);
  if (err < 0)
    goto err_socket;
  sockfd = err;

  memset(&saddr, 0, sizeof saddr);
  memcpy(&saddr.sun_path, name, namelen);
  saddr.sun_family = AF_UNIX;

  if (bind(sockfd, (struct sockaddr*)&saddr, addrlen)) {
    err = UV__ERR(errno);
    /* Convert ENOENT to EACCES for compatibility with Windows. */
    if (err == UV_ENOENT)
      err = UV_EACCES;

    uv__close(sockfd);
    goto err_socket;
  }

  /* Success. */
  handle->flags |= UV_HANDLE_BOUND;
  handle->pipe_fname = pipe_fname; /* NULL or a copy of |name| */
  handle->io_watcher.fd = sockfd;
  return 0;

err_socket:
  uv__free(pipe_fname);
  return err;
}


int uv__pipe_listen(uv_pipe_t* handle, int backlog, uv_connection_cb cb) {
  if (uv__stream_fd(handle) == -1)
    return UV_EINVAL;

  if (handle->ipc)
    return UV_EINVAL;

#if defined(__MVS__) || defined(__PASE__)
  /* On zOS, backlog=0 has undefined behaviour */
  /* On IBMi PASE, backlog=0 leads to "Connection refused" error */
  if (backlog == 0)
    backlog = 1;
  else if (backlog < 0)
    backlog = SOMAXCONN;
#endif

  if (listen(uv__stream_fd(handle), backlog))
    return UV__ERR(errno);

  handle->connection_cb = cb;
  handle->io_watcher.cb = uv__server_io;
  uv__io_start(handle->loop, &handle->io_watcher, POLLIN);
  return 0;
}


void uv__pipe_close(uv_pipe_t* handle) {
  if (handle->pipe_fname) {
    /*
     * Unlink the file system entity before closing the file descriptor.
     * Doing it the other way around introduces a race where our process
     * unlinks a socket with the same name that's just been created by
     * another thread or process.
     */
    unlink(handle->pipe_fname);
    uv__free((void*)handle->pipe_fname);
    handle->pipe_fname = NULL;
  }

  uv__stream_close((uv_stream_t*)handle);
}


int uv_pipe_open(uv_pipe_t* handle, uv_file fd) {
  int flags;
  int mode;
  int err;
  flags = 0;

  if (uv__fd_exists(handle->loop, fd))
    return UV_EEXIST;

  do
    mode = fcntl(fd, F_GETFL);
  while (mode == -1 && errno == EINTR);

  if (mode == -1)
    return UV__ERR(errno); /* according to docs, must be EBADF */

  err = uv__nonblock(fd, 1);
  if (err)
    return err;

#if defined(__APPLE__)
  err = uv__stream_try_select((uv_stream_t*) handle, &fd);
  if (err)
    return err;
#endif /* defined(__APPLE__) */

  mode &= O_ACCMODE;
  if (mode != O_WRONLY)
    flags |= UV_HANDLE_READABLE;
  if (mode != O_RDONLY)
    flags |= UV_HANDLE_WRITABLE;

  return uv__stream_open((uv_stream_t*)handle, fd, flags);
}


void uv_pipe_connect(uv_connect_t* req,
                    uv_pipe_t* handle,
                    const char* name,
                    uv_connect_cb cb) {
  int err;

  err = uv_pipe_connect2(req, handle, name, strlen(name), 0, cb);

  if (err) {
    handle->delayed_error = err;
    handle->connect_req = req;

    uv__req_init(handle->loop, req, UV_CONNECT);
    req->handle = (uv_stream_t*) handle;
    req->cb = cb;
    uv__queue_init(&req->queue);

    /* Force callback to run on next tick in case of error. */
    uv__io_feed(handle->loop, &handle->io_watcher);
  }
}


int uv_pipe_connect2(uv_connect_t* req,
                     uv_pipe_t* handle,
                     const char* name,
                     size_t namelen,
                     unsigned int flags,
                     uv_connect_cb cb) {
  struct sockaddr_un saddr;
  int new_sock;
  int err;
  int r;
  socklen_t addrlen;

  if (flags & ~UV_PIPE_NO_TRUNCATE)
    return UV_EINVAL;

  if (name == NULL)
    return UV_EINVAL;

  if (namelen == 0)
    return UV_EINVAL;

  if (includes_nul(name, namelen))
    return UV_EINVAL;

  if (flags & UV_PIPE_NO_TRUNCATE)
    if (namelen > sizeof(saddr.sun_path))
      return UV_EINVAL;

  /* Truncate long paths. Documented behavior. */
  if (namelen > sizeof(saddr.sun_path))
    namelen = sizeof(saddr.sun_path);

  new_sock = (uv__stream_fd(handle) == -1);

  if (new_sock) {
    err = uv__socket(AF_UNIX, SOCK_STREAM, 0);
    if (err < 0)
      goto out;
    handle->io_watcher.fd = err;
  }

  memset(&saddr, 0, sizeof saddr);
  memcpy(&saddr.sun_path, name, namelen);
  saddr.sun_family = AF_UNIX;

  if (*name == '\0')
    addrlen = offsetof(struct sockaddr_un, sun_path) + namelen;
  else
    addrlen = sizeof saddr;

  do {
    r = connect(uv__stream_fd(handle), (struct sockaddr*)&saddr, addrlen);
  }
  while (r == -1 && errno == EINTR);

  if (r == -1 && errno != EINPROGRESS) {
    err = UV__ERR(errno);
#if defined(__CYGWIN__) || defined(__MSYS__)
    /* EBADF is supposed to mean that the socket fd is bad, but
       Cygwin reports EBADF instead of ENOTSOCK when the file is
       not a socket.  We do not expect to see a bad fd here
       (e.g. due to new_sock), so translate the error.  */
    if (err == UV_EBADF)
      err = UV_ENOTSOCK;
#endif
    goto out;
  }

  err = 0;
  if (new_sock) {
    err = uv__stream_open((uv_stream_t*)handle,
                          uv__stream_fd(handle),
                          UV_HANDLE_READABLE | UV_HANDLE_WRITABLE);
  }

  if (err == 0)
    uv__io_start(handle->loop, &handle->io_watcher, POLLOUT);

out:
  handle->delayed_error = err;
  handle->connect_req = req;

  uv__req_init(handle->loop, req, UV_CONNECT);
  req->handle = (uv_stream_t*) handle;
  req->cb = cb;
  uv__queue_init(&req->queue);

  /* Force callback to run on next tick in case of error. */
  if (err)
    uv__io_feed(handle->loop, &handle->io_watcher);

  return 0;
}


static int uv__pipe_getsockpeername(const uv_pipe_t* handle,
                                    uv__peersockfunc func,
                                    char* buffer,
                                    size_t* size) {
#if defined(__linux__)
  static const int is_linux = 1;
#else
  static const int is_linux = 0;
#endif
  struct sockaddr_un sa;
  socklen_t addrlen;
  size_t slop;
  char* p;
  int err;

  if (buffer == NULL || size == NULL || *size == 0)
    return UV_EINVAL;

  addrlen = sizeof(sa);
  memset(&sa, 0, addrlen);
  err = uv__getsockpeername((const uv_handle_t*) handle,
                            func,
                            (struct sockaddr*) &sa,
                            (int*) &addrlen);
  if (err < 0) {
    *size = 0;
    return err;
  }

  slop = 1;
  if (is_linux && sa.sun_path[0] == '\0') {
    /* Linux abstract namespace. Not zero-terminated. */
    slop = 0;
    addrlen -= offsetof(struct sockaddr_un, sun_path);
  } else {
    p = memchr(sa.sun_path, '\0', sizeof(sa.sun_path));
    if (p == NULL)
      p = ARRAY_END(sa.sun_path);
    addrlen = p - sa.sun_path;
  }

  if ((size_t)addrlen + slop > *size) {
    *size = addrlen + slop;
    return UV_ENOBUFS;
  }

  memcpy(buffer, sa.sun_path, addrlen);
  *size = addrlen;

  /* only null-terminate if it's not an abstract socket */
  if (buffer[0] != '\0')
    buffer[addrlen] = '\0';

  return 0;
}


int uv_pipe_getsockname(const uv_pipe_t* handle, char* buffer, size_t* size) {
  return uv__pipe_getsockpeername(handle, getsockname, buffer, size);
}


int uv_pipe_getpeername(const uv_pipe_t* handle, char* buffer, size_t* size) {
  return uv__pipe_getsockpeername(handle, getpeername, buffer, size);
}


void uv_pipe_pending_instances(uv_pipe_t* handle, int count) {
}


int uv_pipe_pending_count(uv_pipe_t* handle) {
  uv__stream_queued_fds_t* queued_fds;

  if (!handle->ipc)
    return 0;

  if (handle->accepted_fd == -1)
    return 0;

  if (handle->queued_fds == NULL)
    return 1;

  queued_fds = handle->queued_fds;
  return queued_fds->offset + 1;
}


uv_handle_type uv_pipe_pending_type(uv_pipe_t* handle) {
  if (!handle->ipc)
    return UV_UNKNOWN_HANDLE;

  if (handle->accepted_fd == -1)
    return UV_UNKNOWN_HANDLE;
  else
    return uv_guess_handle(handle->accepted_fd);
}


int uv_pipe_chmod(uv_pipe_t* handle, int mode) {
  unsigned desired_mode;
  struct stat pipe_stat;
  char name_buffer[1 + UV__PATH_MAX];
  size_t name_len;
  int r;

  if (handle == NULL || uv__stream_fd(handle) == -1)
    return UV_EBADF;

  if (mode != UV_READABLE &&
      mode != UV_WRITABLE &&
      mode != (UV_WRITABLE | UV_READABLE))
    return UV_EINVAL;

  /* Unfortunately fchmod does not work on all platforms, we will use chmod. */
  name_len = sizeof(name_buffer);
  r = uv_pipe_getsockname(handle, name_buffer, &name_len);
  if (r != 0)
    return r;

  /* stat must be used as fstat has a bug on Darwin */
  if (uv__stat(name_buffer, &pipe_stat) == -1)
    return UV__ERR(errno);

  desired_mode = 0;
  if (mode & UV_READABLE)
    desired_mode |= S_IRUSR | S_IRGRP | S_IROTH;
  if (mode & UV_WRITABLE)
    desired_mode |= S_IWUSR | S_IWGRP | S_IWOTH;

  /* Exit early if pipe already has desired mode. */
  if ((pipe_stat.st_mode & desired_mode) == desired_mode)
    return 0;

  pipe_stat.st_mode |= desired_mode;

  r = chmod(name_buffer, pipe_stat.st_mode);

  return r != -1 ? 0 : UV__ERR(errno);
}


int uv_pipe(uv_os_fd_t fds[2], int read_flags, int write_flags) {
  uv_os_fd_t temp[2];
  int err;
#if defined(__linux__) || \
    defined(__FreeBSD__) || \
    defined(__OpenBSD__) || \
    defined(__DragonFly__) || \
    defined(__NetBSD__)
  int flags = O_CLOEXEC;

  if ((read_flags & UV_NONBLOCK_PIPE) && (write_flags & UV_NONBLOCK_PIPE))
    flags |= UV_FS_O_NONBLOCK;

  if (pipe2(temp, flags))
    return UV__ERR(errno);

  if (flags & UV_FS_O_NONBLOCK) {
    fds[0] = temp[0];
    fds[1] = temp[1];
    return 0;
  }
#else
  if (pipe(temp))
    return UV__ERR(errno);

  if ((err = uv__cloexec(temp[0], 1)))
    goto fail;

  if ((err = uv__cloexec(temp[1], 1)))
    goto fail;
#endif

  if (read_flags & UV_NONBLOCK_PIPE)
    if ((err = uv__nonblock(temp[0], 1)))
      goto fail;

  if (write_flags & UV_NONBLOCK_PIPE)
    if ((err = uv__nonblock(temp[1], 1)))
      goto fail;

  fds[0] = temp[0];
  fds[1] = temp[1];
  return 0;

fail:
  uv__close(temp[0]);
  uv__close(temp[1]);
  return err;
}


int uv__make_pipe(int fds[2], int flags) {
  return uv_pipe(fds,
                 flags & UV_NONBLOCK_PIPE,
                 flags & UV_NONBLOCK_PIPE);
}
