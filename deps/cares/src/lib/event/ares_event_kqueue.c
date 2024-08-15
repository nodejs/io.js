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
#include "ares_event.h"

#ifdef HAVE_SYS_TYPES_H
#  include <sys/types.h>
#endif
#ifdef HAVE_SYS_EVENT_H
#  include <sys/event.h>
#endif
#ifdef HAVE_SYS_TIME_H
#  include <sys/time.h>
#endif
#ifdef HAVE_FCNTL_H
#  include <fcntl.h>
#endif

#ifdef HAVE_KQUEUE

typedef struct {
  int            kqueue_fd;
  struct kevent *changelist;
  size_t         nchanges;
  size_t         nchanges_alloc;
} ares_evsys_kqueue_t;

static void ares_evsys_kqueue_destroy(ares_event_thread_t *e)
{
  ares_evsys_kqueue_t *kq = NULL;

  if (e == NULL) {
    return;
  }

  kq = e->ev_sys_data;
  if (kq == NULL) {
    return;
  }

  if (kq->kqueue_fd != -1) {
    close(kq->kqueue_fd);
  }

  ares_free(kq->changelist);
  ares_free(kq);
  e->ev_sys_data = NULL;
}

static ares_bool_t ares_evsys_kqueue_init(ares_event_thread_t *e)
{
  ares_evsys_kqueue_t *kq = NULL;

  kq = ares_malloc_zero(sizeof(*kq));
  if (kq == NULL) {
    return ARES_FALSE;
  }

  e->ev_sys_data = kq;

  kq->kqueue_fd = kqueue();
  if (kq->kqueue_fd == -1) {
    ares_evsys_kqueue_destroy(e);
    return ARES_FALSE;
  }

#  ifdef FD_CLOEXEC
  fcntl(kq->kqueue_fd, F_SETFD, FD_CLOEXEC);
#  endif

  kq->nchanges_alloc = 8;
  kq->changelist =
    ares_malloc_zero(kq->nchanges_alloc * sizeof(*kq->changelist));
  if (kq->changelist == NULL) {
    ares_evsys_kqueue_destroy(e);
    return ARES_FALSE;
  }

  e->ev_signal = ares_pipeevent_create(e);
  if (e->ev_signal == NULL) {
    ares_evsys_kqueue_destroy(e);
    return ARES_FALSE;
  }

  return ARES_TRUE;
}

static void ares_evsys_kqueue_enqueue(ares_evsys_kqueue_t *kq, int fd,
                                      int16_t filter, uint16_t flags)
{
  size_t idx;

  if (kq == NULL) {
    return;
  }

  idx = kq->nchanges;

  kq->nchanges++;

  if (kq->nchanges > kq->nchanges_alloc) {
    kq->nchanges_alloc <<= 1;
    kq->changelist       = ares_realloc_zero(
      kq->changelist, (kq->nchanges_alloc >> 1) * sizeof(*kq->changelist),
      kq->nchanges_alloc * sizeof(*kq->changelist));
  }

  EV_SET(&kq->changelist[idx], fd, filter, flags, 0, 0, 0);
}

static void ares_evsys_kqueue_event_process(ares_event_t      *event,
                                            ares_event_flags_t old_flags,
                                            ares_event_flags_t new_flags)
{
  ares_event_thread_t *e = event->e;
  ares_evsys_kqueue_t *kq;

  if (e == NULL) {
    return;
  }

  kq = e->ev_sys_data;
  if (kq == NULL) {
    return;
  }

  if (new_flags & ARES_EVENT_FLAG_READ && !(old_flags & ARES_EVENT_FLAG_READ)) {
    ares_evsys_kqueue_enqueue(kq, event->fd, EVFILT_READ, EV_ADD | EV_ENABLE);
  }

  if (!(new_flags & ARES_EVENT_FLAG_READ) && old_flags & ARES_EVENT_FLAG_READ) {
    ares_evsys_kqueue_enqueue(kq, event->fd, EVFILT_READ, EV_DELETE);
  }

  if (new_flags & ARES_EVENT_FLAG_WRITE &&
      !(old_flags & ARES_EVENT_FLAG_WRITE)) {
    ares_evsys_kqueue_enqueue(kq, event->fd, EVFILT_WRITE, EV_ADD | EV_ENABLE);
  }

  if (!(new_flags & ARES_EVENT_FLAG_WRITE) &&
      old_flags & ARES_EVENT_FLAG_WRITE) {
    ares_evsys_kqueue_enqueue(kq, event->fd, EVFILT_WRITE, EV_DELETE);
  }
}

static ares_bool_t ares_evsys_kqueue_event_add(ares_event_t *event)
{
  ares_evsys_kqueue_event_process(event, 0, event->flags);
  return ARES_TRUE;
}

static void ares_evsys_kqueue_event_del(ares_event_t *event)
{
  ares_evsys_kqueue_event_process(event, event->flags, 0);
}

static void ares_evsys_kqueue_event_mod(ares_event_t      *event,
                                        ares_event_flags_t new_flags)
{
  ares_evsys_kqueue_event_process(event, event->flags, new_flags);
}

static size_t ares_evsys_kqueue_wait(ares_event_thread_t *e,
                                     unsigned long        timeout_ms)
{
  struct kevent        events[8];
  size_t               nevents = sizeof(events) / sizeof(*events);
  ares_evsys_kqueue_t *kq      = e->ev_sys_data;
  int                  rv;
  size_t               i;
  struct timespec      ts;
  struct timespec     *timeout = NULL;
  size_t               cnt     = 0;

  if (timeout_ms != 0) {
    ts.tv_sec  = (time_t)timeout_ms / 1000;
    ts.tv_nsec = (timeout_ms % 1000) * 1000 * 1000;
    timeout    = &ts;
  }

  memset(events, 0, sizeof(events));

  rv = kevent(kq->kqueue_fd, kq->changelist, (int)kq->nchanges, events,
              (int)nevents, timeout);
  if (rv < 0) {
    return 0;
  }

  /* Changelist was consumed */
  kq->nchanges = 0;
  nevents      = (size_t)rv;

  for (i = 0; i < nevents; i++) {
    ares_event_t      *ev;
    ares_event_flags_t flags = 0;

    ev = ares__htable_asvp_get_direct(e->ev_sock_handles,
                                      (ares_socket_t)events[i].ident);
    if (ev == NULL || ev->cb == NULL) {
      continue;
    }

    cnt++;

    if (events[i].filter == EVFILT_READ ||
        events[i].flags & (EV_EOF | EV_ERROR)) {
      flags |= ARES_EVENT_FLAG_READ;
    } else {
      flags |= ARES_EVENT_FLAG_WRITE;
    }

    ev->cb(e, ev->fd, ev->data, flags);
  }

  return cnt;
}

const ares_event_sys_t ares_evsys_kqueue = { "kqueue",
                                             ares_evsys_kqueue_init,
                                             ares_evsys_kqueue_destroy,
                                             ares_evsys_kqueue_event_add,
                                             ares_evsys_kqueue_event_del,
                                             ares_evsys_kqueue_event_mod,
                                             ares_evsys_kqueue_wait };
#endif
