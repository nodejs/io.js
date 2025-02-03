/* MIT License
 *
 * Copyright (c) Massachusetts Institute of Technology
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

void ares_conn_sock_state_cb_update(ares_conn_t            *conn,
                                    ares_conn_state_flags_t flags)
{
  ares_channel_t *channel = conn->server->channel;

  if ((conn->state_flags & ARES_CONN_STATE_CBFLAGS) != flags &&
      channel->sock_state_cb) {
    channel->sock_state_cb(channel->sock_state_cb_data, conn->fd,
                           flags & ARES_CONN_STATE_READ ? 1 : 0,
                           flags & ARES_CONN_STATE_WRITE ? 1 : 0);
  }

  conn->state_flags &= ~((unsigned int)ARES_CONN_STATE_CBFLAGS);
  conn->state_flags |= flags;
}

ares_conn_err_t ares_conn_read(ares_conn_t *conn, void *data, size_t len,
                               size_t *read_bytes)
{
  ares_channel_t *channel = conn->server->channel;
  ares_conn_err_t err;

  if (!(conn->flags & ARES_CONN_FLAG_TCP)) {
    struct sockaddr_storage sa_storage;
    ares_socklen_t          salen = sizeof(sa_storage);

    memset(&sa_storage, 0, sizeof(sa_storage));

    err =
      ares_socket_recvfrom(channel, conn->fd, ARES_FALSE, data, len, 0,
                           (struct sockaddr *)&sa_storage, &salen, read_bytes);

#ifdef HAVE_RECVFROM
    if (err == ARES_CONN_ERR_SUCCESS &&
        !ares_sockaddr_addr_eq((struct sockaddr *)&sa_storage,
                               &conn->server->addr)) {
      err = ARES_CONN_ERR_WOULDBLOCK;
    }
#endif
  } else {
    err = ares_socket_recv(channel, conn->fd, ARES_TRUE, data, len, read_bytes);
  }

  /* Toggle connected state if needed */
  if (err == ARES_CONN_ERR_SUCCESS) {
    conn->state_flags |= ARES_CONN_STATE_CONNECTED;
  }

  return err;
}

/* Use like:
 *   struct sockaddr_storage sa_storage;
 *   ares_socklen_t          salen     = sizeof(sa_storage);
 *   struct sockaddr        *sa        = (struct sockaddr *)&sa_storage;
 *   ares_conn_set_sockaddr(conn, sa, &salen);
 */
static ares_status_t ares_conn_set_sockaddr(const ares_conn_t *conn,
                                            struct sockaddr   *sa,
                                            ares_socklen_t    *salen)
{
  const ares_server_t *server = conn->server;
  unsigned short       port =
    conn->flags & ARES_CONN_FLAG_TCP ? server->tcp_port : server->udp_port;
  struct sockaddr_in  *sin;
  struct sockaddr_in6 *sin6;

  switch (server->addr.family) {
    case AF_INET:
      sin = (struct sockaddr_in *)(void *)sa;
      if (*salen < (ares_socklen_t)sizeof(*sin)) {
        return ARES_EFORMERR;
      }
      *salen = sizeof(*sin);
      memset(sin, 0, sizeof(*sin));
      sin->sin_family = AF_INET;
      sin->sin_port   = htons(port);
      memcpy(&sin->sin_addr, &server->addr.addr.addr4, sizeof(sin->sin_addr));
      return ARES_SUCCESS;
    case AF_INET6:
      sin6 = (struct sockaddr_in6 *)(void *)sa;
      if (*salen < (ares_socklen_t)sizeof(*sin6)) {
        return ARES_EFORMERR;
      }
      *salen = sizeof(*sin6);
      memset(sin6, 0, sizeof(*sin6));
      sin6->sin6_family = AF_INET6;
      sin6->sin6_port   = htons(port);
      memcpy(&sin6->sin6_addr, &server->addr.addr.addr6,
             sizeof(sin6->sin6_addr));
#ifdef HAVE_STRUCT_SOCKADDR_IN6_SIN6_SCOPE_ID
      sin6->sin6_scope_id = server->ll_scope;
#endif
      return ARES_SUCCESS;
    default:
      break;
  }

  return ARES_EBADFAMILY;
}

static ares_status_t ares_conn_set_self_ip(ares_conn_t *conn, ares_bool_t early)
{
  ares_channel_t         *channel = conn->server->channel;
  struct sockaddr_storage sa_storage;
  int                     rv;
  ares_socklen_t          len = sizeof(sa_storage);

  /* We call this twice on TFO, if we already have the IP we can go ahead and
   * skip processing */
  if (!early && conn->self_ip.family != AF_UNSPEC) {
    return ARES_SUCCESS;
  }

  memset(&sa_storage, 0, sizeof(sa_storage));

  if (channel->sock_funcs.agetsockname == NULL) {
    /* Not specified, we can still use cookies cooked with an empty self_ip */
    memset(&conn->self_ip, 0, sizeof(conn->self_ip));
    return ARES_SUCCESS;
  }
  rv = channel->sock_funcs.agetsockname(conn->fd,
                                        (struct sockaddr *)(void *)&sa_storage,
                                        &len, channel->sock_func_cb_data);
  if (rv != 0) {
    /* During TCP FastOpen, we can't get the IP this early since connect()
     * may not be called.  That's ok, we'll try again later */
    if (early && conn->flags & ARES_CONN_FLAG_TCP &&
        conn->flags & ARES_CONN_FLAG_TFO) {
      memset(&conn->self_ip, 0, sizeof(conn->self_ip));
      return ARES_SUCCESS;
    }
    return ARES_ECONNREFUSED;
  }

  if (!ares_sockaddr_to_ares_addr(&conn->self_ip, NULL,
                                  (struct sockaddr *)(void *)&sa_storage)) {
    return ARES_ECONNREFUSED;
  }

  return ARES_SUCCESS;
}

ares_conn_err_t ares_conn_write(ares_conn_t *conn, const void *data, size_t len,
                                size_t *written)
{
  ares_channel_t         *channel = conn->server->channel;
  ares_bool_t             is_tfo  = ARES_FALSE;
  ares_conn_err_t         err     = ARES_CONN_ERR_SUCCESS;
  struct sockaddr_storage sa_storage;
  ares_socklen_t          salen = 0;
  struct sockaddr        *sa    = NULL;

  *written = 0;

  /* Don't try to write if not doing initial TFO and not connected */
  if (conn->flags & ARES_CONN_FLAG_TCP &&
      !(conn->state_flags & ARES_CONN_STATE_CONNECTED) &&
      !(conn->flags & ARES_CONN_FLAG_TFO_INITIAL)) {
    return ARES_CONN_ERR_WOULDBLOCK;
  }

  /* On initial write during TFO we need to send an address */
  if (conn->flags & ARES_CONN_FLAG_TFO_INITIAL) {
    salen = sizeof(sa_storage);
    sa    = (struct sockaddr *)&sa_storage;

    conn->flags &= ~((unsigned int)ARES_CONN_FLAG_TFO_INITIAL);
    is_tfo       = ARES_TRUE;

    if (ares_conn_set_sockaddr(conn, sa, &salen) != ARES_SUCCESS) {
      return ARES_CONN_ERR_FAILURE;
    }
  }

  err = ares_socket_write(channel, conn->fd, data, len, written, sa, salen);
  if (err != ARES_CONN_ERR_SUCCESS) {
    goto done;
  }

  if (is_tfo) {
    /* If using TFO, we might not have been able to get an IP earlier, since
     * we hadn't informed the OS of the destination.  When using sendto()
     * now we have so we should be able to fetch it */
    ares_conn_set_self_ip(conn, ARES_FALSE);
    goto done;
  }

done:
  if (err == ARES_CONN_ERR_SUCCESS && len == *written) {
    /* Wrote all data, make sure we're not listening for write events unless
     * using TFO, in which case we'll need a write event to know when
     * we're connected. */
    ares_conn_sock_state_cb_update(
      conn, ARES_CONN_STATE_READ |
              (is_tfo ? ARES_CONN_STATE_WRITE : ARES_CONN_STATE_NONE));
  } else if (err == ARES_CONN_ERR_WOULDBLOCK) {
    /* Need to wait on more buffer space to write */
    ares_conn_sock_state_cb_update(conn, ARES_CONN_STATE_READ |
                                           ARES_CONN_STATE_WRITE);
  }

  return err;
}

ares_status_t ares_conn_flush(ares_conn_t *conn)
{
  const unsigned char *data;
  size_t               data_len;
  size_t               count;
  ares_conn_err_t      err;
  ares_status_t        status;
  ares_bool_t          tfo = ARES_FALSE;

  if (conn == NULL) {
    return ARES_EFORMERR;
  }

  if (conn->flags & ARES_CONN_FLAG_TFO_INITIAL) {
    tfo = ARES_TRUE;
  }

  do {
    if (ares_buf_len(conn->out_buf) == 0) {
      status = ARES_SUCCESS;
      goto done;
    }

    if (conn->flags & ARES_CONN_FLAG_TCP) {
      data = ares_buf_peek(conn->out_buf, &data_len);
    } else {
      unsigned short msg_len;

      /* Read length, then provide buffer without length */
      ares_buf_tag(conn->out_buf);
      status = ares_buf_fetch_be16(conn->out_buf, &msg_len);
      if (status != ARES_SUCCESS) {
        return status;
      }
      ares_buf_tag_rollback(conn->out_buf);

      data = ares_buf_peek(conn->out_buf, &data_len);
      if (data_len < (size_t)(msg_len + 2)) {
        status = ARES_EFORMERR;
        goto done;
      }
      data     += 2;
      data_len  = msg_len;
    }

    err = ares_conn_write(conn, data, data_len, &count);
    if (err != ARES_CONN_ERR_SUCCESS) {
      if (err != ARES_CONN_ERR_WOULDBLOCK) {
        status = ARES_ECONNREFUSED;
        goto done;
      }
      status = ARES_SUCCESS;
      goto done;
    }

    /* UDP didn't send the length prefix so augment that here */
    if (!(conn->flags & ARES_CONN_FLAG_TCP)) {
      count += 2;
    }

    /* Strip data written from the buffer */
    ares_buf_consume(conn->out_buf, count);
    status = ARES_SUCCESS;

    /* Loop only for UDP since we have to send per-packet.  We already
     * sent everything we could if using tcp */
  } while (!(conn->flags & ARES_CONN_FLAG_TCP));

done:
  if (status == ARES_SUCCESS) {
    ares_conn_state_flags_t flags = ARES_CONN_STATE_READ;

    /* When using TFO, the we need to enabling waiting on a write event to
     * be notified of when a connection is actually established */
    if (tfo) {
      flags |= ARES_CONN_STATE_WRITE;
    }

    /* If using TCP and not all data was written (partial write), that means
     * we need to also wait on a write event */
    if (conn->flags & ARES_CONN_FLAG_TCP && ares_buf_len(conn->out_buf)) {
      flags |= ARES_CONN_STATE_WRITE;
    }

    ares_conn_sock_state_cb_update(conn, flags);
  }

  return status;
}

static ares_status_t ares_conn_connect(ares_conn_t           *conn,
                                       const struct sockaddr *sa,
                                       ares_socklen_t         salen)
{
  ares_conn_err_t err;

  err = ares_socket_connect(
    conn->server->channel, conn->fd,
    (conn->flags & ARES_CONN_FLAG_TFO) ? ARES_TRUE : ARES_FALSE, sa, salen);

  if (err != ARES_CONN_ERR_WOULDBLOCK && err != ARES_CONN_ERR_SUCCESS) {
    return ARES_ECONNREFUSED;
  }
  return ARES_SUCCESS;
}

ares_status_t ares_open_connection(ares_conn_t   **conn_out,
                                   ares_channel_t *channel,
                                   ares_server_t *server, ares_bool_t is_tcp)
{
  ares_status_t           status;
  struct sockaddr_storage sa_storage;
  ares_socklen_t          salen = sizeof(sa_storage);
  struct sockaddr        *sa    = (struct sockaddr *)&sa_storage;
  ares_conn_t            *conn;
  ares_llist_node_t      *node  = NULL;
  int                     stype = is_tcp ? SOCK_STREAM : SOCK_DGRAM;
  ares_conn_state_flags_t state_flags;

  *conn_out = NULL;

  conn = ares_malloc(sizeof(*conn));
  if (conn == NULL) {
    return ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  memset(conn, 0, sizeof(*conn));
  conn->fd              = ARES_SOCKET_BAD;
  conn->server          = server;
  conn->queries_to_conn = ares_llist_create(NULL);
  conn->flags           = is_tcp ? ARES_CONN_FLAG_TCP : ARES_CONN_FLAG_NONE;
  conn->out_buf         = ares_buf_create();
  conn->in_buf          = ares_buf_create();

  if (conn->queries_to_conn == NULL || conn->out_buf == NULL ||
      conn->in_buf == NULL) {
    /* LCOV_EXCL_START: OutOfMemory */
    status = ARES_ENOMEM;
    goto done;
    /* LCOV_EXCL_STOP */
  }

  /* Try to enable TFO always if using TCP. it will fail later on if its
   * really not supported when we try to enable it on the socket. */
  if (conn->flags & ARES_CONN_FLAG_TCP) {
    conn->flags |= ARES_CONN_FLAG_TFO;
  }

  /* Convert into the struct sockaddr structure needed by the OS */
  status = ares_conn_set_sockaddr(conn, sa, &salen);
  if (status != ARES_SUCCESS) {
    goto done;
  }

  /* Acquire a socket. */
  if (ares_socket_open(&conn->fd, channel, server->addr.family, stype, 0) !=
      ARES_CONN_ERR_SUCCESS) {
    status = ARES_ECONNREFUSED;
    goto done;
  }

  /* Configure channel configured options */
  status = ares_socket_configure(
    channel, server->addr.family,
    (conn->flags & ARES_CONN_FLAG_TCP) ? ARES_TRUE : ARES_FALSE, conn->fd);
  if (status != ARES_SUCCESS) {
    goto done;
  }

  /* Enable TFO if possible */
  if (conn->flags & ARES_CONN_FLAG_TFO &&
      ares_socket_enable_tfo(channel, conn->fd) != ARES_CONN_ERR_SUCCESS) {
    conn->flags &= ~((unsigned int)ARES_CONN_FLAG_TFO);
  }

  if (channel->sock_config_cb) {
    int err =
      channel->sock_config_cb(conn->fd, stype, channel->sock_config_cb_data);
    if (err < 0) {
      status = ARES_ECONNREFUSED;
      goto done;
    }
  }

  /* Connect */
  status = ares_conn_connect(conn, sa, salen);
  if (status != ARES_SUCCESS) {
    goto done;
  }

  if (channel->sock_create_cb) {
    int err =
      channel->sock_create_cb(conn->fd, stype, channel->sock_create_cb_data);
    if (err < 0) {
      status = ARES_ECONNREFUSED;
      goto done;
    }
  }

  /* Let the connection know we haven't written our first packet yet for TFO */
  if (conn->flags & ARES_CONN_FLAG_TFO) {
    conn->flags |= ARES_CONN_FLAG_TFO_INITIAL;
  }

  /* Need to store our own ip for DNS cookie support */
  status = ares_conn_set_self_ip(conn, ARES_TRUE);
  if (status != ARES_SUCCESS) {
    goto done; /* LCOV_EXCL_LINE: UntestablePath */
  }

  /* TCP connections are thrown to the end as we don't spawn multiple TCP
   * connections. UDP connections are put on front where the newest connection
   * can be quickly pulled */
  if (is_tcp) {
    node = ares_llist_insert_last(server->connections, conn);
  } else {
    node = ares_llist_insert_first(server->connections, conn);
  }
  if (node == NULL) {
    /* LCOV_EXCL_START: OutOfMemory */
    status = ARES_ENOMEM;
    goto done;
    /* LCOV_EXCL_STOP */
  }

  /* Register globally to quickly map event on file descriptor to connection
   * node object */
  if (!ares_htable_asvp_insert(channel->connnode_by_socket, conn->fd, node)) {
    /* LCOV_EXCL_START: OutOfMemory */
    status = ARES_ENOMEM;
    goto done;
    /* LCOV_EXCL_STOP */
  }

  state_flags = ARES_CONN_STATE_READ;

  /* Get notified on connect if using TCP */
  if (conn->flags & ARES_CONN_FLAG_TCP) {
    state_flags |= ARES_CONN_STATE_WRITE;
  }

  /* Dot no attempt to update sock state callbacks on TFO until *after* the
   * initial write is performed.  Due to the notification event, its possible
   * an erroneous read can come in before the attempt to write the data which
   * might be used to set the ip address */
  if (!(conn->flags & ARES_CONN_FLAG_TFO_INITIAL)) {
    ares_conn_sock_state_cb_update(conn, state_flags);
  }

  if (is_tcp) {
    server->tcp_conn = conn;
  }

done:
  if (status != ARES_SUCCESS) {
    ares_llist_node_claim(node);
    ares_llist_destroy(conn->queries_to_conn);
    ares_socket_close(channel, conn->fd);
    ares_buf_destroy(conn->out_buf);
    ares_buf_destroy(conn->in_buf);
    ares_free(conn);
  } else {
    *conn_out = conn;
  }
  return status;
}

ares_conn_t *ares_conn_from_fd(const ares_channel_t *channel, ares_socket_t fd)
{
  ares_llist_node_t *node;

  node = ares_htable_asvp_get_direct(channel->connnode_by_socket, fd);
  if (node == NULL) {
    return NULL;
  }

  return ares_llist_node_val(node);
}
