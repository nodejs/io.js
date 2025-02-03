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

/* Some systems might default to something low like 256 (NetBSD), lets define
 * this to assist.  Really, no one should be using select, but lets be safe
 * anyhow */
#define FD_SETSIZE 4096

#include "ares_private.h"
#include "ares_event.h"
#ifdef HAVE_SYS_SELECT_H
#  include <sys/select.h>
#endif

/* All systems have select(), but not all have a way to wake, so we require
 * pipe() to wake the select() */
#if defined(HAVE_PIPE)

static ares_bool_t ares_evsys_select_init(ares_event_thread_t *e)
{
  e->ev_signal = ares_pipeevent_create(e);
  if (e->ev_signal == NULL) {
    return ARES_FALSE; /* LCOV_EXCL_LINE: UntestablePath */
  }
  return ARES_TRUE;
}

static void ares_evsys_select_destroy(ares_event_thread_t *e)
{
  (void)e;
}

static ares_bool_t ares_evsys_select_event_add(ares_event_t *event)
{
  (void)event;
  return ARES_TRUE;
}

static void ares_evsys_select_event_del(ares_event_t *event)
{
  (void)event;
}

static void ares_evsys_select_event_mod(ares_event_t      *event,
                                        ares_event_flags_t new_flags)
{
  (void)event;
  (void)new_flags;
}

static size_t ares_evsys_select_wait(ares_event_thread_t *e,
                                     unsigned long        timeout_ms)
{
  size_t          num_fds = 0;
  ares_socket_t  *fdlist  = ares_htable_asvp_keys(e->ev_sock_handles, &num_fds);
  int             rv;
  size_t          cnt = 0;
  size_t          i;
  fd_set          read_fds;
  fd_set          write_fds;
  fd_set          except_fds;
  int             nfds = 0;
  struct timeval  tv;
  struct timeval *tout = NULL;

  FD_ZERO(&read_fds);
  FD_ZERO(&write_fds);
  FD_ZERO(&except_fds);

  for (i = 0; i < num_fds; i++) {
    const ares_event_t *ev =
      ares_htable_asvp_get_direct(e->ev_sock_handles, fdlist[i]);
    if (ev->flags & ARES_EVENT_FLAG_READ) {
      FD_SET(ev->fd, &read_fds);
    }
    if (ev->flags & ARES_EVENT_FLAG_WRITE) {
      FD_SET(ev->fd, &write_fds);
    }
    FD_SET(ev->fd, &except_fds);
    if (ev->fd + 1 > nfds) {
      nfds = ev->fd + 1;
    }
  }

  if (timeout_ms) {
    tv.tv_sec  = (int)(timeout_ms / 1000);
    tv.tv_usec = (int)((timeout_ms % 1000) * 1000);
    tout       = &tv;
  }

  rv = select(nfds, &read_fds, &write_fds, &except_fds, tout);
  if (rv > 0) {
    for (i = 0; i < num_fds; i++) {
      ares_event_t      *ev;
      ares_event_flags_t flags = 0;

      ev = ares_htable_asvp_get_direct(e->ev_sock_handles, fdlist[i]);
      if (ev == NULL || ev->cb == NULL) {
        continue; /* LCOV_EXCL_LINE: DefensiveCoding */
      }

      if (FD_ISSET(fdlist[i], &read_fds) || FD_ISSET(fdlist[i], &except_fds)) {
        flags |= ARES_EVENT_FLAG_READ;
      }

      if (FD_ISSET(fdlist[i], &write_fds)) {
        flags |= ARES_EVENT_FLAG_WRITE;
      }

      if (flags == 0) {
        continue;
      }

      cnt++;

      ev->cb(e, fdlist[i], ev->data, flags);
    }
  }

  ares_free(fdlist);

  return cnt;
}

const ares_event_sys_t ares_evsys_select = {
  "select",
  ares_evsys_select_init,
  ares_evsys_select_destroy,   /* NoOp */
  ares_evsys_select_event_add, /* NoOp */
  ares_evsys_select_event_del, /* NoOp */
  ares_evsys_select_event_mod, /* NoOp */
  ares_evsys_select_wait
};

#endif
