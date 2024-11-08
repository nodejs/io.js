/* MIT License
 *
 * Copyright (c) 2005 Daniel Stenberg
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

int ares_getsock(const ares_channel_t *channel, ares_socket_t *socks,
                 int numsocks) /* size of the 'socks' array */
{
  ares_slist_node_t *snode;
  size_t             sockindex = 0;
  unsigned int       bitmap    = 0;
  unsigned int       setbits   = 0xffffffff;

  /* Are there any active queries? */
  size_t             active_queries;

  if (channel == NULL || numsocks <= 0) {
    return 0;
  }

  ares_channel_lock(channel);

  active_queries = ares_llist_len(channel->all_queries);

  for (snode = ares_slist_node_first(channel->servers); snode != NULL;
       snode = ares_slist_node_next(snode)) {
    ares_server_t     *server = ares_slist_node_val(snode);
    ares_llist_node_t *node;

    for (node = ares_llist_node_first(server->connections); node != NULL;
         node = ares_llist_node_next(node)) {
      const ares_conn_t *conn = ares_llist_node_val(node);

      if (sockindex >= (size_t)numsocks || sockindex >= ARES_GETSOCK_MAXNUM) {
        break;
      }

      /* We only need to register interest in UDP sockets if we have
       * outstanding queries.
       */
      if (!active_queries && !(conn->flags & ARES_CONN_FLAG_TCP)) {
        continue;
      }

      socks[sockindex] = conn->fd;

      if (active_queries || conn->flags & ARES_CONN_FLAG_TCP) {
        bitmap |= ARES_GETSOCK_READABLE(setbits, sockindex);
      }

      if (conn->state_flags & ARES_CONN_STATE_WRITE) {
        /* then the tcp socket is also writable! */
        bitmap |= ARES_GETSOCK_WRITABLE(setbits, sockindex);
      }

      sockindex++;
    }
  }

  ares_channel_unlock(channel);
  return (int)bitmap;
}
