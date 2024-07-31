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
#ifdef HAVE_POLL_H
#  include <poll.h>
#endif

#if defined(HAVE_POLL)

static ares_bool_t ares_evsys_poll_init(ares_event_thread_t *e)
{
  e->ev_signal = ares_pipeevent_create(e);
  if (e->ev_signal == NULL) {
    return ARES_FALSE; /* LCOV_EXCL_LINE: UntestablePath */
  }
  return ARES_TRUE;
}

static void ares_evsys_poll_destroy(ares_event_thread_t *e)
{
  (void)e;
}

static ares_bool_t ares_evsys_poll_event_add(ares_event_t *event)
{
  (void)event;
  return ARES_TRUE;
}

static void ares_evsys_poll_event_del(ares_event_t *event)
{
  (void)event;
}

static void ares_evsys_poll_event_mod(ares_event_t      *event,
                                      ares_event_flags_t new_flags)
{
  (void)event;
  (void)new_flags;
}

static size_t ares_evsys_poll_wait(ares_event_thread_t *e,
                                   unsigned long        timeout_ms)
{
  size_t         num_fds = 0;
  ares_socket_t *fdlist  = ares__htable_asvp_keys(e->ev_sock_handles, &num_fds);
  struct pollfd *pollfd  = NULL;
  int            rv;
  size_t         cnt = 0;
  size_t         i;

  if (fdlist != NULL && num_fds) {
    pollfd = ares_malloc_zero(sizeof(*pollfd) * num_fds);
    if (pollfd == NULL) {
      goto done; /* LCOV_EXCL_LINE: OutOfMemory */
    }
    for (i = 0; i < num_fds; i++) {
      const ares_event_t *ev =
        ares__htable_asvp_get_direct(e->ev_sock_handles, fdlist[i]);
      pollfd[i].fd = ev->fd;
      if (ev->flags & ARES_EVENT_FLAG_READ) {
        pollfd[i].events |= POLLIN;
      }
      if (ev->flags & ARES_EVENT_FLAG_WRITE) {
        pollfd[i].events |= POLLOUT;
      }
    }
  }
  ares_free(fdlist);

  rv = poll(pollfd, (nfds_t)num_fds, (timeout_ms == 0) ? -1 : (int)timeout_ms);
  if (rv <= 0) {
    goto done;
  }

  for (i = 0; pollfd != NULL && i < num_fds; i++) {
    ares_event_t      *ev;
    ares_event_flags_t flags = 0;

    if (pollfd[i].revents == 0) {
      continue;
    }

    cnt++;

    ev = ares__htable_asvp_get_direct(e->ev_sock_handles, pollfd[i].fd);
    if (ev == NULL || ev->cb == NULL) {
      continue; /* LCOV_EXCL_LINE: DefensiveCoding */
    }

    if (pollfd[i].revents & (POLLERR | POLLHUP | POLLIN)) {
      flags |= ARES_EVENT_FLAG_READ;
    }

    if (pollfd[i].revents & POLLOUT) {
      flags |= ARES_EVENT_FLAG_WRITE;
    }

    ev->cb(e, pollfd[i].fd, ev->data, flags);
  }

done:
  ares_free(pollfd);
  return cnt;
}

const ares_event_sys_t ares_evsys_poll = { "poll",
                                           ares_evsys_poll_init,
                                           ares_evsys_poll_destroy,   /* NoOp */
                                           ares_evsys_poll_event_add, /* NoOp */
                                           ares_evsys_poll_event_del, /* NoOp */
                                           ares_evsys_poll_event_mod, /* NoOp */
                                           ares_evsys_poll_wait };

#endif
