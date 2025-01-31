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

/* This file contains both the uv__async internal infrastructure and the
 * user-facing uv_async_t functions.
 */

#include "uv.h"
#include "internal.h"

#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>  /* snprintf() */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sched.h>  /* sched_yield() */

#ifdef __linux__
#include <sys/eventfd.h>
#endif

#if UV__KQUEUE_EVFILT_USER
static uv_once_t kqueue_runtime_detection_guard = UV_ONCE_INIT;
static int kqueue_evfilt_user_support = 1;


static void uv__kqueue_runtime_detection(void) {
  int kq;
  struct kevent ev[2];
  struct timespec timeout = {0, 0};

  /* Perform the runtime detection to ensure that kqueue with
   * EVFILT_USER actually works. */
  kq = kqueue();
  EV_SET(ev, UV__KQUEUE_EVFILT_USER_IDENT, EVFILT_USER,
         EV_ADD | EV_CLEAR, 0, 0, 0);
  EV_SET(ev + 1, UV__KQUEUE_EVFILT_USER_IDENT, EVFILT_USER,
         0, NOTE_TRIGGER, 0, 0);
  if (kevent(kq, ev, 2, ev, 1, &timeout) < 1 ||
      ev[0].filter != EVFILT_USER ||
      ev[0].ident != UV__KQUEUE_EVFILT_USER_IDENT ||
      ev[0].flags & EV_ERROR)
    /* If we wind up here, we can assume that EVFILT_USER is defined but
     * broken on the current system. */
    kqueue_evfilt_user_support = 0;
  uv__close(kq);
}
#endif

static void uv__async_send(uv_loop_t* loop);
static int uv__async_start(uv_loop_t* loop);
static void uv__cpu_relax(void);


int uv_async_init(uv_loop_t* loop, uv_async_t* handle, uv_async_cb async_cb) {
  int err;

  err = uv__async_start(loop);
  if (err)
    return err;

  uv__handle_init(loop, (uv_handle_t*)handle, UV_ASYNC);
  handle->async_cb = async_cb;
  handle->pending = 0;
  handle->u.fd = 0; /* This will be used as a busy flag. */

  uv__queue_insert_tail(&loop->async_handles, &handle->queue);
  uv__handle_start(handle);

  return 0;
}


int uv_async_send(uv_async_t* handle) {
  _Atomic int* pending;
  _Atomic int* busy;

  pending = (_Atomic int*) &handle->pending;
  busy = (_Atomic int*) &handle->u.fd;

  /* Do a cheap read first. */
  if (atomic_load_explicit(pending, memory_order_relaxed) != 0)
    return 0;

  /* Set the loop to busy. */
  atomic_fetch_add(busy, 1);

  /* Wake up the other thread's event loop. */
  if (atomic_exchange(pending, 1) == 0)
    uv__async_send(handle->loop);

  /* Set the loop to not-busy. */
  atomic_fetch_add(busy, -1);

  return 0;
}


/* Wait for the busy flag to clear before closing.
 * Only call this from the event loop thread. */
static void uv__async_spin(uv_async_t* handle) {
  _Atomic int* pending;
  _Atomic int* busy;
  int i;

  pending = (_Atomic int*) &handle->pending;
  busy = (_Atomic int*) &handle->u.fd;

  /* Set the pending flag first, so no new events will be added by other
   * threads after this function returns. */
  atomic_store(pending, 1);

  for (;;) {
    /* 997 is not completely chosen at random. It's a prime number, acyclic by
     * nature, and should therefore hopefully dampen sympathetic resonance.
     */
    for (i = 0; i < 997; i++) {
      if (atomic_load(busy) == 0)
        return;

      /* Other thread is busy with this handle, spin until it's done. */
      uv__cpu_relax();
    }

    /* Yield the CPU. We may have preempted the other thread while it's
     * inside the critical section and if it's running on the same CPU
     * as us, we'll just burn CPU cycles until the end of our time slice.
     */
    sched_yield();
  }
}


void uv__async_close(uv_async_t* handle) {
  uv__async_spin(handle);
  uv__queue_remove(&handle->queue);
  uv__handle_stop(handle);
}


static void uv__async_io(uv_loop_t* loop, uv__io_t* w, unsigned int events) {
  char buf[1024];
  ssize_t r;
  struct uv__queue queue;
  struct uv__queue* q;
  uv_async_t* h;
  _Atomic int *pending;

  assert(w == &loop->async_io_watcher);

#if UV__KQUEUE_EVFILT_USER
  for (;!kqueue_evfilt_user_support;) {
#else
  for (;;) {
#endif
    r = read(w->fd, buf, sizeof(buf));

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

  uv__queue_move(&loop->async_handles, &queue);
  while (!uv__queue_empty(&queue)) {
    q = uv__queue_head(&queue);
    h = uv__queue_data(q, uv_async_t, queue);

    uv__queue_remove(q);
    uv__queue_insert_tail(&loop->async_handles, q);

    /* Atomically fetch and clear pending flag */
    pending = (_Atomic int*) &h->pending;
    if (atomic_exchange(pending, 0) == 0)
      continue;

    if (h->async_cb == NULL)
      continue;

    h->async_cb(h);
  }
}


static void uv__async_send(uv_loop_t* loop) {
  const void* buf;
  ssize_t len;
  int fd;
  int r;

  buf = "";
  len = 1;
  fd = loop->async_wfd;

#if defined(__linux__)
  if (fd == -1) {
    static const uint64_t val = 1;
    buf = &val;
    len = sizeof(val);
    fd = loop->async_io_watcher.fd;  /* eventfd */
  }
#elif UV__KQUEUE_EVFILT_USER
  struct kevent ev;

  if (kqueue_evfilt_user_support) {
    fd = loop->async_io_watcher.fd; /* magic number for EVFILT_USER */
    EV_SET(&ev, fd, EVFILT_USER, 0, NOTE_TRIGGER, 0, 0);
    r = kevent(loop->backend_fd, &ev, 1, NULL, 0, NULL);
    if (r == 0)
      return;
    abort();
  }
#endif

  do
    r = write(fd, buf, len);
  while (r == -1 && errno == EINTR);

  if (r == len)
    return;

  if (r == -1)
    if (errno == EAGAIN || errno == EWOULDBLOCK)
      return;

  abort();
}


static int uv__async_start(uv_loop_t* loop) {
  int pipefd[2];
  int err;
#if UV__KQUEUE_EVFILT_USER
  struct kevent ev;
#endif

  if (loop->async_io_watcher.fd != -1)
    return 0;

#ifdef __linux__
  err = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
  if (err < 0)
    return UV__ERR(errno);

  pipefd[0] = err;
  pipefd[1] = -1;
#elif UV__KQUEUE_EVFILT_USER
  uv_once(&kqueue_runtime_detection_guard, uv__kqueue_runtime_detection);
  if (kqueue_evfilt_user_support) {
    /* In order not to break the generic pattern of I/O polling, a valid
     * file descriptor is required to take up a room in loop->watchers,
     * thus we create one for that, but this fd will not be actually used,
     * it's just a placeholder and magic number which is going to be closed
     * during the cleanup, as other FDs. */
    err = uv__open_cloexec("/dev/null", O_RDONLY);
    if (err < 0)
      return err;

    pipefd[0] = err;
    pipefd[1] = -1;

    /* When using EVFILT_USER event to wake up the kqueue, this event must be
     * registered beforehand. Otherwise, calling kevent() to issue an
     * unregistered EVFILT_USER event will get an ENOENT.
     * Since uv__async_send() may happen before uv__io_poll() with multi-threads,
     * we can't defer this registration of EVFILT_USER event as we did for other
     * events, but must perform it right away. */
    EV_SET(&ev, err, EVFILT_USER, EV_ADD | EV_CLEAR, 0, 0, 0);
    err = kevent(loop->backend_fd, &ev, 1, NULL, 0, NULL);
    if (err < 0)
      return UV__ERR(errno);
  } else {
    err = uv__make_pipe(pipefd, UV_NONBLOCK_PIPE);
    if (err < 0)
      return err;
  }
#else
  err = uv__make_pipe(pipefd, UV_NONBLOCK_PIPE);
  if (err < 0)
    return err;
#endif

  uv__io_init(&loop->async_io_watcher, uv__async_io, pipefd[0]);
  uv__io_start(loop, &loop->async_io_watcher, POLLIN);
  loop->async_wfd = pipefd[1];

#if UV__KQUEUE_EVFILT_USER
  /* Prevent the EVFILT_USER event from being added to kqueue redundantly
   * and mistakenly later in uv__io_poll(). */
  if (kqueue_evfilt_user_support)
    loop->async_io_watcher.events = loop->async_io_watcher.pevents;
#endif

  return 0;
}


void uv__async_stop(uv_loop_t* loop) {
  struct uv__queue queue;
  struct uv__queue* q;
  uv_async_t* h;

  if (loop->async_io_watcher.fd == -1)
    return;

  /* Make sure no other thread is accessing the async handle fd after the loop
   * cleanup.
   */
  uv__queue_move(&loop->async_handles, &queue);
  while (!uv__queue_empty(&queue)) {
    q = uv__queue_head(&queue);
    h = uv__queue_data(q, uv_async_t, queue);

    uv__queue_remove(q);
    uv__queue_insert_tail(&loop->async_handles, q);

    uv__async_spin(h);
  }

  if (loop->async_wfd != -1) {
    if (loop->async_wfd != loop->async_io_watcher.fd)
      uv__close(loop->async_wfd);
    loop->async_wfd = -1;
  }

  uv__io_stop(loop, &loop->async_io_watcher, POLLIN);
  uv__close(loop->async_io_watcher.fd);
  loop->async_io_watcher.fd = -1;
}


int uv__async_fork(uv_loop_t* loop) {
  struct uv__queue queue;
  struct uv__queue* q;
  uv_async_t* h;

  if (loop->async_io_watcher.fd == -1) /* never started */
    return 0;

  uv__queue_move(&loop->async_handles, &queue);
  while (!uv__queue_empty(&queue)) {
    q = uv__queue_head(&queue);
    h = uv__queue_data(q, uv_async_t, queue);

    uv__queue_remove(q);
    uv__queue_insert_tail(&loop->async_handles, q);

    /* The state of any thread that set pending is now likely corrupt in this
     * child because the user called fork, so just clear these flags and move
     * on. Calling most libc functions after `fork` is declared to be undefined
     * behavior anyways, unless async-signal-safe, for multithreaded programs
     * like libuv, and nothing interesting in pthreads is async-signal-safe.
     */
    h->pending = 0;
    /* This is the busy flag, and we just abruptly lost all other threads. */
    h->u.fd = 0;
  }

  /* Recreate these, since they still exist, but belong to the wrong pid now. */
  if (loop->async_wfd != -1) {
    if (loop->async_wfd != loop->async_io_watcher.fd)
      uv__close(loop->async_wfd);
    loop->async_wfd = -1;
  }

  uv__io_stop(loop, &loop->async_io_watcher, POLLIN);
  uv__close(loop->async_io_watcher.fd);
  loop->async_io_watcher.fd = -1;

  return uv__async_start(loop);
}


static void uv__cpu_relax(void) {
#if defined(__i386__) || defined(__x86_64__)
  __asm__ __volatile__ ("rep; nop" ::: "memory");  /* a.k.a. PAUSE */
#elif (defined(__arm__) && __ARM_ARCH >= 7) || defined(__aarch64__)
  __asm__ __volatile__ ("yield" ::: "memory");
#elif (defined(__ppc__) || defined(__ppc64__)) && defined(__APPLE__)
  __asm volatile ("" : : : "memory");
#elif !defined(__APPLE__) && (defined(__powerpc64__) || defined(__ppc64__) || defined(__PPC64__))
  __asm__ __volatile__ ("or 1,1,1; or 2,2,2" ::: "memory");
#endif
}
