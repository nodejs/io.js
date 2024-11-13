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
#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif
#ifdef HAVE_FCNTL_H
#  include <fcntl.h>
#endif

#ifdef HAVE_PIPE
typedef struct {
  int filedes[2];
} ares_pipeevent_t;

static void ares_pipeevent_destroy(ares_pipeevent_t *p)
{
  if (p->filedes[0] != -1) {
    close(p->filedes[0]);
  }
  if (p->filedes[1] != -1) {
    close(p->filedes[1]);
  }

  ares_free(p);
}

static void ares_pipeevent_destroy_cb(void *arg)
{
  ares_pipeevent_destroy(arg);
}

static ares_pipeevent_t *ares_pipeevent_init(void)
{
  ares_pipeevent_t *p = ares_malloc_zero(sizeof(*p));
  if (p == NULL) {
    return NULL; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  p->filedes[0] = -1;
  p->filedes[1] = -1;

#  ifdef HAVE_PIPE2
  if (pipe2(p->filedes, O_NONBLOCK | O_CLOEXEC) != 0) {
    ares_pipeevent_destroy(p); /* LCOV_EXCL_LINE: UntestablePath */
    return NULL;               /* LCOV_EXCL_LINE: UntestablePath */
  }
#  else
  if (pipe(p->filedes) != 0) {
    ares_pipeevent_destroy(p);
    return NULL;
  }

#    ifdef O_NONBLOCK
  {
    int val;
    val = fcntl(p->filedes[0], F_GETFL, 0);
    if (val >= 0) {
      val |= O_NONBLOCK;
    }
    fcntl(p->filedes[0], F_SETFL, val);

    val = fcntl(p->filedes[1], F_GETFL, 0);
    if (val >= 0) {
      val |= O_NONBLOCK;
    }
    fcntl(p->filedes[1], F_SETFL, val);
  }
#    endif

#    ifdef FD_CLOEXEC
  fcntl(p->filedes[0], F_SETFD, FD_CLOEXEC);
  fcntl(p->filedes[1], F_SETFD, FD_CLOEXEC);
#    endif
#  endif

#  ifdef F_SETNOSIGPIPE
  fcntl(p->filedes[0], F_SETNOSIGPIPE, 1);
  fcntl(p->filedes[1], F_SETNOSIGPIPE, 1);
#  endif

  return p;
}

static void ares_pipeevent_signal(const ares_event_t *e)
{
  const ares_pipeevent_t *p;

  if (e == NULL || e->data == NULL) {
    return; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  p = e->data;
  (void)write(p->filedes[1], "1", 1);
}

static void ares_pipeevent_cb(ares_event_thread_t *e, ares_socket_t fd,
                              void *data, ares_event_flags_t flags)
{
  unsigned char           buf[32];
  const ares_pipeevent_t *p = NULL;

  (void)e;
  (void)fd;
  (void)flags;

  if (data == NULL) {
    return; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  p = data;

  while (read(p->filedes[0], buf, sizeof(buf)) == sizeof(buf)) {
    /* Do nothing */
  }
}

ares_event_t *ares_pipeevent_create(ares_event_thread_t *e)
{
  ares_event_t     *event = NULL;
  ares_pipeevent_t *p     = NULL;
  ares_status_t     status;

  p = ares_pipeevent_init();
  if (p == NULL) {
    return NULL;
  }

  status = ares_event_update(&event, e, ARES_EVENT_FLAG_READ, ares_pipeevent_cb,
                             p->filedes[0], p, ares_pipeevent_destroy_cb,
                             ares_pipeevent_signal);
  if (status != ARES_SUCCESS) {
    ares_pipeevent_destroy(p); /* LCOV_EXCL_LINE: DefensiveCoding */
    return NULL;               /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  return event;
}

#endif
