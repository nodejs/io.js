/*
 * nghttp2 - HTTP/2 C Library
 *
 * Copyright (c) 2012 Tatsuhiro Tsujikawa
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
#include "nghttp2_stream.h"

#include <assert.h>

#include "nghttp2_session.h"
#include "nghttp2_helper.h"
#include "nghttp2_debug.h"
#include "nghttp2_frame.h"

void nghttp2_stream_init(nghttp2_stream *stream, int32_t stream_id,
                         uint8_t flags, nghttp2_stream_state initial_state,
                         int32_t remote_initial_window_size,
                         int32_t local_initial_window_size,
                         void *stream_user_data) {
  stream->stream_id = stream_id;
  stream->flags = flags;
  stream->state = initial_state;
  stream->shut_flags = NGHTTP2_SHUT_NONE;
  stream->stream_user_data = stream_user_data;
  stream->item = NULL;
  stream->remote_window_size = remote_initial_window_size;
  stream->local_window_size = local_initial_window_size;
  stream->recv_window_size = 0;
  stream->consumed_size = 0;
  stream->recv_reduction = 0;
  stream->window_update_queued = 0;

  stream->closed_next = NULL;

  stream->http_flags = NGHTTP2_HTTP_FLAG_NONE;
  stream->content_length = -1;
  stream->recv_content_length = 0;
  stream->status_code = -1;

  stream->queued = 0;
  stream->cycle = 0;
  stream->pending_penalty = 0;
  stream->seq = 0;
  stream->last_writelen = 0;

  stream->extpri = stream->http_extpri = NGHTTP2_EXTPRI_DEFAULT_URGENCY;
}

void nghttp2_stream_free(nghttp2_stream *stream) { (void)stream; }

void nghttp2_stream_shutdown(nghttp2_stream *stream, nghttp2_shut_flag flag) {
  stream->shut_flags = (uint8_t)(stream->shut_flags | flag);
}

void nghttp2_stream_attach_item(nghttp2_stream *stream,
                                nghttp2_outbound_item *item) {
  assert((stream->flags & NGHTTP2_STREAM_FLAG_DEFERRED_ALL) == 0);
  assert(stream->item == NULL);

  DEBUGF("stream: stream=%d attach item=%p\n", stream->stream_id, item);

  stream->item = item;
}

void nghttp2_stream_detach_item(nghttp2_stream *stream) {
  DEBUGF("stream: stream=%d detach item=%p\n", stream->stream_id, stream->item);

  stream->item = NULL;
  stream->flags = (uint8_t)(stream->flags & ~NGHTTP2_STREAM_FLAG_DEFERRED_ALL);
}

void nghttp2_stream_defer_item(nghttp2_stream *stream, uint8_t flags) {
  assert(stream->item);

  DEBUGF("stream: stream=%d defer item=%p cause=%02x\n", stream->stream_id,
         stream->item, flags);

  stream->flags |= flags;
}

void nghttp2_stream_resume_deferred_item(nghttp2_stream *stream,
                                         uint8_t flags) {
  assert(stream->item);

  DEBUGF("stream: stream=%d resume item=%p flags=%02x\n", stream->stream_id,
         stream->item, flags);

  stream->flags = (uint8_t)(stream->flags & ~flags);
}

int nghttp2_stream_check_deferred_item(nghttp2_stream *stream) {
  return stream->item && (stream->flags & NGHTTP2_STREAM_FLAG_DEFERRED_ALL);
}

int nghttp2_stream_check_deferred_by_flow_control(nghttp2_stream *stream) {
  return stream->item &&
         (stream->flags & NGHTTP2_STREAM_FLAG_DEFERRED_FLOW_CONTROL);
}

static int update_initial_window_size(int32_t *window_size_ptr,
                                      int32_t new_initial_window_size,
                                      int32_t old_initial_window_size) {
  int64_t new_window_size = (int64_t)(*window_size_ptr) +
                            new_initial_window_size - old_initial_window_size;
  if (INT32_MIN > new_window_size ||
      new_window_size > NGHTTP2_MAX_WINDOW_SIZE) {
    return -1;
  }
  *window_size_ptr = (int32_t)new_window_size;
  return 0;
}

int nghttp2_stream_update_remote_initial_window_size(
  nghttp2_stream *stream, int32_t new_initial_window_size,
  int32_t old_initial_window_size) {
  return update_initial_window_size(&stream->remote_window_size,
                                    new_initial_window_size,
                                    old_initial_window_size);
}

int nghttp2_stream_update_local_initial_window_size(
  nghttp2_stream *stream, int32_t new_initial_window_size,
  int32_t old_initial_window_size) {
  return update_initial_window_size(&stream->local_window_size,
                                    new_initial_window_size,
                                    old_initial_window_size);
}

void nghttp2_stream_promise_fulfilled(nghttp2_stream *stream) {
  stream->state = NGHTTP2_STREAM_OPENED;
  stream->flags = (uint8_t)(stream->flags & ~NGHTTP2_STREAM_FLAG_PUSH);
}

nghttp2_stream_proto_state nghttp2_stream_get_state(nghttp2_stream *stream) {
  if (stream == &root) {
    return NGHTTP2_STREAM_STATE_IDLE;
  }

  if (stream->flags & NGHTTP2_STREAM_FLAG_CLOSED) {
    return NGHTTP2_STREAM_STATE_CLOSED;
  }

  if (stream->flags & NGHTTP2_STREAM_FLAG_PUSH) {
    if (stream->shut_flags & NGHTTP2_SHUT_RD) {
      return NGHTTP2_STREAM_STATE_RESERVED_LOCAL;
    }

    if (stream->shut_flags & NGHTTP2_SHUT_WR) {
      return NGHTTP2_STREAM_STATE_RESERVED_REMOTE;
    }
  }

  if (stream->shut_flags & NGHTTP2_SHUT_RD) {
    return NGHTTP2_STREAM_STATE_HALF_CLOSED_REMOTE;
  }

  if (stream->shut_flags & NGHTTP2_SHUT_WR) {
    return NGHTTP2_STREAM_STATE_HALF_CLOSED_LOCAL;
  }

  if (stream->state == NGHTTP2_STREAM_IDLE) {
    return NGHTTP2_STREAM_STATE_IDLE;
  }

  return NGHTTP2_STREAM_STATE_OPEN;
}

nghttp2_stream *nghttp2_stream_get_parent(nghttp2_stream *stream) {
  (void)stream;

  return NULL;
}

nghttp2_stream *nghttp2_stream_get_next_sibling(nghttp2_stream *stream) {
  (void)stream;

  return NULL;
}

nghttp2_stream *nghttp2_stream_get_previous_sibling(nghttp2_stream *stream) {
  (void)stream;

  return NULL;
}

nghttp2_stream *nghttp2_stream_get_first_child(nghttp2_stream *stream) {
  (void)stream;

  return NULL;
}

int32_t nghttp2_stream_get_weight(nghttp2_stream *stream) {
  (void)stream;

  return NGHTTP2_DEFAULT_WEIGHT;
}

int32_t nghttp2_stream_get_sum_dependency_weight(nghttp2_stream *stream) {
  (void)stream;

  return 0;
}

int32_t nghttp2_stream_get_stream_id(nghttp2_stream *stream) {
  return stream->stream_id;
}
