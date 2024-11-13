/* MIT License
 *
 * Copyright (c) 2004 Daniel Stenberg
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

/*
 * ares_cancel() cancels all ongoing requests/resolves that might be going on
 * on the given channel. It does NOT kill the channel, use ares_destroy() for
 * that.
 */
void ares_cancel(ares_channel_t *channel)
{
  if (channel == NULL) {
    return;
  }

  ares_channel_lock(channel);

  if (ares_llist_len(channel->all_queries) > 0) {
    ares_llist_node_t *node = NULL;
    ares_llist_node_t *next = NULL;

    /* Swap list heads, so that only those queries which were present on entry
     * into this function are cancelled. New queries added by callbacks of
     * queries being cancelled will not be cancelled themselves.
     */
    ares_llist_t      *list_copy = channel->all_queries;
    channel->all_queries         = ares_llist_create(NULL);

    /* Out of memory, this function doesn't return a result code though so we
     * can't report to caller */
    if (channel->all_queries == NULL) {
      channel->all_queries = list_copy; /* LCOV_EXCL_LINE: OutOfMemory */
      goto done;                        /* LCOV_EXCL_LINE: OutOfMemory */
    }

    node = ares_llist_node_first(list_copy);
    while (node != NULL) {
      ares_query_t *query;

      /* Cache next since this node is being deleted */
      next = ares_llist_node_next(node);

      query                   = ares_llist_node_claim(node);
      query->node_all_queries = NULL;

      /* NOTE: its possible this may enqueue new queries */
      query->callback(query->arg, ARES_ECANCELLED, 0, NULL);
      ares_free_query(query);

      node = next;
    }

    ares_llist_destroy(list_copy);
  }

  /* See if the connections should be cleaned up */
  ares_check_cleanup_conns(channel);

  ares_queue_notify_empty(channel);

done:
  ares_channel_unlock(channel);
}
