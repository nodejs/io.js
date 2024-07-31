/*
 * nghttp2 - HTTP/2 C Library
 *
 * Copyright (c) 2014 Tatsuhiro Tsujikawa
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef NGHTTP2_OPTION_H
#define NGHTTP2_OPTION_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <nghttp2/nghttp2.h>

/**
 * Configuration options
 */
typedef enum {
  /**
   * This option prevents the library from sending WINDOW_UPDATE for a
   * connection automatically.  If this option is set to nonzero, the
   * library won't send WINDOW_UPDATE for DATA until application calls
   * nghttp2_session_consume() to indicate the amount of consumed
   * DATA.  By default, this option is set to zero.
   */
  NGHTTP2_OPT_NO_AUTO_WINDOW_UPDATE = 1,
  /**
   * This option sets the SETTINGS_MAX_CONCURRENT_STREAMS value of
   * remote endpoint as if it is received in SETTINGS frame. Without
   * specifying this option, before the local endpoint receives
   * SETTINGS_MAX_CONCURRENT_STREAMS in SETTINGS frame from remote
   * endpoint, SETTINGS_MAX_CONCURRENT_STREAMS is unlimited. This may
   * cause problem if local endpoint submits lots of requests
   * initially and sending them at once to the remote peer may lead to
   * the rejection of some requests. Specifying this option to the
   * sensible value, say 100, may avoid this kind of issue. This value
   * will be overwritten if the local endpoint receives
   * SETTINGS_MAX_CONCURRENT_STREAMS from the remote endpoint.
   */
  NGHTTP2_OPT_PEER_MAX_CONCURRENT_STREAMS = 1 << 1,
  NGHTTP2_OPT_NO_RECV_CLIENT_MAGIC = 1 << 2,
  NGHTTP2_OPT_NO_HTTP_MESSAGING = 1 << 3,
  NGHTTP2_OPT_MAX_RESERVED_REMOTE_STREAMS = 1 << 4,
  NGHTTP2_OPT_USER_RECV_EXT_TYPES = 1 << 5,
  NGHTTP2_OPT_NO_AUTO_PING_ACK = 1 << 6,
  NGHTTP2_OPT_BUILTIN_RECV_EXT_TYPES = 1 << 7,
  NGHTTP2_OPT_MAX_SEND_HEADER_BLOCK_LENGTH = 1 << 8,
  NGHTTP2_OPT_MAX_DEFLATE_DYNAMIC_TABLE_SIZE = 1 << 9,
  NGHTTP2_OPT_NO_CLOSED_STREAMS = 1 << 10,
  NGHTTP2_OPT_MAX_OUTBOUND_ACK = 1 << 11,
  NGHTTP2_OPT_MAX_SETTINGS = 1 << 12,
  NGHTTP2_OPT_SERVER_FALLBACK_RFC7540_PRIORITIES = 1 << 13,
  NGHTTP2_OPT_NO_RFC9113_LEADING_AND_TRAILING_WS_VALIDATION = 1 << 14,
  NGHTTP2_OPT_STREAM_RESET_RATE_LIMIT = 1 << 15,
  NGHTTP2_OPT_MAX_CONTINUATIONS = 1 << 16,
} nghttp2_option_flag;

/**
 * Struct to store option values for nghttp2_session.
 */
struct nghttp2_option {
  /**
   * NGHTTP2_OPT_STREAM_RESET_RATE_LIMIT
   */
  uint64_t stream_reset_burst;
  uint64_t stream_reset_rate;
  /**
   * NGHTTP2_OPT_MAX_SEND_HEADER_BLOCK_LENGTH
   */
  size_t max_send_header_block_length;
  /**
   * NGHTTP2_OPT_MAX_DEFLATE_DYNAMIC_TABLE_SIZE
   */
  size_t max_deflate_dynamic_table_size;
  /**
   * NGHTTP2_OPT_MAX_OUTBOUND_ACK
   */
  size_t max_outbound_ack;
  /**
   * NGHTTP2_OPT_MAX_SETTINGS
   */
  size_t max_settings;
  /**
   * NGHTTP2_OPT_MAX_CONTINUATIONS
   */
  size_t max_continuations;
  /**
   * Bitwise OR of nghttp2_option_flag to determine that which fields
   * are specified.
   */
  uint32_t opt_set_mask;
  /**
   * NGHTTP2_OPT_PEER_MAX_CONCURRENT_STREAMS
   */
  uint32_t peer_max_concurrent_streams;
  /**
   * NGHTTP2_OPT_MAX_RESERVED_REMOTE_STREAMS
   */
  uint32_t max_reserved_remote_streams;
  /**
   * NGHTTP2_OPT_BUILTIN_RECV_EXT_TYPES
   */
  uint32_t builtin_recv_ext_types;
  /**
   * NGHTTP2_OPT_NO_AUTO_WINDOW_UPDATE
   */
  int no_auto_window_update;
  /**
   * NGHTTP2_OPT_NO_RECV_CLIENT_MAGIC
   */
  int no_recv_client_magic;
  /**
   * NGHTTP2_OPT_NO_HTTP_MESSAGING
   */
  int no_http_messaging;
  /**
   * NGHTTP2_OPT_NO_AUTO_PING_ACK
   */
  int no_auto_ping_ack;
  /**
   * NGHTTP2_OPT_NO_CLOSED_STREAMS
   */
  int no_closed_streams;
  /**
   * NGHTTP2_OPT_SERVER_FALLBACK_RFC7540_PRIORITIES
   */
  int server_fallback_rfc7540_priorities;
  /**
   * NGHTTP2_OPT_NO_RFC9113_LEADING_AND_TRAILING_WS_VALIDATION
   */
  int no_rfc9113_leading_and_trailing_ws_validation;
  /**
   * NGHTTP2_OPT_USER_RECV_EXT_TYPES
   */
  uint8_t user_recv_ext_types[32];
};

#endif /* NGHTTP2_OPTION_H */
