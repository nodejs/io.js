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
#ifndef __ARES_CONN_H
#define __ARES_CONN_H

#include "ares_socket.h"

struct ares_conn;
typedef struct ares_conn ares_conn_t;

struct ares_server;
typedef struct ares_server ares_server_t;

typedef enum {
  /*! No flags */
  ARES_CONN_FLAG_NONE = 0,
  /*! TCP connection, not UDP */
  ARES_CONN_FLAG_TCP = 1 << 0,
  /*! TCP Fast Open is enabled and being used if supported by the OS */
  ARES_CONN_FLAG_TFO = 1 << 1,
  /*! TCP Fast Open has not yet sent its first packet. Gets unset on first
   *  write to a connection */
  ARES_CONN_FLAG_TFO_INITIAL = 1 << 2
} ares_conn_flags_t;

typedef enum {
  ARES_CONN_STATE_NONE      = 0,
  ARES_CONN_STATE_READ      = 1 << 0,
  ARES_CONN_STATE_WRITE     = 1 << 1,
  ARES_CONN_STATE_CONNECTED = 1 << 2, /* This doesn't get a callback */
  ARES_CONN_STATE_CBFLAGS   = ARES_CONN_STATE_READ | ARES_CONN_STATE_WRITE
} ares_conn_state_flags_t;

struct ares_conn {
  ares_server_t          *server;
  ares_socket_t           fd;
  struct ares_addr        self_ip;
  ares_conn_flags_t       flags;
  ares_conn_state_flags_t state_flags;

  /*! Outbound buffered data that is not yet sent.  Exists as one contiguous
   *  stream in TCP format (big endian 16bit length prefix followed by DNS
   *  wire-format message).  For TCP this can be sent as-is, UDP this must
   *  be sent per-packet (stripping the length prefix) */
  ares_buf_t             *out_buf;

  /*! Inbound buffered data that is not yet parsed.  Exists as one contiguous
   *  stream in TCP format (big endian 16bit length prefix followed by DNS
   *  wire-format message).  TCP may have partial data and this needs to be
   *  handled gracefully, but UDP will always have a full message */
  ares_buf_t             *in_buf;

  /* total number of queries run on this connection since it was established */
  size_t                  total_queries;

  /* list of outstanding queries to this connection */
  ares_llist_t           *queries_to_conn;
};

/*! Various buckets for grouping history */
typedef enum {
  ARES_METRIC_1MINUTE = 0, /*!< Bucket for tracking over the last minute */
  ARES_METRIC_15MINUTES,   /*!< Bucket for tracking over the last 15 minutes */
  ARES_METRIC_1HOUR,       /*!< Bucket for tracking over the last hour */
  ARES_METRIC_1DAY,        /*!< Bucket for tracking over the last day */
  ARES_METRIC_INCEPTION,   /*!< Bucket for tracking since inception */
  ARES_METRIC_COUNT        /*!< Count of buckets, not a real bucket */
} ares_server_bucket_t;

/*! Data metrics collected for each bucket */
typedef struct {
  time_t        ts;             /*!< Timestamp divided by bucket divisor */
  unsigned int  latency_min_ms; /*!< Minimum latency for queries */
  unsigned int  latency_max_ms; /*!< Maximum latency for queries */
  ares_uint64_t total_ms;       /*!< Cumulative query time for bucket */
  ares_uint64_t total_count;    /*!< Number of queries for bucket */

  time_t        prev_ts;        /*!< Previous period bucket timestamp */
  ares_uint64_t
    prev_total_ms; /*!< Previous period bucket cumulative query time */
  ares_uint64_t prev_total_count; /*!< Previous period bucket query count */
} ares_server_metrics_t;

typedef enum {
  ARES_COOKIE_INITIAL     = 0,
  ARES_COOKIE_GENERATED   = 1,
  ARES_COOKIE_SUPPORTED   = 2,
  ARES_COOKIE_UNSUPPORTED = 3
} ares_cookie_state_t;

/*! Structure holding tracking data for RFC 7873/9018 DNS cookies.
 *  Implementation plan for this feature is here:
 *  https://github.com/c-ares/c-ares/issues/620
 */
typedef struct {
  /*! starts at INITIAL, transitions as needed. */
  ares_cookie_state_t state;
  /*! randomly-generate client cookie */
  unsigned char       client[8];
  /*! timestamp client cookie was generated, used for rotation purposes */
  ares_timeval_t      client_ts;
  /*! IP address last used for client to connect to server.  If this changes
   *  The client cookie gets invalidated */
  struct ares_addr    client_ip;
  /*! Server Cookie last received, 8-32 bytes in length */
  unsigned char       server[32];
  /*! Length of server cookie on file. */
  size_t              server_len;
  /*! Timestamp of last attempt to use cookies, but it was determined that the
   *  server didn't support them */
  ares_timeval_t      unsupported_ts;
} ares_cookie_t;

struct ares_server {
  /* Configuration */
  size_t                idx;      /* index for server in system configuration */
  struct ares_addr      addr;
  unsigned short        udp_port; /* host byte order */
  unsigned short        tcp_port; /* host byte order */
  char                  ll_iface[64];    /* IPv6 Link Local Interface */
  unsigned int          ll_scope;        /* IPv6 Link Local Scope */

  size_t                consec_failures; /* Consecutive query failure count
                                          * can be hard errors or timeouts
                                          */
  ares_bool_t           probe_pending;   /* Whether a probe is pending for this
                                          * server due to prior failures */
  ares_llist_t         *connections;
  ares_conn_t          *tcp_conn;

  /* The next time when we will retry this server if it has hit failures */
  ares_timeval_t        next_retry_time;

  /*! Buckets for collecting metrics about the server */
  ares_server_metrics_t metrics[ARES_METRIC_COUNT];

  /*! RFC 7873/9018 DNS Cookies */
  ares_cookie_t         cookie;

  /* Link back to owning channel */
  ares_channel_t       *channel;
};

void ares_close_connection(ares_conn_t *conn, ares_status_t requeue_status);
void ares_close_sockets(ares_server_t *server);
void ares_check_cleanup_conns(const ares_channel_t *channel);

void ares_destroy_servers_state(ares_channel_t *channel);
ares_status_t   ares_open_connection(ares_conn_t   **conn_out,
                                     ares_channel_t *channel,
                                     ares_server_t *server, ares_bool_t is_tcp);

ares_conn_err_t ares_conn_write(ares_conn_t *conn, const void *data, size_t len,
                                size_t *written);
ares_status_t   ares_conn_flush(ares_conn_t *conn);
ares_conn_err_t ares_conn_read(ares_conn_t *conn, void *data, size_t len,
                               size_t *read_bytes);
ares_conn_t *ares_conn_from_fd(const ares_channel_t *channel, ares_socket_t fd);
void         ares_conn_sock_state_cb_update(ares_conn_t            *conn,
                                            ares_conn_state_flags_t flags);
ares_conn_err_t ares_socket_recv(ares_channel_t *channel, ares_socket_t s,
                                 ares_bool_t is_tcp, void *data,
                                 size_t data_len, size_t *read_bytes);
ares_conn_err_t ares_socket_recvfrom(ares_channel_t *channel, ares_socket_t s,
                                     ares_bool_t is_tcp, void *data,
                                     size_t data_len, int flags,
                                     struct sockaddr *from,
                                     ares_socklen_t  *from_len,
                                     size_t          *read_bytes);

void            ares_destroy_server(ares_server_t *server);

#endif
