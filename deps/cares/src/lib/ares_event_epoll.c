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

#ifdef HAVE_SYS_EPOLL_H
#  include <sys/epoll.h>
#endif
#ifdef HAVE_FCNTL_H
#  include <fcntl.h>
#endif

#ifdef HAVE_EPOLL

typedef struct {
  int epoll_fd;
} ares_evsys_epoll_t;

static void ares_evsys_epoll_destroy(ares_event_thread_t *e)
{
  ares_evsys_epoll_t *ep = NULL;

  if (e == NULL) {
    return; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  ep = e->ev_sys_data;
  if (ep == NULL) {
    return; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  if (ep->epoll_fd != -1) {
    close(ep->epoll_fd);
  }

  ares_free(ep);
  e->ev_sys_data = NULL;
}

static ares_bool_t ares_evsys_epoll_init(ares_event_thread_t *e)
{
  ares_evsys_epoll_t *ep = NULL;

  ep = ares_malloc_zero(sizeof(*ep));
  if (ep == NULL) {
    return ARES_FALSE; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  e->ev_sys_data = ep;

  ep->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
  if (ep->epoll_fd == -1) {
    ares_evsys_epoll_destroy(e); /* LCOV_EXCL_LINE: UntestablePath */
    return ARES_FALSE;           /* LCOV_EXCL_LINE: UntestablePath */
  }

  e->ev_signal = ares_pipeevent_create(e);
  if (e->ev_signal == NULL) {
    ares_evsys_epoll_destroy(e); /* LCOV_EXCL_LINE: UntestablePath */
    return ARES_FALSE;           /* LCOV_EXCL_LINE: UntestablePath */
  }

  return ARES_TRUE;
}

static ares_bool_t ares_evsys_epoll_event_add(ares_event_t *event)
{
  const ares_event_thread_t *e  = event->e;
  const ares_evsys_epoll_t  *ep = e->ev_sys_data;
  struct epoll_event         epev;

  memset(&epev, 0, sizeof(epev));
  epev.data.fd = event->fd;
  epev.events  = EPOLLRDHUP | EPOLLERR | EPOLLHUP;
  if (event->flags & ARES_EVENT_FLAG_READ) {
    epev.events |= EPOLLIN;
  }
  if (event->flags & ARES_EVENT_FLAG_WRITE) {
    epev.events |= EPOLLOUT;
  }
  if (epoll_ctl(ep->epoll_fd, EPOLL_CTL_ADD, event->fd, &epev) != 0) {
    return ARES_FALSE; /* LCOV_EXCL_LINE: UntestablePath */
  }
  return ARES_TRUE;
}

static void ares_evsys_epoll_event_del(ares_event_t *event)
{
  const ares_event_thread_t *e  = event->e;
  const ares_evsys_epoll_t  *ep = e->ev_sys_data;
  struct epoll_event         epev;

  memset(&epev, 0, sizeof(epev));
  epev.data.fd = event->fd;
  epoll_ctl(ep->epoll_fd, EPOLL_CTL_DEL, event->fd, &epev);
}

static void ares_evsys_epoll_event_mod(ares_event_t      *event,
                                       ares_event_flags_t new_flags)
{
  const ares_event_thread_t *e  = event->e;
  const ares_evsys_epoll_t  *ep = e->ev_sys_data;
  struct epoll_event         epev;

  memset(&epev, 0, sizeof(epev));
  epev.data.fd = event->fd;
  epev.events  = EPOLLRDHUP | EPOLLERR | EPOLLHUP;
  if (new_flags & ARES_EVENT_FLAG_READ) {
    epev.events |= EPOLLIN;
  }
  if (new_flags & ARES_EVENT_FLAG_WRITE) {
    epev.events |= EPOLLOUT;
  }
  epoll_ctl(ep->epoll_fd, EPOLL_CTL_MOD, event->fd, &epev);
}

static size_t ares_evsys_epoll_wait(ares_event_thread_t *e,
                                    unsigned long        timeout_ms)
{
  struct epoll_event        events[8];
  size_t                    nevents = sizeof(events) / sizeof(*events);
  const ares_evsys_epoll_t *ep      = e->ev_sys_data;
  int                       rv;
  size_t                    i;
  size_t                    cnt = 0;

  memset(events, 0, sizeof(events));

  rv = epoll_wait(ep->epoll_fd, events, (int)nevents,
                  (timeout_ms == 0) ? -1 : (int)timeout_ms);
  if (rv < 0) {
    return 0; /* LCOV_EXCL_LINE: UntestablePath */
  }

  nevents = (size_t)rv;

  for (i = 0; i < nevents; i++) {
    ares_event_t      *ev;
    ares_event_flags_t flags = 0;

    ev = ares__htable_asvp_get_direct(e->ev_sock_handles,
                                      (ares_socket_t)events[i].data.fd);
    if (ev == NULL || ev->cb == NULL) {
      continue; /* LCOV_EXCL_LINE: DefensiveCoding */
    }

    cnt++;

    if (events[i].events & (EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
      flags |= ARES_EVENT_FLAG_READ;
    }
    if (events[i].events & EPOLLOUT) {
      flags |= ARES_EVENT_FLAG_WRITE;
    }

    ev->cb(e, ev->fd, ev->data, flags);
  }

  return cnt;
}

const ares_event_sys_t ares_evsys_epoll = { "epoll",
                                            ares_evsys_epoll_init,
                                            ares_evsys_epoll_destroy,
                                            ares_evsys_epoll_event_add,
                                            ares_evsys_epoll_event_del,
                                            ares_evsys_epoll_event_mod,
                                            ares_evsys_epoll_wait };
#endif
