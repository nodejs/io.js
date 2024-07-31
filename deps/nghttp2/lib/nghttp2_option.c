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
#include "nghttp2_option.h"

#include "nghttp2_session.h"

int nghttp2_option_new(nghttp2_option **option_ptr) {
  *option_ptr = calloc(1, sizeof(nghttp2_option));

  if (*option_ptr == NULL) {
    return NGHTTP2_ERR_NOMEM;
  }

  return 0;
}

void nghttp2_option_del(nghttp2_option *option) { free(option); }

void nghttp2_option_set_no_auto_window_update(nghttp2_option *option, int val) {
  option->opt_set_mask |= NGHTTP2_OPT_NO_AUTO_WINDOW_UPDATE;
  option->no_auto_window_update = val;
}

void nghttp2_option_set_peer_max_concurrent_streams(nghttp2_option *option,
                                                    uint32_t val) {
  option->opt_set_mask |= NGHTTP2_OPT_PEER_MAX_CONCURRENT_STREAMS;
  option->peer_max_concurrent_streams = val;
}

void nghttp2_option_set_no_recv_client_magic(nghttp2_option *option, int val) {
  option->opt_set_mask |= NGHTTP2_OPT_NO_RECV_CLIENT_MAGIC;
  option->no_recv_client_magic = val;
}

void nghttp2_option_set_no_http_messaging(nghttp2_option *option, int val) {
  option->opt_set_mask |= NGHTTP2_OPT_NO_HTTP_MESSAGING;
  option->no_http_messaging = val;
}

void nghttp2_option_set_max_reserved_remote_streams(nghttp2_option *option,
                                                    uint32_t val) {
  option->opt_set_mask |= NGHTTP2_OPT_MAX_RESERVED_REMOTE_STREAMS;
  option->max_reserved_remote_streams = val;
}

static void set_ext_type(uint8_t *ext_types, uint8_t type) {
  ext_types[type / 8] = (uint8_t)(ext_types[type / 8] | (1 << (type & 0x7)));
}

void nghttp2_option_set_user_recv_extension_type(nghttp2_option *option,
                                                 uint8_t type) {
  if (type < 10) {
    return;
  }

  option->opt_set_mask |= NGHTTP2_OPT_USER_RECV_EXT_TYPES;
  set_ext_type(option->user_recv_ext_types, type);
}

void nghttp2_option_set_builtin_recv_extension_type(nghttp2_option *option,
                                                    uint8_t type) {
  switch (type) {
  case NGHTTP2_ALTSVC:
    option->opt_set_mask |= NGHTTP2_OPT_BUILTIN_RECV_EXT_TYPES;
    option->builtin_recv_ext_types |= NGHTTP2_TYPEMASK_ALTSVC;
    return;
  case NGHTTP2_ORIGIN:
    option->opt_set_mask |= NGHTTP2_OPT_BUILTIN_RECV_EXT_TYPES;
    option->builtin_recv_ext_types |= NGHTTP2_TYPEMASK_ORIGIN;
    return;
  case NGHTTP2_PRIORITY_UPDATE:
    option->opt_set_mask |= NGHTTP2_OPT_BUILTIN_RECV_EXT_TYPES;
    option->builtin_recv_ext_types |= NGHTTP2_TYPEMASK_PRIORITY_UPDATE;
    return;
  default:
    return;
  }
}

void nghttp2_option_set_no_auto_ping_ack(nghttp2_option *option, int val) {
  option->opt_set_mask |= NGHTTP2_OPT_NO_AUTO_PING_ACK;
  option->no_auto_ping_ack = val;
}

void nghttp2_option_set_max_send_header_block_length(nghttp2_option *option,
                                                     size_t val) {
  option->opt_set_mask |= NGHTTP2_OPT_MAX_SEND_HEADER_BLOCK_LENGTH;
  option->max_send_header_block_length = val;
}

void nghttp2_option_set_max_deflate_dynamic_table_size(nghttp2_option *option,
                                                       size_t val) {
  option->opt_set_mask |= NGHTTP2_OPT_MAX_DEFLATE_DYNAMIC_TABLE_SIZE;
  option->max_deflate_dynamic_table_size = val;
}

void nghttp2_option_set_no_closed_streams(nghttp2_option *option, int val) {
  option->opt_set_mask |= NGHTTP2_OPT_NO_CLOSED_STREAMS;
  option->no_closed_streams = val;
}

void nghttp2_option_set_max_outbound_ack(nghttp2_option *option, size_t val) {
  option->opt_set_mask |= NGHTTP2_OPT_MAX_OUTBOUND_ACK;
  option->max_outbound_ack = val;
}

void nghttp2_option_set_max_settings(nghttp2_option *option, size_t val) {
  option->opt_set_mask |= NGHTTP2_OPT_MAX_SETTINGS;
  option->max_settings = val;
}

void nghttp2_option_set_server_fallback_rfc7540_priorities(
    nghttp2_option *option, int val) {
  option->opt_set_mask |= NGHTTP2_OPT_SERVER_FALLBACK_RFC7540_PRIORITIES;
  option->server_fallback_rfc7540_priorities = val;
}

void nghttp2_option_set_no_rfc9113_leading_and_trailing_ws_validation(
    nghttp2_option *option, int val) {
  option->opt_set_mask |=
      NGHTTP2_OPT_NO_RFC9113_LEADING_AND_TRAILING_WS_VALIDATION;
  option->no_rfc9113_leading_and_trailing_ws_validation = val;
}

void nghttp2_option_set_stream_reset_rate_limit(nghttp2_option *option,
                                                uint64_t burst, uint64_t rate) {
  option->opt_set_mask |= NGHTTP2_OPT_STREAM_RESET_RATE_LIMIT;
  option->stream_reset_burst = burst;
  option->stream_reset_rate = rate;
}

void nghttp2_option_set_max_continuations(nghttp2_option *option, size_t val) {
  option->opt_set_mask |= NGHTTP2_OPT_MAX_CONTINUATIONS;
  option->max_continuations = val;
}
