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
#include <assert.h>

static void ares_requeue_queries(ares_conn_t  *conn,
                                 ares_status_t requeue_status)
{
  ares_query_t  *query;
  ares_timeval_t now;

  ares_tvnow(&now);

  while ((query = ares_llist_first_val(conn->queries_to_conn)) != NULL) {
    ares_requeue_query(query, &now, requeue_status, ARES_TRUE, NULL);
  }
}

void ares_close_connection(ares_conn_t *conn, ares_status_t requeue_status)
{
  ares_server_t  *server  = conn->server;
  ares_channel_t *channel = server->channel;

  /* Unlink */
  ares_llist_node_claim(
    ares_htable_asvp_get_direct(channel->connnode_by_socket, conn->fd));
  ares_htable_asvp_remove(channel->connnode_by_socket, conn->fd);

  if (conn->flags & ARES_CONN_FLAG_TCP) {
    server->tcp_conn = NULL;
  }

  ares_buf_destroy(conn->in_buf);
  ares_buf_destroy(conn->out_buf);

  /* Requeue queries to other connections */
  ares_requeue_queries(conn, requeue_status);

  ares_llist_destroy(conn->queries_to_conn);

  ares_conn_sock_state_cb_update(conn, ARES_CONN_STATE_NONE);

  ares_socket_close(channel, conn->fd);

  ares_free(conn);
}

void ares_close_sockets(ares_server_t *server)
{
  ares_llist_node_t *node;

  while ((node = ares_llist_node_first(server->connections)) != NULL) {
    ares_conn_t *conn = ares_llist_node_val(node);
    ares_close_connection(conn, ARES_SUCCESS);
  }
}

void ares_check_cleanup_conns(const ares_channel_t *channel)
{
  ares_slist_node_t *snode;

  if (channel == NULL) {
    return; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  /* Iterate across each server */
  for (snode = ares_slist_node_first(channel->servers); snode != NULL;
       snode = ares_slist_node_next(snode)) {
    ares_server_t     *server = ares_slist_node_val(snode);
    ares_llist_node_t *cnode;

    /* Iterate across each connection */
    cnode = ares_llist_node_first(server->connections);
    while (cnode != NULL) {
      ares_llist_node_t *next       = ares_llist_node_next(cnode);
      ares_conn_t       *conn       = ares_llist_node_val(cnode);
      ares_bool_t        do_cleanup = ARES_FALSE;
      cnode                         = next;

      /* Has connections, not eligible */
      if (ares_llist_len(conn->queries_to_conn)) {
        continue;
      }

      /* If we are configured not to stay open, close it out */
      if (!(channel->flags & ARES_FLAG_STAYOPEN)) {
        do_cleanup = ARES_TRUE;
      }

      /* If the associated server has failures, close it out. Resetting the
       * connection (and specifically the source port number) can help resolve
       * situations where packets are being dropped.
       */
      if (conn->server->consec_failures > 0) {
        do_cleanup = ARES_TRUE;
      }

      /* If the udp connection hit its max queries, always close it */
      if (!(conn->flags & ARES_CONN_FLAG_TCP) && channel->udp_max_queries > 0 &&
          conn->total_queries >= channel->udp_max_queries) {
        do_cleanup = ARES_TRUE;
      }

      if (!do_cleanup) {
        continue;
      }

      /* Clean it up */
      ares_close_connection(conn, ARES_SUCCESS);
    }
  }
}
