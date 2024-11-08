/* Copyright Joyent, Inc. and other Node contributors. All rights reserved.
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
#include "strtok.h"

#include <stddef.h> /* NULL */
#include <stdio.h> /* printf */
#include <stdlib.h>
#include <string.h> /* strerror */
#include <errno.h>
#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>  /* O_CLOEXEC */
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <limits.h> /* INT_MAX, PATH_MAX, IOV_MAX */
#include <sys/uio.h> /* writev */
#include <sys/resource.h> /* getrusage */
#include <pwd.h>
#include <grp.h>
#include <sys/utsname.h>
#include <sys/time.h>
#include <time.h> /* clock_gettime */

#ifdef __sun
# include <sys/filio.h>
# include <sys/wait.h>
#endif

#if defined(__APPLE__)
# include <sys/filio.h>
# include <sys/sysctl.h>
#endif /* defined(__APPLE__) */


#if defined(__APPLE__) && !TARGET_OS_IPHONE
# include <crt_externs.h>
# include <mach-o/dyld.h> /* _NSGetExecutablePath */
# define environ (*_NSGetEnviron())
#else /* defined(__APPLE__) && !TARGET_OS_IPHONE */
extern char** environ;
#endif /* !(defined(__APPLE__) && !TARGET_OS_IPHONE) */


#if defined(__DragonFly__)      || \
    defined(__FreeBSD__)        || \
    defined(__NetBSD__)         || \
    defined(__OpenBSD__)
# include <sys/sysctl.h>
# include <sys/filio.h>
# include <sys/wait.h>
# include <sys/param.h>
# if defined(__FreeBSD__)
#  include <sys/cpuset.h>
#  define uv__accept4 accept4
# endif
# if defined(__NetBSD__)
#  define uv__accept4(a, b, c, d) paccept((a), (b), (c), NULL, (d))
# endif
#endif

#if defined(__MVS__)
# include <sys/ioctl.h>
# include "zos-sys-info.h"
#endif

#if defined(__linux__)
# include <sched.h>
# include <sys/syscall.h>
# define gettid() syscall(SYS_gettid)
# define uv__accept4 accept4
#endif

#if defined(__FreeBSD__)
# include <sys/param.h>
# include <sys/cpuset.h>
#endif

#if defined(__NetBSD__)
# include <sched.h>
#endif

#if defined(__linux__) && defined(__SANITIZE_THREAD__) && defined(__clang__)
# include <sanitizer/linux_syscall_hooks.h>
#endif

static void uv__run_pending(uv_loop_t* loop);

/* Verify that uv_buf_t is ABI-compatible with struct iovec. */
STATIC_ASSERT(sizeof(uv_buf_t) == sizeof(struct iovec));
STATIC_ASSERT(sizeof(((uv_buf_t*) 0)->base) ==
              sizeof(((struct iovec*) 0)->iov_base));
STATIC_ASSERT(sizeof(((uv_buf_t*) 0)->len) ==
              sizeof(((struct iovec*) 0)->iov_len));
STATIC_ASSERT(offsetof(uv_buf_t, base) == offsetof(struct iovec, iov_base));
STATIC_ASSERT(offsetof(uv_buf_t, len) == offsetof(struct iovec, iov_len));


/* https://github.com/libuv/libuv/issues/1674 */
int uv_clock_gettime(uv_clock_id clock_id, uv_timespec64_t* ts) {
  struct timespec t;
  int r;

  if (ts == NULL)
    return UV_EFAULT;

  switch (clock_id) {
    default:
      return UV_EINVAL;
    case UV_CLOCK_MONOTONIC:
      r = clock_gettime(CLOCK_MONOTONIC, &t);
      break;
    case UV_CLOCK_REALTIME:
      r = clock_gettime(CLOCK_REALTIME, &t);
      break;
  }

  if (r)
    return UV__ERR(errno);

  ts->tv_sec = t.tv_sec;
  ts->tv_nsec = t.tv_nsec;

  return 0;
}


uint64_t uv_hrtime(void) {
  return uv__hrtime(UV_CLOCK_PRECISE);
}


void uv_close(uv_handle_t* handle, uv_close_cb close_cb) {
  assert(!uv__is_closing(handle));

  handle->flags |= UV_HANDLE_CLOSING;
  handle->close_cb = close_cb;

  switch (handle->type) {
  case UV_NAMED_PIPE:
    uv__pipe_close((uv_pipe_t*)handle);
    break;

  case UV_TTY:
    uv__tty_close((uv_tty_t*)handle);
    break;

  case UV_TCP:
    uv__tcp_close((uv_tcp_t*)handle);
    break;

  case UV_UDP:
    uv__udp_close((uv_udp_t*)handle);
    break;

  case UV_PREPARE:
    uv__prepare_close((uv_prepare_t*)handle);
    break;

  case UV_CHECK:
    uv__check_close((uv_check_t*)handle);
    break;

  case UV_IDLE:
    uv__idle_close((uv_idle_t*)handle);
    break;

  case UV_ASYNC:
    uv__async_close((uv_async_t*)handle);
    break;

  case UV_TIMER:
    uv__timer_close((uv_timer_t*)handle);
    break;

  case UV_PROCESS:
    uv__process_close((uv_process_t*)handle);
    break;

  case UV_FS_EVENT:
    uv__fs_event_close((uv_fs_event_t*)handle);
#if defined(__sun) || defined(__MVS__)
    /*
     * On Solaris, illumos, and z/OS we will not be able to dissociate the
     * watcher for an event which is pending delivery, so we cannot always call
     * uv__make_close_pending() straight away. The backend will call the
     * function once the event has cleared.
     */
    return;
#endif
    break;

  case UV_POLL:
    uv__poll_close((uv_poll_t*)handle);
    break;

  case UV_FS_POLL:
    uv__fs_poll_close((uv_fs_poll_t*)handle);
    /* Poll handles use file system requests, and one of them may still be
     * running. The poll code will call uv__make_close_pending() for us. */
    return;

  case UV_SIGNAL:
    uv__signal_close((uv_signal_t*) handle);
    break;

  default:
    assert(0);
  }

  uv__make_close_pending(handle);
}

int uv__socket_sockopt(uv_handle_t* handle, int optname, int* value) {
  int r;
  int fd;
  socklen_t len;

  if (handle == NULL || value == NULL)
    return UV_EINVAL;

  if (handle->type == UV_TCP || handle->type == UV_NAMED_PIPE)
    fd = uv__stream_fd((uv_stream_t*) handle);
  else if (handle->type == UV_UDP)
    fd = ((uv_udp_t *) handle)->io_watcher.fd;
  else
    return UV_ENOTSUP;

  len = sizeof(*value);

  if (*value == 0)
    r = getsockopt(fd, SOL_SOCKET, optname, value, &len);
  else
    r = setsockopt(fd, SOL_SOCKET, optname, (const void*) value, len);

  if (r < 0)
    return UV__ERR(errno);

  return 0;
}

void uv__make_close_pending(uv_handle_t* handle) {
  assert(handle->flags & UV_HANDLE_CLOSING);
  assert(!(handle->flags & UV_HANDLE_CLOSED));
  handle->next_closing = handle->loop->closing_handles;
  handle->loop->closing_handles = handle;
}

int uv__getiovmax(void) {
#if defined(IOV_MAX)
  return IOV_MAX;
#elif defined(_SC_IOV_MAX)
  static _Atomic int iovmax_cached = -1;
  int iovmax;

  iovmax = atomic_load_explicit(&iovmax_cached, memory_order_relaxed);
  if (iovmax != -1)
    return iovmax;

  /* On some embedded devices (arm-linux-uclibc based ip camera),
   * sysconf(_SC_IOV_MAX) can not get the correct value. The return
   * value is -1 and the errno is EINPROGRESS. Degrade the value to 1.
   */
  iovmax = sysconf(_SC_IOV_MAX);
  if (iovmax == -1)
    iovmax = 1;

  atomic_store_explicit(&iovmax_cached, iovmax, memory_order_relaxed);

  return iovmax;
#else
  return 1024;
#endif
}


static void uv__finish_close(uv_handle_t* handle) {
  uv_signal_t* sh;

  /* Note: while the handle is in the UV_HANDLE_CLOSING state now, it's still
   * possible for it to be active in the sense that uv__is_active() returns
   * true.
   *
   * A good example is when the user calls uv_shutdown(), immediately followed
   * by uv_close(). The handle is considered active at this point because the
   * completion of the shutdown req is still pending.
   */
  assert(handle->flags & UV_HANDLE_CLOSING);
  assert(!(handle->flags & UV_HANDLE_CLOSED));
  handle->flags |= UV_HANDLE_CLOSED;

  switch (handle->type) {
    case UV_PREPARE:
    case UV_CHECK:
    case UV_IDLE:
    case UV_ASYNC:
    case UV_TIMER:
    case UV_PROCESS:
    case UV_FS_EVENT:
    case UV_FS_POLL:
    case UV_POLL:
      break;

    case UV_SIGNAL:
      /* If there are any caught signals "trapped" in the signal pipe,
       * we can't call the close callback yet. Reinserting the handle
       * into the closing queue makes the event loop spin but that's
       * okay because we only need to deliver the pending events.
       */
      sh = (uv_signal_t*) handle;
      if (sh->caught_signals > sh->dispatched_signals) {
        handle->flags ^= UV_HANDLE_CLOSED;
        uv__make_close_pending(handle);  /* Back into the queue. */
        return;
      }
      break;

    case UV_NAMED_PIPE:
    case UV_TCP:
    case UV_TTY:
      uv__stream_destroy((uv_stream_t*)handle);
      break;

    case UV_UDP:
      uv__udp_finish_close((uv_udp_t*)handle);
      break;

    default:
      assert(0);
      break;
  }

  uv__handle_unref(handle);
  uv__queue_remove(&handle->handle_queue);

  if (handle->close_cb) {
    handle->close_cb(handle);
  }
}


static void uv__run_closing_handles(uv_loop_t* loop) {
  uv_handle_t* p;
  uv_handle_t* q;

  p = loop->closing_handles;
  loop->closing_handles = NULL;

  while (p) {
    q = p->next_closing;
    uv__finish_close(p);
    p = q;
  }
}


int uv_is_closing(const uv_handle_t* handle) {
  return uv__is_closing(handle);
}


int uv_backend_fd(const uv_loop_t* loop) {
  return loop->backend_fd;
}


static int uv__loop_alive(const uv_loop_t* loop) {
  return uv__has_active_handles(loop) ||
         uv__has_active_reqs(loop) ||
         !uv__queue_empty(&loop->pending_queue) ||
         loop->closing_handles != NULL;
}


static int uv__backend_timeout(const uv_loop_t* loop) {
  if (loop->stop_flag == 0 &&
      /* uv__loop_alive(loop) && */
      (uv__has_active_handles(loop) || uv__has_active_reqs(loop)) &&
      uv__queue_empty(&loop->pending_queue) &&
      uv__queue_empty(&loop->idle_handles) &&
      (loop->flags & UV_LOOP_REAP_CHILDREN) == 0 &&
      loop->closing_handles == NULL)
    return uv__next_timeout(loop);
  return 0;
}


int uv_backend_timeout(const uv_loop_t* loop) {
  if (uv__queue_empty(&loop->watcher_queue))
    return uv__backend_timeout(loop);
  /* Need to call uv_run to update the backend fd state. */
  return 0;
}


int uv_loop_alive(const uv_loop_t* loop) {
  return uv__loop_alive(loop);
}


int uv_run(uv_loop_t* loop, uv_run_mode mode) {
  int timeout;
  int r;
  int can_sleep;

  r = uv__loop_alive(loop);
  if (!r)
    uv__update_time(loop);

  /* Maintain backwards compatibility by processing timers before entering the
   * while loop for UV_RUN_DEFAULT. Otherwise timers only need to be executed
   * once, which should be done after polling in order to maintain proper
   * execution order of the conceptual event loop. */
  if (mode == UV_RUN_DEFAULT && r != 0 && loop->stop_flag == 0) {
    uv__update_time(loop);
    uv__run_timers(loop);
  }

  while (r != 0 && loop->stop_flag == 0) {
    can_sleep =
        uv__queue_empty(&loop->pending_queue) &&
        uv__queue_empty(&loop->idle_handles);

    uv__run_pending(loop);
    uv__run_idle(loop);
    uv__run_prepare(loop);

    timeout = 0;
    if ((mode == UV_RUN_ONCE && can_sleep) || mode == UV_RUN_DEFAULT)
      timeout = uv__backend_timeout(loop);

    uv__metrics_inc_loop_count(loop);

    uv__io_poll(loop, timeout);

    /* Process immediate callbacks (e.g. write_cb) a small fixed number of
     * times to avoid loop starvation.*/
    for (r = 0; r < 8 && !uv__queue_empty(&loop->pending_queue); r++)
      uv__run_pending(loop);

    /* Run one final update on the provider_idle_time in case uv__io_poll
     * returned because the timeout expired, but no events were received. This
     * call will be ignored if the provider_entry_time was either never set (if
     * the timeout == 0) or was already updated b/c an event was received.
     */
    uv__metrics_update_idle_time(loop);

    uv__run_check(loop);
    uv__run_closing_handles(loop);

    uv__update_time(loop);
    uv__run_timers(loop);

    r = uv__loop_alive(loop);
    if (mode == UV_RUN_ONCE || mode == UV_RUN_NOWAIT)
      break;
  }

  /* The if statement lets gcc compile it to a conditional store. Avoids
   * dirtying a cache line.
   */
  if (loop->stop_flag != 0)
    loop->stop_flag = 0;

  return r;
}


void uv_update_time(uv_loop_t* loop) {
  uv__update_time(loop);
}


int uv_is_active(const uv_handle_t* handle) {
  return uv__is_active(handle);
}


/* Open a socket in non-blocking close-on-exec mode, atomically if possible. */
int uv__socket(int domain, int type, int protocol) {
  int sockfd;
  int err;

#if defined(SOCK_NONBLOCK) && defined(SOCK_CLOEXEC)
  sockfd = socket(domain, type | SOCK_NONBLOCK | SOCK_CLOEXEC, protocol);
  if (sockfd != -1)
    return sockfd;

  if (errno != EINVAL)
    return UV__ERR(errno);
#endif

  sockfd = socket(domain, type, protocol);
  if (sockfd == -1)
    return UV__ERR(errno);

  err = uv__nonblock(sockfd, 1);
  if (err == 0)
    err = uv__cloexec(sockfd, 1);

  if (err) {
    uv__close(sockfd);
    return err;
  }

#if defined(SO_NOSIGPIPE)
  {
    int on = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
  }
#endif

  return sockfd;
}

/* get a file pointer to a file in read-only and close-on-exec mode */
FILE* uv__open_file(const char* path) {
  int fd;
  FILE* fp;

  fd = uv__open_cloexec(path, O_RDONLY);
  if (fd < 0)
    return NULL;

   fp = fdopen(fd, "r");
   if (fp == NULL)
     uv__close(fd);

   return fp;
}


int uv__accept(int sockfd) {
  int peerfd;
  int err;

  (void) &err;
  assert(sockfd >= 0);

  do
#ifdef uv__accept4
    peerfd = uv__accept4(sockfd, NULL, NULL, SOCK_NONBLOCK|SOCK_CLOEXEC);
#else
    peerfd = accept(sockfd, NULL, NULL);
#endif
  while (peerfd == -1 && errno == EINTR);

  if (peerfd == -1)
    return UV__ERR(errno);

#ifndef uv__accept4
  err = uv__cloexec(peerfd, 1);
  if (err == 0)
    err = uv__nonblock(peerfd, 1);

  if (err != 0) {
    uv__close(peerfd);
    return err;
  }
#endif

  return peerfd;
}


/* close() on macos has the "interesting" quirk that it fails with EINTR
 * without closing the file descriptor when a thread is in the cancel state.
 * That's why libuv calls close$NOCANCEL() instead.
 *
 * glibc on linux has a similar issue: close() is a cancellation point and
 * will unwind the thread when it's in the cancel state. Work around that
 * by making the system call directly. Musl libc is unaffected.
 */
int uv__close_nocancel(int fd) {
#if defined(__APPLE__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdollar-in-identifier-extension"
#if defined(__LP64__) || TARGET_OS_IPHONE
  extern int close$NOCANCEL(int);
  return close$NOCANCEL(fd);
#else
  extern int close$NOCANCEL$UNIX2003(int);
  return close$NOCANCEL$UNIX2003(fd);
#endif
#pragma GCC diagnostic pop
#elif defined(__linux__) && defined(__SANITIZE_THREAD__) && defined(__clang__)
  long rc;
  __sanitizer_syscall_pre_close(fd);
  rc = syscall(SYS_close, fd);
  __sanitizer_syscall_post_close(rc, fd);
  return rc;
#elif defined(__linux__) && !defined(__SANITIZE_THREAD__)
  return syscall(SYS_close, fd);
#else
  return close(fd);
#endif
}


int uv__close_nocheckstdio(int fd) {
  int saved_errno;
  int rc;

  assert(fd > -1);  /* Catch uninitialized io_watcher.fd bugs. */

  saved_errno = errno;
  rc = uv__close_nocancel(fd);
  if (rc == -1) {
    rc = UV__ERR(errno);
    if (rc == UV_EINTR || rc == UV__ERR(EINPROGRESS))
      rc = 0;    /* The close is in progress, not an error. */
    errno = saved_errno;
  }

  return rc;
}


int uv__close(int fd) {
  assert(fd > STDERR_FILENO);  /* Catch stdio close bugs. */
#if defined(__MVS__)
  SAVE_ERRNO(epoll_file_close(fd));
#endif
  return uv__close_nocheckstdio(fd);
}

#if UV__NONBLOCK_IS_IOCTL
int uv__nonblock_ioctl(int fd, int set) {
  int r;

  do
    r = ioctl(fd, FIONBIO, &set);
  while (r == -1 && errno == EINTR);

  if (r)
    return UV__ERR(errno);

  return 0;
}
#endif


int uv__nonblock_fcntl(int fd, int set) {
  int flags;
  int r;

  do
    r = fcntl(fd, F_GETFL);
  while (r == -1 && errno == EINTR);

  if (r == -1)
    return UV__ERR(errno);

  /* Bail out now if already set/clear. */
  if (!!(r & O_NONBLOCK) == !!set)
    return 0;

  if (set)
    flags = r | O_NONBLOCK;
  else
    flags = r & ~O_NONBLOCK;

  do
    r = fcntl(fd, F_SETFL, flags);
  while (r == -1 && errno == EINTR);

  if (r)
    return UV__ERR(errno);

  return 0;
}


int uv__cloexec(int fd, int set) {
  int flags;
  int r;

  flags = 0;
  if (set)
    flags = FD_CLOEXEC;

  do
    r = fcntl(fd, F_SETFD, flags);
  while (r == -1 && errno == EINTR);

  if (r)
    return UV__ERR(errno);

  return 0;
}


ssize_t uv__recvmsg(int fd, struct msghdr* msg, int flags) {
#if defined(__ANDROID__)   || \
    defined(__DragonFly__) || \
    defined(__FreeBSD__)   || \
    defined(__NetBSD__)    || \
    defined(__OpenBSD__)   || \
    defined(__linux__)
  ssize_t rc;
  rc = recvmsg(fd, msg, flags | MSG_CMSG_CLOEXEC);
  if (rc == -1)
    return UV__ERR(errno);
  return rc;
#else
  struct cmsghdr* cmsg;
  int* pfd;
  int* end;
  ssize_t rc;
  rc = recvmsg(fd, msg, flags);
  if (rc == -1)
    return UV__ERR(errno);
  if (msg->msg_controllen == 0)
    return rc;
  for (cmsg = CMSG_FIRSTHDR(msg); cmsg != NULL; cmsg = CMSG_NXTHDR(msg, cmsg))
    if (cmsg->cmsg_type == SCM_RIGHTS)
      for (pfd = (int*) CMSG_DATA(cmsg),
           end = (int*) ((char*) cmsg + cmsg->cmsg_len);
           pfd < end;
           pfd += 1)
        uv__cloexec(*pfd, 1);
  return rc;
#endif
}


int uv_cwd(char* buffer, size_t* size) {
  char scratch[1 + UV__PATH_MAX];

  if (buffer == NULL || size == NULL)
    return UV_EINVAL;

  /* Try to read directly into the user's buffer first... */
  if (getcwd(buffer, *size) != NULL)
    goto fixup;

  if (errno != ERANGE)
    return UV__ERR(errno);

  /* ...or into scratch space if the user's buffer is too small
   * so we can report how much space to provide on the next try.
   */
  if (getcwd(scratch, sizeof(scratch)) == NULL)
    return UV__ERR(errno);

  buffer = scratch;

fixup:

  *size = strlen(buffer);

  if (*size > 1 && buffer[*size - 1] == '/') {
    *size -= 1;
    buffer[*size] = '\0';
  }

  if (buffer == scratch) {
    *size += 1;
    return UV_ENOBUFS;
  }

  return 0;
}


int uv_chdir(const char* dir) {
  if (chdir(dir))
    return UV__ERR(errno);

  return 0;
}


void uv_disable_stdio_inheritance(void) {
  int fd;

  /* Set the CLOEXEC flag on all open descriptors. Unconditionally try the
   * first 16 file descriptors. After that, bail out after the first error.
   */
  for (fd = 0; ; fd++)
    if (uv__cloexec(fd, 1) && fd > 15)
      break;
}


int uv_fileno(const uv_handle_t* handle, uv_os_fd_t* fd) {
  int fd_out;

  switch (handle->type) {
  case UV_TCP:
  case UV_NAMED_PIPE:
  case UV_TTY:
    fd_out = uv__stream_fd((uv_stream_t*) handle);
    break;

  case UV_UDP:
    fd_out = ((uv_udp_t *) handle)->io_watcher.fd;
    break;

  case UV_POLL:
    fd_out = ((uv_poll_t *) handle)->io_watcher.fd;
    break;

  default:
    return UV_EINVAL;
  }

  if (uv__is_closing(handle) || fd_out == -1)
    return UV_EBADF;

  *fd = fd_out;
  return 0;
}


static void uv__run_pending(uv_loop_t* loop) {
  struct uv__queue* q;
  struct uv__queue pq;
  uv__io_t* w;

  uv__queue_move(&loop->pending_queue, &pq);

  while (!uv__queue_empty(&pq)) {
    q = uv__queue_head(&pq);
    uv__queue_remove(q);
    uv__queue_init(q);
    w = uv__queue_data(q, uv__io_t, pending_queue);
    w->cb(loop, w, POLLOUT);
  }
}


static unsigned int next_power_of_two(unsigned int val) {
  val -= 1;
  val |= val >> 1;
  val |= val >> 2;
  val |= val >> 4;
  val |= val >> 8;
  val |= val >> 16;
  val += 1;
  return val;
}

static void maybe_resize(uv_loop_t* loop, unsigned int len) {
  uv__io_t** watchers;
  void* fake_watcher_list;
  void* fake_watcher_count;
  unsigned int nwatchers;
  unsigned int i;

  if (len <= loop->nwatchers)
    return;

  /* Preserve fake watcher list and count at the end of the watchers */
  if (loop->watchers != NULL) {
    fake_watcher_list = loop->watchers[loop->nwatchers];
    fake_watcher_count = loop->watchers[loop->nwatchers + 1];
  } else {
    fake_watcher_list = NULL;
    fake_watcher_count = NULL;
  }

  nwatchers = next_power_of_two(len + 2) - 2;
  watchers = uv__reallocf(loop->watchers,
                          (nwatchers + 2) * sizeof(loop->watchers[0]));

  if (watchers == NULL)
    abort();
  for (i = loop->nwatchers; i < nwatchers; i++)
    watchers[i] = NULL;
  watchers[nwatchers] = fake_watcher_list;
  watchers[nwatchers + 1] = fake_watcher_count;

  loop->watchers = watchers;
  loop->nwatchers = nwatchers;
}


void uv__io_init(uv__io_t* w, uv__io_cb cb, int fd) {
  assert(cb != NULL);
  assert(fd >= -1);
  uv__queue_init(&w->pending_queue);
  uv__queue_init(&w->watcher_queue);
  w->cb = cb;
  w->fd = fd;
  w->events = 0;
  w->pevents = 0;
}


void uv__io_start(uv_loop_t* loop, uv__io_t* w, unsigned int events) {
  assert(0 == (events & ~(POLLIN | POLLOUT | UV__POLLRDHUP | UV__POLLPRI)));
  assert(0 != events);
  assert(w->fd >= 0);
  assert(w->fd < INT_MAX);

  w->pevents |= events;
  maybe_resize(loop, w->fd + 1);

#if !defined(__sun)
  /* The event ports backend needs to rearm all file descriptors on each and
   * every tick of the event loop but the other backends allow us to
   * short-circuit here if the event mask is unchanged.
   */
  if (w->events == w->pevents)
    return;
#endif

  if (uv__queue_empty(&w->watcher_queue))
    uv__queue_insert_tail(&loop->watcher_queue, &w->watcher_queue);

  if (loop->watchers[w->fd] == NULL) {
    loop->watchers[w->fd] = w;
    loop->nfds++;
  }
}


void uv__io_stop(uv_loop_t* loop, uv__io_t* w, unsigned int events) {
  assert(0 == (events & ~(POLLIN | POLLOUT | UV__POLLRDHUP | UV__POLLPRI)));
  assert(0 != events);

  if (w->fd == -1)
    return;

  assert(w->fd >= 0);

  /* Happens when uv__io_stop() is called on a handle that was never started. */
  if ((unsigned) w->fd >= loop->nwatchers)
    return;

  w->pevents &= ~events;

  if (w->pevents == 0) {
    uv__queue_remove(&w->watcher_queue);
    uv__queue_init(&w->watcher_queue);
    w->events = 0;

    if (w == loop->watchers[w->fd]) {
      assert(loop->nfds > 0);
      loop->watchers[w->fd] = NULL;
      loop->nfds--;
    }
  }
  else if (uv__queue_empty(&w->watcher_queue))
    uv__queue_insert_tail(&loop->watcher_queue, &w->watcher_queue);
}


void uv__io_close(uv_loop_t* loop, uv__io_t* w) {
  uv__io_stop(loop, w, POLLIN | POLLOUT | UV__POLLRDHUP | UV__POLLPRI);
  uv__queue_remove(&w->pending_queue);

  /* Remove stale events for this file descriptor */
  if (w->fd != -1)
    uv__platform_invalidate_fd(loop, w->fd);
}


void uv__io_feed(uv_loop_t* loop, uv__io_t* w) {
  if (uv__queue_empty(&w->pending_queue))
    uv__queue_insert_tail(&loop->pending_queue, &w->pending_queue);
}


int uv__io_active(const uv__io_t* w, unsigned int events) {
  assert(0 == (events & ~(POLLIN | POLLOUT | UV__POLLRDHUP | UV__POLLPRI)));
  assert(0 != events);
  return 0 != (w->pevents & events);
}


int uv__fd_exists(uv_loop_t* loop, int fd) {
  return (unsigned) fd < loop->nwatchers && loop->watchers[fd] != NULL;
}


int uv_getrusage(uv_rusage_t* rusage) {
  struct rusage usage;

  if (getrusage(RUSAGE_SELF, &usage))
    return UV__ERR(errno);

  rusage->ru_utime.tv_sec = usage.ru_utime.tv_sec;
  rusage->ru_utime.tv_usec = usage.ru_utime.tv_usec;

  rusage->ru_stime.tv_sec = usage.ru_stime.tv_sec;
  rusage->ru_stime.tv_usec = usage.ru_stime.tv_usec;

#if !defined(__MVS__) && !defined(__HAIKU__)
  rusage->ru_maxrss = usage.ru_maxrss;
  rusage->ru_ixrss = usage.ru_ixrss;
  rusage->ru_idrss = usage.ru_idrss;
  rusage->ru_isrss = usage.ru_isrss;
  rusage->ru_minflt = usage.ru_minflt;
  rusage->ru_majflt = usage.ru_majflt;
  rusage->ru_nswap = usage.ru_nswap;
  rusage->ru_inblock = usage.ru_inblock;
  rusage->ru_oublock = usage.ru_oublock;
  rusage->ru_msgsnd = usage.ru_msgsnd;
  rusage->ru_msgrcv = usage.ru_msgrcv;
  rusage->ru_nsignals = usage.ru_nsignals;
  rusage->ru_nvcsw = usage.ru_nvcsw;
  rusage->ru_nivcsw = usage.ru_nivcsw;
#endif

  /* Most platforms report ru_maxrss in kilobytes; macOS and Solaris are
   * the outliers because of course they are.
   */
#if defined(__APPLE__)
  rusage->ru_maxrss /= 1024;                  /* macOS and iOS report bytes. */
#elif defined(__sun)
  rusage->ru_maxrss *= getpagesize() / 1024;  /* Solaris reports pages. */
#endif

  return 0;
}


int uv__open_cloexec(const char* path, int flags) {
#if defined(O_CLOEXEC)
  int fd;

  fd = open(path, flags | O_CLOEXEC);
  if (fd == -1)
    return UV__ERR(errno);

  return fd;
#else  /* O_CLOEXEC */
  int err;
  int fd;

  fd = open(path, flags);
  if (fd == -1)
    return UV__ERR(errno);

  err = uv__cloexec(fd, 1);
  if (err) {
    uv__close(fd);
    return err;
  }

  return fd;
#endif  /* O_CLOEXEC */
}


int uv__slurp(const char* filename, char* buf, size_t len) {
  ssize_t n;
  int fd;

  assert(len > 0);

  fd = uv__open_cloexec(filename, O_RDONLY);
  if (fd < 0)
    return fd;

  do
    n = read(fd, buf, len - 1);
  while (n == -1 && errno == EINTR);

  if (uv__close_nocheckstdio(fd))
    abort();

  if (n < 0)
    return UV__ERR(errno);

  buf[n] = '\0';

  return 0;
}


int uv__dup2_cloexec(int oldfd, int newfd) {
#if defined(__FreeBSD__) || defined(__NetBSD__) || defined(__linux__)
  int r;

  r = dup3(oldfd, newfd, O_CLOEXEC);
  if (r == -1)
    return UV__ERR(errno);

  return r;
#else
  int err;
  int r;

  r = dup2(oldfd, newfd);  /* Never retry. */
  if (r == -1)
    return UV__ERR(errno);

  err = uv__cloexec(newfd, 1);
  if (err != 0) {
    uv__close(newfd);
    return err;
  }

  return r;
#endif
}


int uv_os_homedir(char* buffer, size_t* size) {
  uv_passwd_t pwd;
  size_t len;
  int r;

  /* Check if the HOME environment variable is set first. The task of
     performing input validation on buffer and size is taken care of by
     uv_os_getenv(). */
  r = uv_os_getenv("HOME", buffer, size);

  if (r != UV_ENOENT)
    return r;

  /* HOME is not set, so call uv_os_get_passwd() */
  r = uv_os_get_passwd(&pwd);

  if (r != 0) {
    return r;
  }

  len = strlen(pwd.homedir);

  if (len >= *size) {
    *size = len + 1;
    uv_os_free_passwd(&pwd);
    return UV_ENOBUFS;
  }

  memcpy(buffer, pwd.homedir, len + 1);
  *size = len;
  uv_os_free_passwd(&pwd);

  return 0;
}


int uv_os_tmpdir(char* buffer, size_t* size) {
  const char* buf;
  size_t len;

  if (buffer == NULL || size == NULL || *size == 0)
    return UV_EINVAL;

#define CHECK_ENV_VAR(name)                                                   \
  do {                                                                        \
    buf = getenv(name);                                                       \
    if (buf != NULL)                                                          \
      goto return_buffer;                                                     \
  }                                                                           \
  while (0)

  /* Check the TMPDIR, TMP, TEMP, and TEMPDIR environment variables in order */
  CHECK_ENV_VAR("TMPDIR");
  CHECK_ENV_VAR("TMP");
  CHECK_ENV_VAR("TEMP");
  CHECK_ENV_VAR("TEMPDIR");

#undef CHECK_ENV_VAR

  /* No temp environment variables defined */
  #if defined(__ANDROID__)
    buf = "/data/local/tmp";
  #else
    buf = "/tmp";
  #endif

return_buffer:
  len = strlen(buf);

  if (len >= *size) {
    *size = len + 1;
    return UV_ENOBUFS;
  }

  /* The returned directory should not have a trailing slash. */
  if (len > 1 && buf[len - 1] == '/') {
    len--;
  }

  memcpy(buffer, buf, len + 1);
  buffer[len] = '\0';
  *size = len;

  return 0;
}


static int uv__getpwuid_r(uv_passwd_t *pwd, uid_t uid) {
  struct passwd pw;
  struct passwd* result;
  char* buf;
  size_t bufsize;
  size_t name_size;
  size_t homedir_size;
  size_t shell_size;
  int r;

  if (pwd == NULL)
    return UV_EINVAL;

  /* Calling sysconf(_SC_GETPW_R_SIZE_MAX) would get the suggested size, but it
   * is frequently 1024 or 4096, so we can just use that directly. The pwent
   * will not usually be large. */
  for (bufsize = 2000;; bufsize *= 2) {
    buf = uv__malloc(bufsize);

    if (buf == NULL)
      return UV_ENOMEM;

    do
      r = getpwuid_r(uid, &pw, buf, bufsize, &result);
    while (r == EINTR);

    if (r != 0 || result == NULL)
      uv__free(buf);

    if (r != ERANGE)
      break;
  }

  if (r != 0)
    return UV__ERR(r);

  if (result == NULL)
    return UV_ENOENT;

  /* Allocate memory for the username, shell, and home directory */
  name_size = strlen(pw.pw_name) + 1;
  homedir_size = strlen(pw.pw_dir) + 1;
  shell_size = strlen(pw.pw_shell) + 1;
  pwd->username = uv__malloc(name_size + homedir_size + shell_size);

  if (pwd->username == NULL) {
    uv__free(buf);
    return UV_ENOMEM;
  }

  /* Copy the username */
  memcpy(pwd->username, pw.pw_name, name_size);

  /* Copy the home directory */
  pwd->homedir = pwd->username + name_size;
  memcpy(pwd->homedir, pw.pw_dir, homedir_size);

  /* Copy the shell */
  pwd->shell = pwd->homedir + homedir_size;
  memcpy(pwd->shell, pw.pw_shell, shell_size);

  /* Copy the uid and gid */
  pwd->uid = pw.pw_uid;
  pwd->gid = pw.pw_gid;

  uv__free(buf);

  return 0;
}


int uv_os_get_group(uv_group_t* grp, uv_uid_t gid) {
#if defined(__ANDROID__) && __ANDROID_API__ < 24
  /* This function getgrgid_r() was added in Android N (level 24) */
  return UV_ENOSYS;
#else
  struct group gp;
  struct group* result;
  char* buf;
  char* gr_mem;
  size_t bufsize;
  size_t name_size;
  long members;
  size_t mem_size;
  int r;

  if (grp == NULL)
    return UV_EINVAL;

  /* Calling sysconf(_SC_GETGR_R_SIZE_MAX) would get the suggested size, but it
   * is frequently 1024 or 4096, so we can just use that directly. The pwent
   * will not usually be large. */
  for (bufsize = 2000;; bufsize *= 2) {
    buf = uv__malloc(bufsize);

    if (buf == NULL)
      return UV_ENOMEM;

    do
      r = getgrgid_r(gid, &gp, buf, bufsize, &result);
    while (r == EINTR);

    if (r != 0 || result == NULL)
      uv__free(buf);

    if (r != ERANGE)
      break;
  }

  if (r != 0)
    return UV__ERR(r);

  if (result == NULL)
    return UV_ENOENT;

  /* Allocate memory for the groupname and members. */
  name_size = strlen(gp.gr_name) + 1;
  members = 0;
  mem_size = sizeof(char*);
  for (r = 0; gp.gr_mem[r] != NULL; r++) {
    mem_size += strlen(gp.gr_mem[r]) + 1 + sizeof(char*);
    members++;
  }

  gr_mem = uv__malloc(name_size + mem_size);
  if (gr_mem == NULL) {
    uv__free(buf);
    return UV_ENOMEM;
  }

  /* Copy the members */
  grp->members = (char**) gr_mem;
  grp->members[members] = NULL;
  gr_mem = (char*) &grp->members[members + 1];
  for (r = 0; r < members; r++) {
    grp->members[r] = gr_mem;
    strcpy(gr_mem, gp.gr_mem[r]);
    gr_mem += strlen(gr_mem) + 1;
  }
  assert(gr_mem == (char*)grp->members + mem_size);

  /* Copy the groupname */
  grp->groupname = gr_mem;
  memcpy(grp->groupname, gp.gr_name, name_size);
  gr_mem += name_size;

  /* Copy the gid */
  grp->gid = gp.gr_gid;

  uv__free(buf);

  return 0;
#endif
}


int uv_os_get_passwd(uv_passwd_t* pwd) {
  return uv__getpwuid_r(pwd, geteuid());
}


int uv_os_get_passwd2(uv_passwd_t* pwd, uv_uid_t uid) {
  return uv__getpwuid_r(pwd, uid);
}


int uv_translate_sys_error(int sys_errno) {
  /* If < 0 then it's already a libuv error. */
  return sys_errno <= 0 ? sys_errno : -sys_errno;
}


int uv_os_environ(uv_env_item_t** envitems, int* count) {
  int i, j, cnt;
  uv_env_item_t* envitem;

  *envitems = NULL;
  *count = 0;

  for (i = 0; environ[i] != NULL; i++);

  *envitems = uv__calloc(i, sizeof(**envitems));

  if (*envitems == NULL)
    return UV_ENOMEM;

  for (j = 0, cnt = 0; j < i; j++) {
    char* buf;
    char* ptr;

    if (environ[j] == NULL)
      break;

    buf = uv__strdup(environ[j]);
    if (buf == NULL)
      goto fail;

    ptr = strchr(buf, '=');
    if (ptr == NULL) {
      uv__free(buf);
      continue;
    }

    *ptr = '\0';

    envitem = &(*envitems)[cnt];
    envitem->name = buf;
    envitem->value = ptr + 1;

    cnt++;
  }

  *count = cnt;
  return 0;

fail:
  for (i = 0; i < cnt; i++) {
    envitem = &(*envitems)[cnt];
    uv__free(envitem->name);
  }
  uv__free(*envitems);

  *envitems = NULL;
  *count = 0;
  return UV_ENOMEM;
}


int uv_os_getenv(const char* name, char* buffer, size_t* size) {
  char* var;
  size_t len;

  if (name == NULL || buffer == NULL || size == NULL || *size == 0)
    return UV_EINVAL;

  var = getenv(name);

  if (var == NULL)
    return UV_ENOENT;

  len = strlen(var);

  if (len >= *size) {
    *size = len + 1;
    return UV_ENOBUFS;
  }

  memcpy(buffer, var, len + 1);
  *size = len;

  return 0;
}


int uv_os_setenv(const char* name, const char* value) {
  if (name == NULL || value == NULL)
    return UV_EINVAL;

  if (setenv(name, value, 1) != 0)
    return UV__ERR(errno);

  return 0;
}


int uv_os_unsetenv(const char* name) {
  if (name == NULL)
    return UV_EINVAL;

  if (unsetenv(name) != 0)
    return UV__ERR(errno);

  return 0;
}


int uv_os_gethostname(char* buffer, size_t* size) {
  /*
    On some platforms, if the input buffer is not large enough, gethostname()
    succeeds, but truncates the result. libuv can detect this and return ENOBUFS
    instead by creating a large enough buffer and comparing the hostname length
    to the size input.
  */
  char buf[UV_MAXHOSTNAMESIZE];
  size_t len;

  if (buffer == NULL || size == NULL || *size == 0)
    return UV_EINVAL;

  if (gethostname(buf, sizeof(buf)) != 0)
    return UV__ERR(errno);

  buf[sizeof(buf) - 1] = '\0'; /* Null terminate, just to be safe. */
  len = strlen(buf);

  if (len >= *size) {
    *size = len + 1;
    return UV_ENOBUFS;
  }

  memcpy(buffer, buf, len + 1);
  *size = len;
  return 0;
}


uv_os_fd_t uv_get_osfhandle(int fd) {
  return fd;
}

int uv_open_osfhandle(uv_os_fd_t os_fd) {
  return os_fd;
}

uv_pid_t uv_os_getpid(void) {
  return getpid();
}


uv_pid_t uv_os_getppid(void) {
  return getppid();
}

int uv_cpumask_size(void) {
#if UV__CPU_AFFINITY_SUPPORTED
  return CPU_SETSIZE;
#else
  return UV_ENOTSUP;
#endif
}

int uv_os_getpriority(uv_pid_t pid, int* priority) {
  int r;

  if (priority == NULL)
    return UV_EINVAL;

  errno = 0;
  r = getpriority(PRIO_PROCESS, (int) pid);

  if (r == -1 && errno != 0)
    return UV__ERR(errno);

  *priority = r;
  return 0;
}


int uv_os_setpriority(uv_pid_t pid, int priority) {
  if (priority < UV_PRIORITY_HIGHEST || priority > UV_PRIORITY_LOW)
    return UV_EINVAL;

  if (setpriority(PRIO_PROCESS, (int) pid, priority) != 0)
    return UV__ERR(errno);

  return 0;
}

/**
 * If the function succeeds, the return value is 0.
 * If the function fails, the return value is non-zero.
 * for Linux, when schedule policy is SCHED_OTHER (default), priority is 0.
 * So the output parameter priority is actually the nice value.
*/
int uv_thread_getpriority(uv_thread_t tid, int* priority) {
  int r;
  int policy;
  struct sched_param param;
#ifdef __linux__
  pid_t pid = gettid();
#endif

  if (priority == NULL)
    return UV_EINVAL;

  r = pthread_getschedparam(tid, &policy, &param);
  if (r != 0)
    return UV__ERR(errno);

#ifdef __linux__
  if (SCHED_OTHER == policy && pthread_equal(tid, pthread_self())) {
    errno = 0;
    r = getpriority(PRIO_PROCESS, pid);
    if (r == -1 && errno != 0)
      return UV__ERR(errno);
    *priority = r;
    return 0;
  }
#endif

  *priority = param.sched_priority;
  return 0;
}

#ifdef __linux__
static int set_nice_for_calling_thread(int priority) {
  int r;
  int nice;

  if (priority < UV_THREAD_PRIORITY_LOWEST || priority > UV_THREAD_PRIORITY_HIGHEST)
    return UV_EINVAL;

  pid_t pid = gettid();
  nice = 0 - priority * 2;
  r = setpriority(PRIO_PROCESS, pid, nice);
  if (r != 0)
    return UV__ERR(errno);
  return 0;
}
#endif

/**
 * If the function succeeds, the return value is 0.
 * If the function fails, the return value is non-zero.
*/
int uv_thread_setpriority(uv_thread_t tid, int priority) {
#if !defined(__GNU__)
  int r;
  int min;
  int max;
  int range;
  int prio;
  int policy;
  struct sched_param param;

  if (priority < UV_THREAD_PRIORITY_LOWEST || priority > UV_THREAD_PRIORITY_HIGHEST)
    return UV_EINVAL;

  r = pthread_getschedparam(tid, &policy, &param);
  if (r != 0)
    return UV__ERR(errno);

#ifdef __linux__
/**
 * for Linux, when schedule policy is SCHED_OTHER (default), priority must be 0,
 * we should set the nice value in this case.
*/
  if (SCHED_OTHER == policy && pthread_equal(tid, pthread_self()))
    return set_nice_for_calling_thread(priority);
#endif

#ifdef __PASE__
  min = 1;
  max = 127;
#else
  min = sched_get_priority_min(policy);
  max = sched_get_priority_max(policy);
#endif

  if (min == -1 || max == -1)
    return UV__ERR(errno);

  range = max - min;

  switch (priority) {
    case UV_THREAD_PRIORITY_HIGHEST:
      prio = max;
      break;
    case UV_THREAD_PRIORITY_ABOVE_NORMAL:
      prio = min + range * 3 / 4;
      break;
    case UV_THREAD_PRIORITY_NORMAL:
      prio = min + range / 2;
      break;
    case UV_THREAD_PRIORITY_BELOW_NORMAL:
      prio = min + range / 4;
      break;
    case UV_THREAD_PRIORITY_LOWEST:
      prio = min;
      break;
    default:
      return 0;
  }

  if (param.sched_priority != prio) {
    param.sched_priority = prio;
    r = pthread_setschedparam(tid, policy, &param);
    if (r != 0)
      return UV__ERR(errno);
  }

  return 0;
#else  /* !defined(__GNU__) */
  /* Simulate success on systems where thread priority is not implemented. */
  return 0;
#endif  /* !defined(__GNU__) */
}

int uv_os_uname(uv_utsname_t* buffer) {
  struct utsname buf;
  int r;

  if (buffer == NULL)
    return UV_EINVAL;

  if (uname(&buf) == -1) {
    r = UV__ERR(errno);
    goto error;
  }

  r = uv__strscpy(buffer->sysname, buf.sysname, sizeof(buffer->sysname));
  if (r == UV_E2BIG)
    goto error;

#ifdef _AIX
  r = snprintf(buffer->release,
               sizeof(buffer->release),
               "%s.%s",
               buf.version,
               buf.release);
  if (r >= sizeof(buffer->release)) {
    r = UV_E2BIG;
    goto error;
  }
#else
  r = uv__strscpy(buffer->release, buf.release, sizeof(buffer->release));
  if (r == UV_E2BIG)
    goto error;
#endif

  r = uv__strscpy(buffer->version, buf.version, sizeof(buffer->version));
  if (r == UV_E2BIG)
    goto error;

#if defined(_AIX) || defined(__PASE__)
  r = uv__strscpy(buffer->machine, "ppc64", sizeof(buffer->machine));
#else
  r = uv__strscpy(buffer->machine, buf.machine, sizeof(buffer->machine));
#endif

  if (r == UV_E2BIG)
    goto error;

  return 0;

error:
  buffer->sysname[0] = '\0';
  buffer->release[0] = '\0';
  buffer->version[0] = '\0';
  buffer->machine[0] = '\0';
  return r;
}

int uv__getsockpeername(const uv_handle_t* handle,
                        uv__peersockfunc func,
                        struct sockaddr* name,
                        int* namelen) {
  socklen_t socklen;
  uv_os_fd_t fd;
  int r;

  r = uv_fileno(handle, &fd);
  if (r < 0)
    return r;

  /* sizeof(socklen_t) != sizeof(int) on some systems. */
  socklen = (socklen_t) *namelen;

  if (func(fd, name, &socklen))
    return UV__ERR(errno);

  *namelen = (int) socklen;
  return 0;
}

int uv_gettimeofday(uv_timeval64_t* tv) {
  struct timeval time;

  if (tv == NULL)
    return UV_EINVAL;

  if (gettimeofday(&time, NULL) != 0)
    return UV__ERR(errno);

  tv->tv_sec = (int64_t) time.tv_sec;
  tv->tv_usec = (int32_t) time.tv_usec;
  return 0;
}

void uv_sleep(unsigned int msec) {
  struct timespec timeout;
  int rc;

  timeout.tv_sec = msec / 1000;
  timeout.tv_nsec = (msec % 1000) * 1000 * 1000;

  do
    rc = nanosleep(&timeout, &timeout);
  while (rc == -1 && errno == EINTR);

  assert(rc == 0);
}

int uv__search_path(const char* prog, char* buf, size_t* buflen) {
  char abspath[UV__PATH_MAX];
  size_t abspath_size;
  char trypath[UV__PATH_MAX];
  char* cloned_path;
  char* path_env;
  char* token;
  char* itr;

  if (buf == NULL || buflen == NULL || *buflen == 0)
    return UV_EINVAL;

  /*
   * Possibilities for prog:
   * i) an absolute path such as: /home/user/myprojects/nodejs/node
   * ii) a relative path such as: ./node or ../myprojects/nodejs/node
   * iii) a bare filename such as "node", after exporting PATH variable
   *     to its location.
   */

  /* Case i) and ii) absolute or relative paths */
  if (strchr(prog, '/') != NULL) {
    if (realpath(prog, abspath) != abspath)
      return UV__ERR(errno);

    abspath_size = strlen(abspath);

    *buflen -= 1;
    if (*buflen > abspath_size)
      *buflen = abspath_size;

    memcpy(buf, abspath, *buflen);
    buf[*buflen] = '\0';

    return 0;
  }

  /* Case iii). Search PATH environment variable */
  cloned_path = NULL;
  token = NULL;
  path_env = getenv("PATH");

  if (path_env == NULL)
    return UV_EINVAL;

  cloned_path = uv__strdup(path_env);
  if (cloned_path == NULL)
    return UV_ENOMEM;

  token = uv__strtok(cloned_path, ":", &itr);
  while (token != NULL) {
    snprintf(trypath, sizeof(trypath) - 1, "%s/%s", token, prog);
    if (realpath(trypath, abspath) == abspath) {
      /* Check the match is executable */
      if (access(abspath, X_OK) == 0) {
        abspath_size = strlen(abspath);

        *buflen -= 1;
        if (*buflen > abspath_size)
          *buflen = abspath_size;

        memcpy(buf, abspath, *buflen);
        buf[*buflen] = '\0';

        uv__free(cloned_path);
        return 0;
      }
    }
    token = uv__strtok(NULL, ":", &itr);
  }
  uv__free(cloned_path);

  /* Out of tokens (path entries), and no match found */
  return UV_EINVAL;
}

#if defined(__linux__) || defined (__FreeBSD__)
# define uv__cpu_count(cpuset) CPU_COUNT(cpuset)
#elif defined(__NetBSD__)
static int uv__cpu_count(cpuset_t* set) {
  int rc;
  cpuid_t i;

  rc = 0;
  for (i = 0;; i++) {
    int r = cpuset_isset(i, set);
    if (r < 0)
      break;
    if (r)
      rc++;
  }

  return rc;
}
#endif /* __NetBSD__ */

unsigned int uv_available_parallelism(void) {
  long rc = -1;

#ifdef __linux__
  cpu_set_t set;

  memset(&set, 0, sizeof(set));

  /* sysconf(_SC_NPROCESSORS_ONLN) in musl calls sched_getaffinity() but in
   * glibc it's... complicated... so for consistency try sched_getaffinity()
   * before falling back to sysconf(_SC_NPROCESSORS_ONLN).
   */
  if (0 == sched_getaffinity(0, sizeof(set), &set))
    rc = uv__cpu_count(&set);
#elif defined(__MVS__)
  rc = __get_num_online_cpus();
  if (rc < 1)
    rc = 1;

  return (unsigned) rc;
#elif defined(__FreeBSD__)
  cpuset_t set;

  memset(&set, 0, sizeof(set));

  if (0 == cpuset_getaffinity(CPU_LEVEL_WHICH, CPU_WHICH_PID, -1, sizeof(set), &set))
    rc = uv__cpu_count(&set);
#elif defined(__NetBSD__)
  cpuset_t* set = cpuset_create();
  if (set != NULL) {
    if (0 == sched_getaffinity_np(getpid(), sizeof(set), &set))
      rc = uv__cpu_count(&set);
    cpuset_destroy(set);
  }
#elif defined(__APPLE__)
  int nprocs;
  size_t i;
  size_t len = sizeof(nprocs);
  static const char *mib[] = {
    "hw.activecpu",
    "hw.logicalcpu",
    "hw.ncpu"
  };

  for (i = 0; i < ARRAY_SIZE(mib); i++) {
    if (0 == sysctlbyname(mib[i], &nprocs, &len, NULL, 0) &&
	      len == sizeof(nprocs) &&
	      nprocs > 0) {
      rc = nprocs;
      break;
    }
  }
#elif defined(__OpenBSD__)
  int nprocs;
  size_t i;
  size_t len = sizeof(nprocs);
  static int mib[][2] = {
# ifdef HW_NCPUONLINE
    { CTL_HW, HW_NCPUONLINE },
# endif
    { CTL_HW, HW_NCPU }
  };

  for (i = 0; i < ARRAY_SIZE(mib); i++) {
    if (0 == sysctl(mib[i], ARRAY_SIZE(mib[i]), &nprocs, &len, NULL, 0) &&
	len == sizeof(nprocs) &&
        nprocs > 0) {
      rc = nprocs;
      break;
    }
  }
#endif /* __linux__ */

  if (rc < 0)
    rc = sysconf(_SC_NPROCESSORS_ONLN);

#ifdef __linux__
  {
    double rc_with_cgroup;
    uv__cpu_constraint c = {0, 0, 0.0};

    if (uv__get_constrained_cpu(&c) == 0 && c.period_length > 0) {
      rc_with_cgroup = (double)c.quota_per_period / c.period_length * c.proportions;
      if (rc_with_cgroup < rc)
        rc = (long)rc_with_cgroup; /* Casting is safe since rc_with_cgroup < rc < LONG_MAX */
    }
  }
#endif  /* __linux__ */

  if (rc < 1)
    rc = 1;

  return (unsigned) rc;
}

int uv__sock_reuseport(int fd) {
  int on = 1;
#if defined(__FreeBSD__) && __FreeBSD__ >= 12 && defined(SO_REUSEPORT_LB)
  /* FreeBSD 12 introduced a new socket option named SO_REUSEPORT_LB
   * with the capability of load balancing, it's the substitution of
   * the SO_REUSEPORTs on Linux and DragonFlyBSD. */
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEPORT_LB, &on, sizeof(on)))
    return UV__ERR(errno);
#elif (defined(__linux__) || \
      defined(_AIX73) || \
      (defined(__DragonFly__) && __DragonFly_version >= 300600) || \
      (defined(UV__SOLARIS_11_4) && UV__SOLARIS_11_4)) && \
      defined(SO_REUSEPORT)
  /* On Linux 3.9+, the SO_REUSEPORT implementation distributes connections
   * evenly across all of the threads (or processes) that are blocked in
   * accept() on the same port. As with TCP, SO_REUSEPORT distributes datagrams
   * evenly across all of the receiving threads (or process).
   *
   * DragonFlyBSD 3.6.0 extended SO_REUSEPORT to distribute workload to
   * available sockets, which made it the equivalent of Linux's SO_REUSEPORT.
   *
   * AIX 7.2.5 added the feature that would add the capability to distribute
   * incoming connections or datagrams across all listening ports for SO_REUSEPORT.
   *
   * Solaris 11 supported SO_REUSEPORT, but it's implemented only for
   * binding to the same address and port, without load balancing.
   * Solaris 11.4 extended SO_REUSEPORT with the capability of load balancing.
   */
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on)))
    return UV__ERR(errno);
#else
  (void) (fd);
  (void) (on);
  /* SO_REUSEPORTs do not have the capability of load balancing on platforms
   * other than those mentioned above. The semantics are completely different,
   * therefore we shouldn't enable it, but fail this operation to indicate that
   * UV_[TCP/UDP]_REUSEPORT is not supported on these platforms. */
  return UV_ENOTSUP;
#endif

  return 0;
}
