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
#include "nghttp2_outbound_item.h"

#include <assert.h>
#include <string.h>

nghttp2_data_provider_wrap *
nghttp2_data_provider_wrap_v1(nghttp2_data_provider_wrap *dpw,
                              const nghttp2_data_provider *data_prd) {
  if (!data_prd) {
    return NULL;
  }

  dpw->version = NGHTTP2_DATA_PROVIDER_V1;
  dpw->data_prd.v1 = *data_prd;

  return dpw;
}

nghttp2_data_provider_wrap *
nghttp2_data_provider_wrap_v2(nghttp2_data_provider_wrap *dpw,
                              const nghttp2_data_provider2 *data_prd) {
  if (!data_prd) {
    return NULL;
  }

  dpw->version = NGHTTP2_DATA_PROVIDER_V2;
  dpw->data_prd.v2 = *data_prd;

  return dpw;
}

void nghttp2_outbound_item_init(nghttp2_outbound_item *item) {
  item->cycle = 0;
  item->qnext = NULL;
  item->queued = 0;

  memset(&item->aux_data, 0, sizeof(nghttp2_aux_data));
}

void nghttp2_outbound_item_free(nghttp2_outbound_item *item, nghttp2_mem *mem) {
  nghttp2_frame *frame;

  if (item == NULL) {
    return;
  }

  frame = &item->frame;

  switch (frame->hd.type) {
  case NGHTTP2_DATA:
    nghttp2_frame_data_free(&frame->data);
    break;
  case NGHTTP2_HEADERS:
    nghttp2_frame_headers_free(&frame->headers, mem);
    break;
  case NGHTTP2_PRIORITY:
    nghttp2_frame_priority_free(&frame->priority);
    break;
  case NGHTTP2_RST_STREAM:
    nghttp2_frame_rst_stream_free(&frame->rst_stream);
    break;
  case NGHTTP2_SETTINGS:
    nghttp2_frame_settings_free(&frame->settings, mem);
    break;
  case NGHTTP2_PUSH_PROMISE:
    nghttp2_frame_push_promise_free(&frame->push_promise, mem);
    break;
  case NGHTTP2_PING:
    nghttp2_frame_ping_free(&frame->ping);
    break;
  case NGHTTP2_GOAWAY:
    nghttp2_frame_goaway_free(&frame->goaway, mem);
    break;
  case NGHTTP2_WINDOW_UPDATE:
    nghttp2_frame_window_update_free(&frame->window_update);
    break;
  default: {
    nghttp2_ext_aux_data *aux_data;

    aux_data = &item->aux_data.ext;

    if (aux_data->builtin == 0) {
      nghttp2_frame_extension_free(&frame->ext);
      break;
    }

    switch (frame->hd.type) {
    case NGHTTP2_ALTSVC:
      nghttp2_frame_altsvc_free(&frame->ext, mem);
      break;
    case NGHTTP2_ORIGIN:
      nghttp2_frame_origin_free(&frame->ext, mem);
      break;
    case NGHTTP2_PRIORITY_UPDATE:
      nghttp2_frame_priority_update_free(&frame->ext, mem);
      break;
    default:
      assert(0);
      break;
    }
  }
  }
}

void nghttp2_outbound_queue_init(nghttp2_outbound_queue *q) {
  q->head = q->tail = NULL;
  q->n = 0;
}

void nghttp2_outbound_queue_push(nghttp2_outbound_queue *q,
                                 nghttp2_outbound_item *item) {
  if (q->tail) {
    q->tail = q->tail->qnext = item;
  } else {
    q->head = q->tail = item;
  }
  ++q->n;
}

void nghttp2_outbound_queue_pop(nghttp2_outbound_queue *q) {
  nghttp2_outbound_item *item;
  if (!q->head) {
    return;
  }
  item = q->head;
  q->head = q->head->qnext;
  item->qnext = NULL;
  if (!q->head) {
    q->tail = NULL;
  }
  --q->n;
}
