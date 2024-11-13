/* MIT License
 *
 * Copyright (c) 1998 Massachusetts Institute of Technology
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
#include "event/ares_event.h"
#include <assert.h>

void ares_destroy(ares_channel_t *channel)
{
  size_t             i;
  ares_llist_node_t *node = NULL;

  if (channel == NULL) {
    return;
  }

  /* Mark as being shutdown */
  ares_channel_lock(channel);
  channel->sys_up = ARES_FALSE;
  ares_channel_unlock(channel);

  /* Disable configuration change monitoring.  We can't hold a lock because
   * some cleanup routines, such as on Windows, are synchronous operations.
   * What we've observed is a system config change event was triggered right
   * at shutdown time and it tries to take the channel lock and the destruction
   * waits for that event to complete before it continues so we get a channel
   * lock deadlock at shutdown if we hold a lock during this process. */
  if (channel->optmask & ARES_OPT_EVENT_THREAD) {
    ares_event_thread_t *e = channel->sock_state_cb_data;
    if (e && e->configchg) {
      ares_event_configchg_destroy(e->configchg);
      e->configchg = NULL;
    }
  }

  /* Wait for reinit thread to exit if there was one pending, can't be
   * holding a lock as the thread may take locks. */
  if (channel->reinit_thread != NULL) {
    void *rv;
    ares_thread_join(channel->reinit_thread, &rv);
    channel->reinit_thread = NULL;
  }

  /* Lock because callbacks will be triggered, and any system-generated
   * callbacks need to hold a channel lock. */
  ares_channel_lock(channel);

  /* Destroy all queries */
  node = ares_llist_node_first(channel->all_queries);
  while (node != NULL) {
    ares_llist_node_t *next  = ares_llist_node_next(node);
    ares_query_t      *query = ares_llist_node_claim(node);

    query->node_all_queries = NULL;
    query->callback(query->arg, ARES_EDESTRUCTION, 0, NULL);
    ares_free_query(query);

    node = next;
  }

  ares_queue_notify_empty(channel);

#ifndef NDEBUG
  /* Freeing the query should remove it from all the lists in which it sits,
   * so all query lists should be empty now.
   */
  assert(ares_llist_len(channel->all_queries) == 0);
  assert(ares_htable_szvp_num_keys(channel->queries_by_qid) == 0);
  assert(ares_slist_len(channel->queries_by_timeout) == 0);
#endif

  ares_destroy_servers_state(channel);

#ifndef NDEBUG
  assert(ares_htable_asvp_num_keys(channel->connnode_by_socket) == 0);
#endif

  /* No more callbacks will be triggered after this point, unlock */
  ares_channel_unlock(channel);

  /* Shut down the event thread */
  if (channel->optmask & ARES_OPT_EVENT_THREAD) {
    ares_event_thread_destroy(channel);
  }

  if (channel->domains) {
    for (i = 0; i < channel->ndomains; i++) {
      ares_free(channel->domains[i]);
    }
    ares_free(channel->domains);
  }

  ares_llist_destroy(channel->all_queries);
  ares_slist_destroy(channel->queries_by_timeout);
  ares_htable_szvp_destroy(channel->queries_by_qid);
  ares_htable_asvp_destroy(channel->connnode_by_socket);

  ares_free(channel->sortlist);
  ares_free(channel->lookups);
  ares_free(channel->resolvconf_path);
  ares_free(channel->hosts_path);
  ares_destroy_rand_state(channel->rand_state);

  ares_hosts_file_destroy(channel->hf);

  ares_qcache_destroy(channel->qcache);

  ares_channel_threading_destroy(channel);

  ares_free(channel);
}

void ares_destroy_server(ares_server_t *server)
{
  if (server == NULL) {
    return; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  ares_close_sockets(server);
  ares_llist_destroy(server->connections);
  ares_free(server);
}

void ares_destroy_servers_state(ares_channel_t *channel)
{
  ares_slist_node_t *node;

  while ((node = ares_slist_node_first(channel->servers)) != NULL) {
    ares_server_t *server = ares_slist_node_claim(node);
    ares_destroy_server(server);
  }

  ares_slist_destroy(channel->servers);
  channel->servers = NULL;
}
