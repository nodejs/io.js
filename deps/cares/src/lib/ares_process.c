/* MIT License
 *
 * Copyright (c) 1998 Massachusetts Institute of Technology
 * Copyright (c) 2010 Daniel Stenberg
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

#ifdef HAVE_STRINGS_H
#  include <strings.h>
#endif
#ifdef HAVE_SYS_IOCTL_H
#  include <sys/ioctl.h>
#endif
#ifdef NETWARE
#  include <sys/filio.h>
#endif
#ifdef HAVE_STDINT_H
#  include <stdint.h>
#endif

#include <assert.h>
#include <fcntl.h>
#include <limits.h>


static void          timeadd(ares_timeval_t *now, size_t millisecs);
static ares_status_t process_write(ares_channel_t *channel,
                                   ares_socket_t   write_fd);
static ares_status_t process_read(ares_channel_t       *channel,
                                  ares_socket_t         read_fd,
                                  const ares_timeval_t *now);
static ares_status_t process_timeouts(ares_channel_t       *channel,
                                      const ares_timeval_t *now);
static ares_status_t process_answer(ares_channel_t      *channel,
                                    const unsigned char *abuf, size_t alen,
                                    ares_conn_t          *conn,
                                    const ares_timeval_t *now);
static void handle_conn_error(ares_conn_t *conn, ares_bool_t critical_failure,
                              ares_status_t failure_status);
static ares_bool_t same_questions(const ares_query_t      *query,
                                  const ares_dns_record_t *arec);
static void        end_query(ares_channel_t *channel, ares_server_t *server,
                             ares_query_t *query, ares_status_t status,
                             const ares_dns_record_t *dnsrec);

static void        ares_query_remove_from_conn(ares_query_t *query)
{
  /* If its not part of a connection, it can't be tracked for timeouts either */
  ares_slist_node_destroy(query->node_queries_by_timeout);
  ares_llist_node_destroy(query->node_queries_to_conn);
  query->node_queries_by_timeout = NULL;
  query->node_queries_to_conn    = NULL;
  query->conn                    = NULL;
}

/* Invoke the server state callback after a success or failure */
static void invoke_server_state_cb(const ares_server_t *server,
                                   ares_bool_t success, int flags)
{
  const ares_channel_t *channel = server->channel;
  ares_buf_t           *buf;
  ares_status_t         status;
  char                 *server_string;

  if (channel->server_state_cb == NULL) {
    return;
  }

  buf = ares_buf_create();
  if (buf == NULL) {
    return; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  status = ares_get_server_addr(server, buf);
  if (status != ARES_SUCCESS) {
    ares_buf_destroy(buf); /* LCOV_EXCL_LINE: OutOfMemory */
    return;                /* LCOV_EXCL_LINE: OutOfMemory */
  }

  server_string = ares_buf_finish_str(buf, NULL);
  buf           = NULL;
  if (server_string == NULL) {
    return; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  channel->server_state_cb(server_string, success, flags,
                           channel->server_state_cb_data);
  ares_free(server_string);
}

static void server_increment_failures(ares_server_t *server,
                                      ares_bool_t    used_tcp)
{
  ares_slist_node_t    *node;
  const ares_channel_t *channel = server->channel;
  ares_timeval_t        next_retry_time;

  node = ares_slist_node_find(channel->servers, server);
  if (node == NULL) {
    return; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  server->consec_failures++;
  ares_slist_node_reinsert(node);

  ares_tvnow(&next_retry_time);
  timeadd(&next_retry_time, channel->server_retry_delay);
  server->next_retry_time = next_retry_time;

  invoke_server_state_cb(server, ARES_FALSE,
                         used_tcp == ARES_TRUE ? ARES_SERV_STATE_TCP
                                               : ARES_SERV_STATE_UDP);
}

static void server_set_good(ares_server_t *server, ares_bool_t used_tcp)
{
  ares_slist_node_t    *node;
  const ares_channel_t *channel = server->channel;

  node = ares_slist_node_find(channel->servers, server);
  if (node == NULL) {
    return; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  if (server->consec_failures > 0) {
    server->consec_failures = 0;
    ares_slist_node_reinsert(node);
  }

  server->next_retry_time.sec  = 0;
  server->next_retry_time.usec = 0;

  invoke_server_state_cb(server, ARES_TRUE,
                         used_tcp == ARES_TRUE ? ARES_SERV_STATE_TCP
                                               : ARES_SERV_STATE_UDP);
}

/* return true if now is exactly check time or later */
ares_bool_t ares_timedout(const ares_timeval_t *now,
                          const ares_timeval_t *check)
{
  ares_int64_t secs = (now->sec - check->sec);

  if (secs > 0) {
    return ARES_TRUE; /* yes, timed out */
  }
  if (secs < 0) {
    return ARES_FALSE; /* nope, not timed out */
  }

  /* if the full seconds were identical, check the sub second parts */
  return ((ares_int64_t)now->usec - (ares_int64_t)check->usec) >= 0
           ? ARES_TRUE
           : ARES_FALSE;
}

/* add the specific number of milliseconds to the time in the first argument */
static void timeadd(ares_timeval_t *now, size_t millisecs)
{
  now->sec  += (ares_int64_t)millisecs / 1000;
  now->usec += (unsigned int)((millisecs % 1000) * 1000);

  if (now->usec >= 1000000) {
    now->sec  += now->usec / 1000000;
    now->usec %= 1000000;
  }
}

static ares_status_t ares_process_fds_nolock(ares_channel_t         *channel,
                                             const ares_fd_events_t *events,
                                             size_t nevents, unsigned int flags)
{
  ares_timeval_t now;
  size_t         i;
  ares_status_t  status = ARES_SUCCESS;

  if (channel == NULL || (events == NULL && nevents != 0)) {
    return ARES_EFORMERR; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  ares_tvnow(&now);

  /* Process write events */
  for (i = 0; i < nevents; i++) {
    if (events[i].fd == ARES_SOCKET_BAD ||
        !(events[i].events & ARES_FD_EVENT_WRITE)) {
      continue;
    }
    status = process_write(channel, events[i].fd);
    /* We only care about ENOMEM, anything else is handled via connection
     * retries, etc */
    if (status == ARES_ENOMEM) {
      goto done;
    }
  }

  /* Process read events */
  for (i = 0; i < nevents; i++) {
    if (events[i].fd == ARES_SOCKET_BAD ||
        !(events[i].events & ARES_FD_EVENT_READ)) {
      continue;
    }
    status = process_read(channel, events[i].fd, &now);
    if (status == ARES_ENOMEM) {
      goto done;
    }
  }

  if (!(flags & ARES_PROCESS_FLAG_SKIP_NON_FD)) {
    ares_check_cleanup_conns(channel);
    status = process_timeouts(channel, &now);
    if (status == ARES_ENOMEM) {
      goto done;
    }
  }

done:
  if (status == ARES_ENOMEM) {
    return ARES_ENOMEM;
  }
  return ARES_SUCCESS;
}

ares_status_t ares_process_fds(ares_channel_t         *channel,
                               const ares_fd_events_t *events, size_t nevents,
                               unsigned int flags)
{
  ares_status_t status;

  if (channel == NULL) {
    return ARES_EFORMERR;
  }

  ares_channel_lock(channel);
  status = ares_process_fds_nolock(channel, events, nevents, flags);
  ares_channel_unlock(channel);
  return status;
}

void ares_process_fd(ares_channel_t *channel, ares_socket_t read_fd,
                     ares_socket_t write_fd)
{
  ares_fd_events_t events[2];
  size_t           nevents = 0;

  memset(events, 0, sizeof(events));

  if (read_fd != ARES_SOCKET_BAD) {
    nevents++;
    events[nevents - 1].fd      = read_fd;
    events[nevents - 1].events |= ARES_FD_EVENT_READ;
  }

  if (write_fd != ARES_SOCKET_BAD) {
    if (write_fd != read_fd) {
      nevents++;
    }
    events[nevents - 1].fd      = write_fd;
    events[nevents - 1].events |= ARES_FD_EVENT_WRITE;
  }

  ares_process_fds(channel, events, nevents, ARES_PROCESS_FLAG_NONE);
}

static ares_socket_t *channel_socket_list(const ares_channel_t *channel,
                                          size_t               *num)
{
  ares_slist_node_t *snode;
  ares_array_t      *arr = ares_array_create(sizeof(ares_socket_t), NULL);

  *num = 0;

  if (arr == NULL) {
    return NULL; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  for (snode = ares_slist_node_first(channel->servers); snode != NULL;
       snode = ares_slist_node_next(snode)) {
    ares_server_t     *server = ares_slist_node_val(snode);
    ares_llist_node_t *node;

    for (node = ares_llist_node_first(server->connections); node != NULL;
         node = ares_llist_node_next(node)) {
      const ares_conn_t *conn = ares_llist_node_val(node);
      ares_socket_t     *sptr;
      ares_status_t      status;

      if (conn->fd == ARES_SOCKET_BAD) {
        continue;
      }

      status = ares_array_insert_last((void **)&sptr, arr);
      if (status != ARES_SUCCESS) {
        ares_array_destroy(arr); /* LCOV_EXCL_LINE: OutOfMemory */
        return NULL;             /* LCOV_EXCL_LINE: OutOfMemory */
      }
      *sptr = conn->fd;
    }
  }

  return ares_array_finish(arr, num);
}

/* Something interesting happened on the wire, or there was a timeout.
 * See what's up and respond accordingly.
 */
void ares_process(ares_channel_t *channel, fd_set *read_fds, fd_set *write_fds)
{
  size_t            i;
  size_t            num_sockets;
  ares_socket_t    *socketlist;
  ares_fd_events_t *events  = NULL;
  size_t            nevents = 0;

  if (channel == NULL) {
    return;
  }

  ares_channel_lock(channel);

  /* There is no good way to iterate across an fd_set, instead we must pull a
   * list of all known fds, and iterate across that checking against the fd_set.
   */
  socketlist = channel_socket_list(channel, &num_sockets);

  /* Lets create an events array, maximum number is the number of sockets in
   * the list, so we'll use that and just track entries with nevents */
  if (num_sockets) {
    events = ares_malloc_zero(sizeof(*events) * num_sockets);
    if (events == NULL) {
      goto done;
    }
  }

  for (i = 0; i < num_sockets; i++) {
    ares_bool_t had_read = ARES_FALSE;
    if (read_fds && FD_ISSET(socketlist[i], read_fds)) {
      nevents++;
      events[nevents - 1].fd      = socketlist[i];
      events[nevents - 1].events |= ARES_FD_EVENT_READ;
      had_read                    = ARES_TRUE;
    }
    if (write_fds && FD_ISSET(socketlist[i], write_fds)) {
      if (!had_read) {
        nevents++;
      }
      events[nevents - 1].fd      = socketlist[i];
      events[nevents - 1].events |= ARES_FD_EVENT_WRITE;
    }
  }

done:
  ares_process_fds_nolock(channel, events, nevents, ARES_PROCESS_FLAG_NONE);
  ares_free(events);
  ares_free(socketlist);
  ares_channel_unlock(channel);
}

static ares_status_t process_write(ares_channel_t *channel,
                                   ares_socket_t   write_fd)
{
  ares_conn_t  *conn = ares_conn_from_fd(channel, write_fd);
  ares_status_t status;

  if (conn == NULL) {
    return ARES_SUCCESS;
  }

  /* Mark as connected if we got here and TFO Initial not set */
  if (!(conn->flags & ARES_CONN_FLAG_TFO_INITIAL)) {
    conn->state_flags |= ARES_CONN_STATE_CONNECTED;
  }

  status = ares_conn_flush(conn);
  if (status != ARES_SUCCESS) {
    handle_conn_error(conn, ARES_TRUE, status);
  }
  return status;
}

void ares_process_pending_write(ares_channel_t *channel)
{
  ares_slist_node_t *node;

  if (channel == NULL) {
    return;
  }

  ares_channel_lock(channel);
  if (!channel->notify_pending_write) {
    ares_channel_unlock(channel);
    return;
  }

  /* Set as untriggerd before calling into ares_conn_flush(), this is
   * because its possible ares_conn_flush() might cause additional data to
   * be enqueued if there is some form of exception so it will need to recurse.
   */
  channel->notify_pending_write = ARES_FALSE;

  for (node = ares_slist_node_first(channel->servers); node != NULL;
       node = ares_slist_node_next(node)) {
    ares_server_t *server = ares_slist_node_val(node);
    ares_conn_t   *conn   = server->tcp_conn;
    ares_status_t  status;

    if (conn == NULL) {
      continue;
    }

    /* Enqueue any pending data if there is any */
    status = ares_conn_flush(conn);
    if (status != ARES_SUCCESS) {
      handle_conn_error(conn, ARES_TRUE, status);
    }
  }

  ares_channel_unlock(channel);
}

static ares_status_t read_conn_packets(ares_conn_t *conn)
{
  ares_bool_t           read_again;
  ares_conn_err_t       err;
  const ares_channel_t *channel = conn->server->channel;

  do {
    size_t         count;
    size_t         len = 65535;
    unsigned char *ptr;
    size_t         start_len = ares_buf_len(conn->in_buf);

    /* If UDP, lets write out a placeholder for the length indicator */
    if (!(conn->flags & ARES_CONN_FLAG_TCP) &&
        ares_buf_append_be16(conn->in_buf, 0) != ARES_SUCCESS) {
      handle_conn_error(conn, ARES_FALSE /* not critical to connection */,
                        ARES_SUCCESS);
      return ARES_ENOMEM;
    }

    /* Get a buffer of sufficient size */
    ptr = ares_buf_append_start(conn->in_buf, &len);

    if (ptr == NULL) {
      handle_conn_error(conn, ARES_FALSE /* not critical to connection */,
                        ARES_SUCCESS);
      return ARES_ENOMEM;
    }

    /* Read from socket */
    err = ares_conn_read(conn, ptr, len, &count);

    if (err != ARES_CONN_ERR_SUCCESS) {
      ares_buf_append_finish(conn->in_buf, 0);
      if (!(conn->flags & ARES_CONN_FLAG_TCP)) {
        ares_buf_set_length(conn->in_buf, start_len);
      }
      break;
    }

    /* Record amount of data read */
    ares_buf_append_finish(conn->in_buf, count);

    /* Only loop if sockets support non-blocking operation, and are using UDP
     * or are using TCP and read the maximum buffer size */
    read_again = ARES_FALSE;
    if (channel->sock_funcs.flags & ARES_SOCKFUNC_FLAG_NONBLOCKING &&
        (!(conn->flags & ARES_CONN_FLAG_TCP) || count == len)) {
      read_again = ARES_TRUE;
    }

    /* If UDP, overwrite length */
    if (!(conn->flags & ARES_CONN_FLAG_TCP)) {
      len = ares_buf_len(conn->in_buf);
      ares_buf_set_length(conn->in_buf, start_len);
      ares_buf_append_be16(conn->in_buf, (unsigned short)count);
      ares_buf_set_length(conn->in_buf, len);
    }
    /* Try to read again only if *we* set up the socket, otherwise it may be
     * a blocking socket and would cause recvfrom to hang. */
  } while (read_again);

  if (err != ARES_CONN_ERR_SUCCESS && err != ARES_CONN_ERR_WOULDBLOCK) {
    handle_conn_error(conn, ARES_TRUE, ARES_ECONNREFUSED);
    return ARES_ECONNREFUSED;
  }

  return ARES_SUCCESS;
}

static ares_status_t read_answers(ares_conn_t *conn, const ares_timeval_t *now)
{
  ares_status_t   status;
  ares_channel_t *channel = conn->server->channel;

  /* Process all queued answers */
  while (1) {
    unsigned short       dns_len  = 0;
    const unsigned char *data     = NULL;
    size_t               data_len = 0;

    /* Tag so we can roll back */
    ares_buf_tag(conn->in_buf);

    /* Read length indicator */
    status = ares_buf_fetch_be16(conn->in_buf, &dns_len);
    if (status != ARES_SUCCESS) {
      ares_buf_tag_rollback(conn->in_buf);
      break;
    }

    /* Not enough data for a full response yet */
    status = ares_buf_consume(conn->in_buf, dns_len);
    if (status != ARES_SUCCESS) {
      ares_buf_tag_rollback(conn->in_buf);
      break;
    }

    /* Can't fail except for misuse */
    data = ares_buf_tag_fetch(conn->in_buf, &data_len);
    if (data == NULL || data_len < 2) {
      ares_buf_tag_clear(conn->in_buf);
      break;
    }

    /* Strip off 2 bytes length */
    data     += 2;
    data_len -= 2;

    /* We finished reading this answer; process it */
    status = process_answer(channel, data, data_len, conn, now);
    if (status != ARES_SUCCESS) {
      handle_conn_error(conn, ARES_TRUE, status);
      return status;
    }

    /* Since we processed the answer, clear the tag so space can be reclaimed */
    ares_buf_tag_clear(conn->in_buf);
  }
  return status;
}

static ares_status_t process_read(ares_channel_t       *channel,
                                  ares_socket_t         read_fd,
                                  const ares_timeval_t *now)
{
  ares_conn_t  *conn = ares_conn_from_fd(channel, read_fd);
  ares_status_t status;

  if (conn == NULL) {
    return ARES_SUCCESS;
  }

  /* TODO: There might be a potential issue here where there was a read that
   *       read some data, then looped and read again and got a disconnect.
   *       Right now, that would cause a resend instead of processing the data
   *       we have.  This is fairly unlikely to occur due to only looping if
   *       a full buffer of 65535 bytes was read. */
  status = read_conn_packets(conn);

  if (status != ARES_SUCCESS) {
    return status;
  }

  return read_answers(conn, now);
}

/* If any queries have timed out, note the timeout and move them on. */
static ares_status_t process_timeouts(ares_channel_t       *channel,
                                      const ares_timeval_t *now)
{
  ares_slist_node_t *node;
  ares_status_t      status = ARES_SUCCESS;

  /* Just keep popping off the first as this list will re-sort as things come
   * and go.  We don't want to try to rely on 'next' as some operation might
   * cause a cleanup of that pointer and would become invalid */
  while ((node = ares_slist_node_first(channel->queries_by_timeout)) != NULL) {
    ares_query_t *query = ares_slist_node_val(node);
    ares_conn_t  *conn;

    /* Since this is sorted, as soon as we hit a query that isn't timed out,
     * break */
    if (!ares_timedout(now, &query->timeout)) {
      break;
    }

    query->timeouts++;

    conn = query->conn;
    server_increment_failures(conn->server, query->using_tcp);
    status = ares_requeue_query(query, now, ARES_ETIMEOUT, ARES_TRUE, NULL);
    if (status == ARES_ENOMEM) {
      goto done;
    }
  }
done:
  if (status == ARES_ENOMEM) {
    return ARES_ENOMEM;
  }
  return ARES_SUCCESS;
}

static ares_status_t rewrite_without_edns(ares_query_t *query)
{
  ares_status_t status = ARES_SUCCESS;
  size_t        i;
  ares_bool_t   found_opt_rr = ARES_FALSE;

  /* Find and remove the OPT RR record */
  for (i = 0; i < ares_dns_record_rr_cnt(query->query, ARES_SECTION_ADDITIONAL);
       i++) {
    const ares_dns_rr_t *rr;
    rr = ares_dns_record_rr_get(query->query, ARES_SECTION_ADDITIONAL, i);
    if (ares_dns_rr_get_type(rr) == ARES_REC_TYPE_OPT) {
      ares_dns_record_rr_del(query->query, ARES_SECTION_ADDITIONAL, i);
      found_opt_rr = ARES_TRUE;
      break;
    }
  }

  if (!found_opt_rr) {
    status = ARES_EFORMERR;
    goto done;
  }

done:
  return status;
}

/* Handle an answer from a server. This must NEVER cleanup the
 * server connection! Return something other than ARES_SUCCESS to cause
 * the connection to be terminated after this call. */
static ares_status_t process_answer(ares_channel_t      *channel,
                                    const unsigned char *abuf, size_t alen,
                                    ares_conn_t          *conn,
                                    const ares_timeval_t *now)
{
  ares_query_t      *query;
  /* Cache these as once ares_send_query() gets called, it may end up
   * invalidating the connection all-together */
  ares_server_t     *server  = conn->server;
  ares_dns_record_t *rdnsrec = NULL;
  ares_status_t      status;
  ares_bool_t        is_cached = ARES_FALSE;

  /* UDP can have 0-byte messages, drop them to the ground */
  if (alen == 0) {
    return ARES_SUCCESS;
  }

  /* Parse the response */
  status = ares_dns_parse(abuf, alen, 0, &rdnsrec);
  if (status != ARES_SUCCESS) {
    /* Malformations are never accepted */
    status = ARES_EBADRESP;
    goto cleanup;
  }

  /* Find the query corresponding to this packet. The queries are
   * hashed/bucketed by query id, so this lookup should be quick.
   */
  query = ares_htable_szvp_get_direct(channel->queries_by_qid,
                                      ares_dns_record_get_id(rdnsrec));
  if (!query) {
    /* We may have stopped listening for this query, that's ok */
    status = ARES_SUCCESS;
    goto cleanup;
  }

  /* Both the query id and the questions must be the same. We will drop any
   * replies that aren't for the same query as this is considered invalid. */
  if (!same_questions(query, rdnsrec)) {
    /* Possible qid conflict due to delayed response, that's ok */
    status = ARES_SUCCESS;
    goto cleanup;
  }

  /* Validate DNS cookie in response. This function may need to requeue the
   * query. */
  if (ares_cookie_validate(query, rdnsrec, conn, now) != ARES_SUCCESS) {
    /* Drop response and return */
    status = ARES_SUCCESS;
    goto cleanup;
  }

  /* At this point we know we've received an answer for this query, so we should
   * remove it from the connection's queue so we can possibly invalidate the
   * connection. Delay cleaning up the connection though as we may enqueue
   * something new.  */
  ares_llist_node_destroy(query->node_queries_to_conn);
  query->node_queries_to_conn = NULL;

  /* If we use EDNS and server answers with FORMERR without an OPT RR, the
   * protocol extension is not understood by the responder. We must retry the
   * query without EDNS enabled. */
  if (ares_dns_record_get_rcode(rdnsrec) == ARES_RCODE_FORMERR &&
      ares_dns_get_opt_rr_const(query->query) != NULL &&
      ares_dns_get_opt_rr_const(rdnsrec) == NULL) {
    status = rewrite_without_edns(query);
    if (status != ARES_SUCCESS) {
      end_query(channel, server, query, status, NULL);
      goto cleanup;
    }

    /* Send to same server */
    ares_send_query(server, query, now);
    status = ARES_SUCCESS;
    goto cleanup;
  }

  /* If we got a truncated UDP packet and are not ignoring truncation,
   * don't accept the packet, and switch the query to TCP if we hadn't
   * done so already.
   */
  if (ares_dns_record_get_flags(rdnsrec) & ARES_FLAG_TC &&
      !(conn->flags & ARES_CONN_FLAG_TCP) &&
      !(channel->flags & ARES_FLAG_IGNTC)) {
    query->using_tcp = ARES_TRUE;
    ares_send_query(NULL, query, now);
    status = ARES_SUCCESS; /* Switched to TCP is ok */
    goto cleanup;
  }

  /* If we aren't passing through all error packets, discard packets
   * with SERVFAIL, NOTIMP, or REFUSED response codes.
   */
  if (!(channel->flags & ARES_FLAG_NOCHECKRESP)) {
    ares_dns_rcode_t rcode = ares_dns_record_get_rcode(rdnsrec);
    if (rcode == ARES_RCODE_SERVFAIL || rcode == ARES_RCODE_NOTIMP ||
        rcode == ARES_RCODE_REFUSED) {
      switch (rcode) {
        case ARES_RCODE_SERVFAIL:
          status = ARES_ESERVFAIL;
          break;
        case ARES_RCODE_NOTIMP:
          status = ARES_ENOTIMP;
          break;
        case ARES_RCODE_REFUSED:
          status = ARES_EREFUSED;
          break;
        default:
          break;
      }

      server_increment_failures(server, query->using_tcp);
      ares_requeue_query(query, now, status, ARES_TRUE, rdnsrec);

      /* Should any of these cause a connection termination?
       * Maybe SERVER_FAILURE? */
      status = ARES_SUCCESS;
      goto cleanup;
    }
  }

  /* If cache insertion was successful, it took ownership.  We ignore
   * other cache insertion failures. */
  if (ares_qcache_insert(channel, now, query, rdnsrec) == ARES_SUCCESS) {
    is_cached = ARES_TRUE;
  }

  server_set_good(server, query->using_tcp);
  end_query(channel, server, query, ARES_SUCCESS, rdnsrec);

  status = ARES_SUCCESS;

cleanup:
  /* Don't cleanup the cached pointer to the dns response */
  if (!is_cached) {
    ares_dns_record_destroy(rdnsrec);
  }

  return status;
}

static void handle_conn_error(ares_conn_t *conn, ares_bool_t critical_failure,
                              ares_status_t failure_status)
{
  ares_server_t *server = conn->server;

  /* Increment failures first before requeue so it is unlikely to requeue
   * to the same server */
  if (critical_failure) {
    server_increment_failures(
      server, (conn->flags & ARES_CONN_FLAG_TCP) ? ARES_TRUE : ARES_FALSE);
  }

  /* This will requeue any connections automatically */
  ares_close_connection(conn, failure_status);
}

ares_status_t ares_requeue_query(ares_query_t *query, const ares_timeval_t *now,
                                 ares_status_t            status,
                                 ares_bool_t              inc_try_count,
                                 const ares_dns_record_t *dnsrec)
{
  ares_channel_t *channel   = query->channel;
  size_t          max_tries = ares_slist_len(channel->servers) * channel->tries;

  ares_query_remove_from_conn(query);

  if (status != ARES_SUCCESS) {
    query->error_status = status;
  }

  if (inc_try_count) {
    query->try_count++;
  }

  if (query->try_count < max_tries && !query->no_retries) {
    return ares_send_query(NULL, query, now);
  }

  /* If we are here, all attempts to perform query failed. */
  if (query->error_status == ARES_SUCCESS) {
    query->error_status = ARES_ETIMEOUT;
  }

  end_query(channel, NULL, query, query->error_status, dnsrec);
  return ARES_ETIMEOUT;
}

/*! Count the number of servers that share the same highest priority (lowest
 *  consecutive failures).  Since they are sorted in priority order, we just
 *  stop when the consecutive failure count changes. Used for random selection
 *  of good servers. */
static size_t count_highest_prio_servers(const ares_channel_t *channel)
{
  ares_slist_node_t *node;
  size_t             cnt                  = 0;
  size_t             last_consec_failures = SIZE_MAX;

  for (node = ares_slist_node_first(channel->servers); node != NULL;
       node = ares_slist_node_next(node)) {
    const ares_server_t *server = ares_slist_node_val(node);

    if (last_consec_failures != SIZE_MAX &&
        last_consec_failures < server->consec_failures) {
      break;
    }

    last_consec_failures = server->consec_failures;
    cnt++;
  }

  return cnt;
}

/* Pick a random *best* server from the list, we first get a random number in
 * the range of the number of *best* servers, then scan until we find that
 * server in the list */
static ares_server_t *ares_random_server(ares_channel_t *channel)
{
  unsigned char      c;
  size_t             cnt;
  size_t             idx;
  ares_slist_node_t *node;
  size_t             num_servers = count_highest_prio_servers(channel);

  /* Silence coverity, not possible */
  if (num_servers == 0) {
    return NULL;
  }

  ares_rand_bytes(channel->rand_state, &c, 1);

  cnt = c;
  idx = cnt % num_servers;

  cnt = 0;
  for (node = ares_slist_node_first(channel->servers); node != NULL;
       node = ares_slist_node_next(node)) {
    if (cnt == idx) {
      return ares_slist_node_val(node);
    }

    cnt++;
  }

  return NULL;
}

static void server_probe_cb(void *arg, ares_status_t status, size_t timeouts,
                            const ares_dns_record_t *dnsrec)
{
  (void)arg;
  (void)status;
  (void)timeouts;
  (void)dnsrec;
  /* Nothing to do, the logic internally will handle success/fail of this */
}

/* Determine if we should probe a downed server */
static void ares_probe_failed_server(ares_channel_t      *channel,
                                     const ares_server_t *server,
                                     const ares_query_t  *query)
{
  const ares_server_t *last_server = ares_slist_last_val(channel->servers);
  unsigned short       r;
  ares_timeval_t       now;
  ares_slist_node_t   *node;
  ares_server_t       *probe_server = NULL;

  /* If no servers have failures, or we're not configured with a server retry
   * chance, then nothing to probe */
  if ((last_server != NULL && last_server->consec_failures == 0) ||
      channel->server_retry_chance == 0) {
    return;
  }

  /* Generate a random value to decide whether to retry a failed server. The
   * probability to use is 1/channel->server_retry_chance, rounded up to a
   * precision of 1/2^B where B is the number of bits in the random value.
   * We use an unsigned short for the random value for increased precision.
   */
  ares_rand_bytes(channel->rand_state, (unsigned char *)&r, sizeof(r));
  if (r % channel->server_retry_chance != 0) {
    return;
  }

  /* Select the first server with failures to retry that has passed the retry
   * timeout and doesn't already have a pending probe */
  ares_tvnow(&now);
  for (node = ares_slist_node_first(channel->servers); node != NULL;
       node = ares_slist_node_next(node)) {
    ares_server_t *node_val = ares_slist_node_val(node);
    if (node_val != NULL && node_val->consec_failures > 0 &&
        !node_val->probe_pending &&
        ares_timedout(&now, &node_val->next_retry_time)) {
      probe_server = node_val;
      break;
    }
  }

  /* Either nothing to probe or the query was enqueud to the same server
   * we were going to probe. Do nothing. */
  if (probe_server == NULL || server == probe_server) {
    return;
  }

  /* Enqueue an identical query onto the specified server without honoring
   * the cache or allowing retries.  We want to make sure it only attempts to
   * use the server in question */
  probe_server->probe_pending = ARES_TRUE;
  ares_send_nolock(channel, probe_server,
                   ARES_SEND_FLAG_NOCACHE | ARES_SEND_FLAG_NORETRY,
                   query->query, server_probe_cb, NULL, NULL);
}

static size_t ares_calc_query_timeout(const ares_query_t   *query,
                                      const ares_server_t  *server,
                                      const ares_timeval_t *now)
{
  const ares_channel_t *channel  = query->channel;
  size_t                timeout  = ares_metrics_server_timeout(server, now);
  size_t                timeplus = timeout;
  size_t                rounds;
  size_t                num_servers = ares_slist_len(channel->servers);

  if (num_servers == 0) {
    return 0; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  /* For each trip through the entire server list, we want to double the
   * retry from the last retry */
  rounds = (query->try_count / num_servers);
  if (rounds > 0) {
    timeplus <<= rounds;
  }

  if (channel->maxtimeout && timeplus > channel->maxtimeout) {
    timeplus = channel->maxtimeout;
  }

  /* Add some jitter to the retry timeout.
   *
   * Jitter is needed in situation when resolve requests are performed
   * simultaneously from multiple hosts and DNS server throttle these requests.
   * Adding randomness allows to avoid synchronisation of retries.
   *
   * Value of timeplus adjusted randomly to the range [0.5 * timeplus,
   * timeplus].
   */
  if (rounds > 0) {
    unsigned short r;
    float          delta_multiplier;

    ares_rand_bytes(channel->rand_state, (unsigned char *)&r, sizeof(r));
    delta_multiplier  = ((float)r / USHRT_MAX) * 0.5f;
    timeplus         -= (size_t)((float)timeplus * delta_multiplier);
  }

  /* We want explicitly guarantee that timeplus is greater or equal to timeout
   * specified in channel options. */
  if (timeplus < timeout) {
    timeplus = timeout;
  }

  return timeplus;
}

static ares_conn_t *ares_fetch_connection(const ares_channel_t *channel,
                                          ares_server_t        *server,
                                          const ares_query_t   *query)
{
  ares_llist_node_t *node;
  ares_conn_t       *conn;

  if (query->using_tcp) {
    return server->tcp_conn;
  }

  /* Fetch existing UDP connection */
  node = ares_llist_node_first(server->connections);
  if (node == NULL) {
    return NULL;
  }

  conn = ares_llist_node_val(node);
  /* Not UDP, skip */
  if (conn->flags & ARES_CONN_FLAG_TCP) {
    return NULL;
  }

  /* Used too many times */
  if (channel->udp_max_queries > 0 &&
      conn->total_queries >= channel->udp_max_queries) {
    return NULL;
  }

  return conn;
}

static ares_status_t ares_conn_query_write(ares_conn_t          *conn,
                                           ares_query_t         *query,
                                           const ares_timeval_t *now)
{
  ares_server_t  *server  = conn->server;
  ares_channel_t *channel = server->channel;
  ares_status_t   status;

  status = ares_cookie_apply(query->query, conn, now);
  if (status != ARES_SUCCESS) {
    return status;
  }

  /* We write using the TCP format even for UDP, we just strip the length
   * before putting on the wire */
  status = ares_dns_write_buf_tcp(query->query, conn->out_buf);
  if (status != ARES_SUCCESS) {
    return status;
  }

  /* Not pending a TFO write and not connected, so we can't even try to
   * write until we get a signal */
  if (conn->flags & ARES_CONN_FLAG_TCP &&
      !(conn->state_flags & ARES_CONN_STATE_CONNECTED) &&
      !(conn->flags & ARES_CONN_FLAG_TFO_INITIAL)) {
    return ARES_SUCCESS;
  }

  /* Delay actual write if possible (TCP only, and only if callback
   * configured) */
  if (channel->notify_pending_write_cb && !channel->notify_pending_write &&
      conn->flags & ARES_CONN_FLAG_TCP) {
    channel->notify_pending_write = ARES_TRUE;
    channel->notify_pending_write_cb(channel->notify_pending_write_cb_data);
    return ARES_SUCCESS;
  }

  /* Unfortunately we need to write right away and can't aggregate multiple
   * queries into a single write. */
  return ares_conn_flush(conn);
}

ares_status_t ares_send_query(ares_server_t *requested_server,
                              ares_query_t *query, const ares_timeval_t *now)
{
  ares_channel_t *channel = query->channel;
  ares_server_t  *server;
  ares_conn_t    *conn;
  size_t          timeplus;
  ares_status_t   status;
  ares_bool_t     probe_downed_server = ARES_TRUE;


  /* Choose the server to send the query to */
  if (requested_server != NULL) {
    server = requested_server;
  } else {
    /* If rotate is turned on, do a random selection */
    if (channel->rotate) {
      server = ares_random_server(channel);
    } else {
      /* First server in list */
      server = ares_slist_first_val(channel->servers);
    }
  }

  if (server == NULL) {
    end_query(channel, server, query, ARES_ENOSERVER /* ? */, NULL);
    return ARES_ENOSERVER;
  }

  /* If a query is directed to a specific query, or the server chosen has
   * failures, or the query is being retried, don't probe for downed servers */
  if (requested_server != NULL || server->consec_failures > 0 ||
      query->try_count != 0) {
    probe_downed_server = ARES_FALSE;
  }

  conn = ares_fetch_connection(channel, server, query);
  if (conn == NULL) {
    status = ares_open_connection(&conn, channel, server, query->using_tcp);
    switch (status) {
      /* Good result, continue on */
      case ARES_SUCCESS:
        break;

      /* These conditions are retryable as they are server-specific
       * error codes */
      case ARES_ECONNREFUSED:
      case ARES_EBADFAMILY:
        server_increment_failures(server, query->using_tcp);
        return ares_requeue_query(query, now, status, ARES_TRUE, NULL);

      /* Anything else is not retryable, likely ENOMEM */
      default:
        end_query(channel, server, query, status, NULL);
        return status;
    }
  }

  /* Write the query */
  status = ares_conn_query_write(conn, query, now);
  switch (status) {
    /* Good result, continue on */
    case ARES_SUCCESS:
      break;

    case ARES_ENOMEM:
      /* Not retryable */
      end_query(channel, server, query, status, NULL);
      return status;

    /* These conditions are retryable as they are server-specific
     * error codes */
    case ARES_ECONNREFUSED:
    case ARES_EBADFAMILY:
      handle_conn_error(conn, ARES_TRUE, status);
      status = ares_requeue_query(query, now, status, ARES_TRUE, NULL);
      if (status == ARES_ETIMEOUT) {
        status = ARES_ECONNREFUSED;
      }
      return status;

    default:
      server_increment_failures(server, query->using_tcp);
      status = ares_requeue_query(query, now, status, ARES_TRUE, NULL);
      return status;
  }

  timeplus = ares_calc_query_timeout(query, server, now);
  /* Keep track of queries bucketed by timeout, so we can process
   * timeout events quickly.
   */
  ares_slist_node_destroy(query->node_queries_by_timeout);
  query->ts      = *now;
  query->timeout = *now;
  timeadd(&query->timeout, timeplus);
  query->node_queries_by_timeout =
    ares_slist_insert(channel->queries_by_timeout, query);
  if (!query->node_queries_by_timeout) {
    /* LCOV_EXCL_START: OutOfMemory */
    end_query(channel, server, query, ARES_ENOMEM, NULL);
    return ARES_ENOMEM;
    /* LCOV_EXCL_STOP */
  }

  /* Keep track of queries bucketed by connection, so we can process errors
   * quickly. */
  ares_llist_node_destroy(query->node_queries_to_conn);
  query->node_queries_to_conn =
    ares_llist_insert_last(conn->queries_to_conn, query);

  if (query->node_queries_to_conn == NULL) {
    /* LCOV_EXCL_START: OutOfMemory */
    end_query(channel, server, query, ARES_ENOMEM, NULL);
    return ARES_ENOMEM;
    /* LCOV_EXCL_STOP */
  }

  query->conn = conn;
  conn->total_queries++;

  /* We just successfully enqueud a query, see if we should probe downed
   * servers. */
  if (probe_downed_server) {
    ares_probe_failed_server(channel, server, query);
  }

  return ARES_SUCCESS;
}

static ares_bool_t same_questions(const ares_query_t      *query,
                                  const ares_dns_record_t *arec)
{
  size_t                   i;
  ares_bool_t              rv      = ARES_FALSE;
  const ares_dns_record_t *qrec    = query->query;
  const ares_channel_t    *channel = query->channel;


  if (ares_dns_record_query_cnt(qrec) != ares_dns_record_query_cnt(arec)) {
    goto done;
  }

  for (i = 0; i < ares_dns_record_query_cnt(qrec); i++) {
    const char         *qname = NULL;
    const char         *aname = NULL;
    ares_dns_rec_type_t qtype;
    ares_dns_rec_type_t atype;
    ares_dns_class_t    qclass;
    ares_dns_class_t    aclass;

    if (ares_dns_record_query_get(qrec, i, &qname, &qtype, &qclass) !=
          ARES_SUCCESS ||
        qname == NULL) {
      goto done;
    }

    if (ares_dns_record_query_get(arec, i, &aname, &atype, &aclass) !=
          ARES_SUCCESS ||
        aname == NULL) {
      goto done;
    }

    if (qtype != atype || qclass != aclass) {
      goto done;
    }

    if (channel->flags & ARES_FLAG_DNS0x20 && !query->using_tcp) {
      /* NOTE: for DNS 0x20, part of the protection is to use a case-sensitive
       *       comparison of the DNS query name.  This expects the upstream DNS
       *       server to preserve the case of the name in the response packet.
       *       https://datatracker.ietf.org/doc/html/draft-vixie-dnsext-dns0x20-00
       */
      if (!ares_streq(qname, aname)) {
        goto done;
      }
    } else {
      /* without DNS0x20 use case-insensitive matching */
      if (!ares_strcaseeq(qname, aname)) {
        goto done;
      }
    }
  }

  rv = ARES_TRUE;

done:
  return rv;
}

static void ares_detach_query(ares_query_t *query)
{
  /* Remove the query from all the lists in which it is linked */
  ares_query_remove_from_conn(query);
  ares_htable_szvp_remove(query->channel->queries_by_qid, query->qid);
  ares_llist_node_destroy(query->node_all_queries);
  query->node_all_queries = NULL;
}

static void end_query(ares_channel_t *channel, ares_server_t *server,
                      ares_query_t *query, ares_status_t status,
                      const ares_dns_record_t *dnsrec)
{
  /* If we were probing for the server to come back online, lets mark it as
   * no longer being probed */
  if (server != NULL) {
    server->probe_pending = ARES_FALSE;
  }

  ares_metrics_record(query, server, status, dnsrec);

  /* Invoke the callback. */
  query->callback(query->arg, status, query->timeouts, dnsrec);
  ares_free_query(query);

  /* Check and notify if no other queries are enqueued on the channel.  This
   * must come after the callback and freeing the query for 2 reasons.
   *  1) The callback itself may enqueue a new query
   *  2) Technically the current query isn't detached until it is free()'d.
   */
  ares_queue_notify_empty(channel);
}

void ares_free_query(ares_query_t *query)
{
  ares_detach_query(query);
  /* Zero out some important stuff, to help catch bugs */
  query->callback = NULL;
  query->arg      = NULL;
  /* Deallocate the memory associated with the query */
  ares_dns_record_destroy(query->query);

  ares_free(query);
}
