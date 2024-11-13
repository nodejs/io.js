/* MIT License
 *
 * Copyright (c) 1998 Massachusetts Institute of Technology
 * Copyright (c) The c-ares project and its contributors
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

#ifdef HAVE_LIMITS_H
#  include <limits.h>
#endif


void ares_timeval_remaining(ares_timeval_t       *remaining,
                            const ares_timeval_t *now,
                            const ares_timeval_t *tout)
{
  memset(remaining, 0, sizeof(*remaining));

  /* Expired! */
  if (tout->sec < now->sec ||
      (tout->sec == now->sec && tout->usec < now->usec)) {
    return;
  }

  remaining->sec = tout->sec - now->sec;
  if (tout->usec < now->usec) {
    remaining->sec  -= 1;
    remaining->usec  = (tout->usec + 1000000) - now->usec;
  } else {
    remaining->usec = tout->usec - now->usec;
  }
}

void ares_timeval_diff(ares_timeval_t *tvdiff, const ares_timeval_t *tvstart,
                       const ares_timeval_t *tvstop)
{
  tvdiff->sec = tvstop->sec - tvstart->sec;
  if (tvstop->usec > tvstart->usec) {
    tvdiff->usec = tvstop->usec - tvstart->usec;
  } else {
    tvdiff->sec  -= 1;
    tvdiff->usec  = tvstop->usec + 1000000 - tvstart->usec;
  }
}

static void ares_timeval_to_struct_timeval(struct timeval       *tv,
                                           const ares_timeval_t *atv)
{
#ifdef USE_WINSOCK
  tv->tv_sec = (long)atv->sec;
#else
  tv->tv_sec = (time_t)atv->sec;
#endif

  tv->tv_usec = (int)atv->usec;
}

static void struct_timeval_to_ares_timeval(ares_timeval_t       *atv,
                                           const struct timeval *tv)
{
  atv->sec  = (ares_int64_t)tv->tv_sec;
  atv->usec = (unsigned int)tv->tv_usec;
}

static struct timeval *ares_timeout_int(const ares_channel_t *channel,
                                        struct timeval       *maxtv,
                                        struct timeval       *tvbuf)
{
  const ares_query_t *query;
  ares_slist_node_t  *node;
  ares_timeval_t      now;
  ares_timeval_t      atvbuf;
  ares_timeval_t      amaxtv;

  /* The minimum timeout of all queries is always the first entry in
   * channel->queries_by_timeout */
  node = ares_slist_node_first(channel->queries_by_timeout);
  /* no queries/timeout */
  if (node == NULL) {
    return maxtv;
  }

  query = ares_slist_node_val(node);

  ares_tvnow(&now);

  ares_timeval_remaining(&atvbuf, &now, &query->timeout);

  ares_timeval_to_struct_timeval(tvbuf, &atvbuf);

  if (maxtv == NULL) {
    return tvbuf;
  }

  /* Return the minimum time between maxtv and tvbuf */
  struct_timeval_to_ares_timeval(&amaxtv, maxtv);

  if (atvbuf.sec > amaxtv.sec) {
    return maxtv;
  }

  if (atvbuf.sec < amaxtv.sec) {
    return tvbuf;
  }

  if (atvbuf.usec > amaxtv.usec) {
    return maxtv;
  }

  return tvbuf;
}

struct timeval *ares_timeout(const ares_channel_t *channel,
                             struct timeval *maxtv, struct timeval *tvbuf)
{
  struct timeval *rv;

  if (channel == NULL || tvbuf == NULL) {
    return NULL;
  }

  ares_channel_lock(channel);

  rv = ares_timeout_int(channel, maxtv, tvbuf);

  ares_channel_unlock(channel);

  return rv;
}
