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

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <sys/sysctl.h>
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#if defined(__FreeBSD__)
#include <sys/user.h>
#endif
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

/*
 * Required on
 * - Until at least FreeBSD 11.0
 * - Older versions of Mac OS X
 *
 * http://www.boost.org/doc/libs/1_61_0/boost/asio/detail/kqueue_reactor.hpp
 */
#ifndef EV_OOBAND
#define EV_OOBAND  EV_FLAG1
#endif

static void uv__fs_event(uv_loop_t* loop, uv__io_t* w, unsigned int fflags);


int uv__kqueue_init(uv_loop_t* loop) {
  loop->backend_fd = kqueue();
  if (loop->backend_fd == -1)
    return UV__ERR(errno);

  uv__cloexec(loop->backend_fd, 1);

  return 0;
}


#if defined(__APPLE__) && MAC_OS_X_VERSION_MAX_ALLOWED >= 1070
static _Atomic int uv__has_forked_with_cfrunloop;
#endif

int uv__io_fork(uv_loop_t* loop) {
  int err;
  loop->backend_fd = -1;
  err = uv__kqueue_init(loop);
  if (err)
    return err;

#if defined(__APPLE__) && MAC_OS_X_VERSION_MAX_ALLOWED >= 1070
  if (loop->cf_state != NULL) {
    /* We cannot start another CFRunloop and/or thread in the child
       process; CF aborts if you try or if you try to touch the thread
       at all to kill it. So the best we can do is ignore it from now
       on. This means we can't watch directories in the same way
       anymore (like other BSDs). It also means we cannot properly
       clean up the allocated resources; calling
       uv__fsevents_loop_delete from uv_loop_close will crash the
       process. So we sidestep the issue by pretending like we never
       started it in the first place.
    */
    atomic_store_explicit(&uv__has_forked_with_cfrunloop,
                          1,
                          memory_order_relaxed);
    uv__free(loop->cf_state);
    loop->cf_state = NULL;
  }
#endif /* #if defined(__APPLE__) && MAC_OS_X_VERSION_MAX_ALLOWED >= 1070 */
  return err;
}


int uv__io_check_fd(uv_loop_t* loop, int fd) {
  struct kevent ev;
  int rc;

  rc = 0;
  EV_SET(&ev, fd, EVFILT_READ, EV_ADD, 0, 0, 0);
  if (kevent(loop->backend_fd, &ev, 1, NULL, 0, NULL))
    rc = UV__ERR(errno);

  EV_SET(&ev, fd, EVFILT_READ, EV_DELETE, 0, 0, 0);
  if (rc == 0)
    if (kevent(loop->backend_fd, &ev, 1, NULL, 0, NULL))
      abort();

  return rc;
}


static void uv__kqueue_delete(int kqfd, const struct kevent *ev) {
  struct kevent change;

  EV_SET(&change, ev->ident, ev->filter, EV_DELETE, 0, 0, 0);

  if (0 == kevent(kqfd, &change, 1, NULL, 0, NULL))
    return;

  if (errno == EBADF || errno == ENOENT)
    return;

  abort();
}


void uv__io_poll(uv_loop_t* loop, int timeout) {
  uv__loop_internal_fields_t* lfields;
  struct kevent events[1024];
  struct kevent* ev;
  struct timespec spec;
  unsigned int nevents;
  unsigned int revents;
  struct uv__queue* q;
  uv__io_t* w;
  uv_process_t* process;
  sigset_t* pset;
  sigset_t set;
  uint64_t base;
  uint64_t diff;
  int have_signals;
  int filter;
  int fflags;
  int count;
  int nfds;
  int fd;
  int op;
  int i;
  int user_timeout;
  int reset_timeout;

  if (loop->nfds == 0) {
    assert(uv__queue_empty(&loop->watcher_queue));
    return;
  }

  lfields = uv__get_internal_fields(loop);
  nevents = 0;

  while (!uv__queue_empty(&loop->watcher_queue)) {
    q = uv__queue_head(&loop->watcher_queue);
    uv__queue_remove(q);
    uv__queue_init(q);

    w = uv__queue_data(q, uv__io_t, watcher_queue);
    assert(w->pevents != 0);
    assert(w->fd >= 0);
    assert(w->fd < (int) loop->nwatchers);

    if ((w->events & POLLIN) == 0 && (w->pevents & POLLIN) != 0) {
      filter = EVFILT_READ;
      fflags = 0;
      op = EV_ADD;

      if (w->cb == uv__fs_event) {
        filter = EVFILT_VNODE;
        fflags = NOTE_ATTRIB | NOTE_WRITE  | NOTE_RENAME
               | NOTE_DELETE | NOTE_EXTEND | NOTE_REVOKE;
        op = EV_ADD | EV_ONESHOT; /* Stop the event from firing repeatedly. */
      }

      EV_SET(events + nevents, w->fd, filter, op, fflags, 0, 0);

      if (++nevents == ARRAY_SIZE(events)) {
        if (kevent(loop->backend_fd, events, nevents, NULL, 0, NULL))
          abort();
        nevents = 0;
      }
    }

    if ((w->events & POLLOUT) == 0 && (w->pevents & POLLOUT) != 0) {
      EV_SET(events + nevents, w->fd, EVFILT_WRITE, EV_ADD, 0, 0, 0);

      if (++nevents == ARRAY_SIZE(events)) {
        if (kevent(loop->backend_fd, events, nevents, NULL, 0, NULL))
          abort();
        nevents = 0;
      }
    }

   if ((w->events & UV__POLLPRI) == 0 && (w->pevents & UV__POLLPRI) != 0) {
      EV_SET(events + nevents, w->fd, EV_OOBAND, EV_ADD, 0, 0, 0);

      if (++nevents == ARRAY_SIZE(events)) {
        if (kevent(loop->backend_fd, events, nevents, NULL, 0, NULL))
          abort();
        nevents = 0;
      }
    }

    w->events = w->pevents;
  }

  pset = NULL;
  if (loop->flags & UV_LOOP_BLOCK_SIGPROF) {
    pset = &set;
    sigemptyset(pset);
    sigaddset(pset, SIGPROF);
  }

  assert(timeout >= -1);
  base = loop->time;
  count = 48; /* Benchmarks suggest this gives the best throughput. */

  if (lfields->flags & UV_METRICS_IDLE_TIME) {
    reset_timeout = 1;
    user_timeout = timeout;
    timeout = 0;
  } else {
    reset_timeout = 0;
  }

  for (;; nevents = 0) {
    /* Only need to set the provider_entry_time if timeout != 0. The function
     * will return early if the loop isn't configured with UV_METRICS_IDLE_TIME.
     */
    if (timeout != 0)
      uv__metrics_set_provider_entry_time(loop);

    if (timeout != -1) {
      spec.tv_sec = timeout / 1000;
      spec.tv_nsec = (timeout % 1000) * 1000000;
    }

    if (pset != NULL)
      pthread_sigmask(SIG_BLOCK, pset, NULL);

    /* Store the current timeout in a location that's globally accessible so
     * other locations like uv__work_done() can determine whether the queue
     * of events in the callback were waiting when poll was called.
     */
    lfields->current_timeout = timeout;

    nfds = kevent(loop->backend_fd,
                  events,
                  nevents,
                  events,
                  ARRAY_SIZE(events),
                  timeout == -1 ? NULL : &spec);

    if (nfds == -1)
      assert(errno == EINTR);
    else if (nfds == 0)
      /* Unlimited timeout should only return with events or signal. */
      assert(timeout != -1);

    if (pset != NULL)
      pthread_sigmask(SIG_UNBLOCK, pset, NULL);

    /* Update loop->time unconditionally. It's tempting to skip the update when
     * timeout == 0 (i.e. non-blocking poll) but there is no guarantee that the
     * operating system didn't reschedule our process while in the syscall.
     */
    uv__update_time(loop);

    if (nfds == 0 || nfds == -1) {
      /* If kqueue is empty or interrupted, we might still have children ready
       * to reap immediately. */
      if (loop->flags & UV_LOOP_REAP_CHILDREN) {
        loop->flags &= ~UV_LOOP_REAP_CHILDREN;
        uv__wait_children(loop);
        assert((reset_timeout == 0 ? timeout : user_timeout) == 0);
        return; /* Equivalent to fall-through behavior. */
      }

      if (reset_timeout != 0) {
        timeout = user_timeout;
        reset_timeout = 0;
      } else if (nfds == 0) {
        return;
      }

      /* Interrupted by a signal. Update timeout and poll again. */
      goto update_timeout;
    }

    have_signals = 0;
    nevents = 0;

    assert(loop->watchers != NULL);
    loop->watchers[loop->nwatchers] = (void*) events;
    loop->watchers[loop->nwatchers + 1] = (void*) (uintptr_t) nfds;
    for (i = 0; i < nfds; i++) {
      ev = events + i;
      fd = ev->ident;

      /* Handle kevent NOTE_EXIT results */
      if (ev->filter == EVFILT_PROC) {
        uv__queue_foreach(q, &loop->process_handles) {
          process = uv__queue_data(q, uv_process_t, queue);
          if (process->pid == fd) {
            process->flags |= UV_HANDLE_REAP;
            loop->flags |= UV_LOOP_REAP_CHILDREN;
            break;
          }
        }
        nevents++;
        continue;
      }

      /* Skip invalidated events, see uv__platform_invalidate_fd */
      if (fd == -1)
        continue;
      w = loop->watchers[fd];

      if (w == NULL) {
        /* File descriptor that we've stopped watching, disarm it. */
        uv__kqueue_delete(loop->backend_fd, ev);
        continue;
      }

      if (ev->filter == EVFILT_VNODE) {
        assert(w->events == POLLIN);
        assert(w->pevents == POLLIN);
        uv__metrics_update_idle_time(loop);
        w->cb(loop, w, ev->fflags); /* XXX always uv__fs_event() */
        nevents++;
        continue;
      }

      revents = 0;

      if (ev->filter == EVFILT_READ) {
        if (w->pevents & POLLIN)
          revents |= POLLIN;
        else
          uv__kqueue_delete(loop->backend_fd, ev);

        if ((ev->flags & EV_EOF) && (w->pevents & UV__POLLRDHUP))
          revents |= UV__POLLRDHUP;
      }

      if (ev->filter == EV_OOBAND) {
        if (w->pevents & UV__POLLPRI)
          revents |= UV__POLLPRI;
        else
          uv__kqueue_delete(loop->backend_fd, ev);
      }

      if (ev->filter == EVFILT_WRITE) {
        if (w->pevents & POLLOUT)
          revents |= POLLOUT;
        else
          uv__kqueue_delete(loop->backend_fd, ev);
      }

      if (ev->flags & EV_ERROR)
        revents |= POLLERR;

      if (revents == 0)
        continue;

      /* Run signal watchers last.  This also affects child process watchers
       * because those are implemented in terms of signal watchers.
       */
      if (w == &loop->signal_io_watcher) {
        have_signals = 1;
      } else {
        uv__metrics_update_idle_time(loop);
        w->cb(loop, w, revents);
      }

      nevents++;
    }

    if (loop->flags & UV_LOOP_REAP_CHILDREN) {
      loop->flags &= ~UV_LOOP_REAP_CHILDREN;
      uv__wait_children(loop);
    }

    uv__metrics_inc_events(loop, nevents);
    if (reset_timeout != 0) {
      timeout = user_timeout;
      reset_timeout = 0;
      uv__metrics_inc_events_waiting(loop, nevents);
    }

    if (have_signals != 0) {
      uv__metrics_update_idle_time(loop);
      loop->signal_io_watcher.cb(loop, &loop->signal_io_watcher, POLLIN);
    }

    loop->watchers[loop->nwatchers] = NULL;
    loop->watchers[loop->nwatchers + 1] = NULL;

    if (have_signals != 0)
      return;  /* Event loop should cycle now so don't poll again. */

    if (nevents != 0) {
      if (nfds == ARRAY_SIZE(events) && --count != 0) {
        /* Poll for more events but don't block this time. */
        timeout = 0;
        continue;
      }
      return;
    }

update_timeout:
    if (timeout == 0)
      return;

    if (timeout == -1)
      continue;

    assert(timeout > 0);

    diff = loop->time - base;
    if (diff >= (uint64_t) timeout)
      return;

    timeout -= diff;
  }
}


void uv__platform_invalidate_fd(uv_loop_t* loop, int fd) {
  struct kevent* events;
  uintptr_t i;
  uintptr_t nfds;

  assert(loop->watchers != NULL);
  assert(fd >= 0);

  events = (struct kevent*) loop->watchers[loop->nwatchers];
  nfds = (uintptr_t) loop->watchers[loop->nwatchers + 1];
  if (events == NULL)
    return;

  /* Invalidate events with same file descriptor */
  for (i = 0; i < nfds; i++)
    if ((int) events[i].ident == fd && events[i].filter != EVFILT_PROC)
      events[i].ident = -1;
}


static void uv__fs_event(uv_loop_t* loop, uv__io_t* w, unsigned int fflags) {
  uv_fs_event_t* handle;
  struct kevent ev;
  int events;
  const char* path;
#if defined(F_GETPATH)
  /* MAXPATHLEN == PATH_MAX but the former is what XNU calls it internally. */
  char pathbuf[MAXPATHLEN];
#endif

  handle = container_of(w, uv_fs_event_t, event_watcher);

  if (fflags & (NOTE_ATTRIB | NOTE_EXTEND))
    events = UV_CHANGE;
  else
    events = UV_RENAME;

  path = NULL;
#if defined(F_GETPATH)
  /* Also works when the file has been unlinked from the file system. Passing
   * in the path when the file has been deleted is arguably a little strange
   * but it's consistent with what the inotify backend does.
   */
  if (fcntl(handle->event_watcher.fd, F_GETPATH, pathbuf) == 0)
    path = uv__basename_r(pathbuf);
#elif defined(F_KINFO)
  /* We try to get the file info reference from the file descriptor.
   * the struct's kf_structsize must be initialised beforehand
   * whether with the KINFO_FILE_SIZE constant or this way.
   */
  struct stat statbuf;
  struct kinfo_file kf;

  if (handle->event_watcher.fd != -1 &&
     (!uv__fstat(handle->event_watcher.fd, &statbuf) && !(statbuf.st_mode & S_IFDIR))) {
     /* we are purposely not using KINFO_FILE_SIZE here
      * as it is not available on non intl archs
      * and here it gives 1392 too on intel.
      * anyway, the man page also mentions we can proceed
      * this way.
      */
     kf.kf_structsize = sizeof(kf);
     if (fcntl(handle->event_watcher.fd, F_KINFO, &kf) == 0)
       path = uv__basename_r(kf.kf_path);
  }
#endif
  handle->cb(handle, path, events, 0);

  if (handle->event_watcher.fd == -1)
    return;

  /* Watcher operates in one-shot mode, re-arm it. */
  fflags = NOTE_ATTRIB | NOTE_WRITE  | NOTE_RENAME
         | NOTE_DELETE | NOTE_EXTEND | NOTE_REVOKE;

  EV_SET(&ev, w->fd, EVFILT_VNODE, EV_ADD | EV_ONESHOT, fflags, 0, 0);

  if (kevent(loop->backend_fd, &ev, 1, NULL, 0, NULL))
    abort();
}


int uv_fs_event_init(uv_loop_t* loop, uv_fs_event_t* handle) {
  uv__handle_init(loop, (uv_handle_t*)handle, UV_FS_EVENT);
  return 0;
}


int uv_fs_event_start(uv_fs_event_t* handle,
                      uv_fs_event_cb cb,
                      const char* path,
                      unsigned int flags) {
  int fd;
#if defined(__APPLE__) && MAC_OS_X_VERSION_MAX_ALLOWED >= 1070
  struct stat statbuf;
#endif

  if (uv__is_active(handle))
    return UV_EINVAL;

  handle->cb = cb;
  handle->path = uv__strdup(path);
  if (handle->path == NULL)
    return UV_ENOMEM;

  /* TODO open asynchronously - but how do we report back errors? */
  fd = open(handle->path, O_RDONLY);
  if (fd == -1) {
    uv__free(handle->path);
    handle->path = NULL;
    return UV__ERR(errno);
  }

#if defined(__APPLE__) && MAC_OS_X_VERSION_MAX_ALLOWED >= 1070
  /* Nullify field to perform checks later */
  handle->cf_cb = NULL;
  handle->realpath = NULL;
  handle->realpath_len = 0;
  handle->cf_flags = flags;

  if (uv__fstat(fd, &statbuf))
    goto fallback;
  /* FSEvents works only with directories */
  if (!(statbuf.st_mode & S_IFDIR))
    goto fallback;

  if (0 == atomic_load_explicit(&uv__has_forked_with_cfrunloop,
                                memory_order_relaxed)) {
    int r;
    /* The fallback fd is no longer needed */
    uv__close_nocheckstdio(fd);
    handle->event_watcher.fd = -1;
    r = uv__fsevents_init(handle);
    if (r == 0) {
      uv__handle_start(handle);
    } else {
      uv__free(handle->path);
      handle->path = NULL;
    }
    return r;
  }
fallback:
#endif /* #if defined(__APPLE__) && MAC_OS_X_VERSION_MAX_ALLOWED >= 1070 */

  uv__handle_start(handle);
  uv__io_init(&handle->event_watcher, uv__fs_event, fd);
  uv__io_start(handle->loop, &handle->event_watcher, POLLIN);

  return 0;
}


int uv_fs_event_stop(uv_fs_event_t* handle) {
  int r;
  r = 0;

  if (!uv__is_active(handle))
    return 0;

  uv__handle_stop(handle);

#if defined(__APPLE__) && MAC_OS_X_VERSION_MAX_ALLOWED >= 1070
  if (0 == atomic_load_explicit(&uv__has_forked_with_cfrunloop,
                                memory_order_relaxed))
    if (handle->cf_cb != NULL)
      r = uv__fsevents_close(handle);
#endif

  if (handle->event_watcher.fd != -1) {
    uv__io_close(handle->loop, &handle->event_watcher);
    uv__close(handle->event_watcher.fd);
    handle->event_watcher.fd = -1;
  }

  uv__free(handle->path);
  handle->path = NULL;

  return r;
}


void uv__fs_event_close(uv_fs_event_t* handle) {
  uv_fs_event_stop(handle);
}
